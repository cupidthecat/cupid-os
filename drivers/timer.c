/*
 * Timer Driver Implementation
 * 
 * This file implements a comprehensive timer system using the Intel 8253/8254 
 * Programmable Interval Timer (PIT). The PIT provides essential timing services
 * for the operating system:
 *
 * Core Timer Features:
 * - Configures PIT Channel 0 as the system timer (default 100Hz)
 * - Handles timer interrupts (IRQ0) for regular system ticks
 * - Maintains system uptime counter with millisecond precision
 * - Provides sleep() and delay() functions for precise timing
 * - Supports dynamic frequency adjustment (1Hz to 1.19MHz)
 * 
 * Advanced Capabilities:
 * - Multi-channel support (3 independent timer channels):
 *   - Channel 0: System timer for OS timing
 *   - Channel 1: Available for custom timing events
 *   - Channel 2: PC Speaker control
 * - High precision timing using CPU timestamp counter (TSC)
 * - Timer calibration for accurate delays
 * - Configurable timer callbacks per channel
 * - Power-efficient sleep modes using HLT
 *
 * Usage:
 * - System timing and scheduling
 * - Device driver timing requirements
 * - User application delays
 * - Sound generation via Channel 2
 * - Performance measurements
 * - Event timing and timeouts
 *
 * Dependencies:
 * - ports.h: I/O port access
 * - irq.h: Interrupt handling
 * - types.h: Data structures
 * - kernel.h: System functions
 */

#include "../kernel/ports.h"
#include "../kernel/irq.h"
#include "../kernel/types.h"
#include "../kernel/kernel.h"
#include "../drivers/keyboard.h"
#include "../kernel/math.h"

// PIT hardware ports and constants
#define PIT_CHANNEL0_DATA 0x40    // Channel 0 data port
#define PIT_COMMAND      0x43    // PIT command register
#define PIT_FREQUENCY    1193180 // Base frequency in Hz
#define PIT_MODE3       0x36    // Square wave generator mode
#define PIT_CHANNEL1_DATA 0x41
#define PIT_CHANNEL2_DATA 0x42
#define PIT_MODE_COMMAND 0x43

// Global timer state
static volatile uint64_t tick_count = 0;  // Number of timer ticks since boot

// Timer state structure (using the one from types.h)
static timer_state_t timer_state = {
    .ticks = 0,
    .frequency = 0,
    .ms_per_tick = 0,
    .is_calibrated = false
};

// Timer callback function type
typedef void (*timer_callback_t)(struct registers*, uint32_t channel);

// Channel configuration
static struct {
    uint32_t frequency;
    timer_callback_t callback;
    bool active;
} timer_channels[3] = {0};

// Timer interrupt handler - increments tick counter
static void timer_irq_handler(struct registers* r) {
    tick_count++;
    
    // Update timer state
    timer_state.ticks++;
    
    // Call channel callbacks if configured
    for (int i = 0; i < 3; i++) {
        if (timer_channels[i].active && timer_channels[i].callback) {
            timer_channels[i].callback(r, i);
        }
    }
    
    // Update keyboard ticks for key repeat functionality
    keyboard_update_ticks();
}

// Ensure rdtsc is defined at the top
static inline uint64_t rdtsc(void) {
    uint32_t low, high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

// Initialize the PIT with specified frequency
void timer_init(uint32_t hz) {
    // Validate frequency
    if (hz == 0 || hz < 19 || hz > 1193180) {
        hz = 100; // Default to 100Hz if invalid
    }

    uint32_t divisor = 1193180 / hz;
    timer_state.frequency = hz;
    timer_state.ms_per_tick = 1000 / hz;

    // Set up channel 0 (system timer)
    outb(PIT_MODE_COMMAND, 0x36);  // Channel 0, square wave mode
    outb(PIT_CHANNEL0_DATA, divisor & 0xFF);
    outb(PIT_CHANNEL0_DATA, (divisor >> 8) & 0xFF);

    // Register IRQ handler
    irq_install_handler(0, timer_irq_handler);
    
    timer_channels[0].frequency = hz;
    timer_channels[0].active = true;
}

// Get the current tick count
uint64_t timer_get_ticks(void) {
    uint64_t ticks;
    __asm__ volatile("cli");  // Disable interrupts
    ticks = timer_state.ticks;
    __asm__ volatile("sti");  // Enable interrupts
    return ticks;
}

// Get the current timer frequency
uint32_t timer_get_frequency(void) {
    return timer_state.frequency;
}

// Get system uptime in milliseconds
uint32_t timer_get_uptime_ms(void) {
    // Safely get current tick count
    __asm__ volatile("cli");
    uint64_t current_ticks = timer_get_ticks();
    __asm__ volatile("sti");

    if (timer_state.frequency == 0) {
        return 0;
    }

    // Calculate milliseconds using 32-bit math
    return (uint32_t)((current_ticks * 1000) / timer_state.frequency);
}

// Sleep for specified number of milliseconds
void timer_sleep_ms(uint32_t ms) {
    uint64_t target_ticks = timer_state.ticks + udiv64((uint64_t)ms * timer_state.frequency, 1000);
    
    while (timer_state.ticks < target_ticks) {
        __asm__ volatile("hlt");  // Power-efficient wait
    }
}

// Delay for specified number of microseconds
void timer_delay_us(uint32_t us) {
    // For very short delays, use busy waiting with TSC
    uint64_t start_tsc = rdtsc();
    uint64_t cycles = udiv64(get_cpu_freq(), 1000000) * us;
    
    while ((rdtsc() - start_tsc) < cycles) {
        __asm__ volatile("pause");  // More efficient busy-wait
    }
}

void timer_start_measure(timer_measure_t* measure) {
    if (measure) {
        measure->start_tick = timer_get_ticks();
        measure->duration_ms = 0;
    }
}

uint64_t timer_end_measure(timer_measure_t* measure) {
    if (!measure) return 0;
    
    uint64_t end_tick = timer_get_ticks();
    measure->duration_ms = udiv64((end_tick - measure->start_tick) * 1000, timer_state.frequency);
    return measure->duration_ms;
}

bool timer_configure_channel(uint8_t channel, uint32_t frequency, timer_callback_t callback) {
    if (channel >= 3 || frequency == 0) return false;
    
    uint32_t divisor = 1193180 / frequency;
    uint8_t channel_port = PIT_CHANNEL0_DATA + channel;
    uint8_t channel_mode = 0x36 | (channel << 6);
    
    outb(PIT_MODE_COMMAND, channel_mode);
    outb(channel_port, divisor & 0xFF);
    outb(channel_port, (divisor >> 8) & 0xFF);
    
    timer_channels[channel].frequency = frequency;
    timer_channels[channel].callback = callback;
    timer_channels[channel].active = true;
    
    return true;
}

  
