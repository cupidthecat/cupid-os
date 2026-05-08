/**
 * cupidc_string.h - String utility functions for CupidC programs
 *
 * Provides additional string operations beyond the basic kernel string.h:
 *   - strdup (kmalloc'd copy)
 *   - strncat
 *   - strtok
 *   - atoi / itoa
 *   - Character classification (isspace, isdigit, isalpha, isalnum)
 *   - Utility helpers (strtrim, startswith, endswith)
 *   - memmove
 *
 * These are bound into CupidC programs as kernel API calls.
 */

#ifndef CUPIDC_STRING_H
#define CUPIDC_STRING_H

#include "types.h"

char *cc_strdup(const char *s);

char *cc_strncat(char *dst, const char *src, uint32_t n);

char *cc_strtok(char *s, const char *delim);

int cc_atoi(const char *s);
char *cc_itoa(int n, char *buf);

int cc_isspace(int c);
int cc_isdigit(int c);
int cc_isalpha(int c);
int cc_isalnum(int c);
int cc_isupper(int c);
int cc_islower(int c);
int cc_toupper(int c);
int cc_tolower(int c);

char *cc_strtrim(char *s);
int cc_startswith(const char *s, const char *prefix);
int cc_endswith(const char *s, const char *suffix);

void *cc_memmove(void *dst, const void *src, uint32_t n);

int cc_snprintf(char *buf, uint32_t size, const char *fmt, ...);

#endif /* CUPIDC_STRING_H */
