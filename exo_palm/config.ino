// =====================================================
// CONFIG PERSISTENCE (NVS)
//
// The preset tables in the main tab are the RUNTIME representation --
// their code values act as first-boot defaults. configLoad() overwrites
// them from NVS at boot; the SysEx configurator (sysex.ino) edits them
// live and calls configSave() to persist. The pack format doubles as
// the SysEx dump format (see vst-design/DESIGN.md for the address map).
// =====================================================

#include <Preferences.h>

#define CONFIG_SCHEMA_VERSION 1
#define CONFIG_BLOB_SIZE 218  // 2 global + 4 presets x 6 + 8 fingers x 4 presets x 6

static Preferences palmPrefs;

// Pack the runtime tables into the storage/wire blob
static void configPack(uint8_t* b) {
  b[0] = leftHand ? 1 : 0;
  b[1] = MIDI_CHANNEL;
  for (uint8_t p = 0; p < NUMBER_OF_PRESETS; p++) {
    uint8_t* q = b + 2 + p * 6;
    q[0] = presetMode[p];
    q[1] = presetSelectorCC[p];
    for (uint8_t i = 0; i < 4; i++) q[2 + i] = presetGlobal[p][i];
  }
  for (uint8_t f = 0; f < NUMBER_OF_FINGERS; f++)
    for (uint8_t p = 0; p < NUMBER_OF_PRESETS; p++) {
      uint8_t* q = b + 26 + (f * NUMBER_OF_PRESETS + p) * NUMBER_OF_BYTES_IN_PRESET;
      for (uint8_t i = 0; i < NUMBER_OF_BYTES_IN_PRESET; i++) q[i] = preset[f][p][i];
    }
}

// Unpack a blob into the runtime tables (takes effect immediately)
static void configUnpack(const uint8_t* b) {
  leftHand = (b[0] != 0);
  MIDI_CHANNEL = constrain(b[1], 1, 16);
  for (uint8_t p = 0; p < NUMBER_OF_PRESETS; p++) {
    const uint8_t* q = b + 2 + p * 6;
    presetMode[p] = q[0];
    presetSelectorCC[p] = q[1];
    for (uint8_t i = 0; i < 4; i++) presetGlobal[p][i] = q[2 + i];
  }
  for (uint8_t f = 0; f < NUMBER_OF_FINGERS; f++)
    for (uint8_t p = 0; p < NUMBER_OF_PRESETS; p++) {
      const uint8_t* q = b + 26 + (f * NUMBER_OF_PRESETS + p) * NUMBER_OF_BYTES_IN_PRESET;
      for (uint8_t i = 0; i < NUMBER_OF_BYTES_IN_PRESET; i++) preset[f][p][i] = q[i];
    }
}

// Apply one parameter by blob address (live edit from the configurator)
static bool configApply(uint16_t addr, uint8_t value) {
  if (addr >= CONFIG_BLOB_SIZE) return false;
  uint8_t blob[CONFIG_BLOB_SIZE];
  configPack(blob);
  blob[addr] = value;
  configUnpack(blob);
  return true;
}

void configSave() {
  uint8_t blob[CONFIG_BLOB_SIZE];
  configPack(blob);
  palmPrefs.putBytes("cfg", blob, CONFIG_BLOB_SIZE);
  palmPrefs.putUChar("ver", CONFIG_SCHEMA_VERSION);
  palmPrefs.putString("name", bleDeviceName);
  Serial.println("config: saved to NVS");
}

void configLoad() {
  palmPrefs.begin("palm", false);
  String n = palmPrefs.getString("name", "");
  if (n.length() > 0 && n.length() <= 16) {
    strncpy(bleDeviceName, n.c_str(), sizeof(bleDeviceName) - 1);
    bleDeviceName[sizeof(bleDeviceName) - 1] = 0;
  }
  if (palmPrefs.getUChar("ver", 0) == CONFIG_SCHEMA_VERSION
      && palmPrefs.getBytesLength("cfg") == CONFIG_BLOB_SIZE) {
    uint8_t blob[CONFIG_BLOB_SIZE];
    palmPrefs.getBytes("cfg", blob, CONFIG_BLOB_SIZE);
    configUnpack(blob);
    Serial.println("config: loaded from NVS");
  } else {
    configSave();
    Serial.println("config: seeded NVS with firmware defaults");
  }
}
