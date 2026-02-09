/**
 * gui_menus.h - Menu System & Toolbars for cupid-os
 *
 * Menu bars, context menus, toolbars, status bars,
 * tooltips, and keyboard shortcut handling.
 */
#ifndef GUI_MENUS_H
#define GUI_MENUS_H

#include "types.h"
#include "ui.h"

/* ── Menu Item ────────────────────────────────────────────────────── */

typedef struct {
    const char *label;
    int         id;          /* unique ID for this item   */
    bool        enabled;
    bool        checked;     /* for checkable items       */
    bool        separator;   /* draw as separator line    */
    const char *shortcut;    /* display text e.g. "Ctrl+S"*/
    int         shortcut_key;/* scancode (0=none)         */
} ui_menu_item_t;

typedef struct {
    const char     *title;
    ui_menu_item_t *items;
    int             item_count;
} ui_menu_t;

/* ── Menu Bar ─────────────────────────────────────────────────────── */

typedef struct {
    int  open_menu;     /* index of open dropdown (-1 = none) */
    int  hover_item;    /* hovered item inside open menu      */
    bool mouse_in_bar;
} ui_menubar_state_t;

int  ui_draw_menubar(ui_rect_t r, ui_menu_t *menus, int menu_count,
                     ui_menubar_state_t *state, int16_t mx, int16_t my,
                     bool clicked, bool released);

ui_rect_t ui_menubar_content_rect(ui_rect_t window, int menubar_height);

/* ── Context Menu ─────────────────────────────────────────────────── */

typedef struct {
    bool  visible;
    int16_t x, y;
    int  hover_item;
} ui_context_menu_state_t;

void ui_context_menu_show(ui_context_menu_state_t *state,
                          int16_t x, int16_t y);

int  ui_draw_context_menu(ui_menu_item_t *items, int count,
                          ui_context_menu_state_t *state,
                          int16_t mx, int16_t my, bool clicked);

void ui_context_menu_handle_input(ui_context_menu_state_t *state,
                                  ui_rect_t trigger_area,
                                  int16_t mx, int16_t my,
                                  uint8_t buttons);

/* ── Toolbar ──────────────────────────────────────────────────────── */

typedef struct {
    int         id;
    const char *label;
    int         icon_sprite;  /* sprite handle or -1 */
    bool        enabled;
    bool        toggle;
    bool        pressed;      /* current toggle state */
    const char *tooltip;
} ui_toolbar_button_t;

typedef struct {
    int      hover_button;
    int      pressed_button;
    uint32_t tooltip_timer;
} ui_toolbar_state_t;

int  ui_draw_toolbar(ui_rect_t r, ui_toolbar_button_t *buttons,
                     int count, ui_toolbar_state_t *state,
                     int16_t mx, int16_t my, bool clicked);

typedef enum {
    UI_TOOLBAR_BUTTON,
    UI_TOOLBAR_SEPARATOR,
    UI_TOOLBAR_SPACER
} ui_toolbar_item_type_t;

typedef struct {
    ui_toolbar_item_type_t type;
    ui_toolbar_button_t    button;
} ui_toolbar_item_t;

int  ui_draw_toolbar_ex(ui_rect_t r, ui_toolbar_item_t *items,
                        int count, ui_toolbar_state_t *state,
                        int16_t mx, int16_t my, bool clicked);

/* ── Status Bar ───────────────────────────────────────────────────── */

typedef struct {
    const char *text;
    int         width;      /* 0 = flexible, >0 = fixed */
    ui_align_t  align;
} ui_statusbar_section_t;

void ui_draw_statusbar(ui_rect_t r, ui_statusbar_section_t *sections,
                       int count);
void ui_draw_statusbar_simple(ui_rect_t r, const char *text);

/* ── Tooltip ──────────────────────────────────────────────────────── */

#define UI_TOOLTIP_DELAY  500   /* ms before tooltip shows */

typedef struct {
    bool        visible;
    int16_t     x, y;
    const char *text;
    uint32_t    show_timer;
} ui_tooltip_state_t;

void ui_tooltip_update(ui_tooltip_state_t *state, const char *text,
                       int16_t mx, int16_t my, uint32_t tick);
void ui_draw_tooltip(ui_tooltip_state_t *state);

/* ── Keyboard Shortcuts ───────────────────────────────────────────── */

typedef struct {
    int  key_scancode;
    bool ctrl;
    bool alt;
    bool shift;
    int  menu_id;           /* associated menu item */
} ui_shortcut_t;

int  ui_shortcuts_handle(ui_shortcut_t *shortcuts, int count,
                         uint8_t scancode, bool ctrl,
                         bool alt, bool shift);

/* Common shortcut scancodes */
#define UI_KEY_CTRL_N  0x31
#define UI_KEY_CTRL_O  0x18
#define UI_KEY_CTRL_S  0x1F
#define UI_KEY_CTRL_Z  0x2C
#define UI_KEY_CTRL_X  0x2D
#define UI_KEY_CTRL_C  0x2E
#define UI_KEY_CTRL_V  0x2F

/* Init */
void gui_menus_init(void);

#endif /* GUI_MENUS_H */
