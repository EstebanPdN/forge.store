// forge.store for Nintendo 3DS: start-up, main loop, status bar, toasts, the
// performance overlay (L+SELECT), the test-build memory dump (SELECT) and the scripted
// test harness used with Azahar.
//
// Start-up: both screens stay black while everything loads, then the grey dotted
// background fades in and only after that the intro builds itself up.
#include <sys/stat.h>
#include <time.h>

#include "audio.h"
#include "catalog.h"
#include "fx.h"
#include "gfx.h"
#include "install.h"
#include "scenes.h"
#include "video.h"
#include "media.h"
#include "remote_image.h"
#include "universal_sync.h"
#include "dsp_dump.h"
#include "debug_dump.h"
#include "store_update.h"
#include "update_icon.h"
#include "github_prefetch.h"
#include "forge_mode.h"

#define BOOT_FADE 0.7f  // black -> background, before the intro starts

Input IN;
float NOW, DT;
bool IS_NEW3DS;
volatile bool BG_HOLD;
u64 EXIT_DEADLINE_MS;
bool EXIT_ABANDONED;

static C3D_RenderTarget *top_l, *top_r, *bottom;
static bool mcu_ok, show_perf;
static float boot_ms;

// ---- toasts -------------------------------------------------------------------------------------
typedef struct {
    char text[112];
    u32 color;
    float t0;
} Toast;
static Toast toasts[3];
static int ntoasts;

void toast(const char *s, u32 color) {
    if (ntoasts == 3) memmove(toasts, toasts + 1, 2 * sizeof(Toast)), ntoasts--;
    Toast *t = &toasts[ntoasts++];
    snprintf(t->text, sizeof t->text, "%s", s);
    t->color = color;
    t->t0 = NOW;
}

static void toasts_update(void) {
    while (ntoasts && NOW - toasts[0].t0 > 2.9f) memmove(toasts, toasts + 1, --ntoasts * sizeof(Toast));
}

void toasts_draw(void) {
    float y = 34;  // under the status bar
    for (int i = 0; i < ntoasts; i++) {
        Toast *t = &toasts[i];
        float in = ease_out(span(NOW, t->t0, .36f)), out = ease_in(span(NOW, t->t0 + 2.6f, .26f));
        float a = in * (1 - out);
        if (a <= 0) continue;
        float w=fminf(384,text_w(t->text,10)+34), h=fmaxf(26,text_wrap_h(t->text,10,w-34)+10), x = 200 - w / 2 + EYE * 1.5f, ty = y - 10 * (1 - in) - 6 * out;
        rrect(x, ty + 1.5f, w,h,13,rgb(0x000000, .14f * a));
        rrect(x, ty, w,h,13,rgb(TH.surface, .97f * a));
        circle(x + 13, ty + 13, 9, rgb(t->color, a));
        draw_icon(t->color == DANGER ? "close" : t->color == AMBER ? "warn" : "check", x + 13, ty + 13, 11, rgb(0xffffff, a));
        text_wrap(t->text,x+27,ty+5,10,rgb(TH.ink,a),w-34);
        y+=(h+4)*a;
    }
}

// ---- status bar (top screen) -----------------------------------------------------------------------
static float sb_a;
// Only an explicit test.txt can enable these visual fixtures.
static bool test_battery;
static int test_battery_pct;
static bool test_battery_charging;
static struct {
    float t;
    int pct;
    bool charging;
    u8 wifi;
    char clock[8];
} sys = {.t = -100};

static void status_refresh(void) {
    if (NOW - sys.t < 2) return;
    sys.t = NOW;
    u8 lvl = 0;
    if (mcu_ok && R_SUCCEEDED(MCUHWC_GetBatteryLevel(&lvl))) sys.pct = lvl > 100 ? 100 : lvl;
    else if (R_SUCCEEDED(PTMU_GetBatteryLevel(&lvl))) sys.pct = lvl * 20;
    u8 ch = 0;
    PTMU_GetBatteryChargeState(&ch);
    bool plugged = false;
    PTMU_GetAdapterState(&plugged);
    sys.charging = ch || plugged;
    if(test_battery){sys.pct=test_battery_pct;sys.charging=test_battery_charging;}
    sys.wifi = osGetWifiStrength();
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    snprintf(sys.clock, sizeof sys.clock, "%02d:%02d", tm->tm_hour, tm->tm_min);
}

/** Lowercase for the section name (ASCII, Latin-1 accents and Cyrillic). */
static void lower_utf8(char *out, size_t n, const char *s) {
    size_t o = 0;
    for (const u8 *p = (const u8 *)s; *p && o + 2 < n; p++) {
        u8 c = *p;
        if (c >= 'A' && c <= 'Z') c += 32;
        else if (c == 0xC3 && p[1] >= 0x80 && p[1] <= 0x9E && p[1] != 0x97) {
            out[o++] = c, out[o++] = p[1] + 0x20, p++;
            continue;
        } else if (c == 0xD0 && p[1] >= 0x90 && p[1] <= 0x9F) {
            out[o++] = c, out[o++] = p[1] + 0x20, p++;
            continue;
        } else if (c == 0xD0 && p[1] >= 0xA0 && p[1] <= 0xAF) {
            out[o++] = 0xD1, out[o++] = p[1] - 0x20, p++;
            continue;
        }
        out[o++] = c;
    }
    out[o] = 0;
}

// Native status bar. Left: the emblem, the wordmark (the largest
// text) and the section after a hairline, sharing the wordmark's baseline. Right: signal,
// clock, battery and percentage, centred on their actual raster bounds.
#define SB_Y 6
#define SB_H 24
#define SB_RIGHT_H 23  // Odd height gives equal margins around 9 px digits / 11 px battery.
#define SB_DIGITS 12

static void status_battery_draw(float x,float cy,int percent,bool charging,float a){
    x=roundf(x);
    u32 ink=rgb(TH.ink,a);
    // 21x11 body with a 3 px radius around a 1 px gap, so the charge never touches the rim.
    float top=roundf(cy-5.5f);
    rrect_outline(x,top,21,11,3,1,rgb(TH.ink,.55f*a));
    rrect(x+22,top+4,2,3,1,rgb(TH.ink,.55f*a));
    int fill=(int)roundf(17*clampf(percent/100.f,0,1));
    u32 color=charging?rgb(0x2fb574,a):percent<=20?rgb(DANGER,a):ink;
    if(fill)rrect(x+2,top+2,fmaxf(fill,3),7,1.5f,color);
}

/** Wi-Fi as three solid bands of a 90 degree fan, antialiased per pixel (16x12). */
static void wifi_draw(float x, float y, int bars, float a) {
    static float cover[12][16];
    static bool ready;
    static signed char band[12][16];
    if (!ready) {
        const float r0[3] = {0, 4.6f, 8.6f}, r1[3] = {2.9f, 7.0f, 11.6f};
        for (int py = 0; py < 12; py++)
            for (int px = 0; px < 16; px++) {
                int hits[3] = {0};
                for (int sy = 0; sy < 4; sy++)
                    for (int sx = 0; sx < 4; sx++) {
                        float dx = px + (sx + .5f) / 4 - 8, dy = 11.8f - (py + (sy + .5f) / 4);
                        float r = sqrtf(dx * dx + dy * dy);
                        if (dy <= 0 || fabsf(dx) > dy) continue;  // inside the 90 degree fan
                        for (int k = 0; k < 3; k++) if (r >= r0[k] && r <= r1[k]) hits[k]++;
                    }
                int k = hits[2] ? 2 : hits[1] ? 1 : 0;
                band[py][px] = hits[k] ? k : -1;
                cover[py][px] = hits[k] / 16.f;
            }
        ready = true;
    }
    for (int py = 0; py < 12; py++)
        for (int px = 0; px < 16; px++) {
            int k = band[py][px];
            if (k < 0) continue;
            float on = k < bars ? 1 : .28f;
            rect(x + px, y + py, 1, 1, rgb(TH.ink, cover[py][px] * on * a));
        }
}

static void statusbar_draw(void) {
    float a = ease_out(sb_a);
    if (a <= 0.01f) return;
    float y = roundf(SB_Y - 8 * (1-a)), cy = y + SB_H / 2;
    u32 ink=rgb(TH.ink,a), soft=rgb(TH.ink_soft,a);
    char sec[48], pct[8]; lower_utf8(sec,sizeof sec,scenes_section());
    snprintf(pct,sizeof pct,"%d%%",sys.pct);
    bool dl=scenes_downloading();

    // Left pill.
    float forge_w=ceilf(text_style_w("forge.",15,750,FONT_BRAND));
    float brand=forge_w+ceilf(text_style_w("store",15,400,FONT_BRAND));
    float secw=ceilf(text_style_w(sec,15,400,FONT_BRAND));
    float bx=37, divx=bx+brand+4, sx=divx+5;
    float lw=fminf(sx+secw+12-8, 240);
    rrect(8,y,lw,SB_H,SB_H/2,rgb(TH.glass,0.9f*a));
    draw_img(ui_img("sb-emblem"),13,y+3,1,1,a);
    float ty=cy-line_h(15)/2, base=ty+text_base(15,750,FONT_BRAND);
    text_style("forge.",bx,ty,15,750,FONT_BRAND,ink,AL_LEFT);
    text_style("store",bx+forge_w,ty,15,400,FONT_BRAND,ink,AL_LEFT);
    rect(divx,cy-6,1,12,rgb(TH.ink,.22f*a));
    scissor(TOP_W,sx,y,8+lw-sx-8,SB_H);
    text_style(sec,sx,base-text_base(15,400,FONT_BRAND),15,400,FONT_BRAND,soft,AL_LEFT);
    scissor_off();

    // Right pill.
    float cw=ceilf(text_status_w(sys.clock,SB_DIGITS)), pw=ceilf(text_status_w(pct,SB_DIGITS));
    float rw=11+(dl?13+9:0)+16+9+cw+11+pw+5+24+10;
    float rx=TOP_W-8-rw, tx=rx+11;
    float rcy=y+SB_RIGHT_H/2.0f;
    rrect(rx,y,rw,SB_RIGHT_H,SB_RIGHT_H/2.0f,rgb(TH.glass,0.9f*a));
    float dy=text_ink_top(sys.clock,rcy,SB_DIGITS,600,FONT_UI);
    if(dl){ draw_icon("download",tx+6.5f,cy,13,rgb(FORGE,a)); tx+=13+9; }
    wifi_draw(tx,cy-6,sys.wifi,a); tx+=16+9;
    text_status(sys.clock,tx,dy,SB_DIGITS,ink); tx+=cw+11;
    // Reviewed optical position: 1 px right of the equal-gap position.
    status_battery_draw(tx-2,rcy,sys.pct,sys.charging,a); tx+=24+5;
    text_status(pct,tx,text_ink_top(pct,rcy,SB_DIGITS,600,FONT_UI),SB_DIGITS,soft);
}

// ---- performance overlay ----------------------------------------------------------------------------
static struct {
    float acc, cpu_sum, cpu_max, gpu_max;
    int frames, late;
    // last full second
    float fps, cpu, cpu_peak, gpu_peak;
    int late_s, late_total;
} perf;
static FILE *test_log;

static void perf_frame(float real_dt, float cpu_ms) {
    perf.acc += real_dt;
    perf.frames++;
    perf.cpu_sum += cpu_ms;
    if (cpu_ms > perf.cpu_max) perf.cpu_max = cpu_ms;
    float g = C3D_GetDrawingTime();
    if (g > perf.gpu_max) perf.gpu_max = g;
    if (real_dt > 1.5f / 59.83f) perf.late++, perf.late_total++;
    if (perf.acc < 1) return;
    perf.fps = perf.frames / perf.acc;
    perf.cpu = perf.cpu_sum / perf.frames;
    perf.cpu_peak = perf.cpu_max;
    perf.gpu_peak = perf.gpu_max;
    perf.late_s = perf.late;
    if (test_log) {
        fprintf(test_log, "t=%5.1f fps=%4.1f cpu=%4.1fms peak=%4.1fms gpu_peak=%4.1fms late=%d linear_free=%luKB\n", NOW, perf.fps, perf.cpu,
                perf.cpu_peak, perf.gpu_peak, perf.late, linearSpaceFree() / 1024);
        audio_log(test_log);
        fprintf(test_log,"store update state=%d progress=%.3f\n",store_update_state(),store_update_progress());
        if (media_busy()) fprintf(test_log, "media phase=%s bytes=%u/%u\n", media_verifying() ? "verify" : "download", media_received(), media_total());
        if (video_active()) fprintf(test_log, "video time=%.3f paused=%d muted=%d\n", video_time(), video_paused(), video_muted());
        fflush(test_log);
    }
    perf.acc = perf.cpu_sum = perf.cpu_max = perf.gpu_max = 0;
    perf.frames = perf.late = 0;
}

#if FORGE_DEBUG
static void perf_report(FILE *f) {
    fprintf(f, "perf fps=%.1f cpu=%.1f ms peak=%.1f ms gpu_peak=%.1f ms late=%d late_total=%d\n", perf.fps, perf.cpu, perf.cpu_peak,
            perf.gpu_peak, perf.late_s, perf.late_total);
}
#endif

static void perf_draw(void) {
    char line[128];
    snprintf(line, sizeof line, "%.0f fps · CPU %.1f ms (máx %.1f) · GPU %.1f ms · lentos %d/%d · carga %.0f ms", perf.fps, perf.cpu, perf.cpu_peak,
             perf.gpu_peak, perf.late_s, perf.late_total, boot_ms);
    float w = text_w(line, 8) + 12;
    rrect(4, 220, w, 16, 8, rgb(0x000000, .7f));
    text_dyn(line, 10, 228 - line_h(8) / 2, 8, rgb(perf.late_s ? 0xffb36b : 0x9cf0c8, 1), AL_LEFT);
}

// ---- scripted input for tests (sdmc:/3ds/forge-store/test.txt) ----------------------------------
// One command per line, "frame:command": kA (press a key: A B X Y L R S=start E=select U D
// I=left T=right), t160,62 (tap), h160,62,30 (hold), d10,100,300,100,20 (drag), sname
// (save both screens to shots/name.raw), q (quit).
// b82,0 overrides battery percent/charging for visual review; b-1 restores hardware.
typedef struct {
    int frame;
    char cmd;
    int a[5];
    char name[40];
} Cmd;
static Cmd cmds[256];
static int ncmds;
static char shot_name[40];
static bool quit_req;
static void update_test_view(int state);

static void test_load(void) {
    FILE *f = fopen(DATA_DIR "/test.txt", "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof line, f) && ncmds < 256) {
        char *colon = strchr(line, ':');
        if (!colon || !colon[1]) continue;
        Cmd *c = &cmds[ncmds];
        memset(c, 0, sizeof *c);
        c->frame = atoi(line);
        c->cmd = colon[1];
        if (c->cmd == 'k' || c->cmd == 's' || c->cmd == 'v') sscanf(colon + 2, "%39s", c->name);
        else sscanf(colon + 2, "%d,%d,%d,%d,%d", &c->a[0], &c->a[1], &c->a[2], &c->a[3], &c->a[4]);
        ncmds++;
    }
    fclose(f);
    if (!ncmds) return;
    mkdir(DATA_DIR "/shots", 0777);
    test_log = fopen(DATA_DIR "/log.txt", "w");
}

static u32 key_bit(char k) {
    switch (k) {
        case 'A': return KEY_A;
        case 'B': return KEY_B;
        case 'X': return KEY_X;
        case 'Y': return KEY_Y;
        case 'L': return KEY_L;
        case 'R': return KEY_R;
        case 'S': return KEY_START;
        case 'E': return KEY_SELECT;
        case 'U': return KEY_DUP;
        case 'D': return KEY_DDOWN;
        case 'I': return KEY_DLEFT;
        case 'T': return KEY_DRIGHT;
    }
    return 0;
}

/** Script state for this frame: held keys and an optional touch point. */
static u32 script_apply(int frame, bool *touch, int *tx, int *ty) {
    u32 held = 0;
    for (int i = 0; i < ncmds; i++) {
        Cmd *c = &cmds[i];
        int f = frame - c->frame;
        switch (c->cmd) {
            case 'k':
                if (f >= 0 && f < 3) held |= key_bit(c->name[0]);
                break;
            case 't':
                if (f >= 0 && f < 3) *touch = true, *tx = c->a[0], *ty = c->a[1];
                break;
            case 'h':
                if (f >= 0 && f < c->a[2]) *touch = true, *tx = c->a[0], *ty = c->a[1];
                break;
            case 'd': {
                int n = c->a[4] > 0 ? c->a[4] : 10;
                if (f >= 0 && f <= n) *touch = true, *tx = c->a[0] + (c->a[2] - c->a[0]) * f / n, *ty = c->a[1] + (c->a[3] - c->a[1]) * f / n;
                break;
            }
            case 'v':
                if(f==0)scenes_test_view(c->name);
                break;
            case 'u':
                if(f==0)update_test_view(c->a[0]); // Explicit test.txt only; never installs a fixture.
                break;
            case 'b':
                if(f==0){
                    test_battery=c->a[0]>=0;
                    test_battery_pct=(int)clampf(c->a[0],0,100);
                    test_battery_charging=c->a[1]!=0;
                    if(test_battery){sys.pct=test_battery_pct;sys.charging=test_battery_charging;}
                    else sys.t=-100;
                }
                break;
            case 's':
                if (f == 0) snprintf(shot_name, sizeof shot_name, "%s", c->name);
                break;
            case 'q':
                if (f >= 0) quit_req = true;
                break;
        }
    }
    return held;
}

// ---- input --------------------------------------------------------------------------------------------
static void read_input(int frame) {
    static u32 script_prev;
    static bool was_touching;
    hidScanInput();
    u32 held = hidKeysHeld(), down = hidKeysDown(), rep = hidKeysDownRepeat();
    touchPosition tp;
    hidTouchRead(&tp);
    bool touching = held & KEY_TOUCH;
    int tx = tp.px, ty = tp.py;
    if (ncmds) {
        u32 sk = script_apply(frame, &touching, &tx, &ty);
        held |= sk;
        down |= sk & ~script_prev;
        rep |= sk & ~script_prev;
        script_prev = sk;
    }
    IN.held = held;
    IN.down = down;
    IN.repeat = rep;
    IN.touch_down = touching && !was_touching;
    IN.touch_up = !touching && was_touching;
    if (IN.touch_down) {
        IN.dragged = false;
        IN.start_x = IN.last_x = tx;
        IN.start_y = IN.last_y = ty;
        IN.touch_t0 = NOW;
    }
    if (touching) IN.tx = tx, IN.ty = ty;
    if (touching && (abs(tx - IN.start_x) >= 10 || abs(ty - IN.start_y) >= 10)) IN.dragged = true;
    IN.touching = touching;
    was_touching = touching;
}

// ---- test builds: SELECT saves a dump, R+SELECT adds the linear heap ---------------------------------
#if FORGE_DEBUG
static int dump_wanted;  // 1 standard, 2 full; taken at the top of the next frame
static void debug_keys(void) {
    static bool armed, full;
    if (IN.down & KEY_SELECT) armed = !(IN.held & (KEY_L | KEY_START)), full = (IN.held & KEY_R) != 0;
    if ((IN.held & KEY_SELECT) && (IN.down & ~(KEY_SELECT | KEY_R | KEY_TOUCH))) armed = false;  // a combo, not a dump
    if (armed && !(IN.held & KEY_SELECT)) armed = false, dump_wanted = full ? 2 : 1;
    // The scenes never see SELECT in a test build (it used to open the SD browser).
    IN.down &= ~KEY_SELECT, IN.held &= ~KEY_SELECT, IN.repeat &= ~KEY_SELECT;
}
static void debug_dump_take(void) {
    FILE *info = debug_dump_begin(dump_wanted == 2);
    dump_wanted = 0;
    if (!info) { toast("Volcado: ya hay uno en curso o la SD no responde", AMBER); return; }
    fprintf(info, "build=%ld model=%s uptime=%.1f s boot=%.0f ms\n", (long)FORGE_BUILD_UNIX, IS_NEW3DS ? "New 3DS" : "Old 3DS", NOW, boot_ms);
    perf_report(info);
    fprintf(info, "status battery=%d charging=%d wifi=%d clock=%s\n", sys.pct, sys.charging, sys.wifi, sys.clock);
    audio_log(info);
    scenes_test_log(info);
    debug_dump_commit(info);
    toast("Guardando volcado en la SD…", FORGE);
}
#endif

// ---- frames -----------------------------------------------------------------------------------------
static float cover;  // black over everything during the start-up fade and before a launch

static void black_frame(void) {
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TargetClear(top_l, C2D_Color32(0, 0, 0, 255));
    C2D_TargetClear(top_r, C2D_Color32(0, 0, 0, 255));
    C2D_TargetClear(bottom, C2D_Color32(0, 0, 0, 255));
    C3D_FrameEnd(0);
}

static void save_shot(const char *name, const u8 *top, const u8 *bot) {
    char path[96];
    snprintf(path, sizeof path, DATA_DIR "/shots/%s.raw", name);
    // Copy with the CPU first: emulators only write the GPU's output back to memory when the
    // CPU reads it (a file write straight from the framebuffer can see stale data).
    const size_t nt = 400 * 240 * 3, nb = 320 * 240 * 3;
    u8 *buf = malloc(nt + nb);
    if (!buf) return;
    for (size_t i = 0; i < nt; i++) buf[i] = top[i];
    for (size_t i = 0; i < nb; i++) buf[nt + i] = bot[i];
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(buf, 1, nt + nb, f);
        fclose(f);
    }
    free(buf);
}

// Startup UI owns the screens until checking, confirmation or installation ends.
static bool update_gate=true, preload_gate;
static u64 preload_started;
static float update_settled=-1, update_changed, update_gate_started, intro_epoch;
static int update_focus=1, update_fixture=-1;
static StoreUpdateState update_seen=(StoreUpdateState)-1;
static UpdateIcon update_animation;
static float update_progress_from, update_progress_to;
static float update_progress_space(void){return lerpf(update_progress_from,update_progress_to,ease_inout(span(NOW,update_changed,.34f)));}
static StoreUpdateState update_state(void) {
    return update_fixture>=0?(StoreUpdateState)update_fixture:store_update_state();
}
static const char *update_version(void) {
    return update_fixture>=0?"2026.11.12.3":store_update_version();
}
static void update_test_view(int state) {
    if(state < -1 || state > UPDATE_DECLINED)return;
    if(!update_gate||update_fixture<0||state<0){
        update_seen=(StoreUpdateState)-1;update_gate_started=NOW;update_focus=1;
        update_progress_from=update_progress_to=0;update_animation.initialized=false;
    }
    update_fixture=state;update_gate=true;update_settled=-1;
}
static UpdateIconKind update_icon_kind(StoreUpdateState state){
    if(state==UPDATE_CHECKING)return UI_SPIN;
    if(state==UPDATE_CURRENT||state==UPDATE_DONE)return UI_CHECK;
    if(state==UPDATE_DOWNLOADING||state==UPDATE_INSTALLING)return UI_DOWNLOADING;
    if(state==UPDATE_AVAILABLE||state==UPDATE_DECLINED)return UI_DOWNLOAD;
    return UI_WARN;
}
static void update_mark(float cx,float cy) {
    UpdateIconFrame frame;uint8_t pixels[UPDATE_ICON_SIZE][UPDATE_ICON_SIZE];
    update_icon_frame(&update_animation,NOW,&frame);update_icon_raster(&frame,pixels);
    u32 color=C2D_Color32((int)lerpf(255,79,frame.green),(int)lerpf(138,158,frame.green),(int)lerpf(31,134,frame.green),255);
    int left=(int)roundf(cx)-UPDATE_ICON_SIZE/2,top=(int)roundf(cy)-UPDATE_ICON_SIZE/2;
    // Whole-pixel scanline runs: no interpolated texture or anti-aliased geometry.
    for(int y=0;y<UPDATE_ICON_SIZE;y++)for(int x=0;x<UPDATE_ICON_SIZE;){
        if(!pixels[y][x]){x++;continue;}
        int start=x;while(x<UPDATE_ICON_SIZE&&pixels[y][x])x++;
        C2D_DrawRectSolid(left+start,top+y,.5f,x-start,1,color);
    }
}
static void update_screen(bool top) {
    scenes_draw_background(top);
    StoreUpdateState state=update_seen==(StoreUpdateState)-1?UPDATE_CHECKING:update_seen;
    if(!top){
        if(state==UPDATE_AVAILABLE||state==UPDATE_DECLINED)scenes_draw_update_confirmation(update_version(),update_focus,1);
        else if(state==UPDATE_DOWNLOADING||state==UPDATE_INSTALLING){
            text(T("detail.version"),160,98,11,rgb(TH.ink_soft,1),AL_CENTER);
            text_style(update_version(),160,120,18,700,0,rgb(TH.ink,1),AL_CENTER);
        }
        return;
    }
    const char *key;
    switch(state){
        case UPDATE_CURRENT:key="update.current";break;
        case UPDATE_OFFLINE:key="update.offline";break;
        case UPDATE_DOWNLOADING:key="update.downloading";break;
        case UPDATE_INSTALLING:key="update.installing";break;
        case UPDATE_DONE:key="update.done";break;
        case UPDATE_FAILED:key="update.failed";break;
        case UPDATE_AVAILABLE:case UPDATE_DECLINED:key="update.available";break;
        default:key="update.checking";break;
    }
    const char *label=T(key);
    float h=text_wrap_h_style(label,14,700,360);
    bool progress=state==UPDATE_DOWNLOADING||state==UPDATE_INSTALLING;
    // Centre the complete icon + label (+ progress) block around y=120.
    float height=48+16+h+update_progress_space(),y=(SCR_H-height)/2;
    update_mark(200,y+24);
    if(h<=line_h(14))text_style(label,200,y+64,14,700,0,rgb(TH.ink,1),AL_CENTER);
    else text_wrap_style(label,20,y+64,14,700,rgb(TH.ink,1),360);
    if(progress){
        float p=update_fixture>=0?.42f:clampf(store_update_progress(),0,1),by=y+64+h+12;
        rrect(60,by,280,5,2.5f,rgb(TH.surface3,1));rrect(60,by,fmaxf(1,280*p),5,2.5f,rgb(FORGE,1));
        char percent[20];snprintf(percent,sizeof percent,"%d %%",(int)(p*100));
        text(percent,200,by+9,11,rgb(TH.ink_soft,1),AL_CENTER);
    }
}
static void update_boot(void){
    store_update_poll();
    StoreUpdateState state=update_state();
    // A fast response still gives the rotating mark time to read as a check.
    if(NOW-update_gate_started<.9f)state=UPDATE_CHECKING;
    if(state!=update_seen){
        update_progress_from=update_progress_space();
        update_progress_to=state==UPDATE_DOWNLOADING||state==UPDATE_INSTALLING?34:0;
        update_icon_set(&update_animation,update_icon_kind(state),NOW);
        update_seen=state;update_changed=NOW;update_settled=-1;
        if(state==UPDATE_AVAILABLE)update_focus=1;
        sfx(state==UPDATE_CURRENT||state==UPDATE_DONE?"complete":state==UPDATE_DOWNLOADING?"download":"page");
    }
    if(state==UPDATE_AVAILABLE){
        if(NOW-update_changed>.2f){
            int choice=scenes_update_confirmation_input(&update_focus);
            if(choice>=0){
                bool decided=update_fixture>=0||store_update_decide(choice!=0);
                if(decided){
                    if(update_fixture>=0)update_fixture=choice?UPDATE_DOWNLOADING:UPDATE_DECLINED;
                    sfx(choice?"select":"close");
                }
            }
        }
        return;
    }
    if(state==UPDATE_DECLINED){if(NOW-update_changed>=.4f)quit_req=true;return;}
    bool terminal=state==UPDATE_CURRENT||state==UPDATE_OFFLINE||state==UPDATE_DONE||state==UPDATE_FAILED;
    if(!terminal)return;
    if(update_settled<0)update_settled=NOW;
    if(NOW<update_settled+1.5f)return;
    if(state==UPDATE_DONE){if(update_fixture<0)store_update_restart();quit_req=true;return;}
    update_gate=false;preload_gate=true;preload_started=osGetTime();
    catalog_request_banners();
}
static float update_cover(void){
    if(update_seen==UPDATE_DECLINED)return ease_inout(span(NOW,update_changed,.4f));
    if(update_settled>=0)return ease_inout(span(NOW,update_settled+1.1f,.4f));
    return 1-ease_inout(span(NOW,update_gate_started,.3f));
}

static void render(void) {
    float slider = osGet3DSliderState();
    text_frame();
    for (int eye = 0; eye < (slider > 0 ? 2 : 1); eye++) {
        C3D_RenderTarget *t = eye ? top_r : top_l;
        C2D_TargetClear(t, C2D_Color32(0, 0, 0, 255));
        C2D_SceneBegin(t);
        // Positive depth pops out of the screen: the left image moves right.
        EYE = slider > 0 ? (eye ? -slider : slider) : 0;
        if(update_gate)update_screen(true);
        else if(!preload_gate){scenes_draw_top();statusbar_draw();toasts_draw();}
        if (show_perf) perf_draw();
        if (cover > 0) rect(0, 0, TOP_W, SCR_H, C2D_Color32f(0, 0, 0, cover));
    }
    EYE = 0;
    C2D_TargetClear(bottom, C2D_Color32(0, 0, 0, 255));
    C2D_SceneBegin(bottom);
    if(update_gate)update_screen(false);else if(!preload_gate)scenes_draw_bottom();
    if (cover > 0) rect(0, 0, BOT_W, SCR_H, C2D_Color32f(0, 0, 0, cover));
    u8 *ft = NULL, *fb = NULL;
    if (shot_name[0]) {
        ft = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
        fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    }
    C3D_FrameEnd(0);
    if (shot_name[0]) {
        // Wait until the copy has reached the screens, then read it back.
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C3D_FrameEnd(0);
        gspWaitForVBlank();
        gspWaitForVBlank();
        save_shot(shot_name, ft, fb);
        if(test_log){fprintf(test_log,"shot %s ",shot_name);scenes_test_log(test_log);fprintf(test_log,"status battery=%d charging=%d\n",sys.pct,sys.charging);fflush(test_log);}
        shot_name[0] = 0;
    }
}

// HOME Menu and sleep: the workers stop touching the SD card and the network until the
// store is back, so the system gets them to itself and the HOME Menu opens at once.
static aptHookCookie apt_cookie;
static void apt_hook(APT_HookType type, void *unused) {
    switch (type) {
        case APTHOOK_ONSUSPEND: BG_HOLD = true; debug_note("suspend (HOME Menu)"); break;
        case APTHOOK_ONSLEEP: BG_HOLD = true; debug_note("sleep"); break;
        case APTHOOK_ONRESTORE: BG_HOLD = false; debug_note("restore"); break;
        case APTHOOK_ONWAKEUP: BG_HOLD = false; debug_note("wake up"); break;
        default: BG_HOLD = false; break;  // exit: the workers must be able to finish
    }
}

int main(int argc, char **argv) {
    (void)argc, (void)argv;
    u64 t_boot = svcGetSystemTick();
    gfxInitDefault();
    gfxSet3D(true);
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE * 2);
    C2D_Init(12288);
    C2D_Prepare();
    top_l = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    top_r = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
    bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    black_frame();  // the screens stay black while everything loads

    aptHook(&apt_cookie, apt_hook, NULL);
    romfsInit();
    cfguInit();
    ptmuInit();
    mcu_ok = R_SUCCEEDED(mcuHwcInit());
    APT_CheckNew3DS(&IS_NEW3DS);
    if (IS_NEW3DS) osSetSpeedupEnable(true);
    mkdir("sdmc:/3ds", 0777);
    mkdir(DATA_DIR, 0777);
    test_load();

    settings_load();
    strings_load();
    theme_apply();
    gfx_init();
    catalog_load();
    installed_load();
    if(!path_exists("sdmc:/3ds/dspfirm.cdc")){char err[128];dsp_dump(err,sizeof err);}
    audio_init();
    audio_load_rest();
    install_init();
    forge_mode_init();
    scenes_init(BOOT_FADE);
    store_update_start(argc>0?argv[0]:NULL);
    // Home art warms after the update gate, before the logo/intro.
    status_refresh();
    hidSetRepeatParameters(20, 5);
    boot_ms = (svcGetSystemTick() - t_boot) / (float)CPU_TICKS_PER_MSEC;
    if (test_log) fprintf(test_log, "boot %.0f ms, %s, %d apps, linear_free=%luKB\n", boot_ms, IS_NEW3DS ? "New 3DS" : "Old 3DS", NAPPS, linearSpaceFree() / 1024);

    NOW = 0;
    int frame = 0;
    u64 last = svcGetSystemTick();
#if FORGE_DEBUG
    debug_watch_start();
#endif
    for (;;) {
        debug_beat("apt");  // HOME Menu and sleep are handled (and waited for) in here
        if (!aptMainLoop()) break;
        debug_beat("frame");
        // Wait for the previous GPU frame before updating or freeing its textures.
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
#if FORGE_DEBUG
        // The frame on the LCDs now is the one SELECT was released on: no toast in it yet.
        if (dump_wanted) debug_dump_take();
        bool dump_ok;
        const char *dumped = debug_dump_poll(&dump_ok);
        if (dumped) toast(dumped, dump_ok ? MINT : DANGER);
#endif
        u64 now = svcGetSystemTick();
        float real_dt = (now - last) / (float)SYSCLOCK_ARM11;
        last = now;
        DT = clampf(real_dt, 0, 1 / 20.0f);  // long pauses (keyboard, HOME) don't skip animations
        NOW += DT;

        read_input(frame);
        if (quit_req || scenes_wants_exit() || (IN.held & KEY_SELECT && IN.down & KEY_START)) {
            C3D_FrameEnd(0);
            break;
        }
        if ((IN.down & KEY_SELECT) && (IN.held & KEY_L)) {show_perf = !show_perf; IN.down &= ~KEY_SELECT;}
#if FORGE_DEBUG
        debug_keys();
#endif
        debug_beat("update");

        remote_image_pump();
        sheets_pump();
        audio_update();
        video_pump();
        media_pump();
        media_set_paused(install_active()!=NULL||scenes_reader_active()||forge_mode_state()==FORGE_CHECKING);
        if(update_gate)update_boot();
        else if(preload_gate){
            forge_mode_pump();
            u64 elapsed=osGetTime()-preload_started;
            if((elapsed>=500&&catalog_banners_ready())||elapsed>=6000){
                preload_gate=false;intro_epoch=NOW;scenes_intro_restart(BOOT_FADE);
                if(test_log)fprintf(test_log,"preload %llu ms ready=%d linear_free=%luKB\n",(unsigned long long)elapsed,catalog_banners_ready(),linearSpaceFree()/1024);
            }
        }else scenes_update();
        if (IN.touching) IN.last_x = IN.tx, IN.last_y = IN.ty;
        toasts_update();
        status_refresh();
        float want = scenes_statusbar() ? 1 : 0;
        sb_a = clampf(sb_a + (want - sb_a) * (1 - powf(.0005f, DT)), 0, 1);
        cover = preload_gate ? 1 : update_gate ? update_cover() : fmaxf(1-ease_inout(span(NOW,intro_epoch,BOOT_FADE)),scenes_exit_fade());

        // FrameBegin now precedes updates, so citro3d includes both update and drawing.
        debug_beat("render");
        render();
        perf_frame(real_dt, C3D_GetProcessingTime());
        frame++;
    }

    if (test_log) {
        fprintf(test_log, "late frames total: %d\n", perf.late_total);
        fclose(test_log);
    }
    // No frame is drawn after the loop: when the HOME Menu closes the application nothing
    // may wait for the GPU or for a worker without a limit ("Closing software..." forever).
    BG_HOLD = false;
    debug_exit_trace(NULL);
    EXIT_DEADLINE_MS = osGetTime() + (install_active() || store_update_state()==UPDATE_INSTALLING ? 15000 : 3000);
    // First everyone is told to stop, so the workers wind down at the same time...
    github_prefetch_stop();
    store_update_stop();
    media_shutdown();
    universal_sync_stop();
    remote_image_stop();
    install_stop();
    scenes_stop();
    video_stop();
    debug_exit_trace("stop requested");
    // ...then each one is collected within the shared deadline.
    universal_sync_exit();
    video_exit();
    debug_exit_trace("video");
    remote_image_exit();
    media_exit();
    debug_exit_trace("media");
    scenes_exit();
    debug_exit_trace("scenes");
    github_prefetch_exit();
    store_update_exit();
    install_exit();
    forge_mode_exit();
    debug_exit_trace("install");
    saves_flush();
    audio_exit();
    debug_exit_trace("audio");
#if FORGE_DEBUG
    debug_dump_exit();
#endif
    gfx_exit();
    debug_exit_trace(EXIT_ABANDONED ? "workers abandoned" : "workers joined");
    C2D_Fini();
    C3D_Fini();
    if (mcu_ok) mcuHwcExit();
    ptmuExit();
    cfguExit();
    if (!EXIT_ABANDONED) romfsExit();
    gfxExit();
    debug_exit_trace("done");
    if (ncmds) {
        FILE *d = fopen(DATA_DIR "/done.txt", "w");
        if (d) fclose(d);
    }
    return 0;
}
