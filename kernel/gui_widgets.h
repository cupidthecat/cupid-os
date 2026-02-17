/**
 * gui_widgets.h - Essential GUI Controls for cupid-os
 *
 * Stateless rendering functions for common UI widgets:
 * checkbox, radio, dropdown, listbox, slider, progress bar,
 * spinner, toggle switch.
 *
 * All widgets use ui_rect_t for layout and return interaction
 * results (clicked, changed, etc.) for the caller to handle.
 */

#ifndef GUI_WIDGETS_H
#define GUI_WIDGETS_H

#include "types.h"
#include "ui.h"

typedef struct {
    bool open;
    int  selected;
    int  hover_item;
} ui_dropdown_state_t;

typedef struct {
    int offset;         /* First visible item index       */
    int selected;       /* Selected item index (-1=none)  */
    int hover_item;     /* Item under mouse (-1=none)     */
} ui_listbox_state_t;

typedef struct {
    int  value;
    bool up_hover;
    bool down_hover;
} ui_spinner_state_t;

/* Controls */

/** Draw a checkbox with label.  Returns true if the checkbox area
 *  was clicked (caller should toggle `checked`).
 *  Pass current mouse position and click state externally. */
bool ui_draw_checkbox(ui_rect_t r, const char *label, bool checked,
                      bool enabled, int16_t mx, int16_t my, bool clicked);

/** Draw a radio button. Returns true if clicked. */
bool ui_draw_radio(ui_rect_t r, const char *label, bool selected,
                   bool enabled, int16_t mx, int16_t my, bool clicked);

/** Radio group: draw `count` radios stacked vertically.
 *  Returns new selected index, or -1 if unchanged. */
int ui_radio_group(ui_rect_t r, const char **labels, int count,
                   int selected, int16_t mx, int16_t my, bool clicked);

/** Draw dropdown. Returns true if selection changed.
 *  State is mutated (open/close, hover tracking). */
bool ui_draw_dropdown(ui_rect_t r, const char **items, int count,
                      ui_dropdown_state_t *state, int16_t mx, int16_t my,
                      bool clicked);

/** Draw list box with scrollbar. Returns true if selection changed.
 *  scroll_delta: mouse wheel (-1 up, +1 down, 0 none). */
bool ui_draw_listbox(ui_rect_t r, const char **items, int count,
                     ui_listbox_state_t *state, int16_t mx, int16_t my,
                     bool clicked, int scroll_delta);

/** Hit test: which item index is at (mx,my)?  Returns -1 if none. */
int ui_listbox_hit(ui_rect_t r, int offset, int item_height, int count,
                   int16_t mx, int16_t my);

/** Draw horizontal slider. Returns new value (0..max).
 *  `dragging` should be true while mouse is held on the thumb. */
int ui_draw_slider_h(ui_rect_t r, int value, int max, bool dragging,
                     int16_t mx, int16_t my);

/** Draw vertical slider. Returns new value (0..max). */
int ui_draw_slider_v(ui_rect_t r, int value, int max, bool dragging,
                     int16_t mx, int16_t my);

/** Slider with label and value display. */
int ui_draw_slider_labeled(ui_rect_t r, const char *label, int value,
                           int min, int max, bool dragging,
                           int16_t mx, int16_t my);

/** Progress bar. value in [0..max]. */
void ui_draw_progressbar(ui_rect_t r, int value, int max, bool show_text);

/** Indeterminate progress bar (animated). Pass tick counter. */
void ui_draw_progressbar_indeterminate(ui_rect_t r, uint32_t tick);

/** Styled progress bar with custom colors. */
void ui_draw_progressbar_styled(ui_rect_t r, int value, int max,
                                uint32_t bar_color, uint32_t bg_color);

/** Draw spinner with up/down buttons. Returns true if value changed.
 *  State is mutated (value clamped to [min,max]). */
bool ui_draw_spinner(ui_rect_t r, ui_spinner_state_t *state,
                     int min, int max, int16_t mx, int16_t my, bool clicked);

/** Draw a toggle switch. Returns true if clicked. */
bool ui_draw_toggle(ui_rect_t r, bool on, bool enabled,
                    int16_t mx, int16_t my, bool clicked);

void gui_widgets_init(void);

#endif /* GUI_WIDGETS_H */
