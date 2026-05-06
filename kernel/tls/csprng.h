#ifndef CUPID_TLS_CSPRNG_H
#define CUPID_TLS_CSPRNG_H

#include "../types.h"

/* Initialize the CSPRNG. Must be called once at boot, after serial is up
 * (so seeding messages are visible) and before any consumer requests
 * randomness. Pulls entropy from RDRAND if available, else rdtsc jitter. */
void csprng_init(void);

/* Fill `buf` with `len` cryptographically-strong random bytes.
 * Safe to call before init — returns zeros and is a no-op (callers
 * must check return values of consumers like TLS that depend on real
 * entropy). */
void crypto_random_bytes(uint8_t *buf, uint32_t len);

/* Mix additional entropy into the internal pool. May be called from
 * IRQ-context entropy sources (RX-packet timing, user keystroke jitter,
 * /dev/random writes). Cheap. */
void crypto_random_add_entropy(const uint8_t *buf, uint32_t len);

#endif
