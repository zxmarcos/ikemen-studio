// Stage: the stage's def file, its sections and fields edited here (Apply saves it and the game
// loads the stage again, the match going on), and its BGs picked and dragged on the Game window.
#include "app_internal.h"

#include <cfloat>
#include <cmath>

#include "imgui_stdlib.h"
#include "ini.h"

namespace app {

std::string g_stageDef;
std::vector<FrameBg> g_frameBgs;
bool g_stagePick;
int g_stageBg = -1;

// The def parsed, again when its text changes.
const IniFile &StageIni(const SourceFile &sf) {
    static std::string text;
    static IniFile ini;
    static bool parsed;
    if (!parsed || text != sf.text) {
        text = sf.text;
        Ini_Parse(text, ini);
        parsed = true;
    }
    return ini;
}

SourceFile *StageSource() { return g_stageDef.empty() ? nullptr : &LoadSource(g_stageDef); }

// The section of a BG (by its index in the stage), nullptr when the def does not have it.
const IniSection *BgSection(const IniFile &ini, int index) {
    std::vector<int> bgs = ini.StageBgs();
    return index >= 0 && index < (int)bgs.size() ? &ini.sections[bgs[index]] : nullptr;
}

std::string BgLabel(const IniFile &ini, int index) {
    const IniSection *s = BgSection(ini, index);
    return "[" + (s ? s->name : "BG " + std::to_string(index)) + "]";
}

// Moves a BG by whole units: on the game at once, and its start in the def's text.
void MoveBg(int index, int dx, int dy) {
    SourceFile *sf = StageSource();
    if (!sf || (dx == 0 && dy == 0)) return;
    const IniSection *s = BgSection(StageIni(*sf), index);
    if (!s) return;
    float start[2];
    s->Floats("start", start, 2);
    start[0] += dx, start[1] += dy;
    if (!Ini_SetField(sf->text, *s, "start", Ini_Floats(start, 2))) return;
    sf->version++;
    char lua[96];
    snprintf(lua, sizeof(lua), "debugStageBgMove(%d, %d, %d)", index, dx, dy);
    SendRaw(lua);
}

void ApplyStage(SourceFile &sf) {
    if (sf.Dirty() && !SaveSource(sf)) return;
    Exec("local ok, err = debugReloadStage() if not ok then error(err, 0) end return 'stage loaded again'");
}

bool StageOverlay(ImDrawList *draw, bool hovered, bool clicked, const std::function<ImVec2(float, float)> &toScreen,
                  ImVec2 scale) {
    static int dragBg = -1;
    static ImVec2 rest;
    SourceFile *sf = StageSource();
    const IniFile *ini = sf ? &StageIni(*sf) : nullptr;
    auto corners = [&](const FrameBg &b, ImVec2 &lo, ImVec2 &hi) {
        lo = toScreen(b.rect.x, b.rect.y);
        hi = toScreen(b.rect.x + b.rect.z, b.rect.y + b.rect.w);
    };

    // The BG under the mouse: the last one drawn there (the one in front)
    int hover = -1;
    if (g_stagePick && hovered && dragBg < 0) {
        ImVec2 m = ImGui::GetMousePos();
        for (int i = (int)g_frameBgs.size() - 1; i >= 0 && hover < 0; i--) {
            ImVec2 lo, hi;
            corners(g_frameBgs[i], lo, hi);
            if (m.x >= lo.x && m.x < hi.x && m.y >= lo.y && m.y < hi.y) hover = i;
        }
        if (hover >= 0) {
            const FrameBg &b = g_frameBgs[hover];
            ImGui::SetTooltip("%s  layer %d%s\nDrag: move it (its start)", ini ? BgLabel(*ini, b.index).c_str() : "",
                              b.layer, b.tiled ? ", tiled" : "");
            if (clicked) {
                g_stageBg = b.index;
                dragBg = b.index;
                rest = ImVec2(0, 0);
            }
        }
    }

    // Dragging: whole units of the BG's start
    if (dragBg >= 0) {
        const FrameBg *b = nullptr;
        for (const auto &f : g_frameBgs)
            if (f.index == dragBg) b = &f;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            dragBg = -1;
        } else if (b && b->k.x != 0 && b->k.y != 0) {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            rest.x += d.x / scale.x / b->k.x;
            rest.y += d.y / scale.y / b->k.y;
            int dx = (int)std::trunc(rest.x), dy = (int)std::trunc(rest.y);
            if (dx != 0 || dy != 0) {
                MoveBg(dragBg, dx, dy);
                rest.x -= dx;
                rest.y -= dy;
            }
        }
    }

    // Outlines: the selected BG always, the others while picking
    float thick = std::max(1.0f, ImGui::GetFontSize() / 13);
    for (int i = 0; i < (int)g_frameBgs.size(); i++) {
        const FrameBg &b = g_frameBgs[i];
        bool sel = b.index == g_stageBg, hot = i == hover;
        if (!sel && !hot && !g_stagePick) continue;
        ImVec2 lo, hi;
        corners(b, lo, hi);
        ImU32 col = sel ? IM_COL32(255, 220, 60, 255) : hot ? IM_COL32(255, 255, 255, 230) : IM_COL32(200, 200, 255, 70);
        if (hot) draw->AddRectFilled(lo, hi, IM_COL32(255, 255, 255, 30));
        draw->AddRect(lo, hi, col, 0, sel || hot ? thick * 2 : thick);
        if (sel && ini) draw->AddText(ImVec2(lo.x + 4, lo.y + 2), col, BgLabel(*ini, b.index).c_str());
    }
    return g_stagePick && (hover >= 0 || dragBg >= 0);
}

void StageWindow() {
    if (!ImGui::Begin("Stage")) {
        ImGui::End();
        return;
    }
    SourceFile *sfp = StageSource();
    if (!sfp) {
        ImGui::TextDisabled(g_bridge.Running() ? "Needs the game's frames (patched Ikemen GO), in a match."
                                               : "Launch the game from the Session window.");
        ImGui::End();
        return;
    }
    SourceFile &sf = *sfp;
    const IniFile &ini = StageIni(sf);
    std::vector<int> bgs = ini.StageBgs();
    float em = ImGui::GetFontSize();

    bool apply = ImGui::Button(sf.Dirty() ? "Save and apply (Ctrl+S)" : "Apply (Ctrl+S)");
    ImGuiIO &io = ImGui::GetIO();
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
        apply = true;
    if (apply) ApplyStage(sf);
    ImGui::SetItemTooltip("Saves the def, then the game loads the stage again; the match goes on.");
    ImGui::SameLine();
    ImGui::BeginDisabled(!sf.Dirty());
    if (ImGui::Button("Revert")) ReadSource(sf);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Checkbox("Pick on the Game window", &g_stagePick);
    ImGui::SetItemTooltip("The mouse picks and drags BGs on the Game window instead of the players.");
    ImGui::TextDisabled("%s%s  %zu BGs, %zu drawn", sf.path.c_str(), sf.Dirty() ? " *" : "", bgs.size(), g_frameBgs.size());

    // The selected section: a BG picked on the game, or one clicked in the list
    static int selSection = -1;
    static int selBg = -2;
    if (g_stageBg != selBg) {
        selBg = g_stageBg;
        if (selBg >= 0 && selBg < (int)bgs.size()) selSection = bgs[selBg];
    }
    if (selSection >= (int)ini.sections.size()) selSection = -1;

    // Left: the sections
    if (ImGui::BeginChild("sections", ImVec2(em * 11, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX)) {
        for (int i = 0; i < (int)ini.sections.size(); i++) {
            const IniSection &s = ini.sections[i];
            if (s.lower.compare(0, 13, "begin action ") == 0) continue; // the stage's animations
            int bg = -1;
            for (int n = 0; n < (int)bgs.size(); n++)
                if (bgs[n] == i) bg = n;
            bool drawn = false;
            for (const auto &f : g_frameBgs) drawn = drawn || f.index == bg;
            std::string label = (bg >= 0 ? "  " : "") + s.name + "##" + std::to_string(i);
            if (bg >= 0 && !drawn) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (ImGui::Selectable(label.c_str(), selSection == i)) {
                selSection = i;
                g_stageBg = selBg = bg;
            }
            if (bg >= 0 && !drawn) {
                ImGui::PopStyleColor();
                ImGui::SetItemTooltip("Not drawn in the last frame (off screen, invisible or disabled).");
            }
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // Right: the fields of the selected section
    ImGui::BeginGroup();
    if (selSection < 0) {
        ImGui::TextDisabled("Pick a section, or a BG on the Game window.");
        ImGui::EndGroup();
        ImGui::End();
        return;
    }
    const IniSection &s = ini.sections[selSection];
    ImGui::Text("[%s]", s.name.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Show in Code")) {
        g_codeFile = sf.path;
        g_codeLine = g_codeScrollTo = s.line;
        g_codeHold = true;
    }
    std::string before = sf.text;
    if (selBg >= 0) {
        // start, dragged here or on the game: the game follows at once
        float start[2];
        s.Floats("start", start, 2);
        float edit[2] = {start[0], start[1]};
        ImGui::SetNextItemWidth(em * 10);
        if (ImGui::DragFloat2("start", edit, 1.0f, -FLT_MAX, FLT_MAX, "%.0f")) {
            int dx = (int)std::lround(edit[0] - start[0]), dy = (int)std::lround(edit[1] - start[1]);
            MoveBg(selBg, dx, dy);
        }
        ImGui::SetItemTooltip("Drag to move the BG; the game follows at once (other fields need Apply).");
        for (const auto &f : g_frameBgs)
            if (f.index == selBg) {
                ImGui::SameLine();
                ImGui::TextDisabled("layer %d%s, %.0fx%.0f px", f.layer, f.tiled ? ", tiled" : "", f.rect.z, f.rect.w);
            }
    }
    if (sf.text == before &&
        ImGui::BeginTable("fields", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("key");
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("");
        static std::string editing;
        static int editLine = -1;
        bool changed = false;
        for (const IniField &f : s.fields) {
            ImGui::PushID(f.line);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(f.key.c_str());
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            std::string value = editLine == f.line ? editing : f.value;
            if (ImGui::InputText("##v", &value)) editing = value, editLine = f.line;
            if (ImGui::IsItemDeactivatedAfterEdit() && !changed) {
                editLine = -1;
                if (value != f.value && !value.empty()) changed = Ini_SetField(sf.text, s, f.key.c_str(), value);
            }
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("x") && !changed) changed = Ini_SetField(sf.text, s, f.key.c_str(), "");
            ImGui::SetItemTooltip("Remove this line");
            ImGui::PopID();
        }
        // A new field
        static std::string newKey, newValue;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(em * 7);
        ImGui::InputTextWithHint("##nk", "new key", &newKey);
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        bool add = ImGui::InputTextWithHint("##nv", "value", &newValue, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::TableNextColumn();
        add = ImGui::SmallButton("+") || add;
        if (add && !changed && !newKey.empty() && !newValue.empty()) {
            std::string key = newKey;
            for (char &ch : key) ch = (char)tolower((unsigned char)ch);
            changed = Ini_SetField(sf.text, s, key.c_str(), newValue);
            newKey.clear(), newValue.clear();
        }
        ImGui::EndTable();
        if (changed) sf.version++;
    }
    ImGui::EndGroup();
    ImGui::End();
}

} // namespace app
