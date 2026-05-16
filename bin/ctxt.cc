#pragma once

int CTXT_BODY;
int CTXT_H1;
int CTXT_H2;
int CTXT_H3;
int CTXT_RULE;
int CTXT_CENTER;
int CTXT_COMMENT;
int CTXT_BUTTON;
int CTXT_CODE_HEADER;
int CTXT_CODE;
int CTXT_SPRITE;
int CTXT_TREE;

int CTXT_ACT_OPEN;
int CTXT_ACT_SHELL;
int CTXT_ACT_REPL;
int CTXT_ACT_TREE;
int CTXT_ACT_WEB;

int CTXT_MAX_LINES;
int CTXT_MAX_LINKS;
int CTXT_MAX_TEXT;
int CTXT_WEB_BUF_SIZE;
int CTXT_AUTO_FETCH_REMOTE_IMAGES;
int CTXT_REMOTE_WORKER_STARTED;

int ctxt_line_count;
int ctxt_line_type[1024];
char ctxt_line_text[1024][128];
int ctxt_line_color[1024];
int ctxt_line_bg_color[1024];
int ctxt_line_ref[1024];
int ctxt_line_aux_a[1024];
int ctxt_line_aux_b[1024];
int ctxt_line_tree_mask[1024];

int ctxt_link_count;
char ctxt_link_target[192][512];
int ctxt_link_action[192];
int ctxt_link_ref[192];
int ctxt_link_x[192];
int ctxt_link_y[192];
int ctxt_link_w[192];
int ctxt_link_h[192];

int ctxt_action_count;
int ctxt_action_type[128];
char ctxt_action_payload[128][512];

int ctxt_sprite_count;
char ctxt_sprite_path[64][512];
int ctxt_sprite_w[64];
int ctxt_sprite_h[64];
int ctxt_sprite_action[64];
int ctxt_sprite_remote[64];
int ctxt_sprite_state[64];
int ctxt_sprite_handle[64];
int ctxt_sprite_explicit_size[64];

int ctxt_tree_count;
int ctxt_tree_open[32];

int ctxt_theme_light;
int ctxt_total_h;
int ctxt_total_w;

int ctxt_col_bg;
int ctxt_col_h1;
int ctxt_col_h2;
int ctxt_col_h3;
int ctxt_col_body;
int ctxt_col_rule;
int ctxt_col_box_bg;
int ctxt_col_box_text;
int ctxt_col_link;
int ctxt_col_link_hover;
int ctxt_col_code_bg;
int ctxt_col_code_text;

int ctxt_strlen(char *s) {
  int n = 0;
  while (s[n]) n = n + 1;
  return n;
}

void ctxt_strcpy_n(char *dst, char *src, int max) {
  int i = 0;
  while (src[i] && i < max - 1) {
    dst[i] = src[i];
    i = i + 1;
  }
  dst[i] = 0;
}

void ctxt_strcat_n(char *dst, char *src, int max) {
  int d = 0;
  int s = 0;
  while (dst[d] && d < max - 1) d = d + 1;
  while (src[s] && d < max - 1) {
    dst[d] = src[s];
    d = d + 1;
    s = s + 1;
  }
  dst[d] = 0;
}

int ctxt_strcmp(char *a, char *b) {
  int i = 0;
  while (a[i] && b[i] && a[i] == b[i]) i = i + 1;
  return a[i] - b[i];
}

int ctxt_starts_with(char *line, char *prefix) {
  int i = 0;
  while (prefix[i]) {
    if (line[i] != prefix[i]) return 0;
    i = i + 1;
  }
  return 1;
}

int ctxt_starts_with_ci(char *line, char *prefix) {
  int i = 0;
  while (prefix[i]) {
    int a = line[i];
    int b = prefix[i];
    if (a >= 65 && a <= 90) a = a + 32;
    if (b >= 65 && b <= 90) b = b + 32;
    if (a != b) return 0;
    i = i + 1;
  }
  return 1;
}

int ctxt_hexdig(char c) {
  if (c >= 48 && c <= 57) return c - 48;
  if (c >= 65 && c <= 70) return c - 55;
  if (c >= 97 && c <= 102) return c - 87;
  return 0;
}

int ctxt_is_space(char c) {
  return c == 32 || c == 9 || c == 13 || c == 10;
}

int ctxt_tolower(int ch) {
  if (ch >= 65 && ch <= 90) return ch + 32;
  return ch;
}

int ctxt_is_path_char(char c) {
  if (c >= 48 && c <= 57) return 1;
  if (c >= 65 && c <= 90) return 1;
  if (c >= 97 && c <= 122) return 1;
  if (c == 47 || c == 46 || c == 95 || c == 45 || c == 58) return 1;
  return 0;
}

int ctxt_has_known_ext(char *s, int start, int end) {
  int dot = -1;
  int i = start;
  while (i < end) {
    if (s[i] == 46) dot = i;
    i = i + 1;
  }
  if (dot <= start || dot >= end - 1) return 0;

  int elen = end - dot - 1;
  int a = 0;
  int b = 0;
  int c = 0;
  int d = 0;

  if (elen >= 1) a = ctxt_tolower(s[dot + 1]);
  if (elen >= 2) b = ctxt_tolower(s[dot + 2]);
  if (elen >= 3) c = ctxt_tolower(s[dot + 3]);
  if (elen >= 4) d = ctxt_tolower(s[dot + 4]);

  if (elen == 1 && a == 'c') return 1;
  if (elen == 1 && a == 'h') return 1;
  if (elen == 2 && a == 'c' && b == 'c') return 1;
  if (elen == 2 && a == 'm' && b == 'd') return 1;
  if (elen == 3 && a == 't' && b == 'x' && c == 't') return 1;
  if (elen == 3 && a == 'c' && b == 'u' && c == 'p') return 1;
  if (elen == 3 && a == 'a' && b == 's' && c == 'm') return 1;
  if (elen == 3 && a == 'e' && b == 'l' && c == 'f') return 1;
  if (elen == 4 && a == 'c' && b == 't' && c == 'x' && d == 't') return 1;
  return 0;
}

int ctxt_is_bare_link_token(char *s, int start, int end) {
  int has_slash = 0;
  int has_dot = 0;
  int i = start;
  if (end <= start) return 0;
  while (i < end) {
    if (s[i] == 47) has_slash = 1;
    if (s[i] == 46) has_dot = 1;
    i = i + 1;
  }
  if (!has_dot) return 0;
  if (has_slash) return 1;
  return ctxt_has_known_ext(s, start, end);
}

int ctxt_parse_color(char *buf, int i) {
  if (buf[i] != 35) return 0;
  i = i + 1;
  int r = ctxt_hexdig(buf[i]) * 16 + ctxt_hexdig(buf[i + 1]);
  int g = ctxt_hexdig(buf[i + 2]) * 16 + ctxt_hexdig(buf[i + 3]);
  int b = ctxt_hexdig(buf[i + 4]) * 16 + ctxt_hexdig(buf[i + 5]);
  return (r * 65536) + (g * 256) + b;
}

int ctxt_trim_copy(char *src, char *dst, int max) {
  int s = 0;
  int e = ctxt_strlen(src);
  int i = 0;
  while (src[s] && ctxt_is_space(src[s])) s = s + 1;
  while (e > s && ctxt_is_space(src[e - 1])) e = e - 1;
  while (s < e && i < max - 1) {
    dst[i] = src[s];
    i = i + 1;
    s = s + 1;
  }
  dst[i] = 0;
  return i;
}

int ctxt_parse_u32(char *src) {
  int i = 0;
  int v = 0;
  while (src[i] && ctxt_is_space(src[i])) i = i + 1;
  while (src[i] >= 48 && src[i] <= 57) {
    v = v * 10 + (src[i] - 48);
    i = i + 1;
  }
  return v;
}

int ctxt_is_web_url(char *s) {
  return ctxt_starts_with_ci(s, "http://") || ctxt_starts_with_ci(s, "https://");
}

int ctxt_url_path_has_ext(char *s, char *ext) {
  int end = 0;
  int elen = ctxt_strlen(ext);
  while (s[end] && s[end] != '?' && s[end] != '#') end = end + 1;
  if (end < elen) return 0;
  return ctxt_starts_with_ci(s + end - elen, ext);
}

int ctxt_is_remote_image_url(char *s) {
  if (!ctxt_is_web_url(s)) return 0;
  return ctxt_url_path_has_ext(s, ".png") ||
         ctxt_url_path_has_ext(s, ".bmp") ||
         ctxt_url_path_has_ext(s, ".jpg") ||
         ctxt_url_path_has_ext(s, ".jpeg");
}

U32 ctxt_url_hash(char *s) {
  U32 h = 2166136261u;
  int i = 0;
  while (s[i]) {
    h = h ^ (U32)((unsigned char)s[i]);
    h = h * 16777619u;
    i = i + 1;
  }
  return h;
}

void ctxt_hex8(U32 v, char *out) {
  char *hex = "0123456789abcdef";
  int i = 0;
  while (i < 8) {
    int shift = (7 - i) * 4;
    out[i] = hex[(v >> shift) & 15u];
    i = i + 1;
  }
  out[8] = 0;
}

void ctxt_remote_image_ext(char *url, char *out) {
  int end = 0;
  int dot = -1;
  int o = 0;
  while (url[end] && url[end] != '?' && url[end] != '#') {
    if (url[end] == '.') dot = end;
    end = end + 1;
  }
  if (dot < 0 || dot >= end) {
    ctxt_strcpy_n(out, ".img", 8);
    return;
  }
  while (dot < end && o < 7) {
    out[o] = (char)ctxt_tolower(url[dot]);
    o = o + 1;
    dot = dot + 1;
  }
  out[o] = 0;
}

void ctxt_remote_cache_paths(int idx, char *asset, char *job,
                             char *done, char *fail) {
  char hex[9];
  char ext[8];
  ctxt_hex8(ctxt_url_hash(ctxt_sprite_path[idx]), hex);
  ctxt_remote_image_ext(ctxt_sprite_path[idx], ext);

  ctxt_strcpy_n(asset, "/ctxt-cache/ctxt_", 256);
  ctxt_strcat_n(asset, hex, 256);
  ctxt_strcat_n(asset, ext, 256);

  ctxt_strcpy_n(job, "/ctxt-cache/ctxt_", 256);
  ctxt_strcat_n(job, hex, 256);
  ctxt_strcat_n(job, ".job", 256);

  ctxt_strcpy_n(done, "/ctxt-cache/ctxt_", 256);
  ctxt_strcat_n(done, hex, 256);
  ctxt_strcat_n(done, ".done", 256);

  ctxt_strcpy_n(fail, "/ctxt-cache/ctxt_", 256);
  ctxt_strcat_n(fail, hex, 256);
  ctxt_strcat_n(fail, ".fail", 256);
}

int ctxt_file_exists(char *path) {
  char st[8];
  return vfs_stat(path, st) >= 0;
}

int ctxt_schedule_remote_image(int idx) {
  char asset[256];
  char job[256];
  char done[256];
  char fail[256];
  char body[2048];
  if (!CTXT_AUTO_FETCH_REMOTE_IMAGES) return 0;
  ctxt_remote_cache_paths(idx, asset, job, done, fail);

  if (ctxt_file_exists(done) && ctxt_file_exists(asset)) return 1;
  if (ctxt_file_exists(fail)) return 0;

  vfs_mkdir("/ctxt-cache");
  if (!ctxt_file_exists(job)) {
    body[0] = 0;
    ctxt_strcat_n(body, ctxt_sprite_path[idx], 2048);
    ctxt_strcat_n(body, "\n", 2048);
    ctxt_strcat_n(body, asset, 2048);
    ctxt_strcat_n(body, "\n", 2048);
    ctxt_strcat_n(body, done, 2048);
    ctxt_strcat_n(body, "\n", 2048);
    ctxt_strcat_n(body, fail, 2048);
    ctxt_strcat_n(body, "\n", 2048);
    vfs_write_text(job, body);
  }

  if (!CTXT_REMOTE_WORKER_STARTED) {
    ctxt_image_worker_start();
    CTXT_REMOTE_WORKER_STARTED = 1;
  }
  return 1;
}

int ctxt_is_url_break(char c) {
  if (ctxt_is_space(c)) return 1;
  if (c == ')' || c == ']' || c == '}' || c == '"' || c == '\'') return 1;
  return 0;
}

int ctxt_parse_ipv4(char *host, int *ip_out) {
  int parts[4];
  int i = 0;
  int p = 0;
  while (p < 4) {
    int v = 0;
    int digits = 0;
    if (host[i] < '0' || host[i] > '9') return 0;
    while (host[i] >= '0' && host[i] <= '9') {
      v = v * 10 + (host[i] - '0');
      digits = digits + 1;
      if (digits > 3 || v > 255) return 0;
      i = i + 1;
    }
    parts[p] = v;
    if (p < 3) {
      if (host[i] != '.') return 0;
      i = i + 1;
    }
    p = p + 1;
  }
  if (host[i] != 0) return 0;
  *ip_out = parts[0] | (parts[1] << 8) | (parts[2] << 16) | (parts[3] << 24);
  return 1;
}

int ctxt_parse_url(char *url, char *host, int *port_out, char *path,
                   int *is_https_out) {
  int i = 0;
  int hi = 0;
  int pi = 0;
  *is_https_out = 0;
  if (ctxt_starts_with_ci(url, "http://")) {
    i = 7;
  } else if (ctxt_starts_with_ci(url, "https://")) {
    i = 8;
    *is_https_out = 1;
  } else {
    return -1;
  }
  *port_out = *is_https_out ? 443 : 80;
  while (url[i] && url[i] != ':' && url[i] != '/' && hi < 255) {
    host[hi] = url[i];
    hi = hi + 1;
    i = i + 1;
  }
  host[hi] = 0;
  if (hi == 0) return -1;
  if (url[i] == ':') {
    int v = 0;
    i = i + 1;
    while (url[i] >= '0' && url[i] <= '9') {
      v = v * 10 + (url[i] - '0');
      i = i + 1;
    }
    if (v <= 0 || v > 65535) return -1;
    *port_out = v;
  }
  if (url[i] != '/') {
    path[pi] = '/';
    pi = pi + 1;
  }
  while (url[i] && pi < 1023) {
    path[pi] = url[i];
    pi = pi + 1;
    i = i + 1;
  }
  path[pi] = 0;
  return 0;
}

int ctxt_resolve_redirect(char *location, char *host, int port, int is_https,
                          char *out, int max) {
  int p = 0;
  int i = 0;
  if (ctxt_is_web_url(location)) {
    ctxt_strcpy_n(out, location, max);
    return 0;
  }
  if (location[0] != '/') return -1;
  char *prefix = is_https ? "https://" : "http://";
  while (prefix[i] && p < max - 1) {
    out[p] = prefix[i];
    p = p + 1;
    i = i + 1;
  }
  i = 0;
  while (host[i] && p < max - 1) {
    out[p] = host[i];
    p = p + 1;
    i = i + 1;
  }
  {
    int default_port = is_https ? 443 : 80;
    if (port != default_port && p < max - 8) {
      char tmp[8];
      int n = 0;
      int v = port;
      out[p] = ':';
      p = p + 1;
      while (v > 0 && n < 7) {
        tmp[n] = '0' + (v % 10);
        n = n + 1;
        v = v / 10;
      }
      while (n > 0 && p < max - 1) {
        n = n - 1;
        out[p] = tmp[n];
        p = p + 1;
      }
    }
  }
  i = 0;
  while (location[i] && p < max - 1) {
    out[p] = location[i];
    p = p + 1;
    i = i + 1;
  }
  out[p] = 0;
  return 0;
}

int ctxt_decode_chunked(char *raw, int body_start, int raw_len, char *out,
                        int max) {
  int i = body_start;
  int out_len = 0;
  while (i < raw_len) {
    int sz = 0;
    while (i < raw_len) {
      int ch = raw[i];
      int dig = -1;
      if (ch >= '0' && ch <= '9') dig = ch - '0';
      else if (ch >= 'a' && ch <= 'f') dig = 10 + (ch - 'a');
      else if (ch >= 'A' && ch <= 'F') dig = 10 + (ch - 'A');
      if (dig >= 0) {
        sz = (sz * 16) + dig;
        i = i + 1;
      } else {
        break;
      }
    }
    while (i < raw_len && raw[i] != 10) i = i + 1;
    if (i < raw_len && raw[i] == 10) i = i + 1;
    if (sz <= 0) return out_len;
    if (i + sz > raw_len) sz = raw_len - i;
    {
      int can = max - out_len;
      int k = 0;
      if (sz < can) can = sz;
      while (k < can) {
        out[out_len + k] = raw[i + k];
        k = k + 1;
      }
      out_len = out_len + can;
    }
    i = i + sz;
    while (i < raw_len && (raw[i] == 13 || raw[i] == 10)) i = i + 1;
    if (out_len >= max) return out_len;
  }
  return out_len;
}

int ctxt_fetch_url(char *url, char *out, int max, int *out_len) {
  char work_url[512];
  int redirects = 0;
  ctxt_strcpy_n(work_url, url, 512);
  *out_len = 0;

  while (redirects < 5) {
    char host[256];
    char path[1024];
    int port = 80;
    int is_https = 0;
    int ip = 0;
    int fd = -1;
    if (ctxt_parse_url(work_url, host, &port, path, &is_https) != 0) return -1;
    if (!ctxt_parse_ipv4(host, &ip)) {
      if (dns_resolve(host, &ip) != 0) return -1;
    }
    fd = socket(2);
    if (fd < 0) return -1;
    if (connect(fd, ip, htons(port)) != 0) {
      close(fd);
      return -1;
    }
    if (is_https) {
      int hl = ctxt_strlen(host);
      if (setsockopt(fd, 1, 1, host, hl + 1) != 0) {
        close(fd);
        return -1;
      }
    }

    char req[2048];
    int rp = 0;
    rp = ctxt_strlen("GET ");
    ctxt_strcpy_n(req, "GET ", 2048);
    ctxt_strcat_n(req, path, 2048);
    ctxt_strcat_n(req, " HTTP/1.1\r\nHost: ", 2048);
    ctxt_strcat_n(req, host, 2048);
    ctxt_strcat_n(req, "\r\nUser-Agent: cupidos-ctxt/1.0\r\nAccept: image/*,*/*\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n", 2048);
    rp = ctxt_strlen(req);
    if (send(fd, req, rp) < 0) {
      close(fd);
      return -1;
    }

    int raw_cap = max + 8192;
    char *raw = (char*)kmalloc(raw_cap);
    if (!raw) {
      close(fd);
      return -1;
    }
    int raw_len = 0;
    char tmp[4096];
    while (1) {
      int n = recv(fd, tmp, 4096);
      if (n <= 0) break;
      int can = raw_cap - raw_len;
      int k = 0;
      if (n < can) can = n;
      while (k < can) {
        raw[raw_len + k] = tmp[k];
        k = k + 1;
      }
      raw_len = raw_len + can;
      if (raw_len >= raw_cap) break;
    }
    close(fd);

    int header_end = -1;
    int i = 0;
    while (i + 3 < raw_len) {
      if (raw[i] == 13 && raw[i + 1] == 10 &&
          raw[i + 2] == 13 && raw[i + 3] == 10) {
        header_end = i + 4;
        break;
      }
      i = i + 1;
    }
    if (header_end < 0) {
      kfree(raw);
      return -1;
    }

    int status = 0;
    i = 0;
    while (i < raw_len && raw[i] != ' ') i = i + 1;
    while (i < raw_len && raw[i] == ' ') i = i + 1;
    while (i < raw_len && raw[i] >= '0' && raw[i] <= '9') {
      status = status * 10 + (raw[i] - '0');
      i = i + 1;
    }

    int chunked = 0;
    char location[512];
    location[0] = 0;
    i = 0;
    while (i < header_end) {
      int ls = i;
      int le = i;
      while (le < header_end && raw[le] != 10) le = le + 1;
      if (le > ls && raw[le - 1] == 13) le = le - 1;
      if (le - ls > 9 && ctxt_starts_with_ci(raw + ls, "location:")) {
        int p = ls + 9;
        int o = 0;
        while (p < le && ctxt_is_space(raw[p])) p = p + 1;
        while (p < le && o < 511) {
          location[o] = raw[p];
          o = o + 1;
          p = p + 1;
        }
        location[o] = 0;
      }
      if (le - ls > 18 && ctxt_starts_with_ci(raw + ls, "transfer-encoding:")) {
        int p = ls + 18;
        while (p < le && ctxt_is_space(raw[p])) p = p + 1;
        if (ctxt_starts_with_ci(raw + p, "chunked")) chunked = 1;
      }
      i = le + 1;
    }

    if (status >= 300 && status < 400 && location[0]) {
      char next_url[512];
      if (ctxt_resolve_redirect(location, host, port, is_https, next_url, 512) == 0) {
        ctxt_strcpy_n(work_url, next_url, 512);
        redirects = redirects + 1;
        kfree(raw);
        continue;
      }
    }

    if (status < 200 || status >= 300) {
      kfree(raw);
      return -1;
    }
    if (chunked) {
      *out_len = ctxt_decode_chunked(raw, header_end, raw_len, out, max);
    } else {
      int body_len = raw_len - header_end;
      int k = 0;
      if (body_len > max) body_len = max;
      while (k < body_len) {
        out[k] = raw[header_end + k];
        k = k + 1;
      }
      *out_len = body_len;
    }
    kfree(raw);
    return *out_len > 0 ? 0 : -1;
  }
  return -1;
}

int ctxt_named_color(char *name) {
  if (ctxt_strcmp(name, "black") == 0) return 0x00000000;
  if (ctxt_strcmp(name, "red") == 0) return 0x00FF5555;
  if (ctxt_strcmp(name, "green") == 0) return 0x0055FF55;
  if (ctxt_strcmp(name, "yellow") == 0) return 0x00FFFF55;
  if (ctxt_strcmp(name, "blue") == 0) return 0x005555FF;
  if (ctxt_strcmp(name, "purple") == 0) return 0x00FF55FF;
  if (ctxt_strcmp(name, "cyan") == 0) return 0x0055FFFF;
  if (ctxt_strcmp(name, "white") == 0) return 0x00FFFFFF;
  if (ctxt_strcmp(name, "ltgray") == 0) return 0x00AAAAAA;
  if (ctxt_strcmp(name, "dkgray") == 0) return 0x00555555;
  if (ctxt_strcmp(name, "dkred") == 0) return 0x00AA0000;
  if (ctxt_strcmp(name, "dkblue") == 0) return 0x000000AA;
  if (ctxt_strcmp(name, "dkgreen") == 0) return 0x0000AA00;
  return -1;
}

void ctxt_set_theme(int light) {
  ctxt_theme_light = light;
  if (light) {
    ctxt_col_bg = 0x00F7F4EC;
    ctxt_col_h1 = 0x00B03060;
    ctxt_col_h2 = 0x002B4FA8;
    ctxt_col_h3 = 0x009A5A00;
    ctxt_col_body = 0x001A1A1A;
    ctxt_col_rule = 0x00B8B1A3;
    ctxt_col_box_bg = 0x00EEE7D8;
    ctxt_col_box_text = 0x00223355;
    ctxt_col_link = 0x001D4ED8;
    ctxt_col_link_hover = 0x000B3AA8;
    ctxt_col_code_bg = 0x00E7E0D2;
    ctxt_col_code_text = 0x001A1A1A;
  } else {
    ctxt_col_bg = 0x001E1E2E;
    ctxt_col_h1 = 0x00F38BA8;
    ctxt_col_h2 = 0x0089B4FA;
    ctxt_col_h3 = 0x00FAB387;
    ctxt_col_body = 0x00CDD6F4;
    ctxt_col_rule = 0x00585B70;
    ctxt_col_box_bg = 0x00313244;
    ctxt_col_box_text = 0x00CDD6F4;
    ctxt_col_link = 0x0089DCEB;
    ctxt_col_link_hover = 0x00BDEBFF;
    ctxt_col_code_bg = 0x00242A3B;
    ctxt_col_code_text = 0x00E6EDF7;
  }
}

void ctxt_set_theme_temple() {
  ctxt_theme_light = 0;
  ctxt_col_bg = 0x00000000;
  ctxt_col_h1 = 0x00FFFF55;
  ctxt_col_h2 = 0x0055FFFF;
  ctxt_col_h3 = 0x00FF5555;
  ctxt_col_body = 0x00FFFFFF;
  ctxt_col_rule = 0x00AA0000;
  ctxt_col_box_bg = 0x00AA0000;
  ctxt_col_box_text = 0x00FFFFFF;
  ctxt_col_link = 0x0055FFFF;
  ctxt_col_link_hover = 0x00FFFF55;
  ctxt_col_code_bg = 0x00000000;
  ctxt_col_code_text = 0x0055FF55;
}

void ctxt_release_sprite_handles() {
  int i = 0;
  while (i < ctxt_sprite_count) {
    if (ctxt_sprite_handle[i] >= 0) {
      gfx2d_image_free(ctxt_sprite_handle[i]);
      ctxt_sprite_handle[i] = -1;
    }
    ctxt_sprite_state[i] = 0;
    i = i + 1;
  }
}

void ctxt_reset() {
  ctxt_release_sprite_handles();
  ctxt_line_count = 0;
  ctxt_link_count = 0;
  ctxt_action_count = 0;
  ctxt_sprite_count = 0;
  ctxt_tree_count = 0;
  ctxt_total_h = 0;
  ctxt_total_w = 0;
  CTXT_BODY = 0;
  CTXT_H1 = 1;
  CTXT_H2 = 2;
  CTXT_H3 = 3;
  CTXT_RULE = 4;
  CTXT_CENTER = 5;
  CTXT_COMMENT = 6;
  CTXT_BUTTON = 7;
  CTXT_CODE_HEADER = 8;
  CTXT_CODE = 9;
  CTXT_SPRITE = 10;
  CTXT_TREE = 11;
  CTXT_ACT_OPEN = 0;
  CTXT_ACT_SHELL = 1;
  CTXT_ACT_REPL = 2;
  CTXT_ACT_TREE = 3;
  CTXT_ACT_WEB = 4;
  CTXT_MAX_LINES = 1024;
  CTXT_MAX_LINKS = 192;
  CTXT_MAX_TEXT = 128;
  CTXT_WEB_BUF_SIZE = 524288;
  CTXT_AUTO_FETCH_REMOTE_IMAGES = 1;
  CTXT_REMOTE_WORKER_STARTED = 0;
  ctxt_set_theme(1);
}

int ctxt_line_h(int type) {
  if (type == CTXT_H1) return 20;
  if (type == CTXT_H2) return 16;
  if (type == CTXT_H3) return 12;
  if (type == CTXT_RULE) return 10;
  if (type == CTXT_BUTTON) return 18;
  if (type == CTXT_TREE) return 16;
  if (type == CTXT_CODE_HEADER) return 18;
  if (type == CTXT_CODE) return 12;
  if (type == CTXT_SPRITE) return 12;
  if (type == CTXT_COMMENT) return 0;
  return 10;
}

int ctxt_line_scale(int type) {
  if (type == CTXT_H1) return 2;
  if (type == CTXT_H2) return 2;
  return 1;
}

int ctxt_add_action(int type, char *payload) {
  int idx = ctxt_action_count;
  if (idx >= 128) return -1;
  ctxt_action_type[idx] = type;
  ctxt_strcpy_n(ctxt_action_payload[idx], payload, 512);
  ctxt_action_count = ctxt_action_count + 1;
  return idx;
}

int ctxt_add_sprite(char *path, int w, int h, int action_ref, int explicit_size) {
  int idx = ctxt_sprite_count;
  int remote = ctxt_is_remote_image_url(path);
  if (idx >= 64) return -1;
  ctxt_strcpy_n(ctxt_sprite_path[idx], path, 512);
  ctxt_sprite_w[idx] = w;
  ctxt_sprite_h[idx] = h;
  ctxt_sprite_action[idx] = action_ref;
  ctxt_sprite_remote[idx] = remote;
  ctxt_sprite_state[idx] = 0;
  ctxt_sprite_handle[idx] = -1;
  ctxt_sprite_explicit_size[idx] = explicit_size;
  ctxt_sprite_count = ctxt_sprite_count + 1;
  return idx;
}

int ctxt_add_tree(int open) {
  int idx = ctxt_tree_count;
  if (idx >= 32) return -1;
  ctxt_tree_open[idx] = open;
  ctxt_tree_count = ctxt_tree_count + 1;
  return idx;
}

int ctxt_image_arg_pos(char *line) {
  if (ctxt_starts_with(line, ">sprite ")) return 8;
  if (ctxt_starts_with(line, ">image ")) return 7;
  if (ctxt_starts_with(line, ">img ")) return 5;
  return -1;
}

int ctxt_parse_action(char *src, int type_out_ptr, int payload_out_ptr, int payload_max) {
  char *payload = payload_out_ptr;
  int type = CTXT_ACT_OPEN;
  char tmp[512];
  int i = 0;
  while (src[i] && ctxt_is_space(src[i])) i = i + 1;
  ctxt_strcpy_n(tmp, src + i, 512);
  if (ctxt_starts_with(tmp, "open:")) {
    type = CTXT_ACT_OPEN;
    ctxt_trim_copy(tmp + 5, payload, payload_max);
  } else if (ctxt_starts_with(tmp, "shell:")) {
    type = CTXT_ACT_SHELL;
    ctxt_trim_copy(tmp + 6, payload, payload_max);
  } else if (ctxt_starts_with(tmp, "repl:")) {
    type = CTXT_ACT_REPL;
    ctxt_trim_copy(tmp + 5, payload, payload_max);
  } else if (ctxt_starts_with(tmp, "web:")) {
    type = CTXT_ACT_WEB;
    ctxt_trim_copy(tmp + 4, payload, payload_max);
  } else if (ctxt_is_web_url(tmp)) {
    type = CTXT_ACT_WEB;
    ctxt_trim_copy(tmp, payload, payload_max);
  } else {
    type = CTXT_ACT_SHELL;
    ctxt_trim_copy(tmp, payload, payload_max);
  }
  int *type_out = type_out_ptr;
  *type_out = type;
  return payload[0] != 0;
}

int ctxt_line_visible(int idx) {
  int mask = ctxt_line_tree_mask[idx];
  int bit = 0;
  while (bit < ctxt_tree_count && bit < 32) {
    if ((mask & (1 << bit)) && !ctxt_tree_open[bit]) return 0;
    bit = bit + 1;
  }
  return 1;
}

void ctxt_refresh_metrics() {
  int i = 0;
  int total_h = 0;
  int max_w = 0;
  while (i < ctxt_line_count) {
    int type = ctxt_line_type[i];
    int lh = ctxt_line_h(type);
    int lw = 0;
    if (ctxt_line_visible(i) && type != CTXT_COMMENT) {
      if (type == CTXT_BUTTON || type == CTXT_TREE) {
        lw = gfx2d_text_width(ctxt_line_text[i], 1) + 20;
      } else if (type == CTXT_CODE_HEADER) {
        lw = gfx2d_text_width(ctxt_line_text[i], 1) + 72;
      } else if (type == CTXT_SPRITE) {
        lw = ctxt_line_aux_a[i] + 8;
        total_h = total_h + ctxt_line_aux_b[i];
      } else {
        lw = gfx2d_text_width(ctxt_line_text[i], ctxt_line_scale(type));
      }
      if (lw > max_w) max_w = lw;
      total_h = total_h + lh;
    }
    i = i + 1;
  }
  ctxt_total_h = total_h;
  ctxt_total_w = max_w + 8;
}

void ctxt_parse(int buf_ptr, int len) {
  char *buf = buf_ptr;
  int i = 0;
  int in_box = 0;
  int cur_box_bg = 0;
  int tree_mask = 0;
  int tree_stack[32];
  int tree_top = 0;
  int code_action = -1;

  ctxt_release_sprite_handles();
  ctxt_line_count = 0;
  ctxt_link_count = 0;
  ctxt_action_count = 0;
  ctxt_sprite_count = 0;
  ctxt_tree_count = 0;

  while (i < len && ctxt_line_count < CTXT_MAX_LINES) {
    int ls = i;
    while (i < len && buf[i] != 10) i = i + 1;
    int le = i;
    if (i < len) i = i + 1;

    char line[256];
    int ll = le - ls;
    if (ll > 255) ll = 255;
    int j = 0;
    while (j < ll) {
      line[j] = buf[ls + j];
      j = j + 1;
    }
    line[ll] = 0;
    if (ll > 0 && line[ll - 1] == 13) {
      ll = ll - 1;
      line[ll] = 0;
    }

    int n = ctxt_line_count;
    int type = CTXT_BODY;
    int ref = -1;
    int aux_a = 0;
    int aux_b = 0;

    ctxt_line_color[n] = 0;
    ctxt_line_bg_color[n] = 0;
    ctxt_line_ref[n] = -1;
    ctxt_line_aux_a[n] = 0;
    ctxt_line_aux_b[n] = 0;
    /* Snapshot BEFORE >tree/>endtree update tree_mask. A tree's own button
     * line must be visible regardless of its own open/closed state - otherwise
     * a closed tree hides the toggle that would open it.*/
    int line_tree_mask = tree_mask;

    if (code_action >= 0) {
      if (ctxt_starts_with(line, ">endcode")) {
        code_action = -1;
        continue;
      }
      type = CTXT_CODE;
      ref = code_action;
      ctxt_strcpy_n(ctxt_line_text[n], line, CTXT_MAX_TEXT);
      if (ctxt_action_payload[code_action][0]) ctxt_strcat_n(ctxt_action_payload[code_action], "\n", 512);
      ctxt_strcat_n(ctxt_action_payload[code_action], line, 512);
    } else if (line[0] == 62) {
      if (ctxt_starts_with(line, ">h1 ")) {
        type = CTXT_H1;
        ctxt_strcpy_n(ctxt_line_text[n], line + 4, CTXT_MAX_TEXT);
      } else if (ctxt_starts_with(line, ">h2 ")) {
        type = CTXT_H2;
        ctxt_strcpy_n(ctxt_line_text[n], line + 4, CTXT_MAX_TEXT);
      } else if (ctxt_starts_with(line, ">h3 ")) {
        type = CTXT_H3;
        ctxt_strcpy_n(ctxt_line_text[n], line + 4, CTXT_MAX_TEXT);
      } else if (ctxt_starts_with(line, ">rule")) {
        type = CTXT_RULE;
        ctxt_line_text[n][0] = 0;
      } else if (ctxt_starts_with(line, ">center ")) {
        type = CTXT_CENTER;
        ctxt_strcpy_n(ctxt_line_text[n], line + 8, CTXT_MAX_TEXT);
      } else if (ctxt_starts_with(line, ">comment")) {
        type = CTXT_COMMENT;
        ctxt_line_text[n][0] = 0;
      } else if (ctxt_starts_with(line, ">theme light")) {
        ctxt_set_theme(1);
        type = CTXT_COMMENT;
        ctxt_line_text[n][0] = 0;
      } else if (ctxt_starts_with(line, ">theme dark")) {
        ctxt_set_theme(0);
        type = CTXT_COMMENT;
        ctxt_line_text[n][0] = 0;
      } else if (ctxt_starts_with(line, ">theme temple")) {
        ctxt_set_theme_temple();
        type = CTXT_COMMENT;
        ctxt_line_text[n][0] = 0;
      } else if (ctxt_starts_with(line, ">style ")) {
        int hash_i = 0;
        int color = 0;
        while (line[hash_i] && line[hash_i] != 35) hash_i = hash_i + 1;
        if (line[hash_i] == 35) color = ctxt_parse_color(line, hash_i);
        if (color != 0) {
          if (ctxt_starts_with(line, ">style bg ")) ctxt_col_bg = color;
          else if (ctxt_starts_with(line, ">style body ")) ctxt_col_body = color;
          else if (ctxt_starts_with(line, ">style h1 ")) ctxt_col_h1 = color;
          else if (ctxt_starts_with(line, ">style h2 ")) ctxt_col_h2 = color;
          else if (ctxt_starts_with(line, ">style h3 ")) ctxt_col_h3 = color;
          else if (ctxt_starts_with(line, ">style rule ")) ctxt_col_rule = color;
          else if (ctxt_starts_with(line, ">style box ")) ctxt_col_box_bg = color;
          else if (ctxt_starts_with(line, ">style boxtext ")) ctxt_col_box_text = color;
          else if (ctxt_starts_with(line, ">style codebg ")) ctxt_col_code_bg = color;
          else if (ctxt_starts_with(line, ">style codetext ")) ctxt_col_code_text = color;
          else if (ctxt_starts_with(line, ">style link ")) ctxt_col_link = color;
          else if (ctxt_starts_with(line, ">style linkhover ")) ctxt_col_link_hover = color;
        }
        type = CTXT_COMMENT;
        ctxt_line_text[n][0] = 0;
      } else if (ctxt_starts_with(line, ">box")) {
        in_box = 1;
        cur_box_bg = ctxt_col_box_bg;
        if (ctxt_strlen(line) >= 11) {
          int color = ctxt_parse_color(line, 5);
          if (color != 0) cur_box_bg = color;
        }
        type = CTXT_COMMENT;
        ctxt_line_text[n][0] = 0;
      } else if (ctxt_starts_with(line, ">endbox")) {
        in_box = 0;
        cur_box_bg = 0;
        type = CTXT_COMMENT;
        ctxt_line_text[n][0] = 0;
      } else if (ctxt_starts_with(line, ">button ")) {
        char label[128];
        char action_src[512];
        char payload[512];
        int action_type = CTXT_ACT_SHELL;
        int p = 8;
        int l = 0;
        int a = 0;
        while (line[p] && !(line[p] == '|' )) {
          if (l < 127) label[l] = line[p];
          l = l + 1;
          p = p + 1;
        }
        if (l > 127) l = 127;
        label[l] = 0;
        if (line[p] == '|') p = p + 1;
        while (line[p]) {
          if (a < 511) action_src[a] = line[p];
          a = a + 1;
          p = p + 1;
        }
        if (a > 511) a = 511;
        action_src[a] = 0;
        ctxt_trim_copy(label, ctxt_line_text[n], CTXT_MAX_TEXT);
        if (ctxt_parse_action(action_src, &action_type, payload, 512)) {
          type = CTXT_BUTTON;
          ref = ctxt_add_action(action_type, payload);
        } else {
          type = CTXT_COMMENT;
          ctxt_line_text[n][0] = 0;
        }
      } else if (ctxt_starts_with(line, ">code")) {
        char label[128];
        char payload[2];
        payload[0] = 0;
        payload[1] = 0;
        ctxt_trim_copy(line + 5, label, 128);
        if (!label[0]) ctxt_strcpy_n(label, "Run CupidC", 128);
        type = CTXT_CODE_HEADER;
        ref = ctxt_add_action(CTXT_ACT_REPL, payload);
        ctxt_strcpy_n(ctxt_line_text[n], label, CTXT_MAX_TEXT);
        code_action = ref;
      } else if (ctxt_starts_with(line, ">tree")) {
        char label[128];
        int open = 0;
        char *arg = line + 5;
        while (*arg && ctxt_is_space(*arg)) arg = arg + 1;
        if (ctxt_starts_with(arg, "open ")) {
          open = 1;
          arg = arg + 5;
        } else if (ctxt_starts_with(arg, "closed ")) {
          arg = arg + 7;
        }
        ctxt_trim_copy(arg, label, 128);
        if (!label[0]) ctxt_strcpy_n(label, "Section", 128);
        type = CTXT_TREE;
        ctxt_strcpy_n(ctxt_line_text[n], label, CTXT_MAX_TEXT);
        ref = ctxt_add_tree(open);
        if (ref >= 0 && tree_top < 32) {
          tree_stack[tree_top] = ref;
          tree_top = tree_top + 1;
          tree_mask = tree_mask | (1 << ref);
        }
      } else if (ctxt_starts_with(line, ">endtree")) {
        type = CTXT_COMMENT;
        ctxt_line_text[n][0] = 0;
        if (tree_top > 0) {
          tree_top = tree_top - 1;
          tree_mask = tree_mask & ~(1 << tree_stack[tree_top]);
        }
      } else if (ctxt_image_arg_pos(line) >= 0) {
        char sprite_args[512];
        char path[512];
        char size1[32];
        char size2[32];
        char action_src[512];
        char payload[512];
        int action_type = CTXT_ACT_OPEN;
        int action_ref = -1;
        int p = ctxt_image_arg_pos(line);
        int q = 0;
        int s = 0;
        int draw_w = 0;
        int draw_h = 0;
        int explicit_size = 0;
        int cut = 0;
        while (line[p + cut] && line[p + cut] != '|') {
          if (cut < 511) sprite_args[cut] = line[p + cut];
          cut = cut + 1;
        }
        if (cut > 511) cut = 511;
        sprite_args[cut] = 0;
        if (line[p + cut] == '|') {
          int a = 0;
          p = p + cut + 1;
          while (line[p] && a < 511) {
            action_src[a] = line[p];
            a = a + 1;
            p = p + 1;
          }
          action_src[a] = 0;
          if (ctxt_parse_action(action_src, &action_type, payload, 512)) {
            action_ref = ctxt_add_action(action_type, payload);
          }
          p = 0;
        } else {
          p = ctxt_image_arg_pos(line);
        }
        if (sprite_args[0]) {
          p = 0;
          while (sprite_args[p] && ctxt_is_space(sprite_args[p])) p = p + 1;
        }
        while (sprite_args[p] && !ctxt_is_space(sprite_args[p]) && q < 511) {
          path[q] = sprite_args[p];
          q = q + 1;
          p = p + 1;
        }
        path[q] = 0;
        while (sprite_args[p] && ctxt_is_space(sprite_args[p])) p = p + 1;
        while (sprite_args[p] && !ctxt_is_space(sprite_args[p]) && s < 31) {
          size1[s] = sprite_args[p];
          s = s + 1;
          p = p + 1;
        }
        size1[s] = 0;
        while (sprite_args[p] && ctxt_is_space(sprite_args[p])) p = p + 1;
        s = 0;
        while (sprite_args[p] && !ctxt_is_space(sprite_args[p]) && s < 31) {
          size2[s] = sprite_args[p];
          s = s + 1;
          p = p + 1;
        }
        size2[s] = 0;
        draw_w = ctxt_parse_u32(size1);
        draw_h = ctxt_parse_u32(size2);
        explicit_size = size1[0] != 0;
        if (draw_w <= 0) draw_w = ctxt_is_web_url(path) ? 160 : 96;
        if (draw_h <= 0) draw_h = draw_w;
        type = CTXT_SPRITE;
        ref = ctxt_add_sprite(path, draw_w, draw_h, action_ref, explicit_size);
        aux_a = draw_w;
        aux_b = draw_h;
        ctxt_strcpy_n(ctxt_line_text[n], path, CTXT_MAX_TEXT);
      } else {
        ctxt_strcpy_n(ctxt_line_text[n], line, CTXT_MAX_TEXT);
      }
    } else {
      ctxt_strcpy_n(ctxt_line_text[n], line, CTXT_MAX_TEXT);
    }

    ctxt_line_type[n] = type;
    ctxt_line_ref[n] = ref;
    ctxt_line_aux_a[n] = aux_a;
    ctxt_line_aux_b[n] = aux_b;
    ctxt_line_tree_mask[n] = line_tree_mask;
    ctxt_line_bg_color[n] = in_box ? cur_box_bg : 0;
    ctxt_line_count = ctxt_line_count + 1;
  }

  ctxt_refresh_metrics();
}

int ctxt_content_h() {
  return ctxt_total_h;
}

int ctxt_content_w() { return ctxt_total_w; }

void ctxt_add_link_rect(int x, int y, int w, int h, int action, int ref, char *target) {
  if (ctxt_link_count >= CTXT_MAX_LINKS) return;
  ctxt_link_x[ctxt_link_count] = x;
  ctxt_link_y[ctxt_link_count] = y;
  ctxt_link_w[ctxt_link_count] = w;
  ctxt_link_h[ctxt_link_count] = h;
  ctxt_link_action[ctxt_link_count] = action;
  ctxt_link_ref[ctxt_link_count] = ref;
  ctxt_strcpy_n(ctxt_link_target[ctxt_link_count], target, 512);
  ctxt_link_count = ctxt_link_count + 1;
}

void ctxt_draw_text_links(int base_x, int py, int x, int x2, int y, int y2,
                          int lh, int scale, int fg, char *text) {
  int ch_w = 8 * scale;
  int px = base_x;
  int ti = 0;
  int cur_fg = fg;
  int cur_bg = 0;
  while (text[ti]) {
    if (text[ti] == 36) {
      int li = ti + 1;
      char code[8];
      char arg[64];
      char arg2[512];
      int ci = 0;
      int ai = 0;
      int has_arg = 0;
      while (text[li] && text[li] != 36 && text[li] != 44 && ci < 7) {
        code[ci] = text[li];
        ci = ci + 1;
        li = li + 1;
      }
      code[ci] = 0;
      if (text[li] == 44) {
        has_arg = 1;
        li = li + 1;
        if (text[li] == 34) li = li + 1;
        while (text[li] && text[li] != 36 && text[li] != 34 && ai < 63) {
          arg[ai] = text[li];
          ai = ai + 1;
          li = li + 1;
        }
        arg[ai] = 0;
        if (text[li] == 34) li = li + 1;
        if (text[li] == 44) {
          int a2 = 0;
          li = li + 1;
          while (text[li] && text[li] != 36 && a2 < 511) {
            arg2[a2] = text[li];
            a2 = a2 + 1;
            li = li + 1;
          }
          arg2[a2] = 0;
        } else {
          arg2[0] = 0;
        }
      } else {
        arg[0] = 0;
        arg2[0] = 0;
      }
      if (text[li] == 36) {
        if (ctxt_strcmp(code, "FG") == 0) {
          if (has_arg) {
            int c = arg[0] == 35 ? ctxt_parse_color(arg, 0) : ctxt_named_color(arg);
            if (c >= 0) cur_fg = c;
          } else {
            cur_fg = fg;
          }
          ti = li + 1;
          continue;
        } else if (ctxt_strcmp(code, "BG") == 0) {
          if (has_arg) {
            int c = arg[0] == 35 ? ctxt_parse_color(arg, 0) : ctxt_named_color(arg);
            if (c >= 0) cur_bg = c;
          } else {
            cur_bg = 0;
          }
          ti = li + 1;
          continue;
        } else if (ctxt_strcmp(code, "CM") == 0) {
          ti = li + 1;
          continue;
        } else if (ctxt_strcmp(code, "TX") == 0) {
          int k = 0;
          while (arg[k]) {
            if (cur_bg) gfx2d_rect_fill(px, py, ch_w, lh, cur_bg);
            if (px + ch_w > x && px < x2 && py + lh > y && py < y2) {
              gfx2d_char_scaled(px, py, arg[k], cur_fg, scale);
            }
            px = px + ch_w;
            k = k + 1;
          }
          ti = li + 1;
          continue;
        } else if (ctxt_strcmp(code, "LK") == 0) {
          char payload[512];
          int action_type = CTXT_ACT_OPEN;
          int ux = px;
          int k = 0;
          while (arg[k]) {
            if (px + ch_w > x && px < x2 && py + lh > y && py < y2) {
              gfx2d_char_scaled(px, py, arg[k], ctxt_col_link, scale);
            }
            px = px + ch_w;
            k = k + 1;
          }
          if (arg2[0] && ctxt_parse_action(arg2, &action_type, payload, 512)) {
            int uw = k * ch_w;
            if (uw > 0) {
              gfx2d_hline(ux, py + lh - 2, uw, ctxt_col_link);
              ctxt_add_link_rect(ux, py, uw, lh, action_type, -1, payload);
            }
          }
          ti = li + 1;
          continue;
        }
      }
    }
    if (text[ti] == 91) {
      int li = ti + 1;
      char label[64];
      char target[512];
      char payload[512];
      int action_type = CTXT_ACT_OPEN;
      int lpos = 0;
      int tpos = 0;
      while (text[li] && text[li] != 93 && lpos < 63) {
        label[lpos] = text[li];
        lpos = lpos + 1;
        li = li + 1;
      }
      label[lpos] = 0;
      if (text[li] == 93 && text[li + 1] == 40) {
        li = li + 2;
        while (text[li] && text[li] != 41 && tpos < 511) {
          target[tpos] = text[li];
          tpos = tpos + 1;
          li = li + 1;
        }
        target[tpos] = 0;
        if (text[li] == 41) {
          int ux = px;
          int k = 0;
          while (label[k]) {
            if (px + ch_w > x && px < x2 && py + lh > y && py < y2) {
              gfx2d_char_scaled(px, py, label[k], ctxt_col_link, scale);
            }
            px = px + ch_w;
            k = k + 1;
          }
          if (ctxt_parse_action(target, &action_type, payload, 512)) {
            int uw = lpos * ch_w;
            if (uw > 0) {
              gfx2d_hline(ux, py + lh - 2, uw, ctxt_col_link);
              ctxt_add_link_rect(ux, py, uw, lh, action_type, -1, payload);
            }
          }
          ti = li + 1;
          continue;
        }
      }
    }

    if (ctxt_is_web_url(text + ti) &&
        (ti == 0 || ctxt_is_url_break(text[ti - 1]))) {
      int li = ti;
      int plen = 0;
      while (text[li] && !ctxt_is_url_break(text[li]) && plen < 511) {
        li = li + 1;
        plen = plen + 1;
      }
      if (plen > 7) {
        int ux = px;
        int k = 0;
        while (k < plen) {
          if (px + ch_w > x && px < x2 && py + lh > y && py < y2) {
            gfx2d_char_scaled(px, py, text[ti + k], ctxt_col_link, scale);
          }
          px = px + ch_w;
          k = k + 1;
        }
        gfx2d_hline(ux, py + lh - 2, plen * ch_w, ctxt_col_link);
        {
          char tok[512];
          k = 0;
          while (k < plen && k < 511) {
            tok[k] = text[ti + k];
            k = k + 1;
          }
          tok[k] = 0;
          ctxt_add_link_rect(ux, py, plen * ch_w, lh, CTXT_ACT_WEB, -1, tok);
        }
        ti = li;
        continue;
      }
    }

    if (ctxt_is_path_char(text[ti]) &&
        (ti == 0 || !ctxt_is_path_char(text[ti - 1]))) {
      int li = ti;
      int plen = 0;
      while (text[li] && ctxt_is_path_char(text[li]) && plen < 127) {
        li = li + 1;
        plen = plen + 1;
      }
      if (plen >= 3 && ctxt_is_bare_link_token(text, ti, li)) {
        int ux = px;
        int k = 0;
        while (k < plen) {
          if (px + ch_w > x && px < x2 && py + lh > y && py < y2) {
            gfx2d_char_scaled(px, py, text[ti + k], ctxt_col_link, scale);
          }
          px = px + ch_w;
          k = k + 1;
        }
        gfx2d_hline(ux, py + lh - 2, plen * ch_w, ctxt_col_link);
        {
          char tok[128];
          k = 0;
          while (k < plen && k < 127) {
            tok[k] = text[ti + k];
            k = k + 1;
          }
          tok[k] = 0;
          ctxt_add_link_rect(ux, py, plen * ch_w, lh, CTXT_ACT_OPEN, -1, tok);
        }
        ti = li;
        continue;
      }
    }

    if (cur_bg && px + ch_w > x && px < x2 && py + lh > y && py < y2) {
      gfx2d_rect_fill(px, py, ch_w, lh, cur_bg);
    }
    if (px + ch_w > x && px < x2 && py + lh > y && py < y2) {
      gfx2d_char_scaled(px, py, text[ti], cur_fg, scale);
    }
    px = px + ch_w;
    ti = ti + 1;
  }
}

void ctxt_apply_sprite_intrinsic(int idx) {
  int iw = gfx2d_image_width(ctxt_sprite_handle[idx]);
  int ih = gfx2d_image_height(ctxt_sprite_handle[idx]);
  if (iw <= 0 || ih <= 0) return;
  if (!ctxt_sprite_explicit_size[idx]) {
    if (iw > 640) {
      ih = (ih * 640) / iw;
      iw = 640;
    }
    if (ih > 480) {
      iw = (iw * 480) / ih;
      ih = 480;
    }
    if (iw < 1) iw = 1;
    if (ih < 1) ih = 1;
    ctxt_sprite_w[idx] = iw;
    ctxt_sprite_h[idx] = ih;
  }
}

void ctxt_sprite_ensure_loaded(int idx) {
  if (idx < 0 || idx >= ctxt_sprite_count) return;
  if (ctxt_sprite_state[idx] != 0) return;

  if (ctxt_sprite_remote[idx]) {
    if (CTXT_AUTO_FETCH_REMOTE_IMAGES && ctxt_schedule_remote_image(idx)) {
      ctxt_sprite_state[idx] = 3;
    } else {
      ctxt_sprite_state[idx] = 2;
    }
    return;
  }

  ctxt_sprite_state[idx] = 2;
  int handle = gfx2d_image_load(ctxt_sprite_path[idx]);
  if (handle >= 0) {
    ctxt_sprite_handle[idx] = handle;
    ctxt_sprite_state[idx] = 1;
    ctxt_apply_sprite_intrinsic(idx);
  }
}

int ctxt_pump_remote_images() {
  if (!CTXT_AUTO_FETCH_REMOTE_IMAGES) return 0;

  int changed = 0;
  int i = 0;
  while (i < ctxt_sprite_count) {
    if (ctxt_sprite_remote[i]) {
      if (ctxt_sprite_state[i] == 0) {
        if (ctxt_schedule_remote_image(i)) {
          ctxt_sprite_state[i] = 3;
        } else {
          ctxt_sprite_state[i] = 2;
        }
        changed = 1;
      } else if (ctxt_sprite_state[i] == 3) {
        char asset[256];
        char job[256];
        char done[256];
        char fail[256];
        ctxt_remote_cache_paths(i, asset, job, done, fail);
        if (ctxt_file_exists(done) && ctxt_file_exists(asset)) {
          int handle = gfx2d_image_load(asset);
          if (handle >= 0) {
            ctxt_sprite_handle[i] = handle;
            ctxt_sprite_state[i] = 1;
            ctxt_apply_sprite_intrinsic(i);
          } else {
            ctxt_sprite_state[i] = 2;
          }
          changed = 1;
        } else if (ctxt_file_exists(fail)) {
          ctxt_sprite_state[i] = 2;
          changed = 1;
        } else if (!ctxt_file_exists(job) && !ctxt_file_exists(done)) {
          ctxt_schedule_remote_image(i);
        }
      }
    }
    i = i + 1;
  }
  return changed;
}

void ctxt_draw_sprite(int idx, int x, int y, int w, int h) {
  if (idx < 0 || idx >= ctxt_sprite_count || w <= 0 || h <= 0) return;

  ctxt_sprite_ensure_loaded(idx);
  if (ctxt_sprite_state[idx] == 1 && ctxt_sprite_handle[idx] >= 0) {
    gfx2d_image_draw_scaled(ctxt_sprite_handle[idx], x, y, w, h);
    gfx2d_rect(x, y, w, h, ctxt_col_rule);
    return;
  }

  gfx2d_rect(x, y, w, h, ctxt_col_rule);
  if (ctxt_sprite_remote[idx]) {
    if (ctxt_sprite_state[idx] == 0 || ctxt_sprite_state[idx] == 3) {
      gfx2d_text(x + 4, y + 4, "(loading image)", ctxt_col_rule, 1);
    } else {
      gfx2d_text(x + 4, y + 4, "(image failed)", ctxt_col_rule, 1);
    }
  } else {
    gfx2d_text(x + 4, y + 4, "(missing image)", ctxt_col_rule, 1);
  }
}

void ctxt_render(int x, int y, int w, int h, int sy, int sx) {
  gfx2d_rect_fill(x, y, w, h, ctxt_col_bg);
  int x2 = x + w;
  int y2 = y + h;
  int py = y - sy;
  ctxt_link_count = 0;
  int panel_bg = ctxt_theme_light ? 0x00D8D4CC : 0x003A4152;
  int panel_text = ctxt_theme_light ? ctxt_col_body : ctxt_col_box_text;
  int code_button_bg = ctxt_theme_light ? 0x00D9E7F8 : 0x00303D57;
  int code_button_text = ctxt_col_code_text;

  int i = 0;
  while (i < ctxt_line_count) {
    int type = ctxt_line_type[i];
    int lh = ctxt_line_h(type);

    if (ctxt_line_visible(i) && type != CTXT_COMMENT) {
      if (py + lh >= y && py <= y2) {
        int fg = ctxt_col_body;
        if (type == CTXT_H1) fg = ctxt_col_h1;
        if (type == CTXT_H2) fg = ctxt_col_h2;
        if (type == CTXT_H3) fg = ctxt_col_h3;

        if (ctxt_line_bg_color[i]) {
          gfx2d_rect_fill(x, py, w, lh, ctxt_line_bg_color[i]);
          gfx2d_vline(x, py, lh, ctxt_col_rule);
          fg = ctxt_col_box_text;
        }

        if (type == CTXT_RULE) {
          gfx2d_hline(x + 2, py + (lh / 2), w - 4, ctxt_col_rule);
        } else if (type == CTXT_BUTTON) {
          int bw = gfx2d_text_width(ctxt_line_text[i], 1) + 20;
          gfx2d_panel(x + 2, py, bw, lh - 2, panel_bg);
          gfx2d_text(x + 10, py + 4, ctxt_line_text[i], panel_text, 1);
          if (ctxt_line_ref[i] >= 0 && ctxt_line_ref[i] < ctxt_action_count) {
            ctxt_add_link_rect(x + 2, py, bw, lh - 2,
                               ctxt_action_type[ctxt_line_ref[i]],
                               ctxt_line_ref[i],
                               ctxt_action_payload[ctxt_line_ref[i]]);
          }
        } else if (type == CTXT_TREE) {
          char label[128];
          int bw;
          label[0] = ctxt_tree_open[ctxt_line_ref[i]] ? '-' : '+';
          label[1] = ' ';
          label[2] = 0;
          ctxt_strcat_n(label, ctxt_line_text[i], 128);
          bw = gfx2d_text_width(label, 1) + 12;
          gfx2d_panel(x + 2, py, bw, lh - 1, panel_bg);
          gfx2d_text(x + 8, py + 3, label, panel_text, 1);
          ctxt_add_link_rect(x + 2, py, bw, lh - 1, CTXT_ACT_TREE,
                             ctxt_line_ref[i], label);
        } else if (type == CTXT_CODE_HEADER) {
          int bw = 58;
          gfx2d_rect_fill(x + 2, py, w - 4, lh, ctxt_col_code_bg);
          gfx2d_rect(x + 2, py, w - 4, lh, ctxt_col_rule);
          gfx2d_text(x + 8, py + 4, ctxt_line_text[i], ctxt_col_code_text, 1);
          gfx2d_panel(x + w - bw - 6, py + 1, bw, lh - 2, code_button_bg);
          gfx2d_text(x + w - bw + 10, py + 4, "Run", code_button_text, 1);
          if (ctxt_line_ref[i] >= 0 && ctxt_line_ref[i] < ctxt_action_count) {
            ctxt_add_link_rect(x + 2, py, w - 4, lh,
                               CTXT_ACT_REPL, ctxt_line_ref[i],
                               ctxt_action_payload[ctxt_line_ref[i]]);
            ctxt_add_link_rect(x + w - bw - 6, py + 1, bw, lh - 2,
                               CTXT_ACT_REPL, ctxt_line_ref[i],
                               ctxt_action_payload[ctxt_line_ref[i]]);
          }
        } else if (type == CTXT_CODE) {
          gfx2d_rect_fill(x + 2, py, w - 4, lh, ctxt_col_code_bg);
          gfx2d_rect(x + 2, py, w - 4, lh, ctxt_col_rule);
          gfx2d_text(x + 8, py + 2, ctxt_line_text[i], ctxt_col_code_text, 1);
          if (ctxt_line_ref[i] >= 0 && ctxt_line_ref[i] < ctxt_action_count) {
            ctxt_add_link_rect(x + 2, py, w - 4, lh, CTXT_ACT_REPL,
                               ctxt_line_ref[i],
                               ctxt_action_payload[ctxt_line_ref[i]]);
          }
        } else if (type == CTXT_SPRITE) {
          if (ctxt_line_ref[i] >= 0 && ctxt_line_ref[i] < ctxt_sprite_count) {
            int spr = ctxt_line_ref[i];
            ctxt_sprite_ensure_loaded(spr);
            if (!ctxt_sprite_explicit_size[spr] &&
                ctxt_sprite_state[spr] == 1) {
              ctxt_line_aux_a[i] = ctxt_sprite_w[spr];
              ctxt_line_aux_b[i] = ctxt_sprite_h[spr];
            }
          }
          gfx2d_rect_fill(x + 2, py, ctxt_line_aux_a[i] + 8, ctxt_line_aux_b[i] + 8,
                          ctxt_col_code_bg);
          if (ctxt_line_ref[i] >= 0 && ctxt_line_ref[i] < ctxt_sprite_count) {
            ctxt_draw_sprite(ctxt_line_ref[i], x + 6, py + 4,
                             ctxt_line_aux_a[i], ctxt_line_aux_b[i]);
            if (ctxt_sprite_action[ctxt_line_ref[i]] >= 0 &&
                ctxt_sprite_action[ctxt_line_ref[i]] < ctxt_action_count) {
              int action_ref = ctxt_sprite_action[ctxt_line_ref[i]];
              ctxt_add_link_rect(x + 2, py, ctxt_line_aux_a[i] + 8,
                                 ctxt_line_aux_b[i] + 8,
                                 ctxt_action_type[action_ref], action_ref,
                                 ctxt_action_payload[action_ref]);
            }
          }
        } else {
          int scale = ctxt_line_scale(type);
          int text_x = x + 2 - sx;
          if (type == CTXT_CENTER) {
            int tw = gfx2d_text_width(ctxt_line_text[i], scale);
            text_x = x + ((w - tw) / 2) - sx;
          }
          ctxt_draw_text_links(text_x, py, x, x2, y, y2, lh, scale, fg,
                               ctxt_line_text[i]);
        }
      }
      py = py + lh;
      if (type == CTXT_SPRITE) py = py + ctxt_line_aux_b[i];
    }
    i = i + 1;
  }

  ctxt_refresh_metrics();
}

int ctxt_link_at(int mx, int my, int sy, int sx) {
  (void)sy;
  (void)sx;
  int i = 0;
  while (i < ctxt_link_count) {
    if (mx >= ctxt_link_x[i] && mx < ctxt_link_x[i] + ctxt_link_w[i] &&
        my >= ctxt_link_y[i] && my < ctxt_link_y[i] + ctxt_link_h[i]) {
      return i;
    }
    i = i + 1;
  }
  return -1;
}

void ctxt_get_link(int idx, int out_ptr, int size) {
  if (idx < 0 || idx >= ctxt_link_count) return;
  char *out = out_ptr;
  ctxt_strcpy_n(out, ctxt_link_target[idx], size);
}

int ctxt_get_link_action(int idx) {
  if (idx < 0 || idx >= ctxt_link_count) return -1;
  return ctxt_link_action[idx];
}

int ctxt_get_link_ref(int idx) {
  if (idx < 0 || idx >= ctxt_link_count) return -1;
  return ctxt_link_ref[idx];
}

void ctxt_toggle_tree(int idx) {
  if (idx < 0 || idx >= ctxt_tree_count) return;
  ctxt_tree_open[idx] = 1 - ctxt_tree_open[idx];
  ctxt_refresh_metrics();
}
