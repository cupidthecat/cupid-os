#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "../kernel/types.h"
#include "../kernel/irq.h"

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

#endif 