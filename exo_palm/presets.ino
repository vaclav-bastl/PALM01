// =====================================================
// PRESET SELECTION (thumbs) + SMART SWITCH
//
// Thumb pads select presets 1-3 (none held = preset 0); the layout
// mirrors for the other hand. On a switch, switchPresetSmart()
// cleans up only what changes meaning: notes are killed across
// NOTE-mode transitions, and CCs whose mapping (cc/gate/reset)
// differs in the new preset are reset to their configured values.
// =====================================================

void switchPresetSmart() {
  uint8_t lastCh = presetMidiChannel(lastPreset);
  uint8_t newCh = presetMidiChannel(currentPreset);

  resetGlobalSuppression();

  // --- Handle NOTE preset transitions: kill any sounding notes ---
  if (isNotePreset(lastPreset) || isNotePreset(currentPreset)) {
    noteModeAllNotesOffLocal(lastCh);

    if (isNotePreset(lastPreset) && !isNotePreset(currentPreset)) {
      sendCC_immediate(123, 0, lastCh);  // All Notes Off
    }

    noteModeResetRoot();
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
