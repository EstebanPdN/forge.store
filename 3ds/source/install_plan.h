#pragma once
#include <jansson.h>
#include <stdbool.h>
#define PLAN_MAX 256
#define PLAN_PATH 1024
// Owned preview data; no filesystem writes and no borrowed strings.
typedef struct {char path[PLAN_PATH];bool dir,dsp,exists;int requirement,depth;double size;} PlanEntry;
typedef struct {PlanEntry rows[PLAN_MAX];int count;bool cia,truncated;} InstallPlan;
void install_plan_build(InstallPlan *p,json_t *steps,json_t *assets,json_t *requires,bool old3ds);
