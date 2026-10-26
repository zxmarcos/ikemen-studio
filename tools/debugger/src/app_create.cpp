// Create: new things made from a form, written into the files and applied. Move: an attack's
// statedef as KFM writes its own (src/make.cpp), at the end of the character's first state file,
// and a ChangeState on its command at the top of [Statedef -1].
#include "app_internal.h"

#include <cfloat>

#include "cns.h"
#include "imgui_stdlib.h"
#include "make.h"
#include "roster.h"

namespace app {

namespace {

enum { kTabMove, kTabChar, kTabs };
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

// ---------------------------------------------------------------------------
// A character from another one
// ---------------------------------------------------------------------------

struct CharForm {
    std::string folder, name, author;
    std::string base;  // as ListDefs names it: "kfm" (chars/kfm/kfm.def) or "dir/x.def"
    int mode = 0;      // 0 its basics, 1 a full copy
    bool select = true; // added to select.def
    int load = 0;      // 0 in P1's place, 1 in P2's, 2 nowhere
    std::vector<std::string> list;
    bool listed = false;
    std::string filter, status;
    bool failed = false;
};
CharForm g_char;

std::string WithoutCr(const std::string &s) {
    std::string t;
    t.reserve(s.size());
    for (char ch : s)
        if (ch != '\r') t += ch;
    return t;
}

// '\n' line ends become "\r\n", as character files are usually written
bool WriteText(const std::string &path, const std::string &text) {
    std::string out;
    for (char ch : text) {
        if (ch == '\n') out += '\r';
        out += ch;
    }
    FILE *f = fopen(GamePath(path).c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
    return fclose(f) == 0 && ok;
}

bool Exists(const std::string &path) { return GetFileAttributesA(GamePath(path).c_str()) != INVALID_FILE_ATTRIBUTES; }

// The folders a relative path needs, then the file
bool CopyInto(const std::string &from, const std::string &to) {
    for (size_t slash = to.find_first_of("\\/"); slash != std::string::npos; slash = to.find_first_of("\\/", slash + 1))
        CreateDirectoryA(GamePath(to.substr(0, slash)).c_str(), nullptr);
    return CopyFileA(GamePath(from).c_str(), GamePath(to).c_str(), TRUE) != 0;
}

void CopyTree(const std::string &from, const std::string &to, const std::string &skip, int *copied) {
    CreateDirectoryA(GamePath(to).c_str(), nullptr);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(GamePath(from + "*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::string n = fd.cFileName;
        if (n == "." || n == "..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) CopyTree(from + n + "/", to + n + "/", "", copied);
        else if (_stricmp(n.c_str(), skip.c_str()) != 0 && CopyFileA(GamePath(from + n).c_str(), GamePath(to + n).c_str(), TRUE))
            ++*copied;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

// A field of a def's section set (added when missing; "" removes it)
void SetDefField(std::string &text, const char *section, const char *key, const std::string &value) {
    for (;;) { // removing: each line with that key
        IniFile f;
        Ini_Parse(text, f);
        const IniSection *s = nullptr;
        for (const IniSection &sec : f.sections)
            if (sec.lower == section) s = &sec;
        if (!s || (value.empty() && !s->Field(key)) || !Ini_SetField(text, *s, key, value) || !value.empty()) return;
    }
}

std::string Today() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d,%02d,%04d", t.wMonth, t.wDay, t.wYear);
    return buf;
}

// The def's [Info] given the new name, author and date
void NameDef(std::string &def, const CharForm &c) {
    std::string name = "\"" + c.name + "\"";
    SetDefField(def, "info", "name", name);
    SetDefField(def, "info", "displayname", name);
    IniFile f;
    Ini_Parse(def, f);
    for (const IniSection &s : f.sections)
        if (s.lower == "info" && s.Field("lifebarname")) SetDefField(def, "info", "lifebarname", name);
    SetDefField(def, "info", "versiondate", Today());
    SetDefField(def, "info", "author", "\"" + c.author + "\"");
}

std::string Extension(const std::string &path) {
    size_t dot = path.find_last_of('.'), slash = path.find_last_of("\\/");
    return dot == std::string::npos || (slash != std::string::npos && dot < slash) ? "" : path.substr(dot);
}

// Its basics: the def with the new files, the sprites and sounds renamed after the folder, the
// other files of [Files] in the base's folder copied as they are (palettes, a movelist...), the
// constants, the statedefs below 200 (CNS ones with the constants, ZSS ones in a .zss with
// every [Function]), the common states' actions, a new CMD file.
bool CreateBasics(const CharForm &c, const std::string &dir, const std::string &defText, const std::string &to,
                  std::string *error) {
    IniFile ini;
    Ini_Parse(defText, ini);
    const IniSection *files = nullptr;
    for (const IniSection &s : ini.sections)
        if (s.lower == "files") files = &s;
    if (!files) return *error = "the def has no [Files]", false;
    auto value = [&](const char *key) {
        const IniField *f = files->Field(key);
        std::string v = f ? f->value : "";
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
        return v;
    };
    auto stKey = [](const std::string &k) {
        return k == "st" || (k.size() > 2 && k.compare(0, 2, "st") == 0 && k.find_first_not_of("0123456789", 2) == std::string::npos);
    };
    const std::string &f = c.folder;
    // The files kept as they are
    static const char *own[] = {"cmd", "cns", "anim", "ai", "sprite", "sound", "stcommon"};
    for (const IniField &fl : files->fields) {
        bool skip = stKey(fl.key);
        for (const char *o : own) skip |= fl.key == o;
        std::string v = fl.value;
        if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
        if (skip || v.empty() || v.find("..") != std::string::npos || v.find(':') != std::string::npos) continue;
        if (Exists(dir + v)) CopyInto(dir + v, to + v);
    }
    if (value("sprite").empty() || !CopyInto(dir + value("sprite"), to + f + ".sff"))
        return *error = "could not copy the sprites " + dir + value("sprite"), false;
    if (!value("sound").empty() && !CopyInto(dir + value("sound"), to + f + ".snd"))
        return *error = "could not copy the sounds " + dir + value("sound"), false;

    // The constants, the states below 200
    std::string cnsPath = value("cns");
    std::string constants = WithoutCr(ReadWholeFile(GamePath(dir + cnsPath)));
    if (cnsPath.empty() || constants.empty()) return *error = "could not read the constants " + dir + cnsPath, false;
    std::vector<std::string> states;
    for (const IniField &fl : files->fields)
        if (stKey(fl.key) && std::find(states.begin(), states.end(), fl.value) == states.end()) states.push_back(fl.value);
    bool cnsIsSt = std::find(states.begin(), states.end(), cnsPath) != states.end();
    std::string cnsOut = "; Constants and states of " + c.name + "\n; The constants and the states below 200 come from " +
                         dir + ".\n\n" + (cnsIsSt ? Make_Head(constants, false) : constants);
    std::string zssOut;
    int count = 0;
    bool taunt = false, cnsStates = false;
    for (const std::string &p : states) {
        std::string text = WithoutCr(ReadWholeFile(GamePath(dir + p)));
        bool zss = Cns_IsZss(p);
        std::string kept = Make_KeepStates(text, zss, &count, &taunt);
        if (kept.empty()) continue;
        if (zss) zssOut += "# From " + dir + p + "\n" + kept;
        else cnsOut += (cnsOut.back() == '\n' ? "" : "\n") + kept, cnsStates = true;
    }
    std::string cnsName = f + (Extension(cnsPath).empty() ? ".cns" : Extension(cnsPath));
    if (Cns_IsZss(cnsPath)) cnsName = f + ".const";

    // The common states' actions
    std::string air = WithoutCr(ReadWholeFile(GamePath(dir + value("anim"))));
    if (value("anim").empty() || air.empty()) return *error = "could not read the animations " + dir + value("anim"), false;
    int actions = 0;
    std::string airOut = "; Animations of " + c.name + "\n; The actions of the common states (0-199, 5000 up) come from " +
                         dir + ".\n\n" + Make_KeepActions(air, &actions);

    // The def
    std::string def = defText;
    SetDefField(def, "files", "cmd", f + ".cmd");
    SetDefField(def, "files", "cns", cnsName);
    std::vector<std::string> stKeys;
    for (const IniField &fl : files->fields)
        if (stKey(fl.key) && fl.key != "st") stKeys.push_back(fl.key); // st stays in its place
    for (const std::string &k : stKeys) SetDefField(def, "files", k.c_str(), "");
    std::string st = cnsStates || zssOut.empty() ? cnsName : f + ".zss";
    SetDefField(def, "files", "st", st);
    if (cnsStates && !zssOut.empty()) SetDefField(def, "files", "st1", f + ".zss");
    SetDefField(def, "files", "anim", f + ".air");
    SetDefField(def, "files", "sprite", f + ".sff");
    SetDefField(def, "files", "sound", value("sound").empty() ? "" : f + ".snd");
    SetDefField(def, "files", "ai", "");
    def = Make_RemoveSection(def, "arcade");
    NameDef(def, c);
    def = "; " + c.name + ", made from " + dir + " with the Ikemen GO debugger\n" + def;
    bool ok = WriteText(to + f + ".def", def) && WriteText(to + cnsName, cnsOut) && WriteText(to + f + ".air", airOut) &&
              WriteText(to + f + ".cmd", Make_CommandFile(c.name, taunt)) &&
              (zssOut.empty() || WriteText(to + f + ".zss", "# States of " + c.name + ": the ones below 200 of " + dir +
                                                                ", and its functions\n\n" + zssOut));
    if (!ok) return *error = "could not write the files in " + to, false;
    Log(LogLine::Info, std::to_string(count) + " statedefs and " + std::to_string(actions) + " actions from " + dir);
    return true;
}

void CreateCharacter(CharForm &c) {
    auto fail = [&](const std::string &why) {
        c.status = why;
        c.failed = true;
    };
    c.failed = false;
    if (c.folder.empty() || c.folder.size() > 48 ||
        c.folder.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos)
        return fail("The folder takes letters, digits, '_' and '-' only.");
    if (c.base.empty()) return fail("Pick the character to start from.");
    std::string to = "chars/" + c.folder + "/";
    if (Exists("chars/" + c.folder)) return fail(to + " exists already.");
    size_t slash = c.base.find('/');
    std::string dir = "chars/" + (slash == std::string::npos ? c.base : c.base.substr(0, slash)) + "/";
    std::string defName = slash == std::string::npos ? c.base + ".def" : c.base.substr(slash + 1);
    std::string defText = WithoutCr(ReadWholeFile(GamePath(dir + defName)));
    if (defText.empty()) return fail("Could not read " + dir + defName);
    CharForm named = c;
    if (named.name.empty()) named.name = c.folder;
    if (!CreateDirectoryA(GamePath("chars/" + c.folder).c_str(), nullptr)) return fail("Could not make " + to);
    std::string error;
    bool ok;
    if (c.mode == 0) {
        ok = CreateBasics(named, dir, defText, to, &error);
    } else {
        int copied = 0;
        CopyTree(dir, to, defName, &copied);
        std::string def = defText;
        NameDef(def, named);
        ok = WriteText(to + c.folder + ".def", "; " + named.name + ", a copy of " + dir + defName + "\n" + def);
        if (!ok) error = "could not write " + to + c.folder + ".def";
        Log(LogLine::Info, std::to_string(copied) + " files copied from " + dir);
    }
    if (!ok) return fail("Not made: " + error + " (" + to + " is left as it is).");
    c.status = "Made " + to + c.folder + ".def";
    Log(LogLine::Info, c.status);
    // Into select.def, at the end of [Characters]
    if (c.select && !g_selectDef.empty()) {
        SourceFile &sel = LoadSource(g_selectDef);
        RosterFile r;
        Roster_Parse(sel.text, r);
        if (sel.Dirty()) Log(LogLine::Error, g_selectDef + " has changes not saved: " + c.folder + " is not added to it.");
        else if (r.charsLine && Roster_InsertLine(sel.text, r.charsEnd, c.folder) && SaveSource(sel)) {
            sel.version++;
            c.status += ", added to " + g_selectDef;
        }
    }
    if (c.load < 2 && Connected()) {
        SwapChar(c.load + 1, c.folder);
        c.status += ", now P" + std::to_string(c.load + 1);
    }
    c.listed = false;
}

void CharacterTab() {
    CharForm &c = g_char;
    float em = ImGui::GetFontSize();
    if (!c.listed) {
        c.list = ListDefs(false);
        c.listed = true;
        if (c.base.empty() && !c.list.empty()) c.base = std::find(c.list.begin(), c.list.end(), "kfm") != c.list.end() ? "kfm" : c.list[0];
    }
    ImGui::SetNextItemWidth(em * 10);
    ImGui::InputTextWithHint("folder", "chars/...", &c.folder);
    ImGui::SetItemTooltip("chars/<folder>/<folder>.def: letters, digits, '_' and '-'.");
    ImGui::SetNextItemWidth(em * 10);
    ImGui::InputTextWithHint("name", c.folder.empty() ? "its folder" : c.folder.c_str(), &c.name);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(em * 8);
    ImGui::InputText("author", &c.author);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Start from");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(em * 12);
    if (ImGui::BeginCombo("##base", c.base.c_str(), ImGuiComboFlags_HeightLarge)) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##filter", "filter", &c.filter);
        for (const std::string &s : c.list) {
            if (!c.filter.empty() && s.find(c.filter) == std::string::npos) continue;
            if (ImGui::Selectable(s.c_str(), c.base == s)) c.base = s;
        }
        ImGui::EndCombo();
    }
    ImGui::RadioButton("its basics", &c.mode, 0);
    ImGui::SetItemTooltip("The sprites and sounds (renamed after the folder), the palettes and other files of [Files], "
                          "the constants, the states below 200 it gives itself (intro, win poses, taunt), the actions "
                          "of the common states and a new CMD file: a character that stands, walks, jumps, guards and "
                          "gets hit like its base, with no attack yet.");
    ImGui::SameLine();
    ImGui::RadioButton("a full copy, moves included", &c.mode, 1);
    ImGui::Checkbox("add it to select.def", &c.select);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", g_selectDef.empty() ? "(not known yet)" : g_selectDef.c_str());
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Then put it");
    ImGui::SameLine();
    ImGui::RadioButton("in P1's place", &c.load, 0);
    ImGui::SameLine();
    ImGui::RadioButton("in P2's", &c.load, 1);
    ImGui::SameLine();
    ImGui::RadioButton("nowhere", &c.load, 2);
    if (ImGui::Button("Create the character")) CreateCharacter(c);
    if (!c.status.empty()) {
        ImGui::SameLine();
        ImGui::PushTextWrapPos(0);
        if (c.failed) ImGui::TextColored(ImVec4(1, 0.45f, 0.45f, 1), "%s", c.status.c_str());
        else ImGui::TextUnformatted(c.status.c_str());
        ImGui::PopTextWrapPos();
    }
}

} // namespace

void CreateMenu() {
    if (ImGui::MenuItem("A move...")) g_tabAsked = kTabMove, ShowWindow("Create");
    ImGui::SetItemTooltip("An attack's state for the selected player's character, and the command that starts it.");
    if (ImGui::MenuItem("A character...")) g_tabAsked = kTabChar, ShowWindow("Create");
    ImGui::SetItemTooltip("A new character from another one: its basics, or a full copy.");
}

void CreateToolWindow() {
    if (!BeginTool("Create")) return;
    if (ImGui::BeginTabBar("create")) {
        static const char *tabs[kTabs] = {"Move", "Character"};
        for (int i = 0; i < kTabs; i++) {
            if (!ImGui::BeginTabItem(tabs[i], nullptr, g_tabAsked == i ? ImGuiTabItemFlags_SetSelected : 0)) continue;
            if (i == kTabMove) MoveTab();
            else if (i == kTabChar) CharacterTab();
            ImGui::EndTabItem();
        }
        g_tabAsked = -1;
        ImGui::EndTabBar();
    }
    ImGui::End();
}

} // namespace app
