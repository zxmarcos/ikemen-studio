// Problems: what is wrong, from every source in one list: the Lint of the character worked on,
// the checks of the stage (the match's, or the toolbar's), the last Apply that failed, and the
// warnings and errors the engine logs ("WARNING: Kung Fu Man's state 1027 in kfm.cns, line
// 1476: ..."); a row clicked shows its line in Code. Their count is on the status bar.
#include "app_internal.h"

#include <regex>

namespace app {

namespace {

std::vector<Problem> g_engine; // the engine's, the same text once (with how many times)

// A file the engine names by its name only: the match's player's file of that name
std::string EngineFile(const std::string &name) {
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) return name;
    std::string want = NormPath(name);
    for (const auto &[pn, files] : g_charFiles)
        for (const auto &[key, paths] : files)
            for (const auto &p : paths) {
                std::string n = NormPath(p);
                if (n.size() >= want.size() && n.compare(n.size() - want.size(), want.size(), want) == 0 &&
                    (n.size() == want.size() || n[n.size() - want.size() - 1] == '/'))
                    return p;
            }
    return name;
}

void Gather(std::vector<Problem> &out) {
    out.clear();
    // The last Apply that failed: the file and line the compiler named
    for (const auto &kv : g_files)
        if (kv.second.errorLine > 0) out.push_back({0, "Apply", kv.second.path, kv.second.errorLine, kv.second.errorMsg});
    if (!g_applyError.empty() && out.empty()) out.push_back({0, "Apply", "", 0, g_applyError});
    for (const Problem &p : g_engine) out.push_back(p);
    LintProblems(out);
    StageProblems(out);
    ScreenProblems(out);
}

} // namespace

void EngineLine(const std::string &line) {
    std::string text = line;
    while (!text.empty() && isspace((unsigned char)text.back())) text.pop_back();
    int level;
    if (text.compare(0, 8, "WARNING:") == 0) level = 1;
    else if (text.compare(0, 6, "Error:") == 0 || text.compare(0, 6, "ERROR:") == 0 || text.compare(0, 6, "panic:") == 0)
        level = 0;
    else return;
    for (Problem &p : g_engine)
        if (p.text == text) {
            p.count++;
            return;
        }
    Problem p{level, "Engine", "", 0, text};
    static const std::regex in(R"(in ([^,]+), line (\d+))"), at(R"(([\w./\\ -]+\.\w+):(\d+))");
    std::smatch m;
    if (std::regex_search(text, m, in) || std::regex_search(text, m, at)) {
        p.file = m[1].str();
        p.line = atoi(m[2].str().c_str());
    }
    g_engine.push_back(p);
    if (g_engine.size() > 500) g_engine.erase(g_engine.begin());
}

void ToolError(const std::string &text) {
    for (Problem &p : g_engine)
        if (p.text == text) {
            p.count++;
            return;
        }
    g_engine.push_back({0, "Debugger", "", 0, text});
}

void ClearEngineProblems() { g_engine.clear(); }

// Gathered again a few times a second (the Lint's model hashes the character's files to know
// whether they changed)
static const std::vector<Problem> &Problems() {
    static std::vector<Problem> all;
    static double at = -100;
    if (ImGui::GetTime() - at > 0.3) {
        at = ImGui::GetTime();
        Gather(all);
    }
    return all;
}

void ProblemCounts(int count[3]) {
    count[0] = count[1] = count[2] = 0;
    for (const Problem &p : Problems()) count[std::clamp(p.level, 0, 2)]++;
}

void ProblemsWindow() {
    if (!BeginTool("Problems")) return;
    static bool show[3] = {true, true, false};
    const std::vector<Problem> &all = Problems();
    int count[3] = {};
    for (const Problem &p : all) count[std::clamp(p.level, 0, 2)]++;
    const char *names[3] = {"Errors", "Warnings", "Notes"};
    const ImVec4 colors[3] = {Ink(kRed), Ink(kYellow), Ink(kBlue)};
    for (int i = 0; i < 3; i++) {
        char label[32];
        snprintf(label, sizeof(label), "%s (%d)", names[i], count[i]);
        if (i) ImGui::SameLine();
        ImGui::Checkbox(label, &show[i]);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(g_engine.empty());
    if (ImGui::SmallButton("Clear the engine's")) ClearEngineProblems();
    ImGui::EndDisabled();
    ImGui::SameLine();
    HelpMarker("Lint: the character worked on (Analysis). Stage: the checks of the stage in the match, or of the "
               "toolbar's. Apply: the last one that failed. Engine: the warnings and errors it logged since it "
               "started. A row clicked shows its line in Code.");
    float em = ImGui::GetFontSize();
    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("problems", 3, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("From", ImGuiTableColumnFlags_WidthFixed, em * 4);
        ImGui::TableSetupColumn("Where", ImGuiTableColumnFlags_WidthFixed, em * 12);
        ImGui::TableSetupColumn("What", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (size_t n = 0; n < all.size(); n++) {
            const Problem &p = all[n];
            int level = std::clamp(p.level, 0, 2);
            if (!show[level]) continue;
            ImGui::PushID((int)n);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", p.source);
            ImGui::TableNextColumn();
            std::string file = p.source == std::string("Engine") ? EngineFile(p.file) : p.file;
            std::string where = file.empty() ? "" : file.substr(file.find_last_of("/\\") + 1);
            if (p.line > 0) where += ":" + std::to_string(p.line);
            if (ImGui::Selectable(where.empty() ? "-" : where.c_str(), false, ImGuiSelectableFlags_SpanAllColumns) &&
                !file.empty() && p.line > 0)
                CodeShowLine(file, p.line);
            if (!file.empty()) ImGui::SetItemTooltip("%s:%d", file.c_str(), p.line);
            ImGui::TableNextColumn();
            ImGui::TextColored(colors[level], "%s", p.text.c_str());
            if (p.count > 1) {
                ImGui::SameLine();
                ImGui::TextDisabled("(%d times)", p.count);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

} // namespace app
