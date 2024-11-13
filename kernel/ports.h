#ifndef PORTS_H
#define PORTS_H

#include "types.h"

static inline uint8_t inb(uint16_t port) {
    uint8_t result;
    __asm__("in %%dx, %%al" : "=a" (result) : "d" (port));
    return result;
}

static inline void outb(uint16_t port, uint8_t data) {
    __asm__("out %%al, %%dx" : : "a" (data), "d" (port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t result;
    __asm__("in %%dx, %%ax" : "=a" (result) : "d" (port));
    return result;
}

static inline void outw(uint16_t port, uint16_t data) {
    __asm__("out %%ax, %%dx" : : "a" (data), "d" (port));
}

#endif 