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

static void TestSelectors() {
    CHECK(Ini_SelectorMatches("p1", "p1") && Ini_SelectorMatches("p*", "p1") && Ini_SelectorMatches("p2|p1", "p1"));
    CHECK(!Ini_SelectorMatches("p*", "team1") && !Ini_SelectorMatches("p2|p3", "p1") && !Ini_SelectorMatches("p*", "pos"));
    CHECK(Ini_SelectorMatches("team*", "team2") && !Ini_SelectorMatches("p1|p11", "p"));
    CHECK(Ini_KeyApplies("p*.bg0.anim", "p1.bg0.") && Ini_KeyApplies("p1|p3.bg0.spr", "p3.bg0."));
    CHECK(!Ini_KeyApplies("p*.bg1.anim", "p1.bg0.") && !Ini_KeyApplies("p2.bg0.anim", "p1.bg0."));
    IniFile f;
    Ini_Parse("[Lifebar]\np1.pos = 1,2\np*.bg0.offset = 0,0\np1|p3.front.offset = 1,1\np*.front.offset = 2,2\n", f);
    const IniSection *s = &f.sections[0];
    CHECK(Ini_DefKeyFor(s, "p1.pos") == "p1.pos" && Ini_DefKeyFor(s, "p2.pos") == "");
    CHECK(Ini_DefKeyFor(s, "p2.bg0.offset") == "p*.bg0.offset");
    CHECK(Ini_DefKeyFor(s, "p3.front.offset") == "p1|p3.front.offset" && Ini_DefKeyFor(s, "p2.front.offset") == "p*.front.offset");
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

static void TestSpritesFile() {
    CHECK(Ini_SpritesFile("[Files]\nspr = \"a.sff\"\nsnd = a.snd\n") == "a.sff");  // a motif's
    CHECK(Ini_SpritesFile("[Info]\nname = x\n[Files]\nsff = fight.sff\n") == "fight.sff"); // a fight screen's
    CHECK(Ini_SpritesFile("[BGdef]\nspr = stage.sff\n") == "stage.sff");
    CHECK(Ini_SpritesFile("[SceneDef]\nspr = intro.sff\n") == "intro.sff");
    CHECK(Ini_SpritesFile("[Info]\nspr = no.sff\n").empty());
}

static void TestRefs() {
    IniFile f;
    Ini_Parse("[Files]\nspr = a.sff\nsnd = a.snd\n"
              "[BGdef]\nspr = a.sff\n"
              "[Lifebar]\np1.bg0.spr = 10,0\np*.ko.anim = 5\ncursor.move.snd = 100,0\nspr.x = 1\nanimated = 5\n"
              "[BG a]\ntype = anim\nactionno = 5\nspriteno = 1,0\n"
              "[BGCtrl b]\ntype = Anim\nvalue = 5\n"
              "[BGCtrl c]\ntype = PosAdd\nvalue = 5\n"
              "[Begin Action 5]\n",
              f);
    auto ref = [&](int s, const char *key) {
        for (const IniField &k : f.sections[s].fields)
            if (k.key == key) return Ini_FieldRef(f.sections[s], k);
        return (IniRefKind)-1;
    };
    CHECK(ref(0, "spr") == kIniRefNone && ref(0, "snd") == kIniRefNone && ref(1, "spr") == kIniRefNone);
    CHECK(ref(2, "p1.bg0.spr") == kIniRefSprite);
    CHECK(ref(2, "p*.ko.anim") == kIniRefAction);
    CHECK(ref(2, "cursor.move.snd") == kIniRefSound);
    CHECK(ref(2, "spr.x") == kIniRefNone && ref(2, "animated") == kIniRefNone);
    CHECK(ref(3, "actionno") == kIniRefAction && ref(3, "spriteno") == kIniRefSprite);
    CHECK(ref(4, "value") == kIniRefAction && ref(5, "value") == kIniRefNone);
    std::vector<std::pair<int, int>> users = Ini_ActionUsers(f, 5);
    CHECK(users.size() == 3); // p*.ko.anim, actionno, the BGCtrl Anim's value
    CHECK(Ini_ActionUsers(f, 6).empty());
    // What the scripts look up in the character shown, not in the def
    IniFile g;
    Ini_Parse("[Select Info]\np1.face.anim = 0\np1.face.done.anim = 181\np1.face.spr = 9000,1\np2.palmenu.preview.anim = 181\n"
              "p1.face.random.anim = 3\np1.cursor.active.anim = 4\n"
              "[VS Screen]\np1.anim = 0\np1.done.anim = 181\np1.bg0.anim = 5\n",
              g);
    auto refIn = [&](int s, const char *key) {
        for (const IniField &k : g.sections[s].fields)
            if (k.key == key) return Ini_FieldRef(g.sections[s], k);
        return (IniRefKind)-1;
    };
    for (const char *key : {"p1.face.anim", "p1.face.done.anim", "p1.face.spr", "p2.palmenu.preview.anim"}) CHECK(refIn(0, key) == kIniRefNone);
    CHECK(refIn(0, "p1.face.random.anim") == kIniRefAction && refIn(0, "p1.cursor.active.anim") == kIniRefAction);
    CHECK(refIn(1, "p1.anim") == kIniRefNone && refIn(1, "p1.done.anim") == kIniRefNone);
    CHECK(refIn(1, "p1.bg0.anim") == kIniRefAction);
}

static void TestBgTime() {
    IniFile f;
    Ini_Parse("[BGCtrlDef A]\nlooptime = 100\n[BGCtrl a]\ntime = 10\n[BGCtrl b]\ntime = 10, 20\n[BGCtrl c]\ntime = 10, , 50\n"
              "[BGCtrlDef B]\n[BGCtrl d]\ntime = 1, 2, 3\n[BGCtrl e]\ntype = null\n",
              f);
    IniBgTime t = Ini_BgCtrlTime(f, 1);
    CHECK(t.start == 10 && t.end == 10 && t.loop == 100 && !t.ownLoop);
    CHECK(Ini_BgTimeValue(t) == "10");
    t = Ini_BgCtrlTime(f, 2);
    CHECK(t.start == 10 && t.end == 20 && t.loop == 100 && !t.ownLoop);
    CHECK(Ini_BgTimeValue(t) == "10, 20");
    t = Ini_BgCtrlTime(f, 3); // an empty part keeps what it had
    CHECK(t.start == 10 && t.end == 10 && t.loop == 50 && t.ownLoop);
    CHECK(Ini_BgTimeValue(t) == "10, 10, 50");
    t = Ini_BgCtrlTime(f, 5); // the last BGCtrlDef is the one that counts
    CHECK(t.start == 1 && t.end == 2 && t.loop == 3 && t.ownLoop);
    t = Ini_BgCtrlTime(f, 6);
    CHECK(t.start == 0 && t.end == 0 && t.loop == -1);
}

int main() {
    TestBgTime();
    TestSpritesFile();
    TestRefs();
    TestParse();
    TestEdits();
    TestSelectors();
    TestKfm();
    printf(g_failed ? "ini-test: %d failed\n" : "ini-test: all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
