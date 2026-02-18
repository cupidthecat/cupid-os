/**
 * ui.c — Lightweight UI widget toolkit for cupid-os
 *
 * Builds on top of the raw graphics primitives in graphics.h to
 * provide composite, self-contained widgets (buttons, labels, panels,
 * text fields, scrollbars) that handle their own text centering,
 * padding, and 3D effects.
 */

#include "ui.h"
#include "graphics.h"
#include "font_8x8.h"
#include "string.h"
#include "../drivers/vga.h"

/*
 *  Constructors / layout helpers
 */

ui_rect_t ui_rect(int16_t x, int16_t y, uint16_t w, uint16_t h) {
    ui_rect_t r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

ui_rect_t ui_pad(ui_rect_t r, int16_t pad) {
    ui_rect_t o;
    o.x = (int16_t)(r.x + pad);
    o.y = (int16_t)(r.y + pad);
    o.w = (r.w > (uint16_t)(pad * 2)) ? (uint16_t)(r.w - (uint16_t)(pad * 2)) : 0;
    o.h = (r.h > (uint16_t)(pad * 2)) ? (uint16_t)(r.h - (uint16_t)(pad * 2)) : 0;
    return o;
}

ui_rect_t ui_pad_xy(ui_rect_t r, int16_t px, int16_t py) {
    ui_rect_t o;
    o.x = (int16_t)(r.x + px);
    o.y = (int16_t)(r.y + py);
    o.w = (r.w > (uint16_t)(px * 2)) ? (uint16_t)(r.w - (uint16_t)(px * 2)) : 0;
    o.h = (r.h > (uint16_t)(py * 2)) ? (uint16_t)(r.h - (uint16_t)(py * 2)) : 0;
    return o;
}

ui_rect_t ui_center(ui_rect_t outer, uint16_t cw, uint16_t ch) {
    ui_rect_t o;
    o.x = (int16_t)(outer.x + (int16_t)((outer.w - cw) / 2));
    o.y = (int16_t)(outer.y + (int16_t)((outer.h - ch) / 2));
    o.w = cw;
    o.h = ch;
    return o;
}

ui_rect_t ui_cut_top(ui_rect_t *r, uint16_t height) {
    ui_rect_t slice;
    slice.x = r->x;
    slice.y = r->y;
    slice.w = r->w;
    slice.h = (height > r->h) ? r->h : height;
    r->y = (int16_t)(r->y + (int16_t)slice.h);
    r->h = (uint16_t)(r->h - slice.h);
    return slice;
}

ui_rect_t ui_cut_bottom(ui_rect_t *r, uint16_t height) {
    ui_rect_t slice;
    uint16_t sh = (height > r->h) ? r->h : height;
    slice.x = r->x;
    slice.y = (int16_t)(r->y + (int16_t)(r->h - sh));
    slice.w = r->w;
    slice.h = sh;
    r->h = (uint16_t)(r->h - sh);
    return slice;
}

ui_rect_t ui_cut_left(ui_rect_t *r, uint16_t width) {
    ui_rect_t slice;
    uint16_t sw = (width > r->w) ? r->w : width;
    slice.x = r->x;
    slice.y = r->y;
    slice.w = sw;
    slice.h = r->h;
    r->x = (int16_t)(r->x + (int16_t)sw);
    r->w = (uint16_t)(r->w - sw);
    return slice;
}

ui_rect_t ui_cut_right(ui_rect_t *r, uint16_t width) {
    ui_rect_t slice;
    uint16_t sw = (width > r->w) ? r->w : width;
    slice.x = (int16_t)(r->x + (int16_t)(r->w - sw));
    slice.y = r->y;
    slice.w = sw;
    slice.h = r->h;
    r->w = (uint16_t)(r->w - sw);
    return slice;
}

/* 
 *  Hit testing
 */

bool ui_contains(ui_rect_t r, int16_t px, int16_t py) {
    return px >= r.x && px < (int16_t)(r.x + (int16_t)r.w) &&
           py >= r.y && py < (int16_t)(r.y + (int16_t)r.h);
}

/* 
 *  Drawing: low-level
 */

void ui_draw_shadow(ui_rect_t r, uint32_t color, int16_t offset) {
    gfx_fill_rect((int16_t)(r.x + offset), (int16_t)(r.y + offset),
                  r.w, r.h, color);
}

void ui_draw_panel(ui_rect_t r, uint32_t bg, bool border_3d, bool raised) {
    gfx_fill_rect(r.x, r.y, r.w, r.h, bg);
    if (border_3d) {
        gfx_draw_3d_rect(r.x, r.y, r.w, r.h, raised);
    }
}

/* 
 *  Drawing: composite widgets
  */

/* Button */

void ui_draw_button(ui_rect_t r, const char *label, bool focused) {
    /* Background + 3D raised edge */
    ui_draw_panel(r, COLOR_WINDOW_BG, true, true);

    /* Focus ring (black outline) */
    if (focused) {
        gfx_draw_rect((int16_t)(r.x - 1), (int16_t)(r.y - 1),
                      (uint16_t)(r.w + 2), (uint16_t)(r.h + 2),
                      COLOR_BLACK);
    }

    /* Auto-center label text */
    uint16_t tw = gfx_text_width(label);
    int16_t tx = (int16_t)(r.x + (int16_t)((r.w - tw) / 2));
    int16_t ty = (int16_t)(r.y + (int16_t)((r.h - (uint16_t)FONT_H) / 2));
    gfx_draw_text(tx, ty, label, COLOR_BLACK);
}

/* Label */

void ui_draw_label(ui_rect_t r, const char *text, uint32_t color,
                   ui_align_t align) {
    uint16_t tw = gfx_text_width(text);
    int16_t tx;

    switch (align) {
        case UI_ALIGN_CENTER:
            tx = (int16_t)(r.x + (int16_t)((r.w - tw) / 2));
            break;
        case UI_ALIGN_RIGHT:
            tx = (int16_t)(r.x + (int16_t)r.w - (int16_t)tw);
            break;
        default: /* UI_ALIGN_LEFT */
            tx = r.x;
            break;
    }

    /* Vertically center */
    int16_t ty = (int16_t)(r.y + (int16_t)((r.h - (uint16_t)FONT_H) / 2));
    gfx_draw_text(tx, ty, text, color);
}

/* Text field (sunken input box) */

void ui_draw_textfield(ui_rect_t r, const char *text, int cursor_pos) {
    /* Sunken background */
    ui_draw_panel(r, COLOR_TEXT_LIGHT, true, false);

    /* Text with 2px left padding, vertically centered (clipped to box width) */
    int16_t tx = (int16_t)(r.x + 2);
    int16_t ty = (int16_t)(r.y + (int16_t)((r.h - (uint16_t)FONT_H) / 2));
    int max_chars = ((int)r.w - 4) / FONT_W;
    if (max_chars < 1)
        max_chars = 1;

    char visible[128];
    int vis_len = 0;
    int text_len = (int)strlen(text);
    int start = 0;

    if (text_len > max_chars) {
        if (max_chars >= 4) {
            visible[0] = '.';
            visible[1] = '.';
            visible[2] = '.';
            start = text_len - (max_chars - 3);
            if (start < 0)
                start = 0;
            vis_len = 3;
            while (text[start] && vis_len < max_chars && vis_len < 127) {
                visible[vis_len++] = text[start++];
            }
        } else {
            start = text_len - max_chars;
            if (start < 0)
                start = 0;
            while (text[start] && vis_len < max_chars && vis_len < 127) {
                visible[vis_len++] = text[start++];
            }
        }
    } else {
        while (text[vis_len] && vis_len < max_chars && vis_len < 127)
            vis_len++;
        for (int i = 0; i < vis_len; i++)
            visible[i] = text[i];
    }
    visible[vis_len] = '\0';

    gfx_draw_text(tx, ty, visible, COLOR_BLACK);

    /* Blinking cursor */
    if (cursor_pos >= 0) {
        int rel_cursor = cursor_pos - start;
        if (text_len > max_chars && max_chars >= 4)
            rel_cursor += 3;
        if (rel_cursor < 0)
            rel_cursor = 0;
        if (rel_cursor > vis_len)
            rel_cursor = vis_len;
        int16_t cx = (int16_t)(tx + (int16_t)(rel_cursor * FONT_W));
        int16_t max_x = (int16_t)(r.x + (int16_t)r.w - 2);
        if (cx < max_x) {
            gfx_draw_vline(cx, (int16_t)(r.y + 2),
                           (uint16_t)(r.h - 4), COLOR_BLACK);
        }
    }
}

/* Title bar */

void ui_draw_titlebar(ui_rect_t r, const char *title, bool focused) {
    uint32_t bg = focused ? COLOR_TITLEBAR : COLOR_TITLE_UNFOC;
    gfx_fill_rect(r.x, r.y, r.w, r.h, bg);

    /* Left-aligned text with 3px padding, vertically centered */
    int16_t tx = (int16_t)(r.x + 3);
    int16_t ty = (int16_t)(r.y + (int16_t)((r.h - (uint16_t)FONT_H) / 2));
    gfx_draw_text(tx, ty, title, COLOR_TEXT_LIGHT);
}

/* Vertical scrollbar */

void ui_draw_vscrollbar(ui_rect_t r, int total, int visible, int offset) {
    int rw = (int)r.w;
    int rh = (int)r.h;

    /* Track background */
    gfx_fill_rect(r.x, r.y, r.w, r.h, COLOR_BORDER);

    /* Up arrow button */
    ui_rect_t up_btn = ui_rect(r.x, r.y, r.w, r.w);
    ui_draw_panel(up_btn, COLOR_WINDOW_BG, true, true);
    gfx_draw_char((int16_t)(r.x + 2), (int16_t)(r.y + 2), '^', COLOR_BLACK);

    /* Down arrow button */
    ui_rect_t dn_btn = ui_rect(r.x,
                               (int16_t)(r.y + (int16_t)r.h - (int16_t)r.w),
                               r.w, r.w);
    ui_draw_panel(dn_btn, COLOR_WINDOW_BG, true, true);
    gfx_draw_char((int16_t)(dn_btn.x + 2), (int16_t)(dn_btn.y + 2),
                  'v', COLOR_BLACK);

    /* Thumb (only if content overflows) */
    int track_h = rh - 2 * rw;
    if (track_h > 4 && total > visible && visible > 0) {
        int thumb_h = (track_h * visible) / total;
        if (thumb_h < 8) thumb_h = 8;
        if (thumb_h > track_h) thumb_h = track_h;

        int thumb_max = track_h - thumb_h;
        int thumb_off = 0;
        int max_scroll = total - visible;
        if (max_scroll > 0) {
            thumb_off = (offset * thumb_max) / max_scroll;
        }

        int16_t thumb_y = (int16_t)(r.y + (int16_t)r.w + (int16_t)thumb_off);
        ui_rect_t thumb = ui_rect((int16_t)(r.x + 1), thumb_y,
                                  (uint16_t)(rw - 2), (uint16_t)thumb_h);
        ui_draw_panel(thumb, COLOR_WINDOW_BG, true, true);
    }
}

/* Vertical scrollbar hit test */

int ui_vscrollbar_hit(ui_rect_t r, int16_t mx, int16_t my, bool *page) {
    if (!ui_contains(r, mx, my)) return 0;

    if (page) *page = false;

    int rw = (int)r.w;
    int ry = (int)r.y;
    int rh = (int)r.h;

    /* Up arrow area */
    if (my < (int16_t)(ry + rw)) {
        return -1;
    }

    /* Down arrow area */
    if (my >= (int16_t)(ry + rh - rw)) {
        return 1;
    }

    /* Track area — page scroll */
    if (page) *page = true;
    int mid = ry + rh / 2;
    return (my < (int16_t)mid) ? -1 : 1;
}
