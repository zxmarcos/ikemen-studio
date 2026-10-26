// Create: new things made from a form, written into the files and applied. Move: an attack's
// statedef as KFM writes its own (src/make.cpp), at the end of the character's first state file,
// and a ChangeState on its command at the top of [Statedef -1].
#include "app_internal.h"

#include <cfloat>

#include "cns.h"
#include "imgui_stdlib.h"
#include "make.h"

namespace app {

namespace {

enum { kTabMove, kTabs };
int g_tabAsked = -1;

struct MoveForm {
    std::string def; // the character it was set up for
    MakeMove m;
    int strength = 0;
    std::string command; // "" none
    bool ctrl = true;
    int made = -1; // the state made last, to try
    std::string madeIn;
    int madeLine = 0;
    int copyFrom = 200; // an action to copy as the move's when it has none
};
MoveForm g_move;

// The first element (1-based) with Clsn1 boxes, 0 none; sparkY: the middle of its first box
int FirstHitElem(const AirAction &a, int *sparkY) {
    for (size_t i = 0; i < a.elems.size(); i++)
        if (!a.elems[i].clsn[0].empty()) {
            const AirBox &b = a.elems[i].clsn[0][0];
            *sparkY = (b.y1 + b.y2) / 2;
            return (int)i + 1;
        }
    return 0;
}

int RootId(int pn) {
    for (const Entity &e : g_ents)
        if (e.pn == pn && !e.helper) return e.id;
    return 0;
}

// A sprite of the player's, at most size high
void SpriteThumb(int pn, int group, int image, float size) {
    SpriteFile *spf = SpritesOf(pn);
    int i = spf ? Sff_FindSprite(spf->doc, group, image) : -1;
    if (i < 0) return;
    const SffSprite &s = spf->doc.sprites[i];
    if (GLuint tex = SpriteTexture(*spf, i)) {
        float k = std::min(1.0f, size / std::max(1, s.h));
        ImVec2 p = ImGui::GetCursorScreenPos(), q(p.x + s.w * k, p.y + s.h * k);
        Checkerboard(ImGui::GetWindowDrawList(), p, q);
        ImGui::Image(TexRef(tex), ImVec2(s.w * k, s.h * k));
    }
}

void MoveTab() {
    const Entity *e = FindEntity(g_selId);
    if (!e) {
        ImGui::TextDisabled("Pick a player: the move is made for its character.");
        return;
    }
    int pn = e->pn;
    std::string def = CharFile(pn, "def");
    const std::vector<std::string> *st = nullptr;
    if (auto it = g_charFiles.find(pn); it != g_charFiles.end())
        if (auto f = it->second.find("st"); f != it->second.end() && !f->second.empty()) st = &f->second;
    if (def.empty() || !st) {
        AirPath(pn); // asks for the files
        ImGui::TextDisabled(g_canTrace ? "Waiting for the files of P%d..." : "Needs the patched Ikemen GO.", pn);
        return;
    }
    MoveForm &f = g_move;
    MakeMove &m = f.m;
    if (f.def != def) {
        f = MoveForm();
        f.def = def;
        while (!StatedefFile(pn, m.state).empty()) m.state += 10;
        m.action = m.state;
    }
    float em = ImGui::GetFontSize();
    ImGui::TextDisabled("For P%d's %s, at the end of %s", pn, def.c_str(), (*st)[0].c_str());

    ImGui::SetNextItemWidth(em * 14);
    ImGui::InputText("name", &m.name);
    ImGui::SetNextItemWidth(em * 6);
    ImGui::InputInt("state", &m.state, 0);
    std::string taken = StatedefFile(pn, m.state);
    if (!taken.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0.45f, 0.45f, 1), "exists already (%s)", taken.c_str());
    } else if (m.state < 200 || m.state >= 5000) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "a common state's number: it would take its place");
    }
    ImGui::SetNextItemWidth(em * 8);
    ImGui::Combo("done", &m.kind, "standing\0crouching\0in the air\0");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(em * 10);
    ImGui::Combo("kind", &m.klass, "normal attack\0special attack\0hyper (1000 power)\0");
    ImGui::SetNextItemWidth(em * 8);
    if (ImGui::Combo("strength", &f.strength, "light\0medium\0hard\0")) m.hit = kMakeHits[f.strength];
    ImGui::SetItemTooltip("Sets the hit's values (after KFM's punches); each stays editable below.");
    if (ImGui::CollapsingHeader("The hit (HitDef)")) {
        MakeHit &h = m.hit;
        ImGui::PushItemWidth(em * 8);
        ImGui::InputInt2("damage, guard damage", &h.damage);
        ImGui::InputInt2("pausetime (attacker, target)", &h.pause1);
        ImGui::InputInt("sparkno", &h.spark, 0);
        ImGui::InputInt("ground.slidetime", &h.slideTime, 0);
        ImGui::InputInt("ground.hittime", &h.hitTime, 0);
        ImGui::InputFloat("ground.velocity", &h.velocity, 0, 0, "%g");
        ImGui::InputFloat2("air.velocity", &h.airVx, "%g");
        ImGui::InputInt("air.hittime", &h.airHitTime, 0);
        ImGui::Combo("animtype", &h.animType, "Light\0Medium\0Hard\0");
        ImGui::PopItemWidth();
    }

    // The action: its first element with a Clsn1 box arms the HitDef
    ImGui::SeparatorText("Animation");
    ImGui::SetNextItemWidth(em * 6);
    ImGui::InputInt("action", &m.action, 0);
    ImGui::SameLine();
    if (ImGui::SmallButton("the state's number")) m.action = m.state;
    std::string airPath = CharFile(pn, "anim");
    SourceFile *air = airPath.empty() ? nullptr : &LoadSource(airPath);
    const AirFile *af = air ? &AirOf(*air) : nullptr;
    auto act = af ? af->actions.find(m.action) : std::map<int, AirAction>::const_iterator();
    m.hitElem = 0;
    m.sparkY = -70;
    if (!af || act == af->actions.end()) {
        ImGui::TextColored(ImVec4(1, 0.45f, 0.45f, 1), "There is no action %d.", m.action);
        ImGui::SetNextItemWidth(em * 6);
        ImGui::InputInt("##copy", &f.copyFrom, 0);
        ImGui::SameLine();
        auto from = af ? af->actions.find(f.copyFrom) : std::map<int, AirAction>::const_iterator();
        ImGui::BeginDisabled(!af || from == af->actions.end());
        char label[64];
        snprintf(label, sizeof(label), "Copy it as action %d", m.action);
        if (ImGui::Button(label) && Air_CopyAction(air->text, from->second, m.action)) {
            air->version++;
            ApplyAnims(*air);
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Then draw its boxes in the Animation window (Show on the player).");
    } else {
        const AirAction &a = act->second;
        m.hitElem = FirstHitElem(a, &m.sparkY);
        int ticks = 0;
        bool endless = false;
        for (const auto &el : a.elems) el.time < 0 ? endless = true : ticks += el.time;
        const AirElem &shown = a.elems[m.hitElem ? m.hitElem - 1 : 0];
        SpriteThumb(pn, shown.group, shown.image, em * 6);
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::Text("%zu elements, %d ticks%s", a.elems.size(), ticks, endless ? " and one that lasts" : "");
        if (m.hitElem) ImGui::Text("hits from element %d (its first Clsn1 box)", m.hitElem);
        else ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "no Clsn1 box: it hits nothing until one is drawn");
        if (ImGui::SmallButton("Animation window")) ShowWindow("Animation");
        ImGui::EndGroup();
    }
    ImGui::Checkbox("a sound at the start", &m.sound);
    if (m.sound) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(em * 7);
        ImGui::InputInt2("##snd", m.snd);
        ImGui::SameLine();
        const SndFile *snd = SndOf(pn);
        int i = snd ? Snd_Find(*snd, m.snd[0], m.snd[1]) : -1;
        ImGui::BeginDisabled(i < 0);
        if (ImGui::SmallButton("Play")) PlayWav(snd->sounds[i].wav);
        ImGui::EndDisabled();
        if (snd && i < 0) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 0.45f, 0.45f, 1), "not in the SND file");
        }
    }

    // The command that starts it
    ImGui::SeparatorText("Command");
    std::vector<std::string> names = CommandNames(pn);
    ImGui::SetNextItemWidth(em * 10);
    if (ImGui::BeginCombo("##command", f.command.empty() ? "none" : f.command.c_str(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::Selectable("none", f.command.empty())) f.command.clear();
        for (const auto &n : names)
            if (ImGui::Selectable(n.c_str(), f.command == n)) f.command = n;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Checkbox("with control", &f.ctrl);
    ImGui::SameLine();
    if (ImGui::SmallButton("Commands window")) ShowWindow("Commands");
    ImGui::TextDisabled(f.command.empty() ? "Without one, try it with the button below or SelfState."
                                          : "A ChangeState on it goes at the top of [Statedef -1]%s.",
                        m.klass == 2 ? ", with power >= 1000" : "");

    ImGui::Separator();
    ImGui::BeginDisabled(!taken.empty() || m.state < 0 || g_applying);
    if (ImGui::Button("Create the move")) {
        // At the end of the first state file, after one blank line
        SourceFile &sf = LoadSource((*st)[0]);
        std::string text = sf.text;
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
        int line = 3 + (text.empty() ? 0 : (int)std::count(text.begin(), text.end(), '\n') + 2);
        text += (text.empty() ? "" : "\n\n") + Make_MoveState(m, Cns_IsZss(sf.path));
        sf.text = text;
        sf.version++;
        std::string where = sf.path;
        if (!f.command.empty()) {
            int entry = 0;
            std::string at = AddCommandEntry(pn, f.command, m.state, m.kind + 1, f.ctrl, m.klass == 2 ? "power >= 1000" : "", &entry);
            if (!at.empty()) where += ", a ChangeState on \"" + f.command + "\" in " + at;
        }
        Log(LogLine::Info, "State " + std::to_string(m.state) + " (" + m.name + ") written in " + where + "; saved and applied.");
        SaveAndApply();
        f.made = m.state;
        f.madeIn = sf.path;
        f.madeLine = line;
        g_codeFile = sf.path;
        g_codeLine = g_codeScrollTo = line;
        g_codeHold = true;
    }
    ImGui::EndDisabled();
    if (f.made >= 0) {
        ImGui::SameLine();
        char label[64];
        snprintf(label, sizeof(label), "Try state %d on P%d", f.made, pn);
        ImGui::BeginDisabled(!Connected() || g_applying || !RootId(pn));
        if (ImGui::Button(label)) ExecOn(RootId(pn), "selfState(" + std::to_string(f.made) + ")");
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton("Show in Code")) {
            g_codeFile = f.madeIn;
            g_codeLine = g_codeScrollTo = f.madeLine;
            g_codeHold = true;
        }
    }
    if (ImGui::CollapsingHeader("The text it writes")) {
        std::string text = Make_MoveState(m, Cns_IsZss((*st)[0]));
        ImGui::InputTextMultiline("##text", &text, ImVec2(-FLT_MIN, em * 18), ImGuiInputTextFlags_ReadOnly);
    }
}

} // namespace

void CreateMenu() {
    if (ImGui::MenuItem("A move...")) g_tabAsked = kTabMove, ShowWindow("Create");
    ImGui::SetItemTooltip("An attack's state for the selected player's character, and the command that starts it.");
}

void CreateToolWindow() {
    if (!BeginTool("Create")) return;
    if (ImGui::BeginTabBar("create")) {
        static const char *tabs[kTabs] = {"Move"};
        for (int i = 0; i < kTabs; i++) {
            if (!ImGui::BeginTabItem(tabs[i], nullptr, g_tabAsked == i ? ImGuiTabItemFlags_SetSelected : 0)) continue;
            if (i == kTabMove) MoveTab();
            ImGui::EndTabItem();
        }
        g_tabAsked = -1;
        ImGui::EndTabBar();
    }
    ImGui::End();
}

} // namespace app
