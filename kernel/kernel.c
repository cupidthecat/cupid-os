/**
 * kernel.c - Core kernel functionality for cupid-os
 * 
 * This file implements the main kernel functionality including:
 * - Core kernel initialization and entry point (_start and kmain)
 * - VGA text mode driver with 80x25 character display
 * - Screen output functions (print, putchar)
 * - Screen manipulation (clear_screen, cursor movement) 
 * - Port I/O functions (inb/outb) for hardware interaction
 * - Interrupt handling setup (PIC, IDT initialization)
 * - PS/2 keyboard driver initialization and interrupt handling
 * - Timer calibration and frequency measurement
 * - System timing services via PIT channels
 * - Main kernel loop with interrupt handling and power management
 */

#include "idt.h"
#include "pic.h"
#include "kernel.h"
#include "debug.h"
#include "ports.h"
#include "shell.h"
#include "types.h"
#include "../drivers/speaker.h"
#include "../drivers/keyboard.h"
#include "../drivers/timer.h"
#include "../drivers/vga.h"
#include "../filesystem/fs.h"

#define PIT_FREQUENCY 1193180    // Base PIT frequency in Hz
#define CALIBRATION_MS 250        // Time to calibrate over (in milliseconds)

static uint64_t tsc_freq = 0;    // Stores CPU timestamp frequency
static uint32_t pit_ticks_per_ms = 0; // Stores PIT ticks per millisecond

// Assembly entry point
void _start(void) __attribute__((section(".text.start")));
// Main kernel function
void kmain(void);

// Global tick counters
static uint32_t ticks_channel0 = 0;
static uint32_t ticks_channel1 = 0;

/**
 * timer_callback_channel0 - Timer callback for channel 0
 * 
 * Increments the ticks_channel0 counter when called with channel 0.
 * 
 * @param r: Pointer to the registers structure
 * @param channel: The timer channel (0 in this case)
 */
void timer_callback_channel0(struct registers* r, uint32_t channel) {
    if (channel == 0) {
        ticks_channel0++;
    }
}
/**
 * timer_callback_channel1 - Timer callback for channel 1
 * 
 * Increments the ticks_channel1 counter when called with channel 1.
 * 
 * @param r: Pointer to the registers structure
 * @param channel: The timer channel (1 in this case)
 */
void timer_callback_channel1(struct registers* r, uint32_t channel) {
    if (channel == 1) {
        ticks_channel1++;
    }
}

/**
 * timer_get_ticks_channel - Get the tick count for a specific timer channel
 * 
 * Returns the current tick count for the specified timer channel.
 * - Channel 0: System tick counter
 * - Channel 1: Reserved for future use (currently unused)
 * 
 * @param channel: The timer channel to get ticks for (0 or 1)
 * @return: The current tick count for the specified channel
 */
uint32_t timer_get_ticks_channel(uint32_t channel) {
    if (channel == 0) {
        return ticks_channel0;
    } else if (channel == 1) {
        return ticks_channel1;
    }
    return 0;
}

/**
 * print_int - Prints an unsigned 32-bit integer to the screen
 * 
 * Converts the number to a string by repeatedly dividing by 10 and storing
 * the digits in a buffer. Since division gives digits in reverse order,
 * stores them in a buffer first then prints in reverse to display correctly.
 * Special cases the value 0 to avoid buffer manipulation.
 *
 * @param num: The unsigned 32-bit integer to print
 *
 * Implementation notes:
 * - Uses a fixed 10-byte buffer which is sufficient for 32-bit integers
 * - Converts digits to ASCII by adding '0' (0x30)
 * - Prints digits in reverse order to maintain correct number representation
 */
void print_int(uint32_t num) {
    char buffer[10];
    int i = 0;
    if (num == 0) {
        putchar('0');
        return;
    }
    while (num > 0) {
        buffer[i++] = (num % 10) + '0';
        num /= 10;
    }
    while (i > 0) {
        putchar(buffer[--i]);
    }
}

/**
 * print - Outputs a null-terminated string to the VGA text buffer
 * 
 * Iterates through each character in the provided string and displays it
 * on screen using putchar(). Handles special characters like newlines and
 * automatically wraps text and scrolls when reaching screen boundaries.
 * 
 * @str: Pointer to the null-terminated string to print
 */
void print(const char* str) {
    for(int i = 0; str[i] != '\0'; i++) {
        putchar(str[i]);
    }
}

/**
 * _start - Entry point for the kernel
 * 
 * This is the first function called after the bootloader hands control to the kernel.
 * It sets up the initial execution environment by:
 * 
 * 1. Setting up the stack:
 *    - Sets stack pointer (ESP) to 0x90000 for kernel stack space
 *    - Initializes base pointer (EBP) to match stack pointer
 * 
 * 2. Transferring control:
 *    - Calls kmain() to begin kernel initialization
 *    - kmain() never returns as it contains the main kernel loop
 * 
 * Note: When this function runs, we are already in 32-bit protected mode
 * with basic segment registers configured by the bootloader.
 */
void _start(void) {
    // We're already in protected mode with segments set up
    __asm__ volatile(
        "mov $0x90000, %esp\n"
        "mov %esp, %ebp\n"
    );
    
    // Call main kernel function
    kmain();
}

/**
 * rdtsc - Read the CPU's Time Stamp Counter
 * 
 * Uses the RDTSC instruction to read the CPU's internal timestamp counter,
 * which increments at the CPU's frequency. The counter value is returned
 * as a 64-bit number combining the high and low 32-bit parts.
 * 
 * Used for high-precision timing and CPU frequency calibration.
 * 
 * @return: 64-bit TSC value
 */
static inline uint64_t rdtsc(void) {
    uint32_t low, high;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return ((uint64_t)high << 32) | low;
}

/**
 * calibrate_timer - Calibrate system timer using CPU timestamp counter
 * 
 * This function calibrates the system timer by:
 * 1. Configuring PIT channel 0 in one-shot mode
 * 2. Using the CPU's timestamp counter (TSC) to measure elapsed time
 * 3. Calculating the CPU frequency based on TSC measurements
 * 4. Computing PIT ticks per millisecond for timing calculations
 * 
 * The calibration process:
 * - Sets PIT to maximum count and waits for CALIBRATION_MS milliseconds
 * - Measures TSC values before and after to determine CPU frequency
 * - Handles both high and low frequency CPUs by adjusting calculation method
 * - Resets PIT to normal operation when complete
 * - Prints calibration results showing CPU frequency in MHz
 * 
 * Results are stored in global variables:
 * - tsc_freq: CPU frequency in Hz
 * - pit_ticks_per_ms: PIT ticks per millisecond for timing
 */
void calibrate_timer(void) {
    // Configure PIT channel 0 for one-shot mode
    outb(0x43, 0x30);    // Channel 0, one-shot mode, binary
    
    // Calculate maximum safe duration for one-shot mode (55ms)
    uint32_t max_ticks = 0xFFFF;
    uint32_t actual_ms = (max_ticks * 1000) / PIT_FREQUENCY;
    if(actual_ms > 50) actual_ms = 50;  // Clamp to 50ms max
    
    // Set initial count to maximum safe value
    uint16_t initial_count = (PIT_FREQUENCY * actual_ms) / 1000;
    outb(0x40, initial_count & 0xFF);
    outb(0x40, (initial_count >> 8) & 0xFF);
    
    // Get starting TSC value
    uint64_t start_tsc = rdtsc();
    
    // Wait for PIT to count down
    uint32_t count;
    do {
        outb(0x43, 0x00); // Latch counter
        count = inb(0x40);
        count |= inb(0x40) << 8;
    } while(count > 0 && count < 0xFFFF);
    
    // Get ending TSC value
    uint64_t end_tsc = rdtsc();
    
    // Calculate CPU frequency with 128-bit intermediate
    uint64_t tsc_diff = end_tsc - start_tsc;
    uint64_t freq_hz = (tsc_diff * 1000) / actual_ms;
    
    // Fallback checks
    if(freq_hz < 1000000) {  // Below 1MHz is impossible for modern CPUs
        freq_hz = 1000000000; // Default to 1GHz
    }
    
    tsc_freq = freq_hz;
    pit_ticks_per_ms = PIT_FREQUENCY / 1000;
    
    // Reset PIT to normal operation
    timer_init(100);
    
    //print("Timer calibration complete:\n");
    //debug_print_int("Calibration Window (ms): ", actual_ms);
    //debug_print_int("CPU Frequency (MHz): ", (uint32_t)(tsc_freq / 1000000));
}

/**
 * get_cpu_freq - Get the calibrated CPU frequency
 * 
 * Returns the CPU frequency in Hz that was measured during timer calibration.
 * This value represents the number of CPU cycles per second and is used for
 * precise timing calculations.
 *
 * @return: The CPU frequency in Hz as measured by the TSC during calibration
 */
uint64_t get_cpu_freq(void) {
    return tsc_freq;
}

/**
 * get_pit_ticks_per_ms - Get the number of PIT ticks per millisecond
 * 
 * Returns the calibrated number of PIT (Programmable Interval Timer) ticks
 * that occur in one millisecond. This value is determined during timer
 * calibration and is used for accurate timing calculations.
 *
 * @return: The number of PIT ticks per millisecond
 */
uint32_t get_pit_ticks_per_ms(void) {
    return pit_ticks_per_ms;
}

/**
 * kmain - Main kernel entry point
 * 
 * This function initializes core kernel subsystems and drivers:
 * - VGA text mode display initialization for console output
 * - PIC (Programmable Interrupt Controller) setup for hardware interrupts
 * - IDT (Interrupt Descriptor Table) configuration for interrupt handling
 * - PS/2 keyboard driver initialization with event buffer
 * - PIT (Programmable Interval Timer) calibration and setup
 * - System timer configuration for accurate timing
 *
 * After initialization, the kernel enters an idle loop where interrupts
 * drive system activity through keyboard input processing and timer events.
 * The system remains in this state indefinitely, with the CPU halted between
 * interrupt events to conserve power.
 */
void kmain(void) {
    init_vga();
    clear_screen();
    idt_init();
    pic_init();
    keyboard_init();
    calibrate_timer();
    fs_init();

    debug_print_int("[:3] System Timer Frequency: ", timer_get_frequency());
    debug_print_int("[:3] CPU Frequency (MHz): ", (uint32_t)(get_cpu_freq() / 1000000));

    print("VGA Color Text Support Test!!\n");
    // Test VGA colors
    vga_set_color(VGA_RED, VGA_BLACK);
    print("Red text on black background\n");
    
    vga_set_color(VGA_GREEN, VGA_BLACK);
    print("Green text on black background\n");
    
    vga_set_color(VGA_BLUE, VGA_BLACK);
    print("Blue text on black background\n");
    
    vga_set_color(VGA_WHITE, VGA_BLUE);
    print("White text on blue background\n");
    
    vga_set_color(VGA_YELLOW, VGA_RED);
    print("Yellow text on red background\n");
    
    // Reset to default colors
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    print("Back to default colors\n");

    pic_clear_mask(1);
    __asm__ volatile("sti");
    
    shell_run();
    
    while(1) {
        __asm__ volatile("hlt");
    }
}

