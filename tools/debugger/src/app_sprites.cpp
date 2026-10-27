// Sprites: the selected player's SFF file.
#include "app_internal.h"

#include <commdlg.h>
#include <cmath>
#include <cstring>
#include <fstream>

#include "imgui_stdlib.h"

namespace app {

std::map<std::string, SpriteFile> g_spriteFiles; // by path

static SpriteFile *g_spritesShown; // a file another window showed here
static std::string g_spritesShownWhat;
static int g_spritesShownN;

void ShowInSprites(SpriteFile &sf, int index, const std::string &what) {
    g_spritesShown = &sf;
    g_spritesShownWhat = what;
    if (index >= 0) sf.sel = index;
    g_spritesShownN++;
}

std::string CharFile(int pn, const char *key) {
    const auto *files = FilesFor(pn);
    if (!files) return "";
    auto f = files->find(key);
    return f == files->end() || f->second.empty() ? "" : f->second[0];
}

void SpriteLog(const std::string &s) { Log(LogLine::Info, s); }

SpriteFile &SpriteFileAt(const std::string &path, const std::string &pal) {
    auto it = g_spriteFiles.find(NormPath(path));
    if (it != g_spriteFiles.end()) return it->second;
    SpriteFile &sf = g_spriteFiles[NormPath(path)];
    sf.path = path;
    sf.pal = pal;
    Sff_Load(sf.doc, GamePath(path), pal.empty() ? "" : GamePath(pal));
    return sf;
}

// The SFF file of a player's sprites (nullptr until the game has told its files).
SpriteFile *SpritesOf(int pn) {
    std::string path = CharFile(pn, "sprite");
    if (path.empty()) {
        AirPath(pn); // asks for the files
        return nullptr;
    }
    return &SpriteFileAt(path, CharFile(pn, "pal1"));
}

void SpritesChanged(SpriteFile &sf) {
    sf.dirty = true;
    sf.version++;
}

// Textures of sprites, made when asked for and again after a change.
struct SpriteTex {
    GLuint tex = 0;
    unsigned version = 0;
    int pal = -2;
};
std::map<std::pair<const SpriteFile *, int>, SpriteTex> g_spriteTex;

GLuint SpriteTexture(SpriteFile &sf, int index, int pal) {
    if (index < 0 || index >= (int)sf.doc.sprites.size()) return 0;
    SpriteTex &t = g_spriteTex[{&sf, index}];
    if (t.tex && t.version == sf.version && t.pal == pal) return t.tex;
    const SffSprite &s = sf.doc.sprites[index];
    std::vector<unsigned char> px, rgba;
    int bpp = 0;
    t.version = sf.version;
    t.pal = pal;
    if (!Sff_Decode(s, px, &bpp)) return 0;
    Sff_ToRgba(sf.doc, s, px, bpp, rgba, pal);
    if (!t.tex) glGenTextures(1, &t.tex);
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

ImTextureRef TexRef(GLuint tex) { return ImTextureRef((ImTextureID)(intptr_t)tex); }

// Windows file dialogs (ANSI paths).
std::vector<std::string> PickFiles(const char *filter, bool multi) {
    // Tests (scripts/test_ui.ps1) give the files picked: paths separated by '|'
    if (const char *test = getenv("IKDBG_TEST_PICK"); test && *test) {
        std::vector<std::string> out;
        for (std::string s = test;;) {
            size_t bar = s.find('|');
            out.push_back(s.substr(0, bar));
            if (bar == std::string::npos || !multi) return out;
            s = s.substr(bar + 1);
        }
    }
    std::vector<char> buf(65536, 0);
    OPENFILENAMEA ofn = {sizeof(ofn)};
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = (DWORD)buf.size();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR | (multi ? OFN_ALLOWMULTISELECT : 0);
    std::vector<std::string> out;
    if (!GetOpenFileNameA(&ofn)) return out;
    // One file: its path. Several: the folder, then the names, each 0-ended
    std::string first = buf.data();
    const char *p = buf.data() + first.size() + 1;
    if (*p == 0) {
        out.push_back(first);
        return out;
    }
    for (; *p; p += strlen(p) + 1) out.push_back(first + "\\" + p);
    return out;
}

std::string PickSaveFile(const char *filter, const char *ext, const std::string &name) {
    std::vector<char> buf(MAX_PATH * 4, 0);
    snprintf(buf.data(), buf.size(), "%s", name.c_str());
    OPENFILENAMEA ofn = {sizeof(ofn)};
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = (DWORD)buf.size();
    ofn.lpstrDefExt = ext;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_EXPLORER | OFN_NOCHANGEDIR;
    return GetSaveFileNameA(&ofn) ? std::string(buf.data()) : std::string();
}

const char kPngFilter[] = "PNG files (*.png)\0*.png\0All files (*.*)\0*.*\0";

// Writes the SFF file (the first time, the old one is kept as .bak) and has the game read it again.
void SaveSprites(SpriteFile &sf) {
    std::string path = GamePath(sf.path), bak = path + ".bak";
    if (GetFileAttributesA(bak.c_str()) == INVALID_FILE_ATTRIBUTES) CopyFileA(path.c_str(), bak.c_str(), TRUE);
    std::ofstream out(path, std::ios::binary);
    out << Sff_Build(sf.doc);
    out.close();
    if (!out) {
        Log(LogLine::Error, "Could not write " + path);
        return;
    }
    sf.dirty = false;
    sf.doc.converted = false;
    Log(LogLine::Info, "Saved " + path);
    ScreenSpritesSaved(sf.path);
    FontSpritesSaved(sf.path);
    for (const auto &kv : g_charFiles) {
        auto f = kv.second.find("sprite");
        if (f == kv.second.end() || f->second.empty() || NormPath(f->second[0]) != NormPath(sf.path)) continue;
        SendRaw("ikdbg.sprites(" + std::to_string(kv.first) + ")"); // every player using the file
        // The game compiled the AIR file on disk again: changes not saved go back
        std::string air = CharFile(kv.first, "anim");
        if (!air.empty()) {
            SourceFile &a = LoadSource(air);
            if (a.Dirty()) ApplyAnims(a);
        }
        break;
    }
}

void AddSprites(SpriteFile &sf, const std::vector<std::string> &files, int group, int number, int axis, int palMode) {
    std::vector<SffSprite> made = Sff_SpritesOfPngs(sf.doc, files, palMode, SpriteLog);
    for (SffSprite &s : made) {
        while (Sff_FindSprite(sf.doc, group, number) >= 0) number++;
        s.group = group;
        s.number = number;
        Sff_SetAxis(s, axis);
        sf.doc.sprites.push_back(s);
        sf.sel = (int)sf.doc.sprites.size() - 1;
        Log(LogLine::Info, "Added sprite " + std::to_string(group) + ", " + std::to_string(number));
    }
    if (!made.empty()) SpritesChanged(sf);
}

// A checkerboard behind sprites, so that their transparent pixels show.
void Checkerboard(ImDrawList *draw, ImVec2 a, ImVec2 b) {
    float cell = ImGui::GetFontSize() * 0.6f;
    draw->AddRectFilled(a, b, IM_COL32(60, 60, 64, 255));
    for (float y = a.y; y < b.y; y += cell)
        for (float x = a.x + (((int)((y - a.y) / cell)) & 1) * cell; x < b.x; x += cell * 2)
            draw->AddRectFilled(ImVec2(x, y), ImVec2(std::min(x + cell, b.x), std::min(y + cell, b.y)),
                                IM_COL32(80, 80, 86, 255));
}

void SpritesWindow() {
    if (!BeginTool("Sprites")) return;
    // Whose: the selected player's, the SFF of the screen whose element was picked last, or the
    // file another window showed here (a font's), whichever came last
    enum { Player, Screen, Shown };
    static int mode = Player;
    static int seenSel = -2, seenSelPicked = 0, seenPicked = 0, seenShown = 0;
    if (g_selId != seenSel || g_selPicked != seenSelPicked) seenSel = g_selId, seenSelPicked = g_selPicked, mode = Player;
    static std::string seenWork;
    if (std::string work = WorkDef(); work != seenWork) seenWork = work, mode = Player;
    bool picked = g_screenPicked != seenPicked, scroll = false;
    if (picked) seenPicked = g_screenPicked, mode = Screen;
    if (g_spritesShownN != seenShown) seenShown = g_spritesShownN, mode = Shown, scroll = true;
    const char *screen = nullptr;
    int shows = -1; // the sprite the element picked shows
    SpriteFile *spf = mode == Screen ? SelectedScreenSprites(&screen, picked ? &shows : nullptr)
                    : mode == Shown ? g_spritesShown : nullptr;
    if (mode == Shown) screen = g_spritesShownWhat.c_str();
    if (spf && shows >= 0) spf->sel = shows, scroll = true;
    if (!spf) {
        // The character worked on; a player showing another's animation (a custom state): that one's
        int pn = WorkPn();
        const Entity *e = FindEntity(g_selId);
        const FrameEntity *fe = FindFrameEntity(g_selId);
        if (e && e->pn == pn && fe && fe->animPn > 0) pn = fe->animPn;
        spf = pn > 0 ? SpritesOf(pn) : nullptr;
        if (!spf) {
            ImGui::TextDisabled(!pn ? "Pick a player or a character in Explorer, or an element of a screen (Fight "
                                      "screen, Motif, Storyboard)."
                                : g_canTrace ? "Waiting for the files of P%d..." : "Needs the patched Ikemen GO.", pn);
            ImGui::End();
            return;
        }
    }
    SpriteFile &sf = *spf;
    SffDoc &d = sf.doc;
    ImGui::BeginDisabled(!sf.dirty);
    if (ImGui::Button("Save")) SaveSprites(sf);
    ImGui::SetItemTooltip("Writes the SFF file (the old one is kept as .bak the first time) and the game reads it again.");
    ImGui::SameLine();
    if (ImGui::Button("Revert")) {
        Sff_Load(d, GamePath(sf.path), sf.pal.empty() ? "" : GamePath(sf.pal));
        sf.dirty = false;
        sf.version++;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Add PNG files...")) ImGui::OpenPopup("add");
    ImGui::SameLine();
    ImGui::TextDisabled("%s%s%s%s%s  %zu sprites, %zu palettes", screen ? "the " : "", screen ? screen : "",
                        screen ? "'s " : "", sf.path.c_str(), sf.dirty ? " *" : "", d.sprites.size(), d.pals.size());
    if (!d.error.empty()) ImGui::TextColored(Ink(kRed), "%s", d.error.c_str());
    if (d.converted)
        ImGui::TextColored(Ink(kYellow), "An SFF v1 file: saving writes SFF v2 (palette 1,1 from the .act).");

    static int addGroup = 0, addNumber = 0, addAxis = SFF_AXIS_FEET, addPal = SFF_PAL_SHARED;
    if (ImGui::BeginPopup("add")) {
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
        ImGui::InputInt("Group", &addGroup, 0);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
        ImGui::InputInt("From number", &addNumber, 0);
        ImGui::Combo("Axis", &addAxis, "At the feet\0In the centre\0Top left\0");
        ImGui::Combo("Palette", &addPal, screen ? "The shared one (1,1)\0Their own\0" : "The character's (1,1)\0Their own\0");
        if (ImGui::Button("Choose the files...")) {
            std::vector<std::string> files = PickFiles(kPngFilter, true);
            if (!files.empty()) AddSprites(sf, files, addGroup, addNumber, addAxis, addPal);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Left: the sprites (filter by "group" or "group,number")
    static std::string filter;
    float listW = ImGui::GetFontSize() * 13;
    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(listW);
    ImGui::InputTextWithHint("##filter", "group or group,number", &filter);
    int fg = -1, fn = -1;
    if (!filter.empty()) sscanf(filter.c_str(), "%d , %d", &fg, &fn);
    std::vector<int> shown;
    for (int i = 0; i < (int)d.sprites.size(); i++)
        if ((fg < 0 || d.sprites[i].group == fg) && (fn < 0 || d.sprites[i].number == fn)) shown.push_back(i);
    if (ImGui::BeginChild("list", ImVec2(listW, 0), ImGuiChildFlags_Borders)) {
        auto at = std::find(shown.begin(), shown.end(), sf.sel);
        if (scroll && at != shown.end())
            ImGui::SetScrollY((at - shown.begin()) * ImGui::GetTextLineHeightWithSpacing() - ImGui::GetWindowHeight() / 2);
        ImGuiListClipper clipper;
        clipper.Begin((int)shown.size());
        while (clipper.Step())
            for (int k = clipper.DisplayStart; k < clipper.DisplayEnd; k++) {
                const SffSprite &s = d.sprites[shown[k]];
                char label[64];
                snprintf(label, sizeof(label), "%d, %d  %dx%d##%d", s.group, s.number, s.w, s.h, shown[k]);
                if (ImGui::Selectable(label, sf.sel == shown[k])) sf.sel = shown[k];
            }
    }
    ImGui::EndChild();
    ImGui::EndGroup();
    ImGui::SameLine();

    // Right: the sprite
    ImGui::BeginGroup();
    if (sf.sel < 0 || sf.sel >= (int)d.sprites.size()) {
        ImGui::TextDisabled("Pick a sprite.");
        ImGui::EndGroup();
        ImGui::End();
        return;
    }
    SffSprite &s = d.sprites[sf.sel];
    static const char *formats[] = {"raw", "?", "RLE8", "RLE5", "LZ5", "?", "?", "?", "?", "?", "PNG8", "PNG24", "PNG32"};
    ImGui::Text("%d, %d: %d x %d, %s, %d bits", s.group, s.number, s.w, s.h,
                s.format >= 0 && s.format <= 12 ? formats[s.format] : "?", s.depth);
    static int num[2], forSel = -1;
    static const SpriteFile *forFile;
    if (forSel != sf.sel || forFile != &sf) {
        forSel = sf.sel, forFile = &sf;
        num[0] = s.group, num[1] = s.number;
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
    ImGui::InputInt2("##num", num);
    ImGui::SameLine();
    bool taken = (num[0] != s.group || num[1] != s.number) && Sff_FindSprite(d, num[0], num[1]) >= 0;
    ImGui::BeginDisabled(taken || (num[0] == s.group && num[1] == s.number) || num[0] < 0 || num[1] < 0);
    if (ImGui::Button("Renumber")) {
        s.group = num[0], s.number = num[1];
        SpritesChanged(sf);
    }
    ImGui::EndDisabled();
    if (taken) {
        ImGui::SameLine();
        ImGui::TextColored(Ink(kRed), "taken");
    }
    int axis[2] = {s.x, s.y};
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
    if (ImGui::DragInt2("Axis", axis, 0.2f)) {
        s.x = axis[0], s.y = axis[1];
        SpritesChanged(sf);
    }
    // The palettes the sprite can be seen with
    static int viewPal = -1;
    std::string palName = viewPal < 0 || viewPal >= (int)d.pals.size()
                              ? std::string("its own")
                              : std::to_string(d.pals[viewPal].group) + ", " + std::to_string(d.pals[viewPal].number);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
    if (ImGui::BeginCombo("Palette", palName.c_str())) {
        if (ImGui::Selectable("its own", viewPal < 0)) viewPal = -1;
        for (int i = 0; i < (int)d.pals.size(); i++) {
            std::string n = std::to_string(d.pals[i].group) + ", " + std::to_string(d.pals[i].number);
            if (ImGui::Selectable(n.c_str(), viewPal == i)) viewPal = i;
        }
        ImGui::EndCombo();
    }
    if (ImGui::Button("Replace with a PNG...")) {
        std::vector<std::string> files = PickFiles(kPngFilter, false);
        if (!files.empty()) {
            std::vector<SffSprite> made = Sff_SpritesOfPngs(d, files, s.pal >= 0 && d.pals[s.pal].group == 1 ? SFF_PAL_SHARED : SFF_PAL_OWN, SpriteLog);
            if (!made.empty()) {
                SffSprite &n = made[0];
                n.group = s.group, n.number = s.number, n.x = s.x, n.y = s.y;
                d.sprites[sf.sel] = n;
                SpritesChanged(sf);
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save as a PNG...")) {
        std::string name = "sprite_" + std::to_string(s.group) + "_" + std::to_string(s.number) + ".png";
        std::string path = PickSaveFile(kPngFilter, "png", name), error;
        if (!path.empty()) {
            if (Sff_SavePng(d, s, path, &error))
                Log(LogLine::Info, "Saved " + path);
            else
                Log(LogLine::Error, error);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        d.sprites.erase(d.sprites.begin() + sf.sel);
        sf.sel = std::min(sf.sel, (int)d.sprites.size() - 1);
        SpritesChanged(sf);
        ImGui::EndGroup();
        ImGui::End();
        return;
    }

    // The preview: wheel zooms, a drag moves the axis
    static float zoom = 2;
    static int fitFor = -1;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (fitFor != sf.sel) {
        // A new sprite: as large as fits (the axis is at two thirds of the height), at most 4x
        fitFor = sf.sel;
        float zx = avail.x * 0.9f / std::max(1, s.w), zy = avail.y * 0.6f / std::max(1, std::max(s.y, s.h - s.y) + 1);
        zoom = std::max(0.25f, std::min(4.0f, std::min(zx, zy)));
    }
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("preview", ImVec2(std::max(avail.x, 32.0f), std::max(avail.y, 32.0f)));
    bool hovered = ImGui::IsItemHovered();
    ImGuiIO &io = ImGui::GetIO();
    if (hovered && io.MouseWheel != 0) zoom = std::max(0.25f, std::min(16.0f, zoom * powf(1.25f, io.MouseWheel)));
    ImDrawList *draw = ImGui::GetWindowDrawList();
    ImVec2 p1(p0.x + avail.x, p0.y + avail.y);
    draw->PushClipRect(p0, p1, true);
    Checkerboard(draw, p0, p1);
    // The axis sits in the middle, at two thirds of the height
    ImVec2 axisAt(floorf(p0.x + avail.x * 0.5f), floorf(p0.y + avail.y * 0.67f));
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0)) {
        static ImVec2 rest;
        rest.x += io.MouseDelta.x / zoom;
        rest.y += io.MouseDelta.y / zoom;
        int dx = (int)rest.x, dy = (int)rest.y;
        if (dx || dy) {
            s.x -= dx, s.y -= dy; // the image moves with the mouse: the axis moves the other way within it
            rest.x -= dx, rest.y -= dy;
            SpritesChanged(sf);
        }
    }
    if (GLuint tex = SpriteTexture(sf, sf.sel, viewPal)) {
        ImVec2 a(axisAt.x - s.x * zoom, axisAt.y - s.y * zoom);
        draw->AddImage(TexRef(tex), a, ImVec2(a.x + s.w * zoom, a.y + s.h * zoom));
        draw->AddRect(a, ImVec2(a.x + s.w * zoom, a.y + s.h * zoom), IM_COL32(255, 255, 255, 60));
    } else {
        draw->AddText(p0, IM_COL32(255, 120, 120, 255), "can't decode this sprite");
    }
    float r = ImGui::GetFontSize();
    draw->AddLine(ImVec2(axisAt.x - r, axisAt.y), ImVec2(axisAt.x + r, axisAt.y), IM_COL32(255, 220, 60, 255));
    draw->AddLine(ImVec2(axisAt.x, axisAt.y - r), ImVec2(axisAt.x, axisAt.y + r), IM_COL32(255, 220, 60, 255));
    draw->PopClipRect();
    if (hovered) ImGui::SetTooltip("Wheel: zoom (%.2gx). Drag: move the axis.", zoom);
    ImGui::EndGroup();
    ImGui::End();
}

} // namespace app
