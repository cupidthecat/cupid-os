#include "desktop.h"
#include "window.h"
#include "vga.h"
#include "timer.h"
#include "mouse.h"

// Global pointer for our main window.
window_t* current_window = 0;
extern volatile mouse_packet_t current_packet;

void desktop_init(void) {
    // Clear the screen using the desktop background color.
    draw_rect(0, 0, 320, 200, DESKTOP_BG_COLOR);
    
    // Create a window (centered on the screen)
    current_window = window_create((320 - 280) / 2, (200 - 180) / 2, 280, 180, "Window");
    if (current_window)
        window_draw(current_window);
}

void desktop_run(void) {
    desktop_init();
    
    while (1) {
        // Handle mouse input – update the current window based on mouse position.
        window_handle_mouse(current_window, mouse_x, mouse_y, current_packet.left_button);
        
        // A short delay so we are not redrawing too rapidly.
        timer_delay_us(10000);
        __asm__ volatile("hlt");
    }
}
