#include "make.h"

#include <algorithm>
#include <vector>

#include "cns.h"

namespace {

std::vector<std::string> SplitLines(const std::string &text) {
    std::vector<std::string> lines;
    size_t start = 0;
    for (;;) {
        size_t nl = text.find('\n', start);
        lines.push_back(text.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return lines;
}

std::string JoinLines(const std::vector<std::string> &lines) {
    std::string text;
    for (size_t i = 0; i < lines.size(); i++) {
        if (i) text += '\n';
        text += lines[i];
    }
    return text;
}

const char *const kTypes[] = {"", "S", "C", "A"};

} // namespace

std::string Make_CommandEntry(bool zss, const std::string &command, int state, int when, bool ctrl) {
    std::string value = std::to_string(state);
    if (when < 0 || when > 3) when = 0;
    if (zss) {
        std::string cond = "command = \"" + command + "\"";
        if (when) cond += std::string(" && stateType = ") + kTypes[when];
        if (ctrl) cond += " && ctrl";
        return "if " + cond + " {\n\tchangeState{value: " + value + "}\n}\n";
    }
    std::string t = "[State -1, " + command + "]\ntype = ChangeState\nvalue = " + value + "\n";
    t += "triggerall = command = \"" + command + "\"\n";
    if (when) t += std::string("trigger1 = statetype = ") + kTypes[when] + "\n";
    if (ctrl) t += "trigger1 = ctrl\n";
    if (!when && !ctrl) t += "trigger1 = 1\n";
    return t + "\n";
}

bool Make_InsertInStatedef(std::string &text, bool zss, int no, const std::string &block, int *line) {
    CnsFile f;
    Cns_Parse(text, zss, f);
    const CnsState *st = nullptr;
    for (const CnsState &s : f.states)
        if (s.literal && s.function.empty() && s.no == no) {
            st = &s;
            break;
        }
    if (!st) return false;
    // Right after the header (ZSS: until its "]"; CNS: and the statedef's parameters), so that
    // the comments above the first controller stay with it
    std::vector<std::string> lines = SplitLines(text);
    size_t at; // the index the block's first line takes
    if (zss) {
        size_t end = st->line - 1;
        while (end < lines.size() && lines[end].find(']') == std::string::npos) end++;
        at = std::min(end + 1, lines.size());
    } else {
        int last = st->line;
        for (const CnsParam &p : st->params) last = std::max(last, p.line);
        at = std::min((size_t)last, lines.size());
    }
    std::vector<std::string> add = SplitLines(block);
    while (!add.empty() && add.back().empty()) add.pop_back();
    add.insert(add.begin(), ""); // apart from the header
    if (at < lines.size() && !lines[at].empty() && lines[at].find_first_not_of(" \t\r") != std::string::npos)
        add.push_back(""); // and from what follows
    lines.insert(lines.begin() + at, add.begin(), add.end());
    if (line) *line = (int)at + 2;
    text = JoinLines(lines);
    return true;
}
