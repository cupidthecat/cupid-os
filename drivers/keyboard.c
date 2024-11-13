#include "keyboard.h"
#include "../kernel/ports.h"
#include "../kernel/kernel.h"

// US keyboard layout scancode map
static const char scancode_to_char[] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' '
};

// Keyboard state
static keyboard_state_t keyboard_state = {0};

// Add at the top of keyboard.c
#define KEYBOARD_TIMEOUT 100000

// Helper function to wait for keyboard
static bool wait_for_keyboard(uint8_t type) {
    int timeout = KEYBOARD_TIMEOUT;
    if (type == 0) {
        while (timeout-- && (inb(KEYBOARD_STATUS_PORT) & 2)) {
            if (timeout <= 0) return false;
        }
    } else {
        while (timeout-- && !(inb(KEYBOARD_STATUS_PORT) & 1)) {
            if (timeout <= 0) return false;
        }
    }
    return true;
}

// Initialize the keyboard
void keyboard_init(void) {
    // Register keyboard interrupt handler
    irq_install_handler(1, keyboard_handler);
    
    // Reset keyboard buffer
    keyboard_state.buffer.head = 0;
    keyboard_state.buffer.tail = 0;
    keyboard_state.buffer.count = 0;
    
    // Enable keyboard
    if (!wait_for_keyboard(0)) {
        print("Keyboard timeout during initialization\n");
        return;
    }
    
    outb(KEYBOARD_STATUS_PORT, 0xAE);       // Enable keyboard interface
    
    if (!wait_for_keyboard(0)) {
        print("Keyboard timeout during enable\n");
        return;
    }
    
    outb(KEYBOARD_DATA_PORT, KEYBOARD_CMD_ENABLE);
    
    print("Keyboard initialized.\n");
}

// Add event to keyboard buffer
static void keyboard_buffer_push(key_event_t event) {
    if (keyboard_state.buffer.count < KEYBOARD_BUFFER_SIZE) {
        keyboard_state.buffer.events[keyboard_state.buffer.tail] = event;
        keyboard_state.buffer.tail = (keyboard_state.buffer.tail + 1) % KEYBOARD_BUFFER_SIZE;
        keyboard_state.buffer.count++;
    }
}

// Handle keyboard interrupt
void keyboard_handler(struct registers* r) {
    print("Keyboard interrupt received!\n");  // Debug output
    
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);
    print("Scancode: ");
    
    // Convert scancode to hex string
    char hex[3];
    hex[0] = "0123456789ABCDEF"[(scancode >> 4) & 0xF];
    hex[1] = "0123456789ABCDEF"[scancode & 0xF];
    hex[2] = '\0';
    print(hex);
    print("\n");
    
    bool is_release = scancode & 0x80;
    uint8_t key = scancode & 0x7F;
    
    // Only handle key press events
    if (!is_release && key < sizeof(scancode_to_char) && scancode_to_char[key]) {
        putchar(scancode_to_char[key]);
    }
}

// Get current state of a key
bool keyboard_get_key_state(uint8_t scancode) {
    return keyboard_state.key_states[scancode] == KEY_DOWN;
}

// Get raw scancode (blocking)
char keyboard_get_scancode(void) {
    while (!(inb(KEYBOARD_STATUS_PORT) & 1));  // Wait for data
    return inb(KEYBOARD_DATA_PORT);
} 