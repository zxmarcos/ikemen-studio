// Fonts: the font files the game loaded (frame geometry, editor_font.go). A font's [Def] edited
// here, the game reading the font again into the fonts in use at each change; Save writes it.
// A .def bitmap font: its glyphs (the sprites of its SFF: group 0, the number a character's code)
// shown and edited in the Sprites window. A .fnt (MUGEN 1.0): its text ([Def], [Map]) and its
// glyph sheet (a PCX image). Both: a sample text drawn as the engine draws it.
#include "app_internal.h"

#include <cfloat>
#include <cmath>
#include <cstring>
#include <fstream>

#include "fnt.h"
#include "imgui_stdlib.h"

namespace app {

std::vector<std::string> g_fontFiles;
std::string g_fontPicked;

static std::string LowerOf(std::string s) {
    for (char &ch : s) ch = (char)tolower((unsigned char)ch);
    return s;
}

static bool HasExt(const std::string &path, const char *ext) {
    size_t n = strlen(ext);
    return path.size() > n && LowerOf(path.substr(path.size() - n)) == ext;
}

static const IniSection *DefSection(const IniFile &ini) {
    for (const IniSection &s : ini.sections)
        if (s.lower == "def") return &s;
    return nullptr;
}

// The SFF of a bitmap font, where the engine looks for it (LoadFntSff): next to the def, in
// font/, at the root, in data/
static std::string FontSffPath(const std::string &def, const std::string &file) {
    if (file.empty()) return "";
    size_t slash = def.find_last_of("/\\");
    std::string dir = slash == std::string::npos ? "" : def.substr(0, slash + 1);
    for (const std::string &p : {dir + file, "font/" + file, file, "data/" + file}) {
        DWORD a = GetFileAttributesA(GamePath(p).c_str());
        if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) return p;
    }
    return "";
}

// A .fnt being edited: the file as read, its text being edited, its image as a texture
struct FntEdit {
    FntV1 file;
    std::string text;
    GLuint tex = 0;
    int w = 0, h = 0;
    bool Dirty() const { return text != file.text; }
};
static std::map<std::string, FntEdit> g_fnts;

static void FntRead(const std::string &path, FntEdit &e) {
    FntV1_Read(ReadWholeFile(GamePath(path)), e.file);
    e.text = e.file.text;
    std::vector<unsigned char> px, pal(256 * 4);
    if (e.file.error.empty() &&
        Sff_DecodePcx(e.file.bytes.substr(e.file.pcxAt, e.file.pcxLen), &e.w, &e.h, px, pal.data())) {
        std::vector<unsigned char> rgba(px.size() * 4);
        for (size_t i = 0; i < px.size(); i++) memcpy(&rgba[i * 4], &pal[px[i] * 4], 4);
        if (!e.tex) glGenTextures(1, &e.tex);
        glBindTexture(GL_TEXTURE_2D, e.tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, e.w, e.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    }
}

static FntEdit &FntOf(const std::string &path) {
    auto it = g_fnts.find(path);
    if (it != g_fnts.end()) return it->second;
    FntEdit &e = g_fnts[path];
    FntRead(path, e);
    return e;
}

// The first time, the old file is kept as .bak
static bool SaveFnt(const std::string &sel, FntEdit &e) {
    std::string path = GamePath(sel), bak = path + ".bak";
    if (GetFileAttributesA(bak.c_str()) == INVALID_FILE_ATTRIBUTES) CopyFileA(path.c_str(), bak.c_str(), TRUE);
    std::ofstream out(path, std::ios::binary);
    out << FntV1_Build(e.file, e.text);
    out.close();
    if (!out) {
        Log(LogLine::Error, "Could not write " + path);
        return false;
    }
    FntRead(sel, e);
    Log(LogLine::Info, "Saved " + sel);
    return true;
}

void FontsUnsaved(std::vector<Unsaved> &out) {
    for (auto &kv : g_fnts)
        if (kv.second.Dirty()) {
            std::string path = kv.first;
            FntEdit *e = &kv.second;
            out.push_back({path, [path, e] { return SaveFnt(path, *e); }});
        }
}

// Reloading: the font being edited goes to a file next to it (a .fnt's name still ends in .fnt,
// which tells the engine its kind), which the game reads then deletes
static std::string g_fontPending; // the font with an edit not sent yet
static double g_fontSent = -1;

static void FontReload(const std::string &path, const std::string *bytes) {
    std::string arg = LuaQuote(path);
    if (bytes) {
        static int n;
        std::string tmp = path + "." + std::to_string(++n % 100) + ".ikdbg" + (HasExt(path, ".fnt") ? ".fnt" : "");
        std::ofstream out(GamePath(tmp), std::ios::binary);
        out << *bytes;
        if (!out) {
            Log(LogLine::Error, "Could not write " + GamePath(tmp));
            return;
        }
        arg += ", " + LuaQuote(tmp);
    }
    Exec("local ok, err = debugReloadFont(" + arg + ") if not ok then error(err, 0) end");
    g_fontSent = ImGui::GetTime();
}

// What the game reads for a font being edited: a .def's text, a .fnt's file with its text
static std::string FontBytes(const std::string &path) {
    if (HasExt(path, ".fnt")) {
        FntEdit &e = FntOf(path);
        return FntV1_Build(e.file, e.text);
    }
    return LoadSource(path).text;
}

void FontSpritesSaved(const std::string &path) {
    for (const std::string &def : g_fontFiles) {
        if (!HasExt(def, ".def")) continue;
        IniFile ini;
        Ini_Parse(LoadSource(def).text, ini);
        const IniSection *s = DefSection(ini);
        const IniField *f = s ? s->Field("file") : nullptr;
        if (f && NormPath(FontSffPath(def, f->value)) == NormPath(path)) FontReload(def, nullptr);
    }
}

// The [Def] of a font's text: the numbers dragged, the rest as text. True when text changed.
static bool DefFields(std::string &text, const IniSection &def, bool fnt) {
    float em = ImGui::GetFontSize();
    std::string before = text;
    auto pair = [&](const char *key, const char *help) {
        float v[2] = {0, 0};
        def.Floats(key, v, 2);
        int iv[2] = {(int)v[0], (int)v[1]};
        ImGui::SetNextItemWidth(em * 9);
        if (ImGui::DragInt2(key, iv, 0.1f) && text == before) {
            float nv[2] = {(float)iv[0], (float)iv[1]};
            Ini_SetField(text, def, key, Ini_Floats(nv, 2));
        }
        ImGui::SetItemTooltip("%s", help);
    };
    pair("size", "Width (that of a space, and of a character the map gives none) and height.");
    pair("spacing", "Added between characters, and between lines.");
    pair("offset", "Where the text is drawn from its position.");
    std::vector<const char *> keys = {"colors"};
    if (!fnt) keys = {"type", "banktype", "colors", "file"};
    for (const char *key : keys) {
        const IniField *f = def.Field(key);
        std::string v = f ? f->value : "";
        ImGui::SetNextItemWidth(em * 12);
        if (ImGui::InputText(key, &v, ImGuiInputTextFlags_EnterReturnsTrue) && text == before)
            Ini_SetField(text, def, key, v);
    }
    return text != before;
}

// A sample text, drawn as Fnt.DrawText draws it: the baseline at the position, the top at
// offset.y - size.y + 1 from it; each glyph by its axis, the pen moving by its width plus
// spacing.x; a space or a character the font lacks moves it by size.x.
// glyph(c, tex, uv0, uv1, w, h, axisX, axisY): the character's picture, false when none.
using GlyphFn = std::function<bool(int, GLuint &, ImVec2 &, ImVec2 &, int &, int &, int &, int &)>;
static void Sample(const std::string &key, const float *size, const float *spacing, const float *offset,
                   const GlyphFn &glyph) {
    static std::string sample = "The quick brown fox 0123456789!?";
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##sample", &sample);
    static float zoom = 2;
    static std::string zoomFor;
    if (zoomFor != key) zoomFor = key, zoom = std::max(1.0f, std::floor(24 / std::max(size[1], 1.0f)));
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float h = std::max(size[1], 8.0f) * zoom * 2 + 8;
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("sample", ImVec2(std::max(avail.x, 32.0f), h));
    if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0)
        zoom = std::max(1.0f, std::min(8.0f, zoom + ImGui::GetIO().MouseWheel));
    ImDrawList *draw = ImGui::GetWindowDrawList();
    ImVec2 p1(p0.x + std::max(avail.x, 32.0f), p0.y + h);
    draw->PushClipRect(p0, p1, true);
    draw->AddRectFilled(p0, p1, IM_COL32(30, 30, 36, 255));
    float base = p0.y + h * 0.7f, pen = p0.x + 4 + offset[0] * zoom, top = base + (offset[1] - size[1] + 1) * zoom;
    draw->AddLine(ImVec2(p0.x, base), ImVec2(p1.x, base), IM_COL32(255, 220, 60, 80));
    for (unsigned char ch : sample) {
        GLuint tex = 0;
        ImVec2 uv0, uv1;
        int w = 0, gh = 0, ax = 0, ay = 0;
        if (ch == ' ' || !glyph(ch, tex, uv0, uv1, w, gh, ax, ay)) {
            pen += (size[0] + spacing[0]) * zoom;
            continue;
        }
        if (tex) {
            ImVec2 a(pen - ax * zoom, top - ay * zoom);
            draw->AddImage(TexRef(tex), a, ImVec2(a.x + w * zoom, a.y + gh * zoom), uv0, uv1);
        }
        pen += (w + spacing[0]) * zoom;
    }
    draw->PopClipRect();
    ImGui::SetItemTooltip("As the game draws it (bank 0). Wheel: zoom.");
}

void FontsWindow() {
    if (!g_fontPending.empty() && ImGui::GetTime() - g_fontSent > 0.1) {
        std::string bytes = FontBytes(g_fontPending);
        FontReload(g_fontPending, &bytes);
        g_fontPending.clear();
    }
    if (!BeginTool("Fonts")) return;
    static std::string sel, seenPicked;
    if (g_fontPicked != seenPicked) seenPicked = sel = g_fontPicked; // a text picked on the game
    float em = ImGui::GetFontSize();
    if (g_fontFiles.empty()) {
        if (g_bridge.Running()) ImGui::TextDisabled("Needs the game's frames (patched Ikemen GO).");
        else EmptyNote("The fonts of the screens show here once the game is running.");
        ImGui::End();
        return;
    }

    // Left: the fonts loaded
    ImGui::BeginChild("fontlist", ImVec2(em * 14, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    for (const std::string &f : g_fontFiles)
        if (ImGui::Selectable(f.c_str(), f == sel)) sel = f;
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("font");
    auto done = [] {
        ImGui::EndChild();
        ImGui::End();
    };
    if (sel.empty()) {
        ImGui::TextDisabled("Pick a font, or a text on the Game window (Screenpack on).");
        return done();
    }
    bool fnt = HasExt(sel, ".fnt");
    SourceFile *sf = fnt ? nullptr : &LoadSource(sel);
    FntEdit *fe = fnt ? &FntOf(sel) : nullptr;
    if (fe && !fe->file.error.empty()) {
        ImGui::TextDisabled("%s: %s.", sel.c_str(), fe->file.error.c_str());
        return done();
    }
    std::string &text = fnt ? fe->text : sf->text;
    bool dirty = fnt ? fe->Dirty() : sf->Dirty();
    IniFile ini;
    Ini_Parse(text, ini);
    const IniSection *def = DefSection(ini);

    ImGui::BeginDisabled(!dirty);
    if (ImGui::Button(ICON_SAVE " Save")) {
        if (fnt) SaveFnt(sel, *fe);
        else SaveSource(*sf);
    }
    ImGui::SetItemTooltip(fnt ? "Writes the .fnt file (the old one is kept as .bak the first time). The game already shows every change."
                              : "Writes the font's def. The game already shows every change.");
    ImGui::SameLine();
    if (ImGui::Button(ICON_REVERT " Revert")) {
        if (fnt)
            fe->text = fe->file.text;
        else
            ReadSource(*sf);
        FontReload(sel, nullptr);
    }
    ImGui::EndDisabled();
    SaveState(dirty, true);
    if (!fnt) {
        ImGui::SameLine();
        if (ImGui::Button(ICON_CODE " Show in Code")) {
            g_codeFile = sf->path;
            g_codeLine = g_codeScrollTo = def ? def->line : 1;
            g_codeHold = true;
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s%s%s", sel.c_str(), dirty ? " *" : "", fnt ? "  (MUGEN 1.0 font)" : "");
    if (!def) {
        ImGui::TextDisabled("No [Def] section.");
        return done();
    }
    if (DefFields(text, *def, fnt)) {
        if (sf) sf->version++;
        g_fontPending = sel;
        return done(); // the text changed: parsed again next frame
    }
    float size[2] = {0, 0}, spacing[2] = {0, 0}, offset[2] = {0, 0};
    def->Floats("size", size, 2);
    def->Floats("spacing", spacing, 2);
    def->Floats("offset", offset, 2);

    if (fnt) {
        // The glyphs: places in the PCX image (its [Map]), each the image's height
        std::map<int, FntGlyph> map = FntV1_Map(text, (int)size[0]);
        ImGui::SeparatorText("Sample");
        Sample(sel, size, spacing, offset, [&](int c, GLuint &tex, ImVec2 &uv0, ImVec2 &uv1, int &w, int &h, int &ax, int &ay) {
            auto it = map.find(c);
            if (it == map.end() || fe->w <= 0) return false;
            tex = fe->tex, w = it->second.w, h = fe->h, ax = 0, ay = 0;
            uv0 = ImVec2((float)it->second.ofs / fe->w, 0), uv1 = ImVec2((float)(it->second.ofs + w) / fe->w, 1);
            return true;
        });
        ImGui::SeparatorText("Glyph sheet");
        if (fe->tex) {
            float zoom = std::max(1.0f, std::min(4.0f, (ImGui::GetContentRegionAvail().x - 8) / std::max(1, fe->w)));
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("sheet", ImVec2(fe->w * zoom, fe->h * zoom));
            ImDrawList *draw = ImGui::GetWindowDrawList();
            Checkerboard(draw, p0, ImVec2(p0.x + fe->w * zoom, p0.y + fe->h * zoom));
            draw->AddImage(TexRef(fe->tex), p0, ImVec2(p0.x + fe->w * zoom, p0.y + fe->h * zoom));
            float mx = ImGui::GetIO().MousePos.x;
            for (const auto &g : map) {
                ImVec2 a(p0.x + g.second.ofs * zoom, p0.y), b(a.x + g.second.w * zoom, p0.y + fe->h * zoom);
                bool hot = ImGui::IsItemHovered() && mx >= a.x && mx < b.x;
                draw->AddRect(a, b, hot ? IM_COL32(255, 220, 60, 255) : IM_COL32(255, 255, 255, 40));
                if (hot)
                    ImGui::SetTooltip("%c (0x%02X): at %d, %d wide", g.first >= 32 && g.first < 127 ? g.first : '?', g.first,
                                      g.second.ofs, g.second.w);
            }
        }
        ImGui::SeparatorText("Text ([Def], [Map]: a character, then its x in the image and its width)");
        // Typed here, it goes to the game as it is typed; changed elsewhere, it is shown
        static std::string editing, editFor;
        static bool typing;
        if (editFor != sel || (!typing && editing != text)) editing = text, editFor = sel;
        if (ImGui::InputTextMultiline("##text", &editing, ImVec2(-FLT_MIN, em * 14)) && editing != text) {
            text = editing;
            g_fontPending = sel;
        }
        typing = ImGui::IsItemActive();
        return done();
    }

    // A .def font: TrueType (glyphs in its .ttf), or bitmap (glyphs in its SFF)
    std::string type = def->Field("type") ? LowerOf(def->Field("type")->value) : "bitmap";
    if (type == "truetype") {
        ImGui::TextDisabled("A TrueType font: its glyphs are in the .ttf file.");
        return done();
    }
    std::string sffPath = FontSffPath(sel, def->Field("file") ? def->Field("file")->value : "");
    if (sffPath.empty()) {
        ImGui::TextDisabled("Its SFF file is not found.");
        return done();
    }
    SpriteFile &spr = SpriteFileAt(sffPath);
    ImGui::SeparatorText("Sample");
    Sample(sel, size, spacing, offset, [&](int c, GLuint &tex, ImVec2 &uv0, ImVec2 &uv1, int &w, int &h, int &ax, int &ay) {
        int i = Sff_FindSprite(spr.doc, 0, c);
        if (i < 0) return false;
        const SffSprite &s = spr.doc.sprites[i];
        tex = SpriteTexture(spr, i), uv0 = ImVec2(0, 0), uv1 = ImVec2(1, 1);
        w = s.w, h = s.h, ax = s.x, ay = s.y;
        return true;
    });

    ImGui::SeparatorText("Glyphs");
    ImGui::TextDisabled("Click one to edit it in the Sprites window (its axis places it; its width is the sprite's).");
    float cell = em * 2.6f;
    float gap = ImGui::GetStyle().ItemSpacing.x;
    int perRow = std::max(1, (int)((ImGui::GetContentRegionAvail().x + gap) / (cell + gap)));
    int col = 0;
    for (int i = 0; i < (int)spr.doc.sprites.size(); i++) {
        const SffSprite &s = spr.doc.sprites[i];
        if (s.group != 0) continue;
        if (col++ % perRow) ImGui::SameLine();
        ImGui::PushID(i);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("g", ImVec2(cell, cell + em))) ShowInSprites(spr, i, "font " + sel);
        bool hovered = ImGui::IsItemHovered();
        ImDrawList *draw = ImGui::GetWindowDrawList();
        Checkerboard(draw, p0, ImVec2(p0.x + cell, p0.y + cell));
        if (GLuint tex = SpriteTexture(spr, i)) {
            float k = std::min(std::min(cell / std::max(1, s.w), cell / std::max(1, s.h)), 4.0f);
            ImVec2 a(p0.x + (cell - s.w * k) / 2, p0.y + (cell - s.h * k) / 2);
            draw->AddImage(TexRef(tex), a, ImVec2(a.x + s.w * k, a.y + s.h * k));
        }
        draw->AddRect(p0, ImVec2(p0.x + cell, p0.y + cell), ImGui::GetColorU32(ImGuiCol_Text, hovered ? 0.8f : 0.16f));
        char label[16];
        if (s.number >= 33 && s.number < 127)
            snprintf(label, sizeof(label), "%c", (char)s.number);
        else
            snprintf(label, sizeof(label), "%d", s.number);
        draw->AddText(ImVec2(p0.x + 2, p0.y + cell), ImGui::GetColorU32(ImGuiCol_Text), label);
        if (hovered) ImGui::SetTooltip("sprite 0, %d: %d x %d, axis %d, %d", s.number, s.w, s.h, s.x, s.y);
        ImGui::PopID();
    }
    if (col == 0) ImGui::TextDisabled("No sprites in group 0.");
    done();
}

} // namespace app
