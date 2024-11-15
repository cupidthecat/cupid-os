#include "isr.h"
#include "pic.h"

// IRQ handler function pointers
static irq_handler_t irq_handlers[16] = { 0 };

// Install a custom IRQ handler
void irq_install_handler(int irq, irq_handler_t handler) {
    if (irq >= 0 && irq < 16) {
        irq_handlers[irq] = handler;
        print("IRQ handler installed for IRQ ");
        // Add code to print irq number
        print("\n");
    }
}

// Remove an IRQ handler
void irq_uninstall_handler(int irq) {
    irq_handlers[irq] = 0;
}

// IRQ handler dispatcher
void irq_handler(struct registers* r) {
    // Get IRQ number (subtract 32 from interrupt number)
    int irq = r->int_no - 32;
    
    // Call handler if it exists
    if (irq_handlers[irq]) {
        irq_handlers[irq](r);
    }
    
    // Send EOI to PIC
    pic_send_eoi(irq);
} 