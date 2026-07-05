// =====================================================
// MIDI TRANSPORTS
//
// Send helpers fan out to every enabled transport (#defines in the
// main tab): ESP-NOW (espnow.ino), BLE MIDI server, hardware serial,
// USB. With ESP-NOW enabled the BLE stack starts only as a fallback
// (see espnow.ino); whichever link connects first wins.
// =====================================================

#include <NimBLEDevice.h>
#include <MIDI.h>
#include "USB.h"
#include "USBMIDI.h"
#include "palm_shared.h"

#ifdef USE_SERIAL_MIDI
MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);
#endif

#ifdef USE_USB_MIDI
USBMIDI MIDIUSB;
#endif

#ifdef USE_BLE_MIDI
#define MIDI_SERVICE_UUID "03B80E5A-EDE8-4B33-A751-6CE34EC4C700"
#define MIDI_CHARACTERISTIC_UUID "7772E5DB-3868-4112-A1A9-F2669D106BF3"
NimBLECharacteristic* pMidiCharacteristic = nullptr;
#endif

#ifdef USE_BLE_MIDI
// ---- BLE MIDI TX buffering (small messages flush immediately) ----
uint8_t midiBuffer[1024];
size_t midiBufferLen = 0;

static inline void bleFlushMidiBuffer() {
  if (midiBufferLen == 0) return;
  if (!pMidiCharacteristic || !bleConnected) { midiBufferLen = 0; return; }

  size_t totalLen = midiBufferLen + 2;
  if (totalLen > 66) {
#ifdef USE_DEBUG
    Serial.println("BLE MIDI packet too large, truncating.");
#endif
    midiBufferLen = 64;
    totalLen = 66;
  }

  uint8_t packet[66];
  packet[0] = 0x80;  // BLE-MIDI header (timestamp nibble = 0)
  packet[1] = 0x80;
  memcpy(&packet[2], midiBuffer, midiBufferLen);

  pMidiCharacteristic->setValue(packet, totalLen);
  pMidiCharacteristic->notify();

  midiBufferLen = 0;
}

static inline void bleQueueMIDIMessage(const uint8_t* data, size_t len) {
  if (len > sizeof(midiBuffer)) return;
  if (!pMidiCharacteristic) return;  // BLE stack not started (ESP-NOW active)

  while (midiBufferLen + len > sizeof(midiBuffer)) {
    bleFlushMidiBuffer();
    delay(0);  // yield without blocking BLE task
  }

  memcpy(&midiBuffer[midiBufferLen], data, len);
  midiBufferLen += len;

  // Flush immediately for tiny, latency-sensitive messages
  if (len <= 3 || midiBufferLen >= 60) {
    bleFlushMidiBuffer();
  }
}
#endif

void sendNoteOn(uint8_t note, uint8_t velocity, uint8_t channel) {
#ifdef USE_BLE_MIDI
  uint8_t msg[] = { uint8_t(0x90 | ((channel - 1) & 0x0F)), note, velocity };
  bleQueueMIDIMessage(msg, 3);
#endif
#ifdef USE_ESPNOW_MIDI
  { uint8_t m[] = { uint8_t(0x90 | ((channel - 1) & 0x0F)), note, velocity };
    espnowQueueMIDIMessage(m, sizeof m); }
#endif
#ifdef USE_SERIAL_MIDI
  MIDI.sendNoteOn(note, velocity, channel);
#endif
#ifdef USE_USB_MIDI
  MIDIUSB.noteOn(note, velocity, channel);
#endif
}

void sendNoteOff(uint8_t note, uint8_t velocity, uint8_t channel) {
#ifdef USE_BLE_MIDI
  uint8_t msg[] = { uint8_t(0x80 | ((channel - 1) & 0x0F)), note, velocity };
  bleQueueMIDIMessage(msg, 3);
#endif
#ifdef USE_ESPNOW_MIDI
  { uint8_t m[] = { uint8_t(0x80 | ((channel - 1) & 0x0F)), note, velocity };
    espnowQueueMIDIMessage(m, sizeof m); }
#endif
#ifdef USE_SERIAL_MIDI
  MIDI.sendNoteOff(note, velocity, channel);
#endif
#ifdef USE_USB_MIDI
  MIDIUSB.noteOff(note, velocity, channel);
#endif
}

void sendControlChange(uint8_t number, uint8_t value, uint8_t channel) {
  outputCCValue[number] = value;
#ifdef USE_BLE_MIDI
  uint8_t msg[] = { uint8_t(0xB0 | ((channel - 1) & 0x0F)), number, value };
  bleQueueMIDIMessage(msg, 3);
#endif
#ifdef USE_ESPNOW_MIDI
  { uint8_t m[] = { uint8_t(0xB0 | ((channel - 1) & 0x0F)), number, value };
    espnowQueueMIDIMessage(m, sizeof m); }
#endif
#ifdef USE_SERIAL_MIDI
  MIDI.sendControlChange(number, value, channel);
#endif
#ifdef USE_USB_MIDI
  MIDIUSB.controlChange(number, value, channel);
#endif
}

void sendProgramChange(uint8_t program, uint8_t channel) {
#ifdef USE_BLE_MIDI
  uint8_t msg[] = { uint8_t(0xC0 | ((channel - 1) & 0x0F)), program };
  bleQueueMIDIMessage(msg, 2);
#endif
#ifdef USE_ESPNOW_MIDI
  { uint8_t m[] = { uint8_t(0xC0 | ((channel - 1) & 0x0F)), program };
    espnowQueueMIDIMessage(m, sizeof m); }
#endif
#ifdef USE_SERIAL_MIDI
  MIDI.sendProgramChange(program, channel);
#endif
#ifdef USE_USB_MIDI
  MIDIUSB.programChange(program, channel);
#endif
}

void sendAftertouch(uint8_t pressure, uint8_t channel) {
#ifdef USE_BLE_MIDI
  uint8_t msg[] = { uint8_t(0xD0 | ((channel - 1) & 0x0F)), pressure };
  bleQueueMIDIMessage(msg, 2);
#endif
#ifdef USE_ESPNOW_MIDI
  { uint8_t m[] = { uint8_t(0xD0 | ((channel - 1) & 0x0F)), pressure };
    espnowQueueMIDIMessage(m, sizeof m); }
#endif
#ifdef USE_SERIAL_MIDI
  MIDI.sendAfterTouch(pressure, channel);
#endif
#ifdef USE_USB_MIDI
  MIDIUSB.channelPressure(pressure, channel);
#endif
}

void sendPitchBend(int16_t bend, uint8_t channel) {
  bend = constrain(bend, -8192, 8191);
  uint16_t value = bend + 8192;
#ifdef USE_BLE_MIDI
  uint8_t msg[] = { uint8_t(0xE0 | ((channel - 1) & 0x0F)), uint8_t(value & 0x7F), uint8_t((value >> 7) & 0x7F) };
  bleQueueMIDIMessage(msg, 3);
#endif
#ifdef USE_ESPNOW_MIDI
  { uint8_t m[] = { uint8_t(0xE0 | ((channel - 1) & 0x0F)), uint8_t(value & 0x7F), uint8_t((value >> 7) & 0x7F) };
    espnowQueueMIDIMessage(m, sizeof m); }
#endif
#ifdef USE_SERIAL_MIDI
  MIDI.sendPitchBend(value, channel);
#endif
#ifdef USE_USB_MIDI
  MIDIUSB.pitchBend(value, channel);
#endif
}

#ifdef USE_BLE_MIDI
// ---- Connection param retry ----
static uint16_t g_connHandle = 0;


static void requestFastParamsRetry(void* /*pv*/) {
  vTaskDelay(pdMS_TO_TICKS(300));
  if (g_connHandle) {
    NimBLEDevice::getServer()->updateConnParams(g_connHandle, 12, 12, 0, 400);  // 15 ms
  }
  vTaskDelay(pdMS_TO_TICKS(400));
  if (g_connHandle) {
    NimBLEDevice::getServer()->updateConnParams(g_connHandle, 6, 6, 0, 400);  // 7.5 ms
  }
  vTaskDelete(nullptr);
}

class MyServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, NimBLEConnInfo& ci) override {
    bleConnected = true;
    g_connHandle = ci.getConnHandle();
    Serial.printf("ble: central connected (%s), interval %.2f ms\n",
                  ci.getAddress().toString().c_str(), ci.getConnInterval() * 1.25f);
#ifdef USE_DEBUG
    Serial.printf("Conn: itvl=%.2fms, lat=%u, supTO=%.1fs, handle=%u\n",
                  ci.getConnInterval() * 1.25f,
                  ci.getConnLatency(),
                  ci.getConnTimeout() * 10.0f / 1000.0f,
                  ci.getConnHandle());
#endif
    // First shot 15 ms (accepted more readily), then retries down to 7.5 ms
    NimBLEDevice::getServer()->updateConnParams(ci.getConnHandle(), 12, 12, 0, 400);
    xTaskCreatePinnedToCore(requestFastParamsRetry, "bleFastRetry", 2048, nullptr, 1, nullptr, 0);
  }

  void onConfirmPassKey(NimBLEConnInfo& ci, uint32_t pin) override {
    Serial.printf("ble: numeric comparison code %06lu -> auto-confirm\n", (unsigned long)pin);
    NimBLEDevice::injectConfirmPasskey(ci, true);
  }

  void onAuthenticationComplete(NimBLEConnInfo& ci) override {
    Serial.printf("ble: auth complete: encrypted=%d authenticated=%d bonded=%d\n",
                  ci.isEncrypted(), ci.isAuthenticated(), ci.isBonded());
  }

  void onConnParamsUpdate(NimBLEConnInfo& ci) override {
    Serial.printf("ble: conn params now: interval %.2f ms, latency %u, timeout %.1f s\n",
                  ci.getConnInterval() * 1.25f, ci.getConnLatency(),
                  ci.getConnTimeout() * 10.0f / 1000.0f);
  }

  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int reason) override {
    bleConnected = false;
    g_connHandle = 0;
    Serial.printf("ble: central disconnected (reason %d)\n", reason);
#ifdef USE_DEBUG
    Serial.println("Client disconnected");
#endif
    // bleAdvTick restarts advertising when appropriate
  }
};

// Pairing experiment helper: wipe stored bonds (BUTTON_L while disconnected)
void bleWipeBonds() {
  int n = NimBLEDevice::getNumBonds();
  NimBLEDevice::deleteAllBonds();
  Serial.printf("ble: wiped %d bond(s) -- also remove PALM_03 on the Mac\n", n);
}

void bleMidiInit() {
  NimBLEDevice::init(BLE_DEVICE_NAME);
  // Fresh static-random address: macOS caches GATT structure per address,
  // and this device's GATT changed several times during development --
  // a new identity sidesteps every stale cache on every Mac.
  static const uint8_t ownAddr[6] = { 0x0A, 0x20, 0x26, 0x3C, 0x9E, 0xCE };  // CE:9E:3C:26:20:0A
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
  NimBLEDevice::setOwnAddr(ownAddr);
  Serial.printf("ble: %d bond(s) stored, addr %s\n", NimBLEDevice::getNumBonds(),
                NimBLEDevice::getAddress().toString().c_str());

  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  NimBLEDevice::setMTU(185);

  // Match the reference WIDI Jack: BOND but "just works" (no MITM), no
  // auth prompt. macOS 13.5+ auto-reconnects a known BLE-MIDI device on
  // this basis; forcing authentication fought that and caused the
  // connect/drop/retry loop.
  NimBLEDevice::setSecurityAuth(true, false, true);  // bond, noMITM, SC
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new MyServerCallbacks());

  NimBLEService* service = server->createService(MIDI_SERVICE_UUID);
  // Encryption-required (but NOT authentication-required): macOS must
  // pair to read this, and with no-MITM the pairing is silent "just
  // works" -- no dialog -- producing the bond + paired-registry entry
  // that macOS 13.5+ auto-reconnect keys off.
  pMidiCharacteristic = service->createCharacteristic(
    MIDI_CHARACTERISTIC_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC |
    NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_ENC |
    NIMBLE_PROPERTY::NOTIFY);  // props match the WIDI: Read+WriteNR+Notify

  pMidiCharacteristic->createDescriptor("2902", NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
  NimBLEDescriptor* reportRefDesc = pMidiCharacteristic->createDescriptor("2908", NIMBLE_PROPERTY::READ);
  uint8_t reportRef[] = { 0x01, 0x03 };
  reportRefDesc->setValue(reportRef, sizeof(reportRef));

  service->start();

  // Device Information Service -- the WIDI carries one and it is the only
  // device data MIDIServer's table shows populated for it (mimicry test:
  // exact CME strings; if auto-connect starts working, bisect later)
  NimBLEService* dis = server->createService("180A");
  dis->createCharacteristic("2A29", NIMBLE_PROPERTY::READ)->setValue("Bastl Instruments");
  dis->createCharacteristic("2A24", NIMBLE_PROPERTY::READ)->setValue("PALM");
  dis->createCharacteristic("2A26", NIMBLE_PROPERTY::READ)->setValue("0001");
  dis->createCharacteristic("2A27", NIMBLE_PROPERTY::READ)->setValue("0001");
  dis->start();

  // WIDI-mimicry advertisement: manufacturer data + MIDI service UUID,
  // NO local name on the air (name via GATT after connect); 16-bit FFD0
  // service in the scan response. Branch experiment -- the exohub's
  // name-in-scan-response advertising is bypassed here.
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData advData;
  advData.setFlags(0x06);
  advData.setManufacturerData(std::string("\xff\xff\x01", 3));  // 0xFFFF = SIG test ID
  advData.setCompleteServices(NimBLEUUID(MIDI_SERVICE_UUID));
  adv->setAdvertisementData(advData);
  NimBLEAdvertisementData rsp;
  rsp.setName(BLE_DEVICE_NAME);  // panel shows the name pre-connect; exohub binds ports by it
  adv->setScanResponseData(rsp);
  adv->start();

#ifdef USE_DEBUG
  Serial.println("BLE MIDI ready");
#endif
}

// ---- WIDI-style reconnect advertising ----
// While bonded and disconnected, alternate DIRECTED advertising at the
// bonded central (radio-level auto-reconnect, invisible to scans -- the
// same trick keyboards and the CME WIDI use) with normal undirected
// advertising (so Bluetooth MIDI panels can still discover us).
static void bleAdvTick() {
  // Plain continuous undirected advertising (WIDI-style: macOS answers a
  // bonded device's ordinary advertisement within ~2 s). Quiet while the
  // ESP-NOW receiver holds the link -- ESP-NOW has priority.
  if (bleConnected) return;
#ifdef USE_ESPNOW_MIDI
  if (espnowLinked) return;
#endif
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  if (!adv->isAdvertising()) adv->start();
}

// ---- ESP-NOW fallback arbitration (called from espnow.ino) ----
static bool bleStackStarted = false;

static void bleFallbackStart() {
  if (bleStackStarted) return;
  bleStackStarted = true;
  bleMidiInit();
  Serial.println("ble: fallback advertising started");
}

// Hang up on a connected BLE central (the receiver takes priority)
static void bleDropConnection() {
  if (!bleStackStarted || !bleConnected) return;
  NimBLEServer* s = NimBLEDevice::getServer();
  if (s && g_connHandle) s->disconnect(g_connHandle);
}

static void bleAdvertisingControl(bool on) {
  if (!bleStackStarted) return;
  if (on) {
    if (!bleConnected) NimBLEDevice::startAdvertising();
  } else {
    NimBLEDevice::stopAdvertising();
  }
}

// Manual advertising (BUTTON_R): starts the BLE stack if needed
void adverstiseBle() {
  if (!bleStackStarted) bleFallbackStart();
  else if (!bleConnected) NimBLEDevice::startAdvertising();
}
#else
void adverstiseBle() {}
#endif  // USE_BLE_MIDI

void initMidi() {
#ifdef USE_BLE_MIDI
#ifndef USE_ESPNOW_MIDI
  bleFallbackStart();  // BLE is the primary transport (sets bleStackStarted)
#endif
  // with ESP-NOW enabled, BLE starts later as fallback (see espnow.ino)
#endif
#ifdef USE_ESPNOW_MIDI
  espnowBegin();
#endif
#ifdef USE_SERIAL_MIDI
  MIDI.begin(MIDI_CHANNEL_OMNI);
#endif
#ifdef USE_USB_MIDI
  MIDIUSB.begin();
  USB.begin();
#endif
}

void midiRead() {
#ifdef USE_SERIAL_MIDI
  MIDI.read();
#endif
}

void midiDispatch() {
#ifdef USE_BLE_MIDI
  bleFlushMidiBuffer();
  bleAdvTick();
#endif
#ifdef USE_ESPNOW_MIDI
  espnowLoop();
#endif
}
