// select.def: see roster.h. The rules are main.lua's (main.f_loadSelectDef, main.f_addChar,
// main.f_addStageEntry): ";" starts a comment, "," separates an entry's items, "unlock =" takes
// the rest of the line, "slot = {" ... "}" groups entries (separated by "|" or lines).
#include "roster.h"

#include <cctype>

static std::string Trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

static std::string Lower(std::string s) {
    for (char &ch : s) ch = (char)tolower((unsigned char)ch);
    return s;
}

static bool EndsWithDef(const std::string &s) {
    return s.size() >= 4 && Lower(s.substr(s.size() - 4)) == ".def";
}

// "key = value" -> key (lower case), or "" when the item has no "="
static std::string KeyOf(const std::string &item) {
    size_t eq = item.find('=');
    return eq == std::string::npos ? "" : Lower(Trim(item.substr(0, eq)));
}

std::string RosterEntry::Param(const char *key) const {
    for (const std::string &it : items)
        if (KeyOf(it) == key) return Trim(it.substr(it.find('=') + 1));
    return "";
}

void RosterEntry::SetParam(const char *key, const std::string &value) {
    for (size_t i = 0; i < items.size(); i++) {
        if (KeyOf(items[i]) != key) continue;
        if (value.empty()) {
            items.erase(items.begin() + i);
        } else {
            // As it was written: "order=0" stays without spaces
            size_t eq = items[i].find('=');
            bool spaced = eq + 1 < items[i].size() && items[i][eq + 1] == ' ';
            items[i] = items[i].substr(0, eq + 1) + (spaced ? " " : "") + value;
        }
        return;
    }
    if (!value.empty()) items.push_back(std::string(key) + " = " + value);
}

std::vector<std::string> RosterEntry::Stages() const {
    std::vector<std::string> out;
    for (const std::string &it : items)
        if (EndsWithDef(it) || Lower(it) == "random") out.push_back(it);
    return out;
}

std::string RosterEntry::Text() const {
    std::string s = name;
    for (const std::string &it : items) s += ", " + it;
    if (!unlock.empty()) s += ", unlock = " + unlock;
    return s;
}

RosterEntry Roster_ParseEntry(const std::string &text) {
    RosterEntry e;
    std::string rest = text;
    // ", unlock = <lua>": the rest of the line
    for (size_t at = rest.find(','); at != std::string::npos; at = rest.find(',', at + 1)) {
        size_t k = rest.find_first_not_of(" \t", at + 1);
        if (k == std::string::npos || rest.compare(k, 6, "unlock") != 0) continue;
        size_t eq = rest.find_first_not_of(" \t", k + 6);
        if (eq == std::string::npos || rest[eq] != '=') continue;
        e.unlock = Trim(rest.substr(eq + 1));
        rest = rest.substr(0, at);
        break;
    }
    size_t start = 0;
    for (bool first = true;; first = false) {
        size_t comma = rest.find(',', start);
        std::string item = Trim(rest.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (first)
            e.name = item;
        else if (!item.empty())
            e.items.push_back(item);
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return e;
}

std::string RosterCell::Text() const {
    if (!slot) return entries.empty() ? "" : entries[0].Text();
    std::string s = "slot = {";
    for (size_t i = 0; i < entries.size(); i++) s += (i ? " | " : "") + entries[i].Text();
    return s + "}";
}

static std::vector<std::string> Lines(const std::string &text) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        size_t nl = text.find('\n', start);
        out.push_back(text.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return out;
}

static std::string Join(const std::vector<std::string> &lines) {
    std::string s;
    for (size_t i = 0; i < lines.size(); i++) s += (i ? "\n" : "") + lines[i];
    return s;
}

static std::string Content(const std::string &line) { return Trim(line.substr(0, line.find(';'))); }

void Roster_Parse(const std::string &text, RosterFile &out) {
    out = RosterFile();
    std::vector<std::string> lines = Lines(text);
    int section = 0; // 1 [Characters], 2 [ExtraStages]
    int lastContent = 0;
    RosterCell slot;
    bool inSlot = false;
    auto endSection = [&]() {
        if (section == 1) out.charsEnd = (lastContent ? lastContent : out.charsLine) + 1;
        if (section == 2) out.stagesEnd = (lastContent ? lastContent : out.stagesLine) + 1;
        inSlot = false;
    };
    auto addEntries = [&](RosterCell &c, const std::string &s) {
        size_t start = 0;
        for (;;) {
            size_t bar = s.find('|', start);
            std::string part = Trim(s.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
            if (!part.empty()) c.entries.push_back(Roster_ParseEntry(part));
            if (bar == std::string::npos) break;
            start = bar + 1;
        }
    };
    for (int n = 1; n <= (int)lines.size(); n++) {
        std::string c = Content(lines[n - 1]);
        if (c.empty()) continue;
        if (c[0] == '[') {
            endSection();
            std::string name = Lower(Trim(c.substr(1, c.find(']') - 1)));
            section = name == "characters" ? 1 : name == "extrastages" ? 2 : 0;
            if (section == 1) out.charsLine = n;
            if (section == 2) out.stagesLine = n;
            lastContent = 0;
            continue;
        }
        if (section == 0) continue;
        lastContent = n;
        std::vector<RosterCell> &cells = section == 1 ? out.chars : out.stages;
        std::string lc = Lower(c);
        if (!inSlot && lc.compare(0, 4, "slot") == 0) {
            size_t eq = lc.find_first_not_of(" \t", 4);
            size_t br = eq != std::string::npos && lc[eq] == '=' ? lc.find_first_not_of(" \t", eq + 1) : std::string::npos;
            if (br != std::string::npos && lc[br] == '{') {
                slot = RosterCell();
                slot.slot = true;
                slot.first = n;
                inSlot = true;
                c = Trim(c.substr(br + 1));
            }
        }
        if (inSlot) {
            bool end = !c.empty() && c.back() == '}';
            if (end) c = Trim(c.substr(0, c.size() - 1));
            addEntries(slot, c);
            if (end) {
                slot.last = n;
                cells.push_back(slot);
                inSlot = false;
            }
            continue;
        }
        RosterCell cell;
        cell.first = cell.last = n;
        cell.entries.push_back(Roster_ParseEntry(c));
        cell.exclude = section == 1 && cell.entries[0].Param("exclude") == "1";
        cells.push_back(cell);
    }
    endSection();
}

bool Roster_SetCell(std::string &text, const RosterCell &c, const std::string &line) {
    std::vector<std::string> lines = Lines(text);
    if (c.first < 1 || c.last < c.first || c.last > (int)lines.size()) return false;
    const std::string &old = lines[c.first - 1];
    size_t indent = old.find_first_not_of(" \t");
    std::string head = indent == std::string::npos ? "" : old.substr(0, indent);
    std::string comment; // with the spaces before it
    if (size_t semi = old.find(';'); semi != std::string::npos) {
        while (semi > 0 && (old[semi - 1] == ' ' || old[semi - 1] == '\t')) semi--;
        comment = old.substr(semi);
    }
    lines[c.first - 1] = head + line + comment;
    lines.erase(lines.begin() + c.first, lines.begin() + c.last);
    text = Join(lines);
    return true;
}

bool Roster_DeleteCell(std::string &text, const RosterCell &c) {
    std::vector<std::string> lines = Lines(text);
    if (c.first < 1 || c.last < c.first || c.last > (int)lines.size()) return false;
    lines.erase(lines.begin() + c.first - 1, lines.begin() + c.last);
    text = Join(lines);
    return true;
}

bool Roster_InsertLine(std::string &text, int n, const std::string &line) {
    std::vector<std::string> lines = Lines(text);
    if (n < 1 || n > (int)lines.size() + 1) return false;
    lines.insert(lines.begin() + n - 1, line);
    text = Join(lines);
    return true;
}

bool Roster_MoveCell(std::string &text, const RosterCell &c, int n) {
    std::vector<std::string> lines = Lines(text);
    if (c.first < 1 || c.last < c.first || c.last > (int)lines.size() || n < 1 || n > (int)lines.size() + 1)
        return false;
    if (n >= c.first && n <= c.last + 1) return true; // where it is
    std::vector<std::string> block(lines.begin() + c.first - 1, lines.begin() + c.last);
    lines.erase(lines.begin() + c.first - 1, lines.begin() + c.last);
    if (n > c.last) n -= (int)block.size();
    lines.insert(lines.begin() + n - 1, block.begin(), block.end());
    text = Join(lines);
    return true;
}
