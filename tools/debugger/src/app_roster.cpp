// Roster: the motif's select.def (src/roster.cpp), its characters in the select screen's grid
// and its extra stages, edited here; each change has the game add them again
// (ikdbg.reloadRoster), the select screen showing them at once. Save writes the file.
#include "app_internal.h"

#include <cfloat>
#include <fstream>

#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "roster.h"

namespace app {

std::string g_selectDef; // as the game names it (frame geometry)
int g_rosterPick;

// ---------------------------------------------------------------------------
// What the entries name: a character's def, name and portrait (sprite 9000,0), a stage's name
// ---------------------------------------------------------------------------

struct DefInfo {
    std::string def, name; // def: as the game names it, "" when not found
    SpriteFile *sprites = nullptr;
    int portrait = -1;
    bool spritesRead = false;
};
static std::map<std::string, DefInfo> g_defInfo; // by the entry's name, lower case

static std::string LowerOf(std::string s) {
    for (char &ch : s) ch = (char)tolower((unsigned char)ch);
    return s;
}

static bool FileExists(const std::string &path) {
    DWORD a = GetFileAttributesA(GamePath(path).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static std::string Unquote(std::string s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
    return s;
}

static std::string DirOf(const std::string &path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? "" : path.substr(0, slash + 1);
}

// As Select.AddChar finds it: "kfm" is kfm/kfm.def, "kfm/alt" kfm/alt.def, under chars/
static std::string CharDefPath(const std::string &name) {
    std::string guess = name;
    if (LowerOf(guess).size() < 4 || LowerOf(guess).compare(guess.size() - 4, 4, ".def") != 0)
        guess = guess.find('/') == std::string::npos ? guess + "/" + guess + ".def" : guess + ".def";
    for (const char *dir : {"chars/", "", "data/"})
        if (FileExists(dir + guess)) return dir + guess;
    return "";
}

// The info of an entry (character or stage). budget: reading an SFF uses it (one per frame).
static DefInfo &InfoOf(const std::string &name, bool stage, bool &budget) {
    DefInfo &d = g_defInfo[LowerOf(name)];
    if (d.def.empty() && d.name.empty()) {
        d.def = stage ? (FileExists(name) ? name : "") : CharDefPath(name);
        d.name = name;
        if (!d.def.empty()) {
            IniFile ini;
            Ini_Parse(ReadWholeFile(GamePath(d.def)), ini);
            for (const IniSection &s : ini.sections)
                if (s.lower == "info") {
                    const IniField *f = s.Field("displayname");
                    if (!f || Unquote(f->value).empty()) f = s.Field("name");
                    if (f && !Unquote(f->value).empty()) d.name = Unquote(f->value);
                }
        }
    }
    if (!stage && !d.spritesRead && !d.def.empty() && budget) {
        budget = false;
        d.spritesRead = true;
        IniFile ini;
        Ini_Parse(ReadWholeFile(GamePath(d.def)), ini);
        std::string spr, pal;
        for (const IniSection &s : ini.sections)
            if (s.lower == "files") {
                if (const IniField *f = s.Field("sprite")) spr = f->value;
                if (const IniField *f = s.Field("pal1")) pal = f->value;
            }
        if (!spr.empty()) {
            std::string dir = DirOf(d.def);
            d.sprites = &SpriteFileAt(dir + spr, pal.empty() ? "" : dir + pal);
            d.portrait = Sff_FindSprite(d.sprites->doc, 9000, 0);
        }
    }
    return d;
}

// The folders' contents, for adding: characters as select.def names them, stages' defs
std::vector<std::string> ListDefs(bool stages) {
    std::vector<std::string> out;
    WIN32_FIND_DATAA fd;
    auto each = [&](const std::string &pattern, const std::function<void(const WIN32_FIND_DATAA &)> &f) {
        HANDLE h = FindFirstFileA(GamePath(pattern).c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        do f(fd);
        while (FindNextFileA(h, &fd));
        FindClose(h);
    };
    if (stages) {
        each("stages/*.def", [&](const WIN32_FIND_DATAA &f) { out.push_back(std::string("stages/") + f.cFileName); });
    } else {
        each("chars/*", [&](const WIN32_FIND_DATAA &f) {
            std::string d = f.cFileName;
            if (!(f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || d == "." || d == "..") return;
            if (FileExists("chars/" + d + "/" + d + ".def")) {
                out.push_back(d);
                return;
            }
            std::vector<std::string> defs;
            WIN32_FIND_DATAA inner;
            HANDLE h = FindFirstFileA(GamePath("chars/" + d + "/*.def").c_str(), &inner);
            if (h == INVALID_HANDLE_VALUE) return;
            do defs.push_back(d + "/" + inner.cFileName);
            while (FindNextFileA(h, &inner));
            FindClose(h);
            out.insert(out.end(), defs.begin(), defs.end());
        });
    }
    std::sort(out.begin(), out.end());
    return out;
}

// ---------------------------------------------------------------------------
// Reloading: the game adds the roster again from the text being edited (a file next to
// select.def, which the game deletes once read)
// ---------------------------------------------------------------------------

static bool g_rosterPending;
static double g_rosterSent = -1;

static void RosterReload(SourceFile &sf, bool fromText) {
    std::string arg;
    if (fromText) {
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
    Exec("ikdbg.reloadRoster(" + arg + ")");
    g_rosterSent = ImGui::GetTime();
    g_rosterPending = false;
}

static void RosterEdited(SourceFile &sf) {
    sf.version++;
    g_rosterPending = true;
}

// ---------------------------------------------------------------------------
// The window
// ---------------------------------------------------------------------------

// A cell's picture: the portrait of its (first) character, else a word
static void DrawCell(ImDrawList *draw, ImVec2 a, float size, const RosterCell *c, bool &budget) {
    ImVec2 b(a.x + size, a.y + size);
    draw->AddRectFilled(a, b, IM_COL32(40, 40, 46, 255));
    if (!c || c->entries.empty()) return;
    const RosterEntry &e = c->entries[0];
    std::string lower = LowerOf(e.name);
    const char *word = lower == "randomselect" ? "?" : lower == "skipslot" ? "skip" : lower == "emptyslot" ? "empty" : nullptr;
    if (!word) {
        DefInfo &d = InfoOf(e.name, false, budget);
        GLuint tex = d.sprites && d.portrait >= 0 ? SpriteTexture(*d.sprites, d.portrait) : 0;
        if (tex) {
            const SffSprite &s = d.sprites->doc.sprites[d.portrait];
            float k = std::min(size / std::max(1, s.w), size / std::max(1, s.h));
            ImVec2 p(a.x + (size - s.w * k) / 2, a.y + (size - s.h * k) / 2);
            draw->AddImage(TexRef(tex), p, ImVec2(p.x + s.w * k, p.y + s.h * k));
        } else {
            word = d.def.empty() ? "missing" : nullptr;
            draw->PushClipRect(a, b, true);
            draw->AddText(ImVec2(a.x + 2, a.y + 2), IM_COL32(220, 220, 220, 255), d.name.c_str());
            draw->PopClipRect();
        }
    }
    if (word) {
        ImVec2 ts = ImGui::CalcTextSize(word);
        draw->AddText(ImVec2(a.x + (size - ts.x) / 2, a.y + (size - ts.y) / 2),
                      lower == "randomselect" ? IM_COL32(255, 220, 60, 255) : IM_COL32(160, 160, 170, 255), word);
    }
    if (c->slot) draw->AddText(ImVec2(a.x + 2, b.y - ImGui::GetFontSize()), IM_COL32(120, 200, 255, 255),
                               ("slot " + std::to_string(c->entries.size())).c_str());
    if (e.Param("hidden") != "" && e.Param("hidden") != "0")
        draw->AddText(ImVec2(b.x - ImGui::CalcTextSize("h").x - 2, a.y), IM_COL32(255, 120, 120, 255), "h");
}

// The parameters most used, as fields; the others stay in the line's text
static bool EntryFields(RosterEntry &e, bool stage) {
    bool changed = false;
    float em = ImGui::GetFontSize();
    auto field = [&](const char *key, const char *help) {
        std::string v = e.Param(key);
        ImGui::SetNextItemWidth(em * 8);
        if (ImGui::InputText(key, &v, ImGuiInputTextFlags_EnterReturnsTrue)) {
            e.SetParam(key, v);
            changed = true;
        }
        ImGui::SetItemTooltip("%s", help);
    };
    field("order", "Roster-building modes fight smaller orders first (default 1).");
    if (stage) {
        field("music", "The stage's music, instead of the stage def's bgmusic.");
        return changed;
    }
    int hidden = atoi(e.Param("hidden").c_str());
    ImGui::SetNextItemWidth(em * 8);
    if (ImGui::Combo("hidden", &hidden, "0: shown\0" "1: no art, selectable\0" "2: until unlocked\0" "3: random until unlocked\0")) {
        e.SetParam("hidden", hidden ? std::to_string(hidden) : "");
        changed = true;
    }
    bool exclude = e.Param("exclude") == "1";
    if (ImGui::Checkbox("exclude (in no cell)", &exclude)) {
        e.SetParam("exclude", exclude ? "1" : "");
        changed = true;
    }
    field("music", "Music when facing this character (instead of the stage's).");
    field("includestage", "0: its stages not in the stage list; -1: listed, never random.");
    field("ai", "1 to 8: this AI level, whatever the difficulty.");
    // Its stages
    std::vector<std::string> stages = e.Stages();
    ImGui::TextUnformatted("stages:");
    for (size_t i = 0; i < stages.size(); i++) {
        ImGui::SameLine();
        ImGui::PushID((int)i);
        if (ImGui::SmallButton((stages[i] + "  x").c_str())) {
            for (size_t k = 0; k < e.items.size(); k++)
                if (e.items[k] == stages[i]) {
                    e.items.erase(e.items.begin() + k);
                    break;
                }
            changed = true;
        }
        ImGui::PopID();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(ICON_PLUS)) ImGui::OpenPopup("addstage");
    if (ImGui::BeginPopup("addstage")) {
        static std::vector<std::string> list;
        if (ImGui::IsWindowAppearing()) list = ListDefs(true);
        // Stages go before the parameters
        auto insertStage = [&](const std::string &s) {
            size_t at = 0;
            while (at < e.items.size() && e.items[at].find('=') == std::string::npos) at++;
            e.items.insert(e.items.begin() + at, s);
            changed = true;
            ImGui::CloseCurrentPopup();
        };
        if (ImGui::Selectable("random")) insertStage("random");
        for (const std::string &s : list)
            if (ImGui::Selectable(s.c_str())) insertStage(s);
        ImGui::EndPopup();
    }
    return changed;
}

// A list of names to pick one from (characters, stages), with a filter
static bool PickName(const char *id, bool stages, std::string &picked) {
    bool done = false;
    if (ImGui::BeginPopup(id)) {
        static std::vector<std::string> list;
        static std::string filter;
        if (ImGui::IsWindowAppearing()) {
            list = ListDefs(stages);
            filter.clear();
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
        ImGui::InputTextWithHint("##f", "filter", &filter);
        if (!stages) {
            for (const char *k : {"randomselect", "emptyslot", "skipslot"})
                if (ImGui::Selectable(k)) picked = k, done = true;
            ImGui::Separator();
        }
        if (ImGui::BeginChild("names", ImVec2(ImGui::GetFontSize() * 14, ImGui::GetFontSize() * 16))) {
            for (const std::string &s : list)
                if ((filter.empty() || LowerOf(s).find(LowerOf(filter)) != std::string::npos) && ImGui::Selectable(s.c_str()))
                    picked = s, done = true;
        }
        ImGui::EndChild();
        if (done) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return done;
}

void RosterWindow() {
    if (g_rosterPending && ImGui::GetTime() - g_rosterSent > 0.3 && !g_selectDef.empty())
        RosterReload(LoadSource(g_selectDef), true);
    if (!BeginTool("Roster")) return;
    if (g_selectDef.empty()) {
        ImGui::TextDisabled(g_bridge.Running() ? "Needs the game's frames (patched Ikemen GO)." : "Launch the game (Launch, on the toolbar).");
        ImGui::End();
        return;
    }
    SourceFile &sf = LoadSource(g_selectDef);
    static RosterFile r;
    static std::string parsed;
    static bool parsedOnce;
    if (!parsedOnce || parsed != sf.text) parsed = sf.text, parsedOnce = true, Roster_Parse(parsed, r);
    float em = ImGui::GetFontSize();
    bool budget = true; // one SFF read per frame
    std::string before = sf.text;

    ImGui::BeginDisabled(!sf.Dirty());
    if (ImGui::Button(ICON_SAVE " Save")) SaveSource(sf);
    ImGui::SetItemTooltip("Writes select.def. The game already shows every change (in menus; not during a match).");
    ImGui::SameLine();
    if (ImGui::Button(ICON_REVERT " Revert")) {
        ReadSource(sf);
        RosterReload(sf, false);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(ICON_CODE " Show in Code")) {
        g_codeFile = sf.path;
        g_codeLine = g_codeScrollTo = r.charsLine;
        g_codeHold = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s%s", sf.path.c_str(), sf.Dirty() ? " *" : "");

    // The grid as the motif lays it out: [Select Info] rows, columns, cells skipped
    const IniSection *si = MotifSection("select info");
    int rows = 0, cols = 0;
    if (si) {
        float v = 0;
        if (si->Floats("rows", &v, 1)) rows = (int)v;
        if (si->Floats("columns", &v, 1)) cols = (int)v;
    }
    if (rows <= 0 || cols <= 0) rows = 0, cols = 8;
    static int sel = -1; // index into r.chars
    if (sel >= (int)r.chars.size()) sel = -1;
    // The roster cell each cell of the grid shows (-1 none, -2 skipped by the motif), in the
    // order the scripts fill them (excluded characters take none); the cells past the grid
    std::vector<int> grid(rows * cols, -1), past;
    {
        int next = 0;
        auto nextCell = [&]() {
            while (next < (int)r.chars.size() && r.chars[next].exclude) next++;
            return next < (int)r.chars.size() ? next++ : -1;
        };
        for (int i = 0; i < rows * cols; i++) {
            char key[48];
            snprintf(key, sizeof(key), "cell.%d-%d.skip", i % cols, i / cols);
            const IniField *skip = si ? si->Field(key) : nullptr;
            grid[i] = skip && atoi(skip->value.c_str()) != 0 ? -2 : nextCell();
        }
        for (int ci; (ci = nextCell()) >= 0;) past.push_back(ci);
    }
    // A cell picked on the Game window
    if (g_rosterPick > 0) {
        if (g_rosterPick <= (int)grid.size() && grid[g_rosterPick - 1] >= 0) sel = grid[g_rosterPick - 1];
        g_rosterPick = 0;
    }

    if (ImGui::BeginTabBar("rostertabs")) {
        if (ImGui::BeginTabItem("Characters")) {
            if (r.charsLine == 0) ImGui::TextDisabled("select.def has no [Characters] section.");
            // Left: the grid, then the cells past it and the excluded characters
            float cell = em * 3.2f, gap = 3;
            ImGui::BeginChild("grid", ImVec2(std::min(ImGui::GetContentRegionAvail().x * 0.6f, cols * (cell + gap) + em * 2), 0),
                              ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
            ImDrawList *draw = ImGui::GetWindowDrawList();
            int dropFrom = -1, dropTo = -1; // a cell dropped on another (to: r.chars.size() for the end)
            bool openInsert = false;        // a character to insert before the cell selected
            // A cell: ci -1 for an empty one of the grid; gridIndex -1 for one past the grid
            auto slotOf = [&](int ci, ImVec2 a, int gridIndex) {
                ImGui::SetCursorScreenPos(a);
                ImGui::PushID(ci < 0 ? 100000 + gridIndex : ci);
                ImGui::InvisibleButton("c", ImVec2(cell, cell));
                bool hovered = ImGui::IsItemHovered();
                if (ci >= 0) {
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemClicked(ImGuiMouseButton_Right)) sel = ci;
                    // On a select screen, P1's cursor goes there
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && gridIndex >= 0) {
                        SendRaw("ikdbg.selectCursor(" + std::to_string(gridIndex % cols) + ", " + std::to_string(gridIndex / cols) + ")");
                        OutlineSelectCell(gridIndex + 1);
                    }
                    if (ImGui::BeginDragDropSource()) {
                        ImGui::SetDragDropPayload("rostercell", &ci, sizeof(int));
                        ImGui::TextUnformatted(r.chars[ci].Text().c_str());
                        ImGui::EndDragDropSource();
                    }
                    if (hovered) ImGui::SetTooltip("%s\nline %d. Drag it to another cell; right click: more.", r.chars[ci].Text().c_str(), r.chars[ci].first);
                    if (ImGui::BeginPopupContextItem("cellmenu")) {
                        if (ImGui::MenuItem("Insert a character before...")) openInsert = true;
                        if (ImGui::MenuItem(ICON_TRASH " Delete")) Roster_DeleteCell(sf.text, r.chars[ci]);
                        ImGui::EndPopup();
                    }
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload("rostercell")) dropFrom = *(const int *)p->Data, dropTo = ci < 0 ? (int)r.chars.size() : ci;
                    ImGui::EndDragDropTarget();
                }
                DrawCell(draw, a, cell, ci >= 0 ? &r.chars[ci] : nullptr, budget);
                ImU32 col = ci >= 0 && ci == sel ? ImGui::GetColorU32(Ink(kYellow)) : ImGui::GetColorU32(ImGuiCol_Text, hovered ? 0.8f : 0.16f);
                draw->AddRect(a, ImVec2(a.x + cell, a.y + cell), col, 0, ci == sel ? 2.0f : 1.0f);
                ImGui::PopID();
            };
            ImVec2 origin = ImGui::GetCursorScreenPos();
            for (int i = 0; i < rows * cols; i++) {
                ImVec2 a(origin.x + (i % cols) * (cell + gap), origin.y + (i / cols) * (cell + gap));
                if (grid[i] == -2)
                    draw->AddRect(a, ImVec2(a.x + cell, a.y + cell), ImGui::GetColorU32(ImGuiCol_Text, 0.08f)); // skipped by the motif
                else
                    slotOf(grid[i], a, i);
            }
            ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + rows * (cell + gap)));
            ImGui::Dummy(ImVec2(1, 1));
            // Past the grid (still added), excluded
            std::vector<int> excluded;
            for (int ci = 0; ci < (int)r.chars.size(); ci++)
                if (r.chars[ci].exclude) excluded.push_back(ci);
            for (int pass = 0; pass < 2; pass++) {
                const std::vector<int> &list = pass ? excluded : past;
                if (list.empty()) continue;
                ImGui::TextDisabled(pass ? "Excluded (exclude = 1, in no cell):" : rows ? "Past the grid's %d cells:" : "Cells:", rows * cols);
                ImVec2 o = ImGui::GetCursorScreenPos();
                int perRow = std::max(1, (int)((ImGui::GetContentRegionAvail().x + gap) / (cell + gap)));
                for (size_t k = 0; k < list.size(); k++)
                    slotOf(list[k], ImVec2(o.x + (k % perRow) * (cell + gap), o.y + (k / perRow) * (cell + gap)), -1);
                ImGui::SetCursorScreenPos(ImVec2(o.x, o.y + ((list.size() + perRow - 1) / perRow) * (cell + gap)));
                ImGui::Dummy(ImVec2(1, 1));
            }
            // A free cell at the end takes new characters and cells dropped there
            if (ImGui::Button(ICON_PLUS " Add a character...")) ImGui::OpenPopup("addchar");
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload *p = ImGui::AcceptDragDropPayload("rostercell")) dropFrom = *(const int *)p->Data, dropTo = (int)r.chars.size();
                ImGui::EndDragDropTarget();
            }
            ImGui::SetItemTooltip("Adds a character after the last one (drop a cell here to move it to the end).");
            std::string picked;
            if (PickName("addchar", false, picked) && r.charsLine) {
                Roster_InsertLine(sf.text, r.charsEnd, picked);
                sel = (int)r.chars.size();
            }
            if (openInsert) ImGui::OpenPopup("pickchar");
            if (PickName("pickchar", false, picked) && sel >= 0) Roster_InsertLine(sf.text, r.chars[sel].first, picked);
            if (dropFrom >= 0 && dropFrom != dropTo) {
                // It takes the place of the cell it was dropped on
                int line = dropTo >= (int)r.chars.size() ? r.charsEnd
                         : dropFrom < dropTo ? (dropTo + 1 < (int)r.chars.size() ? r.chars[dropTo + 1].first : r.charsEnd)
                                             : r.chars[dropTo].first;
                if (Roster_MoveCell(sf.text, r.chars[dropFrom], line)) sel = std::min(dropTo, (int)r.chars.size() - 1);
            }
            ImGui::EndChild();
            ImGui::SameLine();

            // Right: the cell selected
            ImGui::BeginChild("cell");
            if (sel < 0 || sf.text != before) {
                ImGui::TextDisabled("Pick a cell. Drag cells to move them; right click for more.");
            } else {
                RosterCell c = r.chars[sel];
                ImGui::Text("Line %d%s", c.first, c.slot ? "  (a slot: several characters in one cell)" : "");
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_TRASH " Delete")) Roster_DeleteCell(sf.text, c);
                // The line as text
                static std::string edit;
                static int editFor = -1;
                static std::string editOf;
                if (editFor != sel || editOf != c.Text()) edit = editOf = c.Text(), editFor = sel;
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::InputText("##line", &edit, ImGuiInputTextFlags_EnterReturnsTrue) && edit != c.Text() && !edit.empty())
                    Roster_SetCell(sf.text, c, edit);
                ImGui::SetItemTooltip("The cell's line, as in select.def (Enter applies it).");
                for (size_t k = 0; k < c.entries.size() && sf.text == before; k++) {
                    RosterEntry &e = c.entries[k];
                    ImGui::PushID((int)k);
                    std::string lower = LowerOf(e.name);
                    bool keyword = lower == "randomselect" || lower == "skipslot" || lower == "emptyslot";
                    DefInfo &d = InfoOf(e.name, false, budget);
                    ImGui::SeparatorText(keyword ? e.name.c_str() : d.name.c_str());
                    if (!keyword) {
                        ImGui::TextDisabled("%s", d.def.empty() ? "not found under chars/" : d.def.c_str());
                        if (!d.def.empty()) {
                            ImGui::SameLine();
                            if (ImGui::SmallButton(ICON_FOLDER_OPEN " Open")) g_codeFile = d.def, g_codeLine = g_codeScrollTo = 1;
                        }
                        if (EntryFields(e, false)) Roster_SetCell(sf.text, c, c.Text());
                    }
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Stages")) {
            if (r.stagesLine == 0) ImGui::TextDisabled("select.def has no [ExtraStages] section.");
            static int ssel = -1;
            if (ssel >= (int)r.stages.size()) ssel = -1;
            ImGui::BeginChild("stagelist", ImVec2(em * 18, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
            for (int i = 0; i < (int)r.stages.size(); i++) {
                const RosterCell &c = r.stages[i];
                std::string label = c.slot ? c.Text() : InfoOf(c.entries[0].name, true, budget).name + "  " + c.entries[0].name;
                if (ImGui::Selectable((label + "##" + std::to_string(i)).c_str(), ssel == i)) ssel = i;
            }
            std::string picked;
            if (ImGui::Button(ICON_PLUS " Add a stage...")) ImGui::OpenPopup("addstage");
            if (PickName("addstage", true, picked) && r.stagesLine) {
                Roster_InsertLine(sf.text, r.stagesEnd, picked);
                ssel = (int)r.stages.size();
            }
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild("stage");
            if (ssel < 0) {
                ImGui::TextDisabled("Pick a stage. These are the stages of the stage list besides the characters' own.");
            } else {
                RosterCell c = r.stages[ssel];
                ImGui::Text("Line %d", c.first);
                ImGui::SameLine();
                ImGui::BeginDisabled(ssel == 0);
                if (ImGui::SmallButton("Up") && Roster_MoveCell(sf.text, c, r.stages[ssel - 1].first)) ssel--;
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(ssel + 1 >= (int)r.stages.size());
                if (ImGui::SmallButton("Down") &&
                    Roster_MoveCell(sf.text, c, ssel + 2 < (int)r.stages.size() ? r.stages[ssel + 2].first : r.stagesEnd))
                    ssel++;
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::SmallButton(ICON_TRASH " Delete")) Roster_DeleteCell(sf.text, c);
                if (sf.text == before && !c.slot) {
                    RosterEntry &e = c.entries[0];
                    ImGui::TextDisabled("%s", e.name.c_str());
                    if (EntryFields(e, true)) Roster_SetCell(sf.text, c, c.Text());
                }
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    if (sf.text != before) RosterEdited(sf);
    ImGui::End();
}

} // namespace app
