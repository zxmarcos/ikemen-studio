// Tests of story.cpp: a storyboard read as the engine reads it, its times, edits of its scenes.
#include "story.h"

#include <cmath>
#include <cstdio>
#include <string>

static int g_fails = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            g_fails++;                                                  \
        }                                                               \
    } while (0)

static const char kDef[] =
    "[Info]\n"
    "localcoord = 1280,720\n"
    "\n"
    "[SceneDef]\n"
    "spr = intro.sff\n"
    "startscene = 1 ; for debugging\n"
    "\n"
    "[Scene 0]\n"
    "clearcolor = 255,255,255\n"
    "layerall.pos = 160,0\n"
    "fadein.time = 60\n"
    "fadeout.time = 15\n"
    "fadeout.col = 255,128,0\n"
    "layer0.anim = 0\n"
    "layer0.offset = 0,32\n"
    "layer10.anim = 3\n"
    "layer2.text = \"Hello\\nthere\"\n"
    "layer2.font = 1,0,-1,10,20\n"
    "layer2.starttime = 30\n"
    "layer2.endtime = 90\n"
    "layer2.textdelay = 3\n"
    "sound0.value = 5,1\n"
    "sound0.starttime = 12\n"
    "end.time = 300\n"
    "\n"
    "; the next one\n"
    "[Scene 1]\n"
    "layer_0.spr = 1,2\n"
    "layer_0.velocity = 1,-2\n"
    "end.time = 100\n"
    "jump = 3\n"
    "\n"
    "[BG0]\n"
    "type = normal\n"
    "\n"
    "[Scene 2]\n"
    "clearcolor = -1\n"
    "bg.name = BG0\n"
    "end.time = 50\n"
    "\n"
    "[Scene 3]\n"
    "fadeout.time = 30\n";

static void Read(const std::string &text, IniFile &ini, StoryDoc &doc) {
    Ini_Parse(text, ini);
    Story_Read(ini, doc);
}

static void TestRead() {
    IniFile ini;
    StoryDoc doc;
    Read(kDef, ini, doc);
    CHECK(Story_Is(ini));
    CHECK(doc.coord[0] == 1280 && doc.coord[1] == 720 && doc.startScene == 1);
    CHECK(doc.scenes.size() == 4);
    const StoryScene &a = doc.scenes[0];
    CHECK(a.name == "Scene 0" && a.end == 300 && a.in.time == 60 && a.out.time == 15 && a.out.col[1] == 128);
    // As text: layer10 before layer2, the order they are drawn
    CHECK(a.layers.size() == 3 && a.layers[0].key == "layer0" && a.layers[1].key == "layer10" && a.layers[2].key == "layer2");
    CHECK(a.layers[0].anim == 0 && a.layers[0].offset[1] == 32);
    const StoryLayer &t = a.layers[2];
    CHECK(t.text == "Hello\nthere" && t.start == 30 && t.end == 90 && t.textDelay == 3);
    CHECK(t.font[0] == 1 && t.font[2] == -1 && t.font[3] == 10 && t.font[5] == 255); // those not written keep their default
    CHECK(a.sounds.size() == 1 && a.sounds[0].value[0] == 5 && a.sounds[0].start == 12);
    // Carried over; a scene's own value replaces it from there
    const StoryScene &b = doc.scenes[1], &c = doc.scenes[2];
    CHECK(b.clear[0] == 255 && b.layerall[0] == 160);
    CHECK(c.clear[0] == -1 && c.layerall[0] == 160 && c.bg == "bg0");
    CHECK(b.layers[0].key == "layer_0" && b.layers[0].number == 0 && b.layers[0].spr[1] == 2 && b.layers[0].velocity[1] == -2);
    CHECK(b.jump == 3);
    CHECK(!Story_Is(IniFile()));
}

static void TestTimes() {
    IniFile ini;
    StoryDoc doc;
    Read(kDef, ini, doc);
    CHECK(Story_Length(doc.scenes[0]) == 300);
    CHECK(Story_Length(doc.scenes[3]) == 30); // end.time 0: only its fade out
    std::vector<int> order = Story_Order(doc);
    CHECK(order.size() == 2 && order[0] == 1 && order[1] == 3); // from startscene, the jump taken
    CHECK(Story_Next(doc, 0) == 1 && Story_Next(doc, 1) == 3 && Story_Next(doc, 3) == -1);
    // A jump back ends the list
    StoryDoc loop = doc;
    loop.scenes[3].jump = 1;
    CHECK(Story_Order(loop).size() == 2);

    StoryLayer l;
    l.velocity[0] = 2, l.velocity[1] = -1;
    float at[2];
    Story_Motion(l, 10, at);
    CHECK(at[0] == 20 && at[1] == -10);
    l.friction[0] = 0.5f, l.velocity[0] = 8, l.friction[1] = 1, l.velocity[1] = 0, l.accel[1] = 1;
    Story_Motion(l, 3, at);
    CHECK(at[0] == 14); // 8 + 4 + 2
    CHECK(at[1] == 3);  // 0 + 1 + 2

    l.start = 10, l.end = 20;
    CHECK(!l.Visible(9) && l.Visible(10) && l.Visible(19) && !l.Visible(20));
    l.end = 0;
    CHECK(l.Visible(100000));
    l.textDelay = 2;
    CHECK(Story_Typed(l, 9, 5) == 0 && Story_Typed(l, 13, 5) == 2 && Story_Typed(l, 500, 5) == 5);
    l.textDelay = 0;
    CHECK(Story_Typed(l, 9, 5) == 5);

    int col[3] = {};
    const StoryScene &a = doc.scenes[0];
    CHECK(Story_FadeAlpha(a, 0, col) == 1 && Story_FadeAlpha(a, 30, col) == 0.5f && Story_FadeAlpha(a, 60, col) == 0);
    CHECK(Story_FadeAlpha(a, 200, col) == 0);
    CHECK(fabsf(Story_FadeAlpha(a, 299, col) - 14.0f / 15) < 0.001f && col[1] == 128);
}

static void TestEdits() {
    IniFile ini;
    StoryDoc doc;
    std::string text = kDef;
    Read(text, ini, doc);
    CHECK(Story_AddScene(text, ini, doc, 0));
    Read(text, ini, doc);
    CHECK(doc.scenes.size() == 5 && doc.scenes[1].name == "Scene 4" && doc.scenes[1].end == 300);
    CHECK(doc.scenes[0].layers.size() == 3 && doc.scenes[2].name == "Scene 1"); // the others as they were
    CHECK(text.find("; the next one") != std::string::npos);

    CHECK(Story_DuplicateScene(text, ini, doc, 0));
    Read(text, ini, doc);
    CHECK(doc.scenes.size() == 6 && doc.scenes[1].name == "Scene 5" && doc.scenes[1].layers.size() == 3);
    CHECK(doc.scenes[1].layers[2].text == "Hello\nthere");

    CHECK(Story_MoveScene(text, ini, doc, 2, 1)); // Scene 4 after Scene 1
    Read(text, ini, doc);
    CHECK(doc.scenes[2].name == "Scene 1" && doc.scenes[3].name == "Scene 4" && doc.scenes[2].jump == 3);
    CHECK(Story_MoveScene(text, ini, doc, 3, -1));
    Read(text, ini, doc);
    CHECK(doc.scenes[2].name == "Scene 4" && doc.scenes[3].name == "Scene 1");
    CHECK(!Story_MoveScene(text, ini, doc, 0, -1) && !Story_MoveScene(text, ini, doc, 5, 1));

    CHECK(Story_DeleteScene(text, ini, doc, 1));
    Read(text, ini, doc);
    CHECK(doc.scenes.size() == 5 && doc.scenes[1].name == "Scene 4");
    CHECK(Story_RemoveKeys(text, ini.sections[doc.scenes[0].section], "layer2."));
    Read(text, ini, doc);
    CHECK(doc.scenes[0].layers.size() == 2 && doc.scenes[0].end == 300);
    CHECK(Story_NewLayerKey(doc.scenes[0]) == "layer11");
    CHECK(!Story_RemoveKeys(text, ini.sections[doc.scenes[0].section], "layer9."));

    // A new text uses the font [SceneDef] has
    text = "[SceneDef]\nspr = a.sff\nfont3 = a.def\nfont1 = b.def\nfont1.height = 9\n\n[Scene 0]\nlayerall.pos = 20,10\nlayer4.anim = 1\nend.time = 9\n\n[Scene 1]\n";
    Read(text, ini, doc);
    CHECK(Story_AddText(text, ini, doc, 0));
    Read(text, ini, doc);
    CHECK(doc.scenes[0].layers.size() == 2 && doc.scenes[0].layers[1].key == "layer5");
    const StoryLayer added = doc.scenes[0].layers[1];
    CHECK(added.text == "Text" && added.font[0] == 1 && added.offset[0] == 140 && added.offset[1] == 110);
    CHECK(doc.scenes[1].layers.empty() && doc.scenes[0].end == 9);
    // None: one is added, in [SceneDef] or in a new one
    text = "[SceneDef]\nspr = a.sff\n\n[Scene 0]\nend.time = 9\n";
    Read(text, ini, doc);
    CHECK(Story_AddText(text, ini, doc, 0));
    CHECK(text.find("font0 = f-6x9.def") < text.find("[Scene 0]"));
    Read(text, ini, doc);
    CHECK(doc.scenes[0].layers.size() == 1 && doc.scenes[0].layers[0].font[0] == 0 && doc.scenes[0].layers[0].font[3] == 255);
    // Black on a light scene
    text = "[SceneDef]\nfont0 = a.def\n\n[Scene 0]\nclearcolor = 255,255,255\n";
    Read(text, ini, doc);
    CHECK(Story_AddText(text, ini, doc, 0));
    Read(text, ini, doc);
    CHECK(doc.scenes[0].layers[0].font[0] == 0 && doc.scenes[0].layers[0].font[3] == 0 && doc.scenes[0].layers[0].font[5] == 0);
    text = "[Info]\nlocalcoord = 1280,720\n\n[Scene 0]\nend.time = 9\n";
    Read(text, ini, doc);
    CHECK(Story_AddText(text, ini, doc, 0));
    CHECK(text.find("[SceneDef]\nfont0 = f-6x9.def\n\n[Scene 0]") != std::string::npos);
    Read(text, ini, doc);
    CHECK(doc.scenes[0].end == 9 && doc.scenes[0].layers[0].offset[0] == 640 && doc.coord[1] == 720);

    // A file without a line end at its end, and without scenes
    text = "[Scene 0]\nend.time = 5";
    Read(text, ini, doc);
    CHECK(Story_DuplicateScene(text, ini, doc, 0));
    Read(text, ini, doc);
    CHECK(doc.scenes.size() == 2 && doc.scenes[1].end == 5 && doc.scenes[1].name == "Scene 1");
    text = "[Info]\nlocalcoord = 320,240";
    Read(text, ini, doc);
    CHECK(Story_AddScene(text, ini, doc, -1));
    Read(text, ini, doc);
    CHECK(doc.scenes.size() == 1 && doc.scenes[0].name == "Scene 0" && doc.coord[1] == 240);
}

int main() {
    TestRead();
    TestTimes();
    TestEdits();
    if (g_fails == 0) printf("story_test: ok\n");
    return g_fails ? 1 : 0;
}
