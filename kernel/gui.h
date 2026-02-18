#ifndef GUI_H
#define GUI_H

#include "types.h"

#define GUI_OK                0
#define GUI_ERR_NO_MEMORY    -1
#define GUI_ERR_TOO_MANY     -2
#define GUI_ERR_INVALID_ID   -3
#define GUI_ERR_INVALID_ARGS -4

#define WINDOW_FLAG_VISIBLE  0x01
#define WINDOW_FLAG_FOCUSED  0x02
#define WINDOW_FLAG_DIRTY    0x04
#define WINDOW_FLAG_DRAGGING 0x08  /* being dragged - skip content redraw */
#define WINDOW_FLAG_RESIZING 0x10  /* being resized */

#define MAX_WINDOWS     16
#define TITLEBAR_H      14
#define CLOSE_BTN_SIZE  10
#define BORDER_W         1
#define WINDOW_CONTENT_TOP_PAD 0
#define WINDOW_CONTENT_BORDER  1

typedef struct window {
    uint32_t  id;
    uint32_t  owner_pid;
    int16_t   x, y;
    int16_t   prev_x, prev_y;  /* position before last drag move */
    uint16_t  width, height;
    char      title[64];
    uint8_t   flags;
    void     *app_data;
    void    (*redraw)(struct window *win);
    void    (*on_close)(struct window *win);
    int       key_queue[16];
    int       key_head;
    int       key_tail;
    uint32_t *content_cache;
    uint16_t  content_cache_w;
    uint16_t  content_cache_h;
} window_t;

typedef struct {
    bool   dragging;
    bool   resizing;
    int    window_id;
    int16_t drag_offset_x, drag_offset_y;
    int16_t start_mouse_x, start_mouse_y;
    uint16_t start_width, start_height;
} drag_state_t;

void      gui_init(void);

/* Returns window ID (>= 0) on success, negative on error */
int       gui_create_window(int16_t x, int16_t y, uint16_t w, uint16_t h,
                            const char *title);
int       gui_destroy_window(int wid);
int       gui_destroy_windows_by_owner(uint32_t owner_pid);

/* Drawing */
int       gui_draw_window(int wid);
/* draw_shadows: pass true when the background was repainted (shadows must
 * be redrawn); pass false when only window content changed and no windows
 * moved (shadow pixels in back_buffer are already correct from prev frame). */
void      gui_draw_all_windows(bool draw_shadows);

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
void      gui_mark_all_dirty(void);
void      gui_mark_visible_rects(void);
int       gui_cache_window_content(int wid);
/* True if focused window has no redraw callback (self-rendering CupidC app) */
bool      gui_focused_is_self_rendering(void);

/* During active drag/resize, returns the workspace region that must be
 * repainted under moving/resizing window(s). Returns false if unavailable. */
bool      gui_get_drag_invalidate_rect(int16_t *x, int16_t *y,
                                       uint16_t *w, uint16_t *h);

#endif
