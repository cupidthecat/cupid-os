#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "types.h"
#include "irq.h"

// Keyboard ports
#define KEYBOARD_DATA_PORT    0x60
#define KEYBOARD_STATUS_PORT  0x64

// Keyboard commands
#define KEYBOARD_CMD_LED      0xED
#define KEYBOARD_CMD_ENABLE   0xF4
#define KEYBOARD_CMD_DISABLE  0xF5
#define KEYBOARD_CMD_RESET    0xFF

// Function declarations
void keyboard_init(void);
void keyboard_handler(struct registers* r);
char keyboard_get_scancode(void);
bool keyboard_get_key_state(uint8_t scancode);
bool keyboard_get_function_key(uint8_t f_num);
void keyboard_update_ticks(void);
char keyboard_get_char(void);
bool keyboard_get_caps_lock(void);
bool keyboard_get_shift(void);
bool keyboard_get_ctrl(void);
bool keyboard_get_alt(void);
char getchar(void);
bool keyboard_read_event(key_event_t* event);

/* Inject a PS/2-style scancode byte (high bit = release) as if it came
 * from the IRQ1 ISR. Used by USB HID driver to unify input paths. */
void keyboard_inject_scancode(uint8_t raw_scancode);

/* Raw scancode subscriber.
 * Callback fires from the keyboard IRQ tail with the cooked
 * (high bit cleared) scancode and a `pressed` flag (true on
 * make, false on break). Single subscriber slot - second
 * subscribe attempt returns -1.
 *
 * Subscriber callback runs under the BKL with IRQs disabled;
 * keep it short - push to a ring and process out of IRQ.
 *
 * subscribe/unsubscribe must run under the BKL on this SMP kernel -
 * concurrent IRQ-side fire is permitted because each call caches the
 * callback pointer locally before dispatch.
 */
typedef void (*kbd_event_cb)(uint8_t scancode, bool pressed, void *ctx);
int  keyboard_subscribe(kbd_event_cb cb, void *ctx);   /* returns 0 on success, -1 if slot taken */
void keyboard_unsubscribe(void);

/* Test-shim accessors - used by bin/kbdsub_test.cc smoke test via CupidC. */
int  keyboard_test_sub_start(void);
void keyboard_test_sub_stop(void);
int  keyboard_test_sub_calls(void);
int  keyboard_test_sub_last_sc(void);
int  keyboard_test_sub_last_pressed(void);

#endif
