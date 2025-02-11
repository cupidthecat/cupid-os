/*
 * Interrupt Descriptor Table (IDT) Implementation
 * 
 * The IDT is a critical component of the x86 architecture's interrupt handling
 * system. It contains 256 entries that map interrupt vectors to their respective
 * handler functions. Each entry is called a gate and contains:
 * - The address of the interrupt handler
 * - The code segment selector
 * - Access flags and type information
 * 
 * This implementation:
 * 1. Defines the IDT structure and pointer
 * 2. Sets up exception handlers with descriptive error messages
 * 3. Provides functions to configure IDT gates
 * 4. Initializes the IDT with default handlers
 * 5. Maps CPU exceptions to custom handlers
 * 6. Loads the IDT into the CPU using the LIDT instruction
 * 
 * The IDT enables the kernel to:
 * - Handle CPU exceptions (e.g., divide by zero, page faults)
 * - Process hardware interrupts (e.g., keyboard, timer)
 * - Implement system calls
 * - Manage protection violations
 */

#include "idt.h"
#include "isr.h"

// IDT entries array - 256 entries for all possible interrupt vectors
struct idt_entry idt[256];
struct idt_ptr idtp;

// Human-readable messages for CPU exceptions (0-14)
const char* exception_messages[] = {
    "Division By Zero",            // 0
    "Debug",                       // 1
    "Non Maskable Interrupt",      // 2
    "Breakpoint",                  // 3
    "Overflow",                    // 4
    "Bound Range Exceeded",        // 5
    "Invalid Opcode",              // 6
    "Device Not Available",        // 7
    "Double Fault",                // 8
    "Coprocessor Segment Overrun", // 9
    "Bad TSS",                     // 10
    "Segment Not Present",         // 11
    "Stack-Segment Fault",         // 12
    "General Protection Fault",    // 13
    "Page Fault"                   // 14
};

// Assembly function to load IDT pointer into CPU
extern void load_idt(struct idt_ptr* ptr);

/**
 * idt_set_gate - Configures an IDT entry
 * @num: Interrupt vector number (0-255)
 * @base: Address of the interrupt handler
 * @sel: Code segment selector (0x08 for kernel code)
 * @flags: Gate type and attributes (e.g., IDT_INTERRUPT_GATE)
 *
 * This function splits the 32-bit handler address into low and high words
 * and sets up the IDT entry structure according to the x86 specification.
 */
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low = base & 0xFFFF;         // Lower 16 bits of handler address
    idt[num].base_high = (base >> 16) & 0xFFFF; // Upper 16 bits of handler address
    idt[num].segment = sel;                    // Code segment selector
    idt[num].reserved = 0;                     // Reserved bits (must be 0)
    idt[num].flags = flags;                    // Gate type and attributes
}

/**
 * idt_init - Initializes the Interrupt Descriptor Table (IDT)
 *
 * This function:
 * 1. Sets up the IDT pointer with the correct limit and base address
 * 2. Clears all IDT entries to zero
 * 3. Sets up exception handlers for CPU exceptions (0-14)
 * 4. Installs IRQ handlers for hardware interrupts (32-47)
 * 5. Loads the IDT using the LIDT instruction
 */
void idt_init(void) {
    // Set up IDT pointer
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base = (uint32_t)&idt;

    // Clear IDT
    for(int i = 0; i < 256; i++) {
        idt_set_gate(i, 0, 0, 0);
    }

    // Set up exception handlers
    idt_set_gate(0, (uint32_t)isr0, 0x08, IDT_INTERRUPT_GATE);  // Divide by zero
    idt_set_gate(1, (uint32_t)isr1, 0x08, IDT_INTERRUPT_GATE);  // Debug
    idt_set_gate(2, (uint32_t)isr2, 0x08, IDT_INTERRUPT_GATE);  // NMI
    idt_set_gate(3, (uint32_t)isr3, 0x08, IDT_INTERRUPT_GATE);  // Breakpoint
    idt_set_gate(4, (uint32_t)isr4, 0x08, IDT_INTERRUPT_GATE);  // Overflow
    idt_set_gate(5, (uint32_t)isr5, 0x08, IDT_INTERRUPT_GATE);  // BOUND range exceeded
    idt_set_gate(6, (uint32_t)isr6, 0x08, IDT_INTERRUPT_GATE);  // Invalid opcode
    idt_set_gate(7, (uint32_t)isr7, 0x08, IDT_INTERRUPT_GATE);  // Device not available
    idt_set_gate(8, (uint32_t)isr8, 0x08, IDT_INTERRUPT_GATE);  // Double fault
    idt_set_gate(13, (uint32_t)isr13, 0x08, IDT_INTERRUPT_GATE); // General protection fault
    idt_set_gate(14, (uint32_t)isr14, 0x08, IDT_INTERRUPT_GATE); // Page fault

    // Install IRQ handlers
    idt_set_gate(32, (uint32_t)irq0, 0x08, IDT_INTERRUPT_GATE);  // PIT
    idt_set_gate(33, (uint32_t)irq1, 0x08, 0x8E);               // Keyboard
    idt_set_gate(34, (uint32_t)irq2, 0x08, IDT_INTERRUPT_GATE);  // Cascade
    idt_set_gate(35, (uint32_t)irq3, 0x08, IDT_INTERRUPT_GATE);  // COM2
    idt_set_gate(36, (uint32_t)irq4, 0x08, IDT_INTERRUPT_GATE);  // COM1
    idt_set_gate(37, (uint32_t)irq5, 0x08, IDT_INTERRUPT_GATE);  // LPT2
    idt_set_gate(38, (uint32_t)irq6, 0x08, IDT_INTERRUPT_GATE);  // Floppy
    idt_set_gate(39, (uint32_t)irq7, 0x08, IDT_INTERRUPT_GATE);  // LPT1
    idt_set_gate(40, (uint32_t)irq8, 0x08, IDT_INTERRUPT_GATE);  // CMOS RTC
    idt_set_gate(41, (uint32_t)irq9, 0x08, IDT_INTERRUPT_GATE);  // Legacy SCSI
    idt_set_gate(42, (uint32_t)irq10, 0x08, IDT_INTERRUPT_GATE); // SCSI/NIC
    idt_set_gate(43, (uint32_t)irq11, 0x08, IDT_INTERRUPT_GATE); // SCSI/NIC
    idt_set_gate(44, (uint32_t)irq12, 0x08, IDT_INTERRUPT_GATE); // PS/2 mouse
    idt_set_gate(45, (uint32_t)irq13, 0x08, IDT_INTERRUPT_GATE); // FPU
    idt_set_gate(46, (uint32_t)irq14, 0x08, IDT_INTERRUPT_GATE); // Primary ATA
    idt_set_gate(47, (uint32_t)irq15, 0x08, IDT_INTERRUPT_GATE); // Secondary ATA

    // Load IDT
    load_idt(&idtp);

    print("[:3] IDT gates set up.\n");
    print("[:3] IDT initialized.\n");
}

/**
 * isr_handler - Handles CPU exceptions
 * @r: Pointer to CPU register state at time of interrupt
 *
 * This function:
 * 1. Checks if the interrupt number corresponds to a CPU exception (0-15)
 * 2. Prints the exception message and additional details for specific exceptions
 * 3. Halts the system by entering an infinite loop with HLT instructions
 */
void isr_handler(struct registers* r) {
    if (r->int_no < 15) {
        print("\nEXCEPTION CAUGHT: ");
        print(exception_messages[r->int_no]);
        print("\n");
        
        // Additional information for specific exceptions
        switch(r->int_no) {
            case 0:
                print("[>:3] EIP: Attempted division by zero\n");
                break;
                
            case 6:
                print("[>:3] EIP: Invalid instruction executed\n");
                break;
                
            case 8:
                print("[>:3] Double fault - system error\n");
                break;
                
            case 13:
                print("[>:3] General protection fault\n");
                break;
                
            case 14:
                print("[>:3] Page Fault ( ");
                if (!(r->err_code & 0x1)) print("present ");
                if (r->err_code & 0x2) print("write ");
                if (r->err_code & 0x4) print("user ");
                if (r->err_code & 0x8) print("reserved ");
                print(")\n");
                break;
        }
    }
    
    print("[>:3] System Halted!\n");
    while(1) {
        __asm__ volatile("hlt");
    }
}