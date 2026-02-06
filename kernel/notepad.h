/**
 * notepad.h - Windows XP-style Notepad for cupid-os
 *
 * Full GUI text editor with menu bar, scrollbars, file operations,
 * clipboard support, and undo/redo.  Renders in VGA Mode 13h
 * (320x200, 256 colors) using the existing graphics primitives.
 */

#ifndef NOTEPAD_H
#define NOTEPAD_H

#include "gui.h"

/* Launch Notepad (creates window, registers callbacks) */
void notepad_launch(void);

/* Redraw callback for the notepad window */
void notepad_redraw(window_t *win);

/* Forward a key event to notepad */
void notepad_handle_key(uint8_t scancode, char character);

/* Forward a mouse event to notepad */
void notepad_handle_mouse(int16_t mx, int16_t my, uint8_t buttons,
                          uint8_t prev_buttons);

/* Handle scroll wheel in notepad */
void notepad_handle_scroll(int delta);

/* Periodic tick (cursor blink) */
void notepad_tick(void);

/* Get notepad window ID (-1 if not open) */
int notepad_get_wid(void);

#endif
