#include "ctxt_image_worker.h"

#include "dns.h"
#include "memory.h"
#include "process.h"
#include "socket.h"
#include "string.h"
#include "timer.h"
#include "types.h"
#include "vfs.h"
#include "vfs_helpers.h"

#define CTXT_IMG_MAX (4u * 1024u * 1024u)
#define CTXT_IMG_RAW_EXTRA 8192u
#define CTXT_IMG_WORKER_STACK (256u * 1024u)

static volatile uint8_t ctxt_img_worker_running = 0u;

static int ctxt_img_ci_starts(const char *str, const char *prefix) {
  uint32_t i;

  i = 0u;
  while (prefix[i] != '\0') {
    char a;
    char b;

    a = str[i];
    b = prefix[i];
    if (a >= 'A' && a <= 'Z') {
      a = (char)(a - 'A' + 'a');
    }
    if (b >= 'A' && b <= 'Z') {
      b = (char)(b - 'A' + 'a');
    }
    if (a != b) {
      return 0;
    }
    i++;
  }
  return 1;
}

static int ctxt_img_streq(const char *a, const char *b) {
  uint32_t i;

  i = 0u;
  while (a[i] != '\0' && a[i] == b[i]) {
    i++;
  }
  return a[i] == b[i];
}

static int ctxt_img_ends_with(const char *str, const char *suffix) {
  uint32_t sl;
  uint32_t xl;

  sl = (uint32_t)strlen(str);
  xl = (uint32_t)strlen(suffix);
  if (xl > sl) {
    return 0;
  }
  return ctxt_img_streq(str + (sl - xl), suffix);
}

static void ctxt_img_cpy(char *dst, const char *src, uint32_t max) {
  uint32_t i;

  if (max == 0u) {
    return;
  }
  i = 0u;
  while (src[i] != '\0' && i < (max - 1u)) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
}

static void ctxt_img_cat(char *dst, const char *src, uint32_t max) {
  uint32_t d;
  uint32_t s;

  if (max == 0u) {
    return;
  }
  d = 0u;
  s = 0u;
  while (dst[d] != '\0' && d < (max - 1u)) {
    d++;
  }
  while (src[s] != '\0' && d < (max - 1u)) {
    dst[d] = src[s];
    d++;
    s++;
  }
  dst[d] = '\0';
}

static void ctxt_img_cat_u32(char *dst, uint32_t value, uint32_t max) {
  char tmp[12];
  char rev[12];
  uint32_t n;
  uint32_t i;

  if (value == 0u) {
    ctxt_img_cat(dst, "0", max);
    return;
  }

  n = 0u;
  while (value > 0u && n < (uint32_t)sizeof(rev)) {
    rev[n] = (char)('0' + (char)(value % 10u));
    n++;
    value /= 10u;
  }
  for (i = 0u; i < n; i++) {
    tmp[i] = rev[n - 1u - i];
  }
  tmp[n] = '\0';
  ctxt_img_cat(dst, tmp, max);
}

static int ctxt_img_file_exists(const char *path) {
  vfs_stat_t st;

  return vfs_stat(path, &st) >= 0;
}

static int ctxt_img_parse_url(const char *url, char *host, uint16_t *port_out,
                              char *path, bool *is_https_out) {
  uint32_t i;
  uint32_t hi;
  uint32_t pi;

  i = 0u;
  hi = 0u;
  pi = 0u;
  *is_https_out = false;
  if (ctxt_img_ci_starts(url, "http://")) {
    i = 7u;
  } else if (ctxt_img_ci_starts(url, "https://")) {
    i = 8u;
    *is_https_out = true;
  } else {
    return -1;
  }

  *port_out = *is_https_out ? 443u : 80u;
  while (url[i] != '\0' && url[i] != ':' && url[i] != '/' && hi < 255u) {
    host[hi] = url[i];
    hi++;
    i++;
  }
  host[hi] = '\0';
  if (hi == 0u) {
    return -1;
  }

  if (url[i] == ':') {
    uint32_t value;

    value = 0u;
    i++;
    while (url[i] >= '0' && url[i] <= '9') {
      value = (value * 10u) + ((uint32_t)(uint8_t)url[i] - (uint32_t)'0');
      if (value > 65535u) {
        return -1;
      }
      i++;
    }
    if (value == 0u) {
      return -1;
    }
    *port_out = (uint16_t)value;
  }

  if (url[i] != '/') {
    path[pi] = '/';
    pi++;
  }
  while (url[i] != '\0' && pi < 1023u) {
    path[pi] = url[i];
    pi++;
    i++;
  }
  path[pi] = '\0';
  return 0;
}

static int ctxt_img_resolve_redirect(const char *location, const char *host,
                                     uint16_t port, bool is_https, char *out,
                                     uint32_t max) {
  uint16_t default_port;

  if (ctxt_img_ci_starts(location, "http://") ||
      ctxt_img_ci_starts(location, "https://")) {
    ctxt_img_cpy(out, location, max);
    return 0;
  }
  if (location[0] != '/') {
    return -1;
  }

  out[0] = '\0';
  ctxt_img_cat(out, is_https ? "https://" : "http://", max);
  ctxt_img_cat(out, host, max);
  default_port = is_https ? 443u : 80u;
  if (port != default_port) {
    ctxt_img_cat(out, ":", max);
    ctxt_img_cat_u32(out, (uint32_t)port, max);
  }
  ctxt_img_cat(out, location, max);
  return 0;
}

static uint32_t ctxt_img_decode_chunked(const char *raw, uint32_t body_start,
                                        uint32_t raw_len, char *out,
                                        uint32_t max) {
  uint32_t i;
  uint32_t out_len;

  i = body_start;
  out_len = 0u;
  while (i < raw_len) {
    uint32_t chunk_size;

    chunk_size = 0u;
    while (i < raw_len) {
      int dig;
      char ch;

      ch = raw[i];
      dig = -1;
      if (ch >= '0' && ch <= '9') {
        dig = (int)((uint32_t)(uint8_t)ch - (uint32_t)'0');
      } else if (ch >= 'a' && ch <= 'f') {
        dig = (int)(10u + ((uint32_t)(uint8_t)ch - (uint32_t)'a'));
      } else if (ch >= 'A' && ch <= 'F') {
        dig = (int)(10u + ((uint32_t)(uint8_t)ch - (uint32_t)'A'));
      } else {
      }
      if (dig < 0) {
        break;
      }
      chunk_size = (chunk_size * 16u) + (uint32_t)dig;
      i++;
    }
    while (i < raw_len && raw[i] != '\n') {
      i++;
    }
    if (i < raw_len && raw[i] == '\n') {
      i++;
    }
    if (chunk_size == 0u) {
      return out_len;
    }
    if ((raw_len - i) < chunk_size) {
      chunk_size = raw_len - i;
    }
    if (out_len < max) {
      uint32_t can;

      can = max - out_len;
      if (chunk_size < can) {
        can = chunk_size;
      }
      if (can > 0u) {
        memcpy(out + out_len, raw + i, can);
      }
      out_len += can;
    }
    i += chunk_size;
    while (i < raw_len && (raw[i] == '\r' || raw[i] == '\n')) {
      i++;
    }
    if (out_len >= max) {
      return out_len;
    }
  }
  return out_len;
}

static int ctxt_img_fetch_url_to_file(const char *url_in,
                                      const char *out_path) {
  char work_url[1024];
  uint32_t redirects;

  ctxt_img_cpy(work_url, url_in, (uint32_t)sizeof(work_url));

  for (redirects = 0u; redirects < 5u; redirects++) {
    char host[256];
    char path[1024];
    char req[2048];
    char tmp[4096];
    uint16_t port;
    bool is_https;
    uint32_t ip;
    uint32_t raw_cap;
    uint32_t raw_len;
    char *raw;
    int fd;
    int header_found;
    uint32_t header_end;
    uint32_t status;
    uint32_t si;
    int chunked;
    char location[1024];
    uint32_t line_start;

    port = 80u;
    is_https = false;
    ip = 0u;
    if (ctxt_img_parse_url(work_url, host, &port, path, &is_https) != 0) {
      return -1;
    }
    if (dns_resolve(host, &ip) != 0) {
      return -1;
    }

    fd = socket_create(SOCK_TYPE_TCP);
    if (fd < 0) {
      return -1;
    }
    if (socket_connect(fd, ip, htons(port)) != 0) {
      (void)socket_close(fd);
      return -1;
    }
    if (is_https) {
      uint32_t hlen;

      hlen = (uint32_t)strlen(host);
      if (socket_setsockopt(fd, SOL_TLS, TLS_ENABLE, host, hlen + 1u) != 0) {
        (void)socket_close(fd);
        return -1;
      }
    }

    req[0] = '\0';
    ctxt_img_cat(req, "GET ", (uint32_t)sizeof(req));
    ctxt_img_cat(req, path, (uint32_t)sizeof(req));
    ctxt_img_cat(req, " HTTP/1.1\r\nHost: ", (uint32_t)sizeof(req));
    ctxt_img_cat(req, host, (uint32_t)sizeof(req));
    ctxt_img_cat(req,
                 "\r\nUser-Agent: cupidos-ctxtimgd/1.0\r\n"
                 "Accept: image/*,*/*\r\n"
                 "Accept-Encoding: identity\r\n"
                 "Connection: close\r\n\r\n",
                 (uint32_t)sizeof(req));
    {
      uint32_t req_len;
      int sent;

      req_len = (uint32_t)strlen(req);
      sent = socket_send(fd, req, req_len);
      if (sent != (int)req_len) {
        (void)socket_close(fd);
        return -1;
      }
    }

    raw_cap = CTXT_IMG_MAX + CTXT_IMG_RAW_EXTRA;
    raw = kmalloc(raw_cap);
    if (!raw) {
      (void)socket_close(fd);
      return -1;
    }
    raw_len = 0u;
    while (raw_len < raw_cap) {
      int n;
      uint32_t got;
      uint32_t can;

      n = socket_recv(fd, tmp, (uint32_t)sizeof(tmp));
      if (n <= 0) {
        break;
      }
      got = (uint32_t)n;
      can = raw_cap - raw_len;
      if (got < can) {
        can = got;
      }
      memcpy(raw + raw_len, tmp, can);
      raw_len += can;
      if (can < got) {
        break;
      }
    }
    (void)socket_close(fd);
    if (raw_len >= raw_cap) {
      kfree(raw);
      return -1;
    }

    header_found = 0;
    header_end = 0u;
    if (raw_len >= 4u) {
      uint32_t scan_pos;

      for (scan_pos = 0u; scan_pos + 3u < raw_len; scan_pos++) {
        if (raw[scan_pos] == '\r' && raw[scan_pos + 1u] == '\n' &&
            raw[scan_pos + 2u] == '\r' &&
            raw[scan_pos + 3u] == '\n') {
          header_end = scan_pos + 4u;
          header_found = 1;
          break;
        }
      }
    }
    if (!header_found) {
      kfree(raw);
      return -1;
    }

    status = 0u;
    si = 0u;
    while (si < raw_len && raw[si] != ' ') {
      si++;
    }
    while (si < raw_len && raw[si] == ' ') {
      si++;
    }
    while (si < raw_len && raw[si] >= '0' && raw[si] <= '9') {
      status = (status * 10u) +
               ((uint32_t)(uint8_t)raw[si] - (uint32_t)'0');
      si++;
    }

    chunked = 0;
    location[0] = '\0';
    line_start = 0u;
    while (line_start < header_end) {
      uint32_t line_end;
      uint32_t len;
      uint32_t copy_len;
      char line[1024];

      line_end = line_start;
      while (line_end < header_end && raw[line_end] != '\n') {
        line_end++;
      }
      len = line_end - line_start;
      while (len > 0u &&
             (raw[line_start + len - 1u] == '\r' ||
              raw[line_start + len - 1u] == '\n')) {
        len--;
      }
      copy_len = len;
      if (copy_len > ((uint32_t)sizeof(line) - 1u)) {
        copy_len = (uint32_t)sizeof(line) - 1u;
      }
      memcpy(line, raw + line_start, copy_len);
      line[copy_len] = '\0';

      if (ctxt_img_ci_starts(line, "transfer-encoding:")) {
        uint32_t p;

        p = 18u;
        while (line[p] == ' ' || line[p] == '\t') {
          p++;
        }
        if (ctxt_img_ci_starts(line + p, "chunked")) {
          chunked = 1;
        }
      } else if (ctxt_img_ci_starts(line, "location:")) {
        uint32_t p;
        uint32_t o;

        p = 9u;
        o = 0u;
        while (line[p] == ' ' || line[p] == '\t') {
          p++;
        }
        while (line[p] != '\0' && o < ((uint32_t)sizeof(location) - 1u)) {
          location[o] = line[p];
          o++;
          p++;
        }
        location[o] = '\0';
      } else {
      }
      line_start = line_end + 1u;
    }

    if (status >= 300u && status < 400u && location[0] != '\0') {
      char next_url[1024];
      int rr;

      rr = ctxt_img_resolve_redirect(location, host, port, is_https, next_url,
                                     (uint32_t)sizeof(next_url));
      kfree(raw);
      if (rr != 0) {
        return -1;
      }
      ctxt_img_cpy(work_url, next_url, (uint32_t)sizeof(work_url));
      continue;
    }

    if (status < 200u || status >= 300u) {
      kfree(raw);
      return -1;
    }

    {
      uint32_t body_len;
      int rc;

      body_len = raw_len - header_end;
      rc = -1;
      if (chunked) {
        char *body;

        body = kmalloc(CTXT_IMG_MAX);
        if (body) {
          uint32_t decoded_len;

          decoded_len =
              ctxt_img_decode_chunked(raw, header_end, raw_len, body,
                                      CTXT_IMG_MAX);
          if (decoded_len > 0u &&
              vfs_write_all(out_path, body, decoded_len) == (int)decoded_len) {
            rc = 0;
          }
          kfree(body);
        }
      } else if (body_len > 0u && body_len <= CTXT_IMG_MAX) {
        if (vfs_write_all(out_path, raw + header_end, body_len) ==
            (int)body_len) {
          rc = 0;
        }
      } else {
      }
      kfree(raw);
      return rc;
    }
  }
  return -1;
}

static uint32_t ctxt_img_read_line(const char *src, uint32_t *pos, char *out,
                                   uint32_t max) {
  uint32_t p;
  uint32_t o;

  p = *pos;
  o = 0u;
  if (max == 0u) {
    return 0u;
  }
  while (src[p] != '\0' && src[p] != '\n' && src[p] != '\r' &&
         o < (max - 1u)) {
    out[o] = src[p];
    o++;
    p++;
  }
  out[o] = '\0';
  while (src[p] == '\n' || src[p] == '\r') {
    p++;
  }
  *pos = p;
  return o;
}

static void ctxt_img_process_job(const char *job_path) {
  char job[2400];
  char url[1024];
  char out_path[256];
  char done_path[256];
  char fail_path[256];
  uint32_t pos;
  int n;

  n = vfs_read_text(job_path, job, (uint32_t)sizeof(job));
  if (n <= 0) {
    (void)vfs_unlink(job_path);
    return;
  }

  pos = 0u;
  (void)ctxt_img_read_line(job, &pos, url, (uint32_t)sizeof(url));
  (void)ctxt_img_read_line(job, &pos, out_path, (uint32_t)sizeof(out_path));
  (void)ctxt_img_read_line(job, &pos, done_path, (uint32_t)sizeof(done_path));
  (void)ctxt_img_read_line(job, &pos, fail_path, (uint32_t)sizeof(fail_path));
  if (url[0] == '\0' || out_path[0] == '\0' || done_path[0] == '\0' ||
      fail_path[0] == '\0') {
    (void)vfs_unlink(job_path);
    return;
  }

  if (ctxt_img_file_exists(done_path) && ctxt_img_file_exists(out_path)) {
    (void)vfs_unlink(job_path);
    return;
  }

  (void)vfs_unlink(fail_path);
  if (ctxt_img_fetch_url_to_file(url, out_path) == 0) {
    (void)vfs_write_text(done_path, "ok\n");
  } else {
    (void)vfs_write_text(fail_path, "fail\n");
  }
  (void)vfs_unlink(job_path);
}

static uint32_t ctxt_img_scan_once(void) {
  int fd;
  vfs_dirent_t ent;
  char jobs[16][128];
  uint32_t job_count;
  uint32_t j;

  (void)vfs_mkdir("/ctxt-cache");
  fd = vfs_open("/ctxt-cache", O_RDONLY);
  if (fd < 0) {
    return 0u;
  }

  job_count = 0u;
  while (vfs_readdir(fd, &ent) > 0 && job_count < 16u) {
    if (ctxt_img_ends_with(ent.name, ".job")) {
      ctxt_img_cpy(jobs[job_count], ent.name, (uint32_t)sizeof(jobs[0]));
      job_count++;
    }
  }
  (void)vfs_close(fd);

  for (j = 0u; j < job_count; j++) {
    char path[256];

    ctxt_img_cpy(path, "/ctxt-cache/", (uint32_t)sizeof(path));
    ctxt_img_cat(path, jobs[j], (uint32_t)sizeof(path));
    ctxt_img_process_job(path);
  }
  return job_count;
}

static void ctxt_img_sleep_ms(uint32_t ms) {
  uint32_t start;

  start = timer_get_uptime_ms();
  while ((timer_get_uptime_ms() - start) < ms) {
    process_yield();
  }
}

static void ctxt_img_worker_entry(void) {
  uint32_t idle;

  idle = 0u;
  while (idle < 20u) {
    uint32_t jobs;

    jobs = ctxt_img_scan_once();
    if (jobs > 0u) {
      idle = 0u;
    } else {
      idle++;
    }
    ctxt_img_sleep_ms(250u);
  }
  ctxt_img_worker_running = 0u;
  process_exit();
}

void ctxt_image_worker_start(void) {
  uint32_t pid;

  if (ctxt_img_worker_running) {
    return;
  }
  ctxt_img_worker_running = 1u;
  pid = process_create_ex(ctxt_img_worker_entry, "ctxtimgd",
                          CTXT_IMG_WORKER_STACK, PROCESS_DOMAIN_KERNEL);
  if (pid == 0u) {
    ctxt_img_worker_running = 0u;
  }
}
