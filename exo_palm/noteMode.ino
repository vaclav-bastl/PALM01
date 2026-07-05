// =====================================================
// NOTE MODE
//
// Touch combinations pick a root note (C..B; two adjacent scale
// fingers add a dominant 7th), and the elbow angle sweeps an
// arpeggio of chord tones above that root. First note velocity
// comes from touch pressure; subsequent notes from elbow speed.
// Preset-global wrist/elbow CCs stay active alongside.
// =====================================================

static const int8_t kOffsetsTriad[13] = { 0, 4, 7, 12, 16, 19, 24, 28, 31, 36, 40, 43, 48 };
static const int8_t kOffsetsWith7[17] = { 0, 4, 7, 10, 12, 16, 19, 22, 24, 28, 31, 34, 36, 40, 43, 46, 48 };

static const int MIDI_NOTE_C3 = 24;
static const int MIDI_NOTE_D3 = 26;
static const int MIDI_NOTE_E3 = 28;
static const int MIDI_NOTE_F3 = 29;
static const int MIDI_NOTE_G3 = 31;
static const int MIDI_NOTE_A3 = 33;
static const int MIDI_NOTE_B3 = 35;

static bool g_addSeventh = false;

// Which touch index produced the current root (and optional partner in 7th case)
static int8_t g_rootTouchIdxPrimary = -1;
static int8_t g_rootTouchIdxSecondary = -1;

static int8_t noteQuantIdx = -1;
static int8_t lastMainNote = -1;
static int8_t last7thNote = -1;
static uint8_t lastElbowVal = 0;
static int lastRootBase = -100;
static int lastBucketCount = 13;

static inline int elbowToIndex(uint8_t v, int bucketCount) {
  int idx = (int)((uint16_t)v * bucketCount / 128);
  int maxIdx = bucketCount - 1;
  return (idx > maxIdx) ? maxIdx : idx;
}

static inline uint8_t elbowVelocity(uint8_t nowV, uint8_t lastV, int bucketCount) {
  const float bucketWidth = 128.0f / (float)bucketCount;
  float delta = (float)((nowV > lastV) ? (nowV - lastV) : (lastV - nowV));
  float frac = delta / bucketWidth;
  if (frac >= 1.0f) return 127;
  int vel = (int)(1.0f + frac * 126.0f);
  if (vel < 1) vel = 1;
  if (vel > 127) vel = 127;
  return (uint8_t)vel;
}

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

static int getRootFromTouchesLocal(bool leftHandLocal) {
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

static inline int nearestOctaveRoot(int target, int rootBase) {
  int diff = target - rootBase;
  int k = (diff >= 0) ? ((diff + 6) / 12) : ((diff - 6) / 12);
  return rootBase + 12 * k;
}

void noteModeAllNotesOffLocal(uint8_t ch) {
  if (lastMainNote >= 0) { sendNoteOff((uint8_t)lastMainNote, 0, ch); lastMainNote = -1; }
  if (last7thNote >= 0) { sendNoteOff((uint8_t)last7thNote, 0, ch); last7thNote = -1; }
  noteQuantIdx = -1;
}

// Called by switchPresetSmart on NOTE-preset transitions
void noteModeResetRoot() {
  lastRootBase = -100;
}

// The per-loop NOTE mode tick (called from handleContext)
void noteModeTick(uint8_t ch) {
  int rootBase = getRootFromTouchesLocal(!leftHand);
  uint8_t curV = constrain(
    stickyMap(accelRunningValue[X_AXIS], -255, 255, 127, 0,
              lastAccelRunningValue[X_AXIS], ACCEL_HYSTERESIS),
    0, 127);

  if (rootBase < 0) {
    if (lastMainNote >= 0 || last7thNote >= 0) noteModeAllNotesOffLocal(ch);
    lastRootBase = -100;
    lastElbowVal = curV;

    noteModeGlobalCCs(ch);
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

    if (lastMainNote >= 0) { sendNoteOff((uint8_t)lastMainNote, 0, ch); lastMainNote = -1; }
    if (last7thNote >= 0)  { sendNoteOff((uint8_t)last7thNote, 0, ch);  last7thNote  = -1; }

    int mainSemis = offsets[idx];
    int targetNote = rootBase + mainSemis;

    int mainNote = isFirstNoteAfterActivation
                     ? nearestOctaveRoot(targetNote, rootBase)
                     : targetNote;

    sendNoteOn((uint8_t)mainNote, vel, ch);
    lastMainNote = (int8_t)mainNote;

    noteQuantIdx = idx;
  }

  lastElbowVal = curV;

  noteModeGlobalCCs(ch);
}

// Preset-global wrist/elbow CCs, shared by the active and idle branches
static void noteModeGlobalCCs(uint8_t ch) {
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
}
