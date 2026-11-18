#pragma once
// Host stand-ins for detect.c: a minimal App, a worker that runs at once, and AM.
#include <jansson.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t s32;
typedef int Result;
typedef void *Thread;
#define R_SUCCEEDED(r) ((r) >= 0)
#define R_FAILED(r) ((r) < 0)
#define MEDIATYPE_SD 1
#define CUR_THREAD_HANDLE 0
typedef struct { u64 titleID; u64 size; u16 version; } AM_TitleInfo;
typedef struct {
    json_t *j;
    const char *id;
    bool dev, retired;
    u64 found_title;
    u16 found_version;
    char *found_path;
    char uu_updated[17];
} App;
#define MAX_HOST_APPS 16
static App APPS[MAX_HOST_APPS];
static int NAPPS;
static u64 host_titles[8];
static u16 host_versions[8];
static int host_ntitles;
static bool host_installing;
static Result AM_GetTitleCount(int m, u32 *n) { *n = (u32)host_ntitles; return 0; }
static Result AM_GetTitleList(u32 *got, int m, u32 cap, u64 *t) {
    *got = 0;
    for (int i = 0; i < host_ntitles && *got < cap; i++) t[(*got)++] = host_titles[i];
    return 0;
}
static Result AM_GetTitleInfo(int m, u32 n, u64 *t, AM_TitleInfo *info) {
    for (int i = 0; i < host_ntitles; i++)
        if (host_titles[i] == *t) { info->titleID = *t; info->version = host_versions[i]; return 0; }
    return -1;
}
static void *install_active(void) { return host_installing ? (void *)1 : NULL; }
static void svcGetThreadPriority(s32 *p, int h) { *p = 0x30; }
static void bg_wait(void) {}
static Thread threadCreate(void (*fn)(void *), void *arg, size_t stack, int prio, int core, bool detached) {
    fn(arg);  // the scan runs to completion before detect_pump looks again
    return (Thread)1;
}
static bool thread_done(Thread *t) { *t = NULL; return true; }
static bool thread_reap(Thread *t) { *t = NULL; return true; }
