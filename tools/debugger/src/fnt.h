// MUGEN 1.0 fonts (.fnt): a header, a PCX image of the characters side by side and a text
// ([Def], [Map]). Read, its text replaced (the rest of the file kept), and the character map
// as the engine reads it (loadFntV1).
#pragma once

#include <map>
#include <string>
#include <vector>

struct FntV1 {
    std::string bytes;         // the file
    size_t pcxAt = 0, pcxLen = 0, textAt = 0, textLen = 0;
    std::string text;          // the text part, '\n' line ends
    bool crlf = false;         // the file's text had "\r\n"
    std::string error;         // why it could not be read
};

bool FntV1_Read(const std::string &bytes, FntV1 &out);
std::string FntV1_Build(const FntV1 &f, const std::string &text); // the file with that text

// A character's place in the image: its x and width (the height is the image's)
struct FntGlyph {
    int ofs = 0, w = 0;
};
// The [Map] of a text, with the [Def] size's width (sizeX), as loadFntV1 builds it.
std::map<int, FntGlyph> FntV1_Map(const std::string &text, int sizeX);
