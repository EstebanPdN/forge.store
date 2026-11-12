#pragma once
#include <stdbool.h>
// Main thread copies a bounded set of public curated repos; worker never reads catalog state.
void github_prefetch_start(void);
void github_prefetch_pause(bool paused);
void github_prefetch_stop(void);
void github_prefetch_exit(void);
