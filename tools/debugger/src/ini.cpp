#include "ini.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

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

std::string Trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::string Clean(const std::string &line) { return Trim(line.substr(0, line.find(';'))); }

std::string Lower(std::string s) {
    for (char &ch : s) ch = (char)tolower((unsigned char)ch);
    return s;
}

std::string Indent(const std::string &line) { return line.substr(0, line.find_first_not_of(" \t")); }

bool InRange(const std::vector<std::string> &lines, const IniSection &s) {
    if (s.line < 1 || s.last < s.line || s.last > (int)lines.size()) return false;
    std::string head = Clean(lines[s.line - 1]);
    return head.size() >= 2 && head[0] == '[' && Lower(Trim(head.substr(1, head.find(']') - 1))) == s.lower;
}

} // namespace

const IniField *IniSection::Field(const char *key) const {
    const IniField *found = nullptr;
    for (const IniField &f : fields)
        if (f.key == key) found = &f;
    return found;
}

bool IniSection::Floats(const char *key, float *out, int count) const {
    for (int i = 0; i < count; i++) out[i] = 0;
    const IniField *f = Field(key);
    if (!f) return false;
    const char *p = f->value.c_str();
    for (int i = 0; i < count && *p; i++) {
        char *end;
        out[i] = strtof(p, &end);
        p = end;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != ',') break;
        p++;
    }
    return true;
}

std::vector<int> IniFile::StageBgs() const {
    std::vector<int> out;
    for (size_t i = 0; i < sections.size(); i++) {
        const std::string &n = sections[i].lower;
        size_t space = n.find_first_of(" \t");
        if (space != std::string::npos && n.compare(0, space, "bg") == 0) out.push_back((int)i);
    }
    return out;
}

void Ini_Parse(const std::string &text, IniFile &out) {
    out = IniFile();
    std::vector<std::string> lines = SplitLines(text);
    IniSection *cur = nullptr;
    for (size_t i = 0; i < lines.size(); i++) {
        std::string s = Clean(lines[i]);
        if (s.empty()) continue;
        if (s[0] == '[') {
            size_t close = s.find(']');
            out.sections.emplace_back();
            cur = &out.sections.back();
            cur->name = Trim(s.substr(1, close == std::string::npos ? std::string::npos : close - 1));
            cur->lower = Lower(cur->name);
            cur->line = cur->last = (int)i + 1;
            continue;
        }
        size_t eq = s.find('=');
        if (!cur || eq == std::string::npos) continue;
        IniField f;
        f.key = Lower(Trim(s.substr(0, eq)));
        f.value = Trim(s.substr(eq + 1));
        f.line = (int)i + 1;
        cur->fields.push_back(f);
        cur->last = f.line;
    }
}

bool Ini_SetField(std::string &text, const IniSection &s, const char *key, const std::string &value) {
    std::vector<std::string> lines = SplitLines(text);
    if (!InRange(lines, s)) return false;
    if (const IniField *f = s.Field(key)) {
        if (f->line < 1 || f->line > (int)lines.size()) return false;
        if (value.empty()) {
            lines.erase(lines.begin() + (f->line - 1));
        } else {
            // The spacing around "=" as it was, the comment kept
            const std::string &old = lines[f->line - 1];
            size_t eq = old.find('=');
            if (eq == std::string::npos) return false;
            size_t v = old.find_first_not_of(" \t", eq + 1);
            if (v == std::string::npos) v = old.size();
            size_t semi = old.find(';', v);
            std::string comment;
            if (semi != std::string::npos) {
                size_t sp = old.find_last_not_of(" \t", semi - 1);
                comment = old.substr(sp == std::string::npos || sp < v ? semi : sp + 1);
            }
            lines[f->line - 1] = old.substr(0, v) + value + comment;
        }
    } else {
        if (value.empty()) return true;
        std::string indent = s.fields.empty() ? "" : Indent(lines[s.fields.back().line - 1]);
        lines.insert(lines.begin() + s.last, indent + key + " = " + value);
    }
    text = JoinLines(lines);
    return true;
}

std::string Ini_Floats(const float *v, int count) {
    std::string out;
    for (int i = 0; i < count; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%g", v[i]);
        out += (i ? ", " : "") + std::string(buf);
    }
    return out;
}
