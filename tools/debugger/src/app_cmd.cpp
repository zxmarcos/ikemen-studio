// Commands: the selected player's input as the game reads it, the commands under way, and
// its CMD file's [Command] sections, edited here and applied like the Code window.
#include "app_internal.h"

#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <set>

#include "cmd.h"
#include "cns.h"
#include "make.h"
#include "imgui_stdlib.h"

namespace app {

const char *const kInputKeys[14] = {"U", "D", "B", "F", "a", "b", "c", "x", "y", "z", "s", "d", "w", "m"};

// An input held for some frames: the keys held (bits in kInputKeys order).
struct InputRun {
    unsigned keys;
    int frames;
};
std::map<int, std::deque<InputRun>> g_inputRuns; // player number -> newest first

// When each command definition was last seen buffered, and how many times it was completed.
struct CmdSeen {
    double on = -1e9; // ImGui time
    int buffer = 0, count = 0;
};
std::map<std::string, CmdSeen> g_cmdSeen; // "pn name n"

std::string CmdSeenKey(int pn, const std::string &name, int n) {
    return std::to_string(pn) + " " + name + " " + std::to_string(n);
}

// What every player's input and commands did since the last frame read.
void UpdateInputs() {
    static unsigned lastFrame;
    if (g_framesRead == lastFrame) return;
    lastFrame = g_framesRead;
    for (const FrameEntity &f : g_frameEnts) {
        if (f.helper || f.held.size() < 14) continue;
        // The keys held now, since as long as the key that changed last
        unsigned keys = 0;
        int frames = INT_MAX;
        for (int k = 0; k < 14; k++) {
            if (f.held[k] > 0) keys |= 1u << k;
            frames = std::min(frames, abs(f.held[k]));
        }
        auto &runs = g_inputRuns[f.pn];
        if (runs.empty() || runs.front().keys != keys || frames < runs.front().frames) runs.push_front({keys, frames});
        else runs.front().frames = frames;
        while (runs.size() > 300) runs.pop_back();

        std::set<std::string> buffered;
        for (const auto &c : f.cmds) {
            if (c.buffer <= 0) continue;
            std::string key = CmdSeenKey(f.pn, c.name, c.n);
            buffered.insert(key);
            CmdSeen &s = g_cmdSeen[key];
            if (c.buffer > s.buffer) s.count++; // buffered again: completed
            s.on = ImGui::GetTime();
            s.buffer = c.buffer;
        }
        std::string prefix = std::to_string(f.pn) + " ";
        for (auto &kv : g_cmdSeen)
            if (kv.first.compare(0, prefix.size(), prefix) == 0 && !buffered.count(kv.first)) kv.second.buffer = 0;
    }
}

// Keys as the CMD file writes them: "DF+a", "x+y"; "" for none.
std::string InputName(unsigned keys) {
    std::string dir, out;
    for (int k = 0; k < 4; k++)
        if (keys & (1u << k)) dir += kInputKeys[k];
    out = dir;
    for (int k = 4; k < 14; k++)
        if (keys & (1u << k)) out += (out.empty() ? "" : "+") + std::string(kInputKeys[k]);
    return out;
}

void HeldKeys(const FrameEntity &f) {
    ImDrawList *draw = ImGui::GetWindowDrawList();
    float cell = ImGui::GetFrameHeight();
    for (int k = 0; k < 14; k++) {
        if (k) ImGui::SameLine(0, k == 4 ? cell * 0.6f : 2);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(cell, cell));
        bool held = f.held[k] > 0;
        draw->AddRectFilled(p, ImVec2(p.x + cell, p.y + cell), held ? IM_COL32(60, 150, 80, 255) : IM_COL32(45, 45, 52, 255), 3);
        ImVec2 size = ImGui::CalcTextSize(kInputKeys[k]);
        draw->AddText(ImVec2(p.x + (cell - size.x) / 2, p.y + (cell - size.y) / 2),
                      held ? IM_COL32_WHITE : IM_COL32(120, 120, 128, 255), kInputKeys[k]);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(held ? "%s: held for %d frames" : "%s: released %d frames ago", kInputKeys[k], abs(f.held[k]));
    }
}

// Parsed CMD files, parsed again when their text changes.
struct CmdCache {
    std::string text;
    bool parsed = false;
    CmdFile cmd;
};
std::map<std::string, CmdCache> g_cmdFiles;

const CmdFile &CmdOf(const SourceFile &sf) {
    CmdCache &c = g_cmdFiles[NormPath(sf.path)];
    if (!c.parsed || c.text != sf.text) {
        c.text = sf.text;
        Cmd_Parse(c.text, c.cmd);
        c.parsed = true;
    }
    return c.cmd;
}

// The text of the CMD file changed here: the Code window's editor takes it.
void CmdChanged(SourceFile &sf, bool ok) {
    if (ok) sf.version++;
    else Log(LogLine::Error, "The command moved in the file; pick it again.");
}

// A direction as a vector, facing right (F is +x, D is +y)
ImVec2 DirVector(int d) {
    static const float v[9][2] = {{0, 0}, {-1, 0}, {-1, 1}, {0, 1}, {1, 1}, {1, 0}, {1, -1}, {0, -1}, {-1, -1}};
    float x = v[d][0], y = v[d][1], n = sqrtf(x * x + y * y);
    return n > 0 ? ImVec2(x / n, y / n) : ImVec2(0, 0);
}

void DrawArrow(ImDrawList *draw, ImVec2 c, float r, int d, ImU32 col) {
    if (!d) {
        draw->AddCircleFilled(c, r * 0.25f, col);
        return;
    }
    ImVec2 v = DirVector(d), n(-v.y, v.x);
    ImVec2 tip(c.x + v.x * r, c.y + v.y * r), tail(c.x - v.x * r * 0.8f, c.y - v.y * r * 0.8f);
    ImVec2 base(c.x + v.x * r * 0.15f, c.y + v.y * r * 0.15f);
    draw->AddLine(tail, base, col, r * 0.28f);
    draw->AddTriangleFilled(tip, ImVec2(base.x + n.x * r * 0.55f, base.y + n.y * r * 0.55f),
                            ImVec2(base.x - n.x * r * 0.55f, base.y - n.y * r * 0.55f), col);
}

// In kCmdButtons order: a b c x y z s d w m
const ImU32 kButtonColors[10] = {IM_COL32(220, 90, 90, 255),  IM_COL32(230, 180, 60, 255), IM_COL32(90, 190, 90, 255),
                                 IM_COL32(90, 140, 230, 255), IM_COL32(170, 110, 220, 255), IM_COL32(80, 200, 200, 255),
                                 IM_COL32(160, 160, 160, 255), IM_COL32(200, 130, 90, 255), IM_COL32(130, 170, 110, 255),
                                 IM_COL32(120, 120, 140, 255)};

float StepWidth(const CmdStep &s, float em) {
    if (!s.raw.empty()) return std::max(em * 3, ImGui::CalcTextSize(s.raw.c_str()).x + em * 0.8f);
    int buttons = __builtin_popcount(s.buttons);
    return em * 0.8f + (s.dir || !buttons ? em * 1.9f : 0) + buttons * em * 1.5f;
}

// A step as a chip: its arrow and buttons, its prefixes above. True when clicked.
bool StepChip(const CmdStep &s, int id, bool picked) {
    float em = ImGui::GetFontSize(), w = StepWidth(s, em), h = em * 3.4f;
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("step", ImVec2(w, h));
    ImDrawList *draw = ImGui::GetWindowDrawList();
    ImU32 bg = picked ? IM_COL32(90, 75, 30, 255) : ImGui::IsItemHovered() ? IM_COL32(60, 64, 80, 255) : IM_COL32(45, 48, 60, 255);
    draw->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bg, em * 0.35f);
    if (picked) draw->AddRect(p, ImVec2(p.x + w, p.y + h), IM_COL32(230, 190, 80, 255), em * 0.35f, 2.0f);
    std::string mods;
    if (s.strict) mods += "> ";
    if (s.release) mods += s.charge > 0 ? "~" + std::to_string(s.charge) + " " : "~ ";
    if (s.hold) mods += "hold ";
    if (s.four) mods += "$";
    draw->AddText(ImVec2(p.x + em * 0.3f, p.y + em * 0.1f), IM_COL32(230, 200, 120, 255), mods.c_str());
    float x = p.x + em * 0.4f, cy = p.y + h * 0.62f;
    if (!s.raw.empty()) {
        draw->AddText(ImVec2(x, cy - em * 0.5f), IM_COL32(255, 140, 140, 255), s.raw.c_str());
    } else {
        if (s.dir || !s.buttons) {
            DrawArrow(draw, ImVec2(x + em * 0.95f, cy), em * 0.7f, s.dir, IM_COL32(235, 235, 235, 255));
            x += em * 1.9f;
        }
        for (int i = 0; kCmdButtons[i]; i++)
            if (s.buttons & (1u << i)) {
                draw->AddCircleFilled(ImVec2(x + em * 0.7f, cy), em * 0.62f, kButtonColors[i]);
                char t[2] = {kCmdButtons[i], 0};
                ImVec2 ts = ImGui::CalcTextSize(t);
                draw->AddText(ImVec2(x + em * 0.7f - ts.x / 2, cy - ts.y / 2), IM_COL32(20, 20, 20, 255), t);
                x += em * 1.5f;
            }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Cmd_StepText(s).c_str());
    ImGui::PopID();
    return clicked;
}

// Motions to start from: the last step's buttons stay at the end
const struct {
    const char *label, *command;
} kMotions[] = {
    {"Quarter circle forward (D, DF, F)", "~D, DF, F"},
    {"Quarter circle back (D, DB, B)", "~D, DB, B"},
    {"Dragon punch (F, D, DF)", "~F, D, DF"},
    {"Half circle forward (B, DB, D, DF, F)", "~B, DB, D, DF, F"},
    {"Half circle back (F, DF, D, DB, B)", "~F, DF, D, DB, B"},
    {"Double quarter circle forward", "~D, DF, F, D, DF, F"},
    {"Dash forward (F, F)", "F, F"},
    {"Dash back (B, B)", "B, B"},
    {"Charge back, then forward", "~30$B, F"},
    {"Charge down, then up", "~30$D, U"},
    {"Hold down", "/$D"},
};

// The selected command's motion as a row of steps, the one picked edited under it; and a
// ChangeState on the command at the top of [Statedef -1].
void MotionEditor(SourceFile &sf, const CmdCommand &c, int pn) {
    static int pick;
    static std::string pickFor;
    if (pickFor != c.name + "\n" + std::to_string(c.index)) pickFor = c.name + "\n" + std::to_string(c.index), pick = 0;
    std::vector<CmdStep> steps = Cmd_Steps(c.command);
    bool changed = false;
    float em = ImGui::GetFontSize();
    if (ImGui::CollapsingHeader("Motion", ImGuiTreeNodeFlags_DefaultOpen)) {
        float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        for (int i = 0; i < (int)steps.size(); i++) {
            if (i) {
                ImGui::SameLine(0, em * 0.3f);
                if (ImGui::GetCursorScreenPos().x + StepWidth(steps[i], em) > right) ImGui::NewLine();
            }
            if (StepChip(steps[i], i, i == pick)) pick = i;
        }
        if (!steps.empty()) ImGui::SameLine(0, em * 0.3f);
        if (ImGui::Button("+", ImVec2(em * 2, em * 3.4f))) {
            CmdStep s;
            s.dir = 5; // F
            int at = std::min((int)steps.size(), pick + 1);
            steps.insert(steps.begin() + at, s);
            pick = at;
            changed = true;
        }
        ImGui::SetItemTooltip("A step after the one picked");
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##start", "Start from a motion...", ImGuiComboFlags_HeightLarge)) {
            for (const auto &m : kMotions)
                if (ImGui::Selectable(m.label)) {
                    unsigned buttons = !steps.empty() && !steps.back().dir && steps.back().raw.empty() ? steps.back().buttons : 1u << 3;
                    steps = Cmd_Steps(m.command);
                    if (!strchr(m.command, '/')) {
                        CmdStep last;
                        last.buttons = buttons;
                        steps.push_back(last);
                    }
                    pick = (int)steps.size() - 1;
                    changed = true;
                }
            ImGui::EndCombo();
        }

        // The step picked
        if (!changed && !steps.empty()) {
            pick = std::clamp(pick, 0, (int)steps.size() - 1);
            CmdStep &s = steps[pick];
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Step %d", pick + 1);
            ImGui::SameLine();
            if (ImGui::SmallButton("<") && pick > 0) {
                std::swap(steps[pick], steps[pick - 1]);
                pick--;
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(">") && pick + 1 < (int)steps.size()) {
                std::swap(steps[pick], steps[pick + 1]);
                pick++;
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete the step") && steps.size() > 1) {
                steps.erase(steps.begin() + pick);
                pick = std::max(0, pick - 1);
                changed = true;
            }
            if (!changed && !s.raw.empty()) {
                ImGui::TextColored(ImVec4(1, 0.55f, 0.55f, 1), "\"%s\" is edited in the command's text.", s.raw.c_str());
            } else if (!changed) {
                // The direction pad, facing right
                static const int pad[3][3] = {{8, 7, 6}, {1, 0, 5}, {2, 3, 4}};
                float cell = em * 2.1f;
                ImGui::BeginGroup();
                ImGui::TextDisabled("direction");
                for (int r = 0; r < 3; r++)
                    for (int col = 0; col < 3; col++) {
                        int d = pad[r][col];
                        if (col) ImGui::SameLine(0, 2);
                        ImGui::PushID(r * 3 + col);
                        ImVec2 bp = ImGui::GetCursorScreenPos();
                        ImGui::PushStyleColor(ImGuiCol_Button, s.dir == d ? ImVec4(0.75f, 0.6f, 0.2f, 1) : ImVec4(0.2f, 0.22f, 0.28f, 1));
                        if (ImGui::Button("##d", ImVec2(cell, cell))) {
                            s.dir = d;
                            changed = true;
                        }
                        ImGui::PopStyleColor();
                        DrawArrow(ImGui::GetWindowDrawList(), ImVec2(bp.x + cell / 2, bp.y + cell / 2), cell * 0.32f, d,
                                  d ? IM_COL32(235, 235, 235, 255) : IM_COL32(150, 150, 150, 255));
                        ImGui::SetItemTooltip(d ? "%s" : "no direction", kCmdDirs[d]);
                        ImGui::PopID();
                    }
                ImGui::EndGroup();
                ImGui::SameLine(0, em);
                ImGui::BeginGroup();
                ImGui::TextDisabled("buttons, together");
                for (int i = 0; kCmdButtons[i]; i++) {
                    if (i % 3 || i == 9) ImGui::SameLine(0, 2); // a b c, x y z, s d w m
                    bool on = (s.buttons >> i) & 1;
                    char t[2] = {kCmdButtons[i], 0};
                    ImGui::PushID(100 + i);
                    ImGui::PushStyleColor(ImGuiCol_Button, on ? ImGui::ColorConvertU32ToFloat4(kButtonColors[i]) : ImVec4(0.2f, 0.22f, 0.28f, 1));
                    ImGui::PushStyleColor(ImGuiCol_Text, on ? ImVec4(0.08f, 0.08f, 0.08f, 1) : ImGui::GetStyleColorVec4(ImGuiCol_Text));
                    if (ImGui::Button(t, ImVec2(cell, cell))) {
                        s.buttons ^= 1u << i;
                        changed = true;
                    }
                    ImGui::PopStyleColor(2);
                    ImGui::PopID();
                }
                ImGui::EndGroup();
                ImGui::SameLine(0, em);
                ImGui::BeginGroup();
                ImGui::TextDisabled("how");
                changed |= ImGui::Checkbox("held (/)", &s.hold);
                changed |= ImGui::Checkbox("released (~)", &s.release);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(em * 3);
                ImGui::BeginDisabled(!s.release);
                if (ImGui::InputInt("charge", &s.charge, 0)) {
                    s.charge = std::max(0, s.charge);
                    changed = true;
                }
                ImGui::EndDisabled();
                ImGui::SetItemTooltip("Frames it must be held before it is released (0: none)");
                changed |= ImGui::Checkbox("$: the diagonals count", &s.four);
                changed |= ImGui::Checkbox(">: nothing else in between", &s.strict);
                ImGui::EndGroup();
                if (!s.dir && !s.buttons) ImGui::TextColored(ImVec4(1, 0.55f, 0.55f, 1), "A step needs a direction or a button.");
            }
        }
    }
    // Written once every step means something
    for (const CmdStep &s : steps)
        if (s.raw.empty() && !s.dir && !s.buttons) changed = false;
    if (changed) {
        CmdChanged(sf, Cmd_SetField(sf.text, c, "command", Cmd_StepsText(steps)));
        return; // c is the old text's
    }

    if (ImGui::CollapsingHeader("Start a state with this command")) {
        static int state = 200, when = 1;
        static bool ctrl = true;
        ImGui::SetNextItemWidth(em * 5);
        ImGui::InputInt("state", &state, 0);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(em * 8);
        ImGui::Combo("when", &when, "any statetype\0standing (S)\0crouching (C)\0in the air (A)\0");
        ImGui::SameLine();
        ImGui::Checkbox("with control", &ctrl);
        // The [Statedef -1] the engine reads first, or a new one in the CMD file
        std::string file = StatedefFile(pn, -1);
        if (ImGui::Button("Add to [Statedef -1]")) {
            SourceFile &tf = LoadSource(file.empty() ? sf.path : file);
            bool zss = Cns_IsZss(tf.path);
            if (file.empty()) tf.text += std::string(tf.text.empty() || tf.text.back() == '\n' ? "" : "\n") + "\n[Statedef -1]\n";
            int line = 0;
            if (Make_InsertInStatedef(tf.text, zss, -1, Make_CommandEntry(zss, c.name, state, when, ctrl), &line)) {
                tf.version++;
                g_codeFile = tf.path;
                g_codeLine = g_codeScrollTo = line;
                g_codeHold = true;
                Log(LogLine::Info, "A ChangeState to " + std::to_string(state) + " on \"" + c.name + "\" at the top of [Statedef -1] (" +
                                       tf.path + "); Apply to try it.");
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", file.empty() ? "(a new one in the CMD file)" : file.c_str());
        ImGui::TextDisabled("It goes first: entries higher up in [Statedef -1] win over those below.");
    }
}

void CommandsWindow() {
    UpdateInputs();
    if (!BeginTool("Commands")) return;
    const Entity *e = FindEntity(g_selId);
    if (!e) {
        ImGui::TextDisabled("Pick a player.");
        ImGui::End();
        return;
    }
    int pn = e->pn;
    const FrameEntity *fe = nullptr;
    for (const auto &f : g_frameEnts)
        if (!f.helper && f.pn == pn) fe = &f;
    float em = ImGui::GetFontSize();

    // The keys held now; under them on the left, what was held before
    ImGui::AlignTextToFramePadding();
    ImGui::Text("P%d input", pn);
    ImGui::SameLine();
    if (fe && fe->held.size() >= 14) HeldKeys(*fe);
    else ImGui::TextDisabled("needs the game's frames (patched Ikemen GO)");
    if (ImGui::BeginChild("history", ImVec2(em * 8, 0), ImGuiChildFlags_Borders)) {
        auto it = g_inputRuns.find(pn);
        if (it != g_inputRuns.end()) {
            for (const InputRun &r : it->second) {
                std::string name = InputName(r.keys);
                ImGui::TextDisabled("%5d", r.frames);
                ImGui::SameLine();
                if (name.empty()) ImGui::TextDisabled("-");
                else ImGui::TextUnformatted(name.c_str());
            }
        }
        if (ImGui::BeginPopupContextWindow()) {
            if (ImGui::MenuItem("Clear")) g_inputRuns.erase(pn);
            ImGui::EndPopup();
        }
    }
    ImGui::EndChild();
    ImGui::SetItemTooltip("What was held, for how many frames; newest first. Right click: clear.");
    ImGui::SameLine();

    // Right: the CMD file
    ImGui::BeginGroup();
    std::string path = CharFile(pn, "cmd");
    if (path.empty()) {
        AirPath(pn); // asks for the files
        ImGui::TextDisabled(g_canTrace ? "Waiting for the files of P%d..." : "Needs the patched Ikemen GO.", pn);
        ImGui::EndGroup();
        ImGui::End();
        return;
    }
    SourceFile &sf = LoadSource(path);
    const CmdFile &cf = CmdOf(sf);

    bool apply = ImGui::Button(sf.Dirty() ? "Save and apply (Ctrl+S)" : "Apply (Ctrl+S)");
    ImGuiIO &io = ImGui::GetIO();
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
        apply = true;
    if (apply) SaveAndApply();
    ImGui::SetItemTooltip("Saves the changed files, then the game compiles the characters again; a match goes on.");
    ImGui::SameLine();
    ImGui::BeginDisabled(!sf.Dirty());
    if (ImGui::Button("Revert")) ReadSource(sf);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%s%s  %zu commands%s", sf.path.c_str(), sf.Dirty() ? " *" : "", cf.commands.size(),
                        g_applying ? "  applying..." : "");
    if (!g_applyError.empty()) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextColored(ImVec4(1, 0.45f, 0.45f, 1), "%s", g_applyError.c_str());
        ImGui::PopTextWrapPos();
    }

    // The selected command, by name and index (the parse changes after every edit)
    static std::string selName;
    static int selIndex = -1;
    const CmdCommand *sel = nullptr;
    for (const auto &c : cf.commands)
        if (c.name == selName && c.index == selIndex) sel = &c;
    if (sel) {
        static std::string name, command, time, buffer, step, forText;
        static const CmdCommand *forSel;
        if (forText != sf.text || forSel != sel) {
            forText = sf.text, forSel = sel;
            name = sel->name, command = sel->command;
            auto field = [&](const char *key) {
                const CmdField *f = sel->Field(key);
                return f ? f->value : std::string();
            };
            time = field("time"), buffer = field("buffer.time"), step = field("steptime");
        }
        std::string text = sf.text;
        ImGui::SetNextItemWidth(em * 12);
        ImGui::InputText("##name", &name);
        if (ImGui::IsItemDeactivatedAfterEdit() && !name.empty() && name != sel->name) {
            CmdChanged(sf, Cmd_SetName(sf.text, *sel, name));
            selName = name;
            selIndex = 0;
            for (const auto &c : cf.commands) selIndex += c.name == name && c.line < sel->line;
        }
        ImGui::SetItemTooltip("name: what \"command = \" triggers in the states test.");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##command", "e.g. ~D, DF, F, x", &command);
        if (ImGui::IsItemDeactivatedAfterEdit() && command != sel->command)
            CmdChanged(sf, Cmd_SetField(sf.text, *sel, "command", command));
        ImGui::SetItemTooltip("~ release, / hold, $ any direction like it, > nothing else between, + together");
        struct Num {
            const char *label, *key;
            std::string *value;
            int deflt;
            const char *help;
        } nums[] = {
            {"time", "time", &time, cf.time, "Frames from the first step to the last."},
            {"buffer.time", "buffer.time", &buffer, cf.bufferTime, "Frames the command stays true once done."},
            {"steptime", "steptime", &step, 0, "Frames allowed between two steps (default: time)."},
        };
        for (int i = 0; i < 3; i++) {
            Num &n = nums[i];
            if (i) ImGui::SameLine();
            char hint[32];
            snprintf(hint, sizeof(hint), n.deflt ? "%d" : "default", n.deflt);
            ImGui::SetNextItemWidth(em * 4);
            ImGui::InputTextWithHint(n.label, hint, n.value, ImGuiInputTextFlags_CharsDecimal);
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                const CmdField *f = sel->Field(n.key);
                if (*n.value != (f ? f->value : std::string())) CmdChanged(sf, Cmd_SetField(sf.text, *sel, n.key, *n.value));
            }
            ImGui::SetItemTooltip("%s Empty: the default.", n.help);
        }
        // On the same line while there is room
        auto flow = [](const char *label) {
            ImGui::SameLine();
            if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2)
                ImGui::NewLine();
            return ImGui::Button(label);
        };
        if (sf.text == text) { // nothing changed above: sel still points into the parse
            if (flow("Duplicate")) {
                CmdChanged(sf, Cmd_Duplicate(sf.text, *sel));
                selIndex++;
            } else if (flow("Delete")) {
                CmdChanged(sf, Cmd_Delete(sf.text, *sel));
                selIndex = -1;
            } else if (flow("Show in Code")) {
                g_codeFile = sf.path;
                g_codeLine = g_codeScrollTo = sel->line;
                g_codeHold = true;
            }
        }
        if (sf.text == text) MotionEditor(sf, *sel, pn);
    }

    static std::string filter;
    static bool underWay;
    ImGui::SetNextItemWidth(em * 12);
    ImGui::InputTextWithHint("##filter", "name or keys", &filter);
    ImGui::SameLine();
    ImGui::Checkbox("Only the ones under way", &underWay);
    ImGui::SameLine();
    if (ImGui::Button("Add a command")) {
        if (Cmd_Add(sf.text, cf, sel, "new", "x")) {
            sf.version++;
            selName = "new";
            selIndex = 0;
            for (const auto &c : cf.commands) selIndex += c.name == "new" && sel && c.line <= sel->line;
        }
    }
    ImGui::SetItemTooltip("After the selected command (or before the first).");

    auto lower = [](std::string s) {
        for (char &ch : s) ch = (char)tolower((unsigned char)ch);
        return s;
    };
    std::string want = lower(filter);
    // At least some rows high: under the motion editor the window scrolls
    if (ImGui::BeginTable("commands", 6,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit |
                              ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable,
                          ImVec2(0, std::max(ImGui::GetContentRegionAvail().y, em * 14)))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Command", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Time");
        ImGui::TableSetupColumn("Buffer");
        ImGui::TableSetupColumn("Now", ImGuiTableColumnFlags_WidthFixed, em * 5);
        ImGui::TableSetupColumn("Done");
        ImGui::TableHeadersRow();
        double now = ImGui::GetTime();
        for (const CmdCommand &c : cf.commands) {
            const FrameEntity::Cmd *live = nullptr;
            if (fe)
                for (const auto &l : fe->cmds)
                    if (l.name == c.name && l.n == c.index) live = &l;
            if (underWay && !live) continue;
            if (!want.empty() && lower(c.name).find(want) == std::string::npos &&
                lower(c.command).find(want) == std::string::npos)
                continue;
            auto seen = g_cmdSeen.find(CmdSeenKey(pn, c.name, c.index));
            ImGui::PushID(c.line);
            ImGui::TableNextRow();
            // Buffered ("command = name" is true): green, fading for a second after
            float glow = seen == g_cmdSeen.end() ? 0 : (float)std::max(0.0, 1.0 - (now - seen->second.on));
            if (live && live->buffer > 0) glow = 1;
            if (glow > 0) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(60, 150, 80, (int)(40 + 120 * glow)));
            ImGui::TableNextColumn();
            if (ImGui::Selectable(c.name.c_str(), sel == &c, ImGuiSelectableFlags_SpanAllColumns)) {
                selName = c.name;
                selIndex = c.index;
            }
            if (ImGui::IsItemHovered() && c.index > 0) ImGui::SetTooltip("Definition %d of \"%s\"", c.index + 1, c.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(c.command.c_str());
            ImGui::TableNextColumn();
            if (c.Field("time")) ImGui::Text("%d", c.Int("time", cf.time));
            else ImGui::TextDisabled("%d", cf.time);
            ImGui::TableNextColumn();
            if (c.Field("buffer.time")) ImGui::Text("%d", c.Int("buffer.time", cf.bufferTime));
            else ImGui::TextDisabled("%d", cf.bufferTime);
            ImGui::TableNextColumn();
            if (live && live->buffer > 0) {
                ImGui::Text("true %d", live->buffer);
                ImGui::SetItemTooltip("\"command = %s\" is true for %d more frames", c.name.c_str(), live->buffer);
            } else if (live && live->steps > 0) {
                // A bar as high as the text, so that the row keeps its height
                char label[32];
                snprintf(label, sizeof(label), "%d/%d", live->done, live->steps);
                ImVec2 p = ImGui::GetCursorScreenPos();
                float w = ImGui::GetContentRegionAvail().x, h = ImGui::GetTextLineHeight();
                ImGui::Dummy(ImVec2(w, h));
                ImDrawList *draw = ImGui::GetWindowDrawList();
                draw->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(45, 45, 52, 255));
                draw->AddRectFilled(p, ImVec2(p.x + w * live->done / live->steps, p.y + h), IM_COL32(170, 130, 30, 255));
                draw->AddText(ImVec2(p.x + 3, p.y), IM_COL32_WHITE, label);
                ImGui::SetItemTooltip("Step %d of %d done, %d of %d frames", live->done, live->steps, live->time,
                                      live->maxtime);
            }
            ImGui::TableNextColumn();
            if (seen != g_cmdSeen.end() && seen->second.count) ImGui::Text("%d", seen->second.count);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndGroup();
    ImGui::End();
}

} // namespace app
