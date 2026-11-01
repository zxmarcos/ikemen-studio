// Tests of src/gif.cpp: frames written, read back by a small decoder here, come out the same.
// Built as gif-test; scripts/build.ps1 test builds and runs it.
#include "gif.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static int g_failed;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failed++;                                                 \
        }                                                               \
    } while (0)

struct Read {
    int delay = 0, transparent = -1;
    std::vector<unsigned char> table, px;
};

// LZW as a reader does it
static void Decode(const unsigned char *&p, std::vector<unsigned char> &out) {
    int minBits = *p++;
    int clear = 1 << minBits, stop = clear + 1, size = minBits + 1;
    std::vector<std::vector<unsigned char>> table;
    auto reset = [&]() {
        table.clear();
        for (int i = 0; i < clear + 2; i++) table.push_back({(unsigned char)i});
        size = minBits + 1;
    };
    reset();
    unsigned value = 0;
    int count = 0, prev = -1;
    bool done = false;
    while (*p) {
        int n = *p++;
        for (int i = 0; i < n; i++) {
            value |= (unsigned)p[i] << count;
            count += 8;
            while (count >= size && !done) {
                int code = value & ((1 << size) - 1);
                value >>= size;
                count -= size;
                if (code == clear) {
                    reset();
                    prev = -1;
                    continue;
                }
                if (code == stop) {
                    done = true;
                    break;
                }
                std::vector<unsigned char> entry;
                if (code < (int)table.size()) {
                    entry = table[code];
                } else {
                    CHECK(code == (int)table.size() && prev >= 0);
                    if (prev < 0) return;
                    entry = table[prev];
                    entry.push_back(table[prev][0]);
                }
                out.insert(out.end(), entry.begin(), entry.end());
                if (prev >= 0 && table.size() < 4096) {
                    std::vector<unsigned char> grown = table[prev];
                    grown.push_back(entry[0]);
                    table.push_back(grown);
                    if ((int)table.size() == (1 << size) && size < 12) size++;
                }
                prev = code;
            }
        }
        p += n;
    }
    p++;
    CHECK(done);
}

static std::vector<Read> Parse(const std::string &gif, int *w, int *h) {
    std::vector<Read> frames;
    const unsigned char *p = (const unsigned char *)gif.data();
    CHECK(!memcmp(p, "GIF89a", 6));
    *w = p[6] | p[7] << 8, *h = p[8] | p[9] << 8;
    p += 13;
    Read cur;
    while (*p != 0x3B) {
        if (*p == 0x21) {
            if (p[1] == 0xF9) {
                cur.delay = p[4] | p[5] << 8;
                cur.transparent = (p[3] & 1) ? p[6] : -1;
            }
            p += 2;
            while (*p) p += *p + 1;
            p++;
        } else {
            CHECK(*p == 0x2C);
            int bits = (p[9] & 7) + 1;
            CHECK(p[9] & 0x80);
            p += 10;
            cur.table.assign(p, p + (3 << bits));
            p += 3 << bits;
            cur.px.clear();
            Decode(p, cur.px);
            frames.push_back(cur);
        }
    }
    return frames;
}

// What a frame looks like read back: its colours, transparent as 0
static std::vector<unsigned char> Rgba(const Read &r) {
    std::vector<unsigned char> o(r.px.size() * 4, 0);
    for (size_t i = 0; i < r.px.size(); i++) {
        if (r.px[i] == r.transparent) continue;
        memcpy(&o[i * 4], &r.table[r.px[i] * 3], 3);
        o[i * 4 + 3] = 255;
    }
    return o;
}

int main() {
    // A few colours, runs and transparent pixels; then 200 colours of noise, which fill the code table
    srand(7);
    const int w = 64, h = 48;
    GifFrame a, b;
    a.rgba.assign(w * h * 4, 0);
    b.rgba.assign(w * h * 4, 0);
    a.delay = 5, b.delay = 12;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            unsigned char *c = &a.rgba[(y * w + x) * 4];
            if ((x / 8 + y / 8) & 1) c[0] = 200, c[1] = 40, c[2] = 10, c[3] = 255;
            unsigned char *d = &b.rgba[(y * w + x) * 4];
            int v = rand() % 200;
            d[0] = (unsigned char)v, d[1] = (unsigned char)(255 - v), d[2] = 9, d[3] = 255;
        }
    int rw = 0, rh = 0;
    std::vector<Read> frames = Parse(Gif_Build(w, h, {a, b}), &rw, &rh);
    CHECK(rw == w && rh == h && frames.size() == 2);
    if (frames.size() == 2) {
        CHECK(frames[0].delay == 5 && frames[1].delay == 12);
        CHECK(frames[0].transparent == 0);
        CHECK(Rgba(frames[0]) == a.rgba);
        CHECK(Rgba(frames[1]) == b.rgba);
    }

    // A long image of noise: the table fills and starts over
    const int big = 400;
    GifFrame n;
    n.rgba.assign(big * big * 4, 255);
    for (int i = 0; i < big * big; i++) n.rgba[i * 4] = (unsigned char)(rand() % 16 * 16), n.rgba[i * 4 + 1] = (unsigned char)(rand() % 8 * 32);
    frames = Parse(Gif_Build(big, big, {n}), &rw, &rh);
    CHECK(frames.size() == 1 && frames[0].px.size() == (size_t)big * big);
    if (frames.size() == 1) CHECK(Rgba(frames[0]) == n.rgba);

    // Past 255 colours: still a picture, of nearby colours
    GifFrame many;
    many.rgba.assign(64 * 64 * 4, 255);
    for (int i = 0; i < 64 * 64; i++) many.rgba[i * 4] = (unsigned char)(i & 255), many.rgba[i * 4 + 1] = (unsigned char)(i >> 8 << 2);
    frames = Parse(Gif_Build(64, 64, {many}), &rw, &rh);
    CHECK(frames.size() == 1 && frames[0].px.size() == 64 * 64);
    if (frames.size() == 1) {
        std::vector<unsigned char> back = Rgba(frames[0]);
        bool near = true;
        for (int i = 0; i < 64 * 64 * 4; i++) near = near && abs(back[i] - many.rgba[i]) <= 16;
        CHECK(near);
    }

    printf(g_failed ? "gif-test: %d failed\n" : "gif-test: all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
