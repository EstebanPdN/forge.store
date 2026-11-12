#pragma once

// PBKDF2-HMAC-SHA256, one 32-byte output block and the FM01 16-byte salt.
// Reuse the SHA states after the 64-byte HMAC pads. The generic mbedTLS HMAC
// wrapper hashes those pads again on every iteration on this toolchain.
// This produces the same key, with the same iteration count and saved-key format.
static int forge_derive_key(const unsigned char *pass, size_t length,
                            const unsigned char salt[16], unsigned iterations,
                            unsigned char out[32]) {
    if (length > 64 || !iterations) return -1;
    mbedtls_sha256_context inner, outer, work;
    unsigned char pad[64], input[20], u[32];
    int err = 0;
    mbedtls_sha256_init(&inner); mbedtls_sha256_init(&outer); mbedtls_sha256_init(&work);
    memset(pad, 0x36, sizeof pad);
    for (size_t i=0; i<length; i++) pad[i] ^= pass[i];
    if ((err=mbedtls_sha256_starts_ret(&inner,0)) ||
        (err=mbedtls_sha256_update_ret(&inner,pad,sizeof pad))) goto end;
    for (size_t i=0; i<sizeof pad; i++) pad[i] ^= 0x36 ^ 0x5c;
    if ((err=mbedtls_sha256_starts_ret(&outer,0)) ||
        (err=mbedtls_sha256_update_ret(&outer,pad,sizeof pad))) goto end;
    memcpy(input,salt,16); memset(input+16,0,4); input[19]=1;
    memset(out,0,32);
    for (unsigned round=0; round<iterations; round++) {
        mbedtls_sha256_clone(&work,&inner);
        if ((err=mbedtls_sha256_update_ret(&work,round?u:input,round?32:20)) ||
            (err=mbedtls_sha256_finish_ret(&work,u))) goto end;
        mbedtls_sha256_clone(&work,&outer);
        if ((err=mbedtls_sha256_update_ret(&work,u,32)) ||
            (err=mbedtls_sha256_finish_ret(&work,u))) goto end;
        for (unsigned i=0; i<32; i++) out[i] ^= u[i];
    }
end:
    mbedtls_sha256_free(&inner); mbedtls_sha256_free(&outer); mbedtls_sha256_free(&work);
    wipe(pad,sizeof pad); wipe(input,sizeof input); wipe(u,sizeof u);
    return err;
}
