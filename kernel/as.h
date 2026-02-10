/**
 * as.h — CupidASM assembler for CupidOS
 *
 * An x86-32 assembler that runs inside CupidOS.  Intel-style syntax,
 * single-pass encoding with forward-reference patch table, JIT and
 * AOT output modes.  Integrates with VFS and the shell.
 *
 * Features:
 *   - JIT mode: assemble .asm source and execute immediately
 *   - AOT mode: assemble .asm source to an ELF32 binary on disk
 *   - Intel-style flat 32-bit syntax
 *   - Labels, forward references, equ constants
 *   - Data directives: db, dw, dd
 *   - Section directives: .text, .data
 *   - %include for file inclusion (depth-limited)
 */

#ifndef AS_H
#define AS_H

#include "types.h"

/* ══════════════════════════════════════════════════════════════════════
 *  Limits
 * ══════════════════════════════════════════════════════════════════════ */
#define AS_MAX_CODE    (128u * 1024u) /* 128KB code buffer           */
#define AS_MAX_DATA    (32u * 1024u)  /* 32KB data buffer            */
#define AS_MAX_LABELS  512            /* max labels                  */
#define AS_MAX_PATCHES 512            /* max forward-ref patches     */
#define AS_MAX_IDENT   64             /* max identifier length       */
#define AS_MAX_STRING  256            /* max string literal length    */
#define AS_MAX_INCLUDE_DEPTH 4        /* max nested %include depth   */
#define AS_MAX_LINE    256            /* max source line length       */

/* Memory region for assembler JIT (separate from CupidC's 0x400000) */
#define AS_JIT_CODE_BASE  0x00500000u
#define AS_JIT_DATA_BASE  0x00520000u  /* 128KB after code */

/* Memory region for AOT output (uses same addresses as JIT) */
#define AS_AOT_CODE_BASE  0x00500000u
#define AS_AOT_DATA_BASE  0x00520000u

/* ══════════════════════════════════════════════════════════════════════
 *  Token Types
 * ══════════════════════════════════════════════════════════════════════ */
typedef enum {
  AS_TOK_MNEMONIC,    /* mov, push, call, ret, ...           */
  AS_TOK_REGISTER,    /* eax, ebx, esp, al, cx, ...          */
  AS_TOK_NUMBER,      /* 42, 0xFF, 0b1010                    */
  AS_TOK_LABEL_DEF,   /* main:                               */
  AS_TOK_IDENT,       /* label ref, equ name                 */
  AS_TOK_DIRECTIVE,   /* db, dw, dd, equ, section, %include  */
  AS_TOK_STRING,      /* "hello"                             */
  AS_TOK_LBRACK,      /* [                                   */
  AS_TOK_RBRACK,      /* ]                                   */
  AS_TOK_PLUS,        /* +                                   */
  AS_TOK_MINUS,       /* -                                   */
  AS_TOK_STAR,        /* *                                   */
  AS_TOK_COMMA,       /* ,                                   */
  AS_TOK_COLON,       /* :                                   */
  AS_TOK_NEWLINE,     /* end of line (instruction boundary)  */
  AS_TOK_EOF,         /* end of file                         */
  AS_TOK_ERROR        /* lexer error                         */
} as_token_type_t;

/* ── Token structure ─────────────────────────────────────────────── */
typedef struct {
  as_token_type_t type;
  char text[AS_MAX_IDENT]; /* holds idents, mnemonics, strings */
  int32_t int_value;       /* for AS_TOK_NUMBER                */
  int reg_index;           /* for AS_TOK_REGISTER (0=eax..7)   */
  int reg_size;            /* 1=8-bit, 2=16-bit, 4=32-bit      */
  int line;
} as_token_t;

/* ══════════════════════════════════════════════════════════════════════
 *  Instruction Encoding
 * ══════════════════════════════════════════════════════════════════════ */

/* Operand forms */
typedef enum {
  FORM_NONE,       /* ret, nop                              */
  FORM_REG,        /* push eax                              */
  FORM_IMM,        /* push 42                               */
  FORM_REG_REG,    /* mov eax, ebx                          */
  FORM_REG_IMM,    /* mov eax, 42                           */
  FORM_REG_MEM,    /* mov eax, [ebx+disp]                   */
  FORM_MEM_REG,    /* mov [ebx+disp], eax                   */
  FORM_MEM_IMM,    /* mov [ebx], 42                         */
  FORM_REL8,       /* jmp short label                       */
  FORM_REL32,      /* jmp label / call label                */
  FORM_REG_ONLY,   /* inc eax (/r encoded in opcode)        */
} as_form_t;

/* Encoding table row */
typedef struct {
  const char  *mnemonic;
  as_form_t    form;
  uint8_t      op[3];     /* opcode bytes (1-3)                */
  int          op_len;    /* number of opcode bytes            */
  int8_t       digit;     /* ModRM /digit (-1 = use reg)       */
  int          imm_size;  /* 1 or 4, 0 = none                  */
  int          plus_reg;  /* 1 = last opcode byte += reg index */
} as_enc_t;

/* ══════════════════════════════════════════════════════════════════════
 *  Label Table & Forward References
 * ══════════════════════════════════════════════════════════════════════ */

/* Defined label / equ constant */
typedef struct {
  char     name[AS_MAX_IDENT];
  uint32_t address;   /* absolute address in code/data buffer  */
  int      defined;   /* 1 = address is valid                  */
  int      is_equ;    /* 1 = equ constant (address = value)    */
} as_label_t;

/* Forward-reference patch */
typedef struct {
  uint32_t code_offset;        /* where to write the resolved value  */
  char     name[AS_MAX_IDENT];
  int      rel;                /* 1=relative (jmp/call), 0=absolute  */
  int      width;              /* 1=byte (rel8), 4=dword             */
} as_patch_t;

/* ══════════════════════════════════════════════════════════════════════
 *  Assembler State
 * ══════════════════════════════════════════════════════════════════════ */
typedef struct {
  /* Source */
  const char *source;
  int pos;
  int line;

  /* Current/peeked token */
  as_token_t cur;
  as_token_t peek_buf;
  int has_peek;

  /* Code generation */
  uint8_t  *code;       /* code output buffer                  */
  uint32_t  code_pos;   /* current write position in code      */
  uint32_t  code_base;  /* base address of code in memory      */

  /* Data section (db/dw/dd, string literals) */
  uint8_t  *data;       /* data output buffer                  */
  uint32_t  data_pos;   /* current write position in data      */
  uint32_t  data_base;  /* base address of data in memory      */

  /* Label table */
  as_label_t labels[AS_MAX_LABELS];
  int label_count;

  /* Forward reference patches */
  as_patch_t patches[AS_MAX_PATCHES];
  int patch_count;

  /* Current section: 0=text, 1=data */
  int current_section;

  /* Error state */
  int  error;
  char error_msg[128];

  /* Entry point */
  uint32_t entry_offset; /* offset of main/entry label in code */
  int has_entry;

  /* Mode */
  int jit_mode;  /* 1 = JIT (execute), 0 = AOT (save)  */

  /* Include depth tracking */
  int include_depth;
} as_state_t;

/* ══════════════════════════════════════════════════════════════════════
 *  Public API
 * ══════════════════════════════════════════════════════════════════════ */

/**
 * as_jit — Assemble and immediately execute a .asm source file.
 *
 * @param path  VFS path to the .asm source file
 */
void as_jit(const char *path);

/**
 * as_aot — Assemble a .asm source to an ELF32 binary on disk.
 *
 * @param src_path   VFS path to the .asm source file
 * @param out_path   VFS path for the output ELF binary
 */
void as_aot(const char *src_path, const char *out_path);

/* ── Lexer API (used internally) ─────────────────────────────────── */
void       as_lex_init(as_state_t *as, const char *source);
as_token_t as_lex_next(as_state_t *as);
as_token_t as_lex_peek(as_state_t *as);

/* ── Parser + Encoder API (used internally) ──────────────────────── */
void as_parse_program(as_state_t *as);

/* ── ELF Writer API (used internally) ────────────────────────────── */
int as_write_elf(as_state_t *as, const char *path);

#endif /* AS_H */
