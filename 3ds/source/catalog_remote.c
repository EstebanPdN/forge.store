#include "catalog_remote.h"
#include "catalog.h"
#include "install.h"
#include "network.h"
#include "reader_cache.h"
#include <time.h>
#include "site_progress.h"
// The repository's catalog is kept current by a scheduled GitHub Action that follows
// every listed repository and my website's Progress page. The store reads that file from raw.githubusercontent.com
// (no API rate limit) on launch and every 30 minutes, and adopts newer releases and
// install plans for the apps it already ships. Art and text stay bundled.
#define URL "https://raw.githubusercontent.com/EstebanPdN/forge.store/main/catalog/forge-store.json"
#define CACHE DATA_DIR "/catalog-remote.cache"
#define LIMIT (2u * 1024u * 1024u)
#define PERIOD 1800.f
#define SITE "https://estebanpdn.github.io/"
static Thread worker;
static bool stop, ready, from_cache_done;
static json_t *result;
static SiteProgress site[SITE_PROGRESS_MAX];  // worker writes before ready, main reads after
static int site_count;
static bool site_done;
static char note[128] = "idle";  // last outcome, for the test log
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
// One bounded HTTPS GET into a NUL-terminated buffer; NULL on any failure.
static char *fetch(const char *url, size_t *length) {
    CURL *c = curl_easy_init();
    Buffer b = {0};
    if (!c) return NULL;
    network_options(c);
    curl_easy_setopt(c, CURLOPT_URL, url);
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
    if (rc != CURLE_OK || code != 200 || !b.data || stopped()) {
        snprintf(note, sizeof note, "%.48s: curl %d http %ld", url + 8, (int)rc, code);
        free(b.data);
        return NULL;
    }
    *length = b.length;
    return b.data;
}
static void run(void *ctx) {
    bool cache_only = ctx != NULL;
    size_t n = 0;
    if (cache_only) {
        char *cached = reader_cache_load(CACHE, (uint64_t)time(NULL), UINT64_MAX, LIMIT, &n);
        if (cached) { result = parse(cached, n); free(cached); }
    } else {
        char *body = fetch(URL, &n);
        if (body) {
            result = parse(body, n);
            if (result) { bg_wait(); reader_cache_save(CACHE, (uint64_t)time(NULL), body, n, LIMIT); }
            free(body);
        }
        // My website's Progress page is the source of development percentages; once per launch.
        if (!site_done && !stopped()) {
            char *page = fetch(SITE, &n);
            if (page) { site_count = site_progress_parse(page, n, site, SITE_PROGRESS_MAX); free(page);
                snprintf(note, sizeof note, "catalog=%s site cards=%d", result ? "ok" : "failed", site_count); }
            site_done = true;
        }
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
        // Development percentages: the Action copies them from my website, so an offline
        // or failed website read still gets the last synchronized value.
        json_t *progress = json_object_get(r, "progress");
        if (a->progress >= 0 && json_is_integer(progress) && json_integer_value(progress) >= 0 &&
            json_integer_value(progress) <= 100 && json_integer_value(progress) != a->progress) {
            a->progress = (int)json_integer_value(progress);
            json_object_set(a->j, "progress", progress);
            changed = true;
        }
        app_refresh_updated(a);  // releases may have been replaced: never keep a pointer into them
        json_t *plan = json_object_get(r, "install");
        if (json_is_object(plan) && json_object_size(plan) && !json_equal(plan, json_object_get(a->j, "install")))
            if (!json_object_set(a->j, "install", plan)) changed = true;
    }
    return changed;
}
// Apps name their card with "progressCard" (e.g. "port-worms") in the catalog.
static bool apply_progress(void) {
    bool changed = false;
    for (int i = 0; i < site_count; i++)
        for (int k = 0; k < BUILTIN_APPS; k++) {
            const char *card = json_string_value(json_object_get(APPS[k].j, "progressCard"));
            if (!card || strcmp(card, site[i].card) || APPS[k].progress == site[i].value) continue;
            APPS[k].progress = site[i].value;
            json_object_set_new(APPS[k].j, "progress", json_integer(site[i].value));
            changed = true;
        }
    site_count = 0;  // applied once
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
    changed |= apply_progress();
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
const char *catalog_remote_status(void) { return note; }
