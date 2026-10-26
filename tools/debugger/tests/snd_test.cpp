// Tests of src/snd.cpp on kfm.snd (run from the root of the checkout; skipped
// when missing): it parses, every sound is a PCM WAV file, and the file built
// again is the same bytes.
#include "snd.h"

#include <cstdio>
#include <fstream>
#include <sstream>

static int g_failed;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failed++;                                                 \
        }                                                               \
    } while (0)

int main() {
    std::ifstream f("chars/kfm/kfm.snd", std::ios::binary);
    if (!f) {
        printf("skipped (missing): chars/kfm/kfm.snd\n");
        return 0;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string data = ss.str(), error;
    SndFile snd;
    CHECK(Snd_Parse(data, snd, &error));
    int pcm = 0;
    for (const SndSound &s : snd.sounds) {
        WavInfo w;
        if (Wav_Describe(s.wav, &w, &error) && w.Seconds() > 0) pcm++;
    }
    printf("kfm.snd: %zu sounds, %d PCM WAV\n", snd.sounds.size(), pcm);
    CHECK(!snd.sounds.empty() && pcm == (int)snd.sounds.size());
    // Elecbyte's files have the sounds one after the other from 0x200: the same bytes come back
    std::string built = Snd_Build(snd);
    CHECK(built.size() == data.size());
    CHECK(built == data);
    SndFile again;
    CHECK(Snd_Parse(built, again, &error) && again.sounds.size() == snd.sounds.size());
    CHECK(Snd_Find(snd, snd.sounds.back().group, snd.sounds.back().number) == (int)snd.sounds.size() - 1);
    printf(g_failed ? "snd-test: %d failed\n" : "snd-test: all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
