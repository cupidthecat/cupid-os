#ifndef ISR_H
#define ISR_H

#include "types.h"

// Function to print (defined in kernel.c)
void print(const char* str);

// Exception handlers
extern void isr0(void);
extern void isr1(void);

// C handler called from assembly
void isr_handler(void);

#endif 