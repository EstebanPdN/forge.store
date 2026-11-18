#ifdef FORGE_DETECT_HOST_TEST
#include "../tests/detect-host.h"  // the same scan against a host folder and mocked AM
#else
#include "detect.h"
#include "catalog.h"
#include "install.h"
#endif
#include "detect_rules.h"
#include <dirent.h>
#include <sys/stat.h>

// Universal-Updater keeps what it installed, per store and entry, with the entry's update
// time at installation: {"Universal-DB": {"NetPass": {"installed": [...], "updated": "..."}}}.
#define UU_META "sdmc:/3ds/Universal-Updater/MetaData.json"
#define UU_LIMIT (2 * 1024 * 1024)
#define ROOT_LIMIT 4096

typedef struct {
    App *app;
    char id[136];
    uint64_t titles[DETECT_TITLES];
    int ntitles, nfiles;
    char (*files)[DETECT_PATH];
    char uu[160];
    // Written by the worker.
    uint64_t title;
    uint16_t version;
    int file;  // candidate index + 1
    char uu_updated[DETECT_STAMP];
} Item;

static Thread worker;
static Item *items;  // the worker's until ready, then the main thread's
static int nitems;
static bool wanted = true, stop;
static volatile bool ready;
static char note[96] = "idle";

static bool stopped(void) { return __atomic_load_n(&stop, __ATOMIC_ACQUIRE); }

static void free_items(void) {
    for (int i = 0; i < nitems; i++) free(items[i].files);
    free(items);
    items = NULL;
    nitems = 0;
}

static int cmp_title(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}
static int cmp_name(const void *a, const void *b) { return strcasecmp(*(char *const *)a, *(char *const *)b); }

typedef struct {
    char **names;
    int count;
} Listing;

// One listing of sdmc:/3ds answers most lookups without opening files.
static Listing list_root(void) {
    Listing l = {0};
    int cap = 0;
    DIR *d = opendir("sdmc:/3ds");
    if (!d) return l;
    struct dirent *e;
    while (!stopped() && l.count < ROOT_LIMIT && (e = readdir(d))) {
        if (l.count == cap) {
            int grown = cap ? cap * 2 : 128;
            char **more = realloc(l.names, grown * sizeof *more);
            if (!more) break;
            l.names = more;
            cap = grown;
        }
        if (!(l.names[l.count] = strdup(e->d_name))) break;
        l.count++;
    }
    closedir(d);
    if (l.names) qsort(l.names, l.count, sizeof *l.names, cmp_name);
    return l;
}

static void free_listing(Listing *l) {
    for (int i = 0; i < l->count; i++) free(l->names[i]);
    free(l->names);
}

static bool present(const Listing *root, const char *path) {
    // Under sdmc:/3ds the first name must be in the listing; only then is the file opened.
    if (root->names && !strncasecmp(path, "sdmc:/3ds/", 10)) {
        const char *first = path + 10, *slash = strchr(first, '/');
        size_t n = slash ? (size_t)(slash - first) : strlen(first);
        char name[256];
        if (!n || n >= sizeof name) return false;
        memcpy(name, first, n);
        name[n] = 0;
        const char *key = name;
        if (!bsearch(&key, root->names, root->count, sizeof *root->names, cmp_name)) return false;
    }
    struct stat st;
    return !stat(path, &st) && (S_ISDIR(st.st_mode) || st.st_size > 0);
}

static json_t *uu_records(void) {
    struct stat st;
    if (stat(UU_META, &st) || st.st_size <= 0 || st.st_size > UU_LIMIT) return NULL;
    json_t *all = json_load_file(UU_META, 0, NULL);
    json_t *store = json_incref(json_object_get(all, "Universal-DB"));
    json_decref(all);
    if (json_is_object(store)) return store;
    json_decref(store);
    return NULL;
}

static void run(void *ctx) {
    (void)ctx;
    u32 count = 0, got = 0;
    uint64_t *titles = NULL;
    bg_wait();
    if (R_SUCCEEDED(AM_GetTitleCount(MEDIATYPE_SD, &count)) && count && count <= 8192 &&
        (titles = malloc(count * sizeof *titles))) {
        if (R_FAILED(AM_GetTitleList(&got, MEDIATYPE_SD, count, titles))) got = 0;
        qsort(titles, got, sizeof *titles, cmp_title);
    }
    bg_wait();
    Listing root = list_root();
    bg_wait();
    json_t *uu = uu_records();
    int found = 0;
    for (int i = 0; i < nitems && !stopped(); i++) {
        Item *it = &items[i];
        if (!(i & 15)) bg_wait();
        for (int k = 0; k < it->ntitles && !it->title; k++) {
            if (!got || !bsearch(&it->titles[k], titles, got, sizeof *titles, cmp_title)) continue;
            AM_TitleInfo info;
            it->title = it->titles[k];
            if (R_SUCCEEDED(AM_GetTitleInfo(MEDIATYPE_SD, 1, &it->title, &info))) it->version = info.version;
        }
        for (int k = 0; k < it->nfiles && !it->file; k++)
            if (present(&root, it->files[k])) it->file = k + 1;
        // Universal-Updater's record only dates an installation that is really there.
        if (uu && it->uu[0] && (it->title || it->file)) {
            json_t *record = json_object_get(uu, it->uu);
            if (json_array_size(json_object_get(record, "installed")))
                detect_stamp(json_string_value(json_object_get(record, "updated")), it->uu_updated);
        }
        found += it->title || it->file;
    }
    snprintf(note, sizeof note, "titles=%lu root=%d apps=%d found=%d uu=%s", (unsigned long)got, root.count, nitems,
             found, uu ? "yes" : "no");
    json_decref(uu);
    free_listing(&root);
    free(titles);
    __atomic_store_n(&ready, true, __ATOMIC_RELEASE);
}

// Main thread: what to look for, copied so the worker never reads the live catalog.
static bool snapshot(void) {
    items = calloc(NAPPS > 0 ? NAPPS : 1, sizeof *items);
    if (!items) return false;
    char files[DETECT_FILES][DETECT_PATH];
    for (int i = 0; i < NAPPS; i++) {
        App *a = &APPS[i];
        if (!a->id || a->retired || a->dev) continue;
        Item *it = &items[nitems];
        int nf = detect_candidates(a->j, files, DETECT_FILES);
        it->ntitles = detect_titles(a->j, it->titles, DETECT_TITLES);
        const char *uu = detect_uu_name(a->j);
        if (!nf && !it->ntitles) continue;
        if (nf && !(it->files = malloc(nf * sizeof *it->files))) nf = 0;
        if (nf) memcpy(it->files, files, nf * sizeof *it->files);
        it->nfiles = nf;
        it->app = a;
        snprintf(it->id, sizeof it->id, "%s", a->id);
        snprintf(it->uu, sizeof it->uu, "%s", uu ? uu : "");
        nitems++;
    }
    return true;
}

static void clear(App *a) {
    a->found_title = 0;
    a->found_version = 0;
    a->uu_updated[0] = 0;
    free(a->found_path);
    a->found_path = NULL;
}

static void apply(void) {
    // A complete scan replaces everything the previous one found.
    for (int i = 0; i < NAPPS; i++) clear(&APPS[i]);
    for (int i = 0; i < nitems; i++) {
        Item *it = &items[i];
        App *a = it->app;
        if (!a->id || strcmp(a->id, it->id)) continue;  // the catalog changed meanwhile
        a->found_title = it->title;
        a->found_version = it->version;
        snprintf(a->uu_updated, sizeof a->uu_updated, "%s", it->uu_updated);
        free(a->found_path);
        a->found_path = it->file ? strdup(it->files[it->file - 1]) : NULL;
    }
}

void detect_request(void) { wanted = true; }

void detect_forget(const char *app_id) {
    for (int i = 0; app_id && i < NAPPS; i++)
        if (APPS[i].id && !strcmp(APPS[i].id, app_id)) clear(&APPS[i]);
    wanted = true;
}

bool detect_pump(void) {
    if (!worker && !items && wanted && !install_active() && !stopped()) {
        wanted = false;
        if (!snapshot()) { free_items(); return false; }
        s32 priority = 0x30;
        svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
        worker = threadCreate(run, NULL, 24 * 1024, priority + 2, -2, false);
        if (!worker) { free_items(); wanted = true; return false; }
    }
    if (!worker || !__atomic_load_n(&ready, __ATOMIC_ACQUIRE) || !thread_done(&worker)) return false;
    __atomic_store_n(&ready, false, __ATOMIC_RELEASE);
    bool complete = !stopped();
    if (complete) apply();
    free_items();
    return complete;
}

void detect_stop(void) { __atomic_store_n(&stop, true, __ATOMIC_RELEASE); }

void detect_exit(void) {
    detect_stop();
    if (!thread_reap(&worker)) return;  // still running: it owns items until the process ends
    free_items();
}

const char *detect_status(void) { return note; }
