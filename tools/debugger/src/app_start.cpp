// The start page: what the user came to do, not a list of tools. Each choice is a workspace, with
// only the windows of that task.
#include "app_internal.h"

#include <cfloat>

#include "imgui_internal.h"

namespace app {

namespace {

bool g_showAtStart = true; // saved with the settings
bool g_asked;              // shown from the next frame
bool g_started;            // its first frame has passed
int g_createAsked = -1;    // what to make, once the workspace it goes with is entered
int g_tour = -1;           // the step of the first steps shown; -1: none
bool g_tourDone;           // saved: seen to the end, or skipped
int g_tourPick;            // the player picks when its step began

struct Task {
    const char *icon, *title, *text, *workspace;
};

const Task kTasks[] = {
    {ICON_LAB, "Test a character", "Play it, stop it, go tick by tick and see what each move does.", "Test"},
    {ICON_ANIM, "Edit animations and sprites", "Boxes, timing, sprites, sounds and commands, with the game showing them.",
     "Character"},
    {ICON_STAGE, "Edit a stage", "Move its backgrounds, camera and limits on the game itself.", "Stage"},
    {ICON_DISPLAY, "Edit the screenpack", "Lifebars, menus, the select screen, storyboards and fonts.", "Screenpack"},
};

void GoTo(const char *workspace) {
    for (int n = 0; WorkspaceName(n); n++)
        if (strcmp(WorkspaceName(n), workspace) == 0) AskWorkspace(n);
}

// A big button: an icon, a title and what it is for. True when clicked.
bool Card(const Task &t, ImVec2 size) {
    ImGui::PushID(t.title);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("card", size);
    bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
    ImGui::PopID();
    const ImGuiStyle &st = ImGui::GetStyle();
    float em = ImGui::GetFontSize(), pad = em, rounding = em * 0.4f;
    ImDrawList *draw = ImGui::GetWindowDrawList();
    ImVec2 end(p.x + size.x, p.y + size.y);
    draw->AddRectFilled(p, end, ImGui::GetColorU32(hot ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg), rounding);
    if (hot) draw->AddRect(p, end, ImGui::GetColorU32(ImGuiCol_ButtonActive), rounding, std::max(1.0f, em / 10));
    ImGui::PushFont(nullptr, st.FontSizeBase * 2.2f);
    float iconSize = ImGui::GetFontSize();
    draw->AddText(ImVec2(p.x + pad, p.y + pad), ImGui::GetColorU32(ImGuiCol_CheckMark), t.icon);
    ImGui::PopFont();
    float x = p.x + pad, y = p.y + pad + iconSize + em * 0.7f;
    draw->AddText(ImVec2(x, y), ImGui::GetColorU32(ImGuiCol_Text), t.title);
    draw->AddText(nullptr, 0, ImVec2(x, y + em * 1.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), t.text, nullptr,
                  size.x - pad * 2);
    return clicked;
}

} // namespace

// The first steps, in a corner: not in the way, and each goes on when the user has done it
static void TourFrame() {
    if (g_tour < 0) return;
    static const struct {
        const char *title, *text;
    } steps[] = {
        {"Start a match", "Press Launch at the top left, or F5. The game runs inside this window."},
        {"", ""}, // what the workspace picks
        {"Change something", "Drag it on the game, or use the button on that line and change a value. Each editor says whether the game has it yet."},
        {"That's all", "Ctrl+S saves. The Select choices on the game pick fighters, stages and screens the same way, and the bar at "
                       "the left changes what you are working on."},
    };
    // What the Select choice on the game picks, and the count that moves when it does
    const char *pickTitle = g_stagePick ? "Pick a background" : g_screenPick ? "Pick a lifebar or a text" : "Pick a fighter";
    int picks = g_selPicked + g_screenPicked + g_stageBg;
    if (g_tour == 0 && Connected()) {
        g_tour = 1;
        g_tourPick = picks;
    }
    if (g_tour == 1 && picks != g_tourPick) g_tour = 2;
    if (g_tour == 2 && !UnsavedList().empty()) g_tour = 3;

    const ImGuiStyle &st = ImGui::GetStyle();
    ImGuiViewport *vp = ImGui::GetMainViewport();
    float em = ImGui::GetFontSize(), margin = em;
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - margin, vp->WorkPos.y + vp->WorkSize.y - margin),
                            ImGuiCond_Always, ImVec2(1, 1));
    ImGui::SetNextWindowSize(ImVec2(em * 26, 0));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
    ImGui::Begin("##tour", nullptr, flags);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    bool last = g_tour == IM_ARRAYSIZE(steps) - 1;
    if (!last) ImGui::TextDisabled("Step %d of %d", g_tour + 1, IM_ARRAYSIZE(steps) - 1);
    ImGui::PushFont(nullptr, st.FontSizeBase * 1.2f);
    ImGui::TextUnformatted(g_tour == 1 ? pickTitle : steps[g_tour].title);
    ImGui::PopFont();
    ImGui::TextWrapped("%s", g_tour == 1 ? "Click it on the game. The line above the picture says what is selected and where to edit it."
                                         : steps[g_tour].text);
    if (ImGui::SmallButton(last ? ICON_CHECK " Done" : "Skip")) {
        g_tour = -1;
        g_tourDone = true;
        ImGui::MarkIniSettingsDirty();
    }
    ImGui::End();
}

void EmptyNote(const char *text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void AskStartPage() { g_asked = true; }

void AskTour() { g_tour = 0; }

void StartPageFrame() {
    if (!g_started) {
        g_started = true;
        // Scripted runs (--launch) go straight to the game
        g_asked = g_showAtStart && !g_autoLaunch;
        if (!g_asked && !g_autoLaunch && !g_tourDone) g_tour = 0;
    }
    TourFrame();
    if (g_createAsked >= 0) {
        CreateAsk(g_createAsked);
        g_createAsked = -1;
    }
    if (g_asked) {
        g_asked = false;
        ImGui::OpenPopup("##start");
    }
    ImGuiViewport *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f),
                            ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(em * 40, 0));
    if (!ImGui::BeginPopupModal("##start", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) return;
    const ImGuiStyle &st = ImGui::GetStyle();
    ImGui::PushFont(nullptr, st.FontSizeBase * 1.5f);
    ImGui::TextUnformatted("What do you want to do?");
    ImGui::PopFont();
    ImGui::TextDisabled("You can change it at any time, on the bar at the left.");
    ImGui::Dummy(ImVec2(0, em * 0.3f));

    bool close = ImGui::IsKeyPressed(ImGuiKey_Escape);
    bool tour = true; // the first steps are about seeing a change live, not for who came to start something
    float w = (ImGui::GetContentRegionAvail().x - st.ItemSpacing.x) * 0.5f;
    for (int i = 0; i < IM_ARRAYSIZE(kTasks); i++) {
        if (i % 2) ImGui::SameLine();
        if (Card(kTasks[i], ImVec2(w, em * 9))) {
            GoTo(kTasks[i].workspace);
            close = true;
        }
    }
    ImGui::Dummy(ImVec2(0, em * 0.3f));
    ImGui::Separator();
    if (ImGui::Button(ICON_PERSON " New character")) {
        GoTo("Character");
        g_createAsked = kNewCharacter;
        close = true, tour = false;
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_STAGE " New stage")) {
        GoTo("Stage");
        g_createAsked = kNewStage;
        close = true, tour = false;
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_CODE " Write code")) {
        GoTo("Code");
        close = true, tour = false;
    }
    const char *label = "Show at startup";
    ImGui::SameLine(ImGui::GetWindowWidth() - st.WindowPadding.x - ImGui::GetFrameHeight() - st.ItemInnerSpacing.x -
                    ImGui::CalcTextSize(label).x);
    if (ImGui::Checkbox(label, &g_showAtStart)) ImGui::MarkIniSettingsDirty();
    ImGui::SetItemTooltip("It stays in Help, as Start page.");
    if (close) {
        ImGui::CloseCurrentPopup();
        if (!g_tourDone && tour) g_tour = 0;
    }
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Help: the keys, and the words the files and the interface use
// ---------------------------------------------------------------------------

static int g_helpAsked = -1, g_help;

void HelpAsk(int what) { g_helpAsked = what; }

void HelpMenu() {
    if (ImGui::MenuItem(ICON_PLAY " Start page")) AskStartPage();
    if (ImGui::MenuItem(ICON_INFO " Getting started")) AskTour();
    ImGui::Separator();
    if (ImGui::MenuItem(ICON_KEYBOARD " Keyboard shortcuts...")) HelpAsk(0);
    if (ImGui::MenuItem(ICON_LIST " Words used here...")) HelpAsk(1);
}

void HelpDialog() {
    static const char *titles[] = {ICON_KEYBOARD " Keyboard shortcuts", ICON_LIST " Words used here"};
    if (g_helpAsked >= 0) {
        g_help = g_helpAsked;
        g_helpAsked = -1;
        ImGui::OpenPopup(titles[g_help]);
    }
    static const struct {
        const char *a, *b;
    } keys[] = {
        {"", "Everywhere"},
        {"F5", "Launch the game; then pause and continue it"},
        {"Shift+F5", "Stop the game"},
        {"Ctrl+Shift+F5", "Start the game again"},
        {"F10 / F11", "One tick, ten ticks"},
        {"Ctrl+S", "Save everything changed"},
        {"Ctrl+Z / Ctrl+Y", "Undo, do again, in the editor being used; on the Game window, in the editor of what is dragged there"},
        {"Ctrl+1 to Ctrl+5", "Change workspace"},
        {"Ctrl+P", "Go to a file"},
        {"Ctrl+Shift+P", "Any command"},
        {"Ctrl+Shift+F", "Find in files"},
        {"Esc", "Leave a dialog or a field"},
        {"", "In Code"},
        {"Ctrl+G / Ctrl+Shift+O", "Go to a line, to a section"},
        {"Ctrl+F / Ctrl+H", "Find, replace"},
        {"F3 / Shift+F3", "The next match, the one before"},
        {"F12 or Ctrl+click", "Go to what a number or a name stands for"},
        {"Shift+F12", "The places changing to that state"},
        {"Ctrl+Space", "Completion"},
        {"Ctrl+Wheel / Ctrl+0", "Bigger or smaller text, back to 100%"},
        {"Ctrl+W", "Close the tab"},
    }, words[] = {
        {"Character / fighter", "The files of chars/<name>/. P1 and P2 are the two sides of the match"},
        {"Action", "An animation, numbered, in the AIR file: [Begin Action n]"},
        {"Element", "One picture of an action, shown for some ticks"},
        {"Tick", "1/60 of a second: the game's unit of time"},
        {"Sprite", "A picture in the SFF file, named by a group and a number"},
        {"Axis", "The point of a sprite placed on the character's position"},
        {"Hit box (Clsn1)", "Where an element hits"},
        {"Hurt box (Clsn2)", "Where an element can be hit"},
        {"State / statedef", "What a character is doing, numbered, in its CNS or ZSS file"},
        {"Controller", "One thing a state does, under its triggers"},
        {"Trigger", "A condition of a controller"},
        {"Stage layer / BG", "One background of a stage, a [BG ...] section of its def"},
        {"BGCtrl", "A timed change to backgrounds"},
        {"Screenpack / motif", "The game's menus, select screen and screens: its system.def"},
        {"Fight screen", "The lifebars and all that a match shows: fight.def"},
        {"Storyboard", "A def of scenes: intros, endings, credits"},
        {"def", "A text file saying which files make something up, and how"},
        {"SFF, SND, AIR, CMD, CNS", "Sprites, sounds, animations, commands, constants and states"},
    };
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    float em = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(em * 40, 0));
    ImGui::SetNextWindowBgAlpha(1.0f);
    bool open = true;
    if (!ImGui::BeginPopupModal(titles[g_help], &open, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) ImGui::CloseCurrentPopup();
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, std::max(vp->WorkSize.y - em * 7, 0.0f)));
    ImGui::BeginChild("rows", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY);
    if (ImGui::BeginTable("help", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, em * 11);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        const auto *rows = g_help ? words : keys;
        int n = g_help ? IM_ARRAYSIZE(words) : IM_ARRAYSIZE(keys);
        for (int i = 0; i < n; i++) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (!*rows[i].a) {
                ImGui::TextDisabled("%s", rows[i].b);
                continue;
            }
            ImGui::TextUnformatted(rows[i].a);
            ImGui::TableNextColumn();
            ImGui::TextWrapped("%s", rows[i].b);
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::EndPopup();
}

void StartSettingsLine(const std::string &key, const std::string &value) {
    if (key == "StartPage") g_showAtStart = value != "0";
    else if (key == "TourDone") g_tourDone = value == "1";
}

void StartSettingsWrite(ImGuiTextBuffer *buf) {
    buf->appendf("StartPage=%d\n", g_showAtStart);
    buf->appendf("TourDone=%d\n", g_tourDone);
}

} // namespace app
