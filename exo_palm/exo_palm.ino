// =====================================================
// exo_palm — PALM wearable MIDI controller (Seeed XIAO ESP32S3)
//
// A hand-worn controller: 8 finger touch points + 3 thumb pads
// (MPR121), 2 buttons, an accelerometer for wrist/elbow gestures.
// Presets are either CC mode (per-finger touch CCs + motion CCs)
// or NOTE mode (touch chooses a root, elbow angle arpeggiates).
// MIDI leaves over ESP-NOW to the wirelessToUSBmidi dongle
// (primary) and/or BLE MIDI, plus hardware serial.
//
// This tab owns the configuration, the preset tables, and all state
// shared between tabs (Arduino builds tabs alphabetically after this
// file — shared globals must live here). Each behavior has its own tab:
//
//   context.ino    CC mode: per-finger touch/wrist/elbow CCs, global
//                  motion CCs + suppression, preset selector
//   espnow.ino     ESP-NOW link to the receiver (+ BLE fallback logic)
//   gyro.ino       accelerometer reading + smoothing
//   hw.ino         pins, power, buttons, NeoPixel
//   midi.ino       transports: send helpers, BLE, serial, USB
//   noteMode.ino   NOTE mode: root from touches, elbow arpeggiation
//   presets.ino    thumb preset selection + smart CC cleanup on switch
//   touch.ino      MPR121 reading + pressure values
//   utility.ino    small generic helpers (stickyMap etc.)
// =====================================================

#include "palm_shared.h"

#include <Adafruit_Sensor.h>
#include <Wire.h>
#include "Adafruit_MPR121.h"
#include <Adafruit_ADXL345_U.h>

// =====================================================
// CONFIGURATION
// =====================================================
// Transports. With BOTH radio transports defined: ESP-NOW is primary;
// if no receiver answers within 5 s the controller starts advertising
// as a BLE MIDI device. Whichever link connects first wins.
// BRANCH ble-pin-pairing: BLE-primary build for testing Mac PIN pairing
//#define USE_ESPNOW_MIDI  // custom link to the wirelessToUSBmidi receiver
#define USE_BLE_MIDI     // BLE MIDI fallback (or primary if ESPNOW disabled)
#define USE_SERIAL_MIDI  // hardware MIDI on Serial1
// #define USE_USB_MIDI
//#define USE_DEBUG

const char* BLE_DEVICE_NAME = "PALM_03";
#define NEO_PIXEL_PIN D2

// Fixed velocity for the first note in NOTE mode
#define NOTE_FIRST_VELOCITY 64

// Default channel used when a preset is in CC mode (presetMode == 0)
const uint8_t MIDI_CHANNEL = 1;  // 1..16

#ifdef USE_ESPNOW_MIDI
extern bool espnowLinked;  // defined in espnow.ino
#endif

// =====================================================
// SHARED STATE
// =====================================================
// Accelerometer (filled by gyro.ino)
#define ACCEL_HYSTERESIS 3
#define X_AXIS 0
#define Y_AXIS 1
#define Z_AXIS 2
int accelValue[3] = { 0, 0, 0 };
int accelRunningValue[3] = { 0, 0, 0 };
int lastAccelRunningValue[3] = { 0, 0, 0 };

uint8_t rgb[3] = { 0, 0, 0 };  // set by incoming CCs (onControlChange)
uint8_t currentPreset = 0;
uint8_t lastPreset = 0;
bool bleConnected = false;

// Touch (filled by touch.ino)
bool touchState[NUMBER_OF_TOUCHPOINTS] = { 0 };
bool lastTouchState[NUMBER_OF_TOUCHPOINTS] = { 0 };
bool justTouched[NUMBER_OF_TOUCHPOINTS] = { 0 };
bool justUntouched[NUMBER_OF_TOUCHPOINTS] = { 0 };
uint8_t runningTouchValue[NUMBER_OF_TOUCHPOINTS] = { 0 };

// Buttons (filled by hw.ino)
#define NUMBER_OF_BUTTONS 2
bool buttonState[NUMBER_OF_BUTTONS] = { 0 };
bool lastButtonState[NUMBER_OF_BUTTONS] = { 0 };
bool justPressed[NUMBER_OF_BUTTONS] = { 0 };
bool justReleased[NUMBER_OF_BUTTONS] = { 0 };

long longPressTime = 0;

// =====================================================
// TOUCHPOINT / BUTTON INDICES
// =====================================================
bool leftHand = true;  // mirrors the finger layout for the other hand

#define INDEX_A 6
#define INDEX_B 7
#define MIDDLE_A 4
#define MIDDLE_B 5
#define RING_A 2
#define RING_B 3
#define LITTLE_A 0
#define LITTLE_B 1

#define THUMB_L 8
#define THUMB_M 9
#define THUMB_R 10

#define BUTTON_R 0
#define BUTTON_L 1
#define WAKE_BUTTON_PIN 9

// =====================================================
// CC TRACKING / SEND HELPERS
// =====================================================
#define CC_UNSET 0xFF
uint8_t outputCCValue[128];  // last sent (0..127), CC_UNSET if never sent
uint32_t ccLastSentAt[128];  // millis() of last send per CC

inline bool ccValid(uint16_t cc) {
  return cc <= 127;
}

void initCcTracking() {
  for (int i = 0; i < 128; ++i) {
    outputCCValue[i] = CC_UNSET;
    ccLastSentAt[i] = 0;
  }
}

inline uint8_t quantize7(uint8_t v, uint8_t step) {
  if (step <= 1) return v;
  uint8_t q = (uint8_t)((v + (step / 2)) / step) * step;
  return (q > 127) ? 127 : q;
}

// Immediate send (gates, resets, preset changes)
inline void sendCC_immediate(uint16_t cc, uint16_t value, uint8_t channel) {
  if (ccDisabled(cc) || !ccValid(cc)) return;
  uint8_t v = (value > 127) ? 127 : (uint8_t)value;
  sendControlChange((uint8_t)cc, v, channel);
  outputCCValue[cc] = v;
  ccLastSentAt[cc] = millis();
}

// Throttled send (continuous streams)
inline void sendCC_throttled(uint16_t cc, uint16_t value, uint8_t channel,
                             uint8_t minStep, uint16_t minIntervalMs, uint8_t quantStep) {
  if (ccDisabled(cc) || !ccValid(cc)) return;
  uint8_t raw = (value > 127) ? 127 : (uint8_t)value;
  uint8_t v = quantize7(raw, quantStep);

  uint8_t last = outputCCValue[cc];
  uint32_t now = millis();
  uint32_t dt = now - ccLastSentAt[cc];

  if (last == CC_UNSET) {
    sendControlChange((uint8_t)cc, v, channel);
    outputCCValue[cc] = v;
    ccLastSentAt[cc] = now;
    return;
  }

  uint8_t diff = (last > v) ? (last - v) : (v - last);
  if (diff < minStep) return;
  if (dt < minIntervalMs) return;

  sendControlChange((uint8_t)cc, v, channel);
  outputCCValue[cc] = v;
  ccLastSentAt[cc] = now;
}

// =====================================================
// PRESETS
// =====================================================
// Mode/channel per preset: 0 = CC mode (uses MIDI_CHANNEL),
// 1..16 = NOTE mode on that MIDI channel.
static uint8_t presetMode[NUMBER_OF_PRESETS] = { 0, 0, 1, 0 };

// Preset-global wrist/elbow CCs (fields per palm_shared.h)
uint8_t presetGlobal[NUMBER_OF_PRESETS][4] = {
  /* preset 0 */ { /*G_WRIST_CC*/ 255, /*G_WRIST_RESET*/ 0, /*G_ELBOW_CC*/ 255, /*G_ELBOW_RESET*/ 0 },
  /* preset 1 */ { /*G_WRIST_CC*/ 255, /*G_WRIST_RESET*/ 0, /*G_ELBOW_CC*/ 255, /*G_ELBOW_RESET*/ 0 },
  /* preset 2 */ { /*G_WRIST_CC*/ 94,  /*G_WRIST_RESET*/ 0, /*G_ELBOW_CC*/ 255, /*G_ELBOW_RESET*/ 0 },
  /* preset 3 */ { /*G_WRIST_CC*/ 92,  /*G_WRIST_RESET*/ 0, /*G_ELBOW_CC*/ 93,  /*G_ELBOW_RESET*/ 0 }
};

// Per-finger mapping: preset[finger][presetIndex][field]
uint8_t preset[NUMBER_OF_FINGERS][NUMBER_OF_PRESETS][NUMBER_OF_BYTES_IN_PRESET] = {
  /* LITTLE_A */ {
    { /*TCH_CC,RNG*/  8,255,  /*WR_CC,RST*/ 255,0,  /*EL_CC,RST*/ 255,0 },
    { /*TCH_CC,RNG*/  8,255,  /*WR_CC,RST*/ 255,0,  /*EL_CC,RST*/ 255,0 },
    { /*TCH_CC,RNG*/  8,255,  /*WR_CC,RST*/ 255,0,  /*EL_CC,RST*/ 255,0 },
    { /*TCH_CC,RNG*/  8,255,  /*WR_CC,RST*/ 255,0,  /*EL_CC,RST*/ 255,0 }
  },

  /* LITTLE_B */ {
    { /*TCH_CC,RNG*/  7,255,  /*WR_CC,RST*/ 255,0,  /*EL_CC,RST*/ 255,0 },
    { /*TCH_CC,RNG*/  7,255,  /*WR_CC,RST*/ 255,0,  /*EL_CC,RST*/ 255,0 },
    { /*TCH_CC,RNG*/  7,255,  /*WR_CC,RST*/ 255,0,  /*EL_CC,RST*/ 255,0 },
    { /*TCH_CC,RNG*/  7,255,  /*WR_CC,RST*/ 255,0,  /*EL_CC,RST*/ 255,0 }
  },

  /* RING_A */ {
    { /*TCH_CC,RNG*/  6,255,  /*WR_CC,RST*/ 38,255, /*EL_CC,RST*/ 71,0 },
    { /*TCH_CC,RNG*/ 14,255,  /*WR_CC,RST*/ 46,127, /*EL_CC,RST*/ 79,0 },
    { /*TCH_CC,RNG*/ 22,255,  /*WR_CC,RST*/ 54,127, /*EL_CC,RST*/ 87,0 },
    { /*TCH_CC,RNG*/ 30,255,  /*WR_CC,RST*/ 92,0,   /*EL_CC,RST*/ 93,0 }
  },

  /* RING_B */ {
    { /*TCH_CC,RNG*/  5,255,  /*WR_CC,RST*/ 37,255, /*EL_CC,RST*/ 70,0 },
    { /*TCH_CC,RNG*/ 13,255,  /*WR_CC,RST*/ 45,127, /*EL_CC,RST*/ 78,0 },
    { /*TCH_CC,RNG*/ 21,255,  /*WR_CC,RST*/ 53,127, /*EL_CC,RST*/ 86,0 },
    { /*TCH_CC,RNG*/ 29,255,  /*WR_CC,RST*/ 92,0,   /*EL_CC,RST*/ 93,0 }
  },

  /* MIDDLE_A */ {
    { /*TCH_CC,RNG*/  4,255,  /*WR_CC,RST*/ 36,64,  /*EL_CC,RST*/ 69,0 },
    { /*TCH_CC,RNG*/ 12,255,  /*WR_CC,RST*/ 44,127, /*EL_CC,RST*/ 77,0 },
    { /*TCH_CC,RNG*/ 20,255,  /*WR_CC,RST*/ 52,127, /*EL_CC,RST*/ 85,0 },
    { /*TCH_CC,RNG*/ 28,255,  /*WR_CC,RST*/ 92,0,   /*EL_CC,RST*/ 93,0 }
  },

  /* MIDDLE_B */ {
    { /*TCH_CC,RNG*/  3,255,  /*WR_CC,RST*/ 35,64,  /*EL_CC,RST*/ 68,0 },
    { /*TCH_CC,RNG*/ 11,255,  /*WR_CC,RST*/ 43,127, /*EL_CC,RST*/ 76,0 },
    { /*TCH_CC,RNG*/ 19,255,  /*WR_CC,RST*/ 51,127, /*EL_CC,RST*/ 84,0 },
    { /*TCH_CC,RNG*/ 27,255,  /*WR_CC,RST*/ 92,0,   /*EL_CC,RST*/ 93,0 }
  },

  /* INDEX_A */ {
    { /*TCH_CC,RNG*/  2,255,  /*WR_CC,RST*/ 34,127, /*EL_CC,RST*/ 67,0 },
    { /*TCH_CC,RNG*/ 10,255,  /*WR_CC,RST*/ 42,127, /*EL_CC,RST*/ 75,0 },
    { /*TCH_CC,RNG*/ 18,255,  /*WR_CC,RST*/ 50,127, /*EL_CC,RST*/ 83,0 },
    { /*TCH_CC,RNG*/ 26,255,  /*WR_CC,RST*/ 92,0,   /*EL_CC,RST*/ 93,0 }
  },

  /* INDEX_B */ {
    { /*TCH_CC,RNG*/  1,255,  /*WR_CC,RST*/ 33,127, /*EL_CC,RST*/ 66,0 },
    { /*TCH_CC,RNG*/  9,255,  /*WR_CC,RST*/ 41,127, /*EL_CC,RST*/ 74,0 },
    { /*TCH_CC,RNG*/ 17,255,  /*WR_CC,RST*/ 49,127, /*EL_CC,RST*/ 82,0 },
    { /*TCH_CC,RNG*/ 25,255,  /*WR_CC,RST*/ 92,0,   /*EL_CC,RST*/ 93,0 }
  }
};

inline bool isNotePreset(uint8_t p) {
  return presetMode[p] >= 1 && presetMode[p] <= 16;
}
inline uint8_t presetMidiChannel(uint8_t p) {
  return isNotePreset(p) ? presetMode[p] : MIDI_CHANNEL;
}

// Per-preset selector CC (pinky-held)
static uint8_t presetSelectorCC[NUMBER_OF_PRESETS] = { 102, 102, 102, 102 };
inline uint8_t getSelectorCc(uint8_t p) { return presetSelectorCC[p]; }

// Smart-switch getters (specs per palm_shared.h)
TouchSpec getTouchSpec(uint8_t finger, uint8_t p) {
  uint8_t cc = preset[finger][p][TOUCH_CC];
  uint8_t rng = preset[finger][p][TOUCH_CC_RANGE];
  return TouchSpec{ cc, !ccDisabled(cc), isGate(rng) };
}
MotionSpec getWristSpec(uint8_t finger, uint8_t p) {
  uint8_t cc = preset[finger][p][WRIST_CC];
  return MotionSpec{ cc, preset[finger][p][WRIST_RESET], !ccDisabled(cc) };
}
MotionSpec getElbowSpec(uint8_t finger, uint8_t p) {
  uint8_t cc = preset[finger][p][ELBOW_CC];
  return MotionSpec{ cc, preset[finger][p][ELBOW_RESET], !ccDisabled(cc) };
}

// =====================================================
// INCOMING MIDI (host -> PALM, e.g. LED color)
// =====================================================
void onControlChange(uint8_t channel, uint8_t controller, uint8_t value, uint16_t timestamp) {
  rgb[controller % 3] = value;
}

// =====================================================
// MAPPING MODE + BUTTONS
// =====================================================
uint8_t mappingMode = 0;
#define NOT_MAPPING 0
#define MAP_WRIST 1
#define MAP_ELBOW 2
#define MAP_TOUCH 3

void handleButtons() {
  if (bleConnected && justPressed[BUTTON_L]) {
    mappingMode++;
    if (mappingMode > 3) mappingMode = 0;
  }
  // pairing experiment: BUTTON_L while disconnected wipes all BLE bonds
  if (!bleConnected && justPressed[BUTTON_L]) {
    bleWipeBonds();
  }
  if (justPressed[BUTTON_R]) {
    adverstiseBle();  // manual BLE advertising (starts the stack if needed)
    longPressTime = millis();
  }
  if (buttonState[BUTTON_R] && ((millis() - longPressTime)) > 3000) { sleep(); }
}

// Any finger touch (not thumbs) currently active?
bool anyContextTouchActive() {
  for (uint8_t i = 0; i < NUMBER_OF_FINGERS; ++i)
    if (touchState[i]) return true;
  return false;
}

// =====================================================
// MAIN
// =====================================================
void readSensors() {
  readTouchSensors();
  readButtons();
  readAccelerometer();
}

void setup() {
  wakeUp();
  Serial.begin(115200);
  delay(500);
  Serial.println("start");
  initHw();
  initMidi();
  initCcTracking();
  noteModeAllNotesOffLocal(MIDI_CHANNEL);
}

void loop() {
  readSensors();
  handleButtons();
  handleThumb();
  handleContext();

  midiRead();
  midiDispatch();

  // Status pixel: link state + mapping mode
#if defined(USE_ESPNOW_MIDI) && defined(USE_BLE_MIDI)
  bool linkUp = espnowLinked || bleConnected;
#elif defined(USE_ESPNOW_MIDI)
  bool linkUp = espnowLinked;
#else
  bool linkUp = bleConnected;
#endif
  if (linkUp) {
    if (mappingMode == MAP_WRIST) neopixelWrite(NEO_PIXEL_PIN, 0, 10, 0);
    else if (mappingMode == MAP_ELBOW) neopixelWrite(NEO_PIXEL_PIN, 0, 10, 10);
    else if (mappingMode == MAP_TOUCH) neopixelWrite(NEO_PIXEL_PIN, 20, 10, 10);
    else neopixelWrite(NEO_PIXEL_PIN, 0, 0, 5);
  } else {
    neopixelWrite(NEO_PIXEL_PIN, 10, 0, 0);
  }
  delay(1);
}
