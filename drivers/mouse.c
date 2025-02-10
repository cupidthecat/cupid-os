#include "mouse.h"
#include "../kernel/ports.h"
#include "../kernel/irq.h"
#include "../kernel/kernel.h"
#include "../kernel/pic.h"

static volatile bool mouse_ready = false;
static volatile uint8_t mouse_cycle = 0;
static volatile int8_t mouse_byte[3];
static volatile mouse_packet_t current_packet;

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
            
            current_packet.x_movement = mouse_byte[1];
            current_packet.y_movement = mouse_byte[2];
            
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
            
            mouse_cycle = 0;
            break;
    }
}

bool mouse_get_packet(mouse_packet_t* packet) {
    if(!mouse_ready) return false;
    
    __asm__ volatile("cli");
    *packet = current_packet;
    mouse_ready = false;
    __asm__ volatile("sti");
    
    return true;
}

void mouse_wait(uint8_t type) {
    uint32_t timeout = 100000;
    while(timeout--) {
        if((inb(MOUSE_STATUS_PORT) & (type + 1)) == type)
            return;
    }
}

void mouse_write(uint8_t data) {
    mouse_wait(1);
    outb(MOUSE_COMMAND_PORT, 0xD4);
    mouse_wait(1);
    outb(MOUSE_DATA_PORT, data);
}

uint8_t mouse_read(void) {
    mouse_wait(0);
    return inb(MOUSE_DATA_PORT);
}