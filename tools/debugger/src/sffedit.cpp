#include "sffedit.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

namespace {

// A picture to work on: a value for each pixel, a palette index or packed RGBA.
struct Picture {
    int w = 0, h = 0;
    bool indexed = true;
    std::vector<unsigned> px;
    unsigned &At(int x, int y) { return px[(size_t)y * w + x]; }
    unsigned At(int x, int y) const { return px[(size_t)y * w + x]; }
    bool Empty(unsigned v) const { return indexed ? v == 0 : (v >> 24) == 0; }
};

bool Read(const SffSprite &s, Picture &p) {
    std::vector<unsigned char> raw;
    int bpp = 0;
    if (s.w <= 0 || s.h <= 0 || !Sff_Decode(s, raw, &bpp)) return false;
    p.w = s.w, p.h = s.h, p.indexed = bpp == 1;
    p.px.resize((size_t)s.w * s.h);
    for (size_t i = 0; i < p.px.size(); i++) {
        if (bpp == 1)
            p.px[i] = raw[i];
        else
            p.px[i] = raw[i * bpp] | raw[i * bpp + 1] << 8 | raw[i * bpp + 2] << 16 | (unsigned)(bpp == 4 ? raw[i * 4 + 3] : 255) << 24;
    }
    return true;
}

void Write(SffSprite &s, const Picture &p) {
    int bpp = p.indexed ? 1 : 4;
    std::vector<unsigned char> raw(p.px.size() * bpp);
    for (size_t i = 0; i < p.px.size(); i++) {
        if (p.indexed)
            raw[i] = (unsigned char)p.px[i];
        else
            for (int k = 0; k < 4; k++) raw[i * 4 + k] = (unsigned char)(p.px[i] >> (8 * k));
    }
    Sff_SetPixels(s, p.w, p.h, raw, bpp);
}

Picture Nearest(const Picture &p, int w, int h) {
    Picture o;
    o.w = w, o.h = h, o.indexed = p.indexed;
    o.px.resize((size_t)w * h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) o.At(x, y) = p.At(std::min(p.w - 1, (x * 2 + 1) * p.w / (w * 2)), std::min(p.h - 1, (y * 2 + 1) * p.h / (h * 2)));
    return o;
}

Picture Epx2x(const Picture &p) {
    Picture o;
    o.w = p.w * 2, o.h = p.h * 2, o.indexed = p.indexed;
    o.px.resize((size_t)o.w * o.h);
    for (int y = 0; y < p.h; y++)
        for (int x = 0; x < p.w; x++) {
            unsigned c = p.At(x, y);
            unsigned a = p.At(x, std::max(0, y - 1)), b = p.At(std::min(p.w - 1, x + 1), y);
            unsigned l = p.At(std::max(0, x - 1), y), d = p.At(x, std::min(p.h - 1, y + 1));
            o.At(x * 2, y * 2) = l == a && l != d && a != b ? a : c;
            o.At(x * 2 + 1, y * 2) = a == b && a != l && b != d ? b : c;
            o.At(x * 2, y * 2 + 1) = d == l && d != b && l != a ? l : c;
            o.At(x * 2 + 1, y * 2 + 1) = b == d && b != a && d != l ? d : c;
        }
    return o;
}

// Colours as floats, multiplied by their alpha so that the transparent ones do not bleed in
struct Rgba {
    float c[4] = {0, 0, 0, 0};
};

Rgba ColourOf(const Picture &p, unsigned v, const unsigned char *pal) {
    Rgba o;
    if (p.indexed) {
        if (v == 0 || !pal) return o;
        for (int k = 0; k < 3; k++) o.c[k] = pal[v * 4 + k];
        o.c[3] = 255;
        return o;
    }
    float a = (float)(v >> 24);
    for (int k = 0; k < 3; k++) o.c[k] = (float)((v >> (8 * k)) & 255) * a / 255.0f;
    o.c[3] = a;
    return o;
}

Picture Bilinear(const Picture &p, int w, int h, const unsigned char *pal, int ncolors) {
    Picture o;
    o.w = w, o.h = h, o.indexed = p.indexed;
    o.px.resize((size_t)w * h);
    std::map<unsigned, unsigned> nearest; // by colour
    for (int y = 0; y < h; y++) {
        float fy = std::max(0.0f, (y + 0.5f) * p.h / h - 0.5f);
        int y0 = (int)fy, y1 = std::min(p.h - 1, y0 + 1);
        float ty = fy - y0;
        for (int x = 0; x < w; x++) {
            float fx = std::max(0.0f, (x + 0.5f) * p.w / w - 0.5f);
            int x0 = (int)fx, x1 = std::min(p.w - 1, x0 + 1);
            float tx = fx - x0;
            Rgba c[4] = {ColourOf(p, p.At(x0, y0), pal), ColourOf(p, p.At(x1, y0), pal), ColourOf(p, p.At(x0, y1), pal), ColourOf(p, p.At(x1, y1), pal)};
            float mix[4];
            for (int k = 0; k < 4; k++)
                mix[k] = (c[0].c[k] * (1 - tx) + c[1].c[k] * tx) * (1 - ty) + (c[2].c[k] * (1 - tx) + c[3].c[k] * tx) * ty;
            float a = mix[3];
            if (a < (p.indexed ? 128.0f : 1.0f)) continue;
            unsigned rgb = 0;
            for (int k = 0; k < 3; k++) rgb |= (unsigned)std::min(255.0f, mix[k] * 255.0f / a + 0.5f) << (8 * k);
            if (!p.indexed) {
                o.At(x, y) = rgb | (unsigned)(a + 0.5f) << 24;
                continue;
            }
            auto it = nearest.find(rgb);
            if (it == nearest.end()) {
                int best = 1, bestD = INT_MAX;
                for (int i = 1; i < ncolors && pal; i++) {
                    int d = 0;
                    for (int k = 0; k < 3; k++) {
                        int e = (int)((rgb >> (8 * k)) & 255) - pal[i * 4 + k];
                        d += e * e;
                    }
                    if (d < bestD) bestD = d, best = i;
                }
                it = nearest.emplace(rgb, (unsigned)best).first;
            }
            o.At(x, y) = it->second;
        }
    }
    return o;
}

// The sprites' pictures changed once each: linked sprites go on sharing the new one. change makes
// it from the sprite's and says the corner of the old picture that is now its (0, 0), scaled.
struct Moved {
    std::shared_ptr<std::string> data;
    int w = 0, h = 0, format = 0, depth = 0, left = 0, top = 0;
    bool changed = false;
};

} // namespace

int Sff_Crop(SffDoc &doc, const std::vector<int> &sprites) {
    std::map<const std::string *, Moved> done;
    int count = 0;
    for (int i : sprites) {
        if (i < 0 || i >= (int)doc.sprites.size()) continue;
        SffSprite &s = doc.sprites[i];
        auto it = done.find(s.data.get());
        if (it == done.end()) {
            Moved m;
            Picture p;
            if (Read(s, p)) {
                int x0 = p.w, y0 = p.h, x1 = -1, y1 = -1;
                for (int y = 0; y < p.h; y++)
                    for (int x = 0; x < p.w; x++)
                        if (!p.Empty(p.At(x, y))) {
                            x0 = std::min(x0, x), x1 = std::max(x1, x);
                            y0 = std::min(y0, y), y1 = std::max(y1, y);
                        }
                if (x1 >= 0 && (x0 > 0 || y0 > 0 || x1 < p.w - 1 || y1 < p.h - 1)) {
                    Picture o;
                    o.w = x1 - x0 + 1, o.h = y1 - y0 + 1, o.indexed = p.indexed;
                    o.px.resize((size_t)o.w * o.h);
                    for (int y = 0; y < o.h; y++)
                        for (int x = 0; x < o.w; x++) o.At(x, y) = p.At(x0 + x, y0 + y);
                    SffSprite made = s;
                    Write(made, o);
                    m.data = made.data, m.w = made.w, m.h = made.h, m.format = made.format, m.depth = made.depth;
                    m.left = x0, m.top = y0, m.changed = true;
                }
            }
            it = done.emplace(s.data.get(), m).first;
        }
        const Moved &m = it->second;
        if (!m.changed) continue;
        s.data = m.data, s.w = m.w, s.h = m.h, s.format = m.format, s.depth = m.depth;
        s.x -= m.left, s.y -= m.top;
        count++;
    }
    return count;
}

int Sff_Scale(SffDoc &doc, const std::vector<int> &sprites, float xscale, float yscale, int filter) {
    if (xscale <= 0 || yscale <= 0) return 0;
    std::map<const std::string *, Moved> done;
    int count = 0;
    for (int i : sprites) {
        if (i < 0 || i >= (int)doc.sprites.size()) continue;
        SffSprite &s = doc.sprites[i];
        auto it = done.find(s.data.get());
        if (it == done.end()) {
            Moved m;
            Picture p;
            int w = std::max(1, (int)lroundf(s.w * xscale)), h = std::max(1, (int)lroundf(s.h * yscale));
            if ((w != s.w || h != s.h) && Read(s, p)) {
                Picture o;
                const SffPal *pal = s.pal >= 0 && s.pal < (int)doc.pals.size() ? &doc.pals[s.pal] : nullptr;
                if (filter == SFF_SCALE_BILINEAR) {
                    o = Bilinear(p, w, h, pal ? pal->rgba.data() : nullptr, pal ? pal->ncolors : 0);
                } else {
                    o = p;
                    while (filter == SFF_SCALE_EPX && o.w < w && o.h < h) o = Epx2x(o);
                    if (o.w != w || o.h != h) o = Nearest(o, w, h);
                }
                SffSprite made = s;
                Write(made, o);
                m.data = made.data, m.w = made.w, m.h = made.h, m.format = made.format, m.depth = made.depth;
                m.changed = true;
            }
            it = done.emplace(s.data.get(), m).first;
        }
        const Moved &m = it->second;
        if (!m.changed) continue;
        s.data = m.data, s.w = m.w, s.h = m.h, s.format = m.format, s.depth = m.depth;
        s.x = (int)lroundf(s.x * xscale), s.y = (int)lroundf(s.y * yscale);
        count++;
    }
    return count;
}

bool Sff_Export(const SffDoc &doc, const std::vector<int> &sprites, const std::string &folder, bool aligned, std::string *error) {
    int left = 0, top = 0, right = 0, bottom = 0; // of the canvas, from the axis
    for (int i : sprites) {
        const SffSprite &s = doc.sprites[i];
        left = std::max(left, s.x), top = std::max(top, s.y);
        right = std::max(right, s.w - s.x), bottom = std::max(bottom, s.h - s.y);
    }
    std::ofstream list(folder + "\\" + kSffListName);
    list << "; file, group, number, axis x, axis y\n";
    for (int i : sprites) {
        SffSprite s = doc.sprites[i];
        Picture p;
        if (aligned && Read(s, p)) {
            Picture o;
            o.w = left + right, o.h = top + bottom, o.indexed = p.indexed;
            o.px.assign((size_t)o.w * o.h, 0);
            for (int y = 0; y < p.h; y++)
                for (int x = 0; x < p.w; x++) {
                    int cx = left - s.x + x, cy = top - s.y + y;
                    if (cx >= 0 && cy >= 0 && cx < o.w && cy < o.h) o.At(cx, cy) = p.At(x, y);
                }
            Write(s, o);
            s.x = left, s.y = top;
        }
        std::string name = std::to_string(s.group) + "_" + std::to_string(s.number) + ".png";
        if (!Sff_SavePng(doc, s, folder + "\\" + name, error)) return false;
        list << name << ", " << s.group << ", " << s.number << ", " << s.x << ", " << s.y << "\n";
    }
    list.close();
    if (!list) {
        *error = "Could not write " + folder + "\\" + kSffListName;
        return false;
    }
    return true;
}

bool Sff_ReadList(const std::string &path, std::vector<SffListRow> &rows) {
    std::ifstream in(path);
    if (!in) return false;
    rows.clear();
    std::string line;
    while (std::getline(in, line)) {
        line = line.substr(0, line.find(';'));
        size_t comma = line.find(',');
        if (comma == std::string::npos) continue;
        SffListRow r;
        r.file = line.substr(0, comma);
        size_t a = r.file.find_first_not_of(" \t"), b = r.file.find_last_not_of(" \t\r");
        if (a == std::string::npos) continue;
        r.file = r.file.substr(a, b - a + 1);
        if (sscanf(line.c_str() + comma + 1, "%d , %d , %d , %d", &r.group, &r.number, &r.x, &r.y) == 4) rows.push_back(r);
    }
    return !rows.empty();
}

bool Sff_NumbersOfName(const std::string &file, int *group, int *number) {
    size_t slash = file.find_last_of("/\\");
    std::string name = slash == std::string::npos ? file : file.substr(slash + 1);
    name = name.substr(0, name.find_last_of('.'));
    // The last two runs of digits, one separator between them
    size_t end = name.size();
    while (end > 0 && !isdigit((unsigned char)name[end - 1])) end--;
    size_t start = end;
    while (start > 0 && isdigit((unsigned char)name[start - 1])) start--;
    if (start == end || start == 0 || !strchr("_-,", name[start - 1])) return false;
    size_t gEnd = start - 1, gStart = gEnd;
    while (gStart > 0 && isdigit((unsigned char)name[gStart - 1])) gStart--;
    if (gStart == gEnd) return false;
    *group = atoi(name.substr(gStart, gEnd - gStart).c_str());
    *number = atoi(name.substr(start, end - start).c_str());
    return true;
}

int Sff_ImportSprites(SffDoc &doc, const SffDoc &from, const std::vector<int> &sprites, bool replace, int palMode, int *skipped) {
    int shared = Sff_FindPal(doc, 1, 1), count = 0;
    std::map<int, int> ownPal;                     // the source's palette -> its place here
    std::map<const std::string *, SffSprite> adapted; // a picture mapped once
    if (skipped) *skipped = 0;
    for (int i : sprites) {
        if (i < 0 || i >= (int)from.sprites.size()) continue;
        SffSprite s = from.sprites[i];
        int at = Sff_FindSprite(doc, s.group, s.number);
        if (at >= 0 && !replace) {
            if (skipped) (*skipped)++;
            continue;
        }
        if (s.pal >= 0 && s.pal < (int)from.pals.size()) {
            const SffPal &src = from.pals[s.pal];
            if (palMode == SFF_IMPORT_SAME && shared >= 0) {
                s.pal = shared;
            } else if (palMode == SFF_IMPORT_ADAPT && shared >= 0) {
                auto it = adapted.find(s.data.get());
                if (it == adapted.end()) {
                    SffSprite made = s;
                    Picture p;
                    if (Read(s, p)) {
                        const SffPal &to = doc.pals[shared];
                        std::vector<unsigned> table(256, 0);
                        for (int c = 1; c < 256; c++) {
                            int best = 1, bestD = INT_MAX;
                            for (int k = 1; k < to.ncolors; k++) {
                                int d = 0;
                                for (int b = 0; b < 3; b++) {
                                    int e = (int)src.rgba[c * 4 + b] - to.rgba[k * 4 + b];
                                    d += e * e;
                                }
                                if (d < bestD) bestD = d, best = k;
                            }
                            table[c] = (unsigned)best;
                        }
                        for (unsigned &v : p.px) v = table[v & 255];
                        Write(made, p);
                    }
                    it = adapted.emplace(s.data.get(), made).first;
                }
                s.data = it->second.data, s.format = it->second.format, s.depth = it->second.depth;
                s.pal = shared;
            } else {
                auto it = ownPal.find(s.pal);
                if (it == ownPal.end()) {
                    int place = -1;
                    for (int p = 0; p < (int)doc.pals.size() && place < 0; p++)
                        if (doc.pals[p].ncolors == src.ncolors && !memcmp(doc.pals[p].rgba.data() + 4, src.rgba.data() + 4, 1020)) place = p;
                    if (place < 0) {
                        SffPal p = src;
                        if (Sff_FindPal(doc, p.group, p.number) >= 0) {
                            p.number = std::max(p.number, 1000); // past a character's own numbers
                            while (Sff_FindPal(doc, p.group, p.number) >= 0) p.number++;
                        }
                        doc.pals.push_back(p);
                        place = (int)doc.pals.size() - 1;
                    }
                    it = ownPal.emplace(s.pal, place).first;
                }
                s.pal = it->second;
            }
        }
        if (at >= 0)
            doc.sprites[at] = s;
        else
            doc.sprites.push_back(s);
        count++;
    }
    return count;
}

// Palettes taken out (flags by palette), the sprites' indices following
static int RemovePals(SffDoc &doc, const std::vector<char> &out, const std::vector<int> &sameAs) {
    std::vector<int> place(doc.pals.size(), -1);
    std::vector<SffPal> kept;
    for (int i = 0; i < (int)doc.pals.size(); i++)
        if (!out[i]) {
            place[i] = (int)kept.size();
            kept.push_back(doc.pals[i]);
        }
    for (SffSprite &s : doc.sprites)
        if (s.pal >= 0 && s.pal < (int)place.size()) s.pal = place[out[s.pal] ? sameAs[s.pal] : s.pal];
    int removed = (int)doc.pals.size() - (int)kept.size();
    doc.pals.swap(kept);
    return removed;
}

int Sff_RemoveUnusedPals(SffDoc &doc) {
    std::vector<char> used(doc.pals.size(), 0), out(doc.pals.size(), 0);
    for (const SffSprite &s : doc.sprites)
        if (s.pal >= 0 && s.pal < (int)used.size()) used[s.pal] = 1;
    for (size_t i = 0; i < doc.pals.size(); i++) out[i] = !used[i] && doc.pals[i].group != 1;
    return RemovePals(doc, out, std::vector<int>(doc.pals.size(), 0));
}

int Sff_MergeDuplicatePals(SffDoc &doc) {
    std::vector<char> out(doc.pals.size(), 0);
    std::vector<int> sameAs(doc.pals.size(), 0);
    for (int i = 0; i < (int)doc.pals.size(); i++) {
        const SffPal &p = doc.pals[i];
        if (p.group == 1) continue;
        for (int j = 0; j < (int)doc.pals.size() && !out[i]; j++) {
            const SffPal &q = doc.pals[j];
            if (j == i || out[j] || (q.group != 1 && j > i) || q.ncolors != p.ncolors) continue;
            if (!memcmp(p.rgba.data() + 4, q.rgba.data() + 4, 1020)) out[i] = 1, sameAs[i] = j;
        }
    }
    return RemovePals(doc, out, sameAs);
}

bool Sff_DeletePal(SffDoc &doc, int pal) {
    if (pal < 0 || pal >= (int)doc.pals.size()) return false;
    for (const SffSprite &s : doc.sprites)
        if (s.pal == pal) return false;
    std::vector<char> out(doc.pals.size(), 0);
    out[pal] = 1;
    return RemovePals(doc, out, std::vector<int>(doc.pals.size(), 0)) == 1;
}

int Sff_UsePal(SffDoc &doc, const std::vector<int> &sprites, int pal) {
    int count = 0;
    for (int i : sprites) {
        SffSprite &s = doc.sprites[i];
        if (s.pal < 0 || s.pal == pal) continue;
        s.pal = pal;
        count++;
    }
    return count;
}

int Sff_AdaptToPal(SffDoc &doc, const std::vector<int> &sprites, int pal) {
    if (pal < 0 || pal >= (int)doc.pals.size()) return 0;
    const SffPal &to = doc.pals[pal];
    std::map<const std::string *, SffSprite> done;
    std::map<unsigned, unsigned> nearest; // by colour
    auto index = [&](unsigned rgb) {
        auto it = nearest.find(rgb);
        if (it != nearest.end()) return it->second;
        int best = 1, bestD = INT_MAX;
        for (int k = 1; k < to.ncolors && bestD; k++) {
            int d = 0;
            for (int b = 0; b < 3; b++) {
                int e = (int)((rgb >> (8 * b)) & 255) - to.rgba[k * 4 + b];
                d += e * e;
            }
            if (d < bestD) bestD = d, best = k;
        }
        return nearest[rgb] = (unsigned)best;
    };
    int count = 0;
    for (int i : sprites) {
        SffSprite &s = doc.sprites[i];
        if (s.pal == pal) continue;
        auto it = done.find(s.data.get());
        if (it == done.end()) {
            SffSprite made = s;
            Picture p;
            if (Read(s, p)) {
                const unsigned char *from = s.pal >= 0 && s.pal < (int)doc.pals.size() ? doc.pals[s.pal].rgba.data() : nullptr;
                for (unsigned &v : p.px) {
                    if (p.Empty(v)) {
                        v = 0;
                    } else if (p.indexed) {
                        const unsigned char *c = from ? from + v * 4 : nullptr;
                        v = c ? index((unsigned)c[0] | (unsigned)c[1] << 8 | (unsigned)c[2] << 16) : 1;
                    } else {
                        v = (v >> 24) < 128 ? 0 : index(v & 0xFFFFFF);
                    }
                }
                p.indexed = true;
                Write(made, p);
            }
            it = done.emplace(s.data.get(), made).first;
        }
        s.data = it->second.data, s.format = it->second.format, s.depth = it->second.depth;
        s.pal = pal;
        count++;
    }
    return count;
}

std::vector<unsigned char> Sff_PalOfSprites(const SffDoc &doc, const std::vector<int> &sprites) {
    std::map<unsigned, int> colors;
    std::map<const std::string *, char> seen;
    for (int i : sprites) {
        const SffSprite &s = doc.sprites[i];
        if (!seen.emplace(s.data.get(), 1).second) continue;
        Picture p;
        if (!Read(s, p)) continue;
        const unsigned char *pal = s.pal >= 0 && s.pal < (int)doc.pals.size() ? doc.pals[s.pal].rgba.data() : nullptr;
        for (unsigned v : p.px) {
            if (p.Empty(v) || (p.indexed && !pal)) continue;
            if (p.indexed) colors[(unsigned)pal[v * 4] << 16 | (unsigned)pal[v * 4 + 1] << 8 | pal[v * 4 + 2]]++;
            else if ((v >> 24) >= 128) colors[(v & 255) << 16 | (v & 0xFF00) | ((v >> 16) & 255)]++;
        }
    }
    return Sff_MakePalette(colors);
}
