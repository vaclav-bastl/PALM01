/*
Credits:
Developed by Václav Peloušek @toyotavangelis of Bastl Instruments at Pifcamp 2025
https://bastl-instruments.com
https://pif.camp

BLE MIDI aid from Rein Gundersen Bentdal who makes this cool instrument https://wavyindustries.com/monkey/

Contributions from pifcamp members:

Full documentation is here:
https://docs.google.com/document/d/1CUI6_zo0RurEk8GgtcJpuo6qGxklACaIrTYTMEp8r8M/edit?pli=1&tab=t.0

GitHub repository here:
https://github.com/vaclav-bastl/PALM01/

License TBD
*/

#include <Arduino.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include "Adafruit_MPR121.h"
#include <Adafruit_ADXL345_U.h>

const char* BLE_DEVICE_NAME = "PALM_01";  // Name shown to host devices

#define NEO_PIXEL_PIN D2

// === CONFIGURATION SECTION ===
#define USE_BLE_MIDI
#define USE_SERIAL_MIDI
// #define USE_USB_MIDI
#define USE_DEBUG
const uint8_t MIDI_CHANNEL = 1;  // Default channel (1-16)

// --- Accelerometer (externals should exist elsewhere) ---
#define ACCEL_HYSTERESIS 3
#define X_AXIS 0
#define Y_AXIS 1
#define Z_AXIS 2
int accelValue[3] = {0, 0, 0};           // latest raw (kept as int to preserve your range)
int accelRunningValue[3] = {0, 0, 0};    // moving average (same range)
int lastAccelRunningValue[3] = {0, 0, 0};

uint8_t rgb[3] = {0, 0, 0};
uint8_t currentPreset = 0;
uint8_t lastPreset = 0;

bool bleConnected = false;

// --- Touch & buttons ---
#define NUMBER_OF_TOUCHPOINTS 11
bool touchState[NUMBER_OF_TOUCHPOINTS] = {0};
bool lastTouchState[NUMBER_OF_TOUCHPOINTS] = {0};
bool justTouched[NUMBER_OF_TOUCHPOINTS] = {0};
bool justUntouched[NUMBER_OF_TOUCHPOINTS] = {0};
uint8_t runningTouchValue[NUMBER_OF_TOUCHPOINTS] = {0};

#define NUMBER_OF_BUTTONS 2
bool buttonState[NUMBER_OF_BUTTONS] = {0};
bool lastButtonState[NUMBER_OF_BUTTONS] = {0};
bool justPressed[NUMBER_OF_BUTTONS] = {0};
bool justReleased[NUMBER_OF_BUTTONS] = {0};

uint8_t activeIndex = 0;
uint8_t lastActiveIndex = 0;
uint8_t activeNote = 0, lastActiveNote = 0;
uint8_t touchThreshold = 120;

uint8_t note = 255;
uint8_t highestPointPressed = 0;
uint8_t newNote = 255;
uint8_t octave = 0;
long longPressTime = 0;

// --- Thumb / finger mapping indices ---
#define NUMBER_OF_BYTES_IN_PRESET 6
#define NUMBER_OF_PRESETS 4
#define NUMBER_OF_FINGERS 8

bool leftHand = true;  // set left/right priority for thumb points

#define INDEX_A   6
#define INDEX_B   7
#define MIDDLE_A  4
#define MIDDLE_B  5
#define RING_A    2
#define RING_B    3
#define LITTLE_A  0
#define LITTLE_B  1


// --- Pair arbitration settings ---
#define PAIR_HYST 8  // difference in calibratedTouchValue (0..255) required to steal focus

static constexpr uint8_t PAIR_COUNT = 4;
const uint8_t pairA[PAIR_COUNT] = { LITTLE_A, RING_A,  MIDDLE_A,  INDEX_A };
const uint8_t pairB[PAIR_COUNT] = { LITTLE_B, RING_B,  MIDDLE_B,  INDEX_B };

// -1 = no latch, 0 = A is latched, 1 = B is latched
static int8_t pairLatch[PAIR_COUNT] = { -1, -1, -1, -1 };



#define THUMB_L   8
#define THUMB_M   9
#define THUMB_R   10

#define BUTTON_R  0
#define BUTTON_L  1

#define WAKE_BUTTON_PIN 9

// ----- Preset byte layout per context point (finger) -----
// 0: TOUCH_CC        (0–127, 255=disabled)
// 1: TOUCH_CC_RANGE  (0–127 max pressure; 255=ON/OFF only)
// 2: WRIST_CC        (0–127, 255=disabled)
// 3: WRIST_RESET     (0–127 value on release; 255=hang)
// 4: ELBOW_CC        (0–127, 255=disabled)
// 5: ELBOW_RESET     (0–127 value on release; 255=hang)
enum PresetField : uint8_t {
  TOUCH_CC = 0,
  TOUCH_CC_RANGE,
  WRIST_CC,
  WRIST_RESET,
  ELBOW_CC,
  ELBOW_RESET
};

#define BYTES_PER_PRESET NUMBER_OF_BYTES_IN_PRESET
#define NUM_PRESETS      NUMBER_OF_PRESETS
#define NUM_FINGERS      NUMBER_OF_FINGERS

// Presets[finger][preset][field]
// Presets[finger][presetIndex][field]
// Field groups per entry: /*TCH_CC,RNG*/  ,  /*WR_CC,RST*/  ,  /*EL_CC,RST*/
uint8_t preset[NUM_FINGERS][NUM_PRESETS][BYTES_PER_PRESET] = {
  /* LITTLE_A */
  {
    { /*TCH_CC,RNG*/  8,255,  /*WR_CC,RST*/ 255,0,  /*EL_CC,RST*/ 255,  0 }, // preset 0
    { /*TCH_CC,RNG*/  8,255,  /*WR_CC,RST*/ 255,0,  /*EL_CC,RST*/ 255,  0 }, // preset 1
    {/*TCH_CC,RNG*/  8,255,  /*WR_CC,RST*/ 255,0,  /*EL_CC,RST*/ 255,  0 }, // preset 2
    {/*TCH_CC,RNG*/  8,255,  /*WR_CC,RST*/ 255,0,  /*EL_CC,RST*/ 255,  0 }  // preset 3
  },

  /* LITTLE_B */
  {
    { /*TCH_CC,RNG*/  7,255,  /*WR_CC,RST*/ 255,0,  /*EL_CC,RST*/ 255,  0 }, // preset 0
    { /*TCH_CC,RNG*/  7,255,  /*WR_CC,RST*/ 255,0,  /*EL_CC,RST*/ 255,  0 }, // preset 1
    { /*TCH_CC,RNG*/  7,255,  /*WR_CC,RST*/ 255,0,  /*EL_CC,RST*/ 255,  0 }, // preset 2
    { /*TCH_CC,RNG*/  7,255,  /*WR_CC,RST*/ 255,0,  /*EL_CC,RST*/ 255,  0 }  // preset 3
  },

  /* RING_A */
  {
    { /*TCH_CC,RNG*/  6,255,  /*WR_CC,RST*/ 38,255,  /*EL_CC,RST*/ 71,  0 }, // preset 0
    { /*TCH_CC,RNG*/ 14,255,  /*WR_CC,RST*/ 46,127,  /*EL_CC,RST*/ 79,  0 }, // preset 1
    { /*TCH_CC,RNG*/ 22,255,  /*WR_CC,RST*/ 54,127,  /*EL_CC,RST*/ 87,  0 }, // preset 2
    { /*TCH_CC,RNG*/ 30,255,  /*WR_CC,RST*/ 62,127,  /*EL_CC,RST*/ 95,  0 }  // preset 3
  },

  /* RING_B */
  {
    { /*TCH_CC,RNG*/  5,255,  /*WR_CC,RST*/ 37,255,  /*EL_CC,RST*/ 70,  0 }, // preset 0
    { /*TCH_CC,RNG*/ 13,255,  /*WR_CC,RST*/ 45,127,  /*EL_CC,RST*/ 78,  0 }, // preset 1
    { /*TCH_CC,RNG*/ 21,255,  /*WR_CC,RST*/ 53,127,  /*EL_CC,RST*/ 86,  0 }, // preset 2
    { /*TCH_CC,RNG*/ 29,255,  /*WR_CC,RST*/ 61,127,  /*EL_CC,RST*/ 94,  0 }  // preset 3
  },

  /* MIDDLE_A */
  {
    { /*TCH_CC,RNG*/  4,255,  /*WR_CC,RST*/ 36, 64,  /*EL_CC,RST*/255,  0 }, // preset 0
    { /*TCH_CC,RNG*/ 12,127,  /*WR_CC,RST*/ 44,127,  /*EL_CC,RST*/ 77,  0 }, // preset 1
    { /*TCH_CC,RNG*/ 20,255,  /*WR_CC,RST*/ 52,127,  /*EL_CC,RST*/ 85,  0 }, // preset 2
    { /*TCH_CC,RNG*/ 28,255,  /*WR_CC,RST*/ 60,127,  /*EL_CC,RST*/ 93,  0 }  // preset 3
  },

  /* MIDDLE_B */
  {
    { /*TCH_CC,RNG*/  3,255,  /*WR_CC,RST*/ 35, 64,  /*EL_CC,RST*/255,  0 }, // preset 0
    { /*TCH_CC,RNG*/ 11,255,  /*WR_CC,RST*/ 43,127,  /*EL_CC,RST*/ 76,  0 }, // preset 1
    { /*TCH_CC,RNG*/ 19,255,  /*WR_CC,RST*/ 51,127,  /*EL_CC,RST*/ 84,  0 }, // preset 2
    { /*TCH_CC,RNG*/ 27,255,  /*WR_CC,RST*/ 59,127,  /*EL_CC,RST*/ 92,  0 }  // preset 3
  },

  /* INDEX_A */
  {
    { /*TCH_CC,RNG*/  2,255,  /*WR_CC,RST*/255,127,  /*EL_CC,RST*/255,  0 }, // preset 0
    { /*TCH_CC,RNG*/ 10,255,  /*WR_CC,RST*/ 42,127,  /*EL_CC,RST*/ 75,  0 }, // preset 1
    { /*TCH_CC,RNG*/ 18,255,  /*WR_CC,RST*/ 50,127,  /*EL_CC,RST*/ 83,  0 }, // preset 2
    { /*TCH_CC,RNG*/ 26,255,  /*WR_CC,RST*/ 58,127,  /*EL_CC,RST*/ 91,  0 }  // preset 3
  },

  /* INDEX_B */
  {
    { /*TCH_CC,RNG*/  1,255,  /*WR_CC,RST*/255,127,  /*EL_CC,RST*/255,  0 }, // preset 0
    { /*TCH_CC,RNG*/  9,255,  /*WR_CC,RST*/ 41,127,  /*EL_CC,RST*/ 74,  0 }, // preset 1
    { /*TCH_CC,RNG*/ 17,255,  /*WR_CC,RST*/ 49,127,  /*EL_CC,RST*/ 82,  0 }, // preset 2
    { /*TCH_CC,RNG*/ 25,255,  /*WR_CC,RST*/ 57,127,  /*EL_CC,RST*/ 90,  0 }  // preset 3
  }
};

struct TouchSpec {
  uint8_t cc;
  bool enabled;
  bool gate;   // true = gate(on/off), false = continuous
};

struct MotionSpec {
  uint8_t cc;
  uint8_t reset;   // value to set on release
  bool enabled;
};

inline TouchSpec getTouchSpec(uint8_t finger, uint8_t p) {
  uint8_t cc  = preset[finger][p][TOUCH_CC];
  uint8_t rng = preset[finger][p][TOUCH_CC_RANGE];
  return TouchSpec{ cc, !isDisabled(cc), isGate(rng) };
}

inline MotionSpec getWristSpec(uint8_t finger, uint8_t p) {
  uint8_t cc = preset[finger][p][WRIST_CC];
  return MotionSpec{ cc, preset[finger][p][WRIST_RESET], !isDisabled(cc) };
}

inline MotionSpec getElbowSpec(uint8_t finger, uint8_t p) {
  uint8_t cc = preset[finger][p][ELBOW_CC];
  return MotionSpec{ cc, preset[finger][p][ELBOW_RESET], !isDisabled(cc) };
}


// --- CC tracking / safety layer ---
#define CC_UNSET 0xFF
uint8_t outputCCValue[128];  // 0..127 valid CC numbers

inline bool ccDisabled(uint16_t cc) { return cc == 255; }
inline bool ccValid(uint16_t cc)    { return cc <= 127; }

inline void sendCC_ifChanged(uint16_t cc, uint16_t value, uint8_t channel) {
  if (ccDisabled(cc) || !ccValid(cc)) return;
  uint8_t v = (value > 127) ? 127 : (uint8_t)value;
  if (outputCCValue[cc] == v) return;         // no-op if unchanged
  sendControlChange((uint8_t)cc, v, channel); // external
  outputCCValue[cc] = v;                      // track last sent
}

void initCcTracking() {
  for (int i = 0; i < 128; ++i) outputCCValue[i] = CC_UNSET;
}

// --- Incoming CC hook (kept) ---
void onControlChange(uint8_t channel, uint8_t controller, uint8_t value, uint16_t timestamp) {
  rgb[controller % 3] = value;
  // Serial.printf("Received control change : channel %d, controller %d, value %d (timestamp %dms)\n", channel, controller, value, timestamp);
}

// --- Sensor reads (externals) ---
void readSensors() {
  readTouchSensors();
  readButtons();
  readAccelerometer();
}

// --- Mapping mode ---
uint8_t mappingMode = 0;
#define NOT_MAPPING 0
#define MAP_WRIST   1
#define MAP_ELBOW   2
#define MAP_TOUCH   3

// --- Buttons handler ---
void handleButtons() {
  if (bleConnected) {
    if (justPressed[BUTTON_L]) {
      mappingMode++;
      if (mappingMode > 3) mappingMode = 0;
    }
  }
  if (justPressed[BUTTON_R]) {
    adverstiseBle(); // keep original symbol if defined that way elsewhere
    longPressTime = millis();
  }
  if (buttonState[BUTTON_R] && ((millis() - longPressTime)) > 3000) {
    sleep();
  }
}

// --- Setup / Loop ---
void setup() {
  wakeUp();
  Serial.begin(115200);
  delay(500);
  Serial.println("start");

  initHw();
  initMidi();
  initCcTracking();
}


// --- Helpers for smart preset switching ---
inline bool isDisabled(uint8_t cc) { return cc == 255; }
inline bool isGate(uint8_t rng)    { return rng == 255; }   // 255 means gate mode for TOUCH



void switchPreset() {                                // brute-force: more MIDI traffic, robust state
  for (uint8_t i = 0; i < NUM_FINGERS; i++) {
    // TOUCH resets (both gate & continuous reset to 0)
    if (mappingMode == NOT_MAPPING || mappingMode == MAP_TOUCH) {
      uint8_t cc = preset[i][lastPreset][TOUCH_CC];
      if (!ccDisabled(cc)) {
        if (outputCCValue[cc] != 0) sendCC_ifChanged(cc, 0, MIDI_CHANNEL);
      }
    }

    // WRIST reset to preset value (unless disabled)
    if (mappingMode == NOT_MAPPING || mappingMode == MAP_WRIST) {
      uint8_t cc = preset[i][lastPreset][WRIST_CC];
      if (!ccDisabled(cc)) {
        uint8_t tgt = preset[i][lastPreset][WRIST_RESET];
        sendCC_ifChanged(cc, tgt, MIDI_CHANNEL);
      }
    }

    // ELBOW reset to preset value (unless disabled)
    if (mappingMode == NOT_MAPPING || mappingMode == MAP_ELBOW) {
      uint8_t cc = preset[i][lastPreset][ELBOW_CC];
      if (!ccDisabled(cc)) {
        uint8_t tgt = preset[i][lastPreset][ELBOW_RESET];
        sendCC_ifChanged(cc, tgt, MIDI_CHANNEL);
      }
    }

    // Dispatch a bit after each finger to avoid buffer overflow
    midiDispatch();
    delay(1);
  }
}


void switchPresetSmart() {
  for (uint8_t i = 0; i < NUM_FINGERS; i++) {

    // ----- TOUCH -----
    if (mappingMode == NOT_MAPPING || mappingMode == MAP_TOUCH) {
      TouchSpec oldT = getTouchSpec(i, lastPreset);
      TouchSpec newT = getTouchSpec(i, currentPreset);

      if (oldT.enabled) {
        // Reset only if the *meaning* changed: CC changed OR gate/continuous changed OR now disabled
        bool meaningChanged = (!newT.enabled) || (oldT.cc != newT.cc) || (oldT.gate != newT.gate);
        if (meaningChanged) {
          // Clear old touch output so we don't leave a hanging value
          sendCC_ifChanged(oldT.cc, 0, MIDI_CHANNEL);
        }
        // else: same CC and same mode -> keep value (no reset)
      }
    }

    // ----- WRIST -----
    if (mappingMode == NOT_MAPPING || mappingMode == MAP_WRIST) {
      MotionSpec oldW = getWristSpec(i, lastPreset);
      MotionSpec newW = getWristSpec(i, currentPreset);

      if (oldW.enabled) {
        // Reset only if CC or reset semantics changed, or it becomes disabled
        bool meaningChanged = (!newW.enabled) || (oldW.cc != newW.cc) || (oldW.reset != newW.reset);
        if (meaningChanged) {
          sendCC_ifChanged(oldW.cc, oldW.reset, MIDI_CHANNEL);
        }
        // else: same CC+reset -> keep current value (no reset)
      }
    }

    // ----- ELBOW -----
    if (mappingMode == NOT_MAPPING || mappingMode == MAP_ELBOW) {
      MotionSpec oldE = getElbowSpec(i, lastPreset);
      MotionSpec newE = getElbowSpec(i, currentPreset);

      if (oldE.enabled) {
        bool meaningChanged = (!newE.enabled) || (oldE.cc != newE.cc) || (oldE.reset != newE.reset);
        if (meaningChanged) {
          sendCC_ifChanged(oldE.cc, oldE.reset, MIDI_CHANNEL);
        }
      }
    }

    // keep your pacing to avoid BLE MIDI buffer overflow
    midiDispatch();
    delay(1);
  }
}


void handleContext() {
  for (uint8_t i = 0; i < NUM_FINGERS; i++) {
    // TOUCH: gate or pressure
    if (mappingMode == NOT_MAPPING || mappingMode == MAP_TOUCH) {
      uint8_t cc = preset[i][currentPreset][TOUCH_CC];
      if (!ccDisabled(cc)) {
        if (preset[i][currentPreset][TOUCH_CC_RANGE] == 255) {
          // ON/OFF gate
          if (justTouched[i])   sendCC_ifChanged(cc, 127, MIDI_CHANNEL);
          if (justUntouched[i]) sendCC_ifChanged(cc,   0, MIDI_CHANNEL);
        } else {
          // Continuous pressure (while touched), reset to 0 on release
          if (touchState[i]) {
            uint8_t newV = runningTouchValue[i];
            sendCC_ifChanged(cc, newV, MIDI_CHANNEL);
          }
          if (justUntouched[i]) {
            sendCC_ifChanged(cc, 0, MIDI_CHANNEL);
          }
        }
      }
    }

    // MOTION (only while finger is active)
    if (touchState[i]) {
      // WRIST
      if (mappingMode == NOT_MAPPING || mappingMode == MAP_WRIST) {
        uint8_t cc = preset[i][currentPreset][WRIST_CC];
        if (!ccDisabled(cc)) {
          uint8_t newV = constrain(
              stickyMap(accelRunningValue[Z_AXIS], -200, 255, 127, 0,
                        lastAccelRunningValue[Z_AXIS], ACCEL_HYSTERESIS),
              0, 127);
          sendCC_ifChanged(cc, newV, MIDI_CHANNEL);
        }
      }

      // ELBOW
      if (mappingMode == NOT_MAPPING || mappingMode == MAP_ELBOW) {
        uint8_t cc = preset[i][currentPreset][ELBOW_CC];
        if (!ccDisabled(cc)) {
          uint8_t newV = constrain(
              stickyMap(accelRunningValue[X_AXIS], -255, 255, 127, 0,
                        lastAccelRunningValue[X_AXIS], ACCEL_HYSTERESIS),
              0, 127);
          sendCC_ifChanged(cc, newV, MIDI_CHANNEL);
        }
      }
    }

    // Resets on release
    if (justUntouched[i]) {
      if (mappingMode == NOT_MAPPING || mappingMode == MAP_WRIST) {
        uint8_t cc = preset[i][currentPreset][WRIST_CC];
        if (!ccDisabled(cc)) {
          uint8_t tgt = preset[i][currentPreset][WRIST_RESET];
          sendCC_ifChanged(cc, tgt, MIDI_CHANNEL);
        }
      }
      if (mappingMode == NOT_MAPPING || mappingMode == MAP_ELBOW) {
        uint8_t cc = preset[i][currentPreset][ELBOW_CC];
        if (!ccDisabled(cc)) {
          uint8_t tgt = preset[i][currentPreset][ELBOW_RESET];
          sendCC_ifChanged(cc, tgt, MIDI_CHANNEL);
        }
      }
    }

    // Avoid flooding
    midiDispatch();
    delay(1);
  }
}

void handleThumb() {
  lastPreset = currentPreset;
  currentPreset = 0;
  if (leftHand) {
    if (touchState[THUMB_L]) currentPreset = 1;
    if (touchState[THUMB_M]) currentPreset = 2;
    if (touchState[THUMB_R]) currentPreset = 3;
  } else {
    if (touchState[THUMB_R]) currentPreset = 1;
    if (touchState[THUMB_M]) currentPreset = 2;
    if (touchState[THUMB_L]) currentPreset = 3;
  }
  if (lastPreset != currentPreset) switchPresetSmart();
}


void loop() {
  readSensors();

  handleButtons();
  handleThumb();
  handleContext();

  midiRead();     // external
  // midiDispatch(); // already dispatching within loops

  if (bleConnected) {
    if      (mappingMode == MAP_WRIST) neopixelWrite(NEO_PIXEL_PIN, 0, 10, 0);
    else if (mappingMode == MAP_ELBOW) neopixelWrite(NEO_PIXEL_PIN, 0, 10, 10);
    else if (mappingMode == MAP_TOUCH) neopixelWrite(NEO_PIXEL_PIN, 20, 10, 10);
    else                               neopixelWrite(NEO_PIXEL_PIN, 0, 0, 5);
  } else {
    // adverstiseBle();
    neopixelWrite(NEO_PIXEL_PIN, 10, 0, 0);
  }

  delay(20);
}
