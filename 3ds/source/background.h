// Downloads with the lid closed, like other 3DS stores: while the installer has work the
// console does not go to sleep; closing the lid only turns off the screens and the music.
// When the last download finishes, the notification LED blinks green until a button,
// the touch screen or the lid is used.
#pragma once
#include <stdbool.h>

/** Main thread, once per frame. `busy`: an installation is queued or running. */
void background_update(bool busy, bool input);
/** The download queue just emptied after at least one successful installation. */
void background_finished(void);
/** Screens back on, sleep allowed again, LED off. */
void background_exit(void);
/** The lid is closed while downloads keep running (the frame need not be drawn). */
bool background_lid_closed(void);
