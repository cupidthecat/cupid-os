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

void navigate(char *u) {
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
    /* push history */
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
    /* Reset persistent string pool BEFORE parse_html runs. The tree
     * builder used to do this internally, but now tokenize() interns into
     * attr_pool, so we must reset before tokenize starts. */
    attr_pool_pos = 1;
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
    if (hist_count <= 1) return;
    hist_count = hist_count - 1;
    char prev[1024];
    b_strcpy_n(prev, hist_url_pool + (hist_count - 1) * URL_MAX, URL_MAX);
    /* re-fetch (don't double-push) */
    hist_count = hist_count - 1;
    navigate(prev);
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
