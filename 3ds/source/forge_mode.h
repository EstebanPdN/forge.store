#pragma once
#include <stdbool.h>
#include <jansson.h>

typedef enum { FORGE_LOCKED, FORGE_CHECKING, FORGE_ENABLED, FORGE_INVALID, FORGE_STORAGE_ERROR } ForgeModeState;
void forge_mode_init(void);
void forge_mode_pump(void);
void forge_mode_exit(void);
bool forge_mode_unlock(const char *code);
bool forge_mode_enabled(void);
ForgeModeState forge_mode_state(void);
// Main-thread owned snapshot. Never mutates the public catalog or logs URLs.
json_t *forge_mode_plan(const char *app_id, json_t *base);
