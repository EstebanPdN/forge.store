#pragma once
#include <jansson.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "store_version.h"
// Manifest is fetched over verified HTTPS from the fixed, trusted repository.
// Reject malformed metadata before choosing a destination or creating an AM handle.
static inline bool store_update_metadata(json_t *root, bool homebrew, uint64_t *bytes, char hash[65],
                                         int64_t *build) {
    json_t *version = json_object_get(root, "store_build");
    if (!json_is_integer(version) || json_integer_value(version) <= 0)
        return false;
    *build = json_integer_value(version);
    json_t *file =
        json_object_get(json_object_get(root, "files"), homebrew ? "forge-store.3dsx" : "forge-store.cia");
    json_t *size = json_object_get(file, "bytes");
    const char *sha = json_string_value(json_object_get(file, "sha256"));
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
