// Go to: the game put on a screen and kept there, to edit what only shows for a moment (the mod's
// ikdbg.goTo). The game is started again without -p1 -p2 when it was a quick match, which has no
// menus and exits after its match; moments of a match are then watched for on the frame geometry
// until their element shows.
#include "app_internal.h"

namespace app {

namespace {

struct GoTarget {
    const char *group, *label, *key;
    const char *prefix; // a moment: the [Round] elements it waits for ("a|b"), "" what the mod holds itself
    int ticks;          // and held that long after it shows: past its fade, before it goes
};

const GoTarget kTargets[] = {
    {"Menus", "Title", "title", nullptr},
    {"Menus", "Options", "options", nullptr},
    {"Menus", "Select screen", "select", nullptr},
    {"Menus", "Select screen with teams", "teamselect", nullptr},
    {"Menus", "Versus screen", "vs", nullptr},
    {"Menus", "Hiscore", "hiscore", nullptr},
    {"Storyboards", "Logo", "logo", nullptr},
    {"Storyboards", "Intro", "intro", nullptr},
    {"Storyboards", "Game over", "gameover", nullptr},
    {"Storyboards", "Credits", "credits", nullptr},
    {"Storyboards", "Default ending", "ending", nullptr},
    {"After a match", "Victory screen", "victory", nullptr},
    {"After a match", "Results (won)", "results", nullptr},
    {"After a match", "Results (lost)", "lose", nullptr},
    {"After a match", "Continue screen", "continue", nullptr},
    {"In a match", "Round call", "round", "round", 15},
    {"In a match", "Fight!", "fight", "fight.", 15},
    {"In a match", "KO", "ko", "ko.", 40},
    {"In a match", "Double KO", "dko", "dko.", 40},
    {"In a match", "Perfect", "perfect", "p1.perfect.", 30},
    {"In a match", "Time over", "timeover", "to.", 40},
    {"In a match", "Winner", "win", "ai.win|ai.lose|p1.win|p2.win", 30},
    {"In a match", "Draw", "draw", "draw.", 40},
    {"In a match", "Pause menu", "pause", ""},
};

const char *const kTeams[] = {"single", "simul", "turns", "tag"};

std::string g_held, g_going;  // the screen the game stays on, the one it is going to (keys)
bool g_menusRun;              // this run started without -p1 -p2: it has menus to go back to
int g_team[2], g_teamNum[2] = {2, 2};
const GoTarget *g_watch;      // a moment being waited for

const char *LabelOf(const std::string &key) {
    if (key.compare(0, 5, "file:") == 0) return key.c_str() + 5;
    for (const GoTarget &t : kTargets)
        if (key == t.key) return t.label;
    return key.c_str();
}

void GoTo(const GoTarget &t) {
    int n1 = g_team[0] ? g_teamNum[0] : 1, n2 = g_team[1] ? g_teamNum[1] : 1;
    std::string stage = SettingsStageDef(), p1 = g_settings.p1.empty() ? "kfm" : g_settings.p1, p2 = g_settings.p2.empty() ? "kfm" : g_settings.p2;
    std::string text = std::string(t.key) + "|" + kTeams[g_team[0]] + "|" + std::to_string(n1) + "|" + kTeams[g_team[1]] + "|" +
                       std::to_string(n2) + "|" + p1 + "|" + p2 + "|" + stage;
    g_held.clear();
    g_going = t.key;
    g_watch = t.prefix && *t.prefix ? &t : nullptr;
    if (Connected() && g_menusRun) {
        SendRaw("ikdbg.goTo(" + LuaQuote(text) + ")");
        return;
    }
    // The mod reads it when it loads: the logo and the intro are skipped on the way
    SetEnvironmentVariableA("IKDBG_GOTO", text.c_str());
    Launch(true);
    SetEnvironmentVariableA("IKDBG_GOTO", nullptr);
}

} // namespace

void GoToStory(const std::string &def) {
    static std::string key;
    key = "file:" + def;
    GoTo(GoTarget{"", "", key.c_str(), nullptr, 0});
}

void GoToLaunched(bool menus) {
    g_menusRun = menus;
    if (!menus) g_held.clear(), g_going.clear(), g_watch = nullptr;
}

void OnGoTo(const Json &j) {
    std::string held = j["held"].str();
    if (!held.empty() && held != g_held) Log(LogLine::Info, std::string("Held on ") + LabelOf(held));
    g_held = held;
    g_going = j["goal"].str();
}

void GoToFrame() {
    // While the mod still has the goal, a match on screen is the one being left, maybe showing that element
    if (!g_watch || !g_going.empty()) return;
    bool shown = false;
    for (std::string left = g_watch->prefix; !left.empty() && !shown;) { // "a|b": either
        size_t bar = left.find('|');
        shown = ScreenItemShown("round", left.substr(0, bar).c_str());
        left = bar == std::string::npos ? "" : left.substr(bar + 1);
    }
    if (!shown) return;
    // In the game's ticks: this program sees few frames when the machine is busy. In Turns the next
    // fighter comes in soon after a KO
    bool turns = g_team[0] == 2 || g_team[1] == 2;
    int ticks = turns ? std::min(g_watch->ticks, 10) : g_watch->ticks;
    SendRaw(std::string("ikdbg.holdAfter('") + g_watch->key + "', " + std::to_string(ticks) + ")");
    g_watch = nullptr;
}

void GoToButton() {
    if (ImGui::Button(ICON_TRACE " Go to " ICON_CARET)) ImGui::OpenPopup("goto");
    ImGui::SetItemTooltip("Puts the game on a screen and keeps it there, to edit what only shows for a moment.");
    float em = ImGui::GetFontSize();
    ImGui::SetNextWindowSizeConstraints(ImVec2(em * 19, 0), ImVec2(FLT_MAX, em * 34));
    if (ImGui::BeginPopup("goto")) {
        ImGui::TextDisabled("%s vs %s, on %s", g_settings.p1.c_str(), g_settings.p2.c_str(), g_settings.stage.c_str());
        static const char *const names[] = {"Single", "Simul", "Turns", "Tag"};
        for (int side = 0; side < 2; side++) {
            ImGui::PushID(side);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Team %d", side + 1);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(em * 6);
            ImGui::Combo("##team", &g_team[side], names, 4);
            if (g_team[side]) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(em * 4);
                ImGui::SliderInt("##num", &g_teamNum[side], 2, 4);
            }
            ImGui::PopID();
        }
        ImGui::SetItemTooltip("The match's teams, for the fight screen's Simul, Turns and Tag sections");
        const char *group = nullptr;
        for (const GoTarget &t : kTargets) {
            if (!group || strcmp(group, t.group) != 0) ImGui::SeparatorText(group = t.group);
            if (ImGui::Selectable(t.label, g_held == t.key)) {
                GoTo(t);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}

void GoToLine() {
    if (!g_going.empty() || g_watch) {
        ImGui::SameLine();
        ImGui::TextColored(Ink(kYellow), "Going to %s...", g_watch ? g_watch->label : LabelOf(g_going));
    } else if (!g_held.empty()) {
        std::string held = std::string("Held on ") + LabelOf(g_held);
        SameLineFits(ImGui::CalcTextSize(held.c_str()).x);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(Ink(kYellow), "%s", held.c_str());
        SameLineFits(ImGui::CalcTextSize(ICON_PLAY " Let it go on").x + ImGui::GetStyle().FramePadding.x * 2);
        if (ImGui::Button(ICON_PLAY " Let it go on")) SendRaw("ikdbg.letGo()");
        ImGui::SetItemTooltip("The game goes on from this screen");
    }
}

} // namespace app
