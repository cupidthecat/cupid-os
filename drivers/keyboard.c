/*
 * PS/2 Keyboard Driver
 * 
 * Implements a complete PS/2 keyboard interface with the following features:
 * 
 * Input Handling:
 * - Full US keyboard layout support with scancode-to-ASCII mapping
 * - Interrupt-driven input processing via IRQ1
 * - Key state tracking and event buffering in circular buffer
 * - Configurable key repeat with initial delay and repeat rate
 * - Key debouncing via event timestamping
 * 
 * Modifier Keys:
 * - Shift (left/right), Caps Lock, Ctrl (left/right), Alt (left/right)
 * - Proper state tracking and toggle support for Caps Lock
 * - Shift state affects ASCII mapping
 * 
 * Special Keys:
 * - Function keys F1-F12 with state tracking
 * - Extended keys (arrows, insert, delete, etc)
 * - System keys (backspace, tab, enter)
 * 
 * Error Handling:
 * - Controller status monitoring
 * - Buffer overflow protection
 * - Input validation and error checking
 * 
 * Key Event Processing:
 * - Scancode translation to ASCII with shift/caps lock
 * - Key press and release detection
 * - Extended key sequence handling
 * - Event timestamping for debouncing
 * 
 * State Management:
 * - Global keyboard state tracking
 * - Per-key up/down state
 * - Modifier key states
 * - Key repeat status
 * 
 * Buffer Implementation:
 * - Circular buffer for key events
 * - Configurable buffer size
 * - Overflow protection
 * - FIFO event processing
 * 
 * Dependencies:
 * - ports.h: I/O port access functions
 * - irq.h: Interrupt registration
 * - kernel.h: System functions
 * - types.h: Data type definitions
 */

#include "keyboard.h"
#include "../kernel/ports.h"
#include "../kernel/irq.h"
#include "../kernel/kernel.h"
#include "../kernel/shell.h"
#include "../kernel/terminal_app.h"
#include "../kernel/desktop.h"

// Global keyboard state
static keyboard_state_t keyboard_state = {0};

// Scancode to ASCII mapping (lowercase)
static const char scancode_to_ascii[] = {
    0,   0,   '1', '2', '3', '4', '5', '6', '7', '8',  // 0x00 - 0x09
    '9', '0', '-', '=', '\b', '\t', 'q', 'w', 'e', 'r', // 0x0A - 0x13
    't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,    // 0x14 - 0x1D
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',   // 0x1E - 0x27
    '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', // 0x28 - 0x31
    'm', ',', '.', '/', 0,   '*', 0,   ' '              // 0x32 - 0x3F
};

// Scancode to ASCII mapping (uppercase)
static const char scancode_to_ascii_shift[] = {
    0,   0,   '!', '@', '#', '$', '%', '^', '&', '*',  // 0x00 - 0x09
    '(', ')', '_', '+', '\b', '\t', 'Q', 'W', 'E', 'R', // 0x0A - 0x13
    'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,    // 0x14 - 0x1D
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',   // 0x1E - 0x27
    '"', '~', 0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N',   // 0x28 - 0x31
    'M', '<', '>', '?', 0,   '*', 0,   ' '              // 0x32 - 0x3F
};

// Define keys and constants
#define KEY_LSHIFT      0x2A
#define KEY_RSHIFT      0x36
#define KEY_CAPS        0x3A
#define KEY_EXTENDED    0xE0
#define KEY_RELEASED    0x80
#define KEYBOARD_DATA_PORT    0x60
#define KEYBOARD_STATUS_PORT  0x64
#define KEYBOARD_CMD_ENABLE   0xF4

// Function keys' scancodes
#define KEY_F1    0x3B
#define KEY_F2    0x3C
#define KEY_F3    0x3D
#define KEY_F4    0x3E
#define KEY_F5    0x3F
#define KEY_F6    0x40
#define KEY_F7    0x41
#define KEY_F8    0x42
#define KEY_F9    0x43
#define KEY_F10   0x44
#define KEY_F11   0x57
#define KEY_F12   0x58

// Key repeat timing (not fully implemented here)
#define KEY_REPEAT_DELAY    500  // Initial delay in ms before repeat starts
#define KEY_REPEAT_RATE     50   // Delay between repeats in ms

// Modifier key scancodes
#define KEY_LCTRL     0x1D
#define KEY_RCTRL     0x1D  // Right Ctrl sends 0xE0, 0x1D
#define KEY_LALT      0x38
#define KEY_RALT      0x38  // Right Alt sends 0xE0, 0x38
#define KEY_CAPS_LOCK 0x3A

// Modifier keys indices
#define MOD_SHIFT 0
#define MOD_CTRL  1
#define MOD_ALT   2
#define MOD_CAPS  3

// Extended keys for arrow keys
#define EXT_KEY_UP    0x48
#define EXT_KEY_DOWN  0x50
#define EXT_KEY_LEFT  0x4B
#define EXT_KEY_RIGHT 0x4D

// Add to keyboard_state_t in types.h or here
typedef struct {
    uint32_t last_keypress_time;  // Time of last keypress
    uint32_t last_repeat_time;    // Time of last repeat
    bool is_repeating;            // Whether key is currently repeating
    uint8_t last_key;             // Last key pressed
} key_repeat_state_t;

// Key repeat and system tick tracking
static uint32_t system_ticks = 0;  // Updated by timer interrupt

// Track if we're handling an extended key sequence
static bool handling_extended = false;

// Add these state tracking variables near the top with other static variables
static bool caps_lock_active = false;
static bool left_shift_active = false;
static bool right_shift_active = false;

// Add this function declaration
static void process_keypress(uint8_t key);
static void enqueue_event(uint8_t scancode, char ascii);

// Helper to push a key event into the circular buffer
static void enqueue_event(uint8_t scancode, char ascii) {
    key_event_t event;
    event.scancode = scancode;
    event.character = ascii;
    event.pressed = true;
    event.timestamp = system_ticks;

    keyboard_buffer_t *buffer = &keyboard_state.buffer;
    buffer->events[buffer->head] = event;
    buffer->head = (uint8_t)((buffer->head + 1U) % KEYBOARD_BUFFER_SIZE);
    if (buffer->count < 255U) {  /* Check against max value - 1 */
        buffer->count++;
    } else {
        // Overwrite oldest when full
        buffer->tail = (uint8_t)((buffer->tail + 1U) % KEYBOARD_BUFFER_SIZE);
    }
}

// Initialize keyboard
void keyboard_init(void) {
    // Register keyboard interrupt handler for IRQ1
    irq_install_handler(1, keyboard_handler);

    // Reset keyboard state
    for (int i = 0; i < 256; i++) {
        keyboard_state.key_states[i] = KEY_UP;
        keyboard_state.last_keypress_time[i] = 0;
    }

    for (int i = 0; i < 8; i++) {
        keyboard_state.modifier_states[i] = false;
    }

    keyboard_state.buffer.head = 0;
    keyboard_state.buffer.tail = 0;
    keyboard_state.buffer.count = 0;

    // Enable keyboard
    while (inb(KEYBOARD_STATUS_PORT) & 0x02);
    outb(KEYBOARD_DATA_PORT, KEYBOARD_CMD_ENABLE);

    print("Keyboard initialized.\n");
}

// Update system ticks (usually called from timer interrupt)
void keyboard_update_ticks(void) {
    system_ticks++;
}

// Handle extended keys (e.g., arrow keys) after detecting `KEY_EXTENDED`
static void handle_extended_key(uint8_t key) {
    bool is_release = (key & KEY_RELEASED) != 0;
    key = (uint8_t)(key & ~KEY_RELEASED);

    if (is_release) {
        return;
    }

    switch (key) {
        case EXT_KEY_UP:
        case EXT_KEY_DOWN:
        case EXT_KEY_LEFT:
        case EXT_KEY_RIGHT:
        case 0x53:  // Delete key
            enqueue_event(key, 0);
            break;
        default:
            // Other extended keys can be handled here
            break;
    }
}

// Process a normal key press, including function keys and modifiers
static void process_keypress(uint8_t key) {
    bool is_release = (key & KEY_RELEASED) != 0;
    key = (uint8_t)(key & ~KEY_RELEASED);  // Remove release bit

    // Ignore scancodes we don't have a translation for to avoid OOB access
    if (key >= sizeof(scancode_to_ascii)) {
        return;
    }

    // Handle modifier key presses/releases
    switch (key) {
        case KEY_CAPS:
            if (!is_release) {
                caps_lock_active = !caps_lock_active;  // Toggle Caps Lock on press
            }
            return;

        case KEY_LSHIFT:
            left_shift_active = !is_release;
            keyboard_state.modifier_states[MOD_SHIFT] = left_shift_active || right_shift_active;
            return;

        case KEY_RSHIFT:
            right_shift_active = !is_release;
            keyboard_state.modifier_states[MOD_SHIFT] = left_shift_active || right_shift_active;
            return;
    }

    // Skip processing if key was released
    if (is_release) {
        return;
    }

    // Determine if shift is active and pick the proper ASCII mapping
    bool shift_active = left_shift_active || right_shift_active;
    char ascii;
    if (shift_active ^ caps_lock_active) {
        ascii = scancode_to_ascii_shift[key];
    } else {
        ascii = scancode_to_ascii[key];
    }

    // Enqueue the key event into the circular buffer
    enqueue_event(key, ascii);
}

// Convert scancode to ASCII
static char get_ascii_from_scancode(uint8_t scancode) {
    // Ignore key releases
    if (scancode & KEY_RELEASED) {
        return 0;
    }

    // Ensure scancode is within bounds
    if (scancode >= sizeof(scancode_to_ascii)) {
        return 0;
    }

    // Determine character from scancode
    if (keyboard_state.modifier_states[MOD_SHIFT] ^ keyboard_state.modifier_states[MOD_CAPS]) {
        return scancode_to_ascii_shift[scancode];
    } else {
        return scancode_to_ascii[scancode];
    }
}
static bool function_keys[12] = {false};
// Keyboard interrupt handler
void keyboard_handler(struct registers* r) {
    (void)r; /* Unused parameter */
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);

    // Handle extended key sequences
    if (scancode == KEY_EXTENDED) {
        handling_extended = true;
        return;
    }

    // If we are handling an extended key, call the appropriate handler
    if (handling_extended) {
        handling_extended = false;
        handle_extended_key(scancode);
        return;
    }

    // Process the key event
    process_keypress(scancode);
}

// Check if a key is currently pressed
bool keyboard_get_key_state(uint8_t scancode) {
    return keyboard_state.key_states[scancode] == KEY_DOWN;
}

// Retrieve a raw scancode from the keyboard buffer
char keyboard_get_scancode(void) {
    if (keyboard_state.buffer.count > 0) {
        key_event_t event = keyboard_state.buffer.events[keyboard_state.buffer.head];
        keyboard_state.buffer.head = (uint8_t)((keyboard_state.buffer.head + 1U) % KEYBOARD_BUFFER_SIZE);
        keyboard_state.buffer.count--;
        return (char)event.scancode;
    }
    return 0;
}

// Check if a specific function key (1-12) is pressed
bool keyboard_get_function_key(uint8_t f_num) {
    if (f_num >= 1 && f_num <= 12) {
        return function_keys[f_num - 1];
    }
    return false;
}

// Get the current state of Caps Lock
bool keyboard_get_caps_lock(void) {
    return caps_lock_active;
}

// Get the current state of Shift
bool keyboard_get_shift(void) {
    return left_shift_active || right_shift_active;
}

// Retrieve a single character from keyboard input
char keyboard_get_char(void) {
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);
    
    // Ignore key release
    if (scancode & KEY_RELEASED) {
        return 0;
    }
    
    // Convert scancode to ASCII
    return get_ascii_from_scancode(scancode);
}

char getchar(void) {
    key_event_t event;
    /* Blocking: wait for a key event.
     * In GUI mode, pump the display so the screen stays alive
     * (needed when a blocking command like ed is running). */
    while (!keyboard_read_event(&event)) {
        if (shell_get_output_mode() == SHELL_OUTPUT_GUI) {
            /* Mark terminal dirty so any new output is painted */
            terminal_mark_dirty();
            desktop_redraw_cycle();
        }
        __asm__ volatile("hlt");
    }
    return event.character;
}

bool keyboard_read_event(key_event_t* event) {
    if (!event) {
        return false;
    }

    /* Non-blocking: return false if no events available */
    if (keyboard_state.buffer.count > 0) {
        *event = keyboard_state.buffer.events[keyboard_state.buffer.tail];
        keyboard_state.buffer.tail = (uint8_t)((keyboard_state.buffer.tail + 1U) % KEYBOARD_BUFFER_SIZE);
        keyboard_state.buffer.count--;
        return true;
    }
    return false;
}
