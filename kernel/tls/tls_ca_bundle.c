/* Embedded trust anchors for TLS chain validation.
 *
 * `tools/fetch_ca_bundle_mozilla.sh` generates
 * `kernel/tls/tls_ca_bundle_data.c` from curl.se's Mozilla bundle. The
 * tracked file currently contains a curated set of 39 roots and needs
 * curl, OpenSSL, and xxd when it is refreshed.
 *
 * Without the generated object, the weak bundle below is empty. The
 * current lenient verifier can still accept a chain after its time and
 * hostname checks, but it cannot authenticate that chain against an
 * embedded root.
 */

#include "x509_chain.h"

/* Weak default: empty bundle. Linking `tls_ca_bundle_data.o` supplies
 * the tracked strong definitions. */
__attribute__((weak)) const ca_root_t TLS_CA_BUNDLE[1] = { { 0, 0, 0 } };
__attribute__((weak)) const uint32_t  TLS_CA_BUNDLE_COUNT = 0u;
