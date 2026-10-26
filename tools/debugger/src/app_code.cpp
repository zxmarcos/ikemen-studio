// Code: the source of the selected entity's state, and the controllers that ran.
#include "app_internal.h"

#include <fstream>

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
    static bool follow = true;
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

    // Toolbar: Save and apply, Revert, the files with changes
    int dirty = 0;
    for (const auto &kv : g_files) dirty += kv.second.Dirty();
    bool save = ImGui::Button(dirty ? "Save and apply (Ctrl+S)" : "Apply (Ctrl+S)");
    ImGuiIO &io = ImGui::GetIO();
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && io.KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_S, false))
        save = true;
    if (save) SaveAndApply();
    ImGui::SetItemTooltip("Saves the changed files, then the game loads and compiles the characters again "
                          "(states, commands, constants, animations, sounds) without restarting the match.");
    ImGui::SameLine();
    ImGui::BeginDisabled(!shown || !shown->Dirty());
    if (ImGui::Button("Revert")) ReadSource(*shown);
    ImGui::EndDisabled();
    ImGui::SameLine();
    std::string label = g_codeFile.empty() ? std::string("(no file)") : g_codeFile;
    if (shown && shown->Dirty()) label += " *";
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18);
    if (ImGui::BeginCombo("##file", label.c_str())) {
        for (auto &kv : g_files) {
            std::string name = kv.second.path + (kv.second.Dirty() ? " *" : "");
            if (ImGui::Selectable(name.c_str(), NormPath(g_codeFile) == kv.first)) {
                g_codeFile = kv.second.path;
                g_codeLine = 0;
                follow = false;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Follow the state", &follow);
    ImGui::SameLine();
    if (ImGui::SmallButton("Statedef")) JumpTo(stateSrc);
    if (dirty) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0.75f, 0.3f, 1), "%d unsaved", dirty);
    }
    if (g_applying) {
        ImGui::SameLine();
        ImGui::TextDisabled("applying...");
    }
    if (!g_applyError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.45f, 0.45f, 1));
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
        ImGui::TextDisabled(g_canTrace ? "Pick a controller on the left."
                                       : "The Code window follows the state with the patched Ikemen GO.");
        ImGui::End();
        return;
    }
    SourceFile &sf = *shown;
    if (g_codeScrollTo > 0) {
        dbg::Editor_GotoLine(sf.ed, g_codeScrollTo, false, false);
        g_codeScrollTo = 0;
    }
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
                    *color = v == 1 ? IM_COL32(90, 220, 110, 255) : v == 0 ? IM_COL32(240, 100, 100, 255)
                                                                           : IM_COL32(160, 160, 160, 255);
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
    if (sf.missing) ImGui::TextDisabled("Could not read %s", sf.path.c_str());
    dbg::Editor_Draw(sf.ed, "file", sf.text, sf.version);
    if (int line = dbg::Editor_MarkClicked(sf.ed)) {
        if (ctrlLines.count(line)) ToggleCtrlBreak(file, line);
        else if (stateAt(line) >= 0) ToggleStateBreak(stateAt(line), e ? e->pn : 0);
    }
    ImGui::End();
}

} // namespace app
