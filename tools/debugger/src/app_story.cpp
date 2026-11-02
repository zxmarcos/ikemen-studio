// The Storyboard window's scenes: a storyboard from disk, the game running or not. A strip of its
// scenes, the timeline of the one selected (layers, sounds, fades, its end, dragged), and a preview
// drawn here from the def, whose layers are picked and moved. The preview is close, not exact:
// texts in this program's font, BG videos and shaders not drawn. While the game plays the def, the
// Game window is the picture and the playhead moves the game (debugStoryboardSeek).
#include "app_internal.h"

#include <cfloat>
#include <cmath>

#include "imgui_stdlib.h"
#include "ini.h"
#include "story.h"

namespace app {

namespace {

const ImU32 kPlayhead = IM_COL32(255, 220, 60, 255);

std::string LowerOf(std::string s) {
    for (char &c : s) c = (char)tolower((unsigned char)c);
    return s;
}

struct StoryState {
    std::string text;       // the def's text g.doc was read from
    std::string path;
    StoryDoc doc;
    int scene = 0, tick = 0;
    bool running = false;   // the preview plays
    double runFrom = 0;     // when tick was tick0
    int tick0 = 0;
    bool seekOnStart = false; // the game was asked to play it: at the scene and time chosen here
    bool changed = false;   // the text changed this frame: nothing else writes
    // A drag on a timeline
    int dragPart = 0, dragSpan = 1, dragA = 0, dragB = 0;
    bool scrubbing = false;
    // A drag on the preview
    std::string moveKey;
    ImVec2 moveFrom;
    float moveValue[2] = {0, 0};
    int moveScene = -1;
} g;

// Writes a field of a scene, once a frame: the sections' lines are old after an edit.
void SetField(StoryHost &h, int scene, const std::string &key, const std::string &value) {
    if (g.changed || scene < 0 || scene >= (int)g.doc.scenes.size()) return;
    g.changed = Ini_SetField(h.sf->text, h.ini->sections[g.doc.scenes[scene].section], key.c_str(), value);
}

void SetTime(StoryHost &h, int scene, const std::string &key, int value) {
    SetField(h, scene, key, value ? std::to_string(value) : std::string());
}

void Seek(StoryHost &h, int scene, int tick) {
    g.scene = scene, g.tick = tick;
    g.tick0 = tick, g.runFrom = ImGui::GetTime();
    if (h.playing) SendRaw("debugStoryboardSeek(" + std::to_string(scene) + ", " + std::to_string(tick) + ")");
}

// The time a scene is seen at first: past its fade in.
int ShownFrom(const StoryScene &s) { return std::min(s.in.time, Story_Length(s) - 1); }

void SelectScene(StoryHost &h, int scene) {
    Seek(h, scene, ShownFrom(g.doc.scenes[scene]));
    h.select(h.ini->sections[g.doc.scenes[scene].section].lower, "");
}

// ---------------------------------------------------------------------------
// The preview
// ---------------------------------------------------------------------------

struct PreviewHit {
    ImVec2 lo = ImVec2(FLT_MAX, FLT_MAX), hi = ImVec2(-FLT_MAX, -FLT_MAX);
    std::string prefix;
    void Add(ImVec2 p) {
        lo = ImVec2(std::min(lo.x, p.x), std::min(lo.y, p.y));
        hi = ImVec2(std::max(hi.x, p.x), std::max(hi.y, p.y));
    }
};

// An action's sprite at that time, or the sprite named; false when it has none.
bool SpriteAt(StoryHost &h, int action, const int spr[2], long tick, AirElem &out) {
    if (action >= 0) {
        const AirFile &air = AirOf(*h.sf);
        auto it = air.actions.find(action);
        if (it == air.actions.end() || it->second.elems.empty()) return false;
        out = it->second.elems[Air_ElemAt(it->second, tick)];
        return true;
    }
    if (spr[0] < 0) return false;
    out = AirElem();
    out.group = spr[0], out.image = spr[1];
    return true;
}

void DrawSpriteAt(StoryHost &h, ImDrawList *draw, const AirElem &e, AirStyle st, const std::function<ImVec2(float, float)> &at,
                  PreviewHit *hit) {
    SpriteFile *spf = h.sprites;
    int i = spf ? Sff_FindSprite(spf->doc, e.group, e.image) : -1;
    GLuint tex = i >= 0 ? SpriteTexture(*spf, i) : 0;
    if (!tex) return;
    const SffSprite &s = spf->doc.sprites[i];
    DrawStyledSprite(draw, tex, s, st, (float)e.x, (float)e.y, at);
    if (hit) {
        float fx = st.flipH ? -1.0f : 1.0f, fy = st.flipV ? -1.0f : 1.0f;
        float x = e.x * fx - s.x * st.xscale * fx, y = e.y * fy - s.y * st.yscale * fy;
        hit->Add(at(x, y));
        hit->Add(at(x + s.w * st.xscale * fx, y + s.h * st.yscale * fy));
    }
}

// A BG of the scene: its sprite or action, at its start, once. Others: named on the picture.
void DrawBgs(StoryHost &h, ImDrawList *draw, const StoryScene &sc, int layer, int tick, ImVec2 o, float k) {
    if (sc.bg.empty()) return;
    for (const IniSection &s : h.ini->sections) {
        if (s.lower.substr(0, s.lower.find(' ')) != sc.bg) continue;
        const IniField *lay = s.Field("layerno");
        if ((lay ? atoi(lay->value.c_str()) : 0) != layer) continue;
        float start[2] = {0, 0};
        s.Floats("start", start, 2);
        const IniField *type = s.Field("type"), *sprite = s.Field("spriteno"), *action = s.Field("actionno");
        if (type && LowerOf(type->value) == "video") {
            const IniField *path = s.Field("path");
            draw->AddText(ImVec2(o.x + 6, o.y + 4), IM_COL32(255, 255, 255, 170),
                          ("Video, not shown here: " + (path ? path->value : std::string("?"))).c_str());
            continue;
        }
        int spr[2] = {-1, 0};
        if (sprite) sscanf(sprite->value.c_str(), "%d , %d", &spr[0], &spr[1]);
        AirElem e;
        if (!SpriteAt(h, action ? atoi(action->value.c_str()) : -1, spr, tick, e)) continue;
        auto at = [&](float x, float y) { return ImVec2(o.x + (start[0] + x) * k, o.y + (start[1] + y) * k); };
        DrawSpriteAt(h, draw, e, e.style, at, nullptr);
    }
}

void DrawLayer(StoryHost &h, ImDrawList *draw, const StoryScene &sc, const StoryLayer &l, int tick, ImVec2 o, float k, PreviewHit &hit) {
    float moved[2];
    Story_Motion(l, std::max(tick - l.start + 1, 0), moved);
    float x = l.offset[0] + sc.layerall[0] + moved[0], y = l.offset[1] + sc.layerall[1] + moved[1];
    hit.prefix = l.key + ".";
    AirElem e;
    if (SpriteAt(h, l.anim, l.spr, tick - l.start, e)) {
        AirStyle st = e.style;
        st.xscale *= l.scale[0], st.yscale *= l.scale[1], st.angle += l.angle;
        if (l.facing < 0) st.flipH = !st.flipH;
        auto at = [&](float ax, float ay) { return ImVec2(o.x + (x + ax) * k, o.y + (y + ay) * k); };
        DrawSpriteAt(h, draw, e, st, at, &hit);
    }
    if (l.text.empty() || l.font[0] < 0) return; // the game draws no text without a font
    // A line of 1/30 of the picture when the font's height is not given
    float size = (l.font[7] > 0 ? l.font[7] : g.doc.coord[1] / 30.0f) * k * l.scale[1];
    ImFont *font = ImGui::GetFont();
    ImU32 col = IM_COL32(std::clamp(l.font[3], 0, 255), std::clamp(l.font[4], 0, 255), std::clamp(l.font[5], 0, 255), std::clamp(l.font[6], 0, 255));
    // The text typed so far, a line at a time; align 1 from x on, 0 around it, -1 up to it
    size_t typed = 0;
    for (int n = Story_Typed(l, tick, (int)l.text.size()); n > 0 && typed < l.text.size(); n--) {
        typed++;
        while (typed < l.text.size() && ((unsigned char)l.text[typed] & 0xC0) == 0x80) typed++;
    }
    float ty = o.y + y * k - size;
    for (size_t from = 0; from <= typed;) {
        size_t nl = l.text.find('\n', from);
        size_t to = std::min(nl == std::string::npos ? l.text.size() : nl, typed);
        const char *text = l.text.c_str();
        float width = font->CalcTextSizeA(size, FLT_MAX, 0, l.text.c_str() + from, l.text.c_str() + (nl == std::string::npos ? l.text.size() : nl)).x;
        float tx = o.x + x * k - (l.font[2] > 0 ? 0 : l.font[2] < 0 ? width : width / 2);
        draw->AddText(font, size, ImVec2(tx, ty), col, text + from, text + to);
        hit.Add(ImVec2(tx, ty));
        hit.Add(ImVec2(tx + width, ty + size));
        ty += size * 1.2f;
        if (nl == std::string::npos) break;
        from = nl + 1;
    }
}

void Preview(StoryHost &h, const StoryScene &sc) {
    float em = ImGui::GetFontSize();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    bool open = ImGui::BeginChild("storyview", ImVec2(em * 20, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNavInputs);
    ImGui::PopStyleVar();
    if (!open) {
        ImGui::EndChild();
        return;
    }
    ImVec2 p0 = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail();
    size = ImVec2(std::max(size.x, 1.0f), std::max(size.y, 1.0f));
    ImGui::InvisibleButton("picture", size);
    bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive(), pressed = ImGui::IsItemActivated();
    float k = std::min(size.x / g.doc.coord[0], size.y / g.doc.coord[1]);
    ImVec2 pic(g.doc.coord[0] * k, g.doc.coord[1] * k);
    ImVec2 o(floorf(p0.x + (size.x - pic.x) / 2), floorf(p0.y + (size.y - pic.y) / 2)), end(o.x + pic.x, o.y + pic.y);
    ImDrawList *draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(o, end, true);
    draw->AddRectFilled(o, end, IM_COL32_BLACK);
    std::vector<PreviewHit> hits;
    int tick = g.tick;
    for (int layer = 0; layer <= 2; layer++) {
        if (sc.clear[0] >= 0 && sc.clearLayer == layer)
            draw->AddRectFilled(o, end, IM_COL32(sc.clear[0], sc.clear[1], sc.clear[2], 255));
        DrawBgs(h, draw, sc, layer, tick, o, k);
        for (const StoryLayer &l : sc.layers) {
            if (l.layerno != layer || !l.Visible(tick)) continue;
            PreviewHit hit;
            DrawLayer(h, draw, sc, l, tick, o, k, hit);
            if (hit.hi.x >= hit.lo.x) hits.push_back(hit);
        }
    }
    int col[3] = {0, 0, 0};
    if (float a = Story_FadeAlpha(sc, tick, col); a > 0)
        draw->AddRectFilled(o, end, IM_COL32(col[0], col[1], col[2], (int)(a * 255)));
    // The layer under the mouse: the last drawn, the one in front
    ImVec2 m = ImGui::GetMousePos();
    const PreviewHit *over = nullptr;
    if (hovered && g.moveKey.empty())
        for (const PreviewHit &hit : hits)
            if (m.x >= hit.lo.x && m.x < hit.hi.x && m.y >= hit.lo.y && m.y < hit.hi.y) over = &hit;
    std::string sceneSection = h.ini->sections[sc.section].lower;
    for (const PreviewHit &hit : hits) {
        bool sel = h.section == sceneSection && h.prefix == hit.prefix, hot = over == &hit;
        if (sel || hot) draw->AddRect(hit.lo, hit.hi, sel ? kPlayhead : IM_COL32(255, 255, 255, 230));
    }
    draw->PopClipRect();
    if (over) {
        ImGui::SetTooltip("%s\nDrag: move it with the others (layerall.pos); Alt+drag: it alone (%soffset)",
                          over->prefix.substr(0, over->prefix.size() - 1).c_str(), over->prefix.c_str());
        if (pressed) {
            h.select(sceneSection, over->prefix);
            bool alone = ImGui::GetIO().KeyAlt;
            g.moveKey = alone ? over->prefix + "offset" : "layerall.pos";
            g.moveFrom = m, g.moveScene = g.scene;
            for (const StoryLayer &l : sc.layers)
                if (l.key + "." == over->prefix) {
                    g.moveValue[0] = alone ? l.offset[0] : sc.layerall[0];
                    g.moveValue[1] = alone ? l.offset[1] : sc.layerall[1];
                }
        }
    }
    if (!g.moveKey.empty()) {
        float v[2] = {g.moveValue[0] + std::round((m.x - g.moveFrom.x) / k), g.moveValue[1] + std::round((m.y - g.moveFrom.y) / k)};
        ImGui::SetTooltip("%s = %.0f, %.0f", g.moveKey.c_str(), v[0], v[1]);
        if (m.x != g.moveFrom.x || m.y != g.moveFrom.y) SetField(h, g.moveScene, g.moveKey, Ini_Floats(v, 2));
        if (!active) g.moveKey.clear();
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// The timelines
// ---------------------------------------------------------------------------

// The strip of scenes, each as wide as its time, in the order of the def. Its menu adds, copies, moves and deletes them.
void SceneStrip(StoryHost &h) {
    const std::vector<StoryScene> &scenes = g.doc.scenes;
    float em = ImGui::GetFontSize(), width = ImGui::GetContentRegionAvail().x, high = ImGui::GetFrameHeight();
    float minimum = em * 1.6f, gap = 2, total = 0;
    for (const StoryScene &s : scenes) total += Story_Length(s);
    float k = total > 0 ? std::max(width - (minimum + gap) * scenes.size(), 0.0f) / total : 0;
    std::vector<int> order = Story_Order(g.doc);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float x = p.x;
    ImDrawList *draw = ImGui::GetWindowDrawList();
    for (int i = 0; i < (int)scenes.size(); i++) {
        const StoryScene &s = scenes[i];
        float w = minimum + Story_Length(s) * k;
        ImGui::PushID(i);
        ImGui::SetCursorScreenPos(ImVec2(x, p.y));
        if (ImGui::InvisibleButton("scene", ImVec2(w, high))) SelectScene(h, i);
        bool plays = std::find(order.begin(), order.end(), i) != order.end(), hot = ImGui::IsItemHovered();
        ImU32 fill = i == g.scene ? IM_COL32(70, 110, 170, 255) : hot ? IM_COL32(70, 76, 92, 255) : IM_COL32(52, 56, 68, 255);
        draw->AddRectFilled(ImVec2(x, p.y), ImVec2(x + w, p.y + high), plays ? fill : IM_COL32(36, 36, 40, 255), 3);
        std::string label = s.name.compare(0, 6, "Scene ") == 0 ? s.name.substr(6) : s.name;
        draw->PushClipRect(ImVec2(x, p.y), ImVec2(x + w, p.y + high), true);
        draw->AddText(ImVec2(x + 5, p.y + ImGui::GetStyle().FramePadding.y), plays ? IM_COL32_WHITE : IM_COL32(150, 150, 150, 255), label.c_str());
        draw->PopClipRect();
        if (i == g.scene) {
            float at = x + std::min(1.0f, (float)g.tick / Story_Length(s)) * w;
            draw->AddLine(ImVec2(at, p.y), ImVec2(at, p.y + high), kPlayhead, 2);
        }
        if (hot)
            ImGui::SetTooltip("[%s]  %d ticks%s%s", s.name.c_str(), Story_Length(s), s.jump >= 0 ? ", then jumps" : "",
                              plays ? "" : "\nDoes not play: a jump goes past it, or startscene is after it");
        if (ImGui::BeginPopupContextItem("scene menu")) {
            std::string &text = h.sf->text;
            if (ImGui::MenuItem(ICON_PLUS " New scene after it")) g.changed = Story_AddScene(text, *h.ini, g.doc, i);
            if (ImGui::MenuItem(ICON_COPY " Duplicate")) g.changed = Story_DuplicateScene(text, *h.ini, g.doc, i);
            if (ImGui::MenuItem("Earlier", nullptr, false, i > 0)) g.changed = Story_MoveScene(text, *h.ini, g.doc, i, -1);
            if (ImGui::MenuItem("Later", nullptr, false, i + 1 < (int)scenes.size())) g.changed = Story_MoveScene(text, *h.ini, g.doc, i, 1);
            ImGui::SetItemTooltip("An order changed: startscene and jump count scenes, so check them");
            if (ImGui::MenuItem(ICON_TRASH " Delete", nullptr, false, scenes.size() > 1)) g.changed = Story_DeleteScene(text, *h.ini, g.doc, i);
            ImGui::EndPopup();
        }
        ImGui::PopID();
        x += w + gap;
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + high + ImGui::GetStyle().ItemSpacing.y));
    ImGui::Dummy(ImVec2(0, 0));
}

enum { kPartNone, kPartStart, kPartEnd, kPartBoth };

// A row's bar from start to end, its body and edges dragged. drawnEnd: where it ends on the row. moment: it has
// no length (a sound), so all of it moves it and its label goes on past it. True when a drag moved it.
bool RowBar(float x0, float y, float w, float high, int span, int &start, int &end, int drawnEnd, ImU32 col, const std::string &label, bool selected,
            bool *clicked, bool moment = false) {
    auto x = [&](int t) { return x0 + w * std::min(std::max(t, 0), span) / span; };
    float a = x(start), b = std::max(x(drawnEnd), a + 3), grip = ImGui::GetFontSize() * 0.4f;
    ImGui::SetCursorScreenPos(ImVec2(x0, y));
    ImGui::InvisibleButton("bar", ImVec2(std::max(w, 1.0f), high));
    bool hot = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    float mouse = ImGui::GetIO().MousePos.x;
    int over = kPartNone;
    if (fabsf(mouse - b) < grip) over = kPartEnd;
    else if (fabsf(mouse - a) < grip) over = kPartStart;
    else if (mouse > a && mouse < b) over = kPartBoth;
    if (moment && over != kPartNone) over = kPartBoth;
    if (ImGui::IsItemActivated()) g.dragPart = over, g.dragA = start, g.dragB = drawnEnd, g.dragSpan = span;
    bool changed = false;
    if (active && g.dragPart != kPartNone && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2)) {
        int d = (int)std::lround(ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 2).x * span / w);
        int ns = start, ne = end;
        if (g.dragPart == kPartStart) ns = std::clamp(g.dragA + d, 0, g.dragB - 1);
        if (g.dragPart == kPartEnd) ne = std::max(g.dragB + d, g.dragA + 1);
        if (g.dragPart == kPartBoth) {
            ns = std::max(g.dragA + d, 0);
            if (end > 0) ne = ns + (g.dragB - g.dragA); // one that lasts until its scene ends stays so
        }
        changed = ns != start || ne != end;
        start = ns, end = ne;
    }
    if (clicked) *clicked = ImGui::IsItemClicked();
    if (hot || active) {
        int part = active ? g.dragPart : over;
        if (part == kPartStart || part == kPartEnd) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (part == kPartBoth) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }
    ImDrawList *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + w, y + high - 2), IM_COL32(40, 40, 46, 255));
    draw->AddRectFilled(ImVec2(a, y), ImVec2(b, y + high - 2), col, 2);
    if (selected) draw->AddRect(ImVec2(a, y), ImVec2(b, y + high - 2), kPlayhead, 2);
    draw->PushClipRect(ImVec2(a, y), ImVec2(moment ? x0 + w : b, y + high), true);
    draw->AddText(ImVec2((moment ? b : a) + 4, y), IM_COL32_WHITE, label.c_str());
    draw->PopClipRect();
    return changed;
}

// The scene selected: its ruler with the playhead, its fades and end, a row per layer and sound.
void SceneTimeline(StoryHost &h, const StoryScene &sc) {
    float em = ImGui::GetFontSize(), rowH = ImGui::GetTextLineHeightWithSpacing();
    std::string section = h.ini->sections[sc.section].lower;
    int length = Story_Length(sc), last = length;
    for (const StoryLayer &l : sc.layers) last = std::max(last, std::max(l.end, l.start + 1));
    for (const StorySound &s : sc.sounds) last = std::max(last, s.start + 1);
    int span = (last + 1) * 6 / 5;
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && g.dragSpan > 1 && g.dragPart != kPartNone) span = g.dragSpan;
    float x0 = ImGui::GetCursorScreenPos().x, w = ImGui::GetContentRegionAvail().x;
    auto X = [&](int t) { return x0 + w * std::min(std::max(t, 0), span) / span; };
    ImDrawList *draw = ImGui::GetWindowDrawList();
    float y = ImGui::GetCursorScreenPos().y;

    // The ruler: click and drag move the playhead; the marks below it are the fades and the end
    ImGui::InvisibleButton("ruler", ImVec2(std::max(w, 1.0f), rowH * 1.2f));
    bool rulerHot = ImGui::IsItemHovered();
    float rulerH = rowH * 1.2f;
    draw->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + w, y + rulerH), IM_COL32(32, 32, 38, 255));
    draw->AddRectFilled(ImVec2(X(length), y), ImVec2(x0 + w, y + rulerH), IM_COL32(22, 22, 25, 255));
    int step = 1;
    for (int s : {5, 10, 30, 60, 120, 300, 600, 1200, 3000})
        if (w * s / span >= em * 3) {
            step = s;
            break;
        }
    if (step > 1)
        for (int t = 0; t <= span; t += step) {
            draw->AddLine(ImVec2(X(t), y + rulerH * 0.55f), ImVec2(X(t), y + rulerH), IM_COL32(120, 120, 130, 255));
            char text[16];
            snprintf(text, sizeof(text), "%d", t);
            draw->AddText(ImVec2(X(t) + 3, y), IM_COL32(150, 150, 160, 255), text);
        }
    int mouseTick = (int)std::lround((ImGui::GetIO().MousePos.x - x0) * span / w);
    if (ImGui::IsItemActivated()) g.scrubbing = true;
    if (g.scrubbing) {
        int t = std::clamp(mouseTick, 0, length - 1);
        if (t != g.tick) Seek(h, g.scene, t);
        if (!ImGui::IsItemActive()) g.scrubbing = false;
    }
    if (rulerHot) ImGui::SetTooltip("Tick %d: click or drag to go there", std::clamp(mouseTick, 0, length - 1));
    y += rulerH + 1;

    // Fades and the end, on a row of their own
    ImGui::PushID("times");
    {
        int in = sc.in.time, out = sc.out.time, end = length;
        ImGui::SetCursorScreenPos(ImVec2(x0, y));
        ImGui::InvisibleButton("row", ImVec2(std::max(w, 1.0f), rowH));
        bool hot = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
        float mouse = ImGui::GetIO().MousePos.x, grip = em * 0.5f;
        enum { kNone, kIn, kOut, kEnd };
        int over = kNone;
        if (fabsf(mouse - X(end)) < grip) over = kEnd;
        else if (fabsf(mouse - X(in)) < grip) over = kIn;
        else if (fabsf(mouse - X(length - out)) < grip) over = kOut;
        if (ImGui::IsItemActivated()) g.dragPart = over ? over + 10 : kPartNone, g.dragA = in, g.dragB = over == kEnd ? end : out, g.dragSpan = span;
        if (active && g.dragPart > 10 && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2)) {
            int d = (int)std::lround(ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 2).x * span / w);
            if (g.dragPart == 10 + kIn && std::max(g.dragA + d, 0) != in) SetTime(h, g.scene, "fadein.time", std::max(g.dragA + d, 0));
            if (g.dragPart == 10 + kOut && std::clamp(g.dragB - d, 0, length) != out) SetTime(h, g.scene, "fadeout.time", std::clamp(g.dragB - d, 0, length));
            if (g.dragPart == 10 + kEnd && std::max(g.dragB + d, 1) != end) SetField(h, g.scene, "end.time", std::to_string(std::max(g.dragB + d, 1)));
        }
        draw->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + w, y + rowH - 2), IM_COL32(32, 32, 38, 255));
        ImU32 fade = IM_COL32(150, 110, 200, 200);
        if (in > 0) draw->AddTriangleFilled(ImVec2(X(0), y), ImVec2(X(0), y + rowH - 2), ImVec2(X(in), y + rowH - 2), fade);
        if (out > 0) draw->AddTriangleFilled(ImVec2(X(length), y), ImVec2(X(length), y + rowH - 2), ImVec2(X(length - out), y + rowH - 2), fade);
        draw->AddText(ImVec2(x0 + w / 2 - em * 2, y), IM_COL32(130, 130, 140, 255), "fades, end");
        draw->AddLine(ImVec2(X(end), y - rulerH), ImVec2(X(end), y + rowH), IM_COL32(230, 90, 80, 255), 2);
        int part = active ? g.dragPart - 10 : over;
        if ((hot || active) && part > 0) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (hot || (active && g.dragPart > 10))
            ImGui::SetTooltip("fadein.time %d, fadeout.time %d, end.time %d\nDrag the slopes' edges and the red line", in, out, sc.end);
    }
    ImGui::PopID();
    y += rowH;

    for (const StoryLayer &l : sc.layers) {
        ImGui::PushID(l.key.c_str());
        std::string prefix = l.key + ".";
        std::string label = l.key + (l.anim >= 0 ? "  anim " + std::to_string(l.anim) : !l.text.empty() ? "  \"" + l.text.substr(0, l.text.find('\n')) + "\"" : l.spr[0] >= 0 ? "  sprite" : "  (empty)");
        if (!l.text.empty() && l.font[0] < 0) label += "  (no font: not drawn)";
        int start = l.start, end = l.end;
        int drawn = l.end > 0 ? l.end : length;
        bool clicked = false;
        bool moved = RowBar(x0, y, w, rowH, span, start, end, drawn, l.text.empty() ? IM_COL32(70, 100, 150, 255) : IM_COL32(70, 130, 110, 255), label,
                            h.section == section && h.prefix == prefix, &clicked);
        if (moved) {
            if (start != l.start) SetTime(h, g.scene, prefix + "starttime", start);
            if (end != l.end) SetTime(h, g.scene, prefix + "endtime", end);
        }
        if (clicked) h.select(section, prefix);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s: from %d to %s\nDrag it to move it, its edges for its start and end; right click for its menu", l.key.c_str(), l.start,
                              l.end > 0 ? std::to_string(l.end).c_str() : "the scene's end");
        if (ImGui::BeginPopupContextItem("layer menu")) {
            if (l.anim >= 0 && ImGui::MenuItem(ICON_CLAPPER " Open its action in Animation")) AnimOpenFile(h.sf->path, l.anim);
            if (l.end > 0 && ImGui::MenuItem("Until the scene ends")) SetTime(h, g.scene, prefix + "endtime", 0);
            if (ImGui::MenuItem(ICON_TRASH " Delete this layer") && !g.changed)
                g.changed = Story_RemoveKeys(h.sf->text, h.ini->sections[sc.section], prefix);
            ImGui::EndPopup();
        }
        ImGui::PopID();
        y += rowH;
    }
    for (const StorySound &s : sc.sounds) {
        ImGui::PushID(s.key.c_str());
        std::string prefix = s.key + ".";
        int start = s.start, end = 0;
        bool clicked = false;
        std::string label = s.key + "  " + std::to_string(s.value[0]) + "," + std::to_string(s.value[1]);
        bool moved = RowBar(x0, y, w, rowH, span, start, end, s.start + std::max(span / 80, 1), IM_COL32(170, 120, 60, 255), label,
                            h.section == section && h.prefix == prefix, &clicked, true);
        if (moved && start != s.start) SetTime(h, g.scene, prefix + "starttime", start);
        if (clicked) h.select(section, prefix);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s: at tick %d\nDrag it to move it; right click for its menu", s.key.c_str(), s.start);
        if (ImGui::BeginPopupContextItem("sound menu")) {
            if (ImGui::MenuItem(ICON_TRASH " Delete this sound") && !g.changed)
                g.changed = Story_RemoveKeys(h.sf->text, h.ini->sections[sc.section], prefix);
            ImGui::EndPopup();
        }
        ImGui::PopID();
        y += rowH;
    }
    // The playhead over them all
    float head = X(g.tick);
    float rows = rulerH + 1 + rowH * (1 + sc.layers.size() + sc.sounds.size());
    draw->AddLine(ImVec2(head, y - rows), ImVec2(head, y), kPlayhead, 2);
    ImGui::SetCursorScreenPos(ImVec2(x0, y + 2));
    ImGui::Dummy(ImVec2(0, 0));
}

// Where the preview is, each frame it plays.
void RunPreview(StoryHost &h) {
    if (!g.running || h.playing) return;
    int tick = g.tick0 + (int)((ImGui::GetTime() - g.runFrom) * 60);
    int scene = g.scene;
    while (scene >= 0 && tick >= Story_Length(g.doc.scenes[scene])) {
        tick -= Story_Length(g.doc.scenes[scene]);
        scene = Story_Next(g.doc, scene);
    }
    if (scene < 0) {
        g.running = false;
        return;
    }
    if (scene != g.scene) {
        g.scene = scene, g.tick = tick;
        g.tick0 = tick, g.runFrom = ImGui::GetTime();
    }
    g.tick = tick;
}

} // namespace

void StoryPicker() {
    float em = ImGui::GetFontSize();
    const std::string &opened = StoryOpened();
    ImGui::SetNextItemWidth(em * 16);
    std::string shown = opened.empty() ? "Pick a storyboard" : opened;
    if (ImGui::BeginCombo("##storyboards", shown.c_str())) {
        static std::vector<std::pair<std::string, std::string>> files;
        if (ImGui::IsWindowAppearing()) files = StoryboardFiles();
        for (const auto &f : files) {
            std::string label = f.first + "  " + f.second;
            if (ImGui::Selectable(label.c_str(), NormPath(f.second) == NormPath(opened))) {
                StoryOpen(f.second);
                ShowWindow("Scenes");
            }
        }
        if (files.empty()) ImGui::TextDisabled("The motif and the characters name none");
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("The storyboards the motif and the characters name; any other: Open in Storyboard, in Explorer's menu");
}

bool StoryPanel(StoryHost &h) {
    g.changed = false;
    if (g.text != h.sf->text || g.path != h.sf->path) {
        if (g.path != h.sf->path) g.scene = -1, g.tick = 0, g.running = false;
        g.text = h.sf->text, g.path = h.sf->path;
        Story_Read(*h.ini, g.doc);
    }
    if (g.doc.scenes.empty()) {
        ImGui::TextDisabled("This def has no scenes.");
        if (ImGui::SmallButton(ICON_PLUS " New scene")) g.changed = Story_AddScene(h.sf->text, *h.ini, g.doc, -1);
        return g.changed;
    }
    if (h.playing && !g.scrubbing) {
        if (g.seekOnStart) {
            g.seekOnStart = false;
            Seek(h, g.scene, g.tick);
            g.changed = h.sf->Dirty(); // it read the def from disk: the text here goes to it
        } else {
            g.scene = h.scene, g.tick = h.tick;
        }
        g.running = false;
    }
    if (g.scene < 0) {
        g.scene = std::clamp(g.doc.startScene, 0, (int)g.doc.scenes.size() - 1);
        g.tick = ShownFrom(g.doc.scenes[g.scene]);
    }
    g.scene = std::clamp(g.scene, 0, (int)g.doc.scenes.size() - 1);
    RunPreview(h);
    const StoryScene &sc = g.doc.scenes[g.scene];
    g.tick = std::clamp(g.tick, 0, Story_Length(sc) - 1);

    if (!h.playing) {
        Preview(h, sc);
        ImGui::SameLine();
    }
    ImGui::BeginGroup();
    // The transport: this preview, or the game when it plays the def
    bool running = h.playing ? !h.paused : g.running;
    if (ImGui::Button(running ? ICON_PAUSE "##run" : ICON_PLAY "##run")) {
        if (h.playing) {
            SendRaw(running ? "ikdbg.holdNow('file:" + h.sf->path + "')" : "ikdbg.letGo()");
        } else {
            g.running = !g.running;
            g.tick0 = g.tick, g.runFrom = ImGui::GetTime();
        }
    }
    ImGui::SetItemTooltip(h.playing ? "The game goes on or stops" : "Plays here, scene after scene, jumps taken");
    ImGui::SameLine();
    ImGui::Text("%s  %d / %d", sc.name.c_str(), g.tick, Story_Length(sc));
    ImGui::SameLine();
    if (!h.playing) {
        if (ImGui::SmallButton(ICON_TRACE " Play in the game")) {
            g.seekOnStart = true;
            GoToStory(h.sf->path);
        }
        ImGui::SetItemTooltip("The game plays this storyboard, stopped at this scene and time. Edits show there at once.");
    } else {
        ImGui::TextColored(Ink(kGreen), "in the game");
    }
    SceneStrip(h);
    // New rows
    if (ImGui::SmallButton(ICON_PLUS " Animation")) {
        std::string key = Story_NewLayerKey(sc);
        SetField(h, g.scene, key + ".anim", "0");
    }
    ImGui::SetItemTooltip("A new layer showing action 0; its field picks another");
    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_PLUS " Text") && !g.changed) g.changed = Story_AddText(h.sf->text, *h.ini, g.doc, g.scene);
    ImGui::SetItemTooltip("A new layer of text, in the middle of the picture, with the first font of [SceneDef] (it gets one if it has none); black on a light scene");
    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_PLUS " Sound")) {
        int n = 0;
        for (const StorySound &s : sc.sounds) n = std::max(n, atoi(s.key.c_str() + 5) + 1);
        SetField(h, g.scene, "sound" + std::to_string(n) + ".value", "0, 0");
    }
    // As the Storyboard window's: the drags are made here
    SameLineFits(UndoButtonsWidth());
    if (int step = UndoButtons(h.history->CanUndo(), h.history->CanRedo())) {
        h.sf->text = step < 0 ? h.history->Undo() : h.history->Redo();
        g.changed = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!h.sf->Dirty());
    bool save = ImGui::Button(ICON_SAVE "##save");
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Writes the def (Ctrl+S)");
    ImGuiIO &io = ImGui::GetIO();
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) save = h.sf->Dirty();
    if (save) SaveSource(*h.sf);
    if (ImGui::BeginChild("rows", ImVec2(0, 0), 0, ImGuiWindowFlags_NoNavInputs)) SceneTimeline(h, sc);
    ImGui::EndChild();
    ImGui::EndGroup();
    return g.changed;
}

} // namespace app
