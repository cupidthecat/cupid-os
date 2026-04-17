#include "lapic.h"
#include "ports.h"
#include "memory.h"
#include "../drivers/serial.h"

static volatile uint8_t *lapic_base;
static uint32_t ticks_per_10ms_val = 0;

uint32_t lapic_read(uint32_t offset) {
    return *(volatile uint32_t*)(lapic_base + offset);
}

void lapic_write(uint32_t offset, uint32_t value) {
    *(volatile uint32_t*)(lapic_base + offset) = value;
}

void lapic_eoi(void) {
    lapic_write(LAPIC_REG_EOI, 0);
}

uint8_t lapic_get_id(void) {
    return (uint8_t)(lapic_read(LAPIC_REG_ID) >> 24);
}

uint32_t lapic_ticks_per_10ms(void) { return ticks_per_10ms_val; }

void lapic_send_ipi(uint8_t target, uint8_t vec, uint32_t deliver) {
    while (lapic_read(LAPIC_REG_ICR_LOW) & (1u << 12))
        __asm__ volatile("pause");
    lapic_write(LAPIC_REG_ICR_HIGH, (uint32_t)target << 24);
    lapic_write(LAPIC_REG_ICR_LOW, (uint32_t)vec | deliver);
    while (lapic_read(LAPIC_REG_ICR_LOW) & (1u << 12))
        __asm__ volatile("pause");
}

static uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

/* PIT channel 2 one-shot 10ms sleep. Uses speaker gate port 0x61 to start. */
static void pit_calibrate_10ms_sleep(void) {
    outb(0x43, 0xB0);  /* ch2, lobyte/hibyte, mode 0, binary */
    uint16_t count = 11932;   /* 1193182Hz / 100 = 10ms */
    outb(0x42, (uint8_t)(count & 0xFFu));
    outb(0x42, (uint8_t)((count >> 8) & 0xFFu));
    uint8_t gate = inb(0x61);
    outb(0x61, (uint8_t)((gate & 0xFEu) | 1u));
    while ((inb(0x61) & 0x20u) == 0u) __asm__ volatile("pause");
    outb(0x61, (uint8_t)(gate & 0xFEu));
}

void lapic_init_bsp(void) {
    uint64_t apic_base_msr = rdmsr(0x1B);
    uint32_t base_phys = (uint32_t)(apic_base_msr & 0xFFFFF000ull);
    if (base_phys == 0) base_phys = LAPIC_DEFAULT_BASE;

    paging_map_mmio(base_phys, 4096);
    lapic_base = (volatile uint8_t*)base_phys;

    /* Software enable + spurious vector */
    lapic_write(LAPIC_REG_TPR, 0);
    lapic_write(LAPIC_REG_SVR, 0x100u | (uint32_t)LAPIC_SPURIOUS_VEC);
    lapic_write(LAPIC_REG_LVT_LINT0, 1u << 16);
    lapic_write(LAPIC_REG_LVT_LINT1, 1u << 16);
    lapic_write(LAPIC_REG_LVT_ERROR, 1u << 16);
    lapic_write(LAPIC_REG_TIMER_DIV, 0xBu);

    /* Calibrate: one-shot, vector masked, 0xFFFFFFFF init, sleep, read cur. */
    lapic_write(LAPIC_REG_LVT_TIMER, (uint32_t)LAPIC_TIMER_VEC | (1u << 16));
    lapic_write(LAPIC_REG_TIMER_INIT, 0xFFFFFFFFu);
    pit_calibrate_10ms_sleep();
    uint32_t remaining = lapic_read(LAPIC_REG_TIMER_CUR);
    ticks_per_10ms_val = 0xFFFFFFFFu - remaining;

    /* Arm periodic at LAPIC_TIMER_VEC = 0x20, 100Hz (ticks_per_10ms). */
    lapic_write(LAPIC_REG_LVT_TIMER, (uint32_t)LAPIC_TIMER_VEC | (1u << 17));
    lapic_write(LAPIC_REG_TIMER_INIT, ticks_per_10ms_val);

    KINFO("lapic: BSP base=%x id=%u ticks/10ms=%u",
          base_phys, lapic_get_id(), ticks_per_10ms_val);
}

void lapic_init_ap(void) {
    /* Same MMIO, share base. */
    lapic_write(LAPIC_REG_TPR, 0);
    lapic_write(LAPIC_REG_SVR, 0x100u | (uint32_t)LAPIC_SPURIOUS_VEC);
    lapic_write(LAPIC_REG_LVT_LINT0, 1u << 16);
    lapic_write(LAPIC_REG_LVT_LINT1, 1u << 16);
    lapic_write(LAPIC_REG_LVT_ERROR, 1u << 16);
    lapic_write(LAPIC_REG_TIMER_DIV, 0xBu);
    lapic_write(LAPIC_REG_LVT_TIMER, (uint32_t)LAPIC_TIMER_VEC | (1u << 17));
    lapic_write(LAPIC_REG_TIMER_INIT, ticks_per_10ms_val);
}
