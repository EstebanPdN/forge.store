// NDSP stereo PCM at 32000 Hz from Ogg Vorbis (Tremor). Short effects are decoded once by
// a background worker into linear memory; the opening arrangement and music loop are
// decoded while they stream from RomFS, a few seconds ahead.
#include "audio.h"
#include "audio_gain.h"
#include "music_stream.h"

#define RATE 32000
#define CH_MUSIC 0
#define CH_INTRO 1
#define CH_SFX 2
#define N_SFX 8
#define MUSIC_BLOCK 4096  // stereo frames per block: 128 ms
#define MUSIC_BYTES (MUSIC_BLOCK * 4)
#define MUSIC_BUFFERS 8   // about one second queued in the DSP (linear memory)
#define RING_BLOCKS 24    // about three seconds of read-ahead in ordinary RAM

typedef struct {
    char name[20];
    s16 *data;     // linear memory, main thread only
    s16 *decoded;  // ordinary heap, handed over by the clip worker
    u32 samples;
} Clip;

static bool ok, ducked;
static Clip clips[24];
// The worker decodes into ordinary memory and publishes `decoded_clips`; the main thread
// moves one clip per frame into linear memory (linearAlloc is not thread-safe) and
// only then makes it playable through `nclips`.
static int nclips, decoded_clips;
static Thread clip_thread;
static ndspWaveBuf wb[2 + N_SFX];
static int next_sfx;
static float music_vol, music_target, music_speed, muffle_now = 15000, muffle_target, master_now = .8f, duck_now = 1;
// The SD card can stall a RomFS read for a long time while a download is being written.
// One thread only reads ahead into the ring; another only feeds the DSP from it, so a
// slow read never leaves the DSP without audio as long as the ring holds data.
static Thread reader_thread, feeder_thread;
static bool stream_exit, stream_wanted;
static u32 stream_gen;  // bumped by the main thread for every fresh start
static LightLock ring_lock;
static u8 *ring;
static u32 ring_head, ring_tail;  // block counters, guarded by ring_lock
static ndspWaveBuf music_waves[MUSIC_BUFFERS];
static s16 *music_data[MUSIC_BUFFERS];
static u32 streamed_frames, stream_underruns, ring_low = RING_BLOCKS;

static bool wanted(void) { return __atomic_load_n(&stream_wanted, __ATOMIC_ACQUIRE); }
static bool exiting(void) { return __atomic_load_n(&stream_exit, __ATOMIC_ACQUIRE); }

static void music_reader(void *unused) {
    MusicStream stream = {0};
    u32 gen = 0;
    while (!exiting()) {
        if (!wanted()) {
            music_stream_close(&stream);
            svcSleepThread(8000000);
            continue;
        }
        u32 now_gen = __atomic_load_n(&stream_gen, __ATOMIC_ACQUIRE);
        if (!stream.open || gen != now_gen) {
            gen = now_gen;
            if (!music_stream_open(&stream,"romfs:/audio/music-start.ogg","romfs:/audio/music.ogg")) {
                __atomic_store_n(&stream_wanted, false, __ATOMIC_RELEASE);
                continue;
            }
        }
        LightLock_Lock(&ring_lock);
        bool space = ring_head - ring_tail < RING_BLOCKS;
        u32 at = ring_head % RING_BLOCKS;
        LightLock_Unlock(&ring_lock);
        if (!space) {
            svcSleepThread(8000000);
            continue;
        }
        // This block is invisible to the feeder until ring_head advances.
        u8 *dst = ring + at * MUSIC_BYTES;
        if (!music_stream_read(&stream,dst,MUSIC_BYTES)) {
            music_stream_close(&stream);
            __atomic_store_n(&stream_wanted, false, __ATOMIC_RELEASE);
            continue;
        }
        audio_gain_music_pcm((s16 *)dst, MUSIC_BLOCK);
        LightLock_Lock(&ring_lock);
        if (gen == __atomic_load_n(&stream_gen, __ATOMIC_ACQUIRE)) ring_head++;  // else: restarted meanwhile
        LightLock_Unlock(&ring_lock);
    }
    music_stream_close(&stream);
}

static void music_feeder(void *unused) {
    int slot = 0;
    bool queued = false;
    while (!exiting()) {
        if (!wanted()) {
            if (queued) {
                ndspChnWaveBufClear(CH_MUSIC);
                memset(music_waves, 0, sizeof music_waves);
                slot = 0;
                queued = false;
            }
            svcSleepThread(4000000);
            continue;
        }
        ndspWaveBuf *b = &music_waves[slot];
        u8 status = __atomic_load_n(&b->status, __ATOMIC_ACQUIRE);
        if (status != NDSP_WBUF_FREE && status != NDSP_WBUF_DONE) {
            svcSleepThread(2000000);
            continue;
        }
        LightLock_Lock(&ring_lock);
        u32 level = ring_head - ring_tail;
        if (level) {
            memcpy(music_data[slot], ring + (ring_tail % RING_BLOCKS) * MUSIC_BYTES, MUSIC_BYTES);
            ring_tail++;
        }
        LightLock_Unlock(&ring_lock);
        if (!level) {  // the reader is behind (slow SD): keep polling, never spin
            svcSleepThread(2000000);
            continue;
        }
        u32 streamed = __atomic_load_n(&streamed_frames, __ATOMIC_RELAXED);
        if (streamed >= MUSIC_BLOCK * MUSIC_BUFFERS) {
            if (level < __atomic_load_n(&ring_low, __ATOMIC_RELAXED)) __atomic_store_n(&ring_low, level, __ATOMIC_RELAXED);
            if (!ndspChnIsPlaying(CH_MUSIC)) __atomic_fetch_add(&stream_underruns, 1, __ATOMIC_RELAXED);
        }
        DSP_FlushDataCache(music_data[slot], MUSIC_BYTES);
        memset(b, 0, sizeof *b);
        b->data_vaddr = music_data[slot];
        b->nsamples = MUSIC_BLOCK;  // Stereo sample frames.
        ndspChnWaveBufAdd(CH_MUSIC, b);
        __atomic_fetch_add(&streamed_frames, MUSIC_BLOCK, __ATOMIC_RELAXED);
        slot = (slot + 1) % MUSIC_BUFFERS;
        queued = true;
    }
    ndspChnWaveBufClear(CH_MUSIC);
}

static Clip *find(const char *name) {
    for (int i = 0; i < nclips; i++)
        if (!strcmp(clips[i].name, name))
            return &clips[i];
    return NULL;
}

// Worker only. Effects play once they are published; until then they are skipped.
static void load(const char *name) {
    int n = __atomic_load_n(&decoded_clips, __ATOMIC_ACQUIRE);
    if (n >= 24)
        return;
    char path[64];
    snprintf(path, sizeof path, "romfs:/audio/%s.ogg", name);
    MusicStream stream = {0};
    if (!music_stream_file(&stream, path))
        return;
    long frames = music_stream_frames(&stream);
    s16 *data = frames > 0 && frames <= 8 * RATE ? malloc(frames * 4) : NULL;
    long size = frames * 4, filled = 0;
    while (data && filled < size && !__atomic_load_n(&stream_exit, __ATOMIC_ACQUIRE)) {
        long got = music_stream_raw(&stream, (unsigned char *)data + filled, size - filled > 4096 ? 4096 : (int)(size - filled));
        if (got <= 0) break;
        filled += got;
    }
    music_stream_close(&stream);
    if (!data || filled != size) {
        free(data);
        return;
    }
    audio_gain_pcm(data, size / sizeof *data, !strncmp(name, "sfx-", 4) ? AUDIO_SFX_GAIN_Q8 : AUDIO_INTRO_GAIN_Q8);
    Clip *c = &clips[n];
    snprintf(c->name, sizeof c->name, "%s", name);
    c->decoded = data;
    c->samples = size / 4;
    __atomic_store_n(&decoded_clips, n + 1, __ATOMIC_RELEASE);
}
// Main thread: at most one clip per frame becomes playable.
static void adopt_clip(void) {
    if (nclips >= __atomic_load_n(&decoded_clips, __ATOMIC_ACQUIRE))
        return;
    Clip *c = &clips[nclips];
    c->data = linearAlloc(c->samples * 4);
    if (!c->data)
        return;  // try again next frame
    memcpy(c->data, c->decoded, c->samples * 4);
    DSP_FlushDataCache(c->data, c->samples * 4);
    free(c->decoded);
    c->decoded = NULL;
    nclips++;
}
static void clip_worker(void *unused) {
    // The introduction's sounds first, then the effects in order of first use.
    static const char *names[] = {"intro-welcome", "intro-enter", "sfx-move", "sfx-select", "sfx-back", "sfx-open",
                                  "sfx-close", "sfx-tab", "sfx-page", "sfx-error", "sfx-type", "sfx-toggle",
                                  "sfx-download", "sfx-complete", "sfx-anvil", "sfx-warn", "sfx-launch"};
    for (unsigned i = 0; i < sizeof names / sizeof *names && !__atomic_load_n(&stream_exit, __ATOMIC_ACQUIRE); i++)
        load(names[i]);
}

static float vol_applied[2 + N_SFX];  // gain + 1 last sent to each channel (0 = none yet)
static void set_vol(int ch, float v) {
    float mix[12] = {0};
    mix[0] = mix[1] = clampf(v * 2, 0, 1);
    if (mix[0] == vol_applied[ch] - 1) return;  // unchanged: leave the channel alone
    vol_applied[ch] = mix[0] + 1;
    ndspChnSetMix(ch, mix);
}

static void play(int ch, Clip *c, float vol, float rate, bool loop) {
    if (!ok || !c)
        return;
    ndspChnWaveBufClear(ch);
    ndspChnSetRate(ch, rate);
    set_vol(ch, vol);
    ndspWaveBuf *b = &wb[ch];
    memset(b, 0, sizeof *b);
    b->data_vaddr = c->data;
    b->nsamples = c->samples;
    b->looping = loop;
    ndspChnWaveBufAdd(ch, b);
}

bool audio_init(void) {
    // Needs sdmc:/3ds/dspfirm.cdc; without it the store runs silently.
    ok = R_SUCCEEDED(ndspInit());
    if (!ok)
        return false;
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    memset(vol_applied, 0, sizeof vol_applied);
    for (int ch = 0; ch < 2 + N_SFX; ch++) {
        ndspChnReset(ch);
        ndspChnSetInterp(ch, NDSP_INTERP_LINEAR);
        ndspChnSetFormat(ch, NDSP_FORMAT_STEREO_PCM16);
        ndspChnSetRate(ch, RATE);
    }
    __atomic_store_n(&stream_exit, false, __ATOMIC_RELEASE);
    if (!clip_thread) {
        s32 priority = 0x30;
        svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
        // Below the main thread: decoding fills the frame loop's idle time.
        clip_thread = threadCreate(clip_worker, NULL, 64 * 1024, priority + 1, -2, false);
    }
    audio_apply_settings();
    return true;
}

void audio_load_rest(void) {
    if (!ok)
        return;
    if (!feeder_thread) {
        ring = malloc(RING_BLOCKS * MUSIC_BYTES);
        bool memory = ring != NULL;
        for (int i = 0; i < MUSIC_BUFFERS; i++) {
            music_data[i] = linearAlloc(MUSIC_BYTES);
            memory = memory && music_data[i];
        }
        LightLock_Init(&ring_lock);
        s32 priority = 0x30;
        svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
        stream_exit = stream_wanted = false;
        // Above the main thread: both spend their time asleep or waiting for the SD.
        if (memory) {
            // Tremor keeps its decode buffers on the stack.
            reader_thread = threadCreate(music_reader, NULL, 64 * 1024, priority - 1, -2, false);
            feeder_thread = threadCreate(music_feeder, NULL, 8 * 1024, priority - 2, -2, false);
        }
        if (!reader_thread || !feeder_thread) {  // no music: both idle workers leave at once
            __atomic_store_n(&stream_exit, true, __ATOMIC_RELEASE);
            if (reader_thread) threadJoin(reader_thread, U64_MAX), threadFree(reader_thread), reader_thread = NULL;
            if (feeder_thread) threadJoin(feeder_thread, U64_MAX), threadFree(feeder_thread), feeder_thread = NULL;
            for (int i = 0; i < MUSIC_BUFFERS; i++) linearFree(music_data[i]), music_data[i] = NULL;
            free(ring);
            ring = NULL;
        }
    }
}

void audio_exit(void) {
    if (!ok)
        return;
    __atomic_store_n(&stream_exit, true, __ATOMIC_RELEASE);
    __atomic_store_n(&stream_wanted, false, __ATOMIC_RELEASE);
    // A reader stuck behind a busy SD card must not hold up closing the application.
    bool feeder = thread_reap(&feeder_thread), reader = thread_reap(&reader_thread), loader = thread_reap(&clip_thread);
    for (int ch = 0; ch < 2 + N_SFX; ch++)
        ndspChnWaveBufClear(ch);
    ndspExit();
    ok = false;
    if (!feeder || !reader || !loader)
        return;
    for (int i = 0; i < decoded_clips; i++) {
        linearFree(clips[i].data);
        free(clips[i].decoded);
        clips[i].data = clips[i].decoded = NULL;
    }
    decoded_clips = 0;
    for (int i = 0; i < MUSIC_BUFFERS; i++) linearFree(music_data[i]), music_data[i] = NULL;
    free(ring);
    ring = NULL;
    nclips = 0;
}

void audio_apply_settings(void) {
    if (!ok)
        return;
    // The master volume follows the setting; the intro sound is scaled back up so
    // it is always heard (requested behaviour).
    ndspSetMasterVol(1.0f);
    if (!S.music)
        music_target = 0;
}

void audio_intro(const char *name, float volume) { if(S.sfx) play(CH_INTRO, find(name), fminf(1,volume*S.volume*1.25f), RATE, false); }

void sfx_pitch(const char *name, float semitones) {
    if (!ok || !S.sfx)
        return;
    char n[24];
    snprintf(n, sizeof n, "sfx-%s", name);
    Clip *c = find(n);
    if (!c)
        return;
    int ch = CH_SFX + next_sfx;
    next_sfx = (next_sfx + 1) % N_SFX;
    // The export already includes the web's .9 effects bus.
    play(ch, c, master_now, RATE * powf(2, semitones / 12), false);
}

void sfx(const char *name) { sfx_pitch(name, 0); }

void music_play(float fade) {
    if (!ok || !S.music || !feeder_thread)
        return;
    if (wanted() && music_target > 0)
        return;
    if (!wanted()) {
        music_vol = 0;
        // Fresh start: drop any read-ahead and make the reader reopen the opening.
        LightLock_Lock(&ring_lock);
        ring_head = ring_tail = 0;
        __atomic_add_fetch(&stream_gen, 1, __ATOMIC_RELEASE);
        LightLock_Unlock(&ring_lock);
        __atomic_store_n(&streamed_frames, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&stream_underruns, 0, __ATOMIC_RELAXED);
        __atomic_store_n(&ring_low, RING_BLOCKS, __ATOMIC_RELAXED);
        __atomic_store_n(&stream_wanted, true, __ATOMIC_RELEASE);
    }
    music_target = 0.85f;
    music_speed = fade > 0 ? fabsf(music_target - music_vol) / fade : 100;
}

void music_stop(float fade) {
    music_target = 0;
    music_speed = fade > 0 ? fmaxf(music_vol, 0.01f) / fade : 100;
    if (music_vol <= 0) __atomic_store_n(&stream_wanted, false, __ATOMIC_RELEASE);
}

void music_muffle(float amount) { muffle_target = amount; }

void audio_update(void) {
    if (!ok)
        return;
    adopt_clip();
    if (music_vol != music_target) {
        float d = music_speed * DT;
        music_vol = music_vol < music_target ? fminf(music_target, music_vol + d) : fmaxf(music_target, music_vol - d);

        if (music_vol <= 0 && music_target <= 0)
            __atomic_store_n(&stream_wanted, false, __ATOMIC_RELEASE);
    }
    // Web Audio's master uses a 50 ms exponential smoothing constant.
    master_now = lerpf(master_now, S.volume, 1 - expf(-DT / .05f));
    duck_now = lerpf(duck_now, ducked ? 0 : 1, 1 - expf(-DT / .18f));
    set_vol(CH_MUSIC, music_vol * master_now * duck_now);
    for (int ch = CH_SFX; ch < CH_SFX + N_SFX; ch++) set_vol(ch, master_now);
    float hz = fminf(15000, 18000 * powf(1 - muffle_target, 3) + 900 * muffle_target);
    muffle_now = lerpf(muffle_now, hz, 1 - expf(-DT / .25f));
    if (fabsf(muffle_now - hz) < 20) muffle_now = hz;
    // Rewriting the filter every frame is audible on hardware. Fully open is a plain
    // bypass; while it moves, new coefficients go out in steps of 2 %.
    static float applied = -1;
    if (muffle_now >= 14500) {
        if (applied >= 0) ndspChnIirBiquadSetEnable(CH_MUSIC, false), applied = -1;
    } else if (applied < 0 || fabsf(muffle_now - applied) > applied * .02f || (muffle_now == hz && applied != hz)) {
        ndspChnIirBiquadSetParamsLowPassFilter(CH_MUSIC, muffle_now, .707f);
        if (applied < 0) ndspChnIirBiquadSetEnable(CH_MUSIC, true);
        applied = muffle_now;
    }
}

bool audio_available(void) { return ok; }
void music_duck(bool d) { ducked = d; }

// Only called by the opt-in scripted harness, never written on normal launches.
void audio_log(FILE *f) {
    float mix[12] = {0};
    if (ok) ndspChnGetMix(CH_MUSIC, mix);
    u32 level = 0;
    if (feeder_thread) {
        LightLock_Lock(&ring_lock);
        level = ring_head - ring_tail;
        LightLock_Unlock(&ring_lock);
    }
    fprintf(f, "audio volume=%.2f gain=%.4f cutoff=%.0f playing=%d queued=%.3fs underruns=%lu readahead=%lu/%d low=%lu\n",
            S.volume, mix[0], muffle_now, ok && ndspChnIsPlaying(CH_MUSIC),
            __atomic_load_n(&streamed_frames, __ATOMIC_RELAXED) / (float)RATE,
            __atomic_load_n(&stream_underruns, __ATOMIC_RELAXED), level, RING_BLOCKS,
            __atomic_load_n(&ring_low, __ATOMIC_RELAXED));
}
