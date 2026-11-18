#pragma once
#include <stdbool.h>
// Recognizes catalog apps already on the console that forge.store did not install
// (FBI, Universal-Updater, copied by hand): their CIA title (AM), their .3dsx and other
// listed files, and Universal-Updater's own record of what it installed and when.
// One bounded scan on a worker; results are applied to APPS on the main thread.

/** Main thread: scan again soon (startup, catalog change, finished installation). */
void detect_request(void);
/** Main thread, once per frame while background work may run. True when results changed. */
bool detect_pump(void);
/** Main thread: an app was just uninstalled; forget what the last scan found for it. */
void detect_forget(const char *app_id);
void detect_stop(void);  // shutdown: request only
void detect_exit(void);
const char *detect_status(void);  // test log only
