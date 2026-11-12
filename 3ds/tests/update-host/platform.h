#pragma once
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <jansson.h>
#include <curl/curl.h>
#include <CommonCrypto/CommonDigest.h>
typedef uint64_t u64;
typedef uint32_t u32;
typedef int32_t s32;
typedef int Result;
typedef int Handle;
typedef void *Thread;
typedef pthread_mutex_t LightLock;
#define DATA_DIR "sdmc:/3ds/forge-store"
#define R_SUCCEEDED(r) ((r) >= 0)
#define R_FAILED(r) ((r) < 0)
#define CUR_THREAD_HANDLE 0
#define MEDIATYPE_SD 1
#define ARCHIVE_SDMC 1
#define PATH_EMPTY 0
#define PATH_ASCII 1
#define FS_OPEN_READ 1
typedef struct {
    uint64_t titleID;
} AM_TitleInfo;
static inline void LightLock_Init(LightLock *l) { pthread_mutex_init(l, NULL); }
static inline void LightLock_Lock(LightLock *l) { pthread_mutex_lock(l); }
static inline void LightLock_Unlock(LightLock *l) { pthread_mutex_unlock(l); }
static inline void svcSleepThread(uint64_t ns) { usleep(ns / 1000); }
static inline void bg_wait(void) {}
static inline u64 osGetTime(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static inline void svcGetThreadPriority(s32 *p, int h) { *p = 48; }
static inline Thread threadCreate(void (*f)(void *), void *a, size_t n, int p, int c, bool d) { return NULL; }
static inline void thread_reap(Thread *t) {}
static void network_options(CURL *c) {}
static CURLcode local_perform(CURL *c) {
    char *url = NULL;
    curl_easy_getinfo(c, CURLINFO_EFFECTIVE_URL, &url);
    const char *path = strstr(url, "://");
    path = path ? strchr(path + 3, '/') : NULL;
    char local[2048];
    snprintf(local, sizeof local, "http://127.0.0.1:%s%s", getenv("TEST_PORT"), path ? path : "/");
    curl_easy_setopt(c, CURLOPT_URL, local);
    return curl_easy_perform(c);
}
#define curl_easy_perform local_perform
static bool hb;
static int starts, writes, finishes, cancels;
static int fault;
static FILE *input;
static bool envIsHomebrew(void) { return hb; }
static const char *fsMakePath(int kind, const char *p) { return p; }
static Result FSUSER_OpenFileDirectly(Handle *h, int a, const char *b, const char *p, int m, int x) {
    char path[1200];
    snprintf(path, sizeof path, "sdmc:%s", p);
    input = fopen(path, "rb");
    *h = 1;
    return input ? 0 : -1;
}
static Result AM_GetCiaFileInfo(int media, AM_TitleInfo *info, Handle h) {
    return fread(&info->titleID, 1, 8, input) == 8 ? 0 : -1;
}
static void FSFILE_Close(Handle h) {
    fclose(input);
    input = NULL;
}
static Result APT_GetProgramID(u64 *id) {
    *id = fault == 3 ? 0 : 0x000400000F0F5E00ULL;
    return 0;
}
static Result AM_StartCiaInstallOverwrite(Handle *h, int m) {
    starts++;
    *h = 7;
    return 0;
}
static Result FSFILE_Write(Handle h, u32 *n, u64 offset, const void *data, size_t size, int flags) {
    writes++;
    *n = fault == 1 ? size - 1 : size;
    return 0;
}
static Result AM_FinishCiaInstall(Handle h) {
    finishes++;
    return fault == 2 ? -1 : 0;
}
static void AM_CancelCIAInstall(Handle h) { cancels++; }
static void aptSetChainloader(u64 id, int media) {}
static void hbl_launch(const char *p) {}
