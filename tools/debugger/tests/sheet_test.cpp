// Tests of src/sheet.cpp: pictures drawn here, cut into pieces.
#include "sheet.h"

#include <cstdio>
#include <cstring>

static int g_failed;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failed++;                                                 \
        }                                                               \
    } while (0)

// All transparent
static SheetImage Blank(int w, int h) {
    SheetImage img;
    img.w = w, img.h = h;
    img.rgba.assign((size_t)w * h * 4, 0);
    return img;
}

static void Fill(SheetImage &img, int x, int y, int w, int h, unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255) {
    for (int py = y; py < y + h; py++)
        for (int px = x; px < x + w; px++) {
            unsigned char *p = &img.rgba[((size_t)py * img.w + px) * 4];
            p[0] = r, p[1] = g, p[2] = b, p[3] = a;
        }
}

static SheetOptions Whole(const SheetImage &img) {
    SheetOptions o;
    o.area.w = img.w, o.area.h = img.h;
    return o;
}

static bool Is(const SheetRect &r, int x, int y, int w, int h) { return r.x == x && r.y == y && r.w == w && r.h == h; }

static void Gaps() {
    // Two squares with 5 empty pixels between them
    SheetImage img = Blank(40, 20);
    Fill(img, 2, 2, 10, 10, 255, 0, 0);
    Fill(img, 17, 2, 10, 10, 0, 255, 0);
    SheetOptions o = Whole(img);
    SheetCut cut = Sheet_Cut(img, o);
    CHECK(cut.pieces.size() == 2);
    CHECK(Is(cut.pieces[0], 2, 2, 10, 10));
    CHECK(Is(cut.pieces[1], 17, 2, 10, 10));
    o.gap = 4;
    CHECK(Sheet_Cut(img, o).pieces.size() == 2);
    o.gap = 5;
    cut = Sheet_Cut(img, o);
    CHECK(cut.pieces.size() == 1);
    CHECK(Is(cut.pieces[0], 2, 2, 25, 10));
    // The gap counts up and down too, and adds nothing to the box
    img = Blank(20, 40);
    Fill(img, 2, 2, 10, 10, 255, 0, 0);
    Fill(img, 2, 15, 10, 10, 0, 255, 0);
    o = Whole(img);
    o.gap = 2;
    CHECK(Sheet_Cut(img, o).pieces.size() == 2);
    o.gap = 3;
    cut = Sheet_Cut(img, o);
    CHECK(cut.pieces.size() == 1);
    CHECK(Is(cut.pieces[0], 2, 2, 10, 23));
}

static void Corners() {
    // Touching on a corner is touching
    SheetImage img = Blank(10, 10);
    Fill(img, 2, 2, 1, 1, 255, 0, 0);
    Fill(img, 3, 3, 1, 1, 255, 0, 0);
    CHECK(Sheet_Cut(img, Whole(img)).pieces.size() == 1);
}

static void Inner() {
    // A ring and a dot in its middle are one sprite
    SheetImage img = Blank(20, 20);
    Fill(img, 2, 2, 12, 1, 255, 0, 0);
    Fill(img, 2, 13, 12, 1, 255, 0, 0);
    Fill(img, 2, 2, 1, 12, 255, 0, 0);
    Fill(img, 13, 2, 1, 12, 255, 0, 0);
    Fill(img, 7, 7, 2, 2, 0, 0, 255);
    SheetCut cut = Sheet_Cut(img, Whole(img));
    CHECK(cut.pieces.size() == 1);
    std::vector<unsigned char> pic = Sheet_Picture(img, cut, 0);
    CHECK(pic[((size_t)5 * 12 + 5) * 4 + 2] == 255); // the dot went along
    CHECK(pic[((size_t)5 * 12 + 5) * 4 + 3] == 255);
}

static void Keys() {
    // No transparency: a colour is the background, near ones too with a tolerance
    SheetImage img = Blank(20, 10);
    Fill(img, 0, 0, 20, 10, 255, 0, 255);
    CHECK(!Sheet_HasTransparency(img));
    Fill(img, 2, 2, 4, 4, 10, 200, 10);
    Fill(img, 10, 2, 4, 4, 10, 200, 10);
    Fill(img, 8, 8, 1, 1, 250, 5, 250); // a colour almost the background's
    SheetOptions o = Whole(img);
    CHECK(Sheet_Cut(img, o).pieces.size() == 1); // nothing is background: all of it is one piece
    o.useKey = true, o.key[0] = 255, o.key[1] = 0, o.key[2] = 255;
    CHECK(Sheet_Cut(img, o).pieces.size() == 3);
    o.tolerance = 5;
    CHECK(Sheet_Cut(img, o).pieces.size() == 2);
    SheetCut cut = Sheet_Cut(img, o);
    std::vector<unsigned char> pic = Sheet_Picture(img, cut, 0);
    CHECK(pic[1] == 200 && pic[3] == 255);
    // Inside the rectangle of the area nothing of the background stays: holes are transparent
    Fill(img, 3, 3, 1, 1, 255, 0, 255);
    cut = Sheet_Cut(img, o);
    pic = Sheet_Picture(img, cut, 0);
    CHECK(pic[((size_t)1 * 4 + 1) * 4 + 3] == 0);
    CHECK(Sheet_HasTransparency(Blank(2, 2)));
}

static void Order() {
    // Two rows of different heights, drawn out of order; a jump higher than the others stays in its row
    SheetImage img = Blank(100, 80);
    Fill(img, 40, 45, 10, 20, 3, 3, 3);
    Fill(img, 5, 45, 10, 20, 2, 2, 2);
    Fill(img, 60, 2, 10, 10, 6, 6, 6);
    Fill(img, 30, 8, 10, 30, 5, 5, 5);
    Fill(img, 5, 10, 10, 30, 4, 4, 4);
    SheetCut cut = Sheet_Cut(img, Whole(img));
    CHECK(cut.pieces.size() == 5);
    CHECK(cut.pieces[0].x == 5 && cut.pieces[0].y == 10);
    CHECK(cut.pieces[1].x == 30);
    CHECK(cut.pieces[2].x == 60);
    CHECK(cut.pieces[3].x == 5 && cut.pieces[3].y == 45);
    CHECK(cut.pieces[4].x == 40);
    // Labels follow the order
    std::vector<unsigned char> pic = Sheet_Picture(img, cut, 4);
    CHECK(pic[0] == 3);
}

static void Neighbours() {
    // Two strokes whose boxes cross: each keeps its pixels only
    SheetImage img = Blank(20, 20);
    Fill(img, 2, 2, 10, 2, 255, 0, 0);
    Fill(img, 2, 2, 2, 7, 255, 0, 0);
    Fill(img, 8, 6, 2, 8, 0, 255, 0);
    Fill(img, 8, 12, 10, 2, 0, 255, 0);
    SheetCut cut = Sheet_Cut(img, Whole(img));
    CHECK(cut.pieces.size() == 2);
    std::vector<unsigned char> first = Sheet_Picture(img, cut, 0);
    const SheetRect &r = cut.pieces[0];
    bool green = false;
    for (int i = 0; i < r.w * r.h; i++) green = green || first[i * 4 + 1] != 0;
    CHECK(!green);
}

static void Small() {
    SheetImage img = Blank(30, 10);
    Fill(img, 2, 2, 5, 5, 255, 0, 0);
    Fill(img, 12, 2, 1, 1, 255, 0, 0);
    Fill(img, 20, 2, 2, 2, 255, 0, 0);
    SheetOptions o = Whole(img);
    CHECK(Sheet_Cut(img, o).pieces.size() == 3);
    o.minPixels = 2;
    CHECK(Sheet_Cut(img, o).pieces.size() == 2);
    o.minPixels = 5;
    SheetCut cut = Sheet_Cut(img, o);
    CHECK(cut.pieces.size() == 1);
    CHECK(cut.labels[(size_t)2 * 30 + 12] == 0); // what was left out belongs to nothing
}

static void Areas() {
    SheetImage img = Blank(40, 20);
    Fill(img, 2, 2, 5, 5, 255, 0, 0);
    Fill(img, 12, 2, 5, 5, 0, 255, 0);
    Fill(img, 30, 2, 5, 5, 0, 0, 255);
    // Only what is in the area, cut by it, in the coordinates of the image
    SheetOptions o;
    o.area.x = 10, o.area.y = 0, o.area.w = 10, o.area.h = 20;
    SheetCut cut = Sheet_Cut(img, o);
    CHECK(cut.pieces.size() == 1);
    CHECK(Is(cut.pieces[0], 12, 2, 5, 5));
    CHECK(Sheet_Picture(img, cut, 0)[1] == 255);
    o.area.x = 14, o.area.w = 2;
    CHECK(Is(Sheet_Cut(img, o).pieces[0], 14, 2, 2, 5));
    // Out of the image
    o.area.x = -10, o.area.y = -10, o.area.w = 100, o.area.h = 100;
    cut = Sheet_Cut(img, o);
    CHECK(cut.pieces.size() == 3);
    CHECK(Is(cut.area, 0, 0, 40, 20));
    o.area.x = 50, o.area.w = 10;
    CHECK(Sheet_Cut(img, o).pieces.empty());
    o.area = SheetRect();
    CHECK(Sheet_Cut(img, o).pieces.empty());
    // Nothing in it
    o.area.x = 20, o.area.y = 10, o.area.w = 5, o.area.h = 5;
    CHECK(Sheet_Cut(img, o).pieces.empty());
}

int main() {
    Gaps();
    Corners();
    Inner();
    Keys();
    Order();
    Neighbours();
    Small();
    Areas();
    if (g_failed) {
        printf("sheet-test: %d failed\n", g_failed);
        return 1;
    }
    printf("sheet-test: all passed\n");
    return 0;
}
