#include "cmd.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>

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

// The line without its comment, trimmed.
std::string Clean(const std::string &line) { return Trim(line.substr(0, line.find(';'))); }

std::string Lower(std::string s) {
    for (char &ch : s) ch = (char)tolower((unsigned char)ch);
    return s;
}

std::string Unquote(const std::string &s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') return s.substr(1, s.size() - 2);
    return s;
}

std::string Indent(const std::string &line) { return line.substr(0, line.find_first_not_of(" \t")); }

// The comment of a line, with the spaces before it ("" when it has none).
std::string Comment(const std::string &line) {
    size_t semi = line.find(';');
    if (semi == std::string::npos) return "";
    size_t start = line.find_last_not_of(" \t", semi == 0 ? 0 : semi - 1);
    return line.substr(start == std::string::npos || semi == 0 ? semi : start + 1);
}

// "key = value" of a field line, as the lines of the section write it.
std::string FieldLine(const std::string &indent, const std::string &key, const std::string &value) {
    return indent + key + " = " + value;
}

bool InRange(const std::vector<std::string> &lines, const CmdCommand &c) {
    return c.line >= 1 && c.last >= c.line && c.last <= (int)lines.size() &&
           Lower(Clean(lines[c.line - 1])).compare(0, 8, "[command") == 0;
}

} // namespace

const CmdField *CmdCommand::Field(const char *key) const {
    for (const CmdField &f : fields)
        if (f.key == key) return &f;
    return nullptr;
}

int CmdCommand::Int(const char *key, int otherwise) const {
    const CmdField *f = Field(key);
    if (!f || f->value.empty()) return otherwise;
    char *end;
    long v = strtol(f->value.c_str(), &end, 10);
    return end == f->value.c_str() ? otherwise : (int)v;
}

void Cmd_Parse(const std::string &text, CmdFile &out) {
    out = CmdFile();
    std::vector<std::string> lines = SplitLines(text);
    std::map<std::string, int> seen;
    CmdCommand *cur = nullptr;
    bool defaults = false, defaultsRead = false;
    auto finish = [&] {
        if (!cur) return;
        if (const CmdField *f = cur->Field("name")) cur->name = Unquote(f->value);
        if (const CmdField *f = cur->Field("command")) cur->command = f->value;
        cur->index = seen[cur->name]++;
        cur = nullptr;
    };
    for (size_t i = 0; i < lines.size(); i++) {
        std::string s = Clean(lines[i]);
        if (s.empty()) continue;
        if (s[0] == '[') {
            finish();
            std::string name = Lower(Trim(s.substr(1, s.find(']') == std::string::npos ? std::string::npos : s.find(']') - 1)));
            defaults = false;
            if (name.compare(0, 7, "command") == 0) {
                out.commands.emplace_back();
                cur = &out.commands.back();
                cur->line = cur->last = (int)i + 1;
            } else if (name == "defaults" && !defaultsRead) {
                defaults = defaultsRead = true;
                out.defaultsLine = (int)i + 1;
            }
            continue;
        }
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        CmdField f;
        f.key = Lower(Trim(s.substr(0, eq)));
        f.value = Trim(s.substr(eq + 1));
        f.line = (int)i + 1;
        if (cur) {
            cur->fields.push_back(f);
            cur->last = f.line;
        } else if (defaults) {
            if (f.key == "command.time") out.time = atoi(f.value.c_str());
            else if (f.key == "command.buffer.time") out.bufferTime = std::max(1, atoi(f.value.c_str()));
        }
    }
    finish();
}

bool Cmd_SetField(std::string &text, const CmdCommand &c, const char *key, const std::string &value) {
    std::vector<std::string> lines = SplitLines(text);
    if (!InRange(lines, c)) return false;
    const CmdField *f = c.Field(key);
    if (f) {
        if (f->line < 1 || f->line > (int)lines.size()) return false;
        std::string &line = lines[f->line - 1];
        if (value.empty()) {
            lines.erase(lines.begin() + (f->line - 1));
        } else {
            // The spacing around "=" as it was, the comment kept
            std::string old = line;
            size_t eq = old.find('=');
            if (eq == std::string::npos) return false;
            size_t v = old.find_first_not_of(" \t", eq + 1);
            std::string head = old.substr(0, v == std::string::npos ? old.size() : v);
            line = head + value + Comment(old.substr(v == std::string::npos ? old.size() : v));
        }
    } else {
        if (value.empty()) return true;
        std::string indent = c.fields.empty() ? "" : Indent(lines[c.fields.back().line - 1]);
        lines.insert(lines.begin() + c.last, FieldLine(indent, key, value));
    }
    text = JoinLines(lines);
    return true;
}

bool Cmd_SetName(std::string &text, const CmdCommand &c, const std::string &name) {
    return Cmd_SetField(text, c, "name", "\"" + name + "\"");
}

bool Cmd_Add(std::string &text, const CmdFile &f, const CmdCommand *after, const std::string &name,
             const std::string &command) {
    std::vector<std::string> lines = SplitLines(text);
    std::vector<std::string> add = {"[Command]", "name = \"" + name + "\"", "command = " + command, "time = 15", ""};
    size_t at;
    if (after) {
        if (!InRange(lines, *after)) return false;
        at = after->last;
        add.insert(add.begin(), "");
        add.pop_back();
    } else if (!f.commands.empty()) {
        at = f.commands.front().line - 1;
    } else {
        at = lines.size();
        if (!lines.empty() && !Trim(lines.back()).empty()) add.insert(add.begin(), "");
        add.pop_back();
    }
    lines.insert(lines.begin() + at, add.begin(), add.end());
    text = JoinLines(lines);
    return true;
}

bool Cmd_Duplicate(std::string &text, const CmdCommand &c) {
    std::vector<std::string> lines = SplitLines(text);
    if (!InRange(lines, c)) return false;
    std::vector<std::string> copy(lines.begin() + (c.line - 1), lines.begin() + c.last);
    copy.insert(copy.begin(), "");
    lines.insert(lines.begin() + c.last, copy.begin(), copy.end());
    text = JoinLines(lines);
    return true;
}

bool Cmd_Delete(std::string &text, const CmdCommand &c) {
    std::vector<std::string> lines = SplitLines(text);
    if (!InRange(lines, c)) return false;
    size_t from = c.line - 1, to = c.last; // [from, to)
    // One blank line with it, so that blank lines do not pile up
    if (to < lines.size() && Trim(lines[to]).empty()) to++;
    else if (from > 0 && Trim(lines[from - 1]).empty()) from--;
    lines.erase(lines.begin() + from, lines.begin() + to);
    text = JoinLines(lines);
    return true;
}

const char *const kCmdDirs[9] = {"", "B", "DB", "D", "DF", "F", "UF", "U", "UB"};
const char kCmdButtons[] = "abcxyzsdwm";

namespace {

bool ParseStep(const std::string &text, CmdStep &s) {
    s = CmdStep();
    std::string t = Trim(text);
    size_t i = 0;
    for (; i < t.size(); i++) {
        char ch = t[i];
        if (ch == '>' && !s.strict) s.strict = true;
        else if (ch == '~' && !s.release) {
            s.release = true;
            while (i + 1 < t.size() && isdigit((unsigned char)t[i + 1])) s.charge = s.charge * 10 + (t[++i] - '0');
        } else if (ch == '/' && !s.hold) {
            s.hold = true;
            if (i + 1 < t.size() && isdigit((unsigned char)t[i + 1])) return false; // Ikemen's charged hold
        } else if (ch == '$' && !s.four) s.four = true;
        else if (ch != ' ' && ch != '\t') break;
    }
    std::string body = t.substr(i);
    if (body.empty() || body.find('|') != std::string::npos) return false;
    size_t a = 0;
    for (;;) {
        size_t b = body.find('+', a);
        std::string part = Trim(body.substr(a, b == std::string::npos ? std::string::npos : b - a));
        int d = 0;
        for (int k = 1; k <= 8; k++)
            if (part == kCmdDirs[k]) d = k;
        const char *btn = part.size() == 1 ? strchr(kCmdButtons, part[0]) : nullptr;
        if (d && !s.dir) s.dir = d;
        else if (btn && !(s.buttons & (1u << (btn - kCmdButtons)))) s.buttons |= 1u << (btn - kCmdButtons);
        else return false; // a prefix on a later key, N, L, R, two directions...
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return true;
}

} // namespace

std::vector<CmdStep> Cmd_Steps(const std::string &command) {
    std::vector<CmdStep> out;
    size_t a = 0;
    for (;;) {
        size_t b = command.find(',', a);
        std::string part = Trim(command.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (!part.empty()) {
            CmdStep s;
            if (!ParseStep(part, s)) {
                s = CmdStep();
                s.raw = part;
            }
            out.push_back(s);
        }
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return out;
}

std::string Cmd_StepText(const CmdStep &s) {
    if (!s.raw.empty()) return s.raw;
    std::string t;
    if (s.strict) t += '>';
    if (s.release) t += '~' + (s.charge > 0 ? std::to_string(s.charge) : "");
    if (s.hold) t += '/';
    if (s.four) t += '$';
    std::string body = kCmdDirs[s.dir];
    for (int i = 0; kCmdButtons[i]; i++)
        if (s.buttons & (1u << i)) body += (body.empty() ? "" : "+") + std::string(1, kCmdButtons[i]);
    return t + body;
}

std::string Cmd_StepsText(const std::vector<CmdStep> &steps) {
    std::string t;
    for (const CmdStep &s : steps) t += (t.empty() ? "" : ", ") + Cmd_StepText(s);
    return t;
}