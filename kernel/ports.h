#ifndef PORTS_H
#define PORTS_H

#include "types.h"

static inline uint8_t inb(uint16_t port) {
    uint8_t result;
    __asm__ volatile("in %%dx, %%al" : "=a" (result) : "d" (port));
    return result;
}

static inline void outb(uint16_t port, uint8_t data) {
    __asm__ volatile("out %%al, %%dx" : : "a" (data), "d" (port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t result;
    __asm__ volatile("in %%dx, %%ax" : "=a" (result) : "d" (port));
    return result;
}

static inline void outw(uint16_t port, uint16_t data) {
    __asm__ volatile("out %%ax, %%dx" : : "a" (data), "d" (port));
}

// Read multiple 16-bit words from port
static inline void insw(uint16_t port, void* buffer, uint32_t count) {
    __asm__ volatile("cld; rep insw"
        : "+D" (buffer), "+c" (count)
        : "d" (port)
        : "memory");
}

// Write multiple 16-bit words to port
static inline void outsw(uint16_t port, const void* buffer, uint32_t count) {
    __asm__ volatile("cld; rep outsw"
        : "+S" (buffer), "+c" (count)
        : "d" (port));
}

#endif 