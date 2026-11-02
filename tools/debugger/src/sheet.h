// A sprite sheet cut in pieces: inside an area of the image, the pixels that are not background and
// touch (or nearly) make one piece. No window here: app_sheet.cpp is the tool.
#pragma once

#include <vector>

struct SheetRect {
    int x = 0, y = 0, w = 0, h = 0;
};

struct SheetImage {
    int w = 0, h = 0;
    std::vector<unsigned char> rgba;
};

struct SheetOptions {
    SheetRect area;
    bool useKey = false;           // a colour is background too, besides the transparent pixels
    unsigned char key[3] = {0, 0, 0};
    int tolerance = 0;             // how far from it each channel can be
    int gap = 0;                   // parts with up to this many empty pixels between them are one piece
    int minPixels = 1;             // pieces with fewer pixels are left out
};

struct SheetCut {
    SheetRect area;                // cut to the image
    std::vector<SheetRect> pieces; // in reading order: rows from the top, each from the left
    std::vector<int> labels;       // for each pixel of the area, the piece it belongs to plus 1, 0 none
};

bool Sheet_HasTransparency(const SheetImage &img);
SheetCut Sheet_Cut(const SheetImage &img, const SheetOptions &o);
// The piece as RGBA, as big as its rectangle: what is not its own is transparent, so that a neighbour
// reaching into the rectangle stays out.
std::vector<unsigned char> Sheet_Picture(const SheetImage &img, const SheetCut &cut, int piece);
