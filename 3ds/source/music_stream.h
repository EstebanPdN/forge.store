#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
// Ogg Vorbis at 32 kHz stereo, decoded with Tremor (integer, made for ARM) on the
// console and with libvorbisfile in the host harness. Only the music reader touches it.
// A corrupt page, a wrong format or an empty/missing loop stops the stream rather than spins.
#ifdef MUSIC_STREAM_HOST
#include <vorbis/vorbisfile.h>
#define music_ov_read(vf, buf, n, bs) ov_read(vf, buf, n, 0, 2, 1, bs)
#else
#include <tremor/ivorbisfile.h>
#define music_ov_read(vf, buf, n, bs) ov_read(vf, buf, n, bs)
#endif

#define MUSIC_STREAM_RATE 32000

typedef struct {
    OggVorbis_File vf;
    bool open, opening;
    const char *loop;
    bool produced;  // this pass has given at least one sample (an empty loop must not spin)
} MusicStream;

static inline size_t music_cb_read(void *p, size_t size, size_t n, void *f) { return fread(p, size, n, (FILE *)f); }
static inline int music_cb_seek(void *f, ogg_int64_t at, int whence) { return fseek((FILE *)f, (long)at, whence); }
static inline int music_cb_close(void *f) { return fclose((FILE *)f); }
static inline long music_cb_tell(void *f) { return ftell((FILE *)f); }

static inline void music_stream_close(MusicStream *s) {
    if (s->open) ov_clear(&s->vf);  // closes the file through the callbacks
    s->open = false;
}
static inline bool music_stream_file(MusicStream *s, const char *path) {
    music_stream_close(s);
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) return false;
    setvbuf(f, NULL, _IOFBF, 16384);
    ov_callbacks cb = {music_cb_read, music_cb_seek, music_cb_close, music_cb_tell};
    if (ov_open_callbacks(f, &s->vf, NULL, 0, cb)) { fclose(f); return false; }
    s->open = true;
    vorbis_info *vi = ov_info(&s->vf, -1);
    if (!vi || vi->channels != 2 || vi->rate != MUSIC_STREAM_RATE) { music_stream_close(s); return false; }
    s->produced = false;
    return true;
}
static inline bool music_stream_open(MusicStream *s, const char *opening, const char *loop) {
    s->loop = loop;
    s->opening = true;
    return music_stream_file(s, opening);
}
/** Whole-file length in stereo frames, or -1 (used to decode short effects at once). */
static inline long music_stream_frames(MusicStream *s) { return s->open ? (long)ov_pcm_total(&s->vf, -1) : -1; }
/** Raw decode of the current file without looping; returns bytes, 0 at the end, <0 on error. */
static inline long music_stream_raw(MusicStream *s, unsigned char *dst, int bytes) {
    int section = 0;
    if (!s->open) return -1;
    long n = music_ov_read(&s->vf, (char *)dst, bytes, &section);
    return n < 0 || n % 4 ? -1 : n;
}
/** Fills exactly `bytes` of interleaved PCM16, continuing from the opening into the loop. */
static inline bool music_stream_read(MusicStream *s, unsigned char *dst, size_t bytes) {
    if (!s->open || bytes > 1024 * 1024 || bytes % 4) return false;
    size_t filled = 0;
    while (filled < bytes) {
        int section = 0;
        size_t want = bytes - filled;
        long n = music_ov_read(&s->vf, (char *)dst + filled, want > 4096 ? 4096 : (int)want, &section);
        if (n < 0 || n % 4) { music_stream_close(s); return false; }
        if (n > 0) { filled += (size_t)n; s->produced = true; continue; }
        // End of this pass: the opening hands over to the loop; the loop starts again.
        if (s->opening) {
            s->opening = false;
            if (!music_stream_file(s, s->loop)) return false;
        } else {
            if (!s->produced) { music_stream_close(s); return false; }
            if (ov_pcm_seek(&s->vf, 0) && !music_stream_file(s, s->loop)) return false;
            s->produced = false;
        }
    }
    return true;
}
