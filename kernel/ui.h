#ifndef UI_H
#define UI_H

#include "types.h"

/* ══════════════════════════════════════════════════════════════════════
 *  ui.h — Lightweight UI widget toolkit for cupid-os
 *
 *  Provides a ui_rect_t layout primitive and composite widget drawing
 *  functions (buttons, labels, panels, text fields, scrollbars) so
 *  that draw code and hit-test code share the same geometry, and text
 *  is automatically centered/padded without manual pixel math.
 * ══════════════════════════════════════════════════════════════════════ */

/* ── Rectangle primitive ──────────────────────────────────────────── */

typedef struct {
    int16_t  x, y;
    uint16_t w, h;
} ui_rect_t;

/* ── Text alignment ───────────────────────────────────────────────── */

typedef enum {
    UI_ALIGN_LEFT   = 0,
    UI_ALIGN_CENTER = 1,
    UI_ALIGN_RIGHT  = 2
} ui_align_t;

/* ── Constructors / layout helpers ────────────────────────────────── */

/* Build a rect from components */
ui_rect_t ui_rect(int16_t x, int16_t y, uint16_t w, uint16_t h);

/* Inset a rect by `pad` pixels on every side */
ui_rect_t ui_pad(ui_rect_t r, int16_t pad);

/* Inset with separate horizontal / vertical padding */
ui_rect_t ui_pad_xy(ui_rect_t r, int16_t px, int16_t py);

/* Center a (cw × ch) rect inside `outer` */
ui_rect_t ui_center(ui_rect_t outer, uint16_t cw, uint16_t ch);

/* Slice `height` pixels from the top; returns the slice, modifies *r */
ui_rect_t ui_cut_top(ui_rect_t *r, uint16_t height);

/* Slice `height` pixels from the bottom */
ui_rect_t ui_cut_bottom(ui_rect_t *r, uint16_t height);

/* Slice `width` pixels from the left */
ui_rect_t ui_cut_left(ui_rect_t *r, uint16_t width);

/* Slice `width` pixels from the right */
ui_rect_t ui_cut_right(ui_rect_t *r, uint16_t width);

/* ── Hit testing ──────────────────────────────────────────────────── */

/* Returns true if (px, py) is inside r */
bool ui_contains(ui_rect_t r, int16_t px, int16_t py);

/* ── Drawing: low-level ───────────────────────────────────────────── */

/* Drop shadow: dark rectangle offset behind `r` */
void ui_draw_shadow(ui_rect_t r, uint8_t color, int16_t offset);

/* Filled panel with optional Windows-95-style 3D raised/sunken edge */
void ui_draw_panel(ui_rect_t r, uint8_t bg, bool border_3d, bool raised);

/* ── Drawing: composite widgets ───────────────────────────────────── */

/* Button: 3D raised panel + auto-centered label.
 * If `focused` is true, draws a 1px black focus ring. */
void ui_draw_button(ui_rect_t r, const char *label, bool focused);

/* Label: text with alignment, vertically centered in rect */
void ui_draw_label(ui_rect_t r, const char *text, uint8_t color,
                   ui_align_t align);

/* Sunken text-entry field with cursor.
 * `cursor_pos` = character index, or < 0 to hide cursor. */
void ui_draw_textfield(ui_rect_t r, const char *text, int cursor_pos);

/* Title bar: filled color bar with left-aligned white text */
void ui_draw_titlebar(ui_rect_t r, const char *title, bool focused);

/* Vertical scrollbar (up/down arrows + thumb).
 * `total` = total items, `visible` = visible items,
 * `offset` = first visible item index. */
void ui_draw_vscrollbar(ui_rect_t r, int total, int visible, int offset);

/* Scrollbar hit test.
 * Returns:  -1 = up/page-up area,
 *           +1 = down/page-down area,
 *            0 = not hit or nothing to do.
 * `page` is set to true if the click was on the track (page
 * scroll) rather than an arrow button. */
int ui_vscrollbar_hit(ui_rect_t r, int16_t mx, int16_t my, bool *page);

#endif
