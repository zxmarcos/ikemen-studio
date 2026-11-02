#include "sff.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

#include "lodepng.h"

namespace {

const unsigned kSffV101 = 0x01000100, kSffV200 = 0x02000000, kSffV201 = 0x02000100;
const unsigned kTdataFlag = 1; // SFF v2 sprite flag: its data is in tdata
const size_t kPrefix = 4;      // sprite data starts with the decoded size
const size_t kMaxPixels = 64u << 20;

unsigned U32(const std::string &b, size_t at) {
    return (unsigned char)b[at] | (unsigned char)b[at + 1] << 8 | (unsigned char)b[at + 2] << 16 |
           (unsigned)(unsigned char)b[at + 3] << 24;
}
unsigned U16(const std::string &b, size_t at) { return (unsigned char)b[at] | (unsigned char)b[at + 1] << 8; }
void Put32(std::string &b, size_t at, unsigned v) {
    for (int i = 0; i < 4; i++) b[at + i] = (char)(v >> (8 * i));
}
void Add16(std::string &b, unsigned v) {
    b += (char)v;
    b += (char)(v >> 8);
}
void Add32(std::string &b, unsigned v) {
    Add16(b, v & 0xFFFF);
    Add16(b, v >> 16);
}

bool ReadFile(const std::string &path, std::string &out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// An 8-bit PCX image (an SFF v1.01 subfile): its pixels, and its palette when it has one.
bool DecodePcx(const unsigned char *p, size_t n, int *w, int *h, std::vector<unsigned char> &px, unsigned char *pal,
               bool *hasPal) {
    if (n < 128 || p[0] != 10 || p[3] != 8 || p[65] != 1) return false;
    auto s16 = [p](int at) { return (int)(short)(p[at] | p[at + 1] << 8); };
    *w = s16(8) - s16(4) + 1;
    *h = s16(10) - s16(6) + 1;
    int bpl = p[66] | p[67] << 8;
    if (*w <= 0 || *h <= 0 || bpl < *w || (size_t)*w * *h > kMaxPixels) return false;
    px.assign((size_t)*w * *h, 0);
    std::vector<unsigned char> line(bpl);
    size_t at = 128;
    for (int y = 0; y < *h; y++) {
        int k = 0;
        while (k < bpl && at < n) {
            unsigned char b = p[at++];
            int count = 1;
            if ((b & 0xC0) == 0xC0) {
                count = b & 0x3F;
                if (at >= n) break;
                b = p[at++];
            }
            while (count-- > 0 && k < bpl) line[k++] = b;
        }
        memcpy(&px[(size_t)y * *w], line.data(), *w);
    }
    *hasPal = n >= 128 + 769 && p[n - 769] == 12;
    for (int i = 0; i < 256; i++) {
        for (int k = 0; k < 3; k++) pal[i * 4 + k] = *hasPal ? p[n - 768 + i * 3 + k] : 0;
        pal[i * 4 + 3] = i == 0 ? 0 : 255;
    }
    return true;
}

// RLE8 (format 2): 0x40 | n then a value is a run of n pixels, any other byte a pixel.
void Rle8Decode(const std::string &d, std::vector<unsigned char> &out) {
    size_t o = 0, i = kPrefix;
    while (i < d.size() && o < out.size()) {
        unsigned char b = (unsigned char)d[i++];
        if ((b & 0xC0) == 0x40) {
            if (i >= d.size()) break;
            unsigned char v = (unsigned char)d[i++];
            for (int n = b & 0x3F; n > 0 && o < out.size(); n--) out[o++] = v;
        } else {
            out[o++] = b;
        }
    }
}

std::shared_ptr<std::string> Rle8Encode(const std::vector<unsigned char> &px) {
    auto out = std::make_shared<std::string>(kPrefix, '\0');
    Put32(*out, 0, (unsigned)px.size());
    auto run = [&](int count, unsigned char v) {
        if (count == 1 && (v & 0xC0) != 0x40) {
            *out += (char)v;
        } else if (count > 0) {
            *out += (char)(0x40 | count);
            *out += (char)v;
        }
    };
    size_t i = 0;
    while (i < px.size()) {
        unsigned char v = px[i];
        int count = 1;
        while (i + count < px.size() && px[i + count] == v && count < 63) count++;
        run(count, v);
        i += count;
    }
    return out;
}

// RLE5 (format 3): packets of a run of one colour then 5-bit pixels with 3-bit repeats.
void Rle5Decode(const std::string &d, std::vector<unsigned char> &out) {
    const unsigned char *src = (const unsigned char *)d.data();
    size_t i = kPrefix, o = 0, n = d.size();
    while (i + 1 < n && o < out.size()) {
        int runLen = src[i] + 1;
        unsigned char ctrl = src[i + 1];
        int count = ctrl & 0x7F;
        i += 2;
        unsigned char color = 0;
        if (ctrl & 0x80) {
            if (i >= n) break;
            color = src[i++];
        }
        for (; runLen > 0 && o < out.size(); runLen--) out[o++] = color;
        for (; count > 0 && i < n; count--) {
            unsigned char b = src[i++];
            for (int k = (b >> 5) + 1; k > 0 && o < out.size(); k--) out[o++] = b & 0x1F;
        }
    }
}

// LZ5 (format 4): a flag byte before each 8 codes; a set bit copies from earlier output (the
// top 2 bits of 4 short copies make another offset byte), a clear bit is a run of one colour.
void Lz5Decode(const std::string &d, std::vector<unsigned char> &out) {
    const unsigned char *src = (const unsigned char *)d.data();
    size_t i = kPrefix, n = d.size(), pos = 0;
    int bits = 0, recBits = 0;
    unsigned char flags = 0, rec = 0;
    while (i < n && pos < out.size()) {
        if (bits == 0) {
            flags = src[i++];
            bits = 8;
            if (i >= n) break;
        }
        bits--;
        unsigned char c = src[i];
        if (flags & 1) {
            int len = c & 0x3F;
            size_t offset;
            if (len != 0) {
                recBits += 2;
                rec = (unsigned char)(c >> 6 | rec << 2);
                len++;
                if (recBits == 8) {
                    offset = rec;
                    recBits = 0;
                } else {
                    if (++i >= n) break;
                    offset = src[i];
                }
            } else {
                if (i + 2 >= n) break;
                offset = src[i + 1] | (c & 0xC0) << 2;
                i += 2;
                len = src[i] + 3;
            }
            if (offset + 1 > pos) break; // before the start of the output
            for (size_t from = pos - offset - 1; len > 0 && pos < out.size(); len--) out[pos++] = out[from++];
        } else {
            int len = c >> 5;
            if (len == 0) {
                if (++i >= n) break;
                len = src[i] + 8;
            }
            for (; len > 0 && pos < out.size(); len--) out[pos++] = c & 0x1F;
        }
        flags >>= 1;
        i++;
    }
}

bool LoadV2(SffDoc &d, const std::string &f, unsigned version, std::string *error) {
    unsigned sprAt = U32(f, 0x24), sprN = U32(f, 0x28), palAt = U32(f, 0x2C), palN = U32(f, 0x30);
    unsigned ldata = U32(f, 0x34), tdata = U32(f, 0x3C);
    if ((size_t)sprAt + (size_t)sprN * 28 > f.size() || (size_t)palAt + (size_t)palN * 16 > f.size()) {
        *error = "the file ends early";
        return false;
    }
    for (unsigned i = 0; i < palN; i++) {
        size_t at = palAt + (size_t)i * 16;
        SffPal p;
        p.group = (int)U16(f, at);
        p.number = (int)U16(f, at + 2);
        p.rgba.assign(1024, 0);
        unsigned ncolors = std::min(U16(f, at + 4), 256u), link = U16(f, at + 6);
        unsigned off = U32(f, at + 8), size = U32(f, at + 12);
        if (size != 0) {
            if ((size_t)ldata + off + ncolors * 4 > f.size()) {
                *error = "palette " + std::to_string(i) + " is past the end of the file";
                return false;
            }
            memcpy(p.rgba.data(), f.data() + ldata + off, ncolors * 4);
        } else if (link < i) {
            p.rgba = d.pals[link].rgba;
        }
        p.ncolors = ncolors != 0 ? (int)ncolors : link < i ? d.pals[link].ncolors : 256;
        if (version == kSffV200) // v2.00 palettes have no alpha
            for (int k = 0; k < 256; k++) p.rgba[k * 4 + 3] = k == 0 ? 0 : 255;
        d.pals.push_back(p);
    }
    for (unsigned i = 0; i < sprN; i++) {
        size_t at = sprAt + (size_t)i * 28;
        SffSprite s;
        s.group = (int)U16(f, at);
        s.number = (int)U16(f, at + 2);
        s.w = (int)U16(f, at + 4);
        s.h = (int)U16(f, at + 6);
        s.x = (short)U16(f, at + 8);
        s.y = (short)U16(f, at + 10);
        unsigned link = U16(f, at + 12);
        s.format = (unsigned char)f[at + 14];
        s.depth = (unsigned char)f[at + 15];
        unsigned off = U32(f, at + 16), size = U32(f, at + 20), pal = U16(f, at + 24), flags = U16(f, at + 26);
        if (size != 0) {
            size_t from = (size_t)((flags & kTdataFlag) ? tdata : ldata) + off;
            if (from + size > f.size()) {
                *error = "sprite " + std::to_string(s.group) + ", " + std::to_string(s.number) + " is past the end of the file";
                return false;
            }
            s.data = std::make_shared<std::string>(f, from, size);
        } else if (link < i) {
            const SffSprite &src = d.sprites[link];
            s.data = src.data;
            s.w = src.w, s.h = src.h, s.format = src.format, s.depth = src.depth;
        } else {
            *error = "sprite " + std::to_string(i) + " is linked to a later one";
            return false;
        }
        s.pal = s.depth <= 8 && pal < palN ? (int)pal : -1;
        d.sprites.push_back(s);
    }
    return true;
}

// An ACT palette: 256 RGB colours, last first; colour 0 is transparent.
bool ReadAct(const std::string &path, std::vector<unsigned char> &rgba) {
    std::string f;
    if (!ReadFile(path, f) || f.size() < 768) return false;
    rgba.assign(1024, 0);
    for (int i = 0; i < 256; i++) {
        unsigned char *c = &rgba[(255 - i) * 4];
        for (int k = 0; k < 3; k++) c[k] = (unsigned char)f[i * 3 + k];
        c[3] = 255;
    }
    rgba[3] = 0;
    return true;
}

bool LoadV1(SffDoc &d, const std::string &f, const std::string &pal1, std::string *error) {
    int count = (int)U32(f, 0x14);
    size_t at = U32(f, 0x18);
    int lastPal = -1;
    for (int i = 0; i < count; i++) {
        if (at + 32 > f.size()) {
            *error = "the file ends early (sprite " + std::to_string(i) + ")";
            return false;
        }
        size_t next = U32(f, at), size = U32(f, at + 4);
        SffSprite s;
        s.x = (short)U16(f, at + 8);
        s.y = (short)U16(f, at + 10);
        s.group = (short)U16(f, at + 12);
        s.number = (short)U16(f, at + 14);
        int link = (short)U16(f, at + 16);
        bool same = f[at + 18] != 0;
        if (size > 0) {
            // the size counts the 768-byte palette of a sprite sharing the previous one, which is not there
            size_t end = next > at + 32 && next <= f.size() ? next : f.size();
            size = std::min(size, end - std::min(end, at + 32));
            std::vector<unsigned char> px;
            unsigned char pal[1024];
            bool hasPal = false;
            if (!DecodePcx((const unsigned char *)f.data() + at + 32, size, &s.w, &s.h, px, pal, &hasPal)) {
                *error = "sprite " + std::to_string(s.group) + ", " + std::to_string(s.number) + " is not an 8-bit PCX image";
                return false;
            }
            if (lastPal == -1 || !same) {
                SffPal p;
                p.number = (int)d.pals.size();
                p.rgba.assign(pal, pal + 1024);
                d.pals.push_back(p);
                lastPal = (int)d.pals.size() - 1;
            }
            s.pal = lastPal;
            s.format = 2;
            s.depth = 8;
            s.data = Rle8Encode(px);
        } else {
            if (link < 0 || link >= i) {
                *error = "sprite " + std::to_string(i) + " is linked to a later one";
                return false;
            }
            const SffSprite &src = d.sprites[link];
            s.w = src.w, s.h = src.h, s.format = src.format, s.depth = src.depth, s.data = src.data, s.pal = src.pal;
        }
        d.sprites.push_back(s);
        if (next <= at) break;
        at = next;
    }
    d.converted = true;
    int s0 = Sff_FindSprite(d, 0, 0), s9000 = Sff_FindSprite(d, 9000, 0);
    SffPal p11;
    if (pal1.empty() || s0 < 0 || !ReadAct(pal1, p11.rgba)) return true;
    int shared0 = d.sprites[s0].pal, shared9000 = s9000 >= 0 ? d.sprites[s9000].pal : -1;
    p11.group = p11.number = 1;
    d.pals.push_back(p11);
    int index = (int)d.pals.size() - 1;
    for (SffSprite &sp : d.sprites)
        if (sp.pal != -1 && (sp.pal == shared0 || sp.pal == shared9000)) sp.pal = index;
    return true;
}

struct Image {
    std::string path;
    int w = 0, h = 0;
    bool indexed = false;
    std::vector<unsigned char> px;  // indices, or RGBA
    std::vector<unsigned char> pal; // indexed: 256 RGBA colours, 0 transparent
};

SffImageReader g_imageReader;

// PCX: indexed, colour 0 transparent. Others, read by the app: RGBA; with nothing transparent (BMP),
// the colour of the top left corner is.
bool LoadOther(const std::string &f, bool pcx, Image &img) {
    if (pcx) {
        img.pal.assign(1024, 0);
        bool hasPal = false;
        if (!DecodePcx((const unsigned char *)f.data(), f.size(), &img.w, &img.h, img.px, img.pal.data(), &hasPal)) return false;
        img.indexed = true;
        return true;
    }
    if (!g_imageReader || !g_imageReader((const unsigned char *)f.data(), f.size(), img.w, img.h, img.px)) return false;
    img.indexed = false;
    bool solid = true;
    for (size_t i = 3; i < img.px.size() && solid; i += 4) solid = img.px[i] == 255;
    for (size_t i = 0; solid && i + 3 < img.px.size(); i += 4)
        if (!memcmp(&img.px[i], img.px.data(), 3)) img.px[i + 3] = 0;
    return true;
}

bool LoadPng(const std::string &path, Image &img, std::string *error) {
    std::string f;
    img.path = path;
    if (!ReadFile(path, f)) {
        *error = "Can't read " + path;
        return false;
    }
    size_t dot = path.find_last_of('.');
    std::string ext = dot == std::string::npos ? "" : path.substr(dot + 1);
    for (char &ch : ext) ch = (char)tolower((unsigned char)ch);
    if (ext == "pcx" || ext == "bmp" || ext == "gif") {
        if (LoadOther(f, ext == "pcx", img)) return true;
        *error = path + " could not be read";
        return false;
    }
    lodepng::State state;
    unsigned w = 0, h = 0;
    if (lodepng_inspect(&w, &h, &state, (const unsigned char *)f.data(), f.size()) != 0) {
        *error = path + " is not a PNG file";
        return false;
    }
    if (state.info_png.color.colortype == LCT_PALETTE) {
        state.info_raw.colortype = LCT_PALETTE;
        state.info_raw.bitdepth = 8;
        if (lodepng::decode(img.px, w, h, state, (const unsigned char *)f.data(), f.size()) == 0) {
            img.indexed = true;
            img.w = (int)w, img.h = (int)h;
            img.pal.assign(1024, 0);
            const LodePNGColorMode &c = state.info_png.color;
            for (size_t i = 0; i < c.palettesize && i < 256; i++) memcpy(&img.pal[i * 4], &c.palette[i * 4], 4);
            // MUGEN's transparent colour is 0: a PNG's transparent colour takes its place
            int t = -1;
            for (size_t i = 0; i < c.palettesize && i < 256 && t < 0; i++)
                if (img.pal[i * 4 + 3] < 128) t = (int)i;
            if (t > 0) {
                for (unsigned char &px : img.px) px = px == t ? 0 : px == 0 ? (unsigned char)t : px;
                for (int k = 0; k < 4; k++) std::swap(img.pal[k], img.pal[t * 4 + k]);
            }
            for (int i = 0; i < 256; i++) img.pal[i * 4 + 3] = i == 0 ? 0 : 255;
            return true;
        }
    }
    img.px.clear();
    if (lodepng::decode(img.px, w, h, (const unsigned char *)f.data(), f.size(), LCT_RGBA, 8) != 0) {
        *error = path + " is not a PNG file";
        return false;
    }
    img.indexed = false;
    img.w = (int)w, img.h = (int)h;
    return true;
}

unsigned Rgb(const unsigned char *c) { return (unsigned)c[0] << 16 | (unsigned)c[1] << 8 | c[2]; }

void CountColors(const Image &img, std::map<unsigned, int> &colors) {
    size_t n = (size_t)img.w * img.h;
    for (size_t i = 0; i < n; i++) {
        if (img.indexed) {
            if (img.px[i] != 0) colors[Rgb(&img.pal[img.px[i] * 4])]++;
        } else if (img.px[i * 4 + 3] >= 128) {
            colors[Rgb(&img.px[i * 4])]++;
        }
    }
}

// A palette of up to 255 colours (1..255; 0 transparent): all of them, or a median cut.
std::vector<unsigned char> MakePalette(const std::map<unsigned, int> &colors) {
    std::vector<unsigned char> pal(1024, 0);
    std::vector<std::vector<std::pair<unsigned, int>>> boxes;
    if (!colors.empty()) boxes.emplace_back(colors.begin(), colors.end());
    auto channel = [](unsigned c, int k) { return (int)(c >> (16 - 8 * k)) & 0xFF; };
    while (boxes.size() < 255) {
        int best = -1, bestK = 0, bestRange = 0;
        for (int b = 0; b < (int)boxes.size(); b++) {
            if (boxes[b].size() < 2) continue;
            for (int k = 0; k < 3; k++) {
                int lo = 255, hi = 0;
                for (auto &e : boxes[b]) lo = std::min(lo, channel(e.first, k)), hi = std::max(hi, channel(e.first, k));
                if (hi - lo > bestRange) bestRange = hi - lo, best = b, bestK = k;
            }
        }
        if (best < 0) break;
        auto &box = boxes[best];
        std::sort(box.begin(), box.end(), [&](const std::pair<unsigned, int> &a, const std::pair<unsigned, int> &b) {
            return channel(a.first, bestK) < channel(b.first, bestK);
        });
        long long total = 0, half = 0;
        for (auto &e : box) total += e.second;
        size_t cut = 1;
        for (; cut < box.size() - 1; cut++) {
            half += box[cut - 1].second;
            if (half * 2 >= total) break;
        }
        std::vector<std::pair<unsigned, int>> rest(box.begin() + cut, box.end());
        box.resize(cut);
        boxes.push_back(rest);
    }
    for (int b = 0; b < (int)boxes.size(); b++) {
        long long sum[3] = {0, 0, 0}, n = 0;
        for (auto &e : boxes[b]) {
            for (int k = 0; k < 3; k++) sum[k] += (long long)channel(e.first, k) * e.second;
            n += e.second;
        }
        for (int k = 0; k < 3; k++) pal[(b + 1) * 4 + k] = (unsigned char)(n > 0 ? sum[k] / n : 0);
        pal[(b + 1) * 4 + 3] = 255;
    }
    return pal;
}

// The image as indices of pal (the nearest of colours 1..ncolors-1); *inexact counts colours not in it.
std::vector<unsigned char> MapToPalette(const Image &img, const std::vector<unsigned char> &pal, int ncolors, int *inexact) {
    std::map<unsigned, unsigned char> cache;
    size_t n = (size_t)img.w * img.h;
    std::vector<unsigned char> out(n, 0);
    for (size_t i = 0; i < n; i++) {
        const unsigned char *c;
        if (img.indexed) {
            if (img.px[i] == 0) continue;
            c = &img.pal[img.px[i] * 4];
        } else {
            if (img.px[i * 4 + 3] < 128) continue;
            c = &img.px[i * 4];
        }
        auto it = cache.find(Rgb(c));
        if (it == cache.end()) {
            int best = 1, bestD = INT_MAX;
            for (int k = 1; k < ncolors && bestD != 0; k++) {
                int dr = c[0] - pal[k * 4], dg = c[1] - pal[k * 4 + 1], db = c[2] - pal[k * 4 + 2];
                int dd = dr * dr + dg * dg + db * db;
                if (dd < bestD) bestD = dd, best = k;
            }
            if (bestD != 0) (*inexact)++;
            it = cache.emplace(Rgb(c), (unsigned char)best).first;
        }
        out[i] = it->second;
    }
    return out;
}

int DepthFor(int ncolors) {
    int depth = 1;
    while ((1 << depth) < ncolors && depth < 8) depth++;
    return depth;
}

bool SamePalette(const std::vector<unsigned char> &a, const std::vector<unsigned char> &b) {
    for (int i = 4; i < 1024; i++)
        if ((i & 3) != 3 && a[i] != b[i]) return false;
    return true;
}

} // namespace

int Sff_FindSprite(const SffDoc &d, int group, int number) {
    for (int i = 0; i < (int)d.sprites.size(); i++)
        if (d.sprites[i].group == group && d.sprites[i].number == number) return i;
    return -1;
}

int Sff_FindPal(const SffDoc &d, int group, int number) {
    for (int i = 0; i < (int)d.pals.size(); i++)
        if (d.pals[i].group == group && d.pals[i].number == number) return i;
    return -1;
}

bool Sff_ReadAct(const std::string &path, std::vector<unsigned char> &rgba) { return ReadAct(path, rgba); }

std::string Sff_ActBytes(const std::vector<unsigned char> &rgba) {
    std::string out(768, '\0');
    for (int i = 0; i < 256 && (255 - i) * 4 + 2 < (int)rgba.size(); i++)
        for (int k = 0; k < 3; k++) out[i * 3 + k] = (char)rgba[(255 - i) * 4 + k];
    return out;
}

bool Sff_Load(SffDoc &d, const std::string &path, const std::string &pal1) {
    d = SffDoc();
    d.path = path;
    std::string f, error;
    if (!ReadFile(path, f)) {
        d.error = "Can't read " + path;
        return false;
    }
    if (f.size() < 0x200 || f.compare(0, 12, std::string("ElecbyteSpr\0", 12)) != 0) {
        d.error = path + " is not an SFF file";
        return false;
    }
    unsigned version = U32(f, 0x0C);
    bool ok = version == kSffV101 ? LoadV1(d, f, pal1, &error) : LoadV2(d, f, version, &error);
    if (!ok) {
        d.pals.clear();
        d.sprites.clear();
        d.error = path + ": " + error;
    }
    return ok;
}

std::string Sff_Build(const SffDoc &d) {
    // The palettes that sprites use and those of group 1 (the character's)
    std::vector<int> palIndex(d.pals.size(), -1), pals;
    for (int i = 0; i < (int)d.pals.size(); i++) {
        bool used = d.pals[i].group == 1;
        for (const SffSprite &s : d.sprites) used = used || s.pal == i;
        if (used) {
            palIndex[i] = (int)pals.size();
            pals.push_back(i);
        }
    }
    bool png = false;
    for (const SffSprite &s : d.sprites) png = png || s.format >= 10;
    std::string out(0x200, '\0');
    memcpy(&out[0], "ElecbyteSpr", 12);
    Put32(out, 0x0C, kSffV201);
    Put32(out, 0x18, png ? kSffV201 : kSffV200);

    // ldata: the palettes, then the sprites' data (once for linked sprites)
    std::string ldata;
    std::vector<unsigned> palOffset;
    for (int i : pals) {
        palOffset.push_back((unsigned)ldata.size());
        ldata.append((const char *)d.pals[i].rgba.data(), d.pals[i].ncolors * 4);
    }
    std::map<const std::string *, int> first;
    std::vector<unsigned> sprOffset(d.sprites.size(), 0);
    for (int i = 0; i < (int)d.sprites.size(); i++) {
        const std::string *data = d.sprites[i].data.get();
        if (first.count(data) == 0) {
            first[data] = i;
            sprOffset[i] = (unsigned)ldata.size();
            ldata += *data;
        }
    }
    unsigned sprAt = 0x200, palAt = sprAt + (unsigned)d.sprites.size() * 28;
    unsigned ldataAt = palAt + (unsigned)pals.size() * 16;
    Put32(out, 0x24, sprAt);
    Put32(out, 0x28, (unsigned)d.sprites.size());
    Put32(out, 0x2C, palAt);
    Put32(out, 0x30, (unsigned)pals.size());
    Put32(out, 0x34, ldataAt);
    Put32(out, 0x38, (unsigned)ldata.size());
    Put32(out, 0x3C, ldataAt + (unsigned)ldata.size());
    Put32(out, 0x40, 0);
    for (int i = 0; i < (int)d.sprites.size(); i++) {
        const SffSprite &s = d.sprites[i];
        int src = first[s.data.get()];
        Add16(out, (unsigned)s.group);
        Add16(out, (unsigned)s.number);
        Add16(out, (unsigned)s.w);
        Add16(out, (unsigned)s.h);
        Add16(out, (unsigned)(unsigned short)s.x);
        Add16(out, (unsigned)(unsigned short)s.y);
        Add16(out, src == i ? 0 : (unsigned)src);
        out += (char)s.format;
        out += (char)s.depth;
        Add32(out, sprOffset[src]);
        Add32(out, src == i ? (unsigned)s.data->size() : 0);
        Add16(out, s.pal >= 0 && palIndex[s.pal] >= 0 ? (unsigned)palIndex[s.pal] : 0);
        Add16(out, 0);
    }
    for (int k = 0; k < (int)pals.size(); k++) {
        const SffPal &p = d.pals[pals[k]];
        Add16(out, (unsigned)p.group);
        Add16(out, (unsigned)p.number);
        Add16(out, (unsigned)p.ncolors);
        Add16(out, 0);
        Add32(out, palOffset[k]);
        Add32(out, (unsigned)p.ncolors * 4);
    }
    return out + ldata;
}

bool Sff_Decode(const SffSprite &s, std::vector<unsigned char> &px, int *bpp) {
    if (!s.data) return false;
    const std::string &d = *s.data;
    *bpp = s.depth <= 8 ? 1 : s.depth / 8;
    size_t need = (size_t)s.w * s.h * *bpp;
    if (need == 0 || need > kMaxPixels * 4) return false;
    px.assign(need, 0);
    switch (s.format) {
    case 0:
        memcpy(px.data(), d.data(), std::min(need, d.size()));
        return true;
    case 2:
        Rle8Decode(d, px);
        return true;
    case 3:
        Rle5Decode(d, px);
        return true;
    case 4:
        Lz5Decode(d, px);
        return true;
    case 10:
    case 11:
    case 12: {
        if (d.size() <= kPrefix) return false;
        lodepng::State state;
        state.info_raw.colortype = s.format == 10 ? LCT_PALETTE : s.format == 11 ? LCT_RGB : LCT_RGBA;
        state.info_raw.bitdepth = 8;
        std::vector<unsigned char> out;
        unsigned w = 0, h = 0;
        if (lodepng::decode(out, w, h, state, (const unsigned char *)d.data() + kPrefix, d.size() - kPrefix) != 0)
            return false;
        memcpy(px.data(), out.data(), std::min(need, out.size()));
        return true;
    }
    default:
        return false;
    }
}

void Sff_ToRgba(const SffDoc &d, const SffSprite &s, const std::vector<unsigned char> &px, int bpp,
                std::vector<unsigned char> &rgba, int palOverride) {
    int p = palOverride >= 0 && palOverride < (int)d.pals.size() ? palOverride : s.pal;
    Sff_ToRgbaWith(s, px, bpp, rgba, p >= 0 && p < (int)d.pals.size() ? d.pals[p].rgba.data() : nullptr);
}

void Sff_ToRgbaWith(const SffSprite &s, const std::vector<unsigned char> &px, int bpp, std::vector<unsigned char> &rgba,
                    const unsigned char *pal) {
    size_t n = (size_t)s.w * s.h;
    rgba.assign(n * 4, 0);
    for (size_t i = 0; i < n && i * bpp < px.size(); i++) {
        unsigned char *o = &rgba[i * 4];
        if (bpp == 1) {
            unsigned char c = px[i];
            if (pal) {
                memcpy(o, pal + c * 4, 3);
            } else {
                o[0] = o[1] = o[2] = c;
            }
            o[3] = c == 0 ? 0 : 255;
        } else {
            memcpy(o, &px[i * bpp], 3);
            o[3] = bpp == 4 ? px[i * 4 + 3] : 255;
        }
    }
}

bool Sff_SavePng(const SffDoc &d, const SffSprite &s, const std::string &path, std::string *error) {
    std::vector<unsigned char> px, rgba, png;
    int bpp = 0;
    if (!Sff_Decode(s, px, &bpp)) {
        *error = "can't decode sprite " + std::to_string(s.group) + ", " + std::to_string(s.number);
        return false;
    }
    unsigned err;
    if (bpp == 1 && s.pal >= 0) {
        // Indexed, with its palette; colour 0 transparent
        lodepng::State state;
        state.info_raw.colortype = LCT_PALETTE;
        state.info_raw.bitdepth = 8;
        state.info_png.color.colortype = LCT_PALETTE;
        state.info_png.color.bitdepth = 8;
        state.encoder.auto_convert = 0;
        const unsigned char *pal = d.pals[s.pal].rgba.data();
        for (int i = 0; i < 256; i++) {
            unsigned char a = i == 0 ? 0 : 255;
            lodepng_palette_add(&state.info_raw, pal[i * 4], pal[i * 4 + 1], pal[i * 4 + 2], a);
            lodepng_palette_add(&state.info_png.color, pal[i * 4], pal[i * 4 + 1], pal[i * 4 + 2], a);
        }
        err = lodepng::encode(png, px, (unsigned)s.w, (unsigned)s.h, state);
    } else {
        Sff_ToRgba(d, s, px, bpp, rgba);
        err = lodepng::encode(png, rgba, (unsigned)s.w, (unsigned)s.h);
    }
    if (err == 0) err = lodepng::save_file(png, path);
    if (err != 0) {
        *error = path + ": " + lodepng_error_text(err);
        return false;
    }
    return true;
}

static std::vector<SffSprite> SpritesOfImages(SffDoc &d, const std::vector<Image> &images, int palMode,
                                              const std::function<void(const std::string &)> &log) {
    std::vector<SffSprite> out;
    bool shared = palMode == SFF_PAL_SHARED || palMode == SFF_PAL_SAME;
    int p11 = Sff_FindPal(d, 1, 1);
    int one = -1; // the palette all of them share, of their own
    if (palMode == SFF_PAL_ONE && !images.empty()) {
        std::map<unsigned, int> colors;
        for (const Image &img : images) CountColors(img, colors);
        SffPal p;
        while (Sff_FindPal(d, 0, p.number) >= 0) p.number++;
        p.rgba = MakePalette(colors);
        if (colors.size() > 255) log(std::to_string(colors.size()) + " colours in the images: their palette has the 255 that stand for them best");
        d.pals.push_back(p);
        one = (int)d.pals.size() - 1;
    }
    if (shared && p11 < 0 && !images.empty()) {
        bool same = true;
        for (const Image &img : images) same = same && img.indexed && SamePalette(img.pal, images[0].pal);
        SffPal p;
        p.group = p.number = 1;
        if (same) {
            p.rgba = images[0].pal;
        } else {
            std::map<unsigned, int> colors;
            for (const Image &img : images) CountColors(img, colors);
            p.rgba = MakePalette(colors);
            if (colors.size() > 255)
                log(std::to_string(colors.size()) + " colours in the images: palette 1,1 has the 255 that stand for them best");
        }
        d.pals.push_back(p);
        p11 = (int)d.pals.size() - 1;
        log("Palette 1,1 made from the images");
    }
    for (const Image &img : images) {
        SffSprite s;
        s.w = img.w, s.h = img.h;
        std::vector<unsigned char> px;
        if (one >= 0) {
            int inexact = 0;
            px = MapToPalette(img, d.pals[one].rgba, 256, &inexact);
            s.pal = one;
        } else if (shared) {
            const std::vector<unsigned char> &pal = d.pals[p11].rgba;
            if (img.indexed && (palMode == SFF_PAL_SAME || SamePalette(img.pal, pal))) {
                px = img.px;
            } else {
                int inexact = 0;
                px = MapToPalette(img, pal, d.pals[p11].ncolors, &inexact);
                if (inexact > 0)
                    log(img.path + ": " + std::to_string(inexact) + " colours are not in palette 1,1 and took the nearest");
            }
            s.pal = p11;
            s.depth = DepthFor(d.pals[p11].ncolors);
        } else {
            std::vector<unsigned char> pal;
            std::map<unsigned, int> colors;
            if (img.indexed) {
                px = img.px;
                pal = img.pal;
            } else {
                CountColors(img, colors);
            }
            if (!img.indexed && colors.size() > 255) {
                // True colour: a PNG32 sprite
                std::vector<unsigned char> png;
                if (lodepng::encode(png, img.px, (unsigned)img.w, (unsigned)img.h) != 0) continue;
                s.format = 12;
                s.depth = 32;
                s.data = std::make_shared<std::string>(kPrefix, '\0');
                Put32(*s.data, 0, (unsigned)img.w * img.h * 4);
                s.data->append((const char *)png.data(), png.size());
                s.pal = -1;
                out.push_back(s);
                continue;
            }
            if (!img.indexed) {
                pal = MakePalette(colors);
                int inexact = 0;
                px = MapToPalette(img, pal, 256, &inexact);
            }
            int number = 0;
            while (Sff_FindPal(d, 0, number) >= 0) number++;
            SffPal p;
            p.number = number;
            p.rgba = pal;
            d.pals.push_back(p);
            s.pal = (int)d.pals.size() - 1;
        }
        s.data = Rle8Encode(px);
        out.push_back(s);
    }
    return out;
}

std::vector<SffSprite> Sff_SpritesOfPngs(SffDoc &d, const std::vector<std::string> &files, int palMode,
                                         const std::function<void(const std::string &)> &log) {
    std::vector<Image> images;
    for (const std::string &f : files) {
        Image img;
        std::string error;
        if (LoadPng(f, img, &error))
            images.push_back(img);
        else
            log(error);
    }
    return SpritesOfImages(d, images, palMode, log);
}

std::vector<SffSprite> Sff_SpritesOfPictures(SffDoc &d, const std::vector<SffPicture> &pictures, int palMode,
                                             const std::function<void(const std::string &)> &log) {
    std::vector<Image> images;
    for (const SffPicture &p : pictures) {
        Image img;
        img.path = "A picture";
        img.w = p.w, img.h = p.h;
        img.px = p.rgba;
        images.push_back(std::move(img));
    }
    return SpritesOfImages(d, images, palMode, log);
}

void Sff_SetImageReader(SffImageReader read) { g_imageReader = read; }

std::vector<unsigned char> Sff_MakePalette(const std::map<unsigned, int> &colors) { return MakePalette(colors); }

void Sff_SetPixels(SffSprite &s, int w, int h, const std::vector<unsigned char> &px, int bpp) {
    s.w = w, s.h = h;
    if (bpp == 1) {
        s.format = 2, s.depth = 8;
        s.data = Rle8Encode(px);
        return;
    }
    std::vector<unsigned char> png;
    if (lodepng::encode(png, px, (unsigned)w, (unsigned)h) != 0) return;
    s.format = 12, s.depth = 32;
    s.data = std::make_shared<std::string>(kPrefix, '\0');
    Put32(*s.data, 0, (unsigned)w * h * 4);
    s.data->append((const char *)png.data(), png.size());
}

void Sff_SetAxis(SffSprite &s, int mode) {
    s.x = mode == SFF_AXIS_TOP_LEFT ? 0 : s.w / 2;
    s.y = mode == SFF_AXIS_FEET ? s.h : mode == SFF_AXIS_CENTRE ? s.h / 2 : 0;
}

bool Sff_DecodePcx(const std::string &data, int *w, int *h, std::vector<unsigned char> &px, unsigned char *pal) {
    bool hasPal = false;
    return DecodePcx((const unsigned char *)data.data(), data.size(), w, h, px, pal, &hasPal);
}
