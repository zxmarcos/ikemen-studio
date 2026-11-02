// Text written for new things in a character's files: a ChangeState on a command at the top of
// [Statedef -1], a move's statedef. CNS or ZSS, as the file it goes in is written.
#pragma once

#include <string>
#include <vector>

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

// A new character's files from its base's texts ('\n' line ends). Each statedef or action
// takes the comment lines just above its header with it.
// The text before a state file's first statedef (a CNS constants file's [Data], [Size]...).
std::string Make_Head(const std::string &text, bool zss);
// The statedefs from 0 to 199 the file gives (what the base does in the common states: intro,
// win poses, taunt), and every ZSS [Function] (states call them). count: statedefs kept;
// taunt: state 195 among them.
std::string Make_KeepStates(const std::string &text, bool zss, int *count, bool *taunt);
// The AIR actions of the common states: below 200, 5000 up.
std::string Make_KeepActions(const std::string &air, int *count);
// A CMD file with the commands the engine and the common states name ("FF", "BB", "recovery",
// the holds...), the buttons, two motions; [Statedef -1] starts the taunt (state 195) on start
// when the character has one, else holds a Null.
std::string Make_CommandFile(const std::string &name, bool taunt);
// The text without a section (its header to the line before the next one).
std::string Make_RemoveSection(const std::string &text, const char *lowerName);

// A stage of images, from the back: each one sprite k,0 of the stage's SFF (axis at its top
// centre), a BG centred with its bottom on the screen's bottom.
struct MakeLayer {
    std::string name; // the BG's
    int w = 0, h = 0;
    float delta = 1;  // how much it moves with the camera (1 like the floor, less far away)
};
// A stage's 3D model: its file ("" none), where [Model] puts it and the camera around it
struct MakeModel {
    std::string file;
    float offset[3] = {0, 0, 0}, scale = 1, fov = 30, zNear = 0.1f, zFar = 10000;
    int left = 0, right = 0, high = 0;
};

struct MakeStage {
    std::string name, author, sff, music, date;
    int width = 320, height = 240; // localcoord: one pixel of the images is one unit
    int zoffset = 200;             // the floor
    std::vector<MakeLayer> layers;
    MakeModel model;
    bool portrait = false; // the SFF has one, 9000,1, as a stage's select screen shows it
};
// How far the camera goes: as long as every image covers the whole screen.
void Make_StageBounds(const MakeStage &s, int *left, int *right, int *high);
std::string Make_StageDef(const MakeStage &s);
