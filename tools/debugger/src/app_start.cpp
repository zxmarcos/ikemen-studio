// The start page: what the user came to do, not a list of tools. Each choice is a workspace, with
// only the windows of that task.
#include "app_internal.h"

#include "imgui_internal.h"

namespace app {

namespace {

bool g_showAtStart = true; // saved with the settings
bool g_asked;              // shown from the next frame
bool g_started;            // its first frame has passed
int g_createAsked = -1;    // Create's tab, once the workspace it goes with is entered

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

void AskStartPage() { g_asked = true; }

void StartPageFrame() {
    if (!g_started) {
        g_started = true;
        // Scripted runs (--launch) go straight to the game
        g_asked = g_showAtStart && !g_autoLaunch;
    }
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
        g_createAsked = 1;
        close = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_STAGE " New stage")) {
        GoTo("Stage");
        g_createAsked = 2;
        close = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_CODE " Write code")) {
        GoTo("Code");
        close = true;
    }
    const char *label = "Show at startup";
    ImGui::SameLine(ImGui::GetWindowWidth() - st.WindowPadding.x - ImGui::GetFrameHeight() - st.ItemInnerSpacing.x -
                    ImGui::CalcTextSize(label).x);
    if (ImGui::Checkbox(label, &g_showAtStart)) ImGui::MarkIniSettingsDirty();
    ImGui::SetItemTooltip("It stays in File, as Start page.");
    if (close) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void StartSettingsLine(const std::string &key, const std::string &value) {
    if (key == "StartPage") g_showAtStart = value != "0";
}

void StartSettingsWrite(ImGuiTextBuffer *buf) { buf->appendf("StartPage=%d\n", g_showAtStart); }

} // namespace app
