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
// Changes sent to the game
// ---------------------------------------------------------------------------

// From disk: into the model read (what Save writes), the preview showing it at once
void SendTRS(int i) {
    const auto &n = View().nodes[i];
    if (!g_isLive) {
        GltfNode &d = g_disk.doc.nodes[i];
        memcpy(d.t, n.t, sizeof(d.t)), memcpy(d.r, n.r, sizeof(d.r)), memcpy(d.s, n.s, sizeof(d.s));
        g_disk.doc.trs.insert(i);
        return;
    }
    char lua[320];
    snprintf(lua, sizeof(lua), "debugModelNodeTRS(%d, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g)", i, n.t[0], n.t[1], n.t[2],
             n.r[0], n.r[1], n.r[2], n.r[3], n.s[0], n.s[1], n.s[2]);
    SendRaw(lua);
    g_modelAsked = ImGui::GetTime();
}

void SendProps(int i) {
    const auto &n = View().nodes[i];
    if (!g_isLive) {
        GltfNode &d = g_disk.doc.nodes[i];
        d.trans = n.trans, d.hasLayer = n.hasLayer, d.layer = n.layer;
        d.shadow = n.shadow, d.ztest = n.ztest, d.zwrite = n.zwrite, d.outline = n.outline;
        g_disk.doc.props.insert(i);
        return;
    }
    char lua[200];
    snprintf(lua, sizeof(lua), "debugModelNodeProps(%d, %d, %s, %s, %s, %s, %g)", i, n.trans,
             n.hasLayer ? std::to_string(n.layer).c_str() : "nil", n.shadow ? "true" : "false",
             n.ztest ? "true" : "false", n.zwrite ? "true" : "false", n.outline);
    SendRaw(lua);
    g_modelAsked = ImGui::GetTime();
}

void SendMaterial(int i) {
    const auto &m = View().materials[i];
    if (!g_isLive) {
        GltfMaterial &d = g_disk.doc.materials[i];
        memcpy(d.color, m.color, sizeof(d.color)), memcpy(d.emission, m.emission, sizeof(d.emission));
        d.metallic = m.metallic, d.roughness = m.roughness, d.ao = m.ao, d.alpha = m.alpha, d.cutoff = m.cutoff;
        d.doubleSided = m.doubleSided, d.unlit = m.unlit;
        g_disk.doc.mats.insert(i);
        return;
    }
    char lua[400];
    snprintf(lua, sizeof(lua), "debugModelMaterial(%d, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %d, %g, %s, %s)", i,
             m.color[0], m.color[1], m.color[2], m.color[3], m.emission[0], m.emission[1], m.emission[2], m.metallic,
             m.roughness, m.ao, m.alpha, m.cutoff, m.doubleSided ? "true" : "false", m.unlit ? "true" : "false");
    SendRaw(lua);
    g_modelAsked = ImGui::GetTime();
}

void SendLight(int i) {
    const auto &l = View().lights[i];
    if (!g_isLive) {
        GltfLight &d = g_disk.doc.lights[i];
        memcpy(d.rgb, l.rgb, sizeof(d.rgb));
        d.intensity = l.intensity, d.range = l.range, d.inner = l.inner, d.outer = l.outer;
        g_disk.doc.lightsChanged.insert(i);
        return;
    }
    char lua[220];
    snprintf(lua, sizeof(lua), "debugModelLight(%d, %g, %g, %g, %g, %g, %g, %g)", i, l.rgb[0], l.rgb[1], l.rgb[2],
             l.intensity, l.range, l.inner, l.outer);
    SendRaw(lua);
    g_modelAsked = ImGui::GetTime();
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

void AnimationsTab() {
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
    // Each one: on or off, its name and loops, then where it is (dragged to put it there)
    for (int i = 0; i < (int)m.anims.size(); i++) {
        ModelView::Anim &a = m.anims[i];
        ImGui::PushID(i);
        if (i > 0) ImGui::Spacing();
        bool on = a.enabled;
        if (ImGui::Checkbox("##on", &on)) SetAnimOn(i, on);
        ImGui::SetItemTooltip("On: it starts again from its start. Off: what it moves goes back to rest.");
        ImGui::SameLine();
        ImGui::TextUnformatted(a.name.empty() ? ("animation " + std::to_string(i)).c_str() : a.name.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%d channels%s", a.channels, a.id ? (", id " + std::to_string(a.id)).c_str() : "");
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
    ImGui::Text("%s", NodeName(i).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("node %d", i);
    if (n.parent >= 0) {
        ImGui::SameLine();
        if (ImGui::SmallButton(("Parent: " + NodeName(n.parent)).c_str())) SelectNode(n.parent, true);
    }
    bool visible = n.visible;
    if (ImGui::Checkbox("Visible", &visible)) SetVisible(i, visible, false);
    ImGui::SetItemTooltip(g_isLive ? "Until the round starts again; not saved." : "In the preview; not saved.");
    if (!n.children.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton(n.visible ? "Hide with its children" : "Show with its children")) SetVisible(i, !n.visible, true);
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
            ImGui::Bullet();
            if (mat >= 0) {
                if (ImGui::SmallButton(MaterialName(mat).c_str())) Select(kMaterial, mat);
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
            if (ImGui::SmallButton(("Its node: " + NodeName(n)).c_str())) SelectNode(n, true);
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

void ModelPickToggle() {
    static bool stage, screen;
    // The other pick modes, turned on, turn this one off
    if ((g_stagePick && !stage) || (g_screenPick && !screen)) g_modelPick = false;
    if (!g_mframe.ok && !g_modelPick) {
        stage = g_stagePick, screen = g_screenPick;
        return;
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("3D model", &g_modelPick) && g_modelPick) g_stagePick = g_screenPick = false;
    ImGui::SetItemTooltip("The mouse picks the stage model's nodes instead of the players (window Model).");
    stage = g_stagePick, screen = g_screenPick;
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
        if (f.hover >= 0 && f.hover < (int)View().nodes.size())
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
void Preview(float height) {
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
    int lighting = !d.doc.lights.empty() ? 1 : p.environment ? 2 : 0;
    ImGui::SameLine();
    ImGui::TextDisabled(lighting ? "(lights: a rough look)" : "(unlit, as the game draws it)");
    ImGui::SetItemTooltip(lighting ? "The colours and lights as the debugger can show them; the game's shading is finer."
                                   : "The stage has no light and no environment map: the game draws its colours as they are.");

    ImVec2 size(ImGui::GetContentRegionAvail().x, std::max(height - ImGui::GetFrameHeightWithSpacing(), 32.0f));
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
    if (hovered && d.hover >= 0 && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) ImGui::SetTooltip("%s", NodeLabel(d.hover).c_str());
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
                if (t == 3) LightsTab();
                ImGui::EndTabItem();
            }
            g_tab = -1;
            ImGui::EndTabBar();
        }
    };
    auto inspector = [&]() {
        if (g_selKind == kNode && g_sel >= 0 && g_sel < (int)m.nodes.size()) NodeInspector(g_sel, lw);
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
    if (ImGui::SmallButton("Open the Model window")) {
        g_wanted.clear();
        ShowWindow("Model");
    }
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
    // Which model: the one opened from Explorer, else the stage's (the match's, else the toolbar's);
    // live when it is the match's
    bool liveHas = Connected() && g_live.known && g_live.has;
    std::string path, def;
    bool live = false;
    if (!g_wanted.empty()) {
        live = liveHas && SameFile(g_wanted, g_live.file);
        if (!live) path = g_wanted, def = StageDefOfModel(g_wanted);
    } else if (liveHas) {
        live = true;
    } else {
        def = !g_stageDef.empty() ? g_stageDef : SettingsStageDef();
        path = ModelOfDef(def);
    }
    if (live != g_isLive) g_selKind = kNone, g_sel = -1;
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
    SourceFile *sf = live ? StageSource() : g_disk.def.empty() ? nullptr : &LoadSource(g_disk.def);
    bool dirty = live ? (g_mframe.ok ? g_mframe.dirty : m.dirty) : g_disk.doc.Dirty();
    bool defDirty = sf && sf->Dirty();

    // Header: the file, where it comes from, Save, Revert, pick
    std::string name = m.file.substr(m.file.find_last_of("/\\") + 1);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s%s", name.c_str(), dirty ? " *" : "");
    ImGui::SetItemTooltip("%s\n%zu nodes, %zu meshes, %zu materials, %zu animations, %zu lights", m.file.c_str(),
                          m.nodes.size(), m.meshes.size(), m.materials.size(), m.anims.size(), m.lights.size());
    ImGui::SameLine();
    ImGui::TextColored(live ? Ink(kGreen) : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), live ? "in the match" : "from disk");
    ImGui::SetItemTooltip(live ? "The game's model: every change shows on the game."
                               : "Read from its file and drawn here; changes go to the file with Save.");
    ImGui::SameLine();
    ImGui::BeginDisabled(!dirty && !defDirty);
    bool save = ImGui::Button("Save");
    ImGuiIO &io = ImGui::GetIO();
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
        save = dirty || defDirty;
    ImGui::SetItemTooltip("Writes the model's changes into its file (a .bak of it the first time),\n"
                          "and the stage's def when its placement changed.");
    if (save) {
        if (dirty && live) Exec("local ok, err = debugModelSave() if not ok then error(err, 0) end");
        if (dirty && !live) {
            std::string error;
            if (Gltf_Save(g_disk.doc, &error)) {
                g_disk.mtime = FileTime(GamePath(g_disk.path));
                Log(LogLine::Info, "Saved " + g_disk.path);
            } else {
                Log(LogLine::Error, g_disk.path + ": " + error);
            }
        }
        if (defDirty) SaveSource(*sf);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!dirty);
    if (ImGui::Button("Revert")) {
        if (live) {
            SendRaw("debugModelRevert()");
            if (sf) ReloadStage(*sf, true);
        } else {
            DiskLoad(g_disk.path, g_disk.def);
        }
    }
    ImGui::SetItemTooltip(live ? "The game reads the model's file again (the changes not saved are lost)."
                               : "The model read from its file again (the changes not saved are lost).");
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (live) {
        ImGui::SameLine();
        if (ImGui::Checkbox("Pick on the Game window", &g_modelPick) && g_modelPick) g_stagePick = g_screenPick = false;
        ImGui::SetItemTooltip("On the Game window the mouse shows the node under it; a click selects it.");
    }
    if (!g_wanted.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("The stage's model")) g_wanted.clear();
        ImGui::SetItemTooltip("Show the model of the stage (the match's, else the toolbar's) instead of this file");
    }

    PlacementSection(sf, lw);
    if (!live) {
        float h = ImGui::GetContentRegionAvail().y;
        if (ImGui::BeginChild("preview", ImVec2(0, std::max(em * 12, h * 0.5f)), ImGuiChildFlags_ResizeY))
            Preview(ImGui::GetContentRegionAvail().y);
        ImGui::EndChild();
    }
    ListsAndInspector(lw);
    ImGui::End();
}

} // namespace app
