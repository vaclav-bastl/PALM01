#include "PalmProtocol.h"

namespace palm {

static std::vector<uint8_t> frame(std::initializer_list<uint8_t> body) {
    std::vector<uint8_t> f = { 0xF0, 0x7D, 'P', 'L', 'M' };
    f.insert(f.end(), body);
    f.push_back(0xF7);
    return f;
}

std::vector<uint8_t> buildInfoRequest() { return frame({ 0x01 }); }
std::vector<uint8_t> buildDumpRequest() { return frame({ 0x02 }); }

std::vector<uint8_t> buildSetParam(int addr, uint8_t value) {
    return frame({ 0x03,
                   (uint8_t)(addr & 0x7F), (uint8_t)((addr >> 7) & 0x7F),
                   (uint8_t)(value & 0x7F), (uint8_t)((value >> 7) & 0x01) });
}

std::vector<uint8_t> buildSave() { return frame({ 0x04 }); }

std::vector<uint8_t> buildSetName(const std::string& name) {
    std::vector<uint8_t> f = { 0xF0, 0x7D, 'P', 'L', 'M', 0x05 };
    for (size_t i = 0; i < name.size() && i < 16; i++)
        f.push_back((uint8_t)(name[i] & 0x7F));
    f.push_back(0xF7);
    return f;
}

std::vector<uint8_t> buildRevert() { return frame({ 0x06 }); }
std::vector<uint8_t> buildEditMode(bool on) { return frame({ 0x07, (uint8_t)(on ? 1 : 0) }); }

bool isPalmFrame(const uint8_t* d, int len) {
    return len >= 7 && d[0] == 0xF0 && d[1] == 0x7D
        && d[2] == 'P' && d[3] == 'L' && d[4] == 'M' && d[len - 1] == 0xF7;
}

std::optional<DeviceInfo> parseInfo(const uint8_t* d, int len) {
    if (!isPalmFrame(d, len) || d[5] != 0x41 || len < 9) return std::nullopt;
    DeviceInfo info;
    info.schemaVersion = d[6];
    info.fwVersion = d[7];
    for (int i = 8; i < len - 1; i++) info.name += (char)d[i];
    return info;
}

std::optional<Config> parseDump(const uint8_t* d, int len) {
    if (!isPalmFrame(d, len) || d[5] != 0x42) return std::nullopt;
    // payload: version + kBlobSize lo/hi pairs
    if (len - 8 < kBlobSize * 2) return std::nullopt;
    if (d[6] != kSchemaVersion) return std::nullopt;
    Config c;
    const uint8_t* p = d + 7;
    for (int i = 0; i < kBlobSize; i++)
        c.blob[i] = (uint8_t)(p[i * 2] | (p[i * 2 + 1] << 7));
    return c;
}

}  // namespace palm
