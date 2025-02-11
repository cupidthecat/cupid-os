#include "vga.h"
#include "../kernel/ports.h"
#include "../kernel/kernel.h"  // Add this to get access to print function

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

// Color state
uint8_t vga_fg_color = VGA_LIGHT_GREY;
uint8_t vga_bg_color = VGA_BLACK;

uint8_t* font_data = 0;

/**
 * vga_make_color - Combines foreground and background colors into a VGA attribute byte
 * @fg: Foreground color (4 bits)
 * @bg: Background color (4 bits)
 * 
 * Returns: Combined color attribute byte where:
 *   - Bits 7-4: Background color
 *   - Bits 3-0: Foreground color
 */
uint8_t vga_make_color(uint8_t fg, uint8_t bg) {
    return (bg << 4) | fg;
}

/**
 * vga_set_color - Sets the current foreground and background colors
 * @fg: New foreground color
 * @bg: New background color
 * 
 * This updates the global color state used for subsequent text output
 */
void vga_set_color(uint8_t fg, uint8_t bg) {
    vga_fg_color = fg;
    vga_bg_color = bg;
}

/**
 * get_vga_entry - Creates a VGA character entry with current colors
 * @c: ASCII character to display
 * 
 * Returns: 16-bit VGA entry where:
 *   - Bits 15-8: Color attribute byte
 *   - Bits 7-0: ASCII character
 */
static uint16_t get_vga_entry(unsigned char c) {
    return (vga_make_color(vga_fg_color, vga_bg_color) << 8) | c;
}

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
    static int cursor_x = 0, cursor_y = 0;
    
    if(c == '\n') {
        cursor_x = 0;
        cursor_y += FONT_HEIGHT;
        return;
    }
    
    uint8_t* glyph = font_data + c * FONT_HEIGHT;
    
    for(int y = 0; y < FONT_HEIGHT; y++) {
        uint8_t row = glyph[y];
        for(int x = 0; x < FONT_WIDTH; x++) {
            if(row & (1 << (7 - x))) {  // Correct bit order
                putpixel(cursor_x + x, cursor_y + y, vga_fg_color);
            }
        }
    }
    
    cursor_x += FONT_WIDTH;
    if(cursor_x >= 320 - FONT_WIDTH) {
        cursor_x = 0;
        cursor_y += FONT_HEIGHT;
    }
}

void putpixel(int x, int y, uint8_t color) {
    volatile uint8_t* vidmem = (uint8_t*)0xA0000; // Mode 0x13 memory
    vidmem[y * 320 + x] = color;
}

void draw_rect(int16_t x, int16_t y, uint16_t w, uint16_t h, uint8_t color) {
    for(int dy = 0; dy < h; dy++) {
        for(int dx = 0; dx < w; dx++) {
            putpixel(x + dx, y + dy, color);
        }
    }
}

uint8_t getpixel(int x, int y) {
    volatile uint8_t* vidmem = (uint8_t*)0xA0000;
    return vidmem[y * 320 + x];
}

void load_font(uint8_t* font) {
    // Verify PSF magic header
    if(*((uint16_t*)font) != 0x0436) {  // PSF1 magic number
        print("Invalid PSF font format!\n");
        return;
    }
    font_data = font + 4; // Skip header
    print("ZAP-Light16 font loaded\n");
}