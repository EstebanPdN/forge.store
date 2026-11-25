#include "background.h"
#include "common.h"
#include "audio.h"

// mcu::HWC register 0x2D: the notification LED pattern (3dbrew "MCU Services").
typedef struct {
    u8 delay, smoothing, loop_delay, unknown;
    u8 red[32], green[32], blue[32];
} LedPattern;

static bool sleep_blocked, lid_closed, lcd_ok, lcd_off, led_on, led_ok = true;
static int poll_frames;

static void led_write(bool blink) {
    if (!led_ok) return;
    LedPattern p = {0};
    if (blink) {
        // Two short green pulses, a pause, and again: the system's own notification rhythm.
        p.delay = 0x20; p.smoothing = 0x40; p.loop_delay = 0x00;
        for (int i = 0; i < 32; i++) p.green[i] = (i < 4 || (i >= 8 && i < 12)) ? 0xff : 0;
    }
    if (R_FAILED(MCUHWC_WriteRegister(0x2d, &p, sizeof p))) led_ok = false;  // no mcu::HWC here
    led_on = blink && led_ok;
}

static void screens(bool on) {
    if (!lcd_ok) lcd_ok = R_SUCCEEDED(gspLcdInit());
    if (!lcd_ok || lcd_off == !on) return;
    if (R_SUCCEEDED(on ? GSPLCD_PowerOnAllBacklights() : GSPLCD_PowerOffAllBacklights())) lcd_off = !on;
}

void background_update(bool busy, bool input) {
    if (busy != sleep_blocked) {
        aptSetSleepAllowed(!busy);
        sleep_blocked = busy;
    }
    if (++poll_frames >= 10) {  // six times a second is enough for a lid
        poll_frames = 0;
        u8 shell = 1;
        bool closed = R_SUCCEEDED(PTMU_GetShellState(&shell)) && !shell;
        if (closed != lid_closed) {
            lid_closed = closed;
            if (!closed && led_on) led_write(false);  // opened: the player has seen it
        }
    }
    // Screens stay off while the lid is closed. If sleep is allowed again the system
    // sleeps on its own the next time the lid closes.
    screens(!(lid_closed && (busy || led_on)));
    music_lid(lid_closed);
    if (input && led_on) led_write(false);
}

void background_finished(void) {
    if (!led_on) led_write(true);
}

bool background_lid_closed(void) { return lid_closed && lcd_off; }

void background_exit(void) {
    if (led_on) led_write(false);
    if (lcd_off) screens(true);
    if (lcd_ok) gspLcdExit();
    lcd_ok = false;
    if (sleep_blocked) aptSetSleepAllowed(true);
    sleep_blocked = false;
}
