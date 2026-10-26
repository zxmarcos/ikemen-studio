// Fonts: the font files the game loaded (frame geometry, editor_font.go), a .def font's [Def]
// edited here (the game reads it again into the fonts in use at each change; Save writes it),
// its glyphs (the sprites of its SFF: group 0, the number a character's code) shown and edited
// in the Sprites window, and a sample text drawn as the engine draws it.
#include "app_internal.h"

#include <cfloat>
#include <cmath>
#include <fstream>

#include "imgui_stdlib.h"

namespace app {

std::vector<std::string> g_fontFiles;
std::string g_fontPicked;

static std::string LowerOf(std::string s) {
    for (char &ch : s) ch = (char)tolower((unsigned char)ch);
    return s;
}

static bool IsDefFont(const std::string &path) {
    return path.size() > 4 && LowerOf(path.substr(path.size() - 4)) == ".def";
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

// Reloading: the text being edited goes to a file next to the def, which the game reads
static std::string g_fontPending; // the def with an edit not sent yet
static double g_fontSent = -1;

static void FontReload(const std::string &def, SourceFile *sf) {
    std::string arg = LuaQuote(def);
    if (sf) {
        static int n;
        std::string tmp = def + "." + std::to_string(++n % 100) + ".ikdbg";
        std::ofstream out(GamePath(tmp), std::ios::binary);
        out << sf->text;
        if (!out) {
            Log(LogLine::Error, "Could not write " + GamePath(tmp));
            return;
        }
        arg += ", " + LuaQuote(tmp);
    }
    Exec("local ok, err = debugReloadFont(" + arg + ") if not ok then error(err, 0) end");
    g_fontSent = ImGui::GetTime();
}

void FontSpritesSaved(const std::string &path) {
    for (const std::string &def : g_fontFiles) {
        if (!IsDefFont(def)) continue;
        IniFile ini;
        Ini_Parse(LoadSource(def).text, ini);
        const IniSection *s = DefSection(ini);
        const IniField *f = s ? s->Field("file") : nullptr;
        if (f && NormPath(FontSffPath(def, f->value)) == NormPath(path)) FontReload(def, nullptr);
    }
}

void FontsWindow() {
    if (!g_fontPending.empty() && ImGui::GetTime() - g_fontSent > 0.1) {
        FontReload(g_fontPending, &LoadSource(g_fontPending));
        g_fontPending.clear();
    }
    if (!ImGui::Begin("Fonts")) {
        ImGui::End();
        return;
    }
    static std::string sel, seenPicked;
    if (g_fontPicked != seenPicked) seenPicked = sel = g_fontPicked; // a text picked on the game
    float em = ImGui::GetFontSize();
    if (g_fontFiles.empty()) {
        ImGui::TextDisabled(g_bridge.Running() ? "Needs the game's frames (patched Ikemen GO)." : "Launch the game from the Session window.");
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
    if (sel.empty()) {
        ImGui::TextDisabled("Pick a font, or a text on the Game window (Screenpack on).");
        ImGui::EndChild();
        ImGui::End();
        return;
    }
    if (!IsDefFont(sel)) {
        ImGui::TextDisabled("%s: a MUGEN 1.0 font (.fnt, one binary file), not edited here.", sel.c_str());
        ImGui::EndChild();
        ImGui::End();
        return;
    }
    SourceFile &sf = LoadSource(sel);
    IniFile ini;
    Ini_Parse(sf.text, ini);
    const IniSection *def = DefSection(ini);
    std::string before = sf.text;

    ImGui::BeginDisabled(!sf.Dirty());
    if (ImGui::Button("Save")) SaveSource(sf);
    ImGui::SetItemTooltip("Writes the font's def. The game already shows every change.");
    ImGui::SameLine();
    if (ImGui::Button("Revert")) {
        ReadSource(sf);
        FontReload(sel, nullptr);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Show in Code")) {
        g_codeFile = sf.path;
        g_codeLine = g_codeScrollTo = def ? def->line : 1;
        g_codeHold = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s%s", sf.path.c_str(), sf.Dirty() ? " *" : "");
    if (!def) {
        ImGui::TextDisabled("No [Def] section.");
        ImGui::EndChild();
        ImGui::End();
        return;
    }

    // [Def]: the numbers dragged, the rest as text
    std::string type = def->Field("type") ? LowerOf(def->Field("type")->value) : "bitmap";
    auto pair = [&](const char *key, const char *help) {
        float v[2] = {0, 0};
        def->Floats(key, v, 2);
        int iv[2] = {(int)v[0], (int)v[1]};
        ImGui::SetNextItemWidth(em * 9);
        if (ImGui::DragInt2(key, iv, 0.1f) && sf.text == before) {
            float nv[2] = {(float)iv[0], (float)iv[1]};
            Ini_SetField(sf.text, *def, key, Ini_Floats(nv, 2));
        }
        ImGui::SetItemTooltip("%s", help);
    };
    pair("size", "Width (that of a space) and height of the characters.");
    pair("spacing", "Added between characters, and between lines.");
    pair("offset", "Where the text is drawn from its position.");
    for (const char *key : {"type", "banktype", "colors", "file"}) {
        const IniField *f = def->Field(key);
        std::string v = f ? f->value : "";
        ImGui::SetNextItemWidth(em * 12);
        if (ImGui::InputText(key, &v, ImGuiInputTextFlags_EnterReturnsTrue) && sf.text == before)
            Ini_SetField(sf.text, *def, key, v);
    }
    if (sf.text != before) {
        sf.version++;
        g_fontPending = sel;
    }

    // The glyphs (a bitmap font): its SFF's sprites of group 0, with a sample text
    if (type == "truetype") {
        ImGui::TextDisabled("A TrueType font: its glyphs are in the .ttf file.");
        ImGui::EndChild();
        ImGui::End();
        return;
    }
    std::string sffPath = FontSffPath(sel, def->Field("file") ? def->Field("file")->value : "");
    if (sffPath.empty()) {
        ImGui::TextDisabled("Its SFF file is not found.");
        ImGui::EndChild();
        ImGui::End();
        return;
    }
    SpriteFile &spr = SpriteFileAt(sffPath);
    float size[2] = {0, 0}, spacing[2] = {0, 0}, offset[2] = {0, 0};
    def->Floats("size", size, 2);
    def->Floats("spacing", spacing, 2);
    def->Floats("offset", offset, 2);

    ImGui::SeparatorText("Sample");
    static std::string sample = "The quick brown fox 0123456789!?";
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##sample", &sample);
    {
        static float zoom = 2;
        static std::string zoomFor;
        if (zoomFor != sel) zoomFor = sel, zoom = std::max(1.0f, std::floor(24 / std::max(size[1], 1.0f)));
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
        // As Fnt.DrawText: the baseline at the position; a glyph drawn by its axis
        float base = p0.y + h * 0.7f, pen = p0.x + 4 + offset[0] * zoom, top = base + (offset[1] - size[1] + 1) * zoom;
        draw->AddLine(ImVec2(p0.x, base), ImVec2(p1.x, base), IM_COL32(255, 220, 60, 80));
        for (unsigned char ch : sample) {
            int i = ch == ' ' ? -1 : Sff_FindSprite(spr.doc, 0, ch);
            if (i < 0) {
                pen += (size[0] + spacing[0]) * zoom; // a space, or a character the font lacks
                continue;
            }
            const SffSprite &s = spr.doc.sprites[i];
            if (GLuint tex = SpriteTexture(spr, i)) {
                ImVec2 a(pen - s.x * zoom, top - s.y * zoom);
                draw->AddImage(TexRef(tex), a, ImVec2(a.x + s.w * zoom, a.y + s.h * zoom));
            }
            pen += (s.w + spacing[0]) * zoom;
        }
        draw->PopClipRect();
        ImGui::SetItemTooltip("As the game draws it (bank 0). Wheel: zoom.");
    }

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
            float k = std::min(cell / std::max(1, s.w), cell / std::max(1, s.h));
            k = std::min(k, 4.0f);
            ImVec2 a(p0.x + (cell - s.w * k) / 2, p0.y + (cell - s.h * k) / 2);
            draw->AddImage(TexRef(tex), a, ImVec2(a.x + s.w * k, a.y + s.h * k));
        }
        draw->AddRect(p0, ImVec2(p0.x + cell, p0.y + cell), hovered ? IM_COL32(255, 255, 255, 200) : IM_COL32(255, 255, 255, 40));
        char label[16];
        if (s.number >= 33 && s.number < 127)
            snprintf(label, sizeof(label), "%c", (char)s.number);
        else
            snprintf(label, sizeof(label), "%d", s.number);
        draw->AddText(ImVec2(p0.x + 2, p0.y + cell), IM_COL32(200, 200, 200, 255), label);
        if (hovered) ImGui::SetTooltip("sprite 0, %d: %d x %d, axis %d, %d", s.number, s.w, s.h, s.x, s.y);
        ImGui::PopID();
    }
    if (col == 0) ImGui::TextDisabled("No sprites in group 0.");
    ImGui::EndChild();
    ImGui::End();
}

} // namespace app
