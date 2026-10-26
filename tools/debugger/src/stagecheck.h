// The checks of a stage's def, as Ikemen GO reads it (stage.go, readBackGround, bgCtrl.read):
// files that are not found, sprites and actions the BGs name that do not exist, BGCtrls on ids
// no BG has, BGs and BGCtrls the engine skips, the camera's bounds and the players' start
// positions, sections and keys the engine does not read, sprites nothing uses.
#pragma once

#include <functional>
#include <string>
#include <vector>

struct SffDoc;

struct StageFinding {
    enum Level { Error, Warning, Note } level;
    int line; // 1-based, 0: the file
    std::string text;
};

// exists: whether a file named in the def is found (the app resolves it as the engine does,
// next to the def first). sff: the stage's sprites, nullptr when they could not be read.
std::vector<StageFinding> Stage_Check(const std::string &text, const SffDoc *sff,
                                      const std::function<bool(const std::string &)> &exists);
