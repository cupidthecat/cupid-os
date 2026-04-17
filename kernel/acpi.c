#include "acpi.h"
#include "mp_tables.h"
#include "ioapic.h"
#include "../drivers/serial.h"

typedef struct __attribute__((packed)) {
    char     signature[8];
    uint8_t  checksum;
    char     oem[6];
    uint8_t  revision;
    uint32_t rsdt_addr;
    uint32_t length;
    uint64_t xsdt_addr;
    uint8_t  ext_checksum;
    uint8_t  reserved[3];
} rsdp_t;

typedef struct __attribute__((packed)) {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem[6];
    char     oem_table[8];
    uint32_t oem_rev;
    uint32_t creator_id;
    uint32_t creator_rev;
} sdt_header_t;

typedef struct __attribute__((packed)) {
    sdt_header_t h;
    uint32_t lapic_addr;
    uint32_t flags;
} madt_t;

static bool ck_sum(const void *p, uint32_t n) {
    uint8_t s = 0;
    for (uint32_t i = 0; i < n; i++) s = (uint8_t)(s + ((const uint8_t*)p)[i]);
    return s == 0;
}

static bool sig8_match(const char *p, const char *s) {
    for (int i = 0; i < 8; i++) if (p[i] != s[i]) return false;
    return true;
}

static bool sig4_match(const char *p, const char *s) {
    return p[0] == s[0] && p[1] == s[1] && p[2] == s[2] && p[3] == s[3];
}

static rsdp_t *scan_rsdp_range(uint32_t start, uint32_t end) {
    for (uint32_t p = start; p + 20u <= end; p += 16u) {
        rsdp_t *r = (rsdp_t*)p;
        if (sig8_match(r->signature, "RSD PTR ") && ck_sum(r, 20)) return r;
    }
    return 0;
}

static rsdp_t *find_rsdp(void) {
    rsdp_t *r = scan_rsdp_range(0xE0000u, 0x100000u);
    if (r) return r;
    uint16_t ebda_seg = *(volatile uint16_t*)0x40E;
    uint32_t ebda = (uint32_t)ebda_seg << 4;
    if (ebda) return scan_rsdp_range(ebda, ebda + 1024u);
    return 0;
}

static sdt_header_t *find_table(sdt_header_t *rsdt, const char *sig) {
    uint32_t n = (rsdt->length - (uint32_t)sizeof(sdt_header_t)) / 4u;
    uint32_t *ptrs = (uint32_t*)(rsdt + 1);
    for (uint32_t i = 0; i < n; i++) {
        sdt_header_t *t = (sdt_header_t*)ptrs[i];
        if (sig4_match(t->signature, sig)) return t;
    }
    return 0;
}

static void parse_madt(madt_t *m) {
    mp_cpu_discovered_count = 0;
    ioapic_count = 0;
    for (int i = 0; i < 16; i++) isa_to_gsi[i] = (uint8_t)i;

    uint8_t *p = (uint8_t*)(m + 1);
    uint8_t *end = (uint8_t*)m + m->h.length;
    while (p < end) {
        uint8_t type = p[0];
        uint8_t len = p[1];
        if (len == 0) break;

        if (type == 0 && len >= 8) {
            uint8_t apic_id = p[3];
            uint32_t flags = (uint32_t)p[4] | ((uint32_t)p[5] << 8)
                           | ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
            if ((flags & 1u) && mp_cpu_discovered_count < SMP_MAX_CPUS) {
                mp_cpu_t *mp = &mp_cpus[mp_cpu_discovered_count++];
                mp->apic_id = apic_id;
                mp->enabled = 1;
                mp->bsp = 0;
            }
        } else if (type == 1 && len >= 12) {
            if (ioapic_count < MAX_IOAPICS) {
                ioapics_discovered[ioapic_count].id = p[2];
                ioapics_discovered[ioapic_count].mmio_base =
                    (uint32_t)p[4] | ((uint32_t)p[5] << 8)
                  | (uint32_t)((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
                ioapics_discovered[ioapic_count].gsi_base =
                    (uint32_t)p[8] | ((uint32_t)p[9] << 8)
                  | (uint32_t)((uint32_t)p[10] << 16) | ((uint32_t)p[11] << 24);
                ioapics_discovered[ioapic_count].gsi_count = 0;
                ioapic_count++;
            }
        } else if (type == 2 && len >= 10) {
            uint8_t src_irq = p[3];
            uint32_t gsi = (uint32_t)p[4] | ((uint32_t)p[5] << 8)
                         | ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
            if (src_irq < 16) isa_to_gsi[src_irq] = (uint8_t)gsi;
        }
        p += len;
    }
}

bool acpi_discover(void) {
    rsdp_t *r = find_rsdp();
    if (!r) return false;

    sdt_header_t *rsdt;
    if (r->revision >= 2 && r->xsdt_addr != 0) {
        rsdt = (sdt_header_t*)(uint32_t)r->xsdt_addr;
    } else {
        rsdt = (sdt_header_t*)r->rsdt_addr;
    }
    if (!ck_sum(rsdt, rsdt->length)) return false;

    madt_t *m = (madt_t*)find_table(rsdt, "APIC");
    if (!m) return false;

    parse_madt(m);
    KINFO("acpi: MADT: %d CPUs, %d IOAPIC(s)",
          mp_cpu_discovered_count, ioapic_count);
    return mp_cpu_discovered_count > 0;
}
