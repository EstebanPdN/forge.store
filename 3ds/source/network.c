#include "network.h"
#include "reader_cache.h"
#include "http_cache.h"
#include <mbedtls/x509.h>
#include <time.h>
#include <sys/stat.h>
#include <mbedtls/sha256.h>
#include <strings.h>

// Do not disable date, signature, CA or hostname verification. The local build's
// timestamp is a lower bound for TLS only, useful for consoles left at 2011/2000.
// The system clock/display is never changed. Future clocks still fail closed.
#ifndef FORGE_BUILD_UNIX
#error "Build with 3ds/build.sh to supply the TLS build timestamp"
#endif
static int compare_date(const mbedtls_x509_time *t) {
    time_t now = time(NULL);
    if (now < FORGE_BUILD_UNIX)
        now = FORGE_BUILD_UNIX;
    struct tm tm;
    gmtime_r(&now, &tm);
    int a[] = {t->year, t->mon, t->day, t->hour, t->min, t->sec};
    int b[] = {tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec};
    for (int i = 0; i < 6; i++)
        if (a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    return 0;
}
int __wrap_mbedtls_x509_time_is_past(const mbedtls_x509_time *t) { return compare_date(t) < 0; }
int __wrap_mbedtls_x509_time_is_future(const mbedtls_x509_time *t) { return compare_date(t) > 0; }

void network_options(CURL *c) {
    curl_easy_setopt(c, CURLOPT_CAINFO, "romfs:/cacert.pem");
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "forge.store/1.1 (Nintendo 3DS)");
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 90L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
}
void network_error(char *out, size_t n, const char *e) {
    if (strstr(e, "FUTURE") || strstr(e, "EXPIRED") || strstr(e, "future") || strstr(e, "expired"))
        snprintf(out,n,"%s",T("native.tlsDate"));
    else
        snprintf(out, n, "%s", e);
}

#define READER_LIMIT (1024u * 1024u)
typedef struct {
    Thread thread;
    bool ready, cancel, limited;
    char path[256], error[160];
    char *body;
    size_t length, capacity;
    bool raw, refresh_stale;
    u32 ttl;
    u64 deadline;
    time_t reset;
} ReaderRequest;
static ReaderRequest requests[READER_SLOTS];
static bool request_cancelled(ReaderRequest *r) { return __atomic_load_n(&r->cancel, __ATOMIC_ACQUIRE); }
static bool reader_cancelled(void *ctx){ReaderRequest *r=ctx;return request_cancelled(r)||(r->deadline&&osGetTime()>=r->deadline);}
static void read_thread(void *ctx){
    ReaderRequest *r=ctx;char url[320];snprintf(url,sizeof url,"https://api.github.com/%s",r->path);
    const char *accept=r->raw?"application/vnd.github.raw+json":"application/vnd.github+json";
    bool refresh=r->refresh_stale&&!http_cache_fresh(url,accept,r->ttl);
    r->body=http_cache_get(url,accept,READER_LIMIT,refresh,reader_cancelled,r,&r->length,r->error,sizeof r->error);
    __atomic_store_n(&r->ready,true,__ATOMIC_RELEASE);
}
bool reader_slot_busy(int slot) { return slot >= 0 && slot < READER_SLOTS && requests[slot].thread; }
bool reader_request_slot_ttl(int slot, const char *path, bool raw,u32 ttl) {
    if (slot < 0 || slot >= READER_SLOTS || !path || strlen(path) >= sizeof requests[slot].path) return false;
    ReaderRequest *r = &requests[slot];
    if (r->thread || __atomic_load_n(&r->ready, __ATOMIC_ACQUIRE)) return false;
    free(r->body); memset(r, 0, sizeof *r);
    snprintf(r->path, sizeof r->path, "%s", path); r->raw = raw;r->ttl=ttl?ttl:3600;
    r->deadline=slot<4?osGetTime()+6000:0;
    r->refresh_stale=slot==4; // Catalog updates must still validate bundled releases.
    s32 p = 0x30; svcGetThreadPriority(&p, CUR_THREAD_HANDLE);
    r->thread = threadCreate(read_thread, r, 48 * 1024, p + 2, -2, false);
    if (!r->thread) { snprintf(r->error, sizeof r->error, "Out of memory"); __atomic_store_n(&r->ready, true, __ATOMIC_RELEASE); }
    return true;
}
bool reader_request_slot(int slot,const char *path,bool raw){return reader_request_slot_ttl(slot,path,raw,3600);}
char *reader_take_slot(int slot, char *error, size_t n) {
    if (slot < 0 || slot >= READER_SLOTS) return NULL;
    ReaderRequest *r = &requests[slot];
    if (!__atomic_load_n(&r->ready, __ATOMIC_ACQUIRE)) return NULL;
    if (!thread_done(&r->thread)) return NULL;
    if (error && n) snprintf(error, n, "%s", r->error);
    char *p = r->body; r->body = NULL; r->length = r->capacity = 0;
    __atomic_store_n(&r->ready, false, __ATOMIC_RELEASE);
    return p ? p : strdup("");
}
void reader_cancel_slot(int slot) {
    if (slot >= 0 && slot < READER_SLOTS) __atomic_store_n(&requests[slot].cancel, true, __ATOMIC_RELEASE);
}
bool reader_busy(void) { for (int i = 0; i < READER_SLOTS; i++) if (reader_slot_busy(i)) return true; return false; }
void reader_request(const char *path, bool raw) { reader_request_slot(0, path, raw); }
char *reader_take(char *error, size_t n) { return reader_take_slot(0, error, n); }
void reader_stop(void) { for (int i = 0; i < READER_SLOTS; i++) reader_cancel_slot(i); }
void reader_exit(void) {
    reader_stop();
    for (int i = 0; i < READER_SLOTS; i++) {
        ReaderRequest *r = &requests[i];
        if (!thread_reap(&r->thread)) continue;  // still connecting: it keeps its request
        free(r->body); memset(r, 0, sizeof *r);
    }
}

// Keep readable content from mixed Markdown/HTML READMEs. No markup executes.
char *reader_plaintext(const char *src) {
    size_t n = strlen(src), o = 0;
    char *out = malloc(n + 1);
    if (!out)
        return NULL;
    for (size_t i = 0; i < n;) {
        if (src[i] == '<') {
            const char *end = strchr(src + i, '>');
            if (end) {
                bool block = !strncmp(src + i, "<br", 3) || !strncmp(src + i, "</p", 3) || !strncmp(src + i, "</h", 3) ||
                             !strncmp(src + i, "</div", 5) || !strncmp(src + i, "</tr", 4) || !strncmp(src + i, "</li", 4);
                if (block && o && out[o - 1] != '\n')
                    out[o++] = '\n';
                i = end - src + 1;
                continue;
            }
        }
        bool image = src[i] == '!' && src[i + 1] == '[';
        size_t begin = i + (image ? 1 : 0);
        if (src[begin] == '[') {
            size_t close = begin + 1;
            int depth = 1;
            for (; close < n && depth; close++) {
                if (src[close] == '[')
                    depth++;
                if (src[close] == ']')
                    depth--;
            }
            if (!depth && close < n && src[close] == '(') {
                size_t end = close + 1;
                depth = 1;
                for (; end < n && depth; end++) {
                    if (src[end] == '(')
                        depth++;
                    if (src[end] == ')')
                        depth--;
                }
                if (!depth) {
                    // Badge links wrap an image; don't display their Markdown or URL.
                    if (!image && !(src[begin + 1] == '!' && src[begin + 2] == '[')) {
                        size_t len = close - begin - 2;
                        memcpy(out + o, src + begin + 1, len);
                        o += len;
                    }
                    i = end;
                    continue;
                }
            }
        }
        const char *entities[] = {"&amp;", "&lt;", "&gt;", "&quot;", "&nbsp;", "&#39;"};
        const char values[] = {'&', '<', '>', '"', ' ', '\''};
        bool entity = false;
        if (src[i] == '&')
            for (int k = 0; k < 6; k++) {
                size_t len = strlen(entities[k]);
                if (!strncmp(src + i, entities[k], len)) {
                    out[o++] = values[k];
                    i += len;
                    entity = true;
                    break;
                }
            }
        if (entity)
            continue;
        if (src[i] == '*' && src[i + 1] == '*') {
            i += 2;
            continue;
        }
        out[o++] = src[i++];
    }
    out[o] = 0;
    return out;
}
