#include "string.h"

size_t strlen(const char* str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

int strcmp(const char* s1, const char* s2) {
    while(*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

void* memset(void* ptr, int value, size_t num) {
    unsigned char* p = ptr;
    while(num-- > 0) {
        *p++ = (unsigned char)value;
    }
    return ptr;
}

int strncmp(const char* s1, const char* s2, size_t n) {
    while (n > 0 && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) {
        return 0;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

void* memcpy(void* dest, const void* src, size_t n) {
    unsigned char* d = dest;
    const unsigned char* s = src;
    while (n-- > 0) {
        *d++ = *s++;
    }
    return dest;
}

char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++)) {}
    return dest;
}

char* strncpy(char* dest, const char* src, size_t n) {
    char* d = dest;
    while (n > 0 && *src) {
        *d++ = *src++;
        n--;
    }
    while (n > 0) {
        *d++ = '\0';
        n--;
    }
    return dest;
}

char* strcat(char* dest, const char* src) {
    char* d = dest;
    while (*d) d++;
    while ((*d++ = *src++)) {}
    return dest;
}

char* strchr(const char* s, int c) {
    char* p = (char*)(unsigned long)s;
    while (*p) {
        if (*p == (char)c) return p;
        p++;
    }
    return (c == 0) ? p : (char*)0;
}

char* strrchr(const char* s, int c) {
    char* last = (char*)0;
    char* p = (char*)(unsigned long)s;
    while (*p) {
        if (*p == (char)c) last = p;
        p++;
    }
    return (c == 0) ? p : last;
}

char* strstr(const char* haystack, const char* needle) {
    char* h0 = (char*)(unsigned long)haystack;
    if (!*needle) return h0;
    while (*h0) {
        const char* h = h0;
        const char* n = needle;
        while (*h && *n && *h == *n) {
            h++;
            n++;
        }
        if (!*n) return h0;
        h0++;
    }
    return (char*)0;
}

int memcmp(const void* s1, const void* s2, size_t n) {
    const unsigned char* p1 = s1;
    const unsigned char* p2 = s2;
    while (n-- > 0) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}

/* ──────────────────────────────────────────────────────────────────────
 * Phase D: printf %f support.
 *
 * str_floor: truncate toward -infinity via x87 FRNDINT with a
 * rounding-mode swap (RC=01, round down). Kept `static` so Phase E's
 * libm::floor can coexist without a link collision.
 *
 * fmt_f: write `v` to `out` in %f format with `prec` fractional digits.
 * Exposed to the kernel's printf-like functions (serial_printf,
 * cc_print_builtin, klog, cc_printline_builtin) so that %f / %.Nf
 * behave identically everywhere.  Handles NaN, ±Inf, negative, zero,
 * and rollover (0.999 → 1.000).
 * ────────────────────────────────────────────────────────────────── */

static double str_floor(double x) {
    double r;
    __asm__ volatile(
        "fldl %1\n\t"
        "fnstcw -2(%%esp)\n\t"
        "movw  -2(%%esp), %%ax\n\t"
        "movw  %%ax, -4(%%esp)\n\t"
        "andw  $0xF3FF, -4(%%esp)\n\t"
        "orw   $0x0400, -4(%%esp)\n\t"     /* RC=01 = round down */
        "fldcw -4(%%esp)\n\t"
        "frndint\n\t"
        "fldcw -2(%%esp)\n\t"
        "fstpl %0\n\t"
        : "=m"(r)
        : "m"(x)
        : "ax", "memory");
    return r;
}

int fmt_f(char *out, int out_max, double v, int prec) {
    int n = 0;
    if (out_max <= 0) return 0;
    if (prec < 0) prec = 6;
    if (prec > 17) prec = 17;

    /* NaN / Inf detection via IEEE-754 bit pattern. */
    uint64_t bits;
    memcpy(&bits, &v, 8);
    uint64_t exp_bits  = (bits >> 52) & 0x7FFull;
    uint64_t mant_bits = bits & ((1ull << 52) - 1);
    if (exp_bits == 0x7FF) {
        const char *s;
        if (mant_bits) {
            s = "nan";
        } else if (bits >> 63) {
            s = "-inf";
        } else {
            s = "inf";
        }
        while (*s && n < out_max) { out[n++] = *s++; }
        return n;
    }

    /* Sign. */
    if (v < 0.0) {
        if (n < out_max) out[n++] = '-';
        v = -v;
    }

    /* Split integer / fractional parts via x87 floor. */
    double int_part_d = str_floor(v);
    double frac_part  = v - int_part_d;

    /* uint64_t cast is only well-defined below ~2^63. */
    if (int_part_d >= 1.8e19) {
        const char *s = "(overflow)";
        while (*s && n < out_max) { out[n++] = *s++; }
        return n;
    }
    uint64_t int_part = (uint64_t)int_part_d;

    /* Scale fraction by 10^prec and round to nearest. */
    double scale = 1.0;
    for (int i = 0; i < prec; i++) {
        scale *= 10.0;
    }
    uint64_t frac_int = (uint64_t)str_floor(frac_part * scale + 0.5);

    /* Handle rollover (0.9999 with prec=3 → 1.000). */
    uint64_t scale_i = (uint64_t)scale;
    if (frac_int >= scale_i) {
        int_part++;
        frac_int -= scale_i;
    }

    /* Emit integer part (digits built in reverse, then reversed). */
    {
        char tmp[24];
        int tlen = 0;
        if (int_part == 0) {
            tmp[tlen++] = '0';
        } else {
            while (int_part > 0 && tlen < (int)sizeof(tmp)) {
                tmp[tlen++] = (char)('0' + (int)(int_part % 10));
                int_part /= 10;
            }
        }
        while (tlen > 0 && n < out_max) {
            out[n++] = tmp[--tlen];
        }
    }

    /* Fractional part. */
    if (prec > 0) {
        if (n < out_max) out[n++] = '.';
        char fbuf[24];
        int flen = 0;
        while (flen < prec && flen < (int)sizeof(fbuf)) {
            fbuf[flen++] = (char)('0' + (int)(frac_int % 10));
            frac_int /= 10;
        }
        while (flen > 0 && n < out_max) {
            out[n++] = fbuf[--flen];
        }
    }

    return n;
}

/* ──────────────────────────────────────────────────────────────────────
 * Phase D: printf %e support.
 *
 * fmt_e: write `v` to `out` in scientific notation with `prec`
 * fractional digits.  Mantissa is normalized to [1, 10) by repeated
 * /10 or *10 while tracking the exponent, then emitted via fmt_f.  The
 * exponent is written as 'e' + sign + two digits.
 * Handles NaN, ±Inf, negative, and zero (special-cased as 0.<zeros>e+00).
 * ────────────────────────────────────────────────────────────────── */

int fmt_e(char *out, int out_max, double v, int prec) {
    int n = 0;
    if (out_max <= 0) return 0;
    if (prec < 0) prec = 6;
    if (prec > 17) prec = 17;

    /* NaN / Inf detection via IEEE-754 bit pattern. */
    uint64_t bits;
    memcpy(&bits, &v, 8);
    uint64_t exp_bits  = (bits >> 52) & 0x7FFull;
    uint64_t mant_bits = bits & ((1ull << 52) - 1);
    if (exp_bits == 0x7FF) {
        const char *s;
        if (mant_bits) {
            s = "nan";
        } else if (bits >> 63) {
            s = "-inf";
        } else {
            s = "inf";
        }
        while (*s && n < out_max) { out[n++] = *s++; }
        return n;
    }

    /* Sign. */
    if (v < 0.0) {
        if (n < out_max) out[n++] = '-';
        v = -v;
    }

    /* Zero: emit "0.<prec zeros>e+00". */
    if (v == 0.0) {
        n += fmt_f(out + n, out_max - n, 0.0, prec);
        const char *suf = "e+00";
        while (*suf && n < out_max) { out[n++] = *suf++; }
        return n;
    }

    /* Normalize mantissa to [1, 10). */
    int exp = 0;
    while (v >= 10.0) { v /= 10.0; exp++; }
    while (v <  1.0)  { v *= 10.0; exp--; }

    /* Emit mantissa via fmt_f (now in [1, 10), positive). */
    n += fmt_f(out + n, out_max - n, v, prec);

    /* Exponent: 'e' + sign + 2 digits. */
    if (n < out_max) out[n++] = 'e';
    if (n < out_max) out[n++] = (exp < 0 ? '-' : '+');
    int abs_exp = exp < 0 ? -exp : exp;
    /* Clamp to 2 digits (double's exponent range fits easily, but be safe). */
    if (abs_exp > 99) abs_exp = 99;
    if (n < out_max) out[n++] = (char)('0' + (abs_exp / 10) % 10);
    if (n < out_max) out[n++] = (char)('0' + abs_exp % 10);

    return n;
}

/* ──────────────────────────────────────────────────────────────────────
 * Phase D: printf %g support.
 *
 * fmt_g: format `v` both as %f and %e at the same precision, copy the
 * shorter result into `out`.  On a tie we prefer the %f rendering.
 * Shares fmt_f / fmt_e so NaN / ±Inf / sign / zero handling stays
 * consistent with the individual converters.
 * ────────────────────────────────────────────────────────────────── */

/* Format double in '%g' style: shorter of %f vs %e.
 * If equal length, prefer %f. */
int fmt_g(char *out, int out_max, double v, int prec) {
    char buf_f[64];
    char buf_e[64];
    int len_f = fmt_f(buf_f, (int)sizeof(buf_f), v, prec);
    int len_e = fmt_e(buf_e, (int)sizeof(buf_e), v, prec);

    const char *pick;
    int len;
    if (len_f <= len_e) { pick = buf_f; len = len_f; }
    else                 { pick = buf_e; len = len_e; }

    int n = 0;
    while (n < len && n < out_max) {
        out[n] = pick[n];
        n++;
    }
    return n;
}