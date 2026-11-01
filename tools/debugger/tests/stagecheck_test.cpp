// Tests of src/stagecheck.cpp: a def with one of each mistake, and the stages of the release (run
// from the game's folder), which the engine loads, so none may have an error. Built as
// stagecheck-test; scripts/build.ps1 test builds and runs it.
#include "stagecheck.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "sff.h"

static int g_failed;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failed++;                                                 \
        }                                                               \
    } while (0)

static bool Has(const std::vector<StageFinding> &f, StageFinding::Level level, int line, const char *part) {
    for (const auto &x : f)
        if (x.level == level && x.line == line && x.text.find(part) != std::string::npos) return true;
    return false;
}

static void TestMistakes() {
    std::string text = "[Info]\n"                       // 1
                       "name = \"Test\"\n"              // 2
                       "colour = 3\n"                   // 3 a key not read
                       "[Camera]\n"                     // 4
                       "boundleft = 100\n"              // 5 right of boundright
                       "boundright = -100\n"            // 6
                       "[PlayerInfo]\n"                 // 7
                       "p1startx = -400\n"              // 8 too far apart, out of the camera's reach
                       "p2startx = 70\n"                // 9
                       "[Camara]\n"                     // 10 no such section
                       "[BGdef]\n"                      // 11
                       "spr = test.sff\n"               // 12
                       "[BG sky]\n"                     // 13
                       "type = normal\n"                // 14
                       "spriteno = 0, 0\n"              // 15
                       "id = 5\n"                       // 16
                       "[BG missing]\n"                 // 17
                       "type = normal\n"                // 18
                       "spriteno = 3, 0\n"              // 19 not in the SFF
                       "[BG anim]\n"                    // 20
                       "type = anim\n"                  // 21
                       "actionno = 7\n"                 // 22 no such action
                       "[BG notype]\n"                  // 23 skipped
                       "spriteno = 0, 0\n"              // 24
                       "[BG anim2]\n"                   // 25
                       "type = anim\n"                  // 26
                       "actionno = 8\n"                 // 27
                       "[Begin Action 8]\n"             // 28
                       "1, 0, 0, 0, 5\n"                // 29
                       "1, 9, 0, 0, 5\n"                // 30 not in the SFF
                       "[BGCtrlDef sky]\n"              // 31
                       "ctrlid = 5, 6\n"                // 32 no BG has id 6
                       "[BGCtrl move]\n"                // 33
                       "type = slide\n"                 // 34 unknown type
                       "[BGCtrl a]\n"                   // 35
                       "type = anim\n"                  // 36
                       "value = 9\n"                    // 37 no such action
                       "[Music]\n"                      // 38
                       "bgmusic = gone.mp3\n"           // 39 not found
                       "round2.bgmusic = here.ogg\n";   // 40
    SffDoc sff;
    for (int g : {0, 1, 2}) {
        SffSprite s;
        s.group = g;
        s.number = 0;
        sff.sprites.push_back(s);
    }
    auto exists = [](const std::string &f) { return f == "test.sff" || f == "here.ogg"; };
    std::vector<StageFinding> f = Stage_Check(text, &sff, exists);
    for (const auto &x : f) printf("  %d %d %s\n", (int)x.level, x.line, x.text.c_str());
    CHECK(Has(f, StageFinding::Note, 3, "colour"));
    CHECK(Has(f, StageFinding::Error, 5, "boundleft is right of boundright"));
    CHECK(Has(f, StageFinding::Warning, 9, "apart"));
    CHECK(Has(f, StageFinding::Warning, 8, "P1 starts at -400"));
    CHECK(Has(f, StageFinding::Warning, 10, "Camara"));
    CHECK(Has(f, StageFinding::Error, 19, "sprite 3, 0"));
    CHECK(Has(f, StageFinding::Error, 22, "Begin Action 7"));
    CHECK(Has(f, StageFinding::Warning, 23, "no type"));
    CHECK(Has(f, StageFinding::Error, 30, "sprite 1, 9"));
    CHECK(!Has(f, StageFinding::Error, 29, "sprite"));
    CHECK(Has(f, StageFinding::Warning, 32, "id 6"));
    CHECK(!Has(f, StageFinding::Warning, 32, "id 5"));
    CHECK(Has(f, StageFinding::Warning, 34, "slide"));
    CHECK(Has(f, StageFinding::Error, 37, "Begin Action 9"));
    CHECK(Has(f, StageFinding::Warning, 39, "gone.mp3"));
    CHECK(!Has(f, StageFinding::Warning, 40, "here.ogg"));
    CHECK(Has(f, StageFinding::Note, 11, "2, 0")); // sprite 2,0: nothing uses it
    // Errors first
    CHECK(!f.empty() && f.front().level == StageFinding::Error);

    // No [BGdef]
    f = Stage_Check("[Info]\nname = x\n", nullptr, exists);
    CHECK(Has(f, StageFinding::Error, 0, "BGdef"));
}

static bool ReadText(const std::string &path, std::string &out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    out.erase(std::remove(out.begin(), out.end(), '\r'), out.end());
    return true;
}

// The release's stages load in the engine: no errors; what they give is printed
static void TestReleaseStages() {
    const char *defs[] = {"stages/stage0.def", "stages/kfm.def", "stages/stage1.def", "stages/stageZ.def",
                          "stages/stage3d.def", "stages/stage0-720.def"};
    int read = 0;
    for (const char *def : defs) {
        std::string text;
        if (!ReadText(def, text)) continue;
        read++;
        auto exists = [&](const std::string &name) {
            for (const std::string &dir : {std::string("stages/"), std::string(""), std::string("data/"),
                                            std::string("sound/")}) {
                std::ifstream in(dir + name, std::ios::binary);
                if (in) return true;
            }
            return false;
        };
        SffDoc sff;
        // The SFF the def names
        size_t at = text.find("\nspr");
        if (at != std::string::npos) {
            size_t eq = text.find('=', at), end = text.find('\n', at + 1);
            std::string name = text.substr(eq + 1, end - eq - 1);
            name = name.substr(0, name.find(';'));
            name.erase(0, name.find_first_not_of(" \t"));
            name.erase(name.find_last_not_of(" \t") + 1);
            Sff_Load(sff, "stages/" + name) || Sff_Load(sff, name);
        }
        std::vector<StageFinding> f = Stage_Check(text, sff.sprites.empty() ? nullptr : &sff, exists);
        int errors = 0;
        printf("%s (%zu sprites):\n", def, sff.sprites.size());
        for (const auto &x : f) {
            printf("  %s %d: %s\n", x.level == 0 ? "error" : x.level == 1 ? "warning" : "note", x.line, x.text.c_str());
            errors += x.level == StageFinding::Error;
        }
        CHECK(errors == 0);
    }
    if (!read) printf("(the release's stages were not found: run from the game's folder)\n");
}

static void TestScreenMistakes() {
    std::string text = "[Files]\n"                  // 1
                       "spr = gone.sff\n"           // 2 not found
                       "font1 = there.def, 0, 1\n"  // 3
                       "[Music]\n"                  // 4
                       "title.bgm = gone.mp3\n"     // 5
                       "title.bgm.loop = 1\n"       // 6
                       "[Round]\n"                  // 7
                       "a.anim = 5\n"               // 8
                       "b.anim = 6\n"               // 9 no such action
                       "c.anim = -1\n"              // 10 none
                       "d.spr = 1, 0\n"             // 11
                       "e.spr = 2, 0\n"             // 12 no such sprite
                       "f.face.spr = 9000, 1\n"     // 13 a character's
                       "g.snd = 1, 0\n"             // 14
                       "h.snd = 2, 0\n"             // 15 no such sound
                       "i.snd = F7, 0\n"            // 16 the common sounds
                       "[Begin Action 5]\n"         // 17
                       "1,0, 0,0, 3\n"              // 18
                       "3,0, 0,0, 3\n"              // 19 no such sprite
                       "-1,0, 0,0, 3\n"             // 20 blank
                       "[Begin Action 7]\n"         // 21 nothing names it
                       "1,0, 0,0, -1\n";
    SffDoc sff;
    SffSprite s;
    s.group = 1, s.number = 0;
    sff.sprites.push_back(s);
    auto sound = [](int g, int n) { return g == 1 && n == 0; };
    auto exists = [](const std::string &f) { return f == "there.def"; };
    std::vector<StageFinding> f = Screen_Check(text, &sff, sound, exists);
    for (const auto &x : f) printf("  %d %d %s\n", (int)x.level, x.line, x.text.c_str());
    CHECK(Has(f, StageFinding::Warning, 2, "gone.sff"));
    CHECK(Has(f, StageFinding::Warning, 5, "gone.mp3"));
    CHECK(Has(f, StageFinding::Warning, 9, "[Begin Action 6]"));
    CHECK(Has(f, StageFinding::Warning, 12, "sprite 2, 0"));
    CHECK(Has(f, StageFinding::Warning, 15, "sound 2, 0"));
    CHECK(Has(f, StageFinding::Warning, 19, "sprite 3, 0"));
    CHECK(Has(f, StageFinding::Note, 21, "7"));
    CHECK(f.size() == 7); // nothing else: not lines 3, 8, 10, 11, 13, 14, 16, 18, 20
}

// The release's screens: what they name is there, or a warning says so (read here, the engine loads them)
static void TestReleaseScreens() {
    const char *const defs[][3] = {{"data/fight.def", "data/fight.sff", "data/"}, {"data/ikemen1/system.def", "data/ikemen1/system.sff", "data/ikemen1/"}};
    for (const auto &d : defs) {
        std::string text;
        if (!ReadText(d[0], text)) continue;
        SffDoc sff;
        bool read = Sff_Load(sff, d[1]);
        std::string dir = d[2];
        auto exists = [&](const std::string &f) {
            for (const std::string &base : {dir, std::string("data/"), std::string(), std::string("font/"), std::string("sound/")}) {
                std::ifstream in(base + f);
                if (in) return true;
            }
            return false;
        };
        std::vector<StageFinding> found = Screen_Check(text, read ? &sff : nullptr, {}, exists);
        printf("%s (%zu sprites):\n", d[0], sff.sprites.size());
        for (const auto &x : found) printf("  %s %d: %s\n", x.level == StageFinding::Note ? "note" : "warning", x.line, x.text.c_str());
    }
}

int main() {
    TestMistakes();
    TestReleaseStages();
    TestScreenMistakes();
    TestReleaseScreens();
    if (g_failed) {
        printf("stagecheck-test: %d failed\n", g_failed);
        return 1;
    }
    printf("stagecheck-test: ok\n");
    return 0;
}
