/* Embedded trust anchors for TLS chain validation.
 *
 * Populated by `tools/fetch_ca_bundle.sh`. Run that script (host-side,
 * needs curl + openssl) to generate `kernel/tls/tls_ca_bundle_data.c`,
 * which provides the actual DER blobs and a populated TLS_CA_BUNDLE
 * array. Without that file, the bundle is empty and chain verification
 * always returns X509_ERR_UNKNOWN_ROOT — which is the correct, safe
 * behavior for an unconfigured trust store.
 *
 * The generator emits two roots by default:
 *   - ISRG Root X1   (RSA-4096) — Let's Encrypt
 *   - DigiCert Global Root G2 (RSA-2048) — most CDNs
 */

#include "x509_chain.h"

/* Weak default: empty bundle. Override by linking
 * `tls_ca_bundle_data.o` after running the generator. */
__attribute__((weak)) const ca_root_t TLS_CA_BUNDLE[1] = { { 0, 0, 0 } };
__attribute__((weak)) const uint32_t  TLS_CA_BUNDLE_COUNT = 0u;
