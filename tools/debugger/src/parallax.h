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
    float rate = 1;                // parallax: how many times as fast as its top its bottom scrolls (width or xscale)
    int tile[2] = {0, 0};          // 0 none, 1 for ever, n copies
    float step[2] = {0, 0};        // from a tile to the next
    bool linked = false;           // positionlink: its delta is the one it follows
    bool solid = true;             // hides what is behind: not blended, windowed or turned
    float units = 1;               // of the def's, in these numbers: 0.5 on a hires stage
    int layer = 0;                 // layerno: 1 is drawn after the fighters
    bool widths = false;           // parallax: width sets its top's and its bottom's
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
// How far down a floor the ground is, 0 its top and 1 its bottom; -1 when the ground is not on it.
float Parallax_GroundRow(const ParallaxBg &b, const ParallaxView &v);
// The delta at which the row of a floor the fighters stand on scrolls as they do, -1 when the
// ground is not on it.
float Parallax_FloorDelta(const ParallaxBg &b, const ParallaxView &v);
// How fast that row scrolls now, 1 with the fighters; -1 when the ground is not on the floor.
float Parallax_GroundDelta(const ParallaxBg &b, const ParallaxView &v);

// How fast a BG's row at y scrolls, as drawn: a floor's lower rows faster.
float Parallax_RowSpeed(const ParallaxBg &b, float y);

// Two planes that meet, as a floor's top edge on the wall behind it: each as wide as the screen, the
// lower one drawn over the upper one, whose rows go on at least to the lower one's top. By their place
// in bgs: those a BG meets, the one its top is on first, then the one whose top is on it.
struct ParallaxMeeting {
    int upper = -1, lower = -1;
};
std::vector<ParallaxMeeting> Parallax_Meetings(const std::vector<ParallaxBg> &bgs, int at, const ParallaxView &v);

struct ParallaxPicture {
    const unsigned char *rgba = nullptr; // by rows
    int w = 0, h = 0;
};

// What makes the lines of two planes meet where they meet, wherever the camera is: one takes the
// other's speed there, the lower one's top the width that repeats as the upper one's lines do (a
// width floor that repeats across), and the one that follows moves across to line up. The one that
// follows is the upper one unless the fighters stand on it and not on the lower one. A floor that
// moves with the fighters keeps doing so.
struct ParallaxLineUp {
    bool upperFollows = true;
    float slide = 0;  // how far the join slides now from one limit of the camera to the other
    float match = -1; // how alike their lines are once lined up, 0 to 1; -1 when either side is plain,
                      // and nothing shows the join: nothing is changed
    // What lines them up, as drawn: -1 keeps a delta; the widths are the def's, 0 keeps them
    float delta[2][2] = {{-1, -1}, {-1, -1}}; // the upper one's, the lower one's
    int width[2] = {0, 0};                    // the lower one's top and bottom
    float shift[2] = {0, 0};                  // how far each moves across
    bool done = false;                        // already lined up
};
ParallaxLineUp Parallax_LineUp(const ParallaxBg &upper, const ParallaxPicture &up, const ParallaxBg &lower, const ParallaxPicture &low, const ParallaxView &v);
// The same, by points said to meet: upX[i] of the upper one's on lowX[i] of the lower one's, where each
// is along the join with the camera at its start. One pair moves the one that follows; a second one
// also gives a width floor's top the width that spaces its two as the upper one's are.
ParallaxLineUp Parallax_LineUpBy(const ParallaxBg &upper, const ParallaxBg &lower, const ParallaxView &v, const float *upX, const float *lowX, int pairs);

// The pixel of a BG's picture (spriteW wide) on its row at y under x, with the camera at its start:
// which copy across and which column. False where it draws nothing.
bool Parallax_PixelUnder(const ParallaxBg &b, int spriteW, float y, float x, int *copy, int *column);
// Where the middle of that pixel's column is on the BG's row at y.
float Parallax_PixelX(const ParallaxBg &b, int spriteW, float y, int copy, int column);

// Whether a picture of w by h pixels (RGBA, by rows) shows where it repeats, across (axis 0) or down:
// its opposite edges differ far more than its neighbouring pixels do.
bool Parallax_Seam(const unsigned char *rgba, int w, int h, int axis);
// The BGs that repeat with no gap and show where they do.
std::vector<StageFinding> Parallax_Seams(const std::vector<ParallaxBg> &bgs, const SffDoc &sff);

// What the camera's limits show and its start does not, and the BGs that fill the screen at the
// start and end before it at a limit.
std::vector<StageFinding> Parallax_Check(const IniFile &ini, const std::vector<ParallaxBg> &bgs, const ParallaxView &v);
