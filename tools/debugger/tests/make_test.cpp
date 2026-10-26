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

int main() {
    TestCns();
    TestZss();
    printf(g_failed ? "make-test: %d failed\n" : "make-test: all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
