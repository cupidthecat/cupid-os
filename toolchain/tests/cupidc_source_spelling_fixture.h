#ifndef CUPIDC_SOURCE_SPELLING_FIXTURE_H
#define CUPIDC_SOURCE_SPELLING_FIXTURE_H

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* Compare ordinary C source fragments by tokens, preserving literal bytes and
 * operator boundaries while allowing formatting and comments between tokens. */
static int source_spelling_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

static int source_spelling_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int source_spelling_digit(char c) {
    return c >= '0' && c <= '9';
}

static const char *source_spelling_token(const char **cursor, size_t *size) {
    static const char *const operators[] = {"%:%:", ">>=", "<<=", "...", "->", "++", "--", "<<",
                                            ">>",   "<=",  ">=",  "==",  "!=", "&&", "||", "*=",
                                            "/=",   "%=",  "+=",  "-=",  "&=", "^=", "|=", "##",
                                            "<:",   ":>",  "<%",  "%>",  "%:"};
    const char *start;
    const char *p = *cursor;
    size_t index;
    *size = 0u;
    for (;;) {
        while (source_spelling_space(*p))
            p++;
        if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n')
                p++;
        } else if (p[0] == '/' && p[1] == '*') {
            p += 2;
            while (*p && !(p[0] == '*' && p[1] == '/'))
                p++;
            if (!*p) {
                *cursor = p;
                *size = (size_t)-1;
                return NULL;
            }
            p += 2;
        } else
            break;
    }
    start = p;
    if (!*p) {
        *cursor = p;
        return NULL;
    }
    if ((p[0] == 'u' || p[0] == 'U' || p[0] == 'L') && (p[1] == '"' || p[1] == '\''))
        p++;
    else if (p[0] == 'u' && p[1] == '8' && p[2] == '"')
        p += 2;
    if (*p == '"' || *p == '\'') {
        char quote = *p++;
        while (*p && *p != quote) {
            if (*p == '\\' && p[1])
                p++;
            p++;
        }
        if (!*p) {
            *cursor = p;
            *size = (size_t)-1;
            return NULL;
        }
        p++;
    } else if (source_spelling_letter(*p)) {
        while (source_spelling_letter(*p) || source_spelling_digit(*p))
            p++;
    } else if (source_spelling_digit(*p) || (*p == '.' && source_spelling_digit(p[1]))) {
        p++;
        while (source_spelling_letter(*p) || source_spelling_digit(*p) || *p == '.' ||
               ((*p == '+' || *p == '-') &&
                (p[-1] == 'e' || p[-1] == 'E' || p[-1] == 'p' || p[-1] == 'P')))
            p++;
    } else {
        for (index = 0u; index < sizeof(operators) / sizeof(operators[0]); index++) {
            size_t length = strlen(operators[index]);
            if (strncmp(p, operators[index], length) == 0) {
                p += length;
                break;
            }
        }
        if (p == start)
            p++;
    }
    *cursor = p;
    *size = (size_t)(p - start);
    return start;
}

static const char *source_find_spelling_end(const char *source, const char *fragment,
                                            const char **end_out) {
    const char *cursor = source;
    const char *candidate;
    size_t size;
    while ((candidate = source_spelling_token(&cursor, &size)) != NULL) {
        const char *actual_cursor = candidate;
        const char *expected_cursor = fragment;
        const char *expected;
        size_t expected_size;
        int matched = 0;
        while ((expected = source_spelling_token(&expected_cursor, &expected_size)) != NULL) {
            const char *actual = source_spelling_token(&actual_cursor, &size);
            matched = 1;
            if (actual == NULL || size != expected_size || memcmp(actual, expected, size) != 0) {
                matched = 0;
                break;
            }
        }
        if (matched && expected_size != (size_t)-1) {
            if (end_out != NULL)
                *end_out = actual_cursor;
            return candidate;
        }
    }
    return NULL;
}

static const char *source_find_spelling(const char *source, const char *fragment) {
    return source_find_spelling_end(source, fragment, NULL);
}

static int source_spelling_contract(void) {
    static const struct {
        const char *source;
        const char *fragment;
        int expected;
    } cases[] = {{"int f(void) {\n    return a + b;\n}", "return a+b;", 1},
                 {"return a /* comment */ +\n b;", "return a + b;", 1},
                 {"return a // comment\n + b;", "return a + b;", 1},
                 {"return a + +b;", "return a++b;", 0},
                 {"return a - -b;", "return a--b;", 0},
                 {"return unsignedint;", "return unsigned int;", 0},
                 {"return 1e+2;", "return 1e + 2;", 0},
                 {"return \"a b\";", "return \"ab\";", 0},
                 {"return '\\n';", "return 'n';", 0},
                 {"return L\"a\";", "return L \"a\";", 0},
                 {"/* return wrong; */ return right;", "return wrong;", 0},
                 {"const char *text = \"return wrong;\";", "return wrong;", 0},
                 {"return value;", "return /*", 0},
                 {"return value;", "return \"unfinished", 0},
                 {"int value;", "", 0}};

    size_t index;
    for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
        int found = source_find_spelling(cases[index].source, cases[index].fragment) != NULL;
        if (found != cases[index].expected) {
            (void)fprintf(stderr, "source-spelling case %u differs\n", (unsigned int)index);
            return 1;
        }
    }
    return 0;
}

#endif
