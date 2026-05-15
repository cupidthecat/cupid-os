/**
 * jpeg.h - Minimal baseline JPEG decoder for cupid-os
 *
 * Decodes a JFIF/EXIF JPEG byte stream held in memory into a 32bpp
 * XRGB pixel buffer (0x00RRGGBB).
 *
 * Supported: SOF0 baseline / SOF1 extended sequential, 8-bit samples,
 * 1- or 3-component images, sub-samplings 1x1 / 2x1 / 1x2 / 2x2,
 * standard Huffman tables, restart markers (DRI / RSTn).
 * NOT supported: progressive (SOF2), arithmetic coding, 12-bit, CMYK.
*/

#ifndef JPEG_H
#define JPEG_H

#include "types.h"

#define JPEG_OK         0
#define JPEG_EINVAL    -1
#define JPEG_EFORMAT   -2
#define JPEG_ENOMEM    -3
#define JPEG_ESTREAM   -4

/**
 * Decode a complete JPEG byte stream into a freshly-allocated XRGB buffer.
 *
 * On success returns JPEG_OK and writes:
 *   *out_pixels - heap pointer to width*height uint32_t pixels
 *                 (caller frees with kfree)
 *   *out_w, *out_h - image dimensions
*/
int jpeg_decode_mem(const uint8_t *data, uint32_t len,
                    uint32_t **out_pixels, int *out_w, int *out_h);

#endif /* JPEG_H */
