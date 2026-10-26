// SND files (sounds) in memory: the WAV files of each group and number.
#pragma once

#include <string>
#include <vector>

struct SndSound {
    int group = 0, number = 0;
    std::string wav; // the whole WAV file
};

struct SndFile {
    std::string header; // the file's first 0x200 bytes (version, comment), kept
    std::vector<SndSound> sounds;
};

bool Snd_Parse(const std::string &data, SndFile &out, std::string *error);
std::string Snd_Build(const SndFile &snd); // the header kept (or a new one), the sounds one after the other
int Snd_Find(const SndFile &snd, int group, int number);

struct WavInfo {
    bool pcm = false;
    unsigned channels = 0, rate = 0, bits = 0, bytes = 0;
    double Seconds() const { return rate && channels && bits ? bytes / (rate * channels * (bits / 8.0)) : 0.0; }
};
bool Wav_Describe(const std::string &wav, WavInfo *info, std::string *error);
