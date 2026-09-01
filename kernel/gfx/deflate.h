/* deflate.h - RFC 1951 raw DEFLATE decoder (kernel-side).
 *
 * This decoder accepts raw DEFLATE bytes. Callers skip the zlib CMF/FLG
 * header and Adler-32 trailer when present. Current callers are:
 *   - kernel/gfx/png.cc for zlib-wrapped IDAT data
 *   - bin/browser/woff.cc for per-table zlib data
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
