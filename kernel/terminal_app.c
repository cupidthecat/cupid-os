/**
 * terminal_app.c - GUI terminal application for cupid-os
 *
 * Provides a graphical terminal window that interfaces with
 * the existing shell.  The shell writes to a character buffer
 * and the terminal renders it inside a GUI window.
 */

#include "terminal_app.h"
#include "gui.h"
#include "graphics.h"
#include "font_8x8.h"
#include "shell.h"
#include "string.h"
#include "process.h"
#include "kernel.h"
#include "../drivers/vga.h"
#include "../drivers/serial.h"
#include "../drivers/timer.h"

#define TERM_WIN_W 310
#define TERM_WIN_H 168

#define CURSOR_BLINK_MS 500   /* Toggle cursor every 500 ms */

/* ── Terminal state ───────────────────────────────────────────────── */
static int terminal_wid = -1;  /* Window ID of the terminal */
static int terminal_scroll_offset = 0;  /* Manual scroll offset (lines from bottom) */
static bool cursor_visible = true;      /* Current blink state */
static uint32_t last_blink_ms = 0;      /* Last time cursor toggled */
static uint32_t terminal_pid = 0;       /* PID of the terminal process */

/* ── Launch ───────────────────────────────────────────────────────── */

/* Terminal process entry point — runs as its own process */
static void terminal_process_entry(void) {
    /* This process stays alive as long as the terminal window exists.
     * The actual key handling is event-driven via the desktop loop
     * calling terminal_handle_key().  This process just keeps the
     * terminal alive in the process table and yields its time slice. */
    while (1) {
        /* Check if our window was closed */
        if (terminal_wid < 0 || !gui_get_window(terminal_wid)) {
            terminal_wid = -1;
            terminal_pid = 0;
            break;
        }

        /* Perform deferred reschedule check */
        kernel_check_reschedule();

        /* Yield until next time slice */
        process_yield();
    }
    /* Falls through to process_exit_trampoline */
}

/* ── Launch ───────────────────────────────────────────────────────── */

void terminal_launch(void) {
    /* Don't open a second terminal if one already exists */
    if (terminal_wid >= 0 && gui_get_window(terminal_wid)) return;

    terminal_wid = gui_create_window(5, 10, TERM_WIN_W, TERM_WIN_H, "Terminal");
    if (terminal_wid < 0) {
        KERROR("terminal_launch: failed to create window");
        return;
    }

    window_t *win = gui_get_window(terminal_wid);
    if (win) {
        win->redraw = terminal_redraw;
    }

    /* Compute visible columns from window width and tell the shell */
    {
        uint16_t content_w = (uint16_t)(TERM_WIN_W - 4);
        int vis_cols = (int)content_w / FONT_W;
        if (vis_cols > SHELL_COLS) vis_cols = SHELL_COLS;
        shell_set_visible_cols(vis_cols);
    }

    shell_set_output_mode(SHELL_OUTPUT_GUI);

    gui_set_focus(terminal_wid);

    /* Spawn the terminal as its own process */
    terminal_pid = process_create(terminal_process_entry, "terminal",
                                  DEFAULT_STACK_SIZE);
    if (terminal_pid == 0) {
        KWARN("terminal_launch: failed to create terminal process");
    }

    KINFO("Terminal launched (wid=%d, pid=%u)", terminal_wid, terminal_pid);
}

/* ── Redraw callback ──────────────────────────────────────────────── */

void terminal_redraw(window_t *win) {
    int16_t content_x = (int16_t)(win->x + 2);
    int16_t content_y = (int16_t)(win->y + TITLEBAR_H + 1);
    uint16_t content_w = (uint16_t)(win->width - 4);
    uint16_t content_h = (uint16_t)(win->height - TITLEBAR_H - 2);

    /* Dark terminal background */
    gfx_fill_rect(content_x, content_y, content_w, content_h, COLOR_TERM_BG);

    /* Calculate grid dimensions – only count rows that fit entirely */
    int chars_per_row = (int)content_w / FONT_W;
    int visible_rows  = (int)content_h / FONT_H;
    if (chars_per_row > SHELL_COLS) chars_per_row = SHELL_COLS;
    if (visible_rows > SHELL_ROWS) visible_rows = SHELL_ROWS;
    if (visible_rows < 1) visible_rows = 1;

    /* Get shell buffer */
    const char *buf = shell_get_buffer();
    int scx = shell_get_cursor_x();
    int scy = shell_get_cursor_y();

    /* Determine scroll: auto-follow cursor to keep it visible */
    int scroll_row = 0;
    if (scy >= visible_rows) {
        scroll_row = scy - visible_rows + 1;
    }

    /* Apply manual scroll offset (user scrolling with mouse wheel) */
    scroll_row -= terminal_scroll_offset;
    if (scroll_row < 0) scroll_row = 0;
    /* Don't scroll past the cursor row */
    if (scroll_row > scy) scroll_row = scy;

    /* Render characters – clip to content area */
    for (int row = 0; row < visible_rows; row++) {
        int src_row = row + scroll_row;
        if (src_row >= SHELL_ROWS) break;
        int16_t py = (int16_t)(content_y + row * FONT_H);
        /* Skip row if it would extend past content area */
        if (py + FONT_H > content_y + (int16_t)content_h) break;
        for (int col = 0; col < chars_per_row; col++) {
            char c = buf[src_row * SHELL_COLS + col];
            if (c && c != ' ') {
                int16_t px = (int16_t)(content_x + col * FONT_W);
                gfx_draw_char(px, py, c, COLOR_TEXT_LIGHT);
            }
        }
    }

    /* Draw blinking cursor – only when visible and fits in content area */
    if (cursor_visible) {
        int cursor_screen_row = scy - scroll_row;
        if (cursor_screen_row >= 0 && cursor_screen_row < visible_rows) {
            int16_t cx = (int16_t)(content_x + scx * FONT_W);
            int16_t cy_top = (int16_t)(content_y + cursor_screen_row * FONT_H);
            int16_t cy_bot = (int16_t)(cy_top + FONT_H - 1);
            if (cy_bot < content_y + (int16_t)content_h) {
                /* Draw a thin vertical bar cursor */
                gfx_draw_vline(cx, cy_top, (uint16_t)FONT_H, COLOR_CURSOR);
            }
        }
    }
}

/* ── Key forwarding ───────────────────────────────────────────────── */

void terminal_handle_key(uint8_t scancode, char character) {
    /* Only process if terminal window exists and is focused */
    if (terminal_wid < 0) return;
    window_t *win = gui_get_window(terminal_wid);
    if (!win) return;
    if (!(win->flags & WINDOW_FLAG_FOCUSED)) return;

    /* Page Up/Down for scrolling */
    #define SCANCODE_PAGE_UP   0x49
    #define SCANCODE_PAGE_DOWN 0x51

    if (character == 0 && scancode == SCANCODE_PAGE_UP) {
        terminal_scroll_offset += 5;  /* Scroll up 5 lines */
        if (terminal_scroll_offset > SHELL_ROWS - 10) {
            terminal_scroll_offset = SHELL_ROWS - 10;
        }
        win->flags |= WINDOW_FLAG_DIRTY;
        return;
    }
    if (character == 0 && scancode == SCANCODE_PAGE_DOWN) {
        terminal_scroll_offset -= 5;  /* Scroll down 5 lines */
        if (terminal_scroll_offset < 0) {
            terminal_scroll_offset = 0;
        }
        win->flags |= WINDOW_FLAG_DIRTY;
        return;
    }

    /* Any other key resets scroll to bottom */
    if (character != 0 || (scancode != SCANCODE_PAGE_UP && scancode != SCANCODE_PAGE_DOWN)) {
        terminal_scroll_offset = 0;
    }

    /* Reset cursor blink so it's visible right after typing */
    cursor_visible = true;
    last_blink_ms = timer_get_uptime_ms();

    shell_gui_handle_key(scancode, character);
    win->flags |= WINDOW_FLAG_DIRTY;
}

/* ── Cursor blink tick (called from desktop loop) ─────────────────── */

void terminal_mark_dirty(void) {
    if (terminal_wid < 0) return;
    window_t *win = gui_get_window(terminal_wid);
    if (win) {
        win->flags |= WINDOW_FLAG_DIRTY;
    }
}

void terminal_tick(void) {
    if (terminal_wid < 0) return;
    window_t *win = gui_get_window(terminal_wid);
    if (!win) return;
    if (!(win->flags & WINDOW_FLAG_FOCUSED)) return;

    uint32_t now = timer_get_uptime_ms();
    if (now - last_blink_ms >= CURSOR_BLINK_MS) {
        cursor_visible = !cursor_visible;
        last_blink_ms = now;
        win->flags |= WINDOW_FLAG_DIRTY;
    }
}

/* ── Mouse wheel scroll ───────────────────────────────────────────── */

void terminal_handle_scroll(int delta) {
    if (terminal_wid < 0) return;
    window_t *win = gui_get_window(terminal_wid);
    if (!win) return;
    if (!(win->flags & WINDOW_FLAG_FOCUSED)) return;

    terminal_scroll_offset += delta;

    /* Clamp to valid range */
    int max_scroll = shell_get_cursor_y();
    if (terminal_scroll_offset > max_scroll) {
        terminal_scroll_offset = max_scroll;
    }
    if (terminal_scroll_offset < 0) {
        terminal_scroll_offset = 0;
    }

    win->flags |= WINDOW_FLAG_DIRTY;
}
