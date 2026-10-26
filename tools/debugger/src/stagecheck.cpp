#include "stagecheck.h"

#include "air.h"
#include "ini.h"
#include "sff.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <regex>
#include <set>

namespace {

// The keys each section is read for (stage.go, loadStage; the select screen reads portraitscale;
// versiondate is MUGEN's, for people); a key the engine does not read is a note. Sections not here ([Music], [Shaders], [Constants]) are not looked at.
const std::map<std::string, std::set<std::string>> kSectionKeys = {
    {"info", {"name", "displayname", "author", "mugenversion", "ikemenversion", "roundloop", "versiondate"}},
    {"stageinfo",
     {"zoffset", "zoffsetlink", "hires", "autoturn", "resetbg", "localcoord", "xscale", "yscale", "portraitscale"}},
    {"scaling", {"topz", "botz", "topscale", "botscale", "depthtoscreen"}},
    {"bound", {"screenleft", "screenright"}},
    {"playerinfo", {"partnerspacing", "leftbound", "rightbound", "topbound", "botbound"}},
    {"camera",
     {"startx", "starty", "boundleft", "boundright", "boundhigh", "boundlow", "zboundtop", "zboundbot",
      "verticalfollow", "floortension", "tension", "ztension", "tensionvel", "ztensionvel", "overdrawhigh",
      "overdrawlow", "cuthigh", "cutlow", "startzoom", "fov", "yshift", "near", "far", "autocenter", "zautocenter",
      "yscrollspeed", "verticalfollowzoomdelta", "lowestcap", "zoomin", "zoomout", "zoomanchor", "autozoom",
      "zoomindelay", "zoominspeed", "zoomoutspeed", "boundhighzoomdelta", "tensionlow", "tensionhigh"}},
    {"bgdef", {"spr", "model", "debugbg", "bgclearcolor", "roundpos"}},
    {"model", {"offset", "scale", "environment", "environmentintensity"}},
    {"shadow",
     {"intensity", "color", "xscale", "yscale", "fade.range", "xshear", "angle", "xangle", "yangle", "focallength",
      "projection", "offset", "window", "ydelta", "shader.name"}},
    {"reflection",
     {"reflect", "intensity", "color", "layerno", "xscale", "yscale", "fade.range", "xshear", "angle", "xangle",
      "yangle", "focallength", "projection", "offset", "window", "ydelta", "shader.name"}},
};

// Every key read for a BG, a BGCtrlDef or a BGCtrl (readBackGround, bgCtrl.read)
const std::set<std::string> kBgKeys = {
    "actionno", "add", "alpha", "angle", "autoresizeparallax", "color", "ctrlid", "delta", "dest", "id", "invertall",
    "invertblend", "layerno", "loop", "looptime", "mask", "maskwindow", "mul", "path", "positionlink", "projection",
    "scalefilter", "scalemode", "scaledelta", "scalestart", "sctrlid", "shader.name", "sinadd", "sincolor", "sinhue",
    "sinmul", "source", "spriteno", "start", "startlayer", "tile", "tilespacing", "time", "trans", "type", "value",
    "velocity", "volume", "width", "window", "windowdelta", "x", "xangle", "xbottomzoomdelta", "xscale", "xshear",
    "y", "yangle", "ydelta", "yscale", "yscaledelta", "yscalestart", "zoomdelta", "zoomscaledelta", "focallength",
    "hue", "name", "offset", "scale", "fade.range", "intensity", "topscale", "botscale", "topz", "botz",
    "invertall", "zoffset"};

const std::regex kShaderKey(R"(^(shader\.)?(param\d+|tex\d+\.\w+)$)");

std::vector<int> Ints(const std::string &v) {
    std::vector<int> out;
    const char *p = v.c_str();
    while (*p) {
        char *end;
        long n = strtol(p, &end, 10);
        if (end == p) {
            ++p;
            continue;
        }
        out.push_back((int)n);
        p = end;
    }
    return out;
}

std::string First(const std::string &lower) { return lower.substr(0, lower.find(' ')); }

float Num(const IniSection *s, const char *key, float otherwise) {
    const IniField *f = s ? s->Field(key) : nullptr;
    return f && !f->value.empty() ? (float)atof(f->value.c_str()) : otherwise;
}

} // namespace

std::vector<StageFinding> Stage_Check(const std::string &text, const SffDoc *sff,
                                      const std::function<bool(const std::string &)> &exists) {
    std::vector<StageFinding> out;
    auto add = [&](StageFinding::Level l, int line, const std::string &t) { out.push_back({l, line, t}); };
    IniFile ini;
    Ini_Parse(text, ini);
    AirFile air;
    Air_Parse(text, air);
    // Ikemen takes the first section of a name ("getSection")
    std::map<std::string, const IniSection *> named;
    for (const IniSection &s : ini.sections)
        if (!named.count(s.lower)) named[s.lower] = &s;
    auto section = [&](const char *n) -> const IniSection * {
        auto it = named.find(n);
        return it == named.end() ? nullptr : it->second;
    };

    // Sections and keys the engine does not read
    for (const IniSection &s : ini.sections) {
        std::string first = First(s.lower);
        if (s.lower.find('.') != std::string::npos) continue; // a language's ("en.info")
        if (first == "bg" || first == "bgctrldef" || first == "bgctrl" || first == "bgctrl3d") {
            for (const IniField &f : s.fields)
                if (!kBgKeys.count(f.key) && !std::regex_match(f.key, kShaderKey))
                    add(StageFinding::Note, f.line, "\"" + f.key + "\" is not read for a " + first);
            continue;
        }
        if (s.lower.compare(0, 13, "begin action ") == 0 || s.lower == "music" || s.lower == "shaders" ||
            s.lower == "constants")
            continue;
        auto keys = kSectionKeys.find(s.lower);
        if (keys == kSectionKeys.end()) {
            add(StageFinding::Warning, s.line, "[" + s.name + "]: the engine reads no section of this name");
            continue;
        }
        if (named[s.lower] != &s) {
            add(StageFinding::Warning, s.line, "[" + s.name + "] again: the engine reads only the first one");
            continue;
        }
        static const std::regex player(R"(^p[1-8](startx|starty|startz|facing)$)");
        static const std::regex infoFile(R"(^(attachedchar\d*|round\d+def)$)");
        static const std::regex language(R"(^[a-z]{2}\.)"); // "ja.name": a language's value
        for (const IniField &f : s.fields) {
            if (keys->second.count(f.key) || std::regex_match(f.key, kShaderKey)) continue;
            if (std::regex_search(f.key, language)) continue;
            if (s.lower == "playerinfo" && std::regex_match(f.key, player)) continue;
            if (s.lower == "info" && std::regex_match(f.key, infoFile)) continue;
            add(StageFinding::Note, f.line, "\"" + f.key + "\" is not read in [" + s.name + "]");
        }
    }

    // Files
    const IniSection *bgdef = section("bgdef");
    if (!bgdef) add(StageFinding::Error, 0, "No [BGdef]: the stage has no sprites file");
    auto file = [&](const IniSection *s, const char *key, StageFinding::Level missing) {
        const IniField *f = s ? s->Field(key) : nullptr;
        if (!f || f->value.empty()) return;
        if (!exists(f->value)) add(missing, f->line, std::string(key) + ": " + f->value + " is not found");
    };
    if (bgdef) {
        const IniField *spr = bgdef->Field("spr");
        if (!spr || spr->value.empty()) add(StageFinding::Error, bgdef->line, "[BGdef] names no spr file");
        file(bgdef, "spr", StageFinding::Error);
        file(bgdef, "model", StageFinding::Error);
    }
    file(section("model"), "environment", StageFinding::Warning);
    if (const IniSection *info = section("info"))
        for (const IniField &f : info->fields)
            if ((f.key.compare(0, 12, "attachedchar") == 0 || std::regex_match(f.key, std::regex(R"(round\d+def)"))) &&
                !f.value.empty() && !exists(f.value))
                add(StageFinding::Error, f.line, f.key + ": " + f.value + " is not found");
    if (const IniSection *music = section("music"))
        for (const IniField &f : music->fields)
            if (f.key.size() >= 7 && f.key.compare(f.key.size() - 7, 7, "bgmusic") == 0 && !f.value.empty() &&
                f.value != "auto" && !exists(f.value))
                add(StageFinding::Warning, f.line, f.key + ": " + f.value + " is not found");

    // The BGs: what the engine skips, sprites and actions that do not exist; their ids
    std::set<int> ids;
    std::set<std::pair<int, int>> used;
    auto sprite = [&](int group, int number, int line, const std::string &what) {
        used.insert({group, number});
        if (sff && group >= 0 && Sff_FindSprite(*sff, group, number) < 0)
            add(StageFinding::Error, line,
                what + ": sprite " + std::to_string(group) + ", " + std::to_string(number) + " is not in the SFF");
    };
    auto action = [&](int no, int line, const std::string &what) {
        auto it = air.actions.find(no);
        if (it == air.actions.end()) {
            add(StageFinding::Error, line, what + ": there is no [Begin Action " + std::to_string(no) + "]");
            return;
        }
        for (const AirElem &e : it->second.elems) sprite(e.group, e.image, e.line, "[Begin Action " + std::to_string(no) + "]");
    };
    for (int i : ini.StageBgs()) {
        const IniSection &s = ini.sections[i];
        std::string what = "[" + s.name + "]";
        const IniField *type = s.Field("type");
        if (const IniField *id = s.Field("id"); id && !Ints(id->value).empty()) ids.insert(Ints(id->value)[0]);
        if (!type || type->value.empty()) {
            add(StageFinding::Warning, s.line, what + " has no type: the engine skips it");
            continue;
        }
        char t = (char)tolower((unsigned char)type->value[0]);
        if (!strchr("napvd", t)) {
            add(StageFinding::Warning, type->line, what + ": type " + type->value + " is unknown: the engine skips it");
            continue;
        }
        if (t == 'v') {
            if (const IniField *path = s.Field("path"); path && !path->value.empty() && !exists(path->value))
                add(StageFinding::Error, path->line, what + ": the video " + path->value + " is not found");
            continue;
        }
        if (t == 'd') continue;
        const IniField *spr = s.Field("spriteno"), *act = s.Field("actionno");
        // As readBackGround: the action unless a normal BG has a sprite
        if (act && (t != 'n' || !spr || spr->value.empty())) {
            std::vector<int> no = Ints(act->value);
            if (!no.empty()) action(no[0], act->line, what);
        } else if (spr) {
            std::vector<int> gn = Ints(spr->value);
            if (gn.size() >= 2) sprite(gn[0], gn[1], spr->line, what);
        } else {
            add(StageFinding::Warning, s.line, what + " has no spriteno nor actionno: it shows nothing");
        }
    }

    // The BGCtrls: types, ids, actions
    static const std::set<std::string> kCtrlTypes = {"anim",   "visible", "enable",   "null",      "palfx", "posset", "posadd",
                                                    "remappal", "shaderset", "sinx", "siny", "velset", "veladd"};
    for (const IniSection &s : ini.sections) {
        std::string first = First(s.lower);
        if (first != "bgctrldef" && first != "bgctrl") continue;
        std::string what = "[" + s.name + "]";
        if (const IniField *ctrl = s.Field("ctrlid"))
            for (int id : Ints(ctrl->value))
                if (id != -1 && !ids.count(id))
                    add(StageFinding::Warning, ctrl->line, what + ": no BG has id " + std::to_string(id));
        if (first == "bgctrldef") continue;
        const IniField *type = s.Field("type");
        std::string lower = type ? type->value : "";
        for (char &c : lower) c = (char)tolower((unsigned char)c);
        if (lower.empty()) add(StageFinding::Warning, s.line, what + " has no type");
        else if (!kCtrlTypes.count(lower))
            add(StageFinding::Warning, type->line, what + ": type " + type->value + " is not one the engine knows");
        else if (lower == "anim")
            if (const IniField *v = s.Field("value"); v && !Ints(v->value).empty()) action(Ints(v->value)[0], v->line, what);
    }

    // The camera and the players' start positions (in the stage's units)
    const IniSection *cam = section("camera"), *pi = section("playerinfo"), *bound = section("bound"),
                     *si = section("stageinfo");
    float lw = 320;
    if (si) {
        float lc[2] = {320, 240};
        if (si->Floats("localcoord", lc, 2) && lc[0] > 0) lw = lc[0];
    }
    float ratio = lw / 320;
    float bl = Num(cam, "boundleft", NAN), br = Num(cam, "boundright", NAN);
    if (!std::isnan(bl) && !std::isnan(br) && bl > br)
        add(StageFinding::Error, cam->Field("boundleft")->line, "boundleft is right of boundright");
    float zoomout = std::max(0.01f, Num(cam, "zoomout", 1));
    float sl = Num(bound, "screenleft", 15 * ratio), sr = Num(bound, "screenright", 15 * ratio);
    float view = lw / std::min(1.0f, zoomout) - sl - sr; // the widest the players can be apart
    float p1 = Num(pi, "p1startx", -70 * ratio), p2 = Num(pi, "p2startx", 70 * ratio);
    int piLine = pi ? pi->line : 0;
    if (std::fabs(p2 - p1) > view)
        add(StageFinding::Warning, pi && pi->Field("p2startx") ? pi->Field("p2startx")->line : piLine,
            "P1 and P2 start " + std::to_string((int)std::fabs(p2 - p1)) + " apart; the screen holds " +
                std::to_string((int)view));
    if (!std::isnan(bl) && !std::isnan(br)) {
        float lo = bl - lw / 2 + sl, hi = br + lw / 2 - sr;
        for (int p = 1; p <= 2; p++) {
            float x = p == 1 ? p1 : p2;
            if (x < lo || x > hi) {
                std::string key = "p" + std::to_string(p) + "startx";
                add(StageFinding::Warning, pi && pi->Field(key.c_str()) ? pi->Field(key.c_str())->line : piLine,
                    "P" + std::to_string(p) + " starts at " + std::to_string((int)x) + ", past where the camera can show (" +
                        std::to_string((int)lo) + " to " + std::to_string((int)hi) + ")");
            }
        }
    }

    // Sprites nothing uses (9000,x is the stage's portrait in some screenpacks; a screenpack can show others)
    if (sff) {
        int unused = 0;
        std::string list;
        for (const SffSprite &s : sff->sprites)
            if (!used.count({s.group, s.number}) && s.group != 9000) {
                if (unused++ < 8) list += (list.empty() ? "" : "; ") + std::to_string(s.group) + ", " + std::to_string(s.number);
            }
        if (unused)
            add(StageFinding::Note, bgdef ? bgdef->line : 0,
                std::to_string(unused) + " sprites of the SFF nothing uses: " + list + (unused > 8 ? "..." : ""));
    }

    std::stable_sort(out.begin(), out.end(), [](const StageFinding &a, const StageFinding &b) {
        return a.level != b.level ? a.level < b.level : a.line < b.line;
    });
    return out;
}
