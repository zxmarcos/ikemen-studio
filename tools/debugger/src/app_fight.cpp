// Fight screen: the fight screen's def file (lifebars, time, combo, round...), its sections and
// fields edited here (the game loads the fight screen again from the text at each change, the
// match going on; Save writes the file), its elements picked and dragged on the Game window.
#include "app_internal.h"

#include <cfloat>
#include <cmath>
#include <fstream>

#include "imgui_stdlib.h"
#include "ini.h"

namespace app {

std::string g_fightDef;
std::vector<FrameFightItem> g_fightItems;
bool g_fightPick;
static std::string g_fightSection, g_fightPrefix; // the element selected (section and prefix, lower case)

// The def parsed, again when its text changes.
static const IniFile &FightIni(const SourceFile &sf) {
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

static SourceFile *FightSource() { return g_fightDef.empty() ? nullptr : &LoadSource(g_fightDef); }

// The section the engine names (lower case), the first one with that name as the engine reads it.
static const IniSection *FightSection(const IniFile &ini, const std::string &lower) {
    for (const IniSection &s : ini.sections)
        if (s.lower == lower) return &s;
    return nullptr;
}

static std::string ItemLabel(const IniFile *ini, const std::string &section, const std::string &prefix) {
    const IniSection *s = ini ? FightSection(*ini, section) : nullptr;
    std::string p = prefix.empty() ? "" : " " + prefix.substr(0, prefix.size() - 1);
    return "[" + (s ? s->name : section) + "]" + p;
}

// The field that places an element and the others of its group: the "pos" of its prefix or of a
// shorter one ("p1.bg0." looks for p1.bg0.pos, p1.pos, pos), the first the section has; without
// any, that of its first part when it names a player or a team (p1.pos, team1.pos), else pos.
static std::string GroupPosKey(const IniSection *s, const std::string &prefix) {
    std::vector<std::string> tries;
    for (std::string p = prefix; p.size() >= 2;) {
        tries.push_back(p + "pos");
        size_t dot = p.rfind('.', p.size() - 2); // the one before the last part
        p = dot == std::string::npos ? "" : p.substr(0, dot + 1);
    }
    tries.push_back("pos");
    if (s)
        for (const std::string &k : tries)
            if (s->Field(k.c_str())) return k;
    size_t dot = prefix.find('.');
    std::string first = dot == std::string::npos ? "" : prefix.substr(0, dot);
    bool numbered = !first.empty() && isdigit((unsigned char)first.back());
    return numbered ? first + ".pos" : "pos";
}

// ---------------------------------------------------------------------------
// Reloading: the game loads the fight screen again from the text being edited (a file next to
// the def, which the game deletes once read), at most every so often while edits keep coming
// ---------------------------------------------------------------------------

static bool g_fightPending;
static double g_fightSent = -1;

// keep: the game keeps the SFF, SND and fonts in use (an edit of the text, not of the files).
static void ReloadFight(SourceFile &sf, bool fromText, bool keep = false) {
    std::string arg;
    if (fromText) {
        // One file per reload: the game deletes each once read, maybe after the next is written
        static int n;
        std::string tmp = sf.path + "." + std::to_string(++n % 100) + ".ikdbg";
        std::ofstream out(GamePath(tmp), std::ios::binary);
        out << sf.text;
        if (!out) {
            Log(LogLine::Error, "Could not write " + GamePath(tmp));
            return;
        }
        arg = LuaQuote(tmp);
    }
    if (keep) arg += ", true";
    Exec("local ok, err = debugReloadFight(" + arg + ") if not ok then error(err, 0) end");
    g_fightSent = ImGui::GetTime();
    g_fightPending = false;
}

static void FightEdited(SourceFile &sf) {
    sf.version++;
    g_fightPending = true;
}

// Sends what is pending; called every frame.
static void FightPump() {
    if (!g_fightPending || ImGui::GetTime() - g_fightSent < 0.05) return;
    if (SourceFile *sf = FightSource()) ReloadFight(*sf, true, true);
    else g_fightPending = false;
}

// Adds (dx, dy) to a position field (missing: 0, 0) of a section.
static bool MoveField(SourceFile &sf, const std::string &section, const std::string &key, float dx, float dy) {
    const IniSection *s = FightSection(FightIni(sf), section);
    if (!s) {
        Log(LogLine::Error, "The def has no [" + section + "] section.");
        return false;
    }
    float v[2] = {0, 0};
    s->Floats(key.c_str(), v, 2);
    v[0] += dx, v[1] += dy;
    return Ini_SetField(sf.text, *s, key.c_str(), Ini_Floats(v, 2));
}

// ---------------------------------------------------------------------------
// On the Game window
// ---------------------------------------------------------------------------

bool FightOverlay(ImDrawList *draw, bool hovered, bool clicked, const std::function<ImVec2(float, float)> &toScreen,
                  ImVec2 scale) {
    FightPump();
    static int drag = -1;         // the item dragged
    static bool dragAlone;        // its offset only, not its group's pos
    static std::string dragKey, dragSection, dragPrefix;
    static ImVec2 dragFrom, moved; // mouse at the start; units moved so far
    static ImVec2 dragK;           // picture pixels per unit
    SourceFile *sf = FightSource();
    const IniFile *ini = sf ? &FightIni(*sf) : nullptr;
    ImVec2 m = ImGui::GetMousePos();
    auto corners = [&](const FrameFightItem &f, ImVec2 &lo, ImVec2 &hi) {
        lo = toScreen(f.rect.x, f.rect.y);
        hi = toScreen(f.rect.x + f.rect.z, f.rect.y + f.rect.w);
    };
    // In the group dragged
    auto inDrag = [&](const FrameFightItem &f) {
        if (drag < 0 || f.section != dragSection) return false;
        if (dragAlone) return f.prefix == dragPrefix;
        std::string group = dragKey.substr(0, dragKey.size() - 3); // "p1.pos" -> "p1."
        return f.prefix.compare(0, group.size(), group) == 0;
    };

    // The element under the mouse: the last one drawn there (the one in front), the smallest
    // when one holds another (a bar's fill over its background)
    int hover = -1;
    if (g_fightPick && hovered && drag < 0) {
        float best = FLT_MAX;
        for (int i = (int)g_fightItems.size() - 1; i >= 0; i--) {
            ImVec2 lo, hi;
            corners(g_fightItems[i], lo, hi);
            float area = (hi.x - lo.x) * (hi.y - lo.y);
            if (m.x >= lo.x && m.x < hi.x && m.y >= lo.y && m.y < hi.y && area < best * 0.9f) best = area, hover = i;
        }
        if (hover >= 0) {
            const FrameFightItem &f = g_fightItems[hover];
            std::string pos = GroupPosKey(ini ? FightSection(*ini, f.section) : nullptr, f.prefix);
            ImGui::SetTooltip("%s\nDrag: move it with its group (%s); Alt+drag: it alone (%soffset)",
                              ItemLabel(ini, f.section, f.prefix).c_str(), pos.c_str(), f.prefix.c_str());
            if (clicked) {
                g_fightSection = f.section, g_fightPrefix = f.prefix;
                drag = f.k.x > 0 && f.k.y > 0 ? hover : -1;
                dragK = f.k;
                dragAlone = ImGui::GetIO().KeyAlt;
                dragSection = f.section, dragPrefix = f.prefix;
                dragKey = dragAlone ? f.prefix + "offset" : pos;
                dragFrom = m;
                moved = ImVec2(0, 0);
            }
        }
    }

    // Dragging: whole units of the def, the game following
    if (drag >= 0 && sf) {
        ImVec2 to((m.x - dragFrom.x) / scale.x / dragK.x, (m.y - dragFrom.y) / scale.y / dragK.y);
        float dx = std::round(to.x) - moved.x, dy = std::round(to.y) - moved.y;
        if ((dx != 0 || dy != 0) && MoveField(*sf, dragSection, dragKey, dx, dy)) {
            moved = ImVec2(moved.x + dx, moved.y + dy);
            FightEdited(*sf);
        }
        ImGui::SetTooltip("%s %+.0f, %+.0f", dragKey.c_str(), moved.x, moved.y);
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) || !sf) drag = -1;

    // Outlines: the selected element always, the others while picking
    float thick = std::max(1.0f, ImGui::GetFontSize() / 13);
    for (int i = 0; i < (int)g_fightItems.size(); i++) {
        const FrameFightItem &f = g_fightItems[i];
        bool sel = f.section == g_fightSection && f.prefix == g_fightPrefix, hot = i == hover;
        bool dragged = inDrag(f);
        if (!sel && !hot && !dragged && !g_fightPick) continue;
        ImVec2 lo, hi;
        corners(f, lo, hi);
        ImU32 col = sel ? IM_COL32(255, 220, 60, 255) : hot ? IM_COL32(255, 255, 255, 230)
                  : dragged ? IM_COL32(80, 220, 255, 220) : IM_COL32(200, 200, 255, 70);
        if (hot) draw->AddRectFilled(lo, hi, IM_COL32(255, 255, 255, 30));
        draw->AddRect(lo, hi, col, 0, sel || hot ? thick * 2 : thick);
        if (sel) draw->AddText(ImVec2(lo.x, hi.y + 2), col, ItemLabel(ini, f.section, f.prefix).c_str());
    }
    return g_fightPick && (hover >= 0 || drag >= 0);
}

// ---------------------------------------------------------------------------
// The window
// ---------------------------------------------------------------------------

// A field's row: its key (without the element's prefix, when it has it), its value edited as text,
// a button to remove it. True when the text changed.
static bool FieldRow(SourceFile &sf, const IniSection &s, const IniField &f, size_t strip) {
    static std::string editing;
    static int editLine = -1;
    bool changed = false;
    ImGui::PushID(f.line);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(f.key.c_str() + strip);
    if (strip) ImGui::SetItemTooltip("%s", f.key.c_str());
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
    std::string value = editLine == f.line ? editing : f.value;
    if (ImGui::InputText("##v", &value)) editing = value, editLine = f.line;
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        editLine = -1;
        if (value != f.value && !value.empty()) changed = Ini_SetField(sf.text, s, f.key.c_str(), value);
    }
    ImGui::TableNextColumn();
    if (ImGui::SmallButton("x") && !changed) changed = Ini_SetField(sf.text, s, f.key.c_str(), "");
    ImGui::SetItemTooltip("Remove this line");
    ImGui::PopID();
    return changed;
}

// A position field dragged: the game follows. True when the text changed.
static bool PositionDrag(SourceFile &sf, const IniSection &s, const std::string &key, const char *label, const char *tip) {
    float v[2] = {0, 0};
    bool has = s.Floats(key.c_str(), v, 2);
    float edit[2] = {v[0], v[1]};
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
    bool changed = false;
    if (ImGui::DragFloat2(label, edit, 1.0f, -FLT_MAX, FLT_MAX, "%.0f")) {
        float dx = std::round(edit[0] - v[0]), dy = std::round(edit[1] - v[1]);
        if (dx != 0 || dy != 0) changed = MoveField(sf, s.lower, key, dx, dy);
    }
    ImGui::SetItemTooltip("%s%s", tip, has ? "" : "\n(Not in the def yet: dragging adds it.)");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", key.c_str());
    return changed;
}

void FightWindow() {
    FightPump();
    if (!ImGui::Begin("Fight screen")) {
        ImGui::End();
        return;
    }
    SourceFile *sfp = FightSource();
    if (!sfp) {
        ImGui::TextDisabled(g_bridge.Running() ? "Needs the game's frames (patched Ikemen GO), in a match."
                                               : "Launch the game from the Session window.");
        ImGui::End();
        return;
    }
    SourceFile &sf = *sfp;
    const IniFile &ini = FightIni(sf);
    float em = ImGui::GetFontSize();

    ImGui::BeginDisabled(!sf.Dirty());
    bool save = ImGui::Button("Save (Ctrl+S)");
    ImGuiIO &io = ImGui::GetIO();
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
        save = sf.Dirty();
    if (save) SaveSource(sf);
    ImGui::SetItemTooltip("Writes the def. The game already shows every change.");
    ImGui::SameLine();
    if (ImGui::Button("Revert")) {
        ReadSource(sf);
        ReloadFight(sf, false);
    }
    ImGui::EndDisabled();
    auto flow = [](const char *label, float extra) {
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(label).x + extra) ImGui::NewLine();
    };
    float box = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x, pad = ImGui::GetStyle().FramePadding.x * 2;
    flow("Reload", pad);
    if (ImGui::Button("Reload")) ReloadFight(sf, true);
    ImGui::SetItemTooltip("The game loads the fight screen again (from the text here), e.g. after its files changed.");
    flow("Pick on the Game window", box);
    if (ImGui::Checkbox("Pick on the Game window", &g_fightPick) && g_fightPick) g_stagePick = false;
    ImGui::SetItemTooltip("The mouse picks and drags the fight screen's elements on the Game window instead of the players.");
    ImGui::TextDisabled("%s%s  %zu elements drawn", sf.path.c_str(), sf.Dirty() ? " *" : "", g_fightItems.size());

    // Left: the sections (not the animations), with how many of their elements are on screen
    if (ImGui::BeginChild("sections", ImVec2(em * 11, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX)) {
        for (int i = 0; i < (int)ini.sections.size(); i++) {
            const IniSection &s = ini.sections[i];
            if (s.lower.compare(0, 13, "begin action ") == 0) continue;
            int drawn = 0;
            for (const auto &f : g_fightItems) drawn += f.section == s.lower;
            std::string label = s.name + (drawn ? "  (" + std::to_string(drawn) + ")" : "") + "##" + std::to_string(i);
            if (!drawn) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (ImGui::Selectable(label.c_str(), g_fightSection == s.lower)) {
                if (g_fightSection != s.lower) g_fightPrefix.clear();
                g_fightSection = s.lower;
            }
            if (!drawn) ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // Right: the section selected, its elements on screen, the fields of the element selected
    ImGui::BeginGroup();
    const IniSection *sp = g_fightSection.empty() ? nullptr : FightSection(ini, g_fightSection);
    if (!sp) {
        ImGui::TextDisabled("Pick a section, or an element on the Game window.");
        ImGui::EndGroup();
        ImGui::End();
        return;
    }
    const IniSection &s = *sp;
    ImGui::Text("[%s]", s.name.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Show in Code")) {
        g_codeFile = sf.path;
        g_codeLine = g_codeScrollTo = s.line;
        g_codeHold = true;
    }
    // The elements of this section drawn in the last frame
    std::vector<std::string> prefixes;
    for (const auto &f : g_fightItems)
        if (f.section == s.lower && std::find(prefixes.begin(), prefixes.end(), f.prefix) == prefixes.end())
            prefixes.push_back(f.prefix);
    std::sort(prefixes.begin(), prefixes.end());
    if (!g_fightPrefix.empty() && std::find(prefixes.begin(), prefixes.end(), g_fightPrefix) == prefixes.end())
        prefixes.push_back(g_fightPrefix);
    for (size_t i = 0; i < prefixes.size(); i++) {
        const std::string &p = prefixes[i];
        std::string label = (p.empty() ? std::string("(no prefix)") : p.substr(0, p.size() - 1)) + "##e" + std::to_string(i);
        flow(label.c_str(), pad * 2);
        if (ImGui::Selectable(label.c_str(), p == g_fightPrefix, 0, ImVec2(ImGui::CalcTextSize(label.c_str(), nullptr, true).x, 0)))
            g_fightPrefix = p == g_fightPrefix ? "" : p;
    }
    if (prefixes.empty()) ImGui::TextDisabled("None of its elements is on screen now.");
    for (const auto &f : g_fightItems)
        if (f.section == s.lower && f.prefix == g_fightPrefix && !g_fightPrefix.empty()) {
            ImGui::TextDisabled("at %.0f, %.0f  %.0fx%.0f px (the game's picture), %.3g px per unit", f.rect.x, f.rect.y,
                                f.rect.z, f.rect.w, f.k.y);
            break;
        }

    std::string before = sf.text;
    bool changed = false;
    static bool allFields;
    if (!g_fightPrefix.empty()) {
        std::string pos = GroupPosKey(&s, g_fightPrefix);
        changed = PositionDrag(sf, s, pos, "group", "Where the element is, with the others of its group.");
        if (!changed) changed = PositionDrag(sf, s, g_fightPrefix + "offset", "offset", "Where the element is, from its group's place.");
        ImGui::Checkbox("All the section's fields", &allFields);
    }
    if (!changed && sf.text == before &&
        ImGui::BeginTable("fields", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("key");
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("");
        bool filter = !g_fightPrefix.empty() && !allFields;
        for (const IniField &f : s.fields) {
            if (filter && f.key.compare(0, g_fightPrefix.size(), g_fightPrefix) != 0) continue;
            if (!changed) changed = FieldRow(sf, s, f, filter ? g_fightPrefix.size() : 0);
        }
        // A new field (with the element's prefix to start with)
        static std::string newKey, newValue, keyFor;
        if (keyFor != g_fightPrefix) newKey = keyFor = g_fightPrefix;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(em * 9);
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
            newKey = g_fightPrefix, newValue.clear();
        }
        ImGui::EndTable();
    }
    if (changed) FightEdited(sf);
    ImGui::EndGroup();
    ImGui::End();
}

} // namespace app
