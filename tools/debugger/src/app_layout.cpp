// The windows and where they go: the menu bar (Windows opens and closes each one, Create, Theme,
// and the workspaces), the toolbar above the dock space, and the workspaces: each task's own
// arrangement of the windows (Debug, Code, Character, Stage, Screenpack), kept as it is changed
// and restored on demand.
#include "app_internal.h"

#include <cstdlib>
#include <map>

#include "imgui_internal.h"

namespace app {

namespace {

struct ToolWindow {
    const char *name, *group;
    const char *nearby; // opened where it is not: next to the first of these that is open ("a|b")
    bool advanced = false;     // shown with View > Advanced tools, or when something asks for it
    const char *panel = nullptr; // a tab of this window: it is drawn in it, only while it is the tab shown
    bool open = false;
    bool asked = false;        // hidden, but asked for
    ToolWindow *tab = nullptr; // a panel: the tab chosen
    ToolWindow *shown = nullptr; // a panel: the tab drawn last
    float scroll = 0;          // a tab: where it was left
    bool dock = false; // opened from the menu: goes next to its neighbours
    bool floating = false;
    ImVec4 rect;       // floating: its place, applied on its next frame
};

// In the order of the Windows menu. Most are tabs of a panel, so that a workspace shows a few areas
// and each has one job; a panel's tabs are its rows with its name in `panel`.
ToolWindow g_windows[] = {
    {"Game", "Game", ""},
    {"Project", "Files", "Fighters|Testing"},
    {"Explorer", "", "", false, "Project"},
    {"Outline", "", "", false, "Project"},
    {"Code", "Files", "Output|Testing"},
    {"Output", "Files", "Testing|Code"},
    {"Problems", "", "", false, "Output"},
    {"Search", "", "", false, "Output"},
    {"Console", "", "", true, "Output"},
    {"Fighters", "Testing", "Project|Inspector"},
    {"Players", "", "", false, "Fighters"},
    {"Watch", "", "", true, "Fighters"},
    {"Breakpoints", "", "", true, "Fighters"},
    {"Inspector", "Testing", "Stage editor|Assets|Fighters"},
    {"Testing", "Testing", "Output|Code"},
    {"Lab", "", "", false, "Testing"},
    {"Frame data", "", "", false, "Testing"},
    {"Trace", "", "", true, "Testing"},
    {"Analysis", "", "", true, "Testing"},
    {"Performance", "", "", true, "Testing"},
    {"Animation", "Making", "Code|Inspector|Stage editor"},
    {"Scenes", "Making", "Animation|Code|Output"},
    {"Assets", "Making", "Inspector|Stage editor|Screenpack|Animation"},
    {"Sprites", "", "", false, "Assets"},
    {"Sounds", "", "", false, "Assets"},
    {"Commands", "", "", false, "Assets"},
    {"Stage editor", "Making", "Screenpack|Inspector"},
    {"Stage", "", "", false, "Stage editor"},
    {"Model", "", "", true, "Stage editor"},
    {"Screenpack", "Making", "Stage editor|Inspector"},
    {"Fight screen", "", "", false, "Screenpack"},
    {"Motif", "", "", false, "Screenpack"},
    {"Storyboard", "", "", false, "Screenpack"},
    {"Roster", "", "", false, "Screenpack"},
    {"Fonts", "", "", false, "Screenpack"},
};
// An arrangement: X<ratio>(left)(right) or Y<ratio>(top)(bottom) splits a node, the ratio being
// the first one's share; L:a,b*,c is a node's tabs (* the one in front, else the first). The
// windows in it are open, the others closed. A kept one adds |name@x,y,w,h;... for the floating
// windows.
enum CodeFile { kFollow, kStageDef, kFightDef }; // what Code shows in a workspace

struct Workspace {
    const char *name, *icon, *help, *layout;
    CodeFile code;
    std::string saved; // as the user left it ("" until then)
};

Workspace g_spaces[] = {
    {"Test", ICON_LAB, "A fight tick by tick: the fighters, their states, what ran, the code.",
     "X0.14(L:Fighters,Project)(X0.68(Y0.68(L:Game)(L:Testing,Output,Code))(L:Inspector))", kFollow},
    {"Code", ICON_CODE, "Writing a character: the files and the outline, the editor in the middle, the game beside it.",
     "X0.16(L:Project)(X0.68(Y0.72(L:Code)(L:Output))(L:Game))", kFollow},
    {"Character", ICON_PERSON, "A character's animations, sprites, sounds and commands.",
     "X0.15(L:Project,Fighters)(X0.62(L:Animation,Code,Output)(Y0.4(L:Game)(L:Assets)))", kFollow},
    {"Stage", ICON_STAGE, "The stage on the game, its def's sections and fields, its animations, sprites, checks and 3D model.",
     "X0.15(L:Project)(X0.62(Y0.52(L:Game)(L:Animation,Code,Output))(Y0.5(L:Stage editor)(L:Assets)))", kStageDef},
    {"Screenpack", ICON_DISPLAY, "The fight screen, the motif's screens, storyboards, their animations and sprites, the roster and fonts.",
     "X0.15(L:Project)(X0.62(Y0.52(L:Game)(L:Animation,Scenes,Code,Output))(Y0.5(L:Screenpack)(L:Assets)))", kFightDef},
};
// Saved with the settings; an older layout (or none) is replaced by the workspace's own.
const int kLayoutVersion = 8;
int g_layoutVersion;
bool g_advanced;          // the advanced tools are shown
int g_space;              // the workspace shown
int g_spaceAsked = -1;    // picked: shown from the start of the next frame
bool g_restoreAsked;
bool g_entered;           // the first frame has set the windows up
ImGuiID g_dock;
std::vector<const char *> g_front; // windows whose tab goes to the front
int g_frontUntil;                  // asked again on each frame until then (a tab appearing takes the front)
std::map<std::string, int> g_tabOrder; // each tab's place in the arrangement built last

// The icon of a window, in the Windows menu
const char *WindowIcon(const char *name) {
    static const std::map<std::string, const char *> icons = {
        {"Project", ICON_EXPLORER}, {"Output", ICON_CONSOLE}, {"Fighters", ICON_PLAYERS}, {"Testing", ICON_LAB},
        {"Assets", ICON_IMAGE}, {"Stage editor", ICON_STAGE}, {"Screenpack", ICON_DISPLAY},
        {"Game", ICON_GAME}, {"Explorer", ICON_EXPLORER}, {"Code", ICON_CODE}, {"Outline", ICON_OUTLINE},
        {"Search", ICON_SEARCH}, {"Problems", ICON_WARNING}, {"Players", ICON_PLAYERS}, {"Breakpoints", ICON_BREAKPOINT},
        {"Watch", ICON_WATCH}, {"Inspector", ICON_INSPECTOR}, {"Trace", ICON_TRACE}, {"Frame data", ICON_TABLE},
        {"Console", ICON_CONSOLE}, {"Performance", ICON_PERF}, {"Lab", ICON_LAB}, {"Analysis", ICON_ANALYSIS},
        {"Animation", ICON_ANIM}, {"Sprites", ICON_IMAGE}, {"Sounds", ICON_SOUND}, {"Commands", ICON_KEYBOARD},
        {"Stage", ICON_STAGE}, {"Model", ICON_CUBE}, {"Fight screen", ICON_FIGHT}, {"Motif", ICON_PALETTE},
        {"Storyboard", ICON_CLAPPER}, {"Scenes", ICON_CLAPPER}, {"Roster", ICON_ROSTER}, {"Fonts", ICON_FONT}};
    auto it = icons.find(name);
    return it == icons.end() ? "" : it->second;
}

// A character's: not on the workspace of a stage or a screen
bool CharacterOnly(const ToolWindow &t) { return strcmp(t.name, "Commands") == 0 && g_spaces[g_space].code != kFollow; }

bool Hidden(const ToolWindow &t) { return ((t.advanced && !g_advanced) || CharacterOnly(t)) && !t.asked; }

ToolWindow *FindTool(const char *name) {
    for (ToolWindow &t : g_windows)
        if (strcmp(t.name, name) == 0) return &t;
    return nullptr;
}

bool InPanel(const ToolWindow &t, const ToolWindow &panel) { return t.panel && strcmp(t.panel, panel.name) == 0; }

bool IsPanel(const ToolWindow &t) {
    for (const ToolWindow &o : g_windows)
        if (InPanel(o, t)) return true;
    return false;
}

int TabCount(const ToolWindow &panel) {
    int n = 0;
    for (const ToolWindow &o : g_windows) n += InPanel(o, panel) && !Hidden(o);
    return n;
}

// The tab a panel shows: the one chosen, else its first
ToolWindow *TabOf(ToolWindow &panel) {
    if (panel.tab && !Hidden(*panel.tab)) return panel.tab;
    for (ToolWindow &o : g_windows)
        if (InPanel(o, panel) && !Hidden(o)) return panel.tab = &o;
    return nullptr;
}

// Its tab to the front for the next frames (instead of another of its node asked before)
void Front(const ToolWindow &t) {
    if (ImGui::GetFrameCount() > g_frontUntil) g_front.clear();
    ImGuiWindow *w = ImGui::FindWindowByName(t.name);
    g_front.erase(std::remove_if(g_front.begin(), g_front.end(),
                                 [&](const char *n) {
                                     ImGuiWindow *o = ImGui::FindWindowByName(n);
                                     return strcmp(n, t.name) == 0 || (w && o && w->DockId && o->DockId == w->DockId);
                                 }),
                  g_front.end());
    g_front.push_back(t.name);
    g_frontUntil = ImGui::GetFrameCount() + 10;
}

// ---------------------------------------------------------------------------
// Arrangements: built in the dock space from their text, and the dock space as text
// ---------------------------------------------------------------------------

void BuildNode(ImGuiID id, const char *&p, short &order) {
    if (*p == 'X' || *p == 'Y') {
        bool x = *p++ == 'X';
        char *end;
        float r = strtof(p, &end);
        p = end;
        ImGuiID first, second;
        ImGui::DockBuilderSplitNode(id, x ? ImGuiDir_Left : ImGuiDir_Up, std::clamp(r, 0.05f, 0.95f), &first, &second);
        for (ImGuiID child : {first, second}) {
            if (*p == '(') p++;
            BuildNode(child, p, order);
            if (*p == ')') p++;
        }
        return;
    }
    if (p[0] == 'L' && p[1] == ':') p += 2;
    const char *front = nullptr;
    bool picked = false;
    while (*p && *p != ')') {
        const char *start = p;
        while (*p && *p != ',' && *p != ')') p++;
        std::string name(start, p);
        if (*p == ',') p++;
        bool sel = !name.empty() && name.back() == '*';
        if (sel) name.pop_back();
        ToolWindow *t = FindTool(name.c_str());
        if (!t) continue;
        t->open = true;
        ImGui::DockBuilderDockWindow(t->name, id);
        // Tabs in this order (else in the order of the windows' first frame)
        if (ImGuiWindow *w = ImGui::FindWindowByName(t->name)) w->DockOrder = order;
        else if (ImGuiWindowSettings *s = ImGui::FindWindowSettingsByID(ImHashStr(t->name))) s->DockOrder = order;
        g_tabOrder[t->name] = order++;
        if (!front || (sel && !picked)) front = t->name, picked = sel;
    }
    if (front) Front(*FindTool(front));
}

std::string NodeText(const ImGuiDockNode *n) {
    if (!n) return "L:";
    if (n->IsSplitNode()) {
        int axis = n->SplitAxis;
        float r = n->ChildNodes[0]->Size[axis] / std::max(1.0f, n->Size[axis]);
        char head[16];
        snprintf(head, sizeof(head), "%c%.3f", axis == ImGuiAxis_X ? 'X' : 'Y', r);
        return head + ("(" + NodeText(n->ChildNodes[0]) + ")(" + NodeText(n->ChildNodes[1]) + ")");
    }
    std::vector<ImGuiWindow *> windows;
    if (n->TabBar)
        for (const ImGuiTabItem &tab : n->TabBar->Tabs)
            if (tab.Window) windows.push_back(tab.Window);
    if (windows.empty())
        for (ImGuiWindow *w : n->Windows) windows.push_back(w);
    std::string s = "L:";
    bool first = true;
    for (ImGuiWindow *w : windows) {
        const ToolWindow *t = FindTool(w->Name);
        if (!t || !t->open) continue;
        s += (first ? "" : ",") + std::string(t->name) + (w->TabId == n->SelectedTabId ? "*" : "");
        first = false;
    }
    return s;
}

// The workspace as it is now: its dock space, then its floating windows
std::string Capture() {
    std::string s = NodeText(ImGui::DockBuilderGetNode(g_dock)) + "|";
    for (const ToolWindow &t : g_windows) {
        ImGuiWindow *w = ImGui::FindWindowByName(t.name);
        if (!t.open || !w) continue;
        bool docked = w->DockNode && ImGui::DockNodeGetRootNode(w->DockNode)->ID == g_dock;
        if (docked) continue;
        char rect[96];
        snprintf(rect, sizeof(rect), "@%.0f,%.0f,%.0f,%.0f;", w->Pos.x, w->Pos.y, w->Size.x, w->Size.y);
        s += t.name + std::string(rect);
    }
    return s;
}

// The windows open in an arrangement, without building it (ImGui's own settings have it)
void OpenFrom(const std::string &text) {
    for (ToolWindow &t : g_windows) {
        size_t at = text.find(t.name);
        // A whole name, between the arrangement's separators
        bool found = false;
        while (at != std::string::npos && !found) {
            char before = at ? text[at - 1] : ':', after = text[at + strlen(t.name)];
            found = strchr(":,|;", before) && strchr(",)*|@", after);
            at = text.find(t.name, at + 1);
        }
        t.open = found;
    }
}

void Enter(const std::string &text, ImVec2 size) {
    for (ToolWindow &t : g_windows) t.open = t.dock = t.floating = false;
    g_front.clear();
    g_tabOrder.clear();
    ImGui::DockBuilderRemoveNode(g_dock);
    ImGui::DockBuilderAddNode(g_dock, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(g_dock, size);
    size_t bar = text.find('|');
    std::string tree = text.substr(0, bar);
    const char *p = tree.c_str();
    short order = 0;
    BuildNode(g_dock, p, order);
    ImGui::DockBuilderFinish(g_dock);
    if (bar == std::string::npos) return;
    // The floating ones, where they were
    std::string rest = text.substr(bar + 1);
    for (size_t at = 0; at < rest.size();) {
        size_t end = rest.find(';', at), mark = rest.find('@', at);
        if (end == std::string::npos) end = rest.size();
        if (mark < end)
            if (ToolWindow *t = FindTool(rest.substr(at, mark - at).c_str())) {
                t->open = t->floating = true;
                sscanf(rest.c_str() + mark + 1, "%f,%f,%f,%f", &t->rect.x, &t->rect.y, &t->rect.z, &t->rect.w);
            }
        at = end + 1;
    }
}

// The node of the first open neighbour of a window, of Console's, Code's or Game's: 0 without one.
ImGuiID NearNode(const ToolWindow &self) {
    std::string list = std::string(self.nearby) + "|Output|Code|Game";
    for (size_t at = 0; at <= list.size();) {
        size_t end = list.find('|', at);
        if (end == std::string::npos) end = list.size();
        std::string name = list.substr(at, end - at);
        at = end + 1;
        const ToolWindow *t = FindTool(name.c_str());
        if (!t || t == &self || !t->open) continue;
        ImGuiWindow *w = ImGui::FindWindowByName(t->name);
        if (w && w->DockId && ImGui::DockBuilderGetNode(w->DockId)) return w->DockId;
    }
    return 0;
}

// Code shows the workspace's file (the stage's def, the fight screen's) once the game has told it,
// or follows the state
bool g_codeAsked = true;

// A click on the game selects what the workspace is about
void PickFor(const Workspace &w) { SetPickMode(w.code == kStageDef ? kPickStage : w.code == kFightDef ? kPickScreen : kPickFighters); }

void CodeFor(const Workspace &w) {
    std::string file = w.code == kStageDef ? ProjectDef(kProjectStage) : w.code == kFightDef ? FightScreenDef() : "";
    if (w.code != kFollow && file.empty()) return;
    CodeShow(file);
    g_codeAsked = false;
}

void RestoreMenuItem() {
    char label[64];
    snprintf(label, sizeof(label), ICON_REVERT " Restore %s's arrangement", g_spaces[g_space].name);
    if (ImGui::MenuItem(label)) g_restoreAsked = true;
    ImGui::SetItemTooltip("Its windows where they were at first; what was changed in it is forgotten.");
}

} // namespace

bool BeginTool(const char *name, ImGuiWindowFlags flags) {
    ToolWindow *t = FindTool(name);
    IM_ASSERT(t);
    if (t->panel) {
        // Appended to its panel, which PanelFrame has begun
        ToolWindow *panel = FindTool(t->panel);
        if (!panel->open || TabOf(*panel) != t) return false;
        if (ImGui::Begin(panel->name, &panel->open)) return true;
        ImGui::End();
        return false;
    }
    if (!t->open) return false;
    ImGuiWindow *w = ImGui::FindWindowByName(name);
    if (t->floating) {
        // Floating in this workspace, where it was
        ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2(t->rect.x, t->rect.y), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(t->rect.z, t->rect.w), ImGuiCond_Always);
        t->floating = false;
    } else {
        // Opened again: back to its node if the workspace still has it, or next to its neighbours.
        // A window the saved arrangement does not know joins them too.
        bool lost = t->dock && !(w && w->DockId && ImGui::DockBuilderGetNode(w->DockId));
        if (lost || !w)
            if (ImGuiID id = NearNode(*t)) ImGui::SetNextWindowDockID(id, lost ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
    }
    t->dock = false;
    // Its tab to the front once its node has a tab bar
    if (ImGui::GetFrameCount() > g_frontUntil) g_front.clear();
    for (const char *f : g_front)
        if (strcmp(f, name) == 0 && w && w->DockNode && w->DockNode->TabBar)
            w->DockNode->TabBar->NextSelectedTabId = w->TabId;
    // Appearing does not take the focus, which would bring the last window begun to the front
    if (ImGui::Begin(name, &t->open, flags | ImGuiWindowFlags_NoFocusOnAppearing)) return true;
    ImGui::End();
    return false;
}

void ShowWindow(const char *name) {
    ToolWindow *t = FindTool(name);
    if (!t) return;
    t->asked = t->advanced || CharacterOnly(*t);
    if (t->panel) {
        ToolWindow *panel = FindTool(t->panel);
        panel->tab = t;
        t = panel;
    }
    if (!t->open) t->open = t->dock = true;
    Front(*t);
}

bool AdvancedTools() { return g_advanced; }

void SetAdvancedTools(bool on) {
    g_advanced = on;
    for (ToolWindow &t : g_windows) t.asked = false;
    ImGui::MarkIniSettingsDirty();
}

const char *ToolName(int n) { return n >= 0 && n < IM_ARRAYSIZE(g_windows) ? g_windows[n].name : nullptr; }
const char *WorkspaceName(int n) { return n >= 0 && n < IM_ARRAYSIZE(g_spaces) ? g_spaces[n].name : nullptr; }

void AskWorkspace(int n) {
    if (n < 0) g_restoreAsked = true;
    else g_spaceAsked = n;
}

ProjectKind CurrentProject() {
    CodeFile code = g_spaces[g_spaceAsked >= 0 ? g_spaceAsked : g_space].code;
    return code == kStageDef ? kProjectStage : code == kFightDef ? kProjectScreenpack : kProjectCharacter;
}

void AskProjectWorkspace(ProjectKind kind) {
    if (kind != kProjectCharacter) g_codeAsked = true; // Code shows its def
    if (CurrentProject() == kind) return;
    for (int n = 0; n < IM_ARRAYSIZE(g_spaces); n++) {
        const Workspace &w = g_spaces[n];
        bool fits = kind == kProjectStage ? w.code == kStageDef : kind == kProjectScreenpack ? w.code == kFightDef : strcmp(w.name, "Character") == 0;
        if (fits) AskWorkspace(n);
    }
}

// The bar at the bottom: what the game is doing (its colour too, as an IDE's while debugging) and
// the player selected; at the right, the file shown in Code and its cursor, the changes not
// saved, the workspace.
void StatusBar() {
    bool running = g_bridge.Running(), live = Connected();
    ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg);
    if (live) {
        ImVec4 ink = !g_breakText.empty() ? Ink(kRed) : g_paused ? Ink(kYellow) : Ink(kGreen);
        float k = 0.22f;
        bg = ImVec4(bg.x + (ink.x - bg.x) * k, bg.y + (ink.y - bg.y) * k, bg.z + (ink.z - bg.z) * k, 1);
    }
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, bg);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoNavFocus;
    if (ImGui::BeginViewportSideBar("##status", ImGui::GetMainViewport(), ImGuiDir_Down, ImGui::GetFrameHeight(), flags) &&
        ImGui::BeginMenuBar()) {
        float em = ImGui::GetFontSize();
        if (!running) {
            ImGui::TextDisabled("Not running");
        } else if (!live) {
            ImGui::TextColored(Ink(kYellow), "Waiting for the mod...");
        } else {
            ImGui::TextUnformatted(g_steps > 0 ? "Stepping" : g_paused ? "Paused" : "Running");
            ImGui::SameLine(0, em);
            ImGui::Text("tick %d", g_tick);
            ImGui::SameLine(0, em);
            ImGui::TextDisabled("round %d", g_roundNo);
            ImGui::SetItemTooltip("RoundState %d", g_roundState);
            if (const Entity *e = FindEntity(g_selId)) {
                ImGui::SameLine(0, em * 2);
                ImGui::Text("%s  state %d", Who(e->pn, e->helper, e->id).c_str(), e->st);
            }
        }
        // At the right
        struct Item {
            std::string text;
            ImVec4 color;
            std::function<void()> click;
            const char *tip;
        };
        std::vector<Item> right;
        int problems[3];
        ProblemCounts(problems);
        auto showProblems = [] { ShowWindow("Problems"); };
        const char *problemsTip = "Lint, the stage's checks, Apply, the engine's warnings (Problems)";
        if (problems[0]) right.push_back({std::to_string(problems[0]) + (problems[0] == 1 ? " error" : " errors"), Ink(kRed), showProblems, problemsTip});
        if (problems[1])
            right.push_back({std::to_string(problems[1]) + (problems[1] == 1 ? " warning" : " warnings"), Ink(kYellow), showProblems, problemsTip});
        if (!problems[0] && !problems[1])
            right.push_back({"No problems", ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), showProblems, problemsTip});
        std::string file;
        int line, col;
        if (CodeCursor(&file, &line, &col)) {
            char where[64];
            snprintf(where, sizeof(where), "  Ln %d, Col %d", line, col);
            right.push_back({file.substr(file.find_last_of("\\/") + 1) + where, ImGui::GetStyleColorVec4(ImGuiCol_Text),
                             [] { ShowWindow("Code"); }, "The file shown in Code"});
        }
        if (float zoom = dbg::Editor_Zoom(); zoom != 1.0f)
            right.push_back({std::to_string((int)(zoom * 100 + 0.5f)) + "%", ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled),
                             [] { dbg::Editor_SetZoom(1.0f); },
                             "The code editor's text size (Ctrl+Wheel over it); a click, or Ctrl+0 in it: 100%"});
        size_t dirty = UnsavedList().size();
        if (g_applying) right.push_back({"applying...", ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), nullptr, nullptr});
        else if (dirty)
            right.push_back({std::to_string(dirty) + " unsaved", Ink(kYellow), [] { UnsavedAsk(false); }, "Which files, to save them all"});
        right.push_back({g_spaces[g_space].name, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), nullptr,
                         "The workspace (Ctrl+1..5)"});
        float total = 0;
        for (const Item &it : right) total += ImGui::CalcTextSize(it.text.c_str()).x + em * 1.5f;
        float at = ImGui::GetWindowWidth() - total - ImGui::GetStyle().WindowPadding.x;
        // How the last thing done went, for a while, in the room between them; a click: all of them
        if (ImGui::GetTime() - g_noteAt < (g_noteError ? 15 : 6)) {
            ImGui::SameLine(0, em * 2);
            float room = at - ImGui::GetCursorPosX() - em;
            if (room > em * 6) {
                ImGui::PushStyleColor(ImGuiCol_Text, g_noteError ? Ink(kRed) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                if (ImGui::Selectable((g_note + "##note").c_str(), false, ImGuiSelectableFlags_NoAutoClosePopups,
                                      ImVec2(std::min(room, ImGui::CalcTextSize(g_note.c_str()).x), 0)))
                    ShowWindow("Console");
                ImGui::PopStyleColor();
                ImGui::SetItemTooltip("%s", g_note.c_str());
            }
        }
        if (at > ImGui::GetCursorPosX() + em) ImGui::SameLine(at);
        else ImGui::SameLine(0, em * 2);
        for (const Item &it : right) {
            ImGui::PushStyleColor(ImGuiCol_Text, it.color);
            ImVec2 size(ImGui::CalcTextSize(it.text.c_str()).x, 0);
            if (ImGui::Selectable(it.text.c_str(), false, ImGuiSelectableFlags_NoAutoClosePopups, size) && it.click)
                it.click();
            ImGui::PopStyleColor();
            if (it.tip) ImGui::SetItemTooltip("%s", it.tip);
            ImGui::SameLine(0, em * 1.5f);
        }
        ImGui::EndMenuBar();
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

// A panel: the window itself and, on its menu bar (which does not scroll), its tabs. The tab shown
// appends its content from BeginTool.
static void PanelFrame(ToolWindow &panel) {
    ToolWindow *shown = TabOf(panel);
    bool bar = TabCount(panel) > 1;
    if (!BeginTool(panel.name, bar ? ImGuiWindowFlags_MenuBar : 0)) return;
    // Each tab keeps its own scroll
    if (shown != panel.shown) {
        if (panel.shown) panel.shown->scroll = ImGui::GetScrollY();
        if (shown) ImGui::SetScrollY(shown->scroll);
        panel.shown = shown;
    }
    if (bar && ImGui::BeginMenuBar()) {
        // Without room for every name, only the one shown keeps its name
        float spacing = ImGui::GetStyle().ItemSpacing.x, total = 0;
        for (const ToolWindow &t : g_windows)
            if (InPanel(t, panel) && !Hidden(t))
                total += ImGui::CalcTextSize((std::string(WindowIcon(t.name)) + " " + t.name).c_str()).x + spacing;
        bool iconsOnly = total > ImGui::GetContentRegionAvail().x;
        for (ToolWindow &t : g_windows) {
            if (!InPanel(t, panel) || Hidden(t)) continue;
            std::string label = std::string(WindowIcon(t.name)) + (iconsOnly && &t != shown ? "" : std::string(" ") + t.name);
            if (ImGui::Selectable(label.c_str(), &t == shown, 0, ImVec2(ImGui::CalcTextSize(label.c_str()).x, 0)) && &t != shown) {
                panel.tab = &t;
                for (ToolWindow &o : g_windows) o.asked = false;
                ImGui::MarkIniSettingsDirty();
            }
            if (iconsOnly) ImGui::SetItemTooltip("%s", t.name);
        }
        ImGui::EndMenuBar();
    }
    ImGui::End();
}

// The workspaces, down the left side: an icon and its name
static void WorkspaceRail(float width, float height) {
    const ImGuiStyle &st = ImGui::GetStyle();
    float em = ImGui::GetFontSize(), rowH = em * 3.6f;
    ImDrawList *draw = ImGui::GetWindowDrawList();
    ImVec2 at = ImGui::GetCursorScreenPos();
    draw->AddRectFilled(at, ImVec2(at.x + width, at.y + height), ImGui::GetColorU32(ImGuiCol_MenuBarBg));
    for (int n = 0; n < IM_ARRAYSIZE(g_spaces); n++) {
        const Workspace &w = g_spaces[n];
        ImVec2 p(at.x, at.y + rowH * n);
        ImGui::SetCursorScreenPos(p);
        ImGui::PushID(n);
        if (ImGui::InvisibleButton("space", ImVec2(width, rowH))) g_spaceAsked = n;
        bool hot = ImGui::IsItemHovered();
        ImGui::SetItemTooltip("%s (Ctrl+%d)", w.help, n + 1);
        ImGui::PopID();
        ImVec2 end(p.x + width, p.y + rowH);
        if (n == g_space) {
            draw->AddRectFilled(p, end, ImGui::GetColorU32(ImGuiCol_Header));
            draw->AddRectFilled(p, ImVec2(p.x + em * 0.2f, end.y), ImGui::GetColorU32(ImGuiCol_CheckMark));
        } else if (hot) {
            draw->AddRectFilled(p, end, ImGui::GetColorU32(ImGuiCol_HeaderHovered));
        }
        ImU32 ink = ImGui::GetColorU32(n == g_space || hot ? ImGuiCol_Text : ImGuiCol_TextDisabled);
        ImGui::PushFont(nullptr, st.FontSizeBase * 1.5f);
        ImVec2 size = ImGui::CalcTextSize(w.icon);
        draw->AddText(ImVec2(p.x + (width - size.x) * 0.5f, p.y + em * 0.55f), ink, w.icon);
        ImGui::PopFont();
        ImGui::PushFont(nullptr, st.FontSizeBase * 0.9f);
        size = ImGui::CalcTextSize(w.name);
        draw->AddText(ImVec2(p.x + (width - size.x) * 0.5f, p.y + em * 2.2f), ink, w.name);
        ImGui::PopFont();
    }
    ImGui::SetCursorScreenPos(ImVec2(at.x + width, at.y));
}

void LayoutFrame() {
    ThemeFrame();
    ImGuiIO &io = ImGui::GetIO();
    if (io.KeyCtrl && !io.KeyShift && !io.KeyAlt && !io.WantTextInput && !ImGui::GetTopMostPopupModal())
        for (int n = 0; n < IM_ARRAYSIZE(g_spaces); n++)
            if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + n), false)) g_spaceAsked = n;
    // Saves whatever has changes, from any window (Code and Commands then apply them)
    if (io.KeyCtrl && !io.KeyShift && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_S, false) && !ImGui::GetTopMostPopupModal()) {
        if (UnsavedList().empty()) Log(LogLine::Info, "Nothing to save.");
        else SaveAll();
    }

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu(ICON_FOLDER_OPEN " File")) {
            FileMenu();
            ImGui::Separator();
            if (ImGui::BeginMenu(ICON_WAND " New")) {
                CreateMenu();
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(ICON_LIST " View")) {
            if (ImGui::BeginMenu(ICON_TABLE " Windows")) {
                const char *group = "";
                for (ToolWindow &t : g_windows) {
                    if (t.panel) continue;
                    if (strcmp(group, t.group) != 0) ImGui::SeparatorText(group = t.group);
                    std::string label = std::string(WindowIcon(t.name)) + "  " + t.name;
                    // A panel of several tabs: each of them. Else the window itself.
                    if (TabCount(t) > 1) {
                        if (!ImGui::BeginMenu(label.c_str())) continue;
                        for (ToolWindow &o : g_windows)
                            if (InPanel(o, t) && !Hidden(o) &&
                                ImGui::MenuItem((std::string(WindowIcon(o.name)) + "  " + o.name).c_str(), nullptr, t.open && t.tab == &o))
                                ShowWindow(o.name);
                        ImGui::EndMenu();
                    } else if (ImGui::MenuItem(label.c_str(), nullptr, t.open)) {
                        t.open = !t.open;
                        t.dock = t.open;
                        if (t.open) Front(t);
                    }
                }
                ImGui::EndMenu();
            }
            RestoreMenuItem();
            if (ImGui::BeginMenu(ICON_PALETTE " Theme")) {
                ThemeMenu();
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_BUG " Advanced tools", nullptr, g_advanced)) SetAdvancedTools(!g_advanced);
            ImGui::SetItemTooltip("Breakpoints, Watch, Trace, Console, Performance, Analysis and the 3D Model window.\nOff, they stay out of the way until something asks for them.");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(ICON_INFO " Help")) {
            HelpMenu();
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // The window holding the toolbar and, below it, the dock space
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##workspace", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(3);
    const ImGuiStyle &st = ImGui::GetStyle();
    ImGui::SetCursorPosY(st.FramePadding.y + st.ItemSpacing.y * 0.5f);
    ImGui::Indent(st.WindowPadding.x);
    Toolbar();
    ImGui::Unindent(st.WindowPadding.x);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + st.ItemSpacing.y * 0.5f);

    g_dock = ImGui::GetID("IkemenDebuggerDock");
    WorkspaceRail(ImGui::GetFontSize() * 6, ImGui::GetContentRegionAvail().y);
    ImVec2 size = ImGui::GetContentRegionAvail();
    if (!g_entered) {
        // No arrangement saved, or an older one: the workspace's own. Else ImGui's settings have
        // it, and the open windows are those in it.
        if (g_layoutVersion < kLayoutVersion)
            for (Workspace &old : g_spaces) old.saved.clear();
        const Workspace &w = g_spaces[g_space];
        if (g_firstRun || w.saved.empty()) Enter(w.layout, size);
        else OpenFrom(w.saved);
        PickFor(w);
        g_entered = true;
        g_firstRun = false;
        g_layoutVersion = kLayoutVersion;
        ImGui::MarkIniSettingsDirty();
    }
    if (g_spaceAsked >= 0 && g_spaceAsked != g_space) {
        g_spaces[g_space].saved = Capture();
        g_space = g_spaceAsked;
        const Workspace &w = g_spaces[g_space];
        Enter(w.saved.empty() ? w.layout : w.saved, size);
        PickFor(w);
        g_codeAsked = true;
        ImGui::MarkIniSettingsDirty();
    }
    g_spaceAsked = -1;
    if (g_restoreAsked) {
        g_spaces[g_space].saved.clear();
        Enter(g_spaces[g_space].layout, size);
        g_restoreAsked = false;
        ImGui::MarkIniSettingsDirty();
    }
    // Tabs in the arrangement's order: the windows already shown join a new node before the others
    // (DockOrder follows the tabs as they are, frame by frame)
    if (ImGui::GetFrameCount() <= g_frontUntil)
        for (const ToolWindow &t : g_windows) {
            ImGuiWindow *w = ImGui::FindWindowByName(t.name);
            ImGuiTabBar *bar = w && w->DockNode ? w->DockNode->TabBar : nullptr;
            if (!bar || bar->Tabs.Size < 2) continue;
            auto order = [](const ImGuiTabItem &a) {
                auto it = a.Window ? g_tabOrder.find(a.Window->Name) : g_tabOrder.end();
                return it == g_tabOrder.end() ? 1000 : it->second;
            };
            std::stable_sort(bar->Tabs.begin(), bar->Tabs.end(),
                             [&](const ImGuiTabItem &a, const ImGuiTabItem &b) { return order(a) < order(b); });
        }
    if (g_codeAsked) CodeFor(g_spaces[g_space]);
    ImGui::DockSpace(g_dock, ImVec2(0, 0));
    ImGui::End();
    for (ToolWindow &t : g_windows)
        if (t.open && IsPanel(t)) PanelFrame(t);
}

void LayoutSettingsLine(const std::string &key, const std::string &value) {
    if (key == "Layout") {
        g_layoutVersion = atoi(value.c_str());
    } else if (key == "AdvancedTools") {
        g_advanced = value == "1";
    } else if (key == "PanelTab") {
        size_t bar = value.find('|');
        ToolWindow *panel = FindTool(value.substr(0, bar).c_str());
        ToolWindow *tab = bar == std::string::npos ? nullptr : FindTool(value.c_str() + bar + 1);
        if (panel && tab && InPanel(*tab, *panel)) panel->tab = tab;
    } else if (key == "Workspace") {
        for (int n = 0; n < IM_ARRAYSIZE(g_spaces); n++)
            if (value == g_spaces[n].name) g_space = n;
    } else if (key == "WorkspaceLayout") {
        size_t bar = value.find('|');
        for (Workspace &w : g_spaces)
            if (value.compare(0, bar, w.name) == 0 && bar == strlen(w.name)) w.saved = value.substr(bar + 1);
    }
}

void LayoutSettingsWrite(ImGuiTextBuffer *buf) {
    if (g_entered) g_spaces[g_space].saved = Capture();
    buf->appendf("Layout=%d\n", g_layoutVersion);
    buf->appendf("AdvancedTools=%d\n", g_advanced);
    for (const ToolWindow &t : g_windows)
        if (t.tab) buf->appendf("PanelTab=%s|%s\n", t.name, t.tab->name);
    buf->appendf("Workspace=%s\n", g_spaces[g_space].name);
    for (const Workspace &w : g_spaces)
        if (!w.saved.empty()) buf->appendf("WorkspaceLayout=%s|%s\n", w.name, w.saved.c_str());
}

} // namespace app
