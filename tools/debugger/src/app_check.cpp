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

// Variables' names, by character: "def|kind|index" -> name
std::map<std::string, std::string> g_varNames;

std::string VarKey(const std::string &def, char kind, int index) {
    return NormPath(def) + "|" + kind + "|" + std::to_string(index);
}

const char *KindName(char kind) { return kind == 'v' ? "var" : kind == 'f' ? "fvar" : kind == 's' ? "sysvar" : "sysfvar"; }

// Where it is used, as small buttons: its state's number; a click shows the line
void UseButtons(const Model &m, const std::vector<const VarUse *> &uses, const char *id) {
    float right = ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX();
    for (size_t i = 0; i < uses.size(); i++) {
        const VarUse &u = *uses[i];
        char label[32];
        snprintf(label, sizeof(label), u.stateLiteral ? "%d" : "fn", u.state);
        float w = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2;
        if (i > 0) {
            ImGui::SameLine(0, 2);
            if (ImGui::GetCursorPosX() + w > right) ImGui::NewLine();
        }
        ImGui::PushID(id);
        ImGui::PushID((int)i);
        if (ImGui::SmallButton(label)) ShowLine(m, u.file, u.line);
        ImGui::PopID();
        ImGui::PopID();
        std::string file = m.files[u.file].path;
        ImGui::SetItemTooltip("%s:%d%s", file.c_str(), u.line, u.stateLiteral ? "" : " (a function, or outside states)");
    }
}

void VariablesTab(Model &m, const Entity &e) {
    static int kindIdx = 0;
    static bool onlyUsed = true;
    const char kinds[] = {'v', 'f', 's', 'S'};
    const int sizes[] = {60, 40, 5, 5};
    for (int i = 0; i < 4; i++) {
        if (i) ImGui::SameLine();
        ImGui::RadioButton(KindName(kinds[i]), &kindIdx, i);
    }
    ImGui::SameLine();
    ImGui::Checkbox("Only used ones", &onlyUsed);
    HelpMarker("Where each variable is written (VarSet, VarAdd, VarRandom, VarRangeSet, ParentVarSet/Add, :=) "
               "and read (any expression), by state; a click shows the line. Names are kept per character "
               "and shown in the Inspector. Now: the selected entity's value.");
    char kind = kinds[kindIdx];
    if (kind == 'v' || kind == 'f') Watch(e.id); // its var/fvar come with the snapshots
    const std::vector<double> &now = kind == 'v' ? e.vars : e.fvars;

    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable;
    if (!ImGui::BeginTable("vars", 5, flags)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 4);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 8);
    ImGui::TableSetupColumn("Now", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 4);
    ImGui::TableSetupColumn("Written in", ImGuiTableColumnFlags_WidthStretch, 1);
    ImGui::TableSetupColumn("Read in", ImGuiTableColumnFlags_WidthStretch, 1);
    ImGui::TableHeadersRow();
    for (int i = 0; i < sizes[kindIdx]; i++) {
        std::vector<const VarUse *> writes, reads;
        for (const auto &u : m.vars)
            if (u.kind == kind && u.index == i) (u.write ? writes : reads).push_back(&u);
        std::string &name = g_varNames[VarKey(m.def, kind, i)];
        if (onlyUsed && writes.empty() && reads.empty() && name.empty()) continue;
        ImGui::PushID(i);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("%s(%d)", KindName(kind), i);
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##name", "name", &name);
        ImGui::TableNextColumn();
        if (i < (int)now.size())
            ImGui::Text(kind == 'f' ? "%.3f" : "%.0f", now[i]);
        else
            ImGui::TextDisabled("-");
        ImGui::TableNextColumn();
        if (writes.empty()) ImGui::TextDisabled(reads.empty() ? "" : "never (read only)");
        UseButtons(m, writes, "w");
        ImGui::TableNextColumn();
        if (reads.empty()) ImGui::TextDisabled(writes.empty() ? "" : "never (written only)");
        UseButtons(m, reads, "r");
        ImGui::PopID();
    }
    ImGui::EndTable();
}

// A line, dashed when asked, along a cubic curve
void Curve(ImDrawList *draw, ImVec2 a, ImVec2 b, ImU32 col, float thick, bool dashed) {
    float dx = (b.x - a.x) * 0.5f;
    ImVec2 c1(a.x + dx, a.y), c2(b.x - dx, b.y);
    if (!dashed) {
        draw->AddBezierCubic(a, c1, c2, b, col, thick);
        return;
    }
    const int steps = 24;
    ImVec2 prev = a;
    for (int i = 1; i <= steps; i++) {
        ImVec2 p = ImBezierCubicCalc(a, c1, c2, b, (float)i / steps);
        if (i % 2) draw->AddLine(prev, p, col, thick);
        prev = p;
    }
}

// The states around one: those that change to it on the left, those it changes to on the right
// (by numbers written in ChangeState, SelfState, HitDef p1stateno...; dashed: given to another),
// a few steps each way. The path the player came by is green; states that do not exist red.
void GraphTab(Model &m, const Entity &e) {
    static int focus = 0, depth = 2;
    static bool follow = true;
    float em = ImGui::GetFontSize();
    ImGui::Checkbox("Follow the player", &follow);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(em * 5);
    if (ImGui::InputInt("State", &focus, 0)) follow = false;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(em * 6);
    ImGui::SliderInt("##depth", &depth, 1, 4, "%d steps");
    ImGui::SameLine();
    HelpMarker("Left: the states that change to it; right: those it changes to, by numbers written in "
               "ChangeState, SelfState, HitDef/ReversalDef/Projectile p1stateno (dashed: p2stateno, "
               "TargetState, Helper: given to another). Green: the path the player came by; red: a state "
               "that does not exist. Click a state to center on it, double-click to show it in Code.");
    if (follow) focus = e.st;

    std::map<int, std::vector<const StateRef *>> out, in;
    for (const auto &r : m.refs)
        if (r.fromLiteral && r.from != r.to) {
            out[r.from].push_back(&r);
            in[r.to].push_back(&r);
        }
    // Columns: -depth .. depth
    std::map<int, int> col;
    std::vector<std::vector<int>> cols(2 * depth + 1);
    col[focus] = 0;
    cols[depth].push_back(focus);
    for (int dir : {1, -1}) {
        std::vector<int> frontier = {focus};
        for (int d = 1; d <= depth; d++) {
            std::vector<int> next;
            for (int s : frontier)
                for (const StateRef *r : (dir > 0 ? out : in)[s]) {
                    int t = dir > 0 ? r->to : r->from;
                    if (col.count(t)) continue;
                    col[t] = dir * d;
                    next.push_back(t);
                }
            std::sort(next.begin(), next.end());
            cols[depth + dir * d] = next;
            frontier = next;
        }
    }
    // The path the player came by: its last states, oldest first
    std::vector<int> path;
    if (auto h = g_history.find(e.id); h != g_history.end())
        for (auto r = h->second.rbegin(); r != h->second.rend() && path.size() < 10; ++r)
            if (path.empty() || path.back() != r->st) path.push_back(r->st);
    std::reverse(path.begin(), path.end());
    std::set<std::pair<int, int>> pathEdges;
    for (size_t i = 1; i < path.size(); i++) pathEdges.insert({path[i - 1], path[i]});
    std::set<int> pathStates(path.begin(), path.end());

    float colW = em * 9, rowH = em * 2.2f, nodeW = em * 6, nodeH = em * 1.5f;
    size_t rows = 1;
    for (const auto &c : cols) rows = std::max(rows, c.size());
    if (!ImGui::BeginChild("graph", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::EndChild();
        return;
    }
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float width = std::max(avail.x, cols.size() * colW), height = std::max(avail.y, rows * rowH + em);
    ImGui::Dummy(ImVec2(width, height));
    float left = origin.x + (width - cols.size() * colW) * 0.5f;
    std::map<int, ImVec2> pos; // top left of each node
    for (size_t c = 0; c < cols.size(); c++)
        for (size_t r = 0; r < cols[c].size(); r++) {
            float top = origin.y + em * 0.5f + (height - em - cols[c].size() * rowH) * 0.5f + r * rowH;
            pos[cols[c][r]] = ImVec2(left + c * colW + (colW - nodeW) * 0.5f, top);
        }
    // The state in focus in view when it changes
    static int shown = INT_MIN;
    if (shown != focus && pos.count(focus)) {
        shown = focus;
        ImGui::SetScrollX(pos[focus].x - origin.x + nodeW * 0.5f - avail.x * 0.5f);
        ImGui::SetScrollY(pos[focus].y - origin.y + nodeH * 0.5f - avail.y * 0.5f);
    }
    ImDrawList *draw = ImGui::GetWindowDrawList();
    float thick = std::max(1.0f, em / 13);
    // Edges between neighbouring columns, left to right
    for (const auto &r : m.refs) {
        if (!r.fromLiteral || r.from == r.to || !col.count(r.from) || !col.count(r.to)) continue;
        if (col[r.to] != col[r.from] + 1) continue;
        ImVec2 a = pos[r.from], b = pos[r.to];
        bool onPath = pathEdges.count({r.from, r.to});
        ImU32 color = onPath ? IM_COL32(90, 220, 90, 255) : IM_COL32(150, 160, 190, 170);
        Curve(draw, ImVec2(a.x + nodeW, a.y + nodeH * 0.5f), ImVec2(b.x, b.y + nodeH * 0.5f), color,
              onPath ? thick * 2.5f : thick, r.other);
    }
    // Nodes
    for (const auto &[no, p] : pos) {
        bool exists = m.states.count(no) > 0, now = no == e.st, onPath = pathStates.count(no) > 0;
        ImU32 fill = !exists ? IM_COL32(120, 40, 40, 230) : onPath ? IM_COL32(40, 100, 50, 230) : IM_COL32(45, 55, 80, 230);
        ImU32 border = no == focus ? IM_COL32(255, 220, 60, 255) : now ? IM_COL32(90, 230, 90, 255) : IM_COL32(120, 130, 160, 255);
        ImVec2 q(p.x + nodeW, p.y + nodeH);
        draw->AddRectFilled(p, q, fill, em * 0.3f);
        draw->AddRect(p, q, border, em * 0.3f, no == focus || now ? thick * 2 : thick);
        std::string label = std::to_string(no);
        ImVec2 sz = ImGui::CalcTextSize(label.c_str());
        draw->AddText(ImVec2(p.x + (nodeW - sz.x) * 0.5f, p.y + (nodeH - sz.y) * 0.5f), IM_COL32(240, 240, 240, 255), label.c_str());
        ImGui::SetCursorScreenPos(p);
        ImGui::PushID(no);
        ImGui::InvisibleButton("node", ImVec2(nodeW, nodeH));
        ImGui::PopID();
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && exists) {
            auto at = m.states[no];
            ShowLine(m, at.first, m.files[at.first].cns.states[at.second].line);
        } else if (ImGui::IsItemClicked()) {
            focus = no;
            follow = false;
        }
        if (ImGui::BeginItemTooltip()) {
            if (exists) {
                auto at = m.states[no];
                ImGui::Text("State %d: %s:%d", no, m.files[at.first].path.c_str(), m.files[at.first].cns.states[at.second].line);
            } else {
                ImGui::TextColored(ImVec4(1, 0.45f, 0.45f, 1), "State %d does not exist", no);
            }
            ImGui::TextDisabled("%zu change to it, it changes to %zu", in[no].size(), out[no].size());
            ImGui::EndTooltip();
        }
    }
    ImGui::EndChild();
}

} // namespace

std::string VarName(const std::string &def, char kind, int index) {
    auto it = g_varNames.find(VarKey(def, kind, index));
    return it == g_varNames.end() ? "" : it->second;
}

void AnalysisSettingsLine(const std::string &key, const std::string &value) {
    if (key != "VarName") return;
    // def|kind|index|name
    size_t a = value.find('|'), b = a == std::string::npos ? a : value.find('|', a + 1),
           c = b == std::string::npos ? b : value.find('|', b + 1);
    if (c == std::string::npos || b != a + 2) return;
    g_varNames[VarKey(value.substr(0, a), value[a + 1], atoi(value.c_str() + b + 1))] = value.substr(c + 1);
}

void AnalysisSettingsWrite(ImGuiTextBuffer *buf) {
    for (const auto &[k, name] : g_varNames)
        if (!name.empty()) buf->appendf("VarName=%s|%s\n", k.c_str(), name.c_str());
}

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
        if (ImGui::BeginTabItem("Variables")) {
            VariablesTab(*m, *e);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("States")) {
            GraphTab(*m, *e);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

} // namespace app
