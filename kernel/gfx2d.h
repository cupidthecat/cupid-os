/**
 * gfx2d.h - 2D graphics library for cupid-os
 *
 * Software-rendered 2D graphics exposed as CupidC kernel bindings.
 * Supports alpha blending, gradients, shadows, retro effects, sprites,
 * and Win95/demo-scene aesthetics.
 */

#ifndef GFX2D_H
#define GFX2D_H

#include "types.h"

/* Font size constants (also registered as CupidC bindings) */
#define GFX2D_FONT_SMALL 0  /* 6x8 clipped  */
#define GFX2D_FONT_NORMAL 1 /* 8x8 standard */
#define GFX2D_FONT_LARGE 2  /* 8x8 scaled 2x */

/* Dither pattern constants */
#define GFX2D_DITHER_CHECKER 0
#define GFX2D_DITHER_HLINES 1
#define GFX2D_DITHER_VLINES 2
#define GFX2D_DITHER_DIAGONAL 3

/* Blend modes (set with gfx2d_blend_mode) */
#define GFX2D_BLEND_NORMAL 0
#define GFX2D_BLEND_ADD 1
#define GFX2D_BLEND_MULTIPLY 2
#define GFX2D_BLEND_SCREEN 3
#define GFX2D_BLEND_OVERLAY 4

/* Initialize the 2D graphics library (call after gfx_init) */
void gfx2d_init(void);

/* Update cached framebuffer pointer (call after vga_flip) */
void gfx2d_set_framebuffer(uint32_t *fb);

void gfx2d_clear(uint32_t color);
void gfx2d_flip(void);
uint32_t *gfx2d_get_active_fb(void);
int gfx2d_width(void);
int gfx2d_height(void);

void gfx2d_pixel(int x, int y, uint32_t color);
uint32_t gfx2d_getpixel(int x, int y);
void gfx2d_line(int x1, int y1, int x2, int y2, uint32_t color);
void gfx2d_hline(int x, int y, int w, uint32_t color);
void gfx2d_vline(int x, int y, int h, uint32_t color);

void gfx2d_rect(int x, int y, int w, int h, uint32_t color);
void gfx2d_rect_fill(int x, int y, int w, int h, uint32_t color);
void gfx2d_rect_round(int x, int y, int w, int h, int r, uint32_t color);
void gfx2d_rect_round_fill(int x, int y, int w, int h, int r, uint32_t color);

void gfx2d_circle(int x, int y, int r, uint32_t color);
void gfx2d_circle_fill(int x, int y, int r, uint32_t color);
void gfx2d_ellipse(int x, int y, int rx, int ry, uint32_t color);
void gfx2d_ellipse_fill(int x, int y, int rx, int ry, uint32_t color);

void gfx2d_rect_fill_alpha(int x, int y, int w, int h, uint32_t argb);
void gfx2d_pixel_alpha(int x, int y, uint32_t argb);

void gfx2d_gradient_h(int x, int y, int w, int h, uint32_t c1, uint32_t c2);
void gfx2d_gradient_v(int x, int y, int w, int h, uint32_t c1, uint32_t c2);
void gfx2d_gradient_radial(int x, int y, int w, int h,
                           uint32_t inner, uint32_t outer);

uint32_t gfx2d_color_hsv(int hue, int sat, int val);
void gfx2d_color_picker_draw_sv(int x, int y, int w, int h,
                                int hue, int sat, int val);
void gfx2d_color_picker_draw_hue(int x, int y, int w, int h, int hue);
int gfx2d_color_picker_pick_hue(int x, int y, int w, int h, int mx, int my);
int gfx2d_color_picker_pick_sat(int x, int y, int w, int h, int mx, int my);
int gfx2d_color_picker_pick_val(int x, int y, int w, int h, int mx, int my);

void gfx2d_glow_circle(int cx, int cy, int radius, uint32_t color, int alpha);
void gfx2d_light_sweep(int x, int y, int w, int h, int tick, uint32_t color,
                       int alpha);

void gfx2d_shadow(int x, int y, int w, int h, int blur, uint32_t color);

void gfx2d_dither_rect(int x, int y, int w, int h, uint32_t c1, uint32_t c2,
                       int pattern);

void gfx2d_scanlines(int x, int y, int w, int h, int alpha);

void gfx2d_clip_set(int x, int y, int w, int h);
void gfx2d_clip_clear(void);

int gfx2d_sprite_load(const char *path);
void gfx2d_sprite_free(int handle);
void gfx2d_sprite_draw(int handle, int x, int y);
void gfx2d_sprite_draw_alpha(int handle, int x, int y);
void gfx2d_sprite_draw_scaled(int handle, int x, int y, int w, int h);
int gfx2d_sprite_width(int handle);
int gfx2d_sprite_height(int handle);
uint32_t gfx2d_sprite_get_pixel(int handle, int x, int y);

void gfx2d_text(int x, int y, const char *str, uint32_t color, int font);
void gfx2d_text_shadow(int x, int y, const char *str, uint32_t color,
                       uint32_t shadow_color, int font);
void gfx2d_text_outline(int x, int y, const char *str, uint32_t color,
                        uint32_t outline_color, int font);
void gfx2d_text_wrap(int x, int y, int w, const char *str, uint32_t color,
                     int font);
int gfx2d_text_width(const char *str, int font);
int gfx2d_text_height(int font);

void gfx2d_vignette(int strength);
void gfx2d_pixelate(int x, int y, int w, int h, int block_size);
void gfx2d_invert(int x, int y, int w, int h);
void gfx2d_tint(int x, int y, int w, int h, uint32_t color, int alpha);

void gfx2d_bevel(int x, int y, int w, int h, int raised);
void gfx2d_panel(int x, int y, int w, int h);
void gfx2d_titlebar(int x, int y, int w, int h, uint32_t c1, uint32_t c2);

void gfx2d_copper_bars(int y, int count, int spacing, int *colors);
void gfx2d_plasma(int x, int y, int w, int h, int tick);
void gfx2d_checkerboard(int x, int y, int w, int h, int size, uint32_t c1,
                        uint32_t c2);

void gfx2d_blend_mode(int mode);

#define GFX2D_MAX_SURFACES 8

int gfx2d_surface_alloc(int w, int h); /* returns handle 0..7, or -1 */
void gfx2d_surface_free(int handle);
void gfx2d_surface_fill(int handle, uint32_t color);
void gfx2d_surface_set_active(int handle); /* redirect drawing here */
void gfx2d_surface_unset_active(void);     /* back to screen */
void gfx2d_surface_blit(int handle, int x, int y);
void gfx2d_surface_blit_alpha(int handle, int x, int y, int alpha);
void gfx2d_surface_blit_scaled(int handle, int x, int y, int w, int h);
uint32_t *gfx2d_surface_data(int handle, int *w, int *h);
void gfx2d_capture_screen_to_surface(int handle);

int gfx2d_tween_linear(int t, int start, int end, int dur);
int gfx2d_tween_ease_in_out(int t, int start, int end, int dur);
int gfx2d_tween_bounce(int t, int start, int end, int dur);
int gfx2d_tween_elastic(int t, int start, int end, int dur);

#define GFX2D_MAX_PARTICLE_SYSTEMS 4
#define GFX2D_MAX_PARTICLES_PER_SYS 64

int gfx2d_particles_create(void); /* returns handle */
void gfx2d_particles_free(int handle);
void gfx2d_particle_emit(int handle, int x, int y, int vx, int vy,
                         uint32_t color, int life);
void gfx2d_particles_update(int handle, int gravity);
void gfx2d_particles_draw(int handle);
int gfx2d_particles_alive(int handle); /* count of live particles */

void gfx2d_tri(int x0, int y0, int x1, int y1, int x2, int y2,
               uint32_t color);
void gfx2d_bezier(int x0, int y0, int x1, int y1, int x2, int y2,
                  uint32_t color);
void gfx2d_tri_fill(int x0, int y0, int x1, int y1, int x2, int y2,
                    uint32_t color);
void gfx2d_tri_fill_gradient(int x0, int y0, uint32_t c0,
                             int x1, int y1, uint32_t c1,
                             int x2, int y2, uint32_t c2);
void gfx2d_line_thick(int x0, int y0, int x1, int y1,
                      int thickness, uint32_t color);
void gfx2d_circle_thick(int x, int y, int r, int thickness, uint32_t color);
void gfx2d_line_aa(int x0, int y0, int x1, int y1, uint32_t color);
void gfx2d_flood_fill(int x, int y, uint32_t color);

void gfx2d_fullscreen_enter(void);
void gfx2d_fullscreen_exit(void);
int gfx2d_fullscreen_active(void); /* returns 1 if fullscreen mode is active */
int gfx2d_should_quit(void);        /* returns 1 if program was killed via ps/kill */

#define GFX2D_TOOLBAR_H       14
#define GFX2D_TOOLBAR_NONE     0
#define GFX2D_TOOLBAR_CLOSE    1
#define GFX2D_TOOLBAR_MINIMIZE 2

#define GFX2D_WINDOW_NONE      0
#define GFX2D_WINDOW_CLOSE     1
#define GFX2D_WINDOW_MINIMIZE  2

void gfx2d_window_reset(int x, int y, int w, int h);
int gfx2d_window_frame(const char *title, int mx, int my,
                       int clicked, int mouse_down);
int gfx2d_window_x(void);
int gfx2d_window_y(void);
int gfx2d_window_w(void);
int gfx2d_window_h(void);
int gfx2d_window_content_x(void);
int gfx2d_window_content_y(void);
int gfx2d_window_content_w(void);
int gfx2d_window_content_h(void);

/**
 * Draws a Win95-style title bar at the top of the screen (y=0).
 * Contains the app title, a close [X] button, and a minimize [_] button.
 *
 * @param title   Application title to display
 * @param mx      Current mouse X position
 * @param my      Current mouse Y position
 * @param clicked 1 if left mouse button was just pressed, 0 otherwise
 * @return GFX2D_TOOLBAR_NONE, GFX2D_TOOLBAR_CLOSE, or GFX2D_TOOLBAR_MINIMIZE
 */
int gfx2d_app_toolbar(const char *title, int mx, int my, int clicked);

/**
 * Minimizes the current fullscreen app.
 * Exits fullscreen, shows the desktop with a taskbar button for the app,
 * and blocks until the user clicks the taskbar button to restore.
 * Then re-enters fullscreen and returns.
 *
 * @param app_name  Name shown on the taskbar button
 */
void gfx2d_minimize(const char *app_name);

void gfx2d_draw_cursor(void); /* draw cursor at current mouse position */
void gfx2d_cursor_hide(
    void); /* restore pixels under cursor (call before canvas ops) */

/**
 * Shows a modal file open dialog with directory navigation.
 *
 * @param start_path   Initial directory (e.g., "/", "/home")
 * @param result_path  Output buffer (must be VFS_MAX_PATH bytes)
 * @param filter_ext   Optional extension filter (e.g., ".txt") or NULL
 * @return 1 if file selected, 0 if cancelled, negative on error
 */
int gfx2d_file_dialog_open(const char *start_path, char *result_path,
                            const char *filter_ext);

/**
 * Shows a modal file save dialog with directory navigation.
 *
 * @param start_path    Initial directory
 * @param default_name  Pre-filled filename (can be NULL or "")
 * @param result_path   Output buffer (must be VFS_MAX_PATH bytes)
 * @param filter_ext    Optional extension filter or NULL
 * @return 1 if path entered, 0 if cancelled, negative on error
 */
int gfx2d_file_dialog_save(const char *start_path, const char *default_name,
                            char *result_path, const char *filter_ext);

/**
 * Shows a modal Yes/No confirmation dialog.
 *
 * @param message  The message to display
 * @return 1 if Yes, 0 if No
 */
int gfx2d_confirm_dialog(const char *message);

/**
 * Shows a modal text input dialog.
 *
 * @param prompt   Prompt text displayed above the input field
 * @param result   Output buffer for the entered text
 * @param maxlen   Maximum length of result buffer
 * @return 1 if text entered, 0 if cancelled
 */
int gfx2d_input_dialog(const char *prompt, char *result, int maxlen);

/**
 * Shows a modal message dialog with OK button.
 *
 * @param message  The message to display
 */
void gfx2d_message_dialog(const char *message);

/**
 * Shows a modal popup/context menu.
 *
 * @param x      X position of menu
 * @param y      Y position of menu
 * @param items  Array of string pointers (menu item labels)
 * @param count  Number of menu items (max 16)
 * @return selected index (0-based) or -1 if cancelled
 */
int gfx2d_popup_menu(int x, int y, const char **items, int count);

/**
 * Draws a non-modal tooltip near (x,y), clamped to screen bounds.
 * Text is rendered in normal font with a bordered translucent box.
 */
void gfx2d_tooltip(int x, int y, const char *text);

#endif /* GFX2D_H */
