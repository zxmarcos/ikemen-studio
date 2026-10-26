// Analysis: a character's states read from its files as the game names them (debugCharFiles) and
// checked (Lint): states, actions, sounds and commands named that do not exist, sprites missing,
// controllers without a type or a trigger, statedefs given twice, states and commands nothing
// uses. The state files are read in the order the engine compiles them (st, cmd, stcommon,
// config.ini's common states): the first statedef of a number is the one used.
#include "app_internal.h"

#include <cfloat>
#include <cstdint>
#include <regex>
#include <set>

#include "cmd.h"
#include "cns.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"

namespace app {

namespace {

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

struct ParsedFile {
    std::string path;
    bool own = false; // the character's (st, cmd); else common (stcommon, config.ini's)
    CnsFile cns;
};

// A state named by a controller: in which state, where, how
struct StateRef {
    int from = 0;   // the state it is in (its number)
    bool fromLiteral = false;
    int to = 0;
    int file = 0, line = 0;
    std::string how; // "changestate", "p2stateno"...
    bool other = false; // given to another (a target, a helper)
};

// A variable written or read
struct VarUse {
    char kind; // 'v' var, 'f' fvar, 's' sysvar, 'S' sysfvar
    int index;
    bool write;
    int file, line, state;
    bool stateLiteral;
};

struct Finding {
    int level; // 0 error, 1 warning, 2 note
    int file, line;
    std::string text;
};

struct Model {
    int pn = 0;
    std::string def;
    size_t signature = 0;
    std::vector<ParsedFile> files;
    std::map<int, std::pair<int, int>> states; // number -> (file, index in it): the one used
    std::vector<StateRef> refs;
    std::vector<VarUse> vars;
    std::vector<Finding> findings;
    bool waiting = false; // for the character's files or sprites
};

Model g_model;

// The texts the model was made from, to make it again when one changes
size_t Signature(const std::vector<std::string> &paths, int pn) {
    size_t h = std::hash<std::string>()(CharFile(pn, "def"));
    for (const auto &p : paths) {
        const SourceFile &sf = LoadSource(p);
        h = h * 31 + std::hash<std::string>()(sf.text);
    }
    if (const SourceFile *air = CharFile(pn, "anim").empty() ? nullptr : &LoadSource(CharFile(pn, "anim")))
        h = h * 31 + std::hash<std::string>()(air->text);
    return h;
}

const std::vector<std::string> &FilesOf(int pn, const char *key) {
    static const std::vector<std::string> none;
    auto it = g_charFiles.find(pn);
    if (it == g_charFiles.end()) return none;
    auto f = it->second.find(key);
    return f == it->second.end() ? none : f->second;
}

// Where a controller names a state: (parameter, given to another)
const std::vector<std::pair<const char *, bool>> &StateParams(const std::string &type) {
    static const std::map<std::string, std::vector<std::pair<const char *, bool>>> table = {
        {"changestate", {{"value", false}}},
        {"selfstate", {{"value", false}}},
        {"targetstate", {{"value", true}}},
        {"hitdef", {{"p1stateno", false}, {"p2stateno", true}}},
        {"reversaldef", {{"p1stateno", false}, {"p2stateno", true}}},
        {"projectile", {{"p1stateno", false}, {"p2stateno", true}}},
        {"helper", {{"stateno", true}}},
    };
    static const std::vector<std::pair<const char *, bool>> none;
    auto it = table.find(type);
    return it == table.end() ? none : it->second;
}

// Where a controller names an action of the character's (an "F" prefix: fightfx's)
const std::vector<const char *> &ActionParams(const std::string &type) {
    static const std::map<std::string, std::vector<const char *>> table = {
        {"changeanim", {"value"}},
        {"changeanim2", {"value"}},
        {"explod", {"anim"}},
        {"projectile", {"projanim", "projhitanim", "projremanim", "projcancelanim"}},
        {"hitdef", {"sparkno", "guard.sparkno"}}, // only with an "S" prefix: fightfx's otherwise
        {"reversaldef", {"sparkno"}},
    };
    static const std::vector<const char *> none;
    auto it = table.find(type);
    return it == table.end() ? none : it->second;
}

const std::vector<const char *> &SoundParams(const std::string &type) {
    static const std::map<std::string, std::vector<const char *>> table = {
        {"playsnd", {"value"}},
        {"hitdef", {"hitsound", "guardsound"}}, // only with an "S" prefix
        {"reversaldef", {"hitsound"}},
    };
    static const std::vector<const char *> none;
    auto it = table.find(type);
    return it == table.end() ? none : it->second;
}

void FindVars(const std::string &text, int file, int line, int state, bool literal, std::vector<VarUse> &out) {
    static const std::regex re(R"((^|[^A-Za-z0-9_])(sysfvar|sysvar|fvar|var)\s*\(\s*(\d+)\s*\)(\s*:=)?)",
                               std::regex::icase);
    for (std::sregex_iterator it(text.begin(), text.end(), re), end; it != end; ++it) {
        std::string k = Lower((*it)[2].str());
        char kind = k == "var" ? 'v' : k == "fvar" ? 'f' : k == "sysvar" ? 's' : 'S';
        out.push_back({kind, atoi((*it)[3].str().c_str()), (*it)[4].matched, file, line, state, literal});
    }
}

// Variables a controller sets through its parameters (VarSet v = 3, var(3) = 1, VarRangeSet...)
void SetVars(const CnsCtrl &c, int file, int state, bool literal, std::vector<VarUse> &out) {
    static const std::set<std::string> setters = {"varset", "varadd", "varrandom", "parentvarset", "parentvaradd"};
    if (setters.count(c.type)) {
        for (const auto &p : c.params) {
            CnsNumbers n;
            char kind = p.key == "v" ? 'v' : p.key == "fv" ? 'f' : p.key == "sysvar" ? 's' : p.key == "sysfvar" ? 'S' : 0;
            if (kind && Cns_Numbers(p.value, n) && !n.prefix) out.push_back({kind, n.n[0], true, file, p.line, state, literal});
            std::smatch m;
            static const std::regex key(R"((sysfvar|sysvar|fvar|var)\s*\(\s*(\d+)\s*\))");
            if (std::regex_match(p.key, m, key)) {
                std::string k = m[1].str();
                char kd = k == "var" ? 'v' : k == "fvar" ? 'f' : k == "sysvar" ? 's' : 'S';
                out.push_back({kd, atoi(m[2].str().c_str()), true, file, p.line, state, literal});
            }
        }
    } else if (c.type == "varrangeset") {
        CnsNumbers a, b;
        const CnsParam *first = c.Param("first"), *last = c.Param("last");
        int lo = first && Cns_Numbers(first->value, a) ? a.n[0] : 0;
        bool floats = c.Param("fvalue") != nullptr;
        int hi = last && Cns_Numbers(last->value, b) ? b.n[0] : (floats ? 39 : 59);
        for (int i = lo; i <= hi && i - lo < 100; i++) out.push_back({floats ? 'f' : 'v', i, true, file, c.line, state, literal});
    }
}

void Build(Model &m, int pn, const std::vector<std::string> &paths, const std::vector<bool> &own) {
    m.files.clear();
    m.states.clear();
    m.refs.clear();
    m.vars.clear();
    m.findings.clear();
    auto find = [&](int level, int file, int line, std::string text) { m.findings.push_back({level, file, line, text}); };

    // The state files; the first statedef of a number is used
    for (size_t i = 0; i < paths.size(); i++) {
        ParsedFile pf;
        pf.path = paths[i];
        pf.own = own[i];
        Cns_Parse(LoadSource(paths[i]).text, Cns_IsZss(paths[i]), pf.cns);
        m.files.push_back(std::move(pf));
    }
    for (int f = 0; f < (int)m.files.size(); f++) {
        const auto &states = m.files[f].cns.states;
        for (int s = 0; s < (int)states.size(); s++) {
            const CnsState &st = states[s];
            if (!st.literal) continue;
            auto it = m.states.find(st.no);
            if (it == m.states.end())
                m.states[st.no] = {f, s};
            else if (m.files[f].own && m.files[it->second.first].own)
                find(1, f, st.line, "Statedef " + std::to_string(st.no) + " again: the first one (" +
                                        m.files[it->second.first].path + ":" +
                                        std::to_string(m.files[it->second.first].cns.states[it->second.second].line) +
                                        ") is the one used");
        }
    }

    // States the character only gives to others (custom states): in them, ChangeAnim and the
    // statedef's anim are the other's animations
    std::set<int> given, entered;
    for (int f = 0; f < (int)m.files.size(); f++)
        for (const CnsState &st : m.files[f].cns.states)
            for (const CnsCtrl &c : st.ctrls)
                for (const auto &[key, other] : StateParams(c.type)) {
                    const CnsParam *p = c.Param(key);
                    CnsNumbers n;
                    if (p && Cns_Numbers(p->value, n) && !n.prefix) (other && c.type != "helper" ? given : entered).insert(n.n[0]);
                }

    // The character's actions, sounds, sprites and commands
    std::set<int> actions;
    std::string airPath = CharFile(pn, "anim");
    const AirFile *air = airPath.empty() ? nullptr : &AirOf(LoadSource(airPath));
    if (air)
        for (const auto &a : air->actions) actions.insert(a.first);
    for (const auto &p : FilesOf(pn, "common.air")) {
        AirFile common;
        Air_Parse(LoadSource(p).text, common);
        for (const auto &a : common.actions) actions.insert(a.first);
    }
    const SndFile *snd = SndOf(pn);
    std::set<std::string> commands;
    for (const char *key : {"cmd", "common.cmd"})
        for (const auto &p : FilesOf(pn, key)) {
            CmdFile cmd;
            Cmd_Parse(LoadSource(p).text, cmd);
            for (const auto &c : cmd.commands) commands.insert(c.name);
        }
    std::map<std::string, std::pair<int, int>> commandUsed; // name -> where first named
    static const std::regex commandRe(R"re(command\s*!?=\s*"([^"]*)")re", std::regex::icase);

    for (int f = 0; f < (int)m.files.size(); f++) {
        const ParsedFile &pf = m.files[f];
        for (const CnsState &st : pf.cns.states) {
            bool used = st.literal && m.states[st.no] == std::make_pair(f, (int)(&st - pf.cns.states.data()));
            if (st.literal && !used) continue; // a statedef the engine does not use
            bool custom = st.literal && given.count(st.no) && !entered.count(st.no);
            if (const CnsParam *a = st.Param("anim"); a && pf.own && !custom) {
                CnsNumbers n;
                if (Cns_Numbers(a->value, n) && !n.prefix && n.n[0] >= 0 && air && !actions.count(n.n[0]))
                    find(0, f, a->line, "Statedef " + std::to_string(st.no) + ": action " + std::to_string(n.n[0]) + " is not in the AIR file");
            }
            for (const CnsCtrl &c : st.ctrls) {
                if (c.type.empty()) {
                    find(0, f, c.line, "A controller without a type");
                    continue;
                }
                if (!c.hasTrigger) find(0, f, c.line, "A " + c.type + " without trigger1 (it never runs)");
                for (const auto &[key, other] : StateParams(c.type)) {
                    const CnsParam *p = c.Param(key);
                    CnsNumbers n;
                    if (!p || !Cns_Numbers(p->value, n) || n.prefix || n.n.size() != 1) continue;
                    if (c.type == "hitdef" && std::string(key) == "p2stateno")
                        if (const CnsParam *g = c.Param("p2getp1state"); g && g->value == "0") continue; // the target's own
                    m.refs.push_back({st.no, st.literal, n.n[0], f, p->line, c.type == "hitdef" || c.type == "reversaldef" || c.type == "projectile" ? key : c.type, other});
                    if (pf.own && !m.states.count(n.n[0]))
                        find(0, f, p->line, c.type + " " + key + " = " + std::to_string(n.n[0]) + ": there is no state " + std::to_string(n.n[0]));
                }
                if (!pf.own) continue; // the common files name what every character has
                for (const char *key : ActionParams(c.type)) {
                    const CnsParam *p = c.Param(key);
                    CnsNumbers n;
                    if (!p || !Cns_Numbers(p->value, n) || n.n[0] < 0 || n.prefix == 'F') continue;
                    if ((c.type == "hitdef" || c.type == "reversaldef") && n.prefix != 'S') continue;
                    if (custom && c.type == "changeanim") continue; // the other's animation
                    // Guarded by its own trigger: SelfAnimExist(n)
                    if (c.triggers.find("selfanimexist(" + std::to_string(n.n[0]) + ")") != std::string::npos) continue;
                    if (air && !actions.count(n.n[0]))
                        find(0, f, p->line, c.type + " " + key + " = " + p->value + ": action " + std::to_string(n.n[0]) + " is not in the AIR file");
                }
                for (const char *key : SoundParams(c.type)) {
                    const CnsParam *p = c.Param(key);
                    CnsNumbers n;
                    if (!p || !Cns_Numbers(p->value, n) || n.n.size() < 2 || n.n[0] < 0 || n.prefix == 'F') continue;
                    if (c.type != "playsnd" && n.prefix != 'S') continue;
                    if (snd && Snd_Find(*snd, n.n[0], n.n[1]) < 0)
                        find(0, f, p->line, c.type + " " + key + " = " + p->value + ": sound " + std::to_string(n.n[0]) + "," +
                                                std::to_string(n.n[1]) + " is not in the SND file");
                }
            }
        }
        // Lines of code: commands and variables
        for (const CnsCode &code : pf.cns.code) {
            const CnsState &st = pf.cns.states[code.state];
            if (pf.own) {
                for (std::sregex_iterator it(code.text.begin(), code.text.end(), commandRe), end; it != end; ++it) {
                    std::string name = (*it)[1].str();
                    if (!commandUsed.count(name)) commandUsed[name] = {f, code.line};
                    if (!commands.empty() && !commands.count(name))
                        find(0, f, code.line, "command \"" + name + "\" is not in the CMD file");
                }
            }
            FindVars(code.text, f, code.line, st.no, st.literal && st.function.empty(), m.vars);
        }
        for (const CnsState &st : pf.cns.states)
            for (const CnsCtrl &c : st.ctrls) SetVars(c, f, st.no, st.literal && st.function.empty(), m.vars);
    }

    // Sprites the AIR file shows that are not in the SFF
    if (air)
        if (SpriteFile *sprites = SpritesOf(pn)) {
            int airFile = -1;
            for (int f = 0; f < (int)m.files.size(); f++)
                if (NormPath(m.files[f].path) == NormPath(airPath)) airFile = f;
            if (airFile < 0) {
                m.files.push_back({airPath, true, {}});
                airFile = (int)m.files.size() - 1;
            }
            for (const auto &[no, act] : air->actions)
                for (const auto &e : act.elems)
                    if (e.group >= 0 && Sff_FindSprite(sprites->doc, e.group, e.image) < 0)
                        find(1, airFile, e.line, "Action " + std::to_string(no) + ": sprite " + std::to_string(e.group) +
                                                     "," + std::to_string(e.image) + " is not in the SFF file");
        }

    // Notes: the character's states nothing names (200 to 4999: the engine enters the others),
    // commands no trigger uses
    std::set<int> named;
    for (const auto &r : m.refs) named.insert(r.to);
    for (const auto &[no, at] : m.states)
        if (m.files[at.first].own && no >= 200 && no < 5000 && !named.count(no))
            find(2, at.first, m.files[at.first].cns.states[at.second].line,
                 "Nothing changes to state " + std::to_string(no) + " by its number (an expression may)");
    for (const char *key : {"cmd"})
        for (const auto &p : FilesOf(pn, key)) {
            CmdFile cmd;
            Cmd_Parse(LoadSource(p).text, cmd);
            int file = -1;
            for (int f = 0; f < (int)m.files.size(); f++)
                if (NormPath(m.files[f].path) == NormPath(p)) file = f;
            if (file < 0) {
                m.files.push_back({p, true, {}});
                file = (int)m.files.size() - 1;
            }
            std::set<std::string> told;
            for (const auto &c : cmd.commands)
                if (!commandUsed.count(c.name) && told.insert(c.name).second && c.name.compare(0, 4, "hold") != 0)
                    find(2, file, c.line, "No trigger uses command \"" + c.name + "\"");
        }
    std::stable_sort(m.findings.begin(), m.findings.end(), [](const Finding &a, const Finding &b) { return a.level < b.level; });
}

// The model of a player's character, made again when one of its files changes
Model *ModelOf(int pn) {
    std::string def = CharFile(pn, "def");
    if (def.empty()) {
        AirPath(pn); // asks for the files
        return nullptr;
    }
    std::vector<std::string> paths;
    std::vector<bool> own;
    for (const char *key : {"st", "cmd", "stcommon", "common.states"})
        for (const auto &p : FilesOf(pn, key)) {
            if (std::find(paths.begin(), paths.end(), p) != paths.end()) continue;
            paths.push_back(p);
            own.push_back(std::string(key) == "st" || std::string(key) == "cmd");
        }
    size_t sig = Signature(paths, pn);
    if (g_model.pn != pn || g_model.def != def || g_model.signature != sig) {
        g_model.pn = pn;
        g_model.def = def;
        g_model.signature = sig;
        Build(g_model, pn, paths, own);
    }
    return &g_model;
}

void ShowLine(const Model &m, int file, int line) {
    if (file < 0 || file >= (int)m.files.size()) return;
    g_codeFile = m.files[file].path;
    g_codeLine = g_codeScrollTo = line;
    g_codeHold = true;
    if (ImGuiWindow *code = ImGui::FindWindowByName("Code")) ImGui::FocusWindow(code);
}

void LintTab(Model &m) {
    static bool show[3] = {true, true, false};
    int count[3] = {};
    for (const auto &f : m.findings) count[f.level]++;
    const char *names[3] = {"Errors", "Warnings", "Notes"};
    const ImVec4 colors[3] = {ImVec4(1, 0.45f, 0.45f, 1), ImVec4(1, 0.8f, 0.3f, 1), ImVec4(0.6f, 0.75f, 1, 1)};
    for (int i = 0; i < 3; i++) {
        char label[32];
        snprintf(label, sizeof(label), "%s (%d)", names[i], count[i]);
        if (i) ImGui::SameLine();
        ImGui::Checkbox(label, &show[i]);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Check again")) m.signature = 0;
    HelpMarker("Read from the character's state files as the engine compiles them (st, cmd, stcommon, then "
               "config.ini's common states; the first statedef of a number is the one used). Only numbers "
               "written as numbers are checked: an expression is not. Notes: states 200 to 4999 nothing "
               "changes to by number, commands no trigger names. Click a line to show it in Code.");
    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable;
    if (!ImGui::BeginTable("lint", 2, flags)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Where", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 12);
    ImGui::TableSetupColumn("What", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    for (size_t n = 0; n < m.findings.size(); n++) {
        const Finding &f = m.findings[n];
        if (!show[f.level]) continue;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        std::string where = m.files[f.file].path;
        where = where.substr(where.find_last_of("/\\") + 1) + ":" + std::to_string(f.line);
        ImGui::PushID((int)n);
        if (ImGui::Selectable(where.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) ShowLine(m, f.file, f.line);
        ImGui::PopID();
        ImGui::SetItemTooltip("%s:%d", m.files[f.file].path.c_str(), f.line);
        ImGui::TableNextColumn();
        ImGui::TextColored(colors[f.level], "%s", f.text.c_str());
    }
    ImGui::EndTable();
}

} // namespace

std::string VarName(const std::string &, char, int) { return ""; }

void AnalysisWindow() {
    // A window the saved layout does not know joins Code's dock
    if (ImGuiWindow *code = ImGui::FindWindowByName("Code"); code && code->DockId)
        ImGui::SetNextWindowDockID(code->DockId, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Analysis")) {
        ImGui::End();
        return;
    }
    const Entity *e = FindEntity(g_selId);
    if (!g_canTrace || !e) {
        ImGui::TextDisabled(!Connected() ? "Launch the game." : !g_canTrace ? "Needs an Ikemen GO with the debugger patch."
                                                                            : "Pick a player.");
        ImGui::End();
        return;
    }
    Model *m = ModelOf(e->pn);
    if (!m) {
        ImGui::TextDisabled("Waiting for the files of P%d's character...", e->pn);
        ImGui::End();
        return;
    }
    ImGui::Text("%s", m->def.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(P%d: %zu state files, %zu statedefs used)", e->pn, m->files.size(), m->states.size());
    if (ImGui::BeginTabBar("analysis")) {
        if (ImGui::BeginTabItem("Lint")) {
            LintTab(*m);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

} // namespace app
