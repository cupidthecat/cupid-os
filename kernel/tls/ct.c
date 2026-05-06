#include "ct.h"

int ct_memcmp(const uint8_t *a, const uint8_t *b, uint32_t n) {
    uint8_t  diff = 0;
    uint32_t i;
    for (i = 0; i < n; i++) diff = (uint8_t)(diff | (a[i] ^ b[i]));
    return (int)diff;
}

uint8_t ct_eq_u8(uint8_t a, uint8_t b) {
    /* x == 0 iff (~x & (x-1)) MSB is 1; for u8 that means propagate via
     * arithmetic over u32 to avoid promotion surprises. */
    uint32_t x = (uint32_t)(a ^ b);
    /* x == 0 → return 0xFFu, else 0x00u. */
    x = (x - 1u) & ~x;
    return (uint8_t)((x >> 31) * 0xFFu);
}

uint8_t ct_select_u8(uint8_t mask, uint8_t a, uint8_t b) {
    return (uint8_t)((mask & a) | ((uint8_t)(~mask) & b));
}

void ct_wipe(volatile void *p, uint32_t len) {
    volatile uint8_t *q = (volatile uint8_t *)p;
    uint32_t i;
    for (i = 0; i < len; i++) q[i] = 0;
}
