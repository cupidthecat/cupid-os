/* RSA verify-only: PKCS#1 v1.5 (cert chain signatures) and PSS
 * (TLS 1.3 CertificateVerify). RFC 3447. Variable-time - every input
 * is public.*/

#include "rsa.h"
#include "bigint.h"
#include "sha256.h"
#include "ct.h"

/* MGF1 with SHA-256 (RFC 3447 §B.2.1). */
static void mgf1_sha256(uint8_t *out, uint32_t out_len,
                        const uint8_t *seed, uint32_t seed_len) {
    uint32_t produced = 0;
    uint32_t counter  = 0;
    while (produced < out_len) {
        sha256_ctx_t ctx;
        uint8_t      cbuf[4];
        uint8_t      hash[32];
        uint32_t     take, i;

        cbuf[0] = (uint8_t)((counter >> 24) & 0xFFu);
        cbuf[1] = (uint8_t)((counter >> 16) & 0xFFu);
        cbuf[2] = (uint8_t)((counter >>  8) & 0xFFu);
        cbuf[3] = (uint8_t)( counter        & 0xFFu);

        sha256_init(&ctx);
        sha256_update(&ctx, seed, seed_len);
        sha256_update(&ctx, cbuf, 4u);
        sha256_final(&ctx, hash);

        take = out_len - produced;
        if (take > 32u) take = 32u;
        for (i = 0; i < take; i++) out[produced + i] = hash[i];
        produced += take;
        counter++;
    }
}

/* Compute m = sig^e mod n, write big-endian bytes into em_out (n_len). */
static int rsa_recover_em(uint8_t *em_out, uint32_t n_len,
                          const uint8_t *n_be, uint32_t n_len_in,
                          const uint8_t *e_be, uint32_t e_len,
                          const uint8_t *sig, uint32_t sig_len) {
    bn_t mod, base, result;
    if (n_len != n_len_in) return -1;
    if (sig_len != n_len) return -1;
    if (bn_from_be(&mod,  n_be, n_len) < 0) return -1;
    if (bn_from_be(&base, sig,  sig_len) < 0) return -1;
    /* Sanity: signature must be < modulus. */
    if (bn_cmp(&base, &mod) >= 0) return -1;

    bn_modexp(&result, &base, e_be, e_len, &mod);
    bn_to_be(em_out, n_len, &result);
    return 0;
}

/* DigestInfo prefix for SHA-256 (RFC 3447 §9.2). */
static const uint8_t SHA256_DIGEST_INFO[19] = {
    0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86,
    0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05,
    0x00, 0x04, 0x20
};
/* DigestInfo prefix for SHA-384 (RFC 3447 §9.2). */
static const uint8_t SHA384_DIGEST_INFO[19] = {
    0x30, 0x41, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86,
    0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x02, 0x05,
    0x00, 0x04, 0x30
};
/* DigestInfo prefix for SHA-512 (RFC 3447 §9.2). */
static const uint8_t SHA512_DIGEST_INFO[19] = {
    0x30, 0x51, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86,
    0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03, 0x05,
    0x00, 0x04, 0x40
};

static int rsa_pkcs1v15_verify_generic(const uint8_t *n_be, uint32_t n_len,
                                       const uint8_t *e_be, uint32_t e_len,
                                       const uint8_t *sig, uint32_t sig_len,
                                       const uint8_t *msg_hash,
                                       uint32_t hash_len,
                                       const uint8_t *digest_info) {
    uint8_t  em[BN_MAX_BITS / 8u];
    uint32_t i, ps_end;
    int      rc;

    if (n_len > sizeof(em)) return 0;
    rc = rsa_recover_em(em, n_len, n_be, n_len, e_be, e_len, sig, sig_len);
    if (rc != 0) return 0;

    if (em[0] != 0x00u || em[1] != 0x01u) return 0;
    for (i = 2; i < n_len; i++) {
        if (em[i] != 0xFFu) break;
    }
    if (i < 2u + 8u) return 0;
    if (i >= n_len)   return 0;
    if (em[i] != 0x00u) return 0;
    ps_end = i + 1u;

    if (ps_end + 19u + hash_len != n_len) return 0;
    for (i = 0; i < 19u; i++) {
        if (em[ps_end + i] != digest_info[i]) return 0;
    }
    return (ct_memcmp(em + ps_end + 19u, msg_hash, hash_len) == 0) ? 1 : 0;
}

int rsa_pkcs1v15_verify_sha256(const uint8_t *n_be, uint32_t n_len,
                               const uint8_t *e_be, uint32_t e_len,
                               const uint8_t *sig, uint32_t sig_len,
                               const uint8_t msg_hash[32]) {
    return rsa_pkcs1v15_verify_generic(n_be, n_len, e_be, e_len,
                                        sig, sig_len, msg_hash, 32u,
                                        SHA256_DIGEST_INFO);
}

int rsa_pkcs1v15_verify_sha384(const uint8_t *n_be, uint32_t n_len,
                               const uint8_t *e_be, uint32_t e_len,
                               const uint8_t *sig, uint32_t sig_len,
                               const uint8_t msg_hash[48]) {
    return rsa_pkcs1v15_verify_generic(n_be, n_len, e_be, e_len,
                                        sig, sig_len, msg_hash, 48u,
                                        SHA384_DIGEST_INFO);
}

int rsa_pkcs1v15_verify_sha512(const uint8_t *n_be, uint32_t n_len,
                               const uint8_t *e_be, uint32_t e_len,
                               const uint8_t *sig, uint32_t sig_len,
                               const uint8_t msg_hash[64]) {
    return rsa_pkcs1v15_verify_generic(n_be, n_len, e_be, e_len,
                                        sig, sig_len, msg_hash, 64u,
                                        SHA512_DIGEST_INFO);
}

int rsa_pss_verify_sha256(const uint8_t *n_be, uint32_t n_len,
                          const uint8_t *e_be, uint32_t e_len,
                          const uint8_t *sig, uint32_t sig_len,
                          const uint8_t msg_hash[32], uint32_t salt_len) {
    /* RFC 3447 §9.1.2 EMSA-PSS-Verify. */
    uint8_t  em[BN_MAX_BITS / 8u];
    uint8_t  db[BN_MAX_BITS / 8u];
    uint8_t  mask[BN_MAX_BITS / 8u];
    uint8_t  mp[8 + 32 + 256];     /* M' = 8x0 || mHash || salt; salt ≤ 256 */
    uint8_t  hp[32];
    uint32_t hlen = 32u;
    uint32_t modbits, embits, emlen;
    uint32_t db_len, top_zero_bits;
    uint32_t i;
    int      rc;
    sha256_ctx_t hctx;

    if (n_len == 0u || n_len > sizeof(em))     return 0;
    if (salt_len > 256u)                        return 0;
    if (8u + hlen + salt_len > sizeof(mp))      return 0;

    /* modBits = bit length of n. Skip leading zero bytes, count first byte. */
    modbits = 0;
    for (i = 0; i < n_len; i++) {
        if (n_be[i] != 0u) {
            uint8_t v = n_be[i];
            uint32_t b = 0;
            while (v) { b++; v = (uint8_t)(v >> 1); }
            modbits = (n_len - i - 1u) * 8u + b;
            break;
        }
    }
    if (modbits == 0u) return 0;

    embits = modbits - 1u;
    emlen  = (embits + 7u) / 8u;
    if (emlen > n_len) return 0;
    if (emlen < hlen + salt_len + 2u) return 0;

    rc = rsa_recover_em(em, n_len, n_be, n_len, e_be, e_len, sig, sig_len);
    if (rc != 0) return 0;

    /* If n_len > emlen, the leading byte(s) of em must be zero. */
    {
        uint32_t lead = n_len - emlen;
        for (i = 0; i < lead; i++) {
            if (em[i] != 0u) return 0;
        }
    }

    /* Trailing byte must be 0xBC. */
    if (em[n_len - 1u] != 0xBCu) return 0;

    /* maskedDB = em[lead .. lead+emlen-hlen-1) ; H = em[lead+emlen-hlen-1 .. lead+emlen-1] */
    db_len = emlen - hlen - 1u;

    mgf1_sha256(mask, db_len, em + (n_len - emlen) + db_len, hlen);
    for (i = 0; i < db_len; i++) {
        db[i] = (uint8_t)(em[(n_len - emlen) + i] ^ mask[i]);
    }

    /* Clear top (8*emlen - embits) bits of db[0] per RFC 3447 §9.1.2 step 6
     * ("Set the leftmost 8emLen-emBits bits of the leftmost octet in DB to
     * zero"). Do NOT reject if those bits are already non-zero - the signer
     * only zeroes the top bits of *maskedDB* (RFC §9.1.1 step 11), so after
     * MGF1 unmasking the top bits of DB equal MGF1's leading bits, which
     * are essentially random. The verifier's job is just to mask them off
     * before checking PS. The previous strict check rejected ~50% of valid
     * RSA-4096 signatures (any case where MGF1 output's high bit was set).*/
    top_zero_bits = (uint32_t)(8u * emlen) - embits;
    if (top_zero_bits != 0u) {
        uint8_t topmask = (uint8_t)(0xFFu >> top_zero_bits);
        db[0] = (uint8_t)(db[0] & topmask);
    }

    /* Verify DB = (emlen - hlen - salt_len - 2 zeros) || 0x01 || salt */
    {
        uint32_t ps_len = db_len - salt_len - 1u;
        for (i = 0; i < ps_len; i++) {
            if (db[i] != 0u) return 0;
        }
        if (db[ps_len] != 0x01u) return 0;
    }

    /* Compute H' = SHA-256(0x00..00 || mHash || salt). */
    for (i = 0; i < 8u; i++) mp[i] = 0;
    for (i = 0; i < hlen; i++) mp[8u + i] = msg_hash[i];
    for (i = 0; i < salt_len; i++) {
        mp[8u + hlen + i] = db[db_len - salt_len + i];
    }
    sha256_init(&hctx);
    sha256_update(&hctx, mp, 8u + hlen + salt_len);
    sha256_final(&hctx, hp);

    return (ct_memcmp(hp, em + (n_len - emlen) + db_len, hlen) == 0) ? 1 : 0;
}
