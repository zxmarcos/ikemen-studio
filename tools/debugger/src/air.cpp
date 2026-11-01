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

std::string Trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// The alphas after the letters of a blending field, "128d64": the defaults stay when absent
void ReadAlphas(const std::string &a, size_t i, int defSrc, int defDst, AirStyle &st) {
    st.src = defSrc, st.dst = defDst;
    auto number = [&](int &out) {
        if (i >= a.size() || !isdigit((unsigned char)a[i])) return;
        long n = 0;
        for (; i < a.size() && isdigit((unsigned char)a[i]); i++) n = std::min(n * 10 + (a[i] - '0'), 99999L);
        out = (int)std::min(n, 255L);
    };
    number(st.src);
    if (i < a.size() && a[i] == 'd') {
        i++;
        number(st.dst);
    }
}

// The fields after the fifth value (lower case, ", H, AS128D64, 1.5, 1.5, 30"): as ReadAnimFrame does
void ReadStyle(const char *rest, AirStyle &st) {
    std::vector<std::string> f;
    for (const char *p = rest; *p == ',' && f.size() < 5;) {
        const char *end = strchr(p + 1, ',');
        f.push_back(Trim(end ? std::string(p + 1, end) : std::string(p + 1)));
        p = end ? end : p + strlen(p);
    }
    if (f.size() > 0)
        for (char ch : f[0]) {
            if (ch == 'h') st.flipH = true;
            if (ch == 'v') st.flipV = true;
        }
    if (f.size() > 1 && !f[1].empty()) {
        size_t ia = f[1].find_first_of("as");
        std::string a = ia == std::string::npos ? "" : f[1].substr(ia);
        if (a == "a1") st.blend = kAirAdd, st.src = 255, st.dst = 128;
        else if (a.compare(0, 3, "sas") == 0) st.blend = kAirSubAdd, ReadAlphas(a, 3, 255, 255, st);
        else if (a.compare(0, 2, "sa") == 0) st.blend = kAirSubAdd, st.src = st.dst = 255;
        else if (a.compare(0, 2, "as") == 0) st.blend = kAirAdd, ReadAlphas(a, 2, 255, 0, st);
        else if (a.compare(0, 2, "ss") == 0) st.blend = kAirSub, ReadAlphas(a, 2, 255, 255, st);
        else if (a.compare(0, 1, "a") == 0) st.blend = kAirAdd, st.src = st.dst = 255;
        else if (a.compare(0, 1, "s") == 0) st.blend = kAirSub, st.src = st.dst = 255;
    }
    if (f.size() > 2 && !f[2].empty()) st.xscale = (float)atof(f[2].c_str());
    if (f.size() > 3 && !f[3].empty()) st.yscale = (float)atof(f[3].c_str());
    if (f.size() > 4 && !f[4].empty()) st.angle = (float)atof(f[4].c_str());
}

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
    ReadStyle(p, e.style);
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
            e.line = e.first = (int)i + 1;
            for (int g = 0; g < 2; g++) {
                if (pendingHeader[g]) {
                    e.clsn[g] = pending[g];
                    e.header[g] = pendingHeader[g];
                    e.own[g] = pendingOwn[g];
                    if (e.own[g]) e.first = std::min(e.first, pendingHeader[g]);
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

std::string Air_BlendText(const AirStyle &st) {
    char buf[24];
    bool full = st.src == 255 && st.dst == 255;
    switch (st.blend) {
    case kAirAdd:
        if (full) return "A";
        if (st.src == 255 && st.dst == 128) return "A1";
        snprintf(buf, sizeof(buf), "AS%dD%d", st.src, st.dst); // Mugen's default destination here is 0: always written
        return buf;
    case kAirSub:
        if (full) return "S";
        snprintf(buf, sizeof(buf), "SS%dD%d", st.src, st.dst);
        return buf;
    case kAirSubAdd:
        if (full) return "SA";
        snprintf(buf, sizeof(buf), "SAS%dD%d", st.src, st.dst);
        return buf;
    }
    return "";
}

bool Air_SetStyle(std::string &text, const AirElem &elem, const AirStyle &st) {
    std::vector<std::string> lines = SplitLines(text);
    if (elem.line < 1 || elem.line > (int)lines.size()) return false;
    std::string &line = lines[elem.line - 1];
    size_t semi = line.find(';');
    std::string code = line.substr(0, semi), comment = semi == std::string::npos ? "" : line.substr(semi);
    // The five values stay as they are
    size_t end = 0;
    for (int commas = 0; end < code.size(); end++)
        if (code[end] == ',' && ++commas == 5) break;
    code = code.substr(0, end);
    while (!code.empty() && (code.back() == ' ' || code.back() == '\t' || code.back() == '\r')) code.pop_back();
    auto num = [](float v) {
        char b[24];
        snprintf(b, sizeof(b), "%g", v);
        return std::string(b);
    };
    std::string f[5] = {std::string(st.flipH ? "H" : "") + (st.flipV ? "V" : ""), Air_BlendText(st),
                        st.xscale == 1 && st.yscale == 1 ? "" : num(st.xscale), st.xscale == 1 && st.yscale == 1 ? "" : num(st.yscale),
                        st.angle == 0 ? "" : num(st.angle)};
    int n = 5;
    while (n > 0 && f[n - 1].empty()) n--;
    for (int i = 0; i < n; i++) code += (i ? ", " : ", ") + f[i];
    line = code + (comment.empty() ? "" : " " + comment);
    text = JoinLines(lines);
    return true;
}

bool Air_DuplicateElem(std::string &text, const AirAction &act, int index) { return Air_DuplicateElems(text, act, {index}); }

bool Air_DeleteElem(std::string &text, const AirAction &act, int index) { return Air_DeleteElems(text, act, {index}); }

bool Air_DuplicateElems(std::string &text, const AirAction &act, const std::vector<int> &indices) {
    std::vector<std::string> lines = SplitLines(text), copies;
    if (indices.empty()) return false;
    for (int index : indices) {
        if (index < 0 || index >= (int)act.elems.size()) return false;
        const AirElem &e = act.elems[index];
        if (e.line > (int)lines.size()) return false;
        copies.insert(copies.end(), lines.begin() + e.first - 1, lines.begin() + e.line);
    }
    lines.insert(lines.begin() + act.elems[indices.back()].line, copies.begin(), copies.end());
    text = JoinLines(lines);
    return true;
}

bool Air_DeleteElems(std::string &text, const AirAction &act, const std::vector<int> &indices) {
    std::vector<std::string> lines = SplitLines(text);
    if (indices.empty()) return false;
    for (int index : indices)
        if (index < 0 || index >= (int)act.elems.size() || act.elems[index].line > (int)lines.size()) return false;
    for (size_t i = indices.size(); i-- > 0;) { // from the last: the lines of those before stay where they are
        const AirElem &e = act.elems[indices[i]];
        lines.erase(lines.begin() + e.first - 1, lines.begin() + e.line);
    }
    text = JoinLines(lines);
    return true;
}

bool Air_MoveElem(std::string &text, const AirAction &act, int index, int dir) {
    int other = index + (dir < 0 ? -1 : 1);
    if (index < 0 || index >= (int)act.elems.size() || other < 0 || other >= (int)act.elems.size()) return false;
    const AirElem &a = act.elems[std::min(index, other)], &b = act.elems[std::max(index, other)];
    std::vector<std::string> lines = SplitLines(text);
    if (b.line > (int)lines.size()) return false;
    // lines: ... [a.first, a.line] gap [b.first, b.line] ...  ->  ... b gap a ...
    std::vector<std::string> blockA(lines.begin() + a.first - 1, lines.begin() + a.line);
    std::vector<std::string> gap(lines.begin() + a.line, lines.begin() + b.first - 1);
    std::vector<std::string> blockB(lines.begin() + b.first - 1, lines.begin() + b.line);
    std::vector<std::string> mid;
    mid.insert(mid.end(), blockB.begin(), blockB.end());
    mid.insert(mid.end(), gap.begin(), gap.end());
    mid.insert(mid.end(), blockA.begin(), blockA.end());
    lines.erase(lines.begin() + a.first - 1, lines.begin() + b.line);
    lines.insert(lines.begin() + a.first - 1, mid.begin(), mid.end());
    text = JoinLines(lines);
    return true;
}

bool Air_SetLoopStart(std::string &text, const AirAction &act, int index) {
    std::vector<std::string> lines = SplitLines(text);
    if (act.last > (int)lines.size()) return false;
    int at = index >= 0 && index < (int)act.elems.size() ? act.elems[index].first - 1 : -1;
    for (int i = act.last - 1; i >= act.line; i--)
        if (StartsWith(Clean(lines[i]), "loopstart")) {
            lines.erase(lines.begin() + i);
            if (at > i) at--;
        }
    if (at >= 0) lines.insert(lines.begin() + at, Indent(lines[at]) + "Loopstart");
    text = JoinLines(lines);
    return true;
}

bool Air_CopyAction(std::string &text, const AirAction &act, int newNo) {
    std::vector<std::string> lines = SplitLines(text);
    if (act.last > (int)lines.size()) return false;
    while (!lines.empty() && lines.back().find_first_not_of(" \t\r") == std::string::npos) lines.pop_back();
    lines.push_back("");
    lines.push_back("[Begin Action " + std::to_string(newNo) + "]");
    for (int i = act.line; i < act.last; i++) lines.push_back(lines[i]);
    lines.push_back("");
    text = JoinLines(lines);
    return true;
}

bool Air_DeleteAction(std::string &text, const AirAction &act) {
    std::vector<std::string> lines = SplitLines(text);
    if (act.last > (int)lines.size()) return false;
    lines.erase(lines.begin() + act.line - 1, lines.begin() + act.last);
    text = JoinLines(lines);
    return true;
}

bool Air_RenumberAction(std::string &text, const AirAction &act, int newNo) {
    std::vector<std::string> lines = SplitLines(text);
    if (act.line > (int)lines.size()) return false;
    std::string &line = lines[act.line - 1];
    size_t open = line.find('['), close = line.find(']');
    if (open == std::string::npos || close == std::string::npos) return false;
    line = line.substr(0, open) + "[Begin Action " + std::to_string(newNo) + "]" + line.substr(close + 1);
    text = JoinLines(lines);
    return true;
}