#include "palfile.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "sff.h"

namespace {

std::string Extension(const std::string &path) {
    size_t dot = path.find_last_of('.');
    std::string ext = dot == std::string::npos ? "" : path.substr(dot + 1);
    for (char &ch : ext) ch = (char)tolower((unsigned char)ch);
    return ext;
}

unsigned Be16(const std::string &f, size_t at) { return (unsigned char)f[at] << 8 | (unsigned char)f[at + 1]; }
unsigned Be32(const std::string &f, size_t at) { return Be16(f, at) << 16 | Be16(f, at + 2); }
unsigned Le16(const std::string &f, size_t at) { return (unsigned char)f[at] | (unsigned char)f[at + 1] << 8; }
unsigned Le32(const std::string &f, size_t at) { return Le16(f, at) | Le16(f, at + 2) << 16; }

// Colours read, in order: each sets the next one
struct Filler {
    std::vector<unsigned char> &rgba;
    int n = 0;
    void Add(int r, int g, int b) {
        if (n >= 256) return;
        rgba[n * 4] = (unsigned char)r, rgba[n * 4 + 1] = (unsigned char)g, rgba[n * 4 + 2] = (unsigned char)b;
        n++;
    }
};

bool ReadJasc(const std::string &f, Filler &out) {
    std::istringstream in(f);
    std::string line;
    std::getline(in, line); // JASC-PAL
    std::getline(in, line); // version
    std::getline(in, line); // count
    int r, g, b;
    while (std::getline(in, line))
        if (sscanf(line.c_str(), "%d %d %d", &r, &g, &b) == 3) out.Add(r, g, b);
    return out.n > 0;
}

bool ReadRiff(const std::string &f, Filler &out) {
    size_t at = 12;
    while (at + 8 <= f.size()) {
        unsigned size = Le32(f, at + 4);
        if (!f.compare(at, 4, "data") && at + 12 <= f.size()) {
            int count = (int)Le16(f, at + 10);
            for (int i = 0; i < count && at + 12 + i * 4 + 3 <= f.size(); i++) {
                const char *c = f.data() + at + 12 + i * 4;
                out.Add((unsigned char)c[0], (unsigned char)c[1], (unsigned char)c[2]);
            }
            return out.n > 0;
        }
        at += 8 + size + (size & 1);
    }
    return false;
}

bool ReadAco(const std::string &f, Filler &out) {
    if (f.size() < 4) return false;
    int count = (int)Be16(f, 2);
    for (int i = 0; i < count && 4 + (size_t)(i + 1) * 10 <= f.size(); i++) {
        size_t at = 4 + (size_t)i * 10;
        if (Be16(f, at) == 0) out.Add((int)(Be16(f, at + 2) >> 8), (int)(Be16(f, at + 4) >> 8), (int)(Be16(f, at + 6) >> 8));
    }
    return out.n > 0;
}

float BeFloat(const std::string &f, size_t at) {
    unsigned v = Be32(f, at);
    float x;
    memcpy(&x, &v, 4);
    return x;
}

bool ReadAse(const std::string &f, Filler &out) {
    if (f.size() < 12 || f.compare(0, 4, "ASEF") != 0) return false;
    size_t at = 12;
    while (at + 6 <= f.size()) {
        unsigned type = Be16(f, at), size = Be32(f, at + 2);
        size_t body = at + 6;
        if (type == 1 && body + 2 <= f.size()) {
            size_t model = body + 2 + (size_t)Be16(f, body) * 2; // past the name, UTF-16
            if (model + 16 <= f.size() && !f.compare(model, 3, "RGB")) {
                auto byte = [&](size_t a) { return (int)lroundf(BeFloat(f, a) * 255); };
                out.Add(byte(model + 4), byte(model + 8), byte(model + 12));
            }
        }
        at = body + size;
    }
    return out.n > 0;
}

bool ReadGpl(const std::string &f, Filler &out) {
    std::istringstream in(f);
    std::string line;
    int r, g, b;
    while (std::getline(in, line))
        if (sscanf(line.c_str(), " %d %d %d", &r, &g, &b) == 3) out.Add(r, g, b);
    return out.n > 0;
}

void PutBe16(std::string &o, unsigned v) {
    o += (char)(v >> 8);
    o += (char)v;
}

void Selected(const std::vector<char> &sel, std::vector<int> &out) {
    for (int i = 1; i < 256 && i < (int)sel.size(); i++)
        if (sel[i]) out.push_back(i);
}

// Hue 0..1, lightness 0..1, whether it has a colour at all
void Tone(const unsigned char *c, float *hue, float *light, bool *grey) {
    float r = c[0] / 255.0f, g = c[1] / 255.0f, b = c[2] / 255.0f;
    float hi = std::max(r, std::max(g, b)), lo = std::min(r, std::min(g, b)), d = hi - lo;
    *light = (hi + lo) / 2;
    *grey = d < 0.04f;
    float h = 0;
    if (d > 0) {
        if (hi == r) h = fmodf((g - b) / d + 6, 6);
        else if (hi == g) h = (b - r) / d + 2;
        else h = (r - g) / d + 4;
    }
    *hue = h / 6;
}

} // namespace

bool Pal_Read(const std::string &path, std::vector<unsigned char> &rgba, int *count, std::string *error) {
    std::string ext = Extension(path);
    if (ext == "act") {
        if (!Sff_ReadAct(path, rgba)) {
            *error = "Not an ACT palette: " + path;
            return false;
        }
        if (count) *count = 256;
        return true;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        *error = "Can't read " + path;
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    std::string f = ss.str();
    std::vector<unsigned char> out(1024, 0);
    Filler fill{out};
    bool ok = false;
    if (ext == "pal") ok = !f.compare(0, 4, "RIFF") ? ReadRiff(f, fill) : ReadJasc(f, fill);
    else if (ext == "aco") ok = ReadAco(f, fill);
    else if (ext == "ase") ok = ReadAse(f, fill);
    else if (ext == "gpl") ok = ReadGpl(f, fill);
    if (!ok) {
        *error = "Not a palette: " + path;
        return false;
    }
    for (int i = 0; i < 256; i++) out[i * 4 + 3] = i == 0 ? 0 : 255;
    rgba = out;
    if (count) *count = fill.n;
    return true;
}

bool Pal_Write(const std::string &path, const std::vector<unsigned char> &rgba, int count, std::string *error) {
    std::string ext = Extension(path), out;
    count = std::min(count, 256);
    if (ext == "act") {
        out = Sff_ActBytes(rgba);
    } else if (ext == "pal") {
        std::ostringstream o;
        o << "JASC-PAL\r\n0100\r\n" << count << "\r\n";
        for (int i = 0; i < count; i++) o << (int)rgba[i * 4] << ' ' << (int)rgba[i * 4 + 1] << ' ' << (int)rgba[i * 4 + 2] << "\r\n";
        out = o.str();
    } else if (ext == "gpl") {
        std::ostringstream o;
        o << "GIMP Palette\nName: Ikemen\nColumns: 16\n#\n";
        for (int i = 0; i < count; i++) o << (int)rgba[i * 4] << ' ' << (int)rgba[i * 4 + 1] << ' ' << (int)rgba[i * 4 + 2] << " colour " << i << "\n";
        out = o.str();
    } else if (ext == "aco") {
        PutBe16(out, 1);
        PutBe16(out, (unsigned)count);
        for (int i = 0; i < count; i++) {
            PutBe16(out, 0);
            for (int k = 0; k < 3; k++) PutBe16(out, (unsigned)rgba[i * 4 + k] * 257);
            PutBe16(out, 0);
        }
    } else {
        *error = "Palettes are not written as ." + ext;
        return false;
    }
    std::ofstream file(path, std::ios::binary);
    file << out;
    file.close();
    if (!file) {
        *error = "Could not write " + path;
        return false;
    }
    return true;
}

void Pal_Move(std::vector<unsigned char> &rgba, int from, int to) {
    if (from < 1 || to < 1 || from > 255 || to > 255 || from == to) return;
    unsigned char c[4];
    memcpy(c, &rgba[from * 4], 4);
    int dir = to > from ? 1 : -1;
    for (int i = from; i != to; i += dir) memcpy(&rgba[i * 4], &rgba[(i + dir) * 4], 4);
    memcpy(&rgba[to * 4], c, 4);
}

void Pal_Reverse(std::vector<unsigned char> &rgba, const std::vector<char> &sel) {
    std::vector<int> at;
    Selected(sel, at);
    for (size_t a = 0, b = at.size(); a + 1 < b; a++, b--)
        for (int k = 0; k < 4; k++) std::swap(rgba[at[a] * 4 + k], rgba[at[b - 1] * 4 + k]);
}

void Pal_SortByTone(std::vector<unsigned char> &rgba, const std::vector<char> &sel) {
    std::vector<int> at;
    Selected(sel, at);
    struct Item {
        float hue, light;
        bool grey;
        unsigned char c[4];
    };
    std::vector<Item> items;
    for (int i : at) {
        Item it;
        Tone(&rgba[i * 4], &it.hue, &it.light, &it.grey);
        memcpy(it.c, &rgba[i * 4], 4);
        items.push_back(it);
    }
    std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        if (a.grey != b.grey) return a.grey;
        if (!a.grey && fabsf(a.hue - b.hue) > 0.02f) return a.hue < b.hue;
        return a.light < b.light;
    });
    for (size_t k = 0; k < at.size(); k++) memcpy(&rgba[at[k] * 4], items[k].c, 4);
}

void Pal_Fill(std::vector<unsigned char> &rgba, const std::vector<char> &sel, const unsigned char *colour) {
    std::vector<int> at;
    Selected(sel, at);
    for (int i : at) memcpy(&rgba[i * 4], colour, 3);
}

const char *Pal_EffectName(int effect) {
    static const char *names[] = {"Invert", "Grey", "Red and blue swapped", "Red and green swapped", "Green and blue swapped", "Red to green to blue"};
    return effect >= 0 && effect < PAL_EFFECTS ? names[effect] : "";
}

void Pal_Effect(std::vector<unsigned char> &rgba, const std::vector<char> &sel, int effect) {
    std::vector<int> at;
    Selected(sel, at);
    for (int i : at) {
        unsigned char *c = &rgba[i * 4], r = c[0], g = c[1], b = c[2];
        switch (effect) {
        case PAL_INVERT:
            c[0] = 255 - r, c[1] = 255 - g, c[2] = 255 - b;
            break;
        case PAL_GREY:
            c[0] = c[1] = c[2] = (unsigned char)((r * 77 + g * 151 + b * 28) >> 8);
            break;
        case PAL_SWAP_RB:
            c[0] = b, c[2] = r;
            break;
        case PAL_SWAP_RG:
            c[0] = g, c[1] = r;
            break;
        case PAL_SWAP_GB:
            c[1] = b, c[2] = g;
            break;
        case PAL_ROTATE:
            c[0] = b, c[1] = r, c[2] = g;
            break;
        }
    }
}

bool PalFx::Plain() const {
    for (int k = 0; k < 3; k++)
        if (add[k] != 0 || mul[k] != 256 || (period > 1 && sinadd[k] != 0)) return false;
    return color == 1 && !invert;
}

void Pal_ApplyFx(std::vector<unsigned char> &rgba, const PalFx &fx, int tick) {
    float sine = fx.period > 1 ? sinf(2 * 3.14159265f * (float)tick / (float)fx.period) : 0;
    for (int i = 1; i < 256; i++) {
        unsigned char *c = &rgba[i * 4];
        float v[3] = {(float)c[0], (float)c[1], (float)c[2]};
        if (fx.invert)
            for (float &x : v) x = 255 - x;
        float grey = (v[0] + v[1] + v[2]) / 3;
        for (int k = 0; k < 3; k++) {
            float x = v[k] + (grey - v[k]) * (1 - fx.color);
            x = (x + fx.add[k] + sine * fx.sinadd[k]) * fx.mul[k] / 256;
            c[k] = (unsigned char)std::max(0.0f, std::min(255.0f, x));
        }
    }
}

std::string Pal_FxText(const PalFx &fx, int time) {
    char buf[256];
    std::string out = "[State , PalFX]\ntype = PalFX\ntrigger1 = 1\n";
    snprintf(buf, sizeof(buf), "time = %d\nadd = %d, %d, %d\nmul = %d, %d, %d\n", time, fx.add[0], fx.add[1], fx.add[2], fx.mul[0], fx.mul[1], fx.mul[2]);
    out += buf;
    if (fx.period > 1 && (fx.sinadd[0] || fx.sinadd[1] || fx.sinadd[2])) {
        snprintf(buf, sizeof(buf), "sinadd = %d, %d, %d, %d\n", fx.sinadd[0], fx.sinadd[1], fx.sinadd[2], fx.period);
        out += buf;
    }
    if (fx.color != 1) {
        snprintf(buf, sizeof(buf), "color = %d\n", (int)lroundf(fx.color * 256));
        out += buf;
    }
    if (fx.invert) out += "invertall = 1\n";
    return out;
}
