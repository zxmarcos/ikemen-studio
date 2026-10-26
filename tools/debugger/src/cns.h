// State files (CNS and ZSS): their statedefs, the controllers of each with their parameters,
// and every line of code with the state it is in, for checks over a character's states (Lint,
// Variables, the state graph). Read as Ikemen reads them (compiler.go): CNS [Statedef n] and
// [State n, label] sections of "key = value" lines (";" comments); ZSS [StateDef n; key: value;]
// headers, [Function ...] sections, controllers written name{key: value; ...} ("#" comments).
#pragma once

#include <climits>
#include <string>
#include <vector>

struct CnsParam {
    std::string key;   // lower case ("trigger1", "value", "var(3)")
    std::string value; // trimmed, without its comment
    int line = 0;      // 1-based
};

struct CnsCtrl {
    std::string type;        // lower case ("changestate"); "" when it has none
    int line = 0;            // CNS: of its [State] header; ZSS: of its name
    bool hasTrigger = false; // CNS: a triggerall or trigger1; ZSS: always (its ifs)
    std::vector<CnsParam> params; // CNS: every line but type and the triggers; ZSS: the braces' pairs
    const CnsParam *Param(const char *key) const;
};

struct CnsState {
    int no = 0;
    bool literal = true;  // the number is written as one (ZSS can use an expression)
    std::string function; // a ZSS [Function name(...)] section instead of a state
    int line = 0;
    std::vector<CnsParam> params; // of the statedef: type, anim, ctrl...
    std::vector<CnsCtrl> ctrls;
    const CnsParam *Param(const char *key) const;
};

// A line of code (comments out): CNS parameter and trigger values, ZSS lines in states and
// functions. state: the index in CnsFile::states, -1 outside any.
struct CnsCode {
    int line = 0, state = -1;
    std::string text;
};

struct CnsFile {
    bool zss = false;
    std::vector<CnsState> states;
    std::vector<CnsCode> code;
};

bool Cns_IsZss(const std::string &path); // ".zss"
void Cns_Parse(const std::string &text, bool zss, CnsFile &out);

// A value made of numbers: "5030" -> {5030}; "S1000, 2" -> 'S', {1000, 2}; "F5" -> 'F', {5}.
// False for anything else (an expression).
struct CnsNumbers {
    char prefix = 0;
    std::vector<int> n;
};
bool Cns_Numbers(const std::string &value, CnsNumbers &out);
