#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {uint16_t slot;uint32_t token;} FileView;
// Main-thread handle API; paths are immutable copies, stat runs off-thread.
FileView file_view_watch(const char *path,bool permanent);
void file_view_plan_begin(void);
bool file_view_exists(FileView view);
bool file_view_nonempty(FileView view);
unsigned file_view_refresh(void);
unsigned file_view_completed(void);
void file_view_pump(float now);
void file_view_stop(void); // shutdown: ask the probe to end, without waiting
void file_view_exit(void);
