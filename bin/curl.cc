//help: HTTP/HTTPS client. http:// uses plain TCP; https:// uses TLS 1.3.
//help: Usage: curl [-o file] [-i] [-s] [-X METHOD] [-d data] [-H header] <url>
//help:   -o file   write body to file (default: stdout)
//help:   -i        include response headers in output
//help:   -s        silent (suppress error messages)
//help:   -X METHOD set request method (default GET; -d implies POST)
//help:   -d DATA   send DATA as request body (sets method=POST)
//help:   -H "Hdr: V"  add a header line. Quote with " for spaces.

enum {
    VFS_WRONLY = 1,
    VFS_CREAT  = 256,
    VFS_TRUNC  = 512,
    SOCK_TCP   = 2,
    SOL_TLS    = 1,
    TLS_ENABLE = 1,
    BUFSZ      = 4096,
    REQ_MAX    = 4096,
    HOST_MAX   = 256,
    PATH_MAX_  = 1024,
    HDR_MAX    = 512
};

int sw(char *s, char *p) {              /* prefix match */
    int i = 0;
    while (p[i]) {
        if (s[i] != p[i]) return 0;
        i = i + 1;
    }
    return 1;
}

int streq(char *a, char *b) {
    int i = 0;
    while (a[i] && a[i] == b[i]) i = i + 1;
    return a[i] == b[i];
}

void cstrcpy(char *d, char *s, int max) {
    int i = 0;
    while (s[i] && i < max - 1) { d[i] = s[i]; i = i + 1; }
    d[i] = 0;
}

/* Returns: 0 on success, -1 on bad URL.
 * Sets *is_https_out to 1 for https:// (default port 443) or 0 for http://.*/
int parse_url(char *url, char *host, int *port_out, char *path,
              int *is_https_out) {
    int i = 0;
    *is_https_out = 0;
    if (sw(url, "http://"))  i = 7;
    else if (sw(url, "https://")) { i = 8; *is_https_out = 1; }
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

/* Lowercase ASCII helper. */
int lc(int c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

/* Case-insensitive prefix match. */
int sw_ci(char *s, char *p) {
    int i = 0;
    while (p[i]) {
        if (lc(s[i]) != lc(p[i])) return 0;
        i = i + 1;
    }
    return 1;
}

/* Resolve a Location header value against the current host.
 * Handles absolute URLs (http://, https://) and absolute paths (/foo)
 * by reusing the current host + port + scheme.
 * Returns 0 on success, -1 on parse error.*/
int resolve_redirect(char *location, char *cur_host, int cur_port,
                     int cur_is_https, char *out_url, int max) {
    if (sw_ci(location, "http://") || sw_ci(location, "https://")) {
        cstrcpy(out_url, location, max);
        return 0;
    }
    if (location[0] == '/') {
        /* Reuse the current scheme. */
        int p = 0;
        char *prefix = cur_is_https ? "https://" : "http://";
        int i = 0;
        while (prefix[i] && p < max - 1) { out_url[p++] = prefix[i++]; }
        i = 0;
        while (cur_host[i] && p < max - 1) { out_url[p++] = cur_host[i++]; }
        int default_port = cur_is_https ? 443 : 80;
        if (cur_port != default_port && p < max - 7) {
            out_url[p++] = ':';
            char num[8];
            int n = 0;
            int v = cur_port;
            while (v > 0) { num[n++] = '0' + (v % 10); v = v / 10; }
            while (n > 0 && p < max - 1) out_url[p++] = num[--n];
        }
        i = 0;
        while (location[i] && p < max - 1) { out_url[p++] = location[i++]; }
        out_url[p] = 0;
        return 0;
    }
    return -1;
}

int append(char *dst, int p, char *src) {
    int i = 0;
    while (src[i]) { dst[p] = src[i]; p = p + 1; i = i + 1; }
    return p;
}

int append_int(char *dst, int p, int v) {
    char buf[16];
    int n = 0;
    if (v == 0) { dst[p] = '0'; return p + 1; }
    if (v < 0)  { dst[p] = '-'; p = p + 1; v = -v; }
    while (v > 0) { buf[n] = '0' + (v % 10); n = n + 1; v = v / 10; }
    while (n > 0) { n = n - 1; dst[p] = buf[n]; p = p + 1; }
    return p;
}

int build_request(char *buf, char *method, char *path, char *host,
                  char *extra_hdr, char *body) {
    int p = 0;
    int blen = 0;
    p = append(buf, p, method);
    buf[p] = ' '; p = p + 1;
    p = append(buf, p, path);
    p = append(buf, p, " HTTP/1.0\r\nHost: ");
    p = append(buf, p, host);
    p = append(buf, p, "\r\nUser-Agent: cupidos-curl/1.0\r\nConnection: close\r\n");
    if (extra_hdr && extra_hdr[0]) {
        p = append(buf, p, extra_hdr);
        buf[p] = '\r'; p = p + 1;
        buf[p] = '\n'; p = p + 1;
    }
    if (body && body[0]) {
        while (body[blen]) blen = blen + 1;
        p = append(buf, p, "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: ");
        p = append_int(buf, p, blen);
        buf[p] = '\r'; p = p + 1;
        buf[p] = '\n'; p = p + 1;
    }
    buf[p] = '\r'; p = p + 1;
    buf[p] = '\n'; p = p + 1;
    if (body && body[0]) {
        p = append(buf, p, body);
    }
    return p;
}

/* Tokeniser that respects "quoted strings" (no escape sequences).
 * Returns next position after the token, or -1 if no token. Sets out.*/
int next_token(char *s, int pos, char *out, int max) {
    int i = pos;
    while (s[i] == ' ' || s[i] == '\t') i = i + 1;
    if (s[i] == 0) return -1;
    int j = 0;
    if (s[i] == '"') {
        i = i + 1;
        while (s[i] && s[i] != '"' && j < max - 1) {
            out[j] = s[i]; i = i + 1; j = j + 1;
        }
        if (s[i] == '"') i = i + 1;
    } else {
        while (s[i] && s[i] != ' ' && s[i] != '\t' && j < max - 1) {
            out[j] = s[i]; i = i + 1; j = j + 1;
        }
    }
    out[j] = 0;
    return i;
}

void main() {
    char *raw = (char*)get_args();
    if (!raw || raw[0] == 0) {
        println("usage: curl [-o file] [-i] [-s] [-X METHOD] [-d data]");
        println("            [-H header] <url>");
        return;
    }

    char url[1024];   url[0] = 0;
    char outfile[256]; outfile[0] = 0;
    char method[16];  cstrcpy(method, "GET", 16);
    char post_data[1024]; post_data[0] = 0;
    char extra_hdr[512]; extra_hdr[0] = 0;
    int  show_headers = 0;
    int  silent = 0;

    char tok[1024];
    char val[1024];
    int  pos = 0;
    while (1) {
        pos = next_token(raw, pos, tok, 1024);
        if (pos < 0) break;
        if (streq(tok, "-o")) {
            pos = next_token(raw, pos, outfile, 256);
            if (pos < 0) { println("curl: -o needs file"); return; }
        } else if (streq(tok, "-i")) {
            show_headers = 1;
        } else if (streq(tok, "-s")) {
            silent = 1;
        } else if (streq(tok, "-X")) {
            pos = next_token(raw, pos, method, 16);
            if (pos < 0) { println("curl: -X needs method"); return; }
        } else if (streq(tok, "-d")) {
            pos = next_token(raw, pos, post_data, 1024);
            if (pos < 0) { println("curl: -d needs data"); return; }
            cstrcpy(method, "POST", 16);
        } else if (streq(tok, "-H")) {
            pos = next_token(raw, pos, val, HDR_MAX);
            if (pos < 0) { println("curl: -H needs header"); return; }
            cstrcpy(extra_hdr, val, HDR_MAX);
        } else if (tok[0] == '-') {
            print("curl: unknown option ");
            println(tok);
            return;
        } else {
            cstrcpy(url, tok, 1024);
        }
    }

    if (url[0] == 0) { println("curl: no URL given"); return; }

    char host[256];
    int  port = 80;
    char path[1024];
    int  out_fd = -1;
    if (outfile[0]) {
        char rpath[256];
        resolve_path(outfile, rpath);
        out_fd = vfs_open(rpath, VFS_WRONLY + VFS_CREAT + VFS_TRUNC);
        if (out_fd < 0) {
            if (!silent) {
                print("curl: cannot open ");
                println(outfile);
            }
            return;
        }
    }

    int redirects = 0;
    int is_https = 0;
    while (1) {
        int rc = parse_url(url, host, &port, path, &is_https);
        if (rc != 0) {
            if (!silent) println("curl: bad URL");
            if (out_fd >= 0) vfs_close(out_fd);
            return;
        }

        U32 ip = 0;
        if (dns_resolve(host, &ip) != 0) {
            if (!silent) {
                print("curl: could not resolve host: ");
                println(host);
            }
            if (out_fd >= 0) vfs_close(out_fd);
            return;
        }

        int fd = socket(SOCK_TCP);
        if (fd < 0) {
            if (!silent) println("curl: socket() failed");
            if (out_fd >= 0) vfs_close(out_fd);
            return;
        }
        if (connect(fd, ip, htons(port)) != 0) {
            if (!silent) {
                print("curl: connect failed to ");
                println(host);
            }
            close(fd);
            if (out_fd >= 0) vfs_close(out_fd);
            return;
        }

        if (is_https) {
            int hlen = 0;
            while (host[hlen]) hlen = hlen + 1;
            int sr = setsockopt(fd, SOL_TLS, TLS_ENABLE, host, hlen + 1);
            if (sr != 0) {
                if (!silent) {
                    print("curl: TLS handshake failed for ");
                    print(host);
                    print(" (rc=");
                    print_int(sr);
                    println(")");
                }
                close(fd);
                if (out_fd >= 0) vfs_close(out_fd);
                return;
            }
        }

        char req[4096];
        int  rlen = build_request(req, method, path, host, extra_hdr, post_data);
        if (send(fd, req, rlen) < 0) {
            if (!silent) println("curl: send failed");
            close(fd);
            if (out_fd >= 0) vfs_close(out_fd);
            return;
        }

        char buf[4096];
        int hdr_state = 0;
        int in_body   = 0;
        int body_len  = 0;
        int status    = 0;
        int sl_state  = 0;          /* 0=skip "HTTP/x.y", 1=digits, 2=done */
        int sl_digits = 0;
        char location[1024]; location[0] = 0;
        int loc_len = 0;
        /* Per-line header capture: buffer the current header line so we
         * can match "Location:" case-insensitively at line end.*/
        char line[1024]; int line_len = 0;

        while (1) {
            int n = recv(fd, buf, BUFSZ);
            if (n <= 0) break;
            int j = 0;
            if (!in_body) {
                while (j < n) {
                    char b = buf[j];
                    if (show_headers) putchar(b);

                    /* Status code from first line. */
                    if (sl_state == 0) {
                        if (b == ' ') sl_state = 1;
                    } else if (sl_state == 1) {
                        if (b >= '0' && b <= '9') {
                            sl_digits = sl_digits * 10 + (b - '0');
                        } else {
                            status = sl_digits;
                            sl_state = 2;
                        }
                    }

                    /* Location header capture. */
                    if (b == '\n') {
                        /* End of header line - check Location: */
                        if (line_len > 9 && sw_ci(line, "location:")) {
                            int li = 9;
                            while (li < line_len && (line[li] == ' ' || line[li] == '\t')) li = li + 1;
                            int lo = 0;
                            while (li < line_len && line[li] != '\r' && lo < 1023) {
                                location[lo++] = line[li++];
                            }
                            location[lo] = 0;
                            loc_len = lo;
                        }
                        line_len = 0;
                    } else if (b != '\r' && line_len < 1023) {
                        line[line_len++] = b;
                    }

                    if (b == '\r' && (hdr_state == 0 || hdr_state == 2)) hdr_state = hdr_state + 1;
                    else if (b == '\n' && (hdr_state == 1 || hdr_state == 3)) hdr_state = hdr_state + 1;
                    else hdr_state = 0;
                    j = j + 1;
                    if (hdr_state == 4) { in_body = 1; break; }
                }
            }
            /* Skip body entirely if we're going to follow a redirect. */
            int skip_body = (status >= 300 && status < 400 && loc_len > 0
                             && redirects < 5);
            if (in_body && j < n && !skip_body) {
                int blen = n - j;
                if (out_fd >= 0) {
                    vfs_write(out_fd, buf + j, blen);
                } else {
                    int k = 0;
                    while (k < blen) { putchar(buf[j + k]); k = k + 1; }
                }
                body_len = body_len + blen;
            }
        }
        close(fd);

        /* Follow redirect? */
        if (status >= 300 && status < 400 && loc_len > 0 && redirects < 5) {
            char new_url[1024];
            int rr = resolve_redirect(location, host, port, is_https,
                                      new_url, 1024);
            if (rr != 0) {
                if (!silent) {
                    print("curl: bad Location header: ");
                    println(location);
                }
                if (out_fd >= 0) vfs_close(out_fd);
                return;
            }
            if (!silent && show_headers) {
                print("curl: following redirect ");
                print_int(status);
                print(" -> ");
                println(new_url);
            }
            cstrcpy(url, new_url, 1024);
            redirects = redirects + 1;
            /* Force GET on redirect (matches browser behaviour for 301/302). */
            cstrcpy(method, "GET", 16);
            post_data[0] = 0;
            continue;
        }

        if (out_fd >= 0) {
            vfs_close(out_fd);
            if (!silent) {
                print("curl: wrote ");
                print_int(body_len);
                print(" bytes to ");
                println(outfile);
            }
        }
        return;
    }
}
