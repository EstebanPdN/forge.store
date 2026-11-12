// The store catalog (romfs:/catalog.json, the same file the prototype uses),
// banner layers, screenshots and the record of installed apps.
#pragma once
#include "common.h"
#include "gfx.h"

typedef struct {
    float x, y;
    int i;  // index in the fg sheet, -1 = none
    int w, h;
} Layer;

typedef struct {
    json_t *j;
    const char *id, *title, *full, *author, *category, *status, *repo, *platform, *fx, *folder, *updated;
    int year, progress, stars, icon;
    u32 color;
    bool universal, dev, needs_rom, needs_data, stereo, old_ok, pixel_icon;
    bool featured, has_stars, old_experimental, retired;
    Sheet bg, fg;
    size_t banner_bytes;
    unsigned banner_used;
    Layer mid, logo;
    int shots;
    bool shot_bottom[12];
    Sheet shots_t[3], shots_b[3];
    int shot_index[3];
    Sheet shot_thumbs;
} App;

typedef struct {
    const char *id, *icon;
    json_t *label;
} Category;

extern App APPS[MAX_APPS];
extern int NAPPS;
extern Category CATS[8];
extern int NCATS;
extern Sheet ICONS;
extern json_t *CATALOG;

bool catalog_load(void);
/** Main thread: bounded LRU; warm Forge before the intro and nearby Home items. */
void catalog_request_banners(void);
void app_request_banner(App *a);
void app_prefetch_banner(App *a);
bool catalog_banners_ready(void);
void catalog_trim_banners(App *current, App *previous);
void app_request_shots(App *a);
/** Main thread: retain current/previous/neighbor pairs; thumbnails stay resident. */
void app_request_gallery(App *a, int current, int previous);
C2D_Image app_shot(App *a, int index, bool bottom, bool *ready);
void app_free_shots(App *a);
C2D_Image app_icon(App *a);
/** Icon rendered at exactly `size` px (20, 32, 40, 48 or 64); the 48 px one otherwise. */
C2D_Image app_icon_px(App *a, int size);

/** Newest published version first; "all" is the curated catalog. Undated last. */
int catalog_filter(const char *cat, App **out, int max);
int catalog_search(const char *query, App **out, int max);

/** Release for the channel (experimental only when newer than stable). NULL = none. */
json_t *app_release(App *a, bool experimental);
bool app_has_experimental(App *a);
/** Actual selected channel, including stable/experimental fallback. */
bool app_release_experimental(App *a, json_t *release);
json_t *app_install_steps(App *a, bool cia);
bool app_has_format(App *a, bool cia);
/** Release asset (Old 3DS variant when this console needs it). */
json_t *app_asset(App *a, json_t *rel, const char *key);
u32 app_download_size(App *a, bool cia, bool experimental);

// Installed apps: sdmc:/3ds/forge-store/installed.json
typedef enum { ST_AVAILABLE, ST_INSTALLED, ST_UPDATE, ST_DEV, ST_BUSY, ST_NEEDS_FILE } AppStatus;
AppStatus app_status(App *a);
const char *app_installed_version(App *a);
void installed_load(void);
/** Main thread only. Borrowed read-only record, valid until the next successful
 * installed mutation/load. NULL if absent. Never mutate it from a worker. */
json_t *installed_record(App *a);
/** Main thread only, synchronous durable JSON save. Borrows files/dirs (NULL allowed).
 * false leaves the in-memory installed record unchanged; caller must roll back files.
 * true acknowledges that file backups may be dropped. */
bool installed_commit(App *a, const char *version, bool experimental, bool cia, json_t *files, json_t *dirs);
/** Legacy wrapper: borrows files, ignores failure. New installer code must use commit. */
void installed_set(App *a, const char *version, bool experimental, bool cia, json_t *files);
/** Like installed_commit; merges previous hashes with new hashes (new keys win).
 * hashes is a borrowed path -> SHA256 object, or NULL. */
bool installed_commit_manifest(App *a, const char *version, bool experimental, bool cia,
                               json_t *files, json_t *dirs, json_t *hashes);
/** Main thread, synchronous durable metadata removal. No file/title deletion. */
bool installed_forget(App *a);
void installed_remove(App *a);
bool path_exists(const char *path);
/** Same answer from a short-lived cache (safe to call while drawing). */
bool path_exists_cached(const char *path);
void path_cache_clear(void);
/** First user file (ROM, data) still missing, or NULL. */
json_t *app_missing_requirement(App *a);
/** Existing nonempty regular user file; folder paths check example or file.bin.
 * Missing/pathless/invalid paths remain unresolved. No hash verification. */
bool app_requirement_met(App *a, json_t *requirement);
// The same validated filename used by the requirement check; no filesystem IO.
bool app_requirement_path(json_t *requirement,char *out,size_t size);
bool catalog_sd_path(const char *path,char *out,size_t size);
/** Selects an existing nonempty .3dsx from the installed file inventory.
 * Writes a sdmc:/ path, clears output on failure, never guesses from app.folder. */
bool app_launch_path(App *a, char *out, size_t size);

void fmt_size(char *out, size_t n, double bytes);

void catalog_universal(bool enabled);
// Main thread; app addresses and bundled-art indices stay stable. One live
// generation per launch, retaining old JSON for completed job metadata readers.
bool catalog_apply_universal(json_t *apps);
extern int BUILTIN_APPS;

/** Main-thread visible working set; bundled sheets remain bounded (7 today).
 * Begin, request every visible app, end. Nothing visible is evicted mid-batch. */
void catalog_request_icon(App *app);
void catalog_icons_begin(void);
void catalog_icons_end(void);
bool catalog_icon_ready(App *app);
