// Store screens: intro and theme picker, store grid, app page, install progress,
// settings. Each draws both screens and reacts to buttons and the stylus.
#pragma once
#include "common.h"

extern float EYE;  // stereo offset scale for the eye being drawn (0 = mono)

/** Loads the backgrounds; the intro begins `intro_delay` seconds later. */
void scenes_init(float intro_delay);
void scenes_update(void);
void scenes_exit(void);
void scenes_draw_top(void);
void scenes_draw_bottom(void);
const char *scenes_section(void);
bool scenes_statusbar(void);
void theme_apply(void);

/** True once the fade that follows a validated launch has finished; main then cleans up. */
bool scenes_wants_exit(void);
/** Black over both screens while the store leaves for a launched application (0..1). */
float scenes_exit_fade(void);
/** An installation or the store's resource package is downloading (status bar icon). */
bool scenes_downloading(void);
/** Shutdown: ask the scenes' workers to stop (no waiting), before scenes_exit collects them. */
void scenes_stop(void);

/** Opt-in test.txt harness only; deterministic view selection and capture metadata. */
void scenes_test_view(const char *name);
void scenes_test_log(FILE *file);
