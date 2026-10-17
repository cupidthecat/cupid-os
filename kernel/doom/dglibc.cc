/*
 * dglibc.cc - libc shim bridging DOOM calls to CupidOS kernel APIs
 *
 * Maps malloc/free, fopen/fread/fseek/ftell, snprintf/sprintf/printf,
 * setjmp/longjmp, ctype, strcasecmp, qsort, time, getenv to existing
 * CupidOS APIs.
 *
 * Compiled through the checked Doom compatibility CupidC profile.
*/

#include "dglibc.h"
#include "memory.h"
#include "string.h"
#include "vfs.h"
#include "vfs_helpers.h"
#include "fat16.h"
#include "fat16_control.h"
#include "blockcache.h"
#include "homefs.h"
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

/* Shared by every Doom translation unit through dglibc_compat.h. */
int dg_errno = 0;

enum {
    DG_EIO = 5,
    DG_ENOENT = 2,
    DG_EBADF = 9,
    DG_EACCES = 13,
    DG_EBUSY = 16,
    DG_ENOMEM = 12,
    DG_EEXIST = 17,
    DG_EISDIR = 21,
    DG_EINVAL = 22
};

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

void dg_disarm_exit(void) {
    s_exit_armed = 0;
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
    "    leal  4(%esp), %ecx\n"
    "    movl  %ecx, 16(%eax)\n"
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
    if (n > 0xffffffffu - (uint32_t)sizeof(dg_alloc_hdr_t)) {
        dg_errno = DG_ENOMEM;
        return NULL;
    }
    h = (dg_alloc_hdr_t *)kmalloc(sizeof(dg_alloc_hdr_t) + (size_t)n);
    if (!h) {
        dg_errno = DG_ENOMEM;
        return NULL;
    }
    h->size = n;
    h->magic = DG_ALLOC_MAGIC;
    return (void *)(h + 1);
}

void *dg_calloc(uint32_t n, uint32_t sz) {
    uint32_t total;
    void *p;
    if (sz != 0u && n > 0xffffffffu / sz) {
        dg_errno = DG_ENOMEM;
        return NULL;
    }
    total = n * sz;
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
        serial_write_string("[dglibc] realloc rejected a foreign or damaged pointer\n");
        dg_errno = DG_EINVAL;
        return NULL;
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
        serial_write_string("[dglibc] free rejected a foreign or damaged pointer\n");
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
    if (len == 0xffffffffu) {
        dg_errno = DG_ENOMEM;
        return NULL;
    }
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
 * Subset: %d %i %u %x %X %p %c %s %f %%
 * Width, precision, left alignment, and zero padding.*/

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

        if (spec == 'f') {
            char float_buf[48];
            double value = va_arg(ap, double);
            int flen = fmt_f(float_buf, (int)sizeof(float_buf), value, prec);
            int pad = (width > flen) ? (width - flen) : 0;
            if (!left_align) {
                int p;
                char padding = zero_pad ? '0' : ' ';
                for (p = 0; p < pad; p++) { EMIT(padding); }
            }
            { int k; for (k = 0; k < flen; k++) { EMIT(float_buf[k]); } }
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
                if (ival < 0) { neg = 1; uval = 0u - (uint32_t)ival; }
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

    if (ret < 0 || (uint32_t)ret >= (uint32_t)sizeof(buf)) {
        dg_errno = DG_EIO;
        if (f && f->magic == DG_FILE_MAGIC) { f->is_err = 1; }
        return -1;
    }
    if (dg_fwrite(buf, 1u, (uint32_t)ret, f) != (uint32_t)ret) {
        return -1;
    }
    return ret;
}

int dg_vfprintf(DG_FILE *f, const char *fmt, void *va) {
    char buf[512];
    int ret = format_into(buf, (uint32_t)sizeof(buf), fmt, (va_list)va);
    if (ret < 0 || (uint32_t)ret >= (uint32_t)sizeof(buf)) {
        dg_errno = DG_EIO;
        if (f && f->magic == DG_FILE_MAGIC) { f->is_err = 1; }
        return -1;
    }
    if (dg_fwrite(buf, 1u, (uint32_t)ret, f) != (uint32_t)ret) {
        return -1;
    }
    return ret;
}

/* stdio - file ops */

static const char *dg_home_path(const char *name, char *buffer,
                                uint32_t buffer_size) {
    const char *prefix = "/home/doom";
    uint32_t used = 0;

    if (!buffer || buffer_size == 0u) { return NULL; }
    while (*prefix) {
        if (used + 1u >= buffer_size) { return NULL; }
        buffer[used++] = *prefix++;
    }
    if (name && name[0]) {
        if (used + 1u >= buffer_size) { return NULL; }
        buffer[used++] = '/';
        while (*name) {
            if (used + 1u >= buffer_size) { return NULL; }
            buffer[used++] = *name++;
        }
    }
    buffer[used] = '\0';
    return buffer;
}

static const char *dg_resolve_path(const char *path, char *buffer,
                                   uint32_t buffer_size) {
    const char *relative;
    const char *base;
    uint32_t len;

    if (!path) { return NULL; }
    if (path[0] == '/') { return path; }

    relative = path;
    if (relative[0] == '.' && relative[1] == '/') {
        relative += 2;
    }
    if (relative[0] == '\0' ||
        (relative[0] == '.' && relative[1] == '\0')) {
        return dg_home_path(NULL, buffer, buffer_size);
    }
    if (strcmp(relative, ".savegame") == 0 ||
        strcmp(relative, ".savegame/") == 0) {
        return dg_home_path(NULL, buffer, buffer_size);
    }
    if (strncmp(relative, ".savegame/", 10u) == 0) {
        base = relative + 10;
        if (base[0] == '\0' || strcmp(base, ".") == 0 ||
            strcmp(base, "..") == 0 || strchr(base, '/')) {
            return NULL;
        }
        return dg_home_path(base, buffer, buffer_size);
    }

    len = (uint32_t)strlen(relative);
    if (!strchr(relative, '/') && len >= 4u &&
        strcmp(relative + len - 4u, ".cfg") == 0) {
        return dg_home_path(relative, buffer, buffer_size);
    }
    if (strcmp(relative, "temp.dsg") == 0) {
        return dg_home_path(relative, buffer, buffer_size);
    }

    return path;
}

static int dg_mode_flags(const char *mode, uint32_t *flags_out) {
    uint32_t flags;
    uint32_t index;
    int update = 0;
    int binary = 0;

    if (!mode || !flags_out || mode[0] == '\0') { return 0; }
    if (mode[0] == 'r') {
        flags = O_RDONLY;
    } else if (mode[0] == 'w') {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
    } else if (mode[0] == 'a') {
        flags = O_WRONLY | O_CREAT | O_APPEND;
    } else {
        return 0;
    }
    for (index = 1u; mode[index] != '\0'; index++) {
        if (mode[index] == '+' && !update) {
            update = 1;
        } else if (mode[index] == 'b' && !binary) {
            binary = 1;
        } else {
            return 0;
        }
    }
    if (update) {
        flags &= ~((uint32_t)O_WRONLY);
        flags |= O_RDWR;
    }
    *flags_out = flags;
    return 1;
}

static int dg_posix_vfs_result(int status) {
    if (status == VFS_OK) { return 0; }
    dg_errno = status < 0 ? -status : DG_EIO;
    return -1;
}

DG_FILE *dg_fopen(const char *path, const char *mode) {
    uint32_t flags;
    DG_FILE *f;
    int fd;
    int status;
    char fixed[128];
    vfs_stat_t st;

    if (!path || !dg_mode_flags(mode, &flags)) {
        dg_errno = DG_EINVAL;
        return NULL;
    }

    path = dg_resolve_path(path, fixed, (uint32_t)sizeof(fixed));
    if (!path) {
        dg_errno = DG_EINVAL;
        return NULL;
    }

    status = vfs_stat(path, &st);
    if (status == VFS_OK && st.type == VFS_TYPE_DIR) {
        dg_errno = DG_EISDIR;
        return NULL;
    }

    f = (DG_FILE *)dg_malloc((uint32_t)sizeof(struct DG_FILE));
    if (!f) {
        dg_errno = DG_ENOMEM;
        return NULL;
    }

    fd = vfs_open(path, flags);
    if (fd < 0) {
        dg_free(f);
        dg_errno = -fd;
        return NULL;
    }

    f->magic  = DG_FILE_MAGIC;
    f->fd     = fd;
    f->is_eof = 0;
    f->is_err = 0;
    f->pos    = 0;
    return f;
}

int remove(const char *path) {
    char fixed[128];
    vfs_stat_t st;
    int status;
    const char *resolved = dg_resolve_path(
        path, fixed, (uint32_t)sizeof(fixed));
    if (!resolved) {
        dg_errno = DG_EINVAL;
        return -1;
    }
    status = vfs_stat(resolved, &st);
    if (status != VFS_OK) {
        return dg_posix_vfs_result(status);
    }
    return dg_posix_vfs_result(vfs_unlink(resolved));
}

int rename(const char *old_path, const char *new_path) {
    char old_fixed[128];
    char new_fixed[128];
    vfs_stat_t st;
    int status;
    const char *old_resolved = dg_resolve_path(
        old_path, old_fixed, (uint32_t)sizeof(old_fixed));
    const char *new_resolved = dg_resolve_path(
        new_path, new_fixed, (uint32_t)sizeof(new_fixed));
    if (!old_resolved || !new_resolved) {
        dg_errno = DG_EINVAL;
        return -1;
    }
    status = vfs_stat(old_resolved, &st);
    if (status != VFS_OK) {
        return dg_posix_vfs_result(status);
    }
    if (strcmp(old_resolved, new_resolved) == 0) {
        return 0;
    }
    return dg_posix_vfs_result(vfs_rename(old_resolved, new_resolved));
}

int mkdir(const char *path, uint32_t mode) {
    char fixed[128];
    vfs_stat_t st;
    int status;
    const char *resolved = dg_resolve_path(
        path, fixed, (uint32_t)sizeof(fixed));
    (void)mode;
    if (!resolved) {
        dg_errno = DG_EINVAL;
        return -1;
    }
    status = vfs_stat(resolved, &st);
    if (status == VFS_OK) {
        dg_errno = DG_EEXIST;
        return -1;
    }
    if (status != VFS_ENOENT) {
        return dg_posix_vfs_result(status);
    }
    return dg_posix_vfs_result(vfs_mkdir(resolved));
}

int dg_fclose(DG_FILE *f) {
    int status = VFS_OK;
    if (!f || f->magic != DG_FILE_MAGIC) {
        dg_errno = DG_EBADF;
        return -1;
    }
    if (f == dg_stdin || f == dg_stdout || f == dg_stderr) {
        f->is_eof = 0;
        f->is_err = 0;
        return 0;
    }
    if (f->fd >= 0) { status = vfs_close(f->fd); }
    f->magic = 0;
    dg_free(f);
    return dg_posix_vfs_result(status);
}

uint32_t dg_fread(void *p, uint32_t sz, uint32_t n, DG_FILE *f) {
    uint32_t total;
    uint32_t completed = 0u;
    if (!f || f->magic != DG_FILE_MAGIC || !p) {
        dg_errno = DG_EBADF;
        return 0;
    }
    if (sz == 0 || n == 0) { return 0; }
    if (n > 0xffffffffu / sz) {
        dg_errno = DG_EINVAL;
        f->is_err = 1;
        return 0;
    }
    if (f->fd < 0) {
        dg_errno = DG_EBADF;
        f->is_err = 1;
        return 0;
    }
    total = sz * n;
    while (completed < total) {
        uint32_t request = total - completed;
        int got;
        if (request > 0x7fffffffu) request = 0x7fffffffu;
        got = vfs_read(f->fd, (uint8_t *)p + completed, request);
        if (got < 0) {
            dg_errno = got == (-2147483647 - 1) ? DG_EIO : -got;
            f->is_err = 1;
            break;
        }
        if (got == 0) {
            f->is_eof = 1;
            break;
        }
        if ((uint32_t)got > request) {
            dg_errno = DG_EIO;
            f->is_err = 1;
            break;
        }
        completed += (uint32_t)got;
    }
    return completed / sz;
}

uint32_t dg_fwrite(const void *p, uint32_t sz, uint32_t n, DG_FILE *f) {
    uint32_t total;
    uint32_t completed = 0u;
    if (!f || f->magic != DG_FILE_MAGIC || !p) {
        dg_errno = DG_EBADF;
        return 0;
    }
    if (sz == 0 || n == 0) { return 0; }
    if (n > 0xffffffffu / sz) {
        dg_errno = DG_EINVAL;
        f->is_err = 1;
        return 0;
    }
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
    if (f->fd < 0) {
        dg_errno = DG_EBADF;
        f->is_err = 1;
        return 0;
    }
    while (completed < total) {
        uint32_t request = total - completed;
        int wrote;
        if (request > 0x7fffffffu) request = 0x7fffffffu;
        wrote = vfs_write(f->fd, (const uint8_t *)p + completed, request);
        if (wrote < 0) {
            dg_errno = wrote == (-2147483647 - 1) ? DG_EIO : -wrote;
            f->is_err = 1;
            break;
        }
        if (wrote == 0 || (uint32_t)wrote > request) {
            dg_errno = DG_EIO;
            f->is_err = 1;
            break;
        }
        completed += (uint32_t)wrote;
    }
    return completed / sz;
}

int dg_fseek(DG_FILE *f, int32_t off, int whence) {
    int status;
    if (!f || f->magic != DG_FILE_MAGIC || f->fd < 0) {
        dg_errno = DG_EBADF;
        return -1;
    }
    status = vfs_seek(f->fd, off, whence);
    if (status < 0) {
        dg_errno = -status;
        f->is_err = 1;
        return -1;
    }
    f->is_eof = 0;
    return 0;
}

int32_t dg_ftell(DG_FILE *f) {
    int pos;
    if (!f || f->magic != DG_FILE_MAGIC || f->fd < 0) {
        dg_errno = DG_EBADF;
        return -1;
    }
    pos = vfs_seek(f->fd, 0, SEEK_CUR);
    if (pos < 0) {
        dg_errno = -pos;
        f->is_err = 1;
        return -1;
    }
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
    if (!f || f->magic != DG_FILE_MAGIC || f->fd < 0) {
        dg_errno = DG_EBADF;
        return -1;
    }
    got = vfs_read(f->fd, &c, 1);
    if (got < 0) {
        dg_errno = -got;
        f->is_err = 1;
        return -1;
    }
    if (got == 0) { f->is_eof = 1; return -1; }
    return (int)c;
}

int dg_fputc(int c, DG_FILE *f) {
    char ch = (char)c;
    int status;
    if (!f || f->magic != DG_FILE_MAGIC) {
        dg_errno = DG_EBADF;
        return -1;
    }
    if (f->fd == -2 || f->fd == -3) {
        serial_write_char(ch);
        return c;
    }
    if (f->fd < 0) {
        dg_errno = DG_EBADF;
        return -1;
    }
    status = vfs_write(f->fd, &ch, 1);
    if (status != 1) {
        dg_errno = status < 0 ? -status : DG_EIO;
        f->is_err = 1;
        return -1;
    }
    return c;
}

int dg_fputs(const char *s, DG_FILE *f) {
    uint32_t length;
    if (!s) {
        dg_errno = DG_EINVAL;
        if (f && f->magic == DG_FILE_MAGIC) { f->is_err = 1; }
        return -1;
    }
    length = (uint32_t)strlen(s);
    if (dg_fwrite(s, 1u, length, f) != length) { return -1; }
    return 0;
}

int dg_fflush(DG_FILE *f) {
    if (!f) { return 0; }
    if (f->magic != DG_FILE_MAGIC) {
        dg_errno = DG_EBADF;
        return -1;
    }
    return 0;
}

int dg_ferror(DG_FILE *f) {
    if (!f || f->magic != DG_FILE_MAGIC) {
        dg_errno = DG_EBADF;
        return 1;
    }
    return f->is_err;
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

extern int M_FileExists(char *filename);
extern int M_ConfigFilesystemTest(void);
extern void I_AtExit(void (*func)(void), int run_if_error);
extern void I_Quit(void) __attribute__((noreturn));
extern void I_Error(char *error, ...) __attribute__((noreturn));
extern void I_ResetExitState(void);
extern long strtol(const char *s, char **endp, int base);
extern unsigned long strtoul(const char *s, char **endp, int base);
extern int sscanf(const char *s, const char *fmt, ...);

static int dg_lifecycle_trace[8];
static int dg_lifecycle_count;
static int dg_lifecycle_base;

static void dg_lifecycle_first(void) {
    if (dg_lifecycle_count < 8) {
        dg_lifecycle_trace[dg_lifecycle_count++] = dg_lifecycle_base + 1;
    }
}

static void dg_lifecycle_second(void) {
    if (dg_lifecycle_count < 8) {
        dg_lifecycle_trace[dg_lifecycle_count++] = dg_lifecycle_base + 2;
    }
}

static void dg_lifecycle_error(void) {
    if (dg_lifecycle_count < 8) {
        dg_lifecycle_trace[dg_lifecycle_count++] = dg_lifecycle_base + 3;
    }
}

static void dg_lifecycle_normal_only(void) {
    if (dg_lifecycle_count < 8) {
        dg_lifecycle_trace[dg_lifecycle_count++] = dg_lifecycle_base + 4;
    }
}

static int dg_lifecycle_quit_cycle(int base) {
    dg_jmp_buf env;
    int jumped = dg_setjmp(env);
    if (jumped == 0) {
        I_ResetExitState();
        dg_lifecycle_base = base;
        I_AtExit(dg_lifecycle_first, 0);
        I_AtExit(dg_lifecycle_second, 0);
        dg_arm_exit(env);
        I_Quit();
    }
    dg_disarm_exit();
    I_ResetExitState();
    return jumped != 0;
}

static int dg_lifecycle_error_cycle(int base) {
    dg_jmp_buf env;
    int jumped = dg_setjmp(env);
    if (jumped == 0) {
        I_ResetExitState();
        dg_lifecycle_base = base;
        I_AtExit(dg_lifecycle_error, 1);
        I_AtExit(dg_lifecycle_normal_only, 0);
        dg_arm_exit(env);
        I_Error((char *)"dglibc lifecycle error %d", base);
    }
    dg_disarm_exit();
    I_ResetExitState();
    return jumped != 0;
}

static int dglibc_expect_path(const char *input, const char *expected) {
    char buffer[128];
    const char *resolved = dg_resolve_path(input, buffer,
                                            (uint32_t)sizeof(buffer));
    if (!resolved || strcmp(resolved, expected) != 0) {
        serial_write_string("[FAIL] dglibc resolved the wrong path for ");
        serial_write_string(input ? input : "(null)");
        serial_write_string("\n");
        return 0;
    }
    return 1;
}

static int dglibc_test_body(void) {
    /* snprintf */
    {
        char b[64];
        int n = dg_snprintf(b, sizeof(b), "hello %s %d %x %.2f",
                            "world", 42, 0xCAFEu, 1.25);
        (void)n;
        const char *expected = "hello world 42 cafe 1.25";
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
        if (dg_fclose(dg_stdout) != 0 || dg_ferror(dg_stdout)) {
            serial_write_string("[FAIL] dglibc damaged a static stream\n");
            return 1;
        }
        serial_write_string("[PASS] dglibc snprintf\n");
    }

    /* malloc/free */
    {
        void *p = dg_malloc(1024u);
        if (!p) { serial_write_string("[FAIL] dg_malloc\n"); return 1; }
        dg_free(p);
        p = dg_malloc(0xfffffff8u);
        if (p || dg_errno != DG_ENOMEM) {
            if (p) dg_free(p);
            serial_write_string("[FAIL] dg_malloc accepted header overflow\n");
            return 1;
        }
        p = dg_calloc(0x80000000u, 2u);
        if (p || dg_errno != DG_ENOMEM) {
            if (p) dg_free(p);
            serial_write_string("[FAIL] dg_calloc accepted product overflow\n");
            return 1;
        }
        serial_write_string("[PASS] dglibc malloc/free\n");
    }

    /* setjmp/longjmp and the shell exit envelope */
    {
        dg_jmp_buf env;
        int rc = dg_setjmp(env);
        if (rc == 0) { dg_longjmp(env, 7); }
        if (rc == 7) {
            dg_arm_exit(env);
            dg_exit(9);
        }
        if (rc != 9) {
            serial_write_string("[FAIL] dglibc jump or exit envelope\n");
            return 1;
        }
        dg_disarm_exit();
        serial_write_string("[PASS] dglibc setjmp/longjmp and exit envelope\n");
    }

    /* Checked integer parsing used by Doom's configuration reader. */
    {
        char *end;
        int parsed = -1;
        const char *no_digits = "+";

        dg_errno = 0;
        if (strtol("2147483647x", &end, 10) != 2147483647L ||
            *end != 'x' ||
            strtol("-2147483648", &end, 10) !=
                (-2147483647L - 1L) || *end != '\0' ||
            strtol("0x2a", &end, 0) != 42L || *end != '\0' ||
            strtol(no_digits, &end, 10) != 0L || end != no_digits) {
            serial_write_string("[FAIL] dglibc parsed a bounded integer incorrectly\n");
            return 1;
        }
        dg_errno = 0;
        if (strtol("2147483648", &end, 10) != 2147483647L ||
            dg_errno != 34 || *end != '\0') {
            serial_write_string("[FAIL] dglibc missed signed integer overflow\n");
            return 1;
        }
        dg_errno = 0;
        if (strtoul("4294967296", &end, 10) != 0xffffffffu ||
            dg_errno != 34 || *end != '\0' ||
            sscanf(" 0x2a", " 0x%x", &parsed) != 1 || parsed != 42 ||
            sscanf(" sign", " %d", &parsed) != 0) {
            serial_write_string("[FAIL] dglibc accepted an unsafe integer conversion\n");
            return 1;
        }
        serial_write_string("[PASS] dglibc checked integer parsing\n");
    }

    /* Exit callbacks belong to one in-process Doom launch. */
    {
        dg_lifecycle_count = 0;
        if (!dg_lifecycle_quit_cycle(100) ||
            !dg_lifecycle_quit_cycle(200) ||
            !dg_lifecycle_error_cycle(300) ||
            !dg_lifecycle_error_cycle(400) ||
            dg_lifecycle_count != 6 ||
            dg_lifecycle_trace[0] != 102 ||
            dg_lifecycle_trace[1] != 101 ||
            dg_lifecycle_trace[2] != 202 ||
            dg_lifecycle_trace[3] != 201 ||
            dg_lifecycle_trace[4] != 303 ||
            dg_lifecycle_trace[5] != 403) {
            serial_write_string("[FAIL] dglibc reused Doom exit callbacks\n");
            return 1;
        }
        serial_write_string("[PASS] dglibc Doom exit callback lifecycle\n");
    }

    /* Active Doom config and save paths stay under /home/doom. */
    {
        char path_buffer[128];
        char short_buffer[8];
        DG_FILE *directory;
        DG_FILE *missing;
        vfs_mkdir("/home/doom");
        if (!dglibc_expect_path(".", "/home/doom") ||
            !dglibc_expect_path(".savegame", "/home/doom") ||
            !dglibc_expect_path("./.savegame", "/home/doom") ||
            !dglibc_expect_path(".default.cfg",
                                "/home/doom/.default.cfg") ||
            !dglibc_expect_path(".doomgenericdoom.cfg",
                                "/home/doom/.doomgenericdoom.cfg") ||
            !dglibc_expect_path("temp.dsg", "/home/doom/temp.dsg") ||
            !dglibc_expect_path("./.savegame/temp.dsg",
                                "/home/doom/temp.dsg") ||
            !dglibc_expect_path("./.savegame/doomsav0.dsg",
                                "/home/doom/doomsav0.dsg") ||
            !dglibc_expect_path("/tmp/recovery.dsg",
                                "/tmp/recovery.dsg")) {
            return 1;
        }
        if (dg_resolve_path("./.savegame/nested/file.dsg",
                            path_buffer,
                            (uint32_t)sizeof(path_buffer)) != NULL) {
            serial_write_string("[FAIL] dglibc accepted a nested save path\n");
            return 1;
        }
        if (dg_resolve_path("./.savegame/../escape.dsg",
                            path_buffer,
                            (uint32_t)sizeof(path_buffer)) != NULL) {
            serial_write_string("[FAIL] dglibc accepted save traversal\n");
            return 1;
        }
        if (dg_resolve_path(".doomgenericdoom.cfg", short_buffer,
                            (uint32_t)sizeof(short_buffer)) != NULL) {
            serial_write_string("[FAIL] dglibc truncated a config path\n");
            return 1;
        }
        directory = dg_fopen("/home/doom", "rb");
        if (directory || dg_errno != DG_EISDIR) {
            if (directory) { dg_fclose(directory); }
            serial_write_string("[FAIL] dglibc did not report a directory open\n");
            return 1;
        }
        missing = dg_fopen("/home/doom/dglibc-missing.cfg", "rb");
        if (missing || dg_errno != DG_ENOENT) {
            if (missing) { dg_fclose(missing); }
            serial_write_string("[FAIL] dglibc did not report a missing file\n");
            return 1;
        }
        if (dg_fopen("/home/doom/dglibc-invalid.cfg", "rx") != NULL ||
            dg_errno != DG_EINVAL) {
            serial_write_string("[FAIL] dglibc accepted an invalid file mode\n");
            return 1;
        }
        if (dg_fopen("../doomsav0.dsg", "wb") != NULL ||
            dg_errno != DG_EINVAL) {
            serial_write_string("[FAIL] dglibc accepted an unrelated save path\n");
            return 1;
        }
        if (!M_FileExists("/home/doom") ||
            M_FileExists("/home/doom/dglibc-missing.cfg")) {
            serial_write_string("[FAIL] dglibc errno did not cross Doom objects\n");
            return 1;
        }
        serial_write_string("[PASS] dglibc Doom path resolution\n");
        serial_write_string("[PASS] dglibc shared errno bridge\n");
    }

    if (M_ConfigFilesystemTest() != 0) {
        serial_write_string("[FAIL] dglibc Doom config round trip\n");
        return 1;
    }
    serial_write_string("[PASS] dglibc Doom config round trip\n");

    /* Config files use the same relative bridge without touching live names. */
    {
        const char *config = ".dglibc-test.cfg";
        const char *payload = "bridge=ready\n";
        char oversized[520];
        char restored[16];
        DG_FILE *f;
        uint32_t mode_flags;
        uint32_t oversized_index;

        vfs_mkdir("/home/doom");
        vfs_unlink("/home/doom/.dglibc-test.cfg");
        if (!dg_mode_flags("rb+", &mode_flags) || mode_flags != O_RDWR ||
            !dg_mode_flags("r+b", &mode_flags) || mode_flags != O_RDWR ||
            dg_mode_flags("r++", &mode_flags) ||
            dg_mode_flags("rbb", &mode_flags)) {
            serial_write_string("[FAIL] dglibc parsed file modes incorrectly\n");
            return 1;
        }
        f = dg_fopen(config, "wb");
        if (!f) {
            serial_write_string("[FAIL] dglibc could not open a relative config\n");
            return 1;
        }
        if (vfs_read(f->fd, restored, 1u) != VFS_EACCES) {
            dg_fclose(f);
            serial_write_string("[FAIL] dglibc read through a write-only stream\n");
            return 1;
        }
        if (vfs_seek(f->fd, 0x7fffffff, SEEK_SET) != 0x7fffffff ||
            vfs_seek(f->fd, 1, SEEK_CUR) != VFS_EINVAL ||
            vfs_write(f->fd, payload, 0xffffffffu) != VFS_ENOSPC ||
            vfs_seek(f->fd, 0, SEEK_SET) != 0) {
            dg_fclose(f);
            serial_write_string("[FAIL] dglibc accepted a HomeFS offset overflow\n");
            return 1;
        }
        if (dg_fwrite(payload, 0xffffffffu, 2u, f) != 0u ||
            dg_errno != DG_EINVAL || !f->is_err) {
            dg_fclose(f);
            serial_write_string("[FAIL] dglibc accepted a write size overflow\n");
            return 1;
        }
        dg_clearerr(f);
        for (oversized_index = 0u;
             oversized_index + 1u < (uint32_t)sizeof(oversized);
             oversized_index++) {
            oversized[oversized_index] = 'x';
        }
        oversized[sizeof(oversized) - 1u] = '\0';
        if (dg_fprintf(f, "%s", oversized) != -1 ||
            dg_errno != DG_EIO || !dg_ferror(f)) {
            dg_fclose(f);
            serial_write_string("[FAIL] dglibc accepted oversized formatted output\n");
            return 1;
        }
        dg_clearerr(f);
        if (dg_fputs("bridge=", f) != 0 ||
            dg_fprintf(f, "%s\n", "ready") != 6 ||
            dg_fflush(f) != 0 || dg_ferror(f)) {
            dg_fclose(f);
            serial_write_string("[FAIL] dglibc could not write a relative config\n");
            return 1;
        }
        if (dg_fclose(f) != 0) {
            serial_write_string("[FAIL] dglibc could not close a relative config\n");
            return 1;
        }
        f = dg_fopen(config, "rb");
        if (!f) {
            serial_write_string("[FAIL] dglibc could not reopen a relative config\n");
            return 1;
        }
        if (vfs_write(f->fd, payload, 1u) != VFS_EACCES) {
            dg_fclose(f);
            serial_write_string("[FAIL] dglibc wrote through a read-only stream\n");
            return 1;
        }
        if (dg_fread(restored, 0xffffffffu, 2u, f) != 0u ||
            dg_errno != DG_EINVAL || !f->is_err) {
            dg_fclose(f);
            serial_write_string("[FAIL] dglibc accepted a read size overflow\n");
            return 1;
        }
        dg_clearerr(f);
        if (dg_fread(restored, 0x80000000u, 1u, f) != 0u ||
            !f->is_eof || f->is_err || dg_fseek(f, 0, SEEK_SET) != 0) {
            dg_fclose(f);
            serial_write_string("[FAIL] dglibc mishandled a short wide read\n");
            return 1;
        }
        if (dg_fread(restored, 1u, 13u, f) != 13u ||
            dg_fclose(f) != 0 || memcmp(restored, payload, 13u) != 0) {
            serial_write_string("[FAIL] dglibc could not read a relative config\n");
            return 1;
        }
        if (vfs_open("/home/doom/.dglibc-test.cfg", O_TRUNC) != VFS_EINVAL ||
            vfs_open("/home/doom/.dglibc-test.cfg", O_APPEND) != VFS_EINVAL) {
            serial_write_string("[FAIL] dglibc accepted read-only mutation flags\n");
            return 1;
        }
        f = dg_fopen(config, "ab+");
        if (!f || dg_fseek(f, 0, SEEK_SET) != 0 ||
            dg_fwrite("!", 1u, 1u, f) != 1u || dg_fclose(f) != 0) {
            serial_write_string("[FAIL] dglibc could not append after a seek\n");
            return 1;
        }
        f = dg_fopen(config, "rb");
        if (!f || dg_fread(restored, 1u, 14u, f) != 14u ||
            dg_fclose(f) != 0 || memcmp(restored, payload, 13u) != 0 ||
            restored[13] != '!') {
            serial_write_string("[FAIL] dglibc append did not preserve the file tail\n");
            return 1;
        }
        if (remove(config) != 0) {
            serial_write_string("[FAIL] dglibc could not remove the test config\n");
            return 1;
        }
        serial_write_string("[PASS] dglibc synthetic config filesystem bridge\n");
    }

    /* Doom save files use a relative virtual directory. */
    {
        const char *dir = "/home/dglibc-test-dir";
        const char *temp = "./.savegame/dglibc-test-temp.dsg";
        const char *save = "./.savegame/dglibc-test-save.dsg";
        const char *payload = "Cupid Doom save bridge\n";
        const char *destination_dir = "/home/dglibc-test-destination-dir";
        const char *missing_parent = "/home/doom/dglibc-missing-parent";
        const char *missing_destination =
            "/home/doom/dglibc-missing-parent/save.dsg";
        char restored[32];
        DG_FILE *f;
        DG_FILE *open_target;
        int directory_fd;
        vfs_stat_t st;

        vfs_mkdir("/home/doom");
        vfs_unlink(dir);
        vfs_unlink("/home/doom/dglibc-test-temp.dsg");
        vfs_unlink("/home/doom/dglibc-test-save.dsg");
        vfs_unlink(missing_destination);
        vfs_unlink(missing_parent);
        vfs_unlink(destination_dir);

        if (mkdir("./.savegame/", 0755u) == 0 ||
            dg_errno != DG_EEXIST) {
            serial_write_string("[FAIL] dglibc save directory lost its existing state\n");
            return 1;
        }
        if (mkdir(dir, 0755u) != 0 ||
            vfs_stat(dir, &st) != VFS_OK ||
            st.type != VFS_TYPE_DIR) {
            serial_write_string("[FAIL] dglibc could not create the Doom save directory\n");
            return 1;
        }
        if (mkdir(dir, 0755u) == 0 || dg_errno != DG_EEXIST) {
            serial_write_string("[FAIL] dglibc mkdir hid an existing directory\n");
            return 1;
        }
        if (remove(dir) != 0 ||
            vfs_stat(dir, &st) != VFS_ENOENT) {
            serial_write_string("[FAIL] dglibc did not remove the test directory\n");
            return 1;
        }
        if (remove(dir) == 0 || dg_errno != DG_ENOENT) {
            serial_write_string("[FAIL] dglibc remove hid a missing path\n");
            return 1;
        }

        f = dg_fopen(save, "wb");
        if (!f || dg_fwrite("old\n", 1u, 4u, f) != 4u ||
            dg_fclose(f) != 0) {
            serial_write_string("[FAIL] dglibc could not seed the old save\n");
            return 1;
        }

        f = dg_fopen(temp, "wb");
        if (!f) {
            serial_write_string("[FAIL] dglibc could not write the temporary save\n");
            return 1;
        }
        if (dg_fwrite(payload, 1u, 23u, f) != 23u) {
            dg_fclose(f);
            serial_write_string("[FAIL] dglibc could not write the temporary save\n");
            return 1;
        }
        if (dg_fclose(f) != 0) {
            serial_write_string("[FAIL] dglibc could not close the temporary save\n");
            return 1;
        }
        if (rename(temp, save) != 0) {
            serial_write_string("[FAIL] dglibc could not commit the temporary save\n");
            return 1;
        }
        f = dg_fopen(temp, "rb");
        if (f) {
            dg_fclose(f);
            serial_write_string("[FAIL] dglibc rename left the temporary save behind\n");
            return 1;
        }
        f = dg_fopen(save, "rb");
        if (!f || dg_fread(restored, 1u, 23u, f) != 23u ||
            dg_fclose(f) != 0 || memcmp(restored, payload, 23u) != 0) {
            serial_write_string("[FAIL] dglibc did not preserve the committed save\n");
            return 1;
        }
        open_target = dg_fopen(save, "rb");
        f = dg_fopen(temp, "wb");
        if (!open_target || !f || dg_fwrite(payload, 1u, 23u, f) != 23u ||
            dg_fclose(f) != 0 || rename(temp, save) == 0 ||
            dg_errno != DG_EBUSY || dg_fclose(open_target) != 0 ||
            rename(temp, save) != 0) {
            serial_write_string("[FAIL] dglibc replaced an open HomeFS target\n");
            return 1;
        }
        directory_fd = vfs_open("/home/doom", O_RDONLY);
        if (directory_fd < 0 || vfs_unlink("/home/doom/dglibc-test-save.dsg") !=
                                  VFS_EBUSY ||
            vfs_close(directory_fd) != VFS_OK) {
            serial_write_string("[FAIL] dglibc invalidated a HomeFS iterator\n");
            return 1;
        }
        if (rename(save, save) != 0) {
            serial_write_string("[FAIL] dglibc changed an existing same-path rename\n");
            return 1;
        }
        if (rename(save, "/home/doom//dglibc-test-save.dsg") != 0) {
            serial_write_string("[FAIL] dglibc did not preserve an aliased rename\n");
            return 1;
        }
        if (vfs_rename("/home/doom/dglibc-test-save.dsg",
                       missing_destination) != VFS_ENOENT ||
            vfs_stat("/home/doom/dglibc-test-save.dsg", &st) != VFS_OK ||
            vfs_stat(missing_parent, &st) != VFS_ENOENT) {
            serial_write_string("[FAIL] dglibc HomeFS rename created a parent\n");
            return 1;
        }
        if (mkdir(destination_dir, 0755u) != 0 ||
            rename(save, destination_dir) == 0 ||
            dg_errno != DG_EISDIR ||
            vfs_stat("/home/doom/dglibc-test-save.dsg", &st) != VFS_OK ||
            vfs_stat(destination_dir, &st) != VFS_OK ||
            st.type != VFS_TYPE_DIR) {
            serial_write_string("[FAIL] dglibc rename damaged a directory boundary\n");
            return 1;
        }
        if (remove(destination_dir) != 0) {
            serial_write_string("[FAIL] dglibc could not remove the rename target directory\n");
            return 1;
        }
        if (rename(temp, save) == 0 || dg_errno != DG_ENOENT) {
            serial_write_string("[FAIL] dglibc rename hid a missing source\n");
            return 1;
        }
        if (rename("./.savegame/missing.dsg",
                   "./.savegame/missing.dsg") == 0 ||
            dg_errno != DG_ENOENT) {
            serial_write_string("[FAIL] dglibc renamed a missing path to itself\n");
            return 1;
        }
        if (remove(save) != 0 || remove(save) == 0 ||
            dg_errno != DG_ENOENT) {
            serial_write_string("[FAIL] dglibc remove did not report the save state\n");
            return 1;
        }
        if (dg_fopen("dglibc-relative.tmp", "wb") != NULL ||
            dg_errno != DG_EINVAL) {
            serial_write_string("[FAIL] dglibc accepted an unrelated relative path\n");
            return 1;
        }
        serial_write_string("[PASS] dglibc synthetic save filesystem bridge\n");
    }

    /* Native rename stays inside one mounted filesystem. */
    {
        const char *source = "/tmp/dglibc-rename-source";
        const char *destination = "/tmp/dglibc-rename-destination";
        const char *replacement = "/tmp/dglibc-rename-replacement";
        const char *cross_mount = "/home/doom/dglibc-cross-mount";
        const char *missing_parent = "/tmp/dglibc-rename-missing-parent";
        const char *missing_destination =
            "/tmp/dglibc-rename-missing-parent/file";
        char restored[4];
        int fd;
        int target_fd;
        int directory_fd;
        vfs_stat_t st;

        vfs_unlink(source);
        vfs_unlink(destination);
        vfs_unlink(replacement);
        vfs_unlink(cross_mount);
        vfs_unlink(missing_destination);
        vfs_unlink(missing_parent);
        fd = vfs_open(source, O_WRONLY | O_CREAT | O_TRUNC);
        if (fd < 0 || vfs_write(fd, "ram", 3u) != 3 ||
            vfs_close(fd) != VFS_OK ||
            vfs_rename(source, destination) != VFS_OK ||
            vfs_stat(source, &st) != VFS_ENOENT ||
            vfs_stat(destination, &st) != VFS_OK || st.size != 3u ||
            vfs_rename(destination,
                       "/tmp//dglibc-rename-destination") != VFS_OK ||
            vfs_rename(destination, missing_destination) != VFS_ENOENT ||
            vfs_stat(destination, &st) != VFS_OK ||
            vfs_stat(missing_parent, &st) != VFS_ENOENT ||
            vfs_rename(destination, cross_mount) != VFS_EXDEV ||
            vfs_stat(destination, &st) != VFS_OK ||
            vfs_stat(cross_mount, &st) != VFS_ENOENT) {
            serial_write_string("[FAIL] dglibc crossed a VFS rename boundary\n");
            return 1;
        }
        fd = vfs_open(replacement, O_WRONLY | O_CREAT | O_TRUNC);
        if (fd < 0 || vfs_write(fd, "new", 3u) != 3 ||
            vfs_close(fd) != VFS_OK) {
            serial_write_string("[FAIL] dglibc could not seed a replacement\n");
            return 1;
        }
        target_fd = vfs_open(destination, O_RDONLY);
        if (target_fd < 0 ||
            vfs_rename(replacement, destination) != VFS_EBUSY ||
            vfs_stat(replacement, &st) != VFS_OK ||
            vfs_stat(destination, &st) != VFS_OK ||
            vfs_close(target_fd) != VFS_OK) {
            serial_write_string("[FAIL] dglibc replaced an open VFS target\n");
            return 1;
        }
        directory_fd = vfs_open("/tmp", O_RDONLY);
        if (directory_fd < 0 || vfs_unlink(destination) != VFS_EBUSY ||
            vfs_stat(destination, &st) != VFS_OK ||
            vfs_close(directory_fd) != VFS_OK ||
            vfs_rename(replacement, destination) != VFS_OK) {
            serial_write_string("[FAIL] dglibc invalidated a VFS iterator\n");
            return 1;
        }
        fd = vfs_open(destination, O_RDONLY);
        if (fd < 0 || vfs_read(fd, restored, 3u) != 3 ||
            vfs_close(fd) != VFS_OK || memcmp(restored, "new", 3u) != 0 ||
            vfs_unlink(destination) != VFS_OK) {
            serial_write_string("[FAIL] dglibc lost a native VFS rename\n");
            return 1;
        }
        serial_write_string("[PASS] dglibc VFS rename boundaries\n");
    }

    /* Copying or truncating a live path alias must preserve its bytes. */
    {
        const char *path = "/tmp/dglibc-copy-self";
        const char *directory = "/tmp/dglibc-copy-directory";
        char restored[5];
        int reader;
        int alias;

        vfs_unlink(path);
        vfs_unlink(directory);
        if (vfs_write_all(path, "copy", 4u) != 4 ||
            vfs_mkdir(directory) != VFS_OK ||
            vfs_copy_file(path, path) != VFS_EINVAL ||
            vfs_copy_file("/tmp//dglibc-copy-self",
                          "/tmp/dglibc-copy-self/") != VFS_EINVAL ||
            vfs_copy_file(directory, path) != VFS_EISDIR ||
            vfs_read_all(path, restored, sizeof(restored)) != 4 ||
            memcmp(restored, "copy", 4u) != 0) {
            serial_write_string("[FAIL] dglibc accepted a VFS self-copy\n");
            return 1;
        }
        reader = vfs_open(path, O_RDONLY);
        alias = vfs_open("/tmp//dglibc-copy-self/",
                         O_WRONLY | O_TRUNC);
        if (reader < 0 || alias != VFS_EBUSY ||
            vfs_close(reader) != VFS_OK ||
            vfs_read_all(path, restored, sizeof(restored)) != 4 ||
            memcmp(restored, "copy", 4u) != 0 ||
            vfs_unlink(path) != VFS_OK ||
            vfs_unlink(directory) != VFS_OK) {
            serial_write_string("[FAIL] dglibc damaged a VFS path alias\n");
            if (alias >= 0) vfs_close(alias);
            return 1;
        }
        serial_write_string("[PASS] dglibc VFS copy boundaries\n");
    }

    /* A failed cache fill cannot relabel or re-dirty the evicted bytes. */
    if (blockcache_failure_selftest() != 0) {
        serial_write_string("[FAIL] dglibc block cache failure boundary\n");
        return 1;
    }
    serial_write_string("[PASS] dglibc block cache failure boundary\n");

    /* RamFS accepts its exact 64 KiB boundary and rejects the next byte. */
    {
        const char *path = "/tmp/dglibc-ramfs-limit";
        char last = 0;
        int fd;
        vfs_stat_t st;

        vfs_unlink(path);
        fd = vfs_open(path, O_WRONLY | O_CREAT | O_TRUNC);
        if (fd < 0 || vfs_seek(fd, 65535, SEEK_SET) != 65535 ||
            vfs_write(fd, "Z", 1u) != 1 ||
            vfs_write(fd, "X", 1u) != VFS_ENOSPC ||
            vfs_seek(fd, 65537, SEEK_SET) != VFS_ENOSPC ||
            vfs_close(fd) != VFS_OK ||
            vfs_stat(path, &st) != VFS_OK || st.size != 65536u) {
            serial_write_string("[FAIL] dglibc crossed the RamFS size boundary\n");
            return 1;
        }
        fd = vfs_open(path, O_RDONLY);
        if (fd < 0 || vfs_seek(fd, 65535, SEEK_SET) != 65535 ||
            vfs_read(fd, &last, 1u) != 1 || last != 'Z' ||
            vfs_close(fd) != VFS_OK || vfs_unlink(path) != VFS_OK) {
            serial_write_string("[FAIL] dglibc changed RamFS after a rejected write\n");
            return 1;
        }
        serial_write_string("[PASS] dglibc RamFS size boundary\n");
    }

    /* A FAT file write must not replace a directory with the same 8.3 name. */
    {
        const char *directory = "/disk/DGDIR";
        const char *child = "/disk/DGDIR/KEEP.TXT";
        char restored[5];
        int fd;
        vfs_stat_t st;

        vfs_unlink(child);
        vfs_unlink(directory);
        if (vfs_mkdir(directory) != VFS_OK) {
            serial_write_string("[FAIL] dglibc could not seed a FAT directory\n");
            return 1;
        }
        if (vfs_write_all(child, "keep", 4u) != 4 ||
            fat16_write_file("DGDIR", "bad", 3u) >= 0 ||
            vfs_stat(directory, &st) != VFS_OK || st.type != VFS_TYPE_DIR) {
            serial_write_string("[FAIL] dglibc replaced a FAT directory\n");
            return 1;
        }
        fd = vfs_open(child, O_RDONLY);
        if (fd < 0 || vfs_read(fd, restored, 4u) != 4 ||
            vfs_close(fd) != VFS_OK || memcmp(restored, "keep", 4u) != 0 ||
            vfs_unlink(child) != VFS_OK ||
            vfs_unlink(directory) != VFS_OK) {
            serial_write_string("[FAIL] dglibc damaged a FAT directory collision\n");
            return 1;
        }
        serial_write_string("[PASS] dglibc FAT directory collision\n");
    }

    /* FAT reads clamp by subtraction even for the largest request. */
    {
        const char *path = "/disk/RANGE.TXT";
        fat16_file_t *file;
        char restored[5];

        vfs_unlink(path);
        if (vfs_write_all(path, "range", 5u) != 5) {
            serial_write_string("[FAIL] dglibc could not seed a FAT range\n");
            return 1;
        }
        memset(restored, '?', sizeof(restored));
        file = fat16_open("RANGE.TXT");
        if (!file) {
            serial_write_string("[FAIL] dglibc could not open a FAT range\n");
            return 1;
        }
        file->position = 2u;
        if (fat16_read(file, restored, 0xffffffffu) != 3 ||
            memcmp(restored, "nge", 3u) != 0 || restored[3] != '?' ||
            fat16_close(file) != 0 || vfs_unlink(path) != VFS_OK) {
            serial_write_string("[FAIL] dglibc crossed a FAT read boundary\n");
            return 1;
        }
        serial_write_string("[PASS] dglibc FAT read boundary\n");
    }

    /* Exhausting raw FAT handles must not turn O_CREAT into truncation. */
    {
        const char *path = "/disk/POOL.TXT";
        fat16_file_t *handles[16];
        char restored[5];
        int acquired = 0;
        int denied;
        int okay = 1;

        vfs_unlink(path);
        if (vfs_write_all(path, "alive", 5u) != 5) {
            serial_write_string("[FAIL] dglibc could not seed a FAT pool target\n");
            return 1;
        }
        while (acquired < 16) {
            fat16_file_t *handle = fat16_open("POOL.TXT");
            if (!handle) break;
            handles[acquired++] = handle;
        }
        denied = vfs_open(path, O_WRONLY | O_CREAT);
        if (acquired == 0 || denied != VFS_EMFILE) {
            if (denied >= 0) vfs_close(denied);
            okay = 0;
        }
        for (int i = 0; i < acquired; i++) {
            if (fat16_close(handles[i]) != 0) okay = 0;
        }
        if (!okay || vfs_read_all(path, restored, sizeof(restored)) != 5 ||
            memcmp(restored, "alive", 5u) != 0 ||
            vfs_unlink(path) != VFS_OK) {
            serial_write_string("[FAIL] dglibc truncated through FAT handle exhaustion\n");
            return 1;
        }
        serial_write_string("[PASS] dglibc FAT handle exhaustion\n");
    }

    /* Replacement and deletion wait until the old FAT entry is unused. */
    {
        const char *path = "/disk/BUSY.TXT";
        char restored[4];
        int reader;
        int writer;

        vfs_unlink(path);
        if (vfs_write_all(path, "old", 3u) != 3) {
            serial_write_string("[FAIL] dglibc could not seed a busy FAT file\n");
            return 1;
        }
        reader = vfs_open(path, O_RDONLY);
        writer = vfs_open(path, O_WRONLY | O_TRUNC);
        if (reader < 0 || writer < 0 || vfs_write(writer, "new", 3u) != 3 ||
            vfs_close(writer) != VFS_EBUSY ||
            vfs_unlink(path) != VFS_EBUSY ||
            vfs_read(reader, restored, 3u) != 3 ||
            memcmp(restored, "old", 3u) != 0 ||
            vfs_close(reader) != VFS_OK ||
            vfs_read_all(path, restored, sizeof(restored)) != 3 ||
            memcmp(restored, "old", 3u) != 0 ||
            vfs_write_all(path, "new", 3u) != 3 ||
            vfs_read_all(path, restored, sizeof(restored)) != 3 ||
            memcmp(restored, "new", 3u) != 0 ||
            vfs_unlink(path) != VFS_OK) {
            serial_write_string("[FAIL] dglibc replaced a live FAT entry\n");
            return 1;
        }
        serial_write_string("[PASS] dglibc FAT busy replacement\n");
    }

    /* FAT VFS paths must fit exactly into one-level 8.3 components. */
    {
        const char *base = "/disk/ABCDEFGH";
        const char *extended = "/disk/ABCDEFGH.TXT";
        char restored[5];
        int fd;

        vfs_unlink(base);
        vfs_unlink(extended);
        if (vfs_write_all(base, "base", 4u) != 4 ||
            vfs_write_all(extended, "text", 4u) != 4) {
            serial_write_string("[FAIL] dglibc could not seed FAT path boundaries\n");
            return 1;
        }
        fd = vfs_open("/disk/ABCDEFGHZZZZZZZZ.TXT",
                      O_WRONLY | O_CREAT | O_TRUNC);
        if (fd != VFS_EINVAL) {
            if (fd >= 0) vfs_close(fd);
            serial_write_string("[FAIL] dglibc accepted a long FAT component\n");
            return 1;
        }
        fd = vfs_open("/disk/ABCDEFGHZZZZ/TAIL.TXT",
                      O_WRONLY | O_CREAT | O_TRUNC);
        if (fd != VFS_EINVAL) {
            if (fd >= 0) vfs_close(fd);
            serial_write_string("[FAIL] dglibc accepted a deep FAT alias\n");
            return 1;
        }
        if (vfs_read_all(base, restored, sizeof(restored)) != 4 ||
            memcmp(restored, "base", 4u) != 0 ||
            vfs_read_all(extended, restored, sizeof(restored)) != 4 ||
            memcmp(restored, "text", 4u) != 0 ||
            vfs_unlink(base) != VFS_OK ||
            vfs_unlink(extended) != VFS_OK) {
            serial_write_string("[FAIL] dglibc aliased a rejected FAT path\n");
            return 1;
        }
        serial_write_string("[PASS] dglibc FAT 8.3 path boundary\n");
    }

    /* The live HomeFS container has one owner and no writable FAT alias. */
    {
        const char *probe = "/home/dglibc-container-owner";
        char restored[5];
        int alias = vfs_open("/disk/HOMEFS.SYS",
                             O_WRONLY | O_TRUNC);

        vfs_unlink(probe);
        if (alias != VFS_EBUSY ||
            vfs_unlink("/disk/HOMEFS.SYS") != VFS_EBUSY ||
            fat16_write_file("HOMEFS.SYS", "bad", 3u) != FAT16_BUSY ||
            vfs_write_all(probe, "safe", 4u) != 4 ||
            vfs_read_all(probe, restored, sizeof(restored)) != 4 ||
            memcmp(restored, "safe", 4u) != 0 ||
            vfs_unlink(probe) != VFS_OK ||
            vfs_mount(NULL, "/home-second", "homefs") != VFS_EBUSY) {
            if (alias >= 0) vfs_close(alias);
            serial_write_string("[FAIL] dglibc crossed the HomeFS container boundary\n");
            return 1;
        }
        serial_write_string("[PASS] dglibc HomeFS mount boundary\n");
    }

    /* HomeFS recursive walks stay within their documented depth. */
    {
        char path[128];
        uint32_t length = 5u;
        int created = 0;
        int okay = 1;
        int status;

        strcpy(path, "/home");
        homefs_seed_begin();
        while (created < 32) {
            path[length++] = '/';
            path[length++] = 'z';
            path[length++] = 'z';
            path[length] = '\0';
            if (vfs_mkdir(path) != VFS_OK) {
                length -= 3u;
                path[length] = '\0';
                okay = 0;
                break;
            }
            created++;
        }
        if (okay) {
            path[length++] = '/';
            path[length++] = 'z';
            path[length++] = 'z';
            path[length] = '\0';
            status = vfs_mkdir(path);
            if (status == VFS_OK) {
                created++;
                okay = 0;
            } else {
                length -= 3u;
                path[length] = '\0';
                if (status != VFS_ENOENT) okay = 0;
            }
        }
        while (created > 0) {
            if (vfs_unlink(path) != VFS_OK) okay = 0;
            length -= 3u;
            path[length] = '\0';
            created--;
        }
        homefs_seed_end();
        if (!okay) {
            serial_write_string("[FAIL] dglibc HomeFS depth boundary\n");
            return 1;
        }
        serial_write_string("[PASS] dglibc HomeFS depth boundary\n");
    }

    return 0;
}

int dglibc_test_main(void) {
    int result;
    int publish_status;

    if (homefs_batch_end() != VFS_EINVAL) {
        serial_write_string("[FAIL] dglibc accepted an unmatched HomeFS batch\n");
        return 1;
    }
    if (homefs_batch_begin() != VFS_OK) {
        serial_write_string("[FAIL] dglibc could not begin a HomeFS batch\n");
        return 1;
    }

    result = dglibc_test_body();
    publish_status = homefs_batch_end();
    if (result != 0) return result;
    if (publish_status != VFS_OK) {
        serial_write_string("[FAIL] dglibc could not publish its HomeFS batch\n");
        return 1;
    }
    serial_write_string("[PASS] dglibc HomeFS batch boundary\n");
    return 0;
}
