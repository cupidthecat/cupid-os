/*
 * dglibc.c - libc shim bridging DOOM calls to CupidOS kernel APIs
 *
 * Maps malloc/free, fopen/fread/fseek/ftell, snprintf/sprintf/printf,
 * setjmp/longjmp, ctype, strcasecmp, qsort, time, getenv to existing
 * CupidOS APIs.
 *
 * Built with CFLAGS_DOOM (relaxed flags).
*/

#include "dglibc.h"
#include "memory.h"
#include "string.h"
#include "vfs.h"
#include "serial.h"
#include "timer.h"

/* Use GCC built-in va_list - stdarg.h not available under -nostdinc */
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type)   __builtin_va_arg(ap, type)
#define va_end(ap)         __builtin_va_end(ap)

/* Internal DG_FILE struct */

#define DG_FILE_MAGIC 0xD600F11E

struct DG_FILE {
    uint32_t magic;
    int      fd;       /* vfs fd, or -1 for stdin/stdout/stderr */
    int      is_eof;
    int      is_err;
    int32_t  pos;      /* tracked position for stdin/stdout/stderr */
};

/* Static sentinel file objects for stdin/stdout/stderr */
static struct DG_FILE s_stdin_obj  = { DG_FILE_MAGIC, -1, 0, 0, 0 };
static struct DG_FILE s_stdout_obj = { DG_FILE_MAGIC, -2, 0, 0, 0 };
static struct DG_FILE s_stderr_obj = { DG_FILE_MAGIC, -3, 0, 0, 0 };

DG_FILE *dg_stdin  = &s_stdin_obj;
DG_FILE *dg_stdout = &s_stdout_obj;
DG_FILE *dg_stderr = &s_stderr_obj;

/* Exit envelope */

static dg_jmp_buf s_exit_env;
static int        s_exit_armed = 0;

void dg_arm_exit(dg_jmp_buf env) {
    int i;
    for (i = 0; i < 6; i++) {
        s_exit_env[i] = env[i];
    }
    s_exit_armed = 1;
}

/*setjmp / longjmp - x86-32, AT&T inline asm
 * Saves/restores: ebx esi edi ebp esp eip (6 dwords at indices 0-5)*/

__asm__(
    ".global dg_setjmp\n"
    "dg_setjmp:\n"
    "    movl  4(%esp), %eax\n"      /* eax = &env[0] */
    "    movl  %ebx,  0(%eax)\n"
    "    movl  %esi,  4(%eax)\n"
    "    movl  %edi,  8(%eax)\n"
    "    movl  %ebp, 12(%eax)\n"
    "    movl  %esp, 16(%eax)\n"
    "    movl  (%esp), %ecx\n"       /* ecx = return address */
    "    movl  %ecx, 20(%eax)\n"
    "    xorl  %eax, %eax\n"         /* return 0 */
    "    ret\n"

    ".global dg_longjmp\n"
    "dg_longjmp:\n"
    "    movl  4(%esp), %eax\n"      /* eax = &env[0] */
    "    movl  8(%esp), %ecx\n"      /* ecx = val */
    "    testl %ecx, %ecx\n"
    "    jnz   1f\n"
    "    movl  $1, %ecx\n"           /* val=0 becomes 1 (POSIX) */
    "1:\n"
    "    movl  0(%eax), %ebx\n"
    "    movl  4(%eax), %esi\n"
    "    movl  8(%eax), %edi\n"
    "    movl 12(%eax), %ebp\n"
    "    movl 16(%eax), %esp\n"
    "    movl 20(%eax), %edx\n"      /* saved eip */
    "    movl  %ecx,  %eax\n"        /* return val in eax */
    "    jmp  *%edx\n"
);

/* Heap */

/* dg_malloc'd blocks carry an 8-byte size prefix so dg_realloc
 * can copy exactly the old payload without over-reading past the
 * allocation boundary (which would risk heap corruption). The
 * prefix is invisible to callers - dg_malloc returns the pointer
 * AFTER the header, dg_free walks back to the header.
 *
 * IMPORTANT: pointers passed to dg_free / dg_realloc must have
 * come from dg_malloc / dg_calloc / dg_strdup - not raw kmalloc.
*/
typedef struct {
    uint32_t size;
    uint32_t magic;   /* DG_ALLOC_MAGIC - sanity check on free/realloc */
} dg_alloc_hdr_t;

#define DG_ALLOC_MAGIC 0xDA110CADu

void *dg_malloc(uint32_t n) {
    dg_alloc_hdr_t *h;
    if (n == 0) { n = 1; }
    h = (dg_alloc_hdr_t *)kmalloc(sizeof(dg_alloc_hdr_t) + (size_t)n);
    if (!h) { return NULL; }
    h->size = n;
    h->magic = DG_ALLOC_MAGIC;
    return (void *)(h + 1);
}

void *dg_calloc(uint32_t n, uint32_t sz) {
    uint32_t total = n * sz;
    void *p;
    if (total == 0) { total = 1; }
    p = dg_malloc(total);
    if (p) {
        memset(p, 0, (size_t)total);
    }
    return p;
}

void *dg_realloc(void *p, uint32_t newsz) {
    dg_alloc_hdr_t *h;
    void *newp;
    uint32_t oldsz, copy;
    if (!p) { return dg_malloc(newsz); }
    if (newsz == 0) { dg_free(p); return NULL; }
    h = ((dg_alloc_hdr_t *)p) - 1;
    if (h->magic != DG_ALLOC_MAGIC) {
        /* Pointer wasn't from dg_malloc - fall back to old behaviour
         * but warn loudly. Should never happen in well-behaved DOOM.*/
        serial_write_string("[dglibc] realloc on foreign pointer!\n");
        newp = kmalloc((size_t)newsz);
        if (newp) { memcpy(newp, p, (size_t)newsz); kfree(p); }
        return newp;
    }
    oldsz = h->size;
    newp = dg_malloc(newsz);
    if (!newp) { return NULL; }
    copy = (oldsz < newsz) ? oldsz : newsz;
    memcpy(newp, p, (size_t)copy);
    dg_free(p);
    return newp;
}

void dg_free(void *p) {
    dg_alloc_hdr_t *h;
    if (!p) { return; }
    h = ((dg_alloc_hdr_t *)p) - 1;
    if (h->magic != DG_ALLOC_MAGIC) {
        /* Foreign pointer - kfree direct. Diagnostic. */
        serial_write_string("[dglibc] free on foreign pointer!\n");
        kfree(p);
        return;
    }
    h->magic = 0;   /* poison so double-free is caught */
    kfree(h);
}

char *dg_strdup(const char *s) {
    char *d;
    size_t len;
    if (!s) { return NULL; }
    len = strlen(s);
    d = (char *)dg_malloc((uint32_t)(len + 1));
    if (d) {
        memcpy(d, s, len + 1);
    }
    return d;
}

/* ctype */

int dg_isspace(int c) {
    return (c == ' ' || c == '\t' || c == '\n' ||
            c == '\r' || c == '\f' || c == '\v');
}

int dg_isdigit(int c) {
    return (c >= '0' && c <= '9');
}

int dg_isalpha(int c) {
    return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
}

int dg_isprint(int c) {
    return (c >= 0x20 && c <= 0x7E);
}

int dg_tolower(int c) {
    if (c >= 'A' && c <= 'Z') { return c + 32; }
    return c;
}

int dg_toupper(int c) {
    if (c >= 'a' && c <= 'z') { return c - 32; }
    return c;
}

/* String helpers */

int dg_strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = dg_tolower((unsigned char)*a);
        int cb = dg_tolower((unsigned char)*b);
        if (ca != cb) { return ca - cb; }
        a++;
        b++;
    }
    return dg_tolower((unsigned char)*a) - dg_tolower((unsigned char)*b);
}

int dg_strncasecmp(const char *a, const char *b, uint32_t n) {
    uint32_t i;
    for (i = 0; i < n && *a && *b; i++, a++, b++) {
        int ca = dg_tolower((unsigned char)*a);
        int cb = dg_tolower((unsigned char)*b);
        if (ca != cb) { return ca - cb; }
    }
    if (i == n) { return 0; }
    return dg_tolower((unsigned char)*a) - dg_tolower((unsigned char)*b);
}

/* env / time */

char *dg_getenv(const char *name) {
    (void)name;
    return NULL;  /* No environment in kernel */
}

uint32_t dg_time(void *t) {
    uint32_t secs = timer_get_uptime_ms() / 1000u;
    if (t) {
        *(uint32_t *)t = secs;
    }
    return secs;
}

/*printf core - format_into
 * Subset: %d %i %u %x %X %p %c %s %%
 * Width + zero-pad. No floats.*/

static int format_into(char *out, uint32_t cap, const char *fmt, va_list ap) {
    uint32_t pos = 0;

#define EMIT(ch) do { \
    if (pos + 1 < cap) { out[pos] = (char)(ch); } \
    pos++; \
} while (0)

    while (*fmt) {
        char ch = *fmt++;
        if (ch != '%') {
            EMIT(ch);
            continue;
        }

        /* Parse flags */
        int zero_pad = 0;
        int left_align = 0;
        ch = *fmt;
        while (ch == '0' || ch == '-' || ch == ' ' || ch == '+') {
            if (ch == '0') { zero_pad = 1; }
            if (ch == '-') { left_align = 1; }
            fmt++;
            ch = *fmt;
        }
        if (left_align) { zero_pad = 0; }

        /* Parse width */
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        /* Parse precision (ignored except for %s truncation) */
        int prec = -1;
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            while (*fmt >= '0' && *fmt <= '9') {
                prec = prec * 10 + (*fmt - '0');
                fmt++;
            }
        }

        /* Parse length modifier (ignore) */
        if (*fmt == 'l' || *fmt == 'h') { fmt++; }
        if (*fmt == 'l' || *fmt == 'h') { fmt++; }

        char spec = *fmt++;

        if (spec == '%') {
            EMIT('%');
            continue;
        }

        if (spec == 'c') {
            int c = va_arg(ap, int);
            /* Width padding */
            if (!left_align && width > 1) {
                int p; for (p = 0; p < width - 1; p++) { EMIT(' '); }
            }
            EMIT(c);
            if (left_align && width > 1) {
                int p; for (p = 0; p < width - 1; p++) { EMIT(' '); }
            }
            continue;
        }

        if (spec == 's') {
            const char *s = va_arg(ap, const char *);
            if (!s) { s = "(null)"; }
            int slen = 0;
            const char *sp = s;
            while (*sp) { slen++; sp++; }
            if (prec >= 0 && slen > prec) { slen = prec; }
            int pad = (width > slen) ? (width - slen) : 0;
            if (!left_align) {
                int p; for (p = 0; p < pad; p++) { EMIT(' '); }
            }
            int k; for (k = 0; k < slen; k++) { EMIT(s[k]); }
            if (left_align) {
                int p; for (p = 0; p < pad; p++) { EMIT(' '); }
            }
            continue;
        }

        /* Integer specifiers */
        if (spec == 'd' || spec == 'i' || spec == 'u' ||
            spec == 'x' || spec == 'X' || spec == 'p') {

            char buf[32];
            int  blen = 0;
            int  neg  = 0;
            uint32_t uval;

            if (spec == 'p') {
                uval = (uint32_t)(size_t)va_arg(ap, void *);
                spec = 'x';
            } else if (spec == 'd' || spec == 'i') {
                int ival = va_arg(ap, int);
                if (ival < 0) { neg = 1; uval = (uint32_t)(-ival); }
                else          { uval = (uint32_t)ival; }
            } else {
                uval = va_arg(ap, uint32_t);
            }

            if (spec == 'x' || spec == 'X') {
                const char *hex = (spec == 'x') ? "0123456789abcdef"
                                                 : "0123456789ABCDEF";
                do {
                    buf[blen++] = hex[uval & 0xFu];
                    uval >>= 4;
                } while (uval);
            } else {
                do {
                    buf[blen++] = (char)('0' + (int)(uval % 10u));
                    uval /= 10u;
                } while (uval);
            }

            /* Reverse digit string */
            int lo = 0, hi = blen - 1;
            while (lo < hi) {
                char tmp = buf[lo]; buf[lo] = buf[hi]; buf[hi] = tmp;
                lo++; hi--;
            }

            /* Precision for integers = minimum digit count, zero-padded
             * on the left.  Suppresses ' ' / '0' width-flag padding when
             * precision is specified (printf semantics).*/
            int prec_pad = 0;
            if (prec >= 0 && prec > blen) {
                prec_pad = prec - blen;
            }

            int total = blen + prec_pad + (neg ? 1 : 0);
            int pad = (width > total) ? (width - total) : 0;

            if (!left_align) {
                if (zero_pad && prec < 0) {
                    if (neg) { EMIT('-'); }
                    int p; for (p = 0; p < pad; p++) { EMIT('0'); }
                } else {
                    int p; for (p = 0; p < pad; p++) { EMIT(' '); }
                    if (neg) { EMIT('-'); }
                }
            } else {
                if (neg) { EMIT('-'); }
            }

            { int p; for (p = 0; p < prec_pad; p++) { EMIT('0'); } }
            int k; for (k = 0; k < blen; k++) { EMIT(buf[k]); }

            if (left_align) {
                int p; for (p = 0; p < pad; p++) { EMIT(' '); }
            }
            continue;
        }

        /* Unknown specifier - emit as-is */
        EMIT('%');
        EMIT(spec);
    }

    /* NUL-terminate */
    if (cap > 0) {
        if (pos < cap) { out[pos] = '\0'; }
        else           { out[cap - 1] = '\0'; }
    }

    return (int)pos;

#undef EMIT
}

/* printf family */

int dg_vsnprintf(char *s, uint32_t n, const char *fmt, void *va) {
    if (!s || n == 0) { return 0; }
    /* On x86-32 sysv ABI, va_list is char* - same size as void*. The
     * caller passes the va_list value directly via a void* parameter,
     * so cast it back rather than dereferencing.*/
    return format_into(s, n, fmt, (va_list)va);
}

int dg_snprintf(char *s, uint32_t n, const char *fmt, ...) {
    int ret;
    va_list ap;
    va_start(ap, fmt);
    ret = format_into(s, n, fmt, ap);
    va_end(ap);
    return ret;
}

int dg_sprintf(char *s, const char *fmt, ...) {
    int ret;
    va_list ap;
    va_start(ap, fmt);
    /* Use large cap - caller must ensure buffer is big enough */
    ret = format_into(s, 65536u, fmt, ap);
    va_end(ap);
    return ret;
}

int dg_printf(const char *fmt, ...) {
    char buf[512];
    int ret;
    va_list ap;
    va_start(ap, fmt);
    ret = format_into(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    serial_write_string(buf);
    return ret;
}

int dg_fprintf(DG_FILE *f, const char *fmt, ...) {
    char buf[512];
    int ret;
    va_list ap;
    va_start(ap, fmt);
    ret = format_into(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (!f || f->fd == -2 || f->fd == -3 || f->fd == -1) {
        /* stdout / stderr / stdin -> serial */
        serial_write_string(buf);
    } else {
        vfs_write(f->fd, buf, (uint32_t)ret);
    }
    return ret;
}

/* stdio - file ops */

DG_FILE *dg_fopen(const char *path, const char *mode) {
    uint32_t flags = O_RDONLY;
    DG_FILE *f;
    int fd;
    char fixed[128];

    if (!path || !mode) { return NULL; }

    /*
     * Translate relative DOOM save/cfg paths to /home/doom/.
     * Fires when the path has no leading '/' (relative) and contains
     * "doomsav" (e.g. "./.savegame/doomsav0.dsg") or "default.cfg"
     * (e.g. ".default.cfg").  We extract the basename (component after
     * the last '/') and prepend "/home/doom/".
     * Absolute paths are passed through unchanged.
*/
    if (path[0] != '/' &&
        (strstr(path, "doomsav") || strstr(path, "default.cfg"))) {
        /* Find last '/' to get basename */
        const char *base = path;
        const char *p = path;
        while (*p) {
            if (*p == '/') { base = p + 1; }
            p++;
        }
        /* Build "/home/doom/" + base into fixed[] */
        const char *prefix = "/home/doom/";
        char *q = fixed;
        const char *r = prefix;
        while (*r) { *q++ = *r++; }
        r = base;
        while (*r && (uint32_t)(q - fixed) < sizeof(fixed) - 1u) {
            *q++ = *r++;
        }
        *q = '\0';
        path = fixed;
    }

    /* Decode mode string */
    if (mode[0] == 'r') {
        flags = O_RDONLY;
        if (mode[1] == '+') { flags = O_RDWR; }
    } else if (mode[0] == 'w') {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
        if (mode[1] == '+') { flags = O_RDWR | O_CREAT | O_TRUNC; }
    } else if (mode[0] == 'a') {
        flags = O_WRONLY | O_CREAT | O_APPEND;
        if (mode[1] == '+') { flags = O_RDWR | O_CREAT | O_APPEND; }
    }

    fd = vfs_open(path, flags);
    if (fd < 0) { return NULL; }

    f = (DG_FILE *)dg_malloc((uint32_t)sizeof(struct DG_FILE));
    if (!f) { vfs_close(fd); return NULL; }

    f->magic  = DG_FILE_MAGIC;
    f->fd     = fd;
    f->is_eof = 0;
    f->is_err = 0;
    f->pos    = 0;
    return f;
}

int dg_fclose(DG_FILE *f) {
    if (!f || f->magic != DG_FILE_MAGIC) { return -1; }
    if (f->fd >= 0) { vfs_close(f->fd); }
    f->magic = 0;
    dg_free(f);
    return 0;
}

uint32_t dg_fread(void *p, uint32_t sz, uint32_t n, DG_FILE *f) {
    int got;
    uint32_t total;
    if (!f || !p || sz == 0 || n == 0) { return 0; }
    if (f->fd < 0) { return 0; } /* stdin not supported */
    total = sz * n;
    got = vfs_read(f->fd, p, total);
    if (got <= 0) {
        f->is_eof = 1;
        return 0;
    }
    return (uint32_t)got / sz;
}

uint32_t dg_fwrite(const void *p, uint32_t sz, uint32_t n, DG_FILE *f) {
    uint32_t total;
    if (!f || !p || sz == 0 || n == 0) { return 0; }
    total = sz * n;
    if (f->fd == -2 || f->fd == -3) {
        /* stdout/stderr -> serial */
        const char *s = (const char *)p;
        uint32_t i;
        for (i = 0; i < total; i++) {
            serial_write_char(s[i]);
        }
        return n;
    }
    if (f->fd < 0) { return 0; }
    if (vfs_write(f->fd, p, total) < 0) {
        f->is_err = 1;
        return 0;
    }
    return n;
}

int dg_fseek(DG_FILE *f, int32_t off, int whence) {
    if (!f || f->magic != DG_FILE_MAGIC) { return -1; }
    if (f->fd < 0) { return -1; }
    if (vfs_seek(f->fd, off, whence) < 0) { return -1; }
    f->is_eof = 0;
    return 0;
}

int32_t dg_ftell(DG_FILE *f) {
    int pos;
    if (!f || f->magic != DG_FILE_MAGIC) { return -1; }
    if (f->fd < 0) { return -1; }
    pos = vfs_seek(f->fd, 0, SEEK_CUR);
    if (pos < 0) { return -1; }
    return (int32_t)pos;
}

int dg_feof(DG_FILE *f) {
    if (!f) { return 1; }
    return f->is_eof;
}

void dg_clearerr(DG_FILE *f) {
    if (!f) { return; }
    f->is_eof = 0;
    f->is_err = 0;
}

char *dg_fgets(char *s, int n, DG_FILE *f) {
    int i;
    if (!s || n <= 0 || !f) { return NULL; }
    for (i = 0; i < n - 1; i++) {
        int c = dg_fgetc(f);
        if (c < 0) {
            if (i == 0) { return NULL; }
            break;
        }
        s[i] = (char)c;
        if (c == '\n') { i++; break; }
    }
    s[i] = '\0';
    return s;
}

int dg_fgetc(DG_FILE *f) {
    unsigned char c;
    int got;
    if (!f || f->fd < 0) { return -1; }
    got = vfs_read(f->fd, &c, 1);
    if (got <= 0) { f->is_eof = 1; return -1; }
    return (int)c;
}

int dg_fputc(int c, DG_FILE *f) {
    char ch = (char)c;
    if (!f) { return -1; }
    if (f->fd == -2 || f->fd == -3) {
        serial_write_char(ch);
        return c;
    }
    if (f->fd < 0) { return -1; }
    if (vfs_write(f->fd, &ch, 1) < 0) { return -1; }
    return c;
}

/* exit / abort */

void dg_exit(int code) {
    (void)code;
    if (s_exit_armed) {
        s_exit_armed = 0;
        dg_longjmp(s_exit_env, (code == 0) ? 1 : code);
    }
    /* Halt loop if not armed */
    for (;;) {
        __asm__ volatile("hlt");
    }
}

void dg_abort(void) {
    dg_exit(1);
}

/* qsort - iterative quicksort with insertion sort fallback */

#define QS_INSERTION_THRESHOLD 16
#define QS_STACK_DEPTH 64

static void swap_bytes(char *a, char *b, uint32_t sz) {
    uint32_t i;
    for (i = 0; i < sz; i++) {
        char t = a[i];
        a[i] = b[i];
        b[i] = t;
    }
}

static void insertion_sort(char *base, uint32_t n, uint32_t sz,
                           int (*cmp)(const void *, const void *)) {
    uint32_t i, j;
    for (i = 1; i < n; i++) {
        for (j = i; j > 0 &&
             cmp(base + (j-1)*sz, base + j*sz) > 0; j--) {
            swap_bytes(base + (j-1)*sz, base + j*sz, sz);
        }
    }
}

void dg_qsort(void *base, uint32_t n, uint32_t sz,
              int (*cmp)(const void *, const void *)) {
    struct { uint32_t lo; uint32_t hi; } stack[QS_STACK_DEPTH];
    int top = 0;

    if (!base || n < 2 || sz == 0 || !cmp) { return; }

    stack[top].lo = 0;
    stack[top].hi = n - 1;
    top++;

    while (top > 0) {
        top--;
        uint32_t lo = stack[top].lo;
        uint32_t hi = stack[top].hi;

        if (hi <= lo) { continue; }
        uint32_t len = hi - lo + 1;

        if (len <= QS_INSERTION_THRESHOLD) {
            insertion_sort((char *)base + lo * sz, len, sz, cmp);
            continue;
        }

        /* Median-of-three pivot */
        uint32_t mid = lo + len / 2;
        char *p_lo  = (char *)base + lo  * sz;
        char *p_mid = (char *)base + mid * sz;
        char *p_hi  = (char *)base + hi  * sz;
        if (cmp(p_lo, p_mid) > 0) { swap_bytes(p_lo, p_mid, sz); }
        if (cmp(p_lo, p_hi)  > 0) { swap_bytes(p_lo, p_hi,  sz); }
        if (cmp(p_mid, p_hi) > 0) { swap_bytes(p_mid, p_hi, sz); }
        /* pivot is now at mid; move to hi-1 for partitioning */
        swap_bytes(p_mid, (char *)base + (hi - 1) * sz, sz);
        char *pivot = (char *)base + (hi - 1) * sz;

        uint32_t i = lo;
        uint32_t j = hi - 1;

        while (1) {
            while (++i < hi && cmp((char *)base + i * sz, pivot) < 0) {}
            while (j > lo && cmp((char *)base + --j * sz, pivot) > 0) {}
            if (i >= j) { break; }
            swap_bytes((char *)base + i * sz, (char *)base + j * sz, sz);
        }
        /* Restore pivot */
        swap_bytes((char *)base + i * sz, pivot, sz);

        /* Push larger partition last so smaller is processed first */
        if (top + 2 < QS_STACK_DEPTH) {
            if (i - lo > hi - i) {
                if (i > lo + 1) {
                    stack[top].lo = lo; stack[top].hi = i - 1; top++;
                }
                if (i + 1 < hi) {
                    stack[top].lo = i + 1; stack[top].hi = hi; top++;
                }
            } else {
                if (i + 1 < hi) {
                    stack[top].lo = i + 1; stack[top].hi = hi; top++;
                }
                if (i > lo + 1) {
                    stack[top].lo = lo; stack[top].hi = i - 1; top++;
                }
            }
        }
    }
}

/* Smoke test */

int dglibc_test_main(void) {
    /* snprintf */
    {
        char b[64];
        int n = dg_snprintf(b, sizeof(b), "hello %s %d %x", "world", 42, 0xCAFEu);
        (void)n;
        const char *expected = "hello world 42 cafe";
        const char *p = b;
        const char *q = expected;
        while (*q) {
            if (*p++ != *q++) { break; }
        }
        if (*q != 0 || *p != 0) {
            serial_write_string("[FAIL] dglibc snprintf: ");
            serial_write_string(b);
            serial_write_string("\n");
            return 1;
        }
        serial_write_string("[PASS] dglibc snprintf\n");
    }

    /* malloc/free */
    {
        void *p = dg_malloc(1024u);
        if (!p) { serial_write_string("[FAIL] dg_malloc\n"); return 1; }
        dg_free(p);
        serial_write_string("[PASS] dglibc malloc/free\n");
    }

    /* setjmp/longjmp */
    {
        dg_jmp_buf env;
        int rc = dg_setjmp(env);
        if (rc == 0) { dg_longjmp(env, 7); }
        if (rc != 7) { serial_write_string("[FAIL] dglibc setjmp\n"); return 1; }
        serial_write_string("[PASS] dglibc setjmp/longjmp\n");
    }

    return 0;
}
