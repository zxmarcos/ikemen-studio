// The debugger's toolbar and windows: Players, Inspector, Frame data, Trace, Breakpoints, Watch,
// Console.
#include "app_internal.h"
#include "cns.h"

#include <cfloat>
#include <cstdarg>
#include <fstream>

#include "imgui_internal.h"
#include "imgui_stdlib.h"

namespace app {

// Matches saved by name (the toolbar's Matches): what Launch starts with
struct SavedMatch {
    std::string name, p1, p2, stage;
    bool ai1 = false, ai2 = true;
};
std::vector<SavedMatch> g_matches;

void MatchSettingsLine(const std::string &key, const std::string &value) {
    if (key != "Match") return;
    // name|p1|p2|stage|ai1|ai2
    std::vector<std::string> f;
    for (size_t at = 0;;) {
        size_t bar = value.find('|', at);
        f.push_back(value.substr(at, bar - at));
        if (bar == std::string::npos) break;
        at = bar + 1;
    }
    if (f.size() == 6) g_matches.push_back({f[0], f[1], f[2], f[3], f[4] == "1", f[5] == "1"});
}

void MatchSettingsWrite(ImGuiTextBuffer *buf) {
    for (const SavedMatch &m : g_matches)
        buf->appendf("Match=%s|%s|%s|%s|%d|%d\n", m.name.c_str(), m.p1.c_str(), m.p2.c_str(), m.stage.c_str(), m.ai1, m.ai2);
}

// The popup of the toolbar's Matches: a saved match picked, the one set saved
void MatchesPopup() {
    Settings &s = g_settings;
    float em = ImGui::GetFontSize();
    static std::string name;
    if (ImGui::IsWindowAppearing()) name = s.p1 + " vs " + s.p2 + (s.stage.empty() ? "" : ", " + s.stage);
    if (g_matches.empty()) ImGui::TextDisabled("No match saved yet.");
    for (size_t i = 0; i < g_matches.size(); i++) {
        const SavedMatch &m = g_matches[i];
        ImGui::PushID((int)i);
        bool current = m.p1 == s.p1 && m.p2 == s.p2 && m.stage == s.stage && m.ai1 == s.ai1 && m.ai2 == s.ai2;
        if (ImGui::Selectable(m.name.c_str(), current, ImGuiSelectableFlags_AllowOverlap, ImVec2(em * 16, 0))) {
            s.p1 = m.p1, s.p2 = m.p2, s.stage = m.stage, s.ai1 = m.ai1, s.ai2 = m.ai2;
            ImGui::MarkIniSettingsDirty();
        }
        ImGui::SetItemTooltip("P1 %s%s, P2 %s%s, stage %s", m.p1.c_str(), m.ai1 ? " (AI)" : "", m.p2.c_str(),
                              m.ai2 ? " (AI)" : "", m.stage.c_str());
        ImGui::SameLine();
        bool del = ImGui::SmallButton(ICON_XMARK);
        ImGui::SetItemTooltip("Forget it");
        ImGui::PopID();
        if (del) {
            g_matches.erase(g_matches.begin() + i);
            ImGui::MarkIniSettingsDirty();
            break;
        }
    }
    ImGui::Separator();
    ImGui::SetNextItemWidth(em * 14);
    ImGui::InputText("##name", &name);
    ImGui::SameLine();
    ImGui::BeginDisabled(name.empty() || name.find('|') != std::string::npos);
    if (ImGui::Button(ICON_SAVE " Save this match")) {
        bool replaced = false;
        for (SavedMatch &m : g_matches)
            if (m.name == name) m = {name, s.p1, s.p2, s.stage, s.ai1, s.ai2}, replaced = true;
        if (!replaced) g_matches.push_back({name, s.p1, s.p2, s.stage, s.ai1, s.ai2});
        ImGui::MarkIniSettingsDirty();
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("These players and stage, under that name (the same name: replaced)");
}

// The toolbar above the windows: the game started and stopped, the match it starts with, its
// time (pause, steps, speed) and state; the rest of the session in Settings.
void Toolbar() {
    Settings &s = g_settings;
    bool running = g_bridge.Running(), live = Connected();
    ImGuiIO &io = ImGui::GetIO();
    if (live && !io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_F5, false) && !io.KeyShift && !io.KeyCtrl)
            SendRaw(g_paused ? "ikdbg.pause(false)" : "ikdbg.pause(true)");
        if (ImGui::IsKeyPressed(ImGuiKey_F10)) SendRaw("ikdbg.step(1)");
        if (ImGui::IsKeyPressed(ImGuiKey_F11)) SendRaw("ikdbg.step(10)");
    }
    // As an IDE runs: F5 starts the game (then pauses and continues it), Shift+F5 stops it,
    // Ctrl+Shift+F5 starts it again
    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
        if (!running && !io.KeyShift) Launch();
        else if (running && io.KeyShift && io.KeyCtrl) Launch();
        else if (running && io.KeyShift) {
            g_bridge.Stop();
            Log(LogLine::Info, "Stopped the game.");
        }
    }
    float em = ImGui::GetFontSize();
    // On the same line while there is room
    auto flow = [&](float width) {
        ImGui::SameLine(0, em);
        if (ImGui::GetContentRegionAvail().x < width) ImGui::NewLine();
    };

    if (ImGui::Button(running ? ICON_RESTART " Restart" : ICON_PLAY " Launch (F5)")) Launch();
    ImGui::SetItemTooltip(running ? "Starts the game again with the match beside (Ctrl+Shift+F5)."
                                  : "Starts the game with the match beside (F5).");
    ImGui::SameLine();
    ImGui::BeginDisabled(!running);
    if (ImGui::Button(ICON_STOP " Stop")) {
        g_bridge.Stop();
        Log(LogLine::Info, "Stopped the game.");
    }
    ImGui::SetItemTooltip("Shift+F5");
    ImGui::EndDisabled();

    // The match it starts with: one line, the fields in the popup it opens
    flow(em * 18);
    auto side = [](const std::string &name, bool ai) { return (name.empty() ? std::string("?") : name) + (ai ? " (AI)" : ""); };
    std::string summary = s.p1.empty() && s.p2.empty() ? std::string("Title screen")
                                                       : side(s.p1, s.ai1) + "  vs  " + side(s.p2, s.ai2) +
                                                             (s.stage.empty() ? "" : "  on " + s.stage);
    if (ImGui::Button((std::string(ICON_GAME "  ") + summary + "  " ICON_CARET + "###match").c_str())) ImGui::OpenPopup("match");
    ImGui::SetItemTooltip("The match the game starts with (Launch, Restart): who fights, and where");
    if (ImGui::BeginPopup("match")) {
        auto player = [&](const char *label, std::string &name, bool &ai, const char *id) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine(em * 6);
            ImGui::SetNextItemWidth(em * 10);
            ImGui::InputText((std::string("##") + label).c_str(), &name);
            ImGui::SameLine();
            ImGui::Checkbox(id, &ai);
            ImGui::SetItemTooltip("The computer plays this side");
        };
        player("Player 1", s.p1, s.ai1, "AI##1");
        player("Player 2", s.p2, s.ai2, "AI##2");
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Stage");
        ImGui::SameLine(em * 6);
        ImGui::SetNextItemWidth(em * 10);
        ImGui::InputText("##stage", &s.stage);
        ImGui::TextDisabled("Leave both players empty to start at the title screen.");
        ImGui::TextDisabled("Explorer's menu on a character or a stage sets them too.");
        ImGui::Separator();
        if (ImGui::TreeNode(ICON_BOOKMARKS " Saved matches")) {
            MatchesPopup();
            ImGui::TreePop();
        }
        ImGui::EndPopup();
    }

    // The game's time
    flow(em * 26);
    ImGui::BeginDisabled(!live);
    if (ImGui::Button(g_paused ? ICON_PLAY " Continue (F5)" : ICON_PAUSE " Pause (F5)")) SendRaw(g_paused ? "ikdbg.pause(false)" : "ikdbg.pause(true)");
    ImGui::SameLine();
    if (ImGui::Button(ICON_STEP " Step (F10)")) SendRaw("ikdbg.step(1)");
    ImGui::SameLine();
    if (ImGui::Button(ICON_ELLIPSIS)) ImGui::OpenPopup("more");
    ImGui::SetItemTooltip("More: speed, ten ticks at once, save and load state, reset the round");
    if (ImGui::BeginPopup("more")) {
        static float speed = 1;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Speed");
        ImGui::SameLine();
        for (float v : {0.25f, 0.5f, 1.0f, 2.0f, 4.0f}) {
            char label[16];
            snprintf(label, sizeof(label), "%gx", v);
            ImGui::SameLine();
            if (ImGui::Selectable(label, v == speed, 0, ImVec2(em * 2.5f, 0))) {
                speed = v;
                Exec("setAccel(" + std::to_string(v) + ")");
            }
        }
        ImGui::Separator();
        if (ImGui::Button(ICON_FAST " Ten ticks (F11)")) SendRaw("ikdbg.step(10)");
        static int stepN = 60;
        ImGui::SetNextItemWidth(em * 5);
        ImGui::InputInt("##n", &stepN, 0);
        ImGui::SameLine();
        if (ImGui::Button(ICON_PLAY " Run N ticks")) SendRaw("ikdbg.step(" + std::to_string(std::max(1, stepN)) + ")");
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_SAVE_STATE " Save state")) Exec("saveState()");
        if (ImGui::MenuItem(ICON_LOAD_STATE " Load state")) Exec("loadState()");
        if (ImGui::MenuItem(ICON_RESET " Reset round")) Exec("resetRound()");
        if (ImGui::MenuItem(ICON_RESTART " Reload match")) Exec("reload()");
        ImGui::SetItemTooltip("Reads the characters, stage and fight screen from disk again: edit a file, then reload.");
        ImGui::Separator();
        if (ImGui::MenuItem("Ikemen's collision boxes")) Exec("toggleClsnDisplay()");
        if (ImGui::MenuItem("Ikemen's debug display")) Exec("toggleDebugDisplay()");
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();

    // Why it stopped (its state is on the status bar)
    if (!g_breakText.empty()) {
        flow(em * 22);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(Ink(kRed), "%s", g_breakText.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_XMARK "##break")) g_breakText.clear();
    }

    // The rest of the session
    flow(em * 6);
    if (ImGui::Button(ICON_GEAR)) ImGui::OpenPopup("session");
    ImGui::SetItemTooltip("Settings: the game's executable, extra flags, connection state");
    if (ImGui::BeginPopup("session")) {
        ImGui::SetNextItemWidth(em * 28);
        ImGui::InputTextWithHint("Ikemen GO", "Ikemen_GO.exe", &s.exe);
        ImGui::SetNextItemWidth(em * 28);
        ImGui::InputText("Extra", &s.extra);
        HelpMarker("Other Ikemen GO command line flags.");
        ImGui::Checkbox("Show the game's own window", &s.gameWindow);
        HelpMarker("With the patched Ikemen GO the game is shown in the Game window and its own window is hidden.");
        ImGui::Separator();
        if (running) {
            ImGui::Text("pid %lu", g_bridge.Pid());
            ImGui::SameLine();
            if (g_hello) ImGui::TextColored(Ink(kGreen), "bridge connected");
            else ImGui::TextColored(Ink(kYellow), "waiting for the mod...");
            ImGui::TextDisabled("%s", g_ikemenVersion.c_str());
            ImGui::Text("%u snapshots, %u/s", g_snapshots, g_lastRate);
        } else {
            ImGui::TextDisabled("not running");
        }
        ImGui::EndPopup();
    }
}

void PlayersWindow() {
    if (!BeginTool("Players")) return;
    if (!g_bridge.Running()) {
        EmptyNote("The fighters of the match show here.");
        ImGui::End();
        return;
    }
    // The columns a character maker reads; the others are in the headers' right click menu
    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_Hideable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_SizingFixedFit;
    // Tight, whatever the theme's padding: the four columns fit a narrow panel
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ImGui::GetFontSize() * 0.2f, ImGui::GetStyle().CellPadding.y));
    if (ImGui::BeginTable("fighters", 14, flags)) { // a new name: widths saved under the old font don't apply
        ImGui::TableSetupScrollFreeze(0, 1);
        float em = ImGui::GetFontSize();
        ImGui::TableSetupColumn("Who", ImGuiTableColumnFlags_WidthFixed, em * 3.5f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_DefaultHide, em * 8);
        for (const char *h : {"State", "Prev", "Anim", "Elem", "Time", "Type", "Ctrl", "Pos", "Vel", "Life", "Power",
                              "Explods, projs"}) {
            bool shown = !strcmp(h, "State") || !strcmp(h, "Anim") || !strcmp(h, "Life");
            // Fitted once, while the table is empty: Life's header is narrower than its values
            float w = !strcmp(h, "Life") ? ImGui::CalcTextSize("10000").x : 0.0f;
            ImGui::TableSetupColumn(h, shown ? 0 : ImGuiTableColumnFlags_DefaultHide, w);
        }
        ImGui::TableHeadersRow();
        for (const auto &e : g_ents) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(e.id);
            std::string who = Who(e.pn, e.helper, e.id);
            if (ImGui::Selectable(who.c_str(), g_selId == e.id, ImGuiSelectableFlags_SpanAllColumns))
                g_selId = e.id, g_selPicked++;
            ImGui::PopID();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(e.name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%d", e.st);
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%d", e.pst);
            ImGui::TableNextColumn();
            ImGui::Text("%d", e.anim);
            ImGui::TableNextColumn();
            ImGui::Text("%d/%d", e.elem, e.elemn);
            ImGui::TableNextColumn();
            ImGui::Text("%d", e.time);
            ImGui::TableNextColumn();
            ImGui::Text("%s %s %s", e.stt.c_str(), e.mvt.c_str(), e.phy.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(e.ctrl ? "yes" : "-");
            ImGui::TableNextColumn();
            ImGui::Text("%.1f, %.1f", e.x, e.y);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f, %.2f", e.vx, e.vy);
            ImGui::TableNextColumn();
            ImGui::Text("%d", e.life);
            ImGui::TableNextColumn();
            ImGui::Text("%d", e.pow);
            ImGui::TableNextColumn();
            if (e.numExplods || e.numProjs) ImGui::Text("%d, %d", e.numExplods, e.numProjs);
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    ImGui::End();
}

void Field(const char *label, const char *fmt, ...) {
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", label);
    ImGui::TableNextColumn();
    va_list args;
    va_start(args, fmt);
    ImGui::TextV(fmt, args);
    va_end(args);
}

// var/fvar values; with the debugger patch, a double click edits one (setter is setVar or setFvar).
void VarGrid(const char *id, const std::vector<double> &vals, const std::vector<double> *prev, bool floats,
             int entity, const char *setter) {
    static int editIndex;
    static double editValue;
    int cols = std::max(1, (int)(ImGui::GetContentRegionAvail().x / (ImGui::GetFontSize() * (floats ? 8 : 6))));
    if (!ImGui::BeginTable(id, cols, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) return;
    bool open = false;
    for (size_t n = 0; n < vals.size(); n++) {
        ImGui::TableNextColumn();
        bool changed = prev && n < prev->size() && (*prev)[n] != vals[n];
        bool zero = vals[n] == 0;
        ImVec4 color = changed ? Ink(kYellow)
                     : zero    ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)
                               : ImGui::GetStyleColorVec4(ImGuiCol_Text);
        // Its name (Analysis, Variables) when it has one
        const Entity *ent = FindEntity(entity);
        std::string name = ent ? VarName(CharFile(ent->pn, "def"), floats ? 'f' : 'v', (int)n) : "";
        char label[96];
        if (floats)
            snprintf(label, sizeof(label), "%2zu%s%s: %.3f##%zu", n, name.empty() ? "" : " ", name.c_str(), vals[n], n);
        else
            snprintf(label, sizeof(label), "%2zu%s%s: %.0f##%zu", n, name.empty() ? "" : " ", name.c_str(), vals[n], n);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::Selectable(label, false, ImGuiSelectableFlags_AllowDoubleClick);
        if (!name.empty()) ImGui::SetItemTooltip("%s(%zu): %s", floats ? "fvar" : "var", n, name.c_str());
        if (g_canSet && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            editIndex = (int)n;
            editValue = vals[n];
            open = true;
        }
        ImGui::PopStyleColor();
    }
    ImGui::EndTable();
    if (open) ImGui::OpenPopup(id); // outside the table, which has its own ID scope
    if (ImGui::BeginPopup(id)) {
        ImGui::Text("%s(%d)", floats ? "fvar" : "var", editIndex);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        bool set = ImGui::InputDouble("##value", &editValue, 0, 0, floats ? "%.4f" : "%.0f",
                                      ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        set |= ImGui::Button("Set");
        if (set) {
            char lua[96];
            snprintf(lua, sizeof(lua), "%s(%d, %.6g)", setter, editIndex, editValue);
            ExecOn(entity, lua);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void InspectorWindow() {
    if (!BeginTool("Inspector")) return;
    const Entity *e = FindEntity(g_selId);
    if (!e) {
        if (!g_bridge.Running()) EmptyNote("What the selected fighter is doing shows here, tick by tick.");
        else ImGui::TextDisabled(g_selId < 0 ? "Click a fighter on the game." : "The selected one is gone.");
        ImGui::End();
        return;
    }
    if (Connected()) Watch(e->id);
    ImGui::Text("%s  %s", Who(e->pn, e->helper, e->id).c_str(), e->name.c_str());
    // The values take all that is left: their widths change each tick
    auto fields = [](const char *id) {
        if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit)) return false;
        ImGui::TableSetupColumn("label");
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        return true;
    };
    if (fields("fields")) {
        Field("State", "%d (prev %d)", e->st, e->pst);
        Field("Anim", "%d", e->anim);
        Field("Elem", "%d of %d, %d ticks in", e->elem, e->elemn, e->etime);
        Field("Ctrl", "%s", e->ctrl ? "yes" : "no");
        Field("Pos", "%.2f, %.2f", e->x, e->y);
        Field("Life", "%d / %d", e->life, e->lifemax);
        Field("Power", "%d / %d", e->pow, e->powmax);
        ImGui::EndTable();
    }
    // The rest on demand: open at first with the advanced tools
    auto section = [](const char *label) {
        if (AdvancedTools()) ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        return ImGui::CollapsingHeader(label);
    };
    if (section("Details") && fields("details")) {
        Field("Time", "%d", e->time);
        Field("AnimTime", "%d", e->atime);
        Field("Types", "%s / %s / %s", e->stt.c_str(), e->mvt.c_str(), e->phy.c_str());
        Field("Facing", "%s", e->facing < 0 ? "left" : "right");
        Field("Vel", "%.3f, %.3f", e->vx, e->vy);
        Field("HitPause", "%d", e->hitpause);
        Field("MoveHit", "%d (contact %d, guarded %d)", e->moveHit, e->moveContact, e->moveGuarded);
        ImGui::EndTable();
    }

    bool live = Connected();
    if (section("Actions")) {
        ImGui::BeginDisabled(!live);
        static int stateNo = 0, animNo = 0, life = 1000;
        float w = ImGui::GetFontSize() * 6;
        ImGui::SetNextItemWidth(w);
        ImGui::InputInt("##state", &stateNo, 0);
        ImGui::SameLine();
        if (ImGui::Button("SelfState")) ExecOn(e->id, "selfState(" + std::to_string(stateNo) + ")");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(w);
        ImGui::InputInt("##anim", &animNo, 0);
        ImGui::SameLine();
        if (ImGui::Button("ChangeAnim")) ExecOn(e->id, "changeAnim(" + std::to_string(animNo) + ")");
        ImGui::SetNextItemWidth(w);
        ImGui::InputInt("##life", &life, 0);
        ImGui::SameLine();
        if (ImGui::Button("Set life")) ExecOn(e->id, "setLife(" + std::to_string(life) + ")");
        ImGui::SameLine();
        if (ImGui::Button("Full life/power")) ExecOn(e->id, "setLife(lifeMax()); setPower(powerMax())");
        ImGui::SameLine();
        if (ImGui::Button("Stand")) ExecOn(e->id, "selfState(0)");
        if (!e->helper) {
            ImGui::SameLine();
            if (ImGui::Button("Toggle AI"))
                ExecOn(e->id, "if aiLevel() > 0 then setAILevel(0) return 'AI off' else setAILevel(8) return 'AI on' end");
        }
        if (g_canSet) {
            static float pos[2], vel[2];
            ImGui::SetNextItemWidth(w * 2);
            ImGui::InputFloat2("##pos", pos, "%.1f");
            ImGui::SameLine();
            if (ImGui::Button("Set pos")) {
                char lua[96];
                snprintf(lua, sizeof(lua), "setPos(%g, %g)", pos[0], pos[1]);
                ExecOn(e->id, lua);
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(w * 2);
            ImGui::InputFloat2("##vel", vel, "%.2f");
            ImGui::SameLine();
            if (ImGui::Button("Set vel")) {
                char lua[96];
                snprintf(lua, sizeof(lua), "setVel(%g, %g)", vel[0], vel[1]);
                ExecOn(e->id, lua);
            }
            if (!ImGui::IsAnyItemActive()) {
                pos[0] = (float)e->x, pos[1] = (float)e->y;
                vel[0] = (float)e->vx, vel[1] = (float)e->vy;
            }
        }
        ImGui::EndDisabled();
    }

    if (section("Variables")) {
        ImGui::SeparatorText("var");
        if (e->vars.empty())
            ImGui::TextDisabled("waiting for the variables...");
        else {
            auto prev = g_prevVars.find(e->id);
            VarGrid("vars", e->vars, prev == g_prevVars.end() ? nullptr : &prev->second, false, e->id, "setVar");
        }
        ImGui::SeparatorText("fvar");
        if (!e->fvars.empty()) {
            auto prev = g_prevFvars.find(e->id);
            VarGrid("fvars", e->fvars, prev == g_prevFvars.end() ? nullptr : &prev->second, true, e->id, "setFvar");
        }
        ImGui::TextDisabled(g_canSet ? "Yellow: changed on the last tick. Double-click a value to change it."
                                     : "Values that changed on the last tick are yellow.");
    }

    // Its explods and projectiles
    auto objects = [](const char *id, const std::vector<Entity::Object> &list, bool explods) {
        if (list.empty()) {
            ImGui::TextDisabled("none");
            return;
        }
        ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
        if (!ImGui::BeginTable(id, 7, flags)) return;
        for (const char *h : {"ID", "Anim", "Time", "Remove", "Pos", explods ? "Bind" : "Vel", explods ? "Layer" : "Hits"})
            ImGui::TableSetupColumn(h);
        ImGui::TableHeadersRow();
        for (const auto &o : list) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%d", o.id);
            ImGui::TableNextColumn();
            ImGui::Text("%d (%d)", o.anim, o.elem);
            ImGui::SetItemTooltip("Action %d, element %d", o.anim, o.elem);
            ImGui::TableNextColumn();
            ImGui::Text("%d", o.time);
            ImGui::TableNextColumn();
            ImGui::Text("%d", o.removetime);
            ImGui::TableNextColumn();
            ImGui::Text("%.1f, %.1f", o.x, o.y);
            ImGui::TableNextColumn();
            if (explods)
                ImGui::Text("%d", o.bindtime);
            else
                ImGui::Text("%.2f, %.2f", o.vx, o.vy);
            ImGui::TableNextColumn();
            if (explods) {
                ImGui::Text("%d", o.layer);
                ImGui::SetItemTooltip("Layer %d, sprpriority %d, facing %d", o.layer, o.sprpriority, o.facing);
            } else {
                ImGui::Text("%d", o.hits);
                ImGui::SetItemTooltip("Hits left %d, miss time %d, priority %d", o.hits, o.misstime, o.priority);
            }
        }
        ImGui::EndTable();
    };
    char head[48];
    snprintf(head, sizeof(head), "Explods (%zu)###explods", e->explods.size());
    if (!e->hasObjects)
        ImGui::TextDisabled(g_canTrace ? "" : "Explods and projectiles: needs an Ikemen GO with the debugger patch.");
    else {
        if (section(head)) objects("explods", e->explods, true);
        snprintf(head, sizeof(head), "Projectiles (%zu)###projs", e->projs.size());
        if (section(head)) objects("projs", e->projs, false);
    }
    ImGui::End();
}

// A move of an entity, from its tick history: from entering an attack state (movetype A) until
// control comes back. Its frames are the ticks it was not in hit pause.
struct Move {
    int start, end = -1; // ticks: its first, and the first one with control back (-1: not yet)
    std::vector<int> states;
    int frames = 0;
    int firstActive = 0, lastActive = 0; // frames (1-based) with a Clsn1 box
    std::vector<int> runs;               // lengths of the runs of frames with a Clsn1 box
    bool canceled = false, interrupted = false;
    // what its hits did: how many, their damage; the last one: when, on whom, guarded
    int hits = 0, damage = 0, target = 0, hitTick = -1;
    bool guarded = false;
    bool hasAdvantage = false, combo = false;
    int advantage = 0; // ticks the attacker is free before the one hit (negative: after)
};

// The first tick from `from` on at which the entity has control back after losing it.
static int FreeAgain(const std::deque<FrameRow> &rows, int from) {
    bool lost = false;
    for (const auto &r : rows) {
        if (r.t < from) continue;
        if (!r.ctrl) lost = true;
        else if (lost) return r.t;
    }
    return -1;
}

static std::vector<Move> FindMoves(int id) {
    std::vector<Move> moves;
    auto it = g_history.find(id);
    if (it == g_history.end()) return moves;
    const auto &rows = it->second;
    bool lastActive = false;
    for (size_t n = 0; n < rows.size(); n++) {
        const FrameRow &r = rows[n];
        const FrameRow *p = n ? &rows[n - 1] : nullptr;
        Move *m = moves.empty() || moves.back().end >= 0 ? nullptr : &moves.back();
        // A move begins entering an attack state, or changing state in one after its hits (a cancel)
        bool begins = r.mvt == "A" && (!p || p->mvt != "A" || (r.st != p->st && m && m->firstActive));
        if (begins) {
            if (m) m->canceled = true, m->end = r.t;
            moves.push_back({r.t});
            m = &moves.back();
            lastActive = false;
        } else if (m && (r.mvt == "H" || r.ctrl)) {
            m->interrupted = r.mvt == "H";
            m->end = r.t;
            continue;
        }
        if (!m) continue;
        if (m->states.empty() || m->states.back() != r.st) m->states.push_back(r.st);
        if (!begins && p->hitpause > 0) continue; // a tick of hit pause
        m->frames++;
        if (r.clsn1) {
            if (!m->firstActive) m->firstActive = m->frames;
            m->lastActive = m->frames;
            if (lastActive) m->runs.back()++;
            else m->runs.push_back(1);
        }
        lastActive = r.clsn1;
    }
    for (auto &m : moves) {
        for (const auto &t : g_trace) {
            if (t.kind != "got" || t.by != id || t.t < m.start || (m.end >= 0 && t.t >= m.end)) continue;
            m.hits++;
            m.damage += t.damage;
            m.target = t.id, m.hitTick = t.t, m.guarded = t.guarded;
        }
        if (m.hitTick < 0 || m.end < 0 || m.canceled || m.interrupted) continue;
        auto target = g_history.find(m.target);
        if (target == g_history.end()) continue;
        int free = FreeAgain(target->second, m.hitTick);
        if (free < 0) continue;
        // Hit again before it was free: a combo, no advantage to tell
        for (const auto &t : g_trace)
            if (t.kind == "got" && t.id == m.target && t.t > m.hitTick && t.t < free) m.combo = true;
        if (!m.combo) m.hasAdvantage = true, m.advantage = free - m.end;
    }
    return moves;
}

static void TicksTable(std::deque<FrameRow> &rows);

static void MovesTable(int id) {
    std::vector<Move> moves = FindMoves(id);
    if (moves.empty()) {
        ImGui::TextDisabled("No attack (a state with movetype A) in the last %zu ticks.", kHistory);
        return;
    }
    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable;
    if (!ImGui::BeginTable("moves", 9, flags)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    for (const char *h : {"Tick", "States", "Startup", "Active", "Recovery", "Total", "Result", "Advantage", "Damage"})
        ImGui::TableSetupColumn(h);
    ImGui::TableHeadersRow();
    for (auto m = moves.rbegin(); m != moves.rend(); ++m) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        char tick[16];
        snprintf(tick, sizeof(tick), "%d", m->start);
        ImGui::PushID(m->start);
        if (ImGui::Selectable(tick, g_selTick == m->start, ImGuiSelectableFlags_SpanAllColumns)) g_selTick = m->start;
        ImGui::PopID();
        ImGui::TableNextColumn();
        std::string states;
        for (int s : m->states) states += (states.empty() ? "" : " > ") + std::to_string(s);
        ImGui::TextUnformatted(states.c_str());
        ImGui::TableNextColumn();
        if (m->firstActive) ImGui::Text("%d", m->firstActive);
        else ImGui::TextDisabled("-");
        ImGui::TableNextColumn();
        std::string runs;
        for (int r : m->runs) runs += (runs.empty() ? "" : "+") + std::to_string(r);
        ImGui::TextUnformatted(runs.empty() ? "-" : runs.c_str());
        if (m->firstActive) ImGui::SetItemTooltip("Frames %d to %d", m->firstActive, m->lastActive);
        ImGui::TableNextColumn();
        if (m->end < 0) ImGui::TextDisabled("...");
        else if (m->canceled) ImGui::TextDisabled("canceled");
        else if (m->interrupted) ImGui::TextDisabled("got hit");
        else if (m->firstActive) ImGui::Text("%d", m->frames - m->lastActive);
        else ImGui::TextDisabled("-");
        ImGui::TableNextColumn();
        if (m->end >= 0) ImGui::Text("%d", m->frames);
        ImGui::TableNextColumn();
        if (m->hits == 0) ImGui::TextDisabled(m->end < 0 ? "" : "whiff");
        else {
            ImGui::TextColored(m->guarded ? Ink(kBlue) : Ink(kOrange), "%s",
                               m->guarded ? "guarded" : "hit");
            if (m->hits > 1) {
                ImGui::SameLine(0, 0);
                ImGui::Text(" (%d hits)", m->hits);
            }
        }
        ImGui::TableNextColumn();
        if (m->hasAdvantage)
            ImGui::TextColored(m->advantage >= 0 ? Ink(kGreen) : Ink(kRed), "%+d",
                               m->advantage);
        else if (m->combo)
            ImGui::TextDisabled("combo");
        ImGui::TableNextColumn();
        if (m->hits) ImGui::Text("%d", m->damage);
    }
    ImGui::EndTable();
}

void FrameDataWindow() {
    if (!BeginTool("Frame data")) return;
    auto it = g_history.find(g_selId);
    if (it == g_history.end() || it->second.empty()) {
        ImGui::TextDisabled("Tick by tick history of the selected player appears here.");
        ImGui::End();
        return;
    }
    if (ImGui::BeginTabBar("frametabs")) {
        if (ImGui::BeginTabItem("Ticks")) {
            TicksTable(it->second);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Moves")) {
            ImGui::TextDisabled("Frames: ticks out of hit pause. Startup: the first with a hit box; recovery: "
                                "until control is back.");
            HelpMarker("A move begins entering a state with movetype A, or changing state in one after its Clsn1 "
                       "frames (a cancel, or a follow-up state). Advantage: the tick its last hit's target has "
                       "control back, less the tick the attacker has (positive: the attacker is free first); none "
                       "when the target was hit again first (a combo). From the last ticks the debugger keeps.");
            MovesTable(g_selId);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

static void TicksTable(std::deque<FrameRow> &rows) {
    static bool onlyChanges = false;
    ImGui::Checkbox("Only ticks with a state change or a hit", &onlyChanges);
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) rows.clear();
    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable;
    if (g_canTrace) {
        ImGui::SameLine();
        ImGui::TextDisabled("Click a tick to see its controllers in Code.");
    }
    if (ImGui::BeginTable("frames", 12, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        for (const char *h : {"Tick", "State", "Anim", "Elem", "Elem time", "Type", "Ctrl", "HitPause", "Life",
                              "Pos", "Vel", "Controllers"})
            ImGui::TableSetupColumn(h);
        ImGui::TableHeadersRow();
        for (auto r = rows.rbegin(); r != rows.rend(); ++r) {
            if (onlyChanges && !r->stateChanged && !r->contact) continue;
            ImGui::TableNextRow();
            if (r->contact)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(150, 50, 50, 90));
            else if (r->stateChanged)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(60, 90, 160, 90));
            ImGui::TableNextColumn();
            char tick[16];
            snprintf(tick, sizeof(tick), "%d", r->t);
            bool picked = g_selTick == r->t || (g_selTick < 0 && r == rows.rbegin());
            if (ImGui::Selectable(tick, picked, ImGuiSelectableFlags_SpanAllColumns)) g_selTick = r->t;
            ImGui::TableNextColumn();
            ImGui::Text("%d", r->st);
            ImGui::TableNextColumn();
            ImGui::Text("%d", r->anim);
            ImGui::TableNextColumn();
            if (r->clsn1) {
                ImGui::TextColored(Ink(kRed), "%d", r->elem);
                ImGui::SetItemTooltip("Has a hit box");
            } else
                ImGui::Text("%d", r->elem);
            ImGui::TableNextColumn();
            ImGui::Text("%d", r->etime);
            ImGui::TableNextColumn();
            ImGui::Text("%s %s", r->stt.c_str(), r->mvt.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r->ctrl ? "yes" : "-");
            ImGui::TableNextColumn();
            ImGui::Text("%d", r->hitpause);
            ImGui::TableNextColumn();
            ImGui::Text("%d", r->life);
            ImGui::TableNextColumn();
            ImGui::Text("%.1f, %.1f", r->x, r->y);
            ImGui::TableNextColumn();
            ImGui::Text("%.2f, %.2f", r->vx, r->vy);
            ImGui::TableNextColumn();
            if (g_canTrace) ImGui::Text("%zu", r->ran.size());
        }
        ImGui::EndTable();
    }
}

void TraceWindow() {
    if (!BeginTool("Trace")) return;
    static bool showStates = true, showHits = true, follow = true;
    static int player = 0;
    ImGui::Checkbox("State changes", &showStates);
    ImGui::SameLine();
    ImGui::Checkbox("Hits", &showHits);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6);
    ImGui::Combo("Player", &player, "All\0P1\0P2\0P3\0P4\0");
    ImGui::SameLine();
    ImGui::Checkbox("Follow", &follow);
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) g_trace.clear();

    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("trace", 3, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Tick");
        ImGui::TableSetupColumn("Who", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 8);
        ImGui::TableSetupColumn("Event", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (size_t n = 0; n < g_trace.size(); n++) {
            const TraceEvent &t = g_trace[n];
            bool isState = t.kind == "st";
            if ((isState && !showStates) || (!isState && !showHits)) continue;
            if (player > 0 && t.pn != player) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%d", t.t);
            ImGui::TableNextColumn();
            ImGui::PushID((int)n);
            if (ImGui::Selectable(Who(t.pn, t.helper, t.id).c_str(), false,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
                g_selId = t.id, g_selPicked++;
            ImGui::PopID();
            ImGui::TableNextColumn();
            if (isState)
                ImGui::Text("state %d -> %d", t.from, t.to);
            else if (t.kind == "got") {
                if (t.hitdefSrc) {
                    const Source &s = g_sources[t.hitdefSrc];
                    ImGui::PushID((int)n);
                    if (ImGui::SmallButton(s.type.c_str())) {
                        g_codeFile = s.file;
                        g_codeLine = g_codeScrollTo = s.line;
                        g_codeHold = true;
                    }
                    ImGui::PopID();
                    ImGui::SetItemTooltip("The attack: %s:%d, in state %d. Click to show it in Code.", s.file.c_str(),
                                          s.line, s.state);
                    ImGui::SameLine();
                }
                // what the hit does to it: hitstun (or guard stun), hit shake (its hit pause), slide, ctrl
                std::string by = t.byPn ? Who(t.byPn, t.byHelper, t.by) : "id " + std::to_string(t.by);
                char combo[64] = "", juggle[32] = "";
                if (!t.guarded && t.combo > 1)
                    snprintf(combo, sizeof(combo), ", combo %d hits %d damage", t.combo, t.comboDamage);
                if (t.fall && t.juggle >= 0) snprintf(juggle, sizeof(juggle), ", juggle %d left", t.juggle);
                ImGui::TextColored(t.guarded ? Ink(kBlue) : Ink(kOrange),
                                   "%s %s (state %d): %d damage%s, %s %d, shake %d, slide %d, ctrl %d%s%s",
                                   t.guarded ? "guarded" : "hit by", by.c_str(), t.bySt, t.damage, combo,
                                   t.guarded ? "guard time" : "hit time", t.hittime, t.shake, t.slide,
                                   t.ctrltime, t.fall ? ", falls" : "", juggle);
                if (ImGui::BeginItemTooltip()) {
                    ImGui::Text("In its state %d. Attr %s, anim type %d, hit %d of the combo (%d damage)", t.st,
                                t.attr.c_str(), t.animtype, t.combo, t.comboDamage);
                    if (t.juggle >= 0) ImGui::Text("Juggle points its attacker has left on it: %d", t.juggle);
                    ImGui::TextDisabled("The damage is taken on the next tick. Hit time: the hit state's\n"
                                        "length after the shake; ctrl: when control comes back (guarding).");
                    ImGui::EndTooltip();
                }
            } else
                ImGui::TextColored(t.kind == "hit" ? Ink(kRed) : Ink(kBlue),
                                   "%s (in state %d)", t.kind == "hit" ? "its attack hit" : "its attack was guarded",
                                   t.st);
        }
        if (follow) ImGui::SetScrollHereY(1.0f);
        ImGui::EndTable();
    }
    ImGui::End();
}

void BreakpointsWindow() {
    if (!BeginTool("Breakpoints")) return;
    static int target = 0;
    static std::string cond = "stateNo() == 200";
    const char *targets[] = {"Any", "P1", "P1 + helpers", "P2", "P2 + helpers"};
    const int targetValues[] = {0, 1, -1, 2, -2};
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
    ImGui::Combo("##target", &target, targets, IM_ARRAYSIZE(targets));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-ImGui::GetFontSize() * 4);
    bool add = ImGui::InputText("##cond", &cond, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    add |= ImGui::Button(ICON_PLUS " Add");
    if (add && !cond.empty()) {
        Breakpoint b{g_nextBid++, targetValues[target], cond};
        g_breakpoints.push_back(b);
        if (Connected()) SendBreakpoint(b);
    }
    ImGui::TextDisabled("A Lua expression over Ikemen's triggers, checked after every tick:");
    ImGui::TextDisabled("  stateNo() == 200   moveType() == \"H\"   life() < 300   var(5) ~= 0");
    ImGui::TextDisabled("  anim() == 210 and animElemNo(0) == 3   moveHit() == 1");

    if (ImGui::BeginTable("bps", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("On");
        ImGui::TableSetupColumn("Who");
        ImGui::TableSetupColumn("Condition", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Hits");
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        for (size_t n = 0; n < g_breakpoints.size(); n++) {
            Breakpoint &b = g_breakpoints[n];
            ImGui::PushID(b.bid);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Checkbox("##on", &b.enabled) && Connected()) SendBreakpoint(b);
            ImGui::TableNextColumn();
            for (int k = 0; k < IM_ARRAYSIZE(targetValues); k++)
                if (targetValues[k] == b.target) ImGui::TextUnformatted(targets[k]);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(b.cond.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%d", b.hits);
            ImGui::TableNextColumn();
            if (ImGui::SmallButton(ICON_TRASH " Delete")) {
                if (Connected()) SendRaw("ikdbg.brkDel(" + std::to_string(b.bid) + ")");
                g_breakpoints.erase(g_breakpoints.begin() + n);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::SeparatorText("On state controllers");
    if (g_ctrlBreaks.empty())
        ImGui::TextDisabled(g_canTrace || !Connected() ? "Click left of a controller in the Code window."
                                                       : "Needs an Ikemen GO with the debugger patch.");
    if (!g_ctrlBreaks.empty() && ImGui::BeginTable("ctrlbps", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("On");
        ImGui::TableSetupColumn("Where", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Only if", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Hits");
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        for (size_t n = 0; n < g_ctrlBreaks.size(); n++) {
            CtrlBreak &b = g_ctrlBreaks[n];
            ImGui::PushID((int)n);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Checkbox("##on", &b.enabled) && Connected()) SendCtrlBreak(b, b.enabled);
            ImGui::TableNextColumn();
            char where[300];
            snprintf(where, sizeof(where), "%s:%d", b.file.c_str(), b.line);
            if (ImGui::Selectable(where)) {
                g_codeFile = b.file;
                g_codeLine = g_codeScrollTo = b.line;
            }
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##if", "always", &b.cond);
            if (ImGui::IsItemDeactivatedAfterEdit() && b.enabled && Connected()) SendCtrlBreak(b, true);
            ImGui::SetItemTooltip("Lua over Ikemen's triggers, read after the tick on the one that ran the "
                                  "controller:\n  var(3) == 2   stateNo() == 200 and time() > 4");
            ImGui::TableNextColumn();
            ImGui::Text("%d", b.hits);
            ImGui::TableNextColumn();
            if (ImGui::SmallButton(ICON_TRASH " Delete")) {
                if (Connected()) SendCtrlBreak(b, false);
                g_ctrlBreaks.erase(g_ctrlBreaks.begin() + n);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void WatchWindow() {
    if (!BeginTool("Watch")) return;
    // Expressions on the selected entity are read on the one whose variables are watched
    if (Connected() && g_selId >= 0) Watch(g_selId);
    static int target = 0;
    static std::string code;
    const char *targets[] = {"Selected", "P1", "P2", "P3", "P4"};
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6);
    ImGui::Combo("##target", &target, targets, IM_ARRAYSIZE(targets));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-ImGui::GetFontSize() * 4);
    bool add = ImGui::InputTextWithHint("##code", "life(), var(3)", &code, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    add |= ImGui::Button(ICON_PLUS " Add");
    if (add && !code.empty()) {
        g_watches.push_back({g_nextWatch++, target, code});
        if (Connected()) SendWatchExpr(g_watches.back());
        code.clear();
    }
    ImGui::TextDisabled("Lua over Ikemen's triggers, read with each tick: an expression, or statements that return.");

    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("watch", 4, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Who", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 5);
        ImGui::TableSetupColumn("Expression", ImGuiTableColumnFlags_WidthStretch, 1);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1);
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        for (size_t n = 0; n < g_watches.size(); n++) {
            WatchExpr &w = g_watches[n];
            ImGui::PushID(w.n);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::Combo("##t", &w.target, targets, IM_ARRAYSIZE(targets)) && Connected()) SendWatchExpr(w);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::InputText("##code", &w.code, ImGuiInputTextFlags_EnterReturnsTrue) && Connected()) {
                w.seen = false;
                SendWatchExpr(w);
            }
            ImGui::TableNextColumn();
            if (!w.error.empty())
                ImGui::TextColored(Ink(kRed), "%s", w.error.c_str());
            else if (w.changedTick >= 0 && w.changedTick == g_tick)
                ImGui::TextColored(Ink(kYellow), "%s", w.value.c_str());
            else
                ImGui::TextUnformatted(w.value.c_str());
            if (ImGui::BeginItemTooltip()) {
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40);
                ImGui::TextUnformatted(w.error.empty() ? w.value.c_str() : w.error.c_str());
                ImGui::PopTextWrapPos();
                if (w.changedTick >= 0)
                    ImGui::TextDisabled("Last changed at tick %d (%d ticks ago)", w.changedTick, g_tick - w.changedTick);
                ImGui::EndTooltip();
            }
            ImGui::TableNextColumn();
            if (ImGui::SmallButton(ICON_TRASH " Delete")) {
                if (Connected()) SendRaw("ikdbg.exprDel(" + std::to_string(w.n) + ")");
                g_watches.erase(g_watches.begin() + n);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

namespace {

// Controllers typed in the Console, as CNS and as ZSS
struct ConsoleExample {
    const char *name, *cns, *zss;
};
const ConsoleExample kConsoleExamples[] = {
    {"ChangeState", "type = ChangeState\nvalue = 0\nctrl = 1", "ChangeState{value: 0; ctrl: 1}"},
    {"VelSet", "type = VelSet\nx = 4\ny = -6", "VelSet{x: 4; y: -6}"},
    {"PosSet", "type = PosSet\nx = 0\ny = 0", "PosSet{x: 0; y: 0}"},
    {"VarSet", "type = VarSet\nv = 0\nvalue = 1", "VarSet{v: 0; value: 1}"},
    {"LifeAdd", "type = LifeAdd\nvalue = -100", "LifeAdd{value: -100}"},
    {"PowerAdd", "type = PowerAdd\nvalue = 1000", "PowerAdd{value: 1000}"},
    {"PalFX", "type = PalFX\ntime = 30\nadd = 120, 0, 0\nsinadd = 80, 0, 0, 10",
     "PalFX{time: 30; add: 120, 0, 0; sinadd: 80, 0, 0, 10}"},
    {"Explod", "type = Explod\nanim = F2\npos = 0, -70\npostype = p1\nsprpriority = 3",
     "Explod{anim: F2; pos: 0, -70; postype: p1; sprpriority: 3}"},
    {"EnvShake", "type = EnvShake\ntime = 20\nampl = -8\nfreq = 120", "EnvShake{time: 20; ampl: -8; freq: 120}"},
    {"HitDef",
     "type = HitDef\nattr = S, NA\ndamage = 30\nanimtype = Light\nguardflag = MA\nhitflag = MAF\npausetime = 8, "
     "8\nsparkno = 0\nground.type = High\nground.slidetime = 10\nground.hittime = 12\nground.velocity = -4",
     "HitDef{attr: S, NA; damage: 30; animtype: Light; guardflag: MA; hitflag: MAF; pausetime: 8, 8; sparkno: 0;\n"
     "  ground.type: High; ground.slidetime: 10; ground.hittime: 12; ground.velocity: -4}"},
};

// Is the character's first state file ZSS (false when not known yet)
bool CharIsZss(int pn) {
    const auto *files = FilesFor(pn);
    if (!files) return false;
    auto st = files->find("st");
    return st != files->end() && !st->second.empty() && Cns_IsZss(st->second[0]);
}

// The Console's controllers: the text, run on the selected player with Ctrl+Enter. True when run.
bool ControllerInput(float em) {
    static std::string text;
    static bool zss = false;
    static int zssPn = -1; // the player zss was last set for: a new one sets it from its files
    const Entity *e = FindEntity(g_selId);
    if (e && e->pn != zssPn && g_charFiles.count(e->pn)) {
        zss = CharIsZss(e->pn);
        zssPn = e->pn;
    }
    if (e) ImGui::Text("On %s%s (%d) %s", e->helper ? "a helper of P" : "P", std::to_string(e->pn).c_str(), e->id,
                       e->name.c_str());
    else ImGui::TextDisabled("Pick a player (Players) to run them on.");
    ImGui::SameLine();
    ImGui::Checkbox("ZSS", &zss);
    ImGui::SetItemTooltip("The text is ZSS statements (set from the character's first state file).");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(em * 9);
    if (ImGui::BeginCombo("##examples", "Examples")) {
        for (const auto &x : kConsoleExamples)
            if (ImGui::Selectable(x.name)) text = zss ? x.zss : x.cns;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    bool run = false;
    ImGui::BeginDisabled(!e || !Connected() || text.empty());
    run = ImGui::Button(ICON_PLAY " Run (Ctrl+Enter)");
    ImGui::EndDisabled();
    HelpMarker("State controllers compiled with the character's compiler and run once on the selected player, "
               "as a negative state runs: a ChangeState is taken at once. CNS: a [State] header and "
               "\"trigger1 = 1\" are added when missing; several [State] blocks run in order. ZSS: statements, "
               "with if, let and the character's functions.");
    ImGui::PushFont(dbg::Editor_Font(), 0.0f);
    bool enter = ImGui::InputTextMultiline("##ctrl", &text, ImVec2(-FLT_MIN, -FLT_MIN),
                                           ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AllowTabInput);
    ImGui::PopFont();
    if (enter) ImGui::SetKeyboardFocusHere(-1); // Ctrl+Enter leaves the text: back to it
    if ((run || enter) && e && Connected() && !text.empty()) {
        Log(LogLine::Info, std::string(zss ? "> (ZSS) " : "> (CNS) ") + text);
        ExecOn(e->id, "local ok, err = debugRunController(" + LuaQuote(text) + (zss ? ", true" : ", false") +
                          "); if not ok then error(err, 0) end; return 'ran; now in state ' .. stateNo()");
        return true;
    }
    return false;
}

} // namespace

void ConsoleWindow() {
    if (!BeginTool("Console")) return;
    static bool showEngine = true;
    static int mode = 0; // Lua, controllers
    static std::string input;
    static std::vector<std::string> history;
    static int historyPos = -1;
    static bool toBottom = false; // something was run: the log goes to its end
    float em = ImGui::GetFontSize();

    ImGui::Checkbox("Engine log", &showEngine);
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) g_log.clear();
    ImGui::SameLine(0, em * 1.5f);
    ImGui::RadioButton("Lua", &mode, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Controllers", &mode, 1);
    ImGui::SetItemTooltip("CNS or ZSS state controllers, run on the selected player.");
    float footer = mode == 0 ? ImGui::GetFrameHeightWithSpacing()
                             : ImGui::GetFrameHeightWithSpacing() + ImGui::GetTextLineHeight() * 6;
    if (ImGui::BeginChild("log", ImVec2(0, -footer), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::PushFont(dbg::Editor_Font(), 0.0f);
        for (const auto &l : g_log) {
            if (l.kind == LogLine::Engine && !showEngine) continue;
            ImVec4 color = l.kind == LogLine::Error    ? Ink(kRed)
                         : l.kind == LogLine::Result   ? Ink(kGreen)
                         : l.kind == LogLine::Engine   ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)
                         : l.kind == LogLine::Break    ? Ink(kYellow)
                                                       : ImGui::GetStyleColorVec4(ImGuiCol_Text);
            ImGui::TextColored(color, "%s", l.text.c_str());
        }
        ImGui::PopFont();
        if (toBottom || (g_logScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - ImGui::GetFontSize() * 4))
            ImGui::SetScrollHereY(1.0f);
        g_logScroll = toBottom = false;
    }
    ImGui::EndChild();
    if (mode == 1) {
        toBottom = ControllerInput(em);
        ImGui::End();
        return;
    }

    auto callback = [](ImGuiInputTextCallbackData *data) -> int {
        if (data->EventFlag != ImGuiInputTextFlags_CallbackHistory || history.empty()) return 0;
        if (data->EventKey == ImGuiKey_UpArrow)
            historyPos = historyPos < 0 ? (int)history.size() - 1 : std::max(0, historyPos - 1);
        else if (historyPos >= 0 && ++historyPos >= (int)history.size())
            historyPos = -1;
        data->DeleteChars(0, data->BufTextLen);
        if (historyPos >= 0) data->InsertChars(0, history[historyPos].c_str());
        return 0;
    };
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::PushFont(dbg::Editor_Font(), 0.0f);
    bool entered = ImGui::InputTextWithHint("##lua", "Lua, e.g.  player(2); return stateNo(), var(3)", &input,
                                            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory,
                                            callback);
    ImGui::PopFont();
    if (entered) {
        if (!input.empty()) {
            Log(LogLine::Info, "> " + input);
            Exec(input);
            toBottom = true;
            history.push_back(input);
            historyPos = -1;
            input.clear();
        }
        ImGui::SetKeyboardFocusHere(-1);
    }
    ImGui::End();
}

} // namespace app
