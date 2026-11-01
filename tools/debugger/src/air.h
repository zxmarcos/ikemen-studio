// AIR files (character animations): the actions, their elements and Clsn boxes
// with the lines they are on, and edits of the text that keep everything else.
// Boxes follow Ikemen's reading (anim.go): an element takes the "ClsnN:" or
// "ClsnNDefault:" group read since the element before it, else the last
// "ClsnNDefault:" group of the action.
#pragma once

#include <map>
#include <string>
#include <vector>

struct AirBox {
    int line = 0; // 1-based
    int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
};

enum AirBlend { kAirOpaque, kAirAdd, kAirSub, kAirSubAdd };

// What follows the five values of an element, as anim.go reads it: flip, blending, scales, angle.
struct AirStyle {
    bool flipH = false, flipV = false;
    int blend = kAirOpaque;
    int src = 255, dst = 255; // the blending's alphas, 0..255
    float xscale = 1, yscale = 1, angle = 0;
    bool Plain() const { return !flipH && !flipV && blend == kAirOpaque && xscale == 1 && yscale == 1 && angle == 0; }
};

struct AirElem {
    int line = 0;
    int first = 0; // its first line: its own box groups come before it
    int group = 0, image = 0, x = 0, y = 0, time = 0;
    AirStyle style;
    std::vector<AirBox> clsn[2]; // Clsn1, Clsn2 in effect for this element
    int header[2] = {};          // line of the "ClsnN: n" / "ClsnNDefault: n" they come from, 0: none
    bool own[2] = {};            // that header is the element's own (not a default)
};

struct AirAction {
    int no = 0;
    int line = 0, last = 0; // of "[Begin Action]" and its last line
    int loopStart = -1;     // element index
    std::vector<AirElem> elems;
};

struct AirFile {
    std::map<int, AirAction> actions;
};

void Air_Parse(const std::string &text, AirFile &out);

// Edits ('\n' line ends). Each returns false when it could not be done.
bool Air_SetBox(std::string &text, const AirBox &box, int x1, int y1, int x2, int y2);
// Gives the element its own group of boxes (written before its line, replacing an own group).
bool Air_SetOwnBoxes(std::string &text, const AirElem &elem, int group, const std::vector<AirBox> &boxes);
bool Air_SetElem(std::string &text, const AirElem &elem, int group, int image, int x, int y, int time);
// Writes the element's flip, blending, scales and angle after its five values (a plain style writes nothing),
// keeping the five values and the comment.
bool Air_SetStyle(std::string &text, const AirElem &elem, const AirStyle &style);
// The text of the blending field ("", "A", "AS128D64", "SS200D255"...) and the style read from the fields
std::string Air_BlendText(const AirStyle &style);

// Elements and actions. An element moves with its own box groups.
bool Air_DuplicateElem(std::string &text, const AirAction &act, int index); // the copy goes after it
bool Air_DeleteElem(std::string &text, const AirAction &act, int index);
bool Air_MoveElem(std::string &text, const AirAction &act, int index, int dir); // -1 earlier, 1 later
bool Air_SetLoopStart(std::string &text, const AirAction &act, int index);      // -1: none
bool Air_CopyAction(std::string &text, const AirAction &act, int newNo);        // at the end of the file
bool Air_DeleteAction(std::string &text, const AirAction &act);
bool Air_RenumberAction(std::string &text, const AirAction &act, int newNo);
