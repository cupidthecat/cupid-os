#include "mouse.h"
#include "vga.h"

#include "../kernel/ports.h"
#include "../kernel/irq.h"
#include "../kernel/kernel.h"
#include "../kernel/pic.h"

static volatile bool mouse_ready = false; // Flag indicating if mouse data is ready
static volatile uint8_t mouse_cycle = 0; // Current byte in mouse packet (0-2)
static volatile int8_t mouse_byte[3]; // Stores the 3 bytes of mouse data
volatile mouse_packet_t current_packet; // Current mouse packet data
volatile int16_t mouse_x = 160;  // Current mouse X position (center of 320x200 screen)
volatile int16_t mouse_y = 100;  // Current mouse Y position (center of 320x200 screen)
static uint8_t current_bg_pixels[4] = {0}; // Stores background pixels under cursor

/**
 * Initializes the PS/2 mouse
 */
void mouse_init(void) {
    // Disable both devices
    mouse_wait(1);
    outb(MOUSE_COMMAND_PORT, 0xA7);
    mouse_wait(1);
    outb(MOUSE_COMMAND_PORT, 0xA8);

    // Enable interrupts
    mouse_wait(1);
    outb(MOUSE_COMMAND_PORT, 0x20);
    mouse_wait(0);
    uint8_t status = inb(MOUSE_DATA_PORT) | 0x02;
    mouse_wait(1);
    outb(MOUSE_COMMAND_PORT, 0x60);
    mouse_wait(1);
    outb(MOUSE_DATA_PORT, status);

    // Enable auxiliary device
    mouse_wait(1);
    outb(MOUSE_COMMAND_PORT, 0xA8);

    // Set default parameters
    mouse_write(0xF6);
    mouse_read();
    
    // Enable data reporting
    mouse_write(0xF4);
    mouse_read();

    // Install handler and enable IRQ
    irq_install_handler(MOUSE_IRQ, mouse_handler);
    pic_clear_mask(MOUSE_IRQ);
    
    print("[:3] PS/2 Mouse initialized.\n");
}

/**
 * Handles mouse interrupts
 * @param r CPU registers structure
 */
void mouse_handler(struct registers* r) {
    uint8_t status = inb(MOUSE_STATUS_PORT);
    
    if(!(status & 0x20)) return; // Not mouse data
    
    uint8_t data = inb(MOUSE_DATA_PORT);
    
    switch(mouse_cycle) {
        case 0:
            if((data & 0x08) == 0) break;
            mouse_byte[0] = data;
            mouse_cycle++;
            break;
        case 1:
            mouse_byte[1] = data;
            mouse_cycle++;
            break;
        case 2:
            mouse_byte[2] = data;
            
            // Parse packet
            current_packet.left_button = mouse_byte[0] & 0x01;
            current_packet.right_button = mouse_byte[0] & 0x02;
            current_packet.middle_button = mouse_byte[0] & 0x04;
            
            current_packet.x_movement = (int8_t)mouse_byte[1];
            current_packet.y_movement = (int8_t)mouse_byte[2];
            
            // Print debug info
            // Uncoment to check mouse
            /*
            print("Mouse: ");
            if(current_packet.left_button) print("L ");
            if(current_packet.right_button) print("R ");
            if(current_packet.middle_button) print("M ");
            print("X: ");
            print_int((int8_t)current_packet.x_movement);
            print(" Y: ");
            print_int((int8_t)current_packet.y_movement);
            print("\n");
            */
            
            // Handle overflow (we just ignore overflowed packets)
            if(!(mouse_byte[0] & 0xC0)) {
                mouse_ready = true;
            }
            
            // Save previous position
            int16_t prev_x = mouse_x;
            int16_t prev_y = mouse_y;
            
            // Update coordinates
            mouse_x += current_packet.x_movement;
            mouse_y -= current_packet.y_movement;
            
            // Clamp values
            if(mouse_x < 0) mouse_x = 0;
            if(mouse_x >= 320) mouse_x = 319;
            if(mouse_y < 0) mouse_y = 0;
            if(mouse_y >= 200) mouse_y = 199;
            
            // Only redraw if position changed
            if (mouse_x != prev_x || mouse_y != prev_y) {
                // Restore previous background using stored pixels
                putpixel(prev_x, prev_y, current_bg_pixels[0]);
                putpixel(prev_x+1, prev_y, current_bg_pixels[1]);
                putpixel(prev_x, prev_y+1, current_bg_pixels[2]);
                putpixel(prev_x+1, prev_y+1, current_bg_pixels[3]);
                
                // Save new background before drawing cursor
                current_bg_pixels[0] = getpixel(mouse_x, mouse_y);
                current_bg_pixels[1] = getpixel(mouse_x+1, mouse_y);
                current_bg_pixels[2] = getpixel(mouse_x, mouse_y+1);
                current_bg_pixels[3] = getpixel(mouse_x+1, mouse_y+1);
                
                // Draw new cursor
                putpixel(mouse_x, mouse_y, 0x04);
                putpixel(mouse_x+1, mouse_y, 0x04);
                putpixel(mouse_x, mouse_y+1, 0x04);
                putpixel(mouse_x+1, mouse_y+1, 0x04);
            }
            
            mouse_cycle = 0;
            
            break;
    }
}

/**
 * Gets the current mouse packet
 * @param packet Pointer to store the mouse packet
 * @return true if packet was available, false otherwise
 */
bool mouse_get_packet(mouse_packet_t* packet) {
    if(!mouse_ready) return false;
    
    __asm__ volatile("cli");
    *packet = current_packet;
    mouse_ready = false;
    __asm__ volatile("sti");
    
    return true;
}

/**
 * Waits for mouse status
 * @param type 0 to wait for output buffer, 1 to wait for input buffer
 */
void mouse_wait(uint8_t type) {
    uint32_t timeout = 100000;
    while(timeout--) {
        if((inb(MOUSE_STATUS_PORT) & (type + 1)) == type)
            return;
    }
}

/**
 * Writes data to the mouse
 * @param data The data byte to write
 */
void mouse_write(uint8_t data) {
    mouse_wait(1);
    outb(MOUSE_COMMAND_PORT, 0xD4);
    mouse_wait(1);
    outb(MOUSE_DATA_PORT, data);
}

/**
 * Reads data from the mouse
 * @return The data byte read
 */
uint8_t mouse_read(void) {
    mouse_wait(0);
    return inb(MOUSE_DATA_PORT);
}