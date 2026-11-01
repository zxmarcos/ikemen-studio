// Sprites changed in batch: cropped, scaled, moved between files, sent out as PNG files and read
// back. Linked sprites (the same picture) stay linked. Sprites are indices in doc.sprites.
#pragma once

#include "sff.h"

#include <string>
#include <vector>

// The empty borders off; each sprite shows where it did (its axis moves with the picture). Returns
// how many changed. A sprite with nothing in it stays.
int Sff_Crop(SffDoc &doc, const std::vector<int> &sprites);

enum { SFF_SCALE_NEAREST, SFF_SCALE_BILINEAR, SFF_SCALE_EPX };
// The pictures and axes scaled. Bilinear blends colours: an indexed sprite takes the nearest of its
// palette. EPX (Eric's Pixel Expansion) doubles the picture while it is smaller than asked, keeping
// its colours; nearest does the rest. Returns how many changed.
int Sff_Scale(SffDoc &doc, const std::vector<int> &sprites, float xscale, float yscale, int filter);

// One PNG file for each sprite in a folder (group_number.png) and a list of them, sprites.txt, which
// says each one's numbers and axis. aligned: all with the same canvas and axis, so that they stay in
// register in a drawing program.
const char *const kSffListName = "sprites.txt";
bool Sff_Export(const SffDoc &doc, const std::vector<int> &sprites, const std::string &folder, bool aligned, std::string *error);

struct SffListRow {
    std::string file; // as listed: a name in the list's folder
    int group = 0, number = 0, x = 0, y = 0;
};
bool Sff_ReadList(const std::string &path, std::vector<SffListRow> &rows);

// The group and number a file's name says ("200_3.png", "200-3.png", "200,3.png"); false: it says none.
bool Sff_NumbersOfName(const std::string &file, int *group, int *number);

// Sprites of another file brought in, with their numbers. Own: each with its palette (one with the
// same colours when there is one, else a copy). Adapt: its colours mapped to the nearest of palette
// 1,1. Same: palette 1,1 for the same indices, for characters whose palettes are laid out alike.
// True colour sprites stay as they are. A sprite with a number in use is replaced, or skipped.
enum { SFF_IMPORT_OWN, SFF_IMPORT_ADAPT, SFF_IMPORT_SAME };
int Sff_ImportSprites(SffDoc &doc, const SffDoc &from, const std::vector<int> &sprites, bool replace, int palMode, int *skipped);
