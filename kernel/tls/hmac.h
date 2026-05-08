#ifndef CUPID_TLS_HMAC_H
#define CUPID_TLS_HMAC_H

#include "../types.h"
#include "sha256.h"

/* HMAC-SHA256 (RFC 2104). Output is always 32 bytes. */
void hmac_sha256(const uint8_t *key, uint32_t klen,
                 const uint8_t *msg, uint32_t mlen,
                 uint8_t out[SHA256_DIGEST_SIZE]);

#endif
