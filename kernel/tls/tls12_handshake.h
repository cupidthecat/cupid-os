#ifndef CUPID_TLS12_HANDSHAKE_H
#define CUPID_TLS12_HANDSHAKE_H

#include "types.h"
#include "tls_ctx.h"

/* TLS 1.2 handshake driver.
 *
 * Entered from tls_handshake_client() after parsing a ServerHello whose
 * cipher suite is one of the 1.2 ECDHE+AEAD suites we offered. By the
 * time this is called the ClientHello is in the transcript hash and the
 * ServerHello is too (caller appends both before dispatching).
 *
 * Returns TLS_ERR_OK or a TLS_ERR_* / X509_ERR_* (negative). */
int tls12_handshake_client(tls_ctx_t *ctx);

#endif
