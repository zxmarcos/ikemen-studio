// Tests of src/pixels.cpp: what the pixel editor does to a picture.
// Built as pixels-test; scripts/build.ps1 test builds and runs it.
#include "pixels.h"

#include <cstdio>

static int g_failed;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failed++;                                                 \
        }                                                               \
    } while (0)

static int Count(const PixelGrid &g, unsigned char v) {
    int n = 0;
    for (unsigned char p : g.px) n += p == v;
    return n;
}

int main() {
    // A line: its ends and a pixel each step, none past the edge
    PixelGrid g(10, 10);
    Px_Line(g, 1, 1, 8, 1, 1, 5);
    CHECK(g.At(1, 1) == 5 && g.At(8, 1) == 5 && Count(g, 5) == 8);
    g = PixelGrid(10, 10);
    Px_Line(g, 0, 0, 9, 9, 1, 3);
    CHECK(Count(g, 3) == 10 && g.At(5, 5) == 3);
    g = PixelGrid(10, 10);
    Px_Line(g, 9, 9, 9, 9, 3, 2); // a 3x3 dot in the corner: the part inside
    CHECK(Count(g, 2) == 4);
    g = PixelGrid(10, 10);
    Px_Line(g, 4, 4, 4, 4, 2, 2);
    CHECK(Count(g, 2) == 4 && g.At(4, 4) == 2 && g.At(5, 5) == 2);

    // The bucket stops at another colour; not contiguous takes them all
    g = PixelGrid(6, 6);
    for (int y = 0; y < 6; y++) g.At(3, y) = 1;
    Px_Fill(g, 0, 0, 7, true);
    CHECK(Count(g, 7) == 18 && g.At(4, 0) == 0);
    Px_Fill(g, 3, 0, 9, false);
    CHECK(Count(g, 9) == 6 && Count(g, 1) == 0);
    Px_Fill(g, 3, 0, 9, true); // the same colour: nothing
    CHECK(Count(g, 9) == 6);
    Px_Fill(g, 50, 50, 1, true);
    CHECK(Count(g, 1) == 0);

    // A rectangle cut to the picture, copied, cleared, pasted over: colour 0 leaves what is there
    g = PixelGrid(6, 6);
    g.At(1, 1) = 4, g.At(2, 1) = 5;
    PixelRect r;
    r.x = 1, r.y = 1, r.w = 2, r.h = 2;
    PixelGrid part = Px_Copy(g, r);
    CHECK(part.w == 2 && part.h == 2 && part.At(0, 0) == 4 && part.At(1, 0) == 5 && part.At(0, 1) == 0);
    Px_Clear(g, r);
    CHECK(Count(g, 0) == 36);
    g.At(4, 5) = 8;
    Px_Paste(g, part, 4, 4);
    CHECK(g.At(4, 4) == 4 && g.At(5, 4) == 5 && g.At(4, 5) == 8);
    Px_Paste(g, part, 5, 5); // past the edge: what fits
    CHECK(g.At(5, 5) == 4);
    r.x = -2, r.y = 4, r.w = 5, r.h = 9;
    PixelRect in = Px_Inside(g, r);
    CHECK(in.x == 0 && in.y == 4 && in.w == 3 && in.h == 2);
    r.x = 9, r.y = 9, r.w = 2, r.h = 2;
    CHECK(Px_Inside(g, r).Empty());

    // Flips: inside the rectangle only
    g = PixelGrid(4, 2);
    g.At(0, 0) = 1, g.At(1, 0) = 2, g.At(2, 0) = 3, g.At(3, 0) = 4;
    r.x = 1, r.y = 0, r.w = 3, r.h = 2;
    Px_FlipH(g, r);
    CHECK(g.At(0, 0) == 1 && g.At(1, 0) == 4 && g.At(2, 0) == 3 && g.At(3, 0) == 2);
    r.x = 0, r.w = 1;
    Px_FlipV(g, r);
    CHECK(g.At(0, 0) == 0 && g.At(0, 1) == 1 && g.At(1, 0) == 4);

    // More room: the picture moves with it
    g = PixelGrid(2, 2);
    g.At(1, 1) = 6;
    PixelGrid big = Px_Grow(g, 3, 1, 0, 2);
    CHECK(big.w == 5 && big.h == 5 && big.At(4, 2) == 6 && Count(big, 6) == 1);

    printf(g_failed ? "pixels-test: %d failed\n" : "pixels-test: all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
