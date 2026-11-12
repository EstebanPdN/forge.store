// Real installer: downloads release assets from GitHub (libcurl + mbedTLS),
// verifies SHA-256, extracts archives (libarchive), installs CIAs (AM) and
// creates the folders each app needs on the SD card. Runs on its own thread.
#pragma once
#include "catalog.h"

typedef enum { JOB_QUEUED, JOB_RUNNING, JOB_DONE, JOB_FAILED, JOB_CANCELLED } JobState;
typedef enum { STEP_PENDING, STEP_ACTIVE, STEP_DONE, STEP_WARN, STEP_ERROR } StepState;

typedef struct {
    json_t *raw;
    char label[112];
    char result[112];
    volatile StepState state;
    volatile float progress;
    bool bytes;  // a download: progress is in bytes
} JobStep;

typedef struct {
    App *app;
    bool cia, experimental, reported, uninstall;
    char tag[128];
    u64 serial;
    volatile bool commit_pending, commit_ack;
    bool commit_ok;
    json_t *release, *script;
    volatile JobState state;
    volatile float progress, speed;
    volatile u64 done_bytes, total_bytes;
    JobStep steps[28];
    float weight[28], wtotal;  // share of the total time each step takes
    int nsteps;
    volatile int cur;
    volatile bool cancel;
    char error[128];
    int warnings;
    json_t *files, *dirs, *hashes, *record;
} Job;

void install_init(void);
void install_stop(void); // shutdown: cancel and wake the worker, without waiting
void install_exit(void);
Job *install_start(App *a, bool cia, bool experimental);
Job *install_job(App *a);
Job *install_active(void);
bool install_busy(App *a);
void install_cancel(Job *j);
/** Called by the main thread once per frame: finishes jobs (sounds, records). */
void install_poll(void (*on_done)(Job *));
/** Overall progress 0..1, including the step in progress. */
float job_progress(Job *j);
void tvars(char *out, size_t n, const char *tmpl, const char *k1, const char *v1, const char *k2, const char *v2);

int install_count(void);
Job *install_at(int i);

/** Current tracked CIA title if AM still finds it; zero otherwise. */
u64 install_title_id(App *a);
/** Removes only recorded unchanged files/title; preserves user edits and data. */
Job *install_uninstall(App *a);
