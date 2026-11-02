#include "parallax.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "air.h"
#include "ini.h"
#include "sff.h"

namespace {

// "a, b" over what is there: a number left out keeps its default, as the engine reads it
void Numbers(const IniSection &s, const char *key, float *out, int count) {
    const IniField *f = s.Field(key);
    if (!f) return;
    const char *p = f->value.c_str();
    for (int i = 0; i < count; i++) {
        char *end;
        float v = strtof(p, &end);
        if (end != p) out[i] = v;
        p = strchr(p, ',');
        if (!p) return;
        p++;
    }
}

const IniSection *Named(const IniFile &ini, const char *name) {
    for (const IniSection &s : ini.sections)
        if (s.lower == name) return &s; // Ikemen takes the first
    return nullptr;
}

// How a BG's picture grows with the camera zoomed: all of it at delta 1, none at 0
float Zoomed(float scale, float zoomDelta, float delta) {
    float d = zoomDelta >= 0 ? zoomDelta : std::max(0.0f, delta);
    return std::max(0.0f, scale + (1 - scale) * (1 - d));
}

bool Blended(const IniSection &s) {
    const IniField *f = s.Field("trans");
    if (!f) return false;
    std::string v = f->value;
    for (char &c : v) c = (char)tolower((unsigned char)c);
    return v != "none" && v != "default" && !v.empty();
}

int Round(float v) { return (int)std::lround(v); }

// Where the empty part of a grid is, or "" when it is not a strip along one side
std::string StripOf(const std::vector<char> &grid, int cols, int rows, const ParallaxView &v) {
    int x0 = cols, x1 = -1, y0 = rows, y1 = -1;
    for (int y = 0; y < rows; y++)
        for (int x = 0; x < cols; x++)
            if (grid[(size_t)y * cols + x]) x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
    if (x1 < 0) return "";
    bool tall = y1 - y0 + 1 >= rows / 2, wide = x1 - x0 + 1 >= cols / 2;
    int across = Round((x1 - x0 + 1) * v.width / cols), down = Round((y1 - y0 + 1) * v.height / rows);
    if (tall && !wide && x0 == 0) return "the left " + std::to_string(across) + " units of the screen";
    if (tall && !wide && x1 == cols - 1) return "the right " + std::to_string(across) + " units of the screen";
    if (wide && !tall && y0 == 0) return "the top " + std::to_string(down) + " units of the screen";
    if (wide && !tall && y1 == rows - 1) return "the bottom " + std::to_string(down) + " units of the screen";
    return "";
}

} // namespace

ParallaxView Parallax_ViewOf(const IniFile &ini) {
    ParallaxView v;
    if (const IniSection *s = Named(ini, "stageinfo")) {
        float local[2] = {v.width, v.height};
        Numbers(*s, "localcoord", local, 2);
        if (local[0] > 0 && local[1] > 0) v.width = local[0], v.height = local[1];
        v.zoffset = v.height * 200 / 240;
        Numbers(*s, "zoffset", &v.zoffset, 1);
    }
    float zoomout = 1;
    if (const IniSection *c = Named(ini, "camera")) {
        Numbers(*c, "boundleft", &v.left, 1);
        Numbers(*c, "boundright", &v.right, 1);
        Numbers(*c, "boundhigh", &v.high, 1);
        Numbers(*c, "boundlow", &v.low, 1);
        Numbers(*c, "zoomout", &zoomout, 1);
    }
    if (zoomout < 1) v.minScale = std::max(zoomout, v.width / (v.width + std::max(0.0f, v.right - v.left)));
    return v;
}

std::vector<ParallaxBg> Parallax_Bgs(const IniFile &ini, const AirFile &air, const SffDoc &sff) {
    std::vector<ParallaxBg> out;
    bool hires = false;
    if (const IniSection *s = Named(ini, "stageinfo")) {
        float h = 0;
        Numbers(*s, "hires", &h, 1);
        hires = h != 0;
    }
    float linkStart[2] = {0, 0}, linkDelta[2] = {1, 1};
    std::vector<int> sections = ini.StageBgs();
    for (int n = 0; n < (int)sections.size(); n++) {
        const IniSection &s = ini.sections[sections[n]];
        const IniField *typeField = s.Field("type");
        char type = typeField && !typeField->value.empty() ? (char)tolower((unsigned char)typeField->value[0]) : 0;
        float start[2] = {0, 0}, delta[2] = {1, 1}, linked = 0;
        Numbers(s, "start", start, 2);
        Numbers(s, "delta", delta, 2);
        Numbers(s, "positionlink", &linked, 1);
        if (linked != 0 && n > 0) {
            start[0] += linkStart[0], start[1] += linkStart[1];
            delta[0] = linkDelta[0], delta[1] = linkDelta[1];
        } else {
            linkStart[0] = start[0], linkStart[1] = start[1];
            linkDelta[0] = delta[0], linkDelta[1] = delta[1];
        }
        if (!strchr("nap", type) || !type) continue;
        // The sprite it shows: an action's first, as a BG starts
        int group = -1, number = -1;
        const IniField *spr = s.Field("spriteno"), *act = s.Field("actionno");
        if (act && (type != 'n' || !spr || spr->value.empty())) {
            auto it = air.actions.find(atoi(act->value.c_str()));
            if (it != air.actions.end() && !it->second.elems.empty()) group = it->second.elems[0].group, number = it->second.elems[0].image;
        } else if (spr) {
            float gn[2] = {-1, -1};
            Numbers(s, "spriteno", gn, 2);
            group = (int)gn[0], number = (int)gn[1];
        }
        int found = group >= 0 ? Sff_FindSprite(sff, group, number) : -1;
        if (found < 0) continue;
        const SffSprite &image = sff.sprites[found];

        ParallaxBg b;
        b.index = n, b.line = s.line, b.name = s.name, b.linked = linked != 0, b.sprite = found;
        float scale[2] = {1, 1}, width[2] = {0, 0};
        Numbers(s, "scalestart", scale, 2);
        b.delta[0] = delta[0], b.delta[1] = delta[1];
        Numbers(s, "zoomdelta", b.zoomDelta, 2);
        if (s.Field("zoomdelta") && b.zoomDelta[1] < 0) b.zoomDelta[1] = b.zoomDelta[0];
        float tile[2] = {0, 0}, spacing[2] = {0, 0};
        Numbers(s, "tile", tile, 2);
        if (type != 'p') Numbers(s, "tilespacing", spacing, 2);
        b.tile[0] = tile[0] < 0 ? INT32_MAX : (int)tile[0], b.tile[1] = type == 'p' ? 0 : (int)tile[1];
        b.w = image.w * std::fabs(scale[0]), b.h = image.h * std::fabs(scale[1]);
        b.left = start[0] - image.x * std::fabs(scale[0]), b.top = start[1] - image.y * std::fabs(scale[1]);
        if (type == 'p') {
            float yscalestart = 100;
            Numbers(s, "yscalestart", &yscalestart, 1);
            if (yscalestart != 0) b.h *= std::fabs(100 / yscalestart);
            Numbers(s, "width", width, 2);
            if (width[0] > 0 && width[1] != 0) {
                b.w = width[0] * std::fabs(scale[0]);
                b.widen = width[1] / width[0];
                b.left = start[0] - b.w / 2; // the engine centres it (xofs)
            }
        }
        b.step[0] = b.w + spacing[0], b.step[1] = b.h + spacing[1];
        b.solid = !Blended(s) && !s.Field("window") && !s.Field("maskwindow") && !s.Field("angle") && !s.Field("xangle") &&
                  !s.Field("yangle") && !s.Field("projection");
        if (hires) {
            b.left *= 0.5f, b.top *= 0.5f, b.w *= 0.5f, b.h *= 0.5f, b.step[0] *= 0.5f, b.step[1] *= 0.5f;
            b.delta[0] *= 0.5f, b.delta[1] *= 0.5f, b.units = 0.5f;
        }
        out.push_back(b);
    }
    return out;
}

std::vector<ParallaxStop> Parallax_Stops(const ParallaxView &v) {
    std::vector<ParallaxStop> out = {{"the camera's start", 0, 0, 1}};
    float half = v.width / 2;
    auto add = [&](float x, float y, float scale, const std::string &name) {
        // Zoomed out, it stops short of the bounds: the edge of the view stays where it was
        float lo = v.left - half + half / scale, hi = v.right + half - half / scale;
        out.push_back({name, std::min(std::max(x, lo), hi), y, scale});
    };
    add(v.left, 0, 1, "the camera's left limit");
    add(v.right, 0, 1, "the camera's right limit");
    if (v.high < 0) {
        add(v.left, v.high, 1, "the camera's left limit, up high");
        add(v.right, v.high, 1, "the camera's right limit, up high");
    }
    if (v.minScale < 1) {
        add(v.left, 0, v.minScale, "the camera's left limit, zoomed out");
        add(v.right, 0, v.minScale, "the camera's right limit, zoomed out");
    }
    return out;
}

bool Parallax_Covers(const ParallaxBg &b, const ParallaxView &v, const ParallaxStop &at, float x, float y) {
    float zx = Zoomed(at.scale, b.zoomDelta[0], b.delta[0]), zy = Zoomed(at.scale, b.zoomDelta[1], b.delta[1]);
    float top = zy * (b.top - at.y * b.delta[1]) + v.zoffset * (1 - zy) - v.lift;
    float height = b.h * zy;
    if (height <= 0) return false;
    float t = 0;
    if (b.tile[1] != 1) {
        float last = height + (std::max(b.tile[1], 1) - 1) * b.step[1] * zy;
        if (y < top || y >= top + last) return false;
        t = std::min(1.0f, (y - top) / height);
    }
    if (b.tile[0] == 1) return true;
    float widen = 1 + (b.widen - 1) * t; // a floor scrolls and widens more further down
    float left = (b.left - at.x * b.delta[0]) * zx * widen;
    float last = b.w + (std::max(b.tile[0], 1) - 1.0f) * b.step[0];
    return x >= left && x < left + last * zx * widen;
}

std::vector<char> Parallax_Empty(const std::vector<ParallaxBg> &bgs, const ParallaxView &v, const ParallaxStop &at, int cols, int rows) {
    std::vector<char> grid((size_t)cols * rows, 1);
    for (int y = 0; y < rows; y++)
        for (int x = 0; x < cols; x++) {
            float sx = (x + 0.5f) * v.width / cols - v.width / 2, sy = (y + 0.5f) * v.height / rows;
            for (const ParallaxBg &b : bgs)
                if (b.solid && Parallax_Covers(b, v, at, sx, sy)) {
                    grid[(size_t)y * cols + x] = 0;
                    break;
                }
        }
    return grid;
}

float Parallax_FitDelta(const ParallaxBg &b, const ParallaxView &v) {
    float half = v.width / 2, right = b.left + b.w;
    if (b.left > -half || right < half) return -1; // does not fill the screen with the camera at 0
    float fit = 1e9f;
    if (v.right > 0) fit = std::min(fit, (right - half) / v.right);
    if (v.left < 0) fit = std::min(fit, (b.left + half) / v.left);
    return fit == 1e9f ? -1 : fit;
}

float Parallax_WidthFor(const ParallaxView &v, float delta) { return v.width + std::max(0.0f, v.right - v.left) * std::max(0.0f, delta); }

ParallaxShape Parallax_Shape(const ParallaxBg &b, const ParallaxView &v, const ParallaxStop &at) {
    float zx = Zoomed(at.scale, b.zoomDelta[0], b.delta[0]), zy = Zoomed(at.scale, b.zoomDelta[1], b.delta[1]);
    ParallaxShape s;
    s.top = zy * (b.top - at.y * b.delta[1]) + v.zoffset * (1 - zy) - v.lift;
    s.bottom = s.top + b.h * zy;
    float left = (b.left - at.x * b.delta[0]) * zx, width = b.w * zx;
    s.topLeft = left, s.topRight = left + width;
    s.bottomLeft = left * b.widen, s.bottomRight = (left + width) * b.widen;
    return s;
}

float Parallax_FloorDelta(const ParallaxBg &b, const ParallaxView &v) {
    if (b.h <= 0) return -1;
    float t = (v.zoffset - b.top) / b.h;
    if (t < 0 || t > 1) return -1;
    return 1 / (1 + (b.widen - 1) * t);
}

std::vector<StageFinding> Parallax_Check(const IniFile &ini, const std::vector<ParallaxBg> &bgs, const ParallaxView &v) {
    std::vector<StageFinding> out;
    const int cols = 64, rows = 36;
    const float cell = 100.0f / (cols * rows);
    std::vector<ParallaxStop> stops = Parallax_Stops(v);
    const IniSection *camera = Named(ini, "camera");
    int line = camera ? camera->line : 0;
    auto count = [](const std::vector<char> &g) { return (int)std::count(g.begin(), g.end(), 1); };
    int rest = count(Parallax_Empty(bgs, v, stops[0], cols, rows));
    for (size_t i = 1; i < stops.size(); i++) {
        std::vector<char> grid = Parallax_Empty(bgs, v, stops[i], cols, rows);
        int empty = count(grid);
        if (empty <= rest + 8) continue; // about a third of a percent: the grid's own edges
        std::string strip = StripOf(grid, cols, rows, v);
        if (strip.empty()) strip = std::to_string(Round((empty - rest) * cell)) + "% more of the screen";
        out.push_back({StageFinding::Warning, line, "At " + stops[i].name + ", " + strip + " show no BG"});
    }
    // A BG that fills the screen where it starts is meant to, wherever the camera is
    float half = v.width / 2;
    for (const ParallaxBg &b : bgs) {
        if (!b.solid || b.tile[0] || b.left > -half || b.left + b.w < half) continue;
        float worst = 0;
        std::string where;
        for (size_t i = 1; i < stops.size(); i++) {
            const ParallaxStop &at = stops[i];
            float zx = Zoomed(at.scale, b.zoomDelta[0], b.delta[0]);
            float left = (b.left - at.x * b.delta[0]) * zx, right = left + b.w * zx;
            float gap = std::max(left + half, half - right);
            if (gap > worst) worst = gap, where = at.name;
        }
        if (worst < 1) continue;
        float fit = Parallax_FitDelta(b, v);
        std::string text = "[" + b.name + "] ends " + std::to_string(Round(worst)) + " units before the screen's edge at " + where;
        if (fit >= 0 && fit < b.delta[0]) {
            char number[32];
            snprintf(number, sizeof(number), "%.3g", fit);
            text += std::string("; delta ") + number + " would reach";
        }
        out.push_back({StageFinding::Note, b.line, text});
    }
    return out;
}
