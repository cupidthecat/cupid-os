/* URL parsing */

int parse_url(char *url, char *host, int *port_out, char *path,
              int *is_https_out) {
    int i = 0;
    *is_https_out = 0;
    if (url[0]=='h' && url[1]=='t' && url[2]=='t' && url[3]=='p' &&
        url[4]=='s' && url[5]==':' && url[6]=='/' && url[7]=='/') {
        i = 8; *is_https_out = 1;
    } else if (url[0]=='h' && url[1]=='t' && url[2]=='t' && url[3]=='p' &&
               url[4]==':' && url[5]=='/' && url[6]=='/') {
        i = 7;
    }
    *port_out = (*is_https_out) ? 443 : 80;
    int hi = 0;
    while (url[i] && url[i] != ':' && url[i] != '/' && hi < HOST_MAX - 1) {
        host[hi] = url[i];
        i = i + 1;
        hi = hi + 1;
    }
    host[hi] = 0;
    if (hi == 0) return -1;
    if (url[i] == ':') {
        i = i + 1;
        int v = 0;
        while (url[i] >= '0' && url[i] <= '9') {
            v = v * 10 + (url[i] - '0');
            i = i + 1;
        }
        if (v <= 0 || v > 65535) return -1;
        *port_out = v;
    }
    int pi = 0;
    if (url[i] != '/') { path[pi] = '/'; pi = pi + 1; }
    while (url[i] && pi < PATH_MAX_ - 1) {
        path[pi] = url[i];
        i = i + 1;
        pi = pi + 1;
    }
    path[pi] = 0;
    return 0;
}

int resolve_redirect(char *location, char *cur_h, int cur_p, int cur_is_h,
                     char *out_url, int max) {
    if ((b_strieq_n(location, "http://", 7)) ||
        (b_strieq_n(location, "https://", 8))) {
        b_strcpy_n(out_url, location, max);
        return 0;
    }
    if (location[0] == '/') {
        int p = 0;
        char *prefix = cur_is_h ? "https://" : "http://";
        int i = 0;
        while (prefix[i] && p < max - 1) { out_url[p] = prefix[i]; p = p + 1; i = i + 1; }
        i = 0;
        while (cur_h[i] && p < max - 1) { out_url[p] = cur_h[i]; p = p + 1; i = i + 1; }
        int default_port = cur_is_h ? 443 : 80;
        if (cur_p != default_port && p < max - 7) {
            out_url[p] = ':'; p = p + 1;
            char num[8];
            int n = 0;
            int v = cur_p;
            while (v > 0) { num[n] = '0' + (v % 10); n = n + 1; v = v / 10; }
            while (n > 0 && p < max - 1) { n = n - 1; out_url[p] = num[n]; p = p + 1; }
        }
        i = 0;
        while (location[i] && p < max - 1) { out_url[p] = location[i]; p = p + 1; i = i + 1; }
        out_url[p] = 0;
        return 0;
    }
    return -1;
}

