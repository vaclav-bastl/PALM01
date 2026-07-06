// =====================================================
// SYSEX CONFIGURATOR PROTOCOL
//
// Frame: F0 7D 'P' 'L' 'M' <cmd> <payload...> F7   (all payload 7-bit;
// full-range bytes travel as lo7,hi7 pairs). See vst-design/DESIGN.md.
//
//   0x01 -> 0x41  device info (schema ver, fw ver, name)
//   0x02 -> 0x42  full config dump (CONFIG_BLOB_SIZE bytes as 7-bit pairs)
//   0x03          set param: addr lo,hi, value lo,hi (applies live)
//   0x04          save config + name to NVS
//   0x05          set device name (ASCII <=16, applies on reboot)
//   0x06          revert: reload NVS, discarding live edits
//
// Requests can arrive over BLE (characteristic writes) or over the
// exohub's ESP-NOW downstream; replies return on the same transport.
// =====================================================

#define SYSEX_FW_VERSION 1
#define SYSEX_MAX 512

enum : uint8_t { SX_SRC_BLE = 0, SX_SRC_ESPNOW = 1 };

static uint8_t sxBuf[SYSEX_MAX];
static uint16_t sxLen = 0;
static bool sxActive = false;

bool sysexActive() {  // espnow drain routes chunks here while a frame is open
  return sxActive;
}

// ---- reply transports ----

#ifdef USE_BLE_MIDI
// Spec-correct BLE-MIDI SysEx: first packet carries ts+F0, continuations
// carry raw data, F7 is preceded by a timestamp byte.
static void bleSendSysex(const uint8_t* f, uint16_t len) {
  if (!bleConnected || !pMidiCharacteristic) return;
  const uint16_t maxPay = 100;  // safely under the 185 MTU
  uint8_t pkt[104];
  uint16_t i = 0;
  bool first = true;
  while (i < len) {
    uint16_t k = 0;
    pkt[k++] = 0x80;                 // packet header
    if (first) pkt[k++] = 0x80;      // timestamp before F0
    while (i < len && k < maxPay) {
      if (f[i] == 0xF7) {
        if (k + 2 > maxPay) break;
        pkt[k++] = 0x80;             // timestamp before F7
        pkt[k++] = f[i++];
      } else {
        pkt[k++] = f[i++];
      }
    }
    pMidiCharacteristic->setValue(pkt, (uint16_t)k);
    pMidiCharacteristic->notify();
    first = false;
    delay(2);  // pace successive notifications
  }
}
#endif

static void sysexReply(const uint8_t* body, uint16_t bodyLen, uint8_t source) {
  static uint8_t frame[SYSEX_MAX];
  if (bodyLen + 6 > SYSEX_MAX) return;
  uint16_t n = 0;
  frame[n++] = 0xF0;
  frame[n++] = 0x7D;
  frame[n++] = 'P'; frame[n++] = 'L'; frame[n++] = 'M';
  memcpy(frame + n, body, bodyLen);
  n += bodyLen;
  frame[n++] = 0xF7;

  if (source == SX_SRC_BLE) {
#ifdef USE_BLE_MIDI
    bleSendSysex(frame, n);
#endif
  } else {
#ifdef USE_ESPNOW_MIDI
    for (uint16_t i = 0; i < n; i += 8)
      espnowQueueMIDIMessage(frame + i, (uint8_t)min((uint16_t)8, (uint16_t)(n - i)));
#endif
  }
}

// ---- command handlers ----

static void sysexSendInfo(uint8_t source) {
  uint8_t body[24];
  uint16_t n = 0;
  body[n++] = 0x41;
  body[n++] = CONFIG_SCHEMA_VERSION;
  body[n++] = SYSEX_FW_VERSION;
  for (const char* c = bleDeviceName; *c && n < sizeof(body); c++)
    body[n++] = (uint8_t)(*c & 0x7F);
  sysexReply(body, n, source);
}

static void sysexSendDump(uint8_t source) {
  uint8_t blob[CONFIG_BLOB_SIZE];
  configPack(blob);
  static uint8_t body[2 + CONFIG_BLOB_SIZE * 2];
  uint16_t n = 0;
  body[n++] = 0x42;
  body[n++] = CONFIG_SCHEMA_VERSION;
  for (uint16_t i = 0; i < CONFIG_BLOB_SIZE; i++) {
    body[n++] = blob[i] & 0x7F;         // lo 7 bits
    body[n++] = (blob[i] >> 7) & 0x01;  // hi bit
  }
  sysexReply(body, n, source);
}

static void sysexHandle(uint8_t source) {
  if (sxLen < 4) return;
  if (sxBuf[0] != 0x7D || sxBuf[1] != 'P' || sxBuf[2] != 'L' || sxBuf[3] != 'M') return;
  if (sxLen < 5) return;
  uint8_t cmd = sxBuf[4];
  const uint8_t* p = sxBuf + 5;
  uint16_t n = sxLen - 5;

  switch (cmd) {
    case 0x01:
      sysexSendInfo(source);
      break;
    case 0x02:
      sysexSendDump(source);
      break;
    case 0x03:
      if (n >= 4) {
        uint16_t addr = p[0] | ((uint16_t)p[1] << 7);
        uint8_t val = (uint8_t)(p[2] | (p[3] << 7));
        if (configApply(addr, val))
          Serial.printf("sysex: set [%u] = %u\n", addr, val);
      }
      break;
    case 0x04:
      configSave();
      break;
    case 0x05: {
      uint8_t l = (uint8_t)min(n, (uint16_t)16);
      for (uint8_t i = 0; i < l; i++) bleDeviceName[i] = (char)(p[i] & 0x7F);
      bleDeviceName[l] = 0;
      Serial.printf("sysex: name set to '%s' (save + reboot to apply)\n", bleDeviceName);
      break;
    }
    case 0x06:
      configLoad();
      break;
  }
}

// ---- byte-stream feeder (shared by BLE + ESP-NOW paths) ----
// Tolerates interleaved realtime bytes; any other status byte aborts the
// frame; BLE-MIDI timestamp bytes must be stripped by the caller or are
// harmlessly treated as aborts outside a frame.
void sysexFeed(const uint8_t* data, uint16_t len, uint8_t source) {
  for (uint16_t i = 0; i < len; i++) {
    uint8_t b = data[i];
    if (b == 0xF0) { sxActive = true; sxLen = 0; continue; }
    if (!sxActive) continue;
    if (b == 0xF7) { sxActive = false; sysexHandle(source); continue; }
    if (b >= 0x80) { if (b < 0xF8) sxActive = false; continue; }
    if (sxLen < SYSEX_MAX) sxBuf[sxLen++] = b;
    else sxActive = false;  // oversized frame: drop
  }
}

#ifdef USE_BLE_MIDI
// BLE characteristic write -> strip BLE-MIDI framing, route SysEx + CCs.
// Inside SysEx, any high byte except F7/realtime is a timestamp: skipped.
void palmBleMidiRx(const uint8_t* data, uint16_t len) {
  static uint8_t ccStatus = 0, ccD1 = 0, ccNeed = 0;
  for (uint16_t i = 1; i < len; i++) {  // [0] = packet header
    uint8_t b = data[i];
    if (sxActive) {
      if (b == 0xF7 || b < 0x80) sysexFeed(&b, 1, SX_SRC_BLE);
      // other high bytes = timestamps/realtime: skip
      continue;
    }
    if (b == 0xF0) { sysexFeed(&b, 1, SX_SRC_BLE); continue; }
    if (b & 0x80) {
      if ((b & 0xF0) == 0xB0) { ccStatus = b; ccNeed = 2; }
      else if (b < 0xF0) ccStatus = 0;  // other channel status: ignore
      continue;  // timestamps + statuses
    }
    if (ccStatus && ccNeed == 2) { ccD1 = b; ccNeed = 1; }
    else if (ccStatus && ccNeed == 1) {
      onControlChange((ccStatus & 0x0F) + 1, ccD1, b, 0);
      ccNeed = 2;
    }
  }
}
#endif
