#ifndef GUI_H
#define GUI_H

#include "types.h"

/* ── Error codes ──────────────────────────────────────────────────── */
#define GUI_OK                0
#define GUI_ERR_NO_MEMORY    -1
#define GUI_ERR_TOO_MANY     -2
#define GUI_ERR_INVALID_ID   -3
#define GUI_ERR_INVALID_ARGS -4

/* ── Window flags ─────────────────────────────────────────────────── */
#define WINDOW_FLAG_VISIBLE  0x01
#define WINDOW_FLAG_FOCUSED  0x02
#define WINDOW_FLAG_DIRTY    0x04
#define WINDOW_FLAG_DRAGGING 0x08  /* being dragged — skip content redraw */
#define WINDOW_FLAG_RESIZING 0x10  /* being resized */

/* ── Constants ────────────────────────────────────────────────────── */
#define MAX_WINDOWS     16
#define TITLEBAR_H      14
#define CLOSE_BTN_SIZE  10
#define BORDER_W         1

/* ── Window structure ─────────────────────────────────────────────── */
typedef struct window {
    uint32_t  id;
    int16_t   x, y;
    int16_t   prev_x, prev_y;  /* position before last drag move */
    uint16_t  width, height;
    char      title[64];
    uint8_t   flags;
    void     *app_data;
    void    (*redraw)(struct window *win);
    void    (*on_close)(struct window *win);
} window_t;

/* ── Drag state ───────────────────────────────────────────────────── */
typedef struct {
    bool   dragging;
    bool   resizing;
    int    window_id;
    int16_t drag_offset_x, drag_offset_y;
    int16_t start_mouse_x, start_mouse_y;
    uint16_t start_width, start_height;
} drag_state_t;

/* ── Public API ───────────────────────────────────────────────────── */
void      gui_init(void);

/* Returns window ID (>= 0) on success, negative on error */
int       gui_create_window(int16_t x, int16_t y, uint16_t w, uint16_t h,
                            const char *title);
int       gui_destroy_window(int wid);

/* Drawing */
int       gui_draw_window(int wid);
void      gui_draw_all_windows(void);

/* Focus */
int       gui_set_focus(int wid);
window_t *gui_get_focused_window(void);
window_t *gui_get_window(int wid);

/* Input dispatch */
void      gui_handle_mouse(int16_t mx, int16_t my, uint8_t buttons,
                           uint8_t prev_buttons);
void      gui_handle_key(uint8_t scancode, char character);

/* Hit testing (returns window ID or -1) */
int       gui_hit_test_titlebar(int16_t mx, int16_t my);
int       gui_hit_test_close(int16_t mx, int16_t my);
int       gui_hit_test_window(int16_t mx, int16_t my);

/* Query */
int       gui_window_count(void);
bool      gui_any_dirty(void);
window_t *gui_get_window_by_index(int i);

/* True if any window was created, destroyed, or moved since last clear */
bool      gui_layout_changed(void);
void      gui_clear_layout_changed(void);

/* Drag state query */
bool      gui_is_dragging_any(void);
bool      gui_is_dragging_window(int wid);


#endif
