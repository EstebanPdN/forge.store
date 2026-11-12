// Host-only platform/transport shim. Production media.c is included by the test.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <jansson.h>
#include <CommonCrypto/CommonDigest.h>
#include <stdarg.h>
#include <assert.h>
#define DATA_DIR test_dir
// media.c concatenates literal DATA_DIR; fixtures chdir into a unique temp dir.
#undef DATA_DIR
#define DATA_DIR "."
#define MEDIA_MANIFEST "media.json"
#define MEDIA_RETRY_MS 20u
#define U64_MAX UINT64_MAX
#define CUR_THREAD_HANDLE 0
#define R_SUCCEEDED(r) ((r) == 0)
#define SYSTEM_MEDIATYPE_SD 0
typedef int32_t s32;
typedef pthread_mutex_t LightLock;
static void LightLock_Init(LightLock *l) { assert(!pthread_mutex_init(l, NULL)); }
static void LightLock_Lock(LightLock *l) { assert(!pthread_mutex_lock(l)); }
static void LightLock_Unlock(LightLock *l) { assert(!pthread_mutex_unlock(l)); }
static void svcGetThreadPriority(s32 *p, int unused) { *p = 0x30; }
static void svcSleepThread(uint64_t ns) { struct timespec t = {ns / 1000000000, ns % 1000000000}; nanosleep(&t, NULL); }
static uint64_t osGetTime(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000; }
typedef struct HostThread { pthread_t id; void (*fn)(void *); void *arg; bool done; } *Thread;
static void *host_thread(void *p) { Thread t = p; t->fn(t->arg); __atomic_store_n(&t->done, true, __ATOMIC_RELEASE); return NULL; }
static Thread threadCreate(void (*fn)(void *), void *arg, size_t stack, int priority, int core, bool detached) {
    Thread t = calloc(1, sizeof *t); assert(t); t->fn = fn; t->arg = arg;
    assert(!pthread_create(&t->id, NULL, host_thread, t)); return t;
}
static int threadJoin(Thread t, uint64_t timeout) {
    if (!timeout && !__atomic_load_n(&t->done, __ATOMIC_ACQUIRE)) return 1;
    return pthread_join(t->id, NULL);
}
static void threadFree(Thread t) { free(t); }
typedef struct { uint64_t freeClusters, clusterSize; } FS_ArchiveResource;
static bool test_full;
static int FSUSER_GetArchiveResource(FS_ArchiveResource *r, int unused) { r->freeClusters = test_full ? 0 : UINT32_MAX; r->clusterSize = 4096; return 0; }
typedef CC_SHA256_CTX mbedtls_sha256_context;
static void mbedtls_sha256_init(mbedtls_sha256_context *c) { memset(c, 0, sizeof *c); }
static void mbedtls_sha256_free(mbedtls_sha256_context *c) {}
static int mbedtls_sha256_starts_ret(mbedtls_sha256_context *c, int mode) { return !CC_SHA256_Init(c); }
static int mbedtls_sha256_update_ret(mbedtls_sha256_context *c, const void *buf, size_t n) { return !CC_SHA256_Update(c, buf, (CC_LONG)n); }
static int mbedtls_sha256_finish_ret(mbedtls_sha256_context *c, unsigned char *digest) { return !CC_SHA256_Final(digest, c); }

typedef int64_t curl_off_t;
typedef enum { CURLE_OK, CURLE_COULDNT_RESOLVE_HOST, CURLE_COULDNT_RESOLVE_PROXY, CURLE_COULDNT_CONNECT,
    CURLE_OPERATION_TIMEDOUT, CURLE_RECV_ERROR, CURLE_SEND_ERROR, CURLE_PARTIAL_FILE, CURLE_GOT_NOTHING,
    CURLE_WRITE_ERROR, CURLE_ABORTED_BY_CALLBACK, CURLE_HTTP_RETURNED_ERROR } CURLcode;
enum { CURLOPT_URL, CURLOPT_NOSIGNAL, CURLOPT_TIMEOUT, CURLOPT_LOW_SPEED_LIMIT, CURLOPT_LOW_SPEED_TIME,
    CURLOPT_WRITEFUNCTION, CURLOPT_WRITEDATA, CURLOPT_ERRORBUFFER, CURLOPT_NOPROGRESS, CURLOPT_XFERINFOFUNCTION, CURLINFO_RESPONSE_CODE };
#define CURL_ERROR_SIZE 256
typedef struct {
    const char *url;
    size_t (*write)(char *, size_t, size_t, void *);
    int (*progress)(void *, curl_off_t, curl_off_t, curl_off_t, curl_off_t);
    void *data;
    long code;
} CURL;
static unsigned char payload[65536];
static unsigned calls, slow_started;
static bool hold_transfer, bad_payload;
static int fail_mode; // 1 transient offline; 2 HTTP 404
static char call_order[32][384];
static CURL *curl_easy_init(void) { return calloc(1, sizeof(CURL)); }
static void curl_easy_cleanup(CURL *c) { free(c); }
static void network_options(CURL *c) {}
static void curl_easy_setopt(CURL *c, int option, ...) {
    va_list ap; va_start(ap, option);
    switch (option) {
    case CURLOPT_URL: c->url = va_arg(ap, const char *); break;
    case CURLOPT_WRITEFUNCTION: c->write = va_arg(ap, size_t (*)(char *, size_t, size_t, void *)); break;
    case CURLOPT_WRITEDATA: c->data = va_arg(ap, void *); break;
    case CURLOPT_XFERINFOFUNCTION: c->progress = va_arg(ap, int (*)(void *, curl_off_t, curl_off_t, curl_off_t, curl_off_t)); break;
    default: break;
    }
    va_end(ap);
}
static CURLcode curl_easy_perform(CURL *c) {
    unsigned n = __atomic_fetch_add(&calls, 1, __ATOMIC_ACQ_REL);
    if (n < 32) snprintf(call_order[n], sizeof call_order[n], "%s", c->url);
    c->code = fail_mode == 2 ? 404 : fail_mode == 1 ? 0 : 200;
    if (fail_mode) return fail_mode == 1 ? CURLE_COULDNT_CONNECT : CURLE_HTTP_RETURNED_ERROR;
    for (unsigned offset = 0; offset < sizeof payload; offset += 1024) {
        if (c->progress(NULL, 0, 0, 0, 0)) return CURLE_ABORTED_BY_CALLBACK;
        char chunk[1024]; memcpy(chunk, payload + offset, sizeof chunk);
        if (bad_payload) chunk[0] ^= 1;
        if (c->write(chunk, 1, sizeof chunk, c->data) != sizeof chunk) return CURLE_WRITE_ERROR;
        __atomic_store_n(&slow_started, 1, __ATOMIC_RELEASE);
        while (__atomic_load_n(&hold_transfer, __ATOMIC_ACQUIRE)) {
            if (c->progress(NULL, 0, 0, 0, 0)) return CURLE_ABORTED_BY_CALLBACK;
            svcSleepThread(1000000);
        }
    }
    return CURLE_OK;
}
static void curl_easy_getinfo(CURL *c, int ignored, long *code) { *code = c->code; }
static const char *curl_easy_strerror(CURLcode c) { return c == CURLE_OK ? "OK" : "Injected transport failure"; }
