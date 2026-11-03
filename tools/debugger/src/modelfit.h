// A model as a stage: where a [Model] puts it, as stage.go and model.go work it out, what a new
// stage from it needs, and what the game will make of it. The fighters' feet are always on the
// model's origin: zoffset is worked out from [Model] offset, so the offset moves both on the
// screen, and only moving the model itself puts them on another surface.
#pragma once

#include <string>
#include <vector>

#include "gltf.h"
#include "stagecheck.h"

// [Model] and [Camera] of a def
struct ModelPlace {
    float offset[3] = {0, 0, 0}, scale[3] = {1, 1, 1}, fov = 40, zNear = 0.1f, zFar = 10000;
    int width = 320, height = 240; // localcoord
};

float Model_FighterHeight(int height);     // how tall a fighter stands on a stage of that height, in its units
float Model_PosMul(const ModelPlace &p);   // the camera's units of one unit of the stage, at the model's origin
float Model_Units(const ModelPlace &p);    // units of the stage of one unit of the model, there
int Model_Zoffset(const ModelPlace &p);    // the line the fighters stand on, as loadStage works it out

// A new stage around a model whose origin is where the fight goes
struct ModelFit {
    int width = 1280, height = 720, zoffset = 600; // localcoord, and the fighters' line on the screen
    float fov = 30;
    float fighter = 1.8f;       // how tall a fighter stands, in the model's units
    float lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0}; // its box
    float loAll[3] = {0, 0, 0}, hiAll[3] = {0, 0, 0}; // with what goes around it (a sky)
    // Worked out
    ModelPlace place;
    int left = 0, right = 0, high = 0; // the camera's limits
};
void Model_Fit(ModelFit &f);
// How tall a fighter is taken to stand when the model's scale is not known: its floor about two screens wide
float Model_GuessFighter(const ModelFit &f);
// The surface at the model's origin, when there is one: a model made for a stage has the fight there
bool Model_FloorAtOrigin(const GltfDoc &doc, const std::vector<GltfMat> &world, float *y);
// The floor at a point: the first surface going down from the middle of the model's box, which
// leaves out a roof above and a dome or a sea around it; else its lowest surface there. False: none.
bool Model_FloorAt(const GltfDoc &doc, const std::vector<GltfMat> &world, float x, float z, float *y);

struct ModelCheckIn {
    ModelPlace place;
    bool environment = false;
    float left = 0, right = 0, screenLeft = 15, screenRight = 15; // the camera's limits, the players' bounds
};
std::vector<StageFinding> Model_Check(const GltfDoc &doc, const std::vector<GltfMat> &world, const ModelCheckIn &in);
