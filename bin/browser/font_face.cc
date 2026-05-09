/* §2.x @font-face web-font registry.
 *
 * Reference: blink/Source/core/css/CSSFontFace.{cpp,h},
 * RemoteFontFaceSource.{cpp,h}, FontResource.{cpp,h}, CSSFontSelector.{cpp,h}.
 *
 * Pipeline:
 *   1. css_at_font_face (css.cc) parses an `@font-face { font-family: 'X';
 *      src: url('Y') format('truetype'); font-weight: W; font-style: S }`
 *      block and calls font_face_add_rule.
 *   2. font_face_add_rule fetches the URL synchronously (commit 3 path; the
 *      async pump in commit 4 replaces this with a deferred fetch_url),
 *      copies the bytes into a static blob slot, and hands them to
 *      fontsys_register_blob to obtain a face_id.
 *   3. cs_face_id_for (layout.cc) consults font_face_match BEFORE the
 *      generic fontsys_match path, so any @font-face hit takes priority
 *      over the kernel's preinstalled faces.
 *
 * Limits: 8 webfonts per page, 512 KiB per blob (covers Liberation /
 * Noto / Roboto comfortably). Format gate: only `format("truetype")`,
 * `format("opentype")`, or no format token (bare url()) are accepted.
 * WOFF/WOFF2 are skipped with a serial warning - we lack a Brotli/zlib
 * decompressor in the browser. Same-origin / CORS is ignored (single-user
 * OS, no security boundary). */

/* State enum. */
enum {
    FF_S_PENDING = 0,
    FF_S_LOADED  = 1,
    FF_S_FAILED  = 2,
    FF_S_SKIPPED = 3
};

/* Storage. Sized as static arrays so allocator pressure stays bounded
 * and slots survive across repaints. */

int  ff_count;
int  ff_state_dirty;     /* 1 when a slot transitioned PENDING -> LOADED
                          * since the last font_face_state_clear */

/* 4 slots, 256 KiB each = 1 MiB, fits CC_MAX_DATA alongside the rest
 * of the browser globals (page_buf 512K, attr_pool 128K, cs/rt/la
 * pools, ~2.5 MiB total). Liberation, Roboto, Noto regular plus bold
 * all fit under 200 KiB each. */
char ff_family[4][64];
int  ff_family_len[4];

char ff_url[4][1024];     /* URL_MAX */
int  ff_url_len[4];

int  ff_weight  [4];
int  ff_italic  [4];
int  ff_face_id [4];      /* -1 until LOADED */
int  ff_state   [4];

/* Blob storage is heap-allocated via kmalloc when a font arrives, NOT a
 * static array. CupidC data section is capped at 4 MiB and the rest of
 * the browser globals (page_buf, attr_pool, cs/rt/la pools) already eat
 * most of it. Each pointer stays alive for the life of the program -
 * fontsys retains the pointer (take_ownership=0). */
char *ff_blob_ptr[4];
int   ff_blob_len[4];

void font_face_init(void) {
    ff_count = 0;
    ff_state_dirty = 0;
    for (int i = 0; i < 4; i = i + 1) {
        ff_family_len[i] = 0;
        ff_url_len   [i] = 0;
        ff_weight    [i] = 400;
        ff_italic    [i] = 0;
        ff_face_id   [i] = -1;
        ff_state     [i] = FF_S_PENDING;
        ff_blob_len  [i] = 0;
        /* Heap blobs persist across pages: fontsys still holds the
         * pointer for previously-registered faces. ff_count reset alone
         * decouples per-page family lookup from the underlying allocation. */
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

/* Record a webfont rule and (in the async model) leave it PENDING for
 * the pump to fetch. Page renders with the fallback face; when the pump
 * finishes the fetch and registers the blob, ff_state_dirty fires and
 * the main loop reflows + repaints (FOUT). Reference:
 * blink/Source/core/css/RemoteFontFaceSource.cpp::beginLoadIfNeeded. */
int font_face_add_rule(char *family, int family_len,
                       char *src_url, int src_url_len,
                       int weight, int italic) {
    if (ff_count >= 4) {
        serial_printf("[browser] @font-face: budget exceeded, skipping\n");
        return -1;
    }
    if (family_len <= 0 || src_url_len <= 0) return -1;

    /* Strip quotes from the family name (matches CSS authoring style). */
    int qoff;
    int qlen;
    ff_strip_quotes(family, family_len, &qoff, &qlen);
    if (qlen <= 0 || qlen >= 64) return -1;

    /* Dedup by URL: same-URL declarations share a face_id. The pump
     * promotes the first slot it sees with that URL; later slots inherit
     * the result during do_fetch / via cache lookup. */
    int reuse = -1;
    for (int i = 0; i < ff_count; i = i + 1) {
        if (ff_url_len[i] == src_url_len &&
            ff_state[i] == FF_S_LOADED) {
            int eq = 1;
            for (int k = 0; k < src_url_len; k = k + 1) {
                if (ff_url[i][k] != src_url[k]) { eq = 0; break; }
            }
            if (eq) { reuse = i; break; }
        }
    }

    int slot = ff_count;
    ff_count = ff_count + 1;

    /* Copy family + url into the slot. */
    for (int i = 0; i < qlen; i = i + 1) ff_family[slot][i] = family[qoff + i];
    ff_family_len[slot] = qlen;
    int ulen = src_url_len; if (ulen > 1023) ulen = 1023;
    for (int i = 0; i < ulen; i = i + 1) ff_url[slot][i] = src_url[i];
    ff_url[slot][ulen] = 0;
    ff_url_len[slot] = ulen;
    ff_weight[slot] = weight;
    ff_italic[slot] = italic;

    if (reuse >= 0) {
        ff_face_id[slot] = ff_face_id[reuse];
        ff_state[slot]   = FF_S_LOADED;
        ff_state_dirty   = 1;     /* harmless: triggers a reflow that re-runs match */
        return slot;
    }

    ff_face_id[slot] = -1;
    ff_state[slot]   = FF_S_PENDING;
    return slot;
}

/* Internal: drive ONE pending slot through fetch + register. Returns 1 if
 * a slot was advanced, 0 if no work. Saves / restores cur_* + page_buf so
 * the outer page state survives the synchronous fetch (per-pump-tick
 * blocking is acceptable; the page has already first-painted). */
int ff_advance_one_pending(void) {
    int slot = -1;
    for (int i = 0; i < ff_count; i = i + 1) {
        if (ff_state[i] == FF_S_PENDING) { slot = i; break; }
    }
    if (slot < 0) return 0;

    /* URL-cache hit (a slot fetched in this same pump session). */
    for (int i = 0; i < ff_count; i = i + 1) {
        if (i == slot) continue;
        if (ff_state[i] != FF_S_LOADED) continue;
        if (ff_url_len[i] != ff_url_len[slot]) continue;
        int eq = 1;
        for (int k = 0; k < ff_url_len[i]; k = k + 1) {
            if (ff_url[i][k] != ff_url[slot][k]) { eq = 0; break; }
        }
        if (eq) {
            ff_face_id[slot] = ff_face_id[i];
            ff_state[slot]   = FF_S_LOADED;
            ff_state_dirty   = 1;
            return 1;
        }
    }

    char saved_host[256];
    char saved_path[1024];
    char saved_url [1024];
    int  saved_port    = cur_port;
    int  saved_https   = cur_is_https;
    int  saved_page_len = page_len;
    b_strcpy_n(saved_host, cur_host, 256);
    b_strcpy_n(saved_path, cur_path, 1024);
    b_strcpy_n(saved_url,  cur_url,  1024);

    char ct[128]; ct[0] = 0;
    char absu[1024];
    compute_url_relative(ff_url[slot], absu, 1024);
    int rc = fetch_url(absu, ct);

    int copy_len = page_len;
    /* 1 MiB hard cap per webfont. Anything bigger is almost certainly a
     * served HTML 404 page mistakenly tagged with a TTF URL. */
    if (copy_len > 1024 * 1024) copy_len = 1024 * 1024;

    int face_id = -1;
    if (rc == 0 && copy_len > 0) {
        char *blob = (char*)kmalloc(copy_len);
        if (blob) {
            for (int k = 0; k < copy_len; k = k + 1) blob[k] = page_buf[k];
            ff_blob_ptr[slot] = blob;
            ff_blob_len[slot] = copy_len;
            face_id = fontsys_register_blob(blob, copy_len, 0);
            serial_printf("[browser] @font-face load: %s -> face %d (%d bytes)\n",
                          ff_family[slot], face_id, copy_len);
        } else {
            serial_printf("[browser] @font-face: kmalloc failed for %s\n", ff_url[slot]);
        }
    } else {
        serial_printf("[browser] @font-face fetch failed: %s\n", ff_url[slot]);
    }

    b_strcpy_n(cur_host, saved_host, 256);
    b_strcpy_n(cur_path, saved_path, 1024);
    b_strcpy_n(cur_url,  saved_url,  1024);
    cur_port     = saved_port;
    cur_is_https = saved_https;
    page_len     = saved_page_len;

    if (face_id >= 0) {
        ff_face_id[slot] = face_id;
        ff_state[slot]   = FF_S_LOADED;
        ff_state_dirty   = 1;
    } else {
        ff_state[slot]   = FF_S_FAILED;
        ff_face_id[slot] = -1;
    }
    return 1;
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

/* Walk a comma-separated CSS font-family list, comparing each token
 * (after quote/whitespace strip) against the registry. Returns the
 * face_id of the best-scoring loaded slot, or -1 if no match. */
int font_face_match(char *family, int family_len, int weight, int italic) {
    if (family_len <= 0 || ff_count == 0) return -1;
    int best = -1;
    int best_score = 0x7fffffff;

    int i = 0;
    while (i < family_len) {
        /* Skip leading ws + commas. */
        while (i < family_len &&
               (family[i] == ' ' || family[i] == '\t' ||
                family[i] == ',' || family[i] == '\n' || family[i] == '\r')) {
            i = i + 1;
        }
        if (i >= family_len) break;
        int t_start = i;
        /* Walk to next comma, but allow quoted segments to span commas. */
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
            int sc = ff_score_match(ff_weight[s], ff_italic[s], weight, italic);
            if (sc < best_score) { best_score = sc; best = s; }
        }
        if (best >= 0) return ff_face_id[best];
    }
    return -1;
}

/* Drive at most one PENDING slot per call so a single slow font doesn't
 * stall the main loop for multiple round-trips at once. The pump is
 * called from main.cc's render loop; subsequent ticks pick up remaining
 * slots until all are LOADED or FAILED. */
void font_face_pump(void) {
    ff_advance_one_pending();
}
