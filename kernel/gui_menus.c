/**
 * gui_menus.c - Menu System & Toolbars for cupid-os
 *
 * Menu bars, context menus, toolbars, status bars,
 * tooltips, and keyboard shortcuts.
 */

#include "gui_menus.h"
#include "gui_themes.h"
#include "gfx2d.h"
#include "graphics.h"
#include "font_8x8.h"
#include "string.h"
#include "../drivers/vga.h"

#define MENU_ITEM_W_MIN 120
#define MENU_ITEM_H      20
#define MENU_PAD          6
#define TOOLBAR_BTN_SIZE 24
#define TOOLTIP_PAD       4

void gui_menus_init(void) {
    /* Nothing to initialize */
}

/* Menu Bar */

int ui_draw_menubar(ui_rect_t r, ui_menu_t *menus, int menu_count,
                    ui_menubar_state_t *state, int16_t mx, int16_t my,
                    bool clicked, bool released) {
    ui_theme_t *theme = ui_theme_get();
    int i, tx;
    int result = 0;

    /* Background */
    gfx2d_rect_fill(r.x, r.y, (int)r.w, (int)r.h, theme->menu_bg);
    gfx2d_hline(r.x, r.y + (int)r.h - 1, (int)r.w, theme->window_border);

    state->mouse_in_bar = ui_contains(r, mx, my);

    tx = r.x + 4;

    for (i = 0; i < menu_count; i++) {
        int tw = (int)strlen(menus[i].title) * FONT_W + MENU_PAD * 2;
        ui_rect_t title_r = ui_rect((int16_t)tx, r.y,
                                    (uint16_t)tw, r.h);
        bool hover = ui_contains(title_r, mx, my);
        bool is_open = (state->open_menu == i);

        /* Title highlight */
        if (is_open || hover) {
            gfx2d_rect_fill(tx, r.y, tw, (int)r.h, theme->menu_hover);
        }

        /* Title text */
        gfx2d_text(tx + MENU_PAD,
                   r.y + ((int)r.h - FONT_H) / 2,
                   menus[i].title, theme->menu_text, GFX2D_FONT_NORMAL);

        /* Open on click, or hover-switch when one is already open */
        if (clicked && hover) {
            state->open_menu = is_open ? -1 : i;
        } else if (hover && state->open_menu >= 0 && !is_open) {
            state->open_menu = i;
        }

        /* Draw dropdown if this menu is open */
        if (state->open_menu == i) {
            int j;
            int mw = MENU_ITEM_W_MIN;
            int mh;
            int16_t my2;
            int item_y;

            /* Calculate dropdown width (max of all item labels + shortcut) */
            for (j = 0; j < menus[i].item_count; j++) {
                int iw;
                if (menus[i].items[j].separator) continue;
                iw = (int)strlen(menus[i].items[j].label) * FONT_W + 40;
                if (menus[i].items[j].shortcut) {
                    iw += (int)strlen(menus[i].items[j].shortcut) * FONT_W + 20;
                }
                if (iw > mw) mw = iw;
            }

            mh = 0;
            for (j = 0; j < menus[i].item_count; j++) {
                mh += menus[i].items[j].separator ? 8 : MENU_ITEM_H;
            }

            my2 = (int16_t)(r.y + (int16_t)r.h);

            /* Menu background + border */
            gfx2d_rect_fill(tx, my2, mw, mh, theme->menu_bg);
            gfx2d_rect(tx, my2, mw, mh, theme->window_border);

            /* Shadow */
            gfx2d_hline(tx + 2, my2 + mh, mw, 0x00808080);
            gfx2d_vline(tx + mw, my2 + 2, mh, 0x00808080);

            /* Items */
            item_y = (int)my2;
            for (j = 0; j < menus[i].item_count; j++) {
                ui_menu_item_t *mi = &menus[i].items[j];

                if (mi->separator) {
                    /* Separator line */
                    gfx2d_hline(tx + 3, item_y + 3, mw - 6,
                                theme->menu_separator);
                    item_y += 8;
                    continue;
                }

                {
                    ui_rect_t item_r = ui_rect((int16_t)(tx + 1),
                                               (int16_t)item_y,
                                               (uint16_t)(mw - 2),
                                               MENU_ITEM_H);
                    bool item_hover = ui_contains(item_r, mx, my);

                    if (item_hover && mi->enabled) {
                        state->hover_item = j;
                        gfx2d_rect_fill(tx + 1, item_y, mw - 2,
                                        MENU_ITEM_H, theme->menu_hover);
                    }

                    /* Checkmark */
                    if (mi->checked) {
                        gfx2d_text(tx + 4,
                                   item_y + (MENU_ITEM_H - FONT_H) / 2,
                                   "*", theme->menu_text, GFX2D_FONT_NORMAL);
                    }

                    /* Label */
                    {
                        uint32_t col = mi->enabled ? theme->menu_text
                                                   : theme->menu_disabled_text;
                        gfx2d_text(tx + 20,
                                   item_y + (MENU_ITEM_H - FONT_H) / 2,
                                   mi->label, col, GFX2D_FONT_NORMAL);
                    }

                    /* Shortcut text */
                    if (mi->shortcut) {
                        int sw = (int)strlen(mi->shortcut) * FONT_W;
                        uint32_t col = mi->enabled ? theme->button_disabled_text
                                                   : theme->menu_disabled_text;
                        gfx2d_text(tx + mw - sw - 8,
                                   item_y + (MENU_ITEM_H - FONT_H) / 2,
                                   mi->shortcut, col, GFX2D_FONT_NORMAL);
                    }

                    /* Handle click */
                    if (released && item_hover && mi->enabled) {
                        result = mi->id;
                        state->open_menu = -1;
                    }
                }

                item_y += MENU_ITEM_H;
            }

            /* Dismiss if clicked outside */
            {
                ui_rect_t dropdown_r = ui_rect((int16_t)tx, my2,
                                               (uint16_t)mw,
                                               (uint16_t)mh);
                if (clicked && !ui_contains(dropdown_r, mx, my) &&
                    !ui_contains(title_r, mx, my)) {
                    state->open_menu = -1;
                }
            }
        }

        tx += tw;
    }

    return result;
}

ui_rect_t ui_menubar_content_rect(ui_rect_t window, int menubar_height) {
    return ui_rect(window.x,
                   (int16_t)(window.y + (int16_t)menubar_height),
                   window.w,
                   (uint16_t)((int)window.h - menubar_height));
}

/* Context Menu */

void ui_context_menu_show(ui_context_menu_state_t *state,
                          int16_t x, int16_t y) {
    state->visible = true;
    state->x = x;
    state->y = y;
    state->hover_item = -1;
}

int ui_draw_context_menu(ui_menu_item_t *items, int count,
                         ui_context_menu_state_t *state,
                         int16_t mx, int16_t my, bool clicked) {
    ui_theme_t *theme = ui_theme_get();
    int i, mw, mh, item_y, result;

    if (!state->visible) return 0;

    result = 0;

    /* Calculate dimensions */
    mw = MENU_ITEM_W_MIN;
    mh = 0;
    for (i = 0; i < count; i++) {
        if (items[i].separator) {
            mh += 8;
        } else {
            int iw = (int)strlen(items[i].label) * FONT_W + 40;
            if (items[i].shortcut) {
                iw += (int)strlen(items[i].shortcut) * FONT_W + 20;
            }
            if (iw > mw) mw = iw;
            mh += MENU_ITEM_H;
        }
    }

    /* Background + border + shadow */
    gfx2d_rect_fill(state->x, state->y, mw, mh, theme->menu_bg);
    gfx2d_rect(state->x, state->y, mw, mh, theme->window_border);
    gfx2d_hline(state->x + 2, state->y + mh, mw, 0x00808080);
    gfx2d_vline(state->x + mw, state->y + 2, mh, 0x00808080);

    state->hover_item = -1;
    item_y = (int)state->y;

    for (i = 0; i < count; i++) {
        if (items[i].separator) {
            gfx2d_hline(state->x + 3, item_y + 3, mw - 6,
                        theme->menu_separator);
            item_y += 8;
            continue;
        }

        {
            ui_rect_t ir = ui_rect((int16_t)(state->x + 1),
                                   (int16_t)item_y,
                                   (uint16_t)(mw - 2),
                                   MENU_ITEM_H);
            bool hover = ui_contains(ir, mx, my);

            if (hover && items[i].enabled) {
                state->hover_item = i;
                gfx2d_rect_fill(state->x + 1, item_y, mw - 2,
                                MENU_ITEM_H, theme->menu_hover);
            }

            /* Checkmark */
            if (items[i].checked) {
                gfx2d_text(state->x + 4,
                           item_y + (MENU_ITEM_H - FONT_H) / 2,
                           "*", theme->menu_text, GFX2D_FONT_NORMAL);
            }

            /* Label */
            {
                uint32_t col = items[i].enabled ? theme->menu_text
                                                : theme->menu_disabled_text;
                gfx2d_text(state->x + 20,
                           item_y + (MENU_ITEM_H - FONT_H) / 2,
                           items[i].label, col, GFX2D_FONT_NORMAL);
            }

            /* Shortcut */
            if (items[i].shortcut) {
                int sw = (int)strlen(items[i].shortcut) * FONT_W;
                gfx2d_text(state->x + mw - sw - 8,
                           item_y + (MENU_ITEM_H - FONT_H) / 2,
                           items[i].shortcut, theme->button_disabled_text,
                           GFX2D_FONT_NORMAL);
            }

            if (clicked && hover && items[i].enabled) {
                result = items[i].id;
                state->visible = false;
            }
        }

        item_y += MENU_ITEM_H;
    }

    /* Dismiss if clicked outside */
    {
        ui_rect_t menu_r = ui_rect(state->x, state->y,
                                   (uint16_t)mw, (uint16_t)mh);
        if (clicked && !ui_contains(menu_r, mx, my)) {
            state->visible = false;
            result = -1;
        }
    }

    return result;
}

void ui_context_menu_handle_input(ui_context_menu_state_t *state,
                                  ui_rect_t trigger_area,
                                  int16_t mx, int16_t my,
                                  uint8_t buttons) {
    /* Right-click (bit 1) inside trigger area */
    if ((buttons & 0x02) && ui_contains(trigger_area, mx, my)) {
        ui_context_menu_show(state, mx, my);
    }
}

/* Toolbar */

int ui_draw_toolbar(ui_rect_t r, ui_toolbar_button_t *buttons, int count,
                    ui_toolbar_state_t *state, int16_t mx, int16_t my,
                    bool clicked) {
    ui_theme_t *theme = ui_theme_get();
    int i, bx, result;

    result = 0;
    state->hover_button = -1;

    /* Background */
    gfx2d_rect_fill(r.x, r.y, (int)r.w, (int)r.h, theme->menu_bg);
    gfx2d_hline(r.x, r.y + (int)r.h - 1, (int)r.w, theme->window_border);

    bx = r.x + 2;

    for (i = 0; i < count; i++) {
        ui_rect_t br = ui_rect((int16_t)bx,
                               (int16_t)(r.y + 2),
                               TOOLBAR_BTN_SIZE,
                               TOOLBAR_BTN_SIZE);
        bool hover = ui_contains(br, mx, my) && buttons[i].enabled;
        bool pressed = buttons[i].toggle && buttons[i].pressed;

        if (hover) state->hover_button = i;

        /* Button background */
        if (pressed || (hover && clicked)) {
            ui_draw_panel(br, theme->button_face, true, false);
        } else if (hover) {
            ui_draw_panel(br, theme->menu_hover, true, true);
        }

        /* Icon (sprite) or label fallback */
        if (buttons[i].icon_sprite >= 0) {
            gfx2d_sprite_draw(buttons[i].icon_sprite,
                              bx + 4, r.y + 6);
        } else if (buttons[i].label) {
            /* Abbreviated: first char centered */
            char c[2];
            c[0] = buttons[i].label[0];
            c[1] = '\0';
            gfx2d_text(bx + (TOOLBAR_BTN_SIZE - FONT_W) / 2,
                       r.y + 2 + (TOOLBAR_BTN_SIZE - FONT_H) / 2,
                       c,
                       buttons[i].enabled ? theme->button_text
                                          : theme->button_disabled_text,
                       GFX2D_FONT_NORMAL);
        }

        /* Disabled overlay */
        if (!buttons[i].enabled) {
            /* Dithered gray overlay - just lighten visually */
        }

        /* Handle click */
        if (clicked && hover) {
            if (buttons[i].toggle) {
                buttons[i].pressed = !buttons[i].pressed;
            }
            result = buttons[i].id;
            state->pressed_button = i;
        }

        bx += TOOLBAR_BTN_SIZE + 2;
    }

    return result;
}

int ui_draw_toolbar_ex(ui_rect_t r, ui_toolbar_item_t *items, int count,
                       ui_toolbar_state_t *state, int16_t mx, int16_t my,
                       bool clicked) {
    ui_theme_t *theme = ui_theme_get();
    int i, bx, result;

    result = 0;
    state->hover_button = -1;

    gfx2d_rect_fill(r.x, r.y, (int)r.w, (int)r.h, theme->menu_bg);
    gfx2d_hline(r.x, r.y + (int)r.h - 1, (int)r.w, theme->window_border);

    bx = r.x + 2;

    for (i = 0; i < count; i++) {
        switch (items[i].type) {
        case UI_TOOLBAR_SEPARATOR:
            gfx2d_vline(bx + 2, r.y + 3, (int)r.h - 6, theme->menu_separator);
            bx += 8;
            break;

        case UI_TOOLBAR_SPACER:
            /* Spacer - skip remaining width evenly */
            bx += 16;
            break;

        case UI_TOOLBAR_BUTTON: {
            ui_toolbar_button_t *btn = &items[i].button;
            ui_rect_t br = ui_rect((int16_t)bx,
                                   (int16_t)(r.y + 2),
                                   TOOLBAR_BTN_SIZE,
                                   TOOLBAR_BTN_SIZE);
            bool hover = ui_contains(br, mx, my) && btn->enabled;
            bool pressed = btn->toggle && btn->pressed;

            if (hover) state->hover_button = i;

            if (pressed || (hover && clicked)) {
                ui_draw_panel(br, theme->button_face, true, false);
            } else if (hover) {
                ui_draw_panel(br, theme->menu_hover, true, true);
            }

            if (btn->icon_sprite >= 0) {
                gfx2d_sprite_draw(btn->icon_sprite, bx + 4, r.y + 6);
            } else if (btn->label) {
                char c[2];
                c[0] = btn->label[0];
                c[1] = '\0';
                gfx2d_text(bx + (TOOLBAR_BTN_SIZE - FONT_W) / 2,
                           r.y + 2 + (TOOLBAR_BTN_SIZE - FONT_H) / 2,
                           c,
                           btn->enabled ? theme->button_text
                                        : theme->button_disabled_text,
                           GFX2D_FONT_NORMAL);
            }

            if (clicked && hover) {
                if (btn->toggle) btn->pressed = !btn->pressed;
                result = btn->id;
                state->pressed_button = i;
            }

            bx += TOOLBAR_BTN_SIZE + 2;
            break;
        }
        }
    }

    return result;
}

/* Status Bar */

void ui_draw_statusbar(ui_rect_t r, ui_statusbar_section_t *sections,
                       int count) {
    ui_theme_t *theme = ui_theme_get();
    int i, sx;
    int flex_count = 0;
    int fixed_total = 0;
    int flex_w;

    /* Background */
    gfx2d_rect_fill(r.x, r.y, (int)r.w, (int)r.h, theme->menu_bg);
    gfx2d_hline(r.x, r.y, (int)r.w, theme->window_border);

    /* Calculate flexible space */
    for (i = 0; i < count; i++) {
        if (sections[i].width > 0) {
            fixed_total += sections[i].width;
        } else {
            flex_count++;
        }
    }

    flex_w = 0;
    if (flex_count > 0) {
        flex_w = ((int)r.w - fixed_total - (count - 1) * 2) / flex_count;
        if (flex_w < 20) flex_w = 20;
    }

    sx = r.x + 2;

    for (i = 0; i < count; i++) {
        int sec_w = (sections[i].width > 0) ? sections[i].width : flex_w;
        ui_rect_t sr = ui_rect((int16_t)sx, (int16_t)(r.y + 2),
                               (uint16_t)sec_w,
                               (uint16_t)((int)r.h - 4));

        /* Section divider */
        if (i > 0) {
            gfx2d_vline(sx - 1, r.y + 2, (int)r.h - 4, theme->window_border);
        }

        /* Text */
        if (sections[i].text) {
            ui_draw_label(sr, sections[i].text, theme->button_text,
                          sections[i].align);
        }

        sx += sec_w + 2;
    }
}

void ui_draw_statusbar_simple(ui_rect_t r, const char *text) {
    ui_theme_t *theme = ui_theme_get();
    gfx2d_rect_fill(r.x, r.y, (int)r.w, (int)r.h, theme->menu_bg);
    gfx2d_hline(r.x, r.y, (int)r.w, theme->window_border);

    if (text) {
        gfx2d_text(r.x + 4, r.y + ((int)r.h - FONT_H) / 2,
                   text, theme->button_text, GFX2D_FONT_NORMAL);
    }
}

/* Tooltip */

void ui_tooltip_update(ui_tooltip_state_t *state, const char *text,
                       int16_t mx, int16_t my, uint32_t tick) {
    if (text) {
        if (state->text != text) {
            state->text = text;
            state->show_timer = tick;
            state->visible = false;
        }
        if (!state->visible && (tick - state->show_timer) >= UI_TOOLTIP_DELAY) {
            state->visible = true;
            state->x = (int16_t)(mx + 12);
            state->y = (int16_t)(my + 16);
        }
    } else {
        state->visible = false;
        state->text = NULL;
    }
}

void ui_draw_tooltip(ui_tooltip_state_t *state) {
    ui_theme_t *theme = ui_theme_get();
    int tw, th;

    if (!state->visible || !state->text) return;

    tw = (int)strlen(state->text) * FONT_W + TOOLTIP_PAD * 2;
    th = FONT_H + TOOLTIP_PAD * 2;

    /* Ensure on screen */
    if (state->x + tw > 640) state->x = (int16_t)(640 - tw);
    if (state->y + th > 480) state->y = (int16_t)(state->y - th - 20);

    /* Background + border */
    gfx2d_rect_fill(state->x, state->y, tw, th, theme->input_bg);
    gfx2d_rect(state->x, state->y, tw, th, theme->window_border);

    /* Text */
    gfx2d_text(state->x + TOOLTIP_PAD, state->y + TOOLTIP_PAD,
               state->text, theme->button_text, GFX2D_FONT_NORMAL);
}

/* Keyboard Shortcuts */

int ui_shortcuts_handle(ui_shortcut_t *shortcuts, int count,
                        uint8_t scancode, bool ctrl, bool alt, bool shift) {
    int i;
    for (i = 0; i < count; i++) {
        if ((uint8_t)shortcuts[i].key_scancode == scancode &&
            shortcuts[i].ctrl  == ctrl &&
            shortcuts[i].alt   == alt &&
            shortcuts[i].shift == shift) {
            return shortcuts[i].menu_id;
        }
    }
    return 0;
}
