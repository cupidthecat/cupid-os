/**
 * gui_themes.h - Theming System for cupid-os
 *
 * Theme palettes, style properties, built-in theme presets,
 * and theme load/save from .theme files.
 */
#ifndef GUI_THEMES_H
#define GUI_THEMES_H

#include "types.h"

typedef struct {
    /* Window colors */
    uint32_t window_bg;
    uint32_t window_border;
    uint32_t titlebar_active_start;
    uint32_t titlebar_active_end;
    uint32_t titlebar_inactive_start;
    uint32_t titlebar_inactive_end;
    uint32_t titlebar_text;

    /* Control colors */
    uint32_t button_face;
    uint32_t button_highlight;
    uint32_t button_shadow;
    uint32_t button_text;
    uint32_t button_disabled_text;

    /* Input controls */
    uint32_t input_bg;
    uint32_t input_border;
    uint32_t input_text;
    uint32_t input_selection;

    /* List/menu colors */
    uint32_t menu_bg;
    uint32_t menu_hover;
    uint32_t menu_selected;
    uint32_t menu_text;
    uint32_t menu_disabled_text;
    uint32_t menu_separator;

    /* Accent colors */
    uint32_t accent_primary;
    uint32_t accent_secondary;
    uint32_t link_color;

    /* Status colors */
    uint32_t success;
    uint32_t warning;
    uint32_t error;
    uint32_t info;

    /* Desktop/taskbar */
    uint32_t desktop_bg;
    uint32_t taskbar_bg;
    uint32_t taskbar_text;
} ui_theme_t;

typedef struct {
    int  window_shadow_offset;
    int  window_shadow_blur;
    int  window_border_width;
    int  button_border_width;
    int  corner_radius;         /* 0 = square corners     */
    bool use_gradients;
    bool use_shadows;
    bool use_animations;
    int  animation_duration_ms;
} ui_style_t;

void        ui_theme_set(const ui_theme_t *theme);
ui_theme_t *ui_theme_get(void);
void        ui_theme_reset_default(void);

void        ui_style_set(const ui_style_t *style);
ui_style_t *ui_style_get(void);

/* Load/save from .theme file (INI-style) */
int         ui_theme_load(const char *path);
int         ui_theme_save(const char *path);

extern const ui_theme_t UI_THEME_WINDOWS95;
extern const ui_theme_t UI_THEME_PASTEL_DREAM;
extern const ui_theme_t UI_THEME_DARK_MODE;
extern const ui_theme_t UI_THEME_HIGH_CONTRAST;
extern const ui_theme_t UI_THEME_RETRO_AMBER;
extern const ui_theme_t UI_THEME_VAPORWAVE;

/* Init */
void gui_themes_init(void);

#endif /* GUI_THEMES_H */
