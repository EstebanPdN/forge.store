// forge.store for Nintendo 3DS — shared types and state.
#pragma once
#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include <jansson.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOP_W 400
#define BOT_W 320
#define SCR_H 240
#define DATA_DIR "sdmc:/3ds/forge-store"
#define MAX_APPS 512

static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float ease_out(float t) { t = clampf(t, 0, 1); return 1 - powf(1 - t, 3); }
static inline float ease_in(float t) { t = clampf(t, 0, 1); return t * t * t; }
static inline float ease_inout(float t) { t = clampf(t, 0, 1); return t < .5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) / 2; }
// Small overshoot, like cubic-bezier(0.34, 1.3, 0.64, 1).
static inline float ease_back(float t) { t = clampf(t, 0, 1); const float c = 1.4f; return 1 + (c + 1) * powf(t - 1, 3) + c * powf(t - 1, 2); }
/** Progress of an animation that starts at `start` and lasts `dur` seconds. */
static inline float span(float now, float start, float dur) { return clampf((now - start) / dur, 0, 1); }

// ---- colours (ABGR as citro2d wants, built from 0xRRGGBB) -----------------
static inline u32 rgb(u32 hex, float a) {
    return C2D_Color32((hex >> 16) & 255, (hex >> 8) & 255, hex & 255, (u8)(clampf(a, 0, 1) * 255));
}

typedef struct {
    u32 bg, ink, ink_soft, ink_faint, surface, surface2, surface3, glass, line, eyebrow, title, mint, cursor_glow;
    float glass_a, line_a;
    bool dark;
} Theme;
extern Theme TH;
#define FORGE 0xff8a1f
#define FORGE2 0xff5a2c
#define MINT 0x4f9e86
#define LAV 0x7c6fd6
#define AMBER 0xe7a008
#define DANGER 0xe5484d
#define SKY 0x2f8fe0

// ---- settings ------------------------------------------------------------------
typedef struct {
    bool dark, music, sfx, onboarded, cia, experimental, universal;
    int lang;  // index into LANGS
    float volume;
} Settings;
extern Settings S;
extern const char *LANGS[8];
extern const char *LANG_NAMES[8];
void settings_load(void);
void settings_save(void);
/** Waits for settings/installed files still being written (call before exiting). */
void saves_flush(void);

// ---- input --------------------------------------------------------------------
typedef struct {
    u32 down, held, repeat;
    bool touching, touch_down, touch_up, dragged;
    int tx, ty, start_x, start_y, last_x, last_y;
    float touch_t0;
} Input;
extern Input IN;

// ---- time ----------------------------------------------------------------------
extern float NOW, DT;
extern bool IS_NEW3DS;

// ---- shutdown --------------------------------------------------------------------
// Closing from the HOME Menu must finish quickly: workers get a shared deadline and one
// that is still busy (SD scan, DNS, TLS) is abandoned instead of joined forever. Its
// module then frees nothing the worker may still touch; process exit reclaims it.
#ifdef __3DS__  // host tests build these sources against a stub SDK without threads
extern u64 EXIT_DEADLINE_MS;  // osGetTime() value after which joins stop waiting
extern bool EXIT_ABANDONED;   // a worker was left running: skip shared-service teardown
/** Final-teardown join. True when the thread ended and was freed (or never existed). */
static inline bool thread_reap(Thread *t) {
    if (!*t) return true;
    u64 now = osGetTime();
    u64 wait = EXIT_DEADLINE_MS > now ? (EXIT_DEADLINE_MS - now) * 1000000ULL : 0;
    // svcWaitSynchronization reports a timeout as a positive, "successful" result.
    if (threadJoin(*t, wait) != 0) { EXIT_ABANDONED = true; return false; }
    threadFree(*t);
    *t = NULL;
    return true;
}
/** Runtime reap after the worker published its last result: never waits. */
static inline bool thread_done(Thread *t) {
    if (!*t) return true;
    if (threadJoin(*t, 0) != 0) return false;
    threadFree(*t);
    *t = NULL;
    return true;
}

// ---- system transitions ---------------------------------------------------------------
// While the HOME Menu or sleep takes over, the workers leave the SD card and the network
// alone: their requests keep the system modules busy and the HOME Menu then needs many
// seconds to open. Set and cleared by the main thread only.
extern volatile bool BG_HOLD;
/** Call between two pieces of background IO. */
static inline void bg_wait(void) {
    while (BG_HOLD) svcSleepThread(10000000);
}
#endif

// ---- strings -------------------------------------------------------------------
void strings_load(void);
const char *T(const char *key);
/** Localized value of a catalog field: string or {es, en, ...}. */
const char *LJ(json_t *v);

// ---- toasts --------------------------------------------------------------------
void toast(const char *text, u32 color);
void toasts_draw(void);
