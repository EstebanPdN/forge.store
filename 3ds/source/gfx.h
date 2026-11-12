// Textures (.ftx sheets), background loading, text cache and shape helpers.
#pragma once
#include "common.h"

#define SHEET_MAX 64

typedef enum { SHEET_EMPTY, SHEET_QUEUED, SHEET_DECODED, SHEET_READY, SHEET_FAILED } SheetState;

typedef struct {
    volatile SheetState state;
    char path[96];
    C3D_Tex tex;
    int count, w, h, fmt;
    Tex3DS_SubTexture sub[SHEET_MAX];
    void *pixels;  // decoded on the loader thread, uploaded on the main thread
    volatile bool discard;
    size_t size, copied;  // upload is spread over frames: bytes already in the texture
    bool tex_init;
    bool crisp;  // baked UI pixels: nearest magnification, linear reduction
} Sheet;

void gfx_init(void);
void gfx_exit(void);
/** Loads now, on the calling thread (used before the first frame). */
bool sheet_load_now(Sheet *s, const char *path);
/** Queues a background load; the sheet becomes READY a few frames later. */
void sheet_request(Sheet *s, const char *path);
void sheet_free(Sheet *s);
static inline bool sheet_ok(const Sheet *s) { return s->state == SHEET_READY; }
static inline C2D_Image sheet_img(Sheet *s, int i) { return (C2D_Image){&s->tex, &s->sub[i]}; }
/** Copies decoded sheets into GPU textures, a few hundred KB per frame. Call once per frame. */
void sheets_pump(void);

// UI atlas lookup by name (ui.json).
extern Sheet UI;
int ui_index(const char *name);
C2D_Image ui_img(const char *name);

// ---- drawing ---------------------------------------------------------------------
void draw_img(C2D_Image im, float x, float y, float sx, float sy, float alpha);
/** Draws a white image recoloured to `color` (alpha included). */
void draw_img_tint(C2D_Image im, float x, float y, float sx, float sy, u32 color);
void draw_img_rot(C2D_Image im, float cx, float cy, float sx, float sy, float angle, float alpha);
void draw_icon(const char *name, float cx, float cy, float size, u32 color);
int icon_index(const char *name);
void draw_badge(const char *button, float x, float y, float alpha);
void rect(float x, float y, float w, float h, u32 c);
void rrect(float x, float y, float w, float h, float r, u32 c);
void rrect_outline(float x, float y, float w, float h, float r, float t, u32 c);
void rrect_hgrad(float x, float y, float w, float h, float r, u32 c1, u32 c2);
void circle(float cx, float cy, float r, u32 c);
/** Filled ellipse in the box (x, y, w, h); soft edge, for small particles. */
void ellipse(float x, float y, float w, float h, u32 c);
void ring(float cx, float cy, float r, float thick, float from, float to, u32 c);
void vgrad(float x, float y, float w, float h, u32 top, u32 bottom);
/** Local panel offset applied by ALL gfx primitives and scissor; reset after drawing.
 * Do not also call C2D_ViewTranslate with these offsets. */
extern float DRAW_X, DRAW_Y;
void scissor(int screen_w, float x, float y, float w, float h);
void scissor_off(void);

// ---- text (the prototype's fonts, baked per pixel size) ------------------------------------
enum { FONT_UI = 0, FONT_BRAND = 1, FONT_MONO = 2 };
enum { FONT_WEIGHT_BODY = 500, FONT_WEIGHT_LABEL = 600, FONT_WEIGHT_TITLE = 700 };
enum { TEXT_META_PX = 9, TEXT_LABEL_PX = 10, TEXT_BODY_PX = 11, TEXT_TITLE_PX = 15 };
enum { TEXT_META_LINE = 12, TEXT_LABEL_LINE = 14, TEXT_BODY_LINE = 15, TEXT_TITLE_LINE = 18 };
enum { AL_LEFT = 0, AL_CENTER = 1, AL_RIGHT = 2 };
/** One line at the CSS px size of the prototype (Inter 500); y is the top of the line box.
 *  Returns the line height. */
float text(const char *s, float x, float y, float size, u32 color, int align);
/** Same with an explicit weight and family (FONT_UI Inter, FONT_BRAND DM Sans). */
float text_style(const char *s, float x, float y, float size, int weight, int family, u32 color, int align);
float text_style_w(const char *s, float size, int weight, int family);
/** Explicit weight, family and CSS letter-spacing. */
float text_ex(const char *s, float x, float y, float size, int weight, int family, float spacing, u32 color, int align);
float text_ex_w(const char *s, float size, int weight, int family, float spacing);
/** Uppercase eyebrow label: Inter 600 with CSS letter-spacing. */
float text_caps(const char *s, float x, float y, float size, float spacing, u32 color, int align);
float text_caps_w(const char *s, float size, float spacing);
/** Paragraph wrapped to `width` (Inter 500; native 9/12, 10/14, 11/15 line boxes). Returns its height. */
float text_wrap(const char *s, float x, float y, float size, u32 color, float width);
/** Text that changes every frame (numbers, times). */
void text_dyn(const char *s, float x, float y, float size, u32 color, int align);
float text_w(const char *s, float size);
float text_wrap_h(const char *s, float size, float width);
float line_h(float size);
/** Distance from the top of a line (the y given to text calls) to its baseline. */
float text_base(float size, int weight, int family);
void text_reset(void);
void text_frame(void);

float text_wrap_style(const char*s,float x,float y,float size,int weight,u32 color,float width);
float text_wrap_h_style(const char*s,float size,int weight,float width);

/** Explicit family for the rich reader's monospaced code. */
float text_wrap_family(const char*,float,float,float,int,int,u32,float);
float text_wrap_h_family(const char*,float,int,int,float);
