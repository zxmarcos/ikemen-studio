#include "pixels.h"

#include <algorithm>
#include <cstdlib>

static void Dot(PixelGrid &g, int x, int y, int size, unsigned char value) {
    int x0 = x - (size - 1) / 2, y0 = y - (size - 1) / 2;
    for (int j = y0; j < y0 + size; j++)
        for (int i = x0; i < x0 + size; i++)
            if (g.Inside(i, j)) g.At(i, j) = value;
}

void Px_Line(PixelGrid &g, int x0, int y0, int x1, int y1, int size, unsigned char value) {
    // Bresenham
    int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;) {
        Dot(g, x0, y0, size, value);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) err += dy, x0 += sx;
        if (e2 <= dx) err += dx, y0 += sy;
    }
}

void Px_Fill(PixelGrid &g, int x, int y, unsigned char value, bool contiguous) {
    if (!g.Inside(x, y)) return;
    unsigned char from = g.At(x, y);
    if (from == value) return;
    if (!contiguous) {
        std::replace(g.px.begin(), g.px.end(), from, value);
        return;
    }
    std::vector<std::pair<int, int>> open{{x, y}};
    while (!open.empty()) {
        auto [px, py] = open.back();
        open.pop_back();
        if (!g.Inside(px, py) || g.At(px, py) != from) continue;
        g.At(px, py) = value;
        open.push_back({px + 1, py});
        open.push_back({px - 1, py});
        open.push_back({px, py + 1});
        open.push_back({px, py - 1});
    }
}

PixelRect Px_Inside(const PixelGrid &g, PixelRect r) {
    int x1 = std::min(g.w, r.x + r.w), y1 = std::min(g.h, r.y + r.h);
    r.x = std::max(0, r.x), r.y = std::max(0, r.y);
    r.w = std::max(0, x1 - r.x), r.h = std::max(0, y1 - r.y);
    return r;
}

void Px_Clear(PixelGrid &g, const PixelRect &rect) {
    PixelRect r = Px_Inside(g, rect);
    for (int y = r.y; y < r.y + r.h; y++)
        for (int x = r.x; x < r.x + r.w; x++) g.At(x, y) = 0;
}

PixelGrid Px_Copy(const PixelGrid &g, const PixelRect &rect) {
    PixelRect r = Px_Inside(g, rect);
    PixelGrid o(r.w, r.h);
    for (int y = 0; y < r.h; y++)
        for (int x = 0; x < r.w; x++) o.At(x, y) = g.At(r.x + x, r.y + y);
    return o;
}

void Px_Paste(PixelGrid &g, const PixelGrid &from, int x, int y) {
    for (int j = 0; j < from.h; j++)
        for (int i = 0; i < from.w; i++)
            if (from.At(i, j) != 0 && g.Inside(x + i, y + j)) g.At(x + i, y + j) = from.At(i, j);
}

void Px_FlipH(PixelGrid &g, const PixelRect &rect) {
    PixelRect r = Px_Inside(g, rect);
    for (int y = r.y; y < r.y + r.h; y++)
        for (int a = r.x, b = r.x + r.w - 1; a < b; a++, b--) std::swap(g.At(a, y), g.At(b, y));
}

void Px_FlipV(PixelGrid &g, const PixelRect &rect) {
    PixelRect r = Px_Inside(g, rect);
    for (int x = r.x; x < r.x + r.w; x++)
        for (int a = r.y, b = r.y + r.h - 1; a < b; a++, b--) std::swap(g.At(x, a), g.At(x, b));
}

PixelGrid Px_Grow(const PixelGrid &g, int left, int top, int right, int bottom) {
    PixelGrid o(g.w + left + right, g.h + top + bottom);
    for (int y = 0; y < g.h; y++)
        for (int x = 0; x < g.w; x++) o.At(x + left, y + top) = g.At(x, y);
    return o;
}
