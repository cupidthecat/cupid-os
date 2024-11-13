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
 * - Main kernel loop with interrupt handling
 */

#include "idt.h"
#include "pic.h"
#include "../drivers/keyboard.h"

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

// Add these port I/O functions at the top
static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Add these VGA-related definitions at the top
#define VGA_CTRL_REGISTER 0x3D4
#define VGA_DATA_REGISTER 0x3D5
#define VGA_OFFSET_LOW 0x0F
#define VGA_OFFSET_HIGH 0x0E

// Add these function declarations at the top, after the #defines
void print(const char* str);
void putchar(char c);
void clear_screen(void);
void init_vga(void);

// Add this function to initialize the cursor
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

// Print a string
void print(const char* str) {
    for(int i = 0; str[i] != '\0'; i++) {
        putchar(str[i]);
    }
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
    // Initialize VGA first
    init_vga();
    clear_screen();
    print("Testing output...\n");
    
    // Initialize interrupts and PIC
    pic_init();
    print("PIC initialized.\n");
    
    idt_init();
    print("IDT initialized.\n");
    
    // Initialize keyboard
    keyboard_init();
    
    print("\nWelcome to cupid-os!\n");
    print("------------------\n");
    print("Start typing...\n");
    
    // Enable interrupts
    __asm__ volatile("sti");
    
    // Main kernel loop
    while(1) {
        __asm__ volatile("hlt");
    }
}
