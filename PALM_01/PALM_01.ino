#include "palm_shared.h"

#include <Adafruit_Sensor.h>
#include <Wire.h>
#include "Adafruit_MPR121.h"
#include <Adafruit_ADXL345_U.h>

// ======================== CONFIG ========================
const char* BLE_DEVICE_NAME = "PALM_03";
#define NEO_PIXEL_PIN D2

// Fixed velocity for the first note in NOTE mode
#define NOTE_FIRST_VELOCITY 64

#define USE_BLE_MIDI
#define USE_SERIAL_MIDI
// #define USE_USB_MIDI
//#define USE_DEBUG

// Default channel used when a preset is in CC mode (presetMode == 0)
const uint8_t MIDI_CHANNEL = 1;  // 1..16

// ======================== ACCEL =========================
#define ACCEL_HYSTERESIS 3
#define X_AXIS 0
#define Y_AXIS 1
#define Z_AXIS 2
int accelValue[3] = { 0, 0, 0 };
int accelRunningValue[3] = { 0, 0, 0 };
int lastAccelRunningValue[3] = { 0, 0, 0 };

// ====================== GLOBAL STATE ====================
uint8_t rgb[3] = { 0, 0, 0 };
uint8_t currentPreset = 0;
uint8_t lastPreset = 0;
bool bleConnected = false;

// =================== TOUCH & BUTTONS ====================
bool touchState[NUMBER_OF_TOUCHPOINTS] = { 0 };
bool lastTouchState[NUMBER_OF_TOUCHPOINTS] = { 0 };
bool justTouched[NUMBER_OF_TOUCHPOINTS] = { 0 };
bool justUntouched[NUMBER_OF_TOUCHPOINTS] = { 0 };
uint8_t runningTouchValue[NUMBER_OF_TOUCHPOINTS] = { 0 };

#define NUMBER_OF_BUTTONS 2
bool buttonState[NUMBER_OF_BUTTONS] = { 0 };
bool lastButtonState[NUMBER_OF_BUTTONS] = { 0 };
bool justPressed[NUMBER_OF_BUTTONS] = { 0 };
bool justReleased[NUMBER_OF_BUTTONS] = { 0 };

long longPressTime = 0;

// Which touch index produced the current root (and optional partner in 7th case)
int8_t g_rootTouchIdxPrimary = -1;    // the one that maps to the root note itself
int8_t g_rootTouchIdxSecondary = -1;  // the other finger in the 7th pair (if any)

// =================== CONTEXT INDICES ====================
bool leftHand = true;  // shared with other tabs

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

// ================== CC TRACKING / FILTERS ===============
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

// ---------- Mode/channel per preset ----------
// 0 = CC mode (use MIDI_CHANNEL)
// 1..16 = NOTE mode, and value is the MIDI channel for this preset.
static uint8_t presetMode[NUMBER_OF_PRESETS] = { 0, 0, 1, 0 };  // example: preset 2 is NOTE mode on ch 1, preset 3 is NOTE on ch2

// ---------- Preset-global CCs (matches palm_shared.h exactly) ----------
uint8_t presetGlobal[NUMBER_OF_PRESETS][4] = {
  /* preset 0 */ { /*G_WRIST_CC*/ 255, /*G_WRIST_RESET*/ 0, /*G_ELBOW_CC*/ 255, /*G_ELBOW_RESET*/ 0 },
  /* preset 1 */ { /*G_WRIST_CC*/ 255, /*G_WRIST_RESET*/ 0, /*G_ELBOW_CC*/ 255, /*G_ELBOW_RESET*/ 0 },
  /* preset 2 */ { /*G_WRIST_CC*/ 94,  /*G_WRIST_RESET*/ 0, /*G_ELBOW_CC*/ 255, /*G_ELBOW_RESET*/ 0 },
  /* preset 3 */ { /*G_WRIST_CC*/ 92,  /*G_WRIST_RESET*/ 0, /*G_ELBOW_CC*/ 93,  /*G_ELBOW_RESET*/ 0 }
};

// ===================== PRESET TABLES ====================
// Per-finger mapping: Presets[finger][presetIndex][field]
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

// ---------- Per-preset selector CC (pinky-held) ----------
static uint8_t presetSelectorCC[NUMBER_OF_PRESETS] = { 102, 102, 102, 102 };
inline uint8_t getSelectorCc(uint8_t p) { return presetSelectorCC[p]; }

// ============== SMART SWITCH GETTERS ====================
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

// ================= INCOMING CC (optional UI) ============
void onControlChange(uint8_t channel, uint8_t controller, uint8_t value, uint16_t timestamp) {
  rgb[controller % 3] = value;
}

// =================== SENSOR AGGREGATION =================
void readSensors() {
  readTouchSensors();
  readButtons();
  readAccelerometer();
}

// =================== MAPPING MODE =======================
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
  if (justPressed[BUTTON_R]) {
    adverstiseBle();
    longPressTime = millis();
  }
  if (buttonState[BUTTON_R] && ((millis() - longPressTime)) > 3000) { sleep(); }
}

// ===================== CONTEXT HELPERS ==================
bool anyContextTouchActive() {
  for (uint8_t i = 0; i < NUMBER_OF_FINGERS; ++i)
    if (touchState[i]) return true;
  return false;
}

// ================== GLOBAL OVERRIDE (CC MODE) ==================
static bool g_suppressGlobalWrist = false;
static bool g_suppressGlobalElbow = false;

inline bool axisCcIsActive(uint8_t cc) {
  return (cc != 255) && (!ccDisabled(cc));
}

inline bool anyFingerWristOverrideActive(uint8_t p) {
  for (uint8_t i = 0; i < NUMBER_OF_FINGERS; ++i) {
    if (!touchState[i]) continue;
    uint8_t cc = preset[i][p][WRIST_CC];
    if (axisCcIsActive(cc)) return true;
  }
  return false;
}

inline bool anyFingerElbowOverrideActive(uint8_t p) {
  for (uint8_t i = 0; i < NUMBER_OF_FINGERS; ++i) {
    if (!touchState[i]) continue;
    uint8_t cc = preset[i][p][ELBOW_CC];
    if (axisCcIsActive(cc)) return true;
  }
  return false;
}

// NEW: used to avoid resetting global when the active finger uses the same CC as global
inline bool anyActiveFingerAxisUsesCc(uint8_t p, uint8_t axisField /* WRIST_CC or ELBOW_CC */, uint8_t ccToMatch) {
  if (ccToMatch == 255) return false;
  for (uint8_t i = 0; i < NUMBER_OF_FINGERS; ++i) {
    if (!touchState[i]) continue;
    uint8_t cc = preset[i][p][axisField];
    if (!axisCcIsActive(cc)) continue;
    if (cc == ccToMatch) return true;
  }
  return false;
}

inline uint8_t computeGlobalWristValue() {
  return (uint8_t)constrain(
    stickyMap(accelRunningValue[Z_AXIS], -200, 255, 127, 0,
              lastAccelRunningValue[Z_AXIS], ACCEL_HYSTERESIS),
    0, 127
  );
}
inline uint8_t computeGlobalElbowValue() {
  return (uint8_t)constrain(
    stickyMap(accelRunningValue[X_AXIS], -255, 255, 127, 0,
              lastAccelRunningValue[X_AXIS], ACCEL_HYSTERESIS),
    0, 127
  );
}

// ======== NOTE MODE HELPERS / STATE (isolated to this TU) ========
namespace {
static const int8_t kOffsetsTriad[13] = { 0, 4, 7, 12, 16, 19, 24, 28, 31, 36, 40, 43, 48 };
static const int8_t kOffsetsWith7[17] = { 0, 4, 7, 10, 12, 16, 19, 22, 24, 28, 31, 34, 36, 40, 43, 46, 48 };

inline int elbowToIndex(uint8_t v, int bucketCount) {
  int idx = (int)((uint16_t)v * bucketCount / 128);
  int maxIdx = bucketCount - 1;
  return (idx > maxIdx) ? maxIdx : idx;
}
inline uint8_t elbowVelocity(uint8_t nowV, uint8_t lastV, int bucketCount) {
  const float bucketWidth = 128.0f / (float)bucketCount;
  float delta = (float)((nowV > lastV) ? (nowV - lastV) : (lastV - nowV));
  float frac = delta / bucketWidth;
  if (frac >= 1.0f) return 127;
  int vel = (int)(1.0f + frac * 126.0f);
  if (vel < 1) vel = 1;
  if (vel > 127) vel = 127;
  return (uint8_t)vel;
}

static const int MIDI_NOTE_C3 = 24;
static const int MIDI_NOTE_D3 = 26;
static const int MIDI_NOTE_E3 = 28;
static const int MIDI_NOTE_F3 = 29;
static const int MIDI_NOTE_G3 = 31;
static const int MIDI_NOTE_A3 = 33;
static const int MIDI_NOTE_B3 = 35;

bool g_addSeventh = false;

static inline bool detectDominant7Pair(bool leftHandLocal, bool& seventhOut, int& rootOut) {
  auto a_b = [&](uint8_t a, uint8_t b) -> uint8_t { return leftHandLocal ? a : b; };

  uint8_t idxA = a_b(INDEX_A, INDEX_B);
  uint8_t idxB = a_b(INDEX_B, INDEX_A);
  uint8_t midA = a_b(MIDDLE_A, MIDDLE_B);
  uint8_t midB = a_b(MIDDLE_B, MIDDLE_A);
  uint8_t rngA = a_b(RING_A, RING_B);
  uint8_t rngB = a_b(RING_B, RING_A);
  uint8_t litB = a_b(LITTLE_B, LITTLE_A);

  bool tC = touchState[idxB];
  bool tD = touchState[midB];
  bool tE = touchState[rngB];
  bool tF = touchState[litB];
  bool tG = touchState[idxA];
  bool tA = touchState[midA];
  bool tB = touchState[rngA];

  if (tC && tB) { seventhOut = true; rootOut = MIDI_NOTE_C3; g_rootTouchIdxPrimary = idxB; g_rootTouchIdxSecondary = rngA; return true; }
  if (tD && tC) { seventhOut = true; rootOut = MIDI_NOTE_D3; g_rootTouchIdxPrimary = midB; g_rootTouchIdxSecondary = idxB; return true; }
  if (tE && tD) { seventhOut = true; rootOut = MIDI_NOTE_E3; g_rootTouchIdxPrimary = rngB; g_rootTouchIdxSecondary = midB; return true; }
  if (tF && tE) { seventhOut = true; rootOut = MIDI_NOTE_F3; g_rootTouchIdxPrimary = litB; g_rootTouchIdxSecondary = rngB; return true; }
  if (tG && tF) { seventhOut = true; rootOut = MIDI_NOTE_G3; g_rootTouchIdxPrimary = idxA; g_rootTouchIdxSecondary = litB; return true; }
  if (tA && tG) { seventhOut = true; rootOut = MIDI_NOTE_A3; g_rootTouchIdxPrimary = midA; g_rootTouchIdxSecondary = idxA; return true; }
  if (tB && tA) { seventhOut = true; rootOut = MIDI_NOTE_B3; g_rootTouchIdxPrimary = rngA; g_rootTouchIdxSecondary = midA; return true; }

  seventhOut = false;
  return false;
}

int getRootFromTouchesLocal(bool leftHandLocal) {
  g_rootTouchIdxPrimary = -1;
  g_rootTouchIdxSecondary = -1;

  int rootIfPair = -1;
  bool sev = false;
  if (detectDominant7Pair(leftHandLocal, sev, rootIfPair)) {
    g_addSeventh = sev;
    return rootIfPair;
  }

  g_addSeventh = false;
  auto a_b = [&](uint8_t a, uint8_t b) -> uint8_t { return leftHandLocal ? a : b; };

  uint8_t idxA = a_b(INDEX_A, INDEX_B);
  uint8_t idxB = a_b(INDEX_B, INDEX_A);
  uint8_t midA = a_b(MIDDLE_A, MIDDLE_B);
  uint8_t midB = a_b(MIDDLE_B, MIDDLE_A);
  uint8_t rngA = a_b(RING_A, RING_B);
  uint8_t rngB = a_b(RING_B, RING_A);
  uint8_t litB = a_b(LITTLE_B, LITTLE_A);

  int candidatesMidi[7];
  uint8_t candidatesIdx[7];
  int n = 0;

  auto addIf = [&](uint8_t finger, int midi) {
    if (touchState[finger]) {
      candidatesMidi[n] = midi;
      candidatesIdx[n] = finger;
      n++;
    }
  };

  addIf(idxB, MIDI_NOTE_C3);
  addIf(midB, MIDI_NOTE_D3);
  addIf(rngB, MIDI_NOTE_E3);
  addIf(litB, MIDI_NOTE_F3);
  addIf(idxA, MIDI_NOTE_G3);
  addIf(midA, MIDI_NOTE_A3);
  addIf(rngA, MIDI_NOTE_B3);

  if (n == 0) return -1;

  int minMidi = candidatesMidi[0];
  uint8_t minIdx = candidatesIdx[0];
  for (int i = 1; i < n; i++) {
    if (candidatesMidi[i] < minMidi) {
      minMidi = candidatesMidi[i];
      minIdx = candidatesIdx[i];
    }
  }
  g_rootTouchIdxPrimary = (int8_t)minIdx;
  return minMidi;
}

inline int nearestOctaveRoot(int target, int rootBase) {
  int diff = target - rootBase;
  int k = (diff >= 0) ? ((diff + 6) / 12) : ((diff - 6) / 12);
  return rootBase + 12 * k;
}

int8_t noteQuantIdx = -1;
int8_t lastMainNote = -1;
int8_t last7thNote = -1;
uint8_t lastElbowVal = 0;
int lastRootBase = -100;
int lastBucketCount = 13;

inline void midiNoteOnHelper(uint8_t ch, uint8_t note, uint8_t vel) { sendNoteOn(note, vel, ch); }
inline void midiNoteOffHelper(uint8_t ch, uint8_t note) { sendNoteOff(note, 0, ch); }

void noteModeAllNotesOffLocal(uint8_t ch) {
  if (lastMainNote >= 0) { midiNoteOffHelper(ch, (uint8_t)lastMainNote); lastMainNote = -1; }
  if (last7thNote >= 0) { midiNoteOffHelper(ch, (uint8_t)last7thNote); last7thNote = -1; }
  noteQuantIdx = -1;
}
}  // namespace

// ================= SMART PRESET SWITCH ==================
void switchPresetSmart() {
  uint8_t lastCh = presetMidiChannel(lastPreset);
  uint8_t newCh = presetMidiChannel(currentPreset);

  // reset CC-mode suppression state on preset change
  g_suppressGlobalWrist = false;
  g_suppressGlobalElbow = false;

  // --- Handle NOTE preset transitions: kill any sounding notes ---
  if (isNotePreset(lastPreset) || isNotePreset(currentPreset)) {
    noteModeAllNotesOffLocal(lastCh);

    if (isNotePreset(lastPreset) && !isNotePreset(currentPreset)) {
      sendCC_immediate(123, 0, lastCh);  // All Notes Off
    }

    lastRootBase = -100;
  }

  // --- Always reset OLD preset's GLOBAL CCs on preset change (mode-agnostic) ---
  if (mappingMode == NOT_MAPPING || mappingMode == MAP_WRIST) {
    uint8_t occ  = presetGlobal[lastPreset][G_WRIST_CC];
    uint8_t oset = presetGlobal[lastPreset][G_WRIST_RESET];
    if (!ccDisabled(occ) && oset != 255) {
      sendCC_immediate(occ, oset, lastCh);
    }
  }
  if (mappingMode == NOT_MAPPING || mappingMode == MAP_ELBOW) {
    uint8_t occ  = presetGlobal[lastPreset][G_ELBOW_CC];
    uint8_t oset = presetGlobal[lastPreset][G_ELBOW_RESET];
    if (!ccDisabled(occ) && oset != 255) {
      sendCC_immediate(occ, oset, lastCh);
    }
  }

  // --- If we are ENTERING NOTE mode, force-reset all old preset TOUCH CCs to 0
  if (isNotePreset(currentPreset) && !isNotePreset(lastPreset)) {
    for (uint8_t i = 0; i < NUMBER_OF_FINGERS; ++i) {
      TouchSpec oldT = getTouchSpec(i, lastPreset);
      if (oldT.enabled) {
        sendCC_immediate(oldT.cc, 0, lastCh);
      }
    }
  }

  // --- Per-finger meaning-aware resets only if there is touch context ---
  if (!anyContextTouchActive()) return;

  for (uint8_t i = 0; i < NUMBER_OF_FINGERS; i++) {
    if (mappingMode == NOT_MAPPING || mappingMode == MAP_TOUCH) {
      TouchSpec oldT = getTouchSpec(i, lastPreset);
      TouchSpec newT = getTouchSpec(i, currentPreset);
      if (oldT.enabled) {
        bool meaningChanged = (!newT.enabled) || (oldT.cc != newT.cc) || (oldT.gate != newT.gate);
        if (meaningChanged) sendCC_immediate(oldT.cc, 0, lastCh);
      }
    }

    if (mappingMode == NOT_MAPPING || mappingMode == MAP_WRIST) {
      MotionSpec oldW = getWristSpec(i, lastPreset);
      MotionSpec newW = getWristSpec(i, currentPreset);
      if (oldW.enabled) {
        bool meaningChanged = (!newW.enabled) || (oldW.cc != newW.cc) || (oldW.reset != newW.reset);
        if (meaningChanged && oldW.reset != 255) {
          sendCC_immediate(oldW.cc, oldW.reset, lastCh);
        }
      }
    }

    if (mappingMode == NOT_MAPPING || mappingMode == MAP_ELBOW) {
      MotionSpec oldE = getElbowSpec(i, lastPreset);
      MotionSpec newE = getElbowSpec(i, currentPreset);
      if (oldE.enabled) {
        bool meaningChanged = (!newE.enabled) || (oldE.cc != newE.cc) || (oldE.reset != newE.reset);
        if (meaningChanged && oldE.reset != 255) {
          sendCC_immediate(oldE.cc, oldE.reset, lastCh);
        }
      }
    }
  }
}

// ----------- SELECTOR (pinky-held) helpers ---------------
inline bool selectorHeld() {
  return !leftHand ? touchState[LITTLE_A] : touchState[LITTLE_B];
}

// -------------------- CONTEXT HANDLER --------------------
void handleContext() {
  // Selector behavior
  if (selectorHeld()) {
    uint8_t selCC = getSelectorCc(currentPreset);
    if (!ccDisabled(selCC)) {
      const uint8_t order[6] = { INDEX_A, INDEX_B, MIDDLE_A, MIDDLE_B, RING_A, RING_B };
      const uint8_t values[6] = { 0, 25, 51, 76, 102, 127 };
      int lastIdx = -1;
      for (int i = 0; i < 6; ++i) {
        if (justTouched[order[i]]) lastIdx = i;
      }
      if (lastIdx >= 0) {
        sendCC_immediate(selCC, values[lastIdx], presetMidiChannel(currentPreset));
      }
    }
  }

  // ===== NOTE MODE short-circuit =====
  if (isNotePreset(currentPreset)) {
    uint8_t ch = presetMidiChannel(currentPreset);

    int rootBase = getRootFromTouchesLocal(!leftHand);
    uint8_t curV = constrain(
      stickyMap(accelRunningValue[X_AXIS], -255, 255, 127, 0,
                lastAccelRunningValue[X_AXIS], ACCEL_HYSTERESIS),
      0, 127);

    if (rootBase < 0) {
      if (lastMainNote >= 0 || last7thNote >= 0) noteModeAllNotesOffLocal(ch);
      lastRootBase = -100;
      lastElbowVal = curV;

      if (mappingMode == NOT_MAPPING || mappingMode == MAP_WRIST) {
        uint8_t ccw = presetGlobal[currentPreset][G_WRIST_CC];
        if (!ccDisabled(ccw)) {
          uint8_t wV = constrain(
            stickyMap(accelRunningValue[Z_AXIS], -200, 255, 127, 0,
                      lastAccelRunningValue[Z_AXIS], ACCEL_HYSTERESIS),
            0, 127);
          sendCC_throttled(ccw, wV, ch, 2, 10, 1);
        }
      }
      if (mappingMode == NOT_MAPPING || mappingMode == MAP_ELBOW) {
        uint8_t cce = presetGlobal[currentPreset][G_ELBOW_CC];
        if (!ccDisabled(cce)) {
          uint8_t eV = constrain(
            stickyMap(accelRunningValue[X_AXIS], -255, 255, 127, 0,
                      lastAccelRunningValue[X_AXIS], ACCEL_HYSTERESIS),
            0, 127);
          sendCC_throttled(cce, eV, ch, 2, 10, 1);
        }
      }
      return;
    }

    const int8_t* offsets = g_addSeventh ? kOffsetsWith7 : kOffsetsTriad;
    const int bucketCount = g_addSeventh ? 17 : 13;

    if (bucketCount != lastBucketCount) {
      noteModeAllNotesOffLocal(ch);
      noteQuantIdx = -1;
      lastBucketCount = bucketCount;
    }

    if (rootBase != lastRootBase) {
      noteModeAllNotesOffLocal(ch);
      lastRootBase = rootBase;
    }

    int idx = elbowToIndex(curV, bucketCount);
    if (idx != noteQuantIdx) {
      const bool isFirstNoteAfterActivation = (noteQuantIdx < 0);

      uint8_t vel = isFirstNoteAfterActivation
                      ? (g_rootTouchIdxPrimary >= 0
                           ? (uint8_t)getTouchVelocity((uint8_t)g_rootTouchIdxPrimary)
                           : NOTE_FIRST_VELOCITY)
                      : elbowVelocity(curV, lastElbowVal, bucketCount);

      if (lastMainNote >= 0) { midiNoteOffHelper(ch, (uint8_t)lastMainNote); lastMainNote = -1; }
      if (last7thNote >= 0)  { midiNoteOffHelper(ch, (uint8_t)last7thNote);  last7thNote  = -1; }

      int mainSemis = offsets[idx];
      int targetNote = rootBase + mainSemis;

      int mainNote = isFirstNoteAfterActivation
                       ? nearestOctaveRoot(targetNote, rootBase)
                       : targetNote;

      midiNoteOnHelper(ch, (uint8_t)mainNote, vel);
      lastMainNote = (int8_t)mainNote;

      noteQuantIdx = idx;
    }

    lastElbowVal = curV;

    if (mappingMode == NOT_MAPPING || mappingMode == MAP_WRIST) {
      uint8_t ccw = presetGlobal[currentPreset][G_WRIST_CC];
      if (!ccDisabled(ccw)) {
        uint8_t wV = constrain(
          stickyMap(accelRunningValue[Z_AXIS], -200, 255, 127, 0,
                    lastAccelRunningValue[Z_AXIS], ACCEL_HYSTERESIS),
          0, 127);
        sendCC_throttled(ccw, wV, ch, 2, 10, 1);
      }
    }
    if (mappingMode == NOT_MAPPING || mappingMode == MAP_ELBOW) {
      uint8_t cce = presetGlobal[currentPreset][G_ELBOW_CC];
      if (!ccDisabled(cce)) {
        uint8_t eV = constrain(
          stickyMap(accelRunningValue[X_AXIS], -255, 255, 127, 0,
                    lastAccelRunningValue[X_AXIS], ACCEL_HYSTERESIS),
          0, 127);
        sendCC_throttled(cce, eV, ch, 2, 10, 1);
      }
    }
    return;
  }

  // ===== CC MODE =====

  bool wantSuppressWrist = (mappingMode == NOT_MAPPING || mappingMode == MAP_WRIST)
                           ? anyFingerWristOverrideActive(currentPreset)
                           : false;

  bool wantSuppressElbow = (mappingMode == NOT_MAPPING || mappingMode == MAP_ELBOW)
                           ? anyFingerElbowOverrideActive(currentPreset)
                           : false;

  // ENTER wrist suppression: reset global unless same CC is actively used by a finger
  if (wantSuppressWrist && !g_suppressGlobalWrist) {
    uint8_t gcc  = presetGlobal[currentPreset][G_WRIST_CC];
    uint8_t grst = presetGlobal[currentPreset][G_WRIST_RESET];

    bool sameCcIsActiveOnFinger = anyActiveFingerAxisUsesCc(currentPreset, WRIST_CC, gcc);
    if (gcc != 255 && !ccDisabled(gcc) && grst != 255 && !sameCcIsActiveOnFinger) {
      sendCC_immediate(gcc, grst, MIDI_CHANNEL);
    }
    g_suppressGlobalWrist = true;
  } else if (!wantSuppressWrist && g_suppressGlobalWrist) {
    g_suppressGlobalWrist = false;
    uint8_t gcc = presetGlobal[currentPreset][G_WRIST_CC];
    if (gcc != 255 && !ccDisabled(gcc)) {
      sendCC_immediate(gcc, computeGlobalWristValue(), MIDI_CHANNEL);
    }
  }

  // ENTER elbow suppression: reset global unless same CC is actively used by a finger
  if (wantSuppressElbow && !g_suppressGlobalElbow) {
    uint8_t gcc  = presetGlobal[currentPreset][G_ELBOW_CC];
    uint8_t grst = presetGlobal[currentPreset][G_ELBOW_RESET];

    bool sameCcIsActiveOnFinger = anyActiveFingerAxisUsesCc(currentPreset, ELBOW_CC, gcc);
    if (gcc != 255 && !ccDisabled(gcc) && grst != 255 && !sameCcIsActiveOnFinger) {
      sendCC_immediate(gcc, grst, MIDI_CHANNEL);
    }
    g_suppressGlobalElbow = true;
  } else if (!wantSuppressElbow && g_suppressGlobalElbow) {
    g_suppressGlobalElbow = false;
    uint8_t gcc = presetGlobal[currentPreset][G_ELBOW_CC];
    if (gcc != 255 && !ccDisabled(gcc)) {
      sendCC_immediate(gcc, computeGlobalElbowValue(), MIDI_CHANNEL);
    }
  }

  for (uint8_t i = 0; i < NUMBER_OF_FINGERS; i++) {
    // TOUCH
    if (mappingMode == NOT_MAPPING || mappingMode == MAP_TOUCH) {
      uint8_t cc = preset[i][currentPreset][TOUCH_CC];
      if (!ccDisabled(cc)) {
        if (preset[i][currentPreset][TOUCH_CC_RANGE] == 255) {
          if (justTouched[i])   sendCC_immediate(cc, 127, MIDI_CHANNEL);
          if (justUntouched[i]) sendCC_immediate(cc, 0,   MIDI_CHANNEL);
        } else {
          if (touchState[i]) {
            uint8_t newV = runningTouchValue[i];
            sendCC_throttled(cc, newV, MIDI_CHANNEL, 2, 12, 1);
          }
          if (justUntouched[i]) sendCC_immediate(cc, 0, MIDI_CHANNEL);
        }
      }
    }

    // PER-FINGER MOTION (only if axis CC active AND touch held)
    if (touchState[i]) {
      if (mappingMode == NOT_MAPPING || mappingMode == MAP_WRIST) {
        uint8_t cc = preset[i][currentPreset][WRIST_CC];
        if (axisCcIsActive(cc)) {
          uint8_t newV = computeGlobalWristValue();
          sendCC_throttled(cc, newV, MIDI_CHANNEL, 2, 10, 1);
        }
      }
      if (mappingMode == NOT_MAPPING || mappingMode == MAP_ELBOW) {
        uint8_t cc = preset[i][currentPreset][ELBOW_CC];
        if (axisCcIsActive(cc)) {
          uint8_t newV = computeGlobalElbowValue();
          sendCC_throttled(cc, newV, MIDI_CHANNEL, 2, 10, 1);
        }
      }
    }

    // Per-finger resets on release
    if (justUntouched[i]) {
      if (mappingMode == NOT_MAPPING || mappingMode == MAP_WRIST) {
        uint8_t cc = preset[i][currentPreset][WRIST_CC];
        uint8_t reset = preset[i][currentPreset][WRIST_RESET];
        if (axisCcIsActive(cc) && reset != 255) {
          sendCC_immediate(cc, reset, MIDI_CHANNEL);
        }
      }
      if (mappingMode == NOT_MAPPING || mappingMode == MAP_ELBOW) {
        uint8_t cc = preset[i][currentPreset][ELBOW_CC];
        uint8_t reset = preset[i][currentPreset][ELBOW_RESET];
        if (axisCcIsActive(cc) && reset != 255) {
          sendCC_immediate(cc, reset, MIDI_CHANNEL);
        }
      }
    }
  }

  // PRESET-GLOBAL MOTION (only if not suppressed)
  if (!g_suppressGlobalWrist && (mappingMode == NOT_MAPPING || mappingMode == MAP_WRIST)) {
    uint8_t cc = presetGlobal[currentPreset][G_WRIST_CC];
    if (cc != 255 && !ccDisabled(cc)) {
      sendCC_throttled(cc, computeGlobalWristValue(), MIDI_CHANNEL, 2, 10, 1);
    }
  }
  if (!g_suppressGlobalElbow && (mappingMode == NOT_MAPPING || mappingMode == MAP_ELBOW)) {
    uint8_t cc = presetGlobal[currentPreset][G_ELBOW_CC];
    if (cc != 255 && !ccDisabled(cc)) {
      sendCC_throttled(cc, computeGlobalElbowValue(), MIDI_CHANNEL, 2, 10, 1);
    }
  }
}

// -------------------- THUMB / PRESET ---------------------
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

// ------------------------ SETUP/LOOP ---------------------
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

  if (bleConnected) {
    if (mappingMode == MAP_WRIST) neopixelWrite(NEO_PIXEL_PIN, 0, 10, 0);
    else if (mappingMode == MAP_ELBOW) neopixelWrite(NEO_PIXEL_PIN, 0, 10, 10);
    else if (mappingMode == MAP_TOUCH) neopixelWrite(NEO_PIXEL_PIN, 20, 10, 10);
    else neopixelWrite(NEO_PIXEL_PIN, 0, 0, 5);
  } else {
    neopixelWrite(NEO_PIXEL_PIN, 10, 0, 0);
  }
  delay(1);
}
