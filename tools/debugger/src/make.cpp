#include "make.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#include "cns.h"

namespace {

std::vector<std::string> SplitLines(const std::string &text) {
    std::vector<std::string> lines;
    size_t start = 0;
    for (;;) {
        size_t nl = text.find('\n', start);
        lines.push_back(text.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return lines;
}

std::string JoinLines(const std::vector<std::string> &lines) {
    std::string text;
    for (size_t i = 0; i < lines.size(); i++) {
        if (i) text += '\n';
        text += lines[i];
    }
    return text;
}

const char *const kTypes[] = {"", "S", "C", "A"};

} // namespace

std::string Make_CommandEntry(bool zss, const std::string &command, int state, int when, bool ctrl,
                              const std::string &extra) {
    std::string value = std::to_string(state);
    if (when < 0 || when > 3) when = 0;
    if (zss) {
        std::string cond = "command = \"" + command + "\"";
        if (!extra.empty()) cond += " && " + extra;
        if (when) cond += std::string(" && stateType = ") + kTypes[when];
        if (ctrl) cond += " && ctrl";
        return "if " + cond + " {\n\tchangeState{value: " + value + "}\n}\n";
    }
    std::string t = "[State -1, " + command + "]\ntype = ChangeState\nvalue = " + value + "\n";
    t += "triggerall = command = \"" + command + "\"\n";
    if (!extra.empty()) t += "triggerall = " + extra + "\n";
    if (when) t += std::string("trigger1 = statetype = ") + kTypes[when] + "\n";
    if (ctrl) t += "trigger1 = ctrl\n";
    if (!when && !ctrl) t += "trigger1 = 1\n";
    return t + "\n";
}

bool Make_InsertInStatedef(std::string &text, bool zss, int no, const std::string &block, int *line) {
    CnsFile f;
    Cns_Parse(text, zss, f);
    const CnsState *st = nullptr;
    for (const CnsState &s : f.states)
        if (s.literal && s.function.empty() && s.no == no) {
            st = &s;
            break;
        }
    if (!st) return false;
    // Right after the header (ZSS: until its "]"; CNS: and the statedef's parameters), so that
    // the comments above the first controller stay with it
    std::vector<std::string> lines = SplitLines(text);
    size_t at; // the index the block's first line takes
    if (zss) {
        size_t end = st->line - 1;
        while (end < lines.size() && lines[end].find(']') == std::string::npos) end++;
        at = std::min(end + 1, lines.size());
    } else {
        int last = st->line;
        for (const CnsParam &p : st->params) last = std::max(last, p.line);
        at = std::min((size_t)last, lines.size());
    }
    std::vector<std::string> add = SplitLines(block);
    while (!add.empty() && add.back().empty()) add.pop_back();
    add.insert(add.begin(), ""); // apart from the header
    if (at < lines.size() && !lines[at].empty() && lines[at].find_first_not_of(" \t\r") != std::string::npos)
        add.push_back(""); // and from what follows
    lines.insert(lines.begin() + at, add.begin(), add.end());
    if (line) *line = (int)at + 2;
    text = JoinLines(lines);
    return true;
}

const MakeHit kMakeHits[3] = {
    {23, 0, 8, 8, 0, 5, 11, -4.0f, -1.4f, -3.0f, 15, 0},
    {45, 0, 12, 12, 1, 8, 15, -5.0f, -2.0f, -4.0f, 18, 1},
    {70, 0, 14, 14, 1, 10, 18, -6.0f, -2.5f, -4.5f, 20, 2},
};

namespace {

std::string Num(float v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%g", v);
    return buf;
}

using Pairs = std::vector<std::pair<std::string, std::string>>;

// A controller: CNS "[State n, label]" with its trigger and parameters; ZSS an if around it
std::string Ctrl(bool zss, int state, const char *label, const char *type, const std::string &trigger,
                 const Pairs &params) {
    std::string t;
    if (zss) {
        t += "if " + trigger + " {\n\t" + type + "{";
        if (params.size() > 3) {
            t += "\n";
            for (const auto &[k, v] : params) t += "\t\t" + k + ": " + v + ";\n";
            t += "\t}\n";
        } else {
            for (size_t i = 0; i < params.size(); i++) t += (i ? "; " : "") + params[i].first + ": " + params[i].second;
            t += "}\n";
        }
        return t + "}\n";
    }
    t += "[State " + std::to_string(state) + ", " + label + "]\ntype = " + type + "\ntrigger1 = " + trigger + "\n";
    for (const auto &[k, v] : params) t += k + " = " + v + "\n";
    return t;
}

} // namespace

std::string Make_MoveState(const MakeMove &m, bool zss) {
    static const char *types[] = {"S", "C", "A"};
    static const char *classes[] = {"NA", "SA", "HA"};
    static const char *animTypes[] = {"Light", "Medium", "Hard"};
    const MakeHit &h = m.hit;
    int kind = m.kind < 0 || m.kind > 2 ? 0 : m.kind, klass = m.klass < 0 || m.klass > 2 ? 0 : m.klass;
    int animType = h.animType < 0 || h.animType > 2 ? 0 : h.animType;
    std::string type = types[kind];
    Pairs head = {{"type", type}, {"movetype", "A"}, {"physics", type}, {"juggle", kind == 2 ? "2" : "1"}};
    if (kind != 2) head.push_back({"velset", "0, 0"});
    head.push_back({"ctrl", "0"});
    head.push_back({"anim", std::to_string(m.action)});
    head.push_back({"poweradd", klass == 2 ? "-1000" : klass == 1 ? "30" : "10"});
    head.push_back({"sprpriority", "2"});

    std::string t, c = zss ? "#" : ";", n = std::to_string(m.state);
    t += c + "---------------------------------------------------------------------------\n";
    t += c + " " + m.name + " (made with the move wizard)\n";
    if (zss) {
        t += "[StateDef " + n + ";\n";
        for (size_t i = 0; i < head.size(); i++)
            t += head[i].first + ": " + head[i].second + ";" + (i + 1 < head.size() ? "\n" : "]\n");
    } else {
        t += "[Statedef " + n + "]\n";
        for (const auto &[k, v] : head) t += k + " = " + v + "\n";
    }
    t += "\n";
    if (m.sound)
        t += Ctrl(zss, m.state, "Sound", zss ? "playSnd" : "PlaySnd", zss ? "time = 1" : "Time = 1",
                  {{"value", std::to_string(m.snd[0]) + ", " + std::to_string(m.snd[1])}}) + "\n";
    Pairs hit = {
        {"attr", type + ", " + classes[klass]},
        {"damage", std::to_string(h.damage) + ", " + std::to_string(h.guardDamage)},
        {"animtype", animTypes[animType]},
        {"guardflag", kind == 0 ? "MA" : kind == 1 ? "L" : "HA"},
        {"hitflag", "MAF"},
        {"priority", std::to_string(3 + animType) + ", Hit"},
        {"pausetime", std::to_string(h.pause1) + ", " + std::to_string(h.pause2)},
        {"sparkno", std::to_string(h.spark)},
        {"sparkxy", "-10, " + std::to_string(m.sparkY)},
        {"hitsound", animType == 0 ? "5, 0" : animType == 1 ? "5, 2" : "5, 3"},
        {"guardsound", "6, 0"},
        {"ground.type", kind == 1 ? "Low" : "High"},
        {"ground.slidetime", std::to_string(h.slideTime)},
        {"ground.hittime", std::to_string(h.hitTime)},
        {"ground.velocity", Num(h.velocity)},
        {"air.velocity", Num(h.airVx) + ", " + Num(h.airVy)},
        {"air.hittime", std::to_string(h.airHitTime)},
    };
    std::string armed = m.hitElem > 0 ? (zss ? "animElem = " : "AnimElem = ") + std::to_string(m.hitElem)
                                      : (zss ? "time = 0" : "Time = 0");
    t += Ctrl(zss, m.state, "Hit", zss ? "hitDef" : "HitDef", armed, hit) + "\n";
    if (kind == 2) // landing ends it (the air physics); control comes back when the animation ends
        t += Ctrl(zss, m.state, "Control", zss ? "ctrlSet" : "CtrlSet", zss ? "animTime = 0" : "AnimTime = 0",
                  {{"value", "1"}});
    else
        t += Ctrl(zss, m.state, "End", zss ? "changeState" : "ChangeState", zss ? "animTime = 0" : "AnimTime = 0",
                  {{"value", kind == 1 ? "11" : "0"}, {"ctrl", "1"}});
    return t;
}