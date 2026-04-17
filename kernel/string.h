#ifndef STRING_H
#define STRING_H

#include "types.h"

size_t strlen(const char* str);
int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, size_t n);
void* memset(void* ptr, int value, size_t num);
void* memcpy(void* dest, const void* src, size_t n);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t n);
char* strcat(char* dest, const char* src);
char* strchr(const char* s, int c);
char* strrchr(const char* s, int c);
char* strstr(const char* haystack, const char* needle);
int memcmp(const void* s1, const void* s2, size_t n);

/* Phase D: printf %f / %.Nf helper.
 * Writes `v` to `out[0..out_max-1]` in %f format with `prec` decimal
 * digits (clamped to [0,17], default 6 when `prec < 0`).  Handles NaN,
 * ±Inf, negative, zero, and rollover.  Returns bytes written.
 *
 * Callers (serial_printf, cc_print_builtin, klog, cc_printline_builtin)
 * share this to guarantee %f is rendered identically everywhere.
 */
int fmt_f(char *out, int out_max, double v, int prec);

/* Phase D: printf %e / %.Ne helper.
 * Writes `v` to `out[0..out_max-1]` in scientific (mantissa.fractione±XX)
 * form with `prec` fractional digits (default 6).  Normalizes to [1,10)
 * via repeated /10 or *10 and emits a two-digit exponent.  Handles NaN,
 * ±Inf, negative, and zero.  Returns bytes written.
 */
int fmt_e(char *out, int out_max, double v, int prec);

/* Phase D: printf %g / %.Ng helper.
 * Formats `v` both as %f and %e (each with `prec` digits, default 6)
 * and copies whichever rendering is shorter into `out`.  Ties prefer %f.
 * Returns bytes written.
 */
int fmt_g(char *out, int out_max, double v, int prec);

#endif