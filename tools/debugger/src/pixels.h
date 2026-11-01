// A picture of palette indices and what a pixel editor does to it. Colour 0 is transparent.
#pragma once

#include <vector>

struct PixelGrid {
    int w = 0, h = 0;
    std::vector<unsigned char> px;
    PixelGrid() {}
    PixelGrid(int width, int height) : w(width), h(height), px((size_t)width * height, 0) {}
    bool Inside(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
    unsigned char &At(int x, int y) { return px[(size_t)y * w + x]; }
    unsigned char At(int x, int y) const { return px[(size_t)y * w + x]; }
};

struct PixelRect {
    int x = 0, y = 0, w = 0, h = 0;
    bool Empty() const { return w <= 0 || h <= 0; }
    bool Contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

// A square of that size at each point of the line (whole pixels; parts outside are left out)
void Px_Line(PixelGrid &g, int x0, int y0, int x1, int y1, int size, unsigned char value);
// The pixels of the colour at x, y that touch it (up, down, left, right), or all of that colour
void Px_Fill(PixelGrid &g, int x, int y, unsigned char value, bool contiguous);
void Px_Clear(PixelGrid &g, const PixelRect &r);
// r cut to the picture
PixelRect Px_Inside(const PixelGrid &g, PixelRect r);
PixelGrid Px_Copy(const PixelGrid &g, const PixelRect &r);
// Its colours over g with its corner at x, y: colour 0 leaves what is there
void Px_Paste(PixelGrid &g, const PixelGrid &from, int x, int y);
void Px_FlipH(PixelGrid &g, const PixelRect &r);
void Px_FlipV(PixelGrid &g, const PixelRect &r);
// The picture with more room around it
PixelGrid Px_Grow(const PixelGrid &g, int left, int top, int right, int bottom);
