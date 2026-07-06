// exoPALM configurator -- SysEx protocol client (see vst-design/DESIGN.md)
// Pure logic, no I/O: builds request frames, parses replies, and models
// the 218-byte config blob with named addresses.
#pragma once
#include <cstdint>
#include <vector>
#include <optional>
#include <string>

namespace palm {

constexpr int kSchemaVersion = 1;
constexpr int kBlobSize = 218;
constexpr int kNumPresets = 4;
constexpr int kNumFingers = 8;

// ---- blob addresses (must match firmware config.ino) ----
constexpr int kAddrLeftHand = 0;
constexpr int kAddrCcChannel = 1;
inline int addrPreset(int p, int field) { return 2 + p * 6 + field; }  // field: 0=mode 1=selectorCC 2..5=global
inline int addrFinger(int f, int p, int field) { return 26 + (f * kNumPresets + p) * 6 + field; }
// finger fields: 0=TOUCH_CC 1=TOUCH_RANGE 2=WRIST_CC 3=WRIST_RESET 4=ELBOW_CC 5=ELBOW_RESET

struct DeviceInfo {
    int schemaVersion = 0;
    int fwVersion = 0;
    std::string name;
};

struct Config {
    uint8_t blob[kBlobSize] = {};
    uint8_t get(int addr) const { return addr >= 0 && addr < kBlobSize ? blob[addr] : 0; }
    void set(int addr, uint8_t v) { if (addr >= 0 && addr < kBlobSize) blob[addr] = v; }
};

// ---- frame builders (complete SysEx incl. F0/F7) ----
std::vector<uint8_t> buildInfoRequest();
std::vector<uint8_t> buildDumpRequest();
std::vector<uint8_t> buildSetParam(int addr, uint8_t value);
std::vector<uint8_t> buildSave();
std::vector<uint8_t> buildSetName(const std::string& name);
std::vector<uint8_t> buildRevert();
std::vector<uint8_t> buildEditMode(bool on);  // mute normal device output (RAM only)

// ---- note-mode chords (finger bytes repurposed per root, see firmware) ----
constexpr uint8_t kChordMaskMagic = 0x42;  // finger field 5 marks mask valid

// ---- reply parsing (input: complete SysEx frame incl. F0/F7) ----
bool isPalmFrame(const uint8_t* data, int len);
std::optional<DeviceInfo> parseInfo(const uint8_t* data, int len);
std::optional<Config> parseDump(const uint8_t* data, int len);

}  // namespace palm
