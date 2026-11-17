#pragma once
#include <stdbool.h>
// Adopts newer releases and install plans from the repository catalog, which a GitHub
// Action keeps synchronized with every listed repository. On launch and every 30 min.
bool catalog_remote_pump(bool may_apply);
void catalog_remote_now(void);   // check again as soon as possible (e.g. after a 404)
void catalog_remote_stop(void);  // shutdown: request only
void catalog_remote_exit(void);
