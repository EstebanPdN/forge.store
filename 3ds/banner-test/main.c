#include <3ds.h>
#include <stdio.h>
#ifndef FORGE_BANNER_VERSION
#define FORGE_BANNER_VERSION "19"
#endif
int main(void) {
    gfxInitDefault();
    consoleInit(GFX_BOTTOM, NULL);
    printf("Forge Banner Test %s\n\n", FORGE_BANNER_VERSION);
    printf("HOME banner diagnostic.\n\n");
    printf("Press START to return to HOME.\n");
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        gspWaitForVBlank();
    }
    gfxExit();
    return 0;
}
