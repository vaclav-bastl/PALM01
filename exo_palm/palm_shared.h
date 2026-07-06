#pragma once
#include <Arduino.h>

// ---------- Sizes ----------
#define NUMBER_OF_BYTES_IN_PRESET 6
#define NUMBER_OF_PRESETS 4
#define NUMBER_OF_FINGERS 8
#define NUMBER_OF_TOUCHPOINTS 11  // finger (8) + thumbs (3)

// ---------- Preset byte layout (per-finger) ----------
enum PresetField : uint8_t {
  TOUCH_CC = 0,       // 0–127, 255=disabled
  TOUCH_CC_RANGE,     // 0–127 max pressure; 255=gate (on/off)
  WRIST_CC,           // 0–127, 255=disabled
  WRIST_RESET,        // 0–127 on release; 255=hang
  ELBOW_CC,           // 0–127, 255=disabled
  ELBOW_RESET         // 0–127 on release; 255=hang
};

// ---------- Preset-level (global) wrist/elbow per preset ----------
enum PresetGlobalField : uint8_t {
  G_WRIST_CC = 0,     // 0–127, 255=disabled (NOT sent)
  G_WRIST_RESET,      // 0–127 on all-context release; 255=hang
  G_ELBOW_CC,         // 0–127, 255=disabled (NOT sent)
  G_ELBOW_RESET       // 0–127 on all-context release; 255=hang
};

// ---------- Handy checks ----------
inline bool ccDisabled(uint8_t cc) { return cc == 255; }
inline bool isGate(uint8_t rng)    { return rng == 255; }  // TOUCH gate flag

// ---------- Smart-switch types ----------
struct TouchSpec {
  uint8_t cc;
  bool    enabled;  // !ccDisabled(cc)
  bool    gate;     // isGate(range)
};
struct MotionSpec {
  uint8_t cc;
  uint8_t reset;
  bool    enabled;  // !ccDisabled(cc)
};

// ---------- Pair arbitration (A/B per finger) ----------
#define PAIR_HYST 8
static const uint8_t PAIR_COUNT = 4;
// Indices of A/B pairs in the "finger context" range [0..7]
static const uint8_t pairA[PAIR_COUNT] = { 0 /*LITTLE_A*/, 2 /*RING_A*/, 4 /*MIDDLE_A*/, 6 /*INDEX_A*/ };
static const uint8_t pairB[PAIR_COUNT] = { 1 /*LITTLE_B*/, 3 /*RING_B*/, 5 /*MIDDLE_B*/, 7 /*INDEX_B*/ };

// ---------- Note-mode chord masks (stored in the per-finger bytes) ----------
// In NOTE presets the CC fields are unused; the configurator repurposes
// them per root finger: [0]=pitch-class mask lo7, [1]=mask hi5,
// [5]=CHORD_MASK_MAGIC marks the mask valid (defaults keep the old tables).
#define CHORD_MASK_MAGIC 0x42

// ---------- Shared vars provided by main tab ----------
extern bool leftHand;
extern bool sysexEditMute;  // configurator "edit only output": normal MIDI muted

// Per-finger mapping table
extern uint8_t preset[NUMBER_OF_FINGERS][NUMBER_OF_PRESETS][NUMBER_OF_BYTES_IN_PRESET];
// NEW: Preset-level wrist/elbow mapping table
extern uint8_t presetGlobal[NUMBER_OF_PRESETS][4];

// Exposed getters (implemented in main tab)
TouchSpec  getTouchSpec(uint8_t finger, uint8_t p);
MotionSpec getWristSpec(uint8_t finger, uint8_t p);
MotionSpec getElbowSpec(uint8_t finger, uint8_t p);

// Context helpers (implemented in main tab)
bool anyContextTouchActive();
