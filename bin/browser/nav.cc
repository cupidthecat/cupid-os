/* ---------- Navigation ---------- */

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
        boxes_count = 0;
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

void submit_form(int form_node_idx) {
    int form_idx = -1;
    int i = 0;
    /* find form_idx */
    int seen = 0;
    int n = 0;
    while (n < nodes_count) {
        if (n_tag[n] == T_FORM) {
            if (n == form_node_idx) { form_idx = seen; break; }
            seen = seen + 1;
        }
        n = n + 1;
    }
    if (form_idx < 0 || form_idx >= forms_count) return;

    char base_url[1024];
    if (form_action[form_idx] >= 0) {
        char *act = attr_pool + form_action[form_idx];
        compute_url_relative(act, base_url, URL_MAX);
    } else {
        b_strcpy_n(base_url, cur_url, URL_MAX);
    }

    char query[1024]; int qp = 0;
    int has = 0;
    int ii = 0;
    while (ii < inputs_count) {
        if (input_form[ii] == form_idx && input_name_off[ii] >= 0) {
            char *nm = attr_pool + input_name_off[ii];
            char *vl = input_value + ii * 128;
            if (qp < 1023) {
                query[qp] = has ? '&' : '?'; qp = qp + 1;
            }
            int j = 0;
            while (nm[j] && qp < 1023) {
                query[qp] = nm[j]; qp = qp + 1; j = j + 1;
            }
            if (qp < 1023) { query[qp] = '='; qp = qp + 1; }
            j = 0;
            while (vl[j] && qp < 1023) {
                char c = vl[j];
                if (c == ' ') { query[qp] = '+'; qp = qp + 1; }
                else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                         (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                         c == '.' || c == '~') {
                    query[qp] = c; qp = qp + 1;
                } else if (qp + 3 <= 1023) {
                    char hex[16];
                    hex[0] = '0'; hex[1] = '1'; hex[2] = '2'; hex[3] = '3';
                    hex[4] = '4'; hex[5] = '5'; hex[6] = '6'; hex[7] = '7';
                    hex[8] = '8'; hex[9] = '9'; hex[10] = 'A'; hex[11] = 'B';
                    hex[12] = 'C'; hex[13] = 'D'; hex[14] = 'E'; hex[15] = 'F';
                    int u = (int)c & 0xFF;
                    query[qp] = '%'; qp = qp + 1;
                    query[qp] = hex[(u >> 4) & 0xF]; qp = qp + 1;
                    query[qp] = hex[u & 0xF]; qp = qp + 1;
                }
                j = j + 1;
            }
            has = 1;
        }
        ii = ii + 1;
    }
    query[qp] = 0;

    char full[1024];
    int p = 0;
    int j = 0;
    while (base_url[j] && p < URL_MAX - 1 && base_url[j] != '?') {
        full[p] = base_url[j]; p = p + 1; j = j + 1;
    }
    j = 0;
    while (query[j] && p < URL_MAX - 1) {
        full[p] = query[j]; p = p + 1; j = j + 1;
    }
    full[p] = 0;
    navigate(full);
}
