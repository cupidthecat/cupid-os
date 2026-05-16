#ifndef CUPID_TLS_X25519_H
#define CUPID_TLS_X25519_H

#include "types.h"

/* RFC 7748 §5 X25519. Both inputs and output are 32-byte little-endian
 * canonical encodings of u-coordinates / scalars. The function is the
 * Diffie-Hellman primitive - call once with the peer's u as `peer_u` to
 * derive a shared secret, and once with the standard base point u=9
 * (X25519_BASE_POINT) to produce a public key.*/

extern const uint8_t X25519_BASE_POINT[32];

void x25519(uint8_t shared_or_pub[32],
            const uint8_t scalar[32],
            const uint8_t peer_u[32]);

#endif
