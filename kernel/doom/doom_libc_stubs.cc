/* doom_libc_stubs.cc
 * Minimal libc-like stubs needed by the DOOM source tree that are not
 * provided by dglibc or the kernel string implementation.
 *
 * The compatibility profile omits the forced dglibc_compat.h include to
 * avoid macro conflicts.
*/

#include "types.h"
#include "string.h"
#include "dglibc.h"
#include "serial.h"

/* atoi / atol / atof / strtol / strtoul / strtod */

#define DG_LONG_MAX  2147483647L
#define DG_LONG_MIN  (-2147483647L - 1L)
#define DG_ULONG_MAX 0xffffffffUL
#define DG_ERANGE     34
#define DG_EINVAL     22

static int digit_value(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return -1;
}

long strtol(const char *s, char **endp, int base);

int atoi(const char *s)
{
    return (int)strtol(s, NULL, 10);
}

long atol(const char *s)
{
    return strtol(s, NULL, 10);
}

double atof(const char *s)
{
    /* minimal floating point parse */
    if (!s) return 0.0;
    while (*s == ' ' || *s == '\t') s++;
    double sign = 1.0;
    if (*s == '-') { sign = -1.0; s++; }
    else if (*s == '+') { s++; }
    double val = 0.0;
    while (*s >= '0' && *s <= '9') { val = val * 10.0 + (*s - '0'); s++; }
    if (*s == '.') {
        s++;
        double frac = 0.1;
        while (*s >= '0' && *s <= '9') { val += (*s - '0') * frac; frac *= 0.1; s++; }
    }
    /* ignore exponent for now */
    return sign * val;
}

long strtol(const char *s, char **endp, int base)
{
    const char *original = s;
    const char *cursor = s;
    unsigned long value = 0u;
    unsigned long limit;
    unsigned long cutoff;
    unsigned long cutlim;
    int negative = 0;
    int converted = 0;
    int overflow = 0;

    if (!s) { if (endp) *endp = (char *)s; return 0; }
    if (base != 0 && (base < 2 || base > 36)) {
        dg_errno = DG_EINVAL;
        if (endp) *endp = (char *)original;
        return 0;
    }
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' ||
           *cursor == '\r' || *cursor == '\f' || *cursor == '\v') cursor++;
    if (*cursor == '-') { negative = 1; cursor++; }
    else if (*cursor == '+') { cursor++; }

    if ((base == 0 || base == 16) && cursor[0] == '0' &&
        (cursor[1] == 'x' || cursor[1] == 'X') &&
        digit_value(cursor[2]) >= 0 && digit_value(cursor[2]) < 16) {
        base = 16;
        cursor += 2;
    } else if (base == 0) {
        base = cursor[0] == '0' ? 8 : 10;
    }

    limit = negative ? 0x80000000UL : 0x7fffffffUL;
    cutoff = limit / (unsigned long)base;
    cutlim = limit % (unsigned long)base;
    while (1) {
        int digit = digit_value(*cursor);
        if (digit < 0 || digit >= base) break;
        converted = 1;
        if (value > cutoff ||
            (value == cutoff && (unsigned long)digit > cutlim)) {
            overflow = 1;
        } else if (!overflow) {
            value = value * (unsigned long)base + (unsigned long)digit;
        }
        cursor++;
    }

    if (!converted) {
        if (endp) *endp = (char *)original;
        return 0;
    }
    if (endp) *endp = (char *)cursor;
    if (overflow) {
        dg_errno = DG_ERANGE;
        return negative ? DG_LONG_MIN : DG_LONG_MAX;
    }
    if (negative && value == 0x80000000UL) return DG_LONG_MIN;
    return negative ? -(long)value : (long)value;
}

unsigned long strtoul(const char *s, char **endp, int base)
{
    const char *original = s;
    const char *cursor = s;
    unsigned long value = 0u;
    unsigned long cutoff;
    unsigned long cutlim;
    int negative = 0;
    int converted = 0;
    int overflow = 0;

    if (!s) { if (endp) *endp = (char *)s; return 0; }
    if (base != 0 && (base < 2 || base > 36)) {
        dg_errno = DG_EINVAL;
        if (endp) *endp = (char *)original;
        return 0;
    }
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' ||
           *cursor == '\r' || *cursor == '\f' || *cursor == '\v') cursor++;
    if (*cursor == '-') { negative = 1; cursor++; }
    else if (*cursor == '+') { cursor++; }

    if ((base == 0 || base == 16) && cursor[0] == '0' &&
        (cursor[1] == 'x' || cursor[1] == 'X') &&
        digit_value(cursor[2]) >= 0 && digit_value(cursor[2]) < 16) {
        base = 16;
        cursor += 2;
    } else if (base == 0) {
        base = cursor[0] == '0' ? 8 : 10;
    }

    cutoff = DG_ULONG_MAX / (unsigned long)base;
    cutlim = DG_ULONG_MAX % (unsigned long)base;
    while (1) {
        int digit = digit_value(*cursor);
        if (digit < 0 || digit >= base) break;
        converted = 1;
        if (value > cutoff ||
            (value == cutoff && (unsigned long)digit > cutlim)) {
            overflow = 1;
        } else if (!overflow) {
            value = value * (unsigned long)base + (unsigned long)digit;
        }
        cursor++;
    }

    if (!converted) {
        if (endp) *endp = (char *)original;
        return 0;
    }
    if (endp) *endp = (char *)cursor;
    if (overflow) {
        dg_errno = DG_ERANGE;
        return DG_ULONG_MAX;
    }
    return negative ? 0u - value : value;
}

double strtod(const char *s, char **endp)
{
    /* same as atof but update endp */
    const char *p = s;
    if (!p) { if (endp) *endp = (char*)s; return 0.0; }
    while (*p == ' ' || *p == '\t') p++;
    double sign = 1.0;
    if (*p == '-') { sign = -1.0; p++; }
    else if (*p == '+') { p++; }
    double val = 0.0;
    while (*p >= '0' && *p <= '9') { val = val * 10.0 + (*p - '0'); p++; }
    if (*p == '.') {
        p++;
        double frac = 0.1;
        while (*p >= '0' && *p <= '9') { val += (*p - '0') * frac; frac *= 0.1; p++; }
    }
    if (endp) *endp = (char*)p;
    return sign * val;
}

/* memmove - kernel may only have memcpy */

void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char*)dst;
    const unsigned char *s = (const unsigned char*)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

/* strncat / strdup (in case kernel doesn't export them) */

char *strncat(char *dst, const char *src, size_t n)
{
    char *d = dst;
    while (*d) d++;
    while (n-- && *src) *d++ = *src++;
    *d = '\0';
    return dst;
}

/* puts / vfprintf / sscanf stubs */

int puts(const char *s)
{
    dg_printf("%s\n", s);
    return 0;
}

int vfprintf(DG_FILE *f, const char *fmt, __builtin_va_list va)
{
    return dg_vfprintf(f, fmt, va);
}

/* sscanf - the conversions used by the active Doom tree. */
int sscanf(const char *s, const char *fmt, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int matched = 0;
    const char *p = s;
    while (*fmt && *p) {
        if (*fmt == '%') {
            fmt++;
            if (*fmt == 'i' || *fmt == 'd' || *fmt == 'x' ||
                *fmt == 'X' || *fmt == 'o') {
                char *end;
                int base = *fmt == 'i' ? 0 :
                           (*fmt == 'x' || *fmt == 'X' ? 16 :
                           (*fmt == 'o' ? 8 : 10));
                int *out = __builtin_va_arg(ap, int*);
                long value = strtol(p, &end, base);
                if (end == p) {
                    __builtin_va_end(ap);
                    return matched;
                }
                *out = (int)value;
                p = end;
                matched++;
            } else if (*fmt == 'u') {
                char *end;
                unsigned int *out = __builtin_va_arg(ap, unsigned int*);
                unsigned long value = strtoul(p, &end, 10);
                if (end == p) {
                    __builtin_va_end(ap);
                    return matched;
                }
                *out = (unsigned int)value;
                p = end;
                matched++;
            } else if (*fmt == 'f') {
                while (*p == ' ' || *p == '\t') p++;
                float *out = __builtin_va_arg(ap, float*);
                double sign = 1.0;
                if (*p == '-') { sign = -1.0; p++; }
                double val = 0.0;
                while (*p >= '0' && *p <= '9') { val = val*10.0 + (*p-'0'); p++; }
                if (*p == '.') { p++; double f=0.1; while(*p>='0'&&*p<='9'){val+=(*p-'0')*f;f*=0.1;p++;} }
                *out = (float)(sign * val);
                matched++;
            } else if (*fmt == 's') {
                while (*p == ' ' || *p == '\t') p++;
                char *out = __builtin_va_arg(ap, char*);
                while (*p && *p != ' ' && *p != '\t' && *p != '\n') *out++ = *p++;
                *out = '\0';
                matched++;
            } else if (*fmt == 'c') {
                char *out = __builtin_va_arg(ap, char*);
                *out = *p++;
                matched++;
            }
            fmt++;
        } else if (*fmt == ' ') {
            while (*p == ' ' || *p == '\t') p++;
            fmt++;
        } else {
            if (*fmt != *p) break;
            fmt++; p++;
        }
    }
    __builtin_va_end(ap);
    return matched;
}

int system(const char *cmd)
{
    (void)cmd;
    return -1;
}

/* 64-bit division helpers - GCC emits these for 64-bit ops on 32-bit */
/* __udivdi3 already lives in kernel/cpu/math.cc. */

/* Forward-declare kernel's __udivdi3 */
extern unsigned long long __udivdi3(unsigned long long a, unsigned long long b);

long long __divdi3(long long a, long long b)
{
    int neg = 0;
    unsigned long long ua = (unsigned long long)a;
    unsigned long long ub = (unsigned long long)b;
    if (a < 0) { ua = (unsigned long long)(-a); neg = !neg; }
    if (b < 0) { ub = (unsigned long long)(-b); neg = !neg; }
    unsigned long long q = __udivdi3(ua, ub);
    return neg ? -(long long)q : (long long)q;
}

long long __moddi3(long long a, long long b)
{
    long long q = __divdi3(a, b);
    return a - q * b;
}

unsigned long long __umoddi3(unsigned long long a, unsigned long long b)
{
    unsigned long long q = __udivdi3(a, b);
    return a - q * b;
}

/* Sound configuration globals shared with i_sound_cupidos.cc. */

int snd_musicdevice = 0;
int snd_sfxdevice   = 0;
int snd_sbport       = 0;
int snd_sbirq        = 0;
int snd_sbdma        = 0;
int snd_mport        = 0;
int snd_samplerate   = 22050;
int snd_cachesize    = 64 * 1024 * 1024;
int snd_maxslicetime_ms = 28;
char *snd_musiccmd   = (char*)"";
int snd_pitchshift   = 0;

void I_BindSoundVariables(void)        {}

/* Music entry points are implemented in i_sound_cupidos.cc. */
