#ifndef CUPID_TLS_SHA256_H
#define CUPID_TLS_SHA256_H

#include "../types.h"

#define SHA256_BLOCK_SIZE 64u
#define SHA256_DIGEST_SIZE 32u

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  buffer[SHA256_BLOCK_SIZE];
    uint32_t buflen;
} sha256_ctx_t;

void sha256_init(sha256_ctx_t *ctx);
void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, uint32_t len);
void sha256_final(sha256_ctx_t *ctx, uint8_t out[SHA256_DIGEST_SIZE]);

/* Convenience: one-shot hash. */
void sha256(const uint8_t *data, uint32_t len,
            uint8_t out[SHA256_DIGEST_SIZE]);

#endif
