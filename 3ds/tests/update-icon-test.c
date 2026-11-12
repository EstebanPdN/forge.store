#include "update_icon.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static int frames;
static void pixels_at(const UpdateIcon *icon, float now, uint8_t pixels[40][40]) {
    UpdateIconFrame frame;
    update_icon_frame(icon, now, &frame);
    update_icon_raster(&frame, pixels);
    int ink = 0;
    for (int y = 0; y < 40; y++) for (int x = 0; x < 40; x++) {
        assert(pixels[y][x] <= 1); // Never antialias or alpha-blend icon edges.
        if (x < 3 || x > 37 || y < 3 || y > 37) assert(!pixels[y][x]);
        ink += pixels[y][x];
    }
    assert(ink > 30 && ink < 600); // No invisible, clipped or filled-square morph frame.
    frames++;
}
static void picture(const char *dir, const char *name, const UpdateIcon *icon, float now) {
    if (!dir) return;
    char path[1024];snprintf(path, sizeof path, "%s/%s.pgm", dir, name);
    FILE *f = fopen(path, "wb");assert(f);
    uint8_t pixels[40][40];pixels_at(icon, now, pixels);
    fprintf(f, "P5\n40 40\n255\n");
    for (int y = 0; y < 40; y++) for (int x = 0; x < 40; x++) fputc(pixels[y][x] * 255, f);
    fclose(f);
}
int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : NULL;
    uint8_t before[40][40], after[40][40];
    // Every state can be interrupted without a discontinuity at the handoff.
    for (int a = UI_SPIN; a <= UI_FORGING; a++) for (int b = UI_SPIN; b <= UI_FORGING; b++) {
        UpdateIcon icon = {0};update_icon_set(&icon, a, 0);
        pixels_at(&icon, .37f, before);update_icon_set(&icon, b, .37f);
        pixels_at(&icon, .37f, after);assert(!memcmp(before, after, sizeof before));
        for (int i = 0; i <= 60; i++) pixels_at(&icon, .37f + i / 60.f, after);
        pixels_at(&icon, 1.6f, before);update_icon_set(&icon, UI_CHECK, 1.6f);
        pixels_at(&icon, 1.6f, after);assert(!memcmp(before, after, sizeof before));
    }
    UpdateIcon spin = {0};update_icon_set(&spin, UI_SPIN, 0);
    for (int i = 0; i < 32; i++) {
        char name[32];snprintf(name, sizeof name, "spin-%02d", i);
        picture(dir, name, &spin, i * 1.2f / 32);
    }
    UpdateIcon down = spin;update_icon_set(&down, UI_DOWNLOAD, 1.2f);
    for (int i = 0; i <= 12; i++) {
        char name[32];snprintf(name, sizeof name, "to-download-%02d", i);
        picture(dir, name, &down, 1.2f + i * .04f);
    }
    pixels_at(&down, 2, after);
    // Aligned 3-pixel stem and tray, symmetric arrow, no filtered fringe.
    for (int y = 6; y <= 12; y++) for (int x = 18; x <= 22; x++)
        assert(after[y][x] == (x >= 19 && x <= 21));
    for (int y = 31; y <= 35; y++) for (int x = 9; x <= 31; x++)
        assert(after[y][x] == (y >= 32 && y <= 34));
    update_icon_set(&down, UI_DOWNLOADING, 2);
    pixels_at(&down, 2.9f, before);pixels_at(&down, 3.35f, after);
    assert(memcmp(before, after, sizeof before)); // The downloading arrow really moves.
    for (int y = 31; y < 36; y++) assert(!memcmp(before[y], after[y], 40)); // Tray stays fixed.
    for (int i = 0; i <= 12; i++) {
        char name[32];snprintf(name, sizeof name, "download-%02d", i);
        picture(dir, name, &down, 2.9f + i * .075f);
    }
    update_icon_set(&down, UI_CHECK, 3.35f);
    for (int i = 0; i <= 12; i++) {
        char name[32];snprintf(name, sizeof name, "to-check-%02d", i);
        picture(dir, name, &down, 3.35f + i * .04f);
    }
    UpdateIcon direct = {0};update_icon_set(&direct, UI_SPIN, 0);update_icon_set(&direct, UI_CHECK, 1.2f);
    picture(dir, "current-check", &direct, 2);
    pixels_at(&direct, 2, before);pixels_at(&down, 4, after);
    assert(!memcmp(before, after, sizeof before)); // Same final check for both paths.
    UpdateIcon forge = {0};update_icon_set(&forge,UI_FORGING,0);
    // The head must stay perpendicular to the handle for the entire swing.
    // A horizontal head joined to a diagonal handle was visibly malformed.
    for(int i=0;i<120;i++){
        UpdateIconFrame frame;update_icon_shape(UI_FORGING,i*.72f/120,&frame);
        float hx=frame.p[1][16].x-frame.p[1][0].x,hy=frame.p[1][16].y-frame.p[1][0].y;
        float tx=frame.p[2][1].x-frame.p[2][0].x,ty=frame.p[2][1].y-frame.p[2][0].y;
        assert(fabsf(hx*tx+hy*ty)<.001f);
        assert(fabsf(tx*tx+ty*ty-144)<.001f);
        pixels_at(&forge,i*.72f/120,after);
    }
    pixels_at(&forge,0,before);pixels_at(&forge,.4f,after);assert(memcmp(before,after,sizeof before));
    for(int i=0;i<12;i++){char name[32];snprintf(name,sizeof name,"forge-%02d",i);picture(dir,name,&forge,i*.06f);}
    update_icon_set(&forge,UI_CHECK,.72f);
    for(int i=0;i<=12;i++){char name[32];snprintf(name,sizeof name,"forge-check-%02d",i);picture(dir,name,&forge,.72f+i*.04f);}
    pixels_at(&forge,1.3f,before);pixels_at(&direct,2,after);assert(!memcmp(before,after,sizeof before));
    printf("PASS: %d pixel masks, 49 state transitions, interruption continuity, crisp strokes, hammer and download motion.\n", frames);
}
