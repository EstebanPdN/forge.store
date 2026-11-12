#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MEDIA_MAX_ITEMS 128
// Paths are informational except in COMPLETED; use media_ready_path to play.
typedef enum {
    MEDIA_QUEUED, MEDIA_DOWNLOADING, MEDIA_VERIFYING, MEDIA_ERROR,
    MEDIA_COMPLETED, MEDIA_CLEARED
} MediaState;
typedef struct {
    char appid[49], id[49], path[256], error[160];
    MediaState state;
    unsigned received, total, attempts;
    float progress;
    unsigned retry_seconds; // ERROR with >0 automatically retries, bounded to 3 attempts
} MediaItem;

// Main-thread API. Start once AFTER Home is usable and network initialized.
// No hashing, file deletion, transfer or blocking join happens in these calls.
bool media_start(void);
void media_pump(void); // nonblocking worker reap; call once per frame
bool media_loading(void);
unsigned media_count(void);
bool media_item(unsigned index, MediaItem *out); // synchronized copy, never borrowed data
int media_find(const char *app, const char *id);
bool media_prioritize(const char *app, const char *id); // next queued item, no active abort
bool media_retry(const char *app, const char *id); // explicit retry ERROR/CLEARED only
void media_retry_all(void); // explicitly resume all after cleaning or errors
void media_set_paused(bool paused); // installer priority; abort/requeue current transfer
bool media_paused(void);
bool media_ready_path(const char *app, const char *id, char *out, size_t size);
bool media_path_ready(const char *path); // in-memory verified-complete guard for decoder
bool media_take_ready(void); // coalesced flag: rescan ready items (no callback from worker)
const char *media_queue_error(void); // startup/manifest/clear error (main-thread copy)
bool media_clear(void); // ACCEPTS async clear, invalidates ready state immediately
int media_clear_poll(void); // 0 pending/no request, 1 success, -1 failure; stable until next clear
bool media_clearing(void);
void media_shutdown(void); // asynchronous stop, call before final cleanup
void media_exit(void); // shutdown-only join; NEVER call while animating/navigating

// Legacy watcher API: watches one queue item; cancel only detaches this watcher.
// Request does NOT explicitly retry errors or restart a cleaned cache.
bool media_request(const char *app, const char *id);
bool media_busy(void); // watcher pending, NOT background queue busy
int media_poll(void); // 0 pending/no watch, 1 ready, -1 failed, -2 detached/cleared
void media_cancel(void);
const char *media_path(void); // nonempty only after successful poll, still check ready state
const char *media_error(void);
float media_progress(void);
unsigned media_received(void);
unsigned media_total(void);
bool media_verifying(void);
