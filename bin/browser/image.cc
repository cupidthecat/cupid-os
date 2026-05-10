/* <img> fetch + decode pump.
 *
 * Mirrors font_face.cc's per-tick async pump. After first paint, walk
 * the DOM for T_IMG nodes whose `src` attr resolves to a fetchable URL,
 * queue them, and decode one per render tick. Branches on Content-Type
 * to feed PNG / JPEG into kernel/gfx/gfx2d_assets.gfx2d_image_load_mem,
 * which returns an opaque handle paint.cc draws via
 * gfx2d_image_draw_scaled.
 *
 * Reference: blink/Source/core/loader/ImageLoader.cpp (deferred fetch +
 * once-loaded relayout); blink/Source/core/rendering/RenderImage.cpp
 * (intrinsic size from decoded data). */

enum {
    IMG_S_PENDING = 0,
    IMG_S_LOADED  = 1,
    IMG_S_FAILED  = 2
};

int  img_count;
int  img_state_dirty;     /* 1 when at least one slot transitioned to LOADED */

/* Fixed-cap queue. Modern news sites stack a dozen or two thumbnails
 * per article; 32 covers the common case. Overflow is silently dropped
 * (placeholder paint stays). cupidc requires literal sizes. */
char img_url[32][1024];
int  img_url_len[32];
int  img_dom[32];          /* DOM node index of the <img> */
int  img_state[32];

void image_queue_init(void) {
    img_count = 0;
    img_state_dirty = 0;
    for (int i = 0; i < 32; i = i + 1) {
        img_url_len[i] = 0;
        img_dom[i]     = -1;
        img_state[i]   = IMG_S_PENDING;
    }
    /* Per-DOM-node handle table: reset every slot, not just the ones
     * the previous page populated. Otherwise <img>s in the new document
     * inherit stale handles from previous pages and paint the wrong
     * image (or panic on an already-freed handle). */
    for (int i = 0; i < 4096; i = i + 1) {
        n_img_handle[i] = -1;
        n_img_intrinsic_w[i] = 0;
        n_img_intrinsic_h[i] = 0;
    }
}

/* Free all decoded images on per-page navigation. Each handle came from
 * gfx2d_image_load_mem, which kmalloc'd both the metadata and the
 * decoded RGBA pixel buffer; gfx2d_image_free reclaims both. */
void image_evict_all(void) {
    for (int i = 0; i < nodes_count; i = i + 1) {
        int h = n_img_handle[i];
        if (h >= 0) gfx2d_image_free(h);
        n_img_handle[i] = -1;
        n_img_intrinsic_w[i] = 0;
        n_img_intrinsic_h[i] = 0;
    }
}

int image_any_state_changed(void) { return img_state_dirty; }
void image_state_clear(void) { img_state_dirty = 0; }

/* Walk the DOM for T_IMG nodes; queue ones with a non-empty src that we
 * haven't already enqueued. Capped at 32; further <img>s render as the
 * placeholder rectangle. */
void image_queue_collect(void) {
    if (img_count >= 32) return;
    for (int n = 0; n < nodes_count && img_count < 32; n = n + 1) {
        if (n_tag[n] != T_IMG) continue;
        if (n_img_handle[n] >= 0) continue;        /* already loaded */
        int src_off = dom_attr_get(n, "src");
        if (src_off < 0) continue;
        char *src = attr_pool + src_off;
        int slen = 0;
        while (slen < 1023 && src[slen]) slen = slen + 1;
        if (slen <= 0) continue;
        /* Dedup against in-flight queue: same URL maps to one fetch. If
         * we find a LOADED slot for the same URL, point this DOM node
         * at its handle right away. */
        int reuse_handle = -1;
        for (int j = 0; j < img_count; j = j + 1) {
            if (img_url_len[j] != slen) continue;
            int eq = 1;
            for (int k = 0; k < slen; k = k + 1) {
                if (img_url[j][k] != src[k]) { eq = 0; break; }
            }
            if (!eq) continue;
            if (img_state[j] == IMG_S_LOADED) {
                reuse_handle = n_img_handle[img_dom[j]];
            }
            break;
        }
        if (reuse_handle >= 0) {
            n_img_handle[n] = reuse_handle;
            n_img_intrinsic_w[n] = gfx2d_image_width(reuse_handle);
            n_img_intrinsic_h[n] = gfx2d_image_height(reuse_handle);
            img_state_dirty = 1;
            continue;
        }
        int slot = img_count;
        img_count = img_count + 1;
        for (int k = 0; k < slen; k = k + 1) img_url[slot][k] = src[k];
        img_url[slot][slen] = 0;
        img_url_len[slot] = slen;
        img_dom[slot] = n;
        img_state[slot] = IMG_S_PENDING;
    }
}

/* Branch the fetched body to the right decoder. PNG header is 8 bytes
 * starting `\x89PNG\r\n\x1a\n`; JPEG starts `\xff\xd8`. gfx2d_image_load_mem
 * itself sniffs the magic, so we just forward the bytes. */
int image_decode_blob(char *buf, int len) {
    if (len < 4) return -1;
    return gfx2d_image_load_mem(buf, len);
}

/* Drive ONE pending slot through fetch + decode per call. Returns 1 if
 * a slot was advanced, 0 if no PENDING slots. Saves / restores cur_*
 * + page_buf so the outer page state survives the synchronous fetch. */
int image_advance_one_pending(void) {
    int slot = -1;
    for (int i = 0; i < img_count; i = i + 1) {
        if (img_state[i] == IMG_S_PENDING) { slot = i; break; }
    }
    if (slot < 0) return 0;

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
    /* Preserve the page-level status message so a sub-resource fetch
     * failure (a 404 image, a fake @font-face URL) doesn't replace
     * the user-visible status with `HTTP error: 404`. fetch_url writes
     * status_msg unconditionally on non-2xx — sub-resource fetches
     * silently fail and the main page's status text stays intact. */
    b_strcpy_n(saved_status, status_msg, 256);

    char ct[128]; ct[0] = 0;
    char absu[1024];
    compute_url_relative(img_url[slot], absu, 1024);
    int rc = fetch_url(absu, ct);

    int handle = -1;
    if (rc == 0 && page_len > 0) {
        handle = image_decode_blob(page_buf, page_len);
    } else {
        serial_printf("[browser] <img> fetch failed: %s\n", img_url[slot]);
    }

    b_strcpy_n(cur_host, saved_host, 256);
    b_strcpy_n(cur_path, saved_path, 1024);
    b_strcpy_n(cur_url,  saved_url,  1024);
    b_strcpy_n(status_msg, saved_status, 256);
    cur_port     = saved_port;
    cur_is_https = saved_https;
    page_len     = saved_page_len;

    if (handle >= 0) {
        int dom = img_dom[slot];
        n_img_handle[dom]      = handle;
        n_img_intrinsic_w[dom] = gfx2d_image_width(handle);
        n_img_intrinsic_h[dom] = gfx2d_image_height(handle);
        /* Apply the same handle to any other DOM nodes with the same
         * src URL (gfx2d image cache stays a single decode). */
        int u_len = img_url_len[slot];
        for (int n = 0; n < nodes_count; n = n + 1) {
            if (n == dom) continue;
            if (n_tag[n] != T_IMG) continue;
            if (n_img_handle[n] >= 0) continue;
            int o = dom_attr_get(n, "src");
            if (o < 0) continue;
            char *s = attr_pool + o;
            int eq = 1;
            for (int k = 0; k < u_len; k = k + 1) {
                if (s[k] != img_url[slot][k]) { eq = 0; break; }
            }
            if (eq && (s[u_len] == 0)) {
                n_img_handle[n] = handle;
                n_img_intrinsic_w[n] = n_img_intrinsic_w[dom];
                n_img_intrinsic_h[n] = n_img_intrinsic_h[dom];
            }
        }
        img_state[slot]  = IMG_S_LOADED;
        img_state_dirty  = 1;
        serial_printf("[browser] <img> load: %s -> handle %d (%dx%d)\n",
                      img_url[slot], handle,
                      n_img_intrinsic_w[dom],
                      n_img_intrinsic_h[dom]);
    } else {
        img_state[slot] = IMG_S_FAILED;
    }
    return 1;
}

void image_pump(void) {
    image_advance_one_pending();
}
