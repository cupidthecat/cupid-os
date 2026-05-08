#ifndef MP_TABLES_H
#define MP_TABLES_H

#include "types.h"
#include "percpu.h"
#include "ioapic.h"

typedef struct {
    uint8_t apic_id;
    uint8_t enabled;
    uint8_t bsp;
} mp_cpu_t;

extern mp_cpu_t mp_cpus[SMP_MAX_CPUS];
extern int mp_cpu_discovered_count;

bool mp_tables_discover(void);

#endif
