// The project: what is being worked on, one def at a time. Each kind keeps its own, saved with the
// settings, and the workspace says which is in use: the editors have one answer to "what is open?",
// which a click on a fighter does not change.
#include "app_internal.h"

#include <algorithm>
#include <map>

#include "imgui_internal.h"

namespace app {

namespace {

std::string g_project[kProjectKinds]; // opened; "": what the match uses
std::vector<ProjectRecent> g_recent;
const size_t kRecents = 4;
const char kLetters[] = "csm"; // a kind in the settings

int KindOfLetter(char letter) {
    const char *at = strchr(kLetters, letter);
    return at && letter ? (int)(at - kLetters) : -1;
}

const char *KindIcon(ProjectKind kind) {
    return kind == kProjectStage ? ICON_STAGE : kind == kProjectScreenpack ? ICON_DISPLAY : ICON_PERSON;
}

void AddRecent(ProjectKind kind, const std::string &def) {
    auto same = [&](const ProjectRecent &r) { return r.kind == kind && NormPath(r.def) == NormPath(def); };
    if (!g_recent.empty() && same(g_recent[0])) return;
    g_recent.erase(std::remove_if(g_recent.begin(), g_recent.end(), same), g_recent.end());
    g_recent.insert(g_recent.begin(), {kind, def});
    if (g_recent.size() > kRecents) g_recent.pop_back();
}

// Already on its way into P1's place: a double click would start the match again twice
bool GoingIn(const std::string &name) {
    auto want = g_swapWanted.find(1);
    return want != g_swapWanted.end() && want->second.first == name;
}

// Where it is in the game; "" the game not running, or a character still going in
std::string ProjectWhere(ProjectKind kind, const std::string &def) {
    if (!Connected()) return "";
    if (kind == kProjectStage) return NormPath(def) == NormPath(g_stageDef) ? "in the match" : "not in the match";
    if (kind == kProjectScreenpack) return NormPath(def) == NormPath(MotifDef()) ? "in the game" : "not in the game";
    if (!g_swapWanted.empty()) return "";
    int pn = PlayerOf(def);
    return pn ? "P" + std::to_string(pn) : "not in the match";
}

} // namespace

const std::vector<ProjectRecent> &ProjectRecents() { return g_recent; }

std::string ProjectDef(ProjectKind kind) {
    const std::string &opened = g_project[kind];
    if (!opened.empty() && GameFileExists(opened)) return opened;
    if (kind == kProjectStage) return Connected() && !g_stageDef.empty() ? g_stageDef : SettingsStageDef();
    if (kind == kProjectScreenpack) return ScreenpackDef();
    std::string p1 = Connected() ? CharFile(1, "def") : "";
    return p1.empty() ? CharDefOf(g_settings.p1) : p1;
}

std::string ProjectName(ProjectKind kind, const std::string &def) {
    if (kind == kProjectCharacter) return CharNameOf(def);
    if (kind == kProjectStage) return StageNameOf(def);
    // data/ikemen1/system.def: its folder
    size_t slash = def.find_last_of("/\\");
    std::string dir = slash == std::string::npos ? "" : def.substr(0, slash);
    return dir.substr(dir.find_last_of("/\\") + 1);
}

void ProjectOpen(ProjectKind kind, const std::string &def, bool enter) {
    if (def.empty()) return;
    g_project[kind] = def;
    AddRecent(kind, def);
    ImGui::MarkIniSettingsDirty();
    AskProjectWorkspace(kind);
    if (!enter) return;
    if (kind == kProjectStage) {
        PlayStage(def);
    } else if (kind == kProjectCharacter && !Connected()) {
        g_settings.p1 = CharNameOf(def);
    } else if (kind == kProjectCharacter && !PlayerOf(def) && !GoingIn(CharNameOf(def))) {
        g_settings.p1 = CharNameOf(def);
        SwapChar(1, g_settings.p1);
    }
}

bool OfProject(int pn) {
    std::string def = pn > 0 ? CharFile(pn, "def") : "";
    return !def.empty() && NormPath(def) == NormPath(ProjectDef(kProjectCharacter));
}

bool SelectedOfProject() {
    const Entity *e = FindEntity(g_selId);
    const FrameEntity *fe = FindFrameEntity(g_selId);
    return e && (OfProject(e->pn) || (fe && fe->action >= 0 && OfProject(fe->animPn)));
}

int ProjectFighter(int pn) {
    if (SelectedOfProject()) return g_selId;
    for (const Entity &e : g_ents)
        if (e.pn == pn && !e.helper) return e.id;
    return -1;
}

void ProjectBar() {
    ProjectKind kind = CurrentProject();
    std::string def = ProjectDef(kind);
    float em = ImGui::GetFontSize();
    std::string name = def.empty() ? "Nothing open" : ProjectName(kind, def);
    if (ImGui::Button((std::string(KindIcon(kind)) + "  " + name + "  " ICON_CARET "###project").c_str())) ImGui::OpenPopup("project");
    ImGui::SetItemTooltip("%s\nThe project: what the editors work on, and what Package takes. Clicking a fighter does not change it.",
                          def.empty() ? "Nothing open" : def.c_str());
    if (std::string where = def.empty() ? "" : ProjectWhere(kind, def); !where.empty()) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        if (where.compare(0, 3, "not") == 0) ImGui::TextColored(Ink(kYellow), "%s", where.c_str());
        else ImGui::TextDisabled("%s", where.c_str());
    }
    if (!ImGui::BeginPopup("project")) return;
    static std::vector<std::string> names;
    static std::map<std::string, std::string> defs; // name -> def
    if (ImGui::IsWindowAppearing()) {
        names.clear(), defs.clear();
        if (kind != kProjectScreenpack)
            for (const std::string &n : ListDefs(kind == kProjectStage)) {
                std::string shown = kind == kProjectStage ? StageNameOf(n) : n;
                names.push_back(shown);
                defs[shown] = kind == kProjectStage ? n : CharDefOf(n);
            }
    }
    bool any = false;
    for (const ProjectRecent &r : g_recent) {
        if (r.kind != kind || !GameFileExists(r.def)) continue;
        any = true;
        std::string go = r.def; // opening it moves it in the list
        ImGui::PushID(go.c_str());
        bool picked = ImGui::Selectable(ProjectName(kind, go).c_str(), NormPath(go) == NormPath(def));
        ImGui::SetItemTooltip("%s", go.c_str());
        ImGui::PopID();
        if (picked) {
            ProjectOpen(kind, go);
            ImGui::CloseCurrentPopup();
            break;
        }
    }
    if (any && !names.empty()) ImGui::Separator();
    if (!names.empty()) {
        ImGui::SetNextItemWidth(em * 16);
        std::string picked;
        if (NameCombo("##open", "Open another...", names, picked)) {
            ProjectOpen(kind, defs[picked]);
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndPopup();
}

void ProjectSettingsLine(const std::string &key, const std::string &value) {
    if (value.size() < 3 || value[1] != '|') return;
    int kind = KindOfLetter(value[0]);
    if (kind < 0) return;
    if (key == "Recent" && g_recent.size() < kRecents) g_recent.push_back({(ProjectKind)kind, value.substr(2)});
    else if (key == "Project") g_project[kind] = value.substr(2);
}

void ProjectSettingsWrite(ImGuiTextBuffer *buf) {
    for (int k = 0; k < kProjectKinds; k++)
        if (!g_project[k].empty()) buf->appendf("Project=%c|%s\n", kLetters[k], g_project[k].c_str());
    for (const ProjectRecent &r : g_recent) buf->appendf("Recent=%c|%s\n", kLetters[r.kind], r.def.c_str());
}

} // namespace app
