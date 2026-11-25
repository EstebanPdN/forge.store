// Real installer: downloads release assets from GitHub (libcurl + mbedTLS),
// verifies SHA-256, extracts archives (libarchive), installs CIAs (AM) and
// creates the folders each app needs on the SD card. Runs on its own thread.
#pragma once
#include "catalog.h"

typedef enum { JOB_QUEUED, JOB_RUNNING, JOB_DONE, JOB_FAILED, JOB_CANCELLED } JobState;
typedef enum { STEP_PENDING, STEP_ACTIVE, STEP_DONE, STEP_WARN, STEP_ERROR } StepState;

typedef enum { PHASE_CHECK, PHASE_STEPS, PHASE_PREPARE, PHASE_WRITE, PHASE_FINISH } InstallPhase;
typedef struct {
    json_t *raw;
    char label[112];
    char result[112];
    volatile StepState state;
    volatile float progress;
    int extracted;
    bool reuse;
    bool bytes;  // a download: progress is in bytes
} JobStep;

#define INSTALL_MAX_STEPS 64

typedef struct {
    App *app;
    bool cia, experimental, reported, uninstall;
    char tag[128];
    u64 serial;
    volatile bool commit_pending, commit_ack;
    bool commit_ok;
    json_t *release, *script;
    volatile JobState state;
    volatile InstallPhase phase;
    volatile int cia_percent; // -1 while the download total is unknown
    bool unknown_size;
    int last_download;
    volatile float progress, speed;
    volatile u64 done_bytes, total_bytes;
    JobStep steps[INSTALL_MAX_STEPS];
    float weight[INSTALL_MAX_STEPS], wtotal;  // share of the total time each step takes
    int nsteps;
    volatile int cur;
    volatile bool cancel;
    char error[128];
    int warnings;
    json_t *files, *dirs, *hashes, *record;
    // Identifies this installation in its recovery inventory and in the installed record:
    // after an interruption, a matching record means it was committed.
    char transaction[40];
    // CIA titles known to belong to this app (record and catalog), copied on the main thread.
    // Only these may be replaced without the package forge.store installed before.
    u64 known_titles[8];
    int nknown;
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
int job_percent(Job *j);
const char *job_activity(Job *j);
void tvars(char *out, size_t n, const char *tmpl, const char *k1, const char *v1, const char *k2, const char *v2);

int install_count(void);
Job *install_at(int i);

/** Current tracked CIA title if AM still finds it; zero otherwise. */
u64 install_title_id(App *a);
/** Removes only recorded unchanged files/title; preserves user edits and data. */
Job *install_uninstall(App *a);
