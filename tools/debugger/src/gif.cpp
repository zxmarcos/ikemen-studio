#include "gif.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <unordered_map>

namespace {

void Add16(std::string &out, int v) {
    out += (char)(v & 255);
    out += (char)((v >> 8) & 255);
}

// Bits, low first, in sub-blocks of at most 255 bytes.
struct Bits {
    std::string &out;
    std::string block;
    unsigned value = 0;
    int count = 0;
    void Flush() {
        if (block.empty()) return;
        out += (char)block.size();
        out += block;
        block.clear();
    }
    void Put(int code, int size) {
        value |= (unsigned)code << count;
        count += size;
        while (count >= 8) {
            block += (char)(value & 255);
            value >>= 8;
            count -= 8;
            if (block.size() == 255) Flush();
        }
    }
    void End() {
        if (count > 0) {
            block += (char)(value & 255);
            value = 0, count = 0;
        }
        Flush();
        out += '\0';
    }
};

void Lzw(std::string &out, const std::vector<unsigned char> &px, int minBits) {
    const int clear = 1 << minBits, stop = clear + 1;
    out += (char)minBits;
    Bits bits{out};
    std::unordered_map<unsigned, int> table; // a known code and the next pixel -> its code
    int size = minBits + 1, last = stop;
    bits.Put(clear, size);
    int code = -1;
    for (unsigned char p : px) {
        if (code < 0) {
            code = p;
            continue;
        }
        unsigned key = (unsigned)code << 8 | p;
        auto it = table.find(key);
        if (it != table.end()) {
            code = it->second;
            continue;
        }
        bits.Put(code, size);
        table[key] = ++last;
        if (last >= (1 << size)) size++;
        if (last == 4095) {
            bits.Put(clear, size);
            table.clear();
            size = minBits + 1, last = stop;
        }
        code = p;
    }
    if (code >= 0) bits.Put(code, size);
    bits.Put(clear, size); // the reader's table may have grown a size here: start over before the end
    bits.Put(stop, minBits + 1);
    bits.End();
}

// The frame's colour table (RGB, colour 0 unused: transparent) and its pixels as indices
void Index(const GifFrame &f, size_t count, std::vector<unsigned char> &table, std::vector<unsigned char> &px) {
    for (int low = 0;; low++) { // bits taken off each channel until they fit
        unsigned byte = 0xFFu << low & 0xFF, mask = byte << 16 | byte << 8 | byte;
        std::map<unsigned, int> colours;
        bool fits = true;
        for (size_t i = 0; i < count && fits; i++) {
            const unsigned char *c = &f.rgba[i * 4];
            if (c[3] < 128) continue;
            unsigned v = ((unsigned)c[0] << 16 | (unsigned)c[1] << 8 | c[2]) & mask;
            if (colours.emplace(v, 0).second) fits = colours.size() <= 255;
        }
        if (!fits) continue;
        table.assign(3, 0);
        int n = 1;
        for (auto &kv : colours) {
            kv.second = n++;
            table.push_back((unsigned char)(kv.first >> 16));
            table.push_back((unsigned char)(kv.first >> 8));
            table.push_back((unsigned char)kv.first);
        }
        px.assign(count, 0);
        for (size_t i = 0; i < count; i++) {
            const unsigned char *c = &f.rgba[i * 4];
            if (c[3] >= 128) px[i] = (unsigned char)colours[((unsigned)c[0] << 16 | (unsigned)c[1] << 8 | c[2]) & mask];
        }
        return;
    }
}

} // namespace

std::string Gif_Build(int width, int height, const std::vector<GifFrame> &frames) {
    std::string out = "GIF89a";
    Add16(out, width);
    Add16(out, height);
    out += (char)0x70; // no global table
    out += '\0';
    out += '\0';
    out += std::string("\x21\xFF\x0BNETSCAPE2.0\x03\x01", 16); // loops
    Add16(out, 0);
    out += '\0';
    size_t count = (size_t)width * height;
    for (const GifFrame &f : frames) {
        std::vector<unsigned char> table, px;
        Index(f, count, table, px);
        int bits = 1;
        while ((1u << bits) < table.size() / 3) bits++;
        table.resize((size_t)3 << bits, 0);
        out += std::string("\x21\xF9\x04\x09", 4); // the frame goes before the next one; colour 0 transparent
        Add16(out, f.delay);
        out += '\0'; // transparent index
        out += '\0';
        out += (char)0x2C;
        Add16(out, 0);
        Add16(out, 0);
        Add16(out, width);
        Add16(out, height);
        out += (char)(0x80 | (bits - 1));
        out.append((const char *)table.data(), table.size());
        Lzw(out, px, std::max(2, bits));
    }
    out += (char)0x3B;
    return out;
}
