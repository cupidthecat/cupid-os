#ifndef MOUSE_H
#define MOUSE_H

#include "../kernel/types.h"
#include "../kernel/isr.h"

/* Mouse button masks */
#define MOUSE_LEFT   0x01
#define MOUSE_RIGHT  0x02
#define MOUSE_MIDDLE 0x04

/* Clamp helper */
#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))

/* Mouse state (global, updated by IRQ12) */
typedef struct {
    int16_t  x, y;           /* Cursor position (0-319, 0-199) */
    int8_t   scroll_z;       /* Scroll wheel delta (neg=up, pos=down) */
    uint8_t  buttons;        /* Current button state            */
    uint8_t  prev_buttons;   /* Previous button state           */
    bool     updated;        /* New data available              */
} mouse_state_t;

extern mouse_state_t mouse;

/* Initialize PS/2 mouse and install IRQ12 handler */
void mouse_init(void);

/* IRQ12 handler (registered automatically by mouse_init) */
void mouse_irq_handler(struct registers *r);

/* Draw / erase the mouse cursor on the back buffer */
void mouse_draw_cursor(void);
void mouse_save_under_cursor(void);
void mouse_restore_under_cursor(void);
void mouse_mark_cursor_dirty(void); /* mark dirty rect covering old+new cursor */

/* Fast path: restore old cursor and draw new cursor directly on the
 * displayed LFB page — no memcpy/flip needed. */
void mouse_update_cursor_direct(void);

#endif
