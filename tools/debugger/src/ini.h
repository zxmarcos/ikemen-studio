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
    // A stage's BGs, in the engine's order: sections whose name's first word is "bg"
    std::vector<int> StageBgs() const; // indices into sections
};

void Ini_Parse(const std::string &text, IniFile &out);

// Edits ('\n' line ends). Each returns false when it could not be done (the section moved).
// A field's line is replaced (its comment kept; an empty value removes it), or added after the
// section's last field.
bool Ini_SetField(std::string &text, const IniSection &s, const char *key, const std::string &value);
// Numbers as a field value: "10, -2.5"
std::string Ini_Floats(const float *v, int count);
