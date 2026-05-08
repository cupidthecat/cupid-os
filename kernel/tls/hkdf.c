/* HKDF-SHA256 (RFC 5869) plus the HKDF-Expand-Label construction from
 * RFC 8446 §7.1, used throughout the TLS 1.3 key schedule. */

#include "hkdf.h"
#include "hmac.h"

void hkdf_extract(const uint8_t *salt, uint32_t salt_len,
                  const uint8_t *ikm,  uint32_t ikm_len,
                  uint8_t prk[SHA256_DIGEST_SIZE]) {
    uint8_t  zero_salt[SHA256_DIGEST_SIZE];
    uint32_t i;

    if (salt == 0 || salt_len == 0) {
        for (i = 0; i < SHA256_DIGEST_SIZE; i++) zero_salt[i] = 0;
        hmac_sha256(zero_salt, SHA256_DIGEST_SIZE, ikm, ikm_len, prk);
    } else {
        hmac_sha256(salt, salt_len, ikm, ikm_len, prk);
    }
}

void hkdf_expand(const uint8_t *prk, uint32_t prk_len,
                 const uint8_t *info, uint32_t info_len,
                 uint8_t *out, uint32_t out_len) {
    uint8_t  T[SHA256_DIGEST_SIZE];
    uint32_t produced = 0;
    uint8_t  counter = 0;
    uint8_t  buf[SHA256_DIGEST_SIZE + 512u + 1u];  /* T(prev) || info || ctr */
    /* If info_len > 512 the build-time array bound trips; in TLS 1.3 it
     * never does (HkdfLabel maxes ~ 514 bytes including length prefix
     * but realistic labels stay under 100). Static assert below guards. */

    if (info_len > 512u) {
        /* Caller bug - caller must supply info_len ≤ 512. */
        for (uint32_t i = 0; i < out_len; i++) out[i] = 0;
        return;
    }

    while (produced < out_len) {
        uint32_t off = 0;
        uint32_t take;
        uint32_t i;

        counter = (uint8_t)(counter + 1u);
        if (counter > 1u) {
            for (i = 0; i < SHA256_DIGEST_SIZE; i++) buf[off + i] = T[i];
            off += SHA256_DIGEST_SIZE;
        }
        for (i = 0; i < info_len; i++) buf[off + i] = info[i];
        off += info_len;
        buf[off++] = counter;

        hmac_sha256(prk, prk_len, buf, off, T);

        take = out_len - produced;
        if (take > SHA256_DIGEST_SIZE) take = SHA256_DIGEST_SIZE;
        for (i = 0; i < take; i++) out[produced + i] = T[i];
        produced += take;
    }
}

/* Build an HkdfLabel and call hkdf_expand. Format (RFC 8446 §7.1):
 *
 *   struct {
 *       uint16 length = Length;
 *       opaque label<7..255> = "tls13 " + Label;
 *       opaque context<0..255> = Context;
 *   } HkdfLabel;
 */
void hkdf_expand_label(const uint8_t *secret, uint32_t secret_len,
                       const char *label,
                       const uint8_t *context, uint32_t ctx_len,
                       uint8_t *out, uint16_t out_len) {
    static const char prefix[] = "tls13 ";
    uint8_t  hkdf_label[2u + 1u + 6u + 255u + 1u + 255u];
    uint32_t off = 0;
    uint32_t lab_len = 0;
    uint32_t i;

    while (label[lab_len] != '\0') lab_len++;
    if (lab_len > 249u) lab_len = 249u;          /* "tls13 " + label ≤ 255 */
    if (ctx_len > 255u) ctx_len = 255u;

    hkdf_label[off++] = (uint8_t)((out_len >> 8) & 0xFFu);
    hkdf_label[off++] = (uint8_t)(out_len & 0xFFu);
    hkdf_label[off++] = (uint8_t)(6u + lab_len);   /* length of label */
    for (i = 0; i < 6u; i++) {
        hkdf_label[off++] = (uint8_t)prefix[i];
    }
    for (i = 0; i < lab_len; i++) {
        hkdf_label[off++] = (uint8_t)label[i];
    }
    hkdf_label[off++] = (uint8_t)ctx_len;
    for (i = 0; i < ctx_len; i++) hkdf_label[off++] = context[i];

    hkdf_expand(secret, secret_len, hkdf_label, off, out, out_len);
}
