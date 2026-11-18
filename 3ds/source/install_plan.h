#pragma once
#include <jansson.h>
#include <stdbool.h>
#define PLAN_MAX 256
#define PLAN_PATH 1024
// Owned preview data; no filesystem writes and no borrowed strings.
typedef struct {char path[PLAN_PATH];bool dir,dsp,exists;int requirement,depth;double size;} PlanEntry;
typedef struct {PlanEntry rows[PLAN_MAX];int count;bool cia,truncated;} InstallPlan;
// found(requirement): where the store found that file ("sdmc:/roms/n64/pm.z64"), or NULL: the
// tree then shows it there instead of the catalog's example location. `found` may be NULL.
void install_plan_build(InstallPlan *p,json_t *steps,json_t *assets,json_t *requires,bool old3ds,
                        const char *(*found)(json_t *requirement));
