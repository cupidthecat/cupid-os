#ifndef CUPID_TLS_X509_H
#define CUPID_TLS_X509_H

#include "../types.h"

/* Single X.509 cert parser. Stores spans into the caller's DER buffer
 * - no copies. The buffer must outlive the parsed cert. */

#define X509_MAX_SAN_DNS 16

typedef struct {
    /* RSA pubkey - modulus + exponent in canonical big-endian (no
     * leading zero). */
    const uint8_t *modulus;
    uint32_t       modulus_len;
    const uint8_t *exponent;
    uint32_t       exponent_len;
} x509_rsa_pubkey_t;

typedef struct {
    /* SEC1 uncompressed P-256 point: 0x04 || X(32) || Y(32) = 65 bytes.
     * Stored as a span into the cert's DER buffer. */
    const uint8_t *point;
    uint32_t       point_len;
} x509_ec_p256_pubkey_t;

#define X509_PK_NONE     0
#define X509_PK_RSA      1
#define X509_PK_EC_P256  2

typedef struct {
    int type;
    x509_rsa_pubkey_t     rsa;
    x509_ec_p256_pubkey_t ec;
} x509_pubkey_t;

typedef struct {
    const uint8_t *name;
    uint32_t       len;
} x509_san_t;

#define X509_SIG_NONE                 0
#define X509_SIG_RSA_PKCS1_SHA256     1
#define X509_SIG_RSA_PSS_SHA256       2
#define X509_SIG_ECDSA_P256_SHA256    3
#define X509_SIG_RSA_PKCS1_SHA384     4
#define X509_SIG_RSA_PKCS1_SHA512     5

typedef struct {
    /* Spans within the original DER buffer. */
    const uint8_t *raw;
    uint32_t       raw_len;
    const uint8_t *tbs;          /* TBSCertificate (signed bytes) */
    uint32_t       tbs_len;
    const uint8_t *sig;
    uint32_t       sig_len;

    /* Issuer + subject DN - kept as raw DER for byte-equality compare
     * of subject==issuer when matching against parents/roots. */
    const uint8_t *issuer;
    uint32_t       issuer_len;
    const uint8_t *subject;
    uint32_t       subject_len;

    /* Validity in seconds since Unix epoch. */
    uint64_t not_before;
    uint64_t not_after;

    /* Subject Public Key Info - RSA or EC-P256. */
    x509_pubkey_t pubkey;

    /* Signature algorithm - both the OID found in tbs.signatureAlg
     * and in the outer signatureAlgorithm must be identical and one
     * of the supported choices below. */
    int sig_alg;                  /* X509_SIG_* */

    /* SAN dNSName entries (slot 0..san_count-1 valid). */
    x509_san_t san[X509_MAX_SAN_DNS];
    uint32_t   san_count;

    /* Subject CN, used as fallback if SAN absent. */
    const uint8_t *cn;
    uint32_t       cn_len;

    uint8_t version;              /* 1, 2, or 3 */
    uint8_t is_ca;                /* basicConstraints CA:TRUE */
} x509_cert_t;

/* Parse a single DER-encoded certificate. Returns 0 on success.
 * On any malformedness - non-canonical lengths, oversize allocations,
 * unknown critical extensions, unsupported pubkey/sig alg, etc. -
 * returns -1. */
int x509_parse(const uint8_t *der, uint32_t der_len, x509_cert_t *out);

/* Hostname matcher (RFC 6125 §6.4): exact case-insensitive ASCII match
 * of host against any SAN dnsName, or a single leftmost-label wildcard
 * (e.g. "*.example.com" matches "foo.example.com" but not
 * "foo.bar.example.com" and not "example.com"). Falls back to subject
 * CN ONLY when no SAN entries exist. Returns 1 on match, 0 otherwise. */
int x509_match_hostname(const x509_cert_t *cert, const char *host);

/* DN byte-equality compare (used to test subject == parent issuer). */
int x509_dn_equal(const uint8_t *a, uint32_t alen,
                  const uint8_t *b, uint32_t blen);

#endif
