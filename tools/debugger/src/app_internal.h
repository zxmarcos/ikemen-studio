// What the debugger's files share: its state and the functions of each window's file.
//   app.cpp          state, commands to the game, what the game sends, layout, settings
//   app_game.cpp     Game: the game's frames, the players' boxes over them, keys to the game
//   app_anim.cpp     Animation: the AIR element of the selected player
//   app_sprites.cpp  Sprites: the SFF file
//   app_sounds.cpp   Sounds: the SND file
//   app_code.cpp     Code: the source files, Apply
//   app_cmd.cpp      Commands: input, the commands under way, the CMD file
//   app_debug.cpp    Session, Control, Players, Inspector, Frame data, Trace, Breakpoints, Console
#pragma once

#include <algorithm>
#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "bridge.h"
#include "frame.h"
#include "air.h"
#include "sff.h"
#include "snd.h"
#include "editor.hpp"
#include <GL/gl.h>
#include "imgui.h"
#include "json.h"

namespace app {

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

// A player or helper as drawn in the last frame, in the game picture's pixels.
struct FrameEntity {
    int id = 0, pn = 0;
    bool helper = false;
    ImVec2 axis;
    std::vector<ImVec4> c1, c2; // x, y, w, h
    int action = -1, elem = 0;  // the animation shown; elem 1-based
    int animPn = 0;             // the player whose AIR file it comes from
    ImVec2 k;                   // pixels per AIR unit (x has the facing's sign)
    // Players only: frames each of U, D, B, F, a, b, c, x, y, z, s, d, w, m is held (> 0) or
    // released (< 0), and the commands under way or buffered (editor_input.go)
    std::vector<int> held;
    struct Cmd {
        std::string name;
        int n, done, steps, time, maxtime, buffer; // n: the n-th definition of that name
    };
    std::vector<Cmd> cmds;
};

extern Bridge g_bridge;
extern Settings g_settings;
extern std::string g_iniPath;
extern bool g_firstRun, g_autoLaunch;

extern bool g_hello;
extern std::string g_modVersion, g_ikemenVersion;
extern bool g_exitReported;

extern int g_tick, g_roundState, g_roundNo, g_steps;
extern bool g_paused;
extern std::vector<Entity> g_ents;
extern std::map<int, std::vector<double>> g_prevVars, g_prevFvars;
extern std::map<int, std::deque<FrameRow>> g_history;
extern std::deque<TraceEvent> g_trace;
extern std::vector<Breakpoint> g_breakpoints;
extern int g_nextBid;
extern std::string g_breakText;
extern std::deque<LogLine> g_log;
extern bool g_logScroll;
extern int g_selId, g_watchId;
extern unsigned g_snapshots, g_lastRate, g_rateCount;
extern double g_rateTime;

extern bool g_canTrace, g_canSet;
extern std::vector<Source> g_sources;
extern std::map<int, std::vector<int>> g_ran;
extern std::vector<CtrlBreak> g_ctrlBreaks;
extern int g_selTick;
extern std::map<std::string, SourceFile> g_files;
extern std::string g_codeFile;
extern int g_codeLine, g_codeScrollTo;
extern bool g_codeHold;

extern FrameLink g_frameLink;
extern GameFrame g_frame;
extern GLuint g_frameTex;
extern int g_texW, g_texH;
extern ImVec4 g_frameVp;
extern ImVec2 g_frameScr;
extern std::vector<FrameEntity> g_frameEnts;
extern unsigned g_framesRead;

extern std::map<int, std::map<std::string, std::vector<std::string>>> g_charFiles;
extern std::map<int, double> g_filesAsked;

const size_t kHistory = 1200, kTrace = 4000, kLog = 4000;

void Log(LogLine::Kind kind, const std::string &text);
std::string Who(int pn, bool helper, int id);
const Entity *FindEntity(int id);
const FrameEntity *FindFrameEntity(int id);
std::string NormPath(const std::string &path);
std::string ExeDir();
std::string ReadWholeFile(const std::string &path);
void HelpMarker(const char *text);

// Commands
bool Connected();
void SendRaw(const std::string &lua);
void Exec(const std::string &lua);
void ExecOn(int id, const std::string &lua);
void SendBreakpoint(const Breakpoint &b);
void Watch(int id);
void SendCtrlBreak(const CtrlBreak &b, bool on, size_t from = 1);
CtrlBreak *FindCtrlBreak(const std::string &file, int line);
void ToggleCtrlBreak(const std::string &file, int line);
void Launch();

// ---------------------------------------------------------------------------
// The windows
// ---------------------------------------------------------------------------

// Game
void PollFrames();
void GameWindow();

// Animation
extern bool g_boxEdit, g_followSel, g_onion, g_onionSprite, g_animFollow;
extern int g_animDrag, g_animAction, g_animElem;

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

void OnFiles(const Json &j);
std::string AirPath(int pn);
std::string CharFile(int pn, const char *key);
const AirFile &AirOf(const SourceFile &sf);
AnimTarget GetAnimTarget();
void ApplyAnims(SourceFile &sf);
void AnimationWindow();

// Sprites
struct SpriteFile {
    SffDoc doc;
    std::string path;   // as the game names it
    bool dirty = false;
    unsigned version = 1; // bumped by every change (the textures follow)
    int sel = -1;
};

SpriteFile *SpritesOf(int pn);
GLuint SpriteTexture(SpriteFile &sf, int index, int pal = -1);
ImTextureRef TexRef(GLuint tex);
void Checkerboard(ImDrawList *draw, ImVec2 a, ImVec2 b);
std::vector<std::string> PickFiles(const char *filter, bool multi);
std::string PickSaveFile(const char *filter, const char *ext, const std::string &name);
void SpritesWindow();

// Sounds
void SoundsWindow();

// Code
extern bool g_applying;          // waiting for the game's answer to Apply
extern std::string g_applyError; // of the last Apply that failed
std::string GamePath(const std::string &file);
SourceFile &LoadSource(const std::string &file);
void ReadSource(SourceFile &sf);
bool SaveSource(SourceFile &sf);
void SaveAndApply(); // saves the files changed, the game compiles its characters again
void OnReload(const Json &j);
void CodeWindow();

// Commands
void CommandsWindow();

// Session, Control, Players, Inspector, Frame data, Trace, Breakpoints, Console
void SessionWindow();
void ControlWindow();
void PlayersWindow();
void InspectorWindow();
void FrameDataWindow();
void TraceWindow();
void BreakpointsWindow();
void ConsoleWindow();

} // namespace app
