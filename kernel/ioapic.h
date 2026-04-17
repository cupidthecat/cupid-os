#ifndef IOAPIC_H
#define IOAPIC_H

#include "types.h"

#define IOAPIC_DEFAULT_BASE 0xFEC00000u

#define IOAPIC_REG_ID     0x00
#define IOAPIC_REG_VER    0x01
#define IOAPIC_REG_ARB    0x02
#define IOAPIC_REG_REDIR0 0x10

typedef struct {
    uint8_t  id;
    uint32_t mmio_base;
    uint32_t gsi_base;
    uint8_t  gsi_count;
} ioapic_info_t;

#define MAX_IOAPICS 4

extern ioapic_info_t ioapics_discovered[MAX_IOAPICS];
extern int ioapic_count;
extern uint8_t isa_to_gsi[16];

void ioapic_init_all(uint8_t bsp_apic_id);
void ioapic_set_redirect(uint32_t gsi, uint8_t vector, uint8_t dest_apic_id,
                         bool level_triggered, bool active_low);
void ioapic_mask_gsi(uint32_t gsi);
void ioapic_unmask_gsi(uint32_t gsi);
uint32_t ioapic_irq_to_gsi(uint8_t irq);

#endif
