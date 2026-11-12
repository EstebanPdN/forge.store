// Extract the console's own DSP component from HOME Menu, never from a download.
// Format reference: zoogie/DSP1 (HOME .code archive path, BLZ and DSP1 sections).
#include "dsp_dump.h"
#include "catalog.h"
#include <mbedtls/sha256.h>
static u32 le(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static u8 *unpack(const u8 *src, size_t n, size_t *length) {
    if (n < 8)
        return NULL;
    u32 tail = le(src + n - 8), extra = le(src + n - 4);
    size_t header = tail >> 24, packed = tail & 0xffffff;
    if (header < 8 || header > n || packed > n || packed < header || extra > 16 * 1024 * 1024 || n + extra > 24 * 1024 * 1024)
        return NULL;
    size_t size = n + extra, in = n - header, limit = n - packed, out = size;
    u8 *dst = calloc(1, size);
    if (!dst)
        return NULL;
    memcpy(dst, src, n);
    while (in > limit) {
        u8 flags = src[--in];
        for (int bit = 7; bit >= 0 && in > limit; bit--) {
            if (flags & (1 << bit)) {
                if (in < 2)
                    goto bad;
                in -= 2;
                unsigned code = src[in] | src[in + 1] << 8, cnt = (code >> 12) + 3, off = (code & 4095) + 2;
                if (out < cnt)
                    goto bad;
                while (cnt--) {
                    if (out + off >= size)
                        goto bad;
                    dst[out - 1] = dst[out + off];
                    out--;
                }
            } else {
                if (!out)
                    goto bad;
                dst[--out] = src[--in];
            }
        }
    }
    *length = size;
    return dst;
bad:
    free(dst);
    return NULL;
}
static bool valid_dsp(const u8 *p, size_t n) {
    if (n < 0x300 || memcmp(p + 0x100, "DSP1", 4) || le(p + 0x104) != n)
        return false;
    int checked = 0;
    for (int i = 0; i < 10; i++) {
        const u8 *s = p + 0x120 + i * 0x30;
        u32 off = le(s), len = le(s + 8);
        if (!len)
            continue;
        if (off > n || len > n - off)
            return false;
        u8 hash[32];
        if (mbedtls_sha256_ret(p + off, len, hash, 0) || memcmp(hash, s + 16, 32))
            return false;
        checked++;
    }
    return checked > 0;
}
bool dsp_dump(char *error, size_t error_size) {
    if (path_exists("sdmc:/3ds/dspfirm.cdc"))
        return true;
    const u64 titles[] = {0x0004003000008f02ULL, 0x0004003000008202ULL, 0x0004003000009802ULL,
                          0x000400300000a102ULL, 0x000400300000a902ULL, 0x000400300000b102ULL};
    for (unsigned i = 0; i < 6; i++) {
        u32 archive[] = {titles[i], titles[i] >> 32, 0, 0}, code[] = {0, 0, 2, 0x646f632e, 0x65};
        Handle h;
        Result r = FSUSER_OpenFileDirectly(&h, (FS_ArchiveID)0x2345678a, (FS_Path){PATH_BINARY, sizeof archive, archive},
                                           (FS_Path){PATH_BINARY, sizeof code, code}, FS_OPEN_READ, 0);
        if (R_FAILED(r))
            continue;
        u64 n = 0;
        FSFILE_GetSize(h, &n);
        if (n < 0x300 || n > 16 * 1024 * 1024) {
            FSFILE_Close(h);
            continue;
        }
        u8 *raw = malloc(n);
        u32 got = 0;
        r = raw ? FSFILE_Read(h, &got, 0, raw, n) : -1;
        FSFILE_Close(h);
        if (R_FAILED(r) || got != n) {
            free(raw);
            continue;
        }
        size_t len = 0;
        u8 *data = unpack(raw, n, &len);
        if (!data) {
            data = raw;
            len = n;
            raw = NULL;
        }
        free(raw);
        for (size_t off = 0x100; off + 8 < len; off += 4)
            if (!memcmp(data + off, "DSP1", 4)) {
                size_t size = le(data + off + 4), start = off - 0x100;
                if (size > len - start || !valid_dsp(data + start, size))
                    continue;
                FILE *f = fopen("sdmc:/3ds/dspfirm.cdc.tmp", "wb");
                bool ok = false;
                if (f) {
                    ok = fwrite(data + start, 1, size, f) == size;
                    ok = fclose(f) == 0 && ok;
                    if (ok)
                        ok = rename("sdmc:/3ds/dspfirm.cdc.tmp", "sdmc:/3ds/dspfirm.cdc") == 0;
                }
                free(data);
                if (ok) {
                    path_cache_clear();
                    return true;
                }
                goto failed;
            }
        free(data);
    }
failed:
    snprintf(error, error_size, "%s",
             S.lang == 0 ? "No se pudo extraer DSP. Rosalina > Miscellaneous > Dump DSP firmware."
                         : "DSP extraction failed. Rosalina > Miscellaneous > Dump DSP firmware.");
    return false;
}
