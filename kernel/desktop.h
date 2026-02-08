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

/* Taskbar hit-test: returns window ID or -1 */
int  desktop_hit_test_taskbar(int16_t mx, int16_t my);

/* Calendar popup */
extern calendar_state_t cal_state;
void desktop_toggle_calendar(void);
void desktop_close_calendar(void);
bool desktop_calendar_visible(void);

#endif
