// Palettes as 256 RGBA colours (1024 bytes): read from and written to the files other programs use,
// and what can be done to their colours. Colour 0 is transparent and the tools leave it alone.
#pragma once

#include <string>
#include <vector>

// .act (as MUGEN writes it, the last colour first), .pal (JASC text or RIFF), .aco (Photoshop),
// .ase (Adobe), .gpl (GIMP); by the name's extension. A file with fewer colours leaves the rest
// black. *count: how many it has (may be nullptr).
bool Pal_Read(const std::string &path, std::vector<unsigned char> &rgba, int *count, std::string *error);
// .act, .pal (JASC), .aco, .gpl
bool Pal_Write(const std::string &path, const std::vector<unsigned char> &rgba, int count, std::string *error);

// sel: 256 flags
void Pal_Move(std::vector<unsigned char> &rgba, int from, int to); // the colours between shift by one
void Pal_Reverse(std::vector<unsigned char> &rgba, const std::vector<char> &sel);
void Pal_SortByTone(std::vector<unsigned char> &rgba, const std::vector<char> &sel); // greys first, then by hue and lightness
void Pal_Fill(std::vector<unsigned char> &rgba, const std::vector<char> &sel, const unsigned char *colour);
enum { PAL_INVERT, PAL_GREY, PAL_SWAP_RB, PAL_SWAP_RG, PAL_SWAP_GB, PAL_ROTATE, PAL_EFFECTS };
const char *Pal_EffectName(int effect);
void Pal_Effect(std::vector<unsigned char> &rgba, const std::vector<char> &sel, int effect);

// The PalFX controller's sums, as the game does them on a colour: inverted, towards grey, added to,
// multiplied. tick: the sine's place.
struct PalFx {
    int add[3] = {0, 0, 0}, mul[3] = {256, 256, 256}, sinadd[3] = {0, 0, 0};
    int period = 0;
    float color = 1; // 0 grey, 1 its colours
    bool invert = false;
    bool Plain() const;
};
void Pal_ApplyFx(std::vector<unsigned char> &rgba, const PalFx &fx, int tick);
std::string Pal_FxText(const PalFx &fx, int time); // as a controller of a CNS file
