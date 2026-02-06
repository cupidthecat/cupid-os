#ifndef PANIC_H
#define PANIC_H

#include "types.h"
#include "isr.h"

/* Panic – never returns.  Prints message to VGA + serial, halts CPU. */
void kernel_panic(const char *fmt, ...) __attribute__((noreturn));

/* Panic with register context (called from ISR / exception paths). */
void kernel_panic_regs(struct registers *regs,
                       const char *fmt, ...) __attribute__((noreturn));

/* Print a stack trace starting from the given EBP / EIP. */
void print_stack_trace(uint32_t ebp, uint32_t eip);

/* Set output functions for panic/debug output (for GUI mode support) */
void panic_set_output(void (*print_fn)(const char*), void (*putchar_fn)(char));

/* Capture current registers and panic (for use in macros). */
#define PANIC(msg, ...) do { \
        uint32_t _ebp, _eip; \
        __asm__ volatile("movl %%ebp, %0" : "=r"(_ebp)); \
        __asm__ volatile("call 1f\n1: popl %0" : "=r"(_eip)); \
        kernel_panic(msg, ##__VA_ARGS__); \
    } while(0)

#endif
