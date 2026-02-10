/**
 * cupidc_string.c — String utility functions for CupidC programs
 *
 * Provides additional string operations beyond the basic kernel string.h.
 * These are bound into CupidC as kernel API calls so that CupidC shell
 * and other programs can use them.
 */

#include "cupidc_string.h"
#include "memory.h"
#include "string.h"

/* ══════════════════════════════════════════════════════════════════════
 *  String duplication
 * ══════════════════════════════════════════════════════════════════════ */

char *cc_strdup(const char *s) {
  if (!s)
    return (char *)0;
  size_t len = strlen(s);
  char *dup = kmalloc(len + 1);
  if (!dup)
    return (char *)0;
  memcpy(dup, s, len + 1);
  return dup;
}

/* ══════════════════════════════════════════════════════════════════════
 *  String concatenation with length limit
 * ══════════════════════════════════════════════════════════════════════ */

char *cc_strncat(char *dst, const char *src, uint32_t n) {
  char *d = dst;
  while (*d)
    d++;
  while (n > 0 && *src) {
    *d++ = *src++;
    n--;
  }
  *d = '\0';
  return dst;
}

/* ══════════════════════════════════════════════════════════════════════
 *  String tokenization (static state — not reentrant)
 * ══════════════════════════════════════════════════════════════════════ */

static char *strtok_state = (char *)0;

char *cc_strtok(char *s, const char *delim) {
  if (s)
    strtok_state = s;
  if (!strtok_state)
    return (char *)0;

  /* Skip leading delimiters */
  while (*strtok_state) {
    const char *d = delim;
    int is_delim = 0;
    while (*d) {
      if (*strtok_state == *d) {
        is_delim = 1;
        break;
      }
      d++;
    }
    if (!is_delim)
      break;
    strtok_state++;
  }

  if (*strtok_state == '\0') {
    strtok_state = (char *)0;
    return (char *)0;
  }

  /* Start of token */
  char *token = strtok_state;

  /* Find end of token */
  while (*strtok_state) {
    const char *d = delim;
    while (*d) {
      if (*strtok_state == *d) {
        *strtok_state = '\0';
        strtok_state++;
        return token;
      }
      d++;
    }
    strtok_state++;
  }

  /* End of string */
  strtok_state = (char *)0;
  return token;
}

/* ══════════════════════════════════════════════════════════════════════
 *  String/number conversion
 * ══════════════════════════════════════════════════════════════════════ */

int cc_atoi(const char *s) {
  if (!s)
    return 0;

  /* Skip whitespace */
  while (*s == ' ' || *s == '\t')
    s++;

  int sign = 1;
  if (*s == '-') {
    sign = -1;
    s++;
  } else if (*s == '+') {
    s++;
  }

  int val = 0;
  while (*s >= '0' && *s <= '9') {
    val = val * 10 + (*s - '0');
    s++;
  }
  return sign * val;
}

char *cc_itoa(int n, char *buf) {
  if (!buf)
    return (char *)0;

  char tmp[16];
  int i = 0;
  int neg = 0;

  if (n < 0) {
    neg = 1;
    /* Handle INT_MIN carefully */
    if (n == -2147483647 - 1) {
      strcpy(buf, "-2147483648");
      return buf;
    }
    n = -n;
  }

  if (n == 0) {
    tmp[i++] = '0';
  } else {
    while (n > 0) {
      tmp[i++] = (char)('0' + n % 10);
      n /= 10;
    }
  }

  int j = 0;
  if (neg)
    buf[j++] = '-';

  while (i > 0)
    buf[j++] = tmp[--i];

  buf[j] = '\0';
  return buf;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Character classification
 * ══════════════════════════════════════════════════════════════════════ */

int cc_isspace(int c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
         c == '\v';
}

int cc_isdigit(int c) { return c >= '0' && c <= '9'; }

int cc_isalpha(int c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

int cc_isalnum(int c) { return cc_isalpha(c) || cc_isdigit(c); }

int cc_isupper(int c) { return c >= 'A' && c <= 'Z'; }

int cc_islower(int c) { return c >= 'a' && c <= 'z'; }

int cc_toupper(int c) {
  if (c >= 'a' && c <= 'z')
    return c - 32;
  return c;
}

int cc_tolower(int c) {
  if (c >= 'A' && c <= 'Z')
    return c + 32;
  return c;
}

/* ══════════════════════════════════════════════════════════════════════
 *  String utilities
 * ══════════════════════════════════════════════════════════════════════ */

/* Trim leading and trailing whitespace in-place. Returns s. */
char *cc_strtrim(char *s) {
  if (!s)
    return (char *)0;

  /* Skip leading whitespace */
  char *start = s;
  while (*start && cc_isspace(*start))
    start++;

  /* If all whitespace */
  if (*start == '\0') {
    s[0] = '\0';
    return s;
  }

  /* Find end */
  char *end = start;
  while (*end)
    end++;
  end--;
  while (end > start && cc_isspace(*end))
    end--;

  /* Move trimmed string to beginning */
  uint32_t len = (uint32_t)(end - start) + 1;
  if (start != s) {
    uint32_t i;
    for (i = 0; i < len; i++)
      s[i] = start[i];
  }
  s[len] = '\0';
  return s;
}

int cc_startswith(const char *s, const char *prefix) {
  if (!s || !prefix)
    return 0;
  while (*prefix) {
    if (*s != *prefix)
      return 0;
    s++;
    prefix++;
  }
  return 1;
}

int cc_endswith(const char *s, const char *suffix) {
  if (!s || !suffix)
    return 0;
  size_t slen = strlen(s);
  size_t suflen = strlen(suffix);
  if (suflen > slen)
    return 0;
  return strcmp(s + slen - suflen, suffix) == 0;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Memory move (handles overlapping regions)
 * ══════════════════════════════════════════════════════════════════════ */

void *cc_memmove(void *dst, const void *src, uint32_t n) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;

  if (d < s) {
    /* Copy forward */
    while (n--)
      *d++ = *s++;
  } else if (d > s) {
    /* Copy backward */
    d += n;
    s += n;
    while (n--)
      *(--d) = *(--s);
  }
  return dst;
}
