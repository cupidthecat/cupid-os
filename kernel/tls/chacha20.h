#ifndef CUPID_TLS_CHACHA20_H
#define CUPID_TLS_CHACHA20_H

#include "../types.h"

/* RFC 8439 ChaCha20 block function. Produces one 64-byte keystream
 * block from (key, counter, nonce). */
void chacha20_block(const uint8_t key[32], uint32_t counter,
                    const uint8_t nonce[12], uint8_t out[64]);

/* RFC 8439 ChaCha20 stream cipher (one-shot encrypt/decrypt).
 * `counter` is the starting block counter (0 for keystream-only,
 * 1 for AEAD payload per RFC 8439 §2.4). `in` and `out` may alias. */
void chacha20_xor(const uint8_t key[32], uint32_t counter,
                  const uint8_t nonce[12],
                  const uint8_t *in, uint8_t *out, uint32_t len);

#endif
