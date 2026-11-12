#pragma once
#include <stdbool.h>
// Once per launch, after Home arrives. Fetch/parse/convert stays off the UI thread.
// Applying is deferred until a safe scene with no active installer.
bool universal_sync_pump(bool may_apply);
void universal_sync_stop(void); // shutdown: request only
void universal_sync_exit(void);
const char *universal_sync_status(void);
