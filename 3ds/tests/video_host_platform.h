// Host-only platform shim. Production video.c is included by tests/video-test.c and runs
// its real worker on pthreads, with host libtheoradec / libjpeg-turbo and a simulated NDSP
// channel. Nothing here is compiled into the 3DS build.
#pragma once
#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <theora/theoradec.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
#define U64_MAX UINT64_MAX
#define CUR_THREAD_HANDLE 0
#define R_SUCCEEDED(r) ((r) >= 0)

// ---- public API (mirrors source/video.h, which pulls in the 3DS headers) --------------------
bool video_start(const char *file);
void video_stop(void);
void video_pump(void);
bool video_stopping(void);
void video_exit(void);
void video_update(void);
bool video_active(void);
bool video_paused(void);
void video_pause(void);
void video_toggle_mute(void);
bool video_muted(void);
bool video_has_audio(void);
void video_seek(float seconds);
float video_time(void);
float video_duration(void);
bool video_bottom(void);
void video_draw(bool bottom, float x, float y, float w, float h, float alpha);
const char *video_error(void);

// ---- app state the decoder reads -------------------------------------------------------------
static struct { bool music, sfx; float volume; } S = {true, true, .8f};
static float DT = 1 / 60.f;
static bool host_audio = true, host_path_ready = true, host_ducked;
static const char *T(const char *key) { return key; }
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static bool media_path_ready(const char *path) { return host_path_ready; }
static bool audio_available(void) { return host_audio; }
static void music_duck(bool duck) { host_ducked = duck; }

// ---- threads -----------------------------------------------------------------------------------
static double host_seconds(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
typedef pthread_mutex_t LightLock;
static void LightLock_Init(LightLock *l) { assert(!pthread_mutex_init(l, NULL)); }
static void LightLock_Lock(LightLock *l) { assert(!pthread_mutex_lock(l)); }
static void LightLock_Unlock(LightLock *l) { assert(!pthread_mutex_unlock(l)); }
static void svcGetThreadPriority(s32 *p, int unused) { *p = 0x30; }
static void svcSleepThread(u64 ns) { struct timespec t = {ns / 1000000000, ns % 1000000000}; nanosleep(&t, NULL); }
typedef struct HostThread { pthread_t id; void (*fn)(void *); void *arg; bool done; } *Thread;
static int host_threads; // live worker threads: must be zero whenever resources are released
static void *host_thread(void *p) {
    Thread t = p;
    t->fn(t->arg);
    __atomic_store_n(&t->done, true, __ATOMIC_RELEASE);
    return NULL;
}
static Thread threadCreate(void (*fn)(void *), void *arg, size_t stack, int priority, int core, bool detached) {
    Thread t = calloc(1, sizeof *t);
    assert(t);
    t->fn = fn;
    t->arg = arg;
    assert(!pthread_create(&t->id, NULL, host_thread, t));
    __atomic_add_fetch(&host_threads, 1, __ATOMIC_RELAXED);
    return t;
}
// Like svcWaitSynchronization: a timeout is a positive, non-failing result.
static int threadJoin(Thread t, u64 timeout_ns) {
    double end = host_seconds() + (timeout_ns == U64_MAX ? 1e9 : timeout_ns / 1e9);
    while (!__atomic_load_n(&t->done, __ATOMIC_ACQUIRE)) {
        if (host_seconds() >= end) return 0x09401BFE;
        usleep(200);
    }
    assert(!pthread_join(t->id, NULL));
    __atomic_sub_fetch(&host_threads, 1, __ATOMIC_RELAXED);
    return 0;
}
static void threadFree(Thread t) { free(t); }

// ---- GPU textures ------------------------------------------------------------------------------
enum { GPU_RGB565 = 1, GPU_NEAREST = 0, GPU_LINEAR = 1, GPU_CLAMP_TO_EDGE = 0 };
typedef struct { void *data; int width, height, filter; } C3D_Tex;
typedef struct { u16 width, height; float left, top, right, bottom; } Tex3DS_SubTexture;
typedef struct { C3D_Tex *tex; const Tex3DS_SubTexture *subtex; } C2D_Image;
static int host_textures, host_flushes, host_draws;
static bool C3D_TexInit(C3D_Tex *t, int w, int h, int format) {
    assert(format == GPU_RGB565);
    t->data = malloc(w * h * 2); // deliberately not cleared, like linear memory
    assert(t->data);
    memset(t->data, 0xa5, w * h * 2);
    t->width = w;
    t->height = h;
    host_textures++;
    return true;
}
static void C3D_TexDelete(C3D_Tex *t) {
    assert(!__atomic_load_n(&host_threads, __ATOMIC_RELAXED)); // never while the decoder may run
    free(t->data);
    t->data = NULL;
    host_textures--;
}
static void C3D_TexFlush(C3D_Tex *t) { host_flushes++; }
static void C3D_TexSetFilter(C3D_Tex *t, int mag, int min) { t->filter = mag; }
static void C3D_TexSetWrap(C3D_Tex *t, int s, int w) {}
static C2D_Image host_drawn[2];
static void draw_img(C2D_Image im, float x, float y, float sx, float sy, float alpha) {
    host_drawn[im.subtex->width == 320] = im;
    host_draws++;
}

// ---- NDSP channel: buffers play back to back in simulated time ------------------------------
enum { NDSP_WBUF_FREE = 0, NDSP_WBUF_QUEUED = 1, NDSP_WBUF_PLAYING = 2, NDSP_WBUF_DONE = 3 };
enum { NDSP_FORMAT_MONO_PCM16 = 1, NDSP_INTERP_LINEAR = 1 };
typedef struct ndspWaveBuf { const void *data_vaddr; u32 nsamples; u8 status; double host_end; } ndspWaveBuf;
static struct {
    float rate, mix;
    bool paused;
    double now, tail;         // simulated seconds; end of the last queued buffer
    double underrun;          // silence between buffers after the first
    ndspWaveBuf *queued[8];
    int nqueued;
    s16 *pcm;                 // everything ever queued, in order
    size_t samples, capacity, buffers, clears;
} host_dsp;
static int linear_live;
static void *linearAlloc(size_t n) { linear_live++; return malloc(n); }
static void linearFree(void *p) { if (p) { linear_live--; free(p); } }
static void DSP_FlushDataCache(const void *p, u32 n) {}
static void ndspChnReset(int ch) { host_dsp.rate = 0; host_dsp.nqueued = 0; host_dsp.paused = false; host_dsp.tail = 0; }
static void ndspChnSetFormat(int ch, int format) { assert(format == NDSP_FORMAT_MONO_PCM16); }
static void ndspChnSetRate(int ch, float rate) { host_dsp.rate = rate; }
static void ndspChnSetInterp(int ch, int interp) {}
static void ndspChnSetMix(int ch, float mix[12]) { host_dsp.mix = mix[0]; }
static void ndspChnSetPaused(int ch, bool paused) { host_dsp.paused = paused; }
static void ndspChnWaveBufClear(int ch) { host_dsp.nqueued = 0; host_dsp.tail = 0; host_dsp.clears++; }
static void ndspChnWaveBufAdd(int ch, ndspWaveBuf *w) {
    assert(host_dsp.rate > 0 && w->nsamples && host_dsp.nqueued < 8);
    if (host_dsp.samples + w->nsamples > host_dsp.capacity) {
        host_dsp.capacity = (host_dsp.samples + w->nsamples) * 2;
        host_dsp.pcm = realloc(host_dsp.pcm, host_dsp.capacity * 2);
        assert(host_dsp.pcm);
    }
    memcpy(host_dsp.pcm + host_dsp.samples, w->data_vaddr, w->nsamples * 2);
    host_dsp.samples += w->nsamples;
    if (host_dsp.buffers && host_dsp.tail && host_dsp.now > host_dsp.tail) host_dsp.underrun += host_dsp.now - host_dsp.tail;
    double start = host_dsp.tail > host_dsp.now ? host_dsp.tail : host_dsp.now;
    w->host_end = host_dsp.tail = start + w->nsamples / host_dsp.rate;
    w->status = NDSP_WBUF_QUEUED;
    host_dsp.queued[host_dsp.nqueued++] = w;
    host_dsp.buffers++;
}
/** Advances simulated playback: finished buffers become DONE. Call before video_update. */
static void host_dsp_tick(double dt) {
    if (host_dsp.paused) { // a paused channel holds its queue: shift the pending ends
        for (int i = 0; i < host_dsp.nqueued; i++) host_dsp.queued[i]->host_end += dt;
        if (host_dsp.tail) host_dsp.tail += dt;
    }
    host_dsp.now += dt;
    int kept = 0;
    for (int i = 0; i < host_dsp.nqueued; i++) {
        ndspWaveBuf *w = host_dsp.queued[i];
        if (w->host_end <= host_dsp.now) w->status = NDSP_WBUF_DONE;
        else host_dsp.queued[kept++] = w;
    }
    host_dsp.nqueued = kept;
}

// ---- decoder load injection ----------------------------------------------------------------
// Sleeps added around the real libtheora calls, to stand in for a console that cannot
// decode or convert in real time. Zero by default.
static int host_decode_delay_us, host_convert_delay_us;
static int host_decode_packetin(th_dec_ctx *dec, const ogg_packet *op, ogg_int64_t *granule) {
    int us = __atomic_load_n(&host_decode_delay_us, __ATOMIC_RELAXED);
    if (us) usleep(us);
    return th_decode_packetin(dec, op, granule);
}
static int host_decode_ycbcr_out(th_dec_ctx *dec, th_ycbcr_buffer planes) {
    int us = __atomic_load_n(&host_convert_delay_us, __ATOMIC_RELAXED);
    if (us) usleep(us);
    return th_decode_ycbcr_out(dec, planes);
}
#define th_decode_packetin host_decode_packetin
#define th_decode_ycbcr_out host_decode_ycbcr_out
