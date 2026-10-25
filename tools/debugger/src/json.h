// A small JSON reader for the lines the ikdbg.lua bridge sends.
#pragma once

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

private:
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
