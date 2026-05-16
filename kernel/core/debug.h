// debug.h
#ifndef DEBUG_H
#define DEBUG_H

#include "kernel.h"   // for print(), print_int(), etc.

// A simple helper function to print a label followed by an integer value.
static inline void debug_print_int(const char *label, uint32_t value) {
    print(label);
    print_int(value);
    print("\n");
}

// Similarly, for 64-bit values (or you can print them in hex)
static inline void debug_print_hex(const char *label, uint32_t value) {
    print(label);
    print_hex(value);
    print("\n");
}

#endif
