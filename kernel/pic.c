#include "pic.h"

// Helper functions for port I/O
static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void pic_init(void) {
    // ICW1: Start initialization sequence
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);

    // ICW2: Set vector offsets
    outb(PIC1_DATA, 0x20);  // IRQ 0-7: interrupts 32-39
    outb(PIC2_DATA, 0x28);  // IRQ 8-15: interrupts 40-47

    // ICW3: Configure master/slave relationship
    outb(PIC1_DATA, 4);     // Tell Master PIC that Slave PIC is at IRQ2
    outb(PIC2_DATA, 2);     // Tell Slave PIC its cascade identity

    // ICW4: Set mode
    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);

    // Clear masks for both PICs first
    outb(PIC1_DATA, 0x0);
    outb(PIC2_DATA, 0x0);
    
    // Enable only IRQ1 (keyboard) and IRQ2 (cascade)
    outb(PIC1_DATA, ~((1 << 1) | (1 << 2)));  // Enable IRQ1 and IRQ2
    outb(PIC2_DATA, 0xFF);  // Mask all interrupts on slave PIC
    
    // Clear any pending interrupts
    inb(PIC1_DATA);
    inb(PIC2_DATA);
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8)
        outb(PIC2_COMMAND, 0x20);  // Send EOI to slave PIC
    outb(PIC1_COMMAND, 0x20);      // Send EOI to master PIC
}

void pic_set_mask(uint8_t irq) {
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    value = inb(port) | (1 << irq);
    outb(port, value);
}

void pic_clear_mask(uint8_t irq) {
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    value = inb(port) & ~(1 << irq);
    outb(port, value);
} 