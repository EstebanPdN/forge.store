// NDSP audio: looping music, the site's intro sounds and synthesized effects.
#pragma once
#include "common.h"

bool audio_init(void);
/** Loads short stereo effects and prepares the bounded music stream. */
void audio_load_rest(void);
void audio_exit(void);
/** Intro sounds always play, even with music and effects muted. */
void audio_intro(const char *name, float volume);
void sfx(const char *name);
void sfx_pitch(const char *name, float semitones);
void music_play(float fade);
void music_stop(float fade);
/** Lowers the music's brightness on detail and install pages (0 open, 1 muffled). */
void music_muffle(float amount);
void audio_update(void);
void audio_apply_settings(void);

bool audio_available(void);
void music_duck(bool duck);
void audio_log(FILE *f);
