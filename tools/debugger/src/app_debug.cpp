// The debugger's windows: Session, Control, Players, Inspector, Frame data, Trace, Breakpoints, Watch,
// Console.
#include "app_internal.h"

#include <cfloat>
#include <cstdarg>
#include <fstream>

#include "imgui_internal.h"
#include "imgui_stdlib.h"

namespace app {

void SessionWindow() {
    if (!ImGui::Begin("Session")) {
        ImGui::End();
        return;
    }
    Settings &s = g_settings;
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##exe", "Ikemen_GO.exe", &s.exe);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
    ImGui::InputText("P1", &s.p1);
    ImGui::SameLine();
    ImGui::Checkbox("AI##1", &s.ai1);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
    ImGui::InputText("P2", &s.p2);
    ImGui::SameLine();
    ImGui::Checkbox("AI##2", &s.ai2);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
    ImGui::InputText("Stage", &s.stage);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
    ImGui::InputText("Extra", &s.extra);
    HelpMarker("Leave P1 or P2 empty to start at the title screen. Any other Ikemen GO command line flags go "
               "in Extra.");
    ImGui::Checkbox("Show the game's own window", &s.gameWindow);
    HelpMarker("With the patched Ikemen GO the game is shown in the Game window and its own window is hidden.");
    if (ImGui::SmallButton("Reset window layout")) g_firstRun = true; // App_Frame builds the default one

    bool running = g_bridge.Running();
    if (ImGui::Button(running ? "Restart" : "Launch")) Launch();
    ImGui::SameLine();
    ImGui::BeginDisabled(!running);
    if (ImGui::Button("Stop")) {
        g_bridge.Stop();
        Log(LogLine::Info, "Stopped the game.");
    }
    ImGui::EndDisabled();

    ImGui::Separator();
    if (running) {
        ImGui::Text("pid %lu", g_bridge.Pid());
        ImGui::SameLine();
        if (g_hello)
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1), "bridge connected");
        else
            ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.3f, 1), "waiting for the mod...");
        ImGui::TextDisabled("%s", g_ikemenVersion.c_str());
        ImGui::Text("%u snapshots, %u/s", g_snapshots, g_lastRate);
    } else {
        ImGui::TextDisabled("not running");
    }
    ImGui::End();
}

void ControlWindow() {
    if (!ImGui::Begin("Control")) {
        ImGui::End();
        return;
    }
    bool live = Connected();
    ImGui::BeginDisabled(!live);
    if (ImGui::Button(g_paused ? "Continue (F5)" : "Pause (F5)")) SendRaw(g_paused ? "ikdbg.pause(false)" : "ikdbg.pause(true)");
    ImGui::SameLine();
    if (ImGui::Button("Step (F10)")) SendRaw("ikdbg.step(1)");
    ImGui::SameLine();
    if (ImGui::Button("Step 10 (F11)")) SendRaw("ikdbg.step(10)");
    ImGui::SameLine();
    static int stepN = 60;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5);
    ImGui::InputInt("##n", &stepN, 0);
    ImGui::SameLine();
    if (ImGui::Button("Run N ticks")) SendRaw("ikdbg.step(" + std::to_string(std::max(1, stepN)) + ")");

    ImGui::TextUnformatted("Speed");
    for (float speed : {0.25f, 0.5f, 1.0f, 2.0f, 4.0f}) {
        ImGui::SameLine();
        char label[16];
        snprintf(label, sizeof(label), "%gx", speed);
        if (ImGui::SmallButton(label)) Exec("setAccel(" + std::to_string(speed) + ")");
    }
    ImGui::SameLine(0, ImGui::GetFontSize() * 2);
    if (ImGui::SmallButton("Save state")) Exec("saveState()");
    ImGui::SameLine();
    if (ImGui::SmallButton("Load state")) Exec("loadState()");
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset round")) Exec("resetRound()");
    ImGui::SameLine();
    if (ImGui::SmallButton("Reload match")) Exec("reload()");
    HelpMarker("Reload match reads the characters, stage and fight screen from disk again: edit a file, "
               "then reload.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Collision boxes")) Exec("toggleClsnDisplay()");
    ImGui::SameLine();
    if (ImGui::SmallButton("Debug display")) Exec("toggleDebugDisplay()");
    ImGui::EndDisabled();

    ImGui::Separator();
    if (!live) {
        ImGui::TextDisabled("Launch the game from the Session window.");
    } else {
        const char *state = g_steps > 0 ? "STEPPING" : g_paused ? "PAUSED" : "RUNNING";
        ImVec4 color = g_paused ? ImVec4(1, 0.75f, 0.3f, 1) : ImVec4(0.4f, 0.9f, 0.4f, 1);
        ImGui::TextColored(color, "%s", state);
        ImGui::SameLine();
        ImGui::Text("tick %d   round %d   round state %d   %d players/helpers", g_tick, g_roundNo, g_roundState,
                    (int)g_ents.size());
        if (!g_breakText.empty()) {
            ImGui::TextColored(ImVec4(1, 0.45f, 0.45f, 1), "%s", g_breakText.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) g_breakText.clear();
        }
    }
    ImGui::End();

    ImGuiIO &io = ImGui::GetIO();
    if (live && !io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) SendRaw(g_paused ? "ikdbg.pause(false)" : "ikdbg.pause(true)");
        if (ImGui::IsKeyPressed(ImGuiKey_F10)) SendRaw("ikdbg.step(1)");
        if (ImGui::IsKeyPressed(ImGuiKey_F11)) SendRaw("ikdbg.step(10)");
    }
}

void PlayersWindow() {
    if (!ImGui::Begin("Players")) {
        ImGui::End();
        return;
    }
    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("players", 13, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        float em = ImGui::GetFontSize();
        ImGui::TableSetupColumn("Who", ImGuiTableColumnFlags_WidthFixed, em * 7);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, em * 8);
        for (const char *h : {"State", "Prev", "Anim", "Elem", "Time", "Type", "Ctrl", "Pos", "Vel", "Life", "Power"})
            ImGui::TableSetupColumn(h);
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
            ImGui::Text("%d/%d", e.life, e.lifemax);
            ImGui::TableNextColumn();
            ImGui::Text("%d", e.pow);
        }
        ImGui::EndTable();
    }
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
        ImVec4 color = changed ? ImVec4(1, 0.8f, 0.3f, 1)
                     : zero    ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)
                               : ImGui::GetStyleColorVec4(ImGuiCol_Text);
        char label[48];
        if (floats)
            snprintf(label, sizeof(label), "%2zu: %.3f##%zu", n, vals[n], n);
        else
            snprintf(label, sizeof(label), "%2zu: %.0f##%zu", n, vals[n], n);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::Selectable(label, false, ImGuiSelectableFlags_AllowDoubleClick);
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
    if (!ImGui::Begin("Inspector")) {
        ImGui::End();
        return;
    }
    const Entity *e = FindEntity(g_selId);
    if (!e) {
        ImGui::TextDisabled(g_selId < 0 ? "Pick a player in the Players window." : "The selected entity is gone.");
        ImGui::End();
        return;
    }
    if (Connected()) Watch(e->id);
    ImGui::Text("%s  %s", Who(e->pn, e->helper, e->id).c_str(), e->name.c_str());
    if (ImGui::BeginTable("fields", 2, ImGuiTableFlags_SizingFixedFit)) {
        Field("State", "%d (prev %d)", e->st, e->pst);
        Field("Time", "%d", e->time);
        Field("Anim", "%d", e->anim);
        Field("Elem", "%d of %d, %d ticks in", e->elem, e->elemn, e->etime);
        Field("AnimTime", "%d", e->atime);
        Field("Types", "%s / %s / %s", e->stt.c_str(), e->mvt.c_str(), e->phy.c_str());
        Field("Ctrl", "%s", e->ctrl ? "yes" : "no");
        Field("Facing", "%s", e->facing < 0 ? "left" : "right");
        Field("Pos", "%.2f, %.2f", e->x, e->y);
        Field("Vel", "%.3f, %.3f", e->vx, e->vy);
        Field("Life", "%d / %d", e->life, e->lifemax);
        Field("Power", "%d / %d", e->pow, e->powmax);
        Field("HitPause", "%d", e->hitpause);
        Field("MoveHit", "%d (contact %d, guarded %d)", e->moveHit, e->moveContact, e->moveGuarded);
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Actions");
    bool live = Connected();
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
        if (ImGui::CollapsingHeader(head, ImGuiTreeNodeFlags_DefaultOpen)) objects("explods", e->explods, true);
        snprintf(head, sizeof(head), "Projectiles (%zu)###projs", e->projs.size());
        if (ImGui::CollapsingHeader(head, ImGuiTreeNodeFlags_DefaultOpen)) objects("projs", e->projs, false);
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
            ImGui::TextColored(m->guarded ? ImVec4(0.6f, 0.8f, 1, 1) : ImVec4(1, 0.7f, 0.4f, 1), "%s",
                               m->guarded ? "guarded" : "hit");
            if (m->hits > 1) {
                ImGui::SameLine(0, 0);
                ImGui::Text(" (%d hits)", m->hits);
            }
        }
        ImGui::TableNextColumn();
        if (m->hasAdvantage)
            ImGui::TextColored(m->advantage >= 0 ? ImVec4(0.4f, 0.9f, 0.4f, 1) : ImVec4(1, 0.45f, 0.45f, 1), "%+d",
                               m->advantage);
        else if (m->combo)
            ImGui::TextDisabled("combo");
        ImGui::TableNextColumn();
        if (m->hits) ImGui::Text("%d", m->damage);
    }
    ImGui::EndTable();
}

void FrameDataWindow() {
    if (!ImGui::Begin("Frame data")) {
        ImGui::End();
        return;
    }
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
            ImGui::TextDisabled("Frames: ticks out of hit pause. Startup: the first with a Clsn1 box; recovery: "
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
                ImGui::TextColored(ImVec4(1, 0.45f, 0.45f, 1), "%d", r->elem);
                ImGui::SetItemTooltip("Has a Clsn1 box");
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
    if (!ImGui::Begin("Trace")) {
        ImGui::End();
        return;
    }
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
            if (ImGui::Selectable(Who(t.pn, t.helper, t.id).c_str(), false, ImGuiSelectableFlags_SpanAllColumns))
                g_selId = t.id, g_selPicked++;
            ImGui::PopID();
            ImGui::TableNextColumn();
            if (isState)
                ImGui::Text("state %d -> %d", t.from, t.to);
            else if (t.kind == "got") {
                // what the hit does to it: hitstun (or guard stun), hit shake (its hit pause), slide, ctrl
                std::string by = t.byPn ? Who(t.byPn, t.byHelper, t.by) : "id " + std::to_string(t.by);
                ImGui::TextColored(t.guarded ? ImVec4(0.6f, 0.8f, 1, 1) : ImVec4(1, 0.7f, 0.4f, 1),
                                   "%s %s (state %d): %d damage, %s %d, shake %d, slide %d, ctrl %d%s",
                                   t.guarded ? "guarded" : "hit by", by.c_str(), t.bySt, t.damage,
                                   t.guarded ? "guard time" : "hit time", t.hittime, t.shake, t.slide,
                                   t.ctrltime, t.fall ? ", falls" : "");
                if (ImGui::BeginItemTooltip()) {
                    ImGui::Text("In its state %d. Attr %s, anim type %d, hit %d of the combo", t.st, t.attr.c_str(),
                                t.animtype, t.combo);
                    ImGui::TextDisabled("The damage is taken on the next tick. Hit time: the hit state's\n"
                                        "length after the shake; ctrl: when control comes back (guarding).");
                    ImGui::EndTooltip();
                }
            } else
                ImGui::TextColored(t.kind == "hit" ? ImVec4(1, 0.5f, 0.5f, 1) : ImVec4(0.6f, 0.8f, 1, 1),
                                   "%s (in state %d)", t.kind == "hit" ? "its attack hit" : "its attack was guarded",
                                   t.st);
        }
        if (follow) ImGui::SetScrollHereY(1.0f);
        ImGui::EndTable();
    }
    ImGui::End();
}

void BreakpointsWindow() {
    if (!ImGui::Begin("Breakpoints")) {
        ImGui::End();
        return;
    }
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
    add |= ImGui::Button("Add");
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
            if (ImGui::SmallButton("Delete")) {
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
            if (ImGui::SmallButton("Delete")) {
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
    // A window the saved layout does not know joins Breakpoints
    if (ImGuiWindow *bp = ImGui::FindWindowByName("Breakpoints"); bp && bp->DockId)
        ImGui::SetNextWindowDockID(bp->DockId, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Watch")) {
        ImGui::End();
        return;
    }
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
    add |= ImGui::Button("Add");
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
                ImGui::TextColored(ImVec4(1, 0.45f, 0.45f, 1), "%s", w.error.c_str());
            else if (w.changedTick >= 0 && w.changedTick == g_tick)
                ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "%s", w.value.c_str());
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
            if (ImGui::SmallButton("Delete")) {
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

void ConsoleWindow() {
    if (!ImGui::Begin("Console")) {
        ImGui::End();
        return;
    }
    static bool showEngine = true;
    static std::string input;
    static std::vector<std::string> history;
    static int historyPos = -1;

    ImGui::Checkbox("Engine log", &showEngine);
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) g_log.clear();
    float footer = ImGui::GetFrameHeightWithSpacing();
    if (ImGui::BeginChild("log", ImVec2(0, -footer), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
        for (const auto &l : g_log) {
            if (l.kind == LogLine::Engine && !showEngine) continue;
            ImVec4 color = l.kind == LogLine::Error    ? ImVec4(1, 0.45f, 0.45f, 1)
                         : l.kind == LogLine::Result   ? ImVec4(0.6f, 0.9f, 0.6f, 1)
                         : l.kind == LogLine::Engine   ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)
                         : l.kind == LogLine::Break    ? ImVec4(1, 0.75f, 0.3f, 1)
                                                       : ImGui::GetStyleColorVec4(ImGuiCol_Text);
            ImGui::TextColored(color, "%s", l.text.c_str());
        }
        if (g_logScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - ImGui::GetFontSize() * 4)
            ImGui::SetScrollHereY(1.0f);
        g_logScroll = false;
    }
    ImGui::EndChild();

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
    if (ImGui::InputTextWithHint("##lua", "Lua, e.g.  player(2); return stateNo(), var(3)", &input,
                                 ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory,
                                 callback)) {
        if (!input.empty()) {
            Log(LogLine::Info, "> " + input);
            Exec(input);
            history.push_back(input);
            historyPos = -1;
            input.clear();
        }
        ImGui::SetKeyboardFocusHere(-1);
    }
    ImGui::End();
}

} // namespace app
