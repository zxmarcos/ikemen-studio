// The windows and where they go: the menu bar (Windows opens and closes each one, Layout puts
// them in one of the ready layouts, Theme colours them), each window's place in the dock, and a
// window opened again joining the others of its place.
#include "app_internal.h"

#include "imgui_internal.h"

namespace app {

namespace {

// The dock's nodes. A window always goes to its place's node: in the layouts, and when it is
// opened again and the node it was in is gone.
enum Place { kSession, kLeft, kControl, kGame, kRight, kBottom, kCode };

struct ToolWindow {
    const char *name, *group;
    Place place;
    bool open = true;
    bool dock = false; // opened from the menu: joins its place's windows
};

// In the order of their tabs
ToolWindow g_windows[] = {
    {"Session", "Game", kSession},
    {"Control", "Game", kControl},
    {"Game", "Game", kGame},
    {"Players", "Debugging", kLeft},
    {"Breakpoints", "Debugging", kLeft},
    {"Watch", "Debugging", kLeft},
    {"Inspector", "Debugging", kRight},
    {"Trace", "Debugging", kBottom},
    {"Frame data", "Debugging", kBottom},
    {"Console", "Debugging", kBottom},
    {"Performance", "Debugging", kBottom},
    {"Code", "Debugging", kCode},
    {"Lab", "Character", kBottom},
    {"Analysis", "Character", kBottom},
    {"Animation", "Character", kRight},
    {"Sprites", "Character", kRight},
    {"Sounds", "Character", kRight},
    {"Commands", "Character", kRight},
    {"Stage", "Stage and screens", kRight},
    {"Fight screen", "Stage and screens", kRight},
    {"Motif", "Stage and screens", kRight},
    {"Storyboard", "Stage and screens", kRight},
    {"Roster", "Stage and screens", kLeft},
    {"Fonts", "Stage and screens", kLeft},
    {"Create", "Create", kRight},
};

struct Layout {
    const char *name, *help;
    float left;              // the left column's share of the width
    std::vector<const char *> open; // the others are closed
};

const Layout kLayouts[] = {
    {"Debugging", "A fight: the players, their states and code, the character's files.", 0.19f,
     {"Session", "Control", "Game", "Players", "Breakpoints", "Watch", "Inspector", "Trace", "Frame data",
      "Console", "Code", "Lab", "Analysis", "Performance", "Animation", "Sprites", "Sounds", "Commands"}},
    {"Stage and screens", "The stage, the fight screen and the screenpack's screens.", 0.24f,
     {"Session", "Control", "Game", "Console", "Performance", "Code", "Stage", "Fight screen", "Motif", "Storyboard", "Roster",
      "Fonts"}},
};

// Saved with the settings; an older layout (or none) is replaced by the first one.
const int kLayoutVersion = 2;
int g_layoutVersion;
int g_layoutAsked = -1; // a layout picked in the menu, built at the start of the next frame
std::vector<const char *> g_front; // windows whose tab goes to the front: after a layout, each place's first
int g_frontUntil;                  // asked again on each frame until then (a tab appearing takes the front)

ToolWindow *FindTool(const char *name) {
    for (ToolWindow &t : g_windows)
        if (strcmp(t.name, name) == 0) return &t;
    return nullptr;
}

// Its tab to the front for the next frames (instead of another of its place asked before)
void Front(const ToolWindow &t) {
    if (ImGui::GetFrameCount() > g_frontUntil) g_front.clear();
    g_front.erase(std::remove_if(g_front.begin(), g_front.end(), [&](const char *n) { return FindTool(n)->place == t.place; }),
                  g_front.end());
    g_front.push_back(t.name);
    g_frontUntil = ImGui::GetFrameCount() + 10;
}

void BuildLayout(ImGuiID dock, const Layout &l) {
    ImGui::DockBuilderRemoveNode(dock);
    ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
    ImVec2 size = ImGui::GetMainViewport()->WorkSize;
    ImGui::DockBuilderSetNodeSize(dock, size);
    // Left: Session above the lists. The rest: Control and Game, the files' windows on the right,
    // then the whole width below for the tick by tick windows and Code.
    ImGuiID left, rest, upper, bottom, right, center, code, node[kCode + 1];
    ImGui::DockBuilderSplitNode(dock, ImGuiDir_Left, l.left, &left, &rest);
    ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.45f, &node[kSession], &node[kLeft]);
    ImGui::DockBuilderSplitNode(rest, ImGuiDir_Down, 0.36f, &bottom, &upper);
    ImGui::DockBuilderSplitNode(upper, ImGuiDir_Right, 0.4f, &right, &center);
    ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Right, 0.55f, &code, &node[kBottom]);
    // Control: its tab and three rows
    const ImGuiStyle &st = ImGui::GetStyle();
    float controlH = ImGui::GetFrameHeight() * 4 + st.ItemSpacing.y * 2 + st.WindowPadding.y * 2;
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Up, controlH / (size.y * 0.64f), &node[kControl], &node[kGame]);
    node[kRight] = right;
    node[kCode] = code;
    short order = 0;
    bool placed[kCode + 1] = {};
    for (ToolWindow &t : g_windows) {
        t.open = std::find_if(l.open.begin(), l.open.end(),
                              [&](const char *n) { return strcmp(n, t.name) == 0; }) != l.open.end();
        t.dock = false;
        if (t.open && !placed[t.place]) {
            placed[t.place] = true;
            Front(t);
        }
        ImGui::DockBuilderDockWindow(t.name, node[t.place]);
        // Tabs in the table's order (else in the order of the windows' first frame)
        if (ImGuiWindow *w = ImGui::FindWindowByName(t.name)) w->DockOrder = order;
        else if (ImGuiWindowSettings *s = ImGui::FindWindowSettingsByID(ImHashStr(t.name))) s->DockOrder = order;
        order++;
    }
    ImGui::DockBuilderFinish(dock);
}

// The node of an open window of that place, of Code's, or of Game's: 0 without one.
ImGuiID PlaceNode(const ToolWindow &self) {
    auto nodeOf = [](const char *name) -> ImGuiID {
        ImGuiWindow *w = ImGui::FindWindowByName(name);
        return w && w->DockId && ImGui::DockBuilderGetNode(w->DockId) ? w->DockId : 0;
    };
    for (const ToolWindow &t : g_windows)
        if (&t != &self && t.open && t.place == self.place)
            if (ImGuiID id = nodeOf(t.name)) return id;
    if (ImGuiID id = nodeOf("Code")) return id;
    return nodeOf("Game");
}

} // namespace

bool BeginTool(const char *name, ImGuiWindowFlags flags) {
    ToolWindow *t = FindTool(name);
    IM_ASSERT(t);
    if (!t->open) return false;
    // Opened again: back to its node if the layout still has it, or with its place's windows. A
    // window the saved layout does not know joins them too.
    ImGuiWindow *w = ImGui::FindWindowByName(name);
    bool lost = t->dock && !(w && w->DockId && ImGui::DockBuilderGetNode(w->DockId));
    if (lost || !w)
        if (ImGuiID id = PlaceNode(*t)) ImGui::SetNextWindowDockID(id, lost ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
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

void LayoutFrame() {
    ThemeFrame();
    ImGuiID dock = ImGui::GetID("IkemenDebuggerDock");
    if (ImGui::BeginMainMenuBar()) {
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
        if (ImGui::BeginMenu("Layout")) {
            for (int n = 0; n < IM_ARRAYSIZE(kLayouts); n++) {
                if (ImGui::MenuItem(kLayouts[n].name)) g_layoutAsked = n;
                ImGui::SetItemTooltip("%s", kLayouts[n].help);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Theme")) {
            ThemeMenu();
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    // No layout saved, or an older one: the first. Picked in the menu: that one.
    if (g_firstRun || g_layoutVersion < kLayoutVersion) g_layoutAsked = std::max(g_layoutAsked, 0);
    if (g_layoutAsked >= 0) {
        BuildLayout(dock, kLayouts[g_layoutAsked]);
        g_layoutAsked = -1;
        g_firstRun = false;
        g_layoutVersion = kLayoutVersion;
        ImGui::MarkIniSettingsDirty();
    }
    ImGui::DockSpaceOverViewport(dock);
}

void LayoutSettingsLine(const std::string &key, const std::string &value) {
    if (key == "Layout") g_layoutVersion = atoi(value.c_str());
    else if (key == "Closed")
        if (ToolWindow *t = FindTool(value.c_str())) t->open = false;
}

void LayoutSettingsWrite(ImGuiTextBuffer *buf) {
    buf->appendf("Layout=%d\n", g_layoutVersion);
    for (const ToolWindow &t : g_windows)
        if (!t.open) buf->appendf("Closed=%s\n", t.name);
}

} // namespace app
