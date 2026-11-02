#include "story.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

bool IsScene(const IniSection &s) { return s.lower == "scene" || s.lower.compare(0, 6, "scene ") == 0; }

void Ints(const IniSection &s, const char *key, int *out, int count) {
    float v[8];
    const IniField *f = s.Field(key);
    if (!f) return;
    // Those written, the others left as they are
    int n = 0;
    const char *p = f->value.c_str();
    while (n < count && n < 8) {
        char *end;
        v[n] = strtof(p, &end);
        if (end != p) out[n] = (int)v[n];
        n++;
        p = strchr(end, ',');
        if (!p) break;
        p++;
    }
}

std::string Unquoted(std::string v) {
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
    std::string out;
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i] == '\\' && i + 1 < v.size() && v[i + 1] == 'n') out += '\n', i++;
        else out += v[i];
    }
    return out;
}

// "layer0.anim": its part ("layer0"), number and what follows ("anim"), for those named so
bool PartOf(const std::string &key, const char *name, std::string &part, int &number, std::string &rest) {
    size_t n = strlen(name), i = n;
    if (key.compare(0, n, name) != 0) return false;
    if (i < key.size() && key[i] == '_') i++;
    size_t digits = i;
    while (i < key.size() && isdigit((unsigned char)key[i])) i++;
    if (i == digits || i >= key.size() || key[i] != '.') return false;
    part = key.substr(0, i), number = atoi(key.c_str() + digits), rest = key.substr(i + 1);
    return true;
}

void Floats(const std::string &value, float *out, int count) {
    const char *p = value.c_str();
    for (int n = 0; n < count; n++) {
        char *end;
        float v = strtof(p, &end);
        if (end != p) out[n] = v;
        p = strchr(end, ',');
        if (!p) break;
        p++;
    }
}

template <class T> T &Named(std::vector<T> &list, const std::string &key) {
    for (T &t : list)
        if (t.key == key) return t;
    list.emplace_back();
    list.back().key = key;
    return list.back();
}

void ReadLayerField(StoryLayer &l, const std::string &rest, const std::string &value) {
    if (rest == "anim") l.anim = atoi(value.c_str());
    else if (rest == "spr") sscanf(value.c_str(), "%d , %d", &l.spr[0], &l.spr[1]);
    else if (rest == "offset") Floats(value, l.offset, 2);
    else if (rest == "scale") Floats(value, l.scale, 2);
    else if (rest == "velocity") Floats(value, l.velocity, 2);
    else if (rest == "accel") Floats(value, l.accel, 2);
    else if (rest == "friction") Floats(value, l.friction, 2);
    else if (rest == "angle") l.angle = (float)atof(value.c_str());
    else if (rest == "facing") l.facing = atoi(value.c_str());
    else if (rest == "layerno") l.layerno = atoi(value.c_str());
    else if (rest == "text") l.text = Unquoted(value);
    else if (rest == "textdelay") l.textDelay = (float)atof(value.c_str());
    else if (rest == "starttime") l.start = atoi(value.c_str());
    else if (rest == "endtime") l.end = atoi(value.c_str());
    else if (rest == "font") {
        float v[8];
        for (int i = 0; i < 8; i++) v[i] = (float)l.font[i];
        Floats(value, v, 8);
        for (int i = 0; i < 8; i++) l.font[i] = (int)v[i];
    }
}

// Where a line starts in the text, 1-based; the text's end past the last one
size_t LineStart(const std::string &text, int line) {
    size_t at = 0;
    for (int i = 1; i < line; i++) {
        at = text.find('\n', at);
        if (at == std::string::npos) return text.size();
        at++;
    }
    return at;
}

// A scene's lines, as text
void SceneBlock(const std::string &text, const IniSection &s, size_t &from, size_t &to) {
    from = LineStart(text, s.line);
    to = LineStart(text, s.last + 1);
}

std::string FreeName(const StoryDoc &doc) {
    int n = 0;
    for (const StoryScene &s : doc.scenes) {
        size_t space = s.name.find(' ');
        if (space != std::string::npos) n = std::max(n, atoi(s.name.c_str() + space + 1) + 1);
    }
    return "Scene " + std::to_string(n);
}

} // namespace

bool Story_Is(const IniFile &ini) {
    for (const IniSection &s : ini.sections)
        if (s.lower == "scenedef" || IsScene(s)) return true;
    return false;
}

void Story_Read(const IniFile &ini, StoryDoc &out) {
    out = StoryDoc();
    StoryScene carried; // what goes on to the next scene
    for (size_t i = 0; i < ini.sections.size(); i++) {
        const IniSection &s = ini.sections[i];
        if (s.lower == "info") {
            Ints(s, "localcoord", out.coord, 2);
        } else if (s.lower == "scenedef") {
            if (const IniField *f = s.Field("startscene")) out.startScene = atoi(f->value.c_str());
        } else if (IsScene(s)) {
            StoryScene sc;
            sc.section = (int)i;
            sc.name = s.name;
            memcpy(sc.clear, carried.clear, sizeof(sc.clear));
            sc.clearLayer = carried.clearLayer;
            sc.layerall[0] = carried.layerall[0], sc.layerall[1] = carried.layerall[1];
            for (const IniField &f : s.fields) {
                std::string part, rest;
                int number = 0;
                if (f.key == "end.time") sc.end = atoi(f.value.c_str());
                else if (f.key == "fadein.time") sc.in.time = atoi(f.value.c_str());
                else if (f.key == "fadeout.time") sc.out.time = atoi(f.value.c_str());
                else if (f.key == "fadein.col") Ints(s, "fadein.col", sc.in.col, 3);
                else if (f.key == "fadeout.col") Ints(s, "fadeout.col", sc.out.col, 3);
                else if (f.key == "clearcolor") Ints(s, "clearcolor", sc.clear, 3);
                else if (f.key == "clearlayerno") sc.clearLayer = atoi(f.value.c_str());
                else if (f.key == "layerall.pos") Floats(f.value, sc.layerall, 2);
                else if (f.key == "jump") sc.jump = atoi(f.value.c_str());
                else if (f.key == "bg.name") {
                    sc.bg = f.value;
                    std::transform(sc.bg.begin(), sc.bg.end(), sc.bg.begin(), [](unsigned char c) { return (char)tolower(c); });
                } else if (PartOf(f.key, "layer", part, number, rest)) {
                    StoryLayer &l = Named(sc.layers, part);
                    l.number = number;
                    ReadLayerField(l, rest, f.value);
                } else if (PartOf(f.key, "sound", part, number, rest)) {
                    StorySound &snd = Named(sc.sounds, part);
                    if (rest == "value") sscanf(f.value.c_str(), "%d , %d", &snd.value[0], &snd.value[1]);
                    if (rest == "starttime") snd.start = atoi(f.value.c_str());
                }
            }
            // SortedKeys: as text, so layer10 comes before layer2
            std::sort(sc.layers.begin(), sc.layers.end(), [](const StoryLayer &a, const StoryLayer &b) { return a.key < b.key; });
            std::sort(sc.sounds.begin(), sc.sounds.end(), [](const StorySound &a, const StorySound &b) { return a.key < b.key; });
            carried = sc;
            out.scenes.push_back(std::move(sc));
        }
    }
}

int Story_Length(const StoryScene &s) { return std::max(std::max(s.end, s.out.time), 1); }

int Story_Next(const StoryDoc &doc, int scene) {
    if (scene < 0 || scene >= (int)doc.scenes.size()) return -1;
    int jump = doc.scenes[scene].jump;
    int next = jump >= 0 && jump != scene ? jump : scene + 1;
    return next < (int)doc.scenes.size() ? next : -1;
}

std::vector<int> Story_Order(const StoryDoc &doc) {
    std::vector<int> order;
    std::vector<char> seen(doc.scenes.size());
    for (int i = doc.startScene; i >= 0 && i < (int)doc.scenes.size() && !seen[i]; i = Story_Next(doc, i)) {
        seen[i] = 1;
        order.push_back(i);
    }
    return order;
}

void Story_Motion(const StoryLayer &l, int ticks, float out[2]) {
    for (int a = 0; a < 2; a++) {
        // Anim.updateVel, one step per tick; long scenes of a layer at a steady speed need no loop
        float v = l.velocity[a];
        if (l.accel[a] == 0 && l.friction[a] == 1) {
            out[a] = v * ticks;
            continue;
        }
        float x = 0;
        for (int t = 0; t < ticks; t++) {
            x += v;
            v = v * l.friction[a] + l.accel[a];
            if (fabsf(v) < 0.1f && fabsf(l.friction[a]) < 1) v = 0;
        }
        out[a] = x;
    }
}

int Story_Typed(const StoryLayer &l, int counter, int chars) {
    if (l.textDelay <= 0) return chars;
    int typed = (int)((counter - l.start + 1) / l.textDelay);
    return std::min(std::max(typed, 0), chars);
}

float Story_FadeAlpha(const StoryScene &s, int counter, int col[3]) {
    int length = Story_Length(s);
    float alpha = 0;
    const StoryFade *f = nullptr;
    if (s.in.time > 0 && counter < s.in.time) {
        f = &s.in;
        alpha = 1 - (float)counter / s.in.time;
    }
    if (s.out.time > 0 && counter >= length - s.out.time) {
        float a = (float)(counter - (length - s.out.time)) / s.out.time;
        if (a > alpha) f = &s.out, alpha = a;
    }
    if (f) memcpy(col, f->col, sizeof(f->col));
    return std::min(alpha, 1.0f);
}

bool Story_AddScene(std::string &text, const IniFile &ini, const StoryDoc &doc, int after) {
    std::string block = "[" + FreeName(doc) + "]\nend.time = 300\n\n";
    size_t at = text.size();
    if (after >= 0 && after < (int)doc.scenes.size()) {
        at = LineStart(text, ini.sections[doc.scenes[after].section].last + 1);
        block = "\n" + block.substr(0, block.size() - 1);
    } else if (!doc.scenes.empty()) {
        at = LineStart(text, ini.sections[doc.scenes[0].section].line);
    } else if (!text.empty() && text.back() != '\n') {
        block = "\n" + block;
    }
    text.insert(at, block);
    return true;
}

bool Story_DuplicateScene(std::string &text, const IniFile &ini, const StoryDoc &doc, int scene) {
    if (scene < 0 || scene >= (int)doc.scenes.size()) return false;
    size_t from, to;
    const IniSection &s = ini.sections[doc.scenes[scene].section];
    SceneBlock(text, s, from, to);
    std::string block = text.substr(from, to - from);
    if (block.empty() || block.back() != '\n') block += '\n';
    block.replace(0, block.find('\n'), "[" + FreeName(doc) + "]");
    text.insert(to, (to > 0 && text[to - 1] != '\n' ? "\n\n" : "\n") + block);
    return true;
}

bool Story_DeleteScene(std::string &text, const IniFile &ini, const StoryDoc &doc, int scene) {
    if (scene < 0 || scene >= (int)doc.scenes.size()) return false;
    size_t from, to;
    SceneBlock(text, ini.sections[doc.scenes[scene].section], from, to);
    while (to < text.size() && text[to] == '\n') to++; // the blank lines after it
    text.erase(from, to - from);
    return true;
}

bool Story_MoveScene(std::string &text, const IniFile &ini, const StoryDoc &doc, int scene, int dir) {
    int other = scene + dir;
    if (scene < 0 || scene >= (int)doc.scenes.size() || other < 0 || other >= (int)doc.scenes.size()) return false;
    int first = std::min(scene, other), second = std::max(scene, other);
    size_t a0, a1, b0, b1;
    SceneBlock(text, ini.sections[doc.scenes[first].section], a0, a1);
    SceneBlock(text, ini.sections[doc.scenes[second].section], b0, b1);
    std::string a = text.substr(a0, a1 - a0), b = text.substr(b0, b1 - b0), between = text.substr(a1, b0 - a1);
    if (b.empty() || b.back() != '\n') b += '\n';
    if (a.empty() || a.back() != '\n') a += '\n';
    text = text.substr(0, a0) + b + between + a + text.substr(b1);
    return true;
}

std::string Story_NewLayerKey(const StoryScene &s) {
    int n = 0;
    for (const StoryLayer &l : s.layers) n = std::max(n, l.number + 1);
    return "layer" + std::to_string(n);
}

bool Story_AddText(std::string &text, const IniFile &ini, const StoryDoc &doc, int scene) {
    if (scene < 0 || scene >= (int)doc.scenes.size()) return false;
    std::string out = text;
    int font = -1;
    const IniSection *def = nullptr;
    for (const IniSection &s : ini.sections) {
        if (s.lower != "scenedef") continue;
        def = &s;
        for (const IniField &f : s.fields) {
            bool digits = f.key.size() > 4 && f.key.compare(0, 4, "font") == 0;
            for (size_t i = 4; i < f.key.size(); i++) digits = digits && isdigit((unsigned char)f.key[i]);
            int n = digits ? atoi(f.key.c_str() + 4) : -1;
            if (n >= 0 && (font < 0 || n < font)) font = n;
        }
    }
    if (font < 0) {
        font = 0;
        if (def) {
            if (!Ini_SetField(out, *def, "font0", "f-6x9.def")) return false;
        } else {
            out.insert(LineStart(out, ini.sections[doc.scenes[0].section].line), "[SceneDef]\nfont0 = f-6x9.def\n\n");
        }
    }
    // A field at a time: each moves the lines after it
    for (int step = 0; step < 3; step++) {
        IniFile now;
        StoryDoc read;
        Ini_Parse(out, now);
        Story_Read(now, read);
        const StoryScene &sc = read.scenes[scene];
        // Its key from the first step on: that layer exists then
        std::string key = step == 0 ? Story_NewLayerKey(sc) : sc.layers.back().key;
        if (step > 0)
            for (const StoryLayer &l : sc.layers)
                if (l.text == "Text" && l.number >= sc.layers.back().number) key = l.key;
        float middle[2] = {read.coord[0] / 2 - sc.layerall[0], read.coord[1] / 2 - sc.layerall[1]};
        // White, the default, does not show on a light scene
        bool light = sc.clear[0] >= 0 && sc.clear[0] + sc.clear[1] + sc.clear[2] > 3 * 128;
        const char *field = step == 0 ? ".text" : step == 1 ? ".font" : ".offset";
        std::string value = step == 0 ? "\"Text\"" : step == 1 ? std::to_string(font) + (light ? ", 0, 0, 0, 0, 0" : "") : Ini_Floats(middle, 2);
        if (!Ini_SetField(out, now.sections[sc.section], (key + field).c_str(), value)) return false;
    }
    text = out;
    return true;
}

bool Story_RemoveKeys(std::string &text, const IniSection &s, const std::string &prefix) {
    std::vector<int> lines;
    for (const IniField &f : s.fields)
        if (f.key.compare(0, prefix.size(), prefix) == 0) lines.push_back(f.line);
    std::sort(lines.rbegin(), lines.rend());
    for (int line : lines) {
        size_t from = LineStart(text, line), to = LineStart(text, line + 1);
        text.erase(from, to - from);
    }
    return !lines.empty();
}
