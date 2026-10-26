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
