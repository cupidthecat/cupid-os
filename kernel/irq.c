#include "isr.h"
#include "pic.h"
#include "math.h"

// IRQ handler function pointers
static irq_handler_t irq_handlers[16] = { 0 };

// Install a custom IRQ handler
void irq_install_handler(int irq, irq_handler_t handler) {
    if (irq >= 0 && irq < 16) {
        // Disable IRQ while modifying handler
        pic_set_mask(irq);
        
        irq_handlers[irq] = handler;
        
        // Enable IRQ after setting handler
        pic_clear_mask(irq);
        
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

void irq_list_handlers(void) {
    print("Installed IRQ Handlers:\n");
    for(int i = 0; i < 16; i++) {
        if(irq_handlers[i]) {
            print("IRQ");
            char num[3];
            itoa(i, num);
            print(num);
            print(": 0x");
            print_hex((uint32_t)irq_handlers[i]);
            print("\n");
        }
    }
}

static void default_irq_handler(struct registers* r) {
    print("Unhandled IRQ: ");
    char irq_str[3];
    itoa(r->int_no - 32, irq_str);
    print(irq_str);
    print("\n");
}

void irq_init(void) {
    for(int i = 0; i < 16; i++) {
        irq_handlers[i] = default_irq_handler;
    }
    print("IRQ subsystem initialized\n");
} 