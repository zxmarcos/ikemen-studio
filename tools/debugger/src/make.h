// Text written for new things in a character's files: a ChangeState on a command at the top of
// [Statedef -1], a move's statedef. CNS or ZSS, as the file it goes in is written.
#pragma once

#include <string>

// when: 0 any statetype, 1 standing, 2 crouching, 3 in the air
std::string Make_CommandEntry(bool zss, const std::string &command, int state, int when, bool ctrl);

// block put at the top of [Statedef no]: before its first controller (CNS: after the
// statedef's parameters) or right after its header (ZSS). False when the text has no such
// statedef. line: where the block starts (1-based).
bool Make_InsertInStatedef(std::string &text, bool zss, int no, const std::string &block, int *line = nullptr);
