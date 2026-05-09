/* Navigation */

void compute_url_relative(char *rel, char *out, int max) {
    if (b_strieq_n(rel, "http://", 7) || b_strieq_n(rel, "https://", 8)) {
        b_strcpy_n(out, rel, max);
        return;
    }
    /* Build prefix: scheme://host[:port] */
    int p = 0;
    char *prefix = cur_is_https ? "https://" : "http://";
    int i = 0;
    while (prefix[i] && p < max - 1) { out[p] = prefix[i]; p = p + 1; i = i + 1; }
    i = 0;
    while (cur_host[i] && p < max - 1) { out[p] = cur_host[i]; p = p + 1; i = i + 1; }
    int default_port = cur_is_https ? 443 : 80;
    if (cur_port != default_port && p < max - 7) {
        out[p] = ':'; p = p + 1;
        char num[8]; int n = 0; int v = cur_port;
        while (v > 0) { num[n] = '0' + (v % 10); n = n + 1; v = v / 10; }
        while (n > 0 && p < max - 1) { n = n - 1; out[p] = num[n]; p = p + 1; }
    }
    if (rel[0] == '/') {
        i = 0;
        while (rel[i] && p < max - 1) { out[p] = rel[i]; p = p + 1; i = i + 1; }
        out[p] = 0;
        return;
    }
    /* relative: append cur_path's directory + rel */
    int last_slash = -1;
    i = 0;
    while (cur_path[i]) {
        if (cur_path[i] == '/') last_slash = i;
        i = i + 1;
    }
    int j = 0;
    while (j <= last_slash && p < max - 1) {
        out[p] = cur_path[j]; p = p + 1; j = j + 1;
    }
    i = 0;
    while (rel[i] && p < max - 1) { out[p] = rel[i]; p = p + 1; i = i + 1; }
    out[p] = 0;
}

/* §2.x External stylesheet loader. After the tokenizer and tree builder
 * complete (so all text is interned in attr_pool and page_buf is free
 * for reuse), walk the DOM for `<link rel="stylesheet" href="...">` and
 * fetch each, feeding the body into css_parse_block.
 *
 * Cascade caveat: external rules are always appended AFTER inline
 * <style> rules emitted during tree-build, so a `<style>` preceding a
 * `<link>` in the source will out-rank it on doc-order ties. Real-world
 * pages put link first; document the limitation and move on. Reference:
 * Blink core/css/StyleEngine.cpp::createSheet.
 *
 * Net globals (cur_host/cur_port/cur_path/cur_is_https/cur_url/page_buf/
 * page_len) are saved on entry and restored on exit so the outer
 * navigate() resumes against the original document URL. */
void fetch_external_stylesheets(void) {
    int link_count = 0;
    /* Pre-scan: count and short-circuit if no candidates so the save/restore
     * dance and serial spam stay off the hot path. */
    for (int n = 0; n < nodes_count; n = n + 1) {
        if (n_tag[n] != T_LINK) continue;
        char *rel = dom_attr_str(n, "rel");
        if (!rel || !b_strieq(rel, "stylesheet")) continue;
        char *href = dom_attr_str(n, "href");
        if (!href || href[0] == 0) continue;
        link_count = link_count + 1;
    }
    if (link_count == 0) return;
    if (link_count > 16) link_count = 16;     /* PENDING_LINK_MAX */

    /* Save net state so fetch_url's clobber of cur_* + page_buf is reversible. */
    char saved_host[256];
    char saved_path[1024];
    char saved_url [1024];
    int  saved_port    = cur_port;
    int  saved_https   = cur_is_https;
    int  saved_page_len = page_len;
    b_strcpy_n(saved_host, cur_host, 256);
    b_strcpy_n(saved_path, cur_path, 1024);
    b_strcpy_n(saved_url,  cur_url,  1024);

    int parsed = 0;
    for (int n = 0; n < nodes_count && parsed < 16; n = n + 1) {
        if (n_tag[n] != T_LINK) continue;
        char *rel = dom_attr_str(n, "rel");
        if (!rel || !b_strieq(rel, "stylesheet")) continue;
        char *href = dom_attr_str(n, "href");
        if (!href || href[0] == 0) continue;

        char absu[1024];
        compute_url_relative(href, absu, URL_MAX);

        char ct[128]; ct[0] = 0;
        if (fetch_url(absu, ct) != 0) {
            serial_printf("[browser] link skip (fetch failed): %s\n", absu);
            continue;
        }
        serial_printf("[browser] link parsed: %s (%d bytes)\n", absu, page_len);
        css_parse_block(page_buf, page_len);
        parsed = parsed + 1;
    }

    /* Restore. page_buf contents are no longer needed by anything (text was
     * interned during tree-build), so we don't bother to repopulate it -
     * only the metadata globals matter. */
    b_strcpy_n(cur_host, saved_host, 256);
    b_strcpy_n(cur_path, saved_path, 1024);
    b_strcpy_n(cur_url,  saved_url,  1024);
    cur_port     = saved_port;
    cur_is_https = saved_https;
    page_len     = saved_page_len;
}

void about_dump() {
    /* Stream the current render tree and per-node computed-style summary
     * to the serial port. Useful for `about:dump` and the Ctrl-D shortcut.
     * The output appears in the host's serial log; the visible page is
     * not affected. */
    serial_printf("[browser] === about:dump ===\n");
    serial_printf("[browser] %d DOM nodes, %d RT nodes, %d CSS rules\n",
                  nodes_count, rt_count, css_rule_count);
    if (rt_count > 0) dump_rt(0, 0);
    for (int n = 0; n < nodes_count; n = n + 1) dump_style(n);
    serial_printf("[browser] === end about:dump ===\n");
}

void navigate(char *u) {
    /* about:dump - dump the previous page's render tree and per-node
     * computed style to serial; do not actually fetch anything. The
     * status bar reports completion. */
    if (b_strieq_n(u, "about:dump", 10)) {
        about_dump();
        b_strcpy_n(status_msg, "about:dump - serial log written", 256);
        return;
    }
    b_strcpy_n(status_msg, "Loading: ", 256);
    int sl = b_strlen(status_msg);
    b_strcpy_n(status_msg + sl, u, 256 - sl);
    render();

    char ct[128]; ct[0] = 0;
    if (fetch_url(u, ct) != 0) {
        nodes_count = 0;
        doc_h = 0;
        scroll_y = 0;
        return;
    }
    /* push history. Back/forward set nav_no_push so they don't grow
     * the trail when revisiting old entries.  A normal navigation
     * truncates any forward history past hist_pos before adding the
     * new entry — same as Chrome / Firefox behaviour. */
    if (!nav_no_push) {
        if (hist_pos < hist_count) hist_count = hist_pos;
        if (hist_count < HIST_MAX) {
            b_strcpy_n(hist_url_pool + hist_count * URL_MAX, cur_url, URL_MAX);
            hist_count = hist_count + 1;
        } else {
            int k = 0;
            while (k < HIST_MAX - 1) {
                int j = 0;
                while (j < URL_MAX) {
                    hist_url_pool[k * URL_MAX + j] = hist_url_pool[(k + 1) * URL_MAX + j];
                    j = j + 1;
                }
                k = k + 1;
            }
            b_strcpy_n(hist_url_pool + (HIST_MAX - 1) * URL_MAX, cur_url, URL_MAX);
        }
        hist_pos = hist_count;
    }
    nav_no_push = 0;
    /* Reset persistent string pool BEFORE parse_html runs. The tree
     * builder used to do this internally, but now tokenize() interns into
     * attr_pool, so we must reset before tokenize starts. */
    attr_pool_pos = 1;
    js_reset_per_page();
    /* §2.x Per-page webfont registry reset. Faces stay registered in
     * fontsys (no fontsys_unregister exists yet), but the browser-side
     * mapping from family -> face_id is rebuilt for each page so a stale
     * @font-face rule can't bleed into the next document. */
    font_face_init();
    parse_html(page_len);
    run_layout();           /* render-tree layout drives the visible pipeline */
    scroll_y = 0;
    if (title_buf[0]) b_strcpy_n(status_msg, title_buf, 256);
    else              b_strcpy_n(status_msg, cur_url, 256);
    /* update address bar */
    b_strcpy_n(addr_buf, cur_url, URL_MAX);
    addr_len = b_strlen(addr_buf);
    addr_cursor = addr_len;
}

void go_back() {
    if (hist_pos <= 1) return;
    hist_pos = hist_pos - 1;
    char prev[1024];
    b_strcpy_n(prev, hist_url_pool + (hist_pos - 1) * URL_MAX, URL_MAX);
    nav_no_push = 1;
    navigate(prev);
}

void go_forward() {
    if (hist_pos >= hist_count) return;
    hist_pos = hist_pos + 1;
    char nxt[1024];
    b_strcpy_n(nxt, hist_url_pool + (hist_pos - 1) * URL_MAX, URL_MAX);
    nav_no_push = 1;
    navigate(nxt);
}

/* GET-only form submit (per spec §10).
 *
 * Takes a DOM node index pointing at the <form> element. Walks the form's
 * descendant subtree via n_first_child/n_next, collects every <input> with
 * a name attribute, URL-encodes name=value pairs, and navigates to
 * action?query (action defaults to the current URL when absent). */
void submit_form(int form_node_idx) {
    if (form_node_idx < 0 || form_node_idx >= nodes_count) return;
    if (n_tag[form_node_idx] != T_FORM) return;

    char *action = dom_attr_str(form_node_idx, "action");

    /* Build query string by depth-first walk of the form subtree. */
    char query[1024];
    int qlen = 0;
    int first_pair = 1;
    int stack[64];
    int sp = 0;
    int kid = n_first_child[form_node_idx];
    while (kid >= 0 && sp < 64) { stack[sp] = kid; sp = sp + 1; kid = n_next[kid]; }

    while (sp > 0) {
        sp = sp - 1;
        int node = stack[sp];

        if (n_tag[node] == T_INPUT) {
            char *name = dom_attr_str(node, "name");
            /* Locate this input's runtime entry to read its current value. */
            int ii = -1;
            for (int k = 0; k < inputs_count; k++) {
                if (input_node[k] == node) { ii = k; break; }
            }
            char *value;
            if (ii >= 0) {
                value = input_value + ii * 128;
            } else {
                char *dv = dom_attr_str(node, "value");
                value = dv ? dv : "";
            }
            /* Skip submit/image inputs - they are activators, not data. */
            char *typ = dom_attr_str(node, "type");
            int is_submit = (typ != 0 &&
                             (b_strieq(typ, "submit") || b_strieq(typ, "image")));

            if (name != 0 && !is_submit && qlen + 200 < 1024) {
                if (!first_pair) {
                    query[qlen] = '&'; qlen = qlen + 1;
                }
                first_pair = 0;
                int nl = b_strlen(name);
                for (int k = 0; k < nl && qlen < 1023; k++) {
                    query[qlen] = name[k]; qlen = qlen + 1;
                }
                if (qlen < 1023) { query[qlen] = '='; qlen = qlen + 1; }
                int vl = b_strlen(value);
                for (int k = 0; k < vl && qlen < 1023; k++) {
                    char ch = value[k];
                    if (ch == ' ') {
                        query[qlen] = '+'; qlen = qlen + 1;
                    } else if ((ch >= 'a' && ch <= 'z') ||
                               (ch >= 'A' && ch <= 'Z') ||
                               (ch >= '0' && ch <= '9') ||
                               ch == '-' || ch == '_' || ch == '.' || ch == '~') {
                        query[qlen] = ch; qlen = qlen + 1;
                    } else if (qlen + 3 < 1024) {
                        char hex[16];
                        hex[0] = '0'; hex[1] = '1'; hex[2] = '2'; hex[3] = '3';
                        hex[4] = '4'; hex[5] = '5'; hex[6] = '6'; hex[7] = '7';
                        hex[8] = '8'; hex[9] = '9'; hex[10] = 'A'; hex[11] = 'B';
                        hex[12] = 'C'; hex[13] = 'D'; hex[14] = 'E'; hex[15] = 'F';
                        int u = (int)ch & 0xFF;
                        query[qlen] = '%';                       qlen = qlen + 1;
                        query[qlen] = hex[(u >> 4) & 0xF];       qlen = qlen + 1;
                        query[qlen] = hex[u & 0xF];              qlen = qlen + 1;
                    }
                }
            }
        }

        /* Push children of this node so the walk descends. */
        int cc = n_first_child[node];
        while (cc >= 0 && sp < 64) { stack[sp] = cc; sp = sp + 1; cc = n_next[cc]; }
    }
    query[qlen] = 0;

    /* Build target URL: action (default = cur_path) + '?' + query. */
    char target[1024];
    int tp = 0;
    if (action != 0) {
        int al = b_strlen(action);
        for (int k = 0; k < al && tp < URL_MAX - 1; k++) {
            target[tp] = action[k]; tp = tp + 1;
        }
    } else {
        /* Empty action means submit to current document URL. */
        int pl = b_strlen(cur_path);
        for (int k = 0; k < pl && tp < URL_MAX - 1; k++) {
            target[tp] = cur_path[k]; tp = tp + 1;
        }
    }
    if (qlen > 0 && tp < URL_MAX - 1) {
        target[tp] = '?'; tp = tp + 1;
        for (int k = 0; k < qlen && tp < URL_MAX - 1; k++) {
            target[tp] = query[k]; tp = tp + 1;
        }
    }
    target[tp] = 0;

    char absu[1024];
    compute_url_relative(target, absu, URL_MAX);
    navigate(absu);
}
