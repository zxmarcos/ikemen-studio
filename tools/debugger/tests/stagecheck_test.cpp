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

int main() {
    TestMistakes();
    TestReleaseStages();
    if (g_failed) {
        printf("stagecheck-test: %d failed\n", g_failed);
        return 1;
    }
    printf("stagecheck-test: ok\n");
    return 0;
}
