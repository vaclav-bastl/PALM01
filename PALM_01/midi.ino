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
NimBLECharacteristic* pMidiCharacteristic;
#endif

#ifdef USE_BLE_MIDI
// ---- BLE MIDI TX buffering (small messages flush immediately to reduce latency) ----
uint8_t midiBuffer[1024];
size_t midiBufferLen = 0;

static inline void bleFlushMidiBuffer() {
  if (midiBufferLen == 0) return;

  size_t totalLen = midiBufferLen + 2;
  if (totalLen > 66) {
#ifdef USE_DEBUG
    Serial.println("⚠️ BLE MIDI packet too large, truncating.");
#endif
    midiBufferLen = 64;
    totalLen = 66;
  }

  uint8_t packet[66];
  packet[0] = 0x80; // BLE-MIDI header (simple timestamp nibble = 0)
  packet[1] = 0x80;
  memcpy(&packet[2], midiBuffer, midiBufferLen);

  pMidiCharacteristic->setValue(packet, totalLen);
  pMidiCharacteristic->notify();

  midiBufferLen = 0;

  // keep LED lightweight
  neopixelWrite(NEO_PIXEL_PIN, 0, 0, 20);
}

static inline void bleQueueMIDIMessage(const uint8_t* data, size_t len) {
  if (len > sizeof(midiBuffer)) return;

  while (midiBufferLen + len > sizeof(midiBuffer)) {
    bleFlushMidiBuffer();
    delay(0); // yield without blocking BLE task
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
  uint8_t msg[] = { uint8_t(0xE0 | ((channel - 1) & 0x0F)), value & 0x7F, (value >> 7) & 0x7F };
  bleQueueMIDIMessage(msg, 3);
#endif
#ifdef USE_SERIAL_MIDI
  MIDI.sendPitchBend(value, channel);
#endif
#ifdef USE_USB_MIDI
  MIDIUSB.pitchBend(value, channel);
#endif
}

#ifdef USE_BLE_MIDI
// ---- Connection param retry (portable across older NimBLE versions) ----
static uint16_t g_connHandle = 0;

// Replace your retry task:
static void requestFastParamsRetry(void* /*pv*/) {
  // 1st retry after 300 ms: try 15 ms
  vTaskDelay(pdMS_TO_TICKS(300));
  if (g_connHandle) {
    NimBLEDevice::getServer()->updateConnParams(g_connHandle, 12, 12, 0, 400); // 15 ms
  }
  // 2nd retry after 700 ms: try 7.5 ms
  vTaskDelay(pdMS_TO_TICKS(400));
  if (g_connHandle) {
    NimBLEDevice::getServer()->updateConnParams(g_connHandle, 6, 6, 0, 400);   // 7.5 ms
  }
  vTaskDelete(nullptr);
}

class MyServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, NimBLEConnInfo& ci) override {
    bleConnected = true;
    g_connHandle = ci.getConnHandle();
#ifdef USE_DEBUG
    Serial.printf("Conn: itvl=%.2fms, lat=%u, supTO=%.1fs, handle=%u\n",
                  ci.getConnInterval() * 1.25f,
                  ci.getConnLatency(),
                  ci.getConnTimeout() * 10.0f / 1000.0f,
                  ci.getConnHandle());
#endif
    // First shot: 15 ms (often accepted more readily than 7.5 ms)
    NimBLEDevice::getServer()->updateConnParams(ci.getConnHandle(), 12, 12, 0, 400);

    // Schedule retries (15 ms again, then 7.5 ms)
    xTaskCreatePinnedToCore(requestFastParamsRetry, "bleFastRetry", 2048, nullptr, 1, nullptr, 0);

    neopixelWrite(NEO_PIXEL_PIN, 0, 10, 0);
  }

 // Not marked 'override' so it compiles even if your NimBLE lacks this callback.
  void onConnParamsUpdate(NimBLEConnInfo& ci) {
#ifdef USE_DEBUG
    Serial.printf("ConnUpdate: itvl=%.2fms, lat=%u, supTO=%.1fs\n",
                  ci.getConnInterval() * 1.25f,
                  ci.getConnLatency(),
                  ci.getConnTimeout() * 10.0f / 1000.0f);
#endif
  }

  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
    bleConnected = false;
    g_connHandle = 0;
#ifdef USE_DEBUG
    Serial.println("🔌 Client disconnected");
#endif
    NimBLEDevice::startAdvertising();
    neopixelWrite(NEO_PIXEL_PIN, 20, 0, 0);
  }
};


void adverstiseBle() {
  NimBLEDevice::startAdvertising();
}

void bleMidiInit() {
  NimBLEDevice::init(BLE_DEVICE_NAME);

  // ---- radio/throughput tunings (portable set) ----
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  NimBLEDevice::setMTU(185);           // available on older NimBLE; helps efficiency
  // (Your NimBLE lacks setMinPreferred/setMaxPreferred/setDataLen/setPHY — omitted)

  // ---- security ----
  NimBLEDevice::setSecurityAuth(true, true, false);
  NimBLEDevice::setSecurityPasskey(123456);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);

  // ---- GATT server & MIDI characteristic ----
  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new MyServerCallbacks());

  NimBLEService* service = server->createService(MIDI_SERVICE_UUID);
  pMidiCharacteristic = service->createCharacteristic(
    MIDI_CHARACTERISTIC_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY);

  pMidiCharacteristic->createDescriptor("2902", NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
  NimBLEDescriptor* reportRefDesc = pMidiCharacteristic->createDescriptor("2908", NIMBLE_PROPERTY::READ);
  uint8_t reportRef[] = { 0x01, 0x03 };
  reportRefDesc->setValue(reportRef, sizeof(reportRef));

  service->start();

  // ---- advertising ----
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(MIDI_SERVICE_UUID);
  adv->setAppearance(0x03C0);
  adv->start();

#ifdef USE_DEBUG
  Serial.println("📡 BLE MIDI Ready");
#endif
}
#endif // USE_BLE_MIDI

void initMidi() {
#ifdef USE_DEBUG
  Serial.begin(115200);
#endif
#ifdef USE_BLE_MIDI
  bleMidiInit();
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

void testMidi() {
  static uint32_t lastTime = 0;
  static bool noteOn = false;

  if (millis() - lastTime > 1000) {
    if (noteOn) {
      sendNoteOff(60, 100, MIDI_CHANNEL);
    } else {
      sendNoteOn(60, 100, MIDI_CHANNEL);
      sendControlChange(1, 64, MIDI_CHANNEL);
      sendProgramChange(10, MIDI_CHANNEL);
      sendAftertouch(40, MIDI_CHANNEL);
      sendPitchBend(512, MIDI_CHANNEL);
    }
    noteOn = !noteOn;
#ifdef USE_BLE_MIDI
    bleFlushMidiBuffer();
#endif
#ifdef USE_DEBUG
    Serial.println("Test output triggered");
#endif
    lastTime = millis();
  }
}

void midiDispatch() {
#ifdef USE_BLE_MIDI
  bleFlushMidiBuffer();
#endif
}
