// Animated GIF files: frames of RGBA pixels written as GIF89a, each with its own colour table. A
// pixel with alpha under 128 is transparent. Past 255 colours in a frame, colours lose their low bits.
#pragma once

#include <string>
#include <vector>

struct GifFrame {
    std::vector<unsigned char> rgba; // width * height * 4
    int delay = 2;                   // hundredths of a second
};

// The frames in a file's bytes; loops for ever.
std::string Gif_Build(int width, int height, const std::vector<GifFrame> &frames);
