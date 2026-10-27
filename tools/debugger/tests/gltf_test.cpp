// Tests of src/gltf.cpp on the release's 3D stage (run from the root of the checkout; skipped when
// missing): the model read, its JSON written and read back the same, a node picked by a ray, and
// changes saved into a copy (its binary chunk as it was) and read back.
#include "gltf.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
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

int main() {
    const std::string src = "stages/stage3d.glb";
    if (GetFileAttributesA(src.c_str()) == INVALID_FILE_ATTRIBUTES) {
        printf("skipped (missing): %s\ngltf-test: all passed\n", src.c_str());
        return 0;
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
