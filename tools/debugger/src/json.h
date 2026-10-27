// A small JSON reader for the lines the ikdbg.lua bridge sends; also edited and written again
// (a glTF file's JSON: its fields keep their order, numbers read back as they were).
#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

struct Json {
    enum Type { Null, Bool, Number, String, Array, Object };

    Type type = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> fields;

    static const Json &None() {
        static const Json none;
        return none;
    }

    const Json &operator[](const char *key) const {
        for (const auto &f : fields)
            if (f.first == key) return f.second;
        return None();
    }
    const Json &operator[](size_t i) const { return i < items.size() ? items[i] : None(); }

    bool isNull() const { return type == Null; }
    size_t size() const { return items.size(); }
    double num(double def = 0) const { return type == Number ? n : type == Bool ? (b ? 1 : 0) : def; }
    int i(int def = 0) const { return type == Number || type == Bool ? (int)num() : def; }
    bool truthy() const { return type == Bool ? b : type == Number ? n != 0 : type != Null; }
    const std::string &str() const { return s; }

    static bool Parse(const char *text, Json &out) {
        Reader r{text};
        r.space();
        if (!r.value(out)) return false;
        r.space();
        return *r.p == 0;
    }

    // Editing: a field of an object (added, null, when missing; a non-object becomes one)
    Json &at(const char *key) {
        if (type != Object) *this = Json(), type = Object;
        for (auto &f : fields)
            if (f.first == key) return f.second;
        fields.push_back({key, Json()});
        return fields.back().second;
    }
    void erase(const char *key) {
        for (size_t k = 0; k < fields.size(); k++)
            if (fields[k].first == key) fields.erase(fields.begin() + k--);
    }
    bool has(const char *key) const { return !(*this)[key].isNull() || HasNull(key); }
    static Json Num(double v) {
        Json j;
        j.type = Number;
        j.n = v;
        return j;
    }
    // A float as the shortest decimal that reads back as it (0.05f is written 0.05)
    static Json Float(float f) {
        char buf[32];
        for (int digits = 6; digits <= 9; digits++) {
            snprintf(buf, sizeof(buf), "%.*g", digits, f);
            if ((float)strtod(buf, nullptr) == f) break;
        }
        return Num(strtod(buf, nullptr));
    }
    static Json Floats(const float *v, int count) {
        Json j;
        j.type = Array;
        for (int k = 0; k < count; k++) j.items.push_back(Float(v[k]));
        return j;
    }
    static Json Str(const std::string &s) {
        Json j;
        j.type = String;
        j.s = s;
        return j;
    }
    static Json Boolean(bool v) {
        Json j;
        j.type = Bool;
        j.b = v;
        return j;
    }
    static Json EmptyObject() {
        Json j;
        j.type = Object;
        return j;
    }

    // As text: compact, or with an indent of that many spaces
    std::string dump(int indent = -1) const {
        std::string out;
        write(out, indent, 0);
        return out;
    }

private:
    bool HasNull(const char *key) const {
        for (const auto &f : fields)
            if (f.first == key) return true;
        return false;
    }
    static void quote(std::string &out, const std::string &s) {
        out += '"';
        for (unsigned char c : s) {
            if (c == '"' || c == '\\') out += '\\', out += (char)c;
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else if (c == '\t') out += "\\t";
            else if (c < 0x20) {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else out += (char)c;
        }
        out += '"';
    }
    // The shortest text that reads back as the same double
    static void number(std::string &out, double v) {
        char buf[40];
        if (!std::isfinite(v)) v = 0;
        if (v == std::floor(v) && std::fabs(v) < 1e15) snprintf(buf, sizeof(buf), "%.0f", v);
        else
            for (int digits = 15; digits <= 17; digits++) {
                snprintf(buf, sizeof(buf), "%.*g", digits, v);
                if (strtod(buf, nullptr) == v) break;
            }
        out += buf;
    }
    void write(std::string &out, int indent, int depth) const {
        auto newline = [&](int d) {
            if (indent < 0) return;
            out += '\n';
            out.append((size_t)(indent * d), ' ');
        };
        switch (type) {
        case Null: out += "null"; break;
        case Bool: out += b ? "true" : "false"; break;
        case Number: number(out, n); break;
        case String: quote(out, s); break;
        case Array:
            out += '[';
            for (size_t k = 0; k < items.size(); k++) {
                if (k) out += ',';
                newline(depth + 1);
                items[k].write(out, indent, depth + 1);
            }
            if (!items.empty()) newline(depth);
            out += ']';
            break;
        case Object:
            out += '{';
            for (size_t k = 0; k < fields.size(); k++) {
                if (k) out += ',';
                newline(depth + 1);
                quote(out, fields[k].first);
                out += indent < 0 ? ":" : ": ";
                fields[k].second.write(out, indent, depth + 1);
            }
            if (!fields.empty()) newline(depth);
            out += '}';
            break;
        }
    }

    struct Reader {
        const char *p;

        void space() {
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        }
        bool word(const char *w) {
            size_t len = strlen(w);
            if (strncmp(p, w, len) != 0) return false;
            p += len;
            return true;
        }
        static void utf8(std::string &out, unsigned c) {
            if (c < 0x80) {
                out += (char)c;
            } else if (c < 0x800) {
                out += (char)(0xC0 | (c >> 6));
                out += (char)(0x80 | (c & 0x3F));
            } else {
                out += (char)(0xE0 | (c >> 12));
                out += (char)(0x80 | ((c >> 6) & 0x3F));
                out += (char)(0x80 | (c & 0x3F));
            }
        }
        bool string(std::string &out) {
            if (*p != '"') return false;
            p++;
            while (*p && *p != '"') {
                if (*p != '\\') {
                    out += *p++;
                    continue;
                }
                p++;
                switch (*p) {
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    char hex[5] = {0};
                    for (int k = 0; k < 4; k++) {
                        if (!p[1 + k]) return false;
                        hex[k] = p[1 + k];
                    }
                    utf8(out, (unsigned)strtoul(hex, nullptr, 16));
                    p += 4;
                    break;
                }
                case 0: return false;
                default: out += *p; break;
                }
                p++;
            }
            if (*p != '"') return false;
            p++;
            return true;
        }
        bool value(Json &v) {
            space();
            if (*p == '{') {
                v.type = Object;
                p++;
                space();
                if (*p == '}') {
                    p++;
                    return true;
                }
                for (;;) {
                    space();
                    std::pair<std::string, Json> f;
                    if (!string(f.first)) return false;
                    space();
                    if (*p++ != ':') return false;
                    if (!value(f.second)) return false;
                    v.fields.push_back(std::move(f));
                    space();
                    if (*p == ',') {
                        p++;
                        continue;
                    }
                    if (*p == '}') {
                        p++;
                        return true;
                    }
                    return false;
                }
            }
            if (*p == '[') {
                v.type = Array;
                p++;
                space();
                if (*p == ']') {
                    p++;
                    return true;
                }
                for (;;) {
                    Json item;
                    if (!value(item)) return false;
                    v.items.push_back(std::move(item));
                    space();
                    if (*p == ',') {
                        p++;
                        continue;
                    }
                    if (*p == ']') {
                        p++;
                        return true;
                    }
                    return false;
                }
            }
            if (*p == '"') {
                v.type = String;
                return string(v.s);
            }
            if (word("true")) {
                v.type = Bool;
                v.b = true;
                return true;
            }
            if (word("false")) {
                v.type = Bool;
                return true;
            }
            if (word("null")) return true;
            char *end = nullptr;
            v.n = strtod(p, &end);
            if (end == p) return false;
            v.type = Number;
            p = end;
            return true;
        }
    };
};
