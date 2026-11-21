/**
 * @file dglibc.cc
 * @brief Connect Doom's libc calls to Cupid OS kernel APIs.
 *
 * Maps malloc/free, fopen/fread/fseek/ftell, snprintf/sprintf/printf,
 * setjmp/longjmp, ctype, strcasecmp, qsort, time, and getenv.
 *
 * Compiled through the checked Doom compatibility CupidC profile.
 */

#include "dglibc.h"
#include "blockcache.h"
#include "fat16.h"
#include "fat16_control.h"
#include "homefs.h"
#include "memory.h"
#include "serial.h"
#include "string.h"
#include "timer.h"
#include "vfs.h"
#include "vfs_helpers.h"

/* The freestanding shim uses the compiler's variadic builtins. */
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)

/* Internal DG_FILE struct */

#define DG_FILE_MAGIC 0xD600F11E

struct DG_FILE {
    uint32_t magic;
    int fd; /* vfs fd, or -1 for stdin/stdout/stderr */
    int is_eof;
    int is_err;
    int32_t pos; /* tracked position for stdin/stdout/stderr */
};

/* Static sentinel file objects for stdin/stdout/stderr */
static struct DG_FILE s_stdin_obj = {DG_FILE_MAGIC, -1, 0, 0, 0};
static struct DG_FILE s_stdout_obj = {DG_FILE_MAGIC, -2, 0, 0, 0};
static struct DG_FILE s_stderr_obj = {DG_FILE_MAGIC, -3, 0, 0, 0};

DG_FILE *dg_stdin = &s_stdin_obj;
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
static int s_exit_armed = 0;

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

__asm__(".global dg_setjmp\n"
        "dg_setjmp:\n"
        "    movl  4(%esp), %eax\n" /* eax = &env[0] */
        "    movl  %ebx,  0(%eax)\n"
        "    movl  %esi,  4(%eax)\n"
        "    movl  %edi,  8(%eax)\n"
        "    movl  %ebp, 12(%eax)\n"
        "    leal  4(%esp), %ecx\n"
        "    movl  %ecx, 16(%eax)\n"
        "    movl  (%esp), %ecx\n" /* ecx = return address */
        "    movl  %ecx, 20(%eax)\n"
        "    xorl  %eax, %eax\n" /* return 0 */
        "    ret\n"

        ".global dg_longjmp\n"
        "dg_longjmp:\n"
        "    movl  4(%esp), %eax\n" /* eax = &env[0] */
        "    movl  8(%esp), %ecx\n" /* ecx = val */
        "    testl %ecx, %ecx\n"
        "    jnz   1f\n"
        "    movl  $1, %ecx\n" /* val=0 becomes 1 (POSIX) */
        "1:\n"
        "    movl  0(%eax), %ebx\n"
        "    movl  4(%eax), %esi\n"
        "    movl  8(%eax), %edi\n"
        "    movl 12(%eax), %ebp\n"
        "    movl 16(%eax), %esp\n"
        "    movl 20(%eax), %edx\n" /* saved eip */
        "    movl  %ecx,  %eax\n"   /* return val in eax */
        "    jmp  *%edx\n");

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
    uint32_t magic; /* DG_ALLOC_MAGIC - sanity check on free/realloc */
} dg_alloc_hdr_t;

#define DG_ALLOC_MAGIC 0xDA110CADu

void *dg_malloc(uint32_t n) {
    dg_alloc_hdr_t *h;
    if (n == 0) {
        n = 1;
    }
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
    if (total == 0) {
        total = 1;
    }
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
    if (!p) {
        return dg_malloc(newsz);
    }
    if (newsz == 0) {
        dg_free(p);
        return NULL;
    }
    h = ((dg_alloc_hdr_t *)p) - 1;
    if (h->magic != DG_ALLOC_MAGIC) {
        serial_write_string("[dglibc] realloc rejected a foreign or damaged pointer\n");
        dg_errno = DG_EINVAL;
        return NULL;
    }
    oldsz = h->size;
    newp = dg_malloc(newsz);
    if (!newp) {
        return NULL;
    }
    copy = (oldsz < newsz) ? oldsz : newsz;
    memcpy(newp, p, (size_t)copy);
    dg_free(p);
    return newp;
}

void dg_free(void *p) {
    dg_alloc_hdr_t *h;
    if (!p) {
        return;
    }
    h = ((dg_alloc_hdr_t *)p) - 1;
    if (h->magic != DG_ALLOC_MAGIC) {
        serial_write_string("[dglibc] free rejected a foreign or damaged pointer\n");
        return;
    }
    h->magic = 0; /* poison so double-free is caught */
    kfree(h);
}

char *dg_strdup(const char *s) {
    char *d;
    size_t len;
    if (!s) {
        return NULL;
    }
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
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v');
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
    if (c >= 'A' && c <= 'Z') {
        return c + 32;
    }
    return c;
}

int dg_toupper(int c) {
    if (c >= 'a' && c <= 'z') {
        return c - 32;
    }
    return c;
}

/* String helpers */

int dg_strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = dg_tolower((unsigned char)*a);
        int cb = dg_tolower((unsigned char)*b);
        if (ca != cb) {
            return ca - cb;
        }
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
        if (ca != cb) {
            return ca - cb;
        }
    }
    if (i == n) {
        return 0;
    }
    return dg_tolower((unsigned char)*a) - dg_tolower((unsigned char)*b);
}

/* env / time */

char *dg_getenv(const char *name) {
    (void)name;
    return NULL; /* No environment in kernel */
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

#define EMIT(ch)                                                                                   \
    do {                                                                                           \
        if (pos + 1 < cap) {                                                                       \
            out[pos] = (char)(ch);                                                                 \
        }                                                                                          \
        pos++;                                                                                     \
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
            if (ch == '0') {
                zero_pad = 1;
            }
            if (ch == '-') {
                left_align = 1;
            }
            fmt++;
            ch = *fmt;
        }
        if (left_align) {
            zero_pad = 0;
        }

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
        if (*fmt == 'l' || *fmt == 'h') {
            fmt++;
        }
        if (*fmt == 'l' || *fmt == 'h') {
            fmt++;
        }

        char spec = *fmt++;

        if (spec == '%') {
            EMIT('%');
            continue;
        }

        if (spec == 'c') {
            int c = va_arg(ap, int);
            /* Width padding */
            if (!left_align && width > 1) {
                int p;
                for (p = 0; p < width - 1; p++) {
                    EMIT(' ');
                }
            }
            EMIT(c);
            if (left_align && width > 1) {
                int p;
                for (p = 0; p < width - 1; p++) {
                    EMIT(' ');
                }
            }
            continue;
        }

        if (spec == 's') {
            const char *s = va_arg(ap, const char *);
            if (!s) {
                s = "(null)";
            }
            int slen = 0;
            const char *sp = s;
            while (*sp) {
                slen++;
                sp++;
            }
            if (prec >= 0 && slen > prec) {
                slen = prec;
            }
            int pad = (width > slen) ? (width - slen) : 0;
            if (!left_align) {
                int p;
                for (p = 0; p < pad; p++) {
                    EMIT(' ');
                }
            }
            int k;
            for (k = 0; k < slen; k++) {
                EMIT(s[k]);
            }
            if (left_align) {
                int p;
                for (p = 0; p < pad; p++) {
                    EMIT(' ');
                }
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
                for (p = 0; p < pad; p++) {
                    EMIT(padding);
                }
            }
            {
                int k;
                for (k = 0; k < flen; k++) {
                    EMIT(float_buf[k]);
                }
            }
            if (left_align) {
                int p;
                for (p = 0; p < pad; p++) {
                    EMIT(' ');
                }
            }
            continue;
        }

        /* Integer specifiers */
        if (spec == 'd' || spec == 'i' || spec == 'u' || spec == 'x' || spec == 'X' ||
            spec == 'p') {

            char buf[32];
            int blen = 0;
            int neg = 0;
            uint32_t uval;

            if (spec == 'p') {
                uval = (uint32_t)(size_t)va_arg(ap, void *);
                spec = 'x';
            } else if (spec == 'd' || spec == 'i') {
                int ival = va_arg(ap, int);
                if (ival < 0) {
                    neg = 1;
                    uval = 0u - (uint32_t)ival;
                } else {
                    uval = (uint32_t)ival;
                }
            } else {
                uval = va_arg(ap, uint32_t);
            }

            if (spec == 'x' || spec == 'X') {
                const char *hex = (spec == 'x') ? "0123456789abcdef" : "0123456789ABCDEF";
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
                char tmp = buf[lo];
                buf[lo] = buf[hi];
                buf[hi] = tmp;
                lo++;
                hi--;
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
                    if (neg) {
                        EMIT('-');
                    }
                    int p;
                    for (p = 0; p < pad; p++) {
                        EMIT('0');
                    }
                } else {
                    int p;
                    for (p = 0; p < pad; p++) {
                        EMIT(' ');
                    }
                    if (neg) {
                        EMIT('-');
                    }
                }
            } else {
                if (neg) {
                    EMIT('-');
                }
            }

            {
                int p;
                for (p = 0; p < prec_pad; p++) {
                    EMIT('0');
                }
            }
            int k;
            for (k = 0; k < blen; k++) {
                EMIT(buf[k]);
            }

            if (left_align) {
                int p;
                for (p = 0; p < pad; p++) {
                    EMIT(' ');
                }
            }
            continue;
        }

        /* Unknown specifier - emit as-is */
        EMIT('%');
        EMIT(spec);
    }

    /* NUL-terminate */
    if (cap > 0) {
        if (pos < cap) {
            out[pos] = '\0';
        } else {
            out[cap - 1] = '\0';
        }
    }

    return (int)pos;

#undef EMIT
}

/* printf family */

int dg_vsnprintf(char *s, uint32_t n, const char *fmt, void *va) {
    if (!s || n == 0) {
        return 0;
    }
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
        if (f && f->magic == DG_FILE_MAGIC) {
            f->is_err = 1;
        }
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
        if (f && f->magic == DG_FILE_MAGIC) {
            f->is_err = 1;
        }
        return -1;
    }
    if (dg_fwrite(buf, 1u, (uint32_t)ret, f) != (uint32_t)ret) {
        return -1;
    }
    return ret;
}

/* stdio - file ops */

static const char *dg_home_path(const char *name, char *buffer, uint32_t buffer_size) {
    const char *prefix = "/home/doom";
    uint32_t used = 0;

    if (!buffer || buffer_size == 0u) {
        return NULL;
    }
    while (*prefix) {
        if (used + 1u >= buffer_size) {
            return NULL;
        }
        buffer[used++] = *prefix++;
    }
    if (name && name[0]) {
        if (used + 1u >= buffer_size) {
            return NULL;
        }
        buffer[used++] = '/';
        while (*name) {
            if (used + 1u >= buffer_size) {
                return NULL;
            }
            buffer[used++] = *name++;
        }
    }
    buffer[used] = '\0';
    return buffer;
}

static const char *dg_resolve_path(const char *path, char *buffer, uint32_t buffer_size) {
    const char *relative;
    const char *base;
    uint32_t len;

    if (!path) {
        return NULL;
    }
    if (path[0] == '/') {
        return path;
    }

    relative = path;
    if (relative[0] == '.' && relative[1] == '/') {
        relative += 2;
    }
    if (relative[0] == '\0' || (relative[0] == '.' && relative[1] == '\0')) {
        return dg_home_path(NULL, buffer, buffer_size);
    }
    if (strcmp(relative, ".savegame") == 0 || strcmp(relative, ".savegame/") == 0) {
        return dg_home_path(NULL, buffer, buffer_size);
    }
    if (strncmp(relative, ".savegame/", 10u) == 0) {
        base = relative + 10;
        if (base[0] == '\0' || strcmp(base, ".") == 0 || strcmp(base, "..") == 0 ||
            strchr(base, '/')) {
            return NULL;
        }
        return dg_home_path(base, buffer, buffer_size);
    }

    len = (uint32_t)strlen(relative);
    if (!strchr(relative, '/') && len >= 4u && strcmp(relative + len - 4u, ".cfg") == 0) {
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

    if (!mode || !flags_out || mode[0] == '\0') {
        return 0;
    }
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
    if (status == VFS_OK) {
        return 0;
    }
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

    f->magic = DG_FILE_MAGIC;
    f->fd = fd;
    f->is_eof = 0;
    f->is_err = 0;
    f->pos = 0;
    return f;
}

int remove(const char *path) {
    char fixed[128];
    vfs_stat_t st;
    int status;
    const char *resolved = dg_resolve_path(path, fixed, (uint32_t)sizeof(fixed));
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
    const char *old_resolved = dg_resolve_path(old_path, old_fixed, (uint32_t)sizeof(old_fixed));
    const char *new_resolved = dg_resolve_path(new_path, new_fixed, (uint32_t)sizeof(new_fixed));
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
    const char *resolved = dg_resolve_path(path, fixed, (uint32_t)sizeof(fixed));
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
    if (f->fd >= 0) {
        status = vfs_close(f->fd);
    }
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
    if (sz == 0 || n == 0) {
        return 0;
    }
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
        if (request > 0x7fffffffu)
            request = 0x7fffffffu;
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
    if (sz == 0 || n == 0) {
        return 0;
    }
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
        if (request > 0x7fffffffu)
            request = 0x7fffffffu;
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
    if (!f) {
        return 1;
    }
    return f->is_eof;
}

void dg_clearerr(DG_FILE *f) {
    if (!f) {
        return;
    }
    f->is_eof = 0;
    f->is_err = 0;
}

char *dg_fgets(char *s, int n, DG_FILE *f) {
    int i;
    if (!s || n <= 0 || !f) {
        return NULL;
    }
    for (i = 0; i < n - 1; i++) {
        int c = dg_fgetc(f);
        if (c < 0) {
            if (i == 0) {
                return NULL;
            }
            break;
        }
        s[i] = (char)c;
        if (c == '\n') {
            i++;
            break;
        }
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
    if (got == 0) {
        f->is_eof = 1;
        return -1;
    }
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
        if (f && f->magic == DG_FILE_MAGIC) {
            f->is_err = 1;
        }
        return -1;
    }
    length = (uint32_t)strlen(s);
    if (dg_fwrite(s, 1u, length, f) != length) {
        return -1;
    }
    return 0;
}

int dg_fflush(DG_FILE *f) {
    if (!f) {
        return 0;
    }
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
        for (j = i; j > 0 && cmp(base + (j - 1) * sz, base + j * sz) > 0; j--) {
            swap_bytes(base + (j - 1) * sz, base + j * sz, sz);
        }
    }
}

void dg_qsort(void *base, uint32_t n, uint32_t sz, int (*cmp)(const void *, const void *)) {
    struct {
        uint32_t lo;
        uint32_t hi;
    } stack[QS_STACK_DEPTH];

    int top = 0;

    if (!base || n < 2 || sz == 0 || !cmp) {
        return;
    }

    stack[top].lo = 0;
    stack[top].hi = n - 1;
    top++;

    while (top > 0) {
        top--;
        uint32_t lo = stack[top].lo;
        uint32_t hi = stack[top].hi;

        if (hi <= lo) {
            continue;
        }
        uint32_t len = hi - lo + 1;

        if (len <= QS_INSERTION_THRESHOLD) {
            insertion_sort((char *)base + lo * sz, len, sz, cmp);
            continue;
        }

        /* Median-of-three pivot */
        uint32_t mid = lo + len / 2;
        char *p_lo = (char *)base + lo * sz;
        char *p_mid = (char *)base + mid * sz;
        char *p_hi = (char *)base + hi * sz;
        if (cmp(p_lo, p_mid) > 0) {
            swap_bytes(p_lo, p_mid, sz);
        }
        if (cmp(p_lo, p_hi) > 0) {
            swap_bytes(p_lo, p_hi, sz);
        }
        if (cmp(p_mid, p_hi) > 0) {
            swap_bytes(p_mid, p_hi, sz);
        }
        /* pivot is now at mid; move to hi-1 for partitioning */
        swap_bytes(p_mid, (char *)base + (hi - 1) * sz, sz);
        char *pivot = (char *)base + (hi - 1) * sz;

        uint32_t i = lo;
        uint32_t j = hi - 1;

        while (1) {
            while (++i < hi && cmp((char *)base + i * sz, pivot) < 0) {
            }
            while (j > lo && cmp((char *)base + --j * sz, pivot) > 0) {
            }
            if (i >= j) {
                break;
            }
            swap_bytes((char *)base + i * sz, (char *)base + j * sz, sz);
        }
        /* Restore pivot */
        swap_bytes((char *)base + i * sz, pivot, sz);

        /* Push larger partition last so smaller is processed first */
        if (top + 2 < QS_STACK_DEPTH) {
            if (i - lo > hi - i) {
                if (i > lo + 1) {
                    stack[top].lo = lo;
                    stack[top].hi = i - 1;
                    top++;
                }
                if (i + 1 < hi) {
                    stack[top].lo = i + 1;
                    stack[top].hi = hi;
                    top++;
                }
            } else {
                if (i + 1 < hi) {
                    stack[top].lo = i + 1;
                    stack[top].hi = hi;
                    top++;
                }
                if (i > lo + 1) {
                    stack[top].lo = lo;
                    stack[top].hi = i - 1;
                    top++;
                }
            }
        }
    }
}

#include "dglibc_selftest.inc"
