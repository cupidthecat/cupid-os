//help: Download a URL to a file. http:// (plain TCP) or https:// (TLS 1.3).
//help: Usage: wget [-O file] [-q] <url>
//help:   -O file   output filename (default: derived from URL path)
//help:   -q        quiet (suppress progress + error messages)

enum {
    VFS_WRONLY = 1,
    VFS_CREAT  = 256,
    VFS_TRUNC  = 512,
    SOCK_TCP   = 2,
    SOL_TLS    = 1,
    TLS_ENABLE = 1,
    BUFSZ      = 4096,
    REQ_MAX    = 2048,
    HOST_MAX   = 256,
    PATH_MAX_  = 1024
};

int sw(char *s, char *p) {
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
    if (hi == 0) { println("wget: empty host"); return -1; }
    if (url[i] == ':') {
        i = i + 1;
        int v = 0;
        while (url[i] >= '0' && url[i] <= '9') {
            v = v * 10 + (url[i] - '0');
            i = i + 1;
        }
        if (v <= 0 || v > 65535) { println("wget: bad port"); return -1; }
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

/* Derive default filename from URL path. Last segment of path, or
 * "index.html" if the path ends with / or has no segments. Stops at
 * any '?' so we don't include query strings in the filename. */
void derive_filename(char *path, char *out, int max) {
    int last_slash = -1;
    int i = 0;
    int end = 0;
    while (path[i]) {
        if (path[i] == '?') break;
        if (path[i] == '/') last_slash = i;
        end = end + 1;
        i = i + 1;
    }
    if (last_slash < 0 || last_slash == end - 1) {
        cstrcpy(out, "index.html", max);
        return;
    }
    int j = last_slash + 1;
    int o = 0;
    while (j < end && o < max - 1) {
        out[o] = path[j];
        j = j + 1;
        o = o + 1;
    }
    out[o] = 0;
}

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

int append(char *dst, int p, char *src) {
    int i = 0;
    while (src[i]) { dst[p] = src[i]; p = p + 1; i = i + 1; }
    return p;
}

int build_request(char *buf, char *path, char *host) {
    int p = 0;
    p = append(buf, p, "GET ");
    p = append(buf, p, path);
    p = append(buf, p, " HTTP/1.0\r\nHost: ");
    p = append(buf, p, host);
    p = append(buf, p, "\r\nUser-Agent: cupidos-wget/1.0\r\nConnection: close\r\n\r\n");
    return p;
}

void main() {
    char *raw = (char*)get_args();
    if (!raw || raw[0] == 0) {
        println("usage: wget [-O file] [-q] <url>");
        return;
    }

    char url[1024]; url[0] = 0;
    char outname[256]; outname[0] = 0;
    int  quiet = 0;

    char tok[1024];
    int  pos = 0;
    while (1) {
        pos = next_token(raw, pos, tok, 1024);
        if (pos < 0) break;
        if (streq(tok, "-O")) {
            pos = next_token(raw, pos, outname, 256);
            if (pos < 0) { println("wget: -O needs file"); return; }
        } else if (streq(tok, "-q")) {
            quiet = 1;
        } else if (tok[0] == '-') {
            print("wget: unknown option ");
            println(tok);
            return;
        } else {
            cstrcpy(url, tok, 1024);
        }
    }

    if (url[0] == 0) { println("wget: no URL given"); return; }

    char host[256];
    int  port = 80;
    char path[1024];
    int  is_https = 0;
    if (parse_url(url, host, &port, path, &is_https) != 0) return;

    if (outname[0] == 0) derive_filename(path, outname, 256);

    if (!quiet) {
        print("wget: ");
        print(host);
        print(" -> ");
        println(outname);
    }

    U32 ip = 0;
    if (dns_resolve(host, &ip) != 0) {
        if (!quiet) {
            print("wget: could not resolve ");
            println(host);
        }
        return;
    }

    int fd = socket(SOCK_TCP);
    if (fd < 0) {
        if (!quiet) println("wget: socket() failed");
        return;
    }
    if (connect(fd, ip, htons(port)) != 0) {
        if (!quiet) {
            print("wget: connect failed to ");
            println(host);
        }
        close(fd);
        return;
    }

    if (is_https) {
        int hlen = 0;
        while (host[hlen]) hlen = hlen + 1;
        int sr = setsockopt(fd, SOL_TLS, TLS_ENABLE, host, hlen + 1);
        if (sr != 0) {
            if (!quiet) {
                print("wget: TLS handshake failed for ");
                print(host);
                print(" (rc=");
                print_int(sr);
                println(")");
            }
            close(fd);
            return;
        }
    }

    char req[2048];
    int  rlen = build_request(req, path, host);
    if (send(fd, req, rlen) < 0) {
        if (!quiet) println("wget: send failed");
        close(fd);
        return;
    }

    char rpath[256];
    resolve_path(outname, rpath);
    int out_fd = vfs_open(rpath, VFS_WRONLY + VFS_CREAT + VFS_TRUNC);
    if (out_fd < 0) {
        if (!quiet) {
            print("wget: cannot create ");
            println(outname);
        }
        close(fd);
        return;
    }

    char buf[4096];
    int hdr_state  = 0;       /* 0..4 for "\r\n\r\n" */
    int in_body    = 0;
    int body_len   = 0;
    int status     = 0;
    /* Status-line state: 0=skipping "HTTP/x.y", 1=reading digits, 2=done. */
    int sl_state   = 0;
    int sl_digits  = 0;
    while (1) {
        int n = recv(fd, buf, 4096);
        if (n <= 0) break;
        int j = 0;
        if (!in_body) {
            while (j < n) {
                char b = buf[j];
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
                if (b == '\r' && (hdr_state == 0 || hdr_state == 2)) hdr_state = hdr_state + 1;
                else if (b == '\n' && (hdr_state == 1 || hdr_state == 3)) hdr_state = hdr_state + 1;
                else hdr_state = 0;
                j = j + 1;
                if (hdr_state == 4) { in_body = 1; break; }
            }
        }
        if (in_body && j < n) {
            int blen = n - j;
            vfs_write(out_fd, buf + j, blen);
            body_len = body_len + blen;
        }
    }

    vfs_close(out_fd);
    close(fd);

    if (!quiet) {
        print("wget: HTTP ");
        if (status > 0) print_int(status);
        else            print("???");
        print(", ");
        print_int(body_len);
        print(" bytes saved to ");
        println(outname);
    }
}
