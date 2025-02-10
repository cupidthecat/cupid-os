#ifndef MOUSE_H
#define MOUSE_H

#include "../kernel/types.h"

struct registers;

#define MOUSE_IRQ 12
#define MOUSE_DATA_PORT 0x60
#define MOUSE_STATUS_PORT 0x64
#define MOUSE_COMMAND_PORT 0x64

// Mouse packet structure
typedef struct {
    int8_t x_movement;
    int8_t y_movement;
    bool left_button;
    bool right_button;
    bool middle_button;
} mouse_packet_t;

// Mouse initialization and handling functions
void mouse_init(void);
void mouse_handler(struct registers* r);
bool mouse_get_packet(mouse_packet_t* packet);
void mouse_wait(uint8_t type);
void mouse_write(uint8_t data);
uint8_t mouse_read(void);

#endif