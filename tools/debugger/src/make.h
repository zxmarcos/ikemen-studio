// Text written for new things in a character's files: a ChangeState on a command at the top of
// [Statedef -1], a move's statedef. CNS or ZSS, as the file it goes in is written.
#pragma once

#include <string>

// when: 0 any statetype, 1 standing, 2 crouching, 3 in the air; extra: one more condition
// ("power >= 1000"), or ""
std::string Make_CommandEntry(bool zss, const std::string &command, int state, int when, bool ctrl,
                              const std::string &extra = "");

// block put at the top of [Statedef no], right after its header (CNS: and the statedef's
// parameters). False when the text has no such statedef. line: where the block starts (1-based).
bool Make_InsertInStatedef(std::string &text, bool zss, int no, const std::string &block, int *line = nullptr);

// A move's HitDef values
struct MakeHit {
    int damage, guardDamage;
    int pause1, pause2; // pausetime: the attacker's, the target's
    int spark;
    int slideTime, hitTime;
    float velocity;
    float airVx, airVy;
    int airHitTime;
    int animType; // 0 Light, 1 Medium, 2 Hard
};
extern const MakeHit kMakeHits[3]; // light, medium and hard, after KFM's punches

struct MakeMove {
    std::string name = "New move";
    int state = 200, action = 200;
    int kind = 0;    // 0 standing, 1 crouching, 2 in the air
    int klass = 0;   // 0 normal, 1 special, 2 hyper (takes 1000 power)
    int hitElem = 0; // the element (1-based) that arms the HitDef, 0: from the start
    int sparkY = -70;
    bool sound = false;
    int snd[2] = {0, 0};
    MakeHit hit = kMakeHits[0];
};

// The statedef of an attack as KFM writes its own: its animation, the HitDef, a sound at the
// start, and the way back to standing or crouching (an air move ends on landing, its control
// back when the animation ends). Starts with a comment line; ends with a line end.
std::string Make_MoveState(const MakeMove &m, bool zss);
