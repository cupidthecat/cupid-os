#ifndef ISR_H
#define ISR_H

#include "types.h"

// Function to print strings to the screen
// Defined in kernel.c, used for debug output during interrupts
void print(const char* str);

// Structure representing CPU register state during an interrupt
// This is pushed onto the stack when an interrupt occurs and is used
// by interrupt handlers to access CPU state
struct registers {
    uint32_t ds;                                     // Data segment selector
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; // General purpose registers (pushed by pusha)
    uint32_t int_no, err_code;                       // Interrupt number and optional error code
    uint32_t eip, cs, eflags, useresp, ss;          // Automatically pushed by CPU on interrupt
};

// Type definition for IRQ handler functions
// Each handler receives a pointer to the saved register state
typedef void (*irq_handler_t)(struct registers*);

// Exception handler declarations - these are implemented in isr.asm
// and handle CPU exceptions (faults, traps, and aborts)
extern void isr0(void);  // Divide by zero exception
extern void isr1(void);  // Debug exception
extern void isr2(void);  // Non-maskable interrupt
extern void isr3(void);  // Breakpoint exception
extern void isr4(void);  // Overflow exception
extern void isr5(void);  // Bound range exceeded exception
extern void isr6(void);  // Invalid opcode exception
extern void isr7(void);  // Device not available exception
extern void isr8(void);  // Double fault exception
extern void isr13(void); // General protection fault exception
extern void isr14(void); // Page fault exception

// IRQ handler declarations - these are implemented in isr.asm
// and handle hardware interrupts from devices
extern void irq0(void);  // Programmable Interval Timer (PIT)
extern void irq1(void);  // Keyboard
extern void irq2(void);  // Cascade (used internally by PICs)
extern void irq3(void);  // COM2/Serial Port 2
extern void irq4(void);  // COM1/Serial Port 1
extern void irq5(void);  // LPT2/Parallel Port 2
extern void irq6(void);  // Floppy Disk Controller
extern void irq7(void);  // LPT1/Parallel Port 1
extern void irq8(void);  // CMOS Real-Time Clock
extern void irq9(void);  // Legacy SCSI/NIC
extern void irq10(void); // SCSI/NIC
extern void irq11(void); // SCSI/NIC
extern void irq12(void); // PS/2 Mouse
extern void irq13(void); // FPU/Coprocessor
extern void irq14(void); // Primary ATA Hard Disk
extern void irq15(void); // Secondary ATA Hard Disk

// IRQ vector numbers - hardware interrupts are mapped to
// interrupt vectors 32-47 to avoid conflicts with CPU exceptions
#define IRQ0 32   // PIT
#define IRQ1 33   // Keyboard
#define IRQ2 34   // Cascade
#define IRQ3 35   // COM2
#define IRQ4 36   // COM1
#define IRQ5 37   // LPT2
#define IRQ6 38   // Floppy
#define IRQ7 39   // LPT1
#define IRQ8 40   // RTC
#define IRQ9 41   // Legacy SCSI/NIC
#define IRQ10 42  // SCSI/NIC
#define IRQ11 43  // SCSI/NIC
#define IRQ12 44  // PS/2 Mouse
#define IRQ13 45  // FPU
#define IRQ14 46  // Primary ATA
#define IRQ15 47  // Secondary ATA

// Function declarations for interrupt management
void irq_install_handler(int irq, irq_handler_t handler);  // Install custom IRQ handler
void irq_uninstall_handler(int irq);                       // Remove IRQ handler
void isr_handler(struct registers* r);                     // Common ISR handler

#endif 