// A glTF 2.0 model (.glb or .gltf) read from disk as Ikemen's model.go reads a stage's: its
// scenes, nodes (with Ikemen's extras), meshes (positions, normals, first texture coordinates,
// indices), materials, textures, animations (translation, rotation, scale) and lights
// (KHR_lights_punctual); posed at a time, picked by a ray, and written back with the changes
// made to it (the rest of its JSON, its buffers and images as they were).
#pragma once

#include <array>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "json.h"

using GltfMat = std::array<float, 16>; // column-major, as OpenGL takes it

GltfMat Gltf_Identity();
GltfMat Gltf_Mul(const GltfMat &a, const GltfMat &b);
GltfMat Gltf_Invert(const GltfMat &m);
GltfMat Gltf_Compose(const float t[3], const float r[4], const float s[3]); // T * R * S
void Gltf_Apply(const GltfMat &m, const float in[3], float out[3]);        // a point

struct GltfPrim {
    int material = -1, mode = 4; // 4: triangles
    std::vector<float> pos, nrm, uv; // 3, 3 and 2 per vertex (nrm, uv empty without them)
    std::vector<unsigned> idx;       // a list of triangles (strips and fans made into one)
    float lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};
};

struct GltfMesh {
    std::string name;
    std::vector<GltfPrim> prims;
};

struct GltfNode {
    std::string name;
    int mesh = -1, light = -1, parent = -1;
    std::vector<int> children;
    float t[3] = {0, 0, 0}, r[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1}; // at rest
    // Ikemen's extras, as model.go reads them
    int trans = 0; // 0 none, 1 add, 2 sub, 3 mul
    bool hasLayer = false;
    int layer = 0;
    bool shadow = true, ztest = true, zwrite = true;
    float outline = 0;
};

struct GltfMaterial {
    std::string name;
    float color[4] = {1, 1, 1, 1}, emission[3] = {0, 0, 0};
    float metallic = 1, roughness = 1, ao = 1, cutoff = 0.5f;
    int alpha = 0; // 0 opaque, 1 mask, 2 blend
    bool doubleSided = false, unlit = false;
    int baseTexture = -1; // a texture's index
    int tex = 0;          // 1 base colour, 2 normal, 4 metallic-roughness, 8 occlusion, 16 emission
};

struct GltfImage {
    std::string name;
    int w = 0, h = 0;
    std::vector<unsigned char> rgba; // empty when it could not be decoded
};

struct GltfTexture {
    int image = -1;
    int wrapS = 10497, wrapT = 10497; // GL_REPEAT
    bool nearest = false;
};

struct GltfAnim {
    std::string name;
    float duration = 0;
    struct Channel {
        int node = -1, path = 0; // 0 translation, 1 rotation, 2 scale
        int sampler = -1;        // its animation's sampler (others may share it)
        int interp = 0;          // 0 linear, 1 step, 2 cubic spline
        std::vector<float> in, out; // times; values (a cubic spline's: in-tangent, value, out-tangent)
    };
    std::vector<Channel> channels;
};

struct GltfLight {
    std::string name;
    int type = 0; // 0 directional, 1 point, 2 spot
    float rgb[3] = {1, 1, 1}, intensity = 1, range = -1, inner = 0, outer = 0.785398f;
};

struct GltfScene {
    std::string name;
    std::vector<int> roots;
};

// Decodes an image file's bytes (PNG, JPEG...) into RGBA; false when it cannot
using GltfDecoder = std::function<bool(const unsigned char *data, size_t size, int &w, int &h, std::vector<unsigned char> &rgba)>;

struct GltfDoc {
    std::string path, error;
    bool glb = false;
    Json json;
    std::string bin; // a .glb's binary chunk
    std::vector<std::string> buffers;
    std::vector<GltfScene> scenes;
    std::vector<GltfNode> nodes;
    std::vector<GltfMesh> meshes;
    std::vector<GltfMaterial> materials;
    std::vector<GltfImage> images;
    std::vector<GltfTexture> textures;
    std::vector<GltfAnim> anims;
    std::vector<GltfLight> lights;
    // Changed since it was read: what Save writes (keys: animation and sampler, its channels' keys)
    std::set<int> trs, props, mats, lightsChanged;
    std::set<std::pair<int, int>> keys;
    bool Dirty() const { return !trs.empty() || !props.empty() || !mats.empty() || !lightsChanged.empty() || !keys.empty(); }
};

// Reads a model (images decoded when a decoder is given); false with doc.error set
bool Gltf_Load(GltfDoc &doc, const std::string &path, const GltfDecoder &decode = nullptr);
// Every node's world matrix: at rest, moved by the animations whose time is >= 0 (seconds)
void Gltf_Pose(const GltfDoc &doc, const std::vector<float> &animTimes, std::vector<GltfMat> &world);
// The node whose triangles a ray (from, towards) meets first among the visible ones; -1 none
int Gltf_Pick(const GltfDoc &doc, const std::vector<GltfMat> &world, const std::vector<char> &visible,
              const float from[3], const float towards[3]);
// A node's box in the world, as 8 corners (its mesh's box turned with it, else the box of the
// meshes under it); false without any
bool Gltf_Corners(const GltfDoc &doc, const std::vector<GltfMat> &world, int node, float corners[8][3]);
// A channel's value at a time (3 numbers, or 4 for a rotation)
void Gltf_Sample(const GltfAnim::Channel &c, float time, float *out);
// Writes the changes into the file (a .bak of it first, once); false with *error set. Animation
// keys are written into a .glb only (over their data, or at the end of its binary chunk).
bool Gltf_Save(GltfDoc &doc, std::string *error);
