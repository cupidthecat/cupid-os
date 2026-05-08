#ifndef CUPID_TLS_CT_H
#define CUPID_TLS_CT_H

#include "../types.h"

/* Constant-time helpers - used wherever a timing leak could hand an
 * attacker secret material (MAC tags, key-byte compares, padding checks). */

/* Returns 0 if a[0..n-1] == b[0..n-1], else nonzero. Time is independent
 * of the position of the first mismatch. */
int ct_memcmp(const uint8_t *a, const uint8_t *b, uint32_t n);

/* Returns 0xFFu when a == b, else 0x00u. */
uint8_t ct_eq_u8(uint8_t a, uint8_t b);

/* Returns a if mask == 0xFFu, b if mask == 0x00u. */
uint8_t ct_select_u8(uint8_t mask, uint8_t a, uint8_t b);

/* Wipe `len` bytes via volatile writes - defends against compiler
 * dead-store elimination. Use for keys, IVs, transient secrets. */
void ct_wipe(volatile void *p, uint32_t len);

#endif
