// Installer thread. One job at a time; steps follow the catalog's install script.
#ifdef FORGE_INSTALL_HOST_TEST
#include "../tests/install-host.h"
#else
#include "install.h"
#ifndef FORGE_INSTALL_HOST_TEST
#include "http_cache.h"
#endif
#include "network.h"
#include "dsp_dump.h"
#endif
#include <archive.h>
#include <archive_entry.h>
#include <curl/curl.h>
#include <errno.h>
#include <malloc.h>
#ifndef FORGE_INSTALL_HOST_TEST
#include <mbedtls/sha256.h>
#endif
#include <limits.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>

#define SOC_SIZE 0x100000
#define MAX_JOBS 16

static Job jobs[MAX_JOBS];
static int njobs;
static LightLock lock;
static LightEvent wake;
static Thread worker;
static volatile bool quit;
static u32 *soc_buf;
static bool net_ok;
static char sums[8192]; // SHA256SUMS of the release being installed

void tvars(char *out, size_t n, const char *tmpl, const char *k1, const char *v1, const char *k2, const char *v2) {
    size_t o = 0;
    for (const char *p = tmpl; *p && o + 1 < n;) {
        if (*p == '{') {
            const char *end = strchr(p, '}');
            if (end) {
                size_t kl = end - p - 1;
                const char *v = NULL;
                if (k1 && strlen(k1) == kl && !strncmp(p + 1, k1, kl))
                    v = v1;
                if (k2 && strlen(k2) == kl && !strncmp(p + 1, k2, kl))
                    v = v2;
                if (v) {
                    while (*v && o + 1 < n)
                        out[o++] = *v++;
                    p = end + 1;
                    continue;
                }
            }
        }
        out[o++] = *p++;
    }
    out[o] = 0;
}

// All application writes are staged. Only tx_publish touches existing files, and it
// keeps the previous bytes until the durable installed-record commit succeeds.
#define TX_DIR DATA_DIR "/install-transaction"
#define PATH_CAP 512
#define FILE_LIMIT 0x7fffffffULL // stdio/newlib signed offsets on this target
#define TX_LIMIT 4096

typedef struct {
    char path[PATH_CAP], stage[96], backup[96];
    bool deleted, published, had_old, remove_live;
} TxFile;
typedef struct {
    TxFile *files;
    int count, cap;
    json_t *dirs;
    char cia_path[PATH_CAP];
    JobStep *cia_step;
    u64 title_id;
    bool had_title, cia_attempted;
    char cia_cache[PATH_CAP];
} Transaction;
static Transaction tx;
static u64 job_serial;

static bool fail(Job *j, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(j->error, sizeof j->error, fmt, ap);
    va_end(ap);
    return false;
}

static bool checked(char *out, size_t n, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(out, n, fmt, ap);
    va_end(ap);
    if (len < 0 || (size_t)len >= n) {
        if (n) out[0] = 0;
        return false;
    }
    return true;
}

// Reject traversal, device injection, FAT aliases and paths we cannot represent.
static bool safe_path(const char *p) {
    if (!p || strncmp(p, "sdmc:/", 6) || strlen(p) >= PATH_CAP) return false;
    p += 6;
    while (*p) {
        const char *end = strchr(p, '/');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (!n || n > 255 || p[n - 1] == '.' || p[n - 1] == ' ') return false;
        for (size_t i = 0; i < n; ++i)
            if ((unsigned char)p[i] < 32 || strchr(":\\*?\"<>|%", p[i])) return false;
        p += n;
        if (*p) p++;
    }
    return true;
}

static bool sd_path(char *out, size_t n, const char *p) {
    const char *vars[][2] = {{"%3DSX%", "/3ds"}, {"%TMP%", "/3ds/forge-store/tmp"},
                            {"%NDS%", "/nds"}, {"%FIRM%", "/luma/payloads"}};
    if (!p || !*p) return false;
    char tmp[PATH_CAP];
    if (!checked(tmp, sizeof tmp, "%s", p)) return false;
    for (unsigned i = 0; i < 4; i++) {
        char *at = strstr(tmp, vars[i][0]);
        if (at) {
            char replaced[PATH_CAP];
            *at = 0;
            if (!checked(replaced, sizeof replaced, "%s%s%s", tmp, vars[i][1], at + strlen(vars[i][0]))) return false;
            strcpy(tmp, replaced);
        }
    }
    return checked(out, n, "sdmc:%s", tmp) && safe_path(out) &&
           strncmp(out, TX_DIR, strlen(TX_DIR)) &&
           strcmp(out, DATA_DIR "/installed.json") && strcmp(out, DATA_DIR "/settings.json");
}

static bool make_dir(const char *path, bool track) {
    struct stat st;
    if (!stat(path, &st)) return S_ISDIR(st.st_mode);
    if (errno != ENOENT) return false;
    // Reserve the rollback record before creating anything.
    if (track && json_array_append_new(tx.dirs, json_string(path))) return false;
    if (!mkdir(path, 0777)) return true;
    if (track) json_array_remove(tx.dirs, json_array_size(tx.dirs) - 1);
    return false;
}

static bool mkdirs(const char *path, bool is_file) {
    char p[PATH_CAP];
    if (!safe_path(path) || !checked(p, sizeof p, "%s", path)) return false;
    for (char *slash = strchr(p + 6, '/'); slash; slash = strchr(slash + 1, '/')) {
        *slash = 0;
        bool ok = make_dir(p, tx.dirs != NULL);
        *slash = '/';
        if (!ok) return false;
    }
    if (!is_file && strlen(p) > 6) {
        if (p[strlen(p) - 1] == '/') p[strlen(p) - 1] = 0;
        return make_dir(p, tx.dirs != NULL);
    }
    return true;
}

static TxFile *tx_find(const char *path) {
    for (int i = 0; i < tx.count; ++i)
        if (!strcasecmp(tx.files[i].path, path)) return &tx.files[i]; // FAT is case insensitive
    return NULL;
}

static TxFile *tx_file(Job *j, const char *path) {
    if (!safe_path(path) || !strcmp(path, "sdmc:/") ||
        (!strncmp(path, DATA_DIR "/", strlen(DATA_DIR "/")) &&
         strncmp(path, DATA_DIR "/tmp/", strlen(DATA_DIR "/tmp/")) &&
         (!tx.cia_cache[0] || strcmp(path, tx.cia_cache)))) { fail(j, "Unsafe destination"); return NULL; }
    TxFile *e = tx_find(path);
    if (e) return e;
    if (tx.count == TX_LIMIT) { fail(j, "Archive file limit exceeded"); return NULL; }
    if (tx.count == tx.cap) {
        int cap = tx.cap ? tx.cap * 2 : 32;
        TxFile *files = realloc(tx.files, cap * sizeof *files);
        if (!files) { fail(j, "Out of memory"); return NULL; }
        tx.files = files; tx.cap = cap;
    }
    e = &tx.files[tx.count];
    memset(e, 0, sizeof *e);
    strcpy(e->path, path);
    snprintf(e->stage, sizeof e->stage, TX_DIR "/%04d.new", tx.count);
    snprintf(e->backup, sizeof e->backup, TX_DIR "/%04d.old", tx.count++);
    return e;
}

static const char *tx_read(const char *path) {
    TxFile *e = tx_find(path);
    return e ? (e->deleted ? NULL : e->published ? e->path : e->stage) : path;
}

static bool free_space(u64 need) {
#ifdef FORGE_INSTALL_HOST_TEST
    return host_free_space >= need;
#else
    FS_ArchiveResource r;
    return R_SUCCEEDED(FSUSER_GetArchiveResource(&r, SYSTEM_MEDIATYPE_SD)) &&
           (u64)r.freeClusters * r.clusterSize >= need + 1024 * 1024;
#endif
}

static bool durable_close(FILE *f) {
    bool ok = fflush(f) == 0;
    if (ok && fsync(fileno(f))) ok = false;
    if (fclose(f)) ok = false;
    return ok;
}

static bool tx_begin(Job *j) {
    memset(&tx, 0, sizeof tx);
    if (!mkdirs(DATA_DIR, false) || mkdir(TX_DIR, 0777))
        return fail(j, "Install recovery files exist or SD unavailable: " TX_DIR);
    tx.dirs = json_array();
    j->files = json_array();
    j->dirs = json_array();
    j->hashes = json_object();
    return tx.dirs && j->files && j->dirs && j->hashes;
}

static bool tx_finish(Job *j, bool success) {
    bool ok = true;
    for (int i = tx.count - 1; i >= 0; --i) {
        TxFile *e = &tx.files[i];
        if (!success) {
            if (e->published && remove(e->path) && errno != ENOENT) ok = false;
            if (e->had_old && rename(e->backup, e->path)) ok = false;
        } else if (e->had_old && remove(e->backup)) ok = false;
        if (remove(e->stage) && errno != ENOENT) ok = false;
    }
    if (!success) {
        for (size_t i = json_array_size(tx.dirs); i > 0; --i) {
            const char *dir = json_string_value(json_array_get(tx.dirs, i - 1));
            if (rmdir(dir) && errno != ENOENT && errno != ENOTEMPTY) ok = false;
        }
    }
    if (ok) {
        remove(TX_DIR "/recovery.json");
        if (rmdir(TX_DIR)) ok = false;
    }
    if (!ok) fail(j, "SD recovery required; backups retained in " TX_DIR);
    free(tx.files); json_decref(tx.dirs);
    memset(&tx, 0, sizeof tx);
    return ok;
}

static bool tx_publish(Job *j) {
    // Keep a human-readable recovery inventory on SD before the first live rename.
    json_t *manifest = json_array();
    if (!manifest) return fail(j, "Out of memory");
    for (int i = 0; i < tx.count; ++i) {
        TxFile *e = &tx.files[i];
        if (json_array_append_new(manifest, json_pack("{s:s,s:s,s:s,s:b}", "path", e->path,
              "stage", e->stage, "backup", e->backup, "deleted", e->deleted))) {
            json_decref(manifest); return fail(j, "Out of memory");
        }
    }
    FILE *journal = fopen(TX_DIR "/recovery.json", "wb");
    bool saved = journal && json_dumpf(manifest, journal, JSON_INDENT(2)) == 0;
    if (journal && !durable_close(journal)) saved = false;
    json_decref(manifest);
    if (!saved) return fail(j, "Cannot write recovery inventory");
    for (int i = 0; i < tx.count; ++i) {
        TxFile *e = &tx.files[i];
        if (j->cancel) return false;
        // Deleted intermediate downloads never modify a pre-existing file of the same name.
        if (e->deleted && !e->remove_live) continue;
        if (!strncmp(e->path, DATA_DIR "/tmp/", strlen(DATA_DIR "/tmp/"))) continue;
        struct stat st;
        if (!e->remove_live && (stat(e->stage, &st) || !S_ISREG(st.st_mode))) return fail(j, "Missing staged file");
        if (!mkdirs(e->path, true)) return fail(j, "Cannot create destination directory");
        if (!stat(e->path, &st)) {
            if (!S_ISREG(st.st_mode) || rename(e->path, e->backup)) return fail(j, "Cannot back up destination");
            e->had_old = true;
        } else if (errno != ENOENT) return fail(j, "Cannot inspect destination");
        if (e->remove_live) continue;
        if (rename(e->stage, e->path)) return fail(j, "Cannot publish staged file");
        e->published = true;
        if (json_array_append_new(j->files, json_string(e->path))) return fail(j, "Out of memory");
    }
    if (json_array_extend(j->dirs, tx.dirs)) return fail(j, "Out of memory");
    return true;
}

// ---- downloads -------------------------------------------------------------------------
typedef struct {
    Job *job;
    JobStep *step;
    FILE *f;
    char *mem;
    size_t mem_len, mem_cap;
    u64 base;
    u64 last_bytes;
    u64 last_tick;
    u64 written, expected;
} Dl;

static size_t on_write(char *data, size_t size, size_t nmemb, void *user) {
    Dl *d = user;
    if (size && nmemb > SIZE_MAX / size) return 0;
    size_t len = size * nmemb;
    if (d->job && d->job->cancel) return 0;
    if (d->mem) {
        if (len >= d->mem_cap - d->mem_len)
            return 0;
        size_t take = len;
        memcpy(d->mem + d->mem_len, data, take);
        d->mem_len += take;
        d->mem[d->mem_len] = 0;
        return len;
    }
    if (len > FILE_LIMIT - d->written || (d->expected && len > d->expected - d->written) || !free_space(len)) return 0;
    size_t wrote = fwrite(data, 1, len, d->f);
    d->written += wrote;
    return wrote;
}

static int on_progress(void *user, curl_off_t total, curl_off_t now, curl_off_t ut, curl_off_t un) {
    (void)ut;
    (void)un;
    Dl *d = user;
    if (quit || (d->job && d->job->cancel))
        return 1;
    if (!d->step)
        return 0;
    if (total > 0)
        d->step->progress = (float)now / total;
    d->job->done_bytes = d->base + now;
    u64 tick = svcGetSystemTick();
    if (tick - d->last_tick > SYSCLOCK_ARM11 / 2) {
        float secs = (tick - d->last_tick) / (float)SYSCLOCK_ARM11;
        float speed = (now - d->last_bytes) / secs;
        d->job->speed = d->job->speed > 0 ? d->job->speed * 0.6f + speed * 0.4f : speed;
        d->last_tick = tick;
        d->last_bytes = now;
    }
    return 0;
}

static CURLcode fetch(const char *url, Dl *d, char *err, size_t errn) {
    if (!url || strncmp(url, "https://", 8)) return CURLE_URL_MALFORMAT;
    CURL *c = curl_easy_init();
    if (!c)
        return CURLE_FAILED_INIT;
    char ebuf[CURL_ERROR_SIZE] = {0};
    network_options(c);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 0L); // progressing installations have no total deadline
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "forge.store/1.0 (Nintendo 3DS)");
    curl_easy_setopt(c, CURLOPT_CAINFO, "romfs:/cacert.pem");
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 25L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 64L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(c, CURLOPT_BUFFERSIZE, 128 * 1024L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_write);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, d);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, on_progress);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, d);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, ebuf);
    d->last_tick = svcGetSystemTick();
    CURLcode r = curl_easy_perform(c);
    if (r != CURLE_OK)
        network_error(err, errn, ebuf[0] ? ebuf : curl_easy_strerror(r));
    curl_easy_cleanup(c);
    return r;
}

static bool download_to_file(Job *j, JobStep *st, const char *url, const char *path, u64 expected) {
    if (expected > FILE_LIMIT || !free_space(expected)) return fail(j, "Insufficient SD space or file too large");
    TxFile *entry = tx_file(j, path);
    if (!entry) return false;
    entry->deleted = false;
    char partial[96];
    snprintf(partial, sizeof partial, "%s", entry->stage);
    FILE *f = fopen(partial, "wb");
    if (!f) {
        snprintf(j->error, sizeof j->error, "%.80s: %.40s", path + 5, strerror(errno));
        return false;
    }
    static char vbuf[256 * 1024];
    setvbuf(f, vbuf, _IOFBF, sizeof vbuf);
    Dl d = {.job = j, .step = st, .f = f, .base = j->done_bytes, .expected = expected};
    CURLcode r = fetch(url, &d, j->error, sizeof j->error);
    bool disk_ok = durable_close(f);
    if (r != CURLE_OK || !disk_ok || j->cancel || !d.written || (expected && d.written != expected)) {
        if (r == CURLE_OK) fail(j, "Incomplete download or unexpected size");
        if (!disk_ok)
            snprintf(j->error, sizeof j->error, "SD write failed");
        remove(partial);
        return false;
    }

    return true;
}

static bool release_url(char *out, size_t n, Job *j, const char *asset) {
    if (!j->app->repo || !asset || !*asset || !*j->tag) return false;
    CURL *c = curl_easy_init();
    if (!c) return false;
    char *tag = curl_easy_escape(c, j->tag, 0), *name = curl_easy_escape(c, asset, 0);
    bool ok = tag && name && checked(out, n, "https://github.com/%s/releases/download/%s/%s", j->app->repo, tag, name);
    curl_free(tag); curl_free(name); curl_easy_cleanup(c);
    return ok;
}

static void load_sums(Job *j) {
    sums[0] = 0;
    json_t *a = app_asset(j->app, j->release, "sums");
    const char *name = json_string_value(json_object_get(a, "name"));
    if (!name)
        return;
    char url[2048];
    if (!release_url(url, sizeof url, j, name)) return;
    Dl d = {.job = j, .mem = sums, .mem_cap = sizeof sums};
    char err[96];
    if (fetch(url, &d, err, sizeof err) != CURLE_OK)
        sums[0] = 0;
}

static bool sha256_file(const char *path, char hex[65], JobStep *st, volatile bool *cancel) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return false; }
    long total = ftell(f);
    if (total < 0 || fseek(f, 0, SEEK_SET)) { fclose(f); return false; }
    static u8 buf[128 * 1024];
    mbedtls_sha256_context c;
    mbedtls_sha256_init(&c);
    mbedtls_sha256_starts_ret(&c, 0);
    long done = 0;
    size_t r;
    while ((r = fread(buf, 1, sizeof buf, f)) > 0 && !*cancel) {
        mbedtls_sha256_update_ret(&c, buf, r);
        done += r;
        st->progress = total ? (float)done / total : 1;
    }
    u8 out[32];
    mbedtls_sha256_finish_ret(&c, out);
    mbedtls_sha256_free(&c);
    bool ok = !ferror(f) && done == total && !*cancel;
    fclose(f);
    for (int i = 0; i < 32; i++)
        sprintf(hex + i * 2, "%02x", out[i]);
    return ok;
}

// ---- CIA ----------------------------------------------------------------------------------
static bool install_cia(Job *j, JobStep *st, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(j->error, sizeof j->error, "%s", strerror(errno));
        return false;
    }
    if (fseek(f, 0, SEEK_END)) { fclose(f); return false; }
    long total = ftell(f);
    if (total <= 0 || fseek(f, 0, SEEK_SET)) { fclose(f); return false; }
    Handle h;
    Result r = AM_StartCiaInstallOverwrite(&h, MEDIATYPE_SD);
    if (R_FAILED(r))
        r = AM_StartCiaInstall(MEDIATYPE_SD, &h);
    if (R_FAILED(r)) {
        fclose(f);
        snprintf(j->error, sizeof j->error, "AM 0x%08lX", r);
        return false;
    }
    static u8 buf[256 * 1024];
    u64 off = 0;
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
        u32 written = 0;
        r = FSFILE_Write(h, &written, off, buf, n, 0);
        if (R_FAILED(r) || j->cancel || written != n) {
            if (written != n)
                r = -1;
            break;
        }
        off += n;
        st->progress = (float)off / total;
    }
    if (ferror(f) || off != (u64)total)
        r = -1;
    fclose(f);
    if (R_FAILED(r) || j->cancel) {
        AM_CancelCIAInstall(h);
        if (!j->cancel)
            snprintf(j->error, sizeof j->error, "CIA 0x%08lX", r);
        return false;
    }
    r = AM_FinishCiaInstall(h);
    if (R_FAILED(r)) {
        AM_CancelCIAInstall(h);
        snprintf(j->error, sizeof j->error, "CIA 0x%08lX", r);
        return false;
    }
    return true;
}

// Query AM before touching a title. An update needs a retained original CIA;
// arbitrary installed titles cannot be reconstructed from their metadata.
static bool cia_info(const char *path, AM_TitleInfo *info) {
    Handle file;
    if (R_FAILED(FSUSER_OpenFileDirectly(&file, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""),
                 fsMakePath(PATH_ASCII, path + 5), FS_OPEN_READ, 0))) return false;
    Result r = AM_GetCiaFileInfo(MEDIATYPE_SD, info, file);
    FSFILE_Close(file);
    return R_SUCCEEDED(r);
}

static bool copy_file(const char *source, const char *dest) {
    FILE *in = fopen(source, "rb");
    if (!in) return false;
    FILE *out = fopen(dest, "wb");
    if (!out) { fclose(in); return false; }
    char buf[16384];
    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof buf, in)))
        if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
    if (ferror(in)) ok = false;
    if (fclose(in)) ok = false;
    if (!durable_close(out)) ok = false;
    return ok;
}

static bool title_present(u64 id, bool *present) {
    *present = false;
    u32 count = 0, got = 0;
    if (R_FAILED(AM_GetTitleCount(MEDIATYPE_SD, &count)) || count > 8192) return false;
    u64 *titles = calloc(count ? count : 1, sizeof *titles);
    if (!titles) return false;
    Result r = count ? AM_GetTitleList(&got, MEDIATYPE_SD, count, titles) : 0;
    for (u32 i = 0; i < got; ++i) if (titles[i] == id) *present = true;
    free(titles);
    return R_SUCCEEDED(r) && got == count;
}

static void tx_abandon(void) {
    // Preserve all on-disk recovery evidence while releasing the in-memory plan.
    free(tx.files); json_decref(tx.dirs); memset(&tx, 0, sizeof tx);
}

static bool cia_prepare(Job *j) {
    if (!tx.cia_path[0]) return true;
    TxFile *source = tx_find(tx.cia_path);
    if (!source) return fail(j, "CIA must be staged by this job");
    char input[96];
    strcpy(input, source->stage);
    AM_TitleInfo info;
    if (!cia_info(input, &info) || (info.titleID >> 32) != 0x00040000)
        return fail(j, "Invalid CIA or unsupported non-application title");
    tx.title_id = info.titleID;
    if (!title_present(tx.title_id, &tx.had_title)) return fail(j, "Cannot inspect installed titles");
    snprintf(tx.cia_cache, sizeof tx.cia_cache, DATA_DIR "/cia-cache/%016llx.cia", (unsigned long long)info.titleID);
    if (tx.had_title) {
        AM_TitleInfo backup, current;
        if (!cia_info(tx.cia_cache, &backup) || backup.titleID != info.titleID ||
            R_FAILED(AM_GetTitleInfo(MEDIATYPE_SD, 1, &tx.title_id, &current)) || backup.version != current.version)
            return fail(j, "Safe CIA update needs the original package; use FBI for this existing title");
    }
    struct stat st;
    if (stat(input, &st) || !free_space((u64)st.st_size)) return fail(j, "Insufficient SD space for CIA recovery copy");
    TxFile *cache = tx_file(j, tx.cia_cache);
    if (!cache || !copy_file(input, cache->stage)) return fail(j, "Cannot stage CIA recovery copy");
    return true;
}

static bool cia_restore(Job *j) {
    if (!tx.cia_attempted) return true;
    if (!tx.had_title) {
        bool present;
        if (!title_present(tx.title_id, &present)) return false;
        return !present || R_SUCCEEDED(AM_DeleteUserTitle(MEDIATYPE_SD, tx.title_id));
    }
    TxFile *cache = tx_find(tx.cia_cache);
    if (!cache || !cache->had_old) return false;
    Job restore = {0};
    JobStep step = {0};
    bool ok = install_cia(&restore, &step, cache->backup);
    if (!ok) fail(j, "CIA restore failed; original package retained in " TX_DIR);
    return ok;
}

// ---- archives ----------------------------------------------------------------------------------
/** Tiny matcher for the catalog's input patterns: ^ anchor, .* wildcard, \. escapes. */
static bool match_here(const char *re, const char *s) {
    if (!*re || *re == '$')
        return !*re || !*s;
    if (re[0] == '.' && re[1] == '*') {
        for (const char *t = s;; t++) {
            if (match_here(re + 2, t))
                return true;
            if (!*t)
                return false;
        }
    }
    char want = re[0];
    int adv = 1;
    if (want == '\\' && re[1]) {
        want = re[1];
        adv = 2;
    }
    if (*s && (want == '.' && adv == 1 ? true : *s == want))
        return match_here(re + adv, s + 1);
    return false;
}

static bool pattern_match(const char *re, const char *s) {
    if (!re || !*re)
        return true;
    if (*re == '^')
        return match_here(re + 1, s);
    for (; *s; s++)
        if (match_here(re, s))
            return true;
    return false;
}

static bool extract(Job *j, JobStep *st, const char *archive_path, const char *input, const char *out) {
    if (!archive_path || !safe_path(out)) return fail(j, "Invalid archive path");
    struct archive *a = archive_read_new();
    if (!a) return fail(j, "Out of memory");
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);
    bool ok = archive_read_open_filename(a, archive_path, 64 * 1024) == ARCHIVE_OK;
    if (!ok) { fail(j, "Cannot open archive"); archive_read_free(a); return false; }
    bool to_dir = out[strlen(out) - 1] == '/';
    // Universal-DB also names folder outputs without a trailing slash.
    const char *prefix = input && input[0] == '^' ? input + 1 : input;
    size_t prefix_len = prefix && !strpbrk(prefix, "\\.*+?[]()$|") ? strlen(prefix) : 0;
    struct archive_entry *e;
    static char buf[128 * 1024];
    int files = 0, header = ARCHIVE_EOF;
    struct stat ast;
    if (stat(archive_path, &ast)) ok = false;
    while (ok && !j->cancel && (header = archive_read_next_header(a, &e)) == ARCHIVE_OK) {
        const char *name = archive_entry_pathname(e);
        char validated[PATH_CAP];
        if (!name || !*name || !checked(validated, sizeof validated, "sdmc:/%s", name) || !safe_path(validated) ||
            archive_entry_hardlink(e) || archive_entry_symlink(e) ||
            (archive_entry_filetype(e) != AE_IFREG && archive_entry_filetype(e) != AE_IFDIR)) {
            ok = fail(j, "Unsafe archive entry"); break;
        }
        if (archive_entry_filetype(e) == AE_IFDIR || !pattern_match(input, name)) {
            if (archive_read_data_skip(a) < ARCHIVE_OK) ok = fail(j, "Damaged archive");
            continue;
        }
        char dest[PATH_CAP];
        bool folder = !to_dir && prefix_len && !strncmp(name, prefix, prefix_len) && name[prefix_len] == '/';
        bool path_ok = to_dir ? checked(dest, sizeof dest, "%s%s", out, name) :
                       folder ? checked(dest, sizeof dest, "%s/%s", out, name + prefix_len + 1) :
                                checked(dest, sizeof dest, "%s", out);
        if (!path_ok || !safe_path(dest) || (!to_dir && !folder && files)) {
            ok = fail(j, "Archive output ambiguous or path too long"); break;
        }
        la_int64_t size = archive_entry_size(e);
        if (size < 0 || (u64)size > FILE_LIMIT || !free_space((u64)size)) {
            ok = fail(j, "Insufficient SD space or archive entry too large"); break;
        }
        TxFile *entry = tx_file(j, dest);
        if (!entry) { ok = false; break; }
        entry->deleted = false;
        FILE *f = fopen(entry->stage, "wb");
        if (!f) { ok = fail(j, "Cannot stage archive entry"); break; }
        la_ssize_t n = 0;
        u64 written = 0;
        while (!j->cancel && (n = archive_read_data(a, buf, sizeof buf)) > 0) {
            if ((u64)n > (u64)size - written || fwrite(buf, 1, (size_t)n, f) != (size_t)n) { ok = false; break; }
            written += n;
        }
        // Always close even when archive_read_data failed.
        bool closed = durable_close(f);
        if (n < 0 || !closed || written != (u64)size || j->cancel) ok = false;
        if (!ok) { fail(j, "Archive/SD write interrupted"); break; }
        files++;
        st->progress = ast.st_size ? fminf(1, archive_filter_bytes(a, -1) / (float)ast.st_size) : 0.5f;
    }
    if (header != ARCHIVE_EOF || j->cancel) ok = false;
    archive_read_free(a);
    if (!ok && !j->error[0]) fail(j, "Damaged or interrupted archive");
    if (ok && !files) fail(j, "No matching files in archive");
    return ok && files > 0;
}

// ---- job ----------------------------------------------------------------------------------------
static const char *str(json_t *o, const char *k) { return json_string_value(json_object_get(o, k)); }

static bool run_step(Job *j, JobStep *st) {
    json_t *s = st->raw;
    const char *type = str(s, "type");
    char path[PATH_CAP], path2[PATH_CAP], url[2048];
    if (!type) return fail(j, "Missing install operation");
    if (!strcmp(type, "mkdir")) {
        if (!sd_path(path, sizeof path, str(s, "directory"))) return fail(j, "Invalid or oversized catalog path");
        return mkdirs(path, false) || fail(j, "Cannot create directory");
    }
    if (!strcmp(type, "downloadUrl")) {
        const char *direct = str(s, "url");
        if (direct && *direct) {
            if (!checked(url, sizeof url, "%s", direct)) return fail(j, "URL too long");
        }
        else {
            const char *repo = str(s, "repo"), *match = str(s, "match");
            if (!repo || !match) {
                snprintf(j->error, sizeof j->error, "Missing release source");
                return false;
            }
            if (!checked(url, sizeof url, "https://api.github.com/repos/%s/releases/latest", repo)) return fail(j, "URL too long");
            char *body = calloc(1, 128 * 1024);
            if (!body)
                return false;
            Dl d = {.job = j, .mem = body, .mem_cap = 128 * 1024};
            bool ok = fetch(url, &d, j->error, sizeof j->error) == CURLE_OK;
            json_t *rel = ok ? json_loads(body, 0, NULL) : NULL;
            free(body);
            url[0] = 0;
            size_t ai;
            json_t *asset;
            json_array_foreach(json_object_get(rel, "assets"), ai, asset) {
                const char *name = str(asset, "name");
                if (name && pattern_match(match, name)) {
                    const char *source = str(asset, "browser_download_url");
                    if (source) checked(url, sizeof url, "%s", source);
                    break;
                }
            }
            json_decref(rel);
            if (!url[0]) {
                snprintf(j->error, sizeof j->error, "Release asset not found");
                return false;
            }
        }
        if (!sd_path(path, sizeof path, str(s, "output"))) return fail(j, "Invalid or oversized catalog path");
        json_int_t size = json_integer_value(json_object_get(s, "size"));
        if (size < 0) return fail(j, "Invalid download size");
        return download_to_file(j, st, url, path, (u64)size);
    }
    if (!strcmp(type, "downloadRelease")) {
        json_t *asset = app_asset(j->app, j->release, str(s, "asset"));
        if (!release_url(url, sizeof url, j, str(asset, "name"))) return fail(j, "Missing release asset or URL too long");
        if (!sd_path(path, sizeof path, str(s, "output"))) return fail(j, "Invalid or oversized catalog path");
        json_int_t size = json_integer_value(json_object_get(asset, "size"));
        if (size < 0) return fail(j, "Invalid download size");
        return download_to_file(j, st, url, path, (u64)size);
    }
    if (!strcmp(type, "verifySha256")) {
        if (!sd_path(path, sizeof path, str(s, "file"))) return fail(j, "Invalid or oversized catalog path");
        if (!sums[0])
            load_sums(j);
        const char *name = strrchr(path, '/');
        name = name ? name + 1 : path;
        // Verification must bind the digest to this download's original asset name.
        size_t si;
        json_t *step;
        json_array_foreach(j->script ? j->script : app_install_steps(j->app, j->cia), si, step) {
            if (!str(step, "type") || strcmp(str(step, "type"), "downloadRelease"))
                continue;
            if (!sd_path(path2, sizeof path2, str(step, "output"))) return fail(j, "Invalid or oversized catalog path");
            if (!strcmp(path, path2)) {
                const char *asset_name = str(app_asset(j->app, j->release, str(step, "asset")), "name");
                if (asset_name && *asset_name)
                    name = asset_name;
                break;
            }
        }
        char hex[65];
        const char *staged = tx_read(path);
        if (!staged || !sha256_file(staged, hex, st, &j->cancel)) {
            snprintf(j->error, sizeof j->error, "Cannot read file for SHA-256");
            return false;
        }
        if (!sums[0]) {
            snprintf(j->error, sizeof j->error, "SHA256SUMS unavailable; verification incomplete");
            return false;
        }
        for (const char *line = sums; *line;) {
            const char *end = strchr(line, '\n');
            if (!end)
                end = line + strlen(line);
            if (end - line > 65 && !strncasecmp(line, hex, 64) && (line[64] == ' ' || line[64] == '\t')) {
                const char *filename = line + 64;
                while (filename < end && (*filename == ' ' || *filename == '\t'))
                    filename++;
                if (*filename == '*')
                    filename++;
                const char *last = end;
                if (last > filename && last[-1] == '\r')
                    last--;
                if ((size_t)(last - filename) == strlen(name) && !memcmp(filename, name, last - filename)) {
                    snprintf(st->result, sizeof st->result, "%s", T("install.step.verified"));
                    return true;
                }
            }
            line = *end ? end + 1 : end;
        }
        snprintf(j->error, sizeof j->error, "SHA-256 ≠ %s", name);
        return false;
    }
    if (!strcmp(type, "extractFile")) {
        if (!sd_path(path, sizeof path, str(s, "file"))) return fail(j, "Invalid or oversized catalog path");
        if (!sd_path(path2, sizeof path2, str(s, "output"))) return fail(j, "Invalid or oversized catalog path");
        return extract(j, st, tx_read(path), str(s, "input"), path2);
    }
    if (!strcmp(type, "installCia")) {
        if (!sd_path(path, sizeof path, str(s, "file"))) return fail(j, "Invalid or oversized catalog path");
        if (tx.cia_path[0]) return fail(j, "Multiple CIA installs need separate transactions");
        strcpy(tx.cia_path, path);
        tx.cia_step = st;
        return true; // AM runs only after every download, hash and extraction succeeds
    }
    if (!strcmp(type, "deleteFile")) {
        if (!sd_path(path, sizeof path, str(s, "file"))) return fail(j, "Invalid or oversized catalog path");
        TxFile *e = tx_find(path);
        // Catalog cleanup can only delete this transaction's intermediates.
        if (e) e->deleted = true;
        return true;
    }
    if (!strcmp(type, "rmdir")) {
        if (!sd_path(path, sizeof path, str(s, "directory"))) return fail(j, "Invalid or oversized catalog path");
        // Never remove pre-existing user directories. Empty created directories are
        // removed by rollback; cleanup is intentionally non-recursive.
        return true;
    }
    if (!strcmp(type, "move")) {
        if (!sd_path(path, sizeof path, str(s, "old"))) return fail(j, "Invalid or oversized catalog path");
        if (!sd_path(path2, sizeof path2, str(s, "new"))) return fail(j, "Invalid or oversized catalog path");
        TxFile *old = tx_find(path);
        if (!old || old->deleted) return fail(j, "Move source is not staged by this job");
        char source[96];
        strcpy(source, old->stage);
        TxFile *dest = tx_file(j, path2);
        if (!dest) return false;
        if (!strcasecmp(path, path2)) return true;
        if (rename(source, dest->stage)) return fail(j, "Cannot move staged file");
        tx_find(path)->deleted = true;
        dest->deleted = false;
        return true;
    }
    if (!strcmp(type, "requireFile") || !strcmp(type, "checkFile")) {
        json_t *req = NULL;
        size_t i;
        json_t *r;
        json_array_foreach(json_object_get(j->app->j, "requires"), i, r) if (str(r, "id") && str(s, "ref") && !strcmp(str(r, "id"), str(s, "ref"))) req = r;
        const char *p = str(req, "path");
        if (!p) {
            st->state = STEP_WARN;
            snprintf(st->result, sizeof st->result, "%s", LJ(json_object_get(req, "help")));
            j->warnings++;
            return true;
        }
        if (app_requirement_met(j->app, req)) {
            snprintf(st->result, sizeof st->result, "%s", T("install.step.found"));
        } else {
            st->state = STEP_WARN;
            tvars(st->result, sizeof st->result, T("install.step.missing"), "path", p, NULL, NULL);
            j->warnings++;
        }
        return true;
    }
    if (!strcmp(type, "dumpDsp")) {
        if (path_exists("sdmc:/3ds/dspfirm.cdc")) {
            snprintf(st->result, sizeof st->result, "%s", T("install.step.dspExists"));
        } else {
            if (!dsp_dump(st->result, sizeof st->result)) {
                st->state = STEP_WARN;
                j->warnings++;
            } else
                snprintf(st->result, sizeof st->result, "%s", T("settings.dspDone"));
        }
        return true;
    }
    snprintf(j->error, sizeof j->error, "Unsupported install step: %.80s", type);
    return false;
}

static void finish_record(Job *j) {
    // The JSON record belongs to the main thread. Retain backups until its
    // synchronous commit acknowledges success; shutdown can still cancel here.
    __atomic_store_n(&j->commit_pending, true, __ATOMIC_RELEASE);
    while (!__atomic_load_n(&j->commit_ack, __ATOMIC_ACQUIRE) && !quit)
        svcSleepThread(1000000);
    bool saved = __atomic_load_n(&j->commit_ack, __ATOMIC_ACQUIRE) && j->commit_ok;
    __atomic_store_n(&j->commit_pending, false, __ATOMIC_RELEASE);
    bool restored = saved || cia_restore(j);
    bool cleaned = restored && tx_finish(j, saved);
    if (!restored) tx_abandon();
    if (saved && j->uninstall) {
        json_t *dirs = json_object_get(j->record, "dirs");
        for (size_t i = json_array_size(dirs); i > 0; --i) {
            const char *dir = json_string_value(json_array_get(dirs, i - 1));
            if (safe_path(dir) && strcmp(dir, "sdmc:/") && strcmp(dir, "sdmc:/3ds") && strcmp(dir, DATA_DIR)) rmdir(dir);
        }
    }
    if (saved && !cleaned) j->warnings++;
    j->progress = saved ? 1 : j->progress;
    j->state = saved ? JOB_DONE : (j->cancel && cleaned ? JOB_CANCELLED : JOB_FAILED);
}

static bool hash_published(Job *j) {
    JobStep step = {0};
    for (int i = 0; i < tx.count; ++i) {
        TxFile *e = &tx.files[i];
        if (!e->published) continue;
        char hash[65];
        if (!sha256_file(e->path, hash, &step, &j->cancel) ||
            json_object_set_new(j->hashes, e->path, json_string(hash)))
            return fail(j, "Cannot record installed file integrity");
    }
    return true;
}

static u64 record_title(json_t *record) {
    size_t i; json_t *v;
    json_array_foreach(json_object_get(record, "files"), i, v) {
        const char *p = json_string_value(v);
        if (!p || strncmp(p, "title:", 6) || strlen(p) != 22) continue;
        char *end; u64 id = strtoull(p + 6, &end, 16);
        if (!*end && (id >> 32) == 0x00040000) return id;
    }
    return 0;
}

static void run_uninstall(Job *j) {
    j->state = JOB_RUNNING;
    if (!tx_begin(j)) { if (tx.dirs) tx_finish(j, false); j->state = JOB_FAILED; return; }
    tx.title_id = record_title(j->record);
    if (tx.title_id) {
        snprintf(tx.cia_cache, sizeof tx.cia_cache, DATA_DIR "/cia-cache/%016llx.cia", (unsigned long long)tx.title_id);
        AM_TitleInfo info, current;
        if (!title_present(tx.title_id, &tx.had_title)) { fail(j, "Cannot inspect installed titles"); goto failed; }
        if (tx.had_title) {
            if (R_FAILED(AM_GetTitleInfo(MEDIATYPE_SD, 1, &tx.title_id, &current)) ||
                !cia_info(tx.cia_cache, &info) || info.titleID != tx.title_id || info.version != current.version) {
                fail(j, "Original CIA required for safe uninstall"); goto failed;
            }
            tx.had_title = true;
        }
    }
    json_t *hashes = json_object_get(j->record, "hashes"), *v;
    size_t i;
    JobStep step = {0};
    json_array_foreach(json_object_get(j->record, "files"), i, v) {
        const char *path = json_string_value(v);
        const char *expected = path ? json_string_value(json_object_get(hashes, path)) : NULL;
        if (!safe_path(path)) continue;
        struct stat info;
        if (stat(path, &info)) { if (errno == ENOENT) continue; fail(j, "Cannot inspect installed file"); goto failed; }
        char hash[65];
        if (!expected || strlen(expected) != 64 || !S_ISREG(info.st_mode) ||
            !sha256_file(path, hash, &step, &j->cancel) || strcmp(hash, expected)) {
            j->warnings++; // legacy inventory or modified file: preserve it
            if (j->cancel) goto failed;
            continue;
        }
        TxFile *e = tx_file(j, path);
        if (!e) goto failed;
        e->remove_live = true;
    }
    if (tx.had_title && !tx_find(tx.cia_cache)) { fail(j, "CIA recovery copy changed; preserving title"); goto failed; }
    if (j->cancel || !tx_publish(j)) goto failed;
    if (tx.had_title) {
        tx.cia_attempted = true;
        if (R_FAILED(AM_DeleteUserTitle(MEDIATYPE_SD, tx.title_id))) {
            fail(j, "Cannot remove CIA title");
            if (cia_restore(j)) tx_finish(j, false);
            else tx_abandon();
            j->state = JOB_FAILED; return;
        }
    }
    finish_record(j);
    return;
failed:
    { bool restored = tx_finish(j, false); j->state = j->cancel && restored ? JOB_CANCELLED : JOB_FAILED; }
}

static void run(Job *j) {
    j->state = JOB_RUNNING;
    sums[0] = 0;
    if (!tx_begin(j)) {
        if (tx.dirs) tx_finish(j, false);
        j->state = JOB_FAILED; return;
    }
    float *weights = j->weight, total = 0;
    for (int i = 0; i < j->nsteps; i++) {
        weights[i] =
            j->steps[i].bytes
                ? 1 + (float)json_integer_value(json_object_get(app_asset(j->app, j->release, str(j->steps[i].raw, "asset")), "size")) /
                          1048576.0f
                : (!strcmp(str(j->steps[i].raw, "type"), "installCia") ? 3 : 0.4f);
        total += weights[i];
    }
    j->wtotal = total;
    float before = 0;
    for (int i = 0; i < j->nsteps && !j->cancel; i++) {
        JobStep *st = &j->steps[i];
        j->cur = i;
        st->state = STEP_ACTIVE;
        st->progress = 0;
        if (!net_ok && st->bytes) {
            snprintf(j->error, sizeof j->error, "Wi-Fi");
            st->state = STEP_ERROR;
            tx_finish(j, false);
            j->state = JOB_FAILED;
            return;
        }
        // Progress is published from inside long steps through st->progress.
        bool ok = run_step(j, st);
        if (j->cancel)
            break;
        if (!ok) {
            st->state = STEP_ERROR;
            memcpy(st->result, j->error, sizeof st->result - 1);
            tx_finish(j, false);
            j->state = JOB_FAILED;
            return;
        }
        if (st->state == STEP_ACTIVE)
            st->state = STEP_DONE;
        st->progress = 1;
        before += weights[i];
        j->progress = before / total;
    }
    if (j->cancel || !cia_prepare(j) || !tx_publish(j)) {
        bool restored = tx_finish(j, false);
        j->state = j->cancel && restored ? JOB_CANCELLED : JOB_FAILED;
        return;
    }
    if (!hash_published(j)) {
        tx_finish(j, false); j->state = j->cancel ? JOB_CANCELLED : JOB_FAILED; return;
    }
    if (tx.cia_path[0]) {
        tx.cia_attempted = true;
        if (!install_cia(j, tx.cia_step, tx.cia_cache)) {
            bool restored = cia_restore(j);
            if (restored) restored = tx_finish(j, false);
            else tx_abandon();
            j->state = j->cancel && restored ? JOB_CANCELLED : JOB_FAILED; return;
        }
        char title[32];
        snprintf(title, sizeof title, "title:%016llx", (unsigned long long)tx.title_id);
        if (json_array_append_new(j->files, json_string(title))) {
            if (cia_restore(j)) tx_finish(j, false);
            else tx_abandon();
            j->state = JOB_FAILED; return;
        }
    }
    finish_record(j);

}

static void worker_main(void *arg) {
    (void)arg;
    while (!quit) {
        LightEvent_Wait(&wake);
        for (;;) {
            Job *next = NULL;
            LightLock_Lock(&lock);
            for (int i = 0; i < njobs; i++)
                if (jobs[i].state == JOB_QUEUED) {
                    next = &jobs[i];
                    break;
                }
            LightLock_Unlock(&lock);
            if (!next || quit)
                break;
            if (next->cancel) {
                next->state = JOB_CANCELLED;
                continue;
            }
            if (next->uninstall) run_uninstall(next);
            else run(next);
        }
    }
}

// ---- public -------------------------------------------------------------------------------------
void install_init(void) {
#ifndef FORGE_INSTALL_HOST_TEST
    http_cache_init();
#endif
    amInit();
    soc_buf = memalign(0x1000, SOC_SIZE);
    net_ok = soc_buf && R_SUCCEEDED(socInit(soc_buf, SOC_SIZE));
    curl_global_init(CURL_GLOBAL_DEFAULT);
    LightLock_Init(&lock);
    LightEvent_Init(&wake, RESET_ONESHOT);
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    worker = threadCreate(worker_main, NULL, 64 * 1024, prio + 1, -2, false);
}

void install_stop(void) {
    for (int i = 0; i < njobs; i++)
        jobs[i].cancel = true;
    quit = true;
    LightEvent_Signal(&wake);
#ifndef FORGE_INSTALL_HOST_TEST
    reader_stop();
#endif
}

void install_exit(void) {
    install_stop();
#ifdef FORGE_INSTALL_HOST_TEST
    if (worker) {
        threadJoin(worker, U64_MAX);
        threadFree(worker);
    }
    reader_exit();
#else
    thread_reap(&worker);
    reader_exit();
    // Sockets, curl and the job records stay while any worker of the process is still
    // running past the shutdown deadline; the kernel reclaims them with the process.
    if (EXIT_ABANDONED) return;
#endif
    curl_global_cleanup();
    if (net_ok)
        socExit();
    free(soc_buf);
    soc_buf = NULL;
    for (int i = 0; i < njobs; ++i) {
        json_decref(jobs[i].files); json_decref(jobs[i].dirs); json_decref(jobs[i].hashes);
        json_decref(jobs[i].record); json_decref(jobs[i].release); json_decref(jobs[i].script);
    }
    amExit();
}

static const char *step_key(const char *type) {
    if (!strcmp(type, "mkdir"))
        return "install.step.mkdir";
    if (!strcmp(type, "rmdir"))
        return "install.step.rmdir";
    if (!strcmp(type, "downloadRelease") || !strcmp(type, "downloadUrl"))
        return "install.step.download";
    if (!strcmp(type, "verifySha256"))
        return "install.step.verify";
    if (!strcmp(type, "extractFile"))
        return "install.step.extract";
    if (!strcmp(type, "installCia"))
        return "install.step.installCia";
    if (!strcmp(type, "deleteFile"))
        return "install.step.delete";
    if (!strcmp(type, "move"))
        return "install.step.move";
    if (!strcmp(type, "requireFile"))
        return "install.step.require";
    if (!strcmp(type, "checkFile"))
        return "install.step.check";
    if (!strcmp(type, "dumpDsp"))
        return "install.step.dsp";
    return NULL;
}

Job *install_start(App *a, bool cia, bool experimental) {
    if (!a || !worker) {
        toast("Installer unavailable", DANGER);
        return NULL;
    }
    if (install_busy(a))
        return install_job(a);
    json_t *script = app_install_steps(a, cia);
    if (!json_array_size(script) || json_array_size(script) > 28 || json_is_true(json_object_get(a->j, "nativeUnsupported"))) {
        toast(S.lang == 0 ? "Este instalador necesita opciones adicionales." : "This installer requires additional choices.", AMBER);
        return NULL;
    }
    json_t *selected = app_release(a, experimental);
    const char *selected_tag = str(selected, "tag");
    if (!selected_tag || strlen(selected_tag) >= sizeof jobs[0].tag) {
        toast("Missing release or oversized version", DANGER); return NULL;
    }
    if (!IS_NEW3DS && (json_is_false(json_object_get(selected, "o3ds")) ||
        (!json_object_get(selected, "o3ds") && !a->old_ok))) {
        toast("This release requires New Nintendo 3DS", AMBER); return NULL;
    }
    u64 needed = 0;
    size_t si;
    json_t *step;
    json_array_foreach(script, si, step) {
        const char *type = str(step, "type");
        if (!type || !step_key(type)) {
            toast("Unsupported install step", DANGER);
            return NULL;
        }
        if (!strcmp(type, "downloadRelease") || !strcmp(type, "downloadUrl")) {
            json_t *asset = !strcmp(type, "downloadUrl") ? step : app_asset(a, selected, str(step, "asset"));
            json_int_t size = json_integer_value(json_object_get(asset, "size"));
            if (size < 0 || (u64)size > FILE_LIMIT) { toast("Unsupported download size", DANGER); return NULL; }
            needed += (u64)size;
        }

    }
    if (!free_space(needed)) { toast("Insufficient SD space", DANGER); return NULL; }
    LightLock_Lock(&lock);
    int slot = njobs;
    if (njobs == MAX_JOBS) { // reuse a finished slot without moving an active Job
        for (int i = 0; i < njobs; i++)
            if (jobs[i].state >= JOB_DONE) {
                json_decref(jobs[i].files);
                json_decref(jobs[i].dirs);
                json_decref(jobs[i].hashes);
                json_decref(jobs[i].record);
                json_decref(jobs[i].release);
                json_decref(jobs[i].script);
                slot = i;
                break;
            }
    }
    if (slot == MAX_JOBS) {
        LightLock_Unlock(&lock);
        return NULL;
    }
    Job *j = &jobs[slot];
    memset(j, 0, sizeof *j);
    j->serial = ++job_serial;
    j->app = a;
    j->cia = cia;
    j->experimental = app_release_experimental(a, selected);
    j->release = json_deep_copy(selected);
    j->script = json_deep_copy(script);
    if (!j->release || !j->script) {
        json_decref(j->release); json_decref(j->script);
        j->release = j->script = NULL; j->state = JOB_FAILED;
        LightLock_Unlock(&lock); return NULL;
    }
    const char *tag = str(j->release, "tag");
    snprintf(j->tag, sizeof j->tag, "%s", tag ? tag : "unknown");
    size_t i;
    json_t *s;
    json_array_foreach(j->script, i, s) {
        if (j->nsteps >= 28)
            break;
        const char *type = str(s, "type");
        const char *key = step_key(type);
        if (!key)
            continue;
        JobStep *st = &j->steps[j->nsteps++];
        st->raw = s;
        st->bytes = !strcmp(type, "downloadRelease") || !strcmp(type, "downloadUrl");
        char tmp[160];
        if (st->bytes) {
            json_t *asset = !strcmp(type, "downloadUrl") ? s : app_asset(a, j->release, str(s, "asset"));
            snprintf(tmp, sizeof tmp, "%s", str(asset, "name") ? str(asset, "name") : "download");
            tvars(st->label, sizeof st->label, T(key), "name", tmp, NULL, NULL);
            j->total_bytes += json_integer_value(json_object_get(asset, "size"));
        } else if (!strcmp(type, "mkdir") || !strcmp(type, "rmdir")) {
            tvars(st->label, sizeof st->label, T(key), "path", str(s, "directory"), NULL, NULL);
        } else if (!strcmp(type, "deleteFile") || !strcmp(type, "extractFile")) {
            const char *f = str(s, "file");
            tvars(st->label, sizeof st->label, T(key), "name", f ? (strrchr(f, '/') ? strrchr(f, '/') + 1 : f) : "", NULL, NULL);
        } else if (!strcmp(type, "move")) {
            const char *f = str(s, "new");
            tvars(st->label, sizeof st->label, T(key), "name", f ? (strrchr(f, '/') ? strrchr(f, '/') + 1 : f) : "", NULL, NULL);
        } else if (!strcmp(type, "requireFile") || !strcmp(type, "checkFile")) {
            json_t *req = NULL;
            size_t k;
            json_t *r;
            json_array_foreach(json_object_get(a->j, "requires"), k, r) if (str(r, "id") && str(s, "ref") && !strcmp(str(r, "id"), str(s, "ref"))) req = r;
            tvars(st->label, sizeof st->label, T(key), "label", LJ(json_object_get(req, "label")), NULL, NULL);
        } else {
            snprintf(st->label, sizeof st->label, "%s", T(key));
        }
    }
    j->state = JOB_QUEUED;
    if (slot == njobs)
        njobs++;
    LightLock_Unlock(&lock);
    LightEvent_Signal(&wake);
    return j;
}

Job *install_job(App *a) {
    Job *latest = NULL;
    for (int i = 0; i < njobs; i++) {
        if (jobs[i].app != a) continue;
        if (jobs[i].state == JOB_QUEUED || jobs[i].state == JOB_RUNNING) return &jobs[i];
        if (!latest || jobs[i].serial > latest->serial) latest = &jobs[i];
    }
    return latest;
}

Job *install_active(void) {
    for (int i = 0; i < njobs; i++)
        if (jobs[i].state == JOB_RUNNING)
            return &jobs[i];
    for (int i = 0; i < njobs; i++)
        if (jobs[i].state == JOB_QUEUED)
            return &jobs[i];
    return NULL;
}

bool install_busy(App *a) {
    Job *j = install_job(a);
    return j && (j->state == JOB_QUEUED || j->state == JOB_RUNNING);
}

float job_progress(Job *j) {
    if (j->state == JOB_DONE)
        return 1;
    if (j->wtotal <= 0)
        return 0;
    float done = 0;
    for (int i = 0; i < j->nsteps; i++) {
        if (i < j->cur)
            done += j->weight[i];
        else if (i == j->cur)
            done += j->weight[i] * clampf(j->steps[i].progress, 0, 1);
    }
    return clampf(done / j->wtotal, 0, 1);
}

void install_cancel(Job *j) {
    if (j && !__atomic_load_n(&j->commit_pending, __ATOMIC_ACQUIRE))
        j->cancel = true; // publication/record boundary cannot be interrupted after acknowledgment
}

void install_poll(void (*on_done)(Job *)) {
    for (int i = 0; i < njobs; i++) {
        Job *j = &jobs[i];
        if (__atomic_load_n(&j->commit_pending, __ATOMIC_ACQUIRE) && !j->commit_ack) {
            j->commit_ok = !j->cancel && (j->uninstall ? installed_forget(j->app) :
                installed_commit_manifest(j->app, j->tag, j->experimental, j->cia, j->files, j->dirs, j->hashes));
            if (!j->commit_ok) fail(j, "Cannot save installed record; reverting files");
            __atomic_store_n(&j->commit_ack, true, __ATOMIC_RELEASE);
        }
        bool finished = j->state >= JOB_DONE;
        if (finished && !j->reported) {
            j->reported = true;
            if (on_done)
                on_done(j);
        }
        if (!finished)
            j->reported = false;
    }
}

int install_count(void) { return njobs; }
Job *install_at(int i) { return i >= 0 && i < njobs ? &jobs[i] : NULL; }

u64 install_title_id(App *a) {
    u64 title = record_title(installed_record(a));
    AM_TitleInfo info;
    return title && R_SUCCEEDED(AM_GetTitleInfo(MEDIATYPE_SD, 1, &title, &info)) ? title : 0;
}

Job *install_uninstall(App *a) {
    if (!a || !worker) return NULL;
    if (install_busy(a)) return install_job(a);
    json_t *record = installed_record(a);
    if (!record) return NULL;
    json_t *snapshot = json_deep_copy(record);
    if (!snapshot) return NULL;
    LightLock_Lock(&lock);
    int slot = njobs;
    if (slot == MAX_JOBS)
        for (int i = 0; i < njobs; ++i) if (jobs[i].state >= JOB_DONE) { slot = i; break; }
    if (slot == MAX_JOBS) { LightLock_Unlock(&lock); json_decref(snapshot); return NULL; }
    Job *j = &jobs[slot];
    if (slot < njobs) { json_decref(j->files); json_decref(j->dirs); json_decref(j->hashes); json_decref(j->record); json_decref(j->release); json_decref(j->script); }
    memset(j, 0, sizeof *j);
    j->app = a; j->record = snapshot; j->uninstall = true; j->serial = ++job_serial;
    j->state = JOB_QUEUED;
    if (slot == njobs) njobs++;
    LightLock_Unlock(&lock);
    LightEvent_Signal(&wake);
    return j;
}
