// The debugger: launches Ikemen GO with the ikdbg.lua mod, reads its
// snapshots and shows them; every action is Lua sent through the game's stdin
// or the shared memory. The windows are in app_*.cpp (see app_internal.h).
#include "app.h"
#include "app_internal.h"

#include <cstring>
#include <fstream>
#include <sstream>

#include "imgui_internal.h"

namespace app {

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
std::vector<WatchExpr> g_watches;
int g_nextWatch = 1;
std::string g_breakText;
std::deque<LogLine> g_log;
bool g_logScroll;
int g_selId = -1, g_watchId = -1, g_selPicked;
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

void SendWatchExpr(const WatchExpr &w) {
    SendRaw("ikdbg.exprSet(" + std::to_string(w.n) + ", " + std::to_string(w.target) + ", " + LuaQuote(w.code) + ")");
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
    return s.line == line && s.Controller() && NormPath(s.file) == normFile;
}

// Tells the game which source indices a controller breakpoint covers.
void SendCtrlBreak(const CtrlBreak &b, bool on, size_t from) {
    std::string file = NormPath(b.file);
    for (size_t i = std::max<size_t>(from, 1); i < g_sources.size(); i++)
        if (SourceAt(g_sources[i], file, b.line))
            SendRaw("ikdbg.ctrlBrkSet(" + std::to_string(i) + (on ? ", true, " + LuaQuote(b.cond) + ")" : ", false)"));
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
    e.clsn1 = j["c1"].truthy();
    e.numExplods = j["nex"].i();
    e.numProjs = j["npr"].i();
    for (const auto &v : j["vars"].items) e.vars.push_back(v.num());
    for (const auto &v : j["fvars"].items) e.fvars.push_back(v.num());
    e.hasObjects = j["explods"].type == Json::Array;
    auto objects = [](const Json &list, std::vector<Entity::Object> &out) {
        for (const auto &o : list.items)
            out.push_back({o["id"].i(), o["anim"].i(), o["elem"].i(), o["time"].i(), o["removetime"].i(),
                           o["x"].num(), o["y"].num(), o["bindtime"].i(), o["layer"].i(), o["sprpriority"].i(),
                           o["facing"].i(1), o["vx"].num(), o["vy"].num(), o["hits"].i(), o["misstime"].i(),
                           o["priority"].i()});
    };
    objects(j["explods"], e.explods);
    objects(j["projs"], e.projs);
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
    size_t added = j["ev"].items.size();
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
        if (t.kind == "got") {
            t.by = ev["by"].i();
            if (const Entity *by = FindEntity(t.by)) t.byPn = by->pn, t.byHelper = by->helper, t.bySt = by->st;
            t.guarded = ev["guarded"].truthy();
            t.fall = ev["fall"].truthy();
            t.damage = ev["dmg"].i();
            t.hittime = ev["hittime"].i();
            t.shake = ev["shake"].i();
            t.slide = ev["slide"].i();
            t.ctrltime = ev["ctrltime"].i();
            t.combo = ev["combo"].i();
            t.animtype = ev["animtype"].i();
            t.attr = ev["attr"].str();
            if (ev["jug"].type == Json::Number) t.juggle = ev["jug"].i();
            // The combo's damage: hits taken one after another (GetHitVar(hitcount) goes on counting)
            static std::map<int, int> comboDamage;
            int &sum = comboDamage[t.id];
            sum = t.guarded || t.combo <= 1 ? t.damage : sum + t.damage;
            t.comboDamage = sum;
        }
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
                            contact[e.id], e.clsn1, e.mvt, e.stt, e.x, e.y, e.vx, e.vy, e.stateSrc, g_ran[e.id]});
            while (rows.size() > kHistory) rows.pop_front();
        }
        g_selTick = -1;
        // A hit's attack: the HitDef (or Projectile, ReversalDef) its attacker ran last, this tick or before
        for (size_t n = g_trace.size() - std::min(added, g_trace.size()); n < g_trace.size(); n++) {
            TraceEvent &t = g_trace[n];
            auto rows = g_history.find(t.by);
            if (t.kind != "got" || rows == g_history.end()) continue;
            for (auto r = rows->second.rbegin(); r != rows->second.rend() && !t.hitdefSrc; ++r) {
                if (r->t > t.t) continue;
                for (int src : r->ran) {
                    if (src <= 0 || src >= (int)g_sources.size()) continue;
                    std::string type = g_sources[src].type;
                    std::transform(type.begin(), type.end(), type.begin(), ::tolower);
                    if (type == "hitdef" || type == "projectile" || type == "reversaldef") t.hitdefSrc = src;
                }
            }
        }
    }

    for (const auto &bk : j["bk"].items)
        for (auto &b : g_breakpoints)
            if (b.bid == bk["bid"].i()) b.hits = bk["hits"].i();

    OnLabState(j["lab"]);

    for (const auto &item : j["w"].items)
        for (auto &w : g_watches)
            if (w.n == item["n"].i()) {
                std::string v = item["v"].str(), err = item["err"].str();
                if (w.seen && w.value != v && advanced) w.changedTick = g_tick;
                w.value = v, w.error = err, w.seen = true;
            }

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
        for (const auto &w : g_watches) SendWatchExpr(w);
        LabHello();
    } else if (tag == "@IKTAKE") {
        OnTake(j);
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
        g_selPicked++;
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
    } else if (key == "CtrlBreakIf") {
        if (!g_ctrlBreaks.empty()) g_ctrlBreaks.back().cond = value; // of the CtrlBreak line before
    } else if (key == "WatchExpr") {
        // target|code
        int t = 0, used = 0;
        if (sscanf(value.c_str(), "%d|%n", &t, &used) == 1 && used > 0)
            g_watches.push_back({g_nextWatch++, t, value.substr(used)});
    } else {
        LabSettingsLine(key, value);
        AnalysisSettingsLine(key, value);
        LayoutSettingsLine(key, value);
    }
}

void SettingsWrite(ImGuiContext *, ImGuiSettingsHandler *handler, ImGuiTextBuffer *buf) {
    const Settings &s = g_settings;
    buf->appendf("[%s][Settings]\n", handler->TypeName);
    buf->appendf("Exe=%s\nP1=%s\nP2=%s\nStage=%s\nExtra=%s\nAI1=%d\nAI2=%d\nGameWindow=%d\n", s.exe.c_str(),
                 s.p1.c_str(), s.p2.c_str(), s.stage.c_str(), s.extra.c_str(), s.ai1, s.ai2, s.gameWindow);
    for (const auto &b : g_breakpoints) buf->appendf("Breakpoint=%d|%d|%s\n", b.target, b.enabled, b.cond.c_str());
    for (const auto &b : g_ctrlBreaks) {
        buf->appendf("CtrlBreak=%d|%d|%s\n", b.enabled, b.line, b.file.c_str());
        if (!b.cond.empty()) buf->appendf("CtrlBreakIf=%s\n", b.cond.c_str());
    }
    for (const auto &w : g_watches) buf->appendf("WatchExpr=%d|%s\n", w.target, w.code.c_str());
    LabSettingsWrite(buf);
    AnalysisSettingsWrite(buf);
    LayoutSettingsWrite(buf);
    buf->append("\n");
}

} // namespace app

using namespace app;

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
    LayoutFrame();
    PollFrames();
    GameWindow();
    AnimationWindow();
    SpritesWindow();
    SoundsWindow();
    CommandsWindow();
    StageWindow();
    ScreenWindows();
    RosterWindow();
    FontsWindow();
    SessionWindow();
    ControlWindow();
    PlayersWindow();
    InspectorWindow();
    FrameDataWindow();
    TraceWindow();
    BreakpointsWindow();
    WatchWindow();
    LabWindow();
    AnalysisWindow();
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
