// All store screens. Layout numbers match the HTML prototype (native 3DS pixels).
#include "scenes.h"
#include "audio.h"
#include "catalog.h"
#include "fx.h"
#include "gfx.h"
#include "install.h"
#include "network.h"
#include "video.h"
#include "media.h"
#include "dsp_dump.h"
#include "hbl_launch.h"
#include "catalog_sync.h"
#include "rom_find.h"
#include "universal_sync.h"
#include "remote_image.h"
#include "sd_usage.h"
#include "file_view.h"
#include <dirent.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <strings.h>
#include <unistd.h>

float EYE;
Theme TH;
static Sheet HOME_ICONS;
static bool test_freeze_media;
static Sheet BG[4]; // top-light, top-dark, bottom-light, bottom-dark

enum { SC_INTRO, SC_HOME, SC_DETAIL, SC_INSTALL, SC_SETTINGS, SC_SEARCH, SC_DOWNLOADS, SC_SD, SC_GITHUB };
static int scene = SC_INTRO, next_scene = -1, trans_phase, previous_scene = SC_HOME;
static float trans_t;

static u32 fa(u32 c, float a) { return (c & 0x00FFFFFFu) | ((u32)((c >> 24) * clampf(a, 0, 1)) << 24); }
static u32 C(u32 hex, float a) { return rgb(hex, a); }

void theme_apply(void) {
    if (S.dark) {
        TH = (Theme){0x1b1c1f, 0xeef1f2, 0xc2ccd1, 0xa1b0b8, 0x26282c, 0x2f3236, 0x3a3d42, 0x26282c,
                     0xffffff, 0xa3c6b8, 0xf1f2f3, 0x6cc3a5, 0xffa63d, .97f,     .16f,     true};
    } else {
        TH = (Theme){0xf4f7f8, 0x2c3e45, 0x455c66, 0x5d7079, 0xffffff, 0xeef2f4, 0xe3e9ec, 0xffffff,
                     0x2c3e45, 0x6b8d83, 0x34464d, 0x4f9e86, 0xffa63d, .96f,     .17f,     false};
    }
}

// ---- store resources ---------------------------------------------------------------------------
// The previews fetched after the first start are one download for the user ("Store
// resources"), never a list of files. Refreshed once per frame in scenes_update.
typedef struct {
    unsigned items, ready;
    u64 bytes, received;
    float progress;
    MediaState state;  // of the package as a whole
    char error[160];
} MediaPackage;
static MediaPackage PKG;
static void media_package_refresh(void) {
    MediaPackage p = {0};
    bool downloading = false, verifying = false, queued = false, failed = false, cleared = false;
    MediaItem m;
    for (unsigned i = 0, n = media_count(); i < n; i++) {
        if (!media_item(i, &m)) continue;
        p.items++;
        p.bytes += m.total;
        switch (m.state) {
        case MEDIA_COMPLETED: p.ready++, p.received += m.total; break;
        case MEDIA_DOWNLOADING: downloading = true, p.received += m.received; break;
        case MEDIA_VERIFYING: verifying = true, p.received += m.total; break;  // already on the SD
        case MEDIA_QUEUED: queued = true; break;
        case MEDIA_CLEARED: cleared = true; break;
        case MEDIA_ERROR:
            failed = true;
            if (!p.error[0]) snprintf(p.error, sizeof p.error, "%s", m.error);
            break;
        }
    }
    p.progress = p.bytes ? fminf(1, p.received / (float)p.bytes) : 0;
    p.state = downloading ? MEDIA_DOWNLOADING : verifying ? MEDIA_VERIFYING : queued ? MEDIA_QUEUED :
              failed ? MEDIA_ERROR : cleared ? MEDIA_CLEARED : MEDIA_COMPLETED;
    PKG = p;
}
bool scenes_downloading(void) { return install_active() || (PKG.items && PKG.state == MEDIA_DOWNLOADING); }

// ---- shared pieces -------------------------------------------------------------------------
static void draw_bg(bool top, float a) {
    Sheet *s = &BG[(top ? 0 : 2) + (S.dark ? 1 : 0)];
    if (!sheet_ok(s)) {
        rect(0, 0, top ? TOP_W : BOT_W, SCR_H, C(TH.bg, a));
        return;
    }
    float d = fmodf(NOW * 7 / 18.0f, 7); // the site's slow dot drift
    draw_img(sheet_img(s, 0), -7 + d, -7 + d, 1, 1, a);
}

static void glass(float x, float y, float w, float h, float r, float a) {
    rrect(x, y, w, h, r, C(TH.glass, TH.glass_a * a));
    rrect_outline(x, y, w, h, r, 1, C(TH.line, TH.line_a * a));
}

/** UTF-8-safe single line; never lets remote titles paint adjacent controls. */
static void fit_label_style(char *out, size_t cap, const char *label, float size, int weight, float width) {
    if(!cap)return;
    const char *source=label?label:"";size_t length=strlen(source),take=length<cap?length:cap-1;
    while(take<length&&take&&((unsigned char)source[take]&0xc0)==0x80)take--;
    memmove(out,source,take);out[take]=0;
    if (take == length && text_style_w(out,size,weight,0) <= width) return;
    size_t n = strlen(out);
    for (;;) {
        if (n + 4 <= cap) {
            memcpy(out+n,"…",4);
            if (text_style_w(out,size,weight,0) <= width) return;
        }
        if (!n) { out[0]=0; return; }
        do { --n; } while (n && ((unsigned char)out[n] & 0xc0) == 0x80);
        out[n]=0;
    }
}
static void fit_label(char*out,size_t cap,const char*label,float size,float width){fit_label_style(out,cap,label,size,size>=12?700:500,width);}
static void text_fit(const char *s, float x, float y, float size, float width, u32 color) {
    char buf[512]; fit_label(buf, sizeof buf, s, size, width);
    text_style(buf,x,y,size,size>=12?700:500,0,color,AL_LEFT);
}
static void text_fit_style(const char*s,float x,float y,float size,int weight,float width,u32 color){char b[512];fit_label_style(b,sizeof b,s,size,weight,width);text_style(b,x,y,size,weight,0,color,AL_LEFT);}
static void fmt_date(char *out, size_t n, const char *iso);
static void fmt_stars(char *out, size_t n, int stars) {
    if (stars >= 1000) snprintf(out, n, "%.1fk", stars / 1000.0);
    else snprintf(out, n, "%d", stars);
}
/** Metadata pill; same 9/12 Inter, .3px tracking, 10px icon and 3px gap as .chip. */
static float chip_width(const char *label,const char *ic){return text_ex_w(label,9,600,0,.3f)+.3f+10+(ic?13:0);}
static float chip(float x, float y, const char *label, const char *ic, u32 fg, u32 bg, float a) {
    float w=chip_width(label,ic);
    rrect(x, y, w, 16, 8, fa(bg, a));
    float tx = x + 5;
    if (ic) { draw_icon(ic,tx+5,y+8,10,fa(fg,a));tx+=13; }
    text_ex(label,tx,y+2,9,600,0,.3f,fa(fg,a),AL_LEFT);
    return w;
}

typedef enum { BTN_SURFACE, BTN_PRIMARY, BTN_MINT, BTN_GHOST } BtnStyle;

static void button_shape(float x, float y, float w, float h, const char *label, const char *ic, const char *badge, BtnStyle st, float a,float radius) {
    float r = fminf(radius, fminf(w,h) / 2);
    u32 fg = C(TH.ink, a);
    if (st == BTN_PRIMARY) {
        rrect_hgrad(x, y, w, h, r, C(FORGE, a), C(FORGE2, a));
        fg = C(0xffffff, a);
    } else if (st == BTN_MINT) {
        rrect(x, y, w, h, r, C(0x4aae8c, a));
        fg = C(0xffffff, a);
    } else if (st == BTN_GHOST) {
        rrect_outline(x, y, w, h, r, 1, C(TH.line, 0.25f * a));
        fg=C(TH.ink_soft,a);
    } else {
        rrect(x, y, w, h, r, C(TH.surface, a));
        rrect_outline(x, y, w, h, r, 1, C(TH.line, TH.line_a * a));
    }
    bool allowed = badge && ((!strcmp(badge, "A") && ic && (!strcmp(ic, "download") || !strcmp(ic, "update"))) ||
                             (!strcmp(badge, "B") && label && !strcmp(label, T("action.cancel")) && scene == SC_DETAIL) ||
                             (!strcmp(badge, "X") && ic && !strcmp(ic, "github")));
    if (!allowed) badge = NULL;
    float font_size=st==BTN_PRIMARY || st==BTN_MINT?11:10;
    char fitted[256];
    if (label) { fit_label_style(fitted,sizeof fitted,label,font_size,600,w-(scene==SC_HOME&&y>=208?4:14)-(ic?14:0)-(badge?14:0)); label=fitted; }
    float lw = label?text_style_w(label,font_size,600,0) : 0, iw = ic ? 14 : 0, bw = badge ? 14 : 0;
    float cx = x + w / 2 - (lw + iw + bw) / 2;
    if(ic&&!label&&!badge)draw_icon(ic,x+w/2,y+h/2,12,fg);
    else if (ic)
        draw_icon(ic, cx + 5.5f, y + h / 2, 11, fg), cx += iw;
    if (label)
        text_style(label,cx,y+h/2-line_h(font_size)/2,font_size,600,0,fg,AL_LEFT), cx += lw + 3;
    if (badge)
        draw_badge(badge, cx, y + h / 2 - 5.5f, a);
}
static void button(float x,float y,float w,float h,const char *label,const char *ic,const char *badge,BtnStyle st,float a){button_shape(x,y,w,h,label,ic,badge,st,a,8);}
static void button_round(float x,float y,float w,float h,const char *label,const char *ic,const char *badge,BtnStyle st,float a){button_shape(x,y,w,h,label,ic,badge,st,a,13);}

// Draw and touch geometry share the space reserved for translated action labels.
enum { CONFIRM_CANCEL_W=92, CONFIRM_INSTALL_X=114, CONFIRM_INSTALL_W=190,
       INSTALL_BROWSE_W=211, INSTALL_CANCEL_X=221, INSTALL_CANCEL_W=94 };

static bool hit(float x, float y, float w, float h) {
    return IN.touch_up && !IN.dragged && IN.start_x >= x && IN.start_x < x + w && IN.start_y >= y && IN.start_y < y + h &&
           abs(IN.last_x - IN.start_x) < 10 && abs(IN.last_y - IN.start_y) < 10;
}

// UI reads never touch the SD. Installer/launch validation retains its fresh,
// synchronous checks; only presentation uses these worker-owned probe results.
static struct { json_t *requirement; FileView file; } requirement_views[256];
static unsigned requirement_view_count;
static FileView dsp_view;
static App *requirement_check_app;
static json_t *requirement_check;
static bool requirement_check_install;
static unsigned requirement_check_ticket,requirement_check_rom_ticket;
static void requirement_views_rebuild(void) {
    requirement_view_count=0;rom_find_reset();
    for(int a=0;a<NAPPS;a++) {
        size_t i;json_t *r;
        json_array_foreach(json_object_get(APPS[a].j,"requires"),i,r) {
            if(requirement_view_count>=sizeof requirement_views/sizeof requirement_views[0]) break;
            char path[1024];FileView view={0};
            if(app_requirement_path(r,path,sizeof path))view=file_view_watch(path,true);
            rom_find_watch(r);
            requirement_views[requirement_view_count].requirement=r;
            requirement_views[requirement_view_count++].file=view;
        }
    }
}
static bool requirement_view_has(json_t *req) {
    // Ports take the ROM under any name: the scan decides. Not known yet is not a warning.
    if(rom_find_ruled(req))return !rom_find_known(req)||rom_find_have(req);
    for(unsigned i=0;i<requirement_view_count;i++)
        if(requirement_views[i].requirement==req)return file_view_nonempty(requirement_views[i].file);
    return false;
}
static json_t *app_missing_requirement_view(App *a) {
    if(!a)return NULL;
    size_t i;json_t *r;
    json_array_foreach(json_object_get(a->j,"requires"),i,r)if(!requirement_view_has(r))return r;
    return NULL;
}
static AppStatus app_status_view(App *a) {
    if(!a)return ST_AVAILABLE;
    if(install_busy(a))return ST_BUSY;
    if(a->dev)return ST_DEV;
    const char *version=app_installed_version(a);
    if(!version||!*version)return ST_AVAILABLE;
    json_t *record=installed_record(a);
    const char *channel=json_string_value(json_object_get(record,"channel"));
    bool exp=channel?!strcmp(channel,"experimental"):json_is_true(json_object_get(record,"experimental"));
    const char *latest=json_string_value(json_object_get(app_release(a,exp),"tag"));
    if(latest&&*latest&&strcmp(latest,version))return ST_UPDATE;
    return app_missing_requirement_view(a)?ST_NEEDS_FILE:ST_INSTALLED;
}
static void requirement_check_start(App *a,json_t *req,bool install) {
    requirement_check_app=a;requirement_check=req;requirement_check_install=install;
    requirement_check_ticket=file_view_refresh();requirement_check_rom_ticket=rom_find_refresh();
}

static void status_icon(App *a, float cx, float cy, float al) {
    switch (app_status_view(a)) {
    case ST_INSTALLED:
        circle(cx, cy, 7.5f, C(TH.bg, al)), circle(cx, cy, 6, C(MINT, al)), draw_icon("check", cx, cy, 9, C(0xffffff, al));
        break;
    case ST_UPDATE:
        circle(cx, cy, 7.5f, C(TH.bg, al)), circle(cx, cy, 6, C(FORGE, al)), draw_icon("update", cx, cy, 9, C(0xffffff, al));
        break;
    case ST_NEEDS_FILE:
        circle(cx, cy, 7.5f, C(TH.bg, al)), circle(cx, cy, 6, C(AMBER, al)), draw_icon("warn", cx, cy, 9, C(0xffffff, al));
        break;
    default:
        break;
    }
}

/** App icon with its top-left at (x, y), rendered at exactly `size` px (scaled only while animating). */
static void icon_at(App *a, float x, float y, int size, float scale, float al) {
    if(a->universal&&!catalog_icon_ready(a)){rrect(x,y,size*scale,size*scale,size*.23f,C(a->color,al));draw_icon("box",x+size*scale/2,y+size*scale/2,size*.5f*scale,C(0xffffff,al));return;}
    if(a->universal&&a->icon<BUILTIN_APPS){const char *url=json_string_value(json_object_get(a->j,"icon"));if(remote_image_ready(url)){remote_image_draw(url,x,y,size*scale,size*scale,al);return;}}
    C2D_Image im=size==44 && !a->universal && sheet_ok(&HOME_ICONS) && a->icon<HOME_ICONS.count?sheet_img(&HOME_ICONS,a->icon):app_icon_px(a,size);
    float s = scale * size / im.subtex->width;
    draw_img(im, x, y, s, s, al); // apps in development are rendered muted (the .dev filter)
}

static void draw_app_icon(App *a, float cx, float cy, int size, float al) { icon_at(a, cx - size / 2.0f, cy - size / 2.0f, size, 1, al); }

// ---- 3D coins: poses rendered from the prototype's coin (render-coins.mjs) --------------------------
typedef struct {
    Sheet wobble, spin;
    int frames, spins;
    float cx, cy, size;
} Coin;
static Coin COIN_EMBLEM, COIN_THEME[2];

static void coin_load(Coin *c, const char *name, bool spin) {
    static json_t *meta;
    if (!meta)
        meta = json_load_file("romfs:/gfx/coins.json", 0, NULL);
    json_t *m = json_object_get(meta, name);
    c->frames = json_integer_value(json_object_get(m, "wobble"));
    c->spins = json_integer_value(json_object_get(m, "spin"));
    c->cx = json_number_value(json_object_get(m, "cx"));
    c->cy = json_number_value(json_object_get(m, "cy"));
    c->size = json_number_value(json_object_get(m, "size"));
    char path[64];
    snprintf(path, sizeof path, "romfs:/gfx/coin-%s.ftx", name);
    sheet_load_now(&c->wobble, path);
    if (spin && c->spins) {
        snprintf(path, sizeof path, "romfs:/gfx/coin-%s-spin.ftx", name);
        sheet_load_now(&c->spin, path);
    }
}

static void coin_free(Coin *c) {
    sheet_free(&c->wobble);
    sheet_free(&c->spin);
}

/** CSS cubic-bezier(x1, y1, x2, y2) timing function at progress x. */
static float bezier(float x, float x1, float y1, float x2, float y2) {
    x = clampf(x, 0, 1);
    float t = x;
    for (int i = 0; i < 8; i++) {
        float bx = 3 * (1 - t) * (1 - t) * t * x1 + 3 * (1 - t) * t * t * x2 + t * t * t - x;
        float dx = 3 * (1 - t) * (1 - t) * x1 + 6 * (1 - t) * t * (x2 - x1) + 3 * t * t * (1 - x2);
        if (fabsf(bx) < 1e-4f || fabsf(dx) < 1e-6f)
            break;
        t = clampf(t - bx / dx, 0, 1);
    }
    return 3 * (1 - t) * (1 - t) * t * y1 + 3 * (1 - t) * t * t * y2 + t * t * t;
}

// Shared V2 TabBar. Geometry and hit testing use the same sampled state.
#define UI_TAB_MAX 16
#define UI_TAB_SECONDS .340f
typedef struct {
    bool ready;
    int selected, count, direction;
    float padding;
    float x, y, w, widths[UI_TAB_MAX], positions[UI_TAB_MAX];
    float shift, thumb, thumb_w, from_shift, from_thumb, from_w, to_shift, to_thumb, to_w, changed;
} UiTabs;
static float tabs_progress(const UiTabs *t) { return bezier(span(NOW, t->changed, UI_TAB_SECONDS), .22f, 1, .36f, 1); }
static float tabs_content(const UiTabs *t,float a){float p=tabs_progress(t);DRAW_X=t->direction*12*(1-p);return a*p;}
static void tabs_sample(UiTabs *t) {
    float p = tabs_progress(t);
    t->shift = lerpf(t->from_shift, t->to_shift, p);
    t->thumb = lerpf(t->from_thumb, t->to_thumb, p);
    t->thumb_w = lerpf(t->from_w, t->to_w, p);
}
static void tabs_layout(UiTabs *t, float x, float y, float w, const char *const *labels, const char *const *icons, int count, int selected) {
    count = count > UI_TAB_MAX ? UI_TAB_MAX : count;
    if (count <= 0) return;
    selected = (int)clampf(selected, 0, count - 1);
    t->x=x; t->y=y; t->w=w; t->count=count;
    float viewport = w - 38, total = 0;
    for(int i=0;i<count;i++)total+=ceilf(text_style_w(labels[i],10,600,0))+(icons&&icons[i]?16:0);
    // Keep normal localized labels fully visible before falling back to horizontal scrolling.
    float padding=clampf(floorf((viewport-6-total)/(2*count)),t->padding?4:6,t->padding?t->padding:9);
    total=0;
    for (int i=0;i<count;i++) { t->widths[i] = ceilf(text_style_w(labels[i], 10, 600, 0)) + 2*padding + (icons && icons[i] ? 16 : 0); total += t->widths[i]; }
    int spare = (int)fmaxf(0, viewport - 6 - total);
    total = 3;
    for (int i=0;i<count;i++) { t->widths[i] += spare/count + (i < spare%count); t->positions[i]=total; total+=t->widths[i]; }
    float shift = roundf(clampf(t->positions[selected]+t->widths[selected]/2-viewport/2, 0, fmaxf(0,total+3-viewport)));
    if (!t->ready || t->selected != selected || t->to_shift != shift || t->to_w != t->widths[selected]) {
        if (t->ready) tabs_sample(t);
        t->direction = selected >= t->selected ? 1 : -1;
        t->selected=selected; t->changed=NOW;
        t->from_shift=t->shift; t->from_thumb=t->thumb; t->from_w=t->thumb_w;
        t->to_shift=shift; t->to_thumb=t->positions[selected]; t->to_w=t->widths[selected];
        if (!t->ready) { t->from_shift=shift; t->from_thumb=t->to_thumb; t->from_w=t->to_w; t->changed=NOW-1; }
        t->ready=true;
    }
    tabs_sample(t);
}
static void tabs_draw(UiTabs *t, const char *const *labels, const char *const *icons, bool wrap, float a) {
    if (!t->ready) return;
    tabs_sample(t);
    float x=t->x+19, vw=t->w-38, y=t->y;
    draw_badge("L", t->x+2, y+8, a*(wrap || t->selected ? 1 : .42f));
    draw_badge("R", t->x+t->w-14, y+8, a*(wrap || t->selected<t->count-1 ? 1 : .42f));
    glass(x,y,vw,28,9,a);
    scissor(BOT_W,x+1,y+1,vw-2,26);
    rrect(x+t->thumb-t->shift,y+3,t->thumb_w,22,6,C(S.dark?TH.surface3:TH.surface,a));
    for (int i=0;i<t->count;i++) {
        bool on=i==t->selected; float iw=icons && icons[i]?16:0;
        float lw=text_style_w(labels[i],10,600,0), tx=x+t->positions[i]-t->shift+(t->widths[i]-lw-iw)/2;
        if (iw) draw_icon(icons[i],tx+6,y+14,12,C(on?FORGE:TH.ink_soft,a));
        text_style(labels[i],tx+iw,y+7,10,600,0,C(on?TH.ink:TH.ink_soft,a),AL_LEFT);
    }
    scissor_off();
}
/** Returns chosen index or -1. Call only on the active bottom-screen scene. */
static int tabs_input(UiTabs *t, bool wrap) {
    if (!t->ready) return -1;
    int d=0;
    if (IN.down & KEY_L || hit(t->x,t->y,16,28)) d=-1;
    if (IN.down & KEY_R || hit(t->x+t->w-16,t->y,16,28)) d=1;
    if (d) { int n=t->selected+d; return wrap?(n+t->count)%t->count:(int)clampf(n,0,t->count-1); }
    float left=t->x+19,right=t->x+t->w-19;
    for (int i=0;i<t->count;i++) {
        float x=fmaxf(left,left+t->positions[i]-t->shift), r=fminf(right,left+t->positions[i]+t->widths[i]-t->shift);
        if(r>x && hit(x,t->y,r-x,28)) return i;
    }
    return -1;
}
static void back_button(float a) { glass(4,4,28,28,8,a); draw_icon("back",18,18,12,C(TH.ink,a)); }

/** coin-wobble keyframes (ease-in-out): 0 = rotateY(-24deg), 1 = rotateY(24deg). */
static float wobble_pose(float t, float period) {
    float ph = fmodf(fmaxf(t, 0) / period, 1);
    return ph < .5f ? bezier(ph * 2, .42f, 0, .58f, 1) : 1 - bezier((ph - .5f) * 2, .42f, 0, .58f, 1);
}

/** The soft contact shadow, sliding with the wobble like .coin-shadow. */
static void coin_shadow(Coin *c, float cx, float cy, float pose, float scale, float a) {
    C2D_Image sh = ui_img("coin-shadow");
    float size = c->size * scale, w = size * .72f * .9f;
    u32 col = S.dark ? C(0x000000, .6f * a) : C(0x283c46, .3f * a);
    draw_img_tint(sh, cx - w / 2 + (4 - 8 * pose) * scale, cy + size / 2 + 4 * scale, w / sh.subtex->width, scale, col);
}

/** Draws the wobbling coin centred on (cx, cy); neighbouring poses are blended. */
static void coin_draw(Coin *c, float cx, float cy, float pose, float scale, float a) {
    if (!sheet_ok(&c->wobble) || a <= 0.004f)
        return;
    coin_shadow(c, cx, cy, pose, scale, a);
    float f = clampf(pose, 0, 1) * (c->frames - 1);
    int i = (int)f;
    float w = f - i;
    if (i >= c->frames - 1)
        i = c->frames - 1, w = 0;
    float x = cx - c->cx * scale, y = cy - c->cy * scale;
    // Correct source-over weights: continuous poses even while the coin fades.
    float second = a * w;
    float first = second < .999f ? a * (1 - w) / (1 - second) : 0;
    draw_img(sheet_img(&c->wobble, i), x, y, scale, scale, first);
    if (w > .001f)
        draw_img(sheet_img(&c->wobble, i + 1), x, y, scale, scale, second);
}

/** A pose of the confirm spin (rotateY in degrees). */
static void coin_spin(Coin *c, float cx, float cy, float deg, float scale, float a) {
    if (!sheet_ok(&c->spin))
        return coin_draw(c, cx, cy, .5f, scale, a);
    coin_shadow(c, cx, cy, .5f, scale, a);
    int i = ((int)roundf(deg / (360.0f / c->spins)) % c->spins + c->spins) % c->spins;
    draw_img(sheet_img(&c->spin, i), cx - c->cx * scale, cy - c->cy * scale, scale, scale, a);
}

// ---- status bar ------------------------------------------------------------------------------------

// ======================================================================================================
// INTRO and theme picker
// ======================================================================================================
static float intro_t0, leave_t, picker_t, ready_t;
static bool intro_leaving, picker, picked, welcome_played;
static int choice;                                      // 0 light, 1 dark
static float opt_scale[2] = {.88f, .88f}, opt_phase[2]; // picker coins: animated scale, wobble phase
static float home_enter_t;
static float music_due = -1;
static void home_enter(bool from_intro);

static void part(int k, float start, float dur, float *a, float *dy) {
    float p = ease_out(span(NOW, intro_t0 + start, dur));
    *a = p;
    *dy = 8 * (1 - p);
    if (intro_leaving) {
        float q = ease_in(span(NOW, leave_t + (7 - k) * .055f, .42f));
        *a *= 1 - q;
        *dy += 8 * q;
    }
    if (picker) {
        float q = ease_in(span(NOW, picker_t, .36f));
        *a *= 1 - q;
        *dy -= 14 * q;
    }
}

/** The intro starts after `delay` seconds (the background fades in from black first). */
static void intro_start(float delay) {
    intro_t0 = NOW + delay;
    intro_leaving = picker = picked = welcome_played = false;
}

static void intro_go(void) {
    if (intro_leaving || NOW - intro_t0 < 1.1f)
        return;
    if (!S.onboarded && !picker) {
        picker = true;
        picker_t = NOW;
        choice = S.dark ? 1 : 0;
        sfx("select");
        return;
    }
    intro_leaving = true;
    leave_t = NOW;
    audio_intro("intro-enter", .35f);
}

static void picker_choose(int c) {
    if (c == choice)
        return;
    choice = c;
    sfx_pitch("move", c ? -3 : 3);
    S.dark = c == 1;
    theme_apply();
}

static void picker_confirm(void) {
    if (picked)
        return;
    picked = true;
    ready_t = NOW;
    S.onboarded = true;
    settings_save();
    sfx("toggle");
}

static void intro_update(void) {
    // Picker coins: the chosen one grows (.theme-option.on) and wobbles faster (3.4 s vs 5.2 s).
    for (int i = 0; i < 2; i++) {
        float target = choice == i ? 1.06f : .88f;
        opt_scale[i] += (target - opt_scale[i]) * (1 - powf(.0004f, DT));
        opt_phase[i] += DT / (choice == i ? 3.4f : 5.2f);
    }
    if (!welcome_played && NOW >= intro_t0) {
        welcome_played = true;
        audio_intro("intro-welcome", .5f);
    }
    if (picker && !picked) {
        if (IN.down & KEY_LEFT)
            picker_choose(0);
        if (IN.down & KEY_RIGHT)
            picker_choose(1);
        if (IN.down & (KEY_A | KEY_START))
            picker_confirm();
        if (IN.touch_up) {
            int c = IN.last_x < BOT_W / 2 ? 0 : 1;
            if (c == choice)
                picker_confirm();
            else
                picker_choose(c);
        }
    } else if (!picker) {
        if (IN.down & (KEY_A | KEY_START) || IN.touch_up)
            intro_go();
    }
    if (picked && NOW - ready_t > .72f && !intro_leaving) {
        intro_leaving = true;
        leave_t = NOW;
        audio_intro("intro-enter", .35f);
    }
    // Everything has faded out after ~0.81 s: the store builds itself up.
    if (intro_leaving && NOW - leave_t > .86f) {
        scene = SC_HOME;
        home_enter(true);
        // The coins are only used here: give their memory back to the banners.
        coin_free(&COIN_EMBLEM);
        coin_free(&COIN_THEME[0]);
        coin_free(&COIN_THEME[1]);
    }
}

static void intro_top(void) {
    draw_bg(true, 1);
    float a, dy;
    // Emblem: rises from 48 px lower after it appears (the site's welcome-emblem-enter).
    float t = NOW - intro_t0;
    float p = span(NOW, intro_t0, 2.6f);
    float op = clampf(p / .3f, 0, 1), sc = lerpf(.82f, 1, ease_out(p / .3f));
    float rise = ease_out(clampf((p - .52f) / .48f, 0, 1));
    float ey = 87 + 48 * (1 - rise);
    float ea = op;
    if (intro_leaving)
        ea *= 1 - ease_in(span(NOW, leave_t + 7 * .055f, .42f));
    if (picker)
        ea *= 1 - ease_in(span(NOW, picker_t, .36f));
    // Halo (scale .6 -> 1 -> 1.5, opacity 0 -> 1 -> 0)
    float hp = span(NOW, intro_t0 + .35f, 1.7f);
    if (hp > 0 && hp < 1) {
        float ha = hp < .35f ? hp / .35f : 1 - (hp - .35f) / .65f;
        float hs = hp < .35f ? lerpf(.6f, 1, hp / .35f) : lerpf(1, 1.5f, (hp - .35f) / .65f);
        C2D_Image halo = ui_img("halo");
        float hw = halo.subtex->width * hs;
        draw_img(halo, 200 + EYE * 2 - hw / 2, ey - hw / 2, hs, hs, ha * ea);
    }
    coin_draw(&COIN_EMBLEM, 200 + EYE * 2, ey, wobble_pose(test_freeze_media?0:t, 5.2f), sc, ea);

    C2D_Image w = ui_img("welcome"), ti = ui_img("title");
    part(1, 1.65f, 1.05f, &a, &dy);
    draw_img_tint(w, 200 - w.subtex->width / 2, 140 + dy, 1, 1, C(TH.eyebrow, a));
    part(2, 1.65f, 1.05f, &a, &dy);
    draw_img_tint(ti, 200 - ti.subtex->width / 2, 151 + dy, 1, 1, C(TH.title, a));

    // Theme picker (.picker-top): title at 27 px, subtitle 4 px below, options 22 px below that.
    if (picker) {
        float pa = ease_out(span(NOW, picker_t + .28f, .7f));
        if (picked)
            pa *= 1 - ease_in(span(NOW, ready_t + .55f, .3f));
        const char *ta = T("picker.titleA"), *tb = T("picker.titleB");
        float wa = text_style_w(ta, 23, 400, 1), wb = text_style_w(tb, 23, 750, 1);
        float tx = 200 - (wa + wb) / 2, ty = 27 + (25.3f - line_h(23)) / 2 + 8 * (1 - pa);
        text_style(ta, tx, ty, 23, 400, 1, C(TH.ink, pa), AL_LEFT);
        text_style(tb, tx + wa, ty, 23, 750, 1, C(TH.ink, pa), AL_LEFT);
        text(T("picker.sub"), 200, 56, 10, C(TH.ink_faint, pa), AL_CENTER);
        for (int i = 0; i < 2; i++) {
            float op2 = ease_back(span(NOW, picker_t + .38f + i * .12f, .76f));
            bool on = choice == i;
            float s = opt_scale[i] * (.4f + .6f * op2);
            // The option (coin, 17 px gap, label) scales around its own centre at y 146.
            float cx = 200 + (i ? 66 : -66), oy = 146 + 26 * (1 - op2);
            float coin_y = oy - 15.65f * s, label_y = oy + 48.5f * s;
            float al = pa * clampf((opt_scale[i] - .88f) / .18f * .4f + .6f, .6f, 1);
            Coin *c = &COIN_THEME[i];
            float k = s / 1.06f; // poses were rendered at the chosen size
            if (picked && on) {
                float q = bezier(span(NOW, ready_t, .72f), .45f, 0, .2f, 1);
                float pop = q < .5f ? 1 + .32f * q : 1.16f - .32f * (q - .5f);
                coin_spin(c, cx + EYE * 3, coin_y, 360 * q, k * pop, al);
            } else {
                coin_draw(c, cx + EYE * 3, coin_y, wobble_pose(test_freeze_media?0:opt_phase[i] * (on ? 3.4f : 5.2f), on ? 3.4f : 5.2f), k, al);
            }
            // Label with the orange dot of the chosen option
            const char *lb = T(i ? "picker.dark" : "picker.light");
            float lw = text_style_w(lb,11,800,0) * s, lx = cx - (lw + 10 * s) / 2;
            float dot = clampf((opt_scale[i] - .88f) / .18f, 0, 1);
            if (dot > .02f)
                circle(lx + 2.5f * s, label_y, 2.5f * s * dot, C(FORGE, pa));
            text_style(lb,lx+10*s,label_y-line_h(11)/2,11,800,0,C(on?TH.ink:TH.ink_soft,al),AL_LEFT);
        }
    }
}

static void marquee_row(int row, float y, float a) {
    // Match IntroScene's catalog.data.apps. Universal-DB must not lengthen the
    // track and make hundreds of icons race past within the same 34/40 seconds.
    int half = (BUILTIN_APPS + 1) / 2;
    int from = row ? half : 0, to = row ? BUILTIN_APPS : half;
    int n = to - from;
    if (n <= 0)
        return;
    float span_w = n * 50.0f, speed = span_w / (row ? 40.0f : 34.0f);
    float off = test_freeze_media?0:fmodf(fmaxf(0, NOW - intro_t0) * speed, span_w);
    if (row)
        off = span_w - off;
    for (int rep = 0; rep < 3; rep++)
        for (int i = 0; i < n; i++) {
            float x = rep * span_w + i * 50 - off;
            if (x < -44 || x > BOT_W)
                continue;
            // Edge fade like the CSS mask
            float edge = fminf(clampf((x + 40) / 52, 0, 1), clampf((BOT_W - x) / 52, 0, 1));
            draw_app_icon(&APPS[from + i], x + 20, y + 20, 40, a * edge);
        }
}

static void intro_bottom(void) {
    draw_bg(false, 1);
    float a, dy;
    // The block (tagline to button) is 180 px tall: 30 px stay free above and below.
    const float oy = 15;
    part(3, 1.7f, .9f, &a, &dy);
    dy += oy;
    text_style(T("intro.tagline"),160,15+dy,11,800,0,C(TH.ink_soft,a),AL_CENTER);
    part(4, 1.85f, .9f, &a, &dy);
    dy += oy;
    float t = NOW - intro_t0;
    const char *st = t < 1.3f ? T("intro.connecting") : t < 2.35f ? T("intro.reading") : NULL;
    char ready[64];
    if (!st) {
        char n[8];
        snprintf(n, sizeof n, "%d", BUILTIN_APPS);
        tvars(ready, sizeof ready, T("intro.ready"), "n", n, NULL, NULL);
        st = ready;
    }
    float tw = text_w(st,10);
    float sx = 160 - (tw + 16) / 2;
    if (t < 2.35f)
        ring(sx + 5.5f, 37 + dy, 5.5f, 1.6f, NOW * 9, NOW * 9 + 4.4f, C(FORGE, a));
    else
        circle(sx + 5.5f, 37 + dy, 5.5f, C(MINT, a)), draw_icon("check", sx + 5.5f, 37 + dy, 8, C(0xffffff, a));
    text(st, sx + 16, 31 + dy, 10, C(TH.ink_faint, a), AL_LEFT);
    part(5, 2.0f, .9f, &a, &dy);
    dy += oy;
    marquee_row(0,63+dy,a);
    marquee_row(1,111+dy,a);
    part(6, 2.55f, .9f, &a, &dy);
    dy += oy;
    // .intro-enter: 30 px tall, padding 0 14 0 16, 16 px gap before the 13 px arrow
    float tw2 = text_style_w(T("intro.enter"),10.5f,800,0), bw = roundf(16 + tw2 + 16 + 13 + 14);
    float bx = roundf(160 - bw / 2), by = 165 + dy;
    rrect(bx - 3, by + 1, bw + 6, 34, 17, C(0x000000, (S.dark ? .1f : .04f) * a)); // box-shadow 0 4px 14px
    rrect(bx - 1, by + 3, bw + 2, 30, 15, C(0x000000, (S.dark ? .14f : .06f) * a));
    rrect(bx, by, bw, 30, 15, C(S.dark ? 0x35383a : 0xffffff, a));
    rrect_outline(bx, by, bw, 30, 15, 1, S.dark ? C(0xffffff, .07f * a) : C(0x456471, .06f * a));
    u32 ec = C(S.dark ? 0xf1f3f4 : 0x3b535c, a);
    text_style(T("intro.enter"),bx+16,by+15-line_h(10.5f)/2,10.5f,800,0,ec,AL_LEFT);
    draw_icon("next", bx + 16 + tw2 + 16 + 6.5f, by + 15, 13, ec);
    if (picker) {
        float pa=ease_out(span(NOW,picker_t+.6f,.6f));
        if(picked) pa*=1-ease_in(span(NOW,ready_t+.55f,.3f));
        text_style(T("picker.touch"),160,112.5f,11,800,0,C(TH.ink_faint,pa),AL_CENTER);
    }
}

// ======================================================================================================
// HOME
// ======================================================================================================
#define COLS 5
#define ROWS 3
#define PER_PAGE (COLS * ROWS)
static App *list[MAX_APPS];
static int nlist, cat = 1, sel, page;
static char query[128];
static bool searching;
static float cur_x = 38, cur_y = 59, track_x, dot_x;
static App *bn_cur, *bn_prev;
static float bn_t;
static int bn_dir;
static UiTabs home_nav, detail_nav;
static float grid_t = -100;
static int grid_dir;
static bool media_started, cache_clear_pending, cache_clear_wait_video;

static void cell_pos(int i, float *x, float *y) {
    int local = i % PER_PAGE, pg = i / PER_PAGE;
    // 60 px cells with a 1 px gap keep resting icons on native whole pixels.
    *x = pg * BOT_W + 8 + (local % COLS) * 61 + 30;
    *y = 37 + (local / COLS) * 52 + 26;
}

static int pages(void) { return nlist ? (nlist + PER_PAGE - 1) / PER_PAGE : 1; }

static void banner_show(App *a, int dir) {
    if (a == bn_cur)
        return;
    if (a && a->universal) {
        char p[96];
        snprintf(p, sizeof p, "romfs:/gfx/universal/%s.ftx", a->id);
        sheet_request(&a->bg, p);
        if(a->icon<BUILTIN_APPS)remote_image_request(json_string_value(json_object_get(json_object_get(a->j,"banner"),"bg")));
    }
    bn_prev = bn_cur;
    bn_cur = a;
    bn_t = NOW;
    bn_dir = dir;
    fx_set(a ? a->fx : NULL);
}

static void home_list(void) {
    if (cat >= NCATS)
        cat = 0;
    if (searching)
        nlist = catalog_search(query, list, MAX_APPS);
    else
        nlist = catalog_filter(CATS[cat].id, list, MAX_APPS);
}

static void home_select(int i, int dir, bool instant) {
    if (!nlist)
        return;
    i = (int)clampf(i, 0, nlist - 1);
    sel = i;
    page = i / PER_PAGE;
    float x, y;
    cell_pos(i, &x, &y);
    if (instant)
        cur_x = x, cur_y = y, track_x = page * BOT_W, dot_x = page * 9;
    banner_show(list[i], dir);
}

static void home_enter(bool from_intro) {
    home_enter_t = from_intro ? NOW + .09f : NOW - 5;
    home_list();
    home_select(sel, 0, true);
    // Web home.onShow starts 400 ms after the 2.2 s arrival completes.
    music_due = from_intro ? NOW + 1.74f : NOW;
    music_muffle(0);
}

static void set_cat(int c) {
    if (c == cat && !searching)
        return;
    searching = false;
    int dir = c > cat ? 1 : -1;
    cat = c;
    grid_t = NOW;
    grid_dir = dir;
    sfx("tab");
    home_list();
    sel = 0;
    home_select(0, dir, true);
}

static void open_detail(void);
static void sd_pump(void);
static void open_downloads(void);
static void open_sd(void);
static void open_github(const char *name, bool user);
static int detail_parent = SC_HOME, install_parent = SC_DETAIL;
static void open_settings(void);
static void open_search(void);
static void open_install(Job *j);

static void home_move(u32 k) {
    if (!nlist)
        return;
    int local = sel % PER_PAGE, pg = sel / PER_PAGE, col = local % COLS, row = local / COLS, next = sel;
    if (k & KEY_LEFT)
        next = col > 0 ? sel - 1 : pg > 0 ? (pg - 1) * PER_PAGE + row * COLS + COLS - 1 : sel;
    if (k & KEY_RIGHT)
        next = col < COLS - 1 ? sel + 1 : pg < pages() - 1 ? (pg + 1) * PER_PAGE + row * COLS : sel;
    if (k & KEY_UP)
        next = row > 0 ? sel - COLS : sel;
    if (k & KEY_DOWN)
        next = row < ROWS - 1 ? sel + COLS : sel;
    next = (int)clampf(next, 0, nlist - 1);
    if (next == sel) {
        sfx_pitch("move", -5);
        return;
    }
    int npg = next / PER_PAGE;
    sfx_pitch("move", (next % COLS) - 2);
    if (npg != page)
        sfx("page");
    home_select(next, next > sel ? 1 : -1, false);
}

static void home_update(void) {
    if (bn_prev && bn_prev->universal && bn_prev != bn_cur && NOW - bn_t > .6f) {
        sheet_free(&bn_prev->bg);
        bn_prev = NULL;
    }
    // Smooth cursor and page slide (spring-like lerps, frame-rate independent).
    float x, y;
    cell_pos(sel, &x, &y);
    float k = 1 - powf(.0004f, DT);
    cur_x = lerpf(cur_x, x, k);
    cur_y = lerpf(cur_y, y, k);
    track_x = lerpf(track_x, page * BOT_W, 1 - powf(.002f, DT));
    dot_x = lerpf(dot_x, page * 9, 1 - powf(.001f, DT));
    if (NOW < home_enter_t + 1.2f)
        return;
    if(!media_started) media_started=media_start();

    if (IN.repeat & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT))
        home_move(IN.repeat);
    if (IN.down & KEY_A)
        open_detail();
    int chosen = tabs_input(&home_nav, true);
    if (chosen >= 0) set_cat(chosen);
    if (IN.down & KEY_Y)
        open_search();
    if (IN.down & KEY_START)
        open_settings();
    if (IN.down & KEY_X)
        open_downloads();
    if (IN.down & KEY_SELECT)
        open_sd();
    // Touch: tabs, grid, toolbar.
    if (IN.touch_up) {
        int dx = IN.last_x - IN.start_x;
        if (IN.start_y >= 30 && IN.start_y < 200 && abs(dx) > 40 && NOW - IN.touch_t0 < .6f) {
            int target = (int)clampf(page + (dx < 0 ? 1 : -1), 0, pages() - 1);
            if (target != page) {
                sfx("page");
                home_select(target * PER_PAGE + sel % PER_PAGE, dx < 0 ? 1 : -1, false);
            }
        }
    }
    for (int i = page * PER_PAGE; i < nlist && i < (page + 1) * PER_PAGE; i++) {
        float cx, cy;
        cell_pos(i, &cx, &cy);
        cx -= page * BOT_W;
        if (hit(cx - 29, cy - 27, 58, 54)) {
            if (i == sel)
                open_detail();
            else
                sfx_pitch("move", (i % COLS) - 2), home_select(i, i > sel ? 1 : -1, false);
        }
    }
    if (hit(4, 208, 69, 28))
        open_search();
    if (hit(76, 208, 80, 28))
        open_downloads();
    if (hit(159, 208, 42, 28))
        open_sd();
    if (hit(204, 208, 81, 28))
        open_settings();
    if (hit(288, 208, 28, 28))
        open_github(json_string_value(json_object_get(json_object_get(CATALOG, "store"), "author")), true);
}

static void banner_draw(App *a, float al, float dx) {
    if (!a || al <= 0)
        return;
    float px = test_freeze_media?0:sinf(NOW / 2.6f) * .7f, py = test_freeze_media?0:cosf(NOW / 3.4f) * .45f;
#define LX(d) (px * (d) * 8.4f + EYE * (d) * 3 + dx * (d))
#define LY(d) (py * (d) * 4.2f)
    if(sheet_ok(&a->bg)) {
        float burn=test_freeze_media?0:fmodf(fmaxf(0,NOW-bn_t)/18.f,2);if(burn>1)burn=2-burn;
        burn=bezier(burn,.42f,0,.58f,1);
        float scale=1.14f-.11f*burn;
        float bx=(-.016f+.032f*burn)*432*scale,by=(-.01f+.02f*burn)*272*scale;
        C2D_Image bg=sheet_img(&a->bg,0);
        draw_img(bg,200-bg.subtex->width*scale/2+bx+LX(-.7f),120-bg.subtex->height*scale/2+by+LY(-.7f),scale,scale,al);
    } else {
        rect(0,0,TOP_W,SCR_H,C(a->color,al));
        if(a->universal&&a->icon<BUILTIN_APPS){const char *url=json_string_value(json_object_get(json_object_get(a->j,"banner"),"bg"));int w,h;if(remote_image_ready(url)&&remote_image_dimensions(url,&w,&h)){float scale=fmaxf(400.f/w,240.f/h)*1.14f;remote_image_draw(url,200-w*scale/2+LX(-.7f),120-h*scale/2+LY(-.7f),w*scale,h*scale,al);}}
    }
    fx_draw(LX(-.2f),al);
    if(a->universal){
        float ty=76,tx=200+EYE*3+dx;
        text_ex(a->title,tx,ty+3,30,800,FONT_BRAND,-1.2f,C(0,.35f*al),AL_CENTER);
        text_ex(a->title,tx,ty,30,800,FONT_BRAND,-1.2f,C(0xffffff,al),AL_CENTER);
    }
    if(sheet_ok(&a->fg)) {
        float mid=a==bn_cur?bezier(span(NOW,bn_t+.07f,.62f),.22f,1,.36f,1):1;
        if(a->mid.i>=0) {
            C2D_Image im=sheet_img(&a->fg,a->mid.i); float k=1+.04f*(1-mid);
            draw_img(im,a->mid.x+(EYE*3+dx)*.45f+bn_dir*26*1.8f*(1-mid)-(k-1)*im.subtex->width/2,a->mid.y-(k-1)*im.subtex->height/2,k,k,al*mid);
        }
        if(a->logo.i>=0) {
            float logo=a==bn_cur?bezier(span(NOW,bn_t+.14f,.62f),.34f,1.4f,.64f,1):1;
            C2D_Image im=sheet_img(&a->fg,a->logo.i);float k=.9f+.1f*logo;
            // Foreground stays still at rest; retain navigation and stereo depth.
            draw_img(im,a->logo.x+EYE*3+dx+bn_dir*26*3*(1-logo)-(k-1)*im.subtex->width/2,a->logo.y-(k-1)*im.subtex->height/2,k,k,al*fminf(1,logo));
        }
    }
#undef LX
#undef LY
}

static void home_card(App *a, float al, float dy) {
    float x=8+EYE*1.2f,y=164+dy,w=384;
    glass(x,y,w,68,12,al);
    icon_at(a,x+8,y+14,40,1,al);
    text_fit(a->title,x+57,y+8,14,212,C(TH.ink,al));
    char sub[160],by[96],stars[24];
    tvars(by,sizeof by,T("home.by"),"author",a->author,NULL,NULL);
    snprintf(sub,sizeof sub,a->platform?"%s · %s → 3DS":"%s",by,a->platform);
    text_fit(sub,x+57,y+27,10,212,C(TH.ink_soft,al));
    float cx=x+57,cy=y+44;
    json_t *rel=app_release(a,S.experimental);
    scissor(TOP_W,cx,cy,212,16);
    if(rel) cx+=chip(cx,cy,json_string_value(json_object_get(rel,"tag")),NULL,C(TH.mint,1),C(TH.mint,.16f),al)+3;
    if(a->needs_rom || a->needs_data) cx+=chip(cx,cy,T(a->needs_rom?"chip.rom":"chip.data"),a->needs_rom?"rom":"file",C(TH.ink_soft,1),C(TH.surface2,1),al)+3;
    if(!a->old_ok) cx+=chip(cx,cy,T("chip.n3dsOnly"),NULL,C(TH.ink_soft,1),C(TH.surface2,1),al)+3;
    if(a->stereo) chip(cx,cy,T("chip.3d"),"depth",C(TH.ink_soft,1),C(TH.surface2,1),al);
    scissor_off();
    if(a->stars) {
        fmt_stars(stars,sizeof stars,a->stars);
        text(stars,x+w-8,y+12,10,C(S.dark?0xffc94a:0xc98a00,al),AL_RIGHT);
        draw_icon("starFill",x+w-14-text_w(stars,10),y+19,9,C(S.dark?0xffc94a:0xc98a00,al));
    }
    AppStatus st=app_status_view(a); char label[96];
    if(st==ST_BUSY || st==ST_DEV) {
        Job *j=st==ST_BUSY?install_job(a):NULL;
        if(j && j->state==JOB_QUEUED) {
            fit_label(label,sizeof label,T("status.queued"),9,84);
            chip(x+w-10-text_w(label,9)-22,y+40,label,"clock",C(TH.ink_soft,1),C(TH.surface2,1),al);
        } else {
            float p=st==ST_DEV?a->progress/100.f:j?job_progress(j):0;
            snprintf(label,sizeof label,"%d %%",(int)(p*100));
            text(label,x+w-10,y+33,10,C(TH.ink,al),AL_RIGHT);
            rrect(x+w-90,y+53,80,5,2,C(TH.surface3,al));
            rrect(x+w-90,y+53,fmaxf(1,80*p),5,2,C(st==ST_DEV?LAV:FORGE,al));
        }
    } else {
        const char *ic="download"; u32 fg=FORGE;
        if(st==ST_INSTALLED || st==ST_UPDATE || st==ST_NEEDS_FILE) {
            snprintf(label,sizeof label,"%s",T(st==ST_INSTALLED?"status.installed":st==ST_UPDATE?"status.update":"status.needsFile"));
            ic=st==ST_INSTALLED?"check":st==ST_UPDATE?"update":"warn"; fg=st==ST_INSTALLED?TH.mint:st==ST_UPDATE?FORGE:AMBER;
        } else fmt_size(label,sizeof label,app_download_size(a,S.cia,S.experimental));
        char fitted[96]; fit_label(fitted,sizeof fitted,label,9,88);
        chip(x+w-8-text_w(fitted,9)-22,y+39,fitted,ic,C(fg,1),C(fg,.16f),al);
    }
}

static float arrive(float delay, float *dy) {
    float p = ease_out(span(NOW, home_enter_t + delay, .7f));
    if (dy)
        *dy = 8 * (1 - p);
    return p;
}

static void home_top(float sa) {
    float in = arrive(0, NULL);
    float p = ease_out(span(NOW, bn_t, .55f));
    float dir = bn_dir * 26;
    draw_bg(true, sa);
    if (bn_prev && p < 1)
        banner_draw(bn_prev, sa * in, -dir * p);
    banner_draw(bn_cur, sa * in * (bn_prev ? p : 1), dir * (1 - p));
    if (!bn_cur)
        draw_bg(true, sa);
    float dy;
    float ca = arrive(.16f, &dy);
    if (bn_cur)
        home_card(bn_cur, sa * ca, dy * 2);
}

static void home_tabs(float al) {
    const char *labels[UI_TAB_MAX], *icons[UI_TAB_MAX];
    for (int i=0;i<NCATS && i<UI_TAB_MAX;i++) { labels[i]=LJ(CATS[i].label); icons[i]=CATS[i].icon; }
    tabs_layout(&home_nav,4,4,312,labels,icons,NCATS,cat);
    tabs_draw(&home_nav,labels,icons,true,al);
}

static void home_bottom(float sa) {
    draw_bg(false, 1);
    float dy, a;
    a = arrive(.06f, &dy) * sa;
    home_tabs(a);
    // Grid (clipped sideways so pages slide in and out)
    scissor(BOT_W, 0, 36, BOT_W, 158);
    int first = (int)(track_x / BOT_W) * PER_PAGE;
    for (int i = (first > 0 ? first - PER_PAGE : 0); i < nlist && i < first + 2 * PER_PAGE; i++) {
        float cx, cy;
        cell_pos(i, &cx, &cy);
        cx -= track_x;
        if (cx < -40 || cx > BOT_W + 40)
            continue;
        float ia = arrive(.14f + (i % PER_PAGE) * .035f, &dy) * sa;
        float gp = bezier(span(NOW, grid_t, UI_TAB_SECONDS), .22f, 1, .36f, 1);
        cx += grid_dir * 12 * (1 - gp);
        ia *= gp;
        bool on = i == sel;
        App *ap = list[i];
        // Icons stay at their native 44 px; the selected one gets a soft shadow.
        if (on)
            rrect(cx - 22, cy - 20 + dy, 44, 44, 10, C(0x000000, .18f * ia));
        draw_app_icon(ap, cx, cy + dy, 44, ia);
        if (install_busy(ap)) {
            Job *j = install_job(ap);
            circle(cx, cy + dy, 11, C(0x0c1014, .62f * ia));
            if(j && j->state==JOB_QUEUED) draw_icon("clock",cx,cy+dy,12,C(0xffffff,ia));
            else ring(cx, cy + dy, 8.8f, 2.6f, 0, 2 * M_PI, C(0xffffff, .28f * ia));
            float p = j ? job_progress(j) : 0;
            if (p > .01f)
                ring(cx, cy + dy, 8.8f, 2.6f, -M_PI / 2, -M_PI / 2 + 2 * M_PI * p, C(0xff9a3c, ia));
        }
    }
    // Cursor: concentric with the icon, soft orange glow
    float ca = arrive(.56f, NULL) * sa;
    float cx=cur_x-track_x,cy=cur_y;
    float reveal=bezier(span(NOW,grid_t,UI_TAB_SECONDS),.22f,1,.36f,1);
    cx+=grid_dir*12*(1-reveal); ca*=reveal;
    // 44px icon + 2px clear gap + 2px inward stroke on every side.
    if (nlist) rrect_outline(roundf(cx)-26, roundf(cy)-26, 52, 52, 14, 2, C(S.dark ? 0xffe9c9 : 0xffffff, ca));
    // All badges sit above the cursor, with the same motion as their icons.
    for (int i = (first > 0 ? first - PER_PAGE : 0); i < nlist && i < first + 2 * PER_PAGE; i++) {
        float bx, by;
        cell_pos(i, &bx, &by);
        bx -= track_x;
        if (bx < -40 || bx > BOT_W + 40) continue;
        float ia = arrive(.14f + (i % PER_PAGE) * .035f, &dy) * sa;
        bx += grid_dir * 12 * (1 - reveal);
        ia *= reveal;
        if (list[i]->dev) {
            circle(bx - 20, by + 20 + dy, 7.5f, C(TH.bg, ia));
            circle(bx - 20, by + 20 + dy, 6, C(LAV, ia));
            draw_icon("hammer", bx - 20, by + 20 + dy, 9, C(0xffffff, ia));
        }
        status_icon(list[i], bx + 20, by + 20 + dy, ia);
    }
    scissor_off();
    if (!nlist)
        text(T("home.empty"), 160, 110, 10, C(TH.ink_faint, sa), AL_CENTER);
    // Page dots: grey dots plus one orange dot that slides between them
    int np = pages();
    if (np > 1) {
        float da = arrive(.6f, &dy) * sa;
        float w = np * 9 - 4, x0 = 160 - w / 2;
        for (int p = 0; p < np; p++)
            circle(x0 + p * 9 + 2.5f, 202 + dy, 2.5f, C(TH.line, .22f * da));
        circle(x0 + dot_x + 2.5f, 202 + dy, 2.5f, C(FORGE, da));
    }
    // Toolbar
    float ta = arrive(.64f, &dy) * sa;
    const float xs[] = {4,76,159,204,288}, ws[] = {69,80,42,81,28};
    const char *labels[] = {T("home.search"),T("home.downloads"),T("home.sd"),T("home.settings"),NULL};
    const char *icons[] = {"search","download","sd","settings","github"};
    for (int i=0;i<5;i++) button(xs[i],208+dy,ws[i],28,labels[i],icons[i],NULL,BTN_SURFACE,ta);
    if (scenes_downloading()) circle(150,211+dy,3,C(FORGE,ta));
}

// ======================================================================================================
// DETAIL
// ======================================================================================================
static App *dapp;
static Sheet video_posters, video_bottom_posters;
static bool video_wanted;
static char wanted_video[49];
static char poster_path[96];
static int nvideo;
static float thumb_shift, video_ui_until, video_overlay_alpha=1, video_seek_at=-1;
static bool video_reveal_touch,video_ui_on=true;
static float video_ui_changed,video_ui_from=1;
static void video_ui_show(bool on){
    if(on)video_ui_until=NOW+3.2f;
    if(on!=video_ui_on){video_ui_from=video_overlay_alpha;video_ui_changed=NOW;video_ui_on=on;}
}
static int dtab, shot_i, shot_prev = -1, news_open;
static float news_rows[16];
static int news_count;
static struct { float x,y,w; const char *url; json_t *check; } requirement_tools[32];
static int requirement_tool_count;
static void gh_open_url(const char *url);
static float detail_format_y=-1000,detail_channel_y=-1000,detail_format_x=10,detail_format_w=300,detail_channel_x=10,detail_channel_w=300;
static int detail_option_focus=-1;
static float shot_t, dscroll, dscroll_target, content_h;
static bool sheet_on, sheet_cia, sheet_exp, sheet_saved_cia, sheet_saved_exp, uninstall_on;
static int sheet_focus;
static float sheet_t;
static u64 sd_free;
// Requirement filesystem checks are scheduled in update, never during three stereo draws.
#include "install_plan.h"
static InstallPlan detail_plan;
static App *detail_plan_app;
static bool detail_plan_cia,detail_plan_exp;
static FileView detail_plan_views[PLAN_MAX];
static void detail_plan_update(void) {
    if(detail_plan_app!=dapp || detail_plan_cia!=sheet_cia || detail_plan_exp!=sheet_exp) {
        install_plan_build(&detail_plan,app_install_steps(dapp,sheet_cia),json_object_get(app_release(dapp,sheet_exp),"assets"),json_object_get(dapp->j,"requires"),!IS_NEW3DS);
        detail_plan_app=dapp;detail_plan_cia=sheet_cia;detail_plan_exp=sheet_exp;
        file_view_plan_begin();
        memset(detail_plan_views,0,sizeof detail_plan_views);
        for(int i=0;i<detail_plan.count;i++) {
            char path[1024];
            if(catalog_sd_path(detail_plan.rows[i].path,path,sizeof path))detail_plan_views[i]=file_view_watch(path,false);
        }
    }
    for(int i=0;i<detail_plan.count;i++)detail_plan.rows[i].exists=file_view_exists(detail_plan_views[i]);
}
static bool detail_requirement_has(json_t *req) { return requirement_view_has(req); }
static const char *TABS_KEYS[4] = {"detail.tab.info", "detail.tab.shots", "detail.tab.install", "detail.tab.news"};

static void sd_space(void) {
    FS_ArchiveResource r;
    if (R_SUCCEEDED(FSUSER_GetArchiveResource(&r, SYSTEM_MEDIATYPE_SD)))
        sd_free = (u64)r.freeClusters * r.clusterSize;
}

static void open_detail(void) {
    if (!nlist)
        return;
    sfx("select");
    if (dapp && dapp != list[sel])
        app_free_shots(dapp);
    dapp = list[sel];
    rom_find_refresh();  // the ROM may have been copied since the last look
    detail_plan_app=NULL;requirement_check_app=NULL;
    detail_parent = scene;
    dtab = 0;
    nvideo = json_array_size(json_object_get(dapp->j, "videos"));
    sheet_free(&video_posters);
    sheet_free(&video_bottom_posters);
    video_wanted = false;
    snprintf(poster_path, sizeof poster_path, "romfs:/video/%s-posters.ftx", dapp->id);
    if (dapp->universal) {
        char path[96];
        snprintf(path, sizeof path, "romfs:/gfx/universal/%s.ftx", dapp->id);
        sheet_request(&dapp->bg, path);
        if(dapp->icon<BUILTIN_APPS)remote_image_request(json_string_value(json_object_get(json_object_get(dapp->j,"banner"),"bg")));
    }
    thumb_shift = 0;
    shot_i = 0;
    shot_prev = -1;
    shot_t = NOW;
    dscroll = dscroll_target = 0;
    news_open = 0; requirement_tool_count=0;
    sheet_on=false; uninstall_on=false;
    sheet_cia = S.cia && app_has_format(dapp, true) ? true : !app_has_format(dapp, false);
    sheet_exp = S.experimental && app_has_experimental(dapp);
    app_request_shots(dapp);
    sd_space();
    next_scene = SC_DETAIL;
    trans_phase = 1;
    trans_t = NOW;
}

static C2D_Image shot_img(int i, bool bottom, bool *ok) {
    int b = i;
    if (bottom) { // bottom shots are packed without gaps
        b = 0;
        for (int k = 0; k < i; k++)
            b += dapp->shot_bottom[k];
    }
    Sheet *s = bottom ? &dapp->shots_b[b / 4] : &dapp->shots_t[i / 4];
    *ok = sheet_ok(s);
    return sheet_img(s, b % 4);
}

static void detail_next_shot(int d) {
    int nv = nvideo, total = dapp->shots + nv;
    if (total < 2)
        return;
    shot_prev = shot_i;
    shot_i = (shot_i + nv + d + total) % total - nv;
    shot_t = NOW;
}

static void open_sheet(void) {
    if(json_is_true(json_object_get(dapp->j,"nativeUnsupported"))) { if(dapp->repo) open_github(dapp->repo,false); else toast("Universal-Updater",AMBER); return; }
    sheet_saved_cia = sheet_cia; sheet_saved_exp = sheet_exp;
    sheet_on = true;
    sheet_t = NOW;
    sheet_focus = 0;
    sfx("open");
}

// Opening a game fades the store (picture and music) to black before the process leaves.
static float launch_t = -1;
bool scenes_wants_exit(void) { return launch_t >= 0 && NOW - launch_t >= .95f; }
float scenes_exit_fade(void) { return launch_t < 0 ? 0 : ease_inout(span(NOW, launch_t + .08f, .72f)); }
static void launch_begin(void) {
    launch_t = NOW;
    sfx("launch");
    music_stop(.7f);
    video_stop();
}
static void launch_app(App *app) {
    if(install_active()) { toast(T("status.downloading"),AMBER); return; }
    json_t *record=installed_record(app),*file; size_t i;
    json_array_foreach(json_object_get(record,"files"),i,file) {
        const char *path=json_string_value(file);
        if(!path || strncmp(path,"title:",6) || strlen(path)!=22) continue;
        char *end; u64 id=strtoull(path+6,&end,16);
        if(*end || (id>>32)!=0x00040000) continue;
        AM_TitleEntry entry;
        Result rc=AM_GetTitleInfo(MEDIATYPE_SD,1,&id,&entry);
        if(R_SUCCEEDED(rc)) { aptSetChainloader(id,MEDIATYPE_SD); launch_begin(); return; }
    }
    char path[1024];
    if(app_launch_path(app,path,sizeof path)) {
        HblLaunchResult result=hbl_launch(path);
        if(result==HBL_LAUNCH_OK){launch_begin();return;}
        char message[112];snprintf(message,sizeof message,"Homebrew Launcher · %.86s",path);toast(message,AMBER);
    }
    else toast(T("detail.homeMenu"),AMBER);
}

static void close_sheet(void) { sheet_on=false; sheet_cia=sheet_saved_cia; sheet_exp=sheet_saved_exp; }

static bool sheet_compatible(void) {
    json_t *rel=app_release(dapp,sheet_exp), *old=json_object_get(rel,"o3ds");
    return !json_is_true(json_object_get(dapp->j,"nativeUnsupported")) && (IS_NEW3DS || json_is_true(old) || (!json_is_false(old) && dapp->old_ok));
}
static float sheet_format_y(void) { return 54+(app_has_experimental(dapp)?88:0); }

static void start_install(void) {
    if (!sheet_compatible()) { sfx("error"); toast(T("confirm.blockedNew"),DANGER); return; }
    Job *j = install_start(dapp, sheet_cia, sheet_exp);
    sheet_on = false;
    if (!j)
        return;
    sfx("download");
    open_install(j);
}

static json_t *selected_video(void) { return json_array_get(json_object_get(dapp->j, "videos"), shot_i + nvideo); }
static void play_video(void) {
    json_t *v = selected_video();
    const char *id = json_string_value(json_object_get(v, "id"));
    if (!id) return;
    char path[256];
    if (media_ready_path(dapp->id, id, path, sizeof path)) {
        video_wanted = false;
        // The selected video's poster remains available while the decoder
        // starts. Screenshot atlases are invisible and must not compete with
        // its frame banks for Old 3DS linear memory.
        app_free_shots(dapp);
        video_ui_until=NOW+3.2f;video_ui_on=true;video_reveal_touch=false;video_overlay_alpha=video_ui_from=1;video_ui_changed=NOW-1;
        if (!video_start(path)) toast(video_error(), DANGER);
    } else {
        snprintf(wanted_video, sizeof wanted_video, "%s", id);
        video_wanted = true;
        media_prioritize(dapp->id, id);
        MediaItem item; int i = media_find(dapp->id, id);
        if (i >= 0 && media_item(i, &item) && (item.state == MEDIA_ERROR || item.state == MEDIA_CLEARED)) {
            video_wanted = false;
            toast(item.state == MEDIA_ERROR ? item.error : T("media.cleared"), AMBER);
        } else toast(T("media.downloading"), MINT);
    }
}

static void detail_update(void) {
    detail_plan_update();
    if (video_wanted) {
        const char *id = shot_i < 0 ? json_string_value(json_object_get(selected_video(), "id")) : NULL;
        if (!id || strcmp(id, wanted_video)) video_wanted = false;
        else {
            char path[256];
            if (media_ready_path(dapp->id, id, path, sizeof path)) play_video();
            else { MediaItem item; int i = media_find(dapp->id, id);
                if (i >= 0 && media_item(i, &item) && (item.state == MEDIA_ERROR || item.state == MEDIA_CLEARED)) video_wanted = false;
            }
        }
    }
    if (video_active()) {
        bool was_paused=video_paused();
        video_update();
        if(!was_paused&&video_paused())video_ui_show(true);
        if(video_ui_on&&!video_paused()&&NOW>=video_ui_until)video_ui_show(false);
        video_overlay_alpha=lerpf(video_ui_from,video_ui_on?1:0,bezier(span(NOW,video_ui_changed,.6f),.22f,1,.36f,1));
        bool ui=!video_bottom()||video_ui_on,touched=false;
        if (IN.touch_down)
            video_reveal_touch = !ui;
        if (IN.touch_up && (video_reveal_touch || !ui)) {
            video_reveal_touch = false;
            video_ui_show(true);
            return;
        }
        if (IN.down & KEY_B || (ui && hit(12, 12, 42, 26))) {
            video_stop();
            sfx("back");
            return;
        }
        if(IN.down)video_ui_show(true);
        if (IN.down & (KEY_A | KEY_START) || (ui && hit(126, 74, 68, 68))) {
            video_pause();
            sfx("toggle");
            touched=true;
        }
        if (IN.down & KEY_LEFT || (ui && hit(50, 86, 44, 44)))
            {video_seek(video_time()-5);touched=true;}
        if (IN.down & KEY_RIGHT || (ui && hit(226, 86, 44, 44)))
            {video_seek(video_time()+5);touched=true;}
        if (IN.down & KEY_Y || (ui && hit(266, 12, 42, 26))) {
            video_toggle_mute();
            sfx("toggle");
            touched=true;
        }
        if(ui && (IN.touch_up || (IN.touching && NOW-video_seek_at>.08f)) && IN.start_y>=176 && IN.start_y<200) {
            video_seek_at=NOW;video_seek(video_duration()*clampf((IN.tx-24)/272.f,0,1));video_ui_show(true);touched=true;
        }
        if(IN.touch_up&&!IN.dragged){if(touched)video_ui_show(true);else video_ui_show(!video_ui_on);}
        return;
    }
    if (!test_freeze_media && NOW - shot_t > 4.8f && shot_i >= 0 && dapp->shots > 1) {
        shot_prev=shot_i; shot_i=(shot_i+1)%dapp->shots; shot_t=NOW;
    }
    dscroll = lerpf(dscroll, dscroll_target, 1 - powf(.0005f, DT));
    if(uninstall_on) {
        if(IN.down&KEY_B || hit(16,148,130,28)) { uninstall_on=false; sfx("back"); }
        else if(IN.down&KEY_A || hit(152,148,152,28)) {
            Job *job=install_uninstall(dapp); uninstall_on=false;
            if(job) open_install(job);
        }
        return;
    }
    if (sheet_on) {
        int opts = (app_has_experimental(dapp) ? 1 : 0) + (app_has_format(dapp, true) && app_has_format(dapp, false) ? 1 : 0);
        if (IN.down & KEY_B)
            close_sheet(), sfx("back");
        else if (IN.down & KEY_A)
            start_install();
        else if (IN.repeat & (KEY_UP | KEY_DOWN) && opts > 1)
            sheet_focus = (int)clampf(sheet_focus + (IN.repeat & KEY_DOWN ? 1 : -1), 0, opts-1), sfx("move");
        else if (IN.down & (KEY_LEFT | KEY_RIGHT | KEY_L | KEY_R)) {
            int f = app_has_experimental(dapp) ? sheet_focus : 1;
            if (f == 0)
                sheet_exp = (IN.down & (KEY_RIGHT | KEY_R)) != 0;
            else if (app_has_format(dapp, true) && app_has_format(dapp, false))
                sheet_cia = (IN.down & (KEY_LEFT | KEY_L)) != 0;
            sfx("toggle");
        }
        float cy = 54;
        if (app_has_experimental(dapp)) {
            for (int c = 0; c < 2; c++)
                if (hit(16,cy+c*44,288,38)) {
                    sheet_exp = c == 1;
                    sheet_focus = 0;
                    sfx("toggle");
                }
            cy += 88;
        }
        if (app_has_format(dapp, true) && app_has_format(dapp, false)) {
            if (hit(72,cy,116,26))
                sheet_cia = true, sfx("toggle");
            if (hit(188,cy,116,26))
                sheet_cia = false, sfx("toggle");
        }
        if (hit(0,0,6,240))
            close_sheet();
        if (hit(CONFIRM_INSTALL_X,196,CONFIRM_INSTALL_W,28))
            start_install();
        if (hit(16,196,CONFIRM_CANCEL_W,28))
            close_sheet();
        return;
    }
    if (IN.down & KEY_B) {
        sfx("back");
        next_scene = detail_parent;
        trans_phase = 1;
        trans_t = NOW;
        return;
    }
    int tab = tabs_input(&detail_nav, false);
    if (tab >= 0 && tab != dtab) { dtab=tab; dscroll=dscroll_target=0; sfx("tab"); }
    if(dtab==2 && IN.down & KEY_SELECT) { detail_option_focus=(detail_option_focus+1)%2; dscroll=dscroll_target=0; }
    if(dtab==2 && detail_option_focus>=0 && IN.repeat & (KEY_LEFT|KEY_RIGHT)) {
        if(detail_option_focus==0 && app_has_format(dapp,true) && app_has_format(dapp,false)) sheet_cia=(IN.repeat&KEY_LEFT)!=0;
        if(detail_option_focus==1 && app_has_experimental(dapp)) sheet_exp=(IN.repeat&KEY_RIGHT)!=0;
        sfx("toggle");
    } else if (IN.repeat & (KEY_LEFT | KEY_RIGHT))
        detail_next_shot(IN.repeat & KEY_RIGHT ? 1 : -1), sfx_pitch("move", 2);
    if (IN.repeat & (KEY_UP | KEY_DOWN)) {
        if (dtab == 3)
            { news_open = (int)clampf(news_open + (IN.repeat & KEY_DOWN ? 1 : -1), 0, news_count-1);
              if (news_count) dscroll_target=fmaxf(0,dscroll+news_rows[news_open]-41);
              sfx("tab"); }
        else
            dscroll_target += IN.repeat & KEY_DOWN ? 34 : -34;
    }
    if (IN.touching && !IN.touch_down && IN.start_y > 30 && IN.start_y < 204)
        dscroll_target -= (IN.ty - IN.last_y), dscroll = dscroll_target;
    if(dtab==1) dscroll=dscroll_target=0;
    dscroll_target = clampf(dscroll_target, 0, fmaxf(0, content_h - 159));
    if (hit(4, 4, 28, 28)) {
        sfx("back");
        next_scene = detail_parent;
        trans_phase = 1;
        trans_t = NOW;
        return;
    }
    AppStatus st = app_status_view(dapp);
    bool removable=st==ST_INSTALLED || st==ST_NEEDS_FILE || st==ST_UPDATE;
    bool tap_action=hit(5,205,removable?250:280,28);
    if(removable && (IN.down&KEY_Y || hit(259,205,26,28))) { uninstall_on=true; return; }
    if (dtab == 3)
        for (int i = 0; i < news_count; i++) {
            if (news_rows[i] >= 30 && news_rows[i] < 182 && hit(10,news_rows[i],300,40)) {
                news_open = news_open == i ? -1 : i;
                sfx("tab");
            }
        }
    if (dtab == 1) {
        int total = nvideo + dapp->shots, selected = shot_i + nvideo;
        thumb_shift = clampf(selected * 50 + 23 - 150, 0, fmaxf(0, total * 50 - 304));
        for (int i = 0; i < total; i++)
            if (hit(10 + i * 50 - thumb_shift, 44,46,28)) {
                shot_prev = shot_i;
                shot_i = i - nvideo;
                shot_t = NOW;
                sfx("move");
            }
        if (shot_i < 0 && (IN.down & KEY_A || hit(195,125,60,28) || hit(10,82,176,110))) {
            play_video();
            return;
        }
        if (hit(196,186,30,14))
            detail_next_shot(-1);
        if (hit(230,186,30,14))
            detail_next_shot(1);
    }
    if(dtab==2) {
        if(detail_format_y>=36 && detail_format_y+26<=200) {
            if(hit(detail_format_x,detail_format_y,detail_format_w/2,26)) { sheet_cia=true; sfx("toggle"); }
            if(hit(detail_format_x+detail_format_w/2,detail_format_y,detail_format_w/2,26)) { sheet_cia=false; sfx("toggle"); }
        }
        if(detail_channel_y>=36 && detail_channel_y+26<=200) {
            if(hit(detail_channel_x,detail_channel_y,detail_channel_w/2,26)) { sheet_exp=false; sfx("toggle"); }
            if(hit(detail_channel_x+detail_channel_w/2,detail_channel_y,detail_channel_w/2,26)) { sheet_exp=true; sfx("toggle"); }
        }
    }
    if(dtab==2) for(int i=0;i<requirement_tool_count;i++) {
        float y=requirement_tools[i].y;
        if(y>=36 && y+24<=200 && hit(requirement_tools[i].x,y,requirement_tools[i].w,24)) {
            if(requirement_tools[i].check){requirement_check_start(dapp,requirement_tools[i].check,false);return;}
            const char *url=requirement_tools[i].url,*start=url?strstr(url,"github.com/"):NULL;
            if(start) { char repo[128]; snprintf(repo,sizeof repo,"%s",start+11); char *slash=strchr(repo,'/');
                if(slash) { char *end=strchr(slash+1,'/'); if(end) *end=0; open_github(repo,false); return; }
            }
            if(url)gh_open_url(url);
        }
    }
    if ((IN.down & KEY_A && !(dtab == 1 && shot_i < 0)) || tap_action) {
        if(st==ST_NEEDS_FILE){if(dtab!=2)sfx("tab");dtab=2;dscroll=dscroll_target=0;toast(T("toast.needsFile"),AMBER);sfx("warn");}
        else if(st==ST_INSTALLED) launch_app(dapp);
        else if (st == ST_AVAILABLE || st == ST_UPDATE) {
            if (app_release(dapp, false))
                open_sheet();
        } else if (st == ST_BUSY)
            open_install(install_job(dapp));
        else
            sfx("error");
    }
    if (IN.down & KEY_X || hit(288, 208, 28, 28)) {
        if (st == ST_BUSY)
            install_cancel(install_job(dapp));
        else if (dapp->repo)
            open_github(dapp->repo, false);
    }
}

// Captures tab: the top screen keeps only the picture and its counter. 0 = full header,
// 1 = clean; eased every frame in scenes_update so entering and leaving are transitions.
static float shots_clean;
/** Larger counter of the clean view: 11 px label in a 22 px pill. */
static float chip_big(float x, float y, const char *label, float a) {
    float w = ceilf(text_style_w(label, 11, 700, 0)) + 20;
    rrect(x - w, y, w, 22, 11, C(0x000000, .5f * a));
    text_style(label, x - w + 10, y + 11 - line_h(11) / 2, 11, 700, 0, C(0xffffff, a), AL_LEFT);
    return w;
}
static void detail_top_overlay(float sa) {
    float clean = video_active() ? 0 : ease_inout(shots_clean), full = sa;
    sa *= 1 - clean;  // header, shades and the small counter leave together
    // Shade for the header text
    vgrad(0, 0, TOP_W, 48, C(0, .3f * sa), C(0, 0));
    vgrad(0, 100, TOP_W, 140, C(0x060a0e, 0), C(0x060a0e, .85f * sa));
    // Same wrapping and bottom anchoring as .dt-head/.dt-chips.
    struct HeadChip {char label[96];const char*icon;u32 bg,fg;float x,y;} chips[8];int nc=0;
    AppStatus st=app_status_view(dapp);json_t *rel=app_release(dapp,sheet_exp);
#define HEAD_CHIP(label_,icon_,bg_) do{if(nc<8){snprintf(chips[nc].label,sizeof chips[nc].label,"%s",label_);chips[nc].icon=icon_;chips[nc].fg=C(0xffffff,1);chips[nc++].bg=bg_;}}while(0)
    char tmp[96];
    if(st==ST_INSTALLED){HEAD_CHIP(T("status.installed"),"check",C(S.dark?TH.mint:0xdcefe7,S.dark?.16f:1));chips[nc-1].fg=C(S.dark?0x8fdcc0:0x2f7a63,1);}
    if(st==ST_NEEDS_FILE){HEAD_CHIP(T("status.needsFile"),"warn",C(S.dark?AMBER:0xfff4d6,S.dark?.18f:1));chips[nc-1].fg=C(S.dark?0xffcc5c:0xa86f00,1);}
    if(st==ST_DEV){snprintf(tmp,sizeof tmp,"%s · %d %%",T("status.dev"),dapp->progress);HEAD_CHIP(tmp,"hammer",C(S.dark?0x9d92f0:0xece8ff,S.dark?.16f:1));chips[nc-1].fg=C(S.dark?0x9d92f0:LAV,1);}
    if(rel){snprintf(tmp,sizeof tmp,"%s · %s",json_string_value(json_object_get(rel,"tag")),T(sheet_exp?"channel.experimental":"channel.stable"));HEAD_CHIP(tmp,sheet_exp?"sparkle":"shield",C(0,.45f));}
    if(dapp->has_stars){fmt_stars(tmp,sizeof tmp,dapp->stars);HEAD_CHIP(tmp,"star",C(0,.45f));}
    if(!dapp->old_ok)HEAD_CHIP(T("chip.n3dsOnly"),NULL,C(0,.45f));
    if(dapp->stereo)HEAD_CHIP(json_is_string(json_object_get(json_object_get(dapp->j,"compat"),"stereo"))?"3D β":"3D","depth",C(0,.45f));
    if(dapp->needs_rom)HEAD_CHIP(T("chip.rom"),"rom",C(0,.45f));
#undef HEAD_CHIP
    float xx=0;int row=0;
    for(int i=0;i<nc;i++){float cw=chip_width(chips[i].label,chips[i].icon);if(xx&&xx+cw>324){row++;xx=0;}chips[i].x=xx;chips[i].y=row*19;xx+=cw+3;}
    float ch=nc?fminf(36,16+row*19):0,hy=230-(18+1+14+(nc?4+ch:0)),hx=10+EYE*2;
    float saved_draw_y=DRAW_Y;if(video_active())DRAW_Y+=8*(1-video_overlay_alpha);else DRAW_Y+=10*clean;
    rrect(hx-2,182,50,50,12,C(0xffffff,.92f*sa));icon_at(dapp,hx,184,46,1,sa);
    text_fit(dapp->full,hx+56,hy,15,324,C(0xffffff,sa));
    char meta[160],by[96];tvars(by,sizeof by,T("home.by"),"author",dapp->author,NULL,NULL);
    if(dapp->platform&&dapp->year)snprintf(meta,sizeof meta,"%s · %s · %d → 3DS",by,dapp->platform,dapp->year);
    else if(dapp->platform)snprintf(meta,sizeof meta,"%s · %s → 3DS",by,dapp->platform);
    else snprintf(meta,sizeof meta,"%s",by);
    text_fit(meta,hx+56,hy+19,10,324,C(0xffffff,sa));
    scissor(TOP_W,hx+56,230-ch,324,ch);
    for(int i=0;i<nc;i++)chip(hx+56+chips[i].x,230-ch+chips[i].y,chips[i].label,chips[i].icon,chips[i].fg,chips[i].bg,sa);
    scissor_off();
    DRAW_Y=saved_draw_y;
    // Counter (.dt-counter): dark chip "3/6 · Title" with the dots under it, top right.
    int nv = nvideo, total = dapp->shots + nv, selected = shot_i + nv;
    if (total > 1) {
        json_t *sh = shot_i < 0 ? selected_video() : json_array_get(json_object_get(dapp->j, "shots"), shot_i);
        const char *cap = LJ(json_object_get(sh, shot_i < 0 ? "title" : "caption"));
        char label[96];
        if(video_active()){int duration=(int)video_duration();snprintf(label,sizeof label,"%d/%d · ▶ %s · %d:%02d",selected+1,total,cap?cap:"",duration/60,duration%60);}
        else snprintf(label,sizeof label,cap&&*cap?"%d/%d · %s":"%d/%d",selected+1,total,cap);
        char fitted[96];fit_label_style(fitted,sizeof fitted,label,9,600,250);
        snprintf(label,sizeof label,"%s",fitted);
        float cw=chip_width(label,NULL);
        chip(394-cw,36,label,NULL,C(0xffffff,1),C(0x000000,.45f),sa);
        float w = total * 4 + (total - 1) * 3 + 6, x0 = 392 - w;
        for (int i = 0; i < total; i++) {
            bool on = i == selected;
            rrect(x0, 55, on ? 10 : 4, 4, 2, C(0xffffff, (on ? 1 : .45f) * sa));
            x0 += (on ? 10 : 4) + 3;
        }
        if (clean > .004f) {
            // Clean view: the same counter, larger, alone in the corner the status bar left free.
            float ca = full * clean, cy = 8 - 6 * (1 - clean);
            snprintf(label,sizeof label,cap&&*cap?"%d/%d · %s":"%d/%d",selected+1,total,cap);
            fit_label_style(fitted,sizeof fitted,label,11,700,300);
            vgrad(0, 0, TOP_W, 56, C(0, .28f * ca), C(0, 0));
            chip_big(392, cy, fitted, ca);
            w = total * 5 + (total - 1) * 4 + 7, x0 = 392 - w;
            for (int i = 0; i < total; i++) {
                bool on = i == selected;
                rrect(x0, cy + 27, on ? 12 : 5, 5, 2.5f, C(0xffffff, (on ? 1 : .5f) * ca));
                x0 += (on ? 12 : 5) + 4;
            }
        }
    }
}

static void detail_top(float sa) {
    if (video_active()) {
        rect(0, 0, 400, 240, C(0, sa));
        if (sheet_ok(&video_posters))
            draw_img(sheet_img(&video_posters, shot_i + nvideo), 0, 0, 1, 1, sa);
        video_draw(false,0,0,400,240,sa);
        if(video_overlay_alpha>.002f)detail_top_overlay(sa*video_overlay_alpha);
        return;
    }
    float in = ease_out(span(NOW, trans_t, .56f));
    float zoom = trans_phase == 2 ? lerpf(1.08f, 1, in) : 1;
    bool any = false;
    if (dapp->shots && shot_i >= 0) {
        bool ok;
        C2D_Image cur = shot_img(shot_i, false, &ok);
        float p = test_freeze_media?1:ease_out(span(NOW, shot_t, .8f));
        if (ok) {
            any = true;
            float drift = test_freeze_media?1:1 + .07f * (1 - ease_out(span(NOW, shot_t, 6.5f)));
            float s = drift * zoom;
            if (shot_prev >= 0 && p < 1) {
                bool ok2;
                C2D_Image prev = shot_img(shot_prev, false, &ok2);
                if (ok2)
                    draw_img(prev, 200 - 200 * s, 120 - 120 * s, s, s, sa);
            }
            draw_img(cur, 200 - 200 * s + EYE * -2, 120 - 120 * s, s, s, sa * (shot_prev >= 0 ? p : 1));
        }
    }
    if (shot_i < 0 && sheet_ok(&video_posters)) {
        draw_img(sheet_img(&video_posters, shot_i + nvideo), 0, 0, 1, 1, sa);
        any = true;
        draw_icon("play", 200, 105, 26, C(0xffffff, sa));
    }
    if (!any)
        banner_draw(dapp, sa, 0);
    detail_top_overlay(sa);
}

static float facts_box(float x, float y, float w, const char *k, const char *v, float a) {
    float h = fmaxf(40,18+text_wrap_h(v,11,w-14)+6);
    rrect(x, y, w, h, 8, C(TH.surface, a));
    rrect_outline(x, y, w, h, 8, 1, C(TH.line, TH.line_a * a));
    text_caps(k, x + 7, y + 4, 9, .5f, C(TH.ink_faint, a), AL_LEFT);
    text_wrap_style(v, x + 7, y + 18, 11,600, C(TH.ink, a), w - 14);
    return h;
}

static float detail_info(float y, float a) {
    float x = 10, w = 300;
    float y0 = y;
    if(dapp->dev){
        y+=2;float body=text_wrap_h(T("detail.devBody"),11,w-18),h=63+body;
        u32 lavender=S.dark?0x9d92f0:LAV;
        rrect(x,y,w,h,10,C(lavender,.16f*a));draw_icon("hammer",x+14,y+15,12,C(lavender,a));
        text_style(T("detail.devTitle"),x+26,y+9,10,900,0,C(lavender,a),AL_LEFT);
        text_wrap(T("detail.devBody"),x+9,y+27,11,C(TH.ink_soft,a),w-18);
        float py=y+27+body+6;char percent[16];snprintf(percent,sizeof percent,"%d %%",dapp->progress);
        text_style(T("detail.progress"),x+9,py,10,800,0,C(TH.ink,a),AL_LEFT);text_style(percent,x+w-9,py,10,800,0,C(TH.ink,a),AL_RIGHT);
        rrect(x+9,py+17,w-18,5,2.5f,C(TH.surface3,a));rrect(x+9,py+17,(w-18)*dapp->progress/100.f,5,2.5f,C(lavender,a));
        y+=h+8;
    }
    const char *tag = LJ(json_object_get(dapp->j, "tagline")), *desc = LJ(json_object_get(dapp->j, "description"));
    if (strcmp(tag, desc)) y += text_wrap_style(tag, x, y, 12, 700, C(TH.ink, a), w) + 4;
    y += text_wrap(desc, x, y, 11, C(TH.ink_soft, a), w) + 8;
    json_t *feats = json_object_get(dapp->j, "features");
    json_t *fl = json_is_object(feats) ? json_object_get(feats, LANGS[S.lang]) : NULL;
    if (!fl && json_is_object(feats))
        fl = json_object_get(feats, "en");
    if (!fl && json_is_object(feats))
        fl = json_object_get(feats, "es");
    if (json_array_size(fl)) {
        text_caps(T("detail.features"), x, y, 9, 1, C(TH.mint, a), AL_LEFT);
        y += 14;
        size_t i;
        json_t *f;
        json_array_foreach(fl, i, f) {
            draw_icon("check", x + 5, y + 6, 10, C(TH.mint, a));
            y += text_wrap(json_string_value(f), x + 14, y, 11, C(TH.ink, a), w - 14) + 3;
        }
        y += 6;
    }
    if(dapp->universal){draw_icon("globe",x+5,y+5,10,C(SKY,a));text_style(T("udb.source"),x+14,y,8.5f,800,0,C(SKY,a),AL_LEFT);y+=22;}
    text_caps(T("detail.facts"), x, y, 9, 1, C(TH.mint, a), AL_LEFT);
    y += 14;
    json_t *rel = app_release(dapp, sheet_exp);
    char v[96], size[24];
    const char *keys[8] = {"detail.version", "detail.size",   "detail.author",  "detail.original",
                           "detail.console", "detail.stereo", "detail.updated", "detail.stars"};
    const char *vals[8];
    char ver[64] = "—", orig[64] = "—", stars[24];
    if (rel)
        snprintf(ver, sizeof ver, "%s · %s", json_string_value(json_object_get(rel, "tag")),
                 T(sheet_exp ? "channel.experimental" : "channel.stable"));
    fmt_size(size, sizeof size, app_download_size(dapp, sheet_cia, sheet_exp));
    if (dapp->platform)
        snprintf(orig, sizeof orig, dapp->year ? "%s · %d" : "%s", dapp->platform, dapp->year);
    vals[0] = ver;
    vals[1] = rel ? size : "—";
    vals[2] = dapp->author;
    vals[3] = orig;
    vals[4]=T(!dapp->old_ok?"detail.consoleNew":json_is_string(json_object_get(json_object_get(dapp->j,"compat"),"o3ds"))?"detail.consoleNewExp":"detail.consoleBoth");
    fmt_date(v,sizeof v,rel?json_string_value(json_object_get(rel,"date")):"—");
    json_t *stereo = json_object_get(json_object_get(dapp->j, "compat"), "stereo");
    vals[5] = T(json_is_string(stereo) ? "detail.experimental" : dapp->stereo ? "detail.yes" : "detail.no");
    vals[6] = v;
    fmt_stars(stars,sizeof stars,dapp->stars);
    vals[7] = stars;
    for (int i = 0; i < 8; i += 2) {
        float h1 = facts_box(x, y, 148, T(keys[i]), vals[i], a);
        float h2 = facts_box(x + 152, y, 148, T(keys[i + 1]), vals[i + 1], a);
        y += fmaxf(h1, h2) + 4;
    }
    const char *license = json_string_value(json_object_get(dapp->j, "license"));
    if (license)
        y += facts_box(x, y, w, T("detail.license"), license, a) + 4;
    const char *credits = json_string_value(json_object_get(dapp->j, "credits"));
    if (credits)
        y += facts_box(x, y, w, T("detail.basedOn"), credits, a) + 4;
    if (dapp->repo) {
        char r[96];
        snprintf(r, sizeof r, "github.com/%s", dapp->repo);
        y += facts_box(x, y, w, T("detail.repo"), r, a) + 4;
    }
    return y - y0;
}

static float detail_shots(float y, float a) {
    y-=2;
    int total = nvideo + dapp->shots, selected = shot_i + nvideo;
    if (!total) {
        text(T("detail.noShots"), 160, y + 70, 10, C(TH.ink_faint, a), AL_CENTER);
        return 160;
    }
    float shift = clampf(selected * 50 + 23 - 150, 0, fmaxf(0, total * 50 - 304));
    thumb_shift = shift;
    for (int i = 0; i < total; i++) {
        float x = 10 + i * 50 - shift;
        if (x + 46 < 0 || x > 320)
            continue;
        bool ok;
        C2D_Image im;
        if (i < nvideo) {
            ok = sheet_ok(&video_posters);
            im = sheet_img(&video_posters, i);
        } else
            im = shot_img(i - nvideo, false, &ok);
        if (i == selected)
            rrect(x - 2, y + 1, 50, 32, 6, C(FORGE, a));
        if (ok)
            draw_img(im, x, y + 3, 46.f / 400, 28.f / 240, a * (i == selected ? 1 : .55f));
        if (i < nvideo)
            draw_icon("play", x + 23, y + 17, 12, C(0xffffff, a));
    }
    y += 42;
    bool ok;
    C2D_Image im;
    if (shot_i < 0) {
        bool bottom = json_is_string(json_object_get(selected_video(), "bottom"));
        Sheet *posters = bottom ? &video_bottom_posters : &video_posters;
        im = sheet_img(posters, shot_i + nvideo);
        ok = sheet_ok(posters);
    } else
        im = shot_img(shot_i, dapp->shot_bottom[shot_i], &ok);
    float preview_h=shot_i<0&&!json_is_string(json_object_get(selected_video(),"bottom"))?99:132;
    rrect(10, y, 176, preview_h, 8, C(0, .85f * a));
    if (ok && (shot_i < 0 || dapp->shot_bottom[shot_i]))
        { float scale=fminf(176.f/im.subtex->width,preview_h/im.subtex->height);
          draw_img(im,10+(176-im.subtex->width*scale)/2,y+(preview_h-im.subtex->height*scale)/2,scale,scale,a); }
    else
        draw_icon("eye", 98, y + 66, 20, C(TH.ink_faint, a));
    char cap[128];
    json_t *m = shot_i < 0 ? selected_video() : json_array_get(json_object_get(dapp->j, "shots"), shot_i);
    if (shot_i < 0)
        snprintf(cap, sizeof cap, "%s", LJ(json_object_get(m, "title")));
    else
        snprintf(cap, sizeof cap, "%d/%d · %s", selected + 1, total, LJ(json_object_get(m, "caption")));
    text_wrap_style(cap, 195, y, 10,900, C(TH.ink, a), 115);
    if (shot_i >= 0)
        text_wrap_style(T("detail.shotHint"), 195, y + 18, 10,600, C(TH.ink_faint, a), 115);
    if(shot_i<0) {
        double duration=json_number_value(json_object_get(m,"duration"));
        if(duration>0){char time[24];snprintf(time,sizeof time,"%d:%02d",(int)duration/60,(int)duration%60);chip(195,y+20,time,"clock",C(TH.ink_soft,1),C(TH.surface2,1),a);}
        button(195,y+44,60,28,T("video.play"),"play",NULL,BTN_PRIMARY,a);
        circle(98,y+preview_h/2,17,C(0,.45f*a));ring(98,y+preview_h/2,18,2,0,2*M_PI,C(0xffffff,.9f*a));draw_icon("play",99,y+preview_h/2,15,C(0xffffff,a));
    }

    draw_icon("back",211,y+110, 11, C(TH.ink_soft, a));
    draw_icon("next",245,y+110, 11, C(TH.ink_soft, a));
    return 173;
}

static float detail_requirement_card(json_t *req,float x,float y,float w,float a){
    y+=5;bool have=detail_requirement_has(req);const char *title=LJ(json_object_get(req,"label")),*status=T(have?"detail.found":"detail.missing"),*accepted=json_string_value(json_object_get(req,"accept")),*help=LJ(json_object_get(req,"help"));
    char file[1024],valid[1024]="",versions[1024]="",labels[1024]="";bool location=app_requirement_path(req,file,sizeof file);
    const char *found=rom_find_path(req,NULL);
    if(found)snprintf(file,sizeof file,"%s",found);
    else if(location&&rom_find_ruled(req)){char *slash=strrchr(file,'/');if(slash)slash[1]=0;}  // any name: only the folder matters
    const char *subtitle=location||found?file:T("detail.repoReadme");float cw=text_style_w(status,9,600,0)+24,tw=fmaxf(20,w-16-26-6-cw),th=text_wrap_h_style(title,11,700,tw),head=fmaxf(20,th+12),cy=y+7+head;
    if(accepted&&*accepted)tvars(valid,sizeof valid,T("detail.anyName"),"names",accepted,NULL,NULL);
    size_t i;json_t *hash;json_array_foreach(json_object_get(req,"hashes"),i,hash){const char *label=json_string_value(json_object_get(hash,"label"));if(label){size_t used=strlen(labels);snprintf(labels+used,sizeof labels-used,"%s%s",used?", ":"",label);}}
    if(labels[0])tvars(versions,sizeof versions,T("detail.hashes"),"list",labels,NULL,NULL);
    float lines[3]={-1,-1,-1};const char *body[]={valid,versions,help};
    if(found)body[0]=body[1]=body[2]=NULL;  // already there: nothing left to explain
    for(int k=0;k<3;k++)if(body[k]&&*body[k]){cy+=k==2?3:4;lines[k]=cy;cy+=text_wrap_h(body[k],11,w-16);}
    json_t *tool=json_object_get(req,"tool");const char *url=json_string_value(json_object_get(tool,"url")),*tool_label=LJ(json_object_get(tool,"label"));
    struct {const char *label,*icon,*url;json_t *check;float x,y,w;} actions[2];int n=0;
    if(!have)actions[n++]=(typeof(actions[0])){T("native.checkFiles"),"sd",NULL,req,0,0,0};
    if(url&&*url)actions[n++]=(typeof(actions[0])){tool_label,"link",url,NULL,0,0,0};
    float ax=x+8,ay=cy+5;
    for(int k=0;k<n;k++){float aw=fminf(w-16,text_style_w(actions[k].label,10,600,0)+30);if(ax>x+8&&ax+aw>x+w-8){ax=x+8;ay+=28;}actions[k].x=ax;actions[k].y=ay;actions[k].w=aw;ax+=aw+4;}
    float h=(n?ay+24:cy)+7-y;
    rrect(x,y,w,h,10,C(TH.surface,a));rrect_outline(x,y,w,h,10,1,C(have?TH.line:AMBER,(have?TH.line_a:S.dark?.4f:.5f)*a));if(!have&&S.dark)rrect(x+1,y+6,2,h-12,1,C(AMBER,a));
    rrect(x+8,y+7+(head-20)/2,20,20,6,C(TH.surface2,a));const char *id=json_string_value(json_object_get(req,"id"));draw_icon(id&&!strcmp(id,"dspfirm")?"volume":id&&!strcmp(id,"rom")?"rom":"file",x+18,y+7+head/2,12,C(TH.ink_soft,a));
    text_wrap_style(title,x+34,y+7,11,700,C(TH.ink,a),tw);text_fit_style(subtitle,x+34,y+7+th,9,600,tw,C(TH.ink_faint,a));
    chip(x+w-8-cw,y+7+(head-16)/2,status,have?"check":"warn",C(have?TH.mint:S.dark?0xffcc5c:0xa86f00,1),C(have?TH.mint:AMBER,.14f),a);
    for(int k=0;k<3;k++)if(lines[k]>=0)text_wrap(body[k],x+8,lines[k],11,C(TH.ink_soft,a),w-16);
    for(int k=0;k<n;k++){button(actions[k].x,actions[k].y,actions[k].w,24,actions[k].label,actions[k].icon,NULL,actions[k].url?BTN_GHOST:BTN_SURFACE,a);if(requirement_tool_count<32){int j=requirement_tool_count++;requirement_tools[j].x=actions[k].x;requirement_tools[j].y=actions[k].y;requirement_tools[j].w=actions[k].w;requirement_tools[j].url=actions[k].url;requirement_tools[j].check=actions[k].check;}}
    return y+h;
}
static float detail_install(float y, float a) {
    float x = 10, w = 300, y0 = y;
    requirement_tool_count=0; detail_format_y=detail_channel_y=-1000;
    if(json_is_true(json_object_get(dapp->j,"nativeUnsupported"))) {
        y+=text_wrap("Universal-Updater · GitHub",x,y,11,C(AMBER,a),w)+8;
    }
    y+=2;
    for(int opt=0;opt<2;opt++) {
        if(opt==0 && !(app_has_format(dapp,true) && app_has_format(dapp,false))) continue;
        if(opt==1 && !app_has_experimental(dapp)) continue;
        const char *left="CIA · HOME Menu",*right="3DSX · Homebrew Launcher";char stable[128],experimental[128];
        if(opt){snprintf(stable,sizeof stable,"%s %s",T("channel.stable"),json_string_value(json_object_get(app_release(dapp,false),"tag")));snprintf(experimental,sizeof experimental,"%s %s",T("channel.experimental"),json_string_value(json_object_get(app_release(dapp,true),"tag")));left=stable;right=experimental;}
        // Reserve the rendered uppercase label, including its letter spacing.
        float labelw=fmaxf(50,ceilf(text_caps_w(T(opt?"confirm.channel":"confirm.format"),10,.6f)+.6f));
        float natural=text_style_w(left,10,600,0)+text_style_w(right,10,600,0)+32;
        bool stacked=opt==0 || labelw+8+natural>w;
        if(stacked){text_caps(T(opt?"confirm.channel":"confirm.format"),x,y,10,.6f,C(TH.ink_faint,a),AL_LEFT);y+=21;}
        else text_caps(T(opt?"confirm.channel":"confirm.format"),x,y+5,10,.6f,C(TH.ink_faint,a),AL_LEFT);
        float sx=stacked?x:x+labelw+8,sw=stacked?w:w-labelw-8;
        if(opt==0){detail_format_y=y;detail_format_x=sx;detail_format_w=sw;}else{detail_channel_y=y;detail_channel_x=sx;detail_channel_w=sw;}
        bool right_on=opt?sheet_exp:!sheet_cia;
        rrect(sx,y,sw,26,8,C(TH.surface2,a));rrect_outline(sx,y,sw,26,8,1,C(TH.line,TH.line_a*a));
        float half=(sw-4)/2;
        rrect(sx+2+(right_on?half:0),y+2,half,22,6,C(S.dark?TH.surface3:TH.surface,a));
        text_style(left,sx+2+half/2,y+6,10,600,0,C(TH.ink,a),AL_CENTER);
        text_style(right,sx+2+half*1.5f,y+6,10,600,0,C(TH.ink_soft,a),AL_CENTER);
        if(detail_option_focus==opt)rrect_outline(sx,y,sw,26,8,1,C(FORGE,a));
        y+=31;
    }
    y+=5;
    const char *notice = LJ(json_object_get(dapp->j, "notice"));
    if (notice && *notice) {
        float h = text_wrap_h(notice, 11, w - 18) + 22;
        rrect(x, y, w, h, 10, C(AMBER, .16f * a));
        text(T("detail.notice"), x + 9, y + 4, 9, C(S.dark ? 0xffcc5c : 0xa86f00, a), AL_LEFT);
        text_wrap(notice, x + 9, y + 17, 11, C(TH.ink_soft, a), w - 18);
        y += h + 8;
    }
    text_caps(T("detail.sdTitle"), x, y, 9, 1, C(TH.mint, a), AL_LEFT);
    y += 17;
    json_t *steps=app_install_steps(dapp,sheet_cia),*s;size_t i;
    float tree_h=21+detail_plan.count*21+(detail_plan.cia?24:0)+10;
    if(detail_plan.truncated)tree_h+=text_wrap_h(S.lang==0?"Vista limitada a 256 rutas.":"Preview limited to 256 paths.",10,w-16)+4;
    rrect(x,y,w,tree_h,10,C(TH.surface,a));
    float ry=y+5;
    draw_icon("sd",x+18.5f,ry+10.5f,11,C(TH.ink_faint,a));text_style("sdmc:/",x+28,ry+3.5f,10,900,0,C(TH.ink_soft,a),AL_LEFT);ry+=21;
    for(int j=0;j<detail_plan.count;j++) {
        PlanEntry *e=&detail_plan.rows[j];float indent=fminf(10*e->depth,80),xx=x+12.5f+indent;
        const char *name=strrchr(e->path,'/');name=name?name+1:e->path;
        bool requirement=e->requirement>=0;
        bool have=requirement?detail_requirement_has(json_array_get(json_object_get(dapp->j,"requires"),e->requirement)):e->exists;
        bool fresh=!have&&!requirement;
        const char *ic=e->dir?(have?"folder":"folderPlus"):requirement?"rom":e->dsp?"volume":"file";
        char label[PLAN_PATH+2],note[80];snprintf(label,sizeof label,"%s%s",name,e->dir?"/":"");
        if(requirement){
            json_t *req=json_array_get(json_object_get(dapp->j,"requires"),e->requirement);
            const char *found=rom_find_path(req,NULL),*base=found?strrchr(found,'/'):NULL;
            const char *any=json_string_value(json_object_get(req,"accept"));
            if(base)snprintf(label,sizeof label,"%s",base+1);
            else if(any&&any[0]=='*'&&rom_find_ruled(req))snprintf(label,sizeof label,"%s",any);
        }
        if(e->dir)snprintf(note,sizeof note,"%s",T(have?"detail.exists":"detail.newFolder"));
        else if(requirement)snprintf(note,sizeof note,"%s",T(have?"detail.found":"detail.romHere"));
        else if(e->dsp)snprintf(note,sizeof note,"%s",T(have?"detail.exists":"detail.dspAuto"));
        else fmt_size(note,sizeof note,e->size);
        float note_w=fminf(100,text_style_w(note,9,900,0)),h=21;
        if(fresh||requirement){
            if(S.dark){rrect(x+4,ry,w-8,h,5,C(0xffffff,.035f*a));rrect(x+4,ry+2,2,h-4,1,C(requirement?(have?TH.mint:AMBER):FORGE,a));}
            else if(requirement)rrect(x+4,ry,w-8,h,5,C(have?MINT:AMBER,.14f*a));
            else rrect_hgrad(x+4,ry,w-8,h,5,C(0xfff0e1,a),C(TH.surface,a));
        }
        draw_icon(ic,xx,ry+10.5f,11,C(e->dir?(fresh?(S.dark?0xffa04a:FORGE):AMBER):requirement?(have?TH.mint:AMBER):e->dsp?SKY:TH.ink_faint,a));
        text_fit_style(label,xx+9.5f,ry+3.5f,10,800,w-(xx-x)-18-note_w-6,C(TH.ink,a));
        text_style(note,x+w-8,ry+4.5f,9,900,0,C(requirement?(have?TH.mint:AMBER):(fresh&&(e->dir||e->dsp))?(S.dark?0xffb36b:0xd0620b):TH.ink_soft,a),AL_RIGHT);
        ry+=h;
    }
    if(detail_plan.cia){
        ry+=3;for(float lx=x+4;lx<x+w-4;lx+=5)rect(lx,ry,fminf(3,x+w-4-lx),1,C(TH.line,(S.dark?.14f:.18f)*a));
        draw_icon("home",x+18.5f,ry+11.5f,11,C(SKY,a));
        char title[128];fit_label_style(title,sizeof title,dapp->title,9,900,140);
        float note_w=text_style_w(title,9,900,0);
        text_fit_style(T("detail.homeMenu"),x+28,ry+4.5f,10,800,w-40-note_w,C(TH.ink,a));
        text_style(title,x+w-8,ry+5.5f,9,900,0,C(TH.ink_soft,a),AL_RIGHT);ry+=21;
    }
    if(detail_plan.truncated)ry+=text_wrap(T("native.treeLimit"),x+8,ry,10,C(AMBER,a),w-16)+4;
    rrect_outline(x,y,w,tree_h,10,1,C(TH.line,TH.line_a*a));
    y=ry+12;
    text_caps(T("detail.youProvide"),x,y,9,1,C(TH.mint,a),AL_LEFT);y+=17;
    json_array_foreach(json_object_get(dapp->j, "requires"), i, s) {
        y=detail_requirement_card(s,x,y,w,a);
    }
    if(json_array_size(json_object_get(dapp->j,"requires"))==0){draw_icon("check",x+5,y+7,10,C(TH.mint,a));y+=text_wrap(T("detail.noRequirements"),x+16,y,11,C(TH.ink_soft,a),w-16);}
    bool dsp=false; json_array_foreach(steps,i,s) { const char *t=json_string_value(json_object_get(s,"type")); if(t && !strcmp(t,"dumpDsp")) dsp=true; }
    if(dsp){y+=7;draw_icon("volume",x+5,y+6,10,C(SKY,a));y+=text_wrap(T("detail.dspNote"),x+16,y,11,C(TH.ink_soft,a),w-16);}
    return y-y0+12;
}

static float detail_news(float y, float a) {
    float x = 10, w = 300, y0 = y;
    json_t *list_[16];
    int n = 0;
    json_t *rel = json_object_get(dapp->j, "releases");
    if (json_object_get(rel,"experimental"))
        list_[n++] = json_object_get(rel, "experimental");
    if (json_object_get(rel, "stable"))
        list_[n++] = json_object_get(rel, "stable");
    size_t i;
    json_t *h;
    json_array_foreach(json_object_get(dapp->j, "history"), i, h) if (n < 16) list_[n++] = h;
    for(int i=0;i<n;i++) for(int j=i+1;j<n;) {
        const char *a=json_string_value(json_object_get(list_[i],"tag")),*b=json_string_value(json_object_get(list_[j],"tag"));
        if(a && b && !strcmp(a,b)) { memmove(&list_[j],&list_[j+1],(--n-j)*sizeof list_[0]); } else j++;
    }
    for(int i=1;i<n;i++) { json_t *r=list_[i]; int j=i;
        const char *date=json_string_value(json_object_get(r,"date"));
        while(j>0) { const char *prev=json_string_value(json_object_get(list_[j-1],"date"));
            if(strcmp(date?date:"",prev?prev:"")<=0) break;
            list_[j]=list_[j-1]; j--;
        } list_[j]=r;
    }
    news_count = n;
    if(n){char heading[128],number[16];snprintf(number,sizeof number,"%d",n);tvars(heading,sizeof heading,T("detail.history"),"n",number,NULL,NULL);text_caps(heading,x,y+10,9,1,C(TH.mint,a),AL_LEFT);y+=27;}
    if (!n)
        return text_wrap(T("detail.devBody"), x, y, 11, C(TH.ink_soft, a), w);
    news_open = (int)clampf(news_open, -1, n - 1);
    for (int k = 0; k < n; k++) {
        news_rows[k] = y;
        bool open = k == news_open;
        json_t *r = list_[k];
        const char *tag = json_string_value(json_object_get(r, "tag")), *date = json_string_value(json_object_get(r, "date"));
        json_t *notes = json_object_get(r, "notes");
        json_t *nl = json_object_get(notes, LANGS[S.lang]);
        if (!nl)
            nl = json_object_get(notes, "en");
        if (!nl)
            nl = json_object_get(notes, "es");
        float body = 0;
        if (open) {
            size_t q;
            json_t *line;
            json_array_foreach(nl, q, line) body += text_wrap_h(json_string_value(line), 11, w - 30) + 2;
            body += 8;
        }
        float hgt = 30 + body;
        rrect(x, y, w, hgt, 10, C(TH.surface, a));
        rrect_outline(x, y, w, hgt, 10, open ? 1.5f : 1, open ? C(FORGE, .55f * a) : C(TH.line, TH.line_a * a));
        text_fit_style(tag,x+8,y+7,11,900,110,C(TH.ink,a));
        bool exp = r == json_object_get(rel,"experimental") || (tag && strchr(tag, 'E') && strchr(tag, '-'));
        float cx = x + 13 + text_style_w(tag,11,900,0);
        cx += chip(cx, y + 5, T(exp ? "channel.experimental" : "channel.stable"), NULL, C(exp ? LAV : TH.mint, 1),
                   C(exp ? LAV : TH.mint, .16f), a) +
              3;
        if (k == 0)
            chip(cx, y + 5, T("detail.latest"), NULL, C(S.dark ? 0xffb36b : 0xd0620b, 1), C(FORGE, .16f), a);
        char date_label[48]; fmt_date(date_label,sizeof date_label,date);
        text_style(date_label,x+w-22,y+8,9,800,0,C(TH.ink_faint,a),AL_RIGHT);
        draw_icon(open ? "next" : "next", x + w - 10, y + 11, 10, C(open ? FORGE : TH.ink_faint, a));
        if (open) {
            float ly = y + 34;
            size_t q;
            json_t *line;
            json_array_foreach(nl, q, line) {
                circle(x + 14, ly + 6, 1.5f, C(TH.ink_soft, a));
                ly += text_wrap(json_string_value(line), x + 20, ly, 11, C(TH.ink_soft, a), w - 30) + 2;
            }
        }
        y += hgt + 4;
    }
    return y - y0;
}

static void sheet_draw(float a) {
    float p=bezier(span(NOW,sheet_t,.34f),.22f,1,.36f,1), dy=(1-p)*240;
    rect(0,0,320,240,C(0x0a1216,.42f*a*p));
    rrect(6,2+dy,308,232,14,C(TH.surface,a));
    rrect_outline(6,2+dy,308,232,14,1,C(TH.line,TH.line_a*a));
    icon_at(dapp,16,17+dy,30,1,a);
    char title[160],folder[160];
    tvars(title,sizeof title,T("confirm.title"),"title",dapp->title,NULL,NULL);
    text_fit_style(title,53,18+dy,12,900,251,C(TH.ink,a));
    snprintf(folder,sizeof folder,"sdmc:%s",dapp->folder?dapp->folder:"/3ds/");
    text_fit(folder,53,33+dy,9,251,C(TH.ink_faint,a));
    float cy=54+dy;
    if(app_has_experimental(dapp)) {
        for(int c=0;c<2;c++) {
            bool on=sheet_exp==(c==1);
            json_t *rel=json_object_get(json_object_get(dapp->j,"releases"),c?"experimental":"stable");
            rrect(16,cy,288,38,8,C(TH.surface2,a));
            rrect_outline(16,cy,288,38,8,on?2:1,C(on?FORGE:TH.line,(on?1:TH.line_a)*a));
            circle(30,cy+19,5,C(on?FORGE:TH.surface3,a)); if(on) circle(30,cy+19,2,C(TH.surface,a));
            char label[128],date[48];
            snprintf(label,sizeof label,"%s · %s",T(c?"channel.experimental":"channel.stable"),json_string_value(json_object_get(rel,"tag")));
            text_fit_style(label,43,cy+6,10,700,185,C(TH.ink,a));
            fmt_date(date,sizeof date,json_string_value(json_object_get(rel,"date")));
            text_fit_style(T(c?"confirm.expHint":"confirm.stableHint"),43,cy+20,9,700,185,C(TH.ink_soft,a));
            text_style(date,295,cy+12,10,800,0,C(TH.ink_faint,a),AL_RIGHT);
            cy+=44;
        }
    }
    cy=sheet_format_y()+dy;
    if(app_has_format(dapp,true) && app_has_format(dapp,false)) {
        text_caps(T("confirm.format"),16,cy+6,10,.6f,C(TH.ink_faint,a),AL_LEFT);
        rrect(72,cy,232,26,8,C(TH.surface2,a));
        rrect(sheet_cia?75:188,cy+3,113,20,5,C(S.dark?TH.surface3:TH.surface,a));
        text_style("CIA",130,cy+5,10,600,0,C(sheet_cia?TH.ink:TH.ink_soft,a),AL_CENTER);
        text_style("3DSX",246,cy+5,10,600,0,C(!sheet_cia?TH.ink:TH.ink_soft,a),AL_CENTER);
        cy+=32;
    }
    char size[24],free_s[24],info[160],fr[64];
    fmt_size(size,sizeof size,app_download_size(dapp,sheet_cia,sheet_exp));
    fmt_size(free_s,sizeof free_s,(double)sd_free);
    tvars(fr,sizeof fr,T("confirm.free"),"free",free_s,NULL,NULL);
    snprintf(info,sizeof info,"%s · %s · %s",IS_NEW3DS?"New 3DS":"Old 3DS",size,fr);
    bool compatible=sheet_compatible();
    bool old_experimental=!IS_NEW3DS && json_is_string(json_object_get(json_object_get(dapp->j,"compat"),"o3ds"));
    draw_icon(compatible && !old_experimental?"check":"warn",22,cy+7,10,C(compatible && !old_experimental?TH.mint:AMBER,a));
    text_fit(info,32,cy,10,264,C(TH.ink_soft,a));
    if(!compatible || old_experimental) text_fit(T(!compatible?"confirm.blockedNew":"confirm.expOld"),16,cy+17,10,288,C(AMBER,a));
    button(16,196+dy,CONFIRM_CANCEL_W,28,T("action.cancel"),NULL,"B",BTN_GHOST,a);
    button(CONFIRM_INSTALL_X,196+dy,CONFIRM_INSTALL_W,28,T("action.install"),"download","A",BTN_PRIMARY,compatible?a:.4f*a);
}

static void detail_bottom(float sa) {
    if (video_active()) {
        bool split = video_bottom();
        bool ui=!split||video_ui_on;
        if (split) {
            rect(0, 0, 320, 240, C(0, sa));
            video_draw(true, 0, 0, 320, 240, sa);
        } else {
            draw_bg(false, 1);
        }
        if(!ui && video_overlay_alpha<.01f) return;
        sa*=split?video_overlay_alpha:1;
        if (split)
            rect(0, 0, 320, 240, C(TH.bg, .86f * sa));
        button(12, 12, 42, 26, NULL, "back", "B", BTN_SURFACE, sa);
        float ma = video_has_audio() ? sa : .35f * sa;
        button(266, 12, 42, 26, NULL, "volume", "Y", BTN_SURFACE, ma);
        if (video_muted())
            C2D_DrawLine(273, 21, C(FORGE, ma), 282, 29, C(FORGE, ma), 1.5f, .5f);
        for (int i = 0; i < 2; i++) {
            float x = i ? 226 : 50;
            rrect(x, 86, 44, 44, 22, C(TH.surface, sa));
            rrect_outline(x, 86, 44, 44, 22, 1, C(TH.line, TH.line_a * sa));
            draw_icon(i ? "forward5" : "rewind5", x + 22, 108, 26, C(TH.ink, sa));
        }
        rrect(126, 77, 68, 68, 34, C(0, (S.dark ? .16f : .05f) * sa));
        rrect(126, 74, 68, 68, 34, C(TH.surface, sa));
        rrect_outline(126, 74, 68, 68, 34, 1, C(TH.line, TH.line_a * sa));
        if (video_paused()) {
            u32 ink = C(TH.ink, sa);
            C2D_DrawLine(156, 101, ink, 156, 115, ink, 2, .5f);
            C2D_DrawLine(156, 115, ink, 167, 108, ink, 2, .5f);
            C2D_DrawLine(167, 108, ink, 156, 101, ink, 2, .5f);
        } else {
            rrect(155, 101, 2, 14, 1, C(TH.ink, sa));
            rrect(163, 101, 2, 14, 1, C(TH.ink, sa));
        }
        glass(12, 174, 296, 54, 14, sa);
        float d = video_duration(), t = video_time(), ratio = clampf(t / fmaxf(.1f, d), 0, 1);
        if (IN.touching && IN.start_y >= 176 && IN.start_y < 200)
            ratio = clampf((IN.tx - 24) / 272.f, 0, 1);
        rrect(24, 186, 272, 5, 2.5f, C(TH.surface3, sa));
        rrect_hgrad(24, 186, fmaxf(1, 272 * ratio), 5, 2.5f, C(FORGE, sa), C(FORGE2, sa));
        circle(24 + 272 * ratio, 188.5f, 6.5f, C(TH.surface, sa));
        circle(24 + 272 * ratio, 188.5f, 4.5f, C(FORGE, sa));
        char current[16], duration[16];
        snprintf(current, sizeof current, "%d:%02d", (int)t / 60, (int)t % 60);
        snprintf(duration, sizeof duration, "%d:%02d", (int)d / 60, (int)d % 60);
        text(current, 24, 201, 9, C(TH.ink_soft, sa), AL_LEFT);
        text(duration, 296, 201, 9, C(TH.ink_soft, sa), AL_RIGHT);
        return;
    }
    draw_bg(false, 1);
    float a = sa;
    back_button(a);
    const char *labels[4];
    for(int i=0;i<4;i++) labels[i]=T(TABS_KEYS[i]);
    detail_nav.padding=8;tabs_layout(&detail_nav,36,4,280,labels,NULL,4,dtab);
    tabs_draw(&detail_nav,labels,NULL,false,a);
    // Content
    scissor(BOT_W, 0, 36, BOT_W, 164);
    float y = 41 - dscroll;
    a=tabs_content(&detail_nav,a);
    if (dtab == 0)
        content_h = detail_info(y, a);
    else if (dtab == 1)
        content_h = detail_shots(y, a);
    else if (dtab == 2)
        content_h = detail_install(y, a);
    else
        content_h = detail_news(y, a);
    DRAW_X=0;
    scissor_off();
    if (content_h > 159) {
        float bh = fmaxf(14, 164 * 164 / content_h), by = 36 + (164 - bh) * dscroll / (content_h - 159);
        rrect(316, by, 3, bh, 1.5f, C(TH.line, .2f * a));
    }
    a = sa;
    // Action bar
    AppStatus st = app_status_view(dapp);
    char lbl[96], size[24];
    if(json_is_true(json_object_get(dapp->j,"nativeUnsupported"))) {
        button_round(5,205,280,28,"Universal-Updater · GitHub","github",NULL,BTN_SURFACE,a);
    } else if (st == ST_AVAILABLE) {
        fmt_size(size, sizeof size, app_download_size(dapp, sheet_cia, sheet_exp));
        snprintf(lbl, sizeof lbl, "%s · %s", T("action.download"), size);
        button_round(5,205,280,28, lbl,"download",dtab==1 && shot_i<0?NULL:"A",BTN_PRIMARY,a);
    } else if (st == ST_UPDATE) {
        json_t *rel = app_release(dapp, sheet_exp);
        tvars(lbl, sizeof lbl, T("action.update"), "v", json_string_value(json_object_get(rel, "tag")), NULL, NULL);
        button_round(5,205,250,28,lbl,"update",dtab==1 && shot_i<0?NULL:"A",BTN_PRIMARY,a);
    } else if (st == ST_INSTALLED || st == ST_NEEDS_FILE) {
        snprintf(lbl,sizeof lbl,"%s",T("action.open"));
        button_round(5,205,250,28,lbl,"play",NULL,BTN_MINT,a);
    } else if (st == ST_BUSY) {
        Job *j = install_job(dapp);
        float p = j ? job_progress(j) : 0;
        rrect(5, 209, 280, 26, 13, C(TH.surface, a));
        snprintf(lbl, sizeof lbl, "%s", T(j && j->state == JOB_QUEUED ? "status.queued" : "status.downloading"));
        text(lbl, 16, 211, 10, C(TH.ink, a), AL_LEFT);
        snprintf(lbl, sizeof lbl, "%d %%", (int)(p * 100));
        text_dyn(lbl, 274, 211, 10, C(TH.ink, a), AL_RIGHT);
        rrect(16, 225, 258, 4, 2, C(TH.surface3, a));
        rrect(16, 225, fmaxf(4, 258 * p), 4, 2, C(FORGE, a));
        button_round(289,205,26,28, NULL, "close", NULL, BTN_SURFACE, a);
        
    } else {
        char tmp[96];
        char pct[8];
        snprintf(pct, sizeof pct, "%d", dapp->progress);
        tvars(tmp, sizeof tmp, T("action.inDev"), "p", pct, NULL, NULL);
        button_round(5,205,280,28, tmp, "hammer", NULL, BTN_GHOST, a * .7f);
    }
    if(dapp->repo && st!=ST_BUSY) {
        button_round(289,205,26,28,NULL,"github",NULL,BTN_SURFACE,a);
        draw_badge("X",305,223,a);
    }
    if(st==ST_INSTALLED || st==ST_NEEDS_FILE || st==ST_UPDATE) button_round(259,205,26,28,NULL,"trash",NULL,BTN_SURFACE,a);
    if(uninstall_on) {
        rect(0,0,320,240,C(0x0a1216,.55f*a)); glass(6,64,308,124,12,a);
        text_fit(T("action.uninstall"),16,78,14,288,C(TH.ink,a));
        text_fit(dapp->title,16,103,11,288,C(TH.ink_soft,a));
        button(16,148,130,28,T("action.cancel"),NULL,NULL,BTN_GHOST,a);
        button(152,148,152,28,T("action.uninstall"),"trash",NULL,BTN_SURFACE,a);
    }
    if (sheet_on)
        sheet_draw(a);
}

// ======================================================================================================
// INSTALL progress
// ======================================================================================================
static Job *ij;
static Sheet install_backdrop;
static char install_backdrop_id[80];
static float done_t = -1;
static int ij_state_seen;
static json_t *install_missing_req;
static float install_check_y=-1000;
static float ilog_scroll, ilog_target;
static bool ilog_manual;
static float install_row_h(const JobStep *s) {
    float h=text_wrap_h_style(s->label,11,600,242)+6;
    if(s->result[0] && s->state!=STEP_ACTIVE) h+=text_wrap_h(s->result,10,268)+3;
    if(s->state==STEP_ACTIVE && (s->bytes || s->progress>0)) h+=8;
    return fmaxf(30,h)+3;
}
static float install_tail(float y,float a,bool draw) {
    if(ij->state<=JOB_RUNNING) return 0;
    float start=y;
    const char *message=ij->state==JOB_DONE && ij->uninstall?T(ij->warnings?"native.uninstallKept":"native.uninstalled"):ij->state==JOB_FAILED?ij->error:ij->state==JOB_CANCELLED?T("install.cancelled"):ij->warnings?T("install.doneMissing"):T("install.done");
    float h=text_wrap_h(message,11,280);
    if(draw) text_wrap(message,20,y,11,C(ij->state==JOB_FAILED?DANGER:ij->warnings?AMBER:TH.mint,a),280);
    y+=h+8;
    if(ij->state==JOB_DONE && ij->warnings && !ij->uninstall) {
        json_t *req=install_missing_req;
        const char *label=LJ(json_object_get(req,"label")),*help=LJ(json_object_get(req,"help")),*path=json_string_value(json_object_get(req,"path"));
        const char *parts[3]={label,path,help};
        for(int i=0;i<3;i++) if(parts[i] && *parts[i]) { if(draw) text_wrap(parts[i],20,y,11,C(TH.ink_soft,a),280); y+=text_wrap_h(parts[i],11,280)+5; }
    }
    return y-start+6;
}
static float install_log_h(void) {
    float h=0; for(int i=0;i<ij->nsteps;i++) h+=install_row_h(&ij->steps[i]); return h+install_tail(0,0,false);
}


static void open_install(Job *j) {
    if (!j)
        return;
    ij = j;install_missing_req=j->warnings&&!j->uninstall?app_missing_requirement_view(j->app):NULL;
    install_parent = scene;
    done_t = -1;
    ij_state_seen = -1; ilog_scroll=ilog_target=0; ilog_manual=false;
    next_scene = SC_INSTALL;
    trans_phase = 1;
    trans_t = NOW;
}

static void install_update(void) {
    bool running = ij->state == JOB_QUEUED || ij->state == JOB_RUNNING;
    if(IN.repeat & (KEY_UP|KEY_DOWN)) { ilog_manual=true; ilog_target+=IN.repeat&KEY_DOWN?34:-34; }
    if(IN.touching && !IN.touch_down && IN.start_y>=36 && IN.start_y<200) {
        ilog_manual=true; ilog_target-=IN.ty-IN.last_y;
    }
    float total=install_log_h(), view=174;
    if(ij->state==JOB_DONE&&ij->warnings&&!ij->uninstall)view=80;
    if(!ilog_manual && running) {
        float active=0; for(int i=0;i<ij->cur && i<ij->nsteps;i++) active+=install_row_h(&ij->steps[i]);
        float row=ij->cur<ij->nsteps?install_row_h(&ij->steps[ij->cur]):0;
        if(active<ilog_target+14)ilog_target=active-14;else if(active+row>ilog_target+view-14)ilog_target=active+row-view+14;
    }
    ilog_target=clampf(ilog_target,0,fmaxf(0,total-view));
    ilog_scroll=lerpf(ilog_scroll,ilog_target,1-expf(-DT*18));
    if (!running && done_t < 0) {
        install_missing_req=ij->warnings&&!ij->uninstall?app_missing_requirement_view(ij->app):NULL;
        done_t = NOW;
        if(ij->state==JOB_DONE&&ij->uninstall)sfx("back");
        else if (ij->state == JOB_DONE) {
            sfx("anvil");
            sfx("complete");
            fx_burst(200, 96, 90);
        } else
            sfx("error");
    }
    if(!running&&ij->warnings&&install_check_y>0&&hit(17,install_check_y,82,24)){
        requirement_check_start(ij->app,NULL,true);return;
    }
    bool can_open = !running && ij->state == JOB_DONE && !ij->uninstall;
    if (can_open && ((IN.down & KEY_Y) || hit(5,207,64,28))) {
        launch_app(ij->app);
        return;
    }
    if ((IN.down & KEY_B) || (!running && (IN.down & KEY_A)) ||
        hit(can_open?74:5,207,running?INSTALL_BROWSE_W:can_open?241:310,28)) {
        if (running)
            toast(T("install.background"), FORGE);
        sfx("back");
        next_scene = install_parent;
        trans_phase = 1;
        trans_t = NOW;
        return;
    }
    if (running && ((IN.down & KEY_X) || hit(INSTALL_CANCEL_X,207,INSTALL_CANCEL_W,28)))
        install_cancel(ij);
}

static void install_top(float sa) {
    App *a = ij->app;
    if(sheet_ok(&install_backdrop))draw_img(sheet_img(&install_backdrop,0),0,0,1,1,sa);
    else {if(sheet_ok(&a->bg))draw_img(sheet_img(&a->bg,0),-20,-12,1.1f,1.1f,sa);rect(0,0,TOP_W,SCR_H,C(0,.55f*sa));}
    float p = job_progress(ij);
    fx_draw(0, sa);
    char title[96];
    bool done = ij->state == JOB_DONE, fail = ij->state == JOB_FAILED || ij->state == JOB_CANCELLED;
    if(done) snprintf(title,sizeof title,"%s",ij->uninstall?T("native.uninstalled"):ij->warnings?T("install.doneMissing"):T("install.done"));
    else if (fail)
        snprintf(title, sizeof title, "%s", ij->state == JOB_CANCELLED ? T("install.cancelled") : T("install.failed"));
    else if (ij->state == JOB_QUEUED)
        snprintf(title, sizeof title, "%s", T("install.queued"));
    else if(ij->uninstall)snprintf(title,sizeof title,"%s",T("native.uninstalling"));
    else
        snprintf(title, sizeof title, "%s %s", T("install.title"), a->title);
    float chips_w=0;const char *chiplabels[]={ij->tag,ij->cia?"CIA":"3DSX",T(ij->experimental?"channel.experimental":"channel.stable")};
    for(int i=0;i<3;i++)chips_w+=text_style_w(chiplabels[i],9,600,0)+13;
    text_fit_style(title,10,36,13,700,fmaxf(120,372-chips_w),C(0xffffff,sa));
    float cxchips=390-chips_w;for(int i=0;i<3;i++)cxchips+=chip(cxchips,36,chiplabels[i],NULL,C(0xffffff,1),C(0,.45f),sa)+3;
    // Ring
    float cx = 200 + EYE * 2, cy = 110;
    const float radius = 58;
    ring(cx, cy, radius, 6, 0, 2 * M_PI, C(0xffffff, .14f * sa));
    u32 rc = done ? (ij->warnings ? 0xf2b632 : 0x5cc29f) : fail ? 0x777777 : 0xff8a3c;
    if (p > .005f)
        ring(cx, cy, radius, 6, -M_PI / 2, -M_PI / 2 + 2 * M_PI * p, C(rc, sa));
    float pop = done ? 1 + .18f * sinf(clampf((NOW - done_t) / .7f, 0, 1) * M_PI) : 1;
    rrect(cx - 34 * pop, cy - 34 * pop, 68 * pop, 68 * pop, 15, C(0xffffff, .92f * sa));
    icon_at(a, cx - 32 * pop, cy - 32 * pop, 64, pop, sa);
    if (done) {
        float k = ease_back(span(NOW, done_t + .16f, .52f));
        circle(cx + 32, cy + 32, 11 * k, C(0xffffff, sa));
        circle(cx + 32, cy + 32, 9 * k, C(ij->warnings ? AMBER : MINT, sa));
        draw_icon(ij->warnings ? "warn" : "check", cx + 32, cy + 32, 12 * k, C(0xffffff, sa));
    }
    char pct[16];
    snprintf(pct, sizeof pct, "%d %%", (int)roundf(p * 100));
    text_ex(pct,200,178,22, 800, 1, -.5f, C(0xffffff, sa), AL_CENTER);
    JobStep *st = ij->cur < ij->nsteps ? &ij->steps[ij->cur] : NULL;
    if (fail && ij->error[0])
        text_fit(ij->error,10,204,10,380,C(0xffb0a8,sa));
    else if (st && !done)
        text_fit_style(st->label,20,205,10,800,360,C(0xffffff,sa));
    char line[96], a1[24], a2[24];
    if (st && st->bytes && ij->state == JOB_RUNNING && ij->speed > 1) {
        fmt_size(a1, sizeof a1, ij->speed);
        float eta = (ij->total_bytes - ij->done_bytes) / fmaxf(ij->speed, 1);
        snprintf(a2, sizeof a2, eta < 60 ? "%.0f s" : "%.0f min", eta < 60 ? eta : eta / 60);
        snprintf(line, sizeof line, "%s/s · %s", a1, a2);
    } else {
        fmt_size(a1, sizeof a1, (double)ij->done_bytes);
        fmt_size(a2, sizeof a2, (double)ij->total_bytes);
        snprintf(line, sizeof line, "%s / %s", a1, a2);
    }
    if(!ij->uninstall)text_style(line,200,221,10,700,0,C(0xffffff,.9f*sa),AL_CENTER);
    if (done) {
        float f = 1 - ease_out(span(NOW, done_t, .7f));
        if (f > 0)
            rect(0, 0, TOP_W, SCR_H, C(0xfff6e0, .5f * f * sa));
    }
}

static void install_bottom(float sa) {
    draw_bg(false, 1);
    App *a = ij->app;
    icon_at(a, 7, 6, 20, 1, sa);
    text_fit_style(a->title,31,9,11,700,240,C(TH.ink,sa));
    char cnt[16];
    int done_n = 0;
    for (int i = 0; i < ij->nsteps; i++)
        done_n += ij->steps[i].state >= STEP_DONE;
    snprintf(cnt, sizeof cnt, "%d/%d", done_n, ij->nsteps);
    if(!ij->uninstall)text_dyn(cnt,312,10,10,C(TH.ink_faint,sa),AL_RIGHT);
    float top=30,view=174,extra_h=0;char next_text[512]="";
    install_check_y=-1000;
    if(ij->state==JOB_DONE&&ij->warnings&&!ij->uninstall&&install_missing_req){
        const char *path=json_string_value(json_object_get(install_missing_req,"path"));char full[300];snprintf(full,sizeof full,"sdmc:%s",path?path:"");
        tvars(next_text,sizeof next_text,T("install.nextBody"),"label",LJ(json_object_get(install_missing_req,"label")),"path",full);
        extra_h=14+3+text_wrap_h_style(next_text,10,600,286)+5+24+14;view=fmaxf(25,202-extra_h-6-top);
    }
    float y=top-ilog_scroll;
    scissor(BOT_W,0,top,BOT_W,view);
    for(int i=0;i<ij->nsteps;i++) {
        JobStep *st=&ij->steps[i]; float h=install_row_h(st);
        if(y+h<top || y>top+view) { y+=h; continue; }
        float al=sa*(st->state==STEP_PENDING?.8f:1);
        u32 color=st->state==STEP_WARN?AMBER:st->state==STEP_ERROR?DANGER:st->state==STEP_DONE?MINT:FORGE;
        if(st->state==STEP_ACTIVE) { rrect(8,y,304,h-2,8,C(TH.surface,sa)); ring(21,y+15,6.5f,1.6f,NOW*9,NOW*9+4.4f,C(FORGE,sa)); }
        else if(st->state==STEP_PENDING) ring(21,y+15,6.5f,1.4f,0,2*M_PI,C(TH.line,.25f*sa));
        else { circle(21,y+15,6.5f,C(color,sa)); draw_icon(st->state==STEP_WARN?"warn":st->state==STEP_ERROR?"close":"check",21,y+15,8,C(0xffffff,sa)); }
        float ty=y+7+text_wrap_style(st->label,34,y+7,11,600,C(TH.ink,al),242);
        if(st->result[0] && st->state!=STEP_ACTIVE) text_wrap(st->result,34,ty+3,10,C(st->state==STEP_WARN?AMBER:TH.ink_soft,al),268);
        if(st->state==STEP_ACTIVE && (st->bytes || st->progress>0)) {
            rrect(34,ty+3,230,4,2,C(TH.surface3,sa));
            rrect(34,ty+3,fmaxf(1,230*clampf(st->progress,0,1)),4,2,C(FORGE,sa));
            char pct[16]; snprintf(pct,sizeof pct,"%d%%",(int)(100*clampf(st->progress,0,1)));
            text(pct,304,y+4,9,C(TH.ink,sa),AL_RIGHT);
        }
        y+=h;
    }
    if(!extra_h)install_tail(y,sa,true);
    // Full failure/next-step text stays in the same manually scrollable log.
    scissor_off();
    if(extra_h){float ey=202-extra_h;rrect(8,ey,304,extra_h,10,C(TH.surface2,sa));rrect_outline(8,ey,304,extra_h,10,1,C(AMBER,.4f*sa));draw_icon("warn",22,ey+14,10,C(AMBER,sa));text_style(T("install.nextTitle"),31,ey+7,10,800,0,C(AMBER,sa),AL_LEFT);text_wrap_style(next_text,17,ey+24,10,600,C(TH.ink_soft,sa),286);install_check_y=ey+24+text_wrap_h_style(next_text,10,600,286)+5;button(17,install_check_y,82,24,T("detail.simulateCopy"),NULL,NULL,BTN_GHOST,sa);}
    bool running = ij->state == JOB_QUEUED || ij->state == JOB_RUNNING;
    if (running) {
        button(5,207,INSTALL_BROWSE_W,28,T("action.keepBrowsing"), NULL, "B", BTN_GHOST, sa);
        button(INSTALL_CANCEL_X,207,INSTALL_CANCEL_W,28,T("action.cancel"), "close", "X", BTN_SURFACE, sa);
    } else {
        bool can_open=ij->state==JOB_DONE && !ij->uninstall;
        if(can_open) button(5,207,64,28,T("action.open"),"play",NULL,BTN_GHOST,sa);
        button(can_open?74:5,207,can_open?241:310,28, T(ij->state==JOB_DONE?"action.done":"action.back"),ij->state==JOB_DONE?"check":"back",NULL,BTN_PRIMARY,sa);
    }
}

// ======================================================================================================
// SETTINGS
// ======================================================================================================
enum { SET_CONSOLE, SET_THEME, SET_LANG, SET_MUSIC, SET_SFX, SET_VOLUME, SET_FORMAT, SET_CHANNEL, SET_UNIVERSAL, SET_DSP, SET_CACHE, SET_RESET, NSET };
static int sfocus;
static float sscroll, sscroll_target, svelocity;
static bool lang_open;
static bool volume_drag;
static int lang_focus;
static const int lang_order[8] = {1, 0, 2, 3, 4, 5, 6, 7};
static float lang_t;
static const char *SET_KEYS[NSET] = {"settings.console", "settings.theme",   "settings.lang",   "settings.music",
                                     "settings.sfx",     "settings.volume",  "settings.format", "settings.channel",
                                     "settings.sources", "settings.dumpDsp", "settings.videoCache", "settings.reset"};
static const char *SET_ICONS[NSET] = {"cpu", "moon",   "language", "music",  "sparkle", "volume",
                                      "cia", "shield", "globe",    "volume", "trash", "refresh"};
static const u32 SET_COLORS[NSET] = {0x59606a, 0xf2a100, 0x2f8fe0, 0x7c6fd6, 0x9b6fd6, 0xff8a1f,
                                     0x4f9e86, 0x7c6fd6, 0x2f8fe0, 0x2f8fe0, 0x59606a, 0xe5484d};

// Top screen while moving through the options: what is shown leaves quickly, then the
// newly focused option arrives. set_shown is the option currently drawn up there.
static int set_shown, set_leaving;
static float set_t, set_from = 1;
static float settings_top_alpha(float *dy) {
    if (set_leaving) {
        float p = span(NOW, set_t, .08f);
        *dy = -4 * p;
        return set_from * (1 - p);
    }
    float p = bezier(span(NOW, set_t, .24f), .22f, 1, .36f, 1);
    *dy = 7 * (1 - p);
    return p;
}
static void settings_top_sync(void) {
    float dy;
    if (!set_leaving && sfocus != set_shown) {
        set_from = settings_top_alpha(&dy);
        set_leaving = true;
        set_t = NOW;
    }
    if (set_leaving && NOW - set_t >= .08f) {
        set_shown = sfocus;
        set_leaving = false;
        set_t = NOW;
    }
}

static void open_settings(void) {
    sfx("open");
    sfocus = 0;
    set_shown = 0, set_leaving = false, set_t = NOW - 1;
    sscroll = sscroll_target = svelocity = 0;
    lang_open = false;
    volume_drag = false;
    next_scene = SC_SETTINGS;
    trans_phase = 1;
    trans_t = NOW;
}

static Thread dsp_thread;
static volatile bool dsp_done;
static bool dsp_success;
static char dsp_error[128];
static void dsp_task(void *unused) {
    dsp_success = dsp_dump(dsp_error, sizeof dsp_error);
    __atomic_store_n(&dsp_done, true, __ATOMIC_RELEASE);
}
static void set_change(int dir) {
    switch (sfocus) {
    case SET_CONSOLE:
        sfx_pitch("move", -5);
        return;
    case SET_THEME:
        S.dark = dir ? dir > 0 : !S.dark; theme_apply();
        break;
    case SET_LANG:
        lang_open = true;
        lang_focus = S.lang < 2 ? 1 - S.lang : S.lang;
        lang_t = NOW;
        sfx("open");
        return;
    case SET_MUSIC:
        S.music = !S.music;
        S.music ? music_play(2.5f) : music_stop(.6f);
        break;
    case SET_SFX:
        S.sfx = !S.sfx;
        break;
    case SET_VOLUME:
        S.volume = clampf(roundf(S.volume * 10 + dir) / 10, 0, 1);
        break;
    case SET_FORMAT:
        S.cia = dir ? dir < 0 : !S.cia;
        break;
    case SET_CHANNEL:
        S.experimental = dir ? dir > 0 : !S.experimental;
        break;
    case SET_UNIVERSAL:
        catalog_universal(!S.universal);
        break;
    case SET_DSP:
        if (!dsp_thread) {
            s32 p = 0x30;
            svcGetThreadPriority(&p, CUR_THREAD_HANDLE);
            dsp_done = false;
            dsp_thread = threadCreate(dsp_task, NULL, 32 * 1024, p + 2, -2, false);
            toast(T("settings.dspWorking"), MINT);
        }
        return;
    case SET_CACHE:
        video_stop(); cache_clear_wait_video=true;
        return;
    case SET_RESET:
        S.dark = false, S.music = true, S.sfx = true, S.volume = .8f, S.cia = true, S.experimental = false;
        catalog_universal(false);
        theme_apply();
        music_play(1);
        toast(T("settings.resetDone"), MINT);
        break;
    }
    sfx("toggle");
    settings_save();
}

static void settings_update(void) {
    settings_top_sync();
    if (lang_open) {
        if (IN.down & KEY_B || hit(0, 0, 320, 4)) {
            lang_open = false;
            sfx("back");
            return;
        }
        if (IN.repeat & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT)) {
            int d = IN.repeat & KEY_DOWN ? 2 : IN.repeat & KEY_UP ? -2 : IN.repeat & KEY_RIGHT ? 1 : -1;
            lang_focus = (int)clampf(lang_focus + d, 0, 7);
            sfx("move");
        }
        bool pick = IN.down & KEY_A;
        for (int i = 0; i < 8; i++)
            if (hit(i%2?165.6f:16,40+(i/2)*36,i%2?138.4f:145.6f,32))
                lang_focus = i, pick = true;
        if (pick) {
            S.lang = lang_order[lang_focus];
            text_reset();
            settings_save();
            lang_open = false;
            sfx("toggle");
        }
        return;
    }
    if (IN.down & (KEY_B | KEY_START) || hit(4, 4, 28, 28)) {
        sfx("back");
        next_scene = SC_HOME;
        trans_phase = 1;
        trans_t = NOW;
        return;
    }
    if (IN.touch_down) {
        float y = 36 + SET_VOLUME * 38 - sscroll;
        volume_drag = IN.tx >= 248 && IN.ty >= fmaxf(36,y) && IN.ty < y + 34;
        if (volume_drag) sfocus = SET_VOLUME, svelocity = 0, sscroll_target = sscroll;
    }
    if (volume_drag) {
        if (IN.touching)
            S.volume = clampf(ceilf((IN.tx - 257) / 5.f), 0, 10) / 10;  // bar k covers 258+5(k-1) .. +5
        if (IN.touch_up) {
            volume_drag = false;
            settings_save();
        }
        return; // Slider gestures must not scroll or activate the whole row.
    }
    if (IN.touch_down)
        svelocity = 0;
    if (IN.touching && IN.start_y >= 36 && !IN.touch_down) {
        float delta = IN.last_y - IN.ty;
        sscroll_target += delta;
        svelocity = delta / fmaxf(DT, .001f);
        sscroll = sscroll_target;
    } else if (!IN.touching && fabsf(svelocity) > 1) {
        sscroll_target += svelocity * DT;
        svelocity *= expf(-DT * 10);
    }
    sscroll_target = clampf(sscroll_target, 0, fmaxf(0, NSET * 38 + 2 - 204));
    sscroll = lerpf(sscroll, sscroll_target, 1 - expf(-DT * 18));
    if (IN.repeat & (KEY_UP | KEY_DOWN)) {
        int n = (int)clampf(sfocus + (IN.repeat & KEY_DOWN ? 1 : -1), 0, NSET - 1);
        if (n != sfocus) {
            sfocus = n;
            svelocity = 0;
            sfx("move");
            sscroll_target = clampf(sscroll_target, (sfocus + 1) * 38 - 204, sfocus * 38);
            sscroll_target = clampf(sscroll_target, 0, fmaxf(0, NSET * 38 + 2 - 204));
        }
    }
    if (IN.down & KEY_A)
        set_change(0);
    if (IN.repeat & (KEY_LEFT | KEY_RIGHT) && sfocus != SET_RESET && sfocus != SET_CACHE && sfocus != SET_LANG)
        set_change(IN.repeat & KEY_RIGHT ? 1 : -1);
    for (int i = 0; i < NSET; i++) {
        float y = 36 + i * 38 - sscroll;
        if (y < 36 || y > 232)
            continue;
        if (hit(6, y, 308, 34)) {
            sfocus = i;
            set_change(0);
        }
    }
}

// Service queries run off the draw thread; publish the completed snapshot at once.
typedef struct {
    float t,space_t;
    char fw[40], cfw[32], sd_free[24], sd_total[24], battery[12];
    float used;
    u64 total_bytes,free_bytes,store_bytes,user_bytes;
    bool space_complete;
    u8 region, model;
} SystemInfo;
static SystemInfo sys = {.t = -100,.space_t=-100, .model = 0xff}, next_sys;
static Thread sys_thread;
static volatile bool sys_ready;
static bool sys_count_space, sys_space_due;  // due: SD/Downloads was opened, measure once
static json_t *sys_user_paths;
static void sys_task(void *unused) {
    // System version ("11.17.0-50E"); the kernel's FIRM version when it can't be read.
    OS_VersionBin nver, cver;
    if (R_FAILED(osGetSystemVersionData(&nver, &cver)) ||
        R_FAILED(osGetSystemVersionDataString(&nver, &cver, next_sys.fw, sizeof next_sys.fw))) {
        u32 fw = osGetFirmVersion();
        snprintf(next_sys.fw, sizeof next_sys.fw, "%lu.%lu-%lu", GET_VERSION_MAJOR(fw), GET_VERSION_MINOR(fw), GET_VERSION_REVISION(fw));
    }
    FS_ArchiveResource r = {0};
    FSUSER_GetArchiveResource(&r, SYSTEM_MEDIATYPE_SD);
    fmt_size(next_sys.sd_free, sizeof next_sys.sd_free, (double)r.freeClusters * r.clusterSize);
    fmt_size(next_sys.sd_total, sizeof next_sys.sd_total, (double)r.totalClusters * r.clusterSize);
    next_sys.used = r.totalClusters ? 1 - r.freeClusters / (float)r.totalClusters : 0;
    next_sys.total_bytes=(u64)r.totalClusters*r.clusterSize;next_sys.free_bytes=(u64)r.freeClusters*r.clusterSize;
    if(sys_count_space){json_t *installed=json_load_file(DATA_DIR "/installed.json",0,NULL);SdUsage usage=sd_usage_measure(DATA_DIR,installed,sys_user_paths);next_sys.store_bytes=usage.store;next_sys.user_bytes=usage.user;next_sys.space_complete=usage.complete;json_decref(installed);json_decref(sys_user_paths);sys_user_paths=NULL;}
    next_sys.region = 0xff;
    CFGU_SecureInfoGetRegion(&next_sys.region);
    CFGU_GetSystemModel(&next_sys.model);
    s64 luma = 0;
    snprintf(next_sys.cfw, sizeof next_sys.cfw, "—");
    if (R_SUCCEEDED(svcGetSystemInfo(&luma, 0x10000, 0)) && ((luma >> 24) & 255))
        snprintf(next_sys.cfw, sizeof next_sys.cfw, "Luma3DS v%d.%d", (int)((luma >> 24) & 255), (int)((luma >> 16) & 255));
    u8 lvl = 0;
    if (R_SUCCEEDED(MCUHWC_GetBatteryLevel(&lvl)))
        snprintf(next_sys.battery, sizeof next_sys.battery, "%d %%", lvl > 100 ? 100 : lvl);
    else if (R_SUCCEEDED(PTMU_GetBatteryLevel(&lvl)))
        snprintf(next_sys.battery, sizeof next_sys.battery, "%d %%", lvl * 20);
    else
        snprintf(next_sys.battery, sizeof next_sys.battery, "—");
    __atomic_store_n(&sys_ready, true, __ATOMIC_RELEASE);
}
static void sys_refresh(void) {
    bool browsing=scene==SC_SD||scene==SC_DOWNLOADS;
    if (sys_thread) {
        // Walking the card is only worth it while its panel is open: stop it on leaving.
        if(!browsing)sd_usage_cancel(true);
        if (!__atomic_load_n(&sys_ready, __ATOMIC_ACQUIRE) || !thread_done(&sys_thread))
            return;
        sys = next_sys;
    }
    // Measured once each time SD or Downloads is opened, never on a timer: walking
    // thousands of files competes with the music and the downloads for the card.
    bool count=browsing&&sys_space_due&&!install_active();
    if (NOW - sys.t < 3&&!count)
        return;
    sys.t = NOW;
    next_sys = sys;
    sys_count_space=count;
    if(count){sys_space_due=false;sd_usage_cancel(false);next_sys.space_t=NOW;sys_user_paths=json_array();for(int i=0;i<BUILTIN_APPS;i++){size_t k;json_t *req;json_array_foreach(json_object_get(APPS[i].j,"requires"),k,req){const char *path=json_string_value(json_object_get(req,"path"));if(path&&*path){char full[1024];int n=snprintf(full,sizeof full,"%s%s",!strncmp(path,"sdmc:/",6)?"":"sdmc:",path);if(n>0&&(size_t)n<sizeof full)json_array_append_new(sys_user_paths,json_string(full));}}}}
    sys_ready = false;
    s32 p = 0x30;
    svcGetThreadPriority(&p, CUR_THREAD_HANDLE);
    sys_thread = threadCreate(sys_task, NULL, 32 * 1024, p + 2, -2, false);
    if(!sys_thread){json_decref(sys_user_paths);sys_user_paths=NULL;}
}

// ---- settings tiles (.st-tile, rendered with their gradient and glow) ------------------------------
static Sheet TILES;
static json_t *tile_names;
static int tile_pad;

static void tile_draw(const char *kind, u32 color, float x, float y, float a) {
    if (!sheet_ok(&TILES))
        return;
    char name[32];
    snprintf(name, sizeof name, "%s-%06lx", kind, (unsigned long)color);
    size_t i;
    json_t *v;
    json_array_foreach(tile_names, i, v) if (!strcmp(json_string_value(v), name)) {
        draw_img(sheet_img(&TILES, (int)i), x - tile_pad, y - tile_pad, 1, 1, a);
        return;
    }
}

/** Console details (.cs-head, .cs-grid, .cs-sd): the top screen while "Console" is focused. */
static void console_sheet(float a) {
    static const char *MODELS[] = {"Nintendo 3DS", "Nintendo 3DS XL",     "New Nintendo 3DS",
                                   "Nintendo 2DS", "New Nintendo 3DS XL", "New Nintendo 2DS XL"};
    static const char *CODES[] = {"CTR-001", "SPR-001", "KTR-001", "FTR-001", "RED-001", "JAN-001"};
    bool known = sys.model < 6;
    char sub[64];
    snprintf(sub, sizeof sub, IS_NEW3DS ? "%s · %s" : "%s", known ? CODES[sys.model] : "CTR", T("console.c-stick"));
    rrect(12,36,28,28,8,C(0x59606a,a));
    draw_icon("cpu",26,50,18, C(0xffffff, a));
    text_style(known ? MODELS[sys.model] : (IS_NEW3DS ? "New Nintendo 3DS" : "Nintendo 3DS"), 49, 35, 12, 900, 0, C(TH.ink, a), AL_LEFT);
    text_style(sub,49,51,9,800,0,C(TH.ink_faint,a),AL_LEFT);
    const char *regions[] = {"JPN", "USA", "EUR", "AUS", "CHN", "KOR", "TWN"};
    char top_s[40], bot_s[40], apps[8];
    snprintf(top_s, sizeof top_s, "400×240 / 800×240");
    snprintf(bot_s, sizeof bot_s, "320×240 %s", T("console.touch"));
    int installed = 0;
    for (int i = 0; i < NAPPS; i++)
        installed += app_installed_version(&APPS[i]) != NULL;
    snprintf(apps, sizeof apps, "%d", installed);
    const char *facts[][2] = {
        {"console.cpu", IS_NEW3DS ? "ARM11 ×4 · 804 MHz" : "ARM11 ×2 · 268 MHz"},
        {"console.ram", IS_NEW3DS ? "256 MB FCRAM" : "128 MB FCRAM"},
        {"console.gpu", "PICA200 · 268 MHz"},
        {"console.vram", IS_NEW3DS ? "6 MB + 4 MB QTD" : "6 MB"},
        {"console.top", top_s},
        {"console.bottom", bot_s},
        {"console.firmware", sys.fw},
        {"console.cfw", sys.cfw},
        {"console.region", sys.region < 7 ? regions[sys.region] : "—"},
        {"console.language", LANG_NAMES[S.lang]},
        {"console.apps", apps},
        {"console.battery", sys.battery},
    };
    for (int i = 0; i < 12; i++) {
        float x=12+(i%3)*(379.f/3),y=73+(i/3)*33;
        rrect(x, y, 370.f/3, 30, 7, C(TH.glass, TH.glass_a * a));
        rrect_outline(x, y, 370.f/3, 30, 7, 1, C(TH.line, TH.line_a * a));
        text_caps_fit(T(facts[i][0]), x + 5, y + 2, 9, 0, C(TH.ink_faint, a), 113.33f);
        text_fit_style(facts[i][1],x+5,y+15,9,800,113.33f,C(TH.ink,a));
    }
    char line[96];
    tvars(line, sizeof line, T("downloads.free"), "free", sys.sd_free, "total", sys.sd_total);
    text_caps(T("console.sd"),12,208,9,.7f, C(TH.ink_faint, a), AL_LEFT);
    text_style(line,388,208,9,900,0,C(TH.ink,a),AL_RIGHT);
    rrect(12,224,376,6,3, C(TH.surface3, a));
    rrect_hgrad(12,224,fmaxf(1,376*sys.used),6,3, C(FORGE, a), C(FORGE2, a));
}

static Coin COIN_ABOUT;

/** Short date of the catalog in the store's language ("Nov 9, 2026", "9 nov 2026"). */
static void fmt_date(char *out, size_t n, const char *iso) {
    int y = 0, m = 0, d = 0;
    if (!iso || sscanf(iso, "%d-%d-%d", &y, &m, &d) != 3 || m < 1 || m > 12) {
        snprintf(out, n, "%s", iso ? iso : "");
        return;
    }
    static const char *MON[6][12] = {
        {"ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sept", "oct", "nov", "dic"},
        {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"},
        {"jan", "fev", "mar", "abr", "mai", "jun", "jul", "ago", "set", "out", "nov", "dez"},
        {"gen", "feb", "mar", "apr", "mag", "giu", "lug", "ago", "set", "ott", "nov", "dic"},
        {"janv.", "févr.", "mars", "avr.", "mai", "juin", "juil.", "août", "sept.", "oct.", "nov.", "déc."},
        {"Jan.", "Feb.", "März", "Apr.", "Mai", "Juni", "Juli", "Aug.", "Sept.", "Okt.", "Nov.", "Dez."},
    };
    if (S.lang == 1)
        snprintf(out, n, "%s %d, %d", MON[1][m - 1], d, y);
    else if (S.lang == 7)
        snprintf(out, n, "%d年%d月%d日", y, m, d);
    else if (S.lang == 6)
        snprintf(out, n, "%02d.%02d.%d", d, m, y);
    else
        snprintf(out, n, S.lang == 5 ? "%d. %s %d" : "%d %s %d", d, MON[S.lang][m - 1], y);
}

static void settings_top(float sa) {
    draw_bg(true, sa);
    // The focused option's part fades out and in (settings_top_sync). The page title and
    // the About card only take part when the console sheet replaces the whole screen.
    int shown = set_shown;
    float dy, ha = sa * settings_top_alpha(&dy);
    bool whole = shown == SET_CONSOLE || (set_leaving ? sfocus : shown) == SET_CONSOLE;
    if (shown == SET_CONSOLE) {
        DRAW_Y += dy;
        console_sheet(ha);
        DRAW_Y -= dy;
        return;
    }
    if (whole) sa = ha;
    // .pn-title: DM Sans 21 px, 750, letter-spacing -0.8, under the status bar
    text_ex(T("settings.title"), 12, 32 + (22 - line_h(21)) / 2, 21, 750, 1, -.8f, C(TH.ink, sa), AL_LEFT);
    // .st-hero: 52 px tile, 12 px gap, title 13 px + description 9 px (line-height 1.4)
    DRAW_Y += dy;
    tile_draw("hero", SET_COLORS[shown], 12, 64, ha);
    draw_icon(shown == SET_THEME ? (S.dark ? "moon" : "sun") : SET_ICONS[shown], 38, 90, 26, C(0xffffff, ha));
    char key[48];
    snprintf(key, sizeof key, "settings.desc.%s", SET_KEYS[shown] + 9);
    float dh = text_wrap_h(T(key), 11, 312), th = 17 + 3 + dh, ty = 64 + (52 - th) / 2;
    text(T(SET_KEYS[shown]), 76, ty, 13, C(TH.ink, ha), AL_LEFT);
    text_wrap(T(key), 76, ty + 20, 11, C(TH.ink_soft, ha), 312);
    DRAW_Y -= dy;
    // .st-about card: 34 px coin, brand, catalog line and sources
    float cy = 166;
    rrect(12, cy + 1, 376, 64, 12, C(0x000000, .08f * sa));
    glass(12, cy, 376, 64, 12, sa);
    coin_draw(&COIN_ABOUT, 38, cy + 32, wobble_pose(NOW, 5.2f), 1, sa);
    float bw = text_ex_w("forge.", 15, 750, 1, -.5f);
    text_ex("forge.", 65, cy + 8, 15, 750, 1, -.5f, C(TH.ink, sa), AL_LEFT);
    text_ex("store", 65 + bw - .5f, cy + 8, 15, 400, 1, -.5f, C(TH.ink, sa), AL_LEFT);
    json_t *store = json_object_get(CATALOG, "store");
    char rev[8], n[8], date[32], about[96];
    snprintf(rev, sizeof rev, "%d", (int)json_integer_value(json_object_get(store, "revision")));
    snprintf(n, sizeof n, "%d", NAPPS);
    fmt_date(date, sizeof date, json_string_value(json_object_get(store, "updated")));
    tvars(about, sizeof about, T("settings.about"), "rev", rev, "n", n);
    char *dpos = strstr(about, "{date}");
    if (dpos) {
        char tail[64];
        snprintf(tail, sizeof tail, "%s", dpos + 6);
        snprintf(dpos, sizeof about - (dpos - about), "%s%s", date, tail);
    }
    text_fit(about,65,cy+28,10,315,C(TH.ink_soft,sa));
    float chx=65,chy=cy+45;
    chx += chip(chx, chy, "Esteban's Forge", "check", C(TH.mint, 1), C(TH.mint, .16f), sa) + 3;
    chip(chx, chy, "Universal-DB", S.universal ? "check" : "globe", C(S.universal ? TH.mint : TH.ink_soft, 1), C(TH.surface2, 1), sa);
}

static void settings_bottom(float sa) {
    draw_bg(false, 1);
    back_button(sa);
    text_style(T("settings.title"),38,10,12,900,0,C(TH.ink, sa), AL_LEFT);
    scissor(BOT_W,0,36,BOT_W,204);
    for (int i = 0; i < NSET; i++) {
        float y = 36 + i * 38 - sscroll;
        if (y + 34 < 36 || y > 240)
            continue;
        bool on = i == sfocus;
        rrect(6, y, 308, 34, 8, C(TH.surface, sa));
        rrect_outline(6, y, 308, 34, 8, on ? 1.5f : 1, on ? C(FORGE, sa) : C(TH.line, TH.line_a * sa));
        rrect(10,y+7,20,20, 6, C(SET_COLORS[i], sa));
        draw_icon(i == SET_THEME ? (S.dark ? "moon" : "sun") : SET_ICONS[i], 20, y + 17, 12, C(0xffffff, sa));
        text_fit_style(T(SET_KEYS[i]),37,y+10,11,600,157,C(TH.ink,sa));
        char v[48] = "";
        bool sw = false, swon = false;
        switch (i) {
        case SET_CONSOLE:
            snprintf(v, sizeof v, "%s", IS_NEW3DS ? "New 3DS" : "Old 3DS");
            break;
        case SET_THEME:
            snprintf(v, sizeof v, "%s", T(S.dark ? "settings.theme.dark" : "settings.theme.light"));
            break;
        case SET_LANG:
            snprintf(v, sizeof v, "%s", LANG_NAMES[S.lang]);
            break;
        case SET_MUSIC:
            sw = true, swon = S.music;
            break;
        case SET_SFX:
            sw = true, swon = S.sfx;
            break;
        case SET_FORMAT:
            snprintf(v, sizeof v, "%s", S.cia ? "CIA" : "3DSX");
            break;
        case SET_CHANNEL:
            snprintf(v, sizeof v, "%s", T(S.experimental ? "channel.experimental" : "channel.stable"));
            break;
        case SET_UNIVERSAL:
            sw = true;
            swon = S.universal;
            break;
        case SET_CACHE:
            if(media_clearing()||!PKG.items) snprintf(v,sizeof v,"…");
            else snprintf(v,sizeof v,"%d %%",(int)(PKG.progress*100));
            break;
        case SET_DSP:
            snprintf(v, sizeof v, "%s",
                     T(dsp_thread                                    ? "settings.dspWorking"
                       : file_view_nonempty(dsp_view) ? "settings.dspFound"
                                                                     : "settings.run"));
            break;
        case SET_RESET:
            snprintf(v, sizeof v, "%s", T("settings.run"));
            break;
        default:
            break;
        }
        if (sw) {
            rrect(280, y + 10, 26, 15, 7.5f, C(swon ? TH.mint : TH.surface3, sa));
            circle(swon ? 298.5f : 287.5f, y + 17.5f, 5.5f, C(0xffffff, sa));
        } else if (i == SET_VOLUME) {
            // Ten 3 px bars on a 5 px pitch, 4 to 13 px tall, as plain rectangles on whole
            // pixels: every gap is 2 px and every step is 1 px.
            int lit = (int)roundf(S.volume * 10), base = (int)roundf(y) + 24;
            for (int k = 0; k < 10; k++)
                rect(258 + k * 5, base - (4 + k), 3, 4 + k, C(k < lit ? FORGE : TH.surface3, sa));
        } else if (v[0]) {
            char fitted[48]; fit_label(fitted,sizeof fitted,v,10,94); snprintf(v,sizeof v,"%s",fitted);
            float w=text_w(v,10)+14;
            if (i == SET_CONSOLE)
                text_style(v,306,y+17-line_h(10)/2,10,600,0,C(TH.ink_soft,sa),AL_RIGHT);
            else {
                rrect(296 - w, y + 10, w, 15, 7.5f, C(TH.surface2, sa));
                text_style(v,296-w/2,y+17.5f-line_h(10)/2,10,600,0,C(TH.ink,sa),AL_CENTER);
                bool cycle = i == SET_THEME || i == SET_FORMAT || i == SET_CHANNEL; // ‹ value › like the prototype
                if (cycle)
                    draw_icon("back", 296 - w - 8, y + 17.5f, 9, C(on ? FORGE : TH.ink_faint, sa));
                if (i != SET_RESET)
                    draw_icon("next", 304, y + 17.5f, 9, C(on ? FORGE : TH.ink_faint, sa));
            }
        }
    }
    scissor_off();
    // Scroll bar
    float content = NSET * 38 + 2, view = 204;
    if (content > view) {
        float bh = view * view / content, by = 36 + (view - bh) * clampf(sscroll / (content - view), 0, 1);
        rrect(315, by, 3, bh, 1.5f, C(TH.line, .2f * sa));
    }
    if (lang_open) {
        float p = ease_out(span(NOW, lang_t, .42f)), y = 4 + 240 * (1 - p);
        rect(0, 0, 320, 240, C(0x0a1216, .42f * sa * p));
        rrect(6, y, 308, 230, 14, C(TH.surface, sa));
        rrect(147, y + 5, 26, 3, 1.5f, C(TH.line, .2f * sa));
        text_style(T("settings.lang"),16,y+15,12,900,0,C(TH.ink,sa),AL_LEFT);
        const char *en[] = {"Spanish", "English", "Portuguese", "Italian", "French", "German", "Russian", "Japanese"};
        for (int i = 0; i < 8; i++) {
            int lang = lang_order[i];
            float ly=y+36+(i/2)*36,lx=i%2?165.6f:16,lw=i%2?138.4f:145.6f;
            rrect(lx,ly,lw,32,10,C(TH.surface2,sa));
            rrect_outline(lx,ly,lw,32,10,i==lang_focus?1.5f:1,
                          C(i == lang_focus ? FORGE : TH.line, (i == lang_focus ? 1 : TH.line_a) * sa));
            char name[96],hint[96];
            fit_label_style(name,sizeof name,LANG_NAMES[lang],10,900,lw-61);
            float hint_width=fmaxf(0,lw-41-text_style_w(name,10,900,0));
            fit_label_style(hint,sizeof hint,en[lang],9,800,hint_width);
            text_style(name,lx+9,ly+8.5f,10,900,0,C(TH.ink,sa),AL_LEFT);
            text_style(hint,lx+lw-26,ly+10,9,800,0,C(TH.ink_faint,sa),AL_RIGHT);
            circle(lx+lw-14.5f,ly+16,5.5f,C(lang==S.lang?FORGE:TH.line,(lang==S.lang?1:.32f)*sa));
            circle(lx+lw-14.5f,ly+16,lang==S.lang?2:4,C(TH.surface2,sa));
        }
    }
}

// ======================================================================================================
// SEARCH (system keyboard) and plumbing
// ======================================================================================================
#include "panels.inc"

static void job_done(Job *j) {
    file_view_refresh();rom_find_refresh();
    bool watching = scene == SC_INSTALL && ij == j;
    if (watching)
        return;
    char msg[96];
    if (j->state == JOB_DONE) {
        if(j->uninstall&&j->warnings)snprintf(msg,sizeof msg,"%s",T("native.uninstallKept"));
        else if(j->uninstall)tvars(msg,sizeof msg,T("toast.uninstalled"),"title",j->app->title,NULL,NULL);
        else if(j->warnings) snprintf(msg,sizeof msg,"%s · %s", j->app->title,T("status.needsFile"));
        else tvars(msg, sizeof msg, T("toast.installed"), "title", j->app->title, NULL, NULL);
        sfx(j->uninstall?"back":"complete");
        toast(msg, j->warnings ? AMBER : MINT);
    } else if (j->state == JOB_FAILED) {
        snprintf(msg, sizeof msg, "%s · %s", T("install.failed"), j->app->title);
        toast(msg, DANGER);
    } else
        toast(T("toast.cancelled"), DANGER);
}

void scenes_init(float intro_delay) {
    dsp_view=file_view_watch("sdmc:/3ds/dspfirm.cdc",true);
    requirement_views_rebuild();
    theme_apply();
    HOME_ICONS.crisp=true;
    sheet_load_now(&HOME_ICONS,"romfs:/gfx/icons-44.ftx");
    sheet_load_now(&BG[(S.dark ? 1 : 0)], S.dark ? "romfs:/gfx/bg-top-dark.ftx" : "romfs:/gfx/bg-top-light.ftx");
    sheet_load_now(&BG[2 + (S.dark ? 1 : 0)], S.dark ? "romfs:/gfx/bg-bottom-dark.ftx" : "romfs:/gfx/bg-bottom-light.ftx");
    sheet_request(&BG[S.dark ? 0 : 1], S.dark ? "romfs:/gfx/bg-top-light.ftx" : "romfs:/gfx/bg-top-dark.ftx");
    sheet_request(&BG[S.dark ? 2 : 3], S.dark ? "romfs:/gfx/bg-bottom-light.ftx" : "romfs:/gfx/bg-bottom-dark.ftx");
    for (int i = 0; i < NCATS; i++)
        if (!strcmp(CATS[i].id, "forge"))
            cat = i;
    coin_load(&COIN_EMBLEM, "emblem", false);
    coin_load(&COIN_ABOUT, "about", false);
    sheet_load_now(&TILES, "romfs:/gfx/tiles.ftx");
    json_t *tj = json_load_file("romfs:/gfx/tiles.json", 0, NULL);
    tile_names = json_object_get(tj, "names");
    tile_pad = (int)json_integer_value(json_object_get(tj, "pad"));
    if (!S.onboarded) { // first launch: the light/dark picker
        coin_load(&COIN_THEME[0], "light", true);
        coin_load(&COIN_THEME[1], "dark", true);
    }
    sys_refresh();
    intro_start(intro_delay);
}

void scenes_update(void) {
    file_view_pump(NOW);
    rom_find_pump(NOW);
    media_package_refresh();
    sd_pump();
    bool clean = scene == SC_DETAIL && dtab == 1 && !video_active() && !sheet_on && !uninstall_on && trans_phase != 1 &&
                 dapp && dapp->shots + nvideo > 0;
    shots_clean = clampf(shots_clean + ((clean ? 1 : 0) - shots_clean) * (1 - powf(.0002f, DT)), 0, 1);
    if (fabsf(shots_clean - (clean ? 1 : 0)) < .004f) shots_clean = clean ? 1 : 0;
    if(scene!=SC_DETAIL&&detail_plan_app){file_view_plan_begin();detail_plan_app=NULL;}
    if(requirement_check_app) {
        bool visible=requirement_check_install?(scene==SC_INSTALL&&ij&&ij->app==requirement_check_app):(scene==SC_DETAIL&&dapp==requirement_check_app);
        if(!visible)requirement_check_app=NULL;
        else if(file_view_completed()>=requirement_check_ticket&&rom_find_completed()>=requirement_check_rom_ticket) {
            bool found;
            if(requirement_check_install){install_missing_req=app_missing_requirement_view(requirement_check_app);found=!install_missing_req;if(found)ij->warnings=0;}
            else found=requirement_view_has(requirement_check);
            toast(T(found?"detail.found":requirement_check_install?"toast.needsFile":"detail.missing"),found?MINT:AMBER);
            sfx(found?"toggle":"move");requirement_check_app=NULL;
        }
    }
    if(scene==SC_SD||scene==SC_DOWNLOADS)sys_refresh();
    if(scene!=SC_GITHUB&&!S.universal)remote_image_trim();
    bool catalog_changed=false;
    if(!test_freeze_media&&media_started){catalog_changed=catalog_sync_pump();catalog_changed|=universal_sync_pump(scene==SC_HOME||scene==SC_SEARCH||scene==SC_SETTINGS);}
    if(catalog_changed){
        requirement_views_rebuild();
        App *selected=nlist&&sel<nlist?list[sel]:NULL;
        home_list();if(selected)for(int i=0;i<nlist;i++)if(list[i]==selected){sel=i;break;}
        if(scene==SC_HOME)home_select(sel,0,true);
        if(scene==SC_SEARCH)search_refresh();
        detail_plan_app=NULL;
    }
    if(scene==SC_INSTALL&&ij){
        if(strcmp(install_backdrop_id,ij->app->id)){sheet_free(&install_backdrop);snprintf(install_backdrop_id,sizeof install_backdrop_id,"%s",ij->app->id);}
        if(install_backdrop.state==SHEET_EMPTY){char p[128];snprintf(p,sizeof p,"romfs:/gfx/install/%s.ftx",ij->app->id);sheet_request(&install_backdrop,p);}
    }else if(install_backdrop.state!=SHEET_EMPTY)sheet_free(&install_backdrop);

    catalog_icons_begin();
    if(scene==SC_HOME){for(int i=page*PER_PAGE;i<nlist&&i<(page+1)*PER_PAGE;i++)catalog_request_icon(list[i]);catalog_request_icon(bn_cur);catalog_request_icon(bn_prev);}
    if(scene==SC_DETAIL)catalog_request_icon(dapp);
    if(scene==SC_SEARCH)for(int i=0;i<search_n;i++)catalog_request_icon(search_results[i]);
    if(scene==SC_INSTALL&&ij)catalog_request_icon(ij->app);
    if(scene==SC_DOWNLOADS)for(int i=0;i<dl_n;i++)if(dl_rows[i].job&&dl_rows[i].y+39>=dl_scroll&&dl_rows[i].y<dl_scroll+198)catalog_request_icon(dl_rows[i].job->app);
    if(scene==SC_SD&&sd_shown)for(int i=0;i<sd_n;i++)if(i*42+39>=sd_scroll&&i*42<sd_scroll+198)catalog_request_icon(sd_entries[i].owner);
    catalog_icons_end();

    if(cache_clear_wait_video && !video_stopping()) { cache_clear_wait_video=false; if(media_clear()) cache_clear_pending=true; else toast(T("media.clearFailed"),DANGER); }
    if(cache_clear_pending) { int done=media_clear_poll(); if(done) { cache_clear_pending=false; toast(T(done>0?"media.cleared":"media.clearFailed"),done>0?MINT:DANGER); } }
    if (music_due >= 0 && NOW >= music_due) {
        music_due = -1;
        music_play(3);
    }
    if(scene==SC_GITHUB && dapp){app_free_shots(dapp);sheet_free(&video_posters);sheet_free(&video_bottom_posters);}
    if(scene==SC_DETAIL){if(video_active()||video_wanted)app_free_shots(dapp);else app_request_shots(dapp);}
    if (scene == SC_DETAIL && nvideo && video_posters.state == SHEET_EMPTY)
        sheet_request(&video_posters, poster_path);
    if (scene == SC_DETAIL && nvideo && video_bottom_posters.state == SHEET_EMPTY) {
        char path[96]; snprintf(path, sizeof path, "romfs:/video/%s-bottom-posters.ftx", dapp->id);
        video_bottom_posters.crisp=true;
        sheet_request(&video_bottom_posters, path);
    }
    if (dsp_thread && __atomic_load_n(&dsp_done, __ATOMIC_ACQUIRE) && thread_done(&dsp_thread)) {
        toast(dsp_success ? T("settings.dspDone") : dsp_error, dsp_success ? MINT : DANGER);
        if (dsp_success && !audio_available()) {
            audio_init();
            audio_load_rest();
            music_play(2.5f);
        }
    }
    install_poll(job_done);
    fx_update(DT);
    if (trans_phase == 1) {
        previous_scene = scene;
        scene = next_scene;
        trans_phase = 2;
        trans_t = NOW;
        if (scene == SC_HOME)
            home_list(), home_select(sel, 0, true), home_enter_t = NOW - 5, music_muffle(0), fx_set(bn_cur ? bn_cur->fx : NULL);
        if (scene == SC_DETAIL)
            music_muffle(.35f), fx_set(dapp->fx);
        if (scene == SC_INSTALL)
            music_muffle(.55f), fx_set(ij->state <= JOB_RUNNING ? "embers" : NULL);
        if (scene == SC_GITHUB)
            music_muffle(.4f);
        if (scene == SC_SETTINGS)
            fx_set(NULL);
    } else if (trans_phase == 2 && NOW - trans_t >= .46f) {
        trans_phase = 0;
        if(previous_scene==SC_DETAIL&&scene!=SC_DETAIL){
            app_free_shots(dapp);
            sheet_free(&video_posters);
    sheet_free(&video_bottom_posters);
    video_wanted = false;
        }
    }
    if (scene != SC_GITHUB)
        gh_poll();
    if (sys_thread || scene == SC_SETTINGS || scene == SC_SD || scene == SC_DOWNLOADS)
        sys_refresh();
    if (trans_phase || launch_t >= 0)  // nothing reacts while the store fades out for a launch
        return;
    switch (scene) {
    case SC_INTRO:
        intro_update();
        break;
    case SC_HOME:
        home_update();
        break;
    case SC_DETAIL:
        detail_update();
        break;
    case SC_INSTALL:
        install_update();
        break;
    case SC_SETTINGS:
        settings_update();
        break;
    case SC_SEARCH:
        search_update();
        break;
    case SC_DOWNLOADS:
        downloads_update();
        break;
    case SC_SD:
        sd_update();
        break;
    case SC_GITHUB:
        github_update();
        break;
    }
}

static void draw_scene_top(int which, float a) {
    switch (which) {
    case SC_INTRO:
        intro_top();
        break;
    case SC_HOME:
        home_top(a);
        break;
    case SC_DETAIL:
        detail_top(a);
        break;
    case SC_INSTALL:
        install_top(a);
        break;
    case SC_SETTINGS:
        settings_top(a);
        break;
    case SC_SEARCH:
        search_top(a);
        break;
    case SC_DOWNLOADS:
        downloads_top(a);
        break;
    case SC_SD:
        sd_top(a);
        break;
    case SC_GITHUB:
        github_top(a);
        break;
    }
}

static void draw_scene_bottom(int which) {
    switch (which) {
    case SC_INTRO:
        intro_bottom();
        break;
    case SC_HOME:
        home_bottom(1);
        break;
    case SC_DETAIL:
        detail_bottom(1);
        break;
    case SC_INSTALL:
        install_bottom(1);
        break;
    case SC_SETTINGS:
        settings_bottom(1);
        break;
    case SC_SEARCH:
        search_bottom(1);
        break;
    case SC_DOWNLOADS:
        downloads_bottom(1);
        break;
    case SC_SD:
        sd_bottom(1);
        break;
    case SC_GITHUB:
        github_bottom(1);
        break;
    }
}

void scenes_draw_top(void) {
    draw_bg(true, 1);
    if (trans_phase == 2) {
        draw_scene_top(previous_scene, 1);
        draw_scene_top(scene, ease_out(span(NOW, trans_t, .42f)));
    } else
        draw_scene_top(scene, 1);
}

void scenes_draw_bottom(void) {
    draw_bg(false, 1);
    if (trans_phase == 2) {
        bool back = scene == SC_HOME || (previous_scene == SC_DETAIL && scene == detail_parent) ||
                    (previous_scene == SC_INSTALL && scene == install_parent) || (previous_scene == SC_GITHUB && scene == gh_parent);
        float p = ease_out(span(NOW, trans_t, .46f));
        draw_scene_bottom(back ? scene : previous_scene);
        DRAW_Y = 240 * (back ? p : 1 - p);
        draw_scene_bottom(back ? previous_scene : scene);
        DRAW_Y = 0;
    } else
        draw_scene_bottom(scene);
}

const char *scenes_section(void) {
    switch (scene) {
    case SC_DETAIL:
        return T("section.detail");
    case SC_INSTALL:
        return T("section.install");
    case SC_SETTINGS:
        return T("section.settings");
    case SC_SEARCH:
        return T("section.search");
    case SC_DOWNLOADS:
        return T("section.downloads");
    case SC_SD:
        return T("section.sd");
    case SC_GITHUB:
        return T("section.github");
    default:
        return T("section.home");
    }
}

bool scenes_statusbar(void) {
    if (scene == SC_INTRO)
        return false;
    if(video_active())return video_ui_on;
    if (scene == SC_DETAIL && dtab == 1 && !sheet_on && !uninstall_on && dapp && dapp->shots + nvideo > 0)
        return false;  // captures: nothing but the picture and its counter
    if (scene == SC_HOME)
        return NOW > home_enter_t + .9f;
    return true;
}

/** Shutdown, step one: every worker of the scenes is told to stop; nothing waits. */
void scenes_stop(void) {
    file_view_stop();
    rom_find_stop();
    sd_usage_cancel(true);
    __atomic_store_n(&sd_cancel, true, __ATOMIC_RELEASE);
}

/** Shutdown, step two: collect the workers within the shared deadline (common.h). */
void scenes_exit(void) {
    file_view_exit();
    rom_find_exit();
    bool sd_stopped = thread_reap(&sd_thread), sys_stopped = thread_reap(&sys_thread);
    thread_reap(&dsp_thread);
    if (EXIT_ABANDONED) return;  // a worker may still read these
    github_cleanup();
    free(dl_test_jobs);dl_test_jobs=NULL;
    sheet_free(&HOME_ICONS);sheet_free(&install_backdrop);
    if (sd_stopped) { free(sd_entries); sd_entries = NULL; sd_n = sd_alloc = 0; }
    (void)sys_stopped;
}

// Test-only entrypoint called exclusively by an explicitly supplied test.txt.
// Rendering fixtures never enter the installer queue and never write app files.
void scenes_test_view(const char *name) {
    // Explicit motion audit uses live production animation, not frozen fixtures.
    if(!strcmp(name,"motion")){test_freeze_media=false;return;}
    test_freeze_media=true;
    if(!strcmp(name,"scroll-table")){if(scene==SC_GITHUB){gh_layout();for(int i=0;i<gh_document.count;i++)if(gh_document.nodes[i].table){gh_scroll=gh_y[i]-.5f;break;}}return;}
    if(!strcmp(name,"scroll-bottom")){if(scene==SC_DETAIL)dscroll=dscroll_target=fmaxf(0,content_h-159);else if(scene==SC_GITHUB){gh_layout();gh_scroll=fmaxf(0,gh_height-206);}else if(scene==SC_DOWNLOADS)dl_scroll=fmaxf(0,dl_height-198);return;}
    if(!strncmp(name,"scroll-",7)&&isdigit((unsigned char)name[7])){float value=strtof(name+7,NULL);if(scene==SC_DETAIL)dscroll=dscroll_target=clampf(value,0,fmaxf(0,content_h-159));else if(scene==SC_GITHUB)gh_scroll=clampf(value,0,fmaxf(0,gh_height-206));return;}
    if(!strncmp(name,"seek-",5)){if(video_active()){if(!video_paused())video_pause();video_seek(strtof(name+5,NULL));video_ui_show(true);video_ui_until=NOW+100;}return;}
    if(!strcmp(name,"launch-fade")){launch_begin();return;}  // the fade and exit of a launch, without a title
    if(!strcmp(name,"light")||!strcmp(name,"dark")){S.dark=!strcmp(name,"dark");theme_apply();return;}
    if(!strncmp(name,"lang-",5)){for(int i=0;i<8;i++)if(!strcmp(name+5,LANGS[i]))S.lang=i;text_reset();return;}
    video_stop();video_wanted=false;
    trans_phase=0;sheet_on=false;uninstall_on=false;lang_open=false;
    if(!strcmp(name,"home")||!strcmp(name,"home-universal")){scene=SC_HOME;searching=false;for(int i=0;i<NCATS;i++)if(!strcmp(CATS[i].id,!strcmp(name,"home-universal")?"universal":"forge"))cat=i;sel=0;home_enter(false);return;}
    if(!strcmp(name,"search")||!strncmp(name,"search:",7)){open_search();if(name[6]==':'){snprintf(query,sizeof query,"%s",name+7);search_refresh();}scene=SC_SEARCH;trans_phase=0;return;}
    if(!strcmp(name,"downloads")||!strncmp(name,"downloads-",10)){
        dl_test_override=strcmp(name,"downloads")!=0;dl_test_count=0;
        if(dl_test_override&&strcmp(name,"downloads-empty")){
            if(!dl_test_jobs)dl_test_jobs=calloc(6,sizeof *dl_test_jobs);
            if(dl_test_jobs){
                memset(dl_test_jobs,0,6*sizeof *dl_test_jobs);dl_test_count=6;
                const char *ids[]={"alttp","mk64","fbi","doom","starwing","gdiffuser"};
                const JobState states[]={JOB_RUNNING,JOB_QUEUED,JOB_DONE,JOB_DONE,JOB_FAILED,JOB_CANCELLED};
                for(int i=0;i<6;i++){
                    Job *j=&dl_test_jobs[i];for(int a=0;a<NAPPS;a++)if(!strcmp(APPS[a].id,ids[i]))j->app=&APPS[a];
                    if(!j->app)j->app=&APPS[0];
                    j->release=app_release(j->app,false);j->cia=true;j->serial=i+1;j->state=states[i];j->warnings=i==3;
                    if(!strcmp(name,"downloads-history")&&i<2)j->state=JOB_DONE;
                    snprintf(j->tag,sizeof j->tag,"%s",jstr(j->release,"tag"));j->total_bytes=7312320;j->done_bytes=i?7312320:4387392;
                    j->nsteps=1;j->weight[0]=j->wtotal=1;j->steps[0].state=j->state==JOB_RUNNING?STEP_ACTIVE:STEP_PENDING;j->steps[0].progress=i?0:.4f;
                    tvars(j->steps[0].label,sizeof j->steps[0].label,T("install.step.download"),"name","zelda3-3ds.cia",NULL,NULL);
                }
            }
        }
        open_downloads();scene=SC_DOWNLOADS;trans_phase=0;
        if(!strcmp(name,"downloads-bottom"))dl_focus=dl_n-1,dl_scroll=fmaxf(0,dl_height-198);
        return;
    }
    if(!strcmp(name,"sd")){open_sd();scene=SC_SD;trans_phase=0;return;}
    if(!strcmp(name,"sd-fixture")){snprintf(sd_dir,sizeof sd_dir,"sdmc:/parity-fixture/");sd_focus=0;sd_restore[0]=0;sd_confirm=sd_delete_job=false;sd_scan();scene=SC_SD;trans_phase=0;return;}
    if(!strcmp(name,"settings")||!strcmp(name,"languages")){open_settings();scene=SC_SETTINGS;trans_phase=0;lang_open=!strcmp(name,"languages");for(int i=0;i<8;i++)if(lang_order[i]==S.lang)lang_focus=i;lang_t=NOW-1;return;}
    if(!strncmp(name,"github-",7)){
        bool user=!strcmp(name,"github-profile")||!strcmp(name,"github-repos")||!strcmp(name,"github-about");
        const char *target=user?"EstebanPdN":"EstebanPdN/zelda-alttp-3ds";
        if(scene!=SC_GITHUB||gh_user!=user||strcmp(gh_name,target))open_github(target,user);
        scene=SC_GITHUB;trans_phase=0;gh_scroll=0;gh_link=-1;gh_dirty=true;
        gh_tab=!strcmp(name,"github-info")||!strcmp(name,"github-about")?2:!strcmp(name,"github-releases")||!strcmp(name,"github-repos")?1:0;return;
    }
    if(!strcmp(name,"intro")||!strncmp(name,"picker-",7)){
        if(!sheet_ok(&COIN_EMBLEM.wobble))coin_load(&COIN_EMBLEM,"emblem",false);
        scene=SC_INTRO;intro_start(0);
        if(!strncmp(name,"picker-",7)){
            for(int i=0;i<2;i++)if(!sheet_ok(&COIN_THEME[i].wobble))coin_load(&COIN_THEME[i],i?"dark":"light",true);
            picker=true;picker_t=NOW-2;intro_t0=NOW-10;choice=!strcmp(name,"picker-dark");S.dark=choice;theme_apply();opt_scale[0]=choice?.88f:1.06f;opt_scale[1]=choice?1.06f:.88f;
        }return;
    }
    if(!strncmp(name,"install-",8)){
        static Job fixture;memset(&fixture,0,sizeof fixture);fixture.app=&APPS[0];fixture.cia=true;fixture.release=app_release(fixture.app,false);fixture.nsteps=9;fixture.wtotal=9;fixture.serial=1;
        snprintf(fixture.tag,sizeof fixture.tag,"%s",json_string_value(json_object_get(fixture.release,"tag")));
        const char *keys[]={"resolve","mkdir","mkdir","download","verify","installCia","delete","require","dsp"};
        const char *vars[]={"repo","path","path","name",NULL,NULL,"name","label",NULL};
        json_t *asset=app_asset(fixture.app,fixture.release,"cia"),*req=json_array_get(json_object_get(fixture.app->j,"requires"),0);
        const char *values[]={fixture.app->repo,"/3ds/Zelda 3DS/","/3ds/Zelda 3DS/mods/",json_string_value(json_object_get(asset,"name")),NULL,NULL,"zelda3-3ds.cia",LJ(json_object_get(req,"label")),NULL};
        bool queued=!strcmp(name,"install-queued"),done=!strcmp(name,"install-done")||!strcmp(name,"install-missing");
        fixture.state=queued?JOB_QUEUED:done?JOB_DONE:!strcmp(name,"install-error")?JOB_FAILED:!strcmp(name,"install-cancelled")?JOB_CANCELLED:JOB_RUNNING;
        fixture.warnings=!strcmp(name,"install-missing");fixture.cur=done?9:queued?0:3;
        fixture.total_bytes=json_integer_value(json_object_get(asset,"size"));fixture.done_bytes=done?fixture.total_bytes:(u64)(fixture.total_bytes*.6f);
        if(fixture.state==JOB_FAILED)snprintf(fixture.error,sizeof fixture.error,"HTTP 404: asset not found");
        for(int i=0;i<9;i++){char key[80];snprintf(key,sizeof key,"install.step.%s",keys[i]);tvars(fixture.steps[i].label,sizeof fixture.steps[i].label,T(key),vars[i],values[i],NULL,NULL);fixture.weight[i]=1;fixture.steps[i].bytes=i==3;fixture.steps[i].state=i<fixture.cur?STEP_DONE:i==fixture.cur?(fixture.state==JOB_FAILED?STEP_ERROR:fixture.state==JOB_RUNNING?STEP_ACTIVE:STEP_PENDING):STEP_PENDING;fixture.steps[i].progress=i<fixture.cur?1:i==fixture.cur?.6f:0;}
        if(fixture.warnings)fixture.steps[7].state=STEP_WARN;
        open_install(&fixture);scene=SC_INSTALL;trans_phase=0;done_t=NOW-5;return;
    }
    char fixture_id[136]={0};const char *fixture_tab=NULL;
    if(!strncmp(name,"app:",4)){const char *sep=strrchr(name+4,':');if(sep&&sep-name-4>0&&sep-name-4<(int)sizeof fixture_id){memcpy(fixture_id,name+4,sep-name-4);fixture_tab=sep+1;}}
    bool detail=fixture_id[0]||!strncmp(name,"detail-",7)||!strcmp(name,"confirm")||!strcmp(name,"development")||!strcmp(name,"incompatible")||!strncmp(name,"video",5);
    if(detail){
        App *target=NULL;for(int i=0;i<NAPPS;i++)if(!strcmp(APPS[i].id,fixture_id[0]?fixture_id:!strcmp(name,"development")?"sm64ds":!strcmp(name,"incompatible")?"mk64":"alttp")){target=&APPS[i];break;}
        if(!target)target=&APPS[0];
        list[0]=target;nlist=1;sel=0;open_detail();scene=SC_DETAIL;trans_phase=0;detail_parent=SC_HOME;
        if(!strcmp(name,"detail-shots"))dtab=1;
        if(!strcmp(name,"detail-install"))dtab=2;
        if(!strcmp(name,"detail-news"))dtab=3;
        if(fixture_tab)dtab=!strcmp(fixture_tab,"shots")?1:!strcmp(fixture_tab,"install")?2:!strcmp(fixture_tab,"news")?3:0;
        if(!strcmp(name,"confirm")||!strcmp(name,"incompatible")){open_sheet();sheet_t=NOW-1;}
        if(!strncmp(name,"video",5)){dtab=1;shot_i=!strncmp(name,"video-trailer",13)?-nvideo:-1;shot_t=NOW;}
        fx_set(dapp->fx);memset(&detail_nav,0,sizeof detail_nav);return;
    }
}
void scenes_test_log(FILE *file){
    if(!file)return;
    for(unsigned i=0;i<media_count();i++){MediaItem m;if(media_item(i,&m))fprintf(file,"media item=%s/%s state=%d bytes=%u/%u attempts=%u error=%s\n",m.appid,m.id,m.state,m.received,m.total,m.attempts,m.error);}
    fprintf(file,"package state=%d items=%u ready=%u bytes=%llu/%llu\n",PKG.state,PKG.items,PKG.ready,(unsigned long long)PKG.received,(unsigned long long)PKG.bytes);
    fprintf(file,"catalog apps=%d universal=%s\n",NAPPS,universal_sync_status());
    static const char *kinds[]={"intro","home","detail","install","settings","search","downloads","sd","github"};
    fprintf(file,"view kind=%s scroll=%.3f video_time=%.3f\n",kinds[scene],scene==SC_DETAIL?dscroll:scene==SC_GITHUB?gh_scroll:0,video_time());
    if(scene==SC_DOWNLOADS){fprintf(file,"queue focus=%d scroll=%.3f rows=",dl_focus,dl_scroll);for(int i=0;i<dl_n;i++)if(dl_rows[i].job){Job *j=dl_rows[i].job;fprintf(file,"%s:%d:%d,",j->app->id,j->state,j->warnings);}fprintf(file,"\n");}
    if(scene==SC_GITHUB){int images=0,resident=0;for(int i=0;i<gh_document.count;i++){GhNode *n=&gh_document.nodes[i];if(n->kind==GH_IMAGE&&gh_y[i]+gh_h[i]>gh_scroll&&gh_y[i]<gh_scroll+206){images++;resident+=remote_image_ready(n->url);}}fprintf(file,"assets visible=%d resident=%d\n",images,resident);}
    fprintf(file,"capture scene=%s id=%s tab=%d media=%d playing=%d paused=%d queued_videos=%u reader_loading=%d reader_nodes=%d reader_error=%s\n",scenes_section(),scene==SC_DETAIL&&dapp?dapp->id:"-",scene==SC_DETAIL?dtab:scene==SC_GITHUB?gh_tab:-1,scene==SC_DETAIL?shot_i:0,video_active(),video_paused(),media_count(),scene==SC_GITHUB&&gh_loading,scene==SC_GITHUB?gh_document.count:0,scene==SC_GITHUB?gh_error:"");
    App *selected=scene==SC_DETAIL?dapp:scene==SC_HOME&&nlist&&sel<nlist?list[sel]:NULL;
    bool exp=scene==SC_DETAIL?sheet_exp:S.experimental,cia=scene==SC_DETAIL?sheet_cia:S.cia;
    json_t *release=selected?app_release(selected,exp):NULL;
    fprintf(file,"state app=%s tag=%s dark=%d lang=%s format=%s channel=%s query=%s sd=%s\n",selected?selected->id:"-",release?jstr(release,"tag"):"-",S.dark,LANGS[S.lang],cia?"cia":"3dsx",exp?"experimental":"stable",query,scene==SC_SD?sd_dir:"-");
}
