#ifndef TERMINAL_APP_H
#define TERMINAL_APP_H

#include "gui.h"

/* Launch a new terminal window */
void terminal_launch(void);

/* Redraw callback for the terminal window */
void terminal_redraw(window_t *win);

/* Forward a key event to the terminal's shell */
void terminal_handle_key(uint8_t scancode, char character);

/* Call periodically from the desktop loop to animate cursor blink */
void terminal_tick(void);

/* Mark the terminal window dirty so it gets repainted */
void terminal_mark_dirty(void);

/* Scroll the terminal by delta lines (negative=up, positive=down) */
void terminal_handle_scroll(int delta);

#endif
