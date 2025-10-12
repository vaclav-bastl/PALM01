#include "palm_shared.h"

#include <Adafruit_Sensor.h>
#include <Wire.h>
#include "Adafruit_MPR121.h"
#include <Adafruit_ADXL345_U.h>

// ======================== CONFIG ========================
const char* BLE_DEVICE_NAME = "PALM_01";
#define NEO_PIXEL_PIN D2

#define USE_BLE_MIDI
#define USE_SERIAL_MIDI
// #define USE_USB_MIDI
#define USE_DEBUG
const uint8_t MIDI_CHANNEL = 1;  // 1..16 for most Arduino MIDI libs

// ======================== ACCEL =========================
#define ACCEL_HYSTERESIS 3
#define X_AXIS 0
#define Y_AXIS 1
#define Z_AXIS 2
int accelValue[3]            = {0, 0, 0};
int accelRunningValue[3]     = {0, 0, 0};
int lastAccelRunningValue[3] = {0, 0, 0};

// ====================== GLOBAL STATE ====================
uint8_t rgb[3]        = {0, 0, 0};
uint8_t currentPreset = 0;
uint8_t lastPreset    = 0;
bool    bleConnected  = false;

// =================== TOUCH & BUTTONS ====================
bool    touchState[NUMBER_OF_TOUCHPOINTS]     = {0};
bool    lastTouchState[NUMBER_OF_TOUCHPOINTS] = {0};
bool    justTouched[NUMBER_OF_TOUCHPOINTS]    = {0};
bool    justUntouched[NUMBER_OF_TOUCHPOINTS]  = {0};
uint8_t runningTouchValue[NUMBER_OF_TOUCHPOINTS] = {0};

#define NUMBER_OF_BUTTONS 2
bool    buttonState[NUMBER_OF_BUTTONS]     = {0};
bool    lastButtonState[NUMBER_OF_BUTTONS] = {0};
bool    justPressed[NUMBER_OF_BUTTONS]     = {0};
bool    justReleased[NUMBER_OF_BUTTONS]    = {0};

long    longPressTime = 0;

// =================== CONTEXT INDICES ====================
bool leftHand = true;  // shared with other tabs

#define INDEX_A   6
#define INDEX_B   7
#define MIDDLE_A  4
#define MIDDLE_B  5
#define RING_A    2
#define RING_B    3
#define LITTLE_A  0
#define LITTLE_B  1

#define THUMB_L   8
#define THUMB_M   9
#define THUMB_R   10

#define BUTTON_R  0
#define BUTTON_L  1
#define WAKE_BUTTON_PIN 9

// ================== CC TRACKING / FILTERS ===============
#define CC_UNSET 0xFF
uint8_t  outputCCValue[128];   // last sent (0..127), CC_UNSET if never sent
uint32_t ccLastSentAt[128];    // millis() of last send per CC

inline bool ccValid(uint16_t cc) { return cc <= 127; }

void initCcTracking() {
  for (int i = 0; i < 128; ++i) {
    outputCCValue[i] = CC_UNSET;
    ccLastSentAt[i]  = 0;
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
  ccLastSentAt[cc]  = millis();
}

// Throttled send (continuous streams)
inline void sendCC_throttled(uint16_t cc, uint16_t value, uint8_t channel,
                             uint8_t minStep, uint16_t minIntervalMs, uint8_t quantStep) {
  if (ccDisabled(cc) || !ccValid(cc)) return;
  uint8_t raw = (value > 127) ? 127 : (uint8_t)value;
  uint8_t v   = quantize7(raw, quantStep);

  uint8_t  last = outputCCValue[cc];
  uint32_t now  = millis();
  uint32_t dt   = now - ccLastSentAt[cc];

  if (last == CC_UNSET) {
    sendControlChange((uint8_t)cc, v, channel);
    outputCCValue[cc] = v;
    ccLastSentAt[cc]  = now;
    return;
  }

  uint8_t diff = (last > v) ? (last - v) : (v - last);
  if (diff < minStep)       return;
  if (dt < minIntervalMs)   return;

  sendControlChange((uint8_t)cc, v, channel);
  outputCCValue[cc] = v;
  ccLastSentAt[cc]  = now;
}

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
    { /*TCH_CC,RNG*/  6,255,  /*WR_CC,RST*/ 38,255,  /*EL_CC,RST*/ 71,0 },
    { /*TCH_CC,RNG*/ 14,255,  /*WR_CC,RST*/ 46,127,  /*EL_CC,RST*/ 79,0 },
    { /*TCH_CC,RNG*/ 22,255,  /*WR_CC,RST*/ 54,127,  /*EL_CC,RST*/ 87,0 },
    { /*TCH_CC,RNG*/ 30,255,  /*WR_CC,RST*/ 62,127,  /*EL_CC,RST*/ 95,0 }
  },
  /* RING_B */ {
    { /*TCH_CC,RNG*/  5,255,  /*WR_CC,RST*/ 37,255,  /*EL_CC,RST*/ 70,0 },
    { /*TCH_CC,RNG*/ 13,255,  /*WR_CC_R   ST*/ 45,127,  /*EL_CC,RST*/ 78,0 },
    { /*TCH_CC,RNG*/ 21,255,  /*WR_CC,RST*/ 53,127,  /*EL_CC,RST*/ 86,0 },
    { /*TCH_CC,RNG*/ 29,255,  /*WR_CC,RST*/ 61,127,  /*EL_CC,RST*/ 94,0 }
  },
  /* MIDDLE_A */ {
    { /*TCH_CC,RNG*/  4,255,  /*WR_CC,RST*/ 36, 64,  /*EL_CC,RST*/ 69,0 },
    { /*TCH_CC,RNG*/ 12,127,  /*WR_CC,RST*/ 44,127,  /*EL_CC,RST*/ 77,0 },
    { /*TCH_CC,RNG*/ 20,255,  /*WR_CC,RST*/ 52,127,  /*EL_CC,RST*/ 85,0 },
    { /*TCH_CC,RNG*/ 28,255,  /*WR_CC,RST*/ 60,127,  /*EL_CC,RST*/ 93,0 }
  },
  /* MIDDLE_B */ {
    { /*TCH_CC,RNG*/  3,255,  /*WR_CC,RST*/ 35, 64,  /*EL_CC,RST*/ 68,0 },
    { /*TCH_CC,RNG*/ 11,255,  /*WR_CC,RST*/ 43,127,  /*EL_CC,RST*/ 76,0 },
    { /*TCH_CC,RNG*/ 19,255,  /*WR_CC,RST*/ 51,127,  /*EL_CC,RST*/ 84,0 },
    { /*TCH_CC,RNG*/ 27,255,  /*WR_CC,RST*/ 59,127,  /*EL_CC,RST*/ 92,0 }
  },
  /* INDEX_A */ {
    { /*TCH_CC,RNG*/  2,255,  /*WR_CC,RST*/ 34,127,  /*EL_CC,RST*/ 67,0 },
    { /*TCH_CC,RNG*/ 10,255,  /*WR_CC,RST*/ 42,127,  /*EL_CC,RST*/ 75,0 },
    { /*TCH_CC,RNG*/ 18,255,  /*WR_CC,RST*/ 50,127,  /*EL_CC,RST*/ 83,0 },
    { /*TCH_CC,RNG*/ 26,255,  /*WR_CC,RST*/ 58,127,  /*EL_CC,RST*/ 91,0 }
  },
  /* INDEX_B */ {
    { /*TCH_CC,RNG*/  1,255,  /*WR_CC,RST*/ 33,127,  /*EL_CC,RST*/ 66,0 },
    { /*TCH_CC,RNG*/  9,255,  /*WR_CC,RST*/ 41,127,  /*EL_CC,RST*/ 74,0 },
    { /*TCH_CC,RNG*/ 17,255,  /*WR_CC,RST*/ 49,127,  /*EL_CC,RST*/ 82,0 },
    { /*TCH_CC,RNG*/ 25,255,  /*WR_CC,RST*/ 57,127,  /*EL_CC,RST*/ 90,0 }
  }
};

// ---------- Preset-global CCs (matches palm_shared.h exactly) ----------
uint8_t presetGlobal[NUMBER_OF_PRESETS][4] = {
  /* preset 0 */ { /*G_WRIST_CC*/255, /*G_WRIST_RESET*/0,  /*G_ELBOW_CC*/255, /*G_ELBOW_RESET*/0 },
  /* preset 1 */ { /*G_WRIST_CC*/255, /*G_WRIST_RESET*/0,  /*G_ELBOW_CC*/255, /*G_ELBOW_RESET*/0 },
  /* preset 2 */ { /*G_WRIST_CC*/94, /*G_WRIST_RESET*/0,  /*G_ELBOW_CC*/255, /*G_ELBOW_RESET*/0 },
  /* preset 3 */ { /*G_WRIST_CC*/92,  /*G_WRIST_RESET*/0,  /*G_ELBOW_CC*/93,  /*G_ELBOW_RESET*/0 }
};

// ---------- NEW: NOTE/CC mode flag (kept separate to avoid shape changes) ----------
static uint8_t presetMode[NUMBER_OF_PRESETS] = { 0, 0, 1, 0 }; // preset 2 = NOTE mode
inline bool isNotePreset(uint8_t p) { return presetMode[p] == 1; }

// ============== SMART SWITCH GETTERS ====================
TouchSpec getTouchSpec(uint8_t finger, uint8_t p) {
  uint8_t cc  = preset[finger][p][TOUCH_CC];
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
void readSensors() { readTouchSensors(); readButtons(); readAccelerometer(); }

// =================== MAPPING MODE =======================
uint8_t mappingMode = 0;
#define NOT_MAPPING 0
#define MAP_WRIST   1
#define MAP_ELBOW   2
#define MAP_TOUCH   3

void handleButtons() {
  if (bleConnected && justPressed[BUTTON_L]) { mappingMode++; if (mappingMode > 3) mappingMode = 0; }
  if (justPressed[BUTTON_R]) { adverstiseBle(); longPressTime = millis(); }
  if (buttonState[BUTTON_R] && ((millis() - longPressTime)) > 3000) { sleep(); }
}

// ===================== CONTEXT HELPERS ==================
bool anyContextTouchActive() {
  for (uint8_t i = 0; i < NUMBER_OF_FINGERS; ++i) if (touchState[i]) return true;
  return false;
}

// ======== NOTE MODE HELPERS / STATE (isolated to this TU) ========
namespace {
  // 13 thresholds across 4 octaves + top root
  static const int8_t kMainOffsets[13] = {0,4,7,12,16,19,24,28,31,36,40,43,48};

  inline bool indexWantsSeventh(int idx) { return (idx == 0 || idx == 3 || idx == 6 || idx == 9); }

    // 13 thresholds => 13 buckets (0..12), top bucket reachable
  inline int elbowToIndex(uint8_t v) {
    int idx = (int)((uint16_t)v * 13 / 128);   // 0..12
    return (idx > 12) ? 12 : idx;
  }


 // Velocity from how much of one threshold width was traversed since last tick.
// Crossing a full bucket width -> 127; smaller fraction -> proportional 1..127.
inline uint8_t elbowVelocity(uint8_t nowV, uint8_t lastV) {
  // each bucket width in raw units
  const float bucketWidth = 128.0f / 13.0f;        // ≈ 9.846
  float delta = (float)((nowV > lastV) ? (nowV - lastV) : (lastV - nowV));
  float frac  = delta / bucketWidth;               // 0.0 .. ~N buckets
  if (frac >= 1.0f) return 127;                    // ≥ one full bucket = max
  int vel = (int)(1.0f + frac * 126.0f);           // map 0..1 -> 1..127
  if (vel < 1) vel = 1;
  if (vel > 127) vel = 127;
  return (uint8_t)vel;
}


  // Base notes (middle-ish octave for headroom)
  static const int MIDI_NOTE_C3 = 24;
  static const int MIDI_NOTE_D3 = 26;
  static const int MIDI_NOTE_E3 = 28;
  static const int MIDI_NOTE_F3 = 29;
  static const int MIDI_NOTE_G3 = 31;
  static const int MIDI_NOTE_A3 = 33;
  static const int MIDI_NOTE_B3 = 35;

  // Returns base MIDI root or -1 if none. Swaps A/B per finger when rightHand.
  int getRootFromTouchesLocal(bool leftHandLocal) {
    auto a_b = [&](uint8_t a, uint8_t b)->uint8_t { return leftHandLocal ? a : b; };

    uint8_t idxA = a_b(INDEX_A,  INDEX_B);
    uint8_t idxB = a_b(INDEX_B,  INDEX_A);
    uint8_t midA = a_b(MIDDLE_A, MIDDLE_B);
    uint8_t midB = a_b(MIDDLE_B, MIDDLE_A);
    uint8_t rngA = a_b(RING_A,   RING_B);
    uint8_t rngB = a_b(RING_B,   RING_A);
    uint8_t litA = a_b(LITTLE_A, LITTLE_B);
    uint8_t litB = a_b(LITTLE_B, LITTLE_A);
    (void)litA;

    int candidates[8];
    int n = 0;
    auto addIf = [&](uint8_t finger, int midi){ if (touchState[finger]) candidates[n++] = midi; };

    // Map per spec: B-side fingers = C/D/E/F; A-side fingers = G/A/B
    addIf(idxB, MIDI_NOTE_C3);
    addIf(midB, MIDI_NOTE_D3);
    addIf(rngB, MIDI_NOTE_E3);
    addIf(litB, MIDI_NOTE_F3);
    addIf(idxA, MIDI_NOTE_G3);
    addIf(midA, MIDI_NOTE_A3);
    addIf(rngA, MIDI_NOTE_B3);
    // LITTLE_A/B is seventh toggle, not a root

    if (n == 0) return -1;
    int minv = candidates[0];
    for (int i=1;i<n;i++) if (candidates[i] < minv) minv = candidates[i];
    return minv;
  }

  inline bool addSeventhNowLocal(bool leftHandLocal) {
    return leftHandLocal ? touchState[LITTLE_A] : touchState[LITTLE_B];
  }

  // Track currently sounding notes in note mode
  int8_t  noteQuantIdx = -1;
  int8_t  lastMainNote = -1;
  int8_t  last7thNote  = -1;
  uint8_t lastElbowVal = 0;
  int     lastRootBase = -100;

  inline void midiNoteOnHelper(uint8_t ch, uint8_t note, uint8_t vel) { sendNoteOn(note, vel, ch); }
  inline void midiNoteOffHelper(uint8_t ch, uint8_t note)             { sendNoteOff(note, 0, ch); }

  void noteModeAllNotesOffLocal() {
    if (lastMainNote >= 0) { midiNoteOffHelper(MIDI_CHANNEL, (uint8_t)lastMainNote); lastMainNote = -1; }
    if (last7thNote  >= 0) { midiNoteOffHelper(MIDI_CHANNEL, (uint8_t)last7thNote);  last7thNote  = -1; }
    noteQuantIdx = -1;
  }
} // namespace

// ================= SMART PRESET SWITCH ==================
// Sends NOTHING if no context touch is active.
// Sends per-finger meaning-aware resets ONLY if any finger is active,
// but ALWAYS resets preset-global CCs on preset change.
void switchPresetSmart() {
  // --- Handle NOTE preset transitions: kill any sounding notes ---
  if (isNotePreset(lastPreset) || isNotePreset(currentPreset)) {
    noteModeAllNotesOffLocal();
    lastRootBase = -100;
    // Optional extra safety:
    // sendControlChange(123, 0, MIDI_CHANNEL); // All Notes Off
  }

  // --- Always reset OLD preset's GLOBAL CCs on preset change (mode-agnostic) ---
  if (mappingMode == NOT_MAPPING || mappingMode == MAP_WRIST) {
    uint8_t occ  = presetGlobal[lastPreset][G_WRIST_CC];
    uint8_t oset = presetGlobal[lastPreset][G_WRIST_RESET];
    if (!ccDisabled(occ)) {
      sendCC_immediate(occ, oset, MIDI_CHANNEL);
    }
  }
  if (mappingMode == NOT_MAPPING || mappingMode == MAP_ELBOW) {
    uint8_t occ  = presetGlobal[lastPreset][G_ELBOW_CC];
    uint8_t oset = presetGlobal[lastPreset][G_ELBOW_RESET];
    if (!ccDisabled(occ)) {
      sendCC_immediate(occ, oset, MIDI_CHANNEL);
    }
  }

  // --- Per-finger meaning-aware resets only if there is touch context ---
  bool anyActive = anyContextTouchActive();
  if (!anyActive) return;

  for (uint8_t i = 0; i < NUMBER_OF_FINGERS; i++) {
    // TOUCH mapping resets
    if (mappingMode == NOT_MAPPING || mappingMode == MAP_TOUCH) {
      TouchSpec oldT = getTouchSpec(i, lastPreset);
      TouchSpec newT = getTouchSpec(i, currentPreset);
      if (oldT.enabled) {
        bool meaningChanged = (!newT.enabled) || (oldT.cc != newT.cc) || (oldT.gate != newT.gate);
        if (meaningChanged) sendCC_immediate(oldT.cc, 0, MIDI_CHANNEL);
      }
    }

    // WRIST mapping resets
    if (mappingMode == NOT_MAPPING || mappingMode == MAP_WRIST) {
      MotionSpec oldW = getWristSpec(i, lastPreset);
      MotionSpec newW = getWristSpec(i, currentPreset);
      if (oldW.enabled) {
        bool meaningChanged = (!newW.enabled) || (oldW.cc != newW.cc) || (oldW.reset != newW.reset);
        if (meaningChanged) sendCC_immediate(oldW.cc, oldW.reset, MIDI_CHANNEL);
      }
    }

    // ELBOW mapping resets
    if (mappingMode == NOT_MAPPING || mappingMode == MAP_ELBOW) {
      MotionSpec oldE = getElbowSpec(i, lastPreset);
      MotionSpec newE = getElbowSpec(i, currentPreset);
      if (oldE.enabled) {
        bool meaningChanged = (!newE.enabled) || (oldE.cc != newE.cc) || (oldE.reset != newE.reset);
        if (meaningChanged) sendCC_immediate(oldE.cc, oldE.reset, MIDI_CHANNEL);
      }
    }
  }
}


// -------------------- CONTEXT HANDLER --------------------
void handleContext() {
  // ===== NOTE MODE short-circuit =====
  if (isNotePreset(currentPreset)) {
    // root from touches; if none, silence and track elbow baseline
    int rootBase = getRootFromTouchesLocal(leftHand);
    uint8_t curV = constrain(
        stickyMap(accelRunningValue[X_AXIS], -255, 255, 127, 0,
                  lastAccelRunningValue[X_AXIS], ACCEL_HYSTERESIS), 0, 127);

    if (rootBase < 0) {
      if (lastMainNote >= 0 || last7thNote >= 0) noteModeAllNotesOffLocal();
      lastRootBase = -100;
      lastElbowVal = curV;

      // Even if no root is held, we can still stream preset-global CCs if enabled
      if (mappingMode == NOT_MAPPING || mappingMode == MAP_WRIST) {
        uint8_t ccw = presetGlobal[currentPreset][G_WRIST_CC];
        if (!ccDisabled(ccw)) {
          uint8_t wV = constrain(
              stickyMap(accelRunningValue[Z_AXIS], -200, 255, 127, 0,
                        lastAccelRunningValue[Z_AXIS], ACCEL_HYSTERESIS), 0, 127);
          sendCC_throttled(ccw, wV, MIDI_CHANNEL, 2, 10, 1);
        }
      }
      if (mappingMode == NOT_MAPPING || mappingMode == MAP_ELBOW) {
        uint8_t cce = presetGlobal[currentPreset][G_ELBOW_CC];
        if (!ccDisabled(cce)) {
          uint8_t eV = constrain(
              stickyMap(accelRunningValue[X_AXIS], -255, 255, 127, 0,
                        lastAccelRunningValue[X_AXIS], ACCEL_HYSTERESIS), 0, 127);
          sendCC_throttled(cce, eV, MIDI_CHANNEL, 2, 10, 1);
        }
      }

      return;
    }

    // if root changed, stop notes so we don't hang across roots
    if (rootBase != lastRootBase) {
      noteModeAllNotesOffLocal();
      lastRootBase = rootBase;
    }

    int idx = elbowToIndex(curV);
    if (idx != noteQuantIdx) {
      uint8_t vel = elbowVelocity(curV, lastElbowVal);

      // turn off previous tones
      if (lastMainNote >= 0) { midiNoteOffHelper(MIDI_CHANNEL, (uint8_t)lastMainNote); lastMainNote = -1; }
      if (last7thNote  >= 0) { midiNoteOffHelper(MIDI_CHANNEL, (uint8_t)last7thNote);  last7thNote  = -1; }

      // main chord line tone for this threshold
      int mainSemis = kMainOffsets[idx];
      int mainNote  = rootBase + mainSemis;
      midiNoteOnHelper(MIDI_CHANNEL, (uint8_t)mainNote, vel);
      lastMainNote = (int8_t)mainNote;

      // optional dominant 7th (+10) at idx 0,3,6,9 if LITTLE_A engaged
      if (addSeventhNowLocal(leftHand) && indexWantsSeventh(idx)) {
        int seventhNote = rootBase + 10;
        midiNoteOnHelper(MIDI_CHANNEL, (uint8_t)seventhNote, vel);
        last7thNote = (int8_t)seventhNote;
      }

      noteQuantIdx = idx;
    }

    lastElbowVal = curV;

    // --- Also stream PRESET-GLOBAL CCs while in NOTE mode (if enabled) ---
    if (mappingMode == NOT_MAPPING || mappingMode == MAP_WRIST) {
      uint8_t ccw = presetGlobal[currentPreset][G_WRIST_CC];
      if (!ccDisabled(ccw)) {
        uint8_t wV = constrain(
            stickyMap(accelRunningValue[Z_AXIS], -200, 255, 127, 0,
                      lastAccelRunningValue[Z_AXIS], ACCEL_HYSTERESIS), 0, 127);
        sendCC_throttled(ccw, wV, MIDI_CHANNEL, 2, 10, 1);
      }
    }
    if (mappingMode == NOT_MAPPING || mappingMode == MAP_ELBOW) {
      uint8_t cce = presetGlobal[currentPreset][G_ELBOW_CC];
      if (!ccDisabled(cce)) {
        uint8_t eV = constrain(
            stickyMap(accelRunningValue[X_AXIS], -255, 255, 127, 0,
                      lastAccelRunningValue[X_AXIS], ACCEL_HYSTERESIS), 0, 127);
        sendCC_throttled(cce, eV, MIDI_CHANNEL, 2, 10, 1);
      }
    }

    return; // skip CC path entirely
  }

  // ===== ORIGINAL CC PATH =====

  // detect if any finger context is active (for per-finger motion)
  uint8_t activeCount = 0;
  for (uint8_t i = 0; i < NUMBER_OF_FINGERS; ++i) if (touchState[i]) activeCount++;
  bool anyActive = (activeCount > 0);

  for (uint8_t i = 0; i < NUMBER_OF_FINGERS; i++) {
    // TOUCH: gate or pressure (unchanged)
    if (mappingMode == NOT_MAPPING || mappingMode == MAP_TOUCH) {
      uint8_t cc = preset[i][currentPreset][TOUCH_CC];
      if (!ccDisabled(cc)) {
        if (preset[i][currentPreset][TOUCH_CC_RANGE] == 255) {
          if (justTouched[i])   sendCC_immediate(cc, 127, MIDI_CHANNEL);
          if (justUntouched[i]) sendCC_immediate(cc,   0, MIDI_CHANNEL);
        } else {
          if (touchState[i]) {
            uint8_t newV = runningTouchValue[i];
            sendCC_throttled(cc, newV, MIDI_CHANNEL, 2, 12, 1);
          }
          if (justUntouched[i]) sendCC_immediate(cc, 0, MIDI_CHANNEL);
        }
      }
    }

    // MOTION (per-finger) only while this finger is active (unchanged)
    if (touchState[i]) {
      if (mappingMode == NOT_MAPPING || mappingMode == MAP_WRIST) {
        uint8_t cc = preset[i][currentPreset][WRIST_CC];
        if (!ccDisabled(cc)) {
          uint8_t newV = constrain(
              stickyMap(accelRunningValue[Z_AXIS], -200, 255, 127, 0,
                        lastAccelRunningValue[Z_AXIS], ACCEL_HYSTERESIS), 0, 127);
          sendCC_throttled(cc, newV, MIDI_CHANNEL, 2, 10, 1);
        }
      }
      if (mappingMode == NOT_MAPPING || mappingMode == MAP_ELBOW) {
        uint8_t cc = preset[i][currentPreset][ELBOW_CC];
        if (!ccDisabled(cc)) {
          uint8_t newV = constrain(
              stickyMap(accelRunningValue[X_AXIS], -255, 255, 127, 0,
                        lastAccelRunningValue[X_AXIS], ACCEL_HYSTERESIS), 0, 127);
          sendCC_throttled(cc, newV, MIDI_CHANNEL, 2, 10, 1);
        }
      }
    }

    // Per-finger resets on release (unchanged)
    if (justUntouched[i]) {
      if (mappingMode == NOT_MAPPING || mappingMode == MAP_WRIST) {
        uint8_t cc = preset[i][currentPreset][WRIST_CC];
        if (!ccDisabled(cc)) sendCC_immediate(cc, preset[i][currentPreset][WRIST_RESET], MIDI_CHANNEL);
      }
      if (mappingMode == NOT_MAPPING || mappingMode == MAP_ELBOW) {
        uint8_t cc = preset[i][currentPreset][ELBOW_CC];
        if (!ccDisabled(cc)) sendCC_immediate(cc, preset[i][currentPreset][ELBOW_RESET], MIDI_CHANNEL);
      }
    }
  }

  // -------- Preset-level (global) motion: ALWAYS while preset is active --------
  if (mappingMode == NOT_MAPPING || mappingMode == MAP_WRIST) {
    uint8_t cc = presetGlobal[currentPreset][G_WRIST_CC];
    if (!ccDisabled(cc)) {
      uint8_t newV = constrain(
          stickyMap(accelRunningValue[Z_AXIS], -200, 255, 127, 0,
                    lastAccelRunningValue[Z_AXIS], ACCEL_HYSTERESIS), 0, 127);
      sendCC_throttled(cc, newV, MIDI_CHANNEL, 2, 10, 1);
    }
  }
  if (mappingMode == NOT_MAPPING || mappingMode == MAP_ELBOW) {
    uint8_t cc = presetGlobal[currentPreset][G_ELBOW_CC];
    if (!ccDisabled(cc)) {
      uint8_t newV = constrain(
          stickyMap(accelRunningValue[X_AXIS], -255, 255, 127, 0,
                    lastAccelRunningValue[X_AXIS], ACCEL_HYSTERESIS), 0, 127);
      sendCC_throttled(cc, newV, MIDI_CHANNEL, 2, 10, 1);
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
  if (lastPreset != currentPreset) switchPresetSmart();  // will do nothing if no context touch active
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
  noteModeAllNotesOffLocal(); // safety (uses MIDI, so call after initMidi)
}

void loop() {
  readSensors();
  handleButtons();
  handleThumb();
  handleContext();

  midiRead();
  midiDispatch();   // once per loop, no delays

  if (bleConnected) {
    if      (mappingMode == MAP_WRIST) neopixelWrite(NEO_PIXEL_PIN, 0, 10, 0);
    else if (mappingMode == MAP_ELBOW) neopixelWrite(NEO_PIXEL_PIN, 0, 10, 10);
    else if (mappingMode == MAP_TOUCH) neopixelWrite(NEO_PIXEL_PIN, 20, 10, 10);
    else                               neopixelWrite(NEO_PIXEL_PIN, 0, 0, 5);
  } else {
    neopixelWrite(NEO_PIXEL_PIN, 10, 0, 0);
  }
  delay(1);
}
