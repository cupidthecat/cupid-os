/* ---------- Utility ---------- */

int b_strlen(char *s) {
    int n = 0;
    while (s[n]) n = n + 1;
    return n;
}

int b_streq(char *a, char *b) {
    int i = 0;
    while (a[i] && a[i] == b[i]) i = i + 1;
    return a[i] == b[i];
}

int b_lc(int c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

int b_strieq(char *a, char *b) {
    int i = 0;
    while (a[i] && b[i] && b_lc(a[i]) == b_lc(b[i])) i = i + 1;
    return b_lc(a[i]) == b_lc(b[i]);
}

int b_streq_n(char *a, char *b, int n) {
    int i = 0;
    while (i < n && a[i] && a[i] == b[i]) i = i + 1;
    return i == n;
}

int b_strieq_n(char *a, char *b, int n) {
    int i = 0;
    while (i < n) {
        int la = b_lc(a[i]);
        int lb = b_lc(b[i]);
        if (la != lb) return 0;
        i = i + 1;
    }
    return 1;
}

void b_strcpy_n(char *dst, char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i = i + 1; }
    dst[i] = 0;
}

int b_strchr(char *s, int c) {
    int i = 0;
    while (s[i]) {
        if (s[i] == c) return i;
        i = i + 1;
    }
    return -1;
}

int b_append(char *dst, int p, char *src) {
    int i = 0;
    while (src[i]) { dst[p] = src[i]; p = p + 1; i = i + 1; }
    return p;
}

int b_append_n(char *dst, int p, char *src, int n) {
    int i = 0;
    while (i < n) { dst[p] = src[i]; p = p + 1; i = i + 1; }
    return p;
}

int b_append_int(char *dst, int p, int v) {
    char buf[16];
    int n = 0;
    if (v == 0) { dst[p] = '0'; return p + 1; }
    if (v < 0)  { dst[p] = '-'; p = p + 1; v = -v; }
    while (v > 0) { buf[n] = '0' + (v % 10); n = n + 1; v = v / 10; }
    while (n > 0) { n = n - 1; dst[p] = buf[n]; p = p + 1; }
    return p;
}

int hex_digit(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
