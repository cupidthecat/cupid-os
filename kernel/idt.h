/**
 * idt.h
 * 
 * Interrupt Descriptor Table (IDT) header file.
 */

#ifndef IDT_H
#define IDT_H

#include "types.h"

// IDT Gate Types
#define IDT_INTERRUPT_GATE 0x8E
#define IDT_TRAP_GATE     0x8F

// Structure for IDT entry
struct idt_entry {
    uint16_t base_low;
    uint16_t segment;
    uint8_t  reserved;
    uint8_t  flags;
    uint16_t base_high;
} __attribute__((packed));

// IDT descriptor structure
struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

// Function declarations
void idt_init(void);
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);

#endif 