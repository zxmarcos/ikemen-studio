#include "pack.h"

namespace {

const uint32_t kLocal = 0x04034b50, kCentral = 0x02014b50, kEnd = 0x06054b50;
const uint16_t kVersion = 20, kUtf8Names = 0x0800, kStored = 0;
const uint16_t kDate = 0x0021; // 1980-01-01: the same files make the same zip

void Put16(std::string &out, uint16_t v) {
    out += (char)(v & 255);
    out += (char)(v >> 8);
}

void Put32(std::string &out, uint32_t v) {
    Put16(out, (uint16_t)(v & 0xFFFF));
    Put16(out, (uint16_t)(v >> 16));
}

// What a local header and a central one share
void Common(std::string &out, uint32_t crc, uint32_t size, uint16_t nameLen) {
    Put16(out, kVersion);
    Put16(out, kUtf8Names);
    Put16(out, kStored);
    Put16(out, 0); // time
    Put16(out, kDate);
    Put32(out, crc);
    Put32(out, size); // compressed
    Put32(out, size);
    Put16(out, nameLen);
    Put16(out, 0); // extra
}

} // namespace

uint32_t Zip_Crc32(const std::string &data) {
    static uint32_t table[256];
    if (!table[1])
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b : data) c = table[(c ^ b) & 255] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

std::string Zip_Build(const std::vector<PackFile> &files) {
    std::string out, central;
    for (const PackFile &f : files) {
        uint32_t offset = (uint32_t)out.size(), crc = Zip_Crc32(f.data), size = (uint32_t)f.data.size();
        uint16_t nameLen = (uint16_t)f.name.size();
        Put32(out, kLocal);
        Common(out, crc, size, nameLen);
        out += f.name;
        out += f.data;
        Put32(central, kCentral);
        Put16(central, kVersion); // made by
        Common(central, crc, size, nameLen);
        Put16(central, 0); // comment
        Put16(central, 0); // disk
        Put16(central, 0); // internal attributes
        Put32(central, 0); // external attributes
        Put32(central, offset);
        central += f.name;
    }
    uint32_t start = (uint32_t)out.size(), length = (uint32_t)central.size();
    out += central;
    Put32(out, kEnd);
    Put16(out, 0);
    Put16(out, 0);
    Put16(out, (uint16_t)files.size());
    Put16(out, (uint16_t)files.size());
    Put32(out, length);
    Put32(out, start);
    Put16(out, 0); // comment
    return out;
}
