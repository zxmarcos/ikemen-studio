// The debugger: launches Ikemen GO with the ikdbg.lua mod, reads its
// snapshots and shows them; every action is Lua sent through the game's stdin.
#include "app.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "bridge.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "json.h"

namespace {

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

struct Entity {
    int id = 0, pn = 0;
    bool helper = false;
    std::string name;
    int st = 0, pst = 0, anim = 0, elem = 0, elemn = 0, etime = 0, atime = 0, time = 0;
    double x = 0, y = 0, vx = 0, vy = 0;
    int facing = 1;
    int life = 0, lifemax = 0, pow = 0, powmax = 0;
    bool ctrl = false, alive = true;
    std::string stt, mvt, phy;
    int hitpause = 0, moveHit = 0, moveContact = 0, moveGuarded = 0;
    int stateSrc = 0; // source index of the current statedef (patched Ikemen only)
    std::vector<double> vars, fvars;
};

struct FrameRow {
    int t, st, anim, elem, etime, hitpause, life;
    bool ctrl, stateChanged, contact;
    std::string mvt, stt;
    double x, y, vx, vy;
    int stateSrc;
    std::vector<int> ran; // source indices of the controllers that ran on this tick
};

// Where a state controller or statedef comes from (patched Ikemen only).
struct Source {
    std::string file, type;
    int line = 0, state = 0;
};

// A breakpoint on the controller(s) at a line of a file.
struct CtrlBreak {
    std::string file;
    int line;
    bool enabled = true;
    int hits = 0;
};

// A source file shown in the Code window.
struct SourceFile {
    std::vector<std::string> lines;
    bool missing = false;
};

struct TraceEvent {
    int t, id, pn;
    bool helper;
    std::string kind;
    int from, to, st;
};

struct Breakpoint {
    int bid;
    int target; // 0 any, n player n, -n player n and its helpers
    std::string cond;
    bool enabled = true;
    int hits = 0;
};

struct LogLine {
    enum Kind { Info, Result, Error, Engine, Break } kind;
    std::string text;
};

struct Settings {
    std::string exe; // default: the Ikemen_GO.exe of the checkout this exe was built in
    std::string p1 = "kfm", p2 = "kfm", stage = "stage0", extra = "-windowed";
    bool ai1 = false, ai2 = true;
};

Bridge g_bridge;
Settings g_settings;
std::string g_iniPath;
bool g_firstRun, g_autoLaunch;

bool g_hello;
std::string g_modVersion, g_ikemenVersion;
bool g_exitReported;

int g_tick, g_roundState, g_roundNo, g_steps;
bool g_paused;
std::vector<Entity> g_ents;
std::map<int, std::vector<double>> g_prevVars, g_prevFvars;
std::map<int, std::deque<FrameRow>> g_history;
std::deque<TraceEvent> g_trace;
std::vector<Breakpoint> g_breakpoints;
int g_nextBid = 1;
std::string g_breakText;
std::deque<LogLine> g_log;
bool g_logScroll;
int g_selId = -1, g_watchId = -1;
unsigned g_snapshots, g_lastRate, g_rateCount;
double g_rateTime;

bool g_canTrace, g_canSet;               // what the game's Ikemen GO offers
std::vector<Source> g_sources;           // by source index; 0 is unused
std::map<int, std::vector<int>> g_ran;   // entity id -> controllers that ran on the last tick
std::vector<CtrlBreak> g_ctrlBreaks;
int g_selTick = -1;                      // tick picked in Frame data (-1: the last one)
std::map<std::string, SourceFile> g_files;
std::string g_codeFile;                  // file shown in the Code window
int g_codeLine = 0, g_codeScrollTo = 0;  // line to highlight / to scroll to (1-based)
bool g_codeHold;                         // keep the view on a breakpoint instead of following the state

const size_t kHistory = 1200, kTrace = 4000, kLog = 4000;

void Log(LogLine::Kind kind, const std::string &text) {
    g_log.push_back({kind, text});
    while (g_log.size() > kLog) g_log.pop_front();
    g_logScroll = true;
}

std::string Who(int pn, bool helper, int id) {
    char buf[48];
    snprintf(buf, sizeof(buf), helper ? "P%d helper %d" : "P%d (%d)", pn, id);
    return buf;
}

const Entity *FindEntity(int id) {
    for (const auto &e : g_ents)
        if (e.id == id) return &e;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

bool Connected() { return g_bridge.Running() && g_hello; }

void SendRaw(const std::string &lua) {
    if (!g_bridge.Send("if ikdbg then " + lua + " end")) Log(LogLine::Error, "The game is not running.");
}

// Runs Lua in the game; the answer comes back as @IKRES.
void Exec(const std::string &lua) { SendRaw("ikdbg.exec(" + LuaQuote(lua) + ")"); }

// Runs Lua with the trigger context on one entity.
void ExecOn(int id, const std::string &lua) {
    char prefix[64];
    snprintf(prefix, sizeof(prefix), "if not playerId(%d) then return 'no entity %d' end; ", id, id);
    Exec(prefix + lua);
}

void SendBreakpoint(const Breakpoint &b) {
    char head[64];
    snprintf(head, sizeof(head), "ikdbg.brkSet(%d, %d, ", b.bid, b.target);
    SendRaw(head + LuaQuote(b.cond) + (b.enabled ? ", true)" : ", false)"));
}

void Watch(int id) {
    if (id == g_watchId) return;
    if (g_watchId >= 0) SendRaw("ikdbg.setWatch(" + std::to_string(g_watchId) + ", false)");
    if (id >= 0) SendRaw("ikdbg.setWatch(" + std::to_string(id) + ", true)");
    g_watchId = id;
}

// Paths compare case-insensitively with '/' separators.
std::string NormPath(const std::string &path) {
    std::string n = path;
    for (char &ch : n) ch = ch == '\\' ? '/' : (char)tolower((unsigned char)ch);
    return n;
}

bool SourceAt(const Source &s, const std::string &normFile, int line) {
    return s.line == line && s.type != "statedef" && NormPath(s.file) == normFile;
}

// Tells the game which source indices a controller breakpoint covers.
void SendCtrlBreak(const CtrlBreak &b, bool on, size_t from = 1) {
    std::string file = NormPath(b.file);
    for (size_t i = std::max<size_t>(from, 1); i < g_sources.size(); i++)
        if (SourceAt(g_sources[i], file, b.line))
            SendRaw("ikdbg.ctrlBrkSet(" + std::to_string(i) + (on ? ", true)" : ", false)"));
}

CtrlBreak *FindCtrlBreak(const std::string &file, int line) {
    std::string norm = NormPath(file);
    for (auto &b : g_ctrlBreaks)
        if (b.line == line && NormPath(b.file) == norm) return &b;
    return nullptr;
}

void ToggleCtrlBreak(const std::string &file, int line) {
    for (size_t i = 0; i < g_ctrlBreaks.size(); i++) {
        CtrlBreak &b = g_ctrlBreaks[i];
        if (b.line == line && NormPath(b.file) == NormPath(file)) {
            if (Connected()) SendCtrlBreak(b, false);
            g_ctrlBreaks.erase(g_ctrlBreaks.begin() + i);
            return;
        }
    }
    g_ctrlBreaks.push_back({file, line});
    if (Connected()) SendCtrlBreak(g_ctrlBreaks.back(), true);
}

std::string ExeDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring w = path;
    w = w.substr(0, w.find_last_of(L"\\/"));
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}

// The first Ikemen_GO.exe in this exe's folder or one above it (tools/debugger/build
// is three levels under the root of the Ikemen GO checkout).
std::string FindIkemen() {
    std::string dir = ExeDir();
    for (int up = 0; up < 5; up++) {
        std::string exe = dir + "\\Ikemen_GO.exe";
        if (GetFileAttributesA(exe.c_str()) != INVALID_FILE_ATTRIBUTES) return exe;
        size_t cut = dir.find_last_of("\\/");
        if (cut == std::string::npos) break;
        dir.resize(cut);
    }
    return "";
}

std::string ReadWholeFile(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Copies ikdbg.lua (next to this exe) into the game's external/mods.
bool InstallMod() {
    std::string mod = ReadWholeFile(ExeDir() + "\\ikdbg.lua");
    if (mod.empty()) {
        Log(LogLine::Error, "ikdbg.lua is missing next to ikemen-debugger.exe.");
        return false;
    }
    std::string dir = g_settings.exe.substr(0, g_settings.exe.find_last_of("\\/"));
    std::string target = dir + "\\external\\mods\\ikdbg.lua";
    if (ReadWholeFile(target) == mod) return true;
    std::ofstream out(target, std::ios::binary);
    out << mod;
    if (!out) {
        Log(LogLine::Error, "Could not write " + target);
        return false;
    }
    Log(LogLine::Info, "Installed the bridge mod: " + target);
    return true;
}

void Launch() {
    if (!InstallMod()) return;
    std::string args;
    const Settings &s = g_settings;
    if (!s.p1.empty() && !s.p2.empty()) {
        args += "-p1 " + s.p1 + " -p2 " + s.p2;
        if (s.ai1) args += " -p1.ai 1";
        if (s.ai2) args += " -p2.ai 1";
        if (!s.stage.empty()) args += " -s " + s.stage;
        // Without -loadmotif a command line match starts before the mods are loaded.
        args += " -loadmotif";
    }
    if (!s.extra.empty()) args += " " + s.extra;
    args += " -ikdbg 1";

    g_hello = false;
    g_exitReported = false;
    g_ents.clear();
    g_history.clear();
    g_prevVars.clear();
    g_prevFvars.clear();
    g_watchId = -1;
    g_breakText.clear();
    g_sources.assign(1, Source());
    g_ran.clear();
    g_files.clear();
    g_selTick = -1;
    for (auto &b : g_ctrlBreaks) b.hits = 0;
    std::string error;
    if (!g_bridge.Launch(s.exe, args, error)) {
        Log(LogLine::Error, error);
        return;
    }
    Log(LogLine::Info, "Started " + s.exe + " " + args + " (pid " + std::to_string(g_bridge.Pid()) + ")");
}

// ---------------------------------------------------------------------------
// What the game sends
// ---------------------------------------------------------------------------

void ReadEntity(const Json &j, Entity &e) {
    e.id = j["id"].i();
    e.pn = j["pn"].i();
    e.helper = j["h"].truthy();
    e.name = j["name"].str();
    e.st = j["st"].i();
    e.pst = j["pst"].i();
    e.anim = j["anim"].i();
    e.elem = j["elem"].i();
    e.elemn = j["elemn"].i();
    e.etime = j["etime"].i();
    e.atime = j["atime"].i();
    e.time = j["time"].i();
    e.x = j["x"].num();
    e.y = j["y"].num();
    e.vx = j["vx"].num();
    e.vy = j["vy"].num();
    e.facing = j["f"].i(1);
    e.life = j["life"].i();
    e.lifemax = j["lifemax"].i();
    e.pow = j["pow"].i();
    e.powmax = j["powmax"].i();
    e.ctrl = j["ctrl"].truthy();
    e.alive = j["alive"].truthy();
    e.stt = j["stt"].str();
    e.mvt = j["mvt"].str();
    e.phy = j["phy"].str();
    e.hitpause = j["hp"].i();
    e.moveHit = j["mh"].i();
    e.moveContact = j["mc"].i();
    e.moveGuarded = j["mg"].i();
    e.stateSrc = j["ss"].i();
    for (const auto &v : j["vars"].items) e.vars.push_back(v.num());
    for (const auto &v : j["fvars"].items) e.fvars.push_back(v.num());
}

void OnSnapshot(const Json &j) {
    g_snapshots++;
    g_rateCount++;
    g_tick = j["t"].i();
    g_roundState = j["rs"].i();
    g_roundNo = j["rn"].i();
    g_paused = j["paused"].truthy();
    g_steps = j["steps"].i();
    bool advanced = j["adv"].truthy();

    if (advanced) {
        for (const auto &e : g_ents) {
            if (!e.vars.empty()) g_prevVars[e.id] = e.vars;
            if (!e.fvars.empty()) g_prevFvars[e.id] = e.fvars;
        }
    }
    g_ents.clear();
    for (const auto &item : j["ents"].items) {
        Entity e;
        ReadEntity(item, e);
        g_ents.push_back(std::move(e));
    }

    std::map<int, bool> changed, contact;
    for (const auto &ev : j["ev"].items) {
        TraceEvent t;
        t.t = ev["t"].i();
        t.id = ev["id"].i();
        t.pn = ev["pn"].i();
        t.helper = ev["h"].truthy();
        t.kind = ev["k"].str();
        t.from = ev["from"].i();
        t.to = ev["to"].i();
        t.st = ev["st"].i();
        if (t.kind == "st") changed[t.id] = true;
        else contact[t.id] = true;
        g_trace.push_back(t);
    }
    while (g_trace.size() > kTrace) g_trace.pop_front();

    if (advanced) {
        // tr: entity id, source index, entity id, source index...
        g_ran.clear();
        const auto &tr = j["tr"].items;
        for (size_t n = 0; n + 1 < tr.size(); n += 2) g_ran[tr[n].i()].push_back(tr[n + 1].i());
        for (const auto &e : g_ents) {
            auto &rows = g_history[e.id];
            rows.push_back({g_tick, e.st, e.anim, e.elem, e.etime, e.hitpause, e.life, e.ctrl, changed[e.id],
                            contact[e.id], e.mvt, e.stt, e.x, e.y, e.vx, e.vy, e.stateSrc, g_ran[e.id]});
            while (rows.size() > kHistory) rows.pop_front();
        }
        g_selTick = -1;
    }

    for (const auto &bk : j["bk"].items)
        for (auto &b : g_breakpoints)
            if (b.bid == bk["bid"].i()) b.hits = bk["hits"].i();

    if (g_selId < 0 && !g_ents.empty()) g_selId = g_ents[0].id;
}

void OnLine(const Bridge::Line &line) {
    const std::string &text = line.text;
    if (line.err) {
        Log(LogLine::Engine, text);
        return;
    }
    size_t space = text.find(' ');
    std::string tag = text.substr(0, space);
    if (tag.empty() || tag[0] != '@' || space == std::string::npos) {
        Log(LogLine::Engine, text);
        return;
    }
    Json j;
    if (!Json::Parse(text.c_str() + space + 1, j)) {
        Log(LogLine::Error, "Unreadable line: " + text.substr(0, 200));
        return;
    }
    if (tag == "@IKS") {
        OnSnapshot(j);
    } else if (tag == "@IKHELLO") {
        g_hello = true;
        g_watchId = -1;
        g_modVersion = j["v"].str();
        g_ikemenVersion = j["ik"].str();
        g_canTrace = j["trace"].truthy();
        g_canSet = j["set"].truthy();
        Log(LogLine::Info, "Bridge connected: ikdbg " + g_modVersion + ", Ikemen GO " + g_ikemenVersion +
                               (g_canTrace ? " (with the debugger patch)" : " (no controller trace: unpatched Ikemen GO)"));
        for (const auto &b : g_breakpoints) SendBreakpoint(b);
    } else if (tag == "@IKSRC") {
        size_t from = (size_t)std::max(1, j["from"].i());
        const auto &items = j["items"].items;
        if (g_sources.size() < from + items.size()) g_sources.resize(from + items.size());
        for (size_t n = 0; n < items.size(); n++) {
            Source &s = g_sources[from + n];
            s.file = items[n]["file"].str();
            s.line = items[n]["line"].i();
            s.type = items[n]["type"].str();
            s.state = items[n]["state"].i();
        }
        for (const auto &b : g_ctrlBreaks)
            if (b.enabled) SendCtrlBreak(b, true, from);
    } else if (tag == "@IKRES") {
        bool ok = j["ok"].truthy();
        std::string r = j["r"].str();
        if (!r.empty() || !ok) Log(ok ? LogLine::Result : LogLine::Error, (ok ? "= " : "error: ") + r);
    } else if (tag == "@IKBRK") {
        char buf[512];
        int src = j["src"].i();
        if (j["kind"].str() == "ctrl" && src > 0 && src < (int)g_sources.size()) {
            const Source &s = g_sources[src];
            snprintf(buf, sizeof(buf), "Breakpoint at %s:%d (%s) hit at tick %d: %s in state %d", s.file.c_str(),
                     s.line, s.type.c_str(), j["t"].i(), Who(j["pn"].i(), false, j["id"].i()).c_str(), j["st"].i());
            if (CtrlBreak *b = FindCtrlBreak(s.file, s.line)) b->hits++;
            g_codeFile = s.file;
            g_codeLine = g_codeScrollTo = s.line;
            g_codeHold = true;
        } else {
            snprintf(buf, sizeof(buf), "Breakpoint %d hit at tick %d: %s in state %d  [%s]", j["bid"].i(), j["t"].i(),
                     Who(j["pn"].i(), false, j["id"].i()).c_str(), j["st"].i(), j["cond"].str().c_str());
        }
        g_breakText = buf;
        g_selId = j["id"].i();
        Log(LogLine::Break, buf);
    }
}

// ---------------------------------------------------------------------------
// Windows
// ---------------------------------------------------------------------------

void HelpMarker(const char *text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void SessionWindow() {
    if (!ImGui::Begin("Session")) {
        ImGui::End();
        return;
    }
    Settings &s = g_settings;
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##exe", "Ikemen_GO.exe", &s.exe);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
    ImGui::InputText("P1", &s.p1);
    ImGui::SameLine();
    ImGui::Checkbox("AI##1", &s.ai1);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
    ImGui::InputText("P2", &s.p2);
    ImGui::SameLine();
    ImGui::Checkbox("AI##2", &s.ai2);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
    ImGui::InputText("Stage", &s.stage);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
    ImGui::InputText("Extra", &s.extra);
    HelpMarker("Leave P1 or P2 empty to start at the title screen. Any other Ikemen GO command line flags go "
               "in Extra.");

    bool running = g_bridge.Running();
    if (ImGui::Button(running ? "Restart" : "Launch")) Launch();
    ImGui::SameLine();
    ImGui::BeginDisabled(!running);
    if (ImGui::Button("Stop")) {
        g_bridge.Stop();
        Log(LogLine::Info, "Stopped the game.");
    }
    ImGui::EndDisabled();

    ImGui::Separator();
    if (running) {
        ImGui::Text("pid %lu", g_bridge.Pid());
        ImGui::SameLine();
        if (g_hello)
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1), "bridge connected");
        else
            ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1), "waiting for the mod...");
        ImGui::TextDisabled("%s", g_ikemenVersion.c_str());
        ImGui::Text("%u snapshots, %u/s", g_snapshots, g_lastRate);
    } else {
        ImGui::TextDisabled("not running");
    }
    ImGui::End();
}

void ControlWindow() {
    if (!ImGui::Begin("Control")) {
        ImGui::End();
        return;
    }
    bool live = Connected();
    ImGui::BeginDisabled(!live);
    if (ImGui::Button(g_paused ? "Continue (F5)" : "Pause (F5)")) SendRaw(g_paused ? "ikdbg.pause(false)" : "ikdbg.pause(true)");
    ImGui::SameLine();
    if (ImGui::Button("Step (F10)")) SendRaw("ikdbg.step(1)");
    ImGui::SameLine();
    if (ImGui::Button("Step 10 (F11)")) SendRaw("ikdbg.step(10)");
    ImGui::SameLine();
    static int stepN = 60;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5);
    ImGui::InputInt("##n", &stepN, 0);
    ImGui::SameLine();
    if (ImGui::Button("Run N ticks")) SendRaw("ikdbg.step(" + std::to_string(std::max(1, stepN)) + ")");

    ImGui::TextUnformatted("Speed");
    for (float speed : {0.25f, 0.5f, 1.0f, 2.0f, 4.0f}) {
        ImGui::SameLine();
        char label[16];
        snprintf(label, sizeof(label), "%gx", speed);
        if (ImGui::SmallButton(label)) Exec("setAccel(" + std::to_string(speed) + ")");
    }
    ImGui::SameLine(0, ImGui::GetFontSize() * 2);
    if (ImGui::SmallButton("Save state")) Exec("saveState()");
    ImGui::SameLine();
    if (ImGui::SmallButton("Load state")) Exec("loadState()");
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset round")) Exec("resetRound()");
    ImGui::SameLine();
    if (ImGui::SmallButton("Reload match")) Exec("reload()");
    HelpMarker("Reload match reads the characters, stage and fight screen from disk again: edit a file, "
               "then reload.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Collision boxes")) Exec("toggleClsnDisplay()");
    ImGui::SameLine();
    if (ImGui::SmallButton("Debug display")) Exec("toggleDebugDisplay()");
    ImGui::EndDisabled();

    ImGui::Separator();
    if (!live) {
        ImGui::TextDisabled("Launch the game from the Session window.");
    } else {
        const char *state = g_steps > 0 ? "STEPPING" : g_paused ? "PAUSED" : "RUNNING";
        ImVec4 color = g_paused ? ImVec4(1, 0.75f, 0.3f, 1) : ImVec4(0.4f, 0.9f, 0.4f, 1);
        ImGui::TextColored(color, "%s", state);
        ImGui::SameLine();
        ImGui::Text("tick %d   round %d   round state %d   %d players/helpers", g_tick, g_roundNo, g_roundState,
                    (int)g_ents.size());
        if (!g_breakText.empty()) {
            ImGui::TextColored(ImVec4(1, 0.45f, 0.45f, 1), "%s", g_breakText.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) g_breakText.clear();
        }
    }
    ImGui::End();

    ImGuiIO &io = ImGui::GetIO();
    if (live && !io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) SendRaw(g_paused ? "ikdbg.pause(false)" : "ikdbg.pause(true)");
        if (ImGui::IsKeyPressed(ImGuiKey_F10)) SendRaw("ikdbg.step(1)");
        if (ImGui::IsKeyPressed(ImGuiKey_F11)) SendRaw("ikdbg.step(10)");
    }
}

void PlayersWindow() {
    if (!ImGui::Begin("Players")) {
        ImGui::End();
        return;
    }
    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("players", 13, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        float em = ImGui::GetFontSize();
        ImGui::TableSetupColumn("Who", ImGuiTableColumnFlags_WidthFixed, em * 7);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, em * 8);
        for (const char *h : {"State", "Prev", "Anim", "Elem", "Time", "Type", "Ctrl", "Pos", "Vel", "Life", "Power"})
            ImGui::TableSetupColumn(h);
        ImGui::TableHeadersRow();
        for (const auto &e : g_ents) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(e.id);
            std::string who = Who(e.pn, e.helper, e.id);
            if (ImGui::Selectable(who.c_str(), g_selId == e.id, ImGuiSelectableFlags_SpanAllColumns))
                g_selId = e.id;
            ImGui::PopID();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(e.name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%d", e.st);
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%d", e.pst);
            ImGui::TableNextColumn();
            ImGui::Text("%d", e.anim);
            ImGui::TableNextColumn();
            ImGui::Text("%d/%d", e.elem, e.elemn);
            ImGui::TableNextColumn();
            ImGui::Text("%d", e.time);
            ImGui::TableNextColumn();
            ImGui::Text("%s %s %s", e.stt.c_str(), e.mvt.c_str(), e.phy.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(e.ctrl ? "yes" : "-");
            ImGui::TableNextColumn();
            ImGui::Text("%.1f, %.1f", e.x, e.y);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f, %.2f", e.vx, e.vy);
            ImGui::TableNextColumn();
            ImGui::Text("%d/%d", e.life, e.lifemax);
            ImGui::TableNextColumn();
            ImGui::Text("%d", e.pow);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void Field(const char *label, const char *fmt, ...) {
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", label);
    ImGui::TableNextColumn();
    va_list args;
    va_start(args, fmt);
    ImGui::TextV(fmt, args);
    va_end(args);
}

// var/fvar values; with the debugger patch, a double click edits one (setter is setVar or setFvar).
void VarGrid(const char *id, const std::vector<double> &vals, const std::vector<double> *prev, bool floats,
             int entity, const char *setter) {
    static int editIndex;
    static double editValue;
    int cols = std::max(1, (int)(ImGui::GetContentRegionAvail().x / (ImGui::GetFontSize() * (floats ? 8 : 6))));
    if (!ImGui::BeginTable(id, cols, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) return;
    bool open = false;
    for (size_t n = 0; n < vals.size(); n++) {
        ImGui::TableNextColumn();
        bool changed = prev && n < prev->size() && (*prev)[n] != vals[n];
        bool zero = vals[n] == 0;
        ImVec4 color = changed ? ImVec4(1, 0.8f, 0.3f, 1)
                     : zero    ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)
                               : ImGui::GetStyleColorVec4(ImGuiCol_Text);
        char label[48];
        if (floats)
            snprintf(label, sizeof(label), "%2zu: %.3f##%zu", n, vals[n], n);
        else
            snprintf(label, sizeof(label), "%2zu: %.0f##%zu", n, vals[n], n);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::Selectable(label, false, ImGuiSelectableFlags_AllowDoubleClick);
        if (g_canSet && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            editIndex = (int)n;
            editValue = vals[n];
            open = true;
        }
        ImGui::PopStyleColor();
    }
    ImGui::EndTable();
    if (open) ImGui::OpenPopup(id); // outside the table, which has its own ID scope
    if (ImGui::BeginPopup(id)) {
        ImGui::Text("%s(%d)", floats ? "fvar" : "var", editIndex);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        bool set = ImGui::InputDouble("##value", &editValue, 0, 0, floats ? "%.4f" : "%.0f",
                                      ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        set |= ImGui::Button("Set");
        if (set) {
            char lua[96];
            snprintf(lua, sizeof(lua), "%s(%d, %.6g)", setter, editIndex, editValue);
            ExecOn(entity, lua);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void InspectorWindow() {
    if (!ImGui::Begin("Inspector")) {
        ImGui::End();
        return;
    }
    const Entity *e = FindEntity(g_selId);
    if (!e) {
        ImGui::TextDisabled(g_selId < 0 ? "Pick a player in the Players window." : "The selected entity is gone.");
        ImGui::End();
        return;
    }
    if (Connected()) Watch(e->id);
    ImGui::Text("%s  %s", Who(e->pn, e->helper, e->id).c_str(), e->name.c_str());
    if (ImGui::BeginTable("fields", 2, ImGuiTableFlags_SizingFixedFit)) {
        Field("State", "%d (prev %d)", e->st, e->pst);
        Field("Time", "%d", e->time);
        Field("Anim", "%d", e->anim);
        Field("Elem", "%d of %d, %d ticks in", e->elem, e->elemn, e->etime);
        Field("AnimTime", "%d", e->atime);
        Field("Types", "%s / %s / %s", e->stt.c_str(), e->mvt.c_str(), e->phy.c_str());
        Field("Ctrl", "%s", e->ctrl ? "yes" : "no");
        Field("Facing", "%s", e->facing < 0 ? "left" : "right");
        Field("Pos", "%.2f, %.2f", e->x, e->y);
        Field("Vel", "%.3f, %.3f", e->vx, e->vy);
        Field("Life", "%d / %d", e->life, e->lifemax);
        Field("Power", "%d / %d", e->pow, e->powmax);
        Field("HitPause", "%d", e->hitpause);
        Field("MoveHit", "%d (contact %d, guarded %d)", e->moveHit, e->moveContact, e->moveGuarded);
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Actions");
    bool live = Connected();
    ImGui::BeginDisabled(!live);
    static int stateNo = 0, animNo = 0, life = 1000;
    float w = ImGui::GetFontSize() * 6;
    ImGui::SetNextItemWidth(w);
    ImGui::InputInt("##state", &stateNo, 0);
    ImGui::SameLine();
    if (ImGui::Button("SelfState")) ExecOn(e->id, "selfState(" + std::to_string(stateNo) + ")");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(w);
    ImGui::InputInt("##anim", &animNo, 0);
    ImGui::SameLine();
    if (ImGui::Button("ChangeAnim")) ExecOn(e->id, "changeAnim(" + std::to_string(animNo) + ")");
    ImGui::SetNextItemWidth(w);
    ImGui::InputInt("##life", &life, 0);
    ImGui::SameLine();
    if (ImGui::Button("Set life")) ExecOn(e->id, "setLife(" + std::to_string(life) + ")");
    ImGui::SameLine();
    if (ImGui::Button("Full life/power")) ExecOn(e->id, "setLife(lifeMax()); setPower(powerMax())");
    ImGui::SameLine();
    if (ImGui::Button("Stand")) ExecOn(e->id, "selfState(0)");
    if (!e->helper) {
        ImGui::SameLine();
        if (ImGui::Button("Toggle AI"))
            ExecOn(e->id, "if aiLevel() > 0 then setAILevel(0) return 'AI off' else setAILevel(8) return 'AI on' end");
    }
    if (g_canSet) {
        static float pos[2], vel[2];
        ImGui::SetNextItemWidth(w * 2);
        ImGui::InputFloat2("##pos", pos, "%.1f");
        ImGui::SameLine();
        if (ImGui::Button("Set pos")) {
            char lua[96];
            snprintf(lua, sizeof(lua), "setPos(%g, %g)", pos[0], pos[1]);
            ExecOn(e->id, lua);
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(w * 2);
        ImGui::InputFloat2("##vel", vel, "%.2f");
        ImGui::SameLine();
        if (ImGui::Button("Set vel")) {
            char lua[96];
            snprintf(lua, sizeof(lua), "setVel(%g, %g)", vel[0], vel[1]);
            ExecOn(e->id, lua);
        }
        if (!ImGui::IsAnyItemActive()) {
            pos[0] = (float)e->x, pos[1] = (float)e->y;
            vel[0] = (float)e->vx, vel[1] = (float)e->vy;
        }
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("var");
    if (e->vars.empty())
        ImGui::TextDisabled("waiting for the variables...");
    else {
        auto prev = g_prevVars.find(e->id);
        VarGrid("vars", e->vars, prev == g_prevVars.end() ? nullptr : &prev->second, false, e->id, "setVar");
    }
    ImGui::SeparatorText("fvar");
    if (!e->fvars.empty()) {
        auto prev = g_prevFvars.find(e->id);
        VarGrid("fvars", e->fvars, prev == g_prevFvars.end() ? nullptr : &prev->second, true, e->id, "setFvar");
    }
    ImGui::TextDisabled(g_canSet ? "Yellow: changed on the last tick. Double-click a value to change it."
                                 : "Values that changed on the last tick are yellow.");
    ImGui::End();
}

void FrameDataWindow() {
    if (!ImGui::Begin("Frame data")) {
        ImGui::End();
        return;
    }
    auto it = g_history.find(g_selId);
    if (it == g_history.end() || it->second.empty()) {
        ImGui::TextDisabled("Tick by tick history of the selected player appears here.");
        ImGui::End();
        return;
    }
    static bool onlyChanges = false;
    ImGui::Checkbox("Only ticks with a state change or a hit", &onlyChanges);
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) it->second.clear();
    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable;
    if (g_canTrace) {
        ImGui::SameLine();
        ImGui::TextDisabled("Click a tick to see its controllers in Code.");
    }
    if (ImGui::BeginTable("frames", 12, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        for (const char *h : {"Tick", "State", "Anim", "Elem", "Elem time", "Type", "Ctrl", "HitPause", "Life",
                              "Pos", "Vel", "Controllers"})
            ImGui::TableSetupColumn(h);
        ImGui::TableHeadersRow();
        const auto &rows = it->second;
        for (auto r = rows.rbegin(); r != rows.rend(); ++r) {
            if (onlyChanges && !r->stateChanged && !r->contact) continue;
            ImGui::TableNextRow();
            if (r->contact)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(150, 50, 50, 90));
            else if (r->stateChanged)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(60, 90, 160, 90));
            ImGui::TableNextColumn();
            char tick[16];
            snprintf(tick, sizeof(tick), "%d", r->t);
            bool picked = g_selTick == r->t || (g_selTick < 0 && r == rows.rbegin());
            if (ImGui::Selectable(tick, picked, ImGuiSelectableFlags_SpanAllColumns)) g_selTick = r->t;
            ImGui::TableNextColumn();
            ImGui::Text("%d", r->st);
            ImGui::TableNextColumn();
            ImGui::Text("%d", r->anim);
            ImGui::TableNextColumn();
            ImGui::Text("%d", r->elem);
            ImGui::TableNextColumn();
            ImGui::Text("%d", r->etime);
            ImGui::TableNextColumn();
            ImGui::Text("%s %s", r->stt.c_str(), r->mvt.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r->ctrl ? "yes" : "-");
            ImGui::TableNextColumn();
            ImGui::Text("%d", r->hitpause);
            ImGui::TableNextColumn();
            ImGui::Text("%d", r->life);
            ImGui::TableNextColumn();
            ImGui::Text("%.1f, %.1f", r->x, r->y);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f, %.2f", r->vx, r->vy);
            ImGui::TableNextColumn();
            if (g_canTrace) ImGui::Text("%zu", r->ran.size());
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Code: the source of the selected entity's state, and the controllers that ran
// ---------------------------------------------------------------------------

std::string IkemenDir() {
    const std::string &exe = g_settings.exe;
    size_t slash = exe.find_last_of("\\/");
    return slash == std::string::npos ? "." : exe.substr(0, slash);
}

const SourceFile &LoadSource(const std::string &file) {
    std::string key = NormPath(file);
    auto it = g_files.find(key);
    if (it != g_files.end()) return it->second;
    SourceFile sf;
    bool absolute = file.size() > 1 && (file[1] == ':' || file[0] == '\\' || file[0] == '/');
    std::string text = ReadWholeFile(absolute ? file : IkemenDir() + "\\" + file);
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB)
        text.erase(0, 3);
    sf.missing = text.empty();
    std::string line;
    for (char ch : text) {
        if (ch == '\n') {
            sf.lines.push_back(line);
            line.clear();
        } else if (ch == '\t') {
            line += "    ";
        } else if (ch != '\r') {
            line += ch;
        }
    }
    sf.lines.push_back(line);
    return g_files[key] = std::move(sf);
}

// Lines of each file that hold a state controller (rebuilt when sources arrive).
const std::map<int, std::vector<int>> &ControllerLines(const std::string &file) {
    static std::map<std::string, std::map<int, std::vector<int>>> byFile;
    static size_t built;
    if (built != g_sources.size()) {
        byFile.clear();
        for (size_t i = 1; i < g_sources.size(); i++)
            if (g_sources[i].type != "statedef") byFile[NormPath(g_sources[i].file)][g_sources[i].line].push_back((int)i);
        built = g_sources.size();
    }
    static const std::map<int, std::vector<int>> none;
    auto it = byFile.find(NormPath(file));
    return it == byFile.end() ? none : it->second;
}

void JumpTo(int src) {
    if (src <= 0 || src >= (int)g_sources.size()) return;
    g_codeFile = g_sources[src].file;
    g_codeLine = g_codeScrollTo = g_sources[src].line;
}

void CodeWindow() {
    if (!ImGui::Begin("Code")) {
        ImGui::End();
        return;
    }
    if (!g_canTrace) {
        ImGui::TextWrapped("The Code window needs an Ikemen GO built with the debugger patch "
                           "(src/script_debugger.go): it tells where each state controller comes from and "
                           "which ones run on every tick.");
        ImGui::End();
        return;
    }
    const Entity *e = FindEntity(g_selId);
    const FrameRow *row = nullptr;
    auto hist = g_history.find(g_selId);
    if (hist != g_history.end() && !hist->second.empty()) {
        row = &hist->second.back();
        if (g_selTick >= 0)
            for (const auto &r : hist->second)
                if (r.t == g_selTick) row = &r;
    }

    static bool follow = true;
    static int followedSrc;
    int stateSrc = row ? row->stateSrc : e ? e->stateSrc : 0;
    if (g_codeHold) {
        followedSrc = stateSrc;
        g_codeHold = false;
    }
    if (follow && stateSrc != followedSrc) {
        followedSrc = stateSrc;
        JumpTo(stateSrc);
    }
    if (e) {
        ImGui::Text("%s  state %d", Who(e->pn, e->helper, e->id).c_str(), row ? row->st : e->st);
        ImGui::SameLine();
    }
    if (row) {
        ImGui::TextDisabled("tick %d%s", row->t, g_selTick >= 0 ? " (picked in Frame data)" : "");
        ImGui::SameLine();
    }
    ImGui::Checkbox("Follow the state", &follow);
    ImGui::SameLine();
    if (ImGui::SmallButton("Statedef")) JumpTo(stateSrc);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reload files")) g_files.clear();
    if (!g_codeFile.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s:%d", g_codeFile.c_str(), g_codeLine);
    }

    // Left: the controllers that ran, in order. Right: the file.
    float listWidth = ImGui::GetContentRegionAvail().x * 0.38f;
    std::map<int, bool> ranLines;
    std::string codeNorm = NormPath(g_codeFile);
    if (ImGui::BeginChild("ran", ImVec2(listWidth, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX)) {
        ImGui::TextDisabled("%zu controllers ran on this tick, in order", row ? row->ran.size() : 0);
        if (row && ImGui::BeginTable("ranlist", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                                      ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("State");
            ImGui::TableSetupColumn("Controller");
            ImGui::TableSetupColumn("Where", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            ImGuiListClipper clipper;
            clipper.Begin((int)row->ran.size());
            while (clipper.Step()) {
                for (int n = clipper.DisplayStart; n < clipper.DisplayEnd; n++) {
                    int src = row->ran[n];
                    ImGui::TableNextRow();
                    if (src <= 0 || src >= (int)g_sources.size()) {
                        // The source has not arrived yet (they come a few hundred per frame)
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("?");
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("source %d", src);
                        continue;
                    }
                    const Source &s = g_sources[src];
                    const char *name = s.file.c_str() + s.file.find_last_of("\\/") + 1;
                    ImGui::TableNextColumn();
                    char label[32];
                    snprintf(label, sizeof(label), "%d##%d", s.state, n);
                    bool here = NormPath(s.file) == codeNorm && s.line == g_codeLine;
                    if (ImGui::Selectable(label, here, ImGuiSelectableFlags_SpanAllColumns)) JumpTo(src);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(s.type.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%s:%d", name, s.line);
                }
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
    if (row)
        for (int src : row->ran)
            if (src > 0 && src < (int)g_sources.size() && NormPath(g_sources[src].file) == codeNorm)
                ranLines[g_sources[src].line] = true;
    ImGui::SameLine();

    if (ImGui::BeginChild("file", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
        if (g_codeFile.empty()) {
            ImGui::TextDisabled("Pick a controller on the left.");
        } else {
            const SourceFile &sf = LoadSource(g_codeFile);
            if (sf.missing) ImGui::TextDisabled("Could not read %s", g_codeFile.c_str());
            const auto &ctrlLines = ControllerLines(g_codeFile);
            float lineH = ImGui::GetTextLineHeightWithSpacing();
            if (g_codeScrollTo > 0) {
                ImGui::SetScrollY(std::max(0.0f, (g_codeScrollTo - 8) * lineH));
                g_codeScrollTo = 0;
            }
            ImDrawList *draw = ImGui::GetWindowDrawList();
            float gutter = ImGui::GetFontSize() * 1.2f;
            ImGuiListClipper clipper;
            clipper.Begin((int)sf.lines.size(), lineH);
            while (clipper.Step()) {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                    int lineNo = i + 1;
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    float width = std::max(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 80);
                    if (ranLines.count(lineNo))
                        draw->AddRectFilled(p, ImVec2(p.x + width, p.y + lineH), IM_COL32(40, 130, 60, 110));
                    if (lineNo == g_codeLine)
                        draw->AddRectFilled(p, ImVec2(p.x + width, p.y + lineH), IM_COL32(200, 170, 40, 70));
                    bool hasCtrl = ctrlLines.count(lineNo) != 0;
                    CtrlBreak *bp = FindCtrlBreak(g_codeFile, lineNo);
                    ImVec2 c(p.x + gutter * 0.5f, p.y + lineH * 0.5f);
                    if (bp)
                        draw->AddCircleFilled(c, lineH * 0.32f,
                                              bp->enabled ? IM_COL32(220, 60, 60, 255) : IM_COL32(140, 90, 90, 255));
                    else if (hasCtrl)
                        draw->AddCircleFilled(c, lineH * 0.12f, IM_COL32(150, 150, 150, 200));
                    ImGui::PushID(i);
                    if (ImGui::InvisibleButton("gutter", ImVec2(gutter, ImGui::GetTextLineHeight())) && hasCtrl)
                        ToggleCtrlBreak(g_codeFile, lineNo);
                    if (hasCtrl && ImGui::IsItemHovered()) ImGui::SetTooltip("Breakpoint on this controller");
                    ImGui::PopID();
                    ImGui::SameLine(0, 0);
                    ImGui::TextDisabled("%5d ", lineNo);
                    ImGui::SameLine(0, 0);
                    ImGui::TextUnformatted(sf.lines[i].c_str());
                }
            }
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

void TraceWindow() {
    if (!ImGui::Begin("Trace")) {
        ImGui::End();
        return;
    }
    static bool showStates = true, showHits = true, follow = true;
    static int player = 0;
    ImGui::Checkbox("State changes", &showStates);
    ImGui::SameLine();
    ImGui::Checkbox("Hits", &showHits);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6);
    ImGui::Combo("Player", &player, "All\0P1\0P2\0P3\0P4\0");
    ImGui::SameLine();
    ImGui::Checkbox("Follow", &follow);
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) g_trace.clear();

    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("trace", 3, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Tick");
        ImGui::TableSetupColumn("Who", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 8);
        ImGui::TableSetupColumn("Event", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (size_t n = 0; n < g_trace.size(); n++) {
            const TraceEvent &t = g_trace[n];
            bool isState = t.kind == "st";
            if ((isState && !showStates) || (!isState && !showHits)) continue;
            if (player > 0 && t.pn != player) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%d", t.t);
            ImGui::TableNextColumn();
            ImGui::PushID((int)n);
            if (ImGui::Selectable(Who(t.pn, t.helper, t.id).c_str(), false, ImGuiSelectableFlags_SpanAllColumns))
                g_selId = t.id;
            ImGui::PopID();
            ImGui::TableNextColumn();
            if (isState)
                ImGui::Text("state %d -> %d", t.from, t.to);
            else
                ImGui::TextColored(t.kind == "hit" ? ImVec4(1, 0.5f, 0.5f, 1) : ImVec4(0.6f, 0.8f, 1, 1),
                                   "%s (in state %d)", t.kind == "hit" ? "hit" : "guarded", t.st);
        }
        if (follow) ImGui::SetScrollHereY(1.0f);
        ImGui::EndTable();
    }
    ImGui::End();
}

void BreakpointsWindow() {
    if (!ImGui::Begin("Breakpoints")) {
        ImGui::End();
        return;
    }
    static int target = 0;
    static std::string cond = "stateNo() == 200";
    const char *targets[] = {"Any", "P1", "P1 + helpers", "P2", "P2 + helpers"};
    const int targetValues[] = {0, 1, -1, 2, -2};
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
    ImGui::Combo("##target", &target, targets, IM_ARRAYSIZE(targets));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-ImGui::GetFontSize() * 4);
    bool add = ImGui::InputText("##cond", &cond, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    add |= ImGui::Button("Add");
    if (add && !cond.empty()) {
        Breakpoint b{g_nextBid++, targetValues[target], cond};
        g_breakpoints.push_back(b);
        if (Connected()) SendBreakpoint(b);
    }
    ImGui::TextDisabled("A Lua expression over Ikemen's triggers, checked after every tick:");
    ImGui::TextDisabled("  stateNo() == 200   moveType() == \"H\"   life() < 300   var(5) ~= 0");
    ImGui::TextDisabled("  anim() == 210 and animElemNo(0) == 3   moveHit() == 1");

    if (ImGui::BeginTable("bps", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("On");
        ImGui::TableSetupColumn("Who");
        ImGui::TableSetupColumn("Condition", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Hits");
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        for (size_t n = 0; n < g_breakpoints.size(); n++) {
            Breakpoint &b = g_breakpoints[n];
            ImGui::PushID(b.bid);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Checkbox("##on", &b.enabled) && Connected()) SendBreakpoint(b);
            ImGui::TableNextColumn();
            for (int k = 0; k < IM_ARRAYSIZE(targetValues); k++)
                if (targetValues[k] == b.target) ImGui::TextUnformatted(targets[k]);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(b.cond.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%d", b.hits);
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Delete")) {
                if (Connected()) SendRaw("ikdbg.brkDel(" + std::to_string(b.bid) + ")");
                g_breakpoints.erase(g_breakpoints.begin() + n);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::SeparatorText("On state controllers");
    if (g_ctrlBreaks.empty())
        ImGui::TextDisabled(g_canTrace || !Connected() ? "Click left of a controller in the Code window."
                                                       : "Needs an Ikemen GO with the debugger patch.");
    if (!g_ctrlBreaks.empty() && ImGui::BeginTable("ctrlbps", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("On");
        ImGui::TableSetupColumn("Where", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Hits");
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        for (size_t n = 0; n < g_ctrlBreaks.size(); n++) {
            CtrlBreak &b = g_ctrlBreaks[n];
            ImGui::PushID((int)n);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Checkbox("##on", &b.enabled) && Connected()) SendCtrlBreak(b, b.enabled);
            ImGui::TableNextColumn();
            char where[300];
            snprintf(where, sizeof(where), "%s:%d", b.file.c_str(), b.line);
            if (ImGui::Selectable(where)) {
                g_codeFile = b.file;
                g_codeLine = g_codeScrollTo = b.line;
            }
            ImGui::TableNextColumn();
            ImGui::Text("%d", b.hits);
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Delete")) {
                if (Connected()) SendCtrlBreak(b, false);
                g_ctrlBreaks.erase(g_ctrlBreaks.begin() + n);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void ConsoleWindow() {
    if (!ImGui::Begin("Console")) {
        ImGui::End();
        return;
    }
    static bool showEngine = true;
    static std::string input;
    static std::vector<std::string> history;
    static int historyPos = -1;

    ImGui::Checkbox("Engine log", &showEngine);
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) g_log.clear();
    float footer = ImGui::GetFrameHeightWithSpacing();
    if (ImGui::BeginChild("log", ImVec2(0, -footer), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
        for (const auto &l : g_log) {
            if (l.kind == LogLine::Engine && !showEngine) continue;
            ImVec4 color = l.kind == LogLine::Error    ? ImVec4(1, 0.45f, 0.45f, 1)
                         : l.kind == LogLine::Result   ? ImVec4(0.6f, 0.9f, 0.6f, 1)
                         : l.kind == LogLine::Engine   ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)
                         : l.kind == LogLine::Break    ? ImVec4(1, 0.75f, 0.3f, 1)
                                                       : ImGui::GetStyleColorVec4(ImGuiCol_Text);
            ImGui::TextColored(color, "%s", l.text.c_str());
        }
        if (g_logScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - ImGui::GetFontSize() * 4)
            ImGui::SetScrollHereY(1.0f);
        g_logScroll = false;
    }
    ImGui::EndChild();

    auto callback = [](ImGuiInputTextCallbackData *data) -> int {
        if (data->EventFlag != ImGuiInputTextFlags_CallbackHistory || history.empty()) return 0;
        if (data->EventKey == ImGuiKey_UpArrow)
            historyPos = historyPos < 0 ? (int)history.size() - 1 : std::max(0, historyPos - 1);
        else if (historyPos >= 0 && ++historyPos >= (int)history.size())
            historyPos = -1;
        data->DeleteChars(0, data->BufTextLen);
        if (historyPos >= 0) data->InsertChars(0, history[historyPos].c_str());
        return 0;
    };
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##lua", "Lua, e.g.  player(2); return stateNo(), var(3)", &input,
                                 ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory,
                                 callback)) {
        if (!input.empty()) {
            Log(LogLine::Info, "> " + input);
            Exec(input);
            history.push_back(input);
            historyPos = -1;
            input.clear();
        }
        ImGui::SetKeyboardFocusHere(-1);
    }
    ImGui::End();
}

void DefaultLayout(ImGuiID dock) {
    ImGui::DockBuilderRemoveNode(dock);
    ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dock, ImGui::GetMainViewport()->WorkSize);
    ImGuiID left, rest, right, center, top, bottom, control, players, leftTop, leftBottom;
    ImGui::DockBuilderSplitNode(dock, ImGuiDir_Left, 0.19f, &left, &rest);
    ImGui::DockBuilderSplitNode(rest, ImGuiDir_Right, 0.3f, &right, &center);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.62f, &bottom, &top);
    ImGui::DockBuilderSplitNode(top, ImGuiDir_Up, 0.38f, &control, &players);
    ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.45f, &leftTop, &leftBottom);
    ImGui::DockBuilderDockWindow("Session", leftTop);
    ImGui::DockBuilderDockWindow("Breakpoints", leftBottom);
    ImGui::DockBuilderDockWindow("Control", control);
    ImGui::DockBuilderDockWindow("Players", players);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderDockWindow("Code", bottom);
    ImGui::DockBuilderDockWindow("Frame data", bottom);
    ImGui::DockBuilderDockWindow("Trace", bottom);
    ImGui::DockBuilderDockWindow("Console", bottom);
    ImGui::DockBuilderFinish(dock);
}

// Settings live in the ImGui ini, in an [IkemenDebugger][Settings] section.
void *SettingsOpen(ImGuiContext *, ImGuiSettingsHandler *, const char *) { return &g_settings; }

void SettingsLine(ImGuiContext *, ImGuiSettingsHandler *, void *, const char *line) {
    const char *eq = strchr(line, '=');
    if (!eq) return;
    std::string key(line, eq), value(eq + 1);
    Settings &s = g_settings;
    if (key == "Exe") s.exe = value;
    else if (key == "P1") s.p1 = value;
    else if (key == "P2") s.p2 = value;
    else if (key == "Stage") s.stage = value;
    else if (key == "Extra") s.extra = value;
    else if (key == "AI1") s.ai1 = value == "1";
    else if (key == "AI2") s.ai2 = value == "1";
    else if (key == "Breakpoint") {
        // target|enabled|condition
        int t = 0, on = 1, used = 0;
        if (sscanf(value.c_str(), "%d|%d|%n", &t, &on, &used) == 2 && used > 0)
            g_breakpoints.push_back({g_nextBid++, t, value.substr(used), on != 0});
    } else if (key == "CtrlBreak") {
        // enabled|line|file
        int on = 1, lineNo = 0, used = 0;
        if (sscanf(value.c_str(), "%d|%d|%n", &on, &lineNo, &used) == 2 && used > 0)
            g_ctrlBreaks.push_back({value.substr(used), lineNo, on != 0});
    }
}

void SettingsWrite(ImGuiContext *, ImGuiSettingsHandler *handler, ImGuiTextBuffer *buf) {
    const Settings &s = g_settings;
    buf->appendf("[%s][Settings]\n", handler->TypeName);
    buf->appendf("Exe=%s\nP1=%s\nP2=%s\nStage=%s\nExtra=%s\nAI1=%d\nAI2=%d\n", s.exe.c_str(), s.p1.c_str(),
                 s.p2.c_str(), s.stage.c_str(), s.extra.c_str(), s.ai1, s.ai2);
    for (const auto &b : g_breakpoints) buf->appendf("Breakpoint=%d|%d|%s\n", b.target, b.enabled, b.cond.c_str());
    for (const auto &b : g_ctrlBreaks) buf->appendf("CtrlBreak=%d|%d|%s\n", b.enabled, b.line, b.file.c_str());
    buf->append("\n");
}

} // namespace

void App_Init(bool launch) {
    g_autoLaunch = launch;
    g_iniPath = ExeDir() + "\\ikemen-debugger.ini";
    g_settings.exe = FindIkemen(); // the saved settings, read on the first frame, take precedence
    // No window layout saved yet (no ini, or one with only settings): build the default one.
    g_firstRun = ReadWholeFile(g_iniPath).find("[Docking]") == std::string::npos;
    ImGui::GetIO().IniFilename = g_iniPath.c_str();
    ImGuiSettingsHandler handler;
    handler.TypeName = "IkemenDebugger";
    handler.TypeHash = ImHashStr("IkemenDebugger");
    handler.ReadOpenFn = SettingsOpen;
    handler.ReadLineFn = SettingsLine;
    handler.WriteAllFn = SettingsWrite;
    ImGui::AddSettingsHandler(&handler);
    Log(LogLine::Info, "Ikemen GO debugger. Set the path of Ikemen_GO.exe in Session and press Launch.");
}

void App_Poll() {
    std::vector<Bridge::Line> lines;
    g_bridge.Poll(lines);
    for (const auto &l : lines) OnLine(l);
    if (!g_bridge.Running() && g_bridge.Pid() && !g_exitReported) {
        g_exitReported = true;
        g_hello = false;
        char buf[64];
        snprintf(buf, sizeof(buf), "The game exited (code %lu).", g_bridge.ExitCode());
        Log(LogLine::Info, buf);
    }
    double now = ImGui::GetTime();
    if (now - g_rateTime >= 1.0) {
        g_lastRate = g_rateCount;
        g_rateCount = 0;
        g_rateTime = now;
    }
}

void App_Frame() {
    App_Poll();
    ImGuiID dock = ImGui::GetID("IkemenDebuggerDock");
    if (g_firstRun) {
        DefaultLayout(dock);
        g_firstRun = false;
    }
    ImGui::DockSpaceOverViewport(dock);
    SessionWindow();
    ControlWindow();
    PlayersWindow();
    InspectorWindow();
    FrameDataWindow();
    TraceWindow();
    BreakpointsWindow();
    ConsoleWindow();
    CodeWindow();
    if (g_autoLaunch) {
        g_autoLaunch = false;
        Launch();
    }
}

void App_Shutdown() {
    ImGui::SaveIniSettingsToDisk(g_iniPath.c_str());
    g_bridge.Stop();
}
