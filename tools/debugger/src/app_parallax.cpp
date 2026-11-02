// The Stage window's parallax tools, on src/parallax.cpp: the free camera taken along its limits, by
// hand or by itself, what shows nothing there marked on the game, a BG's delta worked out; and the
// layers listed, each with what it shows and its delta, to hide, show alone or lock on the game.
#include "app_internal.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

#include "air.h"
#include "ini.h"
#include "parallax.h"
#include "sff.h"

namespace app {

namespace {

bool g_sweeping;      // going along by itself
float g_sweepWay = 1; // to the right, or back
bool g_showEmpty = true;

const float kSweepSeconds = 6; // from one limit to the other

// The camera the frame is drawn with, as a stop
ParallaxStop FreeStop() {
    float l = g_stageView.lim[0] > 0 ? g_stageView.lim[0] : 1;
    return {"", g_freeX / l, g_freeY / l, g_freeScale};
}

// In the stage's units. The bounds stop it short when zoomed out, as Camera.XBound does.
void SweepTo(const ParallaxView &view, float x, float y, float scale) {
    const StageView &v = g_stageView;
    float l = v.lim[0], half = view.width / 2;
    float lo = view.left - half + half / scale, hi = view.right + half - half / scale;
    g_freeX = std::min(std::max(x, lo), hi) * l;
    g_freeY = y * l;
    g_freeScale = scale;
    g_freeCam = true;
    SendFreeCamera();
}

// How much of the screen each stop leaves empty, in percent, again when the stage changes
const std::vector<int> &EmptyAtStops(const std::vector<ParallaxBg> &bgs, const ParallaxView &view, const std::string &text, unsigned sprites) {
    static std::string forText;
    static unsigned forSprites;
    static ParallaxView forView;
    static std::vector<int> percent;
    static bool done;
    if (!done || forText != text || forSprites != sprites || memcmp(&forView, &view, sizeof(view)) != 0) {
        forText = text, forSprites = sprites, forView = view, done = true;
        percent.clear();
        const int cols = 64, rows = 36;
        for (const ParallaxStop &s : Parallax_Stops(view)) {
            std::vector<char> grid = Parallax_Empty(bgs, view, s, cols, rows);
            percent.push_back((int)std::lround(100.0 * std::count(grid.begin(), grid.end(), 1) / grid.size()));
        }
    }
    return percent;
}

} // namespace

ParallaxView StageParallaxView(const IniFile &ini) {
    ParallaxView view = Parallax_ViewOf(ini);
    const StageView &v = g_stageView;
    if (!StageInMatch() || !v.ok || v.lim[0] <= 0 || v.cam[6] <= 0) return view;
    float l = v.lim[0];
    view.width = v.cam[4] / l, view.height = g_frameScr.y / v.cam[6] / l;
    view.left = v.lim[1] / l, view.right = v.lim[2] / l, view.high = v.lim[3] / l, view.low = v.lim[4] / l;
    if (v.lim[13] > 0) view.minScale = std::min(1.0f, v.lim[13]), view.lift = v.lim[14] / l, view.zoffset = v.lim[15] / l;
    return view;
}

const std::vector<ParallaxBg> &StageParallaxBgs(const SourceFile &sf, const IniFile &ini, const SffDoc *sff, unsigned sprites) {
    static std::string text;
    static unsigned version;
    static std::vector<ParallaxBg> bgs;
    static bool done;
    if (!done || text != sf.text || version != sprites) {
        text = sf.text, version = sprites, done = true;
        bgs.clear();
        if (sff) {
            AirFile air;
            Air_Parse(sf.text, air);
            bgs = Parallax_Bgs(ini, air, *sff);
        }
    }
    return bgs;
}

void ParallaxSection(const SourceFile &sf, const IniFile &ini) {
    if (!ImGui::CollapsingHeader(ICON_FAST " Camera sweep###sweep")) {
        g_sweeping = false;
        return;
    }
    const SpriteFile *spr = StageSpriteFile(sf, ini);
    const SffDoc *sff = spr && spr->doc.error.empty() ? &spr->doc : nullptr;
    unsigned sprites = spr ? spr->version : 0;
    const std::vector<ParallaxBg> &bgs = StageParallaxBgs(sf, ini, sff, sprites);
    ParallaxView view = StageParallaxView(ini);
    bool live = StageInMatch() && g_stageView.ok && g_stageView.lim[0] > 0;
    float em = ImGui::GetFontSize();
    ParallaxStop at = live && g_freeCam ? FreeStop() : ParallaxStop{"", 0, 0, 1};

    if (!live) {
        g_sweeping = false;
        ImGui::TextDisabled("Moves the camera on the game: it needs this stage in the match.");
    }
    ImGui::BeginDisabled(!live);
    ImGui::BeginDisabled(view.right <= view.left);
    if (ImGui::Button(g_sweeping ? ICON_PAUSE " Stop" : ICON_PLAY " Sweep")) g_sweeping = !g_sweeping;
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Takes the camera from one limit to the other and back, for the layers' parallax. The game's own\n"
                          "camera and the fighters stay where they are.");
    ImGui::SameLine();
    ImGui::Checkbox("Mark what shows nothing", &g_showEmpty);
    ImGui::SetItemTooltip("With the camera moved here: in red on the game, the parts of the screen no solid BG draws on.\n"
                          "A sprite's transparent parts count as drawn on, and BGCtrls are not followed.");
    ImGui::SameLine();
    ImGui::BeginDisabled(!g_freeCam);
    if (ImGui::Button(ICON_REVERT " Back")) {
        g_sweeping = false;
        SetFreeCamera(false);
    }
    ImGui::SetItemTooltip("Back to the game's camera");
    ImGui::EndDisabled();

    float x = at.x, y = at.y, scale = at.scale;
    bool moved = false;
    auto slider = [&](const char *label, float *value, float lo, float hi, const char *format, const char *tip) {
        if (lo >= hi) return;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - em * 4);
        moved = ImGui::SliderFloat(label, value, lo, hi, format) || moved;
        ImGui::SetItemTooltip("%s", tip);
    };
    slider("Across", &x, view.left, view.right, "%.0f", "From boundleft to boundright");
    slider("Height", &y, view.high, view.low, "%.0f", "From boundhigh to boundlow");
    slider("Zoom", &scale, view.minScale, 1, "%.2f", "As far out as the camera goes, to no zoom");
    if (moved) g_sweeping = false;
    if (g_sweeping && view.right > view.left) {
        x += g_sweepWay * ImGui::GetIO().DeltaTime * (view.right - view.left) / kSweepSeconds;
        if (x >= view.right) x = view.right, g_sweepWay = -1;
        if (x <= view.left) x = view.left, g_sweepWay = 1;
        moved = true;
    }
    if (moved && live) SweepTo(view, x, y, scale);

    // Where the camera shows the most: a click goes there
    std::vector<ParallaxStop> stops = Parallax_Stops(view);
    const std::vector<int> &empty = EmptyAtStops(bgs, view, sf.text, sprites);
    for (size_t i = 0; i < stops.size() && i < empty.size(); i++) {
        bool worse = empty[i] > empty[0];
        // "the camera's left limit, up high": "Left, up high"
        std::string label = i ? stops[i].name.substr(strlen("the camera's ")) : "Start";
        if (size_t at = label.find(" limit"); at != std::string::npos) label.erase(at, strlen(" limit"));
        label[0] = (char)toupper((unsigned char)label[0]);
        if (worse) label += "  " + std::to_string(empty[i]) + "%";
        if (i) SameLineFits(ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2);
        if (worse) ImGui::PushStyleColor(ImGuiCol_Text, Ink(kRed));
        ImGui::PushID((int)i);
        if (ImGui::Button(label.c_str()) && live) {
            g_sweeping = false;
            SweepTo(view, stops[i].x, stops[i].y, stops[i].scale);
        }
        ImGui::PopID();
        if (worse) ImGui::PopStyleColor();
        ImGui::SetItemTooltip("%s%s", stops[i].name.c_str(), worse ? ": this much of the screen shows no BG" : "");
    }
    if (!sff) ImGui::TextColored(Ink(kYellow), "The SFF could not be read: nothing is known of the BGs' sizes.");
    ImGui::EndDisabled();
}

namespace {

bool g_floorHandles; // the selected floor's corners on the Game window

const ParallaxBg *BgOf(const SourceFile &sf, const IniFile &ini, int index) {
    const SpriteFile *spr = StageSpriteFile(sf, ini);
    const SffDoc *sff = spr && spr->doc.error.empty() ? &spr->doc : nullptr;
    for (const ParallaxBg &one : StageParallaxBgs(sf, ini, sff, spr ? spr->version : 0))
        if (one.index == index) return &one;
    return nullptr;
}

// The camera the frame was drawn with
ParallaxStop ShownStop() {
    const StageView &v = g_stageView;
    float l = v.lim[0] > 0 ? v.lim[0] : 1;
    if (g_freeCam) return FreeStop();
    return {"", v.cam[0] / l, v.cam[2] > 0 ? v.cam[1] / v.cam[2] / l : 0, v.cam[2] > 0 ? v.cam[2] : 1};
}

// Where a BG that repeats meets itself: the end of one copy and the start of the next, side by side
// and enlarged, to see the join as the game will draw it
void JoinButton(const SourceFile &sf, const IniFile &ini, const ParallaxBg &b) {
    int axis = b.tile[0] ? 0 : b.tile[1] ? 1 : -1;
    const SpriteFile *spr = StageSpriteFile(sf, ini);
    if (axis < 0 || !spr || b.sprite < 0) return;
    if (ImGui::Button(ICON_SEARCH " See the join")) ImGui::OpenPopup("join");
    ImGui::SetItemTooltip("Where this layer meets itself as it repeats.");
    if (!ImGui::BeginPopup("join")) return;
    static bool down;
    if (b.tile[0] && b.tile[1]) {
        ImGui::Checkbox("Down, not across", &down);
        axis = down ? 1 : 0;
    }
    const SffSprite &image = spr->doc.sprites[b.sprite];
    GLuint tex = SpriteTexture(const_cast<SpriteFile &>(*spr), b.sprite);
    // A window of the picture on each side of the join, the same size, at 4 times
    const float zoom = 4, each = ImGui::GetFontSize() * 10;
    float span = each / zoom, cross = 2 * each / zoom;
    float u = std::min(1.0f, span / std::max(image.w, 1)), v = std::min(1.0f, span / std::max(image.h, 1));
    float crossU = std::min(1.0f, cross / std::max(image.w, 1)), crossV = std::min(1.0f, cross / std::max(image.h, 1));
    ImDrawList *draw = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 size;
    if (axis == 0) {
        float w = u * image.w * zoom, h = crossV * image.h * zoom;
        draw->AddImage(TexRef(tex), p, ImVec2(p.x + w, p.y + h), ImVec2(1 - u, 0), ImVec2(1, crossV));
        draw->AddImage(TexRef(tex), ImVec2(p.x + w, p.y), ImVec2(p.x + 2 * w, p.y + h), ImVec2(0, 0), ImVec2(u, crossV));
        draw->AddLine(ImVec2(p.x + w, p.y - 4), ImVec2(p.x + w, p.y), IM_COL32(255, 220, 60, 255), 2);
        size = ImVec2(2 * w, h);
    } else {
        float h = v * image.h * zoom, w = crossU * image.w * zoom;
        draw->AddImage(TexRef(tex), p, ImVec2(p.x + w, p.y + h), ImVec2(0, 1 - v), ImVec2(crossU, 1));
        draw->AddImage(TexRef(tex), ImVec2(p.x, p.y + h), ImVec2(p.x + w, p.y + 2 * h), ImVec2(0, 0), ImVec2(crossU, v));
        draw->AddLine(ImVec2(p.x - 4, p.y + h), ImVec2(p.x, p.y + h), IM_COL32(255, 220, 60, 255), 2);
        size = ImVec2(w, 2 * h);
    }
    ImGui::Dummy(size);
    ImGui::TextDisabled("The mark shows the join, at 4 times.");
    ImGui::EndPopup();
}

// Making a BG a floor, or what a floor can do. True when the text changed.
bool FloorLine(SourceFile &sf, const IniSection &s, const ParallaxBg &b, const ParallaxView &view) {
    // A parallax BG without a width is a plain picture: making it a floor gives it one
    bool floor = Ini_BgType(s) == kIniBgParallax && s.Field("width");
    bool edited = false;
    if (!floor && Ini_BgType(s) != kIniBgNormal && Ini_BgType(s) != kIniBgParallax) return false;
    if (!floor) {
        if (ImGui::Button(ICON_STAGE " Make it a floor")) {
            int top = (int)std::lround(b.w / b.units);
            // The engine centres a floor on its start: this keeps it where it is
            float own[2] = {0, 0};
            s.Floats("start", own, 2);
            float start[2] = {(b.left + b.w / 2) / b.units, own[1]};
            std::string text = sf.text; // all of it, or nothing
            auto set = [&](const char *key, const std::string &value) {
                IniFile parsed;
                Ini_Parse(text, parsed); // a field added moves what follows
                for (const IniSection &now : parsed.sections)
                    if (now.line == s.line) return Ini_SetField(text, now, key, value);
                return false;
            };
            if (set("type", "parallax") && set("start", Ini_Floats(start, 2)) &&
                set("width", std::to_string(top) + ", " + std::to_string(top * 2))) {
                sf.text = text;
                g_floorHandles = edited = true;
                StageEdited(sf);
            }
        }
        ImGui::SetItemTooltip("type = parallax, its bottom wider than its top: it scrolls and widens more the nearer it is.");
        return edited;
    }
    ImGui::Checkbox("Floor handles", &g_floorHandles);
    ImGui::SetItemTooltip("On the Game window: this floor's corners and a grid. Drag a corner to change the width of its top or\n"
                          "its bottom; the lines should run as the picture's own lines do.");
    float fit = Parallax_FloorDelta(b, view);
    if (fit < 0) return false;
    float value = std::round(fit / b.units * 1000) / 1000;
    if (std::fabs(value - b.delta[0] / b.units) < 0.0005f) return false;
    SameLineFits(ImGui::CalcTextSize("With the fighters").x + ImGui::GetStyle().FramePadding.x * 2);
    if (ImGui::Button("With the fighters")) {
        float both[2] = {value, b.delta[1] / b.units};
        edited = Ini_SetField(sf.text, s, "delta", Ini_Floats(both, 2));
        if (edited) StageEdited(sf);
    }
    ImGui::SetItemTooltip("Delta %.3g: the row of the floor at the fighters' feet scrolls as they do, so they do not slide on it.", value);
    return edited;
}

} // namespace

bool FloorOverlay(ImDrawList *draw, bool hovered, bool clicked, SourceFile &sf, const IniFile &ini, const std::function<ImVec2(float, float)> &toScreen) {
    const StageView &v = g_stageView;
    static int drag = -1; // 0 the top, 1 the bottom
    static float rest;
    const ParallaxBg *b = g_floorHandles && v.ok && v.lim[0] > 0 ? BgOf(sf, ini, g_stageBg) : nullptr;
    const IniSection *s = nullptr;
    std::vector<int> sections = ini.StageBgs();
    if (b && g_stageBg < (int)sections.size()) s = &ini.sections[sections[g_stageBg]];
    if (!s || Ini_BgType(*s) != kIniBgParallax || !s->Field("width")) {
        drag = -1;
        return false;
    }
    ParallaxView view = StageParallaxView(ini);
    ParallaxShape shape = Parallax_Shape(*b, view, ShownStop());
    float l = v.lim[0];
    auto point = [&](float x, float y) { return toScreen((x * l + v.cam[4] / 2) * v.cam[5], y * l * v.cam[6]); };
    ImVec2 tl = point(shape.topLeft, shape.top), tr = point(shape.topRight, shape.top);
    ImVec2 bl = point(shape.bottomLeft, shape.bottom), br = point(shape.bottomRight, shape.bottom);

    // The grid
    ImU32 line = IM_COL32(255, 220, 60, 150), edge = IM_COL32(255, 220, 60, 255);
    const int kLines = 8;
    for (int i = 0; i <= kLines; i++) {
        float t = (float)i / kLines;
        draw->AddLine(ImVec2(tl.x + (tr.x - tl.x) * t, tl.y), ImVec2(bl.x + (br.x - bl.x) * t, bl.y), i == 0 || i == kLines ? edge : line);
    }
    draw->AddLine(tl, tr, edge, 2);
    draw->AddLine(bl, br, edge, 2);

    // The corners: both of a side move together, about the middle
    ImVec2 m = ImGui::GetMousePos();
    float reach = ImGui::GetFontSize() * 0.6f;
    ImVec2 corners[4] = {tl, tr, bl, br};
    int over = -1;
    for (int i = 0; i < 4; i++) {
        bool hot = (i / 2 == drag) || (drag < 0 && hovered && std::hypot(m.x - corners[i].x, m.y - corners[i].y) < reach);
        if (hot && drag < 0) over = i;
        draw->AddCircleFilled(corners[i], reach * 0.5f, hot ? IM_COL32_WHITE : edge);
    }
    if (over >= 0) {
        ImGui::SetTooltip("Drag: the width of its %s", over < 2 ? "top" : "bottom");
        if (clicked) drag = over / 2, rest = 0;
    }
    if (drag >= 0) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            drag = -1;
        } else if (ImGui::GetIO().MouseDelta.x != 0) {
            float perUnit = std::max(0.01f, (tr.x - tl.x) / std::max(1.0f, b->w)); // screen pixels
            bool rightSide = m.x > (tl.x + tr.x) / 2;
            rest += (rightSide ? 2 : -2) * ImGui::GetIO().MouseDelta.x / perUnit;
            int change = (int)std::trunc(rest);
            if (change) {
                rest -= change;
                float units = b->units;
                int width[2] = {(int)std::lround(b->w / units), (int)std::lround(b->w * b->widen / units)};
                width[drag] = std::max(1, width[drag] + change);
                if (Ini_SetField(sf.text, *s, "width", std::to_string(width[0]) + ", " + std::to_string(width[1]))) StageEdited(sf);
            }
        }
    }
    return drag >= 0 || over >= 0;
}

void ParallaxBgLine(SourceFile &sf, const IniFile &ini, const IniSection &s, int index) {
    const ParallaxBg *b = BgOf(sf, ini, index);
    ParallaxView view = StageParallaxView(ini);
    if (b && !b->linked && FloorLine(sf, s, *b, view)) return; // what was parsed is old
    if (b) JoinButton(sf, ini, *b);
    if (!b || view.right <= view.left) return;
    ImGui::AlignTextToFramePadding();
    if (b->tile[0] == 1) {
        ImGui::TextDisabled("Repeats across: reaches the edges");
        ImGui::SetItemTooltip("At any delta it reaches the screen's edges.");
        return;
    }
    float delta = b->delta[0] / b->units;
    float wide = b->w + (std::max(b->tile[0], 1) - 1) * b->step[0];
    ImGui::TextDisabled("%.0f wide, needs %.0f", wide, Parallax_WidthFor(view, b->delta[0]));
    ImGui::SetItemTooltip("To stay over the whole screen from one limit of the camera to the other at delta %.3g,\n"
                          "a BG centred on the screen needs that width.", delta);
    float fit = b->tile[0] ? -1 : Parallax_FitDelta(*b, view);
    if (fit < 0 || b->linked) return;
    float value = std::floor(fit / b->units * 1000) / 1000; // rounded up, the edge would show
    if (std::fabs(value - delta) < 0.0005f) return;
    char label[48];
    snprintf(label, sizeof(label), "Use delta %.3g", value);
    SameLineFits(ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2);
    if (ImGui::Button(label)) {
        float both[2] = {value, b->delta[1] / b->units};
        if (Ini_SetField(sf.text, s, "delta", Ini_Floats(both, 2))) StageEdited(sf);
    }
    ImGui::SetItemTooltip("The largest delta that keeps this BG over the whole screen, from one limit of the camera to the other.");
}

void ParallaxOverlay(ImDrawList *draw, const SourceFile &sf, const IniFile &ini, const std::function<ImVec2(float, float)> &toScreen) {
    const StageView &v = g_stageView;
    if (!g_showEmpty || !g_freeCam || !v.ok || v.lim[0] <= 0) return;
    const SpriteFile *spr = StageSpriteFile(sf, ini);
    if (!spr || !spr->doc.error.empty()) return;
    ParallaxView view = StageParallaxView(ini);
    // What is hidden here stays in the stage: it still covers what is behind it
    const std::vector<ParallaxBg> &bgs = StageParallaxBgs(sf, ini, &spr->doc, spr->version);
    const int cols = 96, rows = 54;
    std::vector<char> grid = Parallax_Empty(bgs, view, FreeStop(), cols, rows);
    float l = v.lim[0], w = view.width / cols, h = view.height / rows;
    auto corner = [&](int x, int y) { return toScreen(x * w * l * v.cam[5], y * h * l * v.cam[6]); };
    for (int y = 0; y < rows; y++)
        for (int x = 0; x < cols; x++) {
            if (!grid[(size_t)y * cols + x]) continue;
            int end = x; // a run, drawn at once: no seams between its cells
            while (end + 1 < cols && grid[(size_t)y * cols + end + 1]) end++;
            draw->AddRectFilled(corner(x, y), corner(end + 1, y + 1), IM_COL32(230, 40, 40, 110));
            x = end;
        }
}

// ---------------------------------------------------------------------------
// The scale: P1's standing sprite where the players' bounds stop it, and how high a jump takes it
// ---------------------------------------------------------------------------

bool g_stageScale;

namespace {

// A character's jump, from its constants: how far up its feet go and how tall it stands, in its
// AIR units. Read once for each file.
float JumpHeight(const std::string &cns) {
    static std::map<std::string, float> known;
    auto it = known.find(cns);
    if (it != known.end()) return it->second;
    IniFile ini;
    Ini_Parse(ReadWholeFile(GamePath(cns)), ini);
    float jump[2] = {0, 0}, gravity = 0;
    bool found[2] = {};
    for (const IniSection &s : ini.sections) {
        if (s.lower == "velocity") found[0] = s.Floats("jump.neu", jump, 2);
        if (s.lower == "movement") found[1] = s.Floats("yaccel", &gravity, 1);
    }
    float height = found[0] && found[1] && gravity > 0 ? jump[1] * jump[1] / (2 * gravity) : 0;
    return known[cns] = height;
}

} // namespace

void ScaleOverlay(ImDrawList *draw, const std::function<ImVec2(float, float)> &toScreen) {
    const StageView &v = g_stageView;
    if (!g_stageScale || !v.ok) return;
    const FrameEntity *fe = nullptr;
    for (const FrameEntity &e : g_frameEnts)
        if (e.pn == 1 && !e.helper) fe = &e;
    std::string path = CharFile(1, "sprite");
    if (path.empty()) AirPath(1); // asks the game
    if (!fe || path.empty() || fe->k.y == 0) return;
    SpriteFile &spr = SpriteFileAt(path);
    int stand = Sff_FindSprite(spr.doc, 0, 0);
    if (stand < 0) return;
    const SffSprite &s = spr.doc.sprites[stand];
    GLuint tex = SpriteTexture(spr, stand);
    float kx = fabsf(fe->k.x), ky = fabsf(fe->k.y);
    auto screen = [&](float x, float y) {
        ImVec2 p = v.ToPicture(x, y);
        return p;
    };
    // leftbound and rightbound are far out by default: there, the screen's edge at the camera's limit
    // stops a player first, a little inside it
    float margin = 15 * v.lim[0];
    for (float bound : {std::max(v.lim[9], v.lim[5] + margin), std::min(v.lim[10], v.lim[6] - margin)}) {
        ImVec2 feet = screen(bound, 0);
        ImVec2 a = toScreen(feet.x - s.x * kx, feet.y - s.y * ky), b = toScreen(feet.x + (s.w - s.x) * kx, feet.y + (s.h - s.y) * ky);
        draw->AddImage(TexRef(tex), a, b, ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 120));
    }
    std::string cns = CharFile(1, "cns");
    float jump = cns.empty() ? 0 : JumpHeight(cns);
    if (jump <= 0) return;
    // The top of the head at the top of a jump
    ImVec2 left = screen(v.lim[5], 0), right = screen(v.lim[6], 0);
    float y = left.y - (jump + s.y) * ky;
    ImVec2 from = toScreen(left.x, y), to = toScreen(right.x, y);
    ImU32 ink = IM_COL32(120, 255, 200, 200);
    draw->AddLine(from, to, ink);
    draw->AddText(ImVec2(std::max(from.x, draw->GetClipRectMin().x) + 4, from.y + 2), ink, "highest head in a jump");
}

// ---------------------------------------------------------------------------
// The layers: hidden, alone and locked are this session's, not the def's. A BG goes by its section's
// name and place among those of the name, so that moving or adding BGs does not move them.
// ---------------------------------------------------------------------------

namespace {

std::set<std::string> g_hiddenBgs, g_lockedBgs;
std::string g_aloneBg;

std::vector<std::string> LayerKeys(const IniFile &ini) {
    std::vector<std::string> keys;
    std::map<std::string, int> seen;
    for (int section : ini.StageBgs()) {
        const std::string &name = ini.sections[section].lower;
        keys.push_back(name + "#" + std::to_string(seen[name]++));
    }
    return keys;
}

// A small on or off button: lit when on
bool LayerToggle(const char *icon, bool on, const char *tip) {
    if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    bool clicked = IconButton(icon);
    if (on) ImGui::PopStyleColor();
    ImGui::SetItemTooltip("%s", tip);
    return clicked;
}

} // namespace

void StageLayersClear() {
    g_hiddenBgs.clear(), g_lockedBgs.clear(), g_aloneBg.clear();
}

bool StageBgLocked(const IniFile &ini, int index) {
    std::vector<std::string> keys = LayerKeys(ini);
    return index >= 0 && index < (int)keys.size() && g_lockedBgs.count(keys[index]);
}

void SendHiddenBgs(const IniFile *ini) {
    std::string list;
    if (ini) {
        std::vector<std::string> keys = LayerKeys(*ini);
        for (int n = 0; n < (int)keys.size(); n++)
            if (g_aloneBg.empty() ? g_hiddenBgs.count(keys[n]) != 0 : keys[n] != g_aloneBg) list += (list.empty() ? "" : ",") + std::to_string(n);
    }
    static std::string sent;
    static unsigned long game;
    if (!Connected() || (game == g_bridge.Pid() && sent == list)) return;
    game = g_bridge.Pid(), sent = list;
    SendRaw("debugStageBgHide('" + list + "')");
}

void LayersSection(const SourceFile &sf, const IniFile &ini, BgEdit &edit) {
    std::vector<int> sections = ini.StageBgs();
    char header[64];
    snprintf(header, sizeof(header), ICON_LAYERS " Layers (%zu)###layers", sections.size());
    if (!ImGui::CollapsingHeader(header)) return;
    const SpriteFile *spr = StageSpriteFile(sf, ini);
    const SffDoc *sff = spr && spr->doc.error.empty() ? &spr->doc : nullptr;
    const std::vector<ParallaxBg> &bgs = StageParallaxBgs(sf, ini, sff, spr ? spr->version : 0);
    std::vector<std::string> keys = LayerKeys(ini);
    float em = ImGui::GetFontSize(), row = ImGui::GetFrameHeightWithSpacing();
    float height = row * std::min<float>((float)sections.size() + 1, 8);
    if (!ImGui::BeginTable("layers", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit, ImVec2(0, height))) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("##hide");
    ImGui::TableSetupColumn("##alone");
    ImGui::TableSetupColumn("##lock");
    ImGui::TableSetupColumn("##picture");
    ImGui::TableSetupColumn("Layer", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Delta");
    ImGui::TableSetupColumn("##where");
    ImGui::TableHeadersRow();
    for (int n = 0; n < (int)sections.size(); n++) {
        const IniSection &s = ini.sections[sections[n]];
        const ParallaxBg *b = nullptr;
        for (const ParallaxBg &one : bgs)
            if (one.index == n) b = &one;
        ImGui::PushID(n);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        bool hidden = g_hiddenBgs.count(keys[n]) != 0, alone = g_aloneBg == keys[n], locked = g_lockedBgs.count(keys[n]) != 0;
        if (LayerToggle(hidden ? ICON_EYE_OFF : ICON_WATCH, hidden, "Hides this layer on the game, for now: the def does not change.")) {
            if (hidden) g_hiddenBgs.erase(keys[n]);
            else g_hiddenBgs.insert(keys[n]);
        }
        ImGui::TableNextColumn();
        if (LayerToggle(ICON_SOLO, alone, "Shows only this layer on the game, for now")) g_aloneBg = alone ? "" : keys[n];
        ImGui::TableNextColumn();
        if (LayerToggle(ICON_LOCK, locked, "A click on the game no longer picks or drags this layer")) {
            if (locked) g_lockedBgs.erase(keys[n]);
            else g_lockedBgs.insert(keys[n]);
        }
        ImGui::TableNextColumn();
        if (b && b->sprite >= 0 && spr) {
            const SffSprite &image = spr->doc.sprites[b->sprite];
            float fit = std::min(em * 2 / std::max(image.w, 1), ImGui::GetFrameHeight() / std::max(image.h, 1));
            ImGui::Image(TexRef(SpriteTexture(const_cast<SpriteFile &>(*spr), b->sprite)), ImVec2(image.w * fit, image.h * fit));
        }
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        if (ImGui::Selectable(s.name.c_str(), g_stageBg == n)) {
            g_stageBg = n;
            ShowStageBg(n);
        }
        ImGui::SetItemTooltip("[%s]\nLayers are drawn in this order: the last one is in front.", s.name.c_str());
        ImGui::TableNextColumn();
        if (b && !b->linked) {
            float delta = b->delta[0] / b->units, deltaY = b->delta[1] / b->units;
            ImGui::SetNextItemWidth(em * 3);
            if (ImGui::DragFloat("##delta", &delta, 0.005f, 0, 10, "%.3g")) {
                edit = [n, delta, deltaY](SourceFile &file) {
                    float both[2] = {delta, deltaY};
                    std::vector<int> now = StageIni(file).StageBgs();
                    if (n < (int)now.size() && Ini_SetField(file.text, StageIni(file).sections[now[n]], "delta", Ini_Floats(both, 2))) StageEdited(file);
                    return n;
                };
            }
            ImGui::SetItemTooltip("How much of the camera's movement it follows: 1 with the fighters, 0 not at all. Drag it; the game follows.");
        } else if (b) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("linked");
            ImGui::SetItemTooltip("positionlink: it has the delta of the layer it follows");
        }
        ImGui::TableNextColumn();
        float layerNo = 0;
        s.Floats("layerno", &layerNo, 1);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", layerNo != 0 ? "front" : "back");
        ImGui::SetItemTooltip(layerNo != 0 ? "layerno 1: in front of the fighters" : "layerno 0: behind the fighters");
        ImGui::PopID();
    }
    ImGui::EndTable();
    if (!g_hiddenBgs.empty() || !g_aloneBg.empty()) {
        if (ImGui::Button(ICON_WATCH " Show all")) g_hiddenBgs.clear(), g_aloneBg.clear();
        ImGui::SetItemTooltip("Back to every layer on the game");
    }
}

} // namespace app
