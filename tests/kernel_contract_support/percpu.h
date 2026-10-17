#ifndef PERCPU_H
#define PERCPU_H

#include "types.h"

#define SMP_MAX_CPUS 32

/* Hosted process-contract adapter for the CPU-local hardware boundary. */
typedef struct per_cpu_t {
    uint8_t cpu_id;
    uint32_t current_pid;
    uint32_t bkl_depth;
    uint32_t bkl_eflags_saved;
    volatile uint8_t reschedule_pending;
    uint8_t interrupt_depth;
} per_cpu_t;

extern per_cpu_t kernel_contract_cpu;
extern per_cpu_t kernel_contract_remote_cpu;
extern per_cpu_t *kernel_contract_this_cpu;

static inline per_cpu_t *this_cpu(void) {
    return kernel_contract_this_cpu;
}

static inline void percpu_request_reschedule(per_cpu_t *cpu) {
    __atomic_store_n(&cpu->reschedule_pending, 1u, __ATOMIC_RELEASE);
}

static inline bool percpu_reschedule_is_pending(const per_cpu_t *cpu) {
    return __atomic_load_n(&cpu->reschedule_pending, __ATOMIC_ACQUIRE) != 0u;
}

static inline bool percpu_take_reschedule(per_cpu_t *cpu) {
    return __atomic_exchange_n(&cpu->reschedule_pending, 0u,
                               __ATOMIC_ACQ_REL) != 0u;
}

static inline bool percpu_in_interrupt(const per_cpu_t *cpu) {
    return cpu->interrupt_depth != 0u;
}

#endif
