#include "github_prefetch.h"
#include "http_cache.h"
#include "catalog.h"
#include "gh_markup.h"
#include "reader_image.h"
#include "reader_cache.h"
#include <mbedtls/sha256.h>
static Thread worker;
static bool stop, paused;
static char repos[128][160];
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
    bool refresh=!http_cache_fresh(url,accept,!strcmp(accept,"*/*")||strstr(url,"/readme")?7*86400:strstr(url,"/commits")?86400:21600);
    return http_cache_get(url, accept, limit, refresh, interrupted, NULL, &length, error, sizeof error);
}
// Keep only URL digests: 16 KiB instead of reserving 512 KiB of Old 3DS RAM.
static unsigned char seen_images[512][32];
static int seen_count;
static void image(const char *url) {
    if (!url || !*url || strlen(url) >= 1024)
        return;
    unsigned char key[32];mbedtls_sha256_ret((const unsigned char*)url,strlen(url),key,0);
    for (int i = 0; i < seen_count; i++)
        if (!memcmp(key, seen_images[i],sizeof key))
            return;
    if(reader_image_bundled(url)) return;
    // Compare encoded bytes on a refresh. A changed image invalidates only its
    // decoded SD copy; unchanged ETag/304 responses keep the fast path intact.
    char *before=NULL,error[160];size_t old_length=0,new_length=0;
    bool refresh=!http_cache_fresh(url,"*/*",7*86400);
    unsigned char old_hash[32],new_hash[32];
    if(refresh) {
        char path[256];http_cache_path(url,"*/*",path,sizeof path);
        FILE *f=fopen(path,"rb");
        if(f){fclose(f);before=http_cache_get(url,"*/*",2*1024*1024,false,interrupted,NULL,&old_length,error,sizeof error);}
        if(before){mbedtls_sha256_ret((unsigned char*)before,old_length,old_hash,0);free(before);}
    }
    if(!wait_ready())return;
    char *data = http_cache_get(url,"*/*",2*1024*1024,refresh,interrupted,NULL,&new_length,error,sizeof error);
    if(data&&refresh){
        mbedtls_sha256_ret((unsigned char*)data,new_length,new_hash,0);
        if(!old_length||memcmp(old_hash,new_hash,32))reader_image_invalidate(url);
    }
    if (data && seen_count < 512)
        memcpy(seen_images[seen_count++],key,sizeof key);
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
            for (int i = 0; i < doc.count && images < GH_DOC_NODES; i++)
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
    // Rotate six repositories per launch; releases have their own catalog queue.
    // This stays within the anonymous API budget alongside release checks.
    unsigned cursor=0;size_t cursor_length=0;
    char *cursor_data=reader_cache_load(DATA_DIR "/github-prefetch.cursor",UINT64_MAX-1,UINT64_MAX,
                                      sizeof cursor,&cursor_length);
    if(cursor_data){if(cursor_length==sizeof cursor)memcpy(&cursor,cursor_data,sizeof cursor);free(cursor_data);}
    int limit=repo_count<6?repo_count:6;
    for (int step=0;step<limit&&wait_ready();step++) {
        int i=(cursor+step)%repo_count;
        char url[320];
        snprintf(url, sizeof url, "https://api.github.com/repos/%s", repos[i]);
        data = fetch(url, "application/vnd.github+json", 1024 * 1024);
        json_t *meta = data ? json_loads(data, 0, NULL) : NULL;
        free(data);
        const char *branch = json_string_value(json_object_get(meta, "default_branch"));
        readme(repos[i], branch ? branch : "main");
        image(json_string_value(json_object_get(json_object_get(meta, "owner"), "avatar_url")));
        json_decref(meta);
        // The catalog owns release refreshes using this same cache. Do not
        // duplicate them here or consume a second GitHub API request budget.
        snprintf(url, sizeof url, "https://api.github.com/repos/%s/commits?per_page=10", repos[i]);
        data = fetch(url, "application/vnd.github+json", 1024 * 1024);
        free(data);
        if (interrupted(NULL) && !__atomic_load_n(&stop, __ATOMIC_ACQUIRE))
            step--; // resume interrupted repository
    }
    if(!__atomic_load_n(&stop,__ATOMIC_ACQUIRE)&&repo_count){
        cursor=(cursor+limit)%repo_count;
        // Reuse the FAT-safe publication path: rename alone cannot replace the
        // previous cursor on a physical SD card.
        reader_cache_save(DATA_DIR "/github-prefetch.cursor",(uint64_t)time(NULL),
                          (const char*)&cursor,sizeof cursor,sizeof cursor);
    }
}
void github_prefetch_start(void) {
    if (worker)
        return;
    snprintf(repos[repo_count++],sizeof repos[0],"EstebanPdN/forge.store");
    for (int i = 0; i < BUILTIN_APPS && repo_count < 128; i++)
        if (APPS[i].repo && *APPS[i].repo) {
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
