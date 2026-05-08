#ifndef CUPID_TLS_HKDF_H
#define CUPID_TLS_HKDF_H

#include "types.h"
#include "sha256.h"

/* RFC 5869 HKDF over SHA-256. */

void hkdf_extract(const uint8_t *salt, uint32_t salt_len,
                  const uint8_t *ikm,  uint32_t ikm_len,
                  uint8_t prk[SHA256_DIGEST_SIZE]);

/* `out_len` must be ≤ 255 * 32 = 8160. */
void hkdf_expand(const uint8_t *prk, uint32_t prk_len,
                 const uint8_t *info, uint32_t info_len,
                 uint8_t *out, uint32_t out_len);

/* RFC 8446 §7.1 HKDF-Expand-Label.
 * `label` is a NUL-terminated ASCII string; "tls13 " is prepended internally.
 * `context` may be NULL when ctx_len == 0. */
void hkdf_expand_label(const uint8_t *secret, uint32_t secret_len,
                       const char *label,
                       const uint8_t *context, uint32_t ctx_len,
                       uint8_t *out, uint16_t out_len);

#endif
