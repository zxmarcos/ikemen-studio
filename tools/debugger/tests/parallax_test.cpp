// Tests of src/parallax.cpp: stages written here, on sprites made up, against the camera.
#include "parallax.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "air.h"
#include "ini.h"
#include "sff.h"

static int g_failed;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failed++;                                                 \
        }                                                               \
    } while (0)

#define NEAR(a, b) CHECK(std::fabs((a) - (b)) < 0.01f)

static SffDoc Sprites() {
    SffDoc doc;
    auto add = [&](int group, int number, int w, int h, int x, int y) {
        SffSprite s;
        s.group = group, s.number = number, s.w = w, s.h = h, s.x = x, s.y = y;
        doc.sprites.push_back(s);
    };
    add(0, 0, 640, 240, 320, 0); // wide, its axis in the middle
    add(0, 1, 100, 240, 0, 0);   // a narrow one, its axis on its left
    add(0, 2, 200, 40, 100, 0);  // a floor
    add(0, 3, 50, 240, 0, 25);   // stage0's wall
    add(0, 4, 35, 57, 0, 0);     // and its floor
    return doc;
}

static const char *kCamera = "[StageInfo]\nlocalcoord = 320, 240\nzoffset = 200\n[Camera]\nboundleft = -160\nboundright = 160\n";

struct Parsed {
    IniFile ini;
    AirFile air;
    SffDoc sff = Sprites();
    std::vector<ParallaxBg> bgs;
    ParallaxView view;
    explicit Parsed(const std::string &text) {
        Ini_Parse(text, ini);
        Air_Parse(text, air);
        bgs = Parallax_Bgs(ini, air, sff);
        view = Parallax_ViewOf(ini);
    }
};

static bool Has(const std::vector<StageFinding> &f, StageFinding::Level level, const char *part) {
    for (const auto &x : f)
        if (x.level == level && x.text.find(part) != std::string::npos) return true;
    return false;
}

static void TestView() {
    Parsed p(std::string(kCamera) + "zoomout = 0.5\nboundhigh = -40\n");
    NEAR(p.view.width, 320);
    NEAR(p.view.height, 240);
    NEAR(p.view.left, -160);
    NEAR(p.view.right, 160);
    NEAR(p.view.high, -40);
    NEAR(p.view.zoffset, 200);
    NEAR(p.view.minScale, 0.5f); // 320 / (320 + 320)
    Parsed q(std::string(kCamera) + "zoomout = 0.25\n");
    NEAR(q.view.minScale, 0.5f); // no further than the whole stage on screen
    CHECK(Parsed("[StageInfo]\nlocalcoord = 1280, 720\n").view.zoffset > 590);
}

static void TestBgs() {
    Parsed p(std::string(kCamera) +
             "[BG a]\ntype = normal\nspriteno = 0, 0\nstart = 10, 5\ndelta = .5\n"
             "[BG b]\ntype = normal\nspriteno = 0, 1\npositionlink = 1\nstart = 3, 4\n"
             "[BG c]\ntype = dummy\n"
             "[BG d]\ntype = normal\nspriteno = 9, 9\n"
             "[BG e]\ntype = anim\nactionno = 5\ntrans = add\n"
             "[BG f]\ntype = parallax\nspriteno = 0, 2\nstart = 0, 200\nwidth = 300, 900\nyscalestart = 50\n"
             "[BG g]\ntype = normal\nspriteno = 0, 1\ntile = 1, 0\nscalestart = 2, 1\n"
             "[Begin Action 5]\n0, 1, 0, 0, 10\n");
    CHECK(p.bgs.size() == 5); // the dummy and the sprite that is not there are out
    if (p.bgs.size() != 5) return;
    const ParallaxBg &a = p.bgs[0], &b = p.bgs[1], &e = p.bgs[2], &f = p.bgs[3], &g = p.bgs[4];
    CHECK(a.index == 0);
    NEAR(a.left, 10 - 320);
    NEAR(a.top, 5);
    NEAR(a.delta[0], 0.5f);
    NEAR(a.delta[1], 1); // left out, it keeps its default
    CHECK(b.linked);
    NEAR(b.left, 13); // its own start on the one it follows, and that one's delta
    NEAR(b.top, 9);
    NEAR(b.delta[0], 0.5f);
    CHECK(e.index == 4 && !e.solid); // an action's sprite, blended
    NEAR(e.w, 100);
    NEAR(f.w, 300);
    NEAR(f.widen, 3);
    NEAR(f.left, -150);
    NEAR(f.h, 80); // yscalestart 50 doubles it
    CHECK(f.tile[0] == 0);
    CHECK(g.tile[0] == 1);
    NEAR(g.w, 200);
    NEAR(g.step[0], 200);
}

static void TestCovers() {
    Parsed p(std::string(kCamera) + "[BG a]\ntype = normal\nspriteno = 0, 0\ndelta = 1\n");
    const ParallaxBg &b = p.bgs[0];
    ParallaxStop rest{"", 0, 0, 1}, right{"", 160, 0, 1};
    CHECK(Parallax_Covers(b, p.view, rest, -159, 10));
    CHECK(Parallax_Covers(b, p.view, rest, 159, 239));
    CHECK(!Parallax_Covers(b, p.view, rest, 0, 241));
    CHECK(Parallax_Covers(b, p.view, right, 159, 10)); // it ends at 160 exactly
    ParallaxStop further{"", 170, 0, 1};
    CHECK(!Parallax_Covers(b, p.view, further, 155, 10));
    // Up high a BG with delta 1 comes down, and its top leaves the screen's
    ParallaxStop high{"", 0, -30, 1};
    CHECK(!Parallax_Covers(b, p.view, high, 0, 20));
    CHECK(Parallax_Covers(b, p.view, high, 0, 40));

    // Delta 0 stays where it is, at any scale
    Parsed q(std::string(kCamera) + "[BG a]\ntype = normal\nspriteno = 0, 1\nstart = -50, 0\ndelta = 0, 0\n");
    ParallaxStop zoomed{"", 100, 0, 0.5f};
    CHECK(Parallax_Covers(q.bgs[0], q.view, zoomed, -49, 10));
    CHECK(!Parallax_Covers(q.bgs[0], q.view, zoomed, 51, 10));
    // Delta 1 shrinks with the camera
    Parsed r(std::string(kCamera) + "[BG a]\ntype = normal\nspriteno = 0, 1\nstart = -50, 0\n");
    ParallaxStop half{"", 0, 0, 0.5f};
    CHECK(Parallax_Covers(r.bgs[0], r.view, half, 24, 150));
    CHECK(!Parallax_Covers(r.bgs[0], r.view, half, 26, 150));

    // A floor is narrower at its top, and scrolls more at its bottom
    Parsed f(std::string(kCamera) + "[BG a]\ntype = parallax\nspriteno = 0, 2\nstart = 0, 200\nwidth = 200, 600\n");
    const ParallaxBg &floor = f.bgs[0];
    CHECK(!Parallax_Covers(floor, f.view, rest, 120, 200));
    CHECK(Parallax_Covers(floor, f.view, rest, 120, 239));
    ParallaxStop along{"", 50, 0, 1};
    CHECK(Parallax_Covers(floor, f.view, along, -149, 200));
    CHECK(!Parallax_Covers(floor, f.view, along, -151, 200));
    CHECK(Parallax_Covers(floor, f.view, along, -400, 239.9f));
    CHECK(!Parallax_Covers(floor, f.view, along, -451, 239.9f));

    // Tiles
    Parsed t(std::string(kCamera) +
             "[BG a]\ntype = normal\nspriteno = 0, 1\ntile = 1, 0\n[BG b]\ntype = normal\nspriteno = 0, 1\ntile = 3, 0\ntilespacing = 10, 0\n");
    CHECK(Parallax_Covers(t.bgs[0], t.view, rest, -5000, 10));
    CHECK(Parallax_Covers(t.bgs[1], t.view, rest, 319, 10));
    CHECK(!Parallax_Covers(t.bgs[1], t.view, rest, 321, 10));
    CHECK(!Parallax_Covers(t.bgs[1], t.view, rest, -1, 10));
}

static void TestFit() {
    Parsed p(std::string(kCamera) + "[BG a]\ntype = normal\nspriteno = 0, 0\ndelta = 2\n");
    NEAR(Parallax_FitDelta(p.bgs[0], p.view), 1); // (640 - 320) / 320
    NEAR(Parallax_WidthFor(p.view, 0.5f), 480);
    NEAR(Parallax_WidthFor(p.view, 0), 320);
    // Off centre: the nearer edge says
    Parsed q(std::string(kCamera) + "[BG a]\ntype = normal\nspriteno = 0, 0\nstart = 100, 0\n");
    NEAR(Parallax_FitDelta(q.bgs[0], q.view), 0.375f); // its left is at -220, 60 past the screen's: 60 / 160
    // Narrower than the screen: no delta helps
    Parsed r(std::string(kCamera) + "[BG a]\ntype = normal\nspriteno = 0, 1\n");
    CHECK(Parallax_FitDelta(r.bgs[0], r.view) < 0);
    // A camera that does not move leaves any delta good, which says nothing
    Parsed s("[BG a]\ntype = normal\nspriteno = 0, 0\n");
    CHECK(Parallax_FitDelta(s.bgs[0], s.view) < 0);
}

static void TestFloor() {
    Parsed p(std::string(kCamera) + "[BG a]\ntype = parallax\nspriteno = 0, 2\nstart = 0, 180\nwidth = 200, 600\ndelta = .5\n");
    const ParallaxBg &b = p.bgs[0];
    ParallaxStop rest{"", 0, 0, 1}, along{"", 100, 0, 1};
    ParallaxShape s = Parallax_Shape(b, p.view, rest);
    NEAR(s.top, 180);
    NEAR(s.bottom, 220);
    NEAR(s.topLeft, -100);
    NEAR(s.topRight, 100);
    NEAR(s.bottomLeft, -300);
    NEAR(s.bottomRight, 300);
    s = Parallax_Shape(b, p.view, along);
    NEAR(s.topLeft, -150); // half the camera's 100
    NEAR(s.bottomLeft, -450);
    // The ground, at 200, is half way down: the row there is twice the top's, so half the delta
    NEAR(Parallax_FloorDelta(b, p.view), 0.5f);
    NEAR(Parallax_GroundRow(b, p.view), 0.5f);
    NEAR(Parallax_GroundDelta(b, p.view), 1); // .5 at its top, twice that half way
    // xscale slants it instead: not wider, but its rows scroll as a width's do
    Parsed x(std::string(kCamera) + "[BG a]\ntype = parallax\nspriteno = 0, 2\nstart = 0, 180\nxscale = 1, 3\ndelta = .4\n");
    NEAR(x.bgs[0].widen, 1);
    NEAR(x.bgs[0].rate, 3);
    NEAR(Parallax_FloorDelta(x.bgs[0], x.view), 0.5f);
    NEAR(Parallax_GroundDelta(x.bgs[0], x.view), 0.8f);
    // A width wins over an xscale, as the engine reads them
    Parsed w(std::string(kCamera) + "[BG a]\ntype = parallax\nspriteno = 0, 2\nwidth = 200, 400\nxscale = 1, 3\n");
    NEAR(w.bgs[0].rate, 2);
    Parsed q(std::string(kCamera) + "[BG a]\ntype = parallax\nspriteno = 0, 2\nstart = 0, 300\nwidth = 200, 600\n");
    CHECK(Parallax_FloorDelta(q.bgs[0], q.view) < 0); // the ground is above it
}

static void TestSeam() {
    const int w = 40, h = 20;
    std::vector<unsigned char> img((size_t)w * h * 4);
    auto paint = [&](auto colour) {
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                unsigned char *p = &img[((size_t)y * w + x) * 4];
                int v = colour(x, y);
                p[0] = p[1] = p[2] = (unsigned char)std::min(v, 255), p[3] = v < 0 ? 0 : 255;
            }
    };
    // Flat, and a wave that comes back to where it started: no join
    paint([](int, int) { return 100; });
    CHECK(!Parallax_Seam(img.data(), w, h, 0));
    paint([&](int x, int) { return 128 + (int)(100 * sinf(x * 6.2832f / w)); });
    CHECK(!Parallax_Seam(img.data(), w, h, 0));
    // A ramp: dark meets bright where it repeats, across and not down
    paint([&](int x, int) { return x * 255 / (w - 1); });
    CHECK(Parallax_Seam(img.data(), w, h, 0));
    CHECK(!Parallax_Seam(img.data(), w, h, 1));
    // Down too
    paint([&](int, int y) { return y * 255 / (h - 1); });
    CHECK(Parallax_Seam(img.data(), w, h, 1));
    // Noise changes as much inside as at its edges: it hides a join
    paint([&](int x, int y) { return (x * 37 + y * 91) % 2 ? 40 : 200; });
    CHECK(!Parallax_Seam(img.data(), w, h, 0));
    // A grid: a line along an edge is the picture, as the release's stages have them
    paint([&](int x, int) { return x == 0 ? 220 : 60; });
    CHECK(!Parallax_Seam(img.data(), w, h, 0));
    // A cut-out against nothing at one edge
    paint([&](int x, int) { return x < 3 ? -1 : 100; });
    CHECK(Parallax_Seam(img.data(), w, h, 0));
    // All transparent at both edges
    paint([&](int x, int) { return x < 3 || x >= w - 3 ? -1 : 100; });
    CHECK(!Parallax_Seam(img.data(), w, h, 0));
    // Too small to say
    CHECK(!Parallax_Seam(img.data(), 2, h, 0));
}

// stage0's two planes: a wall with a line every 25, a floor with one every 35 at its top
static void TestLineUp() {
    auto picture = [](int w, int h, auto colour) {
        std::vector<unsigned char> img((size_t)w * h * 4);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                unsigned char *p = &img[((size_t)y * w + x) * 4];
                p[0] = p[1] = 0, p[2] = (unsigned char)colour(x, y), p[3] = 255;
            }
        return img;
    };
    std::vector<unsigned char> wall = picture(50, 240, [](int x, int) { return x == 24 || x == 49 ? 239 : 198; });
    std::vector<unsigned char> floor = picture(35, 57, [](int x, int y) { return y == 0 || x == 34 ? 156 : 117; });
    std::vector<unsigned char> plain = picture(35, 57, [](int, int) { return 117; });
    ParallaxPicture up{wall.data(), 50, 240}, low{floor.data(), 35, 57}, flat{plain.data(), 35, 57};
    std::string camera = "[StageInfo]\nlocalcoord = 320, 240\nzoffset = 190\n[Camera]\nboundleft = -125\nboundright = 125\n";
    std::string def = camera + "[BG wall]\ntype = normal\nspriteno = 0, 3\ntile = 1, 0\n"
                               "[BG lamp]\ntype = normal\nspriteno = 0, 1\nstart = 0, 150\n"
                               "[BG floor]\ntype = parallax\nspriteno = 0, 4\nstart = 35, 185\ndelta = .919\ntile = 1, 0\nwidth = 35, 70\n";
    Parsed p(def);
    CHECK(p.bgs.size() == 3);
    if (p.bgs.size() != 3) return;
    // Each meets the other; the lamp, narrower than the screen, is no plane
    std::vector<ParallaxMeeting> m = Parallax_Meetings(p.bgs, 2, p.view);
    CHECK(m.size() == 1 && m[0].upper == 0 && m[0].lower == 2);
    m = Parallax_Meetings(p.bgs, 0, p.view);
    CHECK(m.size() == 1 && m[0].upper == 0 && m[0].lower == 2);
    CHECK(Parallax_Meetings(p.bgs, 1, p.view).empty());
    NEAR(Parallax_RowSpeed(p.bgs[2], 185), 0.919f);
    NEAR(Parallax_RowSpeed(p.bgs[2], 185 + 57), 1.838f);

    // The wall takes the floor's speed (the fighters stand on the floor), the floor's top is 25 wide
    // and its bottom as much wider, and the wall moves to its lines: theirs are at 22.14 + 25k
    ParallaxLineUp u = Parallax_LineUp(p.bgs[0], up, p.bgs[2], low, p.view);
    CHECK(u.upperFollows && !u.done);
    NEAR(u.slide, 0.081f * 250);
    NEAR(u.delta[0][0], 0.919f);
    CHECK(u.delta[0][1] < 0 && u.delta[1][0] < 0); // nothing up high; the floor keeps its own
    CHECK(u.width[0] == 25 && u.width[1] == 50);
    CHECK(u.match > 0.9f);
    CHECK(std::fabs(u.shift[0] + 2.36f) < 0.15f);
    NEAR(u.shift[1], 0);

    // Done, it is done
    std::string fixed = def;
    fixed.replace(fixed.find("tile = 1, 0\n"), 12, "tile = 1, 0\ndelta = .919\nstart = -2.36, 0\n");
    fixed.replace(fixed.find("width = 35, 70"), 14, "width = 25, 50");
    Parsed q(fixed);
    u = Parallax_LineUp(q.bgs[0], up, q.bgs[2], low, q.view);
    CHECK(u.done);
    CHECK(u.match > 0.9f);

    // A plain floor: nothing shows the join slide
    u = Parallax_LineUp(p.bgs[0], up, p.bgs[2], flat, p.view);
    CHECK(u.done && u.match < 0 && u.width[0] == 0 && u.delta[0][0] < 0);
    NEAR(u.shift[0], 0);

    // The fighters on the upper one: the lower one follows it
    Parsed s(camera + "[BG floor]\ntype = normal\nspriteno = 0, 3\ntile = 1, 0\n"
                      "[BG ledge]\ntype = parallax\nspriteno = 0, 4\nstart = 35, 200\ndelta = .5\ntile = 1, 0\nwidth = 35, 70\n");
    u = Parallax_LineUp(s.bgs[0], up, s.bgs[1], low, s.view);
    CHECK(!u.upperFollows);
    NEAR(u.delta[1][0], 1);
    CHECK(u.delta[0][0] < 0);
}

static void TestStops() {
    Parsed p(std::string(kCamera) + "boundhigh = -40\nzoomout = 0.5\n");
    std::vector<ParallaxStop> stops = Parallax_Stops(p.view);
    CHECK(stops.size() == 7);
    NEAR(stops[0].x, 0);
    NEAR(stops[1].x, -160);
    NEAR(stops[2].x, 160);
    NEAR(stops[3].y, -40);
    NEAR(stops[5].scale, 0.5f);
    NEAR(stops[5].x, 0); // the whole stage is on screen: nowhere to go
    NEAR(stops[6].x, 0);
}

static void TestCheck() {
    // Fits: nothing to say
    Parsed good(std::string(kCamera) + "[BG a]\ntype = normal\nspriteno = 0, 0\n");
    CHECK(Parallax_Check(good.ini, good.bgs, good.view).empty());

    // Moves too fast: it fills the screen at the start, and not at the limits
    Parsed fast(std::string(kCamera) + "[BG a]\ntype = normal\nspriteno = 0, 0\ndelta = 1.5\n");
    auto found = Parallax_Check(fast.ini, fast.bgs, fast.view);
    CHECK(Has(found, StageFinding::Warning, "At the camera's right limit, the right"));
    CHECK(Has(found, StageFinding::Warning, "At the camera's left limit, the left"));
    CHECK(Has(found, StageFinding::Note, "[BG a] ends 80 units before"));
    CHECK(Has(found, StageFinding::Note, "delta 1 would reach"));

    // Another BG behind it: no hole, and the one that ends still says so
    Parsed covered(std::string(kCamera) + "[BG back]\ntype = normal\nspriteno = 0, 1\ntile = 1, 1\n[BG a]\ntype = normal\nspriteno = 0, 0\ndelta = 1.5\n");
    found = Parallax_Check(covered.ini, covered.bgs, covered.view);
    CHECK(!Has(found, StageFinding::Warning, "show no BG"));
    CHECK(Has(found, StageFinding::Note, "[BG a] ends"));

    // A stage that never filled the screen has no hole because of the camera; a blended BG hides nothing
    Parsed small(std::string(kCamera) + "[BG a]\ntype = normal\nspriteno = 0, 1\nstart = -50, 0\ndelta = 0\n");
    CHECK(Parallax_Check(small.ini, small.bgs, small.view).empty());
    Parsed glass(std::string(kCamera) + "[BG a]\ntype = normal\nspriteno = 0, 0\ntrans = add\n[BG b]\ntype = normal\nspriteno = 0, 0\ndelta = 1.5\n");
    found = Parallax_Check(glass.ini, glass.bgs, glass.view);
    CHECK(Has(found, StageFinding::Warning, "show no BG"));

    // Up high, the top of a BG that moves with the camera comes into the screen
    Parsed high(std::string(kCamera) + "boundhigh = -40\n[BG a]\ntype = normal\nspriteno = 0, 0\n");
    found = Parallax_Check(high.ini, high.bgs, high.view);
    CHECK(Has(found, StageFinding::Warning, "up high, the top 40 units"));
}

// The release's stages (run from the game's folder): what is said of them, to read
static void TestRelease() {
    for (const char *name : {"stage0", "stage0-720", "stage1", "stageZ", "kfm", "stage3d", "interactivestage"}) {
        std::string def = std::string("stages/") + name + ".def";
        std::ifstream in(def, std::ios::binary);
        if (!in) continue;
        std::stringstream text;
        text << in.rdbuf();
        IniFile ini;
        Ini_Parse(text.str(), ini);
        std::string spr = Ini_SpritesFile(text.str());
        SffDoc sff;
        if (!Sff_Load(sff, "stages/" + spr) && !Sff_Load(sff, spr)) continue;
        AirFile air;
        Air_Parse(text.str(), air);
        ParallaxView view = Parallax_ViewOf(ini);
        std::vector<ParallaxBg> bgs = Parallax_Bgs(ini, air, sff);
        std::vector<StageFinding> found = Parallax_Check(ini, bgs, view);
        printf("%s: %zu BGs measured, camera %.0f to %.0f\n", def.c_str(), bgs.size(), view.left, view.right);
        for (const StageFinding &f : found) printf("  %d: %s\n", f.line, f.text.c_str());
        for (const StageFinding &f : Parallax_Seams(bgs, sff)) printf("  %d: %s\n", f.line, f.text.c_str());
        // Where planes meet, and what would line them up
        std::vector<std::vector<unsigned char>> rgba(bgs.size());
        auto picture = [&](int i) {
            const SffSprite &s = sff.sprites[bgs[i].sprite];
            std::vector<unsigned char> px;
            int bpp = 0;
            if (rgba[i].empty() && Sff_Decode(s, px, &bpp)) Sff_ToRgba(sff, s, px, bpp, rgba[i]);
            return ParallaxPicture{rgba[i].empty() ? nullptr : rgba[i].data(), s.w, s.h};
        };
        for (int i = 0; i < (int)bgs.size(); i++)
            for (const ParallaxMeeting &m : Parallax_Meetings(bgs, i, view)) {
                if (m.lower != i) continue; // each once
                ParallaxLineUp u = Parallax_LineUp(bgs[m.upper], picture(m.upper), bgs[m.lower], picture(m.lower), view);
                printf("  [%s] on [%s]: %s, slides %.1f, lines %.2f, delta %.3g / %.3g, width %d, %d, moved %.2f / %.2f\n",
                       bgs[m.lower].name.c_str(), bgs[m.upper].name.c_str(), u.done ? "lined up" : "not lined up", u.slide, u.match,
                       u.delta[0][0], u.delta[1][0], u.width[0], u.width[1], u.shift[0], u.shift[1]);
            }
    }
}

int main() {
    TestRelease();
    TestView();
    TestBgs();
    TestCovers();
    TestFit();
    TestFloor();
    TestSeam();
    TestLineUp();
    TestStops();
    TestCheck();
    if (g_failed) {
        printf("parallax-test: %d failed\n", g_failed);
        return 1;
    }
    printf("parallax-test: all passed\n");
    return 0;
}
