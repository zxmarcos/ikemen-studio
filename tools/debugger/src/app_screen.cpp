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
    const char *geometry; // its key in the frame geometry
    bool motif;           // read into a struct by ini tags (motif, storyboard): pos alone is not a group
    // Reloading takes ~20 ms (fight screen): a drag writes as it goes, the game following; or
    // ~400 ms (motif, in loadMotif): a drag shows the outlines moving and writes when let go
    bool live;
    double every;                     // seconds at least between reloads
    std::string def;                  // as the game names it (from the frame geometry)
    std::string sff;                  // its SFF file, the same way
    std::vector<FrameFightItem> items; // where its elements were drawn in the last frame
    std::string section, prefix;      // the element selected (lower case)
    int tag = 0;                      // and where it was drawn, when picked at one of its places (a cell)
    bool pending = false;             // an edit not sent yet
    double sent = -1;                 // when the last reload was sent
    std::string parsedText;
    IniFile ini;
    bool parsed = false;
    int animAction = -1; // the action the element selected showed when last on screen

    SourceFile *Source() { return def.empty() ? nullptr : &LoadSource(def); }
    SpriteFile *Sprites() { return def.empty() || sff.empty() ? nullptr : &SpriteFileAt(sff); }
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

static ScreenDef g_fight{"Fight screen", "fight screen", "debugReloadFight", "fight", false, true, 0.05};
static ScreenDef g_motif{"Motif", "motif", "debugReloadMotif", "motif", true, false, 0.5};
static ScreenDef g_story{"Storyboard", "storyboard", "debugReloadStoryboard", "storyboard", true, true, 0.1};
static ScreenDef *const g_screens[] = {&g_fight, &g_story, &g_motif}; // the ones drawn in front first
static ScreenDef *g_lastScreen; // the one whose element was selected last

std::string FightScreenDef() { return g_fight.def; }
std::string MotifDef() { return g_motif.def; }
int g_screenAnimPicked, g_screenPicked;

// An element selected (picked on the game or in its window).
static void SelectElement(ScreenDef &d, const std::string &section, const std::string &prefix, int tag = 0) {
    d.section = section, d.prefix = prefix, d.tag = tag;
    g_lastScreen = &d;
    g_screenPicked++;
    for (const auto &f : d.items)
        if (f.section == section && f.prefix == prefix && f.action >= 0) {
            g_screenAnimPicked++;
            break;
        }
}

bool g_screenPick;

// The first word of a section's name, as Ikemen cuts it.
static std::string FirstWordOf(const IniSection &s) { return s.lower.substr(0, s.lower.find(' ')); }

// A BG section: its first word names a BGDef the def has ("titlebg" with [TitleBGdef]).
static bool IsBgSection(const IniFile &ini, const IniSection &s) {
    std::string w = FirstWordOf(s);
    if (w == s.lower || w == "scene") return false; // [Scene 1] with a storyboard's [SceneDef]
    for (const IniSection &o : ini.sections)
        if (o.lower == w + "def") return true;
    return false;
}

void ReadScreenGeometry(const Json &j) {
    g_selectDef = j["select"].str();
    g_fontFiles.clear();
    for (const auto &f : j["fonts"].items) g_fontFiles.push_back(f.str());
    for (ScreenDef *d : g_screens) {
        const Json &g = j[d->geometry];
        d->def = g["def"].str();
        d->sff = g["sff"].str();
        d->items.clear();
        SourceFile *sf = nullptr;
        for (const auto &f : g["items"].items) {
            FrameFightItem item{f[size_t(0)].str(), f[1].str(),
                                ImVec4((float)f[2].num(), (float)f[3].num(), (float)f[4].num(), (float)f[5].num()),
                                ImVec2((float)f[6].num(), (float)f[7].num())};
            item.action = f[9].isNull() ? -1 : f[9].i();
            item.elem = f[10].i();
            item.tag = f[11].i();
            item.font = f[12].i();
            if (int bg = f[8].i(); bg > 0) {
                // The bg-th section named after the BGDef ("titlebg"), as loadBGDef reads them
                if (!sf && !(sf = d->Source())) continue;
                const IniFile &ini = d->Ini(*sf);
                std::string name = item.section;
                item.section.clear();
                for (const IniSection &s : ini.sections)
                    if (FirstWordOf(s) == name && s.lower != name && --bg == 0) {
                        item.section = s.lower;
                        break;
                    }
                if (item.section.empty()) continue;
                item.bg = true;
            }
            d->items.push_back(std::move(item));
        }
    }
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

// The field that places an element alone: its offset (in a fight screen, maybe written with a
// selector: p*.bg0.offset).
static std::string OffsetKey(const IniSection *s, const std::string &prefix, bool motif) {
    std::string key = prefix + "offset", found = motif ? "" : Ini_DefKeyFor(s, key);
    return found.empty() ? key : found;
}

// The field that places an element and the others of its group: the "pos" of its prefix or of a
// shorter one ("p1.bg0." looks for p1.bg0.pos, p1.pos, then pos in a fight screen), the first
// the section has (in a fight screen, maybe written with a selector: p*.pos). Without any: in a
// fight screen, that of its first part when it names a player or a team (p1.pos, team1.pos),
// else pos; in a motif (where pos alone can be something else, as [Select Info]'s grid), the
// element's own offset.
static std::string GroupPosKey(const IniSection *s, const std::string &prefix, bool motif) {
    std::vector<std::string> tries;
    for (std::string p = prefix; p.size() >= 2;) {
        tries.push_back(p + "pos");
        size_t dot = p.rfind('.', p.size() - 2); // the one before the last part
        p = dot == std::string::npos ? "" : p.substr(0, dot + 1);
    }
    // A storyboard's layers move together by their scene's layerall.pos
    if (s && s->lower.compare(0, 6, "scene ") == 0 && prefix.compare(0, 5, "layer") == 0) return "layerall.pos";
    // [Select Info]'s pos places its grid, the cells
    if (!motif || prefix.empty() || (s && s->lower == "select info" && prefix.compare(0, 5, "cell.") == 0))
        tries.push_back("pos");
    if (s)
        for (const std::string &k : tries) {
            std::string found = motif ? (s->Field(k.c_str()) ? k : "") : Ini_DefKeyFor(s, k);
            if (!found.empty()) return found;
        }
    if (motif) return OffsetKey(s, prefix, motif);
    size_t dot = prefix.find('.');
    std::string first = dot == std::string::npos ? "" : prefix.substr(0, dot);
    bool numbered = !first.empty() && isdigit((unsigned char)first.back());
    return numbered ? first + ".pos" : "pos";
}

// ---------------------------------------------------------------------------
// Reloading: the game loads the def again from the text being edited (a file next to the def,
// which the game deletes once read), at most every so often while edits keep coming
// ---------------------------------------------------------------------------

// keep: the game keeps the SFF, SND and fonts in use (an edit of the text, not of the files);
// sprites: the SFF all the same (it was saved).
static void Reload(ScreenDef &d, SourceFile &sf, bool fromText, bool keep = false, bool sprites = false) {
    if (!Connected()) {
        d.pending = false;
        return;
    }
    std::string arg = "nil";
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
    arg += keep ? ", true" : ", false";
    if (sprites) arg += ", true";
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

std::string ScreenSelection(const char **window) {
    ScreenDef *d = g_lastScreen;
    if (!d || d->section.empty()) return "";
    SourceFile *sf = d->Source();
    *window = d->window;
    return ItemLabel(sf ? &d->Ini(*sf) : nullptr, d->section, d->prefix);
}

// ---------------------------------------------------------------------------
// On the Game window
// ---------------------------------------------------------------------------

bool ScreenOverlay(ImDrawList *draw, bool hovered, bool clicked, const std::function<ImVec2(float, float)> &toScreen,
                   ImVec2 scale) {
    for (ScreenDef *d : g_screens) Pump(*d);
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
        std::string group = dragKey.substr(0, dragKey.size() - 3); // "p1.pos" -> "p1."; "p*.pos" -> "p*."
        size_t gd = group.find('.'), pd = f.prefix.find('.');
        if (gd == std::string::npos || pd == std::string::npos) return f.prefix.compare(0, group.size(), group) == 0;
        return Ini_SelectorMatches(group.substr(0, gd), f.prefix.substr(0, pd)) &&
               f.prefix.compare(pd, group.size() - gd, group, gd, std::string::npos) == 0;
    };

    // The element under the mouse: the last one drawn there (the one in front), the smallest
    // when one holds another (a bar's fill over its background); the fight screen's are in front
    Hit hover;
    if (g_screenPick && hovered && !drag.d) {
        float best = FLT_MAX;
        for (ScreenDef *d : g_screens)
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
            const IniSection *fs = ini ? FindSection(*ini, f.section) : nullptr;
            std::string pos = f.bg ? "start" : GroupPosKey(fs, f.prefix, d.motif);
            std::string offset = OffsetKey(fs, f.prefix, d.motif);
            if (f.bg)
                ImGui::SetTooltip("%s  (%s, a BG)\nDrag: move it (its start)", ItemLabel(ini, f.section, f.prefix).c_str(), d.what);
            else
                ImGui::SetTooltip("%s  (%s)\nDrag: move it with its group (%s); Alt+drag: it alone (%s)",
                                  ItemLabel(ini, f.section, f.prefix).c_str(), d.what, pos.c_str(), offset.c_str());
            if (clicked) {
                if (d.section != f.section || d.prefix != f.prefix) d.animAction = -1;
                SelectElement(d, f.section, f.prefix, f.tag);
                // A select screen's cell: the Roster window shows it
                bool cell = &d == &g_motif && f.section == "select info" && f.tag > 0;
                if (cell) g_rosterPick = f.tag;
                if (f.font > 0 && f.font <= (int)g_fontFiles.size()) g_fontPicked = g_fontFiles[f.font - 1]; // the Fonts window follows
                ShowWindow(cell ? "Roster" : d.window);
                drag = f.k.x > 0 && f.k.y > 0 ? hover : Hit();
                dragK = f.k;
                dragAlone = ImGui::GetIO().KeyAlt;
                dragSection = f.section, dragPrefix = f.prefix;
                dragKey = dragAlone && !f.bg ? offset : pos;
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
    for (ScreenDef *d : {&g_motif, &g_story, &g_fight}) {
        SourceFile *sf = d->Source();
        const IniFile *ini = sf ? &d->Ini(*sf) : nullptr;
        bool labelled = false; // an element drawn several times (menu items) is named once
        for (int i = 0; i < (int)d->items.size(); i++) {
            const FrameFightItem &f = d->items[i];
            bool sel = f.section == d->section && f.prefix == d->prefix && (!d->tag || f.tag == d->tag),
                 hot = hover.d == d && i == hover.i;
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
    ImGui::SetItemTooltip("%s", f.key.c_str());
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
    std::string value = editLine == f.line ? editing : f.value;
    if (ImGui::InputText("##v", &value)) editing = value, editLine = f.line;
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        editLine = -1;
        if (value != f.value && !value.empty()) changed = Ini_SetField(sf.text, s, f.key.c_str(), value);
    }
    ImGui::TableNextColumn();
    std::string picked;
    if (FieldRefCell(sf, s, f, &picked) && !changed) changed = Ini_SetField(sf.text, s, f.key.c_str(), picked);
    ImGui::TableNextColumn();
    if (ImGui::SmallButton(ICON_XMARK) && !changed) changed = Ini_SetField(sf.text, s, f.key.c_str(), "");
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
    if (!BeginTool(d.window)) return;
    SourceFile *sfp = d.Source();
    if (!sfp) {
        if (!g_bridge.Running()) EmptyNote("What the game is showing can be edited here once it is running.");
        else ImGui::TextDisabled(d.reloadFn == g_story.reloadFn ? "No storyboard is playing (intro, ending, logo...)."
                                 : d.motif                      ? "Needs the game's frames (patched Ikemen GO)."
                                                                : "Needs the game's frames (patched Ikemen GO), in a match.");
        ImGui::End();
        return;
    }
    SourceFile &sf = *sfp;
    const IniFile &ini = d.Ini(sf);
    float em = ImGui::GetFontSize();

    ImGui::BeginDisabled(!sf.Dirty());
    bool save = ImGui::Button(ICON_SAVE " Save (Ctrl+S)");
    ImGuiIO &io = ImGui::GetIO();
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
        save = sf.Dirty();
    if (save) SaveSource(sf);
    ImGui::SetItemTooltip("Writes the def. The game already shows every change.");
    ImGui::SameLine();
    if (ImGui::Button(ICON_REVERT " Revert")) {
        ReadSource(sf);
        Reload(d, sf, false);
    }
    ImGui::EndDisabled();
    auto flow = [](const char *label, float extra) {
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(label).x + extra) ImGui::NewLine();
    };
    float pad = ImGui::GetStyle().FramePadding.x * 2;
    flow("Reload", pad);
    if (ImGui::Button(ICON_RESTART " Reload")) Reload(d, sf, true);
    ImGui::SetItemTooltip("The game loads the %s again (from the text here), e.g. after its files changed.", d.what);
    UndoStack<std::string> &history = TrackAnims(sf);
    ImGui::SameLine();
    if (int step = UndoButtons(history.CanUndo(), history.CanRedo())) {
        sf.text = step < 0 ? history.Undo() : history.Redo();
        Edited(d, sf);
        ImGui::End(); // what was parsed is old
        return;
    }
    ImGui::TextDisabled("%s%s  %zu elements drawn", sf.path.c_str(), sf.Dirty() ? " *" : "", d.items.size());
    if (d.motif)
        ImGui::TextDisabled("Some values are read once, when a screen or the game starts: they show then.");

    // Left: the sections (not the animations), with how many of their elements are on screen
    if (ImGui::BeginChild("sections", ImVec2(em * 9, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX)) {
        for (int i = 0; i < (int)ini.sections.size(); i++) {
            const IniSection &s = ini.sections[i];
            if (s.lower.compare(0, 13, "begin action ") == 0) continue;
            int drawn = 0;
            for (const auto &f : d.items) drawn += f.section == s.lower;
            std::string label = s.name + (drawn ? "  (" + std::to_string(drawn) + ")" : "") + "##" + std::to_string(i);
            if (!drawn) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (ImGui::Selectable(label.c_str(), d.section == s.lower)) {
                if (d.section != s.lower) d.animAction = -1;
                SelectElement(d, s.lower, d.section != s.lower ? "" : d.prefix);
            }
            if (!drawn) ImGui::PopStyleColor();
        }
        ActionList(sf);
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
    if (ImGui::SmallButton(ICON_CODE " Show in Code")) {
        g_codeFile = sf.path;
        g_codeLine = g_codeScrollTo = s.line;
        g_codeHold = true;
    }
    // The elements of this section drawn in the last frame
    std::vector<std::string> prefixes;
    for (const auto &f : d.items)
        if (f.section == s.lower && !f.bg && std::find(prefixes.begin(), prefixes.end(), f.prefix) == prefixes.end())
            prefixes.push_back(f.prefix);
    std::sort(prefixes.begin(), prefixes.end());
    if (!d.prefix.empty() && std::find(prefixes.begin(), prefixes.end(), d.prefix) == prefixes.end())
        prefixes.push_back(d.prefix);
    for (size_t i = 0; i < prefixes.size(); i++) {
        const std::string &p = prefixes[i];
        std::string label = (p.empty() ? std::string("(no prefix)") : p.substr(0, p.size() - 1)) + "##e" + std::to_string(i);
        flow(label.c_str(), pad * 2);
        if (ImGui::Selectable(label.c_str(), p == d.prefix, 0, ImVec2(ImGui::CalcTextSize(label.c_str(), nullptr, true).x, 0))) {
            d.animAction = -1;
            SelectElement(d, d.section, p == d.prefix ? "" : p);
        }
    }
    bool bgShown = false;
    for (const auto &f : d.items) bgShown = bgShown || (f.section == s.lower && f.bg);
    if (prefixes.empty() && !bgShown) ImGui::TextDisabled("None of its elements is on screen now.");
    for (const auto &f : d.items)
        if (f.section == s.lower && f.prefix == d.prefix && (!d.prefix.empty() || f.bg)) {
            ImGui::TextDisabled("at %.0f, %.0f  %.0fx%.0f px (the game's picture), %.3g px per unit", f.rect.x, f.rect.y,
                                f.rect.z, f.rect.w, f.k.y);
            break;
        }

    std::string before = sf.text;
    bool changed = false;
    static bool allFields;
    if (IsBgSection(ini, s))
        changed = PositionDrag(d, sf, s, "start", "start", "Where the BG is (as a stage's BG).");
    if (!d.prefix.empty()) {
        std::string pos = GroupPosKey(&s, d.prefix, d.motif), offset = OffsetKey(&s, d.prefix, d.motif);
        if (pos != offset)
            changed = PositionDrag(d, sf, s, pos, "group", "Where the element is, with the others of its group.");
        if (!changed)
            changed = PositionDrag(d, sf, s, offset, "offset", "Where the element is, from its group's place.");
        ImGui::Checkbox("All the section's fields", &allFields);
    }
    if (!changed && sf.text == before &&
        ImGui::BeginTable("fields", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit)) {
        // Shares, not the longest key: a fight screen's keys would leave no room for the values
        ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthStretch, 0.5f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.5f);
        ImGui::TableSetupColumn("what");
        ImGui::TableSetupColumn("");
        bool filter = !d.prefix.empty() && !allFields;
        for (const IniField &f : s.fields) {
            // The element's keys; in a fight screen, those with a selector for it too (p*.bg0.anim),
            // shown whole
            bool own = f.key.compare(0, d.prefix.size(), d.prefix) == 0;
            if (filter && !own && (d.motif || !Ini_KeyApplies(f.key, d.prefix))) continue;
            if (!changed) changed = FieldRow(sf, s, f, filter && own ? d.prefix.size() : 0);
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
        ImGui::TableNextColumn();
        add = ImGui::SmallButton(ICON_PLUS) || add;
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
    ScreenWindow(g_story);
}

bool SelectedScreenAnim(ScreenAnim &out) {
    ScreenDef *d = g_lastScreen;
    SourceFile *sf = d ? d->Source() : nullptr;
    if (!sf || d->section.empty()) return false;
    out = ScreenAnim();
    out.sf = sf;
    for (const auto &f : d->items)
        if (f.section == d->section && f.prefix == d->prefix && f.action >= 0) {
            d->animAction = f.action;
            out.elem = f.elem;
            break;
        }
    if (d->animAction < 0) return false;
    out.action = d->animAction;
    const IniSection *s = FindSection(d->Ini(*sf), d->section);
    std::string p = d->prefix.empty() ? "" : " " + d->prefix.substr(0, d->prefix.size() - 1);
    out.label = "[" + (s ? s->name : d->section) + "]" + p + "  (" + d->what + ")";
    out.apply = [d, sf] { Edited(*d, *sf); };
    out.sprites = d->Sprites();
    return true;
}

void OutlineSelectCell(int tag) {
    g_motif.section = "select info", g_motif.prefix = "cell.bg.", g_motif.tag = tag;
}

const IniSection *MotifSection(const char *lower) {
    SourceFile *sf = g_motif.Source();
    return sf ? FindSection(g_motif.Ini(*sf), lower) : nullptr;
}

SpriteFile *SelectedScreenSprites(const char **what, int *sprite) {
    ScreenDef *d = g_lastScreen;
    SpriteFile *spf = d ? d->Sprites() : nullptr;
    SourceFile *sf = spf ? d->Source() : nullptr;
    if (!sf) return nullptr;
    if (what) *what = d->what;
    if (!sprite) return spf;
    // The sprite: of its action's element on screen, else its spr (a BG's spriteno)
    int g = -1, n = -1;
    ScreenAnim sa;
    const IniFile &ini = d->Ini(*sf);
    const IniSection *s = FindSection(ini, d->section);
    if (SelectedScreenAnim(sa)) {
        auto it = AirOf(*sf).actions.find(sa.action);
        if (sa.elem > 0 && it != AirOf(*sf).actions.end() && sa.elem <= (int)it->second.elems.size())
            g = it->second.elems[sa.elem - 1].group, n = it->second.elems[sa.elem - 1].image;
    } else if (s) {
        std::string key = IsBgSection(ini, *s) ? std::string("spriteno") : d->prefix + "spr";
        if (const IniField *f = s->Field(key.c_str())) sscanf(f->value.c_str(), "%d , %d", &g, &n);
    }
    *sprite = g >= 0 ? Sff_FindSprite(spf->doc, g, n) : -1;
    return spf;
}

void ScreenSpritesSaved(const std::string &path) {
    for (ScreenDef *d : g_screens)
        if (!d->sff.empty() && NormPath(d->sff) == NormPath(path))
            if (SourceFile *sf = d->Source()) Reload(*d, *sf, true, true, true);
    // The fight screen's FX: the SFF they read when the fight screen is loaded
    if (SourceFile *sf = g_fight.Source())
        if (NormPath(DefFile(g_fight.def, "fightfx.sff")) == NormPath(path)) Reload(g_fight, *sf, true, true, true);
}

void ScreenSoundsSaved(const std::string &path) {
    for (ScreenDef *d : g_screens) {
        if (d->def.empty()) continue;
        bool uses = NormPath(DefFile(d->def, "snd")) == NormPath(path) || (d == &g_fight && NormPath(DefFile(d->def, "common.snd")) == NormPath(path));
        if (uses)
            if (SourceFile *sf = d->Source()) Reload(*d, *sf, true);
    }
}

static ScreenDef *ScreenOfDef(const std::string &def) {
    for (ScreenDef *d : g_screens)
        if (!d->def.empty() && NormPath(d->def) == NormPath(def)) return d;
    return nullptr;
}

std::string ScreenFileName(const std::string &def) {
    if (ScreenDef *d = ScreenOfDef(def)) return d->what;
    return !g_stageDef.empty() && NormPath(g_stageDef) == NormPath(def) ? "stage" : "";
}

bool ScreenShowsAction(const std::string &def, int action, int *elem) {
    if (ScreenDef *d = ScreenOfDef(def)) {
        for (const FrameFightItem &f : d->items)
            if (f.action == action) return *elem = f.elem, true;
    } else if (!g_stageDef.empty() && NormPath(g_stageDef) == NormPath(def)) {
        for (const FrameBg &b : g_frameBgs)
            if (b.action == action) return *elem = b.elem, true;
    }
    return false;
}

void ScreenTextEdited(SourceFile &sf) {
    if (ScreenDef *d = ScreenOfDef(sf.path)) Edited(*d, sf);
    else if (!g_stageDef.empty() && NormPath(g_stageDef) == NormPath(sf.path)) StageEdited(sf);
}

void ScreenFileSaved(const std::string &path) {
    if (g_fight.def.empty() || NormPath(DefFile(g_fight.def, "fightfx.air")) != NormPath(path)) return;
    if (SourceFile *sf = g_fight.Source()) Reload(g_fight, *sf, true, true);
}

std::string ScreenDefOfSprites(const std::string &sff) {
    for (ScreenDef *d : g_screens)
        if (!d->def.empty() && NormPath(d->sff) == NormPath(sff)) return d->def;
    return !g_stageSff.empty() && NormPath(g_stageSff) == NormPath(sff) ? g_stageDef : "";
}

std::string ScreenSpritesOf(const std::string &def) {
    if (ScreenDef *d = ScreenOfDef(def)) return d->sff;
    return !g_stageDef.empty() && NormPath(g_stageDef) == NormPath(def) ? g_stageSff : "";
}

} // namespace app
