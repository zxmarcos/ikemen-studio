// Tests of src/air.cpp: parsing AIR text and the edits of it.
// Built as air-test; scripts/build.ps1 test builds and runs it.
#include "air.h"

#include <cstdio>
#include <string>

static int g_failed;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failed++;                                                 \
        }                                                               \
    } while (0)

static const char *kAir =
    "; comment\n"
    "[Begin Action 0]\n"
    "Clsn2Default: 2\n"
    " Clsn2[0] = -13,-79,16,0\n"
    " Clsn2[1] =  -7,-93, 5,-79\n"
    "0,0, 0,0, 7\n"
    "0,1, 0,0, 7\n"
    "Clsn1: 1\n"
    " Clsn1[0] = 10,-80,40,-70\n"
    "0,2, 0,0, 7, H\n"
    "Loopstart\n"
    "0,3, 0,0, 7 ; last\n"
    "\n"
    "; the next one\n"
    "[Begin Action 5]\n"
    "5,0, 0,0, -1\n";

static std::string Line(const std::string &text, int n) {
    size_t start = 0;
    for (int i = 1; i < n; i++) start = text.find('\n', start) + 1;
    return text.substr(start, text.find('\n', start) - start);
}

int main() {
    AirFile air;
    Air_Parse(kAir, air);
    CHECK(air.actions.size() == 2);
    const AirAction &a0 = air.actions[0];
    CHECK(a0.line == 2 && a0.last == 12);
    CHECK(a0.elems.size() == 4);
    CHECK(a0.loopStart == 3);
    CHECK(a0.elems[0].clsn[1].size() == 2 && !a0.elems[0].own[1] && a0.elems[0].header[1] == 3);
    CHECK(a0.elems[0].clsn[1][1].y1 == -93 && a0.elems[0].clsn[1][1].y2 == -79);
    CHECK(a0.elems[1].clsn[0].empty());
    CHECK(a0.elems[2].clsn[0].size() == 1 && a0.elems[2].own[0] && a0.elems[2].header[0] == 8);
    CHECK(a0.elems[2].first == 8 && a0.elems[2].line == 10);
    CHECK(a0.elems[2].clsn[1].size() == 2); // the default still
    CHECK(a0.elems[3].clsn[0].empty());    // an own group is for one element
    CHECK(air.actions[5].elems.size() == 1 && air.actions[5].elems[0].time == -1);

    // A box rewritten in place
    std::string text = kAir;
    CHECK(Air_SetBox(text, a0.elems[0].clsn[1][1], -8, -100, 6, -79));
    CHECK(Line(text, 5) == " Clsn2[1] = -8, -100, 6, -79");

    // An element's own boxes: before it; the other elements keep the default
    text = kAir;
    CHECK(Air_SetOwnBoxes(text, a0.elems[1], 1, {AirBox{0, 1, 2, 3, 4}}));
    Air_Parse(text, air);
    CHECK(air.actions[0].elems[1].own[1] && air.actions[0].elems[1].clsn[1].size() == 1);
    CHECK(air.actions[0].elems[0].clsn[1].size() == 2 && air.actions[0].elems[2].clsn[1].size() == 2);
    // ... and replacing them again leaves one group
    CHECK(Air_SetOwnBoxes(text, air.actions[0].elems[1], 1, {}));
    Air_Parse(text, air);
    CHECK(air.actions[0].elems[1].own[1] && air.actions[0].elems[1].clsn[1].empty());
    CHECK(text.find("Clsn2: 1") == std::string::npos);

    // Element line: the values change, the flags stay
    text = kAir;
    Air_Parse(text, air);
    CHECK(Air_SetElem(text, air.actions[0].elems[2], 0, 2, 5, -3, 4));
    CHECK(Line(text, 10) == "0,2, 5,-3, 4, H");
    CHECK(Air_SetElem(text, air.actions[0].elems[3], 0, 3, 1, 1, 9));
    CHECK(Line(text, 12) == "0,3, 1,1, 9 ; last");

    // Duplicate, move, delete: an element keeps its own boxes
    text = kAir;
    Air_Parse(text, air);
    CHECK(Air_DuplicateElem(text, air.actions[0], 2));
    Air_Parse(text, air);
    CHECK(air.actions[0].elems.size() == 5);
    CHECK(air.actions[0].elems[3].own[0] && air.actions[0].elems[3].clsn[0].size() == 1);
    CHECK(Air_MoveElem(text, air.actions[0], 2, -1));
    Air_Parse(text, air);
    CHECK(air.actions[0].elems[1].image == 2 && air.actions[0].elems[1].own[0]);
    CHECK(air.actions[0].elems[2].image == 1 && air.actions[0].elems[2].clsn[0].empty());
    CHECK(Air_DeleteElem(text, air.actions[0], 1));
    Air_Parse(text, air);
    CHECK(air.actions[0].elems.size() == 4 && air.actions[0].elems[1].image == 1);
    CHECK(air.actions[0].elems[2].own[0]); // the duplicate is still there

    // Loop start: only one
    text = kAir;
    Air_Parse(text, air);
    CHECK(Air_SetLoopStart(text, air.actions[0], 1));
    Air_Parse(text, air);
    CHECK(air.actions[0].loopStart == 1);
    CHECK(Air_SetLoopStart(text, air.actions[0], -1));
    Air_Parse(text, air);
    CHECK(air.actions[0].loopStart == -1);

    // Actions: copy, renumber, delete
    text = kAir;
    Air_Parse(text, air);
    CHECK(Air_CopyAction(text, air.actions[0], 10));
    Air_Parse(text, air);
    CHECK(air.actions.count(10) && air.actions[10].elems.size() == 4 && air.actions[10].loopStart == 3);
    CHECK(Air_RenumberAction(text, air.actions[5], 6));
    Air_Parse(text, air);
    CHECK(!air.actions.count(5) && air.actions.count(6));
    CHECK(Air_DeleteAction(text, air.actions[6]));
    Air_Parse(text, air);
    CHECK(!air.actions.count(6) && air.actions.count(0) && air.actions.count(10));

    printf(g_failed ? "air-test: %d failed\n" : "air-test: all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
