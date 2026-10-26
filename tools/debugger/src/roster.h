// select.def as Ikemen's scripts read it (main.lua, main.f_loadSelectDef): the characters in
// the select screen's cells, with their stages and parameters, the extra stages, with the
// lines they are on; and edits of the text that keep everything else (comments, spacing).
#pragma once

#include <string>
#include <vector>

// One line's entry (or a slot's): "kfm, stages/kfm.def, order = 2, unlock = <lua>"
struct RosterEntry {
    std::string name;               // a character (kfm, kfm/alt.def, suave.zip), a keyword
                                    // (randomselect, skipslot, emptyslot) or a stage's def
    std::vector<std::string> items; // what follows, trimmed: stage defs, "random", "key = value"
    std::string unlock;             // the Lua after "unlock =" (always last), "" for none
    std::string Param(const char *key) const;  // a "key = value" item's value, "" if none
    void SetParam(const char *key, const std::string &value); // "" removes it
    std::vector<std::string> Stages() const;   // the items that are stage defs (or "random")
    std::string Text() const;                  // back to text
};

RosterEntry Roster_ParseEntry(const std::string &text);

// A cell of the select screen (a stage of the stage list): one entry, or a slot's entries
// (slot = { a | b }, on one line or several)
struct RosterCell {
    int first = 0, last = 0; // 1-based lines it takes
    bool slot = false;
    bool exclude = false;    // exclude = 1: added after the grid's cells, in none of them
    std::vector<RosterEntry> entries;
    std::string Text() const; // the cell as one line (a slot: "slot = {a | b}")
};

struct RosterFile {
    int charsLine = 0, charsEnd = 0;   // [Characters]: its header, and the line to insert at
    int stagesLine = 0, stagesEnd = 0; // [ExtraStages], the same (0: no such section)
    std::vector<RosterCell> chars;     // in order (excluded ones too)
    std::vector<RosterCell> stages;
};

void Roster_Parse(const std::string &text, RosterFile &out);

// Edits ('\n' line ends). False when it could not be done.
// A cell's lines become one line (indentation and the first line's comment kept).
bool Roster_SetCell(std::string &text, const RosterCell &c, const std::string &line);
bool Roster_DeleteCell(std::string &text, const RosterCell &c);
// A new line before line n (1-based; one past the last line appends).
bool Roster_InsertLine(std::string &text, int n, const std::string &line);
// A cell's lines go before line n (another cell's first, or a section's end).
bool Roster_MoveCell(std::string &text, const RosterCell &c, int n);
