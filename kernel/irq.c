#include "isr.h"
#include "pic.h"

// IRQ handler function pointers
static irq_handler_t irq_handlers[16] = { 0 };

// Install a custom IRQ handler
void irq_install_handler(int irq, irq_handler_t handler) {
    irq_handlers[irq] = handler;
}

// Remove an IRQ handler
void irq_uninstall_handler(int irq) {
    irq_handlers[irq] = 0;
}

// IRQ handler dispatcher
void irq_handler(struct registers* r) {
    // Get IRQ number (subtract 32 from interrupt number)
    int irq = r->int_no - 32;
    
    print("IRQ received: ");
    char irq_num[2] = {'0' + irq, '\0'};
    print(irq_num);
    print("\n");
    
    // Call handler if it exists
    if (irq_handlers[irq]) {
        irq_handlers[irq](r);
    }
    
    // Send EOI to PIC
    pic_send_eoi(irq);
} 