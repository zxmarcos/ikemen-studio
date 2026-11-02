// The stage's 3D model. The Model window: its scenes and nodes (shown or hidden, placed), its
// meshes, materials, animations (on, off, where they are) and lights, changed while the match
// goes on (editor_model.go: debugModel and the like) and written to its glTF file; where the
// stage's def places it ([Model] offset and scale, the camera's fov). On the Game window: the
// node selected outlined, and in pick mode the node under the mouse (by its triangles).
#include "app_internal.h"

#include <cfloat>
#include <cmath>
#include <cstring>

#include "imgui_stdlib.h"
#include "ini.h"

namespace app {

bool g_modelPick;

namespace {

struct ModelView {
    bool known = false, has = false, dirty = false;
    std::string file;
    struct Scene {
        std::string name;
        std::vector<int> roots;
    };
    struct Anim {
        std::string name;
        int id = 0, loop = 0, loopCount = -1, channels = 0;
        bool enabled = false;
        float time = 0, duration = 0;
    };
    struct Node {
        std::string name;
        int mesh = -1, light = -1, parent = -1;
        std::vector<int> children;
        bool visible = true, animated = false, shadow = true, ztest = true, zwrite = true, hasLayer = false;
        float t[3] = {0, 0, 0}, r[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1}, outline = 0;
        int trans = 0, layer = 0;
    };
    struct Prim {
        int material = -1, vertices = 0, indices = 0, mode = 4;
    };
    struct Mesh {
        std::string name;
        std::vector<Prim> prims;
    };
    struct Material {
        std::string name;
        float color[4] = {1, 1, 1, 1}, emission[3] = {0, 0, 0}, metallic = 1, roughness = 1, ao = 1, cutoff = 0.5f;
        int alpha = 0, tex = 0;
        bool doubleSided = false, unlit = false, animated = false;
    };
    struct Light {
        std::string name;
        int type = 0;
        float rgb[3] = {1, 1, 1}, intensity = 1, range = -1, inner = 0, outer = 0.785f;
        bool animated = false;
    };
    std::vector<Scene> scenes;
    std::vector<Anim> anims;
    std::vector<Node> nodes;
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::vector<Light> lights;
};
ModelView g_live, g_diskView; // the model in the match, and one read from disk
ModelView *g_view = &g_live;    // the one the window shows
bool g_isLive = true;
ModelView &View() { return *g_view; }
double g_modelAsked = -1;

// A model read from disk (the game not running, or another file than the one in the match):
// shown by the debugger itself (app_modelview.cpp), changed in memory, saved by Gltf_Save
struct DiskModel {
    GltfDoc doc;
    std::string path;     // as the game names it
    std::string def;      // the stage's def naming it, "" when not known
    unsigned version = 0; // bumped at each read (the preview's textures)
    FILETIME mtime{};
    double checked = -100;
    std::vector<GltfMat> world;
    bool playing = true;
    // The preview's camera: as the game's at its start (from the def), or turning around a point
    bool gameCamera = true, orbitSet = false;
    float target[3] = {0, 0, 0}, dist = 10, yaw = 0, pitch = 0.3f;
    int hover = -1;
};
DiskModel g_disk;

// The frame geometry's "mdl" (editorModelGeometry)
struct ModelFrame {
    bool ok = false, dirty = false;
    int sel = -1, hover = -1, picked = -1;
    std::vector<float> sb, hb, sp, hp; // the selected and hovered nodes' boxes (segments) or points
    bool mats = false;                 // the selected node's matrices: it can be dragged
    GltfMat viewProj{}, parentWorld{}, world{};
    float w = 0, h = 0; // the game picture's size
};
ModelFrame g_mframe;
int g_pickedSeen = -1;

enum SelKind { kNone, kNode, kMaterial, kLight };
SelKind g_selKind = kNone;
int g_sel = -1;
bool g_reveal; // the tree opens down to the node selected and scrolls to it
int g_tab = -1; // the list's tab to bring to the front: 0 nodes, 1 materials, 2 animations, 3 lights

const char *const kLightTypes[] = {"directional", "point", "spot"};
const char *const kTrans[] = {"normal", "add", "subtract", "multiply"};
const char *const kAlpha[] = {"opaque", "mask (cut at a level)", "blend"};

void SelectNode(int i, bool reveal) {
    g_selKind = i >= 0 ? kNode : kNone;
    g_sel = i;
    g_reveal = reveal;
    if (reveal) g_tab = 0;
    if (g_isLive) SendRaw("debugModelSelect(" + std::to_string(i) + ")");
}

void Select(SelKind kind, int i) {
    g_selKind = kind;
    g_sel = i;
    if (g_isLive) SendRaw("debugModelSelect(-1)");
}

// A node shown or hidden (and its children), until the round starts again; not saved
void SetVisible(int i, bool visible, bool children) {
    ModelView &m = View();
    if (g_isLive) {
        SendRaw("debugModelNode(" + std::to_string(i) + ", " + (visible ? "true" : "false") + (children ? ", true)" : ")"));
        if (i >= 0 && i < (int)m.nodes.size()) m.nodes[i].visible = visible; // until the next answer
        return;
    }
    std::function<void(int, int)> set = [&](int n, int depth) {
        if (n < 0 || n >= (int)m.nodes.size() || depth > 256) return;
        m.nodes[n].visible = visible;
        if (children)
            for (int c : m.nodes[n].children) set(c, depth + 1);
    };
    set(i, 0);
}

// An animation on (from its start) or off
void SetAnimOn(int i, bool on) {
    ModelView::Anim &a = View().anims[i];
    a.enabled = on;
    if (on) a.time = 0, a.loop = 0;
    if (g_isLive) SendRaw("debugModelAnim(" + std::to_string(i) + ", " + (on ? "true)" : "false)"));
}

void SetAnimTime(int i, float t) {
    ModelView::Anim &a = View().anims[i];
    a.time = t;
    a.enabled = true;
    if (!g_isLive) return;
    char lua[96];
    snprintf(lua, sizeof(lua), "debugModelAnimTime(%d, %g)", i, t);
    SendRaw(lua);
    g_modelAsked = ImGui::GetTime();
}

std::string NodeName(int i) {
    if (i < 0 || i >= (int)View().nodes.size()) return "?";
    const auto &n = View().nodes[i];
    return n.name.empty() ? "node " + std::to_string(i) : n.name;
}

std::string MaterialName(int i) {
    if (i < 0 || i >= (int)View().materials.size()) return "no material";
    const auto &m = View().materials[i];
    return m.name.empty() ? "material " + std::to_string(i) : m.name;
}

std::string LightName(int i) {
    if (i < 0 || i >= (int)View().lights.size()) return "?";
    const auto &l = View().lights[i];
    return l.name.empty() ? "light " + std::to_string(i) : l.name;
}

std::string NodeLabel(int i) {
    const auto &n = View().nodes[i];
    std::string label = NodeName(i);
    if (n.mesh >= 0 && n.mesh < (int)View().meshes.size()) {
        const auto &m = View().meshes[n.mesh];
        label += "  [" + (m.name.empty() ? "mesh " + std::to_string(n.mesh) : m.name) + "]";
    }
    if (n.light >= 0) label += "  [" + LightName(n.light) + "]";
    return label;
}

// A node, or one under it, whose label has the filter
bool Matches(int i, const std::string &filter, int depth = 0) {
    if (depth > 256) return false;
    std::string low = NodeLabel(i);
    std::transform(low.begin(), low.end(), low.begin(), ::tolower);
    if (low.find(filter) != std::string::npos) return true;
    for (int c : View().nodes[i].children)
        if (c >= 0 && c < (int)View().nodes.size() && Matches(c, filter, depth + 1)) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Rotations: the glTF quaternion (x, y, z, w) shown as angles in degrees about X, then Y, then Z
// ---------------------------------------------------------------------------

const float kDeg = 57.29577951f;

void QuatToEuler(const float q[4], float e[3]) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    e[0] = atan2f(2 * (w * x + y * z), 1 - 2 * (x * x + y * y)) * kDeg;
    e[1] = asinf(std::clamp(2 * (w * y - z * x), -1.0f, 1.0f)) * kDeg;
    e[2] = atan2f(2 * (w * z + x * y), 1 - 2 * (y * y + z * z)) * kDeg;
}

void EulerToQuat(const float e[3], float q[4]) {
    float cx = cosf(e[0] / kDeg / 2), sx = sinf(e[0] / kDeg / 2);
    float cy = cosf(e[1] / kDeg / 2), sy = sinf(e[1] / kDeg / 2);
    float cz = cosf(e[2] / kDeg / 2), sz = sinf(e[2] / kDeg / 2);
    q[0] = sx * cy * cz - cx * sy * sz;
    q[1] = cx * sy * cz + sx * cy * sz;
    q[2] = cx * cy * sz - sx * sy * cz;
    q[3] = cx * cy * cz + sx * sy * sz;
}

// ---------------------------------------------------------------------------
// Animations' keys: of the model from disk, its document's channels; of the match's, asked from
// the game for each animation shown (debugModelAnimKeys). Channels sharing a sampler share its keys.
// ---------------------------------------------------------------------------

using Channel = GltfAnim::Channel;

struct LiveKeys {
    bool known = false;
    double asked = -100;
    std::vector<Channel> channels;
};
std::vector<LiveKeys> g_liveKeys; // by animation of the match's model
int g_keysAnim = -1;              // the animation whose keys are shown
struct KeySel {
    int anim = -1, sampler = -1, key = -1;
} g_key; // the key selected

// The channels moving nodes in an animation of the model shown; nullptr while the game is asked
std::vector<Channel> *KeysOf(int a) {
    if (g_view != &g_live) return a >= 0 && a < (int)g_disk.doc.anims.size() ? &g_disk.doc.anims[a].channels : nullptr;
    if (a < 0 || a >= (int)g_live.anims.size()) return nullptr;
    if (g_liveKeys.size() != g_live.anims.size()) g_liveKeys.assign(g_live.anims.size(), LiveKeys());
    LiveKeys &k = g_liveKeys[a];
    if (!k.known && ImGui::GetTime() - k.asked > 2) {
        k.asked = ImGui::GetTime();
        SendRaw("ikdbg.modelKeys(" + std::to_string(a) + ")");
    }
    return k.known ? &k.channels : nullptr;
}

Channel *ChannelOf(int a, int sampler) {
    std::vector<Channel> *chs = KeysOf(a);
    if (chs)
        for (Channel &c : *chs)
            if (c.sampler == sampler) return &c;
    return nullptr;
}

int PerKey(const Channel &c) { return (c.path == 1 ? 4 : 3) * (c.interp == 2 ? 3 : 1); }

// A key's value (a cubic spline's: the middle of in-tangent, value, out-tangent)
float *KeyValue(Channel &c, int k) { return &c.out[(size_t)k * PerKey(c) + (c.interp == 2 ? (c.path == 1 ? 4 : 3) : 0)]; }

// ---------------------------------------------------------------------------
// Undo and redo of the model's changes (the def's placement is the def's text): each gesture is
// a step, a field or a node dragged from the press to the release. One history for the match's
// model, one for the model from disk.
// ---------------------------------------------------------------------------

enum EditKind { eTRS, eProps, eMaterial, eLight, eKeys };

struct Values {
    ModelView::Node node;
    ModelView::Material material;
    ModelView::Light light;
    Channel keys;
};

struct Change {
    EditKind kind;
    int i, j; // the node, material, light; the animation and its sampler
    Values before, after;
};

struct History {
    std::string file;
    // The values before the gesture going on (taken while none goes on)
    std::vector<ModelView::Node> nodes;
    std::vector<ModelView::Material> materials;
    std::vector<ModelView::Light> lights;
    std::vector<Change> open; // the gesture's changes
    std::vector<std::vector<Change>> undo, redo;
    int frame = -1;
};
History g_histLive, g_histDisk;
bool g_replaying; // an undo or redo puts values back: not recorded

History &HistoryOf(const ModelView *v) { return v == &g_live ? g_histLive : g_histDisk; }

// The values of a thing now (in the model v, or in the history's copy); false when it is not there
bool GetValues(const std::vector<ModelView::Node> &nodes, const std::vector<ModelView::Material> &materials,
               const std::vector<ModelView::Light> &lights, EditKind k, int i, int j, Values &out) {
    if (k == eKeys) {
        const Channel *c = ChannelOf(i, j);
        if (c) out.keys = *c;
        return c != nullptr;
    }
    size_t n = k == eMaterial ? materials.size() : k == eLight ? lights.size() : nodes.size();
    if (i < 0 || i >= (int)n) return false;
    if (k == eMaterial) out.material = materials[i];
    else if (k == eLight) out.light = lights[i];
    else out.node = nodes[i];
    return true;
}

bool Same(EditKind k, const Values &a, const Values &b) {
    if (k == eTRS) {
        const auto &x = a.node, &y = b.node;
        return !memcmp(x.t, y.t, sizeof(x.t)) && !memcmp(x.r, y.r, sizeof(x.r)) && !memcmp(x.s, y.s, sizeof(x.s));
    }
    if (k == eProps) {
        const auto &x = a.node, &y = b.node;
        return x.trans == y.trans && x.hasLayer == y.hasLayer && x.layer == y.layer && x.shadow == y.shadow &&
               x.ztest == y.ztest && x.zwrite == y.zwrite && x.outline == y.outline;
    }
    if (k == eMaterial) {
        const auto &x = a.material, &y = b.material;
        return !memcmp(x.color, y.color, sizeof(x.color)) && !memcmp(x.emission, y.emission, sizeof(x.emission)) &&
               x.metallic == y.metallic && x.roughness == y.roughness && x.ao == y.ao && x.cutoff == y.cutoff &&
               x.alpha == y.alpha && x.doubleSided == y.doubleSided && x.unlit == y.unlit;
    }
    if (k == eKeys) return a.keys.interp == b.keys.interp && a.keys.in == b.keys.in && a.keys.out == b.keys.out;
    const auto &x = a.light, &y = b.light;
    return !memcmp(x.rgb, y.rgb, sizeof(x.rgb)) && x.intensity == y.intensity && x.range == y.range && x.inner == y.inner &&
           x.outer == y.outer;
}

bool IsOpen(const History &h, EditKind k, int i, int j) {
    for (const Change &c : h.open)
        if (c.kind == k && c.i == i && c.j == j) return true;
    return false;
}

// A change about to be sent: the gesture's first one of that thing keeps its value before
void Record(EditKind k, int i) {
    History &h = HistoryOf(g_view);
    if (g_replaying || IsOpen(h, k, i, -1)) return;
    ModelView &v = View();
    Change c{k, i, -1, {}, {}};
    if (!GetValues(h.nodes, h.materials, h.lights, k, i, -1, c.before) && !GetValues(v.nodes, v.materials, v.lights, k, i, -1, c.before))
        return;
    h.open.push_back(c);
}

// A sampler's keys about to change (called before they do)
void RecordKeys(int a, int sampler) {
    History &h = HistoryOf(g_view);
    if (g_replaying || IsOpen(h, eKeys, a, sampler)) return;
    Change c{eKeys, a, sampler, {}, {}};
    if (GetValues(h.nodes, h.materials, h.lights, eKeys, a, sampler, c.before)) h.open.push_back(c);
}

// Once a frame: the gesture ended (no field active, the mouse let go) becomes a step
void HistoryTick(ModelView &v) {
    History &h = HistoryOf(&v);
    int frame = ImGui::GetFrameCount();
    if (h.frame == frame) return;
    if (h.file != v.file) h = History(), h.file = v.file;
    h.frame = frame;
    bool busy = ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left);
    if (!h.open.empty() && !busy) {
        // The keys are the model shown's
        struct Restore {
            ModelView *view = g_view;
            ~Restore() { g_view = view; }
        } restore;
        g_view = &v;
        std::vector<Change> step;
        for (Change &c : h.open)
            if (GetValues(v.nodes, v.materials, v.lights, c.kind, c.i, c.j, c.after) && !Same(c.kind, c.before, c.after))
                step.push_back(c);
        h.open.clear();
        if (!step.empty()) {
            h.undo.push_back(std::move(step));
            h.redo.clear();
            if (h.undo.size() > 300) h.undo.erase(h.undo.begin());
        }
    }
    if (h.open.empty()) h.nodes = v.nodes, h.materials = v.materials, h.lights = v.lights;
}

// ---------------------------------------------------------------------------
// Changes sent to the game
// ---------------------------------------------------------------------------

void SendLiveTRS(int i, const ModelView::Node &n) {
    Record(eTRS, i);
    char lua[320];
    snprintf(lua, sizeof(lua), "debugModelNodeTRS(%d, %.9g, %.9g, %.9g, %.9g, %.9g, %.9g, %.9g, %.9g, %.9g, %.9g)", i, n.t[0], n.t[1], n.t[2],
             n.r[0], n.r[1], n.r[2], n.r[3], n.s[0], n.s[1], n.s[2]);
    SendRaw(lua);
    g_modelAsked = ImGui::GetTime();
}

// From disk: into the model read (what Save writes), the preview showing it at once
void SendTRS(int i) {
    const auto &n = View().nodes[i];
    Record(eTRS, i);
    if (!g_isLive) {
        GltfNode &d = g_disk.doc.nodes[i];
        memcpy(d.t, n.t, sizeof(d.t)), memcpy(d.r, n.r, sizeof(d.r)), memcpy(d.s, n.s, sizeof(d.s));
        g_disk.doc.trs.insert(i);
        return;
    }
    SendLiveTRS(i, n);
}

// ---------------------------------------------------------------------------
// A node dragged with the mouse, on the Game window or on the preview
// ---------------------------------------------------------------------------

// A point (w 1) or a direction (w 0) through a matrix, not divided by its w
bool Project(const GltfMat &m, const float in[3], float w, float out[4]) {
    for (int r = 0; r < 4; r++) out[r] = m[r] * in[0] + m[4 + r] * in[1] + m[8 + r] * in[2] + m[12 + r] * w;
    return out[3] != 0;
}

float Dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

void Cross(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1], out[1] = a[2] * b[0] - a[0] * b[2], out[2] = a[0] * b[1] - a[1] * b[0];
}

// Its length, it made 1 long (unchanged when 0)
float Normalize(float v[3]) {
    float l = std::sqrt(Dot(v, v));
    if (l > 0)
        for (int k = 0; k < 3; k++) v[k] /= l;
    return l;
}

// a * b (x, y, z, w)
void QuatMul(const float a[4], const float b[4], float out[4]) {
    float r[4] = {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
                  a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
    float l = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2] + r[3] * r[3]);
    for (int k = 0; k < 4; k++) out[k] = l > 0 ? r[k] / l : r[k];
}

// A clip space point back into the world
void Unproject(const GltfMat &inv, float x, float y, float z, float out[3]) {
    float in[3] = {x, y, z}, o[4];
    Project(inv, in, 1, o);
    for (int k = 0; k < 3; k++) out[k] = o[3] != 0 ? o[k] / o[3] : o[k];
}

// Its translation (in its parent's space) once the mouse moved from a to b (both -1..1 across the
// picture, y up): its origin follows the mouse at its depth; with depth, the mouse's height moves
// it along the line of sight instead (up: away). False: it is behind the camera.
bool DragTranslation(const GltfMat &viewProj, const GltfMat &parentWorld, const float t0[3], const float a[2],
                     const float b[2], bool depth, float t[3]) {
    float p[4], c[4];
    Project(parentWorld, t0, 1, p);
    if (!Project(viewProj, p, 1, c) || c[3] < 1e-4f) return false;
    GltfMat inv = Gltf_Invert(viewProj);
    auto unproject = [&](float x, float y, float z, float out[3]) {
        float in[3] = {x, y, z}, o[4];
        Project(inv, in, 1, o);
        for (int k = 0; k < 3; k++) out[k] = o[3] != 0 ? o[k] / o[3] : o[k];
    };
    float z = c[2] / c[3], ua[3], ub[3], d[3];
    unproject(a[0], a[1], z, ua);
    unproject(b[0], b[1], z, ub);
    for (int k = 0; k < 3; k++) d[k] = ub[k] - ua[k];
    if (depth) {
        float um[3], nearP[3], farP[3];
        unproject(b[0], a[1], z, um);
        unproject(c[0] / c[3], c[1] / c[3], -1, nearP);
        unproject(c[0] / c[3], c[1] / c[3], 1, farP);
        float len = std::hypot(ub[0] - um[0], ub[1] - um[1], ub[2] - um[2]) * (b[1] > a[1] ? 1 : -1);
        float dir[3] = {farP[0] - nearP[0], farP[1] - nearP[1], farP[2] - nearP[2]};
        float dl = std::hypot(dir[0], dir[1], dir[2]);
        if (dl <= 0) return false;
        for (int k = 0; k < 3; k++) d[k] = um[k] - ua[k] + dir[k] / dl * len;
    }
    // A direction: into the parent's space without its translation
    GltfMat ip = Gltf_Invert(parentWorld);
    float local[4];
    Project(ip, d, 0, local);
    for (int k = 0; k < 3; k++) t[k] = t0[k] + local[k];
    return true;
}

// Dragged, an animated node keeps its place: it is its place at rest that changes
const char *AnimatedNote(const ModelView::Node &n) {
    return n.animated ? "\nAn animation moves it: its place at rest changes (seen with the animations off)." : "";
}

// What a drag does: the Model window's and the Game window's Move, Turn, Scale
enum DragMode { kMove, kTurn, kScale };
int g_dragMode = kMove;

// How a picture of the model is on the screen: its view-projection, and the rectangle its clip
// space's -1..1 fills
struct Picture {
    GltfMat viewProj{};
    ImVec2 p0, size;
};

void ToNdc(const Picture &p, ImVec2 q, float out[2]) {
    out[0] = (q.x - p.p0.x) / p.size.x * 2 - 1, out[1] = 1 - (q.y - p.p0.y) / p.size.y * 2;
}

ImVec2 NdcToScreen(const Picture &p, float x, float y) {
    return ImVec2(p.p0.x + (x + 1) / 2 * p.size.x, p.p0.y + (1 - y) / 2 * p.size.y);
}

// A point of the world on the screen; false behind the camera
bool WorldToScreen(const Picture &p, const float w[3], ImVec2 &out) {
    float c[4];
    Project(p.viewProj, w, 1, c);
    if (c[3] < 1e-4f) return false;
    out = NdcToScreen(p, c[0] / c[3], c[1] / c[3]);
    return true;
}

// The line under a point of the screen: from the near plane, towards the far one
void Ray(const Picture &p, ImVec2 q, float from[3], float dir[3]) {
    float n[2], to[3];
    ToNdc(p, q, n);
    GltfMat inv = Gltf_Invert(p.viewProj);
    Unproject(inv, n[0], n[1], -1, from);
    Unproject(inv, n[0], n[1], 1, to);
    for (int k = 0; k < 3; k++) dir[k] = to[k] - from[k];
}

// Where the line under a point of the screen passes nearest to the line o + d * s: its s; false
// when they go the same way
bool LineParam(const Picture &p, ImVec2 q, const float o[3], const float d[3], float &s) {
    float r0[3], rd[3], w[3];
    Ray(p, q, r0, rd);
    for (int k = 0; k < 3; k++) w[k] = o[k] - r0[k];
    float a = Dot(d, d), b = Dot(d, rd), c = Dot(rd, rd), dd = Dot(d, w), e = Dot(rd, w);
    float den = a * c - b * b;
    if (std::fabs(den) <= 1e-6f * a * c) return false;
    s = (b * e - c * dd) / den;
    return true;
}

// The handles drawn around the node selected: arrows along its parent's axes (Move), circles
// around them and one facing the camera (Turn), lines along its own axes (Scale); a square in
// the middle moves along the screen or scales the three together
enum Part { kBody, kCentre, kAxisX, kAxisY, kAxisZ, kRingX, kRingY, kRingZ, kRingView };

struct Gizmo {
    bool ok = false;
    float origin[3];  // the node's origin, in the world
    float axis[3][3]; // the handles' directions in the world, 1 long
    float len = 0;    // their length in the world
    float px = 0;     // and on the screen
    ImVec2 o, tip[3];
    std::vector<ImVec2> ring[3];
};

Gizmo MakeGizmo(const Picture &p, const GltfMat &parentWorld, const GltfMat &world, int mode) {
    Gizmo g;
    for (int k = 0; k < 3; k++) g.origin[k] = world[12 + k];
    if (!WorldToScreen(p, g.origin, g.o)) return g;
    // The world's size of a screen pixel at the node: two points 100 pixels apart at its depth
    float c[4], n[2], a[3], b[3];
    Project(p.viewProj, g.origin, 1, c);
    GltfMat inv = Gltf_Invert(p.viewProj);
    ToNdc(p, ImVec2(g.o.x, g.o.y + 100), n);
    Unproject(inv, c[0] / c[3], c[1] / c[3], c[2] / c[3], a);
    Unproject(inv, n[0], n[1], c[2] / c[3], b);
    float perPixel = std::hypot(b[0] - a[0], b[1] - a[1], b[2] - a[2]) / 100;
    if (!(perPixel > 0) || !std::isfinite(perPixel)) return g;
    g.px = ImGui::GetFontSize() * 4.5f;
    g.len = g.px * perPixel;
    const GltfMat &m = mode == kScale ? world : parentWorld;
    for (int k = 0; k < 3; k++) {
        float *d = g.axis[k];
        for (int r = 0; r < 3; r++) d[r] = m[k * 4 + r];
        if (Normalize(d) <= 0) d[0] = d[1] = d[2] = 0, d[k] = 1;
        float tip[3] = {g.origin[0] + d[0] * g.len, g.origin[1] + d[1] * g.len, g.origin[2] + d[2] * g.len};
        if (!WorldToScreen(p, tip, g.tip[k])) g.tip[k] = g.o;
        if (mode != kTurn) continue;
        // A circle around the axis: two directions square to it and to each other
        float other[3] = {0, 0, 0}, u[3], v[3];
        other[std::fabs(d[0]) < 0.9f ? 0 : 1] = 1;
        Cross(d, other, u);
        Normalize(u);
        Cross(d, u, v);
        for (int s = 0; s <= 48; s++) {
            float th = s * 6.2831853f / 48, w[3];
            for (int r = 0; r < 3; r++) w[r] = g.origin[r] + g.len * (cosf(th) * u[r] + sinf(th) * v[r]);
            ImVec2 q;
            if (!WorldToScreen(p, w, q)) {
                g.ring[k].clear();
                break;
            }
            g.ring[k].push_back(q);
        }
    }
    g.ok = true;
    return g;
}

float SegmentDistance(ImVec2 m, ImVec2 a, ImVec2 b) {
    float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
    float t = l2 > 0 ? std::clamp(((m.x - a.x) * dx + (m.y - a.y) * dy) / l2, 0.0f, 1.0f) : 0;
    return std::hypot(m.x - a.x - t * dx, m.y - a.y - t * dy);
}

// The handle under the mouse, -1 none
int GizmoHit(const Gizmo &g, int mode, ImVec2 m) {
    if (!g.ok) return -1;
    float tol = std::max(5.0f, ImGui::GetFontSize() * 0.45f), best = tol;
    int part = -1;
    if (mode != kTurn && std::fabs(m.x - g.o.x) <= tol && std::fabs(m.y - g.o.y) <= tol) return kCentre;
    for (int k = 0; k < 3; k++) {
        float d = FLT_MAX;
        if (mode == kTurn)
            for (size_t s = 1; s < g.ring[k].size(); s++) d = std::min(d, SegmentDistance(m, g.ring[k][s - 1], g.ring[k][s]));
        else
            d = SegmentDistance(m, g.o, g.tip[k]);
        if (d < best) best = d, part = (mode == kTurn ? kRingX : kAxisX) + k;
    }
    if (mode == kTurn && part < 0 && std::fabs(std::hypot(m.x - g.o.x, m.y - g.o.y) - g.px * 1.2f) < tol) part = kRingView;
    return part;
}

void DrawGizmo(ImDrawList *draw, const Gizmo &g, int mode, int hot) {
    if (!g.ok) return;
    const ImU32 cols[3] = {IM_COL32(235, 70, 70, 255), IM_COL32(90, 205, 90, 255), IM_COL32(80, 140, 255, 255)};
    const ImU32 yellow = IM_COL32(255, 220, 60, 255), white = IM_COL32(255, 255, 255, 220);
    float th = std::max(1.5f, ImGui::GetFontSize() / 7), head = ImGui::GetFontSize() * 0.45f;
    if (mode == kTurn) {
        draw->AddCircle(g.o, g.px * 1.2f, hot == kRingView ? yellow : white, 64, hot == kRingView ? th * 1.6f : th);
        for (int k = 0; k < 3; k++)
            if (g.ring[k].size() > 1)
                draw->AddPolyline(g.ring[k].data(), (int)g.ring[k].size(), hot == kRingX + k ? yellow : cols[k],
                                  hot == kRingX + k ? th * 1.6f : th);
        return;
    }
    for (int k = 0; k < 3; k++) {
        ImU32 c = hot == kAxisX + k ? yellow : cols[k];
        float w = hot == kAxisX + k ? th * 1.6f : th;
        ImVec2 a = g.o, b = g.tip[k];
        float dx = b.x - a.x, dy = b.y - a.y, l = std::hypot(dx, dy);
        draw->AddLine(a, b, c, w);
        if (l < 1) continue;
        dx /= l, dy /= l;
        if (mode == kMove)
            draw->AddTriangleFilled(ImVec2(b.x + dx * head, b.y + dy * head), ImVec2(b.x - dy * head * 0.5f, b.y + dx * head * 0.5f),
                                    ImVec2(b.x + dy * head * 0.5f, b.y - dx * head * 0.5f), c);
        else
            draw->AddRectFilled(ImVec2(b.x - head * 0.45f, b.y - head * 0.45f), ImVec2(b.x + head * 0.45f, b.y + head * 0.45f), c);
    }
    float h = std::max(4.0f, ImGui::GetFontSize() * 0.3f);
    if (hot == kCentre) draw->AddRectFilled(ImVec2(g.o.x - h, g.o.y - h), ImVec2(g.o.x + h, g.o.y + h), yellow);
    else draw->AddRect(ImVec2(g.o.x - h, g.o.y - h), ImVec2(g.o.x + h, g.o.y + h), white, 0, th);
}

const char *PartHint(int mode, int part) {
    switch (part) {
    case kAxisX: return mode == kMove ? "Drag: move it along its parent's X (red)" : "Drag: scale it along its own X (red)";
    case kAxisY: return mode == kMove ? "Drag: move it along its parent's Y (green)" : "Drag: scale it along its own Y (green)";
    case kAxisZ: return mode == kMove ? "Drag: move it along its parent's Z (blue)" : "Drag: scale it along its own Z (blue)";
    case kRingX: return "Drag around: turn it about its parent's X (red); Ctrl: by 15 degrees";
    case kRingY: return "Drag around: turn it about its parent's Y (green); Ctrl: by 15 degrees";
    case kRingZ: return "Drag around: turn it about its parent's Z (blue); Ctrl: by 15 degrees";
    case kRingView: return "Drag around: turn it as the screen turns; Ctrl: by 15 degrees";
    }
    return mode == kMove ? "Drag: move it along the screen (Shift: nearer or farther)"
           : mode == kTurn ? "Drag around its middle: turn it as the screen turns; Ctrl: by 15 degrees"
                           : "Drag right or up: scale it, the three together";
}

void SendKeys(int a, int sampler);
int AddKey(Channel &c, float t);
int KeyAt(const Channel &c, float t);
void DeleteKey(Channel &c, int k);
std::string AnimName(int i);

// A drag of a node changing a key instead of its place at rest: the animation whose keys are
// shown, on, moves the node that way (Move: its position, Turn: its rotation, Scale: its scale);
// the key at the animation's time, or one added there at the first move
struct KeyEdit {
    int anim = -1, sampler = -1, key = -1; // key -1: to be added
    bool added = false;
    float time = 0, orig[4] = {0, 0, 0, 1}; // its value at the press
};

bool g_animsTab; // the Animations tab is in front: the inspector shows the keys of the one picked

bool DragKey(int node, int mode, KeyEdit &out) {
    out = KeyEdit();
    int a = g_keysAnim;
    if (!g_animsTab || a < 0 || a >= (int)View().anims.size() || !View().anims[a].enabled) return false;
    std::vector<Channel> *chs = KeysOf(a);
    if (!chs) return false;
    int path = mode == kMove ? 0 : mode == kTurn ? 1 : 2;
    for (Channel &c : *chs)
        if (c.node == node && c.path == path) {
            out.anim = a, out.sampler = c.sampler, out.time = View().anims[a].time;
            out.key = KeyAt(c, out.time);
            if (out.key >= 0) memcpy(out.orig, KeyValue(c, out.key), (path == 1 ? 4 : 3) * sizeof(float));
            else Gltf_Sample(c, out.time, out.orig);
            return true;
        }
    return false;
}

struct NodeDrag {
    int node = -1;      // pressed on, the left button still down
    int part = kBody, mode = kMove;
    bool moving = false; // past a few pixels
    bool depth = false;  // Shift held: from, t0 taken again when it changes
    ImVec2 from;
    float t0[3], r0[4], s0[3]; // at the press
    float ot[3], orr[4], os[3]; // where it was: the right button puts it back
    float line0 = 0;            // an axis handle's parameter at the press
    GltfMat parentWorld, world;
    Picture pic;
    Gizmo gizmo; // as it was at the press
    KeyEdit key; // a key changed instead of the place at rest
};

// The value the drag changes now: the key's, or the place at rest's
const float *DragValue(const NodeDrag &g, const ModelView::Node &n) {
    if (g.key.anim >= 0) {
        if (g.key.key < 0) return g.key.orig;
        if (Channel *c = ChannelOf(g.key.anim, g.key.sampler)) return KeyValue(*c, g.key.key);
    }
    return g.mode == kMove ? n.t : g.mode == kTurn ? n.r : n.s;
}

void BeginDrag(NodeDrag &g, int node, const ModelView &v, const Picture &pic, const GltfMat &parentWorld, const GltfMat &world,
               int part, bool keys) {
    g = NodeDrag();
    g.node = node, g.part = part, g.mode = g_dragMode;
    g.from = ImGui::GetMousePos();
    const ModelView::Node &n = v.nodes[node];
    memcpy(g.t0, n.t, sizeof(g.t0)), memcpy(g.r0, n.r, sizeof(g.r0)), memcpy(g.s0, n.s, sizeof(g.s0));
    memcpy(g.ot, n.t, sizeof(g.ot)), memcpy(g.orr, n.r, sizeof(g.orr)), memcpy(g.os, n.s, sizeof(g.os));
    if (keys && DragKey(node, g.mode, g.key))
        memcpy(g.mode == kMove ? g.t0 : g.mode == kTurn ? g.r0 : g.s0, g.key.orig, (g.mode == kTurn ? 4 : 3) * sizeof(float));
    g.pic = pic, g.parentWorld = parentWorld, g.world = world;
    g.gizmo = MakeGizmo(pic, parentWorld, world, g.mode);
    g.depth = ImGui::GetIO().KeyShift;
    if (part >= kAxisX && part <= kAxisZ) {
        int k = part - kAxisX;
        if (g.mode == kMove) {
            // Along the parent's axis as its translation counts it: one unit of t moves this much
            float d[3] = {parentWorld[k * 4], parentWorld[k * 4 + 1], parentWorld[k * 4 + 2]};
            if (!LineParam(pic, g.from, g.gizmo.origin, d, g.line0)) g.node = -1;
        } else if (!LineParam(pic, g.from, g.gizmo.origin, g.gizmo.axis[k], g.line0) || std::fabs(g.line0) < 1e-6f) {
            g.node = -1;
        }
    }
}

// The angle the mouse went around a point of the screen since the press (counterclockwise)
float AngleAround(ImVec2 o, ImVec2 from, ImVec2 m) {
    float a = atan2f(-(m.y - o.y), m.x - o.x) - atan2f(-(from.y - o.y), from.x - o.x);
    if (a > 3.14159265f) a -= 6.2831853f;
    if (a < -3.14159265f) a += 6.2831853f;
    return a;
}

// The drag going on: the node moved, turned or scaled with the mouse; the right button puts it
// back (Esc would go to the game). False when it ended.
bool StepDrag(NodeDrag &g, ModelView &v, const std::function<void(int)> &send) {
    if (g.node < 0 || g.node >= (int)v.nodes.size()) return g.node = -1, false;
    ModelView::Node &n = v.nodes[g.node];
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        Channel *c = g.key.anim >= 0 && g.key.key >= 0 ? ChannelOf(g.key.anim, g.key.sampler) : nullptr;
        if (c) {
            // The key as it was, or gone when the drag added it
            if (g.key.added) DeleteKey(*c, g.key.key), g_key.key = -1;
            else memcpy(KeyValue(*c, g.key.key), g.key.orig, (c->path == 1 ? 4 : 3) * sizeof(float));
            SendKeys(g.key.anim, g.key.sampler);
        } else if (g.moving && g.key.anim < 0) {
            memcpy(n.t, g.ot, sizeof(n.t)), memcpy(n.r, g.orr, sizeof(n.r)), memcpy(n.s, g.os, sizeof(n.s));
            send(g.node);
        }
        return g.node = -1, false;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) return g.node = -1, false;
    ImVec2 m = ImGui::GetMousePos();
    if (!g.moving && std::hypot(m.x - g.from.x, m.y - g.from.y) < 3) return true;
    g.moving = true;
    ImGuiIO &io = ImGui::GetIO();
    float t[3], r[4], s[3];
    memcpy(t, g.t0, sizeof(t)), memcpy(r, g.r0, sizeof(r)), memcpy(s, g.s0, sizeof(s));
    bool axis = g.part >= kAxisX && g.part <= kAxisZ;
    int k = axis ? g.part - kAxisX : g.part >= kRingX && g.part <= kRingZ ? g.part - kRingX : -1;
    const Gizmo &gz = g.gizmo;
    char what[96] = "";
    if (g.mode == kMove && !axis) {
        bool depth = io.KeyShift;
        if (depth != g.depth) {
            // Another way of moving: from here
            g.depth = depth, g.from = m;
            memcpy(g.t0, DragValue(g, n), sizeof(g.t0));
            return true;
        }
        float a[2], b[2];
        ToNdc(g.pic, g.from, a), ToNdc(g.pic, m, b);
        if (!DragTranslation(g.pic.viewProj, g.parentWorld, g.t0, a, b, depth, t)) memcpy(t, DragValue(g, n), sizeof(t));
        snprintf(what, sizeof(what), "%.3f, %.3f, %.3f", t[0], t[1], t[2]);
    } else if (g.mode == kMove) {
        float d[3] = {g.parentWorld[k * 4], g.parentWorld[k * 4 + 1], g.parentWorld[k * 4 + 2]}, at;
        if (LineParam(g.pic, m, gz.origin, d, at)) t[k] = g.t0[k] + at - g.line0;
        else t[k] = DragValue(g, n)[k];
        snprintf(what, sizeof(what), "%c %.3f", "XYZ"[k], t[k]);
    } else if (g.mode == kTurn) {
        // About one of the parent's axes, or the one coming out of the screen (in the parent's space)
        float angle = AngleAround(gz.o, g.from, m), about[3], world[3];
        if (k >= 0) {
            about[0] = about[1] = about[2] = 0, about[k] = 1;
            float from[3], dir[3];
            Ray(g.pic, gz.o, from, dir);
            if (Dot(gz.axis[k], dir) > 0) angle = -angle; // it points away: turning looks the other way
        } else {
            float from[3], dir[3], local[4];
            Ray(g.pic, gz.o, from, dir);
            for (int a = 0; a < 3; a++) world[a] = -dir[a];
            Project(Gltf_Invert(g.parentWorld), world, 0, local);
            memcpy(about, local, sizeof(about));
            if (Normalize(about) <= 0) about[2] = 1;
        }
        if (io.KeyCtrl) angle = std::round(angle * kDeg / 15) * 15 / kDeg;
        float q[4] = {about[0] * sinf(angle / 2), about[1] * sinf(angle / 2), about[2] * sinf(angle / 2), cosf(angle / 2)};
        QuatMul(q, g.r0, r);
        snprintf(what, sizeof(what), "%.1f deg", angle * kDeg);
    } else if (axis) {
        float at;
        float f = LineParam(g.pic, m, gz.origin, gz.axis[k], at) ? at / g.line0 : 1;
        if (std::fabs(f) < 1e-3f) f = f < 0 ? -1e-3f : 1e-3f;
        s[k] = g.s0[k] * f;
        snprintf(what, sizeof(what), "%c x%.3f", "XYZ"[k], f);
    } else {
        float f = expf(((m.x - g.from.x) - (m.y - g.from.y)) / (gz.px * 2));
        for (int a = 0; a < 3; a++) s[a] = g.s0[a] * f;
        snprintf(what, sizeof(what), "x%.3f", f);
    }
    std::string note = AnimatedNote(n);
    if (g.key.anim >= 0) {
        // The key at the animation's time, added at the first move when there was none
        int path = g.mode == kMove ? 0 : g.mode == kTurn ? 1 : 2, nv = path == 1 ? 4 : 3;
        const float *val = path == 0 ? t : path == 1 ? r : s;
        Channel *c = ChannelOf(g.key.anim, g.key.sampler);
        if (c && (g.key.key < 0 || memcmp(KeyValue(*c, g.key.key), val, nv * sizeof(float)))) {
            RecordKeys(g.key.anim, g.key.sampler);
            if (g.key.key < 0) g.key.key = AddKey(*c, g.key.time), g.key.added = true;
            memcpy(KeyValue(*c, g.key.key), val, nv * sizeof(float));
            g_key = {g.key.anim, g.key.sampler, g.key.key};
            SendKeys(g.key.anim, g.key.sampler);
        }
        char buf[160];
        snprintf(buf, sizeof(buf), "\nIts key at %.2f s of %s%s", g.key.time, AnimName(g.key.anim).c_str(), g.key.added ? " (added)" : "");
        note = buf;
    } else if (memcmp(t, n.t, sizeof(t)) || memcmp(r, n.r, sizeof(r)) || memcmp(s, n.s, sizeof(s))) {
        memcpy(n.t, t, sizeof(t)), memcpy(n.r, r, sizeof(r)), memcpy(n.s, s, sizeof(s));
        send(g.node);
    }
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    ImGui::SetTooltip("%s: %s\n%sright button: back where it was%s", NodeName(g.node).c_str(), what,
                      g.mode == kMove && !axis ? "Shift: nearer or farther; " : g.mode == kTurn ? "Ctrl: by 15 degrees; " : "",
                      note.c_str());
    return true;
}

// The node's handles on a picture, and a press on them or on the node starting a drag. node: the
// one selected (-1 none); hover: the one under the mouse. True when the mouse is theirs.
bool NodeHandles(NodeDrag &drag, ModelView &v, ImDrawList *draw, const Picture &pic, int node, int hover, const GltfMat &parentWorld,
                 const GltfMat &world, bool hovered, bool pressed, bool keys, const std::function<void(int)> &send) {
    if (drag.node >= 0) {
        if (StepDrag(drag, v, send)) {
            DrawGizmo(draw, MakeGizmo(pic, parentWorld, world, drag.mode), drag.mode, drag.part);
            return true;
        }
    }
    if (node < 0 || node >= (int)v.nodes.size()) return false;
    Gizmo gz = MakeGizmo(pic, parentWorld, world, g_dragMode);
    int hot = hovered ? GizmoHit(gz, g_dragMode, ImGui::GetMousePos()) : -1;
    DrawGizmo(draw, gz, g_dragMode, hot);
    if (hot < 0 && hovered && hover == node) hot = kBody;
    if (hot < 0) return false;
    if (pressed) {
        BeginDrag(drag, node, v, pic, parentWorld, world, hot, keys);
        return true;
    }
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    KeyEdit probe;
    std::string note = AnimatedNote(v.nodes[node]);
    if (keys && DragKey(node, g_dragMode, probe)) {
        char buf[160];
        snprintf(buf, sizeof(buf), "\nIt changes its key at %.2f s of %s%s", probe.time, AnimName(probe.anim).c_str(),
                 probe.key < 0 ? " (one added there)" : "");
        note = buf;
    }
    ImGui::SetTooltip("%s\n%s%s", NodeLabel(node).c_str(), PartHint(g_dragMode, hot), note.c_str());
    return true;
}

// Move, Turn, Scale: what a drag does
float DragModeWidth() {
    const ImGuiStyle &st = ImGui::GetStyle();
    return ImGui::CalcTextSize("MoveTurnScale").x + st.FramePadding.x * 6 + st.ItemSpacing.x * 2;
}

void DragModeButtons() {
    const char *names[] = {"Move", "Turn", "Scale"};
    const char *tips[] = {"A drag moves the node selected: along the screen, or along an axis by its arrow",
                          "A drag turns the node selected: about an axis by its circle, or as the screen turns",
                          "A drag scales the node selected: along one of its axes by its handle, or all three"};
    for (int k = 0; k < 3; k++) {
        if (k) ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x);
        bool on = g_dragMode == k;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(names[k])) g_dragMode = k;
        if (on) ImGui::PopStyleColor();
        ImGui::SetItemTooltip("%s", tips[k]);
    }
}

void SendProps(int i) {
    const auto &n = View().nodes[i];
    Record(eProps, i);
    if (!g_isLive) {
        GltfNode &d = g_disk.doc.nodes[i];
        d.trans = n.trans, d.hasLayer = n.hasLayer, d.layer = n.layer;
        d.shadow = n.shadow, d.ztest = n.ztest, d.zwrite = n.zwrite, d.outline = n.outline;
        g_disk.doc.props.insert(i);
        return;
    }
    char lua[200];
    snprintf(lua, sizeof(lua), "debugModelNodeProps(%d, %d, %s, %s, %s, %s, %.9g)", i, n.trans,
             n.hasLayer ? std::to_string(n.layer).c_str() : "nil", n.shadow ? "true" : "false",
             n.ztest ? "true" : "false", n.zwrite ? "true" : "false", n.outline);
    SendRaw(lua);
    g_modelAsked = ImGui::GetTime();
}

void SendMaterial(int i) {
    const auto &m = View().materials[i];
    Record(eMaterial, i);
    if (!g_isLive) {
        GltfMaterial &d = g_disk.doc.materials[i];
        memcpy(d.color, m.color, sizeof(d.color)), memcpy(d.emission, m.emission, sizeof(d.emission));
        d.metallic = m.metallic, d.roughness = m.roughness, d.ao = m.ao, d.alpha = m.alpha, d.cutoff = m.cutoff;
        d.doubleSided = m.doubleSided, d.unlit = m.unlit;
        g_disk.doc.mats.insert(i);
        return;
    }
    char lua[400];
    snprintf(lua, sizeof(lua), "debugModelMaterial(%d, %.9g, %.9g, %.9g, %.9g, %.9g, %.9g, %.9g, %.9g, %.9g, %.9g, %d, %.9g, %s, %s)", i,
             m.color[0], m.color[1], m.color[2], m.color[3], m.emission[0], m.emission[1], m.emission[2], m.metallic,
             m.roughness, m.ao, m.alpha, m.cutoff, m.doubleSided ? "true" : "false", m.unlit ? "true" : "false");
    SendRaw(lua);
    g_modelAsked = ImGui::GetTime();
}

void SendLight(int i) {
    const auto &l = View().lights[i];
    Record(eLight, i);
    if (!g_isLive) {
        GltfLight &d = g_disk.doc.lights[i];
        memcpy(d.rgb, l.rgb, sizeof(d.rgb));
        d.intensity = l.intensity, d.range = l.range, d.inner = l.inner, d.outer = l.outer;
        g_disk.doc.lightsChanged.insert(i);
        return;
    }
    char lua[220];
    snprintf(lua, sizeof(lua), "debugModelLight(%d, %.9g, %.9g, %.9g, %.9g, %.9g, %.9g, %.9g)", i, l.rgb[0], l.rgb[1], l.rgb[2],
             l.intensity, l.range, l.inner, l.outer);
    SendRaw(lua);
    g_modelAsked = ImGui::GetTime();
}

// A sampler's keys (changed in one of its channels) given to the other channels sharing it, and
// sent: into the model read (what Save writes), or to the game
void SendKeys(int a, int sampler) {
    std::vector<Channel> *chs = KeysOf(a);
    Channel *c = ChannelOf(a, sampler);
    if (!chs || !c) return;
    for (Channel &o : *chs)
        if (&o != c && o.sampler == sampler) o.in = c->in, o.out = c->out, o.interp = c->interp;
    if (g_view != &g_live) {
        float duration = 0;
        for (const Channel &o : *chs)
            if (!o.in.empty()) duration = std::max(duration, o.in.back());
        g_disk.doc.anims[a].duration = duration;
        if (a < (int)View().anims.size()) View().anims[a].duration = duration;
        g_disk.doc.keys.insert({a, sampler});
        return;
    }
    std::string lua = "local ok, err = debugModelAnimSampler(" + std::to_string(a) + ", " + std::to_string(sampler) + ", " +
                      std::to_string(c->interp) + ", {";
    char num[32];
    for (size_t k = 0; k < c->in.size(); k++) snprintf(num, sizeof(num), k ? ",%.9g" : "%.9g", c->in[k]), lua += num;
    lua += "}, {";
    for (size_t k = 0; k < c->out.size(); k++) snprintf(num, sizeof(num), k ? ",%.9g" : "%.9g", c->out[k]), lua += num;
    lua += "}) if not ok then error(err, 0) end";
    Exec(lua);
    g_modelAsked = ImGui::GetTime();
}

// A change's values put back into the model shown, and sent
void Put(const Change &c, const Values &x) {
    ModelView &v = View();
    Values now;
    if (!GetValues(v.nodes, v.materials, v.lights, c.kind, c.i, c.j, now)) return;
    int i = c.i;
    if (c.kind == eTRS) {
        ModelView::Node &n = v.nodes[i];
        memcpy(n.t, x.node.t, sizeof(n.t)), memcpy(n.r, x.node.r, sizeof(n.r)), memcpy(n.s, x.node.s, sizeof(n.s));
        SendTRS(i);
    } else if (c.kind == eProps) {
        ModelView::Node &n = v.nodes[i];
        n.trans = x.node.trans, n.hasLayer = x.node.hasLayer, n.layer = x.node.layer;
        n.shadow = x.node.shadow, n.ztest = x.node.ztest, n.zwrite = x.node.zwrite, n.outline = x.node.outline;
        SendProps(i);
    } else if (c.kind == eMaterial) {
        std::string name = v.materials[i].name;
        int tex = v.materials[i].tex;
        bool animated = v.materials[i].animated;
        v.materials[i] = x.material;
        v.materials[i].name = name, v.materials[i].tex = tex, v.materials[i].animated = animated;
        SendMaterial(i);
    } else if (c.kind == eKeys) {
        Channel *ch = ChannelOf(c.i, c.j);
        ch->in = x.keys.in, ch->out = x.keys.out, ch->interp = x.keys.interp;
        if (g_key.anim == c.i && g_key.sampler == c.j && g_key.key >= (int)ch->in.size()) g_key.key = (int)ch->in.size() - 1;
        SendKeys(c.i, c.j);
    } else {
        ModelView::Light &l = v.lights[i];
        memcpy(l.rgb, x.light.rgb, sizeof(l.rgb));
        l.intensity = x.light.intensity, l.range = x.light.range, l.inner = x.light.inner, l.outer = x.light.outer;
        SendLight(i);
    }
}

std::string AnimName(int i) {
    if (i < 0 || i >= (int)View().anims.size()) return "?";
    const auto &a = View().anims[i];
    return a.name.empty() ? "animation " + std::to_string(i) : a.name;
}

std::string StepLabel(const std::vector<Change> &step) {
    if (step.empty()) return "";
    const Change &c = step[0];
    std::string what = c.kind == eTRS      ? "the place of " + NodeName(c.i)
                       : c.kind == eProps    ? "the drawing of " + NodeName(c.i)
                       : c.kind == eMaterial ? "material " + MaterialName(c.i)
                       : c.kind == eKeys     ? "keys of " + AnimName(c.i)
                                             : "light " + LightName(c.i);
    if (step.size() > 1) what += " and " + std::to_string(step.size() - 1) + " more";
    return what;
}

// The last step of the model shown undone (or the last one undone done again)
void UndoStep(bool redo) {
    History &h = HistoryOf(g_view);
    auto &from = redo ? h.redo : h.undo;
    auto &to = redo ? h.undo : h.redo;
    if (from.empty() || !h.open.empty()) return;
    std::vector<Change> step = std::move(from.back());
    from.pop_back();
    g_replaying = true;
    for (size_t k = 0; k < step.size(); k++) {
        const Change &c = redo ? step[k] : step[step.size() - 1 - k];
        Put(c, redo ? c.after : c.before);
    }
    g_replaying = false;
    // What it changed, selected
    const Change &c = step[0];
    if (c.kind == eTRS || c.kind == eProps) SelectNode(c.i, true);
    else if (c.kind == eKeys) g_keysAnim = c.i, g_tab = 2;
    else Select(c.kind == eMaterial ? kMaterial : kLight, c.i), g_tab = c.kind == eMaterial ? 1 : 3;
    ModelView &v = View();
    h.nodes = v.nodes, h.materials = v.materials, h.lights = v.lights;
    to.push_back(std::move(step));
}

// ---------------------------------------------------------------------------
// The lists: nodes (by scene), materials, animations, lights
// ---------------------------------------------------------------------------

void NodeTree(int i, const std::string &filter, const std::vector<char> &onPath, int depth = 0) {
    ModelView::Node &n = View().nodes[i];
    if (depth > 256) return;
    ImGui::PushID(i);
    bool visible = n.visible;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 0));
    if (ImGui::Checkbox("##v", &visible)) SetVisible(i, visible, ImGui::GetIO().KeyShift);
    ImGui::PopStyleVar();
    ImGui::SetItemTooltip(n.mesh < 0 ? "Shown or hidden until the round starts again (no mesh of its own; Shift: its children too)"
                                     : "Shown or hidden until the round starts again (Shift: its children too)");
    ImGui::SameLine();
    bool sel = g_selKind == kNode && g_sel == i;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick | (n.children.empty() ? ImGuiTreeNodeFlags_Leaf : 0) |
                               (sel ? ImGuiTreeNodeFlags_Selected : 0);
    if (!filter.empty()) flags |= ImGuiTreeNodeFlags_DefaultOpen;
    if (g_reveal && i < (int)onPath.size() && onPath[i]) ImGui::SetNextItemOpen(true);
    if (!n.visible) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    bool open = ImGui::TreeNodeEx("##n", flags, "%s", NodeLabel(i).c_str());
    if (!n.visible) ImGui::PopStyleColor();
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) SelectNode(i, false);
    if (sel && g_reveal) {
        ImGui::SetScrollHereY(0.4f);
        g_reveal = false;
    }
    if (open) {
        for (int c : n.children)
            if (c >= 0 && c < (int)View().nodes.size() && (filter.empty() || Matches(c, filter))) NodeTree(c, filter, onPath, depth + 1);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void NodesTab() {
    ModelView &m = View();
    float em = ImGui::GetFontSize();
    static std::string filter;
    ImGui::SetNextItemWidth(std::min(em * 12, ImGui::GetContentRegionAvail().x));
    ImGui::InputTextWithHint("##filter", "filter", &filter);
    std::string low = filter;
    std::transform(low.begin(), low.end(), low.begin(), ::tolower);
    // The nodes above the one to reveal
    std::vector<char> onPath(m.nodes.size(), 0);
    if (g_reveal && g_selKind == kNode)
        for (int p = g_sel >= 0 && g_sel < (int)m.nodes.size() ? m.nodes[g_sel].parent : -1, guard = 0;
             p >= 0 && p < (int)m.nodes.size() && guard < 256; p = m.nodes[p].parent, guard++)
            onPath[p] = 1;
    if (ImGui::BeginChild("tree", ImVec2(0, 0), 0, ImGuiWindowFlags_HorizontalScrollbar)) {
        std::vector<char> inScene(m.nodes.size(), 0);
        for (size_t s = 0; s < m.scenes.size(); s++) {
            const auto &sc = m.scenes[s];
            if (m.scenes.size() > 1 || !sc.name.empty()) {
                std::string title = "Scene " + std::to_string(s) + (sc.name.empty() ? "" : ": " + sc.name) +
                                    (s == 1 ? "  (in front of the players)" : "");
                ImGui::SeparatorText(title.c_str());
            }
            for (int r : sc.roots)
                if (r >= 0 && r < (int)m.nodes.size()) {
                    inScene[r] = 1;
                    if (low.empty() || Matches(r, low)) NodeTree(r, low, onPath);
                }
        }
        // Roots in no scene: never drawn
        bool header = false;
        for (int i = 0; i < (int)m.nodes.size(); i++) {
            if (m.nodes[i].parent >= 0 || inScene[i] || !(low.empty() || Matches(i, low))) continue;
            if (!header) ImGui::SeparatorText("In no scene (not drawn)");
            header = true;
            NodeTree(i, low, onPath);
        }
        if (g_reveal && g_selKind == kNode) g_reveal = false; // filtered out
    }
    ImGui::EndChild();
}

// The nodes whose mesh uses a material
std::vector<int> NodesWithMaterial(int mat) {
    std::vector<int> out;
    for (int i = 0; i < (int)View().nodes.size(); i++) {
        int me = View().nodes[i].mesh;
        if (me < 0 || me >= (int)View().meshes.size()) continue;
        for (const auto &p : View().meshes[me].prims)
            if (p.material == mat) {
                out.push_back(i);
                break;
            }
    }
    return out;
}

void MaterialsTab() {
    ModelView &m = View();
    if (m.materials.empty()) ImGui::TextDisabled("None.");
    if (!ImGui::BeginChild("materials")) {
        ImGui::EndChild();
        return;
    }
    float sw = ImGui::GetTextLineHeight();
    for (int i = 0; i < (int)m.materials.size(); i++) {
        const auto &mt = m.materials[i];
        ImGui::PushID(i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(sw, sw));
        ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + sw, p.y + sw),
                                                  ImGui::ColorConvertFloat4ToU32(ImVec4(mt.color[0], mt.color[1], mt.color[2], 1)), 3);
        ImGui::SameLine();
        if (ImGui::Selectable(MaterialName(i).c_str(), g_selKind == kMaterial && g_sel == i)) Select(kMaterial, i);
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void Row(const char *label, float labelWidth);

const char *const kPaths[] = {"position", "rotation", "scale"};
const char *const kInterps[] = {"linear", "step", "cubic spline"};

// The key at a time (within a quarter of a tick), -1 none
int KeyAt(const Channel &c, float t) {
    for (size_t k = 0; k < c.in.size(); k++)
        if (std::fabs(c.in[k] - t) < 0.25f / 60) return (int)k;
    return -1;
}

// A key added at a time, its value what the channel has there (a cubic spline's tangents flat);
// the index it got
int AddKey(Channel &c, float t) {
    int n = c.path == 1 ? 4 : 3, per = PerKey(c);
    float v[4] = {0, 0, 0, 1};
    Gltf_Sample(c, t, v);
    size_t k = std::upper_bound(c.in.begin(), c.in.end(), t) - c.in.begin();
    c.in.insert(c.in.begin() + k, t);
    std::vector<float> key(per, 0.0f);
    memcpy(&key[c.interp == 2 ? n : 0], v, n * sizeof(float));
    c.out.insert(c.out.begin() + k * per, key.begin(), key.end());
    return (int)k;
}

void DeleteKey(Channel &c, int k) {
    size_t per = PerKey(c);
    c.in.erase(c.in.begin() + k);
    c.out.erase(c.out.begin() + k * per, c.out.begin() + (k + 1) * per);
}

// Another way of interpolating: a cubic spline's tangents dropped, or flat ones added
void SetInterp(Channel &c, int interp) {
    int n = c.path == 1 ? 4 : 3;
    if ((c.interp == 2) != (interp == 2)) {
        std::vector<float> out;
        for (size_t k = 0; k < c.in.size(); k++) {
            const float *v = KeyValue(c, (int)k);
            if (interp == 2) out.insert(out.end(), n, 0.0f);
            out.insert(out.end(), v, v + n);
            if (interp == 2) out.insert(out.end(), n, 0.0f);
        }
        c.out = std::move(out);
    }
    c.interp = interp;
}

// The animation put at a key's time, the preview paused there
void GoToKey(int a, const Channel &c, int k) {
    g_key = {a, c.sampler, k};
    SetAnimTime(a, c.in[k]);
    if (!g_isLive) g_disk.playing = false;
}

// The key selected: its time, value, the channel's interpolation; the ones before and after it
void KeyInspector(int a, float lw) {
    if (g_key.anim != a) return;
    Channel *c = ChannelOf(a, g_key.sampler);
    if (!c || g_key.key < 0 || g_key.key >= (int)c->in.size()) return;
    int k = g_key.key, n = c->path == 1 ? 4 : 3;
    ImGui::SeparatorText(("Key " + std::to_string(k + 1) + " of " + std::to_string(c->in.size()) + ": " + NodeName(c->node) + " " +
                          kPaths[std::clamp(c->path, 0, 2)])
                             .c_str());
    ImGui::BeginDisabled(k == 0);
    if (IconButton(ICON_LEFT "##prev")) GoToKey(a, *c, k - 1);
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("The key before");
    ImGui::SameLine();
    ImGui::BeginDisabled(k + 1 >= (int)c->in.size());
    if (IconButton(ICON_RIGHT "##next")) GoToKey(a, *c, k + 1);
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("The key after");
    ImGui::SameLine();
    if (ImGui::Button("Go to it")) GoToKey(a, *c, k);
    ImGui::SetItemTooltip("The animation put at its time");
    ImGui::SameLine();
    ImGui::BeginDisabled(c->in.size() < 2);
    bool del = ImGui::Button(ICON_TRASH " Delete");
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("A channel keeps one key at least");
    if (del) {
        RecordKeys(a, c->sampler);
        DeleteKey(*c, k);
        g_key.key = std::min(k, (int)c->in.size() - 1);
        SendKeys(a, c->sampler);
        return;
    }

    float lo = k > 0 ? c->in[k - 1] + 0.001f : 0, hi = k + 1 < (int)c->in.size() ? c->in[k + 1] - 0.001f : FLT_MAX, t = c->in[k];
    Row("time", lw);
    if (ImGui::DragFloat("##kt", &t, 0.002f, lo, hi, "%.3f s", ImGuiSliderFlags_AlwaysClamp) && t != c->in[k]) {
        RecordKeys(a, c->sampler);
        c->in[k] = std::clamp(t, lo, hi);
        SendKeys(a, c->sampler);
        SetAnimTime(a, c->in[k]);
    }
    ImGui::SetItemTooltip("Between the keys before and after it");
    float *v = KeyValue(*c, k);
    if (c->path == 1) {
        // As angles, kept while they are dragged (they would jump through the quaternion)
        static float euler[3], eulerQ[4];
        static KeySel eulerKey;
        bool same = eulerKey.anim == a && eulerKey.sampler == c->sampler && eulerKey.key == k;
        for (int q = 0; q < 4; q++) same = same && std::fabs(eulerQ[q] - v[q]) < 1e-5f;
        if (!same) QuatToEuler(v, euler), memcpy(eulerQ, v, sizeof(eulerQ)), eulerKey = g_key;
        Row("rotation", lw);
        if (ImGui::DragFloat3("##kr", euler, 0.5f, -360, 360, "%.1f deg")) {
            float q[4];
            EulerToQuat(euler, q);
            RecordKeys(a, c->sampler);
            memcpy(KeyValue(*c, k), q, sizeof(q)), memcpy(eulerQ, q, sizeof(q));
            SendKeys(a, c->sampler);
        }
        ImGui::SetItemTooltip("Degrees about X, then Y, then Z (Y is up)");
    } else {
        float tmp[3] = {v[0], v[1], v[2]};
        Row(kPaths[c->path], lw);
        if (ImGui::DragFloat3("##kv", tmp, c->path == 0 ? 0.01f : 0.005f, -FLT_MAX, FLT_MAX, "%.3f")) {
            RecordKeys(a, c->sampler);
            memcpy(KeyValue(*c, k), tmp, sizeof(tmp));
            SendKeys(a, c->sampler);
        }
    }
    (void)n;
    int interp = c->interp;
    Row("between keys", lw);
    if (ImGui::Combo("##interp", &interp, kInterps, 3) && interp != c->interp) {
        RecordKeys(a, c->sampler);
        SetInterp(*c, interp);
        SendKeys(a, c->sampler);
    }
    ImGui::SetItemTooltip("How the channel goes from one key to the next (all of its keys);\n"
                          "a cubic spline made from the others gets flat tangents.");
}

// An animation's keys: a line for each channel moving a node, its keys on it and the animation's
// time; a key clicked puts the animation there, dragged moves it in time
void KeysPanel(int a, float lw) {
    ModelView &m = View();
    std::vector<Channel> *chs = KeysOf(a);
    if (!chs) {
        ImGui::TextDisabled("Asking the game for its keys...");
        return;
    }
    if (chs->empty()) {
        ImGui::TextDisabled("None of its channels moves a node.");
        return;
    }
    ModelView::Anim &an = m.anims[a];
    float em = ImGui::GetFontSize(), dur = std::max(an.duration, 1e-3f);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    int selNode = g_selKind == kNode ? g_sel : -1;
    // A key's time dragged: its channel's sampler, the key, where it was grabbed
    static int dragSampler = -1, dragKey = -1;
    static float grab;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) dragSampler = dragKey = -1;
    // Keys for the node selected at the animation's time
    bool any = false;
    for (const Channel &c : *chs) any = any || c.node == selNode;
    if (any) {
        char label[96];
        snprintf(label, sizeof(label), "Key %s at %.2f s", NodeName(selNode).c_str(), an.time);
        if (ImGui::Button(label))
            for (Channel &c : *chs)
                if (c.node == selNode && KeyAt(c, an.time) < 0) {
                    RecordKeys(a, c.sampler);
                    g_key = {a, c.sampler, AddKey(c, an.time)};
                    SendKeys(a, c.sampler);
                }
        ImGui::SetItemTooltip("A key on each of its channels where the animation is, with the value it has there\n"
                              "(the handles on the game or the preview then change it).");
    }
    for (int ci = 0; ci < (int)chs->size(); ci++) {
        Channel &c = (*chs)[ci];
        ImGui::PushID(ci);
        std::string label = NodeName(c.node) + " " + kPaths[std::clamp(c.path, 0, 2)];
        bool mine = c.node == selNode;
        if (mine) ImGui::PushStyleColor(ImGuiCol_Text, Ink(kYellow));
        if (ImGui::Selectable(label.c_str(), false, 0, ImVec2(ImGui::CalcTextSize(label.c_str()).x, 0))) SelectNode(c.node, false);
        if (mine) ImGui::PopStyleColor();
        ImGui::SetItemTooltip("Click: select the node");
        ImGui::SameLine();
        ImGui::TextDisabled("%zu keys, %s", c.in.size(), kInterps[std::clamp(c.interp, 0, 2)]);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        float w = std::max(em * 4, ImGui::GetContentRegionAvail().x), h = ImGui::GetTextLineHeight();
        ImGui::InvisibleButton("strip", ImVec2(w, h), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
        dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + h), ImGui::GetColorU32(ImGuiCol_FrameBg), 3);
        float pad = h * 0.4f;
        auto xOf = [&](float t) { return p0.x + pad + (w - 2 * pad) * t / dur; };
        auto tOf = [&](float x) { return std::clamp((x - p0.x - pad) / (w - 2 * pad) * dur, 0.0f, dur); };
        float mid = p0.y + h / 2, r = h * 0.32f;
        bool dense = c.in.size() * 6 > w;
        ImVec2 mp = ImGui::GetMousePos();
        int hot = -1;
        float best = std::max(4.0f, em * 0.35f);
        ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text), yellow = IM_COL32(255, 210, 50, 255);
        for (int k = 0; k < (int)c.in.size(); k++) {
            float x = xOf(c.in[k]);
            if (hov && std::fabs(mp.x - x) < best) best = std::fabs(mp.x - x), hot = k;
            if (dense) dl->AddLine(ImVec2(x, mid - r), ImVec2(x, mid + r), ink & 0x80FFFFFF);
            else dl->AddQuadFilled(ImVec2(x, mid - r), ImVec2(x + r, mid), ImVec2(x, mid + r), ImVec2(x - r, mid), ink);
        }
        if (g_key.anim == a && g_key.sampler == c.sampler && g_key.key >= 0 && g_key.key < (int)c.in.size()) {
            float x = xOf(c.in[g_key.key]), rr = r * 1.3f;
            dl->AddQuadFilled(ImVec2(x, mid - rr), ImVec2(x + rr, mid), ImVec2(x, mid + rr), ImVec2(x - rr, mid), yellow);
        }
        if (hot >= 0 && !dense) {
            float x = xOf(c.in[hot]), rr = r * 1.3f;
            dl->AddQuad(ImVec2(x, mid - rr), ImVec2(x + rr, mid), ImVec2(x, mid + rr), ImVec2(x - rr, mid), yellow);
        }
        float tx = xOf(std::min(an.time, dur));
        dl->AddLine(ImVec2(tx, p0.y), ImVec2(tx, p0.y + h), IM_COL32(255, 80, 80, 255), std::max(1.0f, em / 10));
        // A press on a key selects it (and can drag its time); elsewhere it puts the animation there
        if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (hot >= 0) {
                GoToKey(a, c, hot);
                dragSampler = c.sampler, dragKey = hot, grab = mp.x - xOf(c.in[hot]);
            } else {
                SetAnimTime(a, tOf(mp.x));
                if (!g_isLive) g_disk.playing = false;
            }
        }
        if (act && dragSampler == c.sampler && dragKey >= 0 && dragKey < (int)c.in.size()) {
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2)) {
                float lo = dragKey > 0 ? c.in[dragKey - 1] + 0.001f : 0;
                float hi = dragKey + 1 < (int)c.in.size() ? c.in[dragKey + 1] - 0.001f : dur;
                float t = tOf(mp.x - grab);
                if (ImGui::GetIO().KeyCtrl) t = std::round(t * 60) / 60;
                t = std::clamp(t, lo, std::max(lo, hi));
                if (t != c.in[dragKey]) {
                    RecordKeys(a, c.sampler);
                    c.in[dragKey] = t;
                    SendKeys(a, c.sampler);
                    SetAnimTime(a, t);
                }
                ImGui::SetTooltip("%.3f s", t);
            }
        } else if (act && ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::GetIO().MouseDelta.x != 0) {
            SetAnimTime(a, tOf(mp.x));
        }
        if (hov && hot < 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            RecordKeys(a, c.sampler);
            g_key = {a, c.sampler, AddKey(c, tOf(mp.x))};
            SendKeys(a, c.sampler);
        }
        if (hov && hot >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            g_key = {a, c.sampler, hot};
            ImGui::OpenPopup("key");
        }
        if (ImGui::BeginPopup("key")) {
            if (ImGui::MenuItem("Delete the key", nullptr, false, c.in.size() > 1 && g_key.key < (int)c.in.size())) {
                RecordKeys(a, c.sampler);
                DeleteKey(c, g_key.key);
                g_key.key = std::min(g_key.key, (int)c.in.size() - 1);
                SendKeys(a, c.sampler);
            }
            ImGui::EndPopup();
        }
        if (hov && dragKey < 0 && !ImGui::IsPopupOpen("key")) {
            if (hot >= 0)
                ImGui::SetTooltip("Key %d of %zu at %.3f s\nClick: go to it; drag: move it in time (Ctrl: by ticks);\n"
                                  "right button: delete it",
                                  hot + 1, c.in.size(), c.in[hot]);
            else
                ImGui::SetTooltip("%.3f s\nClick: put the animation there; double click: a key there", tOf(mp.x));
        }
        ImGui::PopID();
    }
    KeyInspector(a, lw);
}

void AnimationsTab() {
    g_animsTab = true;
    ModelView &m = View();
    float em = ImGui::GetFontSize();
    if (m.anims.empty()) {
        ImGui::TextDisabled("None.");
        return;
    }
    if (!ImGui::BeginChild("anims")) {
        ImGui::EndChild();
        return;
    }
    // Each one: on or off, its name and loops, then where it is (dragged to put it there); its
    // name clicked shows its keys
    for (int i = 0; i < (int)m.anims.size(); i++) {
        ModelView::Anim &a = m.anims[i];
        ImGui::PushID(i);
        if (i > 0) ImGui::Spacing();
        bool on = a.enabled;
        if (ImGui::Checkbox("##on", &on)) SetAnimOn(i, on);
        ImGui::SetItemTooltip("On: it starts again from its start. Off: what it moves goes back to rest.");
        ImGui::SameLine();
        std::string name = AnimName(i);
        bool shown = g_keysAnim == i;
        if (ImGui::Selectable(name.c_str(), shown, 0, ImVec2(ImGui::CalcTextSize(name.c_str()).x, 0)))
            g_keysAnim = shown ? -1 : i;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%d channels%s\nClick: %s its keys (beside or below the list)", a.channels,
                              a.id ? (", id " + std::to_string(a.id)).c_str() : "", shown ? "hide" : "show");
        std::string loops = a.loopCount < 0 ? "loop " + std::to_string(a.loop + 1)
                                            : "loop " + std::to_string(std::min(a.loop + 1, a.loopCount)) + " of " + std::to_string(a.loopCount);
        float w = ImGui::CalcTextSize(loops.c_str()).x;
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x > w + em) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - w);
            ImGui::TextDisabled("%s", loops.c_str());
        } else {
            ImGui::NewLine();
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        float t = a.time;
        char fmt[48];
        snprintf(fmt, sizeof(fmt), "%%.2f / %.2f s", a.duration);
        if (ImGui::SliderFloat("##t", &t, 0, std::max(a.duration, 0.01f), fmt)) SetAnimTime(i, t);
        ImGui::SetItemTooltip(g_isLive ? "Drag to put the animation at a time (with the game paused it stays there)."
                                       : "Drag to put the animation at a time (paused, it stays there).");
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void LightsTab() {
    ModelView &m = View();
    if (m.lights.empty()) {
        ImGui::TextDisabled("None (KHR_lights_punctual).");
        return;
    }
    for (int i = 0; i < (int)m.lights.size(); i++) {
        ImGui::PushID(i);
        const auto &l = m.lights[i];
        std::string label = LightName(i) + "  (" + kLightTypes[std::clamp(l.type, 0, 2)] + ")";
        if (ImGui::Selectable(label.c_str(), g_selKind == kLight && g_sel == i)) Select(kLight, i);
        ImGui::PopID();
    }
}

// ---------------------------------------------------------------------------
// The inspector: the node, material or light selected
// ---------------------------------------------------------------------------

// A label left of the field, the field filling the rest of the line
void Row(const char *label, float labelWidth) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

void LightEditor(int i, float lw) {
    ModelView::Light &l = View().lights[i];
    ImGui::PushID("light");
    ImGui::PushID(i);
    ImGui::TextDisabled("%s light%s", kLightTypes[std::clamp(l.type, 0, 2)], l.animated ? "; an animation changes it (it wins)" : "");
    bool changed = false;
    Row("colour", lw);
    changed |= ImGui::ColorEdit3("##c", l.rgb, ImGuiColorEditFlags_Float);
    Row("intensity", lw);
    changed |= ImGui::DragFloat("##i", &l.intensity, 0.05f, 0, FLT_MAX, "%.2f");
    if (l.type != 0) {
        Row("range", lw);
        changed |= ImGui::DragFloat("##r", &l.range, 0.5f, -1, FLT_MAX, l.range <= 0 ? "none" : "%.1f");
    }
    if (l.type == 2) {
        float cone[2] = {l.inner * kDeg, l.outer * kDeg};
        Row("cone", lw);
        if (ImGui::DragFloat2("##cone", cone, 0.2f, 0, 90, "%.1f deg")) {
            cone[0] = std::min(cone[0], cone[1]);
            l.inner = cone[0] / kDeg, l.outer = cone[1] / kDeg;
            changed = true;
        }
        ImGui::SetItemTooltip("Inner and outer angles of the spot's cone");
    }
    if (changed) SendLight(i);
    ImGui::PopID();
    ImGui::PopID();
}

void NodeInspector(int i, float lw) {
    ModelView &m = View();
    ModelView::Node &n = m.nodes[i];
    float em = ImGui::GetFontSize();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", NodeName(i).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("node %d", i);
    if (n.parent >= 0) {
        ImGui::SameLine();
        if (ImGui::Button(("Parent: " + NodeName(n.parent)).c_str())) SelectNode(n.parent, true);
    }
    bool visible = n.visible;
    if (ImGui::Checkbox("Visible", &visible)) SetVisible(i, visible, false);
    ImGui::SetItemTooltip(g_isLive ? "Until the round starts again; not saved." : "In the preview; not saved.");
    if (!n.children.empty()) {
        ImGui::SameLine();
        if (ImGui::Button(n.visible ? "Hide with its children" : "Show with its children")) SetVisible(i, !n.visible, true);
    }

    ImGui::SeparatorText("Transform");
    if (n.animated)
        ImGui::TextColored(Ink(kYellow), "An animation moves it: these are its values at rest (animations off).");
    // The rotation as angles, kept while they are dragged (they would jump through the quaternion)
    static int eulerNode = -1;
    static float euler[3], eulerQ[4];
    bool sameQ = true;
    for (int k = 0; k < 4; k++) sameQ = sameQ && std::fabs(eulerQ[k] - n.r[k]) < 1e-5f;
    if (eulerNode != i || !sameQ) {
        QuatToEuler(n.r, euler);
        memcpy(eulerQ, n.r, sizeof(eulerQ));
        eulerNode = i;
    }
    float size = std::max(std::fabs(n.t[0]), std::max(std::fabs(n.t[1]), std::fabs(n.t[2])));
    bool trs = false;
    Row("position", lw);
    trs |= ImGui::DragFloat3("##t", n.t, std::max(0.005f, size * 0.004f), -FLT_MAX, FLT_MAX, "%.3f");
    Row("rotation", lw);
    if (ImGui::DragFloat3("##r", euler, 0.5f, -360, 360, "%.1f deg")) {
        EulerToQuat(euler, n.r);
        memcpy(eulerQ, n.r, sizeof(eulerQ));
        trs = true;
    }
    ImGui::SetItemTooltip("Degrees about X, then Y, then Z (Y is up)");
    static bool uniform = true;
    Row("scale", lw);
    float before[3] = {n.s[0], n.s[1], n.s[2]};
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::DragFloat3("##s", n.s, 0.005f, -FLT_MAX, FLT_MAX, "%.3f")) {
        if (uniform)
            for (int k = 0; k < 3; k++)
                if (n.s[k] != before[k] && before[k] != 0) {
                    float f = n.s[k] / before[k];
                    for (int o = 0; o < 3; o++) n.s[o] = o == k ? n.s[k] : before[o] * f;
                    break;
                }
        trs = true;
    }
    ImGui::SameLine();
    ImGui::Checkbox("##u", &uniform);
    ImGui::SetItemTooltip("The three together");
    if (trs) SendTRS(i);

    if (n.mesh >= 0 && n.mesh < (int)m.meshes.size()) {
        const auto &me = m.meshes[n.mesh];
        ImGui::SeparatorText("Mesh");
        int verts = 0, tris = 0;
        for (const auto &p : me.prims) {
            verts += p.vertices;
            tris += p.mode == 4 ? p.indices / 3 : std::max(0, p.indices - 2);
        }
        ImGui::Text("%s", me.name.empty() ? ("mesh " + std::to_string(n.mesh)).c_str() : me.name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%d vertices, %d triangles", verts, tris);
        for (size_t p = 0; p < me.prims.size(); p++) {
            ImGui::PushID((int)p);
            int mat = me.prims[p].material;
            ImGui::AlignTextToFramePadding();
            ImGui::Bullet();
            if (mat >= 0) {
                if (ImGui::Button(MaterialName(mat).c_str())) Select(kMaterial, mat);
                ImGui::SetItemTooltip("Edit the material");
            } else {
                ImGui::TextDisabled("no material (not drawn)");
            }
            ImGui::PopID();
        }

        ImGui::SeparatorText("Drawing (Ikemen's extras)");
        bool props = false;
        Row("blend", lw);
        props |= ImGui::Combo("##trans", &n.trans, kTrans, 4);
        Row("layer", lw);
        const char *layers[] = {"its scene's", "-1 (first)", "0 (behind the players)", "1 (in front of them)"};
        int layer = n.hasLayer ? std::clamp(n.layer, -1, 1) + 2 : 0;
        if (ImGui::Combo("##layer", &layer, layers, 4)) {
            n.hasLayer = layer > 0;
            n.layer = layer - 2;
            props = true;
        }
        ImGui::SetItemTooltip("Which of the stage's passes draws it (scene 0 draws in 0, scene 1 in 1)");
        props |= ImGui::Checkbox("Casts shadows", &n.shadow);
        ImGui::SameLine();
        props |= ImGui::Checkbox("Depth test", &n.ztest);
        ImGui::SameLine();
        props |= ImGui::Checkbox("Depth write", &n.zwrite);
        Row("outline", lw);
        props |= ImGui::DragFloat("##outline", &n.outline, 0.05f, 0, FLT_MAX, n.outline > 0 ? "%.2f" : "none");
        if (props) SendProps(i);
    }
    if (n.light >= 0 && n.light < (int)m.lights.size()) {
        ImGui::SeparatorText(("Light: " + LightName(n.light)).c_str());
        LightEditor(n.light, lw);
    }
    if (n.mesh < 0 && n.light < 0 && !n.children.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%zu children; moving it moves them.", n.children.size());
    }
    (void)em;
}

void MaterialInspector(int i, float lw) {
    ModelView &m = View();
    ModelView::Material &mt = m.materials[i];
    ImGui::Text("%s", MaterialName(i).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("material %d", i);
    if (mt.animated) ImGui::TextColored(Ink(kYellow), "An animation changes it: its own values win while it runs.");
    bool changed = false;
    ImGui::SeparatorText("Colour");
    Row("base colour", lw);
    changed |= ImGui::ColorEdit4("##base", mt.color, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
    ImGui::SetItemTooltip(mt.tex & 1 ? "Multiplies its texture" : "Its colour (no texture)");
    Row("emission", lw);
    changed |= ImGui::ColorEdit3("##em", mt.emission);
    ImGui::SetItemTooltip(mt.tex & 16 ? "Multiplies its emission texture" : "Light it gives off, lit or not");
    ImGui::SeparatorText("Surface");
    Row("metallic", lw);
    changed |= ImGui::SliderFloat("##metal", &mt.metallic, 0, 1, "%.2f");
    Row("roughness", lw);
    changed |= ImGui::SliderFloat("##rough", &mt.roughness, 0, 1, "%.2f");
    if (mt.tex & 8) {
        Row("occlusion", lw);
        changed |= ImGui::SliderFloat("##ao", &mt.ao, 0, 1, "%.2f");
        ImGui::SetItemTooltip("How much its occlusion texture darkens it");
    }
    changed |= ImGui::Checkbox("Unlit", &mt.unlit);
    ImGui::SetItemTooltip("Its colour as it is, whatever the lights (KHR_materials_unlit)");
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Both faces", &mt.doubleSided);
    ImGui::SeparatorText("Transparency");
    Row("alpha", lw);
    changed |= ImGui::Combo("##alpha", &mt.alpha, kAlpha, 3);
    if (mt.alpha == 1) {
        Row("cut at", lw);
        changed |= ImGui::SliderFloat("##cut", &mt.cutoff, 0, 1, "%.2f");
    }
    if (changed) SendMaterial(i);
    const char *texNames[] = {"base colour", "normal", "metallic-roughness", "occlusion", "emission"};
    std::string texts;
    for (int k = 0; k < 5; k++)
        if (mt.tex & (1 << k)) texts += (texts.empty() ? "" : ", ") + std::string(texNames[k]);
    ImGui::TextDisabled("Textures: %s", texts.empty() ? "none" : texts.c_str());
    std::vector<int> users = NodesWithMaterial(i);
    ImGui::SeparatorText(("Used by " + std::to_string(users.size()) + " nodes").c_str());
    for (size_t k = 0; k < users.size() && k < 200; k++) {
        ImGui::PushID((int)k);
        if (ImGui::Selectable(NodeName(users[k]).c_str())) SelectNode(users[k], true);
        ImGui::PopID();
    }
}

void LightInspector(int i, float lw) {
    ImGui::Text("%s", LightName(i).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("light %d", i);
    LightEditor(i, lw);
    for (int n = 0; n < (int)View().nodes.size(); n++)
        if (View().nodes[n].light == i) {
            ImGui::PushID(n);
            if (ImGui::Button(("Its node: " + NodeName(n)).c_str())) SelectNode(n, true);
            ImGui::PopID();
        }
}

// ---------------------------------------------------------------------------
// Where the stage's def places the model: [Model] offset and scale, [Camera] fov, near and far
// ---------------------------------------------------------------------------

// A field of the def set; its section added (before [BGdef]) when the def has none
bool SetDefField(SourceFile &sf, const char *section, const char *key, const std::string &value) {
    const IniFile &ini = StageIni(sf);
    for (const IniSection &s : ini.sections)
        if (s.lower == section) return Ini_SetField(sf.text, s, key, value);
    int line = 0;
    for (const IniSection &s : ini.sections)
        if (s.lower == "bgdef") line = s.line;
    std::string add = std::string("[") + (char)toupper(section[0]) + (section + 1) + "]\n" + key + " = " + value + "\n\n";
    size_t at = sf.text.size();
    if (line > 0) {
        at = 0;
        for (int l = 1; l < line && at != std::string::npos; l++) {
            at = sf.text.find('\n', at);
            if (at != std::string::npos) at++;
        }
        if (at == std::string::npos) at = sf.text.size();
    } else if (!sf.text.empty() && sf.text.back() != '\n') {
        add = "\n" + add;
    }
    sf.text.insert(at, add);
    return true;
}

// A field's numbers; out keeps its values without the field
void ReadFloats(const IniFile &ini, const char *section, const char *key, float *out, int count) {
    float v[4];
    for (const IniSection &s : ini.sections)
        if (s.lower == section && s.Floats(key, v, count)) std::copy(v, v + count, out);
}

// sf: the def placing the model (live: the stage in the match, whose game shows each change;
// from disk: its text changed only, the preview reading it)
void PlacementSection(SourceFile *sf, float lw) {
    if (!sf) return;
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if (!ImGui::CollapsingHeader("Placement (the stage's def)")) return;
    const IniFile &ini = StageIni(*sf);
    float offset[3] = {0, 0, 0}, scale[3] = {1, 1, 1}, fov = 40, nearFar[2] = {0.1f, 10000};
    ReadFloats(ini, "model", "offset", offset, 3);
    ReadFloats(ini, "model", "scale", scale, 3);
    ReadFloats(ini, "camera", "fov", &fov, 1);
    ReadFloats(ini, "camera", "near", &nearFar[0], 1);
    ReadFloats(ini, "camera", "far", &nearFar[1], 1);
    auto rounded = [](float *v, int count, float step) {
        for (int k = 0; k < count; k++) v[k] = std::round(v[k] / step) * step;
        return Ini_Floats(v, count);
    };
    bool changed = false;
    Row("offset", lw);
    if (ImGui::DragFloat3("##offset", offset, 0.005f, -FLT_MAX, FLT_MAX, "%.3f"))
        changed = SetDefField(*sf, "model", "offset", rounded(offset, 3, 0.0001f));
    ImGui::SetItemTooltip("[Model] offset: x, y, z of the model from the camera (z: how far in front of it)");
    static bool uniform = true;
    Row("scale", lw);
    float before[3] = {scale[0], scale[1], scale[2]};
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
    if (ImGui::DragFloat3("##scale", scale, 0.0005f, -FLT_MAX, FLT_MAX, "%.4f")) {
        if (uniform)
            for (int k = 0; k < 3; k++)
                if (scale[k] != before[k] && before[k] != 0) {
                    float f = scale[k] / before[k];
                    for (int o = 0; o < 3; o++) scale[o] = o == k ? scale[k] : before[o] * f;
                    break;
                }
        changed = SetDefField(*sf, "model", "scale", rounded(scale, 3, 0.00001f));
    }
    ImGui::SetItemTooltip("[Model] scale");
    ImGui::SameLine();
    ImGui::Checkbox("##u", &uniform);
    ImGui::SetItemTooltip("The three together");
    Row("fov", lw);
    if (ImGui::DragFloat("##fov", &fov, 0.1f, 1, 179, "%.1f deg"))
        changed = SetDefField(*sf, "camera", "fov", rounded(&fov, 1, 0.1f));
    ImGui::SetItemTooltip("[Camera] fov: the camera's field of view (40 when not given)");
    Row("near, far", lw);
    if (ImGui::DragFloat2("##nearfar", nearFar, 0.05f, 0.001f, FLT_MAX, "%.3f")) {
        nearFar[0] = std::max(0.001f, nearFar[0]);
        nearFar[1] = std::max(nearFar[0] + 0.001f, nearFar[1]);
        changed = SetDefField(*sf, "camera", "near", rounded(&nearFar[0], 1, 0.001f));
        changed = SetDefField(*sf, "camera", "far", rounded(&nearFar[1], 1, 0.001f)) && changed;
    }
    ImGui::SetItemTooltip("[Camera] near and far: what is nearer or farther is not drawn");
    for (const IniSection &s : ini.sections)
        if (s.lower == "model")
            if (const IniField *env = s.Field("environment")) {
                float intensity = 1;
                s.Floats("environmentintensity", &intensity, 1);
                Row("environment", lw);
                ImGui::TextDisabled("%s", env->value.c_str());
                Row("its intensity", lw);
                if (ImGui::DragFloat("##envi", &intensity, 0.01f, 0, FLT_MAX, "%.2f"))
                    changed = SetDefField(*sf, "model", "environmentintensity", rounded(&intensity, 1, 0.01f));
            }
    if (changed && g_isLive) StageEdited(*sf);
    else if (changed) sf->version++;
}

} // namespace

// ---------------------------------------------------------------------------
// From the game
// ---------------------------------------------------------------------------

void OnModel(const Json &j) {
    ModelView &m = g_live;
    m.known = true;
    m.has = !j["none"].truthy();
    std::string file = j["file"].str();
    if (file != m.file && !m.file.empty() && g_isLive) {
        g_selKind = kNone, g_sel = -1;
        SendRaw("debugModelSelect(-1)");
    }
    // Another model: its keys asked again
    if (file != m.file || j["anims"].items.size() != m.anims.size()) {
        g_liveKeys.clear();
        if (g_isLive) g_keysAnim = -1, g_key = KeySel();
    }
    m.file = file;
    m.dirty = j["dirty"].truthy();
    m.anims.clear();
    for (const auto &a : j["anims"].items) {
        ModelView::Anim v;
        v.name = a["name"].str();
        v.id = a["id"].i();
        v.enabled = a["enabled"].truthy();
        v.time = (float)a["time"].num();
        v.duration = (float)a["duration"].num();
        v.loop = a["loop"].i();
        v.loopCount = a["loopCount"].i(-1);
        v.channels = a["channels"].i();
        m.anims.push_back(v);
    }
    // While a value is dragged here, this window's values stay
    if (g_isLive && ImGui::IsAnyItemActive() && j["nodes"].items.size() == m.nodes.size()) return;
    m.scenes.clear();
    for (const auto &s : j["scenes"].items) {
        ModelView::Scene v;
        v.name = s["name"].str();
        for (const auto &r : s["nodes"].items) v.roots.push_back(r.i());
        m.scenes.push_back(v);
    }
    auto floats = [](const Json &a, float *out, int count) {
        for (int k = 0; k < count && k < (int)a.items.size(); k++) out[k] = (float)a[(size_t)k].num();
    };
    m.nodes.clear();
    for (const auto &n : j["nodes"].items) {
        ModelView::Node v;
        v.name = n["name"].str();
        v.visible = n["visible"].truthy();
        v.mesh = n["mesh"].i(-1);
        v.light = n["light"].i(-1);
        v.parent = n["parent"].i(-1);
        for (const auto &c : n["children"].items) v.children.push_back(c.i());
        floats(n["t"], v.t, 3);
        floats(n["r"], v.r, 4);
        floats(n["s"], v.s, 3);
        v.animated = n["animated"].truthy();
        v.trans = n["trans"].i();
        v.hasLayer = !n["layer"].isNull();
        v.layer = n["layer"].i();
        v.shadow = n["shadow"].truthy();
        v.ztest = n["ztest"].truthy();
        v.zwrite = n["zwrite"].truthy();
        v.outline = (float)n["outline"].num();
        m.nodes.push_back(v);
    }
    m.meshes.clear();
    for (const auto &me : j["meshes"].items) {
        ModelView::Mesh v;
        v.name = me["name"].str();
        for (const auto &p : me["prims"].items)
            v.prims.push_back({p["material"].i(-1), p["vertices"].i(), p["indices"].i(), p["mode"].i(4)});
        m.meshes.push_back(v);
    }
    m.materials.clear();
    for (const auto &mt : j["materials"].items) {
        ModelView::Material v;
        v.name = mt["name"].str();
        floats(mt["color"], v.color, 4);
        floats(mt["emission"], v.emission, 3);
        v.metallic = (float)mt["metallic"].num(1);
        v.roughness = (float)mt["roughness"].num(1);
        v.ao = (float)mt["ao"].num(1);
        v.alpha = mt["alpha"].i();
        v.cutoff = v.alpha == 1 ? (float)mt["cutoff"].num(0.5) : 0.5f;
        v.doubleSided = mt["doubleSided"].truthy();
        v.unlit = mt["unlit"].truthy();
        v.tex = mt["tex"].i();
        v.animated = mt["animated"].truthy();
        m.materials.push_back(v);
    }
    m.lights.clear();
    for (const auto &l : j["lights"].items) {
        ModelView::Light v;
        v.name = l["name"].str();
        v.type = l["type"].i();
        v.rgb[0] = (float)l["r"].num(1), v.rgb[1] = (float)l["g"].num(1), v.rgb[2] = (float)l["b"].num(1);
        v.intensity = (float)l["intensity"].num(1);
        v.range = (float)l["range"].num(-1);
        v.inner = (float)l["inner"].num(0);
        v.outer = (float)l["outer"].num(0.785);
        v.animated = l["animated"].truthy();
        m.lights.push_back(v);
    }
    if (!g_isLive) return;
    if (g_selKind == kNode && g_sel >= (int)m.nodes.size()) g_selKind = kNone;
    if (g_selKind == kMaterial && g_sel >= (int)m.materials.size()) g_selKind = kNone;
    if (g_selKind == kLight && g_sel >= (int)m.lights.size()) g_selKind = kNone;
}

void OnModelKeys(const Json &j) {
    int a = j["anim"].i(-1);
    if (g_liveKeys.size() != g_live.anims.size()) g_liveKeys.assign(g_live.anims.size(), LiveKeys());
    if (a < 0 || a >= (int)g_liveKeys.size()) return;
    LiveKeys &k = g_liveKeys[a];
    k.known = true;
    k.channels.clear();
    // Numbers apart by spaces, each read back as the float32 it was
    auto floats = [](const std::string &s) {
        std::vector<float> v;
        const char *p = s.c_str();
        for (char *end;; p = end) {
            float f = strtof(p, &end);
            if (end == p) break;
            v.push_back(f);
        }
        return v;
    };
    for (const Json &c : j["channels"].items) {
        Channel ch;
        ch.node = c["node"].i(-1), ch.path = c["path"].i(), ch.sampler = c["sampler"].i(-1), ch.interp = c["interp"].i();
        ch.in = floats(c["times"].str()), ch.out = floats(c["values"].str());
        if (ch.node < 0 || ch.in.empty() || ch.out.size() != ch.in.size() * PerKey(ch)) continue;
        k.channels.push_back(std::move(ch));
    }
}

void ReadModelGeometry(const Json &mdl) {
    ModelFrame &f = g_mframe;
    f.ok = !mdl.isNull();
    auto floats = [](const Json &a, std::vector<float> &out) {
        out.clear();
        for (const auto &v : a.items) out.push_back((float)v.num());
    };
    f.sel = mdl["sel"].i(-1);
    f.hover = mdl["hover"].i(-1);
    f.picked = mdl["picked"].i(-1);
    f.dirty = mdl["dirty"].truthy();
    floats(mdl["sb"], f.sb);
    floats(mdl["hb"], f.hb);
    floats(mdl["sp"], f.sp);
    floats(mdl["hp"], f.hp);
    f.w = (float)mdl["w"].num(), f.h = (float)mdl["h"].num();
    f.mats = mdl["vp"].items.size() == 16 && mdl["pw"].items.size() == 16 && mdl["nw"].items.size() == 16 && f.w > 0 && f.h > 0;
    for (int k = 0; f.mats && k < 16; k++) {
        f.viewProj[k] = (float)mdl["vp"].items[k].num(), f.parentWorld[k] = (float)mdl["pw"].items[k].num();
        f.world[k] = (float)mdl["nw"].items[k].num();
    }
    // A node picked on the game: selected here
    if (f.ok && f.picked != g_pickedSeen) {
        if (g_pickedSeen >= 0 && f.sel >= 0 && g_isLive) {
            g_selKind = kNode;
            g_sel = f.sel;
            g_reveal = true;
            g_tab = 0;
            ShowWindow("Model");
        }
        g_pickedSeen = f.picked;
    }
}

// ---------------------------------------------------------------------------
// On the Game window
// ---------------------------------------------------------------------------

bool ModelPicking() { return g_modelPick; }
void ModelPickOff() { g_modelPick = false; }

void ModelPickToggle() {
    static bool stage, screen;
    // The other pick modes, turned on, turn this one off
    if ((g_stagePick && !stage) || (g_screenPick && !screen)) g_modelPick = false;
    if (!g_mframe.ok && !g_modelPick) {
        stage = g_stagePick, screen = g_screenPick;
        return;
    }
    SameLineFits(CheckboxWidth("3D model"));
    if (ImGui::Checkbox("3D model", &g_modelPick) && g_modelPick) g_stagePick = g_screenPick = false;
    ImGui::SetItemTooltip("The mouse picks the stage model's nodes instead of the players (window Model);\n"
                          "the one selected, or its handles, dragged, moves, turns or scales it.");
    stage = g_stagePick, screen = g_screenPick;
    if (g_modelPick) {
        SameLineFits(DragModeWidth());
        DragModeButtons();
    }
}

bool ModelOverlay(ImDrawList *draw, bool hovered, bool clicked, const std::function<ImVec2(float, float)> &toScreen,
                  const std::function<ImVec2(ImVec2)> &toPicture) {
    const ModelFrame &f = g_mframe;
    if (!f.ok) return false;
    // The names are the match's model's, whatever the window shows
    struct Restore {
        ModelView *view = g_view;
        ~Restore() { g_view = view; }
    } restore;
    g_view = &g_live;
    float thick = std::max(1.0f, ImGui::GetFontSize() / 13);
    auto drawBox = [&](const std::vector<float> &seg, const std::vector<float> &pt, ImU32 col, float t, int node) {
        float minX = FLT_MAX, minY = FLT_MAX;
        for (size_t k = 0; k + 3 < seg.size(); k += 4) {
            ImVec2 a = toScreen(seg[k], seg[k + 1]), b = toScreen(seg[k + 2], seg[k + 3]);
            draw->AddLine(a, b, col, t);
            minX = std::min(minX, std::min(a.x, b.x)), minY = std::min(minY, std::min(a.y, b.y));
        }
        if (pt.size() >= 2) {
            ImVec2 p = toScreen(pt[0], pt[1]);
            float r = ImGui::GetFontSize() * 0.5f;
            draw->AddQuad(ImVec2(p.x, p.y - r), ImVec2(p.x + r, p.y), ImVec2(p.x, p.y + r), ImVec2(p.x - r, p.y), col, t);
            minX = p.x + r, minY = p.y - r;
        }
        if (minX < FLT_MAX && node >= 0 && node < (int)View().nodes.size())
            draw->AddText(ImVec2(minX + 4, minY + 2), col, NodeName(node).c_str());
    };
    if (g_modelPick && f.hover >= 0 && f.hover != f.sel) drawBox(f.hb, f.hp, IM_COL32(255, 255, 255, 200), thick, f.hover);
    if (f.sel >= 0) drawBox(f.sb, f.sp, IM_COL32(255, 220, 60, 255), thick * 2, f.sel);
    if (!g_modelPick) return false;

    HistoryTick(g_live);
    // The selected node's handles, or the node itself, pressed on and dragged (the matrices it was
    // seen through at the press)
    static NodeDrag drag;
    Picture pic;
    pic.viewProj = f.viewProj;
    pic.p0 = toScreen(0, 0);
    ImVec2 p1 = toScreen(f.w, f.h);
    pic.size = ImVec2(p1.x - pic.p0.x, p1.y - pic.p0.y);
    bool handles = f.mats && f.sel >= 0 && f.sel < (int)g_live.nodes.size() && pic.size.x > 0 && pic.size.y > 0;
    bool mine = (handles || drag.node >= 0) &&
                NodeHandles(drag, g_live, draw, pic, handles ? f.sel : -1, f.hover, f.parentWorld, f.world, hovered, clicked, g_isLive,
                            [](int i) { SendLiveTRS(i, g_live.nodes[i]); });
    if (drag.node >= 0) return true;

    // The node under the mouse, asked when it moves (the game casts the ray)
    static double asked;
    static ImVec2 askedAt(-1, -1);
    static bool wasHovered;
    ImVec2 m = ImGui::GetMousePos();
    if (hovered) {
        ImVec2 p = toPicture(m);
        char lua[128];
        if (clicked) {
            snprintf(lua, sizeof(lua), "debugModelPick(%g, %g, true)", p.x, p.y);
            SendRaw(lua);
            askedAt = m;
            asked = ImGui::GetTime();
        } else if ((m.x != askedAt.x || m.y != askedAt.y) && ImGui::GetTime() - asked > 0.05) {
            snprintf(lua, sizeof(lua), "debugModelPick(%g, %g)", p.x, p.y);
            SendRaw(lua);
            askedAt = m;
            asked = ImGui::GetTime();
        }
        if (!mine && f.hover >= 0 && f.hover < (int)View().nodes.size())
            ImGui::SetTooltip("%s\nClick: select it (window Model)", NodeLabel(f.hover).c_str());
    } else if (wasHovered) {
        SendRaw("debugModelPick()");
        askedAt = ImVec2(-1, -1);
    }
    wasHovered = hovered;
    return hovered;
}

// ---------------------------------------------------------------------------
// A model from disk: read, shown by the debugger, its animations played
// ---------------------------------------------------------------------------

namespace {

std::string g_wanted; // a model file opened from Explorer, "" for the stage's

// The same file named from two folders: one path ends the other
bool SameFile(const std::string &a, const std::string &b) {
    std::string x = NormPath(a), y = NormPath(b);
    if (x.size() < y.size()) std::swap(x, y);
    return !y.empty() && x.compare(x.size() - y.size(), y.size(), y) == 0 &&
           (x.size() == y.size() || x[x.size() - y.size() - 1] == '/');
}

// The model a stage's def names ([BGdef] model), as found next to it; "" without one
std::string ModelOfDef(const std::string &def) {
    static std::string lastDef, lastText, lastModel;
    if (def.empty() || !GameFileExists(def)) return "";
    SourceFile &sf = LoadSource(def);
    if (def == lastDef && sf.text == lastText) return lastModel;
    lastDef = def, lastText = sf.text, lastModel.clear();
    IniFile ini;
    Ini_Parse(sf.text, ini);
    for (const IniSection &s : ini.sections)
        if (s.lower == "bgdef")
            if (const IniField *f = s.Field("model")) {
                std::string v = f->value;
                if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
                lastModel = StageFile(def, v);
            }
    return lastModel;
}

void FillDiskView() {
    const GltfDoc &d = g_disk.doc;
    ModelView &v = g_diskView;
    v = ModelView();
    v.known = v.has = true;
    v.file = g_disk.path;
    for (const GltfScene &s : d.scenes) v.scenes.push_back({s.name, s.roots});
    for (const GltfNode &n : d.nodes) {
        ModelView::Node o;
        o.name = n.name, o.mesh = n.mesh, o.light = n.light, o.parent = n.parent, o.children = n.children;
        memcpy(o.t, n.t, sizeof(o.t)), memcpy(o.r, n.r, sizeof(o.r)), memcpy(o.s, n.s, sizeof(o.s));
        o.trans = n.trans, o.hasLayer = n.hasLayer, o.layer = n.layer;
        o.shadow = n.shadow, o.ztest = n.ztest, o.zwrite = n.zwrite, o.outline = n.outline;
        v.nodes.push_back(o);
    }
    for (const GltfMesh &m : d.meshes) {
        ModelView::Mesh o;
        o.name = m.name;
        for (const GltfPrim &p : m.prims) o.prims.push_back({p.material, (int)(p.pos.size() / 3), (int)p.idx.size(), p.mode});
        v.meshes.push_back(o);
    }
    for (const GltfMaterial &m : d.materials) {
        ModelView::Material o;
        o.name = m.name;
        memcpy(o.color, m.color, sizeof(o.color)), memcpy(o.emission, m.emission, sizeof(o.emission));
        o.metallic = m.metallic, o.roughness = m.roughness, o.ao = m.ao, o.cutoff = m.cutoff;
        o.alpha = m.alpha, o.tex = m.tex, o.doubleSided = m.doubleSided, o.unlit = m.unlit;
        v.materials.push_back(o);
    }
    for (const GltfLight &l : d.lights) {
        ModelView::Light o;
        o.name = l.name, o.type = l.type, o.intensity = l.intensity, o.range = l.range, o.inner = l.inner, o.outer = l.outer;
        memcpy(o.rgb, l.rgb, sizeof(o.rgb));
        v.lights.push_back(o);
    }
    for (const GltfAnim &a : d.anims) {
        ModelView::Anim o;
        o.name = a.name, o.duration = a.duration, o.enabled = true, o.channels = (int)a.channels.size();
        v.anims.push_back(o);
    }
}

void DiskLoad(const std::string &path, const std::string &def) {
    DiskModel &d = g_disk;
    Gltf_Load(d.doc, GamePath(path), DecodeImage);
    d.path = path, d.def = def;
    d.version++;
    d.mtime = FileTime(GamePath(path));
    d.orbitSet = false;
    d.gameCamera = !def.empty();
    d.hover = -1;
    FillDiskView();
    g_selKind = kNone, g_sel = -1;
    g_histDisk = History();
    g_keysAnim = -1, g_key = KeySel();
}

// The model read again when its file changed on disk (and nothing was changed here)
void DiskEnsure(const std::string &path, const std::string &def) {
    DiskModel &d = g_disk;
    if (!SameFile(path, d.path) || d.path.empty()) {
        DiskLoad(path, def);
        return;
    }
    d.def = def;
    if (ImGui::GetTime() - d.checked < 1) return;
    d.checked = ImGui::GetTime();
    FILETIME t = FileTime(GamePath(path));
    if (CompareFileTime(&t, &d.mtime) != 0 && !d.doc.Dirty()) DiskLoad(path, def);
}

// The animations on move on; every node posed
void DiskStep() {
    DiskModel &d = g_disk;
    ModelView &v = g_diskView;
    float dt = ImGui::GetIO().DeltaTime;
    std::vector<float> times(v.anims.size(), -1.0f);
    for (size_t i = 0; i < v.anims.size(); i++) {
        ModelView::Anim &a = v.anims[i];
        if (!a.enabled) continue;
        if (d.playing && !ImGui::IsAnyItemActive()) a.time += dt;
        if (a.duration > 0 && a.time >= a.duration) a.time = fmodf(a.time, a.duration), a.loop++;
        times[i] = a.time;
    }
    for (ModelView::Node &n : v.nodes) n.animated = false;
    for (size_t i = 0; i < d.doc.anims.size() && i < v.anims.size(); i++)
        if (v.anims[i].enabled)
            for (const auto &c : d.doc.anims[i].channels)
                if (c.node >= 0 && c.node < (int)v.nodes.size()) v.nodes[c.node].animated = true;
    Gltf_Pose(d.doc, times, d.world);
}

// The def's placement of the model and its camera
struct Placement {
    float offset[3] = {0, 0, 0}, scale[3] = {1, 1, 1}, fov = 40, zNear = 0.1f, zFar = 10000;
    bool environment = false;
};

Placement DefPlacement(const std::string &def) {
    Placement p;
    if (def.empty()) return p;
    const IniFile &ini = StageIni(LoadSource(def));
    ReadFloats(ini, "model", "offset", p.offset, 3);
    ReadFloats(ini, "model", "scale", p.scale, 3);
    ReadFloats(ini, "camera", "fov", &p.fov, 1);
    ReadFloats(ini, "camera", "near", &p.zNear, 1);
    ReadFloats(ini, "camera", "far", &p.zFar, 1);
    for (const IniSection &s : ini.sections)
        if (s.lower == "model" && s.Field("environment")) p.environment = true;
    return p;
}

// The meshes' middle and size, without a node much bigger than the others (a sky around them)
void SceneBounds(float centre[3], float &radius) {
    const GltfDoc &doc = g_disk.doc;
    struct Box {
        float lo[3], hi[3], size;
    };
    std::vector<Box> boxes;
    for (int i = 0; i < (int)doc.nodes.size(); i++) {
        float c[8][3];
        if (doc.nodes[i].mesh < 0 || !Gltf_Corners(doc, g_disk.world, i, c)) continue;
        Box b{{FLT_MAX, FLT_MAX, FLT_MAX}, {-FLT_MAX, -FLT_MAX, -FLT_MAX}, 0};
        for (auto &p : c)
            for (int a = 0; a < 3; a++) b.lo[a] = std::min(b.lo[a], p[a]), b.hi[a] = std::max(b.hi[a], p[a]);
        b.size = std::hypot(b.hi[0] - b.lo[0], b.hi[1] - b.lo[1], b.hi[2] - b.lo[2]);
        boxes.push_back(b);
    }
    centre[0] = centre[1] = centre[2] = 0;
    radius = 1;
    if (boxes.empty()) return;
    std::vector<float> sizes;
    for (const Box &b : boxes) sizes.push_back(b.size);
    std::nth_element(sizes.begin(), sizes.begin() + sizes.size() / 2, sizes.end());
    float median = sizes[sizes.size() / 2], lo[3] = {FLT_MAX, FLT_MAX, FLT_MAX}, hi[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    for (const Box &b : boxes) {
        if (boxes.size() > 2 && b.size > median * 5) continue;
        for (int a = 0; a < 3; a++) lo[a] = std::min(lo[a], b.lo[a]), hi[a] = std::max(hi[a], b.hi[a]);
    }
    if (lo[0] > hi[0]) return;
    for (int a = 0; a < 3; a++) centre[a] = (lo[a] + hi[a]) / 2;
    radius = std::max(1e-3f, std::hypot(hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]) / 2);
}

void FitOrbit(float fov) {
    DiskModel &d = g_disk;
    float radius;
    SceneBounds(d.target, radius);
    d.dist = radius / std::max(0.05f, tanf(fov / kDeg / 2)) * 1.1f;
    d.yaw = 0.5f, d.pitch = 0.35f;
    d.orbitSet = true;
    d.gameCamera = false;
}

// From the game's camera to turning around: from where it looks, at the model's depth
void OrbitFromGameCamera(const Placement &p) {
    DiskModel &d = g_disk;
    float eye[3];
    for (int a = 0; a < 3; a++) eye[a] = p.scale[a] != 0 ? -p.offset[a] / p.scale[a] : 0;
    d.dist = p.scale[2] != 0 && p.offset[2] != 0 ? std::fabs(p.offset[2] / p.scale[2]) : 10;
    d.target[0] = eye[0], d.target[1] = eye[1], d.target[2] = eye[2] - d.dist;
    d.yaw = 0, d.pitch = 0;
    d.orbitSet = true;
    d.gameCamera = false;
}

void Matrices(const Placement &p, float aspect, GltfMat &proj, GltfMat &view) {
    DiskModel &d = g_disk;
    if (d.gameCamera) {
        float none[4] = {0, 0, 0, 1};
        view = Gltf_Compose(p.offset, none, p.scale);
        proj = Perspective(p.fov / kDeg, aspect, std::max(1e-4f, p.zNear), std::max(p.zNear * 2, p.zFar));
        return;
    }
    float cp = cosf(d.pitch);
    float eye[3] = {d.target[0] + d.dist * cp * sinf(d.yaw), d.target[1] + d.dist * sinf(d.pitch), d.target[2] + d.dist * cp * cosf(d.yaw)};
    view = LookAt(eye, d.target);
    float c[3], radius;
    SceneBounds(c, radius);
    proj = Perspective(p.fov / kDeg, aspect, std::max(1e-3f, d.dist * 0.002f), d.dist * 20 + radius * 40);
}

// The preview: drawn by the debugger; the left button turns around (a click picks a node), the
// right or middle one moves, the wheel comes nearer
void Preview() {
    DiskModel &d = g_disk;
    ModelView &v = g_diskView;
    Placement p = DefPlacement(d.def);
    if (!d.orbitSet && !d.gameCamera) FitOrbit(p.fov);

    ImGui::BeginDisabled(d.def.empty());
    if (ImGui::RadioButton("The game's camera", d.gameCamera)) d.gameCamera = true;
    ImGui::EndDisabled();
    ImGui::SetItemTooltip(d.def.empty() ? "No stage's def names this model."
                                        : "As the stage's camera sees the model at its start ([Model] offset and scale, fov)");
    ImGui::SameLine();
    if (ImGui::RadioButton("Around", !d.gameCamera)) {
        if (d.gameCamera && !d.def.empty()) OrbitFromGameCamera(p);
        d.gameCamera = false;
    }
    ImGui::SetItemTooltip("Drag: turn around; right or middle drag: move; wheel: nearer or farther; double click: all of it");
    ImGui::SameLine();
    if (ImGui::Button(d.playing ? "Pause" : "Play")) d.playing = !d.playing;
    ImGui::SetItemTooltip("The animations that are on");
    ImGui::SameLine();
    if (ImGui::Button("Fit")) FitOrbit(p.fov);
    SameLineFits(DragModeWidth());
    DragModeButtons();
    int lighting = !d.doc.lights.empty() ? 1 : p.environment ? 2 : 0;
    ImGui::SameLine();
    ImGui::TextDisabled(lighting ? "(lights: a rough look)" : "(unlit, as the game draws it)");
    ImGui::SetItemTooltip(lighting ? "The colours and lights as the debugger can show them; the game's shading is finer."
                                   : "The stage has no light and no environment map: the game draws its colours as they are.");

    // Below the toolbar, however many lines it took
    ImVec2 size(ImGui::GetContentRegionAvail().x, std::max(ImGui::GetContentRegionAvail().y, 32.0f));
    GltfMat proj, view;
    Matrices(p, size.x / std::max(1.0f, size.y), proj, view);
    std::vector<char> visible;
    for (const auto &n : v.nodes) visible.push_back(n.visible);
    ModelPreviewArgs a;
    a.doc = &d.doc, a.version = d.version, a.world = &d.world, a.visible = &visible;
    a.proj = proj, a.view = view;
    a.w = (int)size.x, a.h = (int)size.y;
    a.selected = g_selKind == kNode ? g_sel : -1;
    a.hover = d.hover;
    a.lighting = lighting;
    unsigned tex = DrawModelPreview(a);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    if (!tex) {
        ImGui::Dummy(size);
        ImGui::GetWindowDrawList()->AddText(p0, ImGui::GetColorU32(ImGuiCol_TextDisabled), "This OpenGL has no framebuffers: no preview.");
        return;
    }
    ImGui::Image(ImTextureRef((ImTextureID)(intptr_t)tex), size, ImVec2(0, 1), ImVec2(1, 0));
    // A button over it takes the mouse (drags turn the camera, not the window)
    ImGui::SetCursorScreenPos(p0);
    ImGui::InvisibleButton("##view", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
                                               ImGuiButtonFlags_MouseButtonMiddle);
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
    bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    ImGuiIO &io = ImGui::GetIO();

    // The node under the mouse: the ray through it, in the world
    GltfMat inv = Gltf_Invert(Gltf_Mul(proj, view));
    auto pickAt = [&](ImVec2 m) {
        float nx = (m.x - p0.x) / size.x * 2 - 1, ny = 1 - (m.y - p0.y) / size.y * 2;
        float pts[2][3];
        for (int k = 0; k < 2; k++) {
            float z = k ? 1.0f : -1.0f, in[4] = {nx, ny, z, 1}, out[4] = {0, 0, 0, 0};
            for (int r = 0; r < 4; r++)
                for (int c = 0; c < 4; c++) out[r] += inv[c * 4 + r] * in[c];
            for (int a2 = 0; a2 < 3; a2++) pts[k][a2] = out[3] != 0 ? out[a2] / out[3] : out[a2];
        }
        return Gltf_Pick(d.doc, d.world, visible, pts[0], pts[1]);
    };
    // The selected node's handles, or the node itself, pressed on and dragged; elsewhere a drag
    // turns the camera
    static NodeDrag drag;
    Picture pic;
    pic.viewProj = Gltf_Mul(proj, view);
    pic.p0 = p0, pic.size = size;
    int sel = g_selKind == kNode && g_sel >= 0 && g_sel < (int)v.nodes.size() && g_sel < (int)d.world.size() ? g_sel : -1;
    int parent = sel >= 0 ? v.nodes[sel].parent : -1;
    GltfMat parentWorld = parent >= 0 && parent < (int)d.world.size() ? d.world[parent] : Gltf_Identity();
    ImDrawList *draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(p0, ImVec2(p0.x + size.x, p0.y + size.y), true);
    bool wasDragging = drag.node >= 0; // its release does not pick the node under the mouse
    bool mine = (sel >= 0 || drag.node >= 0) &&
                NodeHandles(drag, v, draw, pic, sel, d.hover, parentWorld, sel >= 0 ? d.world[sel] : Gltf_Identity(), hovered,
                            hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left), true, [](int i) { SendTRS(i); });
    draw->PopClipRect();
    if (drag.node >= 0 || wasDragging) {
        d.hover = -1;
        return;
    }
    static bool dragged;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) dragged = false;
    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3)) dragged = true;
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) FitOrbit(p.fov);
    bool panning = active && (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 2) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 2));
    bool moving = (dragged && active && ImGui::IsMouseDown(ImGuiMouseButton_Left)) || panning || (hovered && io.MouseWheel != 0);
    if (moving && d.gameCamera) OrbitFromGameCamera(p);
    if (moving) {
        if (hovered && io.MouseWheel != 0) d.dist *= powf(0.85f, io.MouseWheel);
        if (dragged && active && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            d.yaw -= io.MouseDelta.x * 0.01f;
            d.pitch = std::clamp(d.pitch + io.MouseDelta.y * 0.01f, -1.55f, 1.55f);
        }
        if (panning) {
            // Along the screen: the camera's right and up, as far as the mouse at the target's depth
            float k = d.dist * 2 * tanf(p.fov / kDeg / 2) / size.y;
            float right[3] = {view[0], view[4], view[8]}, up[3] = {view[1], view[5], view[9]};
            for (int a2 = 0; a2 < 3; a2++) d.target[a2] += (-io.MouseDelta.x * right[a2] + io.MouseDelta.y * up[a2]) * k;
        }
    }
    if (hovered && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) d.hover = pickAt(io.MousePos);
    else if (!hovered) d.hover = -1;
    if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !dragged) {
        int hit = pickAt(io.MousePos);
        if (hit >= 0) SelectNode(hit, true);
    }
    if (hovered && !mine && d.hover >= 0 && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        ImGui::SetTooltip("%s", NodeLabel(d.hover).c_str());
}

// The lists (nodes, materials, animations, lights) and the inspector beside them, or under them
void ListsAndInspector(float lw) {
    ModelView &m = View();
    float em = ImGui::GetFontSize();
    bool wide = ImGui::GetContentRegionAvail().x > em * 34;
    float h = ImGui::GetContentRegionAvail().y;
    auto lists = [&]() {
        if (ImGui::BeginTabBar("lists")) {
            const char *tabs[] = {"Nodes", "Materials", "Animations", "Lights"};
            for (int t = 0; t < 4; t++) {
                bool open = ImGui::BeginTabItem(tabs[t], nullptr, g_tab == t ? ImGuiTabItemFlags_SetSelected : 0);
                size_t count = t == 0 ? m.nodes.size() : t == 1 ? m.materials.size() : t == 2 ? m.anims.size() : m.lights.size();
                const char *what[] = {"nodes", "materials", "animations", "lights"};
                ImGui::SetItemTooltip("%zu %s", count, what[t]);
                if (!open) continue;
                if (t == 0) NodesTab();
                if (t == 1) MaterialsTab();
                if (t == 2) AnimationsTab();
                else g_animsTab = false;
                if (t == 3) LightsTab();
                ImGui::EndTabItem();
            }
            g_tab = -1;
            ImGui::EndTabBar();
        }
    };
    auto inspector = [&]() {
        if (g_animsTab && g_keysAnim >= 0 && g_keysAnim < (int)m.anims.size()) {
            ImGui::Text("Keys of %s", AnimName(g_keysAnim).c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%.2f / %.2f s", m.anims[g_keysAnim].time, m.anims[g_keysAnim].duration);
            KeysPanel(g_keysAnim, lw);
        } else if (g_selKind == kNode && g_sel >= 0 && g_sel < (int)m.nodes.size()) NodeInspector(g_sel, lw);
        else if (g_selKind == kMaterial && g_sel >= 0 && g_sel < (int)m.materials.size()) MaterialInspector(g_sel, lw);
        else if (g_selKind == kLight && g_sel >= 0 && g_sel < (int)m.lights.size()) LightInspector(g_sel, lw);
        else
            ImGui::TextDisabled(g_isLive ? "Pick a node, a material or a light in the lists, or a node on the Game window."
                                         : "Pick a node, a material or a light in the lists, or a node on the preview.");
    };
    if (wide) {
        if (ImGui::BeginTable("model", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, ImVec2(0, h))) {
            ImGui::TableSetupColumn("lists", ImGuiTableColumnFlags_WidthStretch, 0.45f);
            ImGui::TableSetupColumn("inspector", ImGuiTableColumnFlags_WidthStretch, 0.55f);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::BeginChild("lists", ImVec2(0, h - ImGui::GetStyle().CellPadding.y * 2))) lists();
            ImGui::EndChild();
            ImGui::TableNextColumn();
            if (ImGui::BeginChild("inspector", ImVec2(0, h - ImGui::GetStyle().CellPadding.y * 2))) inspector();
            ImGui::EndChild();
            ImGui::EndTable();
        }
    } else {
        if (ImGui::BeginChild("lists", ImVec2(0, std::max(em * 10, h * 0.45f)), ImGuiChildFlags_ResizeY)) lists();
        ImGui::EndChild();
        if (ImGui::BeginChild("inspector")) inspector();
        ImGui::EndChild();
    }
}

} // namespace

// ---------------------------------------------------------------------------
// The windows
// ---------------------------------------------------------------------------

std::string ModelFileInUse() { return g_live.known && g_live.has ? g_live.file : ""; }

void ModelOpenFile(const std::string &path) {
    g_wanted = path;
    ShowWindow("Model");
}

// In the Stage window: what the model is, and the way to the Model window
void ModelSection(bool stageHasModel) {
    if (!stageHasModel && !g_mframe.ok) return;
    ImGui::AlignTextToFramePadding();
    if (g_live.known && g_live.has)
        ImGui::TextDisabled("3D model: %s, %zu nodes", g_live.file.c_str(), g_live.nodes.size());
    else
        ImGui::TextDisabled("3D model");
    ImGui::SameLine();
    if (ImGui::Button("Open the Model window")) {
        g_wanted.clear();
        ShowWindow("Model");
    }
}

static void SaveLiveModel() { Exec("local ok, err = debugModelSave() if not ok then error(err, 0) end"); }

static bool SaveDiskModel() {
    std::string error;
    if (!Gltf_Save(g_disk.doc, &error)) {
        Log(LogLine::Error, g_disk.path + ": " + error);
        return false;
    }
    g_disk.mtime = FileTime(GamePath(g_disk.path));
    Log(LogLine::Info, "Saved " + g_disk.path);
    return true;
}

void ModelWindow() {
    if (!BeginTool("Model")) return;
    float em = ImGui::GetFontSize(), lw = em * 6.5f;
    // The match's model, asked again while the window is shown
    if (Connected() && g_canTrace && ImGui::GetTime() - g_modelAsked > 0.4) {
        g_modelAsked = ImGui::GetTime();
        SendRaw("ikdbg.model()");
    }
    if (Connected() && g_canTrace && !g_live.known) {
        ImGui::TextDisabled("Asking the game...");
        ImGui::End();
        return;
    }
    // Which model: the one opened from Explorer, else the stage project's; live when it is the match's
    bool liveHas = StageInMatch() && g_live.known && g_live.has;
    std::string path, def;
    bool live = false;
    if (!g_wanted.empty()) {
        live = liveHas && SameFile(g_wanted, g_live.file);
        if (!live) path = g_wanted, def = StageDefOfModel(g_wanted);
    } else if (liveHas) {
        live = true;
    } else {
        def = ProjectDef(kProjectStage);
        path = ModelOfDef(def);
    }
    if (live != g_isLive) g_selKind = kNone, g_sel = -1, g_keysAnim = -1, g_key = KeySel();
    g_isLive = live;
    g_view = live ? &g_live : &g_diskView;
    if (!live) {
        if (path.empty()) {
            ImGui::TextDisabled("The stage (%s) has no 3D model ([BGdef] model = file.glb).", def.empty() ? "none" : def.c_str());
            ImGui::TextDisabled("A .glb or .gltf file opened in Explorer is shown here.");
            ImGui::End();
            return;
        }
        DiskEnsure(path, def);
        if (!g_disk.doc.error.empty() && g_disk.doc.nodes.empty()) {
            ImGui::TextColored(Ink(kRed), "%s", g_disk.doc.error.c_str());
            ImGui::End();
            return;
        }
        DiskStep();
    }
    ModelView &m = View();
    HistoryTick(m);
    SourceFile *sf = live ? StageSource() : g_disk.def.empty() ? nullptr : &LoadSource(g_disk.def);
    bool dirty = live ? (g_mframe.ok ? g_mframe.dirty : m.dirty) : g_disk.doc.Dirty();
    bool defDirty = sf && sf->Dirty();

    // Header: the file, where it comes from, Save, Revert, pick
    std::string name = m.file.substr(m.file.find_last_of("/\\") + 1);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s%s", name.c_str(), dirty ? " *" : "");
    ImGui::SetItemTooltip("%s\n%zu nodes, %zu meshes, %zu materials, %zu animations, %zu lights", m.file.c_str(),
                          m.nodes.size(), m.meshes.size(), m.materials.size(), m.anims.size(), m.lights.size());
    // The header goes on in more lines when the window is narrow
    auto buttonWidth = [](const char *label) { return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2; };
    SameLineFits(ImGui::CalcTextSize(live ? "in the match" : "from disk").x);
    ImGui::TextColored(live ? Ink(kGreen) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), live ? "in the match" : "from disk");
    ImGui::SetItemTooltip(live ? "The game's model: every change shows on the game."
                               : "Read from its file and drawn here; changes go to the file with Save.");
    SameLineFits(buttonWidth("Save"));
    ImGui::BeginDisabled(!dirty && !defDirty);
    bool save = ImGui::Button(ICON_SAVE " Save");
    ImGuiIO &io = ImGui::GetIO();
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
        save = dirty || defDirty;
    ImGui::SetItemTooltip("Writes the model's changes into its file (a .bak of it the first time),\n"
                          "and the stage's def when its placement changed.");
    if (save) {
        if (dirty && live) SaveLiveModel();
        if (dirty && !live) SaveDiskModel();
        if (defDirty) SaveSource(*sf);
    }
    SameLineFits(buttonWidth("Revert"));
    ImGui::BeginDisabled(!dirty);
    if (ImGui::Button(ICON_REVERT " Revert")) {
        if (live) {
            SendRaw("debugModelRevert()");
            if (sf) ReloadStage(*sf, true);
            g_histLive = History();
            g_liveKeys.clear();
        } else {
            DiskLoad(g_disk.path, g_disk.def);
        }
    }
    ImGui::SetItemTooltip(live ? "The game reads the model's file again (the changes not saved are lost)."
                               : "The model read from its file again (the changes not saved are lost).");
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    // Undo, redo: Ctrl+Z, Ctrl+Y (or Ctrl+Shift+Z) while this window has the keyboard
    History &hist = HistoryOf(&m);
    bool keys = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput && io.KeyCtrl;
    for (int redo = 0; redo < 2; redo++) {
        const auto &steps = redo ? hist.redo : hist.undo;
        SameLineFits(ImGui::GetFrameHeight());
        ImGui::BeginDisabled(steps.empty());
        bool go = IconButton(redo ? ICON_AGAIN "##redo" : ICON_BACK "##undo");
        ImGui::EndDisabled();
        if (steps.empty()) ImGui::SetItemTooltip(redo ? "Nothing undone to do again" : "No change to undo");
        else ImGui::SetItemTooltip("%s %s (%s)", redo ? "Do again:" : "Undo:", StepLabel(steps.back()).c_str(), redo ? "Ctrl+Y" : "Ctrl+Z");
        if (keys && !steps.empty()) {
            if (redo) go |= ImGui::IsKeyPressed(ImGuiKey_Y) || (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z));
            else go |= !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z);
        }
        if (go) UndoStep(redo != 0);
    }
    if (live) {
        SameLineFits(CheckboxWidth("Pick on the Game window"));
        if (ImGui::Checkbox("Pick on the Game window", &g_modelPick) && g_modelPick) g_stagePick = g_screenPick = false;
        ImGui::SetItemTooltip("On the Game window the mouse shows the node under it; a click selects it,\n"
                              "and the one selected, or its handles, dragged, moves, turns or scales it.");
        if (g_modelPick) {
            SameLineFits(DragModeWidth());
            DragModeButtons();
        }
    }
    if (!g_wanted.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("The stage's model")) g_wanted.clear();
        ImGui::SetItemTooltip("Show the model of the stage (the match's, else the toolbar's) instead of this file");
    }

    PlacementSection(sf, lw);
    if (!live) {
        float h = ImGui::GetContentRegionAvail().y;
        if (ImGui::BeginChild("preview", ImVec2(0, std::max(em * 12, h * 0.5f)), ImGuiChildFlags_ResizeY))
            Preview();
        ImGui::EndChild();
    }
    ListsAndInspector(lw);
    ImGui::End();
}

void ModelUnsaved(std::vector<Unsaved> &out) {
    if (Connected() && g_live.known && g_live.has && (g_mframe.ok ? g_mframe.dirty : g_live.dirty))
        out.push_back({g_live.file, [] {
                           SaveLiveModel();
                           return true;
                       }});
    if (g_disk.doc.Dirty()) out.push_back({g_disk.path, SaveDiskModel});
}

} // namespace app
