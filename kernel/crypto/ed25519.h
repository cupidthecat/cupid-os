#ifndef CUPID_TLS_ED25519_H
#define CUPID_TLS_ED25519_H

#include "types.h"

/* Ed25519 (RFC 8032) signature verification, verify-only. PureEd25519.
 * Returns 1 on valid signature, 0 on invalid/malformed. Inputs are
 * public, so timing side-channels are not in scope.*/
int ed25519_verify(const uint8_t pub[32],
                   const uint8_t *msg, uint32_t msg_len,
                   const uint8_t sig[64]);

#endif
