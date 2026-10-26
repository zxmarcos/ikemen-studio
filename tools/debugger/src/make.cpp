#include "make.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#include "air.h"
#include "cns.h"
#include "ini.h"

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
namespace {

bool CommentLine(const std::string &line, bool zss) {
    size_t a = line.find_first_not_of(" \t\r");
    return a != std::string::npos && line[a] == (zss ? '#' : ';');
}

// Where each block starts (0-based line indices): its header, or the comment lines just above it
std::vector<size_t> BlockStarts(const std::vector<std::string> &lines, const std::vector<int> &headers, bool zss) {
    std::vector<size_t> starts;
    size_t floor = 0;
    for (int h : headers) {
        size_t at = h - 1;
        while (at > floor && CommentLine(lines[at - 1], zss)) at--;
        starts.push_back(at);
        floor = h;
    }
    return starts;
}

std::string Lines(const std::vector<std::string> &lines, size_t from, size_t to) { // [from, to)
    std::string t;
    for (size_t i = from; i < to && i < lines.size(); i++) t += lines[i] + "\n";
    return t;
}

} // namespace

std::string Make_Head(const std::string &text, bool zss) {
    CnsFile f;
    Cns_Parse(text, zss, f);
    if (f.states.empty()) return text;
    std::vector<std::string> lines = SplitLines(text);
    return Lines(lines, 0, BlockStarts(lines, {f.states[0].line}, zss)[0]);
}

std::string Make_KeepStates(const std::string &text, bool zss, int *count, bool *taunt) {
    CnsFile f;
    Cns_Parse(text, zss, f);
    std::vector<std::string> lines = SplitLines(text);
    std::vector<int> headers;
    for (const CnsState &s : f.states) headers.push_back(s.line);
    std::vector<size_t> starts = BlockStarts(lines, headers, zss);
    std::string out;
    for (size_t i = 0; i < f.states.size(); i++) {
        const CnsState &s = f.states[i];
        bool keep = !s.function.empty() || (s.literal && s.no >= 0 && s.no < 200);
        if (!keep) continue;
        std::string block = Lines(lines, starts[i], i + 1 < starts.size() ? starts[i + 1] : lines.size());
        while (block.size() >= 2 && block[block.size() - 1] == '\n' && block[block.size() - 2] == '\n') block.pop_back();
        out += block + "\n";
        if (s.function.empty()) {
            if (count) ++*count;
            if (taunt && s.no == 195) *taunt = true;
        }
    }
    return out;
}

std::string Make_KeepActions(const std::string &air, int *count) {
    AirFile f;
    Air_Parse(air, f);
    std::vector<std::string> lines = SplitLines(air);
    std::vector<const AirAction *> acts;
    for (const auto &kv : f.actions) acts.push_back(&kv.second);
    std::sort(acts.begin(), acts.end(), [](const AirAction *a, const AirAction *b) { return a->line < b->line; });
    std::vector<int> headers;
    for (const AirAction *a : acts) headers.push_back(a->line);
    std::vector<size_t> starts = BlockStarts(lines, headers, false);
    std::string out;
    for (size_t i = 0; i < acts.size(); i++) {
        if (acts[i]->no >= 200 && acts[i]->no < 5000) continue;
        std::string block = Lines(lines, starts[i], i + 1 < starts.size() ? starts[i + 1] : lines.size());
        while (block.size() >= 2 && block[block.size() - 1] == '\n' && block[block.size() - 2] == '\n') block.pop_back();
        out += block + "\n";
        if (count) ++*count;
    }
    return out;
}

std::string Make_CommandFile(const std::string &name, bool taunt) {
    std::string s;
    s += "; Commands of " + name + "\n;\n";
    s += "; Each [Command] gives a name to a motion; the states test it as command = \"name\".\n";
    s += "; Directions: B, DB, D, DF, F, UF, U, UB. Buttons: a, b, c, x, y, z, s, d, w.\n";
    s += "; / held, ~ released, $ the diagonals count, + together, > nothing else in between.\n\n";
    s += "[Defaults]\ncommand.time = 15\ncommand.buffer.time = 1\n\n";
    s += ";-| Special motions |--------------------------------------------------------\n";
    s += "[Command]\nname = \"QCF_x\"\ncommand = ~D, DF, F, x\n\n";
    s += "[Command]\nname = \"QCB_a\"\ncommand = ~D, DB, B, a\n\n";
    s += ";-| Double tap |--------------------------------------------------------------\n";
    s += "[Command]\nname = \"FF\"     ;Required (do not remove)\ncommand = F, F\ntime = 10\n\n";
    s += "[Command]\nname = \"BB\"     ;Required (do not remove)\ncommand = B, B\ntime = 10\n\n";
    s += ";-| 2/3 Button Combination |--------------------------------------------------\n";
    s += "[Command]\nname = \"recovery\";Required (do not remove)\ncommand = x+y\ntime = 1\n\n";
    s += ";-| Single Button |-----------------------------------------------------------\n";
    for (const char *b : {"a", "b", "c", "x", "y", "z"})
        s += std::string("[Command]\nname = \"") + b + "\"\ncommand = " + b + "\ntime = 1\n\n";
    s += "[Command]\nname = \"start\"\ncommand = s\ntime = 1\n\n";
    s += ";-| Hold Dir |----------------------------------------------------------------\n";
    for (auto [n, d] : {std::pair{"holdfwd", "F"}, {"holdback", "B"}, {"holdup", "U"}, {"holddown", "D"}})
        s += std::string("[Command]\nname = \"") + n + "\";Required (do not remove)\ncommand = /$" + d + "\ntime = 1\n\n";
    s += ";---------------------------------------------------------------------------\n";
    s += "; The moves the commands start, tested every tick from the top.\n";
    s += "[Statedef -1]\n\n";
    if (taunt)
        s += "[State -1, Taunt]\ntype = ChangeState\nvalue = 195\ntriggerall = command = \"start\"\n"
             "trigger1 = statetype = S\ntrigger1 = ctrl\n\n";
    else
        s += "; [Statedef -1] needs a controller: this one does nothing.\n[State -1, Nothing yet]\ntype = Null\ntrigger1 = 0\n\n";
    s += "; Create > A move writes them here, and Commands > Start a state with this command.\n";
    return s;
}

std::string Make_RemoveSection(const std::string &text, const char *lowerName) {
    IniFile f;
    Ini_Parse(text, f);
    std::vector<std::string> lines = SplitLines(text);
    for (size_t i = 0; i < f.sections.size(); i++) {
        if (f.sections[i].lower != lowerName) continue;
        size_t from = f.sections[i].line - 1;
        size_t to = i + 1 < f.sections.size() ? f.sections[i + 1].line - 1 : lines.size();
        // The next section's comments stay with it
        while (to > from + 1 && i + 1 < f.sections.size() && CommentLine(lines[to - 1], false)) to--;
        lines.erase(lines.begin() + from, lines.begin() + to);
        return JoinLines(lines);
    }
    return text;
}