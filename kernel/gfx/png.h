/**
 * png.h - Minimal PNG decoder for cupid-os
 *
 * Decodes a PNG image held in memory into a 32bpp XRGB pixel buffer
 * (0x00RRGGBB), suitable for direct blit to the framebuffer or upload
 * into a gfx2d image slot.
 *
 * Supported: PNG color types 0 (gray), 2 (RGB), 3 (palette), 6 (RGBA),
 * 8-bit-per-channel only.  Filters None/Sub/Up/Average/Paeth.
 * Interlacing (Adam7) is not supported; non-interlaced PNGs only.
*/

#ifndef PNG_H
#define PNG_H

#include "types.h"

#define PNG_OK             0
#define PNG_EINVAL        -1   /* malformed file or bad arg */
#define PNG_EFORMAT       -2   /* unsupported PNG variant */
#define PNG_ENOMEM        -3   /* allocation failed */
#define PNG_EINFLATE      -4   /* DEFLATE stream error */

/**
 * Decode a complete PNG byte stream into a freshly-allocated XRGB buffer.
 *
 * On success returns PNG_OK and writes:
 *   *out_pixels - heap pointer to width*height uint32_t pixels
 *                 (caller frees with kfree)
 *   *out_w, *out_h - image dimensions
 * On failure returns a negative PNG_E* and leaves outputs untouched.
*/
int png_decode_mem(const uint8_t *data, uint32_t len,
                   uint32_t **out_pixels, int *out_w, int *out_h);

#endif /* PNG_H */
