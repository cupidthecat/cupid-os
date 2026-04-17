#include "fpu.h"
#include "panic.h"
#include "process.h"
#include "libm.h"
#include "../drivers/serial.h"

void fpu_init(void) {
    uint32_t cr0, cr4, mxcsr;

    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1u << 2);   /* CR0.EM = 0 (hw x87)          */
    cr0 |=  (1u << 1);   /* CR0.MP = 1                   */
    cr0 |=  (1u << 5);   /* CR0.NE = 1                   */
    cr0 &= ~(1u << 3);   /* CR0.TS = 0 (eager switch)    */
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1u << 9);    /* CR4.OSFXSR                   */
    cr4 |= (1u << 10);   /* CR4.OSXMMEXCPT               */
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4));

    __asm__ volatile("fninit");

    mxcsr = 0x1F80u;     /* all 6 SIMD FP exceptions masked */
    __asm__ volatile("ldmxcsr %0" :: "m"(mxcsr));

    serial_printf("[fpu] SSE2 enabled, CR0=%x CR4=%x MXCSR=%x\n",
                  cr0, cr4, mxcsr);
}

void fpu_nm_handler(uint32_t eip) {
    panic_fpu("FPU #NM (unexpected with eager switch)", eip);
}
void fpu_mf_handler(uint32_t eip) {
    panic_fpu("x87 #MF", eip);
}
void fpu_xf_handler(uint32_t eip) {
    panic_fpu("SIMD #XF", eip);
}

void fpu_boot_smoke(void) {
    /* 1) xmm0 round-trip — hardware SSE sanity */
    {
        float probe = 1.5f;
        float readback = 0.0f;
        __asm__ volatile(
            "movss %1, %%xmm0\n\t"
            "movss %%xmm0, %0\n\t"
            : "=m"(readback)
            : "m"(probe)
            : "xmm0");
        if (readback != probe) {
            serial_printf("[fpu] SANITY FAIL: probe bits=0x%x readback bits=0x%x\n",
                          *(uint32_t*)&probe, *(uint32_t*)&readback);
            kernel_panic("FPU sanity check failed");
        }
    }

    /* 2) xmm0 survives a context switch */
    {
        float marker = 3.14159f;
        float readback = 0.0f;
        __asm__ volatile("movss %0, %%xmm0" :: "m"(marker) : "xmm0");
        process_yield();
        __asm__ volatile("movss %%xmm0, %0" : "=m"(readback) :: "xmm0");
        if (readback != marker) {
            serial_printf("[fpu] XMM0 LOST across yield: expected=0x%x got=0x%x\n",
                          *(uint32_t*)&marker, *(uint32_t*)&readback);
            kernel_panic("FXSAVE/FXRSTOR round-trip failed");
        }
    }

    serial_printf("[fpu] boot smoke ok\n");
}

/* ----------------------------------------------------------------------
 * fpu_context_stress — 8-thread sin-loop cross-thread corruption probe.
 *
 * Gated by the -DFPU_STRESS build flag: not wired into kmain unless the
 * caller enables it.  Spawns 8 kernel workers, each running a 100k-iter
 * sin() loop seeded by a worker-unique offset.  Each worker's result is
 * compared against a reference computed serially (no context switches)
 * in the main context; a divergence of more than 1e-6 triggers
 * kernel_panic.
 *
 * Why this exists: with eager FXSAVE/FXRSTOR in context_switch.asm, the
 * SSE/x87 register file must round-trip every time schedule() fires.
 * A single 10ms preempt during one of these sin loops would corrupt the
 * result if ANY FP register were not saved/restored.  Eight concurrent
 * loops give the PIT a few dozen opportunities per second to catch it.
 *
 * Note on sin() ABI: the libm.c `sin` is exported with the CupidC-tailored
 * kernel-internal ABI (result in XMM0, not ST(0) — see libm.h).  Calling
 * it from plain C here would not interoperate with the System-V i386
 * return convention GCC expects, so we inline the FSIN via `fldl/fsin/fstpl`.
 * This keeps the stress test self-contained and ABI-independent.
 * -------------------------------------------------------------------- */

static inline double stress_sin(double x) {
    double r;
    __asm__ volatile("fldl %1\n\t"
                     "fsin\n\t"
                     "fstpl %0\n\t"
                     : "=m"(r) : "m"(x));
    return r;
}

static volatile double stress_sum[8];
static volatile int    stress_done[8];

static void stress_worker_impl(uint32_t id) {
    double s = 0.0;
    double base = (double)(id + 1u) * 0.0001;
    for (int i = 0; i < 100000; i++) {
        s += stress_sin(base * (double)i);
    }
    stress_sum[id] = s;
    stress_done[id] = 1;
    while (1) {
        process_yield();
    }
}

#define STRESS_WORKER(N) \
    static void stress_worker_##N(void) { stress_worker_impl(N); }
STRESS_WORKER(0) STRESS_WORKER(1) STRESS_WORKER(2) STRESS_WORKER(3)
STRESS_WORKER(4) STRESS_WORKER(5) STRESS_WORKER(6) STRESS_WORKER(7)

static void (*const stress_workers[8])(void) = {
    stress_worker_0, stress_worker_1, stress_worker_2, stress_worker_3,
    stress_worker_4, stress_worker_5, stress_worker_6, stress_worker_7,
};

void fpu_context_stress(void) {
    double ref[8];

    /* Compute reference values in this context first — serial, no
     * cross-thread switches to muddy the picture.  Uses the same
     * inline-FSIN path as the workers for bit-identical comparison. */
    for (int id = 0; id < 8; id++) {
        double s = 0.0;
        double base = (double)((uint32_t)id + 1u) * 0.0001;
        for (int i = 0; i < 100000; i++) {
            s += stress_sin(base * (double)i);
        }
        ref[id] = s;
        stress_sum[id] = 0.0;
        stress_done[id] = 0;
    }

    /* Spawn 8 workers. MAX_PROCESSES=32, boot usage ~5, so 8 more fits. */
    for (int id = 0; id < 8; id++) {
        uint32_t pid = process_create(stress_workers[id],
                                      "fpu_stress",
                                      DEFAULT_STACK_SIZE);
        if (pid == 0) {
            kernel_panic("fpu_context_stress: process_create failed");
        }
    }

    /* Spin until every worker has recorded its result. */
    while (1) {
        int all_done = 1;
        for (int id = 0; id < 8; id++) {
            if (!stress_done[id]) { all_done = 0; break; }
        }
        if (all_done) break;
        process_yield();
    }

    /* Compare against reference. */
    for (int id = 0; id < 8; id++) {
        double got = stress_sum[id];
        double r   = ref[id];
        double d   = got > r ? got - r : r - got;
        if (d > 1e-6) {
            serial_printf("[fpu_stress] CORRUPT id=%d ref=%f got=%f diff=%f\n",
                          id, r, got, d);
            kernel_panic("FP context-switch corruption detected");
        }
    }
    serial_printf("[fpu_stress] 8-thread sin-loop consistent\n");
}
