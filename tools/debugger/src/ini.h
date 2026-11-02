// INI text as Ikemen reads its def files (stages, and later screenpacks): the
// sections and their "key = value" fields with the lines they are on, and edits
// of the text that keep everything else (comments, spacing, other sections).
#pragma once

#include <string>
#include <utility>
#include <vector>

struct IniField {
    std::string key;   // lower case
    std::string value; // trimmed, without its ";" comment
    int line = 0;      // 1-based
};

struct IniSection {
    std::string name;    // as written between the brackets, trimmed
    std::string lower;   // lower case
    int line = 0, last = 0; // of "[name]" and of its last field (the header when it has none)
    std::vector<IniField> fields;
    const IniField *Field(const char *key) const; // the last one with that key
    // "a, b": the numbers of a field (missing ones are 0), false without the field
    bool Floats(const char *key, float *out, int count) const;
};

struct IniFile {
    std::vector<IniSection> sections;
    // A stage's BGs, in the engine's order: sections whose name's first word is "bg" (indices
    // into sections); its BGCtrls: first word "bgctrl" or "bgctrl3d"
    std::vector<int> StageBgs() const;
    std::vector<int> StageBgCtrls() const;
};

void Ini_Parse(const std::string &text, IniFile &out);

// The SFF a def's text names, as written: a motif's or a storyboard's spr, a fight screen's sff,
// a stage's spr; "" none.
std::string Ini_SpritesFile(const std::string &text);

// Edits ('\n' line ends). Each returns false when it could not be done (the section moved).
// A field's line is replaced (its comment kept; an empty value removes it), or added after the
// section's last field.
bool Ini_SetField(std::string &text, const IniSection &s, const char *key, const std::string &value);
// Numbers as a field value: "10, -2.5"
std::string Ini_Floats(const float *v, int count);

// A stage's BGs as lines of its text. They are drawn in the order of their sections, and a BG with
// positionlink follows the last one without it: that one takes them along, and nothing goes between
// them. A BG's lines are its section's, with the comment right above it and those right after it.
// Each returns the BG's new index, -1 when it could not be done.
std::string Ini_NewBgName(const IniFile &ini, const std::string &base); // "BG base", numbered when taken; base empty: "Layer"
// A new section after that BG (-1: after the last one; with none, after [BGdef])
int Ini_AddBg(std::string &text, const IniFile &ini, int after, const std::string &name, const std::string &body);
int Ini_DuplicateBg(std::string &text, const IniFile &ini, int bg); // right after it, under a name of its own
int Ini_MoveBg(std::string &text, const IniFile &ini, int bg, int dir); // past the BG before (-1) or after it (1)
bool Ini_CanMoveBg(const IniFile &ini, int bg, int dir);
bool Ini_DeleteBg(std::string &text, const IniFile &ini, int bg);
int Ini_BgFollowers(const IniFile &ini, int bg); // how many BGs follow it
// For each BG of now, its index in was (-1: a new one): the same section, else the same name; with as
// many BGs in both, the same place.
std::vector<int> Ini_BgMap(const IniFile &was, const IniFile &now);

// When a BGCtrl runs, as the engine reads it: time = start, end, looptime; end left out is start, and
// the looptime left out is its BGCtrlDef's (-1 none). ownLoop: its time gives one.
struct IniBgTime {
    int start = 0, end = 0, loop = -1;
    bool ownLoop = false;
};
IniBgTime Ini_BgCtrlTime(const IniFile &ini, int section);
std::string Ini_BgTimeValue(const IniBgTime &t); // as its time's value, no longer than it needs

// What a field of a stage's or a screen's def names, by its key: an action of the def ("actionno",
// "p1.ko.anim", a BGCtrl Anim's "value"), a sprite of its SFF ("spriteno", "p1.bg0.spr"), a sound of its
// SND ("cursor.move.snd"). A file's name in [Files], [BGdef] and [SceneDef] is none of these.
enum IniRefKind { kIniRefNone, kIniRefAction, kIniRefSprite, kIniRefSound };
IniRefKind Ini_FieldRef(const IniSection &s, const IniField &f);
// The fields naming an action (section index, field index)
std::vector<std::pair<int, int>> Ini_ActionUsers(const IniFile &ini, int action);

// A fight screen's keys can start with a selector the engine resolves for each player or team
// (resolveLifebarPrefixOperators): "p*" (every p<n>), "p1|p3" (those).
bool Ini_SelectorMatches(const std::string &sel, const std::string &first); // sel is first ("p1") or selects it
bool Ini_KeyApplies(const std::string &key, const std::string &prefix);     // "p*.bg0.anim" to the element "p1.bg0."
// The key of the section giving a key ("p1.pos") its value: itself, else one with a selector
// ("p1|p3.pos", then "p*.pos"); "" when none.
std::string Ini_DefKeyFor(const IniSection *s, const std::string &key);
