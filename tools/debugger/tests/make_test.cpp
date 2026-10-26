// Tests of src/make.cpp: the text written for new things, read back with src/cns.cpp as the
// engine would. Built as make-test; scripts/build.ps1 test builds and runs it.
#include "make.h"

#include <cstdio>
#include <string>

#include "cns.h"

static int g_failed;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failed++;                                                 \
        }                                                               \
    } while (0)

static const CnsState *Find(const CnsFile &f, int no) {
    for (const auto &s : f.states)
        if (s.no == no && s.function.empty()) return &s;
    return nullptr;
}

static void TestCns() {
    std::string text = "[Statedef -1]\n"
                       "\n"
                       "[State -1, old]\n"
                       "type = ChangeState\n"
                       "value = 1000\n"
                       "trigger1 = command = \"old\"\n";
    int line = 0;
    CHECK(Make_InsertInStatedef(text, false, -1, Make_CommandEntry(false, "QCF_x", 1200, 1, true), &line));
    CHECK(line == 3);
    CnsFile f;
    Cns_Parse(text, false, f);
    const CnsState *s = Find(f, -1);
    CHECK(s && s->ctrls.size() == 2);
    if (s && s->ctrls.size() == 2) {
        const CnsCtrl &c = s->ctrls[0];
        CHECK(c.type == "changestate" && c.line == 3 && c.Param("value")->value == "1200" && c.hasTrigger);
        CHECK(c.triggers == "command = \"qcf_x\"\nstatetype = s\nctrl\n");
        CHECK(s->ctrls[1].Param("value")->value == "1000");
    }
    CHECK(!Make_InsertInStatedef(text, false, 200, "x"));
    // A statedef with no controller yet: after its parameters
    text = "[Statedef 200]\ntype = S\nanim = 200\n\n[Statedef 210]\n";
    CHECK(Make_InsertInStatedef(text, false, 200, Make_CommandEntry(false, "x", 0, 0, false), &line));
    Cns_Parse(text, false, f);
    s = Find(f, 200);
    CHECK(s && s->ctrls.size() == 1 && s->Param("anim") && Find(f, 210) && Find(f, 210)->ctrls.empty());
    CHECK(s && !s->ctrls.empty() && s->ctrls[0].line == line && s->ctrls[0].triggers == "command = \"x\"\n1\n");
}

static void TestZss() {
    std::string text = "[StateDef -1]\n"
                       "\n"
                       "if roundState = 2 {\n"
                       "\tchangeState{value: 1000}\n"
                       "}\n"
                       "[StateDef 200;\n"
                       "type: S;]\n"
                       "velSet{x: 0}\n";
    int line = 0;
    CHECK(Make_InsertInStatedef(text, true, -1, Make_CommandEntry(true, "QCF_x", 1200, 3, false), &line));
    CHECK(line == 3);
    CnsFile f;
    Cns_Parse(text, true, f);
    const CnsState *s = Find(f, -1);
    CHECK(s && s->ctrls.size() == 2);
    if (s && s->ctrls.size() == 2) {
        CHECK(s->ctrls[0].type == "changestate" && s->ctrls[0].Param("value")->value == "1200");
        CHECK(s->ctrls[0].triggers == "command = \"qcf_x\" && statetype = a\n");
        CHECK(s->ctrls[1].triggers == "roundstate = 2\n");
    }
    // A header over two lines
    CHECK(Make_InsertInStatedef(text, true, 200, "posSet{y: 0}\n"));
    Cns_Parse(text, true, f);
    s = Find(f, 200);
    CHECK(s && s->ctrls.size() == 2 && s->ctrls[0].type == "posset" && s->Param("type"));
}

// A move's statedef read back as the engine would, CNS and ZSS alike
static void TestMove() {
    for (bool zss : {false, true}) {
        MakeMove m;
        m.state = 260;
        m.action = 210;
        m.kind = 1;
        m.klass = 2;
        m.hit = kMakeHits[1];
        m.hitElem = 3;
        m.sparkY = -40;
        m.sound = true;
        m.snd[0] = 0, m.snd[1] = 2;
        std::string text = (zss ? "[StateDef 200; type: S;]\nvelSet{x: 0}\n" : "[Statedef 200]\ntype = S\n") +
                           std::string("\n") + Make_MoveState(m, zss);
        CnsFile f;
        Cns_Parse(text, zss, f);
        const CnsState *s = Find(f, 260);
        CHECK(f.states.size() == 2 && s);
        if (!s) continue;
        CHECK(s->Param("type")->value == "C" && s->Param("movetype")->value == "A" && s->Param("anim")->value == "210");
        CHECK(s->Param("poweradd")->value == "-1000" && s->Param("velset")->value == "0, 0");
        CHECK(s->ctrls.size() == 3);
        if (s->ctrls.size() != 3) continue;
        const CnsCtrl &snd = s->ctrls[0], &hit = s->ctrls[1], &end = s->ctrls[2];
        CHECK(snd.type == "playsnd" && snd.Param("value")->value == "0, 2" && snd.triggers == "time = 1\n");
        CHECK(hit.type == "hitdef" && hit.triggers == "animelem = 3\n" && hit.Param("attr")->value == "C, HA");
        CHECK(hit.Param("damage")->value == "45, 0" && hit.Param("guardflag")->value == "L");
        CHECK(hit.Param("sparkxy")->value == "-10, -40" && hit.Param("air.velocity")->value == "-2, -4");
        CHECK(hit.Param("animtype")->value == "Medium" && hit.Param("ground.type")->value == "Low");
        CHECK(end.type == "changestate" && end.Param("value")->value == "11" && end.Param("ctrl")->value == "1");
        if (g_failed) printf("%s", text.c_str());
    }
    // In the air: no velset, control back at the end, landing ends it
    MakeMove a;
    a.kind = 2;
    CnsFile f;
    Cns_Parse(Make_MoveState(a, false), false, f);
    CHECK(f.states.size() == 1 && !f.states[0].Param("velset") && f.states[0].ctrls.size() == 2 &&
          f.states[0].ctrls[1].type == "ctrlset" && f.states[0].ctrls[0].triggers == "time = 0\n");
    // The hyper's command entry asks for the power
    CnsFile e;
    Cns_Parse("[Statedef -1]\n" + Make_CommandEntry(false, "x", 3000, 1, true, "power >= 1000"), false, e);
    CHECK(e.states.size() == 1 && e.states[0].ctrls.size() == 1 &&
          e.states[0].ctrls[0].triggers == "command = \"x\"\npower >= 1000\nstatetype = s\nctrl\n");
    CHECK(Make_CommandEntry(true, "x", 3000, 0, false, "power >= 1000") ==
          "if command = \"x\" && power >= 1000 {\n\tchangeState{value: 3000}\n}\n");
}

int main() {
    TestCns();
    TestZss();
    TestMove();
    printf(g_failed ? "make-test: %d failed\n" : "make-test: all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
