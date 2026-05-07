/* doom_libc_stubs.c
 * Minimal libc-like stubs needed by the DOOM source tree that aren't
 * provided by dglibc or the kernel's string.c.
 *
 * Task 12 — CupidOS DOOM port.
 * These are NOT built with -include dglibc_compat.h to avoid macro conflicts.
 */

#include "../types.h"
#include "../string.h"
#include "dglibc.h"
#include "../../drivers/serial.h"

/* ------------------------------------------------------------------ */
/* atoi / atol / atof / strtol / strtoul / strtod                     */
/* ------------------------------------------------------------------ */

int atoi(const char *s)
{
    if (!s) return 0;
    while (*s == ' ' || *s == '\t') s++;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }
    int val = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    return neg ? -val : val;
}

long atol(const char *s)
{
    return (long)atoi(s);
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
    if (!s) { if (endp) *endp = (char*)s; return 0; }
    while (*s == ' ' || *s == '\t') s++;
    long neg = 0;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }
    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (s[0] == '0') { base = 8; s++; }
        else { base = 10; }
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    long val = 0;
    while (1) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        val = val * base + d;
        s++;
    }
    if (endp) *endp = (char*)s;
    return neg ? -val : val;
}

unsigned long strtoul(const char *s, char **endp, int base)
{
    return (unsigned long)strtol(s, endp, base);
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

/* ------------------------------------------------------------------ */
/* memmove — kernel may only have memcpy                               */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/* strncat / strdup (in case kernel doesn't export them)              */
/* ------------------------------------------------------------------ */

char *strncat(char *dst, const char *src, size_t n)
{
    char *d = dst;
    while (*d) d++;
    while (n-- && *src) *d++ = *src++;
    *d = '\0';
    return dst;
}

/* ------------------------------------------------------------------ */
/* puts / vfprintf / sscanf stubs                                     */
/* ------------------------------------------------------------------ */

int puts(const char *s)
{
    dg_printf("%s\n", s);
    return 0;
}

int vfprintf(DG_FILE *f, const char *fmt, __builtin_va_list va)
{
    (void)f;
    return dg_vsnprintf((void*)0, 0, fmt, va);
}

/* sscanf — minimal implementation for DOOM's config parsing */
/* DOOM uses sscanf(s, "%i", &v) and sscanf(s, "%s", buf) and "%f" */
int sscanf(const char *s, const char *fmt, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int matched = 0;
    const char *p = s;
    while (*fmt && *p) {
        if (*fmt == '%') {
            fmt++;
            if (*fmt == 'i' || *fmt == 'd') {
                /* skip whitespace */
                while (*p == ' ' || *p == '\t') p++;
                int *out = __builtin_va_arg(ap, int*);
                int neg = 0;
                if (*p == '-') { neg = 1; p++; }
                int val = 0;
                while (*p >= '0' && *p <= '9') { val = val*10 + (*p-'0'); p++; }
                *out = neg ? -val : val;
                matched++;
            } else if (*fmt == 'u') {
                while (*p == ' ' || *p == '\t') p++;
                unsigned int *out = __builtin_va_arg(ap, unsigned int*);
                unsigned int val = 0;
                while (*p >= '0' && *p <= '9') { val = val*10 + (unsigned int)(*p-'0'); p++; }
                *out = val;
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

/* ------------------------------------------------------------------ */
/* POSIX file ops stubs (DOOM uses these for savegames)               */
/* ------------------------------------------------------------------ */

int remove(const char *path)
{
    /* stub — filesystem removal not needed for boot smoke */
    (void)path;
    return 0;
}

int rename(const char *old_path, const char *new_path)
{
    (void)old_path; (void)new_path;
    return 0;
}

int mkdir(const char *path, uint32_t mode)
{
    (void)path; (void)mode;
    return 0;
}

int system(const char *cmd)
{
    (void)cmd;
    return -1;
}

/* ------------------------------------------------------------------ */
/* 64-bit division helpers — GCC emits these for 64-bit ops on 32-bit */
/* __udivdi3 already lives in kernel/math.c.                          */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/* i_sound.h globals — SFX functions moved to i_sound_cupidos.c (Task 16) */
/* Music functions remain here until Task 17.                             */
/* ----------------------------------------------------------------------- */

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

/* I_*Music stubs removed — implemented in i_sound_cupidos.c (Task 17) */
