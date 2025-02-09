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
#include "../drivers/speaker.h"
#include "../drivers/keyboard.h"
#include "../drivers/timer.h"
#include "ports.h"
#include "shell.h"

// Assembly entry point
void _start(void) __attribute__((section(".text.start")));
// Main kernel function
void kmain(void);

// Screen dimensions
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY 0xB8000

// Colors
#define VGA_BLACK 0
#define VGA_LIGHT_GREY 7
#define VGA_WHITE 15

// Screen position
int cursor_x = 0;
int cursor_y = 0;

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

#define VGA_CTRL_REGISTER 0x3D4
#define VGA_DATA_REGISTER 0x3D5
#define VGA_OFFSET_LOW 0x0F
#define VGA_OFFSET_HIGH 0x0E

void print(const char* str);
void putchar(char c);
void clear_screen(void);
void init_vga(void);

/**
 * init_vga - Initialize the VGA text mode display
 * 
 * This function initializes the VGA text mode display by:
 * - Resetting the hardware cursor position to (0,0)
 * - Clearing the screen with light grey text on black background
 * - Resetting the software cursor position variables
 * - Printing an initialization message
 */
void init_vga(void) {
    // Reset the cursor position
    outb(VGA_CTRL_REGISTER, VGA_OFFSET_HIGH);
    outb(VGA_DATA_REGISTER, 0);
    outb(VGA_CTRL_REGISTER, VGA_OFFSET_LOW);
    outb(VGA_DATA_REGISTER, 0);
    
    // Clear the screen with a known good attribute
    volatile char* vidmem = (char*)VGA_MEMORY;
    for(int i = 0; i < VGA_WIDTH * VGA_HEIGHT * 2; i += 2) {
        vidmem[i] = ' ';           // Space character
        vidmem[i + 1] = 0x07;      // Light grey on black
    }
    
    // Reset cursor position variables
    cursor_x = 0;
    cursor_y = 0;
    
    print("VGA initialized.\n");
}

/**
 * clear_screen - Clears the entire VGA text buffer and resets cursor position
 * 
 * This function:
 * - Fills the entire VGA text buffer with space characters
 * - Sets each character's attribute to light grey on black (0x07)
 * - Resets both X and Y cursor coordinates to 0
 * 
 * Implementation details:
 * - VGA text buffer is accessed directly at VGA_MEMORY
 * - Each character cell takes 2 bytes:
 *   - First byte: ASCII character (space in this case)
 *   - Second byte: Attribute byte (0x07 = light grey on black)
 * - Buffer size is VGA_WIDTH * VGA_HEIGHT characters
 */
void clear_screen() {
    volatile char* vidmem = (char*)VGA_MEMORY;
    for(int i = 0; i < VGA_WIDTH * VGA_HEIGHT * 2; i += 2) {
        vidmem[i] = ' ';           // Space character
        vidmem[i + 1] = 0x07;      // Light grey on black
    }
    cursor_x = 0;
    cursor_y = 0;
}

/**
 * putchar - Outputs a single character to the VGA text buffer
 * 
 * Displays a character at the current cursor position and advances the cursor.
 * Handles special characters like newline, screen wrapping, and scrolling.
 * Updates both the software cursor position and hardware cursor.
 *
 * @param c: The character to display
 *
 * Implementation details:
 * - Each character takes 2 bytes in video memory:
 *   - First byte: ASCII character
 *   - Second byte: Attribute (color/style)
 * - Uses light grey on black (0x07) for character attributes
 * - Handles screen boundaries:
 *   - Wraps to next line when reaching end of line
 *   - Scrolls screen up when reaching bottom
 * - Updates hardware cursor position via VGA registers
 */
void putchar(char c) {
    volatile unsigned char* vidmem = (unsigned char*)VGA_MEMORY;
    
    if(c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else {
        int offset = (cursor_y * VGA_WIDTH + cursor_x) * 2;
        vidmem[offset] = c;
        vidmem[offset + 1] = 0x07;  // Light grey on black
        cursor_x++;
    }
    
    // Handle screen scrolling
    if(cursor_x >= VGA_WIDTH) {
        cursor_x = 0;
        cursor_y++;
    }
    
    if(cursor_y >= VGA_HEIGHT) {
        // Scroll the screen
        for(int i = 0; i < (VGA_HEIGHT-1) * VGA_WIDTH * 2; i++) {
            vidmem[i] = vidmem[i + VGA_WIDTH * 2];
        }
        
        // Clear the last line
        int last_line = (VGA_HEIGHT-1) * VGA_WIDTH * 2;
        for(int i = 0; i < VGA_WIDTH * 2; i += 2) {
            vidmem[last_line + i] = ' ';
            vidmem[last_line + i + 1] = 0x07;
        }
        cursor_y = VGA_HEIGHT - 1;
    }
    
    // Update hardware cursor
    int pos = cursor_y * VGA_WIDTH + cursor_x;
    outb(VGA_CTRL_REGISTER, VGA_OFFSET_HIGH);
    outb(VGA_DATA_REGISTER, (pos >> 8) & 0xFF);
    outb(VGA_CTRL_REGISTER, VGA_OFFSET_LOW);
    outb(VGA_DATA_REGISTER, pos & 0xFF);
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

#define PIT_FREQUENCY 1193180    // Base PIT frequency in Hz
#define CALIBRATION_MS 50        // Time to calibrate over (in milliseconds)

static uint64_t tsc_freq = 0;    // Stores CPU timestamp frequency
static uint32_t pit_ticks_per_ms = 0; // Stores PIT ticks per millisecond


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
    
    // Set initial count to maximum
    outb(0x40, 0xFF);
    outb(0x40, 0xFF);
    
    // Get starting TSC value
    uint64_t start_tsc = rdtsc();
    
    // Wait for CALIBRATION_MS milliseconds
    uint32_t ticks = (PIT_FREQUENCY * CALIBRATION_MS) / 1000;
    uint32_t count;
    do {
        // Latch counter value
        outb(0x43, 0x00);
        // Read count
        count = inb(0x40);
        count |= inb(0x40) << 8;
    } while (count > (0xFFFF - ticks));
    
    // Get ending TSC value
    uint64_t end_tsc = rdtsc();
    
    // Calculate CPU frequency (modified to avoid 64-bit division)
    uint64_t tsc_diff = end_tsc - start_tsc;
    
    // Calculate MHz directly to avoid 64-bit division
    // First get the high 32 bits for rough MHz calculation
    uint32_t freq_mhz = (uint32_t)(tsc_diff >> 32) * 1000 / CALIBRATION_MS;
    if (freq_mhz == 0) {
        // If high bits were 0, use low bits
        freq_mhz = ((uint32_t)tsc_diff * 1000) / (CALIBRATION_MS * 1000000);
    }
    
    tsc_freq = (uint64_t)freq_mhz * 1000000;  // Store full frequency
    
    // Calculate PIT ticks per millisecond
    pit_ticks_per_ms = PIT_FREQUENCY / 1000;
    
    // Reset PIT to normal operation
    timer_init(100);  // Reset to 100 Hz operation
    
    // Print calibration results
    print("Timer calibration complete:\n");
    print("CPU Frequency: ");
    print_int(freq_mhz); // Already in MHz
    print(" MHz\n");
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
    print("Testing output...\n");
    
    idt_init();
    print("IDT initialized.\n");
    
    pic_init();
    print("PIC initialized.\n");
    
    keyboard_init();
    print("Keyboard initialized.\n");
    
    pic_clear_mask(1);
    __asm__ volatile("sti");
    
    shell_run();
    
    while(1) {
        __asm__ volatile("hlt");
    }
}

