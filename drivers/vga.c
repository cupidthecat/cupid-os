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
    volatile unsigned char* vidmem = (unsigned char*)VGA_MEMORY;
    
    if(c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if(c == '\b') {  // Handle backspace
        if(cursor_x > 0) {
            cursor_x--;
        } else if(cursor_y > 0) {
            cursor_y--;
            cursor_x = VGA_WIDTH - 1;
        }
        // Clear the character at current position
        int offset = (cursor_y * VGA_WIDTH + cursor_x) * 2;
        vidmem[offset] = ' ';
        vidmem[offset + 1] = vga_make_color(vga_fg_color, vga_bg_color);
    } else {
        int offset = (cursor_y * VGA_WIDTH + cursor_x) * 2;
        vidmem[offset] = c;
        vidmem[offset + 1] = vga_make_color(vga_fg_color, vga_bg_color);
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
            vidmem[last_line + i + 1] = vga_make_color(vga_fg_color, vga_bg_color);
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