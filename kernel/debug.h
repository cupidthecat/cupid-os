// debug.h
#ifndef DEBUG_H
#define DEBUG_H

#include "kernel.h"   // for print(), print_int(), etc.

/**
 * debug_print_int - Prints a label followed by a 32-bit integer value
 * @label: The string label to print before the value
 * @value: The 32-bit integer value to print
 *
 * This helper function prints a descriptive label followed by an integer value
 * in decimal format, ending with a newline. Useful for debugging numeric values.
 */
static inline void debug_print_int(const char *label, uint32_t value) {
    print(label);
    print_int(value);
    print("\n");
}

/**
 * debug_print_hex - Prints a label followed by a 32-bit value in hexadecimal
 * @label: The string label to print before the value
 * @value: The 32-bit value to print in hexadecimal
 *
 * This helper function prints a descriptive label followed by a value in
 * hexadecimal format, ending with a newline. Useful for debugging memory
 * addresses or bit patterns.
 */
static inline void debug_print_hex(const char *label, uint32_t value) {
    print(label);
    print_hex(value);
    print("\n");
}

#endif
