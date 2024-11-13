 /**
 * kernel.c
 * 
 * Main kernel file for cupid-os. Contains core kernel functionality including:
 * - VGA text mode driver
 * - Screen output functions
 * - Basic screen manipulation (clear, scroll, cursor movement)
 * - Character and string printing capabilities
 */

#include "idt.h"

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
static int cursor_x = 0;
static int cursor_y = 0;

// Clear the screen
void clear_screen() {
    volatile char* vidmem = (char*)VGA_MEMORY;
    for(int i = 0; i < VGA_WIDTH * VGA_HEIGHT * 2; i += 2) {
        vidmem[i] = ' ';           // Space character
        vidmem[i + 1] = 0x07;      // Light grey on black
    }
    cursor_x = 0;
    cursor_y = 0;
}

// Print a single character
void putchar(char c) {
    volatile char* vidmem = (char*)VGA_MEMORY;
    
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
}

// Print a string
void print(const char* str) {
    for(int i = 0; str[i] != '\0'; i++) {
        putchar(str[i]);
    }
}

// Test function for divide by zero
void test_divide_by_zero() {
    int a = 10;
    int b = 0;
    int c = a / b;  // This will trigger interrupt 0
}

void _start(void) {
    // We're already in protected mode with segments set up
    __asm__ volatile(
        "mov $0x90000, %esp\n"
        "mov %esp, %ebp\n"
    );
    
    // Call main kernel function
    kmain();
}

void kmain(void) {
    clear_screen();
    
    // Initialize IDT before printing
    idt_init();
    
    print("Welcome to cupid-os!\n");
    print("------------------\n");
    print("Kernel initialized successfully.\n");
    print("IDT initialized.\n");
    print("Testing IDT with divide by zero...\n");
    
    // Enable interrupts
    __asm__ volatile("sti");
    
    // Test divide by zero
    test_divide_by_zero();
    
    print("If you see this, IDT is NOT working!\n");
    
    while(1) {
        __asm__ volatile("hlt");
    }
}