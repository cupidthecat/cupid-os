#ifndef CUPID_TLS_SHA512_H
#define CUPID_TLS_SHA512_H

#include "types.h"

/* SHA-512 produces 64-byte output; SHA-384 is the same algorithm with
 * different IV constants and a 48-byte truncation.*/

#define SHA512_BLOCK_SIZE  128u
#define SHA512_DIGEST_SIZE 64u
#define SHA384_DIGEST_SIZE 48u

typedef struct {
    uint64_t state[8];
    uint64_t bitlen_lo;   /* low 64 bits of bit count */
    uint64_t bitlen_hi;   /* high 64 bits (for messages > 2^64 bits) */
    uint8_t  buffer[SHA512_BLOCK_SIZE];
    uint32_t buflen;
} sha512_ctx_t;

void sha512_init(sha512_ctx_t *ctx);
void sha384_init(sha512_ctx_t *ctx);
void sha512_update(sha512_ctx_t *ctx, const uint8_t *data, uint32_t len);
void sha512_final(sha512_ctx_t *ctx, uint8_t out[SHA512_DIGEST_SIZE]);
void sha384_final(sha512_ctx_t *ctx, uint8_t out[SHA384_DIGEST_SIZE]);

/* Convenience: one-shot hash. */
void sha512(const uint8_t *data, uint32_t len, uint8_t out[SHA512_DIGEST_SIZE]);
void sha384(const uint8_t *data, uint32_t len, uint8_t out[SHA384_DIGEST_SIZE]);

#endif
