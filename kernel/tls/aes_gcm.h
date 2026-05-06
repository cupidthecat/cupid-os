#ifndef CUPID_TLS_AES_GCM_H
#define CUPID_TLS_AES_GCM_H

#include "../types.h"
#include "aes.h"

/* AES-128-GCM (NIST SP 800-38D / RFC 5116).
 *
 * GCM = AES-CTR for confidentiality + GHASH for authentication.
 * Tag size hardcoded to 16 bytes (full tag). Only 12-byte nonces are
 * supported (the canonical TLS shape; matches RFC 5116 §5.1). */

#define AES128_GCM_KEY_SIZE   16u
#define AES128_GCM_NONCE_SIZE 12u
#define AES128_GCM_TAG_SIZE   16u

void aes128_gcm_seal(const uint8_t key[AES128_GCM_KEY_SIZE],
                     const uint8_t nonce[AES128_GCM_NONCE_SIZE],
                     const uint8_t *aad, uint32_t aad_len,
                     const uint8_t *pt, uint32_t pt_len,
                     uint8_t *ct_out,
                     uint8_t tag_out[AES128_GCM_TAG_SIZE]);

/* Returns 1 on tag-OK, 0 on tag-fail. On fail caller MUST discard pt_out. */
int aes128_gcm_open(const uint8_t key[AES128_GCM_KEY_SIZE],
                    const uint8_t nonce[AES128_GCM_NONCE_SIZE],
                    const uint8_t *aad, uint32_t aad_len,
                    const uint8_t *ct, uint32_t ct_len,
                    const uint8_t tag[AES128_GCM_TAG_SIZE],
                    uint8_t *pt_out);

#endif
