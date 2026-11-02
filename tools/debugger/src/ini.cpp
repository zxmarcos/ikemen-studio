#include "ini.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

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
    if (FirstWord(s.lower) == "scene" && f.key.compare(0, 5, "sound") == 0 && named("value")) return kIniRefSound; // sound0.value
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

std::vector<int> IniFile::BgsOf(const std::string &group) const {
    std::vector<int> out;
    std::string word = Lower(group);
    for (size_t i = 0; i < sections.size(); i++)
        if (FirstWord(sections[i].lower) == word) out.push_back((int)i);
    return out;
}

std::string Ini_BgGroup(const IniFile &ini, const IniSection &s) {
    std::string word = FirstWord(s.lower);
    if (word == "scene") {
        const IniField *f = s.Field("bg.name");
        return f ? f->value : "";
    }
    // [BG0] is a BG as [BG0 Sky] is, when something names BG0
    for (const IniSection &o : ini.sections) {
        const IniField *f = o.Field("bg.name");
        if (o.lower == word + "def" || (f && FirstWord(o.lower) == "scene" && Lower(f->value) == word)) return s.name.substr(0, word.size());
    }
    if (word == s.lower && word != "scenedef" && word.size() > 3 && word.compare(word.size() - 3, 3, "def") == 0)
        return s.name.substr(0, s.name.size() - 3);
    return "";
}

int Ini_BgType(const IniSection &s) {
    const IniField *f = s.Field("type");
    if (!f || f->value.empty()) return -1;
    static const char kLetters[] = "napvd"; // as kIniBgTypes
    const char *at = strchr(kLetters, tolower((unsigned char)f->value[0]));
    return at ? (int)(at - kLetters) : -1;
}

std::vector<IniBgKey> Ini_BgMissingKeys(const IniSection &s) {
    static const std::vector<IniBgKey> kKeys[kIniBgTypes] = {
        {{"spriteno", "group, number", true}, {"mask", "1: colour 0 does not show"}, {"tile", "across, down: 1 for ever, n copies"}, {"tilespacing", "across, down"}},
        {{"actionno", "an action of this def", true}, {"tile", "across, down: 1 for ever, n copies"}, {"tilespacing", "across, down"}},
        {{"spriteno", "group, number", true}, {"mask", "1: colour 0 does not show"}, {"width", "top, bottom"}, {"yscalestart", "100"}, {"yscaledelta", "0"}, {"tile", "across: 1 for ever, n copies"}},
        {{"path", "a video file", true}, {"volume", "0 to 100"}, {"loop", "1: it starts over"}, {"scalemode", "how it fills the screen"}, {"scalefilter", "how it is scaled"}},
        {},
    };
    std::vector<IniBgKey> out;
    int type = Ini_BgType(s);
    if (type < 0) return out;
    bool action = s.Field("actionno") != nullptr;
    for (const IniBgKey &k : kKeys[type])
        if (!s.Field(k.key) && (action ? strcmp(k.key, "spriteno") != 0 && strcmp(k.key, "mask") != 0 : true)) out.push_back(k);
    if (type == kIniBgAnim && s.Field("spriteno")) // it shows that sprite, if not what an anim is for
        for (IniBgKey &k : out) k.needed = false;
    return out;
}

bool Ini_BgKeyRead(const IniSection &s, const std::string &key) {
    static const std::set<std::string> kOwn = {"spriteno", "actionno", "mask", "tile", "tilespacing", "width", "xscale", "yscalestart",
                                               "yscaledelta", "path", "volume", "loop", "scalemode", "scalefilter"};
    int type = Ini_BgType(s);
    if (type < 0 || !kOwn.count(key)) return true;
    const IniField *spr = s.Field("spriteno");
    bool sprite = spr && !spr->value.empty(), action = s.Field("actionno") != nullptr;
    bool tiling = key == "tile" || key == "tilespacing";
    switch (type) {
    case kIniBgNormal: return key == "spriteno" || tiling || (key == "actionno" && !sprite) || (key == "mask" && sprite);
    case kIniBgAnim: return key == "actionno" || tiling || ((key == "spriteno" || key == "mask") && !action);
    case kIniBgParallax:
        if (key == "spriteno" || key == "mask") return !action;
        if (key == "xscale") return !s.Field("width");
        return key == "actionno" || key == "tile" || key == "width" || key == "yscalestart" || key == "yscaledelta";
    case kIniBgVideo: return key == "path" || key == "volume" || key == "loop" || key == "scalemode" || key == "scalefilter";
    default: return false;
    }
}

const std::vector<const char *> *Ini_BgChoices(const std::string &key) {
    static const std::vector<const char *> kTypes = {"normal", "anim", "parallax", "video", "dummy"};
    static const std::vector<const char *> kTrans = {"default", "none", "add", "add1", "addalpha", "sub", "subadd"};
    static const std::vector<const char *> kModes = {"none", "stretch", "fit", "fitwidth", "fitheight", "zoomfill"};
    static const std::vector<const char *> kFilters = {"fastbilinear", "bilinear", "bicubic", "experimental", "neighbor", "area",
                                                       "bicublin", "gauss", "sinc", "lanczos", "spline"};
    return key == "type" ? &kTypes : key == "trans" ? &kTrans : key == "scalemode" ? &kModes : key == "scalefilter" ? &kFilters : nullptr;
}

std::vector<int> IniFile::StageBgCtrls() const {
    std::vector<int> out;
    for (size_t i = 0; i < sections.size(); i++) {
        std::string w = FirstWord(sections[i].lower);
        if (w == "bgctrl" || w == "bgctrl3d") out.push_back((int)i);
    }
    return out;
}

static bool IsComment(const std::string &line) {
    std::string t = Trim(line);
    return !t.empty() && t[0] == ';';
}

// A section's lines, 0-based [from, to): comments running into the next header are that one's.
static void SectionLines(const std::vector<std::string> &lines, const IniSection &s, int &from, int &to) {
    from = s.line - 1;
    while (from > 0 && IsComment(lines[from - 1])) from--;
    to = s.last;
    int end = to;
    while (end < (int)lines.size() && IsComment(lines[end])) end++;
    if (end == (int)lines.size() || Trim(lines[end]).empty()) to = end;
}

// Whether a BG follows another: the engine ignores positionlink on the first one.
static bool Follows(const IniFile &ini, const std::vector<int> &bgs, int bg) {
    const IniField *f = bg > 0 && bg < (int)bgs.size() ? ini.sections[bgs[bg]].Field("positionlink") : nullptr;
    return f && atoi(f->value.c_str()) != 0;
}

static int LastFollower(const IniFile &ini, const std::vector<int> &bgs, int bg) {
    while (Follows(ini, bgs, bg + 1)) bg++;
    return bg;
}

// The block goes in before lines[at], a blank line around it.
static void InsertBlock(std::vector<std::string> &lines, int at, std::vector<std::string> block) {
    while (at > 0 && Trim(lines[at - 1]).empty()) at--;
    block.insert(block.begin(), "");
    if (at < (int)lines.size() && !Trim(lines[at]).empty()) block.push_back("");
    lines.insert(lines.begin() + at, block.begin(), block.end());
}

static bool BgNameTaken(const IniFile &ini, const std::string &name) {
    for (const IniSection &s : ini.sections)
        if (s.lower == Lower(name)) return true;
    return false;
}

// "group base n", the first number from n that is free
static std::string NumberedBg(const IniFile &ini, const std::string &group, const std::string &base, int n) {
    for (;; n++) {
        std::string name = group + " " + base + (base.empty() ? "" : " ") + std::to_string(n);
        if (!BgNameTaken(ini, name)) return name;
    }
}

std::string Ini_NewBgName(const IniFile &ini, const std::string &base, const std::string &group) {
    std::string name;
    for (char ch : base)
        if (std::string("[];\n").find(ch) == std::string::npos) name += ch;
    name = Trim(name);
    if (name.empty()) name = "Layer";
    return BgNameTaken(ini, group + " " + name) ? NumberedBg(ini, group, name, 2) : group + " " + name;
}

int Ini_AddBg(std::string &text, const IniFile &ini, int after, const std::string &name, const std::string &body, const std::string &group) {
    std::vector<int> bgs = ini.BgsOf(group);
    std::vector<std::string> lines = SplitLines(text), block = SplitLines("[" + name + "]\n" + body);
    if (after < 0 || after >= (int)bgs.size()) after = (int)bgs.size() - 1;
    if (after >= 0) after = LastFollower(ini, bgs, after);
    int from, at = (int)lines.size();
    if (after >= 0) SectionLines(lines, ini.sections[bgs[after]], from, at);
    else
        for (const IniSection &s : ini.sections)
            if (s.lower == Lower(group) + "def") SectionLines(lines, s, from, at);
    InsertBlock(lines, at, block);
    text = JoinLines(lines);
    return after + 1;
}

int Ini_DuplicateBg(std::string &text, const IniFile &ini, int bg, const std::string &group) {
    std::vector<int> bgs = ini.BgsOf(group);
    if (bg < 0 || bg >= (int)bgs.size()) return -1;
    std::vector<std::string> lines = SplitLines(text);
    const IniSection &s = ini.sections[bgs[bg]];
    int from, to;
    SectionLines(lines, s, from, to);
    std::vector<std::string> block(lines.begin() + (s.line - 1), lines.begin() + to);
    // "BG Sky" is copied as "BG Sky 2", "BG Sky 2" as "BG Sky 3", "BG 4" as "BG 5"
    std::string base = s.name.find(' ') == std::string::npos ? "" : Trim(s.name.substr(s.name.find(' ')));
    size_t digits = base.find_last_not_of("0123456789") + 1;
    int number = digits < base.size() ? atoi(base.c_str() + digits) : 1;
    block[0] = "[" + NumberedBg(ini, group, Trim(base.substr(0, digits)), number + 1) + "]";
    int last = LastFollower(ini, bgs, bg);
    SectionLines(lines, ini.sections[bgs[last]], from, to);
    InsertBlock(lines, to, block);
    text = JoinLines(lines);
    return last + 1;
}

// The BGs moving, [a0, a1], and those they pass, [b0, b1]: a BG takes its followers along and passes
// another with its followers; a follower stays among those of its BG.
static bool MoveRange(const IniFile &ini, const std::vector<int> &bgs, int bg, int dir, int &a0, int &a1, int &b0, int &b1) {
    int n = (int)bgs.size();
    if (bg < 0 || bg >= n || (dir != 1 && dir != -1)) return false;
    a0 = a1 = bg;
    if (Follows(ini, bgs, bg)) {
        b0 = b1 = bg + dir;
        return Follows(ini, bgs, b0);
    }
    a1 = LastFollower(ini, bgs, bg);
    if (dir > 0) {
        b0 = a1 + 1;
        if (b0 >= n) return false;
        b1 = LastFollower(ini, bgs, b0);
    } else {
        b1 = bg - 1;
        if (b1 < 0) return false;
        for (b0 = b1; Follows(ini, bgs, b0);) b0--;
    }
    return true;
}

bool Ini_CanMoveBg(const IniFile &ini, int bg, int dir, const std::string &group) {
    int a0, a1, b0, b1;
    return MoveRange(ini, ini.BgsOf(group), bg, dir, a0, a1, b0, b1);
}

int Ini_MoveBg(std::string &text, const IniFile &ini, int bg, int dir, const std::string &group) {
    std::vector<int> bgs = ini.BgsOf(group);
    int a0, a1, b0, b1;
    if (!MoveRange(ini, bgs, bg, dir, a0, a1, b0, b1)) return -1;
    int first0 = std::min(a0, b0), first1 = dir > 0 ? a1 : b1, second0 = std::max(a0, b0), second1 = dir > 0 ? b1 : a1;
    std::vector<std::string> lines = SplitLines(text), out;
    int x0, x1, y0, y1, unused;
    SectionLines(lines, ini.sections[bgs[first0]], x0, unused);
    SectionLines(lines, ini.sections[bgs[first1]], unused, x1);
    SectionLines(lines, ini.sections[bgs[second0]], y0, unused);
    SectionLines(lines, ini.sections[bgs[second1]], unused, y1);
    auto copy = [&](int from, int to) { out.insert(out.end(), lines.begin() + from, lines.begin() + to); };
    copy(0, x0);
    copy(y0, y1);
    copy(x1, y0);
    copy(x0, x1);
    copy(y1, (int)lines.size());
    text = JoinLines(out);
    return bg + dir * (b1 - b0 + 1);
}

bool Ini_DeleteBg(std::string &text, const IniFile &ini, int bg, const std::string &group) {
    std::vector<int> bgs = ini.BgsOf(group);
    if (bg < 0 || bg >= (int)bgs.size()) return false;
    std::vector<std::string> lines = SplitLines(text);
    int from, to;
    SectionLines(lines, ini.sections[bgs[bg]], from, to);
    while (to + 1 < (int)lines.size() && Trim(lines[to]).empty()) to++; // the blank lines after it
    lines.erase(lines.begin() + from, lines.begin() + to);
    text = JoinLines(lines);
    return true;
}

int Ini_BgFollowers(const IniFile &ini, int bg, const std::string &group) {
    std::vector<int> bgs = ini.BgsOf(group);
    if (bg < 0 || bg >= (int)bgs.size() || Follows(ini, bgs, bg)) return 0;
    return LastFollower(ini, bgs, bg) - bg;
}

std::vector<int> Ini_BgMap(const IniFile &was, const IniFile &now, const std::string &group) {
    std::vector<int> a = was.BgsOf(group), b = now.BgsOf(group), map(b.size(), -1);
    std::vector<char> used(a.size());
    auto same = [](const IniSection &x, const IniSection &y) {
        if (x.fields.size() != y.fields.size()) return false;
        for (size_t k = 0; k < x.fields.size(); k++)
            if (x.fields[k].key != y.fields[k].key || x.fields[k].value != y.fields[k].value) return false;
        return true;
    };
    for (int exact = 1; exact >= 0; exact--)
        for (size_t i = 0; i < b.size(); i++) {
            const IniSection &s = now.sections[b[i]];
            for (size_t k = 0; k < a.size() && map[i] < 0; k++) {
                const IniSection &o = was.sections[a[k]];
                if (!used[k] && o.lower == s.lower && (!exact || same(o, s))) map[i] = (int)k, used[k] = 1;
            }
        }
    if (a.size() == b.size())
        for (size_t i = 0; i < b.size(); i++)
            if (map[i] < 0 && !used[i]) map[i] = (int)i, used[i] = 1;
    return map;
}

std::string Ini_BgChanges(const IniFile &was, const IniFile &now, const std::vector<std::string> &groups) {
    std::string out;
    for (const std::string &group : groups) {
        std::vector<int> map = Ini_BgMap(was, now, group);
        bool same = map.size() == was.BgsOf(group).size();
        std::string list;
        for (size_t i = 0; i < map.size(); i++) {
            list += (i ? "," : "") + std::to_string(map[i]);
            same = same && map[i] == (int)i;
        }
        if (!same) out += (out.empty() ? "" : ";") + Lower(group) + "=" + list;
    }
    return out;
}

std::vector<std::string> Ini_BgGroups(const IniFile &ini) {
    std::vector<std::string> out;
    auto add = [&](const std::string &group) {
        for (const std::string &o : out)
            if (Lower(o) == Lower(group)) return;
        if (!group.empty()) out.push_back(group);
    };
    for (const IniSection &s : ini.sections)
        if (FirstWord(s.lower) == "scene" || FirstWord(s.lower) == s.lower) add(Ini_BgGroup(ini, s));
    return out;
}

IniBgTime Ini_BgCtrlTime(const IniFile &ini, int section) {
    IniBgTime t;
    for (int i = section - 1; i >= 0; i--)
        if (FirstWord(ini.sections[i].lower) == "bgctrldef") {
            if (const IniField *f = ini.sections[i].Field("looptime")) t.loop = atoi(f->value.c_str());
            break;
        }
    const IniField *f = ini.sections[section].Field("time");
    if (!f) return t;
    // Comma by comma: an empty one keeps what it had (readI32ForStage)
    int part = 0;
    size_t from = 0;
    for (;; part++) {
        size_t comma = f->value.find(',', from);
        std::string v = Trim(f->value.substr(from, comma == std::string::npos ? std::string::npos : comma - from));
        if (part == 0) t.start = t.end = atoi(v.c_str());
        if (part == 1 && !v.empty()) t.end = atoi(v.c_str());
        if (part == 2 && !v.empty()) t.loop = atoi(v.c_str()), t.ownLoop = true;
        if (comma == std::string::npos || part == 2) break;
        from = comma + 1;
    }
    return t;
}

std::string Ini_BgTimeValue(const IniBgTime &t) {
    std::string s = std::to_string(t.start);
    if (t.end != t.start || t.ownLoop) s += ", " + std::to_string(t.end);
    if (t.ownLoop) s += ", " + std::to_string(t.loop);
    return s;
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
