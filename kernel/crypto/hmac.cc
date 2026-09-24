/* HMAC-SHA256 (RFC 2104) - used by HKDF and by the TLS 1.2 PRF. */

#include "hmac.h"

void hmac_sha256(const uint8_t *key, uint32_t klen,
                 const uint8_t *msg, uint32_t mlen,
                 uint8_t out[SHA256_DIGEST_SIZE]) {
    uint8_t      ikey[SHA256_BLOCK_SIZE];
    uint8_t      okey[SHA256_BLOCK_SIZE];
    uint8_t      kbuf[SHA256_BLOCK_SIZE];
    uint8_t      inner[SHA256_DIGEST_SIZE];
    sha256_ctx_t ctx;
    uint32_t     i;

    /* If key longer than block, hash it down to 32 bytes. Else zero-pad. */
    if (klen > SHA256_BLOCK_SIZE) {
        sha256(key, klen, kbuf);
        for (i = SHA256_DIGEST_SIZE; i < SHA256_BLOCK_SIZE; i++) kbuf[i] = 0;
    } else {
        for (i = 0; i < klen; i++) kbuf[i] = key[i];
        for (i = klen; i < SHA256_BLOCK_SIZE; i++) kbuf[i] = 0;
    }

    for (i = 0; i < SHA256_BLOCK_SIZE; i++) {
        ikey[i] = (uint8_t)(kbuf[i] ^ 0x36u);
        okey[i] = (uint8_t)(kbuf[i] ^ 0x5cu);
    }

    sha256_init(&ctx);
    sha256_update(&ctx, ikey, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, msg, mlen);
    sha256_final(&ctx, inner);

    sha256_init(&ctx);
    sha256_update(&ctx, okey, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, inner, SHA256_DIGEST_SIZE);
    sha256_final(&ctx, out);
}
