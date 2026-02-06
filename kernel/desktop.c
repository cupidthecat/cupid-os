/**
 * desktop.c - Desktop shell for cupid-os
 *
 * Implements the desktop background, taskbar, icons, and the
 * main event loop that drives the graphical environment.
 */

#include "desktop.h"
#include "kernel.h"
#include "gui.h"
#include "graphics.h"
#include "string.h"
#include "../drivers/vga.h"
#include "../drivers/mouse.h"
#include "../drivers/keyboard.h"
#include "../drivers/serial.h"
#include "terminal_app.h"
#include "notepad.h"

/* ── Icon storage ─────────────────────────────────────────────────── */
static desktop_icon_t icons[MAX_DESKTOP_ICONS];
static int icon_count = 0;

/* ── Init ─────────────────────────────────────────────────────────── */

void desktop_init(void) {
    icon_count = 0;
    memset(icons, 0, sizeof(icons));
    KINFO("Desktop initialized");
}

/* ── Icons ────────────────────────────────────────────────────────── */

void desktop_add_icon(int16_t x, int16_t y, const char *label,
                      void (*launch)(void)) {
    if (icon_count >= MAX_DESKTOP_ICONS) return;
    desktop_icon_t *ic = &icons[icon_count++];
    ic->x = x;
    ic->y = y;
    ic->launch = launch;
    ic->active = true;

    int i = 0;
    while (label[i] && i < 31) { ic->label[i] = label[i]; i++; }
    ic->label[i] = '\0';
}

/* ── Drawing ──────────────────────────────────────────────────────── */

void desktop_draw_background(void) {
    /* Fill desktop area (above taskbar) */
    gfx_fill_rect(0, 0, VGA_GFX_WIDTH, TASKBAR_Y, COLOR_DESKTOP_BG);
}

void desktop_draw_taskbar(void) {
    /* Taskbar background */
    gfx_fill_rect(0, TASKBAR_Y, VGA_GFX_WIDTH, TASKBAR_HEIGHT, COLOR_TASKBAR);

    /* Top separator line */
    gfx_draw_hline(0, TASKBAR_Y, VGA_GFX_WIDTH, COLOR_BORDER);

    /* "cupid-os" branding on the left */
    gfx_draw_text(4, (int16_t)(TASKBAR_Y + 6), "cupid-os", COLOR_TEXT_LIGHT);

    /* Window buttons starting at x=80 */
    int wc = gui_window_count();
    for (int i = 0; i < wc && i < MAX_WINDOWS; i++) {
        /* We access windows via ID by iterating; use gui_get_window with
         * a scan approach since we don't have direct index access.
         * For simplicity, iterate through possible window IDs. */
        /* Actually, just draw a button per visible window by checking
         * the window array position. We'll use a helper approach. */
        break; /* Filled in the loop below */
    }

    /* Simple approach: iterate through the window array by index.
     * gui module stores windows internally; we re-check by calling
     * gui_get_window with each potential ID (1..next_id). This is
     * not ideal but works for 16 windows max. */
    /* Better: expose a simple iteration.  For now, we count and draw. */
    (void)wc;

    /* Draw a button for each window ID we can find */
    for (int idx = 0; idx < MAX_WINDOWS; idx++) {
        /* Try to find window at z-order position idx */
        /* We'll just scan by trying IDs 1..next_id */
        /* Simplification: we draw buttons as the desktop loop
         * knows the windows.  We use gui_window_count and
         * access via offset. */
        break;
    }

    /* Simplified taskbar buttons: just enumerate windows */
    {
        int16_t btn_x = 80;
        /* We'll try IDs 1..64 (more than enough) */
        for (uint32_t id = 1; id < 64 && btn_x < VGA_GFX_WIDTH - 10; id++) {
            window_t *w = gui_get_window((int)id);
            if (!w) continue;
            if (!(w->flags & WINDOW_FLAG_VISIBLE)) continue;

            uint16_t btn_w = gfx_text_width(w->title);
            if (btn_w < 40) btn_w = 40;
            btn_w = (uint16_t)(btn_w + 8);

            uint8_t bg = (w->flags & WINDOW_FLAG_FOCUSED) ? COLOR_TASKBAR_ACT : COLOR_TASKBAR;
            gfx_fill_rect(btn_x, (int16_t)(TASKBAR_Y + 2),
                          btn_w, (uint16_t)(TASKBAR_HEIGHT - 4), bg);
            gfx_draw_rect(btn_x, (int16_t)(TASKBAR_Y + 2),
                          btn_w, (uint16_t)(TASKBAR_HEIGHT - 4), COLOR_BORDER);
            gfx_draw_text((int16_t)(btn_x + 4), (int16_t)(TASKBAR_Y + 6),
                          w->title, COLOR_TEXT_LIGHT);
            btn_x = (int16_t)(btn_x + (int16_t)btn_w + 2);
        }
    }
}

void desktop_draw_icons(void) {
    for (int i = 0; i < icon_count; i++) {
        desktop_icon_t *ic = &icons[i];
        if (!ic->active) continue;

        /* Check if this is the Notepad icon */
        if (ic->label[0] == 'N' && ic->label[1] == 'o' &&
            ic->label[2] == 't' && ic->label[3] == 'e') {
            /* Notepad icon: spiral-bound notebook */
            /* Page background */
            gfx_fill_rect(ic->x, ic->y, 32, 24, COLOR_TEXT_LIGHT);
            gfx_draw_rect(ic->x, ic->y, 32, 24, COLOR_BLACK);

            /* Spiral binding strip on left */
            gfx_fill_rect(ic->x, ic->y, 6, 24, COLOR_BORDER);
            /* Spiral coils */
            for (int cy = 3; cy < 22; cy += 5) {
                gfx_fill_rect((int16_t)(ic->x + 1), (int16_t)(ic->y + cy),
                              4, 3, COLOR_TEXT_LIGHT);
                gfx_draw_rect((int16_t)(ic->x + 1), (int16_t)(ic->y + cy),
                              4, 3, COLOR_TEXT);
            }

            /* Ruled lines on the page */
            for (int ly = 8; ly < 22; ly += 5) {
                gfx_draw_hline((int16_t)(ic->x + 8), (int16_t)(ic->y + ly),
                               20, COLOR_TEXT);
            }

            /* 3D effect: light top-left, dark bottom-right */
            gfx_draw_hline(ic->x, ic->y, 32, COLOR_TEXT_LIGHT);
            gfx_draw_vline(ic->x, ic->y, 24, COLOR_TEXT_LIGHT);
            gfx_draw_hline(ic->x, (int16_t)(ic->y + 23), 32, COLOR_TEXT);
            gfx_draw_vline((int16_t)(ic->x + 31), ic->y, 24, COLOR_TEXT);
        } else {
            /* Default icon (terminal): filled rectangle with symbol */
            gfx_fill_rect(ic->x, ic->y, 32, 24, COLOR_BUTTON);
            gfx_draw_rect(ic->x, ic->y, 32, 24, COLOR_BORDER);
            /* Draw ">_" inside to represent terminal */
            gfx_draw_text((int16_t)(ic->x + 4), (int16_t)(ic->y + 8),
                          ">_", COLOR_TEXT_LIGHT);
        }

        /* Label below icon — left-aligned to icon edge */
        int16_t lx = ic->x;
        gfx_draw_text(lx, (int16_t)(ic->y + 28), ic->label, COLOR_TEXT);
    }
}

/* ── Taskbar hit-test ─────────────────────────────────────────────── */

int desktop_hit_test_taskbar(int16_t mx, int16_t my) {
    if (my < TASKBAR_Y + 2 || my >= TASKBAR_Y + TASKBAR_HEIGHT) return -1;

    int16_t btn_x = 80;
    for (uint32_t id = 1; id < 64 && btn_x < VGA_GFX_WIDTH - 10; id++) {
        window_t *w = gui_get_window((int)id);
        if (!w) continue;
        if (!(w->flags & WINDOW_FLAG_VISIBLE)) continue;

        uint16_t btn_w = gfx_text_width(w->title);
        if (btn_w < 40) btn_w = 40;
        btn_w = (uint16_t)(btn_w + 8);

        if (mx >= btn_x && mx < btn_x + (int16_t)btn_w) {
            return (int)w->id;
        }
        btn_x = (int16_t)(btn_x + (int16_t)btn_w + 2);
    }
    return -1;
}

/* ── Icon hit-test ────────────────────────────────────────────────── */

static int hit_test_icon(int16_t mx, int16_t my) {
    for (int i = 0; i < icon_count; i++) {
        desktop_icon_t *ic = &icons[i];
        if (!ic->active) continue;
        if (mx >= ic->x && mx < ic->x + 32 &&
            my >= ic->y && my < ic->y + 24) {
            return i;
        }
    }
    return -1;
}

/* ── Main event loop ──────────────────────────────────────────────── */

void desktop_redraw_cycle(void) {
    bool needs_redraw = false;

    /* Process mouse */
    if (mouse.updated) {
        mouse.updated = false;
        needs_redraw = true;

        if (mouse.scroll_z != 0) {
            int scroll_lines = (int)mouse.scroll_z * -5;
            /* Route scroll to focused window */
            int np_wid = notepad_get_wid();
            window_t *np_win = np_wid >= 0 ? gui_get_window(np_wid) : NULL;
            if (np_win && (np_win->flags & WINDOW_FLAG_FOCUSED)) {
                notepad_handle_scroll(scroll_lines);
            } else {
                terminal_handle_scroll(scroll_lines);
            }
            mouse.scroll_z = 0;
        }

        uint8_t btn = mouse.buttons;
        uint8_t prev = mouse.prev_buttons;
        (void)btn; (void)prev;
        /* Don't process clicks during blocking command */
    }

    /* Cursor blink */
    terminal_tick();
    notepad_tick();

    /* Redraw if needed */
    if (needs_redraw || gui_any_dirty()) {
        desktop_draw_background();
        desktop_draw_icons();
        gui_draw_all_windows();
        desktop_draw_taskbar();

        mouse_save_under_cursor();
        mouse_draw_cursor();

        vga_flip();
    }
}

void desktop_run(void) {
    bool needs_redraw = true;

    while (1) {
        /* ── Process mouse ──────────────────────────────────────── */
        if (mouse.updated) {
            mouse.updated = false;
            needs_redraw = true;

            /* Handle scroll wheel – consume accumulated delta */
            if (mouse.scroll_z != 0) {
                /* Each scroll notch is ±1 in scroll_z.
                 * Multiply by 5 lines per notch for snappy scrolling.
                 * Negative scroll_z = scroll up (show older content). */
                int scroll_lines = (int)mouse.scroll_z * -5;
                /* Route scroll to focused window */
                int np_wid = notepad_get_wid();
                window_t *np_win = np_wid >= 0 ? gui_get_window(np_wid) : NULL;
                if (np_win && (np_win->flags & WINDOW_FLAG_FOCUSED)) {
                    notepad_handle_scroll(scroll_lines);
                } else {
                    terminal_handle_scroll(scroll_lines);
                }
                mouse.scroll_z = 0;
            }

            uint8_t btn = mouse.buttons;
            uint8_t prev = mouse.prev_buttons;
            bool pressed = (btn & 0x01) && !(prev & 0x01);

            /* Check taskbar clicks first */
            if (pressed && mouse.y >= TASKBAR_Y) {
                int tb_id = desktop_hit_test_taskbar(mouse.x, mouse.y);
                if (tb_id >= 0) {
                    gui_set_focus(tb_id);
                } else {
                    /* Possibly clicked on empty taskbar area - ignore */
                }
            }
            /* Check icon clicks */
            else if (pressed && gui_hit_test_window(mouse.x, mouse.y) < 0) {
                int ic_idx = hit_test_icon(mouse.x, mouse.y);
                if (ic_idx >= 0 && icons[ic_idx].launch) {
                    icons[ic_idx].launch();
                }
            }
            /* Forward to GUI window manager */
            else {
                gui_handle_mouse(mouse.x, mouse.y, btn, prev);
                /* Also forward to notepad if its window is focused */
                int np_wid = notepad_get_wid();
                window_t *np_win = np_wid >= 0 ? gui_get_window(np_wid) : NULL;
                if (np_win && (np_win->flags & WINDOW_FLAG_FOCUSED)) {
                    notepad_handle_mouse(mouse.x, mouse.y, btn, prev);
                }
            }
        }

        /* ── Process keyboard ───────────────────────────────────── */
        {
            key_event_t event;
            while (keyboard_read_event(&event)) {
                /* Route key to focused window */
                int np_wid = notepad_get_wid();
                window_t *np_win = np_wid >= 0 ? gui_get_window(np_wid) : NULL;
                if (np_win && (np_win->flags & WINDOW_FLAG_FOCUSED)) {
                    notepad_handle_key(event.scancode, event.character);
                } else {
                    terminal_handle_key(event.scancode, event.character);
                }
                needs_redraw = true;
            }
        }

        /* ── Cursor blink tick ──────────────────────────────────── */
        terminal_tick();
        notepad_tick();

        /* ── Redraw ─────────────────────────────────────────────── */
        if (needs_redraw || gui_any_dirty()) {
            desktop_draw_background();
            desktop_draw_icons();
            gui_draw_all_windows();
            desktop_draw_taskbar();

            /* Draw mouse cursor on top */
            mouse_save_under_cursor();
            mouse_draw_cursor();

            vga_flip();
            needs_redraw = false;
        }

        /* Check for deferred reschedule (preemptive time slice) */
        kernel_check_reschedule();

        /* Yield CPU until next interrupt */
        __asm__ volatile("hlt");
    }
}
