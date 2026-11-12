#include "http_cache.h"
#include "network.h"
#include "reader_cache.h"
#include <mbedtls/sha256.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <strings.h>
#define CACHE_DIR DATA_DIR "/http-v1"
#define CACHE_FILES 192
static LightLock write_lock;
// JSON bodies and image payloads share validation, conditional requests and retries.
typedef struct {
    char *body;
    size_t length, limit;
    HttpCancelled cancel;
    void *context;
    char etag[256], modified[128];
} Request;
static bool stopped(Request *r) { return r->cancel && r->cancel(r->context); }
static size_t receive(char *p, size_t size, size_t count, void *ctx) {
    Request *r = ctx;
    bg_wait();
    if (size && count > SIZE_MAX / size)
        return 0;
    size_t n = size * count;
    if (stopped(r) || n > r->limit - r->length)
        return 0;
    char *b = realloc(r->body, r->length + n + 1);
    if (!b)
        return 0;
    r->body = b;
    memcpy(b + r->length, p, n);
    r->length += n;
    b[r->length] = 0;
    return n;
}
static size_t header(char *p, size_t size, size_t count, void *ctx) {
    if (size && count > SIZE_MAX / size)
        return 0;
    Request *r = ctx;
    size_t n = size * count;
    char *dest = NULL;
    size_t cap = 0, offset = 0;
    if (n > 5 && !strncasecmp(p, "ETag:", 5)) {
        dest = r->etag;
        cap = sizeof r->etag;
        offset = 5;
    }
    if (n > 14 && !strncasecmp(p, "Last-Modified:", 14)) {
        dest = r->modified;
        cap = sizeof r->modified;
        offset = 14;
    }
    if (dest) {
        while (offset < n && (p[offset] == ' ' || p[offset] == '\t'))
            offset++;
        size_t end = n;
        while (end > offset && (p[end - 1] == '\r' || p[end - 1] == '\n' || p[end - 1] == ' '))
            end--;
        size_t len = end - offset;
        if (len < cap) {
            memcpy(dest, p + offset, len);
            dest[len] = 0;
        }
    }
    return n;
}
static int progress(void *p, curl_off_t a, curl_off_t b, curl_off_t c, curl_off_t d) {
    bg_wait();
    return stopped(p);
}
void http_cache_path(const char *url, const char *accept, char *path, size_t cap) {
    mbedtls_sha256_context ctx;
    unsigned char hash[32];
    char hex[65];
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts_ret(&ctx, 0);
    mbedtls_sha256_update_ret(&ctx, (const unsigned char *)url, strlen(url));
    mbedtls_sha256_update_ret(&ctx, (const unsigned char *)accept, strlen(accept));
    mbedtls_sha256_finish_ret(&ctx, hash);
    mbedtls_sha256_free(&ctx);
    for (int i = 0; i < 32; i++)
        snprintf(hex + 2 * i, 3, "%02x", hash[i]);
    snprintf(path, cap, CACHE_DIR "/%s", hex);
}
void http_cache_invalidate(const char *url, const char *accept) {
    char path[256], meta[272];
    http_cache_path(url, accept, path, sizeof path);
    snprintf(meta, sizeof meta, "%s.meta", path);
    LightLock_Lock(&write_lock);
    remove(path);
    remove(meta);
    LightLock_Unlock(&write_lock);
}
static void bundled_path(const char *url,const char *accept,char *path,size_t cap) {
    char cache[256];http_cache_path(url,accept,cache,sizeof cache);
    snprintf(path,cap,"romfs:/reader-data/%s",strrchr(cache,'/')+1);
}
bool http_cache_bundled(const char *url,const char *accept) {
    char path[256];bundled_path(url,accept,path,sizeof path);
    struct stat st;return !stat(path,&st)&&S_ISREG(st.st_mode);
}
static bool validator(const char *s) {
    if (!s || strlen(s) > 255)
        return false;
    for (; *s; s++)
        if ((unsigned char)*s < 32 || *s == 127)
            return false;
    return true;
}
// Called on background writes only, never in a draw path. Cache headers supply time
// because the 3DS SD devoptab does not provide reliable st_mtime.
static void trim_cache(void) {
    DIR *d = opendir(CACHE_DIR);
    if (!d)
        return;
    struct dirent *e;
    unsigned count = 0;
    char oldest[256] = {0};
    uint64_t oldest_time = UINT64_MAX;
    while ((e = readdir(d))) {
        if (strlen(e->d_name) != 64)
            continue;
        count++;
        char path[256];
        snprintf(path, sizeof path, CACHE_DIR "/%.64s", e->d_name);
        FILE *f = fopen(path, "rb");
        unsigned char h[12];
        if (f) {
            if (fread(h, 1, 12, f) == 12) {
                uint64_t t = 0;
                for (int i = 11; i >= 4; i--)
                    t = t * 256 + h[i];
                if (t < oldest_time) {
                    oldest_time = t;
                    snprintf(oldest, sizeof oldest, "%s", path);
                }
            }
            fclose(f);
        }
    }
    closedir(d);
    if (count >= CACHE_FILES && oldest[0]) {
        remove(oldest);
        char meta[272];
        snprintf(meta, sizeof meta, "%s.meta", oldest);
        remove(meta);
    }
}
static bool transient(CURLcode rc, long code) {
    return rc == CURLE_GOT_NOTHING || rc == CURLE_RECV_ERROR || rc == CURLE_SEND_ERROR ||
           rc == CURLE_COULDNT_CONNECT || rc == CURLE_COULDNT_RESOLVE_HOST ||
           rc == CURLE_OPERATION_TIMEDOUT || rc == CURLE_PARTIAL_FILE || code == 502 || code == 503 ||
           code == 504;
}
void http_cache_init(void) { LightLock_Init(&write_lock); }
static void save_metadata(const char *meta, const char *etag, const char *modified, time_t checked) {
    json_t *m = json_pack("{s:s,s:s,s:I}", "etag", etag ? etag : "", "modified", modified ? modified : "",
                          "checked", (json_int_t)checked);
    char temp[284];
    snprintf(temp, sizeof temp, "%s.tmp", meta);
    if (m) {
        if (!json_dump_file(m, temp, JSON_COMPACT))
            rename(temp, meta);
        json_decref(m);
        remove(temp);
    }
}
bool http_cache_fresh(const char *url, const char *accept, uint32_t max_age) {
    char path[256], meta[272];
    http_cache_path(url, accept, path, sizeof path);
    snprintf(meta, sizeof meta, "%s.meta", path);
    // Writers publish with rename; readers never wait behind cache pruning.
    json_t *m = json_load_file(meta, 0, NULL);
    if(!m){
        char bundled[256];bundled_path(url,accept,bundled,sizeof bundled);
        FILE *f=fopen(bundled,"rb");unsigned char header[12];
        if(f){bool ok=fread(header,1,12,f)==12&&!memcmp(header,"FGH2",4);fclose(f);
            if(ok){uint64_t stamp=0;for(int i=11;i>=4;i--)stamp=(stamp<<8)|header[i];
                time_t now=time(NULL);if(now>0&&(uint64_t)now>=stamp&&(uint64_t)now-stamp<max_age)return true;}
        }
    }
    json_t *v = json_object_get(m, "checked");
    time_t now = time(NULL);
    json_int_t checked = json_integer_value(v);
    bool fresh = json_is_integer(v) && checked > 0 && now >= checked && (uint64_t)(now - checked) < max_age;
    json_decref(m);
    return fresh;
}
char *http_cache_get(const char *url, const char *accept, size_t limit, bool refresh, HttpCancelled cancel,
                     void *context, size_t *length, char *error, size_t cap) {
    bg_wait();
    if (length)
        *length = 0;
    if (error && cap)
        *error = 0;
    if (!url || strncmp(url, "https://", 8) || !accept || !limit)
        return NULL;
    char path[256], meta[272];
    http_cache_path(url, accept, path, sizeof path);
    snprintf(meta, sizeof meta, "%s.meta", path);
    time_t now = time(NULL);
    size_t cached_size = 0;
    char *cached = reader_cache_load(path, UINT64_MAX - 1, UINT64_MAX, limit, &cached_size);
    json_t *validators = cached && refresh ? json_load_file(meta, 0, NULL) : NULL;
    if(!cached) {
        char bundled[256];bundled_path(url,accept,bundled,sizeof bundled);
        cached=reader_cache_load(bundled,UINT64_MAX-1,UINT64_MAX,limit,&cached_size);
    }
    if (cached && !refresh) {
        if (length)
            *length = cached_size;
        return cached;
    }
    Request r = {.limit = limit, .cancel = cancel, .context = context};
    const char *etag = json_string_value(json_object_get(validators, "etag")),
               *modified = json_string_value(json_object_get(validators, "modified"));
    CURLcode rc = CURLE_FAILED_INIT;
    long code = 0;
    for (int attempt = 0; attempt < 2 && !stopped(&r); attempt++) {
        CURL *c = curl_easy_init();
        if (!c)
            break;
        free(r.body);
        r.body = NULL;
        r.length = 0;
        r.etag[0] = r.modified[0] = 0;
        network_options(c);
        curl_easy_setopt(c, CURLOPT_URL, url);
        curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 4L);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 12L);
        curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, receive);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &r);
        curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, header);
        curl_easy_setopt(c, CURLOPT_HEADERDATA, &r);
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, progress);
        curl_easy_setopt(c, CURLOPT_XFERINFODATA, &r);
        struct curl_slist *headers = NULL;
        char line[384];
        snprintf(line, sizeof line, "Accept: %s", accept);
        headers = curl_slist_append(headers, line);
        if (cached && validator(etag)) {
            snprintf(line, sizeof line, "If-None-Match: %s", etag);
            headers = curl_slist_append(headers, line);
        } else if (cached && validator(modified)) {
            snprintf(line, sizeof line, "If-Modified-Since: %s", modified);
            headers = curl_slist_append(headers, line);
        }
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
        rc = curl_easy_perform(c);
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
        curl_slist_free_all(headers);
        curl_easy_cleanup(c);
        if (!transient(rc, code) || attempt)
            break;
        for (int i = 0; i < 5 && !stopped(&r); i++)
            svcSleepThread(50000000);
    }
    if (!stopped(&r) && rc == CURLE_OK && code == 304 && cached) {
        LightLock_Lock(&write_lock);
        save_metadata(meta, etag, modified, now);
        LightLock_Unlock(&write_lock);
        json_decref(validators);
        free(r.body);
        if (length)
            *length = cached_size;
        return cached;
    }
    json_decref(validators);
    // API JSON must parse before it can replace a previously usable cache entry.
    bool valid = true;
    if (rc == CURLE_OK && code >= 200 && code < 300 && !strcmp(accept, "application/vnd.github+json")) {
        json_t *body = r.body ? json_loadb(r.body, r.length, JSON_REJECT_DUPLICATES, NULL) : NULL;
        valid = body != NULL;
        json_decref(body);
    }
    if (!stopped(&r) && rc == CURLE_OK && code >= 200 && code < 300 && r.body && r.length && valid) {
        LightLock_Lock(&write_lock);
        mkdir(CACHE_DIR, 0777);
        trim_cache();
        remove(meta);
        if (reader_cache_save(path, (uint64_t)now, r.body, r.length, limit)) {
            save_metadata(meta, r.etag, r.modified, now);
        }
        LightLock_Unlock(&write_lock);
        free(cached);
        if (length)
            *length = r.length;
        return r.body;
    }
    free(r.body);
    if (cached && !stopped(&r)) {
        if (length)
            *length = cached_size;
        return cached;
    }
    free(cached);
    if (error && cap)
        snprintf(error, cap,
                 code == 404                  ? "GitHub: 404"
                 : code == 403 || code == 429 ? "GitHub: retry later"
                 : stopped(&r)                ? "Cancelled"
                                              : "Connection interrupted. Retry.");
    return NULL;
}
