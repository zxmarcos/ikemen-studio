// Palettes: what a sprite file's sprites can be seen with (a character's palettes: its ACT files,
// else its SFF's 1,N; the SFF's other palettes), and the palette editor: colours picked on the
// sprite, edited, sent to the game as they change, saved to the ACT file or with the SFF.
#include "app_internal.h"
#include "palfile.h"
#include "sffedit.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <set>

namespace app {

std::map<int, int> g_palNo;

static PalFx g_palFx;           // the PalFX section's effect
static bool g_palFxOn = true, g_palFxOpen = false; // shown on the preview while its section is open (only there: it changes each tick)
static std::string g_palWanted; // the label of a palette picked in the list, for the Sprites window

// ACT files, read when first asked for
struct ActFile {
    std::string path; // as the game names it
    std::vector<unsigned char> rgba;
    bool ok = false, dirty = false;
};
static std::map<std::string, ActFile> g_acts; // by path

static ActFile &Act(const std::string &path) {
    std::string key = NormPath(path);
    auto it = g_acts.find(key);
    if (it != g_acts.end()) return it->second;
    ActFile &a = g_acts[key];
    a.path = path;
    a.ok = Sff_ReadAct(GamePath(path), a.rgba);
    return a;
}

static std::string FileName(const std::string &path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

static std::string PalName(const SffPal &p) { return std::to_string(p.group) + "," + std::to_string(p.number); }

void PalChoices(SpriteFile &sf, int pn, std::vector<PalChoice> &out) {
    out.clear();
    const SffDoc &d = sf.doc;
    PalChoice own;
    own.label = "Its own";
    own.own = true;
    out.push_back(own);
    std::vector<char> listed(d.pals.size(), 0);
    if (pn > 0) {
        // The character's: palN of its def (an ACT file), else the SFF's 1,N
        for (int n = 1; n <= 99; n++) {
            std::string act = CharFile(pn, ("pal" + std::to_string(n)).c_str());
            int si = Sff_FindPal(d, 1, n);
            bool hasAct = !act.empty() && Act(act).ok;
            if (!hasAct && si < 0) {
                if (n >= 12) break;
                continue;
            }
            PalChoice c;
            c.slot = n;
            c.sff = si;
            if (hasAct) c.act = act;
            c.label = "Palette " + std::to_string(n) + ": " + (hasAct ? FileName(act) : "SFF 1," + std::to_string(n));
            if (si >= 0) listed[si] = 1;
            out.push_back(c);
        }
    }
    std::vector<int> users(d.pals.size(), 0);
    for (const SffSprite &s : d.sprites)
        if (s.pal >= 0 && s.pal < (int)users.size()) users[s.pal]++;
    for (int i = 0; i < (int)d.pals.size(); i++) {
        if (listed[i]) continue;
        PalChoice c;
        c.sff = i;
        c.label = "SFF " + PalName(d.pals[i]) + " (" + std::to_string(users[i]) + (users[i] == 1 ? " sprite)" : " sprites)");
        out.push_back(c);
    }
}

PalChoice PalFor(SpriteFile &sf, const PalChoice &c, int sprite) {
    const SffDoc &d = sf.doc;
    PalChoice r;
    if (sprite < 0 || sprite >= (int)d.sprites.size() || d.sprites[sprite].pal < 0) return r; // true colour
    const SffSprite &s = d.sprites[sprite];
    if (!c.own) {
        if (c.slot > 0 && s.pal == Sff_FindPal(d, 1, 1)) return c; // the character's palette in place of the shared one
        if (c.slot == 0 && c.sff >= 0) return c;                   // another palette: every sprite with it
    }
    // Its own
    r.sff = s.pal;
    if (s.pal < (int)d.pals.size()) {
        const SffPal &p = d.pals[s.pal];
        r.label = "SFF " + PalName(p);
        if (p.group == 1) r.slot = p.number;
    }
    return r;
}

std::string PalTakeWanted() {
    std::string s;
    s.swap(g_palWanted);
    return s;
}

// The colours of a choice (256 RGBA), nullptr none.
static std::vector<unsigned char> *Colours(SpriteFile &sf, const PalChoice &c) {
    if (!c.act.empty()) {
        ActFile &a = Act(c.act);
        return a.ok ? &a.rgba : nullptr;
    }
    if (c.sff >= 0 && c.sff < (int)sf.doc.pals.size()) return &sf.doc.pals[c.sff].rgba;
    return nullptr;
}

// Textures of sprites drawn with another palette than their own (or with colours flashed)
struct PalTex {
    GLuint tex = 0;
    unsigned key = 0;
};
static std::map<std::pair<const SpriteFile *, int>, PalTex> g_palTex;

static unsigned Hash(const unsigned char *p, size_t n, unsigned h = 2166136261u) {
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

GLuint PalSpriteTexture(SpriteFile &sf, int sprite, const PalChoice &c, const std::vector<char> *mark) {
    if (sprite < 0 || sprite >= (int)sf.doc.sprites.size()) return 0;
    const SffSprite &s = sf.doc.sprites[sprite];
    PalChoice r = PalFor(sf, c, sprite);
    std::vector<unsigned char> *cols = Colours(sf, r);
    bool flash = mark && fmod(ImGui::GetTime(), 0.8) < 0.4;
    if (flash) {
        flash = false;
        for (char m : *mark) flash = flash || m;
    }
    bool fx = mark && g_palFxOn && g_palFxOpen && !g_palFx.Plain();
    if (!cols || (r.act.empty() && r.sff == s.pal && !flash && !fx)) return SpriteTexture(sf, sprite); // as in the file
    std::vector<unsigned char> pal = *cols;
    pal.resize(1024, 0);
    if (fx) Pal_ApplyFx(pal, g_palFx, (int)(ImGui::GetTime() * 60));
    if (flash)
        for (int i = 0; i < 256; i++)
            if ((*mark)[i]) pal[i * 4] = 255, pal[i * 4 + 1] = 0, pal[i * 4 + 2] = 255;
    unsigned key = Hash(pal.data(), pal.size(), sf.version * 2654435761u + 1);
    PalTex &t = g_palTex[{&sf, sprite}];
    if (t.tex && t.key == key) return t.tex;
    std::vector<unsigned char> px, rgba;
    int bpp = 0;
    if (!Sff_Decode(s, px, &bpp)) return 0;
    Sff_ToRgbaWith(s, px, bpp, rgba, pal.data());
    if (!t.tex) glGenTextures(1, &t.tex);
    t.key = key;
    glBindTexture(GL_TEXTURE_2D, t.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s.w, s.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    return t.tex;
}

int SpritePixel(SpriteFile &sf, int sprite, int x, int y) {
    static const SpriteFile *forFile;
    static int forSprite = -1;
    static unsigned forVersion;
    static std::vector<unsigned char> px;
    static int bpp;
    if (sprite < 0 || sprite >= (int)sf.doc.sprites.size()) return -1;
    const SffSprite &s = sf.doc.sprites[sprite];
    if (forFile != &sf || forSprite != sprite || forVersion != sf.version) {
        forFile = &sf, forSprite = sprite, forVersion = sf.version;
        if (!Sff_Decode(s, px, &bpp)) px.clear();
    }
    if (bpp != 1 || x < 0 || y < 0 || x >= s.w || y >= s.h || (size_t)(y * s.w + x) >= px.size()) return -1;
    return px[y * s.w + x];
}

// Colours changed: marked to be saved, sent to the game soon
struct Pending {
    int g = 0, n = 0;
    PalChoice c;
};
static std::map<std::string, Pending> g_pending; // by file and palette
static std::set<std::string> g_madeHere;         // palettes made in the editor: the game has none until saved

static std::string PalKey(const SpriteFile &sf, int g, int n) {
    return NormPath(sf.path) + "#" + std::to_string(g) + "," + std::to_string(n);
}

static void Changed(SpriteFile &sf, const PalChoice &c, bool send) {
    sf.edits++;
    if (!c.act.empty())
        Act(c.act).dirty = true;
    else
        SpritesChanged(sf);
    int g = 0, n = 0;
    if (!send) return;
    if (c.slot > 0)
        g = 1, n = c.slot;
    else if (c.sff >= 0 && c.sff < (int)sf.doc.pals.size())
        g = sf.doc.pals[c.sff].group, n = sf.doc.pals[c.sff].number;
    else
        return;
    std::string key = PalKey(sf, g, n);
    if (!g_madeHere.count(key)) g_pending[key] = {g, n, c};
}

// The player the game draws with that file (its sprites), 0 none.
static int PlayerOf(const SpriteFile &sf, int pn) {
    if (!Connected()) return 0;
    if (pn > 0 && pn != kDiskPn && NormPath(CharFile(pn, "sprite")) == NormPath(sf.path)) return pn;
    for (const auto &kv : g_charFiles) {
        auto f = kv.second.find("sprite");
        if (f != kv.second.end() && !f->second.empty() && NormPath(f->second[0]) == NormPath(sf.path)) return kv.first;
    }
    return 0;
}

static void Send(SpriteFile &sf, int player, int g, int n, const PalChoice &c) {
    std::vector<unsigned char> *cols = Colours(sf, c);
    if (!cols || player <= 0) return;
    static const char digits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(2048);
    for (int i = 0; i < 1024; i++) {
        unsigned char b = i < (int)cols->size() ? (*cols)[i] : 0;
        hex += digits[b >> 4];
        hex += digits[b & 15];
    }
    SendRaw("ikdbg.palette(" + std::to_string(player) + ", " + std::to_string(g) + ", " + std::to_string(n) + ", '" +
            hex + "')");
}

void PalFrame(SpriteFile &sf, int pn) {
    static double last;
    if (g_pending.empty() || ImGui::GetTime() - last < 0.06) return;
    last = ImGui::GetTime();
    int player = PlayerOf(sf, pn);
    std::string prefix = NormPath(sf.path) + "#";
    for (auto it = g_pending.begin(); it != g_pending.end();) {
        if (it->first.compare(0, prefix.size(), prefix) != 0) {
            ++it;
            continue;
        }
        Send(sf, player, it->second.g, it->second.n, it->second.c);
        it = g_pending.erase(it);
    }
}

bool PalDirty() {
    for (const auto &kv : g_acts)
        if (kv.second.dirty) return true;
    return false;
}

void PalSave() {
    for (auto &kv : g_acts) {
        ActFile &a = kv.second;
        if (!a.dirty) continue;
        std::string path = GamePath(a.path), bak = path + ".bak";
        if (GetFileAttributesA(bak.c_str()) == INVALID_FILE_ATTRIBUTES) CopyFileA(path.c_str(), bak.c_str(), TRUE);
        std::ofstream out(path, std::ios::binary);
        out << Sff_ActBytes(a.rgba);
        out.close();
        if (!out) {
            Log(LogLine::Error, "Could not write " + path);
            continue;
        }
        a.dirty = false;
        Log(LogLine::Info, "Saved " + path);
    }
}

void PalRevert(SpriteFile &sf, int pn) {
    std::vector<PalChoice> choices;
    PalChoices(sf, pn, choices);
    int player = PlayerOf(sf, pn);
    for (const PalChoice &c : choices) {
        if (c.act.empty()) continue;
        ActFile &a = Act(c.act);
        bool was = a.dirty;
        a.ok = Sff_ReadAct(GamePath(a.path), a.rgba);
        a.dirty = false;
        if (was && c.slot > 0) Send(sf, player, 1, c.slot, c); // the game back to the file's colours
    }
}

void PalCapture(SpriteFile &sf, int pn, SpriteState &st, const SpriteState *before) {
    const std::vector<SffPal> &pals = sf.doc.pals;
    st.pals.resize(pals.size());
    for (size_t i = 0; i < pals.size(); i++) {
        const SffPal *old = before && i < before->pals.size() ? before->pals[i].get() : nullptr;
        bool same = old && old->group == pals[i].group && old->number == pals[i].number && old->ncolors == pals[i].ncolors &&
                    old->rgba == pals[i].rgba;
        st.pals[i] = same ? before->pals[i] : std::make_shared<const SffPal>(pals[i]);
    }
    std::vector<PalChoice> choices;
    PalChoices(sf, pn, choices);
    for (const PalChoice &c : choices)
        if (!c.act.empty()) st.acts[NormPath(c.act)] = Act(c.act).rgba;
}

void PalRestore(SpriteFile &sf, int pn, const SpriteState &st, bool saved) {
    std::vector<SffPal> old;
    old.swap(sf.doc.pals);
    for (const auto &p : st.pals) sf.doc.pals.push_back(*p);
    std::vector<PalChoice> choices;
    PalChoices(sf, pn, choices);
    for (const PalChoice &c : choices) {
        if (c.own) continue;
        bool changed = false;
        if (!c.act.empty()) {
            ActFile &a = Act(c.act);
            auto it = st.acts.find(NormPath(c.act));
            if (it != st.acts.end() && it->second != a.rgba) a.rgba = it->second, changed = true;
        } else if (c.sff >= 0) {
            const SffPal &now = sf.doc.pals[c.sff];
            auto was = std::find_if(old.begin(), old.end(), [&](const SffPal &p) { return p.group == now.group && p.number == now.number; });
            changed = was != old.end() && was->rgba != now.rgba;
        }
        if (changed) Changed(sf, c, true);
        if (saved && !c.act.empty()) Act(c.act).dirty = false;
    }
}

// The colours' tools: shift hue, saturation and brightness, a gradient, copy and paste
static void Hsv(unsigned char *c, float dh, float ds, float dv) {
    float h, s, v, r, g, b;
    ImGui::ColorConvertRGBtoHSV(c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, h, s, v);
    h = fmodf(h + dh + 1.0f, 1.0f);
    s = std::max(0.0f, std::min(1.0f, s + ds));
    v = std::max(0.0f, std::min(1.0f, v + dv));
    ImGui::ColorConvertHSVtoRGB(h, s, v, r, g, b);
    c[0] = (unsigned char)lroundf(r * 255), c[1] = (unsigned char)lroundf(g * 255), c[2] = (unsigned char)lroundf(b * 255);
}

void PalettePanel(SpriteFile &sf, int pn, const PalChoice &target, int picked, bool shift, std::vector<char> &mark) {
    static std::vector<char> sel(256, 0);
    static int anchor = -1, primary = -1, moving = -1; // moving: the colour being taken elsewhere (Alt+drag)
    static std::vector<unsigned char> clip; // colours copied (RGB)
    mark.assign(256, 0);
    std::vector<unsigned char> *cols = Colours(sf, target);
    float em = ImGui::GetFontSize();
    const ImGuiStyle &style = ImGui::GetStyle();
    if (!cols) {
        ImGui::TextDisabled("A true colour sprite: it has no palette.");
        return;
    }
    cols->resize(1024, 0);
    auto select = [&](int i, bool add) {
        if (!add) std::fill(sel.begin(), sel.end(), 0);
        sel[i] = 1;
        anchor = primary = i;
    };
    if (picked >= 0 && picked < 256) select(picked, shift);

    // Which palette, from where
    if (!target.act.empty())
        ImGui::TextWrapped("%s", target.act.c_str());
    else if (target.sff >= 0)
        ImGui::Text("SFF palette %s", PalName(sf.doc.pals[target.sff]).c_str());
    ImGui::TextDisabled(target.slot > 0 ? "The character's palette %d" : "Not one of the character's palettes", target.slot);
    if (!target.act.empty() && Act(target.act).dirty) {
        ImGui::SameLine();
        ImGui::TextColored(Ink(kYellow), "*");
    }
    // An SFF palette 1,N the character's ACT file takes the place of: the game does not show it
    bool shadowed = false;
    if (target.act.empty() && target.slot > 0 && pn > 0) {
        std::string act = CharFile(pn, ("pal" + std::to_string(target.slot)).c_str());
        if (!act.empty() && Act(act).ok) {
            shadowed = true;
            ImGui::TextColored(Ink(kYellow), "The game uses %s in its place.", FileName(act).c_str());
        }
    }
    auto edit = [&]() { Changed(sf, target, !shadowed); };

    // The colours, 16 a row (an SFF palette has those it was saved with, an ACT file 256): a click
    // picks one, Shift a range, Ctrl one more; a drag a range
    int ncol = target.act.empty() && target.sff >= 0 ? std::max(1, std::min(256, sf.doc.pals[target.sff].ncolors)) : 256;
    for (int i = ncol; i < 256; i++) sel[i] = 0;
    float cell = floorf(std::min(ImGui::GetContentRegionAvail().x / 16, em * 1.5f));
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("colours", ImVec2(cell * 16, cell * ((ncol + 15) / 16)));
    bool hovered = ImGui::IsItemHovered();
    ImVec2 m = ImGui::GetMousePos();
    int hx = (int)((m.x - p0.x) / cell), hy = (int)((m.y - p0.y) / cell);
    int hot = m.x >= p0.x && m.y >= p0.y && hx >= 0 && hx < 16 && hy >= 0 && hy * 16 + hx < ncol ? hy * 16 + hx : -1;
    ImGuiIO &io = ImGui::GetIO();
    if (moving >= 0 && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (hot >= 1 && hot != moving) {
            Pal_Move(*cols, moving, hot);
            select(hot, false);
            edit();
        }
        moving = -1;
    } else if (ImGui::IsItemClicked() && hot >= 1 && io.KeyAlt) {
        moving = hot;
    } else if (ImGui::IsItemClicked() && hot >= 0) {
        if (io.KeyShift && anchor >= 0) {
            std::fill(sel.begin(), sel.end(), 0);
            for (int i = std::min(anchor, hot); i <= std::max(anchor, hot); i++) sel[i] = 1;
            primary = hot;
        } else if (io.KeyCtrl) {
            sel[hot] = !sel[hot];
            anchor = primary = hot;
        } else {
            select(hot, false);
        }
    } else if (moving < 0 && ImGui::IsItemActive() && hot >= 0 && hot != primary && !io.KeyCtrl && ImGui::IsMouseDragging(0, 2)) {
        std::fill(sel.begin(), sel.end(), 0);
        for (int i = std::min(anchor, hot); i <= std::max(anchor, hot); i++) sel[i] = 1;
        primary = hot;
    }
    ImDrawList *draw = ImGui::GetWindowDrawList();
    for (int i = 0; i < ncol; i++) {
        ImVec2 a(p0.x + (i % 16) * cell, p0.y + (i / 16) * cell), b(a.x + cell, a.y + cell);
        const unsigned char *c = &(*cols)[i * 4];
        if (i == 0) {
            Checkerboard(draw, a, b); // transparent
        } else {
            draw->AddRectFilled(a, b, IM_COL32(c[0], c[1], c[2], 255));
        }
    }
    for (int i = 0; i < 256; i++) {
        if (!sel[i]) continue;
        ImVec2 a(p0.x + (i % 16) * cell, p0.y + (i / 16) * cell), b(a.x + cell, a.y + cell);
        draw->AddRect(a, b, IM_COL32(0, 0, 0, 255), 0, 3.0f);
        draw->AddRect(a, b, i == primary ? IM_COL32(255, 230, 80, 255) : IM_COL32(255, 255, 255, 255), 0, 1.5f);
    }
    if (moving >= 0 && hot >= 1) {
        ImVec2 a(p0.x + (hot % 16) * cell, p0.y + (hot / 16) * cell);
        draw->AddRect(a, ImVec2(a.x + cell, a.y + cell), IM_COL32(255, 230, 80, 255), 0, 3.0f);
    }
    if (hovered && hot >= 0 && ImGui::BeginTooltip()) {
        const unsigned char *c = &(*cols)[hot * 4];
        ImGui::Text("Colour %d: %d, %d, %d  #%02X%02X%02X%s", hot, c[0], c[1], c[2], c[0], c[1], c[2], hot == 0 ? " (transparent)" : "");
        ImGui::TextDisabled("Alt+drag: move it, those between shift");
        ImGui::EndTooltip();
    }
    int count = 0, first = -1, last = -1;
    for (int i = 0; i < 256; i++)
        if (sel[i]) {
            count++;
            if (first < 0) first = i;
            last = i;
        }
    mark = sel;
    if (primary < 0 || !sel[primary]) primary = first;

    // The colour picked
    if (primary >= 0) {
        unsigned char *c = &(*cols)[primary * 4];
        float f[3] = {c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f};
        ImGui::Spacing();
        ImGui::Text("Colour %d", primary);
        if (count > 1) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%d selected)", count);
        }
        ImGui::SetNextItemWidth(std::min(ImGui::GetContentRegionAvail().x, em * 14));
        if (ImGui::ColorPicker3("##colour", f, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_PickerHueBar)) {
            for (int k = 0; k < 3; k++) c[k] = (unsigned char)lroundf(f[k] * 255);
            edit();
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("Pick a colour, here or on the sprite.");
        ImGui::PopStyleColor();
    }

    // The colours selected: shifted, a gradient, copied
    ImGui::SeparatorText("Selected colours");
    ImGui::BeginDisabled(count == 0);
    static float dh, ds, dv, drgb[3];
    static std::vector<unsigned char> base;
    const char *names[6] = {"Hue", "Saturation", "Brightness", "Red", "Green", "Blue"};
    float *vals[6] = {&dh, &ds, &dv, &drgb[0], &drgb[1], &drgb[2]};
    for (int k = 0; k < 6; k++) {
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Brightness").x - style.ItemInnerSpacing.x);
        bool moved = k < 3 ? ImGui::SliderFloat(names[k], vals[k], k == 0 ? -0.5f : -1.0f, k == 0 ? 0.5f : 1.0f, "%+.2f")
                           : ImGui::SliderFloat(names[k], vals[k], -255.0f, 255.0f, "%+.0f");
        if (ImGui::IsItemActivated()) base = *cols; // a drag shifts from the colours it started with
        if (moved && base.size() == cols->size()) {
            for (int i = 1; i < 256; i++)
                if (sel[i]) {
                    unsigned char *c = &(*cols)[i * 4];
                    memcpy(c, &base[i * 4], 3);
                    Hsv(c, dh, ds, dv);
                    for (int b = 0; b < 3; b++) c[b] = (unsigned char)std::max(0, std::min(255, (int)c[b] + (int)drgb[b]));
                }
            edit();
        }
        if (ImGui::IsItemDeactivated()) dh = ds = dv = drgb[0] = drgb[1] = drgb[2] = 0;
        ImGui::SetItemTooltip("Drag to shift the colours selected (they stay when let go)");
    }
    ImGui::BeginDisabled(count < 3);
    if (ImGui::Button("Gradient")) {
        // From the first colour selected to the last, over those between
        const unsigned char *a = &(*cols)[first * 4], *b = &(*cols)[last * 4];
        int k = 0;
        for (int i = first; i <= last; i++)
            if (sel[i]) {
                float t = (float)k++ / (float)(count - 1);
                for (int j = 0; j < 3; j++) (*cols)[i * 4 + j] = (unsigned char)lroundf(a[j] + (b[j] - a[j]) * t);
            }
        edit();
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("The colours selected go from the first one to the last one");
    ImGui::SameLine();
    if (ImGui::Button("Copy")) {
        clip.clear();
        for (int i = 0; i < 256; i++)
            if (sel[i]) clip.insert(clip.end(), &(*cols)[i * 4], &(*cols)[i * 4 + 3]);
    }
    ImGui::SameLine();
    ImGui::EndDisabled();
    ImGui::BeginDisabled(clip.empty() || count == 0);
    if (ImGui::Button("Paste")) {
        // Over the colours selected, in order (one copied: over all of them)
        size_t k = 0, n = clip.size() / 3;
        for (int i = 0; i < 256 && n > 0; i++)
            if (sel[i]) {
                memcpy(&(*cols)[i * 4], &clip[(n == 1 ? 0 : k) * 3], 3);
                if (++k == n && n > 1) break;
            }
        edit();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("All")) {
        std::fill(sel.begin() + 1, sel.begin() + ncol, 1);
        primary = anchor = 1;
    }
    ImGui::SetItemTooltip("Select every colour but the transparent one");
    ImGui::BeginDisabled(count == 0);
    auto buttonFits = [&](const char *label) { SameLineFits(ImGui::CalcTextSize(label).x + style.FramePadding.x * 2); };
    if (ImGui::Button("Fill") && primary >= 0) {
        Pal_Fill(*cols, sel, &(*cols)[primary * 4]);
        edit();
    }
    ImGui::SetItemTooltip("The colours selected all take the colour picked");
    buttonFits("Reverse");
    if (ImGui::Button("Reverse")) {
        Pal_Reverse(*cols, sel);
        edit();
    }
    ImGui::SetItemTooltip("The colours selected in the other order");
    buttonFits("Sort");
    if (ImGui::Button("Sort")) {
        Pal_SortByTone(*cols, sel);
        edit();
    }
    ImGui::SetItemTooltip("By tone: greys first, then by hue, from dark to light");
    static int effect = 0;
    ImGui::SetNextItemWidth(em * 9);
    ImGui::Combo("##effect", &effect, [](void *, int i) { return Pal_EffectName(i); }, nullptr, PAL_EFFECTS);
    ImGui::SameLine();
    if (ImGui::Button("Apply")) {
        Pal_Effect(*cols, sel, effect);
        edit();
    }
    ImGui::EndDisabled();

    // The whole palette
    if (ImGui::CollapsingHeader("Files")) {
        static const char kReadFilter[] = "Palettes and images\0*.act;*.pal;*.aco;*.ase;*.gpl;*.png;*.pcx;*.bmp;*.gif\0All files (*.*)\0*.*\0";
        static const char kWriteFilter[] = "ACT (MUGEN)\0*.act\0PAL (JASC)\0*.pal\0ACO (Photoshop)\0*.aco\0GPL (GIMP)\0*.gpl\0";
        if (ImGui::Button("Import...")) {
            std::vector<std::string> files = PickFiles(kReadFilter, false);
            if (!files.empty()) {
                std::vector<unsigned char> rgba;
                std::string error, ext = files[0].substr(files[0].find_last_of('.') + 1);
                for (char &ch : ext) ch = (char)tolower((unsigned char)ch);
                bool image = ext == "png" || ext == "pcx" || ext == "bmp" || ext == "gif";
                if (image) {
                    // An indexed image's palette, or one made from its colours
                    SffDoc read;
                    Sff_SetImageReader(DecodeImage);
                    Sff_SpritesOfPngs(read, files, SFF_PAL_OWN, [](const std::string &m) { Log(LogLine::Info, m); });
                    if (!read.pals.empty()) rgba = read.pals[0].rgba;
                    else error = "No palette in " + files[0];
                } else {
                    Pal_Read(files[0], rgba, nullptr, &error);
                }
                if (rgba.size() == 1024) {
                    for (int i = 1; i < 256; i++) memcpy(&(*cols)[i * 4], &rgba[i * 4], 3);
                    edit();
                } else {
                    Log(LogLine::Error, error);
                }
            }
        }
        ImGui::SetItemTooltip("Its colours from a palette file (ACT, PAL, ACO, ASE, GPL) or an image");
        ImGui::SameLine();
        if (ImGui::Button("Export...")) {
            std::string path = PickSaveFile(kWriteFilter, "act", "palette.act"), error;
            if (!path.empty()) {
                if (Pal_Write(path, *cols, ncol, &error)) Log(LogLine::Info, "Saved " + path);
                else Log(LogLine::Error, error);
            }
        }
    }
    if (pn > 0) {
        // A new palette of the character's: SFF 1,N, the first number without one
        int free = 0;
        for (int n = 1; n <= 12 && !free; n++) {
            std::string act = CharFile(pn, ("pal" + std::to_string(n)).c_str());
            if ((act.empty() || !Act(act).ok) && Sff_FindPal(sf.doc, 1, n) < 0) free = n;
        }
        ImGui::BeginDisabled(!free);
        char label[48];
        snprintf(label, sizeof(label), free ? "Copy as palette %d" : "Copy as a new palette", free);
        if (ImGui::Button(label)) {
            SffPal p;
            p.group = 1, p.number = free;
            p.rgba = *cols;
            sf.doc.pals.push_back(p);
            g_madeHere.insert(PalKey(sf, 1, free));
            SpritesChanged(sf);
            Log(LogLine::Info, "Made palette 1," + std::to_string(free) + " in " + sf.path + ": the game sees it once saved");
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("A copy of these colours as the character's next palette (in the SFF, 1,N), for the "
                              "game once the SFF is saved");
    }

    // The sprites selected and this palette
    SffDoc &d = sf.doc;
    std::vector<int> sprites = SelectedSprites(sf);
    if (ImGui::CollapsingHeader("Sprites and palette")) {
        ImGui::TextDisabled("%zu sprites selected", sprites.size());
        ImGui::BeginDisabled(target.sff < 0 || sprites.empty());
        if (ImGui::Button("Use this palette")) {
            int n = Sff_UsePal(d, sprites, target.sff);
            Log(LogLine::Info, "Palette changed on " + std::to_string(n) + " sprites");
            if (n) SpritesChanged(sf);
        }
        ImGui::SetItemTooltip("They point to it: their pixels keep their indices, the colours change");
        if (ImGui::Button("Adapt them to it")) {
            int n = Sff_AdaptToPal(d, sprites, target.sff);
            Log(LogLine::Info, "Adapted " + std::to_string(n) + " sprites");
            if (n) SpritesChanged(sf);
        }
        ImGui::SetItemTooltip("Each pixel takes the nearest colour of this palette: they look as they did, as near as it allows");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(sprites.empty());
        if (ImGui::Button("New palette from them")) {
            SffPal p;
            while (Sff_FindPal(d, p.group, p.number) >= 0) p.number++;
            p.rgba = Sff_PalOfSprites(d, sprites);
            d.pals.push_back(p);
            SpritesChanged(sf);
            Log(LogLine::Info, "Made palette " + PalName(p) + " from " + std::to_string(sprites.size()) + " sprites");
            ImGui::EndDisabled();
            return; // the colours shown may have moved
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Holds their colours, the 255 that stand for them best when they have more");
    }

    // Every palette of the file
    if (ImGui::CollapsingHeader("All the palettes")) {
        std::vector<int> users(d.pals.size(), 0);
        for (const SffSprite &s : d.sprites)
            if (s.pal >= 0 && s.pal < (int)users.size()) users[s.pal]++;
        std::vector<PalChoice> choices;
        PalChoices(sf, pn, choices);
        static int listed = -1;
        float row = ImGui::GetTextLineHeightWithSpacing();
        if (ImGui::BeginChild("pals", ImVec2(0, row * std::min(8.0f, (float)d.pals.size() + 0.5f)), ImGuiChildFlags_Borders)) {
            ImGuiListClipper clip2;
            clip2.Begin((int)d.pals.size(), row);
            while (clip2.Step())
                for (int i = clip2.DisplayStart; i < clip2.DisplayEnd; i++) {
                    char label[48];
                    snprintf(label, sizeof(label), "%s  %d sprites##%d", PalName(d.pals[i]).c_str(), users[i], i);
                    if (ImGui::Selectable(label, listed == i || (listed < 0 && target.sff == i))) {
                        listed = i;
                        for (const PalChoice &c : choices)
                            if (c.sff == i) g_palWanted = c.label;
                    }
                }
        }
        ImGui::EndChild();
        int at = listed >= 0 && listed < (int)d.pals.size() ? listed : target.sff;
        bool valid = at >= 0 && at < (int)d.pals.size();
        ImGui::BeginDisabled(!valid);
        if (ImGui::Button(ICON_COPY " New")) {
            SffPal p = d.pals[at];
            p.number = 0;
            while (Sff_FindPal(d, p.group, p.number) >= 0) p.number++;
            d.pals.push_back(p);
            SpritesChanged(sf);
            ImGui::EndDisabled();
            return;
        }
        ImGui::SetItemTooltip("A copy of it, with the next free number of its group");
        ImGui::SameLine();
        ImGui::BeginDisabled(!valid || users[at] > 0);
        if (ImGui::Button(ICON_TRASH " Delete") && Sff_DeletePal(d, at)) {
            listed = -1;
            SpritesChanged(sf);
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            return;
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Only a palette no sprite uses");
        static int num[2], numOf = -1;
        ImGui::SetNextItemWidth(em * 7);
        if (numOf != at && valid) numOf = at, num[0] = d.pals[at].group, num[1] = d.pals[at].number;
        ImGui::InputInt2("##palnum", num);
        ImGui::SameLine();
        bool free = valid && Sff_FindPal(d, num[0], num[1]) < 0;
        ImGui::BeginDisabled(!free);
        if (ImGui::Button("Renumber")) {
            d.pals[at].group = num[0], d.pals[at].number = num[1];
            SpritesChanged(sf);
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            return;
        }
        ImGui::EndDisabled();
        if (valid && ImGui::Button(d.pals[at].ncolors == 32 ? "Make it 256 colours" : "Make it 32 colours")) {
            d.pals[at].ncolors = d.pals[at].ncolors == 32 ? 256 : 32;
            SpritesChanged(sf);
        }
        ImGui::SetItemTooltip("How many colours the file keeps: 32 only when its sprites use no more");
        ImGui::EndDisabled();
        if (ImGui::Button("Remove unused")) {
            int n = Sff_RemoveUnusedPals(d);
            Log(LogLine::Info, "Removed " + std::to_string(n) + " palettes");
            if (n) {
                listed = -1;
                SpritesChanged(sf);
                return;
            }
        }
        ImGui::SetItemTooltip("Palettes no sprite uses, but for the character's (1,N)");
        ImGui::SameLine();
        if (ImGui::Button("Merge alike")) {
            int n = Sff_MergeDuplicatePals(d);
            Log(LogLine::Info, "Merged " + std::to_string(n) + " palettes");
            if (n) {
                listed = -1;
                SpritesChanged(sf);
                return;
            }
        }
        ImGui::SetItemTooltip("Palettes with the same colours become one, their sprites going to it");
    }

    // PalFX: an effect tried on the sprite, written as a controller
    g_palFxOpen = ImGui::CollapsingHeader("PalFX");
    if (g_palFxOpen) {
        ImGui::Checkbox("See it on the sprite", &g_palFxOn);
        float w = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("SinAdd").x - style.ItemInnerSpacing.x;
        ImGui::SetNextItemWidth(w);
        ImGui::DragInt3("Add", g_palFx.add, 0.5f, -255, 255);
        ImGui::SetNextItemWidth(w);
        ImGui::DragInt3("Mul", g_palFx.mul, 0.5f, 0, 512);
        ImGui::SetNextItemWidth(w);
        ImGui::DragInt3("SinAdd", g_palFx.sinadd, 0.5f, -255, 255);
        ImGui::SetNextItemWidth(w);
        ImGui::DragInt("Period", &g_palFx.period, 0.2f, 0, 240);
        ImGui::SetItemTooltip("The sine's length in ticks; 0: no sine");
        ImGui::SetNextItemWidth(w);
        ImGui::SliderFloat("Colour", &g_palFx.color, 0, 1, "%.2f");
        ImGui::Checkbox("Invert", &g_palFx.invert);
        static int time = 30;
        ImGui::SetNextItemWidth(em * 5);
        ImGui::InputInt("Ticks", &time, 0);
        if (ImGui::Button(ICON_COPY " Copy as a controller")) ImGui::SetClipboardText(Pal_FxText(g_palFx, time).c_str());
        ImGui::SetItemTooltip("A PalFX state controller, ready to paste in a state");
        ImGui::SameLine();
        if (ImGui::Button("Reset")) g_palFx = PalFx();
    }
}

} // namespace app
