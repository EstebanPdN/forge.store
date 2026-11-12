// Host test of the production preview decoder (source/video.c) on pthreads, with host
// libtheoradec / libjpeg-turbo, a simulated NDSP channel and real encoded previews.
// Fixtures (reference pictures decoded by ffmpeg, reference PCM and ADPCM vectors from the
// Python encoder) are produced by tests/run-video-test.py, which also builds and runs this.
#define VIDEO_HOST_TEST 1
#ifndef VIDEO_C
#define VIDEO_C "../source/video.c"
#endif
#include VIDEO_C

static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); abort(); } } while (0)
#define LOAD(x) __atomic_load_n(&(x), __ATOMIC_ACQUIRE)
static void pass(const char *name) { printf("PASS %s (%u checks)\n", name, checks); fflush(stdout); }

// ---- fixtures ----------------------------------------------------------------------------------
static u8 *slurp(const char *name, size_t *size) {
    FILE *f = fopen(name, "rb");
    if (!f) return NULL;
    CHECK(!fseek(f, 0, SEEK_END));
    *size = ftell(f);
    rewind(f);
    u8 *data = malloc(*size + 1);
    CHECK(data && fread(data, 1, *size, f) == *size);
    fclose(f);
    return data;
}
static void spit(const char *name, const u8 *data, size_t size) {
    FILE *f = fopen(name, "wb");
    CHECK(f && fwrite(data, 1, size, f) == size);
    CHECK(!fclose(f));
}
// Reference pictures: u32 count, u32 bottom flag, then per entry u32 frame index, 400x240 u16
// (row major), and 320x240 u16 when the stream has a bottom screen.
typedef struct { u8 *data; int count, max; bool bottom; const u16 **top, **bot; } Ref;
static Ref load_ref(const char *name) {
    Ref r = {0};
    size_t size;
    r.data = slurp(name, &size);
    CHECK(r.data);
    u32 count = le32(r.data);
    r.bottom = le32(r.data + 4);
    size_t entry = 4 + 400 * 240 * 2 + (r.bottom ? 320 * 240 * 2 : 0);
    CHECK(size == 8 + count * entry);
    for (u32 i = 0; i < count; i++) {
        int index = le32(r.data + 8 + i * entry);
        if (index > r.max) r.max = index;
    }
    r.top = calloc(r.max + 1, sizeof *r.top);
    r.bot = calloc(r.max + 1, sizeof *r.bot);
    for (u32 i = 0; i < count; i++) {
        const u8 *p = r.data + 8 + i * entry;
        int index = le32(p);
        r.top[index] = (const u16 *)(p + 4);
        if (r.bottom) r.bot[index] = (const u16 *)(p + 4 + 400 * 240 * 2);
    }
    r.count = count;
    return r;
}
static void free_ref(Ref *r) { free(r->data); free(r->top); free(r->bot); }
/** Undo the texture layout: 8x8 tiles in row order, Morton order inside. */
static void untile(const u16 *tex, int width, u16 *out) {
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < width; x++) {
            u32 m = (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
            out[y * width + x] = tex[(((y >> 3) * 64 + (x >> 3)) << 6) + m];
        }
}

// ---- driving the player --------------------------------------------------------------------------
static int shown[2] = {-1, -1}; // frame index of the picture each screen currently shows
static int consumed_last = -1;  // index of the last record handed to the player
static unsigned consumed, pictures[2], held, compared, unknown;
static const Ref *ref;
static u16 scratch[400 * 240];
static void compare_screen(int s) {
    if (!ref || shown[s] < 0 || shown[s] > ref->max) return;
    const u16 *want = s ? ref->bot[shown[s]] : ref->top[shown[s]];
    if (!want) return;
    int width = s ? 320 : 400;
    untile(tex[display_bank[s]][s].data, width, scratch);
    if (memcmp(scratch, want, width * 240 * 2)) {
        for (int i = 0; i < width * 240; i++)
            if (scratch[i] != want[i]) {
                fprintf(stderr, "screen %d frame %d differs at x=%d y=%d: got %04x want %04x (generation %u, clock %.3f)\n", s, shown[s], i % width,
                        i / width, scratch[i], want[i], (unsigned)generation, elapsed);
                break;
            }
        abort();
    }
    compared++;
}
/** One UI frame. In lockstep mode the worker is given time to keep ahead, so nothing is late. */
static void step(bool lockstep, double dt) {
    if (lockstep)
        for (int i = 0; i < 50000 && active && (!paused || seeking); i++) {
            if (LOAD(frames[read_slot].ready) || LOAD(decode_failed) || LOAD(ended_generation) == generation) break;
            usleep(100);
        }
    // A ready slot belongs to this thread until it is released, so it can be read here.
    Frame before[NSLOT];
    bool was_ready[NSLOT];
    for (int i = 0; i < NSLOT; i++) {
        was_ready[i] = LOAD(frames[i].ready);
        if (was_ready[i]) before[i] = frames[i];
    }
    int from = read_slot;
    u32 current = generation;
    DT = dt > 1 / 20.0 ? 1 / 20.f : (float)dt;
    host_dsp_tick(dt);
    video_update();
    for (int i = from, n = 0; n < NSLOT && (i != read_slot || n == 0) && active; i = (i + 1) % NSLOT, n++) {
        if (i == read_slot && n == 0) break; // nothing consumed
        if (!was_ready[i]) { // published between the snapshot and the update: index unknown
            unknown++;
            shown[0] = shown[1] = -1;
            continue;
        }
        if (before[i].generation != current) continue; // stale after a seek: drained, never shown
        consumed++;
        consumed_last = before[i].index;
        bool any = false;
        for (int s = 0; s < 2; s++)
            if (before[i].pic[s]) {
                shown[s] = before[i].index;
                pictures[s]++;
                any = true;
                compare_screen(s);
            }
        if (!any) held++;
    }
}
/** Injected decoder load (microseconds per packet / per conversion); read by the worker. */
static void load(int decode_us, int convert_us) {
    __atomic_store_n(&host_decode_delay_us, decode_us, __ATOMIC_RELAXED);
    __atomic_store_n(&host_convert_delay_us, convert_us, __ATOMIC_RELAXED);
}
static void reset_counts(void) {
    shown[0] = shown[1] = consumed_last = -1;
    consumed = pictures[0] = pictures[1] = held = compared = unknown = 0;
    free(host_dsp.pcm);
    memset(&host_dsp, 0, sizeof host_dsp);
    load(0, 0);
}
static void stop_and_reap(void) {
    video_stop();
    CHECK(!video_active());
    for (int i = 0; i < 20000 && video_stopping(); i++) { video_pump(); usleep(100); }
    CHECK(!video_stopping() && !thread && !host_textures && !linear_live && !LOAD(host_threads));
}

// ---- tests -------------------------------------------------------------------------------------
static void test_adpcm(void) {
    size_t size;
    u8 *data = slurp("adpcm.bin", &size);
    CHECK(data);
    size_t pos = 0;
    unsigned vectors = 0;
    while (pos < size) {
        u32 bytes = le32(data + pos), count = le32(data + pos + 4);
        const u8 *block = data + pos + 8;
        const s16 *want = (const s16 *)(block + bytes);
        s16 out[4096];
        u32 got = 0;
        CHECK(count <= 4096 && adpcm_decode(block, bytes, out, count, &got) && got == count); // an odd count ends on a low nibble
        CHECK(!memcmp(out, want, count * 2));
        // The limit truncates without touching the rest of the buffer.
        out[count / 2] = 0x1234;
        CHECK(adpcm_decode(block, bytes, out, count / 2, &got) && got == count / 2 && out[count / 2] == 0x1234);
        pos += 8 + bytes + count * 2;
        vectors++;
    }
    CHECK(pos == size && vectors >= 8);
    u8 bad[8] = {0, 0, 89, 0, 0x77, 0x77, 0x77, 0x77};
    s16 out[8];
    u32 got;
    CHECK(!adpcm_decode(bad, 8, out, 8, &got)); // step index out of range
    bad[2] = 0, bad[3] = 1;
    CHECK(!adpcm_decode(bad, 8, out, 8, &got)); // reserved byte
    bad[3] = 0;
    CHECK(!adpcm_decode(bad, 3, out, 8, &got)); // shorter than its header
    CHECK(adpcm_decode(bad, 4, out, 8, &got) && got == 0);
    free(data);
    pass("ADPCM decoder matches the Python encoder's reference bit for bit");
}

static void put32(u8 *p, u32 v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void put16(u8 *p, u32 v) { p[0] = v; p[1] = v >> 8; }
static bool header_ok(const u8 *data, size_t size) {
    spit("mutant.fvid", data, size);
    FILE *f = fopen("mutant.fvid", "rb");
    CHECK(f);
    Stream s;
    bool ok = read_header(f, &s);
    fclose(f);
    // The public entry point must agree and leave nothing allocated on rejection.
    bool started = video_start("mutant.fvid");
    CHECK(started == ok);
    if (!started) CHECK(!strcmp(video_error(), "Invalid video header") && !video_active() && !host_textures && !linear_live && !thread);
    else stop_and_reap();
    return ok;
}
static void test_headers(const char *clip, const char *trailer) {
    size_t size, tsize;
    u8 *good = slurp(clip, &size), *audio = slurp(trailer, &tsize);
    CHECK(good && audio && header_ok(good, size) && header_ok(audio, tsize));
    u8 *m = malloc(size + 16), *a = malloc(tsize + 16);
#define MUTATE(buffer, source, bytes, edit) do { memcpy(buffer, source, bytes); edit; CHECK(!header_ok(buffer, bytes)); } while (0)
    MUTATE(m, good, size, m[3] = '3');                          // magic
    MUTATE(m, good, size, put16(m + 4, 2));                     // version
    MUTATE(m, good, size, put16(m + 6, 4));                     // unknown flag
    MUTATE(m, good, size, put16(m + 6, 0));                     // bottom flag cleared, bottom size kept
    MUTATE(m, good, size, put32(m + 8, 0));                     // fps
    MUTATE(m, good, size, put32(m + 8, 61));
    MUTATE(m, good, size, put32(m + 12, 2));                    // fps denominator
    MUTATE(m, good, size, put32(m + 16, 0));                    // frames
    MUTATE(m, good, size, put32(m + 16, 0x7fffffff));
    MUTATE(m, good, size, put32(m + 16, le32(m + 16) + 1));     // frames disagree with the offsets
    MUTATE(m, good, size, put16(m + 20, 408));                  // top width
    MUTATE(m, good, size, put16(m + 22, 256));
    MUTATE(m, good, size, put16(m + 24, 400));                  // bottom width
    MUTATE(m, good, size, put32(m + 28, 24000));                // audio fields without the audio flag
    MUTATE(m, good, size, put32(m + 36, 1200));
    MUTATE(m, good, size, put32(m + 40, 0));                    // largest packet
    MUTATE(m, good, size, put32(m + 40, MAX_PACKET + 1));
    MUTATE(m, good, size, put32(m + 44, 8));                    // setup bytes
    MUTATE(m, good, size, put32(m + 44, 0x10000000));
    MUTATE(m, good, size, put32(m + 48, le32(m + 48) + 4));     // index offset
    MUTATE(m, good, size, put32(m + 52, le32(m + 52) - 4));     // data offset
    MUTATE(m, good, size, put32(m + 56, le32(m + 56) + 1));     // declared size
    MUTATE(m, good, size - 1, (void)0);                         // truncated download
    MUTATE(m, good, size + 1, m[size] = 0);                     // padded download
    MUTATE(m, good, 40, (void)0);                               // shorter than the header
    MUTATE(a, audio, tsize, put32(a + 28, 7999));               // audio rate
    MUTATE(a, audio, tsize, put32(a + 28, 48001));
    MUTATE(a, audio, tsize, put16(a + 32, 2));                  // channels
    MUTATE(a, audio, tsize, put16(a + 34, 2));                  // codec
    MUTATE(a, audio, tsize, put32(a + 36, 0));                  // samples per record
    MUTATE(a, audio, tsize, put32(a + 36, MAX_AUDIO_SAMPLES + 1));
    MUTATE(a, audio, tsize, put16(a + 6, 3));                   // bottom flag without a bottom size
    u8 legacy[20] = "FVID";
    put32(legacy + 4, 20), put32(legacy + 8, 10);
    CHECK(header_ok(legacy, 20) == true);                       // legacy header alone is accepted (the worker then fails cleanly)
    MUTATE(m, legacy, 20, put32(m + 4, 30));
    MUTATE(m, legacy, 20, put32(m + 8, 0));
    MUTATE(m, legacy, 20, put32(m + 8, 20000));
    MUTATE(m, legacy, 20, put32(m + 12, 2));
    MUTATE(m, legacy, 20, put32(m + 16, 2));
    MUTATE(m, legacy, 19, (void)0);
    host_path_ready = false;
    CHECK(!video_start(clip) && !strcmp(video_error(), "Video cache is not verified"));
    host_path_ready = true;
    CHECK(!video_start("missing.fvid") && !strcmp(video_error(), "Video file missing"));
    CHECK(!video_start(NULL));
    free(m), free(a), free(good), free(audio);
    remove("mutant.fvid");
    pass("header validation: 39 malformed headers rejected before any allocation");
}

static void test_clip(const char *clip, const char *refs) {
    Ref r = load_ref(refs);
    ref = &r;
    reset_counts();
    CHECK(video_start(clip) && video_active() && video_bottom() && !video_has_audio() && video_muted());
    int count = st.count;
    CHECK(r.bottom && r.count == count && video_duration() == count / 20.f && st.theora);
    video_draw(false, 0, 0, 400, 240, 1);
    CHECK(!host_draws); // nothing is drawn before the first decoded picture
    int previous = -1;
    for (int i = 0; i < 100000 && !video_paused(); i++) {
        step(true, 1 / 60.0);
        CHECK(!video_error()[0]);
        if (consumed_last != previous) { CHECK(consumed_last == previous + 1); previous = consumed_last; }
        if (consumed_last >= 0) {
            video_draw(false, 0, 0, 400, 240, 1);
            video_draw(true, 0, 0, 320, 240, 1);
            CHECK(host_drawn[0].tex == &tex[display_bank[0]][0] && host_drawn[1].tex == &tex[display_bank[1]][1]);
        }
    }
    // Every record reached the player in order and every picture matched ffmpeg's decode.
    CHECK(!unknown && previous == count - 1 && consumed == (unsigned)count && video_paused() && video_time() == video_duration());
    CHECK(compared == pictures[0] + pictures[1] && pictures[0] > 100 && pictures[1] > 50 && held > 0);
    CHECK(stat_late == 0 && stat_jumps == 0 && !host_dsp.samples);
    printf("  clip: %d records, %u top + %u bottom pictures identical to the reference, %u records reused both textures\n", count, pictures[0], pictures[1], held);
    // A at the end restarts from zero.
    video_pause();
    CHECK(!video_paused());
    for (int i = 0; i < 2000 && consumed_last != 30; i++) step(true, 1 / 60.0);
    CHECK(consumed_last == 30 && shown[0] <= 30 && video_time() > 1.4f && video_time() < 1.6f);
    stop_and_reap();
    ref = NULL;
    free_ref(&r);
    pass("split clip: full playback, duplicate frames, EOF, restart, release");
}

static void seek_and_check(int target, bool while_paused) {
    if (while_paused != video_paused()) video_pause();
    size_t before = host_dsp.samples;
    video_seek(target / 20.f + .001f);
    CHECK(seeking);
    for (int i = 0; i < 20000 && seeking; i++) step(true, 1 / 60.0);
    CHECK(!seeking && !video_error()[0] && shown[0] == target && consumed_last == target);
    CHECK(fabsf(video_time() - target / 20.f) < .01f);
    if (while_paused) {
        // Paused: exactly the target picture; the first sound blocks wait in the paused
        // channel and nothing more is queued until playback resumes.
        size_t waiting = host_dsp.samples;
        CHECK(host_dsp.paused && waiting <= before + NSLOT * st.samples);
        for (int i = 0; i < 30; i++) step(false, 1 / 60.0);
        CHECK(consumed_last == target && host_dsp.samples == waiting);
        video_pause();
    }
    for (int i = 0; i < 2000 && consumed_last < target + 6 && !video_paused(); i++) step(true, 1 / 60.0);
    CHECK(consumed_last == target + 6 || video_paused());
}
static void test_trailer(const char *trailer, const char *refs, const char *pcm_name, const char *seeks_name) {
    Ref r = load_ref(refs);
    size_t pcm_size, seeks_size;
    s16 *pcm = (s16 *)slurp(pcm_name, &pcm_size);
    u8 *seeks = slurp(seeks_name, &seeks_size);
    CHECK(pcm && seeks);
    ref = &r;
    reset_counts();
    CHECK(video_start(trailer) && video_has_audio() && !video_bottom() && !video_muted() && host_ducked);
    int count = st.count;
    CHECK(pcm_size == (size_t)count * st.samples * 2 && host_dsp.rate == 24000 && st.samples == 1200);
    for (int i = 0; i < 100000 && consumed_last < 199; i++) step(true, 1 / 60.0);
    // 200 records on time: every picture exact, the sound stream identical and gapless.
    CHECK(!unknown && consumed == 200 && compared == pictures[0] && pictures[0] > 150 && held > 0 && !stat_late && !stat_jumps);
    // Sound runs a few records ahead of the pictures, never behind.
    CHECK(host_dsp.samples >= 200 * 1200 && host_dsp.samples <= 204 * 1200 && host_dsp.samples % 1200 == 0);
    CHECK(!memcmp(host_dsp.pcm, pcm, host_dsp.samples * 2) && host_dsp.underrun < .001);
    printf("  trailer: first 200 records, %u pictures identical, %zu samples identical, %u duplicate frames held\n", pictures[0], host_dsp.samples, held);
    // Mute / pause.
    video_toggle_mute();
    CHECK(video_muted() && !host_ducked);
    step(true, 1 / 60.0);
    CHECK(host_dsp.mix == 0);
    video_toggle_mute();
    video_pause();
    CHECK(video_paused() && host_dsp.paused && host_ducked);
    int at = consumed_last;
    for (int i = 0; i < 60; i++) step(false, 1 / 60.0);
    CHECK(consumed_last == at);
    video_pause();
    // Seeks: keyframes, the frames around them, mid-GOP, duplicate runs, first and last.
    unsigned done = 0;
    for (size_t i = 0; i + 4 <= seeks_size; i += 4) {
        int target = le32(seeks + i);
        size_t before = host_dsp.samples;
        seek_and_check(target, i & 4);
        if (target + 12 < count) {
            // Sound restarts exactly at the target record (stored block state, no warm-up)
            // and continues without a gap.
            size_t fresh = host_dsp.samples - before;
            CHECK(fresh >= (size_t)(consumed_last - target + 1) * 1200 && fresh % 1200 == 0);
            CHECK(!memcmp(host_dsp.pcm + before, pcm + (size_t)target * 1200, fresh * 2));
        }
        done++;
    }
    CHECK(done >= 12 && !stat_late && !stat_jumps);
    if (video_paused()) video_pause(); // the last target ran into the end: A restarts from zero
    // Clamping, and requests that must be ignored.
    video_seek(-5);
    for (int i = 0; i < 20000 && seeking; i++) step(true, 1 / 60.0);
    CHECK(shown[0] == 0);
    video_seek(NAN);
    CHECK(!seeking);
    // A burst of seeks: only the last request is ever shown.
    for (int i = 0; i < 40; i++) {
        video_seek((i * 37 % 130) + .3f);
        if (i % 3 == 0) step(false, 1 / 60.0);
    }
    video_seek(100.f);
    for (int i = 0; i < 20000 && seeking; i++) step(true, 1 / 60.0);
    CHECK(shown[0] == 2000 && consumed_last == 2000);
    // Seek past the end: last frame, then EOF pauses and clamps the clock.
    video_seek(1e9f);
    for (int i = 0; i < 20000 && !video_paused(); i++) step(true, 1 / 60.0);
    CHECK(video_paused() && shown[0] <= count - 1 && consumed_last == count - 1 && video_time() == video_duration() && host_ducked);
    printf("  trailer: %u seek targets exact (%u pictures compared in total)\n", done, compared);
    stop_and_reap();
    CHECK(!host_ducked);
    ref = NULL;
    free_ref(&r);
    free(pcm), free(seeks);
    pass("trailer: playback, audio, pause/mute, seeking, burst seeks, EOF");
}

/** Plays in real time for `seconds` with injected decoder load; returns the clock's lag behind real time. */
static double realtime(double seconds) {
    double start = host_seconds(), last = start, now;
    while ((now = host_seconds()) - start < seconds && !video_paused()) {
        step(false, now - last);
        last = now;
        usleep(16000);
    }
    return (now - start) - video_time();
}
static void test_overload(const char *clip, const char *clip_refs, const char *trailer, const char *trailer_refs) {
    size_t pcm_size;
    s16 *pcm = (s16 *)slurp("trailer.pcm", &pcm_size);
    CHECK(pcm);
    // 1. Conversion is the bottleneck (decode 20 ms + convert 45 ms per record > 50 ms):
    //    late records skip the conversion, so the decoder keeps up without a keyframe jump.
    Ref r = load_ref(trailer_refs);
    ref = &r;
    reset_counts();
    CHECK(video_start(trailer));
    load(20000, 45000);
    double lag = realtime(8), played = video_time();
    CHECK(!video_error()[0] && !video_paused());
    stop_and_reap(); // the worker's counters are read once it has ended
    printf("  slow conversion: 8 s real time, clock lag %.3f s, %u conversions skipped, %u pictures shown (all exact), sound %.1f%% queued, %.3f s silence\n",
           lag, (unsigned)stat_late, pictures[0], 100 * host_dsp.samples / (played * 24000), host_dsp.underrun);
    CHECK(stat_late > 10 && stat_jumps == 0 && lag < .35 && compared > 40 && compared == pictures[0]);
    // Sound does not depend on the pictures: all of it, in order, without a gap.
    CHECK(host_dsp.samples >= (played - .06) * 24000 && host_dsp.underrun < .05 && !memcmp(host_dsp.pcm, pcm, host_dsp.samples * 2));
    // 2. Decoding itself is too slow (70 ms per packet): the decoder jumps to keyframes,
    //    records keep flowing, the clock holds real time, and no shown picture is corrupt.
    reset_counts();
    CHECK(video_start(trailer));
    load(70000, 0);
    lag = realtime(10), played = video_time();
    CHECK(!video_error()[0] && !video_paused());
    stop_and_reap();
    printf("  slow decode: 10 s real time, clock lag %.3f s, %u keyframe jumps, %u pictures shown (all exact), sound %.1f%% queued, %.3f s silence\n",
           lag, (unsigned)stat_jumps, pictures[0], 100 * host_dsp.samples / (played * 24000), host_dsp.underrun);
    CHECK(stat_jumps > 0 && lag < .8 && pictures[0] > 20 && compared == pictures[0]);
    CHECK(host_dsp.samples >= (played - .06) * 24000 && host_dsp.underrun < .05 && !memcmp(host_dsp.pcm, pcm, host_dsp.samples * 2));
    free_ref(&r);
    free(pcm);
    // 3. Same on the two-stream clip, to the end of the file.
    r = load_ref(clip_refs);
    ref = &r;
    reset_counts();
    CHECK(video_start(clip));
    load(40000, 0); // two packets per record
    lag = realtime(20);
    CHECK(video_paused() && video_time() == video_duration() && !video_error()[0]);
    stop_and_reap();
    printf("  slow decode, split clip: finished %.2f s late, %u jumps, %u+%u pictures shown (all exact)\n", lag, (unsigned)stat_jumps, pictures[0], pictures[1]);
    CHECK(stat_jumps > 0 && lag < 1.0 && compared > 20 && compared == pictures[0] + pictures[1]);
    ref = NULL;
    free_ref(&r);
    pass("overload: late frames skip conversion, hopeless lag jumps to keyframes, clock keeps real time");
}

static bool plays_or_fails(const u8 *data, size_t size, int updates) {
    spit("mutant.fvid", data, size);
    reset_counts();
    if (!video_start("mutant.fvid")) return false;
    for (int i = 0; i < updates && !video_error()[0]; i++) step(true, 1 / 60.0);
    bool failed = video_error()[0] != 0;
    if (failed) {
        CHECK(!strcmp(video_error(), "native.videoDecode") && video_paused() && !host_ducked);
        video_seek(1); // ignored after a decode failure
        CHECK(!seeking);
    }
    stop_and_reap();
    return !failed;
}
static void test_malformed(const char *clip, const char *trailer) {
    size_t size, tsize;
    u8 *good = slurp(clip, &size), *audio = slurp(trailer, &tsize);
    u8 *m = malloc(size), *a = malloc(tsize);
    u32 setup = le32(good + 44), index = le32(good + 48), data = le32(good + 52), frames = le32(good + 16);
    CHECK(plays_or_fails(good, size, 200));
#define BROKEN(buffer, source, bytes, edit) do { memcpy(buffer, source, bytes); edit; CHECK(!plays_or_fails(buffer, bytes, 4000)); } while (0)
    BROKEN(m, good, size, put32(m + 64, MAX_SETUP + 1));               // setup header length
    BROKEN(m, good, size, put32(m + 64, le32(m + 64) + 1));            // headers no longer fill the section
    BROKEN(m, good, size, m[64 + 4] = 0x81);                           // not an identification header
    BROKEN(m, good, size, memset(m + 64 + 4 + 60, 0xff, setup - 200)); // garbage codec setup
    BROKEN(m, good, size, m[64 + 4 + 10 + 1] ^= 1);                    // Theora frame width
    BROKEN(m, good, size, put32(m + index, le32(m + index) & ~KEY_TOP));        // record 0 must be a keyframe
    BROKEN(m, good, size, put32(m + index, le32(m + index) + 4));               // first offset
    BROKEN(m, good, size, put32(m + index + 40, (le32(m + index + 40) & ~OFFSET_MASK) | 5));          // offset going backwards
    BROKEN(m, good, size, put32(m + index + 40, (le32(m + index + 40) & ~OFFSET_MASK) | OFFSET_MASK)); // offset past the end
    BROKEN(m, good, size, put32(m + index + 4 * (frames - 1), le32(m + index + 4 * (frames - 1)) + 8)); // last record too short
    u32 third = le32(good + index + 8) & OFFSET_MASK;
    BROKEN(m, good, size, put32(m + third, le32(m + third) + 1));      // top length
    BROKEN(m, good, size, put32(m + third, 0x7fffffff));
    BROKEN(m, good, size, put32(m + third + 4, 0xffffffff));           // bottom length
    BROKEN(m, good, size, put32(m + third + 8, 16));                   // audio in a silent stream
    BROKEN(m, good, size, m[third + 12] |= 0x80);                      // header packet among the data
    BROKEN(m, good, size, put32(m + data, 0));                         // first packet empty: nothing to show, then lengths disagree
    u32 taudio = le32(audio + 52) + 12;
    BROKEN(a, audio, tsize, a[taudio + 2] = 89);                       // ADPCM step index
    BROKEN(a, audio, tsize, a[taudio + 3] = 1);                        // ADPCM reserved byte
    BROKEN(a, audio, tsize, put32(a + le32(a + 52) + 8, 4));           // audio block with no samples
    BROKEN(a, audio, tsize, put32(a + le32(a + 52) + 4, 9));           // bottom packet in a single-screen stream
    // Corrupt packet payloads: libtheora either reports them or decodes noise; never a crash,
    // and the player either fails cleanly or keeps going.
    unsigned survived = 0;
    for (int round = 0; round < 12; round++) {
        memcpy(m, good, size);
        u32 offset = le32(m + index + 4 * (round * 3 + 1)) & OFFSET_MASK, length = le32(m + offset);
        for (u32 i = 1 + round % 3; i < length; i += 1 + round) m[offset + 12 + i] ^= 0x55 + round;
        survived += plays_or_fails(m, size, 400);
    }
    printf("  12 streams with corrupted packet payloads: %u played on, %u reported a decode error, none crashed\n", survived, 12 - survived);
    free(m), free(a), free(good), free(audio);
    remove("mutant.fvid");
    pass("malformed streams fail cleanly in the worker");
}

static void test_lifecycle(const char *clip, const char *trailer) {
    reset_counts();
    // Stop is asynchronous and a start during it is queued, replaced, or cancelled.
    CHECK(video_start(trailer));
    for (int i = 0; i < 200 && consumed_last < 5; i++) step(true, 1 / 60.0);
    load(150000, 0); // the worker is busy for a while when asked to stop
    usleep(20000);
    video_stop();
    CHECK(video_stopping() && !video_active() && video_duration() == 0 && video_time() == 0);
    video_pump();
    CHECK(video_stopping() && host_textures == 2); // still owned by the running decoder
    CHECK(video_start(trailer) && !video_active() && video_stopping()); // queued
    CHECK(video_start(clip) && !video_active());                         // replaces the queued start
    load(0, 0);
    for (int i = 0; i < 20000 && !video_active(); i++) { video_pump(); usleep(100); }
    CHECK(video_active() && video_bottom() && host_textures == 4);
    for (int i = 0; i < 400 && consumed_last < 3; i++) step(true, 1 / 60.0);
    CHECK(consumed_last >= 3);
    load(100000, 0);
    usleep(20000);
    CHECK(video_start(trailer));
    video_stop(); // cancels the queued start
    load(0, 0);
    for (int i = 0; i < 20000 && video_stopping(); i++) { video_pump(); usleep(100); }
    CHECK(!video_active() && !video_stopping() && !host_textures && !linear_live);
    // Without audio hardware nothing touches the channel.
    host_audio = false;
    reset_counts();
    CHECK(video_start(trailer));
    for (int i = 0; i < 400 && consumed_last < 20; i++) step(true, 1 / 60.0);
    CHECK(consumed_last == 20 && !host_dsp.buffers && !host_dsp.rate);
    stop_and_reap();
    host_audio = true;
    // Both sound settings off: starts muted, still plays.
    S.music = S.sfx = false;
    CHECK(video_start(trailer) && video_muted() && !host_ducked);
    stop_and_reap();
    S.music = S.sfx = true;
    // Final teardown with a healthy decoder joins and frees.
    CHECK(video_start(clip));
    for (int i = 0; i < 400 && consumed_last < 3; i++) step(true, 1 / 60.0);
    video_exit();
    CHECK(!thread && !host_textures && !linear_live && !LOAD(host_threads));
    // Final teardown with a decoder stuck for 3 s: returns after about 2 s and frees nothing
    // the decoder may still use.
    reset_counts();
    CHECK(video_start(trailer));
    for (int i = 0; i < 400 && consumed_last < 3; i++) step(true, 1 / 60.0);
    load(3000000, 0);
    usleep(100000);
    double start = host_seconds();
    video_exit();
    double waited = host_seconds() - start;
    CHECK(waited > 1.8 && waited < 2.5 && thread && host_textures == 2 && linear_live == NSLOT && frames[0].top && sounds[0].pcm);
    printf("  video_exit with a stuck decoder returned after %.2f s, resources left untouched\n", waited);
    // (The process would exit here. Let the decoder finish so the sanitizers see a clean end.)
    load(0, 0);
    CHECK(reap(U64_MAX));
    release_resources();
    CHECK(!host_textures && !linear_live);
    pass("lifecycle: async stop, queued/replaced/cancelled start, no audio, bounded final join");
}

static void test_legacy(const char *file, const char *refs) {
    Ref r = load_ref(refs);
    reset_counts();
    CHECK(video_start(file) && !st.theora && st.samples == LEGACY_SAMPLES && video_bottom() == r.bottom);
    int previous = -1;
    double worst = 99;
    for (int i = 0; i < 20000 && consumed_last < r.max; i++) {
        step(true, 1 / 60.0);
        if (consumed_last == previous) continue;
        CHECK(consumed_last == previous + 1);
        previous = consumed_last;
        for (int s = 0; s < (r.bottom ? 2 : 1); s++) {
            // libjpeg's fast IDCT / plain upsampling is not bit-identical to the reference decode.
            int width = s ? 320 : 400;
            const u16 *want = s ? r.bot[previous] : r.top[previous];
            untile(tex[display_bank[s]][s].data, width, scratch);
            double error = 0;
            for (int p = 0; p < width * 240; p++) {
                int a = scratch[p], b = want[p];
                int dr = ((a >> 11) - (b >> 11)) * 8, dg = (((a >> 5) & 63) - ((b >> 5) & 63)) * 4, db = ((a & 31) - (b & 31)) * 8;
                error += dr * dr + dg * dg + db * db;
            }
            double psnr = 10 * log10(255.0 * 255 * 3 * width * 240 / (error + 1));
            if (psnr < worst) worst = psnr;
        }
    }
    CHECK(previous == r.max && worst > 30);
    printf("  legacy FVID: %d frames in order, worst PSNR against the reference JPEG decode %.1f dB\n", previous + 1, worst);
    video_seek(4);
    for (int i = 0; i < 20000 && seeking; i++) step(true, 1 / 60.0);
    CHECK(consumed_last == 80 && !video_error()[0]);
    stop_and_reap();
    free_ref(&r);
    pass("legacy MJPEG stream still plays and seeks");
}

/** Weighted sum of a picture's RGB565 words: the fingerprint run-video-test.py --all stores. */
static u64 fingerprint(const u16 *pixels, int count) {
    u64 sum = 0;
    for (int i = 0; i < count; i++) sum += pixels[i] * (((u64)i * 0x9E3779B97F4A7C15ull + 0x1234567) | 1);
    return sum;
}
/** Plays one whole stream and checks every picture it shows against ffmpeg's decode. */
static void test_play(const char *stem) {
    char name[256];
    size_t size;
    snprintf(name, sizeof name, "%s.hashes", stem);
    u8 *hashes = slurp(name, &size);
    CHECK(hashes);
    u32 count = le32(hashes), streams = le32(hashes + 4);
    CHECK(size == 8 + (size_t)count * streams * 8);
    snprintf(name, sizeof name, "%s.fvid", stem);
    reset_counts();
    CHECK(video_start(name) && st.count == count && (st.bottom ? 2u : 1u) == streams);
    unsigned checked = 0;
    int previous = -1, last[2] = {-1, -1};
    for (int i = 0; i < 2000000 && !video_paused(); i++) {
        step(true, 1 / 60.0);
        CHECK(!video_error()[0]);
        if (consumed_last != previous) { CHECK(consumed_last == previous + 1); previous = consumed_last; }
        for (u32 s = 0; s < streams; s++) {
            if (shown[s] == last[s]) continue;
            last[s] = shown[s];
            int width = s ? 320 : 400;
            untile(tex[display_bank[s]][s].data, width, scratch);
            u64 want;
            memcpy(&want, hashes + 8 + ((size_t)shown[s] * streams + s) * 8, 8);
            if (fingerprint(scratch, width * 240) != want) { fprintf(stderr, "%s: screen %u frame %d differs\n", stem, s, shown[s]); abort(); }
            checked++;
        }
    }
    CHECK(!unknown && previous == (int)count - 1 && video_time() == video_duration() && checked == pictures[0] + pictures[1]);
    printf("PASS %s: %u records, %u pictures identical to ffmpeg's decode, %u duplicate records, %zu sound samples\n", stem, count, checked, held, host_dsp.samples);
    stop_and_reap();
    free(hashes);
}

int main(int argc, char **argv) {
    CHECK(argc >= 2 && !chdir(argv[1]));
    if (argc == 4 && !strcmp(argv[2], "play")) {
        test_play(argv[3]);
        free(host_dsp.pcm);
        return 0;
    }
    CHECK(argc == 2);
    test_adpcm();
    test_headers("clip.fvid", "trailer.fvid");
    test_clip("clip.fvid", "clip.ref");
    test_trailer("trailer.fvid", "trailer.ref", "trailer.pcm", "trailer.seeks");
    test_malformed("clip.fvid", "trailer.fvid");
    test_lifecycle("clip.fvid", "trailer.fvid");
    test_overload("clip.fvid", "clip.ref", "trailer.fvid", "trailer.ref");
    if (!access("legacy.fvid", R_OK)) test_legacy("legacy.fvid", "legacy.ref");
    else printf("SKIP legacy MJPEG stream (no 3ds/media/doom-forest.fvid)\n");
    free(host_dsp.pcm);
    printf("PASS video: %u checks\n", checks);
    return 0;
}
