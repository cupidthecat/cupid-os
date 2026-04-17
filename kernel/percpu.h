#ifndef PERCPU_H
#define PERCPU_H

#include "types.h"
#include "process.h"

#define SMP_MAX_CPUS 32
#define GDT_GS_BASE_INDEX 5

typedef struct per_cpu_t {
    struct per_cpu_t *self_ptr;
    uint8_t  cpu_id;
    uint8_t  apic_id;
    uint8_t  bootstrap;
    uint8_t  online;
    process_t *current;
    process_t *idle;
    uint32_t bkl_depth;
    uint64_t preempt_count;
    uint8_t *idle_stack_top;
    uint32_t bkl_eflags_saved;
    uint32_t current_pid;      /* PID running on this CPU (0 = idle/hlt)   */
    void   (*call_fn)(void *arg);
    void    *call_arg;
    volatile uint8_t call_pending;
    volatile uint8_t call_done;
    /* Tail pad so the struct rounds up to a cache-line pair. The
     * aligned(64) attribute on the struct forces sizeof to a
     * multiple of 64, so _pad only needs to avoid truncating fields. */
    uint8_t  _pad[76];
} per_cpu_t __attribute__((aligned(64)));

/* Cache-line-pair isolation guaranteed: sizeof must be 128. */
_Static_assert(sizeof(per_cpu_t) == 128,
               "per_cpu_t must be exactly 128 bytes (2 cache lines) so cpus[i] don't share cache lines");

extern per_cpu_t cpus[SMP_MAX_CPUS];
extern int smp_cpu_count_var;

static inline per_cpu_t *this_cpu(void) {
    per_cpu_t *p;
    __asm__ volatile("mov %%gs:0, %0" : "=r"(p));
    return p;
}

void percpu_init_bsp(void);
void percpu_load_kernel_gdt(int cpu_id);
int smp_cpu_count(void);
int smp_current_cpu(void);

#endif
