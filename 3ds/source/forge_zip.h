#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <jansson.h>

static inline json_int_t forge_wire_size(json_t *asset) {
    json_t *member = json_object_get(asset, "zipMember");
    return json_integer_value(member ? json_object_get(member,"bytes") : json_object_get(asset,"size"));
}

// The authenticated Forge manifest identifies one raw-DEFLATE member of a
// WinZip AES-256 archive. The range excludes its salt, verifier and trailer.
// Its plaintext SHA-256 in the same signed entry is checked before publication.
typedef struct {
    uint32_t offset, bytes;
    unsigned char key[32];
} ForgeZipMember;

static inline int forge_hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return -1;
}

static inline bool forge_zip_member(json_t *value, ForgeZipMember *out) {
    json_t *start = json_object_get(value, "offset"), *length = json_object_get(value, "bytes");
    json_int_t offset = json_integer_value(start), bytes = json_integer_value(length);
    json_t *key = json_object_get(value, "key");
    if (!json_is_integer(start) || !json_is_integer(length) || offset < 18 || offset > INT32_MAX ||
        bytes <= 0 || bytes > INT32_MAX - offset || !json_is_string(key) || json_string_length(key) != 64)
        return false;
    const char *hex = json_string_value(key);
    for (int i = 0; i < 32; i++) {
        int hi = forge_hex_digit(hex[2*i]), lo = forge_hex_digit(hex[2*i+1]);
        if (hi < 0 || lo < 0) return false;
        if (out) out->key[i] = (unsigned char)((hi << 4) | lo);
    }
    if (out) { out->offset = (uint32_t)offset; out->bytes = (uint32_t)bytes; }
    return true;
}
