// Included by the installer so ciphertext is never committed to an SD destination.
// WinZip AES uses a little-endian block counter starting at one. At most INT32_MAX
// compressed bytes are accepted, so the low 32 bits cover the entire member.
#include "forge_zip.h"
#include <zlib.h>
#ifndef FORGE_INSTALL_HOST_TEST
#include <mbedtls/aes.h>
#endif

typedef struct {
    mbedtls_aes_context aes;
    z_stream inflate;
    uint32_t block;
    unsigned used;
    u8 mask[16];
    u8 out[32768];
    bool ended;
    u64 plain, limit;
    mbedtls_sha256_context *sha; // optional: digest of the plaintext written to the file
} ForgeZipStream;

static bool forge_zip_begin(ForgeZipStream *s, const ForgeZipMember *member, u64 limit) {
    memset(s, 0, sizeof *s);
    s->used = sizeof s->mask;
    s->limit = limit;
    mbedtls_aes_init(&s->aes);
    if (mbedtls_aes_setkey_enc(&s->aes, member->key, 256) || inflateInit2(&s->inflate, -MAX_WBITS) != Z_OK) {
        mbedtls_aes_free(&s->aes);
        return false;
    }
    return true;
}

static void forge_zip_end(ForgeZipStream *s) {
    inflateEnd(&s->inflate);
    mbedtls_aes_free(&s->aes);
    volatile unsigned char *p = (volatile unsigned char *)s;
    for (size_t i = 0; i < sizeof *s; i++) p[i] = 0;
}

static bool forge_zip_write(ForgeZipStream *s, FILE *file, u8 *data, size_t len) {
    if (!len) return true;
    if (s->ended || len > UINT_MAX) return false;
    size_t pos = 0;
    while (pos < len) {
        if (s->used == sizeof s->mask) {
            u8 counter[16] = {0};
            ++s->block;
            for (unsigned i = 0; i < 4; i++) counter[i] = (u8)(s->block >> (8*i));
            if (mbedtls_aes_crypt_ecb(&s->aes, MBEDTLS_AES_ENCRYPT, counter, s->mask)) return false;
            s->used = 0;
        }
        size_t n = sizeof s->mask - s->used;
        if (n > len - pos) n = len - pos;
        for (size_t i = 0; i < n; i++) data[pos+i] ^= s->mask[s->used+i];
        s->used += n;
        pos += n;
    }
    s->inflate.next_in = data;
    s->inflate.avail_in = (uInt)len;
    do {
        s->inflate.next_out = s->out;
        s->inflate.avail_out = sizeof s->out;
        int rc = inflate(&s->inflate, Z_NO_FLUSH);
        size_t n = sizeof s->out - s->inflate.avail_out;
        if (n > s->limit - s->plain || fwrite(s->out, 1, n, file) != n) return false;
        if (s->sha && n) mbedtls_sha256_update_ret(s->sha, s->out, n);
        s->plain += n;
        if (rc == Z_STREAM_END) { s->ended = true; return s->inflate.avail_in == 0; }
        if (rc == Z_BUF_ERROR && !n && !s->inflate.avail_in) break;
        if (rc != Z_OK) return false;
    } while (s->inflate.avail_in || !s->inflate.avail_out);
    return true;
}
