#ifndef CUPID_TLS_KDF_H
#define CUPID_TLS_KDF_H

#include "../types.h"

/* TLS 1.3 key schedule helpers (RFC 8446 §7).
 *
 *   Derive-Secret(secret, label, messages) =
 *       HKDF-Expand-Label(secret, label, SHA-256(messages), 32)
 *
 * The "messages" input is supplied as a precomputed transcript hash,
 * not the raw bytes. */

/* Derive-Secret. transcript_hash may be NULL when ctx_len == 0
 * (special "" empty-context case used by the early/derived secrets). */
void tls_kdf_derive_secret(const uint8_t secret[32],
                           const char *label,
                           const uint8_t *transcript_hash,
                           uint32_t transcript_len,
                           uint8_t out[32]);

/* Derive AEAD key (`key_len` bytes: 16 for AES-128-GCM, 32 for
 * ChaCha20-Poly1305) and IV (12) from a traffic secret. */
void tls_kdf_traffic_keys(const uint8_t traffic_secret[32],
                          uint8_t *key_out, uint32_t key_len,
                          uint8_t iv_out[12]);

/* Compute HKDF-Expand-Label(secret, "finished", "", 32). */
void tls_kdf_finished_key(const uint8_t traffic_secret[32],
                          uint8_t finished_key[32]);

/* TLS 1.2 PRF: P_SHA256(secret, label || seed) -> out_len bytes. */
void tls12_prf(const uint8_t *secret,  uint32_t secret_len,
               const char    *label,
               const uint8_t *seed,    uint32_t seed_len,
               uint8_t       *out,     uint32_t out_len);

#endif
