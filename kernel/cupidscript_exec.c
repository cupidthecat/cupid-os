/*
 * cupidscript_exec.c - Interpreter/executor for CupidScript
 *
 * Walks the AST and executes nodes: commands, assignments,
 * conditionals, loops, functions, and test expressions.
 * Also contains the top-level cupidscript_run_file() entry point.
 */
#include "cupidscript.h"
#include "string.h"
#include "memory.h"
#include "kernel.h"
#include "shell.h"
#include "fs.h"
#include "fat16.h"
#include "vfs.h"
#include "terminal_ansi.h"
#include "cupidscript_streams.h"
#include "cupidscript_jobs.h"
#include "process.h"
#include "exec.h"
#include "cupidc.h"
#include "../drivers/serial.h"
#include "../drivers/rtc.h"
#include "calendar.h"

static void (*cs_print)(const char *) = NULL;
static void (*cs_putchar)(char) = NULL;
static void (*cs_print_int)(uint32_t) = NULL;

/* Active command-output redirection target (for built-ins/functions). */
static script_context_t *cs_active_ctx = NULL;
static int cs_active_stdout_fd = CS_STDOUT;

typedef struct {
    int stdout_fd;
    int stderr_fd;
    int stdin_fd;
    bool background;
} cs_exec_opts_t;

static void cs_exec_opts_default(cs_exec_opts_t *opts) {
    opts->stdout_fd = CS_STDOUT;
    opts->stderr_fd = CS_STDERR;
    opts->stdin_fd = CS_STDIN;
    opts->background = false;
}

void cupidscript_set_output(void (*print_fn)(const char *),
                            void (*putchar_fn)(char),
                            void (*print_int_fn)(uint32_t)) {
    cs_print = print_fn;
    cs_putchar = putchar_fn;
    cs_print_int = print_int_fn;
}

/* Use context's output or fall back to globals */
static void cs_out(script_context_t *ctx, const char *s) {
    if (cs_active_ctx && s) {
        int len = 0;
        while (s[len]) len++;
        if (len > 0) {
            int w = fd_write(&cs_active_ctx->fd_table, cs_active_stdout_fd,
                             s, (size_t)len);
            if (w >= 0) return;
        }
    }
    if (ctx->print_fn) ctx->print_fn(s);
    else if (cs_print) cs_print(s);
    else print(s);
}

static void cs_outchar(script_context_t *ctx, char c) {
    if (cs_active_ctx) {
        int w = fd_write(&cs_active_ctx->fd_table, cs_active_stdout_fd, &c, 1);
        if (w >= 0) return;
    }
    if (ctx->putchar_fn) ctx->putchar_fn(c);
    else if (cs_putchar) cs_putchar(c);
    else putchar(c);
}

static void str_cpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int parse_int(const char *s) {
    int val = 0;
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    return neg ? -val : val;
}

static int execute_node(ast_node_t *node, script_context_t *ctx);

/* Test expression evaluator
 * Evaluates [ arg1 op arg2 ] style test expressions.
 * Returns 0 for true (success), 1 for false (failure).
 */
static int evaluate_test(ast_node_t *node, script_context_t *ctx) {
    if (!node || node->type != NODE_TEST) return 1;

    int argc = node->data.test.argc;

    /* Expand variables in all arguments */
    char expanded[MAX_ARGS][MAX_EXPAND_LEN];
    for (int i = 0; i < argc && i < MAX_ARGS; i++) {
        char *exp = cupidscript_expand(node->data.test.argv[i], ctx);
        if (exp) {
            str_cpy(expanded[i], exp, MAX_EXPAND_LEN);
            kfree(exp);
        } else {
            str_cpy(expanded[i], node->data.test.argv[i], MAX_EXPAND_LEN);
        }
    }

    /* Unary tests */
    if (argc == 2) {
        /* -z string (true if zero length) */
        if (strcmp(expanded[0], "-z") == 0) {
            return (expanded[1][0] == '\0') ? 0 : 1;
        }
        /* -n string (true if non-zero length) */
        if (strcmp(expanded[0], "-n") == 0) {
            return (expanded[1][0] != '\0') ? 0 : 1;
        }
    }

    /* Binary tests: arg1 op arg2 */
    if (argc == 3) {
        const char *lhs = expanded[0];
        const char *op  = expanded[1];
        const char *rhs = expanded[2];

        /* Numeric comparisons */
        if (strcmp(op, "-eq") == 0) {
            return (parse_int(lhs) == parse_int(rhs)) ? 0 : 1;
        }
        if (strcmp(op, "-ne") == 0) {
            return (parse_int(lhs) != parse_int(rhs)) ? 0 : 1;
        }
        if (strcmp(op, "-lt") == 0) {
            return (parse_int(lhs) < parse_int(rhs)) ? 0 : 1;
        }
        if (strcmp(op, "-gt") == 0) {
            return (parse_int(lhs) > parse_int(rhs)) ? 0 : 1;
        }
        if (strcmp(op, "-le") == 0) {
            return (parse_int(lhs) <= parse_int(rhs)) ? 0 : 1;
        }
        if (strcmp(op, "-ge") == 0) {
            return (parse_int(lhs) >= parse_int(rhs)) ? 0 : 1;
        }

        /* String comparisons */
        if (strcmp(op, "=") == 0) {
            return (strcmp(lhs, rhs) == 0) ? 0 : 1;
        }
        if (strcmp(op, "!=") == 0) {
            return (strcmp(lhs, rhs) != 0) ? 0 : 1;
        }

        KERROR("CupidScript: unknown test operator '%s'", op);
        return 1;
    }

    /* Single value: true if non-empty */
    if (argc == 1) {
        return (expanded[0][0] != '\0') ? 0 : 1;
    }

    return 1; /* default: false */
}

/* Built-in command: echo
 * Handles echo specially so we can do $VAR expansion in arguments.
 */
static int builtin_echo(int argc, char expanded[MAX_ARGS][MAX_EXPAND_LEN],
                        script_context_t *ctx) {
    int start_arg = 1;

    /* Check for -c flag: echo -c <color> <text> */
    if (argc >= 3 && strcmp(expanded[1], "-c") == 0) {
        int color = parse_int(expanded[2]);
        /* Emit ANSI foreground code */
        char ansi_buf[16];
        int p = 0;
        ansi_buf[p++] = '\x1B';
        ansi_buf[p++] = '[';
        if (color >= 8) {
            ansi_buf[p++] = '9';
            ansi_buf[p++] = (char)('0' + (color - 8));
        } else {
            ansi_buf[p++] = '3';
            ansi_buf[p++] = (char)('0' + color);
        }
        ansi_buf[p++] = 'm';
        ansi_buf[p] = '\0';
        cs_out(ctx, ansi_buf);

        /* Print remaining args */
        for (int i = 3; i < argc; i++) {
            if (i > 3) cs_outchar(ctx, ' ');
            cs_out(ctx, expanded[i]);
        }
        cs_outchar(ctx, '\n');

        /* Reset color */
        cs_out(ctx, "\x1B[0m");
        return 0;
    }

    for (int i = start_arg; i < argc; i++) {
        if (i > start_arg) cs_outchar(ctx, ' ');
        cs_out(ctx, expanded[i]);
    }
    cs_outchar(ctx, '\n');
    return 0;
}

/* Built-in: setcolor <fg> [bg] */
static int builtin_setcolor(int argc, char expanded[MAX_ARGS][MAX_EXPAND_LEN],
                            script_context_t *ctx) {
    if (argc < 2) {
        cs_out(ctx, "Usage: setcolor <fg> [bg]\n");
        return 1;
    }

    int fg = parse_int(expanded[1]);
    int bg = -1;
    if (argc >= 3) {
        bg = parse_int(expanded[2]);
    }

    /* Emit ANSI escape sequence */
    char buf[32];
    int p = 0;
    buf[p++] = '\x1B';
    buf[p++] = '[';

    /* Foreground */
    if (fg >= 8) {
        buf[p++] = '9';
        buf[p++] = (char)('0' + (fg - 8));
    } else {
        buf[p++] = '3';
        buf[p++] = (char)('0' + fg);
    }

    /* Background */
    if (bg >= 0) {
        buf[p++] = ';';
        buf[p++] = '4';
        buf[p++] = (char)('0' + (bg & 7));
    }

    buf[p++] = 'm';
    buf[p] = '\0';

    cs_out(ctx, buf);
    return 0;
}

/* Built-in: resetcolor */
static int builtin_resetcolor(script_context_t *ctx) {
    cs_out(ctx, "\x1B[0m");
    return 0;
}

/* Built-in: printc <color> <text> */
static int builtin_printc(int argc, char expanded[MAX_ARGS][MAX_EXPAND_LEN],
                          script_context_t *ctx) {
    if (argc < 3) {
        cs_out(ctx, "Usage: printc <fg> <text>\n");
        return 1;
    }

    int color = parse_int(expanded[1]);

    /* Emit color */
    char ansi_buf[16];
    int p = 0;
    ansi_buf[p++] = '\x1B';
    ansi_buf[p++] = '[';
    if (color >= 8) {
        ansi_buf[p++] = '9';
        ansi_buf[p++] = (char)('0' + (color - 8));
    } else {
        ansi_buf[p++] = '3';
        ansi_buf[p++] = (char)('0' + color);
    }
    ansi_buf[p++] = 'm';
    ansi_buf[p] = '\0';
    cs_out(ctx, ansi_buf);

    /* Print text */
    for (int i = 2; i < argc; i++) {
        if (i > 2) cs_outchar(ctx, ' ');
        cs_out(ctx, expanded[i]);
    }
    cs_outchar(ctx, '\n');

    /* Reset */
    cs_out(ctx, "\x1B[0m");
    return 0;
}

/* Built-in: jobs [-l] */
static int builtin_jobs(int argc, char expanded[MAX_ARGS][MAX_EXPAND_LEN],
                        script_context_t *ctx) {
    bool show_pids = false;
    if (argc >= 2 && strcmp(expanded[1], "-l") == 0) {
        show_pids = true;
    }

    if (ctx->jobs.job_count == 0) {
        if (show_pids) shell_execute_line("jobs -l");
        else shell_execute_line("jobs");
        return 0;
    }

    job_list(&ctx->jobs, show_pids, ctx->print_fn);
    return 0;
}

/* Built-in: declare -A name (create associative array) */
static int builtin_declare(int argc, char expanded[MAX_ARGS][MAX_EXPAND_LEN],
                           script_context_t *ctx) {
    if (argc >= 3 && strcmp(expanded[1], "-A") == 0) {
        cs_assoc_create(ctx->assoc_arrays, &ctx->assoc_count, expanded[2]);
        return 0;
    }
    cs_out(ctx, "Usage: declare -A <name>\n");
    return 1;
}

/* Built-in: date [+epoch|+short] */
static int builtin_date(int argc, char expanded[MAX_ARGS][MAX_EXPAND_LEN],
                        script_context_t *ctx) {
    rtc_date_t date;
    rtc_time_t time;
    rtc_read_date(&date);
    rtc_read_time(&time);

    if (argc >= 2 && strcmp(expanded[1], "+epoch") == 0) {
        char ebuf[16];
        uint32_t epoch = rtc_get_epoch_seconds();
        /* int-to-string */
        if (epoch == 0) { ebuf[0] = '0'; ebuf[1] = '\0'; }
        else {
            char tmp[16]; int i = 0;
            while (epoch > 0 && i < 15) {
                tmp[i++] = (char)('0' + (epoch % 10));
                epoch /= 10;
            }
            int j = 0;
            while (i > 0) ebuf[j++] = tmp[--i];
            ebuf[j] = '\0';
        }
        cs_out(ctx, ebuf);
        cs_outchar(ctx, '\n');
        return 0;
    }

    if (argc >= 2 && strcmp(expanded[1], "+short") == 0) {
        char dbuf[20], tbuf[20];
        format_date_short(&date, dbuf, 20);
        format_time_12hr(&time, tbuf, 20);
        cs_out(ctx, dbuf);
        cs_out(ctx, "  ");
        cs_out(ctx, tbuf);
        cs_outchar(ctx, '\n');
        return 0;
    }

    /* Default: full date + time */
    char datebuf[48], timebuf[20];
    format_date_full(&date, datebuf, 48);
    format_time_12hr_sec(&time, timebuf, 20);
    cs_out(ctx, datebuf);
    cs_out(ctx, "  ");
    cs_out(ctx, timebuf);
    cs_outchar(ctx, '\n');
    return 0;
}

static int cs_build_cmdline(char out[256], int argc,
                            char expanded[MAX_ARGS][MAX_EXPAND_LEN]) {
    int pos = 0;
    if (!out) return 0;

    for (int i = 0; i < argc; i++) {
        if (i > 0 && pos < 255) out[pos++] = ' ';
        int j = 0;
        while (expanded[i][j] && pos < 255) {
            out[pos++] = expanded[i][j++];
        }
    }
    out[pos] = '\0';
    return pos;
}

static int cs_ends_with(const char *str, const char *suffix) {
    int slen = 0;
    int xlen = 0;
    while (str[slen]) slen++;
    while (suffix[xlen]) xlen++;
    if (xlen > slen) return 0;
    return strcmp(str + slen - xlen, suffix) == 0;
}

static void cs_basename(const char *path, char *out, int max) {
    int start = 0;
    int i = 0;
    while (path[i]) {
        if (path[i] == '/') start = i + 1;
        i++;
    }
    int j = 0;
    while (path[start] && j < max - 1) {
        out[j++] = path[start++];
    }
    out[j] = '\0';
}

static int cs_build_prog_args(char out[256], int argc,
                              char expanded[MAX_ARGS][MAX_EXPAND_LEN]) {
    int pos = 0;
    out[0] = '\0';
    for (int i = 1; i < argc; i++) {
        if (i > 1 && pos < 255) out[pos++] = ' ';
        int j = 0;
        while (expanded[i][j] && pos < 255) {
            out[pos++] = expanded[i][j++];
        }
    }
    out[pos] = '\0';
    return pos;
}

static int cs_try_async_exec_background(script_context_t *ctx,
                                        const char *cmd,
                                        int argc,
                                        char expanded[MAX_ARGS][MAX_EXPAND_LEN],
                                        const char *cmdline,
                                        ast_node_t *node) {
    if (!ctx || !cmd || !cmd[0]) return 0;

    /* For now, only plain external commands are async-safe here. */
    if (node && node->type == NODE_COMMAND && node->data.command.redir_count > 0) {
        return 0;
    }

    /* Built-ins/functions are not external commands. */
    if (strcmp(cmd, "echo") == 0 || strcmp(cmd, "setcolor") == 0 ||
        strcmp(cmd, "resetcolor") == 0 || strcmp(cmd, "printc") == 0 ||
        strcmp(cmd, "jobs") == 0 || strcmp(cmd, "declare") == 0 ||
        strcmp(cmd, "date") == 0) {
        return 0;
    }

    char candidates[6][VFS_MAX_PATH];
    int candidate_count = 0;

    /* 1) resolved direct path */
    shell_resolve_path(cmd, candidates[candidate_count]);
    candidate_count++;

    /* 2) /bin/<cmd> */
    {
        int p = 0;
        const char *pre = "/bin/";
        while (*pre && p < VFS_MAX_PATH - 1) candidates[1][p++] = *pre++;
        int i = 0;
        while (cmd[i] && p < VFS_MAX_PATH - 1) candidates[1][p++] = cmd[i++];
        candidates[candidate_count][p] = '\0';
        candidate_count++;
    }

    /* 3) /home/bin/<cmd> */
    {
        int p = 0;
        const char *pre = "/home/bin/";
        while (*pre && p < VFS_MAX_PATH - 1) candidates[2][p++] = *pre++;
        int i = 0;
        while (cmd[i] && p < VFS_MAX_PATH - 1) candidates[2][p++] = cmd[i++];
        candidates[candidate_count][p] = '\0';
        candidate_count++;
    }

    /* 4) /bin/<cmd>.cc */
    {
        int p = 0;
        const char *pre = "/bin/";
        while (*pre && p < VFS_MAX_PATH - 1) candidates[candidate_count][p++] = *pre++;
        int i = 0;
        while (cmd[i] && p < VFS_MAX_PATH - 4) candidates[candidate_count][p++] = cmd[i++];
        candidates[candidate_count][p++] = '.';
        candidates[candidate_count][p++] = 'c';
        candidates[candidate_count][p++] = 'c';
        candidates[candidate_count][p] = '\0';
        candidate_count++;
    }

    /* 5) /home/bin/<cmd>.cc */
    {
        int p = 0;
        const char *pre = "/home/bin/";
        while (*pre && p < VFS_MAX_PATH - 1) candidates[candidate_count][p++] = *pre++;
        int i = 0;
        while (cmd[i] && p < VFS_MAX_PATH - 4) candidates[candidate_count][p++] = cmd[i++];
        candidates[candidate_count][p++] = '.';
        candidates[candidate_count][p++] = 'c';
        candidates[candidate_count][p++] = 'c';
        candidates[candidate_count][p] = '\0';
        candidate_count++;
    }

    for (int c = 0; c < candidate_count; c++) {
        const char *path = candidates[c];

        vfs_stat_t st;
        if (vfs_stat(path, &st) < 0 || st.type != VFS_TYPE_FILE) {
            continue;
        }

        char exec_path[VFS_MAX_PATH];
        str_cpy(exec_path, path, VFS_MAX_PATH);

        if (cs_ends_with(path, ".cc")) {
            /* Compile source -> ELF, then launch ELF in background. */
            char base[PROCESS_NAME_LEN];
            cs_basename(path, base, PROCESS_NAME_LEN);
            int bi = 0;
            while (base[bi]) bi++;
            if (bi >= 3 && base[bi - 3] == '.' && base[bi - 2] == 'c' && base[bi - 1] == 'c') {
                base[bi - 3] = '\0';
            }

            int p = 0;
            const char *pre = "/home/.bg_";
            while (*pre && p < VFS_MAX_PATH - 1) exec_path[p++] = *pre++;
            int i = 0;
            while (base[i] && p < VFS_MAX_PATH - 5) exec_path[p++] = base[i++];
            exec_path[p++] = '.';
            exec_path[p++] = 'e';
            exec_path[p++] = 'l';
            exec_path[p++] = 'f';
            exec_path[p] = '\0';

            cupidc_aot(path, exec_path);

            vfs_stat_t out_st;
            if (vfs_stat(exec_path, &out_st) < 0 || out_st.type != VFS_TYPE_FILE) {
                continue;
            }
        } else if (cs_ends_with(path, ".cup") || cs_ends_with(path, ".asm")) {
            continue;
        }

        char prog_args[256];
        cs_build_prog_args(prog_args, argc, expanded);
        shell_set_program_args(prog_args);

        char proc_name[PROCESS_NAME_LEN];
        cs_basename(exec_path, proc_name, PROCESS_NAME_LEN);
        if (proc_name[0] == '\0') {
            str_cpy(proc_name, cmd, PROCESS_NAME_LEN);
        }

        int pid = exec(exec_path, proc_name);
        if (pid >= 0) {
            job_add(&ctx->jobs, (uint32_t)pid, cmdline);
            return 1;
        }
    }

    return 0;
}

static int cs_spawn_shell_background(script_context_t *ctx,
                                     const char *cmdline) {
    if (!cmdline || cmdline[0] == '\0') return 0;

    if (ctx && ctx->print_fn) {
        ctx->print_fn("cupid: async background unsupported for this command shape; running foreground\n");
    }
    shell_execute_line(cmdline);
    return 0;
}

static void cs_apply_command_redirections(ast_node_t *node,
                                          script_context_t *ctx,
                                          cs_exec_opts_t *opts,
                                          int opened_fds[MAX_REDIRECTIONS],
                                          int *opened_count) {
    if (!node || node->type != NODE_COMMAND || !ctx || !opts) return;

    for (int i = 0; i < node->data.command.redir_count; i++) {
        redirection_t *r = &node->data.command.redirections[i];

        if (r->target_fd >= 0) {
            if (r->source_fd == 2) {
                opts->stderr_fd = r->target_fd;
            } else {
                opts->stdout_fd = r->target_fd;
            }
            continue;
        }

        if (!r->filename) continue;

        int flags = O_RDONLY;
        if (r->source_fd == 0) {
            flags = O_RDONLY;
        } else {
            flags = O_WRONLY | O_CREAT;
            flags |= r->append ? O_APPEND : O_TRUNC;
        }

        int fd = fd_open_file(&ctx->fd_table, r->filename, flags);
        if (fd < 0) {
            opts->stderr_fd = CS_STDERR;
            opts->stdout_fd = CS_STDOUT;
            continue;
        }

        if (*opened_count < MAX_REDIRECTIONS) {
            opened_fds[*opened_count] = fd;
            (*opened_count)++;
        }

        if (r->source_fd == 0) {
            opts->stdin_fd = fd;
        } else if (r->source_fd == 2) {
            opts->stderr_fd = fd;
        } else {
            opts->stdout_fd = fd;
        }
    }
}

/* Execute a command node
 * Expands variables, checks for functions, then dispatches.
 */
static int execute_command_internal(ast_node_t *node, script_context_t *ctx,
                                    cs_exec_opts_t *base_opts) {
    if (!node || node->type != NODE_COMMAND) return 1;
    if (node->data.command.argc == 0) return 0;

    cs_exec_opts_t opts;
    int opened_fds[MAX_REDIRECTIONS];
    int opened_count = 0;
    for (int i = 0; i < MAX_REDIRECTIONS; i++) opened_fds[i] = -1;
    cs_exec_opts_default(&opts);
    if (base_opts) opts = *base_opts;
    cs_apply_command_redirections(node, ctx, &opts, opened_fds, &opened_count);
    if (node->data.command.background) opts.background = true;

    /* Expand variables in all argv[] */
    char expanded[MAX_ARGS][MAX_EXPAND_LEN];
    int argc = node->data.command.argc;

    for (int i = 0; i < argc && i < MAX_ARGS; i++) {
        char *exp = cupidscript_expand(node->data.command.argv[i], ctx);
        if (exp) {
            str_cpy(expanded[i], exp, MAX_EXPAND_LEN);
            kfree(exp);
        } else {
            str_cpy(expanded[i], node->data.command.argv[i], MAX_EXPAND_LEN);
        }
    }

    if (opts.stdin_fd != CS_STDIN && argc < MAX_ARGS) {
        char *stdin_text = fd_get_buffer_contents(&ctx->fd_table, opts.stdin_fd);
        if (stdin_text && stdin_text[0]) {
            str_cpy(expanded[argc], stdin_text, MAX_EXPAND_LEN);
            argc++;
        }
        if (stdin_text) kfree(stdin_text);
    }

    const char *cmd = expanded[0];
    int result = 0;

    if (cmd[0] == '\0') {
        result = 0;
        goto done_no_active;
    }
    if (strcmp(cmd, "!") == 0 || cmd[0] == '#' || cmd[0] == '/') {
        result = 0;
        goto done_no_active;
    }

    script_context_t *saved_ctx = cs_active_ctx;
    int saved_stdout_fd = cs_active_stdout_fd;
    cs_active_ctx = ctx;
    cs_active_stdout_fd = opts.stdout_fd;

    /* built-in commands */
    if (strcmp(cmd, "echo") == 0) {
        result = builtin_echo(argc, expanded, ctx);
        goto done;
    }
    if (strcmp(cmd, "setcolor") == 0) {
        result = builtin_setcolor(argc, expanded, ctx);
        goto done;
    }
    if (strcmp(cmd, "resetcolor") == 0) {
        result = builtin_resetcolor(ctx);
        goto done;
    }
    if (strcmp(cmd, "printc") == 0) {
        result = builtin_printc(argc, expanded, ctx);
        goto done;
    }
    if (strcmp(cmd, "jobs") == 0) {
        result = builtin_jobs(argc, expanded, ctx);
        goto done;
    }
    if (strcmp(cmd, "declare") == 0) {
        result = builtin_declare(argc, expanded, ctx);
        goto done;
    }
    if (strcmp(cmd, "date") == 0) {
        result = builtin_date(argc, expanded, ctx);
        goto done;
    }

    /* user-defined function */
    {
        ast_node_t *func_body = cupidscript_lookup_function(ctx, cmd);
        if (func_body) {
            char saved_args[MAX_SCRIPT_ARGS][MAX_VAR_VALUE];
            int saved_argc = ctx->script_argc;
            char saved_name[MAX_VAR_NAME];
            str_cpy(saved_name, ctx->script_name, MAX_VAR_NAME);
            for (int i = 0; i < saved_argc && i < MAX_SCRIPT_ARGS; i++) {
                str_cpy(saved_args[i], ctx->script_args[i], MAX_VAR_VALUE);
            }

            str_cpy(ctx->script_name, cmd, MAX_VAR_NAME);
            ctx->script_argc = argc - 1;
            for (int i = 1; i < argc && i - 1 < MAX_SCRIPT_ARGS; i++) {
                str_cpy(ctx->script_args[i - 1], expanded[i], MAX_VAR_VALUE);
            }

            ctx->return_flag = 0;
            result = execute_node(func_body, ctx);
            if (ctx->return_flag) {
                result = ctx->return_value;
                ctx->return_flag = 0;
            }

            str_cpy(ctx->script_name, saved_name, MAX_VAR_NAME);
            ctx->script_argc = saved_argc;
            for (int i = 0; i < saved_argc && i < MAX_SCRIPT_ARGS; i++) {
                str_cpy(ctx->script_args[i], saved_args[i], MAX_VAR_VALUE);
            }

            ctx->last_exit_status = result;
            goto done;
        }
    }

    /* shell dispatch (external commands) */
    {
        char cmdline[256];
        cs_build_cmdline(cmdline, argc, expanded);
        if (cmdline[0] == '\0') {
            result = 0;
            goto done;
        }

        /* Append redirections for shell commands (best-effort). */
        for (int i = 0; i < node->data.command.redir_count; i++) {
            redirection_t *r = &node->data.command.redirections[i];
            int p = 0;
            while (cmdline[p]) p++;
            if (p >= 250) break;

            cmdline[p++] = ' ';
            if (r->target_fd == 1 && r->source_fd == 2) {
                cmdline[p++] = '2';
                cmdline[p++] = '>';
                cmdline[p++] = '&';
                cmdline[p++] = '1';
                cmdline[p] = '\0';
                continue;
            }

            if (r->source_fd == 2) cmdline[p++] = '2';
            if (r->source_fd == 0) cmdline[p++] = '<';
            else if (r->append) {
                cmdline[p++] = '>';
                cmdline[p++] = '>';
            } else {
                cmdline[p++] = '>';
            }
            cmdline[p++] = ' ';
            if (r->filename) {
                int j = 0;
                while (r->filename[j] && p < 255) {
                    cmdline[p++] = r->filename[j++];
                }
            }
            cmdline[p] = '\0';
        }

        if (opts.background) {
            if (cs_try_async_exec_background(ctx, cmd, argc, expanded, cmdline, node)) {
                result = 0;
            } else {
                result = cs_spawn_shell_background(ctx, cmdline);
            }
        } else {
            shell_execute_line(cmdline);
            result = 0;
        }
        ctx->last_exit_status = result;
    }

done:
    cs_active_ctx = saved_ctx;
    cs_active_stdout_fd = saved_stdout_fd;
done_no_active:
    for (int i = 0; i < opened_count; i++) {
        if (opened_fds[i] >= 0) {
            fd_close(&ctx->fd_table, opened_fds[i]);
        }
    }
    return result;
}

static int execute_command(ast_node_t *node, script_context_t *ctx) {
    cs_exec_opts_t opts;
    cs_exec_opts_default(&opts);
    return execute_command_internal(node, ctx, &opts);
}

static int execute_pipeline(ast_node_t *node, script_context_t *ctx) {
    if (!node || node->type != NODE_PIPELINE) return 1;
    if (node->data.pipeline.command_count <= 0) return 0;

    int prev_read_fd = CS_STDIN;
    int result = 0;

    for (int i = 0; i < node->data.pipeline.command_count; i++) {
        ast_node_t *cmd = node->data.pipeline.commands[i];
        if (!cmd || cmd->type != NODE_COMMAND) {
            result = 1;
            break;
        }

        int next_read_fd = CS_STDIN;
        int write_fd = CS_STDOUT;
        bool is_last = (i == node->data.pipeline.command_count - 1);

        if (!is_last) {
            if (fd_create_pipe(&ctx->fd_table, &next_read_fd, &write_fd) != 0) {
                result = 1;
                break;
            }
        }

        cs_exec_opts_t opts;
        cs_exec_opts_default(&opts);
        opts.stdin_fd = prev_read_fd;
        opts.stdout_fd = write_fd;
        opts.background = is_last ? node->data.pipeline.background : false;

        result = execute_command_internal(cmd, ctx, &opts);

        if (prev_read_fd >= 3) {
            fd_close(&ctx->fd_table, prev_read_fd);
        }
        if (write_fd >= 3) {
            fd_close(&ctx->fd_table, write_fd);
        }

        prev_read_fd = next_read_fd;
        if (result != 0) break;
    }

    if (prev_read_fd >= 3) {
        fd_close(&ctx->fd_table, prev_read_fd);
    }

    ctx->last_exit_status = result;
    return result;
}

/* Execute an assignment node */
static int execute_assignment(ast_node_t *node, script_context_t *ctx) {
    if (!node || node->type != NODE_ASSIGNMENT) return 1;

    /* Expand variables in the value */
    char *expanded = cupidscript_expand(node->data.assignment.value, ctx);
    if (expanded) {
        cupidscript_set_variable(ctx, node->data.assignment.name, expanded);
        kfree(expanded);
    } else {
        cupidscript_set_variable(ctx, node->data.assignment.name,
                                 node->data.assignment.value);
    }

    return 0;
}

/* Execute an if statement */
static int execute_if(ast_node_t *node, script_context_t *ctx) {
    if (!node || node->type != NODE_IF) return 1;

    int cond;
    if (node->data.if_stmt.condition &&
        node->data.if_stmt.condition->type == NODE_TEST) {
        cond = evaluate_test(node->data.if_stmt.condition, ctx);
    } else {
        /* Condition is a command - run it and check exit status */
        cond = execute_node(node->data.if_stmt.condition, ctx);
    }

    if (cond == 0) { /* true */
        return execute_node(node->data.if_stmt.then_body, ctx);
    } else if (node->data.if_stmt.else_body) {
        return execute_node(node->data.if_stmt.else_body, ctx);
    }

    return 0;
}

/* Execute a while loop */
static int execute_while(ast_node_t *node, script_context_t *ctx) {
    if (!node || node->type != NODE_WHILE) return 1;

    int iteration = 0;
    int max_iterations = 10000; /* safety limit */

    while (iteration < max_iterations) {
        int cond;
        if (node->data.while_stmt.condition &&
            node->data.while_stmt.condition->type == NODE_TEST) {
            cond = evaluate_test(node->data.while_stmt.condition, ctx);
        } else {
            cond = execute_node(node->data.while_stmt.condition, ctx);
        }

        if (cond != 0) break; /* condition false */

        execute_node(node->data.while_stmt.body, ctx);

        if (ctx->return_flag) break;
        iteration++;
    }

    if (iteration >= max_iterations) {
        KWARN("CupidScript: while loop hit iteration limit (%d)",
              max_iterations);
    }

    return 0;
}

/* Execute a for loop */
static int execute_for(ast_node_t *node, script_context_t *ctx) {
    if (!node || node->type != NODE_FOR) return 1;

    for (int i = 0; i < node->data.for_stmt.word_count; i++) {
        /* Expand variables in the word */
        char *expanded = cupidscript_expand(
            node->data.for_stmt.word_list[i], ctx);
        if (expanded) {
            cupidscript_set_variable(ctx, node->data.for_stmt.var_name,
                                     expanded);
            kfree(expanded);
        } else {
            cupidscript_set_variable(ctx, node->data.for_stmt.var_name,
                                     node->data.for_stmt.word_list[i]);
        }

        execute_node(node->data.for_stmt.body, ctx);

        if (ctx->return_flag) break;
    }

    return 0;
}

/* Execute a function definition
 * Just registers the function - doesn't execute the body. */
static int execute_function_def(ast_node_t *node, script_context_t *ctx) {
    if (!node || node->type != NODE_FUNCTION_DEF) return 1;

    cupidscript_register_function(ctx, node->data.function_def.name,
                                  node->data.function_def.body);
    return 0;
}

/* Execute a return statement */
static int execute_return(ast_node_t *node, script_context_t *ctx) {
    if (!node || node->type != NODE_RETURN) return 1;

    ctx->return_flag = 1;
    ctx->return_value = node->data.return_stmt.exit_code;
    return node->data.return_stmt.exit_code;
}

/* Execute a sequence of statements */
static int execute_sequence(ast_node_t *node, script_context_t *ctx) {
    if (!node || node->type != NODE_SEQUENCE) return 1;

    int result = 0;
    for (int i = 0; i < node->data.sequence.count; i++) {
        job_check_completed(&ctx->jobs, ctx->print_fn);
        result = execute_node(node->data.sequence.statements[i], ctx);
        if (ctx->return_flag) break;
    }
    return result;
}

/* Execute any AST node (main dispatcher) */
static int execute_node(ast_node_t *node, script_context_t *ctx) {
    if (!node) return 0;

    switch (node->type) {
    case NODE_COMMAND:      return execute_command(node, ctx);
    case NODE_PIPELINE:     return execute_pipeline(node, ctx);
    case NODE_ASSIGNMENT:   return execute_assignment(node, ctx);
    case NODE_IF:           return execute_if(node, ctx);
    case NODE_WHILE:        return execute_while(node, ctx);
    case NODE_FOR:          return execute_for(node, ctx);
    case NODE_FUNCTION_DEF: return execute_function_def(node, ctx);
    case NODE_SEQUENCE:     return execute_sequence(node, ctx);
    case NODE_RETURN:       return execute_return(node, ctx);
    case NODE_TEST:         return evaluate_test(node, ctx);
    }

    return 0;
}

/* Public: execute an AST */
int cupidscript_execute(ast_node_t *ast, script_context_t *ctx) {
    return execute_node(ast, ctx);
}

/* Helper: parse arguments string into individual args */
static int parse_args(const char *args, char argv[MAX_SCRIPT_ARGS][MAX_VAR_VALUE]) {
    if (!args || !args[0]) return 0;

    int argc = 0;
    int i = 0;

    while (args[i] && argc < MAX_SCRIPT_ARGS) {
        /* skip whitespace */
        while (args[i] == ' ' || args[i] == '\t') i++;
        if (!args[i]) break;

        /* collect word */
        int j = 0;
        while (args[i] && args[i] != ' ' && args[i] != '\t' &&
               j < MAX_VAR_VALUE - 1) {
            argv[argc][j++] = args[i++];
        }
        argv[argc][j] = '\0';
        argc++;
    }

    return argc;
}

/* cupidscript_run_file
 * Top-level entry point: reads a script file and executes it.
 * Called from shell.c. */
int cupidscript_run_file(const char *filename, const char *args) {
    const char *source = NULL;
    uint32_t source_len = 0;
    char *disk_buf = NULL;

    /* Determine output mode based on shell state */
    if (shell_get_output_mode() == SHELL_OUTPUT_GUI) {
        /* Use the external GUI print functions */
        cs_print = shell_gui_print_ext;
        cs_putchar = shell_gui_putchar_ext;
        cs_print_int = shell_gui_print_int_ext;
    }

    /* 1. Try in-memory filesystem first */
    const fs_file_t *file = fs_find(filename);
    if (file && file->data && file->size > 0) {
        source = file->data;
        source_len = file->size;
        KINFO("CupidScript: loading '%s' from in-memory fs (%u bytes)",
              filename, source_len);
    }

    /* 2. Try VFS (supports /home/file, /tmp/file, etc.) */
    if (!source) {
        /* Build VFS path: if not absolute, prepend CWD */
        char vpath[VFS_MAX_PATH];
        if (filename[0] == '/') {
            int k = 0;
            while (filename[k] && k < VFS_MAX_PATH - 1) {
                vpath[k] = filename[k];
                k++;
            }
            vpath[k] = '\0';
        } else {
            const char *cwd = shell_get_cwd();
            int j = 0, k = 0;
            while (cwd[k] && j < VFS_MAX_PATH - 2) {
                vpath[j++] = cwd[k++];
            }
            if (j > 1) vpath[j++] = '/';
            k = 0;
            while (filename[k] && j < VFS_MAX_PATH - 1) {
                vpath[j++] = filename[k++];
            }
            vpath[j] = '\0';
        }

        int fd = vfs_open(vpath, O_RDONLY);
        if (fd >= 0) {
            /* Get file size via stat */
            vfs_stat_t st;
            uint32_t fsize = 8192; /* default max */
            if (vfs_stat(vpath, &st) == 0 && st.size > 0) {
                fsize = st.size;
            }
            disk_buf = kmalloc(fsize + 1);
            if (!disk_buf) {
                vfs_close(fd);
                void (*out)(const char *) = cs_print ? cs_print : print;
                out("cupid: out of memory reading ");
                out(filename);
                out("\n");
                return 1;
            }
            int total_read = vfs_read(fd, disk_buf, fsize);
            vfs_close(fd);
            if (total_read < 0) total_read = 0;
            disk_buf[total_read] = '\0';
            source = disk_buf;
            source_len = (uint32_t)total_read;
            KINFO("CupidScript: loading '%s' from VFS (%u bytes)",
                  vpath, source_len);
        }
    }

    /* 3. Try direct FAT16 (bare filenames like "script.cup") */
    if (!source) {
        fat16_file_t *df = fat16_open(filename);
        if (!df) {
            void (*out)(const char *) = cs_print ? cs_print : print;
            out("cupid: cannot open ");
            out(filename);
            out("\n");
            return 1;
        }

        /* Read entire file into buffer */
        disk_buf = kmalloc(df->file_size + 1);
        if (!disk_buf) {
            fat16_close(df);
            void (*out)(const char *) = cs_print ? cs_print : print;
            out("cupid: out of memory reading ");
            out(filename);
            out("\n");
            return 1;
        }

        int total_read = 0;
        int bytes;
        while ((bytes = fat16_read(df, disk_buf + total_read,
                                   512)) > 0) {
            total_read += bytes;
        }
        disk_buf[total_read] = '\0';
        source = disk_buf;
        source_len = (uint32_t)total_read;
        fat16_close(df);

        KINFO("CupidScript: loading '%s' from FAT16 (%u bytes)",
              filename, source_len);
    }

    /* 3. Tokenize */
    token_t *tokens = kmalloc(MAX_TOKENS * sizeof(token_t));
    if (!tokens) {
        if (disk_buf) kfree(disk_buf);
        void (*out)(const char *) = cs_print ? cs_print : print;
        out("cupid: out of memory for tokenizer\n");
        return 1;
    }

    int token_count = cupidscript_tokenize(source, source_len,
                                            tokens, MAX_TOKENS);

    /* 4. Parse */
    ast_node_t *ast = cupidscript_parse(tokens, token_count);
    if (!ast) {
        kfree(tokens);
        if (disk_buf) kfree(disk_buf);
        void (*out)(const char *) = cs_print ? cs_print : print;
        out("cupid: parse error in ");
        out(filename);
        out("\n");
        return 1;
    }

    /* 5. Set up context and execute */
    script_context_t *ctx = kmalloc(sizeof(script_context_t));
    if (!ctx) {
        void (*out)(const char *) = cs_print ? cs_print : print;
        out("cupid: out of memory\n");
        cupidscript_free_ast(ast);
        kfree(tokens);
        if (disk_buf) kfree(disk_buf);
        return 1;
    }
    memset(ctx, 0, sizeof(script_context_t));
    cupidscript_init_context(ctx);

    /* Set output functions */
    if (cs_print) ctx->print_fn = cs_print;
    if (cs_putchar) ctx->putchar_fn = cs_putchar;
    if (cs_print_int) ctx->print_int_fn = cs_print_int;

    /* Keep fd-table terminal outputs in sync with active script output. */
    ctx->fd_table.fds[CS_STDOUT].terminal.output_fn = ctx->print_fn;
    ctx->fd_table.fds[CS_STDERR].terminal.output_fn = ctx->print_fn;

    /* Set script name and arguments */
    str_cpy(ctx->script_name, filename, MAX_VAR_NAME);
    if (args && args[0]) {
        ctx->script_argc = parse_args(args, ctx->script_args);
    }

    KINFO("CupidScript: executing '%s' with %d args", filename,
          ctx->script_argc);

    int result = cupidscript_execute(ast, ctx);

    /* 6. Cleanup */
    cupidscript_free_ast(ast);
    kfree(tokens);
    if (disk_buf) kfree(disk_buf);
    kfree(ctx);

    KINFO("CupidScript: '%s' finished with exit status %d",
          filename, result);

    return result;
}
