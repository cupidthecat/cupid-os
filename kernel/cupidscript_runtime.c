/*
 * cupidscript_runtime.c - Runtime variable/function management for CupidScript
 *
 * Manages variable storage, function registry, and variable expansion.
 */
#include "cupidscript.h"
#include "string.h"
#include "memory.h"
#include "kernel.h"
#include "../drivers/serial.h"
#include "../drivers/rtc.h"

/* ══════════════════════════════════════════════════════════════════════
 *  Context initialization
 * ══════════════════════════════════════════════════════════════════════ */
void cupidscript_init_context(script_context_t *ctx) {
    memset(ctx, 0, sizeof(script_context_t));
    ctx->var_count = 0;
    ctx->func_count = 0;
    ctx->last_exit_status = 0;
    ctx->return_flag = 0;
    ctx->return_value = 0;
    ctx->script_argc = 0;
    ctx->script_name[0] = '\0';
    /* Initialize stream system */
    fd_table_init(&ctx->fd_table, ctx);
    /* Initialize job table */
    job_table_init(&ctx->jobs);
    /* Initialize arrays */
    ctx->array_count = 0;
    ctx->assoc_count = 0;
    /* Initialize color state */
    ansi_init(&ctx->color_state);
    /* Default output functions */
    ctx->print_fn = print;
    ctx->putchar_fn = putchar;
    ctx->print_int_fn = print_int;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Variable management
 * ══════════════════════════════════════════════════════════════════════ */

/* Helper: copy src to dst, at most (max-1) chars, null-terminate */
static void str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* Helper: integer to string */
static void int_to_str(int val, char *buf, int bufsize) {
    if (bufsize <= 0) return;
    if (val < 0) {
        buf[0] = '-';
        int_to_str(-val, buf + 1, bufsize - 1);
        return;
    }
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    char tmp[16];
    int i = 0;
    int v = val;
    while (v > 0 && i < 15) {
        tmp[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    int j = 0;
    while (i > 0 && j < bufsize - 1) {
        buf[j++] = tmp[--i];
    }
    buf[j] = '\0';
}

const char *cupidscript_get_variable(script_context_t *ctx, const char *name) {
    /* Special variables */
    if (name[0] == '?' && name[1] == '\0') {
        /* $? - last exit status: store in first variable slot as temp */
        static char status_buf[16];
        int_to_str(ctx->last_exit_status, status_buf, 16);
        return status_buf;
    }

    if (name[0] == '#' && name[1] == '\0') {
        /* $# - argument count */
        static char argc_buf[16];
        int_to_str(ctx->script_argc, argc_buf, 16);
        return argc_buf;
    }

    /* $0 - script name */
    if (name[0] == '0' && name[1] == '\0') {
        return ctx->script_name;
    }

    /* $1..$9 - positional arguments */
    if (name[0] >= '1' && name[0] <= '9' && name[1] == '\0') {
        int idx = name[0] - '1';
        if (idx < ctx->script_argc) {
            return ctx->script_args[idx];
        }
        return ""; /* undefined args expand to empty */
    }

    /* $EPOCHSECONDS - seconds since Unix epoch */
    if (strcmp(name, "EPOCHSECONDS") == 0) {
        static char epoch_buf[16];
        int_to_str((int)rtc_get_epoch_seconds(), epoch_buf, 16);
        return epoch_buf;
    }

    /* Regular variables */
    for (int i = 0; i < ctx->var_count; i++) {
        if (strcmp(ctx->variables[i].name, name) == 0) {
            return ctx->variables[i].value;
        }
    }

    return ""; /* undefined variables expand to empty (bash behavior) */
}

void cupidscript_set_variable(script_context_t *ctx, const char *name,
                              const char *value) {
    /* Check if variable already exists */
    for (int i = 0; i < ctx->var_count; i++) {
        if (strcmp(ctx->variables[i].name, name) == 0) {
            str_copy(ctx->variables[i].value, value, MAX_VAR_VALUE);
            KDEBUG("CupidScript: set %s = %s", name, value);
            return;
        }
    }

    /* Add new variable */
    if (ctx->var_count < MAX_VARIABLES) {
        str_copy(ctx->variables[ctx->var_count].name, name, MAX_VAR_NAME);
        str_copy(ctx->variables[ctx->var_count].value, value, MAX_VAR_VALUE);
        ctx->var_count++;
        KDEBUG("CupidScript: new %s = %s", name, value);
    } else {
        KERROR("CupidScript: too many variables (max %d)", MAX_VARIABLES);
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Variable expansion
 *
 *  Replaces $VAR patterns in a string with their values.
 *  Returns a kmalloc'd string — caller must kfree().
 * ══════════════════════════════════════════════════════════════════════ */

static int is_varname_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

/* Parse a string as an integer for arithmetic.  Returns 0 for empty or
 * non-numeric strings. */
static int parse_arith_int(const char *s) {
    if (!s || !*s) return 0;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    int v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return neg ? -v : v;
}

char *cupidscript_expand(const char *str, script_context_t *ctx) {
    char *result = kmalloc(MAX_EXPAND_LEN);
    if (!result) return NULL;

    int out = 0;
    int i = 0;
    int len = 0;
    while (str[len]) len++;

    while (i < len && out < MAX_EXPAND_LEN - 1) {
        if (str[i] == '$') {
            i++;
            if (i >= len) break;

            /* Special: $? $# $0-$9 */
            if (str[i] == '?' || str[i] == '#' ||
                (str[i] >= '0' && str[i] <= '9')) {
                char special[2];
                special[0] = str[i];
                special[1] = '\0';
                i++;
                const char *val = cupidscript_get_variable(ctx, special);
                int j = 0;
                while (val[j] && out < MAX_EXPAND_LEN - 1) {
                    result[out++] = val[j++];
                }
                continue;
            }

            /* $! — last background PID */
            if (str[i] == '!') {
                i++;
                char pid_str[16];
                uint32_t bg_pid = ctx->jobs.last_bg_pid;
                int pi = 0;
                if (bg_pid == 0) {
                    pid_str[0] = '0';
                    pi = 1;
                } else {
                    char tmp2[16];
                    int ti = 0;
                    while (bg_pid > 0 && ti < 15) {
                        tmp2[ti++] = (char)('0' + (bg_pid % 10));
                        bg_pid /= 10;
                    }
                    while (ti > 0) pid_str[pi++] = tmp2[--ti];
                }
                pid_str[pi] = '\0';
                int j = 0;
                while (pid_str[j] && out < MAX_EXPAND_LEN - 1) {
                    result[out++] = pid_str[j++];
                }
                continue;
            }

            /* ${...} — advanced variable operations */
            if (str[i] == '{') {
                i++; /* skip { */
                /* Find matching } */
                int expr_start = i;
                int depth = 1;
                while (i < len && depth > 0) {
                    if (str[i] == '{') depth++;
                    if (str[i] == '}') depth--;
                    if (depth > 0) i++;
                }
                /* Extract expression (without closing }) */
                int expr_len = i - expr_start;
                if (i < len) i++; /* skip } */

                char expr[MAX_EXPAND_LEN];
                int eidx = 0;
                for (int k = 0; k < expr_len && k < MAX_EXPAND_LEN - 1; k++) {
                    expr[eidx++] = str[expr_start + k];
                }
                expr[eidx] = '\0';

                /* Delegate to advanced string operations */
                char *expanded = cs_expand_advanced_var(expr, ctx);
                if (expanded) {
                    int j = 0;
                    while (expanded[j] && out < MAX_EXPAND_LEN - 1) {
                        result[out++] = expanded[j++];
                    }
                    kfree(expanded);
                }
                continue;
            }

            /* $((expr)) — arithmetic */
            if (str[i] == '(' && i + 1 < len && str[i + 1] == '(') {
                /* Find matching )) */
                i += 2;
                char expr[MAX_TOKEN_LEN];
                int eidx = 0;
                int depth = 1;
                while (i < len && depth > 0) {
                    if (str[i] == ')' && i + 1 < len && str[i + 1] == ')') {
                        depth--;
                        if (depth == 0) { i += 2; break; }
                    }
                    if (eidx < MAX_TOKEN_LEN - 1) {
                        expr[eidx++] = str[i];
                    }
                    i++;
                }
                expr[eidx] = '\0';

                /* Expand variables within the expression first */
                char *expanded_expr = cupidscript_expand(expr, ctx);
                if (!expanded_expr) continue;

                /* Simple arithmetic evaluator for expanded expression.
                 * Supports: +, -, *, /, % with integer operands.
                 * Operands can be numbers or bare variable names
                 * (bash allows both $VAR and VAR inside $(())). */

                /* Helper: parse one operand (number or variable) */
                #define ARITH_PARSE_OPERAND(ptr, result_var) do {      \
                    while (*(ptr) == ' ') (ptr)++;                     \
                    int _neg = 0;                                      \
                    if (*(ptr) == '-') { _neg = 1; (ptr)++; }          \
                    if (*(ptr) >= '0' && *(ptr) <= '9') {              \
                        (result_var) = 0;                              \
                        while (*(ptr) >= '0' && *(ptr) <= '9') {       \
                            (result_var) = (result_var) * 10 +         \
                                           (*(ptr) - '0');             \
                            (ptr)++;                                   \
                        }                                              \
                    } else if (is_varname_char(*(ptr))) {              \
                        char _vn[MAX_VAR_NAME];                        \
                        int _vi = 0;                                   \
                        while (is_varname_char(*(ptr)) &&              \
                               _vi < MAX_VAR_NAME - 1) {              \
                            _vn[_vi++] = *(ptr);                       \
                            (ptr)++;                                   \
                        }                                              \
                        _vn[_vi] = '\0';                               \
                        const char *_vv =                              \
                            cupidscript_get_variable(ctx, _vn);        \
                        (result_var) = parse_arith_int(_vv);           \
                    } else {                                           \
                        (result_var) = 0;                              \
                    }                                                  \
                    if (_neg) (result_var) = -(result_var);            \
                } while (0)

                int val = 0;
                const char *ep = expanded_expr;

                ARITH_PARSE_OPERAND(ep, val);

                /* Parse operator and second operand if present */
                while (*ep == ' ') ep++;
                if (*ep) {
                    char op = *ep;
                    ep++;

                    int val2 = 0;
                    ARITH_PARSE_OPERAND(ep, val2);

                    switch (op) {
                    case '+': val = val + val2; break;
                    case '-': val = val - val2; break;
                    case '*': val = val * val2; break;
                    case '/': if (val2 != 0) val = val / val2; break;
                    case '%': if (val2 != 0) val = val % val2; break;
                    default: break;
                    }
                }

                #undef ARITH_PARSE_OPERAND

                kfree(expanded_expr);

                /* Convert result to string */
                char numbuf[16];
                int_to_str(val, numbuf, 16);
                int j = 0;
                while (numbuf[j] && out < MAX_EXPAND_LEN - 1) {
                    result[out++] = numbuf[j++];
                }
                continue;
            }

            /* Regular variable name */
            char varname[MAX_VAR_NAME];
            int vi = 0;
            while (i < len && is_varname_char(str[i]) &&
                   vi < MAX_VAR_NAME - 1) {
                varname[vi++] = str[i++];
            }
            varname[vi] = '\0';

            if (vi > 0) {
                const char *val = cupidscript_get_variable(ctx, varname);
                int j = 0;
                while (val[j] && out < MAX_EXPAND_LEN - 1) {
                    result[out++] = val[j++];
                }
            }
        } else if (str[i] == '\\' && i + 1 < len) {
            /* Escape sequences */
            i++;
            switch (str[i]) {
            case 'n': result[out++] = '\n'; break;
            case 't': result[out++] = '\t'; break;
            case '\\': result[out++] = '\\'; break;
            case '$': result[out++] = '$'; break;
            case '"': result[out++] = '"'; break;
            default: result[out++] = str[i]; break;
            }
            i++;
        } else {
            result[out++] = str[i++];
        }
    }

    result[out] = '\0';
    return result;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Function management
 * ══════════════════════════════════════════════════════════════════════ */

void cupidscript_register_function(script_context_t *ctx, const char *name,
                                   ast_node_t *body) {
    /* Check if function already exists — update it */
    for (int i = 0; i < ctx->func_count; i++) {
        if (strcmp(ctx->functions[i].name, name) == 0) {
            ctx->functions[i].body = body;
            KDEBUG("CupidScript: updated function '%s'", name);
            return;
        }
    }

    if (ctx->func_count < MAX_FUNCTIONS) {
        str_copy(ctx->functions[ctx->func_count].name, name, MAX_VAR_NAME);
        ctx->functions[ctx->func_count].body = body;
        ctx->func_count++;
        KDEBUG("CupidScript: registered function '%s'", name);
    } else {
        KERROR("CupidScript: too many functions (max %d)", MAX_FUNCTIONS);
    }
}

ast_node_t *cupidscript_lookup_function(script_context_t *ctx,
                                        const char *name) {
    for (int i = 0; i < ctx->func_count; i++) {
        if (strcmp(ctx->functions[i].name, name) == 0) {
            return ctx->functions[i].body;
        }
    }
    return NULL;
}
