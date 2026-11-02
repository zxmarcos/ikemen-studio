// Tests of src/sff.cpp on the SFF files of the test characters (run from the
// root of the checkout; files that are missing are skipped): every sprite
// decodes, a file written by Sff_Build reads back with the same pixels, and a
// sprite saved as PNG comes back as the same sprite.
#include "sffedit.h"

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

// A small document: a 10x10 sprite, colour 5 in the square (3,4)-(5,6), and one linked to it
static SffDoc SmallDoc() {
    SffDoc d;
    SffPal p;
    p.group = p.number = 1;
    p.rgba.assign(1024, 0);
    for (int i = 1; i < 256; i++) {
        p.rgba[i * 4] = (unsigned char)i;
        p.rgba[i * 4 + 3] = 255;
    }
    d.pals.push_back(p);
    std::vector<unsigned char> px(100, 0);
    for (int y = 4; y <= 6; y++)
        for (int x = 3; x <= 5; x++) px[y * 10 + x] = 5;
    SffSprite s;
    s.group = 1, s.number = 0, s.x = 5, s.y = 10, s.pal = 0;
    Sff_SetPixels(s, 10, 10, px, 1);
    d.sprites.push_back(s);
    s.number = 1, s.x = 6, s.y = 9;
    d.sprites.push_back(s); // the same data: linked
    return d;
}

static void BatchTests() {
    // Crop: the picture and the axis move together, linked sprites stay linked
    SffDoc d = SmallDoc();
    CHECK(Sff_Crop(d, {0, 1}) == 2);
    const SffSprite &c = d.sprites[0];
    CHECK(c.w == 3 && c.h == 3 && c.x == 2 && c.y == 6);
    CHECK(d.sprites[1].x == 3 && d.sprites[1].y == 5);
    CHECK(d.sprites[0].data == d.sprites[1].data);
    std::vector<unsigned char> px;
    int bpp = 0;
    CHECK(Sff_Decode(c, px, &bpp) && bpp == 1 && px == std::vector<unsigned char>(9, 5));
    CHECK(Sff_Crop(d, {0}) == 0); // nothing left to crop

    // Scale: the size and the axis; EPX and nearest keep the colours, bilinear stays in the palette
    for (int filter : {SFF_SCALE_NEAREST, SFF_SCALE_BILINEAR, SFF_SCALE_EPX}) {
        d = SmallDoc();
        CHECK(Sff_Scale(d, {0, 1}, 2, 1.5f, filter) == 2);
        const SffSprite &s = d.sprites[0];
        CHECK(s.w == 20 && s.h == 15 && s.x == 10 && s.y == 15);
        CHECK(d.sprites[0].data == d.sprites[1].data);
        CHECK(Sff_Decode(s, px, &bpp) && bpp == 1 && px.size() == 300);
        bool colours = true;
        for (unsigned char v : px) colours = colours && (v == 0 || v == 5);
        CHECK(colours);
    }
    d = SmallDoc();
    CHECK(Sff_Scale(d, {0}, 1, 1, SFF_SCALE_NEAREST) == 0);

    // Export: PNG files and their list; aligned, one canvas and one axis; back in with their numbers
    std::string dir = std::string(getenv("TEMP") ? getenv("TEMP") : ".");
    std::string error;
    d = SmallDoc();
    Sff_Crop(d, {1});
    CHECK(Sff_Export(d, {0, 1}, dir, true, &error));
    std::vector<SffListRow> rows;
    CHECK(Sff_ReadList(dir + "\\" + kSffListName, rows));
    CHECK(rows.size() == 2 && rows[1].file == "1_1.png" && rows[1].group == 1 && rows[1].number == 1);
    CHECK(rows[0].x == rows[1].x && rows[0].y == rows[1].y);
    SffDoc back;
    back.pals = d.pals;
    std::vector<SffSprite> made = Sff_SpritesOfPngs(back, {dir + "\\1_0.png", dir + "\\1_1.png"}, SFF_PAL_SHARED, [](const std::string &) {});
    CHECK(made.size() == 2 && made[0].w == made[1].w && made[0].h == made[1].h);

    // A file's name
    int g = 0, n = 0;
    CHECK(Sff_NumbersOfName("c:\\x\\200_3.png", &g, &n) && g == 200 && n == 3);
    CHECK(Sff_NumbersOfName("walk 20-11.pcx", &g, &n) && g == 20 && n == 11);
    CHECK(Sff_NumbersOfName("5,7.bmp", &g, &n) && g == 5 && n == 7);
    CHECK(!Sff_NumbersOfName("hero.png", &g, &n));
    CHECK(!Sff_NumbersOfName("hero3.png", &g, &n));

    // From another file: a number in use skipped or replaced; the palette kept, mapped or the same
    SffDoc from = SmallDoc(), to = SmallDoc();
    from.sprites[1].number = 2;
    from.pals[0].rgba[5 * 4 + 1] = 200; // colour 5 differs, so palette 1,1 is another one
    int skipped = 0;
    CHECK(Sff_ImportSprites(to, from, {0, 1}, false, SFF_IMPORT_OWN, &skipped) == 1 && skipped == 1);
    CHECK(to.sprites.size() == 3 && to.pals.size() == 2 && to.pals[1].group == 1 && to.pals[1].number == 1000);
    CHECK(to.sprites[2].pal == 1);
    to = SmallDoc();
    CHECK(Sff_ImportSprites(to, from, {0}, true, SFF_IMPORT_SAME, nullptr) == 1);
    CHECK(to.sprites.size() == 2 && to.pals.size() == 1 && to.sprites[0].pal == 0);
    to = SmallDoc();
    for (int i = 1; i < 256; i++) to.pals[0].rgba[i * 4] = 0;
    to.pals[0].rgba[9 * 4] = 5, to.pals[0].rgba[9 * 4 + 1] = 200; // the nearest of colour 5 is 9 here
    CHECK(Sff_ImportSprites(to, from, {1}, false, SFF_IMPORT_ADAPT, nullptr) == 1);
    CHECK(Sff_Decode(to.sprites[2], px, &bpp) && px[4 * 10 + 3] == 9);
    // Palettes: unused ones go, the character's stay; alike ones merge, sprites following
    d = SmallDoc();
    SffPal extra = d.pals[0];
    extra.group = 3, extra.number = 7;
    extra.rgba[5 * 4] = 77;
    d.pals.push_back(extra);
    extra.number = 8;
    d.pals.push_back(extra);
    d.sprites[1].pal = 2;
    CHECK(Sff_RemoveUnusedPals(d) == 1);
    CHECK(d.pals.size() == 2 && d.pals[1].number == 8 && d.sprites[1].pal == 1 && d.sprites[0].pal == 0);
    d.pals.push_back(d.pals[1]);
    d.pals[2].number = 9;
    d.sprites[0].pal = 2;
    CHECK(Sff_MergeDuplicatePals(d) == 1);
    CHECK(d.pals.size() == 2 && d.sprites[0].pal == 1 && d.sprites[1].pal == 1);
    CHECK(!Sff_DeletePal(d, 1)); // sprites use it
    CHECK(Sff_DeletePal(d, 0));
    CHECK(d.pals.size() == 1 && d.sprites[0].pal == 0);

    // Used, adapted, made: the picture keeps its indices, or maps to the nearest
    d = SmallDoc();
    extra = d.pals[0];
    extra.group = 2, extra.number = 0;
    for (int i = 1; i < 256; i++) extra.rgba[i * 4] = 0;
    extra.rgba[9 * 4] = 5; // the same red as colour 5 of the first
    d.pals.push_back(extra);
    CHECK(Sff_UsePal(d, {0}, 1) == 1 && d.sprites[0].pal == 1);
    CHECK(Sff_Decode(d.sprites[0], px, &bpp) && px[4 * 10 + 3] == 5);
    d = SmallDoc();
    d.pals.push_back(extra);
    CHECK(Sff_AdaptToPal(d, {0, 1}, 1) == 2);
    CHECK(d.sprites[0].pal == 1 && d.sprites[0].data == d.sprites[1].data);
    CHECK(Sff_Decode(d.sprites[0], px, &bpp) && px[4 * 10 + 3] == 9 && px[0] == 0);
    d = SmallDoc();
    std::vector<unsigned char> made2 = Sff_PalOfSprites(d, {0, 1});
    CHECK(made2.size() == 1024 && made2[4] == 5 && made2[7] == 255 && made2[3] == 0);
    printf("batch edits checked\n");
}

static void PictureTests() {
    // Two pictures: red and green on one, red and blue on the other
    SffPicture a, b;
    a.w = b.w = 2, a.h = b.h = 1;
    a.rgba = {255, 0, 0, 255, 0, 255, 0, 255};
    b.rgba = {255, 0, 0, 255, 0, 0, 255, 0}; // the blue one transparent
    auto quiet = [](const std::string &) {};
    SffDoc d;
    std::vector<SffSprite> made = Sff_SpritesOfPictures(d, {a, b}, SFF_PAL_ONE, quiet);
    CHECK(made.size() == 2);
    CHECK(d.pals.size() == 1 && d.pals[0].group == 0 && d.pals[0].number == 0);
    CHECK(made[0].pal == 0 && made[1].pal == 0);
    std::vector<unsigned char> px, rgba;
    int bpp = 0;
    CHECK(Sff_Decode(made[0], px, &bpp) && bpp == 1);
    Sff_ToRgba(d, made[0], px, bpp, rgba);
    CHECK(rgba == a.rgba);
    CHECK(Sff_Decode(made[1], px, &bpp));
    CHECK(px[1] == 0 && px[0] != 0);
    Sff_ToRgba(d, made[1], px, bpp, rgba);
    CHECK(rgba[0] == 255 && rgba[7] == 0);
    // Through the file: the palette stays one, the colours the same
    for (size_t i = 0; i < made.size(); i++) {
        made[i].group = 77, made[i].number = (int)i;
        d.sprites.push_back(made[i]);
    }
    std::string tmp = std::string(getenv("TEMP") ? getenv("TEMP") : ".") + "\\sff_test_pictures.sff";
    std::ofstream(tmp, std::ios::binary) << Sff_Build(d);
    SffDoc back;
    CHECK(Sff_Load(back, tmp));
    CHECK(back.sprites.size() == 2 && back.pals.size() == 1 && back.sprites[0].pal == back.sprites[1].pal);
    int found = Sff_FindSprite(back, 77, 0);
    CHECK(found >= 0 && Sff_Decode(back.sprites[found], px, &bpp));
    Sff_ToRgba(back, back.sprites[found], px, bpp, rgba);
    CHECK(rgba == a.rgba);
    remove(tmp.c_str());
    d.sprites.clear();
    // A second group has a palette of its own, the first stays
    made = Sff_SpritesOfPictures(d, {a}, SFF_PAL_ONE, quiet);
    CHECK(d.pals.size() == 2 && d.pals[1].number == 1 && made[0].pal == 1);
    // On palette 1,1: made from them when there is none
    SffDoc shared;
    made = Sff_SpritesOfPictures(shared, {a, b}, SFF_PAL_SHARED, quiet);
    CHECK(made.size() == 2 && shared.pals.size() == 1 && shared.pals[0].group == 1 && made[1].pal == 0);
    // Each with its own
    SffDoc own;
    made = Sff_SpritesOfPictures(own, {a, b}, SFF_PAL_OWN, quiet);
    CHECK(made.size() == 2 && own.pals.size() == 2 && made[0].pal == 0 && made[1].pal == 1);
    // Nothing: no palette
    SffDoc none;
    CHECK(Sff_SpritesOfPictures(none, {}, SFF_PAL_ONE, quiet).empty() && none.pals.empty());
}

int main() {
    RoundTrip("chars/kfm/kfm.sff", "chars/kfm/kfm.act");
    RoundTrip("chars/ryu_pots/ryusprite.sff", "");
    ActRoundTrip("chars/ryu_pots/ryupalettes/default.act");
    BatchTests();
    PictureTests();
    printf(g_failed ? "sff-test: %d failed\n" : "sff-test: all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
