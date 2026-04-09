/**
 * gui_events.c - Event System & Dialogs for cupid-os
 *
 * Event dispatching, message box, input dialog,
 * color picker, progress dialog.
 */

#include "gui_events.h"
#include "gfx2d.h"
#include "graphics.h"
#include "font_8x8.h"
#include "string.h"
#include "memory.h"
#include "../drivers/vga.h"

#define EVENT_QUEUE_SIZE 32

typedef struct {
    int                win_id; /* 0 = global/default, -1 = unused */
    ui_event_handler_t handlers[UI_MAX_EVENT_HANDLERS];
    int                handler_count;
    ui_event_t         queue[EVENT_QUEUE_SIZE];
    int                event_head;
    int                event_tail;
} ui_event_slot_t;

static ui_event_slot_t g_slots[MAX_WINDOWS + 1];

static ui_event_slot_t *ui_event_slot_for(window_t *win, bool create) {
    int target_id = win ? (int)win->id : 0;
    int i;

    for (i = 0; i < MAX_WINDOWS + 1; i++) {
        if (g_slots[i].win_id == target_id) {
            return &g_slots[i];
        }
    }

    if (!create) {
        return NULL;
    }

    /* Reclaim stale window slots before giving up. */
    for (i = 0; i < MAX_WINDOWS + 1; i++) {
        if (g_slots[i].win_id > 0 && !gui_get_window(g_slots[i].win_id)) {
            g_slots[i].win_id = -1;
            g_slots[i].handler_count = 0;
            g_slots[i].event_head = 0;
            g_slots[i].event_tail = 0;
        }
    }

    for (i = 0; i < MAX_WINDOWS + 1; i++) {
        if (g_slots[i].win_id < 0) {
            g_slots[i].win_id = target_id;
            g_slots[i].handler_count = 0;
            g_slots[i].event_head = 0;
            g_slots[i].event_tail = 0;
            return &g_slots[i];
        }
    }

    return NULL;
}

void gui_events_init(void) {
    int i;
    for (i = 0; i < MAX_WINDOWS + 1; i++) {
        g_slots[i].win_id = -1;
        g_slots[i].handler_count = 0;
        g_slots[i].event_head = 0;
        g_slots[i].event_tail = 0;
    }
    /* Reserve slot 0 for global/non-windowed dialogs. */
    g_slots[0].win_id = 0;
}

void ui_register_handler(window_t *win, ui_event_handler_t handler) {
    ui_event_slot_t *slot = ui_event_slot_for(win, true);
    if (slot && slot->handler_count < UI_MAX_EVENT_HANDLERS) {
        slot->handlers[slot->handler_count++] = handler;
    }
}

void ui_emit_event(window_t *win, ui_event_t *event) {
    ui_event_slot_t *slot = ui_event_slot_for(win, true);
    int next;

    if (!slot) {
        return;
    }

    next = (slot->event_tail + 1) % EVENT_QUEUE_SIZE;
    if (next != slot->event_head) {
        slot->queue[slot->event_tail] = *event;
        slot->event_tail = next;
    }
}

void ui_process_events(window_t *win) {
    ui_event_slot_t *slot = ui_event_slot_for(win, false);

    if (!slot) {
        return;
    }

    while (slot->event_head != slot->event_tail) {
        ui_event_t *ev = &slot->queue[slot->event_head];
        int i;

        for (i = 0; i < slot->handler_count; i++) {
            ui_event_handler_t *h = &slot->handlers[i];

            /* Filter by widget_id */
            if (h->widget_id != 0 && h->widget_id != ev->widget_id)
                continue;

            /* Filter by event type */
            if (h->filter != UI_EVENT_NONE && h->filter != ev->type)
                continue;

            if (h->callback) {
                h->callback(ev, h->context);
            }
        }

        slot->event_head = (slot->event_head + 1) % EVENT_QUEUE_SIZE;
    }
}

/* Message Box */

/* Dialog dimensions */
#define MSGBOX_W  280
#define MSGBOX_H  120
#define BTN_W      60
#define BTN_H      22

static const char *msgbox_icon_char(ui_msgbox_type_t type) {
    switch (type) {
    case UI_MSGBOX_INFO:     return "i";
    case UI_MSGBOX_WARNING:  return "!";
    case UI_MSGBOX_ERROR:    return "X";
    case UI_MSGBOX_QUESTION: return "?";
    }
    return " ";
}

ui_msgbox_result_t ui_msgbox_draw(ui_msgbox_state_t *state,
                                   int16_t mx, int16_t my, bool clicked) {
    int16_t dx, dy;
    ui_rect_t dlg, btn1_r, btn2_r, btn3_r;
    const char *btn1_text = NULL;
    const char *btn2_text = NULL;
    const char *btn3_text = NULL;
    ui_msgbox_result_t r1 = UI_MSGBOX_RESULT_OK;
    ui_msgbox_result_t r2 = UI_MSGBOX_RESULT_CANCEL;
    ui_msgbox_result_t r3 = UI_MSGBOX_RESULT_CANCEL;
    int btn_count = 1;
    int total_btn_w, start_x;

    if (!state->active) return UI_MSGBOX_RESULT_CANCEL;

    dx = (int16_t)((VGA_GFX_WIDTH - MSGBOX_W) / 2);
    dy = (int16_t)((VGA_GFX_HEIGHT - MSGBOX_H) / 2);
    dlg = ui_rect(dx, dy, MSGBOX_W, MSGBOX_H);

    /* Dim background */
    gfx2d_rect_fill(0, 0, VGA_GFX_WIDTH, VGA_GFX_HEIGHT, 0x40000000);

    /* Dialog frame */
    gfx2d_rect_fill(dx, dy, MSGBOX_W, MSGBOX_H, COLOR_WINDOW_BG);
    gfx2d_rect(dx, dy, MSGBOX_W, MSGBOX_H, COLOR_BORDER);

    /* Titlebar */
    gfx2d_rect_fill(dx + 1, dy + 1, MSGBOX_W - 2, 14, COLOR_TITLEBAR);
    gfx2d_text(dx + 4, dy + 3, state->title, COLOR_TEXT, GFX2D_FONT_NORMAL);

    /* Icon */
    gfx2d_text(dx + 16, dy + 40, msgbox_icon_char(state->type),
               COLOR_TEXT, GFX2D_FONT_NORMAL);

    /* Message */
    gfx2d_text(dx + 40, dy + 36, state->message, COLOR_TEXT,
               GFX2D_FONT_NORMAL);

    /* Setup buttons */
    switch (state->buttons) {
    case UI_MSGBOX_OK:
        btn_count = 1;
        btn1_text = "OK";
        r1 = UI_MSGBOX_RESULT_OK;
        break;
    case UI_MSGBOX_OK_CANCEL:
        btn_count = 2;
        btn1_text = "OK";      r1 = UI_MSGBOX_RESULT_OK;
        btn2_text = "Cancel";  r2 = UI_MSGBOX_RESULT_CANCEL;
        break;
    case UI_MSGBOX_YES_NO:
        btn_count = 2;
        btn1_text = "Yes";     r1 = UI_MSGBOX_RESULT_YES;
        btn2_text = "No";      r2 = UI_MSGBOX_RESULT_NO;
        break;
    case UI_MSGBOX_YES_NO_CANCEL:
        btn_count = 3;
        btn1_text = "Yes";     r1 = UI_MSGBOX_RESULT_YES;
        btn2_text = "No";      r2 = UI_MSGBOX_RESULT_NO;
        btn3_text = "Cancel";  r3 = UI_MSGBOX_RESULT_CANCEL;
        break;
    case UI_MSGBOX_RETRY_CANCEL:
        btn_count = 2;
        btn1_text = "Retry";   r1 = UI_MSGBOX_RESULT_RETRY;
        btn2_text = "Cancel";  r2 = UI_MSGBOX_RESULT_CANCEL;
        break;
    }

    total_btn_w = btn_count * BTN_W + (btn_count - 1) * 8;
    start_x = dx + (MSGBOX_W - total_btn_w) / 2;

    /* Button 1 */
    btn1_r = ui_rect((int16_t)start_x,
                     (int16_t)(dy + MSGBOX_H - BTN_H - 10),
                     BTN_W, BTN_H);
    {
        bool hover = ui_contains(btn1_r, mx, my);
        ui_draw_button(btn1_r, btn1_text, hover);
        if (clicked && hover) {
            state->result = r1;
            state->active = false;
            return r1;
        }
    }

    /* Button 2 */
    if (btn_count >= 2) {
        btn2_r = ui_rect((int16_t)(start_x + BTN_W + 8),
                         (int16_t)(dy + MSGBOX_H - BTN_H - 10),
                         BTN_W, BTN_H);
        {
            bool hover = ui_contains(btn2_r, mx, my);
            ui_draw_button(btn2_r, btn2_text, hover);
            if (clicked && hover) {
                state->result = r2;
                state->active = false;
                return r2;
            }
        }
    }

    /* Button 3 */
    if (btn_count >= 3) {
        btn3_r = ui_rect((int16_t)(start_x + (BTN_W + 8) * 2),
                         (int16_t)(dy + MSGBOX_H - BTN_H - 10),
                         BTN_W, BTN_H);
        {
            bool hover = ui_contains(btn3_r, mx, my);
            ui_draw_button(btn3_r, btn3_text, hover);
            if (clicked && hover) {
                state->result = r3;
                state->active = false;
                return r3;
            }
        }
    }

    (void)dlg;
    return UI_MSGBOX_RESULT_OK; /* still active, no result yet */
}

ui_msgbox_result_t ui_msgbox(const char *title, const char *message,
                              ui_msgbox_type_t type,
                              ui_msgbox_buttons_t buttons) {
    /* Blocking msgbox - set up state, caller should loop on ui_msgbox_draw */
    /* In a cooperative multitasking OS, true blocking isn't ideal.
       We provide the draw function for frame-by-frame rendering instead. */
    ui_msgbox_state_t state;
    state.active = true;
    state.title = title;
    state.message = message;
    state.type = type;
    state.buttons = buttons;
    state.result = UI_MSGBOX_RESULT_CANCEL;
    state.hover_btn = 0;

    /* In a real blocking implementation we'd loop here.
       For cupid-os, use ui_msgbox_draw() per frame instead. */
    (void)state;
    return UI_MSGBOX_RESULT_OK;
}

/* Input Dialog */

#define INPUT_DLG_W  300
#define INPUT_DLG_H  110

void ui_input_dialog_init(ui_input_dialog_state_t *state,
                          char *buffer, int buffer_size) {
    state->active = true;
    state->buffer = buffer;
    state->buffer_size = buffer_size;
    state->cursor = (int)strlen(buffer);
    state->hover_btn = 0;
    state->confirmed = false;
    state->cancelled = false;
}

int ui_input_dialog_draw(ui_input_dialog_state_t *state,
                         const char *title, const char *prompt,
                         int16_t mx, int16_t my, bool clicked,
                         uint8_t key, char ch) {
    int16_t dx, dy;
    ui_rect_t field_r, ok_r, cancel_r;

    if (!state->active) return state->confirmed ? 1 : -1;

    dx = (int16_t)((VGA_GFX_WIDTH - INPUT_DLG_W) / 2);
    dy = (int16_t)((VGA_GFX_HEIGHT - INPUT_DLG_H) / 2);

    /* Dim background */
    gfx2d_rect_fill(0, 0, VGA_GFX_WIDTH, VGA_GFX_HEIGHT, 0x40000000);

    /* Dialog frame */
    gfx2d_rect_fill(dx, dy, INPUT_DLG_W, INPUT_DLG_H, COLOR_WINDOW_BG);
    gfx2d_rect(dx, dy, INPUT_DLG_W, INPUT_DLG_H, COLOR_BORDER);

    /* Titlebar */
    gfx2d_rect_fill(dx + 1, dy + 1, INPUT_DLG_W - 2, 14, COLOR_TITLEBAR);
    gfx2d_text(dx + 4, dy + 3, title, COLOR_TEXT, GFX2D_FONT_NORMAL);

    /* Prompt */
    gfx2d_text(dx + 10, dy + 24, prompt, COLOR_TEXT, GFX2D_FONT_NORMAL);

    /* Text field */
    field_r = ui_rect((int16_t)(dx + 10), (int16_t)(dy + 40),
                      (uint16_t)(INPUT_DLG_W - 20), 18);
    ui_draw_textfield(field_r, state->buffer, state->cursor);

    /* Handle typing */
    if (ch >= 32 && ch < 127 && state->cursor < state->buffer_size - 1) {
        state->buffer[state->cursor++] = ch;
        state->buffer[state->cursor] = '\0';
    }
    if (key == 0x0E && state->cursor > 0) { /* Backspace */
        state->buffer[--state->cursor] = '\0';
    }
    if (key == 0x1C) { /* Enter */
        state->confirmed = true;
        state->active = false;
        return 1;
    }
    if (key == 0x01) { /* Escape */
        state->cancelled = true;
        state->active = false;
        return -1;
    }

    /* OK button */
    ok_r = ui_rect((int16_t)(dx + INPUT_DLG_W - BTN_W * 2 - 18),
                   (int16_t)(dy + INPUT_DLG_H - BTN_H - 10),
                   BTN_W, BTN_H);
    {
        bool hover = ui_contains(ok_r, mx, my);
        ui_draw_button(ok_r, "OK", hover);
        if (clicked && hover) {
            state->confirmed = true;
            state->active = false;
            return 1;
        }
    }

    /* Cancel button */
    cancel_r = ui_rect((int16_t)(dx + INPUT_DLG_W - BTN_W - 10),
                       (int16_t)(dy + INPUT_DLG_H - BTN_H - 10),
                       BTN_W, BTN_H);
    {
        bool hover = ui_contains(cancel_r, mx, my);
        ui_draw_button(cancel_r, "Cancel", hover);
        if (clicked && hover) {
            state->cancelled = true;
            state->active = false;
            return -1;
        }
    }

    return 0; /* still active */
}

/* Color Picker */

/* Simple HSV to RGB conversion (integer-only) */
static uint32_t hsv_to_rgb(int h, int s, int v) {
    int region, remainder, p, q, t;
    int r, g, b;

    if (s == 0) {
        return ((uint32_t)v << 16) | ((uint32_t)v << 8) | (uint32_t)v;
    }

    /* h: 0-359, s: 0-255, v: 0-255 */
    region = h / 60;
    remainder = (h - (region * 60)) * 255 / 60;

    p = (v * (255 - s)) / 255;
    q = (v * (255 - (s * remainder) / 255)) / 255;
    t = (v * (255 - (s * (255 - remainder)) / 255)) / 255;

    switch (region) {
    case 0:  r = v; g = t; b = p; break;
    case 1:  r = q; g = v; b = p; break;
    case 2:  r = p; g = v; b = t; break;
    case 3:  r = p; g = q; b = v; break;
    case 4:  r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }

    return ((uint32_t)(r & 0xFF) << 16) |
           ((uint32_t)(g & 0xFF) << 8) |
            (uint32_t)(b & 0xFF);
}

static void rgb_from_color(uint32_t c, int *r, int *g, int *b) {
    *r = (int)((c >> 16) & 0xFF);
    *g = (int)((c >> 8)  & 0xFF);
    *b = (int)(c & 0xFF);
}

static void hsv_from_rgb(int r, int g, int b, int *h, int *s, int *v) {
    int maxc = r;
    int minc = r;
    int delta;
    int hue = 0;

    if (g > maxc) maxc = g;
    if (b > maxc) maxc = b;
    if (g < minc) minc = g;
    if (b < minc) minc = b;

    delta = maxc - minc;
    *v = maxc;

    if (maxc <= 0) {
        *s = 0;
        *h = 0;
        return;
    }

    *s = (delta * 255) / maxc;
    if (delta == 0) {
        *h = 0;
        return;
    }

    if (maxc == r) {
        hue = (60 * (g - b)) / delta;
    } else if (maxc == g) {
        hue = 120 + (60 * (b - r)) / delta;
    } else {
        hue = 240 + (60 * (r - g)) / delta;
    }

    while (hue < 0) hue += 360;
    while (hue >= 360) hue -= 360;
    if (hue > 359) hue = 359;
    *h = hue;
}

bool ui_draw_colorpicker(ui_rect_t r, ui_colorpicker_state_t *state,
                         int16_t mx, int16_t my, bool clicked) {
    bool changed = false;
    int sv_size, hue_w;
    ui_rect_t sv_r, hue_r, preview_r, recent_r;

    /* Layout: [SV square][gap][Hue bar][gap][Preview + Recent] */
    sv_size = (int)r.h - 40;
    if (sv_size > (int)r.w - 60) sv_size = (int)r.w - 60;
    if (sv_size < 32) sv_size = 32;
    hue_w = 16;

    /* SV square */
    sv_r = ui_rect(r.x, r.y, (uint16_t)sv_size, (uint16_t)sv_size);

    /* Draw SV gradient (simplified: sample every 4 pixels) */
    {
        int px, py;
        for (py = 0; py < sv_size; py += 2) {
            for (px = 0; px < sv_size; px += 2) {
                int s = (px * 255) / sv_size;
                int v = 255 - (py * 255) / sv_size;
                uint32_t c = hsv_to_rgb(state->hue, s, v);
                gfx2d_pixel(sv_r.x + px, sv_r.y + py, c);
                gfx2d_pixel(sv_r.x + px + 1, sv_r.y + py, c);
                gfx2d_pixel(sv_r.x + px, sv_r.y + py + 1, c);
                gfx2d_pixel(sv_r.x + px + 1, sv_r.y + py + 1, c);
            }
        }
        gfx2d_rect(sv_r.x, sv_r.y, sv_size, sv_size, COLOR_BORDER);

        /* SV cursor */
        {
            int cx = sv_r.x + (state->saturation * sv_size) / 255;
            int cy = sv_r.y + ((255 - state->value) * sv_size) / 255;
            gfx2d_circle(cx, cy, 3, 0x00FFFFFF);
            gfx2d_circle(cx, cy, 4, COLOR_TEXT);
        }

        /* SV click */
        if (clicked && ui_contains(sv_r, mx, my)) {
            state->saturation = ((mx - sv_r.x) * 255) / sv_size;
            state->value = 255 - ((my - sv_r.y) * 255) / sv_size;
            if (state->saturation < 0) state->saturation = 0;
            if (state->saturation > 255) state->saturation = 255;
            if (state->value < 0) state->value = 0;
            if (state->value > 255) state->value = 255;
            state->selected_color = hsv_to_rgb(state->hue,
                                               state->saturation,
                                               state->value);
            rgb_from_color(state->selected_color,
                          &state->red, &state->green, &state->blue);
            changed = true;
        }
    }

    /* Hue bar */
    hue_r = ui_rect((int16_t)(sv_r.x + (int16_t)sv_size + 6), r.y,
                    (uint16_t)hue_w, (uint16_t)sv_size);
    {
        int py;
        for (py = 0; py < sv_size; py++) {
            int h = (py * 359) / sv_size;
            uint32_t c = hsv_to_rgb(h, 255, 255);
            gfx2d_hline(hue_r.x, hue_r.y + py, hue_w, c);
        }
        gfx2d_rect(hue_r.x, hue_r.y, hue_w, sv_size, COLOR_BORDER);

        /* Hue cursor */
        {
            int hy = hue_r.y + (state->hue * sv_size) / 359;
            gfx2d_hline(hue_r.x - 2, hy, hue_w + 4, COLOR_TEXT);
        }

        /* Hue click */
        if (clicked && ui_contains(hue_r, mx, my)) {
            state->hue = ((my - hue_r.y) * 359) / sv_size;
            if (state->hue < 0) state->hue = 0;
            if (state->hue > 359) state->hue = 359;
            state->selected_color = hsv_to_rgb(state->hue,
                                               state->saturation,
                                               state->value);
            rgb_from_color(state->selected_color,
                          &state->red, &state->green, &state->blue);
            changed = true;
        }
    }

    /* Color preview */
    preview_r = ui_rect((int16_t)(hue_r.x + (int16_t)hue_w + 8), r.y,
                        40, 40);
    gfx2d_rect_fill(preview_r.x, preview_r.y, 40, 40,
                    state->selected_color);
    gfx2d_rect(preview_r.x, preview_r.y, 40, 40, COLOR_BORDER);

    /* RGB values display */
    {
        int16_t tx = preview_r.x;
        int16_t ty = (int16_t)(preview_r.y + 48);
        char buf[16];

        gfx2d_text(tx, ty, "R:", COLOR_TEXT, GFX2D_FONT_NORMAL);
        /* Simple int display */
        {
            int n = state->red, d;
            buf[0] = (char)('0' + (n / 100) % 10);
            d = n / 10;
            buf[1] = (char)('0' + d % 10);
            buf[2] = (char)('0' + n % 10);
            buf[3] = '\0';
        }
        gfx2d_text(tx + 20, ty, buf, COLOR_TEXT, GFX2D_FONT_NORMAL);
        ty = (int16_t)(ty + (int16_t)FONT_H + 2);

        gfx2d_text(tx, ty, "G:", COLOR_TEXT, GFX2D_FONT_NORMAL);
        {
            int n = state->green, d;
            buf[0] = (char)('0' + (n / 100) % 10);
            d = n / 10;
            buf[1] = (char)('0' + d % 10);
            buf[2] = (char)('0' + n % 10);
            buf[3] = '\0';
        }
        gfx2d_text(tx + 20, ty, buf, COLOR_TEXT, GFX2D_FONT_NORMAL);
        ty = (int16_t)(ty + (int16_t)FONT_H + 2);

        gfx2d_text(tx, ty, "B:", COLOR_TEXT, GFX2D_FONT_NORMAL);
        {
            int n = state->blue, d;
            buf[0] = (char)('0' + (n / 100) % 10);
            d = n / 10;
            buf[1] = (char)('0' + d % 10);
            buf[2] = (char)('0' + n % 10);
            buf[3] = '\0';
        }
        gfx2d_text(tx + 20, ty, buf, COLOR_TEXT, GFX2D_FONT_NORMAL);
    }

    /* Recent colors row */
    recent_r = ui_rect(r.x,
                       (int16_t)(r.y + (int16_t)sv_size + 6),
                       r.w, 18);
    {
        int i;
        for (i = 0; i < 16; i++) {
            int16_t sx = (int16_t)(recent_r.x + (int16_t)(i * 18));
            ui_rect_t swatch = ui_rect(sx, recent_r.y, 16, 16);

            gfx2d_rect_fill(sx, recent_r.y, 16, 16,
                            state->recent_colors[i]);
            gfx2d_rect(sx, recent_r.y, 16, 16, COLOR_BORDER);

            if (clicked && ui_contains(swatch, mx, my)) {
                state->selected_color = state->recent_colors[i];
                rgb_from_color(state->selected_color,
                              &state->red, &state->green, &state->blue);
                hsv_from_rgb(state->red, state->green, state->blue,
                             &state->hue, &state->saturation, &state->value);
                changed = true;
            }
        }
    }

    return changed;
}

bool ui_draw_color_swatch(ui_rect_t r, uint32_t color,
                          int16_t mx, int16_t my, bool clicked) {
    bool hover = ui_contains(r, mx, my);

    gfx2d_rect_fill(r.x, r.y, (int)r.w, (int)r.h, color);
    gfx2d_rect(r.x, r.y, (int)r.w, (int)r.h,
               hover ? COLOR_TEXT : COLOR_BORDER);

    if (hover) {
        /* Highlight border for hover */
        gfx2d_rect(r.x - 1, r.y - 1, (int)r.w + 2, (int)r.h + 2,
                   COLOR_TEXT);
    }

    return clicked && hover;
}

/* Progress Dialog */

#define PROGRESS_DLG_W  300
#define PROGRESS_DLG_H   90

void ui_progress_dialog_init(ui_progress_dialog_t *dlg,
                             const char *title, const char *message,
                             bool cancelable) {
    dlg->active = true;
    dlg->cancelable = cancelable;
    dlg->cancelled = false;
    dlg->title = title;
    dlg->message = message;
    dlg->value = 0;
    dlg->max_value = 100;
}

void ui_progress_dialog_update(ui_progress_dialog_t *dlg,
                               int value, int max_val) {
    dlg->value = value;
    dlg->max_value = max_val;
}

void ui_progress_dialog_set_message(ui_progress_dialog_t *dlg,
                                    const char *msg) {
    dlg->message = msg;
}

bool ui_progress_dialog_is_canceled(ui_progress_dialog_t *dlg) {
    return dlg->cancelled;
}

void ui_progress_dialog_draw(ui_progress_dialog_t *dlg,
                             int16_t mx, int16_t my, bool clicked) {
    int16_t dx, dy;
    ui_rect_t bar_r;

    if (!dlg->active) return;

    dx = (int16_t)((VGA_GFX_WIDTH - PROGRESS_DLG_W) / 2);
    dy = (int16_t)((VGA_GFX_HEIGHT - PROGRESS_DLG_H) / 2);

    /* Dialog frame */
    gfx2d_rect_fill(dx, dy, PROGRESS_DLG_W, PROGRESS_DLG_H,
                    COLOR_WINDOW_BG);
    gfx2d_rect(dx, dy, PROGRESS_DLG_W, PROGRESS_DLG_H, COLOR_BORDER);

    /* Titlebar */
    gfx2d_rect_fill(dx + 1, dy + 1, PROGRESS_DLG_W - 2, 14, COLOR_TITLEBAR);
    gfx2d_text(dx + 4, dy + 3, dlg->title, COLOR_TEXT, GFX2D_FONT_NORMAL);

    /* Message */
    if (dlg->message) {
        gfx2d_text(dx + 10, dy + 24, dlg->message, COLOR_TEXT,
                   GFX2D_FONT_NORMAL);
    }

    /* Progress bar */
    bar_r = ui_rect((int16_t)(dx + 10), (int16_t)(dy + 42),
                    (uint16_t)(PROGRESS_DLG_W - 20), 16);
    {
        int fill_w = 0;
        if (dlg->max_value > 0) {
            fill_w = ((int)bar_r.w * dlg->value) / dlg->max_value;
        }

        /* Track */
        gfx2d_rect_fill(bar_r.x, bar_r.y, (int)bar_r.w, 16, 0x00FFFFFF);
        gfx2d_rect(bar_r.x, bar_r.y, (int)bar_r.w, 16, COLOR_BORDER);

        /* Fill */
        if (fill_w > 0) {
            gfx2d_rect_fill(bar_r.x + 1, bar_r.y + 1, fill_w - 2, 14,
                            COLOR_TITLEBAR);
        }
    }

    /* Cancel button */
    if (dlg->cancelable) {
        ui_rect_t cancel_r = ui_rect(
            (int16_t)(dx + PROGRESS_DLG_W - BTN_W - 10),
            (int16_t)(dy + PROGRESS_DLG_H - BTN_H - 8),
            BTN_W, BTN_H);
        bool hover = ui_contains(cancel_r, mx, my);
        ui_draw_button(cancel_r, "Cancel", hover);

        if (clicked && hover) {
            dlg->cancelled = true;
            dlg->active = false;
        }
    }
}

void ui_progress_dialog_close(ui_progress_dialog_t *dlg) {
    dlg->active = false;
}
