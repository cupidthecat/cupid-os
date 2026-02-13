/**
 * bmp.c - BMP image encoding/decoding for cupid-os
 *
 * Supports 24-bit uncompressed BMP (BITMAPINFOHEADER) files.
 * Uses VFS for file I/O and outputs 32bpp XRGB pixel data.
 */

#include "bmp.h"
#include "memory.h"
#include "string.h"
#include "vfs.h"
#include "../drivers/serial.h"
#include "../drivers/vga.h"

/* ══════════════════════════════════════════════════════════════════════
 *  Little-endian helpers (x86 is LE-native, but be explicit)
 * ══════════════════════════════════════════════════════════════════════ */

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0]        | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void write_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static int bmp_read_exact(int fd, void *buffer, uint32_t count)
{
    uint8_t *dst = (uint8_t *)buffer;
    uint32_t total = 0;

    while (total < count) {
        int rc = vfs_read(fd, dst + total, count - total);
        if (rc <= 0)
            return BMP_EIO;
        total += (uint32_t)rc;
    }

    return BMP_OK;
}

static int bmp_write_exact(int fd, const void *buffer, uint32_t count)
{
    const uint8_t *src = (const uint8_t *)buffer;
    uint32_t total = 0;

    while (total < count) {
        int rc = vfs_write(fd, src + total, count - total);
        if (rc <= 0)
            return BMP_EIO;
        total += (uint32_t)rc;
    }

    return BMP_OK;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Internal: parse and validate BMP headers from a file descriptor
 * ══════════════════════════════════════════════════════════════════════ */

/**
 * Read and validate BMP file header + DIB header.
 * On success, fills *info and leaves fd positioned at pixel data.
 * Returns BMP_OK or negative error.
 */
static int bmp_read_headers(int fd, bmp_info_t *info, uint32_t *data_offset,
                            int *top_down)
{
    uint8_t hdr[BMP_HEADER_SIZE];
    int rc;

    /* Read the combined 54-byte header */
    rc = vfs_read(fd, hdr, BMP_HEADER_SIZE);
    if (rc < BMP_HEADER_SIZE) {
        KERROR("BMP: could not read header (got %d bytes)\n", rc);
        return BMP_EIO;
    }

    /* --- File header (14 bytes) --- */

    uint16_t sig = read_le16(hdr + 0);
    if (sig != BMP_SIGNATURE) {
        KERROR("BMP: bad signature 0x%x (expected 0x4D42)\n", (unsigned)sig);
        return BMP_EFORMAT;
    }

    *data_offset = read_le32(hdr + 10);

    /* --- DIB header (BITMAPINFOHEADER, 40 bytes at offset 14) --- */

    uint32_t dib_size = read_le32(hdr + 14);
    if (dib_size < BMP_DIB_HDR_SIZE) {
        KERROR("BMP: unsupported DIB header size %u\n", dib_size);
        return BMP_EFORMAT;
    }

    int32_t  w           = (int32_t)read_le32(hdr + 18);
    int32_t  h           = (int32_t)read_le32(hdr + 22);
    uint16_t planes      = read_le16(hdr + 26);
    uint16_t bpp         = read_le16(hdr + 28);
    uint32_t compression = read_le32(hdr + 30);

    /* Validate format constraints */
    if (planes != 1) {
        KERROR("BMP: planes=%u (expected 1)\n", (unsigned)planes);
        return BMP_EFORMAT;
    }
    if (bpp != 24) {
        KERROR("BMP: bpp=%u (only 24-bit supported)\n", (unsigned)bpp);
        return BMP_EFORMAT;
    }
    if (compression != 0) {
        KERROR("BMP: compression=%u (only uncompressed supported)\n",
               compression);
        return BMP_EFORMAT;
    }
    if (w <= 0 || h == 0) {
        KERROR("BMP: invalid dimensions %dx%d\n", w, h);
        return BMP_EINVAL;
    }
    /* Treat negative height as top-down; take absolute value */
    uint32_t width  = (uint32_t)w;
    uint32_t height = (h > 0) ? (uint32_t)h : (uint32_t)(-h);
    if (top_down) {
        *top_down = (h < 0) ? 1 : 0;
    }

    if (width > BMP_MAX_DIM || height > BMP_MAX_DIM) {
        KERROR("BMP: dimensions %ux%u exceed max %u\n",
               width, height, (unsigned)BMP_MAX_DIM);
        return BMP_EINVAL;
    }

    info->width     = width;
    info->height    = height;
    info->bpp       = bpp;
    info->data_size = width * height * 4;

    return BMP_OK;
}

/* ══════════════════════════════════════════════════════════════════════
 *  bmp_get_info — read BMP dimensions without loading pixel data
 * ══════════════════════════════════════════════════════════════════════ */

int bmp_get_info(const char *path, bmp_info_t *info)
{
    if (!path || !info)
        return BMP_EINVAL;

    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) {
        KERROR("BMP: cannot open '%s' (err %d)\n", path, fd);
        return BMP_EIO;
    }

    uint32_t data_offset = 0;
    int rc = bmp_read_headers(fd, info, &data_offset, 0);
    vfs_close(fd);
    return rc;
}

/* ══════════════════════════════════════════════════════════════════════
 *  bmp_decode — decode BMP to 32bpp XRGB buffer
 * ══════════════════════════════════════════════════════════════════════ */

int bmp_decode(const char *path, uint32_t *buffer, uint32_t buffer_size)
{
    if (!path || !buffer)
        return BMP_EINVAL;

    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) {
        KERROR("BMP: cannot open '%s' (err %d)\n", path, fd);
        return BMP_EIO;
    }

    bmp_info_t info;
    uint32_t   data_offset = 0;
    int top_down = 0;
    int rc = bmp_read_headers(fd, &info, &data_offset, &top_down);
    if (rc != BMP_OK) {
        vfs_close(fd);
        return rc;
    }

    uint32_t width  = info.width;
    uint32_t height = info.height;

    /* Validate caller's buffer is large enough */
    uint32_t needed = width * height * 4;
    if (buffer_size < needed) {
        KERROR("BMP: buffer too small (%u < %u)\n", buffer_size, needed);
        vfs_close(fd);
        return BMP_ENOMEM;
    }

    /* Seek to pixel data */
    vfs_seek(fd, (int32_t)data_offset, SEEK_SET);

    /* Row size in bytes, padded to 4-byte boundary */
    uint32_t row_bytes = (width * 3 + 3) & ~3u;

    /* Allocate temp buffer for one scanline */
    uint8_t *row_buf = (uint8_t *)kmalloc(row_bytes);
    if (!row_buf) {
        KERROR("BMP: cannot allocate row buffer (%u bytes)\n", row_bytes);
        vfs_close(fd);
        return BMP_ENOMEM;
    }

    /*
     * BMP stores scanlines bottom-to-top by default.
     * We decode so that buffer[0] = top-left pixel.
     */
    uint32_t y;
    for (y = 0; y < height; y++) {
        /* BMP row index: bottom-up → read row for (height - 1 - y) */
        uint32_t dest_row = y;  /* top-down in output */

        rc = bmp_read_exact(fd, row_buf, row_bytes);
        if (rc != BMP_OK) {
            KERROR("BMP: read error at row %u\n", y);
            kfree(row_buf);
            vfs_close(fd);
            return BMP_EIO;
        }

        /* Convert BGR triplets → XRGB uint32_t */
        uint32_t out_row = top_down ? dest_row : (height - 1 - dest_row);
        uint32_t *dest = buffer + out_row * width;
        uint32_t x;
        for (x = 0; x < width; x++) {
            uint8_t b = row_buf[x * 3 + 0];
            uint8_t g = row_buf[x * 3 + 1];
            uint8_t r = row_buf[x * 3 + 2];
            dest[x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }

    kfree(row_buf);
    vfs_close(fd);

    KDEBUG("BMP: decoded '%s' (%ux%u)\n", path, width, height);
    return BMP_OK;
}

/* ══════════════════════════════════════════════════════════════════════
 *  bmp_encode — encode 32bpp XRGB buffer as 24-bit BMP
 * ══════════════════════════════════════════════════════════════════════ */

int bmp_encode(const char *path, const uint32_t *buffer,
               uint32_t width, uint32_t height)
{
    if (!path || !buffer)
        return BMP_EINVAL;
    if (width == 0 || height == 0 || width > BMP_MAX_DIM ||
        height > BMP_MAX_DIM)
        return BMP_EINVAL;

    /* Row size in bytes, padded to 4-byte boundary */
    uint32_t row_bytes = (width * 3 + 3) & ~3u;
    uint32_t pixel_data_size = row_bytes * height;
    uint32_t file_size = BMP_HEADER_SIZE + pixel_data_size;

    int fd = vfs_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        KERROR("BMP: cannot create '%s' (err %d)\n", path, fd);
        return BMP_EIO;
    }

    /* ── Write 14-byte file header ─────────────────────────────── */
    uint8_t hdr[BMP_HEADER_SIZE];
    memset(hdr, 0, BMP_HEADER_SIZE);

    /* Signature "BM" */
    hdr[0] = 0x42;
    hdr[1] = 0x4D;
    write_le32(hdr + 2, file_size);
    /* reserved fields at offset 6..9 stay 0 */
    write_le32(hdr + 10, BMP_HEADER_SIZE); /* data offset = 54 */

    int rc = bmp_write_exact(fd, hdr, BMP_FILE_HDR_SIZE);
    if (rc != BMP_OK) {
        vfs_close(fd);
        return BMP_EIO;
    }

    /* ── Write 40-byte DIB header (BITMAPINFOHEADER) ───────────── */
    uint8_t dib[BMP_DIB_HDR_SIZE];
    memset(dib, 0, BMP_DIB_HDR_SIZE);

    write_le32(dib + 0,  BMP_DIB_HDR_SIZE);   /* header size: 40 */
    write_le32(dib + 4,  width);               /* width           */
    write_le32(dib + 8,  height);              /* height (positive = bottom-up) */
    write_le16(dib + 12, 1);                   /* planes          */
    write_le16(dib + 14, 24);                  /* bpp             */
    write_le32(dib + 16, 0);                   /* compression: BI_RGB */
    write_le32(dib + 20, pixel_data_size);     /* image data size */
    /* x/y pixels per meter, colors used/important: all 0 */

    rc = bmp_write_exact(fd, dib, BMP_DIB_HDR_SIZE);
    if (rc != BMP_OK) {
        vfs_close(fd);
        return BMP_EIO;
    }

    /* ── Write pixel data (bottom-to-top scanlines) ────────────── */

    /* Allocate temp buffer for one row */
    uint8_t *row_buf = (uint8_t *)kmalloc(row_bytes);
    if (!row_buf) {
        KERROR("BMP: cannot allocate row buffer (%u bytes)\n", row_bytes);
        vfs_close(fd);
        return BMP_ENOMEM;
    }

    uint32_t y;
    for (y = 0; y < height; y++) {
        /* BMP is bottom-up: first written row = bottom of image */
        const uint32_t *src = buffer + (height - 1 - y) * width;

        /* Convert XRGB → BGR triplets */
        uint32_t x;
        for (x = 0; x < width; x++) {
            uint32_t px = src[x];
            row_buf[x * 3 + 0] = (uint8_t)(px & 0xFF);          /* B */
            row_buf[x * 3 + 1] = (uint8_t)((px >> 8) & 0xFF);   /* G */
            row_buf[x * 3 + 2] = (uint8_t)((px >> 16) & 0xFF);  /* R */
        }

        /* Zero-pad remainder of row */
        uint32_t pad_start = width * 3;
        while (pad_start < row_bytes) {
            row_buf[pad_start++] = 0;
        }

        rc = bmp_write_exact(fd, row_buf, row_bytes);
        if (rc != BMP_OK) {
            KERROR("BMP: write error at row %u\n", y);
            kfree(row_buf);
            vfs_close(fd);
            return BMP_EIO;
        }
    }

    kfree(row_buf);
    vfs_close(fd);

    KDEBUG("BMP: encoded '%s' (%ux%u, %u bytes)\n",
           path, width, height, file_size);
    return BMP_OK;
}

/* ══════════════════════════════════════════════════════════════════════
 *  bmp_decode_to_fb — decode BMP directly to VGA framebuffer
 * ══════════════════════════════════════════════════════════════════════ */

int bmp_decode_to_fb(const char *path, int dest_x, int dest_y)
{
    if (!path)
        return BMP_EINVAL;

    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) {
        KERROR("BMP: cannot open '%s' (err %d)\n", path, fd);
        return BMP_EIO;
    }

    bmp_info_t info;
    uint32_t   data_offset = 0;
    int top_down = 0;
    int rc = bmp_read_headers(fd, &info, &data_offset, &top_down);
    if (rc != BMP_OK) {
        vfs_close(fd);
        return rc;
    }

    uint32_t width  = info.width;
    uint32_t height = info.height;

    /* Seek to pixel data */
    vfs_seek(fd, (int32_t)data_offset, SEEK_SET);

    uint32_t row_bytes = (width * 3 + 3) & ~3u;

    uint8_t *row_buf = (uint8_t *)kmalloc(row_bytes);
    if (!row_buf) {
        vfs_close(fd);
        return BMP_ENOMEM;
    }

    uint32_t *fb = vga_get_framebuffer();

    uint32_t y;
    for (y = 0; y < height; y++) {
        rc = bmp_read_exact(fd, row_buf, row_bytes);
        if (rc != BMP_OK) {
            kfree(row_buf);
            vfs_close(fd);
            return BMP_EIO;
        }

        /* BMP bottom-up → framebuffer row */
        int fb_y = top_down ? (dest_y + (int)y)
                            : (dest_y + (int)(height - 1 - y));
        if (fb_y < 0 || fb_y >= VGA_GFX_HEIGHT)
            continue;

        uint32_t x;
        for (x = 0; x < width; x++) {
            int fb_x = dest_x + (int)x;
            if (fb_x < 0 || fb_x >= VGA_GFX_WIDTH)
                continue;

            uint8_t b = row_buf[x * 3 + 0];
            uint8_t g = row_buf[x * 3 + 1];
            uint8_t r = row_buf[x * 3 + 2];
            uint32_t color = ((uint32_t)r << 16) |
                             ((uint32_t)g << 8)  |
                             (uint32_t)b;

            fb[fb_y * VGA_GFX_WIDTH + fb_x] = color;
        }
    }

    kfree(row_buf);
    vfs_close(fd);

    KDEBUG("BMP: decoded '%s' to fb at (%d,%d)\n", path, dest_x, dest_y);
    return BMP_OK;
}
