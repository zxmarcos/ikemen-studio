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

static std::string g_stageOpened; // the stage picked in Explorer

// The match's stage; the game not running, the one picked in Explorer, else the last match's or the toolbar's.
SourceFile *StageSource() {
    std::string def = g_stageDef;
    if (!Connected() && !g_stageOpened.empty()) def = g_stageOpened;
    if (def.empty()) def = SettingsStageDef();
    return def.empty() ? nullptr : &LoadSource(def);
}

void StageOpen(const std::string &def) {
    g_stageOpened = def;
    g_stageBg = -1;
}

std::string StageNameOf(const std::string &def) {
    std::string n = def.compare(0, 7, "stages/") == 0 ? def.substr(7) : def;
    if (n.size() > 4 && NormPath(n).compare(n.size() - 4, 4, ".def") == 0) n.resize(n.size() - 4);
    return n;
}

void PlayStage(const std::string &def) {
    StageOpen(def);
    AddRecent(true, def);
    g_settings.stage = StageNameOf(def);
    if (Connected() && g_canTrace && NormPath(def) != NormPath(g_stageDef))
        Exec("local ok, err = debugSwapStage(" + LuaQuote(def) + ") if not ok then error(err, 0) end");
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
    if (!Connected()) return;
    char lua[96];
    snprintf(lua, sizeof(lua), "debugStageBgMove(%d, %d, %d)", index, dx, dy);
    SendRaw(lua);
}

// The game loads the stage again from the text being edited (a file next to the def, which the
// game deletes once read), or from the def on disk; sprites: its SFF too (it was saved).
void ReloadStage(SourceFile &sf, bool fromText, bool sprites) {
    if (!Connected()) return;
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
    if (SourceFile *sf = StageSource()) ReloadStage(*sf, true, true);
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

static BgEdit g_bgEdit; // an edit of the stage's BGs, as the next frame starts

bool StageOverlay(ImDrawList *draw, bool hovered, bool clicked, const std::function<ImVec2(float, float)> &toScreen,
                  ImVec2 scale) {
    static int dragBg = -1;
    static ImVec2 rest;
    // The limits' handles first
    if (LimitsOverlay(draw, hovered && dragBg < 0, clicked && dragBg < 0, toScreen)) {
        hovered = clicked = false;
        if (g_stagePick) return true;
    }
    SourceFile *sf = StageSource();
    const IniFile *ini = sf ? &StageIni(*sf) : nullptr;
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
            if (m.x >= lo.x && m.x < hi.x && m.y >= lo.y && m.y < hi.y) hover = i;
        }
        // A tiled BG is sent as one tile: past it, it is still there along the axes it repeats on
        for (int i = (int)g_frameBgs.size() - 1; i >= 0 && hover < 0 && ini; i--) {
            const IniSection *s = g_frameBgs[i].tiled ? BgSection(*ini, g_frameBgs[i].index) : nullptr;
            const IniField *tile = s ? s->Field("tile") : nullptr;
            if (!tile) continue;
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

// The checks of a stage's def, again when its text or its SFF changes; *sffRead: its SFF was read
const std::vector<StageFinding> &StageChecks(SourceFile &sf, const IniFile &ini, bool *sffRead = nullptr) {
    static std::string path, text;
    static unsigned sffVersion;
    static std::vector<StageFinding> found;
    static bool done, read;
    // The SFF, as the Sprites window has it (its edits count)
    const SpriteFile *spr = nullptr;
    for (const IniSection &s : ini.sections)
        if (s.lower == "bgdef")
            if (const IniField *f = s.Field("spr"))
                if (std::string p = StageFile(sf.path, f->value); !p.empty()) spr = &SpriteFileAt(p);
    const SffDoc *sff = spr && spr->doc.error.empty() ? &spr->doc : nullptr;
    if (!done || path != sf.path || text != sf.text || sffVersion != (spr ? spr->version : 0)) {
        path = sf.path;
        text = sf.text;
        sffVersion = spr ? spr->version : 0;
        found = Stage_Check(sf.text, sff, [&](const std::string &name) { return !StageFile(sf.path, name).empty(); });
        done = true;
        read = sff != nullptr;
    }
    if (sffRead) *sffRead = read;
    return found;
}

std::string SettingsStageDef() {
    const std::string &s = g_settings.stage;
    if (s.empty()) return "";
    std::string def = s.find('/') == std::string::npos && s.find('\\') == std::string::npos ? "stages/" + s : s;
    if (NormPath(def).size() < 4 || NormPath(def).compare(NormPath(def).size() - 4, 4, ".def") != 0) def += ".def";
    return GameFileExists(def) ? def : "";
}

// The stage in the match, else the toolbar's
void StageProblems(std::vector<Problem> &out) {
    std::string def = g_stageDef;
    if (def.empty()) def = SettingsStageDef();
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
    FindingsSection(sf, found, sffRead);
}

void FindingsSection(const SourceFile &sf, const std::vector<StageFinding> &found, bool sffRead) {
    int count[3] = {};
    for (const auto &f : found) count[f.level]++;
    char header[96];
    snprintf(header, sizeof(header), "Checks: %d %s, %d %s, %d %s###stagechecks", count[0], count[0] == 1 ? "error" : "errors", count[1],
             count[1] == 1 ? "warning" : "warnings", count[2], count[2] == 1 ? "note" : "notes");
    if (!ImGui::CollapsingHeader(header)) return;
    if (found.empty()) ImGui::TextDisabled("Nothing found.");
    if (!sffRead) ImGui::TextColored(Ink(kYellow), "The SFF could not be read: its sprites are not checked.");
    const char *names[3] = {"error", "warning", "note"};
    const ImVec4 colors[3] = {Ink(kRed), Ink(kYellow), Ink(kBlue)};
    if (ImGui::BeginTable("checks", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY,
                          ImVec2(0, std::min<float>(found.size() + 1, 10) * ImGui::GetFrameHeight()))) {
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
            if (ImGui::Selectable(f.text.c_str(), false, ImGuiSelectableFlags_SpanAllColumns) && f.line) {
                g_codeFile = sf.path;
                g_codeLine = g_codeScrollTo = f.line;
                g_codeHold = true;
            }
            if (f.line) ImGui::SetItemTooltip("Show the line in Code");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

void StageWindow() {
    if (!BeginTool("Stage")) return;
    SourceFile *sfp = StageSource();
    if (!sfp) {
        if (g_bridge.Running()) ImGui::TextDisabled("Needs the game's frames (patched Ikemen GO), in a match.");
        else EmptyNote("Pick a stage in Explorer, or start the game: the stage of the match is edited here.");
        ImGui::End();
        return;
    }
    bool live = Connected() && !g_stageDef.empty() && NormPath(g_stageDef) == NormPath(sfp->path);
    SourceFile &sf = *sfp;
    // The selected section: a BG picked on the game, or one clicked in the list
    static int selSection = -1;
    static int selBg = -2;
    static bool showSel; // the list scrolls to it
    if (g_bgEdit) {
        g_stageBg = g_bgEdit(sf);
        g_bgEdit = nullptr;
        selSection = -1, selBg = -2;
        showSel = true;
        if (g_stageBg >= 0) ShowBg(g_stageBg);
    }
    const IniFile &ini = StageIni(sf);
    std::vector<int> bgs = ini.StageBgs();
    float em = ImGui::GetFontSize();

    ImGui::BeginDisabled(!sf.Dirty());
    bool save = ImGui::Button(ICON_SAVE " Save (Ctrl+S)");
    ImGuiIO &io = ImGui::GetIO();
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
        save = sf.Dirty();
    if (save) SaveSource(sf);
    ImGui::SetItemTooltip("Writes the def. The game already shows every change.");
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
    float box = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x, pad = ImGui::GetStyle().FramePadding.x * 2;
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
    // What needs the game's picture
    ImGui::BeginDisabled(!live);
    flow("Limits", box);
    ImGui::Checkbox("Limits", &g_stageLimits);
    ImGui::SetItemTooltip("On the Game window: how far the camera goes (orange), the players' bounds (red), the floor,\n"
                          "the starting positions. In pick mode their handles can be dragged.");
    flow("Free camera", box);
    bool free = g_freeCam;
    if (ImGui::Checkbox("Free camera", &free)) SetFreeCamera(free);
    ImGui::SetItemTooltip("Only what is drawn moves: the wheel zooms, the right button pans (on the Game window).\n"
                          "The game's camera, and what the players can do, stay as they are.");
    flow("Fit the stage", pad);
    if (ImGui::Button(ICON_SEARCH " Fit the stage")) FitStage();
    ImGui::EndDisabled();
    // The game restarted, or another tool turned it off
    static int offFrames;
    offFrames = g_freeCam && g_stageView.ok && !g_stageView.free ? offFrames + 1 : 0;
    if (offFrames > 60) g_freeCam = false;
    std::vector<int> ctrls = ini.StageBgCtrls();
    const StageView &sv = g_stageView;
    if (live) {
        ImGui::TextDisabled("%s  %zu BGs, %zu drawn", sf.path.c_str(), bgs.size(), g_frameBgs.size());
        SaveState(sf.Dirty(), true);
        int running = 0;
        for (const auto &c : sv.ctrls) running += c.ran;
        ImGui::Text("stage time %d", sv.time);
        ImGui::SameLine();
        ImGui::TextDisabled("%zu BGCtrls, %d running", ctrls.size(), running);
        flow("Restart the stage", pad);
        if (ImGui::SmallButton(ICON_RESTART " Restart the stage")) SendRaw("debugStageReset()");
        ImGui::SetItemTooltip("Stage time back to 0, the BGs where they start; the BGCtrls run again from there.");
        if (ctrls.size() != sv.ctrls.size() && !sf.Dirty())
            ImGui::TextColored(Ink(kYellow), "The game has %zu BGCtrls, the def %zu.", sv.ctrls.size(), ctrls.size());
    } else {
        ImGui::TextColored(Ink(kOrange), "from disk");
        ImGui::SameLine();
        ImGui::TextDisabled("%s  %zu BGs, %zu BGCtrls", sf.path.c_str(), bgs.size(), ctrls.size());
        SaveState(sf.Dirty(), false);
    }

    bool hasModel = false;
    for (const IniSection &s : ini.sections)
        if (s.lower == "bgdef" && s.Field("model")) hasModel = true; // [BGdef] model = x.glb
    ModelSection(hasModel);
    ChecksSection(sf, ini);

    if (g_stageBg != selBg) {
        selBg = g_stageBg;
        if (selBg >= 0 && selBg < (int)bgs.size()) selSection = bgs[selBg];
    }
    if (selSection >= (int)ini.sections.size()) selSection = -1;

    // The layers: new ones go after the selected BG, in front of it
    BgHost layers;
    layers.selected = selBg >= 0 && selBg < (int)bgs.size() ? selBg : -1;
    layers.parsed = [](SourceFile &s) -> const IniFile & { return StageIni(s); };
    layers.edited = StageEdited;
    layers.showsSff = [](const std::string &path) { return NormPath(g_stageSff) == NormPath(path); };
    BgToolbar(sf, ini, layers, g_bgEdit);

    // Left: the sections
    if (ImGui::BeginChild("sections", ImVec2(em * 9, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX)) {
        for (int i = 0; i < (int)ini.sections.size(); i++) {
            const IniSection &s = ini.sections[i];
            if (s.lower.compare(0, 13, "begin action ") == 0) continue; // the stage's animations
            int bg = -1, ctrl = -1;
            for (int n = 0; n < (int)bgs.size(); n++)
                if (bgs[n] == i) bg = n;
            for (int n = 0; n < (int)ctrls.size(); n++)
                if (ctrls[n] == i) ctrl = n;
            bool drawn = false;
            for (const auto &f : g_frameBgs) drawn = drawn || f.index == bg;
            bool ran = ctrl >= 0 && ctrl < (int)sv.ctrls.size() && sv.ctrls[ctrl].ran;
            std::string label = (bg >= 0 || ctrl >= 0 ? "  " : "") + s.name + "##" + std::to_string(i);
            bool dim = live && bg >= 0 && !drawn;
            if (dim) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (ran) ImGui::PushStyleColor(ImGuiCol_Text, Ink(kGreen));
            if (ImGui::Selectable(label.c_str(), selSection == i)) {
                selSection = i;
                g_stageBg = selBg = bg;
                if (bg >= 0) ShowBg(bg);
            }
            if (showSel && selSection == i) {
                ImGui::SetScrollHereY();
                showSel = false;
            }
            if (dim || ran) ImGui::PopStyleColor();
            if (dim) ImGui::SetItemTooltip("Not drawn in the last frame (off screen, invisible or disabled).");
            if (ran) ImGui::SetItemTooltip("Ran on the stage's last step.");
        }
        ActionList(sf);
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // Right: the fields of the selected section
    ImGui::BeginGroup();
    if (selSection < 0) {
        ImGui::TextDisabled("Pick a section, or a BG on the Game window.");
        ImGui::EndGroup();
        ImGui::End();
        return;
    }
    const IniSection &s = ini.sections[selSection];
    ImGui::Text("[%s]", s.name.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_CODE " Show in Code")) {
        g_codeFile = sf.path;
        g_codeLine = g_codeScrollTo = s.line;
        g_codeHold = true;
    }
    std::string before = sf.text;
    // A BGCtrl, or a BGCtrlDef and the BGCtrls after it: when they run, and their BGs on the game
    g_stageMarked.clear();
    std::string word = FirstWord(s);
    if (word == "bgctrl" || word == "bgctrl3d" || word == "bgctrldef") {
        int goTo = -1;
        for (int n = 0; n < (int)ctrls.size(); n++) {
            bool mine = ctrls[n] == selSection;
            if (word == "bgctrldef" && ctrls[n] > selSection) {
                mine = true;
                for (int k = selSection + 1; k < ctrls[n]; k++) mine = mine && FirstWord(ini.sections[k]) != "bgctrldef";
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
        if (goTo >= 0) selSection = goTo;
        if (word != "bgctrldef" && live) {
            std::string on;
            for (int b : g_stageMarked) on += (on.empty() ? "" : ", ") + BgLabel(ini, b);
            ImGui::TextWrapped("acts on %s", on.empty() ? "no BG" : on.c_str());
        }
    }
    if (selBg >= 0) {
        // start, dragged here or on the game: the game follows at once
        float start[2];
        s.Floats("start", start, 2);
        float edit[2] = {start[0], start[1]};
        ImGui::SetNextItemWidth(em * 10);
        if (ImGui::DragFloat2("start", edit, 1.0f, -FLT_MAX, FLT_MAX, "%.0f")) {
            int dx = (int)std::lround(edit[0] - start[0]), dy = (int)std::lround(edit[1] - start[1]);
            MoveBg(selBg, dx, dy);
        }
        ImGui::SetItemTooltip("Drag to move the BG; the game follows at once (other fields need Apply).");
        for (const auto &f : g_frameBgs)
            if (f.index == selBg) {
                ImGui::SameLine();
                ImGui::TextDisabled("layer %d%s, %.0fx%.0f px", f.layer, f.tiled ? ", tiled" : "", f.rect.z, f.rect.w);
            }
    }
    if (sf.text == before &&
        ImGui::BeginTable("fields", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthStretch, 0.45f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.55f);
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
            ImGui::SetNextItemWidth(-FLT_MIN);
            std::string value = editLine == f.line ? editing : f.value;
            if (ImGui::InputText("##v", &value)) editing = value, editLine = f.line;
            if (ImGui::IsItemDeactivatedAfterEdit() && !changed) {
                editLine = -1;
                if (value != f.value && !value.empty()) changed = Ini_SetField(sf.text, s, f.key.c_str(), value);
            }
            ImGui::TableNextColumn();
            std::string picked;
            if (FieldRefCell(sf, s, f, &picked) && !changed) changed = Ini_SetField(sf.text, s, f.key.c_str(), picked);
            ImGui::TableNextColumn();
            if (ImGui::SmallButton(ICON_XMARK) && !changed) changed = Ini_SetField(sf.text, s, f.key.c_str(), "");
            ImGui::SetItemTooltip("Remove this line");
            ImGui::PopID();
        }
        // A new field
        static std::string newKey, newValue;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(em * 7);
        ImGui::InputTextWithHint("##nk", "new key", &newKey);
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        bool add = ImGui::InputTextWithHint("##nv", "value", &newValue, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::TableNextColumn();
        ImGui::TableNextColumn();
        add = ImGui::SmallButton(ICON_PLUS) || add;
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
    ImGui::EndGroup();
    ImGui::End();
}

} // namespace app
