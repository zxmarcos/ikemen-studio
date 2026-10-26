// CMD files (character commands): the [Command] sections with the lines they
// are on, and edits of the text that keep everything else (comments, the
// states of the file). Read as Ikemen reads them (compiler.go): sections whose
// name starts with "command", values without their ";" comment, the first
// [Defaults] section.
#pragma once

#include <string>
#include <vector>

struct CmdField {
    std::string key;   // lower case
    std::string value; // trimmed, without its comment
    int line = 0;      // 1-based
};

struct CmdCommand {
    int line = 0, last = 0; // of "[Command]" and of its last field
    std::string name;       // without its quotes
    std::string command;    // as written
    int index = 0;          // the n-th section with that name (0-based): the engine's order too
    std::vector<CmdField> fields;
    const CmdField *Field(const char *key) const;
    int Int(const char *key, int otherwise) const; // a number field, or otherwise
};

struct CmdFile {
    std::vector<CmdCommand> commands;
    int defaultsLine = 0;
    int time = 15, bufferTime = 1; // [Defaults] command.time, command.buffer.time
};

void Cmd_Parse(const std::string &text, CmdFile &out);

// Edits ('\n' line ends). Each returns false when it could not be done.
// A field's line is replaced (an empty value removes it) or added after the section's last field.
bool Cmd_SetField(std::string &text, const CmdCommand &c, const char *key, const std::string &value);
bool Cmd_SetName(std::string &text, const CmdCommand &c, const std::string &name);
// A new section after that one (nullptr: before the first section, or at the end without any).
bool Cmd_Add(std::string &text, const CmdFile &f, const CmdCommand *after, const std::string &name,
             const std::string &command);
bool Cmd_Duplicate(std::string &text, const CmdCommand &c); // the copy goes after it
bool Cmd_Delete(std::string &text, const CmdCommand &c);

// A step of a command's motion ("~30$D", "/F", ">x+y") as the visual editor shows it: a
// direction and buttons pressed together, with the prefixes on the first of them. A step it
// does not show (prefixes on a later key, "|", "*", N, L, R, a charge on "/") stays in raw.
struct CmdStep {
    bool strict = false;  // > nothing else in between
    bool release = false; // ~
    int charge = 0;       // ~30: held that long before the release
    bool hold = false;    // /
    bool four = false;    // $ the diagonals count
    int dir = 0;          // 0 none, then kCmdDirs
    unsigned buttons = 0; // bits in kCmdButtons order
    std::string raw;      // as written, when not shown
};
extern const char *const kCmdDirs[9]; // "", "B", "DB", "D", "DF", "F", "UF", "U", "UB"
extern const char kCmdButtons[];      // "abcxyzsdwm"
std::vector<CmdStep> Cmd_Steps(const std::string &command);
std::string Cmd_StepText(const CmdStep &s);
std::string Cmd_StepsText(const std::vector<CmdStep> &steps); // "~D, DF, F, x"
