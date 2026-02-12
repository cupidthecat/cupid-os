/**
 * cupidc.h — CupidC compiler for CupidOS
 *
 * A HolyC-inspired C compiler that compiles directly to x86 machine code
 * and can emit ELF32 binaries.  Runs in ring 0 with full system access.
 *
 * Features:
 *   - JIT mode: compile .cc source and execute immediately
 *   - AOT mode: compile .cc source to an ELF32 binary on disk
 *   - Types: int, char, void, pointers, arrays
 *   - Control flow: if/else, while, for, return
 *   - Functions with cdecl calling convention
 *   - Inline assembly (asm { ... })
 *   - Direct port I/O via inb()/outb() builtins
 *   - Full kernel API access via predefined bindings
 */

#ifndef CUPIDC_H
#define CUPIDC_H

#include "types.h"

/* ══════════════════════════════════════════════════════════════════════
 *  Limits
 * ══════════════════════════════════════════════════════════════════════ */
#define CC_MAX_CODE (256u * 1024u) /* 256KB code buffer          */
#define CC_MAX_DATA (128u * 1024u) /* 128KB data/string buffer   */
#define CC_MAX_SYMBOLS 4096        /* max symbols in scope        */
#define CC_MAX_LOCALS 256          /* max locals per function     */
#define CC_MAX_PARAMS 16           /* max function parameters     */
#define CC_MAX_PATCHES 512         /* max forward-ref patches     */
#define CC_MAX_BREAKS 64           /* max nested loop depth        */
#define CC_MAX_BREAKS_PER_LOOP 32  /* max break statements per loop */
#define CC_MAX_IDENT 64            /* max identifier length       */
#define CC_MAX_STRING 128          /* max string literal length   */
#define CC_MAX_ERRORS 1            /* fail-fast: stop at first    */
#define CC_MAX_FUNCS 512           /* max functions               */
#define CC_MAX_STRUCTS 32          /* max struct definitions       */
#define CC_MAX_FIELDS 16           /* max fields per struct        */

/* Memory region for JIT code/data */
#define CC_JIT_CODE_BASE 0x00400000u
#define CC_JIT_DATA_BASE (CC_JIT_CODE_BASE + CC_MAX_CODE)

/* Memory region for AOT-compiled ELF output — must be >= 0x400000 */
#define CC_AOT_CODE_BASE 0x00400000u
#define CC_AOT_DATA_BASE (CC_AOT_CODE_BASE + CC_MAX_CODE)

/* ══════════════════════════════════════════════════════════════════════
 *  Token Types
 * ══════════════════════════════════════════════════════════════════════ */
typedef enum {
  /* Keywords */
  CC_TOK_INT,
  CC_TOK_CHAR,
  CC_TOK_VOID,
  CC_TOK_IF,
  CC_TOK_ELSE,
  CC_TOK_WHILE,
  CC_TOK_FOR,
  CC_TOK_RETURN,
  CC_TOK_ASM,
  CC_TOK_BREAK,
  CC_TOK_CONTINUE,
  CC_TOK_STRUCT,
  CC_TOK_SIZEOF,
  CC_TOK_DO,
  CC_TOK_SWITCH,
  CC_TOK_CASE,
  CC_TOK_DEFAULT,
  CC_TOK_BOOL,
  CC_TOK_ENUM,
  CC_TOK_UNSIGNED,
  CC_TOK_TYPEDEF,
  CC_TOK_CONST,
  CC_TOK_STATIC,
  CC_TOK_VOLATILE,

  /* Identifiers and literals */
  CC_TOK_IDENT,    /* variable/function names               */
  CC_TOK_NUMBER,   /* integer literals                      */
  CC_TOK_STRING,   /* "string literals"                     */
  CC_TOK_CHAR_LIT, /* 'A'                                   */

  /* Operators */
  CC_TOK_PLUS,
  CC_TOK_MINUS,
  CC_TOK_STAR,
  CC_TOK_SLASH,
  CC_TOK_PERCENT,
  CC_TOK_EQ,
  CC_TOK_EQEQ,
  CC_TOK_NE,
  CC_TOK_LT,
  CC_TOK_GT,
  CC_TOK_LE,
  CC_TOK_GE,
  CC_TOK_AND,
  CC_TOK_OR,
  CC_TOK_NOT,
  CC_TOK_BAND,
  CC_TOK_BOR,
  CC_TOK_BXOR,
  CC_TOK_BNOT,
  CC_TOK_SHL,
  CC_TOK_SHR,
  CC_TOK_PLUSEQ,
  CC_TOK_MINUSEQ,
  CC_TOK_STAREQ,
  CC_TOK_SLASHEQ,
  CC_TOK_ANDEQ,
  CC_TOK_OREQ,
  CC_TOK_XOREQ,
  CC_TOK_SHLEQ,
  CC_TOK_SHREQ,
  CC_TOK_PLUSPLUS,
  CC_TOK_MINUSMINUS,
  CC_TOK_AMP, /* & (address-of, also bitwise AND)      */

  /* Delimiters */
  CC_TOK_LPAREN,
  CC_TOK_RPAREN,
  CC_TOK_LBRACE,
  CC_TOK_RBRACE,
  CC_TOK_LBRACK,
  CC_TOK_RBRACK,
  CC_TOK_SEMICOLON,
  CC_TOK_COMMA,
  CC_TOK_DOT,
  CC_TOK_ELLIPSIS,
  CC_TOK_ARROW,
  CC_TOK_COLON,
  CC_TOK_QUESTION,

  CC_TOK_EOF,
  CC_TOK_ERROR
} cc_token_type_t;

/* ── Token structure ─────────────────────────────────────────────── */
typedef struct {
  cc_token_type_t type;
  char text[CC_MAX_STRING]; /* holds idents & string content */
  int32_t int_value;
  int line;
} cc_token_t;

/* ══════════════════════════════════════════════════════════════════════
 *  Symbol Table
 * ══════════════════════════════════════════════════════════════════════ */

/* Symbol kind */
typedef enum {
  SYM_LOCAL,  /* local variable (EBP-relative)         */
  SYM_PARAM,  /* function parameter (EBP+relative)     */
  SYM_FUNC,   /* user-defined function                 */
  SYM_KERNEL, /* kernel binding (absolute address)     */
  SYM_GLOBAL  /* global variable in data section       */
} cc_sym_kind_t;

/* Type representation */
typedef enum {
  TYPE_INT,        /* 32-bit int                            */
  TYPE_CHAR,       /* 8-bit char                            */
  TYPE_VOID,       /* void (functions only)                 */
  TYPE_PTR,        /* pointer (any)                         */
  TYPE_INT_PTR,    /* int*                                  */
  TYPE_CHAR_PTR,   /* char*                                 */
  TYPE_STRUCT,     /* struct value (stack-allocated)         */
  TYPE_STRUCT_PTR, /* pointer to struct                     */
  TYPE_FUNC_PTR    /* int (*fn)(...) — function pointer     */
} cc_type_t;

/* Symbol entry */
typedef struct {
  char name[CC_MAX_IDENT];
  cc_sym_kind_t kind;
  cc_type_t type;
  int32_t offset;   /* stack offset or code offset       */
  uint32_t address; /* absolute address (kernel/func)    */
  int param_count;  /* for functions                     */
  int is_defined;   /* has function body been emitted?   */
  int is_array;     /* stack-allocated array?             */
  int struct_index; /* index into structs[] for struct types */
  int array_elem_size; /* element size for array subscript scaling */
} cc_symbol_t;

/* ── Struct field definition ─────────────────────────────────────── */
typedef struct {
  char name[CC_MAX_IDENT];
  cc_type_t type;
  int32_t offset;   /* byte offset within struct         */
  int struct_index; /* if type is struct, which struct    */
  int array_count;  /* >0 if this field is a fixed array  */
} cc_field_t;

/* ── Struct definition ───────────────────────────────────────────── */
typedef struct {
  char name[CC_MAX_IDENT];
  cc_field_t fields[CC_MAX_FIELDS];
  int field_count;
  int32_t total_size; /* total size in bytes (includes field padding) */
  int32_t align;      /* natural alignment (1 or 4 for current types) */
  int is_complete;    /* 1 after full definition parsed, 0 for forward tag */
} cc_struct_def_t;

/* ── Forward reference patch ─────────────────────────────────────── */
typedef struct {
  uint32_t code_offset;    /* where in code buffer to patch     */
  char name[CC_MAX_IDENT]; /* target symbol name                */
} cc_patch_t;

/* ══════════════════════════════════════════════════════════════════════
 *  Compiler State
 * ══════════════════════════════════════════════════════════════════════ */
typedef struct {
  /* Source */
  const char *source;
  int pos;
  int line;

  /* Current/peeked token */
  cc_token_t cur;
  cc_token_t peek_buf;
  int has_peek;

  /* Code generation */
  uint8_t *code;      /* code output buffer                */
  uint32_t code_pos;  /* current write position in code    */
  uint32_t code_base; /* base address of code in memory    */

  /* Data section (string literals, globals) */
  uint8_t *data;      /* data output buffer                */
  uint32_t data_pos;  /* current write position in data    */
  uint32_t data_base; /* base address of data in memory    */

  /* Symbol table */
  cc_symbol_t symbols[CC_MAX_SYMBOLS];
  int sym_count;

  /* Struct definitions */
  cc_struct_def_t structs[CC_MAX_STRUCTS];
  int struct_count;

  /* Local scope tracking */
  int local_offset;     /* current stack offset for locals   */
  int max_local_offset; /* deepest stack offset seen (most negative) */
  int scope_start;      /* symbol index at function start    */
  int param_count;      /* params in current function        */

  /* Forward reference patches */
  cc_patch_t patches[CC_MAX_PATCHES];
  int patch_count;

  /* Break/continue stack for loops */
  uint32_t break_patches[CC_MAX_BREAKS][CC_MAX_BREAKS_PER_LOOP];
  int break_counts[CC_MAX_BREAKS];
  uint32_t continue_targets[CC_MAX_BREAKS];
  int loop_depth;

  /* Error state */
  int error;
  char error_msg[128];

  /* Entry point */
  uint32_t entry_offset; /* offset of main() in code          */
  int has_entry;

  /* Mode */
  int jit_mode; /* 1 = JIT (execute), 0 = AOT (save)*/

  /* Typedef aliases (global scope only) */
  char typedef_names[16][CC_MAX_IDENT];
  cc_type_t typedef_types[16];
  int typedef_count;
} cc_state_t;

/* ══════════════════════════════════════════════════════════════════════
 *  Public API
 * ══════════════════════════════════════════════════════════════════════ */

/**
 * cupidc_jit — Compile and immediately execute a .cc source file.
 *
 * @param path  VFS path to the .cc source file
 */
void cupidc_jit(const char *path);

/**
 * cupidc_aot — Compile a .cc source to an ELF32 binary on disk.
 *
 * @param src_path   VFS path to the .cc source file
 * @param out_path   VFS path for the output ELF binary
 */
void cupidc_aot(const char *src_path, const char *out_path);

/* ── Lexer API (used internally) ─────────────────────────────────── */
void cc_lex_init(cc_state_t *cc, const char *source);
cc_token_t cc_lex_next(cc_state_t *cc);
cc_token_t cc_lex_peek(cc_state_t *cc);

/* ── Parser + Codegen API (used internally) ──────────────────────── */
void cc_parse_program(cc_state_t *cc);

/* ── ELF Writer API (used internally) ────────────────────────────── */
int cc_write_elf(cc_state_t *cc, const char *path);

/* ── Symbol table API (used internally) ──────────────────────────── */
void cc_sym_init(cc_state_t *cc);
cc_symbol_t *cc_sym_find(cc_state_t *cc, const char *name);
cc_symbol_t *cc_sym_add(cc_state_t *cc, const char *name, cc_sym_kind_t kind,
                        cc_type_t type);

#endif /* CUPIDC_H */
