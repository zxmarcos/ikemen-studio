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
        float layer = 0;
        Numbers(s, "layerno", &layer, 1);
        b.layer = (int)layer;
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
                b.rate = b.widen = width[1] / width[0];
                b.widths = true;
                b.left = start[0] - b.w / 2; // the engine centres it (xofs)
            } else if (!s.Field("width")) {
                // xscale slants the picture by its position (rasterx), so its rows scroll as a width's do
                float raster[2] = {1, 1};
                Numbers(s, "xscale", raster, 2);
                if (raster[0] != 0) b.rate = raster[1] / raster[0];
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

float Parallax_GroundRow(const ParallaxBg &b, const ParallaxView &v) {
    if (b.h <= 0) return -1;
    float t = (v.zoffset - b.top) / b.h;
    return t < 0 || t > 1 ? -1 : t;
}

// A row of a floor is at its top's place times 1 + (rate - 1) * t (render.go's convergence, or the shear)
float Parallax_FloorDelta(const ParallaxBg &b, const ParallaxView &v) {
    float t = Parallax_GroundRow(b, v);
    return t < 0 ? -1 : 1 / (1 + (b.rate - 1) * t);
}

float Parallax_GroundDelta(const ParallaxBg &b, const ParallaxView &v) {
    float t = Parallax_GroundRow(b, v);
    return t < 0 ? -1 : b.delta[0] * (1 + (b.rate - 1) * t); // as drawn: a hires def's delta is halved
}

float Parallax_RowSpeed(const ParallaxBg &b, float y) {
    if (b.h <= 0) return b.delta[0];
    float t = std::min(1.0f, std::max(0.0f, (y - b.top) / b.h));
    return b.delta[0] * (1 + (b.rate - 1) * t);
}

namespace {

// Drawn first: layer 0 before layer 1, each in the order of the def
bool Before(const std::vector<ParallaxBg> &bgs, int i, int j) {
    return bgs[i].layer < bgs[j].layer || (bgs[i].layer == bgs[j].layer && i < j);
}

// As wide as the screen with no gap: a plane, not pillars
bool Wide(const ParallaxBg &b, const ParallaxView &v) {
    float half = v.width / 2;
    if (b.tile[0] && b.step[0] > b.w + 0.5f) return false;
    return b.tile[0] == 1 || (b.left <= -half && b.left + b.w + (std::max(b.tile[0], 1) - 1) * b.step[0] >= half);
}

// Whether a BG draws on the row at y, below its top
bool OnRow(const ParallaxBg &b, float y) {
    if (b.tile[1] == 1) return true;
    float last = b.top + b.h + (std::max(b.tile[1], 1) - 1) * b.step[1];
    return y > b.top + 0.5f && y <= last + 0.5f;
}

float Brightness(const unsigned char *p) { return 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]; }

// A picture's rows as how far each pixel is from its row's mean brightness: its lines stand out, -1
// where it shows nothing
struct Lines {
    int w = 0, h = 0;
    std::vector<float> v;
};

Lines LinesOf(const ParallaxPicture &p) {
    Lines l;
    if (!p.rgba || p.w <= 0 || p.h <= 0) return l;
    l.w = p.w, l.h = p.h;
    l.v.assign((size_t)p.w * p.h, -1);
    for (int y = 0; y < p.h; y++) {
        const unsigned char *row = p.rgba + (size_t)y * p.w * 4;
        float sum = 0;
        int seen = 0;
        for (int x = 0; x < p.w; x++)
            if (row[x * 4 + 3] >= 128) sum += Brightness(row + x * 4), seen++;
        if (!seen) continue;
        for (int x = 0; x < p.w; x++)
            if (row[x * 4 + 3] >= 128) l.v[(size_t)y * p.w + x] = std::fabs(Brightness(row + x * 4) - sum / seen);
    }
    return l;
}

// Where in its copy down a BG's row at y is, -1 off it
float InCopy(const ParallaxBg &b, float y) {
    float ry = y - b.top;
    if (b.tile[1] && b.step[1] > 0) {
        float copy = std::floor(ry / b.step[1]);
        if (b.tile[1] == 1 || (copy >= 0 && copy < b.tile[1])) ry -= copy * b.step[1];
    }
    return ry < 0 || ry >= b.h ? -1 : ry;
}

// What a BG shows of its row at pick, placed as its row at y is, with the camera at its start: at
// points step apart from x0, added to out. Moved across by shift, its top w wide (a width floor stays
// centred on its start). A floor's lines spread from its top edge: the rows by it, placed as the
// edge is, show where they start.
void Sample(const ParallaxBg &b, const Lines &l, float w, float shift, float y, float pick, float x0, float step, std::vector<float> &out) {
    if (b.h <= 0 || w <= 0 || l.w <= 0) return;
    float ry = InCopy(b, pick), at = InCopy(b, y);
    if (ry < 0) return;
    if (at < 0) at = std::min(std::max(y - b.top, 0.0f), b.h);
    int row = std::min(l.h - 1, (int)(ry / b.h * l.h));
    // A floor's row is its top's place times f (render.go's convergence, or the slant), its copies' width times g
    float t = at / b.h, f = 1 + (b.rate - 1) * t, g = 1 + (b.widen - 1) * t;
    float left = b.left + (b.w - w) / 2 + shift, across = b.step[0] - b.w + w;
    if (across <= 0) return;
    const float *pixels = &l.v[(size_t)row * l.w];
    for (size_t i = 0; i < out.size(); i++) {
        float x = x0 + i * step, nearest = std::floor((x / f - left) / across);
        for (float k = nearest - 1; k <= nearest + 1; k++) {
            if (b.tile[0] != 1 && (k < 0 || k >= std::max(b.tile[0], 1))) continue;
            float u = (x - (left + k * across) * f) / (w * g);
            if (u < 0 || u >= 1) continue;
            float p = pixels[std::min(l.w - 1, (int)(u * l.w))];
            if (p > 0) out[i] += p;
            break;
        }
    }
}

// Three rows of a BG along y, above it (way -1) or below, smoothed: a line a pixel wide meets one
// beside it
std::vector<float> Band(const ParallaxBg &b, const Lines &l, float w, float shift, float y, int way, float x0, float step, int n) {
    std::vector<float> rows(n, 0), out(n, 0);
    for (int r = 0; r < 3; r++) Sample(b, l, w, shift, y, y + way * (r + 0.5f), x0, step, rows);
    for (int i = 0; i < n; i++) {
        int from = std::max(0, i - 1), to = std::min(n - 1, i + 1);
        for (int j = from; j <= to; j++) out[i] += rows[j];
        out[i] /= to - from + 1;
    }
    return out;
}

bool Flat(const std::vector<float> &a) {
    double mean = 0, var = 0;
    for (float x : a) mean += x;
    mean /= std::max<size_t>(a.size(), 1);
    for (float x : a) var += (x - mean) * (x - mean);
    return var / std::max<size_t>(a.size(), 1) < 0.25;
}

// How alike two runs are, -1 to 1, by how they change about their means; 0 when either is flat
float Alike(const float *a, const float *b, int n) {
    double ma = 0, mb = 0;
    for (int i = 0; i < n; i++) ma += a[i], mb += b[i];
    ma /= n, mb /= n;
    double ab = 0, aa = 0, bb = 0;
    for (int i = 0; i < n; i++) {
        double da = a[i] - ma, db = b[i] - mb;
        ab += da * db, aa += da * da, bb += db * db;
    }
    return aa < 0.25 * n || bb < 0.25 * n ? 0 : (float)(ab / std::sqrt(aa * bb));
}

} // namespace

std::vector<ParallaxMeeting> Parallax_Meetings(const std::vector<ParallaxBg> &bgs, int at, const ParallaxView &v) {
    std::vector<ParallaxMeeting> out;
    int n = (int)bgs.size();
    if (at < 0 || at >= n || !Wide(bgs[at], v)) return out;
    // Its top on the plane drawn last before it
    int found = -1;
    for (int i = 0; i < n; i++)
        if (i != at && Before(bgs, i, at) && Wide(bgs[i], v) && OnRow(bgs[i], bgs[at].top) && (found < 0 || Before(bgs, found, i))) found = i;
    if (found >= 0) out.push_back({found, at});
    // The top of the first plane drawn after it, on it
    found = -1;
    for (int i = 0; i < n; i++)
        if (i != at && Before(bgs, at, i) && Wide(bgs[i], v) && OnRow(bgs[at], bgs[i].top) && (found < 0 || Before(bgs, i, found))) found = i;
    if (found >= 0) out.push_back({at, found});
    return out;
}

ParallaxLineUp Parallax_LineUp(const ParallaxBg &up, const ParallaxPicture &upPic, const ParallaxBg &low, const ParallaxPicture &lowPic, const ParallaxView &v) {
    ParallaxLineUp out;
    float y = low.top;
    // The fighters' floor stays as it is
    out.upperFollows = Parallax_GroundRow(low, v) >= 0 || Parallax_GroundRow(up, v) < 0;
    const ParallaxBg &lead = out.upperFollows ? low : up, &follow = out.upperFollows ? up : low;
    int fi = out.upperFollows ? 0 : 1;
    out.slide = std::fabs(Parallax_RowSpeed(up, y) - Parallax_RowSpeed(low, y)) * std::max(0.0f, v.right - v.left);

    // The lines, over all the camera shows of the join once it scrolls as the lead does
    int width = (int)std::lround(low.w / low.units), top = width;
    float r = 0;
    Lines lu = LinesOf(upPic), ll = LinesOf(lowPic);
    float speed = std::max(0.0f, Parallax_RowSpeed(lead, y));
    float lo = -v.width / 2 + std::min(0.0f, v.left) * speed, hi = std::min(v.width / 2 + std::max(0.0f, v.right) * speed, lo + 4000);
    // Roughly first, every half unit: the lower one against the upper one moved along by up to half
    // of what repeats
    const float coarse = 0.5f;
    int n = (int)((hi - lo) / coarse) + 1;
    bool sized = low.widths && low.tile[0] != 0;
    int fewest = sized ? std::max(2, width / 2) : width, most = sized ? width * 2 : width;
    float spacing = low.step[0] - low.w;
    float reach = std::min(512.0f, std::max(up.step[0], spacing + most * low.units));
    int m = (int)(reach / coarse) + 1;
    std::vector<float> upper = Band(up, lu, up.w, 0, y, -1, lo - m * coarse, coarse, n + 2 * m);
    // A plain colour on either side: nothing shows the join move
    if (lu.w <= 0 || ll.w <= 0 || Flat(upper) || Flat(Band(low, ll, low.w, 0, y, 1, lo, coarse, n))) {
        out.done = true;
        return out;
    }
    struct Try {
        float score = -2;
        int j = 0;
    };
    std::vector<Try> tries(most + 1);
    float best = -2;
    for (int w = fewest; w <= most; w++) {
        std::vector<float> lower = Band(low, ll, w * low.units, 0, y, 1, lo, coarse, n);
        float period = std::max(up.step[0], spacing + w * low.units);
        int half = std::min(m, (int)(period / 2 / coarse) + 1);
        Try &t = tries[w];
        for (int j = -half; j <= half; j++) {
            // lower(x) like upper(x + j): the lower one moved j along lines up
            float s = Alike(lower.data(), upper.data() + m + j, n);
            if (s > t.score + 1e-3f || (std::fabs(s - t.score) <= 1e-3f && std::abs(j) < std::abs(t.j))) t.score = s, t.j = j;
        }
        best = std::max(best, t.score);
    }
    out.match = std::max(0.0f, best);
    if (best >= 0.5f) {
        // Of those about as good, the width nearest the one it has
        top = -1;
        for (int w = fewest; w <= most; w++)
            if (tries[w].score >= best - 0.02f && (top < 0 || std::abs(w - width) < std::abs(top - width))) top = w;
        // Then to a twentieth of a unit, the one that follows moved
        const float fine = 0.1f;
        int n2 = (int)((hi - lo) / fine) + 1;
        float w0 = top * low.units, around = tries[top].j * coarse;
        std::vector<float> still = out.upperFollows ? Band(low, ll, w0, 0, y, 1, lo, fine, n2) : Band(up, lu, up.w, 0, y, -1, lo, fine, n2);
        out.match = -2;
        for (int i = -10; i <= 10; i++) {
            float at = around + i * 0.05f;
            std::vector<float> moved = out.upperFollows ? Band(up, lu, up.w, -at, y, -1, lo, fine, n2) : Band(low, ll, w0, at, y, 1, lo, fine, n2);
            float s = Alike(still.data(), moved.data(), n2);
            if (s > out.match) out.match = s, r = at;
        }
    }

    // The lower one's top, and its bottom as much wider as it was
    float lowDelta = low.delta[0];
    if (top != width) {
        out.width[0] = top, out.width[1] = std::max(1, (int)std::lround(top * low.widen));
        // A floor moving with the fighters keeps doing so
        float ground = Parallax_GroundRow(low, v);
        if (ground >= 0 && std::fabs(Parallax_GroundDelta(low, v) - 1) < 0.002f) {
            float rate = (float)out.width[1] / out.width[0];
            lowDelta = 1 / (1 + (rate - 1) * ground);
            if (std::fabs(lowDelta - low.delta[0]) > 0.0005f) out.delta[1][0] = lowDelta;
        }
    }
    // The speed of the lead's row along the join, for the one that follows: its row there
    float leadSpeed = out.upperFollows ? lowDelta : Parallax_RowSpeed(up, y);
    float t = follow.h > 0 ? std::min(1.0f, std::max(0.0f, (y - follow.top) / follow.h)) : 0;
    float wanted = leadSpeed / (1 + (follow.rate - 1) * t);
    if (std::fabs(wanted - follow.delta[0]) > 0.0005f) out.delta[fi][0] = wanted;
    if (v.high < 0 && std::fabs(lead.delta[1] - follow.delta[1]) > 0.0005f) out.delta[fi][1] = lead.delta[1];
    out.shift[fi] = out.upperFollows ? -r : r;
    out.done = top == width && std::fabs(r) < 0.25f && out.delta[0][0] < 0 && out.delta[0][1] < 0 && out.delta[1][0] < 0 &&
               out.delta[1][1] < 0;
    return out;
}

// Two pixels' difference, 0 to 255: a pixel against nothing is the most there is
static float PixelDiff(const unsigned char *a, const unsigned char *b) {
    bool seen = a[3] >= 128, other = b[3] >= 128;
    if (!seen && !other) return 0;
    if (seen != other) return 255;
    return (std::abs(a[0] - b[0]) + std::abs(a[1] - b[1]) + std::abs(a[2] - b[2])) / 3.0f;
}

bool Parallax_Seam(const unsigned char *rgba, int w, int h, int axis) {
    int across = axis == 0 ? w : h, along = axis == 0 ? h : w;
    if (across < 4 || along < 1) return false;
    auto at = [&](int i, int j) { return rgba + ((size_t)(axis == 0 ? j : i) * w + (axis == 0 ? i : j)) * 4; };
    float edge = 0;
    for (int j = 0; j < along; j++) edge += PixelDiff(at(across - 1, j), at(0, j));
    edge /= along;
    // What the picture itself changes by from one pixel to the next: enough places across it to tell
    float inside = 0;
    int measured = 0, step = std::max(1, across / 32);
    for (int i = 0; i + 1 < across; i += step)
        for (int j = 0; j < along; j++, measured++) inside += PixelDiff(at(i, j), at(i + 1, j));
    inside /= std::max(measured, 1);
    // A line drawn along an edge, as a grid has, stands out from the pixels beside it too: that is the
    // picture, not a join
    float first = 0, last = 0;
    for (int j = 0; j < along; j++) {
        first += PixelDiff(at(0, j), at(1, j));
        last += PixelDiff(at(across - 2, j), at(across - 1, j));
    }
    float beside = std::max(first, last) / along;
    return edge > 16 && edge > inside * 3 && edge > beside * 2;
}

std::vector<StageFinding> Parallax_Seams(const std::vector<ParallaxBg> &bgs, const SffDoc &sff) {
    std::vector<StageFinding> out;
    for (const ParallaxBg &b : bgs) {
        bool across = b.tile[0] != 0 && b.step[0] <= b.w, down = b.tile[1] != 0 && b.step[1] <= b.h;
        if ((!across && !down) || b.sprite < 0) continue;
        const SffSprite &s = sff.sprites[b.sprite];
        std::vector<unsigned char> px, rgba;
        int bpp = 0;
        if (!Sff_Decode(s, px, &bpp)) continue;
        Sff_ToRgba(sff, s, px, bpp, rgba);
        if (across && Parallax_Seam(rgba.data(), s.w, s.h, 0))
            out.push_back({StageFinding::Note, b.line, "[" + b.name + "] repeats across and its left and right edges do not match: the join shows"});
        if (down && Parallax_Seam(rgba.data(), s.w, s.h, 1))
            out.push_back({StageFinding::Note, b.line, "[" + b.name + "] repeats down and its top and bottom edges do not match: the join shows"});
    }
    return out;
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
