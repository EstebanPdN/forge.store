#include "github_prefetch.h"
#include "http_cache.h"
#include "catalog.h"
#include "gh_markup.h"
static Thread worker;
static bool stop, paused;
static char repos[24][160];
static int repo_count;
static bool interrupted(void *ctx) {
    return __atomic_load_n(&stop, __ATOMIC_ACQUIRE) || __atomic_load_n(&paused, __ATOMIC_ACQUIRE);
}
static bool wait_ready(void) {
    while (interrupted(NULL) && !__atomic_load_n(&stop, __ATOMIC_ACQUIRE))
        svcSleepThread(100000000);
    return !__atomic_load_n(&stop, __ATOMIC_ACQUIRE);
}
static char *fetch(const char *url, const char *accept, size_t limit) {
    if (!wait_ready())
        return NULL;
    size_t length;
    char error[160];
    return http_cache_get(url, accept, limit, true, interrupted, NULL, &length, error, sizeof error);
}
static char seen_images[128][1024];
static int seen_count;
static void image(const char *url) {
    if (!url || !*url || strlen(url) >= sizeof seen_images[0])
        return;
    for (int i = 0; i < seen_count; i++)
        if (!strcmp(url, seen_images[i]))
            return;
    char *data = fetch(url, "*/*", 2 * 1024 * 1024);
    if (data && seen_count < 128)
        snprintf(seen_images[seen_count++], sizeof seen_images[0], "%s", url);
    free(data);
}
static void readme(const char *repo, const char *branch) {
    char url[320];
    snprintf(url, sizeof url, "https://api.github.com/repos/%s/readme", repo);
    char *data = fetch(url, "application/vnd.github.raw+json", 1024 * 1024);
    if (!data)
        return;
    // Cache common images for both themes, without allocating any GPU textures.
    for (int dark = 0; dark < 2 && wait_ready(); dark++) {
        GhDocument doc = {0};
        if (gh_document_parse(&doc, data, repo, branch, dark)) {
            int images = 0;
            for (int i = 0; i < doc.count && images < 12; i++)
                if (doc.nodes[i].kind == GH_IMAGE) {
                    image(doc.nodes[i].url);
                    images++;
                }
        }
        gh_document_free(&doc);
    }
    free(data);
}
static void run(void *unused) {
    char *data = fetch("https://api.github.com/users/EstebanPdN", "application/vnd.github+json", 1024 * 1024);
    json_t *profile = data ? json_loads(data, 0, NULL) : NULL;
    free(data);
    image(json_string_value(json_object_get(profile, "avatar_url")));
    json_decref(profile);
    data = fetch("https://api.github.com/users/EstebanPdN/repos?sort=pushed&per_page=40",
                 "application/vnd.github+json", 1024 * 1024);
    free(data);
    readme("EstebanPdN/EstebanPdN", "main");
    for (int i = 0; i < repo_count && wait_ready(); i++) {
        char url[320];
        snprintf(url, sizeof url, "https://api.github.com/repos/%s", repos[i]);
        data = fetch(url, "application/vnd.github+json", 1024 * 1024);
        json_t *meta = data ? json_loads(data, 0, NULL) : NULL;
        free(data);
        const char *branch = json_string_value(json_object_get(meta, "default_branch"));
        readme(repos[i], branch ? branch : "main");
        image(json_string_value(json_object_get(json_object_get(meta, "owner"), "avatar_url")));
        json_decref(meta);
        snprintf(url, sizeof url, "https://api.github.com/repos/%s/releases?per_page=12", repos[i]);
        data = fetch(url, "application/vnd.github+json", 1024 * 1024);
        free(data);
        snprintf(url, sizeof url, "https://api.github.com/repos/%s/commits?per_page=10", repos[i]);
        data = fetch(url, "application/vnd.github+json", 1024 * 1024);
        free(data);
        if (interrupted(NULL) && !__atomic_load_n(&stop, __ATOMIC_ACQUIRE))
            i--; // resume interrupted repository
    }
}
void github_prefetch_start(void) {
    if (worker)
        return;
    for (int i = 0; i < BUILTIN_APPS && repo_count < 24; i++)
        if (APPS[i].repo && !strncmp(APPS[i].repo, "EstebanPdN/", 11)) {
            bool duplicate = false;
            for (int k = 0; k < repo_count; k++)
                if (!strcmp(repos[k], APPS[i].repo))
                    duplicate = true;
            if (!duplicate)
                snprintf(repos[repo_count++], sizeof repos[0], "%s", APPS[i].repo);
        }
    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    worker = threadCreate(run, NULL, 96 * 1024, priority + 3, -2, false);
}
void github_prefetch_pause(bool value) { __atomic_store_n(&paused, value, __ATOMIC_RELEASE); }
void github_prefetch_stop(void) { __atomic_store_n(&stop, true, __ATOMIC_RELEASE); }
void github_prefetch_exit(void) {
    github_prefetch_stop();
    thread_reap(&worker);
}
