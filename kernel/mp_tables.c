#include "mp_tables.h"
#include "ioapic.h"
#include "../drivers/serial.h"

mp_cpu_t mp_cpus[SMP_MAX_CPUS];
int mp_cpu_discovered_count = 0;

typedef struct __attribute__((packed)) {
    char     signature[4];
    uint32_t config_ptr;
    uint8_t  length;
    uint8_t  spec_rev;
    uint8_t  checksum;
    uint8_t  feature[5];
} mp_float_t;

typedef struct __attribute__((packed)) {
    char     signature[4];
    uint16_t length;
    uint8_t  spec_rev;
    uint8_t  checksum;
    char     oem[8];
    char     product[12];
    uint32_t oem_table_ptr;
    uint16_t oem_table_size;
    uint16_t entry_count;
    uint32_t lapic_base;
    uint16_t ext_length;
    uint8_t  ext_checksum;
    uint8_t  reserved;
} mp_config_t;

static bool sig4(const char *p, const char *s) {
    return p[0] == s[0] && p[1] == s[1] && p[2] == s[2] && p[3] == s[3];
}

static mp_float_t *scan_range(uint32_t start, uint32_t end) {
    for (uint32_t p = start; p + 16u <= end; p += 16u) {
        mp_float_t *f = (mp_float_t*)p;
        if (sig4(f->signature, "_MP_")) {
            uint8_t sum = 0;
            for (int i = 0; i < 16; i++) sum = (uint8_t)(sum + ((uint8_t*)f)[i]);
            if (sum == 0) return f;
        }
    }
    return 0;
}

bool mp_tables_discover(void) {
    mp_float_t *f = 0;

    uint16_t ebda_seg = *(volatile uint16_t*)0x40E;
    uint32_t ebda = (uint32_t)ebda_seg << 4;
    if (ebda) f = scan_range(ebda, ebda + 1024u);

    if (!f) f = scan_range(0x9FC00u, 0xA0000u);
    if (!f) f = scan_range(0xF0000u, 0x100000u);
    if (!f) return false;

    mp_config_t *cfg = (mp_config_t*)f->config_ptr;
    if (!sig4(cfg->signature, "PCMP")) return false;

    uint8_t *p = (uint8_t*)cfg + sizeof(mp_config_t);
    mp_cpu_discovered_count = 0;
    ioapic_count = 0;
    for (int i = 0; i < 16; i++) isa_to_gsi[i] = (uint8_t)i;

    for (int e = 0; e < cfg->entry_count; e++) {
        uint8_t type = p[0];
        if (type == 0) {
            if (mp_cpu_discovered_count < SMP_MAX_CPUS) {
                mp_cpu_t *mp = &mp_cpus[mp_cpu_discovered_count++];
                mp->apic_id = p[1];
                uint8_t flags = p[3];
                mp->enabled = (uint8_t)((flags & 1u) ? 1u : 0u);
                mp->bsp     = (uint8_t)((flags & 2u) ? 1u : 0u);
            }
            p += 20;
        } else if (type == 2) {
            if (ioapic_count < MAX_IOAPICS) {
                ioapics_discovered[ioapic_count].id = p[1];
                ioapics_discovered[ioapic_count].mmio_base =
                    (uint32_t)p[4] | ((uint32_t)p[5] << 8)
                  | ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
                ioapics_discovered[ioapic_count].gsi_base = 0;
                ioapics_discovered[ioapic_count].gsi_count = 0;
                ioapic_count++;
            }
            p += 8;
        } else if (type == 3) {
            uint8_t src_irq = p[5];
            uint8_t dst_gsi = p[7];
            if (src_irq < 16) isa_to_gsi[src_irq] = dst_gsi;
            p += 8;
        } else {
            p += 8;
        }
    }

    KINFO("mp: discovered %d CPUs, %d IOAPIC(s)",
          mp_cpu_discovered_count, ioapic_count);
    return mp_cpu_discovered_count > 0;
}
