// Host-only platform shim for the production assets.c (compiled as its own translation unit).
// Fixtures chdir into a temporary folder that holds "romfs:/..." and "packs/" directories.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <jansson.h>
#define DATA_DIR "."
typedef pthread_mutex_t LightLock;
static inline void LightLock_Init(LightLock *l) { pthread_mutex_init(l, NULL); }
static inline void LightLock_Lock(LightLock *l) { pthread_mutex_lock(l); }
static inline void LightLock_Unlock(LightLock *l) { pthread_mutex_unlock(l); }
static inline void svcSleepThread(uint64_t ns) { struct timespec t = {ns / 1000000000, ns % 1000000000}; nanosleep(&t, NULL); }
