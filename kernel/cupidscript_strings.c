/*
 * cupidscript_strings.c - Advanced string operations for CupidScript
 *
 * Implements bash-like string manipulation:
 *   ${#var}          - String length
 *   ${var:start:len} - Substring extraction
 *   ${var%pattern}   - Suffix removal (shortest)
 *   ${var%%pattern}  - Suffix removal (longest)
 *   ${var#pattern}   - Prefix removal (shortest)
 *   ${var##pattern}  - Prefix removal (longest)
 *   ${var/old/new}   - Replace first occurrence
 *   ${var//old/new}  - Replace all occurrences
 *   ${var^^}         - Uppercase all
 *   ${var,,}         - Lowercase all
 *   ${var^}          - Capitalize first
 */

#include "cupidscript.h"
#include "string.h"
#include "memory.h"
#include "../drivers/serial.h"

static bool simple_match(const char *pattern, const char *str, int str_len) {
    int pi = 0, si = 0;
    int plen = 0;
    while (pattern[plen]) plen++;

    /* Handle * at end of pattern */
    if (plen > 0 && pattern[plen - 1] == '*') {
        /* Match prefix only */
        for (int i = 0; i < plen - 1 && i < str_len; i++) {
            if (pattern[i] != str[i] && pattern[i] != '?') return false;
        }
        return true;
    }

    /* Handle * at start of pattern */
    if (plen > 0 && pattern[0] == '*') {
        /* Match suffix only */
        const char *pat_rest = pattern + 1;
        int rest_len = plen - 1;
        if (rest_len > str_len) return false;
        for (int i = 0; i < rest_len; i++) {
            if (pat_rest[i] != str[str_len - rest_len + i] &&
                pat_rest[i] != '?') return false;
        }
        return true;
    }

    /* Exact match (with ? wildcards) */
    if (plen != str_len) return false;
    while (pi < plen && si < str_len) {
        if (pattern[pi] != str[si] && pattern[pi] != '?') return false;
        pi++;
        si++;
    }
    return pi == plen && si == str_len;
}

/* String length: ${#var} */
char *cs_string_length(const char *value) {
    char *result = kmalloc(16);
    if (!result) return NULL;

    int len = 0;
    while (value[len]) len++;

    /* int to string */
    if (len == 0) {
        result[0] = '0';
        result[1] = '\0';
    } else {
        char tmp[16];
        int i = 0;
        int v = len;
        while (v > 0 && i < 15) {
            tmp[i++] = (char)('0' + (v % 10));
            v /= 10;
        }
        int j = 0;
        while (i > 0) {
            result[j++] = tmp[--i];
        }
        result[j] = '\0';
    }
    return result;
}

/* Substring: ${var:start:len} */
char *cs_string_substring(const char *value, int start, int len) {
    int vlen = 0;
    while (value[vlen]) vlen++;

    /* Handle negative start (from end) */
    if (start < 0) {
        start = vlen + start;
        if (start < 0) start = 0;
    }

    if (start >= vlen) {
        char *r = kmalloc(1);
        if (r) r[0] = '\0';
        return r;
    }

    /* If len < 0 or not specified (use -1), go to end */
    int available = vlen - start;
    if (len < 0 || len > available) len = available;

    char *result = kmalloc((size_t)len + 1);
    if (!result) return NULL;

    for (int i = 0; i < len; i++) {
        result[i] = value[start + i];
    }
    result[len] = '\0';
    return result;
}

/* Suffix removal: ${var%pattern} (shortest) / ${var%%pattern} (longest) */
char *cs_string_remove_suffix(const char *value, const char *pattern,
                               bool longest) {
    int vlen = 0;
    while (value[vlen]) vlen++;

    int plen = 0;
    while (pattern[plen]) plen++;

    if (plen == 0 || vlen == 0) {
        char *r = kmalloc((size_t)vlen + 1);
        if (r) { memcpy(r, value, (size_t)vlen); r[vlen] = '\0'; }
        return r;
    }

    /* Try suffixes: for shortest, start from longest suffix;
     * for longest, start from shortest suffix (entire string) */
    int best_end = vlen;  /* How many chars to keep */

    if (longest) {
        /* Try removing the longest possible suffix */
        for (int i = 0; i <= vlen; i++) {
            if (simple_match(pattern, value + i, vlen - i)) {
                best_end = i;
                break;
            }
        }
    } else {
        /* Try removing the shortest possible suffix */
        for (int i = vlen; i >= 0; i--) {
            if (simple_match(pattern, value + i, vlen - i)) {
                best_end = i;
                break;
            }
        }
    }

    char *result = kmalloc((size_t)best_end + 1);
    if (!result) return NULL;
    memcpy(result, value, (size_t)best_end);
    result[best_end] = '\0';
    return result;
}

/* Prefix removal: ${var#pattern} (shortest) / ${var##pattern} (longest) */
char *cs_string_remove_prefix(const char *value, const char *pattern,
                               bool longest) {
    int vlen = 0;
    while (value[vlen]) vlen++;

    int plen = 0;
    while (pattern[plen]) plen++;

    if (plen == 0 || vlen == 0) {
        char *r = kmalloc((size_t)vlen + 1);
        if (r) { memcpy(r, value, (size_t)vlen); r[vlen] = '\0'; }
        return r;
    }

    int best_start = 0;

    if (longest) {
        /* Try removing the longest possible prefix */
        for (int i = vlen; i >= 0; i--) {
            if (simple_match(pattern, value, i)) {
                best_start = i;
                break;
            }
        }
    } else {
        /* Try removing the shortest possible prefix */
        for (int i = 1; i <= vlen; i++) {
            if (simple_match(pattern, value, i)) {
                best_start = i;
                break;
            }
        }
    }

    int result_len = vlen - best_start;
    char *result = kmalloc((size_t)result_len + 1);
    if (!result) return NULL;
    memcpy(result, value + best_start, (size_t)result_len);
    result[result_len] = '\0';
    return result;
}

/* String replacement: ${var/old/new} or ${var//old/new} */
char *cs_string_replace(const char *value, const char *pattern,
                         const char *replacement, bool replace_all) {
    int vlen = 0;
    while (value[vlen]) vlen++;

    int plen = 0;
    while (pattern[plen]) plen++;

    int rlen = 0;
    while (replacement[rlen]) rlen++;

    if (plen == 0) {
        char *r = kmalloc((size_t)vlen + 1);
        if (r) { memcpy(r, value, (size_t)vlen); r[vlen] = '\0'; }
        return r;
    }

    /* Worst case: every char replaced with longer string */
    size_t max_size = (size_t)(vlen * (rlen + 1)) + 1;
    if (max_size < 256) max_size = 256;
    char *result = kmalloc(max_size);
    if (!result) return NULL;

    int out = 0;
    int i = 0;
    bool replaced = false;

    while (i < vlen && (size_t)out < max_size - 1) {
        /* Check for pattern match at current position */
        if ((!replaced || replace_all) && i + plen <= vlen) {
            bool match = true;
            for (int j = 0; j < plen; j++) {
                if (value[i + j] != pattern[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                /* Copy replacement */
                for (int j = 0; j < rlen && (size_t)out < max_size - 1; j++) {
                    result[out++] = replacement[j];
                }
                i += plen;
                replaced = true;
                continue;
            }
        }
        result[out++] = value[i++];
    }

    result[out] = '\0';
    return result;
}

/* Case conversion: ${var^^} / ${var,,} / ${var^} */
char *cs_string_toupper(const char *value) {
    int len = 0;
    while (value[len]) len++;

    char *result = kmalloc((size_t)len + 1);
    if (!result) return NULL;

    for (int i = 0; i < len; i++) {
        if (value[i] >= 'a' && value[i] <= 'z') {
            result[i] = (char)(value[i] - 'a' + 'A');
        } else {
            result[i] = value[i];
        }
    }
    result[len] = '\0';
    return result;
}

char *cs_string_tolower(const char *value) {
    int len = 0;
    while (value[len]) len++;

    char *result = kmalloc((size_t)len + 1);
    if (!result) return NULL;

    for (int i = 0; i < len; i++) {
        if (value[i] >= 'A' && value[i] <= 'Z') {
            result[i] = (char)(value[i] - 'A' + 'a');
        } else {
            result[i] = value[i];
        }
    }
    result[len] = '\0';
    return result;
}

char *cs_string_capitalize(const char *value) {
    int len = 0;
    while (value[len]) len++;

    char *result = kmalloc((size_t)len + 1);
    if (!result) return NULL;

    memcpy(result, value, (size_t)len);
    result[len] = '\0';

    if (len > 0 && result[0] >= 'a' && result[0] <= 'z') {
        result[0] = (char)(result[0] - 'a' + 'A');
    }
    return result;
}

/* Main advanced variable expansion entry point
 * Called from cupidscript_runtime.c when ${...} is detected.
 * `expr` points to the content after "${" (before closing "}").
 */
char *cs_expand_advanced_var(const char *expr, script_context_t *ctx) {
    int elen = 0;
    while (expr[elen] && expr[elen] != '}') elen++;

    /* ${#var} - string length */
    if (expr[0] == '#') {
        char varname[MAX_VAR_NAME];
        int i = 0;
        int j = 1;
        while (j < elen && i < MAX_VAR_NAME - 1) {
            varname[i++] = expr[j++];
        }
        varname[i] = '\0';
        const char *val = cupidscript_get_variable(ctx, varname);
        return cs_string_length(val);
    }

    /* Extract variable name first (up to operator) */
    char varname[MAX_VAR_NAME];
    int vi = 0;
    int pos = 0;
    while (pos < elen && expr[pos] != ':' && expr[pos] != '%' &&
           expr[pos] != '#' && expr[pos] != '/' && expr[pos] != '^' &&
           expr[pos] != ',') {
        if (vi < MAX_VAR_NAME - 1) {
            varname[vi++] = expr[pos];
        }
        pos++;
    }
    varname[vi] = '\0';

    const char *value = cupidscript_get_variable(ctx, varname);
    if (!value) value = "";

    /* No operator - just return the variable value */
    if (pos >= elen) {
        int vlen = 0;
        while (value[vlen]) vlen++;
        char *r = kmalloc((size_t)vlen + 1);
        if (r) { memcpy(r, value, (size_t)vlen); r[vlen] = '\0'; }
        return r;
    }

    char op = expr[pos];

    /* ${var:start:len} - substring */
    if (op == ':') {
        pos++;  /* skip : */
        /* Parse start */
        int start = 0;
        bool neg_start = false;
        /* Skip spaces */
        while (pos < elen && expr[pos] == ' ') pos++;
        if (pos < elen && expr[pos] == '-') { neg_start = true; pos++; }
        while (pos < elen && expr[pos] >= '0' && expr[pos] <= '9') {
            start = start * 10 + (expr[pos] - '0');
            pos++;
        }
        if (neg_start) start = -start;

        int len = -1;  /* -1 = to end */
        if (pos < elen && expr[pos] == ':') {
            pos++;  /* skip second : */
            len = 0;
            while (pos < elen && expr[pos] >= '0' && expr[pos] <= '9') {
                len = len * 10 + (expr[pos] - '0');
                pos++;
            }
        }

        return cs_string_substring(value, start, len);
    }

    /* ${var%pattern} / ${var%%pattern} - suffix removal */
    if (op == '%') {
        pos++;
        bool longest = false;
        if (pos < elen && expr[pos] == '%') {
            longest = true;
            pos++;
        }
        char pattern[MAX_VAR_VALUE];
        int pi = 0;
        while (pos < elen && pi < MAX_VAR_VALUE - 1) {
            pattern[pi++] = expr[pos++];
        }
        pattern[pi] = '\0';
        return cs_string_remove_suffix(value, pattern, longest);
    }

    /* ${var#pattern} / ${var##pattern} - prefix removal */
    if (op == '#') {
        pos++;
        bool longest = false;
        if (pos < elen && expr[pos] == '#') {
            longest = true;
            pos++;
        }
        char pattern[MAX_VAR_VALUE];
        int pi = 0;
        while (pos < elen && pi < MAX_VAR_VALUE - 1) {
            pattern[pi++] = expr[pos++];
        }
        pattern[pi] = '\0';
        return cs_string_remove_prefix(value, pattern, longest);
    }

    /* ${var/old/new} / ${var//old/new} - replacement */
    if (op == '/') {
        pos++;
        bool replace_all = false;
        if (pos < elen && expr[pos] == '/') {
            replace_all = true;
            pos++;
        }
        /* Parse old pattern (up to next /) */
        char old_pat[MAX_VAR_VALUE];
        int oi = 0;
        while (pos < elen && expr[pos] != '/' && oi < MAX_VAR_VALUE - 1) {
            old_pat[oi++] = expr[pos++];
        }
        old_pat[oi] = '\0';

        /* Parse replacement */
        char new_pat[MAX_VAR_VALUE];
        int ni = 0;
        if (pos < elen && expr[pos] == '/') {
            pos++;  /* skip / */
            while (pos < elen && ni < MAX_VAR_VALUE - 1) {
                new_pat[ni++] = expr[pos++];
            }
        }
        new_pat[ni] = '\0';

        return cs_string_replace(value, old_pat, new_pat, replace_all);
    }

    /* ${var^^} / ${var,,} / ${var^} - case conversion */
    if (op == '^') {
        pos++;
        if (pos < elen && expr[pos] == '^') {
            return cs_string_toupper(value);
        }
        return cs_string_capitalize(value);
    }

    if (op == ',') {
        pos++;
        if (pos < elen && expr[pos] == ',') {
            return cs_string_tolower(value);
        }
        /* Single , - lowercase first char */
        int vlen = 0;
        while (value[vlen]) vlen++;
        char *r = kmalloc((size_t)vlen + 1);
        if (r) {
            memcpy(r, value, (size_t)vlen);
            r[vlen] = '\0';
            if (vlen > 0 && r[0] >= 'A' && r[0] <= 'Z') {
                r[0] = (char)(r[0] - 'A' + 'a');
            }
        }
        return r;
    }

    /* Fallback: just return the value */
    {
        int vlen = 0;
        while (value[vlen]) vlen++;
        char *r = kmalloc((size_t)vlen + 1);
        if (r) { memcpy(r, value, (size_t)vlen); r[vlen] = '\0'; }
        return r;
    }
}
