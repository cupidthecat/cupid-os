/**
 * gfx2d_assets.h - Image & Font Loading System for cupid-os
 *
 * Extended image loading (BMP), custom bitmap font support,
 * and text measurement for layout.
 */

#ifndef GFX2D_ASSETS_H
#define GFX2D_ASSETS_H

#include "types.h"

#define GFX2D_MAX_IMAGES 16
#define GFX2D_MAX_FONTS   8

#define GFX2D_TEXT_SHADOW       0x01
#define GFX2D_TEXT_OUTLINE      0x02
#define GFX2D_TEXT_UNDERLINE    0x04
#define GFX2D_TEXT_STRIKETHROUGH 0x08

#define GFX2D_FNT_MAGIC  0x00544E46   /* "FNT\0" in little-endian */

typedef struct {
    uint32_t magic;        /* Must be GFX2D_FNT_MAGIC */
    uint32_t version;      /* Format version (1)      */
    uint32_t char_width;   /* Glyph width in pixels   */
    uint32_t char_height;  /* Glyph height in pixels  */
    uint32_t first_char;   /* First ASCII code        */
    uint32_t last_char;    /* Last ASCII code         */
    uint32_t flags;        /* Reserved flags          */
} gfx2d_fnt_header_t;

/** Load a BMP image from VFS path. Returns handle (>= 0) or -1. */
int  gfx2d_image_load(const char *path);

/** Free a loaded image. */
void gfx2d_image_free(int handle);

/** Draw image at (x, y), unscaled. */
void gfx2d_image_draw(int handle, int x, int y);

/** Draw image scaled to (w x h) at (x, y). */
void gfx2d_image_draw_scaled(int handle, int x, int y, int w, int h);

/** Draw a region of the image.
 *  Source rect (sx,sy,sw,sh) drawn at dest (dx,dy). */
void gfx2d_image_draw_region(int handle, int sx, int sy, int sw, int sh,
                              int dx, int dy);

/** Get image width. */
int  gfx2d_image_width(int handle);

/** Get image height. */
int  gfx2d_image_height(int handle);

/** Get pixel color at (x,y) within the image. */
uint32_t gfx2d_image_get_pixel(int handle, int x, int y);

/** Get direct image pixel buffer and dimensions (read-only).
 *  Returns NULL on invalid handle. */
const uint32_t *gfx2d_image_data(int handle, int *w, int *h);

/** Load a .fnt bitmap font from VFS path. Returns handle (>= 0) or -1. */
int  gfx2d_font_load(const char *path);

/** Free a loaded font. */
void gfx2d_font_free(int handle);

/** Set font as the default for gfx2d_text_ex when handle == -1. */
void gfx2d_font_set_default(int handle);

/** Get width of text string rendered with given font. */
int  gfx2d_font_text_width(int handle, const char *text);

/** Get height of the font glyphs. */
int  gfx2d_font_text_height(int handle);

/** Draw text with a loaded font and text effects.
 *  font_handle of -1 uses current default font (or built-in 8x8). */
void gfx2d_text_ex(int x, int y, const char *text, uint32_t color,
                   int font_handle, int effects);

void gfx2d_assets_init(void);

#endif /* GFX2D_ASSETS_H */
