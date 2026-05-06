/* ---------- HTTP fetch ---------- */

int build_request(char *buf, char *method, char *p, char *h) {
    int q = 0;
    q = b_append(buf, q, method);
    buf[q] = ' '; q = q + 1;
    q = b_append(buf, q, p);
    q = b_append(buf, q, " HTTP/1.0\r\nHost: ");
    q = b_append(buf, q, h);
    q = b_append(buf, q, "\r\nUser-Agent: cupidos-browser/1.0\r\n");
    q = b_append(buf, q, "Accept: text/html,*/*\r\nConnection: close\r\n\r\n");
    return q;
}

/* Fetch into page_buf. Sets page_len. Returns 0 on success, -1 on fail. */
int fetch_url(char *url, char *content_type_out) {
    int redirects = 0;
    page_len = 0;
    content_type_out[0] = 0;
    char work_url[1024];
    b_strcpy_n(work_url, url, URL_MAX);

    while (1) {
        int rc = parse_url(work_url, cur_host, &cur_port, cur_path, &cur_is_https);
        if (rc != 0) {
            b_strcpy_n(status_msg, "bad URL", 256);
            return -1;
        }

        U32 ip = 0;
        if (dns_resolve(cur_host, &ip) != 0) {
            b_strcpy_n(status_msg, "DNS lookup failed: ", 256);
            int sl = b_strlen(status_msg);
            b_strcpy_n(status_msg + sl, cur_host, 256 - sl);
            return -1;
        }

        int fd = socket(SOCK_TCP);
        if (fd < 0) {
            b_strcpy_n(status_msg, "socket() failed", 256);
            return -1;
        }
        if (connect(fd, ip, htons(cur_port)) != 0) {
            b_strcpy_n(status_msg, "connect failed", 256);
            close(fd);
            return -1;
        }
        if (cur_is_https) {
            int hl = 0;
            while (cur_host[hl]) hl = hl + 1;
            if (setsockopt(fd, SOL_TLS, TLS_ENABLE, cur_host, hl + 1) != 0) {
                b_strcpy_n(status_msg, "TLS handshake failed", 256);
                close(fd);
                return -1;
            }
        }

        char req[4096];
        int rlen = build_request(req, "GET", cur_path, cur_host);
        if (send(fd, req, rlen) < 0) {
            b_strcpy_n(status_msg, "send failed", 256);
            close(fd);
            return -1;
        }

        char buf[4096];
        int hdr_state = 0;
        int in_body = 0;
        int status = 0;
        int sl_state = 0;
        int sl_digits = 0;
        char location[1024]; location[0] = 0;
        int loc_len = 0;
        char line[1024]; int line_len = 0;
        page_len = 0;

        while (1) {
            int n = recv(fd, buf, RECV_BUF_SIZE);
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

                    if (b == '\n') {
                        if (line_len > 9 && b_strieq_n(line, "location:", 9)) {
                            int li = 9;
                            while (li < line_len &&
                                   (line[li] == ' ' || line[li] == '\t')) li = li + 1;
                            int lo = 0;
                            while (li < line_len && line[li] != '\r' &&
                                   lo < URL_MAX - 1) {
                                location[lo] = line[li];
                                lo = lo + 1; li = li + 1;
                            }
                            location[lo] = 0;
                            loc_len = lo;
                        } else if (line_len > 13 &&
                                   b_strieq_n(line, "content-type:", 13)) {
                            int li = 13;
                            while (li < line_len &&
                                   (line[li] == ' ' || line[li] == '\t')) li = li + 1;
                            int lo = 0;
                            while (li < line_len && line[li] != '\r' &&
                                   line[li] != ';' && lo < 127) {
                                content_type_out[lo] = line[li];
                                lo = lo + 1; li = li + 1;
                            }
                            content_type_out[lo] = 0;
                        }
                        line_len = 0;
                    } else if (b != '\r' && line_len < HEADER_LINE_MAX - 1) {
                        line[line_len] = b; line_len = line_len + 1;
                    }

                    if (b == '\r' && (hdr_state == 0 || hdr_state == 2)) hdr_state = hdr_state + 1;
                    else if (b == '\n' && (hdr_state == 1 || hdr_state == 3)) hdr_state = hdr_state + 1;
                    else hdr_state = 0;
                    j = j + 1;
                    if (hdr_state == 4) { in_body = 1; break; }
                }
            }
            int skip_body = (status >= 300 && status < 400 &&
                             loc_len > 0 && redirects < 5);
            if (in_body && j < n && !skip_body) {
                int blen = n - j;
                int can = PAGE_BUF_SIZE - 1 - page_len;
                if (blen > can) blen = can;
                if (blen > 0) {
                    int k = 0;
                    while (k < blen) {
                        page_buf[page_len + k] = buf[j + k];
                        k = k + 1;
                    }
                    page_len = page_len + blen;
                }
            }
        }
        close(fd);
        page_buf[page_len] = 0;

        if (status >= 300 && status < 400 && loc_len > 0 && redirects < 5) {
            char new_url[1024];
            if (resolve_redirect(location, cur_host, cur_port, cur_is_https,
                                 new_url, URL_MAX) != 0) {
                b_strcpy_n(status_msg, "bad Location header", 256);
                return -1;
            }
            b_strcpy_n(work_url, new_url, URL_MAX);
            redirects = redirects + 1;
            continue;
        }

        if (status < 200 || status >= 300) {
            b_strcpy_n(status_msg, "HTTP error: ", 256);
            int sl = b_strlen(status_msg);
            b_append_int(status_msg, sl, status);
            status_msg[sl + 4] = 0;
            return -1;
        }
        b_strcpy_n(cur_url, work_url, URL_MAX);
        return 0;
    }
    return -1;
}
