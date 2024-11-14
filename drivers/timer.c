/*
 * Timer Driver Implementation
 * 
 * This file implements the Programmable Interval Timer (PIT) functionality:
 * - Initializes PIT Channel 0 for system timing
 * - Provides high precision timing services
 * - Handles timer interrupts (IRQ0)
 * - Tracks system uptime
 * - Implements sleep/delay functions
 * - Supports variable timer frequencies
 */

#include "../kernel/ports.h"
#include "../kernel/irq.h"
#include "../kernel/types.h"

// PIT hardware ports and constants
#define PIT_CHANNEL0_DATA 0x40    // Channel 0 data port
#define PIT_COMMAND      0x43    // PIT command register
#define PIT_FREQUENCY    1193180 // Base frequency in Hz
#define PIT_MODE3       0x36    // Square wave generator mode

// Global timer state
static volatile uint32_t tick_count = 0;  // Number of timer ticks since boot
static uint32_t frequency = 0;            // Current timer frequency in Hz

// Timer interrupt handler - increments tick counter
static void timer_callback(struct registers* regs) {
    tick_count++;
}

// Initialize the PIT with specified frequency
void timer_init(uint32_t hz) {
    frequency = hz;
    
    // Calculate divisor for requested frequency
    uint32_t divisor = PIT_FREQUENCY / hz;
    
    // Configure PIT Channel 0 in Mode 3 (square wave)
    outb(PIT_COMMAND, PIT_MODE3);
    
    // Program the frequency divisor
    uint8_t low = (uint8_t)(divisor & 0xFF);         // Low byte
    uint8_t high = (uint8_t)((divisor >> 8) & 0xFF); // High byte
    outb(PIT_CHANNEL0_DATA, low);
    outb(PIT_CHANNEL0_DATA, high);
    
    // Register timer interrupt handler
    irq_install_handler(0, timer_callback);
}

// Get the current tick count
uint32_t timer_get_ticks(void) {
    return tick_count;
}

// Get the current timer frequency
uint32_t timer_get_frequency(void) {
    return frequency;
}

// Get system uptime in milliseconds
uint32_t timer_get_uptime_ms(void) {
    return (tick_count * 1000) / frequency;
}

// Sleep for specified number of milliseconds
void timer_sleep_ms(uint32_t ms) {
    uint32_t start = timer_get_ticks();
    uint32_t ticks_to_wait = (ms * frequency) / 1000;
    
    while (timer_get_ticks() - start < ticks_to_wait) {
        __asm__ volatile("hlt");  // CPU sleep until next interrupt
    }
}

// Delay for specified number of microseconds
void timer_delay_us(uint32_t us) {
    uint32_t start = timer_get_ticks();
    uint32_t ticks_to_wait = (us * frequency) / 1000000;
    
    // Ensure at least 1 tick delay for very short intervals
    if (ticks_to_wait == 0) ticks_to_wait = 1;
    
    while (timer_get_ticks() - start < ticks_to_wait) {
        __asm__ volatile("hlt");  // CPU sleep until next interrupt
    }
} 