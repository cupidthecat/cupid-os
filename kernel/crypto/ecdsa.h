#ifndef CUPID_TLS_ECDSA_H
#define CUPID_TLS_ECDSA_H

#include "types.h"
#include "p256.h"

/* ECDSA-P256 signature verification (FIPS 186-4 §6.4.2).
 *
 * Inputs are big-endian byte strings. Signatures are already split into
 * (r, s) components by the caller (parsed from the SEC1 / DER encoding).
 *
 * Returns 0 on success, -1 on any reject - invalid r/s range, point at
 * infinity, or signature mismatch.
 */
int ecdsa_p256_verify(const p256_aff_t *pubkey,
                      const uint8_t *hash,    uint32_t hash_len,
                      const uint8_t *r_be,    uint32_t r_len,
                      const uint8_t *s_be,    uint32_t s_len);

/* ECDSA-P256 signing for SSH host keys.
 * `priv_be32` is the private scalar in big-endian form.
 * `hash` is the already-computed message digest.
 * Writes fixed-width big-endian r/s values. Returns 0 on success. */
int ecdsa_p256_sign(const uint8_t priv_be32[32],
                    const uint8_t *hash, uint32_t hash_len,
                    uint8_t r_be32[32], uint8_t s_be32[32]);

#endif
