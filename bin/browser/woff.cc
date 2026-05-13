/* WOFF1 (W3C WOFF File Format 1.0) decoder.
 *
 * Web fonts are typically served as WOFF1 (zlib-compressed sfnt) or
 * WOFF2 (Brotli; see woff2.cc). woff1_unwrap reverses the wrapping:
 * read the 44-byte header, walk the per-table directory, kdeflate_raw
 * each compressed table, and rebuild a plain sfnt with the standard
 * 12-byte offset subtable + 16-byte directory entries.
 *
 * Caller invokes only after detecting the 'wOFF' magic. Returns a
 * fresh kmalloc'd sfnt buffer (caller frees) or NULL on any failure -
 * caller falls through to next URL in the @font-face src list.
 *
 * Reference:
 *   https://www.w3.org/TR/WOFF/
 *   kernel/gfx/png.c (zlib-strip pattern: skip 2-byte CMF/FLG header,
 *                    drop 4-byte trailing Adler-32, kdeflate_raw rest)
 */

int woff_be32(char *p, int o) {
    int b0 = (int)(unsigned char)p[o];
    int b1 = (int)(unsigned char)p[o+1];
    int b2 = (int)(unsigned char)p[o+2];
    int b3 = (int)(unsigned char)p[o+3];
    return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}

int woff_be16(char *p, int o) {
    int b0 = (int)(unsigned char)p[o];
    int b1 = (int)(unsigned char)p[o+1];
    return (b0 << 8) | b1;
}

void woff_wr32(char *p, int o, int v) {
    p[o]   = (char)((v >> 24) & 0xFF);
    p[o+1] = (char)((v >> 16) & 0xFF);
    p[o+2] = (char)((v >>  8) & 0xFF);
    p[o+3] = (char)(v & 0xFF);
}

void woff_wr16(char *p, int o, int v) {
    p[o]   = (char)((v >> 8) & 0xFF);
    p[o+1] = (char)(v & 0xFF);
}

/* Largest power of 2 <= n. n must be > 0. */
int woff_largest_pow2_le(int n) {
    int v = 1;
    while ((v << 1) <= n) v = v << 1;
    return v;
}

int woff_log2_pow2(int x) {
    int n = 0;
    while ((x >> n) > 1) n = n + 1;
    return n;
}

int woff_align4(int x) {
    int r = x & 3;
    if (r == 0) return x;
    return x + (4 - r);
}

/* Caller passes the raw fetched body (whole WOFF1 file). On success:
 *   - returns a kmalloc'd sfnt blob
 *   - sets *out_len to the sfnt blob size
 * On failure returns NULL with no allocation (or frees its own scratch). */
char *woff1_unwrap(char *src, int src_len, int *out_len) {
    if (!src || src_len < 44) return (char*)0;
    if (src[0] != 'w' || src[1] != 'O' ||
        src[2] != 'F' || src[3] != 'F') return (char*)0;

    int flavor     = woff_be32(src, 4);
    int num_tables = woff_be16(src, 12);
    int total_sfnt = woff_be32(src, 16);

    if (num_tables <= 0 || num_tables > 64) return (char*)0;
    if (total_sfnt < 12) return (char*)0;
    if (total_sfnt > 1024 * 1024) return (char*)0;

    int dir_size = num_tables * 20;
    if (44 + dir_size > src_len) return (char*)0;

    char *out = (char*)kmalloc(total_sfnt);
    if (!out) return (char*)0;
    /* Pre-zero so 4-byte alignment padding never carries undefined bytes
     * into the parser. */
    for (int i = 0; i < total_sfnt; i = i + 1) out[i] = 0;

    /* sfnt 12-byte offset subtable. */
    int pow2 = woff_largest_pow2_le(num_tables);
    int sr   = pow2 * 16;
    int es   = woff_log2_pow2(pow2);
    int rs   = num_tables * 16 - sr;
    woff_wr32(out, 0, flavor);
    woff_wr16(out, 4,  num_tables);
    woff_wr16(out, 6,  sr);
    woff_wr16(out, 8,  es);
    woff_wr16(out, 10, rs);

    int dir_cur  = 12;
    int data_cur = 12 + 16 * num_tables;
    data_cur = woff_align4(data_cur);

    for (int i = 0; i < num_tables; i = i + 1) {
        int eo = 44 + i * 20;
        int tag      = woff_be32(src, eo + 0);
        int sd_off   = woff_be32(src, eo + 4);
        int sd_csize = woff_be32(src, eo + 8);
        int sd_orig  = woff_be32(src, eo + 12);
        int sd_csum  = woff_be32(src, eo + 16);

        if (sd_off < 0 || sd_csize < 0 || sd_orig < 0) {
            kfree(out); return (char*)0;
        }
        if (sd_off + sd_csize > src_len) {
            kfree(out); return (char*)0;
        }
        if (data_cur + sd_orig > total_sfnt) {
            kfree(out); return (char*)0;
        }

        if (sd_csize == sd_orig) {
            /* Stored uncompressed. */
            for (int k = 0; k < sd_orig; k = k + 1) {
                out[data_cur + k] = src[sd_off + k];
            }
        } else {
            /* zlib-wrapped DEFLATE. The 2-byte CMF/FLG header is skipped
             * before kdeflate_raw and the trailing 4-byte Adler-32 is
             * dropped from the byte length, exactly mirroring how
             * kernel/gfx/png.c hands IDAT chunks to the same primitive. */
            if (sd_csize < 6) { kfree(out); return (char*)0; }
            int rc = kdeflate_raw(src + sd_off + 2,
                                  sd_csize - 6,
                                  out + data_cur,
                                  sd_orig);
            if (rc != 0) { kfree(out); return (char*)0; }
        }

        woff_wr32(out, dir_cur +  0, tag);
        woff_wr32(out, dir_cur +  4, sd_csum);
        woff_wr32(out, dir_cur +  8, data_cur);
        woff_wr32(out, dir_cur + 12, sd_orig);
        dir_cur  = dir_cur + 16;
        data_cur = data_cur + sd_orig;
        data_cur = woff_align4(data_cur);
    }

    *out_len = total_sfnt;
    return out;
}
