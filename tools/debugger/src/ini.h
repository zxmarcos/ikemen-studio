// INI text as Ikemen reads its def files (stages, and later screenpacks): the
// sections and their "key = value" fields with the lines they are on, and edits
// of the text that keep everything else (comments, spacing, other sections).
#pragma once

#include <string>
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

// A fight screen's keys can start with a selector the engine resolves for each player or team
// (resolveLifebarPrefixOperators): "p*" (every p<n>), "p1|p3" (those).
bool Ini_SelectorMatches(const std::string &sel, const std::string &first); // sel is first ("p1") or selects it
bool Ini_KeyApplies(const std::string &key, const std::string &prefix);     // "p*.bg0.anim" to the element "p1.bg0."
// The key of the section giving a key ("p1.pos") its value: itself, else one with a selector
// ("p1|p3.pos", then "p*.pos"); "" when none.
std::string Ini_DefKeyFor(const IniSection *s, const std::string &key);
