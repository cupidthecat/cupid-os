// File: drivers/desktop.c
// Make sure to include the appropriate headers
#include "desktop.h"      // You can declare your prototypes here
#include "vga.h"          // For draw_rect() and putpixel()
#include "mouse.h"        // For the mouse globals and window_t type
#include "timer.h"        // For timer_delay_us()

// Use the same desktop background color defined in your original code.
#define DESKTOP_BG_COLOR 0x1D
// Define the height (in pixels) of the window "title bar"
#define WINDOW_HEADER_HEIGHT (FONT_HEIGHT + 2)  // Match font height + padding

// Create one global window – you can also allocate more and manage them in a list.
window_t current_window = {
    .x = (320 - 280) / 2,  // Center horizontally
    .y = (200 - 180) / 2,  // Center vertically
    .width = 280,          // 280px wide (320px max)
    .height = 180,         // 180px tall (200px max)
    .dragging = false,
    .drag_offset_x = 0,
    .drag_offset_y = 0,
    .title = "Window",
    .visible = true,
};

// These are declared in your mouse driver.
extern volatile int16_t mouse_x;
extern volatile int16_t mouse_y;
extern volatile mouse_packet_t current_packet;

/*
 * desktop_init()
 *
 * Clears the entire screen (320x200 in mode 0x13) using the desktop background color.
 */
void desktop_init(void) {
    // Clear screen
    draw_rect(0, 0, 320, 200, DESKTOP_BG_COLOR);
    
    // Draw initial window
    desktop_draw_window(&current_window);
}

void putchar_at(char c, int x, int y) {
    uint8_t* glyph = font_data + c * FONT_HEIGHT;
    for(uint8_t cy = 0; cy < FONT_HEIGHT; cy++, glyph++) {
        for(uint8_t cx = 0; cx < FONT_WIDTH; cx++) {
            if(*glyph & (0x80 >> cx)) {
                putpixel(x + cx, y + cy, vga_fg_color);
            }
        }
    }
}

void draw_close_button(window_t *win) {
    int button_x = win->x + win->width - CLOSE_BUTTON_SIZE - CLOSE_BUTTON_MARGIN;
    int button_y = win->y + CLOSE_BUTTON_MARGIN;
    
    // Draw the button background with a border
    draw_rect(button_x, button_y, CLOSE_BUTTON_SIZE, CLOSE_BUTTON_SIZE, CLOSE_BUTTON_BG);
    
    // Draw border around the button
    for (int i = 0; i < CLOSE_BUTTON_SIZE; i++) {
        putpixel(button_x + i, button_y, VGA_WHITE); // top border
        putpixel(button_x + i, button_y + CLOSE_BUTTON_SIZE - 1, VGA_WHITE); // bottom border
        putpixel(button_x, button_y + i, VGA_WHITE); // left border
        putpixel(button_x + CLOSE_BUTTON_SIZE - 1, button_y + i, VGA_WHITE); // right border
    }

    // Draw a thicker "X" inside the button
    for (int i = 1; i < CLOSE_BUTTON_SIZE - 1; i++) {
        // Main diagonal
        putpixel(button_x + i, button_y + i, CLOSE_BUTTON_XCOLOR);
        putpixel(button_x + i + 1, button_y + i, CLOSE_BUTTON_XCOLOR);
        putpixel(button_x + i, button_y + i + 1, CLOSE_BUTTON_XCOLOR);
        
        // Secondary diagonal
        putpixel(button_x + CLOSE_BUTTON_SIZE - 1 - i, button_y + i, CLOSE_BUTTON_XCOLOR);
        putpixel(button_x + CLOSE_BUTTON_SIZE - 1 - i + 1, button_y + i, CLOSE_BUTTON_XCOLOR);
        putpixel(button_x + CLOSE_BUTTON_SIZE - 1 - i, button_y + i + 1, CLOSE_BUTTON_XCOLOR);
    }
}

/*
 * desktop_draw_window()
 *
 * Draws a window (with a title bar and body) at its current position.
 * The title bar (first WINDOW_HEADER_HEIGHT pixels) is drawn in a distinct color.
 */
void desktop_draw_window(window_t *win) {
    // Draw the title bar (header) in color 0x4 (for example, red)
    draw_rect(win->x, win->y, win->width, WINDOW_HEADER_HEIGHT, 0x4);
    
    // Draw window title
    if (win->title) {
        int title_x = win->x + 4;
        int title_y = win->y + (WINDOW_HEADER_HEIGHT - FONT_HEIGHT)/2;
        int max_title_width = win->width - (CLOSE_BUTTON_SIZE + CLOSE_BUTTON_MARGIN + 8); // Reserve space for button + margins
        vga_set_color(VGA_WHITE, 0x4);
        for (int i = 0; win->title[i] && (title_x - win->x) < max_title_width; i++) {
            putchar_at(win->title[i], title_x, title_y);
            title_x += FONT_WIDTH;
        }
    }
    draw_close_button(win);

    // Draw the window body (using a light gray color, for example 0x07)
    draw_rect(win->x, win->y + WINDOW_HEADER_HEIGHT, win->width, win->height - WINDOW_HEADER_HEIGHT, 0x07);
    
    // Draw borders
    for (int j = win->y; j < win->y + win->height; j++) {
        putpixel(win->x, j, 0); // left border
        putpixel(win->x + win->width - 1, j, 0); // right border
    }
    for (int i = win->x; i < win->x + win->width; i++) {
        putpixel(i, win->y + win->height - 1, 0); // bottom border
    }
}


/*W
 * desktop_update_window()
 *
 * Checks the current mouse position and button state to decide whether to
 * start dragging the window (if the mouse is clicked in the header) or to update
 * its position if it is already being dragged.
 *
 * @param win Pointer to the window structure to update
 */
void desktop_update_window(window_t *win) {
    // First, check if the mouse click occurred inside the close button.
    if (current_packet.left_button) {
        // Calculate close button bounds.
        int close_x = win->x + win->width - (CLOSE_BUTTON_SIZE + CLOSE_BUTTON_MARGIN);
        int close_y = win->y + CLOSE_BUTTON_MARGIN;
        if (mouse_x >= close_x && mouse_x < close_x + CLOSE_BUTTON_SIZE &&
            mouse_y >= close_y && mouse_y < close_y + CLOSE_BUTTON_SIZE) {
            // Close the window:
            win->visible = false;
            // Clear the window area so it disappears from the desktop.
            draw_rect(win->x, win->y, win->width, win->height, DESKTOP_BG_COLOR);
            return; // Exit early – do not process further dragging
        }
    }
    
    // (Existing code to handle dragging the window, etc.)
    if (current_packet.left_button) {
        // If not already dragging, check if click is in header area (but not over the close button)
        if (!win->dragging) {
            if (mouse_x >= win->x && mouse_x < win->x + win->width &&
                mouse_y >= win->y && mouse_y < win->y + WINDOW_HEADER_HEIGHT) {
                win->dragging = true;
                win->drag_offset_x = mouse_x - win->x;
                win->drag_offset_y = mouse_y - win->y;
            }
        } else {
            // Update dragging...
            int16_t prev_x = win->x;
            int16_t prev_y = win->y;
            win->x = mouse_x - win->drag_offset_x;
            win->y = mouse_y - win->drag_offset_y;
            // Constrain window within screen boundaries...
            if (win->x < 0) win->x = 0;
            if (win->y < 0) win->y = 0;
            if (win->x + win->width > 320) win->x = 320 - win->width;
            if (win->y + win->height > 200) win->y = 200 - win->height;
            
            if (win->x != prev_x || win->y != prev_y) {
                // Clear previous window location
                draw_rect(prev_x, prev_y, win->width, win->height, DESKTOP_BG_COLOR);
                // Redraw window at new position
                desktop_draw_window(win);
            }
        }
    } else {
        // Cancel dragging when mouse button is released.
        win->dragging = false;
    }
}


/*
 * desktop_run()
 *
 * Main desktop loop. In this loop the desktop background is redrawn, the window is updated
 * and re-drawn in its new location (if it has been dragged), and then the CPU is halted until
 * the next interrupt.
 */
void desktop_run(void) {
    // Draw initial desktop and window
    desktop_init();
    desktop_draw_window(&current_window);
    
    while (1) {
        // Update window position based on mouse input
        desktop_update_window(&current_window);
        
        // Only redraw if window has moved
        if (current_window.dragging) {
            // Clear only the area where the window was previously
            draw_rect(current_window.x, current_window.y, 
                     current_window.width, current_window.height, 
                     DESKTOP_BG_COLOR);
            
            // Redraw window at new position
            desktop_draw_window(&current_window);
        }
        
        timer_delay_us(10000); // Delay roughly 10ms
        __asm__ volatile("hlt");  // Halt until next interrupt
    }
}

