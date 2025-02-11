#ifndef IRQ_H
#define IRQ_H

#include "types.h"
#include "isr.h"

/* 
 * IRQ vector numbers (32-47)
 * These define the interrupt vector numbers for hardware IRQs 0-15
 * IRQs are mapped to interrupt vectors starting at 32 to avoid conflicts
 * with CPU exceptions (0-31)
 */
#define IRQ0 32   // Programmable Interval Timer (PIT)
#define IRQ1 33   // Keyboard
#define IRQ2 34   // Cascade (used internally by PICs)
#define IRQ3 35   // COM2/Serial Port 2
#define IRQ4 36   // COM1/Serial Port 1
#define IRQ5 37   // LPT2/Parallel Port 2
#define IRQ6 38   // Floppy Disk Controller
#define IRQ7 39   // LPT1/Parallel Port 1
#define IRQ8 40   // CMOS Real-Time Clock
#define IRQ9 41   // Legacy SCSI/NIC
#define IRQ10 42  // SCSI/NIC
#define IRQ11 43  // SCSI/NIC
#define IRQ12 44  // PS/2 Mouse
#define IRQ13 45  // FPU/Coprocessor
#define IRQ14 46  // Primary ATA Hard Disk
#define IRQ15 47  // Secondary ATA Hard Disk

/* 
 * IRQ handler function type
 * Defines the signature for interrupt handler functions
 * @param regs: Pointer to CPU register state at time of interrupt
 */
typedef void (*irq_handler_t)(struct registers*);

/* Function prototypes */
void register_interrupt_handler(uint8_t n, irq_handler_t handler);  // Register handler for specific IRQ
void irq_install_handler(int irq, irq_handler_t handler);           // Install IRQ handler
void irq_uninstall_handler(int irq);                                // Remove IRQ handler
void irq_handler(struct registers* r);

#endif 