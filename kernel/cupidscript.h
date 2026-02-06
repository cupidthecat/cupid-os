/*
 * cupidscript.h - CupidScript scripting language for cupid-os
 *
 * A bash-like scripting language supporting variables, conditionals,
 * loops, and functions. Scripts use .cup extension and can be run
 * via "cupid script.cup" or "./script.cup".
 */
#ifndef CUPIDSCRIPT_H
#define CUPIDSCRIPT_H

#include "types.h"
#include "terminal_ansi.h"
#include "cupidscript_streams.h"
#include "cupidscript_jobs.h"
#include "cupidscript_arrays.h"

/* ══════════════════════════════════════════════════════════════════════
 *  Limits
 * ══════════════════════════════════════════════════════════════════════ */
#define MAX_VARIABLES       64
#define MAX_FUNCTIONS       16
#define MAX_VAR_NAME        64
#define MAX_VAR_VALUE      256
#define MAX_TOKENS        2048
#define MAX_ARGS            16
#define MAX_WORD_LIST       32
#define MAX_SEQUENCE       128
#define MAX_TOKEN_LEN      256
#define MAX_EXPAND_LEN     256
#define MAX_SCRIPT_ARGS      8

/* ══════════════════════════════════════════════════════════════════════
 *  Token types
 * ══════════════════════════════════════════════════════════════════════ */
typedef enum {
    TOK_EOF,
    TOK_NEWLINE,
    TOK_WORD,
    TOK_STRING,          /* "quoted string" or 'single quoted' */
    TOK_VARIABLE,        /* $VAR */
    TOK_ASSIGN,          /* = (only when following a WORD directly) */
    TOK_IF,
    TOK_THEN,
    TOK_ELSE,
    TOK_ELIF,
    TOK_FI,
    TOK_WHILE,
    TOK_DO,
    TOK_DONE,
    TOK_FOR,
    TOK_IN,
    TOK_LBRACE,          /* { */
    TOK_RBRACE,          /* } */
    TOK_LPAREN,          /* ( */
    TOK_RPAREN,          /* ) */
    TOK_SEMICOLON,       /* ; */
    TOK_RETURN,
    TOK_LBRACKET,        /* [ */
    TOK_RBRACKET,        /* ] */
    TOK_COMMENT,         /* # ... (skipped) */
    TOK_ARITH,           /* $((...)) content */
    TOK_HASH_BANG,       /* #!/bin/cupid shebang */
    /* ── I/O redirection and pipeline tokens ── */
    TOK_PIPE,            /* | */
    TOK_REDIR_OUT,       /* > */
    TOK_REDIR_APPEND,    /* >> */
    TOK_REDIR_IN,        /* < */
    TOK_REDIR_ERR,       /* 2> */
    TOK_REDIR_ERR_OUT,   /* 2>&1 */
    TOK_BACKGROUND,      /* & */
    TOK_CMD_SUBST_START, /* $( */
    TOK_BACKTICK         /* ` */
} token_type_t;

typedef struct {
    token_type_t type;
    char value[MAX_TOKEN_LEN];
    int line;
} token_t;

/* ══════════════════════════════════════════════════════════════════════
 *  AST node types
 * ══════════════════════════════════════════════════════════════════════ */
typedef enum {
    NODE_COMMAND,         /* Simple command: echo hello */
    NODE_ASSIGNMENT,      /* VAR=value */
    NODE_IF,              /* if/then/else/fi */
    NODE_WHILE,           /* while/do/done */
    NODE_FOR,             /* for/in/do/done */
    NODE_FUNCTION_DEF,    /* function definition */
    NODE_SEQUENCE,        /* List of statements */
    NODE_RETURN,          /* return statement */
    NODE_TEST             /* [ test expression ] */
} node_type_t;

/* Forward declaration */
typedef struct ast_node ast_node_t;

struct ast_node {
    node_type_t type;
    union {
        struct {                     /* COMMAND */
            char argv[MAX_ARGS][MAX_TOKEN_LEN];
            int argc;
        } command;

        struct {                     /* ASSIGNMENT */
            char name[MAX_VAR_NAME];
            char value[MAX_VAR_VALUE];
        } assignment;

        struct {                     /* IF */
            ast_node_t *condition;
            ast_node_t *then_body;
            ast_node_t *else_body;   /* may be NULL */
        } if_stmt;

        struct {                     /* WHILE */
            ast_node_t *condition;
            ast_node_t *body;
        } while_stmt;

        struct {                     /* FOR */
            char var_name[MAX_VAR_NAME];
            char word_list[MAX_WORD_LIST][MAX_TOKEN_LEN];
            int word_count;
            ast_node_t *body;
        } for_stmt;

        struct {                     /* FUNCTION_DEF */
            char name[MAX_VAR_NAME];
            ast_node_t *body;
        } function_def;

        struct {                     /* SEQUENCE */
            ast_node_t *statements[MAX_SEQUENCE];
            int count;
        } sequence;

        struct {                     /* RETURN */
            int exit_code;
        } return_stmt;

        struct {                     /* TEST */
            char argv[MAX_ARGS][MAX_TOKEN_LEN];
            int argc;
        } test;
    } data;
};

/* ══════════════════════════════════════════════════════════════════════
 *  Runtime context
 * ══════════════════════════════════════════════════════════════════════ */
typedef struct {
    char name[MAX_VAR_NAME];
    char value[MAX_VAR_VALUE];
} cs_variable_t;

typedef struct {
    char name[MAX_VAR_NAME];
    ast_node_t *body;
} cs_function_t;

typedef struct script_context {
    cs_variable_t variables[MAX_VARIABLES];
    int var_count;
    cs_function_t functions[MAX_FUNCTIONS];
    int func_count;
    int last_exit_status;    /* $? */
    int return_flag;         /* set by return statement */
    int return_value;
    /* Script arguments */
    char script_name[MAX_VAR_NAME];
    char script_args[MAX_SCRIPT_ARGS][MAX_VAR_VALUE];
    int script_argc;
    /* ── NEW: Stream system ── */
    fd_table_t fd_table;
    /* ── NEW: Job control ── */
    job_table_t jobs;
    /* ── NEW: Arrays ── */
    cs_array_t arrays[MAX_ARRAYS];
    int array_count;
    cs_assoc_array_t assoc_arrays[MAX_ASSOC_ARRAYS];
    int assoc_count;
    /* ── NEW: Terminal color state ── */
    terminal_color_state_t color_state;
    /* Output function pointers (for GUI/text mode routing) */
    void (*print_fn)(const char *);
    void (*putchar_fn)(char);
    void (*print_int_fn)(uint32_t);
} script_context_t;

/* ══════════════════════════════════════════════════════════════════════
 *  Public API - Lexer  (cupidscript_lex.c)
 * ══════════════════════════════════════════════════════════════════════ */
int cupidscript_tokenize(const char *source, uint32_t length,
                         token_t *tokens, int max_tokens);

/* ══════════════════════════════════════════════════════════════════════
 *  Public API - Parser  (cupidscript_parse.c)
 * ══════════════════════════════════════════════════════════════════════ */
ast_node_t *cupidscript_parse(token_t *tokens, int token_count);
void cupidscript_free_ast(ast_node_t *node);

/* ══════════════════════════════════════════════════════════════════════
 *  Public API - Runtime  (cupidscript_runtime.c)
 * ══════════════════════════════════════════════════════════════════════ */
void cupidscript_init_context(script_context_t *ctx);
const char *cupidscript_get_variable(script_context_t *ctx, const char *name);
void cupidscript_set_variable(script_context_t *ctx, const char *name,
                              const char *value);
char *cupidscript_expand(const char *str, script_context_t *ctx);
void cupidscript_register_function(script_context_t *ctx, const char *name,
                                   ast_node_t *body);
ast_node_t *cupidscript_lookup_function(script_context_t *ctx,
                                        const char *name);

/* ══════════════════════════════════════════════════════════════════════
 *  Public API - Executor  (cupidscript_exec.c)
 * ══════════════════════════════════════════════════════════════════════ */
int cupidscript_execute(ast_node_t *ast, script_context_t *ctx);

/* ══════════════════════════════════════════════════════════════════════
 *  Public API - Top-level entry  (called from shell.c)
 * ══════════════════════════════════════════════════════════════════════ */
int cupidscript_run_file(const char *filename, const char *args);

/* Set output functions (for GUI mode support) */
void cupidscript_set_output(void (*print_fn)(const char *),
                            void (*putchar_fn)(char),
                            void (*print_int_fn)(uint32_t));

/* ══════════════════════════════════════════════════════════════════════
 *  Public API - Advanced string operations  (cupidscript_strings.c)
 * ══════════════════════════════════════════════════════════════════════ */
char *cs_expand_advanced_var(const char *expr, script_context_t *ctx);
char *cs_string_length(const char *value);
char *cs_string_substring(const char *value, int start, int len);
char *cs_string_remove_suffix(const char *value, const char *pattern, bool longest);
char *cs_string_remove_prefix(const char *value, const char *pattern, bool longest);
char *cs_string_replace(const char *value, const char *pattern,
                         const char *replacement, bool replace_all);
char *cs_string_toupper(const char *value);
char *cs_string_tolower(const char *value);
char *cs_string_capitalize(const char *value);

#endif /* CUPIDSCRIPT_H */
