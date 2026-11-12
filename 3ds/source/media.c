// Single persistent worker: only verified, atomically published files are playable.
#include "media.h"
#ifdef MEDIA_HOST_TEST
#include "../tests/media_host_platform.h"
#else
#include "common.h"
#include "network.h"
#include <mbedtls/sha256.h>
#endif
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

#ifndef MEDIA_CACHE
#define MEDIA_CACHE DATA_DIR "/cache/videos"
#endif
#ifndef MEDIA_MANIFEST
#define MEDIA_MANIFEST "romfs:/media.json"
#endif
#ifndef MEDIA_RETRY_MS
#define MEDIA_RETRY_MS 15000u
#endif
#define LOAD(x) __atomic_load_n(&(x), __ATOMIC_ACQUIRE)
#define STORE(x, v) __atomic_store_n(&(x), (v), __ATOMIC_RELEASE)

typedef struct {
    MediaItem view;
    char url[384], hash[65];
    bool eligible;
    uint64_t retry_at;
} Entry;
static struct {
    Thread thread;
    LightLock lock;
    bool initialized, stop, done, paused, clear_pending;
    bool loading, suspended, ready_event;
    unsigned count;
    int priority, clear_result;
    char error[160];
    Entry entries[MEDIA_MAX_ITEMS];
} q;
// Main-thread-only foreground watcher. It never owns the worker.
static struct {
    bool pending;
    int terminal;
    char app[49], id[49], path[256], error[160];
} watch;

static void lock(void) { LightLock_Lock(&q.lock); }
static void unlock(void) { LightLock_Unlock(&q.lock); }
static bool interrupted(void) { return LOAD(q.stop) || LOAD(q.paused) || LOAD(q.clear_pending); }
static uint64_t now_ms(void) { return osGetTime(); }
#ifdef MEDIA_HOST_TEST
static void bg_wait(void) {}
#endif
static bool safe_id(const char *s) {
    if (!s || !*s || strlen(s) > 48) return false;
    for (; *s; ++s)
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
              (*s >= '0' && *s <= '9') || *s == '-' || *s == '_')) return false;
    return true;
}
static bool media_url_allowed(const char *url){
    return url&&(!strncmp(url,"https://github.com/",19)||!strncmp(url,"https://raw.githubusercontent.com/",34));
}
static int find_locked(const char *app, const char *id) {
    if (!app || !id) return -1;
    for (unsigned i = 0; i < q.count; i++)
        if (!strcmp(q.entries[i].view.appid, app) && !strcmp(q.entries[i].view.id, id)) return (int)i;
    return -1;
}
static void queue_error(const char *error) {
    lock(); snprintf(q.error, sizeof q.error, "%s", error); unlock();
}
static void phase(unsigned i, MediaState state) {
    lock();
    if (!LOAD(q.clear_pending)) {
        q.entries[i].view.state = state;
        q.entries[i].view.received = 0;
        q.entries[i].view.error[0] = 0;
    }
    unlock();
}
static void received(unsigned i, unsigned bytes) {
    lock();
    if (!LOAD(q.clear_pending)) q.entries[i].view.received = bytes;
    unlock();
}
// All manifest IO is worker-owned; count is published only after validation.
static void load_manifest(void) {
    struct stat st;
    if (stat(MEDIA_MANIFEST, &st) || st.st_size > 512 * 1024 || st.st_size < 2) {
        queue_error("Media manifest missing or too large"); goto done;
    }
    json_error_t error;
    json_t *manifest = json_load_file(MEDIA_MANIFEST, JSON_REJECT_DUPLICATES, &error);
    if (!json_is_object(manifest)) {
        json_decref(manifest); queue_error("Invalid media manifest"); goto done;
    }
    if (json_object_size(manifest) > MEDIA_MAX_ITEMS) {
        json_decref(manifest); queue_error("Too many media entries (maximum 128)"); goto done;
    }
    unsigned count = 0;
    const char *key; json_t *v;
    json_object_foreach(manifest, key, v) {
        char app[49], id[49];
        const char *slash = strchr(key, '/');
        if (!slash || slash == key || (size_t)(slash - key) > 48 || strlen(slash + 1) > 48) {
            queue_error("Invalid media manifest key"); goto invalid;
        }
        memcpy(app, key, slash - key); app[slash - key] = 0;
        snprintf(id, sizeof id, "%s", slash + 1);
        if (!safe_id(app) || !safe_id(id)) { queue_error("Invalid media manifest key"); goto invalid; }
        Entry *e = &q.entries[count]; // invisible while count == 0
        memset(e, 0, sizeof *e);
        snprintf(e->view.appid, sizeof e->view.appid, "%s", app);
        snprintf(e->view.id, sizeof e->view.id, "%s", id);
        const char *url = json_string_value(json_object_get(v, "url"));
        const char *hash = json_string_value(json_object_get(v, "sha256"));
        json_t *size = json_object_get(v, "bytes");
        json_int_t bytes = json_integer_value(size);
        bool valid = media_url_allowed(url) && strlen(url) < sizeof e->url &&
                     hash && strlen(hash) == 64 && json_is_integer(size) && bytes >= 20 && bytes <= 512 * 1024 * 1024;
        if (valid) for (int k = 0; k < 64; k++) if (!isxdigit((unsigned char)hash[k])) valid = false;
        if (!valid) {
            e->view.state = MEDIA_ERROR;
            snprintf(e->view.error, sizeof e->view.error, "Invalid media URL, size or SHA-256");
        } else {
            snprintf(e->url, sizeof e->url, "%s", url);
            for (int k = 0; k < 64; k++) e->hash[k] = (char)tolower((unsigned char)hash[k]);
            e->view.total = (unsigned)bytes;
            // Preserve existing cache filenames. The content hash makes any ambiguous
            // app/id hyphen boundary share only identical verified content.
            snprintf(e->view.path, sizeof e->view.path, MEDIA_CACHE "/%s-%s-%s.fvid", app, id, e->hash);
            e->view.state = MEDIA_QUEUED;
            e->eligible = true;
        }
        count++;
    }
    lock();
    if (q.suspended) for (unsigned i = 0; i < count; i++) {
        q.entries[i].eligible = false;
        q.entries[i].view.state = MEDIA_CLEARED;
    }
    q.count = count;
    unlock();
invalid:
    json_decref(manifest);
done:
    lock(); q.loading = false; unlock();
}

static bool valid_file(unsigned index, const Entry *job, const char *path) {
    struct stat st;
    if (stat(path, &st) || !S_ISREG(st.st_mode) || st.st_size != job->view.total || interrupted()) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    phase(index, MEDIA_VERIFYING);
    mbedtls_sha256_context c;
    mbedtls_sha256_init(&c);
    int rc = mbedtls_sha256_starts_ret(&c, 0);
    unsigned bytes = 0;
    unsigned char buf[16384], digest[32];
    size_t n;
    while (!rc && !interrupted() && (bg_wait(), n = fread(buf, 1, sizeof buf, f))) {
        rc = mbedtls_sha256_update_ret(&c, buf, n);
        bytes += n;
        received(index, bytes);
    }
    bool ok = !rc && !interrupted() && !ferror(f) && bytes == job->view.total && !mbedtls_sha256_finish_ret(&c, digest);
    fclose(f);
    mbedtls_sha256_free(&c);
    char hex[65];
    if (ok) {
        for (int k = 0; k < 32; k++) snprintf(hex + k * 2, 3, "%02x", digest[k]);
        ok = !strcmp(hex, job->hash);
    }
    return ok;
}
typedef struct { FILE *file; unsigned index, bytes, total; bool io_error; } Transfer;
static size_t receive(char *data, size_t size, size_t count, void *opaque) {
    Transfer *t = opaque;
    if (size && count > SIZE_MAX / size) return 0;
    size_t n = size * count;
    bg_wait();  // HOME Menu or sleep: the SD card and the network are theirs
    if (interrupted() || t->bytes > t->total || n > t->total - t->bytes) return 0;
    size_t written = fwrite(data, 1, n, t->file);
    if (written != n) t->io_error = true;
    t->bytes += written;
    received(t->index, t->bytes);
    return written;
}
static int progress(void *unused, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) { return interrupted(); }
static bool transient(CURLcode result, long code) {
    return code == 408 || code == 429 || code >= 500 || result == CURLE_COULDNT_RESOLVE_HOST ||
           result == CURLE_COULDNT_RESOLVE_PROXY || result == CURLE_COULDNT_CONNECT ||
           result == CURLE_OPERATION_TIMEDOUT || result == CURLE_RECV_ERROR ||
           result == CURLE_SEND_ERROR || result == CURLE_PARTIAL_FILE || result == CURLE_GOT_NOTHING;
}
static void run_item(unsigned index) {
    Entry job;
    lock(); job = q.entries[index]; unlock();
    char tmp[272], error[160] = {0};
    snprintf(tmp, sizeof tmp, "%s.part", job.view.path);
    bool ok = false, retryable = false, attempted = false;
    mkdir(DATA_DIR "/cache", 0777); mkdir(MEDIA_CACHE, 0777);
    if (valid_file(index, &job, job.view.path)) { ok = true; goto done; }
    if (interrupted()) goto done;
    // Never expose stale/corrupt content; remove only this entry's own cache path.
    if (remove(job.view.path) && errno != ENOENT) {
        snprintf(error, sizeof error, "Cannot remove invalid cache file"); goto done;
    }
    remove(tmp);
    FS_ArchiveResource r;
    if (R_SUCCEEDED(FSUSER_GetArchiveResource(&r, SYSTEM_MEDIATYPE_SD)) &&
        (uint64_t)r.freeClusters * r.clusterSize < (uint64_t)job.view.total + 1024 * 1024) {
        snprintf(error, sizeof error, "Not enough space on SD"); goto done;
    }
    Transfer transfer = {.index = index, .total = job.view.total};
    transfer.file = fopen(tmp, "wb");
    if (!transfer.file) { snprintf(error, sizeof error, "Cannot write to SD"); goto done; }
    CURL *c = curl_easy_init();
    if (!c) { fclose(transfer.file); snprintf(error, sizeof error, "Network initialization failed"); goto done; }
    phase(index, MEDIA_DOWNLOADING);
    lock(); q.entries[index].view.attempts++; unlock(); attempted = true;
    char detail[CURL_ERROR_SIZE] = {0};
    network_options(c);
    curl_easy_setopt(c, CURLOPT_URL, job.url);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 0L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 128L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, detail);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, progress);
    CURLcode result = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(c);
    bool closed = fclose(transfer.file) == 0;
    if (interrupted()) goto done;
    if (!closed || transfer.io_error) { snprintf(error, sizeof error, "Cannot write video to SD"); goto done; }
    if (result != CURLE_OK || code != 200) {
        retryable = transient(result, code);
        snprintf(error, sizeof error, "HTTP %ld: %.130s", code, detail[0] ? detail : curl_easy_strerror(result));
        goto done;
    }
    if (!valid_file(index, &job, tmp)) { snprintf(error, sizeof error, "Video size or SHA-256 mismatch"); goto done; }
    if (interrupted()) goto done;
    if (rename(tmp, job.view.path)) { snprintf(error, sizeof error, "Could not save verified video"); goto done; }
    ok = true;
done:
    remove(tmp);
    lock();
    Entry *e = &q.entries[index];
    if (interrupted()) {
        if (attempted && e->view.attempts) e->view.attempts--;
        if (!LOAD(q.clear_pending)) e->view.state = MEDIA_QUEUED;
        e->view.received = 0;
    } else if (ok) {
        e->view.state = MEDIA_COMPLETED;
        e->view.received = e->view.total;
        e->view.error[0] = 0;
        e->eligible = false;
        q.ready_event = true;
    } else {
        e->view.state = MEDIA_ERROR;
        snprintf(e->view.error, sizeof e->view.error, "%s", error[0] ? error : "Media operation failed");
        e->eligible = retryable && e->view.attempts < 3;
        e->retry_at = e->eligible ? now_ms() + MEDIA_RETRY_MS * (e->view.attempts == 1 ? 1 : 4) : 0;
    }
    unlock();
}
static void clear_files(void) {
    bool ok = true;
    DIR *d = opendir(MEDIA_CACHE);
    if (!d && errno != ENOENT) ok = false;
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            size_t len = strlen(e->d_name);
            if (e->d_name[0] == '.' || len < 5 ||
                (strcmp(e->d_name + len - 5, ".fvid") && strcmp(e->d_name + len - 5, ".part"))) continue;
            char path[512];
            snprintf(path, sizeof path, MEDIA_CACHE "/%s", e->d_name);
            if (remove(path)) ok = false;
        }
        closedir(d);
    }
    lock();
    q.clear_result = ok ? 1 : -1;
    if (!ok) snprintf(q.error, sizeof q.error, "Some video cache files could not be removed");
    STORE(q.clear_pending, false);
    unlock();
}
static void worker(void *unused) {
    load_manifest();
    while (!LOAD(q.stop)) {
        if (LOAD(q.clear_pending)) { clear_files(); continue; }
        int next = -1;
        if (!LOAD(q.paused)) {
            uint64_t now = now_ms();
            lock();
            for (unsigned i = 0; i < q.count; i++) {
                Entry *e = &q.entries[i];
                if (e->eligible && (e->view.state == MEDIA_QUEUED ||
                    (e->view.state == MEDIA_ERROR && now >= e->retry_at))) {
                    if (next < 0 || (int)i == q.priority) next = (int)i;
                    if ((int)i == q.priority) break;
                }
            }
            if (next == q.priority) q.priority = -1;
            unlock();
        }
        bg_wait();
        if (next >= 0) run_item((unsigned)next);
        else svcSleepThread(20000000); // idle worker, no main-thread wait or busy spin
    }
    STORE(q.done, true);
}

bool media_start(void) {
    if (q.initialized) return q.thread && !LOAD(q.stop);
    bool initially_paused = LOAD(q.paused);
    memset(&q, 0, sizeof q);
    q.paused = initially_paused;
    LightLock_Init(&q.lock);
    q.initialized = q.loading = true;
    q.priority = -1;
    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    q.thread = threadCreate(worker, NULL, 64 * 1024, priority + 2, -2, false);
    if (!q.thread) {
        q.loading = false;
        snprintf(q.error, sizeof q.error, "Not enough memory for media worker");
        return false;
    }
    return true;
}
void media_pump(void) {
    if (q.thread && LOAD(q.done) && threadJoin(q.thread, 0) == 0) {
        threadFree(q.thread); q.thread = NULL;
    }
}
bool media_loading(void) { if (!q.initialized) return false; lock(); bool v = q.loading; unlock(); return v; }
unsigned media_count(void) { if (!q.initialized) return 0; lock(); unsigned n = q.count; unlock(); return n; }
bool media_item(unsigned i, MediaItem *out) {
    if (!q.initialized || !out) return false;
    lock();
    bool ok = i < q.count;
    if (ok) {
        *out = q.entries[i].view;
        out->progress = out->total ? fminf(1, out->received / (float)out->total) : 0;
        uint64_t now = now_ms(), at = q.entries[i].retry_at;
        out->retry_seconds = out->state == MEDIA_ERROR && q.entries[i].eligible && at > now ? (unsigned)((at - now + 999) / 1000) : 0;
    }
    unlock(); return ok;
}
int media_find(const char *app, const char *id) {
    if (!q.initialized) return -1;
    lock(); int i = find_locked(app, id); unlock(); return i;
}
bool media_prioritize(const char *app, const char *id) {
    if (!q.initialized) return false;
    lock(); int i = find_locked(app, id);
    bool ok = i >= 0 && q.entries[i].eligible && !LOAD(q.clear_pending);
    if (ok && q.entries[i].view.state == MEDIA_QUEUED) q.priority = i;
    unlock(); return ok;
}
static bool retry_locked(unsigned i) {
    Entry *e = &q.entries[i];
    if (!e->url[0] || (e->view.state != MEDIA_ERROR && e->view.state != MEDIA_CLEARED)) return false;
    e->view.state = MEDIA_QUEUED;
    e->view.received = e->view.attempts = 0;
    e->view.error[0] = 0;
    e->eligible = true; e->retry_at = 0;
    return true;
}
bool media_retry(const char *app, const char *id) {
    if (!q.initialized || LOAD(q.stop)) return false;
    lock(); int i = find_locked(app, id);
    bool ok = i >= 0 && !LOAD(q.clear_pending) && retry_locked((unsigned)i);
    if (ok) q.priority = i;
    unlock(); return ok;
}
void media_retry_all(void) {
    if (!q.initialized || LOAD(q.stop)) return;
    lock();
    if (!LOAD(q.clear_pending)) {
        q.suspended = false;
        for (unsigned i = 0; i < q.count; i++) retry_locked(i);
    }
    unlock();
}
void media_set_paused(bool paused) { STORE(q.paused, paused); }
bool media_paused(void) { return LOAD(q.paused); }
bool media_path_ready(const char *path) {
    if (!q.initialized || !path || !*path) return false;
    lock(); bool ready = false;
    if (!LOAD(q.clear_pending)) for (unsigned i = 0; i < q.count; i++)
        if (q.entries[i].view.state == MEDIA_COMPLETED && !strcmp(path, q.entries[i].view.path)) { ready = true; break; }
    unlock(); return ready;
}
bool media_ready_path(const char *app, const char *id, char *out, size_t size) {
    if (!out || !size) return false;
    out[0] = 0;
    if (!q.initialized) return false;
    lock(); int i = find_locked(app, id);
    bool ready = i >= 0 && !LOAD(q.clear_pending) && q.entries[i].view.state == MEDIA_COMPLETED && strlen(q.entries[i].view.path) < size;
    if (ready) snprintf(out, size, "%s", q.entries[i].view.path);
    unlock(); return ready;
}
bool media_take_ready(void) {
    if (!q.initialized) return false;
    lock(); bool ready = q.ready_event; q.ready_event = false; unlock(); return ready;
}
const char *media_queue_error(void) {
    static char error[160];
    error[0] = 0;
    if (q.initialized) { lock(); snprintf(error, sizeof error, "%s", q.error); unlock(); }
    return error;
}
bool media_clear(void) {
    if (!q.initialized && !media_start()) return false;
    if (!q.thread || LOAD(q.stop)) return false;
    lock();
    q.suspended = true;
    q.clear_result = 0;
    q.error[0] = 0;
    q.ready_event = false;
    q.priority = -1;
    STORE(q.clear_pending, true);
    for (unsigned i = 0; i < q.count; i++) {
        q.entries[i].eligible = false;
        q.entries[i].view.state = MEDIA_CLEARED;
        q.entries[i].view.received = 0;
        q.entries[i].retry_at = 0;
    }
    unlock();
    watch.path[0] = 0;
    return true;
}
int media_clear_poll(void) { if (!q.initialized) return 0; lock(); int r = q.clear_result; unlock(); return r; }
bool media_clearing(void) { return LOAD(q.clear_pending); }
void media_shutdown(void) { STORE(q.stop, true); }
#ifdef MEDIA_HOST_TEST
#define thread_reap(t) (threadJoin(*(t), U64_MAX), threadFree(*(t)), *(t) = NULL, true)
#endif
// The transfer aborts at its next callback; a worker still inside DNS or TLS at the
// shutdown deadline is left behind rather than waited for.
void media_exit(void) {
    media_shutdown();
    if (q.thread) thread_reap(&q.thread);
}

bool media_request(const char *app, const char *id) {
    if (watch.pending) return false;
    memset(&watch, 0, sizeof watch);
    if (!safe_id(app) || !safe_id(id) || !media_start()) {
        snprintf(watch.error, sizeof watch.error, "Video unavailable"); return false;
    }
    snprintf(watch.app, sizeof watch.app, "%s", app);
    snprintf(watch.id, sizeof watch.id, "%s", id);
    watch.pending = true;
    media_prioritize(app, id);
    return true;
}
bool media_busy(void) { return watch.pending; }
void media_cancel(void) { if (watch.pending) watch.terminal = -2; }
int media_poll(void) {
    if (!watch.pending) return 0;
    int result = watch.terminal;
    if (!result && media_loading()) return 0;
    if (!result) {
        int i = media_find(watch.app, watch.id);
        MediaItem item;
        if (i < 0 || !media_item((unsigned)i, &item)) {
            snprintf(watch.error, sizeof watch.error, "Video unavailable"); result = -1;
        } else if (item.state == MEDIA_COMPLETED && media_ready_path(watch.app, watch.id, watch.path, sizeof watch.path)) result = 1;
        else if (item.state == MEDIA_ERROR) { snprintf(watch.error, sizeof watch.error, "%s", item.error); result = -1; }
        else if (item.state == MEDIA_CLEARED) result = -2;
    }
    if (result) watch.pending = false;
    return result;
}
const char *media_path(void) { return media_path_ready(watch.path) ? watch.path : ""; }
const char *media_error(void) { return watch.error; }
static MediaItem watched(void) {
    MediaItem item = {0};
    int i = media_find(watch.app, watch.id);
    if (i >= 0) media_item((unsigned)i, &item);
    return item;
}
unsigned media_received(void) { return watched().received; }
unsigned media_total(void) { return watched().total; }
float media_progress(void) { return watched().progress; }
bool media_verifying(void) { return watched().state == MEDIA_VERIFYING; }
