// =====================================================
// ESP-NOW MIDI TRANSPORT (PALM -> wirelessToUSBmidi receiver)
// Unicast with per-packet ACK + app-level retry; every MIDI/heartbeat
// packet carries a sequence number so the receiver can detect gaps.
// Peer discovery: broadcast PING until the receiver answers PONG.
// =====================================================
#ifdef USE_ESPNOW_MIDI

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define ESPNOW_CHANNEL   1
#define ESPNOW_MAGIC     0xE7

// Shared secrets -- must be identical on ALL controllers and receivers
// (exo_receiver has the same values). Changing them means reflashing everything.
#define ESPNOW_PMK "vB7#kQ2pXw9!mZ4r"
#define ESPNOW_LMK "eXo5$Fh8@Ln1&Tc3"
#define TX_MAX_RETRIES   3    // app-level resends after a failed hardware ACK
#define LINK_FAIL_LIMIT  4    // consecutive lost packets -> back to discovery (~1 s idle)
#define PING_INTERVAL_MS 500
#define LINKED_PING_MS   3000 // keepalive PING while linked: lets a rebooted
                              // receiver re-learn our key-peer (its MAC-level
                              // ACKs would otherwise hide the reboot from us)
#define HEARTBEAT_MS     250  // keepalive when no MIDI is flowing
#define BLE_FALLBACK_MS  5000 // no receiver by then -> advertise BLE MIDI

enum : uint8_t { PKT_MIDI = 0, PKT_HEARTBEAT = 1, PKT_PING = 2, PKT_PONG = 3 };

typedef struct __attribute__((packed)) {
  uint8_t magic;
  uint8_t type;
  uint8_t seq;
  uint8_t len;         // payload bytes
  uint8_t payload[8];
} EspNowPacket;

bool espnowLinked = false;  // read by loop() for the status pixel

static uint8_t peerMac[6] = { 0 };
static uint8_t selfMac[6] = { 0 };
static const uint8_t broadcastMac[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static uint8_t txSeq = 0;

// TX queue: written from loop(), drained from loop() + ESP-NOW send callback
#define TXQ_SIZE 64
static EspNowPacket txq[TXQ_SIZE];
static volatile uint8_t txqHead = 0, txqTail = 0;
static volatile bool txInFlight = false;
static volatile bool txAttempted = false;  // txq[txqTail] has been sent at least once
static volatile bool txFailed = false;
static uint8_t txRetries = 0;
static uint8_t consecFails = 0;
static uint32_t lastPingMs = 0;
static uint32_t lastQueueMs = 0;

static uint32_t mon_espnow_ok = 0, mon_espnow_retry = 0, mon_espnow_lost = 0, mon_espnow_drop = 0;

static portMUX_TYPE espnowMux = portMUX_INITIALIZER_UNLOCKED;

// Downstream (receiver -> us) MIDI, e.g. light-control CCs from the host.
// Filled from the radio callback, dispatched from loop context.
#define DRXQ_SIZE 16
static uint8_t drxqLen[DRXQ_SIZE];
static uint8_t drxqData[DRXQ_SIZE][8];
static volatile uint8_t drxqHead = 0, drxqTail = 0;


// Decide next action under the lock, do the actual send outside it.
static void espnowPump() {
  const uint8_t* sendData = nullptr;
  uint8_t sendLen = 0;
  bool linkLost = false;

  portENTER_CRITICAL(&espnowMux);
  if (!txInFlight) {
    if (txAttempted) {
      if (txFailed && txRetries < TX_MAX_RETRIES) {
        txRetries++;
        mon_espnow_retry++;
      } else {
        if (txFailed) {
          mon_espnow_lost++;
          if (++consecFails >= LINK_FAIL_LIMIT) {
            espnowLinked = false;
            linkLost = true;
          }
        } else {
          mon_espnow_ok++;
          consecFails = 0;
        }
        txqTail = (uint8_t)((txqTail + 1) % TXQ_SIZE);
        txAttempted = false;
        txRetries = 0;
      }
    }
    if (espnowLinked && (txAttempted || txqTail != txqHead)) {
      txAttempted = true;
      txFailed = false;
      txInFlight = true;
      sendData = (const uint8_t*)&txq[txqTail];
      sendLen = 4 + txq[txqTail].len;
    }
  }
  portEXIT_CRITICAL(&espnowMux);

  if (linkLost) {
    Serial.println("espnow: link lost, searching...");
#ifdef USE_BLE_MIDI
    bleAdvertisingControl(true);  // receiver gone: offer BLE again
#endif
  }
  if (sendData && esp_now_send(peerMac, sendData, sendLen) != ESP_OK) {
    portENTER_CRITICAL(&espnowMux);
    txInFlight = false;
    txFailed = true;  // retried on the next pump from espnowLoop()
    portEXIT_CRITICAL(&espnowMux);
  }
}

static void espnowQueuePacket(uint8_t type, const uint8_t* data, uint8_t len) {
  if (len > sizeof(txq[0].payload)) return;
  bool doPump = false;
  portENTER_CRITICAL(&espnowMux);
  uint8_t next = (uint8_t)((txqHead + 1) % TXQ_SIZE);
  if (next == txqTail) {
    mon_espnow_drop++;  // queue full (link down for a while) -> drop newest
  } else {
    EspNowPacket& p = txq[txqHead];
    p.magic = ESPNOW_MAGIC;
    p.type = type;
    p.seq = txSeq++;
    p.len = len;
    if (len) memcpy(p.payload, data, len);
    txqHead = next;
    doPump = true;
  }
  portEXIT_CRITICAL(&espnowMux);
  lastQueueMs = millis();
  if (doPump) espnowPump();
}

static void espnowQueueMIDIMessage(const uint8_t* data, uint8_t len) {
  espnowQueuePacket(PKT_MIDI, data, len);
}

static void espnowOnRecv(const uint8_t* srcMac, const uint8_t* data, int len) {
  if (len < 4) return;
  const EspNowPacket* p = (const EspNowPacket*)data;
  if (p->magic != ESPNOW_MAGIC) return;

  if (p->type == PKT_MIDI) {
    // downstream MIDI from our linked receiver
    if (!espnowLinked || memcmp(srcMac, peerMac, 6) != 0) return;
    if (p->len == 0 || p->len > sizeof(drxqData[0]) || 4 + p->len > len) return;
    portENTER_CRITICAL(&espnowMux);
    uint8_t next = (uint8_t)((drxqHead + 1) % DRXQ_SIZE);
    if (next != drxqTail) {
      drxqLen[drxqHead] = p->len;
      memcpy(drxqData[drxqHead], p->payload, p->len);
      drxqHead = next;
    }
    portEXIT_CRITICAL(&espnowMux);
    return;
  }
  if (p->type != PKT_PONG) return;
  // PONG is broadcast (the receiver can't encrypt to us before we share a peer
  // entry) and names its addressee -- ignore PONGs meant for other controllers
  if (p->len != 6 || memcmp(p->payload, selfMac, 6) != 0) return;
  if (espnowLinked && memcmp(peerMac, srcMac, 6) == 0) return;  // duplicate

  // (re)register the receiver as our encrypted peer
  esp_now_del_peer(srcMac);  // ok if absent
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, srcMac, 6);
  peer.channel = ESPNOW_CHANNEL;
  peer.ifidx = WIFI_IF_STA;
  memcpy(peer.lmk, ESPNOW_LMK, 16);
  peer.encrypt = true;
  esp_now_add_peer(&peer);
  bool wasLinked;
  portENTER_CRITICAL(&espnowMux);
  memcpy(peerMac, srcMac, 6);
  wasLinked = espnowLinked;
  espnowLinked = true;
  consecFails = 0;
  if (!wasLinked && !txInFlight) {
    // discard MIDI queued while the link was down -- it is stale by now
    // and would replay seconds-old notes on reconnect
    txqTail = txqHead;
    txAttempted = false;
    txRetries = 0;
  }
  portEXIT_CRITICAL(&espnowMux);
  if (!wasLinked) Serial.println("espnow: linked");
#ifdef USE_BLE_MIDI
  bleAdvertisingControl(false);  // receiver wins: don't offer BLE alongside
  bleDropConnection();           // and hang up on a BLE host if one grabbed us
#endif
  espnowPump();
}

static void espnowOnSent(const uint8_t* dstMac, bool ok) {
  if (memcmp(dstMac, peerMac, 6) != 0) return;
  portENTER_CRITICAL(&espnowMux);
  txFailed = !ok;
  txInFlight = false;
  portEXIT_CRITICAL(&espnowMux);
  espnowPump();
}

static void espnowBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_ps(WIFI_PS_NONE);  // power save off: latency matters more
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_get_mac(WIFI_IF_STA, selfMac);
  esp_err_t initErr = esp_now_init();
  if (initErr != ESP_OK) Serial.printf("espnow: init FAILED (%d)\n", (int)initErr);
  esp_now_set_pmk((const uint8_t*)ESPNOW_PMK);

  // lambdas: keeps IDF types out of .ino function signatures (prototype generation)
  esp_now_register_recv_cb([](const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    espnowOnRecv(info->src_addr, data, len);
  });
  esp_now_register_send_cb([](const esp_now_send_info_t* info, esp_now_send_status_t status) {
    espnowOnSent(info->des_addr, status == ESP_NOW_SEND_SUCCESS);
  });

  esp_now_peer_info_t bcast = {};
  memcpy(bcast.peer_addr, broadcastMac, 6);
  bcast.channel = ESPNOW_CHANNEL;
  bcast.ifidx = WIFI_IF_STA;
  esp_now_add_peer(&bcast);
}

// PING carries our name so the receiver can bind us to a named USB port.
// Sent raw (not via txq): discovery must work before a peer exists, and the
// name (up to 16 bytes) wouldn't fit EspNowPacket's 8-byte payload.
static esp_err_t espnowSendPing() {
  uint8_t nameLen = (uint8_t)strlen(BLE_DEVICE_NAME);
  if (nameLen > 16) nameLen = 16;
  uint8_t pkt[20] = { ESPNOW_MAGIC, PKT_PING, 0, nameLen };
  memcpy(pkt + 4, BLE_DEVICE_NAME, nameLen);
  return esp_now_send(broadcastMac, pkt, (size_t)(4 + nameLen));
}

static void espnowLoop() {
  // dispatch downstream MIDI (loop context: handlers touch loop-owned globals)
  for (;;) {
    uint8_t buf[8], blen = 0;
    portENTER_CRITICAL(&espnowMux);
    if (drxqTail != drxqHead) {
      blen = drxqLen[drxqTail];
      memcpy(buf, drxqData[drxqTail], blen);
      drxqTail = (uint8_t)((drxqTail + 1) % DRXQ_SIZE);
    }
    portEXIT_CRITICAL(&espnowMux);
    if (!blen) break;
    // configurator SysEx streams in chunks; everything else is channel voice
    if (sysexActive() || buf[0] == 0xF0) {
      sysexFeed(buf, blen, 1 /* SX_SRC_ESPNOW */);
      continue;
    }
    uint8_t i = 0;
    while (i < blen) {
      uint8_t status = buf[i];
      if (!(status & 0x80)) break;
      uint8_t upper = status & 0xF0;
      uint8_t need = (upper == 0xC0 || upper == 0xD0) ? 1 : 2;
      if ((uint8_t)(i + 1 + need) > blen) break;
      // PALM only reacts to incoming CCs (LED color via onControlChange)
      if (upper == 0xB0) onControlChange((status & 0x0F) + 1, buf[i + 1], buf[i + 2], 0);
      i += 1 + need;
    }
  }

  uint32_t now = millis();
  if (!espnowLinked) {
#ifdef USE_BLE_MIDI
    if (now >= BLE_FALLBACK_MS) bleFallbackStart();  // no-op after first call
    // keep pinging even while BLE-connected: the receiver is preferred,
    // and we switch back to it the moment it reappears
#endif
    if (now - lastPingMs >= PING_INTERVAL_MS) {
      lastPingMs = now;
      esp_err_t e = espnowSendPing();
      static uint32_t lastSearchPrint = 0;
      if (now - lastSearchPrint >= 5000) {
        lastSearchPrint = now;
        Serial.printf("espnow: searching (ping err=%d)\n", (int)e);
      }
    }
    return;
  }
  if (now - lastQueueMs >= HEARTBEAT_MS) {
    espnowQueuePacket(PKT_HEARTBEAT, nullptr, 0);
  }
  if (now - lastPingMs >= LINKED_PING_MS) {
    lastPingMs = now;
    espnowSendPing();
  }
  espnowPump();  // safety net if a send error left the queue idle
}

#endif  // USE_ESPNOW_MIDI
