// A stage's BGs against its camera, as Ikemen GO draws them (stage.go, backGround.drawTransformed;
// camera.go): what each BG covers of the screen with the camera anywhere, what shows nothing at the
// camera's limits, and the delta or the width that keep a BG on screen. Units are the stage's. Not
// modelled: a stage's xscale and yscale, a BG's velocity, sin and BGCtrls, and the transparent parts
// of a sprite, which count as covered.
#pragma once

#include <string>
#include <vector>

#include "stagecheck.h"

struct AirFile;
struct IniFile;
struct SffDoc;

struct ParallaxView {
    float width = 320, height = 240;              // the screen at scale 1
    float left = 0, right = 0, high = 0, low = 0; // how far the camera goes
    float minScale = 1;                           // as far as it zooms out
    float zoffset = 200;                          // the ground, from the top of the screen
    float lift = 0;                               // what the engine takes from a BG's y for the screen's shape
};

struct ParallaxBg {
    int index = -1, line = 0;
    std::string name;
    int sprite = -1;                       // in the SFF
    float left = 0, top = 0, w = 0, h = 0; // with the camera at 0; w is the top's width
    float delta[2] = {1, 1};
    float zoomDelta[2] = {-1, -1}; // -1: the delta does it
    float widen = 1;               // parallax: the bottom's width over the top's
    int tile[2] = {0, 0};          // 0 none, 1 for ever, n copies
    float step[2] = {0, 0};        // from a tile to the next
    bool linked = false;           // positionlink: its delta is the one it follows
    bool solid = true;             // hides what is behind: not blended, windowed or turned
    float units = 1;               // of the def's, in these numbers: 0.5 on a hires stage
};

struct ParallaxStop {
    std::string name;
    float x = 0, y = 0, scale = 1;
};

// From the def alone, as the game would have it at the def's localcoord.
ParallaxView Parallax_ViewOf(const IniFile &ini);
// The BGs of a def that show a sprite of sff.
std::vector<ParallaxBg> Parallax_Bgs(const IniFile &ini, const AirFile &air, const SffDoc &sff);
// Where the camera can be that shows the most: the first is its start.
std::vector<ParallaxStop> Parallax_Stops(const ParallaxView &v);

// Whether a BG draws on a point of the screen: x from its middle, y from its top.
bool Parallax_Covers(const ParallaxBg &b, const ParallaxView &v, const ParallaxStop &at, float x, float y);
// A grid of the screen, by rows, cols across: 1 where no solid BG draws.
std::vector<char> Parallax_Empty(const std::vector<ParallaxBg> &bgs, const ParallaxView &v, const ParallaxStop &at, int cols, int rows);

// The largest delta that keeps a BG over the whole screen from one limit of the camera to the
// other, -1 when none does (it is narrower than the screen, or off to a side).
float Parallax_FitDelta(const ParallaxBg &b, const ParallaxView &v);
// How wide a BG centred on the screen has to be for a delta.
float Parallax_WidthFor(const ParallaxView &v, float delta);

// A BG's corners on the screen, x from its middle and y from its top; a parallax BG's bottom differs.
struct ParallaxShape {
    float top, bottom;
    float topLeft, topRight, bottomLeft, bottomRight;
};
ParallaxShape Parallax_Shape(const ParallaxBg &b, const ParallaxView &v, const ParallaxStop &at);
// The delta at which the row of a floor the fighters stand on scrolls as they do, -1 when the
// ground is not on it.
float Parallax_FloorDelta(const ParallaxBg &b, const ParallaxView &v);

// What the camera's limits show and its start does not, and the BGs that fill the screen at the
// start and end before it at a limit.
std::vector<StageFinding> Parallax_Check(const IniFile &ini, const std::vector<ParallaxBg> &bgs, const ParallaxView &v);
