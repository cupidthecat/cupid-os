/**
 * gui.c - Window manager and GUI API for cupid-os
 *
 * Provides window creation, rendering, dragging, focus management,
 * and input dispatch.  Windows are stored in a flat array ordered
 * by z-index (index 0 = back, highest = front / focused).
 */

#include "gui.h"
#include "graphics.h"
#include "string.h"
#include "../drivers/vga.h"
#include "../drivers/serial.h"

/* ── Window array (z-ordered) ─────────────────────────────────────── */
static window_t  windows[MAX_WINDOWS];
static int       win_count = 0;
static uint32_t  next_id   = 1;

/* ── Drag state ───────────────────────────────────────────────────── */
static drag_state_t drag = { false, -1, 0, 0 };

/* ── Init ─────────────────────────────────────────────────────────── */

void gui_init(void) {
    win_count = 0;
    next_id   = 1;
    drag.dragging = false;
    memset(windows, 0, sizeof(windows));
    KINFO("GUI initialized (max %d windows)", MAX_WINDOWS);
}

/* ── Helpers ──────────────────────────────────────────────────────── */

/* Find array index for a given window ID; returns -1 if not found */
static int find_index(int wid) {
    for (int i = 0; i < win_count; i++) {
        if ((int)windows[i].id == wid) return i;
    }
    return -1;
}

/* Copy a window_t */
static void win_copy(window_t *dst, const window_t *src) {
    memcpy(dst, src, sizeof(window_t));
}

/* ── Create / Destroy ─────────────────────────────────────────────── */

int gui_create_window(int16_t x, int16_t y, uint16_t w, uint16_t h,
                      const char *title) {
    if (win_count >= MAX_WINDOWS) {
        KERROR("GUI: cannot create window, limit reached");
        return GUI_ERR_TOO_MANY;
    }

    window_t *win = &windows[win_count];
    memset(win, 0, sizeof(window_t));

    win->id     = next_id++;
    win->x      = x;
    win->y      = y;
    win->width  = w;
    win->height = h;
    win->flags  = WINDOW_FLAG_VISIBLE | WINDOW_FLAG_DIRTY;
    win->redraw = NULL;
    win->app_data = NULL;

    /* Copy title safely */
    int i = 0;
    if (title) {
        while (title[i] && i < 63) {
            win->title[i] = title[i];
            i++;
        }
    }
    win->title[i] = '\0';

    win_count++;
    gui_set_focus((int)win->id);

    KINFO("GUI: window %u created \"%s\" (%dx%d at %d,%d)",
          win->id, win->title, (int)w, (int)h, (int)x, (int)y);
    return (int)win->id;
}

int gui_destroy_window(int wid) {
    int idx = find_index(wid);
    if (idx < 0) return GUI_ERR_INVALID_ID;

    /* Notify the application that its window is being destroyed */
    if (windows[idx].on_close) {
        windows[idx].on_close(&windows[idx]);
    }

    /* Shift remaining windows down */
    for (int i = idx; i < win_count - 1; i++) {
        win_copy(&windows[i], &windows[i + 1]);
    }
    win_count--;

    /* Mark everything dirty */
    for (int i = 0; i < win_count; i++) {
        windows[i].flags |= WINDOW_FLAG_DIRTY;
    }

    KINFO("GUI: window %d destroyed", wid);
    return GUI_OK;
}

/* ── Focus ────────────────────────────────────────────────────────── */

int gui_set_focus(int wid) {
    int idx = find_index(wid);
    if (idx < 0) return GUI_ERR_INVALID_ID;

    /* Clear focused flag on all */
    for (int i = 0; i < win_count; i++) {
        windows[i].flags &= (uint8_t)~WINDOW_FLAG_FOCUSED;
        windows[i].flags |= WINDOW_FLAG_DIRTY;
    }

    /* Move window to top (end of array) */
    if (idx < win_count - 1) {
        window_t tmp;
        win_copy(&tmp, &windows[idx]);
        for (int i = idx; i < win_count - 1; i++) {
            win_copy(&windows[i], &windows[i + 1]);
        }
        win_copy(&windows[win_count - 1], &tmp);
    }

    windows[win_count - 1].flags |= WINDOW_FLAG_FOCUSED | WINDOW_FLAG_DIRTY;
    return GUI_OK;
}

window_t *gui_get_focused_window(void) {
    if (win_count == 0) return NULL;
    window_t *top = &windows[win_count - 1];
    if (top->flags & WINDOW_FLAG_FOCUSED) return top;
    return NULL;
}

window_t *gui_get_window(int wid) {
    int idx = find_index(wid);
    if (idx < 0) return NULL;
    return &windows[idx];
}

int gui_window_count(void) {
    return win_count;
}

bool gui_any_dirty(void) {
    for (int i = 0; i < win_count; i++) {
        if (windows[i].flags & WINDOW_FLAG_DIRTY) return true;
    }
    return false;
}

/* ── Drawing ──────────────────────────────────────────────────────── */

static void draw_single_window(window_t *win) {
    bool focused = (win->flags & WINDOW_FLAG_FOCUSED) != 0;

    /* Border */
    gfx_draw_rect(win->x, win->y, win->width, win->height, COLOR_BORDER);

    /* Title bar background */
    uint8_t title_color = focused ? COLOR_TITLEBAR : COLOR_TITLE_UNFOC;
    gfx_fill_rect((int16_t)(win->x + 1), (int16_t)(win->y + 1),
                  (uint16_t)(win->width - 2), (uint16_t)(TITLEBAR_H - 1),
                  title_color);

    /* Title text (left-aligned with 4px padding) */
    gfx_draw_text((int16_t)(win->x + 4), (int16_t)(win->y + 3),
                  win->title, COLOR_TEXT);

    /* Close button [X] in top-right corner */
    int16_t cx = (int16_t)(win->x + (int16_t)win->width - CLOSE_BTN_SIZE - 2);
    int16_t cy = (int16_t)(win->y + 2);
    gfx_fill_rect(cx, cy, CLOSE_BTN_SIZE, CLOSE_BTN_SIZE, COLOR_CLOSE_BG);
    /* Draw X inside close button */
    gfx_draw_line((int16_t)(cx + 2), (int16_t)(cy + 2),
                  (int16_t)(cx + CLOSE_BTN_SIZE - 3), (int16_t)(cy + CLOSE_BTN_SIZE - 3),
                  COLOR_TEXT_LIGHT);
    gfx_draw_line((int16_t)(cx + CLOSE_BTN_SIZE - 3), (int16_t)(cy + 2),
                  (int16_t)(cx + 2), (int16_t)(cy + CLOSE_BTN_SIZE - 3),
                  COLOR_TEXT_LIGHT);

    /* Content area background */
    gfx_fill_rect((int16_t)(win->x + 1), (int16_t)(win->y + TITLEBAR_H),
                  (uint16_t)(win->width - 2),
                  (uint16_t)(win->height - TITLEBAR_H - 1),
                  COLOR_WINDOW_BG);

    /* App-specific redraw */
    if (win->redraw) {
        win->redraw(win);
    }

    win->flags &= (uint8_t)~WINDOW_FLAG_DIRTY;
}

int gui_draw_window(int wid) {
    int idx = find_index(wid);
    if (idx < 0) return GUI_ERR_INVALID_ID;
    draw_single_window(&windows[idx]);
    return GUI_OK;
}

void gui_draw_all_windows(void) {
    /* Draw all windows back-to-front */
    for (int i = 0; i < win_count; i++) {
        if (windows[i].flags & WINDOW_FLAG_VISIBLE) {
            draw_single_window(&windows[i]);
        }
    }
}

/* ── Hit testing (front to back) ──────────────────────────────────── */

int gui_hit_test_titlebar(int16_t mx, int16_t my) {
    for (int i = win_count - 1; i >= 0; i--) {
        window_t *w = &windows[i];
        if (!(w->flags & WINDOW_FLAG_VISIBLE)) continue;
        if (mx >= w->x && mx < w->x + (int16_t)w->width &&
            my >= w->y && my < w->y + TITLEBAR_H) {
            return (int)w->id;
        }
    }
    return -1;
}

int gui_hit_test_close(int16_t mx, int16_t my) {
    for (int i = win_count - 1; i >= 0; i--) {
        window_t *w = &windows[i];
        if (!(w->flags & WINDOW_FLAG_VISIBLE)) continue;
        int16_t cx = (int16_t)(w->x + (int16_t)w->width - CLOSE_BTN_SIZE - 2);
        int16_t cy = (int16_t)(w->y + 2);
        if (mx >= cx && mx < cx + CLOSE_BTN_SIZE &&
            my >= cy && my < cy + CLOSE_BTN_SIZE) {
            return (int)w->id;
        }
    }
    return -1;
}

int gui_hit_test_window(int16_t mx, int16_t my) {
    for (int i = win_count - 1; i >= 0; i--) {
        window_t *w = &windows[i];
        if (!(w->flags & WINDOW_FLAG_VISIBLE)) continue;
        if (mx >= w->x && mx < w->x + (int16_t)w->width &&
            my >= w->y && my < w->y + (int16_t)w->height) {
            return (int)w->id;
        }
    }
    return -1;
}

/* ── Input handling ───────────────────────────────────────────────── */

void gui_handle_mouse(int16_t mx, int16_t my, uint8_t buttons,
                      uint8_t prev_buttons) {
    bool pressed  = (buttons & 0x01) && !(prev_buttons & 0x01);
    bool released = !(buttons & 0x01) && (prev_buttons & 0x01);

    /* Dragging in progress */
    if (drag.dragging) {
        if (released) {
            drag.dragging = false;
            drag.window_id = -1;
        } else {
            window_t *w = gui_get_window(drag.window_id);
            if (w) {
                w->x = (int16_t)(mx - drag.drag_offset_x);
                w->y = (int16_t)(my - drag.drag_offset_y);
                /* Clamp so window stays partially on screen */
                if (w->x < (int16_t)(-(int16_t)w->width + 20))
                    w->x = (int16_t)(-(int16_t)w->width + 20);
                if (w->y < 0) w->y = 0;
                if (w->x > VGA_GFX_WIDTH - 20)
                    w->x = (int16_t)(VGA_GFX_WIDTH - 20);
                if (w->y > VGA_GFX_HEIGHT - 20)
                    w->y = (int16_t)(VGA_GFX_HEIGHT - 20);
                w->flags |= WINDOW_FLAG_DIRTY;
            }
        }
        return;
    }

    /* New press */
    if (pressed) {
        /* Check close button first */
        int close_id = gui_hit_test_close(mx, my);
        if (close_id >= 0) {
            gui_destroy_window(close_id);
            return;
        }

        /* Check title bar for drag */
        int title_id = gui_hit_test_titlebar(mx, my);
        if (title_id >= 0) {
            gui_set_focus(title_id);
            window_t *w = gui_get_window(title_id);
            if (w) {
                drag.dragging = true;
                drag.window_id = title_id;
                drag.drag_offset_x = (int16_t)(mx - w->x);
                drag.drag_offset_y = (int16_t)(my - w->y);
            }
            return;
        }

        /* Click on window body -> focus */
        int body_id = gui_hit_test_window(mx, my);
        if (body_id >= 0) {
            gui_set_focus(body_id);
        }
    }
}

void gui_handle_key(uint8_t scancode, char character) {
    window_t *focused = gui_get_focused_window();
    if (!focused) return;

    /* Forward to application via redraw callback convention:
     * The desktop/terminal layer handles this externally. */
    (void)scancode;
    (void)character;

    /* Mark dirty so the window redraws (app may have changed state) */
    focused->flags |= WINDOW_FLAG_DIRTY;
}
