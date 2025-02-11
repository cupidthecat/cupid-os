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

// Helper functions for low-level port I/O using inline assembly
// These provide direct hardware access for communicating with the PIC
static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/**
 * Initialize the Programmable Interrupt Controller (PIC)
 * 
 * This critical function configures both the master (PIC1) and slave (PIC2) controllers:
 * 1. Preserves current interrupt masks to maintain system stability during initialization
 * 2. Initiates the 4-step ICW (Initialization Command Word) sequence for both PICs
 * 3. Remaps IRQ vectors to avoid conflicts with CPU exceptions (0-31)
 * 4. Establishes master-slave relationship through cascade configuration
 * 5. Sets 8086 mode for proper x86 compatibility
 * 6. Restores original interrupt masks after configuration
 * 7. Enables essential system interrupts (timer and keyboard)
 * 
 * The PIC initialization is crucial for proper hardware interrupt handling
 */
void pic_init(void) {
    // Preserve current interrupt masks to prevent unintended interrupts
    uint8_t mask1 = inb(PIC1_DATA);  // Master PIC mask
    uint8_t mask2 = inb(PIC2_DATA);  // Slave PIC mask

    // Begin ICW1 initialization sequence (cascade mode, ICW4 required)
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);  // Initialize master PIC
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);  // Initialize slave PIC

    // ICW2: Remap IRQ vectors to avoid CPU exception conflicts
    outb(PIC1_DATA, 32);   // Master PIC: IRQ 0-7 -> INT 32-39
    outb(PIC2_DATA, 40);   // Slave PIC: IRQ 8-15 -> INT 40-47

    // ICW3: Configure master-slave relationship
    outb(PIC1_DATA, 4);    // Master PIC: Slave connected at IRQ2
    outb(PIC2_DATA, 2);    // Slave PIC: Cascade identity (connected to IRQ2)

    // ICW4: Set 8086 mode for proper x86 compatibility
    outb(PIC1_DATA, ICW4_8086);  // Master PIC in 8086 mode
    outb(PIC2_DATA, ICW4_8086);  // Slave PIC in 8086 mode

    // Restore original interrupt masks to maintain system stability
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);

    // Enable critical system interrupts
    pic_clear_mask(0);  // Enable timer interrupt (IRQ0)
    pic_clear_mask(1);  // Enable keyboard interrupt (IRQ1)

    print("[:3] PIC initialized.\n");
}

/**
 * Send End of Interrupt (EOI) signal to PIC(s)
 * 
 * This function acknowledges interrupt completion to the PIC(s):
 * - For IRQs 0-7: Sends EOI only to master PIC
 * - For IRQs 8-15: Sends EOI to both slave and master PICs
 * 
 * @param irq: The interrupt request number (0-15)
 * 
 * Note: Failure to send EOI will prevent further interrupts from that IRQ
 */
void pic_send_eoi(uint8_t irq) {
    if (irq >= 8)
        outb(PIC2_COMMAND, 0x20);  // Acknowledge slave PIC for IRQs 8-15
    outb(PIC1_COMMAND, 0x20);      // Always acknowledge master PIC
}

/**
 * Mask (disable) a specific interrupt request
 * 
 * This function sets the corresponding bit in the PIC's IMR (Interrupt Mask Register)
 * to disable a specific IRQ. Masked interrupts will be ignored by the CPU.
 * 
 * @param irq: The interrupt request number to mask (0-15)
 */
void pic_set_mask(uint8_t irq) {
    uint16_t port;
    uint8_t value;

    // Select appropriate PIC based on IRQ number
    if (irq < 8) {
        port = PIC1_DATA;  // Master PIC handles IRQs 0-7
    } else {
        port = PIC2_DATA;  // Slave PIC handles IRQs 8-15
        irq -= 8;          // Adjust IRQ number for slave PIC
    }
    // Set the mask bit for the specified IRQ
    value = inb(port) | (1 << irq);
    outb(port, value);
}

/**
 * Unmask (enable) a specific interrupt request
 * 
 * This function clears the corresponding bit in the PIC's IMR (Interrupt Mask Register)
 * to enable a specific IRQ. Unmasked interrupts will be processed by the CPU.
 * 
 * @param irq: The interrupt request number to unmask (0-15)
 */
void pic_clear_mask(uint8_t irq) {
    uint16_t port;
    uint8_t value;

    // Select appropriate PIC based on IRQ number
    if (irq < 8) {
        port = PIC1_DATA;  // Master PIC handles IRQs 0-7
    } else {
        port = PIC2_DATA;  // Slave PIC handles IRQs 8-15
        irq -= 8;          // Adjust IRQ number for slave PIC
    }
    // Clear the mask bit for the specified IRQ
    value = inb(port) & ~(1 << irq);
    outb(port, value);
}