#include "snd.h"

#include <algorithm>
#include <cstring>

namespace {

const char kSignature[12] = "ElecbyteSnd";

unsigned U32(const std::string &b, size_t at) {
    return (unsigned char)b[at] | (unsigned char)b[at + 1] << 8 | (unsigned char)b[at + 2] << 16 |
           (unsigned)(unsigned char)b[at + 3] << 24;
}
unsigned U16(const std::string &b, size_t at) { return (unsigned char)b[at] | (unsigned char)b[at + 1] << 8; }
void Add32(std::string &b, unsigned v) {
    for (int i = 0; i < 4; i++) b += (char)(v >> (8 * i));
}
void Put32(std::string &b, size_t at, unsigned v) {
    for (int i = 0; i < 4; i++) b[at + i] = (char)(v >> (8 * i));
}

} // namespace

bool Snd_Parse(const std::string &data, SndFile &out, std::string *error) {
    out = SndFile();
    if (data.size() < 0x200 || data.compare(0, 12, std::string(kSignature, 12)) != 0) {
        *error = "not an SND file";
        return false;
    }
    out.header = data.substr(0, 0x200);
    int count = (int)U32(data, 0x10);
    size_t at = U32(data, 0x14);
    for (int i = 0; i < count; i++) {
        if (at + 16 > data.size()) {
            *error = "the file ends early (sound " + std::to_string(i) + ")";
            return false;
        }
        unsigned size = U32(data, at + 4);
        if (at + 16 + size > data.size()) {
            *error = "the file ends early (sound " + std::to_string(i) + ")";
            return false;
        }
        SndSound s;
        s.group = (int)U32(data, at + 8);
        s.number = (int)U32(data, at + 12);
        s.wav = data.substr(at + 16, size);
        out.sounds.push_back(s);
        size_t next = U32(data, at);
        if (next <= at && i + 1 < count) {
            *error = "sound " + std::to_string(i) + " points back";
            return false;
        }
        at = next;
    }
    return true;
}

std::string Snd_Build(const SndFile &snd) {
    std::string out = snd.header;
    if (out.size() != 0x200) {
        out.assign(0x200, '\0');
        memcpy(&out[0], kSignature, sizeof(kSignature));
        out[12] = 4; // the version Elecbyte's tools write
    }
    Put32(out, 0x10, (unsigned)snd.sounds.size());
    Put32(out, 0x14, 0x200);
    for (const SndSound &s : snd.sounds) {
        Add32(out, (unsigned)(out.size() + 16 + s.wav.size()));
        Add32(out, (unsigned)s.wav.size());
        Add32(out, (unsigned)s.group);
        Add32(out, (unsigned)s.number);
        out += s.wav;
    }
    return out;
}

int Snd_Find(const SndFile &snd, int group, int number) {
    for (int i = 0; i < (int)snd.sounds.size(); i++)
        if (snd.sounds[i].group == group && snd.sounds[i].number == number) return i;
    return -1;
}

bool Wav_Describe(const std::string &w, WavInfo *d, std::string *error) {
    *d = WavInfo();
    if (w.size() < 12 || w.compare(0, 4, "RIFF") != 0 || w.compare(8, 4, "WAVE") != 0) {
        *error = "not a WAV file";
        return false;
    }
    bool fmt = false, data = false;
    size_t at = 12;
    while (at + 8 <= w.size()) {
        unsigned size = U32(w, at + 4);
        if (w.compare(at, 4, "fmt ") == 0 && size >= 16 && at + 8 + 16 <= w.size()) {
            fmt = true;
            d->pcm = U16(w, at + 8) == 1;
            d->channels = U16(w, at + 10);
            d->rate = U32(w, at + 12);
            d->bits = U16(w, at + 22);
        } else if (w.compare(at, 4, "data") == 0) {
            data = true;
            d->bytes = (unsigned)std::min<size_t>(size, w.size() - (at + 8));
        }
        at += 8 + (size_t)size + (size & 1);
    }
    if (!fmt || !data) {
        *error = "a WAV file without its format or its data";
        return false;
    }
    if (!d->pcm) {
        *error = "not PCM";
        return false;
    }
    return true;
}
