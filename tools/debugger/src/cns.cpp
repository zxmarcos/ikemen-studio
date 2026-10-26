#include "cns.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace {

std::string Trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::string Lower(std::string s) {
    for (char &c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// The line without its comment (from the comment character, outside double quotes)
std::string StripComment(const std::string &line, char comment) {
    bool quoted = false;
    for (size_t i = 0; i < line.size(); i++) {
        if (line[i] == '"') quoted = !quoted;
        else if (line[i] == comment && !quoted) return line.substr(0, i);
    }
    return line;
}

std::vector<std::string> Lines(const std::string &text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string l = text.substr(start, end - start);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        lines.push_back(l);
        start = end + 1;
    }
    return lines;
}

// A state number: "200", "-1"; literal false for anything else
int StateNo(const std::string &s, bool &literal) {
    std::string t = Trim(s);
    char *end = nullptr;
    long v = strtol(t.c_str(), &end, 10);
    literal = !t.empty() && end && *end == 0;
    return literal ? (int)v : 0;
}

void ParseCns(const std::vector<std::string> &lines, CnsFile &out) {
    int st = -1;
    CnsCtrl *ctrl = nullptr;
    for (size_t i = 0; i < lines.size(); i++) {
        int line = (int)i + 1;
        std::string s = Trim(StripComment(lines[i], ';'));
        if (s.empty()) continue;
        if (s[0] == '[') {
            size_t close = s.find(']');
            std::string head = Trim(s.substr(1, close == std::string::npos ? std::string::npos : close - 1));
            std::string low = Lower(head);
            ctrl = nullptr;
            if (low.compare(0, 8, "statedef") == 0) {
                CnsState state;
                std::string no = head.substr(8);
                no = no.substr(0, no.find(','));
                state.no = StateNo(no, state.literal);
                state.line = line;
                out.states.push_back(state);
                st = (int)out.states.size() - 1;
            } else if (low.compare(0, 5, "state") == 0 && st >= 0) {
                out.states[st].ctrls.push_back(CnsCtrl());
                ctrl = &out.states[st].ctrls.back();
                ctrl->line = line;
            } else {
                st = -1; // another section ([Data], [Command]...)
            }
            continue;
        }
        if (st < 0) continue;
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        CnsParam p{Lower(Trim(s.substr(0, eq))), Trim(s.substr(eq + 1)), line};
        out.code.push_back({line, st, p.value});
        if (!ctrl) {
            out.states[st].params.push_back(p);
        } else if (p.key == "type") {
            ctrl->type = Lower(p.value);
        } else if (p.key.compare(0, 7, "trigger") == 0) {
            if (p.key == "trigger1" || p.key == "triggerall") ctrl->hasTrigger = true;
        } else {
            ctrl->params.push_back(p);
        }
    }
}

bool IdentStart(char c) { return isalpha((unsigned char)c) || c == '_'; }
bool IdentChar(char c) { return isalnum((unsigned char)c) || c == '_' || c == '.'; }

// Words followed by a block, not a controller
bool BlockWord(const std::string &w) {
    static const char *words[] = {"if", "else", "for", "while", "switch", "case", "default", "ignorehitpause",
                                  "persistent", "let", "call", "break", "continue"};
    for (const char *k : words)
        if (w == k) return true;
    return false;
}

void ParseZss(const std::vector<std::string> &lines, CnsFile &out) {
    // The text without comments, and where each line starts in it
    std::string text;
    std::vector<size_t> starts;
    for (const auto &l : lines) {
        starts.push_back(text.size());
        text += StripComment(l, '#');
        text += '\n';
    }
    auto lineOf = [&](size_t pos) { return (int)(std::upper_bound(starts.begin(), starts.end(), pos) - starts.begin()); };
    // The state each line is in, from the headers
    std::vector<int> stateOfLine(lines.size() + 2, -1);
    int st = -1;
    size_t pos = 0, n = text.size();
    while (pos < n) {
        char c = text[pos];
        bool lineStart = pos == 0 || text[pos - 1] == '\n';
        if (lineStart) {
            size_t p = text.find_first_not_of(" \t", pos);
            if (p != std::string::npos && p < n && text[p] == '[') {
                size_t close = text.find(']', p), eol = text.find('\n', p);
                if (close != std::string::npos && close < eol) {
                    std::string head = text.substr(p + 1, close - p - 1);
                    std::vector<std::string> parts;
                    size_t a = 0;
                    while (a <= head.size()) {
                        size_t b = head.find(';', a);
                        if (b == std::string::npos) b = head.size();
                        parts.push_back(Trim(head.substr(a, b - a)));
                        a = b + 1;
                    }
                    std::string low = Lower(parts[0]);
                    int line = lineOf(p);
                    if (low.compare(0, 8, "statedef") == 0 || low.compare(0, 8, "function") == 0) {
                        CnsState state;
                        state.line = line;
                        if (low[0] == 's') {
                            state.no = StateNo(parts[0].substr(8), state.literal);
                        } else {
                            std::string f = Trim(parts[0].substr(8));
                            state.function = f.substr(0, f.find('('));
                            state.literal = false;
                        }
                        for (size_t k = 1; k < parts.size(); k++) {
                            size_t colon = parts[k].find(':');
                            if (colon == std::string::npos) continue;
                            state.params.push_back({Lower(Trim(parts[k].substr(0, colon))), Trim(parts[k].substr(colon + 1)), line});
                        }
                        out.states.push_back(state);
                        st = (int)out.states.size() - 1;
                    } else {
                        st = -1;
                    }
                    for (int l = line; l < (int)stateOfLine.size(); l++) stateOfLine[l] = st;
                    pos = close + 1;
                    continue;
                }
            }
        }
        if (c == '"') {
            size_t q = text.find('"', pos + 1);
            pos = q == std::string::npos ? n : q + 1;
            continue;
        }
        if (IdentStart(c) && (pos == 0 || !IdentChar(text[pos - 1]))) {
            size_t e = pos;
            while (e < n && IdentChar(text[e])) e++;
            std::string word = Lower(text.substr(pos, e - pos));
            size_t j = text.find_first_not_of(" \t\r\n", e);
            if (word == "persistent" && j != std::string::npos && text[j] == '(') {
                size_t close = text.find(')', j);
                j = close == std::string::npos ? n : text.find_first_not_of(" \t\r\n", close + 1);
            }
            // A controller starts a statement: after a brace, a ";" or a header (not in "if !alive {")
            size_t before = text.find_last_not_of(" \t\r\n", pos == 0 ? 0 : pos - 1);
            bool statement = pos == 0 || before == std::string::npos ||
                             std::string("{};]").find(text[before]) != std::string::npos;
            if (j == std::string::npos || text[j] != '{' || BlockWord(word) || st < 0 || !statement) {
                pos = e;
                continue;
            }
            // A controller: its pairs up to the matching brace
            CnsCtrl ctrl;
            ctrl.type = word;
            ctrl.line = lineOf(pos);
            ctrl.hasTrigger = true;
            int depth = 0;
            bool quoted = false;
            size_t k = j + 1, itemStart = k;
            auto item = [&](size_t a, size_t b) {
                std::string s = text.substr(a, b - a);
                size_t first = s.find_first_not_of(" \t\r\n");
                if (first == std::string::npos) return;
                int d = 0;
                for (size_t i = 0; i < s.size(); i++) {
                    char ch = s[i];
                    if (ch == '(' || ch == '[') d++;
                    else if (ch == ')' || ch == ']') d--;
                    else if (ch == ':' && d == 0 && (i + 1 >= s.size() || s[i + 1] != '=')) {
                        ctrl.params.push_back({Lower(Trim(s.substr(0, i))), Trim(s.substr(i + 1)), lineOf(a + first)});
                        return;
                    }
                }
            };
            for (; k < n; k++) {
                char ch = text[k];
                if (ch == '"') quoted = !quoted;
                if (quoted) continue;
                if (ch == '(' || ch == '[' || ch == '{') depth++;
                else if ((ch == ')' || ch == ']' || ch == '}') && depth > 0) depth--;
                else if (ch == '}') break;
                else if (ch == ';' && depth == 0) {
                    item(itemStart, k);
                    itemStart = k + 1;
                }
            }
            item(itemStart, std::min(k, n));
            out.states[st].ctrls.push_back(ctrl);
            pos = k + 1;
            continue;
        }
        pos++;
    }
    for (size_t i = 0; i < lines.size(); i++) {
        std::string s = Trim(StripComment(lines[i], '#'));
        int s0 = stateOfLine[i + 1];
        if (!s.empty() && s0 >= 0 && s[0] != '[') out.code.push_back({(int)i + 1, s0, s});
    }
}

} // namespace

const CnsParam *CnsCtrl::Param(const char *key) const {
    for (const auto &p : params)
        if (p.key == key) return &p;
    return nullptr;
}

const CnsParam *CnsState::Param(const char *key) const {
    for (const auto &p : params)
        if (p.key == key) return &p;
    return nullptr;
}

bool Cns_IsZss(const std::string &path) {
    return path.size() >= 4 && Lower(path.substr(path.size() - 4)) == ".zss";
}

void Cns_Parse(const std::string &text, bool zss, CnsFile &out) {
    out = CnsFile();
    out.zss = zss;
    std::vector<std::string> lines = Lines(text);
    if (zss)
        ParseZss(lines, out);
    else
        ParseCns(lines, out);
}

bool Cns_Numbers(const std::string &value, CnsNumbers &out) {
    out = CnsNumbers();
    std::string s = Trim(value);
    if (!s.empty() && (toupper((unsigned char)s[0]) == 'S' || toupper((unsigned char)s[0]) == 'F') && s.size() > 1 &&
        (isdigit((unsigned char)s[1]) || s[1] == '-')) {
        out.prefix = (char)toupper((unsigned char)s[0]);
        s = s.substr(1);
    }
    size_t a = 0;
    while (a <= s.size()) {
        size_t b = s.find(',', a);
        if (b == std::string::npos) b = s.size();
        std::string part = Trim(s.substr(a, b - a));
        char *end = nullptr;
        long v = strtol(part.c_str(), &end, 10);
        if (part.empty() || !end || *end != 0) return false;
        out.n.push_back((int)v);
        a = b + 1;
    }
    return !out.n.empty();
}
