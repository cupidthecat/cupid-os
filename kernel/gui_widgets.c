/**
 * gui_widgets.c - Essential GUI Controls for cupid-os
 *
 * Stateless rendering functions for checkboxes, radio buttons,
 * dropdowns, list boxes, sliders, progress bars, spinners,
 * and toggle switches.
 *
 * All drawing goes through the gfx2d/graphics primitives so
 * clipping and surfaces are respected.
 */

#include "gui_widgets.h"
#include "gfx2d.h"
#include "graphics.h"
#include "font_8x8.h"
#include "string.h"
#include "math.h"
#include "../drivers/vga.h"

/* ── Local helpers ────────────────────────────────────────────────── */

static const uint32_t COL_CHECK_BG   = 0x00FFFFFF;
static const uint32_t COL_CHECK_MARK = 0x00282830;
static const uint32_t COL_DISABLED   = 0x009898A0;
static const uint32_t COL_SLIDER_TRACK = 0x00C0C0C8;
static const uint32_t COL_SLIDER_THUMB = 0x00B8DDFF;
static const uint32_t COL_PROGRESS_BG  = 0x00C0C0C8;
static const uint32_t COL_PROGRESS_BAR = 0x0080C0FF;
static const uint32_t COL_TOGGLE_ON    = 0x0080D080;
static const uint32_t COL_TOGGLE_OFF   = 0x00C0C0C0;

/* ── Small itoa for numbers ────────────────────────────────────── */
static void int_to_str(int v, char *buf, int bufsz) {
    int neg = 0;
    int i = 0;
    int start, end;
    char tmp;

    if (bufsz < 2) { buf[0] = '\0'; return; }
    if (v < 0) { neg = 1; v = -v; }
    if (v == 0) { buf[0] = '0'; buf[1] = '\0'; return; }

    while (v > 0 && i < bufsz - 1) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    if (neg && i < bufsz - 1) buf[i++] = '-';
    buf[i] = '\0';

    /* Reverse */
    start = 0; end = i - 1;
    while (start < end) {
        tmp = buf[start];
        buf[start] = buf[end];
        buf[end] = tmp;
        start++; end--;
    }
}

void gui_widgets_init(void) {
    /* Nothing to initialize */
}

/* ══════════════════════════════════════════════════════════════════════
 *  Checkbox
 * ══════════════════════════════════════════════════════════════════════ */

bool ui_draw_checkbox(ui_rect_t r, const char *label, bool checked,
                      bool enabled, int16_t mx, int16_t my, bool clicked) {
    int box_size = 12;
    int16_t bx = r.x;
    int16_t by = (int16_t)(r.y + (int16_t)((r.h - (uint16_t)box_size) / 2));
    uint32_t fg = enabled ? COLOR_TEXT : COL_DISABLED;
    bool hit = false;

    /* Box background */
    gfx2d_rect_fill(bx, by, box_size, box_size,
                    enabled ? COL_CHECK_BG : 0x00E0E0E0);
    gfx2d_rect(bx, by, box_size, box_size, COLOR_BORDER);

    /* Check mark */
    if (checked) {
        /* Simple 'X' or checkmark using lines */
        gfx2d_line(bx + 2, by + 5, bx + 4, by + 9, COL_CHECK_MARK);
        gfx2d_line(bx + 4, by + 9, bx + 9, by + 2, COL_CHECK_MARK);
        gfx2d_line(bx + 2, by + 6, bx + 4, by + 10, COL_CHECK_MARK);
        gfx2d_line(bx + 4, by + 10, bx + 9, by + 3, COL_CHECK_MARK);
    }

    /* Label */
    if (label) {
        int16_t tx = (int16_t)(bx + (int16_t)box_size + 4);
        int16_t ty = (int16_t)(r.y + (int16_t)((r.h - FONT_H) / 2));
        gfx2d_text(tx, ty, label, fg, GFX2D_FONT_NORMAL);
    }

    /* Hit test */
    if (enabled && clicked && ui_contains(r, mx, my))
        hit = true;

    return hit;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Radio Button
 * ══════════════════════════════════════════════════════════════════════ */

bool ui_draw_radio(ui_rect_t r, const char *label, bool selected,
                   bool enabled, int16_t mx, int16_t my, bool clicked) {
    int radius = 6;
    int16_t cx = (int16_t)(r.x + radius);
    int16_t cy = (int16_t)(r.y + (int16_t)(r.h / 2));
    uint32_t fg = enabled ? COLOR_TEXT : COL_DISABLED;
    bool hit = false;

    /* Outer circle */
    gfx2d_circle_fill(cx, cy, radius,
                      enabled ? COL_CHECK_BG : 0x00E0E0E0);
    gfx2d_circle(cx, cy, radius, COLOR_BORDER);

    /* Inner filled circle when selected */
    if (selected) {
        gfx2d_circle_fill(cx, cy, 3, COL_CHECK_MARK);
    }

    /* Label */
    if (label) {
        int16_t tx = (int16_t)(r.x + radius * 2 + 4);
        int16_t ty = (int16_t)(r.y + (int16_t)((r.h - FONT_H) / 2));
        gfx2d_text(tx, ty, label, fg, GFX2D_FONT_NORMAL);
    }

    if (enabled && clicked && ui_contains(r, mx, my))
        hit = true;

    return hit;
}

int ui_radio_group(ui_rect_t r, const char **labels, int count,
                   int selected, int16_t mx, int16_t my, bool clicked) {
    int i;
    int item_h = (r.h > 0 && count > 0) ? (int)r.h / count : 20;
    int new_selected = -1;

    for (i = 0; i < count; i++) {
        ui_rect_t ir = ui_rect(r.x,
                               (int16_t)(r.y + (int16_t)(i * item_h)),
                               r.w, (uint16_t)item_h);
        if (ui_draw_radio(ir, labels[i], (i == selected), true,
                          mx, my, clicked)) {
            new_selected = i;
        }
    }

    return new_selected;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Dropdown / Combo Box
 * ══════════════════════════════════════════════════════════════════════ */

bool ui_draw_dropdown(ui_rect_t r, const char **items, int count,
                      ui_dropdown_state_t *state, int16_t mx, int16_t my,
                      bool clicked) {
    bool changed = false;
    int16_t arrow_w = 16;
    ui_rect_t btn_area;
    uint32_t sel_text_color = COLOR_TEXT;

    /* Draw the closed dropdown button */
    ui_draw_panel(r, COL_CHECK_BG, true, true);

    /* Selected item text */
    if (state->selected >= 0 && state->selected < count) {
        ui_rect_t text_r = ui_rect(
            (int16_t)(r.x + 3),
            r.y,
            (uint16_t)(r.w - (uint16_t)arrow_w - 4u),
            r.h);
        ui_draw_label(text_r, items[state->selected],
                      sel_text_color, UI_ALIGN_LEFT);
    }

    /* Down arrow */
    btn_area = ui_rect(
        (int16_t)(r.x + (int16_t)r.w - arrow_w),
        r.y, (uint16_t)arrow_w, r.h);
    {
        int16_t ax = (int16_t)(btn_area.x + (int16_t)(arrow_w / 2));
        int16_t ay = (int16_t)(btn_area.y + (int16_t)(r.h / 2));
        gfx2d_line(ax - 3, ay - 1, ax, ay + 2, COLOR_TEXT);
        gfx2d_line(ax, ay + 2, ax + 3, ay - 1, COLOR_TEXT);
    }

    /* Handle click on closed dropdown */
    if (clicked && ui_contains(r, mx, my) && !state->open) {
        state->open = true;
        state->hover_item = -1;
    } else if (state->open) {
        /* Draw dropdown list */
        int item_h = (int)r.h;
        int list_h = item_h * count;
        int16_t list_y = (int16_t)(r.y + (int16_t)r.h);
        ui_rect_t list_r = ui_rect(r.x, list_y, r.w, (uint16_t)list_h);
        int i;

        /* Background */
        gfx2d_rect_fill(list_r.x, list_r.y, (int)list_r.w, list_h,
                        COL_CHECK_BG);
        gfx2d_rect(list_r.x, list_r.y, (int)list_r.w, list_h, COLOR_BORDER);

        /* Track hover */
        state->hover_item = -1;
        if (ui_contains(list_r, mx, my)) {
            state->hover_item =
                (int)(my - list_y) / item_h;
            if (state->hover_item >= count)
                state->hover_item = count - 1;
        }

        /* Draw items */
        for (i = 0; i < count; i++) {
            int16_t iy = (int16_t)(list_y + (int16_t)(i * item_h));
            ui_rect_t ir = ui_rect(r.x, iy, r.w, (uint16_t)item_h);

            if (i == state->hover_item) {
                gfx2d_rect_fill(ir.x + 1, ir.y, (int)ir.w - 2,
                                item_h, COLOR_TITLEBAR);
            }

            ui_rect_t label_r = ui_pad_xy(ir, 3, 0);
            ui_draw_label(label_r, items[i],
                          (i == state->hover_item) ? COLOR_TEXT_LIGHT
                                                   : COLOR_TEXT,
                          UI_ALIGN_LEFT);
        }

        /* Handle click in list */
        if (clicked) {
            if (ui_contains(list_r, mx, my) && state->hover_item >= 0) {
                if (state->hover_item != state->selected) {
                    state->selected = state->hover_item;
                    changed = true;
                }
            }
            state->open = false;
        }
    }

    return changed;
}

/* ══════════════════════════════════════════════════════════════════════
 *  List Box
 * ══════════════════════════════════════════════════════════════════════ */

int ui_listbox_hit(ui_rect_t r, int offset, int item_height, int count,
                   int16_t mx, int16_t my) {
    int idx;
    if (!ui_contains(r, mx, my)) return -1;
    idx = (int)(my - r.y) / item_height + offset;
    if (idx < 0 || idx >= count) return -1;
    return idx;
}

bool ui_draw_listbox(ui_rect_t r, const char **items, int count,
                     ui_listbox_state_t *state, int16_t mx, int16_t my,
                     bool clicked, int scroll_delta) {
    int item_h = FONT_H + 4;
    int visible;
    int i;
    bool changed = false;
    int sb_w = 12;

    if (r.h == 0) return false;
    visible = (int)r.h / item_h;
    if (visible < 1) visible = 1;

    /* Sunken background */
    ui_draw_panel(r, COL_CHECK_BG, true, false);

    /* Handle scroll */
    if (scroll_delta != 0 && ui_contains(r, mx, my)) {
        state->offset += scroll_delta;
        if (state->offset < 0) state->offset = 0;
        if (state->offset > count - visible)
            state->offset = count - visible;
        if (state->offset < 0) state->offset = 0;
    }

    /* Hover tracking */
    state->hover_item = -1;
    if (ui_contains(r, mx, my)) {
        int hit = ui_listbox_hit(r, state->offset, item_h, count,
                                 mx, my);
        state->hover_item = hit;
    }

    /* Draw items */
    for (i = 0; i < visible && (i + state->offset) < count; i++) {
        int idx = i + state->offset;
        int16_t iy = (int16_t)(r.y + (int16_t)(i * item_h));
        ui_rect_t ir = ui_rect((int16_t)(r.x + 1), iy,
                               (uint16_t)(r.w - (uint16_t)sb_w - 2u),
                               (uint16_t)item_h);

        /* Highlight */
        if (idx == state->selected) {
            gfx2d_rect_fill(ir.x, ir.y, (int)ir.w, item_h, COLOR_TITLEBAR);
            ui_draw_label(ir, items[idx], COLOR_TEXT_LIGHT, UI_ALIGN_LEFT);
        } else if (idx == state->hover_item) {
            gfx2d_rect_fill(ir.x, ir.y, (int)ir.w, item_h, COLOR_HIGHLIGHT);
            ui_draw_label(ir, items[idx], COLOR_TEXT, UI_ALIGN_LEFT);
        } else {
            ui_draw_label(ir, items[idx], COLOR_TEXT, UI_ALIGN_LEFT);
        }
    }

    /* Scrollbar */
    {
        ui_rect_t sb = ui_rect(
            (int16_t)(r.x + (int16_t)r.w - (int16_t)sb_w),
            r.y, (uint16_t)sb_w, r.h);
        ui_draw_vscrollbar(sb, count, visible, state->offset);
    }

    /* Handle click */
    if (clicked && state->hover_item >= 0 &&
        state->hover_item != state->selected) {
        state->selected = state->hover_item;
        changed = true;
    }

    return changed;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Slider (Horizontal)
 * ══════════════════════════════════════════════════════════════════════ */

int ui_draw_slider_h(ui_rect_t r, int value, int max, bool dragging,
                     int16_t mx, int16_t my) {
    int track_h = 4;
    int thumb_w = 12;
    int thumb_h;
    int track_y;
    int track_w;
    int thumb_x;
    int new_val = value;

    (void)my;

    if (max < 1) max = 1;
    if (value < 0) value = 0;
    if (value > max) value = max;

    thumb_h = (int)r.h;
    track_y = r.y + (int)r.h / 2 - track_h / 2;
    track_w = (int)r.w - thumb_w;

    /* Track */
    gfx2d_rect_fill(r.x, track_y, (int)r.w, track_h, COL_SLIDER_TRACK);
    gfx2d_rect(r.x, track_y, (int)r.w, track_h, COLOR_BORDER);

    /* Filled portion */
    {
        int fill_w = (track_w > 0) ? (value * track_w) / max : 0;
        gfx2d_rect_fill(r.x, track_y, fill_w, track_h, COL_SLIDER_THUMB);
    }

    /* Thumb */
    thumb_x = r.x + ((track_w > 0) ? (value * track_w) / max : 0);
    {
        ui_rect_t thumb_r = ui_rect((int16_t)thumb_x, r.y,
                                    (uint16_t)thumb_w, (uint16_t)thumb_h);
        ui_draw_panel(thumb_r, COLOR_WINDOW_BG, true, true);
    }

    /* Handle dragging */
    if (dragging && ui_contains(r, mx, my)) {
        int rel = (int)(mx - r.x);
        if (track_w > 0) {
            new_val = (rel * max) / track_w;
            if (new_val < 0) new_val = 0;
            if (new_val > max) new_val = max;
        }
    }

    return new_val;
}

int ui_draw_slider_v(ui_rect_t r, int value, int max, bool dragging,
                     int16_t mx, int16_t my) {
    int track_w = 4;
    int thumb_h = 12;
    int track_x;
    int track_h;
    int thumb_y;
    int new_val = value;

    (void)mx;

    if (max < 1) max = 1;
    if (value < 0) value = 0;
    if (value > max) value = max;

    track_x = r.x + (int)r.w / 2 - track_w / 2;
    track_h = (int)r.h - thumb_h;

    /* Track */
    gfx2d_rect_fill(track_x, r.y, track_w, (int)r.h, COL_SLIDER_TRACK);
    gfx2d_rect(track_x, r.y, track_w, (int)r.h, COLOR_BORDER);

    /* Thumb */
    thumb_y = r.y + (int)r.h - thumb_h -
              ((track_h > 0) ? (value * track_h) / max : 0);
    {
        ui_rect_t thumb_r = ui_rect(r.x, (int16_t)thumb_y,
                                    r.w, (uint16_t)thumb_h);
        ui_draw_panel(thumb_r, COLOR_WINDOW_BG, true, true);
    }

    if (dragging && ui_contains(r, mx, my)) {
        int rel = (int)(r.y + (int16_t)r.h - my);
        if (track_h > 0) {
            new_val = (rel * max) / track_h;
            if (new_val < 0) new_val = 0;
            if (new_val > max) new_val = max;
        }
    }

    return new_val;
}

int ui_draw_slider_labeled(ui_rect_t r, const char *label, int value,
                           int min, int max, bool dragging,
                           int16_t mx, int16_t my) {
    ui_rect_t label_area, val_area, slider_area;
    char val_str[16];
    int slider_val;
    int range = max - min;

    if (range < 1) range = 1;

    /* Layout: [label] [slider] [value] */
    label_area = ui_cut_left(&r, 60);
    val_area = ui_cut_right(&r, 40);
    slider_area = r;

    ui_draw_label(label_area, label, COLOR_TEXT, UI_ALIGN_LEFT);

    slider_val = ui_draw_slider_h(slider_area, value - min, range,
                                  dragging, mx, my);

    int_to_str(slider_val + min, val_str, sizeof(val_str));
    ui_draw_label(val_area, val_str, COLOR_TEXT, UI_ALIGN_RIGHT);

    return slider_val + min;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Progress Bar
 * ══════════════════════════════════════════════════════════════════════ */

void ui_draw_progressbar(ui_rect_t r, int value, int max, bool show_text) {
    ui_draw_progressbar_styled(r, value, max, COL_PROGRESS_BAR, COL_PROGRESS_BG);

    if (show_text && max > 0) {
        char buf[8];
        int pct = (value * 100) / max;
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        int_to_str(pct, buf, 6);
        /* Append '%' */
        {
            int len = (int)strlen(buf);
            buf[len] = '%';
            buf[len + 1] = '\0';
        }
        ui_draw_label(r, buf, COLOR_TEXT, UI_ALIGN_CENTER);
    }
}

void ui_draw_progressbar_indeterminate(ui_rect_t r, uint32_t tick) {
    int bar_w = (int)r.w / 4;
    int cycle = (int)r.w + bar_w;
    int pos;

    if (cycle < 1) cycle = 1;
    pos = (int)(tick % (uint32_t)cycle) - bar_w;

    /* Background */
    ui_draw_panel(r, COL_PROGRESS_BG, true, false);

    /* Moving bar */
    {
        int bx = r.x + pos;
        int bw = bar_w;
        if (bx < r.x) { bw -= (r.x - bx); bx = r.x; }
        if (bx + bw > r.x + (int)r.w) bw = r.x + (int)r.w - bx;
        if (bw > 0) {
            gfx2d_rect_fill(bx, r.y + 1, bw, (int)r.h - 2, COL_PROGRESS_BAR);
        }
    }
}

void ui_draw_progressbar_styled(ui_rect_t r, int value, int max,
                                uint32_t bar_color, uint32_t bg_color) {
    int fill_w;

    if (max < 1) max = 1;
    if (value < 0) value = 0;
    if (value > max) value = max;

    /* Background */
    gfx2d_rect_fill(r.x, r.y, (int)r.w, (int)r.h, bg_color);
    gfx2d_rect(r.x, r.y, (int)r.w, (int)r.h, COLOR_BORDER);

    /* Fill */
    fill_w = (value * ((int)r.w - 2)) / max;
    if (fill_w > 0) {
        gfx2d_rect_fill(r.x + 1, r.y + 1, fill_w, (int)r.h - 2, bar_color);
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Spinner
 * ══════════════════════════════════════════════════════════════════════ */

bool ui_draw_spinner(ui_rect_t r, ui_spinner_state_t *state,
                     int min, int max, int16_t mx, int16_t my,
                     bool clicked) {
    int btn_w = 16;
    bool changed = false;
    char val_str[16];
    ui_rect_t text_area, up_area, down_area;

    /* Layout: [text_field][up][down] */
    up_area = ui_cut_right(&r, (uint16_t)btn_w);
    down_area = ui_rect(up_area.x,
                        (int16_t)(up_area.y + (int16_t)(up_area.h / 2)),
                        up_area.w,
                        (uint16_t)(up_area.h / 2));
    up_area.h = (uint16_t)(up_area.h / 2);

    text_area = r;

    /* Text field */
    int_to_str(state->value, val_str, sizeof(val_str));
    ui_draw_textfield(text_area, val_str, -1);

    /* Up button */
    state->up_hover = ui_contains(up_area, mx, my);
    ui_draw_panel(up_area, state->up_hover ? COLOR_BUTTON_HOVER : COLOR_WINDOW_BG,
                  true, true);
    {
        int16_t ax = (int16_t)(up_area.x + (int16_t)(up_area.w / 2));
        int16_t ay = (int16_t)(up_area.y + (int16_t)(up_area.h / 2) - 1);
        gfx2d_line(ax - 2, ay + 1, ax, ay - 1, COLOR_TEXT);
        gfx2d_line(ax, ay - 1, ax + 2, ay + 1, COLOR_TEXT);
    }

    /* Down button */
    state->down_hover = ui_contains(down_area, mx, my);
    ui_draw_panel(down_area,
                  state->down_hover ? COLOR_BUTTON_HOVER : COLOR_WINDOW_BG,
                  true, true);
    {
        int16_t ax = (int16_t)(down_area.x + (int16_t)(down_area.w / 2));
        int16_t ay = (int16_t)(down_area.y + (int16_t)(down_area.h / 2));
        gfx2d_line(ax - 2, ay - 1, ax, ay + 1, COLOR_TEXT);
        gfx2d_line(ax, ay + 1, ax + 2, ay - 1, COLOR_TEXT);
    }

    /* Handle clicks */
    if (clicked) {
        if (state->up_hover && state->value < max) {
            state->value++;
            changed = true;
        }
        if (state->down_hover && state->value > min) {
            state->value--;
            changed = true;
        }
    }

    return changed;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Toggle Switch
 * ══════════════════════════════════════════════════════════════════════ */

bool ui_draw_toggle(ui_rect_t r, bool on, bool enabled,
                    int16_t mx, int16_t my, bool clicked) {
    int track_w = 36;
    int track_h = 18;
    int knob_r = 7;
    int16_t tx, ty;
    uint32_t track_color;
    bool hit = false;

    /* Center the toggle in the rect */
    tx = (int16_t)(r.x + (int16_t)((r.w - (uint16_t)track_w) / 2));
    ty = (int16_t)(r.y + (int16_t)((r.h - (uint16_t)track_h) / 2));

    /* Track */
    track_color = on ? COL_TOGGLE_ON : COL_TOGGLE_OFF;
    if (!enabled) track_color = COL_DISABLED;

    gfx2d_rect_round_fill(tx, ty, track_w, track_h, track_h / 2,
                          track_color);
    gfx2d_rect_round(tx, ty, track_w, track_h, track_h / 2,
                     COLOR_BORDER);

    /* Knob */
    {
        int16_t kx;
        if (on) {
            kx = (int16_t)(tx + track_w - knob_r - 2);
        } else {
            kx = (int16_t)(tx + knob_r + 2);
        }
        int16_t ky = (int16_t)(ty + track_h / 2);
        gfx2d_circle_fill(kx, ky, knob_r, COLOR_TEXT_LIGHT);
        gfx2d_circle(kx, ky, knob_r, COLOR_BORDER);
    }

    /* Hit test */
    if (enabled && clicked) {
        ui_rect_t hit_r = ui_rect(tx, ty, (uint16_t)track_w,
                                  (uint16_t)track_h);
        if (ui_contains(hit_r, mx, my))
            hit = true;
    }

    return hit;
}
