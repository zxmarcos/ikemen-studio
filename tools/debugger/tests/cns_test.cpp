// Tests of src/cns.cpp: CNS and ZSS state files read into statedefs, controllers and lines of
// code, then kfm.cns, kfm.cmd and the common ZSS files (run from the root of the checkout;
// skipped when missing). Built as cns-test; scripts/build.ps1 test builds and runs it.
#include "cns.h"

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

static const char *kCns =
    "[Data]\n"                       // 1
    "life = 1000\n"                  // 2
    "\n"                             // 3
    "[Statedef 200]\n"               // 4
    "type = S\n"                     // 5
    "anim = 200 ; comment\n"         // 6
    "[State 200, hit]\n"             // 7
    "type = HitDef\n"                // 8
    "trigger1 = AnimElem = 3\n"      // 9
    "damage = 23, 0\n"               // 10
    "p2stateno = 5000\n"             // 11
    "[State 200, end]\n"             // 12
    "type = ChangeState\n"           // 13
    "trigger1 = AnimTime = 0\n"      // 14
    "value = 0\n"                    // 15
    "[State 200, no trigger]\n"      // 16
    "type = VarSet\n"                // 17
    "v = 3\n"                        // 18
    "value = 1\n"                    // 19
    "[Statedef -1]\n"                // 20
    "[State -1, x]\n"                // 21
    "type = ChangeState\n"           // 22
    "triggerall = command = \"x;y\"\n" // 23: a ";" in quotes is no comment
    "trigger1 = ctrl\n"              // 24
    "value = 200\n";                 // 25

static const char *kZss =
    "# comment\n"                                // 1
    "[StateDef 0; type: S; anim: 0;]\n"          // 2
    "if time = 0 {\n"                            // 3
    "\tvelSet{y: 0}\n"                           // 4
    "}\n"                                        // 5
    "if !alive {\n"                              // 6
    "\tchangeState{value: 5050; ctrl: 0} # end\n" // 7
    "}\n"                                        // 8
    "var(3) := 1;\n"                             // 9
    "[Function F(x) ret]\n"                      // 10
    "let ret = 1;\n"                             // 11
    "[StateDef 210; anim: 210;]\n"               // 12
    "hitDef{attr: S, NA;\n"                      // 13
    "\tdamage: 23, 0; p2stateno: 5000}\n"        // 14
    "ignoreHitPause if time = 1 {\n"             // 15
    "\tplaySnd{value: S0, 1}\n"                  // 16
    "}\n"                                        // 17
    "persistent(0) if 1 { changeState{value: 0} }\n" // 18
    "[StateDef 220;\n"                                 // 19: a header over lines
    "type: S; anim: 220;]\n"                           // 20
    "changeState{value: 0}\n";                         // 21

static void TestCns() {
    CnsFile f;
    Cns_Parse(kCns, false, f);
    CHECK(f.states.size() == 2);
    if (f.states.size() != 2) return;
    const CnsState &s = f.states[0], &m = f.states[1];
    CHECK(s.no == 200 && s.literal && s.line == 4 && s.Param("anim") && s.Param("anim")->value == "200");
    CHECK(s.ctrls.size() == 3 && m.no == -1 && m.ctrls.size() == 1);
    if (s.ctrls.size() != 3 || m.ctrls.size() != 1) return;
    const CnsCtrl &hit = s.ctrls[0], &end = s.ctrls[1], &var = s.ctrls[2];
    CHECK(hit.type == "hitdef" && hit.line == 7 && hit.hasTrigger);
    CHECK(hit.Param("damage") && hit.Param("damage")->value == "23, 0" && hit.Param("damage")->line == 10);
    CHECK(hit.Param("p2stateno") && hit.Param("p2stateno")->value == "5000" && !hit.Param("trigger1"));
    CHECK(end.type == "changestate" && end.Param("value")->value == "0");
    CHECK(var.type == "varset" && !var.hasTrigger && var.Param("v")->value == "3");
    CHECK(m.ctrls[0].hasTrigger && m.ctrls[0].Param("value")->value == "200");
    bool command = false;
    for (const auto &c : f.code)
        if (c.line == 23) command = c.text == "command = \"x;y\"" && c.state == 1;
    CHECK(command);
}

static void TestZss() {
    CnsFile f;
    Cns_Parse(kZss, true, f);
    CHECK(f.zss && f.states.size() == 4);
    if (f.states.size() != 4) return;
    CHECK(f.states[3].no == 220 && f.states[3].line == 19 && f.states[3].Param("anim") &&
          f.states[3].Param("anim")->value == "220" && f.states[3].ctrls.size() == 1);
    const CnsState &s0 = f.states[0], &fn = f.states[1], &s210 = f.states[2];
    CHECK(s0.no == 0 && s0.literal && s0.line == 2 && s0.Param("anim")->value == "0");
    CHECK(fn.function == "F" && !fn.literal && fn.ctrls.empty());
    CHECK(s210.no == 210 && s210.line == 12);
    CHECK(s0.ctrls.size() == 2);
    if (s0.ctrls.size() == 2) {
        CHECK(s0.ctrls[0].type == "velset" && s0.ctrls[0].line == 4 && s0.ctrls[0].Param("y")->value == "0");
        const CnsCtrl &c = s0.ctrls[1];
        CHECK(c.type == "changestate" && c.line == 7 && c.Param("value")->value == "5050" && c.Param("ctrl")->value == "0");
    }
    CHECK(s210.ctrls.size() == 3);
    if (s210.ctrls.size() == 3) {
        const CnsCtrl &h = s210.ctrls[0];
        CHECK(h.type == "hitdef" && h.line == 13 && h.Param("attr")->value == "S, NA");
        CHECK(h.Param("damage") && h.Param("damage")->value == "23, 0" && h.Param("damage")->line == 14);
        CHECK(h.Param("p2stateno") && h.Param("p2stateno")->value == "5000");
        CHECK(s210.ctrls[1].type == "playsnd" && s210.ctrls[1].Param("value")->value == "S0, 1");
        CHECK(s210.ctrls[1].triggers == "time = 1\n" && s210.ctrls[0].triggers.empty()); // its if's condition
        CHECK(s210.ctrls[2].type == "changestate" && s210.ctrls[2].Param("value")->value == "0");
    }
    bool assign = false;
    for (const auto &c : f.code)
        if (c.line == 9) assign = c.text == "var(3) := 1;" && c.state == 0;
    CHECK(assign);
}

static void TestNumbers() {
    CnsNumbers n;
    CHECK(Cns_Numbers("5050", n) && n.prefix == 0 && n.n.size() == 1 && n.n[0] == 5050);
    CHECK(Cns_Numbers("S0, 1", n) && n.prefix == 'S' && n.n.size() == 2 && n.n[1] == 1);
    CHECK(Cns_Numbers(" f5 ", n) && n.prefix == 'F' && n.n[0] == 5);
    CHECK(Cns_Numbers("-1", n) && n.n[0] == -1);
    CHECK(!Cns_Numbers("ifElse(var(1), 200, 210)", n));
    CHECK(!Cns_Numbers("0.5", n) && !Cns_Numbers("", n));
}

static bool ReadText(const char *path, std::string &text) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        printf("skipped (missing): %s\n", path);
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    text = ss.str();
    return true;
}

static void TestFiles() {
    std::string text;
    if (ReadText("chars/kfm/kfm.cns", text)) {
        CnsFile f;
        Cns_Parse(text, false, f);
        size_t ctrls = 0;
        const CnsCtrl *hit200 = nullptr;
        for (const auto &s : f.states) {
            ctrls += s.ctrls.size();
            if (s.no == 200)
                for (const auto &c : s.ctrls)
                    if (c.type == "hitdef") hit200 = &c;
        }
        printf("kfm.cns: %zu statedefs, %zu controllers\n", f.states.size(), ctrls);
        CHECK(f.states.size() > 50);
        CHECK(hit200 && hit200->line == 268 && hit200->Param("damage") && hit200->Param("damage")->value == "23, 0");
    }
    if (ReadText("chars/kfm/kfm.cmd", text)) {
        CnsFile f;
        Cns_Parse(text, false, f);
        CHECK(f.states.size() == 1 && f.states[0].no == -1 && f.states[0].ctrls.size() > 20);
        printf("kfm.cmd: state -1 with %zu controllers\n", f.states.empty() ? 0 : f.states[0].ctrls.size());
    }
    if (ReadText("data/common1.cns.zss", text)) {
        CnsFile f;
        Cns_Parse(text, true, f);
        size_t ctrls = 0;
        bool dead = false;
        for (const auto &s : f.states) {
            ctrls += s.ctrls.size();
            if (s.no == 0)
                for (const auto &c : s.ctrls)
                    if (c.type == "changestate" && c.Param("value") && c.Param("value")->value == "5050") dead = true;
        }
        printf("common1.cns.zss: %zu statedefs, %zu controllers\n", f.states.size(), ctrls);
        CHECK(f.states.size() > 50 && dead);
    }
    if (ReadText("chars/kfm_zss/kfm.zss", text)) {
        CnsFile f;
        Cns_Parse(text, true, f);
        size_t ctrls = 0, functions = 0;
        for (const auto &s : f.states) ctrls += s.ctrls.size(), functions += !s.function.empty();
        printf("kfm_zss/kfm.zss: %zu statedefs, %zu functions, %zu controllers\n", f.states.size() - functions,
               functions, ctrls);
        CHECK(f.states.size() - functions == 55 && functions == 4); // its headers go over lines
    }
    if (ReadText("data/functions.zss", text)) {
        CnsFile f;
        Cns_Parse(text, true, f);
        size_t functions = 0;
        for (const auto &s : f.states) functions += !s.function.empty();
        printf("functions.zss: %zu functions\n", functions);
        CHECK(functions > 3 && !f.states.empty() && f.states[0].function == "IkSys_FirstAttack");
    }
}

int main() {
    TestCns();
    TestZss();
    TestNumbers();
    TestFiles();
    printf(g_failed ? "cns-test: %d failed\n" : "cns-test: all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
