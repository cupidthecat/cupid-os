/*
 * PIC (Programmable Interrupt Controller) Driver Implementation
 * 
 * This file implements the PIC driver functionality:
 * - Initializes both master (PIC1) and slave (PIC2) controllers
 * - Remaps PIC interrupts to avoid conflicts with CPU exceptions
 * - Provides functions to send EOI (End of Interrupt) signals
 * - Handles interrupt masking/unmasking
 * - Supports cascaded PICs with master/slave configuration
 * - Maps IRQs 0-15 to interrupts 32-47
 * 
 * The PIC allows the kernel to handle hardware interrupts from
 * devices like the keyboard, timer, etc in a controlled way.
 */

#include "pic.h"
#include "kernel.h"

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
    // Save masks
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    // Start initialization sequence
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);

    // Set vector offsets
    outb(PIC1_DATA, 32);   // IRQ 0-7: 32-39
    outb(PIC2_DATA, 40);   // IRQ 8-15: 40-47

    // Tell Master PIC there is a slave at IRQ2
    outb(PIC1_DATA, 4);    // Slave at IRQ2
    outb(PIC2_DATA, 2);    // Slave's cascade identity

    // Set 8086 mode
    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);

    // Restore saved masks
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);

    // Clear masks for timer (IRQ0) and keyboard (IRQ1)
    pic_clear_mask(0);  // Timer
    pic_clear_mask(1);  // Keyboard

    print("[:3] PIC initialized.\n");
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