// Tests of src/roster.cpp: parsing select.def text and editing it, then data/select.def
// (run from the root of the checkout; skipped when missing).
// Built as roster-test; scripts/build.ps1 test builds and runs it.
#include "roster.h"

#include <algorithm>
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
    "[Characters]\n"                                                   // 1
    " ;kfm, stages/mybg.def\n"                                         // 2
    "kfm, stages/kfm.def, order=2 ;the first\n"                        // 3
    "\n"                                                               // 4
    "kfm720, stages/a.def, stages/b.DEF, music = x.mp3, unlock = a, b\n" // 5
    "slot = {\n"                                                       // 6
    "  kfm_zss, next=w\n"                                              // 7
    "  kfm_zaxis | suave.zip\n"                                        // 8
    "}\n"                                                              // 9
    "boss, exclude = 1\n"                                              // 10
    "randomselect\n"                                                   // 11
    "\n"                                                               // 12
    "[ExtraStages]\n"                                                  // 13
    "stages/stage0.def, order = 2\n"                                   // 14
    "slot = {stages/a.def | stages/b.def}\n"                           // 15
    "[Options]\n"                                                      // 16
    "arcade.maxmatches = 6,1\n";                                       // 17

static std::string Line(const std::string &text, int n) {
    std::istringstream in(text);
    std::string line;
    for (int i = 0; i < n && std::getline(in, line); i++) {}
    return line;
}

static void TestEntry() {
    RosterEntry e = Roster_ParseEntry("kfm720, stages/a.def, random, music = x.mp3, unlock = a, b");
    CHECK(e.name == "kfm720" && e.items.size() == 3 && e.unlock == "a, b");
    CHECK(e.Param("music") == "x.mp3" && e.Param("order") == "");
    CHECK(e.Stages().size() == 2 && e.Stages()[1] == "random");
    e.SetParam("order", "3");
    e.SetParam("music", "");
    CHECK(e.Text() == "kfm720, stages/a.def, random, order = 3, unlock = a, b");
    RosterEntry f = Roster_ParseEntry("kfm, order=0");
    f.SetParam("order", "1");
    CHECK(f.Text() == "kfm, order=1");
    CHECK(Roster_ParseEntry("randomselect").items.empty());
}

static void TestParse() {
    RosterFile r;
    Roster_Parse(kDef, r);
    CHECK(r.charsLine == 1 && r.charsEnd == 12 && r.stagesLine == 13 && r.stagesEnd == 16);
    CHECK(r.chars.size() == 5);
    if (r.chars.size() != 5) return;
    CHECK(r.chars[0].first == 3 && r.chars[0].entries[0].name == "kfm" && r.chars[0].entries[0].Param("order") == "2");
    CHECK(r.chars[1].entries[0].Stages().size() == 2 && r.chars[1].entries[0].unlock == "a, b");
    const RosterCell &slot = r.chars[2];
    CHECK(slot.slot && slot.first == 6 && slot.last == 9 && slot.entries.size() == 3);
    CHECK(slot.entries.size() == 3 && slot.entries[2].name == "suave.zip");
    CHECK(slot.Text() == "slot = {kfm_zss, next=w | kfm_zaxis | suave.zip}");
    CHECK(r.chars[3].exclude && !r.chars[4].exclude && r.chars[4].entries[0].name == "randomselect");
    CHECK(r.stages.size() == 2 && r.stages[1].slot && r.stages[1].first == 15 && r.stages[1].last == 15 &&
          r.stages[1].entries.size() == 2);
}

static void TestEdits() {
    std::string text = kDef;
    RosterFile r;
    Roster_Parse(text, r);
    // A cell rewritten keeps its indentation and comment
    RosterEntry e = r.chars[0].entries[0];
    e.SetParam("order", "5");
    CHECK(Roster_SetCell(text, r.chars[0], e.Text()));
    CHECK(Line(text, 3) == "kfm, stages/kfm.def, order=5 ;the first");
    // A slot becomes one line
    Roster_Parse(text, r);
    CHECK(Roster_SetCell(text, r.chars[2], r.chars[2].Text()));
    CHECK(Line(text, 6) == "slot = {kfm_zss, next=w | kfm_zaxis | suave.zip}" && Line(text, 7) == "boss, exclude = 1");
    // Moved before the first cell, then to the section's end
    Roster_Parse(text, r);
    CHECK(Roster_MoveCell(text, r.chars[4], r.chars[0].first));
    CHECK(Line(text, 3) == "randomselect" && Line(text, 4) == "kfm, stages/kfm.def, order=5 ;the first");
    Roster_Parse(text, r);
    CHECK(r.chars[0].entries[0].name == "randomselect");
    CHECK(Roster_MoveCell(text, r.chars[0], r.charsEnd));
    Roster_Parse(text, r);
    CHECK(r.chars.back().entries[0].name == "randomselect" && r.chars[0].entries[0].name == "kfm");
    // Inserted at the end, deleted
    CHECK(Roster_InsertLine(text, r.charsEnd, "newchar"));
    Roster_Parse(text, r);
    CHECK(r.chars.size() == 6 && r.chars.back().entries[0].name == "newchar");
    CHECK(Roster_DeleteCell(text, r.chars.back()));
    Roster_Parse(text, r);
    CHECK(r.chars.size() == 5 && r.stages.size() == 2);
    CHECK(Line(text, (int)std::count(text.begin(), text.end(), '\n')) == "arcade.maxmatches = 6,1");
}

static void TestFile() {
    std::ifstream in("data/select.def", std::ios::binary);
    if (!in) {
        printf("skipped (missing): data/select.def\n");
        return;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    std::string text;
    for (char ch : ss.str())
        if (ch != '\r') text += ch;
    RosterFile r;
    Roster_Parse(text, r);
    printf("select.def: %zu cells, %zu stages\n", r.chars.size(), r.stages.size());
    CHECK(r.charsLine > 0 && !r.chars.empty());
    for (const RosterCell &c : r.chars) CHECK(!c.entries.empty() && !c.entries[0].name.empty());
}

int main() {
    TestEntry();
    TestParse();
    TestEdits();
    TestFile();
    printf(g_failed ? "roster-test: %d failed\n" : "roster-test: all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
