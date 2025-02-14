#include "window.h"
#include "vga.h"      // For draw_rect, putpixel, etc.
#include "desktop.h"
#include "mouse.h"
#define MAX_WINDOWS 10
#define TITLE_BAR_HEIGHT 20

// A static pool for window objects.
static window_t window_pool[MAX_WINDOWS];
static bool window_pool_used[MAX_WINDOWS] = {0};

window_t* window_create(int16_t x, int16_t y, uint16_t width, uint16_t height, const char* title) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!window_pool_used[i]) {
            window_pool_used[i] = true;
            window_t* win = &window_pool[i];
            win->x = x;
            win->y = y;
            win->width = width;
            win->height = height;
            win->visible = true;
            win->dragging = false;
            win->drag_offset_x = 0;
            win->drag_offset_y = 0;
            win->title = title;
            win->z_index = 0;
            return win;
        }
    }
    return 0; // No free window slot available.
}

void window_destroy(window_t* win) {
    if (win) {
        int index = win - window_pool; // pointer arithmetic
        if (index >= 0 && index < MAX_WINDOWS) {
            window_pool_used[index] = false;
        }
    }
}

void window_draw(window_t* win) {
    if (!win || !win->visible) return;
    
    // Clear previous window area
    draw_rect(win->x, win->y, win->width, win->height, DESKTOP_BG_COLOR);
    
    // Draw title bar
    draw_rect(win->x, win->y, win->width, TITLE_BAR_HEIGHT, 0x04); // e.g. blue title bar
    // Draw close button
    int close_x = win->x + win->width - CLOSE_BUTTON_SIZE;
    draw_rect(close_x, win->y, CLOSE_BUTTON_SIZE, CLOSE_BUTTON_SIZE, 0x40); // Dark red
    // Draw window body
    draw_rect(win->x, win->y + TITLE_BAR_HEIGHT, win->width, win->height - TITLE_BAR_HEIGHT, 0x07); // light gray body

    // Draw borders (simple single-pixel border)
    for (int j = win->y; j < win->y + win->height; j++) {
        putpixel(win->x, j, 0);                         // left border
        putpixel(win->x + win->width - 1, j, 0);        // right border
    }
    for (int i = win->x; i < win->x + win->width; i++) {
        putpixel(i, win->y, 0);                         // top border
        putpixel(i, win->y + win->height - 1, 0);       // bottom border
    }
    
    // Draw window title text inside the title bar.
    if (win->title) {
        int title_x = win->x + 4;
        int title_y = win->y + 2;  // Fixed position 2 pixels from top
        vga_set_color(VGA_WHITE, 0x04);
        
        // Ensure title doesn't overflow into close button area
        int max_title_width = win->width - CLOSE_BUTTON_SIZE - 8;
        for (int i = 0; win->title[i] && (title_x < (win->x + max_title_width)); i++) {
            putchar_at(win->title[i], title_x, title_y);
            title_x += FONT_WIDTH;
        }
    }
}

void window_update(window_t* win) {
    // todo: animations or dirty-region redraws.
    window_draw(win);
}

void window_handle_mouse(window_t* win, int mouse_x, int mouse_y, bool left_button) {
    if (!win || !win->visible) return;
    
    if (left_button) {
        if (!win->dragging) {
            // Check if click is inside title bar or close button
            int close_x_start = win->x + win->width - CLOSE_BUTTON_SIZE;
            bool in_close = mouse_x >= close_x_start && mouse_x < close_x_start + CLOSE_BUTTON_SIZE &&
                          mouse_y >= win->y && mouse_y < win->y + CLOSE_BUTTON_SIZE;
            
            if (in_close) {
                win->visible = false;
                // Force mouse cursor redraw
                int16_t saved_x = mouse_x;
                int16_t saved_y = mouse_y;
                mouse_x++;
                mouse_handler(0); // Force cursor update
                mouse_x = saved_x;
                mouse_y = saved_y;
                mouse_handler(0); // Restore position
                
                // Clear window area
                draw_rect(win->x, win->y, win->width, win->height, DESKTOP_BG_COLOR);
            }
            else if (mouse_x >= win->x && mouse_x < win->x + win->width &&
                mouse_y >= win->y && mouse_y < win->y + TITLE_BAR_HEIGHT) {
                win->dragging = true;
                win->drag_offset_x = mouse_x - win->x;
                win->drag_offset_y = mouse_y - win->y;
            }
        } else {
            // Update window position based on mouse movement.
            int16_t old_x = win->x;
            int16_t old_y = win->y;
            
            win->x = mouse_x - win->drag_offset_x;
            win->y = mouse_y - win->drag_offset_y;
            
            // Clamp the window inside the screen boundaries (assuming 320x200 resolution).
            if (win->x < 0) win->x = 0;
            if (win->y < 0) win->y = 0;
            if (win->x + win->width > 320) win->x = 320 - win->width;
            if (win->y + win->height > 200) win->y = 200 - win->height;
            
            // Clear previous window location
            draw_rect(old_x, old_y, win->width, win->height, DESKTOP_BG_COLOR);
            
            // Redraw window after any changes.
            window_draw(win);
        }
    } else {
        win->dragging = false;
    }
}

void window_set_title(window_t* win, const char* title) {
    if (win) {
        win->title = title;
        window_draw(win);
    }
}
