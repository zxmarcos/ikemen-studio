// Tests of src/fnt.cpp: the character map, then font/f-4x6.fnt read, its text replaced and
// read back (run from the root of the checkout; skipped when missing).
// Built as fnt-test; scripts/build.ps1 test builds and runs it.
#include "fnt.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

static int g_failed;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failed++;                                                 \
        }                                                               \
    } while (0)

static void TestMap() {
    std::map<int, FntGlyph> m = FntV1_Map("[Def]\nsize = 4,6\n[Map]\nA\nB 10\nC ;comment\n0x44 20 3\nE\n[Other]\nF\n", 4);
    CHECK(m.size() == 5);
    CHECK(m['A'].ofs == 0 && m['A'].w == 4);
    CHECK(m['B'].ofs == 10 && m['C'].ofs == 14);
    CHECK(m['D'].ofs == 20 && m['D'].w == 3 && m['E'].ofs == 23); // a width sets where the next starts
    CHECK(m.count('F') == 0);
}

static void TestFile() {
    std::ifstream in("font/f-4x6.fnt", std::ios::binary);
    if (!in) {
        printf("skipped (missing): font/f-4x6.fnt\n");
        return;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    FntV1 f;
    CHECK(FntV1_Read(ss.str(), f));
    printf("f-4x6.fnt: pcx %zu bytes, text %zu bytes, %zu characters\n", f.pcxLen, f.textLen, FntV1_Map(f.text, 4).size());
    CHECK(f.text.find("[Def]") != std::string::npos || f.text.find("[def]") != std::string::npos);
    CHECK(FntV1_Build(f, f.text) == f.bytes); // the same text: the same file
    std::string text = f.text + "\n; added\n";
    FntV1 g;
    CHECK(FntV1_Read(FntV1_Build(f, text), g));
    CHECK(g.text == text && g.pcxAt == f.pcxAt && g.pcxLen == f.pcxLen);
    CHECK(g.bytes.compare(g.pcxAt, g.pcxLen, f.bytes, f.pcxAt, f.pcxLen) == 0);
}

int main() {
    TestMap();
    TestFile();
    printf(g_failed ? "fnt-test: %d failed\n" : "fnt-test: all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
