#pragma once
#include "common.h"
typedef enum {
    UPDATE_CHECKING,
    UPDATE_CURRENT,
    UPDATE_OFFLINE,
    UPDATE_DOWNLOADING,
    UPDATE_INSTALLING,
    UPDATE_DONE,
    UPDATE_FAILED,
    UPDATE_AVAILABLE,
    UPDATE_DECLINED
} StoreUpdateState;
// Main thread starts/polls; one worker owns transport and staged files.
void store_update_start(const char *executable);
StoreUpdateState store_update_state(void);
// Main-thread deadline also bounds checks when the platform resolver is blocked.
void store_update_poll(void);
float store_update_progress(void);
// Valid after acquiring UPDATE_AVAILABLE. Remains immutable until shutdown.
const char *store_update_version(void);
// Main thread: the worker cannot download/install before one explicit decision.
bool store_update_decide(bool accept);
void store_update_stop(void);
void store_update_exit(void);
// Called only after UPDATE_DONE and the exit fade, before normal teardown.
void store_update_restart(void);
