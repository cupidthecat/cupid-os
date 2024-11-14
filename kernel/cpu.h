#ifndef CPU_H
#define CPU_H

#include "types.h"

/**
 * rdtsc - Read Time-Stamp Counter
 * 
 * Reads the processor's time-stamp counter using the RDTSC instruction.
 * This counter increments with each CPU clock cycle and provides
 * high-precision timing capabilities.
 *
 * Returns:
 *   64-bit value where:
 *   - Upper 32 bits = TSC high bits
 *   - Lower 32 bits = TSC low bits
 *
 * Note: The actual time duration of a tick depends on the CPU frequency.
 * For accurate timing, the CPU frequency should be calibrated first.
 */
static inline uint64_t rdtsc(void) {
    uint32_t low, high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

/**
 * Additional CPU-related functions could go here:
 * - CPU feature detection
 * - CPU frequency measurement
 * - Cache control
 * - CPU identification
 * etc.
 */

#endif 