/**
 * gfx2d_assets.c - Image & Font Loading System for cupid-os
 *
 * Extended BMP image loading with handle pool, custom bitmap font
 * loading (.fnt format), and text rendering with effects.
 */

#include "gfx2d_assets.h"
#include "gfx2d.h"
#include "bmp.h"
#include "font_8x8.h"
#include "memory.h"
#include "string.h"
#include "vfs.h"
#include "../drivers/serial.h"

/* ══════════════════════════════════════════════════════════════════════
 *  Image pool
 * ══════════════════════════════════════════════════════════════════════ */

static uint32_t *g2d_img_data[GFX2D_MAX_IMAGES];
static int       g2d_img_w[GFX2D_MAX_IMAGES];
static int       g2d_img_h[GFX2D_MAX_IMAGES];
static int       g2d_img_used[GFX2D_MAX_IMAGES];

/* ══════════════════════════════════════════════════════════════════════
 *  Font pool
 * ══════════════════════════════════════════════════════════════════════ */

typedef struct {
    uint8_t *glyph_data;      /* Raw 1-bpp glyph bitmaps (row-major, MSB left) */
    int      char_width;
    int      char_height;
    int      first_char;
    int      last_char;
    int      used;
} g2d_font_t;

static g2d_font_t g2d_fonts[GFX2D_MAX_FONTS];
static int g2d_default_font = -1;  /* -1 = use built-in 8x8 */

/* ══════════════════════════════════════════════════════════════════════
 *  Init
 * ══════════════════════════════════════════════════════════════════════ */

void gfx2d_assets_init(void) {
    int i;
    for (i = 0; i < GFX2D_MAX_IMAGES; i++) {
        g2d_img_data[i] = NULL;
        g2d_img_used[i] = 0;
    }
    for (i = 0; i < GFX2D_MAX_FONTS; i++) {
        g2d_fonts[i].used = 0;
        g2d_fonts[i].glyph_data = NULL;
    }
    g2d_default_font = -1;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Image loading (BMP)
 * ══════════════════════════════════════════════════════════════════════ */

int gfx2d_image_load(const char *path) {
    int i;
    bmp_info_t info;
    uint32_t *data;
    int rc;

    /* Find free slot */
    for (i = 0; i < GFX2D_MAX_IMAGES; i++) {
        if (!g2d_img_used[i])
            break;
    }
    if (i >= GFX2D_MAX_IMAGES) {
        serial_printf("[gfx2d_assets] image pool full\n");
        return -1;
    }

    /* Get image dimensions */
    rc = bmp_get_info(path, &info);
    if (rc != BMP_OK) {
        serial_printf("[gfx2d_assets] image_load: cannot read info %s (%d)\n",
                      path, rc);
        return -1;
    }

    if (info.width == 0 || info.height == 0 ||
        info.width > 8192 || info.height > 8192) {
        serial_printf("[gfx2d_assets] image_load: bad dimensions %ux%u\n",
                      info.width, info.height);
        return -1;
    }

    /* Allocate pixel buffer */
    data = (uint32_t *)kmalloc(info.data_size);
    if (!data) {
        serial_printf("[gfx2d_assets] image_load: out of memory (%u bytes)\n",
                      info.data_size);
        return -1;
    }

    /* Decode BMP */
    rc = bmp_decode(path, data, info.data_size);
    if (rc != BMP_OK) {
        kfree(data);
        serial_printf("[gfx2d_assets] image_load: decode failed %s (%d)\n",
                      path, rc);
        return -1;
    }

    g2d_img_data[i] = data;
    g2d_img_w[i] = (int)info.width;
    g2d_img_h[i] = (int)info.height;
    g2d_img_used[i] = 1;

    serial_printf("[gfx2d_assets] image %d loaded: %dx%d from %s\n",
                  i, (int)info.width, (int)info.height, path);
    return i;
}

void gfx2d_image_free(int handle) {
    if (handle < 0 || handle >= GFX2D_MAX_IMAGES)
        return;
    if (!g2d_img_used[handle])
        return;
    kfree(g2d_img_data[handle]);
    g2d_img_data[handle] = NULL;
    g2d_img_used[handle] = 0;
}

void gfx2d_image_draw(int handle, int x, int y) {
    int row, col;
    if (handle < 0 || handle >= GFX2D_MAX_IMAGES)
        return;
    if (!g2d_img_used[handle])
        return;
    int w = g2d_img_w[handle], h = g2d_img_h[handle];
    uint32_t *data = g2d_img_data[handle];
    for (row = 0; row < h; row++)
        for (col = 0; col < w; col++)
            gfx2d_pixel(x + col, y + row,
                        data[(uint32_t)row * (uint32_t)w + (uint32_t)col]);
}

void gfx2d_image_draw_scaled(int handle, int x, int y, int dw, int dh) {
    int row, col;
    if (handle < 0 || handle >= GFX2D_MAX_IMAGES)
        return;
    if (!g2d_img_used[handle])
        return;
    if (dw <= 0 || dh <= 0)
        return;
    int sw = g2d_img_w[handle], sh = g2d_img_h[handle];
    uint32_t *data = g2d_img_data[handle];
    for (row = 0; row < dh; row++) {
        int sy = (row * sh) / dh;
        for (col = 0; col < dw; col++) {
            int sx = (col * sw) / dw;
            gfx2d_pixel(x + col, y + row,
                        data[(uint32_t)sy * (uint32_t)sw + (uint32_t)sx]);
        }
    }
}

void gfx2d_image_draw_region(int handle, int sx, int sy, int sw, int sh,
                              int dx, int dy) {
    int row, col;
    if (handle < 0 || handle >= GFX2D_MAX_IMAGES)
        return;
    if (!g2d_img_used[handle])
        return;
    int iw = g2d_img_w[handle], ih = g2d_img_h[handle];
    uint32_t *data = g2d_img_data[handle];

    /* Clamp source region to image bounds */
    if (sx < 0) { dx -= sx; sw += sx; sx = 0; }
    if (sy < 0) { dy -= sy; sh += sy; sy = 0; }
    if (sx + sw > iw) sw = iw - sx;
    if (sy + sh > ih) sh = ih - sy;
    if (sw <= 0 || sh <= 0) return;

    for (row = 0; row < sh; row++)
        for (col = 0; col < sw; col++)
            gfx2d_pixel(dx + col, dy + row,
                        data[(uint32_t)(sy + row) * (uint32_t)iw +
                             (uint32_t)(sx + col)]);
}

int gfx2d_image_width(int handle) {
    if (handle < 0 || handle >= GFX2D_MAX_IMAGES)
        return 0;
    if (!g2d_img_used[handle])
        return 0;
    return g2d_img_w[handle];
}

int gfx2d_image_height(int handle) {
    if (handle < 0 || handle >= GFX2D_MAX_IMAGES)
        return 0;
    if (!g2d_img_used[handle])
        return 0;
    return g2d_img_h[handle];
}

uint32_t gfx2d_image_get_pixel(int handle, int x, int y) {
    if (handle < 0 || handle >= GFX2D_MAX_IMAGES)
        return 0;
    if (!g2d_img_used[handle])
        return 0;
    int w = g2d_img_w[handle], h = g2d_img_h[handle];
    if (x < 0 || x >= w || y < 0 || y >= h)
        return 0;
    return g2d_img_data[handle][(uint32_t)y * (uint32_t)w + (uint32_t)x];
}

/* ══════════════════════════════════════════════════════════════════════
 *  Font loading (.fnt format)
 *
 *  .fnt file layout:
 *    - gfx2d_fnt_header_t (28 bytes)
 *    - For each char (first_char..last_char):
 *        char_height bytes of row data (1 byte per row, MSB = left pixel)
 *        (same format as font_8x8, but variable size)
 * ══════════════════════════════════════════════════════════════════════ */

int gfx2d_font_load(const char *path) {
    int i, fd;
    gfx2d_fnt_header_t hdr;
    int char_count;
    int bytes_per_char;
    uint32_t total_bytes;
    uint8_t *glyph_buf;

    /* Find free font slot */
    for (i = 0; i < GFX2D_MAX_FONTS; i++) {
        if (!g2d_fonts[i].used)
            break;
    }
    if (i >= GFX2D_MAX_FONTS) {
        serial_printf("[gfx2d_assets] font pool full\n");
        return -1;
    }

    fd = vfs_open(path, O_RDONLY);
    if (fd < 0) {
        serial_printf("[gfx2d_assets] font_load: cannot open %s\n", path);
        return -1;
    }

    /* Read header */
    if (vfs_read(fd, &hdr, sizeof(hdr)) != (int)sizeof(hdr)) {
        vfs_close(fd);
        serial_printf("[gfx2d_assets] font_load: bad header\n");
        return -1;
    }

    if (hdr.magic != GFX2D_FNT_MAGIC) {
        vfs_close(fd);
        serial_printf("[gfx2d_assets] font_load: bad magic 0x%x\n", hdr.magic);
        return -1;
    }

    if (hdr.char_width == 0 || hdr.char_width > 32 ||
        hdr.char_height == 0 || hdr.char_height > 32) {
        vfs_close(fd);
        serial_printf("[gfx2d_assets] font_load: bad glyph size %ux%u\n",
                      hdr.char_width, hdr.char_height);
        return -1;
    }

    if (hdr.first_char > hdr.last_char || hdr.last_char > 255) {
        vfs_close(fd);
        return -1;
    }

    char_count = (int)(hdr.last_char - hdr.first_char + 1);
    /* Each row: 1 byte per 8 pixels width, so ceil(char_width / 8) bytes */
    bytes_per_char = (int)(((hdr.char_width + 7) / 8) * hdr.char_height);
    total_bytes = (uint32_t)char_count * (uint32_t)bytes_per_char;

    glyph_buf = (uint8_t *)kmalloc(total_bytes);
    if (!glyph_buf) {
        vfs_close(fd);
        serial_printf("[gfx2d_assets] font_load: out of memory\n");
        return -1;
    }

    if ((uint32_t)vfs_read(fd, glyph_buf, total_bytes) != total_bytes) {
        kfree(glyph_buf);
        vfs_close(fd);
        serial_printf("[gfx2d_assets] font_load: short read\n");
        return -1;
    }

    vfs_close(fd);

    g2d_fonts[i].glyph_data = glyph_buf;
    g2d_fonts[i].char_width = (int)hdr.char_width;
    g2d_fonts[i].char_height = (int)hdr.char_height;
    g2d_fonts[i].first_char = (int)hdr.first_char;
    g2d_fonts[i].last_char = (int)hdr.last_char;
    g2d_fonts[i].used = 1;

    serial_printf("[gfx2d_assets] font %d loaded: %dx%d, chars %d-%d from %s\n",
                  i, (int)hdr.char_width, (int)hdr.char_height,
                  (int)hdr.first_char, (int)hdr.last_char, path);
    return i;
}

void gfx2d_font_free(int handle) {
    if (handle < 0 || handle >= GFX2D_MAX_FONTS)
        return;
    if (!g2d_fonts[handle].used)
        return;
    kfree(g2d_fonts[handle].glyph_data);
    g2d_fonts[handle].glyph_data = NULL;
    g2d_fonts[handle].used = 0;
    if (g2d_default_font == handle)
        g2d_default_font = -1;
}

void gfx2d_font_set_default(int handle) {
    if (handle >= 0 && handle < GFX2D_MAX_FONTS && g2d_fonts[handle].used)
        g2d_default_font = handle;
    else
        g2d_default_font = -1;
}

int gfx2d_font_text_width(int handle, const char *text) {
    int len;
    if (!text) return 0;
    len = (int)strlen(text);
    if (handle >= 0 && handle < GFX2D_MAX_FONTS && g2d_fonts[handle].used)
        return len * g2d_fonts[handle].char_width;
    /* Fall back to built-in 8x8 */
    return len * FONT_W;
}

int gfx2d_font_text_height(int handle) {
    if (handle >= 0 && handle < GFX2D_MAX_FONTS && g2d_fonts[handle].used)
        return g2d_fonts[handle].char_height;
    return FONT_H;
}

/* ── Draw a single custom font glyph ─────────────────────────────── */

static void draw_custom_glyph(const g2d_font_t *fnt, int x, int y,
                              char ch, uint32_t color) {
    int code = (int)(unsigned char)ch;
    int row_bytes, row, col;
    const uint8_t *glyph;

    if (code < fnt->first_char || code > fnt->last_char)
        return;

    row_bytes = (fnt->char_width + 7) / 8;
    glyph = fnt->glyph_data +
            (code - fnt->first_char) * row_bytes * fnt->char_height;

    for (row = 0; row < fnt->char_height; row++) {
        for (col = 0; col < fnt->char_width; col++) {
            int byte_idx = col / 8;
            int bit_idx = 7 - (col % 8);
            if (glyph[row * row_bytes + byte_idx] & (1 << bit_idx))
                gfx2d_pixel(x + col, y + row, color);
        }
    }
}

/* ── Draw text with effects ───────────────────────────────────────── */

void gfx2d_text_ex(int x, int y, const char *text, uint32_t color,
                   int font_handle, int effects) {
    int use_custom = 0;
    const g2d_font_t *fnt = NULL;
    int cw, ch;
    int i, len;
    uint32_t shadow_color;

    if (!text) return;
    len = (int)strlen(text);

    /* Resolve font */
    if (font_handle == -1)
        font_handle = g2d_default_font;

    if (font_handle >= 0 && font_handle < GFX2D_MAX_FONTS &&
        g2d_fonts[font_handle].used) {
        fnt = &g2d_fonts[font_handle];
        cw = fnt->char_width;
        ch = fnt->char_height;
        use_custom = 1;
    } else {
        cw = FONT_W;
        ch = FONT_H;
    }

    /* Shadow: dark copy offset by (1,1) */
    if (effects & GFX2D_TEXT_SHADOW) {
        shadow_color = 0x00404040;
        if (use_custom) {
            for (i = 0; i < len; i++)
                draw_custom_glyph(fnt, x + 1 + i * cw, y + 1,
                                  text[i], shadow_color);
        } else {
            gfx2d_text(x + 1, y + 1, text, shadow_color, GFX2D_FONT_NORMAL);
        }
    }

    /* Outline: draw at 8 surrounding offsets */
    if (effects & GFX2D_TEXT_OUTLINE) {
        uint32_t outline_color = 0x00000000;
        int dx, dy;
        for (dy = -1; dy <= 1; dy++) {
            for (dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) continue;
                if (use_custom) {
                    for (i = 0; i < len; i++)
                        draw_custom_glyph(fnt, x + dx + i * cw, y + dy,
                                          text[i], outline_color);
                } else {
                    gfx2d_text(x + dx, y + dy, text, outline_color,
                               GFX2D_FONT_NORMAL);
                }
            }
        }
    }

    /* Main text */
    if (use_custom) {
        for (i = 0; i < len; i++)
            draw_custom_glyph(fnt, x + i * cw, y, text[i], color);
    } else {
        gfx2d_text(x, y, text, color, GFX2D_FONT_NORMAL);
    }

    /* Underline */
    if (effects & GFX2D_TEXT_UNDERLINE) {
        int uy = y + ch + 1;
        int uw = len * cw;
        gfx2d_hline(x, uy, uw, color);
    }

    /* Strikethrough */
    if (effects & GFX2D_TEXT_STRIKETHROUGH) {
        int sy2 = y + ch / 2;
        int sw2 = len * cw;
        gfx2d_hline(x, sy2, sw2, color);
    }
}
