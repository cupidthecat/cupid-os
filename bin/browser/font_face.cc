/* @font-face web-font registry.
 *
 * Reference: blink/Source/core/css/CSSFontFace.{cpp,h},
 * RemoteFontFaceSource.{cpp,h}, FontResource.{cpp,h}, CSSFontSelector.{cpp,h}.
 *
 * Pipeline:
 *   1. css_at_font_face (css.cc) parses an `@font-face` block and calls
 *      font_face_add_rule_n (or font_face_add_rule for single-URL src).
 *   2. The render loop pumps one PENDING slot per tick through fetch_url,
 *      WOFF1/WOFF2 unwrap if needed, then fontsys_register_blob.
 *   3. cs_face_id_for_cp (layout.cc) consults font_face_match_cp before
 *      the kernel fontsys_match path, so an @font-face hit with a
 *      matching unicode-range wins over the preinstalled faces.
 *
 * Limits: 128 slots per page (covers Google Fonts subsetted families),
 * 3 fallback URLs per slot (covers `src: url(.woff2), url(.woff),
 * url(.ttf)` style declarations), 8 unicode-range tokens per slot, 1 MiB
 * per blob. Same-origin / CORS ignored (single-user OS). */

/* State enum. */
enum {
    FF_S_PENDING = 0,
    FF_S_LOADED  = 1,
    FF_S_FAILED  = 2,
    FF_S_SKIPPED = 3
};

/* Storage. Static arrays so allocator pressure stays bounded and slots
 * survive across repaints. cupidc requires literal sizes; FF_MAX_SLOTS
 * is documented as 128 / FF_MAX_URLS as 3 / FF_MAX_RANGES as 8. */

int  ff_count;
int  ff_state_dirty;     /* 1 when a slot transitioned PENDING -> LOADED
                          * since the last font_face_state_clear */

char ff_family[128][64];
int  ff_family_len[128];

char ff_url[128][3][1024];      /* up to 3 fallback URLs per slot */
int  ff_url_len[128][3];
int  ff_url_count[128];         /* how many URLs are populated */
int  ff_url_tried[128];         /* which URL the pump tries next */

int  ff_weight  [128];
int  ff_italic  [128];
int  ff_face_id [128];          /* -1 until LOADED */
int  ff_state   [128];

/* Blob storage is heap-allocated via kmalloc when a font arrives, NOT a
 * static array. CupidC data section is capped at 4 MiB and the rest of
 * the browser globals (page_buf, attr_pool, cs/rt/la pools) already eat
 * most of it. Per-page eviction frees these on navigate. */
char *ff_blob_ptr[128];
int   ff_blob_len[128];

/* unicode-range descriptor storage. count == 0 means "covers all
 * codepoints" (the default when no unicode-range descriptor was given). */
int  ff_range_count[128];
int  ff_range_lo[128][8];
int  ff_range_hi[128][8];

/* Per-pump diagnostic counters; logged at end of pump cycle. */
int  ff_diag_loaded;
int  ff_diag_failed;
int  ff_diag_skipped;

void font_face_init(void) {
    /* Free per-page heap blobs and unregister kernel faces. Single-threaded
     * and paint-quiescent at navigate time, so no refcount needed. */
    for (int i = 0; i < ff_count; i = i + 1) {
        if (ff_face_id[i] >= 0) fontsys_unregister(ff_face_id[i]);
        if (ff_blob_ptr[i]) {
            kfree(ff_blob_ptr[i]);
            ff_blob_ptr[i] = (char*)0;
        }
    }
    ff_count = 0;
    ff_state_dirty = 0;
    ff_diag_loaded = 0;
    ff_diag_failed = 0;
    ff_diag_skipped = 0;
    for (int i = 0; i < 128; i = i + 1) {
        ff_family_len[i] = 0;
        ff_url_count [i] = 0;
        ff_url_tried [i] = 0;
        ff_url_len[i][0] = 0;
        ff_url_len[i][1] = 0;
        ff_url_len[i][2] = 0;
        ff_weight    [i] = 400;
        ff_italic    [i] = 0;
        ff_face_id   [i] = -1;
        ff_state     [i] = FF_S_PENDING;
        ff_blob_len  [i] = 0;
        ff_blob_ptr  [i] = (char*)0;
        ff_range_count[i] = 0;
    }
}

int font_face_count(void) { return ff_count; }

int font_face_any_state_changed(void) { return ff_state_dirty; }

void font_face_state_clear(void) { ff_state_dirty = 0; }

/* Case-insensitive ASCII compare of two byte ranges. */
int ff_ieq_n(char *a, int al, char *b, int bl) {
    if (al != bl) return 0;
    for (int i = 0; i < al; i = i + 1) {
        char ca = a[i]; char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = ca + 32;
        if (cb >= 'A' && cb <= 'Z') cb = cb + 32;
        if (ca != cb) return 0;
    }
    return 1;
}

/* Strip a single pair of '...' or "..." surrounding the family name
 * (CSS allows quoted family identifiers). Operates on (off, len) -> updates
 * out_off and out_len. */
void ff_strip_quotes(char *s, int len, int *out_off, int *out_len) {
    int o = 0;
    int l = len;
    while (l > 0 && (s[o] == ' ' || s[o] == '\t')) { o = o + 1; l = l - 1; }
    while (l > 0 && (s[o + l - 1] == ' ' || s[o + l - 1] == '\t')) l = l - 1;
    if (l >= 2 && (s[o] == '\'' || s[o] == '"') && s[o + l - 1] == s[o]) {
        o = o + 1; l = l - 2;
    }
    *out_off = o;
    *out_len = l;
}

/* Internal: append a URL to slot's fallback chain. ulen <= 0 is a no-op. */
void ff_slot_push_url(int slot, char *u, int ulen) {
    if (ulen <= 0) return;
    int idx = ff_url_count[slot];
    if (idx >= 3) return;     /* full */
    int n = ulen; if (n > 1023) n = 1023;
    for (int i = 0; i < n; i = i + 1) ff_url[slot][idx][i] = u[i];
    ff_url[slot][idx][n] = 0;
    ff_url_len[slot][idx] = n;
    ff_url_count[slot] = idx + 1;
}

/* Multi-URL + unicode-range registration. Used by css.cc's @font-face
 * walker for `src: url(a.woff2), url(a.woff), url(a.ttf)` and for rules
 * with `unicode-range:` descriptors. range_lo/range_hi may be NULL when
 * n_ranges == 0; in that case the slot covers all codepoints. */
int font_face_add_rule_n(char *family, int family_len,
                         char *url0, int url0_len,
                         char *url1, int url1_len,
                         char *url2, int url2_len,
                         int weight, int italic,
                         int *range_lo, int *range_hi, int n_ranges) {
    if (ff_count >= 128) {
        serial_printf("[browser] @font-face: 128-slot budget exhausted, skipping\n");
        ff_diag_skipped = ff_diag_skipped + 1;
        return -1;
    }
    if (family_len <= 0) return -1;
    if (url0_len <= 0 && url1_len <= 0 && url2_len <= 0) return -1;

    int qoff; int qlen;
    ff_strip_quotes(family, family_len, &qoff, &qlen);
    if (qlen <= 0 || qlen >= 64) return -1;

    int slot = ff_count;
    ff_count = ff_count + 1;

    for (int i = 0; i < qlen; i = i + 1) ff_family[slot][i] = family[qoff + i];
    ff_family_len[slot] = qlen;

    ff_url_count[slot] = 0;
    ff_url_tried[slot] = 0;
    ff_slot_push_url(slot, url0, url0_len);
    ff_slot_push_url(slot, url1, url1_len);
    ff_slot_push_url(slot, url2, url2_len);

    ff_weight[slot] = weight;
    ff_italic[slot] = italic;

    int rc = n_ranges; if (rc > 8) rc = 8;
    ff_range_count[slot] = rc;
    for (int i = 0; i < rc; i = i + 1) {
        ff_range_lo[slot][i] = range_lo[i];
        ff_range_hi[slot][i] = range_hi[i];
    }

    /* Dedup by primary URL: same-URL declarations share a face_id. */
    int reuse = -1;
    int p_len = ff_url_len[slot][0];
    for (int i = 0; i < ff_count - 1; i = i + 1) {
        if (ff_state[i] != FF_S_LOADED) continue;
        if (ff_url_count[i] == 0) continue;
        if (ff_url_len[i][0] != p_len) continue;
        int eq = 1;
        for (int k = 0; k < p_len; k = k + 1) {
            if (ff_url[i][0][k] != ff_url[slot][0][k]) { eq = 0; break; }
        }
        if (eq) { reuse = i; break; }
    }

    if (reuse >= 0) {
        ff_face_id[slot] = ff_face_id[reuse];
        ff_state[slot]   = FF_S_LOADED;
        ff_state_dirty   = 1;
        return slot;
    }
    ff_face_id[slot] = -1;
    ff_state[slot]   = FF_S_PENDING;
    return slot;
}

/* Single-URL convenience wrapper; preserves the old call site shape from
 * css.cc until the multi-URL walker lands. */
int font_face_add_rule(char *family, int family_len,
                       char *src_url, int src_url_len,
                       int weight, int italic) {
    return font_face_add_rule_n(family, family_len,
                                src_url, src_url_len,
                                (char*)0, 0, (char*)0, 0,
                                weight, italic,
                                (int*)0, (int*)0, 0);
}

/* Try one URL of the slot (the one indexed by ff_url_tried). On success
 * promotes the slot to LOADED and returns 1. On failure, advances
 * ff_url_tried; if more URLs remain returns 1 (PENDING for next pump),
 * otherwise marks slot FAILED and returns 1. */
int ff_try_one_url(int slot) {
    int idx = ff_url_tried[slot];
    if (idx >= ff_url_count[slot]) {
        ff_state[slot] = FF_S_FAILED;
        ff_diag_failed = ff_diag_failed + 1;
        return 1;
    }
    char saved_host[256];
    char saved_path[1024];
    char saved_url [1024];
    char saved_status[256];
    int  saved_port    = cur_port;
    int  saved_https   = cur_is_https;
    int  saved_page_len = page_len;
    b_strcpy_n(saved_host, cur_host, 256);
    b_strcpy_n(saved_path, cur_path, 1024);
    b_strcpy_n(saved_url,  cur_url,  1024);
    /* Preserve status_msg across the sub-resource fetch (see image.cc
     * for the same pattern). fetch_url writes `HTTP error: 404` etc on
     * non-2xx responses; without this save+restore those errors would
     * end up in the page-level status bar each time a fake @font-face
     * URL 404s. */
    b_strcpy_n(saved_status, status_msg, 256);

    char ct[128]; ct[0] = 0;
    char absu[1024];
    compute_url_relative(ff_url[slot][idx], absu, 1024);
    int rc = fetch_url(absu, ct);

    int copy_len = page_len;
    if (copy_len > 1024 * 1024) copy_len = 1024 * 1024;

    int face_id = -1;
    char *blob = (char*)0;
    if (rc == 0 && copy_len > 0) {
        blob = (char*)kmalloc(copy_len);
        if (blob) {
            for (int k = 0; k < copy_len; k = k + 1) blob[k] = page_buf[k];
            /* WOFF1 / WOFF2 magic detect: unwrap to plain sfnt before
             * handing to fontsys. woff1_unwrap / woff2_unwrap return a
             * fresh kmalloc'd buffer (we kfree the wrapper) or NULL on
             * format unsupported / decode error. */
            if (copy_len >= 4) {
                unsigned char m0 = (unsigned char)blob[0];
                unsigned char m1 = (unsigned char)blob[1];
                unsigned char m2 = (unsigned char)blob[2];
                unsigned char m3 = (unsigned char)blob[3];
                if (m0 == 'w' && m1 == 'O' && m2 == 'F' && m3 == 'F') {
                    int sf_len = 0;
                    char *sf = woff1_unwrap(blob, copy_len, &sf_len);
                    if (sf) {
                        kfree(blob);
                        blob = sf;
                        copy_len = sf_len;
                    } else {
                        kfree(blob); blob = (char*)0;
                        serial_printf("[browser] @font-face: WOFF1 decode failed: %s\n", ff_url[slot][idx]);
                    }
                } else if (m0 == 'w' && m1 == 'O' && m2 == 'F' && m3 == '2') {
                    int sf_len = 0;
                    char *sf = woff2_unwrap(blob, copy_len, &sf_len);
                    if (sf) {
                        kfree(blob);
                        blob = sf;
                        copy_len = sf_len;
                    } else {
                        kfree(blob); blob = (char*)0;
                        serial_printf("[browser] @font-face: WOFF2 decode failed: %s\n", ff_url[slot][idx]);
                    }
                }
            }
            if (blob) {
                face_id = fontsys_register_blob(blob, copy_len, 0);
                if (face_id >= 0) {
                    ff_blob_ptr[slot] = blob;
                    ff_blob_len[slot] = copy_len;
                    serial_printf("[browser] @font-face load: %s -> face %d (%d bytes)\n",
                                  ff_family[slot], face_id, copy_len);
                } else {
                    kfree(blob);
                    blob = (char*)0;
                    serial_printf("[browser] @font-face: register rejected: %s\n", ff_url[slot][idx]);
                }
            }
        } else {
            serial_printf("[browser] @font-face: kmalloc failed for %s\n", ff_url[slot][idx]);
        }
    } else {
        serial_printf("[browser] @font-face fetch failed: %s\n", ff_url[slot][idx]);
    }

    b_strcpy_n(cur_host, saved_host, 256);
    b_strcpy_n(cur_path, saved_path, 1024);
    b_strcpy_n(cur_url,  saved_url,  1024);
    b_strcpy_n(status_msg, saved_status, 256);
    cur_port     = saved_port;
    cur_is_https = saved_https;
    page_len     = saved_page_len;

    if (face_id >= 0) {
        ff_face_id[slot] = face_id;
        ff_state[slot]   = FF_S_LOADED;
        ff_state_dirty   = 1;
        ff_diag_loaded   = ff_diag_loaded + 1;
        return 1;
    }
    /* This URL failed; advance and try next on the next pump tick. */
    ff_url_tried[slot] = idx + 1;
    if (ff_url_tried[slot] >= ff_url_count[slot]) {
        ff_state[slot] = FF_S_FAILED;
        ff_face_id[slot] = -1;
        ff_diag_failed = ff_diag_failed + 1;
    }
    return 1;
}

/* Drive ONE pending slot through fetch + register. Returns 1 if any
 * slot was touched, 0 if no PENDING slots remain. */
int ff_advance_one_pending(void) {
    int slot = -1;
    for (int i = 0; i < ff_count; i = i + 1) {
        if (ff_state[i] == FF_S_PENDING) { slot = i; break; }
    }
    if (slot < 0) return 0;

    /* URL-cache hit on the slot's primary URL across already-loaded slots. */
    int p_len = (ff_url_count[slot] > 0) ? ff_url_len[slot][0] : 0;
    if (p_len > 0) {
        for (int i = 0; i < ff_count; i = i + 1) {
            if (i == slot) continue;
            if (ff_state[i] != FF_S_LOADED) continue;
            if (ff_url_count[i] == 0) continue;
            if (ff_url_len[i][0] != p_len) continue;
            int eq = 1;
            for (int k = 0; k < p_len; k = k + 1) {
                if (ff_url[i][0][k] != ff_url[slot][0][k]) { eq = 0; break; }
            }
            if (eq) {
                ff_face_id[slot] = ff_face_id[i];
                ff_state[slot]   = FF_S_LOADED;
                ff_state_dirty   = 1;
                ff_diag_loaded   = ff_diag_loaded + 1;
                return 1;
            }
        }
    }

    return ff_try_one_url(slot);
}

/* CSS weight matching (Blink core/css/FontFaceSet::matchingFaces). Simplified:
 *   - exact match preferred
 *   - else if requested <= 500: try 400 then closest below
 *   - else: try 700 then closest above
 *   - italic: exact preferred, else any */
int ff_score_match(int slot_w, int slot_i, int want_w, int want_i) {
    int s = 0;
    int dw = slot_w - want_w;
    if (dw < 0) dw = -dw;
    s = s + dw;
    if (slot_i != want_i) s = s + 200;
    return s;
}

/* True if codepoint cp falls inside any of slot s's unicode-range
 * intervals. A slot with ff_range_count == 0 covers all codepoints. */
int ff_slot_covers_cp(int s, int cp) {
    int n = ff_range_count[s];
    if (n <= 0) return 1;
    for (int k = 0; k < n; k = k + 1) {
        if (cp >= ff_range_lo[s][k] && cp <= ff_range_hi[s][k]) return 1;
    }
    return 0;
}

/* Walk a comma-separated CSS font-family list, comparing each token
 * (after quote/whitespace strip) against the registry. If `cp` is >= 0,
 * also require the slot's unicode-range to cover `cp` (slots with a
 * narrower range win against range-less slots when both family-match).
 * Returns the face_id of the best-scoring loaded slot, or -1. */
int font_face_match_cp(char *family, int family_len,
                       int weight, int italic, int cp) {
    if (family_len <= 0 || ff_count == 0) return -1;
    int best = -1;
    int best_score = 0x7fffffff;

    int i = 0;
    while (i < family_len) {
        while (i < family_len &&
               (family[i] == ' ' || family[i] == '\t' ||
                family[i] == ',' || family[i] == '\n' || family[i] == '\r')) {
            i = i + 1;
        }
        if (i >= family_len) break;
        int t_start = i;
        if (family[i] == '\'' || family[i] == '"') {
            char q = family[i];
            i = i + 1;
            while (i < family_len && family[i] != q) i = i + 1;
            if (i < family_len) i = i + 1;
        } else {
            while (i < family_len && family[i] != ',') i = i + 1;
        }
        int t_end = i;
        int qoff;
        int qlen;
        ff_strip_quotes(family + t_start, t_end - t_start, &qoff, &qlen);
        if (qlen <= 0) continue;

        for (int s = 0; s < ff_count; s = s + 1) {
            if (ff_state[s] != FF_S_LOADED) continue;
            if (!ff_ieq_n(family + t_start + qoff, qlen,
                          ff_family[s], ff_family_len[s])) continue;
            if (cp >= 0 && !ff_slot_covers_cp(s, cp)) continue;
            int sc = ff_score_match(ff_weight[s], ff_italic[s], weight, italic);
            /* Range-having slots beat range-less slots when both match,
             * so Google Fonts' Latin subset wins over a catch-all entry
             * for ASCII text without skipping the catch-all for Cyrillic. */
            if (cp >= 0 && ff_range_count[s] > 0) sc = sc - 50;
            if (sc < best_score) { best_score = sc; best = s; }
        }
        if (best >= 0) return ff_face_id[best];
    }
    return -1;
}

/* Range-blind variant kept for callers that don't have a codepoint. */
int font_face_match(char *family, int family_len, int weight, int italic) {
    return font_face_match_cp(family, family_len, weight, italic, -1);
}

/* Drive at most one PENDING slot per call so a single slow font doesn't
 * stall the main loop. Logs an end-of-pass diagnostic when no PENDING
 * slot remains. */
void font_face_pump(void) {
    int worked = ff_advance_one_pending();
    if (worked) return;
    /* No pending slots; print a one-time diag if we just finished. */
    if (ff_diag_loaded || ff_diag_failed || ff_diag_skipped) {
        serial_printf("[browser] @font-face: %d loaded / %d failed / %d skipped\n",
                      ff_diag_loaded, ff_diag_failed, ff_diag_skipped);
        ff_diag_loaded = 0;
        ff_diag_failed = 0;
        ff_diag_skipped = 0;
    }
}
