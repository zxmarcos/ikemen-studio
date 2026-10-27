// Animation: the selected player's AIR element, its boxes edited here and on the game; or the
// action a screen's element shows (fight screen, motif, storyboard: [Begin Action] in their def).
#include "app_internal.h"
#include "imgui_internal.h"

#include <cfloat>
#include <cmath>
#include <fstream>

namespace app {

bool g_boxEdit = true;    // the selected player's boxes can be dragged on the Game window
bool g_followSel = true;  // a zoomed Game window keeps the selected player in view
int g_animDrag = 0;       // what a drag on the selected player changes: 0 its boxes, 1 the sprite's offset
bool g_onion = true;      // the previous element's boxes are drawn too, faint
bool g_onionSprite = false; // and its sprite
bool g_animFollow = true; // the Animation window shows what the selected player shows
int g_animAction = 0, g_animElem = 1;

std::map<int, std::pair<std::string, double>> g_swapWanted;

void OnFiles(const Json &j) {
    for (const auto &p : j["players"].items) {
        int pn = p["pn"].i();
        g_palNo[pn] = p["palno"].i();
        auto &files = g_charFiles[pn];
        files.clear();
        for (const auto &kv : p["files"].fields)
            for (const auto &f : kv.second.items) files[kv.first].push_back(f.str());
        // Still the character before a swap (asked for before the match started again): asked again
        auto want = g_swapWanted.find(pn);
        if (want == g_swapWanted.end()) continue;
        const std::string &name = want->second.first;
        std::string def = files.count("def") && !files["def"].empty() ? NormPath(files["def"][0]) : "";
        std::string expected = NormPath("chars/" + (name.find('/') == std::string::npos ? name + "/" + name + ".def" : name));
        if (def == expected || ImGui::GetTime() - want->second.second > 15) g_swapWanted.erase(want);
        else g_charFiles.erase(pn);
    }
}

// The AIR file of a player's animations ("" until the game has told).
std::string AirPath(int pn) {
    const auto *files = FilesFor(pn);
    if (!files) {
        if (pn != kDiskPn && Connected() && g_canTrace && ImGui::GetTime() - g_filesAsked[pn] > 2.0) {
            g_filesAsked[pn] = ImGui::GetTime();
            SendRaw("ikdbg.files()");
        }
        return "";
    }
    auto f = files->find("anim");
    return f == files->end() || f->second.empty() ? "" : f->second[0];
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

AnimTarget GetAnimTarget() {
    AnimTarget t;
    if (WorkPn() == kDiskPn) {
        // A character picked in Explorer, not in the match: its AIR file from disk
        t.pn = kDiskPn;
    } else {
        t.e = FindEntity(g_selId);
        t.fe = FindFrameEntity(g_selId);
        t.pn = t.fe && t.fe->action >= 0 ? t.fe->animPn : t.e ? t.e->pn : 0;
    }
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

// A box added to the element's Clsn1 (g 0) or Clsn2 (g 1): at, or under the last one.
void AddBox(const AnimTarget &t, int g, const AirBox *at = nullptr) {
    const AirElem &el = t.act->elems[t.elem - 1];
    std::vector<AirBox> boxes = el.clsn[g];
    AirBox b;
    if (at) {
        b = *at;
    } else if (boxes.empty()) {
        b.x1 = -15, b.y1 = -80, b.x2 = 15, b.y2 = 0;
    } else {
        b = boxes.back();
        b.y1 -= 10, b.y2 -= 10;
    }
    boxes.push_back(b);
    if (Air_SetOwnBoxes(t.sf->text, el, g, boxes)) ApplyAnims(*t.sf);
}

void DeleteBox(const AnimTarget &t, int g, int index) {
    const AirElem &el = t.act->elems[t.elem - 1];
    std::vector<AirBox> boxes = el.clsn[g];
    boxes.erase(boxes.begin() + index);
    if (Air_SetOwnBoxes(t.sf->text, el, g, boxes)) ApplyAnims(*t.sf);
}

bool BoxHandles(BoxDrag &d, ImDrawList *draw, bool hovered, const AnimTarget &t,
                const std::function<ImVec2(float, float)> &airToScreen, const std::function<ImVec2(ImVec2)> &screenToAir,
                bool flip, int *hit) {
    const AirElem &el = t.act->elems[t.elem - 1];
    // The box as dragged so far (AIR units, x1 <= x2, y1 <= y2)
    auto dragged = [&](ImVec2 m) {
        AirBox b = d.box;
        ImVec2 a = screenToAir(m);
        int dx = (int)lroundf(a.x - d.start.x), dy = (int)lroundf(a.y - d.start.y);
        int e = d.edges;
        if (e == 15) {
            b.x1 += dx, b.x2 += dx, b.y1 += dy, b.y2 += dy;
        } else {
            // facing left: the screen's left edge is the box's x2
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
    float thick = std::max(1.0f, ImGui::GetFontSize() / 13);

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
    if (hit) *hit = hovered && hitG >= 0 ? hitG * 1000 + hitI : -1;

    if (d.active && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        // Released: write the box and send the animations to the game
        AirBox b = dragged(m);
        const AirBox &old = d.box;
        d.active = false;
        if ((b.x1 != old.x1 || b.y1 != old.y1 || b.x2 != old.x2 || b.y2 != old.y2) && d.g < 2 &&
            d.i < (int)el.clsn[d.g].size() && Air_SetBox(t.sf->text, el.clsn[d.g][d.i], b.x1, b.y1, b.x2, b.y2))
            ApplyAnims(*t.sf);
        return true;
    }
    if (!d.active && hovered && hitG >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        d.active = true;
        d.g = hitG, d.i = hitI, d.edges = hitEdges;
        d.start = screenToAir(m);
        d.box = el.clsn[hitG][hitI];
    }

    int cursorEdges = d.active ? d.edges : hovered ? hitEdges : 0;
    if (cursorEdges == 15)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    else if ((cursorEdges & 3) && (cursorEdges & 12))
        ImGui::SetMouseCursor(((cursorEdges & 1) != 0) == ((cursorEdges & 4) != 0) ? ImGuiMouseCursor_ResizeNWSE
                                                                                    : ImGuiMouseCursor_ResizeNESW);
    else if (cursorEdges & 3)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    else if (cursorEdges & 12)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

    for (int g = 0; g < 2; g++)
        for (size_t i = 0; i < el.clsn[g].size(); i++) {
            bool dragging = d.active && d.g == g && d.i == (int)i;
            bool hot = dragging || (!d.active && hovered && hitG == g && hitI == (int)i);
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
    return d.active || (hovered && hitG >= 0);
}

// Number of elements of the action whose boxes come from that header line.
int SharedBy(const AirAction &act, int g, int header) {
    int n = 0;
    for (const auto &e : act.elems) n += e.header[g] == header;
    return n;
}

// The action drawn here, without the game: each element's sprite at its offset from the axis,
// its Clsn2 (blue) and Clsn1 (red) boxes; played at the game's 60 ticks a second, looping from
// its loop start, or the element selected. The selected element's boxes are edited on it: a drag
// moves an edge, a corner or the whole box; the right button adds or deletes one. height: of the
// canvas, 0 for one the user sizes. Returns true when the element's lines changed (a box added or
// deleted).
bool AnimPreview(const AnimTarget &t, SpriteFile *spf, bool boxes, float height) {
    static bool playing = false;
    static double started;
    static const AirAction *forAct;
    static BoxDrag drag;
    // The view, fitted to the action when it or the canvas changes (or on a double click), and
    // kept while its boxes are edited; the wheel zooms at the mouse, a middle drag pans
    static std::string viewOf;
    static bool refit;
    static float k;
    static ImVec2 origin; // the axis, from the canvas's corner
    float em = ImGui::GetFontSize();
    // Its toolbar: play, the previous element, the zoom
    if (ImGui::Button(playing ? "Stop" : "Play", ImVec2(em * 3.5f, 0))) {
        playing = !playing;
        started = ImGui::GetTime();
    }
    ImGui::SetItemTooltip("Plays the action at the game's speed (60 ticks a second), from its loop start after the end");
    ImGui::SameLine();
    ImGui::TextDisabled("Previous:");
    ImGui::SameLine();
    ImGui::Checkbox("boxes##onion", &g_onion);
    ImGui::SameLine();
    ImGui::Checkbox("sprite##onion", &g_onionSprite);
    ImGui::SetItemTooltip("The previous element drawn faint, here and on the game");
    {
        char zoom[16];
        snprintf(zoom, sizeof(zoom), "%.0f%%", k * 100);
        float w = ImGui::CalcTextSize(zoom).x + ImGui::CalcTextSize("Fit").x + ImGui::GetStyle().FramePadding.x * 2 +
                  ImGui::GetStyle().ItemSpacing.x * 2 + ImGui::CalcTextSize("(?)").x;
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < w)
            ImGui::NewLine();
        else
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - w);
        ImGui::TextDisabled("%s", zoom);
        ImGui::SameLine();
        if (ImGui::SmallButton("Fit")) refit = true;
        ImGui::SetItemTooltip("The whole action in view (double click on the preview does it too)");
        HelpMarker(boxes ? "Drag an edge, a corner or the inside of a box to change it. Right click: add a Clsn2 or "
                           "Clsn1 box there, or delete the box under the mouse (Delete does it too). Changes go to "
                           "the game at once; Save writes the file. Wheel: zoom; middle drag: pan; double click: fit."
                         : "Wheel: zoom; middle drag: pan; double click: fit.");
    }
    const AirAction &act = *t.act;
    // The element shown: the one selected, or the one playing
    int shown = t.elem - 1;
    if (playing) {
        if (forAct != &act) started = ImGui::GetTime();
        long tick = (long)((ImGui::GetTime() - started) * 60), at = 0;
        int loop = act.loopStart >= 0 ? act.loopStart : 0;
        long total = 0, loopTotal = 0;
        bool endless = false;
        for (size_t i = 0; i < act.elems.size(); i++) {
            if (act.elems[i].time < 0) endless = true;
            total += std::max(1, act.elems[i].time);
            if ((int)i >= loop) loopTotal += std::max(1, act.elems[i].time);
        }
        if (!endless && tick >= total && loopTotal > 0) tick = total - loopTotal + (tick - total) % loopTotal;
        for (shown = 0; shown < (int)act.elems.size(); shown++) {
            const AirElem &e = act.elems[shown];
            if (e.time < 0 || tick < at + std::max(1, e.time)) break;
            at += std::max(1, e.time);
        }
        shown = std::min(shown, (int)act.elems.size() - 1);
    }
    forAct = &act;
    // The box of every element (sprites and boxes, from the axis), to fit them all
    float minX = -10, minY = -10, maxX = 10, maxY = 10;
    auto grow = [&](float x1, float y1, float x2, float y2) {
        minX = std::min(minX, std::min(x1, x2)), maxX = std::max(maxX, std::max(x1, x2));
        minY = std::min(minY, std::min(y1, y2)), maxY = std::max(maxY, std::max(y1, y2));
    };
    for (const AirElem &e : act.elems) {
        int i = spf ? Sff_FindSprite(spf->doc, e.group, e.image) : -1;
        if (i >= 0) {
            const SffSprite &s = spf->doc.sprites[i];
            grow((float)(e.x - s.x), (float)(e.y - s.y), (float)(e.x - s.x + s.w), (float)(e.y - s.y + s.h));
        }
        if (boxes)
            for (const auto &g : e.clsn)
                for (const AirBox &b : g) grow((float)b.x1, (float)b.y1, (float)b.x2, (float)b.y2);
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    bool open = ImGui::BeginChild("preview", ImVec2(0, height > 0 ? height : em * 14),
                                  ImGuiChildFlags_Borders | (height > 0 ? 0 : ImGuiChildFlags_ResizeY), ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    if (!open) {
        ImGui::EndChild();
        return false;
    }
    ImVec2 p0 = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("canvas", ImVec2(std::max(size.x, 1.0f), std::max(size.y, 1.0f)),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    bool hovered = ImGui::IsItemHovered();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    Checkerboard(draw, p0, ImVec2(p0.x + size.x, p0.y + size.y));
    char key[64];
    snprintf(key, sizeof(key), "#%d %.0fx%.0f ", g_animAction, size.x, size.y);
    std::string view = key + (t.sf ? t.sf->path : std::string());
    if (viewOf != view || refit) {
        viewOf = view, refit = false;
        float pad = em;
        k = std::min((size.x - pad * 2) / (maxX - minX), (size.y - pad * 2) / (maxY - minY));
        k = k >= 1 ? floorf(k) : std::max(k, 0.1f); // whole pixels when it can
        origin = ImVec2((size.x - (maxX - minX) * k) * 0.5f - minX * k, (size.y - (maxY - minY) * k) * 0.5f - minY * k);
    }
    ImGuiIO &io = ImGui::GetIO();
    if (hovered && io.MouseWheel != 0) {
        float before = k;
        k = std::max(0.1f, std::min(32.0f, k * powf(1.25f, io.MouseWheel)));
        ImVec2 m(io.MousePos.x - p0.x, io.MousePos.y - p0.y);
        origin = ImVec2(m.x - (m.x - origin.x) * k / before, m.y - (m.y - origin.y) * k / before);
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0)) {
        origin.x += io.MouseDelta.x, origin.y += io.MouseDelta.y;
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    ImVec2 axis(floorf(p0.x + origin.x), floorf(p0.y + origin.y));
    auto at = [&](float x, float y) { return ImVec2(floorf(axis.x + x * k), floorf(axis.y + y * k)); };
    const AirElem &el = act.elems[std::clamp(shown, 0, (int)act.elems.size() - 1)];
    // The previous element, faint (the last one's before the first)
    if (!playing && act.elems.size() > 1) {
        const AirElem &prev = act.elems[shown >= 1 ? shown - 1 : act.elems.size() - 1];
        int pi = g_onionSprite && spf ? Sff_FindSprite(spf->doc, prev.group, prev.image) : -1;
        if (GLuint tex = pi >= 0 ? SpriteTexture(*spf, pi) : 0) {
            const SffSprite &s = spf->doc.sprites[pi];
            draw->AddImage(TexRef(tex), at((float)(prev.x - s.x), (float)(prev.y - s.y)),
                           at((float)(prev.x - s.x + s.w), (float)(prev.y - s.y + s.h)), ImVec2(0, 0), ImVec2(1, 1),
                           IM_COL32(255, 255, 255, 90));
        }
        if (boxes && g_onion)
            for (int g = 0; g < 2; g++)
                for (const AirBox &b : prev.clsn[g])
                    draw->AddRect(at((float)b.x1, (float)b.y1), at((float)b.x2, (float)b.y2),
                                  g == 0 ? IM_COL32(255, 120, 120, 90) : IM_COL32(140, 180, 255, 90));
    }
    int si = spf ? Sff_FindSprite(spf->doc, el.group, el.image) : -1;
    if (si >= 0) {
        const SffSprite &s = spf->doc.sprites[si];
        if (GLuint tex = SpriteTexture(*spf, si))
            draw->AddImage(TexRef(tex), at((float)(el.x - s.x), (float)(el.y - s.y)),
                           at((float)(el.x - s.x + s.w), (float)(el.y - s.y + s.h)));
    } else if (el.group >= 0) {
        char note[64];
        snprintf(note, sizeof(note), "sprite %d, %d is not in the SFF", el.group, el.image);
        draw->AddText(ImVec2(p0.x + em * 0.5f, p0.y + em * 0.4f), ImGui::GetColorU32(Ink(kRed)), note);
    }
    if (playing) {
        char note[48];
        snprintf(note, sizeof(note), "element %d of %zu", shown + 1, act.elems.size());
        draw->AddText(ImVec2(p0.x + em * 0.5f, p0.y + em * 0.4f), IM_COL32(255, 255, 255, 200), note);
    }
    // The axis
    draw->AddLine(ImVec2(axis.x - em * 0.6f, axis.y), ImVec2(axis.x + em * 0.6f, axis.y), IM_COL32(255, 230, 80, 255));
    draw->AddLine(ImVec2(axis.x, axis.y - em * 0.6f), ImVec2(axis.x, axis.y + em * 0.6f), IM_COL32(255, 230, 80, 255));
    bool changed = false;
    int hit = -1;
    if (boxes && !playing && t.sf) {
        // The selected element's boxes, with handles
        auto toAir = [&](ImVec2 s) { return ImVec2((s.x - axis.x) / k, (s.y - axis.y) / k); };
        BoxHandles(drag, draw, hovered, t, at, toAir, false, &hit);
        static int menuHit;
        static ImVec2 menuAt;
        if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
            menuHit = hit, menuAt = toAir(ImGui::GetMousePos());
            ImGui::OpenPopup("boxes");
        }
        auto exists = [&](int h) { return h >= 0 && h % 1000 < (int)el.clsn[h / 1000].size(); };
        if (ImGui::BeginPopup("boxes")) {
            if (exists(menuHit)) {
                if (ImGui::MenuItem(menuHit / 1000 ? "Delete this Clsn2 box" : "Delete this Clsn1 box", "Delete")) {
                    DeleteBox(t, menuHit / 1000, menuHit % 1000);
                    changed = true;
                }
                ImGui::Separator();
            }
            for (int g = 1; g >= 0; g--)
                if (ImGui::MenuItem(g ? "Add a Clsn2 box here" : "Add a Clsn1 box here")) {
                    int x = (int)lroundf(menuAt.x), y = (int)lroundf(menuAt.y);
                    AirBox b;
                    b.x1 = x - 10, b.y1 = y - 10, b.x2 = x + 10, b.y2 = y + 10;
                    AddBox(t, g, &b);
                    changed = true;
                }
            ImGui::EndPopup();
        }
        if (exists(hit) && !drag.active && ImGui::IsKeyPressed(ImGuiKey_Delete, false) &&
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            DeleteBox(t, hit / 1000, hit % 1000);
            changed = true;
        }
    } else {
        drag.active = false;
        if (boxes)
            for (int g = 1; g >= 0; g--)
                for (const AirBox &b : el.clsn[g]) {
                    ImU32 col = g ? IM_COL32(60, 120, 255, 255) : IM_COL32(255, 60, 60, 255);
                    draw->AddRectFilled(at((float)b.x1, (float)b.y1), at((float)b.x2, (float)b.y2), (col & 0x00FFFFFF) | 0x30000000);
                    draw->AddRect(at((float)b.x1, (float)b.y1), at((float)b.x2, (float)b.y2), col);
                }
    }
    if (hovered && hit < 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) refit = true;
    ImGui::EndChild();
    return changed;
}

// The action a screen's element shows, as a target: the def is its file.
static AnimTarget ScreenAnimTarget(const ScreenAnim &sa) {
    AnimTarget t;
    t.sf = sa.sf;
    if (g_animFollow) {
        g_animAction = sa.action;
        if (sa.elem > 0) g_animElem = sa.elem;
    }
    const AirFile &air = AirOf(*t.sf);
    auto it = air.actions.find(g_animAction);
    if (it == air.actions.end() || it->second.elems.empty()) return t;
    t.act = &it->second;
    t.elem = std::max(1, std::min(g_animElem, (int)t.act->elems.size()));
    t.live = sa.action == g_animAction && sa.elem == t.elem;
    return t;
}

// A strip of the action's elements: each one's sprite, number and time, the loop start marked,
// the element on the game now with a dot. Returns the element clicked (1-based) or 0; *menu: the
// one right clicked.
static int ElementStrip(const AnimTarget &t, SpriteFile *spf, int onGame, int *menu) {
    float em = ImGui::GetFontSize();
    const ImGuiStyle &style = ImGui::GetStyle();
    float thumb = em * 3.2f, pad = em * 0.25f, line = ImGui::GetTextLineHeight();
    ImVec2 cell(thumb + pad * 2, thumb + pad * 3 + line);
    int clicked = 0;
    *menu = 0;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em * 0.3f, em * 0.3f));
    bool open = ImGui::BeginChild("strip", ImVec2(0, cell.y + em * 0.6f + style.ScrollbarSize + style.ChildBorderSize * 2 + 2),
                                  ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PopStyleVar();
    if (open) {
        // The wheel scrolls it sideways (taken from the window around it, which scrolls at the
        // start of a frame: a frame ahead)
        ImGuiID wheel = ImGui::GetID("wheel");
        if (ImGui::IsWindowHovered() && ImGui::GetScrollMaxX() > 0) {
            ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, wheel);
            float w = ImGui::GetIO().MouseWheel;
            if (w != 0) ImGui::SetScrollX(ImGui::GetScrollX() - w * cell.x);
        } else if (ImGui::GetKeyOwner(ImGuiKey_MouseWheelY) == wheel) {
            ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, ImGuiKeyOwner_NoOwner);
        }
        static int scrolledTo, scrolledAction = -1;
        ImDrawList *draw = ImGui::GetWindowDrawList();
        const AirAction &act = *t.act;
        for (int n = 1; n <= (int)act.elems.size(); n++) {
            const AirElem &e = act.elems[n - 1];
            if (n > 1) ImGui::SameLine(0, em * 0.35f);
            ImGui::PushID(n);
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("e", cell);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) clicked = n;
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) *menu = n;
            bool cur = n == t.elem, hot = ImGui::IsItemHovered();
            ImVec2 q(p.x + cell.x, p.y + cell.y);
            draw->AddRectFilled(p, q, ImGui::GetColorU32(cur ? ImGuiCol_Header : hot ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg),
                                style.FrameRounding);
            // The sprite, as large as fits (at most twice its size)
            ImVec2 a(p.x + pad, p.y + pad), b(a.x + thumb, a.y + thumb);
            draw->AddRectFilled(a, b, IM_COL32(40, 40, 44, 255));
            int si = spf ? Sff_FindSprite(spf->doc, e.group, e.image) : -1;
            if (GLuint tex = si >= 0 ? SpriteTexture(*spf, si) : 0) {
                const SffSprite &s = spf->doc.sprites[si];
                float k = std::min(2.0f, std::min(thumb / std::max(1, s.w), thumb / std::max(1, s.h)));
                ImVec2 c(floorf(a.x + (thumb - s.w * k) * 0.5f), floorf(a.y + (thumb - s.h * k) * 0.5f));
                draw->AddImage(TexRef(tex), c, ImVec2(c.x + s.w * k, c.y + s.h * k));
            }
            // Its number, its time
            char num[16], time[16];
            snprintf(num, sizeof(num), "%d", n);
            snprintf(time, sizeof(time), e.time < 0 ? "forever" : "%dt", e.time);
            float ty = b.y + pad * 0.5f;
            draw->AddText(ImVec2(a.x, ty), ImGui::GetColorU32(ImGuiCol_Text), num);
            draw->AddText(ImVec2(b.x - ImGui::CalcTextSize(time).x, ty), ImGui::GetColorU32(ImGuiCol_TextDisabled), time);
            if (cur) draw->AddRect(p, q, ImGui::GetColorU32(ImGuiCol_CheckMark), style.FrameRounding, 2.0f);
            if (act.loopStart == n - 1) {
                // The loop start: a bar before it
                float x = p.x - em * 0.2f;
                draw->AddLine(ImVec2(x, p.y), ImVec2(x, q.y), ImGui::GetColorU32(Ink(kYellow)), 2.0f);
            }
            if (n == onGame) draw->AddCircleFilled(ImVec2(b.x - em * 0.3f, a.y + em * 0.3f), em * 0.22f, ImGui::GetColorU32(Ink(kGreen)));
            if (ImGui::BeginItemTooltip()) {
                ImGui::Text("Element %d: sprite %d, %d; offset %d, %d; time %d", n, e.group, e.image, e.x, e.y, e.time);
                if (act.loopStart == n - 1) ImGui::TextColored(Ink(kYellow), "The loop starts here.");
                if (n == onGame) ImGui::TextColored(Ink(kGreen), "On the game now.");
                ImGui::TextDisabled("Right click: duplicate, delete, move, loop from here");
                ImGui::EndTooltip();
            }
            if (cur && (scrolledTo != n || scrolledAction != g_animAction)) {
                ImGui::SetScrollHereX(0.5f);
                scrolledTo = n, scrolledAction = g_animAction;
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    return clicked;
}

// A row of the element panel: its label, then the field.
static void FieldLabel(const char *label) {
    float x = ImGui::GetCursorPosX();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine();
    ImGui::SetCursorPosX(x + ImGui::CalcTextSize("Offset").x + ImGui::GetStyle().ItemSpacing.x * 2);
}

// The selected element: what can be done with it, its sprite, offset and time, its boxes.
// goTo: the element to show after an edit of the elements. Returns true when its lines changed
// (what was parsed is old: the caller stops).
static bool ElementPanel(const AnimTarget &t, bool screen, SpriteFile *spf, const std::function<void(SourceFile &)> &apply,
                         int &goTo) {
    SourceFile &sf = *t.sf;
    const AirAction &act = *t.act;
    const AirElem &el = act.elems[t.elem - 1];
    int index = t.elem - 1, count = (int)act.elems.size();
    float em = ImGui::GetFontSize();
    // The next small button on the same line when it fits there
    auto flow = [](const char *label) {
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2) ImGui::NewLine();
    };
    char title[48];
    snprintf(title, sizeof(title), "Element %d of %d", t.elem, count);
    ImGui::SeparatorText(title);
    if (ImGui::SmallButton("Duplicate") && Air_DuplicateElem(sf.text, act, index)) goTo = t.elem + 1;
    flow("Delete");
    ImGui::BeginDisabled(count < 2);
    if (ImGui::SmallButton("Delete") && Air_DeleteElem(sf.text, act, index)) goTo = std::max(1, t.elem - 1);
    ImGui::EndDisabled();
    flow("<");
    ImGui::BeginDisabled(index == 0);
    if (ImGui::SmallButton("<") && Air_MoveElem(sf.text, act, index, -1)) goTo = t.elem - 1;
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Move it earlier");
    ImGui::SameLine(0, 2);
    ImGui::BeginDisabled(index == count - 1);
    if (ImGui::SmallButton(">") && Air_MoveElem(sf.text, act, index, 1)) goTo = t.elem + 1;
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Move it later");
    bool loopHere = act.loopStart == index;
    flow(loopHere ? "No loop" : "Loop here");
    if (ImGui::SmallButton(loopHere ? "No loop" : "Loop here") && Air_SetLoopStart(sf.text, act, loopHere ? -1 : index))
        goTo = t.elem;
    ImGui::SetItemTooltip(loopHere ? "The action loops from here: make it loop from the start" : "Make the action loop from here (LoopStart)");
    if (goTo) return true;
    ImGui::Spacing();

    // sprite (group, image), offset (x, y), time
    int v[5] = {el.group, el.image, el.x, el.y, el.time};
    bool done = false;
    float pickW = ImGui::CalcTextSize("Pick...").x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x;
    FieldLabel("Sprite");
    ImGui::SetNextItemWidth(std::max(em * 5, ImGui::GetContentRegionAvail().x - pickW));
    if (ImGui::DragInt2("##sprite", &v[0], 0.2f)) Air_SetElem(sf.text, el, v[0], v[1], v[2], v[3], v[4]);
    done |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SetItemTooltip("Group, image");
    ImGui::SameLine();
    if (ImGui::Button("Pick...")) ImGui::OpenPopup("pick");
    ImGui::SetItemTooltip("Pick the element's sprite among the sprites of a group");
    FieldLabel("Offset");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::DragInt2("##offset", &v[2], 0.2f)) Air_SetElem(sf.text, el, v[0], v[1], v[2], v[3], v[4]);
    done |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SetItemTooltip("x, y from the axis");
    FieldLabel("Time");
    ImGui::SetNextItemWidth(em * 4);
    if (ImGui::DragInt("##time", &v[4], 0.2f, -1, 9999)) Air_SetElem(sf.text, el, v[0], v[1], v[2], v[3], v[4]);
    done |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    ImGui::TextDisabled(v[4] < 0 ? "forever" : "ticks");
    if (done) apply(sf);
    if (ImGui::BeginPopup("pick")) {
        static int group = 0;
        if (ImGui::IsWindowAppearing()) group = el.group;
        ImGui::SetNextItemWidth(em * 6);
        ImGui::InputInt("Group", &group);
        if (!spf) ImGui::TextDisabled("Waiting for the sprites...");
        float cell = em * 5;
        int col = 0, cols = 8;
        for (int i = 0; spf && i < (int)spf->doc.sprites.size(); i++) {
            const SffSprite &s = spf->doc.sprites[i];
            if (s.group != group) continue;
            if (col++ % cols) ImGui::SameLine();
            ImGui::PushID(i);
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            bool picked = ImGui::InvisibleButton("s", ImVec2(cell, cell + em));
            ImDrawList *draw = ImGui::GetWindowDrawList();
            bool cur = s.group == el.group && s.number == el.image;
            Checkerboard(draw, p0, ImVec2(p0.x + cell, p0.y + cell));
            if (GLuint tex = SpriteTexture(*spf, i)) {
                float k = std::min(cell / std::max(1, s.w), cell / std::max(1, s.h));
                ImVec2 a(p0.x + (cell - s.w * k) * 0.5f, p0.y + (cell - s.h * k) * 0.5f);
                draw->AddImage(TexRef(tex), a, ImVec2(a.x + s.w * k, a.y + s.h * k));
            }
            draw->AddRect(p0, ImVec2(p0.x + cell, p0.y + cell),
                          cur ? ImGui::GetColorU32(Ink(kYellow)) : ImGui::GetColorU32(ImGuiCol_Text, ImGui::IsItemHovered() ? 0.8f : 0.16f));
            char label[32];
            snprintf(label, sizeof(label), "%d, %d", s.group, s.number);
            draw->AddText(ImVec2(p0.x, p0.y + cell), ImGui::GetColorU32(ImGuiCol_Text), label);
            if (picked && Air_SetElem(sf.text, el, s.group, s.number, el.x, el.y, el.time)) {
                apply(sf);
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        if (col == 0 && spf) ImGui::TextDisabled("No sprites in group %d.", group);
        ImGui::EndPopup();
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Line %d", el.line);
    ImGui::SameLine();
    if (ImGui::SmallButton("Show in Code")) {
        g_codeFile = sf.path;
        g_codeLine = g_codeScrollTo = el.line;
    }

    // The boxes: Clsn2, then Clsn1
    for (int g = 1; g >= 0 && !screen; g--) {
        ImGui::PushID(g);
        ImGui::Spacing();
        ImGui::SeparatorText(g == 1 ? "Clsn2: can be hit" : "Clsn1: hits");
        // A swatch of the colour the preview draws them in, where they come from
        ImVec2 p = ImGui::GetCursorScreenPos();
        float s = ImGui::GetTextLineHeight() * 0.7f, y = p.y + (ImGui::GetTextLineHeight() - s) * 0.5f;
        ImU32 col = g == 1 ? IM_COL32(80, 140, 255, 255) : IM_COL32(255, 70, 70, 255);
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + s, y + s), (col & 0x00FFFFFF) | 0x50000000);
        ImGui::GetWindowDrawList()->AddRect(ImVec2(p.x, y), ImVec2(p.x + s, y + s), col);
        ImGui::Dummy(ImVec2(s, ImGui::GetTextLineHeight()));
        ImGui::SameLine();
        if (el.header[g] == 0)
            ImGui::TextDisabled("none");
        else if (el.own[g])
            ImGui::TextDisabled("its own");
        else
            ImGui::TextDisabled("the action's default (%d)", SharedBy(act, g, el.header[g]));
        if (el.header[g] != 0 && ImGui::BeginItemTooltip()) {
            if (el.own[g])
                ImGui::Text("The element's own boxes, line %d.", el.header[g]);
            else
                ImGui::Text("The action's default boxes (line %d), shared by %d elements: a change changes them all.",
                            el.header[g], SharedBy(act, g, el.header[g]));
            ImGui::EndTooltip();
        }
        float delW = ImGui::GetFrameHeight();
        for (size_t i = 0; i < el.clsn[g].size(); i++) {
            const AirBox &b = el.clsn[g][i];
            ImGui::PushID((int)i);
            int r[4] = {b.x1, b.y1, b.x2, b.y2};
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - delW - ImGui::GetStyle().ItemSpacing.x);
            if (ImGui::DragInt4("##box", r, 0.2f)) Air_SetBox(sf.text, b, r[0], r[1], r[2], r[3]);
            if (ImGui::IsItemDeactivatedAfterEdit()) apply(sf);
            ImGui::SetItemTooltip("x1, y1, x2, y2");
            ImGui::SameLine();
            bool del = ImGui::Button("x", ImVec2(delW, 0));
            ImGui::SetItemTooltip("Delete this box (the element gets its own boxes)");
            ImGui::PopID();
            if (del) {
                DeleteBox(t, g, (int)i);
                ImGui::PopID();
                return true;
            }
        }
        if (ImGui::Button("Add a box", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
            AddBox(t, g);
            ImGui::PopID();
            return true;
        }
        ImGui::PopID();
    }
    return false;
}

void AnimationWindow() {
    if (!BeginTool("Animation")) return;
    // What it shows: the selected player's animation, or that of the screen's element picked last
    static bool onScreen = false;
    static int seenSel = -2, seenSelPicked = 0, seenPicked = 0;
    if (g_selId != seenSel || g_selPicked != seenSelPicked) seenSel = g_selId, seenSelPicked = g_selPicked, onScreen = false;
    if (g_screenAnimPicked != seenPicked) seenPicked = g_screenAnimPicked, onScreen = true, g_animFollow = true;
    ScreenAnim sa;
    bool screen = onScreen && SelectedScreenAnim(sa);
    AnimTarget t = screen ? ScreenAnimTarget(sa) : GetAnimTarget();
    // An edit: the game shows it (a character's animations, or the screen's def)
    std::function<void(SourceFile &)> apply = [&](SourceFile &s) {
        if (screen) {
            sa.apply();
        } else {
            ApplyAnims(s);
        }
    };
    bool disk = !screen && t.pn == kDiskPn; // a character not in the match, from disk: no game to show it
    if (!screen && !t.e && !disk) {
        ImGui::TextDisabled("Pick a player in the Game or Players window, a character in Explorer, or an animated\n"
                            "element of a screen (Fight screen, Motif, Storyboard).");
        ImGui::End();
        return;
    }
    float em = ImGui::GetFontSize();
    const ImGuiStyle &style = ImGui::GetStyle();
    auto rightAlign = [&](float w) { // the next item at the right edge, on the same line
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < w)
            ImGui::NewLine();
        else
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - w);
    };

    // Whose animations, the file, Save and Revert
    ImGui::AlignTextToFramePadding();
    if (screen) {
        ImGui::TextUnformatted(sa.label.c_str());
        if (!t.live && sa.elem == 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("· not on screen now");
        }
    } else if (disk) {
        ImGui::TextUnformatted(CharFile(kDiskPn, "def").c_str());
        ImGui::SameLine();
        ImGui::TextColored(Ink(kOrange), "from disk");
        ImGui::SetItemTooltip("Not in the match: its AIR file edited from disk. Put the character in the match to see it on the game.");
    } else {
        ImGui::Text("%s  %s", Who(t.e->pn, t.e->helper, t.e->id).c_str(), t.e->name.c_str());
    }
    if (!t.sf) {
        ImGui::TextDisabled(g_canTrace ? "Waiting for the files of P%d..." : "Needs the patched Ikemen GO.", t.pn);
        ImGui::End();
        return;
    }
    SourceFile &sf = *t.sf;
    {
        ImGui::SameLine();
        ImGui::TextDisabled("· %s%s", sf.path.c_str(), sf.Dirty() ? " *" : "");
        float w = ImGui::CalcTextSize("Save").x + ImGui::CalcTextSize("Revert").x + style.FramePadding.x * 4 + style.ItemSpacing.x;
        rightAlign(w);
        ImGui::BeginDisabled(!sf.Dirty());
        if (sf.Dirty()) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        bool save = ImGui::Button("Save");
        if (sf.Dirty()) ImGui::PopStyleColor();
        if (save) SaveSource(sf);
        ImGui::SetItemTooltip(screen ? "Writes the def" : "Writes the AIR file");
        ImGui::SameLine();
        if (ImGui::Button("Revert")) {
            ReadSource(sf);
            apply(sf);
        }
        ImGui::EndDisabled();
    }

    // The action: its number, the one before or after; following the game; what a drag on it does
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Action");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(em * 5);
    if (ImGui::InputInt("##action", &g_animAction, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue)) g_animFollow = false;
    const AirFile &air = AirOf(sf);
    ImGui::SameLine(0, 2);
    if (ImGui::ArrowButton("##prevact", ImGuiDir_Left)) {
        auto it = air.actions.lower_bound(g_animAction);
        if (it != air.actions.begin()) g_animAction = std::prev(it)->first, g_animElem = 1, g_animFollow = false;
    }
    ImGui::SetItemTooltip("The action before");
    ImGui::SameLine(0, 2);
    if (ImGui::ArrowButton("##nextact", ImGuiDir_Right)) {
        auto it = air.actions.upper_bound(g_animAction);
        if (it != air.actions.end()) g_animAction = it->first, g_animElem = 1, g_animFollow = false;
    }
    ImGui::SetItemTooltip("The action after");
    ImGui::SameLine(0, em);
    ImGui::BeginDisabled(disk);
    ImGui::Checkbox("Follow", &g_animFollow);
    ImGui::EndDisabled();
    ImGui::SetItemTooltip(screen ? "Show what the element shows on the screen" : "Show what the player shows on the game");
    if (!screen && !disk) {
        ImGui::SameLine(0, em);
        ImGui::TextUnformatted("On the game, drag");
        ImGui::SameLine();
        const char *drags[] = {"nothing", "its boxes", "the sprite"};
        int d = g_boxEdit ? 1 + g_animDrag : 0;
        ImGui::SetNextItemWidth(ImGui::CalcTextSize("the sprite").x + ImGui::GetFrameHeight() + style.FramePadding.x * 2);
        if (ImGui::Combo("##gamedrag", &d, drags, 3)) {
            g_boxEdit = d > 0;
            if (d > 0) g_animDrag = d - 1;
        }
        HelpMarker("Pause the game (F5). Its boxes: drag the edges, the corners or the inside of a box of the selected "
                   "player's element. The sprite: drag near the player to move the element's offset. Changes go to "
                   "the game at once; Save writes the AIR file. Mouse wheel on the game: zoom; right button: pan.");
    } else {
        HelpMarker(screen ? "The action this element of the screen shows, in its def ([Begin Action]). Every change goes "
                            "to the game at once (the def is loaded again); Save writes the def, which its window edits too."
                          : "A character picked in Explorer that is not in the match: its AIR file edited from disk, the "
                            "action drawn below. Save writes it; put the character in the match to see it on the game.");
    }
    // What can be done with the action, in a menu
    const char *want = nullptr;
    if (t.act) {
        rightAlign(ImGui::CalcTextSize("Action").x + style.FramePadding.x * 2 + ImGui::GetFrameHeight());
        if (ImGui::BeginCombo("##actmenu", "Action", ImGuiComboFlags_WidthFitPreview | ImGuiComboFlags_PopupAlignLeft)) {
            if (!screen && t.e && ImGui::Selectable("Show on the player")) {
                ExecOn(t.e->id, "changeAnim(" + std::to_string(g_animAction) + ")");
                g_animFollow = true;
            }
            if (!screen && t.e) ImGui::SetItemTooltip("ChangeAnim on the selected player (not on helpers); pause first to keep it.");
            if (ImGui::Selectable("Copy as...")) want = "copy";
            if (ImGui::Selectable("Renumber...")) want = "renumber";
            ImGui::Separator();
            if (ImGui::Selectable("Delete...")) want = "delete";
            ImGui::EndCombo();
        }
    }
    static int newNo;
    if (want) {
        newNo = want[0] == 'c' ? g_animAction + 1 : g_animAction;
        ImGui::OpenPopup(want);
    }
    for (const char *popup : {"copy", "renumber"}) {
        if (!ImGui::BeginPopup(popup)) continue;
        bool copy = popup[0] == 'c';
        ImGui::TextUnformatted(copy ? "A copy of this action, numbered" : "This action's new number");
        ImGui::SetNextItemWidth(em * 6);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        bool ok = ImGui::InputInt("##no", &newNo, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        ok |= ImGui::Button("OK");
        bool taken = air.actions.count(newNo) != 0;
        if (taken) ImGui::TextColored(Ink(kRed), "Action %d exists already.", newNo);
        if (ok && !taken && t.act) {
            if (copy ? Air_CopyAction(sf.text, *t.act, newNo) : Air_RenumberAction(sf.text, *t.act, newNo)) {
                apply(sf);
                g_animAction = newNo;
                g_animFollow = false;
            }
            ImGui::CloseCurrentPopup();
        }
        if (!copy) ImGui::TextDisabled("States that use the old number are not changed.");
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("delete")) {
        ImGui::Text("Delete action %d?", g_animAction);
        if (ImGui::Button("Delete") && t.act) {
            if (Air_DeleteAction(sf.text, *t.act)) apply(sf);
            g_animFollow = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (!t.act) {
        ImGui::Spacing();
        ImGui::TextDisabled("Action %d is not in %s.", g_animAction, sf.path.c_str());
        ImGui::End();
        return;
    }

    // The elements; a click shows one on the game too (pause first to keep it there)
    SpriteFile *spf = screen ? sa.sprites : SpritesOf(t.pn);
    bool live = !screen && t.fe && t.fe->action == g_animAction;
    auto showElem = [&](int n) {
        g_animElem = n;
        if (live) {
            char lua[96];
            snprintf(lua, sizeof(lua), "ikdbg.with(%d, function() debugSetAnimElem(%d) end)", t.e->id, n);
            SendRaw(lua);
        }
    };
    int onGame = live ? t.fe->elem : screen && sa.action == g_animAction ? sa.elem : 0;
    int menuElem;
    int clicked = ElementStrip(t, spf, onGame, &menuElem);
    if (clicked || menuElem) {
        showElem(clicked ? clicked : menuElem);
        if (!live) g_animFollow = false;
    }
    if (menuElem) ImGui::OpenPopup("element");
    int goTo = 0; // element to show after an edit of the elements
    if (ImGui::BeginPopup("element")) {
        int index = t.elem - 1, count = (int)t.act->elems.size();
        ImGui::TextDisabled("Element %d", t.elem);
        if (ImGui::MenuItem("Duplicate") && Air_DuplicateElem(sf.text, *t.act, index)) goTo = t.elem + 1;
        if (ImGui::MenuItem("Delete", nullptr, false, count > 1) && Air_DeleteElem(sf.text, *t.act, index))
            goTo = std::max(1, t.elem - 1);
        if (ImGui::MenuItem("Move earlier", nullptr, false, index > 0) && Air_MoveElem(sf.text, *t.act, index, -1))
            goTo = t.elem - 1;
        if (ImGui::MenuItem("Move later", nullptr, false, index < count - 1) && Air_MoveElem(sf.text, *t.act, index, 1))
            goTo = t.elem + 1;
        bool loopHere = t.act->loopStart == index;
        if (ImGui::MenuItem("Loop from here", nullptr, loopHere) && Air_SetLoopStart(sf.text, *t.act, loopHere ? -1 : index))
            goTo = t.elem;
        ImGui::EndPopup();
    }

    // The preview and the element's panel: side by side when there is room, else one under the other
    bool changed = false;
    if (!goTo) {
        float h = std::max(em * 12, ImGui::GetContentRegionAvail().y - style.CellPadding.y * 2 - 2);
        if (ImGui::GetContentRegionAvail().x > em * 34 &&
            ImGui::BeginTable("main", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("preview", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("element", ImGuiTableColumnFlags_WidthFixed, em * 18);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            changed = AnimPreview(t, spf, !screen, h - ImGui::GetFrameHeightWithSpacing());
            ImGui::TableSetColumnIndex(1);
            if (!changed) {
                if (ImGui::BeginChild("element", ImVec2(0, h))) changed = ElementPanel(t, screen, spf, apply, goTo);
                ImGui::EndChild();
            }
            ImGui::EndTable();
        } else {
            changed = AnimPreview(t, spf, !screen, 0);
            if (!changed) changed = ElementPanel(t, screen, spf, apply, goTo);
        }
    }
    if (goTo) {
        apply(sf);
        if (screen) g_animFollow = false;
        showElem(goTo);
    }
    ImGui::End();
}

} // namespace app
