// The stage's 3D model, in the Stage window: its animations (on, off, where they are), its nodes
// (shown or hidden, alone or with their children) and its lights (colour, intensity, range),
// changed while the match goes on (editor_model.go: debugModel and the like).
#include "app_internal.h"

#include <cfloat>

#include "imgui_stdlib.h"

namespace app {

namespace {

struct ModelView {
    bool known = false, has = false;
    std::string file;
    struct Anim {
        std::string name;
        int id = 0, loop = 0, loopCount = -1;
        bool enabled = false;
        float time = 0, duration = 0;
    };
    struct Node {
        std::string name, mesh;
        bool visible = true;
        int light = -1, parent = -1, layer = 0;
        std::vector<int> children;
    };
    struct Light {
        std::string name;
        int type = 0;
        float rgb[3] = {1, 1, 1}, intensity = 1, range = -1;
        bool animated = false;
    };
    std::vector<Anim> anims;
    std::vector<Node> nodes;
    std::vector<Light> lights;
};
ModelView g_model;
double g_modelAsked = -1;

const char *const kLightTypes[] = {"directional", "point", "spot"};

std::string NodeLabel(const ModelView::Node &n, int i) {
    std::string label = n.name.empty() ? "node " + std::to_string(i) : n.name;
    if (!n.mesh.empty()) label += "  (" + n.mesh + ")";
    if (n.light >= 0 && n.light < (int)g_model.lights.size()) {
        const auto &l = g_model.lights[n.light];
        label += "  light: " + (l.name.empty() ? std::to_string(n.light) : l.name);
    }
    return label;
}

void NodeTree(int i, const std::string &filter) {
    ModelView::Node &n = g_model.nodes[i];
    ImGui::PushID(i);
    bool visible = n.visible;
    if (ImGui::Checkbox("##v", &visible)) {
        bool children = ImGui::GetIO().KeyShift;
        SendRaw("debugModelNode(" + std::to_string(i) + ", " + (visible ? "true" : "false") + (children ? ", true)" : ")"));
        n.visible = visible; // until the next answer
    }
    ImGui::SetItemTooltip(n.mesh.empty() ? "No mesh of its own: hiding it changes nothing (Shift: its children too)"
                                         : "Shown or hidden until the round starts again (Shift: its children too)");
    ImGui::SameLine();
    std::string label = NodeLabel(n, i);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | (n.children.empty() ? ImGuiTreeNodeFlags_Leaf : 0);
    if (!filter.empty()) flags |= ImGuiTreeNodeFlags_DefaultOpen;
    if (!n.visible) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    bool open = ImGui::TreeNodeEx("##n", flags, "%s", label.c_str());
    if (!n.visible) ImGui::PopStyleColor();
    if (n.layer) ImGui::SetItemTooltip("layerNumber %d", n.layer);
    if (open) {
        for (int c : n.children) NodeTree(c, filter);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

// A node, or one under it, whose label has the filter
bool Matches(int i, const std::string &filter) {
    std::string low = NodeLabel(g_model.nodes[i], i);
    std::transform(low.begin(), low.end(), low.begin(), ::tolower);
    if (low.find(filter) != std::string::npos) return true;
    for (int c : g_model.nodes[i].children)
        if (Matches(c, filter)) return true;
    return false;
}

} // namespace

void OnModel(const Json &j) {
    ModelView &m = g_model;
    m.known = true;
    m.has = !j["none"].truthy();
    m.file = j["file"].str();
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
        m.anims.push_back(v);
    }
    m.nodes.clear();
    for (const auto &n : j["nodes"].items) {
        ModelView::Node v;
        v.name = n["name"].str();
        v.mesh = n["mesh"].str();
        v.visible = n["visible"].truthy();
        v.light = n["light"].i(-1);
        v.parent = n["parent"].i(-1);
        v.layer = n["layer"].i();
        m.nodes.push_back(v);
    }
    for (int i = 0; i < (int)m.nodes.size(); i++)
        if (int p = m.nodes[i].parent; p >= 0 && p < (int)m.nodes.size()) m.nodes[p].children.push_back(i);
    // While a light's value is dragged here, this window's values stay
    if (ImGui::IsAnyItemActive() && j["lights"].items.size() == m.lights.size()) return;
    m.lights.clear();
    for (const auto &l : j["lights"].items) {
        ModelView::Light v;
        v.name = l["name"].str();
        v.type = l["type"].i();
        v.rgb[0] = (float)l["r"].num(1), v.rgb[1] = (float)l["g"].num(1), v.rgb[2] = (float)l["b"].num(1);
        v.intensity = (float)l["intensity"].num(1);
        v.range = (float)l["range"].num(-1);
        v.animated = l["animated"].truthy();
        m.lights.push_back(v);
    }
}

void ModelSection(bool stageHasModel) {
    ModelView &m = g_model;
    if (!stageHasModel && !(m.known && m.has)) return;
    if (!ImGui::CollapsingHeader("3D model")) return;
    // Asked again while it is shown
    if (Connected() && g_canTrace && ImGui::GetTime() - g_modelAsked > 0.3) {
        g_modelAsked = ImGui::GetTime();
        SendRaw("ikdbg.model()");
    }
    if (!m.known) {
        ImGui::TextDisabled("Asking the game...");
        return;
    }
    if (!m.has) {
        ImGui::TextDisabled("The stage in use has no 3D model.");
        return;
    }
    float em = ImGui::GetFontSize();
    ImGui::TextDisabled("%s: %zu nodes, %zu animations, %zu lights", m.file.c_str(), m.nodes.size(), m.anims.size(),
                        m.lights.size());
    if (!ImGui::BeginChild("model", ImVec2(0, em * 15), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY)) {
        ImGui::EndChild();
        return;
    }
    if (ImGui::BeginTabBar("modeltabs")) {
        if (ImGui::BeginTabItem("Animations")) {
            if (m.anims.empty()) ImGui::TextDisabled("None.");
            else if (ImGui::BeginTable("anims", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("On");
                ImGui::TableSetupColumn("Animation", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, em * 9);
                ImGui::TableSetupColumn("Loops");
                ImGui::TableHeadersRow();
                for (int i = 0; i < (int)m.anims.size(); i++) {
                    ModelView::Anim &a = m.anims[i];
                    ImGui::PushID(i);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    if (ImGui::Checkbox("##on", &a.enabled))
                        SendRaw("debugModelAnim(" + std::to_string(i) + ", " + (a.enabled ? "true)" : "false)"));
                    ImGui::SetItemTooltip("On: it starts again from its start. Off: what it moves goes back to rest.");
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", a.name.empty() ? ("animation " + std::to_string(i)).c_str() : a.name.c_str());
                    if (a.id) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("id %d", a.id);
                    }
                    ImGui::TableNextColumn();
                    char label[48];
                    snprintf(label, sizeof(label), "%.2f / %.2f s", a.time, a.duration);
                    ImGui::ProgressBar(a.duration > 0 ? a.time / a.duration : 0, ImVec2(-FLT_MIN, 0), label);
                    ImGui::TableNextColumn();
                    if (a.loopCount < 0) ImGui::Text("%d", a.loop);
                    else ImGui::Text("%d of %d", a.loop, a.loopCount);
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Nodes")) {
            static std::string filter;
            ImGui::SetNextItemWidth(em * 10);
            ImGui::InputTextWithHint("##filter", "filter", &filter);
            ImGui::SameLine();
            ImGui::TextDisabled("Shift+click: a node and its children");
            std::string low = filter;
            std::transform(low.begin(), low.end(), low.begin(), ::tolower);
            if (ImGui::BeginChild("tree")) {
                for (int i = 0; i < (int)m.nodes.size(); i++)
                    if (m.nodes[i].parent < 0 && (low.empty() || Matches(i, low))) NodeTree(i, low);
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Lights")) {
            if (m.lights.empty()) ImGui::TextDisabled("None (KHR_lights_punctual).");
            for (int i = 0; i < (int)m.lights.size(); i++) {
                ModelView::Light &l = m.lights[i];
                ImGui::PushID(i);
                ImGui::SeparatorText((l.name.empty() ? "light " + std::to_string(i) : l.name).c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("%s%s", kLightTypes[std::clamp(l.type, 0, 2)], l.animated ? ", animated: its animation wins" : "");
                bool changed = ImGui::ColorEdit3("colour", l.rgb, ImGuiColorEditFlags_Float);
                ImGui::SetNextItemWidth(em * 8);
                changed |= ImGui::DragFloat("intensity", &l.intensity, 0.05f, 0, FLT_MAX, "%.2f");
                if (l.type != 0) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(em * 8);
                    changed |= ImGui::DragFloat("range", &l.range, 0.5f, -1, FLT_MAX, l.range < 0 ? "none" : "%.1f");
                }
                if (changed) {
                    char lua[160];
                    snprintf(lua, sizeof(lua), "debugModelLight(%d, %g, %g, %g, %g, %g)", i, l.rgb[0], l.rgb[1], l.rgb[2],
                             l.intensity, l.range);
                    SendRaw(lua);
                    g_modelAsked = ImGui::GetTime(); // the values here stay while dragging
                }
                ImGui::PopID();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
}

} // namespace app
