#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Adafruit_NeoPixel.h>
#include <MIDI.h>
#include "USB.h"
#include "USBMIDI.h"

// === CONFIGURATION SECTION ===
#define USE_BLE_MIDI
#define USE_SERIAL_MIDI
//#define USE_USB_MIDI          // Enable if your USBMIDI RX is set up
//#define USBMIDI_API_TINYUSB   // Uncomment if using TinyUSB-style midiEventPacket_t
//#define USBMIDI_API_ESP32     // Uncomment if your USBMIDI has .available()/.read()

#define USE_NEOPIXEL
#define USE_DEBUG

const char* BLE_DEVICE_NAME = "PALM_BLE";
const uint8_t MIDI_CHANNEL = 1;

// === Instances ===
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

// === NeoPixel (CC1 = intensity, CC2 = color incl. white) ===
#ifdef USE_NEOPIXEL
#define NEOPIXEL_PIN D2
#define NEOPIXEL_POWER D3
Adafruit_NeoPixel pixel(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
// === ADD near the top (globals) ===
#ifdef USE_NEOPIXEL
bool neoReady = false;

static void neoInitOnce() {
  if (neoReady) return;
  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, HIGH);
  delay(5);                     // give the pixel power rail a moment
  pixel.begin();
  pixel.setBrightness(255);     // keep full; we scale via CC1 intensity
  pixel.clear();
  pixel.show();
  neoReady = true;
}
#endif


// LED state driven by CC1/CC2
uint8_t ledIntensity = 0;             // 0..255 (from CC1)
uint8_t baseR = 0, baseG = 0, baseB = 0; // color from CC2 before intensity scaling

static inline void hsvToRgb(float h, float s, float v, uint8_t& r, uint8_t& g, uint8_t& b) {
  // h: 0..360, s:0..1, v:0..1
  while (h < 0) h += 360.0f;
  while (h >= 360.0f) h -= 360.0f;
  if (s <= 0.0f) {
    uint8_t x = (uint8_t)(v * 255.0f + 0.5f);
    r = g = b = x; return;
  }
  float c = v * s;
  float hh = h / 60.0f;
  int i = (int)hh;
  float f = hh - i;
  float p = v - c;
  float q = v - c * f;
  float t = v - c * (1.0f - f);
  float rf=0, gf=0, bf=0;
  switch (i) {
    default:
    case 0: rf=v; gf=t; bf=p; break;
    case 1: rf=q; gf=v; bf=p; break;
    case 2: rf=p; gf=v; bf=t; break;
    case 3: rf=p; gf=q; bf=v; break;
    case 4: rf=t; gf=p; bf=v; break;
    case 5: rf=v; gf=p; bf=q; break;
  }
  r = (uint8_t)(rf * 255.0f + 0.5f);
  g = (uint8_t)(gf * 255.0f + 0.5f);
  b = (uint8_t)(bf * 255.0f + 0.5f);
}
// Tunable "orange" green level (0..255). 96–128 is a nice orange.
static const uint8_t ORANGE_G = 96;

// 8-bit linear interpolation helper
static inline uint8_t lerp8(uint8_t a, uint8_t b, uint16_t t, uint16_t tmax) {
  return a + (int16_t)(b - a) * t / tmax;
}

// CC2 color sweep: White -> Green -> Orange -> Red -> Magenta -> Blue -> Cyan -> White
static void setColorFromCC2(uint8_t cc2) {
  // Palette stops (start and end both White for seamless loop)
  const uint8_t palette[8][3] = {
    {255, 255, 255},  // White
    {0,   255, 0  },  // Green
    {255, ORANGE_G, 0},// Orange
    {255, 0,   0  },  // Red
    {255, 0,   255},  // Magenta
    {0,   0,   255},  // Blue
    {0,   255, 255},  // Cyan
    {255, 255, 255}   // White (loop closure)
  };
  const uint8_t N_SEG = 7;         // number of transitions between stops

  // Map cc2 (0..127) onto N_SEG segments with 0..255 intra-segment t
  // Use integer math for stable interpolation.
  uint32_t pos = (uint32_t)cc2 * N_SEG * 255 / 127;  // 0 .. N_SEG*255
  uint16_t seg = pos / 255;                          // which segment
  uint16_t t   = pos % 255;                          // 0..254 (blend factor)

  if (seg >= N_SEG) { seg = N_SEG - 1; t = 255; }    // clamp top edge

  uint8_t r0 = palette[seg][0], g0 = palette[seg][1], b0 = palette[seg][2];
  uint8_t r1 = palette[seg+1][0], g1 = palette[seg+1][1], b1 = palette[seg+1][2];

  baseR = lerp8(r0, r1, t, 255);
  baseG = lerp8(g0, g1, t, 255);
  baseB = lerp8(b0, b1, t, 255);
}



static inline void applyLed() {
  // Scale base color by intensity (0..255)
  uint8_t r = (uint16_t)baseR * ledIntensity / 255;
  uint8_t g = (uint16_t)baseG * ledIntensity / 255;
  uint8_t b = (uint16_t)baseB * ledIntensity / 255;
  pixel.setPixelColor(0, pixel.Color(r, g, b));
  pixel.show();
}
#endif

// === Send helpers (unchanged) ===
#ifdef USE_BLE_MIDI
uint8_t midiBuffer[1024];
size_t midiBufferLen = 0;
static void bleFlushMidiBuffer() {
  if (midiBufferLen == 0) return;
  size_t totalLen = midiBufferLen + 2;
  if (totalLen > 66) { midiBufferLen = 64; totalLen = 66;
#ifdef USE_DEBUG
    Serial.println("⚠️ BLE MIDI packet too large, truncating.");
#endif
  }
  uint8_t packet[66];
  packet[0] = 0x80; packet[1] = 0x80; // minimal timestamp header
  memcpy(&packet[2], midiBuffer, midiBufferLen);
  pMidiCharacteristic->setValue(packet, totalLen);
  pMidiCharacteristic->notify();
  midiBufferLen = 0;
}
static void bleQueueMIDIMessage(uint8_t* data, size_t len) {
  if (len > sizeof(midiBuffer)) return;
  while (midiBufferLen + len > sizeof(midiBuffer)) { bleFlushMidiBuffer(); delay(1); }
  memcpy(&midiBuffer[midiBufferLen], data, len);
  midiBufferLen += len;
}
#endif

static void sendNoteOn(uint8_t note, uint8_t velocity, uint8_t channel) {
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
static void sendNoteOff(uint8_t note, uint8_t velocity, uint8_t channel) {
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
static void sendControlChange(uint8_t number, uint8_t value, uint8_t channel) {
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
static void sendProgramChange(uint8_t program, uint8_t channel) {
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
static void sendAftertouch(uint8_t pressure, uint8_t channel) {
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
static void sendPitchBend(int16_t bend, uint8_t channel) {
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

// === Dispatch targets (called by all inputs) ===
void handleNoteOn(uint8_t ch, uint8_t note, uint8_t vel) {
#ifdef USE_DEBUG
  Serial.printf("NoteOn ch%u note%u vel%u\n", ch, note, vel);
#endif
}
void handleNoteOff(uint8_t ch, uint8_t note, uint8_t vel) {
#ifdef USE_DEBUG
  Serial.printf("NoteOff ch%u note%u vel%u\n", ch, note, vel);
#endif
}
void handleCC(uint8_t ch, uint8_t number, uint8_t value) {
#ifdef USE_NEOPIXEL
  if (number == 1) {
    // CC1 → intensity 0..255
    ledIntensity = (uint16_t)value * 255 / 127;
    applyLed();
  } else if (number == 2) {
    // CC2 → color (rainbow + white at the top end)
    setColorFromCC2(value);
    applyLed();
  }
#endif
#ifdef USE_DEBUG
  Serial.printf("CC ch%u #%u = %u\n", ch, number, value);
#endif
}
void handleProgramChange(uint8_t ch, uint8_t program) {
#ifdef USE_DEBUG
  Serial.printf("Prog ch%u = %u\n", ch, program);
#endif
}
void handleAftertouch(uint8_t ch, uint8_t pressure) {
#ifdef USE_DEBUG
  Serial.printf("Aftertouch ch%u = %u\n", ch, pressure);
#endif
}
void handlePitchBend(uint8_t ch, uint16_t value14) {
#ifdef USE_DEBUG
  Serial.printf("PitchBend ch%u = %u\n", ch, value14);
#endif
}
// Realtime
void handleClock()         { /* 24 PPQN */ }
void handleStart()         { /* song start */ }
void handleStop()          { /* stop */ }
void handleContinueRT()    { /* continue */ }
void handleActiveSensing() { /* active sensing */ }
void handleSystemReset()   { /* system reset */ }

// === BLE MIDI RX + parser ===
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

static inline void dispatchChannelMsg(uint8_t status, uint8_t d1, uint8_t d2) {
  uint8_t upper = status & 0xF0;
  uint8_t ch = (status & 0x0F) + 1;
  switch (upper) {
    case 0x80: handleNoteOff(ch, d1 & 0x7F, d2 & 0x7F); break;
    case 0x90: if (d2) handleNoteOn(ch, d1 & 0x7F, d2 & 0x7F);
               else     handleNoteOff(ch, d1 & 0x7F, 0); break;
    case 0xA0: /* poly aftertouch ignored here */ break;
    case 0xB0: handleCC(ch, d1 & 0x7F, d2 & 0x7F); break;
    case 0xC0: handleProgramChange(ch, d1 & 0x7F); break;
    case 0xD0: handleAftertouch(ch, d1 & 0x7F); break;
    case 0xE0: { uint16_t v14 = ((uint16_t)(d2 & 0x7F) << 7) | (d1 & 0x7F);
                 handlePitchBend(ch, v14); } break;
  }
}

static void processMidiPacket(const uint8_t* data, size_t len) {
  static uint8_t runningStatus = 0;
  static uint8_t needed = 0;
  static uint8_t d1 = 0;

  for (size_t i = 0; i < len; ++i) {
    uint8_t b = data[i];

    // Realtime
    if (b >= 0xF8) {
      if (b == 0xF8) handleClock();
      else if (b == 0xFA) handleStart();
      else if (b == 0xFB) handleContinueRT();
      else if (b == 0xFC) handleStop();
      else if (b == 0xFE) handleActiveSensing();
      else if (b == 0xFF) handleSystemReset();
      continue;
    }

    if (b & 0x80) {
      uint8_t upper = b & 0xF0;
      if (upper >= 0x80 && upper <= 0xE0) {
        runningStatus = b;
        needed = (upper == 0xC0 || upper == 0xD0) ? 1 : 2;
      }
      continue; // ignore sys common/timestamps
    }

    // data
    if (!runningStatus) continue;
    if (needed == 0) needed = ((runningStatus & 0xF0) == 0xC0 || (runningStatus & 0xF0) == 0xD0) ? 1 : 2;

    if (needed == 2) { d1 = b & 0x7F; needed = 1; }
    else {
      uint8_t d2 = b & 0x7F;
      dispatchChannelMsg(runningStatus, d1, d2);
      needed = ((runningStatus & 0xF0) == 0xC0 || (runningStatus & 0xF0) == 0xD0) ? 1 : 2;
    }
  }
}

class MidiCharCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    std::string v = c->getValue(); if (v.empty()) return;
    processMidiPacket(reinterpret_cast<const uint8_t*>(v.data()), v.size());
  }
};

static void bleMidiInit() {

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
  uint8_t reportRef[] = { 0x01, 0x03 }; reportRefDesc->setValue(reportRef, sizeof(reportRef));
  pMidiCharacteristic->setCallbacks(new MidiCharCallbacks());
  service->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(MIDI_SERVICE_UUID);
  adv->setAppearance(0x03C0);
  adv->start();
#ifdef USE_DEBUG
  Serial.println("📡 BLE MIDI Ready");
#endif
}
#endif // USE_BLE_MIDI

// === Serial MIDI RX via FortySevenEffects callbacks ===
#ifdef USE_SERIAL_MIDI
static void serialMidiInit() {
  MIDI.begin(MIDI_CHANNEL_OMNI);

  MIDI.setHandleNoteOn([](byte ch, byte note, byte vel){ handleNoteOn(ch, note, vel); });
  MIDI.setHandleNoteOff([](byte ch, byte note, byte vel){ handleNoteOff(ch, note, vel); });
  MIDI.setHandleControlChange([](byte ch, byte num, byte val){ handleCC(ch, num, val); });
  MIDI.setHandleProgramChange([](byte ch, byte pgm){ handleProgramChange(ch, pgm); });
  MIDI.setHandleAfterTouchChannel([](byte ch, byte pr){ handleAftertouch(ch, pr); });
  MIDI.setHandlePitchBend([](byte ch, int bend){ uint16_t v14 = (uint16_t)(bend + 8192); handlePitchBend(ch, v14); });

  MIDI.setHandleClock([](){ handleClock(); });
  MIDI.setHandleStart([](){ handleStart(); });
  MIDI.setHandleStop([](){ handleStop(); });
  MIDI.setHandleContinue([](){ handleContinueRT(); });
  MIDI.setHandleActiveSensing([](){ handleActiveSensing(); });
  MIDI.setHandleSystemReset([](){ handleSystemReset(); });
}
#endif

// === USB MIDI RX (choose one API by defining a macro) ===
#ifdef USE_USB_MIDI
static void usbMidiInit() {
  MIDIUSB.begin();
  USB.begin();
}

#ifdef USBMIDI_API_TINYUSB
#include "Adafruit_TinyUSB.h"
static void usbReadAndDispatch() {
  midiEventPacket_t rx;
  while (tud_midi_available()) {
    tud_midi_packet_read(&rx);
    uint8_t status = rx.byte1, d1 = rx.byte2, d2 = rx.byte3;
    if (status >= 0xF8) {
      if (status == 0xF8) handleClock();
      else if (status == 0xFA) handleStart();
      else if (status == 0xFB) handleContinueRT();
      else if (status == 0xFC) handleStop();
      else if (status == 0xFE) handleActiveSensing();
      else if (status == 0xFF) handleSystemReset();
      continue;
    }
    uint8_t upper = status & 0xF0, ch = (status & 0x0F) + 1;
    switch (upper) {
      case 0x80: handleNoteOff(ch, d1 & 0x7F, d2 & 0x7F); break;
      case 0x90: if (d2) handleNoteOn(ch, d1 & 0x7F, d2 & 0x7F);
                 else     handleNoteOff(ch, d1 & 0x7F, 0); break;
      case 0xB0: handleCC(ch, d1 & 0x7F, d2 & 0x7F); break;
      case 0xC0: handleProgramChange(ch, d1 & 0x7F); break;
      case 0xD0: handleAftertouch(ch, d1 & 0x7F); break;
      case 0xE0: { uint16_t v14 = ((uint16_t)(d2 & 0x7F) << 7) | (d1 & 0x7F); handlePitchBend(ch, v14); } break;
    }
  }
}
#endif

#ifdef USBMIDI_API_ESP32
static void usbReadAndDispatch() {
  while (MIDIUSB.available()) {
    auto m = MIDIUSB.read();
    uint8_t status = m.status, d1 = m.data1, d2 = m.data2;
    if (status >= 0xF8) {
      if (status == 0xF8) handleClock();
      else if (status == 0xFA) handleStart();
      else if (status == 0xFB) handleContinueRT();
      else if (status == 0xFC) handleStop();
      else if (status == 0xFE) handleActiveSensing();
      else if (status == 0xFF) handleSystemReset();
      continue;
    }
    uint8_t upper = status & 0xF0, ch = (status & 0x0F) + 1;
    switch (upper) {
      case 0x80: handleNoteOff(ch, d1 & 0x7F, d2 & 0x7F); break;
      case 0x90: if (d2) handleNoteOn(ch, d1 & 0x7F, d2 & 0x7F);
                 else     handleNoteOff(ch, d1 & 0x7F, 0); break;
      case 0xB0: handleCC(ch, d1 & 0x7F, d2 & 0x7F); break;
      case 0xC0: handleProgramChange(ch, d1 & 0x7F); break;
      case 0xD0: handleAftertouch(ch, d1 & 0x7F); break;
      case 0xE0: { uint16_t v14 = ((uint16_t)(d2 & 0x7F) << 7) | (d1 & 0x7F); handlePitchBend(ch, v14); } break;
    }
  }
}
#endif

#ifndef USBMIDI_API_TINYUSB
#ifndef USBMIDI_API_ESP32
static void usbReadAndDispatch() { /* choose a USB API above to enable RX */ }
#endif
#endif
#endif // USE_USB_MIDI

// === App wiring ===
static void midiBegin() {
#ifdef USE_DEBUG
  Serial.begin(115200);
#endif
#ifdef USE_BLE_MIDI
  bleMidiInit();
#endif
#ifdef USE_SERIAL_MIDI
  serialMidiInit();
#endif
#ifdef USE_USB_MIDI
  usbMidiInit();
#endif
}

static void midiRead() {
#ifdef USE_SERIAL_MIDI
  MIDI.read(); // fires callbacks
#endif
#ifdef USE_USB_MIDI
  usbReadAndDispatch();
#endif
}

static void midiDispatch(){
#ifdef USE_BLE_MIDI
  bleFlushMidiBuffer();
#endif
}

void setup() {
  #ifdef USE_NEOPIXEL
  neoInitOnce();
  // sanity: show dim red so we know the LED path works at boot
  // (comment out after you confirm)
  extern uint8_t ledIntensity, baseR, baseG, baseB;
  ledIntensity = 64; baseR = 255; baseG = 0; baseB = 0;
  extern void applyLed();
  applyLed();
#endif

  midiBegin();

}


void loop() {
  midiRead();       // RX from Serial + USB
  midiDispatch();   // Flush BLE TX
  delay(1);
}
