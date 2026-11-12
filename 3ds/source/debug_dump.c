// Test builds only. Everything is written under sdmc:/3ds/forge-store/dumps/<name>/:
//   top.bmp, top-right.bmp (3D on), bottom.bmp  the framebuffers the LCDs are scanning out
//   vram.bin       the 6 MiB of VRAM (render targets, textures)
//   ram-image.bin  .data and .bss: every static of the program (see forge-store.map)
//   ram-heap.bin   the used part of the malloc heap
//   ram-linear.bin the whole linear heap (only with R held: tens of MiB, slow)
//   info.txt       addresses and sizes of the above plus the state written by main.c
#include "debug_dump.h"

#if FORGE_DEBUG
#include <curl/curl.h>
#include <malloc.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

extern char __bss_start__[], __end__[];
extern u32 __ctru_heap, __ctru_heap_size, __ctru_linear_heap, __ctru_linear_heap_size;

#define CHUNK (64 * 1024)
typedef struct {
    const char *file;
    u8 *pixels;  // BMP order: bottom row first, BGR
    int w;
} Screen;
static struct {
    Thread thread;
    int state;  // 0 idle, 1 writing, 2 finished (message not taken yet)
    bool full, ok;
    char name[24], dir[80], message[112];
    Screen screens[3];
    u32 image_base, image_size, heap_size;
} job;

/** One LCD framebuffer (columns of 240 pixels, bottom first) as upright BMP rows. */
static u8 *grab(const u8 *fb, int w, u32 format, u32 stride) {
    static const u8 BPP[] = {4, 3, 2, 2, 2};
    u32 kind = format & 7;
    if (!fb || kind > 4 || stride < 240u * BPP[kind]) return NULL;
    u8 *out = malloc(w * 240 * 3);
    if (!out) return NULL;
    for (int row = 0; row < 240; row++)
        for (int x = 0; x < w; x++) {
            const u8 *p = fb + x * stride + row * BPP[kind];
            u8 *d = out + (row * w + x) * 3, r, g, b;
            if (kind == 1) b = p[0], g = p[1], r = p[2];
            else if (kind == 0) b = p[1], g = p[2], r = p[3];
            else {
                u16 v = p[0] | p[1] << 8;
                if (kind == 2) r = (v >> 11) << 3, g = ((v >> 5) & 63) << 2, b = (v & 31) << 3;
                else if (kind == 3) r = (v >> 11) << 3, g = ((v >> 6) & 31) << 3, b = ((v >> 1) & 31) << 3;
                else r = (v >> 12) << 4, g = ((v >> 8) & 15) << 4, b = ((v >> 4) & 15) << 4;
            }
            d[0] = b, d[1] = g, d[2] = r;
        }
    return out;
}

static bool write_bmp(const char *path, const u8 *pixels, int w) {
    u32 size = w * 240 * 3;
    u8 head[54] = {'B', 'M'};
    u32 fields[] = {54 + size, 0, 54, 40, w, 240, 1 | 24 << 16, 0, size, 2835, 2835, 0, 0};
    memcpy(head + 2, fields, sizeof fields);
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(head, 1, 54, f) == 54 && fwrite(pixels, 1, size, f) == size;
    return fclose(f) == 0 && ok;
}

/** Memory goes through a CPU copy first: an emulator only syncs GPU-owned pages when the
 *  CPU reads them, and a file write straight from the pointer can see stale data. */
static bool write_region(const char *path, const void *base, u32 size, u64 *total) {
    FILE *f = fopen(path, "wb");
    u8 *buf = malloc(CHUNK);
    bool ok = f && buf;
    for (u32 at = 0; ok && at < size; at += CHUNK) {
        u32 n = size - at < CHUNK ? size - at : CHUNK;
        memcpy(buf, (const u8 *)base + at, n);
        ok = fwrite(buf, 1, n, f) == n;
    }
    free(buf);
    if (f && fclose(f)) ok = false;
    if (ok) *total += size;
    return ok;
}

#ifdef FORGE_DUMP_URL
static size_t feed(char *out, size_t size, size_t count, void *file) { return fread(out, size, count, file); }
/** Best effort: the development Mac may simply not be on this network. */
static bool upload(const char *dir, const char *name, const char *file) {
    char path[160], url[256];
    snprintf(path, sizeof path, "%s/%s", dir, file);
    snprintf(url, sizeof url, FORGE_DUMP_URL "/%s/%s", name, file);
    struct stat st;
    FILE *f = fopen(path, "rb");
    CURL *c = f && !stat(path, &st) ? curl_easy_init() : NULL;
    bool ok = false;
    if (c) {
        curl_easy_setopt(c, CURLOPT_URL, url);
        curl_easy_setopt(c, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(c, CURLOPT_READFUNCTION, feed);
        curl_easy_setopt(c, CURLOPT_READDATA, f);
        curl_easy_setopt(c, CURLOPT_INFILESIZE_LARGE, (curl_off_t)st.st_size);
        curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 4L);
        curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);
        curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 15L);
        curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
        long code = 0;
        ok = curl_easy_perform(c) == CURLE_OK && !curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code) && code / 100 == 2;
        curl_easy_cleanup(c);
    }
    if (f) fclose(f);
    return ok;
}
#endif

static void dump_worker(void *unused) {
    char path[160];
    u64 total = 0;
    bool ok = true;
    for (int i = 0; i < 3; i++) {
        Screen *s = &job.screens[i];
        if (!s->pixels) continue;
        snprintf(path, sizeof path, "%s/%s", job.dir, s->file);
        if (write_bmp(path, s->pixels, s->w)) total += s->w * 240 * 3 + 54;
        else ok = false;
        free(s->pixels);
        s->pixels = NULL;
    }
    snprintf(path, sizeof path, "%s/vram.bin", job.dir);
    ok = write_region(path, (void *)OS_VRAM_VADDR, OS_VRAM_SIZE, &total) && ok;
    snprintf(path, sizeof path, "%s/ram-image.bin", job.dir);
    ok = write_region(path, (void *)job.image_base, job.image_size, &total) && ok;
    snprintf(path, sizeof path, "%s/ram-heap.bin", job.dir);
    ok = write_region(path, (void *)__ctru_heap, job.heap_size, &total) && ok;
    if (job.full) {
        snprintf(path, sizeof path, "%s/ram-linear.bin", job.dir);
        ok = write_region(path, (void *)__ctru_linear_heap, __ctru_linear_heap_size, &total) && ok;
    }
    const char *sent = "";
#ifdef FORGE_DUMP_URL
    if (ok) {
        static const char *files[] = {"info.txt", "top.bmp", "top-right.bmp", "bottom.bmp", "vram.bin", "ram-image.bin", "ram-heap.bin", "ram-linear.bin"};
        bool all = upload(job.dir, job.name, files[0]);
        for (unsigned i = 1; all && i < sizeof files / sizeof *files; i++) {
            snprintf(path, sizeof path, "%s/%s", job.dir, files[i]);
            struct stat st;
            if (!stat(path, &st)) all = upload(job.dir, job.name, files[i]);
        }
        sent = all ? " · enviado al Mac" : " · sin Mac en la red";
    }
#endif
    job.ok = ok;
    if (ok) snprintf(job.message, sizeof job.message, "Volcado %s · %.1f MB%s", job.name, total / 1048576.0, sent);
    else snprintf(job.message, sizeof job.message, "Volcado %s incompleto: revisa la SD", job.name);
    __atomic_store_n(&job.state, 2, __ATOMIC_RELEASE);
}

FILE *debug_dump_begin(bool full) {
    if (__atomic_load_n(&job.state, __ATOMIC_ACQUIRE)) return NULL;
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    snprintf(job.name, sizeof job.name, "%02d%02d-%02d%02d%02d", tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
    snprintf(job.dir, sizeof job.dir, DATA_DIR "/dumps/%s", job.name);
    mkdir(DATA_DIR "/dumps", 0777);
    mkdir(job.dir, 0777);
    char path[160];
    snprintf(path, sizeof path, "%s/info.txt", job.dir);
    FILE *info = fopen(path, "w");
    if (!info) return NULL;
    job.full = full;

    // What the LCDs are scanning out right now, not the frame being drawn.
    GSPGPU_CaptureInfo cap = {0};
    bool have = R_SUCCEEDED(GSPGPU_ImportDisplayCaptureInfo(&cap));
    GSPGPU_CaptureInfoEntry *top = &cap.screencapture[0], *bot = &cap.screencapture[1];
    if (!have) {
        u16 w, h;
        top->framebuf0_vaddr = (u32 *)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &w, &h);
        bot->framebuf0_vaddr = (u32 *)gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &w, &h);
        top->format = bot->format = GSP_BGR8_OES;
        top->framebuf_widthbytesize = bot->framebuf_widthbytesize = 240 * 3;
    }
    bool stereo = have && osGet3DSliderState() > 0 && top->framebuf1_vaddr && top->framebuf1_vaddr != top->framebuf0_vaddr;
    job.screens[0] = (Screen){"top.bmp", grab((u8 *)top->framebuf0_vaddr, 400, top->format, top->framebuf_widthbytesize), 400};
    job.screens[1] = (Screen){"top-right.bmp", stereo ? grab((u8 *)top->framebuf1_vaddr, 400, top->format, top->framebuf_widthbytesize) : NULL, 400};
    job.screens[2] = (Screen){"bottom.bmp", grab((u8 *)bot->framebuf0_vaddr, 320, bot->format, bot->framebuf_widthbytesize), 320};

    MemInfo mem = {0};
    PageInfo page;
    job.image_base = (u32)__bss_start__ & ~0xfffu;
    if (R_SUCCEEDED(svcQueryMemory(&mem, &page, (u32)__bss_start__)) && mem.base_addr <= (u32)__bss_start__) job.image_base = mem.base_addr;
    job.image_size = (u32)__end__ - job.image_base;
    u32 brk = (u32)sbrk(0);
    job.heap_size = brk > __ctru_heap && brk - __ctru_heap <= __ctru_heap_size ? brk - __ctru_heap : 0;
    struct mallinfo mi = mallinfo();
    fprintf(info, "forge.store debug dump %s (%s)\n", job.name, full ? "full: R+SELECT" : "SELECT");
    fprintf(info, "screens: source=%s top=%p/%p format=%lu stride=%lu bottom=%p format=%lu stride=%lu slider=%.2f\n",
            have ? "display capture info" : "gfx back buffer", top->framebuf0_vaddr, top->framebuf1_vaddr, top->format,
            top->framebuf_widthbytesize, bot->framebuf0_vaddr, bot->format, bot->framebuf_widthbytesize, osGet3DSliderState());
    fprintf(info, "vram.bin       base=0x%08x size=0x%08x free=%lu\n", OS_VRAM_VADDR, OS_VRAM_SIZE, vramSpaceFree());
    fprintf(info, "ram-image.bin  base=0x%08lx size=0x%08lx (.data + .bss; .bss starts at %p)\n", job.image_base, job.image_size, __bss_start__);
    fprintf(info, "ram-heap.bin   base=0x%08lx size=0x%08lx of 0x%08lx; malloc in use=%u free=%u\n", __ctru_heap, job.heap_size,
            __ctru_heap_size, (unsigned)mi.uordblks, (unsigned)mi.fordblks);
    fprintf(info, "ram-linear.bin base=0x%08lx size=0x%08lx free=%lu%s\n", __ctru_linear_heap, __ctru_linear_heap_size, linearSpaceFree(),
            full ? "" : " (not written: hold R with SELECT)");
    fprintf(info, "application region free=%lu\n", (unsigned long)osGetMemRegionFree(MEMREGION_APPLICATION));
    return info;
}

void debug_dump_commit(FILE *info) {
    fclose(info);
    __atomic_store_n(&job.state, 1, __ATOMIC_RELEASE);
    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    job.thread = threadCreate(dump_worker, NULL, 32 * 1024, priority + 3, -2, false);
    if (!job.thread) {
        for (int i = 0; i < 3; i++) free(job.screens[i].pixels), job.screens[i].pixels = NULL;
        job.ok = false;
        snprintf(job.message, sizeof job.message, "Volcado %s: sin memoria", job.name);
        __atomic_store_n(&job.state, 2, __ATOMIC_RELEASE);
    }
}

const char *debug_dump_poll(bool *ok) {
    if (__atomic_load_n(&job.state, __ATOMIC_ACQUIRE) != 2 || !thread_done(&job.thread)) return NULL;
    *ok = job.ok;
    __atomic_store_n(&job.state, 0, __ATOMIC_RELEASE);
    return job.message;
}


// ---- stall watchdog ---------------------------------------------------------------------------
static struct {
    Thread thread;
    u32 beat;
    const char *stage;
    bool quit;
} watch;

void debug_beat(const char *stage) {
    __atomic_store_n(&watch.stage, stage, __ATOMIC_RELEASE);
    __atomic_add_fetch(&watch.beat, 1, __ATOMIC_RELEASE);
}

static void stall_line(const char *text) {
    FILE *f = fopen(DATA_DIR "/stall.txt", "a");
    if (!f) return;
    fputs(text, f);
    fclose(f);
}

static void watch_worker(void *unused) {
    u32 seen = 0;
    u64 since = osGetTime();
    bool reported = false;
    while (!__atomic_load_n(&watch.quit, __ATOMIC_ACQUIRE)) {
        svcSleepThread(250000000);
        u32 beat = __atomic_load_n(&watch.beat, __ATOMIC_ACQUIRE);
        u64 now = osGetTime();
        char line[160];
        if (beat != seen) {
            if (reported) {
                snprintf(line, sizeof line, "  ...resumed after %.1f s\n", (now - since) / 1000.0);
                stall_line(line);
            }
            seen = beat, since = now, reported = false;
        } else if (!reported && now - since > 1500) {
            const char *stage = __atomic_load_n(&watch.stage, __ATOMIC_ACQUIRE);
            snprintf(line, sizeof line, "t=%.1f main loop stopped in '%s'%s\n", NOW, stage ? stage : "?",
                     stage && !strcmp(stage, "apt") ? " (HOME Menu, sleep or closing)" : "");
            stall_line(line);
            reported = true;
        }
    }
}

void debug_note(const char *what) {
    char line[120];
    snprintf(line, sizeof line, "t=%.1f %s (os %llu ms)\n", NOW, what, (unsigned long long)osGetTime());
    stall_line(line);
}

void debug_watch_start(void) {
    remove(DATA_DIR "/stall.txt");
    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    watch.thread = threadCreate(watch_worker, NULL, 8 * 1024, priority - 3, -2, false);
}

void debug_exit_trace(const char *step) {
    static u64 t0;
    if (!step) {
        t0 = osGetTime();
        __atomic_store_n(&watch.quit, true, __ATOMIC_RELEASE);
    }
    FILE *f = fopen(DATA_DIR "/exit-trace.txt", step ? "a" : "w");
    if (!f) return;
    fprintf(f, "%5lu ms  %s\n", (unsigned long)(osGetTime() - t0), step ? step : "main loop left");
    fclose(f);
}

void debug_dump_exit(void) {
    __atomic_store_n(&watch.quit, true, __ATOMIC_RELEASE);
    thread_reap(&watch.thread);
    thread_reap(&job.thread);
}
#endif
