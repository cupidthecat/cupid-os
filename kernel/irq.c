#include "isr.h"    // For interrupt service routines and register struct
#include "pic.h"    // For Programmable Interrupt Controller functions
#include "math.h"   // For utility math functions

// Array of function pointers for IRQ handlers (16 IRQs total)
static irq_handler_t irq_handlers[16] = { 0 };

/**
 * irq_install_handler - Install a custom handler for a specific IRQ
 * @irq: IRQ number to install handler for (0-15)
 * @handler: Function pointer to the handler implementation
 *
 * This function safely installs an IRQ handler by:
 * 1. Disabling the IRQ during modification
 * 2. Setting the handler function pointer
 * 3. Re-enabling the IRQ
 */
void irq_install_handler(int irq, irq_handler_t handler) {
    if (irq >= 0 && irq < 16) {
        // Disable IRQ while modifying handler to prevent race conditions
        pic_set_mask(irq);
        
        // Set the handler function pointer
        irq_handlers[irq] = handler;
        
        // Re-enable IRQ after handler is set
        pic_clear_mask(irq);
        
        print("[:3] IRQ handler installed for IRQ ");
        // TODO: Add code to print irq number
        print("\n");
    }
}

/**
 * irq_uninstall_handler - Remove the handler for a specific IRQ
 * @irq: IRQ number to remove handler for (0-15)
 *
 * This function clears the handler for the specified IRQ by setting
 * its function pointer to NULL.
 */
void irq_uninstall_handler(int irq) {
    irq_handlers[irq] = 0;
}

/**
 * irq_handler - Main IRQ handler dispatcher
 * @r: Pointer to CPU register state at time of interrupt
 *
 * This function:
 * 1. Calculates the IRQ number from the interrupt vector
 * 2. Calls the appropriate handler if one is installed
 * 3. Sends End of Interrupt (EOI) to the PIC
 */
void irq_handler(struct registers* r) {
    // Convert interrupt vector to IRQ number (IRQs start at vector 32)
    int irq = r->int_no - 32;
    
    // Call the registered handler if it exists
    if (irq_handlers[irq]) {
        irq_handlers[irq](r);
    }
    
    // Acknowledge interrupt completion to PIC
    pic_send_eoi(irq);
}

/**
 * irq_list_handlers - Print list of installed IRQ handlers
 *
 * This function displays all currently installed IRQ handlers
 * along with their memory addresses in hexadecimal format.
 */
void irq_list_handlers(void) {
    print("[:3] Installed IRQ Handlers:\n");
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

/**
 * default_irq_handler - Default handler for unhandled IRQs
 * @r: Pointer to CPU register state at time of interrupt
 *
 * This function is called when an IRQ occurs but no specific
 * handler is installed. It prints a warning message with the
 * IRQ number.
 */
static void default_irq_handler(struct registers* r) {
    print("[>:3] Unhandled IRQ: ");
    char irq_str[3];
    itoa(r->int_no - 32, irq_str);
    print(irq_str);
    print("\n");
}

/**
 * irq_init - Initialize the IRQ subsystem
 *
 * This function sets up the IRQ system by:
 * 1. Installing the default handler for all IRQs
 * 2. Printing initialization message
 */
void irq_init(void) {
    // Set default handler for all IRQs
    for(int i = 0; i < 16; i++) {
        irq_handlers[i] = default_irq_handler;
    }
    print("[:3] IRQ subsystem initialized\n");
}