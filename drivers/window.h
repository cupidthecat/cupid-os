#ifndef WINDOW_H
#define WINDOW_H

#include "../kernel/types.h"

// A window structure – you can add additional fields as needed.
typedef struct window {
    int16_t x, y;           // Top-left position
    uint16_t width, height; // Dimensions
    bool visible;           // Is the window visible?
    bool dragging;          // Is the window currently being dragged?
    int16_t drag_offset_x, drag_offset_y; // Mouse offset when dragging
    const char* title;      // Window title
    uint8_t z_index;        // Z-order (for layering, if desired)
} window_t;

#define CLOSE_BUTTON_SIZE 16

/* API Functions */

// Create a new window. (This example uses a static pool.)
window_t* window_create(int16_t x, int16_t y, uint16_t width, uint16_t height, const char* title);

// Destroys (frees) a window.
void window_destroy(window_t* win);

// Draws the window on screen.
void window_draw(window_t* win);

// Updates the window (redraw, etc).
void window_update(window_t* win);

// Handles mouse input for this window.
// Parameters: current mouse coordinates and left-button state.
void window_handle_mouse(window_t* win, int mouse_x, int mouse_y, bool left_button);

// Change the window title.
void window_set_title(window_t* win, const char* title);

#endif
