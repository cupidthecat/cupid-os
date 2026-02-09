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

/* ── Screen ───────────────────────────────────────────────────────── */
void gfx2d_clear(uint32_t color);
void gfx2d_flip(void);
int gfx2d_width(void);
int gfx2d_height(void);

/* ── Pixels & Lines ───────────────────────────────────────────────── */
void gfx2d_pixel(int x, int y, uint32_t color);
uint32_t gfx2d_getpixel(int x, int y);
void gfx2d_line(int x1, int y1, int x2, int y2, uint32_t color);
void gfx2d_hline(int x, int y, int w, uint32_t color);
void gfx2d_vline(int x, int y, int h, uint32_t color);

/* ── Rectangles ───────────────────────────────────────────────────── */
void gfx2d_rect(int x, int y, int w, int h, uint32_t color);
void gfx2d_rect_fill(int x, int y, int w, int h, uint32_t color);
void gfx2d_rect_round(int x, int y, int w, int h, int r, uint32_t color);
void gfx2d_rect_round_fill(int x, int y, int w, int h, int r, uint32_t color);

/* ── Circles & Ellipses ───────────────────────────────────────────── */
void gfx2d_circle(int x, int y, int r, uint32_t color);
void gfx2d_circle_fill(int x, int y, int r, uint32_t color);
void gfx2d_ellipse(int x, int y, int rx, int ry, uint32_t color);
void gfx2d_ellipse_fill(int x, int y, int rx, int ry, uint32_t color);

/* ── Alpha Blending (color format: 0xAARRGGBB) ───────────────────── */
void gfx2d_rect_fill_alpha(int x, int y, int w, int h, uint32_t argb);
void gfx2d_pixel_alpha(int x, int y, uint32_t argb);

/* ── Gradients ────────────────────────────────────────────────────── */
void gfx2d_gradient_h(int x, int y, int w, int h, uint32_t c1, uint32_t c2);
void gfx2d_gradient_v(int x, int y, int w, int h, uint32_t c1, uint32_t c2);

/* ── Drop Shadow ──────────────────────────────────────────────────── */
void gfx2d_shadow(int x, int y, int w, int h, int blur, uint32_t color);

/* ── Dithering ────────────────────────────────────────────────────── */
void gfx2d_dither_rect(int x, int y, int w, int h, uint32_t c1, uint32_t c2,
                       int pattern);

/* ── Scanlines (CRT effect) ───────────────────────────────────────── */
void gfx2d_scanlines(int x, int y, int w, int h, int alpha);

/* ── Clipping ─────────────────────────────────────────────────────── */
void gfx2d_clip_set(int x, int y, int w, int h);
void gfx2d_clip_clear(void);

/* ── Sprites ──────────────────────────────────────────────────────── */
int gfx2d_sprite_load(const char *path);
void gfx2d_sprite_free(int handle);
void gfx2d_sprite_draw(int handle, int x, int y);
void gfx2d_sprite_draw_alpha(int handle, int x, int y);
void gfx2d_sprite_draw_scaled(int handle, int x, int y, int w, int h);
int gfx2d_sprite_width(int handle);
int gfx2d_sprite_height(int handle);

/* ── Text ─────────────────────────────────────────────────────────── */
void gfx2d_text(int x, int y, const char *str, uint32_t color, int font);
void gfx2d_text_shadow(int x, int y, const char *str, uint32_t color,
                       uint32_t shadow_color, int font);
void gfx2d_text_outline(int x, int y, const char *str, uint32_t color,
                        uint32_t outline_color, int font);
int gfx2d_text_width(const char *str, int font);
int gfx2d_text_height(int font);

/* ── Retro Atmosphere Effects ─────────────────────────────────────── */
void gfx2d_vignette(int strength);
void gfx2d_pixelate(int x, int y, int w, int h, int block_size);
void gfx2d_invert(int x, int y, int w, int h);
void gfx2d_tint(int x, int y, int w, int h, uint32_t color, int alpha);

/* ── Win95-Style UI Helpers ───────────────────────────────────────── */
void gfx2d_bevel(int x, int y, int w, int h, int raised);
void gfx2d_panel(int x, int y, int w, int h);
void gfx2d_titlebar(int x, int y, int w, int h, uint32_t c1, uint32_t c2);

/* ── Demo Scene Effects ───────────────────────────────────────────── */
void gfx2d_copper_bars(int y, int count, int spacing, int *colors);
void gfx2d_plasma(int x, int y, int w, int h, int tick);
void gfx2d_checkerboard(int x, int y, int w, int h, int size, uint32_t c1,
                        uint32_t c2);

/* ── Blend Modes ──────────────────────────────────────────────────── */
void gfx2d_blend_mode(int mode);

/* ── Offscreen Surfaces ───────────────────────────────────────────── */
#define GFX2D_MAX_SURFACES 8

int gfx2d_surface_alloc(int w, int h); /* returns handle 0..7, or -1 */
void gfx2d_surface_free(int handle);
void gfx2d_surface_fill(int handle, uint32_t color);
void gfx2d_surface_set_active(int handle); /* redirect drawing here */
void gfx2d_surface_unset_active(void);     /* back to screen */
void gfx2d_surface_blit(int handle, int x, int y);
void gfx2d_surface_blit_alpha(int handle, int x, int y, int alpha);

/* ── Tweening / Easing (integer, t=0..dur maps start..end) ───────── */
int gfx2d_tween_linear(int t, int start, int end, int dur);
int gfx2d_tween_ease_in_out(int t, int start, int end, int dur);
int gfx2d_tween_bounce(int t, int start, int end, int dur);
int gfx2d_tween_elastic(int t, int start, int end, int dur);

/* ── Particle System ──────────────────────────────────────────────── */
#define GFX2D_MAX_PARTICLE_SYSTEMS 4
#define GFX2D_MAX_PARTICLES_PER_SYS 64

int gfx2d_particles_create(void); /* returns handle */
void gfx2d_particles_free(int handle);
void gfx2d_particle_emit(int handle, int x, int y, int vx, int vy,
                         uint32_t color, int life);
void gfx2d_particles_update(int handle, int gravity);
void gfx2d_particles_draw(int handle);
int gfx2d_particles_alive(int handle); /* count of live particles */

/* ── Advanced Drawing Tools ───────────────────────────────────────── */
void gfx2d_bezier(int x0, int y0, int x1, int y1, int x2, int y2,
                  uint32_t color);
void gfx2d_tri_fill(int x0, int y0, int x1, int y1, int x2, int y2,
                    uint32_t color);
void gfx2d_line_aa(int x0, int y0, int x1, int y1, uint32_t color);
void gfx2d_flood_fill(int x, int y, uint32_t color);

/* ── Fullscreen Mode (disables desktop rendering) ───────────────── */
void gfx2d_fullscreen_enter(void);
void gfx2d_fullscreen_exit(void);
int gfx2d_fullscreen_active(void); /* returns 1 if fullscreen mode is active */

/* ── Mouse Cursor (for fullscreen apps) ──────────────────────────── */
void gfx2d_draw_cursor(void); /* draw cursor at current mouse position */
void gfx2d_cursor_hide(
    void); /* restore pixels under cursor (call before canvas ops) */

/* ── File Dialogs (modal, self-contained event loop) ─────────────── */

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

#endif /* GFX2D_H */
