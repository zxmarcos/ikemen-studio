// What the debugger's files share: its state and the functions of each window's file.
//   app.cpp          state, commands to the game, what the game sends, settings
//   app_layout.cpp   the menu bar, the windows opened and closed, the layouts
//   app_game.cpp     Game: the game's frames, the players' boxes over them, keys to the game
//   app_anim.cpp     Animation: the AIR element of the selected player
//   app_sprites.cpp  Sprites: the SFF file
//   app_sounds.cpp   Sounds: the SND file
//   app_code.cpp     Code: the source files, Apply
//   app_cmd.cpp      Commands: input, the commands under way, the CMD file
//   app_stage.cpp    Stage: the stage's def, its BGs picked and dragged on the Game window
//   app_screen.cpp   Fight screen, Motif: their defs, their elements picked and dragged on the Game window
//   app_roster.cpp   Roster: select.def's characters in the select screen's grid, its stages
//   app_lab.cpp      Lab: a dummy, takes of input, characters swapped
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
#include "ini.h"
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
    bool clsn1 = false; // its animation element has a Clsn1 box
    int numExplods = 0, numProjs = 0;
    int stateSrc = 0;   // source index of the current statedef (patched Ikemen only)
    std::vector<double> vars, fvars;
    // Its explods and projectiles (the watched entity, patched Ikemen only)
    struct Object {
        int id, anim, elem, time, removetime;
        double x, y;
        int bindtime, layer, sprpriority, facing; // explods
        double vx, vy;                            // projectiles
        int hits, misstime, priority;
    };
    bool hasObjects = false;
    std::vector<Object> explods, projs;
};

struct FrameRow {
    int t, st, anim, elem, etime, hitpause, life;
    bool ctrl, stateChanged, contact, clsn1;
    std::string mvt, stt;
    double x, y, vx, vy;
    int stateSrc;
    std::vector<int> ran; // source indices of the controllers that ran on this tick
};

// Where a state controller, statedef or trigger comes from (patched Ikemen only).
struct Source {
    std::string file, type; // the controller's type, "statedef" or "trigger" (a CNS trigger line, a ZSS if)
    int line = 0, state = 0;
    bool Controller() const { return type != "statedef" && type != "trigger"; }
};

// A breakpoint on the controller(s) at a line of a file.
struct CtrlBreak {
    std::string file;
    int line;
    bool enabled = true;
    int hits = 0;
    std::string cond; // Lua: stop only if true for the one that ran it (empty: always)
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
    std::string kind; // st: a state change; hit, guarded: its attack made contact; got: hit or guarding one
    int from, to, st;
    // got: by whom (its state then), and the GetHitVars
    int by = 0, byPn = 0, bySt = 0;
    bool byHelper = false, guarded = false, fall = false;
    int damage = 0, hittime = 0, shake = 0, slide = 0, ctrltime = 0, combo = 0, animtype = 0;
    std::string attr;
    int hitdefSrc = 0; // the attacker's HitDef or Projectile controller last run (source index), 0 if not known
    int juggle = -1;     // juggle points the attacker has left on it (-1: not known)
    int comboDamage = 0; // of the hits of the combo so far, this one included
};

struct Breakpoint {
    int bid;
    int target; // 0 any, n player n, -n player n and its helpers
    std::string cond;
    bool enabled = true;
    int hits = 0;
};

// A Watch window expression, read by the game with each snapshot.
struct WatchExpr {
    int n;
    int target; // 0 the entity the Inspector shows, n player n
    std::string code;
    std::string value, error;
    bool seen = false;    // a value came
    int changedTick = -1; // when its value last changed
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
extern std::vector<WatchExpr> g_watches;
extern int g_nextWatch;
extern std::string g_breakText;
extern std::deque<LogLine> g_log;
extern bool g_logScroll;
extern int g_selId, g_watchId;
extern int g_selPicked; // bumped when a player or helper is picked, even the one selected
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
// The windows: the menu bar, each one opened or closed, the layouts (app_layout.cpp)
bool BeginTool(const char *name, ImGuiWindowFlags flags = 0); // ImGui::Begin; false (End called): closed or hidden
void ShowWindow(const char *name); // opened if closed, its tab to the front, without taking the keyboard
void LayoutFrame();                // the menu bar and the dock space, at the start of a frame
void LayoutSettingsLine(const std::string &key, const std::string &value);
void LayoutSettingsWrite(ImGuiTextBuffer *buf);

// Commands
bool Connected();
void SendRaw(const std::string &lua);
void Exec(const std::string &lua);
void ExecOn(int id, const std::string &lua);
void SendBreakpoint(const Breakpoint &b);
void SendWatchExpr(const WatchExpr &w);
void Watch(int id);
void SendCtrlBreak(const CtrlBreak &b, bool on, size_t from = 1);
CtrlBreak *FindCtrlBreak(const std::string &file, int line);
void ToggleCtrlBreak(const std::string &file, int line);
void Launch();

// ---------------------------------------------------------------------------
// The windows
// ---------------------------------------------------------------------------

// Game
// Trigger values of one entity (debugTriggerWatch), by tick, as the frames bring them (the
// snapshots, by another way, may be a tick behind or ahead): sorted (source index, value) pairs,
// value 0 false, 1 true, 2 undefined
extern int g_trigId; // -1: none
extern std::map<int, std::vector<std::pair<int, int>>> g_trigTicks;
int TriggerValue(int tick, int src); // -1: not evaluated (or not known) on that tick
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
    std::string pal;    // the ACT file of an SFF v1's shared palette (a character's pal1), or ""
    bool dirty = false;
    unsigned version = 1; // bumped by every change (the textures follow)
    int sel = -1;
};

SpriteFile &SpriteFileAt(const std::string &path, const std::string &pal = ""); // read the first time
SpriteFile *SpritesOf(int pn);
// The Sprites window shows that file (at that sprite, -1 as it was); what: as "the font x.def"
void ShowInSprites(SpriteFile &sf, int index, const std::string &what);
GLuint SpriteTexture(SpriteFile &sf, int index, int pal = -1);
ImTextureRef TexRef(GLuint tex);
void Checkerboard(ImDrawList *draw, ImVec2 a, ImVec2 b);
std::vector<std::string> PickFiles(const char *filter, bool multi);
std::string PickSaveFile(const char *filter, const char *ext, const std::string &name);
void SpritesWindow();

// Sounds
const SndFile *SndOf(int pn); // a player's SND file, nullptr until known (or unreadable)
void SoundsWindow();

// Analysis: a character's states checked (Lint), its variables, the graph of its states
void AnalysisWindow();
std::string VarName(const std::string &def, char kind, int index); // "" without one ('v', 'f', 's', 'S')
std::string StatedefFile(int pn, int no); // the character's file whose [Statedef no] the engine reads first; "" none
void AnalysisSettingsLine(const std::string &key, const std::string &value);
void AnalysisSettingsWrite(ImGuiTextBuffer *buf);

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

// Stage: the stage's def file and where its BGs were drawn in the last frame (editor_stage.go)
struct FrameBg {
    int index;   // in the stage's BGs (the def's [BG ...] sections, in order)
    ImVec4 rect; // x, y, w, h in the game picture's pixels: the sprite once, untiled
    ImVec2 k;    // picture pixels per unit of its start
    int layer;
    bool tiled;
};
extern std::string g_stageDef;
extern std::vector<FrameBg> g_frameBgs;
// The camera the frame was drawn with and the stage's limits, in the world (a character's
// pos * localscl); see editorStageGeometry in editor_stage.go
struct StageView {
    bool ok = false, free = false;
    float cam[7] = {}; // x, y, scale, ground level, gameWidth, widthScale, heightScale
    float lim[13] = {}; // localscl, boundL, boundR, boundH, boundLo, minLeft, maxRight, above, below,
                        // leftbound, rightbound, camera startx, starty
    std::vector<ImVec2> starts; // P1..P4
    int time = 0, ranAt = 0;    // the stage's time; the stage time of its last step
    struct Ctrl {
        bool ran;               // on that step
        int start, end, loop;   // time = start, end, looptime (as the engine has them)
        std::vector<int> bgs;   // the BGs it acts on
    };
    std::vector<Ctrl> ctrls;    // the def's [BGCtrl ...] sections, in order
    ImVec2 ToPicture(float x, float y) const {
        return ImVec2(((x - cam[0]) * cam[2] + cam[4] / 2) * cam[5], (y * cam[2] - cam[1] + cam[3]) * cam[6]);
    }
};
extern StageView g_stageView;
extern bool g_stagePick; // the Game window picks and drags the stage's BGs, not the players
extern int g_stageBg;    // the BG selected, -1 none
// On the Game window: the BGs' outlines; in pick mode, hover, click and drag. True when it has
// the mouse. scale: screen pixels per picture pixel.
bool StageOverlay(ImDrawList *draw, bool hovered, bool clicked, const std::function<ImVec2(float, float)> &toScreen,
                  ImVec2 scale);
// With the free camera on, the Game window's wheel and right button move it; true then (the
// window's own zoom and pan stay put). pictureAt: a screen point to the game picture's pixels.
bool StageCameraInput(bool hovered, ImVec2 picturePerScreen, const std::function<ImVec2(ImVec2)> &pictureAt);
void StageWindow();

// Fight screen and Motif: where each element of their defs (fight.def, system.def) was drawn in
// the last frame (editor_fight.go, editor_motif.go)
struct FrameFightItem {
    std::string section; // lower case, as "simul lifebar"
    std::string prefix;  // of the def's keys it was read from, lower case, as "p1.bg0."
    ImVec4 rect;         // x, y, w, h in the game picture's pixels
    ImVec2 k;            // picture pixels per unit of the def's positions
    bool bg = false;     // a BG of a BGDef (its section found in the def; it moves by its start)
    int action = -1;     // the action of the def it shows ([Begin Action]), -1: none
    int elem = 0;        // and its element, 1-based
    int tag = 0;         // where the scripts drew it (a select screen's cell: 1 + its grid index), 0 none
    int font = 0;        // a text's font: 1 + its index in g_fontFiles, 0 none
};
// The element selected in the Fight screen, Motif or Storyboard window, when it shows an action
// of its def: for the Animation window, which edits the action in the def's text
struct ScreenAnim {
    SourceFile *sf = nullptr;
    SpriteFile *sprites = nullptr; // the screen's SFF, nullptr: not known
    std::string label;           // as "[Lifebar] p1.front50 (fight screen)"
    int action = -1, elem = 0;   // shown now (elem 0: not on screen)
    std::function<void()> apply; // after an edit of sf's text: the game shows it
};
bool SelectedScreenAnim(ScreenAnim &out);
extern int g_screenAnimPicked; // bumped when such an element is picked
extern int g_screenPicked;     // bumped when any element of a screen is picked
// The SFF of the screen whose element was picked last; what: as "motif"; sprite: the index of the
// one that element shows, -1 not known
SpriteFile *SelectedScreenSprites(const char **what = nullptr, int *sprite = nullptr);
void ScreenSpritesSaved(const std::string &path); // the screens using that SFF read it again

void ReadScreenGeometry(const Json &j); // the frame geometry's "fight" and "motif"
extern bool g_screenPick; // the Game window picks and drags their elements, not the players
// On the Game window, as StageOverlay
bool ScreenOverlay(ImDrawList *draw, bool hovered, bool clicked, const std::function<ImVec2(float, float)> &toScreen,
                   ImVec2 scale);
void ScreenWindows(); // Fight screen, Motif
const IniSection *MotifSection(const char *lower); // a section of the motif's def, nullptr if none

// Roster: the motif's select.def
extern std::string g_selectDef; // as the game names it (frame geometry), "" when not known
extern int g_rosterPick;        // a select screen's cell picked on the Game window: 1 + its grid index
void OutlineSelectCell(int tag); // the Game window outlines that cell (1 + its grid index)
void RosterWindow();
std::vector<std::string> ListDefs(bool stages); // characters as select.def names them, or stages' defs

// Lab: a dummy, takes of a player's input recorded and played, characters swapped in a match
void OnLabState(const Json &lab); // the snapshot's "lab" (null: nothing going on)
void OnTake(const Json &j);       // @IKTAKE: a take recorded
void LabSettingsLine(const std::string &key, const std::string &value);
void LabSettingsWrite(ImGuiTextBuffer *buf);
void LabHello();                  // the game started: its dummy again
void LabWindow();
// Keys as a fighting game writes them ("DF+a"); bits 0-13: U, D, B, F, a, b, c, x, y, z, s, d, w, m
std::string KeysText(int bits);

// Fonts: the font files loaded (frame geometry), their defs and glyphs
extern std::vector<std::string> g_fontFiles; // as the game names them; a text's font is 1 + an index
extern std::string g_fontPicked;             // the font of the text picked last on the Game window
void FontSpritesSaved(const std::string &path); // the fonts using that SFF are read again
void FontsWindow();

// Session, Control, Players, Inspector, Frame data, Trace, Breakpoints, Console
void SessionWindow();
void ControlWindow();
void PlayersWindow();
void InspectorWindow();
void FrameDataWindow();
void TraceWindow();
void BreakpointsWindow();
void WatchWindow();
void ConsoleWindow();

} // namespace app
