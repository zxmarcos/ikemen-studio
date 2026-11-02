// Storyboards (intro, ending, logo...) as the engine reads them (storyboard.go): the scenes of a
// def in the order they play, their times and layers, and edits of its scenes in the text.
#pragma once

#include <string>
#include <vector>

#include "ini.h"

struct StoryFade {
    int time = 0;
    int col[3] = {0, 0, 0};
};

struct StoryLayer {
    std::string key; // "layer0", as the def's keys start, lower case
    int number = 0;
    int anim = -1;
    int spr[2] = {-1, 0};
    float offset[2] = {0, 0}, scale[2] = {1, 1};
    float velocity[2] = {0, 0}, accel[2] = {0, 0}, friction[2] = {1, 1};
    float angle = 0;
    int facing = 1, layerno = 2;
    std::string text;
    int font[8] = {-1, 0, 0, 255, 255, 255, 255, -1}; // index, bank, align, r, g, b, a, height
    float textDelay = 2;
    int start = 0, end = 0; // end 0: until its scene ends
    bool Visible(int counter) const { return counter >= start && (counter < end || end <= 0); }
};

struct StorySound {
    std::string key; // "sound0"
    int value[2] = {-1, 0};
    int start = 0;
};

struct StoryScene {
    int section = 0; // its index in the IniFile
    std::string name;
    int end = 0;     // end.time
    StoryFade in, out;
    int clear[3] = {-1, 0, 0}; // clearcolor, carried over from the scene before; -1: not cleared
    int clearLayer = 0;
    float layerall[2] = {0, 0}; // the same
    std::string bg;  // bg.name, lower case
    int jump = -1;   // the next scene's index, -1 the one after it
    std::vector<StoryLayer> layers; // in the order they are drawn
    std::vector<StorySound> sounds;
};

struct StoryDoc {
    int startScene = 0;
    int coord[2] = {320, 240};
    std::vector<StoryScene> scenes; // in the order of the def: their indices are the engine's
};

void Story_Read(const IniFile &ini, StoryDoc &out);
// A def holding scenes
bool Story_Is(const IniFile &ini);

int Story_Length(const StoryScene &s);                  // ticks it lasts: end.time, or its fade out
std::vector<int> Story_Order(const StoryDoc &doc);      // the scenes played, from its first, jumps taken, once each
int Story_Next(const StoryDoc &doc, int scene);         // the scene after it, -1 the storyboard ended
void Story_Motion(const StoryLayer &l, int ticks, float out[2]); // how far velocity, accel and friction took it
int Story_Typed(const StoryLayer &l, int counter, int chars);    // how much of its text shows at that time
float Story_FadeAlpha(const StoryScene &s, int counter, int col[3]); // the scene's fade over the picture, 0 none

// Edits of the text, each from the IniFile and StoryDoc read from it. New scenes take the next free number.
bool Story_AddScene(std::string &text, const IniFile &ini, const StoryDoc &doc, int after); // -1: the first
bool Story_DuplicateScene(std::string &text, const IniFile &ini, const StoryDoc &doc, int scene);
bool Story_DeleteScene(std::string &text, const IniFile &ini, const StoryDoc &doc, int scene);
bool Story_MoveScene(std::string &text, const IniFile &ini, const StoryDoc &doc, int scene, int dir); // -1 earlier, 1 later
std::string Story_NewLayerKey(const StoryScene &s);     // "layer3": a number the scene does not use
// A new layer of text, in the middle of the picture. The game draws no text without a font: the first of
// [SceneDef], else f-6x9 added there.
bool Story_AddText(std::string &text, const IniFile &ini, const StoryDoc &doc, int scene);
// A new layer showing that action, at the top of the middle of the picture, where an image's axis puts it
bool Story_AddAnim(std::string &text, const IniFile &ini, const StoryDoc &doc, int scene, int action);
bool Story_RemoveKeys(std::string &text, const IniSection &s, const std::string &prefix); // every field of "layer0."
