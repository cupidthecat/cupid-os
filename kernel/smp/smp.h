#ifndef SMP_H
#define SMP_H

#include "types.h"
#include "percpu.h"

void smp_init(void);
void smp_init_ipi(void);
void smp_reschedule(int cpu_id);
void smp_halt_others(void);
int  smp_call_on_cpu(int cpu_id, void (*fn)(void*), void *arg);
void smp_atomic_inc(uint32_t *p);

#endif
