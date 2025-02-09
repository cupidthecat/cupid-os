#include "math.h"
#include "types.h"
#include "kernel.h"

/**
 * udiv64 - Performs 64-bit unsigned division
 * @dividend: The 64-bit number to be divided
 * @divisor: The 32-bit number to divide by
 * 
 * This function implements binary long division to divide a 64-bit unsigned
 * integer by a 32-bit unsigned integer. It works by:
 * 1. Initializing quotient and remainder to 0
 * 2. Iterating through each bit of the dividend from MSB to LSB
 * 3. For each bit:
 *    - Shifting the remainder left and adding the current bit
 *    - If remainder >= divisor, subtract divisor and set quotient bit
 * 
 * Returns: 64-bit quotient of the division
 */
uint64_t udiv64(uint64_t dividend, uint32_t divisor) {
    uint64_t quotient = 0;
    uint64_t remainder = 0;
    for (int i = 63; i >= 0; i--) {
        remainder = (remainder << 1) | ((dividend >> i) & 1);
        if (remainder >= divisor) {
            remainder -= divisor;
            quotient |= (1ULL << i);
        }
    }
    return quotient;
}

/**
 * itoa - Converts an integer to a null-terminated string
 * @value: The integer to convert (can be negative)
 * @str: Pointer to buffer to store the result
 * 
 * This function converts an integer to its string representation by:
 * 1. Handling the special case of 0
 * 2. Handling negative numbers by recording the sign
 * 3. Extracting digits from the number using modulo 10
 * 4. Reversing the digits to get the correct order
 * 
 * Returns: Pointer to the resulting string (same as input str)
 */
char* itoa(int value, char* str) {
    int i = 0;
    int is_negative = 0;

    if (value == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return str;
    }

    if (value < 0) {
        is_negative = 1;
        value = -value;
    }

    while (value != 0) {
        int rem = value % 10;
        str[i++] = rem + '0';
        value /= 10;
    }

    if (is_negative)
        str[i++] = '-';

    str[i] = '\0';

    // Reverse the string
    int start = 0;
    int end = i - 1;
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }

    return str;
}

/**
 * print_hex - Prints a 32-bit unsigned integer in hexadecimal format
 * @n: The number to print in hex
 * 
 * This function converts a 32-bit unsigned integer to its hexadecimal
 * representation and prints it to the screen. It:
 * 1. Uses a lookup table for hex digits (0-F)
 * 2. Extracts each nibble (4 bits) from the number
 * 3. Builds the hex string in reverse order
 * 4. Prints the result with "0x" prefix
 */
void print_hex(uint32_t n) {
    char hex_chars[] = "0123456789ABCDEF";
    char hex[9];
    hex[8] = '\0';

    for (int i = 7; i >= 0; i--) {
        hex[i] = hex_chars[n & 0xF];
        n >>= 4;
    }

    print("0x");
    print(hex);
}

/*
 * __udivdi3 - Perform unsigned 64-bit division.
 *
 * This routine divides a 64-bit unsigned integer by another 64-bit
 * unsigned integer using a simple binary long division algorithm.
 *
 * Parameters:
 *   dividend - the 64-bit numerator
 *   divisor  - the 64-bit denominator
 *
 * Returns:
 *   The 64-bit quotient.
 */
uint64_t __udivdi3(uint64_t dividend, uint64_t divisor) {
    uint64_t quotient = 0;
    uint64_t remainder = 0;
    int i;

    // Check for divisor zero (you may want to handle this differently)
    if (divisor == 0) {
        // Division by zero; in a kernel you might want to halt or raise an error.
        while (1)
            ;
    }

    // Loop over each bit from high to low
    for (i = 63; i >= 0; i--) {
        remainder = (remainder << 1) | ((dividend >> i) & 1);
        if (remainder >= divisor) {
            remainder -= divisor;
            quotient |= (1ULL << i);
        }
    }
    return quotient;
}