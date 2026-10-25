#include "air.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

// The line without its comment, trimmed and in lower case.
std::string Clean(const std::string &line) {
    std::string s = line.substr(0, line.find(';'));
    size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    if (a == std::string::npos) return "";
    s = s.substr(a, b - a + 1);
    for (char &ch : s) ch = (char)tolower((unsigned char)ch);
    return s;
}

bool StartsWith(const std::string &s, const char *prefix) { return s.compare(0, strlen(prefix), prefix) == 0; }

// "group, image, x, y, time[, ...]": an element line
bool ReadElem(const std::string &s, AirElem &e) {
    if (s.empty() || !(isdigit((unsigned char)s[0]) || s[0] == '-')) return false;
    int v[5];
    const char *p = s.c_str();
    for (int n = 0; n < 5; n++) {
        char *end;
        v[n] = (int)strtol(p, &end, 10);
        if (end == p && n < 2) return false;
        p = end;
        while (*p == ' ' || *p == '\t') p++;
        if (n < 4) {
            if (*p != ',') return n >= 4;
            p++;
        }
    }
    e.group = v[0], e.image = v[1], e.x = v[2], e.y = v[3], e.time = v[4];
    return true;
}

// "clsnN[i] = x1, y1, x2, y2" (the index is optional)
bool ReadBox(const std::string &s, AirBox &b) {
    if (!StartsWith(s, "clsn")) return false;
    size_t eq = s.find('=');
    if (eq == std::string::npos) return false;
    int v[4];
    const char *p = s.c_str() + eq + 1;
    for (int n = 0; n < 4; n++) {
        char *end;
        v[n] = (int)strtol(p, &end, 10);
        if (end == p) return false;
        p = end;
        while (*p == ' ' || *p == '\t') p++;
        if (n < 3) {
            if (*p != ',') return false;
            p++;
        }
    }
    b.x1 = std::min(v[0], v[2]), b.x2 = std::max(v[0], v[2]);
    b.y1 = std::min(v[1], v[3]), b.y2 = std::max(v[1], v[3]);
    return true;
}

// Reads the boxes after a "ClsnN: n" header at index i (0-based); returns the index of the last one read.
size_t ReadGroup(const std::vector<std::string> &lines, size_t i, int count, std::vector<AirBox> &out) {
    out.clear();
    size_t last = i;
    for (size_t k = i + 1; k < lines.size() && (int)out.size() < count; k++) {
        std::string s = Clean(lines[k]);
        if (s.empty()) continue;
        AirBox b;
        if (!ReadBox(s, b)) break;
        b.line = (int)k + 1;
        out.push_back(b);
        last = k;
    }
    return last;
}

std::string Indent(const std::string &line) { return line.substr(0, line.find_first_not_of(" \t")); }

} // namespace

void Air_Parse(const std::string &text, AirFile &out) {
    out.actions.clear();
    std::vector<std::string> lines = SplitLines(text);
    AirAction *act = nullptr;
    std::vector<AirBox> pending[2], deflt[2];
    int pendingHeader[2] = {}, defaultHeader[2] = {};
    bool pendingOwn[2] = {};
    for (size_t i = 0; i < lines.size(); i++) {
        std::string s = Clean(lines[i]);
        if (s.empty()) continue;
        if (s[0] == '[') {
            act = nullptr;
            if (StartsWith(s, "[begin action")) {
                int no = atoi(s.c_str() + 13);
                act = &out.actions[no];
                *act = AirAction();
                act->no = no;
                act->line = act->last = (int)i + 1;
                for (int g = 0; g < 2; g++) {
                    pending[g].clear(), deflt[g].clear();
                    pendingHeader[g] = defaultHeader[g] = 0;
                    pendingOwn[g] = false;
                }
            }
            continue;
        }
        if (!act) continue;
        act->last = (int)i + 1;
        if (StartsWith(s, "loopstart")) {
            act->loopStart = (int)act->elems.size();
        } else if (StartsWith(s, "clsn") && s.size() > 5 && (s[4] == '1' || s[4] == '2') && s.find(':') != std::string::npos) {
            int g = s[4] - '1';
            bool isDefault = s.compare(5, 7, "default") == 0;
            int count = atoi(s.c_str() + s.find(':') + 1);
            std::vector<AirBox> boxes;
            size_t last = ReadGroup(lines, i, count, boxes);
            pending[g] = boxes;
            pendingHeader[g] = (int)i + 1;
            pendingOwn[g] = !isDefault;
            if (isDefault) {
                deflt[g] = boxes;
                defaultHeader[g] = (int)i + 1;
            }
            i = last;
            act->last = (int)i + 1;
        } else {
            AirElem e;
            if (!ReadElem(s, e)) continue;
            e.line = (int)i + 1;
            for (int g = 0; g < 2; g++) {
                if (pendingHeader[g]) {
                    e.clsn[g] = pending[g];
                    e.header[g] = pendingHeader[g];
                    e.own[g] = pendingOwn[g];
                } else {
                    e.clsn[g] = deflt[g];
                    e.header[g] = defaultHeader[g];
                    e.own[g] = false;
                }
                pendingHeader[g] = 0;
            }
            act->elems.push_back(e);
        }
    }
}

bool Air_SetBox(std::string &text, const AirBox &box, int x1, int y1, int x2, int y2) {
    std::vector<std::string> lines = SplitLines(text);
    if (box.line < 1 || box.line > (int)lines.size()) return false;
    std::string &line = lines[box.line - 1];
    size_t eq = line.find('=');
    if (eq == std::string::npos) return false;
    size_t semi = line.find(';', eq);
    std::string comment = semi == std::string::npos ? "" : "  " + line.substr(semi);
    char buf[96];
    snprintf(buf, sizeof(buf), " %d, %d, %d, %d", x1, y1, x2, y2);
    line = line.substr(0, eq + 1) + buf + comment;
    text = JoinLines(lines);
    return true;
}

bool Air_SetOwnBoxes(std::string &text, const AirElem &elem, int group, const std::vector<AirBox> &boxes) {
    std::vector<std::string> lines = SplitLines(text);
    if (elem.line < 1 || elem.line > (int)lines.size() || group < 0 || group > 1) return false;
    int at = elem.line - 1; // insert before the element
    if (elem.own[group] && elem.header[group] > 0) {
        // Replace the element's own group: its header and box lines
        int first = elem.header[group] - 1, last = first;
        for (const auto &b : elem.clsn[group]) last = std::max(last, b.line - 1);
        lines.erase(lines.begin() + first, lines.begin() + last + 1);
        at = first;
    }
    std::string indent = Indent(lines[std::min(at, (int)lines.size() - 1)]);
    std::vector<std::string> add;
    char buf[96];
    snprintf(buf, sizeof(buf), "Clsn%d: %d", group + 1, (int)boxes.size());
    add.push_back(indent + buf);
    for (size_t n = 0; n < boxes.size(); n++) {
        const AirBox &b = boxes[n];
        snprintf(buf, sizeof(buf), "  Clsn%d[%d] = %d, %d, %d, %d", group + 1, (int)n, b.x1, b.y1, b.x2, b.y2);
        add.push_back(indent + buf);
    }
    lines.insert(lines.begin() + at, add.begin(), add.end());
    text = JoinLines(lines);
    return true;
}

bool Air_SetElem(std::string &text, const AirElem &elem, int group, int image, int x, int y, int time) {
    std::vector<std::string> lines = SplitLines(text);
    if (elem.line < 1 || elem.line > (int)lines.size()) return false;
    std::string &line = lines[elem.line - 1];
    // Keep what follows the fifth value (flip, blending, scale, angle) and the comment
    size_t pos = line.find_first_not_of(" \t");
    if (pos == std::string::npos) return false;
    size_t end = pos;
    int commas = 0;
    while (end < line.size() && line[end] != ';') {
        if (line[end] == ',' && ++commas == 5) break;
        end++;
    }
    if (commas < 5) {
        // Only five values: keep a trailing comment
        while (end > pos && (line[end - 1] == ' ' || line[end - 1] == '\t')) end--;
    }
    char buf[96];
    snprintf(buf, sizeof(buf), "%d,%d, %d,%d, %d", group, image, x, y, time);
    line = line.substr(0, pos) + buf + line.substr(end);
    text = JoinLines(lines);
    return true;
}
