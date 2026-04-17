#include "ioapic.h"
#include "memory.h"
#include "../drivers/serial.h"

ioapic_info_t ioapics_discovered[MAX_IOAPICS];
int ioapic_count = 0;
uint8_t isa_to_gsi[16];

/* REGSEL is at base+0x00, IOWIN is at base+0x10 */
static uint32_t ioapic_read(ioapic_info_t *io, uint8_t reg) {
    volatile uint8_t *base = (volatile uint8_t *)io->mmio_base;
    *(volatile uint32_t *)(base + 0x00u) = (uint32_t)reg;
    return *(volatile uint32_t *)(base + 0x10u);
}

static void ioapic_write(ioapic_info_t *io, uint8_t reg, uint32_t val) {
    volatile uint8_t *base = (volatile uint8_t *)io->mmio_base;
    *(volatile uint32_t *)(base + 0x00u) = (uint32_t)reg;
    *(volatile uint32_t *)(base + 0x10u) = val;
}

static ioapic_info_t *find_ioapic_for_gsi(uint32_t gsi) {
    int i;
    /* MADT / MP parsers set mmio_base but leave gsi_count=0. Lazy-read
     * the VER register here so discovery-after-ioapic_init still works. */
    for (i = 0; i < ioapic_count; i++) {
        ioapic_info_t *io = &ioapics_discovered[i];
        if (io->gsi_count == 0 && io->mmio_base != 0) {
            paging_map_mmio(io->mmio_base, 4096);
            uint32_t ver = ioapic_read(io, IOAPIC_REG_VER);
            io->gsi_count = (uint8_t)(((ver >> 16) & 0xFFu) + 1u);
        }
    }
    for (i = 0; i < ioapic_count; i++) {
        ioapic_info_t *io = &ioapics_discovered[i];
        if (gsi >= io->gsi_base && gsi < (uint32_t)(io->gsi_base + io->gsi_count))
            return io;
    }
    return (ioapic_info_t *)0;
}

uint32_t ioapic_irq_to_gsi(uint8_t irq) {
    if (irq < 16u) return (uint32_t)isa_to_gsi[irq];
    return (uint32_t)irq;
}

void ioapic_set_redirect(uint32_t gsi, uint8_t vector, uint8_t dest,
                         bool level, bool active_low) {
    ioapic_info_t *io = find_ioapic_for_gsi(gsi);
    uint8_t lo_reg;
    uint8_t hi_reg;
    uint32_t idx;
    uint32_t lo;
    uint32_t hi;
    if (!io) return;
    idx = gsi - io->gsi_base;
    lo = (uint32_t)vector;
    if (active_low) lo |= (1u << 13);
    if (level)      lo |= (1u << 15);
    lo |= (1u << 16);   /* start masked */
    hi = (uint32_t)dest << 24;
    lo_reg = (uint8_t)(IOAPIC_REG_REDIR0 + idx * 2u);
    hi_reg = (uint8_t)(IOAPIC_REG_REDIR0 + idx * 2u + 1u);
    ioapic_write(io, hi_reg, hi);
    ioapic_write(io, lo_reg, lo);
}

static void set_mask(uint32_t gsi, bool mask) {
    ioapic_info_t *io = find_ioapic_for_gsi(gsi);
    uint8_t lo_reg;
    uint32_t idx;
    uint32_t lo;
    if (!io) return;
    idx = gsi - io->gsi_base;
    lo_reg = (uint8_t)(IOAPIC_REG_REDIR0 + idx * 2u);
    lo = ioapic_read(io, lo_reg);
    if (mask) lo |= (1u << 16); else lo &= ~(1u << 16);
    ioapic_write(io, lo_reg, lo);
}

void ioapic_mask_gsi(uint32_t gsi)   { set_mask(gsi, true); }
void ioapic_unmask_gsi(uint32_t gsi) { set_mask(gsi, false); }

void ioapic_init_all(uint8_t bsp_apic_id) {
    int i;
    /* Default: single IOAPIC at 0xFEC00000 with 24 GSIs.
     * T5/T6 will overwrite if MP/ACPI discovery populates something else. */
    if (ioapic_count == 0) {
        ioapics_discovered[0].id = 0;
        ioapics_discovered[0].mmio_base = IOAPIC_DEFAULT_BASE;
        ioapics_discovered[0].gsi_base = 0;
        ioapics_discovered[0].gsi_count = 24;
        ioapic_count = 1;
        for (i = 0; i < 16; i++) isa_to_gsi[i] = (uint8_t)i;
    }

    for (i = 0; i < ioapic_count; i++) {
        ioapic_info_t *io = &ioapics_discovered[i];
        uint32_t g;
        uint32_t ver;
        paging_map_mmio(io->mmio_base, 4096);
        ver = ioapic_read(io, IOAPIC_REG_VER);
        io->gsi_count = (uint8_t)(((ver >> 16) & 0xFFu) + 1u);
        KINFO("ioapic: id=%u base=%x gsis=%u",
              (uint32_t)io->id, io->mmio_base, (uint32_t)io->gsi_count);

        for (g = io->gsi_base; g < (uint32_t)(io->gsi_base + io->gsi_count); g++) {
            ioapic_set_redirect(g, (uint8_t)(0x20u + g), bsp_apic_id, false, false);
        }
    }
}
