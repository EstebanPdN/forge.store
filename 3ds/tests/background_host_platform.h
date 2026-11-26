// Host stand-ins for the services background.c uses. Each counts open sessions so the test
// can check what the HOME Menu would find taken (gsp::Lcd allows very few sessions).
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t u8;
typedef int32_t Result;
#define R_SUCCEEDED(r) ((r) >= 0)
#define R_FAILED(r) ((r) < 0)

static int lcd_sessions, backlights_off, sleep_rejected;
static u8 shell_open = 1;
static bool lid_music;

static Result gspLcdInit(void) { assert(lcd_sessions == 0); lcd_sessions++; return 0; }
static void gspLcdExit(void) { assert(lcd_sessions == 1); lcd_sessions--; }
static Result GSPLCD_PowerOffAllBacklights(void) { assert(lcd_sessions == 1); backlights_off = 1; return 0; }
static Result GSPLCD_PowerOnAllBacklights(void) { assert(lcd_sessions == 1); backlights_off = 0; return 0; }
static void aptSetSleepAllowed(bool allowed) { sleep_rejected = !allowed; }
static Result PTMU_GetShellState(u8 *out) { *out = shell_open; return 0; }
static Result MCUHWC_WriteRegister(u8 reg, const void *data, uint32_t size) { return 0; }
static void music_lid(bool closed) { lid_music = closed; }

static int ndm_sessions, ndm_exclusive, ndm_locks;
#define NDM_EXCLUSIVE_STATE_INFRASTRUCTURE 1
static Result ndmuInit(void) { assert(ndm_sessions == 0); ndm_sessions++; return 0; }
static void ndmuExit(void) { assert(ndm_sessions == 1); ndm_sessions--; }
static Result NDMU_EnterExclusiveState(int state) { assert(ndm_sessions == 1); ndm_exclusive = 1; return 0; }
static Result NDMU_LeaveExclusiveState(void) { assert(ndm_sessions == 1); ndm_exclusive = 0; return 0; }
static Result NDMU_LockState(void) { assert(ndm_sessions == 1); ndm_locks = 1; return 0; }
static Result NDMU_UnlockState(void) { assert(ndm_sessions == 1); ndm_locks = 0; return 0; }
