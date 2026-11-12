// Native counterparts of scenes/search, downloads, sdcard and github.js.
#include "gh_flow.h"
#include "gh_table_layout.h"
// Included after the shared drawing helpers and detail/settings scenes.
#include <3ds/archive.h>
static void go(int to) {
    next_scene = to;
    trans_phase = 1;
    trans_t = NOW;
}
static void panel_title(const char *s, float a) { text_ex(s, 12, 36, 20, 750, 1, -.5f, C(TH.ink, a), AL_LEFT); }
static void panel_bar(const char *s, float a) {
    glass(4, 4, 28, 28, 9, a);
    draw_icon("back", 18, 18, 12, C(TH.ink, a));
    scissor(320, 38, 4, 276, 28);
    text_style(s, 38, 10, 12, 700, 0, C(TH.ink, a), AL_LEFT);
    scissor_off();
}
static void scroll_list(float *scroll, int *focus, int n, int pitch, int height) {
    if (IN.repeat & (KEY_UP | KEY_DOWN)) {
        *focus = (int)clampf(*focus + (IN.repeat & KEY_DOWN ? 1 : -1), 0, fmaxf(0, n - 1));
        *scroll = clampf(*scroll, (*focus + 1) * pitch - height, *focus * pitch);
        sfx("move");
    }
    if (IN.touching && !IN.touch_down && IN.start_y >= 30)
        *scroll += IN.last_y - IN.ty;
    *scroll = clampf(*scroll, 0, fmaxf(0, n * pitch - height));
}
static void capacity(float a) {
    char b[96],legend[3][128],size[24];u64 used=sys.total_bytes>sys.free_bytes?sys.total_bytes-sys.free_bytes:0;
    u64 other=used>sys.store_bytes&&used-sys.store_bytes>sys.user_bytes?used-sys.store_bytes-sys.user_bytes:0;
    const u64 bytes[]={sys.store_bytes,sys.user_bytes,other};const u32 colors[]={FORGE,TH.mint,0x9aa9b0};const char *keys[]={"sd.store","sd.userFile","sd.other"};
    float widths[3],x=12;int rows=1;
    for(int i=0;i<3;i++){fmt_size(size,sizeof size,bytes[i]);snprintf(legend[i],sizeof legend[i],"%s %s%s",T(keys[i]),i<2&&!sys.space_complete?">":"",size);widths[i]=9+text_style_w(legend[i],9,800,0);if(x>12&&x+widths[i]>388){rows++;x=12;}x+=widths[i]+5;}
    float y=184-(rows-1)*12;
    tvars(b, sizeof b, T("downloads.free"), "free", sys.sd_free, "total", sys.sd_total);
    text_style(T("downloads.sd"),12,y,9,900,0,C(TH.ink,a),AL_LEFT);text_style(b,388,y,9,900,0,C(TH.ink_soft,a),AL_RIGHT);
    rrect(12,y+17,376,8,4,C(TH.surface3,a));x=12;
    for(int i=2;i>=0;i--){float w=bytes[i]&&sys.total_bytes?fmaxf(2.256f,376.f*bytes[i]/sys.total_bytes):0;w=fminf(w,388-x);if(w>0)rect(x,y+17,w,8,C(colors[i],a));x+=w;}
    x=12;y+=30;
    for(int i=0;i<3;i++){if(x>12&&x+widths[i]>388){x=12;y+=12;}rrect(x,y+3,6,6,2,C(colors[i],a));text_style(legend[i],x+9,y,9,800,0,C(TH.ink_soft,a),AL_LEFT);x+=widths[i]+5;}
}

// Search — real-time matches on top, 3DS-sized keyboard below.
static int key_row = 1, key_col, search_pick, search_n,search_total;
static float search_scroll;
static App *search_results[MAX_APPS];
static const char *keys[5][10] = {{"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
                                  {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"},
                                  {"a", "s", "d", "f", "g", "h", "j", "k", "l", "ñ"},
                                  {"z", "x", "c", "v", "b", "n", "m", "-", "⌫"},
                                  {" ", "OK"}};
static const int key_counts[] = {10, 10, 10, 9, 2};
static void search_refresh(void) {
    search_total=catalog_search(query,search_results,MAX_APPS);search_n=search_total<5?search_total:5;
    search_scroll = 0;
    search_pick = 0;
}
static void open_search(void) {
    query[0] = 0;
    key_row = 1;
    key_col = 0;
    search_refresh();
    sfx("open");
    go(SC_SEARCH);
}
static void search_open(void) {
    if (!search_n) {
        sfx("error");
        return;
    }
    // open_detail uses the current list; retain the home selection on return.
    App *saved = list[sel];
    int count = nlist;
    list[sel] = search_results[search_pick];
    nlist = 1;
    open_detail();
    list[sel] = saved;
    nlist = count;
    detail_parent = SC_SEARCH;
}
static void search_press(const char *k) {
    size_t n = strlen(query);
    if (!strcmp(k, "OK")) {
        search_open();
        return;
    }
    if (!strcmp(k, "⌫")) {
        if (n) {
            do {
                --n;
            } while (n && ((query[n] & 0xc0) == 0x80));
            query[n] = 0;
        }
    } else {
        unsigned characters=0;for(size_t i=0;i<n;i++)if(((unsigned char)query[i]&0xc0)!=0x80)characters++;
        if(characters>=24||n+strlen(k)>=sizeof query){sfx("error");return;}
        strcat(query,k);
    }
    search_refresh();
    sfx("type");
}
static void key_box(int r, int c, float *x, float *w) {
    *x = 8 + c * 30.7f;
    *w = 27.7f;
    if (r == 3 && c == 8)
        *w = 58.4f;
    if (r == 4) {
        *x = c ? 192.2f : 8;
        *w = c ? 119.8f : 181.2f;
    }
}
static void search_update(void) {
    if (IN.down & KEY_B) {
        if (query[0])
            search_press("⌫");
        else
            go(SC_HOME), sfx("back");
        return;
    }
    if (IN.down & KEY_START) {
        search_open();
        return;
    }
    if (IN.down & (KEY_L | KEY_R)) {
        search_pick = (int)clampf(search_pick + (IN.down & KEY_R ? 1 : -1), 0, fmaxf(0, search_n - 1));
        sfx("move");
    }
    if (IN.repeat & (KEY_LEFT | KEY_RIGHT))
        key_col = (int)clampf(key_col + (IN.repeat & KEY_RIGHT ? 1 : -1), 0, key_counts[key_row] - 1), sfx("move");
    if (IN.repeat & (KEY_UP | KEY_DOWN)) {
        float x, w;
        key_box(key_row, key_col, &x, &w);
        float cx = x + w / 2;
        key_row = (int)clampf(key_row + (IN.repeat & KEY_DOWN ? 1 : -1), 0, 4);
        float best = 1000;
        for (int c = 0; c < key_counts[key_row]; c++) {
            key_box(key_row, c, &x, &w);
            float d = fabsf(x + w / 2 - cx);
            if (d < best)
                best = d, key_col = c;
        }
        sfx("move");
    }
    if (IN.down & KEY_A)
        search_press(keys[key_row][key_col]);
    if (hit(289, 6, 23, 26)) {
        query[0] = 0;
        search_refresh();
    }
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < key_counts[r]; c++) {
            float x, w;
            key_box(r, c, &x, &w);
            if (hit(x, 38 + r * 30, w, 27)) {
                key_row = r;
                key_col = c;
                search_press(keys[r][c]);
            }
        }
    if (hit(8, 205, 28, 28))
        go(SC_HOME), sfx("back");
    if (hit(204, 205, 28, 28) || hit(284, 205, 28, 28)) {
        search_pick = (int)clampf(search_pick + (IN.tx > 260 ? 1 : -1), 0, fmaxf(0, search_n - 1));
        sfx("move");
    }
    search_scroll = clampf(search_scroll, (search_pick + 1) * 34 - 133, search_pick * 34);
    search_scroll = clampf(search_scroll, 0, fmaxf(0, search_n * 34 - 133));
}
static void search_top(float a) {
    draw_bg(true, a);
    panel_title(T("search.title"), a);
    char display[160];snprintf(display,sizeof display,"«%s»",query);
    text_fit_style(query[0]?display:T("search.suggestions"),12,63,12,400,376,C(query[0]?TH.ink:TH.ink_faint,a));
    char count[96], n[16];
    snprintf(n,sizeof n,"%d",search_total);
    tvars(count, sizeof count, T(search_total==1?"search.result":search_total?"search.results":"search.none"),search_total?"n":"q",search_total?n:query, NULL, NULL);
    text_style(query[0]?count:"",12,82,10,800,0,C(TH.ink_faint,a),AL_LEFT);
    scissor(400, 12, 99, 376, 133);
    for (int i = 0; i < search_n; i++) {
        App *ap = search_results[i];
        float y = 99 + i * 34 - search_scroll;
        bool on = i == search_pick;
        if (y + 32 < 99 || y >= 232) continue;
        glass(12, y, 376, 32, 8, a);
        if (on) rrect_outline(12, y, 376, 32, 8, 2, C(FORGE, a));
        draw_app_icon(ap, 29, y + 16, 26, a);
        scissor(400, 50, fmaxf(99, y), 330, fminf(232, y + 32) - fmaxf(99, y));
        text_style(ap->title, 50, y + 3, 11, 600, 0, C(TH.ink, a), AL_LEFT);
        const char *cat = ap->category;
        for (int c = 0; c < NCATS; c++) if (!strcmp(CATS[c].id, ap->category)) { cat = LJ(CATS[c].label); break; }
        char sub[180];
        snprintf(sub, sizeof sub, "%s · %s%s%s", ap->author, ap->platform ? ap->platform : "", ap->platform ? " · " : "", cat);
        text(sub, 50, y + 17, 9, C(TH.ink_soft, a), AL_LEFT);
        scissor(400, 12, 99, 376, 133);
    }
    scissor_off();
}
static void search_bottom(float a) {
    draw_bg(false, 1);
    glass(8, 6, 304, 26, 13, a);
    draw_icon("search", 23, 19, 12, C(TH.ink_faint, a));
    scissor(320, 35, 8, 250, 23);
    text(query[0] ? query : T("search.placeholder"), 35, 12, 11, C(query[0] ? TH.ink : TH.ink_faint, a), AL_LEFT);
    scissor_off();
    draw_icon("close", 301, 19, 10, C(TH.ink_soft, a));
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < key_counts[r]; c++) {
            float x, w;
            key_box(r, c, &x, &w);
            float y = 38 + r * 30;
            bool on = r == key_row && c == key_col, ok = r == 4 && c == 1;
            rrect(x, y + 1.5f, w, 27, 7, C(TH.line, .18f * a));
            rrect(x, y, w, 27, 7, C(ok ? FORGE : TH.surface, a));
            rrect_outline(x, y, w, 27, 7, on ? 1.5f : 1, C(on ? FORGE : TH.line, (on ? 1 : TH.line_a) * a));
            const char *label = r == 4 ? T(c ? "search.open" : "search.space") : keys[r][c];
            if (!strcmp(keys[r][c], "⌫")) draw_icon("back", x + w / 2, y + 13.5f, 12, C(TH.ink, a));
            else text_style(label, roundf(x + w / 2), y + 6, 12, 600, 0, C(ok ? 0xffffff : TH.ink, a), AL_CENTER);
        }
    button(8, 205, 28, 28, "", "back", NULL, BTN_SURFACE, a);
    button(204, 205, 28, 28, "", "back", NULL, BTN_SURFACE, a);
    button(284, 205, 28, 28, "", "next", NULL, BTN_SURFACE, a);
    char position[24];
    snprintf(position, sizeof position, "%d / %d", search_n ? search_pick + 1 : 0, search_n);
    text(position, 258, 212, 10, C(TH.ink_soft, a), AL_CENTER);
}

// Install jobs and the store's resource package share one navigable download view.
// media: -1 for an install job, 0 for the single "Store resources" row.
typedef struct { Job *job; int media; float y; } DownloadRow;
static DownloadRow dl_rows[160];
static int dl_focus, dl_n;
static float dl_scroll, dl_height;
static float dl_sections[4];
// Allocated only by explicit visual test commands, never enqueued or persisted.
static Job *dl_test_jobs;
static int dl_test_count;
static bool dl_test_override;
static bool dl_package(void){return !dl_test_override&&PKG.items;}
static int dl_job_count(void){return dl_test_override?dl_test_count:install_count();}
static Job *dl_job_at(int i){return dl_test_override?&dl_test_jobs[i]:install_at(i);}
static Job *dl_active(void){if(dl_test_override){for(int i=0;i<dl_test_count;i++)if(dl_test_jobs[i].state==JOB_RUNNING)return &dl_test_jobs[i];return NULL;}return install_active();}
static const char *dl_section_keys[] = {"downloads.active", "downloads.queue", "downloads.history"};
static void dl_build(void) {
    Job *previous=dl_focus<dl_n?dl_rows[dl_focus].job:NULL;
    u64 serial=previous?previous->serial:0;
    int previous_media=dl_focus<dl_n?dl_rows[dl_focus].media:-1;
    int count = dl_job_count(); Job *ordered[32];if(count>32)count=32;
    for(int i=0;i<count;i++)ordered[i]=dl_job_at(i);
    for(int i=1;i<count;i++){Job *j=ordered[i];int k=i;while(k>0&&ordered[k-1]->serial>j->serial){ordered[k]=ordered[k-1];k--;}ordered[k]=j;}
    dl_n = 0; float y = 0;
    for (int group = 0; group < 3; group++) {
        dl_sections[group] = -1;
        for (int k = 0; k < count && dl_n < 159; k++) {
            int i = group == 2 ? count - 1 - k : k;
            Job *j = ordered[i];
            int g = j->state == JOB_RUNNING ? 0 : j->state == JOB_QUEUED ? 1 : 2;
            if (g != group) continue;
            if (dl_sections[group] < 0) { dl_sections[group]=y;y+=24; }
            dl_rows[dl_n++] = (DownloadRow){j,-1,y};y+=42;
        }
    }
    dl_sections[3] = -1;
    if (dl_package() && dl_n < 160) { dl_sections[3]=y;y+=24;dl_rows[dl_n++] = (DownloadRow){NULL, 0, y}; y += 42; }
    dl_height=y?y+3:0;
    for(int i=0;i<dl_n;i++)if((previous&&dl_rows[i].job==previous&&dl_rows[i].job->serial==serial)||(!previous&&previous_media>=0&&dl_rows[i].media==previous_media)){dl_focus=i;break;}
    dl_focus = (int)clampf(dl_focus, 0, fmaxf(0, dl_n - 1));
    dl_scroll = clampf(dl_scroll, 0, fmaxf(0, dl_height - 198));
}
static const char *package_label(void) {
    switch (PKG.state) {
    case MEDIA_QUEUED: return T("status.queued");
    case MEDIA_DOWNLOADING: return T("status.downloading");
    case MEDIA_VERIFYING: return T("media.packageVerifying");
    case MEDIA_ERROR: return T("media.retry");
    case MEDIA_CLEARED: return T("media.cleared");
    case MEDIA_COMPLETED: return T("media.packageReady");
    }
    return "";
}
static bool package_busy(void) { return PKG.state == MEDIA_DOWNLOADING || PKG.state == MEDIA_VERIFYING; }
/** "34 % · 4.1 MB / 12.0 MB" while it moves, otherwise its state and size. */
static void package_detail(char *out, size_t n) {
    char got[24], total[24];
    fmt_size(got, sizeof got, (double)PKG.received);
    fmt_size(total, sizeof total, (double)PKG.bytes);
    if (package_busy() || PKG.state == MEDIA_QUEUED) snprintf(out, n, "%d %% · %s / %s%s", (int)(PKG.progress * 100), got, total, PKG.state == MEDIA_QUEUED && media_paused() ? " · …" : "");
    else snprintf(out, n, "%s · %s", package_label(), total);
}
static const char *job_label(Job *j) {
    if(j->uninstall&&j->state==JOB_RUNNING)return T("native.uninstalling");
    if(j->uninstall&&j->state==JOB_DONE)return T("native.uninstalled");
    return T(j->state == JOB_RUNNING ? "status.downloading" : j->state == JOB_QUEUED ? "status.queued" :
             j->state == JOB_DONE ? (j->warnings ? "status.needsFile" : "status.installed") :
             j->state == JOB_FAILED ? "install.failed" : "install.cancelled");
}
static void open_downloads(void) { dl_n=0; dl_focus = 0; dl_scroll = 0; sys_space_due = NOW - sys.space_t >= 30; dl_build(); sfx("open"); go(SC_DOWNLOADS); }
static void downloads_update(void) {
    if (IN.down & KEY_B || hit(4, 4, 28, 28)) { go(SC_HOME); sfx("back"); return; }
    dl_build();
    if (IN.repeat & (KEY_UP | KEY_DOWN)) {
        dl_focus = (int)clampf(dl_focus + (IN.repeat & KEY_DOWN ? 1 : -1), 0, fmaxf(0, dl_n - 1));
        if (dl_n) dl_scroll = clampf(dl_scroll, dl_rows[dl_focus].y + 39 - 198, dl_rows[dl_focus].y);
        sfx("move");
    }
    if (IN.touching && !IN.touch_down && IN.start_y >= 36) dl_scroll += IN.last_y - IN.ty;
    dl_scroll = clampf(dl_scroll, 0, fmaxf(0, dl_height - 198));
    bool open = IN.down & KEY_A;
    for (int i = 0; i < dl_n; i++) {
        float y = 36 + dl_rows[i].y - dl_scroll;
        if (y >= 36 && y < 234 && hit(6, y, 308, 39)) {
            if (i == dl_focus) open = true; else dl_focus = i, sfx("move");
        }
    }
    if (!dl_n) return;
    DownloadRow *row = &dl_rows[dl_focus];
    if (open && row->job) {
        if(row->job->state==JOB_FAILED||row->job->state==JOB_CANCELLED){
            App *saved=list[0];int count=nlist,selection=sel;list[0]=row->job->app;nlist=1;sel=0;
            open_detail();list[0]=saved;nlist=count;sel=selection;detail_parent=SC_DOWNLOADS;
        }else {open_install(row->job);install_parent=SC_DOWNLOADS;}
    }
    if (IN.down & KEY_X && row->job) install_cancel(row->job);
    if (open && row->media >= 0) {
        if (PKG.state == MEDIA_ERROR || PKG.state == MEDIA_CLEARED) { media_retry_all(); sfx("select"); }
        else toast(package_label(), MINT);
    }
}
static void downloads_top(float a) {
    draw_bg(true, a); panel_title(T("downloads.title"), a);
    Job *j = dl_active();
    int active=0;for(int i=0;i<dl_job_count();i++)if(dl_job_at(i)->state<=JOB_RUNNING)active++;
    char subtitle[128],active_count[20];snprintf(active_count,sizeof active_count,"%d",active);
    tvars(subtitle,sizeof subtitle,T(active?"downloads.ofQueue":"downloads.emptyBody"),"n",active_count,NULL,NULL);
    text_style(subtitle,12,60,10,800,0,C(TH.ink_faint,a),AL_LEFT);
    // Without a running installation the card shows the package while it moves, or when
    // its row is the one selected.
    bool pkg = !j && dl_package() && (package_busy() || (dl_focus < dl_n && dl_rows[dl_focus].media >= 0));
    if (j || pkg) {
        App *ap = j ? j->app : NULL;
        glass(12,86,376,74,12,a);
        if (ap) draw_app_icon(ap,43,123,44,a);
        else { rrect_hgrad(21,101,44,44,12,C(FORGE,a),C(FORGE2,a)); draw_icon("layers",43,123,26,C(0xffffff,a)); }
        scissor(400,75,94,304,58);
        text_style(ap?ap->title:T("media.package"),75,94,12,900,0,C(TH.ink,a),AL_LEFT);
        text(j ? (j->cur >= 0 && j->cur < j->nsteps ? j->steps[j->cur].label : job_label(j)) : package_label(),75,111,10, C(TH.ink_soft, a), AL_LEFT);
        float p = j ? job_progress(j) : PKG.progress;
        rrect(75,130,304,5,2.5f, C(TH.surface3, a));
        if (p > 0) rrect(75,130,fmaxf(5,304*p),5,2.5f, C(FORGE, a));
        char b[96], bytes[24], total[24];
        snprintf(b, sizeof b, "%d %%", (int)(p * 100)); text_style(b,75,138,10,800,0,C(TH.ink_soft,a),AL_LEFT);
        if (j && j->speed > 0) {
            fmt_size(bytes, sizeof bytes, j->speed);
            unsigned seconds = j->total_bytes > j->done_bytes ? (j->total_bytes - j->done_bytes) / j->speed : 0;
            snprintf(b, sizeof b, "%s/s · %u:%02u", bytes, seconds / 60, seconds % 60);
        } else {
            fmt_size(bytes, sizeof bytes, j ? (double)j->done_bytes : (double)PKG.received);
            fmt_size(total, sizeof total, j ? (double)j->total_bytes : (double)PKG.bytes);
            snprintf(b, sizeof b, "%s / %s", bytes, total);
        }
        text_style(b,379,138,10,800,0,C(TH.ink_soft,a),AL_RIGHT);
        scissor_off();
        const char *error=j?j->error:PKG.state==MEDIA_ERROR?PKG.error:"";
        if(*error)text_wrap(error,12,165,10,C(DANGER,a),376);
        else if(pkg&&PKG.state==MEDIA_CLEARED)text_wrap(T("media.packageRedownload"),12,165,10,C(TH.ink_soft,a),376);
    } else {
        coin_draw(&COIN_ABOUT,35,111, wobble_pose(NOW, 5.2f), 1, a);
        text_style(T("downloads.empty"),70,95,12,900,0,C(TH.ink,a),AL_LEFT);
        text_wrap(media_loading() ? T("intro.reading") : media_queue_error()[0] ? media_queue_error() : T("downloads.emptyBody"),70,113,10, C(TH.ink_soft, a), 310);
    }
    capacity(a);
}
static void downloads_bottom(float a) {
    draw_bg(false,1);panel_bar(T("downloads.title"),a);dl_build();
    if(dl_n){char count[20];snprintf(count,sizeof count,"%d",dl_n);text_style(count,316,11,10,800,0,C(TH.ink_faint,a),AL_RIGHT);}
    scissor(320, 0, 36, 320, 198);
    if(!dl_n){draw_icon("download",160,105,24,C(TH.ink_faint,a));text_style(media_loading()?T("intro.reading"):T("downloads.empty"),160,123,10,800,0,C(TH.ink_faint,a),AL_CENTER);text_style(T("downloads.emptyBody"),160,143,10,800,0,C(TH.ink_faint,a),AL_CENTER);}
    for (int g = 0; g < 4; g++) if (dl_sections[g] >= 0) {
        float y = 36 + dl_sections[g] - dl_scroll;
        if(y>=20&&y<234)text_caps(T(g==3?"section.home":dl_section_keys[g]),12,y+6,9,1.4f,C((S.dark?0x8fdcc0:0x2f7a63),a),AL_LEFT);
    }
    for (int i = 0; i < dl_n; i++) {
        float y = 36 + dl_rows[i].y - dl_scroll;
        if (y + 39 < 36 || y >= 234) continue;
        Job *j = dl_rows[i].job;
        App *ap = j ? j->app : NULL;
        glass(6, y, 308, 39, 8, a);
        if (i == dl_focus) rrect_outline(6, y, 308, 39, 8, 2, C(FORGE, a));
        if(ap)draw_app_icon(ap,24,y+19.5f,22,a);
        else { rrect_hgrad(13,y+8.5f,22,22,6,C(FORGE,a),C(FORGE2,a)); draw_icon("layers",24,y+19.5f,13,C(0xffffff,a)); }
        const char *label=j?job_label(j):PKG.state==MEDIA_COMPLETED?T("media.ok"):package_label();
        bool running=j?j->state==JOB_RUNNING:package_busy();
        float right_width=running?22:chip_width(label,"check");
        float right_x=307-right_width;
        scissor(320,42,fmaxf(36,y),fmaxf(0,right_x-49),fminf(234,y+39)-fmaxf(36,y));
        text_fit_style(ap?ap->title:T("media.package"),42,y+5,11,600,fmaxf(0,right_x-49),C(TH.ink,a));
        char sub[256];
        if (j) {
            if (j->state == JOB_RUNNING) snprintf(sub, sizeof sub, "%d %% · %s", (int)(job_progress(j) * 100), j->cur >= 0 && j->cur < j->nsteps ? j->steps[j->cur].label : job_label(j));
            else if(j->state==JOB_QUEUED){char total[24];fmt_size(total,sizeof total,j->total_bytes);snprintf(sub,sizeof sub,"%s · %s · %s",j->tag,j->cia?"CIA":"3DSX",total);}
            else snprintf(sub,sizeof sub,"%s · %s · %s",j->tag,j->cia?"CIA":"3DSX",T(j->experimental?"channel.experimental":"channel.stable"));
        }else package_detail(sub,sizeof sub);
        text_fit_style(sub,42,y+20,10,500,fmaxf(0,right_x-49),C(TH.ink_soft,a));
        scissor(320, 0, 36, 320, 198);
        bool queued=j?j->state==JOB_QUEUED:PKG.state==MEDIA_QUEUED;
        bool good = j ? j->state == JOB_DONE && !j->warnings : PKG.state == MEDIA_COMPLETED;
        bool warn = j ? j->state == JOB_DONE && j->warnings : PKG.state == MEDIA_CLEARED;
        if(running){ring(296,y+19.5f,9,3,0,6.283f,C(TH.surface3,a));ring(296,y+19.5f,9,3,-M_PI/2,-M_PI/2+6.283f*fmaxf(.03f,j?job_progress(j):PKG.progress),C(FORGE,a));}
        else chip(right_x,y+11.5f,label,queued?"clock":good?"check":warn?"warn":"close",C(good?(S.dark?0x8fdcc0:0x2f7a63):warn?(S.dark?0xffcc5c:0xa86f00):queued?TH.ink_soft:DANGER,1),good?C(S.dark?TH.mint:0xdcefe7,S.dark?.16f:1):warn?C(S.dark?AMBER:0xfff4d6,S.dark?.18f:1):queued?C(TH.surface2,1):C(S.dark?DANGER:0xfde7e7,S.dark?.18f:1),a);
    }
    scissor_off();
}

// SD directory I/O stays off the render thread, including counting/deletion. The list is
// shown as soon as the names are known; folder sizes and dates are filled in afterwards.
// Every step can be cancelled, so going back or leaving never waits for the card.
typedef struct {
    char name[256];
    bool dir, protected, size_known, counted;
    u64 size;
    time_t modified;
    int children;
    App *owner;
} SdEntry;
static SdEntry *sd_entries;  // the worker's until sd_listed; afterwards it only fills sizes
static int sd_n, sd_focus, sd_alloc;
static float sd_scroll;
static char sd_dir[768] = "sdmc:/", sd_error[128], sd_restore[256], sd_remove_path[1100];
static Thread sd_thread;
static bool sd_listed, sd_finished, sd_cancel;  // shared with the worker: atomic access
static bool sd_confirm, sd_delete_job, sd_rescan, sd_shown;
static bool sd_stop(void) { return __atomic_load_n(&sd_cancel, __ATOMIC_ACQUIRE); }
static int sd_cmp(const void *x, const void *y) {
    const SdEntry *a = x, *b = y;
    if (a->dir != b->dir) return a->dir ? -1 : 1;
    return strcasecmp(a->name, b->name);
}
static bool sd_protected(const char *path) {
    // System data and the running store are never removable through this browser.
    static const char *roots[] = {"sdmc:/Nintendo 3DS", "sdmc:/private", "sdmc:/boot.firm", "sdmc:/boot.3dsx", "sdmc:/luma", DATA_DIR};
    for (unsigned i = 0; i < sizeof roots / sizeof *roots; i++) {
        size_t n = strlen(roots[i]);
        if (!strncasecmp(path, roots[i], n) && (!path[n] || path[n] == '/')) return true;
        size_t p = strlen(path);
        if (!strncasecmp(roots[i], path, p) && roots[i][p] == '/') return true;
    }
    return !strcmp(path, "sdmc:/");
}
static bool sd_unlink_tree(const char *path, int depth) {
    if (depth > 32) { errno = ELOOP; return false; }
    if (sd_protected(path)) { errno = EPERM; return false; }
    struct stat st;
    if (lstat(path, &st)) return errno == ENOENT;
    if (!S_ISDIR(st.st_mode)) return unlink(path) == 0;
    DIR *d = opendir(path);
    if (!d) return false;
    bool ok = true; struct dirent *e;
    while (ok && (e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char child[1100];
        int n = snprintf(child, sizeof child, "%s/%s", path, e->d_name);
        ok = n > 0 && (size_t)n < sizeof child && sd_unlink_tree(child, depth + 1);
    }
    closedir(d);
    return ok && rmdir(path) == 0;
}

// Directory reads go straight to FS: one request returns a batch of entries with their
// sizes, where readdir + lstat costs several requests per file on a real card.
#define SD_BATCH 32
typedef struct { Handle dir; FS_DirectoryEntry *batch; u32 count, at; } SdDir;
static bool sd_dir_open(SdDir *d, FS_Archive archive, const char *path) {  // path: "sdmc:/a/b" or "sdmc:/a/b/"
    static const u16 root[] = {'/', 0};
    u16 wide[1100];
    const char *rel = path + 5;
    ssize_t n = utf8_to_utf16(wide, (const u8 *)rel, sizeof wide / 2 - 1);
    if (n < 0 || (size_t)n >= sizeof wide / 2) return false;
    while (n > 1 && wide[n - 1] == '/') n--;
    wide[n] = 0;
    memset(d, 0, sizeof *d);
    d->batch = malloc(SD_BATCH * sizeof *d->batch);
    if (!d->batch) return false;
    if (R_FAILED(FSUSER_OpenDirectory(&d->dir, archive, fsMakePath(PATH_UTF16, n ? wide : root)))) { free(d->batch); d->batch = NULL; return false; }
    return true;
}
static FS_DirectoryEntry *sd_dir_next(SdDir *d) {
    bg_wait();
    if (d->at == d->count) {
        d->at = d->count = 0;
        if (R_FAILED(FSDIR_Read(d->dir, &d->count, SD_BATCH, d->batch)) || !d->count) return NULL;
    }
    return &d->batch[d->at++];
}
static void sd_dir_close(SdDir *d) { if (d->batch) { FSDIR_Close(d->dir); free(d->batch); d->batch = NULL; } }
static void sd_entry_name(const FS_DirectoryEntry *e, char *out, size_t cap) {
    ssize_t n = utf16_to_utf8((u8 *)out, e->name, cap - 1);
    out[n < 0 ? 0 : (size_t)n < cap ? (size_t)n : cap - 1] = 0;
}
/** Bytes below `path`, visiting at most *left entries; false when it stopped early. */
static bool sd_tree_size(FS_Archive archive, const char *path, int depth, unsigned *left, u64 *bytes, int *children) {
    SdDir d;
    if (depth > 8 || !sd_dir_open(&d, archive, path)) return false;
    bool complete = true;
    FS_DirectoryEntry *e;
    while ((e = sd_dir_next(&d))) {
        if (sd_stop() || !*left) { complete = false; break; }
        --*left;
        if (children) ++*children;
        if (!(e->attributes & FS_ATTRIBUTE_DIRECTORY)) { *bytes += e->fileSize; continue; }
        char child[1100], name[300];
        sd_entry_name(e, name, sizeof name);
        int n = snprintf(child, sizeof child, "%s/%s", path, name);
        if (n < 0 || (size_t)n >= sizeof child || !sd_tree_size(archive, child, depth + 1, left, bytes, NULL)) complete = false;
    }
    sd_dir_close(&d);
    return complete;
}
static void sd_read(void *unused) {
    sd_n = 0;
    sd_error[0] = 0;
    if (sd_delete_job && !sd_unlink_tree(sd_remove_path, 0))
        snprintf(sd_error, sizeof sd_error, "%s", strerror(errno));
    FS_Archive archive = 0;
    SdDir d;
    bool opened = R_SUCCEEDED(FSUSER_OpenArchive(&archive, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, "")));
    json_t *installed = json_load_file(DATA_DIR "/installed.json", 0, NULL);
    // 1. Names, kinds and file sizes: a handful of requests even for a large folder.
    if (opened && sd_dir_open(&d, archive, sd_dir)) {
        FS_DirectoryEntry *e;
        while (!sd_stop() && (e = sd_dir_next(&d))) {
            if (sd_n == sd_alloc) {
                int cap = sd_alloc ? sd_alloc * 2 : 64;
                SdEntry *items = cap <= 8192 ? realloc(sd_entries, cap * sizeof *items) : NULL;
                if(!items){snprintf(sd_error,sizeof sd_error,"%s",T("native.listLimit"));break;}
                sd_entries = items; sd_alloc = cap;
            }
            SdEntry *p = &sd_entries[sd_n];
            memset(p, 0, sizeof *p);
            sd_entry_name(e, p->name, sizeof p->name);
            char path[1100];
            int len = snprintf(path, sizeof path, "%s%s", sd_dir, p->name);
            if (!p->name[0] || len < 0 || (size_t)len >= sizeof path) continue;
            sd_n++;
            p->dir = (e->attributes & FS_ATTRIBUTE_DIRECTORY) != 0;
            p->size = p->dir ? 0 : e->fileSize;
            p->size_known = p->counted = !p->dir;
            p->protected = sd_protected(path);
            for (int ai = 0; ai < NAPPS && !p->owner; ai++) {
                json_t *files = json_object_get(json_object_get(installed, APPS[ai].id), "files"), *f; size_t fi;
                json_array_foreach(files, fi, f) {
                    const char *fp = json_string_value(f);
                    if (fp && !strncmp(fp, "sdmc:", 5)) fp += 5;
                    if (fp && !strncmp(fp, path + 5, strlen(path + 5)) && (!fp[strlen(path + 5)] || fp[strlen(path + 5)] == '/')) { p->owner = &APPS[ai]; break; }
                }
            }
        }
        sd_dir_close(&d);
        qsort(sd_entries, sd_n, sizeof *sd_entries, sd_cmp);
    } else snprintf(sd_error, sizeof sd_error, "%s", strerror(opened ? ENOENT : EIO));
    json_decref(installed);
    if (!sd_stop()) __atomic_store_n(&sd_listed, true, __ATOMIC_RELEASE);
    // 2. Dates, then what each folder holds (bounded, like before). The list is already
    // on screen; each entry's flag is set after its values.
    for (int i = 0; opened && i < sd_n && !sd_stop(); i++) {
        SdEntry *p = &sd_entries[i];
        char path[1100];
        snprintf(path, sizeof path, "%s%s", sd_dir, p->name);
        u64 stamp = 0;
        if (R_SUCCEEDED(archive_getmtime(path, &stamp)) && stamp && stamp <= INT64_MAX) p->modified = (time_t)stamp;
    }
    for (int i = 0; opened && i < sd_n && !sd_stop(); i++) {
        SdEntry *p = &sd_entries[i];
        if (!p->dir || !strcasecmp(p->name, "Nintendo 3DS")) continue;
        char path[1100];
        snprintf(path, sizeof path, "%s%s", sd_dir, p->name);
        unsigned left = 512;
        u64 bytes = 0;
        int children = 0;
        bool complete = sd_tree_size(archive, path, 0, &left, &bytes, &children);
        if (sd_stop()) break;
        p->size = bytes, p->children = children, p->size_known = complete;
        __atomic_store_n(&p->counted, true, __ATOMIC_RELEASE);
    }
    if (opened) FSUSER_CloseArchive(archive);
    __atomic_store_n(&sd_finished, true, __ATOMIC_RELEASE);
}
static void sd_start(void) {
    sd_rescan = false; sd_shown = false; sd_scroll = 0;
    __atomic_store_n(&sd_listed, false, __ATOMIC_RELEASE);
    __atomic_store_n(&sd_finished, false, __ATOMIC_RELEASE);
    __atomic_store_n(&sd_cancel, false, __ATOMIC_RELEASE);
    s32 p = 0x30; svcGetThreadPriority(&p, CUR_THREAD_HANDLE);
    sd_thread = threadCreate(sd_read, NULL, 48 * 1024, p + 2, -2, false);
    if (!sd_thread) { sd_n = 0; snprintf(sd_error, sizeof sd_error, "Out of memory"); sd_listed = sd_finished = true; }
}
/** Lists sd_dir. A worker still busy with the previous folder is cancelled first. */
static void sd_scan(void) {
    __atomic_store_n(&sd_listed, false, __ATOMIC_RELEASE);  // the old list is no longer read
    sd_shown = false;
    if (sd_thread) { __atomic_store_n(&sd_cancel, true, __ATOMIC_RELEASE); sd_rescan = true; }
    else sd_start();
}
/** Every frame, in any scene: show a finished listing, collect the worker, start a pending one. */
static void sd_pump(void) {
    // sd_shown is the main thread's view of the list: the entries are only read once the
    // focus has been fitted to them, whichever frame the worker published in.
    if (!sd_shown && __atomic_load_n(&sd_listed, __ATOMIC_ACQUIRE)) {
        sd_shown = true;
        if (sd_restore[0]) for (int i = 0; i < sd_n; i++) if (!strcmp(sd_entries[i].name, sd_restore)) { sd_focus = i; break; }
        sd_restore[0] = 0;
        sd_focus = (int)clampf(sd_focus, 0, fmaxf(0, sd_n - 1));
        sd_scroll = clampf((sd_focus + 1) * 42 - 198, 0, fmaxf(0, sd_n * 42 - 198));
        if (sd_delete_job) { path_cache_clear(); sd_delete_job = false; sys_space_due = true; }
    }
    if (sd_thread) {
        if (scene != SC_SD && next_scene != SC_SD) __atomic_store_n(&sd_cancel, true, __ATOMIC_RELEASE), sd_rescan = false;
        if (!__atomic_load_n(&sd_finished, __ATOMIC_ACQUIRE) || !thread_done(&sd_thread)) return;
    }
    if (sd_rescan) sd_start();
}
static void sd_leave(void) { go(SC_HOME); sfx("back"); }
static void open_sd(void) {
    snprintf(sd_dir, sizeof sd_dir, "sdmc:/");
    sd_focus = 0; sd_restore[0] = 0; sd_delete_job = sd_confirm = false;
    sys_space_due = NOW - sys.space_t >= 30;
    sd_scan(); sfx("open"); go(SC_SD);
}
static void sd_up(void) {
    if (!strcmp(sd_dir, "sdmc:/")) { sd_leave(); return; }
    sd_dir[strlen(sd_dir) - 1] = 0;
    char *p = strrchr(sd_dir, '/');
    if (p) { snprintf(sd_restore, sizeof sd_restore, "%s", p + 1); p[1] = 0; }
    sd_scan(); sfx("back");
}
static void sd_update(void) {
    bool listed = sd_shown;
    if (listed && sd_confirm) {
        if (IN.down & KEY_B || hit(20, 172, 132, 28)) { sd_confirm = false; sfx("back"); }
        else if (IN.down & KEY_A || hit(168, 172, 132, 28)) {
            sd_confirm = false; sd_delete_job = true;
            snprintf(sd_remove_path, sizeof sd_remove_path, "%s%s", sd_dir, sd_entries[sd_focus].name);
            sd_scan();
        }
        return;
    }
    // Leaving always works, also while the card is still being read.
    if (IN.down & KEY_SELECT) { sd_leave(); return; }
    if (IN.down & (KEY_B | KEY_LEFT) || hit(4, 4, 28, 28)) { sd_up(); return; }
    if (!listed) return;
    scroll_list(&sd_scroll, &sd_focus, sd_n, 42, 198);
    if (IN.down & KEY_X && sd_n) {
        if (sd_entries[sd_focus].protected || install_active()) toast(T("sd.cantDelete"), AMBER);
        else sd_confirm = true;
        return;
    }
    bool open = IN.down & (KEY_A | KEY_RIGHT);
    for (int i = 0; i < sd_n; i++) if (36 + i * 42 - sd_scroll >= 36 && hit(6, 36 + i * 42 - sd_scroll, 308, 39)) {
        if (i == sd_focus) open = true; else sd_focus = i, sfx("move");
    }
    if (open && sd_n && sd_entries[sd_focus].dir) {
        const char *name = sd_entries[sd_focus].name;
        if (!strcasecmp(sd_dir, "sdmc:/") && !strcasecmp(name, "Nintendo 3DS")) { toast(T("sd.locked"), AMBER); return; }
        if (strlen(sd_dir) + strlen(name) + 2 < sizeof sd_dir) {
            strcat(sd_dir, name); strcat(sd_dir, "/"); sd_focus = 0; sd_scan(); sfx("select");
        }
    }
}
static const char *sd_tail(const char *path) {
    if (strlen(path) < 38) return path;
    const char *end = path + strlen(path), *p = end;
    if (p > path && p[-1] == '/') p--;
    int slash = 0;
    while (p > path) { p--; if (*p == '/' && ++slash == 2) return p; }
    return path;
}
static void sd_top(float a) {
    draw_bg(true, a); panel_title(T("sd.title"), a);
    if (!sd_shown) { text(T("intro.reading"), 200, 100, 11, C(TH.ink_soft, a), AL_CENTER); capacity(a); return; }
    SdEntry *e = sd_n ? &sd_entries[sd_focus] : NULL;
    if(!e){char path[800];snprintf(path,sizeof path,"%s",sd_dir);size_t n=strlen(path);if(n>6&&path[n-1]=='/')path[n-1]=0;rrect(12,67,44,44,12,C(TH.surface2,a));draw_icon("folder",34,89,24,C(TH.ink_soft,a));text_style(path,66,74,12,900,0,C(TH.ink,a),AL_LEFT);text_style(T("sd.empty"),66,90,10,600,0,C(TH.ink_faint,a),AL_LEFT);capacity(a);return;}
    glass(12,67,376,66,12,a);
    rrect(21,78,44,44,12,C(TH.surface2,a));
    if(e&&e->owner)draw_app_icon(e->owner,43,100,32,a);
    else draw_icon(e&&!e->dir?"file":"folder",43,100,24,C(e&&e->dir?AMBER:TH.ink_faint,a));
    scissor(400,75,74,304,53);
    text_style(e?e->name:T("sd.empty"),75,75,12,900,0,C(TH.ink,a),AL_LEFT);
    char path[1100], brief[256], b[96];
    snprintf(path, sizeof path, "%s%s", sd_dir, e ? e->name : "");
    snprintf(brief, sizeof brief, "%s%s", strlen(path) > 38 ? "sdmc:/…" : "", sd_tail(path));
    text_style(brief,75,91,10,600,0,C(TH.ink_faint,a),AL_LEFT);
    float cx=75;
    if(e&&e->owner){tvars(b,sizeof b,T("sd.createdBy"),"app",e->owner->title,NULL,NULL);cx+=chip(cx,109,b,NULL,C(FORGE,1),C(TH.surface2,1),a)+3;}
    else if(e&&e->protected)cx+=chip(cx,109,T("sd.system"),"lock",C(TH.ink_soft,1),C(TH.surface2,1),a)+3;
    if (e) {
        if(!__atomic_load_n(&e->counted,__ATOMIC_ACQUIRE))snprintf(b,sizeof b,"…");  // still being measured
        else if(e->size_known)fmt_size(b,sizeof b,e->size);
        else {char n[16];snprintf(n,sizeof n,"%d",e->children);tvars(b,sizeof b,T(e->children==1?"sd.item":"sd.items"),"n",n,NULL,NULL);}
        cx+=chip(cx,109,b,NULL,C(TH.ink_soft,1),C(TH.surface2,1),a)+3;
        struct tm tm;localtime_r(&e->modified,&tm);char iso[16];strftime(iso,sizeof iso,"%Y-%m-%d",&tm);fmt_date(b,sizeof b,iso);
        if(e->modified)chip(cx,109,b,NULL,C(TH.ink_soft,1),C(TH.surface2,1),a);
    }
    scissor_off();
    if (sd_error[0]) text_wrap(sd_error, 12, 175, 10, C(DANGER, a), 376);
    capacity(a);
}
static void sd_bottom(float a) {
    draw_bg(false, 1);
    char crumb[256]; snprintf(crumb, sizeof crumb, "%s%s", strlen(sd_dir) > 38 ? "sdmc:/…" : "", sd_tail(sd_dir));
    size_t crumb_len=strlen(crumb);if(crumb_len>6&&crumb[crumb_len-1]=='/')crumb[crumb_len-1]=0;
    panel_bar(crumb, a);
    bool listed=sd_shown;int shown=listed?sd_n:0;
    char count[32]="",digits[16];snprintf(digits,sizeof digits,"%d",shown);if(listed)tvars(count,sizeof count,T(shown==1?"sd.item":"sd.items"),"n",digits,NULL,NULL);
    rect(36,4,280,28,C(TH.bg,a));
    text_style(count,314,11,10,800,0,C(TH.ink_faint,a),AL_RIGHT);
    scissor(320,38,4,270-text_style_w(count,10,800,0),28);text_style(crumb,38,11,10,700,0,C(TH.ink,a),AL_LEFT);scissor_off();
    if (!listed) { text_style(T("intro.reading"),160,123,10,800,0,C(TH.ink_faint,a),AL_CENTER); return; }
    scissor(320, 0, 36, 320, 198);
    for (int i = 0; i < sd_n; i++) {
        float y = 36 + i * 42 - sd_scroll;
        if (y + 39 < 36 || y > 234) continue;
        SdEntry *e = &sd_entries[i];
        glass(6, y, 308, 39, 8, a);
        if (i == sd_focus) rrect_outline(6, y, 308, 39, 8, 2, C(FORGE, a));
        rrect(13, y + 10, 20, 20, 6, C(e->owner ? FORGE : e->dir ? AMBER : TH.ink_faint, a));
        const char *ext=strrchr(e->name,'.');bool executable=ext&&(!strcasecmp(ext,".3dsx")||!strcasecmp(ext,".cia"));
        draw_icon(e->dir?"folder":executable?"cart":"file",23,y+20,12,C(0xffffff,a));
        scissor(320, 40, fmaxf(36, y), e->owner ? 244 : 267, fminf(234, y + 39) - fmaxf(36, y));
        char label[264];snprintf(label,sizeof label,"%s%s",e->name,e->dir?"/":"");text_style(label,40,y+5,11,600,0,C(TH.ink,a),AL_LEFT);
        char b[64];
        if (e->dir && !__atomic_load_n(&e->counted, __ATOMIC_ACQUIRE)) snprintf(b, sizeof b, "…");
        else if (e->dir) { char n[16]; snprintf(n, sizeof n, "%d", e->children); tvars(b, sizeof b, T(e->children == 1 ? "sd.item" : "sd.items"), "n", n, NULL, NULL); }
        else fmt_size(b, sizeof b, e->size);
        text(b, 40, y + 20, 10, C(TH.ink_soft, a), AL_LEFT);
        scissor(320, 0, 36, 320, 198);
        if (e->owner) draw_app_icon(e->owner, 299, y + 20, 16, a);
    }
    if(!sd_n){draw_icon("folder",160,121,24,C(TH.ink_faint,a));text_style(T("sd.empty"),160,139,10,800,0,C(TH.ink_faint,a),AL_CENTER);}
    scissor_off();
    if (sd_confirm) {
        rect(0, 0, 320, 240, C(0x000000, .65f * a));
        rrect(8, 60, 304, 152, 12, C(TH.surface, a));
        text(T("sd.delete"), 20, 74, 14, C(TH.ink, a), AL_LEFT);
        text_wrap(sd_entries[sd_focus].name, 20, 101, 11, C(TH.ink_soft, a), 280);
        button(20, 172, 132, 28, T("action.cancel"), NULL, NULL, BTN_SURFACE, a);
        button(168, 172, 132, 28, T("sd.delete"), "trash", NULL, BTN_PRIMARY, a);
    }
}

// GitHub: independent requests, rich documents and a shared V2 tab bar.
#include "gh_markup.h"
#include "gh_releases.h"
#include "remote_image.h"
static int gh_parent, gh_tab, gh_focus, gh_link = -1;
static bool gh_reflow, gh_document_dark;
static bool gh_user, gh_loading, gh_dirty, gh_requested[4], gh_done[4], gh_discard[4];
static char gh_name[160], gh_error[160], gh_branch[128] = "main";
static char *gh_docs[3];
static json_t *gh_meta, *gh_repos, *gh_releases, *gh_commits;
static float gh_scroll, gh_height, gh_header_h, gh_y[GH_DOC_NODES], gh_h[GH_DOC_NODES], gh_x[GH_DOC_NODES];
static float gh_repo_y[40],gh_repo_h[40],gh_repo_label_y;
static GhFlow gh_flow;
static GhTables gh_tables;
static unsigned gh_run_first[GH_DOC_NODES],gh_run_count[GH_DOC_NODES];
static float gh_flow_measure(const char *text,size_t bytes,unsigned char style,void *context){
    (void)context;char part[1024];if(bytes>=sizeof part)bytes=sizeof part-1;memcpy(part,text,bytes);part[bytes]=0;
    return text_style_w(part,style&64?10:style&(8|16)?9:11,style&64?600:style&2?700:500,style&64?FONT_MONO:FONT_UI);
}
static float gh_release_y[12], gh_release_h[12];
static int gh_container[GH_DOC_NODES];
static const char *gh_fact_keys[8];
static char gh_fact_values[8][256];
static int gh_fact_count,gh_topic_count;
static float gh_fact_y[8],gh_fact_h[8],gh_topic_x[10],gh_topic_y[10],gh_home_y[2],gh_commit_label_y,gh_commit_y[10];
static GhDocument gh_document;
static UiTabs gh_nav;
static struct { bool have; int parent, focus; float scroll; char name[160]; } gh_return;
static const char *jstr(json_t *o, const char *key) { const char *s = json_string_value(json_object_get(o, key)); return s ? s : ""; }
static void append_doc(char **out, const char *s) {
    if (!s) return;
    size_t n = *out ? strlen(*out) : 0;
    if (n >= GH_DOC_BYTES - 1) return;
    size_t m = strlen(s), cap = GH_DOC_BYTES - n - 1;
    if (m > cap) m = cap;
    char *p = realloc(*out, n + m + 2); if (!p) return;
    memcpy(p + n, s, m); p[n + m] = '\n'; p[n + m + 1] = 0; *out = p;
}
static void gh_info(void) {
    free(gh_docs[2]); gh_docs[2] = NULL;
    char b[700], size[32];
    if (!gh_meta) return;
    if (gh_user) {
        const char *keys[] = {"location", "company", "blog", "created_at"};
        for (int i = 0; i < 4; i++) append_doc(&gh_docs[2], jstr(gh_meta, keys[i]));
        snprintf(b, sizeof b, "%s: %lld · %s: %lld", T("gh.followers"), json_integer_value(json_object_get(gh_meta,"followers")), T("gh.following"), json_integer_value(json_object_get(gh_meta,"following")));
        append_doc(&gh_docs[2], b);
    } else {
        json_t *topics = json_object_get(gh_meta,"topics"), *topic; size_t ti;
        json_array_foreach(topics, ti, topic) append_doc(&gh_docs[2], json_string_value(topic));
        const char *labels[] = {"gh.language","detail.license","gh.branch","gh.updated","gh.created","gh.size"};
        fmt_size(size, sizeof size, json_integer_value(json_object_get(gh_meta,"size"))*1024.);
        const char *vals[] = {jstr(gh_meta,"language"),jstr(json_object_get(gh_meta,"license"),"name"),jstr(gh_meta,"default_branch"),jstr(gh_meta,"pushed_at"),jstr(gh_meta,"created_at"),size};
        for (int i = 0; i < 6; i++) { snprintf(b,sizeof b,"**%s:** %s",T(labels[i]),vals[i]); append_doc(&gh_docs[2],b); }
        if (*jstr(gh_meta,"homepage")) { snprintf(b,sizeof b,"[%s](%s)",jstr(gh_meta,"homepage"),jstr(gh_meta,"homepage")); append_doc(&gh_docs[2],b); }
        snprintf(b,sizeof b,"## %s",T("gh.commits")); append_doc(&gh_docs[2],b);
        size_t i; json_t *c;
        json_array_foreach(gh_commits,i,c) {
            json_t *commit=json_object_get(c,"commit"), *author=json_object_get(commit,"author");
            snprintf(b,sizeof b,"### %.7s · %.400s",jstr(c,"sha"),jstr(commit,"message"));
            char *nl=strchr(b,'\n'); if(nl)*nl=0; append_doc(&gh_docs[2],b);
            snprintf(b,sizeof b,"%s · %.10s",jstr(author,"name"),jstr(author,"date")); append_doc(&gh_docs[2],b);
        }
    }
}
static void gh_info_facts(void){
    gh_fact_count=6;
    const char *repo_keys[]={"gh.language","detail.license","gh.branch","gh.updated","gh.created","gh.size"};
    const char *user_keys[]={"gh.followers","gh.following","gh.repos","gh.joined","gh.updated","gh.location"};
    for(int i=0;i<6;i++)gh_fact_keys[i]=gh_user?user_keys[i]:repo_keys[i];
    if(gh_user){const char *numeric[]={"followers","following","public_repos"};for(int i=0;i<3;i++)fmt_stars(gh_fact_values[i],sizeof gh_fact_values[i],json_integer_value(json_object_get(gh_meta,numeric[i])));
        fmt_date(gh_fact_values[3],sizeof gh_fact_values[3],jstr(gh_meta,"created_at"));fmt_date(gh_fact_values[4],sizeof gh_fact_values[4],jstr(gh_meta,"updated_at"));
        snprintf(gh_fact_values[5],sizeof gh_fact_values[5],"%s",*jstr(gh_meta,"location")?jstr(gh_meta,"location"):"—");
        json_t *r;size_t i;int total=0;json_array_foreach(gh_repos,i,r)total+=json_integer_value(json_object_get(r,"stargazers_count"));
        if(total){gh_fact_keys[6]="gh.totalStars";fmt_stars(gh_fact_values[6],sizeof gh_fact_values[6],total);gh_fact_count=7;}
    }else{
        json_t *license=json_object_get(gh_meta,"license");const char *id=jstr(license,"spdx_id");
        const char *values[]={jstr(gh_meta,"language"),*id&&strcmp(id,"NOASSERTION")?id:jstr(license,"name"),jstr(gh_meta,"default_branch")};
        for(int i=0;i<3;i++)snprintf(gh_fact_values[i],sizeof gh_fact_values[i],"%s",*values[i]?values[i]:"—");
        fmt_date(gh_fact_values[3],sizeof gh_fact_values[3],jstr(gh_meta,"pushed_at"));fmt_date(gh_fact_values[4],sizeof gh_fact_values[4],jstr(gh_meta,"created_at"));
        fmt_size(gh_fact_values[5],sizeof gh_fact_values[5],json_integer_value(json_object_get(gh_meta,"size"))*1024.);
    }
}
static void gh_begin(const char *name, bool user) {
    gh_user=user; gh_tab=gh_focus=0;gh_link=-1;gh_scroll=0;gh_loading=true;gh_dirty=true;
    snprintf(gh_name,sizeof gh_name,"%s",name?name:"");snprintf(gh_branch,sizeof gh_branch,"main"); gh_error[0]=0;
    memset(&gh_nav,0,sizeof gh_nav);
    for(int i=0;i<4;i++){ reader_cancel_slot(i);gh_discard[i]=true;gh_requested[i]=false;gh_done[i]=user&&i==3; }
    for(int i=0;i<3;i++){free(gh_docs[i]);gh_docs[i]=NULL;}
    json_decref(gh_meta);json_decref(gh_repos);json_decref(gh_releases);json_decref(gh_commits);
    gh_meta=gh_repos=gh_releases=gh_commits=NULL;
    gh_document_free(&gh_document); gh_header_h=38; gh_height=60;
    gh_flow_reset(&gh_flow);memset(&gh_tables,0,sizeof gh_tables);
    memset(gh_release_h,0,sizeof gh_release_h);memset(gh_run_count,0,sizeof gh_run_count);
    gh_fact_count=gh_topic_count=0;gh_reflow=true;
}
static void open_github(const char *name, bool user) {
    if(scene!=SC_GITHUB){gh_parent=scene;gh_return.have=false;}
    gh_begin(name,user);sfx("open");go(SC_GITHUB);
}
static void gh_poll(void) {
    if(!gh_loading) return;
    for(int i=0;i<4;i++) {
        char error[160];char *data=reader_take_slot(i,error,sizeof error);
        if(gh_discard[i]) {free(data);if(!reader_slot_busy(i)) gh_discard[i]=false;else continue;data=NULL;}
        if(data && gh_requested[i]) {
            gh_done[i]=true;
            if(error[0]) {
                // A missing optional profile README is not an error for the profile itself.
                if(!(gh_user&&i==2)) snprintf(gh_error,sizeof gh_error,"%s",error);
            } else if((gh_user&&i==2)||(!gh_user&&i==1)) {
                free(gh_docs[0]);gh_docs[0]=data;data=NULL;
            } else {
                json_t *j=json_loads(data,0,NULL);
                if(!j) snprintf(gh_error,sizeof gh_error,"Invalid GitHub response");
                else if(i==0) {
                    json_decref(gh_meta);gh_meta=j;j=NULL;
                    if(!gh_user) {snprintf(gh_branch,sizeof gh_branch,"%s",*jstr(gh_meta,"default_branch")?jstr(gh_meta,"default_branch"):"main");
                        for(int a=0;a<NAPPS;a++)if(APPS[a].repo&&!strcmp(APPS[a].repo,gh_name)){APPS[a].stars=json_integer_value(json_object_get(gh_meta,"stargazers_count"));APPS[a].has_stars=true;}}
                } else if(gh_user&&i==1) {
                    gh_repos=json_array();size_t ri;json_t *r;json_array_foreach(j,ri,r) if(!json_is_true(json_object_get(r,"fork")))json_array_append(gh_repos,r);
                } else if(i==2) {gh_releases=j;j=NULL;}
                else if(i==3) {gh_commits=j;j=NULL;}
                json_decref(j);gh_info();
            }
            free(data);gh_dirty=true;
        } else free(data);
        if(!gh_requested[i]&&!gh_done[i]&&!reader_slot_busy(i)) {
            char path[256];bool raw=false;
            if(gh_user) {
                if(i==0)snprintf(path,sizeof path,"users/%s",gh_name);
                else if(i==1)snprintf(path,sizeof path,"users/%s/repos?sort=pushed&per_page=40",gh_name);
                else {snprintf(path,sizeof path,"repos/%.100s/%.100s/readme",gh_name,gh_name);raw=true;}
            } else {
                if(i==0)snprintf(path,sizeof path,"repos/%s",gh_name);
                else if(i==1){snprintf(path,sizeof path,"repos/%s/readme",gh_name);raw=true;}
                else if(i==2)snprintf(path,sizeof path,"repos/%s/releases?per_page=12",gh_name);
                else snprintf(path,sizeof path,"repos/%s/commits?per_page=10",gh_name);
            }
            int active=0;for(int k=0;k<4;k++)active+=reader_slot_busy(k);
            if(active<2)gh_requested[i]=reader_request_slot(i,path,raw);
        }
    }
    gh_loading=false;for(int i=0;i<4;i++)if(!gh_done[i])gh_loading=true;
}
static void gh_tabs_setup(void) {
    const char *labels[]={T(gh_user?"gh.profile":"gh.readme"),T(gh_user?"gh.repos":"gh.releases"),T(gh_user?"gh.about":"gh.info")};
    tabs_layout(&gh_nav,6,36,308,labels,NULL,3,gh_tab);
}
static void gh_image_size(const GhNode *n,float maxw,float *w,float *h) {
    int iw=0,ih=0;bool have=remote_image_dimensions(n->url,&iw,&ih);
    bool badge=strstr(n->url,"badge")||strstr(n->url,"shields.io");
    *h=badge?13:have?fminf(240,ih):90;
    *w=have?fminf(maxw,*h*iw/fmaxf(1,ih)):badge?65:maxw;
    if(have&&*w<*h*iw/fmaxf(1,ih))*h=*w*ih/iw;
}
static void gh_table_image_size(const GhNode *node,float width,float *w,float *h,void *context){(void)context;gh_image_size((GhNode*)node,width,w,h);}
static void gh_layout(void) {
    if(gh_document_dark!=S.dark)gh_dirty=true;
    if(!gh_dirty&&!gh_reflow)return;
    gh_reflow=false;gh_flow_reset(&gh_flow);memset(&gh_tables,0,sizeof gh_tables);memset(gh_run_count,0,sizeof gh_run_count);
    if(gh_dirty) {
        char repo[224];snprintf(repo,sizeof repo,gh_user?"%s/%s":"%s",gh_name,gh_name);
        if(gh_tab==2){gh_document_free(&gh_document);gh_info_facts();}
        else if(!gh_user&&gh_tab==1)gh_document_releases(&gh_document,gh_releases,repo,gh_branch,S.dark);
        else gh_document_parse(&gh_document,gh_docs[gh_tab]?gh_docs[gh_tab]:"",repo,gh_branch,S.dark);
        gh_document_dark=S.dark;gh_dirty=false;
    }
    const char *description=gh_user?jstr(gh_meta,"bio"):jstr(gh_meta,"description");
    gh_header_h=gh_meta?fmaxf(gh_user?62:50,52+text_wrap_h(description,11,gh_user?302:314)):38;
    float y=gh_header_h+8;
    if(gh_tab==2&&gh_meta){
        gh_topic_count=0;float tx=12,rowh=0;
        if(!gh_user){json_t *topic;size_t i;json_array_foreach(json_object_get(gh_meta,"topics"),i,topic){if(i>=10)break;const char *label=json_string_value(topic);if(!label)continue;float w=text_style_w(label,9,600,0)+13;
            if(tx+w>388&&tx>12){tx=12;y+=19;}gh_topic_x[gh_topic_count]=tx;gh_topic_y[gh_topic_count++]=y;tx+=w+3;rowh=16;
        }}if(rowh)y+=rowh+7;
        for(int i=0;i<gh_fact_count;i+=2){float h=40;
            for(int j=i;j<i+2&&j<gh_fact_count;j++)h=fmaxf(h,19+text_wrap_h_style(gh_fact_values[j],11,600,172)+6);
            for(int j=i;j<i+2&&j<gh_fact_count;j++){gh_fact_y[j]=y;gh_fact_h[j]=h;}y+=h+4;
        }y-=4;
        for(int i=0;i<2;i++){gh_home_y[i]=-1;const char *value=jstr(gh_meta,gh_user?(i?"company":"blog"):(i?"":"homepage"));if(*value){gh_home_y[i]=y+6;y+=6+text_wrap_h_style(value,9,800,362);}}
        gh_commit_label_y=y+10;
        if(!gh_user){y+=24;int n=json_array_size(gh_commits);if(n>10)n=10;for(int i=0;i<n;i++){gh_commit_y[i]=y;y+=40;}}
    }else if(gh_user&&gh_tab==1&&gh_repos) {
        gh_repo_label_y=y+10;y+=31;
        size_t i;json_t *r;json_array_foreach(gh_repos,i,r) {
            if(i>=40)break;
            gh_repo_y[i]=y;const char *description=jstr(r,"description");gh_repo_h[i]=46+(*description?2+text_wrap_h_style(description,11,600,360):0);y+=gh_repo_h[i]+4;
        }
    } else {int release=-1;memset(gh_release_h,0,sizeof gh_release_h);
      for(int i=0;i<gh_document.count;i++) {
        GhNode *n=&gh_document.nodes[i];gh_container[i]=release;float left=release>=0?21:12,wide=release>=0?358:376;
        if(n->kind==GH_HEADING&&release<0&&i>0)y+=gh_document.nodes[i-1].kind==GH_HEADING?6:4;
        gh_y[i]=y;gh_x[i]=left;float h;
        if(n->table){
            unsigned first=gh_tables.placements;float height;
            int end=gh_table_layout(&gh_tables,&gh_flow,&gh_document,i,left,wide,y,&height,gh_flow_measure,gh_table_image_size,NULL);
            for(unsigned k=first;k<gh_tables.placements;k++){GhTableNode *p=&gh_tables.nodes[k];gh_container[p->node]=release;gh_x[p->node]=p->x;gh_y[p->node]=p->y;gh_h[p->node]=p->h;gh_run_first[p->node]=p->first;gh_run_count[p->node]=p->count;}
            y+=height+6;i=end-1;continue;
        }
        if(n->kind==GH_RELEASE){release=n->level;gh_release_y[release]=y;gh_h[i]=16;gh_y[i]=y+7;gh_x[i]=21;y+=28;continue;}
        if(n->kind==GH_RELEASE_END){gh_h[i]=0;gh_release_h[release]=y-gh_release_y[release]+7;y+=13;release=-1;continue;}
        if(n->kind==GH_ASSET){
            int end=i;while(end<gh_document.count&&gh_document.nodes[end].kind==GH_ASSET)end++;
            for(int k=i;k<end;k++){gh_container[k]=release;gh_x[k]=21+(k-i)%2*180.5f;gh_y[k]=y+(k-i)/2*23;gh_h[k]=20;}
            y+=((end-i+1)/2)*23+3;i=end-1;continue;
        }
        if(n->kind==GH_LINK&&!n->text[0]&&i>0&&gh_document.nodes[i-1].kind==GH_IMAGE){gh_y[i]=gh_y[i-1];gh_x[i]=gh_x[i-1];gh_h[i]=gh_h[i-1];continue;}
        if(n->kind==GH_IMAGE){
            int end=i,images=0;float width=0,rowh=0;
            while(end<gh_document.count&&gh_document.nodes[end].group==n->group){GhNode *part=&gh_document.nodes[end];
                if(part->kind==GH_IMAGE){float iw,ih;gh_image_size(part,wide,&iw,&ih);width+=iw+(images?3:0);rowh=fmaxf(rowh,ih);images++;}
                else if(!(part->kind==GH_LINK&&!part->text[0])&&!(part->kind==GH_TEXT&&strspn(part->text," \t")==strlen(part->text)))break;
                end++;
            }
            if(images&&width<=wide){float x=left+(wide-width)/2;int image=-1;
                for(int k=i;k<end;k++){GhNode *part=&gh_document.nodes[k];gh_container[k]=release;
                    if(part->kind==GH_IMAGE){float iw,ih;gh_image_size(part,wide,&iw,&ih);gh_x[k]=x;gh_y[k]=y+(rowh-ih)/2;gh_h[k]=ih;x+=iw+3;image=k;}
                    else if(part->kind==GH_LINK&&image>=0){gh_x[k]=gh_x[image];gh_y[k]=gh_y[image];gh_h[k]=gh_h[image];}
                    else {gh_x[k]=x;gh_y[k]=y;gh_h[k]=0;}
                }y+=rowh+6;i=end-1;continue;
            }
        }
        if(n->kind==GH_TEXT||n->kind==GH_LINK){
            int end=i;while(end<gh_document.count&&gh_document.nodes[end].group==n->group&&(gh_document.nodes[end].kind==GH_TEXT||gh_document.nodes[end].kind==GH_LINK))end++;
            unsigned first=gh_flow.count;
            bool list=n->style&32;float indent=list?14*(n->list_depth+1):0;
            bool sub=true;for(int k=i;k<end;k++)if(!(gh_document.nodes[k].style&8))sub=false;
            // HTML sub's baseline expands a wrapped 15px line box at 11px parent
            // size. Preserve that spacing instead of compressing the footer.
            float row=sub?18.1875f:15;
            float height=gh_flow_layout(&gh_flow,&gh_document,i,end,left+indent,wide-indent,y,row,n->style&1,gh_flow_measure,NULL);
            for(int k=i;k<end;k++){gh_container[k]=release;gh_h[k]=0;gh_run_first[k]=gh_flow.count;}
            for(unsigned k=first;k<gh_flow.count;k++){
                GhFlowRun *run=&gh_flow.runs[k];int node=run->node;
                if(!gh_run_count[node]){gh_run_first[node]=k;gh_x[node]=run->x;gh_y[node]=run->y;}
                gh_run_count[node]++;gh_h[node]=run->y-gh_y[node]+row;
            }
            bool br=(n->style&4)&&end<gh_document.count&&(gh_document.nodes[end].style&4)&&gh_document.nodes[end].group==n->group+1;
            y+=height+(br?0:list&&end<gh_document.count&&(gh_document.nodes[end].style&32)?1:6);i=end-1;continue;
        }
        if(n->kind==GH_IMAGE){float iw;gh_image_size(n,wide,&iw,&h);gh_x[i]=left+(wide-iw)/2;}
        else if(n->kind==GH_RULE)h=8;
        else if(n->kind==GH_CODE)h=text_wrap_h_family(n->text,10,600,FONT_MONO,wide-16)+12;
        else h=text_wrap_h_style(n->text,n->kind==GH_HEADING?(n->level<=2?14:12):n->kind==GH_CODE||n->kind==GH_TABLE?10:11,n->kind==GH_HEADING||n->style&2?700:500,n->kind==GH_CODE||n->kind==GH_QUOTE?wide-16:wide)+(n->kind==GH_CODE?12:n->kind==GH_HEADING&&n->level<=2?4:0);
        gh_h[i]=h;
        bool flow=n->kind==GH_TEXT||n->kind==GH_LINK;
        if(flow && i>0 && n->group==gh_document.nodes[i-1].group && (gh_document.nodes[i-1].kind==GH_TEXT||gh_document.nodes[i-1].kind==GH_LINK)){
            GhNode *prev=&gh_document.nodes[i-1];float end=gh_x[i-1]+text_style_w(prev->text,11,prev->style&2?700:500,0);
            float width=text_style_w(n->text,11,n->style&2?700:500,0);
            if(end+width<=left+wide && gh_h[i-1]<=15 && h<=15){gh_x[i]=end;gh_y[i]=gh_y[i-1];y=gh_y[i]+h+6;continue;}
        }
        if(n->style&1 && flow && text_style_w(n->text,11,n->style&2?700:500,0)<wide)gh_x[i]=200-text_style_w(n->text,11,n->style&2?700:500,0)/2;
        y+=h+(n->kind==GH_HEADING?4:6);
      }
    }
    if(gh_document.truncated||gh_flow.truncated||gh_tables.truncated)y+=32;
    gh_height=y+16; // .gh-page includes the 2px top padding inside its scroll extent.
}
static void gh_open_url(const char *url) {
    if(url&&url[0]=='#') {
        for(int i=0;i<gh_document.count;i++)if(gh_document.nodes[i].kind==GH_HEADING){
            char slug[512];size_t n=0;const unsigned char *s=(const unsigned char *)gh_document.nodes[i].text;
            for(;*s&&n<sizeof slug-1;s++){if(*s==' '||*s=='-')slug[n++]='-';else if(*s>=128||isalnum(*s)||*s=='_')slug[n++]=tolower(*s);}slug[n]=0;
            if(!strcmp(url+1,slug)){gh_scroll=clampf(gh_y[i],0,fmaxf(0,gh_height-206));return;}
        }return;
    }
    if(!url||strncmp(url,"https://",8)||strlen(url)>=1024) return;
    // Internet Browser applet parameters start with a NUL-terminated URL.
    char parameter[1024]={0};snprintf(parameter,sizeof parameter,"%s",url);
    C3D_FrameEnd(0);aptLaunchSystemApplet(APPID_WEB,parameter,sizeof parameter,0);C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
}
static void github_update(void) {
    gh_poll();gh_tabs_setup();
    if(remote_image_changed())gh_reflow=true;
    gh_layout();
    if(IN.down&KEY_B||hit(4,4,28,28)) {
        if(gh_return.have){char name[160];snprintf(name,sizeof name,"%s",gh_return.name);int focus=gh_return.focus;float scroll=gh_return.scroll;gh_parent=gh_return.parent;gh_return.have=false;gh_begin(name,true);gh_tab=1;gh_focus=focus;gh_scroll=scroll;}
        else go(gh_parent);
        sfx("back");return;
    }
    int tab=tabs_input(&gh_nav,false);
    if(tab>=0&&tab!=gh_tab){gh_tab=tab;gh_scroll=0;gh_link=-1;gh_dirty=true;gh_tabs_setup();gh_layout();sfx("tab");}
    bool repos=gh_user&&gh_tab==1&&gh_repos;
    if(IN.touching&&!IN.touch_down&&IN.start_y>=70&&IN.start_y<204)gh_scroll+=IN.last_y-IN.ty;
    if(repos) {
        int n=json_array_size(gh_repos); if(n>40)n=40;
        gh_focus=(int)clampf(gh_focus,0,fmaxf(0,n-1));
        if(IN.repeat&(KEY_UP|KEY_DOWN|KEY_LEFT|KEY_RIGHT)) {
            gh_focus=(int)clampf(gh_focus+(IN.repeat&(KEY_DOWN|KEY_RIGHT)?1:-1),0,fmaxf(0,n-1));
            if(n) gh_scroll=clampf(gh_scroll,gh_repo_y[gh_focus]+gh_repo_h[gh_focus]-206,gh_repo_y[gh_focus]);
            sfx("move");
        }
        if(IN.down&KEY_A&&n) {
            char name[160];snprintf(name,sizeof name,"%s",jstr(json_array_get(gh_repos,gh_focus),"full_name"));
            gh_return.have=true;gh_return.parent=gh_parent;gh_return.focus=gh_focus;gh_return.scroll=gh_scroll;snprintf(gh_return.name,sizeof gh_return.name,"%s",gh_name);
            gh_begin(name,false);return;
        }
    } else {
        if(IN.held&KEY_DOWN) gh_scroll+=DT*130;
        if(IN.held&KEY_UP) gh_scroll-=DT*130;
        if(IN.down&(KEY_LEFT|KEY_RIGHT)) {
            int d=IN.down&KEY_RIGHT?1:-1;
            for(int i=gh_link+d;i>=0&&i<gh_document.count;i+=d)if(gh_document.nodes[i].kind==GH_LINK){gh_link=i;gh_scroll=clampf(gh_scroll,gh_y[i]+gh_h[i]-206,gh_y[i]);break;}
        }
        if(IN.down&KEY_A&&gh_link>=0&&gh_link<gh_document.count)gh_open_url(gh_document.nodes[gh_link].url);
    }
    if(hit(6,209,100,24))gh_scroll-=150;
    if(hit(110,209,100,24))gh_scroll+=150;
    if(hit(214,209,100,24)){char url[192];snprintf(url,sizeof url,"https://github.com/%s",gh_name);gh_open_url(url);}
    gh_scroll=clampf(gh_scroll,0,fmaxf(0,gh_height-206));
    const char *avatar=gh_user?jstr(gh_meta,"avatar_url"):jstr(json_object_get(gh_meta,"owner"),"avatar_url");
    // Requests are prioritized by recency: the first visible images and avatar
    // should arrive before a large showcase farther down the document.
    for(int i=gh_document.count-1;i>=0;i--)if(gh_document.nodes[i].kind==GH_IMAGE&&gh_y[i]+gh_h[i]>gh_scroll&&gh_y[i]<gh_scroll+206)remote_image_request(gh_document.nodes[i].url);
    if(*avatar&&gh_scroll<gh_header_h)remote_image_request_rounded(avatar,gh_user?46:34,gh_user?23:9);
    if(!gh_user&&gh_tab==2){int count=json_array_size(gh_commits);if(count>10)count=10;for(int i=0;i<count;i++)if(gh_commit_y[i]+37>gh_scroll&&gh_commit_y[i]<gh_scroll+206)remote_image_request_rounded(jstr(json_object_get(json_array_get(gh_commits,i),"author"),"avatar_url"),16,8);}
}
static void github_top(float a) {
    draw_bg(true,a);scissor(400,0,34,400,206);
    char stats[160];
    float y=36-gh_scroll;
    if(y+gh_header_h>34){
    glass(12,y,376,gh_header_h,12,a);
    const char *avatar=gh_user?jstr(gh_meta,"avatar_url"):jstr(json_object_get(gh_meta,"owner"),"avatar_url");
    float av=gh_user?46:34,tx=21+av+9;
    if(gh_user)circle(21+av*.5f,y+8+av*.5f,av*.5f+2,C(TH.surface,a));
    if(*avatar)remote_image_draw_rounded(avatar,21,y+8,(int)av,gh_user?23:9,a);else draw_icon("github",36,y+20,20,C(TH.ink,a));
    const char *title=*jstr(gh_meta,gh_user?"name":"full_name")?jstr(gh_meta,gh_user?"name":"full_name"):gh_name;
    if(!gh_user&&gh_meta){char owner[180];snprintf(owner,sizeof owner,"%s / ",jstr(json_object_get(gh_meta,"owner"),"login"));text_style(owner,tx,y+8,12,700,0,C(TH.ink_soft,a),AL_LEFT);text_style(jstr(gh_meta,"name"),tx+text_style_w(owner,12,700,0),y+8,12,900,0,C(TH.ink,a),AL_LEFT);}
    else text_style(title,tx,y+8,12,900,0,C(TH.ink,a),AL_LEFT);
    if(gh_user&&*jstr(gh_meta,"name")){char login[176];snprintf(login,sizeof login," @%s",jstr(gh_meta,"login"));text(login,tx+text_style_w(title,12,900,0),y+8,12,C(TH.ink_soft,a),AL_LEFT);}
    text_wrap(gh_user?jstr(gh_meta,"bio"):jstr(gh_meta,"description"),tx,y+26,11,C(TH.ink_soft,a),388-tx-9);
    if(gh_user)snprintf(stats,sizeof stats,"%lld %s · %lld %s",json_integer_value(json_object_get(gh_meta,"followers")),T("gh.followers"),json_integer_value(json_object_get(gh_meta,"public_repos")),T("gh.repos"));
    if(gh_meta&&gh_user)text_style(stats,tx,y+30,10,800,0,C(TH.ink_soft,a),AL_LEFT);
    else if(gh_meta){const char *keys[]={"stargazers_count","forks_count","subscribers_count","open_issues_count"};const char *icons[]={"star","people","eye","warn"};float sx=tx,sy=y+gh_header_h-19;
        for(int k=0;k<4;k++){snprintf(stats,sizeof stats,"%lld",json_integer_value(json_object_get(gh_meta,keys[k])));draw_icon(icons[k],sx+4.5f,sy+7,9,C(TH.ink_soft,a));text_style(stats,sx+11,sy,10,800,0,C(TH.ink_soft,a),AL_LEFT);sx+=11+text_style_w(stats,10,800,0)+9;}
        if(*jstr(gh_meta,"language")){circle(sx+3,sy+7,3,C(FORGE,a));text_style(jstr(gh_meta,"language"),sx+8,sy,10,800,0,C(TH.ink_soft,a),AL_LEFT);}
    }
    }
    a=tabs_content(&gh_nav,a);
    if(gh_tab==2&&gh_meta){
        for(int i=0;i<gh_topic_count;i++)chip(gh_topic_x[i],36+gh_topic_y[i]-gh_scroll,json_string_value(json_array_get(json_object_get(gh_meta,"topics"),i)),NULL,C(LAV,1),C(LAV,.12f),a);
        for(int i=0;i<gh_fact_count;i++){float by=36+gh_fact_y[i]-gh_scroll;if(by+gh_fact_h[i]<34||by>=240)continue;float x=12+(i%2)*190;
            rrect(x,by,186,gh_fact_h[i],8,C(TH.surface,a));rrect_outline(x,by,186,gh_fact_h[i],8,1,C(TH.line,TH.line_a*a));
            text_caps(T(gh_fact_keys[i]),x+7,by+6,9,.5f,C(TH.ink_faint,a),AL_LEFT);text_wrap_style(gh_fact_values[i],x+7,by+19,11,600,C(TH.ink,a),172);
        }
        for(int i=0;i<2;i++)if(gh_home_y[i]>=0){float by=36+gh_home_y[i]-gh_scroll;draw_icon(i?"people":"globe",17,by+6,10,C(SKY,a));text_wrap_style(jstr(gh_meta,gh_user?(i?"company":"blog"):"homepage"),26,by,9,800,C(SKY,a),362);}
        if(!gh_user){float by=36+gh_commit_label_y-gh_scroll;text_caps(T("gh.commits"),12,by,9,1,C(TH.mint,a),AL_LEFT);int n=json_array_size(gh_commits);if(n>10)n=10;
            for(int i=0;i<n;i++){by=36+gh_commit_y[i]-gh_scroll;if(by+37<34||by>=240)continue;json_t *c=json_array_get(gh_commits,i),*commit=json_object_get(c,"commit"),*author=json_object_get(commit,"author");
                rrect(12,by,376,37,8,C(TH.surface,a));rrect_outline(12,by,376,37,8,1,C(TH.line,TH.line_a*a));
                const char *avatar=jstr(json_object_get(c,"author"),"avatar_url");if(*avatar)remote_image_draw_rounded(avatar,18,by+10.5f,16,8,a);else circle(26,by+18.5f,8,C(TH.surface3,a));
                char message[512],date[48],meta[320];snprintf(message,sizeof message,"%s",jstr(commit,"message"));char *nl=strchr(message,'\n');if(nl)*nl=0;
                text_fit_style(message,40,by+4,11,800,342,C(TH.ink,a));fmt_date(date,sizeof date,jstr(author,"date"));snprintf(meta,sizeof meta,"%s · %s · %.7s",jstr(author,"name"),date,jstr(c,"sha"));text_fit_style(meta,40,by+19,10,700,342,C(TH.ink_faint,a));
            }
        }
     }else if(gh_user&&gh_tab==1&&gh_repos) {
        char count[20],label[128];snprintf(count,sizeof count,"%u",(unsigned)json_array_size(gh_repos));tvars(label,sizeof label,T("gh.reposTitle"),"n",count,NULL,NULL);
        text_caps(label,12,36+gh_repo_label_y-gh_scroll,9,1,C(S.dark?0x8fdcc0:0x2f7a63,a),AL_LEFT);
        size_t i;json_t *r;json_array_foreach(gh_repos,i,r){if(i>=40)break;float by=36+gh_repo_y[i]-gh_scroll,h=gh_repo_h[i];if(by+h<34||by>=240)continue;
            glass(12,by,376,h,10,a);if(i==(size_t)gh_focus)rrect_outline(12,by,376,h,10,2,C(FORGE,a));
            draw_icon("box",25,by+14,10,C(SKY,a));text_fit_style(jstr(r,"name"),34,by+6,12,900,346,C(SKY,a));
            text_wrap_style(jstr(r,"description"),20,by+24,11,600,C(TH.ink_soft,a),360);
            float x=20,sy=by+h-20;const char *language=jstr(r,"language");
            if(*language){circle(x+3,sy+7,3,C(FORGE,a));text_style(language,x+8,sy,10,800,0,C(TH.ink_soft,a),AL_LEFT);x+=8+text_style_w(language,10,800,0)+9;}
            const char *icons[]={"star","people","clock"};
            for(int k=0;k<3;k++){if(k<2)fmt_stars(stats,sizeof stats,json_integer_value(json_object_get(r,k?"forks_count":"stargazers_count")));else fmt_date(stats,sizeof stats,jstr(r,"pushed_at"));draw_icon(icons[k],x+4.5f,sy+7,9,C(TH.ink_soft,a));text_style(stats,x+11,sy,10,800,0,C(TH.ink_soft,a),AL_LEFT);x+=11+text_style_w(stats,10,800,0)+9;}
        }
    } else {
      if(!gh_user&&gh_tab==1)for(int r=0;r<12;r++)if(gh_release_h[r]>0){float ry=36+gh_release_y[r]-gh_scroll;if(ry+gh_release_h[r]>=34&&ry<240){rrect(12,ry,376,gh_release_h[r],10,C(TH.surface,a));rrect_outline(12,ry,376,gh_release_h[r],10,1,C(TH.line,TH.line_a*a));}}
      for(unsigned i=0;i<gh_tables.count;i++){GhTableCell *cell=&gh_tables.cells[i];float by=36+cell->y-gh_scroll;if(by+cell->h>=34&&by<240)rrect_outline(cell->x,by,cell->w,cell->h,0,1,C(TH.line,TH.line_a*a));}
      for(int i=0;i<gh_document.count;i++) {
        GhNode *n=&gh_document.nodes[i];float by=36+gh_y[i]-gh_scroll,h=gh_h[i];if(by+h<34||by>=240)continue;
        float left=gh_container[i]>=0?21:12,wide=gh_container[i]>=0?358:376;
        if(n->kind==GH_RELEASE){json_t *rel=json_array_get(gh_releases,n->level);float x=21;text_style(n->text,x,by+1,10,900,0,C(TH.ink,a),AL_LEFT);x+=text_style_w(n->text,10,900,0)+5;bool pre=json_is_true(json_object_get(rel,"prerelease"));x+=chip(x,by,T(pre?"channel.experimental":"channel.stable"),NULL,C(pre?LAV:MINT,1),C(pre?LAV:MINT,.12f),a)+5;if(n->level==0)chip(x,by,T("detail.latest"),NULL,C(FORGE,1),C(FORGE,.12f),a);char date[48];fmt_date(date,sizeof date,jstr(rel,"published_at"));text_style(date,379,by+2,9,800,0,C(TH.ink_faint,a),AL_RIGHT);}
        else if(n->kind==GH_ASSET){json_t *rel=json_array_get(gh_releases,gh_container[i]),*asset=json_array_get(json_object_get(rel,"assets"),n->level);char size[32];fmt_size(size,sizeof size,json_integer_value(json_object_get(asset,"size")));float x=gh_x[i];rrect(x,by,177.5f,20,6,C(TH.surface2,a));draw_icon(strstr(n->text,".cia")?"cia":"file",x+9.5f,by+10,9,C(TH.ink_soft,a));float sw=text_style_w(size,10,700,0);text_fit_style(n->text,x+17,by+4,9,800,150-sw-4,C(TH.ink,a));text_style(size,x+172.5f,by+3,10,700,0,C(TH.ink_faint,a),AL_RIGHT);}
        else if(n->kind==GH_RELEASE_END)continue;
        else if(n->kind==GH_IMAGE){int iw=376,ih=90;remote_image_dimensions(n->url,&iw,&ih);float w=fminf(wide,h*iw/fmaxf(1,ih));remote_image_draw(n->url,gh_x[i],by,w,h,a);}
        else if(n->kind==GH_RULE)rect(12,by+3,376,1,C(TH.line,TH.line_a*a));
        else {
            float x=gh_x[i],w=left+wide-x,size=n->kind==GH_HEADING?(n->level<=2?14:12):n->kind==GH_CODE||n->kind==GH_TABLE?10:11;
            if(n->kind==GH_CODE){rrect(left,by,wide,h,6,C(TH.surface2,a));x+=7;by+=6;w-=14;}
            if(n->kind==GH_QUOTE){rect(left,by,2,h,C(TH.line,.4f*a));x+=8;w-=8;}
            if(n->kind==GH_LINK&&i==gh_link)rrect_outline(10,by-1,380,h+2,4,1,C(FORGE,a));
            if(n->kind==GH_HEADING&&(n->style&1))x=200-text_style_w(n->text,size,700,0)/2;
            u32 color=C(n->kind==GH_LINK?SKY:gh_container[i]>=0?TH.ink_soft:TH.ink,a);
            if(gh_run_count[i]){
                bool first_list=n->style&32;
                if(first_list&&i>0&&gh_document.nodes[i-1].group==n->group&&(gh_document.nodes[i-1].style&32))first_list=false;
                if(first_list){float bx=left+14*n->list_depth;if(n->level>0){char number[16];snprintf(number,sizeof number,"%d.",n->level);text_style(number,bx+11,by,11,500,0,color,AL_RIGHT);}else circle(bx+3.5f,by+7.5f,1.5f,color);}
                for(unsigned k=gh_run_first[i];k<gh_run_first[i]+gh_run_count[i];k++){
                    GhFlowRun *run=&gh_flow.runs[k];char part[1024];size_t bytes=run->end-run->begin;if(bytes>=sizeof part)bytes=sizeof part-1;
                    memcpy(part,n->text+run->begin,bytes);part[bytes]=0;
                    float size=n->style&64?10:n->style&(8|16)?9:11;int weight=n->style&64?600:n->style&2?700:500;
                    float ry=36+run->y-gh_scroll+(n->style&8?3:0);
                    if(n->style&64)rrect(run->x-2,ry-.5f,text_style_w(part,size,weight,FONT_MONO)+4,14,3,C(TH.surface2,a));
                    text_style(part,run->x,ry,size,weight,n->style&64?FONT_MONO:FONT_UI,color,AL_LEFT);
                }
            }else if(n->kind==GH_CODE)text_wrap_family(n->text,x,by,10,600,FONT_MONO,color,w);
            else text_wrap_style(n->text,x,by,size,n->kind==GH_HEADING||n->style&2?700:500,color,w);
            if(n->kind==GH_HEADING&&n->level<=2)rect(left,by+h-1,wide,1,C(TH.line,TH.line_a*a));
        }
      }
    }
    if(!gh_document.count&&!(gh_user&&gh_tab==1&&gh_repos)&&!(gh_tab==2&&gh_meta))text_wrap(gh_loading?T("gh.loading"):gh_error[0]?gh_error:T("gh.notFound"),12,36+gh_header_h+20-gh_scroll,11,C(TH.ink_soft,a),376);
    if(gh_document.truncated||gh_flow.truncated||gh_tables.truncated)text_wrap(T("native.documentLimit"),12,36+gh_height-44-gh_scroll,10,C(AMBER,a),376);
    DRAW_X=0;scissor_off();
}
static void github_bottom(float a) {
    draw_bg(false,1);back_button(a);gh_tabs_setup();
    draw_icon("github",44,18,12,C(TH.ink,a));
    text_fit_style(gh_name,54,10,12,700,260,C(TH.ink,a));
    const char *labels[]={T(gh_user?"gh.profile":"gh.readme"),T(gh_user?"gh.repos":"gh.releases"),T(gh_user?"gh.about":"gh.info")};tabs_draw(&gh_nav,labels,NULL,false,a);
    glass(6,70,308,134,14,a);
    for(float y=74.5f;y<204;y+=9)for(float x=10.5f;x<314;x+=9){
        float dx=fmaxf(fmaxf(20-x,x-300),0),dy=fmaxf(fmaxf(84-y,y-190),0);
        if(dx*dx+dy*dy<13*13)circle(x,y,1,C(S.dark?0xffffff:0x2c3e45,(S.dark?.14f:.18f)*a));
    }
    draw_icon("layers",160,119.5f,20,C(TH.ink_faint,a));
    text_style(T("gh.pad"),160,132.5f,12,900,0,C(TH.ink,a),AL_CENTER);
    text_style(T("gh.padHint"),160,150.5f,10,700,0,C(TH.ink_faint,a),AL_CENTER);
    float total=fmaxf(206,gh_height),th=clampf(206/total*104,12,104),p=total>206?gh_scroll/(total-206):0;
    rrect(300,84,5,104,3,C(TH.surface3,a));rrect(300,84+p*(104-th),5,th,3,C(FORGE,a));
    char b[24];snprintf(b,sizeof b,"%d %%",(int)roundf(p*100));text_style(b,16,184,9,900,0,C(TH.ink_faint,a),AL_LEFT);
    button_shape(6,209,100,24,T("gh.pageUp"),"update",NULL,BTN_SURFACE,a,11);
    button_shape(110,209,100,24,T("gh.pageDown"),"download",NULL,BTN_SURFACE,a,11);
    button_shape(214,209,100,24,"github.com","link",NULL,BTN_GHOST,a,11);
}
static void github_cleanup(void) {
    gh_flow_free(&gh_flow);gh_document_free(&gh_document);for(int i=0;i<3;i++){free(gh_docs[i]);gh_docs[i]=NULL;}
    json_decref(gh_meta);json_decref(gh_repos);json_decref(gh_releases);json_decref(gh_commits);
    gh_meta=gh_repos=gh_releases=gh_commits=NULL;
}
