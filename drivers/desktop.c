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
    .title = "terminal"
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
        int title_y = win->y + (WINDOW_HEADER_HEIGHT - FONT_HEIGHT)/2;  // Center vertically
        vga_set_color(VGA_WHITE, 0x4); // White text on red background
        for (int i = 0; win->title[i]; i++) {
            putchar_at(win->title[i], title_x, title_y);
            title_x += FONT_WIDTH;
        }
    }
    
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

/*
 * desktop_update_window()
 *
 * Checks the current mouse position and button state to decide whether to
 * start dragging the window (if the mouse is clicked in the header) or to update
 * its position if it is already being dragged.
 *
 * @param win Pointer to the window structure to update
 */
void desktop_update_window(window_t *win) {
    // Save previous position
    int16_t prev_x = win->x;
    int16_t prev_y = win->y;

    // If the left mouse button is pressed...
    if (current_packet.left_button) {
        // If we are not already dragging, check if the click is in the header area.
        if (!win->dragging) {
            if (mouse_x >= win->x && mouse_x < win->x + win->width &&
                mouse_y >= win->y && mouse_y < win->y + WINDOW_HEADER_HEIGHT) {
                win->dragging = true;
                // Record the offset of the mouse within the window so the drag looks natural.
                win->drag_offset_x = mouse_x - win->x;
                win->drag_offset_y = mouse_y - win->y;
            }
        } else {
            // If already dragging, update the window's position based on current mouse position.
            win->x = mouse_x - win->drag_offset_x;
            win->y = mouse_y - win->drag_offset_y;
            // Constrain the window so that it does not move off-screen.
            if (win->x < 0) win->x = 0;
            if (win->y < 0) win->y = 0;
            if (win->x + win->width > 320) win->x = 320 - win->width;
            if (win->y + win->height > 200) win->y = 200 - win->height;

            if (win->x != prev_x || win->y != prev_y) {
                // Save cursor state
                uint8_t cursor_pixels[4] = {
                    getpixel(mouse_x, mouse_y),
                    getpixel(mouse_x+1, mouse_y),
                    getpixel(mouse_x, mouse_y+1),
                    getpixel(mouse_x+1, mouse_y+1)
                };

                // Clear previous window location
                draw_rect(prev_x, prev_y, win->width, win->height, DESKTOP_BG_COLOR);
                
                // Restore cursor if it was over cleared area
                if (mouse_x >= prev_x && mouse_x < prev_x + win->width &&
                    mouse_y >= prev_y && mouse_y < prev_y + win->height) {
                    putpixel(mouse_x, mouse_y, cursor_pixels[0]);
                    putpixel(mouse_x+1, mouse_y, cursor_pixels[1]);
                    putpixel(mouse_x, mouse_y+1, cursor_pixels[2]);
                    putpixel(mouse_x+1, mouse_y+1, cursor_pixels[3]);
                }
                
                // Draw window at new position
                desktop_draw_window(win);
                
                // Force redraw cursor over new window
                putpixel(mouse_x, mouse_y, 0x04);
                putpixel(mouse_x+1, mouse_y, 0x04);
                putpixel(mouse_x, mouse_y+1, 0x04);
                putpixel(mouse_x+1, mouse_y+1, 0x04);
            }
        }
    } else {
        // If the left mouse button is not pressed, cancel dragging.
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

