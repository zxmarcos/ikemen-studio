// Sprites: the selected player's SFF file.
#include "app_internal.h"

#include <commdlg.h>
#include <cfloat>
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

// A row of a panel: its label, then the field.
static void Label(const char *label) {
    float x = ImGui::GetCursorPosX();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine();
    ImGui::SetCursorPosX(x + ImGui::CalcTextSize("Number").x + ImGui::GetStyle().ItemSpacing.x * 2);
}

// The sprites, filtered by "group" or "group,number": a list (a small picture, the numbers, the
// size) or a grid of pictures. A click picks one; the arrows move from one to the next.
static void SpriteBrowser(SpriteFile &sf, const PalChoice &view, bool scroll, float height) {
    static std::string filter;
    static bool grid = false;
    SffDoc &d = sf.doc;
    float em = ImGui::GetFontSize();
    const ImGuiStyle &style = ImGui::GetStyle();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "group or group,number", &filter);
    for (int g = 0; g < 2; g++) {
        if (g) ImGui::SameLine(0, 0);
        bool on = grid == (g == 1);
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::SmallButton(g ? "Grid" : "List")) grid = g == 1;
        if (on) ImGui::PopStyleColor();
    }
    ImGui::SameLine();
    float addW = ImGui::CalcTextSize("Add PNG...").x + style.FramePadding.x * 2;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - addW));
    if (ImGui::SmallButton("Add PNG...")) ImGui::OpenPopup("add");
    ImGui::SetItemTooltip("Add sprites made of PNG files");

    int fg = -1, fn = -1;
    if (!filter.empty()) sscanf(filter.c_str(), "%d , %d", &fg, &fn);
    std::vector<int> shown;
    for (int i = 0; i < (int)d.sprites.size(); i++)
        if ((fg < 0 || d.sprites[i].group == fg) && (fn < 0 || d.sprites[i].number == fn)) shown.push_back(i);
    float h = std::max(em * 6, height - ImGui::GetFrameHeightWithSpacing() - ImGui::GetTextLineHeightWithSpacing() * 2);
    if (ImGui::BeginChild("sprites", ImVec2(0, h), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoNavInputs)) {
        int at = (int)(std::find(shown.begin(), shown.end(), sf.sel) - shown.begin());
        float cellW = em * 4.2f, cellH = em * 4.2f + ImGui::GetTextLineHeight(), rowH = std::max(em * 1.9f, ImGui::GetTextLineHeightWithSpacing());
        int cols = grid ? std::max(1, (int)((ImGui::GetContentRegionAvail().x + style.ItemSpacing.x) / (cellW + style.ItemSpacing.x))) : 1;
        // The arrows, with the list in focus
        if (ImGui::IsWindowFocused() && !shown.empty()) {
            int step = 0;
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) step = cols;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) step = -cols;
            if (grid && ImGui::IsKeyPressed(ImGuiKey_RightArrow)) step = 1;
            if (grid && ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) step = -1;
            if (step) {
                at = at >= (int)shown.size() ? 0 : std::max(0, std::min((int)shown.size() - 1, at + step));
                sf.sel = shown[at];
                scroll = true;
            }
        }
        float lineH = grid ? cellH + style.ItemSpacing.y : rowH;
        if (scroll && at < (int)shown.size()) {
            float y = (at / cols) * lineH;
            if (y < ImGui::GetScrollY() || y + lineH > ImGui::GetScrollY() + ImGui::GetWindowHeight())
                ImGui::SetScrollY(y - ImGui::GetWindowHeight() / 2 + lineH / 2);
        }
        ImDrawList *draw = ImGui::GetWindowDrawList();
        auto picture = [&](int i, ImVec2 a, float size) {
            draw->AddRectFilled(a, ImVec2(a.x + size, a.y + size), IM_COL32(40, 40, 44, 255));
            const SffSprite &s = d.sprites[i];
            if (GLuint tex = PalSpriteTexture(sf, i, view)) {
                float k = std::min(1.0f, std::min(size / std::max(1, s.w), size / std::max(1, s.h)));
                ImVec2 c(floorf(a.x + (size - s.w * k) * 0.5f), floorf(a.y + (size - s.h * k) * 0.5f));
                draw->AddImage(TexRef(tex), c, ImVec2(c.x + s.w * k, c.y + s.h * k));
            }
        };
        ImGuiListClipper clipper;
        clipper.Begin(((int)shown.size() + cols - 1) / cols, lineH);
        while (clipper.Step())
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
                for (int col = 0; col < cols; col++) {
                    int k = row * cols + col;
                    if (k >= (int)shown.size()) break;
                    int i = shown[k];
                    const SffSprite &s = d.sprites[i];
                    bool cur = sf.sel == i;
                    ImGui::PushID(i);
                    if (col) ImGui::SameLine();
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    ImVec2 size = grid ? ImVec2(cellW, cellH) : ImVec2(ImGui::GetContentRegionAvail().x, rowH - style.ItemSpacing.y * 0.5f);
                    if (ImGui::InvisibleButton("s", size)) sf.sel = i;
                    bool hot = ImGui::IsItemHovered();
                    if (cur || hot)
                        draw->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y),
                                            ImGui::GetColorU32(cur ? ImGuiCol_Header : ImGuiCol_HeaderHovered), style.FrameRounding);
                    char num[32], dim[32];
                    snprintf(num, sizeof(num), "%d, %d", s.group, s.number);
                    snprintf(dim, sizeof(dim), "%dx%d", s.w, s.h);
                    if (grid) {
                        float pic = cellW - em * 0.4f;
                        picture(i, ImVec2(p.x + em * 0.2f, p.y + em * 0.2f), pic);
                        ImVec2 t(p.x + (cellW - ImGui::CalcTextSize(num).x) * 0.5f, p.y + em * 0.2f + pic);
                        draw->AddText(t, ImGui::GetColorU32(ImGuiCol_Text), num);
                        if (hot) ImGui::SetTooltip("%s  %s", num, dim);
                    } else {
                        float pic = size.y - 2;
                        picture(i, ImVec2(p.x + 1, p.y + 1), pic);
                        float ty = p.y + (size.y - ImGui::GetTextLineHeight()) * 0.5f, x = p.x + pic + em * 0.5f;
                        draw->AddText(ImVec2(x, ty), ImGui::GetColorU32(ImGuiCol_Text), num);
                        float dimX = p.x + size.x - ImGui::CalcTextSize(dim).x - em * 0.3f;
                        if (dimX > x + ImGui::CalcTextSize(num).x + em * 0.5f) // the size when there is room
                            draw->AddText(ImVec2(dimX, ty), ImGui::GetColorU32(ImGuiCol_TextDisabled), dim);
                        else if (hot)
                            ImGui::SetTooltip("%s  %s", num, dim);
                    }
                    ImGui::PopID();
                }
        if (shown.empty()) ImGui::TextDisabled(d.sprites.empty() ? "No sprites." : "None in that group.");
    }
    ImGui::EndChild();
    ImGui::TextDisabled("%zu of %zu", shown.size(), d.sprites.size());
}

// The sprite: the wheel zooms at the mouse, a drag pans, Ctrl+drag moves the axis, a double click
// fits; picking (the Palette tab) or with Alt, a click picks the colour under the mouse. Returns
// the colour picked, -1 none; *shift: Shift was held.
static int SpritePreview(SpriteFile &sf, const PalChoice &view, const std::vector<char> *mark, bool picking, bool *shift,
                         float height) {
    static float zoom = 2;
    static ImVec2 origin; // the axis, from the canvas's corner
    static int fitFor = -1;
    static const SpriteFile *fitFile;
    static bool moved;
    SffDoc &d = sf.doc;
    SffSprite &s = d.sprites[sf.sel];
    float em = ImGui::GetFontSize();
    ImGuiIO &io = ImGui::GetIO();
    int picked = -1;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    bool open = ImGui::BeginChild("preview", ImVec2(0, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    if (!open) {
        ImGui::EndChild();
        return -1;
    }
    ImVec2 avail = ImGui::GetContentRegionAvail(), p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("canvas", ImVec2(std::max(avail.x, 32.0f), std::max(avail.y, 32.0f)),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    bool hovered = ImGui::IsItemHovered();
    static ImVec2 fitSize;
    bool resized = fabsf(fitSize.x - avail.x) > 0.5f || fabsf(fitSize.y - avail.y) > 0.5f;
    if (fitFor != sf.sel || fitFile != &sf || resized || (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))) {
        fitSize = avail;
        // A new sprite: the picture and its axis as large as fits (whole pixels when it can), at
        // most 4x, in the middle
        fitFor = sf.sel, fitFile = &sf;
        float x0 = (float)std::min(-s.x, 0), y0 = (float)std::min(-s.y, 0);
        float x1 = (float)std::max(s.w - s.x, 0), y1 = (float)std::max(s.h - s.y, 0);
        zoom = std::min(avail.x * 0.9f / std::max(1.0f, x1 - x0), avail.y * 0.9f / std::max(1.0f, y1 - y0));
        zoom = std::max(0.25f, std::min(4.0f, zoom >= 1 ? floorf(zoom) : zoom));
        origin = ImVec2(avail.x * 0.5f - (x0 + x1) * 0.5f * zoom, avail.y * 0.5f - (y0 + y1) * 0.5f * zoom);
    }
    if (hovered && io.MouseWheel != 0) {
        // The point under the mouse stays under it
        float before = zoom;
        zoom = std::max(0.25f, std::min(32.0f, zoom * powf(1.25f, io.MouseWheel)));
        ImVec2 m(io.MousePos.x - p0.x, io.MousePos.y - p0.y);
        origin = ImVec2(m.x - (m.x - origin.x) * zoom / before, m.y - (m.y - origin.y) * zoom / before);
    }
    if (ImGui::IsItemActivated()) moved = false;
    bool dragging = ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0));
    moved |= dragging;
    bool movingAxis = dragging && io.KeyCtrl && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if (dragging && !movingAxis) origin.x += io.MouseDelta.x, origin.y += io.MouseDelta.y;
    bool pick = (picking || io.KeyAlt) && s.pal >= 0;
    if (dragging || (hovered && io.KeyCtrl))
        ImGui::SetMouseCursor(movingAxis || !dragging ? ImGuiMouseCursor_ResizeAll : ImGuiMouseCursor_Hand);
    ImDrawList *draw = ImGui::GetWindowDrawList();
    ImVec2 p1(p0.x + avail.x, p0.y + avail.y);
    Checkerboard(draw, p0, p1);
    ImVec2 axisAt(floorf(p0.x + origin.x), floorf(p0.y + origin.y));
    if (movingAxis) {
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
    ImVec2 a(axisAt.x - s.x * zoom, axisAt.y - s.y * zoom);
    if (GLuint tex = PalSpriteTexture(sf, sf.sel, view, mark)) {
        draw->AddImage(TexRef(tex), a, ImVec2(a.x + s.w * zoom, a.y + s.h * zoom));
        draw->AddRect(a, ImVec2(a.x + s.w * zoom, a.y + s.h * zoom), IM_COL32(255, 255, 255, 60));
    } else {
        draw->AddText(ImVec2(p0.x + em * 0.5f, p0.y + em * 0.4f), IM_COL32(255, 120, 120, 255), "can't decode this sprite");
    }
    draw->AddLine(ImVec2(axisAt.x - em, axisAt.y), ImVec2(axisAt.x + em, axisAt.y), IM_COL32(255, 220, 60, 255));
    draw->AddLine(ImVec2(axisAt.x, axisAt.y - em), ImVec2(axisAt.x, axisAt.y + em), IM_COL32(255, 220, 60, 255));
    // Under the mouse: where, from the axis; its colour
    if (hovered) {
        int px = (int)floorf((io.MousePos.x - a.x) / zoom), py = (int)floorf((io.MousePos.y - a.y) / zoom);
        int colour = SpritePixel(sf, sf.sel, px, py);
        char info[96];
        int n = snprintf(info, sizeof(info), "%d, %d", px - s.x, py - s.y);
        if (colour >= 0) snprintf(info + n, sizeof(info) - n, "   colour %d", colour);
        ImVec2 t(p0.x + em * 0.5f, p1.y - ImGui::GetTextLineHeight() - em * 0.3f);
        draw->AddRectFilled(ImVec2(t.x - em * 0.25f, t.y), ImVec2(t.x + ImGui::CalcTextSize(info).x + em * 0.25f, p1.y - em * 0.2f),
                            IM_COL32(0, 0, 0, 140), 3.0f);
        draw->AddText(t, IM_COL32(255, 255, 255, 230), info);
        if (pick && !dragging) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (pick && ImGui::IsItemDeactivated() && !moved && colour >= 0 && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            picked = colour;
            *shift = io.KeyShift;
        }
    }
    ImGui::EndChild();
    return picked;
}

// The sprite's tab: its number, axis, what it is; replaced, saved, duplicated, deleted; the
// actions using it. Returns true when it was deleted.
static bool SpritePanel(SpriteFile &sf, int pn) {
    SffDoc &d = sf.doc;
    SffSprite &s = d.sprites[sf.sel];
    float em = ImGui::GetFontSize();
    static const char *formats[] = {"raw", "?", "RLE8", "RLE5", "LZ5", "?", "?", "?", "?", "?", "PNG8", "PNG24", "PNG32"};
    static int num[2], forSel = -1;
    static const SpriteFile *forFile;
    if (forSel != sf.sel || forFile != &sf) {
        forSel = sf.sel, forFile = &sf;
        num[0] = s.group, num[1] = s.number;
    }
    Label("Number");
    float renW = ImGui::CalcTextSize("Renumber").x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(std::max(em * 5, ImGui::GetContentRegionAvail().x - renW));
    ImGui::InputInt2("##num", num);
    ImGui::SetItemTooltip("Group, number");
    ImGui::SameLine();
    bool same = num[0] == s.group && num[1] == s.number;
    bool taken = !same && Sff_FindSprite(d, num[0], num[1]) >= 0;
    ImGui::BeginDisabled(taken || same || num[0] < 0 || num[1] < 0);
    if (ImGui::Button("Renumber")) {
        s.group = num[0], s.number = num[1];
        SpritesChanged(sf);
    }
    ImGui::EndDisabled();
    if (taken) ImGui::TextColored(Ink(kRed), "Sprite %d, %d exists already.", num[0], num[1]);
    Label("Axis");
    int axis[2] = {s.x, s.y};
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::DragInt2("##axis", axis, 0.2f)) {
        s.x = axis[0], s.y = axis[1];
        SpritesChanged(sf);
    }
    ImGui::SetItemTooltip("Where the axis is in the picture (Ctrl+drag on the preview moves it)");
    Label("Size");
    ImGui::Text("%d x %d", s.w, s.h);
    Label("Format");
    ImGui::Text("%s, %d bits", s.format >= 0 && s.format <= 12 ? formats[s.format] : "?", s.depth);
    Label("Palette");
    if (s.pal < 0) {
        ImGui::TextUnformatted("none (true colour)");
    } else if (s.pal < (int)d.pals.size()) {
        const SffPal &p = d.pals[s.pal];
        ImGui::Text("%d,%d%s", p.group, p.number, p.group == 1 && p.number == 1 ? " (the shared one)" : "");
    }
    for (int i = 0; i < sf.sel; i++)
        if (d.sprites[i].data == s.data) {
            Label("Linked");
            ImGui::Text("to %d, %d (the same picture)", d.sprites[i].group, d.sprites[i].number);
            break;
        }

    // The next button on the same line when it fits there
    auto flow = [](const char *label) {
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2) ImGui::NewLine();
    };
    ImGui::Spacing();
    if (ImGui::Button("Replace...")) {
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
    ImGui::SetItemTooltip("Its picture from a PNG file (numbers and axis kept)");
    flow("Save PNG...");
    if (ImGui::Button("Save PNG...")) {
        std::string name = "sprite_" + std::to_string(s.group) + "_" + std::to_string(s.number) + ".png";
        std::string path = PickSaveFile(kPngFilter, "png", name), error;
        if (!path.empty()) {
            if (Sff_SavePng(d, s, path, &error))
                Log(LogLine::Info, "Saved " + path);
            else
                Log(LogLine::Error, error);
        }
    }
    flow("Duplicate");
    if (ImGui::Button("Duplicate")) {
        SffSprite copy = s;
        while (Sff_FindSprite(d, copy.group, copy.number) >= 0) copy.number++;
        d.sprites.insert(d.sprites.begin() + sf.sel + 1, copy);
        sf.sel++;
        SpritesChanged(sf);
        return true;
    }
    ImGui::SetItemTooltip("A copy with the next free number in its group");
    flow("Delete");
    if (ImGui::Button("Delete")) {
        d.sprites.erase(d.sprites.begin() + sf.sel);
        sf.sel = std::min(sf.sel, (int)d.sprites.size() - 1);
        SpritesChanged(sf);
        return true;
    }

    // The character's actions showing it
    std::string air = pn > 0 ? CharFile(pn, "anim") : "";
    if (!air.empty()) {
        ImGui::SeparatorText("Used by");
        const AirFile &af = AirOf(LoadSource(air));
        int n = 0;
        for (const auto &kv : af.actions)
            for (size_t e = 0; e < kv.second.elems.size(); e++) {
                const AirElem &el = kv.second.elems[e];
                if (el.group != s.group || el.image != s.number) continue;
                if (++n > 30) continue;
                char label[64];
                snprintf(label, sizeof(label), "Action %d, element %zu##%d", kv.first, e + 1, n);
                if (ImGui::Selectable(label)) {
                    g_animAction = kv.first, g_animElem = (int)e + 1, g_animFollow = false;
                    ShowWindow("Animation");
                }
            }
        if (n > 30) ImGui::TextDisabled("and %d more", n - 30);
        if (n == 0) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("No action of %s shows it.", air.c_str());
            ImGui::PopStyleColor();
        }
    }
    return false;
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
    int pn = 0; // the character whose sprites they are
    if (!spf) {
        // The character worked on; a player showing another's animation (a custom state): that one's
        pn = WorkPn();
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
    float em = ImGui::GetFontSize();
    const ImGuiStyle &style = ImGui::GetStyle();
    PalFrame(sf, pn);
    if (sf.sel < 0 && !d.sprites.empty()) sf.sel = 0;

    // The palettes the sprites are seen with: the player's at first
    std::vector<PalChoice> choices;
    PalChoices(sf, pn, choices);
    static std::map<std::string, std::string> chosen; // file -> the label chosen
    std::string &label = chosen[NormPath(sf.path)];
    int choice = 0;
    for (int i = 0; i < (int)choices.size(); i++)
        if (choices[i].label == label) choice = i;
    if (label.empty() && pn > 0) {
        int want = g_palNo.count(pn) && g_palNo[pn] > 0 ? g_palNo[pn] : 1;
        for (int i = 0; i < (int)choices.size(); i++)
            if (choices[i].slot == want) choice = i;
    }
    const PalChoice &view = choices[choice];

    // Whose, the file, Save and Revert
    bool dirty = sf.dirty || PalDirty();
    ImGui::AlignTextToFramePadding();
    if (screen)
        ImGui::Text("The %s", screen);
    else
        ImGui::TextUnformatted(WorkLabel(pn).c_str());
    ImGui::SameLine();
    size_t slash = sf.path.find_last_of("/\\");
    ImGui::TextDisabled("· %s%s · %zu sprites", slash == std::string::npos ? sf.path.c_str() : sf.path.c_str() + slash + 1,
                        dirty ? " *" : "", d.sprites.size());
    ImGui::SetItemTooltip("%s: %zu sprites, %zu palettes", sf.path.c_str(), d.sprites.size(), d.pals.size());
    {
        float w = ImGui::CalcTextSize("Save").x + ImGui::CalcTextSize("Revert").x + style.FramePadding.x * 4 + style.ItemSpacing.x;
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < w)
            ImGui::NewLine();
        else
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - w);
        ImGui::BeginDisabled(!dirty);
        if (dirty) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        bool save = ImGui::Button("Save");
        if (dirty) ImGui::PopStyleColor();
        if (save) {
            PalSave(); // the ACT files first: the game reads them with the SFF
            if (sf.dirty) SaveSprites(sf);
        }
        ImGui::SetItemTooltip("Writes the SFF file and the ACT files changed (the first time, the old ones are kept as "
                              ".bak); the game reads the SFF again.");
        ImGui::SameLine();
        if (ImGui::Button("Revert")) {
            if (sf.dirty) {
                Sff_Load(d, GamePath(sf.path), sf.pal.empty() ? "" : GamePath(sf.pal));
                sf.dirty = false;
                sf.version++;
            }
            PalRevert(sf, pn);
        }
        ImGui::EndDisabled();
    }
    if (!d.error.empty()) ImGui::TextColored(Ink(kRed), "%s", d.error.c_str());
    if (d.converted) ImGui::TextColored(Ink(kYellow), "An SFF v1 file: saving writes SFF v2 (palette 1,1 from the .act).");

    static int addGroup = 0, addNumber = 0, addAxis = SFF_AXIS_FEET, addPal = SFF_PAL_SHARED;
    auto addPopup = [&]() {
        if (!ImGui::BeginPopup("add")) return;
        ImGui::SetNextItemWidth(em * 8);
        ImGui::InputInt("Group", &addGroup, 0);
        ImGui::SetNextItemWidth(em * 8);
        ImGui::InputInt("From number", &addNumber, 0);
        ImGui::Combo("Axis", &addAxis, "At the feet\0In the centre\0Top left\0");
        ImGui::Combo("Palette", &addPal, screen ? "The shared one (1,1)\0Their own\0" : "The character's (1,1)\0Their own\0");
        if (ImGui::Button("Choose the files...")) {
            std::vector<std::string> files = PickFiles(kPngFilter, true);
            if (!files.empty()) AddSprites(sf, files, addGroup, addNumber, addAxis, addPal);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    };

    // The sprites | the preview | the sprite's and the palette's tabs (under the preview when narrow)
    float h = std::max(em * 14, ImGui::GetContentRegionAvail().y - style.CellPadding.y * 2 - 2);
    bool wide = ImGui::GetContentRegionAvail().x > em * 52;
    if (!ImGui::BeginTable("sprites", wide ? 3 : 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        ImGui::End();
        return;
    }
    ImGui::TableSetupColumn("list", ImGuiTableColumnFlags_WidthFixed, wide ? em * 14 : em * 11);
    ImGui::TableSetupColumn("preview", ImGuiTableColumnFlags_WidthStretch);
    if (wide) ImGui::TableSetupColumn("panel", ImGuiTableColumnFlags_WidthFixed, em * 19);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    SpriteBrowser(sf, view, scroll, h);
    addPopup();
    ImGui::TableSetColumnIndex(1);
    if (sf.sel < 0 || sf.sel >= (int)d.sprites.size()) {
        ImGui::TextDisabled("Pick a sprite.");
        ImGui::EndTable();
        ImGui::End();
        return;
    }
    // The preview's toolbar: the palette seen, the zoom
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Palette");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::min(em * 16, ImGui::GetContentRegionAvail().x - em * 2));
    if (ImGui::BeginCombo("##palette", view.label.c_str(), ImGuiComboFlags_HeightLarge)) {
        for (int i = 0; i < (int)choices.size(); i++) {
            if (i == 1 && pn > 0) ImGui::SeparatorText("The character's");
            if (ImGui::Selectable(choices[i].label.c_str(), i == choice)) label = choices[i].label;
            if (i == 0) ImGui::SetItemTooltip("Each sprite with the palette the SFF gives it");
        }
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip(pn > 0 ? "The character's palettes take the place of the shared one (1,1), as on the game"
                                 : "Every sprite with that palette");
    HelpMarker("Wheel: zoom at the mouse. Drag or middle drag: pan. Ctrl+drag: move the axis. Double click: fit. "
               "With the Palette tab (or Alt held), a click picks the colour under the mouse; Shift+click adds it.");
    static int tab = 0; // 0 the sprite, 1 the palette
    static std::vector<char> mark;
    static int colour = -1; // picked on the sprite, for the Palette tab (shown from the next frame)
    static bool shift = false;
    float previewH = wide ? h - ImGui::GetFrameHeightWithSpacing() : std::max(em * 10, (h - ImGui::GetFrameHeightWithSpacing()) * 0.5f);
    bool now = false;
    if (int c = SpritePreview(sf, view, tab == 1 ? &mark : nullptr, tab == 1, &now, previewH); c >= 0) colour = c, shift = now;
    if (!wide) ImGui::Spacing();
    else ImGui::TableSetColumnIndex(2);
    // The tabs
    bool stop = false;
    if (ImGui::BeginChild("panel", ImVec2(0, wide ? h : 0))) {
        if (ImGui::BeginTabBar("tabs")) {
            if (ImGui::BeginTabItem("Sprite")) {
                tab = 0;
                stop = SpritePanel(sf, pn);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Palette", nullptr, colour >= 0 && tab != 1 ? ImGuiTabItemFlags_SetSelected : 0)) {
                tab = 1;
                PalettePanel(sf, pn, PalFor(sf, view, sf.sel), colour, shift, mark);
                colour = -1;
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::EndChild();
    (void)stop;
    ImGui::EndTable();
    ImGui::End();
}

} // namespace app
