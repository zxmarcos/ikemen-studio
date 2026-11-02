// What the debugger's files share: its state and the functions of each window's file.
//   app.cpp          state, commands to the game, what the game sends, settings
//   app_layout.cpp   the menu bar, the windows opened and closed, the workspaces, the status bar
//   app_explorer.cpp Explorer: the projects' files from disk, a file opened in its window
//   app_palette.cpp  the palette (go to a file, a command, a line, a section) and the File menu
//   app_navigate.cpp Search (find in files, references) and Outline
//   app_problems.cpp Problems: Lint, the stage's checks, Apply, the engine's warnings
//   app_game.cpp     Game: the game's frames, the players' boxes over them, keys to the game
//   app_anim.cpp     Animation: the AIR element of the selected player
//   app_sprites.cpp  Sprites: the SFF file
//   app_sounds.cpp   Sounds: the SND file
//   app_code.cpp     Code: the source files, Apply
//   app_cmd.cpp      Commands: input, the commands under way, the CMD file
//   app_stage.cpp    Stage: the stage's def, its BGs picked and dragged on the Game window
//   app_model.cpp    Model: the stage's 3D model, its nodes outlined and picked on the Game window
//   app_modelview.cpp the Model window's preview of a model read from disk (gltf.cpp)
//   app_screen.cpp   Fight screen, Motif: their defs, their elements picked and dragged on the Game window
//   app_roster.cpp   Roster: select.def's characters in the select screen's grid, its stages
//   app_lab.cpp      Lab: a dummy, takes of input, characters swapped
//   app_create.cpp   Create: a move
//   app_debug.cpp    Session, Control, Players, Inspector, Frame data, Trace, Breakpoints, Console
#pragma once

#include <algorithm>
#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "bridge.h"
#include "frame.h"
#include "air.h"
#include "sff.h"
#include "snd.h"
#include "ini.h"
#include "stagecheck.h"
#include "gltf.h"
#include "undo.h"
#include "editor.hpp"
#include <GL/gl.h>
#include "imgui.h"
#include "json.h"
#include "icons.h"

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
    enum Kind { Info, Result, Error, Engine, Break, Detail } kind; // Detail: for the log only, not the status bar
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
void CodeShow(const std::string &path); // Code shows that file and stops following the state; "": follows it
// Code shows that file in a tab of its own (keep), or in the preview tab, which the next file
// shown replaces
void CodeOpen(const std::string &path, bool keep);
std::string FightScreenDef();           // the fight screen's def, as the game names it ("" not known yet)
std::string MotifDef();                 // the motif's def, the same way

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
extern std::string g_note; // the last result logged, shown for a while on the status bar
extern bool g_noteError;
extern double g_noteAt;
std::string Who(int pn, bool helper, int id);
const Entity *FindEntity(int id);
const FrameEntity *FindFrameEntity(int id);
std::string NormPath(const std::string &path);
std::string ExeDir();
std::string ReadWholeFile(const std::string &path);
void HelpMarker(const char *text);
void SameLineFits(float width);         // SameLine when an item this wide still fits the line, else a new line
bool IconButton(const char *label);     // a button of an icon alone: square, whichever the icon
float CheckboxWidth(const char *label); // what ImGui::Checkbox takes
// The windows: the menu bar, each one opened or closed, the layouts (app_layout.cpp)
bool BeginTool(const char *name, ImGuiWindowFlags flags = 0); // ImGui::Begin; false (End called): closed or hidden
void ShowWindow(const char *name); // opened if closed, its tab to the front, without taking the keyboard
enum PickMode { kPickFighters, kPickStage, kPickScreen }; // what a click on the Game window selects
void SetPickMode(PickMode mode);
bool AdvancedTools();              // the tools for looking inside a character are shown (View > Advanced tools)
void SetAdvancedTools(bool on);
void LayoutFrame();                // the menu bar and the dock space, at the start of a frame
void StatusBar();                  // the bar at the bottom, at the end of a frame
const char *ToolName(int n);       // the windows, in the Windows menu's order; nullptr past the last
const char *WorkspaceName(int n);  // nullptr past the last
void AskWorkspace(int n);          // shown from the next frame; -1: the current one's arrangement restored
void LayoutSettingsLine(const std::string &key, const std::string &value);
void LayoutSettingsWrite(ImGuiTextBuffer *buf);
// The Performance window (app_perf.cpp)
void PerformanceWindow();
void OnPerf(const Json &j);
void AppFrameTime(double ms); // this program's work in its last frame
// The themes (app_theme.cpp)
enum InkColor { kRed, kYellow, kGreen, kBlue, kOrange };
ImVec4 Ink(InkColor c); // a colour for text on a window: darker on a light theme
bool LightTheme();
void ThemeMenu();
const char *ThemeName(int n); // nullptr past the last
void AskTheme(int n);
void ThemeFrame(); // applies a theme asked for, at the start of a frame
void ThemeSettingsLine(const std::string &key, const std::string &value);
void ThemeSettingsWrite(ImGuiTextBuffer *buf);

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
void Launch(bool menus = false); // menus: without -p1 -p2, the game starts in its menus

// ---------------------------------------------------------------------------
// The windows
// ---------------------------------------------------------------------------

// Go to: the game put on a screen and kept there (app_goto.cpp)
void GoToButton();              // the button and its popup, on the Game window
void GoToStory(const std::string &def); // the game plays that storyboard, stopped at its start
void GoToLine();                // on the same line: where it is held, or going
void GoToFrame();               // each frame of the game: a moment being waited for
void GoToLaunched(bool menus);  // the game was started, in its menus or in a quick match
void OnGoTo(const Json &j);     // @IKGO

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
extern bool g_boxEdit, g_followSel, g_onion, g_onionSprite, g_onionBefore, g_onionAfter, g_animFollow;
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

// A drag of one of the element's boxes: an edge, a corner or the inside.
struct BoxDrag {
    bool active = false;
    int g = 0, i = 0, edges = 0; // edges: 1 left, 2 right, 4 top, 8 bottom (screen); 15 move
    ImVec2 start;                // mouse, in AIR units
    AirBox box;                  // when the drag started
};
// The element's boxes drawn with handles, dragged; written to the AIR text when released. flip:
// the screen's x goes the other way (facing left). hit: the box under the mouse (g * 1000 + i), or
// -1. Returns true while it has the mouse.
bool BoxHandles(BoxDrag &d, ImDrawList *draw, bool hovered, const AnimTarget &t,
                const std::function<ImVec2(float, float)> &airToScreen, const std::function<ImVec2(ImVec2)> &screenToAir,
                bool flip, int *hit = nullptr);
// The onion skin: the element (0-based) before index (dir -1: the last one's before the first) or
// after it (dir 1: the loop start's after the last); the tints of their sprites
int OnionElem(const AirAction &act, int index, int dir);
const ImU32 kOnionBefore = IM_COL32(255, 175, 120, 110), kOnionAfter = IM_COL32(120, 235, 150, 110);

void OnFiles(const Json &j);
extern std::map<int, std::pair<std::string, double>> g_swapWanted; // pn -> the character swapped in, when
std::string AirPath(int pn);
std::string CharFile(int pn, const char *key);
const AirFile &AirOf(const SourceFile &sf);
AnimTarget GetAnimTarget();
bool AnimShowLine(const std::string &path, int line); // the action and element at that line of an AIR file, in Animation
void ApplyAnims(SourceFile &sf);
// Animation's New action or (add) Add elements dialog with those sprites (group, number) chosen, for a player's
// AIR file (pn > 0) or that file of actions, which it shows
void AnimAskSprites(bool add, int pn, const std::string &file, const std::vector<std::pair<int, int>> &sprites);
// Animation shows a file of actions by itself: a def's, an AIR file no character uses; "": the
// project's character. action -1: its first one. show: its window comes to the front.
void AnimOpenFile(const std::string &path, int action = -1, int elem = 1, bool show = true);
void AnimFollowFile();                                 // and goes on showing what the game shows of it
// A sprite as the game draws it: flip (which mirrors the offset too), scale and angle around its axis,
// then the blending. at: a place from the element's origin to the screen
void DrawStyledSprite(ImDrawList *draw, GLuint tex, const SffSprite &s, const AirStyle &st, float ex, float ey,
                      const std::function<ImVec2(float, float)> &at);
std::string AnimFileOfSprites(const std::string &sff); // the file of actions those sprites are for, "" none
void ActionList(SourceFile &sf);                       // a def's actions as rows, each opening in Animation
// A file's text followed by undo, shared by the windows editing it: a change is a step when the mouse lets go
UndoStack<std::string> &TrackAnims(const SourceFile &sf);
UndoStack<std::string> &TrackText(const std::string &path, const std::string &text, bool saved); // the same for any text
void AnimationWindow();
// What a def's fields name (app_refs.cpp). The column beside a field's value: its sprite or action as a
// thumbnail, which picks another (true: *value is the field's new value), or its sound played
bool FieldRefCell(SourceFile &sf, const IniSection &s, const IniField &f, std::string *value);
void ActionUsersNote(SourceFile &sf, int action); // on the same line: how many fields of the def name that action

// New layers and animations (app_layers.cpp). An animation asked for: images, sprites of the SFF, or an action
struct NewAnim {
    std::vector<std::string> files;
    std::vector<std::pair<int, int>> sprites;
    int action = -1;
    int time = 5; // of each frame of a new one
};
struct SpriteFile;
// What makes one, in a popup: true once chosen. listActions: those of the def too.
bool AnimChooser(SourceFile &sf, SpriteFile *spf, NewAnim *out, bool listActions);
// The action it shows, -1 none: a new one of the text, with the images in the SFF, which is not saved.
int MakeAnim(SourceFile &sf, SpriteFile *spf, const NewAnim &a, const std::string &name);
// After images went in an SFF, so that the game has them; others: it had other sprites not saved, which wait
void SaveAddedSprites(SpriteFile &sprites, bool others);
// A def's BGs, by group (ini.h). The edit is made as the next frame starts, since what was parsed is in use
// until then: it gives the BG to select, -1 none.
using BgEdit = std::function<int(SourceFile &)>;
struct BgHost {
    std::string group = "BG";
    int selected = -1;                                    // among the group's BGs
    const char *layers = "Behind the fighters\0In front of the fighters\0";
    std::function<const IniFile &(SourceFile &)> parsed;  // the text as it is now
    std::function<void(SourceFile &)> edited;             // the game shows the text
    std::function<bool(const std::string &)> showsSff;    // saving that SFF has the game read the text too
};
void BgToolbar(SourceFile &sf, const IniFile &ini, const BgHost &h, BgEdit &edit);

// Sprites
// What undo and redo put back in a sprite file (the sprites share their pictures with the file's)
struct SpriteState {
    std::vector<SffSprite> sprites;
    std::vector<std::shared_ptr<const SffPal>> pals;        // one not changed is shared with the state before
    std::map<std::string, std::vector<unsigned char>> acts; // its ACT files' colours, by NormPath
};

struct SpriteFile {
    SffDoc doc;
    std::string path;   // as the game names it
    std::string pal;    // the ACT file of an SFF v1's shared palette (a character's pal1), or ""
    bool dirty = false;
    unsigned version = 1; // bumped by every change (the textures follow)
    int sel = -1;
    std::set<int> picks;  // sprites selected with Ctrl or Shift, sel among them; empty: sel alone
    UndoStack<SpriteState> history;
    unsigned edits = 0, recorded = 0; // changes made, and those the history has
};

SpriteFile &SpriteFileAt(const std::string &path, const std::string &pal = ""); // read the first time
SpriteFile *SpritesOf(int pn);
// The Sprites window shows that file (at that sprite, -1 as it was); what: as "the font x.def"
void ShowInSprites(SpriteFile &sf, int index, const std::string &what);
void SpritesSelect(SpriteFile &sf, int index); // that sprite selected and in view, -1 nothing changes
// The sprites of a group as a grid, curGroup, curImage marked; true when one is clicked. SpriteGridSize,
// before the popup holding it, keeps it as wide from the start, so that it opens inside the window
bool SpriteGrid(SpriteFile *spf, int &group, int curGroup, int curImage, int *pickGroup, int *pickImage);
void SpriteGridSize();
// Sprites chosen to make elements of, in their order
struct SpritePicks {
    int group = 0;                            // the group shown
    std::vector<std::pair<int, int>> sprites; // group, number
    std::pair<int, int> from = {-1, -1};      // where a Shift click starts
};
// The groups at the left, the sprites of the one clicked at the right, numbered by their place among those
// chosen (a click, Ctrl, Shift, all of a group), height tall. True on a double click on a sprite: that one alone.
bool SpritePicker(SpriteFile *spf, SpritePicks &p, float height);
GLuint SpriteTexture(SpriteFile &sf, int index, int pal = -1);
ImTextureRef TexRef(GLuint tex);
void Checkerboard(ImDrawList *draw, ImVec2 a, ImVec2 b);
std::vector<std::string> PickFiles(const char *filter, bool multi);
std::string PickSaveFile(const char *filter, const char *ext, const std::string &name);
void SpritesChanged(SpriteFile &sf);
void SaveSprites(SpriteFile &sf);
// Images as new sprites, numbered from 0 in a group of their own, each axis at its top centre as a stage's
// layers have it: false when none could be read. numbers: the group and number of each one made
bool AddSpritesOfImages(SpriteFile &sf, const std::vector<std::string> &files, std::vector<std::pair<int, int>> &numbers);
extern const char kImageFilter[];
int FreeGroup(const SffDoc &d); // a group no sprite has, after those in use (9000 and up, portraits, do not count)
// The sprite sheet tool (app_sheet.cpp): an image picked, then its dialog, which the Sprites window draws
void SheetAsk(SpriteFile &sf);
void SheetDialog(SpriteFile &sf, bool character);
void SpriteLog(const std::string &s); // a line of the log
std::vector<int> SelectedSprites(const SpriteFile &sf); // those selected, else the one shown
// Undo and redo as two small buttons, with Ctrl+Z, Ctrl+Y and Ctrl+Shift+Z while the window has the
// keyboard: -1 undo, 1 redo, 0 neither. fromGame: also while Game has it, for what is dragged there.
// UndoButtonsWidth: what they take.
int UndoButtons(bool canUndo, bool canRedo, bool fromGame = false);
extern bool g_gameFocused; // the Game window has the keyboard
float UndoButtonsWidth();
void SpritesWindow();
extern std::map<int, int> g_palNo; // pn -> the palette the game gave the player (1..), as @IKFILES tells

// Palettes (app_sffpal.cpp): what sprites can be seen with, and the palette editor
struct PalChoice {
    std::string label;
    std::string act;  // a character's ACT file (its def's palN), "" none
    int sff = -1;     // index in the SFF's palettes (the act's 1,N when it has one)
    int slot = 0;     // a character's palette N (1,N), 0 another
    bool own = false; // each sprite with its own palette
};
// The palettes of an SFF: "its own"; a character's palettes (pn > 0) from its ACT files, else the
// SFF's 1,N; the SFF's other palettes.
void PalChoices(SpriteFile &sf, int pn, std::vector<PalChoice> &out);
// The palette sprite is drawn with when c is chosen: c for the sprites using the shared palette
// (1,1; every paletted sprite for a palette that is not a character's), else the sprite's own.
PalChoice PalFor(SpriteFile &sf, const PalChoice &c, int sprite);
// That sprite drawn with that choice; mark: colours flashed (256 flags), nullptr none.
GLuint PalSpriteTexture(SpriteFile &sf, int sprite, const PalChoice &c, const std::vector<char> *mark = nullptr);
int SpritePixel(SpriteFile &sf, int sprite, int x, int y); // its palette index there, -1 none (true colour, outside)
bool PalDirty();               // ACT files changed and not saved
void PalSave();                // writes them (the first time the old one is kept as .bak)
void PalRevert(SpriteFile &sf, int pn); // reads its ACT files again; the game gets them back
// The palettes of a state for undo (before: the state before, whose palettes not changed it shares,
// nullptr none), and those of a state put back, what changed sent to the game.
void PalCapture(SpriteFile &sf, int pn, SpriteState &st, const SpriteState *before);
void PalRestore(SpriteFile &sf, int pn, const SpriteState &st, bool saved);
void PalFrame(SpriteFile &sf, int pn);  // sends the colours changed to the game (now and then while they change)
// The Palette tab: the colours of target, picked, edited. picked: a colour picked on the sprite
// (-1 none), shift: added to the selection. Fills mark with the colours selected.
void PalettePanel(SpriteFile &sf, int pn, const PalChoice &target, int picked, bool shift, std::vector<char> &mark);
std::string PalTakeWanted(); // the label of a palette picked in the Palette tab's list, once; "" none
const std::vector<unsigned char> *PalColours(SpriteFile &sf, const PalChoice &c); // its 256 RGBA colours, nullptr none
// Paint (app_paint.cpp): on the Sprites window's preview, the sprite's pixels edited. at: the picture's corner
// on the screen, zoom: screen pixels for each of its; clipA, clipB: the canvas. False: this sprite can't be painted.
bool PaintCanvas(SpriteFile &sf, const PalChoice &view, ImDrawList *draw, ImVec2 at, float zoom, bool hovered, ImVec2 clipA, ImVec2 clipB);
void PaintPanel(SpriteFile &sf, const PalChoice &view); // the Paint tab

// Sounds
const SndFile *SndOf(int pn); // a player's SND file, nullptr until known (or unreadable)
const SndFile *SndAt(const std::string &path); // an SND file, read the first time; nullptr unreadable
// The Sounds window shows that file (at that sound, -1 as it was); what: as "fight screen"
void ShowInSounds(const std::string &path, int index, const std::string &what);
void PlayWav(const std::string &wav);
void SoundsWindow();

// Analysis: a character's states checked (Lint), its variables, the graph of its states
void AnalysisWindow();
std::string VarName(const std::string &def, char kind, int index); // "" without one ('v', 'f', 's', 'S')
// For the Code window's tooltip of a word (byte offsets in the file's text): a variable's name and
// uses, the state or action a number names; "" nothing
std::string CodeHoverNote(const std::string &file, size_t begin, size_t end);
std::string StatedefFile(int pn, int no); // the character's file whose [Statedef no] the engine reads first; "" none
void AnalysisSettingsLine(const std::string &key, const std::string &value);
void AnalysisSettingsWrite(ImGuiTextBuffer *buf);

// Code
extern bool g_applying;          // waiting for the game's answer to Apply
extern std::string g_applyError; // of the last Apply that failed
std::string GamePath(const std::string &file);
SourceFile &LoadSource(const std::string &file);
void ReadSource(SourceFile &sf);
bool SaveSource(SourceFile &sf); // the first time, the old file is kept as .bak
void SaveAndApply(); // saves everything changed (SaveAll), the game compiles its characters again
void ApplyToGame();  // only the last part

// What has changes not saved, of every kind of file (app_unsaved.cpp)
struct Unsaved {
    std::string path;
    std::function<bool()> save; // false when it could not be written
};
std::vector<Unsaved> UnsavedList();
void PalUnsaved(std::vector<Unsaved> &out);
void SpritesUnsaved(std::vector<Unsaved> &out);
void SoundsUnsaved(std::vector<Unsaved> &out);
void FontsUnsaved(std::vector<Unsaved> &out);
void ModelUnsaved(std::vector<Unsaved> &out);
bool SaveAll(); // false when one could not be written
// On a file's line, what it says about its changes (live: the game shows them before they are saved),
// short when it would leave less than keep for what follows on that line
void SaveState(bool dirty, bool live, float keep = 0);
void UnsavedAsk(bool closing); // the dialog listing them; closing: the window closes after it
void UnsavedDialog();
void OnReload(const Json &j);
FILETIME FileTime(const std::string &path);
// The file shown and the cursor's line and column (1-based); false when none is shown
bool CodeCursor(std::string *file, int *line, int *col);
void CodeCloseShown(); // its tab (asks first when it has changes)
// A line of a file in Code, [begin, end) of its text selected when not empty
void CodeShowLine(const std::string &file, int line, size_t begin = 0, size_t end = 0);
std::string CodeSelectedText(); // the text selected in Code when on one line, else ""
void CodeSettingsLine(const std::string &key, const std::string &value);
void CodeSettingsWrite(ImGuiTextBuffer *buf);
void CodeWindow();

// Explorer: the characters, stages and screenpack as trees of their files, from disk
bool GameFileExists(const std::string &path);
bool IsTextFile(const std::string &path); // not one no window edits as text (sff, snd, png...)
// Opens a file in the window that edits it: Code (text), Sprites (pal: an SFF v1's palette), Sounds
void OpenPath(const std::string &path, bool keep, const std::string &pal = "");
void ProjectFiles(std::vector<std::string> &out); // every file of the projects, for Go to file
std::string ScreenpackDef();                        // the motif's def: the game's, else config.ini's
bool DefNamesFile(const std::string &def, const std::string &name); // the file a def names is found
std::string SpritesFileOf(const std::string &file); // the SFF of a def, or of an AIR file by itself; "" none
std::string DefFile(const std::string &def, const char *key); // the file a def's key names, found; "" none
void ExplorerWindow();
// The project (app_project.cpp): what is being worked on, one def at a time. Each kind keeps its own
// and the workspace says which is in use: Test, Code and Character a character's, Stage and
// Screenpack theirs. Nothing opened: what the match uses.
enum ProjectKind { kProjectCharacter, kProjectStage, kProjectScreenpack, kProjectKinds };
struct ProjectRecent {
    ProjectKind kind;
    std::string def;
};
ProjectKind CurrentProject();                           // the workspace's (app_layout.cpp)
void AskProjectWorkspace(ProjectKind kind);             // a workspace of that kind, unless one is shown
std::string ProjectDef(ProjectKind kind);               // "" none
std::string ProjectName(ProjectKind kind, const std::string &def); // as the toolbar shows it
// It becomes the project; enter: the match's too (at once, or from the next Launch)
void ProjectOpen(ProjectKind kind, const std::string &def, bool enter = true);
bool OfProject(int pn);                                 // that player is the character open
bool SelectedOfProject();                               // the fighter selected is it, or shows its animation
int ProjectFighter(int pn);                             // its fighter: the one selected, else the player's root; -1 none
const std::vector<ProjectRecent> &ProjectRecents();     // the last ones, for going back to them
void ProjectBar();                                      // on the toolbar: its name, where it is, others to open
void ProjectDialog();                                   // another screenpack: the game has to start again
void ProjectSettingsLine(const std::string &key, const std::string &value);
void ProjectSettingsWrite(ImGuiTextBuffer *buf);
// The character the windows work on (Analysis, Sounds, Commands, Sprites): the project, when the
// workspace is a character's. Its player number; one not in the match is read from disk as kDiskPn.
// 0: none.
const int kDiskPn = 1000;
int WorkPn();
std::string WorkDef();          // its def, "" none
std::string WorkLabel(int pn);  // "P1", "from disk"
int PlayerOf(const std::string &def); // the player a character's def is in the match as, 0 none
std::string CharDefOf(const std::string &name); // a character as ListDefs names it ("kfm", "kfm/kfm720.def"): its def
std::string CharNameOf(const std::string &def); // and the other way
std::string PackageTarget();                    // the def File > Package takes, "" none
void SetScreenpack(const std::string &def);     // the game starts with it, from the session's -r
void PackageWorkedOn();
void PackageDialog();                           // how the last package went
void HelpAsk(int what);                         // the keys (0) or the words (1)
void HelpMenu();
void HelpDialog();
void PlayCharacter(const std::string &def);     // it becomes P1: of the match, or of a game started now
// A name from a list, with a filter: true when one is picked. none: an entry for no name.
bool NameCombo(const char *id, const std::string &shown, const std::vector<std::string> &names, std::string &picked,
               const char *none = nullptr);
// A player's files as the game lists them (key -> paths), kDiskPn's read from disk; nullptr not known
const std::map<std::string, std::vector<std::string>> *FilesFor(int pn);

// Navigation: Search (find in files, a state's references) and Outline (app_navigate.cpp); go to
// definition (app_check.cpp)
struct CodePlace {
    std::string file;
    int line = 0;
    std::string text, note; // the line; after it, dimmed
};
// The matches of pat in text, sorted (begin, end) byte offsets
void FindAll(const std::string &text, const std::string &pat, bool matchCase, bool word,
             std::vector<std::pair<size_t, size_t>> &out);
void SearchFor(const std::string &text); // Search comes to the front with that text ("": as it was)
void ShowReferences(const std::string &title, const std::vector<CodePlace> &places);
void SearchWindow();
void OutlineWindow();
// What the word at an offset of a file names (a state, an action, a command, a ZSS function):
// shown in Code; on a statedef, or with references, the places changing to that state, in Search.
// False (logged) when nothing.
bool GoToDefinition(const std::string &file, size_t at, bool references);

// Problems (app_problems.cpp): Lint, the stage's checks, Apply's error, the engine's warnings
struct Problem {
    int level;          // 0 error, 1 warning, 2 note
    const char *source; // "Lint", "Stage", "Apply", "Engine"
    std::string file;   // "" none (the engine's: maybe a name alone, found among the players' files)
    int line = 0;
    std::string text;
    int count = 1;      // the engine's: times logged
};
void EngineLine(const std::string &text); // a line of the engine's log: kept when a warning or an error
void ToolError(const std::string &text);  // an error of the debugger's own, also in Problems
void ClearEngineProblems();
void LintProblems(std::vector<Problem> &out);  // app_check.cpp: the project's character
void StageProblems(std::vector<Problem> &out); // app_stage.cpp: the match's stage, or the toolbar's
void ProblemCounts(int count[3]);              // errors, warnings, notes
void ProblemsWindow();

// The palette (Ctrl+P files, Ctrl+Shift+P commands, Ctrl+G a line, Ctrl+Shift+O a section) and
// the File menu (app_palette.cpp)
void FileMenu();
void PaletteFrame();

// app_start.cpp
void AskStartPage();
void AskTour();                        // the first steps, in a corner of the window
void StartPageFrame();
void EmptyNote(const char *text);       // an empty window: what goes there
void StartSettingsLine(const std::string &key, const std::string &value);
void StartSettingsWrite(ImGuiTextBuffer *buf);

// Commands
// A ChangeState to state on command at the top of the [Statedef -1] the engine reads first (a new
// one in the CMD file without any): the file it went in ("": not done), line where it starts.
// when: 0 any statetype, 1 S, 2 C, 3 A; extra: one more condition or "".
std::string AddCommandEntry(int pn, const std::string &command, int state, int when, bool ctrl, const std::string &extra,
                            int *line);
std::vector<std::string> CommandNames(int pn); // its CMD file's, once each, sorted
void CommandsWindow();

// File > New: a dialog for each thing made
enum { kNewMove, kNewCharacter, kNewStage };
void CreateMenu();
void CreateAsk(int what);
void NewDialog();

// Stage: the stage's def file and where its BGs were drawn in the last frame (editor_stage.go)
struct FrameBg {
    int index;   // in the stage's BGs (the def's [BG ...] sections, in order)
    ImVec4 rect; // x, y, w, h in the game picture's pixels: the sprite once, untiled
    ImVec2 k;    // picture pixels per unit of its start
    int layer;
    bool tiled;
    int action, elem; // the action of the def it shows, -1 a sprite; its element, 1-based
};
extern std::string g_stageDef, g_stageSff; // as the game names them
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
std::string StageSelection();              // the BG selected, as text
extern int g_stageBg;    // the BG selected, -1 none
// On the Game window: the BGs' outlines; in pick mode, hover, click and drag. True when it has
// the mouse. scale: screen pixels per picture pixel.
bool StageOverlay(ImDrawList *draw, bool hovered, bool clicked, const std::function<ImVec2(float, float)> &toScreen,
                  ImVec2 scale);
// With the free camera on, the Game window's wheel and right button move it; true then (the
// window's own zoom and pan stay put). pictureAt: a screen point to the game picture's pixels.
bool StageCameraInput(bool hovered, ImVec2 picturePerScreen, const std::function<ImVec2(ImVec2)> &pictureAt);
void StageWindow();
SourceFile *StageSource();                   // the stage project's def, nullptr when not known
bool StageInMatch();                         // it is the match's: only then does anything go to the game
const IniFile &StageIni(const SourceFile &sf); // parsed, again when its text changes
void StageEdited(SourceFile &sf);             // after an edit of its text: the game shows it
void ReloadStage(SourceFile &sf, bool fromText, bool sprites = false);
void StageSpritesSaved(const std::string &path); // the stage using that SFF reads it again
void StageOpen();                                // another stage is open: nothing of the last one stays selected
void PlayStage(const std::string &def);          // it becomes the toolbar's stage, and the match's when one is on
std::string StageNameOf(const std::string &def); // as the toolbar names it: "stage0"
// A section of a def's checks, each row showing its line in Code. sffRead: else a note that sprites were not checked
void FindingsSection(const SourceFile &sf, const std::vector<StageFinding> &found, bool sffRead);
// The stage's 3D model (app_model.cpp): the Model window, a line of the Stage window, and on the
// Game window its nodes outlined and picked
void OnModel(const Json &j);
void OnModelKeys(const Json &j); // an animation's keys (ikdbg.modelKeys)
void ModelSection(bool stageHasModel);
void ModelWindow();
std::string ModelFileInUse();                 // the model file of the stage in the match, as the game names it; "" none
void ModelOpenFile(const std::string &path);  // a .glb or .gltf shown in the Model window (from disk unless the match's)
std::string StageFile(const std::string &def, const std::string &name); // a file a stage's def names, found; "" none
std::string SettingsStageDef();               // the toolbar's stage as a def path; "" none
std::string StageDefOfModel(const std::string &model); // the stage's def naming that model, "" none (Explorer)
// The Model window's preview of a model from disk (app_modelview.cpp)
bool DecodeImage(const unsigned char *data, size_t size, int &w, int &h, std::vector<unsigned char> &rgba); // WIC
GltfMat Perspective(float fovy, float aspect, float zNear, float zFar);
GltfMat LookAt(const float eye[3], const float at[3]);
struct ModelPreviewArgs {
    const GltfDoc *doc = nullptr;
    unsigned version = 0; // the doc's read: its textures are uploaded again when it changes
    const std::vector<GltfMat> *world = nullptr;
    const std::vector<char> *visible = nullptr;
    GltfMat proj{}, view{};
    int w = 0, h = 0, selected = -1, hover = -1;
    int lighting = 0; // 0 unlit, 1 the model's lights, 2 an environment map (and the lights)
};
unsigned DrawModelPreview(const ModelPreviewArgs &a); // the picture's texture, 0 when it cannot be drawn
void ReadModelGeometry(const Json &mdl); // the frame geometry's stage "mdl"
extern bool g_modelPick;                 // the Game window picks the model's nodes, not the players
void ModelPickToggle();                  // its checkbox on the Game window's toolbar
bool ModelPicking();                     // the Game window picks the model's nodes
void ModelPickOff();
bool ModelOverlay(ImDrawList *draw, bool hovered, bool clicked, const std::function<ImVec2(float, float)> &toScreen,
                  const std::function<ImVec2(ImVec2)> &toPicture);

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
    bool disk = false;           // a file the game is not using
    std::function<void()> apply; // after an edit of sf's text: the game shows it
};
bool SelectedScreenAnim(ScreenAnim &out);
std::string ScreenFileName(const std::string &def);      // what the game uses that def as ("stage"), "" not in use
bool ScreenShowsAction(const std::string &def, int action, int *elem); // something of that def in use shows it now
bool ScreenItemShown(const char *section, const char *prefix); // an element of the fight screen is on screen: its prefix starts so
void ScreenTextEdited(SourceFile &sf);                   // that def's text changed: the game shows it, if it is using it
void ScreenFileSaved(const std::string &path);           // a file a screen reads besides its def (fightfx.air) was saved
std::string ScreenDefOfSprites(const std::string &sff);  // the def in use whose SFF that is, "" none
std::string ScreenSpritesOf(const std::string &def);     // the SFF the game has for a def in use, "" none
void StageBgAnim(const std::string &def, int &action, int &elem); // what the BG selected shows, when def is the stage's
extern int g_screenAnimPicked; // bumped when such an element is picked
extern int g_screenPicked;     // bumped when any element of a screen is picked
// The SFF of the screen whose element was picked last; what: as "motif"; sprite: the index of the
// one that element shows, -1 not known
SpriteFile *SelectedScreenSprites(const char **what = nullptr, int *sprite = nullptr);
void ScreenSpritesSaved(const std::string &path); // the screens using that SFF read it again
void ScreenSoundsSaved(const std::string &path);  // the same for an SND

// The Storyboard window's scenes (app_story.cpp): what the window hands them
struct StoryHost {
    SourceFile *sf = nullptr;
    const IniFile *ini = nullptr;  // sf's text parsed
    SpriteFile *sprites = nullptr;
    bool playing = false, paused = false; // the game is showing this def, stopped
    int scene = 0, tick = 0;              // where in it
    std::string section, prefix;          // the element selected (lower case)
    std::function<void(const std::string &section, const std::string &prefix)> select;
    UndoStack<std::string> *history = nullptr; // of sf's text
};
bool StoryPanel(StoryHost &h);                // the preview and the timelines; true when the text changed
void StoryPicker();                           // which storyboard the window shows, from disk
void StoryOpen(const std::string &def);       // it shows that one; the game not playing another
const std::string &StoryOpened();             // the one chosen, "" none
// The storyboards the motif and the characters name: (who names it, its def)
std::vector<std::pair<std::string, std::string>> StoryboardFiles();

void ReadScreenGeometry(const Json &j); // the frame geometry's "fight" and "motif"
extern bool g_screenPick; // the Game window picks and drags their elements, not the players
std::string ScreenSelection(const char **window); // the element selected, as text, and the window that edits it
// On the Game window, as StageOverlay
bool ScreenOverlay(ImDrawList *draw, bool hovered, bool clicked, const std::function<ImVec2(float, float)> &toScreen,
                   ImVec2 scale);
void ScreenWindows(); // Fight screen, Motif
void ScreenProblems(std::vector<Problem> &out); // the checks of the fight screen and the motif
const IniSection *MotifSection(const char *lower); // a section of the motif's def, nullptr if none

// Roster: the motif's select.def
extern std::string g_selectDef; // as the game names it (frame geometry), "" when not known
std::string SelectDef();        // g_selectDef, else the one the motif's [Files] names (from disk); "" none
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
void SwapChar(int pn, const std::string &name); // a character (as ListDefs names it) in P<pn>'s place; the match starts again
// Keys as a fighting game writes them ("DF+a"); bits 0-13: U, D, B, F, a, b, c, x, y, z, s, d, w, m
std::string KeysText(int bits);

// Fonts: the font files loaded (frame geometry), their defs and glyphs
extern std::vector<std::string> g_fontFiles; // as the game names them; a text's font is 1 + an index
extern std::string g_fontPicked;             // the font of the text picked last on the Game window
void FontSpritesSaved(const std::string &path); // the fonts using that SFF are read again
void FontsWindow();

// Session, Control, Players, Inspector, Frame data, Trace, Breakpoints, Console
void Toolbar(); // above the windows: launch, the match, pause and steps, the game's state
void MatchSettingsLine(const std::string &key, const std::string &value); // the matches saved by name
void MatchSettingsWrite(ImGuiTextBuffer *buf);
void PlayersWindow();
void InspectorWindow();
void FrameDataWindow();
void TraceWindow();
void BreakpointsWindow();
void WatchWindow();
void ConsoleWindow();

} // namespace app
