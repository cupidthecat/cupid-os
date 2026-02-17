/**
 * gui_events.h - Event System & Dialogs for cupid-os
 *
 * Event dispatching, message box, input dialog,
 * color picker, progress dialog.
 */
#ifndef GUI_EVENTS_H
#define GUI_EVENTS_H

#include "types.h"
#include "ui.h"
#include "gui.h"

typedef enum {
    UI_EVENT_NONE = 0,
    UI_EVENT_CLICK,
    UI_EVENT_DOUBLE_CLICK,
    UI_EVENT_RIGHT_CLICK,
    UI_EVENT_MOUSE_DOWN,
    UI_EVENT_MOUSE_UP,
    UI_EVENT_MOUSE_MOVE,
    UI_EVENT_MOUSE_ENTER,
    UI_EVENT_MOUSE_LEAVE,
    UI_EVENT_KEY_DOWN,
    UI_EVENT_KEY_UP,
    UI_EVENT_CHAR_INPUT,
    UI_EVENT_FOCUS_GAINED,
    UI_EVENT_FOCUS_LOST,
    UI_EVENT_VALUE_CHANGED,
    UI_EVENT_SELECTION_CHANGED,
    UI_EVENT_MENU_ITEM_CLICKED,
    UI_EVENT_WINDOW_CLOSE
} ui_event_type_t;

typedef struct {
    ui_event_type_t type;
    int16_t  mouse_x, mouse_y;
    uint8_t  mouse_buttons;
    uint8_t  key_scancode;
    char     character;
    int      widget_id;
    int      value;         /* for VALUE_CHANGED events */
    void    *user_data;
} ui_event_t;

typedef void (*ui_event_callback_t)(ui_event_t *event, void *context);

typedef struct {
    ui_event_callback_t callback;
    void              *context;
    int                widget_id;  /* 0 = all widgets */
    ui_event_type_t    filter;     /* 0 = all events  */
} ui_event_handler_t;

#define UI_MAX_EVENT_HANDLERS 16

void ui_register_handler(window_t *win, ui_event_handler_t handler);
void ui_emit_event(window_t *win, ui_event_t *event);
void ui_process_events(window_t *win);

typedef enum {
    UI_MSGBOX_INFO,
    UI_MSGBOX_WARNING,
    UI_MSGBOX_ERROR,
    UI_MSGBOX_QUESTION
} ui_msgbox_type_t;

typedef enum {
    UI_MSGBOX_OK         = 1,
    UI_MSGBOX_OK_CANCEL  = 2,
    UI_MSGBOX_YES_NO     = 3,
    UI_MSGBOX_YES_NO_CANCEL = 4,
    UI_MSGBOX_RETRY_CANCEL  = 5
} ui_msgbox_buttons_t;

typedef enum {
    UI_MSGBOX_RESULT_OK     = 1,
    UI_MSGBOX_RESULT_CANCEL = 2,
    UI_MSGBOX_RESULT_YES    = 3,
    UI_MSGBOX_RESULT_NO     = 4,
    UI_MSGBOX_RESULT_RETRY  = 5
} ui_msgbox_result_t;

/* Internal state for message box rendering */
typedef struct {
    bool active;
    const char *title;
    const char *message;
    ui_msgbox_type_t    type;
    ui_msgbox_buttons_t buttons;
    ui_msgbox_result_t  result;
    int hover_btn;
} ui_msgbox_state_t;

ui_msgbox_result_t ui_msgbox(const char *title, const char *message,
                              ui_msgbox_type_t type,
                              ui_msgbox_buttons_t buttons);

/* Draw a message box frame (non-blocking, call each frame while active) */
ui_msgbox_result_t ui_msgbox_draw(ui_msgbox_state_t *state,
                                   int16_t mx, int16_t my, bool clicked);

typedef struct {
    bool   active;
    char  *buffer;
    int    buffer_size;
    int    cursor;
    int    hover_btn;   /* 0=none, 1=OK, 2=Cancel */
    bool   confirmed;
    bool   cancelled;
} ui_input_dialog_state_t;

/* Non-blocking: init, draw each frame, check result */
void ui_input_dialog_init(ui_input_dialog_state_t *state,
                          char *buffer, int buffer_size);

int  ui_input_dialog_draw(ui_input_dialog_state_t *state,
                          const char *title, const char *prompt,
                          int16_t mx, int16_t my, bool clicked,
                          uint8_t key, char ch);

typedef struct {
    uint32_t selected_color;
    int hue;            /* 0-359 */
    int saturation;     /* 0-255 */
    int value;          /* 0-255 (HSV) */
    int red, green, blue;
    uint32_t recent_colors[16];
} ui_colorpicker_state_t;

bool ui_draw_colorpicker(ui_rect_t r, ui_colorpicker_state_t *state,
                         int16_t mx, int16_t my, bool clicked);

bool ui_draw_color_swatch(ui_rect_t r, uint32_t color,
                          int16_t mx, int16_t my, bool clicked);

typedef struct {
    bool        active;
    bool        cancelable;
    bool        cancelled;
    const char *title;
    const char *message;
    int         value;
    int         max_value;
} ui_progress_dialog_t;

void ui_progress_dialog_init(ui_progress_dialog_t *dlg,
                             const char *title, const char *message,
                             bool cancelable);

void ui_progress_dialog_update(ui_progress_dialog_t *dlg,
                               int value, int max_val);

void ui_progress_dialog_set_message(ui_progress_dialog_t *dlg,
                                    const char *msg);

bool ui_progress_dialog_is_canceled(ui_progress_dialog_t *dlg);

void ui_progress_dialog_draw(ui_progress_dialog_t *dlg,
                             int16_t mx, int16_t my, bool clicked);

void ui_progress_dialog_close(ui_progress_dialog_t *dlg);

/* Init */
void gui_events_init(void);

#endif /* GUI_EVENTS_H */
