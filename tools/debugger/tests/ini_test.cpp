// Tests of src/ini.cpp: parsing def text and editing it, then stages/kfm.def
// (run from the root of the checkout; skipped when missing).
// Built as ini-test; scripts/build.ps1 test builds and runs it.
#include "ini.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

static int g_failed;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failed++;                                                 \
        }                                                               \
    } while (0)

static const char *kDef =
    "; comment\n"
    "[Info]\n"
    "name = \"Test\" ; the name\n"
    "\n"
    "[BGdef]\n"
    "spr = stage0.sff\n"
    "\n"
    "[BG Sky]\n"
    "type  = normal\n"
    "start =  0, -10   ; top\n"
    "delta = .5, 1\n"
    "\n"
    "[BGCtrlDef x]\n"
    "looptime = 10\n"
    "[bg floor]\n"
    "type = parallax\n"
    "[BGCtrl move]\n"
    "type = posadd\n"
    "[BG]\n"
    "type = normal\n";

static std::string Line(const std::string &text, int n) {
    std::istringstream in(text);
    std::string line;
    for (int i = 0; i < n && std::getline(in, line); i++) {}
    return line;
}

static void TestParse() {
    IniFile f;
    Ini_Parse(kDef, f);
    CHECK(f.sections.size() == 7);
    if (f.sections.size() != 7) return;
    const IniSection &info = f.sections[0], &sky = f.sections[2];
    CHECK(info.name == "Info" && info.line == 2 && info.last == 3 && info.Field("name")->value == "\"Test\"");
    CHECK(sky.name == "BG Sky" && sky.lower == "bg sky" && sky.line == 8 && sky.last == 11);
    float v[2];
    CHECK(sky.Floats("start", v, 2) && v[0] == 0 && v[1] == -10);
    CHECK(sky.Floats("delta", v, 2) && v[0] == 0.5f && v[1] == 1);
    CHECK(!sky.Floats("velocity", v, 2) && v[0] == 0);
    std::vector<int> bgs = f.StageBgs();
    CHECK(bgs.size() == 3 && bgs[0] == 2 && bgs[1] == 4 && bgs[2] == 6); // not BGdef, not BGCtrlDef; [BG] is one
    std::vector<int> ctrls = f.StageBgCtrls();
    CHECK(ctrls.size() == 1 && ctrls[0] == 5);
}

static void TestEdits() {
    std::string text = kDef;
    IniFile f;
    Ini_Parse(text, f);
    float v[2] = {12, -3.5f};
    CHECK(Ini_SetField(text, f.sections[2], "start", Ini_Floats(v, 2)));
    CHECK(Line(text, 10) == "start =  12, -3.5   ; top");
    Ini_Parse(text, f);
    CHECK(Ini_SetField(text, f.sections[2], "velocity", "1, 0"));
    CHECK(Line(text, 12) == "velocity = 1, 0" && Line(text, 13) == "");
    Ini_Parse(text, f);
    CHECK(f.sections[2].last == 12);
    CHECK(Ini_SetField(text, f.sections[2], "velocity", ""));
    CHECK(text.find("velocity") == std::string::npos);
    // A section without fields gets its first one under the header
    Ini_Parse(text, f);
    CHECK(Ini_SetField(text, f.sections[4], "layerno", "1"));
    Ini_Parse(text, f);
    CHECK(f.sections[4].Field("layerno") && f.sections[4].Field("layerno")->line == 17);
    CHECK(f.sections[5].line == 18);
    // A section that moved is not edited
    IniSection stale = f.sections[2];
    stale.line = 3;
    CHECK(!Ini_SetField(text, stale, "start", "0, 0"));
}

static void TestKfm() {
    std::ifstream in("stages/kfm.def", std::ios::binary);
    if (!in) {
        printf("skipped (missing): stages/kfm.def\n");
        return;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    std::string text;
    for (char ch : ss.str())
        if (ch != '\r') text += ch;
    IniFile f;
    Ini_Parse(text, f);
    std::vector<int> bgs = f.StageBgs();
    printf("kfm.def: %zu sections, %zu BGs\n", f.sections.size(), bgs.size());
    CHECK(!bgs.empty());
    for (int i : bgs) CHECK(f.sections[i].Field("type") != nullptr);
}

int main() {
    TestParse();
    TestEdits();
    TestKfm();
    printf(g_failed ? "ini-test: %d failed\n" : "ini-test: all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
