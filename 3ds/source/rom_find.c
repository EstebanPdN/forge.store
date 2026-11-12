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
    if (listed(json_object_get(rule, "names"), name)) return true;
    const char *dot = strrchr(name, '.');
    return dot && dot != name && listed(json_object_get(rule, "ext"), dot + 1);
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

static bool rule_scan(json_t *rule, char *out, size_t out_size, bool cached) {
    const char *dir = json_string_value(json_object_get(rule, "dir"));
    if (!dir || dir[0] != '/' || strstr(dir, "..") || strchr(dir, ':') || strchr(dir, '\\')) return false;
    char folder[PATH_MAX_LEN], path[PATH_MAX_LEN + 260];
    int n = snprintf(folder, sizeof folder, "%s%s", ROM_FIND_ROOT, dir);
    if (n < 2 || (size_t)n >= sizeof folder) return false;
    if (folder[n - 1] != '/') folder[n++] = '/', folder[n] = 0;
    folder[n - 1] = 0;  // opendir wants no trailing slash
    DIR *d = opendir(folder);
    folder[n - 1] = '/';
    if (!d) return false;
    bool found = false;
    struct dirent *e;
    while (!found && !stopping() && (e = readdir(d))) {
        if (!name_accepted(rule, e->d_name)) continue;
        struct stat st;
        int len = snprintf(path, sizeof path, "%s%s", folder, e->d_name);
        if (len < 0 || (size_t)len >= out_size || stat(path, &st) || !S_ISREG(st.st_mode)) continue;
        if (!size_accepted(rule, (uint64_t)st.st_size) || !content_ok(rule, path, (uint64_t)st.st_size, cached)) continue;
        strcpy(out, path);
        found = true;
    }
    closedir(d);
    return found;
}

static bool scan(json_t *rules, char *path, size_t size, bool *derived, bool cached) {
    size_t i; json_t *rule;
    char found[PATH_MAX_LEN + 260];
    json_array_foreach(rules, i, rule) {
        if (stopping() || !rule_scan(rule, found, path && size < sizeof found ? size : sizeof found, cached)) continue;
        if (path) strcpy(path, found);
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
