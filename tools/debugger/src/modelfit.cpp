#include "modelfit.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <set>

namespace {

const float kPi = 3.14159265f;

// The share of the screen's height a fighter stands, in its usual 320 x 240
const float kFighterShare = 100.0f / 240;

std::string Num(float v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", v);
    std::string t = buf;
    while (t.back() == '0') t.pop_back();
    if (t.back() == '.') t.pop_back();
    return t;
}

float HalfView(float fov) { return std::tan(fov * kPi / 180 / 2); }

} // namespace

float Model_FighterHeight(int height) { return kFighterShare * height; }

float Model_PosMul(const ModelPlace &p) { return HalfView(p.fov) * -p.offset[2] / (p.height / 2.0f); }

float Model_Units(const ModelPlace &p) {
    float k = Model_PosMul(p);
    return k != 0 ? p.scale[1] / k : 0;
}

int Model_Zoffset(const ModelPlace &p) {
    float k = Model_PosMul(p);
    return k != 0 ? (int)(p.height / 2.0f - p.offset[1] / k) : p.height;
}

void Model_Fit(ModelFit &f) {
    ModelPlace &p = f.place;
    p.width = f.width, p.height = f.height, p.fov = f.fov;
    // One unit of the camera from the model's origin: scale and distance only count together
    p.offset[0] = 0, p.offset[2] = -1;
    float posMul = Model_PosMul(p);
    float units = kFighterShare * f.height / std::max(f.fighter, 1e-4f); // of the stage, for one of the model
    float scale = units * posMul;
    p.scale[0] = p.scale[1] = p.scale[2] = scale;
    p.offset[1] = (f.height / 2.0f - f.zoffset) * posMul;
    // As far as the camera sees past the box's back
    float deepest = 1 + scale * std::max(0.0f, -f.loAll[2]);
    p.zNear = 0.01f;
    p.zFar = std::max(10.0f, deepest * 1.5f);
    // Where the back of the box stays across the whole screen, at most a screen each way
    float back = 1 + scale * std::max(0.0f, -f.lo[2]);
    float half = HalfView(f.fov) * f.width / f.height * back;
    auto limit = [&](float side) { return std::clamp((scale * side - half) / posMul, 0.0f, (float)f.width); };
    f.right = (int)limit(f.hi[0]);
    f.left = -(int)limit(-f.lo[0]);
    f.high = -f.height * 3 / 10;
}

float Model_GuessFighter(const ModelFit &f) {
    float width = f.hi[0] - f.lo[0];
    return width > 0 ? kFighterShare * f.height * width / (f.width * 2.2f) : 1.8f;
}

bool Model_FloorAt(const GltfDoc &doc, const std::vector<GltfMat> &world, float x, float z, float *y) {
    float lo[3], hi[3];
    if (!Gltf_Bounds(doc, world, lo, hi)) return false;
    float pad = std::max(1e-3f, hi[1] - lo[1]);
    // From under it: a roof is not the floor
    float from[3] = {x, lo[1] - pad, z}, towards[3] = {x, hi[1] + pad, z}, hit[3];
    if (Gltf_Cast(doc, world, std::vector<char>(), from, towards, hit) < 0) return false;
    *y = hit[1];
    return true;
}

std::vector<StageFinding> Model_Check(const GltfDoc &doc, const std::vector<GltfMat> &world, const ModelCheckIn &in) {
    std::vector<StageFinding> out;
    auto add = [&](StageFinding::Level l, const std::string &t) { out.push_back({l, 0, t}); };

    // What the game cannot read: image.Decode in loadglTFModel fails, and the stage with it
    for (size_t i = 0; i < doc.images.size(); i++) {
        const GltfImage &m = doc.images[i];
        if (m.format == "png" || m.format == "jpeg" || m.format == "dds") continue;
        std::string name = m.name.empty() ? "image " + std::to_string(i) : m.name;
        add(StageFinding::Error, name + (m.format == "dxt" ? " is a compressed DDS" : m.format.empty() ? " is in no format the game knows" : " is " + m.format) +
                                     ": the game reads PNG and JPEG, and the stage does not load.");
    }
    for (size_t i = 0; i < doc.textures.size(); i++)
        if (doc.textures[i].image < 0) add(StageFinding::Error, "Texture " + std::to_string(i) + " has no plain image (a webp or basis one only): the game stops on it.");
    static const std::set<std::string> known = {"KHR_lights_punctual", "KHR_materials_unlit", "KHR_texture_transform", "KHR_animation_pointer"};
    for (const Json &e : doc.json["extensionsRequired"].items)
        if (!known.count(e.str())) add(StageFinding::Warning, "The model requires " + e.str() + ", which the game does not read.");
    int matrices = 0;
    std::string firstMatrix;
    for (const GltfNode &n : doc.nodes)
        if (n.hasMatrix) {
            if (!matrices++) firstMatrix = n.name;
        }
    if (matrices)
        add(StageFinding::Warning, std::to_string(matrices) + (matrices > 1 ? " nodes (" + firstMatrix + "...) are" : " node (" + firstMatrix + ") is") +
                                       " placed by a matrix, which the game does not read: on the game it stays at the origin. "
                                       "The Model window fixes that.");

    float units = Model_Units(in.place);
    float lo[3], hi[3];
    if (units > 0 && Gltf_Bounds(doc, world, lo, hi)) {
        // Under the fighters: what is under a fighter's head, else the first thing from above
        float tall = kFighterShare * in.place.height / units;
        auto under = [&](float x, float *y) {
            float from[3] = {x, tall, 0}, down[3] = {x, lo[1] - 1, 0}, high[3] = {x, hi[1] + 1, 0}, hit[3];
            if (Gltf_Cast(doc, world, std::vector<char>(), from, down, hit) >= 0 ||
                (from[1] < hi[1] && Gltf_Cast(doc, world, std::vector<char>(), high, from, hit) >= 0)) {
                *y = hit[1];
                return true;
            }
            return false;
        };
        float y = 0;
        if (!under(0, &y)) {
            add(StageFinding::Warning, "The model has no floor under the fighters' start, its origin.");
        } else if (std::fabs(y * units) > 3) {
            add(StageFinding::Warning, "The floor under the fighters is " + Num(std::fabs(y)) + (y > 0 ? " above" : " below") +
                                           " the model's origin, where their feet go: they " + (y > 0 ? "sink into it" : "float over it") +
                                           ". Put the fight on it in the Model window.");
        }
        // Along where they can walk
        float half = in.place.width / 2.0f;
        float a = (in.left - half + in.screenLeft) / units, b = (in.right + half - in.screenRight) / units;
        const int steps = 16;
        float gapFrom = 0, gapTo = 0;
        bool gap = false;
        for (int s = 0; s <= steps && b > a; s++) {
            float x = a + (b - a) * s / steps, at;
            if (under(x, &at)) continue;
            if (!gap) gapFrom = x;
            gapTo = x, gap = true;
        }
        if (gap)
            add(StageFinding::Warning, "No floor under the fighters from " + std::to_string((int)(gapFrom * units)) + " to " +
                                           std::to_string((int)(gapTo * units)) + " of where they can walk.");
    }
    float all[2][3];
    if (Gltf_Bounds(doc, world, all[0], all[1], true)) {
        float deepest = -in.place.offset[2] + in.place.scale[2] * -all[0][2];
        if (deepest > in.place.zFar)
            add(StageFinding::Warning, "[Camera] far is " + Num(in.place.zFar) + " and the model goes on to " + Num(deepest) + ": what is past it is not drawn.");
    }

    size_t triangles = 0;
    for (const GltfNode &n : doc.nodes)
        if (n.mesh >= 0)
            for (const GltfPrim &p : doc.meshes[n.mesh].prims) triangles += p.idx.size() / 3;
    if (triangles > 500000) add(StageFinding::Note, std::to_string(triangles / 1000) + " thousand triangles: a lot for a stage, with two fighters over it.");
    int big = 0;
    for (const GltfImage &m : doc.images) big += m.w > 4096 || m.h > 4096;
    if (big) add(StageFinding::Note, std::to_string(big) + (big > 1 ? " textures are" : " texture is") + " over 4096 wide: every one is kept whole in the video memory.");
    int lit = 0;
    for (const GltfNode &n : doc.nodes) lit += n.light >= 0;
    if (lit > 4) add(StageFinding::Note, std::to_string(lit) + " lights: the game uses the first 4 of a scene.");
    if (!lit && !in.environment) add(StageFinding::Note, "No light and no environment map: the game draws the model's colours as they are, unlit.");
    return out;
}
