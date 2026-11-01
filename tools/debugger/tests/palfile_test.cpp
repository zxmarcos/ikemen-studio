// Tests of src/palfile.cpp: palettes through their files, the tools on their colours, PalFX's sums.
// Built as palfile-test; scripts/build.ps1 test builds and runs it.
#include "palfile.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

static int g_failed;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failed++;                                                 \
        }                                                               \
    } while (0)

static std::vector<unsigned char> Ramp() {
    std::vector<unsigned char> p(1024, 0);
    for (int i = 0; i < 256; i++) {
        p[i * 4] = (unsigned char)i, p[i * 4 + 1] = (unsigned char)(255 - i), p[i * 4 + 2] = (unsigned char)(i * 3);
        p[i * 4 + 3] = i == 0 ? 0 : 255;
    }
    return p;
}

static std::string Bytes(std::initializer_list<int> list) {
    std::string s;
    for (int v : list) s += (char)v;
    return s;
}

int main() {
    std::string dir = getenv("TEMP") ? getenv("TEMP") : ".", error;
    std::vector<unsigned char> ramp = Ramp(), back;
    int count = 0;

    // Written and read back: every format that writes
    for (const char *ext : {"act", "pal", "aco", "gpl"}) {
        std::string path = dir + "\\palfile_test." + ext;
        CHECK(Pal_Write(path, ramp, 256, &error));
        CHECK(Pal_Read(path, back, &count, &error));
        CHECK(count == 256 && back.size() == 1024);
        bool same = true;
        for (int i = 1; i < 256; i++) same = same && !memcmp(&back[i * 4], &ramp[i * 4], 3) && back[i * 4 + 3] == 255;
        CHECK(same);
        CHECK(back[3] == 0);
    }
    CHECK(!Pal_Write(dir + "\\palfile_test.bmp", ramp, 256, &error));

    // A RIFF palette of two colours
    {
        std::string riff = "RIFF";
        riff += Bytes({0x14, 0, 0, 0});
        riff += "PAL data";
        riff += Bytes({0x0C, 0, 0, 0, 0, 3, 2, 0, 10, 20, 30, 0, 40, 50, 60, 0});
        std::string path = dir + "\\palfile_riff.pal";
        std::ofstream(path, std::ios::binary) << riff;
        CHECK(Pal_Read(path, back, &count, &error));
        CHECK(count == 2 && back[0] == 10 && back[5] == 50 && back[6] == 60);
    }

    // An ASE file of one RGB colour, 1, 0.5, 0
    {
        std::string ase = "ASEF";
        ase += Bytes({0, 1, 0, 0, 0, 0, 0, 1}); // version, one block
        std::string entry = Bytes({0, 2, 0, 'a', 0, 0});
        entry += "RGB ";
        entry += Bytes({0x3F, 0x80, 0, 0, 0x3F, 0, 0, 0, 0, 0, 0, 0, 0, 0});
        ase += Bytes({0, 1, 0, 0, 0, (int)entry.size()});
        ase += entry;
        std::string path = dir + "\\palfile_test.ase";
        std::ofstream(path, std::ios::binary) << ase;
        CHECK(Pal_Read(path, back, &count, &error));
        CHECK(count == 1 && back[0] == 255 && back[1] == 128 && back[2] == 0);
    }

    // Not palettes
    {
        std::string path = dir + "\\palfile_bad.aco";
        std::ofstream(path, std::ios::binary) << "x";
        CHECK(!Pal_Read(path, back, &count, &error));
        CHECK(!Pal_Read(dir + "\\no_such_file.gpl", back, &count, &error));
    }

    // Moving a colour shifts those between; colour 0 stays out of it
    std::vector<unsigned char> p = Ramp();
    Pal_Move(p, 2, 5);
    CHECK(p[2 * 4] == 3 && p[4 * 4] == 5 && p[5 * 4] == 2 && p[6 * 4] == 6);
    Pal_Move(p, 5, 2);
    CHECK(p == Ramp());
    Pal_Move(p, 0, 5);
    CHECK(p == Ramp());

    // Selected colours only
    std::vector<char> sel(256, 0);
    sel[0] = sel[1] = sel[2] = sel[3] = 1;
    p = Ramp();
    Pal_Reverse(p, sel);
    CHECK(p[1 * 4] == 3 && p[2 * 4] == 2 && p[3 * 4] == 1 && p[0] == 0 && p[4 * 4] == 4);
    unsigned char red[3] = {255, 0, 0};
    p = Ramp();
    Pal_Fill(p, sel, red);
    CHECK(p[2 * 4] == 255 && p[2 * 4 + 1] == 0 && p[1] == 255 && p[4 * 4] == 4);
    p = Ramp();
    Pal_Effect(p, sel, PAL_INVERT);
    CHECK(p[1 * 4] == 254 && p[1 * 4 + 1] == 1 && p[0] == 0);
    p = Ramp();
    Pal_Effect(p, sel, PAL_SWAP_RB);
    CHECK(p[2 * 4] == 6 && p[2 * 4 + 2] == 2);
    p = Ramp();
    Pal_Effect(p, sel, PAL_ROTATE);
    CHECK(p[2 * 4] == 6 && p[2 * 4 + 1] == 2 && p[2 * 4 + 2] == 253);
    p = Ramp();
    Pal_Effect(p, sel, PAL_GREY);
    CHECK(p[2 * 4] == p[2 * 4 + 1] && p[2 * 4] == p[2 * 4 + 2]);

    // By tone: greys, then the hues in order; the colours are the same ones
    p.assign(1024, 0);
    const unsigned char cols[5][3] = {{0, 0, 200}, {120, 120, 120}, {0, 200, 0}, {200, 0, 0}, {30, 30, 30}};
    std::vector<char> five(256, 0);
    for (int i = 0; i < 5; i++) {
        memcpy(&p[(i + 1) * 4], cols[i], 3);
        five[i + 1] = 1;
    }
    Pal_SortByTone(p, five);
    CHECK(p[1 * 4] == 30 && p[2 * 4] == 120);
    CHECK(p[3 * 4] == 200 && p[4 * 4 + 1] == 200 && p[5 * 4 + 2] == 200);

    // PalFX: nothing does nothing; each sum alone
    PalFx fx;
    CHECK(fx.Plain());
    p = Ramp();
    Pal_ApplyFx(p, fx, 0);
    CHECK(p == Ramp());
    fx.add[0] = 10;
    CHECK(!fx.Plain());
    p = Ramp();
    Pal_ApplyFx(p, fx, 0);
    CHECK(p[20 * 4] == 30 && p[250 * 4] == 255 && p[0] == 0);
    fx = PalFx();
    fx.mul[1] = 128;
    p = Ramp();
    Pal_ApplyFx(p, fx, 0);
    CHECK(p[55 * 4 + 1] == 100);
    fx = PalFx();
    fx.invert = true;
    p = Ramp();
    Pal_ApplyFx(p, fx, 0);
    CHECK(p[20 * 4] == 235);
    fx = PalFx();
    fx.color = 0;
    p = Ramp();
    Pal_ApplyFx(p, fx, 0);
    CHECK(p[20 * 4] == p[20 * 4 + 1] && p[20 * 4] == p[20 * 4 + 2]);
    fx = PalFx();
    fx.sinadd[2] = 50, fx.period = 4;
    p = Ramp();
    Pal_ApplyFx(p, fx, 1); // the top of the sine
    CHECK(p[10 * 4 + 2] == 80);
    std::string text = Pal_FxText(fx, 30);
    CHECK(text.find("type = PalFX") != std::string::npos && text.find("sinadd = 0, 0, 50, 4") != std::string::npos);
    CHECK(text.find("time = 30") != std::string::npos);

    printf(g_failed ? "palfile-test: %d failed\n" : "palfile-test: all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
