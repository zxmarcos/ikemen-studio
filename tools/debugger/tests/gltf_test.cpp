// Tests of src/gltf.cpp on the release's 3D stage (run from the root of the checkout; skipped when
// missing): the model read, its JSON written and read back the same, a node picked by a ray, and
// changes saved into a copy (its binary chunk as it was) and read back.
#include "gltf.h"
#include "modelfit.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

static int g_failed;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failed++;                                                 \
        }                                                               \
    } while (0)

static std::string Read(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

// A model written here: quads (a floor at y 3 from -15 to 25 across and a wall) and a third placed
// by a matrix, turned a quarter about Y and doubled, at (10, 3, -5)
static std::string SmallGltf() {
    std::vector<float> pos = {-15, 3, -10, 25, 3, -10, 25, 3, 10, -15, 3, 10,     // the floor
                              -15, 3, -10, 25, 3, -10, 25, 13, -10, -15, 13, -10, // the wall
                              -1, 0, 0, 1, 0, 0, 1, 4, 0, -1, 4, 0};              // the pillar
    std::string bin((const char *)pos.data(), pos.size() * 4);
    std::vector<unsigned short> idx = {0, 1, 2, 0, 2, 3};
    bin.append((const char *)idx.data(), idx.size() * 2);
    std::string base64;
    static const char abc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (size_t k = 0; k < bin.size(); k += 3) {
        unsigned v = (unsigned char)bin[k] << 16 | (k + 1 < bin.size() ? (unsigned char)bin[k + 1] << 8 : 0) |
                     (k + 2 < bin.size() ? (unsigned char)bin[k + 2] : 0);
        base64 += abc[v >> 18 & 63];
        base64 += abc[v >> 12 & 63];
        base64 += k + 1 < bin.size() ? abc[v >> 6 & 63] : '=';
        base64 += k + 2 < bin.size() ? abc[v & 63] : '=';
    }
    size_t points = 12 * 4 * 3;
    std::string views, accs, meshes;
    for (int m = 0; m < 3; m++) {
        std::string s = std::to_string(m);
        views += (m ? "," : "") + std::string("{\"buffer\":0,\"byteOffset\":") + std::to_string(m * 48) + ",\"byteLength\":48}";
        accs += (m ? "," : "") + std::string("{\"bufferView\":") + s + ",\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"}";
        meshes += (m ? "," : "") + std::string("{\"primitives\":[{\"attributes\":{\"POSITION\":") + s + "},\"indices\":3}]}";
    }
    views += ",{\"buffer\":0,\"byteOffset\":" + std::to_string(points) + ",\"byteLength\":12}";
    accs += ",{\"bufferView\":3,\"componentType\":5123,\"count\":6,\"type\":\"SCALAR\"}";
    return "{\"asset\":{\"version\":\"2.0\"},\"scenes\":[{\"nodes\":[0,1,2]}],\"nodes\":["
           "{\"name\":\"Floor\",\"mesh\":0},{\"name\":\"Wall\",\"mesh\":1},"
           "{\"name\":\"Pillar\",\"mesh\":2,\"matrix\":[0,0,-2,0, 0,2,0,0, 2,0,0,0, 10,3,-5,1]}],"
           "\"meshes\":[" + meshes + "],\"accessors\":[" + accs + "],\"bufferViews\":[" + views + "],"
           "\"buffers\":[{\"byteLength\":" + std::to_string(bin.size()) + ",\"uri\":\"data:application/octet-stream;base64," + base64 + "\"}]}";
}

static bool Near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

// Moving, measuring, a ray's point, a matrix made into its parts, and what the fit and the checks say
static void TestArena() {
    std::string tmp = std::string(getenv("TEMP") ? getenv("TEMP") : ".") + "\\gltf_arena.gltf";
    {
        std::ofstream f(tmp, std::ios::binary);
        f << SmallGltf();
    }
    GltfDoc doc;
    CHECK(Gltf_Load(doc, tmp));
    CHECK(doc.nodes.size() == 3 && doc.nodes[2].hasMatrix && !doc.nodes[0].hasMatrix && doc.meshes.size() == 3);
    CHECK(Gltf_Roots(doc).size() == 3);

    std::vector<GltfMat> world;
    std::vector<float> none;
    Gltf_Pose(doc, none, world);
    // As the engine: the matrix is not read
    CHECK(Near(world[2][12], 0));
    ModelCheckIn in;
    in.place.fov = 30, in.place.width = 1280, in.place.height = 720;
    in.place.offset[1] = -0.2f, in.place.offset[2] = -1;
    in.place.scale[0] = in.place.scale[1] = in.place.scale[2] = 0.05f;
    in.left = -300, in.right = 300;
    auto says = [](const std::vector<StageFinding> &found, const char *word) {
        for (const StageFinding &f : found)
            if (f.text.find(word) != std::string::npos) return true;
        return false;
    };
    std::vector<StageFinding> found = Model_Check(doc, world, in);
    for (const StageFinding &f : found) printf("  [%d] %s\n", (int)f.level, f.text.c_str());
    CHECK(says(found, "matrix"));
    CHECK(says(found, "above the model's origin"));
    CHECK(says(found, "No light"));

    CHECK(Gltf_BakeMatrices(doc) == 1);
    Gltf_Pose(doc, none, world);
    float a[3] = {0, 0, 0}, b[3];
    Gltf_Apply(world[2], a, b);
    CHECK(Near(b[0], 10) && Near(b[1], 3) && Near(b[2], -5));
    float x[3] = {1, 0, 0};
    Gltf_Apply(world[2], x, b);
    CHECK(Near(b[0], 10) && Near(b[1], 3) && Near(b[2], -7)); // the matrix's first column: (0, 0, -2)
    float lo[3], hi[3];
    CHECK(Gltf_Bounds(doc, world, lo, hi));
    CHECK(Near(lo[0], -15) && Near(hi[0], 25) && Near(lo[1], 3) && Near(hi[1], 13) && Near(lo[2], -10) && Near(hi[2], 10));
    float y = 0;
    CHECK(Model_FloorAt(doc, world, 5, 0, &y) && Near(y, 3));
    CHECK(!Model_FloorAt(doc, world, 60, 0, &y));

    float from[3] = {5, 20, 2}, to[3] = {5, -20, 2}, hit[3];
    CHECK(Gltf_Cast(doc, world, {}, from, to, hit) == 0);
    CHECK(Near(hit[0], 5) && Near(hit[1], 3) && Near(hit[2], 2));

    // The floor's middle made the origin, kept when saved and read back
    float by[3] = {-5, -3, 0};
    Gltf_MoveRoots(doc, by);
    std::string error;
    CHECK(Gltf_Save(doc, &error, false));
    CHECK(GetFileAttributesA((tmp + ".bak").c_str()) == INVALID_FILE_ATTRIBUTES);
    GltfDoc back;
    CHECK(Gltf_Load(back, tmp));
    CHECK(!back.nodes[2].hasMatrix && Near(back.nodes[0].t[0], -5) && Near(back.nodes[0].t[1], -3) && Near(back.nodes[2].t[0], 5));
    Gltf_Pose(back, none, world);
    CHECK(Gltf_Bounds(back, world, lo, hi));
    CHECK(Near(lo[0], -20) && Near(hi[0], 20) && Near(lo[1], 0));

    ModelFit fit;
    memcpy(fit.lo, lo, sizeof(lo)), memcpy(fit.hi, hi, sizeof(hi));
    memcpy(fit.loAll, lo, sizeof(lo)), memcpy(fit.hiAll, hi, sizeof(hi));
    fit.fighter = 1.8f;
    Model_Fit(fit);
    // Read back as the engine works it out: a fighter 300 of the stage's units tall, on the line asked for
    CHECK(Model_Zoffset(fit.place) == 600);
    CHECK(Near(Model_Units(fit.place) * 1.8f, 300));
    CHECK(fit.left == -1280 && fit.right == 1280 && fit.high == -216);
    CHECK(fit.place.zFar >= 10);
    in.place = fit.place;
    in.left = (float)fit.left, in.right = (float)fit.right;
    in.environment = true;
    found = Model_Check(back, world, in);
    for (const StageFinding &f : found) printf("  [%d] %s\n", (int)f.level, f.text.c_str());
    CHECK(!says(found, "matrix") && !says(found, "origin") && !says(found, "No light"));

    // A floor short of where they walk, and a far plane too near
    in.right = 6000, in.place.zFar = 1;
    found = Model_Check(back, world, in);
    CHECK(says(found, "No floor under") && says(found, "is not drawn"));
    DeleteFileA(tmp.c_str());
}

int main() {
    TestArena();
    const std::string src = "stages/stage3d.glb";
    if (GetFileAttributesA(src.c_str()) == INVALID_FILE_ATTRIBUTES) {
        printf("skipped (missing): %s\n", src.c_str());
        printf(g_failed ? "gltf-test: %d failed\n" : "gltf-test: all passed\n", g_failed);
        return g_failed ? 1 : 0;
    }
    GltfDoc doc;
    CHECK(Gltf_Load(doc, src));
    printf("%s: %zu nodes, %zu meshes, %zu materials, %zu images, %zu animations, %zu lights\n", src.c_str(),
           doc.nodes.size(), doc.meshes.size(), doc.materials.size(), doc.images.size(), doc.anims.size(), doc.lights.size());
    CHECK(doc.glb && doc.nodes.size() == 9 && doc.materials.size() == 2 && doc.anims.size() == 6);
    CHECK(doc.scenes.size() == 1 && doc.scenes[0].roots.size() == 9);
    CHECK(doc.materials[0].unlit && doc.materials[0].baseTexture == 0 && doc.materials[0].roughness > 0.89f);
    size_t triangles = 0;
    for (const auto &m : doc.meshes)
        for (const auto &p : m.prims) {
            triangles += p.idx.size() / 3;
            CHECK(!p.nrm.empty() && !p.uv.empty());
        }
    CHECK(triangles > 0);
    CHECK(doc.anims[0].duration > 4 && doc.anims[0].duration < 4.3f);

    // The JSON written reads back as the same text
    Json again;
    std::string text = doc.json.dump();
    CHECK(Json::Parse(text.c_str(), again) && again.dump() == text);

    // Posed, an animation moves its node; a ray at a mesh's middle picks a node
    std::vector<GltfMat> rest, moved;
    Gltf_Pose(doc, std::vector<float>(doc.anims.size(), -1.0f), rest);
    std::vector<float> times(doc.anims.size(), -1.0f);
    times[0] = 1.0f;
    Gltf_Pose(doc, times, moved);
    int gong = doc.anims[0].channels.empty() ? 0 : doc.anims[0].channels[0].node;
    CHECK(rest[gong] != moved[gong]);
    float corners[8][3];
    CHECK(Gltf_Corners(doc, rest, 2, corners));
    float mid[3] = {(corners[0][0] + corners[7][0]) / 2, (corners[0][1] + corners[7][1]) / 2, (corners[0][2] + corners[7][2]) / 2};
    float from[3] = {mid[0], mid[1] + 1000, mid[2]}, towards[3] = {mid[0], mid[1] - 1000, mid[2]};
    int picked = Gltf_Pick(doc, rest, std::vector<char>(doc.nodes.size(), 1), from, towards);
    printf("picked from above node 2's middle: %d\n", picked);
    CHECK(picked >= 0);

    // Saved into a copy, read back
    std::string tmp = std::string(getenv("TEMP") ? getenv("TEMP") : ".") + "\\gltf_test.glb";
    DeleteFileA(tmp.c_str());
    DeleteFileA((tmp + ".bak").c_str());
    CHECK(CopyFileA(src.c_str(), tmp.c_str(), FALSE));
    GltfDoc copy;
    CHECK(Gltf_Load(copy, tmp));
    copy.nodes[2].t[0] = 1.73f;
    copy.trs.insert(2);
    copy.nodes[2].trans = 1;
    copy.nodes[2].shadow = false;
    copy.props.insert(2);
    copy.materials[0].color[0] = 0.5f;
    copy.materials[0].unlit = false;
    copy.mats.insert(0);
    std::string error;
    CHECK(Gltf_Save(copy, &error));
    if (!error.empty()) printf("%s\n", error.c_str());
    CHECK(Read(tmp + ".bak") == Read(src));
    GltfDoc back;
    CHECK(Gltf_Load(back, tmp));
    CHECK(back.nodes.size() == 9 && back.nodes[2].t[0] == 1.73f && back.nodes[2].trans == 1 && !back.nodes[2].shadow);
    CHECK(back.materials[0].color[0] == 0.5f && !back.materials[0].unlit && back.materials[1].unlit);
    CHECK(back.bin == doc.bin);
    CHECK(back.json["nodes"][(size_t)2]["extras"]["trans"].str() == "ADD");
    CHECK(back.json["nodes"][(size_t)2]["extras"]["castShadow"].str() == "0");
    // What was not changed is as it was
    CHECK(back.json["nodes"][(size_t)1].dump() == doc.json["nodes"][(size_t)1].dump());
    CHECK(back.json["animations"].dump() == doc.json["animations"].dump());

    // Keys: one value changed (written over, the binary chunk the same size), then a key added to
    // another channel (its times and values at the end of the binary chunk)
    GltfAnim::Channel &c0 = back.anims[0].channels[0];
    int n0 = c0.path == 1 ? 4 : 3, per0 = c0.interp == 2 ? 3 : 1;
    size_t keys0 = c0.in.size();
    printf("animation 0, channel 0: node %d, path %d, sampler %d, interp %d, %zu keys\n", c0.node, c0.path, c0.sampler, c0.interp,
           keys0);
    CHECK(c0.sampler >= 0 && keys0 > 1);
    c0.out[(size_t)(per0 == 3 ? 1 : 0) * n0] += 0.25f; // the first key's value
    float changed = c0.out[(size_t)(per0 == 3 ? 1 : 0) * n0];
    back.keys.insert({0, c0.sampler});
    CHECK(Gltf_Save(back, &error));
    GltfDoc keys;
    CHECK(Gltf_Load(keys, tmp));
    CHECK(keys.bin.size() == doc.bin.size());
    CHECK(keys.anims[0].channels[0].in == doc.anims[0].channels[0].in);
    CHECK(keys.anims[0].channels[0].out[(size_t)(per0 == 3 ? 1 : 0) * n0] == changed);
    // The other animations as they were
    for (size_t a = 1; a < doc.anims.size(); a++) CHECK(keys.anims[a].channels.size() == doc.anims[a].channels.size());
    GltfAnim::Channel &c1 = keys.anims[1].channels[0];
    std::vector<float> in1 = c1.in, out1 = c1.out;
    int n1 = c1.path == 1 ? 4 : 3;
    float between = (c1.in[0] + c1.in[1]) / 2, v[4];
    Gltf_Sample(c1, between, v);
    CHECK(c1.interp != 2);
    c1.in.insert(c1.in.begin() + 1, between);
    c1.out.insert(c1.out.begin() + n1, v, v + n1);
    keys.keys.insert({1, c1.sampler});
    CHECK(Gltf_Save(keys, &error));
    if (!error.empty()) printf("%s\n", error.c_str());
    GltfDoc added;
    CHECK(Gltf_Load(added, tmp));
    CHECK(added.bin.size() > doc.bin.size());
    CHECK(added.anims[1].channels[0].in.size() == in1.size() + 1 && added.anims[1].channels[0].in[1] == between);
    CHECK(added.anims[1].channels[0].out.size() == out1.size() + n1);
    CHECK(added.anims[0].channels[0].out[(size_t)(per0 == 3 ? 1 : 0) * n0] == changed);
    const Json &acc = added.json["accessors"][(size_t)added.json["animations"][(size_t)1]["samplers"][(size_t)c1.sampler]["input"].i()];
    CHECK(acc["max"][(size_t)0].num() > 0 && acc["count"].i() == (int)in1.size() + 1);
    // Posed between its keys, the node is where it was (the key added was the value there)
    std::vector<float> t1(doc.anims.size(), -1.0f);
    t1[1] = between;
    std::vector<GltfMat> before, after;
    Gltf_Pose(doc, t1, before);
    Gltf_Pose(added, t1, after);
    int node1 = c1.node;
    float diff = 0;
    for (int k = 0; k < 16; k++) diff = std::max(diff, std::fabs(before[node1][k] - after[node1][k]));
    CHECK(diff < 1e-4f);
    DeleteFileA(tmp.c_str());
    DeleteFileA((tmp + ".bak").c_str());

    printf(g_failed ? "gltf-test: %d failed\n" : "gltf-test: all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
