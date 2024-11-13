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
 */

#include "keyboard.h"
#include "../kernel/ports.h"
#include "../kernel/irq.h"
#include "../kernel/kernel.h"

// Global keyboard state
static keyboard_state_t keyboard_state = {0};

// US keyboard layout scancode map
static const char scancode_to_ascii[] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
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

// Initialize keyboard
void keyboard_init(void) {
    // Wait for keyboard controller to be ready
    while(inb(KEYBOARD_STATUS_PORT) & 0x02);
    
    // Flush the output buffer
    while(inb(KEYBOARD_STATUS_PORT) & 0x01) {
        inb(KEYBOARD_DATA_PORT);
    }
    
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
    
    // Install keyboard handler
    irq_install_handler(1, keyboard_handler);
    
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

    // Handle modifier keys
    if (key == KEY_LSHIFT || key == KEY_RSHIFT) {
        keyboard_state.modifier_states[MOD_SHIFT] = true;
        return;
    }
    // ... other modifier key handling ...

    // Convert scancode to ASCII
    char ascii;
    if (keyboard_state.modifier_states[MOD_SHIFT] ^ keyboard_state.modifier_states[MOD_CAPS]) {
        ascii = shifted_scancode_to_ascii[key];
    } else {
        ascii = scancode_to_ascii[key];
    }

    // Output the character
    if (ascii) {
        putchar(ascii);
    }
}

// Then the keyboard_handler function follows
void keyboard_handler(struct registers* r) {
    // Add error checking for keyboard status
    uint8_t status = inb(KEYBOARD_STATUS_PORT);
    if (!(status & 0x01)) {
        return;  // No data available
    }

    uint8_t scancode = inb(KEYBOARD_DATA_PORT);
    
    // Handle extended key sequences
    if (scancode == KEY_EXTENDED) {
        handling_extended = true;
        return;
    }
    
    bool is_release = scancode & 0x80;
    uint8_t key = scancode & 0x7F;
    
    // Handle function key releases
    if (is_release) {
        if (key >= KEY_F1 && key <= KEY_F12) {
            int f_key = (key >= KEY_F11) ? 
                        (key - KEY_F11 + 10) : // F11-F12
                        (key - KEY_F1);        // F1-F10
            function_keys[f_key] = false;
            return;
        }
    }

    // Reset repeat state on key release
    if (is_release) {
        if (key == repeat_state.last_key) {
            repeat_state.is_repeating = false;
        }
        // Handle modifier keys
        if (key == KEY_LCTRL || (handling_extended && key == KEY_RCTRL)) {
            keyboard_state.modifier_states[MOD_CTRL] = false;
        }
        else if (key == KEY_LALT || (handling_extended && key == KEY_RALT)) {
            keyboard_state.modifier_states[MOD_ALT] = false;
        }
        else if (key == KEY_LSHIFT || key == KEY_RSHIFT) {
            keyboard_state.modifier_states[MOD_SHIFT] = false;
        }
        else if (key == KEY_CAPS && !is_release) {
            keyboard_state.modifier_states[MOD_CAPS] = !keyboard_state.modifier_states[MOD_CAPS];
        }
        // Handle regular keys
        else if (!is_release && !handling_extended) {
            bool shift_active = keyboard_state.modifier_states[MOD_SHIFT];
            bool caps_active = keyboard_state.modifier_states[MOD_CAPS];
            bool ctrl_active = keyboard_state.modifier_states[MOD_CTRL];
            bool alt_active = keyboard_state.modifier_states[MOD_ALT];
            
            char ascii;
            if (shift_active ^ caps_active) {
                ascii = shifted_scancode_to_ascii[key];
            } else {
                ascii = scancode_to_ascii[key];
            }
            
            // Handle Ctrl+C, Ctrl+Z, etc.
            if (ctrl_active && ascii >= 'a' && ascii <= 'z') {
                ascii = ascii - 'a' + 1;  // Convert to control codes (1-26)
                // Special handling for common control characters
                switch(ascii) {
                    case 3:  // Ctrl+C
                        print("^C");
                        break;
                    case 26: // Ctrl+Z
                        print("^Z");
                        break;
                    default:
                        // Other control characters
                        if (ascii < 32) {
                            print("^");
                            putchar(ascii + 'A' - 1);
                        }
                }
            }
            // Regular character output
            else if (ascii && !ctrl_active && !alt_active) {
                putchar(ascii);
            }
        }
        
        // Reset extended key flag
        if (!handling_extended) {
            keyboard_state.key_states[key] = is_release ? KEY_UP : KEY_DOWN;
        }
        handling_extended = false;
        return;
    }

    // Handle new keypress
    if (!is_release) {
        repeat_state.last_keypress_time = system_ticks;
        repeat_state.last_key = key;
        repeat_state.is_repeating = false;

        // Process the initial keypress
        process_keypress(key);
    }

    // Check for key repeat
    uint32_t current_time = system_ticks;
    if (repeat_state.last_key != 0) {
        uint32_t time_since_press = current_time - repeat_state.last_keypress_time;
        
        if (!repeat_state.is_repeating) {
            // Start repeating after initial delay
            if (time_since_press > KEY_REPEAT_DELAY / (1000 / TIMER_FREQUENCY)) {
                repeat_state.is_repeating = true;
                repeat_state.last_repeat_time = current_time;
                process_keypress(repeat_state.last_key);
            }
        } else {
            // Continue repeating at regular intervals
            uint32_t time_since_repeat = current_time - repeat_state.last_repeat_time;
            if (time_since_repeat > KEY_REPEAT_RATE / (1000 / TIMER_FREQUENCY)) {
                repeat_state.last_repeat_time = current_time;
                process_keypress(repeat_state.last_key);
            }
        }
    }
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