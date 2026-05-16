#include "tls_kdf.h"
#include "hkdf.h"
#include "hmac.h"

void tls_kdf_derive_secret(const uint8_t secret[32],
                           const char *label,
                           const uint8_t *transcript_hash,
                           uint32_t transcript_len,
                           uint8_t out[32]) {
    hkdf_expand_label(secret, 32u, label,
                      transcript_hash, transcript_len,
                      out, 32u);
}

void tls_kdf_traffic_keys(const uint8_t traffic_secret[32],
                          uint8_t *key_out, uint32_t key_len,
                          uint8_t iv_out[12]) {
    hkdf_expand_label(traffic_secret, 32u, "key", NULL, 0u,
                      key_out, (uint16_t)key_len);
    hkdf_expand_label(traffic_secret, 32u, "iv",  NULL, 0u, iv_out,  12u);
}

void tls_kdf_finished_key(const uint8_t traffic_secret[32],
                          uint8_t finished_key[32]) {
    hkdf_expand_label(traffic_secret, 32u, "finished", NULL, 0u,
                      finished_key, 32u);
}

/* TLS 1.2 PRF (RFC 5246 §5):
 *   P_hash(secret, seed) = HMAC(secret, A(1) || seed) ||
 *                          HMAC(secret, A(2) || seed) || ...
 *   where A(0) = seed, A(i) = HMAC(secret, A(i-1)).
 *   PRF(secret, label, seed) = P_SHA256(secret, label || seed).*/
void tls12_prf(const uint8_t *secret,  uint32_t secret_len,
               const char    *label,
               const uint8_t *seed,    uint32_t seed_len,
               uint8_t       *out,     uint32_t out_len) {
    uint8_t A[32];
    uint8_t buf[256];
    uint32_t label_len = 0;
    uint32_t off = 0;
    uint32_t i;

    while (label[label_len] != '\0') label_len++;
    /* assemble label || seed once */
    for (i = 0; i < label_len; i++) buf[32u + i] = (uint8_t)label[i];
    for (i = 0; i < seed_len;  i++) buf[32u + label_len + i] = seed[i];

    /* A(1) = HMAC(secret, label || seed). */
    hmac_sha256(secret, secret_len,
                buf + 32u, label_len + seed_len, A);

    while (off < out_len) {
        uint8_t block[32];
        uint32_t take;
        for (i = 0; i < 32u; i++) buf[i] = A[i];
        hmac_sha256(secret, secret_len,
                    buf, 32u + label_len + seed_len, block);
        take = out_len - off;
        if (take > 32u) take = 32u;
        for (i = 0; i < take; i++) out[off + i] = block[i];
        off += take;
        if (off < out_len) {
            uint8_t newA[32];
            hmac_sha256(secret, secret_len, A, 32u, newA);
            for (i = 0; i < 32u; i++) A[i] = newA[i];
        }
    }
}
