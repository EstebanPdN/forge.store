#include "rom_find.h"
#ifndef ROM_FIND_HOST_TEST
#include "common.h"
#endif
#include <ctype.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#ifdef ROM_FIND_HOST_TEST
extern char rom_find_root[];  // a temporary folder
#define ROM_FIND_ROOT rom_find_root
#else
#define ROM_FIND_ROOT "sdmc:"
#endif
#define PATH_MAX_LEN 300

static bool stop;
static bool stopping(void) { return __atomic_load_n(&stop, __ATOMIC_ACQUIRE); }

bool rom_find_ruled(json_t *req) {
    json_t *rules = json_object_get(req, "detect");
    return json_is_array(rules) && json_array_size(rules) > 0;
}

static bool listed(json_t *list, const char *name) {
    size_t i; json_t *v;
    json_array_foreach(list, i, v)
        if (json_is_string(v) && !strcasecmp(json_string_value(v), name)) return true;
    return false;
}

static bool name_accepted(json_t *rule, const char *name) {
    if (name[0] == '.' || listed(json_object_get(rule, "skip"), name)) return false;
    json_t *names = json_object_get(rule, "names"), *ext = json_object_get(rule, "ext");
    if (!names && !ext) return true;
    if (listed(names, name)) return true;
    const char *dot = strrchr(name, '.');
    return dot && dot != name && listed(ext, dot + 1);
}

static bool size_accepted(json_t *rule, uint64_t size) {
    json_t *sizes = json_object_get(rule, "sizes"), *v;
    if (size < (uint64_t)json_integer_value(json_object_get(rule, "minSize"))) return false;
    if (!json_is_array(sizes)) return size > 0;
    size_t i;
    json_array_foreach(sizes, i, v)
        if ((uint64_t)json_integer_value(v) == size) return true;
    return false;
}

static int hex_digit(char c) { return isdigit((unsigned char)c) ? c - '0' : isxdigit((unsigned char)c) ? tolower((unsigned char)c) - 'a' + 10 : -1; }

/** Header bytes at one offset: any of the texts, or any of the hex strings. */
static bool magic_ok(FILE *f, json_t *check) {
    unsigned char got[64], want[64];
    json_t *texts = json_object_get(check, "any"), *hexes = json_object_get(check, "hex"), *v;
    long at = (long)json_integer_value(json_object_get(check, "at"));
    size_t i;
    json_array_foreach(texts, i, v) {
        size_t n = json_string_length(v);
        if (!n || n > sizeof got || fseek(f, at, SEEK_SET) || fread(got, 1, n, f) != n) continue;
        if (!memcmp(got, json_string_value(v), n)) return true;
    }
    json_array_foreach(hexes, i, v) {
        const char *s = json_string_value(v);
        size_t n = s ? strlen(s) / 2 : 0;
        if (!n || n > sizeof got || strlen(s) % 2) continue;
        bool valid = true;
        for (size_t k = 0; k < n; k++) {
            int hi = hex_digit(s[k * 2]), lo = hex_digit(s[k * 2 + 1]);
            if (hi < 0 || lo < 0) valid = false;
            want[k] = (unsigned char)(hi << 4 | lo);
        }
        if (valid && !fseek(f, at, SEEK_SET) && fread(got, 1, n, f) == n && !memcmp(got, want, n)) return true;
    }
    return false;
}

static bool crc_ok(FILE *f, json_t *check, uint64_t size) {
    uint64_t tail = (uint64_t)json_integer_value(json_object_get(check, "tail"));
    if (!tail) tail = size;
    if (tail > size || fseek(f, (long)(size - tail), SEEK_SET)) return false;
    unsigned char buf[4096];
    uLong crc = crc32(0, NULL, 0);
    for (uint64_t left = tail; left;) {
        size_t n = left < sizeof buf ? (size_t)left : sizeof buf;
        if (stopping() || fread(buf, 1, n, f) != n) return false;
        crc = crc32(crc, buf, n);
        left -= n;
    }
    char hex[9];
    snprintf(hex, sizeof hex, "%08lx", (unsigned long)crc);
    return listed(json_object_get(check, "any"), hex);
}

// A CRC reads the whole ROM: the answer is kept while the same file stays there.
// Only the cached scans use it (one worker at a time).
static struct { uint32_t path_hash; uint64_t size; bool ok, used; } verdicts[24];
static unsigned verdict_next;
static uint32_t fnv(const char *s) {
    uint32_t h = 2166136261u;
    while (*s) h = (h ^ (unsigned char)tolower((unsigned char)*s++)) * 16777619u;
    return h;
}

static bool content_ok(json_t *rule, const char *path, uint64_t size, bool cached) {
    json_t *magic = json_object_get(rule, "magic"), *crc = json_object_get(rule, "crc32"), *v;
    if (!json_array_size(magic) && !json_is_object(crc)) return true;
    uint32_t hash = fnv(path);
    if (cached && json_is_object(crc))
        for (unsigned i = 0; i < sizeof verdicts / sizeof *verdicts; i++)
            if (verdicts[i].used && verdicts[i].path_hash == hash && verdicts[i].size == size) return verdicts[i].ok;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    bool ok = true;
    size_t i;
    json_array_foreach(magic, i, v) if (ok) ok = magic_ok(f, v);
    if (ok && json_is_object(crc)) {
        ok = crc_ok(f, crc, size);
        if (cached && !stopping()) {
            unsigned slot = verdict_next++ % (sizeof verdicts / sizeof *verdicts);
            verdicts[slot].path_hash = hash, verdicts[slot].size = size, verdicts[slot].ok = ok, verdicts[slot].used = true;
        }
    }
    fclose(f);
    return ok;
}

/** One file against one rule: its name, size and contents. */
static bool file_ok(json_t *rule, const char *path, const char *name, bool cached) {
    struct stat st;
    return name_accepted(rule, name) && !stat(path, &st) && S_ISREG(st.st_mode) &&
           size_accepted(rule, (uint64_t)st.st_size) && content_ok(rule, path, (uint64_t)st.st_size, cached);
}

static bool safe_dir(const char *dir) {
    return dir && dir[0] == '/' && !strstr(dir, "..") && !strchr(dir, ':') && !strchr(dir, '\\');
}

// "dir" ending in "/*/" means every folder directly inside: /3ds/butterscotch/<any game>/.
static bool folder_scan(json_t *rule, const char *dir, char *out, size_t out_size, bool cached) {
    if (!safe_dir(dir)) return false;
    char folder[PATH_MAX_LEN], path[PATH_MAX_LEN + 260];
    size_t dl = strlen(dir);
    bool any_folder = dl >= 3 && !strcmp(dir + dl - 3, "/*/");
    int n = snprintf(folder, sizeof folder, "%s%.*s", ROM_FIND_ROOT, (int)(any_folder ? dl - 2 : dl), dir);
    if (n < 2 || (size_t)n >= sizeof folder) return false;
    if (folder[n - 1] != '/') folder[n++] = '/', folder[n] = 0;
    folder[n - 1] = 0;  // opendir wants no trailing slash
    DIR *d = opendir(folder);
    folder[n - 1] = '/';
    if (!d) return false;
    bool found = false;
    struct dirent *e;
    while (!found && !stopping() && (e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        if (any_folder) {
            char sub[PATH_MAX_LEN];
            int len = snprintf(sub, sizeof sub, "%.*s%s/", (int)(dl - 2), dir, e->d_name);
            struct stat st;
            snprintf(path, sizeof path, "%s%s", folder, e->d_name);
            if (len > 0 && (size_t)len < sizeof sub && !strchr(e->d_name, '*') && !stat(path, &st) && S_ISDIR(st.st_mode))
                found = folder_scan(rule, sub, out, out_size, cached);
            continue;
        }
        int len = snprintf(path, sizeof path, "%s%s", folder, e->d_name);
        if (len < 0 || (size_t)len >= out_size || !file_ok(rule, path, e->d_name, cached)) continue;
        strcpy(out, path);
        found = true;
    }
    closedir(d);
    return found;
}

static bool rule_scan(json_t *rule, char *out, size_t out_size, bool cached) {
    json_t *dirs = json_object_get(rule, "dir"), *dir;
    if (!json_is_array(dirs)) return folder_scan(rule, json_string_value(dirs), out, out_size, cached);
    size_t i;
    json_array_foreach(dirs, i, dir)
        if (!stopping() && folder_scan(rule, json_string_value(dir), out, out_size, cached)) return true;
    return false;
}

// ---- placing a ROM where a port reads it -------------------------------------------------------
// Some ports open one fixed name in .z64 byte order (the N64 recompilations: <folder>/DK64.z64).
// A ROM found under another name or byte order is copied there; the player's file is never
// moved or deleted. One copy at a time: the scan worker and an installation may both look.
static bool placing;

static int n64_order(const unsigned char h[4]) {
    uint32_t w = (uint32_t)h[0] << 24 | (uint32_t)h[1] << 16 | (uint32_t)h[2] << 8 | h[3];
    return w == 0x80371240u ? 0 : w == 0x37804012u ? 1 : w == 0x40123780u ? 2 : -1;
}

static bool copy_as(const char *from, const char *to, bool to_z64) {
    char part[PATH_MAX_LEN + 8];
    if (snprintf(part, sizeof part, "%s.part", to) >= (int)sizeof part) return false;
    FILE *in = fopen(from, "rb"), *out = in ? fopen(part, "wb") : NULL;
    unsigned char *buf = out ? malloc(65536) : NULL;
    bool ok = buf != NULL;
    int order = 0;
    for (size_t n, total = 0; ok && !stopping() && (n = fread(buf, 1, 65536, in)) > 0; total += n) {
        if (!total) order = to_z64 && n >= 4 ? n64_order(buf) : 0;
        if (order > 0 && n % 4) ok = false;  // dumps are whole words
        for (size_t i = 0; ok && order > 0 && i < n; i += 4) {
            unsigned char w[4] = {buf[i], buf[i + 1], buf[i + 2], buf[i + 3]};
            if (order == 1) buf[i] = w[1], buf[i + 1] = w[0], buf[i + 2] = w[3], buf[i + 3] = w[2];
            else buf[i] = w[3], buf[i + 1] = w[2], buf[i + 2] = w[1], buf[i + 3] = w[0];
        }
        ok = ok && fwrite(buf, 1, n, out) == n;
#ifndef ROM_FIND_HOST_TEST
        bg_wait();
#endif
    }
    ok = ok && !stopping() && in && !ferror(in);
    free(buf);
    if (in) fclose(in);
    if (out && fclose(out)) ok = false;
    if (ok) ok = !rename(part, to);
    if (!ok && out) remove(part);
    return ok;
}

static int file_order(const char *path) {
    unsigned char h[4];
    FILE *f = fopen(path, "rb");
    int order = f && fread(h, 1, 4, f) == 4 ? n64_order(h) : -1;
    if (f) fclose(f);
    return order;
}

/** `found` satisfies a rule; afterwards `found` is where the port reads it, or false. */
static bool place(const char *target, char *found, size_t size) {
    char to[PATH_MAX_LEN], from[PATH_MAX_LEN + 260], old[PATH_MAX_LEN + 8];
    if (!safe_dir(target) || target[strlen(target) - 1] == '/' ||
        snprintf(to, sizeof to, "%s%s", ROM_FIND_ROOT, target) >= (int)sizeof to || strlen(to) >= size) return false;
    size_t n = strlen(to);
    bool z64 = n > 4 && !strcasecmp(to + n - 4, ".z64"), same = !strcasecmp(found, to);
    if (same && (!z64 || file_order(to) <= 0)) return true;
    if (__atomic_exchange_n(&placing, true, __ATOMIC_ACQUIRE)) return false;  // the other caller is copying it
    snprintf(from, sizeof from, "%s", found);
    bool ok = true, moved = false;
    struct stat st;
    if (!stat(to, &st)) {
        // Something else has the port's name (another ROM, or this one in another byte
        // order): it stays beside, as <name>.old, and goes back if the copy fails.
        snprintf(old, sizeof old, "%s.old", to);
        ok = moved = access(old, F_OK) && !rename(to, old);
        if (same) snprintf(from, sizeof from, "%s", old);
    }
    ok = ok && copy_as(from, to, z64);
    if (!ok && moved) rename(old, to);
    __atomic_store_n(&placing, false, __ATOMIC_RELEASE);
    if (ok) strcpy(found, to);
    return ok;
}

static bool scan(json_t *rules, char *path, size_t size, bool *derived, bool cached) {
    size_t i; json_t *rule;
    char found[PATH_MAX_LEN + 260];
    size_t cap = path && size < sizeof found ? size : sizeof found;
    json_array_foreach(rules, i, rule) {
        if (stopping() || !rule_scan(rule, found, cap, cached)) continue;
        const char *to = json_string_value(json_object_get(rule, "placeAs"));
        if (to && !place(to, found, cap)) continue;
        if (path) snprintf(path, size, "%s", found);
        if (derived) *derived = json_is_true(json_object_get(rule, "derived"));
        return true;
    }
    return false;
}

bool rom_find_scan(json_t *req, char *path, size_t size, bool *derived) {
    return scan(json_object_get(req, "detect"), path, size, derived, false);
}

#ifndef ROM_FIND_HOST_TEST
// ---- cached answers ------------------------------------------------------------------------------
// The registry and the answers are main-thread only; the worker owns a copy of the rules.
#define WATCHED 64
typedef struct { json_t *req; bool known, have, derived; char path[PATH_MAX_LEN]; } Slot;
typedef struct { json_t *rules; bool have, derived; char path[PATH_MAX_LEN]; } Probe;
static Slot slots[WATCHED];
static Probe *job;
static unsigned nslots, njob, generation, job_generation, requested = 1, launched, completed;
static Thread worker;
static bool ready;
static float retry_after;

static Slot *slot_of(json_t *req) {
    for (unsigned i = 0; i < nslots; i++)
        if (slots[i].req == req) return &slots[i];
    return NULL;
}

void rom_find_reset(void) { nslots = 0, generation++, requested++; }

void rom_find_watch(json_t *req) {
    if (!rom_find_ruled(req) || slot_of(req) || nslots >= WATCHED) return;
    slots[nslots++] = (Slot){.req = req};
    requested++;
}

unsigned rom_find_refresh(void) { return ++requested; }
unsigned rom_find_completed(void) { return completed; }
bool rom_find_known(json_t *req) { Slot *s = slot_of(req); return s && s->known; }
bool rom_find_have(json_t *req) { Slot *s = slot_of(req); return s && s->known && s->have; }
const char *rom_find_path(json_t *req, bool *derived) {
    Slot *s = slot_of(req);
    if (!s || !s->known || !s->have) return NULL;
    if (derived) *derived = s->derived;
    return s->path;
}

static void scan_all(void *unused) {
    for (unsigned i = 0; i < njob && !stopping(); i++) {
        bg_wait();
        job[i].have = scan(job[i].rules, job[i].path, sizeof job[i].path, &job[i].derived, true);
    }
    __atomic_store_n(&ready, true, __ATOMIC_RELEASE);
}

static void job_free(void) {
    for (unsigned i = 0; i < njob; i++) json_decref(job[i].rules);
    free(job);
    job = NULL, njob = 0;
}

void rom_find_pump(float now) {
    if (worker) {
        if (!__atomic_load_n(&ready, __ATOMIC_ACQUIRE) || !thread_done(&worker)) return;
        if (job_generation == generation && !stopping())
            for (unsigned i = 0; i < njob && i < nslots; i++) {
                slots[i].known = true, slots[i].have = job[i].have, slots[i].derived = job[i].derived;
                snprintf(slots[i].path, sizeof slots[i].path, "%s", job[i].path);
            }
        if (job_generation == generation) completed = launched;
        job_free();
        __atomic_store_n(&ready, false, __ATOMIC_RELEASE);
    }
    if (stopping() || now < retry_after || completed == requested) return;
    if (!nslots) { completed = requested; return; }
    job = calloc(nslots, sizeof *job);
    if (!job) { retry_after = now + 1; return; }
    njob = nslots;
    // jansson reference counts are not thread safe: the worker gets its own copy.
    for (unsigned i = 0; i < njob; i++) job[i].rules = json_deep_copy(json_object_get(slots[i].req, "detect"));
    launched = requested, job_generation = generation;
    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    worker = threadCreate(scan_all, NULL, 32 * 1024, priority + 2, -2, false);
    if (!worker) { job_free(); retry_after = now + 1; }
}

void rom_find_stop(void) { __atomic_store_n(&stop, true, __ATOMIC_RELEASE); }
// A scan still waiting for the SD at the shutdown deadline keeps its job.
void rom_find_exit(void) {
    rom_find_stop();
    if (worker && !thread_reap(&worker)) return;
    job_free();
}
#endif
