#pragma once
#include <stdbool.h>
#include <jansson.h>
// Main-thread merge; active jobs keep their own release/script snapshots.
bool live_release_merge(json_t *app,json_t *github_releases);
