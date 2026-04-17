#include "pci.h"
#include "ports.h"
#include "../drivers/serial.h"

static pci_device_t devices[PCI_MAX_DEVICES];
static int device_count = 0;

static uint32_t make_addr(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off) {
    return 0x80000000u
         | ((uint32_t)bus  << 16)
         | ((uint32_t)dev  << 11)
         | ((uint32_t)func << 8)
         | ((uint32_t)(off & 0xFCu));
}

uint32_t pci_config_read_dword(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off) {
    outl(PCI_CONFIG_ADDR, make_addr(bus, dev, func, off));
    return inl(PCI_CONFIG_DATA);
}

void pci_config_write_dword(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint32_t v) {
    outl(PCI_CONFIG_ADDR, make_addr(bus, dev, func, off));
    outl(PCI_CONFIG_DATA, v);
}

uint16_t pci_config_read_word(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off) {
    uint32_t d = pci_config_read_dword(bus, dev, func, off);
    return (uint16_t)((d >> ((off & 2u) * 8u)) & 0xFFFFu);
}

uint8_t pci_config_read_byte(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off) {
    uint32_t d = pci_config_read_dword(bus, dev, func, off);
    return (uint8_t)((d >> ((off & 3u) * 8u)) & 0xFFu);
}

static void probe_function(uint8_t bus, uint8_t dev, uint8_t func) {
    uint16_t vendor = pci_config_read_word(bus, dev, func, 0x00);
    if (vendor == 0xFFFFu) return;
    if (device_count >= PCI_MAX_DEVICES) return;

    pci_device_t *p = &devices[device_count++];
    p->bus = bus; p->device = dev; p->function = func;
    p->vendor_id  = vendor;
    p->device_id  = pci_config_read_word(bus, dev, func, 0x02);
    p->revision   = pci_config_read_byte(bus, dev, func, 0x08);
    p->prog_if    = pci_config_read_byte(bus, dev, func, 0x09);
    p->subclass   = pci_config_read_byte(bus, dev, func, 0x0A);
    p->class_code = pci_config_read_byte(bus, dev, func, 0x0B);
    p->irq_line   = pci_config_read_byte(bus, dev, func, 0x3C);

    uint8_t hdr_type = pci_config_read_byte(bus, dev, func, 0x0E) & 0x7Fu;
    int num_bars = (hdr_type == 0) ? 6 : (hdr_type == 1) ? 2 : 0;

    for (int i = 0; i < 6; i++) {
        p->bars[i] = 0;
        p->bar_is_mmio[i] = false;
    }
    for (int i = 0; i < num_bars; i++) {
        uint32_t bar = pci_config_read_dword(bus, dev, func, (uint8_t)(0x10 + i*4));
        p->bars[i] = bar;
        p->bar_is_mmio[i] = (bar & 0x1u) == 0u;
        if (p->bar_is_mmio[i]) p->bars[i] &= 0xFFFFFFF0u;
        else                   p->bars[i] &= 0xFFFFFFFCu;
    }

    KINFO("pci: %x:%x.%x vid=%x did=%x class=%x:%x:%x irq=%u",
          bus, dev, func, p->vendor_id, p->device_id,
          p->class_code, p->subclass, p->prog_if, p->irq_line);
}

void pci_init(void) {
    device_count = 0;
    for (uint8_t dev = 0; dev < 32; dev++) {
        uint16_t v = pci_config_read_word(0, dev, 0, 0x00);
        if (v == 0xFFFFu) continue;
        uint8_t header = pci_config_read_byte(0, dev, 0, 0x0E);
        uint8_t max_func = (header & 0x80u) ? 8u : 1u;
        for (uint8_t f = 0; f < max_func; f++) probe_function(0, dev, f);
    }
    KINFO("pci: enumerated %d devices", device_count);
}

int pci_device_count(void) { return device_count; }
pci_device_t *pci_get_device(int index) {
    if (index < 0 || index >= device_count) return NULL;
    return &devices[index];
}

pci_device_t *pci_find_by_class(uint8_t class_code, uint8_t subclass,
                                uint8_t prog_if, int start_index) {
    for (int i = (start_index < 0) ? 0 : start_index; i < device_count; i++) {
        pci_device_t *p = &devices[i];
        if (p->class_code == class_code && p->subclass == subclass
            && p->prog_if == prog_if) return p;
    }
    return NULL;
}

pci_device_t *pci_find_by_vendor_device(uint16_t vendor_id, uint16_t device_id) {
    for (int i = 0; i < device_count; i++) {
        pci_device_t *p = &devices[i];
        if (p->vendor_id == vendor_id && p->device_id == device_id) return p;
    }
    return NULL;
}

void pci_enable_bus_master(pci_device_t *d) {
    uint32_t cmd = pci_config_read_dword(d->bus, d->device, d->function, 0x04);
    cmd &= 0x0000FFFFu;  /* preserve only Command; writing 0 to R/WC Status bits is a no-op */
    cmd |= (1u << 2) | (1u << 1) | (1u << 0); /* bus master + mem space + io space */
    pci_config_write_dword(d->bus, d->device, d->function, 0x04, cmd);
}
