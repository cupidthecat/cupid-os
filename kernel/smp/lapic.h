#ifndef LAPIC_H
#define LAPIC_H

#include "types.h"

#define LAPIC_DEFAULT_BASE 0xFEE00000u

#define LAPIC_REG_ID         0x020
#define LAPIC_REG_VERSION    0x030
#define LAPIC_REG_TPR        0x080
#define LAPIC_REG_EOI        0x0B0
#define LAPIC_REG_SVR        0x0F0
#define LAPIC_REG_ESR        0x280
#define LAPIC_REG_ICR_LOW    0x300
#define LAPIC_REG_ICR_HIGH   0x310
#define LAPIC_REG_LVT_TIMER  0x320
#define LAPIC_REG_LVT_LINT0  0x350
#define LAPIC_REG_LVT_LINT1  0x360
#define LAPIC_REG_LVT_ERROR  0x370
#define LAPIC_REG_TIMER_INIT 0x380
#define LAPIC_REG_TIMER_CUR  0x390
#define LAPIC_REG_TIMER_DIV  0x3E0

#define LAPIC_DELIVER_FIXED   (0u << 8)
#define LAPIC_DELIVER_INIT    (5u << 8)
#define LAPIC_DELIVER_STARTUP (6u << 8)
#define LAPIC_LEVEL_ASSERT    (1u << 14)

#define LAPIC_TIMER_VEC     0x20
#define LAPIC_SPURIOUS_VEC  0xFF

void lapic_init_bsp(void);
void lapic_init_ap(void);
void lapic_eoi(void);
uint32_t lapic_read(uint32_t offset);
void lapic_write(uint32_t offset, uint32_t value);
uint8_t lapic_get_id(void);
void lapic_send_ipi(uint8_t target_apic_id, uint8_t vector, uint32_t delivery);
uint32_t lapic_ticks_per_10ms(void);

#endif
