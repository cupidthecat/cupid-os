/*
 * Interrupt Descriptor Table (IDT) Implementation
 * 
 * This file implements the IDT setup and management for the kernel:
 * - Defines the IDT structure and pointer
 * - Sets up exception handlers with descriptive messages
 * - Provides functions to set IDT gates/entries
 * - Initializes the IDT with default handlers
 * - Maps CPU exceptions to custom handlers
 * - Loads the IDT into the processor
 * 
 * The IDT is a crucial part of the interrupt handling system,
 * allowing the kernel to properly handle CPU exceptions and
 * hardware interrupts in protected mode.
 */

#include "idt.h"
#include "isr.h"
#include "kernel.h"
#include "panic.h"
#include "../drivers/serial.h"

// IDT entries array
struct idt_entry idt[256];
struct idt_ptr idtp;

// Exception messages
const char* exception_messages[] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Bad TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault"
};

// External assembly function
extern void load_idt(struct idt_ptr* ptr);

// Set an IDT gate
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low = base & 0xFFFF;
    idt[num].base_high = (uint16_t)((base >> 16) & 0xFFFF);
    idt[num].segment = sel;
    idt[num].reserved = 0;
    idt[num].flags = flags;
}

// Initialize the IDT
void idt_init(void) {
    // Set up IDT pointer
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base = (uint32_t)&idt;

    // Clear IDT
    for(int i = 0; i < 256; i++) {
        idt_set_gate((uint8_t)i, 0, 0, 0);
    }

    // Set up exception handlers
    idt_set_gate(0, (uint32_t)isr0, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(1, (uint32_t)isr1, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(2, (uint32_t)isr2, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(3, (uint32_t)isr3, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(4, (uint32_t)isr4, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(5, (uint32_t)isr5, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(6, (uint32_t)isr6, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(7, (uint32_t)isr7, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(8, (uint32_t)isr8, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(13, (uint32_t)isr13, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(14, (uint32_t)isr14, 0x08, IDT_INTERRUPT_GATE);

    // Install IRQ handlers
    idt_set_gate(32, (uint32_t)irq0, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(33, (uint32_t)irq1, 0x08, 0x8E);
    idt_set_gate(34, (uint32_t)irq2, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(35, (uint32_t)irq3, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(36, (uint32_t)irq4, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(37, (uint32_t)irq5, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(38, (uint32_t)irq6, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(39, (uint32_t)irq7, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(40, (uint32_t)irq8, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(41, (uint32_t)irq9, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(42, (uint32_t)irq10, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(43, (uint32_t)irq11, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(44, (uint32_t)irq12, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(45, (uint32_t)irq13, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(46, (uint32_t)irq14, 0x08, IDT_INTERRUPT_GATE);
    idt_set_gate(47, (uint32_t)irq15, 0x08, IDT_INTERRUPT_GATE);

    // Load IDT
    load_idt(&idtp);

    print("IDT gates set up.\n");
    print("IDT initialized.\n");
}

// Interrupt handler
void isr_handler(struct registers* r) {
    /* Page Fault (INT 14) - enhanced diagnostics */
    if (r->int_no == 14) {
        uint32_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));

        /* Build a human-readable description */
        const char *access = (r->err_code & 0x2) ? "WRITE" : "READ";
        const char *present = (r->err_code & 0x1) ? "protection" : "not-present";
        const char *mode = (r->err_code & 0x4) ? "user" : "kernel";

        /* Detect common patterns */
        if (cr2 < 0x1000) {
            serial_printf("[PANIC] NULL pointer dereference: %s at 0x%x (%s, %s mode)\n",
                          access, cr2, present, mode);
            print("\nNULL POINTER DEREFERENCE\n");
        } else {
            serial_printf("[PANIC] Page fault: %s at 0x%x (%s, %s mode)\n",
                          access, cr2, present, mode);
            print("\nPAGE FAULT\n");
        }

        print("  Faulting address: ");
        print_hex(cr2);
        print("\n  Access: ");
        print(access);
        print("  Cause: ");
        print(present);
        print("  Mode: ");
        print(mode);
        print("\n");

        kernel_panic_regs(r, "Page fault");
    }

    /* Other exceptions */
    const char *msg = "Unknown Exception";
    if (r->int_no < 15) {
        msg = exception_messages[r->int_no];
    }

    serial_printf("[PANIC] CPU Exception %u: %s  err=0x%x\n",
                  r->int_no, msg, r->err_code);

    kernel_panic_regs(r, msg);
} 
