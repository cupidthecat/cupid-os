/* WOFF2 (W3C WOFF File Format 2.0) decoder.
 *
 * WOFF2 wraps an sfnt with Brotli compression of the entire transformed
 * font payload, plus a lossless transform on the glyf/loca tables. Full
 * implementation needs:
 *   1. Brotli decoder (kernel/gfx/brotli/) - separate task A3.
 *   2. Variable-length WOFF2 directory entries (Base128 origLength).
 *   3. glyf/loca transform inverse (per WOFF2 spec section 5.1) -
 *      reconstructs glyph outlines from 7 sub-streams and recomputes
 *      loca offsets.
 *   4. sfnt repacking (offset subtable + 16-byte directory entries).
 *
 * Current state: stub. Detects 'wOF2' magic and returns NULL so the
 * @font-face src walker falls through to a WOFF1 or TTF entry. Once
 * brotli lands, fill in the 4 steps above.
 *
 * Reference:
 *   https://www.w3.org/TR/WOFF2/
 *   google/woff2 (upstream reference c/dec/woff2_dec.cc)
 */

char *woff2_unwrap(char *src, int src_len, int *out_len) {
    (void)src;
    (void)src_len;
    (void)out_len;
    /* Brotli not yet linked into the kernel; let the caller fall over to
     * the next URL in the @font-face src list. */
    return (char*)0;
}
