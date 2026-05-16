/**
 * gfx2d_assets.c - Image & Font Loading System for cupid-os
 *
 * Extended BMP image loading with handle pool, custom bitmap font
 * loading (.fnt format), and text rendering with effects.
 */

#include "gfx2d_assets.h"
#include "gfx2d.h"
#include "bmp.h"
#include "png.h"
#include "jpeg.h"
#include "font_8x8.h"
#include "memory.h"
#include "string.h"
#include "vfs.h"
#include "vfs_helpers.h"
#include "serial.h"

/* Image pool */

static uint32_t *g2d_img_data[GFX2D_MAX_IMAGES];
static int       g2d_img_w[GFX2D_MAX_IMAGES];
static int       g2d_img_h[GFX2D_MAX_IMAGES];
static int       g2d_img_used[GFX2D_MAX_IMAGES];

/* Font pool */

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

/* Init */

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

/* Image loading: dispatches by file/buffer signature */

static uint16_t g2d_read_le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t g2d_read_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int g2d_alloc_slot(void) {
    int i;
    for (i = 0; i < GFX2D_MAX_IMAGES; i++) {
        if (!g2d_img_used[i]) return i;
    }
    return -1;
}

static int g2d_load_bmp_path(int slot, const char *path) {
    bmp_info_t info;
    int rc = bmp_get_info(path, &info);
    if (rc != BMP_OK) {
        serial_printf("[gfx2d_assets] bmp info fail %s (%d)\n", path, rc);
        return -1;
    }
    if (info.width == 0 || info.height == 0 ||
        info.width > 8192 || info.height > 8192) {
        serial_printf("[gfx2d_assets] bad bmp dims %ux%u\n",
                      info.width, info.height);
        return -1;
    }
    uint32_t *data = (uint32_t *)kmalloc(info.data_size);
    if (!data) return -1;
    rc = bmp_decode(path, data, info.data_size);
    if (rc != BMP_OK) { kfree(data); return -1; }
    uint32_t count = info.width * info.height;
    for (uint32_t i = 0; i < count; i++)
        data[i] |= 0xFF000000u;
    g2d_img_data[slot] = data;
    g2d_img_w[slot]    = (int)info.width;
    g2d_img_h[slot]    = (int)info.height;
    g2d_img_used[slot] = 1;
    return slot;
}

static int g2d_load_bmp_mem(int slot, const uint8_t *buf, uint32_t len) {
    if (!buf || len < BMP_HEADER_SIZE)
        return -1;
    if (g2d_read_le16(buf) != BMP_SIGNATURE)
        return -1;

    uint32_t data_offset = g2d_read_le32(buf + 10);
    uint32_t dib_size = g2d_read_le32(buf + 14);
    int32_t w = (int32_t)g2d_read_le32(buf + 18);
    int32_t h = (int32_t)g2d_read_le32(buf + 22);
    uint16_t planes = g2d_read_le16(buf + 26);
    uint16_t bpp = g2d_read_le16(buf + 28);
    uint32_t compression = g2d_read_le32(buf + 30);
    if (dib_size < BMP_DIB_HDR_SIZE || planes != 1u ||
        bpp != 24u || compression != 0u || w <= 0 || h == 0)
        return -1;

    uint32_t width = (uint32_t)w;
    uint32_t height = (h > 0) ? (uint32_t)h : (uint32_t)(-h);
    int top_down = h < 0;
    if (width == 0u || height == 0u ||
        width > BMP_MAX_DIM || height > BMP_MAX_DIM)
        return -1;

    uint32_t row_bytes = (width * 3u + 3u) & ~3u;
    uint64_t pixel_end = (uint64_t)data_offset +
                         (uint64_t)row_bytes * (uint64_t)height;
    if (data_offset >= len || pixel_end > (uint64_t)len)
        return -1;

    uint32_t *data = (uint32_t *)kmalloc(width * height * 4u);
    if (!data)
        return -1;

    for (uint32_t file_y = 0; file_y < height; file_y++) {
        uint32_t out_y = top_down ? file_y : (height - 1u - file_y);
        const uint8_t *row = buf + data_offset + file_y * row_bytes;
        for (uint32_t x = 0; x < width; x++) {
            uint8_t b = row[x * 3u + 0u];
            uint8_t g = row[x * 3u + 1u];
            uint8_t r = row[x * 3u + 2u];
            data[out_y * width + x] =
                0xFF000000u | ((uint32_t)r << 16) |
                ((uint32_t)g << 8) | (uint32_t)b;
        }
    }

    g2d_img_data[slot] = data;
    g2d_img_w[slot] = (int)width;
    g2d_img_h[slot] = (int)height;
    g2d_img_used[slot] = 1;
    return slot;
}

static int g2d_load_png_mem(int slot, const uint8_t *buf, uint32_t len) {
    uint32_t *pix = NULL;
    int w = 0, h = 0;
    int rc = png_decode_mem(buf, len, &pix, &w, &h);
    if (rc != PNG_OK || !pix) {
        serial_printf("[gfx2d_assets] png decode fail (%d)\n", rc);
        return -1;
    }
    g2d_img_data[slot] = pix;
    g2d_img_w[slot]    = w;
    g2d_img_h[slot]    = h;
    g2d_img_used[slot] = 1;
    return slot;
}

static int g2d_load_jpeg_mem(int slot, const uint8_t *buf, uint32_t len) {
    uint32_t *pix = NULL;
    int w = 0, h = 0;
    int rc = jpeg_decode_mem(buf, len, &pix, &w, &h);
    if (rc != JPEG_OK || !pix) {
        serial_printf("[gfx2d_assets] jpeg decode fail (%d)\n", rc);
        return -1;
    }
    g2d_img_data[slot] = pix;
    g2d_img_w[slot]    = w;
    g2d_img_h[slot]    = h;
    g2d_img_used[slot] = 1;
    return slot;
}

int gfx2d_image_load(const char *path) {
    int slot = g2d_alloc_slot();
    if (slot < 0) {
        serial_printf("[gfx2d_assets] image pool full\n");
        return -1;
    }

    /* Peek 8 bytes to identify format */
    uint8_t sig[8];
    int fd = vfs_open(path, 0 /* O_RDONLY */);
    if (fd < 0) {
        serial_printf("[gfx2d_assets] cannot open %s\n", path);
        return -1;
    }
    int sn = vfs_read(fd, sig, 8);
    vfs_close(fd);
    if (sn < 2) return -1;

    if (sig[0] == 0x42 && sig[1] == 0x4D) {
        /* "BM" - BMP, use existing path-based decoder */
        return g2d_load_bmp_path(slot, path);
    }

    /* For PNG/JPEG load whole file into memory then dispatch */
    vfs_stat_t st;
    if (vfs_stat(path, &st) < 0) return -1;
    if (st.size == 0 || st.size > 16u * 1024u * 1024u) {
        serial_printf("[gfx2d_assets] bad image size %u\n", st.size);
        return -1;
    }
    uint8_t *buf = (uint8_t *)kmalloc(st.size);
    if (!buf) return -1;
    if ((uint32_t)vfs_read_all(path, buf, st.size) != st.size) {
        kfree(buf);
        return -1;
    }

    int rc;
    if (sig[0] == 0x89 && sig[1] == 0x50 && sig[2] == 0x4E && sig[3] == 0x47) {
        rc = g2d_load_png_mem(slot, buf, st.size);
    } else if (sig[0] == 0xFF && sig[1] == 0xD8) {
        rc = g2d_load_jpeg_mem(slot, buf, st.size);
    } else {
        serial_printf("[gfx2d_assets] unknown image format %s\n", path);
        rc = -1;
    }
    kfree(buf);
    return rc;
}

int gfx2d_image_load_mem(const uint8_t *buf, uint32_t len) {
    if (!buf || len < 4) return -1;
    int slot = g2d_alloc_slot();
    if (slot < 0) return -1;
    if (buf[0] == 0x42 && buf[1] == 0x4D) {
        return g2d_load_bmp_mem(slot, buf, len);
    }
    if (buf[0] == 0x89 && buf[1] == 0x50 && buf[2] == 0x4E && buf[3] == 0x47) {
        return g2d_load_png_mem(slot, buf, len);
    }
    if (buf[0] == 0xFF && buf[1] == 0xD8) {
        return g2d_load_jpeg_mem(slot, buf, len);
    }
    return -1;
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
    /* Use the alpha-aware blit so transparent PNG regions composite
     * over the destination instead of writing raw RGB=0,A=0 (which
     * would show up as solid black where the source was transparent -
     * the bottom of the Wikipedia logo, for example). BMP/JPEG decode
     * emits A=255 so the same path stays a fast opaque write. */
    for (row = 0; row < dh; row++) {
        int sy = (row * sh) / dh;
        for (col = 0; col < dw; col++) {
            int sx = (col * sw) / dw;
            gfx2d_pixel_alpha(x + col, y + row,
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

const uint32_t *gfx2d_image_data(int handle, int *w, int *h) {
    if (handle < 0 || handle >= GFX2D_MAX_IMAGES)
        return NULL;
    if (!g2d_img_used[handle])
        return NULL;
    if (w)
        *w = g2d_img_w[handle];
    if (h)
        *h = g2d_img_h[handle];
    return g2d_img_data[handle];
}

/* Font loading (.fnt format)
 * .fnt file layout:
 * - gfx2d_fnt_header_t (28 bytes)
 * - For each char (first_char..last_char):
 * char_height bytes of row data (1 byte per row, MSB = left pixel)
 * (same format as font_8x8, but variable size)
 */

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
