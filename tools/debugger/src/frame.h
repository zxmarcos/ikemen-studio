// Reads the frames the game shares (src/editor_frame.go): the picture of each
// frame and where the players and helpers are in it; sends input back.
#pragma once

#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct GameFrame {
    int width = 0, height = 0;
    unsigned frame = 0;                 // the game's frame counter
    std::vector<unsigned char> pixels;  // RGBA, bottom row first
    std::string geometry;               // JSON, see editor_frame.go
};

class FrameLink {
public:
    ~FrameLink() { Close(); }

    // Maps the game's frames; false until the game has drawn its first frame.
    bool Open(DWORD pid);
    void Close();
    bool IsOpen() const { return base_ != nullptr; }
    // Copies the newest frame into out; false when there is no new one.
    bool Read(GameFrame &out);
    // Sends a key to the game, read before its next frame: an SDL key code and SDL modifiers.
    bool SendKey(int sdlKey, bool down, int sdlMods);
    // Has the game size its window as its picture (it is hidden, its frames are shown here).
    bool SendWindowFit(bool fit);
    // Lua the game runs before its next frame (in menus too); false when it does not fit.
    bool SendCommand(const std::string &lua);

private:
    bool SendEvent(int kind, int a, int b, int c);

    HANDLE map_ = nullptr;
    unsigned char *base_ = nullptr;
    unsigned lastCount_ = 0;
};
