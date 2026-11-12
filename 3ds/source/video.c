// Local preview streams. Decode/SD reads off-thread; bounded buffers and
// double-buffered GPU textures keep the UI independent of video frame rate.
//   FVD2  Theora video + IMA ADPCM audio (tools/pack-video.py documents the layout)
//   FVID  MJPEG + PCM16, the first format; still played from existing caches
#ifdef VIDEO_HOST_TEST
#include "../tests/video_host_platform.h"
#else
#include "video.h"
#include "audio.h"
#include "media.h"
#endif
#include <jpeglib.h>
#include <setjmp.h>
#include <theora/theoradec.h>
#define NSLOT 4
#define PIXELS (512 * 256)
#define VCH 10
#define LEGACY_RATE 32000
#define LEGACY_SAMPLES 1600
#define MAX_FRAMES 100000
#define MAX_PACKET (256 * 1024)
#define MAX_SETUP (64 * 1024)
#define MAX_AUDIO_SAMPLES 4096
#define OFFSET_MASK 0x3fffffffu
#define KEY_TOP 0x80000000u
#define KEY_BOTTOM 0x40000000u
// Theora catch-up, in frames: a frame whose slot has already passed is decoded but not
// converted (at most MAX_SKIP in a row, so the picture keeps moving); further behind than
// JUMP_LAG the decoder skips ahead to the next keyframe. Sound is not affected by either.
#define MAX_SKIP 3
#define JUMP_LAG 10
// Theora sound travels in its own ring, ahead of the pictures: a block is dropped only if it
// would start more than this late against the playback clock.
#define NSOUND 8
#define AUDIO_LATE .15f
// Non-zero: on an Old 3DS, run the Theora decoder on the system core (core 1), allowed
// this percentage of it (1 means 60). Not enabled: it changes how the whole app shares
// the console and needs a hardware test first.
#ifndef FORGE_VIDEO_SYSCORE
#define FORGE_VIDEO_SYSCORE 0
#endif

typedef struct {
    volatile bool ready;
    bool pic[2]; // which screens carry a new picture; false keeps the current texture
    int index;
    u32 generation;
    u16 *top, *bot;
    s16 *pcm;
    u32 samples;
} Frame;
// Theora streams: sound blocks decoded ahead of the pictures, so a slow picture never
// starves NDSP. (MJPEG streams keep their PCM in the frame slot.)
typedef struct {
    volatile bool ready;
    int index;
    u32 generation;
    s16 *pcm;
    u32 samples;
} Sound;
// Immutable from video_start until the decoder has been reaped.
typedef struct {
    bool theora, bottom, audio;
    u32 fps, count, rate, samples; // samples: audio samples in one frame record (buffer size)
    u32 max_packet, setup_bytes, index_offset, data_offset, file_bytes;
} Stream;
static Frame frames[NSLOT];
static Sound sounds[NSOUND];
static bool sound_on; // this session feeds NDSP from the sound ring
static C3D_Tex tex[2][2];
static bool tex_live[2][2];
static Tex3DS_SubTexture sub[2] = {{400, 240, 0, 1, 400.f / 512, 1 - 240.f / 256}, {320, 240, 0, 1, 320.f / 512, 1 - 240.f / 256}};
static ndspWaveBuf waves[NSLOT];
static s16 *audio_mem[NSLOT];
static Thread thread;
static bool stop, decode_failed;
static u32 ended_generation;
// The main thread publishes seek requests under this short lock. Buffer ownership
// still moves exclusively via ready: seeks never clear a slot being decoded.
static LightLock seek_lock;
static int requested_frame;
static u32 generation;
static int clock_frame; // playback position in frames, for the Theora worker's catch-up
static bool active, paused, have_frame, muted, seeking;
static Stream st;
static int display_bank[2], read_slot, audio_slot, sound_slot;
static float elapsed;
static char path[256], error[96], pending_path[256];
static bool release_pending;
// Worker counters (decode statistics; the host test and FORGE_VIDEO_TRACE read them).
static u32 stat_decoded, stat_converted, stat_late, stat_jumps;
// -DFORGE_VIDEO_TRACE='"sdmc:/3ds/forge-store/video-trace.txt"' appends one line per
// playback with the counters and the time spent in Theora decoding and colour conversion:
// the way to measure a real console. Off by default.
#ifdef FORGE_VIDEO_TRACE
static u64 trace_ticks[2], trace_peak[2]; // 0 decode a packet, 1 convert a picture
#define TRACE_BEGIN() u64 trace_from = svcGetSystemTick()
#define TRACE_END(i)                                    \
    do {                                                \
        u64 spent = svcGetSystemTick() - trace_from;    \
        trace_ticks[i] += spent;                        \
        if (spent > trace_peak[i]) trace_peak[i] = spent; \
    } while (0)
#else
#define TRACE_BEGIN() ((void)0)
#define TRACE_END(i) ((void)0)
#endif

// ---- container headers ------------------------------------------------------------------------
static u32 le32(const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
static u32 le16(const u8 *p) { return p[0] | p[1] << 8; }
static bool read_header(FILE *f, Stream *s) {
    u8 h[64];
    memset(s, 0, sizeof *s);
    if (fread(h, 1, 20, f) != 20)
        return false;
    if (!memcmp(h, "FVID", 4)) {
        s->fps = le32(h + 4);
        s->count = le32(h + 8);
        u32 bottom = le32(h + 12), audio = le32(h + 16);
        s->bottom = bottom != 0;
        s->audio = audio != 0;
        s->rate = LEGACY_RATE;
        s->samples = LEGACY_SAMPLES;
        return s->fps == 20 && s->count > 0 && s->count < 20000 && bottom <= 1 && audio <= 1;
    }
    if (memcmp(h, "FVD2", 4) || fread(h + 20, 1, 44, f) != 44)
        return false;
    u32 flags = le16(h + 6), fps_den = le32(h + 12), channels = le16(h + 32), codec = le16(h + 34);
    s->theora = true;
    s->bottom = flags & 1;
    s->audio = (flags & 2) != 0;
    s->fps = le32(h + 8);
    s->count = le32(h + 16);
    s->rate = le32(h + 28);
    s->samples = le32(h + 36);
    s->max_packet = le32(h + 40);
    s->setup_bytes = le32(h + 44);
    s->index_offset = le32(h + 48);
    s->data_offset = le32(h + 52);
    s->file_bytes = le32(h + 56);
    if (le16(h + 4) != 1 || flags > 3 || fps_den != 1 || s->fps < 1 || s->fps > 60 || !s->count || s->count > MAX_FRAMES)
        return false;
    // The textures and sub-rectangles are fixed: only whole 3DS screens are accepted.
    if (le16(h + 20) != 400 || le16(h + 22) != 240 || le16(h + 24) != (s->bottom ? 320 : 0) || le16(h + 26) != (s->bottom ? 240 : 0))
        return false;
    if (s->audio ? (s->rate < 8000 || s->rate > 48000 || channels != 1 || codec != 1 || !s->samples || s->samples > MAX_AUDIO_SAMPLES)
                 : (s->rate || channels || codec || s->samples))
        return false;
    u32 streams = s->bottom ? 2 : 1;
    if (!s->max_packet || s->max_packet > MAX_PACKET || s->setup_bytes < streams * 12 || s->setup_bytes > streams * 3 * (MAX_SETUP + 4))
        return false;
    if (s->index_offset != 64 + s->setup_bytes || s->data_offset != s->index_offset + 4 * s->count ||
        s->file_bytes <= s->data_offset || s->file_bytes > OFFSET_MASK || le32(h + 60))
        return false;
    // A truncated or padded download never reaches the decoder.
    return !fseek(f, 0, SEEK_END) && ftell(f) == (long)s->file_bytes;
}

// ---- MJPEG (FVID) --------------------------------------------------------------------------------
typedef struct {
    struct jpeg_error_mgr pub;
    jmp_buf jump;
} JError;
static void jpeg_failed(j_common_ptr c) {
    JError *e = (JError *)c->err;
    longjmp(e->jump, 1);
}
static bool decode_jpeg(const u8 *data, size_t size, u16 *dst, int width) {
    struct jpeg_decompress_struct c = {0};
    JError e;
    c.err = jpeg_std_error(&e.pub);
    e.pub.error_exit = jpeg_failed;
    if (setjmp(e.jump)) {
        jpeg_destroy_decompress(&c);
        return false;
    }
    jpeg_create_decompress(&c);
    jpeg_mem_src(&c, data, size);
    jpeg_read_header(&c, TRUE);
    if (c.image_width != (unsigned)width || c.image_height != 240) {
        jpeg_destroy_decompress(&c);
        return false;
    }
    c.out_color_space = JCS_RGB565;
    c.dither_mode = JDITHER_NONE;
    c.dct_method = JDCT_IFAST;
    c.do_fancy_upsampling = FALSE;
    jpeg_start_decompress(&c);
    u16 row[400];
    while (c.output_scanline < 240) {
        u32 y = c.output_scanline;
        JSAMPROW p = (u8 *)row;
        jpeg_read_scanlines(&c, &p, 1);
        for (u32 x = 0; x < (u32)width; x++) {
            u32 m = (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
            dst[(((y >> 3) * 64 + (x >> 3)) << 6) + m] = row[x];
        }
    }
    jpeg_finish_decompress(&c);
    jpeg_destroy_decompress(&c);
    return true;
}
static bool mjpeg_worker(FILE *f) {
    u32 *offsets = malloc(st.count * 4);
    u8 *jpeg = malloc(256 * 1024);
    bool ok = false;
    if (!offsets || !jpeg)
        goto done;
    if (fseek(f, 20, SEEK_SET))
        goto done;
    if (fread(offsets, 4, st.count, f) != st.count)
        goto done;
    int index = 0, slot = 0;
    u32 worker_generation = 0;
    while (!__atomic_load_n(&stop, __ATOMIC_ACQUIRE)) {
#ifndef VIDEO_HOST_TEST
        bg_wait();
        if (__atomic_load_n(&stop, __ATOMIC_ACQUIRE)) break;
#endif
        LightLock_Lock(&seek_lock);
        if (worker_generation != generation) {
            worker_generation = generation;
            index = requested_frame;
        }
        LightLock_Unlock(&seek_lock);
        if (index >= (int)st.count) {
            __atomic_store_n(&ended_generation, worker_generation, __ATOMIC_RELEASE);
            svcSleepThread(2000000);
            continue;
        }
        Frame *fr = &frames[slot];
        if (__atomic_load_n(&fr->ready, __ATOMIC_ACQUIRE)) {
            svcSleepThread(1000000);
            continue;
        }
        if (fseek(f, offsets[index], SEEK_SET))
            goto done;
        u32 n[3];
        if (fread(n, 4, 3, f) != 3 || n[0] > 256 * 1024 || n[1] > 256 * 1024 || n[2] > LEGACY_SAMPLES * 2 || (n[2] & 1) ||
            (!st.bottom && n[1]) || (st.bottom && !n[1]) || (!st.audio && n[2]))
            goto done;
        if (fread(jpeg, 1, n[0], f) != n[0] || !decode_jpeg(jpeg, n[0], fr->top, 400))
            goto done;
        if (n[1] && (fread(jpeg, 1, n[1], f) != n[1] || !decode_jpeg(jpeg, n[1], fr->bot, 320)))
            goto done;
        if (n[2] && fread(fr->pcm, 1, n[2], f) != n[2])
            goto done;
        fr->samples = n[2] / 2;
        fr->pic[0] = true;
        fr->pic[1] = st.bottom;
        fr->index = index++;
        fr->generation = worker_generation;
        stat_decoded++;
        stat_converted++;
        __atomic_store_n(&fr->ready, true, __ATOMIC_RELEASE);
        slot = (slot + 1) % NSLOT;
    }
    ok = true;
done:
    free(offsets);
    free(jpeg);
    return ok;
}

// ---- Theora (FVD2) -------------------------------------------------------------------------------
#if defined(__3DS__) && !defined(FORGE_THEORA_GENERIC_ARM)
// devkitPro's libtheoradec reports an ARMv5E CPU, which leaves its ARMv6 IDCT, loop
// filter and reconstruction routines unused. The 3DS ARM11 MPCore is ARMv6K, so answer
// for the library: this definition is linked in place of its armcpu.o. Flag values are
// libtheora's OC_CPU_ARM_EDSP and OC_CPU_ARM_MEDIA. Build with -DFORGE_THEORA_GENERIC_ARM
// to return to the library's own answer.
ogg_uint32_t oc_cpu_flags_get(void);
ogg_uint32_t oc_cpu_flags_get(void) { return 0x81 | 0x1000000; }
#endif

// BT.601 limited range -> RGB565 through three clamp tables. The chroma terms are
// scaled into luma units first, so a pixel costs one luma load and three lookups:
//   index = Y + offset(Cb, Cr);  lut[index] = clamp(1.164383 * (index - 16)) as 5/6/5 bits
// Tables hold 32-bit words (ARM scales a word index for free) and the chroma offsets are
// kept as ready-made pointers into them.
#define LUT_BIAS 232
#define LUT_SIZE (LUT_BIAS + 256 + 232)
static u32 lut_r[LUT_SIZE], lut_g[LUT_SIZE], lut_b[LUT_SIZE];
static const u32 *ptr_rv[256], *ptr_gv[256], *ptr_bu[256];
static s32 off_gu[256];
static bool lut_ready;
static void lut_init(void) {
    if (lut_ready)
        return;
    for (int i = 0; i < LUT_SIZE; i++) {
        int v = ((i - LUT_BIAS - 16) * 76309 + 32768) >> 16;
        v = v < 0 ? 0 : v > 255 ? 255 : v;
        lut_r[i] = (v >> 3) << 11;
        lut_g[i] = (v >> 2) << 5;
        lut_b[i] = v >> 3;
    }
    for (int c = 0; c < 256; c++) {
        int d = c - 128;
        ptr_rv[c] = lut_r + LUT_BIAS + ((d * 89831 + 32768) >> 16);  // 1.596027 / 1.164383
        ptr_gv[c] = lut_g + LUT_BIAS - ((d * 45757 + 32768) >> 16);  // 0.812968 / 1.164383
        off_gu[c] = -((d * 22050 + 32768) >> 16);                    // 0.391762 / 1.164383
        ptr_bu[c] = lut_b + LUT_BIAS + ((d * 113538 + 32768) >> 16); // 2.017232 / 1.164383
    }
    lut_ready = true;
}
// One 2x2 luma block shares a chroma sample, and the texture's Morton order visits a
// tile exactly as 2x2 blocks, so the output is written strictly sequentially.
#define BLOCK(cx, cy)                                                                 \
    do {                                                                              \
        int u = uq[(cy) * cs + (cx)], v = vq[(cy) * cs + (cx)];                       \
        const u32 *r = ptr_rv[v], *g = ptr_gv[v] + off_gu[u], *b = ptr_bu[u];         \
        const u8 *l0 = yq + 2 * (cy) * ys + 2 * (cx), *l1 = l0 + ys;                  \
        int a = l0[0], c = l0[1], d = l1[0], e = l1[1];                               \
        o[0] = r[a] | g[a] | b[a];                                                    \
        o[1] = r[c] | g[c] | b[c];                                                    \
        o[2] = r[d] | g[d] | b[d];                                                    \
        o[3] = r[e] | g[e] | b[e];                                                    \
        o += 4;                                                                       \
    } while (0)
/** 4:2:0 planes to the 512x256 RGB565 texture layout (8x8 tiles, Morton order inside).
 *  The body is kept to one 4x4 quad (four blocks) per pass so it stays small in the
 *  instruction cache: a tile is four quads, again in Morton order. */
static void convert_tiles(th_img_plane *plane, u16 *dst, int width) {
    const int ys = plane[0].stride, cs = plane[1].stride, tiles = width >> 3;
    const int luma[4] = {0, 4, 4 * ys, 4 * ys + 4}, chroma[4] = {0, 2, 2 * cs, 2 * cs + 2};
    for (int ty = 0; ty < 30; ty++) {
        const u8 *y0 = plane[0].data + ty * 8 * ys, *u0 = plane[1].data + ty * 4 * cs, *v0 = plane[2].data + ty * 4 * cs;
        u16 *o = dst + ty * 64 * 64;
        for (int tx = 0; tx < tiles; tx++, y0 += 8, u0 += 4, v0 += 4)
            for (int q = 0; q < 4; q++) {
                const u8 *yq = y0 + luma[q], *uq = u0 + chroma[q], *vq = v0 + chroma[q];
                BLOCK(0, 0);
                BLOCK(1, 0);
                BLOCK(0, 1);
                BLOCK(1, 1);
            }
    }
}

static const s16 ima_step[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60,
    66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
    724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
    4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
    27086, 29794, 32767};
static const s8 ima_index[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
/** One record's block: s16 predictor, u8 step index, u8 zero, then 4-bit codes (low nibble first). */
static bool adpcm_decode(const u8 *block, u32 bytes, s16 *out, u32 limit, u32 *samples) {
    if (bytes < 4 || block[2] > 88 || block[3])
        return false;
    int predictor = (s16)le16(block), index = block[2];
    u32 n = (bytes - 4) * 2;
    if (n > limit)
        n = limit;
    for (u32 i = 0; i < n; i++) {
        int code = i & 1 ? block[4 + (i >> 1)] >> 4 : block[4 + (i >> 1)] & 15;
        int step = ima_step[index], delta = step >> 3;
        if (code & 4) delta += step;
        if (code & 2) delta += step >> 1;
        if (code & 1) delta += step >> 2;
        predictor += code & 8 ? -delta : delta;
        predictor = predictor < -32768 ? -32768 : predictor > 32767 ? 32767 : predictor;
        index += ima_index[code & 7];
        index = index < 0 ? 0 : index > 88 ? 88 : index;
        out[i] = predictor;
    }
    *samples = n;
    return true;
}

/** Reads one stream's three Theora headers from the setup section and builds its decoder. */
static th_dec_ctx *theora_open(FILE *f, u32 *remaining, int width) {
    th_info info;
    th_comment comment;
    th_setup_info *setup = NULL;
    th_dec_ctx *dec = NULL;
    th_info_init(&info);
    th_comment_init(&comment);
    bool ok = true;
    for (int i = 0; i < 3 && ok; i++) {
        u8 size[4];
        ok = *remaining >= 4 && fread(size, 1, 4, f) == 4;
        u32 n = ok ? le32(size) : 0;
        ok = ok && n > 0 && n <= MAX_SETUP && n <= *remaining - 4;
        u8 *data = ok ? malloc(n) : NULL;
        ok = data && fread(data, 1, n, f) == n;
        if (ok) {
            *remaining -= 4 + n;
            ogg_packet packet = {.packet = data, .bytes = n, .b_o_s = i == 0, .packetno = i};
            ok = th_decode_headerin(&info, &comment, &setup, &packet) > 0;
        }
        free(data);
    }
    ok = ok && setup && info.frame_width == (unsigned)width && info.frame_height == 240 && info.pic_width == (unsigned)width &&
         info.pic_height == 240 && !info.pic_x && !info.pic_y && info.pixel_fmt == TH_PF_420;
    if (ok)
        dec = th_decode_alloc(&info, setup);
    th_setup_free(setup);
    th_comment_clear(&comment);
    th_info_clear(&info);
    return dec;
}
/** Last record at or before `from` that starts a keyframe of the stream (record 0 always does). */
static int key_before(const u32 *index, int from, u32 flag) {
    while (from > 0 && !(index[from] & flag))
        from--;
    return from;
}
/** First keyframe record at or after `from`, or the frame count when there is none left. */
static int key_after(const u32 *index, int from, u32 flag) {
    while (from < (int)st.count && !(index[from] & flag))
        from++;
    return from;
}
static bool theora_worker(FILE *f) {
    static const u32 key_flag[2] = {KEY_TOP, KEY_BOTTOM};
    const int streams = st.bottom ? 2 : 1, count = (int)st.count;
    const u32 audio_bytes = st.audio ? 4 + (st.samples + 1) / 2 : 0;
    u32 *index = malloc(st.count * 4);
    u8 *packet = malloc(st.max_packet), *audio = malloc(audio_bytes + 1);
    th_dec_ctx *dec[2] = {NULL, NULL};
    // Sound is read through its own handle: it runs ahead of the pictures and is not
    // affected by keyframe jumps. Both handles read the file strictly forwards (the
    // packets between two sound blocks are read and discarded), seeking only on a jump.
    FILE *sound_file = sound_on ? fopen(path, "rb") : NULL;
    bool ok = false;
    if (!index || !packet || !audio || (sound_on && !sound_file))
        goto done;
    setvbuf(f, NULL, _IOFBF, 16 * 1024);
    if (sound_file)
        setvbuf(sound_file, NULL, _IOFBF, 8 * 1024);
    u32 remaining = st.setup_bytes;
    if (fseek(f, 64, SEEK_SET))
        goto done;
    for (int s = 0; s < streams; s++)
        if (!(dec[s] = theora_open(f, &remaining, s ? 320 : 400)))
            goto done;
    if (remaining || fread(index, 4, st.count, f) != st.count)
        goto done;
    // Offsets must tile the data section exactly; record 0 must be decodable on its own.
    if ((index[0] & OFFSET_MASK) != st.data_offset)
        goto done;
    for (int i = 0; i < count; i++) {
        u32 offset = index[i] & OFFSET_MASK, end = i + 1 < count ? index[i + 1] & OFFSET_MASK : st.file_bytes;
        if (end < offset + 12 || end > st.file_bytes || end - offset > 12 + audio_bytes + 2 * st.max_packet)
            goto done;
    }
    if (!(index[0] & KEY_TOP) || (st.bottom && !(index[0] & KEY_BOTTOM)))
        goto done;
    lut_init();
    long position = -1;
    int next = 0, target = 0, slot = 0, skipped = 0;
    int sound_next = 0, sound_write = 0; // next record whose sound is published, and its ring slot
    long sound_position = -1;
    int skip_until[2] = {0, 0}; // per stream: packets before this record are not decoded
    bool valid[2] = {false, false}, fresh[2] = {false, false}, force[2] = {true, true};
    u32 worker_generation = 0;
    while (!__atomic_load_n(&stop, __ATOMIC_ACQUIRE)) {
#ifndef VIDEO_HOST_TEST
        bg_wait();
        if (__atomic_load_n(&stop, __ATOMIC_ACQUIRE)) break;
#endif
        LightLock_Lock(&seek_lock);
        bool moved = worker_generation != generation;
        int want = requested_frame;
        worker_generation = generation;
        LightLock_Unlock(&seek_lock);
        if (moved) {
            want = want < 0 ? 0 : want >= count ? count - 1 : want;
            int key[2] = {key_before(index, want, KEY_TOP), st.bottom ? key_before(index, want, KEY_BOTTOM) : want};
            int start = key[0] < key[1] ? key[0] : key[1];
            // Keep decoding forward when the target lies ahead inside the current GOP and
            // no stream is waiting for a keyframe; otherwise restart from the keyframes.
            bool forward = want >= next && start <= next && skip_until[0] <= next && (!st.bottom || skip_until[1] <= next);
            if (!forward) {
                next = start;
                for (int s = 0; s < streams; s++) {
                    skip_until[s] = key[s];
                    valid[s] = false;
                }
            }
            target = want;
            sound_next = want; // every block carries its own state: sound restarts exactly here
            force[0] = force[1] = true;
            skipped = 0;
        }
        while (sound_file && sound_next < count) {
            Sound *so = &sounds[sound_write];
            if (__atomic_load_n(&so->ready, __ATOMIC_ACQUIRE))
                break;
            u32 offset = index[sound_next] & OFFSET_MASK, end = sound_next + 1 < count ? index[sound_next + 1] & OFFSET_MASK : st.file_bytes;
            u8 head[12];
            u32 gap = sound_position >= 0 && (long)offset >= sound_position ? offset - (u32)sound_position : 0xffffffffu;
            if (gap <= 2 * st.max_packet) {
                for (u32 part; gap; gap -= part) {
                    part = gap < st.max_packet ? gap : st.max_packet;
                    if (fread(packet, 1, part, sound_file) != part)
                        goto done;
                }
            } else if (fseek(sound_file, offset, SEEK_SET))
                goto done;
            sound_position = -1;
            if (fread(head, 1, 12, sound_file) != 12)
                goto done;
            u32 bytes = le32(head + 8);
            if (bytes != audio_bytes || bytes > end - offset - 12 || (bytes && fread(audio, 1, bytes, sound_file) != bytes))
                goto done;
            sound_position = (long)(offset + 12 + bytes);
            so->samples = 0;
            if (bytes && !adpcm_decode(audio, bytes, so->pcm, st.samples, &so->samples))
                goto done;
            so->index = sound_next++;
            so->generation = worker_generation;
            if (!so->samples)
                continue; // a record without sound: nothing to hand over
            __atomic_store_n(&so->ready, true, __ATOMIC_RELEASE);
            sound_write = (sound_write + 1) % NSOUND;
        }
        if (next >= count) {
            __atomic_store_n(&ended_generation, worker_generation, __ATOMIC_RELEASE);
            svcSleepThread(2000000);
            continue;
        }
        // Frames before a seek target only rebuild the reference pictures: never published.
        bool preroll = next < target;
        Frame *fr = &frames[slot];
        if (!preroll && __atomic_load_n(&fr->ready, __ATOMIC_ACQUIRE)) {
            svcSleepThread(1000000);
            continue;
        }
        int lag = preroll ? 0 : __atomic_load_n(&clock_frame, __ATOMIC_RELAXED) - next;
        if (lag > JUMP_LAG) {
            bool jumped = false;
            for (int s = 0; s < streams; s++)
                if (skip_until[s] <= next) {
                    skip_until[s] = key_after(index, next + lag, key_flag[s]);
                    valid[s] = false;
                    jumped = true;
                }
            if (jumped)
                stat_jumps++;
        }
        // Records no stream will decode are passed over without reading them.
        int resume = skip_until[0];
        if (st.bottom && skip_until[1] < resume)
            resume = skip_until[1];
        if (resume > next) {
            next = resume < count ? resume : count;
            continue;
        }
        u32 offset = index[next] & OFFSET_MASK, end = next + 1 < count ? index[next + 1] & OFFSET_MASK : st.file_bytes;
        u8 head[12];
        if (position != (long)offset && fseek(f, offset, SEEK_SET))
            goto done;
        position = -1;
        if (fread(head, 1, 12, f) != 12)
            goto done;
        u32 n[3] = {le32(head), le32(head + 4), le32(head + 8)};
        if (n[0] > st.max_packet || n[1] > st.max_packet || (!st.bottom && n[1]) || n[2] != audio_bytes ||
            12 + n[0] + n[1] + n[2] != end - offset)
            goto done;
        if (n[2] && fread(audio, 1, n[2], f) != n[2]) // sound is delivered by the loop above
            goto done;
        for (int s = 0; s < streams; s++) {
            if (!n[s])
                continue; // zero bytes: this screen repeats its previous picture
            if (fread(packet, 1, n[s], f) != n[s] || (packet[0] & 0x80))
                goto done;
            bool key = !(packet[0] & 0x40);
            if (next < skip_until[s] || (!valid[s] && !key))
                continue;
            ogg_packet op = {.packet = packet, .bytes = n[s], .packetno = next};
            TRACE_BEGIN();
            int rc = th_decode_packetin(dec[s], &op, NULL);
            TRACE_END(0);
            if (rc < 0)
                goto done;
            valid[s] = true;
            stat_decoded++;
            if (rc != TH_DUPFRAME)
                fresh[s] = true;
        }
        position = end;
        if (preroll) {
            next++;
            continue;
        }
        bool pending = false;
        for (int s = 0; s < streams; s++)
            pending |= valid[s] && (fresh[s] || force[s]);
        bool convert = pending && (lag < 1 || skipped >= MAX_SKIP);
        if (pending && !convert) {
            skipped++;
            stat_late++;
        } else
            skipped = 0;
        for (int s = 0; s < 2; s++) {
            fr->pic[s] = false;
            if (!convert || s >= streams || !valid[s] || !(fresh[s] || force[s]))
                continue;
            th_ycbcr_buffer planes;
            if (th_decode_ycbcr_out(dec[s], planes))
                goto done;
            TRACE_BEGIN();
            convert_tiles(planes, s ? fr->bot : fr->top, s ? 320 : 400);
            TRACE_END(1);
            fr->pic[s] = true;
            fresh[s] = force[s] = false;
            stat_converted++;
        }
        fr->samples = 0;
        fr->index = next++;
        fr->generation = worker_generation;
        __atomic_store_n(&fr->ready, true, __ATOMIC_RELEASE);
        slot = (slot + 1) % NSLOT;
    }
    ok = true;
done:
    for (int s = 0; s < 2; s++)
        th_decode_free(dec[s]);
    if (sound_file)
        fclose(sound_file);
    free(index);
    free(packet);
    free(audio);
    return ok;
}

static void decode_worker(void *unused) {
    FILE *f = fopen(path, "rb");
    if (!f || !(st.theora ? theora_worker(f) : mjpeg_worker(f)))
        __atomic_store_n(&decode_failed, true, __ATOMIC_RELEASE);
    if (f)
        fclose(f);
#ifdef FORGE_VIDEO_TRACE
    FILE *trace = fopen(FORGE_VIDEO_TRACE, "a");
    if (trace) {
        const double ms = 1000.0 / SYSCLOCK_ARM11;
        fprintf(trace, "%s packets=%lu converted=%lu late=%lu jumps=%lu failed=%d decode_ms avg=%.2f peak=%.2f convert_ms avg=%.2f peak=%.2f\n",
                path, (unsigned long)stat_decoded, (unsigned long)stat_converted, (unsigned long)stat_late, (unsigned long)stat_jumps,
                decode_failed, stat_decoded ? trace_ticks[0] * ms / stat_decoded : 0, trace_peak[0] * ms,
                stat_converted ? trace_ticks[1] * ms / stat_converted : 0, trace_peak[1] * ms);
        fclose(trace);
    }
#endif
}
// Only called after the decoder has exited; no frame buffers are freed early.
static void release_resources(void) {
    for (int i = 0; i < NSLOT; i++) {
        free(frames[i].top);
        free(frames[i].bot);
        free(frames[i].pcm);
        linearFree(audio_mem[i]);
        audio_mem[i] = NULL;
        memset(&frames[i], 0, sizeof frames[i]);
    }
    for (int i = 0; i < NSOUND; i++) {
        free(sounds[i].pcm);
        memset(&sounds[i], 0, sizeof sounds[i]);
    }
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            if (tex_live[i][j]) {
                C3D_TexDelete(&tex[i][j]);
                tex_live[i][j] = false;
            }
    release_pending = false;
}
void video_stop(void) {
    pending_path[0] = 0;
    __atomic_store_n(&stop, true, __ATOMIC_RELEASE);
    if (audio_available())
        ndspChnWaveBufClear(VCH);
    active = have_frame = false;
    paused = seeking = muted = false;
    // Worker still reads the immutable stream description until reaped.
    // It must not be reset here; reset only at next start.
    elapsed = 0;
    release_pending = true;
    music_duck(false);
}
/** True once the decoder thread has ended and been freed. A timed-out wait is reported by
 *  the kernel as a positive (non-failure) result, so only an exact zero counts as joined. */
static bool reap(u64 timeout_ns) {
    if (!thread)
        return true;
    if (threadJoin(thread, timeout_ns) != 0)
        return false;
    threadFree(thread);
    thread = NULL;
    return true;
}
void video_pump(void) {
    if (!release_pending) return;
    if (!reap(0)) return;
    release_resources();
    if (pending_path[0]) {
        char next[256];
        snprintf(next, sizeof next, "%s", pending_path);
        pending_path[0] = 0;
        video_start(next);
    }
}
bool video_stopping(void) { return release_pending; }
void video_exit(void) {
    video_stop();
    // Final process teardown only. A decoder still inside an SD read after two seconds is
    // left alone with everything it may touch; process exit reclaims it.
#ifdef VIDEO_HOST_TEST
    if (!reap(2000000000ULL)) return;
#else
    if (!thread_reap(&thread)) return;
#endif
    release_resources();
}
bool video_start(const char *file) {
    video_stop();
    video_pump();
    error[0] = 0;
    if (!file || strlen(file) >= sizeof path || !media_path_ready(file)) {
        snprintf(error, sizeof error, "Video cache is not verified");
        return false;
    }
    if (release_pending) {
        snprintf(pending_path, sizeof pending_path, "%s", file);
        return true; // next video_pump starts it once old buffers are no longer owned
    }
    snprintf(path, sizeof path, "%s", file);
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(error, sizeof error, "Video file missing");
        return false;
    }
    bool ok = read_header(f, &st);
    fclose(f);
    if (!ok) {
        snprintf(error, sizeof error, "Invalid video header");
        return false;
    }
    muted = !st.audio || !(S.music || S.sfx);
    sound_on = st.theora && st.audio && audio_available();
    memset(waves, 0, sizeof waves);
    // Buffers are sized by the stream: PCM16 for one frame record (none without sound).
    for (int i = 0; i < NSLOT; i++) {
        frames[i].top = calloc(PIXELS, 2);
        frames[i].bot = st.bottom ? calloc(PIXELS, 2) : NULL;
        frames[i].pcm = st.theora ? NULL : malloc(st.samples * 2);
        audio_mem[i] = st.audio ? linearAlloc(st.samples * 2) : NULL;
        if (!frames[i].top || (st.bottom && !frames[i].bot) || (!st.theora && !frames[i].pcm) || (st.audio && !audio_mem[i]))
            goto failed;
    }
    for (int i = 0; sound_on && i < NSOUND; i++)
        if (!(sounds[i].pcm = malloc(st.samples * 2)))
            goto failed;
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < (st.bottom ? 2 : 1); j++) {
            if (!C3D_TexInit(&tex[i][j], 512, 256, GPU_RGB565))
                goto failed;
            tex_live[i][j] = true;
            // A screen is uploaded only when its picture changes: start from black, not from
            // whatever the allocator returned.
            memset(tex[i][j].data, 0, PIXELS * 2);
            C3D_TexSetFilter(&tex[i][j],st.bottom?GPU_NEAREST:GPU_LINEAR,st.bottom?GPU_NEAREST:GPU_LINEAR);
            C3D_TexSetWrap(&tex[i][j], GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
        }
    __atomic_store_n(&stop, false, __ATOMIC_RELEASE);
    __atomic_store_n(&decode_failed, false, __ATOMIC_RELEASE);
    __atomic_store_n(&ended_generation, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&clock_frame, 0, __ATOMIC_RELAXED);
    paused = seeking = false;
    LightLock_Init(&seek_lock);
    generation = 1;
    requested_frame = 0;
    read_slot = audio_slot = sound_slot = display_bank[0] = display_bank[1] = 0;
    stat_decoded = stat_converted = stat_late = stat_jumps = 0;
#ifdef FORGE_VIDEO_TRACE
    memset(trace_ticks, 0, sizeof trace_ticks);
    memset(trace_peak, 0, sizeof trace_peak);
#endif
    elapsed = 0;
    active = true;
    if (audio_available()) {
        ndspChnReset(VCH);
        ndspChnSetFormat(VCH, NDSP_FORMAT_MONO_PCM16);
        ndspChnSetRate(VCH, st.audio ? st.rate : LEGACY_RATE);
        ndspChnSetInterp(VCH, NDSP_INTERP_LINEAR);
        float mix[12] = {S.volume, S.volume};
        ndspChnSetMix(VCH, mix);
    }
    music_duck(st.audio && !muted);
    s32 p = 0x30;
    svcGetThreadPriority(&p, CUR_THREAD_HANDLE);
    // libtheora's decoder keeps its working state on the heap; the stack only has to
    // cover its call depth and libjpeg's.
    int core = -2;
#if FORGE_VIDEO_SYSCORE
    if (st.theora && !IS_NEW3DS && R_SUCCEEDED(APT_SetAppCpuTimeLimit(FORGE_VIDEO_SYSCORE > 1 ? FORGE_VIDEO_SYSCORE : 60)))
        core = 1;
#endif
    thread = threadCreate(decode_worker, NULL, 96 * 1024, p + 2, core, false);
    if (!thread && core != -2)
        thread = threadCreate(decode_worker, NULL, 96 * 1024, p + 2, -2, false);
    if (!thread)
        goto failed;
    return true;
failed:
    snprintf(error,sizeof error,"%s",T("native.videoMemory"));
    video_stop();
    video_pump();
    return false;
}
/** Hands sound blocks to NDSP ahead of time (Theora streams). A block queued behind a playing
 *  one starts exactly when that one ends, so sound stays continuous and in step for as long
 *  as the queue never runs dry; the first block after a start, seek or underrun is aligned
 *  to the playback clock. */
static void feed_sound(void) {
    const float frame = 1.f / st.fps;
    for (int i = 0; i < NSOUND; i++) {
        Sound *so = &sounds[sound_slot];
        if (!__atomic_load_n(&so->ready, __ATOMIC_ACQUIRE))
            return;
        bool drop = so->generation != generation; // left over from before a seek
        if (!drop) {
            float at = so->index * frame;
            int busy = 0;
            for (int w = 0; w < NSLOT; w++)
                busy += waves[w].status == NDSP_WBUF_QUEUED || waves[w].status == NDSP_WBUF_PLAYING;
            if (!busy) {
                if (!have_frame || seeking || elapsed + .005f < at)
                    return; // starts together with its picture
                drop = elapsed >= at + frame; // its moment has passed: start from a later block
            } else {
                if (elapsed + (NSLOT - 1) * frame < at || busy == NSLOT)
                    return; // far enough ahead / no free buffer yet
                drop = elapsed + busy * frame > at + frame + AUDIO_LATE; // the queue has fallen behind the picture
            }
        }
        if (!drop) {
            ndspWaveBuf *w = &waves[audio_slot];
            memcpy(audio_mem[audio_slot], so->pcm, so->samples * 2);
            DSP_FlushDataCache(audio_mem[audio_slot], so->samples * 2);
            memset(w, 0, sizeof *w);
            w->data_vaddr = audio_mem[audio_slot];
            w->nsamples = so->samples;
            ndspChnWaveBufAdd(VCH, w);
            audio_slot = (audio_slot + 1) % NSLOT;
        }
        __atomic_store_n(&so->ready, false, __ATOMIC_RELEASE);
        sound_slot = (sound_slot + 1) % NSOUND;
    }
}
/** Advances the clock and moves the next due frame into the textures. */
static void present(void) {
    Frame *fr = &frames[read_slot];
    bool ready = __atomic_load_n(&fr->ready, __ATOMIC_ACQUIRE);
    // Drain old generations without touching buffers still owned by the worker.
    for (int i = 0; ready && fr->generation != generation && i < NSLOT; i++) {
        __atomic_store_n(&fr->ready, false, __ATOMIC_RELEASE);
        read_slot = (read_slot + 1) % NSLOT;
        fr = &frames[read_slot];
        ready = __atomic_load_n(&fr->ready, __ATOMIC_ACQUIRE);
    }
    if (ready && fr->generation != generation)
        return;
    // Wait for decoded frames instead of displaying black or outrunning the decoder.
    if (have_frame && !paused && !seeking)
        elapsed += DT;
    __atomic_store_n(&clock_frame, (int)(elapsed * st.fps), __ATOMIC_RELAXED);
    if (!ready) {
        if (__atomic_load_n(&ended_generation, __ATOMIC_ACQUIRE) == generation &&
            elapsed >= video_duration()) {
            elapsed = video_duration();
            paused = true;
            music_duck(st.audio && !muted);
        }
        return;
    }
    float at = fr->index / (float)st.fps;
    if (have_frame && elapsed + 0.005f < at)
        return;
    // MJPEG: the clock waits for a slow decoder, and the frame carries its own PCM.
    // Theora: the clock keeps real time and the worker catches up (MAX_SKIP / JUMP_LAG).
    if (!st.theora && elapsed > at + .1f)
        elapsed = at;
    // Keep the frame owned by the consumer until NDSP has room for its PCM.
    // Previously the picture advanced and silently discarded audio when full.
    if (fr->samples && audio_available() &&
        waves[audio_slot].status != NDSP_WBUF_DONE && waves[audio_slot].status != NDSP_WBUF_FREE) {
        elapsed = at;
        return;
    }
    for (int s = 0; s < 2; s++)
        if (fr->pic[s]) {
            display_bank[s] ^= 1;
            memcpy(tex[display_bank[s]][s].data, s ? fr->bot : fr->top, PIXELS * 2);
            C3D_TexFlush(&tex[display_bank[s]][s]);
        }
    if (fr->samples && audio_available()) {
        ndspWaveBuf *w = &waves[audio_slot];
        memcpy(audio_mem[audio_slot], fr->pcm, fr->samples * 2);
        DSP_FlushDataCache(audio_mem[audio_slot], fr->samples * 2);
        memset(w, 0, sizeof *w);
        w->data_vaddr = audio_mem[audio_slot];
        w->nsamples = fr->samples;
        ndspChnWaveBufAdd(VCH, w);
        audio_slot = (audio_slot + 1) % NSLOT;
    }
    have_frame = true;
    seeking = false;
    __atomic_store_n(&fr->ready, false, __ATOMIC_RELEASE);
    read_slot = (read_slot + 1) % NSLOT;
}
void video_update(void) {
    video_pump();
    if (active && __atomic_load_n(&decode_failed, __ATOMIC_ACQUIRE)) {
        // Worker only publishes a flag; UI owns the error string and NDSP state.
        snprintf(error,sizeof error,"%s",T("native.videoDecode"));
        if (audio_available())
            ndspChnWaveBufClear(VCH);
        paused = true;
        seeking = false;
        music_duck(false);
        return;
    }
    if (active && audio_available()) {
        float mix[12] = {muted ? 0 : S.volume, muted ? 0 : S.volume};
        ndspChnSetMix(VCH, mix);
    }
    if (!active || (paused && !seeking))
        return;
    present();
    if (sound_on)
        feed_sound();
}
bool video_active(void) { return active; }
bool video_paused(void) { return paused; }
bool video_bottom(void) { return active && st.bottom; }
float video_time(void) { return fminf(elapsed, video_duration()); }
float video_duration(void) { return active && st.fps ? st.count / (float)st.fps : 0; }
const char *video_error(void) { return error; }
bool video_muted(void) { return muted; }
bool video_has_audio(void) { return active && st.audio; }
void video_toggle_mute(void) {
    if (!active || !st.audio)
        return;
    muted = !muted;
    music_duck(active && !muted);
}
void video_pause(void) {
    if (active) {
        if (paused && elapsed >= video_duration())
            video_seek(0);
        paused = !paused;
        // Web Audio keeps its music bus ducked while an audible trailer is
        // paused, including after a seek/end. Only mute/close releases it.
        music_duck(st.audio && !muted);
        if (audio_available())
            ndspChnSetPaused(VCH, paused);
    }
}
void video_seek(float seconds) {
    if (!active)
        return;
    if (!isfinite(seconds) || __atomic_load_n(&decode_failed, __ATOMIC_ACQUIRE))
        return;
    if (audio_available()) {
        ndspChnWaveBufClear(VCH);
        memset(waves, 0, sizeof waves);
        ndspChnSetPaused(VCH, paused);
    }
    audio_slot = 0;
    elapsed = clampf(seconds, 0, fmaxf(0, video_duration() - 1.f / st.fps));
    seeking = true;
    LightLock_Lock(&seek_lock);
    requested_frame = (int)(elapsed * st.fps);
    __atomic_store_n(&clock_frame, requested_frame, __ATOMIC_RELAXED);
    generation++;
    if (!generation)
        generation = 1;
    LightLock_Unlock(&seek_lock);
    music_duck(st.audio && !muted);
}
void video_draw(bool bottom, float x, float y, float w, float h, float a) {
    if (!active || !have_frame || (bottom && !st.bottom))
        return;
    C2D_Image im = {&tex[display_bank[bottom ? 1 : 0]][bottom ? 1 : 0], &sub[bottom ? 1 : 0]};
    draw_img(im, x, y, w / im.subtex->width, h / 240, a);
}
