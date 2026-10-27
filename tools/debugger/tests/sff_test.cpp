// Tests of src/sff.cpp on the SFF files of the test characters (run from the
// root of the checkout; files that are missing are skipped): every sprite
// decodes, a file written by Sff_Build reads back with the same pixels, and a
// sprite saved as PNG comes back as the same sprite.
#include "sff.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

static int g_failed;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failed++;                                                 \
        }                                                               \
    } while (0)

static bool Exists(const std::string &path) { return std::ifstream(path).good(); }

static void RoundTrip(const std::string &path, const std::string &pal1) {
    if (!Exists(path)) {
        printf("skipped (missing): %s\n", path.c_str());
        return;
    }
    SffDoc doc;
    CHECK(Sff_Load(doc, path, pal1));
    if (!doc.error.empty()) printf("%s\n", doc.error.c_str());
    int decoded = 0;
    std::vector<std::vector<unsigned char>> pixels(doc.sprites.size());
    for (size_t i = 0; i < doc.sprites.size(); i++) {
        int bpp = 0;
        if (Sff_Decode(doc.sprites[i], pixels[i], &bpp)) decoded++;
    }
    printf("%s: v%s, %zu sprites, %zu palettes, %d decoded\n", path.c_str(), doc.converted ? "1" : "2",
           doc.sprites.size(), doc.pals.size(), decoded);
    CHECK(decoded == (int)doc.sprites.size());

    // Written as v2, read back: the same sprites and pixels
    std::string tmp = std::string(getenv("TEMP") ? getenv("TEMP") : ".") + "\\sff_test.sff";
    std::ofstream(tmp, std::ios::binary) << Sff_Build(doc);
    SffDoc back;
    CHECK(Sff_Load(back, tmp));
    CHECK(back.sprites.size() == doc.sprites.size());
    int same = 0;
    for (size_t i = 0; i < back.sprites.size() && i < doc.sprites.size(); i++) {
        const SffSprite &a = doc.sprites[i], &b = back.sprites[i];
        std::vector<unsigned char> px;
        int bpp = 0;
        if (a.group == b.group && a.number == b.number && a.x == b.x && a.y == b.y && Sff_Decode(b, px, &bpp) &&
            px == pixels[i])
            same++;
    }
    CHECK(same == (int)doc.sprites.size());

    // A sprite as PNG and back, with its own palette: the same indices
    for (size_t i = 0; i < doc.sprites.size(); i++) {
        const SffSprite &s = doc.sprites[i];
        if (s.depth != 8 || s.pal < 0 || s.w < 8 || s.h < 8) continue;
        std::string png = std::string(getenv("TEMP") ? getenv("TEMP") : ".") + "\\sff_test.png", error;
        CHECK(Sff_SavePng(doc, s, png, &error));
        SffDoc d2;
        std::vector<SffSprite> made = Sff_SpritesOfPngs(d2, {png}, SFF_PAL_OWN, [](const std::string &m) {
            printf("  %s\n", m.c_str());
        });
        CHECK(made.size() == 1);
        if (made.size() == 1) {
            std::vector<unsigned char> px;
            int bpp = 0;
            CHECK(Sff_Decode(made[0], px, &bpp) && bpp == 1);
            CHECK(px == pixels[i]);
            CHECK(d2.pals.size() == 1 && d2.pals[0].rgba.size() == 1024);
            bool colours = true;
            for (int c = 1; c < 256 && d2.pals.size() == 1; c++)
                for (int k = 0; k < 3; k++) colours = colours && d2.pals[0].rgba[c * 4 + k] == doc.pals[s.pal].rgba[c * 4 + k];
            CHECK(colours);
        }
        break;
    }
}

// An ACT palette read, written and read again: the same colours, colour 0 transparent.
static void ActRoundTrip(const std::string &path) {
    if (!Exists(path)) {
        printf("skipped (missing): %s\n", path.c_str());
        return;
    }
    std::vector<unsigned char> rgba, back;
    CHECK(Sff_ReadAct(path, rgba));
    CHECK(rgba.size() == 1024 && rgba[3] == 0 && rgba[7] == 255);
    std::string tmp = std::string(getenv("TEMP") ? getenv("TEMP") : ".") + "\\sff_test.act";
    std::string bytes = Sff_ActBytes(rgba);
    CHECK(bytes.size() == 768);
    std::ofstream(tmp, std::ios::binary) << bytes;
    CHECK(Sff_ReadAct(tmp, back));
    CHECK(back == rgba);
    std::string file;
    {
        std::ifstream in(path, std::ios::binary);
        file.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    CHECK(file.substr(0, 768) == bytes); // written as it was read
    printf("%s: ACT palette read and written\n", path.c_str());
}

int main() {
    RoundTrip("chars/kfm/kfm.sff", "chars/kfm/kfm.act");
    RoundTrip("chars/ryu_pots/ryusprite.sff", "");
    ActRoundTrip("chars/ryu_pots/ryupalettes/default.act");
    printf(g_failed ? "sff-test: %d failed\n" : "sff-test: all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
