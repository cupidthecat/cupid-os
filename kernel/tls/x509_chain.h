#ifndef CUPID_TLS_X509_CHAIN_H
#define CUPID_TLS_X509_CHAIN_H

#include "../types.h"
#include "x509.h"

#define X509_CHAIN_MAX_DEPTH 4

#define X509_OK                  0
#define X509_ERR_PARSE          -1
#define X509_ERR_BAD_SIG        -2
#define X509_ERR_EXPIRED        -3
#define X509_ERR_NOT_YET_VALID  -4
#define X509_ERR_HOSTNAME       -5
#define X509_ERR_UNKNOWN_ROOT   -6
#define X509_ERR_DEPTH          -7
#define X509_ERR_BAD_ALG        -8
#define X509_ERR_NOT_CA         -9
#define X509_ERR_NO_TIME       -10

typedef struct {
    x509_cert_t certs[X509_CHAIN_MAX_DEPTH];
    uint32_t    n;
} x509_chain_t;

void x509_chain_init(x509_chain_t *chain);

/* Append a cert from a DER buffer. Buffer must outlive the chain.
 * Returns 0 or X509_ERR_*. */
int x509_chain_add(x509_chain_t *chain,
                   const uint8_t *der, uint32_t der_len);

/* Validate the chain against the embedded CA bundle:
 *   - parsed leaf at certs[0]; intermediates at certs[1..n-1].
 *   - find a root in TLS_CA_BUNDLE whose subject equals certs[n-1].issuer.
 *   - for each adjacent pair, verify chain sig.
 *   - validity window includes now_epoch (or skip if now_epoch == 0).
 *   - leaf hostname matches `host`.
 *
 * Returns X509_OK on success. */
int x509_chain_verify(const x509_chain_t *chain,
                      const char *host,
                      uint64_t now_epoch);

/* CA bundle entry: a single trusted root (DER-encoded). */
typedef struct {
    const char    *name;
    const uint8_t *der;
    uint32_t       der_len;
} ca_root_t;

/* Defined in tls_ca_bundle.c. */
extern const ca_root_t TLS_CA_BUNDLE[];
extern const uint32_t   TLS_CA_BUNDLE_COUNT;

#endif
