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

std::string Ini_SpritesFile(const std::string &text) {
    IniFile ini;
    Ini_Parse(text, ini);
    for (const IniSection &s : ini.sections) {
        const IniField *f = nullptr;
        if (s.lower == "files") f = s.Field("spr") ? s.Field("spr") : s.Field("sff");
        else if (s.lower == "bgdef" || s.lower == "scenedef") f = s.Field("spr");
        if (!f || f->value.empty()) continue;
        std::string v = f->value;
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
        return v;
    }
    return "";
}

// The first word of a section's name, as Ikemen's SectionName cuts it (at the first space).
static std::string FirstWord(const std::string &lower) { return lower.substr(0, lower.find(' ')); }

// The motif's fields the scripts look up in the character shown (start.f_animGet): its portrait on the
// select screen, its animation on the versus screen; not numbers of this def. A portrait's random and
// slot pictures are the motif's own.
static bool OfTheCharacter(const IniSection &s, const std::string &key) {
    bool select = s.lower == "select info", versus = s.lower == "vs screen" || s.lower == "victory screen";
    size_t dot = key.find('.');
    if ((!select && !versus) || key.size() < 3 || key[0] != 'p' || !isdigit((unsigned char)key[1]) || dot == std::string::npos) return false;
    std::string rest = key.substr(dot + 1);
    if (rest.find(".random.") != std::string::npos || rest.find(".slot.") != std::string::npos) return false;
    if (rest.compare(0, 4, "face") == 0 || rest.compare(0, 15, "palmenu.preview") == 0) return true;
    return versus && (rest == "anim" || rest == "spr" || rest == "done.anim" || rest == "done.spr");
}

IniRefKind Ini_FieldRef(const IniSection &s, const IniField &f) {
    if (s.lower == "files" || s.lower == "bgdef" || s.lower == "scenedef" || s.lower == "music" ||
        s.lower.compare(0, 13, "begin action ") == 0 || OfTheCharacter(s, f.key))
        return kIniRefNone;
    auto named = [&](const char *name) {
        std::string k = name, tail = "." + k;
        return f.key == k || (f.key.size() > tail.size() && f.key.compare(f.key.size() - tail.size(), tail.size(), tail) == 0);
    };
    if (named("anim") || f.key == "actionno") return kIniRefAction;
    if (named("spr") || f.key == "spriteno") return kIniRefSprite;
    if (named("snd")) return kIniRefSound;
    if (f.key == "value" && FirstWord(s.lower) == "bgctrl") {
        const IniField *type = s.Field("type");
        if (type && Lower(type->value) == "anim") return kIniRefAction;
    }
    return kIniRefNone;
}

std::vector<std::pair<int, int>> Ini_ActionUsers(const IniFile &ini, int action) {
    std::vector<std::pair<int, int>> out;
    for (size_t i = 0; i < ini.sections.size(); i++)
        for (size_t k = 0; k < ini.sections[i].fields.size(); k++) {
            const IniField &f = ini.sections[i].fields[k];
            if (!f.value.empty() && Ini_FieldRef(ini.sections[i], f) == kIniRefAction && atoi(f.value.c_str()) == action)
                out.push_back({(int)i, (int)k});
        }
    return out;
}

std::vector<int> IniFile::StageBgs() const {
    std::vector<int> out;
    for (size_t i = 0; i < sections.size(); i++)
        if (FirstWord(sections[i].lower) == "bg") out.push_back((int)i);
    return out;
}

std::vector<int> IniFile::StageBgCtrls() const {
    std::vector<int> out;
    for (size_t i = 0; i < sections.size(); i++) {
        std::string w = FirstWord(sections[i].lower);
        if (w == "bgctrl" || w == "bgctrl3d") out.push_back((int)i);
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

// A fight screen's key can start with a selector the engine resolves for each player or team
// (resolveLifebarPrefixOperators): "p*" (every p<n>), "p1|p3" (those). True when sel is first
// ("p1") or such a selector of it.
bool Ini_SelectorMatches(const std::string &sel, const std::string &first) {
    if (sel == first) return true;
    size_t d = first.find_first_of("0123456789");
    if (d == 0 || d == std::string::npos) return false;
    if (sel == first.substr(0, d) + "*") return true;
    if (sel.find('|') == std::string::npos) return false;
    for (size_t start = 0;;) {
        size_t bar = sel.find('|', start);
        if (sel.substr(start, bar == std::string::npos ? std::string::npos : bar - start) == first) return true;
        if (bar == std::string::npos) return false;
        start = bar + 1;
    }
}

// Whether a key of the def applies to an element with that prefix ("p*.bg0.anim" to "p1.bg0.").
bool Ini_KeyApplies(const std::string &key, const std::string &prefix) {
    size_t kd = key.find('.'), pd = prefix.find('.');
    if (kd == std::string::npos || pd == std::string::npos) return key.compare(0, prefix.size(), prefix) == 0;
    return Ini_SelectorMatches(key.substr(0, kd), prefix.substr(0, pd)) &&
           key.compare(kd, prefix.size() - pd, prefix, pd, std::string::npos) == 0;
}

// The key of the def that gives a key ("p1.pos") its value: itself, else one with a selector
// ("p1|p3.pos", then "p*.pos"); "" when the section has none. Fight screens only.
std::string Ini_DefKeyFor(const IniSection *s, const std::string &key) {
    if (!s) return "";
    if (s->Field(key.c_str())) return key;
    size_t dot = key.find('.');
    if (dot == std::string::npos) return "";
    std::string group, wildcard;
    for (const IniField &f : s->fields) {
        size_t fd = f.key.find('.');
        if (fd == std::string::npos || f.key.compare(fd, std::string::npos, key, dot, std::string::npos) != 0) continue;
        std::string sel = f.key.substr(0, fd);
        if (sel == key.substr(0, dot) || !Ini_SelectorMatches(sel, key.substr(0, dot))) continue;
        (sel.back() == '*' ? wildcard : group) = f.key;
    }
    return !group.empty() ? group : wildcard;
}
