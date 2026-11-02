// Search: text found in the files of the character open, of all of Explorer or of the files
// open (Ctrl+Shift+F), and the places a state is named from (Shift+F12 in Code); a result
// clicked shows its line in Code. Outline: the sections of the file shown in Code (a state
// file's statedefs and functions, an AIR file's actions, a def's sections), with the comment
// above each.
#include "app_internal.h"

#include <cfloat>
#include <set>

#include "imgui_stdlib.h"

namespace app {

void FindAll(const std::string &text, const std::string &pat, bool matchCase, bool word,
             std::vector<std::pair<size_t, size_t>> &out) {
    out.clear();
    if (pat.empty()) return;
    auto lower = [](unsigned char c) { return (char)tolower(c); };
    auto ident = [](char c) { return isalnum((unsigned char)c) || c == '_'; };
    std::string hay = text, needle = pat;
    if (!matchCase) {
        std::transform(hay.begin(), hay.end(), hay.begin(), lower);
        std::transform(needle.begin(), needle.end(), needle.begin(), lower);
    }
    for (size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + 1)) {
        size_t end = at + needle.size();
        if (word && ((at > 0 && ident(hay[at - 1])) || (end < hay.size() && ident(hay[end])))) continue;
        out.push_back({at, end});
        at = end - 1;
    }
}

namespace {

// A file's text: as being edited when open in Code, else from disk ('\n' line ends)
std::string FileText(const std::string &path) {
    auto open = g_files.find(NormPath(path));
    if (open != g_files.end()) return open->second.text;
    std::string raw = ReadWholeFile(GamePath(path)), text;
    text.reserve(raw.size());
    for (char ch : raw)
        if (ch != '\r') text += ch;
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB) text.erase(0, 3);
    return text;
}

struct Result {
    std::string file;
    int line;
    size_t begin, end; // in the file's text, end == begin: the whole line
    std::string text;  // the line, trimmed
    std::string note;  // after it, dimmed
};

std::string g_title;                // what the results are
std::vector<Result> g_results;
std::string g_query;
bool g_case, g_word;
int g_scope; // 0 the character open, 1 all of Explorer, 2 the files open
bool g_focusQuery;

void Run() {
    g_results.clear();
    std::vector<std::string> files;
    if (g_scope == 0) {
        if (const auto *f = FilesFor(WorkPn()))
            for (const auto &[key, paths] : *f)
                for (const auto &p : paths) files.push_back(p);
    } else if (g_scope == 1) {
        ProjectFiles(files);
    } else {
        for (const auto &kv : g_files) files.push_back(kv.second.path);
    }
    std::set<std::string> seen;
    size_t total = 0, inFiles = 0;
    std::vector<std::pair<size_t, size_t>> found;
    for (const std::string &p : files) {
        if (!IsTextFile(p) || !seen.insert(NormPath(p)).second) continue;
        std::string text = FileText(p);
        FindAll(text, g_query, g_case, g_word, found);
        if (found.empty()) continue;
        inFiles++;
        int line = 1;
        size_t at = 0;
        for (const auto &[b, e] : found) {
            for (; at < b; at++)
                if (text[at] == '\n') line++;
            size_t lb = text.rfind('\n', b ? b - 1 : 0);
            lb = b == 0 || lb == std::string::npos ? 0 : lb + 1;
            size_t le = text.find('\n', b);
            std::string s = text.substr(lb, (le == std::string::npos ? text.size() : le) - lb);
            size_t first = s.find_first_not_of(" \t");
            g_results.push_back({p, line, b, e, first == std::string::npos ? "" : s.substr(first), ""});
            if (++total >= 5000) break;
        }
        if (total >= 5000) break;
    }
    g_title = "\"" + g_query + "\": " + std::to_string(total) + (total >= 5000 ? "+" : "") + " in " +
              std::to_string(inFiles) + (inFiles == 1 ? " file" : " files");
}

} // namespace

void SearchFor(const std::string &text) {
    if (!text.empty()) g_query = text;
    g_focusQuery = true;
    ShowWindow("Search");
}

void ShowReferences(const std::string &title, const std::vector<CodePlace> &places) {
    g_results.clear();
    for (const CodePlace &p : places) g_results.push_back({p.file, p.line, 0, 0, p.text, p.note});
    g_title = title;
    ShowWindow("Search");
}

void SearchWindow() {
    if (!BeginTool("Search")) return;
    float em = ImGui::GetFontSize();
    if (g_focusQuery) {
        ImGui::SetKeyboardFocusHere();
        g_focusQuery = false;
    }
    ImGui::SetNextItemWidth(std::max(em * 10, ImGui::GetContentRegionAvail().x - em * 22));
    bool go = ImGui::InputTextWithHint("##query", "Find in files (Enter)", &g_query, ImGuiInputTextFlags_EnterReturnsTrue);
    if (go) ImGui::SetKeyboardFocusHere(-1);
    ImGui::SameLine();
    ImGui::Checkbox("Aa", &g_case);
    ImGui::SetItemTooltip("Match case");
    ImGui::SameLine();
    ImGui::Checkbox("Word", &g_word);
    ImGui::SetItemTooltip("Whole words only");
    ImGui::SameLine();
    const char *scopes[] = {"The character open", "All of Explorer", "The files open"};
    ImGui::SetNextItemWidth(em * 11);
    ImGui::Combo("##scope", &g_scope, scopes, IM_ARRAYSIZE(scopes));
    ImGui::SetItemTooltip("The character open: the project's files. All of Explorer: every file it lists.");
    if (go && !g_query.empty()) Run();
    if (!g_title.empty()) ImGui::TextDisabled("%s", g_title.c_str());

    if (!ImGui::BeginChild("results")) {
        ImGui::EndChild();
        ImGui::End();
        return;
    }
    // By file, each a tree node
    for (size_t i = 0; i < g_results.size();) {
        size_t j = i;
        while (j < g_results.size() && g_results[j].file == g_results[i].file) j++;
        const std::string &file = g_results[i].file;
        std::string name = file.substr(file.find_last_of("\\/") + 1);
        ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
        bool open = ImGui::TreeNodeEx(file.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth, "%s  (%zu)", name.c_str(), j - i);
        ImGui::SetItemTooltip("%s", file.c_str());
        if (open) {
            ImGuiListClipper clipper;
            clipper.Begin((int)(j - i));
            while (clipper.Step())
                for (int k = clipper.DisplayStart; k < clipper.DisplayEnd; k++) {
                    const Result &r = g_results[i + k];
                    ImGui::PushID((int)(i + k));
                    char label[16];
                    snprintf(label, sizeof(label), "%5d", r.line);
                    if (ImGui::Selectable(label, false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
                        CodeShowLine(r.file, r.line, r.begin, r.end);
                    ImGui::SameLine();
                    ImGui::TextUnformatted(r.text.c_str());
                    if (!r.note.empty()) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("%s", r.note.c_str());
                    }
                    ImGui::PopID();
                }
            ImGui::TreePop();
        }
        i = j;
    }
    ImGui::EndChild();
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Outline
// ---------------------------------------------------------------------------

void OutlineWindow() {
    if (!BeginTool("Outline")) return;
    static ImGuiTextFilter filter;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##filter", "Filter", filter.InputBuf, IM_ARRAYSIZE(filter.InputBuf))) filter.Build();
    std::string file;
    int cursor = 0, col;
    if (!CodeCursor(&file, &cursor, &col)) {
        ImGui::TextDisabled("Open a file in Code.");
        ImGui::End();
        return;
    }
    // The sections: [...] at the start of a line. In a state file only statedefs and functions
    // (its [State] sections are its controllers).
    struct Item {
        int line;
        std::string head, note;
    };
    static std::vector<Item> items;
    static std::string forFile;
    static unsigned forVersion;
    SourceFile &sf = LoadSource(file);
    if (forFile != NormPath(file) || forVersion != sf.version) {
        forFile = NormPath(file);
        forVersion = sf.version;
        items.clear();
        const std::string &t = sf.text;
        std::string ext = NormPath(file).substr(NormPath(file).find_last_of('.') + 1);
        std::string lastComment;
        int line = 1, commentLine = -10;
        for (size_t at = 0; at <= t.size(); line++) {
            size_t end = t.find('\n', at);
            if (end == std::string::npos) end = t.size();
            size_t b = t.find_first_not_of(" \t", at);
            if (b < end) {
                char c = t[b];
                std::string s = t.substr(b, end - b);
                if (c == ';' || c == '#') {
                    // A comment: kept as the note of a section right after it
                    size_t from = s.find_first_not_of(";# -=*/\t");
                    if (from != std::string::npos) lastComment = s.substr(from), commentLine = line;
                } else if (c == '[') {
                    std::string lower = s;
                    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                    bool stateFile = ext == "cns" || ext == "st" || ext == "zss" || ext == "cmd";
                    bool top = lower.compare(0, 9, "[statedef") == 0 || lower.compare(0, 9, "[function") == 0;
                    if (!stateFile || top) {
                        size_t close = s.find(']');
                        items.push_back({line, s.substr(0, close == std::string::npos ? s.size() : close + 1),
                                         line - commentLine <= 2 ? lastComment : ""});
                    }
                }
            }
            at = end + 1;
        }
    }
    ImGui::TextDisabled("%s  %zu sections", file.substr(file.find_last_of("\\/") + 1).c_str(), items.size());
    if (ImGui::BeginChild("items")) {
        // The section the cursor is in: selected, scrolled to when it changes
        int at = -1;
        for (int i = 0; i < (int)items.size() && items[i].line <= cursor; i++) at = i;
        static int shownAt = -2;
        for (int i = 0; i < (int)items.size(); i++) {
            const Item &it = items[i];
            if (filter.IsActive() && !filter.PassFilter(it.head.c_str()) && !filter.PassFilter(it.note.c_str())) continue;
            ImGui::PushID(i);
            if (ImGui::Selectable(it.head.c_str(), i == at, ImGuiSelectableFlags_AllowOverlap)) CodeShowLine(file, it.line);
            if (i == at && at != shownAt) ImGui::SetScrollHereY(0.3f);
            if (!it.note.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", it.note.c_str());
            }
            ImGui::PopID();
        }
        shownAt = at;
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace app
