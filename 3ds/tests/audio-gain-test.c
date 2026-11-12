#include "audio_gain.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    assert(argc > 1);
    // Maximum supported gain must never wrap or change a sample's sign.
    for (int i = INT16_MIN; i <= INT16_MAX; i++) {
        int16_t s = i;
        audio_gain_pcm(&s, 1, AUDIO_SFX_GAIN_Q8);
        assert(i == 0 ? s == 0 : i < 0 ? s <= 0 : s >= 0);
        if (i > INT16_MAX / 3) assert(s == INT16_MAX);
        if (i < INT16_MIN / 3) assert(s == INT16_MIN);
    }
    for (int file = 1; file < argc; file++) {
        FILE *f = fopen(argv[file], "rb");
        assert(f && !fseek(f, 0, SEEK_END));
        long bytes = ftell(f);
        assert(bytes > 0 && bytes % sizeof(int16_t) == 0);
        rewind(f);
        int16_t *data = malloc(bytes);
        assert(data && fread(data, 1, bytes, f) == (size_t)bytes);
        fclose(f);
        const char *name = strrchr(argv[file], '/');
        name = name ? name + 1 : argv[file];
        int gain = !strncmp(name, "sfx-", 4) ? AUDIO_SFX_GAIN_Q8
                 : !strncmp(name, "intro-", 6) ? AUDIO_INTRO_GAIN_Q8 : AUDIO_MUSIC_GAIN_Q8;
        double before = 0, after = 0;
        for (size_t i = 0; i < bytes / sizeof *data; i++) before += (double)data[i] * data[i];
        audio_gain_pcm(data, bytes / sizeof *data, gain);
        for (size_t i = 0; i < bytes / sizeof *data; i++) {
            assert(data[i] != INT16_MAX && data[i] != INT16_MIN);
            after += (double)data[i] * data[i];
        }
        assert(before > 0 && after > before);
        assert(fabs(10 * log10(after / before) - 20 * log10(gain / 256.0)) < .02);
        printf("%s: +%.2f dB, no saturated samples\n", name, 10 * log10(after / before));
        free(data);
    }
}
