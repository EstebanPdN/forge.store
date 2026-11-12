#pragma once
#include <stddef.h>
#include <stdint.h>

// Music gets twice the previous gain, with linked stereo peak protection below.
// Intro/effect gains and the user's master-volume setting remain independent.
enum { AUDIO_MUSIC_GAIN_Q8 = 820, AUDIO_INTRO_GAIN_Q8 = 384, AUDIO_SFX_GAIN_Q8 = 768 };
static inline void audio_gain_pcm(int16_t *samples, size_t count, int gain_q8) {
    for (size_t i = 0; i < count; i++) {
        int32_t value = (int32_t)samples[i] * gain_q8 / 256;
        if (value > INT16_MAX) value = INT16_MAX;
        if (value < INT16_MIN) value = INT16_MIN;
        samples[i] = (int16_t)value;
    }
}

// Linear gain below the knee; a smooth shoulder approaches the ceiling above it.
// Both channels share attenuation so loud transients preserve the stereo image.
// Stateless processing keeps loop boundaries and reader restarts continuous.
static inline void audio_gain_music_pcm(int16_t *samples, size_t frames) {
    enum { KNEE = 24576, CEILING = 31744, RANGE = CEILING - KNEE };
    for (size_t i = 0; i < frames; i++) {
        int32_t left = (int32_t)samples[2*i] * AUDIO_MUSIC_GAIN_Q8 / 256;
        int32_t right = (int32_t)samples[2*i+1] * AUDIO_MUSIC_GAIN_Q8 / 256;
        int32_t peak = left < 0 ? -left : left, other = right < 0 ? -right : right;
        if (other > peak) peak = other;
        if (peak > KNEE) {
            int32_t excess = peak - KNEE;
            int32_t limited = KNEE + excess * RANGE / (excess + RANGE);
            int32_t scale = limited * 32768 / peak;
            left = left * scale / 32768;
            right = right * scale / 32768;
        }
        samples[2*i] = (int16_t)left;
        samples[2*i+1] = (int16_t)right;
    }
}
