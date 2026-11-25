#pragma once
// Pure rules for recognizing catalog apps that are already on the console without a
// forge.store record (installed with FBI, Universal-Updater or by hand). No SDK and no IO,
// so the host tests run exactly this code. detect.c does the AM and SD queries.
#include <ctype.h>
#include <jansson.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

enum { DETECT_TITLES = 4, DETECT_FILES = 8, DETECT_PATH = 160, DETECT_STAMP = 17 };

/** CIA title of a Universal-DB unique ID, built as Universal-Updater does; 0 = unusable.
 * The devkitPro template's 0xFF3FF is shared by unrelated homebrew: it never identifies an app. */
static inline uint64_t detect_title_of(json_int_t unique) {
    if (unique <= 0 || unique > 0xFFFFF || unique == 0xFF3FF) return 0;
    return 0x0004000000000000ULL | ((uint64_t)unique << 8);
}

/** Titles listed in the catalog's "detect": {"ids": [...]}; duplicates removed. */
static inline int detect_titles(json_t *app, uint64_t *out, int max) {
    int n = 0;
    size_t i;
    json_t *v;
    json_array_foreach(json_object_get(json_object_get(app, "detect"), "ids"), i, v) {
        uint64_t title = json_is_integer(v) ? detect_title_of(json_integer_value(v)) : 0;
        bool seen = !title;
        for (int k = 0; k < n && !seen; k++) seen = out[k] == title;
        if (!seen && n < max) out[n++] = title;
    }
    return n;
}

static inline bool detect_add(char out[][DETECT_PATH], int *n, int max, const char *a, const char *b) {
    char path[DETECT_PATH];
    int len = snprintf(path, sizeof path, "sdmc:%s%s", a, b);
    if (len < 0 || (size_t)len >= sizeof path || path[5] != '/') return false;
    for (const char *c = path + 5; *c; c++) {
        if ((unsigned char)*c < 32 || strchr(":\\*?\"<>|%", *c)) return false;
        if (c[0] == '/' && c[1] == '.' && c[2] == '.' && (c[3] == '/' || !c[3])) return false;
    }
    for (int k = 0; k < *n; k++)
        if (!strcasecmp(out[k], path)) return true;  // FAT is case insensitive
    if (*n >= max) return false;
    memcpy(out[(*n)++], path, (size_t)len + 1);
    return true;
}

/** Where a catalog path can be on the SD card. Universal-DB variables expand as both
 * forge.store and Universal-Updater's defaults place them: %3DSX%/x.3dsx is also looked
 * for as /3ds/x/x.3dsx (its "in folder" option), %NDS% also at the SD root. */
static inline int detect_paths(const char *in, char out[][DETECT_PATH], int *n, int max) {
    if (!in || !*in) return *n;
    if (!strncmp(in, "sdmc:", 5)) in += 5;
    if (!strncmp(in, "%3DSX%/", 7)) {
        const char *rest = in + 6, *dot = strrchr(rest, '.');
        detect_add(out, n, max, "/3ds", rest);
        if (!strchr(rest + 1, '/') && dot && dot > rest + 1) {
            char folder[DETECT_PATH];
            int len = snprintf(folder, sizeof folder, "/3ds%.*s%s", (int)(dot - rest), rest, rest);
            if (len > 0 && (size_t)len < sizeof folder) detect_add(out, n, max, folder, "");
        }
    } else if (!strncmp(in, "%NDS%/", 6)) {
        detect_add(out, n, max, "/nds", in + 5);
        detect_add(out, n, max, "", in + 5);
    } else if (!strncmp(in, "%FIRM%/", 7)) {
        detect_add(out, n, max, "/luma/payloads", in + 6);
    } else if (!strncmp(in, "%ARCHIVE_DEFAULT%/", 18)) {
        detect_add(out, n, max, "", in + 17);
    } else if (in[0] == '/') {
        detect_add(out, n, max, "", in);
    }
    return *n;
}

static inline bool detect_ends(const char *s, const char *suffix) {
    size_t a = s ? strlen(s) : 0, b = strlen(suffix);
    return a >= b && !strcasecmp(s + a - b, suffix);
}

/** Paths whose presence means the app is installed: the catalog's "detect.files"
 * (Universal-DB's installed_files), then every .3dsx its own 3DSX plan writes. */
static inline int detect_candidates(json_t *app, char out[][DETECT_PATH], int max) {
    int n = 0;
    size_t i;
    json_t *v;
    json_array_foreach(json_object_get(json_object_get(app, "detect"), "files"), i, v)
        detect_paths(json_string_value(v), out, &n, max);
    json_array_foreach(json_object_get(json_object_get(app, "install"), "3dsx"), i, v) {
        const char *type = json_string_value(json_object_get(v, "type"));
        const char *path = !type ? NULL : !strcmp(type, "move") ? json_string_value(json_object_get(v, "new")) :
                           (!strcmp(type, "downloadRelease") || !strcmp(type, "downloadUrl") || !strcmp(type, "extractFile"))
                               ? json_string_value(json_object_get(v, "output")) : NULL;
        if (path && detect_ends(path, ".3dsx") && strncmp(path, "%TMP%", 5)) detect_paths(path, out, &n, max);
    }
    return n;
}

/** Universal-Updater stamps ("2026-11-08 at 13:42 (UTC)"), ISO times and plain dates as
 * "YYYY-MM-DDTHH:MM" or "YYYY-MM-DD". False when there is no date ("---"). */
static inline bool detect_stamp(const char *in, char out[DETECT_STAMP]) {
    out[0] = 0;
    if (!in || strlen(in) < 10) return false;
    for (int i = 0; i < 10; i++)
        if (i == 4 || i == 7 ? in[i] != '-' : !isdigit((unsigned char)in[i])) return false;
    memcpy(out, in, 10);
    out[10] = 0;
    const char *t = in + 10;
    while (*t && !isdigit((unsigned char)*t)) t++;
    if (isdigit((unsigned char)t[0]) && isdigit((unsigned char)t[1]) && t[2] == ':' && isdigit((unsigned char)t[3]) &&
        isdigit((unsigned char)t[4])) {
        out[10] = 'T';
        memcpy(out + 11, t, 5);
        out[16] = 0;
    }
    return true;
}

/** Universal-Updater's rule (newer entry than the one it installed), compared only at the
 * precision both stamps have: a release later the same day is not claimed from a date. */
static inline bool detect_newer(const char *available, const char *installed) {
    size_t a = available ? strlen(available) : 0, b = installed ? strlen(installed) : 0;
    size_t n = a < b ? a : b;
    return n >= 10 && strncmp(available, installed, n) > 0;
}

static inline json_t *detect_default_release(json_t *app) {
    json_t *releases = json_object_get(app, "releases");
    json_t *stable = json_object_get(releases, "stable");
    return json_is_object(stable) ? stable : json_object_get(releases, "experimental");
}

/** When the app's default release was published, as precisely as the catalog
 * knows: Universal-DB's update time, else the release date. */
static inline bool detect_available(json_t *app, char out[DETECT_STAMP]) {
    const char *at = json_string_value(json_object_get(app, "updatedAt"));
    if (at && detect_stamp(at, out)) return true;
    return detect_stamp(json_string_value(json_object_get(detect_default_release(app), "date")), out);
}

/** Name of the app's entry in Universal-Updater's records (Universal-DB store). */
static inline const char *detect_uu_name(json_t *app) {
    const char *name = json_string_value(json_object_get(json_object_get(app, "detect"), "uu"));
    if (name && *name) return name;
    const char *source = json_string_value(json_object_get(app, "source"));
    return source && !strcmp(source, "universal") ? json_string_value(json_object_get(app, "title")) : NULL;
}

/** Installed but older: Universal-Updater installed an earlier entry, or the CIA title's
 * version is below the one the default release ships (when the catalog knows it). */
static inline bool detect_outdated(json_t *app, const char *uu_updated, uint64_t title, unsigned version) {
    char available[DETECT_STAMP];
    if (uu_updated && *uu_updated && detect_available(app, available) && detect_newer(available, uu_updated)) return true;
    json_t *want = json_object_get(detect_default_release(app), "ciaVersion");
    return title && json_is_integer(want) && json_integer_value(want) > (json_int_t)version;
}
