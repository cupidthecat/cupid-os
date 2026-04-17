#ifndef PCI_H
#define PCI_H

#include "types.h"

#define PCI_CONFIG_ADDR 0xCF8u
#define PCI_CONFIG_DATA 0xCFCu

#define PCI_CLASS_SERIAL_BUS 0x0Cu
#define PCI_SUBCLASS_USB     0x03u
#define PCI_PROGIF_UHCI      0x00u
#define PCI_PROGIF_OHCI      0x10u
#define PCI_PROGIF_EHCI      0x20u
#define PCI_PROGIF_XHCI      0x30u

#define PCI_MAX_DEVICES 64

typedef struct {
    uint8_t  bus, device, function;
    uint16_t vendor_id, device_id;
    uint8_t  class_code, subclass, prog_if, revision;
    uint8_t  irq_line;
    uint32_t bars[6];
    bool     bar_is_mmio[6];
} pci_device_t;

void pci_init(void);
int  pci_device_count(void);
pci_device_t *pci_get_device(int index);
pci_device_t *pci_find_by_class(uint8_t class_code, uint8_t subclass,
                                uint8_t prog_if, int start_index);
pci_device_t *pci_find_by_vendor_device(uint16_t vendor_id, uint16_t device_id);

uint32_t pci_config_read_dword(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);
void     pci_config_write_dword(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint32_t v);
uint16_t pci_config_read_word (uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);
uint8_t  pci_config_read_byte (uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);

void pci_enable_bus_master(pci_device_t *d);

#endif
