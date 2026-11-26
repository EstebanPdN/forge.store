// Production background.c: the HOME Menu must never find gsp::Lcd taken by the store.
// 1.4.2 and the first 1.4.3 build opened it on the first frame and never closed it; HOME
// then froze an Old 3DS until it was powered off.
#define BACKGROUND_HOST_TEST 1
#include "../source/background.c"
#include <stdio.h>

static void frames(int n, bool busy) { for (int i = 0; i < n; i++) background_update(busy, false); }

int main(void) {
    frames(600, false);
    assert(lcd_sessions == 0 && !sleep_rejected);
    assert(ndm_sessions == 0);
    frames(600, true);
    assert(lcd_sessions == 0 && sleep_rejected && ndm_sessions == 1 && ndm_exclusive && ndm_locks);
    frames(1, false);
    assert(ndm_sessions == 0 && !ndm_exclusive && !ndm_locks && !sleep_rejected);
    frames(1, true);
    puts("PASS lid open, idle or downloading: no gsp::Lcd session; ndm:u only while downloading");

    shell_open = 0; frames(20, true);
    assert(lcd_sessions == 1 && backlights_off && lid_music && background_lid_closed());
    shell_open = 1; frames(20, true);
    assert(lcd_sessions == 0 && !backlights_off && !background_lid_closed());
    puts("PASS lid closed while downloading: screens off, reopened: on and session closed");

    shell_open = 0; frames(20, false);
    assert(lcd_sessions == 0 && !backlights_off);
    puts("PASS lid closed with nothing to do: screens left to the system");

    frames(20, true); assert(lcd_sessions == 1);
    background_exit();
    assert(lcd_sessions == 0 && ndm_sessions == 0 && !backlights_off && !sleep_rejected);
    puts("PASS exit with the lid closed: screens on, session closed, sleep allowed");
    return 0;
}
