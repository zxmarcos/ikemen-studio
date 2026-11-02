#include "sheet.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <numeric>

namespace {

SheetRect Clipped(const SheetImage &img, SheetRect r) {
    int x2 = std::min(r.x + r.w, img.w), y2 = std::min(r.y + r.h, img.h);
    r.x = std::max(r.x, 0), r.y = std::max(r.y, 0);
    r.w = std::max(x2 - r.x, 0), r.h = std::max(y2 - r.y, 0);
    return r;
}

bool IsBackground(const unsigned char *p, const SheetOptions &o) {
    if (p[3] == 0) return true;
    if (!o.useKey) return false;
    for (int c = 0; c < 3; c++)
        if (abs(p[c] - o.key[c]) > o.tolerance) return false;
    return true;
}

// Each pixel reaches gap cells to the right and down: two parts with gap empty pixels between them
// meet, with one more they do not.
std::vector<char> Spread(const std::vector<char> &mask, int w, int h, int gap) {
    if (gap <= 0) return mask;
    std::vector<char> across(mask.size(), 0), out(mask.size(), 0);
    for (int y = 0; y < h; y++) {
        int left = 0;
        for (int x = 0; x < w; x++) {
            if (mask[(size_t)y * w + x]) left = gap + 1;
            if (left > 0) across[(size_t)y * w + x] = 1, left--;
        }
    }
    for (int x = 0; x < w; x++) {
        int left = 0;
        for (int y = 0; y < h; y++) {
            if (across[(size_t)y * w + x]) left = gap + 1;
            if (left > 0) out[(size_t)y * w + x] = 1, left--;
        }
    }
    return out;
}

// Components of what is set, touching on sides and corners: 1 up in each cell, 0 none. Returns how many.
int Label(const std::vector<char> &cells, int w, int h, std::vector<int> &labels) {
    labels.assign(cells.size(), 0);
    std::vector<int> stack;
    int count = 0;
    for (int i = 0; i < (int)cells.size(); i++) {
        if (!cells[i] || labels[i]) continue;
        labels[i] = ++count;
        stack.push_back(i);
        while (!stack.empty()) {
            int at = stack.back(), x = at % w, y = at / w;
            stack.pop_back();
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x + dx, ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    int next = ny * w + nx;
                    if (cells[next] && !labels[next]) labels[next] = count, stack.push_back(next);
                }
        }
    }
    return count;
}

struct Box {
    int x1 = 1 << 30, y1 = 1 << 30, x2 = -1, y2 = -1; // inclusive
    int pixels = 0;
    bool Holds(const Box &o) const { return o.x1 >= x1 && o.y1 >= y1 && o.x2 <= x2 && o.y2 <= y2; }
    long long Area() const { return (long long)(x2 - x1 + 1) * (y2 - y1 + 1); }
};

// A box inside another is a part of it (a detached eye, the middle of a ring): no sheet has a sprite
// inside the box of another. Past this many, it is noise, and comparing them all would take too long.
const int kMaxToJoin = 4000;

void JoinInner(std::vector<Box> &boxes, std::vector<int> &into) {
    int n = (int)boxes.size();
    if (n > kMaxToJoin) return;
    std::vector<int> bySize(n);
    std::iota(bySize.begin(), bySize.end(), 0);
    std::sort(bySize.begin(), bySize.end(), [&](int a, int b) { return boxes[a].Area() > boxes[b].Area(); });
    for (int i = 0; i < n; i++) {
        int outer = bySize[i];
        if (into[outer] != outer) continue;
        for (int k = i + 1; k < n; k++) {
            int inner = bySize[k];
            if (into[inner] != inner || !boxes[outer].Holds(boxes[inner])) continue;
            into[inner] = outer;
            boxes[outer].pixels += boxes[inner].pixels;
        }
    }
}

// Rows from the top: a piece reaching into the row is in it, so that a jump, higher than the frames
// around it, does not start a row of its own.
void ReadingOrder(std::vector<SheetRect> &pieces, std::vector<int> &order) {
    std::vector<int> byTop(pieces.size());
    std::iota(byTop.begin(), byTop.end(), 0);
    std::sort(byTop.begin(), byTop.end(), [&](int a, int b) { return pieces[a].y < pieces[b].y; });
    order.clear();
    for (size_t start = 0; start < byTop.size();) {
        const SheetRect &first = pieces[byTop[start]];
        int bottom = first.y + first.h;
        size_t end = start + 1;
        for (; end < byTop.size(); end++) {
            const SheetRect &p = pieces[byTop[end]];
            if (p.y >= bottom) break;
            bottom = std::max(bottom, p.y + p.h);
        }
        std::sort(byTop.begin() + start, byTop.begin() + end, [&](int a, int b) { return pieces[a].x < pieces[b].x; });
        order.insert(order.end(), byTop.begin() + start, byTop.begin() + end);
        start = end;
    }
}

} // namespace

bool Sheet_HasTransparency(const SheetImage &img) {
    for (size_t i = 3; i < img.rgba.size(); i += 4)
        if (img.rgba[i] == 0) return true;
    return false;
}

SheetCut Sheet_Cut(const SheetImage &img, const SheetOptions &o) {
    SheetCut cut;
    cut.area = Clipped(img, o.area);
    int w = cut.area.w, h = cut.area.h;
    if (w <= 0 || h <= 0) return cut;
    std::vector<char> mask((size_t)w * h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            mask[(size_t)y * w + x] = !IsBackground(&img.rgba[((size_t)(y + cut.area.y) * img.w + x + cut.area.x) * 4], o);
    std::vector<int> groups;
    int count = Label(Spread(mask, w, h, o.gap), w, h, groups);

    // Each part's box, of its own pixels: what the spreading added is not in it
    std::vector<Box> boxes(count);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            size_t at = (size_t)y * w + x;
            if (!mask[at]) continue;
            Box &b = boxes[groups[at] - 1];
            b.x1 = std::min(b.x1, x), b.y1 = std::min(b.y1, y);
            b.x2 = std::max(b.x2, x), b.y2 = std::max(b.y2, y);
            b.pixels++;
        }
    std::vector<int> into(count);
    std::iota(into.begin(), into.end(), 0);
    JoinInner(boxes, into);

    std::vector<SheetRect> found;
    std::vector<int> foundOf(count, -1);
    for (int i = 0; i < count; i++) {
        if (into[i] != i || boxes[i].pixels < o.minPixels) continue;
        const Box &b = boxes[i];
        foundOf[i] = (int)found.size();
        SheetRect r;
        r.x = b.x1 + cut.area.x, r.y = b.y1 + cut.area.y, r.w = b.x2 - b.x1 + 1, r.h = b.y2 - b.y1 + 1;
        found.push_back(r);
    }
    std::vector<int> order;
    ReadingOrder(found, order);
    std::vector<int> placeOf(found.size());
    for (size_t k = 0; k < order.size(); k++) {
        cut.pieces.push_back(found[order[k]]);
        placeOf[order[k]] = (int)k;
    }
    cut.labels.assign(mask.size(), 0);
    for (size_t at = 0; at < mask.size(); at++) {
        if (!mask[at]) continue;
        int f = foundOf[into[groups[at] - 1]];
        if (f >= 0) cut.labels[at] = placeOf[f] + 1;
    }
    return cut;
}

std::vector<unsigned char> Sheet_Picture(const SheetImage &img, const SheetCut &cut, int piece) {
    const SheetRect &r = cut.pieces[piece];
    std::vector<unsigned char> out((size_t)r.w * r.h * 4, 0);
    for (int y = 0; y < r.h; y++)
        for (int x = 0; x < r.w; x++) {
            size_t cell = (size_t)(y + r.y - cut.area.y) * cut.area.w + x + r.x - cut.area.x;
            if (cut.labels[cell] != piece + 1) continue;
            memcpy(&out[((size_t)y * r.w + x) * 4], &img.rgba[((size_t)(y + r.y) * img.w + x + r.x) * 4], 4);
        }
    return out;
}
