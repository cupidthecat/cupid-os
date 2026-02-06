/*
 * cupidscript_parse.c - Parser for CupidScript
 *
 * Transforms a flat token array into an Abstract Syntax Tree (AST).
 */
#include "cupidscript.h"
#include "string.h"
#include "memory.h"
#include "../drivers/serial.h"

/* ── parser state ──────────────────────────────────────────────────── */
static token_t *toks;
static int tok_count;
static int tok_pos;

static token_t *peek(void) {
    if (tok_pos < tok_count) return &toks[tok_pos];
    return &toks[tok_count - 1]; /* EOF */
}

static token_t *advance(void) {
    token_t *t = peek();
    if (tok_pos < tok_count - 1) tok_pos++;
    return t;
}

static int at_end(void) {
    return peek()->type == TOK_EOF;
}

static int match(token_type_t type) {
    if (peek()->type == type) {
        advance();
        return 1;
    }
    return 0;
}

static void skip_newlines(void) {
    while (peek()->type == TOK_NEWLINE || peek()->type == TOK_SEMICOLON ||
           peek()->type == TOK_HASH_BANG) {
        advance();
    }
}

/* Is this token one that starts/is a "word-like" thing? */
static int is_word_token(token_type_t t) {
    return t == TOK_WORD || t == TOK_STRING || t == TOK_VARIABLE ||
           t == TOK_ARITH;
}

/* ── node allocation ───────────────────────────────────────────────── */

static ast_node_t *alloc_node(node_type_t type) {
    ast_node_t *n = kmalloc(sizeof(ast_node_t));
    if (!n) {
        KERROR("CupidScript: out of memory allocating AST node");
        return NULL;
    }
    memset(n, 0, sizeof(ast_node_t));
    n->type = type;
    return n;
}

/* ── forward declarations ──────────────────────────────────────────── */
static ast_node_t *parse_statement(void);
static ast_node_t *parse_block(token_type_t end1, token_type_t end2);

/* ── copy a token's value into an argv slot, restoring original form ── */
static void copy_token_to_argv(char *dst, token_t *t, int max) {
    int i = 0;
    switch (t->type) {
    case TOK_VARIABLE:
        /* Restore $name form so expand() will recognize it */
        dst[i++] = '$';
        {
            int j = 0;
            while (t->value[j] && i < max - 1) {
                dst[i++] = t->value[j++];
            }
        }
        dst[i] = '\0';
        return;
    case TOK_ARITH:
        /* Restore $((expr)) form so expand() will evaluate it */
        if (i < max - 1) dst[i++] = '$';
        if (i < max - 1) dst[i++] = '(';
        if (i < max - 1) dst[i++] = '(';
        {
            int j = 0;
            while (t->value[j] && i < max - 1) {
                dst[i++] = t->value[j++];
            }
        }
        if (i < max - 1) dst[i++] = ')';
        if (i < max - 1) dst[i++] = ')';
        dst[i] = '\0';
        return;
    default:
        /* TOK_WORD, TOK_STRING, etc — copy as-is */
        {
            int j = 0;
            while (t->value[j] && j < max - 1) {
                dst[j] = t->value[j];
                j++;
            }
            dst[j] = '\0';
        }
        return;
    }
}

/* ── parse a test expression: [ arg1 op arg2 ] ─────────────────────── */
static ast_node_t *parse_test(void) {
    /* The [ has already been consumed by the caller */
    ast_node_t *n = alloc_node(NODE_TEST);
    if (!n) return NULL;

    n->data.test.argc = 0;

    while (!at_end() && peek()->type != TOK_RBRACKET) {
        if (peek()->type == TOK_NEWLINE) break;

        token_t *t = advance();
        if (n->data.test.argc < MAX_ARGS) {
            int idx = n->data.test.argc++;
            /* Use helper to restore $var / $((arith)) prefixes */
            copy_token_to_argv(n->data.test.argv[idx], t, MAX_TOKEN_LEN);
        }
    }

    /* consume ] */
    match(TOK_RBRACKET);

    return n;
}

/* ── parse if/then/else/fi ─────────────────────────────────────────── */
static ast_node_t *parse_if(void) {
    ast_node_t *n = alloc_node(NODE_IF);
    if (!n) return NULL;

    /* Parse condition: everything up to 'then'
     * Typically: [ test expression ] */
    skip_newlines();

    if (peek()->type == TOK_LBRACKET) {
        advance(); /* consume [ */
        n->data.if_stmt.condition = parse_test();
    } else {
        /* Condition is a command (exit status) */
        n->data.if_stmt.condition = parse_statement();
    }

    /* expect: ; then  or  \n then */
    skip_newlines();
    if (peek()->type == TOK_SEMICOLON) advance();
    skip_newlines();
    if (peek()->type != TOK_THEN) {
        KERROR("CupidScript: expected 'then' on line %d", peek()->line);
        return n;
    }
    advance(); /* consume then */

    /* Parse then-body */
    n->data.if_stmt.then_body = parse_block(TOK_ELSE, TOK_FI);

    /* Check if we got else or fi */
    if (peek()->type == TOK_ELSE) {
        advance(); /* consume else */
        skip_newlines();
        n->data.if_stmt.else_body = parse_block(TOK_FI, TOK_FI);
    }

    if (peek()->type == TOK_FI) {
        advance(); /* consume fi */
    } else {
        KERROR("CupidScript: expected 'fi' on line %d", peek()->line);
    }

    return n;
}

/* ── parse while/do/done ───────────────────────────────────────────── */
static ast_node_t *parse_while(void) {
    ast_node_t *n = alloc_node(NODE_WHILE);
    if (!n) return NULL;

    skip_newlines();

    /* Parse condition */
    if (peek()->type == TOK_LBRACKET) {
        advance();
        n->data.while_stmt.condition = parse_test();
    } else {
        n->data.while_stmt.condition = parse_statement();
    }

    /* expect: ; do  or  \n do */
    skip_newlines();
    if (peek()->type == TOK_SEMICOLON) advance();
    skip_newlines();
    if (peek()->type != TOK_DO) {
        KERROR("CupidScript: expected 'do' on line %d", peek()->line);
        return n;
    }
    advance(); /* consume do */

    /* Parse body */
    n->data.while_stmt.body = parse_block(TOK_DONE, TOK_DONE);

    if (peek()->type == TOK_DONE) {
        advance();
    } else {
        KERROR("CupidScript: expected 'done' on line %d", peek()->line);
    }

    return n;
}

/* ── parse for/in/do/done ──────────────────────────────────────────── */
static ast_node_t *parse_for(void) {
    ast_node_t *n = alloc_node(NODE_FOR);
    if (!n) return NULL;

    skip_newlines();

    /* Variable name */
    if (peek()->type == TOK_WORD) {
        token_t *var = advance();
        int i = 0;
        while (var->value[i] && i < MAX_VAR_NAME - 1) {
            n->data.for_stmt.var_name[i] = var->value[i];
            i++;
        }
        n->data.for_stmt.var_name[i] = '\0';
    } else {
        KERROR("CupidScript: expected variable name after 'for' on line %d",
               peek()->line);
        return n;
    }

    /* expect 'in' */
    skip_newlines();
    if (peek()->type != TOK_IN) {
        KERROR("CupidScript: expected 'in' on line %d", peek()->line);
        return n;
    }
    advance(); /* consume in */

    /* Word list until ; or newline or do */
    n->data.for_stmt.word_count = 0;
    while (!at_end() && peek()->type != TOK_SEMICOLON &&
           peek()->type != TOK_NEWLINE && peek()->type != TOK_DO) {
        if (is_word_token(peek()->type)) {
            token_t *w = advance();
            if (n->data.for_stmt.word_count < MAX_WORD_LIST) {
                int idx = n->data.for_stmt.word_count++;
                copy_token_to_argv(n->data.for_stmt.word_list[idx],
                                   w, MAX_TOKEN_LEN);
            }
        } else {
            advance(); /* skip unexpected tokens in word list */
        }
    }

    /* expect ; do or \n do */
    skip_newlines();
    if (peek()->type == TOK_SEMICOLON) advance();
    skip_newlines();
    if (peek()->type != TOK_DO) {
        KERROR("CupidScript: expected 'do' on line %d", peek()->line);
        return n;
    }
    advance(); /* consume do */

    /* Parse body */
    n->data.for_stmt.body = parse_block(TOK_DONE, TOK_DONE);

    if (peek()->type == TOK_DONE) {
        advance();
    } else {
        KERROR("CupidScript: expected 'done' on line %d", peek()->line);
    }

    return n;
}

/* ── parse function definition: name() { body } ───────────────────── */
static ast_node_t *parse_function_def(const char *name) {
    ast_node_t *n = alloc_node(NODE_FUNCTION_DEF);
    if (!n) return NULL;

    int i = 0;
    while (name[i] && i < MAX_VAR_NAME - 1) {
        n->data.function_def.name[i] = name[i];
        i++;
    }
    n->data.function_def.name[i] = '\0';

    /* We've already consumed name( ) — now expect { */
    skip_newlines();
    if (peek()->type != TOK_LBRACE) {
        KERROR("CupidScript: expected '{' for function '%s' on line %d",
               name, peek()->line);
        return n;
    }
    advance(); /* consume { */

    /* Parse body until } */
    n->data.function_def.body = parse_block(TOK_RBRACE, TOK_RBRACE);

    if (peek()->type == TOK_RBRACE) {
        advance();
    } else {
        KERROR("CupidScript: expected '}' on line %d", peek()->line);
    }

    return n;
}

/* ── parse a simple command or assignment ──────────────────────────── */
static ast_node_t *parse_command_or_assignment(void) {
    token_t *first = advance();

    /* Check for assignment: WORD = VALUE */
    if (first->type == TOK_WORD && peek()->type == TOK_ASSIGN) {
        ast_node_t *n = alloc_node(NODE_ASSIGNMENT);
        if (!n) return NULL;

        int i = 0;
        while (first->value[i] && i < MAX_VAR_NAME - 1) {
            n->data.assignment.name[i] = first->value[i];
            i++;
        }
        n->data.assignment.name[i] = '\0';

        advance(); /* consume = */

        /* Value: could be a word, string, variable, arith, or empty.
         * The lexer may emit an empty TOK_WORD before $var or $((expr))
         * because $ is not a word char.  Skip empty words and grab the
         * real value token that follows. */
        if (is_word_token(peek()->type)) {
            token_t *val = advance();
            /* If the word is empty and the next token is a variable or
             * arithmetic expression, that is the real value. */
            if (val->value[0] == '\0' && is_word_token(peek()->type)) {
                val = advance();
            }
            copy_token_to_argv(n->data.assignment.value, val,
                               MAX_VAR_VALUE);
        } else {
            /* Empty value */
            n->data.assignment.value[0] = '\0';
        }

        return n;
    }

    /* Check for function definition: name() { */
    if (first->type == TOK_WORD && peek()->type == TOK_LPAREN) {
        /* Look ahead for ) */
        int save = tok_pos;
        advance(); /* consume ( */
        if (peek()->type == TOK_RPAREN) {
            advance(); /* consume ) */
            return parse_function_def(first->value);
        }
        /* Not a function def, backtrack */
        tok_pos = save;
    }

    /* Check for return statement */
    if (first->type == TOK_RETURN) {
        ast_node_t *n = alloc_node(NODE_RETURN);
        if (!n) return NULL;
        n->data.return_stmt.exit_code = 0;
        /* Optional exit code */
        if (peek()->type == TOK_WORD) {
            token_t *code = advance();
            /* Parse integer */
            int val = 0;
            int j = 0;
            while (code->value[j] >= '0' && code->value[j] <= '9') {
                val = val * 10 + (code->value[j] - '0');
                j++;
            }
            n->data.return_stmt.exit_code = val;
        }
        return n;
    }

    /* Regular command: collect all words on this line */
    ast_node_t *n = alloc_node(NODE_COMMAND);
    if (!n) return NULL;

    /* First word is argv[0] */
    n->data.command.argc = 0;
    copy_token_to_argv(n->data.command.argv[0], first, MAX_TOKEN_LEN);
    n->data.command.argc = 1;

    /* Collect remaining arguments */
    while (!at_end() && peek()->type != TOK_NEWLINE &&
           peek()->type != TOK_SEMICOLON &&
           peek()->type != TOK_RBRACE) {
        if (is_word_token(peek()->type)) {
            token_t *arg = advance();
            if (n->data.command.argc < MAX_ARGS) {
                int idx = n->data.command.argc++;
                copy_token_to_argv(n->data.command.argv[idx],
                                   arg, MAX_TOKEN_LEN);
            }
        } else {
            break;
        }
    }

    return n;
}

/* ── parse a single statement ──────────────────────────────────────── */
static ast_node_t *parse_statement(void) {
    skip_newlines();

    if (at_end()) return NULL;

    token_type_t tt = peek()->type;

    if (tt == TOK_IF)    { advance(); return parse_if();    }
    if (tt == TOK_WHILE) { advance(); return parse_while(); }
    if (tt == TOK_FOR)   { advance(); return parse_for();   }

    /* Word-like tokens start commands, assignments, or function defs */
    if (is_word_token(tt) || tt == TOK_RETURN) {
        return parse_command_or_assignment();
    }

    /* Skip other tokens */
    advance();
    return NULL;
}

/* ── parse a block of statements until end1 or end2 token ──────────── */
static ast_node_t *parse_block(token_type_t end1, token_type_t end2) {
    ast_node_t *seq = alloc_node(NODE_SEQUENCE);
    if (!seq) return NULL;

    seq->data.sequence.count = 0;

    while (!at_end()) {
        skip_newlines();
        if (at_end()) break;

        token_type_t tt = peek()->type;
        if (tt == end1 || tt == end2) break;

        ast_node_t *stmt = parse_statement();
        if (stmt && seq->data.sequence.count < MAX_SEQUENCE) {
            seq->data.sequence.statements[seq->data.sequence.count++] = stmt;
        }
    }

    return seq;
}

/* ══════════════════════════════════════════════════════════════════════
 *  cupidscript_parse
 *
 *  Parses `tokens` (token_count entries) and returns an AST root.
 * ══════════════════════════════════════════════════════════════════════ */
ast_node_t *cupidscript_parse(token_t *tokens, int token_count) {
    toks = tokens;
    tok_count = token_count;
    tok_pos = 0;

    ast_node_t *root = alloc_node(NODE_SEQUENCE);
    if (!root) return NULL;

    root->data.sequence.count = 0;

    while (!at_end()) {
        skip_newlines();
        if (at_end()) break;

        ast_node_t *stmt = parse_statement();
        if (stmt && root->data.sequence.count < MAX_SEQUENCE) {
            root->data.sequence.statements[root->data.sequence.count++] = stmt;
        }
    }

    KDEBUG("CupidScript parser: %d top-level statements",
           root->data.sequence.count);
    return root;
}

/* ══════════════════════════════════════════════════════════════════════
 *  cupidscript_free_ast
 *
 *  Recursively frees an AST node and all its children.
 * ══════════════════════════════════════════════════════════════════════ */
void cupidscript_free_ast(ast_node_t *node) {
    if (!node) return;

    switch (node->type) {
    case NODE_IF:
        cupidscript_free_ast(node->data.if_stmt.condition);
        cupidscript_free_ast(node->data.if_stmt.then_body);
        cupidscript_free_ast(node->data.if_stmt.else_body);
        break;
    case NODE_WHILE:
        cupidscript_free_ast(node->data.while_stmt.condition);
        cupidscript_free_ast(node->data.while_stmt.body);
        break;
    case NODE_FOR:
        cupidscript_free_ast(node->data.for_stmt.body);
        break;
    case NODE_FUNCTION_DEF:
        /* Don't free function body — it may be referenced by context */
        break;
    case NODE_SEQUENCE:
        for (int i = 0; i < node->data.sequence.count; i++) {
            cupidscript_free_ast(node->data.sequence.statements[i]);
        }
        break;
    case NODE_COMMAND:
    case NODE_ASSIGNMENT:
    case NODE_RETURN:
    case NODE_TEST:
        /* No child nodes to free */
        break;
    }

    kfree(node);
}
