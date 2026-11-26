#pragma once
#include <jansson.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include "store_version.h"
// CIA versions use six bits each for major and minor and four for patch.
static inline bool store_update_version_code(const char *version, uint16_t *code) {
    if (!version || !*version) return false;
    unsigned parts[3] = {0}, limit[] = {63, 63, 15};
    for (unsigned i = 0; i < 3; i++) {
        const char *start = version;
        while (*version >= '0' && *version <= '9') {
            parts[i] = parts[i] * 10 + (*version++ - '0');
            if (version - start > 2 || parts[i] > limit[i]) return false;
        }
        if (version == start) return false;
        if (!*version && i >= 1) {
            *code = (parts[0] << 10) | (parts[1] << 4) | parts[2];
            return true;
        }
        if (i == 2 || *version++ != '.') return false;
    }
    return false;
}

static inline bool store_update_file(json_t *size, const char *sha, uint64_t *bytes, char hash[65]) {
    if (!json_is_integer(size) || json_integer_value(size) < 32 ||
        json_integer_value(size) > 96 * 1024 * 1024 || !sha || strlen(sha) != 64)
        return false;
    for (int i = 0; i < 64; i++)
        if (!((sha[i] >= '0' && sha[i] <= '9') || (sha[i] >= 'a' && sha[i] <= 'f')))
            return false;
    *bytes = json_integer_value(size);
    memcpy(hash, sha, 65);
    return true;
}

// Use the versioned asset GitHub lists on the latest stable release. Build the URL
// from the validated tag and expected name, independent of URLs in the response.
static inline bool store_update_release(json_t *root, bool homebrew, char tag[12],
                                        uint16_t *code, uint64_t *bytes, char hash[65]) {
    const char *name = json_string_value(json_object_get(root, "tag_name"));
    if (!name || name[0] != 'v' || !store_update_version_code(name + 1, code) ||
        !json_is_false(json_object_get(root, "draft")) || !json_is_false(json_object_get(root, "prerelease")))
        return false;
    snprintf(tag, 12, "%s", name);
    char asset[40];
    snprintf(asset, sizeof asset, "forge-store-%s.%s", tag, homebrew ? "3dsx" : "cia");
    size_t i;
    json_t *entry;
    json_array_foreach(json_object_get(root, "assets"), i, entry) {
        const char *file = json_string_value(json_object_get(entry, "name"));
        if (!file || strcmp(file, asset)) continue;
        const char *digest = json_string_value(json_object_get(entry, "digest"));
        return digest && !strncmp(digest, "sha256:", 7) &&
               store_update_file(json_object_get(entry, "size"), digest + 7, bytes, hash);
    }
    return false;
}

// Every new release also ships the compact manifest as a release asset. This path
// still follows GitHub's latest release when its REST API quota is exhausted.
static inline bool store_update_release_manifest(json_t *root, bool homebrew, char tag[12],
                                                 uint16_t *code, uint64_t *bytes, char hash[65]) {
    const char *version = json_string_value(json_object_get(root, "version"));
    if (!store_update_version_code(version, code)) return false;
    snprintf(tag, 12, "v%s", version);
    json_t *file = json_object_get(json_object_get(root, "files"), homebrew ? "forge-store.3dsx" : "forge-store.cia");
    return store_update_file(json_object_get(file, "bytes"),
                             json_string_value(json_object_get(file, "sha256")), bytes, hash);
}

// The Homebrew Launcher shows a folder of /3ds/ as an app, with its icon, only when it
// holds <folder>.3dsx or boot.3dsx. A release download keeps its versioned name
// (forge-store-v1.1.3dsx), which hides the icon behind a plain folder.
#define STORE_HOME_3DSX DATA_DIR "/forge-store.3dsx"
static inline bool store_update_misnamed(const char *executable) {
    size_t dir = strlen(DATA_DIR "/"), n = strlen(executable);
    return n > dir + 5 && !strncasecmp(executable, DATA_DIR "/", dir) && !strchr(executable + dir, '/') &&
           !strcasecmp(executable + n - 5, ".3dsx") && strcasecmp(executable, STORE_HOME_3DSX);
}
// Until 1.1 the previous build was kept next to the executable as a launchable
// <name>.forge-previous.3dsx: the launcher listed it as a second, identical forge.store
// that offered the same update again. The backup is now <executable>.previous.
static inline bool store_update_stale_backup(const char *name, const char *executable_name) {
    size_t n = strlen(name), e = strlen(executable_name);
    const char legacy[] = ".forge-previous.3dsx", now[] = ".previous";
    if (n > sizeof legacy - 1 && !strcasecmp(name + n - (sizeof legacy - 1), legacy))
        return strcasecmp(name, executable_name) != 0;
    return n == e + sizeof now - 1 && !strncasecmp(name, executable_name, e) && !strcasecmp(name + e, now);
}
