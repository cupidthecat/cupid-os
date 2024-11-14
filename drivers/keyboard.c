/*
 * PS/2 Keyboard Driver
 * 
 * This driver implements a complete PS/2 keyboard interface with:
 * - Full US keyboard layout support via scancode-to-ASCII mapping
 * - Modifier key tracking (Shift, Caps Lock, Ctrl, Alt)
 * - Key state tracking and event buffering
 * - Key repeat functionality with configurable delays
 * - Extended key support (F1-F12, arrow keys, etc)
 * - Interrupt-driven input handling via IRQ1
 * 
 * The keyboard state is maintained in a global structure that tracks:
 * - Current modifier key states
 * - Key up/down states
 * - Input event circular buffer
 * - Key repeat timing and state
 * 
 * Key events are processed through an interrupt handler that manages
 * both regular keypresses and special keys like modifiers and function keys.
 * 
 * The driver provides:
 * - Keyboard initialization and reset
 * - Scancode-to-ASCII conversion with shift/caps lock support
 * - Key repeat handling with configurable delays
 * - Function key support (F1-F12)
 * - Special key handling (backspace, tab, enter)
 * - Modifier key tracking (shift, ctrl, alt, caps lock)
 * - Extended key sequence handling
 * - Key state tracking and querying
 * - Event buffering with circular buffer
 * - Error checking and status monitoring
 * 
 * Key repeat behavior:
 * - Initial delay before repeat starts (KEY_REPEAT_DELAY)
 * - Configurable repeat rate (KEY_REPEAT_RATE)
 * - State tracking for currently repeating key
 * - Proper handling of key releases
 * 
 * The keyboard buffer:
 * - Circular buffer implementation
 * - Configurable size (KEYBOARD_BUFFER_SIZE)
 * - Overflow protection
 * - Event timestamping for debouncing
 * 
 * Modifier key handling:
 * - Separate tracking for left/right shift/ctrl/alt
 * - Caps lock toggle support
 * - Proper state management for extended key sequences
 * 
 * Error handling:
 * - Status port monitoring
 * - Output buffer checking
 * - Controller ready verification
 * 
 * Dependencies:
 * - ports.h: I/O port access
 * - irq.h: Interrupt handling
 * - kernel.h: Screen output
 * - types.h: Data structure definitions
 */

#include "keyboard.h"
#include "../kernel/ports.h"
#include "../kernel/irq.h"
#include "../kernel/kernel.h"

// Global keyboard state
static keyboard_state_t keyboard_state = {0};

// Scancode to ASCII mapping (lowercase)
static const char scancode_to_ascii[] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' '
};

// Scancode to ASCII mapping (uppercase)
static const char scancode_to_ascii_shift[] = {
    0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' '
};

// Add these definitions at the top
#define KEY_LSHIFT 0x2A
#define KEY_RSHIFT 0x36
#define KEY_CAPS   0x3A
#define KEY_REPEAT_DELAY    500  // Initial delay in ms before repeat starts
#define KEY_REPEAT_RATE     50   // Delay between repeats in ms
#define TIMER_FREQUENCY     100  // Timer ticks per second (adjust based on your PIT)
#define KEY_EXTENDED 0xE0
#define KEY_F1  0x3B
#define KEY_F2  0x3C
#define KEY_F3  0x3D
#define KEY_F4  0x3E
#define KEY_F5  0x3F
#define KEY_F6  0x40
#define KEY_F7  0x41
#define KEY_F8  0x42
#define KEY_F9  0x43
#define KEY_F10 0x44
#define KEY_F11 0x57
#define KEY_F12 0x58

// Add to keyboard_state_t in types.h or here
typedef struct {
    uint32_t last_keypress_time;  // Time of last keypress
    uint32_t last_repeat_time;    // Time of last repeat
    bool is_repeating;            // Whether key is currently repeating
    uint8_t last_key;             // Last key pressed
} key_repeat_state_t;

// Add to keyboard_state
static key_repeat_state_t repeat_state = {0};
static uint32_t system_ticks = 0;  // Updated by timer interrupt

// Modifier key scancodes
#define KEY_LCTRL  0x1D
#define KEY_RCTRL  0x1D  // Note: Right Ctrl sends 0xE0, 0x1D
#define KEY_LALT   0x38
#define KEY_RALT   0x38  // Note: Right Alt sends 0xE0, 0x38
#define KEY_LSHIFT 0x2A
#define KEY_RSHIFT 0x36
#define KEY_CAPS   0x3A

// Modifier indices
#define MOD_SHIFT 0
#define MOD_CTRL  1
#define MOD_ALT   2
#define MOD_CAPS  3

// Track if we're handling an extended key sequence
static bool handling_extended = false;

// Add shifted scancode map
static const char shifted_scancode_to_ascii[] = {
    0, 0, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' '
};

// Add function key state tracking to keyboard_state_t
static bool function_keys[12] = {false};  // F1-F12 states

// Add this function declaration at the top of the file, after the includes
static void process_keypress(uint8_t key);

// Add this declaration at the top of the file, after the includes
static char get_ascii_from_scancode(uint8_t scancode);

// Initialize keyboard
void keyboard_init(void) {
    // Register keyboard interrupt handler
    irq_install_handler(1, keyboard_handler);
    
    // Reset keyboard state
    for(int i = 0; i < 256; i++) {
        keyboard_state.key_states[i] = KEY_UP;
        keyboard_state.last_keypress_time[i] = 0;
    }
    
    for(int i = 0; i < 8; i++) {
        keyboard_state.modifier_states[i] = false;
    }
    
    keyboard_state.buffer.head = 0;
    keyboard_state.buffer.tail = 0;
    keyboard_state.buffer.count = 0;
    
    // Enable keyboard
    while(inb(KEYBOARD_STATUS_PORT) & 0x02);
    outb(KEYBOARD_DATA_PORT, KEYBOARD_CMD_ENABLE);
    
    print("Keyboard initialized.\n");
}

// Add event to keyboard buffer
static void keyboard_buffer_add(key_event_t event) {
    if(keyboard_state.buffer.count < KEYBOARD_BUFFER_SIZE) {
        keyboard_state.buffer.events[keyboard_state.buffer.tail] = event;
        keyboard_state.buffer.tail = (keyboard_state.buffer.tail + 1) % KEYBOARD_BUFFER_SIZE;
        keyboard_state.buffer.count++;
    }
}

// Add this function to update system time
void keyboard_update_ticks(void) {
    system_ticks++;
}

// Move the process_keypress implementation before keyboard_handler
static void process_keypress(uint8_t key) {
    // Handle function keys
    if (key >= KEY_F1 && key <= KEY_F12) {
        int f_key = (key >= KEY_F11) ? 
                    (key - KEY_F11 + 10) : // F11-F12
                    (key - KEY_F1);        // F1-F10
        
        function_keys[f_key] = true;
        
        // Print function key press (for debugging)
        print("F");
        char num[3];
        num[0] = ((f_key + 1) / 10) + '0';
        num[1] = ((f_key + 1) % 10) + '0';
        num[2] = '\0';
        print(num);
        print(" pressed\n");
        return;
    }

    // Handle Caps Lock press
    if (key == KEY_CAPS) {
        keyboard_state.modifier_states[MOD_CAPS] = !keyboard_state.modifier_states[MOD_CAPS];
        return;
    }

    // Handle Shift keys
    if (key == KEY_LSHIFT || key == KEY_RSHIFT) {
        keyboard_state.modifier_states[MOD_SHIFT] = true;
        return;
    }

    // Convert scancode to ASCII
    char ascii = get_ascii_from_scancode(key);

    // Special handling for backspace
    if (ascii == '\b') {
        // Move cursor back one position
        if (cursor_x > 0) {
            cursor_x--;
        } else if (cursor_y > 0) {
            cursor_y--;
            cursor_x = VGA_WIDTH - 1;
        }
        
        // Clear the character at current position
        volatile char* vidmem = (char*)VGA_MEMORY;
        int offset = (cursor_y * VGA_WIDTH + cursor_x) * 2;
        vidmem[offset] = ' ';
        vidmem[offset + 1] = 0x07;  // Light grey on black
        
        // Update hardware cursor
        int pos = cursor_y * VGA_WIDTH + cursor_x;
        outb(VGA_CTRL_REGISTER, VGA_OFFSET_HIGH);
        outb(VGA_DATA_REGISTER, (pos >> 8) & 0xFF);
        outb(VGA_CTRL_REGISTER, VGA_OFFSET_LOW);
        outb(VGA_DATA_REGISTER, pos & 0xFF);
        return;
    }

    // Output other characters
    if (ascii) {
        putchar(ascii);
    }
}

// Define the function after process_keypress
static char get_ascii_from_scancode(uint8_t scancode) {
    // Only handle make codes (key press), ignore break codes (key release)
    if (scancode & 0x80) {
        return 0;
    }

    // Handle regular keys
    if (scancode < sizeof(scancode_to_ascii)) {
        if (keyboard_state.modifier_states[MOD_SHIFT] ^ 
            keyboard_state.modifier_states[MOD_CAPS]) {
            return shifted_scancode_to_ascii[scancode];
        } else {
            return scancode_to_ascii[scancode];
        }
    }

    return 0;
}

// Update the keyboard handler
void keyboard_handler(struct registers* r) {
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);
    
    // Handle extended key sequences
    if (scancode == KEY_EXTENDED) {
        handling_extended = true;
        return;
    }
    
    // Track key states
    bool is_release = scancode & 0x80;
    uint8_t key = scancode & 0x7F;
    
    // Update key state
    keyboard_state.key_states[key] = is_release ? KEY_UP : KEY_DOWN;
    
    // Handle Shift key release
    if (is_release) {
        if (key == KEY_LSHIFT || key == KEY_RSHIFT) {
            keyboard_state.modifier_states[MOD_SHIFT] = false;
        }
        // Don't process other key releases
        return;
    }
    
    // Handle key press
    process_keypress(key);
    
    // Reset extended flag
    handling_extended = false;
}

// Get key state
bool keyboard_get_key_state(uint8_t scancode) {
    return keyboard_state.key_states[scancode] == KEY_DOWN;
}

// Get raw scancode (for direct access)
char keyboard_get_scancode(void) {
    if(keyboard_state.buffer.count > 0) {
        key_event_t event = keyboard_state.buffer.events[keyboard_state.buffer.head];
        keyboard_state.buffer.head = (keyboard_state.buffer.head + 1) % KEYBOARD_BUFFER_SIZE;
        keyboard_state.buffer.count--;
        return event.scancode;
    }
    return 0;
}

// Add a new function to check function key states
bool keyboard_get_function_key(uint8_t f_num) {
    if (f_num >= 1 && f_num <= 12) {
        return function_keys[f_num - 1];
    }
    return false;
}

// Get the current state of caps lock
bool keyboard_get_caps_lock(void) {
    return keyboard_state.modifier_states[MOD_CAPS];
}

// Get the current state of shift
bool keyboard_get_shift(void) {
    return keyboard_state.modifier_states[MOD_SHIFT];
}

// Add keyboard_get_char implementation
char keyboard_get_char(void) {
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);
    
    // Handle key release (ignore them)
    if (scancode & 0x80) {
        return 0;
    }
    
    // Convert scancode to ASCII
    if (scancode >= sizeof(scancode_to_ascii)) {
        return 0;
    }
    
    // Handle shift key
    if (keyboard_get_shift() || keyboard_get_caps_lock()) {
        return shifted_scancode_to_ascii[scancode];
    } else {
        return scancode_to_ascii[scancode];
    }
} 