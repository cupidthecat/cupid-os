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
#include "../drivers/serial.h"

/* ── output function pointers (set by shell integration) ───────────── */
static void (*cs_print)(const char *) = NULL;
static void (*cs_putchar)(char) = NULL;
static void (*cs_print_int)(uint32_t) = NULL;

void cupidscript_set_output(void (*print_fn)(const char *),
                            void (*putchar_fn)(char),
                            void (*print_int_fn)(uint32_t)) {
    cs_print = print_fn;
    cs_putchar = putchar_fn;
    cs_print_int = print_int_fn;
}

/* Use context's output or fall back to globals */
static void cs_out(script_context_t *ctx, const char *s) {
    if (ctx->print_fn) ctx->print_fn(s);
    else if (cs_print) cs_print(s);
    else print(s);
}

static void cs_outchar(script_context_t *ctx, char c) {
    if (ctx->putchar_fn) ctx->putchar_fn(c);
    else if (cs_putchar) cs_putchar(c);
    else putchar(c);
}

/* ── helper: copy string ───────────────────────────────────────────── */
static void str_cpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* ── helper: parse integer from string ─────────────────────────────── */
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

/* ── forward declaration ───────────────────────────────────────────── */
static int execute_node(ast_node_t *node, script_context_t *ctx);

/* ══════════════════════════════════════════════════════════════════════
 *  Test expression evaluator
 *
 *  Evaluates [ arg1 op arg2 ] style test expressions.
 *  Returns 0 for true (success), 1 for false (failure).
 * ══════════════════════════════════════════════════════════════════════ */
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

/* ══════════════════════════════════════════════════════════════════════
 *  Built-in command: echo
 *
 *  Handles echo specially so we can do $VAR expansion in arguments.
 * ══════════════════════════════════════════════════════════════════════ */
static int builtin_echo(int argc, char expanded[MAX_ARGS][MAX_EXPAND_LEN],
                        script_context_t *ctx) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) cs_outchar(ctx, ' ');
        cs_out(ctx, expanded[i]);
    }
    cs_outchar(ctx, '\n');
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Execute a command node
 *
 *  Expands variables, checks for functions, then dispatches.
 * ══════════════════════════════════════════════════════════════════════ */
static int execute_command(ast_node_t *node, script_context_t *ctx) {
    if (!node || node->type != NODE_COMMAND) return 1;
    if (node->data.command.argc == 0) return 0;

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

    const char *cmd = expanded[0];

    /* Skip empty commands (e.g. from empty variable expansion) */
    if (cmd[0] == '\0') return 0;

    /* Skip shell-isms that aren't real commands:
     * "!"  — bash negation operator
     * "#"  — comment that leaked through
     * Paths like "/bin/cupid" from shebangs parsed as commands */
    if (strcmp(cmd, "!") == 0 || cmd[0] == '#' || cmd[0] == '/') return 0;

    /* ── built-in: echo ────────────────────────────────────────── */
    if (strcmp(cmd, "echo") == 0) {
        return builtin_echo(argc, expanded, ctx);
    }

    /* ── check for user-defined function ───────────────────────── */
    ast_node_t *func_body = cupidscript_lookup_function(ctx, cmd);
    if (func_body) {
        /* Save current positional args */
        char saved_args[MAX_SCRIPT_ARGS][MAX_VAR_VALUE];
        int saved_argc = ctx->script_argc;
        char saved_name[MAX_VAR_NAME];
        str_cpy(saved_name, ctx->script_name, MAX_VAR_NAME);
        for (int i = 0; i < saved_argc && i < MAX_SCRIPT_ARGS; i++) {
            str_cpy(saved_args[i], ctx->script_args[i], MAX_VAR_VALUE);
        }

        /* Set new positional args from function call */
        str_cpy(ctx->script_name, cmd, MAX_VAR_NAME);
        ctx->script_argc = argc - 1;
        for (int i = 1; i < argc && i - 1 < MAX_SCRIPT_ARGS; i++) {
            str_cpy(ctx->script_args[i - 1], expanded[i], MAX_VAR_VALUE);
        }

        /* Execute function body */
        ctx->return_flag = 0;
        int result = execute_node(func_body, ctx);

        if (ctx->return_flag) {
            result = ctx->return_value;
            ctx->return_flag = 0;
        }

        /* Restore positional args */
        str_cpy(ctx->script_name, saved_name, MAX_VAR_NAME);
        ctx->script_argc = saved_argc;
        for (int i = 0; i < saved_argc && i < MAX_SCRIPT_ARGS; i++) {
            str_cpy(ctx->script_args[i], saved_args[i], MAX_VAR_VALUE);
        }

        ctx->last_exit_status = result;
        return result;
    }

    /* ── dispatch to shell commands ────────────────────────────── */
    /* Build a single command string from expanded args to pass to shell */
    char cmdline[MAX_EXPAND_LEN];
    int pos = 0;
    for (int i = 0; i < argc; i++) {
        if (i > 0 && pos < MAX_EXPAND_LEN - 1) {
            cmdline[pos++] = ' ';
        }
        int j = 0;
        while (expanded[i][j] && pos < MAX_EXPAND_LEN - 1) {
            cmdline[pos++] = expanded[i][j++];
        }
    }
    cmdline[pos] = '\0';

    /* Skip if command line ended up empty */
    if (cmdline[0] == '\0') return 0;

    /* Use the shell's execute_command via shell_execute_line */
    shell_execute_line(cmdline);
    ctx->last_exit_status = 0; /* shell doesn't return status yet */
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Execute an assignment node
 * ══════════════════════════════════════════════════════════════════════ */
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

/* ══════════════════════════════════════════════════════════════════════
 *  Execute an if statement
 * ══════════════════════════════════════════════════════════════════════ */
static int execute_if(ast_node_t *node, script_context_t *ctx) {
    if (!node || node->type != NODE_IF) return 1;

    int cond;
    if (node->data.if_stmt.condition &&
        node->data.if_stmt.condition->type == NODE_TEST) {
        cond = evaluate_test(node->data.if_stmt.condition, ctx);
    } else {
        /* Condition is a command — run it and check exit status */
        cond = execute_node(node->data.if_stmt.condition, ctx);
    }

    if (cond == 0) { /* true */
        return execute_node(node->data.if_stmt.then_body, ctx);
    } else if (node->data.if_stmt.else_body) {
        return execute_node(node->data.if_stmt.else_body, ctx);
    }

    return 0;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Execute a while loop
 * ══════════════════════════════════════════════════════════════════════ */
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

/* ══════════════════════════════════════════════════════════════════════
 *  Execute a for loop
 * ══════════════════════════════════════════════════════════════════════ */
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

/* ══════════════════════════════════════════════════════════════════════
 *  Execute a function definition
 *
 *  Just registers the function — doesn't execute the body.
 * ══════════════════════════════════════════════════════════════════════ */
static int execute_function_def(ast_node_t *node, script_context_t *ctx) {
    if (!node || node->type != NODE_FUNCTION_DEF) return 1;

    cupidscript_register_function(ctx, node->data.function_def.name,
                                  node->data.function_def.body);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Execute a return statement
 * ══════════════════════════════════════════════════════════════════════ */
static int execute_return(ast_node_t *node, script_context_t *ctx) {
    if (!node || node->type != NODE_RETURN) return 1;

    ctx->return_flag = 1;
    ctx->return_value = node->data.return_stmt.exit_code;
    return node->data.return_stmt.exit_code;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Execute a sequence of statements
 * ══════════════════════════════════════════════════════════════════════ */
static int execute_sequence(ast_node_t *node, script_context_t *ctx) {
    if (!node || node->type != NODE_SEQUENCE) return 1;

    int result = 0;
    for (int i = 0; i < node->data.sequence.count; i++) {
        result = execute_node(node->data.sequence.statements[i], ctx);
        if (ctx->return_flag) break;
    }
    return result;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Execute any AST node (main dispatcher)
 * ══════════════════════════════════════════════════════════════════════ */
static int execute_node(ast_node_t *node, script_context_t *ctx) {
    if (!node) return 0;

    switch (node->type) {
    case NODE_COMMAND:      return execute_command(node, ctx);
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

/* ══════════════════════════════════════════════════════════════════════
 *  Public: execute an AST
 * ══════════════════════════════════════════════════════════════════════ */
int cupidscript_execute(ast_node_t *ast, script_context_t *ctx) {
    return execute_node(ast, ctx);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Helper: parse arguments string into individual args
 * ══════════════════════════════════════════════════════════════════════ */
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

/* ══════════════════════════════════════════════════════════════════════
 *  cupidscript_run_file
 *
 *  Top-level entry point: reads a script file and executes it.
 *  Called from shell.c.
 * ══════════════════════════════════════════════════════════════════════ */
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
    script_context_t ctx;
    cupidscript_init_context(&ctx);

    /* Set output functions */
    if (cs_print) ctx.print_fn = cs_print;
    if (cs_putchar) ctx.putchar_fn = cs_putchar;
    if (cs_print_int) ctx.print_int_fn = cs_print_int;

    /* Set script name and arguments */
    str_cpy(ctx.script_name, filename, MAX_VAR_NAME);
    if (args && args[0]) {
        ctx.script_argc = parse_args(args, ctx.script_args);
    }

    KINFO("CupidScript: executing '%s' with %d args", filename,
          ctx.script_argc);

    int result = cupidscript_execute(ast, &ctx);

    /* 6. Cleanup */
    cupidscript_free_ast(ast);
    kfree(tokens);
    if (disk_buf) kfree(disk_buf);

    KINFO("CupidScript: '%s' finished with exit status %d",
          filename, result);

    return result;
}
