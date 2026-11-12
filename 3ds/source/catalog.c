// Catalog, localization, settings and the installed-apps record.
#include "catalog.h"
#include "install.h"
#include "remote_image.h"
#include "rom_find.h"
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

App APPS[MAX_APPS];
int NAPPS, BUILTIN_APPS;
static int total_apps, base_categories;
// Matches pack-universal-art.py: 50 native 48 px icons fit a 512x256 atlas.
#define UNIVERSAL_ICONS_PER_SHEET 50
#define UNIVERSAL_ICON_SHEETS ((MAX_APPS + UNIVERSAL_ICONS_PER_SHEET - 1) / UNIVERSAL_ICONS_PER_SHEET)
static Sheet universal_icons[UNIVERSAL_ICON_SHEETS];
static bool universal_needed[UNIVERSAL_ICON_SHEETS];
Category CATS[8];
int NCATS;
Sheet ICONS;
// The same icons rendered at the other sizes the store uses (never resampled on the console).
static const int ICON_PX[4] = {20, 32, 40, 64};
static Sheet ICON_SZ[4];
json_t *CATALOG;
static json_t *INSTALLED, *BANNERS, *SHOTS, *STRINGS;
static const char *release_update_date(json_t *app);
static json_t *load_saved_object(const char *path);

Settings S = {.dark = false, .music = true, .sfx = true, .cia = true, .volume = 0.8f, .lang = 1};
const char *LANGS[8] = {"es", "en", "pt", "it", "fr", "de", "ru", "ja"};
const char *LANG_NAMES[8] = {"Español", "English", "Português", "Italiano", "Français", "Deutsch", "Русский", "日本語"};

// ---- strings ------------------------------------------------------------------------
void strings_load(void) {
    json_error_t e;
    STRINGS = json_load_file("romfs:/strings.json", 0, &e);
}

const char *T(const char *key) {
    const char *s = json_string_value(json_object_get(json_object_get(STRINGS, LANGS[S.lang >= 0 && S.lang < 8 ? S.lang : 1]), key));
    if (!s)
        s = json_string_value(json_object_get(json_object_get(STRINGS, "en"), key));
    if (!s)
        s = json_string_value(json_object_get(json_object_get(STRINGS, "es"), key));
    return s ? s : key;
}

const char *LJ(json_t *v) {
    if (!v)
        return "";
    if (json_is_string(v))
        return json_string_value(v);
    const char *s = json_string_value(json_object_get(v, LANGS[S.lang >= 0 && S.lang < 8 ? S.lang : 1]));
    if (!s)
        s = json_string_value(json_object_get(v, "en"));
    if (!s)
        s = json_string_value(json_object_get(v, "es"));
    return s ? s : "";
}

// ---- settings --------------------------------------------------------------------------
static int system_lang(void) {
    u8 l = CFG_LANGUAGE_EN;
    CFGU_GetSystemLanguage(&l);
    switch (l) {
    case CFG_LANGUAGE_ES:
        return 0;
    case CFG_LANGUAGE_PT:
        return 2;
    case CFG_LANGUAGE_IT:
        return 3;
    case CFG_LANGUAGE_FR:
        return 4;
    case CFG_LANGUAGE_DE:
        return 5;
    case CFG_LANGUAGE_RU:
        return 6;
    case CFG_LANGUAGE_JP:
        return 7;
    default:
        return 1;
    }
}

void settings_load(void) {
    mkdir(DATA_DIR, 0777);
    json_t *j = load_saved_object(DATA_DIR "/settings.json");
    S.lang = system_lang(); // first launch follows the console language
    if (!j)
        return;
    S.dark = json_is_true(json_object_get(j, "dark"));
    S.music = !json_is_false(json_object_get(j, "music"));
    S.sfx = !json_is_false(json_object_get(j, "sfx"));
    S.onboarded = json_is_true(json_object_get(j, "onboarded"));
    S.cia = !json_is_false(json_object_get(j, "cia"));
    S.experimental = json_is_true(json_object_get(j, "experimental"));
    S.universal = json_is_true(json_object_get(j, "universal"));
    json_t *v = json_object_get(j, "volume");
    if (json_is_number(v))
        S.volume = clampf(json_number_value(v), 0, 1);
    v = json_object_get(j, "lang");
    if (json_is_integer(v))
        S.lang = (int)clampf(json_integer_value(v), 0, 7);
    json_decref(j);
}

// All save scheduling and installed metadata access starts on the main thread.
// Workers own serialized strings only; the lock serializes disk transactions.
typedef struct {
    char path[96];
    char *data;
    u32 seq;
} SaveJob;
static LightLock save_lock;
static bool save_initialized;
static volatile u32 save_pending;
static u32 save_seq;
static struct { char path[96]; u32 seq; } save_latest[4];

static void save_init(void) {
    if (!save_initialized) {
        LightLock_Init(&save_lock);
        save_initialized = true;
    }
}

static u32 *latest_slot(const char *path) {
    for (int i = 0; i < 4; i++)
        if (!save_latest[i].path[0] || !strcmp(save_latest[i].path, path)) {
            snprintf(save_latest[i].path, sizeof save_latest[i].path, "%s", path);
            return &save_latest[i].seq;
        }
    return NULL;
}

static json_t *load_object(const char *path) {
    json_t *j = json_load_file(path, JSON_REJECT_DUPLICATES, NULL);
    if (!json_is_object(j)) { json_decref(j); return NULL; }
    return j;
}

// A .tmp is never committed: after interruption prefer the primary, then .bak.
// Recovery remains readable even when the SD cannot currently be written.
static json_t *load_saved_object(const char *path) {
    json_t *j = load_object(path);
    if (!j) {
        char bak[112];
        snprintf(bak, sizeof bak, "%s.bak", path);
        j = load_object(bak);
        if (!j) {
            snprintf(bak, sizeof bak, "%s.bad", path);
            j = load_object(bak);
        }
    }
    return j;
}

// Caller holds save_lock. FAT cannot replace an existing destination by rename.
// Keep the previous valid JSON at .bak until a later validated replacement exists.
static bool save_atomic(const char *path, const char *data) {
    if (!data) return false;
    json_t *expected = json_loads(data, JSON_REJECT_DUPLICATES, NULL);
    if (!json_is_object(expected)) { json_decref(expected); return false; }
    char tmp[112], bak[112];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    snprintf(bak, sizeof bak, "%s.bak", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) { json_decref(expected); return false; }
    size_t n = strlen(data);
    bool ok = fwrite(data, 1, n, f) == n;
    if (fflush(f) != 0) ok = false;
    // libctru's archive_fsync invokes FSFILE_Flush; close errors also fail the save.
    if (ok && fsync(fileno(f)) != 0) ok = false;
    if (fclose(f) != 0) ok = false;
    json_t *check = ok ? load_object(tmp) : NULL;
    ok = check && json_equal(expected, check);
    json_decref(check);
    json_decref(expected);
    if (!ok) { remove(tmp); return false; }

    json_t *previous = load_object(path);
    bool valid_previous = previous != NULL;
    json_decref(previous);
    char displaced[112];
    snprintf(displaced, sizeof displaced, "%s.%s", path, valid_previous ? "bak" : "bad");
    struct stat st;
    bool have_previous = stat(path, &st) == 0;
    if (!have_previous && errno != ENOENT) { remove(tmp); return false; }
    if (valid_previous) {
        // Removing a stale backup is safe only while the valid primary remains.
        if (remove(bak) != 0 && errno != ENOENT) { remove(tmp); return false; }
    } else if (have_previous) {
        // Even an unreadable primary may be valid (I/O/OOM). Preserve it, and
        // never overwrite the committed backup while recovering a corrupt primary.
        if (stat(displaced, &st) == 0 || errno != ENOENT) { remove(tmp); return false; }
    }
    if (have_previous && rename(path, displaced) != 0) { remove(tmp); return false; }
    if (rename(tmp, path) != 0) {
        if (have_previous) rename(displaced, path); // failed restore stays recoverable
        remove(tmp);
        return false;
    }
    // A new validated primary is now published; an unreadable displaced copy is obsolete.
    if (have_previous && !valid_previous) remove(displaced);
    return true;
}

static void save_main(void *arg) {
    SaveJob *j = arg;
    LightLock_Lock(&save_lock);
    u32 *latest = latest_slot(j->path);
    if (latest && j->seq == *latest) save_atomic(j->path, j->data);
    LightLock_Unlock(&save_lock);
    free(j->data);
    free(j);
    __atomic_sub_fetch(&save_pending, 1, __ATOMIC_SEQ_CST);
}

static void save_json(json_t *j, const char *path) {
    save_init();
    if (!j || strlen(path) >= sizeof ((SaveJob *)0)->path) return;
    SaveJob *job = malloc(sizeof *job);
    if (!job) return;
    job->data = json_dumps(j, JSON_COMPACT);
    if (!job->data) { free(job); return; }
    snprintf(job->path, sizeof job->path, "%s", path);
    LightLock_Lock(&save_lock);
    u32 *latest = latest_slot(path);
    if (latest) *latest = job->seq = ++save_seq;
    LightLock_Unlock(&save_lock);
    if (!latest) { free(job->data); free(job); return; }
    __atomic_add_fetch(&save_pending, 1, __ATOMIC_SEQ_CST);
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    if (!threadCreate(save_main, job, 16 * 1024, prio + 1, -2, true)) save_main(job);
}

static bool save_json_sync(json_t *j, const char *path) {
    save_init();
    char *data = json_dumps(j, JSON_COMPACT);
    if (!data) return false;
    LightLock_Lock(&save_lock);
    u32 *latest = latest_slot(path);
    // Supersede any older queued snapshot, including after a failed commit.
    if (latest) *latest = ++save_seq;
    bool ok = latest && save_atomic(path, data);
    LightLock_Unlock(&save_lock);
    free(data);
    return ok;
}

void saves_flush(void) {
#ifdef __3DS__
    // Settings and install records are small; at shutdown give them a second beyond the
    // deadline at most, so a stuck card cannot keep the application from closing.
    while (__atomic_load_n(&save_pending, __ATOMIC_SEQ_CST) && (!EXIT_DEADLINE_MS || osGetTime() < EXIT_DEADLINE_MS + 1000)) svcSleepThread(5000000);
#else
    while (__atomic_load_n(&save_pending, __ATOMIC_SEQ_CST)) svcSleepThread(5000000);
#endif
}

void settings_save(void) {
    json_t *j = json_pack("{s:b,s:b,s:b,s:b,s:b,s:b,s:f,s:i}", "dark", S.dark, "music", S.music, "sfx", S.sfx, "onboarded", S.onboarded,
                          "cia", S.cia, "experimental", S.experimental, "volume", (double)S.volume, "lang", S.lang);
    json_object_set_new(j, "universal", json_boolean(S.universal));
    save_json(j, DATA_DIR "/settings.json");
    json_decref(j);
}

// ---- catalog -----------------------------------------------------------------------------
static u32 hexcolor(const char *s) { return s && s[0] == '#' ? (u32)strtoul(s + 1, NULL, 16) : 0x556070; }

static void layer_from(Layer *l, json_t *m) {
    l->i = m ? (int)json_integer_value(json_object_get(m, "i")) : -1;
    if (!m)
        return;
    l->x = json_integer_value(json_object_get(m, "x"));
    l->y = json_integer_value(json_object_get(m, "y"));
    l->w = json_integer_value(json_object_get(m, "w"));
    l->h = json_integer_value(json_object_get(m, "h"));
}

bool catalog_load(void) {
    json_error_t e;
    CATALOG = json_load_file("romfs:/catalog.json", 0, &e);
    BANNERS = json_load_file("romfs:/gfx/banners.json", 0, &e);
    SHOTS = json_load_file("romfs:/gfx/shots.json", 0, &e);
    if (!CATALOG)
        return false;
    size_t i;
    json_t *v;
    NCATS = 0;
    json_array_foreach(json_object_get(CATALOG, "categories"), i, v) {
        if (NCATS >= 8)
            break;
        CATS[NCATS++] = (Category){json_string_value(json_object_get(v, "id")), json_string_value(json_object_get(v, "icon")),
                                   json_object_get(v, "label")};
    }
    NAPPS = 0;
    json_array_foreach(json_object_get(CATALOG, "apps"), i, v) {
        if (NAPPS >= MAX_APPS)
            break;
        App *a = &APPS[NAPPS];
        memset(a, 0, sizeof *a);
        a->j = v;
        a->updated = release_update_date(v);
        a->icon = NAPPS++;
        a->id = json_string_value(json_object_get(v, "id"));
        a->title = json_string_value(json_object_get(v, "title"));
        a->full = json_string_value(json_object_get(v, "fullTitle"));
        if (!a->full)
            a->full = a->title;
        a->author = json_string_value(json_object_get(v, "author"));
        a->category = json_string_value(json_object_get(v, "category"));
        a->status = json_string_value(json_object_get(v, "status"));
        a->repo = json_string_value(json_object_get(v, "repo"));
        a->folder = json_string_value(json_object_get(v, "folder"));
        json_t *origin = json_object_get(v, "origin");
        a->platform = json_string_value(json_object_get(origin, "platform"));
        a->year = json_integer_value(json_object_get(origin, "year"));
        a->progress = json_integer_value(json_object_get(v, "progress"));
        a->stars = json_integer_value(json_object_get(v, "stars"));
        a->has_stars = json_is_integer(json_object_get(v, "stars"));
        a->featured = json_is_true(json_object_get(v, "featured"));
        a->color = hexcolor(json_string_value(json_object_get(v, "color")));
        a->dev = a->status && !strcmp(a->status, "development");
        a->fx = json_string_value(json_object_get(json_object_get(v, "banner"), "fx"));
        a->pixel_icon = json_is_true(json_object_get(v, "pixelIcon"));
        json_t *compat = json_object_get(v, "compat");
        a->old_ok = !json_is_false(json_object_get(compat, "o3ds"));
        const char *old = json_string_value(json_object_get(compat, "o3ds"));
        a->old_experimental = old && !strcmp(old, "experimental");
        a->stereo = json_is_true(json_object_get(compat, "stereo")) || json_is_string(json_object_get(compat, "stereo"));
        size_t k;
        json_t *r;
        json_array_foreach(json_object_get(v, "requires"), k, r) {
            if (json_is_string(json_object_get(r, "id")) && !strcmp(json_string_value(json_object_get(r, "id")), "rom"))
                a->needs_rom = true;
            else
                a->needs_data = true;
        }
        json_t *bm = json_object_get(BANNERS, a->id);
        layer_from(&a->mid, json_object_get(bm, "mid"));
        layer_from(&a->logo, json_object_get(bm, "logo"));
        json_t *sm = json_object_get(SHOTS, a->id);
        a->shots = (int)clampf(json_integer_value(json_object_get(sm, "count")), 0, 12);
        json_array_foreach(json_object_get(sm, "bottom"), k, r) if (k < 12) a->shot_bottom[k] = json_is_true(r);
    }
    BUILTIN_APPS = NAPPS;
    base_categories = NCATS;
    json_t *u = json_load_file("romfs:/universal.json", 0, NULL);
    json_array_foreach(json_object_get(u, "apps"), i, v) {
        if (NAPPS >= MAX_APPS)
            break;
        App *a = &APPS[NAPPS];
        a->j = v;
        a->updated = release_update_date(v);
        a->universal = true;
        a->icon = NAPPS++;
        a->id = json_string_value(json_object_get(v, "id"));
        a->title = json_string_value(json_object_get(v, "title"));
        a->full = a->title;
        a->author = json_string_value(json_object_get(v, "author"));
        a->category = "universal";
        a->status = "released";
        a->old_ok = true;
        a->folder = "/3ds/";
        a->repo = json_string_value(json_object_get(v, "repo"));
        a->stars = json_integer_value(json_object_get(v, "stars"));
        a->has_stars = json_is_integer(json_object_get(v, "stars"));
        a->featured = json_is_true(json_object_get(v, "featured"));
        a->color = hexcolor(json_string_value(json_object_get(v, "color")));
        a->mid.i = a->logo.i = -1;
    }
    total_apps = NAPPS;
    if (base_categories < 8) CATS[base_categories] = (Category){"universal", "globe", json_string("Universal")};
    catalog_universal(S.universal);
    ICONS.crisp = true;
    sheet_load_now(&ICONS, "romfs:/gfx/icons.ftx");
    char path[48];
    for (int i = 0; i < 4; i++) {
        snprintf(path, sizeof path, "romfs:/gfx/icons-%d.ftx", ICON_PX[i]);
        ICON_SZ[i].crisp = true;
        sheet_load_now(&ICON_SZ[i], path);
    }
    return true;
}

void catalog_request_banners(void) {
    char path[96];
    for (int i = 0; i < BUILTIN_APPS; i++) {
        snprintf(path, sizeof path, "romfs:/gfx/banner/%s-bg.ftx", APPS[i].id);
        sheet_request(&APPS[i].bg, path);
        if (APPS[i].mid.i >= 0 || APPS[i].logo.i >= 0) {
            snprintf(path, sizeof path, "romfs:/gfx/banner/%s-fg.ftx", APPS[i].id);
            sheet_request(&APPS[i].fg, path);
        }
    }
}

void app_request_shots(App *a) {
    char path[96];
    for (int s = 0; s * 4 < a->shots && s < 3; s++) {
        snprintf(path, sizeof path, "romfs:/gfx/shots/%s-t%d.ftx", a->id, s);
        a->shots_t[s].crisp=true;sheet_request(&a->shots_t[s], path);
        int bottoms = 0;
        for (int k = 0; k < a->shots; k++)
            bottoms += a->shot_bottom[k];
        if (s * 4 < bottoms) {
            snprintf(path, sizeof path, "romfs:/gfx/shots/%s-b%d.ftx", a->id, s);
            a->shots_b[s].crisp=true;sheet_request(&a->shots_b[s], path);
        }
    }
}

void app_free_shots(App *a) {
    for (int s = 0; s < 3; s++) {
        sheet_free(&a->shots_t[s]);
        sheet_free(&a->shots_b[s]);
    }
}

C2D_Image app_icon(App *a) {
    if (a->universal) {
        int i = a->icon - BUILTIN_APPS;
        if(a->icon<BUILTIN_APPS||i/UNIVERSAL_ICONS_PER_SHEET>=UNIVERSAL_ICON_SHEETS)return sheet_img(&ICONS,0);
        Sheet *sheet = &universal_icons[i / UNIVERSAL_ICONS_PER_SHEET];
        return sheet_ok(sheet) ? sheet_img(sheet, i % UNIVERSAL_ICONS_PER_SHEET) : sheet_img(&ICONS, 0);
    }
    return sheet_img(&ICONS, a->icon);
}

C2D_Image app_icon_px(App *a, int size) {
    if (a->universal)
        return app_icon(a);
    for (int i = 0; i < 4; i++)
        if (ICON_PX[i] == size && sheet_ok(&ICON_SZ[i]))
            return sheet_img(&ICON_SZ[i], a->icon);
    return app_icon(a);
}

static bool in_cat(App *a, const char *cat) {
    if(a->retired)return false;
    if (!cat) return false;
    if (!strcmp(cat, "all"))
        return !a->universal;
    if (a->category && !strcmp(a->category, cat))
        return true;
    size_t i;
    json_t *t;
    json_array_foreach(json_object_get(a->j, "tags"), i, t) if (json_is_string(t) && !strcmp(json_string_value(t), cat)) return true;
    return false;
}

static const char *release_update_date(json_t *app) {
    const char *best = "", *key;
    json_t *release;
    json_object_foreach(json_object_get(app, "releases"), key, release) {
        const char *date = json_string_value(json_object_get(release, "date"));
        if (!date || strlen(date) < 10 || date[4] != '-' || date[7] != '-') continue;
        bool valid = true;
        for (int i = 0; i < 10; i++)
            if (i != 4 && i != 7 && !isdigit((unsigned char)date[i])) valid = false;
        if (valid && strncmp(date, best, 10) > 0) best = date;
    }
    return best;
}

static int latest_first(const void *lhs, const void *rhs) {
    const App *a = *(App *const *)lhs, *b = *(App *const *)rhs;
    int date = strncmp(b->updated ? b->updated : "", a->updated ? a->updated : "", 10);
    return date ? date : strcmp(a->id ? a->id : "", b->id ? b->id : "");
}

int catalog_filter(const char *cat, App **out, int max) {
    if (!out || max <= 0) return 0;
    App *ordered[MAX_APPS];
    int n = 0;
    for (int i = 0; i < NAPPS; i++)
        if (in_cat(&APPS[i], cat))
            ordered[n++] = &APPS[i];
    qsort(ordered, n, sizeof *ordered, latest_first);
    if (n > max) n = max;
    memcpy(out, ordered, n * sizeof *out);
    return n;
}

// Unicode 17.0.0 lowercase + canonical decomposition, removing U+0300..036F
// as normalizeText() does on the web. Hangul decomposition is algorithmic below.
// Generated from Unicode data, not locale-dependent libc byte case conversion.
static const struct { u32 code; const char *text; } search_fold[] = {
    {0xc0, "\x61"}, {0xc1, "\x61"}, {0xc2, "\x61"}, {0xc3, "\x61"},
    {0xc4, "\x61"}, {0xc5, "\x61"}, {0xc6, "\xc3\xa6"}, {0xc7, "\x63"},
    {0xc8, "\x65"}, {0xc9, "\x65"}, {0xca, "\x65"}, {0xcb, "\x65"},
    {0xcc, "\x69"}, {0xcd, "\x69"}, {0xce, "\x69"}, {0xcf, "\x69"},
    {0xd0, "\xc3\xb0"}, {0xd1, "\x6e"}, {0xd2, "\x6f"}, {0xd3, "\x6f"},
    {0xd4, "\x6f"}, {0xd5, "\x6f"}, {0xd6, "\x6f"}, {0xd8, "\xc3\xb8"},
    {0xd9, "\x75"}, {0xda, "\x75"}, {0xdb, "\x75"}, {0xdc, "\x75"},
    {0xdd, "\x79"}, {0xde, "\xc3\xbe"}, {0xe0, "\x61"}, {0xe1, "\x61"},
    {0xe2, "\x61"}, {0xe3, "\x61"}, {0xe4, "\x61"}, {0xe5, "\x61"},
    {0xe7, "\x63"}, {0xe8, "\x65"}, {0xe9, "\x65"}, {0xea, "\x65"},
    {0xeb, "\x65"}, {0xec, "\x69"}, {0xed, "\x69"}, {0xee, "\x69"},
    {0xef, "\x69"}, {0xf1, "\x6e"}, {0xf2, "\x6f"}, {0xf3, "\x6f"},
    {0xf4, "\x6f"}, {0xf5, "\x6f"}, {0xf6, "\x6f"}, {0xf9, "\x75"},
    {0xfa, "\x75"}, {0xfb, "\x75"}, {0xfc, "\x75"}, {0xfd, "\x79"},
    {0xff, "\x79"}, {0x100, "\x61"}, {0x101, "\x61"}, {0x102, "\x61"},
    {0x103, "\x61"}, {0x104, "\x61"}, {0x105, "\x61"}, {0x106, "\x63"},
    {0x107, "\x63"}, {0x108, "\x63"}, {0x109, "\x63"}, {0x10a, "\x63"},
    {0x10b, "\x63"}, {0x10c, "\x63"}, {0x10d, "\x63"}, {0x10e, "\x64"},
    {0x10f, "\x64"}, {0x110, "\xc4\x91"}, {0x112, "\x65"}, {0x113, "\x65"},
    {0x114, "\x65"}, {0x115, "\x65"}, {0x116, "\x65"}, {0x117, "\x65"},
    {0x118, "\x65"}, {0x119, "\x65"}, {0x11a, "\x65"}, {0x11b, "\x65"},
    {0x11c, "\x67"}, {0x11d, "\x67"}, {0x11e, "\x67"}, {0x11f, "\x67"},
    {0x120, "\x67"}, {0x121, "\x67"}, {0x122, "\x67"}, {0x123, "\x67"},
    {0x124, "\x68"}, {0x125, "\x68"}, {0x126, "\xc4\xa7"}, {0x128, "\x69"},
    {0x129, "\x69"}, {0x12a, "\x69"}, {0x12b, "\x69"}, {0x12c, "\x69"},
    {0x12d, "\x69"}, {0x12e, "\x69"}, {0x12f, "\x69"}, {0x130, "\x69"},
    {0x132, "\xc4\xb3"}, {0x134, "\x6a"}, {0x135, "\x6a"}, {0x136, "\x6b"},
    {0x137, "\x6b"}, {0x139, "\x6c"}, {0x13a, "\x6c"}, {0x13b, "\x6c"},
    {0x13c, "\x6c"}, {0x13d, "\x6c"}, {0x13e, "\x6c"}, {0x13f, "\xc5\x80"},
    {0x141, "\xc5\x82"}, {0x143, "\x6e"}, {0x144, "\x6e"}, {0x145, "\x6e"},
    {0x146, "\x6e"}, {0x147, "\x6e"}, {0x148, "\x6e"}, {0x14a, "\xc5\x8b"},
    {0x14c, "\x6f"}, {0x14d, "\x6f"}, {0x14e, "\x6f"}, {0x14f, "\x6f"},
    {0x150, "\x6f"}, {0x151, "\x6f"}, {0x152, "\xc5\x93"}, {0x154, "\x72"},
    {0x155, "\x72"}, {0x156, "\x72"}, {0x157, "\x72"}, {0x158, "\x72"},
    {0x159, "\x72"}, {0x15a, "\x73"}, {0x15b, "\x73"}, {0x15c, "\x73"},
    {0x15d, "\x73"}, {0x15e, "\x73"}, {0x15f, "\x73"}, {0x160, "\x73"},
    {0x161, "\x73"}, {0x162, "\x74"}, {0x163, "\x74"}, {0x164, "\x74"},
    {0x165, "\x74"}, {0x166, "\xc5\xa7"}, {0x168, "\x75"}, {0x169, "\x75"},
    {0x16a, "\x75"}, {0x16b, "\x75"}, {0x16c, "\x75"}, {0x16d, "\x75"},
    {0x16e, "\x75"}, {0x16f, "\x75"}, {0x170, "\x75"}, {0x171, "\x75"},
    {0x172, "\x75"}, {0x173, "\x75"}, {0x174, "\x77"}, {0x175, "\x77"},
    {0x176, "\x79"}, {0x177, "\x79"}, {0x178, "\x79"}, {0x179, "\x7a"},
    {0x17a, "\x7a"}, {0x17b, "\x7a"}, {0x17c, "\x7a"}, {0x17d, "\x7a"},
    {0x17e, "\x7a"}, {0x181, "\xc9\x93"}, {0x182, "\xc6\x83"}, {0x184, "\xc6\x85"},
    {0x186, "\xc9\x94"}, {0x187, "\xc6\x88"}, {0x189, "\xc9\x96"}, {0x18a, "\xc9\x97"},
    {0x18b, "\xc6\x8c"}, {0x18e, "\xc7\x9d"}, {0x18f, "\xc9\x99"}, {0x190, "\xc9\x9b"},
    {0x191, "\xc6\x92"}, {0x193, "\xc9\xa0"}, {0x194, "\xc9\xa3"}, {0x196, "\xc9\xa9"},
    {0x197, "\xc9\xa8"}, {0x198, "\xc6\x99"}, {0x19c, "\xc9\xaf"}, {0x19d, "\xc9\xb2"},
    {0x19f, "\xc9\xb5"}, {0x1a0, "\x6f"}, {0x1a1, "\x6f"}, {0x1a2, "\xc6\xa3"},
    {0x1a4, "\xc6\xa5"}, {0x1a6, "\xca\x80"}, {0x1a7, "\xc6\xa8"}, {0x1a9, "\xca\x83"},
    {0x1ac, "\xc6\xad"}, {0x1ae, "\xca\x88"}, {0x1af, "\x75"}, {0x1b0, "\x75"},
    {0x1b1, "\xca\x8a"}, {0x1b2, "\xca\x8b"}, {0x1b3, "\xc6\xb4"}, {0x1b5, "\xc6\xb6"},
    {0x1b7, "\xca\x92"}, {0x1b8, "\xc6\xb9"}, {0x1bc, "\xc6\xbd"}, {0x1c4, "\xc7\x86"},
    {0x1c5, "\xc7\x86"}, {0x1c7, "\xc7\x89"}, {0x1c8, "\xc7\x89"}, {0x1ca, "\xc7\x8c"},
    {0x1cb, "\xc7\x8c"}, {0x1cd, "\x61"}, {0x1ce, "\x61"}, {0x1cf, "\x69"},
    {0x1d0, "\x69"}, {0x1d1, "\x6f"}, {0x1d2, "\x6f"}, {0x1d3, "\x75"},
    {0x1d4, "\x75"}, {0x1d5, "\x75"}, {0x1d6, "\x75"}, {0x1d7, "\x75"},
    {0x1d8, "\x75"}, {0x1d9, "\x75"}, {0x1da, "\x75"}, {0x1db, "\x75"},
    {0x1dc, "\x75"}, {0x1de, "\x61"}, {0x1df, "\x61"}, {0x1e0, "\x61"},
    {0x1e1, "\x61"}, {0x1e2, "\xc3\xa6"}, {0x1e3, "\xc3\xa6"}, {0x1e4, "\xc7\xa5"},
    {0x1e6, "\x67"}, {0x1e7, "\x67"}, {0x1e8, "\x6b"}, {0x1e9, "\x6b"},
    {0x1ea, "\x6f"}, {0x1eb, "\x6f"}, {0x1ec, "\x6f"}, {0x1ed, "\x6f"},
    {0x1ee, "\xca\x92"}, {0x1ef, "\xca\x92"}, {0x1f0, "\x6a"}, {0x1f1, "\xc7\xb3"},
    {0x1f2, "\xc7\xb3"}, {0x1f4, "\x67"}, {0x1f5, "\x67"}, {0x1f6, "\xc6\x95"},
    {0x1f7, "\xc6\xbf"}, {0x1f8, "\x6e"}, {0x1f9, "\x6e"}, {0x1fa, "\x61"},
    {0x1fb, "\x61"}, {0x1fc, "\xc3\xa6"}, {0x1fd, "\xc3\xa6"}, {0x1fe, "\xc3\xb8"},
    {0x1ff, "\xc3\xb8"}, {0x200, "\x61"}, {0x201, "\x61"}, {0x202, "\x61"},
    {0x203, "\x61"}, {0x204, "\x65"}, {0x205, "\x65"}, {0x206, "\x65"},
    {0x207, "\x65"}, {0x208, "\x69"}, {0x209, "\x69"}, {0x20a, "\x69"},
    {0x20b, "\x69"}, {0x20c, "\x6f"}, {0x20d, "\x6f"}, {0x20e, "\x6f"},
    {0x20f, "\x6f"}, {0x210, "\x72"}, {0x211, "\x72"}, {0x212, "\x72"},
    {0x213, "\x72"}, {0x214, "\x75"}, {0x215, "\x75"}, {0x216, "\x75"},
    {0x217, "\x75"}, {0x218, "\x73"}, {0x219, "\x73"}, {0x21a, "\x74"},
    {0x21b, "\x74"}, {0x21c, "\xc8\x9d"}, {0x21e, "\x68"}, {0x21f, "\x68"},
    {0x220, "\xc6\x9e"}, {0x222, "\xc8\xa3"}, {0x224, "\xc8\xa5"}, {0x226, "\x61"},
    {0x227, "\x61"}, {0x228, "\x65"}, {0x229, "\x65"}, {0x22a, "\x6f"},
    {0x22b, "\x6f"}, {0x22c, "\x6f"}, {0x22d, "\x6f"}, {0x22e, "\x6f"},
    {0x22f, "\x6f"}, {0x230, "\x6f"}, {0x231, "\x6f"}, {0x232, "\x79"},
    {0x233, "\x79"}, {0x23a, "\xe2\xb1\xa5"}, {0x23b, "\xc8\xbc"}, {0x23d, "\xc6\x9a"},
    {0x23e, "\xe2\xb1\xa6"}, {0x241, "\xc9\x82"}, {0x243, "\xc6\x80"}, {0x244, "\xca\x89"},
    {0x245, "\xca\x8c"}, {0x246, "\xc9\x87"}, {0x248, "\xc9\x89"}, {0x24a, "\xc9\x8b"},
    {0x24c, "\xc9\x8d"}, {0x24e, "\xc9\x8f"}, {0x370, "\xcd\xb1"}, {0x372, "\xcd\xb3"},
    {0x374, "\xca\xb9"}, {0x376, "\xcd\xb7"}, {0x37e, "\x3b"}, {0x37f, "\xcf\xb3"},
    {0x385, "\xc2\xa8"}, {0x386, "\xce\xb1"}, {0x387, "\xc2\xb7"}, {0x388, "\xce\xb5"},
    {0x389, "\xce\xb7"}, {0x38a, "\xce\xb9"}, {0x38c, "\xce\xbf"}, {0x38e, "\xcf\x85"},
    {0x38f, "\xcf\x89"}, {0x390, "\xce\xb9"}, {0x391, "\xce\xb1"}, {0x392, "\xce\xb2"},
    {0x393, "\xce\xb3"}, {0x394, "\xce\xb4"}, {0x395, "\xce\xb5"}, {0x396, "\xce\xb6"},
    {0x397, "\xce\xb7"}, {0x398, "\xce\xb8"}, {0x399, "\xce\xb9"}, {0x39a, "\xce\xba"},
    {0x39b, "\xce\xbb"}, {0x39c, "\xce\xbc"}, {0x39d, "\xce\xbd"}, {0x39e, "\xce\xbe"},
    {0x39f, "\xce\xbf"}, {0x3a0, "\xcf\x80"}, {0x3a1, "\xcf\x81"}, {0x3a3, "\xcf\x83"},
    {0x3a4, "\xcf\x84"}, {0x3a5, "\xcf\x85"}, {0x3a6, "\xcf\x86"}, {0x3a7, "\xcf\x87"},
    {0x3a8, "\xcf\x88"}, {0x3a9, "\xcf\x89"}, {0x3aa, "\xce\xb9"}, {0x3ab, "\xcf\x85"},
    {0x3ac, "\xce\xb1"}, {0x3ad, "\xce\xb5"}, {0x3ae, "\xce\xb7"}, {0x3af, "\xce\xb9"},
    {0x3b0, "\xcf\x85"}, {0x3ca, "\xce\xb9"}, {0x3cb, "\xcf\x85"}, {0x3cc, "\xce\xbf"},
    {0x3cd, "\xcf\x85"}, {0x3ce, "\xcf\x89"}, {0x3cf, "\xcf\x97"}, {0x3d3, "\xcf\x92"},
    {0x3d4, "\xcf\x92"}, {0x3d8, "\xcf\x99"}, {0x3da, "\xcf\x9b"}, {0x3dc, "\xcf\x9d"},
    {0x3de, "\xcf\x9f"}, {0x3e0, "\xcf\xa1"}, {0x3e2, "\xcf\xa3"}, {0x3e4, "\xcf\xa5"},
    {0x3e6, "\xcf\xa7"}, {0x3e8, "\xcf\xa9"}, {0x3ea, "\xcf\xab"}, {0x3ec, "\xcf\xad"},
    {0x3ee, "\xcf\xaf"}, {0x3f4, "\xce\xb8"}, {0x3f7, "\xcf\xb8"}, {0x3f9, "\xcf\xb2"},
    {0x3fa, "\xcf\xbb"}, {0x3fd, "\xcd\xbb"}, {0x3fe, "\xcd\xbc"}, {0x3ff, "\xcd\xbd"},
    {0x400, "\xd0\xb5"}, {0x401, "\xd0\xb5"}, {0x402, "\xd1\x92"}, {0x403, "\xd0\xb3"},
    {0x404, "\xd1\x94"}, {0x405, "\xd1\x95"}, {0x406, "\xd1\x96"}, {0x407, "\xd1\x96"},
    {0x408, "\xd1\x98"}, {0x409, "\xd1\x99"}, {0x40a, "\xd1\x9a"}, {0x40b, "\xd1\x9b"},
    {0x40c, "\xd0\xba"}, {0x40d, "\xd0\xb8"}, {0x40e, "\xd1\x83"}, {0x40f, "\xd1\x9f"},
    {0x410, "\xd0\xb0"}, {0x411, "\xd0\xb1"}, {0x412, "\xd0\xb2"}, {0x413, "\xd0\xb3"},
    {0x414, "\xd0\xb4"}, {0x415, "\xd0\xb5"}, {0x416, "\xd0\xb6"}, {0x417, "\xd0\xb7"},
    {0x418, "\xd0\xb8"}, {0x419, "\xd0\xb8"}, {0x41a, "\xd0\xba"}, {0x41b, "\xd0\xbb"},
    {0x41c, "\xd0\xbc"}, {0x41d, "\xd0\xbd"}, {0x41e, "\xd0\xbe"}, {0x41f, "\xd0\xbf"},
    {0x420, "\xd1\x80"}, {0x421, "\xd1\x81"}, {0x422, "\xd1\x82"}, {0x423, "\xd1\x83"},
    {0x424, "\xd1\x84"}, {0x425, "\xd1\x85"}, {0x426, "\xd1\x86"}, {0x427, "\xd1\x87"},
    {0x428, "\xd1\x88"}, {0x429, "\xd1\x89"}, {0x42a, "\xd1\x8a"}, {0x42b, "\xd1\x8b"},
    {0x42c, "\xd1\x8c"}, {0x42d, "\xd1\x8d"}, {0x42e, "\xd1\x8e"}, {0x42f, "\xd1\x8f"},
    {0x439, "\xd0\xb8"}, {0x450, "\xd0\xb5"}, {0x451, "\xd0\xb5"}, {0x453, "\xd0\xb3"},
    {0x457, "\xd1\x96"}, {0x45c, "\xd0\xba"}, {0x45d, "\xd0\xb8"}, {0x45e, "\xd1\x83"},
    {0x460, "\xd1\xa1"}, {0x462, "\xd1\xa3"}, {0x464, "\xd1\xa5"}, {0x466, "\xd1\xa7"},
    {0x468, "\xd1\xa9"}, {0x46a, "\xd1\xab"}, {0x46c, "\xd1\xad"}, {0x46e, "\xd1\xaf"},
    {0x470, "\xd1\xb1"}, {0x472, "\xd1\xb3"}, {0x474, "\xd1\xb5"}, {0x476, "\xd1\xb5"},
    {0x477, "\xd1\xb5"}, {0x478, "\xd1\xb9"}, {0x47a, "\xd1\xbb"}, {0x47c, "\xd1\xbd"},
    {0x47e, "\xd1\xbf"}, {0x480, "\xd2\x81"}, {0x48a, "\xd2\x8b"}, {0x48c, "\xd2\x8d"},
    {0x48e, "\xd2\x8f"}, {0x490, "\xd2\x91"}, {0x492, "\xd2\x93"}, {0x494, "\xd2\x95"},
    {0x496, "\xd2\x97"}, {0x498, "\xd2\x99"}, {0x49a, "\xd2\x9b"}, {0x49c, "\xd2\x9d"},
    {0x49e, "\xd2\x9f"}, {0x4a0, "\xd2\xa1"}, {0x4a2, "\xd2\xa3"}, {0x4a4, "\xd2\xa5"},
    {0x4a6, "\xd2\xa7"}, {0x4a8, "\xd2\xa9"}, {0x4aa, "\xd2\xab"}, {0x4ac, "\xd2\xad"},
    {0x4ae, "\xd2\xaf"}, {0x4b0, "\xd2\xb1"}, {0x4b2, "\xd2\xb3"}, {0x4b4, "\xd2\xb5"},
    {0x4b6, "\xd2\xb7"}, {0x4b8, "\xd2\xb9"}, {0x4ba, "\xd2\xbb"}, {0x4bc, "\xd2\xbd"},
    {0x4be, "\xd2\xbf"}, {0x4c0, "\xd3\x8f"}, {0x4c1, "\xd0\xb6"}, {0x4c2, "\xd0\xb6"},
    {0x4c3, "\xd3\x84"}, {0x4c5, "\xd3\x86"}, {0x4c7, "\xd3\x88"}, {0x4c9, "\xd3\x8a"},
    {0x4cb, "\xd3\x8c"}, {0x4cd, "\xd3\x8e"}, {0x4d0, "\xd0\xb0"}, {0x4d1, "\xd0\xb0"},
    {0x4d2, "\xd0\xb0"}, {0x4d3, "\xd0\xb0"}, {0x4d4, "\xd3\x95"}, {0x4d6, "\xd0\xb5"},
    {0x4d7, "\xd0\xb5"}, {0x4d8, "\xd3\x99"}, {0x4da, "\xd3\x99"}, {0x4db, "\xd3\x99"},
    {0x4dc, "\xd0\xb6"}, {0x4dd, "\xd0\xb6"}, {0x4de, "\xd0\xb7"}, {0x4df, "\xd0\xb7"},
    {0x4e0, "\xd3\xa1"}, {0x4e2, "\xd0\xb8"}, {0x4e3, "\xd0\xb8"}, {0x4e4, "\xd0\xb8"},
    {0x4e5, "\xd0\xb8"}, {0x4e6, "\xd0\xbe"}, {0x4e7, "\xd0\xbe"}, {0x4e8, "\xd3\xa9"},
    {0x4ea, "\xd3\xa9"}, {0x4eb, "\xd3\xa9"}, {0x4ec, "\xd1\x8d"}, {0x4ed, "\xd1\x8d"},
    {0x4ee, "\xd1\x83"}, {0x4ef, "\xd1\x83"}, {0x4f0, "\xd1\x83"}, {0x4f1, "\xd1\x83"},
    {0x4f2, "\xd1\x83"}, {0x4f3, "\xd1\x83"}, {0x4f4, "\xd1\x87"}, {0x4f5, "\xd1\x87"},
    {0x4f6, "\xd3\xb7"}, {0x4f8, "\xd1\x8b"}, {0x4f9, "\xd1\x8b"}, {0x4fa, "\xd3\xbb"},
    {0x4fc, "\xd3\xbd"}, {0x4fe, "\xd3\xbf"}, {0x500, "\xd4\x81"}, {0x502, "\xd4\x83"},
    {0x504, "\xd4\x85"}, {0x506, "\xd4\x87"}, {0x508, "\xd4\x89"}, {0x50a, "\xd4\x8b"},
    {0x50c, "\xd4\x8d"}, {0x50e, "\xd4\x8f"}, {0x510, "\xd4\x91"}, {0x512, "\xd4\x93"},
    {0x514, "\xd4\x95"}, {0x516, "\xd4\x97"}, {0x518, "\xd4\x99"}, {0x51a, "\xd4\x9b"},
    {0x51c, "\xd4\x9d"}, {0x51e, "\xd4\x9f"}, {0x520, "\xd4\xa1"}, {0x522, "\xd4\xa3"},
    {0x524, "\xd4\xa5"}, {0x526, "\xd4\xa7"}, {0x528, "\xd4\xa9"}, {0x52a, "\xd4\xab"},
    {0x52c, "\xd4\xad"}, {0x52e, "\xd4\xaf"}, {0x531, "\xd5\xa1"}, {0x532, "\xd5\xa2"},
    {0x533, "\xd5\xa3"}, {0x534, "\xd5\xa4"}, {0x535, "\xd5\xa5"}, {0x536, "\xd5\xa6"},
    {0x537, "\xd5\xa7"}, {0x538, "\xd5\xa8"}, {0x539, "\xd5\xa9"}, {0x53a, "\xd5\xaa"},
    {0x53b, "\xd5\xab"}, {0x53c, "\xd5\xac"}, {0x53d, "\xd5\xad"}, {0x53e, "\xd5\xae"},
    {0x53f, "\xd5\xaf"}, {0x540, "\xd5\xb0"}, {0x541, "\xd5\xb1"}, {0x542, "\xd5\xb2"},
    {0x543, "\xd5\xb3"}, {0x544, "\xd5\xb4"}, {0x545, "\xd5\xb5"}, {0x546, "\xd5\xb6"},
    {0x547, "\xd5\xb7"}, {0x548, "\xd5\xb8"}, {0x549, "\xd5\xb9"}, {0x54a, "\xd5\xba"},
    {0x54b, "\xd5\xbb"}, {0x54c, "\xd5\xbc"}, {0x54d, "\xd5\xbd"}, {0x54e, "\xd5\xbe"},
    {0x54f, "\xd5\xbf"}, {0x550, "\xd6\x80"}, {0x551, "\xd6\x81"}, {0x552, "\xd6\x82"},
    {0x553, "\xd6\x83"}, {0x554, "\xd6\x84"}, {0x555, "\xd6\x85"}, {0x556, "\xd6\x86"},
    {0x622, "\xd8\xa7\xd9\x93"}, {0x623, "\xd8\xa7\xd9\x94"}, {0x624, "\xd9\x88\xd9\x94"}, {0x625, "\xd8\xa7\xd9\x95"},
    {0x626, "\xd9\x8a\xd9\x94"}, {0x6c0, "\xdb\x95\xd9\x94"}, {0x6c2, "\xdb\x81\xd9\x94"}, {0x6d3, "\xdb\x92\xd9\x94"},
    {0x929, "\xe0\xa4\xa8\xe0\xa4\xbc"}, {0x931, "\xe0\xa4\xb0\xe0\xa4\xbc"}, {0x934, "\xe0\xa4\xb3\xe0\xa4\xbc"}, {0x958, "\xe0\xa4\x95\xe0\xa4\xbc"},
    {0x959, "\xe0\xa4\x96\xe0\xa4\xbc"}, {0x95a, "\xe0\xa4\x97\xe0\xa4\xbc"}, {0x95b, "\xe0\xa4\x9c\xe0\xa4\xbc"}, {0x95c, "\xe0\xa4\xa1\xe0\xa4\xbc"},
    {0x95d, "\xe0\xa4\xa2\xe0\xa4\xbc"}, {0x95e, "\xe0\xa4\xab\xe0\xa4\xbc"}, {0x95f, "\xe0\xa4\xaf\xe0\xa4\xbc"}, {0x9cb, "\xe0\xa7\x87\xe0\xa6\xbe"},
    {0x9cc, "\xe0\xa7\x87\xe0\xa7\x97"}, {0x9dc, "\xe0\xa6\xa1\xe0\xa6\xbc"}, {0x9dd, "\xe0\xa6\xa2\xe0\xa6\xbc"}, {0x9df, "\xe0\xa6\xaf\xe0\xa6\xbc"},
    {0xa33, "\xe0\xa8\xb2\xe0\xa8\xbc"}, {0xa36, "\xe0\xa8\xb8\xe0\xa8\xbc"}, {0xa59, "\xe0\xa8\x96\xe0\xa8\xbc"}, {0xa5a, "\xe0\xa8\x97\xe0\xa8\xbc"},
    {0xa5b, "\xe0\xa8\x9c\xe0\xa8\xbc"}, {0xa5e, "\xe0\xa8\xab\xe0\xa8\xbc"}, {0xb48, "\xe0\xad\x87\xe0\xad\x96"}, {0xb4b, "\xe0\xad\x87\xe0\xac\xbe"},
    {0xb4c, "\xe0\xad\x87\xe0\xad\x97"}, {0xb5c, "\xe0\xac\xa1\xe0\xac\xbc"}, {0xb5d, "\xe0\xac\xa2\xe0\xac\xbc"}, {0xb94, "\xe0\xae\x92\xe0\xaf\x97"},
    {0xbca, "\xe0\xaf\x86\xe0\xae\xbe"}, {0xbcb, "\xe0\xaf\x87\xe0\xae\xbe"}, {0xbcc, "\xe0\xaf\x86\xe0\xaf\x97"}, {0xc48, "\xe0\xb1\x86\xe0\xb1\x96"},
    {0xcc0, "\xe0\xb2\xbf\xe0\xb3\x95"}, {0xcc7, "\xe0\xb3\x86\xe0\xb3\x95"}, {0xcc8, "\xe0\xb3\x86\xe0\xb3\x96"}, {0xcca, "\xe0\xb3\x86\xe0\xb3\x82"},
    {0xccb, "\xe0\xb3\x86\xe0\xb3\x82\xe0\xb3\x95"}, {0xd4a, "\xe0\xb5\x86\xe0\xb4\xbe"}, {0xd4b, "\xe0\xb5\x87\xe0\xb4\xbe"}, {0xd4c, "\xe0\xb5\x86\xe0\xb5\x97"},
    {0xdda, "\xe0\xb7\x99\xe0\xb7\x8a"}, {0xddc, "\xe0\xb7\x99\xe0\xb7\x8f"}, {0xddd, "\xe0\xb7\x99\xe0\xb7\x8f\xe0\xb7\x8a"}, {0xdde, "\xe0\xb7\x99\xe0\xb7\x9f"},
    {0xf43, "\xe0\xbd\x82\xe0\xbe\xb7"}, {0xf4d, "\xe0\xbd\x8c\xe0\xbe\xb7"}, {0xf52, "\xe0\xbd\x91\xe0\xbe\xb7"}, {0xf57, "\xe0\xbd\x96\xe0\xbe\xb7"},
    {0xf5c, "\xe0\xbd\x9b\xe0\xbe\xb7"}, {0xf69, "\xe0\xbd\x80\xe0\xbe\xb5"}, {0xf73, "\xe0\xbd\xb1\xe0\xbd\xb2"}, {0xf75, "\xe0\xbd\xb1\xe0\xbd\xb4"},
    {0xf76, "\xe0\xbe\xb2\xe0\xbe\x80"}, {0xf78, "\xe0\xbe\xb3\xe0\xbe\x80"}, {0xf81, "\xe0\xbd\xb1\xe0\xbe\x80"}, {0xf93, "\xe0\xbe\x92\xe0\xbe\xb7"},
    {0xf9d, "\xe0\xbe\x9c\xe0\xbe\xb7"}, {0xfa2, "\xe0\xbe\xa1\xe0\xbe\xb7"}, {0xfa7, "\xe0\xbe\xa6\xe0\xbe\xb7"}, {0xfac, "\xe0\xbe\xab\xe0\xbe\xb7"},
    {0xfb9, "\xe0\xbe\x90\xe0\xbe\xb5"}, {0x1026, "\xe1\x80\xa5\xe1\x80\xae"}, {0x10a0, "\xe2\xb4\x80"}, {0x10a1, "\xe2\xb4\x81"},
    {0x10a2, "\xe2\xb4\x82"}, {0x10a3, "\xe2\xb4\x83"}, {0x10a4, "\xe2\xb4\x84"}, {0x10a5, "\xe2\xb4\x85"},
    {0x10a6, "\xe2\xb4\x86"}, {0x10a7, "\xe2\xb4\x87"}, {0x10a8, "\xe2\xb4\x88"}, {0x10a9, "\xe2\xb4\x89"},
    {0x10aa, "\xe2\xb4\x8a"}, {0x10ab, "\xe2\xb4\x8b"}, {0x10ac, "\xe2\xb4\x8c"}, {0x10ad, "\xe2\xb4\x8d"},
    {0x10ae, "\xe2\xb4\x8e"}, {0x10af, "\xe2\xb4\x8f"}, {0x10b0, "\xe2\xb4\x90"}, {0x10b1, "\xe2\xb4\x91"},
    {0x10b2, "\xe2\xb4\x92"}, {0x10b3, "\xe2\xb4\x93"}, {0x10b4, "\xe2\xb4\x94"}, {0x10b5, "\xe2\xb4\x95"},
    {0x10b6, "\xe2\xb4\x96"}, {0x10b7, "\xe2\xb4\x97"}, {0x10b8, "\xe2\xb4\x98"}, {0x10b9, "\xe2\xb4\x99"},
    {0x10ba, "\xe2\xb4\x9a"}, {0x10bb, "\xe2\xb4\x9b"}, {0x10bc, "\xe2\xb4\x9c"}, {0x10bd, "\xe2\xb4\x9d"},
    {0x10be, "\xe2\xb4\x9e"}, {0x10bf, "\xe2\xb4\x9f"}, {0x10c0, "\xe2\xb4\xa0"}, {0x10c1, "\xe2\xb4\xa1"},
    {0x10c2, "\xe2\xb4\xa2"}, {0x10c3, "\xe2\xb4\xa3"}, {0x10c4, "\xe2\xb4\xa4"}, {0x10c5, "\xe2\xb4\xa5"},
    {0x10c7, "\xe2\xb4\xa7"}, {0x10cd, "\xe2\xb4\xad"}, {0x13a0, "\xea\xad\xb0"}, {0x13a1, "\xea\xad\xb1"},
    {0x13a2, "\xea\xad\xb2"}, {0x13a3, "\xea\xad\xb3"}, {0x13a4, "\xea\xad\xb4"}, {0x13a5, "\xea\xad\xb5"},
    {0x13a6, "\xea\xad\xb6"}, {0x13a7, "\xea\xad\xb7"}, {0x13a8, "\xea\xad\xb8"}, {0x13a9, "\xea\xad\xb9"},
    {0x13aa, "\xea\xad\xba"}, {0x13ab, "\xea\xad\xbb"}, {0x13ac, "\xea\xad\xbc"}, {0x13ad, "\xea\xad\xbd"},
    {0x13ae, "\xea\xad\xbe"}, {0x13af, "\xea\xad\xbf"}, {0x13b0, "\xea\xae\x80"}, {0x13b1, "\xea\xae\x81"},
    {0x13b2, "\xea\xae\x82"}, {0x13b3, "\xea\xae\x83"}, {0x13b4, "\xea\xae\x84"}, {0x13b5, "\xea\xae\x85"},
    {0x13b6, "\xea\xae\x86"}, {0x13b7, "\xea\xae\x87"}, {0x13b8, "\xea\xae\x88"}, {0x13b9, "\xea\xae\x89"},
    {0x13ba, "\xea\xae\x8a"}, {0x13bb, "\xea\xae\x8b"}, {0x13bc, "\xea\xae\x8c"}, {0x13bd, "\xea\xae\x8d"},
    {0x13be, "\xea\xae\x8e"}, {0x13bf, "\xea\xae\x8f"}, {0x13c0, "\xea\xae\x90"}, {0x13c1, "\xea\xae\x91"},
    {0x13c2, "\xea\xae\x92"}, {0x13c3, "\xea\xae\x93"}, {0x13c4, "\xea\xae\x94"}, {0x13c5, "\xea\xae\x95"},
    {0x13c6, "\xea\xae\x96"}, {0x13c7, "\xea\xae\x97"}, {0x13c8, "\xea\xae\x98"}, {0x13c9, "\xea\xae\x99"},
    {0x13ca, "\xea\xae\x9a"}, {0x13cb, "\xea\xae\x9b"}, {0x13cc, "\xea\xae\x9c"}, {0x13cd, "\xea\xae\x9d"},
    {0x13ce, "\xea\xae\x9e"}, {0x13cf, "\xea\xae\x9f"}, {0x13d0, "\xea\xae\xa0"}, {0x13d1, "\xea\xae\xa1"},
    {0x13d2, "\xea\xae\xa2"}, {0x13d3, "\xea\xae\xa3"}, {0x13d4, "\xea\xae\xa4"}, {0x13d5, "\xea\xae\xa5"},
    {0x13d6, "\xea\xae\xa6"}, {0x13d7, "\xea\xae\xa7"}, {0x13d8, "\xea\xae\xa8"}, {0x13d9, "\xea\xae\xa9"},
    {0x13da, "\xea\xae\xaa"}, {0x13db, "\xea\xae\xab"}, {0x13dc, "\xea\xae\xac"}, {0x13dd, "\xea\xae\xad"},
    {0x13de, "\xea\xae\xae"}, {0x13df, "\xea\xae\xaf"}, {0x13e0, "\xea\xae\xb0"}, {0x13e1, "\xea\xae\xb1"},
    {0x13e2, "\xea\xae\xb2"}, {0x13e3, "\xea\xae\xb3"}, {0x13e4, "\xea\xae\xb4"}, {0x13e5, "\xea\xae\xb5"},
    {0x13e6, "\xea\xae\xb6"}, {0x13e7, "\xea\xae\xb7"}, {0x13e8, "\xea\xae\xb8"}, {0x13e9, "\xea\xae\xb9"},
    {0x13ea, "\xea\xae\xba"}, {0x13eb, "\xea\xae\xbb"}, {0x13ec, "\xea\xae\xbc"}, {0x13ed, "\xea\xae\xbd"},
    {0x13ee, "\xea\xae\xbe"}, {0x13ef, "\xea\xae\xbf"}, {0x13f0, "\xe1\x8f\xb8"}, {0x13f1, "\xe1\x8f\xb9"},
    {0x13f2, "\xe1\x8f\xba"}, {0x13f3, "\xe1\x8f\xbb"}, {0x13f4, "\xe1\x8f\xbc"}, {0x13f5, "\xe1\x8f\xbd"},
    {0x1b06, "\xe1\xac\x85\xe1\xac\xb5"}, {0x1b08, "\xe1\xac\x87\xe1\xac\xb5"}, {0x1b0a, "\xe1\xac\x89\xe1\xac\xb5"}, {0x1b0c, "\xe1\xac\x8b\xe1\xac\xb5"},
    {0x1b0e, "\xe1\xac\x8d\xe1\xac\xb5"}, {0x1b12, "\xe1\xac\x91\xe1\xac\xb5"}, {0x1b3b, "\xe1\xac\xba\xe1\xac\xb5"}, {0x1b3d, "\xe1\xac\xbc\xe1\xac\xb5"},
    {0x1b40, "\xe1\xac\xbe\xe1\xac\xb5"}, {0x1b41, "\xe1\xac\xbf\xe1\xac\xb5"}, {0x1b43, "\xe1\xad\x82\xe1\xac\xb5"}, {0x1c89, "\xe1\xb2\x8a"},
    {0x1c90, "\xe1\x83\x90"}, {0x1c91, "\xe1\x83\x91"}, {0x1c92, "\xe1\x83\x92"}, {0x1c93, "\xe1\x83\x93"},
    {0x1c94, "\xe1\x83\x94"}, {0x1c95, "\xe1\x83\x95"}, {0x1c96, "\xe1\x83\x96"}, {0x1c97, "\xe1\x83\x97"},
    {0x1c98, "\xe1\x83\x98"}, {0x1c99, "\xe1\x83\x99"}, {0x1c9a, "\xe1\x83\x9a"}, {0x1c9b, "\xe1\x83\x9b"},
    {0x1c9c, "\xe1\x83\x9c"}, {0x1c9d, "\xe1\x83\x9d"}, {0x1c9e, "\xe1\x83\x9e"}, {0x1c9f, "\xe1\x83\x9f"},
    {0x1ca0, "\xe1\x83\xa0"}, {0x1ca1, "\xe1\x83\xa1"}, {0x1ca2, "\xe1\x83\xa2"}, {0x1ca3, "\xe1\x83\xa3"},
    {0x1ca4, "\xe1\x83\xa4"}, {0x1ca5, "\xe1\x83\xa5"}, {0x1ca6, "\xe1\x83\xa6"}, {0x1ca7, "\xe1\x83\xa7"},
    {0x1ca8, "\xe1\x83\xa8"}, {0x1ca9, "\xe1\x83\xa9"}, {0x1caa, "\xe1\x83\xaa"}, {0x1cab, "\xe1\x83\xab"},
    {0x1cac, "\xe1\x83\xac"}, {0x1cad, "\xe1\x83\xad"}, {0x1cae, "\xe1\x83\xae"}, {0x1caf, "\xe1\x83\xaf"},
    {0x1cb0, "\xe1\x83\xb0"}, {0x1cb1, "\xe1\x83\xb1"}, {0x1cb2, "\xe1\x83\xb2"}, {0x1cb3, "\xe1\x83\xb3"},
    {0x1cb4, "\xe1\x83\xb4"}, {0x1cb5, "\xe1\x83\xb5"}, {0x1cb6, "\xe1\x83\xb6"}, {0x1cb7, "\xe1\x83\xb7"},
    {0x1cb8, "\xe1\x83\xb8"}, {0x1cb9, "\xe1\x83\xb9"}, {0x1cba, "\xe1\x83\xba"}, {0x1cbd, "\xe1\x83\xbd"},
    {0x1cbe, "\xe1\x83\xbe"}, {0x1cbf, "\xe1\x83\xbf"}, {0x1e00, "\x61"}, {0x1e01, "\x61"},
    {0x1e02, "\x62"}, {0x1e03, "\x62"}, {0x1e04, "\x62"}, {0x1e05, "\x62"},
    {0x1e06, "\x62"}, {0x1e07, "\x62"}, {0x1e08, "\x63"}, {0x1e09, "\x63"},
    {0x1e0a, "\x64"}, {0x1e0b, "\x64"}, {0x1e0c, "\x64"}, {0x1e0d, "\x64"},
    {0x1e0e, "\x64"}, {0x1e0f, "\x64"}, {0x1e10, "\x64"}, {0x1e11, "\x64"},
    {0x1e12, "\x64"}, {0x1e13, "\x64"}, {0x1e14, "\x65"}, {0x1e15, "\x65"},
    {0x1e16, "\x65"}, {0x1e17, "\x65"}, {0x1e18, "\x65"}, {0x1e19, "\x65"},
    {0x1e1a, "\x65"}, {0x1e1b, "\x65"}, {0x1e1c, "\x65"}, {0x1e1d, "\x65"},
    {0x1e1e, "\x66"}, {0x1e1f, "\x66"}, {0x1e20, "\x67"}, {0x1e21, "\x67"},
    {0x1e22, "\x68"}, {0x1e23, "\x68"}, {0x1e24, "\x68"}, {0x1e25, "\x68"},
    {0x1e26, "\x68"}, {0x1e27, "\x68"}, {0x1e28, "\x68"}, {0x1e29, "\x68"},
    {0x1e2a, "\x68"}, {0x1e2b, "\x68"}, {0x1e2c, "\x69"}, {0x1e2d, "\x69"},
    {0x1e2e, "\x69"}, {0x1e2f, "\x69"}, {0x1e30, "\x6b"}, {0x1e31, "\x6b"},
    {0x1e32, "\x6b"}, {0x1e33, "\x6b"}, {0x1e34, "\x6b"}, {0x1e35, "\x6b"},
    {0x1e36, "\x6c"}, {0x1e37, "\x6c"}, {0x1e38, "\x6c"}, {0x1e39, "\x6c"},
    {0x1e3a, "\x6c"}, {0x1e3b, "\x6c"}, {0x1e3c, "\x6c"}, {0x1e3d, "\x6c"},
    {0x1e3e, "\x6d"}, {0x1e3f, "\x6d"}, {0x1e40, "\x6d"}, {0x1e41, "\x6d"},
    {0x1e42, "\x6d"}, {0x1e43, "\x6d"}, {0x1e44, "\x6e"}, {0x1e45, "\x6e"},
    {0x1e46, "\x6e"}, {0x1e47, "\x6e"}, {0x1e48, "\x6e"}, {0x1e49, "\x6e"},
    {0x1e4a, "\x6e"}, {0x1e4b, "\x6e"}, {0x1e4c, "\x6f"}, {0x1e4d, "\x6f"},
    {0x1e4e, "\x6f"}, {0x1e4f, "\x6f"}, {0x1e50, "\x6f"}, {0x1e51, "\x6f"},
    {0x1e52, "\x6f"}, {0x1e53, "\x6f"}, {0x1e54, "\x70"}, {0x1e55, "\x70"},
    {0x1e56, "\x70"}, {0x1e57, "\x70"}, {0x1e58, "\x72"}, {0x1e59, "\x72"},
    {0x1e5a, "\x72"}, {0x1e5b, "\x72"}, {0x1e5c, "\x72"}, {0x1e5d, "\x72"},
    {0x1e5e, "\x72"}, {0x1e5f, "\x72"}, {0x1e60, "\x73"}, {0x1e61, "\x73"},
    {0x1e62, "\x73"}, {0x1e63, "\x73"}, {0x1e64, "\x73"}, {0x1e65, "\x73"},
    {0x1e66, "\x73"}, {0x1e67, "\x73"}, {0x1e68, "\x73"}, {0x1e69, "\x73"},
    {0x1e6a, "\x74"}, {0x1e6b, "\x74"}, {0x1e6c, "\x74"}, {0x1e6d, "\x74"},
    {0x1e6e, "\x74"}, {0x1e6f, "\x74"}, {0x1e70, "\x74"}, {0x1e71, "\x74"},
    {0x1e72, "\x75"}, {0x1e73, "\x75"}, {0x1e74, "\x75"}, {0x1e75, "\x75"},
    {0x1e76, "\x75"}, {0x1e77, "\x75"}, {0x1e78, "\x75"}, {0x1e79, "\x75"},
    {0x1e7a, "\x75"}, {0x1e7b, "\x75"}, {0x1e7c, "\x76"}, {0x1e7d, "\x76"},
    {0x1e7e, "\x76"}, {0x1e7f, "\x76"}, {0x1e80, "\x77"}, {0x1e81, "\x77"},
    {0x1e82, "\x77"}, {0x1e83, "\x77"}, {0x1e84, "\x77"}, {0x1e85, "\x77"},
    {0x1e86, "\x77"}, {0x1e87, "\x77"}, {0x1e88, "\x77"}, {0x1e89, "\x77"},
    {0x1e8a, "\x78"}, {0x1e8b, "\x78"}, {0x1e8c, "\x78"}, {0x1e8d, "\x78"},
    {0x1e8e, "\x79"}, {0x1e8f, "\x79"}, {0x1e90, "\x7a"}, {0x1e91, "\x7a"},
    {0x1e92, "\x7a"}, {0x1e93, "\x7a"}, {0x1e94, "\x7a"}, {0x1e95, "\x7a"},
    {0x1e96, "\x68"}, {0x1e97, "\x74"}, {0x1e98, "\x77"}, {0x1e99, "\x79"},
    {0x1e9b, "\xc5\xbf"}, {0x1e9e, "\xc3\x9f"}, {0x1ea0, "\x61"}, {0x1ea1, "\x61"},
    {0x1ea2, "\x61"}, {0x1ea3, "\x61"}, {0x1ea4, "\x61"}, {0x1ea5, "\x61"},
    {0x1ea6, "\x61"}, {0x1ea7, "\x61"}, {0x1ea8, "\x61"}, {0x1ea9, "\x61"},
    {0x1eaa, "\x61"}, {0x1eab, "\x61"}, {0x1eac, "\x61"}, {0x1ead, "\x61"},
    {0x1eae, "\x61"}, {0x1eaf, "\x61"}, {0x1eb0, "\x61"}, {0x1eb1, "\x61"},
    {0x1eb2, "\x61"}, {0x1eb3, "\x61"}, {0x1eb4, "\x61"}, {0x1eb5, "\x61"},
    {0x1eb6, "\x61"}, {0x1eb7, "\x61"}, {0x1eb8, "\x65"}, {0x1eb9, "\x65"},
    {0x1eba, "\x65"}, {0x1ebb, "\x65"}, {0x1ebc, "\x65"}, {0x1ebd, "\x65"},
    {0x1ebe, "\x65"}, {0x1ebf, "\x65"}, {0x1ec0, "\x65"}, {0x1ec1, "\x65"},
    {0x1ec2, "\x65"}, {0x1ec3, "\x65"}, {0x1ec4, "\x65"}, {0x1ec5, "\x65"},
    {0x1ec6, "\x65"}, {0x1ec7, "\x65"}, {0x1ec8, "\x69"}, {0x1ec9, "\x69"},
    {0x1eca, "\x69"}, {0x1ecb, "\x69"}, {0x1ecc, "\x6f"}, {0x1ecd, "\x6f"},
    {0x1ece, "\x6f"}, {0x1ecf, "\x6f"}, {0x1ed0, "\x6f"}, {0x1ed1, "\x6f"},
    {0x1ed2, "\x6f"}, {0x1ed3, "\x6f"}, {0x1ed4, "\x6f"}, {0x1ed5, "\x6f"},
    {0x1ed6, "\x6f"}, {0x1ed7, "\x6f"}, {0x1ed8, "\x6f"}, {0x1ed9, "\x6f"},
    {0x1eda, "\x6f"}, {0x1edb, "\x6f"}, {0x1edc, "\x6f"}, {0x1edd, "\x6f"},
    {0x1ede, "\x6f"}, {0x1edf, "\x6f"}, {0x1ee0, "\x6f"}, {0x1ee1, "\x6f"},
    {0x1ee2, "\x6f"}, {0x1ee3, "\x6f"}, {0x1ee4, "\x75"}, {0x1ee5, "\x75"},
    {0x1ee6, "\x75"}, {0x1ee7, "\x75"}, {0x1ee8, "\x75"}, {0x1ee9, "\x75"},
    {0x1eea, "\x75"}, {0x1eeb, "\x75"}, {0x1eec, "\x75"}, {0x1eed, "\x75"},
    {0x1eee, "\x75"}, {0x1eef, "\x75"}, {0x1ef0, "\x75"}, {0x1ef1, "\x75"},
    {0x1ef2, "\x79"}, {0x1ef3, "\x79"}, {0x1ef4, "\x79"}, {0x1ef5, "\x79"},
    {0x1ef6, "\x79"}, {0x1ef7, "\x79"}, {0x1ef8, "\x79"}, {0x1ef9, "\x79"},
    {0x1efa, "\xe1\xbb\xbb"}, {0x1efc, "\xe1\xbb\xbd"}, {0x1efe, "\xe1\xbb\xbf"}, {0x1f00, "\xce\xb1"},
    {0x1f01, "\xce\xb1"}, {0x1f02, "\xce\xb1"}, {0x1f03, "\xce\xb1"}, {0x1f04, "\xce\xb1"},
    {0x1f05, "\xce\xb1"}, {0x1f06, "\xce\xb1"}, {0x1f07, "\xce\xb1"}, {0x1f08, "\xce\xb1"},
    {0x1f09, "\xce\xb1"}, {0x1f0a, "\xce\xb1"}, {0x1f0b, "\xce\xb1"}, {0x1f0c, "\xce\xb1"},
    {0x1f0d, "\xce\xb1"}, {0x1f0e, "\xce\xb1"}, {0x1f0f, "\xce\xb1"}, {0x1f10, "\xce\xb5"},
    {0x1f11, "\xce\xb5"}, {0x1f12, "\xce\xb5"}, {0x1f13, "\xce\xb5"}, {0x1f14, "\xce\xb5"},
    {0x1f15, "\xce\xb5"}, {0x1f18, "\xce\xb5"}, {0x1f19, "\xce\xb5"}, {0x1f1a, "\xce\xb5"},
    {0x1f1b, "\xce\xb5"}, {0x1f1c, "\xce\xb5"}, {0x1f1d, "\xce\xb5"}, {0x1f20, "\xce\xb7"},
    {0x1f21, "\xce\xb7"}, {0x1f22, "\xce\xb7"}, {0x1f23, "\xce\xb7"}, {0x1f24, "\xce\xb7"},
    {0x1f25, "\xce\xb7"}, {0x1f26, "\xce\xb7"}, {0x1f27, "\xce\xb7"}, {0x1f28, "\xce\xb7"},
    {0x1f29, "\xce\xb7"}, {0x1f2a, "\xce\xb7"}, {0x1f2b, "\xce\xb7"}, {0x1f2c, "\xce\xb7"},
    {0x1f2d, "\xce\xb7"}, {0x1f2e, "\xce\xb7"}, {0x1f2f, "\xce\xb7"}, {0x1f30, "\xce\xb9"},
    {0x1f31, "\xce\xb9"}, {0x1f32, "\xce\xb9"}, {0x1f33, "\xce\xb9"}, {0x1f34, "\xce\xb9"},
    {0x1f35, "\xce\xb9"}, {0x1f36, "\xce\xb9"}, {0x1f37, "\xce\xb9"}, {0x1f38, "\xce\xb9"},
    {0x1f39, "\xce\xb9"}, {0x1f3a, "\xce\xb9"}, {0x1f3b, "\xce\xb9"}, {0x1f3c, "\xce\xb9"},
    {0x1f3d, "\xce\xb9"}, {0x1f3e, "\xce\xb9"}, {0x1f3f, "\xce\xb9"}, {0x1f40, "\xce\xbf"},
    {0x1f41, "\xce\xbf"}, {0x1f42, "\xce\xbf"}, {0x1f43, "\xce\xbf"}, {0x1f44, "\xce\xbf"},
    {0x1f45, "\xce\xbf"}, {0x1f48, "\xce\xbf"}, {0x1f49, "\xce\xbf"}, {0x1f4a, "\xce\xbf"},
    {0x1f4b, "\xce\xbf"}, {0x1f4c, "\xce\xbf"}, {0x1f4d, "\xce\xbf"}, {0x1f50, "\xcf\x85"},
    {0x1f51, "\xcf\x85"}, {0x1f52, "\xcf\x85"}, {0x1f53, "\xcf\x85"}, {0x1f54, "\xcf\x85"},
    {0x1f55, "\xcf\x85"}, {0x1f56, "\xcf\x85"}, {0x1f57, "\xcf\x85"}, {0x1f59, "\xcf\x85"},
    {0x1f5b, "\xcf\x85"}, {0x1f5d, "\xcf\x85"}, {0x1f5f, "\xcf\x85"}, {0x1f60, "\xcf\x89"},
    {0x1f61, "\xcf\x89"}, {0x1f62, "\xcf\x89"}, {0x1f63, "\xcf\x89"}, {0x1f64, "\xcf\x89"},
    {0x1f65, "\xcf\x89"}, {0x1f66, "\xcf\x89"}, {0x1f67, "\xcf\x89"}, {0x1f68, "\xcf\x89"},
    {0x1f69, "\xcf\x89"}, {0x1f6a, "\xcf\x89"}, {0x1f6b, "\xcf\x89"}, {0x1f6c, "\xcf\x89"},
    {0x1f6d, "\xcf\x89"}, {0x1f6e, "\xcf\x89"}, {0x1f6f, "\xcf\x89"}, {0x1f70, "\xce\xb1"},
    {0x1f71, "\xce\xb1"}, {0x1f72, "\xce\xb5"}, {0x1f73, "\xce\xb5"}, {0x1f74, "\xce\xb7"},
    {0x1f75, "\xce\xb7"}, {0x1f76, "\xce\xb9"}, {0x1f77, "\xce\xb9"}, {0x1f78, "\xce\xbf"},
    {0x1f79, "\xce\xbf"}, {0x1f7a, "\xcf\x85"}, {0x1f7b, "\xcf\x85"}, {0x1f7c, "\xcf\x89"},
    {0x1f7d, "\xcf\x89"}, {0x1f80, "\xce\xb1"}, {0x1f81, "\xce\xb1"}, {0x1f82, "\xce\xb1"},
    {0x1f83, "\xce\xb1"}, {0x1f84, "\xce\xb1"}, {0x1f85, "\xce\xb1"}, {0x1f86, "\xce\xb1"},
    {0x1f87, "\xce\xb1"}, {0x1f88, "\xce\xb1"}, {0x1f89, "\xce\xb1"}, {0x1f8a, "\xce\xb1"},
    {0x1f8b, "\xce\xb1"}, {0x1f8c, "\xce\xb1"}, {0x1f8d, "\xce\xb1"}, {0x1f8e, "\xce\xb1"},
    {0x1f8f, "\xce\xb1"}, {0x1f90, "\xce\xb7"}, {0x1f91, "\xce\xb7"}, {0x1f92, "\xce\xb7"},
    {0x1f93, "\xce\xb7"}, {0x1f94, "\xce\xb7"}, {0x1f95, "\xce\xb7"}, {0x1f96, "\xce\xb7"},
    {0x1f97, "\xce\xb7"}, {0x1f98, "\xce\xb7"}, {0x1f99, "\xce\xb7"}, {0x1f9a, "\xce\xb7"},
    {0x1f9b, "\xce\xb7"}, {0x1f9c, "\xce\xb7"}, {0x1f9d, "\xce\xb7"}, {0x1f9e, "\xce\xb7"},
    {0x1f9f, "\xce\xb7"}, {0x1fa0, "\xcf\x89"}, {0x1fa1, "\xcf\x89"}, {0x1fa2, "\xcf\x89"},
    {0x1fa3, "\xcf\x89"}, {0x1fa4, "\xcf\x89"}, {0x1fa5, "\xcf\x89"}, {0x1fa6, "\xcf\x89"},
    {0x1fa7, "\xcf\x89"}, {0x1fa8, "\xcf\x89"}, {0x1fa9, "\xcf\x89"}, {0x1faa, "\xcf\x89"},
    {0x1fab, "\xcf\x89"}, {0x1fac, "\xcf\x89"}, {0x1fad, "\xcf\x89"}, {0x1fae, "\xcf\x89"},
    {0x1faf, "\xcf\x89"}, {0x1fb0, "\xce\xb1"}, {0x1fb1, "\xce\xb1"}, {0x1fb2, "\xce\xb1"},
    {0x1fb3, "\xce\xb1"}, {0x1fb4, "\xce\xb1"}, {0x1fb6, "\xce\xb1"}, {0x1fb7, "\xce\xb1"},
    {0x1fb8, "\xce\xb1"}, {0x1fb9, "\xce\xb1"}, {0x1fba, "\xce\xb1"}, {0x1fbb, "\xce\xb1"},
    {0x1fbc, "\xce\xb1"}, {0x1fbe, "\xce\xb9"}, {0x1fc1, "\xc2\xa8"}, {0x1fc2, "\xce\xb7"},
    {0x1fc3, "\xce\xb7"}, {0x1fc4, "\xce\xb7"}, {0x1fc6, "\xce\xb7"}, {0x1fc7, "\xce\xb7"},
    {0x1fc8, "\xce\xb5"}, {0x1fc9, "\xce\xb5"}, {0x1fca, "\xce\xb7"}, {0x1fcb, "\xce\xb7"},
    {0x1fcc, "\xce\xb7"}, {0x1fcd, "\xe1\xbe\xbf"}, {0x1fce, "\xe1\xbe\xbf"}, {0x1fcf, "\xe1\xbe\xbf"},
    {0x1fd0, "\xce\xb9"}, {0x1fd1, "\xce\xb9"}, {0x1fd2, "\xce\xb9"}, {0x1fd3, "\xce\xb9"},
    {0x1fd6, "\xce\xb9"}, {0x1fd7, "\xce\xb9"}, {0x1fd8, "\xce\xb9"}, {0x1fd9, "\xce\xb9"},
    {0x1fda, "\xce\xb9"}, {0x1fdb, "\xce\xb9"}, {0x1fdd, "\xe1\xbf\xbe"}, {0x1fde, "\xe1\xbf\xbe"},
    {0x1fdf, "\xe1\xbf\xbe"}, {0x1fe0, "\xcf\x85"}, {0x1fe1, "\xcf\x85"}, {0x1fe2, "\xcf\x85"},
    {0x1fe3, "\xcf\x85"}, {0x1fe4, "\xcf\x81"}, {0x1fe5, "\xcf\x81"}, {0x1fe6, "\xcf\x85"},
    {0x1fe7, "\xcf\x85"}, {0x1fe8, "\xcf\x85"}, {0x1fe9, "\xcf\x85"}, {0x1fea, "\xcf\x85"},
    {0x1feb, "\xcf\x85"}, {0x1fec, "\xcf\x81"}, {0x1fed, "\xc2\xa8"}, {0x1fee, "\xc2\xa8"},
    {0x1fef, "\x60"}, {0x1ff2, "\xcf\x89"}, {0x1ff3, "\xcf\x89"}, {0x1ff4, "\xcf\x89"},
    {0x1ff6, "\xcf\x89"}, {0x1ff7, "\xcf\x89"}, {0x1ff8, "\xce\xbf"}, {0x1ff9, "\xce\xbf"},
    {0x1ffa, "\xcf\x89"}, {0x1ffb, "\xcf\x89"}, {0x1ffc, "\xcf\x89"}, {0x1ffd, "\xc2\xb4"},
    {0x2000, "\xe2\x80\x82"}, {0x2001, "\xe2\x80\x83"}, {0x2126, "\xcf\x89"}, {0x212a, "\x6b"},
    {0x212b, "\x61"}, {0x2132, "\xe2\x85\x8e"}, {0x2160, "\xe2\x85\xb0"}, {0x2161, "\xe2\x85\xb1"},
    {0x2162, "\xe2\x85\xb2"}, {0x2163, "\xe2\x85\xb3"}, {0x2164, "\xe2\x85\xb4"}, {0x2165, "\xe2\x85\xb5"},
    {0x2166, "\xe2\x85\xb6"}, {0x2167, "\xe2\x85\xb7"}, {0x2168, "\xe2\x85\xb8"}, {0x2169, "\xe2\x85\xb9"},
    {0x216a, "\xe2\x85\xba"}, {0x216b, "\xe2\x85\xbb"}, {0x216c, "\xe2\x85\xbc"}, {0x216d, "\xe2\x85\xbd"},
    {0x216e, "\xe2\x85\xbe"}, {0x216f, "\xe2\x85\xbf"}, {0x2183, "\xe2\x86\x84"}, {0x219a, "\xe2\x86\x90"},
    {0x219b, "\xe2\x86\x92"}, {0x21ae, "\xe2\x86\x94"}, {0x21cd, "\xe2\x87\x90"}, {0x21ce, "\xe2\x87\x94"},
    {0x21cf, "\xe2\x87\x92"}, {0x2204, "\xe2\x88\x83"}, {0x2209, "\xe2\x88\x88"}, {0x220c, "\xe2\x88\x8b"},
    {0x2224, "\xe2\x88\xa3"}, {0x2226, "\xe2\x88\xa5"}, {0x2241, "\xe2\x88\xbc"}, {0x2244, "\xe2\x89\x83"},
    {0x2247, "\xe2\x89\x85"}, {0x2249, "\xe2\x89\x88"}, {0x2260, "\x3d"}, {0x2262, "\xe2\x89\xa1"},
    {0x226d, "\xe2\x89\x8d"}, {0x226e, "\x3c"}, {0x226f, "\x3e"}, {0x2270, "\xe2\x89\xa4"},
    {0x2271, "\xe2\x89\xa5"}, {0x2274, "\xe2\x89\xb2"}, {0x2275, "\xe2\x89\xb3"}, {0x2278, "\xe2\x89\xb6"},
    {0x2279, "\xe2\x89\xb7"}, {0x2280, "\xe2\x89\xba"}, {0x2281, "\xe2\x89\xbb"}, {0x2284, "\xe2\x8a\x82"},
    {0x2285, "\xe2\x8a\x83"}, {0x2288, "\xe2\x8a\x86"}, {0x2289, "\xe2\x8a\x87"}, {0x22ac, "\xe2\x8a\xa2"},
    {0x22ad, "\xe2\x8a\xa8"}, {0x22ae, "\xe2\x8a\xa9"}, {0x22af, "\xe2\x8a\xab"}, {0x22e0, "\xe2\x89\xbc"},
    {0x22e1, "\xe2\x89\xbd"}, {0x22e2, "\xe2\x8a\x91"}, {0x22e3, "\xe2\x8a\x92"}, {0x22ea, "\xe2\x8a\xb2"},
    {0x22eb, "\xe2\x8a\xb3"}, {0x22ec, "\xe2\x8a\xb4"}, {0x22ed, "\xe2\x8a\xb5"}, {0x2329, "\xe3\x80\x88"},
    {0x232a, "\xe3\x80\x89"}, {0x24b6, "\xe2\x93\x90"}, {0x24b7, "\xe2\x93\x91"}, {0x24b8, "\xe2\x93\x92"},
    {0x24b9, "\xe2\x93\x93"}, {0x24ba, "\xe2\x93\x94"}, {0x24bb, "\xe2\x93\x95"}, {0x24bc, "\xe2\x93\x96"},
    {0x24bd, "\xe2\x93\x97"}, {0x24be, "\xe2\x93\x98"}, {0x24bf, "\xe2\x93\x99"}, {0x24c0, "\xe2\x93\x9a"},
    {0x24c1, "\xe2\x93\x9b"}, {0x24c2, "\xe2\x93\x9c"}, {0x24c3, "\xe2\x93\x9d"}, {0x24c4, "\xe2\x93\x9e"},
    {0x24c5, "\xe2\x93\x9f"}, {0x24c6, "\xe2\x93\xa0"}, {0x24c7, "\xe2\x93\xa1"}, {0x24c8, "\xe2\x93\xa2"},
    {0x24c9, "\xe2\x93\xa3"}, {0x24ca, "\xe2\x93\xa4"}, {0x24cb, "\xe2\x93\xa5"}, {0x24cc, "\xe2\x93\xa6"},
    {0x24cd, "\xe2\x93\xa7"}, {0x24ce, "\xe2\x93\xa8"}, {0x24cf, "\xe2\x93\xa9"}, {0x2adc, "\xe2\xab\x9d"},
    {0x2c00, "\xe2\xb0\xb0"}, {0x2c01, "\xe2\xb0\xb1"}, {0x2c02, "\xe2\xb0\xb2"}, {0x2c03, "\xe2\xb0\xb3"},
    {0x2c04, "\xe2\xb0\xb4"}, {0x2c05, "\xe2\xb0\xb5"}, {0x2c06, "\xe2\xb0\xb6"}, {0x2c07, "\xe2\xb0\xb7"},
    {0x2c08, "\xe2\xb0\xb8"}, {0x2c09, "\xe2\xb0\xb9"}, {0x2c0a, "\xe2\xb0\xba"}, {0x2c0b, "\xe2\xb0\xbb"},
    {0x2c0c, "\xe2\xb0\xbc"}, {0x2c0d, "\xe2\xb0\xbd"}, {0x2c0e, "\xe2\xb0\xbe"}, {0x2c0f, "\xe2\xb0\xbf"},
    {0x2c10, "\xe2\xb1\x80"}, {0x2c11, "\xe2\xb1\x81"}, {0x2c12, "\xe2\xb1\x82"}, {0x2c13, "\xe2\xb1\x83"},
    {0x2c14, "\xe2\xb1\x84"}, {0x2c15, "\xe2\xb1\x85"}, {0x2c16, "\xe2\xb1\x86"}, {0x2c17, "\xe2\xb1\x87"},
    {0x2c18, "\xe2\xb1\x88"}, {0x2c19, "\xe2\xb1\x89"}, {0x2c1a, "\xe2\xb1\x8a"}, {0x2c1b, "\xe2\xb1\x8b"},
    {0x2c1c, "\xe2\xb1\x8c"}, {0x2c1d, "\xe2\xb1\x8d"}, {0x2c1e, "\xe2\xb1\x8e"}, {0x2c1f, "\xe2\xb1\x8f"},
    {0x2c20, "\xe2\xb1\x90"}, {0x2c21, "\xe2\xb1\x91"}, {0x2c22, "\xe2\xb1\x92"}, {0x2c23, "\xe2\xb1\x93"},
    {0x2c24, "\xe2\xb1\x94"}, {0x2c25, "\xe2\xb1\x95"}, {0x2c26, "\xe2\xb1\x96"}, {0x2c27, "\xe2\xb1\x97"},
    {0x2c28, "\xe2\xb1\x98"}, {0x2c29, "\xe2\xb1\x99"}, {0x2c2a, "\xe2\xb1\x9a"}, {0x2c2b, "\xe2\xb1\x9b"},
    {0x2c2c, "\xe2\xb1\x9c"}, {0x2c2d, "\xe2\xb1\x9d"}, {0x2c2e, "\xe2\xb1\x9e"}, {0x2c2f, "\xe2\xb1\x9f"},
    {0x2c60, "\xe2\xb1\xa1"}, {0x2c62, "\xc9\xab"}, {0x2c63, "\xe1\xb5\xbd"}, {0x2c64, "\xc9\xbd"},
    {0x2c67, "\xe2\xb1\xa8"}, {0x2c69, "\xe2\xb1\xaa"}, {0x2c6b, "\xe2\xb1\xac"}, {0x2c6d, "\xc9\x91"},
    {0x2c6e, "\xc9\xb1"}, {0x2c6f, "\xc9\x90"}, {0x2c70, "\xc9\x92"}, {0x2c72, "\xe2\xb1\xb3"},
    {0x2c75, "\xe2\xb1\xb6"}, {0x2c7e, "\xc8\xbf"}, {0x2c7f, "\xc9\x80"}, {0x2c80, "\xe2\xb2\x81"},
    {0x2c82, "\xe2\xb2\x83"}, {0x2c84, "\xe2\xb2\x85"}, {0x2c86, "\xe2\xb2\x87"}, {0x2c88, "\xe2\xb2\x89"},
    {0x2c8a, "\xe2\xb2\x8b"}, {0x2c8c, "\xe2\xb2\x8d"}, {0x2c8e, "\xe2\xb2\x8f"}, {0x2c90, "\xe2\xb2\x91"},
    {0x2c92, "\xe2\xb2\x93"}, {0x2c94, "\xe2\xb2\x95"}, {0x2c96, "\xe2\xb2\x97"}, {0x2c98, "\xe2\xb2\x99"},
    {0x2c9a, "\xe2\xb2\x9b"}, {0x2c9c, "\xe2\xb2\x9d"}, {0x2c9e, "\xe2\xb2\x9f"}, {0x2ca0, "\xe2\xb2\xa1"},
    {0x2ca2, "\xe2\xb2\xa3"}, {0x2ca4, "\xe2\xb2\xa5"}, {0x2ca6, "\xe2\xb2\xa7"}, {0x2ca8, "\xe2\xb2\xa9"},
    {0x2caa, "\xe2\xb2\xab"}, {0x2cac, "\xe2\xb2\xad"}, {0x2cae, "\xe2\xb2\xaf"}, {0x2cb0, "\xe2\xb2\xb1"},
    {0x2cb2, "\xe2\xb2\xb3"}, {0x2cb4, "\xe2\xb2\xb5"}, {0x2cb6, "\xe2\xb2\xb7"}, {0x2cb8, "\xe2\xb2\xb9"},
    {0x2cba, "\xe2\xb2\xbb"}, {0x2cbc, "\xe2\xb2\xbd"}, {0x2cbe, "\xe2\xb2\xbf"}, {0x2cc0, "\xe2\xb3\x81"},
    {0x2cc2, "\xe2\xb3\x83"}, {0x2cc4, "\xe2\xb3\x85"}, {0x2cc6, "\xe2\xb3\x87"}, {0x2cc8, "\xe2\xb3\x89"},
    {0x2cca, "\xe2\xb3\x8b"}, {0x2ccc, "\xe2\xb3\x8d"}, {0x2cce, "\xe2\xb3\x8f"}, {0x2cd0, "\xe2\xb3\x91"},
    {0x2cd2, "\xe2\xb3\x93"}, {0x2cd4, "\xe2\xb3\x95"}, {0x2cd6, "\xe2\xb3\x97"}, {0x2cd8, "\xe2\xb3\x99"},
    {0x2cda, "\xe2\xb3\x9b"}, {0x2cdc, "\xe2\xb3\x9d"}, {0x2cde, "\xe2\xb3\x9f"}, {0x2ce0, "\xe2\xb3\xa1"},
    {0x2ce2, "\xe2\xb3\xa3"}, {0x2ceb, "\xe2\xb3\xac"}, {0x2ced, "\xe2\xb3\xae"}, {0x2cf2, "\xe2\xb3\xb3"},
    {0x304c, "\xe3\x81\x8b\xe3\x82\x99"}, {0x304e, "\xe3\x81\x8d\xe3\x82\x99"}, {0x3050, "\xe3\x81\x8f\xe3\x82\x99"}, {0x3052, "\xe3\x81\x91\xe3\x82\x99"},
    {0x3054, "\xe3\x81\x93\xe3\x82\x99"}, {0x3056, "\xe3\x81\x95\xe3\x82\x99"}, {0x3058, "\xe3\x81\x97\xe3\x82\x99"}, {0x305a, "\xe3\x81\x99\xe3\x82\x99"},
    {0x305c, "\xe3\x81\x9b\xe3\x82\x99"}, {0x305e, "\xe3\x81\x9d\xe3\x82\x99"}, {0x3060, "\xe3\x81\x9f\xe3\x82\x99"}, {0x3062, "\xe3\x81\xa1\xe3\x82\x99"},
    {0x3065, "\xe3\x81\xa4\xe3\x82\x99"}, {0x3067, "\xe3\x81\xa6\xe3\x82\x99"}, {0x3069, "\xe3\x81\xa8\xe3\x82\x99"}, {0x3070, "\xe3\x81\xaf\xe3\x82\x99"},
    {0x3071, "\xe3\x81\xaf\xe3\x82\x9a"}, {0x3073, "\xe3\x81\xb2\xe3\x82\x99"}, {0x3074, "\xe3\x81\xb2\xe3\x82\x9a"}, {0x3076, "\xe3\x81\xb5\xe3\x82\x99"},
    {0x3077, "\xe3\x81\xb5\xe3\x82\x9a"}, {0x3079, "\xe3\x81\xb8\xe3\x82\x99"}, {0x307a, "\xe3\x81\xb8\xe3\x82\x9a"}, {0x307c, "\xe3\x81\xbb\xe3\x82\x99"},
    {0x307d, "\xe3\x81\xbb\xe3\x82\x9a"}, {0x3094, "\xe3\x81\x86\xe3\x82\x99"}, {0x309e, "\xe3\x82\x9d\xe3\x82\x99"}, {0x30ac, "\xe3\x82\xab\xe3\x82\x99"},
    {0x30ae, "\xe3\x82\xad\xe3\x82\x99"}, {0x30b0, "\xe3\x82\xaf\xe3\x82\x99"}, {0x30b2, "\xe3\x82\xb1\xe3\x82\x99"}, {0x30b4, "\xe3\x82\xb3\xe3\x82\x99"},
    {0x30b6, "\xe3\x82\xb5\xe3\x82\x99"}, {0x30b8, "\xe3\x82\xb7\xe3\x82\x99"}, {0x30ba, "\xe3\x82\xb9\xe3\x82\x99"}, {0x30bc, "\xe3\x82\xbb\xe3\x82\x99"},
    {0x30be, "\xe3\x82\xbd\xe3\x82\x99"}, {0x30c0, "\xe3\x82\xbf\xe3\x82\x99"}, {0x30c2, "\xe3\x83\x81\xe3\x82\x99"}, {0x30c5, "\xe3\x83\x84\xe3\x82\x99"},
    {0x30c7, "\xe3\x83\x86\xe3\x82\x99"}, {0x30c9, "\xe3\x83\x88\xe3\x82\x99"}, {0x30d0, "\xe3\x83\x8f\xe3\x82\x99"}, {0x30d1, "\xe3\x83\x8f\xe3\x82\x9a"},
    {0x30d3, "\xe3\x83\x92\xe3\x82\x99"}, {0x30d4, "\xe3\x83\x92\xe3\x82\x9a"}, {0x30d6, "\xe3\x83\x95\xe3\x82\x99"}, {0x30d7, "\xe3\x83\x95\xe3\x82\x9a"},
    {0x30d9, "\xe3\x83\x98\xe3\x82\x99"}, {0x30da, "\xe3\x83\x98\xe3\x82\x9a"}, {0x30dc, "\xe3\x83\x9b\xe3\x82\x99"}, {0x30dd, "\xe3\x83\x9b\xe3\x82\x9a"},
    {0x30f4, "\xe3\x82\xa6\xe3\x82\x99"}, {0x30f7, "\xe3\x83\xaf\xe3\x82\x99"}, {0x30f8, "\xe3\x83\xb0\xe3\x82\x99"}, {0x30f9, "\xe3\x83\xb1\xe3\x82\x99"},
    {0x30fa, "\xe3\x83\xb2\xe3\x82\x99"}, {0x30fe, "\xe3\x83\xbd\xe3\x82\x99"}, {0xa640, "\xea\x99\x81"}, {0xa642, "\xea\x99\x83"},
    {0xa644, "\xea\x99\x85"}, {0xa646, "\xea\x99\x87"}, {0xa648, "\xea\x99\x89"}, {0xa64a, "\xea\x99\x8b"},
    {0xa64c, "\xea\x99\x8d"}, {0xa64e, "\xea\x99\x8f"}, {0xa650, "\xea\x99\x91"}, {0xa652, "\xea\x99\x93"},
    {0xa654, "\xea\x99\x95"}, {0xa656, "\xea\x99\x97"}, {0xa658, "\xea\x99\x99"}, {0xa65a, "\xea\x99\x9b"},
    {0xa65c, "\xea\x99\x9d"}, {0xa65e, "\xea\x99\x9f"}, {0xa660, "\xea\x99\xa1"}, {0xa662, "\xea\x99\xa3"},
    {0xa664, "\xea\x99\xa5"}, {0xa666, "\xea\x99\xa7"}, {0xa668, "\xea\x99\xa9"}, {0xa66a, "\xea\x99\xab"},
    {0xa66c, "\xea\x99\xad"}, {0xa680, "\xea\x9a\x81"}, {0xa682, "\xea\x9a\x83"}, {0xa684, "\xea\x9a\x85"},
    {0xa686, "\xea\x9a\x87"}, {0xa688, "\xea\x9a\x89"}, {0xa68a, "\xea\x9a\x8b"}, {0xa68c, "\xea\x9a\x8d"},
    {0xa68e, "\xea\x9a\x8f"}, {0xa690, "\xea\x9a\x91"}, {0xa692, "\xea\x9a\x93"}, {0xa694, "\xea\x9a\x95"},
    {0xa696, "\xea\x9a\x97"}, {0xa698, "\xea\x9a\x99"}, {0xa69a, "\xea\x9a\x9b"}, {0xa722, "\xea\x9c\xa3"},
    {0xa724, "\xea\x9c\xa5"}, {0xa726, "\xea\x9c\xa7"}, {0xa728, "\xea\x9c\xa9"}, {0xa72a, "\xea\x9c\xab"},
    {0xa72c, "\xea\x9c\xad"}, {0xa72e, "\xea\x9c\xaf"}, {0xa732, "\xea\x9c\xb3"}, {0xa734, "\xea\x9c\xb5"},
    {0xa736, "\xea\x9c\xb7"}, {0xa738, "\xea\x9c\xb9"}, {0xa73a, "\xea\x9c\xbb"}, {0xa73c, "\xea\x9c\xbd"},
    {0xa73e, "\xea\x9c\xbf"}, {0xa740, "\xea\x9d\x81"}, {0xa742, "\xea\x9d\x83"}, {0xa744, "\xea\x9d\x85"},
    {0xa746, "\xea\x9d\x87"}, {0xa748, "\xea\x9d\x89"}, {0xa74a, "\xea\x9d\x8b"}, {0xa74c, "\xea\x9d\x8d"},
    {0xa74e, "\xea\x9d\x8f"}, {0xa750, "\xea\x9d\x91"}, {0xa752, "\xea\x9d\x93"}, {0xa754, "\xea\x9d\x95"},
    {0xa756, "\xea\x9d\x97"}, {0xa758, "\xea\x9d\x99"}, {0xa75a, "\xea\x9d\x9b"}, {0xa75c, "\xea\x9d\x9d"},
    {0xa75e, "\xea\x9d\x9f"}, {0xa760, "\xea\x9d\xa1"}, {0xa762, "\xea\x9d\xa3"}, {0xa764, "\xea\x9d\xa5"},
    {0xa766, "\xea\x9d\xa7"}, {0xa768, "\xea\x9d\xa9"}, {0xa76a, "\xea\x9d\xab"}, {0xa76c, "\xea\x9d\xad"},
    {0xa76e, "\xea\x9d\xaf"}, {0xa779, "\xea\x9d\xba"}, {0xa77b, "\xea\x9d\xbc"}, {0xa77d, "\xe1\xb5\xb9"},
    {0xa77e, "\xea\x9d\xbf"}, {0xa780, "\xea\x9e\x81"}, {0xa782, "\xea\x9e\x83"}, {0xa784, "\xea\x9e\x85"},
    {0xa786, "\xea\x9e\x87"}, {0xa78b, "\xea\x9e\x8c"}, {0xa78d, "\xc9\xa5"}, {0xa790, "\xea\x9e\x91"},
    {0xa792, "\xea\x9e\x93"}, {0xa796, "\xea\x9e\x97"}, {0xa798, "\xea\x9e\x99"}, {0xa79a, "\xea\x9e\x9b"},
    {0xa79c, "\xea\x9e\x9d"}, {0xa79e, "\xea\x9e\x9f"}, {0xa7a0, "\xea\x9e\xa1"}, {0xa7a2, "\xea\x9e\xa3"},
    {0xa7a4, "\xea\x9e\xa5"}, {0xa7a6, "\xea\x9e\xa7"}, {0xa7a8, "\xea\x9e\xa9"}, {0xa7aa, "\xc9\xa6"},
    {0xa7ab, "\xc9\x9c"}, {0xa7ac, "\xc9\xa1"}, {0xa7ad, "\xc9\xac"}, {0xa7ae, "\xc9\xaa"},
    {0xa7b0, "\xca\x9e"}, {0xa7b1, "\xca\x87"}, {0xa7b2, "\xca\x9d"}, {0xa7b3, "\xea\xad\x93"},
    {0xa7b4, "\xea\x9e\xb5"}, {0xa7b6, "\xea\x9e\xb7"}, {0xa7b8, "\xea\x9e\xb9"}, {0xa7ba, "\xea\x9e\xbb"},
    {0xa7bc, "\xea\x9e\xbd"}, {0xa7be, "\xea\x9e\xbf"}, {0xa7c0, "\xea\x9f\x81"}, {0xa7c2, "\xea\x9f\x83"},
    {0xa7c4, "\xea\x9e\x94"}, {0xa7c5, "\xca\x82"}, {0xa7c6, "\xe1\xb6\x8e"}, {0xa7c7, "\xea\x9f\x88"},
    {0xa7c9, "\xea\x9f\x8a"}, {0xa7cb, "\xc9\xa4"}, {0xa7cc, "\xea\x9f\x8d"}, {0xa7ce, "\xea\x9f\x8f"},
    {0xa7d0, "\xea\x9f\x91"}, {0xa7d2, "\xea\x9f\x93"}, {0xa7d4, "\xea\x9f\x95"}, {0xa7d6, "\xea\x9f\x97"},
    {0xa7d8, "\xea\x9f\x99"}, {0xa7da, "\xea\x9f\x9b"}, {0xa7dc, "\xc6\x9b"}, {0xa7f5, "\xea\x9f\xb6"},
    {0xf900, "\xe8\xb1\x88"}, {0xf901, "\xe6\x9b\xb4"}, {0xf902, "\xe8\xbb\x8a"}, {0xf903, "\xe8\xb3\x88"},
    {0xf904, "\xe6\xbb\x91"}, {0xf905, "\xe4\xb8\xb2"}, {0xf906, "\xe5\x8f\xa5"}, {0xf907, "\xe9\xbe\x9c"},
    {0xf908, "\xe9\xbe\x9c"}, {0xf909, "\xe5\xa5\x91"}, {0xf90a, "\xe9\x87\x91"}, {0xf90b, "\xe5\x96\x87"},
    {0xf90c, "\xe5\xa5\x88"}, {0xf90d, "\xe6\x87\xb6"}, {0xf90e, "\xe7\x99\xa9"}, {0xf90f, "\xe7\xbe\x85"},
    {0xf910, "\xe8\x98\xbf"}, {0xf911, "\xe8\x9e\xba"}, {0xf912, "\xe8\xa3\xb8"}, {0xf913, "\xe9\x82\x8f"},
    {0xf914, "\xe6\xa8\x82"}, {0xf915, "\xe6\xb4\x9b"}, {0xf916, "\xe7\x83\x99"}, {0xf917, "\xe7\x8f\x9e"},
    {0xf918, "\xe8\x90\xbd"}, {0xf919, "\xe9\x85\xaa"}, {0xf91a, "\xe9\xa7\xb1"}, {0xf91b, "\xe4\xba\x82"},
    {0xf91c, "\xe5\x8d\xb5"}, {0xf91d, "\xe6\xac\x84"}, {0xf91e, "\xe7\x88\x9b"}, {0xf91f, "\xe8\x98\xad"},
    {0xf920, "\xe9\xb8\x9e"}, {0xf921, "\xe5\xb5\x90"}, {0xf922, "\xe6\xbf\xab"}, {0xf923, "\xe8\x97\x8d"},
    {0xf924, "\xe8\xa5\xa4"}, {0xf925, "\xe6\x8b\x89"}, {0xf926, "\xe8\x87\x98"}, {0xf927, "\xe8\xa0\x9f"},
    {0xf928, "\xe5\xbb\x8a"}, {0xf929, "\xe6\x9c\x97"}, {0xf92a, "\xe6\xb5\xaa"}, {0xf92b, "\xe7\x8b\xbc"},
    {0xf92c, "\xe9\x83\x8e"}, {0xf92d, "\xe4\xbe\x86"}, {0xf92e, "\xe5\x86\xb7"}, {0xf92f, "\xe5\x8b\x9e"},
    {0xf930, "\xe6\x93\x84"}, {0xf931, "\xe6\xab\x93"}, {0xf932, "\xe7\x88\x90"}, {0xf933, "\xe7\x9b\xa7"},
    {0xf934, "\xe8\x80\x81"}, {0xf935, "\xe8\x98\x86"}, {0xf936, "\xe8\x99\x9c"}, {0xf937, "\xe8\xb7\xaf"},
    {0xf938, "\xe9\x9c\xb2"}, {0xf939, "\xe9\xad\xaf"}, {0xf93a, "\xe9\xb7\xba"}, {0xf93b, "\xe7\xa2\x8c"},
    {0xf93c, "\xe7\xa5\xbf"}, {0xf93d, "\xe7\xb6\xa0"}, {0xf93e, "\xe8\x8f\x89"}, {0xf93f, "\xe9\x8c\x84"},
    {0xf940, "\xe9\xb9\xbf"}, {0xf941, "\xe8\xab\x96"}, {0xf942, "\xe5\xa3\x9f"}, {0xf943, "\xe5\xbc\x84"},
    {0xf944, "\xe7\xb1\xa0"}, {0xf945, "\xe8\x81\xbe"}, {0xf946, "\xe7\x89\xa2"}, {0xf947, "\xe7\xa3\x8a"},
    {0xf948, "\xe8\xb3\x82"}, {0xf949, "\xe9\x9b\xb7"}, {0xf94a, "\xe5\xa3\x98"}, {0xf94b, "\xe5\xb1\xa2"},
    {0xf94c, "\xe6\xa8\x93"}, {0xf94d, "\xe6\xb7\x9a"}, {0xf94e, "\xe6\xbc\x8f"}, {0xf94f, "\xe7\xb4\xaf"},
    {0xf950, "\xe7\xb8\xb7"}, {0xf951, "\xe9\x99\x8b"}, {0xf952, "\xe5\x8b\x92"}, {0xf953, "\xe8\x82\x8b"},
    {0xf954, "\xe5\x87\x9c"}, {0xf955, "\xe5\x87\x8c"}, {0xf956, "\xe7\xa8\x9c"}, {0xf957, "\xe7\xb6\xbe"},
    {0xf958, "\xe8\x8f\xb1"}, {0xf959, "\xe9\x99\xb5"}, {0xf95a, "\xe8\xae\x80"}, {0xf95b, "\xe6\x8b\x8f"},
    {0xf95c, "\xe6\xa8\x82"}, {0xf95d, "\xe8\xab\xbe"}, {0xf95e, "\xe4\xb8\xb9"}, {0xf95f, "\xe5\xaf\xa7"},
    {0xf960, "\xe6\x80\x92"}, {0xf961, "\xe7\x8e\x87"}, {0xf962, "\xe7\x95\xb0"}, {0xf963, "\xe5\x8c\x97"},
    {0xf964, "\xe7\xa3\xbb"}, {0xf965, "\xe4\xbe\xbf"}, {0xf966, "\xe5\xbe\xa9"}, {0xf967, "\xe4\xb8\x8d"},
    {0xf968, "\xe6\xb3\x8c"}, {0xf969, "\xe6\x95\xb8"}, {0xf96a, "\xe7\xb4\xa2"}, {0xf96b, "\xe5\x8f\x83"},
    {0xf96c, "\xe5\xa1\x9e"}, {0xf96d, "\xe7\x9c\x81"}, {0xf96e, "\xe8\x91\x89"}, {0xf96f, "\xe8\xaa\xaa"},
    {0xf970, "\xe6\xae\xba"}, {0xf971, "\xe8\xbe\xb0"}, {0xf972, "\xe6\xb2\x88"}, {0xf973, "\xe6\x8b\xbe"},
    {0xf974, "\xe8\x8b\xa5"}, {0xf975, "\xe6\x8e\xa0"}, {0xf976, "\xe7\x95\xa5"}, {0xf977, "\xe4\xba\xae"},
    {0xf978, "\xe5\x85\xa9"}, {0xf979, "\xe5\x87\x89"}, {0xf97a, "\xe6\xa2\x81"}, {0xf97b, "\xe7\xb3\xa7"},
    {0xf97c, "\xe8\x89\xaf"}, {0xf97d, "\xe8\xab\x92"}, {0xf97e, "\xe9\x87\x8f"}, {0xf97f, "\xe5\x8b\xb5"},
    {0xf980, "\xe5\x91\x82"}, {0xf981, "\xe5\xa5\xb3"}, {0xf982, "\xe5\xbb\xac"}, {0xf983, "\xe6\x97\x85"},
    {0xf984, "\xe6\xbf\xbe"}, {0xf985, "\xe7\xa4\xaa"}, {0xf986, "\xe9\x96\xad"}, {0xf987, "\xe9\xa9\xaa"},
    {0xf988, "\xe9\xba\x97"}, {0xf989, "\xe9\xbb\x8e"}, {0xf98a, "\xe5\x8a\x9b"}, {0xf98b, "\xe6\x9b\x86"},
    {0xf98c, "\xe6\xad\xb7"}, {0xf98d, "\xe8\xbd\xa2"}, {0xf98e, "\xe5\xb9\xb4"}, {0xf98f, "\xe6\x86\x90"},
    {0xf990, "\xe6\x88\x80"}, {0xf991, "\xe6\x92\x9a"}, {0xf992, "\xe6\xbc\xa3"}, {0xf993, "\xe7\x85\x89"},
    {0xf994, "\xe7\x92\x89"}, {0xf995, "\xe7\xa7\x8a"}, {0xf996, "\xe7\xb7\xb4"}, {0xf997, "\xe8\x81\xaf"},
    {0xf998, "\xe8\xbc\xa6"}, {0xf999, "\xe8\x93\xae"}, {0xf99a, "\xe9\x80\xa3"}, {0xf99b, "\xe9\x8d\x8a"},
    {0xf99c, "\xe5\x88\x97"}, {0xf99d, "\xe5\x8a\xa3"}, {0xf99e, "\xe5\x92\xbd"}, {0xf99f, "\xe7\x83\x88"},
    {0xf9a0, "\xe8\xa3\x82"}, {0xf9a1, "\xe8\xaa\xaa"}, {0xf9a2, "\xe5\xbb\x89"}, {0xf9a3, "\xe5\xbf\xb5"},
    {0xf9a4, "\xe6\x8d\xbb"}, {0xf9a5, "\xe6\xae\xae"}, {0xf9a6, "\xe7\xb0\xbe"}, {0xf9a7, "\xe7\x8d\xb5"},
    {0xf9a8, "\xe4\xbb\xa4"}, {0xf9a9, "\xe5\x9b\xb9"}, {0xf9aa, "\xe5\xaf\xa7"}, {0xf9ab, "\xe5\xb6\xba"},
    {0xf9ac, "\xe6\x80\x9c"}, {0xf9ad, "\xe7\x8e\xb2"}, {0xf9ae, "\xe7\x91\xa9"}, {0xf9af, "\xe7\xbe\x9a"},
    {0xf9b0, "\xe8\x81\x86"}, {0xf9b1, "\xe9\x88\xb4"}, {0xf9b2, "\xe9\x9b\xb6"}, {0xf9b3, "\xe9\x9d\x88"},
    {0xf9b4, "\xe9\xa0\x98"}, {0xf9b5, "\xe4\xbe\x8b"}, {0xf9b6, "\xe7\xa6\xae"}, {0xf9b7, "\xe9\x86\xb4"},
    {0xf9b8, "\xe9\x9a\xb8"}, {0xf9b9, "\xe6\x83\xa1"}, {0xf9ba, "\xe4\xba\x86"}, {0xf9bb, "\xe5\x83\x9a"},
    {0xf9bc, "\xe5\xaf\xae"}, {0xf9bd, "\xe5\xb0\xbf"}, {0xf9be, "\xe6\x96\x99"}, {0xf9bf, "\xe6\xa8\x82"},
    {0xf9c0, "\xe7\x87\x8e"}, {0xf9c1, "\xe7\x99\x82"}, {0xf9c2, "\xe8\x93\xbc"}, {0xf9c3, "\xe9\x81\xbc"},
    {0xf9c4, "\xe9\xbe\x8d"}, {0xf9c5, "\xe6\x9a\x88"}, {0xf9c6, "\xe9\x98\xae"}, {0xf9c7, "\xe5\x8a\x89"},
    {0xf9c8, "\xe6\x9d\xbb"}, {0xf9c9, "\xe6\x9f\xb3"}, {0xf9ca, "\xe6\xb5\x81"}, {0xf9cb, "\xe6\xba\x9c"},
    {0xf9cc, "\xe7\x90\x89"}, {0xf9cd, "\xe7\x95\x99"}, {0xf9ce, "\xe7\xa1\xab"}, {0xf9cf, "\xe7\xb4\x90"},
    {0xf9d0, "\xe9\xa1\x9e"}, {0xf9d1, "\xe5\x85\xad"}, {0xf9d2, "\xe6\x88\xae"}, {0xf9d3, "\xe9\x99\xb8"},
    {0xf9d4, "\xe5\x80\xab"}, {0xf9d5, "\xe5\xb4\x99"}, {0xf9d6, "\xe6\xb7\xaa"}, {0xf9d7, "\xe8\xbc\xaa"},
    {0xf9d8, "\xe5\xbe\x8b"}, {0xf9d9, "\xe6\x85\x84"}, {0xf9da, "\xe6\xa0\x97"}, {0xf9db, "\xe7\x8e\x87"},
    {0xf9dc, "\xe9\x9a\x86"}, {0xf9dd, "\xe5\x88\xa9"}, {0xf9de, "\xe5\x90\x8f"}, {0xf9df, "\xe5\xb1\xa5"},
    {0xf9e0, "\xe6\x98\x93"}, {0xf9e1, "\xe6\x9d\x8e"}, {0xf9e2, "\xe6\xa2\xa8"}, {0xf9e3, "\xe6\xb3\xa5"},
    {0xf9e4, "\xe7\x90\x86"}, {0xf9e5, "\xe7\x97\xa2"}, {0xf9e6, "\xe7\xbd\xb9"}, {0xf9e7, "\xe8\xa3\x8f"},
    {0xf9e8, "\xe8\xa3\xa1"}, {0xf9e9, "\xe9\x87\x8c"}, {0xf9ea, "\xe9\x9b\xa2"}, {0xf9eb, "\xe5\x8c\xbf"},
    {0xf9ec, "\xe6\xba\xba"}, {0xf9ed, "\xe5\x90\x9d"}, {0xf9ee, "\xe7\x87\x90"}, {0xf9ef, "\xe7\x92\x98"},
    {0xf9f0, "\xe8\x97\xba"}, {0xf9f1, "\xe9\x9a\xa3"}, {0xf9f2, "\xe9\xb1\x97"}, {0xf9f3, "\xe9\xba\x9f"},
    {0xf9f4, "\xe6\x9e\x97"}, {0xf9f5, "\xe6\xb7\x8b"}, {0xf9f6, "\xe8\x87\xa8"}, {0xf9f7, "\xe7\xab\x8b"},
    {0xf9f8, "\xe7\xac\xa0"}, {0xf9f9, "\xe7\xb2\x92"}, {0xf9fa, "\xe7\x8b\x80"}, {0xf9fb, "\xe7\x82\x99"},
    {0xf9fc, "\xe8\xad\x98"}, {0xf9fd, "\xe4\xbb\x80"}, {0xf9fe, "\xe8\x8c\xb6"}, {0xf9ff, "\xe5\x88\xba"},
    {0xfa00, "\xe5\x88\x87"}, {0xfa01, "\xe5\xba\xa6"}, {0xfa02, "\xe6\x8b\x93"}, {0xfa03, "\xe7\xb3\x96"},
    {0xfa04, "\xe5\xae\x85"}, {0xfa05, "\xe6\xb4\x9e"}, {0xfa06, "\xe6\x9a\xb4"}, {0xfa07, "\xe8\xbc\xbb"},
    {0xfa08, "\xe8\xa1\x8c"}, {0xfa09, "\xe9\x99\x8d"}, {0xfa0a, "\xe8\xa6\x8b"}, {0xfa0b, "\xe5\xbb\x93"},
    {0xfa0c, "\xe5\x85\x80"}, {0xfa0d, "\xe5\x97\x80"}, {0xfa10, "\xe5\xa1\x9a"}, {0xfa12, "\xe6\x99\xb4"},
    {0xfa15, "\xe5\x87\x9e"}, {0xfa16, "\xe7\x8c\xaa"}, {0xfa17, "\xe7\x9b\x8a"}, {0xfa18, "\xe7\xa4\xbc"},
    {0xfa19, "\xe7\xa5\x9e"}, {0xfa1a, "\xe7\xa5\xa5"}, {0xfa1b, "\xe7\xa6\x8f"}, {0xfa1c, "\xe9\x9d\x96"},
    {0xfa1d, "\xe7\xb2\xbe"}, {0xfa1e, "\xe7\xbe\xbd"}, {0xfa20, "\xe8\x98\x92"}, {0xfa22, "\xe8\xab\xb8"},
    {0xfa25, "\xe9\x80\xb8"}, {0xfa26, "\xe9\x83\xbd"}, {0xfa2a, "\xe9\xa3\xaf"}, {0xfa2b, "\xe9\xa3\xbc"},
    {0xfa2c, "\xe9\xa4\xa8"}, {0xfa2d, "\xe9\xb6\xb4"}, {0xfa2e, "\xe9\x83\x9e"}, {0xfa2f, "\xe9\x9a\xb7"},
    {0xfa30, "\xe4\xbe\xae"}, {0xfa31, "\xe5\x83\xa7"}, {0xfa32, "\xe5\x85\x8d"}, {0xfa33, "\xe5\x8b\x89"},
    {0xfa34, "\xe5\x8b\xa4"}, {0xfa35, "\xe5\x8d\x91"}, {0xfa36, "\xe5\x96\x9d"}, {0xfa37, "\xe5\x98\x86"},
    {0xfa38, "\xe5\x99\xa8"}, {0xfa39, "\xe5\xa1\x80"}, {0xfa3a, "\xe5\xa2\xa8"}, {0xfa3b, "\xe5\xb1\xa4"},
    {0xfa3c, "\xe5\xb1\xae"}, {0xfa3d, "\xe6\x82\x94"}, {0xfa3e, "\xe6\x85\xa8"}, {0xfa3f, "\xe6\x86\x8e"},
    {0xfa40, "\xe6\x87\xb2"}, {0xfa41, "\xe6\x95\x8f"}, {0xfa42, "\xe6\x97\xa2"}, {0xfa43, "\xe6\x9a\x91"},
    {0xfa44, "\xe6\xa2\x85"}, {0xfa45, "\xe6\xb5\xb7"}, {0xfa46, "\xe6\xb8\x9a"}, {0xfa47, "\xe6\xbc\xa2"},
    {0xfa48, "\xe7\x85\xae"}, {0xfa49, "\xe7\x88\xab"}, {0xfa4a, "\xe7\x90\xa2"}, {0xfa4b, "\xe7\xa2\x91"},
    {0xfa4c, "\xe7\xa4\xbe"}, {0xfa4d, "\xe7\xa5\x89"}, {0xfa4e, "\xe7\xa5\x88"}, {0xfa4f, "\xe7\xa5\x90"},
    {0xfa50, "\xe7\xa5\x96"}, {0xfa51, "\xe7\xa5\x9d"}, {0xfa52, "\xe7\xa6\x8d"}, {0xfa53, "\xe7\xa6\x8e"},
    {0xfa54, "\xe7\xa9\x80"}, {0xfa55, "\xe7\xaa\x81"}, {0xfa56, "\xe7\xaf\x80"}, {0xfa57, "\xe7\xb7\xb4"},
    {0xfa58, "\xe7\xb8\x89"}, {0xfa59, "\xe7\xb9\x81"}, {0xfa5a, "\xe7\xbd\xb2"}, {0xfa5b, "\xe8\x80\x85"},
    {0xfa5c, "\xe8\x87\xad"}, {0xfa5d, "\xe8\x89\xb9"}, {0xfa5e, "\xe8\x89\xb9"}, {0xfa5f, "\xe8\x91\x97"},
    {0xfa60, "\xe8\xa4\x90"}, {0xfa61, "\xe8\xa6\x96"}, {0xfa62, "\xe8\xac\x81"}, {0xfa63, "\xe8\xac\xb9"},
    {0xfa64, "\xe8\xb3\x93"}, {0xfa65, "\xe8\xb4\x88"}, {0xfa66, "\xe8\xbe\xb6"}, {0xfa67, "\xe9\x80\xb8"},
    {0xfa68, "\xe9\x9b\xa3"}, {0xfa69, "\xe9\x9f\xbf"}, {0xfa6a, "\xe9\xa0\xbb"}, {0xfa6b, "\xe6\x81\xb5"},
    {0xfa6c, "\xf0\xa4\x8b\xae"}, {0xfa6d, "\xe8\x88\x98"}, {0xfa70, "\xe4\xb8\xa6"}, {0xfa71, "\xe5\x86\xb5"},
    {0xfa72, "\xe5\x85\xa8"}, {0xfa73, "\xe4\xbe\x80"}, {0xfa74, "\xe5\x85\x85"}, {0xfa75, "\xe5\x86\x80"},
    {0xfa76, "\xe5\x8b\x87"}, {0xfa77, "\xe5\x8b\xba"}, {0xfa78, "\xe5\x96\x9d"}, {0xfa79, "\xe5\x95\x95"},
    {0xfa7a, "\xe5\x96\x99"}, {0xfa7b, "\xe5\x97\xa2"}, {0xfa7c, "\xe5\xa1\x9a"}, {0xfa7d, "\xe5\xa2\xb3"},
    {0xfa7e, "\xe5\xa5\x84"}, {0xfa7f, "\xe5\xa5\x94"}, {0xfa80, "\xe5\xa9\xa2"}, {0xfa81, "\xe5\xac\xa8"},
    {0xfa82, "\xe5\xbb\x92"}, {0xfa83, "\xe5\xbb\x99"}, {0xfa84, "\xe5\xbd\xa9"}, {0xfa85, "\xe5\xbe\xad"},
    {0xfa86, "\xe6\x83\x98"}, {0xfa87, "\xe6\x85\x8e"}, {0xfa88, "\xe6\x84\x88"}, {0xfa89, "\xe6\x86\x8e"},
    {0xfa8a, "\xe6\x85\xa0"}, {0xfa8b, "\xe6\x87\xb2"}, {0xfa8c, "\xe6\x88\xb4"}, {0xfa8d, "\xe6\x8f\x84"},
    {0xfa8e, "\xe6\x90\x9c"}, {0xfa8f, "\xe6\x91\x92"}, {0xfa90, "\xe6\x95\x96"}, {0xfa91, "\xe6\x99\xb4"},
    {0xfa92, "\xe6\x9c\x97"}, {0xfa93, "\xe6\x9c\x9b"}, {0xfa94, "\xe6\x9d\x96"}, {0xfa95, "\xe6\xad\xb9"},
    {0xfa96, "\xe6\xae\xba"}, {0xfa97, "\xe6\xb5\x81"}, {0xfa98, "\xe6\xbb\x9b"}, {0xfa99, "\xe6\xbb\x8b"},
    {0xfa9a, "\xe6\xbc\xa2"}, {0xfa9b, "\xe7\x80\x9e"}, {0xfa9c, "\xe7\x85\xae"}, {0xfa9d, "\xe7\x9e\xa7"},
    {0xfa9e, "\xe7\x88\xb5"}, {0xfa9f, "\xe7\x8a\xaf"}, {0xfaa0, "\xe7\x8c\xaa"}, {0xfaa1, "\xe7\x91\xb1"},
    {0xfaa2, "\xe7\x94\x86"}, {0xfaa3, "\xe7\x94\xbb"}, {0xfaa4, "\xe7\x98\x9d"}, {0xfaa5, "\xe7\x98\x9f"},
    {0xfaa6, "\xe7\x9b\x8a"}, {0xfaa7, "\xe7\x9b\x9b"}, {0xfaa8, "\xe7\x9b\xb4"}, {0xfaa9, "\xe7\x9d\x8a"},
    {0xfaaa, "\xe7\x9d\x80"}, {0xfaab, "\xe7\xa3\x8c"}, {0xfaac, "\xe7\xaa\xb1"}, {0xfaad, "\xe7\xaf\x80"},
    {0xfaae, "\xe7\xb1\xbb"}, {0xfaaf, "\xe7\xb5\x9b"}, {0xfab0, "\xe7\xb7\xb4"}, {0xfab1, "\xe7\xbc\xbe"},
    {0xfab2, "\xe8\x80\x85"}, {0xfab3, "\xe8\x8d\x92"}, {0xfab4, "\xe8\x8f\xaf"}, {0xfab5, "\xe8\x9d\xb9"},
    {0xfab6, "\xe8\xa5\x81"}, {0xfab7, "\xe8\xa6\x86"}, {0xfab8, "\xe8\xa6\x96"}, {0xfab9, "\xe8\xaa\xbf"},
    {0xfaba, "\xe8\xab\xb8"}, {0xfabb, "\xe8\xab\x8b"}, {0xfabc, "\xe8\xac\x81"}, {0xfabd, "\xe8\xab\xbe"},
    {0xfabe, "\xe8\xab\xad"}, {0xfabf, "\xe8\xac\xb9"}, {0xfac0, "\xe8\xae\x8a"}, {0xfac1, "\xe8\xb4\x88"},
    {0xfac2, "\xe8\xbc\xb8"}, {0xfac3, "\xe9\x81\xb2"}, {0xfac4, "\xe9\x86\x99"}, {0xfac5, "\xe9\x89\xb6"},
    {0xfac6, "\xe9\x99\xbc"}, {0xfac7, "\xe9\x9b\xa3"}, {0xfac8, "\xe9\x9d\x96"}, {0xfac9, "\xe9\x9f\x9b"},
    {0xfaca, "\xe9\x9f\xbf"}, {0xfacb, "\xe9\xa0\x8b"}, {0xfacc, "\xe9\xa0\xbb"}, {0xfacd, "\xe9\xac\x92"},
    {0xface, "\xe9\xbe\x9c"}, {0xfacf, "\xf0\xa2\xa1\x8a"}, {0xfad0, "\xf0\xa2\xa1\x84"}, {0xfad1, "\xf0\xa3\x8f\x95"},
    {0xfad2, "\xe3\xae\x9d"}, {0xfad3, "\xe4\x80\x98"}, {0xfad4, "\xe4\x80\xb9"}, {0xfad5, "\xf0\xa5\x89\x89"},
    {0xfad6, "\xf0\xa5\xb3\x90"}, {0xfad7, "\xf0\xa7\xbb\x93"}, {0xfad8, "\xe9\xbd\x83"}, {0xfad9, "\xe9\xbe\x8e"},
    {0xfb1d, "\xd7\x99\xd6\xb4"}, {0xfb1f, "\xd7\xb2\xd6\xb7"}, {0xfb2a, "\xd7\xa9\xd7\x81"}, {0xfb2b, "\xd7\xa9\xd7\x82"},
    {0xfb2c, "\xd7\xa9\xd6\xbc\xd7\x81"}, {0xfb2d, "\xd7\xa9\xd6\xbc\xd7\x82"}, {0xfb2e, "\xd7\x90\xd6\xb7"}, {0xfb2f, "\xd7\x90\xd6\xb8"},
    {0xfb30, "\xd7\x90\xd6\xbc"}, {0xfb31, "\xd7\x91\xd6\xbc"}, {0xfb32, "\xd7\x92\xd6\xbc"}, {0xfb33, "\xd7\x93\xd6\xbc"},
    {0xfb34, "\xd7\x94\xd6\xbc"}, {0xfb35, "\xd7\x95\xd6\xbc"}, {0xfb36, "\xd7\x96\xd6\xbc"}, {0xfb38, "\xd7\x98\xd6\xbc"},
    {0xfb39, "\xd7\x99\xd6\xbc"}, {0xfb3a, "\xd7\x9a\xd6\xbc"}, {0xfb3b, "\xd7\x9b\xd6\xbc"}, {0xfb3c, "\xd7\x9c\xd6\xbc"},
    {0xfb3e, "\xd7\x9e\xd6\xbc"}, {0xfb40, "\xd7\xa0\xd6\xbc"}, {0xfb41, "\xd7\xa1\xd6\xbc"}, {0xfb43, "\xd7\xa3\xd6\xbc"},
    {0xfb44, "\xd7\xa4\xd6\xbc"}, {0xfb46, "\xd7\xa6\xd6\xbc"}, {0xfb47, "\xd7\xa7\xd6\xbc"}, {0xfb48, "\xd7\xa8\xd6\xbc"},
    {0xfb49, "\xd7\xa9\xd6\xbc"}, {0xfb4a, "\xd7\xaa\xd6\xbc"}, {0xfb4b, "\xd7\x95\xd6\xb9"}, {0xfb4c, "\xd7\x91\xd6\xbf"},
    {0xfb4d, "\xd7\x9b\xd6\xbf"}, {0xfb4e, "\xd7\xa4\xd6\xbf"}, {0xff21, "\xef\xbd\x81"}, {0xff22, "\xef\xbd\x82"},
    {0xff23, "\xef\xbd\x83"}, {0xff24, "\xef\xbd\x84"}, {0xff25, "\xef\xbd\x85"}, {0xff26, "\xef\xbd\x86"},
    {0xff27, "\xef\xbd\x87"}, {0xff28, "\xef\xbd\x88"}, {0xff29, "\xef\xbd\x89"}, {0xff2a, "\xef\xbd\x8a"},
    {0xff2b, "\xef\xbd\x8b"}, {0xff2c, "\xef\xbd\x8c"}, {0xff2d, "\xef\xbd\x8d"}, {0xff2e, "\xef\xbd\x8e"},
    {0xff2f, "\xef\xbd\x8f"}, {0xff30, "\xef\xbd\x90"}, {0xff31, "\xef\xbd\x91"}, {0xff32, "\xef\xbd\x92"},
    {0xff33, "\xef\xbd\x93"}, {0xff34, "\xef\xbd\x94"}, {0xff35, "\xef\xbd\x95"}, {0xff36, "\xef\xbd\x96"},
    {0xff37, "\xef\xbd\x97"}, {0xff38, "\xef\xbd\x98"}, {0xff39, "\xef\xbd\x99"}, {0xff3a, "\xef\xbd\x9a"},
    {0x10400, "\xf0\x90\x90\xa8"}, {0x10401, "\xf0\x90\x90\xa9"}, {0x10402, "\xf0\x90\x90\xaa"}, {0x10403, "\xf0\x90\x90\xab"},
    {0x10404, "\xf0\x90\x90\xac"}, {0x10405, "\xf0\x90\x90\xad"}, {0x10406, "\xf0\x90\x90\xae"}, {0x10407, "\xf0\x90\x90\xaf"},
    {0x10408, "\xf0\x90\x90\xb0"}, {0x10409, "\xf0\x90\x90\xb1"}, {0x1040a, "\xf0\x90\x90\xb2"}, {0x1040b, "\xf0\x90\x90\xb3"},
    {0x1040c, "\xf0\x90\x90\xb4"}, {0x1040d, "\xf0\x90\x90\xb5"}, {0x1040e, "\xf0\x90\x90\xb6"}, {0x1040f, "\xf0\x90\x90\xb7"},
    {0x10410, "\xf0\x90\x90\xb8"}, {0x10411, "\xf0\x90\x90\xb9"}, {0x10412, "\xf0\x90\x90\xba"}, {0x10413, "\xf0\x90\x90\xbb"},
    {0x10414, "\xf0\x90\x90\xbc"}, {0x10415, "\xf0\x90\x90\xbd"}, {0x10416, "\xf0\x90\x90\xbe"}, {0x10417, "\xf0\x90\x90\xbf"},
    {0x10418, "\xf0\x90\x91\x80"}, {0x10419, "\xf0\x90\x91\x81"}, {0x1041a, "\xf0\x90\x91\x82"}, {0x1041b, "\xf0\x90\x91\x83"},
    {0x1041c, "\xf0\x90\x91\x84"}, {0x1041d, "\xf0\x90\x91\x85"}, {0x1041e, "\xf0\x90\x91\x86"}, {0x1041f, "\xf0\x90\x91\x87"},
    {0x10420, "\xf0\x90\x91\x88"}, {0x10421, "\xf0\x90\x91\x89"}, {0x10422, "\xf0\x90\x91\x8a"}, {0x10423, "\xf0\x90\x91\x8b"},
    {0x10424, "\xf0\x90\x91\x8c"}, {0x10425, "\xf0\x90\x91\x8d"}, {0x10426, "\xf0\x90\x91\x8e"}, {0x10427, "\xf0\x90\x91\x8f"},
    {0x104b0, "\xf0\x90\x93\x98"}, {0x104b1, "\xf0\x90\x93\x99"}, {0x104b2, "\xf0\x90\x93\x9a"}, {0x104b3, "\xf0\x90\x93\x9b"},
    {0x104b4, "\xf0\x90\x93\x9c"}, {0x104b5, "\xf0\x90\x93\x9d"}, {0x104b6, "\xf0\x90\x93\x9e"}, {0x104b7, "\xf0\x90\x93\x9f"},
    {0x104b8, "\xf0\x90\x93\xa0"}, {0x104b9, "\xf0\x90\x93\xa1"}, {0x104ba, "\xf0\x90\x93\xa2"}, {0x104bb, "\xf0\x90\x93\xa3"},
    {0x104bc, "\xf0\x90\x93\xa4"}, {0x104bd, "\xf0\x90\x93\xa5"}, {0x104be, "\xf0\x90\x93\xa6"}, {0x104bf, "\xf0\x90\x93\xa7"},
    {0x104c0, "\xf0\x90\x93\xa8"}, {0x104c1, "\xf0\x90\x93\xa9"}, {0x104c2, "\xf0\x90\x93\xaa"}, {0x104c3, "\xf0\x90\x93\xab"},
    {0x104c4, "\xf0\x90\x93\xac"}, {0x104c5, "\xf0\x90\x93\xad"}, {0x104c6, "\xf0\x90\x93\xae"}, {0x104c7, "\xf0\x90\x93\xaf"},
    {0x104c8, "\xf0\x90\x93\xb0"}, {0x104c9, "\xf0\x90\x93\xb1"}, {0x104ca, "\xf0\x90\x93\xb2"}, {0x104cb, "\xf0\x90\x93\xb3"},
    {0x104cc, "\xf0\x90\x93\xb4"}, {0x104cd, "\xf0\x90\x93\xb5"}, {0x104ce, "\xf0\x90\x93\xb6"}, {0x104cf, "\xf0\x90\x93\xb7"},
    {0x104d0, "\xf0\x90\x93\xb8"}, {0x104d1, "\xf0\x90\x93\xb9"}, {0x104d2, "\xf0\x90\x93\xba"}, {0x104d3, "\xf0\x90\x93\xbb"},
    {0x10570, "\xf0\x90\x96\x97"}, {0x10571, "\xf0\x90\x96\x98"}, {0x10572, "\xf0\x90\x96\x99"}, {0x10573, "\xf0\x90\x96\x9a"},
    {0x10574, "\xf0\x90\x96\x9b"}, {0x10575, "\xf0\x90\x96\x9c"}, {0x10576, "\xf0\x90\x96\x9d"}, {0x10577, "\xf0\x90\x96\x9e"},
    {0x10578, "\xf0\x90\x96\x9f"}, {0x10579, "\xf0\x90\x96\xa0"}, {0x1057a, "\xf0\x90\x96\xa1"}, {0x1057c, "\xf0\x90\x96\xa3"},
    {0x1057d, "\xf0\x90\x96\xa4"}, {0x1057e, "\xf0\x90\x96\xa5"}, {0x1057f, "\xf0\x90\x96\xa6"}, {0x10580, "\xf0\x90\x96\xa7"},
    {0x10581, "\xf0\x90\x96\xa8"}, {0x10582, "\xf0\x90\x96\xa9"}, {0x10583, "\xf0\x90\x96\xaa"}, {0x10584, "\xf0\x90\x96\xab"},
    {0x10585, "\xf0\x90\x96\xac"}, {0x10586, "\xf0\x90\x96\xad"}, {0x10587, "\xf0\x90\x96\xae"}, {0x10588, "\xf0\x90\x96\xaf"},
    {0x10589, "\xf0\x90\x96\xb0"}, {0x1058a, "\xf0\x90\x96\xb1"}, {0x1058c, "\xf0\x90\x96\xb3"}, {0x1058d, "\xf0\x90\x96\xb4"},
    {0x1058e, "\xf0\x90\x96\xb5"}, {0x1058f, "\xf0\x90\x96\xb6"}, {0x10590, "\xf0\x90\x96\xb7"}, {0x10591, "\xf0\x90\x96\xb8"},
    {0x10592, "\xf0\x90\x96\xb9"}, {0x10594, "\xf0\x90\x96\xbb"}, {0x10595, "\xf0\x90\x96\xbc"}, {0x105c9, "\xf0\x90\x97\x92"},
    {0x105e4, "\xf0\x90\x97\x9a"}, {0x10c80, "\xf0\x90\xb3\x80"}, {0x10c81, "\xf0\x90\xb3\x81"}, {0x10c82, "\xf0\x90\xb3\x82"},
    {0x10c83, "\xf0\x90\xb3\x83"}, {0x10c84, "\xf0\x90\xb3\x84"}, {0x10c85, "\xf0\x90\xb3\x85"}, {0x10c86, "\xf0\x90\xb3\x86"},
    {0x10c87, "\xf0\x90\xb3\x87"}, {0x10c88, "\xf0\x90\xb3\x88"}, {0x10c89, "\xf0\x90\xb3\x89"}, {0x10c8a, "\xf0\x90\xb3\x8a"},
    {0x10c8b, "\xf0\x90\xb3\x8b"}, {0x10c8c, "\xf0\x90\xb3\x8c"}, {0x10c8d, "\xf0\x90\xb3\x8d"}, {0x10c8e, "\xf0\x90\xb3\x8e"},
    {0x10c8f, "\xf0\x90\xb3\x8f"}, {0x10c90, "\xf0\x90\xb3\x90"}, {0x10c91, "\xf0\x90\xb3\x91"}, {0x10c92, "\xf0\x90\xb3\x92"},
    {0x10c93, "\xf0\x90\xb3\x93"}, {0x10c94, "\xf0\x90\xb3\x94"}, {0x10c95, "\xf0\x90\xb3\x95"}, {0x10c96, "\xf0\x90\xb3\x96"},
    {0x10c97, "\xf0\x90\xb3\x97"}, {0x10c98, "\xf0\x90\xb3\x98"}, {0x10c99, "\xf0\x90\xb3\x99"}, {0x10c9a, "\xf0\x90\xb3\x9a"},
    {0x10c9b, "\xf0\x90\xb3\x9b"}, {0x10c9c, "\xf0\x90\xb3\x9c"}, {0x10c9d, "\xf0\x90\xb3\x9d"}, {0x10c9e, "\xf0\x90\xb3\x9e"},
    {0x10c9f, "\xf0\x90\xb3\x9f"}, {0x10ca0, "\xf0\x90\xb3\xa0"}, {0x10ca1, "\xf0\x90\xb3\xa1"}, {0x10ca2, "\xf0\x90\xb3\xa2"},
    {0x10ca3, "\xf0\x90\xb3\xa3"}, {0x10ca4, "\xf0\x90\xb3\xa4"}, {0x10ca5, "\xf0\x90\xb3\xa5"}, {0x10ca6, "\xf0\x90\xb3\xa6"},
    {0x10ca7, "\xf0\x90\xb3\xa7"}, {0x10ca8, "\xf0\x90\xb3\xa8"}, {0x10ca9, "\xf0\x90\xb3\xa9"}, {0x10caa, "\xf0\x90\xb3\xaa"},
    {0x10cab, "\xf0\x90\xb3\xab"}, {0x10cac, "\xf0\x90\xb3\xac"}, {0x10cad, "\xf0\x90\xb3\xad"}, {0x10cae, "\xf0\x90\xb3\xae"},
    {0x10caf, "\xf0\x90\xb3\xaf"}, {0x10cb0, "\xf0\x90\xb3\xb0"}, {0x10cb1, "\xf0\x90\xb3\xb1"}, {0x10cb2, "\xf0\x90\xb3\xb2"},
    {0x10d50, "\xf0\x90\xb5\xb0"}, {0x10d51, "\xf0\x90\xb5\xb1"}, {0x10d52, "\xf0\x90\xb5\xb2"}, {0x10d53, "\xf0\x90\xb5\xb3"},
    {0x10d54, "\xf0\x90\xb5\xb4"}, {0x10d55, "\xf0\x90\xb5\xb5"}, {0x10d56, "\xf0\x90\xb5\xb6"}, {0x10d57, "\xf0\x90\xb5\xb7"},
    {0x10d58, "\xf0\x90\xb5\xb8"}, {0x10d59, "\xf0\x90\xb5\xb9"}, {0x10d5a, "\xf0\x90\xb5\xba"}, {0x10d5b, "\xf0\x90\xb5\xbb"},
    {0x10d5c, "\xf0\x90\xb5\xbc"}, {0x10d5d, "\xf0\x90\xb5\xbd"}, {0x10d5e, "\xf0\x90\xb5\xbe"}, {0x10d5f, "\xf0\x90\xb5\xbf"},
    {0x10d60, "\xf0\x90\xb6\x80"}, {0x10d61, "\xf0\x90\xb6\x81"}, {0x10d62, "\xf0\x90\xb6\x82"}, {0x10d63, "\xf0\x90\xb6\x83"},
    {0x10d64, "\xf0\x90\xb6\x84"}, {0x10d65, "\xf0\x90\xb6\x85"}, {0x1109a, "\xf0\x91\x82\x99\xf0\x91\x82\xba"}, {0x1109c, "\xf0\x91\x82\x9b\xf0\x91\x82\xba"},
    {0x110ab, "\xf0\x91\x82\xa5\xf0\x91\x82\xba"}, {0x1112e, "\xf0\x91\x84\xb1\xf0\x91\x84\xa7"}, {0x1112f, "\xf0\x91\x84\xb2\xf0\x91\x84\xa7"}, {0x1134b, "\xf0\x91\x8d\x87\xf0\x91\x8c\xbe"},
    {0x1134c, "\xf0\x91\x8d\x87\xf0\x91\x8d\x97"}, {0x11383, "\xf0\x91\x8e\x82\xf0\x91\x8f\x89"}, {0x11385, "\xf0\x91\x8e\x84\xf0\x91\x8e\xbb"}, {0x1138e, "\xf0\x91\x8e\x8b\xf0\x91\x8f\x82"},
    {0x11391, "\xf0\x91\x8e\x90\xf0\x91\x8f\x89"}, {0x113c5, "\xf0\x91\x8f\x82\xf0\x91\x8f\x82"}, {0x113c7, "\xf0\x91\x8f\x82\xf0\x91\x8e\xb8"}, {0x113c8, "\xf0\x91\x8f\x82\xf0\x91\x8f\x89"},
    {0x114bb, "\xf0\x91\x92\xb9\xf0\x91\x92\xba"}, {0x114bc, "\xf0\x91\x92\xb9\xf0\x91\x92\xb0"}, {0x114be, "\xf0\x91\x92\xb9\xf0\x91\x92\xbd"}, {0x115ba, "\xf0\x91\x96\xb8\xf0\x91\x96\xaf"},
    {0x115bb, "\xf0\x91\x96\xb9\xf0\x91\x96\xaf"}, {0x118a0, "\xf0\x91\xa3\x80"}, {0x118a1, "\xf0\x91\xa3\x81"}, {0x118a2, "\xf0\x91\xa3\x82"},
    {0x118a3, "\xf0\x91\xa3\x83"}, {0x118a4, "\xf0\x91\xa3\x84"}, {0x118a5, "\xf0\x91\xa3\x85"}, {0x118a6, "\xf0\x91\xa3\x86"},
    {0x118a7, "\xf0\x91\xa3\x87"}, {0x118a8, "\xf0\x91\xa3\x88"}, {0x118a9, "\xf0\x91\xa3\x89"}, {0x118aa, "\xf0\x91\xa3\x8a"},
    {0x118ab, "\xf0\x91\xa3\x8b"}, {0x118ac, "\xf0\x91\xa3\x8c"}, {0x118ad, "\xf0\x91\xa3\x8d"}, {0x118ae, "\xf0\x91\xa3\x8e"},
    {0x118af, "\xf0\x91\xa3\x8f"}, {0x118b0, "\xf0\x91\xa3\x90"}, {0x118b1, "\xf0\x91\xa3\x91"}, {0x118b2, "\xf0\x91\xa3\x92"},
    {0x118b3, "\xf0\x91\xa3\x93"}, {0x118b4, "\xf0\x91\xa3\x94"}, {0x118b5, "\xf0\x91\xa3\x95"}, {0x118b6, "\xf0\x91\xa3\x96"},
    {0x118b7, "\xf0\x91\xa3\x97"}, {0x118b8, "\xf0\x91\xa3\x98"}, {0x118b9, "\xf0\x91\xa3\x99"}, {0x118ba, "\xf0\x91\xa3\x9a"},
    {0x118bb, "\xf0\x91\xa3\x9b"}, {0x118bc, "\xf0\x91\xa3\x9c"}, {0x118bd, "\xf0\x91\xa3\x9d"}, {0x118be, "\xf0\x91\xa3\x9e"},
    {0x118bf, "\xf0\x91\xa3\x9f"}, {0x11938, "\xf0\x91\xa4\xb5\xf0\x91\xa4\xb0"}, {0x16121, "\xf0\x96\x84\x9e\xf0\x96\x84\x9e"}, {0x16122, "\xf0\x96\x84\x9e\xf0\x96\x84\xa9"},
    {0x16123, "\xf0\x96\x84\x9e\xf0\x96\x84\x9f"}, {0x16124, "\xf0\x96\x84\xa9\xf0\x96\x84\x9f"}, {0x16125, "\xf0\x96\x84\x9e\xf0\x96\x84\xa0"}, {0x16126, "\xf0\x96\x84\x9e\xf0\x96\x84\x9e\xf0\x96\x84\x9f"},
    {0x16127, "\xf0\x96\x84\x9e\xf0\x96\x84\xa9\xf0\x96\x84\x9f"}, {0x16128, "\xf0\x96\x84\x9e\xf0\x96\x84\x9e\xf0\x96\x84\xa0"}, {0x16d68, "\xf0\x96\xb5\xa7\xf0\x96\xb5\xa7"}, {0x16d69, "\xf0\x96\xb5\xa3\xf0\x96\xb5\xa7"},
    {0x16d6a, "\xf0\x96\xb5\xa3\xf0\x96\xb5\xa7\xf0\x96\xb5\xa7"}, {0x16e40, "\xf0\x96\xb9\xa0"}, {0x16e41, "\xf0\x96\xb9\xa1"}, {0x16e42, "\xf0\x96\xb9\xa2"},
    {0x16e43, "\xf0\x96\xb9\xa3"}, {0x16e44, "\xf0\x96\xb9\xa4"}, {0x16e45, "\xf0\x96\xb9\xa5"}, {0x16e46, "\xf0\x96\xb9\xa6"},
    {0x16e47, "\xf0\x96\xb9\xa7"}, {0x16e48, "\xf0\x96\xb9\xa8"}, {0x16e49, "\xf0\x96\xb9\xa9"}, {0x16e4a, "\xf0\x96\xb9\xaa"},
    {0x16e4b, "\xf0\x96\xb9\xab"}, {0x16e4c, "\xf0\x96\xb9\xac"}, {0x16e4d, "\xf0\x96\xb9\xad"}, {0x16e4e, "\xf0\x96\xb9\xae"},
    {0x16e4f, "\xf0\x96\xb9\xaf"}, {0x16e50, "\xf0\x96\xb9\xb0"}, {0x16e51, "\xf0\x96\xb9\xb1"}, {0x16e52, "\xf0\x96\xb9\xb2"},
    {0x16e53, "\xf0\x96\xb9\xb3"}, {0x16e54, "\xf0\x96\xb9\xb4"}, {0x16e55, "\xf0\x96\xb9\xb5"}, {0x16e56, "\xf0\x96\xb9\xb6"},
    {0x16e57, "\xf0\x96\xb9\xb7"}, {0x16e58, "\xf0\x96\xb9\xb8"}, {0x16e59, "\xf0\x96\xb9\xb9"}, {0x16e5a, "\xf0\x96\xb9\xba"},
    {0x16e5b, "\xf0\x96\xb9\xbb"}, {0x16e5c, "\xf0\x96\xb9\xbc"}, {0x16e5d, "\xf0\x96\xb9\xbd"}, {0x16e5e, "\xf0\x96\xb9\xbe"},
    {0x16e5f, "\xf0\x96\xb9\xbf"}, {0x16ea0, "\xf0\x96\xba\xbb"}, {0x16ea1, "\xf0\x96\xba\xbc"}, {0x16ea2, "\xf0\x96\xba\xbd"},
    {0x16ea3, "\xf0\x96\xba\xbe"}, {0x16ea4, "\xf0\x96\xba\xbf"}, {0x16ea5, "\xf0\x96\xbb\x80"}, {0x16ea6, "\xf0\x96\xbb\x81"},
    {0x16ea7, "\xf0\x96\xbb\x82"}, {0x16ea8, "\xf0\x96\xbb\x83"}, {0x16ea9, "\xf0\x96\xbb\x84"}, {0x16eaa, "\xf0\x96\xbb\x85"},
    {0x16eab, "\xf0\x96\xbb\x86"}, {0x16eac, "\xf0\x96\xbb\x87"}, {0x16ead, "\xf0\x96\xbb\x88"}, {0x16eae, "\xf0\x96\xbb\x89"},
    {0x16eaf, "\xf0\x96\xbb\x8a"}, {0x16eb0, "\xf0\x96\xbb\x8b"}, {0x16eb1, "\xf0\x96\xbb\x8c"}, {0x16eb2, "\xf0\x96\xbb\x8d"},
    {0x16eb3, "\xf0\x96\xbb\x8e"}, {0x16eb4, "\xf0\x96\xbb\x8f"}, {0x16eb5, "\xf0\x96\xbb\x90"}, {0x16eb6, "\xf0\x96\xbb\x91"},
    {0x16eb7, "\xf0\x96\xbb\x92"}, {0x16eb8, "\xf0\x96\xbb\x93"}, {0x1d15e, "\xf0\x9d\x85\x97\xf0\x9d\x85\xa5"}, {0x1d15f, "\xf0\x9d\x85\x98\xf0\x9d\x85\xa5"},
    {0x1d160, "\xf0\x9d\x85\x98\xf0\x9d\x85\xa5\xf0\x9d\x85\xae"}, {0x1d161, "\xf0\x9d\x85\x98\xf0\x9d\x85\xa5\xf0\x9d\x85\xaf"}, {0x1d162, "\xf0\x9d\x85\x98\xf0\x9d\x85\xa5\xf0\x9d\x85\xb0"}, {0x1d163, "\xf0\x9d\x85\x98\xf0\x9d\x85\xa5\xf0\x9d\x85\xb1"},
    {0x1d164, "\xf0\x9d\x85\x98\xf0\x9d\x85\xa5\xf0\x9d\x85\xb2"}, {0x1d1bb, "\xf0\x9d\x86\xb9\xf0\x9d\x85\xa5"}, {0x1d1bc, "\xf0\x9d\x86\xba\xf0\x9d\x85\xa5"}, {0x1d1bd, "\xf0\x9d\x86\xb9\xf0\x9d\x85\xa5\xf0\x9d\x85\xae"},
    {0x1d1be, "\xf0\x9d\x86\xba\xf0\x9d\x85\xa5\xf0\x9d\x85\xae"}, {0x1d1bf, "\xf0\x9d\x86\xb9\xf0\x9d\x85\xa5\xf0\x9d\x85\xaf"}, {0x1d1c0, "\xf0\x9d\x86\xba\xf0\x9d\x85\xa5\xf0\x9d\x85\xaf"}, {0x1e900, "\xf0\x9e\xa4\xa2"},
    {0x1e901, "\xf0\x9e\xa4\xa3"}, {0x1e902, "\xf0\x9e\xa4\xa4"}, {0x1e903, "\xf0\x9e\xa4\xa5"}, {0x1e904, "\xf0\x9e\xa4\xa6"},
    {0x1e905, "\xf0\x9e\xa4\xa7"}, {0x1e906, "\xf0\x9e\xa4\xa8"}, {0x1e907, "\xf0\x9e\xa4\xa9"}, {0x1e908, "\xf0\x9e\xa4\xaa"},
    {0x1e909, "\xf0\x9e\xa4\xab"}, {0x1e90a, "\xf0\x9e\xa4\xac"}, {0x1e90b, "\xf0\x9e\xa4\xad"}, {0x1e90c, "\xf0\x9e\xa4\xae"},
    {0x1e90d, "\xf0\x9e\xa4\xaf"}, {0x1e90e, "\xf0\x9e\xa4\xb0"}, {0x1e90f, "\xf0\x9e\xa4\xb1"}, {0x1e910, "\xf0\x9e\xa4\xb2"},
    {0x1e911, "\xf0\x9e\xa4\xb3"}, {0x1e912, "\xf0\x9e\xa4\xb4"}, {0x1e913, "\xf0\x9e\xa4\xb5"}, {0x1e914, "\xf0\x9e\xa4\xb6"},
    {0x1e915, "\xf0\x9e\xa4\xb7"}, {0x1e916, "\xf0\x9e\xa4\xb8"}, {0x1e917, "\xf0\x9e\xa4\xb9"}, {0x1e918, "\xf0\x9e\xa4\xba"},
    {0x1e919, "\xf0\x9e\xa4\xbb"}, {0x1e91a, "\xf0\x9e\xa4\xbc"}, {0x1e91b, "\xf0\x9e\xa4\xbd"}, {0x1e91c, "\xf0\x9e\xa4\xbe"},
    {0x1e91d, "\xf0\x9e\xa4\xbf"}, {0x1e91e, "\xf0\x9e\xa5\x80"}, {0x1e91f, "\xf0\x9e\xa5\x81"}, {0x1e920, "\xf0\x9e\xa5\x82"},
    {0x1e921, "\xf0\x9e\xa5\x83"}, {0x2f800, "\xe4\xb8\xbd"}, {0x2f801, "\xe4\xb8\xb8"}, {0x2f802, "\xe4\xb9\x81"},
    {0x2f803, "\xf0\xa0\x84\xa2"}, {0x2f804, "\xe4\xbd\xa0"}, {0x2f805, "\xe4\xbe\xae"}, {0x2f806, "\xe4\xbe\xbb"},
    {0x2f807, "\xe5\x80\x82"}, {0x2f808, "\xe5\x81\xba"}, {0x2f809, "\xe5\x82\x99"}, {0x2f80a, "\xe5\x83\xa7"},
    {0x2f80b, "\xe5\x83\x8f"}, {0x2f80c, "\xe3\x92\x9e"}, {0x2f80d, "\xf0\xa0\x98\xba"}, {0x2f80e, "\xe5\x85\x8d"},
    {0x2f80f, "\xe5\x85\x94"}, {0x2f810, "\xe5\x85\xa4"}, {0x2f811, "\xe5\x85\xb7"}, {0x2f812, "\xf0\xa0\x94\x9c"},
    {0x2f813, "\xe3\x92\xb9"}, {0x2f814, "\xe5\x85\xa7"}, {0x2f815, "\xe5\x86\x8d"}, {0x2f816, "\xf0\xa0\x95\x8b"},
    {0x2f817, "\xe5\x86\x97"}, {0x2f818, "\xe5\x86\xa4"}, {0x2f819, "\xe4\xbb\x8c"}, {0x2f81a, "\xe5\x86\xac"},
    {0x2f81b, "\xe5\x86\xb5"}, {0x2f81c, "\xf0\xa9\x87\x9f"}, {0x2f81d, "\xe5\x87\xb5"}, {0x2f81e, "\xe5\x88\x83"},
    {0x2f81f, "\xe3\x93\x9f"}, {0x2f820, "\xe5\x88\xbb"}, {0x2f821, "\xe5\x89\x86"}, {0x2f822, "\xe5\x89\xb2"},
    {0x2f823, "\xe5\x89\xb7"}, {0x2f824, "\xe3\x94\x95"}, {0x2f825, "\xe5\x8b\x87"}, {0x2f826, "\xe5\x8b\x89"},
    {0x2f827, "\xe5\x8b\xa4"}, {0x2f828, "\xe5\x8b\xba"}, {0x2f829, "\xe5\x8c\x85"}, {0x2f82a, "\xe5\x8c\x86"},
    {0x2f82b, "\xe5\x8c\x97"}, {0x2f82c, "\xe5\x8d\x89"}, {0x2f82d, "\xe5\x8d\x91"}, {0x2f82e, "\xe5\x8d\x9a"},
    {0x2f82f, "\xe5\x8d\xb3"}, {0x2f830, "\xe5\x8d\xbd"}, {0x2f831, "\xe5\x8d\xbf"}, {0x2f832, "\xe5\x8d\xbf"},
    {0x2f833, "\xe5\x8d\xbf"}, {0x2f834, "\xf0\xa0\xa8\xac"}, {0x2f835, "\xe7\x81\xb0"}, {0x2f836, "\xe5\x8f\x8a"},
    {0x2f837, "\xe5\x8f\x9f"}, {0x2f838, "\xf0\xa0\xad\xa3"}, {0x2f839, "\xe5\x8f\xab"}, {0x2f83a, "\xe5\x8f\xb1"},
    {0x2f83b, "\xe5\x90\x86"}, {0x2f83c, "\xe5\x92\x9e"}, {0x2f83d, "\xe5\x90\xb8"}, {0x2f83e, "\xe5\x91\x88"},
    {0x2f83f, "\xe5\x91\xa8"}, {0x2f840, "\xe5\x92\xa2"}, {0x2f841, "\xe5\x93\xb6"}, {0x2f842, "\xe5\x94\x90"},
    {0x2f843, "\xe5\x95\x93"}, {0x2f844, "\xe5\x95\xa3"}, {0x2f845, "\xe5\x96\x84"}, {0x2f846, "\xe5\x96\x84"},
    {0x2f847, "\xe5\x96\x99"}, {0x2f848, "\xe5\x96\xab"}, {0x2f849, "\xe5\x96\xb3"}, {0x2f84a, "\xe5\x97\x82"},
    {0x2f84b, "\xe5\x9c\x96"}, {0x2f84c, "\xe5\x98\x86"}, {0x2f84d, "\xe5\x9c\x97"}, {0x2f84e, "\xe5\x99\x91"},
    {0x2f84f, "\xe5\x99\xb4"}, {0x2f850, "\xe5\x88\x87"}, {0x2f851, "\xe5\xa3\xae"}, {0x2f852, "\xe5\x9f\x8e"},
    {0x2f853, "\xe5\x9f\xb4"}, {0x2f854, "\xe5\xa0\x8d"}, {0x2f855, "\xe5\x9e\x8b"}, {0x2f856, "\xe5\xa0\xb2"},
    {0x2f857, "\xe5\xa0\xb1"}, {0x2f858, "\xe5\xa2\xac"}, {0x2f859, "\xf0\xa1\x93\xa4"}, {0x2f85a, "\xe5\xa3\xb2"},
    {0x2f85b, "\xe5\xa3\xb7"}, {0x2f85c, "\xe5\xa4\x86"}, {0x2f85d, "\xe5\xa4\x9a"}, {0x2f85e, "\xe5\xa4\xa2"},
    {0x2f85f, "\xe5\xa5\xa2"}, {0x2f860, "\xf0\xa1\x9a\xa8"}, {0x2f861, "\xf0\xa1\x9b\xaa"}, {0x2f862, "\xe5\xa7\xac"},
    {0x2f863, "\xe5\xa8\x9b"}, {0x2f864, "\xe5\xa8\xa7"}, {0x2f865, "\xe5\xa7\x98"}, {0x2f866, "\xe5\xa9\xa6"},
    {0x2f867, "\xe3\x9b\xae"}, {0x2f868, "\xe3\x9b\xbc"}, {0x2f869, "\xe5\xac\x88"}, {0x2f86a, "\xe5\xac\xbe"},
    {0x2f86b, "\xe5\xac\xbe"}, {0x2f86c, "\xf0\xa1\xa7\x88"}, {0x2f86d, "\xe5\xaf\x83"}, {0x2f86e, "\xe5\xaf\x98"},
    {0x2f86f, "\xe5\xaf\xa7"}, {0x2f870, "\xe5\xaf\xb3"}, {0x2f871, "\xf0\xa1\xac\x98"}, {0x2f872, "\xe5\xaf\xbf"},
    {0x2f873, "\xe5\xb0\x86"}, {0x2f874, "\xe5\xbd\x93"}, {0x2f875, "\xe5\xb0\xa2"}, {0x2f876, "\xe3\x9e\x81"},
    {0x2f877, "\xe5\xb1\xa0"}, {0x2f878, "\xe5\xb1\xae"}, {0x2f879, "\xe5\xb3\x80"}, {0x2f87a, "\xe5\xb2\x8d"},
    {0x2f87b, "\xf0\xa1\xb7\xa4"}, {0x2f87c, "\xe5\xb5\x83"}, {0x2f87d, "\xf0\xa1\xb7\xa6"}, {0x2f87e, "\xe5\xb5\xae"},
    {0x2f87f, "\xe5\xb5\xab"}, {0x2f880, "\xe5\xb5\xbc"}, {0x2f881, "\xe5\xb7\xa1"}, {0x2f882, "\xe5\xb7\xa2"},
    {0x2f883, "\xe3\xa0\xaf"}, {0x2f884, "\xe5\xb7\xbd"}, {0x2f885, "\xe5\xb8\xa8"}, {0x2f886, "\xe5\xb8\xbd"},
    {0x2f887, "\xe5\xb9\xa9"}, {0x2f888, "\xe3\xa1\xa2"}, {0x2f889, "\xf0\xa2\x86\x83"}, {0x2f88a, "\xe3\xa1\xbc"},
    {0x2f88b, "\xe5\xba\xb0"}, {0x2f88c, "\xe5\xba\xb3"}, {0x2f88d, "\xe5\xba\xb6"}, {0x2f88e, "\xe5\xbb\x8a"},
    {0x2f88f, "\xf0\xaa\x8e\x92"}, {0x2f890, "\xe5\xbb\xbe"}, {0x2f891, "\xf0\xa2\x8c\xb1"}, {0x2f892, "\xf0\xa2\x8c\xb1"},
    {0x2f893, "\xe8\x88\x81"}, {0x2f894, "\xe5\xbc\xa2"}, {0x2f895, "\xe5\xbc\xa2"}, {0x2f896, "\xe3\xa3\x87"},
    {0x2f897, "\xf0\xa3\x8a\xb8"}, {0x2f898, "\xf0\xa6\x87\x9a"}, {0x2f899, "\xe5\xbd\xa2"}, {0x2f89a, "\xe5\xbd\xab"},
    {0x2f89b, "\xe3\xa3\xa3"}, {0x2f89c, "\xe5\xbe\x9a"}, {0x2f89d, "\xe5\xbf\x8d"}, {0x2f89e, "\xe5\xbf\x97"},
    {0x2f89f, "\xe5\xbf\xb9"}, {0x2f8a0, "\xe6\x82\x81"}, {0x2f8a1, "\xe3\xa4\xba"}, {0x2f8a2, "\xe3\xa4\x9c"},
    {0x2f8a3, "\xe6\x82\x94"}, {0x2f8a4, "\xf0\xa2\x9b\x94"}, {0x2f8a5, "\xe6\x83\x87"}, {0x2f8a6, "\xe6\x85\x88"},
    {0x2f8a7, "\xe6\x85\x8c"}, {0x2f8a8, "\xe6\x85\x8e"}, {0x2f8a9, "\xe6\x85\x8c"}, {0x2f8aa, "\xe6\x85\xba"},
    {0x2f8ab, "\xe6\x86\x8e"}, {0x2f8ac, "\xe6\x86\xb2"}, {0x2f8ad, "\xe6\x86\xa4"}, {0x2f8ae, "\xe6\x86\xaf"},
    {0x2f8af, "\xe6\x87\x9e"}, {0x2f8b0, "\xe6\x87\xb2"}, {0x2f8b1, "\xe6\x87\xb6"}, {0x2f8b2, "\xe6\x88\x90"},
    {0x2f8b3, "\xe6\x88\x9b"}, {0x2f8b4, "\xe6\x89\x9d"}, {0x2f8b5, "\xe6\x8a\xb1"}, {0x2f8b6, "\xe6\x8b\x94"},
    {0x2f8b7, "\xe6\x8d\x90"}, {0x2f8b8, "\xf0\xa2\xac\x8c"}, {0x2f8b9, "\xe6\x8c\xbd"}, {0x2f8ba, "\xe6\x8b\xbc"},
    {0x2f8bb, "\xe6\x8d\xa8"}, {0x2f8bc, "\xe6\x8e\x83"}, {0x2f8bd, "\xe6\x8f\xa4"}, {0x2f8be, "\xf0\xa2\xaf\xb1"},
    {0x2f8bf, "\xe6\x90\xa2"}, {0x2f8c0, "\xe6\x8f\x85"}, {0x2f8c1, "\xe6\x8e\xa9"}, {0x2f8c2, "\xe3\xa8\xae"},
    {0x2f8c3, "\xe6\x91\xa9"}, {0x2f8c4, "\xe6\x91\xbe"}, {0x2f8c5, "\xe6\x92\x9d"}, {0x2f8c6, "\xe6\x91\xb7"},
    {0x2f8c7, "\xe3\xa9\xac"}, {0x2f8c8, "\xe6\x95\x8f"}, {0x2f8c9, "\xe6\x95\xac"}, {0x2f8ca, "\xf0\xa3\x80\x8a"},
    {0x2f8cb, "\xe6\x97\xa3"}, {0x2f8cc, "\xe6\x9b\xb8"}, {0x2f8cd, "\xe6\x99\x89"}, {0x2f8ce, "\xe3\xac\x99"},
    {0x2f8cf, "\xe6\x9a\x91"}, {0x2f8d0, "\xe3\xac\x88"}, {0x2f8d1, "\xe3\xab\xa4"}, {0x2f8d2, "\xe5\x86\x92"},
    {0x2f8d3, "\xe5\x86\x95"}, {0x2f8d4, "\xe6\x9c\x80"}, {0x2f8d5, "\xe6\x9a\x9c"}, {0x2f8d6, "\xe8\x82\xad"},
    {0x2f8d7, "\xe4\x8f\x99"}, {0x2f8d8, "\xe6\x9c\x97"}, {0x2f8d9, "\xe6\x9c\x9b"}, {0x2f8da, "\xe6\x9c\xa1"},
    {0x2f8db, "\xe6\x9d\x9e"}, {0x2f8dc, "\xe6\x9d\x93"}, {0x2f8dd, "\xf0\xa3\x8f\x83"}, {0x2f8de, "\xe3\xad\x89"},
    {0x2f8df, "\xe6\x9f\xba"}, {0x2f8e0, "\xe6\x9e\x85"}, {0x2f8e1, "\xe6\xa1\x92"}, {0x2f8e2, "\xe6\xa2\x85"},
    {0x2f8e3, "\xf0\xa3\x91\xad"}, {0x2f8e4, "\xe6\xa2\x8e"}, {0x2f8e5, "\xe6\xa0\x9f"}, {0x2f8e6, "\xe6\xa4\x94"},
    {0x2f8e7, "\xe3\xae\x9d"}, {0x2f8e8, "\xe6\xa5\x82"}, {0x2f8e9, "\xe6\xa6\xa3"}, {0x2f8ea, "\xe6\xa7\xaa"},
    {0x2f8eb, "\xe6\xaa\xa8"}, {0x2f8ec, "\xf0\xa3\x9a\xa3"}, {0x2f8ed, "\xe6\xab\x9b"}, {0x2f8ee, "\xe3\xb0\x98"},
    {0x2f8ef, "\xe6\xac\xa1"}, {0x2f8f0, "\xf0\xa3\xa2\xa7"}, {0x2f8f1, "\xe6\xad\x94"}, {0x2f8f2, "\xe3\xb1\x8e"},
    {0x2f8f3, "\xe6\xad\xb2"}, {0x2f8f4, "\xe6\xae\x9f"}, {0x2f8f5, "\xe6\xae\xba"}, {0x2f8f6, "\xe6\xae\xbb"},
    {0x2f8f7, "\xf0\xa3\xaa\x8d"}, {0x2f8f8, "\xf0\xa1\xb4\x8b"}, {0x2f8f9, "\xf0\xa3\xab\xba"}, {0x2f8fa, "\xe6\xb1\x8e"},
    {0x2f8fb, "\xf0\xa3\xb2\xbc"}, {0x2f8fc, "\xe6\xb2\xbf"}, {0x2f8fd, "\xe6\xb3\x8d"}, {0x2f8fe, "\xe6\xb1\xa7"},
    {0x2f8ff, "\xe6\xb4\x96"}, {0x2f900, "\xe6\xb4\xbe"}, {0x2f901, "\xe6\xb5\xb7"}, {0x2f902, "\xe6\xb5\x81"},
    {0x2f903, "\xe6\xb5\xa9"}, {0x2f904, "\xe6\xb5\xb8"}, {0x2f905, "\xe6\xb6\x85"}, {0x2f906, "\xf0\xa3\xb4\x9e"},
    {0x2f907, "\xe6\xb4\xb4"}, {0x2f908, "\xe6\xb8\xaf"}, {0x2f909, "\xe6\xb9\xae"}, {0x2f90a, "\xe3\xb4\xb3"},
    {0x2f90b, "\xe6\xbb\x8b"}, {0x2f90c, "\xe6\xbb\x87"}, {0x2f90d, "\xf0\xa3\xbb\x91"}, {0x2f90e, "\xe6\xb7\xb9"},
    {0x2f90f, "\xe6\xbd\xae"}, {0x2f910, "\xf0\xa3\xbd\x9e"}, {0x2f911, "\xf0\xa3\xbe\x8e"}, {0x2f912, "\xe6\xbf\x86"},
    {0x2f913, "\xe7\x80\xb9"}, {0x2f914, "\xe7\x80\x9e"}, {0x2f915, "\xe7\x80\x9b"}, {0x2f916, "\xe3\xb6\x96"},
    {0x2f917, "\xe7\x81\x8a"}, {0x2f918, "\xe7\x81\xbd"}, {0x2f919, "\xe7\x81\xb7"}, {0x2f91a, "\xe7\x82\xad"},
    {0x2f91b, "\xf0\xa0\x94\xa5"}, {0x2f91c, "\xe7\x85\x85"}, {0x2f91d, "\xf0\xa4\x89\xa3"}, {0x2f91e, "\xe7\x86\x9c"},
    {0x2f91f, "\xf0\xa4\x8e\xab"}, {0x2f920, "\xe7\x88\xa8"}, {0x2f921, "\xe7\x88\xb5"}, {0x2f922, "\xe7\x89\x90"},
    {0x2f923, "\xf0\xa4\x98\x88"}, {0x2f924, "\xe7\x8a\x80"}, {0x2f925, "\xe7\x8a\x95"}, {0x2f926, "\xf0\xa4\x9c\xb5"},
    {0x2f927, "\xf0\xa4\xa0\x94"}, {0x2f928, "\xe7\x8d\xba"}, {0x2f929, "\xe7\x8e\x8b"}, {0x2f92a, "\xe3\xba\xac"},
    {0x2f92b, "\xe7\x8e\xa5"}, {0x2f92c, "\xe3\xba\xb8"}, {0x2f92d, "\xe3\xba\xb8"}, {0x2f92e, "\xe7\x91\x87"},
    {0x2f92f, "\xe7\x91\x9c"}, {0x2f930, "\xe7\x91\xb1"}, {0x2f931, "\xe7\x92\x85"}, {0x2f932, "\xe7\x93\x8a"},
    {0x2f933, "\xe3\xbc\x9b"}, {0x2f934, "\xe7\x94\xa4"}, {0x2f935, "\xf0\xa4\xb0\xb6"}, {0x2f936, "\xe7\x94\xbe"},
    {0x2f937, "\xf0\xa4\xb2\x92"}, {0x2f938, "\xe7\x95\xb0"}, {0x2f939, "\xf0\xa2\x86\x9f"}, {0x2f93a, "\xe7\x98\x90"},
    {0x2f93b, "\xf0\xa4\xbe\xa1"}, {0x2f93c, "\xf0\xa4\xbe\xb8"}, {0x2f93d, "\xf0\xa5\x81\x84"}, {0x2f93e, "\xe3\xbf\xbc"},
    {0x2f93f, "\xe4\x80\x88"}, {0x2f940, "\xe7\x9b\xb4"}, {0x2f941, "\xf0\xa5\x83\xb3"}, {0x2f942, "\xf0\xa5\x83\xb2"},
    {0x2f943, "\xf0\xa5\x84\x99"}, {0x2f944, "\xf0\xa5\x84\xb3"}, {0x2f945, "\xe7\x9c\x9e"}, {0x2f946, "\xe7\x9c\x9f"},
    {0x2f947, "\xe7\x9c\x9f"}, {0x2f948, "\xe7\x9d\x8a"}, {0x2f949, "\xe4\x80\xb9"}, {0x2f94a, "\xe7\x9e\x8b"},
    {0x2f94b, "\xe4\x81\x86"}, {0x2f94c, "\xe4\x82\x96"}, {0x2f94d, "\xf0\xa5\x90\x9d"}, {0x2f94e, "\xe7\xa1\x8e"},
    {0x2f94f, "\xe7\xa2\x8c"}, {0x2f950, "\xe7\xa3\x8c"}, {0x2f951, "\xe4\x83\xa3"}, {0x2f952, "\xf0\xa5\x98\xa6"},
    {0x2f953, "\xe7\xa5\x96"}, {0x2f954, "\xf0\xa5\x9a\x9a"}, {0x2f955, "\xf0\xa5\x9b\x85"}, {0x2f956, "\xe7\xa6\x8f"},
    {0x2f957, "\xe7\xa7\xab"}, {0x2f958, "\xe4\x84\xaf"}, {0x2f959, "\xe7\xa9\x80"}, {0x2f95a, "\xe7\xa9\x8a"},
    {0x2f95b, "\xe7\xa9\x8f"}, {0x2f95c, "\xf0\xa5\xa5\xbc"}, {0x2f95d, "\xf0\xa5\xaa\xa7"}, {0x2f95e, "\xf0\xa5\xaa\xa7"},
    {0x2f95f, "\xe7\xab\xae"}, {0x2f960, "\xe4\x88\x82"}, {0x2f961, "\xf0\xa5\xae\xab"}, {0x2f962, "\xe7\xaf\x86"},
    {0x2f963, "\xe7\xaf\x89"}, {0x2f964, "\xe4\x88\xa7"}, {0x2f965, "\xf0\xa5\xb2\x80"}, {0x2f966, "\xe7\xb3\x92"},
    {0x2f967, "\xe4\x8a\xa0"}, {0x2f968, "\xe7\xb3\xa8"}, {0x2f969, "\xe7\xb3\xa3"}, {0x2f96a, "\xe7\xb4\x80"},
    {0x2f96b, "\xf0\xa5\xbe\x86"}, {0x2f96c, "\xe7\xb5\xa3"}, {0x2f96d, "\xe4\x8c\x81"}, {0x2f96e, "\xe7\xb7\x87"},
    {0x2f96f, "\xe7\xb8\x82"}, {0x2f970, "\xe7\xb9\x85"}, {0x2f971, "\xe4\x8c\xb4"}, {0x2f972, "\xf0\xa6\x88\xa8"},
    {0x2f973, "\xf0\xa6\x89\x87"}, {0x2f974, "\xe4\x8d\x99"}, {0x2f975, "\xf0\xa6\x8b\x99"}, {0x2f976, "\xe7\xbd\xba"},
    {0x2f977, "\xf0\xa6\x8c\xbe"}, {0x2f978, "\xe7\xbe\x95"}, {0x2f979, "\xe7\xbf\xba"}, {0x2f97a, "\xe8\x80\x85"},
    {0x2f97b, "\xf0\xa6\x93\x9a"}, {0x2f97c, "\xf0\xa6\x94\xa3"}, {0x2f97d, "\xe8\x81\xa0"}, {0x2f97e, "\xf0\xa6\x96\xa8"},
    {0x2f97f, "\xe8\x81\xb0"}, {0x2f980, "\xf0\xa3\x8d\x9f"}, {0x2f981, "\xe4\x8f\x95"}, {0x2f982, "\xe8\x82\xb2"},
    {0x2f983, "\xe8\x84\x83"}, {0x2f984, "\xe4\x90\x8b"}, {0x2f985, "\xe8\x84\xbe"}, {0x2f986, "\xe5\xaa\xb5"},
    {0x2f987, "\xf0\xa6\x9e\xa7"}, {0x2f988, "\xf0\xa6\x9e\xb5"}, {0x2f989, "\xf0\xa3\x8e\x93"}, {0x2f98a, "\xf0\xa3\x8e\x9c"},
    {0x2f98b, "\xe8\x88\x81"}, {0x2f98c, "\xe8\x88\x84"}, {0x2f98d, "\xe8\xbe\x9e"}, {0x2f98e, "\xe4\x91\xab"},
    {0x2f98f, "\xe8\x8a\x91"}, {0x2f990, "\xe8\x8a\x8b"}, {0x2f991, "\xe8\x8a\x9d"}, {0x2f992, "\xe5\x8a\xb3"},
    {0x2f993, "\xe8\x8a\xb1"}, {0x2f994, "\xe8\x8a\xb3"}, {0x2f995, "\xe8\x8a\xbd"}, {0x2f996, "\xe8\x8b\xa6"},
    {0x2f997, "\xf0\xa6\xac\xbc"}, {0x2f998, "\xe8\x8b\xa5"}, {0x2f999, "\xe8\x8c\x9d"}, {0x2f99a, "\xe8\x8d\xa3"},
    {0x2f99b, "\xe8\x8e\xad"}, {0x2f99c, "\xe8\x8c\xa3"}, {0x2f99d, "\xe8\x8e\xbd"}, {0x2f99e, "\xe8\x8f\xa7"},
    {0x2f99f, "\xe8\x91\x97"}, {0x2f9a0, "\xe8\x8d\x93"}, {0x2f9a1, "\xe8\x8f\x8a"}, {0x2f9a2, "\xe8\x8f\x8c"},
    {0x2f9a3, "\xe8\x8f\x9c"}, {0x2f9a4, "\xf0\xa6\xb0\xb6"}, {0x2f9a5, "\xf0\xa6\xb5\xab"}, {0x2f9a6, "\xf0\xa6\xb3\x95"},
    {0x2f9a7, "\xe4\x94\xab"}, {0x2f9a8, "\xe8\x93\xb1"}, {0x2f9a9, "\xe8\x93\xb3"}, {0x2f9aa, "\xe8\x94\x96"},
    {0x2f9ab, "\xf0\xa7\x8f\x8a"}, {0x2f9ac, "\xe8\x95\xa4"}, {0x2f9ad, "\xf0\xa6\xbc\xac"}, {0x2f9ae, "\xe4\x95\x9d"},
    {0x2f9af, "\xe4\x95\xa1"}, {0x2f9b0, "\xf0\xa6\xbe\xb1"}, {0x2f9b1, "\xf0\xa7\x83\x92"}, {0x2f9b2, "\xe4\x95\xab"},
    {0x2f9b3, "\xe8\x99\x90"}, {0x2f9b4, "\xe8\x99\x9c"}, {0x2f9b5, "\xe8\x99\xa7"}, {0x2f9b6, "\xe8\x99\xa9"},
    {0x2f9b7, "\xe8\x9a\xa9"}, {0x2f9b8, "\xe8\x9a\x88"}, {0x2f9b9, "\xe8\x9c\x8e"}, {0x2f9ba, "\xe8\x9b\xa2"},
    {0x2f9bb, "\xe8\x9d\xb9"}, {0x2f9bc, "\xe8\x9c\xa8"}, {0x2f9bd, "\xe8\x9d\xab"}, {0x2f9be, "\xe8\x9e\x86"},
    {0x2f9bf, "\xe4\x97\x97"}, {0x2f9c0, "\xe8\x9f\xa1"}, {0x2f9c1, "\xe8\xa0\x81"}, {0x2f9c2, "\xe4\x97\xb9"},
    {0x2f9c3, "\xe8\xa1\xa0"}, {0x2f9c4, "\xe8\xa1\xa3"}, {0x2f9c5, "\xf0\xa7\x99\xa7"}, {0x2f9c6, "\xe8\xa3\x97"},
    {0x2f9c7, "\xe8\xa3\x9e"}, {0x2f9c8, "\xe4\x98\xb5"}, {0x2f9c9, "\xe8\xa3\xba"}, {0x2f9ca, "\xe3\x92\xbb"},
    {0x2f9cb, "\xf0\xa7\xa2\xae"}, {0x2f9cc, "\xf0\xa7\xa5\xa6"}, {0x2f9cd, "\xe4\x9a\xbe"}, {0x2f9ce, "\xe4\x9b\x87"},
    {0x2f9cf, "\xe8\xaa\xa0"}, {0x2f9d0, "\xe8\xab\xad"}, {0x2f9d1, "\xe8\xae\x8a"}, {0x2f9d2, "\xe8\xb1\x95"},
    {0x2f9d3, "\xf0\xa7\xb2\xa8"}, {0x2f9d4, "\xe8\xb2\xab"}, {0x2f9d5, "\xe8\xb3\x81"}, {0x2f9d6, "\xe8\xb4\x9b"},
    {0x2f9d7, "\xe8\xb5\xb7"}, {0x2f9d8, "\xf0\xa7\xbc\xaf"}, {0x2f9d9, "\xf0\xa0\xa0\x84"}, {0x2f9da, "\xe8\xb7\x8b"},
    {0x2f9db, "\xe8\xb6\xbc"}, {0x2f9dc, "\xe8\xb7\xb0"}, {0x2f9dd, "\xf0\xa0\xa3\x9e"}, {0x2f9de, "\xe8\xbb\x94"},
    {0x2f9df, "\xe8\xbc\xb8"}, {0x2f9e0, "\xf0\xa8\x97\x92"}, {0x2f9e1, "\xf0\xa8\x97\xad"}, {0x2f9e2, "\xe9\x82\x94"},
    {0x2f9e3, "\xe9\x83\xb1"}, {0x2f9e4, "\xe9\x84\x91"}, {0x2f9e5, "\xf0\xa8\x9c\xae"}, {0x2f9e6, "\xe9\x84\x9b"},
    {0x2f9e7, "\xe9\x88\xb8"}, {0x2f9e8, "\xe9\x8b\x97"}, {0x2f9e9, "\xe9\x8b\x98"}, {0x2f9ea, "\xe9\x89\xbc"},
    {0x2f9eb, "\xe9\x8f\xb9"}, {0x2f9ec, "\xe9\x90\x95"}, {0x2f9ed, "\xf0\xa8\xaf\xba"}, {0x2f9ee, "\xe9\x96\x8b"},
    {0x2f9ef, "\xe4\xa6\x95"}, {0x2f9f0, "\xe9\x96\xb7"}, {0x2f9f1, "\xf0\xa8\xb5\xb7"}, {0x2f9f2, "\xe4\xa7\xa6"},
    {0x2f9f3, "\xe9\x9b\x83"}, {0x2f9f4, "\xe5\xb6\xb2"}, {0x2f9f5, "\xe9\x9c\xa3"}, {0x2f9f6, "\xf0\xa9\x85\x85"},
    {0x2f9f7, "\xf0\xa9\x88\x9a"}, {0x2f9f8, "\xe4\xa9\xae"}, {0x2f9f9, "\xe4\xa9\xb6"}, {0x2f9fa, "\xe9\x9f\xa0"},
    {0x2f9fb, "\xf0\xa9\x90\x8a"}, {0x2f9fc, "\xe4\xaa\xb2"}, {0x2f9fd, "\xf0\xa9\x92\x96"}, {0x2f9fe, "\xe9\xa0\x8b"},
    {0x2f9ff, "\xe9\xa0\x8b"}, {0x2fa00, "\xe9\xa0\xa9"}, {0x2fa01, "\xf0\xa9\x96\xb6"}, {0x2fa02, "\xe9\xa3\xa2"},
    {0x2fa03, "\xe4\xac\xb3"}, {0x2fa04, "\xe9\xa4\xa9"}, {0x2fa05, "\xe9\xa6\xa7"}, {0x2fa06, "\xe9\xa7\x82"},
    {0x2fa07, "\xe9\xa7\xbe"}, {0x2fa08, "\xe4\xaf\x8e"}, {0x2fa09, "\xf0\xa9\xac\xb0"}, {0x2fa0a, "\xe9\xac\x92"},
    {0x2fa0b, "\xe9\xb1\x80"}, {0x2fa0c, "\xe9\xb3\xbd"}, {0x2fa0d, "\xe4\xb3\x8e"}, {0x2fa0e, "\xe4\xb3\xad"},
    {0x2fa0f, "\xe9\xb5\xa7"}, {0x2fa10, "\xf0\xaa\x83\x8e"}, {0x2fa11, "\xe4\xb3\xb8"}, {0x2fa12, "\xf0\xaa\x84\x85"},
    {0x2fa13, "\xf0\xaa\x88\x8e"}, {0x2fa14, "\xf0\xaa\x8a\x91"}, {0x2fa15, "\xe9\xba\xbb"}, {0x2fa16, "\xe4\xb5\x96"},
    {0x2fa17, "\xe9\xbb\xb9"}, {0x2fa18, "\xe9\xbb\xbe"}, {0x2fa19, "\xe9\xbc\x85"}, {0x2fa1a, "\xe9\xbc\x8f"},
    {0x2fa1b, "\xe9\xbc\x96"}, {0x2fa1c, "\xe9\xbc\xbb"}, {0x2fa1d, "\xf0\xaa\x98\x80"},
};

static u32 utf8_next(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    u32 cp = *p++;
    int n = cp >= 0xf0 && cp <= 0xf4 ? 3 : cp >= 0xe0 && cp <= 0xef ? 2 : cp >= 0xc2 && cp <= 0xdf ? 1 : 0;
    if (!n) { *s = (const char *)p; return cp < 128 ? cp : 0xfffd; }
    cp &= (1u << (6 - n)) - 1;
    for (int i = 0; i < n; i++) {
        if ((*p & 0xc0) != 0x80) { *s = (const char *)p; return 0xfffd; }
        cp = (cp << 6) | (*p++ & 63);
    }
    *s = (const char *)p;
    if ((n == 1 && cp < 0x80) || (n == 2 && cp < 0x800) || (n == 3 && cp < 0x10000) ||
        cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return 0xfffd;
    return cp;
}

static char *utf8_put(char *out, u32 cp) {
    if (cp < 0x80) *out++ = cp;
    else if (cp < 0x800) { *out++ = 0xc0 | (cp >> 6); *out++ = 0x80 | (cp & 63); }
    else if (cp < 0x10000) { *out++ = 0xe0 | (cp >> 12); *out++ = 0x80 | ((cp >> 6) & 63); *out++ = 0x80 | (cp & 63); }
    else { *out++ = 0xf0 | (cp >> 18); *out++ = 0x80 | ((cp >> 12) & 63); *out++ = 0x80 | ((cp >> 6) & 63); *out++ = 0x80 | (cp & 63); }
    return out;
}

static bool search_space(u32 c) {
    return (c >= 9 && c <= 13) || c == 32 || c == 0xa0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 ||
           c == 0x202f || c == 0x205f || c == 0x3000 || c == 0xfeff;
}

static char *search_normalize(const char *s) {
    if (!s) s = "";
    size_t len = strlen(s);
    if (len > (SIZE_MAX - 1) / 4) return NULL;
    char *buf = malloc(len * 4 + 1), *out = buf, *end = buf;
    if (!buf) return NULL;
    while (*s) {
        u32 cp = utf8_next(&s);
        if (cp >= 0x300 && cp <= 0x36f) continue;
        bool space = search_space(cp);
        if (space && out == buf) continue;
        if (cp >= 'A' && cp <= 'Z') cp += 'a' - 'A';
        if (cp >= 0xac00 && cp <= 0xd7a3) {
            u32 h = cp - 0xac00;
            out = utf8_put(out, 0x1100 + h / 588);
            out = utf8_put(out, 0x1161 + (h % 588) / 28);
            if (h % 28) out = utf8_put(out, 0x11a7 + h % 28);
        } else {
            size_t lo = 0, hi = sizeof search_fold / sizeof *search_fold;
            while (lo < hi) {
                size_t mid = lo + (hi - lo) / 2;
                if (search_fold[mid].code < cp) lo = mid + 1; else hi = mid;
            }
            if (lo < sizeof search_fold / sizeof *search_fold && search_fold[lo].code == cp) {
                size_t n = strlen(search_fold[lo].text);
                memcpy(out, search_fold[lo].text, n); out += n;
            } else out = utf8_put(out, cp);
        }
        if (!space) end = out;
    }
    *end = 0;
    return buf;
}

static char *search_fields(const char **fields, int count) {
    size_t n = 1;
    for (int i = 0; i < count; i++) {
        size_t len = fields[i] ? strlen(fields[i]) : 0;
        if (len >= SIZE_MAX - n) return NULL;
        n += len + 1;
    }
    char *joined = malloc(n);
    if (!joined) return NULL;
    char *p = joined;
    for (int i = 0; i < count; i++) {
        if (i) *p++ = ' ';
        if (fields[i]) { size_t len = strlen(fields[i]); memcpy(p, fields[i], len); p += len; }
    }
    *p = 0;
    char *normal = search_normalize(joined);
    free(joined);
    return normal;
}

typedef struct { App *app; int score; } SearchHit;
static int search_first(const void *lhs, const void *rhs) {
    const SearchHit *a = lhs, *b = rhs;
    return a->score != b->score ? b->score - a->score : latest_first(&a->app, &b->app);
}

int catalog_search(const char *query, App **out, int max) {
    if (!out || max <= 0) return 0;
    char *q = search_normalize(query);
    if (!q) return 0;
    int n = 0;
    if (!*q) {
        for (int i = 0; i < NAPPS && n < max && n < 5; i++) {
            App *a = &APPS[i];
            if (a->status && !strcmp(a->status, "released") &&
                (json_is_true(json_object_get(a->j, "featured")) || (a->category && !strcmp(a->category, "forge")))) out[n++] = a;
        }
        free(q); return n;
    }
    SearchHit hits[MAX_APPS];
    for (int i = 0; i < NAPPS; i++) {
        App *a = &APPS[i];
        if(a->retired)continue;
        const char *fields[] = {a->title, json_string_value(json_object_get(a->j, "fullTitle")), a->author,
                               a->platform, LJ(json_object_get(a->j, "tagline")), a->repo};
        char *title = search_fields(fields, 2), *hay = search_fields(fields, 6);
        int score = title && !strncmp(title, q, strlen(q)) ? 3 : title && strstr(title, q) ? 2 : hay && strstr(hay, q) ? 1 : 0;
        free(title); free(hay);
        if (score) hits[n++] = (SearchHit){a, score};
    }
    free(q);
    qsort(hits, n, sizeof *hits, search_first);
    if (n > max) n = max;
    for (int i = 0; i < n; i++) out[i] = hits[i].app;
    return n;
}

// ---- releases ------------------------------------------------------------------------------
static json_t *valid_release(json_t *rel) {
    const char *tag = json_string_value(json_object_get(rel, "tag"));
    return json_is_object(rel) && tag && *tag ? rel : NULL;
}

static bool iso_date(const char *s) {
    if (!s || strlen(s) < 10 || s[4] != '-' || s[7] != '-') return false;
    for (int i = 0; i < 10; i++)
        if (i != 4 && i != 7 && !isdigit((unsigned char)s[i])) return false;
    return true;
}

json_t *app_release(App *a, bool experimental) {
    if (!a) return NULL;
    json_t *rel = json_object_get(a->j, "releases");
    json_t *st = valid_release(json_object_get(rel, "stable")), *ex = valid_release(json_object_get(rel, "experimental"));
    if (experimental && app_has_experimental(a)) return ex;
    return st ? st : ex;
}

bool app_has_experimental(App *a) {
    if (!a) return false;
    json_t *rel = json_object_get(a->j, "releases");
    json_t *st = valid_release(json_object_get(rel, "stable")), *ex = valid_release(json_object_get(rel, "experimental"));
    if (!ex) return false;
    if (!st) return true;
    const char *ed = json_string_value(json_object_get(ex, "date"));
    const char *sd = json_string_value(json_object_get(st, "date"));
    return iso_date(ed) && iso_date(sd) && strcmp(ed, sd) > 0;
}

bool app_release_experimental(App *a, json_t *rel) {
    return a && rel && rel == json_object_get(json_object_get(a->j, "releases"), "experimental");
}

json_t *app_install_steps(App *a, bool cia) {
    json_t *steps = a ? json_object_get(json_object_get(a->j, "install"), cia ? "cia" : "3dsx") : NULL;
    return json_is_array(steps) ? steps : NULL;
}
bool app_has_format(App *a, bool cia) { return json_array_size(app_install_steps(a, cia)) > 0; }

json_t *app_asset(App *a, json_t *rel, const char *key) {
    (void)a;
    if (!key) return NULL;
    json_t *asset = json_object_get(json_object_get(rel, "assets"), key);
    if (!json_is_object(asset)) return NULL;
    json_t *old = json_object_get(asset, "o3ds");
    return !IS_NEW3DS && json_is_object(old) ? old : asset;
}

u32 app_download_size(App *a, bool cia, bool experimental) {
    json_t *rel = app_release(a, experimental);
    // Home uses the preferred format; use the available one if it is absent.
    if (!app_has_format(a, cia)) cia = !cia;
    json_t *steps = app_install_steps(a, cia);
    if (!rel || !steps) return 0;
    uint64_t total = 0;
    size_t i; json_t *s;
    json_array_foreach(steps, i, s) {
        const char *type = json_string_value(json_object_get(s, "type"));
        json_t *bytes = NULL;
        if (type && !strcmp(type, "downloadUrl")) bytes = json_object_get(s, "size");
        else if (type && !strcmp(type, "downloadRelease"))
            bytes = json_object_get(app_asset(a, rel, json_string_value(json_object_get(s, "asset"))), "size");
        json_int_t size = json_integer_value(bytes);
        if (size > 0) {
            if ((uint64_t)size > UINT32_MAX - total) return UINT32_MAX;
            total += (uint64_t)size;
        }
    }
    return (u32)total;
}

// ---- installed apps -----------------------------------------------------------------------------
bool path_exists(const char *path) {
    struct stat st;
    return path && *path && stat(path, &st) == 0;
}

// The SD card is slow: drawing code asks this cache, which re-checks one stale path per
// frame at most (results are kept for a few seconds).
#define PCACHE 48
static struct {
    u32 hash;
    bool have;
    float t;
} pcache[PCACHE];
static float pcache_frame = -1;

static u32 phash(const char *s) {
    u32 h = 2166136261u;
    while (*s)
        h = (h ^ (u8)*s++) * 16777619u;
    return h ? h : 1;
}

bool path_exists_cached(const char *path) {
    if (!path || !*path) return false;
    u32 h = phash(path);
    int slot = -1, oldest = 0;
    for (int i = 0; i < PCACHE; i++) {
        if (pcache[i].hash == h) {
            slot = i;
            break;
        }
        if (pcache[i].t < pcache[oldest].t)
            oldest = i;
    }
    bool fresh = slot >= 0 && NOW - pcache[slot].t < 4;
    if (fresh || (slot >= 0 && pcache_frame == NOW))
        return pcache[slot].have;
    if (slot < 0)
        slot = oldest; // never seen: always checked, so the first answer is right
    else
        pcache_frame = NOW;
    pcache[slot].hash = h;
    pcache[slot].have = path_exists(path);
    pcache[slot].t = NOW;
    return pcache[slot].have;
}

void path_cache_clear(void) { memset(pcache, 0, sizeof pcache); }

void installed_load(void) {
    json_t *j = load_saved_object(DATA_DIR "/installed.json");
    json_decref(INSTALLED);
    INSTALLED = j ? j : json_object();
    path_cache_clear();
}

json_t *installed_record(App *a) {
    json_t *r = a && a->id ? json_object_get(INSTALLED, a->id) : NULL;
    return json_is_object(r) ? r : NULL;
}

static bool array_add_unique(json_t *out, const char *value) {
    if (!value || !*value) return true;
    size_t i; json_t *v;
    json_array_foreach(out, i, v)
        if (!strcmp(json_string_value(v), value)) return true;
    return json_array_append_new(out, json_string(value)) == 0;
}

static bool merge_strings(json_t *out, json_t *in) {
    size_t i; json_t *v;
    json_array_foreach(in, i, v)
        if (!array_add_unique(out, json_string_value(v))) return false;
    return true;
}

// Arguments are borrowed. Do not publish new metadata to readers until it is saved.
bool installed_commit_manifest(App *a, const char *version, bool experimental, bool cia, json_t *files, json_t *dirs, json_t *hashes) {
    if (!a || !a->id || !*a->id || !version || !*version ||
        (files && !json_is_array(files)) || (dirs && !json_is_array(dirs)) || (hashes && !json_is_object(hashes))) return false;
    json_t *old = installed_record(a);
    json_t *next = INSTALLED ? json_deep_copy(INSTALLED) : json_object();
    json_t *r = old ? json_deep_copy(old) : json_object();
    json_t *all_files = json_array(), *all_dirs = json_array(), *formats = json_array();
    bool ok = next && r && all_files && all_dirs && formats;
    if (ok) ok = merge_strings(all_files, json_object_get(old, "files")) && merge_strings(all_files, files) &&
                 merge_strings(all_dirs, json_object_get(old, "dirs")) && merge_strings(all_dirs, dirs) &&
                 merge_strings(formats, json_object_get(old, "formats"));
    bool was_cia = json_is_true(json_object_get(old, "cia"));
    const char *old_format = json_string_value(json_object_get(old, "format"));
    if (ok && old) {
        ok = array_add_unique(formats, old_format);
        if (ok && was_cia) ok = array_add_unique(formats, "cia");
        // Legacy records had only the last format in the cia flag.
        if (ok && !old_format && json_is_false(json_object_get(old, "cia"))) ok = array_add_unique(formats, "3dsx");
    }
    if (ok) ok = array_add_unique(formats, cia ? "cia" : "3dsx");
    time_t now = time(NULL);
    struct tm *utc = gmtime(&now);
    char date[16] = "";
    if (utc) strftime(date, sizeof date, "%Y-%m-%d", utc);
    json_t *fields = json_pack("{s:s,s:b,s:b,s:s,s:s,s:s}", "version", version, "experimental", experimental,
                              "cia", cia || was_cia, "channel", experimental ? "experimental" : "stable",
                              "format", cia ? "cia" : "3dsx", "date", date);
    if (ok) ok = fields && json_object_update(r, fields) == 0 &&
                 json_object_set(r, "files", all_files) == 0 && json_object_set(r, "dirs", all_dirs) == 0 &&
                 json_object_set(r, "formats", formats) == 0 && json_object_set(next, a->id, r) == 0;
    json_t *merged_hashes = json_is_object(json_object_get(old, "hashes")) ? json_deep_copy(json_object_get(old, "hashes")) : json_object();
    if (ok) ok = merged_hashes && (!hashes || json_object_update(merged_hashes, hashes) == 0) &&
                 json_object_set(r, "hashes", merged_hashes) == 0;
    json_decref(merged_hashes);
    if (ok) ok = save_json_sync(next, DATA_DIR "/installed.json");
    if (ok) {
        json_decref(INSTALLED);
        INSTALLED = next;
        next = NULL;
        path_cache_clear();
    }
    json_decref(fields); json_decref(formats); json_decref(all_dirs); json_decref(all_files);
    json_decref(r); json_decref(next);
    return ok;
}

bool installed_commit(App *a, const char *version, bool experimental, bool cia, json_t *files, json_t *dirs) {
    return installed_commit_manifest(a, version, experimental, cia, files, dirs, NULL);
}

void installed_set(App *a, const char *version, bool experimental, bool cia, json_t *files) {
    (void)installed_commit(a, version, experimental, cia, files, NULL);
}

bool installed_forget(App *a) {
    if (!a || !a->id || !*a->id) return false;
    json_t *next = INSTALLED ? json_deep_copy(INSTALLED) : json_object();
    if (!next) return false;
    json_object_del(next, a->id);
    bool ok = save_json_sync(next, DATA_DIR "/installed.json");
    if (ok) {
        json_decref(INSTALLED); INSTALLED = next; next = NULL;
        path_cache_clear();
    }
    json_decref(next);
    return ok;
}

void installed_remove(App *a) { (void)installed_forget(a); }

const char *app_installed_version(App *a) { return json_string_value(json_object_get(installed_record(a), "version")); }

// Only SD-rooted paths are accepted. Never truncate a path into a different file.
static bool sd_path(const char *p, char *out, size_t n) {
    if (!p) return false;
    if (!strncmp(p, "sdmc:", 5)) p += 5;
    if (*p != '/' || strchr(p, ':') || strchr(p, '\\')) return false;
    for (const char *c = p; *c; c++) {
        if ((unsigned char)*c < 32) return false;
        if (*c == '/' && c[1] == '.' && (c[2] == '/' || !c[2] ||
            (c[2] == '.' && (c[3] == '/' || !c[3])))) return false;
    }
    int len = snprintf(out, n, "sdmc:%s", p);
    return len >= 0 && (size_t)len < n;
}

static bool nonempty_file(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

bool app_requirement_path(json_t *r,char *out,size_t n){
    if(!out||!n)return false;
    out[0]=0;
    const char *p = json_string_value(json_object_get(r, "path"));
    if (!p || !*p) return false;
    char full[1024], path[1024];
    if (!sd_path(p, full, sizeof full)) return false;
    size_t len = strlen(full);
    if (full[len - 1] == '/') {
        const char *example = json_string_value(json_object_get(r, "example"));
        if (!example || !*example) example = "file.bin";
        int count = snprintf(path, sizeof path, "%s%s", full, example);
        if (count < 0 || (size_t)count >= sizeof path || !sd_path(path, full, sizeof full)) return false;
    }
    if(strlen(full)>=n)return false;
    strcpy(out,full);return true;
}
bool catalog_sd_path(const char *path,char *out,size_t size){return sd_path(path,out,size);}
bool app_requirement_met(App *a,json_t *r){
    (void)a;char full[1024];
    if(rom_find_ruled(r))return rom_find_scan(r,NULL,0,NULL);  // any name the port accepts
    return app_requirement_path(r,full,sizeof full)&&nonempty_file(full);
}

json_t *app_missing_requirement(App *a) {
    size_t i; json_t *r;
    if (!a) return NULL;
    json_array_foreach(json_object_get(a->j, "requires"), i, r)
        if (!app_requirement_met(a, r)) return r;
    return NULL;
}

bool app_launch_path(App *a, char *out, size_t n) {
    if (!out || !n) return false;
    out[0] = 0;
    size_t i; json_t *v;
    json_array_foreach(json_object_get(installed_record(a), "files"), i, v) {
        const char *p = json_string_value(v);
        size_t len = p ? strlen(p) : 0;
        if (len < 5 || strcasecmp(p + len - 5, ".3dsx")) continue;
        char path[1024];
        if (sd_path(p, path, sizeof path) && strlen(path) < n && nonempty_file(path)) {
            strcpy(out, path);
            return true;
        }
    }
    return false;
}

AppStatus app_status(App *a) {
    if (!a) return ST_AVAILABLE;
    if (install_busy(a)) return ST_BUSY;
    if (a->dev) return ST_DEV;
    const char *v = app_installed_version(a);
    if (!v || !*v) return ST_AVAILABLE;
    json_t *e = installed_record(a);
    const char *channel = json_string_value(json_object_get(e, "channel"));
    bool experimental = channel ? !strcmp(channel, "experimental") : json_is_true(json_object_get(e, "experimental"));
    json_t *rel = app_release(a, experimental);
    const char *latest = json_string_value(json_object_get(rel, "tag"));
    if (latest && *latest && strcmp(latest, v)) return ST_UPDATE;
    if (app_missing_requirement(a)) return ST_NEEDS_FILE;
    return ST_INSTALLED;
}

void fmt_size(char *out, size_t n, double b) {
    const char *u[] = {"B", "KB", "MB", "GB"};
    int i = 0;
    while (b >= 1024 && i < 3) {
        b /= 1024;
        i++;
    }
    snprintf(out, n, i == 0 || b >= 100 ? "%.0f %s" : "%.1f %s", b, u[i]);
}

void catalog_universal(bool enabled) {
    S.universal = enabled;
    NAPPS = enabled ? total_apps : BUILTIN_APPS;
    NCATS = base_categories + (enabled && base_categories < 8 ? 1 : 0);
    if(!enabled)for(int i=0;i<UNIVERSAL_ICON_SHEETS;i++)sheet_free(&universal_icons[i]);
}
bool catalog_apply_universal(json_t *apps){
    static json_t *live;
    if(live||!json_is_array(apps)||!json_array_size(apps)||json_array_size(apps)>MAX_APPS)return false;
    // Validate the complete generation before touching visible records.
    size_t k;json_t *v;json_t *ids=json_object();bool valid=true;
    json_array_foreach(apps,k,v){const char *id=json_string_value(json_object_get(v,"id")),*title=json_string_value(json_object_get(v,"title"));
        if(!id||strncmp(id,"udb-",4)||!title||!*title||!json_is_object(json_object_get(v,"install"))||json_object_get(ids,id)){valid=false;break;}json_object_set_new(ids,id,json_true());
    }json_decref(ids);if(!valid)return false;
    live=json_incref(apps);for(int i=BUILTIN_APPS;i<total_apps;i++)APPS[i].retired=true;
    int added=0,updated=0;
    json_array_foreach(apps,k,v){const char *id=json_string_value(json_object_get(v,"id"));App *a=NULL;
        for(int i=BUILTIN_APPS;i<total_apps;i++)if(!strcmp(APPS[i].id,id)){a=&APPS[i];break;}
        if(!a){if(total_apps>=MAX_APPS)continue;a=&APPS[total_apps++];memset(a,0,sizeof *a);a->icon=-1;added++;}else updated++;
        a->j=v;a->retired=false;a->id=id;a->title=json_string_value(json_object_get(v,"title"));a->full=a->title;a->author=json_string_value(json_object_get(v,"author"));a->repo=json_string_value(json_object_get(v,"repo"));a->category="universal";a->status="released";a->folder="/3ds/";a->universal=true;a->old_ok=true;a->pixel_icon=true;a->updated=release_update_date(v);a->stars=json_integer_value(json_object_get(v,"stars"));a->has_stars=json_is_integer(json_object_get(v,"stars"));a->featured=json_is_true(json_object_get(v,"featured"));a->color=hexcolor(json_string_value(json_object_get(v,"color")));a->fx=json_string_value(json_object_get(json_object_get(v,"banner"),"fx"));a->mid.i=a->logo.i=-1;
    }catalog_universal(S.universal);printf("Universal-DB applied: %d updated, %d added\n",updated,added);return true;
}
void catalog_request_icon(App *app){
    if(!app||!app->universal)return;
    if(app->icon<BUILTIN_APPS){remote_image_request(json_string_value(json_object_get(app->j,"icon")));return;}
    int index=(app->icon-BUILTIN_APPS)/UNIVERSAL_ICONS_PER_SHEET;
    if(index<0||index>=UNIVERSAL_ICON_SHEETS)return;
    universal_needed[index]=true;
}
void catalog_icons_begin(void){memset(universal_needed,0,sizeof universal_needed);}
void catalog_icons_end(void){
    // All visible icons form one working set, even when date sorting spreads
    // them across the seven bundled sheets. Evict only after collecting it.
    for(int i=0;i<UNIVERSAL_ICON_SHEETS;i++)if(!universal_needed[i])sheet_free(&universal_icons[i]);
    for(int i=0;i<UNIVERSAL_ICON_SHEETS;i++)if(universal_needed[i]&&universal_icons[i].state==SHEET_EMPTY){char path[80];snprintf(path,sizeof path,"romfs:/gfx/universal-%d.ftx",i);universal_icons[i].crisp=true;sheet_request(&universal_icons[i],path);}
}
bool catalog_icon_ready(App *app){
    if(!app||!app->universal)return app!=NULL;
    if(app->icon<BUILTIN_APPS)return remote_image_ready(json_string_value(json_object_get(app->j,"icon")));
    int index=(app->icon-BUILTIN_APPS)/UNIVERSAL_ICONS_PER_SHEET;
    return index>=0&&index<UNIVERSAL_ICON_SHEETS&&sheet_ok(&universal_icons[index]);
}
