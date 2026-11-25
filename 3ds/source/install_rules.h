#pragma once
#include <jansson.h>
#include <stdbool.h>
#include <string.h>

static inline bool install_format_matches(json_t *item, bool cia) {
    const char *format = json_string_value(json_object_get(item, "format"));
    return !format || !strcmp(format, cia ? "cia" : "3dsx");
}

static inline bool install_model_matches(bool new_model, bool old_ok, json_t *release) {
    return new_model || (old_ok && !json_is_false(json_object_get(release, "o3ds")));
}
