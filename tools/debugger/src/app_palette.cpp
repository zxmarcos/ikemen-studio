// The palette: one box at the top of the window for going anywhere. Its first character says what
// it lists: none, the projects' files (Ctrl+P); '>', the commands (Ctrl+Shift+P); ':', a line of
// the file shown in Code (Ctrl+G); '@', that file's sections, as [Statedef 200] or
// [Begin Action 0] (Ctrl+Shift+O). Typing filters them by letters in order: "kfmcns" finds
// chars/kfm/kfm.cns. Also the menu bar's File menu.
#include "app_internal.h"

#include <cfloat>
#include <cstring>

#include "imgui_internal.h"
#include "imgui_stdlib.h"

namespace app {

std::string IkemenDir(); // app_code.cpp

namespace {

struct Entry {
    std::string label, detail; // detail: dimmed after it (a file's folder, a command's keys)
    std::function<void()> run;
    size_t nameAt = 0;         // where the label's text matched starts its last part (a file's name)
    std::string match;         // what the filter matches (the label when empty)
    int score = 0;
};

bool g_ask;           // open it on the next frame, with g_text
std::string g_text;
std::vector<Entry> g_entries; // what its mode lists, before the filter
char g_mode = -1;             // of g_entries
int g_sel;

void Ask(char mode) {
    g_text = mode ? std::string(1, mode) : "";
    g_ask = true;
}

// The letters of pat in text in order: -1 when not. More for letters in a row, at a word's start,
// in the last part of the text (from nameAt).
int Fuzzy(const std::string &pat, const std::string &text, size_t nameAt) {
    int score = 0;
    size_t t = 0;
    bool inRow = false;
    for (char pc : pat) {
        if (pc == ' ') continue;
        char p = (char)tolower((unsigned char)pc);
        bool found = false;
        for (; t < text.size(); t++) {
            if ((char)tolower((unsigned char)text[t]) != p) {
                inRow = false;
                continue;
            }
            score += 1 + (inRow ? 5 : 0) + (t >= nameAt ? 2 : 0);
            if (t == 0 || t == nameAt || strchr("/\\_-. [", text[t - 1])) score += 8;
            inRow = found = true;
            t++;
            break;
        }
        if (!found) return -1;
    }
    return score;
}

void GoToLine(int line) {
    if (line > 0 && !g_codeFile.empty()) CodeShowLine(g_codeFile, line);
}

// A path inside the game's folder as the game names it
std::string GameRelative(const std::string &path) {
    std::string dir = NormPath(IkemenDir()) + "/";
    std::string p = path;
    for (char &ch : p)
        if (ch == '\\') ch = '/';
    return NormPath(p).compare(0, dir.size(), dir) == 0 ? p.substr(dir.size()) : p;
}

void OpenDialog() {
    auto files = PickFiles("Ikemen GO files\0*.def;*.cns;*.st;*.cmd;*.air;*.zss;*.sff;*.lua;*.txt\0All files\0*.*\0", false);
    if (!files.empty()) OpenPath(GameRelative(files[0]), true);
}

void Files(std::vector<Entry> &out) {
    std::vector<std::string> files;
    ProjectFiles(files);
    for (const std::string &p : files) {
        size_t slash = p.find_last_of("\\/");
        size_t name = slash == std::string::npos ? 0 : slash + 1;
        Entry e;
        e.label = p.substr(name);
        e.detail = p.substr(0, name);
        e.match = p;
        e.nameAt = name;
        e.run = [p] { OpenPath(p, true); };
        out.push_back(e);
    }
}

void Sections(std::vector<Entry> &out) {
    if (g_codeFile.empty()) return;
    const std::string &text = LoadSource(g_codeFile).text;
    int line = 1;
    for (size_t at = 0; at < text.size(); line++) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        size_t b = text.find_first_not_of(" \t", at);
        if (b < end && text[b] == '[') {
            size_t close = text.find(']', b);
            Entry e;
            e.label = text.substr(b, (close < end ? close + 1 : end) - b);
            e.detail = "line " + std::to_string(line);
            e.run = [line] { GoToLine(line); };
            out.push_back(e);
        }
        at = end + 1;
    }
}

void Commands(std::vector<Entry> &out) {
    auto add = [&](const std::string &label, const char *keys, std::function<void()> run) {
        Entry e;
        e.label = label;
        e.detail = keys;
        e.run = std::move(run);
        out.push_back(e);
    };
    bool running = g_bridge.Running(), live = Connected();
    add(running ? "Game: Restart" : "Game: Launch", "", [] { Launch(); });
    if (running)
        add("Game: Stop", "", [] {
            g_bridge.Stop();
            Log(LogLine::Info, "Stopped the game.");
        });
    if (live) {
        add(g_paused ? "Game: Continue" : "Game: Pause", "F5",
            [] { SendRaw(g_paused ? "ikdbg.pause(false)" : "ikdbg.pause(true)"); });
        add("Game: Step a tick", "F10", [] { SendRaw("ikdbg.step(1)"); });
        add("Game: Step ten ticks", "F11", [] { SendRaw("ikdbg.step(10)"); });
        add("Game: Save state", "", [] { Exec("saveState()"); });
        add("Game: Load state", "", [] { Exec("loadState()"); });
        add("Game: Reset round", "", [] { Exec("resetRound()"); });
        add("Game: Reload match", "", [] { Exec("reload()"); });
    }
    add("File: Open...", "", OpenDialog);
    add("File: Save and apply", "Ctrl+S", SaveAndApply);
    add("File: Close the tab", "Ctrl+W", CodeCloseShown);
    if (!PackageTarget().empty()) add("File: Package as zip...", "", PackageWorkedOn);
    add("Help: Keyboard shortcuts", "", [] { HelpAsk(0); });
    add("Help: Words used here", "", [] { HelpAsk(1); });
    add("New: A move", "", [] { CreateAsk(kNewMove); });
    add("New: A character", "", [] { CreateAsk(kNewCharacter); });
    add("New: A stage", "", [] { CreateAsk(kNewStage); });
    add("Go to file", "Ctrl+P", [] { Ask(0); });
    add("Go to line", "Ctrl+G", [] { Ask(':'); });
    add("Go to section", "Ctrl+Shift+O", [] { Ask('@'); });
    add("Find in files", "Ctrl+Shift+F", [] { SearchFor(CodeSelectedText()); });    for (int n = 0; WorkspaceName(n); n++) {
        char keys[16];
        snprintf(keys, sizeof(keys), "Ctrl+%d", n + 1);
        add(std::string("Workspace: ") + WorkspaceName(n), keys, [n] { AskWorkspace(n); });
    }
    add("Workspace: Restore its arrangement", "", [] { AskWorkspace(-1); });
    add(AdvancedTools() ? "View: Hide the advanced tools" : "View: Show the advanced tools", "",
        [] { SetAdvancedTools(!AdvancedTools()); });
    add("View: Start page", "", AskStartPage);
    add("View: Getting started", "", AskTour);
    for (int n = 0; ToolName(n); n++) add(std::string("Window: ") + ToolName(n), "", [n] { ShowWindow(ToolName(n)); });
    for (int n = 0; ThemeName(n); n++) add(std::string("Theme: ") + ThemeName(n), "", [n] { AskTheme(n); });
}

} // namespace

void FileMenu() {
    if (ImGui::MenuItem(ICON_SEARCH " Go to file...", "Ctrl+P")) Ask(0);
    if (ImGui::MenuItem("Commands...", "Ctrl+Shift+P")) Ask('>');
    ImGui::BeginDisabled(g_codeFile.empty());
    if (ImGui::MenuItem("Go to line...", "Ctrl+G")) Ask(':');
    if (ImGui::MenuItem("Go to section...", "Ctrl+Shift+O")) Ask('@');
    ImGui::EndDisabled();
    if (ImGui::MenuItem("Find in files...", "Ctrl+Shift+F")) SearchFor(CodeSelectedText());
    ImGui::Separator();
    if (ImGui::MenuItem(ICON_FOLDER_OPEN " Open...")) OpenDialog();
    if (ImGui::MenuItem(ICON_SAVE " Save and apply", "Ctrl+S")) SaveAndApply();
    ImGui::BeginDisabled(g_codeFile.empty());
    if (ImGui::MenuItem("Close the tab", "Ctrl+W")) CodeCloseShown();
    ImGui::EndDisabled();
    ImGui::Separator();
    std::string target = PackageTarget();
    target = target.empty() ? "Package..." : "Package " + target.substr(target.find_last_of("\\/") + 1) + "...";
    ImGui::BeginDisabled(PackageTarget().empty());
    if (ImGui::MenuItem((ICON_DOWNLOAD " " + target).c_str())) PackageWorkedOn();
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("A zip of what you are working on and every file it names, to share it");
}

void PaletteFrame() {
    ImGuiIO &io = ImGui::GetIO();
    // Opening it would close a dialog
    if (io.KeyCtrl && !io.KeyAlt && !ImGui::GetTopMostPopupModal()) {
        if (ImGui::IsKeyPressed(ImGuiKey_P, false)) Ask(io.KeyShift ? '>' : 0);
        else if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_G, false)) Ask(':');
        else if (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O, false)) Ask('@');
        else if (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_F, false)) SearchFor(CodeSelectedText());
    }
    bool appearing = false;
    if (g_ask) {
        g_ask = false;
        g_mode = -1;
        appearing = true;
        ImGui::OpenPopup("##palette");
    }
    const ImGuiViewport *vp = ImGui::GetMainViewport();
    float em = ImGui::GetFontSize();
    float width = std::min(em * 44, vp->WorkSize.x - em * 2);
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + em * 2.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0));
    ImGui::SetNextWindowSize(ImVec2(width, 0));
    if (!ImGui::BeginPopup("##palette")) return;

    if (appearing) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-FLT_MIN);
    bool enter = ImGui::InputTextWithHint("##text", "file name;  > command;  : line;  @ section", &g_text,
                                          ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemActive())
        // The arrows move in the list below, not the keyboard navigation
        for (ImGuiKey k : {ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_PageUp, ImGuiKey_PageDown})
            ImGui::SetKeyOwner(k, ImGui::GetItemID());
    if (ImGui::IsItemActivated()) {
        // The cursor after the mode's character, not the whole text selected
        if (ImGuiInputTextState *s = ImGui::GetInputTextState(ImGui::GetItemID())) s->ClearSelection();
    }
    char mode = g_text.empty() || !strchr(">:@", g_text[0]) ? 0 : g_text[0];
    std::string pat = mode ? g_text.substr(1) : g_text;
    if (mode != g_mode) {
        g_entries.clear();
        if (mode == 0) Files(g_entries);
        else if (mode == '>') Commands(g_entries);
        else if (mode == '@') Sections(g_entries);
        g_mode = mode;
        g_sel = 0;
    }

    // The entries passing the filter, best first
    std::vector<const Entry *> shown;
    for (Entry &e : g_entries) {
        const std::string &m = e.match.empty() ? e.label : e.match;
        e.score = pat.empty() ? 0 : Fuzzy(pat, m, e.nameAt);
        if (e.score >= 0) shown.push_back(&e);
    }
    if (!pat.empty())
        std::stable_sort(shown.begin(), shown.end(), [](const Entry *a, const Entry *b) { return a->score > b->score; });
    if (shown.size() > 200) shown.resize(200);
    static std::string lastPat;
    if (pat != lastPat) g_sel = 0, lastPat = pat;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) g_sel++;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) g_sel--;
    if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) g_sel += 10;
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) g_sel -= 10;
    g_sel = shown.empty() ? 0 : std::clamp(g_sel, 0, (int)shown.size() - 1);

    std::function<void()> run;
    if (mode == ':') {
        int line = atoi(pat.c_str());
        std::string file;
        int at, col;
        if (!CodeCursor(&file, &at, &col)) ImGui::TextDisabled("No file in Code.");
        else if (line > 0) ImGui::Text("Go to line %d of %s", line, file.c_str());
        else ImGui::TextDisabled("A line of %s (now at %d)", file.c_str(), at);
        if (enter && line > 0 && !file.empty()) run = [line] { GoToLine(line); };
    } else if (shown.empty()) {
        ImGui::TextDisabled(mode == '@' && g_codeFile.empty() ? "No file in Code." : "Nothing matches.");
    } else {
        float rows = std::min((float)shown.size(), 16.0f);
        ImGui::BeginChild("list", ImVec2(0, rows * ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y),
                          ImGuiChildFlags_None, ImGuiWindowFlags_NoNav);
        ImGuiListClipper clipper;
        clipper.Begin((int)shown.size());
        clipper.IncludeItemByIndex(g_sel);
        while (clipper.Step())
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
                const Entry &e = *shown[i];
                ImGui::PushID(i);
                if (ImGui::Selectable(e.label.c_str(), i == g_sel)) run = e.run;
                if (i == g_sel && (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow) ||
                                   ImGui::IsKeyPressed(ImGuiKey_PageDown) || ImGui::IsKeyPressed(ImGuiKey_PageUp)))
                    ImGui::SetScrollHereY();
                if (!e.detail.empty()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", e.detail.c_str());
                }
                ImGui::PopID();
            }
        ImGui::EndChild();
        if (enter) run = shown[g_sel]->run;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    if (run) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    if (run) run();
}

} // namespace app
