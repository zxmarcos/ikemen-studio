// Sprites: the selected player's SFF file.
#include "app_internal.h"

#include <commdlg.h>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <fstream>

#include "imgui_stdlib.h"
#include "sffedit.h"

namespace app {

std::map<std::string, SpriteFile> g_spriteFiles; // by path

static SpriteFile *g_spritesShown; // a file another window showed here
static std::string g_spritesShownWhat;
static int g_spritesShownN;

void ShowInSprites(SpriteFile &sf, int index, const std::string &what) {
    g_spritesShown = &sf;
    g_spritesShownWhat = what;
    if (index >= 0) sf.sel = index, sf.picks.clear();
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
    sf.edits++;
}

float UndoButtonsWidth() {
    const ImGuiStyle &style = ImGui::GetStyle();
    return ImGui::CalcTextSize(ICON_BACK).x + ImGui::CalcTextSize(ICON_AGAIN).x + style.FramePadding.x * 4 + style.ItemSpacing.x;
}

int UndoButtons(bool canUndo, bool canRedo) {
    ImGuiIO &io = ImGui::GetIO();
    bool keys = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput && io.KeyCtrl;
    ImGui::BeginDisabled(!canUndo);
    bool undo = ImGui::Button(ICON_BACK "##undo");
    ImGui::EndDisabled();
    ImGui::SetItemTooltip(canUndo ? "Undo (Ctrl+Z)" : "No change to undo");
    ImGui::SameLine();
    ImGui::BeginDisabled(!canRedo);
    bool redo = ImGui::Button(ICON_AGAIN "##redo");
    ImGui::EndDisabled();
    ImGui::SetItemTooltip(canRedo ? "Do again (Ctrl+Y)" : "Nothing undone to do again");
    undo |= keys && canUndo && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z);
    redo |= keys && canRedo && (ImGui::IsKeyPressed(ImGuiKey_Y) || (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)));
    return undo ? -1 : redo ? 1 : 0;
}

// A change is one step of undo when the mouse lets go of it (a colour dragged, an axis moved).
static void TrackSprites(SpriteFile &sf, int pn) {
    SpriteState st;
    if (!sf.history.Started()) {
        st.sprites = sf.doc.sprites;
        PalCapture(sf, pn, st, nullptr);
        sf.history.Start(std::move(st), !sf.dirty && !PalDirty());
        sf.recorded = sf.edits;
        return;
    }
    if (sf.recorded != sf.edits && !ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        st.sprites = sf.doc.sprites;
        PalCapture(sf, pn, st, &sf.history.Current());
        sf.history.Record(std::move(st));
        sf.recorded = sf.edits;
    }
    if (sf.recorded == sf.edits && !sf.dirty && !PalDirty()) sf.history.MarkSaved();
}

static void SpritesStep(SpriteFile &sf, int pn, int dir) {
    const SpriteState &st = dir < 0 ? sf.history.Undo() : sf.history.Redo();
    bool saved = sf.history.Saved();
    sf.doc.sprites = st.sprites;
    PalRestore(sf, pn, st, saved);
    sf.sel = std::min(sf.sel, (int)sf.doc.sprites.size() - 1);
    sf.picks.clear();
    sf.version++;
    sf.dirty = !saved;
    sf.recorded = sf.edits;
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
    // Tests (scripts/test_ui.ps1) give the folder to save in
    if (const char *test = getenv("IKDBG_TEST_SAVE"); test && *test) return std::string(test) + "\\" + name;
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
const char kImageFilter[] = "Images (*.png, *.pcx, *.bmp, *.gif)\0*.png;*.pcx;*.bmp;*.gif\0All files (*.*)\0*.*\0";

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
    StageSpritesSaved(sf.path);
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

// How files become sprites
struct AddOptions {
    int group = 0, number = 0, step = 1;
    bool fromNames = true, crop = false, replace = false; // who names a file 200-3.png means it
    int axis = SFF_AXIS_FEET; // or kAxisCustom: x, y
    int x = 0, y = 0;
    int pal = SFF_PAL_SHARED;
};
const int kAxisCustom = 3;

// rows: the numbers and axes of an export's list, one for each file; nullptr: those of o.
static void AddSprites(SpriteFile &sf, const std::vector<std::string> &files, const AddOptions &o, const std::vector<SffListRow> *rows = nullptr) {
    Sff_SetImageReader(DecodeImage);
    std::vector<SffSprite> made = Sff_SpritesOfPngs(sf.doc, files, o.pal, SpriteLog);
    bool all = made.size() == files.size(); // a file that failed leaves the rest out of step
    if (rows && !all) {
        Log(LogLine::Error, "Not all the files of the list could be read: nothing was added");
        return;
    }
    if (made.empty()) return;
    int group = o.group, number = o.number, replaced = 0;
    std::vector<int> added;
    sf.picks.clear();
    for (size_t k = 0; k < made.size(); k++) {
        SffSprite &s = made[k];
        int g = group, n = number;
        bool named = false;
        if (rows) {
            g = (*rows)[k].group, n = (*rows)[k].number, named = true;
            s.x = (*rows)[k].x, s.y = (*rows)[k].y;
        } else {
            named = o.fromNames && all && Sff_NumbersOfName(files[k], &g, &n);
            if (o.axis == kAxisCustom) s.x = o.x, s.y = o.y;
            else Sff_SetAxis(s, o.axis);
        }
        if (!named) {
            while (!o.replace && Sff_FindSprite(sf.doc, g, number) >= 0) number += o.step;
            n = number;
            number += o.step;
        }
        s.group = g, s.number = n;
        int at = Sff_FindSprite(sf.doc, g, n);
        if (at >= 0 && (o.replace || rows)) {
            sf.doc.sprites[at] = s;
            replaced++;
        } else {
            if (at >= 0) Log(LogLine::Info, "Sprite " + std::to_string(g) + ", " + std::to_string(n) + " exists already: added after it");
            sf.doc.sprites.push_back(s);
            at = (int)sf.doc.sprites.size() - 1;
        }
        added.push_back(at);
    }
    if (o.crop) Sff_Crop(sf.doc, added);
    sf.sel = added.back();
    if (added.size() > 1) sf.picks.insert(added.begin(), added.end());
    Log(LogLine::Info, "Added " + std::to_string(added.size() - replaced) + " sprites, replaced " + std::to_string(replaced));
    SpritesChanged(sf);
}

std::vector<int> SelectedSprites(const SpriteFile &sf) {
    if (!sf.picks.empty()) return std::vector<int>(sf.picks.begin(), sf.picks.end());
    if (sf.sel >= 0 && sf.sel < (int)sf.doc.sprites.size()) return {sf.sel};
    return {};
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

// A click on a sprite of the list: it alone; with Ctrl, one more or one less; with Shift, all
// those from the current one to it.
static void PickSprite(SpriteFile &sf, int i, const std::vector<int> &shown) {
    ImGuiIO &io = ImGui::GetIO();
    auto from = std::find(shown.begin(), shown.end(), sf.sel), to = std::find(shown.begin(), shown.end(), i);
    if (io.KeyShift && from != shown.end() && to != shown.end()) {
        if (from > to) std::swap(from, to);
        sf.picks.clear();
        sf.picks.insert(from, to + 1);
        return;
    }
    if (!io.KeyCtrl) {
        sf.picks.clear();
        sf.sel = i;
        return;
    }
    if (sf.picks.empty() && sf.sel >= 0) sf.picks.insert(sf.sel);
    if (!sf.picks.erase(i)) sf.picks.insert(i);
    if (sf.picks.empty()) sf.picks.insert(i);
    if (sf.picks.count(i)) sf.sel = i;
    else sf.sel = *sf.picks.begin();
    if (sf.picks.size() == 1) sf.picks.clear();
}

// The sprites, filtered by "group" or "group,number": a list (a small picture, the numbers, the
// size) or a grid of pictures. A click picks one, Ctrl and Shift several, Ctrl+A all those shown;
// the arrows move from one to the next.
static void SpriteBrowser(SpriteFile &sf, const PalChoice &view, bool scroll, float height) {
    static std::string filter;
    static bool grid = false;
    static int menuGroup;
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
    float addW = ImGui::CalcTextSize(ICON_PLUS " Add PNG...").x + style.FramePadding.x * 2;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - addW));
    if (ImGui::SmallButton(ICON_PLUS " Add PNG...")) ImGui::OpenPopup("add");
    ImGui::SetItemTooltip("Add sprites made of image files, an export or another SFF");

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
                sf.picks.clear();
                scroll = true;
            }
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false) && shown.size() > 1) sf.picks.insert(shown.begin(), shown.end());
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
        bool openMenu = false;
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
                    if (ImGui::InvisibleButton("s", size)) PickSprite(sf, i, shown);
                    bool hot = ImGui::IsItemHovered(), picked = sf.picks.count(i) != 0;
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                        menuGroup = s.group; // the popup opens outside this row's id
                        openMenu = true;
                    }
                    if (cur || hot || picked)
                        draw->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y),
                                            ImGui::GetColorU32(cur || picked ? ImGuiCol_Header : ImGuiCol_HeaderHovered), style.FrameRounding);
                    if (cur && !sf.picks.empty())
                        draw->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), ImGui::GetColorU32(ImGuiCol_CheckMark), style.FrameRounding);
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
        if (openMenu) ImGui::OpenPopup("##rowmenu");
        if (ImGui::BeginPopup("##rowmenu")) {
            char label[48];
            snprintf(label, sizeof(label), "Select group %d", menuGroup);
            if (ImGui::MenuItem(label)) {
                sf.picks.clear();
                for (int i : shown)
                    if (d.sprites[i].group == menuGroup) sf.picks.insert(i);
                if (!sf.picks.empty()) sf.sel = *sf.picks.begin();
                if (sf.picks.size() == 1) sf.picks.clear();
            }
            if (ImGui::MenuItem("Select all shown") && shown.size() > 1) sf.picks.insert(shown.begin(), shown.end());
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();
    if (sf.picks.empty())
        ImGui::TextDisabled("%zu of %zu", shown.size(), d.sprites.size());
    else
        ImGui::TextDisabled("%zu selected, of %zu", sf.picks.size(), d.sprites.size());
}

// What the preview draws besides the sprite (its View button)
static bool g_spriteGround = true, g_spriteRulers = false;
static struct {
    bool on = false;
    int group = 0, number = 0;
} g_spriteGhost; // another sprite over it, faint, on the same axis, to line them up

// The sprite: the wheel zooms at the mouse, a drag pans, Ctrl+drag or the arrows move the axis of
// the sprites selected, a double click fits; picking (the Palette tab) or with Alt, a click picks
// the colour under the mouse. Returns the colour picked, -1 none; *shift: Shift was held.
static int SpritePreview(SpriteFile &sf, const PalChoice &view, const std::vector<char> *mark, bool picking, bool painting, bool *shift,
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
    bool open = ImGui::BeginChild("preview", ImVec2(0, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNavInputs);
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
    // Painting, the left button is the tool's
    bool dragging = ImGui::IsItemActive() && ((ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3) && !painting) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0));
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
    // The picture moved over its axis: the axis moves the other way within it; every sprite selected
    auto nudge = [&](int dx, int dy) {
        for (int i : SelectedSprites(sf)) d.sprites[i].x -= dx, d.sprites[i].y -= dy;
        SpritesChanged(sf);
    };
    if (movingAxis) {
        static ImVec2 rest;
        rest.x += io.MouseDelta.x / zoom;
        rest.y += io.MouseDelta.y / zoom;
        int dx = (int)rest.x, dy = (int)rest.y;
        if (dx || dy) {
            nudge(dx, dy);
            rest.x -= dx, rest.y -= dy;
        }
    }
    // The arrows, with the picture in focus: one pixel, ten with Shift
    if (ImGui::IsWindowFocused() && !io.WantTextInput) {
        int step = io.KeyShift ? 10 : 1;
        int dx = ImGui::IsKeyPressed(ImGuiKey_RightArrow) ? step : ImGui::IsKeyPressed(ImGuiKey_LeftArrow) ? -step : 0;
        int dy = ImGui::IsKeyPressed(ImGuiKey_DownArrow) ? step : ImGui::IsKeyPressed(ImGuiKey_UpArrow) ? -step : 0;
        if (dx || dy) nudge(dx, dy);
    }
    ImVec2 a(axisAt.x - s.x * zoom, axisAt.y - s.y * zoom);
    if (g_spriteGround) {
        draw->AddLine(ImVec2(p0.x, axisAt.y), ImVec2(p1.x, axisAt.y), IM_COL32(255, 220, 60, 70));
        draw->AddLine(ImVec2(axisAt.x, p0.y), ImVec2(axisAt.x, p1.y), IM_COL32(255, 220, 60, 70));
    }
    if (painting && PaintCanvas(sf, view, draw, a, zoom, hovered, p0, p1)) {
        // PaintCanvas drew it
    } else if (GLuint tex = PalSpriteTexture(sf, sf.sel, view, mark)) {
        draw->AddImage(TexRef(tex), a, ImVec2(a.x + s.w * zoom, a.y + s.h * zoom));
        draw->AddRect(a, ImVec2(a.x + s.w * zoom, a.y + s.h * zoom), IM_COL32(255, 255, 255, 60));
    } else {
        draw->AddText(ImVec2(p0.x + em * 0.5f, p0.y + em * 0.4f), IM_COL32(255, 120, 120, 255), "can't decode this sprite");
    }
    if (g_spriteGhost.on) {
        int gi = Sff_FindSprite(d, g_spriteGhost.group, g_spriteGhost.number);
        if (GLuint tex = gi >= 0 ? PalSpriteTexture(sf, gi, view) : 0) {
            const SffSprite &g = d.sprites[gi];
            ImVec2 b(axisAt.x - g.x * zoom, axisAt.y - g.y * zoom);
            draw->AddImage(TexRef(tex), b, ImVec2(b.x + g.w * zoom, b.y + g.h * zoom), ImVec2(0, 0), ImVec2(1, 1), kOnionBefore);
        }
    }
    draw->AddLine(ImVec2(axisAt.x - em, axisAt.y), ImVec2(axisAt.x + em, axisAt.y), IM_COL32(255, 220, 60, 255));
    draw->AddLine(ImVec2(axisAt.x, axisAt.y - em), ImVec2(axisAt.x, axisAt.y + em), IM_COL32(255, 220, 60, 255));
    if (g_spriteRulers) {
        // Pixels from the axis, along the top and the left: a mark at least half a line apart
        int unit = 1;
        while (unit * zoom < em * 0.5f) unit *= unit == 1 || unit == 10 || unit == 100 ? 5 : 2;
        float bar = em * 1.1f;
        draw->AddRectFilled(p0, ImVec2(p1.x, p0.y + bar), IM_COL32(0, 0, 0, 130));
        draw->AddRectFilled(ImVec2(p0.x, p0.y + bar), ImVec2(p0.x + bar * 1.8f, p1.y), IM_COL32(0, 0, 0, 130));
        for (int k = (int)floorf((p0.x - axisAt.x) / zoom / unit); axisAt.x + k * unit * zoom < p1.x; k++) {
            float x = axisAt.x + k * unit * zoom;
            bool major = k % 5 == 0;
            draw->AddLine(ImVec2(x, p0.y + bar * (major ? 0.0f : 0.6f)), ImVec2(x, p0.y + bar), IM_COL32(255, 255, 255, 170));
            if (major) draw->AddText(ImVec2(x + 2, p0.y), IM_COL32(255, 255, 255, 220), std::to_string(k * unit).c_str());
        }
        for (int k = (int)floorf((p0.y + bar - axisAt.y) / zoom / unit); axisAt.y + k * unit * zoom < p1.y; k++) {
            float y = axisAt.y + k * unit * zoom;
            bool major = k % 5 == 0;
            draw->AddLine(ImVec2(p0.x + bar * (major ? 0.0f : 1.2f), y), ImVec2(p0.x + bar * 1.8f, y), IM_COL32(255, 255, 255, 170));
            if (major) draw->AddText(ImVec2(p0.x + 2, y), IM_COL32(255, 255, 255, 220), std::to_string(k * unit).c_str());
        }
    }
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

// Crop and Resize, for the sprites selected or the one shown
static void CropAndResize(SpriteFile &sf) {
    float em = ImGui::GetFontSize();
    if (ImGui::Button(ICON_XMARK " Crop")) {
        int n = Sff_Crop(sf.doc, SelectedSprites(sf));
        Log(LogLine::Info, "Cropped " + std::to_string(n) + " sprites");
        if (n) SpritesChanged(sf);
    }
    ImGui::SetItemTooltip("Takes the empty borders off; the sprite stays where it was (its axis moves with the picture)");
    ImGui::SameLine();
    if (ImGui::Button(ICON_IMAGE " Resize...")) ImGui::OpenPopup("resize");
    ImGui::SetItemTooltip("Scales the pictures and their axes");
    if (ImGui::BeginPopup("resize")) {
        static float pct[2] = {100, 100};
        static int filter = SFF_SCALE_NEAREST;
        ImGui::SetNextItemWidth(em * 10);
        ImGui::InputFloat2("Width, height %", pct, "%.0f");
        ImGui::SetNextItemWidth(em * 10);
        ImGui::Combo("Filter", &filter, "Nearest\0Bilinear\0EPX (pixel art, enlarging)\0");
        ImGui::TextDisabled("The animations' offsets and boxes stay as they are.");
        if (ImGui::Button(ICON_CHECK " Resize")) {
            int n = Sff_Scale(sf.doc, SelectedSprites(sf), pct[0] / 100, pct[1] / 100, filter);
            Log(LogLine::Info, "Resized " + std::to_string(n) + " sprites");
            if (n) SpritesChanged(sf);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// The tab when several sprites are selected: what can be done with them all.
static void SpritesPanel(SpriteFile &sf, int pn) {
    SffDoc &d = sf.doc;
    float em = ImGui::GetFontSize();
    std::vector<int> sel = SelectedSprites(sf);
    ImGui::Text("%zu sprites selected", sel.size());

    // First: what several sprites are selected for. A character's AIR file, or the def these
    // sprites are a screen's or a stage's
    std::string actions = pn > 0 ? CharFile(pn, "anim") : AnimFileOfSprites(sf.path);
    if (!actions.empty()) {
        ImGui::SeparatorText("Animation");
        static int action = 0, time = 4, actionOf = -1;
        // Actions go by their sprites' group: a fixed number would add them to the standing animation
        if (d.sprites[sel[0]].group != actionOf) actionOf = action = d.sprites[sel[0]].group;
        ImGui::SetNextItemWidth(em * 5);
        ImGui::InputInt("Action", &action, 0);
        ImGui::SetNextItemWidth(em * 5);
        ImGui::InputInt("Ticks each", &time, 0);
        const AirFile &air = AirOf(LoadSource(actions));
        bool exists = air.actions.count(action) != 0;
        std::vector<std::pair<int, int>> numbers;
        for (int i : sel) numbers.push_back({d.sprites[i].group, d.sprites[i].number});
        if (ImGui::Button(exists ? ICON_PLUS " Add to the action" : ICON_PLUS " New action") && !AnimFromSprites(pn, actions, action, time, numbers, exists))
            Log(LogLine::Error, "Could not write the animation");
        ImGui::SetItemTooltip("One element for each sprite, in the order of the file");
    }

    ImGui::SeparatorText("Axes");
    static int by[2];
    ImGui::SetNextItemWidth(em * 8);
    ImGui::InputInt2("##by", by);
    ImGui::SetItemTooltip("x, y: how far each axis moves");
    ImGui::SameLine();
    if (ImGui::Button("Move")) {
        for (int i : sel) d.sprites[i].x += by[0], d.sprites[i].y += by[1];
        SpritesChanged(sf);
    }
    ImGui::SetItemTooltip("Ctrl+drag on the preview or its arrows move them too, watching the sprite");
    const char *places[] = {"Feet", "Centre", "Top left"};
    for (int m = 0; m < 3; m++) {
        if (m) ImGui::SameLine();
        if (ImGui::SmallButton(places[m])) {
            for (int i : sel) Sff_SetAxis(d.sprites[i], m);
            SpritesChanged(sf);
        }
        ImGui::SetItemTooltip("Each one's axis at that place of its picture");
    }

    ImGui::SeparatorText("Numbers");
    static int group = 0, number = 0;
    ImGui::SetNextItemWidth(em * 5);
    ImGui::InputInt("Group", &group, 0);
    ImGui::SetNextItemWidth(em * 5);
    ImGui::InputInt("From", &number, 0);
    // The numbers they would take, in the file's order, must be free or theirs
    bool free = group >= 0 && number >= 0;
    for (size_t k = 0; k < sel.size() && free; k++) {
        int at = Sff_FindSprite(d, group, number + (int)k);
        free = at < 0 || sf.picks.count(at);
    }
    ImGui::BeginDisabled(!free);
    if (ImGui::Button(ICON_LIST " Renumber them")) {
        for (size_t k = 0; k < sel.size(); k++) d.sprites[sel[k]].group = group, d.sprites[sel[k]].number = number + (int)k;
        SpritesChanged(sf);
    }
    ImGui::EndDisabled();
    if (!free) ImGui::TextColored(Ink(kRed), "Those numbers are in use.");
    ImGui::SetItemTooltip("Animations using the old numbers are not changed");

    ImGui::SeparatorText("Pictures and files");
    CropAndResize(sf);
    static bool aligned = true;
    ImGui::Checkbox("Same canvas and axis", &aligned);
    ImGui::SetItemTooltip("All the images the same size, so that they stay lined up in a drawing program");
    if (ImGui::Button(ICON_DOWNLOAD " Export...")) {
        std::string path = PickSaveFile("List of sprites\0sprites.txt\0", "txt", kSffListName), error;
        if (!path.empty()) {
            std::string folder = path.substr(0, path.find_last_of("/\\"));
            if (Sff_Export(d, sel, folder, aligned, &error)) Log(LogLine::Info, "Exported " + std::to_string(sel.size()) + " sprites to " + folder);
            else Log(LogLine::Error, error);
        }
    }
    ImGui::SetItemTooltip("One PNG for each sprite and a list of their numbers and axes (Add PNG reads them back)");
    ImGui::SameLine();
    if (ImGui::Button(ICON_TRASH " Delete")) {
        std::vector<SffSprite> kept;
        for (int i = 0; i < (int)d.sprites.size(); i++)
            if (!sf.picks.count(i)) kept.push_back(d.sprites[i]);
        d.sprites.swap(kept);
        sf.sel = std::min(*sf.picks.begin(), (int)d.sprites.size() - 1);
        sf.picks.clear();
        SpritesChanged(sf);
    }
}

// The sprite's tab: its number, axis, what it is; replaced, saved, duplicated, deleted; the
// actions using it. Returns true when it was deleted.
static bool SpritePanel(SpriteFile &sf, int pn) {
    SffDoc &d = sf.doc;
    SffSprite &s = d.sprites[sf.sel];
    float em = ImGui::GetFontSize();
    static const char *formats[] = {"raw", "?", "RLE8", "RLE5", "LZ5", "?", "?", "?", "?", "?", "PNG8", "PNG24", "PNG32"};
    static int num[2], forSel = -1;
    static unsigned forVersion;
    static const SpriteFile *forFile;
    if (forSel != sf.sel || forFile != &sf || forVersion != sf.version) {
        forSel = sf.sel, forFile = &sf, forVersion = sf.version;
        num[0] = s.group, num[1] = s.number;
    }
    Label("Number");
    float renW = ImGui::CalcTextSize(ICON_LIST " Renumber").x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x;
    bool fits = ImGui::GetContentRegionAvail().x - renW >= em * 5; // else the button goes below
    ImGui::SetNextItemWidth(fits ? ImGui::GetContentRegionAvail().x - renW : -FLT_MIN);
    ImGui::InputInt2("##num", num);
    ImGui::SetItemTooltip("Group, number");
    if (fits) ImGui::SameLine();
    bool same = num[0] == s.group && num[1] == s.number;
    bool taken = !same && Sff_FindSprite(d, num[0], num[1]) >= 0;
    ImGui::BeginDisabled(taken || same || num[0] < 0 || num[1] < 0);
    if (ImGui::Button(ICON_LIST " Renumber")) {
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
    if (ImGui::Button(ICON_UPLOAD " Replace...")) {
        std::vector<std::string> files = PickFiles(kImageFilter, false);
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
    if (ImGui::Button(ICON_DOWNLOAD " Save PNG...")) {
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
    if (ImGui::Button(ICON_COPY " Duplicate")) {
        SffSprite copy = s;
        while (Sff_FindSprite(d, copy.group, copy.number) >= 0) copy.number++;
        d.sprites.insert(d.sprites.begin() + sf.sel + 1, copy);
        sf.sel++;
        SpritesChanged(sf);
        return true;
    }
    ImGui::SetItemTooltip("A copy with the next free number in its group");
    flow("Delete");
    if (ImGui::Button(ICON_TRASH " Delete")) {
        d.sprites.erase(d.sprites.begin() + sf.sel);
        sf.sel = std::min(sf.sel, (int)d.sprites.size() - 1);
        SpritesChanged(sf);
        return true;
    }
    ImGui::Spacing();
    CropAndResize(sf);

    // The actions showing it: the character's, or those of the def these sprites are for
    std::string air = pn > 0 ? CharFile(pn, "anim") : AnimFileOfSprites(sf.path);
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
                if (ImGui::Selectable(label)) AnimOpenFile(pn > 0 ? "" : air, kv.first, (int)e + 1);
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
    TrackSprites(sf, pn);

    // The palettes the sprites are seen with: the player's at first
    std::vector<PalChoice> choices;
    PalChoices(sf, pn, choices);
    static std::map<std::string, std::string> chosen; // file -> the label chosen
    std::string &label = chosen[NormPath(sf.path)];
    if (std::string wanted = PalTakeWanted(); !wanted.empty()) label = wanted;
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
        float w = ImGui::CalcTextSize(ICON_SAVE " Save").x + ImGui::CalcTextSize(ICON_REVERT " Revert").x + style.FramePadding.x * 4 +
                  style.ItemSpacing.x * 2 + UndoButtonsWidth();
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < w)
            ImGui::NewLine();
        else
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - w);
        if (int step = UndoButtons(sf.history.CanUndo(), sf.history.CanRedo())) {
            SpritesStep(sf, pn, step);
            dirty = sf.dirty || PalDirty();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!dirty);
        if (dirty) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        bool save = ImGui::Button(ICON_SAVE " Save");
        if (dirty) ImGui::PopStyleColor();
        if (save) {
            PalSave(); // the ACT files first: the game reads them with the SFF
            if (sf.dirty) SaveSprites(sf);
        }
        ImGui::SetItemTooltip("Writes the SFF file and the ACT files changed (the first time, the old ones are kept as "
                              ".bak); the game reads the SFF again.");
        ImGui::SameLine();
        if (ImGui::Button(ICON_REVERT " Revert")) {
            if (sf.dirty) {
                Sff_Load(d, GamePath(sf.path), sf.pal.empty() ? "" : GamePath(sf.pal));
                sf.dirty = false;
                sf.version++;
            }
            sf.picks.clear();
            sf.edits++;
            PalRevert(sf, pn);
        }
        ImGui::EndDisabled();
    }
    if (!d.error.empty()) ImGui::TextColored(Ink(kRed), "%s", d.error.c_str());
    if (d.converted) ImGui::TextColored(Ink(kYellow), "An SFF v1 file: saving writes SFF v2 (palette 1,1 from the .act).");

    static AddOptions add;
    static SffDoc importFrom;
    static std::string importFilter;
    static int importPal = SFF_IMPORT_OWN;
    static bool importReplace = false, askImport = false;
    auto addPopup = [&]() {
        if (ImGui::BeginPopup("add")) {
            ImGui::SetNextItemWidth(em * 6);
            ImGui::InputInt("Group", &add.group, 0);
            ImGui::SetNextItemWidth(em * 6);
            ImGui::InputInt("From number", &add.number, 0);
            ImGui::SameLine();
            ImGui::RadioButton("by 1", &add.step, 1);
            ImGui::SameLine();
            ImGui::RadioButton("by 10", &add.step, 10);
            ImGui::Checkbox("Numbers from the file names", &add.fromNames);
            ImGui::SetItemTooltip("200_3.png, 200-3.png or 200,3.png is group 200, number 3; other names take the numbers above");
            ImGui::SetNextItemWidth(em * 11);
            ImGui::Combo("Axis", &add.axis, "At the feet\0In the centre\0Top left\0These numbers\0");
            if (add.axis == kAxisCustom) {
                ImGui::SetNextItemWidth(em * 11);
                ImGui::InputInt2("x, y", &add.x);
            }
            ImGui::Checkbox("Crop the empty borders", &add.crop);
            ImGui::SetItemTooltip("After the axis is set, so that each stays where it was in its image");
            ImGui::SetNextItemWidth(em * 11);
            ImGui::Combo("Palette", &add.pal, screen ? "The shared one (1,1)\0Their own\0Palette 1,1, same indices\0"
                                                     : "The character's (1,1)\0Their own\0Palette 1,1, same indices\0");
            ImGui::SetItemTooltip("The first maps each colour to the nearest of 1,1. The last keeps an indexed image's "
                                  "indices, for images drawn on the character's palette.");
            ImGui::Checkbox("Replace those with the same number", &add.replace);
            if (ImGui::Button(ICON_FOLDER_OPEN " Choose the files...")) {
                std::vector<std::string> files = PickFiles(kImageFilter, true);
                if (!files.empty()) AddSprites(sf, files, add);
                ImGui::CloseCurrentPopup();
            }
            ImGui::Separator();
            if (ImGui::Selectable("An export (sprites.txt)...")) {
                std::vector<std::string> list = PickFiles("List of sprites\0sprites.txt\0All files (*.*)\0*.*\0", false);
                std::vector<SffListRow> rows;
                if (!list.empty() && Sff_ReadList(list[0], rows)) {
                    std::string folder = list[0].substr(0, list[0].find_last_of("/\\") + 1);
                    std::vector<std::string> files;
                    for (const SffListRow &r : rows) files.push_back(folder + r.file);
                    AddSprites(sf, files, add, &rows);
                } else if (!list.empty()) {
                    Log(LogLine::Error, "Not a list of sprites: " + list[0]);
                }
            }
            ImGui::SetItemTooltip("Sprites exported here, changed or not: the same numbers replaced, with the Palette above");
            if (ImGui::Selectable("Another SFF...")) {
                std::vector<std::string> files = PickFiles("Sprite files (*.sff)\0*.sff\0All files (*.*)\0*.*\0", false);
                if (!files.empty() && Sff_Load(importFrom, files[0])) askImport = true;
                else if (!files.empty()) Log(LogLine::Error, importFrom.error);
            }
            ImGui::EndPopup();
        }
        if (askImport) ImGui::OpenPopup("import");
        askImport = false;
        if (ImGui::BeginPopup("import")) {
            std::vector<int> chosen;
            int fg = -1, fn = -1;
            if (!importFilter.empty()) sscanf(importFilter.c_str(), "%d , %d", &fg, &fn);
            for (int i = 0; i < (int)importFrom.sprites.size(); i++)
                if ((fg < 0 || importFrom.sprites[i].group == fg) && (fn < 0 || importFrom.sprites[i].number == fn)) chosen.push_back(i);
            ImGui::Text("%s: %zu sprites", importFrom.path.c_str(), importFrom.sprites.size());
            ImGui::SetNextItemWidth(em * 12);
            ImGui::InputTextWithHint("Which", "group or group,number; all", &importFilter);
            ImGui::SetNextItemWidth(em * 12);
            ImGui::Combo("Palette##import", &importPal, "Their own\0Mapped to 1,1\0Palette 1,1, same indices\0");
            ImGui::Checkbox("Replace those with the same number", &importReplace);
            char label[48];
            snprintf(label, sizeof(label), ICON_DOWNLOAD " Import %zu sprites", chosen.size());
            ImGui::BeginDisabled(chosen.empty());
            if (ImGui::Button(label)) {
                int skipped = 0;
                int n = Sff_ImportSprites(d, importFrom, chosen, importReplace, importPal, &skipped);
                Log(LogLine::Info, "Imported " + std::to_string(n) + " sprites, " + std::to_string(skipped) + " skipped: their numbers are in use");
                sf.picks.clear();
                if (n) SpritesChanged(sf);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::EndPopup();
        }
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
    // The preview's toolbar: the palette seen, what is drawn
    float viewW = ImGui::CalcTextSize(ICON_WATCH " View").x + style.FramePadding.x * 2 + style.ItemSpacing.x;
    ImGui::SetNextItemWidth(std::min(em * 16, ImGui::GetContentRegionAvail().x - em * 2 - viewW));
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
    ImGui::SameLine();
    if (ImGui::Button(ICON_WATCH " View")) ImGui::OpenPopup("view");
    if (ImGui::BeginPopup("view")) {
        ImGui::Checkbox("Lines through the axis", &g_spriteGround);
        ImGui::Checkbox("Rulers", &g_spriteRulers);
        ImGui::Checkbox("A ghost over the sprite", &g_spriteGhost.on);
        ImGui::SetItemTooltip("Another sprite on the same axis, to line sprites up against it");
        ImGui::BeginDisabled(!g_spriteGhost.on);
        ImGui::SetNextItemWidth(em * 8);
        ImGui::InputInt2("##ghost", &g_spriteGhost.group);
        ImGui::SameLine();
        if (ImGui::Button("This sprite") && sf.sel >= 0) g_spriteGhost.group = d.sprites[sf.sel].group, g_spriteGhost.number = d.sprites[sf.sel].number;
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    HelpMarker("Wheel: zoom at the mouse. Drag or middle drag: pan. Ctrl+drag, or a click on the picture and the arrows "
               "(Shift: ten pixels): move the axis of the sprites selected. Double click: fit. "
               "With the Palette tab (or Alt held), a click picks the colour under the mouse; Shift+click adds it.");
    static int tab = 0; // 0 the sprite, 1 the palette, 2 paint
    static std::vector<char> mark;
    static int colour = -1; // picked on the sprite, for the Palette tab (shown from the next frame)
    static bool shift = false;
    float previewH = wide ? h - ImGui::GetFrameHeightWithSpacing() : std::max(em * 10, (h - ImGui::GetFrameHeightWithSpacing()) * 0.5f);
    bool now = false;
    if (int c = SpritePreview(sf, view, tab == 1 ? &mark : nullptr, tab == 1, tab == 2, &now, previewH); c >= 0) colour = c, shift = now;
    if (!wide) ImGui::Spacing();
    else ImGui::TableSetColumnIndex(2);
    // The tabs
    bool stop = false;
    if (ImGui::BeginChild("panel", ImVec2(0, wide ? h : 0))) {
        if (ImGui::BeginTabBar("tabs")) {
            if (ImGui::BeginTabItem("Sprite")) {
                tab = 0;
                if (sf.picks.empty()) stop = SpritePanel(sf, pn);
                else SpritesPanel(sf, pn);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Palette", nullptr, colour >= 0 && tab != 1 ? ImGuiTabItemFlags_SetSelected : 0)) {
                tab = 1;
                PalettePanel(sf, pn, PalFor(sf, view, sf.sel), colour, shift, mark);
                colour = -1;
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Paint")) {
                tab = 2;
                PaintPanel(sf, view);
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
