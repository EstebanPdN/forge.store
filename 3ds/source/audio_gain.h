#pragma once
#include <stddef.h>
#include <stdint.h>

// PCM headroom is independent of the DSP mix ceiling. These gains leave the
// bundled music/intro/effects below full scale, including at maximum volume.
enum { AUDIO_MUSIC_GAIN_Q8 = 410, AUDIO_INTRO_GAIN_Q8 = 384, AUDIO_SFX_GAIN_Q8 = 768 };
static inline void audio_gain_pcm(int16_t *samples, size_t count, int gain_q8) {
    for (size_t i = 0; i < count; i++) {
        int32_t value = (int32_t)samples[i] * gain_q8 / 256;
        if (value > INT16_MAX) value = INT16_MAX;
        if (value < INT16_MIN) value = INT16_MIN;
        samples[i] = (int16_t)value;
    }
}
