#ifndef TYPES_H
#define TYPES_H

#define NULL ((void*)0)

typedef enum { false = 0, true = 1 } bool;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef unsigned long size_t;

typedef char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long long int64_t;

// Keyboard buffer size (adjust as needed)
#define KEYBOARD_BUFFER_SIZE 256

// Key states
typedef enum {
    KEY_UP = 0,
    KEY_DOWN = 1,
    KEY_HELD = 2
} key_state_t;

// Enhanced keyboard event structure
typedef struct {
    uint8_t scancode;
    char character;
    bool pressed;
    uint32_t timestamp;
} key_event_t;

// Circular buffer for keyboard events
typedef struct {
    key_event_t events[KEYBOARD_BUFFER_SIZE];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
} keyboard_buffer_t;

// Keyboard state tracking
typedef struct {
    key_state_t key_states[256];      // Track state for each possible scancode
    bool modifier_states[8];          // States for modifier keys (shift, ctrl, alt, etc.)
    uint32_t last_keypress_time[256]; // For debouncing
    keyboard_buffer_t buffer;
} keyboard_state_t;

// Timer calibration and management structures
typedef struct {
    uint64_t ticks;           // Total number of timer ticks since boot
    uint32_t frequency;       // Timer frequency in Hz
    uint32_t ms_per_tick;     // Milliseconds per tick
    bool is_calibrated;       // Whether timer has been calibrated
} timer_state_t;

// Time measurement structure
typedef struct {
    uint64_t start_tick;      // Starting tick count
    uint64_t duration_ms;     // Duration in milliseconds
} timer_measure_t;


#endif 