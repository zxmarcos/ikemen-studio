// Game: the game's frames, with the players' boxes over them; keys go to the game.
#include "app_internal.h"

#include <cfloat>
#include <climits>
#include <cmath>
#include <cstring>
#include <fstream>

namespace app {

int g_trigId = -1;
std::map<int, std::vector<std::pair<int, int>>> g_trigTicks;

void ReadBoxes(const Json &j, std::vector<ImVec4> &out) {
    for (size_t n = 0; n + 3 < j.size(); n += 4)
        out.push_back(ImVec4((float)j[n].num(), (float)j[n + 1].num(), (float)j[n + 2].num(), (float)j[n + 3].num()));
}

// The game's own window: hidden while its frames are shown in Game (unless the settings say
// otherwise). Hidden, not minimized: a minimized window stops drawing.
void UpdateGameWindow() {
    static double lastCheck;
    if (ImGui::GetTime() - lastCheck < 0.5) return;
    lastCheck = ImGui::GetTime();
    struct Find {
        DWORD pid;
        HWND hwnd;
    } find = {g_bridge.Pid(), nullptr};
    EnumWindows(
        [](HWND hwnd, LPARAM param) -> BOOL {
            Find *f = (Find *)param;
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid != f->pid || GetWindow(hwnd, GW_OWNER)) return TRUE;
            char cls[64];
            GetClassNameA(hwnd, cls, sizeof(cls));
            if (strcmp(cls, "SDL_app") != 0) return TRUE;
            f->hwnd = hwnd;
            return FALSE;
        },
        (LPARAM)&find);
    if (!find.hwnd) return;
    bool show = g_settings.gameWindow || !g_frameLink.IsOpen();
    if (show != (IsWindowVisible(find.hwnd) != 0)) ShowWindow(find.hwnd, show ? SW_SHOWNOACTIVATE : SW_HIDE);
}

// SDL key code (SDL_keycode.h) of a key, 0 for those not sent to the game.
int SdlKey(ImGuiKey k) {
    const int sc = 1 << 30; // SDLK_SCANCODE_MASK: keys without a character
    if (k >= ImGuiKey_A && k <= ImGuiKey_Z) return 'a' + (k - ImGuiKey_A);
    if (k >= ImGuiKey_0 && k <= ImGuiKey_9) return '0' + (k - ImGuiKey_0);
    if (k >= ImGuiKey_F1 && k <= ImGuiKey_F12) return sc | (58 + (k - ImGuiKey_F1));
    switch (k) {
    case ImGuiKey_Enter: return 13;
    case ImGuiKey_Escape: return 27;
    case ImGuiKey_Backspace: return 8;
    case ImGuiKey_Tab: return 9;
    case ImGuiKey_Space: return ' ';
    case ImGuiKey_Minus: return '-';
    case ImGuiKey_Equal: return '=';
    case ImGuiKey_Period: return '.';
    case ImGuiKey_Comma: return ',';
    case ImGuiKey_Delete: return 127;
    case ImGuiKey_Insert: return sc | 73;
    case ImGuiKey_Home: return sc | 74;
    case ImGuiKey_PageUp: return sc | 75;
    case ImGuiKey_End: return sc | 77;
    case ImGuiKey_PageDown: return sc | 78;
    case ImGuiKey_RightArrow: return sc | 79;
    case ImGuiKey_LeftArrow: return sc | 80;
    case ImGuiKey_DownArrow: return sc | 81;
    case ImGuiKey_UpArrow: return sc | 82;
    case ImGuiKey_KeypadSubtract: return sc | 86;
    case ImGuiKey_KeypadAdd: return sc | 87;
    case ImGuiKey_KeypadEnter: return sc | 88;
    case ImGuiKey_LeftCtrl: return sc | 224;
    case ImGuiKey_LeftShift: return sc | 225;
    case ImGuiKey_LeftAlt: return sc | 226;
    default: return 0;
    }
}

// While Game has focus, keys go to the game (but the debugger's F5, F10 and F11).
void ForwardKeys(bool focused) {
    static std::vector<ImGuiKey> held;
    ImGuiIO &io = ImGui::GetIO();
    int mods = (io.KeyShift ? 0x1 : 0) | (io.KeyCtrl ? 0x40 : 0) | (io.KeyAlt ? 0x100 : 0); // SDL KMOD_L*
    if (!focused || io.WantTextInput || !g_frameLink.IsOpen()) {
        for (ImGuiKey k : held) g_frameLink.SendKey(SdlKey(k), false, mods);
        held.clear();
        return;
    }
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; k++) {
        ImGuiKey key = (ImGuiKey)k;
        int sdl = SdlKey(key);
        if (!sdl || key == ImGuiKey_F5 || key == ImGuiKey_F10 || key == ImGuiKey_F11) continue;
        bool down = std::find(held.begin(), held.end(), key) != held.end();
        if (ImGui::IsKeyDown(key) && !down) {
            g_frameLink.SendKey(sdl, true, mods);
            held.push_back(key);
        } else if (!ImGui::IsKeyDown(key) && down) {
            g_frameLink.SendKey(sdl, false, mods);
            held.erase(std::find(held.begin(), held.end(), key));
        }
    }
}

static void UpdateInputHistory();

void PollFrames() {
    if (!g_bridge.Running()) {
        g_frameLink.Close();
        return;
    }
    UpdateGameWindow();
    if (!g_frameLink.IsOpen()) {
        static double lastTry;
        if (ImGui::GetTime() - lastTry < 0.25) return;
        lastTry = ImGui::GetTime();
        if (!g_frameLink.Open(g_bridge.Pid())) return;
        g_frameLink.SendWindowFit(!g_settings.gameWindow);
    }
    static bool fit;
    if (fit != !g_settings.gameWindow && g_frameLink.SendWindowFit(!g_settings.gameWindow)) fit = !g_settings.gameWindow;
    if (!g_frameLink.Read(g_frame)) return;
    g_framesRead++;

    // The game's alpha is not meant to be seen
    for (size_t n = 3; n < g_frame.pixels.size(); n += 4) g_frame.pixels[n] = 255;
    if (!g_frameTex) glGenTextures(1, &g_frameTex);
    glBindTexture(GL_TEXTURE_2D, g_frameTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    if (g_texW != g_frame.width || g_texH != g_frame.height) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST); // zoomed: the game's pixels
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_frame.width, g_frame.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     g_frame.pixels.data());
        g_texW = g_frame.width;
        g_texH = g_frame.height;
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g_frame.width, g_frame.height, GL_RGBA, GL_UNSIGNED_BYTE,
                        g_frame.pixels.data());
    }

    Json j;
    if (!Json::Parse(g_frame.geometry.c_str(), j)) return;
    const Json &vp = j["vp"], &scr = j["scr"];
    g_frameVp = ImVec4((float)vp[size_t(0)].num(), (float)vp[1].num(), (float)vp[2].num(), (float)vp[3].num());
    g_frameScr = ImVec2((float)scr[size_t(0)].num(1), (float)scr[1].num(1));
    g_frameEnts.clear();
    for (const auto &e : j["e"].items) {
        FrameEntity f;
        f.id = e["id"].i();
        f.pn = e["pn"].i();
        f.helper = e["h"].truthy();
        f.axis = ImVec2((float)e["ax"][size_t(0)].num(), (float)e["ax"][1].num());
        if (!e["an"].isNull()) {
            f.action = e["an"].i();
            f.elem = e["el"].i();
            f.animPn = e["apn"].i();
            f.k = ImVec2((float)e["k"][size_t(0)].num(), (float)e["k"][1].num());
        }
        ReadBoxes(e["c1"], f.c1);
        ReadBoxes(e["c2"], f.c2);
        for (const auto &v : e["in"].items) f.held.push_back(v.i());
        for (const auto &c : e["cm"].items)
            f.cmds.push_back({c[size_t(0)].str(), c[1].i(), c[2].i(), c[3].i(), c[4].i(), c[5].i(), c[6].i()});
        g_frameEnts.push_back(std::move(f));
    }
    const Json &stage = j["stage"];
    g_stageDef = stage["def"].str();
    StageView &sv = g_stageView;
    sv.ok = !stage["cam"].isNull();
    sv.free = stage["free"].truthy();
    for (size_t n = 0; n < 7; n++) sv.cam[n] = (float)stage["cam"][n].num();
    for (size_t n = 0; n < 13; n++) sv.lim[n] = (float)stage["lim"][n].num();
    sv.starts.clear();
    for (const auto &p : stage["start"].items) sv.starts.push_back(ImVec2((float)p[size_t(0)].num(), (float)p[1].num()));
    sv.time = stage["time"].i();
    sv.ranAt = stage["ranAt"].i();
    sv.ctrls.clear();
    for (const auto &c : stage["bgc"].items) {
        StageView::Ctrl ctrl{c[size_t(0)].truthy(), c[1].i(), c[2].i(), c[3].i(), {}};
        for (const auto &b : c[4].items) ctrl.bgs.push_back(b.i());
        sv.ctrls.push_back(std::move(ctrl));
    }
    g_frameBgs.clear();
    for (const auto &b : stage["bg"].items)
        g_frameBgs.push_back({b[size_t(0)].i(),
                              ImVec4((float)b[1].num(), (float)b[2].num(), (float)b[3].num(), (float)b[4].num()),
                              ImVec2((float)b[5].num(), (float)b[6].num()), b[7].i(), b[8].truthy()});
    ReadScreenGeometry(j);
    UpdateInputHistory();
    const Json &tg = j["tg"];
    int id = tg.isNull() ? -1 : tg["id"].i();
    if (id != g_trigId) g_trigTicks.clear();
    g_trigId = id;
    const auto &vals = tg["v"].items;
    if (id >= 0 && !vals.empty()) {
        auto &tick = g_trigTicks[tg["t"].i()];
        tick.clear();
        for (size_t n = 0; n + 1 < vals.size(); n += 2) tick.push_back({vals[n].i(), vals[n + 1].i()});
        // By source; one evaluated twice in a tick (loops) keeps its values in order, the last one counts
        std::stable_sort(tick.begin(), tick.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        while (g_trigTicks.size() > kHistory) g_trigTicks.erase(g_trigTicks.begin());
    }
}

// Each player's last keys, as runs (keys, ticks), the newest last: from the frames' counters of
// how long each key has been held or released, the smallest being the ticks since the last change
// (so frames the debugger misses lose nothing but runs shorter than them).
struct InputRun {
    int bits, ticks;
};
static std::map<int, std::deque<InputRun>> g_inputHist; // by player number

static void UpdateInputHistory() {
    for (const auto &e : g_frameEnts) {
        if (e.helper || e.held.size() < 14) continue;
        int bits = 0, since = INT_MAX;
        for (int k = 0; k < 14; k++) {
            if (e.held[k] > 0) bits |= 1 << k;
            since = std::min(since, std::abs(e.held[k]));
        }
        auto &h = g_inputHist[e.pn];
        if (h.empty() || h.back().bits != bits || since < h.back().ticks)
            h.push_back({bits, since});
        else
            h.back().ticks = since;
        while (h.size() > 40) h.pop_front();
    }
}

// The history drawn down a side of the picture: P1 (and P3) on the left, P2 (and P4) on the right
static void DrawInputHistory(ImDrawList *draw, ImVec2 p0, ImVec2 size) {
    float line = ImGui::GetTextLineHeight(), pad = line * 0.4f;
    int rows = std::max(1, (int)((size.y - line * 3) / line));
    for (const auto &[pn, h] : g_inputHist) {
        if (pn < 1 || pn > 2) continue;
        bool left = pn == 1;
        float y = p0.y + line * 2.5f;
        int shown = 0;
        for (auto r = h.rbegin(); r != h.rend() && shown < rows; ++r, ++shown) {
            char text[48];
            std::string keys = KeysText(r->bits);
            if (r->ticks > 999)
                snprintf(text, sizeof(text), "%s", keys.c_str());
            else if (left)
                snprintf(text, sizeof(text), "%3d %s", r->ticks, keys.c_str());
            else
                snprintf(text, sizeof(text), "%s %3d", keys.c_str(), r->ticks);
            ImVec2 sz = ImGui::CalcTextSize(text);
            float x = left ? p0.x + pad : p0.x + size.x - pad - sz.x;
            draw->AddRectFilled(ImVec2(x - 3, y), ImVec2(x + sz.x + 3, y + line), IM_COL32(0, 0, 0, 150));
            draw->AddText(ImVec2(x, y), shown == 0 ? IM_COL32(255, 230, 120, 255) : IM_COL32(230, 230, 230, 220), text);
            y += line;
        }
    }
}

int TriggerValue(int tick, int src) {
    auto it = g_trigTicks.find(tick);
    if (it == g_trigTicks.end()) return -1;
    auto v = std::upper_bound(it->second.begin(), it->second.end(), src,
                              [](int s, const std::pair<int, int> &p) { return s < p.first; });
    return v == it->second.begin() || (v - 1)->first != src ? -1 : (v - 1)->second;
}

// The selected player's boxes on the Game window, with handles: drag an edge, a corner or the
// inside. Returns true while it has the mouse (the Game window then neither selects nor moves).
struct BoxDrag {
    bool active = false;
    int g = 0, i = 0, edges = 0; // edges: 1 left, 2 right, 4 top, 8 bottom (screen); 15 move
    ImVec2 start;                // mouse, in AIR units
    AirBox box;                  // when the drag started
} g_boxDrag;

bool BoxEditOverlay(ImDrawList *draw, bool hovered, const std::function<ImVec2(float, float)> &toScreen,
                    const std::function<ImVec2(ImVec2)> &toPicture, bool &shown) {
    shown = false;
    AnimTarget t = GetAnimTarget();
    if (!g_boxEdit || !t.live || !t.fe || t.fe->k.x == 0 || t.fe->k.y == 0) {
        g_boxDrag.active = false;
        return false;
    }
    shown = true;
    const AirElem &el = t.act->elems[t.elem - 1];
    const FrameEntity &fe = *t.fe;
    auto airToScreen = [&](float x, float y) { return toScreen(fe.axis.x + fe.k.x * x, fe.axis.y + fe.k.y * y); };
    auto screenToAir = [&](ImVec2 s) {
        ImVec2 p = toPicture(s);
        return ImVec2((p.x - fe.axis.x) / fe.k.x, (p.y - fe.axis.y) / fe.k.y);
    };
    // The box as dragged so far (AIR units, x1 <= x2, y1 <= y2)
    auto dragged = [&](ImVec2 m) {
        AirBox b = g_boxDrag.box;
        ImVec2 a = screenToAir(m);
        int dx = (int)lroundf(a.x - g_boxDrag.start.x), dy = (int)lroundf(a.y - g_boxDrag.start.y);
        int e = g_boxDrag.edges;
        if (e == 15) {
            b.x1 += dx, b.x2 += dx, b.y1 += dy, b.y2 += dy;
        } else {
            // facing left: the screen's left edge is the box's x2
            bool flip = fe.k.x < 0;
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

    // The previous element's sprite and boxes, faint (the last element's before the first)
    if (g_onionSprite && t.act->elems.size() > 1) {
        const AirElem &prev = t.act->elems[t.elem >= 2 ? t.elem - 2 : t.act->elems.size() - 1];
        SpriteFile *spf = SpritesOf(t.pn);
        int index = spf ? Sff_FindSprite(spf->doc, prev.group, prev.image) : -1;
        if (GLuint tex = index >= 0 ? SpriteTexture(*spf, index) : 0) {
            const SffSprite &s = spf->doc.sprites[index];
            float x0 = (float)(prev.x - s.x), y0 = (float)(prev.y - s.y);
            ImVec2 a = airToScreen(x0, y0), b = airToScreen(x0 + s.w, y0 + s.h), uv0(0, 0), uv1(1, 1);
            if (a.x > b.x) std::swap(a.x, b.x), std::swap(uv0.x, uv1.x); // facing left
            draw->AddImage(TexRef(tex), a, b, uv0, uv1, IM_COL32(255, 255, 255, 110));
        }
    }
    if (g_onion && t.act->elems.size() > 1) {
        const AirElem &prev = t.act->elems[t.elem >= 2 ? t.elem - 2 : t.act->elems.size() - 1];
        for (int g = 0; g < 2; g++)
            for (const auto &b : prev.clsn[g]) {
                ImVec2 a = airToScreen((float)b.x1, (float)b.y1), c = airToScreen((float)b.x2, (float)b.y2);
                draw->AddRect(ImVec2(std::min(a.x, c.x), std::min(a.y, c.y)), ImVec2(std::max(a.x, c.x), std::max(a.y, c.y)),
                              g == 0 ? IM_COL32(255, 120, 120, 90) : IM_COL32(140, 180, 255, 90), 0, thick);
            }
    }

    // Sprite mode: a drag near the player moves the element's offset, sent to the game as it goes
    if (g_animDrag == 1) {
        static bool active;
        static ImVec2 start;
        static int x0, y0, sentX, sentY;
        ImVec2 lo = airToScreen(0, 0), hi = lo;
        for (int g = 0; g < 2; g++)
            for (const auto &b : el.clsn[g])
                for (ImVec2 p : {airToScreen((float)b.x1, (float)b.y1), airToScreen((float)b.x2, (float)b.y2)}) {
                    lo = ImVec2(std::min(lo.x, p.x), std::min(lo.y, p.y));
                    hi = ImVec2(std::max(hi.x, p.x), std::max(hi.y, p.y));
                }
        float pad = ImGui::GetFontSize() * 2;
        bool nearby = m.x >= lo.x - pad && m.x <= hi.x + pad && m.y >= lo.y - pad && m.y <= hi.y + pad;
        if (!active && hovered && nearby && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            active = true;
            start = screenToAir(m);
            x0 = sentX = el.x, y0 = sentY = el.y;
        }
        if (active && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) active = false;
        if (active) {
            ImVec2 a = screenToAir(m);
            int nx = x0 + (int)lroundf(a.x - start.x), ny = y0 + (int)lroundf(a.y - start.y);
            if ((nx != sentX || ny != sentY) && Air_SetElem(t.sf->text, el, el.group, el.image, nx, ny, el.time)) {
                ApplyAnims(*t.sf);
                sentX = nx, sentY = ny;
            }
            char label[48];
            snprintf(label, sizeof(label), "offset %d, %d", sentX, sentY);
            draw->AddText(ImVec2(m.x + grab * 2, m.y + grab * 2), IM_COL32(255, 255, 255, 255), label);
        }
        if (active || (hovered && nearby)) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        for (int g = 0; g < 2; g++)
            for (const auto &b : el.clsn[g]) {
                ImVec2 a = airToScreen((float)b.x1, (float)b.y1), c = airToScreen((float)b.x2, (float)b.y2);
                draw->AddRect(ImVec2(std::min(a.x, c.x), std::min(a.y, c.y)), ImVec2(std::max(a.x, c.x), std::max(a.y, c.y)),
                              g == 0 ? IM_COL32(255, 70, 70, 200) : IM_COL32(80, 140, 255, 200), 0, thick);
            }
        return active || (hovered && nearby);
    }

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

    if (g_boxDrag.active && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        // Released: write the box and send the animations to the game
        AirBox b = dragged(m);
        const AirBox &old = g_boxDrag.box;
        g_boxDrag.active = false;
        if ((b.x1 != old.x1 || b.y1 != old.y1 || b.x2 != old.x2 || b.y2 != old.y2) && g_boxDrag.g < 2 &&
            g_boxDrag.i < (int)el.clsn[g_boxDrag.g].size() &&
            Air_SetBox(t.sf->text, el.clsn[g_boxDrag.g][g_boxDrag.i], b.x1, b.y1, b.x2, b.y2))
            ApplyAnims(*t.sf);
        return true;
    }
    if (!g_boxDrag.active && hovered && hitG >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        g_boxDrag.active = true;
        g_boxDrag.g = hitG, g_boxDrag.i = hitI, g_boxDrag.edges = hitEdges;
        g_boxDrag.start = screenToAir(m);
        g_boxDrag.box = el.clsn[hitG][hitI];
    }

    int cursorEdges = g_boxDrag.active ? g_boxDrag.edges : hovered ? hitEdges : 0;
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
            bool dragging = g_boxDrag.active && g_boxDrag.g == g && g_boxDrag.i == (int)i;
            bool hot = dragging || (!g_boxDrag.active && hitG == g && hitI == (int)i);
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
    return g_boxDrag.active || (hovered && hitG >= 0);
}

void GameWindow() {
    if (!BeginTool("Game", ImGuiWindowFlags_NoNavInputs)) {
        ForwardKeys(false);
        return;
    }
    bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    ForwardKeys(focused);
    static bool showBoxes = true, showAxes = true, onlySelected = false, showKeys = false, showHistory = false;
    ImGui::Checkbox("Boxes", &showBoxes);
    ImGui::SameLine();
    ImGui::Checkbox("Axes", &showAxes);
    ImGui::SameLine();
    ImGui::Checkbox("Keys", &showKeys);
    ImGui::SetItemTooltip("Under each player, the keys it holds (its own, the AI's, a dummy's or a take's).");
    ImGui::SameLine();
    ImGui::Checkbox("History", &showHistory);
    ImGui::SetItemTooltip("P1's last keys down the left side, P2's down the right, each with the ticks it lasted\n"
                          "(newest on top), as training modes show them.");
    ImGui::SameLine();
    ImGui::Checkbox("Only the selected one", &onlySelected);
    ImGui::SameLine();
    ImGui::Checkbox("Follow it", &g_followSel);
    ImGui::SetItemTooltip("Zoomed in (mouse wheel), keep the selected player in view. The right button pans.");
    ImGui::SameLine();
    if (ImGui::Checkbox("Stage", &g_stagePick) && g_stagePick) g_screenPick = false;
    ImGui::SetItemTooltip("The mouse picks and drags the stage's BGs instead of the players (window Stage).");
    ImGui::SameLine();
    if (ImGui::Checkbox("Screenpack", &g_screenPick) && g_screenPick) g_stagePick = false;
    ImGui::SetItemTooltip("The mouse picks and drags the fight screen's elements (lifebars, time...) and the motif's\n(menus, select screen...) instead of the players (windows Fight screen, Motif).");
    if (g_frameTex) {
        ImGui::SameLine();
        ImGui::TextDisabled("%dx%d, frame %u", g_texW, g_texH, g_frame.frame);
        ImGui::SameLine();
        if (focused)
            ImGui::TextColored(Ink(kGreen), "keys go to the game");
        else
            ImGui::TextDisabled("click the picture to play");
    }
    if (!g_frameTex || !g_bridge.Running()) {
        ImGui::TextDisabled(!g_bridge.Running()    ? "Launch the game from the Session window."
                            : !g_frameLink.IsOpen() ? "Waiting for the game's frames (they need the patched Ikemen GO)."
                                                    : "Waiting for the first frame...");
        ImGui::End();
        return;
    }

    // The frame fits the window; zoomed, a part of it fills the same area. view is the frame
    // pixel at the top left.
    static float zoom = 1;
    static ImVec2 view;
    static bool panning;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float fit = std::max(0.01f, std::min(avail.x / g_texW, avail.y / g_texH));
    ImVec2 size(g_texW * fit, g_texH * fit);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    p0.x += (avail.x - size.x) * 0.5f;
    float s = fit * zoom; // screen pixels per frame pixel
    ImVec2 viewSize(size.x / s, size.y / s);
    float fx = g_frameVp.z / g_frameScr.x, fy = g_frameVp.w / g_frameScr.y; // frame pixels per picture pixel
    if (g_followSel && zoom > 1)
        if (const FrameEntity *fe = FindFrameEntity(g_selId)) {
            // The middle of its boxes, else of its axis
            ImVec2 lo = fe->axis, hi = fe->axis;
            for (const auto *boxes : {&fe->c1, &fe->c2})
                for (const auto &b : *boxes) {
                    lo = ImVec2(std::min(lo.x, std::min(b.x, b.x + b.z)), std::min(lo.y, std::min(b.y, b.y + b.w)));
                    hi = ImVec2(std::max(hi.x, std::max(b.x, b.x + b.z)), std::max(hi.y, std::max(b.y, b.y + b.w)));
                }
            view.x = g_frameVp.x + (lo.x + hi.x) * 0.5f * fx - viewSize.x * 0.5f;
            view.y = g_frameVp.y + (lo.y + hi.y) * 0.5f * fy - viewSize.y * 0.5f;
        }
    view.x = std::max(0.0f, std::min(view.x, g_texW - viewSize.x));
    view.y = std::max(0.0f, std::min(view.y, g_texH - viewSize.y));
    ImGui::SetCursorScreenPos(p0);
    // Rows come bottom first
    ImGui::Image(ImTextureRef((ImTextureID)(intptr_t)g_frameTex), size,
                 ImVec2(view.x / g_texW, 1 - view.y / g_texH),
                 ImVec2((view.x + viewSize.x) / g_texW, 1 - (view.y + viewSize.y) / g_texH));
    bool hovered = ImGui::IsItemHovered();
    bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

    // Wheel: zoom on the point under the mouse; right button: pan (the stage's free camera, when on)
    ImGuiIO &io = ImGui::GetIO();
    bool freeCam = StageCameraInput(hovered, ImVec2(1 / (fx * s), 1 / (fy * s)), [&](ImVec2 q) {
        return ImVec2(((q.x - p0.x) / s + view.x - g_frameVp.x) / fx, ((q.y - p0.y) / s + view.y - g_frameVp.y) / fy);
    });
    if (freeCam) {
    } else if (hovered && io.MouseWheel != 0) {
        ImVec2 m = io.MousePos;
        ImVec2 at(view.x + (m.x - p0.x) / s, view.y + (m.y - p0.y) / s);
        zoom = std::max(1.0f, std::min(16.0f, zoom * powf(1.25f, io.MouseWheel)));
        s = fit * zoom;
        viewSize = ImVec2(size.x / s, size.y / s);
        view = ImVec2(at.x - (m.x - p0.x) / s, at.y - (m.y - p0.y) / s);
        view.x = std::max(0.0f, std::min(view.x, g_texW - viewSize.x));
        view.y = std::max(0.0f, std::min(view.y, g_texH - viewSize.y));
    }
    if (hovered && !freeCam && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) panning = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right)) panning = false;
    if (panning) {
        g_followSel = false;
        view.x = std::max(0.0f, std::min(view.x - io.MouseDelta.x / s, g_texW - viewSize.x));
        view.y = std::max(0.0f, std::min(view.y - io.MouseDelta.y / s, g_texH - viewSize.y));
    }

    // Game picture pixels to the screen, and back
    float kx = fx * s, ky = fy * s;
    auto toScreen = [&](float x, float y) {
        return ImVec2(p0.x + (g_frameVp.x + x * fx - view.x) * s, p0.y + (g_frameVp.y + y * fy - view.y) * s);
    };
    auto toPicture = [&](ImVec2 q) {
        return ImVec2(((q.x - p0.x) / s + view.x - g_frameVp.x) / fx, ((q.y - p0.y) / s + view.y - g_frameVp.y) / fy);
    };

    // The selected player's boxes, editable (Animation window); they take the mouse first
    ImDrawList *draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(p0, ImVec2(p0.x + size.x, p0.y + size.y), true);
    draw->ChannelsSplit(2);
    draw->ChannelsSetCurrent(1);
    bool boxesShown = false;
    bool boxMouse = BoxEditOverlay(draw, hovered, toScreen, toPicture, boxesShown);
    draw->ChannelsSetCurrent(0);
    // The fight screen's and the motif's elements, in front of the stage; in pick mode they take the mouse
    if (ScreenOverlay(draw, hovered && !boxMouse, clicked, toScreen, ImVec2(kx, ky))) {
        hovered = false;
        clicked = false;
    }
    // The stage's BGs: in pick mode they take the mouse instead of the players
    if (StageOverlay(draw, hovered && !boxMouse, clicked, toScreen, ImVec2(kx, ky))) {
        hovered = false;
        clicked = false;
    }

    // Dragging a player or helper moves it (debugMoveBy takes pixels of the game picture)
    static int dragId = -1;
    static ImVec2 dragRest;
    if (dragId >= 0) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || !FindEntity(dragId)) {
            dragId = -1;
        } else {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            dragRest.x += d.x / kx;
            dragRest.y += d.y / ky;
            float dx = std::trunc(dragRest.x), dy = std::trunc(dragRest.y);
            if (dx != 0 || dy != 0) {
                char lua[128];
                snprintf(lua, sizeof(lua), "ikdbg.with(%d, function() debugMoveBy(%g, %g) end)", dragId, dx, dy);
                SendRaw(lua);
                dragRest.x -= dx;
                dragRest.y -= dy;
            }
        }
    }

    // What the mouse is over: the smallest box under it, else the nearest axis
    int hoverId = -1;
    if (hovered && !boxMouse) {
        ImVec2 m = ImGui::GetMousePos();
        float best = FLT_MAX;
        for (const auto &e : g_frameEnts)
            for (const auto *boxes : {&e.c2, &e.c1})
                for (const auto &b : *boxes) {
                    ImVec2 a = toScreen(b.x, b.y), c = toScreen(b.x + b.z, b.y + b.w);
                    float area = std::fabs((c.x - a.x) * (c.y - a.y));
                    if (m.x >= std::min(a.x, c.x) && m.x <= std::max(a.x, c.x) && m.y >= std::min(a.y, c.y) &&
                        m.y <= std::max(a.y, c.y) && area < best) {
                        best = area;
                        hoverId = e.id;
                    }
                }
        if (hoverId < 0) {
            float reach = ImGui::GetFontSize() * 1.5f;
            best = reach * reach;
            for (const auto &e : g_frameEnts) {
                ImVec2 a = toScreen(e.axis.x, e.axis.y);
                float d = (a.x - m.x) * (a.x - m.x) + (a.y - m.y) * (a.y - m.y);
                if (d < best) {
                    best = d;
                    hoverId = e.id;
                }
            }
        }
        if (clicked && hoverId >= 0) {
            g_selId = hoverId;
            g_selPicked++;
            dragId = hoverId;
            dragRest = ImVec2(0, 0);
        }
        if (hoverId >= 0 && dragId < 0) {
            const Entity *ent = FindEntity(hoverId);
            ImGui::SetTooltip("%s  %s\nstate %d, anim %d", ent ? Who(ent->pn, ent->helper, ent->id).c_str() : "?",
                              ent ? ent->name.c_str() : "", ent ? ent->st : 0, ent ? ent->anim : 0);
        }
    }

    float thick = std::max(1.0f, ImGui::GetFontSize() / 13);
    for (const auto &e : g_frameEnts) {
        bool sel = e.id == g_selId, hot = e.id == hoverId;
        if (onlySelected && !sel && !hot) continue;
        if (showBoxes && !(sel && boxesShown)) {
            for (int group = 1; group <= 2; group++) {
                ImU32 line = group == 1 ? IM_COL32(255, 70, 70, sel || hot ? 255 : 170)
                                        : IM_COL32(80, 140, 255, sel || hot ? 255 : 170);
                ImU32 fill = group == 1 ? IM_COL32(255, 70, 70, hot ? 70 : 35) : IM_COL32(80, 140, 255, hot ? 70 : 35);
                for (const auto &b : group == 1 ? e.c1 : e.c2) {
                    ImVec2 a = toScreen(b.x, b.y), c = toScreen(b.x + b.z, b.y + b.w);
                    ImVec2 lo(std::min(a.x, c.x), std::min(a.y, c.y)), hi(std::max(a.x, c.x), std::max(a.y, c.y));
                    draw->AddRectFilled(lo, hi, fill);
                    draw->AddRect(lo, hi, line, 0, sel ? thick * 2 : thick);
                }
            }
        }
        if (showAxes || sel || hot) {
            ImVec2 a = toScreen(e.axis.x, e.axis.y);
            float r = ImGui::GetFontSize() * (sel ? 0.8f : 0.5f);
            ImU32 col = sel ? IM_COL32(255, 220, 60, 255) : hot ? IM_COL32(255, 255, 255, 255) : IM_COL32(255, 255, 255, 160);
            draw->AddLine(ImVec2(a.x - r, a.y), ImVec2(a.x + r, a.y), col, sel ? thick * 2 : thick);
            draw->AddLine(ImVec2(a.x, a.y - r), ImVec2(a.x, a.y + r), col, sel ? thick * 2 : thick);
            if (sel || hot) {
                const Entity *ent = FindEntity(e.id);
                std::string label = ent ? Who(ent->pn, ent->helper, ent->id) + "  " + std::to_string(ent->st) : "";
                draw->AddText(ImVec2(a.x + r * 0.6f, a.y + r * 0.3f), col, label.c_str());
            }
        }
        if (showKeys && !e.helper && !e.held.empty()) {
            int bits = 0;
            for (size_t k = 0; k < e.held.size() && k < 14; k++)
                if (e.held[k] > 0) bits |= 1 << k;
            std::string keys = KeysText(bits);
            ImVec2 a = toScreen(e.axis.x, e.axis.y), sz = ImGui::CalcTextSize(keys.c_str());
            ImVec2 at(a.x - sz.x / 2, a.y + ImGui::GetFontSize() * 1.4f);
            draw->AddRectFilled(ImVec2(at.x - 3, at.y - 1), ImVec2(at.x + sz.x + 3, at.y + sz.y + 1), IM_COL32(0, 0, 0, 160), 3);
            draw->AddText(at, bits ? IM_COL32(255, 230, 120, 255) : IM_COL32(200, 200, 200, 200), keys.c_str());
        }
    }
    draw->ChannelsMerge();
    if (showHistory) DrawInputHistory(draw, p0, size);
    draw->PopClipRect();
    if (focused) draw->AddRect(p0, ImVec2(p0.x + size.x, p0.y + size.y), IM_COL32(90, 200, 90, 200), 0, thick);
    ImGui::End();
}

} // namespace app
