// A glTF model read, posed, picked and written: see gltf.h. What model.go does is followed: a
// node's matrix is not read (its translation, rotation and scale are), Ikemen's extras are read
// as loadglTFModel reads them, and Save writes what editor_model.go's editorModelSave writes.
#include "gltf.h"

#include <windows.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

// ---------------------------------------------------------------------------
// Matrices (column-major) and quaternions (x, y, z, w)
// ---------------------------------------------------------------------------

GltfMat Gltf_Identity() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }

GltfMat Gltf_Mul(const GltfMat &a, const GltfMat &b) {
    GltfMat m{};
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++) {
            float v = 0;
            for (int k = 0; k < 4; k++) v += a[k * 4 + r] * b[c * 4 + k];
            m[c * 4 + r] = v;
        }
    return m;
}

GltfMat Gltf_Invert(const GltfMat &m) {
    // General 4x4 inverse (cofactors)
    GltfMat inv{};
    const float *a = m.data();
    inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
    inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
    inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
    inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
    inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
    inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
    inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
    inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
    inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
    inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
    inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
    inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
    inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
    inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
    inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
    inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
    float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
    if (std::fabs(det) < 1e-30f) return Gltf_Identity();
    for (float &v : inv) v /= det;
    return inv;
}

GltfMat Gltf_Compose(const float t[3], const float r[4], const float s[3]) {
    float x = r[0], y = r[1], z = r[2], w = r[3];
    GltfMat m = {1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0,
                 2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0,
                 2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y), 0,
                 t[0], t[1], t[2], 1};
    for (int k = 0; k < 3; k++) m[k] *= s[0], m[4 + k] *= s[1], m[8 + k] *= s[2];
    return m;
}

void Gltf_Apply(const GltfMat &m, const float in[3], float out[3]) {
    float v[3];
    for (int r = 0; r < 3; r++) v[r] = m[r] * in[0] + m[4 + r] * in[1] + m[8 + r] * in[2] + m[12 + r];
    memcpy(out, v, sizeof(v));
}

static void Slerp(const float *a, const float *b, float t, float *out) {
    float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    float bb[4] = {b[0], b[1], b[2], b[3]};
    if (d < 0) {
        d = -d;
        for (float &v : bb) v = -v;
    }
    float ka = 1 - t, kb = t;
    if (d < 0.9995f) {
        float th = acosf(d), sn = sinf(th);
        ka = sinf((1 - t) * th) / sn, kb = sinf(t * th) / sn;
    }
    float len = 0;
    for (int k = 0; k < 4; k++) out[k] = ka * a[k] + kb * bb[k], len += out[k] * out[k];
    len = sqrtf(len);
    if (len > 0)
        for (int k = 0; k < 4; k++) out[k] /= len;
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

namespace {

bool ReadAll(const std::string &path, std::string &out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::stringstream s;
    s << in.rdbuf();
    out = s.str();
    return true;
}

std::string DirOf(const std::string &path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? "" : path.substr(0, slash + 1);
}

std::string UrlDecode(const std::string &s) {
    std::string out;
    for (size_t k = 0; k < s.size(); k++) {
        if (s[k] == '%' && k + 2 < s.size()) {
            out += (char)strtol(s.substr(k + 1, 2).c_str(), nullptr, 16);
            k += 2;
        } else {
            out += s[k];
        }
    }
    return out;
}

std::string Base64(const std::string &s) {
    auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+' || c == '-') return 62;
        if (c == '/' || c == '_') return 63;
        return -1;
    };
    std::string out;
    int bits = 0, acc = 0;
    for (char c : s) {
        int v = value(c);
        if (v < 0) continue;
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += (char)((acc >> bits) & 0xFF);
        }
    }
    return out;
}

// A uri's bytes: data: (base64) or a file next to the model
bool UriBytes(const std::string &model, const std::string &uri, std::string &out) {
    if (uri.compare(0, 5, "data:") == 0) {
        size_t comma = uri.find(',');
        if (comma == std::string::npos) return false;
        out = Base64(uri.substr(comma + 1));
        return true;
    }
    return ReadAll(DirOf(model) + UrlDecode(uri), out);
}

int Components(const std::string &type) {
    if (type == "SCALAR") return 1;
    if (type == "VEC2") return 2;
    if (type == "VEC3") return 3;
    if (type == "VEC4") return 4;
    if (type == "MAT2") return 4;
    if (type == "MAT3") return 9;
    if (type == "MAT4") return 16;
    return 0;
}

// A bufferView's bytes; nullptr when it is not there
const unsigned char *ViewBytes(const GltfDoc &doc, int view, size_t *size, size_t *stride) {
    const Json &bv = doc.json["bufferViews"][(size_t)view];
    if (bv.isNull()) return nullptr;
    size_t buffer = (size_t)bv["buffer"].i(), offset = (size_t)bv["byteOffset"].num(0), length = (size_t)bv["byteLength"].num(0);
    if (buffer >= doc.buffers.size() || offset + length > doc.buffers[buffer].size()) return nullptr;
    if (size) *size = length;
    if (stride) *stride = (size_t)bv["byteStride"].num(0);
    return (const unsigned char *)doc.buffers[buffer].data() + offset;
}

// An accessor's values as floats (normalized integers made 0..1 or -1..1), n per element
bool ReadAccessor(const GltfDoc &doc, int index, std::vector<float> &out, int *perElement = nullptr) {
    out.clear();
    const Json &a = doc.json["accessors"][(size_t)index];
    if (a.isNull()) return false;
    int n = Components(a["type"].str()), type = a["componentType"].i();
    size_t count = (size_t)a["count"].num(0);
    bool norm = a["normalized"].truthy();
    if (perElement) *perElement = n;
    if (n == 0) return false;
    out.assign(count * n, 0.0f);
    if (a["bufferView"].isNull()) return true; // zeros (a sparse accessor's base, not read)
    size_t compSize = type == 5126 || type == 5125 ? 4 : type == 5122 || type == 5123 ? 2 : 1;
    size_t size = 0, stride = 0;
    const unsigned char *base = ViewBytes(doc, a["bufferView"].i(), &size, &stride);
    size_t offset = (size_t)a["byteOffset"].num(0);
    if (!stride) stride = compSize * n;
    if (!base || (count && offset + (count - 1) * stride + compSize * n > size)) return false;
    for (size_t e = 0; e < count; e++) {
        const unsigned char *p = base + offset + e * stride;
        for (int c = 0; c < n; c++, p += compSize) {
            float v = 0;
            switch (type) {
            case 5126: memcpy(&v, p, 4); break;
            case 5125: {
                unsigned u;
                memcpy(&u, p, 4);
                v = (float)u;
                break;
            }
            case 5123: {
                unsigned short u;
                memcpy(&u, p, 2);
                v = norm ? u / 65535.0f : u;
                break;
            }
            case 5122: {
                short u;
                memcpy(&u, p, 2);
                v = norm ? std::max(u / 32767.0f, -1.0f) : u;
                break;
            }
            case 5121: v = norm ? *p / 255.0f : *p; break;
            case 5120: v = norm ? std::max((signed char)*p / 127.0f, -1.0f) : (signed char)*p; break;
            }
            out[e * n + c] = v;
        }
    }
    return true;
}

void ReadFloatsInto(const Json &a, float *out, int count) {
    for (int k = 0; k < count && k < (int)a.size(); k++) out[k] = (float)a[(size_t)k].num(out[k]);
}

// An extra's string, as model.go compares it ("0", "false")
bool ExtraIs(const Json &v, const char *a, const char *b) { return v.type == Json::String && (v.s == a || v.s == b); }

} // namespace

bool Gltf_Load(GltfDoc &doc, const std::string &path, const GltfDecoder &decode) {
    doc = GltfDoc();
    doc.path = path;
    std::string data;
    if (!ReadAll(path, data)) {
        doc.error = "could not read " + path;
        return false;
    }
    std::string text;
    if (data.size() >= 12 && memcmp(data.data(), "glTF", 4) == 0) {
        doc.glb = true;
        size_t at = 12;
        while (at + 8 <= data.size()) {
            unsigned length, type;
            memcpy(&length, data.data() + at, 4);
            memcpy(&type, data.data() + at + 4, 4);
            at += 8;
            if (at + length > data.size()) break;
            if (type == 0x4E4F534A && text.empty()) text = data.substr(at, length);
            else if (type == 0x004E4942 && doc.bin.empty()) doc.bin = data.substr(at, length);
            at += length;
        }
    } else {
        text = data;
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\0')) text.pop_back();
    if (!Json::Parse(text.c_str(), doc.json) || doc.json.type != Json::Object) {
        doc.error = path + ": its JSON could not be read";
        return false;
    }
    const Json &j = doc.json;

    for (const Json &b : j["buffers"].items) {
        std::string bytes;
        if (b["uri"].isNull()) bytes = doc.bin;
        else if (!UriBytes(path, b["uri"].str(), bytes)) doc.error = "buffer " + b["uri"].str() + " not found";
        doc.buffers.push_back(bytes);
    }

    for (const Json &s : j["scenes"].items) {
        GltfScene sc;
        sc.name = s["name"].str();
        for (const Json &n : s["nodes"].items) sc.roots.push_back(n.i());
        doc.scenes.push_back(sc);
    }

    for (const Json &m : j["meshes"].items) {
        GltfMesh mesh;
        mesh.name = m["name"].str();
        for (const Json &p : m["primitives"].items) {
            GltfPrim prim;
            prim.material = p["material"].i(-1);
            prim.mode = p["mode"].i(4);
            const Json &at = p["attributes"];
            int n = 0;
            if (at["POSITION"].isNull() || !ReadAccessor(doc, at["POSITION"].i(), prim.pos, &n) || n != 3) continue;
            if (!at["NORMAL"].isNull()) ReadAccessor(doc, at["NORMAL"].i(), prim.nrm);
            if (!at["TEXCOORD_0"].isNull()) ReadAccessor(doc, at["TEXCOORD_0"].i(), prim.uv);
            size_t vertices = prim.pos.size() / 3;
            if (prim.nrm.size() != vertices * 3) prim.nrm.clear();
            if (prim.uv.size() != vertices * 2) prim.uv.clear();
            std::vector<unsigned> idx;
            if (!p["indices"].isNull()) {
                std::vector<float> f;
                ReadAccessor(doc, p["indices"].i(), f);
                for (float v : f) idx.push_back((unsigned)v);
            } else {
                for (size_t k = 0; k < vertices; k++) idx.push_back((unsigned)k);
            }
            idx.erase(std::remove_if(idx.begin(), idx.end(), [&](unsigned v) { return v >= vertices; }), idx.end());
            // Strips and fans as a list of triangles
            if (prim.mode == 5 || prim.mode == 6) {
                for (size_t k = 0; k + 2 < idx.size(); k++) {
                    if (prim.mode == 5) {
                        bool odd = k & 1;
                        prim.idx.insert(prim.idx.end(), {idx[k + (odd ? 1 : 0)], idx[k + (odd ? 0 : 1)], idx[k + 2]});
                    } else {
                        prim.idx.insert(prim.idx.end(), {idx[0], idx[k + 1], idx[k + 2]});
                    }
                }
                prim.mode = 4;
            } else {
                prim.idx = idx;
                if (prim.mode == 4) prim.idx.resize(prim.idx.size() / 3 * 3);
            }
            for (int a = 0; a < 3; a++) prim.lo[a] = FLT_MAX, prim.hi[a] = -FLT_MAX;
            for (size_t v = 0; v < vertices; v++)
                for (int a = 0; a < 3; a++)
                    prim.lo[a] = std::min(prim.lo[a], prim.pos[v * 3 + a]), prim.hi[a] = std::max(prim.hi[a], prim.pos[v * 3 + a]);
            mesh.prims.push_back(std::move(prim));
        }
        doc.meshes.push_back(std::move(mesh));
    }

    for (const Json &n : j["nodes"].items) {
        GltfNode node;
        node.name = n["name"].str();
        node.mesh = n["mesh"].i(-1);
        if (node.mesh >= (int)doc.meshes.size()) node.mesh = -1;
        for (const Json &c : n["children"].items) node.children.push_back(c.i());
        // As model.go: translation, rotation and scale (a matrix is not read)
        ReadFloatsInto(n["translation"], node.t, 3);
        ReadFloatsInto(n["rotation"], node.r, 4);
        ReadFloatsInto(n["scale"], node.s, 3);
        const Json &light = n["extensions"]["KHR_lights_punctual"];
        if (!light.isNull()) node.light = light["light"].i(-1);
        const Json &x = n["extras"];
        if (x.type == Json::Object) {
            const Json &trans = x["trans"];
            node.trans = trans.str() == "ADD" ? 1 : trans.str() == "SUB" ? 2 : trans.str() == "MUL" ? 3 : 0;
            if (x.has("disableZTest") && !ExtraIs(x["disableZTest"], "0", "false")) node.ztest = false;
            if (x.has("disableZWrite") && !ExtraIs(x["disableZWrite"], "0", "false")) node.zwrite = false;
            if (ExtraIs(x["castShadow"], "0", "false")) node.shadow = false;
            if (x["layerNumber"].type == Json::Number) node.hasLayer = true, node.layer = x["layerNumber"].i();
            node.outline = (float)x["meshOutline"].num(0);
        }
        doc.nodes.push_back(node);
    }
    for (int i = 0; i < (int)doc.nodes.size(); i++)
        for (int c : doc.nodes[i].children)
            if (c >= 0 && c < (int)doc.nodes.size()) doc.nodes[c].parent = i;

    for (const Json &m : j["materials"].items) {
        GltfMaterial mat;
        mat.name = m["name"].str();
        const Json &pbr = m["pbrMetallicRoughness"];
        ReadFloatsInto(pbr["baseColorFactor"], mat.color, 4);
        mat.metallic = (float)pbr["metallicFactor"].num(1);
        mat.roughness = (float)pbr["roughnessFactor"].num(1);
        ReadFloatsInto(m["emissiveFactor"], mat.emission, 3);
        if (!pbr["baseColorTexture"].isNull()) mat.baseTexture = pbr["baseColorTexture"]["index"].i(-1), mat.tex |= 1;
        if (!m["normalTexture"].isNull()) mat.tex |= 2;
        if (!pbr["metallicRoughnessTexture"].isNull()) mat.tex |= 4;
        if (!m["occlusionTexture"].isNull()) mat.tex |= 8, mat.ao = (float)m["occlusionTexture"]["strength"].num(1);
        if (!m["emissiveTexture"].isNull()) mat.tex |= 16;
        std::string mode = m["alphaMode"].str();
        mat.alpha = mode == "MASK" ? 1 : mode == "BLEND" ? 2 : 0;
        mat.cutoff = (float)m["alphaCutoff"].num(0.5);
        mat.doubleSided = m["doubleSided"].truthy();
        mat.unlit = m["extensions"].has("KHR_materials_unlit");
        doc.materials.push_back(mat);
    }

    for (const Json &im : j["images"].items) {
        GltfImage img;
        img.name = im["name"].str();
        std::string bytes;
        if (!im["bufferView"].isNull()) {
            size_t size = 0;
            if (const unsigned char *p = ViewBytes(doc, im["bufferView"].i(), &size, nullptr)) bytes.assign((const char *)p, size);
        } else if (!im["uri"].isNull()) {
            UriBytes(path, im["uri"].str(), bytes);
            if (img.name.empty()) img.name = im["uri"].str().compare(0, 5, "data:") == 0 ? "" : im["uri"].str();
        }
        if (decode && !bytes.empty() && !decode((const unsigned char *)bytes.data(), bytes.size(), img.w, img.h, img.rgba))
            img.rgba.clear(), img.w = img.h = 0;
        doc.images.push_back(std::move(img));
    }
    for (const Json &t : j["textures"].items) {
        GltfTexture tex;
        tex.image = t["source"].i(-1);
        const Json &s = j["samplers"][(size_t)t["sampler"].i(-1)];
        if (!s.isNull()) {
            tex.wrapS = s["wrapS"].i(10497), tex.wrapT = s["wrapT"].i(10497);
            tex.nearest = s["magFilter"].i(9729) == 9728;
        }
        doc.textures.push_back(tex);
    }

    for (const Json &a : j["animations"].items) {
        GltfAnim anim;
        anim.name = a["name"].str();
        for (const Json &c : a["channels"].items) {
            std::string p = c["target"]["path"].str();
            GltfAnim::Channel ch;
            ch.node = c["target"]["node"].i(-1);
            ch.path = p == "translation" ? 0 : p == "rotation" ? 1 : p == "scale" ? 2 : -1;
            if (ch.path < 0 || ch.node < 0 || ch.node >= (int)doc.nodes.size()) continue;
            const Json &s = a["samplers"][(size_t)c["sampler"].i(-1)];
            std::string interp = s["interpolation"].str();
            ch.interp = interp == "STEP" ? 1 : interp == "CUBICSPLINE" ? 2 : 0;
            if (!ReadAccessor(doc, s["input"].i(-1), ch.in) || !ReadAccessor(doc, s["output"].i(-1), ch.out)) continue;
            size_t need = ch.in.size() * (ch.path == 1 ? 4 : 3) * (ch.interp == 2 ? 3 : 1);
            if (ch.in.empty() || ch.out.size() < need) continue;
            anim.duration = std::max(anim.duration, ch.in.back());
            anim.channels.push_back(std::move(ch));
        }
        doc.anims.push_back(std::move(anim));
    }

    for (const Json &l : j["extensions"]["KHR_lights_punctual"]["lights"].items) {
        GltfLight light;
        light.name = l["name"].str();
        std::string type = l["type"].str();
        light.type = type == "point" ? 1 : type == "spot" ? 2 : 0;
        ReadFloatsInto(l["color"], light.rgb, 3);
        light.intensity = (float)l["intensity"].num(1);
        light.range = (float)l["range"].num(-1);
        light.inner = (float)l["spot"]["innerConeAngle"].num(0);
        light.outer = (float)l["spot"]["outerConeAngle"].num(0.785398);
        doc.lights.push_back(light);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Posing, picking, boxes
// ---------------------------------------------------------------------------

namespace {

// A channel's value at a time (n: 3, or 4 for a rotation)
void Sample(const GltfAnim::Channel &c, float time, float *out) {
    int n = c.path == 1 ? 4 : 3;
    size_t keys = c.in.size();
    auto value = [&](size_t k) { return &c.out[(c.interp == 2 ? k * 3 + 1 : k) * n]; };
    if (time <= c.in[0] || keys == 1) {
        memcpy(out, value(0), n * sizeof(float));
        return;
    }
    if (time >= c.in[keys - 1]) {
        memcpy(out, value(keys - 1), n * sizeof(float));
        return;
    }
    size_t k = std::upper_bound(c.in.begin(), c.in.end(), time) - c.in.begin() - 1;
    float t0 = c.in[k], t1 = c.in[k + 1], dt = t1 - t0, u = dt > 0 ? (time - t0) / dt : 0;
    if (c.interp == 1) {
        memcpy(out, value(k), n * sizeof(float));
    } else if (c.interp == 2) {
        const float *v0 = value(k), *v1 = value(k + 1);
        const float *b0 = &c.out[(k * 3 + 2) * n], *a1 = &c.out[((k + 1) * 3) * n];
        float u2 = u * u, u3 = u2 * u;
        for (int i = 0; i < n; i++)
            out[i] = (2 * u3 - 3 * u2 + 1) * v0[i] + (u3 - 2 * u2 + u) * dt * b0[i] + (-2 * u3 + 3 * u2) * v1[i] +
                     (u3 - u2) * dt * a1[i];
        if (n == 4) {
            float len = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]);
            if (len > 0)
                for (int i = 0; i < 4; i++) out[i] /= len;
        }
    } else if (n == 4) {
        Slerp(value(k), value(k + 1), u, out);
    } else {
        const float *v0 = value(k), *v1 = value(k + 1);
        for (int i = 0; i < n; i++) out[i] = v0[i] + (v1[i] - v0[i]) * u;
    }
}

} // namespace

void Gltf_Pose(const GltfDoc &doc, const std::vector<float> &animTimes, std::vector<GltfMat> &world) {
    size_t count = doc.nodes.size();
    std::vector<std::array<float, 10>> trs(count);
    for (size_t i = 0; i < count; i++) {
        const GltfNode &n = doc.nodes[i];
        trs[i] = {n.t[0], n.t[1], n.t[2], n.r[0], n.r[1], n.r[2], n.r[3], n.s[0], n.s[1], n.s[2]};
    }
    // The animations on, in order: a later one moving the same thing wins (as the engine's)
    for (size_t a = 0; a < doc.anims.size() && a < animTimes.size(); a++) {
        if (animTimes[a] < 0) continue;
        for (const auto &c : doc.anims[a].channels) Sample(c, animTimes[a], &trs[c.node][c.path == 0 ? 0 : c.path == 1 ? 3 : 7]);
    }
    world.assign(count, Gltf_Identity());
    std::vector<char> done(count, 0);
    std::function<void(int, const GltfMat &, int)> walk = [&](int i, const GltfMat &parent, int depth) {
        if (i < 0 || i >= (int)count || done[i] || depth > 256) return;
        done[i] = 1;
        world[i] = Gltf_Mul(parent, Gltf_Compose(&trs[i][0], &trs[i][3], &trs[i][7]));
        for (int c : doc.nodes[i].children) walk(c, world[i], depth + 1);
    };
    for (size_t i = 0; i < count; i++)
        if (doc.nodes[i].parent < 0) walk((int)i, Gltf_Identity(), 0);
}

int Gltf_Pick(const GltfDoc &doc, const std::vector<GltfMat> &world, const std::vector<char> &visible,
              const float from[3], const float towards[3]) {
    float best = FLT_MAX;
    int found = -1;
    for (int i = 0; i < (int)doc.nodes.size() && i < (int)world.size(); i++) {
        const GltfNode &n = doc.nodes[i];
        if (n.mesh < 0 || (i < (int)visible.size() && !visible[i])) continue;
        GltfMat inv = Gltf_Invert(world[i]);
        float o[3], e[3], d[3];
        Gltf_Apply(inv, from, o);
        Gltf_Apply(inv, towards, e);
        for (int a = 0; a < 3; a++) d[a] = e[a] - o[a];
        for (const GltfPrim &p : doc.meshes[n.mesh].prims) {
            if (p.mode != 4) continue;
            // The box first
            float tmin = 0, tmax = best;
            for (int a = 0; a < 3 && tmin <= tmax; a++) {
                if (std::fabs(d[a]) < 1e-12f) {
                    if (o[a] < p.lo[a] || o[a] > p.hi[a]) tmin = tmax + 1;
                    continue;
                }
                float t0 = (p.lo[a] - o[a]) / d[a], t1 = (p.hi[a] - o[a]) / d[a];
                if (t0 > t1) std::swap(t0, t1);
                tmin = std::max(tmin, t0), tmax = std::min(tmax, t1);
            }
            if (tmin > tmax) continue;
            for (size_t k = 0; k + 2 < p.idx.size(); k += 3) {
                // Moller-Trumbore, both faces
                const float *va = &p.pos[p.idx[k] * 3], *vb = &p.pos[p.idx[k + 1] * 3], *vc = &p.pos[p.idx[k + 2] * 3];
                float e1[3] = {vb[0] - va[0], vb[1] - va[1], vb[2] - va[2]}, e2[3] = {vc[0] - va[0], vc[1] - va[1], vc[2] - va[2]};
                float h[3] = {d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0]};
                float det = e1[0] * h[0] + e1[1] * h[1] + e1[2] * h[2];
                if (std::fabs(det) < 1e-12f) continue;
                float f = 1 / det, s[3] = {o[0] - va[0], o[1] - va[1], o[2] - va[2]};
                float u = f * (s[0] * h[0] + s[1] * h[1] + s[2] * h[2]);
                if (u < 0 || u > 1) continue;
                float q[3] = {s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0]};
                float v = f * (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]);
                if (v < 0 || u + v > 1) continue;
                float t = f * (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]);
                if (t >= 0 && t < best) best = t, found = i;
            }
        }
    }
    return found;
}

bool Gltf_Corners(const GltfDoc &doc, const std::vector<GltfMat> &world, int node, float corners[8][3]) {
    if (node < 0 || node >= (int)doc.nodes.size() || node >= (int)world.size()) return false;
    auto meshBox = [&](int i, float lo[3], float hi[3]) {
        const GltfNode &n = doc.nodes[i];
        if (n.mesh < 0 || doc.meshes[n.mesh].prims.empty()) return false;
        for (int a = 0; a < 3; a++) lo[a] = FLT_MAX, hi[a] = -FLT_MAX;
        for (const GltfPrim &p : doc.meshes[n.mesh].prims)
            for (int a = 0; a < 3; a++) lo[a] = std::min(lo[a], p.lo[a]), hi[a] = std::max(hi[a], p.hi[a]);
        return true;
    };
    auto corner = [](const float lo[3], const float hi[3], int k, float out[3]) {
        out[0] = k & 1 ? hi[0] : lo[0], out[1] = k & 2 ? hi[1] : lo[1], out[2] = k & 4 ? hi[2] : lo[2];
    };
    float lo[3], hi[3];
    if (meshBox(node, lo, hi)) {
        for (int k = 0; k < 8; k++) {
            float c[3];
            corner(lo, hi, k, c);
            Gltf_Apply(world[node], c, corners[k]);
        }
        return true;
    }
    float wlo[3] = {FLT_MAX, FLT_MAX, FLT_MAX}, whi[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    bool any = false;
    std::function<void(int, int)> walk = [&](int i, int depth) {
        if (i < 0 || i >= (int)doc.nodes.size() || depth > 256) return;
        if (meshBox(i, lo, hi)) {
            for (int k = 0; k < 8; k++) {
                float c[3], w[3];
                corner(lo, hi, k, c);
                Gltf_Apply(world[i], c, w);
                for (int a = 0; a < 3; a++) wlo[a] = std::min(wlo[a], w[a]), whi[a] = std::max(whi[a], w[a]);
            }
            any = true;
        }
        for (int c : doc.nodes[i].children) walk(c, depth + 1);
    };
    walk(node, 0);
    if (!any) return false;
    for (int k = 0; k < 8; k++) corner(wlo, whi, k, corners[k]);
    return true;
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

namespace {

bool AllAre(const float *v, int count, float x) {
    for (int k = 0; k < count; k++)
        if (v[k] != x) return false;
    return true;
}

void SetOrErase(Json &obj, const char *key, bool keep, const Json &value) {
    if (keep) obj.at(key) = value;
    else obj.erase(key);
}

} // namespace

bool Gltf_Save(GltfDoc &doc, std::string *error) {
    Json &j = doc.json;
    for (int i : doc.trs) {
        if (i < 0 || i >= (int)j["nodes"].size()) continue;
        Json &n = j.at("nodes").items[i];
        const GltfNode &g = doc.nodes[i];
        n.erase("matrix");
        SetOrErase(n, "translation", !AllAre(g.t, 3, 0), Json::Floats(g.t, 3));
        SetOrErase(n, "rotation", !(g.r[0] == 0 && g.r[1] == 0 && g.r[2] == 0 && g.r[3] == 1), Json::Floats(g.r, 4));
        SetOrErase(n, "scale", !AllAre(g.s, 3, 1), Json::Floats(g.s, 3));
    }
    for (int i : doc.props) {
        if (i < 0 || i >= (int)j["nodes"].size()) continue;
        Json &n = j.at("nodes").items[i];
        const GltfNode &g = doc.nodes[i];
        Json &x = n.at("extras");
        const char *trans[] = {"NONE", "ADD", "SUB", "MUL"};
        SetOrErase(x, "trans", g.trans != 0, Json::Str(trans[std::clamp(g.trans, 0, 3)]));
        SetOrErase(x, "layerNumber", g.hasLayer, Json::Num(g.layer));
        SetOrErase(x, "castShadow", !g.shadow, Json::Str("0"));
        SetOrErase(x, "disableZTest", !g.ztest, Json::Str("1"));
        SetOrErase(x, "disableZWrite", !g.zwrite, Json::Str("1"));
        SetOrErase(x, "meshOutline", g.outline != 0, Json::Float(g.outline));
        if (x.fields.empty()) n.erase("extras");
    }
    bool unlit = false;
    for (int i : doc.mats) {
        if (i < 0 || i >= (int)j["materials"].size()) continue;
        Json &m = j.at("materials").items[i];
        const GltfMaterial &g = doc.materials[i];
        Json &pbr = m.at("pbrMetallicRoughness");
        SetOrErase(pbr, "baseColorFactor", !AllAre(g.color, 4, 1), Json::Floats(g.color, 4));
        SetOrErase(pbr, "metallicFactor", g.metallic != 1, Json::Float(g.metallic));
        SetOrErase(pbr, "roughnessFactor", g.roughness != 1, Json::Float(g.roughness));
        if (pbr.fields.empty()) m.erase("pbrMetallicRoughness");
        SetOrErase(m, "emissiveFactor", !AllAre(g.emission, 3, 0), Json::Floats(g.emission, 3));
        SetOrErase(m, "alphaMode", g.alpha != 0, Json::Str(g.alpha == 1 ? "MASK" : "BLEND"));
        SetOrErase(m, "alphaCutoff", g.alpha == 1 && g.cutoff != 0.5f, Json::Float(g.cutoff));
        SetOrErase(m, "doubleSided", g.doubleSided, Json::Boolean(true));
        if (m.has("occlusionTexture")) SetOrErase(m.at("occlusionTexture"), "strength", g.ao != 1, Json::Float(g.ao));
        if (g.unlit) {
            m.at("extensions").at("KHR_materials_unlit") = Json::EmptyObject();
            unlit = true;
        } else if (m.has("extensions")) {
            m.at("extensions").erase("KHR_materials_unlit");
            if (m["extensions"].fields.empty()) m.erase("extensions");
        }
    }
    if (unlit) {
        bool used = false;
        for (const Json &u : j["extensionsUsed"].items) used = used || u.str() == "KHR_materials_unlit";
        if (!used) {
            Json &list = j.at("extensionsUsed");
            if (list.type != Json::Array) list = Json(), list.type = Json::Array;
            list.items.push_back(Json::Str("KHR_materials_unlit"));
        }
    }
    for (int i : doc.lightsChanged) {
        if (i < 0 || i >= (int)doc.lights.size() || i >= (int)j["extensions"]["KHR_lights_punctual"]["lights"].size()) continue;
        Json &l = j.at("extensions").at("KHR_lights_punctual").at("lights").items[i];
        const GltfLight &g = doc.lights[i];
        l.at("color") = Json::Floats(g.rgb, 3);
        l.at("intensity") = Json::Float(g.intensity);
        SetOrErase(l, "range", g.range > 0, Json::Float(g.range));
        if (g.type == 2) {
            Json &spot = l.at("spot");
            spot.at("innerConeAngle") = Json::Float(g.inner);
            spot.at("outerConeAngle") = Json::Float(g.outer);
        }
    }

    std::string out;
    if (doc.glb) {
        std::string text = j.dump();
        while (text.size() % 4) text += ' ';
        std::string bin = doc.bin;
        while (bin.size() % 4) bin += '\0';
        auto u32 = [&](unsigned v) { out.append((const char *)&v, 4); };
        out = "glTF";
        u32(2);
        u32((unsigned)(12 + 8 + text.size() + (bin.empty() ? 0 : 8 + bin.size())));
        u32((unsigned)text.size());
        u32(0x4E4F534A);
        out += text;
        if (!bin.empty()) {
            u32((unsigned)bin.size());
            u32(0x004E4942);
            out += bin;
        }
    } else {
        out = j.dump(2);
    }
    // The file as it was, once
    std::string bak = doc.path + ".bak";
    if (GetFileAttributesA(bak.c_str()) == INVALID_FILE_ATTRIBUTES && !CopyFileA(doc.path.c_str(), bak.c_str(), TRUE)) {
        if (error) *error = "could not write " + bak;
        return false;
    }
    std::string tmp = doc.path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary);
        f << out;
        if (!f) {
            if (error) *error = "could not write " + tmp;
            return false;
        }
    }
    if (!MoveFileExA(tmp.c_str(), doc.path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileA(tmp.c_str());
        if (error) *error = "could not replace " + doc.path;
        return false;
    }
    doc.trs.clear(), doc.props.clear(), doc.mats.clear(), doc.lightsChanged.clear();
    return true;
}
