#ifndef CUPID_TLS_POLY1305_H
#define CUPID_TLS_POLY1305_H

#include "types.h"

#define POLY1305_TAG_SIZE 16u
#define POLY1305_KEY_SIZE 32u

typedef struct {
    uint32_t r[5];
    uint32_t h[5];
    uint32_t pad[4];
    uint8_t  buffer[16];
    uint32_t buflen;
    uint8_t  finished;
} poly1305_ctx_t;

void poly1305_init(poly1305_ctx_t *ctx, const uint8_t key[POLY1305_KEY_SIZE]);
void poly1305_update(poly1305_ctx_t *ctx, const uint8_t *m, uint32_t len);
void poly1305_final(poly1305_ctx_t *ctx, uint8_t tag[POLY1305_TAG_SIZE]);

/* One-shot. */
void poly1305_auth(uint8_t tag[POLY1305_TAG_SIZE],
                   const uint8_t *m, uint32_t len,
                   const uint8_t key[POLY1305_KEY_SIZE]);

/* Constant-time tag verify; returns 1 if tag matches, 0 otherwise. */
int poly1305_verify(const uint8_t a[POLY1305_TAG_SIZE],
                    const uint8_t b[POLY1305_TAG_SIZE]);

#endif
