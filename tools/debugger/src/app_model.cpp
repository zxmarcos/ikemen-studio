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
ModelView g_model;
double g_modelAsked = -1;

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
    SendRaw("debugModelSelect(" + std::to_string(i) + ")");
}

void Select(SelKind kind, int i) {
    g_selKind = kind;
    g_sel = i;
    SendRaw("debugModelSelect(-1)");
}

std::string NodeName(int i) {
    if (i < 0 || i >= (int)g_model.nodes.size()) return "?";
    const auto &n = g_model.nodes[i];
    return n.name.empty() ? "node " + std::to_string(i) : n.name;
}

std::string MaterialName(int i) {
    if (i < 0 || i >= (int)g_model.materials.size()) return "no material";
    const auto &m = g_model.materials[i];
    return m.name.empty() ? "material " + std::to_string(i) : m.name;
}

std::string LightName(int i) {
    if (i < 0 || i >= (int)g_model.lights.size()) return "?";
    const auto &l = g_model.lights[i];
    return l.name.empty() ? "light " + std::to_string(i) : l.name;
}

std::string NodeLabel(int i) {
    const auto &n = g_model.nodes[i];
    std::string label = NodeName(i);
    if (n.mesh >= 0 && n.mesh < (int)g_model.meshes.size()) {
        const auto &m = g_model.meshes[n.mesh];
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
    for (int c : g_model.nodes[i].children)
        if (c >= 0 && c < (int)g_model.nodes.size() && Matches(c, filter, depth + 1)) return true;
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

void SendTRS(int i) {
    const auto &n = g_model.nodes[i];
    char lua[320];
    snprintf(lua, sizeof(lua), "debugModelNodeTRS(%d, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g)", i, n.t[0], n.t[1], n.t[2],
             n.r[0], n.r[1], n.r[2], n.r[3], n.s[0], n.s[1], n.s[2]);
    SendRaw(lua);
    g_modelAsked = ImGui::GetTime();
}

void SendProps(int i) {
    const auto &n = g_model.nodes[i];
    char lua[200];
    snprintf(lua, sizeof(lua), "debugModelNodeProps(%d, %d, %s, %s, %s, %s, %g)", i, n.trans,
             n.hasLayer ? std::to_string(n.layer).c_str() : "nil", n.shadow ? "true" : "false",
             n.ztest ? "true" : "false", n.zwrite ? "true" : "false", n.outline);
    SendRaw(lua);
    g_modelAsked = ImGui::GetTime();
}

void SendMaterial(int i) {
    const auto &m = g_model.materials[i];
    char lua[400];
    snprintf(lua, sizeof(lua), "debugModelMaterial(%d, %g, %g, %g, %g, %g, %g, %g, %g, %g, %g, %d, %g, %s, %s)", i,
             m.color[0], m.color[1], m.color[2], m.color[3], m.emission[0], m.emission[1], m.emission[2], m.metallic,
             m.roughness, m.ao, m.alpha, m.cutoff, m.doubleSided ? "true" : "false", m.unlit ? "true" : "false");
    SendRaw(lua);
    g_modelAsked = ImGui::GetTime();
}

void SendLight(int i) {
    const auto &l = g_model.lights[i];
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
    ModelView::Node &n = g_model.nodes[i];
    if (depth > 256) return;
    ImGui::PushID(i);
    bool visible = n.visible;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, 0));
    if (ImGui::Checkbox("##v", &visible)) {
        bool children = ImGui::GetIO().KeyShift;
        SendRaw("debugModelNode(" + std::to_string(i) + ", " + (visible ? "true" : "false") + (children ? ", true)" : ")"));
        n.visible = visible; // until the next answer
    }
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
            if (c >= 0 && c < (int)g_model.nodes.size() && (filter.empty() || Matches(c, filter))) NodeTree(c, filter, onPath, depth + 1);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void NodesTab() {
    ModelView &m = g_model;
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
    for (int i = 0; i < (int)g_model.nodes.size(); i++) {
        int me = g_model.nodes[i].mesh;
        if (me < 0 || me >= (int)g_model.meshes.size()) continue;
        for (const auto &p : g_model.meshes[me].prims)
            if (p.material == mat) {
                out.push_back(i);
                break;
            }
    }
    return out;
}

void MaterialsTab() {
    ModelView &m = g_model;
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
    ModelView &m = g_model;
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
        if (ImGui::Checkbox("##on", &a.enabled))
            SendRaw("debugModelAnim(" + std::to_string(i) + ", " + (a.enabled ? "true)" : "false)"));
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
        if (ImGui::SliderFloat("##t", &t, 0, std::max(a.duration, 0.01f), fmt)) {
            a.time = t;
            a.enabled = true;
            char lua[96];
            snprintf(lua, sizeof(lua), "debugModelAnimTime(%d, %g)", i, t);
            SendRaw(lua);
            g_modelAsked = ImGui::GetTime();
        }
        ImGui::SetItemTooltip("Drag to put the animation at a time (with the game paused it stays there).");
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void LightsTab() {
    ModelView &m = g_model;
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
    ModelView::Light &l = g_model.lights[i];
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
    ModelView &m = g_model;
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
    if (ImGui::Checkbox("Visible", &visible)) {
        SendRaw("debugModelNode(" + std::to_string(i) + ", " + (visible ? "true" : "false") + ")");
        n.visible = visible;
    }
    ImGui::SetItemTooltip("Until the round starts again; not saved.");
    if (!n.children.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton(n.visible ? "Hide with its children" : "Show with its children"))
            SendRaw("debugModelNode(" + std::to_string(i) + ", " + (n.visible ? "false" : "true") + ", true)");
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
    ModelView &m = g_model;
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
    for (int n = 0; n < (int)g_model.nodes.size(); n++)
        if (g_model.nodes[n].light == i) {
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

void PlacementSection(float lw) {
    SourceFile *sf = StageSource();
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
    if (changed) StageEdited(*sf);
}

} // namespace

// ---------------------------------------------------------------------------
// From the game
// ---------------------------------------------------------------------------

void OnModel(const Json &j) {
    ModelView &m = g_model;
    m.known = true;
    m.has = !j["none"].truthy();
    std::string file = j["file"].str();
    if (file != m.file && !m.file.empty()) {
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
    if (ImGui::IsAnyItemActive() && j["nodes"].items.size() == m.nodes.size()) return;
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
        if (g_pickedSeen >= 0 && f.sel >= 0) {
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
        if (minX < FLT_MAX && node >= 0 && node < (int)g_model.nodes.size())
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
        if (f.hover >= 0 && f.hover < (int)g_model.nodes.size())
            ImGui::SetTooltip("%s\nClick: select it (window Model)", NodeLabel(f.hover).c_str());
    } else if (wasHovered) {
        SendRaw("debugModelPick()");
        askedAt = ImVec2(-1, -1);
    }
    wasHovered = hovered;
    return hovered;
}

// ---------------------------------------------------------------------------
// The windows
// ---------------------------------------------------------------------------

std::string ModelFileInUse() { return g_model.known && g_model.has ? g_model.file : ""; }

// In the Stage window: what the model is, and the way to the Model window
void ModelSection(bool stageHasModel) {
    if (!stageHasModel && !g_mframe.ok) return;
    ImGui::AlignTextToFramePadding();
    if (g_model.known && g_model.has)
        ImGui::TextDisabled("3D model: %s, %zu nodes", g_model.file.c_str(), g_model.nodes.size());
    else
        ImGui::TextDisabled("3D model");
    ImGui::SameLine();
    if (ImGui::SmallButton("Open the Model window")) ShowWindow("Model");
}

void ModelWindow() {
    if (!BeginTool("Model")) return;
    ModelView &m = g_model;
    float em = ImGui::GetFontSize();
    // Asked again while it is shown
    if (Connected() && g_canTrace && ImGui::GetTime() - g_modelAsked > 0.4) {
        g_modelAsked = ImGui::GetTime();
        SendRaw("ikdbg.model()");
    }
    if (!Connected() || !m.known) {
        ImGui::TextDisabled(Connected() ? "Asking the game..." : "Launch the game (Launch, on the toolbar), in a match.");
        ImGui::End();
        return;
    }
    if (!m.has) {
        ImGui::TextDisabled("The stage in use has no 3D model ([BGdef] model = file.glb).");
        ImGui::End();
        return;
    }
    SourceFile *sf = StageSource();
    bool dirty = g_mframe.ok ? g_mframe.dirty : m.dirty;
    bool defDirty = sf && sf->Dirty();

    // Header: the file, Save, Revert, pick
    std::string name = m.file.substr(m.file.find_last_of("/\\") + 1);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s%s", name.c_str(), dirty ? " *" : "");
    ImGui::SetItemTooltip("%s\n%zu nodes, %zu meshes, %zu materials, %zu animations, %zu lights", m.file.c_str(),
                          m.nodes.size(), m.meshes.size(), m.materials.size(), m.anims.size(), m.lights.size());
    ImGui::SameLine();
    ImGui::BeginDisabled(!dirty && !defDirty);
    bool save = ImGui::Button("Save");
    ImGuiIO &io = ImGui::GetIO();
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
        save = dirty || defDirty;
    ImGui::SetItemTooltip("Writes the model's changes into its file (a .bak of it the first time),\n"
                          "and the stage's def when its placement changed.");
    if (save) {
        if (dirty) Exec("local ok, err = debugModelSave() if not ok then error(err, 0) end");
        if (defDirty) SaveSource(*sf);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!dirty);
    if (ImGui::Button("Revert")) {
        SendRaw("debugModelRevert()");
        if (sf) ReloadStage(*sf, true);
    }
    ImGui::SetItemTooltip("The game reads the model's file again (the changes not saved are lost).");
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Checkbox("Pick on the Game window", &g_modelPick) && g_modelPick) g_stagePick = g_screenPick = false;
    ImGui::SetItemTooltip("On the Game window the mouse shows the node under it; a click selects it.");

    float lw = em * 6.5f;
    PlacementSection(lw);

    // The lists, and the inspector beside them (under them when narrow)
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
        else ImGui::TextDisabled("Pick a node, a material or a light in the lists, or a node on the Game window.");
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
    ImGui::End();
}

} // namespace app
