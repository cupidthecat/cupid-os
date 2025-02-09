#ifndef TYPES_H
#define TYPES_H

// Standard NULL pointer definition
#define NULL ((void*)0)

// Boolean type definition
typedef enum { false = 0, true = 1 } bool;

// Unsigned integer types
typedef unsigned char uint8_t;       // 8-bit unsigned integer
typedef unsigned short uint16_t;     // 16-bit unsigned integer
typedef unsigned int uint32_t;       // 32-bit unsigned integer
typedef unsigned long long uint64_t; // 64-bit unsigned integer
typedef unsigned long size_t;        // Size type for memory operations

// Signed integer types
typedef char int8_t;                 // 8-bit signed integer
typedef short int16_t;               // 16-bit signed integer
typedef int int32_t;                 // 32-bit signed integer
typedef long long int64_t;           // 64-bit signed integer

// Keyboard buffer size (adjust as needed)
#define KEYBOARD_BUFFER_SIZE 256     // Size of keyboard event buffer

// Key states enumeration
typedef enum {
    KEY_UP = 0,     // Key is not pressed
    KEY_DOWN = 1,   // Key was just pressed
    KEY_HELD = 2    // Key is being held down
} key_state_t;

// Enhanced keyboard event structure
typedef struct {
    uint8_t scancode;    // Raw keyboard scancode
    char character;      // Translated ASCII character
    bool pressed;        // Whether key is pressed or released
    uint32_t timestamp;  // Time of event in system ticks
} key_event_t;

// Circular buffer for keyboard events
typedef struct {
    key_event_t events[KEYBOARD_BUFFER_SIZE]; // Array of stored events
    uint8_t head;                            // Index of newest event
    uint8_t tail;                            // Index of oldest event
    uint8_t count;                           // Number of events in buffer
} keyboard_buffer_t;

// Keyboard state tracking structure
typedef struct {
    key_state_t key_states[256];      // Track state for each possible scancode
    bool modifier_states[8];          // States for modifier keys (shift, ctrl, alt, etc.)
    uint32_t last_keypress_time[256]; // Timestamps for debouncing
    keyboard_buffer_t buffer;         // Event buffer
} keyboard_state_t;

// Timer calibration and management structure
typedef struct {
    uint64_t ticks;           // Total number of timer ticks since boot
    uint32_t frequency;       // Timer frequency in Hz
    uint32_t ms_per_tick;     // Milliseconds per tick
    bool is_calibrated;       // Whether timer has been calibrated
} timer_state_t;

// Time measurement structure
typedef struct {
    uint64_t start_tick;      // Starting tick count for measurement
    uint64_t duration_ms;     // Duration in milliseconds
} timer_measure_t;

#endif 