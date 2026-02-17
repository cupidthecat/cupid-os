#ifndef DESKTOP_H
#define DESKTOP_H

#include "types.h"
#include "calendar.h"
#include "../drivers/vga.h"

/* Maximum desktop icons */
#define MAX_DESKTOP_ICONS 16

/* Taskbar dimensions */
#define TASKBAR_HEIGHT    24
#define TASKBAR_Y         (VGA_GFX_HEIGHT - TASKBAR_HEIGHT)
#define TASKBAR_BTN_MAX_W 120  /* Max width of a window button in the taskbar */
#define TASKBAR_BTN_START 120  /* X offset where window buttons begin */

/* Calendar popup dimensions */
#define CALENDAR_WIDTH    440
#define CALENDAR_HEIGHT   320

/* Desktop icon */
typedef struct {
    int16_t  x, y;
    char     label[32];
    void   (*launch)(void);
    bool     active;
} desktop_icon_t;

/* Initialize desktop (call after gui_init and mouse_init) */
void desktop_init(void);

/* Add a desktop icon */
void desktop_add_icon(int16_t x, int16_t y, const char *label,
                      void (*launch)(void));

/* Main event loop (never returns) */
void desktop_run(void);

/* Perform one redraw cycle (mouse + tick + repaint).
 * Call from blocking loops (e.g. getchar) to keep the GUI alive. */
void desktop_redraw_cycle(void);

/* Drawing helpers (used internally and by redraw) */
void desktop_draw_background(void);
void desktop_draw_taskbar(void);
void desktop_draw_icons(void);

/* Desktop background API (used by apps like BG Studio) */
#define DESKTOP_ANIM_THEME_TIME   0
#define DESKTOP_ANIM_THEME_HEARTS 1
#define DESKTOP_ANIM_THEME_KITTY  2
#define DESKTOP_ANIM_THEME_CLOUDS 3

void desktop_bg_set_mode_anim(void);
void desktop_bg_set_mode_solid(uint32_t color);
void desktop_bg_set_mode_gradient(uint32_t top_color, uint32_t bottom_color);
void desktop_bg_set_mode_tiled_pattern(int pattern, uint32_t fg, uint32_t bg);
int  desktop_bg_set_mode_tiled_bmp(const char *path);
int  desktop_bg_set_mode_bmp(const char *path);
int  desktop_bg_get_mode(void);           /* 0=anim,1=solid,2=bmp,3=gradient,4=tiled */
uint32_t desktop_bg_get_solid_color(void);
void desktop_bg_set_anim_theme(int theme);
int  desktop_bg_get_anim_theme(void);
int  desktop_bg_get_tiled_pattern(void);
int  desktop_bg_get_tiled_use_bmp(void);

#define DESKTOP_TILE_PATTERN_CHECKER 0
#define DESKTOP_TILE_PATTERN_DIAG    1
#define DESKTOP_TILE_PATTERN_DOTS    2

/* Taskbar hit-test: returns window ID or -1 */
int  desktop_hit_test_taskbar(int16_t mx, int16_t my);

/* Minimized fullscreen app support.
 * Runs the desktop event loop with a taskbar button for the minimized app.
 * Blocks until the user clicks the taskbar button to restore the app. */
void desktop_run_minimized_loop(const char *app_name);

/* Calendar popup */
extern calendar_state_t cal_state;
void desktop_toggle_calendar(void);
void desktop_close_calendar(void);
bool desktop_calendar_visible(void);

#endif
