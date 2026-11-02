// Lab: a dummy (a player's keys held by the game: a stance, guarding), takes of a player's input
// recorded and played back, characters swapped in the middle of a match. The game does it (the
// mod's ikdbg.dummy, record, play, swap; debugInputHold and the like in editor_lab.go).
#include "app_internal.h"

#include <cfloat>

#include "imgui_internal.h"
#include "imgui_stdlib.h"

namespace app {

std::string KeysText(int bits) {
    static const char *dirs[16] = {"", "U", "D", "UD", "B", "UB", "DB", "UDB", "F", "UF", "DF", "UDF", "BF", "UBF", "DBF", "UDBF"};
    std::string s = dirs[bits & 15];
    const char *buttons = "abcxyzsdwm";
    std::string b;
    for (int i = 0; i < 10; i++)
        if (bits & (1 << (4 + i))) b += buttons[i];
    if (!b.empty()) s += (s.empty() ? "" : "+") + b;
    return s.empty() ? "-" : s;
}

namespace {

// A take: runs of key bits (U, D, B, F, a, b, c, x, y, z, s, d, w, m: bit 0 to 13) and ticks
struct Take {
    std::string name;
    std::string chr; // the def of the character it was recorded with ("" if not known)
    int pn = 1, seed = 0;
    std::vector<int> runs; // bits, ticks, bits, ticks...
    int Ticks() const {
        int n = 0;
        for (size_t i = 1; i < runs.size(); i += 2) n += runs[i];
        return n;
    }
};

std::vector<Take> g_takes;
int g_takeNo = 1;     // for the next take's name
int g_playing = -1;   // the take played last (index), to show where it is

struct Dummy {
    int pn = 0; // 0: none
    int stance = 0, guard = 0;
    bool life = true, power = false;
    int react = -1, after = 0; // the take it plays once it can act again (-1: none), and after what
    int chance = 50;           // of guarding each attack (guard "random"), in percent
    int reactChance = 100;     // of playing the take each time
} g_dummy;
const char *kStances[] = {"stand", "crouch", "jump"};
const char *kGuards[] = {"never", "always", "first", "random"};
const char *kAfter[] = {"guard", "hit", "wakeup", "any"};
bool g_onlyMatch = false; // only the takes of the characters in the match

struct LabState {
    int dummy = 0, rec = 0, recTicks = 0, play = 0, at = 0, len = 0;
    std::string waiting; // "rec" or "play": for the fight to start after a round reset
} g_lab;

int g_recPn = 1;
bool g_reset = true, g_sameRandom = true, g_loop = false;

std::string RunsList(const std::vector<int> &runs) {
    std::string s;
    for (int v : runs) s += (s.empty() ? "" : ",") + std::to_string(v);
    return s;
}

void SendDummy() {
    char lua[160];
    snprintf(lua, sizeof(lua), "ikdbg.dummy(%d, '%s', '%s', %s, %s, %d)", g_dummy.pn, kStances[g_dummy.stance],
             kGuards[g_dummy.guard], g_dummy.life ? "true" : "false", g_dummy.power ? "true" : "false",
             g_dummy.chance);
    SendRaw(lua);
    if (g_dummy.pn > 0 && g_dummy.react >= 0 && g_dummy.react < (int)g_takes.size())
        SendRaw("ikdbg.dummyReact({" + RunsList(g_takes[g_dummy.react].runs) + "}, '" + kAfter[g_dummy.after] +
                "', " + std::to_string(g_dummy.reactChance) + ")");
}

// Whether a take was recorded with a character in the match (or with one not known)
bool InMatch(const Take &t) {
    if (t.chr.empty()) return true;
    for (int pn = 1; pn <= 4; pn++) {
        std::string def = CharFile(pn, "def");
        if (!def.empty() && NormPath(def) == NormPath(t.chr)) return true;
    }
    return false;
}

// A take's runs, wrapped; the run being played is highlighted
void RunsView(const Take &t, int at) {
    float right = ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX();
    int tick = 0;
    for (size_t i = 0; i + 1 < t.runs.size(); i += 2) {
        char label[48];
        snprintf(label, sizeof(label), "%s %d", KeysText(t.runs[i]).c_str(), t.runs[i + 1]);
        float w = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2;
        if (i > 0) {
            ImGui::SameLine(0, 2);
            if (ImGui::GetCursorPosX() + w > right) ImGui::NewLine();
        }
        bool now = at >= tick && at < tick + t.runs[i + 1];
        bool none = t.runs[i] == 0;
        ImGui::PushStyleColor(ImGuiCol_Button, now    ? ImVec4(0.9f, 0.6f, 0.2f, 0.9f)
                                               : none ? ImVec4(0.3f, 0.3f, 0.3f, 0.5f)
                                                      : ImVec4(0.25f, 0.4f, 0.7f, 0.8f));
        ImGui::PushID((int)i);
        ImGui::SmallButton(label);
        ImGui::PopID();
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("Ticks %d to %d: %s held for %d", tick + 1, tick + t.runs[i + 1],
                              KeysText(t.runs[i]).c_str(), t.runs[i + 1]);
        tick += t.runs[i + 1];
    }
}

void DummySection() {
    bool live = Connected() && g_canTrace;
    bool changed = false;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5);
    changed |= ImGui::Combo("##dummy", &g_dummy.pn, "None\0P1\0P2\0P3\0P4\0");
    ImGui::SameLine();
    // Without a dummy there is nothing to set up
    if (g_dummy.pn == 0) {
        ImGui::TextDisabled("is the dummy: pick a player, and it stands, guards and reverses as you say");
        if (changed && live) SendDummy();
        if (!live) ImGui::TextDisabled(Connected() ? "Needs an Ikemen GO with the debugger patch." : "Launch the game.");
        return;
    }
    ImGui::TextDisabled("is the dummy:");
    ImGui::SameLine();
    for (int i = 0; i < 3; i++) {
        const char *names[] = {"Stands", "Crouches", "Jumps"};
        changed |= ImGui::RadioButton(names[i], &g_dummy.stance, i);
        ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::TextDisabled("Guards:");
    for (int i = 0; i < 4; i++) {
        const char *names[] = {"Never", "Always", "After a first hit", "Randomly"};
        ImGui::SameLine();
        changed |= ImGui::RadioButton(names[i], &g_dummy.guard, i);
    }
    if (g_dummy.guard == 3) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6);
        ImGui::SliderInt("##chance", &g_dummy.chance, 0, 100, "%d%% of attacks");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
    }
    ImGui::SameLine();
    HelpMarker("It holds back when an attack is near enough to guard (InGuardDist). After a first hit: the "
               "first hit of a string goes in, the dummy guards whatever comes once it can (a gap in a "
               "combo), until 90 ticks go by without a hit. Its AI is turned off.");
    changed |= ImGui::Checkbox("Keep its life full", &g_dummy.life);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("its power", &g_dummy.power);
    ImGui::SameLine();
    ImGui::TextDisabled("(once each combo is over)");
    // A reversal is a take played back: nothing to choose until there is one
    if (g_takes.empty()) {
        ImGui::TextDisabled("Record a take below and it can play it as a reversal.");
        if (changed && live) SendDummy();
        if (!live) ImGui::TextDisabled(Connected() ? "Needs an Ikemen GO with the debugger patch." : "Launch the game.");
        return;
    }
    ImGui::TextDisabled("Once it can act again after");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7);
    changed |= ImGui::Combo("##after", &g_dummy.after, "guarding\0a hit\0getting up\0any of them\0");
    ImGui::SameLine();
    ImGui::TextDisabled("it plays");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9);
    const char *current = g_dummy.react >= 0 && g_dummy.react < (int)g_takes.size() ? g_takes[g_dummy.react].name.c_str() : "nothing";
    if (ImGui::BeginCombo("##react", current)) {
        if (ImGui::Selectable("nothing", g_dummy.react < 0)) g_dummy.react = -1, changed = true;
        for (size_t n = 0; n < g_takes.size(); n++) {
            ImGui::PushID((int)n);
            if (ImGui::Selectable(g_takes[n].name.c_str(), g_dummy.react == (int)n)) g_dummy.react = (int)n, changed = true;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (g_dummy.react >= 0) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6);
        ImGui::SliderInt("##reactchance", &g_dummy.reactChance, 0, 100, "%d%% of times");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
    }
    ImGui::SameLine();
    HelpMarker("A reversal: the take starts so that its last tick is the first one the dummy can act on "
               "(the engine tells how long a hit or guard stun lasts, and getting up's animation). Record "
               "the move's keys ending on the button, e.g. F, D, DF+x for KFM's Kung Fu Upper.");
    if (changed && live) SendDummy();
    if (!live) ImGui::TextDisabled(Connected() ? "Needs an Ikemen GO with the debugger patch." : "Launch the game.");
}

void TakesSection() {
    bool live = Connected() && g_canTrace;
    ImGui::BeginDisabled(!live);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4);
    int idx = g_recPn - 1;
    if (ImGui::Combo("##recpn", &idx, "P1\0P2\0P3\0P4\0")) g_recPn = idx + 1;
    ImGui::SameLine();
    bool busy = g_lab.rec || g_lab.play || !g_lab.waiting.empty();
    if (ImGui::Button(ICON_RECORD " Record")) {
        SendRaw("ikdbg.record(" + std::to_string(g_recPn) + (g_reset ? ", true)" : ", false)"));
        g_playing = -1;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!busy);
    if (ImGui::Button(ICON_STOP " Stop")) SendRaw("ikdbg.stopTake()");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Checkbox("Reset the round first", &g_reset);
    ImGui::SameLine();
    ImGui::Checkbox("Same random numbers", &g_sameRandom);
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &g_loop);
    ImGui::SameLine();
    ImGui::Checkbox("Only of the characters in the match", &g_onlyMatch);
    ImGui::EndDisabled();
    if (!g_lab.waiting.empty())
        ImGui::TextColored(Ink(kYellow), "Waiting for the fight to start to %s...",
                           g_lab.waiting == "rec" ? "record" : "play");
    else if (g_lab.rec)
        ImGui::TextColored(Ink(kRed), "Recording P%d: %d ticks", g_lab.rec, g_lab.recTicks);
    else if (g_lab.play)
        ImGui::TextColored(Ink(kGreen), "Playing as P%d: tick %d of %d", g_lab.play, g_lab.at, g_lab.len);
    else
        ImGui::TextDisabled("Record a player's input (the keys it holds tick by tick, back and forward as it "
                            "faces), then play it back as any player.");

    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("takes", 5, flags, ImVec2(0, ImGui::GetFontSize() * 12))) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Take", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 8);
    ImGui::TableSetupColumn("Ticks");
    ImGui::TableSetupColumn("Keys", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Play as");
    ImGui::TableSetupColumn("");
    ImGui::TableHeadersRow();
    for (size_t n = 0; n < g_takes.size(); n++) {
        Take &t = g_takes[n];
        if (g_onlyMatch && !InMatch(t)) continue;
        ImGui::PushID((int)n);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##name", &t.name);
        ImGui::SetItemTooltip("Recorded as P%d%s%s", t.pn, t.chr.empty() ? "" : " with ", t.chr.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%d", t.Ticks());
        ImGui::TableNextColumn();
        RunsView(t, g_lab.play && g_playing == (int)n ? g_lab.at - 1 : -1);
        ImGui::TableNextColumn();
        ImGui::BeginDisabled(!live);
        for (int pn = 1; pn <= 2; pn++) {
            if (pn > 1) ImGui::SameLine();
            char label[8];
            snprintf(label, sizeof(label), "P%d", pn);
            if (ImGui::SmallButton(label)) {
                char head[96];
                snprintf(head, sizeof(head), "ikdbg.play(%d, {%s}, %s, ", pn, RunsList(t.runs).c_str(),
                         g_loop ? "true" : "false");
                SendRaw(head + (g_sameRandom ? std::to_string(t.seed) : std::string("nil")) +
                        (g_reset ? ", true)" : ", false)"));
                g_playing = (int)n;
            }
            ImGui::SetItemTooltip("Play it as P%d%s", pn, pn == t.pn ? " (recorded as)" : "");
        }
        ImGui::EndDisabled();
        ImGui::TableNextColumn();
        bool del = ImGui::SmallButton(ICON_XMARK);
        ImGui::SetItemTooltip("Delete this take");
        if (del) {
            g_takes.erase(g_takes.begin() + n);
            if (g_playing == (int)n) g_playing = -1;
            if (g_dummy.react == (int)n) {
                g_dummy.react = -1;
                if (live) SendRaw("ikdbg.dummyReact(nil)");
            } else if (g_dummy.react > (int)n)
                g_dummy.react--;
            ImGui::PopID();
            break;
        }
        ImGui::SetItemTooltip("Delete it");
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void CharactersSection() {
    static std::vector<std::string> list;
    static std::string filter, picked;
    static bool listed;
    if (!listed || ImGui::SmallButton(ICON_RESTART " Look again")) list = ListDefs(false), listed = true;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
    ImGui::InputTextWithHint("##filter", "filter", &filter);
    ImGui::SameLine();
    bool live = Connected() && g_canTrace;
    ImGui::BeginDisabled(!live || picked.empty());
    for (int pn = 1; pn <= 2; pn++) {
        char label[32];
        snprintf(label, sizeof(label), "In P%d's place", pn);
        if (pn > 1) ImGui::SameLine();
        if (ImGui::Button(label)) SwapChar(pn, picked);
    }
    ImGui::EndDisabled();
    if (ImGui::BeginChild("chars", ImVec2(0, ImGui::GetFontSize() * 8), ImGuiChildFlags_Borders)) {
        for (const std::string &s : list) {
            std::string low = s, f = filter;
            std::transform(low.begin(), low.end(), low.begin(), ::tolower);
            std::transform(f.begin(), f.end(), f.begin(), ::tolower);
            if (!f.empty() && low.find(f) == std::string::npos) continue;
            if (ImGui::Selectable(s.c_str(), picked == s, ImGuiSelectableFlags_AllowDoubleClick)) picked = s;
        }
    }
    ImGui::EndChild();
}

} // namespace

void SwapChar(int pn, const std::string &name) {
    Exec("ikdbg.swap(" + std::to_string(pn) + ", " + LuaQuote(name) + ")");
    g_charFiles.erase(pn);
    g_filesAsked.erase(pn);
    g_swapWanted[pn] = {name, ImGui::GetTime()};
    Log(LogLine::Info, "P" + std::to_string(pn) + ": " + name + " (the match starts again)");
}

void OnLabState(const Json &lab) {
    g_lab = LabState();
    if (lab.type != Json::Object) return;
    g_lab.dummy = lab["dummy"].i();
    g_lab.rec = lab["rec"].i();
    g_lab.recTicks = lab["recTicks"].i();
    g_lab.play = lab["play"].i();
    g_lab.at = lab["at"].i();
    g_lab.len = lab["len"].i();
    g_lab.waiting = lab["waiting"].str();
}

void OnTake(const Json &j) {
    Take t;
    t.pn = j["pn"].i();
    t.seed = j["seed"].i();
    t.chr = j["char"].str();
    for (const auto &v : j["runs"].items) t.runs.push_back(v.i());
    if (t.runs.empty()) {
        Log(LogLine::Info, "The take recorded nothing.");
        return;
    }
    t.name = "Take " + std::to_string(g_takeNo++);
    g_takes.push_back(t);
    Log(LogLine::Info, t.name + ": P" + std::to_string(t.pn) + ", " + std::to_string(t.Ticks()) + " ticks");
}

void LabSettingsLine(const std::string &key, const std::string &value) {
    if (key == "Take") {
        // pn|seed|runs|name
        Take t;
        size_t a = value.find('|'), b = a == std::string::npos ? a : value.find('|', a + 1),
               c = b == std::string::npos ? b : value.find('|', b + 1);
        if (c == std::string::npos) return;
        t.pn = atoi(value.c_str());
        t.seed = atoi(value.c_str() + a + 1);
        std::string runs = value.substr(b + 1, c - b - 1);
        for (size_t p = 0; p < runs.size();) {
            t.runs.push_back(atoi(runs.c_str() + p));
            size_t comma = runs.find(',', p);
            if (comma == std::string::npos) break;
            p = comma + 1;
        }
        t.name = value.substr(c + 1);
        g_takes.push_back(t);
        g_takeNo++;
    } else if (key == "TakeOf") {
        if (!g_takes.empty()) g_takes.back().chr = value; // of the Take line before
    } else if (key == "DummyReact") {
        // after|take index|chance
        sscanf(value.c_str(), "%d|%d|%d", &g_dummy.after, &g_dummy.react, &g_dummy.reactChance);
        g_dummy.after = std::clamp(g_dummy.after, 0, 3);
        g_dummy.reactChance = std::clamp(g_dummy.reactChance, 0, 100);
    } else if (key == "Dummy") {
        // pn|stance|guard|life|power|chance
        int life = 1, power = 0;
        sscanf(value.c_str(), "%d|%d|%d|%d|%d|%d", &g_dummy.pn, &g_dummy.stance, &g_dummy.guard, &life, &power,
               &g_dummy.chance);
        g_dummy.stance = std::clamp(g_dummy.stance, 0, 2);
        g_dummy.guard = std::clamp(g_dummy.guard, 0, 3);
        g_dummy.chance = std::clamp(g_dummy.chance, 0, 100);
        g_dummy.life = life != 0, g_dummy.power = power != 0;
    } else if (key == "LabOptions") {
        int reset = 1, same = 1, loop = 0, only = 0;
        sscanf(value.c_str(), "%d|%d|%d|%d|%d", &g_recPn, &reset, &same, &loop, &only);
        g_reset = reset != 0, g_sameRandom = same != 0, g_loop = loop != 0, g_onlyMatch = only != 0;
    }
}

void LabSettingsWrite(ImGuiTextBuffer *buf) {
    buf->appendf("Dummy=%d|%d|%d|%d|%d|%d\n", g_dummy.pn, g_dummy.stance, g_dummy.guard, g_dummy.life, g_dummy.power,
                 g_dummy.chance);
    buf->appendf("DummyReact=%d|%d|%d\n", g_dummy.after, g_dummy.react, g_dummy.reactChance);
    buf->appendf("LabOptions=%d|%d|%d|%d|%d\n", g_recPn, g_reset, g_sameRandom, g_loop, g_onlyMatch);
    for (const Take &t : g_takes) {
        buf->appendf("Take=%d|%d|%s|%s\n", t.pn, t.seed, RunsList(t.runs).c_str(), t.name.c_str());
        if (!t.chr.empty()) buf->appendf("TakeOf=%s\n", t.chr.c_str());
    }
}

void LabHello() {
    g_lab = LabState();
    if (g_dummy.pn > 0 && g_canTrace) SendDummy();
}

void LabWindow() {
    if (!BeginTool("Lab")) return;
    ImGui::SeparatorText("Dummy");
    DummySection();
    ImGui::SeparatorText("Takes");
    TakesSection();
    ImGui::SeparatorText("Characters");
    CharactersSection();
    ImGui::End();
}

} // namespace app
