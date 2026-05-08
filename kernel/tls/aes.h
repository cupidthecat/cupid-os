#ifndef CUPID_TLS_AES_H
#define CUPID_TLS_AES_H

#include "../types.h"

/* AES-128 block cipher (FIPS 197).
 *
 * Implementation note: software AES via S-box table lookups. Cache-timing
 * leaks are theoretically possible against a co-resident attacker. For a
 * hobby OS without AES-NI / SMP-attacker model this is acceptable; for
 * hardened deployments either add AES-NI fast path or switch to a
 * bitsliced implementation.
 */

#define AES128_KEY_SIZE  16u
#define AES128_BLOCK     16u
#define AES128_NR        10u

typedef struct {
    /* 11 round keys * 16 bytes = 176. Stored as 44 32-bit words. */
    uint32_t rk[44];
} aes128_ctx_t;

void aes128_set_key(aes128_ctx_t *ctx, const uint8_t key[AES128_KEY_SIZE]);
void aes128_encrypt_block(const aes128_ctx_t *ctx,
                          const uint8_t in[AES128_BLOCK],
                          uint8_t out[AES128_BLOCK]);

#endif
