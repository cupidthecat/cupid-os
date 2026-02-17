#ifndef VGA_GFX_H
#define VGA_GFX_H

#include "../kernel/types.h"

/* VBE 640x480, 32bpp true color */
#define VGA_GFX_WIDTH   640
#define VGA_GFX_HEIGHT  480
#define VGA_GFX_BPP     4       /* bytes per pixel */
#define VGA_GFX_PIXELS  (VGA_GFX_WIDTH * VGA_GFX_HEIGHT)
#define VGA_GFX_SIZE    (VGA_GFX_PIXELS * VGA_GFX_BPP)

#define COLOR_BLACK         0x00000000U  /* Pure black          */
#define COLOR_WINDOW_BG     0x00FFF0F5U  /* Soft rose white     */
#define COLOR_TITLEBAR      0x00B8DDFFU  /* Powder blue         */
#define COLOR_HIGHLIGHT     0x00FFFFF0U  /* Ivory               */
#define COLOR_TASKBAR       0x00E8D8F8U  /* Lavender mist       */
#define COLOR_BORDER        0x009898A0U  /* Cool gray           */
#define COLOR_TEXT          0x00282830U  /* Dark charcoal       */
#define COLOR_TEXT_LIGHT    0x00F8F8F8U  /* Snow white          */
#define COLOR_DESKTOP_BG    0x00FFE8F0U  /* Pink blush          */
#define COLOR_BUTTON        0x00C0D8FFU  /* Periwinkle          */
#define COLOR_BUTTON_HOVER  0x00D8E8FFU  /* Light periwinkle    */
#define COLOR_TITLE_UNFOC   0x00C8C8D0U  /* Silver              */
#define COLOR_CLOSE_BG      0x00FF9090U  /* Coral red           */
#define COLOR_TASKBAR_ACT   0x00D0C0F0U  /* Active violet       */
#define COLOR_TERM_BG       0x00141418U  /* Near-black          */
#define COLOR_CURSOR        0x00F0F0F0U  /* Off-white           */

/* Initialize VBE graphics (reads LFB address stored by bootloader) */
void vga_init_vbe(void);

/* Get direct pointer to back buffer (or LFB if no back buffer) */
uint32_t *vga_get_framebuffer(void);

/* Clear entire screen to a single color */
void vga_clear_screen(uint32_t color);

/* Copy back buffer to linear framebuffer */
void vga_flip(void);

/* Mark a dirty rectangle for the next vga_flip().
 * Rectangle is clipped to screen bounds; multiple calls are unioned. */
void vga_mark_dirty(int x, int y, int w, int h);

/* Mark the full screen dirty for the next vga_flip(). */
void vga_mark_dirty_full(void);

/* Enable/disable retrace wait inside vga_flip (default: enabled). */
void vga_set_vsync_wait(bool enabled);

/* Returns true if at least ~16ms have passed since the last vga_flip()
 * (~60 fps cap). Use this to skip rendering work when the frame budget
 * has not yet elapsed. */
bool vga_flip_ready(void);

/* Get pointer to the currently displayed LFB page (for direct cursor updates) */
uint32_t *vga_get_display_buffer(void);

#endif
