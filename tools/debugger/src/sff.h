// SFF files (sprites) in memory: read v1.01 and v2, decode any sprite, write
// v2, make sprites of PNG images and save sprites as PNG.
// The decoders here check
// their output, PNG goes through lodepng.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct SffPal {
    int group = 0, number = 0;
    std::vector<unsigned char> rgba; // 256 colours; colour 0 is transparent
    int ncolors = 256;
};

struct SffSprite {
    int group = 0, number = 0;
    int x = 0, y = 0; // axis
    int w = 0, h = 0;
    int format = 2, depth = 8;         // SFF v2 compression (0 raw, 2 RLE8, 3 RLE5, 4 LZ5, 10-12 PNG) and bits per pixel
    std::shared_ptr<std::string> data; // as in the file, the decoded size first; linked sprites share it
    int pal = -1;                      // index in SffDoc::pals, -1 for true colour
};

struct SffDoc {
    std::string path, error; // error: why it could not be read
    std::vector<SffPal> pals;
    std::vector<SffSprite> sprites; // in the file's order
    bool converted = false;         // read from an SFF v1.01 file (saving writes v2)
};

// Reads the file. A v1.01 file's palettes become (0, n); with pal1 (the character's first .act),
// the palette of sprites 0,0 and 9000,0 becomes 1,1 from it, as the engine uses it.
bool Sff_Load(SffDoc &doc, const std::string &path, const std::string &pal1 = "");
std::string Sff_Build(const SffDoc &doc); // SFF v2.01 (v2.00 compatible unless it has PNG sprites)

int Sff_FindSprite(const SffDoc &doc, int group, int number);
int Sff_FindPal(const SffDoc &doc, int group, int number);
// The pixels: palette indices (bpp 1), RGB (3) or RGBA (4).
bool Sff_Decode(const SffSprite &s, std::vector<unsigned char> &px, int *bpp);
// RGBA with the sprite's palette, or pal (an index in doc.pals) when not -1.
void Sff_ToRgba(const SffDoc &doc, const SffSprite &s, const std::vector<unsigned char> &px, int bpp,
                std::vector<unsigned char> &rgba, int pal = -1);
// The same with any palette (256 RGBA colours; nullptr: grey levels).
void Sff_ToRgbaWith(const SffSprite &s, const std::vector<unsigned char> &px, int bpp, std::vector<unsigned char> &rgba,
                    const unsigned char *pal);

// ACT palettes (a character's pal1..palN): 256 RGB colours, the last first. As 256 RGBA colours,
// colour 0 transparent.
bool Sff_ReadAct(const std::string &path, std::vector<unsigned char> &rgba);
std::string Sff_ActBytes(const std::vector<unsigned char> &rgba);
bool Sff_SavePng(const SffDoc &doc, const SffSprite &s, const std::string &path, std::string *error);

// PNG images as sprites (group, number and axis left at 0). Shared: indexed against palette 1,1
// (made from the images when there is none). Own: a palette (0, n) each, or true colour past
// 255 colours. log tells what was done.
// Same: palette 1,1 for an indexed image's indices as they are (a colour's place is what counts).
// One: a new palette (0, n) made from all the images, which they share.
// Images: PNG, PCX, and whatever Sff_SetImageReader's function reads (BMP, GIF).
enum { SFF_PAL_SHARED, SFF_PAL_OWN, SFF_PAL_SAME, SFF_PAL_ONE };
enum { SFF_AXIS_FEET, SFF_AXIS_CENTRE, SFF_AXIS_TOP_LEFT };
std::vector<SffSprite> Sff_SpritesOfPngs(SffDoc &doc, const std::vector<std::string> &files, int palMode,
                                         const std::function<void(const std::string &)> &log);
// The same with pictures in memory
struct SffPicture {
    int w = 0, h = 0;
    std::vector<unsigned char> rgba;
};
std::vector<SffSprite> Sff_SpritesOfPictures(SffDoc &doc, const std::vector<SffPicture> &pictures, int palMode,
                                             const std::function<void(const std::string &)> &log);
void Sff_SetAxis(SffSprite &s, int mode);
// A palette of up to 255 colours (colour 0 transparent) for colours counted by use (0xRRGGBB): all of them, or a median cut.
std::vector<unsigned char> Sff_MakePalette(const std::map<unsigned, int> &colors);
// Decodes other images to RGBA: the app's, which has the system's codecs.
typedef bool (*SffImageReader)(const unsigned char *data, size_t size, int &w, int &h, std::vector<unsigned char> &rgba);
void Sff_SetImageReader(SffImageReader read);
// A new picture for a sprite (RLE8 with bpp 1, PNG32 with 4), its numbers, axis and palette kept.
void Sff_SetPixels(SffSprite &s, int w, int h, const std::vector<unsigned char> &px, int bpp);

// An 8-bit PCX image (a .fnt's): its palette indices, and its palette as RGBA (256 x 4 bytes; colour 0
// transparent).
bool Sff_DecodePcx(const std::string &data, int *w, int *h, std::vector<unsigned char> &px, unsigned char *pal);
