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

void ParallaxBgLine(SourceFile &sf, const IniFile &ini, const IniSection &s, int index) {
    const SpriteFile *spr = StageSpriteFile(sf, ini);
    const SffDoc *sff = spr && spr->doc.error.empty() ? &spr->doc : nullptr;
    const std::vector<ParallaxBg> &bgs = StageParallaxBgs(sf, ini, sff, spr ? spr->version : 0);
    const ParallaxBg *b = nullptr;
    for (const ParallaxBg &one : bgs)
        if (one.index == index) b = &one;
    ParallaxView view = StageParallaxView(ini);
    if (!b || view.right <= view.left) return;
    ImGui::AlignTextToFramePadding();
    if (b->tile[0] == 1) {
        ImGui::TextDisabled("Repeats across: at any delta it reaches the screen's edges.");
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
