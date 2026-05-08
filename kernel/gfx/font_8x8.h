#ifndef FONT_8X8_H
#define FONT_8X8_H

#include "types.h"

/* 8x8 monospaced bitmap font.
 * Each character is 8 bytes; each byte is one row (MSB = leftmost pixel).
 * Covers ASCII 0-127. */
extern const uint8_t font_8x8[128][8];

#define FONT_W 8
#define FONT_H 8

#endif
