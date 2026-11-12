// Test builds only (FORGE_DEBUG): SELECT saves what the console is really showing and
// holding in memory, a watchdog records main-loop stalls and shutdown leaves a trace.
#pragma once
#include "common.h"

#ifndef FORGE_DEBUG
#define FORGE_DEBUG 0
#endif

#if FORGE_DEBUG
/** Copies the framebuffers on screen right now and opens info.txt for the caller's notes.
 *  NULL when a dump is still being written or the SD refused the folder. */
FILE *debug_dump_begin(bool full);
/** Closes info.txt and writes the screens, VRAM and RAM images on a worker. */
void debug_dump_commit(FILE *info);
/** One message when the running dump finishes; NULL otherwise. */
const char *debug_dump_poll(bool *ok);
/** Main-loop heartbeat: the watchdog logs to stall.txt when it stops for over 1.5 s. */
void debug_beat(const char *stage);
void debug_watch_start(void);
/** Appends one line to exit-trace.txt (the file is restarted with a NULL step). */
void debug_exit_trace(const char *step);
void debug_dump_exit(void);
/** One line in stall.txt with the current time (HOME Menu, sleep). */
void debug_note(const char *what);
#else
static inline void debug_beat(const char *stage) { (void)stage; }
static inline void debug_exit_trace(const char *step) { (void)step; }
static inline void debug_note(const char *what) { (void)what; }
#endif
