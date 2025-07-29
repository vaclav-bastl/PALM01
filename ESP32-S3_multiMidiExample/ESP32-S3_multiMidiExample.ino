#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Adafruit_NeoPixel.h>
#include <MIDI.h>
#include "USB.h"
#include "USBMIDI.h"

// === CONFIGURATION SECTION ===
#define USE_BLE_MIDI
#define USE_SERIAL_MIDI
#define USE_USB_MIDI
#define USE_NEOPIXEL
#define USE_DEBUG

const char* BLE_DEVICE_NAME = "PALM_BLE";  // Name shown to host devices
const uint8_t MIDI_CHANNEL = 1;            // Default channel (1-16)

#ifdef USE_SERIAL_MIDI
MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);
#endif

#ifdef USE_USB_MIDI
USBMIDI MIDIUSB;
#endif

#ifdef USE_BLE_MIDI
#define MIDI_SERVICE_UUID        "03B80E5A-EDE8-4B33-A751-6CE34EC4C700"
#define MIDI_CHARACTERISTIC_UUID "7772E5DB-3868-4112-A1A9-F2669D106BF3"
NimBLECharacteristic* pMidiCharacteristic;
#endif

#ifdef USE_NEOPIXEL
#define NEOPIXEL_PIN 33
#define NEOPIXEL_POWER 21
Adafruit_NeoPixel pixel(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
uint8_t pulseR = 0, pulseG = 0, pulseB = 0;
unsigned long lastPulseTime = 0;
bool pixelActive = false;

void setNeoPulse(uint8_t r, uint8_t g, uint8_t b) {
  pulseR = r; pulseG = g; pulseB = b;
  lastPulseTime = millis();
  pixel.setPixelColor(0, pixel.Color(r, g, b));
  pixel.show();
  pixelActive = true;
}
#endif

#ifdef USE_BLE_MIDI
uint8_t midiBuffer[64];
size_t midiBufferLen = 0;

void bleFlushMidiBuffer() {
  if (midiBufferLen == 0) return;
  uint8_t packet[66];
  packet[0] = 0x80; packet[1] = 0x80;
  memcpy(&packet[2], midiBuffer, midiBufferLen);
  pMidiCharacteristic->setValue(packet, midiBufferLen + 2);
  pMidiCharacteristic->notify();
  midiBufferLen = 0;
}

void bleQueueMIDIMessage(uint8_t* data, size_t len) {
  if (midiBufferLen + len > sizeof(midiBuffer)) bleFlushMidiBuffer();
  memcpy(&midiBuffer[midiBufferLen], data, len);
  midiBufferLen += len;
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
class MyServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, NimBLEConnInfo& ci) override {
#ifdef USE_DEBUG
    Serial.println("🔗 Client connected");
#endif
    NimBLEDevice::getServer()->updateConnParams(ci.getConnHandle(), 6, 9, 0, 400);
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
#ifdef USE_DEBUG
    Serial.println("🔌 Client disconnected");
#endif
    NimBLEDevice::startAdvertising();
  }
};

void bleMidiInit() {
#ifdef USE_NEOPIXEL
  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, HIGH);
  pixel.begin(); pixel.setBrightness(50); pixel.show();
#endif
  NimBLEDevice::init(BLE_DEVICE_NAME);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  NimBLEDevice::setSecurityAuth(true, true, false);
  NimBLEDevice::setSecurityPasskey(123456);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new MyServerCallbacks());

  NimBLEService* service = server->createService(MIDI_SERVICE_UUID);
  pMidiCharacteristic = service->createCharacteristic(
    MIDI_CHARACTERISTIC_UUID,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY
  );
  pMidiCharacteristic->createDescriptor("2902", NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
  NimBLEDescriptor* reportRefDesc = pMidiCharacteristic->createDescriptor("2908", NIMBLE_PROPERTY::READ);
  uint8_t reportRef[] = { 0x01, 0x03 };
  reportRefDesc->setValue(reportRef, sizeof(reportRef));

  service->start();
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(MIDI_SERVICE_UUID);
  adv->setAppearance(0x03C0);
  adv->start();
#ifdef USE_DEBUG
  Serial.println("📡 BLE MIDI Ready");
#endif
}
#endif

void midiBegin() {
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
#ifdef USE_NEOPIXEL
  if (pixelActive) {
    unsigned long elapsed = millis() - lastPulseTime;
    if (elapsed < 300) {
      float f = 1.0f - (elapsed / 300.0f);
      pixel.setPixelColor(0, pulseR * f, pulseG * f, pulseB * f);
      pixel.show();
    } else {
      pixel.clear(); pixel.show();
      pixelActive = false;
    }
  }
#endif
#ifdef USE_SERIAL_MIDI
  MIDI.read();
#endif
}

void testOutput() {
  static uint32_t lastTime = 0;
  static bool noteOn = false;

  if (millis() - lastTime > 1000) {
    if (noteOn) {
      sendNoteOff(60, 100, MIDI_CHANNEL); // Turn off note
    } else {
      sendNoteOn(60, 100, MIDI_CHANNEL);  // Middle C, velocity 100
      sendControlChange(1, 64, MIDI_CHANNEL); // Mod wheel (CC1), value 64
      sendProgramChange(10, MIDI_CHANNEL); // Change to program 10
      sendAftertouch(40, MIDI_CHANNEL);    // Channel pressure
      sendPitchBend(512, MIDI_CHANNEL);    // Slight upward bend
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
void midiDispatch(){
  bleFlushMidiBuffer();
}
void setup() {
  midiBegin();
}

void loop() {
  midiRead();
  midiDispatch();
  testOutput();
  delay(10);
}

