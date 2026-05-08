#ifndef CUPID_TLS_CHACHA20POLY1305_H
#define CUPID_TLS_CHACHA20POLY1305_H

#include "types.h"

/* RFC 8439 §2.8 ChaCha20-Poly1305 AEAD. */

#define CHACHA20POLY1305_KEY_SIZE   32u
#define CHACHA20POLY1305_NONCE_SIZE 12u
#define CHACHA20POLY1305_TAG_SIZE   16u

/* Encrypt + authenticate. `ct_out` must have capacity `pt_len`. The
 * tag is written separately to `tag_out`. `aad` may be NULL when
 * `aad_len == 0`. */
void chacha20poly1305_seal(const uint8_t key[CHACHA20POLY1305_KEY_SIZE],
                           const uint8_t nonce[CHACHA20POLY1305_NONCE_SIZE],
                           const uint8_t *aad, uint32_t aad_len,
                           const uint8_t *pt, uint32_t pt_len,
                           uint8_t *ct_out,
                           uint8_t tag_out[CHACHA20POLY1305_TAG_SIZE]);

/* Verify tag and decrypt. Returns 1 on success, 0 on tag mismatch
 * (in which case `pt_out` content is undefined and the caller MUST
 * discard it). */
int chacha20poly1305_open(const uint8_t key[CHACHA20POLY1305_KEY_SIZE],
                          const uint8_t nonce[CHACHA20POLY1305_NONCE_SIZE],
                          const uint8_t *aad, uint32_t aad_len,
                          const uint8_t *ct, uint32_t ct_len,
                          const uint8_t tag[CHACHA20POLY1305_TAG_SIZE],
                          uint8_t *pt_out);

#endif
