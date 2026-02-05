#ifndef ISR_H
#define ISR_H

#include "types.h"

// Function to print (defined in kernel.c)
void print(const char* str);

// Struct to hold register state during interrupts
struct registers {
    uint32_t gs, fs, es, ds;                         // Pushed segment registers
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; // Pushed by pusha
    uint32_t int_no, err_code;                       // Interrupt number and error code
    uint32_t eip, cs, eflags, useresp, ss;           // Pushed by the processor automatically
};

// IRQ handler type
typedef void (*irq_handler_t)(struct registers*);

// Exception handlers
extern void isr0(void);  // Divide by zero
extern void isr1(void);  // Debug
extern void isr2(void);  // Non-maskable interrupt
extern void isr3(void);  // Breakpoint
extern void isr4(void);  // Overflow
extern void isr5(void);  // Bound range exceeded
extern void isr6(void);  // Invalid opcode
extern void isr7(void);  // Device not available
extern void isr8(void);  // Double fault
extern void isr13(void); // General protection fault
extern void isr14(void); // Page fault

// IRQ handlers
extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

// IRQ numbers
#define IRQ0 32
#define IRQ1 33
#define IRQ2 34
#define IRQ3 35
#define IRQ4 36
#define IRQ5 37
#define IRQ6 38
#define IRQ7 39
#define IRQ8 40
#define IRQ9 41
#define IRQ10 42
#define IRQ11 43
#define IRQ12 44
#define IRQ13 45
#define IRQ14 46
#define IRQ15 47

// Function declarations
void irq_install_handler(int irq, irq_handler_t handler);
void irq_uninstall_handler(int irq);
void isr_handler(struct registers* r);

#endif 
