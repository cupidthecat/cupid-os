#ifndef DESKTOP_H
#define DESKTOP_H

#include "../kernel/types.h"
#include "mouse.h"
// Function prototypes
void desktop_init(void);
void desktop_run(void);

typedef struct {
    int16_t x, y;
    uint16_t width, height;
    bool dragging;
    int16_t drag_offset_x, drag_offset_y;
    const char* title;  // New title field
} window_t;

// Add this after the function prototypes:
extern window_t current_window;

#endif
