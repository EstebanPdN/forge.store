#pragma once
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// One native 40×40 pixel family. Geometry can move smoothly, but coverage is
// binary and is drawn at 1:1: no texture scaling, filtering or subpixel strokes.
enum { UPDATE_ICON_SIZE = 40, UPDATE_ICON_PARTS = 3, UPDATE_ICON_POINTS = 17 };
typedef enum { UI_SPIN, UI_DOWNLOAD, UI_DOWNLOADING, UI_CHECK, UI_WARN, UI_FORGE, UI_FORGING } UpdateIconKind;
typedef struct { float x, y; } UpdateIconPoint;
typedef struct { UpdateIconPoint p[UPDATE_ICON_PARTS][UPDATE_ICON_POINTS]; float green; } UpdateIconFrame;
typedef struct {
    UpdateIconFrame from;
    UpdateIconKind kind;
    float changed;
    bool initialized, morph;
} UpdateIcon;

static inline float update_icon_mix(float a, float b, float t) { return a + (b - a) * t; }
static inline float update_icon_ease(float t) {
    t = fminf(1, fmaxf(0, t));
    return t * t * (3 - 2 * t);
}
static inline UpdateIconPoint update_icon_lerp(UpdateIconPoint a, UpdateIconPoint b, float t) {
    return (UpdateIconPoint){update_icon_mix(a.x, b.x, t), update_icon_mix(a.y, b.y, t)};
}
static inline UpdateIconPoint update_icon_corner(UpdateIconPoint a, UpdateIconPoint b,
                                                UpdateIconPoint c, float t, float corner) {
    return t < corner ? update_icon_lerp(a, b, t / corner)
                      : update_icon_lerp(b, c, (t - corner) / (1 - corner));
}
static inline void update_icon_shape(UpdateIconKind kind, float age, UpdateIconFrame *out) {
    const float pi = 3.14159265358979323846f;
    // Thirty-two exact angular poses avoid shimmer from continuously filtered rotation.
    float angle = floorf(fmodf(fmaxf(0, age), 1.2f) / 1.2f * 32) * (2 * pi / 32);
    float bounce = kind == UI_DOWNLOADING ? roundf(3 * (.5f - .5f * cosf(age * 2 * pi / .9f))) : 0;
    out->green = kind == UI_CHECK ? 1 : 0;
    for (int part = 0; part < UPDATE_ICON_PARTS; part++) {
        for (int i = 0; i < UPDATE_ICON_POINTS; i++) {
            float t = i / 16.f, all = (part + t) / 3;
            UpdateIconPoint p;
            if (kind == UI_SPIN) {
                float a = angle + all * pi * 1.5f;
                p = (UpdateIconPoint){20 + 15 * cosf(a), 20 + 15 * sinf(a)};
            } else if (kind == UI_CHECK) {
                p = update_icon_corner((UpdateIconPoint){8, 19}, (UpdateIconPoint){16, 27},
                                       (UpdateIconPoint){32, 11}, all, 1.f / 3);
            } else if (kind == UI_WARN) {
                if (part == 0) {
                    p = t < .5f ? update_icon_corner((UpdateIconPoint){20, 6}, (UpdateIconPoint){5, 33},
                                                    (UpdateIconPoint){35, 33}, t * 2, .5f)
                                : update_icon_lerp((UpdateIconPoint){35, 33}, (UpdateIconPoint){20, 6}, (t - .5f) * 2);
                } else p = update_icon_lerp((UpdateIconPoint){20, part == 1 ? 16 : 28},
                                            (UpdateIconPoint){20, part == 1 ? 23 : 28}, t);
            } else if (kind == UI_FORGE || kind == UI_FORGING) {
                // The same three polylines become the check: anvil, handle, head.
                // The hammer pivots around its grip; rasterization snaps each pose
                // to LCD pixels rather than filtering a rotated texture.
                static const UpdateIconPoint anvil[] = {
                    {4,24},{34,24},{7,26},{34,26},{16,28},{28,28},{17,30},{27,30},
                    {17,32},{27,32},{13,34},{32,34},{12,36},{33,36},{33,36},{33,36},{33,36}
                };
                if (part == 0) {
                    p = anvil[i];
                } else {
                    float cycle = fmodf(fmaxf(0,age)/(kind==UI_FORGING?.72f:1.8f),1);
                    float swing = cycle<.5f ? 1 : cycle<.72f ? 1-update_icon_ease((cycle-.5f)/.22f) : update_icon_ease((cycle-.72f)/.28f);
                    float rotation = -.35f + .40f*swing;
                    if(part==1)p=update_icon_lerp((UpdateIconPoint){17,11},(UpdateIconPoint){31,18},t);
                    else {
                        // A rigid T: the head's long axis is perpendicular to
                        // the 2:1 handle, even while the whole hammer pivots.
                        // Three parallel strokes fill the rectangular head.
                        int vertex=i<6?i:5;
                        float along=-2+2*(vertex/2), across=vertex%2?6:-6;
                        const float ux=.89442719f,uy=.44721360f;
                        p=(UpdateIconPoint){17+along*ux-across*uy,11+along*uy+across*ux};
                    }
                    float x=p.x-31,y=p.y-18;
                    p=(UpdateIconPoint){31+x*cosf(rotation)-y*sinf(rotation),18+x*sinf(rotation)+y*cosf(rotation)};
                }
            } else {
                if (part == 0) p = update_icon_lerp((UpdateIconPoint){20, 5 + bounce}, (UpdateIconPoint){20, 23 + bounce}, t);
                else if (part == 1) p = update_icon_corner((UpdateIconPoint){11, 14 + bounce}, (UpdateIconPoint){20, 23 + bounce},
                                                          (UpdateIconPoint){29, 14 + bounce}, t, .5f);
                else p = update_icon_lerp((UpdateIconPoint){8, 33}, (UpdateIconPoint){32, 33}, t);
            }
            out->p[part][i] = p;
        }
    }
}
static inline void update_icon_frame(const UpdateIcon *icon, float now, UpdateIconFrame *out) {
    update_icon_shape(icon->kind, now - icon->changed, out);
    if (!icon->morph) return;
    float t = update_icon_ease((now - icon->changed) / .48f);
    if (t >= 1) return;
    out->green = update_icon_mix(icon->from.green, out->green, t);
    for (int part = 0; part < UPDATE_ICON_PARTS; part++)
        for (int i = 0; i < UPDATE_ICON_POINTS; i++)
            out->p[part][i] = update_icon_lerp(icon->from.p[part][i], out->p[part][i], t);
}
static inline void update_icon_set(UpdateIcon *icon, UpdateIconKind kind, float now) {
    if (icon->initialized && icon->kind == kind) return;
    if (icon->initialized) {
        UpdateIconFrame current;
        update_icon_frame(icon, now, &current);
        icon->from = current;
    }
    else update_icon_shape(kind, 0, &icon->from);
    icon->morph = icon->initialized;
    icon->kind = kind;
    icon->changed = now;
    icon->initialized = true;
}

static inline void update_icon_raster(const UpdateIconFrame *frame,
                                     uint8_t pixels[UPDATE_ICON_SIZE][UPDATE_ICON_SIZE]) {
    memset(pixels, 0, UPDATE_ICON_SIZE * UPDATE_ICON_SIZE);
    for (int part = 0; part < UPDATE_ICON_PARTS; part++) {
        for (int i = 1; i < UPDATE_ICON_POINTS; i++) {
            // Quantize vertices before coverage: all final runs occupy whole LCD pixels.
            int ax = (int)roundf(frame->p[part][i - 1].x), ay = (int)roundf(frame->p[part][i - 1].y);
            int bx = (int)roundf(frame->p[part][i].x), by = (int)roundf(frame->p[part][i].y);
            int left = (ax < bx ? ax : bx) - 2, right = (ax > bx ? ax : bx) + 2;
            int top = (ay < by ? ay : by) - 2, bottom = (ay > by ? ay : by) + 2;
            int dx = bx - ax, dy = by - ay, length = dx * dx + dy * dy;
            for (int y = top; y <= bottom; y++) for (int x = left; x <= right; x++) {
                if ((unsigned)x >= UPDATE_ICON_SIZE || (unsigned)y >= UPDATE_ICON_SIZE) continue;
                float t = length ? fminf(1, fmaxf(0, ((x - ax) * dx + (y - ay) * dy) / (float)length)) : 0;
                float ex = x - (ax + t * dx), ey = y - (ay + t * dy);
                if (ex * ex + ey * ey <= 2.25f) pixels[y][x] = 1;
            }
        }
    }
}
