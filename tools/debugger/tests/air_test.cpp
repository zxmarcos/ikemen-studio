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

// A stage's or a screen's def: actions among other sections, which edits of the actions leave as they are.
static const char *kDef =
    "[Info]\n"
    "name = \"x\"\n"
    "\n"
    "[Begin Action 10] ; flag\n"
    "10,0, 0,0, 5\n"
    "10,1, 0,0, 5\n"
    "\n"
    ";the BG of the flag\n"
    "[BG Flag]\n"
    "type = anim\n"
    "actionno = 10\n"
    "\n"
    "[Begin Action 20]\n"
    "20,0, 0,0, -1\n"
    "\n"
    "[BGCtrlDef x]\n"
    "ctrlid = 1\n";

static void Def() {
    static const char *const kKept[] = {"[Info]", "name = \"x\"", "[BG Flag]", "actionno = 10", "[BGCtrlDef x]", "ctrlid = 1"};
    auto kept = [&](const std::string &text) {
        for (const char *line : kKept)
            if (text.find(line) == std::string::npos) return false;
        return true;
    };
    AirFile air;
    Air_Parse(kDef, air);
    CHECK(air.actions.size() == 2);
    CHECK(air.actions[10].elems.size() == 2 && air.actions[10].last == 6 && air.actions[10].name == "flag");
    CHECK(air.actions[20].elems.size() == 1 && air.actions[20].last == 14);

    std::string text = kDef;
    CHECK(Air_NewAction(text, 30, "new", {AirNewElem()}));
    CHECK(kept(text));
    Air_Parse(text, air);
    CHECK(air.actions.size() == 3 && air.actions[30].elems.size() == 1 && air.actions[30].name == "new");
    CHECK(air.actions[20].last == 14); // the section before it not taken in

    text = kDef;
    Air_Parse(text, air);
    CHECK(Air_CopyAction(text, air.actions[10], 11));
    CHECK(kept(text));
    Air_Parse(text, air);
    CHECK(air.actions[11].elems.size() == 2 && air.actions[11].elems[1].image == 1);

    text = kDef;
    Air_Parse(text, air);
    CHECK(Air_DeleteAction(text, air.actions[10]));
    CHECK(kept(text));
    CHECK(text.find("10,0") == std::string::npos && text.find("20,0") != std::string::npos);
    Air_Parse(text, air);
    CHECK(air.actions.size() == 1);

    text = kDef;
    Air_Parse(text, air);
    CHECK(Air_RenumberAction(text, air.actions[20], 21));
    CHECK(kept(text));
    CHECK(Air_DuplicateElems(text, air.actions[10], {0, 1}));
    CHECK(kept(text));
    Air_Parse(text, air);
    CHECK(air.actions[10].elems.size() == 4 && air.actions.count(21) == 1);
}

int main() {
    Def();
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

    // Flip, blending, scale, angle: read as anim.go does, written without the defaults
    {
        AirFile st;
        Air_Parse("[Begin Action 1]\n0,0, 0,0, 1, HV, AS128D64, 1.5, 2, 30\n0,1, 0,0, 1,, A1\n0,2, 0,0, 1,, as\n0,3, 0,0, 1,, S\n0,4, 0,0, 1,, SS200\n", st);
        const auto &e = st.actions[1].elems;
        CHECK(e[0].style.flipH && e[0].style.flipV && e[0].style.blend == kAirAdd && e[0].style.src == 128 && e[0].style.dst == 64);
        CHECK(e[0].style.xscale == 1.5f && e[0].style.yscale == 2 && e[0].style.angle == 30);
        CHECK(e[1].style.blend == kAirAdd && e[1].style.src == 255 && e[1].style.dst == 128 && !e[1].style.flipH);
        CHECK(e[2].style.blend == kAirAdd && e[2].style.src == 255 && e[2].style.dst == 0); // "as" alone: Mugen's defaults
        CHECK(e[3].style.blend == kAirSub && e[3].style.src == 255 && e[3].style.dst == 255);
        CHECK(e[4].style.blend == kAirSub && e[4].style.src == 200 && e[4].style.dst == 255);
        std::string t = "[Begin Action 1]\n0,0, 3,4, 5 ; note\n";
        Air_Parse(t, st);
        AirStyle s;
        s.flipH = true, s.blend = kAirSub, s.src = 100, s.dst = 255, s.angle = 45;
        CHECK(Air_SetStyle(t, st.actions[1].elems[0], s));
        CHECK(Line(t, 2) == "0,0, 3,4, 5, H, SS100D255, , , 45 ; note");
        Air_Parse(t, st);
        CHECK(st.actions[1].elems[0].style.angle == 45 && st.actions[1].elems[0].style.xscale == 1);
        CHECK(Air_SetStyle(t, st.actions[1].elems[0], AirStyle()));
        CHECK(Line(t, 2) == "0,0, 3,4, 5 ; note");
    }

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

    // Several elements: the copies after the last one, in order; each keeps its own boxes
    text = kAir;
    Air_Parse(text, air);
    CHECK(Air_DuplicateElems(text, air.actions[0], {1, 2}));
    Air_Parse(text, air);
    CHECK(air.actions[0].elems.size() == 6);
    CHECK(air.actions[0].elems[3].image == 1 && air.actions[0].elems[4].image == 2 && air.actions[0].elems[5].image == 3);
    CHECK(air.actions[0].elems[4].own[0] && air.actions[0].elems[4].clsn[0].size() == 1);
    CHECK(air.actions[0].loopStart == 5);
    CHECK(Air_DeleteElems(text, air.actions[0], {3, 4}));
    Air_Parse(text, air);
    CHECK(air.actions[0].elems.size() == 4 && air.actions[0].elems[2].image == 2 && air.actions[0].elems[3].image == 3);
    CHECK(air.actions[0].loopStart == 3);
    CHECK(air.actions[5].elems.size() == 1);
    CHECK(!Air_DeleteElems(text, air.actions[0], {}));
    CHECK(!Air_DuplicateElems(text, air.actions[0], {9}));

    // Interpolate lines belong to the element after them, and go with it
    static const char *kInterp =
        "[Begin Action 100]\n"
        "0,0, 0,0, 5\n"
        "Interpolate Offset\n"
        "Interpolate Scale\n"
        "0,1, 10,0, 5\n"
        "0,2, 20,0, 5\n"
        "\n"
        "[Begin Action 101]\n"
        "Copy Action 100\n";
    text = kInterp;
    Air_Parse(text, air);
    CHECK(air.actions[100].elems[1].interp == (kAirInterpOffset | kAirInterpScale));
    CHECK(air.actions[100].elems[0].interp == 0 && air.actions[100].elems[2].interp == 0);
    CHECK(air.actions[101].copyOf == 100 && air.actions[100].copyOf == -1);
    CHECK(Air_MoveElem(text, air.actions[100], 1, 1));
    Air_Parse(text, air);
    CHECK(air.actions[100].elems[1].image == 2 && air.actions[100].elems[1].interp == 0);
    CHECK(air.actions[100].elems[2].image == 1 && air.actions[100].elems[2].interp == (kAirInterpOffset | kAirInterpScale));
    CHECK(Air_DeleteElem(text, air.actions[100], 2));
    Air_Parse(text, air);
    CHECK(text.find("Interpolate") == std::string::npos);
    CHECK(Air_SetInterpolate(text, air.actions[100].elems[1], kAirInterpAngle | kAirInterpBlend));
    Air_Parse(text, air);
    CHECK(air.actions[100].elems[1].interp == (kAirInterpAngle | kAirInterpBlend));
    CHECK(Air_SetInterpolate(text, air.actions[100].elems[1], kAirInterpOffset));
    Air_Parse(text, air);
    CHECK(air.actions[100].elems[1].interp == kAirInterpOffset && air.actions[100].elems.size() == 2);
    CHECK(Air_SetInterpolate(text, air.actions[100].elems[1], 0));
    CHECK(text.find("Interpolate") == std::string::npos);

    // New actions and elements, names
    text = kAir;
    Air_Parse(text, air);
    CHECK(air.actions[5].name == "the next one" && air.actions[0].name == "comment");
    CHECK(Air_NewAction(text, 200, "Punch", {{10, 0, 0, 0, 4}, {10, 1, 0, 0, 6}}));
    Air_Parse(text, air);
    CHECK(air.actions[200].name == "Punch" && air.actions[200].elems.size() == 2 && air.actions[200].elems[1].time == 6);
    CHECK(Air_InsertElems(text, air.actions[200], 0, {{10, 9, 1, 2, 3}}));
    Air_Parse(text, air);
    CHECK(air.actions[200].elems.size() == 3 && air.actions[200].elems[1].image == 9 && air.actions[200].elems[2].image == 1);
    CHECK(Air_InsertElems(text, air.actions[200], -1, {{10, 8, 0, 0, 3}}));
    Air_Parse(text, air);
    CHECK(air.actions[200].elems[0].image == 8);
    CHECK(Air_SetActionName(text, air.actions[200], "Jab"));
    Air_Parse(text, air);
    CHECK(air.actions[200].name == "Jab");
    CHECK(Air_SetActionName(text, air.actions[0], "Stand"));
    Air_Parse(text, air);
    CHECK(air.actions[0].name == "Stand" && air.actions[0].elems.size() == 4);
    CHECK(Air_SetActionName(text, air.actions[0], ""));
    Air_Parse(text, air);
    CHECK(air.actions[0].name == "");

    // From another file
    {
        std::string other = kAir;
        AirFile from;
        Air_Parse(other, from);
        text = "[Begin Action 1]\n1,1, 0,0, 1\n";
        CHECK(Air_CopyActionFrom(text, other, from.actions[5], 9));
        Air_Parse(text, air);
        CHECK(air.actions[9].elems.size() == 1 && air.actions[9].elems[0].group == 5 && air.actions[9].name == "the next one");
    }

    // Scaled: offsets and boxes, a default group once
    text = kAir;
    Air_Parse(text, air);
    CHECK(Air_ScaleAction(text, air.actions[0], 2, 2));
    Air_Parse(text, air);
    CHECK(air.actions[0].elems[0].clsn[1][0].x1 == -26 && air.actions[0].elems[0].clsn[1][1].y1 == -186);
    CHECK(air.actions[0].elems[2].clsn[0][0].x2 == 80);

    // Optimize: boxes alike become the default; not when one element differs
    static const char *kSame =
        "[Begin Action 7]\n"
        "Clsn2: 1\n"
        " Clsn2[0] = 0,0,10,10\n"
        "1,0, 0,0, 3\n"
        "Clsn2: 1\n"
        " Clsn2[0] = 0,0,10,10\n"
        "1,1, 0,0, 3\n"
        "Clsn2: 1\n"
        " Clsn2[0] = 0,0,10,10\n"
        "1,2, 0,0, 3\n";
    text = kSame;
    Air_Parse(text, air);
    CHECK(Air_Optimize(text, air.actions[7]) == 4);
    Air_Parse(text, air);
    CHECK(air.actions[7].elems.size() == 3);
    CHECK(air.actions[7].elems[2].clsn[1].size() == 1 && !air.actions[7].elems[2].own[1] && air.actions[7].elems[2].clsn[1][0].x2 == 10);
    CHECK(Air_Optimize(text, air.actions[7]) == 0);
    text = kAir;
    Air_Parse(text, air);
    CHECK(Air_Optimize(text, air.actions[0]) == 0);

    printf(g_failed ? "air-test: %d failed\n" : "air-test: all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
