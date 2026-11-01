// Tests of src/pack.cpp: a zip read back, by its central directory as unzippers read it.
// Built as pack-test; scripts/build.ps1 test builds and runs it.
#include "pack.h"

#include <cstdio>

static int g_failed;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failed++;                                                 \
        }                                                               \
    } while (0)

static uint32_t U16(const std::string &s, size_t at) { return (unsigned char)s[at] | (unsigned char)s[at + 1] << 8; }
static uint32_t U32(const std::string &s, size_t at) { return U16(s, at) | U16(s, at + 2) << 16; }

int main() {
    CHECK(Zip_Crc32("123456789") == 0xCBF43926u); // the check value of CRC-32
    CHECK(Zip_Crc32("") == 0);

    std::string binary("\0\xff\r\n", 4);
    std::vector<PackFile> files = {{"stages/a.def", "[Info]\nname = a\n"}, {"stages/a.sff", binary}, {"empty.txt", ""}};
    std::string zip = Zip_Build(files);

    size_t end = zip.size() - 22;
    CHECK(U32(zip, end) == 0x06054b50);
    CHECK(U16(zip, end + 8) == 3 && U16(zip, end + 10) == 3);
    size_t at = U32(zip, end + 16);
    for (const PackFile &f : files) {
        CHECK(U32(zip, at) == 0x02014b50);
        CHECK(U16(zip, at + 10) == 0); // stored
        CHECK(U32(zip, at + 16) == Zip_Crc32(f.data));
        CHECK(U32(zip, at + 24) == f.data.size());
        size_t nameLen = U16(zip, at + 28);
        CHECK(zip.substr(at + 46, nameLen) == f.name);
        size_t local = U32(zip, at + 42);
        CHECK(U32(zip, local) == 0x04034b50);
        CHECK(U16(zip, local + 26) == nameLen);
        CHECK(zip.substr(local + 30 + nameLen, f.data.size()) == f.data);
        at += 46 + nameLen;
    }
    CHECK(at == end);
    CHECK(Zip_Build({}).size() == 22);

    printf(g_failed ? "pack-test: %d failed\n" : "pack-test: all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
