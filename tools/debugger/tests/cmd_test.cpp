// Tests of src/cmd.cpp: parsing CMD text and the edits of it, then kfm.cmd
// (run from the root of the checkout; skipped when missing).
// Built as cmd-test; scripts/build.ps1 test builds and runs it.
#include "cmd.h"

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

static const char *kCmd =
    "; comment\n"
    "[Remap]\n"
    "x = x\n"
    "\n"
    "[Defaults]\n"
    "command.time = 20 ; frames\n"
    "command.buffer.time = 2\n"
    "\n"
    "[Command]\n"
    "name = \"QCF_x\"\n"
    "command = ~D, DF, F, x\n"
    "\n"
    "[Command]\n"
    "name = \"QCF_x\"   ;Same name as above\n"
    "command = ~D, F, x\n"
    "time = 12\n"
    "\n"
    "[Command]\n"
    "name = \"x\"\n"
    "command = x\n"
    "time = 1\n"
    "\n"
    "[Statedef -1]\n"
    "[State -1, x]\n"
    "type = ChangeState\n"
    "trigger1 = command = \"x\"\n";

static std::string Line(const std::string &text, int n) {
    std::istringstream in(text);
    std::string line;
    for (int i = 0; i < n && std::getline(in, line); i++) {}
    return line;
}

static void TestParse() {
    CmdFile f;
    Cmd_Parse(kCmd, f);
    CHECK(f.commands.size() == 3);
    CHECK(f.defaultsLine == 5 && f.time == 20 && f.bufferTime == 2);
    if (f.commands.size() != 3) return;
    const CmdCommand &a = f.commands[0], &b = f.commands[1], &c = f.commands[2];
    CHECK(a.line == 9 && a.last == 11 && a.name == "QCF_x" && a.command == "~D, DF, F, x" && a.index == 0);
    CHECK(b.line == 13 && b.last == 16 && b.name == "QCF_x" && b.index == 1 && b.Int("time", f.time) == 12);
    CHECK(a.Int("time", f.time) == 20);
    CHECK(c.name == "x" && c.index == 0 && c.last == 21); // the states after it are not fields
}

static void TestEdits() {
    std::string text = kCmd;
    CmdFile f;
    Cmd_Parse(text, f);

    // A field replaced keeps its comment; one added goes after the last field; an empty value removes it
    CHECK(Cmd_SetName(text, f.commands[1], "QCF_y"));
    CHECK(Line(text, 14) == "name = \"QCF_y\"   ;Same name as above");
    Cmd_Parse(text, f);
    CHECK(Cmd_SetField(text, f.commands[0], "time", "10"));
    CHECK(Line(text, 12) == "time = 10" && Line(text, 13) == "");
    Cmd_Parse(text, f);
    CHECK(f.commands[0].Int("time", 0) == 10 && f.commands[1].line == 14);
    CHECK(Cmd_SetField(text, f.commands[0], "time", ""));
    Cmd_Parse(text, f);
    CHECK(f.commands[0].Field("time") == nullptr && f.commands[0].last == 11);

    // Duplicate, then delete the copy: the text as it was
    std::string before = text;
    CHECK(Cmd_Duplicate(text, f.commands[0]));
    Cmd_Parse(text, f);
    CHECK(f.commands.size() == 4 && f.commands[1].name == "QCF_x" && f.commands[1].index == 1 &&
          f.commands[1].line == 13);
    CHECK(Cmd_Delete(text, f.commands[1]));
    CHECK(text == before);

    // Added after one, and before the first
    Cmd_Parse(text, f);
    CHECK(Cmd_Add(text, f, &f.commands[2], "new", "a+b"));
    Cmd_Parse(text, f);
    CHECK(f.commands.size() == 4 && f.commands[3].name == "new" && f.commands[3].command == "a+b");
    CHECK(Cmd_Add(text, f, nullptr, "first", "c"));
    Cmd_Parse(text, f);
    CHECK(f.commands.size() == 5 && f.commands[0].name == "first" && f.commands[0].line == 9);
    CHECK(f.commands[1].name == "QCF_x" && f.commands[1].line == 14);

    // A section that moved is not edited
    CmdCommand stale = f.commands[0];
    stale.line = 3;
    CHECK(!Cmd_SetField(text, stale, "time", "1"));
}

static void TestKfm() {
    std::ifstream in("chars/kfm/kfm.cmd", std::ios::binary);
    if (!in) {
        printf("skipped (missing): chars/kfm/kfm.cmd\n");
        return;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    std::string text;
    for (char ch : ss.str())
        if (ch != '\r') text += ch;
    CmdFile f;
    Cmd_Parse(text, f);
    int twice = 0;
    for (const auto &c : f.commands) twice += c.index == 1;
    printf("kfm.cmd: %zu commands, %d defined twice or more\n", f.commands.size(), twice);
    CHECK(f.commands.size() == 37);
    CHECK(!f.commands.empty() && f.commands[0].name == "TripleKFPalm" && f.commands[1].index == 1);
    for (const auto &c : f.commands) CHECK(!c.name.empty() && !c.command.empty());
    // Every motion of kfm.cmd is shown as steps and written back as it was
    for (const auto &c : f.commands) {
        std::vector<CmdStep> steps = Cmd_Steps(c.command);
        bool shown = true;
        for (const auto &s : steps) shown &= s.raw.empty();
        CHECK(shown);
        std::string back = Cmd_StepsText(steps), want;
        for (char ch : c.command) want += ch == ' ' ? "" : std::string(1, ch);
        std::string got;
        for (char ch : back) got += ch == ' ' ? "" : std::string(1, ch);
        if (got != want) printf("  %s: \"%s\" written back as \"%s\"\n", c.name.c_str(), c.command.c_str(), back.c_str());
        CHECK(got == want);
    }
}

static void TestSteps() {
    std::vector<CmdStep> s = Cmd_Steps("~D, DF, F, x");
    CHECK(s.size() == 4 && s[0].release && s[0].dir == 3 && s[1].dir == 4 && s[3].dir == 0 && s[3].buttons == 1u << 3);
    s = Cmd_Steps("~30$B, F, x+y");
    CHECK(s.size() == 3 && s[0].charge == 30 && s[0].four && s[0].dir == 1 && s[2].buttons == ((1u << 3) | (1u << 4)));
    CHECK(Cmd_StepsText(s) == "~30$B, F, x+y");
    s = Cmd_Steps("/$D");
    CHECK(s.size() == 1 && s[0].hold && s[0].four && Cmd_StepText(s[0]) == "/$D");
    s = Cmd_Steps(">~F, DF+a");
    CHECK(s.size() == 2 && s[0].strict && s[0].release && s[1].dir == 4 && s[1].buttons == 1);
    CHECK(Cmd_StepsText(s) == ">~F, DF+a");
    // What the editor does not show stays as written
    s = Cmd_Steps("a|b, /30F, D+~a, N, s");
    CHECK(s.size() == 5 && s[0].raw == "a|b" && s[1].raw == "/30F" && s[2].raw == "D+~a" && s[3].raw == "N");
    CHECK(s[4].raw.empty() && s[4].buttons == 1u << 6);
    CHECK(Cmd_StepsText(s) == "a|b, /30F, D+~a, N, s");
    CHECK(Cmd_Steps("m+w")[0].buttons == ((1u << 9) | (1u << 8)));
}

int main() {
    TestParse();
    TestEdits();
    TestKfm();
    TestSteps();
    printf(g_failed ? "cmd-test: %d failed\n" : "cmd-test: all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
