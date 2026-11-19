// Store art and data: RomFS first, then verified resource packs on the SD card.
//
// The CIA carries the interface (fonts, UI, icons, my ports' banners, sounds). Everything
// else lives in packs listed by romfs:/packs.json: each pack is one immutable file whose
// SHA-256 is checked once, after download, before it is renamed into place. Callers keep
// using their "romfs:/..." paths; a path that is in a pack still downloading reports
// ASSET_PENDING, so the interface can show its fallback and try again later.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
struct json_t;
struct json_error_t;

typedef enum { ASSET_READY, ASSET_PENDING, ASSET_MISSING } AssetState;

#define ASSET_PACKS_MAX 96

typedef struct {
    char id[49], url[384], sha256[65], path[256];
    unsigned bytes;
} AssetPack;

/** Opens a RomFS file, or a bounded read-only view inside an installed pack. Other paths: fopen. */
FILE *asset_fopen(const char *path);
/** Whole asset in a malloc'd buffer (NULL if unavailable or larger than `limit`). */
unsigned char *asset_read(const char *path, size_t *size, size_t limit);
/** READY, PENDING (its pack is not on the SD yet; `pack` receives the index) or MISSING. */
AssetState asset_state(const char *path, int *pack);
/** Plain or gzip-compressed JSON, from RomFS or an installed pack. */
struct json_t *asset_json(const char *path, size_t flags, struct json_error_t *error);
/** Changes every time a pack is installed: failed loads of pending assets may retry. */
unsigned asset_generation(void);
/** Something on screen waits for this pack: the downloader takes it next. */
void asset_want(int pack);
/** The most recent wish, once (-1 when none). */
int asset_take_wanted(void);

// ---- packs (media worker) --------------------------------------------------------------
unsigned asset_pack_count(void);
bool asset_pack(unsigned index, AssetPack *out);
bool asset_pack_installed(unsigned index);
/** Called after the verified file was renamed to its final path. */
void asset_pack_publish(unsigned index);
/** Removes stale packs and interrupted downloads that the current manifest does not name. */
void asset_pack_prune(void);
