// Animation: the project's AIR element, its boxes edited here and on the game; the action
// a screen's element shows (fight screen, motif, storyboard: [Begin Action] in their def); or any
// file of actions by itself (a stage's or a screen's def, an AIR file no character uses).
#include "app_internal.h"
#include "gif.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "ini.h"
#include "sffedit.h"

#include <cfloat>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace app {

bool g_boxEdit = true;    // the project's boxes can be dragged on the Game window
bool g_followSel = true;  // a zoomed Game window keeps the selected player in view
int g_animDrag = 0;       // what a drag on the selected player changes: 0 its boxes, 1 the sprite's offset
bool g_onion = true;      // the onion skin: the elements around are drawn too, faint: their boxes
bool g_onionSprite = false; // their sprites
bool g_onionBefore = true, g_onionAfter = false; // the element before, the one after
bool g_animFollow = true; // the Animation window shows what the selected player shows
int g_animAction = 0, g_animElem = 1;
static std::string g_animFile; // the file of actions shown by itself, "" none
static int g_animFileN;    // bumped when it is asked for
static int g_boxDraw = 0;  // a drag on the preview draws a box: 1 Clsn1, 2 Clsn2, 0 none
static int g_animTick = -1; // the tick of the action while it plays
static struct {
    bool on = false;
    int action = 0, elem = 0; // index
} g_animGhost;             // an element of any action drawn faint over the one shown, to compare
static std::vector<AirBox> g_boxClip[2]; // boxes copied, Clsn1 and Clsn2

std::map<int, std::pair<std::string, double>> g_swapWanted;

// Blending of the preview's sprite: GL 1.4's blend equation and constant colour, which Windows' opengl32 only
// gives through wglGetProcAddress. The draw callbacks set it before the sprite and put ImGui's state back.
namespace {
struct AnimBlendFns {
    void(APIENTRY *equation)(GLenum) = nullptr;
    void(APIENTRY *color)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
};
AnimBlendFns g_blendFns;

AnimBlendFns LoadBlendFns() {
    AnimBlendFns f;
    f.equation = (void(APIENTRY *)(GLenum))(void *)wglGetProcAddress("glBlendEquation");
    f.color = (void(APIENTRY *)(GLfloat, GLfloat, GLfloat, GLfloat))(void *)wglGetProcAddress("glBlendColor");
    g_blendFns = f;
    return f;
}

const GLenum kFuncAdd = 0x8006, kFuncReverseSubtract = 0x800B, kConstantAlpha = 0x8003;

// The data: the AirBlend, and the destination's alpha above its 8 bits
void SetBlend(const ImDrawList *, const ImDrawCmd *cmd) {
    intptr_t data = (intptr_t)cmd->UserCallbackData;
    g_blendFns.equation((data & 255) == kAirAdd ? kFuncAdd : kFuncReverseSubtract);
    g_blendFns.color(0, 0, 0, ((data >> 8) & 255) / 255.0f);
    glBlendFunc(GL_SRC_ALPHA, kConstantAlpha);
}

void EndBlend(const ImDrawList *, const ImDrawCmd *) {
    g_blendFns.equation(kFuncAdd);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
} // namespace

void DrawStyledSprite(ImDrawList *draw, GLuint tex, const SffSprite &s, const AirStyle &st, float ex, float ey,
                      const std::function<ImVec2(float, float)> &at) {
    float fx = st.flipH ? -1.0f : 1.0f, fy = st.flipV ? -1.0f : 1.0f;
    float sx = st.xscale * fx, sy = st.yscale * fy;
    float rad = st.angle * 3.14159265f / 180, cs = cosf(rad), sn = sinf(rad);
    auto corner = [&](float cx, float cy) {
        float px = cx * sx, py = cy * sy;
        return at(ex * fx + px * cs + py * sn, ey * fy - px * sn + py * cs);
    };
    float x0 = (float)-s.x, y0 = (float)-s.y, x1 = (float)(s.w - s.x), y1 = (float)(s.h - s.y);
    bool blend = st.blend != kAirOpaque;
    if (blend) {
        static AnimBlendFns fns = LoadBlendFns();
        if (fns.equation && fns.color) draw->AddCallback(SetBlend, (void *)(intptr_t)(st.blend | st.dst << 8));
        else blend = false;
    }
    draw->AddImageQuad(TexRef(tex), corner(x0, y0), corner(x1, y0), corner(x1, y1), corner(x0, y1), ImVec2(0, 0), ImVec2(1, 0),
                       ImVec2(1, 1), ImVec2(0, 1), blend ? IM_COL32(255, 255, 255, st.src) : IM_COL32_WHITE);
    if (blend) {
        draw->AddCallback(EndBlend, nullptr);
        draw->AddCallback(ImGui::GetPlatformIO().DrawCallback_ResetRenderState, nullptr);
    }
}

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
    int work = WorkPn();
    if (work == kDiskPn) {
        // The project, not in the match: its AIR file from disk
        t.pn = kDiskPn;
    } else if (work) {
        int id = ProjectFighter(work);
        t.e = FindEntity(id);
        t.fe = FindFrameEntity(id);
        // In a state of another's, showing its animation: not the project's, so nothing to follow
        if (t.fe && t.fe->action >= 0 && !OfProject(t.fe->animPn)) t.fe = nullptr;
        t.pn = t.fe && t.fe->action >= 0 ? t.fe->animPn : work;
    }
    if (t.pn <= 0) return t;
    std::string path = AirPath(t.pn);
    if (path.empty()) return t;
    t.sf = &LoadSource(path);
    const AirFile &air = AirOf(*t.sf);
    // Not under a drag: the edit stays on the element it began on
    if (g_animFollow && t.fe && t.fe->action >= 0 && !ImGui::IsAnyItemActive()) {
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

// The action and element a line of an AIR file belongs to (the action before it when the line is between
// two), shown in Animation, with the character that uses the file open as the project. False: not an AIR file in use.
bool AnimShowLine(const std::string &path, int line) {
    auto uses = [&](int pn) { return pn > 0 && NormPath(AirPath(pn)) == NormPath(path); };
    bool isAir = path.size() > 4 && _stricmp(path.c_str() + path.size() - 4, ".air") == 0;
    // The project, else a player in the match, else a character from disk whose def is
    // next to the file (the game not running)
    if (isAir && !uses(WorkPn())) {
        int pn = 0;
        for (const auto &kv : g_charFiles)
            if (uses(kv.first)) pn = kv.first;
        if (pn) {
            ProjectOpen(kProjectCharacter, CharFile(pn, "def"), false);
        } else {
            std::string dir = path.substr(0, path.find_last_of("/\\") + 1);
            std::error_code ec;
            for (const auto &de : std::filesystem::directory_iterator(GamePath(dir), ec)) {
                std::string name = de.path().filename().string();
                if (name.size() < 5 || _stricmp(name.c_str() + name.size() - 4, ".def") != 0) continue;
                if (NormPath(DefFile(dir + name, "anim")) != NormPath(path)) continue;
                ProjectOpen(kProjectCharacter, dir + name, false);
                break;
            }
        }
    }
    const AirFile &air = AirOf(LoadSource(path));
    const AirAction *act = nullptr;
    for (const auto &kv : air.actions)
        if (kv.second.line <= line && (!act || kv.second.line > act->line)) act = &kv.second;
    if (!act) return false;
    int elem = 1;
    for (size_t i = 0; i < act->elems.size(); i++)
        if (act->elems[i].first <= line) elem = (int)i + 1;
    // No character's: a def's actions, or an AIR file by itself
    AnimOpenFile(isAir && uses(WorkPn()) ? "" : path, act->no, elem);
    // Called from Code, whose editor holds the keyboard: a dock node keeps showing the tab of the window
    // with the focus, so Code would come back as soon as ShowWindow's frames are over
    ImGui::ClearActiveID();
    ImGui::SetWindowFocus("Animation");
    return true;
}

// Undo and redo of an AIR file's text: a change is one step when the mouse lets go of it.
static std::map<std::string, UndoStack<std::string>> g_animHistory; // by file

UndoStack<std::string> &TrackText(const std::string &path, const std::string &text, bool saved) {
    UndoStack<std::string> &h = g_animHistory[NormPath(path)];
    if (!h.Started())
        h.Start(text, saved);
    else if (text != h.Current() && !ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        h.Record(text);
    return h;
}

UndoStack<std::string> &TrackAnims(const SourceFile &sf) { return TrackText(sf.path, sf.text, !sf.Dirty()); }

static std::set<int> g_elemPicks; // elements (1-based) selected with Ctrl or Shift; empty: the one shown alone

// The elements an edit of the selection acts on: indices sorted, the one shown when none is selected.
static std::vector<int> PickedElems(const AnimTarget &t) {
    std::vector<int> out;
    for (int n : g_elemPicks) out.push_back(n - 1);
    if (out.empty()) out.push_back(t.elem - 1);
    return out;
}

// The action shown gone from the text (deleted, a new one undone): the nearest one takes its place.
static void KeepActionShown(const SourceFile &sf) {
    const AirFile &air = AirOf(sf);
    if (air.actions.empty() || air.actions.count(g_animAction)) return;
    auto it = air.actions.lower_bound(g_animAction);
    if (it == air.actions.end()) --it;
    g_animAction = it->first, g_animElem = 1;
}

void AnimOpenFile(const std::string &path, int action, int elem, bool show) {
    g_animFile = path;
    g_animFileN++;
    g_animFollow = false;
    if (action >= 0) g_animAction = action, g_animElem = std::max(elem, 1);
    if (!path.empty()) {
        // No action asked for: the first one
        const AirFile &air = AirOf(LoadSource(path));
        if (action < 0 && !air.actions.empty()) g_animAction = air.actions.begin()->first, g_animElem = 1;
        KeepActionShown(LoadSource(path));
        // Its sprites in Sprites, at the element's
        if (std::string sff = SpritesFileOf(path); !sff.empty()) {
            SpriteFile &spr = SpriteFileAt(sff);
            auto it = air.actions.find(g_animAction);
            bool has = it != air.actions.end() && g_animElem <= (int)it->second.elems.size();
            std::string what = ScreenFileName(path);
            ShowInSprites(spr, has ? Sff_FindSprite(spr.doc, it->second.elems[g_animElem - 1].group, it->second.elems[g_animElem - 1].image) : -1,
                          what.empty() ? "file " + path.substr(path.find_last_of("/\\") + 1) : what);
        }
    }
    if (show) ShowWindow("Animation");
}

void AnimFollowFile() { g_animFollow = true; }

// The file shown by itself, as a screen's element would be: what shows it now, in the game or not.
static bool FileAnim(ScreenAnim &out) {
    if (g_animFile.empty()) return false;
    out = ScreenAnim();
    out.sf = &LoadSource(g_animFile);
    std::string sff = SpritesFileOf(g_animFile);
    out.sprites = sff.empty() ? nullptr : &SpriteFileAt(sff);
    out.label = ScreenFileName(g_animFile);
    out.disk = out.label.empty() || !Connected();
    if (!out.disk) {
        out.label = "The " + out.label;
        if (g_animFollow) StageBgAnim(g_animFile, out.action, out.elem); // the BG picked
        int elem = 0;
        if (out.action < 0 && ScreenShowsAction(g_animFile, g_animAction, &elem)) out.action = g_animAction, out.elem = elem;
    }
    SourceFile *sf = out.sf;
    out.apply = [sf] { ApplyAnims(*sf); };
    return true;
}

// The changed AIR text goes to every player using that file (the file itself is saved apart), and
// to the screen or stage whose def it is.
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
    ScreenTextEdited(sf);
}

// The actions of a def, each one shown in Animation when clicked.
void ActionList(SourceFile &sf) {
    const AirFile &air = AirOf(sf);
    char title[48];
    snprintf(title, sizeof(title), "Animations (%zu)", air.actions.size());
    ImGui::SeparatorText(title);
    for (const auto &[no, a] : air.actions) {
        char row[96];
        snprintf(row, sizeof(row), "Action %d  %s##anim", no, a.name.c_str());
        bool shown = no == g_animAction && NormPath(g_animFile) == NormPath(sf.path);
        if (ImGui::Selectable(row, shown)) AnimOpenFile(sf.path, no);
        if (!a.name.empty()) ImGui::SetItemTooltip("%s", a.name.c_str());
    }
    if (ImGui::SmallButton(ICON_PLUS " New##animlist")) AnimOpenFile(sf.path, -1);
    ImGui::SetItemTooltip("Shows this file's animations in Animation: its Action menu makes a new one");
}

// Those boxes as the element's own on every element where says so, of one action or (no -1) all.
template <class Where>
static void SetBoxesWhere(SourceFile &sf, int no, int g, const std::vector<AirBox> &boxes, Where where) {
    auto same = [&](const std::vector<AirBox> &a) {
        if (a.size() != boxes.size()) return false;
        for (size_t i = 0; i < a.size(); i++)
            if (a[i].x1 != boxes[i].x1 || a[i].y1 != boxes[i].y1 || a[i].x2 != boxes[i].x2 || a[i].y2 != boxes[i].y2) return false;
        return true;
    };
    for (int guard = 0; guard < 20000; guard++) { // each edit moves the lines after it: read it all again
        AirFile f;
        Air_Parse(sf.text, f);
        const AirElem *next = nullptr;
        for (const auto &kv : f.actions) {
            if (no >= 0 && kv.first != no) continue;
            for (const AirElem &e : kv.second.elems)
                if (!next && where(e) && !same(e.clsn[g])) next = &e;
        }
        if (!next || !Air_SetOwnBoxes(sf.text, *next, g, boxes)) break;
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

int OnionElem(const AirAction &act, int index, int dir) {
    int n = (int)act.elems.size();
    if (dir < 0) return index >= 1 ? index - 1 : n - 1;
    return index + 1 < n ? index + 1 : std::max(0, act.loopStart);
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
    float em = ImGui::GetFontSize(), top = ImGui::GetCursorPosY();
    // Its toolbar: play, the onion skin, the zoom; then what the mouse does on the boxes
    if (ImGui::Button(playing ? ICON_STOP " Stop" : ICON_PLAY " Play", ImVec2(em * 4.5f, 0))) {
        playing = !playing;
        started = ImGui::GetTime();
    }
    ImGui::SetItemTooltip("Plays the action at the game's speed (60 ticks a second), from its loop start after the end");
    // The onion skin: which elements, what of them
    ImGui::SameLine();
    if (ImGui::Button("Onion skin " ICON_CARET)) ImGui::OpenPopup("onion");
    ImGui::SetItemTooltip("The elements before and after the one shown, faint, here and on the game");
    if (ImGui::BeginPopup("onion")) {
        ImGui::Checkbox("The element before", &g_onionBefore);
        ImGui::SetItemTooltip("Drawn faint, its sprite orange");
        ImGui::Checkbox("The element after", &g_onionAfter);
        ImGui::SetItemTooltip("Drawn faint, its sprite green");
        ImGui::Separator();
        ImGui::Checkbox("Their sprites", &g_onionSprite);
        if (boxes) ImGui::Checkbox("Their boxes", &g_onion);
        ImGui::EndPopup();
    }
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
        HelpMarker(boxes ? "Drag boxes: drag an edge, a corner or the inside of a box to change it; the arrow keys move "
                           "the box under the mouse (Shift: by 10). Drag sprite: drag anywhere to move the sprite; the "
                           "arrow keys move it too. Right click: add a hurt or hit box there, or delete the box "
                           "under the mouse (Delete does it too). Changes go to the game at once when the character "
                           "is in the match; Save writes the file. Wheel: zoom; middle drag: pan; double click: fit."
                         : "Wheel: zoom; middle drag: pan; double click: fit.");
    }
    if (boxes) {
        // What a drag changes, here and on the game
        const ImGuiStyle &style = ImGui::GetStyle();
        float radio = ImGui::GetFrameHeight() + style.ItemInnerSpacing.x;
        SameLineFits(ImGui::CalcTextSize("Drag: boxes sprite").x + radio * 2 + style.ItemSpacing.x * 2);
        ImGui::TextDisabled("Drag:");
        ImGui::SameLine();
        ImGui::RadioButton("boxes##drag", &g_animDrag, 0);
        ImGui::SetItemTooltip("A drag changes the element's boxes (its edges, corners or the whole box)");
        ImGui::SameLine();
        ImGui::RadioButton("sprite##drag", &g_animDrag, 1);
        ImGui::SetItemTooltip("A drag moves the element's sprite (its offset from the axis)");
        SameLineFits(ImGui::CalcTextSize("Draw: Hit box Hurt box").x + style.FramePadding.x * 4 + style.ItemSpacing.x * 2);
        ImGui::TextDisabled("Draw:");
        for (int g = 1; g <= 2; g++) {
            ImGui::SameLine();
            bool on = g_boxDraw == g;
            if (on) ImGui::PushStyleColor(ImGuiCol_Button, g == 1 ? IM_COL32(200, 50, 50, 255) : IM_COL32(50, 100, 220, 255));
            if (ImGui::SmallButton(g == 1 ? "Hit box" : "Hurt box")) g_boxDraw = on ? 0 : g;
            if (on) ImGui::PopStyleColor();
            ImGui::SetItemTooltip(g == 1 ? "A drag on the preview draws a box that hits (Clsn1); a right click stops" : "A drag on the preview draws a box that can be hit (Clsn2); a right click stops");
        }
    }
    const AirAction &act = *t.act;
    // The element shown: the one selected, or the one playing
    int shown = t.elem - 1;
    float progress = 0; // through the element shown, for its interpolation
    g_animTick = -1;
    if (playing) {
        if (forAct != &act) started = ImGui::GetTime();
        long tick = (long)((ImGui::GetTime() - started) * 60);
        shown = Air_ElemAt(act, tick, &progress);
        g_animTick = (int)tick;
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
    float left = height - (ImGui::GetCursorPosY() - top); // what its toolbar, on one or more lines, leaves
    bool open = ImGui::BeginChild("preview", ImVec2(0, height > 0 ? left : em * 14),
                                  ImGuiChildFlags_Borders | (height > 0 ? 0 : ImGuiChildFlags_ResizeY),
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNavInputs);
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
    shown = std::clamp(shown, 0, (int)act.elems.size() - 1);
    const AirElem &el = act.elems[shown];
    // The ground, through the axis
    draw->AddLine(ImVec2(p0.x, axis.y), ImVec2(p0.x + size.x, axis.y), IM_COL32(255, 230, 80, 60));
    // The onion skin: the elements before and after, faint
    if (!playing && act.elems.size() > 1) {
        int before = OnionElem(act, shown, -1), after = OnionElem(act, shown, 1);
        auto faint = [&](int index, ImU32 tint) {
            const AirElem &o = act.elems[index];
            int oi = g_onionSprite && spf ? Sff_FindSprite(spf->doc, o.group, o.image) : -1;
            if (GLuint tex = oi >= 0 ? SpriteTexture(*spf, oi) : 0) {
                const SffSprite &s = spf->doc.sprites[oi];
                draw->AddImage(TexRef(tex), at((float)(o.x - s.x), (float)(o.y - s.y)),
                               at((float)(o.x - s.x + s.w), (float)(o.y - s.y + s.h)), ImVec2(0, 0), ImVec2(1, 1), tint);
            }
            if (boxes && g_onion)
                for (int g = 0; g < 2; g++)
                    for (const AirBox &b : o.clsn[g])
                        draw->AddRect(at((float)b.x1, (float)b.y1), at((float)b.x2, (float)b.y2),
                                      g == 0 ? IM_COL32(255, 120, 120, 90) : IM_COL32(140, 180, 255, 90));
        };
        if (g_onionAfter && after != shown && !(g_onionBefore && after == before)) faint(after, kOnionAfter);
        if (g_onionBefore && before != shown) faint(before, kOnionBefore);
    }
    // The element kept as a ghost, from any action
    if (g_animGhost.on && spf && t.sf) {
        const AirFile &air = AirOf(*t.sf);
        auto it = air.actions.find(g_animGhost.action);
        if (it != air.actions.end() && g_animGhost.elem < (int)it->second.elems.size()) {
            const AirElem &o = it->second.elems[g_animGhost.elem];
            int oi = Sff_FindSprite(spf->doc, o.group, o.image);
            if (GLuint tex = oi >= 0 ? SpriteTexture(*spf, oi) : 0) {
                const SffSprite &s = spf->doc.sprites[oi];
                draw->AddImage(TexRef(tex), at((float)(o.x - s.x), (float)(o.y - s.y)),
                               at((float)(o.x - s.x + s.w), (float)(o.y - s.y + s.h)), ImVec2(0, 0), ImVec2(1, 1), kOnionAfter);
            }
        }
    }
    int si = spf ? Sff_FindSprite(spf->doc, el.group, el.image) : -1;
    if (si >= 0) {
        const SffSprite &s = spf->doc.sprites[si];
        if (GLuint tex = SpriteTexture(*spf, si)) {
            // As the game draws it: flip (which mirrors the offset too), scale and angle around the sprite's axis,
            // then the blending
            AirStyle st = el.style;
            float ex = (float)el.x, ey = (float)el.y;
            // Playing: on the way to an element that interpolates (the loop start after the last), as the game
            if (progress > 0) {
                int nextIndex = shown + 1 < (int)act.elems.size() ? shown + 1 : std::max(0, act.loopStart);
                const AirElem &nx = act.elems[nextIndex];
                if (nx.interp & kAirInterpOffset) ex += (nx.x - el.x) * progress, ey += (nx.y - el.y) * progress;
                if (nx.interp & kAirInterpScale) st.xscale += (nx.style.xscale - st.xscale) * progress, st.yscale += (nx.style.yscale - st.yscale) * progress;
                if (nx.interp & kAirInterpAngle) st.angle += (nx.style.angle - st.angle) * progress;
                if ((nx.interp & kAirInterpBlend) && st.blend != kAirOpaque) {
                    st.src += (int)((nx.style.src - st.src) * progress);
                    st.dst += (int)((nx.style.dst - st.dst) * progress);
                }
            }
            DrawStyledSprite(draw, tex, s, st, ex, ey, at);
        }
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
        static bool moving; // the sprite dragged: its offset when the drag began, the one written last
        static ImVec2 moveStart;
        static int moveX, moveY, setX, setY;
        if (g_boxDraw) {
            // A new box, drawn with the mouse; the right button stops
            static bool rubber;
            static ImVec2 from;
            drag.active = false;
            ImVec2 m = toAir(ImGui::GetMousePos());
            ImU32 col = g_boxDraw == 1 ? IM_COL32(255, 70, 70, 255) : IM_COL32(80, 140, 255, 255);
            for (int g = 1; g >= 0; g--)
                for (const AirBox &b : el.clsn[g])
                    draw->AddRect(at((float)b.x1, (float)b.y1), at((float)b.x2, (float)b.y2), g ? IM_COL32(80, 140, 255, 200) : IM_COL32(255, 70, 70, 200));
            if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) rubber = true, from = m;
            if (rubber) {
                draw->AddRectFilled(at(from.x, from.y), at(m.x, m.y), (col & 0x00FFFFFF) | 0x40000000);
                draw->AddRect(at(from.x, from.y), at(m.x, m.y), col);
                if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    rubber = false;
                    AirBox b;
                    b.x1 = (int)lroundf(std::min(from.x, m.x)), b.x2 = (int)lroundf(std::max(from.x, m.x));
                    b.y1 = (int)lroundf(std::min(from.y, m.y)), b.y2 = (int)lroundf(std::max(from.y, m.y));
                    if (b.x2 - b.x1 >= 2 && b.y2 - b.y1 >= 2) {
                        AddBox(t, g_boxDraw - 1, &b);
                        changed = true;
                    }
                }
            }
        } else if (g_animDrag == 1) {
            drag.active = false;
            ImVec2 m = ImGui::GetMousePos();
            if (!moving && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                moving = true;
                moveStart = toAir(m);
                moveX = setX = el.x, moveY = setY = el.y;
            }
            if (moving && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) moving = false;
            if (moving) {
                ImVec2 a = toAir(m);
                int nx = moveX + (int)lroundf(a.x - moveStart.x), ny = moveY + (int)lroundf(a.y - moveStart.y);
                if ((nx != setX || ny != setY) && Air_SetElem(t.sf->text, el, el.group, el.image, nx, ny, el.time)) {
                    ApplyAnims(*t.sf);
                    setX = nx, setY = ny;
                }
                char label[48];
                snprintf(label, sizeof(label), "offset %d, %d", setX, setY);
                draw->AddText(ImVec2(m.x + em, m.y + em), IM_COL32(255, 255, 255, 255), label);
            }
            if (moving || hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
            for (int g = 1; g >= 0; g--)
                for (const AirBox &b : el.clsn[g])
                    draw->AddRect(at((float)b.x1, (float)b.y1), at((float)b.x2, (float)b.y2),
                                  g ? IM_COL32(80, 140, 255, 200) : IM_COL32(255, 70, 70, 200));
        } else {
            moving = false;
            BoxHandles(drag, draw, hovered, t, at, toAir, false, &hit);
        }
        // The arrow keys, with the preview in focus: the sprite, or the box under the mouse, a pixel
        // (Shift: 10) that way
        if (ImGui::IsWindowFocused() && !drag.active && !moving) {
            int step = io.KeyShift ? 10 : 1, dx = 0, dy = 0;
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) dx = -step;
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) dx = step;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) dy = -step;
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) dy = step;
            bool moved = false;
            if ((dx || dy) && g_animDrag == 1) {
                moved = Air_SetElem(t.sf->text, el, el.group, el.image, el.x + dx, el.y + dy, el.time);
            } else if ((dx || dy) && hit >= 0) {
                const AirBox &b = el.clsn[hit / 1000][hit % 1000];
                moved = Air_SetBox(t.sf->text, b, b.x1 + dx, b.y1 + dy, b.x2 + dx, b.y2 + dy);
            }
            if (moved) ApplyAnims(*t.sf);
        }
        static int menuHit;
        static ImVec2 menuAt;
        if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
            menuHit = hit, menuAt = toAir(ImGui::GetMousePos());
            if (g_boxDraw) g_boxDraw = 0;
            else ImGui::OpenPopup("boxes");
        }
        auto exists = [&](int h) { return h >= 0 && h % 1000 < (int)el.clsn[h / 1000].size(); };
        if (ImGui::BeginPopup("boxes")) {
            if (exists(menuHit)) {
                if (ImGui::MenuItem(menuHit / 1000 ? "Delete this hurt box" : "Delete this hit box", "Delete")) {
                    DeleteBox(t, menuHit / 1000, menuHit % 1000);
                    changed = true;
                }
                ImGui::Separator();
            }
            for (int g = 1; g >= 0; g--)
                if (ImGui::MenuItem(g ? "Add a hurt box here" : "Add a hit box here")) {
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
    if (g_animFollow && sa.action >= 0) {
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
    static int picksOf = -1; // the action the selection is in
    if (picksOf != g_animAction) picksOf = g_animAction, g_elemPicks.clear();
    while (!g_elemPicks.empty() && *g_elemPicks.rbegin() > (int)t.act->elems.size()) g_elemPicks.erase(*g_elemPicks.rbegin());
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
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                // Alone; with Ctrl, one more or one less; with Shift, those from the one shown to it
                ImGuiIO &io = ImGui::GetIO();
                if (io.KeyShift) {
                    g_elemPicks.clear();
                    for (int k = std::min(n, t.elem); k <= std::max(n, t.elem); k++) g_elemPicks.insert(k);
                } else if (io.KeyCtrl) {
                    if (g_elemPicks.empty()) g_elemPicks.insert(t.elem);
                    if (!g_elemPicks.erase(n)) g_elemPicks.insert(n), clicked = n;
                    else if (n == t.elem && !g_elemPicks.empty()) clicked = *g_elemPicks.begin();
                    if (g_elemPicks.size() == 1) g_elemPicks.clear();
                } else {
                    g_elemPicks.clear();
                    clicked = n;
                }
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                *menu = n;
                if (!g_elemPicks.count(n)) g_elemPicks.clear();
            }
            bool cur = n == t.elem, hot = ImGui::IsItemHovered(), picked = g_elemPicks.count(n) != 0;
            ImVec2 q(p.x + cell.x, p.y + cell.y);
            draw->AddRectFilled(p, q, ImGui::GetColorU32(cur || picked ? ImGuiCol_Header : hot ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg),
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
                ImGui::TextDisabled("Ctrl or Shift+click: select several. Right click: duplicate, delete, move, loop from here");
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

// The action along its ticks: each element as wide as its time (one for ever, as 30), the loop start
// marked, the tick playing. A click or a drag goes to an element; returns it, 0 none.
static int TickBar(const AnimTarget &t) {
    const AirAction &act = *t.act;
    float em = ImGui::GetFontSize();
    auto ticks = [](const AirElem &e) { return e.time < 0 ? 30 : std::max(1, e.time); };
    int total = 0, sum = 0;
    bool endless = false;
    for (const AirElem &e : act.elems) {
        total += ticks(e), sum += std::max(0, e.time);
        endless = endless || e.time < 0;
    }
    float w = std::max(ImGui::GetContentRegionAvail().x, 1.0f), h = em * 0.85f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("ticks", ImVec2(w, h));
    bool active = ImGui::IsItemActive(), hot = ImGui::IsItemHovered();
    float mx = ImGui::GetMousePos().x;
    ImDrawList *draw = ImGui::GetWindowDrawList();
    int clicked = 0, at = 0;
    for (int n = 0; n < (int)act.elems.size(); n++) {
        const AirElem &e = act.elems[n];
        float x0 = p.x + w * at / total, x1 = p.x + w * (at + ticks(e)) / total;
        bool cur = n + 1 == t.elem, inside = mx >= x0 && (mx < x1 || n + 1 == (int)act.elems.size());
        ImU32 fill = ImGui::GetColorU32(cur ? ImGuiCol_CheckMark : n % 2 ? ImGuiCol_FrameBg : ImGuiCol_FrameBgHovered);
        draw->AddRectFilled(ImVec2(x0, p.y), ImVec2(x1, p.y + h), fill);
        if (e.interp) draw->AddRectFilled(ImVec2(x0, p.y + h * 0.7f), ImVec2(x1, p.y + h), ImGui::GetColorU32(Ink(kOrange)));
        if (act.loopStart == n) draw->AddLine(ImVec2(x0, p.y), ImVec2(x0, p.y + h), ImGui::GetColorU32(Ink(kYellow)), 2.0f);
        if (active && inside && !cur) clicked = n + 1;
        if (hot && inside) {
            if (e.time < 0) ImGui::SetTooltip("Element %d: from tick %d, for ever", n + 1, at);
            else ImGui::SetTooltip("Element %d: ticks %d to %d of %d%s", n + 1, at, at + e.time, sum, endless ? " and for ever" : "");
        }
        at += ticks(e);
    }
    if (g_animTick >= 0) {
        float x = p.x + w * std::min(g_animTick, total) / total;
        draw->AddLine(ImVec2(x, p.y - 1), ImVec2(x, p.y + h + 1), IM_COL32(255, 255, 255, 255), 2.0f);
    }
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
    std::vector<int> group = PickedElems(t);
    bool several = group.size() > 1;
    char title[64];
    if (several) snprintf(title, sizeof(title), "Element %d of %d, %d selected", t.elem, count, (int)group.size());
    else snprintf(title, sizeof(title), "Element %d of %d", t.elem, count);
    ImGui::SeparatorText(title);
    if (ImGui::SmallButton(ICON_COPY " Duplicate") && Air_DuplicateElems(sf.text, act, group)) goTo = group.back() + 2;
    flow("Delete");
    ImGui::BeginDisabled(count <= (int)group.size());
    if (ImGui::SmallButton(ICON_TRASH " Delete") && Air_DeleteElems(sf.text, act, group)) goTo = std::max(1, group[0]);
    ImGui::EndDisabled();
    flow("<");
    ImGui::BeginDisabled(index == 0 || several);
    if (ImGui::SmallButton("<") && Air_MoveElem(sf.text, act, index, -1)) goTo = t.elem - 1;
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Move it earlier");
    ImGui::SameLine(0, 2);
    ImGui::BeginDisabled(index == count - 1 || several);
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
    float pickW = ImGui::CalcTextSize(ICON_IMAGE " Pick...").x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x;
    FieldLabel("Sprite");
    ImGui::SetNextItemWidth(std::max(em * 5, ImGui::GetContentRegionAvail().x - pickW));
    if (ImGui::DragInt2("##sprite", &v[0], 0.2f)) Air_SetElem(sf.text, el, v[0], v[1], v[2], v[3], v[4]);
    done |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SetItemTooltip("Group, image");
    ImGui::SameLine();
    if (ImGui::Button(ICON_IMAGE " Pick...")) ImGui::OpenPopup("pick");
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

    // Flip, blending, scale and angle: the fields after the time, and how it comes from the element
    // before. Most elements use none of them, so they stay closed, the header saying which this one uses
    AirStyle st = el.style;
    std::string used;
    auto uses = [&](bool on, const char *what) {
        if (on) used += (used.empty() ? "" : ", ") + std::string(what);
    };
    uses(st.flipH || st.flipV, "flip");
    uses(st.blend == kAirAdd, "add");
    uses(st.blend == kAirSub || st.blend == kAirSubAdd, "subtract");
    uses(st.xscale != 1 || st.yscale != 1, "scale");
    uses(st.angle != 0, "angle");
    uses(el.interp != 0, "smooth");
    std::string header = "Effects" + (used.empty() ? "" : "  (" + used + ")") + "###effects";
    bool effects = ImGui::CollapsingHeader(header.c_str());
    ImGui::SetItemTooltip("Flip, blending, scale, angle, and changing smoothly from the element before");
    bool styled = false;
    if (effects) {
        FieldLabel("Flip");
        styled |= ImGui::Checkbox("H", &st.flipH);
        ImGui::SetItemTooltip("Mirrors the sprite around its axis (the x offset is mirrored too, as the game does)");
        ImGui::SameLine();
        styled |= ImGui::Checkbox("V", &st.flipV);
        ImGui::SetItemTooltip("Turns the sprite upside down (the y offset is mirrored too)");
        FieldLabel("Blend");
        const char *blends[] = {"Opaque", "Add", "Subtract", "Sub-add"};
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::Combo("##blend", &st.blend, blends, 4)) {
            if (st.blend == kAirAdd && !el.style.blend) st.src = 255, st.dst = 255;
            styled = true;
        }
        ImGui::SetItemTooltip("A, S or SA in the AIR; with alphas, AS, SS or SAS");
        if (st.blend != kAirOpaque) {
            FieldLabel("Alpha");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            int al[2] = {st.src, st.dst};
            if (ImGui::DragInt2("##alpha", al, 0.5f, 0, 255)) st.src = al[0], st.dst = al[1], styled = true;
            done |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SetItemTooltip("Source, destination (0 to 255): the sprite times the first plus (or minus) the background times the second.\n"
                                  "255, 255 is a plain A or S; 255, 128 is A1.");
        }
        FieldLabel("Scale");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        float sc[2] = {st.xscale, st.yscale};
        if (ImGui::DragFloat2("##scale", sc, 0.01f, -16.0f, 16.0f, "%.2f")) st.xscale = sc[0], st.yscale = sc[1], styled = true;
        done |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetItemTooltip("x, y; 1 is the sprite's size. Scaled around the sprite's axis.");
        FieldLabel("Angle");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::DragFloat("##angle", &st.angle, 0.5f, -360.0f, 360.0f, "%.1f deg")) styled = true;
        done |= ImGui::IsItemDeactivatedAfterEdit();
    }
    if (styled) {
        Air_SetStyle(sf.text, el, st);
        if (!ImGui::IsAnyItemActive()) done = true; // a click on a box or the combo is one edit
    }
    if (done) apply(sf);

    // Interpolation: what changes smoothly on the way from the element before to this one
    if (effects) {
        ImGui::TextDisabled("Smooth from the element before:");
        static const struct {
            const char *label;
            int flag;
        } kInterp[] = {{"offset", kAirInterpOffset}, {"scale", kAirInterpScale}, {"angle", kAirInterpAngle}, {"alpha", kAirInterpBlend}};
        for (int k = 0; k < 4; k++) {
            if (k) SameLineFits(CheckboxWidth(kInterp[k].label));
            bool on = (el.interp & kInterp[k].flag) != 0;
            if (ImGui::Checkbox(kInterp[k].label, &on) && Air_SetInterpolate(sf.text, el, el.interp ^ kInterp[k].flag)) {
                goTo = t.elem;
                return true;
            }
        }
    }

    if (several) {
        ImGui::SeparatorText("The elements selected");
        ImGui::TextDisabled("Give them this element's");
        enum { Offset, Time, Flip, Blend, Scale, Angle };
        const char *fields[] = {"Offset", "Time", "Flip", "Blend", "Scale", "Angle"};
        for (int f = 0; f < 6; f++) {
            if (f) flow(fields[f]);
            if (!ImGui::SmallButton(fields[f])) continue;
            for (int i : group) {
                const AirElem &e = act.elems[i];
                if (i == index) continue;
                if (f == Offset || f == Time) {
                    Air_SetElem(sf.text, e, e.group, e.image, f == Offset ? el.x : e.x, f == Offset ? el.y : e.y, f == Time ? el.time : e.time);
                    continue;
                }
                AirStyle s = e.style;
                if (f == Flip) s.flipH = el.style.flipH, s.flipV = el.style.flipV;
                if (f == Blend) s.blend = el.style.blend, s.src = el.style.src, s.dst = el.style.dst;
                if (f == Scale) s.xscale = el.style.xscale, s.yscale = el.style.yscale;
                if (f == Angle) s.angle = el.style.angle;
                Air_SetStyle(sf.text, e, s);
            }
            apply(sf);
            return true;
        }
        static int by[2];
        ImGui::SetNextItemWidth(std::min(em * 7, ImGui::GetContentRegionAvail().x));
        ImGui::InputInt2("##by", by);
        ImGui::SetItemTooltip("x, y: how far they move");
        for (int boxes = 0; boxes < 2; boxes++) {
            const char *label = boxes ? "Move boxes" : "Move offsets";
            if (boxes) flow(label);
            else ImGui::SameLine();
            if (!ImGui::SmallButton(label)) continue;
            std::set<int> done; // boxes elements share, once
            for (int i : group) {
                const AirElem &e = act.elems[i];
                if (!boxes) Air_SetElem(sf.text, e, e.group, e.image, e.x + by[0], e.y + by[1], e.time);
                for (int g = 0; boxes && g < 2; g++)
                    for (const AirBox &b : e.clsn[g])
                        if (done.insert(b.line).second) Air_SetBox(sf.text, b, b.x1 + by[0], b.y1 + by[1], b.x2 + by[0], b.y2 + by[1]);
            }
            apply(sf);
            return true;
        }
    }
    SpriteGridSize();
    if (ImGui::BeginPopup("pick")) {
        static int group = 0;
        if (ImGui::IsWindowAppearing()) group = el.group;
        int g, n;
        if (SpriteGrid(spf, group, el.group, el.image, &g, &n) && Air_SetElem(sf.text, el, g, n, el.x, el.y, el.time)) {
            apply(sf);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Line %d", el.line);
    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_CODE " Show in Code")) CodeShowLine(sf.path, el.line);

    // The boxes: Clsn2, then Clsn1
    for (int g = 1; g >= 0 && !screen; g--) {
        ImGui::PushID(g);
        ImGui::Spacing();
        ImGui::SeparatorText(g == 1 ? "Hurt boxes: can be hit (Clsn2)" : "Hit boxes: hit (Clsn1)");
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
            bool del = ImGui::Button(ICON_XMARK, ImVec2(delW, 0));
            ImGui::SetItemTooltip("Delete this box (the element gets its own boxes)");
            ImGui::PopID();
            if (del) {
                DeleteBox(t, g, (int)i);
                ImGui::PopID();
                return true;
            }
        }
        float moreW = ImGui::GetFrameHeight() * 1.2f;
        if (ImGui::Button(ICON_PLUS " Add a box", ImVec2(ImGui::GetContentRegionAvail().x - moreW - ImGui::GetStyle().ItemSpacing.x, 0))) {
            AddBox(t, g);
            ImGui::PopID();
            return true;
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_ELLIPSIS, ImVec2(moreW, 0))) ImGui::OpenPopup("more");
        ImGui::SetItemTooltip("Copy, paste, more elements");
        int tool = 0;
        if (ImGui::BeginPopup("more")) {
            enum { Paste = 1, FromBefore, SameSprite, Swap, DeleteHere, DeleteAction };
            if (ImGui::MenuItem(ICON_COPY " Copy")) g_boxClip[g] = el.clsn[g];
            if (ImGui::MenuItem("Paste", nullptr, false, !g_boxClip[g].empty())) tool = Paste;
            if (ImGui::MenuItem("As in the element before", nullptr, false, index > 0)) tool = FromBefore;
            ImGui::Separator();
            if (ImGui::MenuItem("On every element with this sprite")) tool = SameSprite;
            ImGui::SetItemTooltip("These boxes on all the elements showing this sprite, in every action");
            if (ImGui::MenuItem(g ? "Swap with the hit boxes" : "Swap with the hurt boxes")) tool = Swap;
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_TRASH " Delete them")) tool = DeleteHere;
            if (ImGui::MenuItem("Delete them in the whole action")) tool = DeleteAction;
            ImGui::EndPopup();
            if (tool) {
                std::vector<AirBox> boxes = el.clsn[g];
                auto sprite = [&](const AirElem &e) { return e.group == el.group && e.image == el.image; };
                auto whole = [&](const AirElem &) { return true; };
                if (tool == Paste) Air_SetOwnBoxes(sf.text, el, g, g_boxClip[g]);
                if (tool == FromBefore) Air_SetOwnBoxes(sf.text, el, g, act.elems[index - 1].clsn[g]);
                if (tool == DeleteHere) Air_SetOwnBoxes(sf.text, el, g, {});
                if (tool == SameSprite) SetBoxesWhere(sf, -1, g, boxes, sprite);
                if (tool == DeleteAction) SetBoxesWhere(sf, act.no, g, {}, whole);
                if (tool == Swap) {
                    int no = act.no;
                    std::vector<AirBox> other = el.clsn[1 - g];
                    Air_SetOwnBoxes(sf.text, el, g, other); // the lines change: the other group from a new reading
                    AirFile again;
                    Air_Parse(sf.text, again);
                    Air_SetOwnBoxes(sf.text, again.actions[no].elems[index], 1 - g, boxes);
                }
                apply(sf);
                ImGui::PopID();
                return true;
            }
        }
        ImGui::PopID();
    }
    return false;
}

// An action as a GIF file: each element at its time, flipped, scaled and turned as the game does,
// on a transparent ground; scale: pixels for each pixel of the game.
static bool ActionGif(const AirAction &act, SpriteFile &spf, int scale, const std::string &path, std::string *error) {
    struct Item {
        const AirElem *e;
        const SffSprite *s;
        std::vector<unsigned char> rgba;
    };
    std::vector<Item> items;
    float minX = 0, minY = 0, maxX = 1, maxY = 1;
    auto mapped = [](const AirElem &e, const SffSprite &s, float cx, float cy, float &x, float &y) {
        const AirStyle &st = e.style;
        float fx = st.flipH ? -1.0f : 1.0f, fy = st.flipV ? -1.0f : 1.0f;
        float rad = st.angle * 3.14159265f / 180, cs = cosf(rad), sn = sinf(rad);
        float px = (cx - s.x) * st.xscale * fx, py = (cy - s.y) * st.yscale * fy;
        x = e.x * fx + px * cs + py * sn, y = e.y * fy - px * sn + py * cs;
    };
    for (const AirElem &e : act.elems) {
        if (e.time == 0) continue;
        int si = Sff_FindSprite(spf.doc, e.group, e.image);
        if (si < 0) continue;
        Item it{&e, &spf.doc.sprites[si], {}};
        std::vector<unsigned char> px;
        int bpp = 0;
        if (!Sff_Decode(*it.s, px, &bpp)) continue;
        Sff_ToRgba(spf.doc, *it.s, px, bpp, it.rgba);
        for (int c = 0; c < 4; c++) {
            float x, y;
            mapped(e, *it.s, c & 1 ? (float)it.s->w : 0, c & 2 ? (float)it.s->h : 0, x, y);
            minX = std::min(minX, x), maxX = std::max(maxX, x);
            minY = std::min(minY, y), maxY = std::max(maxY, y);
        }
        items.push_back(std::move(it));
    }
    if (items.empty()) {
        *error = "The action has no sprite to show";
        return false;
    }
    int w = (int)ceilf((maxX - minX) * scale), h = (int)ceilf((maxY - minY) * scale);
    std::vector<GifFrame> frames;
    double at = 0; // hundredths of a second, so that the rounding does not add up
    for (const Item &it : items) {
        const AirElem &e = *it.e;
        const AirStyle &st = e.style;
        GifFrame f;
        f.rgba.assign((size_t)w * h * 4, 0);
        float fx = st.flipH ? -1.0f : 1.0f, fy = st.flipV ? -1.0f : 1.0f;
        float rad = st.angle * 3.14159265f / 180, cs = cosf(rad), sn = sinf(rad);
        float sx = st.xscale * fx, sy = st.yscale * fy;
        for (int y = 0; y < h && sx != 0 && sy != 0; y++)
            for (int x = 0; x < w; x++) {
                // Back from the canvas to the sprite
                float dx = minX + (x + 0.5f) / scale - e.x * fx, dy = minY + (y + 0.5f) / scale - e.y * fy;
                int ix = (int)floorf((dx * cs - dy * sn) / sx + it.s->x), iy = (int)floorf((dx * sn + dy * cs) / sy + it.s->y);
                if (ix < 0 || iy < 0 || ix >= it.s->w || iy >= it.s->h) continue;
                memcpy(&f.rgba[((size_t)y * w + x) * 4], &it.rgba[((size_t)iy * it.s->w + ix) * 4], 4);
            }
        double end = at + (e.time < 0 ? 60 : e.time) * 100.0 / 60;
        f.delay = std::max(2, (int)lround(end) - (int)lround(at));
        at = end;
        frames.push_back(std::move(f));
    }
    std::ofstream out(path, std::ios::binary);
    out << Gif_Build(w, h, frames);
    out.close();
    if (!out) *error = "Could not write " + path;
    return (bool)out;
}

// "200-220, 300": does the list name that number? Empty: all.
static bool InNumberList(int no, const std::string &list) {
    if (list.find_first_not_of(" ,") == std::string::npos) return true;
    for (size_t at = 0; at < list.size();) {
        size_t end = std::min(list.find(',', at), list.size());
        std::string part = list.substr(at, end - at);
        int a = 0, b = 0;
        int n = sscanf(part.c_str(), "%d - %d", &a, &b);
        if ((n == 1 && no == a) || (n == 2 && no >= a && no <= b)) return true;
        at = end + 1;
    }
    return false;
}

bool AnimFromSprites(int pn, const std::string &file, int no, int time, const std::vector<std::pair<int, int>> &sprites, bool append) {
    std::string path = pn > 0 ? AirPath(pn) : file;
    if (path.empty() || sprites.empty()) return false;
    SourceFile &sf = LoadSource(path);
    std::vector<AirNewElem> elems;
    for (const auto &s : sprites) {
        AirNewElem e;
        e.group = s.first, e.image = s.second, e.time = time;
        elems.push_back(e);
    }
    const AirFile &air = AirOf(sf);
    auto it = air.actions.find(no);
    bool ok;
    if (it != air.actions.end() && append) {
        ok = Air_InsertElems(sf.text, it->second, (int)it->second.elems.size() - 1, elems);
        g_animElem = (int)it->second.elems.size() + 1;
    } else {
        ok = it == air.actions.end() && Air_NewAction(sf.text, no, "", elems);
        g_animElem = 1;
    }
    if (!ok) return false;
    g_animAction = no, g_animFollow = false;
    ApplyAnims(sf);
    if (pn > 0) ShowWindow("Animation");
    else AnimOpenFile(path, no, g_animElem);
    return true;
}

std::string AnimFileOfSprites(const std::string &sff) {
    if (!g_animFile.empty() && NormPath(SpritesFileOf(g_animFile)) == NormPath(sff)) return g_animFile;
    return ScreenDefOfSprites(sff);
}

void AnimationWindow() {
    if (!BeginTool("Animation")) return;
    // What it shows: the project's animation, that of the screen's element picked last, or
    // a file by itself, whichever came last
    enum { Player, Element, File };
    static int source = Player;
    static int seenSel = -2, seenSelPicked = 0, seenPicked = 0, seenFile = 0;
    static std::string seenWork;
    if (g_selId != seenSel || g_selPicked != seenSelPicked) {
        seenSel = g_selId, seenSelPicked = g_selPicked;
        if (SelectedOfProject()) source = Player;
    }
    if (std::string work = WorkDef(); work != seenWork) seenWork = work, source = Player;
    if (g_screenAnimPicked != seenPicked) seenPicked = g_screenAnimPicked, source = Element, g_animFollow = true;
    if (g_animFileN != seenFile) seenFile = g_animFileN, source = File;
    ScreenAnim sa;
    bool screen = source == Element ? SelectedScreenAnim(sa) : source == File && FileAnim(sa);
    AnimTarget t = screen ? ScreenAnimTarget(sa) : GetAnimTarget();
    // An edit: the game shows it (a character's animations, or the screen's def)
    std::function<void(SourceFile &)> apply = [&](SourceFile &s) {
        if (screen) {
            sa.apply();
        } else {
            ApplyAnims(s);
            if (!g_paused) g_animFollow = false; // what was edited stays shown, not what the game goes on to
        }
    };
    bool disk = !screen && t.pn == kDiskPn; // a character not in the match, from disk: no game to show it
    if (!screen && !t.e && !disk) {
        ProjectKind kind = CurrentProject();
        EmptyNote(kind == kProjectStage        ? "Click an animated background on the game, or pick it in the Stage editor: its animation shows here. "
                                                 "Add layer makes one. A right click in Explorer opens any def's animations."
                  : kind == kProjectScreenpack ? "Click an animated element on the game, or pick it in the Screenpack panel: its animation shows here. "
                                                 "A right click in Explorer opens any def's animations."
                                               : "Open a character (at the left of the toolbar, or in Explorer). A right click in Explorer opens any def's animations.");
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
    if (screen && sa.disk) {
        ImGui::TextColored(Ink(kOrange), "from disk");
        ImGui::SetItemTooltip("Not what the game is showing: edited from disk. Save writes it.");
    } else if (screen) {
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
        ImGui::SameLine();
        if (ImGui::SmallButton(Connected() ? ICON_PLAY " Put in the match" : ICON_PLAY " Launch with it")) PlayCharacter(CharFile(kDiskPn, "def"));
        ImGui::SetItemTooltip(Connected() ? "The match starts again with it as P1." : "The game starts with it as P1.");
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
        ImGui::TextDisabled("· %s", sf.path.c_str());
        float w = ImGui::CalcTextSize(ICON_SAVE " Save").x + ImGui::CalcTextSize(ICON_REVERT " Revert").x + style.FramePadding.x * 4 +
                  style.ItemSpacing.x * 2 + UndoButtonsWidth();
        SaveState(sf.Dirty(), !disk && !(screen && sa.disk), w);
        if (t.act && !t.act->name.empty()) {
            ImGui::SameLine();
            // Without room it goes, not the buttons to another line: the panel would jump on the first edit
            if (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(t.act->name.c_str()).x - em * 2 >= w)
                ImGui::TextDisabled("· %s", t.act->name.c_str());
        }
        rightAlign(w);
        UndoStack<std::string> &history = TrackAnims(sf);
        if (int step = UndoButtons(history.CanUndo(), history.CanRedo(), true)) {
            sf.text = step < 0 ? history.Undo() : history.Redo();
            apply(sf);
            KeepActionShown(sf);
            g_elemPicks.clear();
            ImGui::End(); // what was parsed is old
            return;
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!sf.Dirty());
        if (sf.Dirty()) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        bool save = ImGui::Button(ICON_SAVE " Save");
        if (sf.Dirty()) ImGui::PopStyleColor();
        if (save) {
            SaveSource(sf);
            ScreenFileSaved(sf.path);
        }
        ImGui::SetItemTooltip(screen ? "Writes the file" : "Writes the AIR file");
        ImGui::SameLine();
        if (ImGui::Button(ICON_REVERT " Revert")) {
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
    // Every action of the file in a list that can be searched by number: the arrows are for neighbours only
    ImGui::SameLine(0, 2);
    ImGui::SetNextWindowSizeConstraints(ImVec2(em * 16, 0), ImVec2(em * 24, em * 26));
    if (ImGui::BeginCombo("##actlist", "", ImGuiComboFlags_NoPreview)) {
        static char filter[32];
        if (ImGui::IsWindowAppearing()) {
            filter[0] = 0;
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        bool enter = ImGui::InputTextWithHint("##actfilter", "Number or name...", filter, sizeof(filter), ImGuiInputTextFlags_EnterReturnsTrue);
        auto named = [&](const std::string &name) {
            return std::search(name.begin(), name.end(), filter, filter + strlen(filter),
                               [](char x, char y) { return tolower((unsigned char)x) == tolower((unsigned char)y); }) != name.end();
        };
        std::vector<const AirAction *> shown;
        for (const auto &[no, a] : air.actions)
            if (!filter[0] || std::to_string(no).find(filter) != std::string::npos || named(a.name)) shown.push_back(&a);
        int pick = -1;
        if (enter && !shown.empty()) pick = shown[0]->no;
        if (ImGui::BeginChild("##actrows", ImVec2(0, std::min<float>(shown.size(), 14) * ImGui::GetTextLineHeightWithSpacing() + 2))) {
            ImGuiListClipper clip;
            clip.Begin((int)shown.size());
            for (int i = 0; i < (int)shown.size(); i++)
                if (shown[i]->no == g_animAction) clip.IncludeItemByIndex(i); // so the scroll below finds it
            while (clip.Step())
                for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++) {
                    const AirAction &a = *shown[i];
                    int ticks = 0;
                    for (const AirElem &e : a.elems) ticks = e.time < 0 || ticks < 0 ? -1 : ticks + e.time;
                    char row[128];
                    if (ticks < 0) snprintf(row, sizeof(row), "%d  %s  %d el.  inf.", a.no, a.name.c_str(), (int)a.elems.size());
                    else snprintf(row, sizeof(row), "%d  %s  %d el.  %d ticks", a.no, a.name.c_str(), (int)a.elems.size(), ticks);
                    bool cur = a.no == g_animAction;
                    if (ImGui::Selectable(row, cur)) pick = a.no;
                    if (cur && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY();
                }
            clip.End();
        }
        ImGui::EndChild();
        if (pick >= 0) {
            g_animAction = pick, g_animElem = 1, g_animFollow = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Every action of the file, by number or name");
    ImGui::SameLine(0, em);
    ImGui::BeginDisabled(disk || (screen && sa.action < 0));
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
        HelpMarker(screen ? "The animations of a def ([Begin Action]) or of an AIR file. Every change to a def the game is "
                            "using goes to it at once (the def is loaded again); Save writes the file, which its window edits too."
                          : "The project is not in the match: its AIR file edited from disk, the action drawn below. Save "
                            "writes it; put the character in the match to see it on the game.");
    }
    // A def's fields naming the action
    if (screen && sf.path.size() > 4 && _stricmp(sf.path.c_str() + sf.path.size() - 4, ".def") == 0) ActionUsersNote(sf, g_animAction);
    // What can be done with the action, in a menu
    SpriteFile *spfFor = screen ? sa.sprites : t.pn > 0 ? SpritesOf(t.pn) : nullptr;
    const char *want = nullptr;
    {
        rightAlign(ImGui::CalcTextSize("Action").x + style.FramePadding.x * 2 + ImGui::GetFrameHeight());
        if (ImGui::BeginCombo("##actmenu", "Action", ImGuiComboFlags_WidthFitPreview | ImGuiComboFlags_PopupAlignLeft | ImGuiComboFlags_HeightLargest)) {
            if (ImGui::Selectable(ICON_PLUS " New...")) want = "new";
            if (ImGui::Selectable("From another file...")) want = "import";
            ImGui::SetItemTooltip("Actions of an AIR file or of a def (a stage's, a screen's), with their sprites");
            ImGui::Separator();
            ImGui::BeginDisabled(!t.act);
            if (!screen && t.e && ImGui::Selectable("Show on the player")) {
                ExecOn(t.e->id, "changeAnim(" + std::to_string(g_animAction) + ")");
                g_animFollow = true;
            }
            if (!screen && t.e) ImGui::SetItemTooltip("ChangeAnim on the selected player (not on helpers); pause first to keep it.");
            if (ImGui::Selectable("Name...")) want = "name";
            if (ImGui::Selectable("Copy as...")) want = "copy";
            if (ImGui::Selectable("Renumber...")) want = "renumber";
            if (ImGui::Selectable("Scale...")) want = "scale";
            ImGui::SetItemTooltip("Its offsets and boxes multiplied: for sprites resized");
            if (ImGui::Selectable("Optimize") && t.act) {
                int removed = Air_Optimize(sf.text, *t.act);
                Log(LogLine::Info, "Optimize: " + std::to_string(removed) + " lines fewer");
                if (removed) {
                    apply(sf);
                    ImGui::EndDisabled();
                    ImGui::EndCombo();
                    ImGui::End(); // what was parsed is old
                    return;
                }
            }
            ImGui::SetItemTooltip("Boxes every element has alike become the action's default");
            if (ImGui::Selectable("Save as GIF...")) want = "gif";
            ImGui::Separator();
            if (ImGui::Selectable("Delete...")) want = "delete";
            ImGui::EndDisabled();
            ImGui::EndCombo();
        }
    }
    static int newNo;
    static std::string newName;
    static SpriteFile *gifSprites;
    static std::string importPath, importText, importSff, importWhich;
    static AirFile importAir;
    static bool importWithSprites = true, importReplace = false;
    if (want) {
        newNo = want[0] == 'c' ? g_animAction + 1 : g_animAction;
        if (want[0] == 'n' && want[1] == 'e') {
            for (newNo = g_animAction + 1; air.actions.count(newNo);) newNo++;
            newName.clear();
        }
        if (want[0] == 'n' && want[1] == 'a' && t.act) newName = t.act->name;
        if (want[0] == 'g') gifSprites = spfFor;
        if (want[0] == 'i') {
            // A file of actions and its SFF: the one a def names, else beside it, the one named alike or any
            std::vector<std::string> files = PickFiles("Animations (*.air, *.def)\0*.air;*.def\0All files (*.*)\0*.*\0", false);
            want = nullptr;
            if (!files.empty()) {
                importPath = files[0];
                importText = ReadWholeFile(importPath);
                size_t crlf;
                while ((crlf = importText.find('\r')) != std::string::npos) importText.erase(crlf, 1);
                Air_Parse(importText, importAir);
                importSff.clear();
                std::filesystem::path air2(importPath);
                std::error_code ec;
                std::string named = Ini_SpritesFile(importText);
                if (!named.empty() && std::filesystem::exists(air2.parent_path() / named, ec))
                    importSff = (air2.parent_path() / named).string();
                for (const auto &de : std::filesystem::directory_iterator(air2.parent_path(), ec)) {
                    std::string ext = de.path().extension().string();
                    if (!named.empty() && !importSff.empty()) break;
                    if (_stricmp(ext.c_str(), ".sff") != 0) continue;
                    if (de.path().stem() == air2.stem() || importSff.empty()) importSff = de.path().string();
                }
                want = "import";
            }
        }
        if (want) ImGui::OpenPopup(want);
    }
    if (ImGui::BeginPopup("new")) {
        ImGui::TextUnformatted("A new action, of one element");
        ImGui::SetNextItemWidth(em * 6);
        ImGui::InputInt("Number", &newNo, 0);
        ImGui::SetNextItemWidth(em * 12);
        ImGui::InputText("Name", &newName);
        bool taken = air.actions.count(newNo) != 0;
        if (taken) ImGui::TextColored(Ink(kRed), "Action %d exists already.", newNo);
        ImGui::BeginDisabled(taken || newNo < 0);
        if (ImGui::Button(ICON_CHECK " Create")) {
            Air_NewAction(sf.text, newNo, newName, {AirNewElem()});
            apply(sf);
            g_animAction = newNo, g_animElem = 1, g_animFollow = false;
            ImGui::CloseCurrentPopup();
            ImGui::EndDisabled();
            ImGui::EndPopup();
            ImGui::End(); // what was parsed is old
            return;
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("Sprites selected in the Sprites window can start one too.");
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("name")) {
        ImGui::SetNextItemWidth(em * 14);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        bool ok = ImGui::InputText("##name", &newName, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        ok |= ImGui::Button(ICON_CHECK " OK");
        if (ok && t.act) {
            Air_SetActionName(sf.text, *t.act, newName);
            apply(sf);
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            ImGui::End(); // what was parsed is old
            return;
        }
        ImGui::TextDisabled("A comment before the action; the game does not read it.");
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("scale")) {
        static float pct[2] = {200, 200};
        ImGui::SetNextItemWidth(em * 10);
        ImGui::InputFloat2("Width, height %", pct, "%.0f");
        bool one = ImGui::Button("This action");
        ImGui::SameLine();
        bool all = ImGui::Button("All the actions");
        if ((one && t.act) || all) {
            // Scaling keeps the lines where they are: what was parsed stays good
            if (one) Air_ScaleAction(sf.text, *t.act, pct[0] / 100, pct[1] / 100);
            for (const auto &kv : air.actions)
                if (all) Air_ScaleAction(sf.text, kv.second, pct[0] / 100, pct[1] / 100);
            apply(sf);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("gif")) {
        static int gifScale = 2;
        ImGui::SetNextItemWidth(em * 6);
        ImGui::SliderInt("Size", &gifScale, 1, 4, "%dx");
        if (ImGui::Button(ICON_DOWNLOAD " Save...") && t.act && gifSprites) {
            std::string path = PickSaveFile("GIF images (*.gif)\0*.gif\0", "gif", "action" + std::to_string(g_animAction) + ".gif"), error;
            if (!path.empty()) {
                if (ActionGif(*t.act, *gifSprites, gifScale, path, &error)) Log(LogLine::Info, "Saved " + path);
                else Log(LogLine::Error, error);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("import")) {
        std::vector<const AirAction *> chosen;
        for (const auto &kv : importAir.actions)
            if (InNumberList(kv.first, importWhich)) chosen.push_back(&kv.second);
        ImGui::Text("%s: %zu actions", importPath.c_str(), importAir.actions.size());
        ImGui::SetNextItemWidth(em * 14);
        ImGui::InputTextWithHint("Which", "200-220, 300; all", &importWhich);
        ImGui::Checkbox("Replace those with the same number", &importReplace);
        ImGui::BeginDisabled(importSff.empty());
        ImGui::Checkbox("With their sprites", &importWithSprites);
        ImGui::EndDisabled();
        ImGui::SetItemTooltip(importSff.empty() ? "No SFF file for that file" : "From %s; numbers in use are skipped", importSff.c_str());
        char label[48];
        snprintf(label, sizeof(label), ICON_DOWNLOAD " Import %zu actions", chosen.size());
        ImGui::BeginDisabled(chosen.empty());
        if (ImGui::Button(label)) {
            int done = 0, skipped = 0;
            std::set<std::pair<int, int>> wanted;
            for (const AirAction *a : chosen) {
                bool taken = air.actions.count(a->no) != 0;
                if (taken && !importReplace) {
                    skipped++;
                    continue;
                }
                for (const AirElem &e : a->elems) wanted.insert({e.group, e.image});
                if (taken) {
                    AirFile now;
                    Air_Parse(sf.text, now);
                    Air_DeleteAction(sf.text, now.actions[a->no]);
                }
                Air_CopyActionFrom(sf.text, importText, *a, a->no);
                done++;
            }
            Log(LogLine::Info, "Imported " + std::to_string(done) + " actions, " + std::to_string(skipped) + " skipped: their numbers are in use");
            SpriteFile *to = screen ? sa.sprites : SpritesOf(t.pn);
            if (importWithSprites && !importSff.empty() && to && !wanted.empty()) {
                SffDoc from;
                if (Sff_Load(from, importSff)) {
                    std::vector<int> sprites;
                    for (int i = 0; i < (int)from.sprites.size(); i++)
                        if (wanted.count({from.sprites[i].group, from.sprites[i].number})) sprites.push_back(i);
                    int same = 0, n = Sff_ImportSprites(to->doc, from, sprites, false, SFF_IMPORT_OWN, &same);
                    Log(LogLine::Info, "Imported " + std::to_string(n) + " sprites, " + std::to_string(same) + " skipped: their numbers are in use");
                    if (n) SpritesChanged(*to);
                }
            }
            apply(sf);
            ImGui::CloseCurrentPopup();
            ImGui::EndDisabled();
            ImGui::EndPopup();
            ImGui::End(); // what was parsed is old
            return;
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    for (const char *popup : {"copy", "renumber"}) {
        if (!ImGui::BeginPopup(popup)) continue;
        bool copy = popup[0] == 'c';
        ImGui::TextUnformatted(copy ? "A copy of this action, numbered" : "This action's new number");
        ImGui::SetNextItemWidth(em * 6);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        bool ok = ImGui::InputInt("##no", &newNo, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        ok |= ImGui::Button(ICON_CHECK " OK");
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
        if (ImGui::Button(ICON_TRASH " Delete") && t.act) {
            bool gone = Air_DeleteAction(sf.text, *t.act);
            if (gone) apply(sf);
            g_animFollow = false;
            ImGui::CloseCurrentPopup();
            if (gone) {
                KeepActionShown(sf);
                ImGui::EndPopup();
                ImGui::End(); // what was parsed is old
                return;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_XMARK " Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (!t.act) {
        ImGui::Spacing();
        auto listed = air.actions.find(g_animAction);
        if (listed != air.actions.end() && listed->second.copyOf >= 0) {
            ImGui::Text("Action %d is a copy of action %d (Copy Action): the game shows that one's elements.", g_animAction, listed->second.copyOf);
            if (ImGui::SmallButton("Go to it")) g_animAction = listed->second.copyOf, g_animElem = 1, g_animFollow = false;
            ImGui::End();
            return;
        }
        if (air.actions.empty())
            ImGui::TextDisabled("%s has no [Begin Action] yet: the Action menu makes one, sprites selected in Sprites too.", sf.path.c_str());
        else
            ImGui::TextDisabled("Action %d is not in %s.", g_animAction, sf.path.c_str());
        ImGui::End();
        return;
    }

    // The elements; a click shows one on the game too (pause first to keep it there)
    SpriteFile *spf = screen ? sa.sprites : SpritesOf(t.pn);
    bool live = !screen && t.fe && t.fe->action == g_animAction;
    // Sprites goes with the element picked, not with a game running through them; those picked there stay
    static std::pair<int, int> seenElem(-1, -1);
    if (t.act && spf && spf->picks.empty() && (!g_animFollow || g_paused || !live) && seenElem != std::make_pair(g_animAction, t.elem)) {
        seenElem = {g_animAction, t.elem};
        const AirElem &el = t.act->elems[t.elem - 1];
        SpritesSelect(*spf, Sff_FindSprite(spf->doc, el.group, el.image));
    }
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
    if (int tick = TickBar(t)) clicked = tick, g_elemPicks.clear();
    if (clicked || menuElem) {
        showElem(clicked ? clicked : menuElem);
        if (!live || !g_paused) g_animFollow = false;
    }
    if (menuElem) ImGui::OpenPopup("element");
    int goTo = 0; // element to show after an edit of the elements
    if (ImGui::BeginPopup("element")) {
        int index = t.elem - 1, count = (int)t.act->elems.size();
        std::vector<int> group = PickedElems(t);
        bool several = group.size() > 1;
        if (several) ImGui::TextDisabled("%d elements", (int)group.size());
        else ImGui::TextDisabled("Element %d", t.elem);
        if (ImGui::MenuItem(ICON_COPY " Duplicate") && Air_DuplicateElems(sf.text, *t.act, group)) goTo = group.back() + 2;
        if (ImGui::MenuItem(ICON_TRASH " Delete", nullptr, false, count > (int)group.size()) && Air_DeleteElems(sf.text, *t.act, group))
            goTo = std::max(1, group[0]);
        if (ImGui::MenuItem("Move earlier", nullptr, false, index > 0 && !several) && Air_MoveElem(sf.text, *t.act, index, -1))
            goTo = t.elem - 1;
        if (ImGui::MenuItem("Move later", nullptr, false, index < count - 1 && !several) && Air_MoveElem(sf.text, *t.act, index, 1))
            goTo = t.elem + 1;
        bool loopHere = t.act->loopStart == index;
        if (ImGui::MenuItem("Loop from here", nullptr, loopHere) && Air_SetLoopStart(sf.text, *t.act, loopHere ? -1 : index))
            goTo = t.elem;
        ImGui::Separator();
        if (ImGui::MenuItem("Select this one and those after")) {
            g_elemPicks.clear();
            for (int n = t.elem; n <= count; n++) g_elemPicks.insert(n);
            if (g_elemPicks.size() == 1) g_elemPicks.clear();
        }
        bool ghostHere = g_animGhost.on && g_animGhost.action == g_animAction && g_animGhost.elem == index;
        if (ImGui::MenuItem(ghostHere ? "No ghost" : "Keep as a ghost")) {
            g_animGhost.on = !ghostHere, g_animGhost.action = g_animAction, g_animGhost.elem = index;
        }
        ImGui::SetItemTooltip("This element stays faint over the preview, in any action, to compare");
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
            changed = AnimPreview(t, spf, !screen, h);
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
        g_elemPicks.clear();
    }
    ImGui::End();
}

} // namespace app
