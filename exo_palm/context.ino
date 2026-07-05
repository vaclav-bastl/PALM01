// =====================================================
// CC MODE (context handler)
//
// The per-loop tick for CC presets: per-finger touch CCs (gate or
// pressure), per-finger wrist/elbow motion CCs while a finger is
// held, and preset-global motion CCs. When any touched finger maps
// a motion axis, the preset-global CC for that axis is suppressed
// (and reset) so the finger's mapping takes over cleanly.
// The pinky-held selector works in both CC and NOTE presets.
// NOTE presets short-circuit into noteMode.ino.
// =====================================================

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

// Avoid resetting the global CC when an active finger uses the same CC
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

// Reset the CC-mode suppression latches (on preset change)
static void resetGlobalSuppression() {
  g_suppressGlobalWrist = false;
  g_suppressGlobalElbow = false;
}

// ----------- SELECTOR (pinky-held) ---------------
inline bool selectorHeld() {
  return !leftHand ? touchState[LITTLE_A] : touchState[LITTLE_B];
}

// -------------------- CONTEXT HANDLER --------------------
void handleContext() {
  // Selector behavior (works in both CC and NOTE presets)
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

  // NOTE presets have their own engine
  if (isNotePreset(currentPreset)) {
    noteModeTick(presetMidiChannel(currentPreset));
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
