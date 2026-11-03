// Stage: the stage's def file, its sections and fields edited here (the game loads the stage
// again from the text at each change, the match going on; Save writes the file), its BGs added,
// moved and deleted, its BGs and limits picked and dragged on the Game window, a free camera,
// and the def's checks.
#include "app_internal.h"

#include <windows.h>

#include <cfloat>
#include <climits>
#include <cmath>
#include <fstream>

#include "imgui_stdlib.h"
#include "ini.h"
#include "parallax.h"
#include "sff.h"
#include "stagecheck.h"

namespace app {

std::string g_stageDef, g_stageSff;
std::vector<FrameBg> g_frameBgs;
StageView g_stageView;
bool g_stagePick;
int g_stageBg = -1;
std::vector<int> g_stageMarked; // BGs outlined on the game: those of the BGCtrl selected

// The first word of a section's name, as Ikemen cuts it.
std::string FirstWord(const IniSection &s) { return s.lower.substr(0, s.lower.find(' ')); }

// A BGCtrl's time on a bar: when it runs (time = start, end, looptime) and where the stage is (time,
// -1 not known), its parts dragged. True when t changed; *clicked: the bar was clicked, not dragged.
bool CtrlTimeline(IniBgTime &t, bool ran, int time, const char *label, bool *clicked) {
    enum { kNone, kStart, kEnd, kBoth, kLoop };
    static int part = kNone, dragSpan; // the scale stays while dragging, or the bar would slide away
    static IniBgTime from;
    static bool moved;
    float w = ImGui::GetContentRegionAvail().x, h = ImGui::GetTextLineHeightWithSpacing();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##time", ImVec2(std::max(w, 1.0f), h));
    bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    int span = t.loop > 0 ? t.loop + std::max(t.loop / 5, 1) : std::max(std::max(t.end + 1, time + 1), 60) * 5 / 4;
    if (active && part != kNone) span = dragSpan;
    auto x = [&](int v) { return p.x + w * std::min(std::max(v, 0), span) / span; };
    int end = t.loop > 0 ? std::min(t.end, t.loop - 1) : t.end;
    float mouse = ImGui::GetIO().MousePos.x, x0 = x(t.start), x1 = std::max(x(end + 1), x0 + 2), grip = ImGui::GetFontSize() * 0.4f;
    int over = kNone;
    if (t.loop > 0 && fabsf(mouse - x(t.loop)) < grip) over = kLoop;
    else if (fabsf(mouse - x1) < grip) over = kEnd;
    else if (fabsf(mouse - x0) < grip) over = kStart;
    else if (mouse > x0 && mouse < x1) over = kBoth;
    if (ImGui::IsItemActivated()) part = over, from = t, dragSpan = span, moved = false;
    bool changed = false;
    if (active && part != kNone && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2)) {
        int d = (int)std::lround(ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 2).x * span / w);
        IniBgTime n = from;
        int last = from.loop > 0 ? from.loop - 1 : INT_MAX; // the engine runs nothing starting at its looptime
        if (part == kStart) n.start = std::clamp(from.start + d, 0, from.end);
        if (part == kEnd) n.end = std::clamp(from.end + d, from.start, last);
        if (part == kBoth) {
            int length = from.end - from.start;
            n.start = std::clamp(from.start + d, 0, last == INT_MAX ? INT_MAX - length : std::max(last - length, 0));
            n.end = n.start + length;
        }
        if (part == kLoop) n.loop = std::max(from.loop + d, std::max(from.end, from.start + 1)), n.ownLoop = true;
        moved = true;
        changed = n.start != t.start || n.end != t.end || n.loop != t.loop;
        t = n;
    }
    if (clicked) *clicked = ImGui::IsItemDeactivated() && hovered && !moved;
    if (hovered || active) {
        int cursor = active ? part : over;
        if (cursor == kStart || cursor == kEnd || cursor == kLoop) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (cursor == kBoth) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }
    ImDrawList *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(p, ImVec2(p.x + w, p.y + h - 2), IM_COL32(40, 40, 46, 255));
    if (t.loop > 0) draw->AddRectFilled(ImVec2(x(t.loop), p.y), ImVec2(p.x + w, p.y + h - 2), IM_COL32(26, 26, 30, 255));
    if (t.start >= 0)
        draw->AddRectFilled(ImVec2(x0, p.y), ImVec2(x1, p.y + h - 2), ran ? IM_COL32(60, 170, 80, 255) : IM_COL32(70, 90, 130, 255));
    if (t.loop > 0) draw->AddLine(ImVec2(x(t.loop), p.y - 1), ImVec2(x(t.loop), p.y + h - 1), IM_COL32(200, 200, 220, 255), 2);
    if (time >= 0) {
        int at = time;
        if (t.loop > 0) at = time >= t.start ? (t.start + (time - t.start) % t.loop) % t.loop : time % t.loop;
        draw->AddLine(ImVec2(x(at), p.y - 1), ImVec2(x(at), p.y + h - 1), IM_COL32(255, 220, 60, 255), 2);
    }
    draw->AddText(ImVec2(p.x + 4, p.y), IM_COL32_WHITE, label);
    if (hovered || active) {
        std::string tip = "time = " + Ini_BgTimeValue(t) + (t.loop > 0 && !t.ownLoop ? "  (looptime " + std::to_string(t.loop) + " from its BGCtrlDef)" : "");
        if (time >= 0) tip += std::string("\n") + (ran ? "Ran" : "Did not run") + " at stage time " + std::to_string(time);
        tip += "\nDrag it to move it, its edges for its start and end";
        if (t.loop > 0) tip += ", the light line for its looptime";
        ImGui::SetTooltip("%s", tip.c_str());
    }
    return changed;
}

// The def parsed, again when its text changes.
const IniFile &StageIni(const SourceFile &sf) {
    static std::string text;
    static IniFile ini;
    static bool parsed;
    if (!parsed || text != sf.text) {
        text = sf.text;
        Ini_Parse(text, ini);
        parsed = true;
    }
    return ini;
}

// The stage project; from disk when the match is on another, so what goes to the game asks StageInMatch
SourceFile *StageSource() {
    std::string def = ProjectDef(kProjectStage);
    return def.empty() ? nullptr : &LoadSource(def);
}

bool StageInMatch() { return Connected() && !g_stageDef.empty() && NormPath(g_stageDef) == NormPath(ProjectDef(kProjectStage)); }

void StageOpen() {
    g_stageBg = -1;
    StageLayersClear();
}

std::string StageNameOf(const std::string &def) {
    std::string n = def.compare(0, 7, "stages/") == 0 ? def.substr(7) : def;
    if (n.size() > 4 && NormPath(n).compare(n.size() - 4, 4, ".def") == 0) n.resize(n.size() - 4);
    return n;
}

// Width over height of a def's localcoord
static float AspectOfDef(const std::string &def) {
    IniFile ini;
    Ini_Parse(LoadSource(def).text, ini);
    float size[2] = {320, 240}, v[2];
    for (const IniSection &s : ini.sections)
        if (s.lower == "stageinfo" && s.Floats("localcoord", v, 2) && v[1] > 0) size[0] = v[0], size[1] = v[1];
    return size[0] / size[1];
}

void PlayStage(const std::string &def) {
    g_settings.stage = StageNameOf(def);
    if (!Connected() || !g_canTrace || NormPath(def) == NormPath(g_stageDef)) return;
    // A fight's aspect is set when it starts: a stage of another one shows wrongly in it, a model most
    if (g_stageRun && !g_stageDef.empty() && std::fabs(AspectOfDef(def) - AspectOfDef(g_stageDef)) > 0.01f) {
        Launch();
        return;
    }
    Exec("local ok, err = debugSwapStage(" + LuaQuote(def) + ") if not ok then error(err, 0) end");
}

// The game on the stage's workspace stays on the project. Coming in, one that was started otherwise
// starts again: its match can end, and its menus have no stage. A stage opened without going in, goes
// in, asked once in case it does not load. Leaving, the game is left as it is.
void StageRunFrame() {
    static bool was;
    static std::string asked;
    bool now = CurrentProject() == kProjectStage, running = g_bridge.Running();
    if (now && !was && running && !g_stageRun) Launch();
    if (!now && was && running && g_stageRun)
        Log(LogLine::Info, "The game is still on the stage, with no time limit: Restart starts the toolbar's match.");
    was = now;
    std::string def = NormPath(ProjectDef(kProjectStage));
    if (!now || !g_stageRun || g_stageDef.empty() || def == NormPath(g_stageDef)) {
        asked.clear();
        return;
    }
    if (def == asked) return;
    asked = def;
    PlayStage(ProjectDef(kProjectStage));
}

// The section of a BG (by its index in the stage), nullptr when the def does not have it.
const IniSection *BgSection(const IniFile &ini, int index) {
    std::vector<int> bgs = ini.StageBgs();
    return index >= 0 && index < (int)bgs.size() ? &ini.sections[bgs[index]] : nullptr;
}

std::string BgLabel(const IniFile &ini, int index) {
    const IniSection *s = BgSection(ini, index);
    return "[" + (s ? s->name : "BG " + std::to_string(index)) + "]";
}

// Moves a BG by whole units: on the game at once, and its start in the def's text.
void MoveBg(int index, int dx, int dy) {
    SourceFile *sf = StageSource();
    if (!sf || (dx == 0 && dy == 0)) return;
    const IniSection *s = BgSection(StageIni(*sf), index);
    if (!s) return;
    float start[2];
    s->Floats("start", start, 2);
    start[0] += dx, start[1] += dy;
    if (!Ini_SetField(sf->text, *s, "start", Ini_Floats(start, 2))) return;
    sf->version++;
    if (!StageInMatch()) return;
    char lua[96];
    snprintf(lua, sizeof(lua), "debugStageBgMove(%d, %d, %d)", index, dx, dy);
    SendRaw(lua);
}

// The game loads the stage again from the text being edited (a file next to the def, which the
// game deletes once read), or from the def on disk; sprites: its SFF too (it was saved).
void ReloadStage(SourceFile &sf, bool fromText, bool sprites) {
    if (!Connected() || NormPath(g_stageDef) != NormPath(sf.path)) return; // another stage's text is not the match's
    // Which BG is which, when that changes: each goes on where it was. shown: the text the game has
    static std::map<std::string, std::string> shown;
    std::string &was = shown.emplace(NormPath(sf.path), sf.disk).first->second;
    IniFile before, after;
    Ini_Parse(was, before);
    Ini_Parse(fromText ? sf.text : sf.disk, after);
    was = fromText ? sf.text : sf.disk;
    std::vector<int> map = Ini_BgMap(before, after);
    bool same = map.size() == before.StageBgs().size();
    std::string order;
    for (size_t i = 0; i < map.size(); i++) {
        order += (i ? "," : "") + std::to_string(map[i]);
        same = same && map[i] == (int)i;
    }
    std::string arg = "nil";
    if (fromText) {
        std::string tmp = sf.path + ".ikdbg";
        std::ofstream out(GamePath(tmp), std::ios::binary);
        out << sf.text;
        if (!out) {
            Log(LogLine::Error, "Could not write " + GamePath(tmp));
            return;
        }
        arg = LuaQuote(tmp);
    }
    Exec("local ok, err = debugReloadStage(" + arg + (sprites ? ", true" : ", false") + (same ? "" : ", " + LuaQuote(order)) +
         ") if not ok then error(err, 0) end");
}

void StageSpritesSaved(const std::string &path) {
    if (g_stageSff.empty() || NormPath(g_stageSff) != NormPath(path)) return;
    if (!g_stageDef.empty()) ReloadStage(LoadSource(g_stageDef), true, true);
}

// After an edit of the text: the game shows it at once.
void StageEdited(SourceFile &sf) {
    sf.version++;
    ReloadStage(sf, true);
}

// The value of a number field, or otherwise.
float StageNumber(const IniFile &ini, const char *section, const char *key, float otherwise) {
    for (const IniSection &s : ini.sections)
        if (s.lower == section) {
            float v;
            if (s.Floats(key, &v, 1)) return v;
        }
    return otherwise;
}

bool SetStageNumber(SourceFile &sf, const char *section, const char *key, float value) {
    const IniFile &ini = StageIni(sf);
    for (const IniSection &s : ini.sections)
        if (s.lower == section) return Ini_SetField(sf.text, s, key, Ini_Floats(&value, 1));
    return false;
}

// ---------------------------------------------------------------------------
// The stage's limits on the Game window: camera bounds, players' bounds, floor, starting
// positions; their handles drag the def's fields
// ---------------------------------------------------------------------------

bool g_stageLimits;

struct LimitHandle {
    const char *label, *section, *keyX, *keyY; // keyX or keyY null: a line that moves the other way
    ImVec2 world;
    ImU32 color;
};

std::vector<LimitHandle> LimitHandles() {
    const StageView &v = g_stageView;
    std::vector<LimitHandle> h;
    float top = v.lim[3] - v.lim[7], bottom = v.lim[4] + v.lim[8], mid = (top + bottom) / 2;
    float centre = (v.lim[5] + v.lim[6]) / 2;
    ImU32 cam = IM_COL32(255, 160, 40, 255), players = IM_COL32(255, 80, 80, 255), start = IM_COL32(80, 220, 255, 255);
    h.push_back({"boundleft", "camera", "boundleft", nullptr, ImVec2(v.lim[5], mid), cam});
    h.push_back({"boundright", "camera", "boundright", nullptr, ImVec2(v.lim[6], mid), cam});
    h.push_back({"boundhigh", "camera", nullptr, "boundhigh", ImVec2(centre, top), cam});
    h.push_back({"boundlow", "camera", nullptr, "boundlow", ImVec2(centre, bottom), cam});
    h.push_back({"leftbound", "playerinfo", "leftbound", nullptr, ImVec2(v.lim[9], mid), players});
    h.push_back({"rightbound", "playerinfo", "rightbound", nullptr, ImVec2(v.lim[10], mid), players});
    static const char *startKeys[4][3] = {{"P1", "p1startx", "p1starty"}, {"P2", "p2startx", "p2starty"},
                                          {"P3", "p3startx", "p3starty"}, {"P4", "p4startx", "p4starty"}};
    for (size_t i = 0; i < v.starts.size() && i < 4; i++)
        h.push_back({startKeys[i][0], "playerinfo", startKeys[i][1], startKeys[i][2], v.starts[i],
                     i < 2 ? start : IM_COL32(80, 220, 255, 130)});
    h.push_back({"cam", "camera", "startx", "starty", ImVec2(v.lim[11], v.lim[12]), IM_COL32(255, 255, 255, 255)});
    return h;
}

// Draws the limits; with pick on, a handle can be dragged. True while one has the mouse.
bool LimitsOverlay(ImDrawList *draw, bool hovered, bool clicked, const std::function<ImVec2(float, float)> &toScreen) {
    const StageView &v = g_stageView;
    if (!v.ok || !g_stageLimits) return false;
    static int drag = -1;
    static ImVec2 dragFrom, dragWorld;
    auto screen = [&](ImVec2 w) {
        ImVec2 p = v.ToPicture(w.x, w.y);
        return toScreen(p.x, p.y);
    };
    std::vector<LimitHandle> handles = LimitHandles();
    float top = v.lim[3] - v.lim[7], bottom = v.lim[4] + v.lim[8];
    ImVec2 lo = screen(ImVec2(v.lim[5], top)), hi = screen(ImVec2(v.lim[6], bottom));
    ImVec2 m = ImGui::GetMousePos();
    float thick = std::max(1.0f, ImGui::GetFontSize() / 13), reach = ImGui::GetFontSize() * 0.5f;
    // World units per screen pixel
    ImVec2 o = screen(ImVec2(0, 0)), u = screen(ImVec2(1, 1));
    ImVec2 perPixel(1 / std::max(1e-4f, u.x - o.x), 1 / std::max(1e-4f, u.y - o.y));

    // The handle under the mouse
    int hover = -1;
    if (g_stagePick && hovered && drag < 0) {
        float best = reach;
        for (int i = 0; i < (int)handles.size(); i++) {
            ImVec2 p = screen(handles[i].world);
            float d = !handles[i].keyY ? std::fabs(m.x - p.x) : !handles[i].keyX ? std::fabs(m.y - p.y)
                                                                                : std::hypot(m.x - p.x, m.y - p.y);
            // Lines reach as far as the stage does
            bool along = !handles[i].keyY ? m.y >= lo.y && m.y <= hi.y : !handles[i].keyX ? m.x >= lo.x && m.x <= hi.x : true;
            if (along && d < best) best = d, hover = i;
        }
        if (hover >= 0) {
            ImGui::SetTooltip("%s  ([%s])\nDrag to change it; the game shows it at once.", handles[hover].label,
                              handles[hover].section);
            if (clicked) drag = hover, dragFrom = m, dragWorld = handles[hover].world;
        }
    }
    if (drag >= 0 && drag < (int)handles.size()) {
        LimitHandle &h = handles[drag];
        ImVec2 d((m.x - dragFrom.x) * perPixel.x, (m.y - dragFrom.y) * perPixel.y);
        if (!h.keyX) d.x = 0;
        if (!h.keyY) d.y = 0;
        h.world = ImVec2(dragWorld.x + d.x, dragWorld.y + d.y);
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            // In the def's units (the stage's localcoord)
            SourceFile *sf = StageSource();
            float l = v.lim[0] > 0 ? v.lim[0] : 1;
            int dx = (int)std::lround(d.x / l), dy = (int)std::lround(d.y / l);
            if (sf && (dx || dy)) {
                const IniFile &ini = StageIni(*sf);
                bool ok = true;
                if (dx) ok = SetStageNumber(*sf, h.section, h.keyX, StageNumber(ini, h.section, h.keyX, 0) + dx);
                if (dy && ok) ok = SetStageNumber(*sf, h.section, h.keyY, StageNumber(StageIni(*sf), h.section, h.keyY, 0) + dy);
                if (ok) StageEdited(*sf);
                else Log(LogLine::Error, std::string("The def has no [") + h.section + "] section.");
            }
            drag = -1;
        }
    }

    // The stage as far as the camera goes, the floor, then the handles
    draw->AddRect(lo, hi, IM_COL32(255, 160, 40, 160), 0, thick);
    ImVec2 f0 = screen(ImVec2(v.lim[5], 0)), f1 = screen(ImVec2(v.lim[6], 0));
    draw->AddLine(f0, f1, IM_COL32(80, 230, 100, 200), thick);
    draw->AddText(ImVec2(f0.x + 4, f0.y + 2), IM_COL32(80, 230, 100, 230), "floor");
    for (int i = 0; i < (int)handles.size(); i++) {
        const LimitHandle &h = handles[i];
        ImVec2 p = screen(h.world);
        bool hot = i == hover || i == drag;
        float t = hot ? thick * 2.5f : thick;
        float tw = ImGui::CalcTextSize(h.label).x;
        if (!h.keyY) {
            // The label inside the stage: right of a line on the left half, left of one on the right half
            draw->AddLine(ImVec2(p.x, lo.y), ImVec2(p.x, hi.y), h.color, t);
            float x = p.x < (lo.x + hi.x) / 2 ? p.x + 4 : p.x - tw - 4;
            draw->AddText(ImVec2(x, (h.keyX[0] == 'b' ? lo.y : p.y) + 2), h.color, h.label);
        } else if (!h.keyX) {
            draw->AddLine(ImVec2(lo.x, p.y), ImVec2(hi.x, p.y), h.color, t);
            draw->AddText(ImVec2(p.x - tw / 2, p.y < (lo.y + hi.y) / 2 ? p.y + 2 : p.y - ImGui::GetFontSize() - 2),
                          h.color, h.label);
        } else {
            draw->AddCircle(p, reach * 0.7f, h.color, 0, t);
            draw->AddLine(ImVec2(p.x, p.y - reach), ImVec2(p.x, p.y + reach), h.color, thick);
            draw->AddText(ImVec2(p.x - tw / 2, p.y - reach - ImGui::GetFontSize()), h.color, h.label);
        }
    }
    return drag >= 0 || hover >= 0;
}

// ---------------------------------------------------------------------------
// The free camera: only what is drawn moves (the game's camera goes on)
// ---------------------------------------------------------------------------

bool g_freeCam;
float g_freeX, g_freeY, g_freeScale = 1; // the world point in the middle, at the ground line's height; scale

void SendFreeCamera() {
    char lua[128];
    snprintf(lua, sizeof(lua), "debugCamera(true, %g, %g, %g)", g_freeX, g_freeY, g_freeScale);
    SendRaw(lua);
}

void SetFreeCamera(bool on) {
    const StageView &v = g_stageView;
    if (on && !g_freeCam && v.ok && v.cam[2] > 0) {
        g_freeX = v.cam[0], g_freeY = v.cam[1] / v.cam[2], g_freeScale = v.cam[2];
    }
    g_freeCam = on;
    if (on) SendFreeCamera();
    else SendRaw("debugCamera(false)");
}

// The whole stage (as far as the camera goes) on screen.
void FitStage() {
    const StageView &v = g_stageView;
    if (!v.ok || v.cam[5] <= 0 || v.cam[6] <= 0) return;
    float gw = v.cam[4], gh = g_frameScr.y / v.cam[6], gl = v.cam[3];
    float top = v.lim[3] - v.lim[7], bottom = v.lim[4] + v.lim[8];
    float w = std::max(1.0f, v.lim[6] - v.lim[5]), h = std::max(1.0f, bottom - top);
    g_freeScale = std::max(0.05f, std::min(gw / w, gh / h) * 0.95f);
    g_freeX = (v.lim[5] + v.lim[6]) / 2;
    g_freeY = (top + bottom) / 2 - (gh / 2 - gl) / g_freeScale;
    g_freeCam = true;
    SendFreeCamera();
}

// On the Game window's line on the stage's workspace: they all act on the picture
void StageGameControls() {
    ImGui::BeginDisabled(!StageInMatch());
    SameLineFits(CheckboxWidth("Limits"));
    ImGui::Checkbox("Limits", &g_stageLimits);
    ImGui::SetItemTooltip("How far the camera goes (orange), the players' bounds (red), the floor, the starting positions.\n"
                          "Their handles can be dragged.");
    SameLineFits(CheckboxWidth("Scale"));
    ImGui::Checkbox("Scale", &g_stageScale);
    ImGui::SetItemTooltip("P1 standing at the players' bounds, and the highest its head gets in a jump,\n"
                          "to judge the stage's proportions and where the sky has to reach.");
    SameLineFits(CheckboxWidth("Free camera"));
    bool free = g_freeCam;
    if (ImGui::Checkbox("Free camera", &free)) SetFreeCamera(free);
    ImGui::SetItemTooltip("Only what is drawn moves: the wheel zooms, the right button pans.\n"
                          "The game's camera, and what the players can do, stay as they are.");
    SameLineFits(ImGui::CalcTextSize(ICON_SEARCH " Fit the stage").x + ImGui::GetStyle().FramePadding.x * 2);
    if (ImGui::Button(ICON_SEARCH " Fit the stage")) FitStage();
    ImGui::SetItemTooltip("The free camera, with all the stage in view");
    ImGui::EndDisabled();
    // The game restarted, or another tool turned it off
    static int offFrames;
    offFrames = g_freeCam && g_stageView.ok && !g_stageView.free ? offFrames + 1 : 0;
    if (offFrames > 60) g_freeCam = false;
}

bool StageCameraInput(bool hovered, ImVec2 picturePerScreen, const std::function<ImVec2(ImVec2)> &pictureAt) {
    const StageView &v = g_stageView;
    if (!g_freeCam || !v.ok || v.cam[5] <= 0 || v.cam[6] <= 0) return false;
    ImGuiIO &io = ImGui::GetIO();
    static bool panning;
    bool changed = false;
    if (hovered && io.MouseWheel != 0) {
        // Keep the world point under the mouse where it is
        ImVec2 p = pictureAt(io.MousePos);
        float sx = p.x / v.cam[5] - v.cam[4] / 2, sy = p.y / v.cam[6] - v.cam[3];
        float wx = sx / g_freeScale + g_freeX, wy = sy / g_freeScale + g_freeY;
        g_freeScale = std::max(0.05f, std::min(8.0f, g_freeScale * powf(1.15f, io.MouseWheel)));
        g_freeX = wx - sx / g_freeScale;
        g_freeY = wy - sy / g_freeScale;
        changed = true;
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) panning = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) panning = false;
    if (panning && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0)) {
        g_freeX -= io.MouseDelta.x * picturePerScreen.x / v.cam[5] / g_freeScale;
        g_freeY -= io.MouseDelta.y * picturePerScreen.y / v.cam[6] / g_freeScale;
        changed = true;
    }
    if (changed) SendFreeCamera();
    return true;
}

std::string StageSelection() {
    SourceFile *sf = StageSource();
    return sf && g_stageBg >= 0 ? BgLabel(StageIni(*sf), g_stageBg) : "";
}

void StageBgAnim(const std::string &def, int &action, int &elem) {
    if (g_stageDef.empty() || NormPath(g_stageDef) != NormPath(def)) return;
    for (const FrameBg &b : g_frameBgs)
        if (b.index == g_stageBg && b.action >= 0) action = b.action, elem = b.elem;
}

// A BG selected: Animation and Sprites show what it shows, as the Fight screen's elements do. The
// action as readBackGround takes it (a normal BG with a sprite has none), or the one a BGCtrl gave it.
static void ShowBg(int index) {
    SourceFile *sf = StageSource();
    const IniSection *s = sf ? BgSection(StageIni(*sf), index) : nullptr;
    std::string sff = sf ? SpritesFileOf(sf->path) : "";
    if (!s || sff.empty()) return;
    const IniField *type = s->Field("type"), *spr = s->Field("spriteno"), *act = s->Field("actionno");
    bool normal = type && !type->value.empty() && tolower((unsigned char)type->value[0]) == 'n';
    int action = act && (!normal || !spr || spr->value.empty()) ? atoi(act->value.c_str()) : -1, elem = 1;
    for (const FrameBg &b : g_frameBgs)
        if (b.index == index && b.action >= 0) action = b.action, elem = b.elem;
    int g = -1, n = -1;
    if (action >= 0) {
        AnimOpenFile(sf->path, action, elem, false);
        AnimFollowFile();
        auto it = AirOf(*sf).actions.find(action);
        if (it != AirOf(*sf).actions.end() && elem >= 1 && elem <= (int)it->second.elems.size())
            g = it->second.elems[elem - 1].group, n = it->second.elems[elem - 1].image;
    } else if (spr) {
        sscanf(spr->value.c_str(), "%d , %d", &g, &n);
    }
    SpriteFile &sprites = SpriteFileAt(sff);
    ShowInSprites(sprites, g >= 0 ? Sff_FindSprite(sprites.doc, g, n) : -1, "stage");
}

void ShowStageBg(int index) { ShowBg(index); }

static BgEdit g_bgEdit; // an edit of the stage's BGs, as the next frame starts

bool StageOverlay(ImDrawList *draw, bool hovered, bool clicked, const std::function<ImVec2(float, float)> &toScreen,
                  ImVec2 scale) {
    static int dragBg = -1;
    static ImVec2 rest;
    // What the game draws is the match's stage: another's def has other BGs under the same numbers
    if (!StageInMatch()) {
        SendHiddenBgs(nullptr);
        SendStageEmpty(nullptr);
        return false;
    }
    SourceFile *sf = StageSource();
    const IniFile *ini = sf ? &StageIni(*sf) : nullptr;
    SendHiddenBgs(ini);
    SendStageEmpty(ini);
    if (ini) ParallaxOverlay(draw, *sf, *ini, toScreen);
    ScaleOverlay(draw, toScreen);
    if (ini && FloorOverlay(draw, hovered && dragBg < 0, clicked && dragBg < 0, *sf, *ini, toScreen)) return true;
    // The limits' handles first
    if (LimitsOverlay(draw, hovered && dragBg < 0, clicked && dragBg < 0, toScreen)) {
        hovered = clicked = false;
        if (g_stagePick) return true;
    }
    auto locked = [&](const FrameBg &b) { return ini && StageBgLocked(*ini, b.index); };
    auto corners = [&](const FrameBg &b, ImVec2 &lo, ImVec2 &hi) {
        lo = toScreen(b.rect.x, b.rect.y);
        hi = toScreen(b.rect.x + b.rect.z, b.rect.y + b.rect.w);
    };

    // The BG under the mouse: the last one drawn there (the one in front)
    int hover = -1;
    if (g_stagePick && hovered && dragBg < 0) {
        ImVec2 m = ImGui::GetMousePos();
        for (int i = (int)g_frameBgs.size() - 1; i >= 0 && hover < 0; i--) {
            ImVec2 lo, hi;
            corners(g_frameBgs[i], lo, hi);
            if (m.x >= lo.x && m.x < hi.x && m.y >= lo.y && m.y < hi.y && !locked(g_frameBgs[i])) hover = i;
        }
        // A tiled BG is sent as one tile: past it, it is still there along the axes it repeats on
        for (int i = (int)g_frameBgs.size() - 1; i >= 0 && hover < 0 && ini; i--) {
            const IniSection *s = g_frameBgs[i].tiled ? BgSection(*ini, g_frameBgs[i].index) : nullptr;
            const IniField *tile = s ? s->Field("tile") : nullptr;
            if (!tile || locked(g_frameBgs[i])) continue;
            int x = 0, y = 0;
            sscanf(tile->value.c_str(), "%d , %d", &x, &y);
            ImVec2 lo, hi;
            corners(g_frameBgs[i], lo, hi);
            if ((x || (m.x >= lo.x && m.x < hi.x)) && (y || (m.y >= lo.y && m.y < hi.y))) hover = i;
        }
        if (hover >= 0) {
            const FrameBg &b = g_frameBgs[hover];
            ImGui::SetTooltip("%s  layer %d%s\nDrag: move it (its start)", ini ? BgLabel(*ini, b.index).c_str() : "",
                              b.layer, b.tiled ? ", tiled" : "");
            if (clicked) {
                g_stageBg = b.index;
                ShowWindow("Stage");
                ShowBg(b.index);
                dragBg = b.index;
                rest = ImVec2(0, 0);
            }
        }
    }

    // Dragging: whole units of the BG's start
    if (dragBg >= 0) {
        const FrameBg *b = nullptr;
        for (const auto &f : g_frameBgs)
            if (f.index == dragBg) b = &f;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            dragBg = -1;
        } else if (b && b->k.x != 0 && b->k.y != 0) {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            rest.x += d.x / scale.x / b->k.x;
            rest.y += d.y / scale.y / b->k.y;
            int dx = (int)std::trunc(rest.x), dy = (int)std::trunc(rest.y);
            if (dx != 0 || dy != 0) {
                MoveBg(dragBg, dx, dy);
                rest.x -= dx;
                rest.y -= dy;
            }
        }
    }

    // Outlines: the selected BG and those of the selected BGCtrl always, the others while picking
    float thick = std::max(1.0f, ImGui::GetFontSize() / 13);
    for (int i = 0; i < (int)g_frameBgs.size(); i++) {
        const FrameBg &b = g_frameBgs[i];
        bool sel = b.index == g_stageBg, hot = i == hover;
        bool marked = std::find(g_stageMarked.begin(), g_stageMarked.end(), b.index) != g_stageMarked.end();
        if (!sel && !hot && !marked && !g_stagePick) continue;
        ImVec2 lo, hi;
        corners(b, lo, hi);
        ImU32 col = sel ? IM_COL32(255, 220, 60, 255) : hot ? IM_COL32(255, 255, 255, 230)
                  : marked ? IM_COL32(80, 220, 255, 220) : IM_COL32(200, 200, 255, 70);
        if (marked && !sel && ini && g_stageMarked.size() <= 3) // more would only pile up
            draw->AddText(ImVec2(lo.x + 4, lo.y + 2), col, BgLabel(*ini, b.index).c_str());
        if (hot) draw->AddRectFilled(lo, hi, IM_COL32(255, 255, 255, 30));
        draw->AddRect(lo, hi, col, 0, sel || hot ? thick * 2 : thick);
        if (sel && ini) draw->AddText(ImVec2(lo.x + 4, lo.y + 2), col, BgLabel(*ini, b.index).c_str());
    }
    return g_stagePick && (hover >= 0 || dragBg >= 0);
}

// ---------------------------------------------------------------------------
// The checks of the def (stagecheck.cpp), again when the text or the sprites change
// ---------------------------------------------------------------------------

// A file the def names, found as the engine looks for it: next to the def, in the game's
// folder, then in data/, stages/, sound/ and video/. "" when not found.
std::string StageFile(const std::string &def, const std::string &name) {
    std::string dir = def.substr(0, def.find_last_of("/\\") + 1);
    for (const std::string &base : {dir, std::string(), std::string("data/"), std::string("stages/"),
                                    std::string("sound/"), std::string("video/")}) {
        std::string path = base + name;
        if (GetFileAttributesA(GamePath(path).c_str()) != INVALID_FILE_ATTRIBUTES) return path;
    }
    return "";
}

// The SFF, as the Sprites window has it (its edits count)
const SpriteFile *StageSpriteFile(const SourceFile &sf, const IniFile &ini) {
    const SpriteFile *spr = nullptr;
    for (const IniSection &s : ini.sections)
        if (s.lower == "bgdef")
            if (const IniField *f = s.Field("spr"))
                if (std::string p = StageFile(sf.path, f->value); !p.empty()) spr = &SpriteFileAt(p);
    return spr;
}

// The checks of a stage's def, again when its text, its SFF or the camera changes; *sffRead: its SFF
// was read
const std::vector<StageFinding> &StageChecks(SourceFile &sf, const IniFile &ini, bool *sffRead = nullptr) {
    static std::string path, text;
    static unsigned sffVersion, modelVersion;
    static ParallaxView camera;
    static std::vector<StageFinding> found;
    static bool done, read;
    const SpriteFile *spr = StageSpriteFile(sf, ini);
    const SffDoc *sff = spr && spr->doc.error.empty() ? &spr->doc : nullptr;
    ParallaxView view = StageParallaxView(ini);
    bool model = StageHasModel(ini);
    unsigned modelNow = model ? ModelCheckVersion(sf.path) : 0;
    // The rays through a model wait for the end of a drag
    bool changed = !done || path != sf.path || text != sf.text || sffVersion != (spr ? spr->version : 0) || modelVersion != modelNow ||
                   memcmp(&camera, &view, sizeof(view)) != 0;
    if (changed && !(model && done && path == sf.path && ImGui::IsMouseDown(ImGuiMouseButton_Left))) {
        path = sf.path;
        text = sf.text;
        sffVersion = spr ? spr->version : 0;
        modelVersion = modelNow;
        camera = view;
        found = Stage_Check(sf.text, sff, [&](const std::string &name) { return !StageFile(sf.path, name).empty(); });
        const std::vector<ParallaxBg> &bgs = StageParallaxBgs(sf, sff, sffVersion);
        // A model may cover what the BGs leave: the Camera tab's Check the limits sees that
        if (model) {
            ModelChecks(sf, ini, found);
        } else {
            std::vector<StageFinding> holes = Parallax_Check(ini, bgs, view);
            found.insert(found.end(), holes.begin(), holes.end());
        }
        if (sff) {
            std::vector<StageFinding> seams = Parallax_Seams(bgs, *sff);
            found.insert(found.end(), seams.begin(), seams.end());
        }
        std::stable_sort(found.begin(), found.end(), [](const StageFinding &a, const StageFinding &b) { return a.level < b.level; });
        done = true;
        read = sff != nullptr;
    }
    if (sffRead) *sffRead = read;
    return found;
}

bool StageHasModel(const IniFile &ini) {
    for (const IniSection &s : ini.sections)
        if (s.lower == "bgdef" && s.Field("model")) return true;
    return false;
}

std::string SettingsStageDef() {
    const std::string &s = g_settings.stage;
    if (s.empty()) return "";
    std::string def = s.find('/') == std::string::npos && s.find('\\') == std::string::npos ? "stages/" + s : s;
    if (NormPath(def).size() < 4 || NormPath(def).compare(NormPath(def).size() - 4, 4, ".def") != 0) def += ".def";
    return GameFileExists(def) ? def : "";
}

// The stage project
void StageProblems(std::vector<Problem> &out) {
    std::string def = ProjectDef(kProjectStage);
    if (def.empty()) return;
    SourceFile &sf = LoadSource(def);
    static IniFile ini;
    static std::string parsed;
    static bool once;
    if (!once || parsed != sf.text) {
        parsed = sf.text;
        ini = IniFile();
        Ini_Parse(parsed, ini);
        once = true;
    }
    for (const StageFinding &f : StageChecks(sf, ini)) out.push_back({(int)f.level, "Stage", sf.path, f.line, f.text});
}

void ChecksSection(SourceFile &sf, const IniFile &ini) {
    bool sffRead = false;
    const std::vector<StageFinding> &found = StageChecks(sf, ini, &sffRead);
    FindingsSection(sf, found, sffRead, false);
}

// For the tab's name: what is wrong, not the notes
static int StageCheckCount(SourceFile &sf, const IniFile &ini) {
    int count = 0;
    for (const StageFinding &f : StageChecks(sf, ini)) count += f.level != StageFinding::Note;
    return count;
}

void FindingsSection(const SourceFile &sf, const std::vector<StageFinding> &found, bool sffRead, bool header) {
    if (header) {
        int count[3] = {};
        for (const auto &f : found) count[f.level]++;
        char title[96];
        snprintf(title, sizeof(title), "Checks: %d %s, %d %s, %d %s###stagechecks", count[0], count[0] == 1 ? "error" : "errors", count[1],
                 count[1] == 1 ? "warning" : "warnings", count[2], count[2] == 1 ? "note" : "notes");
        if (!ImGui::CollapsingHeader(title)) return;
    }
    if (found.empty()) ImGui::TextDisabled("Nothing found.");
    if (!sffRead) ImGui::TextColored(Ink(kYellow), "The SFF could not be read: its sprites are not checked.");
    const char *names[3] = {"error", "warning", "note"};
    const ImVec4 colors[3] = {Ink(kRed), Ink(kYellow), Ink(kBlue)};
    if (ImGui::BeginTable("checks", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY,
                          ImVec2(0, header ? std::min<float>(found.size() + 1, 10) * ImGui::GetFrameHeight() : 0))) {
        ImGui::TableSetupColumn("");
        ImGui::TableSetupColumn("line");
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        for (size_t i = 0; i < found.size(); i++) {
            const StageFinding &f = found[i];
            ImGui::PushID((int)i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(colors[f.level], "%s", names[f.level]);
            ImGui::TableNextColumn();
            if (f.line) ImGui::Text("%d", f.line);
            ImGui::TableNextColumn();
            // As tall as its text wrapped: a narrow window shows all of it
            ImVec2 at = ImGui::GetCursorPos();
            float height = ImGui::CalcTextSize(f.text.c_str(), nullptr, false, ImGui::GetContentRegionAvail().x).y;
            if (ImGui::Selectable("##row", false, ImGuiSelectableFlags_SpanAllColumns, ImVec2(0, height)) && f.line) {
                g_codeFile = sf.path;
                g_codeLine = g_codeScrollTo = f.line;
                g_codeHold = true;
            }
            if (f.line) ImGui::SetItemTooltip("Show the line in Code");
            ImGui::SetCursorPos(at);
            ImGui::TextWrapped("%s", f.text.c_str());
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

// A section's fields under its name: what its kind has above them (a BGCtrl's time, a BG's start, type
// and tools), then a table. *section: a BGCtrlDef's click on one of its BGCtrls goes there.
static void SectionFields(SourceFile &sf, const IniFile &ini, int *section, int bg, bool live) {
    const IniSection &s = ini.sections[*section];
    std::vector<int> ctrls = ini.StageBgCtrls();
    const StageView &sv = g_stageView;
    float em = ImGui::GetFontSize();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("[%s]", s.name.c_str());
    ImGui::SameLine();
    if (ImGui::Button(ICON_CODE " Show in Code")) {
        g_codeFile = sf.path;
        g_codeLine = g_codeScrollTo = s.line;
        g_codeHold = true;
    }
    std::string before = sf.text;
    // A BGCtrl, or a BGCtrlDef and the BGCtrls after it: when they run, and their BGs on the game
    std::string word = FirstWord(s);
    if (word == "bgctrl" || word == "bgctrl3d" || word == "bgctrldef") {
        int goTo = -1;
        for (int n = 0; n < (int)ctrls.size(); n++) {
            bool mine = ctrls[n] == *section;
            if (word == "bgctrldef" && ctrls[n] > *section) {
                mine = true;
                for (int k = *section + 1; k < ctrls[n]; k++) mine = mine && FirstWord(ini.sections[k]) != "bgctrldef";
            }
            if (!mine) continue;
            const IniSection &cs = ini.sections[ctrls[n]];
            const IniField *type = cs.Field("type");
            std::string label = (word == "bgctrldef" ? cs.name + "  " : "") + (type ? type->value : std::string("?"));
            bool inGame = live && n < (int)sv.ctrls.size(), clicked = false;
            IniBgTime t = Ini_BgCtrlTime(ini, ctrls[n]);
            ImGui::PushID(n);
            bool moved = CtrlTimeline(t, inGame && sv.ctrls[n].ran, inGame ? sv.ranAt : -1, label.c_str(), &clicked);
            ImGui::PopID();
            if (moved && sf.text == before && Ini_SetField(sf.text, cs, "time", Ini_BgTimeValue(t))) StageEdited(sf);
            if (clicked && word == "bgctrldef") goTo = ctrls[n];
            if (inGame)
                for (int b : sv.ctrls[n].bgs) g_stageMarked.push_back(b);
        }
        if (goTo >= 0) *section = goTo;
        if (word != "bgctrldef" && live) {
            std::string on;
            for (int b : g_stageMarked) on += (on.empty() ? "" : ", ") + BgLabel(ini, b);
            ImGui::TextWrapped("acts on %s", on.empty() ? "no BG" : on.c_str());
        }
    }
    if (bg >= 0) {
        // start, dragged here or on the game: the game follows at once
        float start[2];
        s.Floats("start", start, 2);
        float edit[2] = {start[0], start[1]};
        ImGui::SetNextItemWidth(em * 10);
        if (ImGui::DragFloat2("start", edit, 1.0f, -FLT_MAX, FLT_MAX, "%.0f")) {
            int dx = (int)std::lround(edit[0] - start[0]), dy = (int)std::lround(edit[1] - start[1]);
            MoveBg(bg, dx, dy);
        }
        ImGui::SetItemTooltip("Drag to move the BG; the game follows at once (other fields need Apply).");
        for (const auto &f : g_frameBgs)
            if (f.index == bg) {
                ImGui::SameLine();
                ImGui::TextDisabled("layer %d%s, %.0fx%.0f px", f.layer, f.tiled ? ", tiled" : "", f.rect.z, f.rect.w);
            }
        if (BgTypeLine(sf, s)) StageEdited(sf);
        else ParallaxBgLine(sf, ini, s, bg);
    }
    if (bg >= 0) {
        if (sf.text == before && BgFieldsTable(sf, s)) StageEdited(sf);
        return;
    }
    if (sf.text == before &&
        ImGui::BeginTable("fields", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthStretch, 0.35f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.65f);
        ImGui::TableSetupColumn("what");
        ImGui::TableSetupColumn("");
        static std::string editing;
        static int editLine = -1;
        bool changed = false;
        for (const IniField &f : s.fields) {
            ImGui::PushID(f.line);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(f.key.c_str());
            ImGui::SetItemTooltip("%s", f.key.c_str());
            ImGui::TableNextColumn();
            std::string picked;
            ImGui::SetNextItemWidth(-FLT_MIN);
            std::string value = editLine == f.line ? editing : f.value;
            if (ImGui::InputText("##v", &value)) editing = value, editLine = f.line;
            if (ImGui::IsItemDeactivatedAfterEdit() && !changed) {
                editLine = -1;
                if (value != f.value && !value.empty()) changed = Ini_SetField(sf.text, s, f.key.c_str(), value);
            }
            ImGui::TableNextColumn();
            FieldRefCell(sf, s, f, &picked);
            if (!picked.empty() && !changed) changed = Ini_SetField(sf.text, s, f.key.c_str(), picked);
            ImGui::TableNextColumn();
            if (IconButton(ICON_XMARK) && !changed) changed = Ini_SetField(sf.text, s, f.key.c_str(), "");
            ImGui::SetItemTooltip("Remove this line");
            ImGui::PopID();
        }
        // A new field
        static std::string newKey, newValue;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##nk", "new key", &newKey);
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        bool add = ImGui::InputTextWithHint("##nv", "value", &newValue, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::TableNextColumn();
        ImGui::TableNextColumn();
        add = IconButton(ICON_PLUS) || add;
        ImGui::SetItemTooltip("Add this field");
        if (add && !changed && !newKey.empty() && !newValue.empty()) {
            std::string key = newKey;
            for (char &ch : key) ch = (char)tolower((unsigned char)ch);
            changed = Ini_SetField(sf.text, s, key.c_str(), newValue);
            newKey.clear(), newValue.clear();
        }
        ImGui::EndTable();
        if (changed) StageEdited(sf);
    }
}

// The sections the Camera tab shows
static bool OfCamera(const IniSection &s) { return s.lower == "camera" || s.lower == "bound" || s.lower == "playerinfo"; }

// What is left of the def, with its animations: list above, fields below
static void StageTab(SourceFile &sf, const IniFile &ini, int *selected, bool live) {
    std::vector<int> bgs = ini.StageBgs(), ctrls = ini.StageBgCtrls();
    const StageView &sv = g_stageView;
    float height = ImGui::GetContentRegionAvail().y * 0.4f;
    if (ImGui::BeginChild("sections", ImVec2(0, height), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY)) {
        for (int i = 0; i < (int)ini.sections.size(); i++) {
            const IniSection &s = ini.sections[i];
            if (s.lower.compare(0, 13, "begin action ") == 0 || OfCamera(s)) continue; // those have their place
            if (std::find(bgs.begin(), bgs.end(), i) != bgs.end()) continue;
            int ctrl = -1;
            for (int n = 0; n < (int)ctrls.size(); n++)
                if (ctrls[n] == i) ctrl = n;
            bool ran = ctrl >= 0 && ctrl < (int)sv.ctrls.size() && sv.ctrls[ctrl].ran;
            std::string label = (ctrl >= 0 ? "  " : "") + s.name + "##" + std::to_string(i);
            if (ran) ImGui::PushStyleColor(ImGuiCol_Text, Ink(kGreen));
            if (ImGui::Selectable(label.c_str(), *selected == i)) *selected = i;
            if (ran) {
                ImGui::PopStyleColor();
                ImGui::SetItemTooltip("Ran on the stage's last step.");
            }
        }
        ActionList(sf);
    }
    ImGui::EndChild();
    if (*selected >= 0 && *selected < (int)ini.sections.size() && !OfCamera(ini.sections[*selected])) SectionFields(sf, ini, selected, -1, live);
    else ImGui::TextDisabled("Pick a section.");
}

void StageWindow() {
    if (!BeginTool("Stage")) return;
    SourceFile *sfp = StageSource();
    if (!sfp) {
        if (g_bridge.Running()) ImGui::TextDisabled("Needs the game's frames (patched Ikemen GO), in a match.");
        else EmptyNote("Open a stage, at the left of the toolbar or in Explorer.");
        ImGui::End();
        return;
    }
    bool live = StageInMatch();
    SourceFile &sf = *sfp;
    static int selBg = -2;
    static int defSection = -1;     // the Stage tab's
    static const char *camSection = "camera"; // the Camera tab's
    enum { kLayers, kCamera, kDef, kChecks };
    int askTab = -1;
    if (g_bgEdit) {
        g_stageBg = g_bgEdit(sf);
        g_bgEdit = nullptr;
        selBg = -2;
        if (g_stageBg >= 0) ShowBg(g_stageBg);
    }
    const IniFile &ini = StageIni(sf);
    std::vector<int> bgs = ini.StageBgs();

    ImGui::BeginDisabled(!sf.Dirty());
    bool save = ImGui::Button(ICON_SAVE " Save");
    ImGuiIO &io = ImGui::GetIO();
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
        save = sf.Dirty();
    if (save) SaveSource(sf);
    ImGui::SetItemTooltip("Writes the def (Ctrl+S). The game already shows every change.");
    ImGui::SameLine();
    if (ImGui::Button(ICON_REVERT " Revert")) {
        ReadSource(sf);
        ReloadStage(sf, false);
    }
    ImGui::EndDisabled();
    // On the same line while there is room (the window is often narrow)
    auto flow = [](const char *label, float extra) {
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(label).x + extra) ImGui::NewLine();
    };
    float pad = ImGui::GetStyle().FramePadding.x * 2;
    flow("Reload", pad);
    if (ImGui::Button(ICON_RESTART " Reload")) ReloadStage(sf, true);
    ImGui::SetItemTooltip("The game loads the stage again (from the text here), e.g. after its files changed.");
    UndoStack<std::string> &history = TrackAnims(sf);
    flow("xx", UndoButtonsWidth());
    if (int step = UndoButtons(history.CanUndo(), history.CanRedo(), g_stagePick)) {
        sf.text = step < 0 ? history.Undo() : history.Redo();
        StageEdited(sf);
        ImGui::End(); // what was parsed is old
        return;
    }
    std::vector<int> ctrls = ini.StageBgCtrls();
    const StageView &sv = g_stageView;
    if (live) {
        ImGui::TextDisabled("%s  %zu BGs, %zu drawn", sf.path.c_str(), bgs.size(), g_frameBgs.size());
        SaveState(sf.Dirty(), true);
        int running = 0;
        for (const auto &c : sv.ctrls) running += c.ran;
        ImGui::AlignTextToFramePadding();
        ImGui::Text("stage time %d", sv.time);
        ImGui::SameLine();
        ImGui::TextDisabled("%zu BGCtrls, %d running", ctrls.size(), running);
        flow("Restart the stage", pad);
        if (ImGui::Button(ICON_RESTART " Restart the stage")) SendRaw("debugStageReset()");
        ImGui::SetItemTooltip("Stage time back to 0, the BGs where they start; the BGCtrls run again from there.");
        if (ctrls.size() != sv.ctrls.size() && !sf.Dirty())
            ImGui::TextColored(Ink(kYellow), "The game has %zu BGCtrls, the def %zu.", sv.ctrls.size(), ctrls.size());
    } else {
        ImGui::TextColored(Ink(kOrange), "from disk");
        ImGui::SameLine();
        ImGui::TextDisabled("%s  %zu BGs, %zu BGCtrls", sf.path.c_str(), bgs.size(), ctrls.size());
        SaveState(sf.Dirty(), false);
    }

    ModelSection(StageHasModel(ini));

    // A BG picked on the game, or in the list
    if (g_stageBg != selBg) {
        selBg = g_stageBg;
        if (selBg >= 0) askTab = kLayers;
    }
    if (selBg >= (int)bgs.size()) selBg = g_stageBg = -1;
    g_stageMarked.clear();
    auto tab = [&](const char *label, int n) { return ImGui::BeginTabItem(label, nullptr, askTab == n ? ImGuiTabItemFlags_SetSelected : 0); };

    if (!ImGui::BeginTabBar("stage")) {
        ImGui::End();
        return;
    }
    if (tab(ICON_LAYERS " Layers###layers", kLayers)) {
        // New ones go after the selected BG, in front of it
        BgHost layers;
        layers.selected = selBg >= 0 ? selBg : -1;
        layers.parsed = [](SourceFile &s) -> const IniFile & { return StageIni(s); };
        layers.edited = StageEdited;
        layers.showsSff = [](const std::string &path) { return NormPath(g_stageSff) == NormPath(path); };
        BgToolbar(sf, ini, layers, g_bgEdit);
        LayersSection(sf, ini, g_bgEdit, ImGui::GetContentRegionAvail().y * (selBg >= 0 ? 0.4f : 1.0f));
        if (selBg >= 0) {
            int section = bgs[selBg];
            SectionFields(sf, ini, &section, selBg, live);
        }
        ImGui::EndTabItem();
    }
    if (tab(ICON_FAST " Camera###camera", kCamera)) {
        ParallaxSection(sf, ini);
        ImGui::Separator();
        int section = -1;
        for (const char *name : {"camera", "bound", "playerinfo"}) {
            int found = -1;
            for (int i = (int)ini.sections.size() - 1; i >= 0; i--)
                if (ini.sections[i].lower == name) found = i; // the first, as the engine takes it
            if (found < 0) continue;
            if (strcmp(name, "camera") != 0) ImGui::SameLine();
            if (ImGui::RadioButton(ini.sections[found].name.c_str(), strcmp(camSection, name) == 0)) camSection = name;
            if (strcmp(camSection, name) == 0) section = found;
        }
        if (section >= 0) SectionFields(sf, ini, &section, -1, live);
        else ImGui::TextDisabled("The def has no such section.");
        ImGui::EndTabItem();
    }
    if (tab(ICON_STAGE " Stage###def", kDef)) {
        if (defSection >= (int)ini.sections.size()) defSection = -1;
        StageTab(sf, ini, &defSection, live);
        ImGui::EndTabItem();
    }
    char checks[64];
    snprintf(checks, sizeof(checks), "Checks (%d)###checks", StageCheckCount(sf, ini));
    if (tab(checks, kChecks)) {
        ChecksSection(sf, ini);
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
    ImGui::End();
}

} // namespace app
