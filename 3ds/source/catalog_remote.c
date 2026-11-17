#include "catalog_remote.h"
#include "catalog.h"
#include "install.h"
#include "network.h"
#include "reader_cache.h"
#include <time.h>
// The repository's catalog is kept current by a scheduled GitHub Action that follows
// every listed repository. The store reads that file from raw.githubusercontent.com
// (no API rate limit) on launch and every 30 minutes, and adopts newer releases and
// install plans for the apps it already ships. Art and text stay bundled.
#define URL "https://raw.githubusercontent.com/EstebanPdN/forge.store/main/catalog/forge-store.json"
#define CACHE DATA_DIR "/catalog-remote.cache"
#define LIMIT (2u * 1024u * 1024u)
#define PERIOD 1800.f
static Thread worker;
static bool stop, ready, from_cache_done;
static json_t *result;
static float next_at;
typedef struct { char *data; size_t length, capacity; } Buffer;
static bool stopped(void) { return __atomic_load_n(&stop, __ATOMIC_ACQUIRE); }
static size_t receive(char *p, size_t size, size_t count, void *ctx) {
    Buffer *b = ctx;
    bg_wait();
    if (stopped() || (size && count > SIZE_MAX / size)) return 0;
    size_t n = size * count;
    if (n > LIMIT - b->length) return 0;
    if (b->length + n + 1 > b->capacity) {
        size_t cap = b->capacity ? b->capacity * 2 : 65536;
        if (cap < b->length + n + 1) cap = b->length + n + 1;
        char *q = realloc(b->data, cap);
        if (!q) return 0;
        b->data = q; b->capacity = cap;
    }
    memcpy(b->data + b->length, p, n);
    b->length += n; b->data[b->length] = 0;
    return n;
}
static int progress(void *ctx, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) {
    (void)ctx; (void)a; (void)b; (void)c; (void)d;
    bg_wait();
    return stopped();
}
static json_t *parse(const char *data, size_t length) {
    json_t *j = json_loadb(data, length, JSON_REJECT_DUPLICATES, NULL);
    const char *format = json_string_value(json_object_get(j, "format"));
    if (!format || strcmp(format, "forge-store/1") || !json_is_array(json_object_get(j, "apps"))) {
        json_decref(j);
        return NULL;
    }
    return j;
}
static void run(void *ctx) {
    bool cache_only = ctx != NULL;
    if (cache_only) {
        size_t n = 0;
        char *cached = reader_cache_load(CACHE, (uint64_t)time(NULL), UINT64_MAX, LIMIT, &n);
        if (cached) { result = parse(cached, n); free(cached); }
    } else {
        CURL *c = curl_easy_init();
        Buffer b = {0};
        if (c) {
            network_options(c);
            curl_easy_setopt(c, CURLOPT_URL, URL);
            curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 6L);
            curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
            curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
            curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, receive);
            curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
            curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, progress);
            long code = 0;
            CURLcode rc = curl_easy_perform(c);
            curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
            curl_easy_cleanup(c);
            if (rc == CURLE_OK && code == 200 && b.data && !stopped()) {
                result = parse(b.data, b.length);
                if (result) { bg_wait(); reader_cache_save(CACHE, (uint64_t)time(NULL), b.data, b.length, LIMIT); }
            }
        }
        free(b.data);
    }
    __atomic_store_n(&ready, true, __ATOMIC_RELEASE);
}
static const char *date_of(json_t *release) {
    const char *d = json_string_value(json_object_get(release, "date"));
    return d ? d : "";
}
// Main thread only. Running jobs keep their own deep copies of release and script.
static bool apply(json_t *remote) {
    bool changed = false;
    size_t i; json_t *r;
    json_array_foreach(json_object_get(remote, "apps"), i, r) {
        const char *id = json_string_value(json_object_get(r, "id"));
        if (!id) continue;
        App *a = NULL;
        for (int k = 0; k < BUILTIN_APPS; k++) if (APPS[k].id && !strcmp(APPS[k].id, id)) { a = &APPS[k]; break; }
        if (!a || install_busy(a)) continue;
        json_t *have = json_object_get(a->j, "releases"), *want = json_object_get(r, "releases");
        if (json_is_object(have) && json_is_object(want)) {
            const char *channel; json_t *release;
            json_object_foreach(want, channel, release) {
                if (strcmp(channel, "stable") && strcmp(channel, "experimental")) continue;
                if (!json_is_string(json_object_get(release, "tag")) || !json_is_object(json_object_get(release, "assets"))) continue;
                json_t *current = json_object_get(have, channel);
                // Never step back behind a newer release the live per-repo sync already found.
                if (current && (json_equal(current, release) || strcmp(date_of(release), date_of(current)) < 0)) continue;
                if (!json_object_set(have, channel, release)) changed = true;
            }
        }
        json_t *plan = json_object_get(r, "install");
        if (json_is_object(plan) && json_object_size(plan) && !json_equal(plan, json_object_get(a->j, "install")))
            if (!json_object_set(a->j, "install", plan)) changed = true;
    }
    return changed;
}
bool catalog_remote_pump(bool may_apply) {
    bool installing = install_active() != NULL;
    if (!worker && !ready && NOW >= next_at && !installing) {
        s32 priority = 0x30;
        svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
        // First the copy saved from the last launch (works offline), then the network.
        bool cache_first = !from_cache_done;
        from_cache_done = true;
        worker = threadCreate(run, cache_first ? (void *)1 : NULL, 32 * 1024, priority + 2, -2, false);
        next_at = cache_first ? NOW : NOW + PERIOD;
    }
    if (!may_apply || installing || !__atomic_load_n(&ready, __ATOMIC_ACQUIRE)) return false;
    if (worker) { if (!thread_done(&worker)) return false; }
    bool changed = result && apply(result);
    json_decref(result);
    result = NULL;
    __atomic_store_n(&ready, false, __ATOMIC_RELEASE);
    return changed;
}
void catalog_remote_now(void) { if (!worker && !ready) next_at = 0; }
void catalog_remote_stop(void) { __atomic_store_n(&stop, true, __ATOMIC_RELEASE); }
void catalog_remote_exit(void) {
    catalog_remote_stop();
    if (!thread_reap(&worker)) return;
    json_decref(result);
    result = NULL;
}
