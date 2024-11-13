#include "idt.h"
#include "isr.h"

// Define IDT array
struct idt_entry idt[256];
struct idt_ptr idtp;

// External assembly functions we'll need to implement
extern void load_idt(struct idt_ptr* idt_ptr);

// Add ISR handler function
void isr_handler(void) {
    // Print a message when interrupt occurs
    print("Interrupt received!\n");
    print("Divide by zero exception!\n");
    
    // Halt the system
    while(1) {
        __asm__ volatile("hlt");
    }
}

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low = (base & 0xFFFF);
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].segment = sel;
    idt[num].reserved = 0;
    idt[num].flags = flags;
}

void idt_init(void) {
    // Set up IDT pointer
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base = (uint32_t)&idt;

    // Clear IDT
    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, 0, 0, 0);
    }

    // Set up basic exception handlers
    idt_set_gate(0, (uint32_t)isr0, 0x08, IDT_INTERRUPT_GATE);  // Divide by zero
    idt_set_gate(1, (uint32_t)isr1, 0x08, IDT_INTERRUPT_GATE);  // Debug

    // Load IDT
    load_idt(&idtp);
}