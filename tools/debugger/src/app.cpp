// The debugger: launches Ikemen GO with the ikdbg.lua mod, reads its
// snapshots and shows them; every action is Lua sent through the game's stdin.
#include "app.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "bridge.h"
#include "air.h"
#include "editor.hpp"
#include "frame.h"
#include <GL/gl.h>
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

// A source file shown and edited in the Code window. The text has '\n' line ends and no BOM;
// saving puts back what the file had.
struct SourceFile {
    std::string path;          // as the game names it (relative to its folder, or absolute)
    std::string text, disk;    // being edited, and as on disk
    unsigned version = 0;      // bumped when text is replaced from outside the editor
    bool crlf = false, bom = false, missing = false;
    FILETIME mtime = {};
    dbg::CodeEditor *ed = nullptr;
    int errorLine = 0;         // of the last Apply that failed
    std::string errorMsg;
    bool Dirty() const { return text != disk; }
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
    bool gameWindow = false; // keep the game's own window shown although its frames are in Game
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

// A player or helper as drawn in the last frame, in the game picture's pixels.
struct FrameEntity {
    int id = 0, pn = 0;
    bool helper = false;
    ImVec2 axis;
    std::vector<ImVec4> c1, c2; // x, y, w, h
    int action = -1, elem = 0;  // the animation shown; elem 1-based
    int animPn = 0;             // the player whose AIR file it comes from
    ImVec2 k;                   // pixels per AIR unit (x has the facing's sign)
};

FrameLink g_frameLink;                   // frames shared by the patched Ikemen
GameFrame g_frame;
GLuint g_frameTex;
int g_texW, g_texH;
ImVec4 g_frameVp;                        // where the game picture is in the frame
ImVec2 g_frameScr;                       // size of the game picture
std::vector<FrameEntity> g_frameEnts;
unsigned g_framesRead;

std::map<int, std::map<std::string, std::vector<std::string>>> g_charFiles; // pn -> [Files] key -> paths
std::map<int, double> g_filesAsked;

const size_t kHistory = 1200, kTrace = 4000, kLog = 4000;

SourceFile &LoadSource(const std::string &file); // Code window
void ReadSource(SourceFile &sf);
bool SaveSource(SourceFile &sf);

FILE *g_logFile; // the log also goes to ikemen-debugger.log next to the exe (read by test scripts)

void Log(LogLine::Kind kind, const std::string &text) {
    if (g_logFile) {
        fprintf(g_logFile, "%s\n", text.c_str());
        fflush(g_logFile);
    }
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

// Lua for the mod. Through the shared memory when the game has it (no limit per frame, menus
// too), else through stdin (one line per frame, matches only).
void SendRaw(const std::string &lua) {
    std::string line = "if ikdbg then " + lua + " end";
    if (g_frameLink.IsOpen() && g_frameLink.SendCommand(line)) return;
    if (!g_bridge.Send(line)) Log(LogLine::Error, "The game is not running.");
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
    g_frameLink.Close();
    g_frameEnts.clear();
    g_charFiles.clear();
    g_filesAsked.clear();
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

void OnReload(const Json &j); // Code window
void OnFiles(const Json &j);  // Animation window

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
    } else if (tag == "@IKRELOAD") {
        OnReload(j);
    } else if (tag == "@IKFILES") {
        OnFiles(j);
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

// ---------------------------------------------------------------------------
// Game: the game's frames, with the players' boxes over them
// ---------------------------------------------------------------------------

void ReadBoxes(const Json &j, std::vector<ImVec4> &out) {
    for (size_t n = 0; n + 3 < j.size(); n += 4)
        out.push_back(ImVec4((float)j[n].num(), (float)j[n + 1].num(), (float)j[n + 2].num(), (float)j[n + 3].num()));
}

// The game's own window: hidden while its frames are shown in Game (unless the settings say
// otherwise). Hidden, not minimized: a minimized window stops drawing.
void UpdateGameWindow() {
    static double lastCheck;
    if (ImGui::GetTime() - lastCheck < 0.5) return;
    lastCheck = ImGui::GetTime();
    struct Find {
        DWORD pid;
        HWND hwnd;
    } find = {g_bridge.Pid(), nullptr};
    EnumWindows(
        [](HWND hwnd, LPARAM param) -> BOOL {
            Find *f = (Find *)param;
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid != f->pid || GetWindow(hwnd, GW_OWNER)) return TRUE;
            char cls[64];
            GetClassNameA(hwnd, cls, sizeof(cls));
            if (strcmp(cls, "SDL_app") != 0) return TRUE;
            f->hwnd = hwnd;
            return FALSE;
        },
        (LPARAM)&find);
    if (!find.hwnd) return;
    bool show = g_settings.gameWindow || !g_frameLink.IsOpen();
    if (show != (IsWindowVisible(find.hwnd) != 0)) ShowWindow(find.hwnd, show ? SW_SHOWNOACTIVATE : SW_HIDE);
}

// SDL key code (SDL_keycode.h) of a key, 0 for those not sent to the game.
int SdlKey(ImGuiKey k) {
    const int sc = 1 << 30; // SDLK_SCANCODE_MASK: keys without a character
    if (k >= ImGuiKey_A && k <= ImGuiKey_Z) return 'a' + (k - ImGuiKey_A);
    if (k >= ImGuiKey_0 && k <= ImGuiKey_9) return '0' + (k - ImGuiKey_0);
    if (k >= ImGuiKey_F1 && k <= ImGuiKey_F12) return sc | (58 + (k - ImGuiKey_F1));
    switch (k) {
    case ImGuiKey_Enter: return 13;
    case ImGuiKey_Escape: return 27;
    case ImGuiKey_Backspace: return 8;
    case ImGuiKey_Tab: return 9;
    case ImGuiKey_Space: return ' ';
    case ImGuiKey_Minus: return '-';
    case ImGuiKey_Equal: return '=';
    case ImGuiKey_Period: return '.';
    case ImGuiKey_Comma: return ',';
    case ImGuiKey_Delete: return 127;
    case ImGuiKey_Insert: return sc | 73;
    case ImGuiKey_Home: return sc | 74;
    case ImGuiKey_PageUp: return sc | 75;
    case ImGuiKey_End: return sc | 77;
    case ImGuiKey_PageDown: return sc | 78;
    case ImGuiKey_RightArrow: return sc | 79;
    case ImGuiKey_LeftArrow: return sc | 80;
    case ImGuiKey_DownArrow: return sc | 81;
    case ImGuiKey_UpArrow: return sc | 82;
    case ImGuiKey_KeypadSubtract: return sc | 86;
    case ImGuiKey_KeypadAdd: return sc | 87;
    case ImGuiKey_KeypadEnter: return sc | 88;
    case ImGuiKey_LeftCtrl: return sc | 224;
    case ImGuiKey_LeftShift: return sc | 225;
    case ImGuiKey_LeftAlt: return sc | 226;
    default: return 0;
    }
}

// While Game has focus, keys go to the game (but the debugger's F5, F10 and F11).
void ForwardKeys(bool focused) {
    static std::vector<ImGuiKey> held;
    ImGuiIO &io = ImGui::GetIO();
    int mods = (io.KeyShift ? 0x1 : 0) | (io.KeyCtrl ? 0x40 : 0) | (io.KeyAlt ? 0x100 : 0); // SDL KMOD_L*
    if (!focused || io.WantTextInput || !g_frameLink.IsOpen()) {
        for (ImGuiKey k : held) g_frameLink.SendKey(SdlKey(k), false, mods);
        held.clear();
        return;
    }
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; k++) {
        ImGuiKey key = (ImGuiKey)k;
        int sdl = SdlKey(key);
        if (!sdl || key == ImGuiKey_F5 || key == ImGuiKey_F10 || key == ImGuiKey_F11) continue;
        bool down = std::find(held.begin(), held.end(), key) != held.end();
        if (ImGui::IsKeyDown(key) && !down) {
            g_frameLink.SendKey(sdl, true, mods);
            held.push_back(key);
        } else if (!ImGui::IsKeyDown(key) && down) {
            g_frameLink.SendKey(sdl, false, mods);
            held.erase(std::find(held.begin(), held.end(), key));
        }
    }
}

void PollFrames() {
    if (!g_bridge.Running()) {
        g_frameLink.Close();
        return;
    }
    UpdateGameWindow();
    if (!g_frameLink.IsOpen()) {
        static double lastTry;
        if (ImGui::GetTime() - lastTry < 0.25) return;
        lastTry = ImGui::GetTime();
        if (!g_frameLink.Open(g_bridge.Pid())) return;
        g_frameLink.SendWindowFit(!g_settings.gameWindow);
    }
    static bool fit;
    if (fit != !g_settings.gameWindow && g_frameLink.SendWindowFit(!g_settings.gameWindow)) fit = !g_settings.gameWindow;
    if (!g_frameLink.Read(g_frame)) return;
    g_framesRead++;

    // The game's alpha is not meant to be seen
    for (size_t n = 3; n < g_frame.pixels.size(); n += 4) g_frame.pixels[n] = 255;
    if (!g_frameTex) glGenTextures(1, &g_frameTex);
    glBindTexture(GL_TEXTURE_2D, g_frameTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (g_texW != g_frame.width || g_texH != g_frame.height) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_frame.width, g_frame.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     g_frame.pixels.data());
        g_texW = g_frame.width;
        g_texH = g_frame.height;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g_frame.width, g_frame.height, GL_RGBA, GL_UNSIGNED_BYTE,
                        g_frame.pixels.data());
    }

    Json j;
    if (!Json::Parse(g_frame.geometry.c_str(), j)) return;
    const Json &vp = j["vp"], &scr = j["scr"];
    g_frameVp = ImVec4((float)vp[size_t(0)].num(), (float)vp[1].num(), (float)vp[2].num(), (float)vp[3].num());
    g_frameScr = ImVec2((float)scr[size_t(0)].num(1), (float)scr[1].num(1));
    g_frameEnts.clear();
    for (const auto &e : j["e"].items) {
        FrameEntity f;
        f.id = e["id"].i();
        f.pn = e["pn"].i();
        f.helper = e["h"].truthy();
        f.axis = ImVec2((float)e["ax"][size_t(0)].num(), (float)e["ax"][1].num());
        if (!e["an"].isNull()) {
            f.action = e["an"].i();
            f.elem = e["el"].i();
            f.animPn = e["apn"].i();
            f.k = ImVec2((float)e["k"][size_t(0)].num(), (float)e["k"][1].num());
        }
        ReadBoxes(e["c1"], f.c1);
        ReadBoxes(e["c2"], f.c2);
        g_frameEnts.push_back(std::move(f));
    }
}

// ---------------------------------------------------------------------------
// Animation: the selected player's AIR element, its boxes edited here and on the game
// ---------------------------------------------------------------------------

bool g_boxEdit = true;    // the selected player's boxes can be dragged on the Game window
bool g_animFollow = true; // the Animation window shows what the selected player shows
int g_animAction = 0, g_animElem = 1;

void OnFiles(const Json &j) {
    for (const auto &p : j["players"].items) {
        auto &files = g_charFiles[p["pn"].i()];
        files.clear();
        for (const auto &kv : p["files"].fields)
            for (const auto &f : kv.second.items) files[kv.first].push_back(f.str());
    }
}

// The AIR file of a player's animations ("" until the game has told).
std::string AirPath(int pn) {
    auto it = g_charFiles.find(pn);
    if (it == g_charFiles.end()) {
        if (Connected() && g_canTrace && ImGui::GetTime() - g_filesAsked[pn] > 2.0) {
            g_filesAsked[pn] = ImGui::GetTime();
            SendRaw("ikdbg.files()");
        }
        return "";
    }
    auto f = it->second.find("anim");
    return f == it->second.end() || f->second.empty() ? "" : f->second[0];
}

// Parsed AIR files, parsed again when their text changes.
struct AirCache {
    std::string text;
    bool parsed = false;
    AirFile air;
};
std::map<std::string, AirCache> g_airs;

const AirFile &AirOf(const SourceFile &sf) {
    AirCache &c = g_airs[NormPath(sf.path)];
    if (!c.parsed || c.text != sf.text) {
        c.text = sf.text;
        Air_Parse(c.text, c.air);
        c.parsed = true;
    }
    return c.air;
}

const FrameEntity *FindFrameEntity(int id) {
    for (const auto &f : g_frameEnts)
        if (f.id == id) return &f;
    return nullptr;
}

// What the Animation window works on: the selected entity's AIR file, action and element.
struct AnimTarget {
    const Entity *e = nullptr;
    const FrameEntity *fe = nullptr;
    int pn = 0; // the player whose AIR file it is
    SourceFile *sf = nullptr;
    const AirAction *act = nullptr;
    int elem = 0;      // 1-based
    bool live = false; // that element is the one on the game now
};

AnimTarget GetAnimTarget() {
    AnimTarget t;
    t.e = FindEntity(g_selId);
    t.fe = FindFrameEntity(g_selId);
    t.pn = t.fe && t.fe->action >= 0 ? t.fe->animPn : t.e ? t.e->pn : 0;
    if (t.pn <= 0) return t;
    std::string path = AirPath(t.pn);
    if (path.empty()) return t;
    t.sf = &LoadSource(path);
    const AirFile &air = AirOf(*t.sf);
    if (g_animFollow && t.fe && t.fe->action >= 0) {
        g_animAction = t.fe->action;
        g_animElem = t.fe->elem;
    }
    auto it = air.actions.find(g_animAction);
    if (it == air.actions.end() || it->second.elems.empty()) return t;
    t.act = &it->second;
    t.elem = std::max(1, std::min(g_animElem, (int)t.act->elems.size()));
    t.live = t.fe && t.fe->action == g_animAction && t.fe->elem == t.elem;
    return t;
}

// The changed AIR text goes to every player using that file (the file itself is saved apart).
void ApplyAnims(SourceFile &sf) {
    sf.version++; // the Code window's editor takes the new text
    char dir[MAX_PATH];
    GetTempPathA(MAX_PATH, dir);
    for (const auto &kv : g_charFiles) {
        auto f = kv.second.find("anim");
        if (f == kv.second.end() || f->second.empty() || NormPath(f->second[0]) != NormPath(sf.path)) continue;
        std::string tmp = std::string(dir) + "ikdbg_anim_" + std::to_string(kv.first) + ".air";
        std::ofstream out(tmp, std::ios::binary);
        out << sf.text;
        out.close();
        SendRaw("ikdbg.anims(" + std::to_string(kv.first) + ", " + LuaQuote(tmp) + ")");
    }
}

void AddBox(AnimTarget &t, int g) {
    const AirElem &el = t.act->elems[t.elem - 1];
    std::vector<AirBox> boxes = el.clsn[g];
    AirBox b;
    if (boxes.empty()) {
        b.x1 = -15, b.y1 = -80, b.x2 = 15, b.y2 = 0;
    } else {
        b = boxes.back();
        b.y1 -= 10, b.y2 -= 10;
    }
    boxes.push_back(b);
    if (Air_SetOwnBoxes(t.sf->text, el, g, boxes)) ApplyAnims(*t.sf);
}

void DeleteBox(AnimTarget &t, int g, int index) {
    const AirElem &el = t.act->elems[t.elem - 1];
    std::vector<AirBox> boxes = el.clsn[g];
    boxes.erase(boxes.begin() + index);
    if (Air_SetOwnBoxes(t.sf->text, el, g, boxes)) ApplyAnims(*t.sf);
}

// Number of elements of the action whose boxes come from that header line.
int SharedBy(const AirAction &act, int g, int header) {
    int n = 0;
    for (const auto &e : act.elems) n += e.header[g] == header;
    return n;
}

void AnimationWindow() {
    if (!ImGui::Begin("Animation")) {
        ImGui::End();
        return;
    }
    AnimTarget t = GetAnimTarget();
    if (!t.e) {
        ImGui::TextDisabled("Pick a player in the Game or Players window.");
        ImGui::End();
        return;
    }
    ImGui::Text("%s  %s", Who(t.e->pn, t.e->helper, t.e->id).c_str(), t.e->name.c_str());
    ImGui::Checkbox("Follow the player", &g_animFollow);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6);
    if (ImGui::InputInt("Action", &g_animAction, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) g_animFollow = false;
    ImGui::Checkbox("Drag its boxes on the game", &g_boxEdit);
    HelpMarker("Pause the game (F5) and drag the edges, the corners or the inside of a box of the selected "
               "player's element. Changes go to the game at once; Save writes the AIR file.");
    if (!t.sf) {
        ImGui::TextDisabled(g_canTrace ? "Waiting for the files of P%d..." : "Needs the patched Ikemen GO.", t.pn);
        ImGui::End();
        return;
    }
    SourceFile &sf = *t.sf;
    ImGui::BeginDisabled(!sf.Dirty());
    if (ImGui::Button("Save")) SaveSource(sf);
    ImGui::SameLine();
    if (ImGui::Button("Revert")) {
        ReadSource(sf);
        ApplyAnims(sf);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%s%s", sf.path.c_str(), sf.Dirty() ? " *" : "");
    if (!t.act) {
        ImGui::TextDisabled("Action %d is not in %s.", g_animAction, sf.path.c_str());
        ImGui::End();
        return;
    }

    // The elements; a click shows one on the game (pause first to keep it there)
    ImGui::SeparatorText("Elements");
    for (int n = 1; n <= (int)t.act->elems.size(); n++) {
        if (n > 1) ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < ImGui::GetFontSize() * 2) ImGui::NewLine();
        bool cur = n == t.elem;
        if (t.act->loopStart == n - 1) {
            ImGui::TextDisabled("|");
            ImGui::SameLine();
        }
        if (cur) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        char label[16];
        snprintf(label, sizeof(label), "%d", n);
        if (ImGui::SmallButton(label)) {
            g_animElem = n;
            if (t.fe && t.fe->action == g_animAction) {
                char lua[96];
                snprintf(lua, sizeof(lua), "ikdbg.with(%d, function() debugSetAnimElem(%d) end)", t.e->id, n);
                SendRaw(lua);
            } else {
                g_animFollow = false;
            }
        }
        if (cur) ImGui::PopStyleColor();
    }

    const AirElem &el = t.act->elems[t.elem - 1];
    ImGui::SeparatorText("Element");
    int v[5] = {el.group, el.image, el.x, el.y, el.time};
    const char *names[5] = {"group", "image", "x", "y", "time"};
    bool done = false;
    for (int n = 0; n < 5; n++) {
        if (n) ImGui::SameLine();
        ImGui::PushID(n);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 3.5f);
        if (ImGui::DragInt(names[n], &v[n], 0.2f)) Air_SetElem(sf.text, el, v[0], v[1], v[2], v[3], v[4]);
        done |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::PopID();
    }
    if (done) ApplyAnims(sf);
    ImGui::TextDisabled("line %d", el.line);
    ImGui::SameLine();
    if (ImGui::SmallButton("Show in Code")) {
        g_codeFile = sf.path;
        g_codeLine = g_codeScrollTo = el.line;
    }

    for (int g = 1; g >= 0; g--) {
        ImGui::PushID(g);
        ImGui::SeparatorText(g == 1 ? "Clsn2 (can be hit)" : "Clsn1 (hits)");
        if (el.header[g] == 0)
            ImGui::TextDisabled("none");
        else if (el.own[g])
            ImGui::TextDisabled("its own, line %d", el.header[g]);
        else
            ImGui::TextDisabled("the action's default (line %d), shared by %d elements", el.header[g],
                                SharedBy(*t.act, g, el.header[g]));
        for (size_t i = 0; i < el.clsn[g].size(); i++) {
            const AirBox &b = el.clsn[g][i];
            ImGui::PushID((int)i);
            int r[4] = {b.x1, b.y1, b.x2, b.y2};
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
            if (ImGui::DragInt4("##box", r, 0.2f)) Air_SetBox(sf.text, b, r[0], r[1], r[2], r[3]);
            if (ImGui::IsItemDeactivatedAfterEdit()) ApplyAnims(sf);
            ImGui::SameLine();
            bool del = ImGui::SmallButton("x");
            ImGui::SetItemTooltip("Delete this box (the element gets its own boxes)");
            ImGui::PopID();
            if (del) {
                DeleteBox(t, g, (int)i);
                ImGui::PopID();
                ImGui::End();
                return; // the element's lines changed
            }
        }
        if (ImGui::SmallButton("Add a box")) {
            AddBox(t, g);
            ImGui::PopID();
            ImGui::End();
            return;
        }
        ImGui::PopID();
    }
    ImGui::End();
}

// The selected player's boxes on the Game window, with handles: drag an edge, a corner or the
// inside. Returns true while it has the mouse (the Game window then neither selects nor moves).
struct BoxDrag {
    bool active = false;
    int g = 0, i = 0, edges = 0; // edges: 1 left, 2 right, 4 top, 8 bottom (screen); 15 move
    ImVec2 start;                // mouse, in AIR units
    AirBox box;                  // when the drag started
} g_boxDrag;

bool BoxEditOverlay(ImDrawList *draw, bool hovered, const std::function<ImVec2(float, float)> &toScreen,
                    const std::function<ImVec2(ImVec2)> &toPicture, bool &shown) {
    shown = false;
    AnimTarget t = GetAnimTarget();
    if (!g_boxEdit || !t.live || !t.fe || t.fe->k.x == 0 || t.fe->k.y == 0) {
        g_boxDrag.active = false;
        return false;
    }
    shown = true;
    const AirElem &el = t.act->elems[t.elem - 1];
    const FrameEntity &fe = *t.fe;
    auto airToScreen = [&](float x, float y) { return toScreen(fe.axis.x + fe.k.x * x, fe.axis.y + fe.k.y * y); };
    auto screenToAir = [&](ImVec2 s) {
        ImVec2 p = toPicture(s);
        return ImVec2((p.x - fe.axis.x) / fe.k.x, (p.y - fe.axis.y) / fe.k.y);
    };
    // The box as dragged so far (AIR units, x1 <= x2, y1 <= y2)
    auto dragged = [&](ImVec2 m) {
        AirBox b = g_boxDrag.box;
        ImVec2 a = screenToAir(m);
        int dx = (int)lroundf(a.x - g_boxDrag.start.x), dy = (int)lroundf(a.y - g_boxDrag.start.y);
        int e = g_boxDrag.edges;
        if (e == 15) {
            b.x1 += dx, b.x2 += dx, b.y1 += dy, b.y2 += dy;
        } else {
            // facing left: the screen's left edge is the box's x2
            bool flip = fe.k.x < 0;
            if (e & 1) (flip ? b.x2 : b.x1) += dx;
            if (e & 2) (flip ? b.x1 : b.x2) += dx;
            if (e & 4) b.y1 += dy;
            if (e & 8) b.y2 += dy;
        }
        if (b.x1 > b.x2) std::swap(b.x1, b.x2);
        if (b.y1 > b.y2) std::swap(b.y1, b.y2);
        return b;
    };
    ImVec2 m = ImGui::GetMousePos();
    float grab = ImGui::GetFontSize() * 0.45f;
    int hitG = -1, hitI = -1, hitEdges = 0;
    float hitArea = FLT_MAX;
    for (int g = 0; g < 2; g++)
        for (size_t i = 0; i < el.clsn[g].size(); i++) {
            const AirBox &b = el.clsn[g][i];
            ImVec2 a = airToScreen((float)b.x1, (float)b.y1), c = airToScreen((float)b.x2, (float)b.y2);
            ImVec2 lo(std::min(a.x, c.x), std::min(a.y, c.y)), hi(std::max(a.x, c.x), std::max(a.y, c.y));
            if (m.x < lo.x - grab || m.x > hi.x + grab || m.y < lo.y - grab || m.y > hi.y + grab) continue;
            int edges = (fabsf(m.x - lo.x) < grab ? 1 : 0) | (fabsf(m.x - hi.x) < grab ? 2 : 0) |
                        (fabsf(m.y - lo.y) < grab ? 4 : 0) | (fabsf(m.y - hi.y) < grab ? 8 : 0);
            float area = (hi.x - lo.x) * (hi.y - lo.y);
            if (edges == 0 && (m.x < lo.x || m.x > hi.x || m.y < lo.y || m.y > hi.y)) continue;
            // An edge beats an inside; among insides the smallest box wins
            bool better = hitG < 0 || (edges && !hitEdges) || ((edges != 0) == (hitEdges != 0) && area < hitArea);
            if (better) {
                hitG = g, hitI = (int)i, hitEdges = edges ? edges : 15, hitArea = area;
                if (edges == 0) hitEdges = 15;
            }
        }

    if (g_boxDrag.active && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        // Released: write the box and send the animations to the game
        AirBox b = dragged(m);
        const AirBox &old = g_boxDrag.box;
        g_boxDrag.active = false;
        if ((b.x1 != old.x1 || b.y1 != old.y1 || b.x2 != old.x2 || b.y2 != old.y2) && g_boxDrag.g < 2 &&
            g_boxDrag.i < (int)el.clsn[g_boxDrag.g].size() &&
            Air_SetBox(t.sf->text, el.clsn[g_boxDrag.g][g_boxDrag.i], b.x1, b.y1, b.x2, b.y2))
            ApplyAnims(*t.sf);
        return true;
    }
    if (!g_boxDrag.active && hovered && hitG >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        g_boxDrag.active = true;
        g_boxDrag.g = hitG, g_boxDrag.i = hitI, g_boxDrag.edges = hitEdges;
        g_boxDrag.start = screenToAir(m);
        g_boxDrag.box = el.clsn[hitG][hitI];
    }

    int cursorEdges = g_boxDrag.active ? g_boxDrag.edges : hovered ? hitEdges : 0;
    if (cursorEdges == 15)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    else if ((cursorEdges & 3) && (cursorEdges & 12))
        ImGui::SetMouseCursor(((cursorEdges & 1) != 0) == ((cursorEdges & 4) != 0) ? ImGuiMouseCursor_ResizeNWSE
                                                                                    : ImGuiMouseCursor_ResizeNESW);
    else if (cursorEdges & 3)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    else if (cursorEdges & 12)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

    float thick = std::max(1.0f, ImGui::GetFontSize() / 13);
    for (int g = 0; g < 2; g++)
        for (size_t i = 0; i < el.clsn[g].size(); i++) {
            bool dragging = g_boxDrag.active && g_boxDrag.g == g && g_boxDrag.i == (int)i;
            bool hot = dragging || (!g_boxDrag.active && hitG == g && hitI == (int)i);
            AirBox b = dragging ? dragged(m) : el.clsn[g][i];
            ImVec2 a = airToScreen((float)b.x1, (float)b.y1), c = airToScreen((float)b.x2, (float)b.y2);
            ImVec2 lo(std::min(a.x, c.x), std::min(a.y, c.y)), hi(std::max(a.x, c.x), std::max(a.y, c.y));
            ImU32 line = g == 0 ? IM_COL32(255, 70, 70, 255) : IM_COL32(80, 140, 255, 255);
            draw->AddRectFilled(lo, hi, g == 0 ? IM_COL32(255, 70, 70, hot ? 80 : 40) : IM_COL32(80, 140, 255, hot ? 80 : 40));
            draw->AddRect(lo, hi, line, 0, hot ? thick * 2 : thick);
            float h = grab * 0.5f;
            for (ImVec2 p : {lo, hi, ImVec2(lo.x, hi.y), ImVec2(hi.x, lo.y)})
                draw->AddRectFilled(ImVec2(p.x - h, p.y - h), ImVec2(p.x + h, p.y + h), line);
            if (dragging) {
                char label[64];
                snprintf(label, sizeof(label), "%d, %d, %d, %d", b.x1, b.y1, b.x2, b.y2);
                draw->AddText(ImVec2(lo.x, hi.y + thick * 2), IM_COL32(255, 255, 255, 255), label);
            }
        }
    return g_boxDrag.active || (hovered && hitG >= 0);
}

void GameWindow() {
    if (!ImGui::Begin("Game", nullptr, ImGuiWindowFlags_NoNavInputs)) {
        ForwardKeys(false);
        ImGui::End();
        return;
    }
    bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    ForwardKeys(focused);
    static bool showBoxes = true, showAxes = true, onlySelected = false;
    ImGui::Checkbox("Boxes", &showBoxes);
    ImGui::SameLine();
    ImGui::Checkbox("Axes", &showAxes);
    ImGui::SameLine();
    ImGui::Checkbox("Only the selected one", &onlySelected);
    if (g_frameTex) {
        ImGui::SameLine();
        ImGui::TextDisabled("%dx%d, frame %u", g_texW, g_texH, g_frame.frame);
        ImGui::SameLine();
        if (focused)
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1), "keys go to the game");
        else
            ImGui::TextDisabled("click the picture to play");
    }
    if (!g_frameTex || !g_bridge.Running()) {
        ImGui::TextDisabled(!g_bridge.Running()    ? "Launch the game from the Session window."
                            : !g_frameLink.IsOpen() ? "Waiting for the game's frames (they need the patched Ikemen GO)."
                                                    : "Waiting for the first frame...");
        ImGui::End();
        return;
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float scale = std::max(0.01f, std::min(avail.x / g_texW, avail.y / g_texH));
    ImVec2 size(g_texW * scale, g_texH * scale);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    p0.x += (avail.x - size.x) * 0.5f;
    ImGui::SetCursorScreenPos(p0);
    // Rows come bottom first
    ImGui::Image(ImTextureRef((ImTextureID)(intptr_t)g_frameTex), size, ImVec2(0, 1), ImVec2(1, 0));
    bool hovered = ImGui::IsItemHovered();
    bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

    // Game picture pixels to the screen
    float kx = g_frameVp.z / g_frameScr.x * scale, ky = g_frameVp.w / g_frameScr.y * scale;
    auto toScreen = [&](float x, float y) {
        return ImVec2(p0.x + g_frameVp.x * scale + x * kx, p0.y + g_frameVp.y * scale + y * ky);
    };
    auto toPicture = [&](ImVec2 s) {
        return ImVec2((s.x - p0.x - g_frameVp.x * scale) / kx, (s.y - p0.y - g_frameVp.y * scale) / ky);
    };

    // The selected player's boxes, editable (Animation window); they take the mouse first
    ImDrawList *draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(p0, ImVec2(p0.x + size.x, p0.y + size.y), true);
    draw->ChannelsSplit(2);
    draw->ChannelsSetCurrent(1);
    bool boxesShown = false;
    bool boxMouse = BoxEditOverlay(draw, hovered, toScreen, toPicture, boxesShown);
    draw->ChannelsSetCurrent(0);

    // Dragging a player or helper moves it (debugMoveBy takes pixels of the game picture)
    static int dragId = -1;
    static ImVec2 dragRest;
    if (dragId >= 0) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || !FindEntity(dragId)) {
            dragId = -1;
        } else {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            dragRest.x += d.x / kx;
            dragRest.y += d.y / ky;
            float dx = std::trunc(dragRest.x), dy = std::trunc(dragRest.y);
            if (dx != 0 || dy != 0) {
                char lua[128];
                snprintf(lua, sizeof(lua), "ikdbg.with(%d, function() debugMoveBy(%g, %g) end)", dragId, dx, dy);
                SendRaw(lua);
                dragRest.x -= dx;
                dragRest.y -= dy;
            }
        }
    }

    // What the mouse is over: the smallest box under it, else the nearest axis
    int hoverId = -1;
    if (hovered && !boxMouse) {
        ImVec2 m = ImGui::GetMousePos();
        float best = FLT_MAX;
        for (const auto &e : g_frameEnts)
            for (const auto *boxes : {&e.c2, &e.c1})
                for (const auto &b : *boxes) {
                    ImVec2 a = toScreen(b.x, b.y), c = toScreen(b.x + b.z, b.y + b.w);
                    float area = std::fabs((c.x - a.x) * (c.y - a.y));
                    if (m.x >= std::min(a.x, c.x) && m.x <= std::max(a.x, c.x) && m.y >= std::min(a.y, c.y) &&
                        m.y <= std::max(a.y, c.y) && area < best) {
                        best = area;
                        hoverId = e.id;
                    }
                }
        if (hoverId < 0) {
            float reach = ImGui::GetFontSize() * 1.5f;
            best = reach * reach;
            for (const auto &e : g_frameEnts) {
                ImVec2 a = toScreen(e.axis.x, e.axis.y);
                float d = (a.x - m.x) * (a.x - m.x) + (a.y - m.y) * (a.y - m.y);
                if (d < best) {
                    best = d;
                    hoverId = e.id;
                }
            }
        }
        if (clicked && hoverId >= 0) {
            g_selId = hoverId;
            dragId = hoverId;
            dragRest = ImVec2(0, 0);
        }
        if (hoverId >= 0 && dragId < 0) {
            const Entity *ent = FindEntity(hoverId);
            ImGui::SetTooltip("%s  %s\nstate %d, anim %d", ent ? Who(ent->pn, ent->helper, ent->id).c_str() : "?",
                              ent ? ent->name.c_str() : "", ent ? ent->st : 0, ent ? ent->anim : 0);
        }
    }

    float thick = std::max(1.0f, ImGui::GetFontSize() / 13);
    for (const auto &e : g_frameEnts) {
        bool sel = e.id == g_selId, hot = e.id == hoverId;
        if (onlySelected && !sel && !hot) continue;
        if (showBoxes && !(sel && boxesShown)) {
            for (int group = 1; group <= 2; group++) {
                ImU32 line = group == 1 ? IM_COL32(255, 70, 70, sel || hot ? 255 : 170)
                                        : IM_COL32(80, 140, 255, sel || hot ? 255 : 170);
                ImU32 fill = group == 1 ? IM_COL32(255, 70, 70, hot ? 70 : 35) : IM_COL32(80, 140, 255, hot ? 70 : 35);
                for (const auto &b : group == 1 ? e.c1 : e.c2) {
                    ImVec2 a = toScreen(b.x, b.y), c = toScreen(b.x + b.z, b.y + b.w);
                    ImVec2 lo(std::min(a.x, c.x), std::min(a.y, c.y)), hi(std::max(a.x, c.x), std::max(a.y, c.y));
                    draw->AddRectFilled(lo, hi, fill);
                    draw->AddRect(lo, hi, line, 0, sel ? thick * 2 : thick);
                }
            }
        }
        if (showAxes || sel || hot) {
            ImVec2 a = toScreen(e.axis.x, e.axis.y);
            float r = ImGui::GetFontSize() * (sel ? 0.8f : 0.5f);
            ImU32 col = sel ? IM_COL32(255, 220, 60, 255) : hot ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 255, 255, 160);
            draw->AddLine(ImVec2(a.x - r, a.y), ImVec2(a.x + r, a.y), col, sel ? thick * 2 : thick);
            draw->AddLine(ImVec2(a.x, a.y - r), ImVec2(a.x, a.y + r), col, sel ? thick * 2 : thick);
            if (sel || hot) {
                const Entity *ent = FindEntity(e.id);
                std::string label = ent ? Who(ent->pn, ent->helper, ent->id) + "  " + std::to_string(ent->st) : "";
                draw->AddText(ImVec2(a.x + r * 0.6f, a.y + r * 0.3f), col, label.c_str());
            }
        }
    }
    draw->ChannelsMerge();
    draw->PopClipRect();
    if (focused) draw->AddRect(p0, ImVec2(p0.x + size.x, p0.y + size.y), IM_COL32(90, 200, 90, 200), 0, thick);
    ImGui::End();
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
    ImGui::Checkbox("Show the game's own window", &s.gameWindow);
    HelpMarker("With the patched Ikemen GO the game is shown in the Game window and its own window is hidden.");

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

std::string GamePath(const std::string &file) {
    bool absolute = file.size() > 1 && (file[1] == ':' || file[0] == '\\' || file[0] == '/');
    return absolute ? file : IkemenDir() + "\\" + file;
}

FILETIME FileTime(const std::string &path) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data)) return FILETIME{};
    return data.ftLastWriteTime;
}

// Reads the file into sf.text and sf.disk ('\n' line ends, no BOM).
void ReadSource(SourceFile &sf) {
    std::string raw = ReadWholeFile(GamePath(sf.path));
    sf.mtime = FileTime(GamePath(sf.path));
    sf.missing = raw.empty() && sf.mtime.dwLowDateTime == 0 && sf.mtime.dwHighDateTime == 0;
    sf.bom = raw.size() >= 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB &&
             (unsigned char)raw[2] == 0xBF;
    if (sf.bom) raw.erase(0, 3);
    sf.crlf = raw.find("\r\n") != std::string::npos;
    std::string text;
    text.reserve(raw.size());
    for (char ch : raw)
        if (ch != '\r') text += ch;
    sf.text = sf.disk = text;
    sf.version++;
}

SourceFile &LoadSource(const std::string &file) {
    std::string key = NormPath(file);
    auto it = g_files.find(key);
    if (it != g_files.end()) return it->second;
    SourceFile &sf = g_files[key];
    sf.path = file;
    sf.ed = dbg::Editor_Create();
    ReadSource(sf);
    return sf;
}

bool SaveSource(SourceFile &sf) {
    std::string out = sf.bom ? "\xEF\xBB\xBF" : "";
    for (char ch : sf.text) {
        if (ch == '\n' && sf.crlf) out += '\r';
        out += ch;
    }
    std::ofstream f(GamePath(sf.path), std::ios::binary);
    f << out;
    f.close();
    if (!f) {
        Log(LogLine::Error, "Could not write " + GamePath(sf.path));
        return false;
    }
    sf.disk = sf.text;
    sf.mtime = FileTime(GamePath(sf.path));
    return true;
}

// Files changed on disk by something else are read again, unless they have unsaved changes.
void WatchSources() {
    static double last;
    if (ImGui::GetTime() - last < 1.0) return;
    last = ImGui::GetTime();
    for (auto &kv : g_files) {
        SourceFile &sf = kv.second;
        FILETIME now = FileTime(GamePath(sf.path));
        if (CompareFileTime(&now, &sf.mtime) != 0 && !sf.Dirty()) ReadSource(sf);
    }
}

// Saves every changed file and has the game load and compile the characters again.
bool g_applying;
std::string g_applyError;

void SaveAndApply() {
    int saved = 0;
    for (auto &kv : g_files)
        if (kv.second.Dirty()) {
            if (!SaveSource(kv.second)) return;
            saved++;
        }
    for (auto &kv : g_files) {
        kv.second.errorLine = 0;
        dbg::Editor_SetErrorLine(kv.second.ed, 0, nullptr);
    }
    g_applyError.clear();
    if (!Connected()) {
        Log(LogLine::Info, saved ? "Saved; the game is not running." : "Nothing to save.");
        return;
    }
    g_applying = true;
    SendRaw("ikdbg.reload()");
}

// The game's answer to ikdbg.reload(): {items: [{pn, ok, err}]}.
void OnReload(const Json &j) {
    g_applying = false;
    std::string applied;
    for (const auto &item : j["items"].items) {
        int pn = item["pn"].i();
        if (item["ok"].truthy()) {
            applied += (applied.empty() ? "P" : ", P") + std::to_string(pn);
            continue;
        }
        // "path:line:\nmessage" from the compiler
        std::string err = item["err"].str();
        g_applyError = "P" + std::to_string(pn) + ": " + err;
        Log(LogLine::Error, "Not applied, the game keeps the previous version. " + g_applyError);
        size_t nl = err.find('\n');
        std::string head = err.substr(0, nl);
        size_t c2 = head.find_last_of(':');
        size_t c1 = c2 == std::string::npos || c2 == 0 ? std::string::npos : head.find_last_of(':', c2 - 1);
        if (c1 != std::string::npos && c1 > 1) {
            std::string file = head.substr(0, c1);
            int line = atoi(head.c_str() + c1 + 1);
            if (line > 0) {
                SourceFile &sf = LoadSource(file);
                sf.errorLine = line;
                sf.errorMsg = nl == std::string::npos ? err : err.substr(nl + 1);
                dbg::Editor_SetErrorLine(sf.ed, line, sf.errorMsg.c_str());
                g_codeFile = file;
                g_codeLine = 0;
                g_codeScrollTo = line;
            }
        }
        return;
    }
    if (!applied.empty()) Log(LogLine::Result, "Applied to " + applied + ".");
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
    WatchSources();
    if (!ImGui::Begin("Code")) {
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

    // Follow the selected entity's state, but not while its file is being edited
    static bool follow = true;
    static int followedSrc;
    SourceFile *shown = g_codeFile.empty() ? nullptr : &LoadSource(g_codeFile);
    bool editing = shown && dbg::Editor_IsFocused(shown->ed);
    int stateSrc = row ? row->stateSrc : e ? e->stateSrc : 0;
    if (g_codeHold) {
        followedSrc = stateSrc;
        g_codeHold = false;
    }
    if (follow && g_canTrace && stateSrc != followedSrc && !editing) {
        followedSrc = stateSrc;
        JumpTo(stateSrc);
    }

    // Toolbar: Save and apply, Revert, the files with changes
    int dirty = 0;
    for (const auto &kv : g_files) dirty += kv.second.Dirty();
    bool save = ImGui::Button(dirty ? "Save and apply (Ctrl+S)" : "Apply (Ctrl+S)");
    ImGuiIO &io = ImGui::GetIO();
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && io.KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_S, false))
        save = true;
    if (save) SaveAndApply();
    ImGui::SetItemTooltip("Saves the changed files, then the game loads and compiles the characters again "
                          "(states, commands, constants, animations, sounds) without restarting the match.");
    ImGui::SameLine();
    ImGui::BeginDisabled(!shown || !shown->Dirty());
    if (ImGui::Button("Revert")) ReadSource(*shown);
    ImGui::EndDisabled();
    ImGui::SameLine();
    std::string label = g_codeFile.empty() ? std::string("(no file)") : g_codeFile;
    if (shown && shown->Dirty()) label += " *";
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18);
    if (ImGui::BeginCombo("##file", label.c_str())) {
        for (auto &kv : g_files) {
            std::string name = kv.second.path + (kv.second.Dirty() ? " *" : "");
            if (ImGui::Selectable(name.c_str(), NormPath(g_codeFile) == kv.first)) {
                g_codeFile = kv.second.path;
                g_codeLine = 0;
                follow = false;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Follow the state", &follow);
    ImGui::SameLine();
    if (ImGui::SmallButton("Statedef")) JumpTo(stateSrc);
    if (dirty) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0.75f, 0.3f, 1), "%d unsaved", dirty);
    }
    if (g_applying) {
        ImGui::SameLine();
        ImGui::TextDisabled("applying...");
    }
    if (!g_applyError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.45f, 0.45f, 1));
        ImGui::TextWrapped("Not applied, the game keeps the previous version. %s", g_applyError.c_str());
        ImGui::PopStyleColor();
    }
    if (e) {
        ImGui::TextDisabled("%s  state %d", Who(e->pn, e->helper, e->id).c_str(), row ? row->st : e->st);
        if (row) {
            ImGui::SameLine();
            ImGui::TextDisabled("tick %d%s", row->t, g_selTick >= 0 ? " (picked in Frame data)" : "");
        }
    }

    // Left: the controllers that ran, in order (patched Ikemen). Right: the file.
    std::map<int, bool> ranLines;
    std::string codeNorm = NormPath(g_codeFile);
    if (g_canTrace) {
        float listWidth = ImGui::GetContentRegionAvail().x * 0.3f;
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
                        char rowLabel[32];
                        snprintf(rowLabel, sizeof(rowLabel), "%d##%d", s.state, n);
                        bool here = NormPath(s.file) == codeNorm && s.line == g_codeLine;
                        if (ImGui::Selectable(rowLabel, here, ImGuiSelectableFlags_SpanAllColumns)) JumpTo(src);
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
    }

    if (!shown) {
        ImGui::TextDisabled(g_canTrace ? "Pick a controller on the left."
                                       : "The Code window follows the state with the patched Ikemen GO.");
        ImGui::End();
        return;
    }
    SourceFile &sf = *shown;
    if (g_codeScrollTo > 0) {
        dbg::Editor_GotoLine(sf.ed, g_codeScrollTo, false, false);
        g_codeScrollTo = 0;
    }
    const auto &ctrlLines = ControllerLines(sf.path);
    std::string file = sf.path;
    int codeLine = g_codeLine;
    dbg::Editor_SetDecorations(
        sf.ed,
        [&](int line) {
            if (CtrlBreak *bp = FindCtrlBreak(file, line)) return bp->enabled ? 2 : 3;
            return ctrlLines.count(line) ? 1 : 0;
        },
        [&](int line) -> ImU32 {
            if (line == codeLine) return IM_COL32(200, 170, 40, 70);
            return ranLines.count(line) ? IM_COL32(40, 130, 60, 110) : 0;
        });
    if (sf.missing) ImGui::TextDisabled("Could not read %s", sf.path.c_str());
    dbg::Editor_Draw(sf.ed, "file", sf.text, sf.version);
    if (int line = dbg::Editor_MarkClicked(sf.ed))
        if (ctrlLines.count(line)) ToggleCtrlBreak(file, line);
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
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.36f, &bottom, &top);
    ImGui::DockBuilderSplitNode(top, ImGuiDir_Up, 0.16f, &control, &players);
    ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.45f, &leftTop, &leftBottom);
    ImGui::DockBuilderDockWindow("Session", leftTop);
    ImGui::DockBuilderDockWindow("Players", leftBottom);
    ImGui::DockBuilderDockWindow("Breakpoints", leftBottom);
    ImGui::DockBuilderDockWindow("Control", control);
    ImGui::DockBuilderDockWindow("Game", players);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderDockWindow("Animation", right);
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
    else if (key == "GameWindow") s.gameWindow = value == "1";
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
    buf->appendf("Exe=%s\nP1=%s\nP2=%s\nStage=%s\nExtra=%s\nAI1=%d\nAI2=%d\nGameWindow=%d\n", s.exe.c_str(),
                 s.p1.c_str(), s.p2.c_str(), s.stage.c_str(), s.extra.c_str(), s.ai1, s.ai2, s.gameWindow);
    for (const auto &b : g_breakpoints) buf->appendf("Breakpoint=%d|%d|%s\n", b.target, b.enabled, b.cond.c_str());
    for (const auto &b : g_ctrlBreaks) buf->appendf("CtrlBreak=%d|%d|%s\n", b.enabled, b.line, b.file.c_str());
    buf->append("\n");
}

} // namespace

void App_Init(bool launch) {
    g_autoLaunch = launch;
    g_iniPath = ExeDir() + "\\ikemen-debugger.ini";
    g_logFile = fopen((ExeDir() + "\\ikemen-debugger.log").c_str(), "w");
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
    PollFrames();
    GameWindow();
    AnimationWindow();
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

void App_Console(const char *lua) {
    Log(LogLine::Info, std::string("> ") + lua);
    Exec(lua);
}

void App_Shutdown() {
    ImGui::SaveIniSettingsToDisk(g_iniPath.c_str());
    g_bridge.Stop();
}
