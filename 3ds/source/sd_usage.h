#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <jansson.h>
typedef struct {uint64_t store,user;bool complete;} SdUsage;
// Background IO only. Bounded traversal, no symlink following, unique files.
SdUsage sd_usage_measure(const char *data_dir,json_t *installed,json_t *user_paths);
uint64_t sd_usage_path(const char *path,unsigned limit,bool *complete);
// Any thread. While set, running and new measurements stop early and report incomplete.
void sd_usage_cancel(bool cancel);
