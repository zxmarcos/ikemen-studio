// .fnt files: see fnt.h. The header: "ElecbyteFnt\0", two 16-bit versions, then the PCX's
// offset and length and the text's (32-bit, little endian), at 16, 20, 24 and 28.
#include "fnt.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

static unsigned U32(const std::string &b, size_t at) {
    return (unsigned char)b[at] | (unsigned char)b[at + 1] << 8 | (unsigned char)b[at + 2] << 16 |
           (unsigned)(unsigned char)b[at + 3] << 24;
}

static void PutU32(std::string &b, size_t at, unsigned v) {
    for (int i = 0; i < 4; i++) b[at + i] = (char)(v >> (8 * i) & 0xFF);
}

bool FntV1_Read(const std::string &bytes, FntV1 &out) {
    out = FntV1();
    out.bytes = bytes;
    if (bytes.size() < 32 || memcmp(bytes.data(), "ElecbyteFnt\0", 12) != 0) {
        out.error = "not a .fnt file";
        return false;
    }
    out.pcxAt = U32(bytes, 16), out.pcxLen = U32(bytes, 20);
    out.textAt = U32(bytes, 24), out.textLen = U32(bytes, 28);
    if (out.pcxAt + out.pcxLen > bytes.size() || out.textAt + out.textLen > bytes.size()) {
        out.error = "its offsets go past its end";
        return false;
    }
    std::string t = bytes.substr(out.textAt, out.textLen);
    out.crlf = t.find("\r\n") != std::string::npos;
    for (char ch : t)
        if (ch != '\r') out.text += ch;
    return true;
}

std::string FntV1_Build(const FntV1 &f, const std::string &text) {
    std::string t;
    for (char ch : text) {
        if (ch == '\n' && f.crlf) t += '\r';
        t += ch;
    }
    std::string out;
    if (f.textAt >= f.pcxAt + f.pcxLen && f.textAt + f.textLen == f.bytes.size()) {
        out = f.bytes.substr(0, f.textAt) + t; // the text is last: replaced there
    } else {
        out = f.bytes + t; // else appended, the old one left unused
        PutU32(out, 24, (unsigned)f.bytes.size());
    }
    PutU32(out, 28, (unsigned)t.size());
    return out;
}

std::map<int, FntGlyph> FntV1_Map(const std::string &text, int sizeX) {
    std::map<int, FntGlyph> out;
    bool inMap = false, done = false;
    int ofs = 0, w = 0;
    size_t start = 0;
    while (start <= text.size() && !done) {
        size_t nl = text.find('\n', start);
        std::string line = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        start = nl == std::string::npos ? text.size() + 1 : nl + 1;
        size_t a = line.find_first_not_of(" \t\r");
        line = a == std::string::npos ? "" : line.substr(a);
        if (!line.empty() && line[0] == '[') {
            if (inMap) done = true; // only the first [Map]
            std::string name = line.substr(1, line.find(']') - 1);
            for (char &ch : name) ch = (char)tolower((unsigned char)ch);
            inMap = name.substr(0, name.find(' ')) == "map";
            continue;
        }
        if (!inMap) continue;
        // char [offset [width]], before any ";"
        std::string words[3];
        int n = 0;
        line = line.substr(0, line.find(';'));
        for (size_t i = 0; i < line.size() && n < 3;) {
            while (i < line.size() && isspace((unsigned char)line[i])) i++;
            size_t j = i;
            while (j < line.size() && !isspace((unsigned char)line[j])) j++;
            if (j > i) words[n++] = line.substr(i, j - i);
            i = j;
        }
        if (n == 0) continue;
        int c = (unsigned char)words[0][0];
        if (words[0].size() >= 2 && words[0][0] == '0' && (words[0][1] == 'x' || words[0][1] == 'X')) {
            c = 0;
            for (size_t i = 2; i < words[0].size() && isxdigit((unsigned char)words[0][i]); i++)
                c = c << 4 | (isdigit((unsigned char)words[0][i]) ? words[0][i] - '0' : tolower(words[0][i]) - 'a' + 10);
        }
        if (n > 1) ofs = (unsigned short)atoi(words[1].c_str());
        FntGlyph g;
        g.ofs = ofs;
        if (n > 2) {
            w = atoi(words[2].c_str());
            if (w < 0) { // as the engine does it
                ofs = (unsigned short)(ofs + (ofs - w));
                w = -w;
            }
            g.w = (unsigned short)w;
            ofs = (unsigned short)(ofs + g.w - sizeX);
        } else {
            g.w = sizeX;
        }
        ofs = (unsigned short)(ofs + sizeX);
        out[c] = g;
    }
    return out;
}
