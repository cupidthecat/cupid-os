#include "math.h"
#include "types.h"
#include "kernel.h"

// 64-bit unsigned division
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

// Converts an integer to a string.
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

// Prints an unsigned 32-bit value in hexadecimal format.
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