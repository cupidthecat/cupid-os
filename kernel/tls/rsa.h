#ifndef CUPID_TLS_RSA_H
#define CUPID_TLS_RSA_H

#include "../types.h"

/* Both functions: 1 on valid signature, 0 otherwise. Verify-only -
 * no decryption / signing API. Inputs are public, so variable-time
 * implementation is acceptable. */

int rsa_pkcs1v15_verify_sha256(const uint8_t *n_be, uint32_t n_len,
                               const uint8_t *e_be, uint32_t e_len,
                               const uint8_t *sig, uint32_t sig_len,
                               const uint8_t msg_hash[32]);

int rsa_pkcs1v15_verify_sha384(const uint8_t *n_be, uint32_t n_len,
                               const uint8_t *e_be, uint32_t e_len,
                               const uint8_t *sig, uint32_t sig_len,
                               const uint8_t msg_hash[48]);

int rsa_pkcs1v15_verify_sha512(const uint8_t *n_be, uint32_t n_len,
                               const uint8_t *e_be, uint32_t e_len,
                               const uint8_t *sig, uint32_t sig_len,
                               const uint8_t msg_hash[64]);

/* RFC 8446 mandates PSS for TLS 1.3 CertificateVerify. `salt_len` is
 * 32 for TLS 1.3 (= hash length, hLen). */
int rsa_pss_verify_sha256(const uint8_t *n_be, uint32_t n_len,
                          const uint8_t *e_be, uint32_t e_len,
                          const uint8_t *sig, uint32_t sig_len,
                          const uint8_t msg_hash[32], uint32_t salt_len);

#endif
