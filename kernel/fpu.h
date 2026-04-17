#ifndef FPU_H
#define FPU_H

#include "types.h"

/* Enable x87 + SSE/SSE2 hardware. MUST be called as the FIRST statement
 * of kmain — before serial_init() and before any other C callable.
 * Rationale: with -mfpmath=sse in CFLAGS, GCC auto-vectorizes stack
 * array zeroing in serial.c (movdqa), which #UDs without CR4.OSFXSR.
 * fpu_init() itself is SSE-free up to its trailing serial_printf.
 * After return: CR0.EM=0/MP=1/NE=1/TS=0, CR4.OSFXSR=1/OSXMMEXCPT=1,
 * FNINIT executed, MXCSR=0x1F80 (all SIMD FP exceptions masked). */
void fpu_init(void);

/* Exception handlers wired to IDT vectors 7 (#NM), 16 (#MF), 19 (#XF).
 * All call panic() with an FP-state dump — they are NOT expected to
 * fire under eager context switch + masked MXCSR. They exist for
 * debugging when someone unmasks or clears CR0.TS by hand. */
void fpu_nm_handler(uint32_t eip);
void fpu_mf_handler(uint32_t eip);
void fpu_xf_handler(uint32_t eip);

/* Combined boot-time FP smoke test. Runs both xmm0 round-trip and
 * xmm0-survives-yield probes. MUST be called after process_start_scheduler()
 * because the yield probe requires an active scheduler.
 * Panics via kernel_panic on any failure. */
void fpu_boot_smoke(void);

/* 8-thread FP stress test. Spawns 8 kernel workers each running a
 * 100k-iter sin-loop with a worker-unique offset, then compares their
 * results against a serial reference computed in the caller's context.
 * Panics via kernel_panic if any result diverges by more than 1e-6.
 *
 * Not called by default — kmain only invokes it when built with
 * -DFPU_STRESS. To run: add -DFPU_STRESS to CFLAGS in Makefile,
 * rebuild, boot, check serial log for
 *   "[fpu_stress] 8-thread sin-loop consistent"
 * (success) or a "[fpu_stress] CORRUPT ..." line followed by panic. */
void fpu_context_stress(void);

#endif
