#include "ac97.h"
#include "../pci.h"
#include "../../drivers/serial.h"

/* Known AC97 PCI device IDs (Intel ICH family) */
static const uint16_t AC97_VENDOR = 0x8086u;
static const uint16_t AC97_DEVICES[] = {
    0x2415u, 0x2425u, 0x2445u, 0x2485u, 0x24C5u, 0x24D5u, 0x266Eu
};
#define AC97_DEVICE_COUNT (sizeof(AC97_DEVICES) / sizeof(AC97_DEVICES[0]))

static struct {
    bool         present;
    pci_device_t *pci;
    uint16_t     bar_nam;
    uint16_t     bar_nabm;
    uint8_t      irq_line;
    void       (*fill)(int16_t *, uint32_t);
} s_ac97;

/* Static helper: print a 16-bit value as "0xABCD" to serial */
static void hex16(uint16_t v) {
    static const char H[] = "0123456789ABCDEF";
    char buf[7];
    buf[0] = '0'; buf[1] = 'x';
    buf[2] = H[(v >> 12) & 0xFu];
    buf[3] = H[(v >>  8) & 0xFu];
    buf[4] = H[(v >>  4) & 0xFu];
    buf[5] = H[ v        & 0xFu];
    buf[6] = '\0';
    serial_write_string(buf);
}

bool ac97_is_present(void) { return s_ac97.present; }

void ac97_set_fill_callback(void (*fill)(int16_t *, uint32_t)) {
    s_ac97.fill = fill;
}

int ac97_init(void) {
    pci_device_t *dev = NULL;
    uint32_t i;
    for (i = 0; i < AC97_DEVICE_COUNT; i++) {
        dev = pci_find_by_vendor_device(AC97_VENDOR, AC97_DEVICES[i]);
        if (dev) { break; }
    }
    if (!dev) {
        serial_write_string("[ac97] no AC97 device found\n");
        return -1;
    }

    uint32_t bar0 = pci_config_read_dword(dev->bus, dev->device, dev->function, 0x10u);
    uint32_t bar1 = pci_config_read_dword(dev->bus, dev->device, dev->function, 0x14u);
    s_ac97.bar_nam  = (uint16_t)(bar0 & 0xFFFCu);
    s_ac97.bar_nabm = (uint16_t)(bar1 & 0xFFFCu);
    s_ac97.irq_line = pci_config_read_byte(dev->bus, dev->device, dev->function, 0x3Cu);
    s_ac97.pci      = dev;
    s_ac97.present  = true;

    pci_enable_bus_master(dev);

    serial_write_string("[ac97] present: NAM=");
    hex16(s_ac97.bar_nam);
    serial_write_string(" NABM=");
    hex16(s_ac97.bar_nabm);
    serial_write_string("\n");

    return 0;
}

void ac97_start(void) { /* filled in Task 4 */ }
void ac97_stop(void)  { /* filled in Task 4 */ }
void ac97_set_master_volume(uint8_t pct) { (void)pct; /* Task 4 */ }
