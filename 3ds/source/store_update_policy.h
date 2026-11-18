#pragma once
#include <jansson.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include "store_version.h"
static inline void store_update_version_label(json_t *root,int64_t build,char *out,size_t cap) {
    const char *label=json_string_value(json_object_get(root,"version"));
    bool valid=label&&*label&&strlen(label)<=32;
    if(valid)for(const char *p=label;*p;p++)
        if(!((*p>='0'&&*p<='9')||(*p>='A'&&*p<='Z')||(*p>='a'&&*p<='z')||*p=='.'||*p=='-'||*p=='_')){valid=false;break;}
    if(valid)snprintf(out,cap,"%s",label);else snprintf(out,cap,"%lld",(long long)build);
}
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
