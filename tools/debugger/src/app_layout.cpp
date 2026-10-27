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
    bool open = false;
    bool dock = false; // opened from the menu: goes next to its neighbours
    bool floating = false;
    ImVec4 rect;       // floating: its place, applied on its next frame
};

// In the order of the Windows menu
ToolWindow g_windows[] = {
    {"Game", "Game", ""},
    {"Explorer", "Files", "Players|Analysis|Roster"},
    {"Code", "Files", "Console|Trace"},
    {"Outline", "Files", "Explorer|Players"},
    {"Search", "Files", "Analysis|Console|Trace"},
    {"Players", "Debugging", "Watch|Breakpoints|Roster|Analysis"},
    {"Breakpoints", "Debugging", "Watch|Players"},
    {"Watch", "Debugging", "Breakpoints|Players"},
    {"Inspector", "Debugging", "Stage|Sprites|Analysis"},
    {"Trace", "Debugging", "Frame data|Console|Lab"},
    {"Frame data", "Debugging", "Trace|Console|Lab"},
    {"Console", "Debugging", "Trace|Performance"},
    {"Performance", "Debugging", "Console|Trace|Frame data"},
    {"Lab", "Character", "Trace|Frame data|Console"},
    {"Analysis", "Character", "Players|Inspector|Console"},
    {"Animation", "Character", "Inspector|Stage"},
    {"Sprites", "Character", "Sounds|Commands|Inspector"},
    {"Sounds", "Character", "Sprites|Commands|Inspector"},
    {"Commands", "Character", "Sprites|Sounds|Inspector"},
    {"Stage", "Stage and screens", "Fight screen|Motif|Inspector"},
    {"Fight screen", "Stage and screens", "Motif|Storyboard|Stage|Inspector"},
    {"Motif", "Stage and screens", "Fight screen|Storyboard|Stage|Inspector"},
    {"Storyboard", "Stage and screens", "Motif|Fight screen|Stage|Inspector"},
    {"Roster", "Stage and screens", "Fonts|Players"},
    {"Fonts", "Stage and screens", "Roster|Players"},
    {"Create", "Create", "Sprites|Commands|Stage|Inspector"},
};

// An arrangement: X<ratio>(left)(right) or Y<ratio>(top)(bottom) splits a node, the ratio being
// the first one's share; L:a,b*,c is a node's tabs (* the one in front, else the first). The
// windows in it are open, the others closed. A kept one adds |name@x,y,w,h;... for the floating
// windows.
enum CodeFile { kFollow, kStageDef, kFightDef }; // what Code shows in a workspace

struct Workspace {
    const char *name, *help, *layout;
    CodeFile code;
    std::string saved; // as the user left it ("" until then)
};

Workspace g_spaces[] = {
    {"Debug", "A fight tick by tick: the players, their states, what ran, the code.",
     "X0.19(Y0.5(L:Players,Explorer,Outline)(L:Watch,Breakpoints))"
     "(Y0.62(X0.66(L:Game)(L:Inspector))(X0.45(L:Trace,Frame data,Lab,Console,Performance,Search)(L:Code)))", kFollow},
    {"Code", "Writing a character: the files and the outline, the editor in the middle, the Lint, the state graph "
             "and the search below it.",
     "X0.16(Y0.6(L:Explorer)(L:Outline))(X0.68(Y0.68(L:Code)(L:Analysis,Search,Console,Trace))(Y0.45(L:Game)(L:Inspector)))",
     kFollow},
    {"Character", "A character's animations, sprites, sounds and commands; new moves and characters.",
     "X0.16(Y0.5(L:Explorer)(L:Players,Outline))"
     "(X0.62(Y0.66(L:Animation)(L:Code,Console,Search))(Y0.38(L:Game)(L:Sprites,Sounds,Commands,Create)))",
     kFollow},
    {"Stage", "The stage on the game, its def's sections and fields, its checks and 3D model.",
     "X0.15(L:Explorer)(X0.7(Y0.64(L:Game)(X0.62(L:Code)(L:Console,Performance)))(L:Stage))", kStageDef},
    {"Screenpack", "The fight screen, the motif's screens, storyboards, the roster and fonts.",
     "X0.17(Y0.4(L:Explorer)(L:Roster,Fonts))"
     "(X0.7(Y0.64(L:Game)(X0.6(L:Code)(L:Console)))(L:Fight screen,Motif,Storyboard))",
     kFightDef},
};

// Saved with the settings; an older layout (or none) is replaced by the workspace's own.
const int kLayoutVersion = 4;
int g_layoutVersion;
int g_space;              // the workspace shown
int g_spaceAsked = -1;    // picked: shown from the start of the next frame
bool g_restoreAsked;
bool g_entered;           // the first frame has set the windows up
ImGuiID g_dock;
std::vector<const char *> g_front; // windows whose tab goes to the front
int g_frontUntil;                  // asked again on each frame until then (a tab appearing takes the front)
std::map<std::string, int> g_tabOrder; // each tab's place in the arrangement built last

ToolWindow *FindTool(const char *name) {
    for (ToolWindow &t : g_windows)
        if (strcmp(t.name, name) == 0) return &t;
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
    std::string list = std::string(self.nearby) + "|Console|Code|Game";
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

void CodeFor(const Workspace &w) {
    std::string file = w.code == kStageDef ? g_stageDef : w.code == kFightDef ? FightScreenDef() : "";
    if (w.code != kFollow && file.empty()) return;
    CodeShow(file);
    g_codeAsked = false;
}

void WorkspaceMenu() {
    for (int n = 0; n < IM_ARRAYSIZE(g_spaces); n++) {
        char key[16];
        snprintf(key, sizeof(key), "Ctrl+%d", n + 1);
        if (ImGui::MenuItem(g_spaces[n].name, key, n == g_space)) g_spaceAsked = n;
        ImGui::SetItemTooltip("%s", g_spaces[n].help);
    }
    ImGui::Separator();
    char label[64];
    snprintf(label, sizeof(label), "Restore %s's arrangement", g_spaces[g_space].name);
    if (ImGui::MenuItem(label)) g_restoreAsked = true;
    ImGui::SetItemTooltip("Its windows where they were at first; what was changed in it is forgotten.");
}

} // namespace

bool BeginTool(const char *name, ImGuiWindowFlags flags) {
    ToolWindow *t = FindTool(name);
    IM_ASSERT(t);
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
    if (!t->open) t->open = t->dock = true;
    Front(*t);
}

const char *ToolName(int n) { return n >= 0 && n < IM_ARRAYSIZE(g_windows) ? g_windows[n].name : nullptr; }
const char *WorkspaceName(int n) { return n >= 0 && n < IM_ARRAYSIZE(g_spaces) ? g_spaces[n].name : nullptr; }

void AskWorkspace(int n) {
    if (n < 0) g_restoreAsked = true;
    else g_spaceAsked = n;
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
            ImGui::TextDisabled("round %d (%d)", g_roundNo, g_roundState);
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
        std::string file;
        int line, col;
        if (CodeCursor(&file, &line, &col)) {
            char where[64];
            snprintf(where, sizeof(where), "  Ln %d, Col %d", line, col);
            right.push_back({file.substr(file.find_last_of("\\/") + 1) + where, ImGui::GetStyleColorVec4(ImGuiCol_Text),
                             [] { ShowWindow("Code"); }, "The file shown in Code"});
        }
        int dirty = 0;
        for (const auto &kv : g_files) dirty += kv.second.Dirty();
        if (g_applying) right.push_back({"applying...", ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), nullptr, nullptr});
        else if (dirty)
            right.push_back({std::to_string(dirty) + " unsaved", Ink(kYellow), SaveAndApply,
                             "Saves them; the game loads the characters again (Ctrl+S in Code)"});
        right.push_back({g_spaces[g_space].name, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), nullptr,
                         "The workspace (Ctrl+1..5)"});
        float total = 0;
        for (const Item &it : right) total += ImGui::CalcTextSize(it.text.c_str()).x + em * 1.5f;
        float at = ImGui::GetWindowWidth() - total - ImGui::GetStyle().WindowPadding.x;
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

void LayoutFrame() {
    ThemeFrame();
    ImGuiIO &io = ImGui::GetIO();
    if (io.KeyCtrl && !io.KeyShift && !io.KeyAlt && !io.WantTextInput)
        for (int n = 0; n < IM_ARRAYSIZE(g_spaces); n++)
            if (ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + n), false)) g_spaceAsked = n;

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            FileMenu();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Windows")) {
            const char *group = "";
            for (ToolWindow &t : g_windows) {
                if (strcmp(group, t.group) != 0) ImGui::SeparatorText(group = t.group);
                if (ImGui::MenuItem(t.name, nullptr, t.open)) {
                    t.open = !t.open;
                    t.dock = t.open;
                    if (t.open) Front(t);
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Create")) {
            CreateMenu();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Workspace")) {
            WorkspaceMenu();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Theme")) {
            ThemeMenu();
            ImGui::EndMenu();
        }
        // The workspaces as tabs
        ImGui::SameLine(0, ImGui::GetFontSize() * 2);
        for (int n = 0; n < IM_ARRAYSIZE(g_spaces); n++) {
            const char *label = g_spaces[n].name;
            if (ImGui::Selectable(label, n == g_space, 0, ImVec2(ImGui::CalcTextSize(label).x, 0))) g_spaceAsked = n;
            ImGui::SetItemTooltip("%s (Ctrl+%d)", g_spaces[n].help, n + 1);
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
    ImVec2 size = ImGui::GetContentRegionAvail();
    if (!g_entered) {
        // No arrangement saved, or an older one: the workspace's own. Else ImGui's settings have
        // it, and the open windows are those in it.
        if (g_layoutVersion < kLayoutVersion)
            for (Workspace &old : g_spaces) old.saved.clear();
        const Workspace &w = g_spaces[g_space];
        if (g_firstRun || w.saved.empty()) Enter(w.layout, size);
        else OpenFrom(w.saved);
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
}

void LayoutSettingsLine(const std::string &key, const std::string &value) {
    if (key == "Layout") {
        g_layoutVersion = atoi(value.c_str());
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
    buf->appendf("Workspace=%s\n", g_spaces[g_space].name);
    for (const Workspace &w : g_spaces)
        if (!w.saved.empty()) buf->appendf("WorkspaceLayout=%s|%s\n", w.name, w.saved.c_str());
}

} // namespace app
