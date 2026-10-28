// Code: the source of the selected entity's state, and the controllers that ran.
#include "app_internal.h"

#include <fstream>

#include "cns.h"

#include "imgui_internal.h"
#include "imgui_stdlib.h"

namespace app {

std::string IkemenDir() {
    const std::string &exe = g_settings.exe;
    size_t slash = exe.find_last_of("\\/");
    return slash == std::string::npos ? "." : exe.substr(0, slash);
}

std::string GamePath(const std::string &file) {
    bool absolute = file.size() > 1 && (file[1] == ':' || file[0] == '\\' || file[0] == '/');
    return absolute ? file : IkemenDir() + "\\" + file;
}

FILETIME FileTime(const std::string &path) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data)) return FILETIME{};
    return data.ftLastWriteTime;
}

// Reads the file into sf.text and sf.disk ('\n' line ends, no BOM).
void ReadSource(SourceFile &sf) {
    std::string raw = ReadWholeFile(GamePath(sf.path));
    sf.mtime = FileTime(GamePath(sf.path));
    sf.missing = raw.empty() && sf.mtime.dwLowDateTime == 0 && sf.mtime.dwHighDateTime == 0;
    sf.bom = raw.size() >= 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB &&
             (unsigned char)raw[2] == 0xBF;
    if (sf.bom) raw.erase(0, 3);
    sf.crlf = raw.find("\r\n") != std::string::npos;
    std::string text;
    text.reserve(raw.size());
    for (char ch : raw)
        if (ch != '\r') text += ch;
    sf.text = sf.disk = text;
    sf.version++;
}

SourceFile &LoadSource(const std::string &file) {
    std::string key = NormPath(file);
    auto it = g_files.find(key);
    if (it != g_files.end()) return it->second;
    SourceFile &sf = g_files[key];
    sf.path = file;
    sf.ed = dbg::Editor_Create();
    dbg::Editor_SetZss(sf.ed, Cns_IsZss(file));
    dbg::Editor_SetHoverNote(sf.ed, [path = sf.path](size_t b, size_t e) { return CodeHoverNote(path, b, e); });
    ReadSource(sf);
    return sf;
}

bool SaveSource(SourceFile &sf) {
    std::string out = sf.bom ? "\xEF\xBB\xBF" : "";
    for (char ch : sf.text) {
        if (ch == '\n' && sf.crlf) out += '\r';
        out += ch;
    }
    std::ofstream f(GamePath(sf.path), std::ios::binary);
    f << out;
    f.close();
    if (!f) {
        Log(LogLine::Error, "Could not write " + GamePath(sf.path));
        return false;
    }
    sf.disk = sf.text;
    sf.mtime = FileTime(GamePath(sf.path));
    return true;
}

// Files changed on disk by something else are read again, unless they have unsaved changes.
void WatchSources() {
    static double last;
    if (ImGui::GetTime() - last < 1.0) return;
    last = ImGui::GetTime();
    for (auto &kv : g_files) {
        SourceFile &sf = kv.second;
        FILETIME now = FileTime(GamePath(sf.path));
        if (CompareFileTime(&now, &sf.mtime) != 0 && !sf.Dirty()) ReadSource(sf);
    }
}

// Saves every changed file and has the game load and compile the characters again.
bool g_applying;
std::string g_applyError;

void SaveAndApply() {
    int saved = 0;
    for (auto &kv : g_files)
        if (kv.second.Dirty()) {
            if (!SaveSource(kv.second)) return;
            saved++;
        }
    for (auto &kv : g_files) {
        kv.second.errorLine = 0;
        dbg::Editor_SetErrorLine(kv.second.ed, 0, nullptr);
    }
    g_applyError.clear();
    if (!Connected()) {
        Log(LogLine::Info, saved ? "Saved; the game is not running." : "Nothing to save.");
        return;
    }
    g_applying = true;
    SendRaw("ikdbg.reload()");
}

// The game's answer to ikdbg.reload(): {items: [{pn, ok, err}]}.
void OnReload(const Json &j) {
    g_applying = false;
    std::string applied;
    for (const auto &item : j["items"].items) {
        int pn = item["pn"].i();
        if (item["ok"].truthy()) {
            applied += (applied.empty() ? "P" : ", P") + std::to_string(pn);
            continue;
        }
        // "path:line:\nmessage" from the compiler
        std::string err = item["err"].str();
        g_applyError = "P" + std::to_string(pn) + ": " + err;
        Log(LogLine::Error, "Not applied, the game keeps the previous version. " + g_applyError);
        size_t nl = err.find('\n');
        std::string head = err.substr(0, nl);
        size_t c2 = head.find_last_of(':');
        size_t c1 = c2 == std::string::npos || c2 == 0 ? std::string::npos : head.find_last_of(':', c2 - 1);
        if (c1 != std::string::npos && c1 > 1) {
            std::string file = head.substr(0, c1);
            int line = atoi(head.c_str() + c1 + 1);
            if (line > 0) {
                SourceFile &sf = LoadSource(file);
                sf.errorLine = line;
                sf.errorMsg = nl == std::string::npos ? err : err.substr(nl + 1);
                dbg::Editor_SetErrorLine(sf.ed, line, sf.errorMsg.c_str());
                g_codeFile = file;
                g_codeLine = 0;
                g_codeScrollTo = line;
            }
        }
        return;
    }
    if (!applied.empty()) Log(LogLine::Result, "Applied to " + applied + ".");
}

enum SourceKind { kControllers, kTriggers, kStatedefs };

// The sources at each line of a file, of one kind (rebuilt when sources arrive).
const std::map<int, std::vector<int>> &SourceLines(const std::string &file, SourceKind kind) {
    static std::map<std::string, std::map<int, std::vector<int>>> byFile[3];
    static size_t built;
    if (built != g_sources.size()) {
        for (auto &m : byFile) m.clear();
        for (size_t i = 1; i < g_sources.size(); i++) {
            const Source &s = g_sources[i];
            SourceKind k = s.type == "statedef" ? kStatedefs : s.type == "trigger" ? kTriggers : kControllers;
            byFile[k][NormPath(s.file)][s.line].push_back((int)i);
        }
        built = g_sources.size();
    }
    static const std::map<int, std::vector<int>> none;
    auto it = byFile[kind].find(NormPath(file));
    return it == byFile[kind].end() ? none : it->second;
}

// The tick each controller line of a file last ran at, up to tick upTo, in an entity's history.
const std::map<int, int> &LastRan(int id, const std::string &normFile, int upTo) {
    static std::map<int, int> last;
    static std::string forFile;
    static int forId = -1, forTick = -1;
    static size_t forSources;
    if (id == forId && upTo == forTick && normFile == forFile && g_sources.size() == forSources) return last;
    forId = id, forTick = upTo, forFile = normFile, forSources = g_sources.size();
    last.clear();
    auto hist = g_history.find(id);
    if (hist == g_history.end()) return last;
    for (const FrameRow &r : hist->second) {
        if (r.t > upTo) break;
        for (int src : r.ran)
            if (src > 0 && src < (int)g_sources.size() && NormPath(g_sources[src].file) == normFile)
                last[g_sources[src].line] = r.t;
    }
    return last;
}

std::string StateBreakCond(int state) { return "stateNo() == " + std::to_string(state); }

Breakpoint *FindStateBreak(int state) {
    std::string cond = StateBreakCond(state);
    for (auto &b : g_breakpoints)
        if (b.cond == cond) return &b;
    return nullptr;
}

// A condition breakpoint on entering a state (of player pn and its helpers; 0: anyone), or off again.
void ToggleStateBreak(int state, int pn) {
    for (size_t n = 0; n < g_breakpoints.size(); n++)
        if (g_breakpoints[n].cond == StateBreakCond(state)) {
            if (Connected()) SendRaw("ikdbg.brkDel(" + std::to_string(g_breakpoints[n].bid) + ")");
            g_breakpoints.erase(g_breakpoints.begin() + n);
            return;
        }
    Breakpoint b{g_nextBid++, pn > 0 ? -pn : 0, StateBreakCond(state)};
    g_breakpoints.push_back(b);
    if (Connected()) SendBreakpoint(b);
    Log(LogLine::Info, "Breakpoint on entering state " + std::to_string(state) +
                           (pn > 0 ? " (P" + std::to_string(pn) + " and its helpers)" : ""));
}

void JumpTo(int src) {
    if (src <= 0 || src >= (int)g_sources.size()) return;
    g_codeFile = g_sources[src].file;
    g_codeLine = g_codeScrollTo = g_sources[src].line;
}

bool g_codeFollow = true; // Code follows the selected entity's state

void CodeShow(const std::string &path) {
    if (path.empty()) {
        g_codeFollow = true;
        return;
    }
    g_codeFollow = false;
    if (g_codeFile != path) g_codeFile = path, g_codeLine = 0;
}

// ---------------------------------------------------------------------------
// The files open, as tabs. The file shown goes in a tab; the preview one (dimmed) is replaced by
// the next file shown, unless it was kept: double-clicked, edited, or opened to be kept.
// ---------------------------------------------------------------------------

struct OpenDoc {
    std::string path;
    bool preview;
};
std::vector<OpenDoc> g_docs;
int g_closing = -1; // the tab asked to close with changes: Save, Don't save, Cancel

int FindDoc(const std::string &path) {
    for (size_t i = 0; i < g_docs.size(); i++)
        if (NormPath(g_docs[i].path) == NormPath(path)) return (int)i;
    return -1;
}

void TabFor(const std::string &path, bool keep) {
    if (int i = FindDoc(path); i >= 0) {
        g_docs[i].preview = g_docs[i].preview && !keep;
        return;
    }
    for (OpenDoc &d : g_docs)
        if (d.preview && !LoadSource(d.path).Dirty()) {
            d = {path, !keep};
            return;
        }
    g_docs.push_back({path, !keep});
}

bool g_codeFocus; // the editor takes the keyboard at its next draw

void CodeOpen(const std::string &path, bool keep) {
    g_codeFollow = false;
    if (NormPath(g_codeFile) != NormPath(path)) g_codeFile = path, g_codeLine = 0;
    TabFor(path, keep);
    ShowWindow("Code");
    // Opened to be kept (Go to file, a double click): typed into at once
    g_codeFocus |= keep;
}

void CloseDoc(int i) {
    bool shown = NormPath(g_docs[i].path) == NormPath(g_codeFile);
    g_docs.erase(g_docs.begin() + i);
    if (!shown) return;
    // The one next to it is shown; following the state would open the file again
    g_codeFollow = false;
    g_codeLine = 0;
    g_codeFile = g_docs.empty() ? "" : g_docs[std::min(i, (int)g_docs.size() - 1)].path;
}

void AskClose(int i) {
    if (LoadSource(g_docs[i].path).Dirty()) g_closing = i;
    else CloseDoc(i);
}

void CodeCloseShown() {
    if (int i = FindDoc(g_codeFile); i >= 0) AskClose(i);
}

bool CodeCursor(std::string *file, int *line, int *col) {
    if (g_codeFile.empty()) return false;
    SourceFile &sf = LoadSource(g_codeFile);
    *file = sf.path;
    *line = dbg::Editor_CursorLine(sf.ed);
    size_t at = std::min(dbg::Editor_CursorOffset(sf.ed), sf.text.size());
    size_t nl = at ? sf.text.rfind('\n', at - 1) : std::string::npos;
    *col = (int)(at - (nl == std::string::npos ? 0 : nl + 1)) + 1;
    return true;
}

// The tabs; a tab clicked shows its file
void DocTabs(bool &follow) {
    static std::string selected; // the file the tab bar was last told to show
    std::string want = NormPath(g_codeFile);
    bool select = want != selected;
    selected = want;
    int closing = -1;
    if (!ImGui::BeginTabBar("docs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll |
                                        ImGuiTabBarFlags_TabListPopupButton))
        return;
    for (size_t i = 0; i < g_docs.size(); i++) {
        OpenDoc &d = g_docs[i];
        SourceFile &sf = LoadSource(d.path);
        if (sf.Dirty()) d.preview = false;
        bool mine = NormPath(d.path) == want;
        ImGuiTabItemFlags flags = sf.Dirty() ? ImGuiTabItemFlags_UnsavedDocument : 0;
        if (select && mine) flags |= ImGuiTabItemFlags_SetSelected;
        std::string name = d.path.substr(d.path.find_last_of("\\/") + 1);
        std::string label = name + "###" + NormPath(d.path);
        if (d.preview) {
            // Halfway to the dimmed text: a preview, still readable on a selected tab
            ImVec4 a = ImGui::GetStyleColorVec4(ImGuiCol_Text), b = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4((a.x + b.x) / 2, (a.y + b.y) / 2, (a.z + b.z) / 2, a.w));
        }
        bool open = true;
        bool shown = ImGui::BeginTabItem(label.c_str(), &open, flags);
        if (d.preview) ImGui::PopStyleColor();
        ImGui::SetItemTooltip("%s%s", d.path.c_str(), d.preview ? "\nPreview: the next file shown takes its place; "
                                                                  "double-click to keep it" : "");
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) d.preview = false;
        if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) open = false;
        if (shown) {
            // Picked by a click (the bar shows another than the one asked for, and none was asked)
            if (!mine && !select) {
                g_codeFile = d.path;
                g_codeLine = 0;
                follow = false;
                selected = NormPath(d.path);
            }
            ImGui::EndTabItem();
        }
        if (!open) closing = (int)i;
    }
    ImGui::EndTabBar();
    if (closing >= 0) AskClose(closing);
}

// Asks what to do with the changes of a tab being closed
void CloseDialog() {
    if (g_closing >= (int)g_docs.size()) g_closing = -1;
    if (g_closing >= 0 && !ImGui::IsPopupOpen("Close")) ImGui::OpenPopup("Close");
    if (!ImGui::BeginPopupModal("Close", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    if (g_closing < 0) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    SourceFile &sf = LoadSource(g_docs[g_closing].path);
    ImGui::Text("%s has changes.", sf.path.c_str());
    if (ImGui::Button("Save")) {
        if (SaveSource(sf)) CloseDoc(g_closing);
        g_closing = -1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Don't save")) {
        ReadSource(sf);
        CloseDoc(g_closing);
        g_closing = -1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) g_closing = -1;
    if (g_closing < 0) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

std::string CodeSelectedText() {
    if (g_codeFile.empty()) return "";
    SourceFile &sf = LoadSource(g_codeFile);
    size_t b, e;
    bool sel = dbg::Editor_Selection(sf.ed, &b, &e) && e - b < 200 && sf.text.find('\n', b) >= e;
    return sel ? sf.text.substr(b, e - b) : "";
}

// A line of a file in Code (in the preview tab), [begin, end) of its text selected when not empty
std::pair<size_t, size_t> g_codeSelect;

void CodeShowLine(const std::string &file, int line, size_t begin, size_t end) {
    g_codeFollow = false;
    g_codeFile = file;
    g_codeLine = g_codeScrollTo = line;
    g_codeHold = true;
    g_codeSelect = {begin, end};
    g_codeFocus = true;
    TabFor(file, false);
    ShowWindow("Code");
}

// ---------------------------------------------------------------------------
// Find and replace in the file shown (Ctrl+F, Ctrl+H): every match on a background, the one
// selected stronger; Enter, F3 the next, Shift the one before
// ---------------------------------------------------------------------------

struct FindState {
    bool open = false, replace = false, matchCase = false, word = false;
    bool focus = false; // the find field takes the keyboard
    std::string find, repl;
    std::vector<std::pair<size_t, size_t>> matches;
    std::string forFile, forFind;
    unsigned forVersion = 0;
    bool forCase = false, forWord = false;
};
FindState g_find;

// The match selected now, or the one at or after the cursor; -1 none
int MatchAt(SourceFile &sf, bool *exact) {
    size_t b, e;
    bool sel = dbg::Editor_Selection(sf.ed, &b, &e);
    if (!sel) b = e = dbg::Editor_CursorOffset(sf.ed);
    *exact = false;
    for (size_t i = 0; i < g_find.matches.size(); i++) {
        if (sel && g_find.matches[i] == std::make_pair(b, e)) {
            *exact = true;
            return (int)i;
        }
        if (g_find.matches[i].first >= b) return (int)i;
    }
    return g_find.matches.empty() ? -1 : 0;
}

void Step(SourceFile &sf, int dir) {
    if (g_find.matches.empty()) return;
    bool exact;
    int i = MatchAt(sf, &exact), n = (int)g_find.matches.size();
    if (exact || dir < 0) i += dir;
    i = ((i % n) + n) % n;
    dbg::Editor_Select(sf.ed, g_find.matches[i].first, g_find.matches[i].second, false);
}

void FindBar(SourceFile &sf, bool windowFocused) {
    ImGuiIO &io = ImGui::GetIO();
    if (windowFocused && io.KeyCtrl && !io.KeyShift && (ImGui::IsKeyPressed(ImGuiKey_F, false) || ImGui::IsKeyPressed(ImGuiKey_H, false))) {
        g_find.open = g_find.focus = true;
        g_find.replace = ImGui::IsKeyPressed(ImGuiKey_H, false);
        // The text selected, on one line, is the one looked for
        size_t b, e;
        if (dbg::Editor_Selection(sf.ed, &b, &e) && e - b < 200 && sf.text.find('\n', b) >= e) g_find.find = sf.text.substr(b, e - b);
    }
    if (!g_find.open) {
        dbg::Editor_SetHighlights(sf.ed, {}, -1);
        return;
    }
    if (NormPath(sf.path) != g_find.forFile || sf.version != g_find.forVersion || g_find.find != g_find.forFind ||
        g_find.matchCase != g_find.forCase || g_find.word != g_find.forWord) {
        bool typed = g_find.find != g_find.forFind;
        g_find.forFile = NormPath(sf.path), g_find.forVersion = sf.version, g_find.forFind = g_find.find;
        g_find.forCase = g_find.matchCase, g_find.forWord = g_find.word;
        FindAll(sf.text, g_find.find, g_find.matchCase, g_find.word, g_find.matches);
        // Typing selects the first match from the cursor on
        if (typed && !g_find.matches.empty()) {
            size_t from = dbg::Editor_CursorOffset(sf.ed), b, e;
            if (dbg::Editor_Selection(sf.ed, &b, &e)) from = b;
            int i = 0;
            while (i < (int)g_find.matches.size() && g_find.matches[i].first < from) i++;
            if (i == (int)g_find.matches.size()) i = 0;
            dbg::Editor_Select(sf.ed, g_find.matches[i].first, g_find.matches[i].second, false);
        }
    }
    bool exact;
    int cur = MatchAt(sf, &exact);
    dbg::Editor_SetHighlights(sf.ed, g_find.matches, exact ? cur : -1);

    float em = ImGui::GetFontSize();
    if (g_find.focus) {
        // From the editor, which holds the keyboard while it is active
        ImGui::ClearActiveID();
        ImGui::SetKeyboardFocusHere();
        g_find.focus = false;
    }
    ImGui::SetNextItemWidth(em * 16);
    bool enter = ImGui::InputTextWithHint("##find", "Find", &g_find.find, ImGuiInputTextFlags_EnterReturnsTrue);
    bool escaped = ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if (enter) {
        Step(sf, io.KeyShift ? -1 : 1);
        ImGui::SetKeyboardFocusHere(-1);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Aa", &g_find.matchCase);
    ImGui::SetItemTooltip("Match case");
    ImGui::SameLine();
    ImGui::Checkbox("Word", &g_find.word);
    ImGui::SetItemTooltip("Whole words only");
    ImGui::SameLine();
    if (g_find.find.empty()) ImGui::TextDisabled("    ");
    else if (g_find.matches.empty()) ImGui::TextColored(Ink(kRed), "none");
    else if (exact) ImGui::Text("%d of %zu", cur + 1, g_find.matches.size());
    else ImGui::Text("%zu found", g_find.matches.size());
    ImGui::SameLine();
    if (ImGui::ArrowButton("##prev", ImGuiDir_Up)) Step(sf, -1);
    ImGui::SetItemTooltip("The one before (Shift+F3, Shift+Enter)");
    ImGui::SameLine();
    if (ImGui::ArrowButton("##next", ImGuiDir_Down)) Step(sf, 1);
    ImGui::SetItemTooltip("The next (F3, Enter)");
    ImGui::SameLine();
    ImGui::Checkbox("Replace", &g_find.replace);
    ImGui::SameLine();
    if (ImGui::SmallButton("x")) g_find.open = false;
    ImGui::SetItemTooltip("Close (Escape)");
    if (windowFocused && ImGui::IsKeyPressed(ImGuiKey_F3)) Step(sf, io.KeyShift ? -1 : 1);
    if (escaped) {
        // Closed, the keyboard back to the editor with the match selected
        g_find.open = false;
        size_t b, e;
        dbg::Editor_Selection(sf.ed, &b, &e);
        dbg::Editor_Select(sf.ed, b, e, true);
    }
    if (g_find.replace) {
        ImGui::SetNextItemWidth(em * 16);
        bool go = ImGui::InputTextWithHint("##replace", "Replace with", &g_find.repl, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((ImGui::Button("Replace") || go) && cur >= 0) {
            if (exact) {
                auto [b, e] = g_find.matches[cur];
                dbg::Editor_Replace(sf.ed, sf.text, sf.version, b, e, g_find.repl);
            }
            // Found again, then the next one selected
            FindAll(sf.text, g_find.find, g_find.matchCase, g_find.word, g_find.matches);
            g_find.forVersion = sf.version;
            Step(sf, 1);
            if (go) ImGui::SetKeyboardFocusHere(-1);
        }
        ImGui::SetItemTooltip("Replaces the match selected and selects the next (Enter in the field)");
        ImGui::SameLine();
        if (ImGui::Button("All") && !g_find.matches.empty()) {
            // One step of undo, from the first match to the end of the last
            size_t first = g_find.matches.front().first, last = g_find.matches.back().second, at = first;
            std::string mid;
            for (const auto &[b, e] : g_find.matches) {
                mid += sf.text.substr(at, b - at) + g_find.repl;
                at = e;
            }
            Log(LogLine::Info, "Replaced " + std::to_string(g_find.matches.size()) + " in " + sf.path);
            dbg::Editor_Replace(sf.ed, sf.text, sf.version, first, last, mid);
        }
        ImGui::SetItemTooltip("Replaces every match (Ctrl+Z undoes it)");
    }
}

void CodeSettingsLine(const std::string &key, const std::string &value) {
    if (key == "CodeDoc") g_docs.push_back({value, false});
    else if (key == "CodeFile") g_codeFile = value;
    else if (key == "EditorZoom") dbg::Editor_SetZoom((float)atof(value.c_str()));
}

void CodeSettingsWrite(ImGuiTextBuffer *buf) {
    for (const OpenDoc &d : g_docs)
        if (!d.preview) buf->appendf("CodeDoc=%s\n", d.path.c_str());
    if (!g_codeFile.empty() && FindDoc(g_codeFile) >= 0 && !g_docs[FindDoc(g_codeFile)].preview)
        buf->appendf("CodeFile=%s\n", g_codeFile.c_str());
    if (dbg::Editor_Zoom() != 1.0f) buf->appendf("EditorZoom=%.3f\n", dbg::Editor_Zoom());
}

void CodeWindow() {
    WatchSources();
    // The game records the trigger values of the selected entity
    static std::pair<unsigned long, int> trigWatch;
    if (Connected() && g_canTrace && trigWatch != std::make_pair((unsigned long)g_bridge.Pid(), g_selId)) {
        trigWatch = {(unsigned long)g_bridge.Pid(), g_selId};
        SendRaw("if debugTriggerWatch then debugTriggerWatch(" + std::to_string(g_selId) + ") end");
    }
    // A line asked from another window (not the state followed here): Code comes to the front.
    // With the game running it stops following the state, which would leave that line at once.
    bool &follow = g_codeFollow;
    static int askedLine;
    if (g_codeScrollTo > 0 && g_codeScrollTo != askedLine) {
        ShowWindow("Code");
        if (!g_paused) follow = false;
    }
    askedLine = g_codeScrollTo;
    if (!BeginTool("Code")) return;
    const Entity *e = FindEntity(g_selId);
    const FrameRow *row = nullptr;
    auto hist = g_history.find(g_selId);
    if (hist != g_history.end() && !hist->second.empty()) {
        row = &hist->second.back();
        if (g_selTick >= 0)
            for (const auto &r : hist->second)
                if (r.t == g_selTick) row = &r;
    }

    // Follow the selected entity's state, but not while its file is being edited
    static int followedSrc;
    SourceFile *shown = g_codeFile.empty() ? nullptr : &LoadSource(g_codeFile);
    bool editing = shown && dbg::Editor_IsFocused(shown->ed);
    int stateSrc = row ? row->stateSrc : e ? e->stateSrc : 0;
    if (g_codeHold) {
        followedSrc = stateSrc;
        g_codeHold = false;
    }
    if (follow && g_canTrace && stateSrc != followedSrc && !editing) {
        followedSrc = stateSrc;
        JumpTo(stateSrc);
    }
    if (!g_codeFile.empty()) TabFor(g_codeFile, false);
    if (!g_docs.empty()) DocTabs(follow);
    CloseDialog();
    shown = g_codeFile.empty() ? nullptr : &LoadSource(g_codeFile);

    // Toolbar: Save and apply, Revert, the files with changes
    int dirty = 0;
    for (const auto &kv : g_files) dirty += kv.second.Dirty();
    bool save = ImGui::Button(dirty ? "Save and apply (Ctrl+S)" : "Apply (Ctrl+S)");
    ImGuiIO &io = ImGui::GetIO();
    bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (focused && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) save = true;
    if (focused && io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_W, false)) CodeCloseShown();
    if (save) SaveAndApply();
    ImGui::SetItemTooltip("Saves the changed files, then the game loads and compiles the characters again "
                          "(states, commands, constants, animations, sounds) without restarting the match.");
    ImGui::SameLine();
    ImGui::BeginDisabled(!shown || !shown->Dirty());
    if (ImGui::Button("Revert")) ReadSource(*shown);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Checkbox("Follow the state", &follow);
    ImGui::SameLine();
    if (ImGui::SmallButton("Statedef")) JumpTo(stateSrc);
    if (dirty) {
        ImGui::SameLine();
        ImGui::TextColored(Ink(kYellow), "%d unsaved", dirty);
    }
    if (g_applying) {
        ImGui::SameLine();
        ImGui::TextDisabled("applying...");
    }
    if (!g_applyError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, Ink(kRed));
        ImGui::TextWrapped("Not applied, the game keeps the previous version. %s", g_applyError.c_str());
        ImGui::PopStyleColor();
    }
    if (e) {
        ImGui::TextDisabled("%s  state %d", Who(e->pn, e->helper, e->id).c_str(), row ? row->st : e->st);
        if (row) {
            ImGui::SameLine();
            ImGui::TextDisabled("tick %d%s", row->t, g_selTick >= 0 ? " (picked in Frame data)" : "");
        }
    }

    // Left: the controllers that ran, in order (patched Ikemen). Right: the file.
    std::map<int, bool> ranLines;
    std::string codeNorm = NormPath(g_codeFile);
    if (g_canTrace) {
        float listWidth = ImGui::GetContentRegionAvail().x * 0.3f;
        if (ImGui::BeginChild("ran", ImVec2(listWidth, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX)) {
            ImGui::TextDisabled("%zu controllers ran on this tick, in order", row ? row->ran.size() : 0);
            if (row && ImGui::BeginTable("ranlist", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                                          ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("State");
                ImGui::TableSetupColumn("Controller");
                ImGui::TableSetupColumn("Where", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
                ImGuiListClipper clipper;
                clipper.Begin((int)row->ran.size());
                while (clipper.Step()) {
                    for (int n = clipper.DisplayStart; n < clipper.DisplayEnd; n++) {
                        int src = row->ran[n];
                        ImGui::TableNextRow();
                        if (src <= 0 || src >= (int)g_sources.size()) {
                            // The source has not arrived yet (they come a few hundred per frame)
                            ImGui::TableNextColumn();
                            ImGui::TextDisabled("?");
                            ImGui::TableNextColumn();
                            ImGui::TextDisabled("source %d", src);
                            continue;
                        }
                        const Source &s = g_sources[src];
                        const char *name = s.file.c_str() + s.file.find_last_of("\\/") + 1;
                        ImGui::TableNextColumn();
                        char rowLabel[32];
                        snprintf(rowLabel, sizeof(rowLabel), "%d##%d", s.state, n);
                        bool here = NormPath(s.file) == codeNorm && s.line == g_codeLine;
                        if (ImGui::Selectable(rowLabel, here, ImGuiSelectableFlags_SpanAllColumns)) JumpTo(src);
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(s.type.c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text("%s:%d", name, s.line);
                    }
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndChild();
        if (row)
            for (int src : row->ran)
                if (src > 0 && src < (int)g_sources.size() && NormPath(g_sources[src].file) == codeNorm)
                    ranLines[g_sources[src].line] = true;
        ImGui::SameLine();
    }

    if (!shown) {
        ImGui::TextDisabled(g_canTrace ? "Pick a controller on the left, or a file in Explorer (Ctrl+P)."
                                       : "Open a file from Explorer (Ctrl+P). With the game running, Code follows "
                                         "the selected player's state.");
        ImGui::End();
        return;
    }
    SourceFile &sf = *shown;
    if (g_codeScrollTo > 0) {
        dbg::Editor_GotoLine(sf.ed, g_codeScrollTo, false, g_codeFocus);
        g_codeScrollTo = 0;
    } else if (g_codeFocus) {
        size_t b, e;
        dbg::Editor_Selection(sf.ed, &b, &e);
        dbg::Editor_Select(sf.ed, b, e, true);
    }
    g_codeFocus = false;
    const auto &ctrlLines = SourceLines(sf.path, kControllers);
    const auto &trigLines = SourceLines(sf.path, kTriggers);
    const auto &defLines = SourceLines(sf.path, kStatedefs);
    std::string file = sf.path;
    int codeLine = g_codeLine;
    // The state number of a statedef line (-1: none, or a negative state, which is not entered)
    auto stateAt = [&](int line) {
        auto it = defLines.find(line);
        if (it == defLines.end()) return -1;
        return g_sources[it->second[0]].state;
    };
    dbg::Editor_SetDecorations(
        sf.ed,
        [&](int line) {
            if (CtrlBreak *bp = FindCtrlBreak(file, line)) return bp->enabled ? 2 : 3;
            if (ctrlLines.count(line)) return 1;
            int state = stateAt(line);
            if (state < 0) return 0;
            Breakpoint *bp = FindStateBreak(state);
            return bp ? (bp->enabled ? 2 : 3) : 1;
        },
        [&](int line) -> ImU32 {
            if (line == codeLine) return IM_COL32(200, 170, 40, 70);
            return ranLines.count(line) ? IM_COL32(40, 130, 60, 110) : 0;
        });
    // After each line: a trigger's value on the tick shown, and when a controller that did not run
    // on that tick last ran
    bool trigShown = row && g_trigId == g_selId;
    const std::map<int, int> *lastRan = row ? &LastRan(g_selId, NormPath(file), row->t) : nullptr;
    int rowTick = row ? row->t : 0;
    dbg::Editor_SetNotes(sf.ed, [&](int line, ImU32 *color) -> std::string {
        if (trigShown) {
            auto it = trigLines.find(line);
            if (it != trigLines.end())
                for (int src : it->second) {
                    int v = TriggerValue(rowTick, src);
                    if (v < 0) continue;
                    *color = v == 1 ? ImGui::GetColorU32(Ink(kGreen)) : v == 0 ? ImGui::GetColorU32(Ink(kRed))
                                                                               : IM_COL32(150, 150, 150, 255);
                    return v == 1 ? "true" : v == 0 ? "false" : "undefined";
                }
        }
        if (lastRan && ctrlLines.count(line) && !ranLines.count(line)) {
            auto it = lastRan->find(line);
            if (it != lastRan->end()) {
                *color = IM_COL32(140, 140, 150, 255);
                int ago = rowTick - it->second;
                return "ran " + std::to_string(ago) + (ago == 1 ? " tick ago" : " ticks ago");
            }
        }
        return "";
    });
    if (g_codeSelect.second > g_codeSelect.first) {
        dbg::Editor_Select(sf.ed, g_codeSelect.first, g_codeSelect.second, true);
        g_codeSelect = {0, 0};
    }
    ImGui::BeginGroup();
    if (sf.missing) ImGui::TextDisabled("Could not read %s", sf.path.c_str());
    FindBar(sf, focused);
    dbg::Editor_Draw(sf.ed, "file", sf.text, sf.version);
    ImGui::EndGroup();
    if (int line = dbg::Editor_MarkClicked(sf.ed)) {
        if (ctrlLines.count(line)) ToggleCtrlBreak(file, line);
        else if (stateAt(line) >= 0) ToggleStateBreak(stateAt(line), e ? e->pn : 0);
    }
    // Go to definition: F12 or Ctrl+click; Shift+F12 the places naming the state
    size_t clicked = dbg::Editor_CtrlClicked(sf.ed);
    bool f12 = dbg::Editor_IsFocused(sf.ed) && ImGui::IsKeyPressed(ImGuiKey_F12, false);
    if (clicked != SIZE_MAX || f12)
        GoToDefinition(file, clicked != SIZE_MAX ? clicked : dbg::Editor_CursorOffset(sf.ed), f12 && io.KeyShift);
    ImGui::End();
}

} // namespace app
