/**
 * bmp.h - BMP image encoding/decoding for cupid-os
 *
 * Supports 24-bit uncompressed BMP (BITMAPINFOHEADER) files.
 * Decodes to 32bpp XRGB buffers, encodes from 32bpp XRGB buffers.
 * Exposed to CupidC programs via kernel symbol bindings.
 */

#ifndef BMP_H
#define BMP_H

#include "types.h"

/* ── BMP error codes ──────────────────────────────────────────────── */

#define BMP_OK       0
#define BMP_EINVAL  -1   /* Invalid file/parameters */
#define BMP_EFORMAT -2   /* Unsupported BMP format  */
#define BMP_EIO     -3   /* File I/O error          */
#define BMP_ENOMEM  -4   /* Buffer too small        */

/* ── BMP image info ───────────────────────────────────────────────── */

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;         /* bits per pixel (24 for supported files) */
    uint32_t data_size;   /* bytes needed for XRGB buffer: w*h*4    */
} bmp_info_t;

/* ── BMP file format constants ────────────────────────────────────── */

#define BMP_SIGNATURE       0x4D42   /* "BM" in little-endian         */
#define BMP_FILE_HDR_SIZE   14       /* BMP file header size          */
#define BMP_DIB_HDR_SIZE    40       /* BITMAPINFOHEADER size         */
#define BMP_HEADER_SIZE     54       /* file header + DIB header      */
#define BMP_MAX_DIM         8192     /* max width or height           */

/* ── Public API ───────────────────────────────────────────────────── */

/**
 * Get BMP dimensions without loading pixel data.
 * Useful for determining buffer allocation size.
 *
 * @param path   VFS path to .bmp file
 * @param info   Output struct with width, height, bpp, data_size
 * @return       BMP_OK on success, negative error code on failure
 */
int bmp_get_info(const char *path, bmp_info_t *info);

/**
 * Decode a 24-bit BMP file into a caller-allocated 32bpp XRGB buffer.
 * Pixel format: 0x00RRGGBB (same as VGA framebuffer).
 * Image is stored top-to-bottom, left-to-right in the output buffer.
 *
 * @param path        VFS path to .bmp file
 * @param buffer      Output pixel buffer (must be >= width*height*4 bytes)
 * @param buffer_size Size of output buffer in bytes
 * @return            BMP_OK on success, negative error code on failure
 */
int bmp_decode(const char *path, uint32_t *buffer, uint32_t buffer_size);

/**
 * Encode a 32bpp XRGB buffer as a 24-bit BMP file.
 * Writes the BMP file to the given VFS path.
 *
 * @param path    VFS path for output .bmp file
 * @param buffer  Input pixel buffer in XRGB format (0x00RRGGBB)
 * @param width   Image width in pixels
 * @param height  Image height in pixels
 * @return        BMP_OK on success, negative error code on failure
 */
int bmp_encode(const char *path, const uint32_t *buffer,
               uint32_t width, uint32_t height);

/**
 * Decode a BMP file directly to the VGA framebuffer at (dest_x, dest_y).
 * Clips to screen bounds (640x480).
 *
 * @param path    VFS path to .bmp file
 * @param dest_x  X position on framebuffer
 * @param dest_y  Y position on framebuffer
 * @return        BMP_OK on success, negative error code on failure
 */
int bmp_decode_to_fb(const char *path, int dest_x, int dest_y);

/**
 * Decode a BMP file into a gfx2d surface, scaling to fit destination size.
 * Uses streaming row reads to avoid allocating width*height*4 buffers.
 *
 * @param path        VFS path to .bmp file
 * @param surface_id  Destination gfx2d surface id
 * @param dest_w      Destination width in pixels (>0)
 * @param dest_h      Destination height in pixels (>0)
 * @return            BMP_OK on success, negative error code on failure
 */
int bmp_decode_to_surface_fit(const char *path, int surface_id,
                              int dest_w, int dest_h);

#endif /* BMP_H */
