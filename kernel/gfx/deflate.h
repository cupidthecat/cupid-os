/* deflate.h - RFC 1951 raw DEFLATE decoder (kernel-side).
 *
 * Hand-rolled inflate. No zlib wrapper handling - feed raw deflate
 * bytes (caller skips zlib CMF/FLG and Adler-32 if present). Used by:
 *   - png.c   (zlib-wrapped IDAT, header skipped at call site)
 *   - woff1.c (per-table zlib, header skipped at call site)
*/

#ifndef DEFLATE_H
#define DEFLATE_H

#include "types.h"

#define KDEFLATE_OK   0
#define KDEFLATE_ERR -1

/* Decode raw DEFLATE bytes (no zlib wrapper) into a caller-provided
 * output buffer. out_len is the expected uncompressed size; under- or
 * over-runs are reported as KDEFLATE_ERR.
 *
 * Returns KDEFLATE_OK on success, KDEFLATE_ERR on any malformed input
 * or buffer mismatch.*/
int kdeflate_raw(const uint8_t *src, uint32_t src_len,
                 uint8_t *out, uint32_t out_len);

#endif /* DEFLATE_H */
