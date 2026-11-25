#include "install_rules.h"
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
#include "forge_mode.h"
#endif
#include <archive.h>
#include <archive_entry.h>
#include <curl/curl.h>
#include <errno.h>
#include <malloc.h>
#ifndef FORGE_INSTALL_HOST_TEST
#include <mbedtls/sha256.h>
#endif
#include <dirent.h>
#include <limits.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <time.h>
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
//
// Interruption (power, crash, closing during a download) can leave this folder behind.
// tx_recover settles it from what is on the SD alone: the inventory written before the
// first live change, the CIA markers and the saved installed record. Uncommitted work is
// undone (the previous version is kept); committed work is completed. The next install
// therefore always starts from an empty folder.
#define TX_DIR DATA_DIR "/install-transaction"
#define TX_JOURNAL TX_DIR "/recovery.json"
#define TX_DIRS TX_DIR "/dirs.txt"              // folders this installation created
#define TX_CIA_STARTED TX_DIR "/cia-started"    // AM may have replaced the title from here on
#define TX_CIA_FINISHED TX_DIR "/cia-finished"  // AM committed the new title
#define TX_KEPT DATA_DIR "/install-transaction-kept-"
#define PATH_CAP 512
#define FILE_LIMIT 0x7fffffffULL // stdio/newlib signed offsets on this target
#define TX_LIMIT 4096

typedef struct {
    char path[PATH_CAP], stage[96], backup[96];
    bool deleted, published, had_old, remove_live, personal;
    char replace_hash[65]; // Only replace the exact personal resource inspected before download.
} TxFile;
typedef struct {
    TxFile *files;
    int count, cap;
    json_t *dirs;
    char cia_path[PATH_CAP];
    JobStep *cia_step;
    u64 title_id;
    bool had_title, cia_attempted;
    // The title was installed without forge.store's copy of its package: it cannot be
    // put back, so once AM commits the new title the installation is completed instead.
    bool no_restore;
    u16 old_version, new_version;
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

static bool durable_close(FILE *f);

// Folders are created outside the staging folder, so recovery needs their names too.
static bool note_dir(const char *path) {
    FILE *f = fopen(TX_DIRS, "ab");
    if (!f) return false;
    bool ok = fprintf(f, "%s\n", path) > 0;
    return durable_close(f) && ok;
}

static bool make_dir(const char *path, bool track) {
    struct stat st;
    if (!stat(path, &st)) return S_ISDIR(st.st_mode);
    if (errno != ENOENT) return false;
    // Reserve the rollback record before creating anything.
    if (track) {
        if (json_array_append_new(tx.dirs, json_string(path))) return false;
        if (!note_dir(path)) { json_array_remove(tx.dirs, json_array_size(tx.dirs) - 1); return false; }
    }
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

static void tx_recover(void);
static bool tx_clear(bool inventory_first);

static void tx_new_id(char *out, size_t n) {
    static u32 counter;
    snprintf(out, n, "%llx-%llx-%lx", (unsigned long long)time(NULL), (unsigned long long)svcGetSystemTick(),
             (unsigned long)++counter);
}

static bool tx_begin(Job *j) {
    memset(&tx, 0, sizeof tx);
    if (!mkdirs(DATA_DIR, false)) return fail(j, "SD card unavailable");
    if (mkdir(TX_DIR, 0777)) {
        // An earlier installation was interrupted or could not finish cleaning up. Settle it
        // first (undo, or complete when it was committed) so this one starts fresh.
        tx_recover();
        if (mkdir(TX_DIR, 0777)) return fail(j, "Cannot prepare " TX_DIR);
    }
    tx_new_id(j->transaction, sizeof j->transaction);
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
        if (success) {
            if (e->had_old && remove(e->backup) && errno != ENOENT) ok = false;
            if (remove(e->stage) && errno != ENOENT) ok = false;
        } else {
            // The exact inverse of tx_publish: the new file goes back to its staged name before
            // the previous one returns. At every step the SD reads the way recovery expects (a
            // staged file present means "not published"), so a cut here never loses either file.
            bool back = !e->published || !rename(e->path, e->stage) || errno == ENOENT;
            if (!back) ok = false;
            else if (e->had_old && rename(e->backup, e->path)) ok = false;
        }
    }
    if (!success) {
        for (size_t i = json_array_size(tx.dirs); i > 0; --i) {
            const char *dir = json_string_value(json_array_get(tx.dirs, i - 1));
            if (rmdir(dir) && errno != ENOENT && errno != ENOTEMPTY) ok = false;
        }
    }
    // Completed: the backups are gone, the inventory goes last. Rolled back: nothing is
    // published any more, so the inventory goes first and the staged files after it.
    if (ok) ok = tx_clear(!success);
    // Whatever is left is settled from the SD before the next installation (tx_recover).
    if (!ok) fail(j, "SD cleanup incomplete; it is retried before the next installation");
    free(tx.files); json_decref(tx.dirs);
    memset(&tx, 0, sizeof tx);
    return ok;
}

// Replaces or creates its live path (deleted intermediates and %TMP% never do).
static bool tx_publishes(const TxFile *e) {
    return !e->deleted && !e->remove_live && strncmp(e->path, DATA_DIR "/tmp/", strlen(DATA_DIR "/tmp/"));
}

static bool sha256_file(const char *, char [65], JobStep *, volatile bool *);

static bool tx_publish(Job *j) {
    // Every file that will reach a live path is staged now. The inventory promises it, and
    // recovery reads a missing staged file as one already moved into place.
    for (int i = 0; i < tx.count; ++i) {
        struct stat st;
        if (tx_publishes(&tx.files[i]) && (stat(tx.files[i].stage, &st) || !S_ISREG(st.st_mode)))
            return fail(j, "Missing staged file");
    }
    // Keep a human-readable recovery inventory on SD before the first live rename.
    json_t *files = json_array();
    json_t *manifest = files ? json_pack("{s:i,s:s,s:s,s:s,s:O}", "format", 2, "kind", j->uninstall ? "uninstall" : "install",
                                         "app", j->app && j->app->id ? j->app->id : "", "transaction", j->transaction,
                                         "files", files) : NULL;
    bool built = manifest != NULL;
    if (built && tx.title_id) {
        char title[17];
        snprintf(title, sizeof title, "%016llx", (unsigned long long)tx.title_id);
        built = !json_object_set_new(manifest, "title", json_string(title)) &&
                !json_object_set_new(manifest, "hadTitle", json_boolean(tx.had_title)) &&
                !json_object_set_new(manifest, "restore", json_boolean(!tx.no_restore)) &&
                !json_object_set_new(manifest, "oldVersion", json_integer(tx.old_version)) &&
                !json_object_set_new(manifest, "newVersion", json_integer(tx.new_version));
    }
    for (int i = 0; built && i < tx.count; ++i) {
        TxFile *e = &tx.files[i];
        built = !json_array_append_new(files, json_pack("{s:s,s:s,s:s,s:b,s:b}", "path", e->path, "stage", e->stage,
                                                        "backup", e->backup, "publish", tx_publishes(e), "remove", e->remove_live));
    }
    // An uninstallation also removes the folders its installation created, once empty.
    if (built && j->uninstall && json_is_array(json_object_get(j->record, "dirs")))
        built = !json_object_set(manifest, "dirs", json_object_get(j->record, "dirs"));
    json_decref(files);
    if (!built) { json_decref(manifest); return fail(j, "Out of memory"); }
    FILE *journal = fopen(TX_JOURNAL, "wb");
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
            if(e->personal) {
                char prior[65]; JobStep step={0};
                if(!e->replace_hash[0] || !sha256_file(e->path,prior,&step,&j->cancel) || strcmp(e->replace_hash,prior))
                    return fail(j,"Personal file changed during installation; preserved");
            }
            if (!S_ISREG(st.st_mode) || rename(e->path, e->backup)) return fail(j, "Cannot back up destination");
            e->had_old = true;
        } else if (errno != ENOENT) return fail(j, "Cannot inspect destination");
        if (e->remove_live) continue;
        if (rename(e->stage, e->path)) return fail(j, "Cannot publish staged file");
        e->published = true;
        if(e->personal)json_object_del(j->hashes,e->path);
        else if (json_array_append_new(j->files, json_string(e->path))) return fail(j, "Out of memory");
    }
    if (json_array_extend(j->dirs, tx.dirs)) return fail(j, "Out of memory");
    return true;
}

// ---- downloads -------------------------------------------------------------------------
#include "forge_zip_stream.inc"
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
    ForgeZipStream *zip;
    const char *range;
    u64 range_start, range_end; // offsets within this encrypted member
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
    if (d->zip) {
        if (len > d->range_end - d->written) return 0;
        if (!forge_zip_write(d->zip, d->f, (u8 *)data, len)) return 0;
        d->written += len;
        return len;
    }
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
    // libcurl restarts its counter after a redirect and on each range. The
    // write callback counts accepted payload bytes across the entire file.
    now = (curl_off_t)d->written;
    if (d->expected) total = (curl_off_t)d->expected;
    if (total > 0)
        d->step->progress = (float)now / total;
    d->job->done_bytes = d->base + now;
    if (d->job->cia && !d->job->unknown_size && d->job->total_bytes)
        d->job->cia_percent = (int)(50 * d->job->done_bytes / d->job->total_bytes);
    u64 tick = svcGetSystemTick();
    if (tick - d->last_tick > SYSCLOCK_ARM11 / 2) {
        float secs = (tick - d->last_tick) / (float)SYSCLOCK_ARM11;
        float speed = (d->written - d->last_bytes) / secs;
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
    if (d->range) curl_easy_setopt(c, CURLOPT_RANGE, d->range);
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
    if (d->range && r == CURLE_OK) {
        long status = 0;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
        if (status != 206) r = CURLE_RANGE_ERROR;
    }
    if (r != CURLE_OK)
        network_error(err, errn, !d->range && ebuf[0] ? ebuf : curl_easy_strerror(r));
    curl_easy_cleanup(c);
    return r;
}

#ifndef FORGE_ZIP_REQUEST_MAX
#define FORGE_ZIP_REQUEST_MAX (128u * 1024 * 1024)
#endif
static CURLcode fetch_member(const char *url, Dl *d, uint32_t offset, char *err, size_t errn) {
    char range[40];
    while (d->written < d->expected) {
        if (quit || d->job->cancel) return CURLE_ABORTED_BY_CALLBACK;
        d->range_start = d->written;
        u64 remaining = d->expected - d->written;
        d->range_end = d->written + (remaining > FORGE_ZIP_REQUEST_MAX ? FORGE_ZIP_REQUEST_MAX : remaining);
        snprintf(range, sizeof range, "%llu-%llu", (unsigned long long)(offset + d->range_start),
                 (unsigned long long)(offset + d->range_end - 1));
        d->range = range;
        CURLcode result = fetch(url, d, err, errn);
        if (result != CURLE_OK) return result;
        if (d->written != d->range_end) {
            snprintf(err,errn,"Incomplete encrypted resource range");
            return CURLE_PARTIAL_FILE;
        }
    }
    return CURLE_OK;
}

static bool download_to_file(Job *j, JobStep *st, const char *url, const char *path, u64 expected, const ForgeZipMember *member) {
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
    if (member) {
        d.zip = malloc(sizeof *d.zip);
        if (!d.zip || !forge_zip_begin(d.zip, member, expected)) {
            free(d.zip); fclose(f); remove(partial);
            return fail(j, "Cannot open encrypted resource");
        }
        d.expected = member->bytes;
    }
    CURLcode r = member ? fetch_member(url,&d,member->offset,j->error,sizeof j->error)
                        : fetch(url,&d,j->error,sizeof j->error);
    bool decoded = !d.zip || (d.zip->ended && d.zip->plain == expected);
    if (d.zip) { forge_zip_end(d.zip); free(d.zip); }
    bool disk_ok = durable_close(f);
    if (r != CURLE_OK || !disk_ok || !decoded || j->cancel || !d.written || (d.expected && d.written != d.expected)) {
        if (r == CURLE_OK) fail(j, "Incomplete download or unexpected size");
        if (!disk_ok)
            snprintf(j->error, sizeof j->error, "SD write failed");
        remove(partial);
        return false;
    }

    j->done_bytes = d.base + d.written;
    if(j->cia && j->cur==j->last_download){j->cia_percent=50;j->phase=PHASE_PREPARE;}
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
static bool install_cia(Job *j, JobStep *st, const char *path, bool replace_title) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(j->error, sizeof j->error, "%s", strerror(errno));
        return false;
    }
    if (fseek(f, 0, SEEK_END)) { fclose(f); return false; }
    long total = ftell(f);
    if (total <= 0 || fseek(f, 0, SEEK_SET)) { fclose(f); return false; }
    j->phase = PHASE_WRITE;
    j->cia_percent = 50;
    Handle h;
    Result r = replace_title ? AM_StartCiaInstallOverwrite(&h, MEDIATYPE_SD)
                             : AM_StartCiaInstall(MEDIATYPE_SD, &h);
    if (replace_title && R_FAILED(r))
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
        j->cia_percent = 50 + (int)(49 * off / (u64)total);
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
    j->phase = PHASE_FINISH;
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

// Modified personal data stays beside its replacement, outside the uninstall inventory.
static bool preserve_personal(Job *j, const char *path, const char *digest) {
    if (!*digest) return true;
    for (unsigned i=1; i<=1000; ++i) {
        char backup[PATH_CAP]; struct stat info;
        if (!checked(backup,sizeof backup,"%s.forge-backup-%u",path,i)) break;
        if (!stat(backup,&info) || tx_find(backup)) continue;
        if (errno!=ENOENT) break;
        TxFile *e=tx_file(j,backup);
        if (!e || !copy_file(path,e->stage)) break;
        e->personal=true;
        char saved[65]; JobStep step={0};
        if (sha256_file(e->stage,saved,&step,&j->cancel) && !strcmp(saved,digest)) return true;
        break;
    }
    return fail(j,"Cannot preserve existing game data");
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

static bool title_known(Job *j, u64 id) {
    for (int i = 0; i < j->nknown && i < 8; i++)
        if (j->known_titles[i] == id) return true;
    return false;
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
    const char *bound_title=json_string_value(json_object_get(j->release,"forgeTitleId"));
    json_t *cia_asset=app_asset(j->app,j->release,"cia");
    if (!bound_title && !json_object_get(cia_asset,"o3ds") &&
        cia_asset==json_object_get(json_object_get(j->release,"assets"),"cia"))
        bound_title=json_string_value(json_object_get(j->release,"ciaTitle"));
    if (bound_title && (strtoull(bound_title,NULL,16)!=info.titleID))
        return fail(j,"CIA title does not match its release");
    tx.title_id = info.titleID;
    tx.new_version = info.version;
    if (!title_present(tx.title_id, &tx.had_title)) return fail(j, "Cannot inspect installed titles");
    snprintf(tx.cia_cache, sizeof tx.cia_cache, DATA_DIR "/cia-cache/%016llx.cia", (unsigned long long)info.titleID);
    if (tx.had_title) {
        AM_TitleInfo backup, current;
        if (R_FAILED(AM_GetTitleInfo(MEDIATYPE_SD, 1, &tx.title_id, &current))) return fail(j, "Cannot inspect installed titles");
        tx.old_version = current.version;
        // forge.store keeps the package it installed, to put that version back when this one
        // fails after AM. A title installed some other way (FBI, Universal-Updater) has none:
        // AM only replaces it once the new package is verified, and from then on the update
        // is completed instead of undone.
        tx.no_restore = !cia_info(tx.cia_cache, &backup) || backup.titleID != info.titleID || backup.version != current.version;
        // Homebrew title IDs can be shared by unrelated software: replace only one this app
        // is known to use (its catalog entry or an earlier installation).
        if (tx.no_restore && !title_known(j, tx.title_id))
            return fail(j, "Another installed title uses this CIA's title ID; it was not replaced");
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
    bool ok = install_cia(&restore, &step, cache->backup, true);
    if (!ok) fail(j, "CIA restore failed; original package retained in " TX_DIR);
    return ok;
}

static bool title_state(u64 id, bool *present, u16 *version) {
    *version = 0;
    if (!title_present(id, present)) return false;
    AM_TitleInfo info;
    if (*present) {
        if (R_FAILED(AM_GetTitleInfo(MEDIATYPE_SD, 1, &id, &info))) return false;
        *version = info.version;
    }
    return true;
}

// AM commits a CIA atomically: after a reported failure the title is the previous one or,
// rarely, the new one. Only a visible change needs the previous version put back.
static bool tx_title_changed(void) {
    bool present;
    u16 version;
    if (!title_state(tx.title_id, &present, &version)) return true;
    return tx.had_title ? !present || version != tx.old_version : present;
}

static bool tx_mark(const char *path) {
    FILE *f = fopen(path, "wb");
    return f && durable_close(f);
}

// ---- recovery of an interrupted installation ----------------------------------------------
static bool exists(const char *p) {
    struct stat st;
    return !stat(p, &st);
}

// Only names this installer gives its files: TX_DIR/NNNN.new or NNNN.old.
static bool tx_own_file(const char *p, const char *suffix) {
    size_t n = strlen(TX_DIR "/");
    if (!p || strncmp(p, TX_DIR "/", n) || strlen(p) != n + 8) return false;
    for (int i = 0; i < 4; i++)
        if (p[n + i] < '0' || p[n + i] > '9') return false;
    return !strcmp(p + n + 4, suffix);
}

// A live path an installation may change (the rule tx_file applies).
static bool tx_live_path(const char *p) {
    size_t n = strlen(DATA_DIR "/");
    if (!safe_path(p) || !strcmp(p, "sdmc:/")) return false;
    return strncmp(p, DATA_DIR "/", n) || !strncmp(p + n, "tmp/", 4) || !strncmp(p + n, "cia-cache/", 10);
}

typedef struct {
    const char *path, *stage, *backup;
    bool publish, remove;
} TxEntry;

// The inventory lives on the SD: act on it only when every entry has the shape we write.
static bool tx_entry(json_t *v, bool legacy, TxEntry *e) {
    e->path = json_string_value(json_object_get(v, "path"));
    e->stage = json_string_value(json_object_get(v, "stage"));
    e->backup = json_string_value(json_object_get(v, "backup"));
    if (!tx_live_path(e->path) || !tx_own_file(e->stage, ".new") || !tx_own_file(e->backup, ".old")) return false;
    if (legacy) {  // version 1.0: deleted intermediates and %TMP% were never published
        e->publish = !json_is_true(json_object_get(v, "deleted")) && strncmp(e->path, DATA_DIR "/tmp/", strlen(DATA_DIR "/tmp/"));
        e->remove = false;
    } else {
        e->publish = json_is_true(json_object_get(v, "publish"));
        e->remove = json_is_true(json_object_get(v, "remove"));
    }
    return true;
}

// Some saved record already holds this exact file (its SHA-256 matches).
static bool saved_describes(json_t *saved, const char *path) {
    char hash[65];
    volatile bool never = false;
    JobStep step = {0};
    bool hashed = false;
    const char *id;
    json_t *record;
    json_object_foreach(saved, id, record) {
        const char *want = json_string_value(json_object_get(json_object_get(record, "hashes"), path));
        if (!want || strlen(want) != 64) continue;
        if (!hashed && !(hashed = sha256_file(path, hash, &step, &never))) return false;
        if (!strcasecmp(hash, want)) return true;
    }
    return false;
}

// Puts the previous bytes back. A published entry's staged file existed when the inventory
// was written, so a missing one was moved to its live path: that file is the new version.
// Like tx_finish, it moves the new file back to its staged name first and deletes nothing:
// a cut during recovery leaves a state the next recovery reads the same way.
static bool tx_undo(const TxEntry *e, bool legacy, json_t *saved) {
    bool backup = exists(e->backup);
    // 1.0 inventories have no commit id: a record already describing the live file means that
    // installation was committed and only its cleanup was cut short.
    if (legacy && backup && exists(e->path) && saved_describes(saved, e->path))
        return !remove(e->backup) || errno == ENOENT;
    // 1.0 inventories cannot tell a new file from an untouched one: only a path with a backup
    // certainly changed. Anything else is left exactly as it is.
    bool replaced = legacy ? backup : e->publish && !exists(e->stage);
    if (replaced && exists(e->path) && rename(e->path, e->stage)) return false;
    return !backup || !rename(e->backup, e->path);
}

// Removes the folders this installation created, deepest first, when they are empty again.
static void tx_drop_created_dirs(void) {
    FILE *f = fopen(TX_DIRS, "rb");
    if (!f) return;
    enum { MAX_DIRS = 64 };
    char (*lines)[PATH_CAP] = malloc(MAX_DIRS * sizeof *lines);  // the worker's stack is small
    int count = 0;
    while (lines && count < MAX_DIRS && fgets(lines[count], PATH_CAP, f)) {
        lines[count][strcspn(lines[count], "\r\n")] = 0;
        if (safe_path(lines[count]) && strcmp(lines[count], "sdmc:/") && strcmp(lines[count], "sdmc:/3ds") &&
            strcmp(lines[count], DATA_DIR))
            count++;
    }
    fclose(f);
    while (count--) rmdir(lines[count]);  // never recursive: a folder with anything in it stays
    free(lines);
}

// Removes the staging folder's own files (staged, backups, inventory, markers) and the
// folder. After a completion the inventory goes last, so a cut never strands a backup without
// it; after an undo it goes first, so the staged new files never look published. The CIA
// markers are part of the decision and always outlive the inventory.
static int tx_clear_rank(const char *name, bool inventory_first) {
    bool inventory = !strcmp(name, "recovery.json");
    bool marker = !strcmp(name, "cia-started") || !strcmp(name, "cia-finished");
    if (inventory_first) return inventory ? 0 : 1;
    return inventory ? 1 : marker ? 2 : 0;
}

static bool tx_clear(bool inventory_first) {
    DIR *d = opendir(TX_DIR);
    if (!d) return errno == ENOENT;
    char (*names)[64] = NULL;
    int count = 0, cap = 0;
    bool ok = true;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (strlen(e->d_name) >= 64) { ok = false; continue; }
        if (count == cap) {
            int grown = cap ? cap * 2 : 32;
            void *more = grown <= 4 * TX_LIMIT + 64 ? realloc(names, grown * sizeof *names) : NULL;
            if (!more) { ok = false; break; }
            names = more;
            cap = grown;
        }
        strcpy(names[count++], e->d_name);
    }
    closedir(d);
    char path[PATH_CAP];
    for (int pass = 0; pass < 3; pass++)
        for (int i = 0; i < count; i++) {
            if (tx_clear_rank(names[i], inventory_first) != pass) continue;
            snprintf(path, sizeof path, TX_DIR "/%s", names[i]);
            if (remove(path) && errno != ENOENT) ok = false;
        }
    free(names);
    if (ok && rmdir(TX_DIR) && errno != ENOENT) ok = false;
    return ok;
}

static bool tx_has_backup(void) {
    DIR *d = opendir(TX_DIR);
    if (!d) return false;
    bool found = false;
    struct dirent *e;
    while (!found && (e = readdir(d))) {
        size_t n = strlen(e->d_name);
        found = n > 4 && !strcmp(e->d_name + n - 4, ".old");
    }
    closedir(d);
    return found;
}

static void remove_dirs(json_t *dirs);

// Decides from the SD alone. The installed record is the commit point: an installation is
// complete once its record carries the inventory's transaction id, an uninstallation once
// its record is gone. Anything earlier is undone, so the previous version stays.
static bool tx_settle(json_t *journal, bool *completed) {
    bool legacy = json_is_array(journal);
    json_t *list = legacy ? journal : json_object_get(journal, "files");
    if (!json_is_array(list) || (!legacy && json_integer_value(json_object_get(journal, "format")) != 2)) return false;
    size_t count = json_array_size(list);
    TxEntry *entries = calloc(count ? count : 1, sizeof *entries);
    if (!entries) return false;
    for (size_t i = 0; i < count; i++)
        if (!tx_entry(json_array_get(list, i), legacy, &entries[i])) { free(entries); return false; }
    json_t *saved = installed_saved();
    bool forward = false, drop_title = false, reinstall = false, ok = true;
    u64 title = 0;
    if (!legacy) {
        const char *kind = json_string_value(json_object_get(journal, "kind"));
        const char *app = json_string_value(json_object_get(journal, "app"));
        const char *id = json_string_value(json_object_get(journal, "transaction"));
        const char *hex = json_string_value(json_object_get(journal, "title"));
        bool uninstall = kind && !strcmp(kind, "uninstall");
        json_t *record = app && *app ? json_object_get(saved, app) : NULL;
        const char *committed_id = json_string_value(json_object_get(record, "transaction"));
        // An unreadable record never counts as committed: then nothing is deleted.
        forward = saved && app && *app && (uninstall ? !record : id && *id && committed_id && !strcmp(committed_id, id));
        char *end = NULL;
        title = hex && strlen(hex) == 16 ? strtoull(hex, &end, 16) : 0;
        if (title && (*end || (title >> 32) != 0x00040000)) title = 0;
        if (!forward && title) {
            bool present, had = json_is_true(json_object_get(journal, "hadTitle"));
            bool restore = json_is_true(json_object_get(journal, "restore"));
            bool started = exists(TX_CIA_STARTED), finished = exists(TX_CIA_FINISHED);
            u16 version, before = (u16)json_integer_value(json_object_get(journal, "oldVersion"));
            u16 after = (u16)json_integer_value(json_object_get(journal, "newVersion"));
            if (!title_state(title, &present, &version)) { json_decref(saved); free(entries); return false; }
            bool touched = started || finished;
            if (uninstall) reinstall = had && !present;  // deleted: put it back from its restored package
            else if (!had) drop_title = touched && present && version == after;
            else if (restore) reinstall = touched && !(present && version == before && before != after);
            // No package to go back to: once AM committed, complete the update instead.
            else forward = finished || (started && present && version == after && after != before);
        }
    }
    if (forward) {
        // A commit always follows a complete publication; anything else is not ours to guess.
        for (size_t i = 0; i < count; i++)
            if (entries[i].publish && exists(entries[i].stage)) ok = false;
        for (size_t i = count; ok && i-- > 0;) {
            if (remove(entries[i].backup) && errno != ENOENT) ok = false;
            if (remove(entries[i].stage) && errno != ENOENT) ok = false;
        }
        if (ok) remove_dirs(json_object_get(journal, "dirs"));
    } else {
        for (size_t i = count; i-- > 0;)
            if (!tx_undo(&entries[i], legacy, saved)) ok = false;
        if (ok) tx_drop_created_dirs();
        if (ok && drop_title) AM_DeleteUserTitle(MEDIATYPE_SD, title);
        if (ok && reinstall) {
            char package[PATH_CAP];
            snprintf(package, sizeof package, DATA_DIR "/cia-cache/%016llx.cia", (unsigned long long)title);
            Job restore = {0};
            JobStep step = {0};
            if (!install_cia(&restore, &step, package, true)) printf("Recovery: CIA restore failed: %s\n", restore.error);
        }
    }
    json_decref(saved);
    free(entries);
    *completed = forward;
    return ok;
}

static void tx_recover(void) {
    struct stat st;
    if (stat(TX_DIR, &st)) return;
    bool ok = S_ISDIR(st.st_mode), completed = false;
    if (ok) {
        json_t *journal = json_load_file(TX_JOURNAL, JSON_REJECT_DUPLICATES, NULL);
        // Without a readable inventory nothing was published (it is durable before the first
        // live change); backups would contradict that, so then nothing is guessed.
        if (journal) ok = tx_settle(journal, &completed);
        else if ((ok = !tx_has_backup())) tx_drop_created_dirs();
        json_decref(journal);
        if (ok) ok = tx_clear(!completed);
    }
    if (ok) return;  // silent: the next installation simply starts fresh
    // Keep what could not be settled for inspection, out of the next installation's way.
    char kept[96];
    for (int n = 1; n < 100; n++) {
        snprintf(kept, sizeof kept, TX_KEPT "%d", n);
        if (exists(kept)) continue;
        if (rename(TX_DIR, kept)) kept[0] = 0;
        break;
    }
    printf("Recovery: an interrupted installation could not be settled; kept as %s\n", kept[0] ? kept : "(not moved)");
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

static bool extract(Job *j, JobStep *st, const char *archive_path, const char *input, const char *out, bool personal) {
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
        char prior[65]="";
        if (personal) {
            struct stat existing;
            if (!stat(dest,&existing)) {
                const char *want=json_string_value(json_object_get(json_object_get(st->raw,"members"),name));
                if (!S_ISREG(existing.st_mode) || (want && !sha256_file(dest,prior,st,&j->cancel))) {
                    ok=fail(j,"Cannot inspect existing personal resource");break;
                }
                if (!want || !strcasecmp(prior,want)) {
                    if(archive_read_data_skip(a)<ARCHIVE_OK){ok=false;break;}
                    files++; continue;
                }
            } else if (errno!=ENOENT) {ok=fail(j,"Cannot inspect personal resource");break;}
        }
        if(!preserve_personal(j,dest,prior)){ok=false;break;}
        la_int64_t size = archive_entry_size(e);
        if (size < 0 || (u64)size > FILE_LIMIT || !free_space((u64)size)) {
            ok = fail(j, "Insufficient SD space or archive entry too large"); break;
        }
        TxFile *entry = tx_file(j, dest);
        if (!entry) { ok = false; break; }
        entry->deleted = false;
        entry->personal = personal;
        strcpy(entry->replace_hash,prior);
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
    st->extracted=files;
    return ok && files > 0;
}

// ---- job ----------------------------------------------------------------------------------------
static const char *str(json_t *o, const char *k) { return json_string_value(json_object_get(o, k)); }

static bool verify_download(Job *j,JobStep *st,const char *path,const char *expected) {
    if(!expected||!*expected)return true; // older releases use their SHA256SUMS step
    if(strlen(expected)!=64)return fail(j,"Invalid SHA-256 metadata");
    char digest[65];const char *staged=tx_read(path);
    if(!staged||!sha256_file(staged,digest,st,&j->cancel)||strcasecmp(digest,expected))return fail(j,"Downloaded file failed SHA-256 verification");
    return true;
}

// Hash on the install worker. The UI never reads game data to decide what to queue.
static bool matches_file(Job *j, JobStep *step, const char *path, const char *want) {
    char digest[65];
    return want && strlen(want)==64 && sha256_file(path,digest,step,&j->cancel) && !strcasecmp(digest,want);
}

static bool local_download(Job *j, JobStep *step, char path[PATH_CAP]) {
    json_t *s=step->raw;
    if(!sd_path(path,PATH_CAP,str(s,"output")))return false;
    if(json_is_true(json_object_get(s,"personal"))) {
        const char *dir=str(s,"unpackTo");
        if(!dir)return matches_file(j,step,path,str(s,"sha256"));
        json_t *members=json_object_get(s,"members"),*hash;
        if(!json_object_size(members))return false;
        const char *name;
        json_object_foreach(members,name,hash){
            if(!checked(path,PATH_CAP,"sdmc:%s%s",dir,name) || !safe_path(path) ||
               !matches_file(j,step,path,json_string_value(hash)))return false;
        }
        return true;
    }
    json_t *asset=str(s,"asset")?app_asset(j->app,j->release,str(s,"asset")):s;
    const char *title=str(j->release,"ciaTitle");
    if(title && strlen(title)==16 && str(s,"asset") && !strcmp(str(s,"asset"),"cia"))
        snprintf(path,PATH_CAP,DATA_DIR "/cia-cache/%s.cia",title);
    return matches_file(j,step,path,str(asset,"sha256"));
}

// Only skip a complete download/verify/extract/delete sequence with final SD outputs.
static int archive_sequence(Job *j,int first,char key[PATH_CAP]) {
    JobStep *step=&j->steps[first];
    const char *path=str(step->raw,"output");
    if(!step->bytes || !path || strncmp(path,"%TMP%/",6) || first+2>=j->nsteps)return -1;
    json_t *asset=str(step->raw,"asset")?app_asset(j->app,j->release,str(step->raw,"asset")):step->raw;
    const char *hash=str(asset,"sha256");
    if(!hash || strlen(hash)!=64)return -1;
    int at=first+1;
    if(!strcmp(str(j->steps[at].raw,"type"),"verifySha256")){
        const char *file=str(j->steps[at++].raw,"file");
        if(!file || strcmp(file,path))return -1;
    }
    json_t *ex=j->steps[at].raw;
    const char *file=str(ex,"file"),*out=str(ex,"output"),*input=str(ex,"input");
    if(at+1>=j->nsteps || strcmp(str(ex,"type"),"extractFile") || !file || strcmp(file,path) ||
       !out || !*out || strchr(out,'%'))return -1;
    json_t *del=j->steps[at+1].raw;
    if(strcmp(str(del,"type"),"deleteFile") || !str(del,"file") || strcmp(str(del,"file"),path))return -1;
    // Later filesystem operations may need staged outputs, even when identical live files exist.
    for(int k=at+2;k<j->nsteps;k++){
        json_t *later=j->steps[k].raw;
        const char *src=str(later,"old");
        if(!src)src=str(later,"file");
        if(src && !strncmp(src,out,strlen(out)))return -1;
    }
    return checked(key,PATH_CAP,"archive:%s:%s:%s",hash,out,input?input:"")?at:-1;
}

static bool archive_present(Job *j,JobStep *step,json_t *files) {
    if(!json_object_size(files))return false;
    const char *path;json_t *hash;
    json_object_foreach(files,path,hash)
        if(!safe_path(path) || !matches_file(j,step,path,json_string_value(hash)))return false;
    return true;
}

static bool prepare_downloads(Job *j) {
    u64 wire=0,space=0;
    j->last_download=-1;
    for(int i=0;i<j->nsteps && !j->cancel;i++){
        JobStep *step=&j->steps[i];
        if(!step->bytes)continue;
        json_t *s=step->raw,*asset=str(s,"asset")?app_asset(j->app,j->release,str(s,"asset")):s;
        char local[PATH_CAP];
        char receipt[PATH_CAP];
        int extract_at=archive_sequence(j,i,receipt);
        if(extract_at>=0){
            json_t *files=json_object_get(json_object_get(j->record,"hashes"),receipt);
            if(archive_present(j,step,files)){
                for(int k=i;k<=extract_at;k++)j->steps[k].reuse=true;
                continue;
            }
        }
        step->reuse=local_download(j,step,local);
        if(step->reuse && !json_is_true(json_object_get(s,"personal"))){
            char path[PATH_CAP];
            if(!sd_path(path,sizeof path,str(s,"output")))return fail(j,"Invalid download path");
            TxFile *e=tx_file(j,path);
            if(!e || !copy_file(local,e->stage) || !verify_download(j,step,path,str(asset,"sha256")))
                return fail(j,"Cannot stage verified local file");
        }
        if(step->reuse)continue;
        j->last_download=i;
        u64 bytes=forge_wire_size(asset);
        if(!bytes)j->unknown_size=true;
        wire+=bytes;
        space+=(u64)json_integer_value(json_object_get(asset,"size"));
    }
    j->total_bytes=wire;
    j->cia_percent=j->unknown_size?-1:j->last_download<0?50:0;
    return !j->cancel && (free_space(space) || fail(j,"Insufficient SD space"));
}

static bool run_step(Job *j, JobStep *st) {
    json_t *s = st->raw;
    const char *type = str(s, "type");
    char path[PATH_CAP], path2[PATH_CAP], url[2048];
    if (!type) return fail(j, "Missing install operation");
    if (st->reuse) return true;
    if (!strcmp(type, "mkdir")) {
        if (!sd_path(path, sizeof path, str(s, "directory"))) return fail(j, "Invalid or oversized catalog path");
        return mkdirs(path, false) || fail(j, "Cannot create directory");
    }
    if (!strcmp(type, "downloadUrl")) {
        bool personal=json_is_true(json_object_get(s,"personal"));
        const char *unpack=str(s,"unpackTo");
        if(unpack && (!personal || !json_object_get(s,"zipMember") ||
            !sd_path(path2,sizeof path2,unpack) || strncmp(path2,"sdmc:/3ds/",10) ||
            strlen(path2)<=10 || path2[strlen(path2)-1]!='/')) return fail(j,"Invalid personal archive destination");
        char prior[65]="";
        if(personal && !unpack) {
            if(!sd_path(path,sizeof path,str(s,"output")))return fail(j,"Invalid personal-resource path");
            struct stat existing;
            if(!stat(path,&existing)) {
                if(!S_ISREG(existing.st_mode))return fail(j,"Personal-resource path is not a regular file");
                const char *want=str(s,"sha256");
                if(want && !sha256_file(path,prior,st,&j->cancel))return fail(j,"Cannot check personal resource");
                if(!want || !strcasecmp(prior,want)){st->progress=1;return true;}
            } else if(errno!=ENOENT)return fail(j,"Cannot inspect personal-resource path");
        }
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
        if(unpack && strncmp(path,DATA_DIR "/tmp/",strlen(DATA_DIR "/tmp/")))return fail(j,"Invalid personal archive staging path");
        json_int_t size = json_integer_value(json_object_get(s, "size"));
        if (size < 0) return fail(j, "Invalid download size");
        json_t *zip = json_object_get(s,"zipMember");
        ForgeZipMember member;
        if(zip&&(!str(s,"sha256")||!forge_zip_member(zip,&member)))return fail(j,"Invalid encrypted resource");
        if(!preserve_personal(j,path,prior))return false;
        if(!download_to_file(j, st, url, path, (u64)size, zip?&member:NULL))return false;
        if(!verify_download(j,st,path,str(s,"sha256")))return false;
        if(unpack)return extract(j,st,tx_read(path),NULL,path2,true);
        if(personal){TxFile *e=tx_find(path);if(!e)return fail(j,"Missing personal-resource transaction");e->personal=true;strcpy(e->replace_hash,prior);}
        return true;
    }
    if (!strcmp(type, "downloadRelease")) {
        json_t *asset = app_asset(j->app, j->release, str(s, "asset"));
        if (!release_url(url, sizeof url, j, str(asset, "name"))) return fail(j, "Missing release asset or URL too long");
        if (!sd_path(path, sizeof path, str(s, "output"))) return fail(j, "Invalid or oversized catalog path");
        json_int_t size = json_integer_value(json_object_get(asset, "size"));
        if (size < 0) return fail(j, "Invalid download size");
        return download_to_file(j, st, url, path, (u64)size, NULL)&&verify_download(j,st,path,str(asset,"sha256"));
    }
    if (!strcmp(type, "verifySha256")) {
        if (!sd_path(path, sizeof path, str(s, "file"))) return fail(j, "Invalid or oversized catalog path");
        if (!sums[0])
            load_sums(j);
        const char *name = strrchr(path, '/');
        name = name ? name + 1 : path;
        const char *alias=NULL,*alias_hash=NULL;
        // Verification must bind the digest to this download's original asset name.
        size_t si;
        json_t *step;
        json_array_foreach(j->script ? j->script : app_install_steps(j->app, j->cia), si, step) {
            if (!str(step, "type") || strcmp(str(step, "type"), "downloadRelease"))
                continue;
            if (!sd_path(path2, sizeof path2, str(step, "output"))) return fail(j, "Invalid or oversized catalog path");
            if (!strcmp(path, path2)) {
                json_t *asset=app_asset(j->app,j->release,str(step,"asset"));
                const char *asset_name=str(asset,"name");
                alias=str(asset,"sumsName");alias_hash=str(asset,"sha256");
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
                size_t length=(size_t)(last-filename);
                if ((length==strlen(name) && !memcmp(filename,name,length)) ||
                    (alias && alias_hash && !strcasecmp(hex,alias_hash) && length==strlen(alias) && !memcmp(filename,alias,length))) {
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
        int before=tx.count;
        if(!extract(j, st, tx_read(path), str(s, "input"), path2,false))return false;
        int first=j->cur-1;
        if(first>=0 && !strcmp(str(j->steps[first].raw,"type"),"verifySha256"))first--;
        char receipt[PATH_CAP];
        if(first>=0 && archive_sequence(j,first,receipt)==j->cur && st->extracted==tx.count-before && before<tx.count){
            json_t *files=json_object();
            bool ok=files!=NULL;
            for(int k=before;ok && k<tx.count;k++){
                char hash[65];
                ok=tx_publishes(&tx.files[k]) && sha256_file(tx.files[k].stage,hash,st,&j->cancel) &&
                   !json_object_set_new(files,tx.files[k].path,json_string(hash));
            }
            if(ok)json_object_set(j->hashes,receipt,files);
            json_decref(files);
        }
        return !j->cancel;
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
        TxFile *personal=sd_path(path,sizeof path,p)?tx_find(path):NULL;
        if ((personal&&personal->personal&&!personal->deleted) || app_requirement_met(j->app, req)) {
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

// AM committed the new title, but it is not recorded. Put the previous version back; with no
// package to restore it from, keep the verified new version complete instead.
static void tx_after_title(Job *j) {
    if (tx.no_restore) {
        tx_finish(j, true);
        fail(j, "Installed; its installed record could not be saved");
        j->state = JOB_FAILED;
        return;
    }
    bool restored = cia_restore(j);
    if (restored) restored = tx_finish(j, false);
    else tx_abandon();  // settled from the SD before the next installation
    j->state = j->cancel && restored ? JOB_CANCELLED : JOB_FAILED;
}

static bool removable_dir(const char *dir) {
    return safe_path(dir) && strcmp(dir, "sdmc:/") && strcmp(dir, "sdmc:/3ds") && strcmp(dir, DATA_DIR);
}

static void remove_dirs(json_t *dirs) {
    for (size_t i = json_array_size(dirs); i > 0; --i) {
        const char *dir = json_string_value(json_array_get(dirs, i - 1));
        if (removable_dir(dir)) rmdir(dir);  // never recursive: a folder with anything in it stays
    }
}

static void finish_record(Job *j) {
    // The JSON record belongs to the main thread. Retain backups until its
    // synchronous commit acknowledges success; shutdown can still cancel here.
    __atomic_store_n(&j->commit_pending, true, __ATOMIC_RELEASE);
    while (!__atomic_load_n(&j->commit_ack, __ATOMIC_ACQUIRE) && !quit)
        svcSleepThread(1000000);
    bool saved = __atomic_load_n(&j->commit_ack, __ATOMIC_ACQUIRE) && j->commit_ok;
    __atomic_store_n(&j->commit_pending, false, __ATOMIC_RELEASE);
    if (!saved && tx.cia_attempted && !j->uninstall) { tx_after_title(j); return; }
    // Before the inventory goes: an interruption after this still completes the removal.
    if (saved && j->uninstall) remove_dirs(json_object_get(j->record, "dirs"));
    bool restored = saved || cia_restore(j);
    bool cleaned = restored && tx_finish(j, saved);
    if (!restored) tx_abandon();
    if (saved && !cleaned) j->warnings++;
    j->progress = saved ? 1 : j->progress;
    j->state = saved ? JOB_DONE : (j->cancel && cleaned ? JOB_CANCELLED : JOB_FAILED);
}

static bool hash_published(Job *j) {
    JobStep step = {0};
    for (int i = 0; i < tx.count; ++i) {
        TxFile *e = &tx.files[i];
        if (!e->published || e->personal) continue;
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
            tx.old_version = tx.new_version = current.version;
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
    if (!prepare_downloads(j)) {
        tx_finish(j,false);j->state=j->cancel?JOB_CANCELLED:JOB_FAILED;return;
    }
    j->phase = j->cia && j->last_download<0 ? PHASE_PREPARE : PHASE_STEPS;
    float *weights = j->weight, total = 0;
    for (int i = 0; i < j->nsteps; i++) {
        json_t *step=j->steps[i].raw;
        json_t *asset=!strcmp(str(step,"type"),"downloadUrl")?step:app_asset(j->app,j->release,str(step,"asset"));
        weights[i] =
            j->steps[i].bytes
                ? 1 + (float)forge_wire_size(asset) /
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
        if (!net_ok && st->bytes && !st->reuse) {
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
        if(j->cia && i==j->last_download){j->cia_percent=50;j->phase=PHASE_PREPARE;}
        if (st==tx.cia_step) {
            st->state=STEP_PENDING; // AM opens this step only after all files are ready.
            st->progress=0;
        } else {
            if (st->state==STEP_ACTIVE)st->state=STEP_DONE;
            st->progress=1;
        }
        before += weights[i];
        j->progress = before / total;
    }
    j->phase = j->cia ? PHASE_PREPARE : PHASE_FINISH;
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
        // The markers tell recovery whether AM may have replaced the title.
        bool marked = tx_mark(TX_CIA_STARTED);
        if (!marked) fail(j, "SD write failed");
        j->cur=(int)(tx.cia_step-j->steps);
        tx.cia_step->state=STEP_ACTIVE;
        bool installed = marked && install_cia(j, tx.cia_step, tx.cia_cache, tx.had_title);
        tx.cia_step->state=installed?STEP_DONE:STEP_ERROR;
        if (installed) tx_mark(TX_CIA_FINISHED);
        else if (!tx_title_changed()) {
            bool restored = tx_finish(j, false);
            j->state = j->cancel && restored ? JOB_CANCELLED : JOB_FAILED; return;
        }
        char title[32];
        snprintf(title, sizeof title, "title:%016llx", (unsigned long long)tx.title_id);
        if (!installed || json_array_append_new(j->files, json_string(title))) {
            if (!j->error[0]) fail(j, "Out of memory");
            tx_after_title(j);
            return;
        }
    }
    finish_record(j);
}

static void worker_main(void *arg) {
    (void)arg;
    tx_recover();  // an installation interrupted last time (power, crash) is settled first
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

static Job *install_start_plan(App *a, bool cia, bool experimental, json_t *script) {
    if (!a || !worker) {
        toast("Installer unavailable", DANGER);
        return NULL;
    }
    if (install_busy(a))
        return install_job(a);
    if(!app_has_format(a,cia)){toast("This format is unavailable on this console",AMBER);return NULL;}
    if (!json_array_size(script) || json_array_size(script) > INSTALL_MAX_STEPS || json_is_true(json_object_get(a->j, "nativeUnsupported"))) {
        toast(S.lang == 0 ? "Este instalador necesita opciones adicionales." : "This installer requires additional choices.", AMBER);
        return NULL;
    }
    json_t *selected = app_release(a, experimental);
    const char *selected_tag = str(selected, "tag");
    if (!selected_tag || strlen(selected_tag) >= sizeof jobs[0].tag) {
        toast("Missing release or oversized version", DANGER); return NULL;
    }
    if (!install_model_matches(IS_NEW3DS,a->old_ok,selected)) {
        toast(T("confirm.blockedNew"), AMBER); return NULL;
    }
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
        }

    }
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
    // Titles this app is known to use: an earlier installation's, then the catalog's.
    u64 recorded = record_title(installed_record(a));
    if (recorded) j->known_titles[j->nknown++] = recorded;
    j->nknown += app_title_ids(a, j->known_titles + j->nknown, 8 - j->nknown);
    j->record = json_deep_copy(installed_record(a));
    j->cia = cia;
    j->cia_percent = -1;
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
            j->total_bytes += forge_wire_size(asset);
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

Job *install_start(App *a,bool cia,bool experimental) {
    json_t *plan=a?forge_mode_plan(a->id,app_install_steps(a,cia)):NULL;
    Job *j=install_start_plan(a,cia,experimental,plan);
    json_decref(plan);return j;
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
    if (j->cia && !j->uninstall) return j->cia_percent<0 ? -1.f : j->cia_percent/100.f;
    if (j->wtotal <= 0)
        return 0;
    float done = 0;
    for (int i = 0; i < j->nsteps; i++) {
        if (i < j->cur)
            done += j->weight[i];
        else if (i == j->cur)
            done += j->weight[i] * clampf(j->steps[i].progress, 0, 1);
    }
    return clampf(done / j->wtotal, 0, .99f);
}

int job_percent(Job *j) {
    if(j->state==JOB_DONE)return 100;
    if(j->cia && !j->uninstall)return j->cia_percent;
    return (int)(job_progress(j)*100);
}

const char *job_activity(Job *j) {
    if(j->state==JOB_QUEUED)return T("install.queued");
    if(j->phase==PHASE_CHECK)return T("install.checkSD");
    if(j->phase==PHASE_FINISH)return T("install.finalizing");
    if(j->phase==PHASE_WRITE)return T("install.title");
    JobStep *st=j->cur>=0 && j->cur<j->nsteps?&j->steps[j->cur]:NULL;
    if(j->phase==PHASE_PREPARE){
        if(st && st->state==STEP_ACTIVE && !strcmp(str(st->raw,"type"),"dumpDsp"))return st->label;
        return T("install.preparing");
    }
    if(j->cia && st && st->bytes)return T("status.downloading");
    return st?st->label:T("install.preparing");
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
                installed_commit_transaction(j->app, j->tag, j->experimental, j->cia, j->files, j->dirs, j->hashes,
                                             j->transaction));
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
