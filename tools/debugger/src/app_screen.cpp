// Fight screen and Motif: the defs of the fight screen (fight.def: lifebars, time, combo,
// round...) and of the motif (system.def: title, select, versus screens, menus...), their
// sections and fields edited here (the game loads the def again from the text at each change,
// the match or the menu going on; Save writes the file), their elements picked and dragged on
// the Game window.
#include "app_internal.h"

#include <cfloat>
#include <cmath>
#include <fstream>

#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "ini.h"

namespace app {

struct ScreenDef {
    const char *window;   // its window's title
    const char *what;     // as the tooltips name it
    const char *reloadFn; // the game's Lua function that loads it again
    bool motif;
    // Reloading takes ~20 ms (fight screen): a drag writes as it goes, the game following; or
    // ~400 ms (motif, in loadMotif): a drag shows the outlines moving and writes when let go
    bool live;
    double every;                     // seconds at least between reloads
    std::string def;                  // as the game names it (from the frame geometry)
    std::vector<FrameFightItem> items; // where its elements were drawn in the last frame
    std::string section, prefix;      // the element selected (lower case)
    bool pending = false;             // an edit not sent yet
    double sent = -1;                 // when the last reload was sent
    std::string parsedText;
    IniFile ini;
    bool parsed = false;

    SourceFile *Source() { return def.empty() ? nullptr : &LoadSource(def); }
    // The def parsed, again when its text changes.
    const IniFile &Ini(const SourceFile &sf) {
        if (!parsed || parsedText != sf.text) {
            parsedText = sf.text;
            Ini_Parse(parsedText, ini);
            parsed = true;
        }
        return ini;
    }
};

static ScreenDef g_fight{"Fight screen", "fight screen", "debugReloadFight", false, true, 0.05};
static ScreenDef g_motif{"Motif", "motif", "debugReloadMotif", true, false, 0.5};
bool g_screenPick;

void ReadScreenGeometry(const Json &j) {
    for (ScreenDef *d : {&g_fight, &g_motif}) {
        const Json &g = j[d->motif ? "motif" : "fight"];
        d->def = g["def"].str();
        d->items.clear();
        for (const auto &f : g["items"].items)
            d->items.push_back({f[size_t(0)].str(), f[1].str(),
                                ImVec4((float)f[2].num(), (float)f[3].num(), (float)f[4].num(), (float)f[5].num()),
                                ImVec2((float)f[6].num(), (float)f[7].num())});
    }
}

// A docked window's tab comes to the front, without taking the keyboard from the Game window.
static void ShowTab(const char *name) {
    ImGuiWindow *w = ImGui::FindWindowByName(name);
    if (w && w->DockNode && w->DockNode->TabBar) w->DockNode->TabBar->NextSelectedTabId = w->TabId;
}

// The section the engine names (lower case), the first one with that name as the engine reads it.
static const IniSection *FindSection(const IniFile &ini, const std::string &lower) {
    for (const IniSection &s : ini.sections)
        if (s.lower == lower) return &s;
    return nullptr;
}

static std::string ItemLabel(const IniFile *ini, const std::string &section, const std::string &prefix) {
    const IniSection *s = ini ? FindSection(*ini, section) : nullptr;
    std::string p = prefix.empty() ? "" : " " + prefix.substr(0, prefix.size() - 1);
    return "[" + (s ? s->name : section) + "]" + p;
}

// The field that places an element and the others of its group: the "pos" of its prefix or of a
// shorter one ("p1.bg0." looks for p1.bg0.pos, p1.pos, then pos in a fight screen), the first
// the section has. Without any: in a fight screen, that of its first part when it names a player
// or a team (p1.pos, team1.pos), else pos; in a motif (where pos alone can be something else, as
// [Select Info]'s grid), the element's own offset.
static std::string GroupPosKey(const IniSection *s, const std::string &prefix, bool motif) {
    std::vector<std::string> tries;
    for (std::string p = prefix; p.size() >= 2;) {
        tries.push_back(p + "pos");
        size_t dot = p.rfind('.', p.size() - 2); // the one before the last part
        p = dot == std::string::npos ? "" : p.substr(0, dot + 1);
    }
    if (!motif || prefix.empty()) tries.push_back("pos");
    if (s)
        for (const std::string &k : tries)
            if (s->Field(k.c_str())) return k;
    if (motif) return prefix + "offset";
    size_t dot = prefix.find('.');
    std::string first = dot == std::string::npos ? "" : prefix.substr(0, dot);
    bool numbered = !first.empty() && isdigit((unsigned char)first.back());
    return numbered ? first + ".pos" : "pos";
}

// ---------------------------------------------------------------------------
// Reloading: the game loads the def again from the text being edited (a file next to the def,
// which the game deletes once read), at most every so often while edits keep coming
// ---------------------------------------------------------------------------

// keep: the game keeps the SFF, SND and fonts in use (an edit of the text, not of the files).
static void Reload(ScreenDef &d, SourceFile &sf, bool fromText, bool keep = false) {
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
    Exec(std::string("local ok, err = ") + d.reloadFn + "(" + arg + ") if not ok then error(err, 0) end");
    d.sent = ImGui::GetTime();
    d.pending = false;
}

static void Edited(ScreenDef &d, SourceFile &sf) {
    sf.version++;
    d.pending = true;
}

// Sends what is pending; called every frame.
static void Pump(ScreenDef &d) {
    if (!d.pending || ImGui::GetTime() - d.sent < d.every) return;
    if (SourceFile *sf = d.Source()) Reload(d, *sf, true, true);
    else d.pending = false;
}

// Adds (dx, dy) to a position field (missing: 0, 0) of a section.
static bool MoveField(ScreenDef &d, SourceFile &sf, const std::string &section, const std::string &key, float dx, float dy) {
    const IniSection *s = FindSection(d.Ini(sf), section);
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

bool ScreenOverlay(ImDrawList *draw, bool hovered, bool clicked, const std::function<ImVec2(float, float)> &toScreen,
                   ImVec2 scale) {
    Pump(g_fight);
    Pump(g_motif);
    struct Hit {
        ScreenDef *d = nullptr;
        int i = -1;
    };
    static Hit drag;               // the item dragged
    static bool dragAlone;         // its offset only, not its group's pos
    static std::string dragKey, dragSection, dragPrefix;
    static ImVec2 dragFrom, moved; // mouse at the start; units moved so far
    static ImVec2 dragK;           // picture pixels per unit
    ImVec2 m = ImGui::GetMousePos();
    auto corners = [&](const FrameFightItem &f, ImVec2 &lo, ImVec2 &hi) {
        lo = toScreen(f.rect.x, f.rect.y);
        hi = toScreen(f.rect.x + f.rect.z, f.rect.y + f.rect.w);
    };
    // In the group dragged
    auto inDrag = [&](const ScreenDef &d, const FrameFightItem &f) {
        if (drag.d != &d || f.section != dragSection) return false;
        if (dragAlone || dragKey.size() < 3 || dragKey.compare(dragKey.size() - 3, 3, "pos") != 0) return f.prefix == dragPrefix;
        std::string group = dragKey.substr(0, dragKey.size() - 3); // "p1.pos" -> "p1."
        return f.prefix.compare(0, group.size(), group) == 0;
    };

    // The element under the mouse: the last one drawn there (the one in front), the smallest
    // when one holds another (a bar's fill over its background); the fight screen's are in front
    Hit hover;
    if (g_screenPick && hovered && !drag.d) {
        float best = FLT_MAX;
        for (ScreenDef *d : {&g_fight, &g_motif})
            for (int i = (int)d->items.size() - 1; i >= 0; i--) {
                ImVec2 lo, hi;
                corners(d->items[i], lo, hi);
                float area = (hi.x - lo.x) * (hi.y - lo.y);
                if (m.x >= lo.x && m.x < hi.x && m.y >= lo.y && m.y < hi.y && area < best * 0.9f) best = area, hover = {d, i};
            }
        if (hover.d) {
            ScreenDef &d = *hover.d;
            const FrameFightItem &f = d.items[hover.i];
            SourceFile *sf = d.Source();
            const IniFile *ini = sf ? &d.Ini(*sf) : nullptr;
            std::string pos = GroupPosKey(ini ? FindSection(*ini, f.section) : nullptr, f.prefix, d.motif);
            ImGui::SetTooltip("%s  (%s)\nDrag: move it with its group (%s); Alt+drag: it alone (%soffset)",
                              ItemLabel(ini, f.section, f.prefix).c_str(), d.what, pos.c_str(), f.prefix.c_str());
            if (clicked) {
                d.section = f.section, d.prefix = f.prefix;
                ShowTab(d.window);
                drag = f.k.x > 0 && f.k.y > 0 ? hover : Hit();
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
    SourceFile *dragSf = drag.d ? drag.d->Source() : nullptr;
    bool letGo = !ImGui::IsMouseDown(ImGuiMouseButton_Left);
    ImVec2 preview(0, 0); // units the outlines of the group dragged are shown moved by (not live)
    if (dragSf) {
        ImVec2 to((m.x - dragFrom.x) / scale.x / dragK.x, (m.y - dragFrom.y) / scale.y / dragK.y);
        float dx = std::round(to.x) - moved.x, dy = std::round(to.y) - moved.y;
        if (!drag.d->live && !letGo) {
            preview = ImVec2(std::round(to.x), std::round(to.y));
        } else if ((dx != 0 || dy != 0) && MoveField(*drag.d, *dragSf, dragSection, dragKey, dx, dy)) {
            moved = ImVec2(moved.x + dx, moved.y + dy);
            Edited(*drag.d, *dragSf);
        }
        ImGui::SetTooltip("%s %+.0f, %+.0f%s", dragKey.c_str(), drag.d->live ? moved.x : preview.x,
                          drag.d->live ? moved.y : preview.y, drag.d->live ? "" : "\nWritten when the button is let go.");
    }
    if (letGo || !dragSf) drag = Hit();

    // Outlines: the selected elements always, the others while picking
    float thick = std::max(1.0f, ImGui::GetFontSize() / 13);
    for (ScreenDef *d : {&g_motif, &g_fight}) {
        SourceFile *sf = d->Source();
        const IniFile *ini = sf ? &d->Ini(*sf) : nullptr;
        bool labelled = false; // an element drawn several times (menu items) is named once
        for (int i = 0; i < (int)d->items.size(); i++) {
            const FrameFightItem &f = d->items[i];
            bool sel = f.section == d->section && f.prefix == d->prefix, hot = hover.d == d && i == hover.i;
            bool dragged = inDrag(*d, f);
            if (!sel && !hot && !dragged && !g_screenPick) continue;
            ImVec2 lo, hi;
            corners(f, lo, hi);
            if (dragged && (preview.x != 0 || preview.y != 0)) {
                ImVec2 o(preview.x * f.k.x * scale.x, preview.y * f.k.y * scale.y);
                lo = ImVec2(lo.x + o.x, lo.y + o.y), hi = ImVec2(hi.x + o.x, hi.y + o.y);
            }
            ImU32 col = sel ? IM_COL32(255, 220, 60, 255) : hot ? IM_COL32(255, 255, 255, 230)
                      : dragged ? IM_COL32(80, 220, 255, 220) : IM_COL32(200, 200, 255, 70);
            if (hot) draw->AddRectFilled(lo, hi, IM_COL32(255, 255, 255, 30));
            draw->AddRect(lo, hi, col, 0, sel || hot ? thick * 2 : thick);
            if (sel && !labelled) draw->AddText(ImVec2(lo.x, hi.y + 2), col, ItemLabel(ini, f.section, f.prefix).c_str());
            labelled = labelled || sel;
        }
    }
    return g_screenPick && (hover.d || drag.d);
}

// ---------------------------------------------------------------------------
// The windows
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
static bool PositionDrag(ScreenDef &d, SourceFile &sf, const IniSection &s, const std::string &key, const char *label,
                         const char *tip) {
    float v[2] = {0, 0};
    bool has = s.Floats(key.c_str(), v, 2);
    float edit[2] = {v[0], v[1]};
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
    bool changed = false;
    if (ImGui::DragFloat2(label, edit, 1.0f, -FLT_MAX, FLT_MAX, "%.0f")) {
        float dx = std::round(edit[0] - v[0]), dy = std::round(edit[1] - v[1]);
        if (dx != 0 || dy != 0) changed = MoveField(d, sf, s.lower, key, dx, dy);
    }
    ImGui::SetItemTooltip("%s%s", tip, has ? "" : "\n(Not in the def yet: dragging adds it.)");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", key.c_str());
    return changed;
}

static void ScreenWindow(ScreenDef &d) {
    Pump(d);
    if (!ImGui::Begin(d.window)) {
        ImGui::End();
        return;
    }
    SourceFile *sfp = d.Source();
    if (!sfp) {
        ImGui::TextDisabled(g_bridge.Running() ? (d.motif ? "Needs the game's frames (patched Ikemen GO)."
                                                          : "Needs the game's frames (patched Ikemen GO), in a match.")
                                               : "Launch the game from the Session window.");
        ImGui::End();
        return;
    }
    SourceFile &sf = *sfp;
    const IniFile &ini = d.Ini(sf);
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
        Reload(d, sf, false);
    }
    ImGui::EndDisabled();
    auto flow = [](const char *label, float extra) {
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(label).x + extra) ImGui::NewLine();
    };
    float box = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x, pad = ImGui::GetStyle().FramePadding.x * 2;
    flow("Reload", pad);
    if (ImGui::Button("Reload")) Reload(d, sf, true);
    ImGui::SetItemTooltip("The game loads the %s again (from the text here), e.g. after its files changed.", d.what);
    flow("Pick on the Game window", box);
    if (ImGui::Checkbox("Pick on the Game window", &g_screenPick) && g_screenPick) g_stagePick = false;
    ImGui::SetItemTooltip("The mouse picks and drags the fight screen's and the motif's elements on the Game window\n"
                          "instead of the players.");
    ImGui::TextDisabled("%s%s  %zu elements drawn", sf.path.c_str(), sf.Dirty() ? " *" : "", d.items.size());
    if (d.motif)
        ImGui::TextDisabled("Some values are read once, when a screen or the game starts: they show then.");

    // Left: the sections (not the animations), with how many of their elements are on screen
    if (ImGui::BeginChild("sections", ImVec2(em * 11, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX)) {
        for (int i = 0; i < (int)ini.sections.size(); i++) {
            const IniSection &s = ini.sections[i];
            if (s.lower.compare(0, 13, "begin action ") == 0) continue;
            int drawn = 0;
            for (const auto &f : d.items) drawn += f.section == s.lower;
            std::string label = s.name + (drawn ? "  (" + std::to_string(drawn) + ")" : "") + "##" + std::to_string(i);
            if (!drawn) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (ImGui::Selectable(label.c_str(), d.section == s.lower)) {
                if (d.section != s.lower) d.prefix.clear();
                d.section = s.lower;
            }
            if (!drawn) ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // Right: the section selected, its elements on screen, the fields of the element selected
    ImGui::BeginGroup();
    const IniSection *sp = d.section.empty() ? nullptr : FindSection(ini, d.section);
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
    for (const auto &f : d.items)
        if (f.section == s.lower && std::find(prefixes.begin(), prefixes.end(), f.prefix) == prefixes.end())
            prefixes.push_back(f.prefix);
    std::sort(prefixes.begin(), prefixes.end());
    if (!d.prefix.empty() && std::find(prefixes.begin(), prefixes.end(), d.prefix) == prefixes.end())
        prefixes.push_back(d.prefix);
    for (size_t i = 0; i < prefixes.size(); i++) {
        const std::string &p = prefixes[i];
        std::string label = (p.empty() ? std::string("(no prefix)") : p.substr(0, p.size() - 1)) + "##e" + std::to_string(i);
        flow(label.c_str(), pad * 2);
        if (ImGui::Selectable(label.c_str(), p == d.prefix, 0, ImVec2(ImGui::CalcTextSize(label.c_str(), nullptr, true).x, 0)))
            d.prefix = p == d.prefix ? "" : p;
    }
    if (prefixes.empty()) ImGui::TextDisabled("None of its elements is on screen now.");
    for (const auto &f : d.items)
        if (f.section == s.lower && f.prefix == d.prefix && !d.prefix.empty()) {
            ImGui::TextDisabled("at %.0f, %.0f  %.0fx%.0f px (the game's picture), %.3g px per unit", f.rect.x, f.rect.y,
                                f.rect.z, f.rect.w, f.k.y);
            break;
        }

    std::string before = sf.text;
    bool changed = false;
    static bool allFields;
    if (!d.prefix.empty()) {
        std::string pos = GroupPosKey(&s, d.prefix, d.motif);
        if (pos != d.prefix + "offset")
            changed = PositionDrag(d, sf, s, pos, "group", "Where the element is, with the others of its group.");
        if (!changed)
            changed = PositionDrag(d, sf, s, d.prefix + "offset", "offset", "Where the element is, from its group's place.");
        ImGui::Checkbox("All the section's fields", &allFields);
    }
    if (!changed && sf.text == before &&
        ImGui::BeginTable("fields", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("key");
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("");
        bool filter = !d.prefix.empty() && !allFields;
        for (const IniField &f : s.fields) {
            if (filter && f.key.compare(0, d.prefix.size(), d.prefix) != 0) continue;
            if (!changed) changed = FieldRow(sf, s, f, filter ? d.prefix.size() : 0);
        }
        // A new field (with the element's prefix to start with)
        static std::string newKey, newValue, keyFor;
        if (keyFor != d.prefix) newKey = keyFor = d.prefix;
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
            newKey = d.prefix, newValue.clear();
        }
        ImGui::EndTable();
    }
    if (changed) Edited(d, sf);
    ImGui::EndGroup();
    ImGui::End();
}

void ScreenWindows() {
    ScreenWindow(g_fight);
    ScreenWindow(g_motif);
}

} // namespace app
