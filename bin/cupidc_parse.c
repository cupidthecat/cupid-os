/**
 * cupidc_parse.c — Parser and x86 code generator for CupidC
 *
 * Single-pass recursive descent parser that emits x86 machine code
 * directly into a code buffer.  Implements the full CupidC language:
 *   - Types: int, char, void, pointers, arrays
 *   - Expressions with full C operator precedence
 *   - Control flow: if/else, while, for, break, continue, return
 *   - Functions with cdecl calling convention
 *   - Inline assembly blocks
 *   - Kernel function bindings (print, kmalloc, etc.)
 *   - Port I/O builtins (inb, outb)
 */

#include "../drivers/serial.h"
#include "cupidc.h"
#include "kernel.h"
#include "string.h"

/* ══════════════════════════════════════════════════════════════════════
 *  x86 Machine Code Emission Helpers
 * ══════════════════════════════════════════════════════════════════════ */

/* Emit a single byte */
static void emit8(cc_state_t *cc, uint8_t b) {
  if (cc->code_pos < CC_MAX_CODE) {
    cc->code[cc->code_pos++] = b;
  } else {
    cc->error = 1;
  }
}

/* Emit a 32-bit little-endian value */
static void emit32(cc_state_t *cc, uint32_t v) {
  emit8(cc, (uint8_t)(v & 0xFF));
  emit8(cc, (uint8_t)((v >> 8) & 0xFF));
  emit8(cc, (uint8_t)((v >> 16) & 0xFF));
  emit8(cc, (uint8_t)((v >> 24) & 0xFF));
}

/* Patch a 32-bit value at a specific offset */
static void patch32(cc_state_t *cc, uint32_t offset, uint32_t value) {
  if (offset + 4 <= CC_MAX_CODE) {
    cc->code[offset] = (uint8_t)(value & 0xFF);
    cc->code[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
    cc->code[offset + 2] = (uint8_t)((value >> 16) & 0xFF);
    cc->code[offset + 3] = (uint8_t)((value >> 24) & 0xFF);
  }
}

/* Current code address (base + position) */
static uint32_t cc_code_addr(cc_state_t *cc) {
  return cc->code_base + cc->code_pos;
}

/* ── x86 instruction emitters ────────────────────────────────────── */

/* mov eax, imm32 */
static void emit_mov_eax_imm(cc_state_t *cc, uint32_t val) {
  emit8(cc, 0xB8);
  emit32(cc, val);
}

/* mov eax, [ebp + offset] (load local/param) */
static void emit_load_local(cc_state_t *cc, int32_t offset) {
  emit8(cc, 0x8B); /* mov eax, [ebp+disp32] */
  emit8(cc, 0x85);
  emit32(cc, (uint32_t)offset);
}

/* mov [ebp + offset], eax (store local/param) */
static void emit_store_local(cc_state_t *cc, int32_t offset) {
  emit8(cc, 0x89); /* mov [ebp+disp32], eax */
  emit8(cc, 0x85);
  emit32(cc, (uint32_t)offset);
}

/* push eax */
static void emit_push_eax(cc_state_t *cc) { emit8(cc, 0x50); }

/* pop eax */
static void emit_pop_eax(cc_state_t *cc) { emit8(cc, 0x58); }

/* pop ebx */
static void emit_pop_ebx(cc_state_t *cc) { emit8(cc, 0x5B); }

/* push imm32 */
static void emit_push_imm(cc_state_t *cc, uint32_t val) {
  emit8(cc, 0x68);
  emit32(cc, val);
}

/* call absolute address */
static void emit_call_abs(cc_state_t *cc, uint32_t addr) {
  uint32_t from = cc_code_addr(cc) + 5;
  int32_t rel = (int32_t)(addr - from);
  emit8(cc, 0xE8);
  emit32(cc, (uint32_t)rel);
}

/* call relative (placeholder — returns offset of the rel32 for patching) */
static uint32_t emit_call_rel_placeholder(cc_state_t *cc) {
  emit8(cc, 0xE8);
  uint32_t patch_pos = cc->code_pos;
  emit32(cc, 0); /* placeholder */
  return patch_pos;
}

/* jmp rel32 (unconditional) — returns offset for patching */
static uint32_t emit_jmp_placeholder(cc_state_t *cc) {
  emit8(cc, 0xE9);
  uint32_t patch_pos = cc->code_pos;
  emit32(cc, 0);
  return patch_pos;
}

/* jcc rel32 (conditional jump) — returns offset for patching */
static uint32_t emit_jcc_placeholder(cc_state_t *cc, uint8_t cond) {
  emit8(cc, 0x0F);
  emit8(cc, cond);
  uint32_t patch_pos = cc->code_pos;
  emit32(cc, 0);
  return patch_pos;
}

/* Patch a relative jump/call target to the current code position */
static void patch_jump(cc_state_t *cc, uint32_t patch_pos) {
  uint32_t target = cc->code_pos;
  uint32_t from = patch_pos + 4; /* instruction after the rel32 */
  int32_t rel = (int32_t)(target - from);
  patch32(cc, patch_pos, (uint32_t)rel);
}

/* add esp, imm8 (clean up stack args) */
static void emit_add_esp(cc_state_t *cc, int32_t val) {
  if (val == 0)
    return;
  emit8(cc, 0x83);
  emit8(cc, 0xC4);
  emit8(cc, (uint8_t)(val & 0xFF));
}

/* sub esp, imm32 (allocate locals) */
static void emit_sub_esp(cc_state_t *cc, uint32_t val) {
  if (val == 0)
    return;
  emit8(cc, 0x81);
  emit8(cc, 0xEC);
  emit32(cc, val);
}

/* Function prologue: push ebp; mov ebp, esp */
static void emit_prologue(cc_state_t *cc) {
  emit8(cc, 0x55); /* push ebp */
  emit8(cc, 0x89); /* mov ebp, esp */
  emit8(cc, 0xE5);
}

/* Function epilogue: mov esp, ebp; pop ebp; ret */
static void emit_epilogue(cc_state_t *cc) {
  emit8(cc, 0x89); /* mov esp, ebp */
  emit8(cc, 0xEC);
  emit8(cc, 0x5D); /* pop ebp */
  emit8(cc, 0xC3); /* ret */
}

/* cmp eax, 0 */
static void emit_cmp_eax_zero(cc_state_t *cc) {
  emit8(cc, 0x83);
  emit8(cc, 0xF8);
  emit8(cc, 0x00);
}

/* ret */
static void emit_ret(cc_state_t *cc) { emit8(cc, 0xC3); }

/* nop */
static void emit_nop(cc_state_t *cc) { emit8(cc, 0x90); }

/* movzx eax, al (zero-extend byte to dword) */
static void emit_movzx_eax_al(cc_state_t *cc) {
  emit8(cc, 0x0F);
  emit8(cc, 0xB6);
  emit8(cc, 0xC0);
}

/* mov [eax], bl (store byte through pointer) */
static void emit_store_byte_ptr(cc_state_t *cc) {
  emit8(cc, 0x88); /* mov [eax], bl */
  emit8(cc, 0x18);
}

/* mov [eax], ebx (store dword through pointer) */
static void emit_store_dword_ptr(cc_state_t *cc) {
  emit8(cc, 0x89); /* mov [eax], ebx */
  emit8(cc, 0x18);
}

/* mov eax, [eax] (dereference dword pointer) */
static void emit_deref_dword(cc_state_t *cc) {
  emit8(cc, 0x8B);
  emit8(cc, 0x00);
}

/* movzx eax, byte [eax] (dereference byte pointer) */
static void emit_deref_byte(cc_state_t *cc) {
  emit8(cc, 0x0F);
  emit8(cc, 0xB6);
  emit8(cc, 0x00);
}

/* lea eax, [ebp + offset] (address of local) */
static void emit_lea_local(cc_state_t *cc, int32_t offset) {
  emit8(cc, 0x8D); /* lea eax, [ebp+disp32] */
  emit8(cc, 0x85);
  emit32(cc, (uint32_t)offset);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Error Handling
 * ══════════════════════════════════════════════════════════════════════ */

static void cc_error(cc_state_t *cc, const char *msg) {
  if (cc->error)
    return; /* already errored */
  cc->error = 1;

  /* Build error message */
  int i = 0;
  const char *prefix = "CupidC Error (line ";
  while (prefix[i] && i < 100) {
    cc->error_msg[i] = prefix[i];
    i++;
  }

  /* line number */
  int line = cc->cur.line;
  if (line == 0)
    line = cc->line;
  char num[12];
  int ni = 0;
  if (line == 0) {
    num[ni++] = '0';
  } else {
    int tmp = line;
    char rev[12];
    int ri = 0;
    while (tmp > 0) {
      rev[ri++] = (char)('0' + tmp % 10);
      tmp /= 10;
    }
    while (ri > 0) {
      num[ni++] = rev[--ri];
    }
  }
  num[ni] = '\0';
  int j = 0;
  while (num[j] && i < 120) {
    cc->error_msg[i++] = num[j++];
  }

  const char *mid = "): ";
  j = 0;
  while (mid[j] && i < 120) {
    cc->error_msg[i++] = mid[j++];
  }

  j = 0;
  while (msg[j] && i < 126) {
    cc->error_msg[i++] = msg[j++];
  }
  cc->error_msg[i++] = '\n';
  cc->error_msg[i] = '\0';
}

/* ══════════════════════════════════════════════════════════════════════
 *  Token Helpers
 * ══════════════════════════════════════════════════════════════════════ */

static cc_token_t cc_next(cc_state_t *cc) { return cc_lex_next(cc); }

static cc_token_t cc_peek(cc_state_t *cc) { return cc_lex_peek(cc); }

static int cc_expect(cc_state_t *cc, cc_token_type_t type) {
  cc_token_t tok = cc_next(cc);
  if (tok.type != type) {
    cc_error(cc, "unexpected token");
    return 0;
  }
  return 1;
}

static int cc_match(cc_state_t *cc, cc_token_type_t type) {
  if (cc_peek(cc).type == type) {
    cc_next(cc);
    return 1;
  }
  return 0;
}

/* ── Check if token is a type keyword ────────────────────────────── */
static int cc_is_type(cc_token_type_t t) {
  return t == CC_TOK_INT || t == CC_TOK_CHAR || t == CC_TOK_VOID ||
         t == CC_TOK_STRUCT || t == CC_TOK_BOOL || t == CC_TOK_UNSIGNED ||
         t == CC_TOK_CONST || t == CC_TOK_VOLATILE;
}

/* ── Find typedef alias, returns type or -1 if not found ─────────── */
static cc_type_t cc_find_typedef(cc_state_t *cc, const char *name) {
  int i;
  for (i = 0; i < cc->typedef_count; i++) {
    if (strcmp(cc->typedef_names[i], name) == 0) {
      return cc->typedef_types[i];
    }
  }
  return (cc_type_t)-1;
}

static int cc_is_type_or_typedef(cc_state_t *cc, cc_token_t tok) {
  return cc_is_type(tok.type) ||
         (tok.type == CC_TOK_IDENT && (int)cc_find_typedef(cc, tok.text) >= 0);
}

/* Track what kind of value the last expression produced */
static cc_type_t cc_last_expr_type;
static int cc_last_expr_struct_index; /* which struct, if TYPE_STRUCT */
static int cc_last_type_struct_index; /* set by cc_parse_type */
static int cc_last_expr_elem_size;    /* element size for array subscripts */

/* ── Struct lookup helper ───────────────────────────────────────────── */
static int cc_find_struct(cc_state_t *cc, const char *name) {
  for (int i = 0; i < cc->struct_count; i++) {
    if (strcmp(cc->structs[i].name, name) == 0)
      return i;
  }
  return -1;
}

static int cc_get_or_add_struct_tag(cc_state_t *cc, const char *name) {
  int si = cc_find_struct(cc, name);
  if (si >= 0)
    return si;

  if (cc->struct_count >= CC_MAX_STRUCTS) {
    cc_error(cc, "too many struct definitions");
    return -1;
  }

  si = cc->struct_count++;
  cc_struct_def_t *sd = &cc->structs[si];
  memset(sd, 0, sizeof(*sd));
  int i = 0;
  while (name[i] && i < CC_MAX_IDENT - 1) {
    sd->name[i] = name[i];
    i++;
  }
  sd->name[i] = '\0';
  sd->align = 4;
  sd->is_complete = 0;
  return si;
}

static int cc_struct_is_complete(cc_state_t *cc, int struct_index) {
  return struct_index >= 0 && struct_index < cc->struct_count &&
         cc->structs[struct_index].is_complete;
}

static cc_field_t *cc_find_field(cc_state_t *cc, int struct_index,
                                 const char *name) {
  if (struct_index < 0 || struct_index >= cc->struct_count)
    return NULL;
  cc_struct_def_t *sd = &cc->structs[struct_index];
  for (int i = 0; i < sd->field_count; i++) {
    if (strcmp(sd->fields[i].name, name) == 0)
      return &sd->fields[i];
  }
  return NULL;
}

/* ── Parse a type specifier, returns cc_type_t ───────────────────── */
static cc_type_t cc_parse_type(cc_state_t *cc) {
  cc_token_t tok = cc_next(cc);
  cc_type_t base;
  cc_last_type_struct_index = -1;

  /* Strip qualifiers: const, unsigned, volatile (order-agnostic). */
  while (tok.type == CC_TOK_CONST || tok.type == CC_TOK_UNSIGNED ||
         tok.type == CC_TOK_VOLATILE) {
    tok = cc_next(cc);
  }

  switch (tok.type) {
  case CC_TOK_INT:
    base = TYPE_INT;
    break;
  case CC_TOK_CHAR:
    base = TYPE_CHAR;
    break;
  case CC_TOK_VOID:
    base = TYPE_VOID;
    break;
  case CC_TOK_BOOL:
    base = TYPE_INT;
    break; /* bool is alias for int */
  case CC_TOK_IDENT: {
    /* Check if this is a typedef alias */
    cc_type_t td = cc_find_typedef(cc, tok.text);
    if ((int)td >= 0) {
      base = td;
      break;
    }
    cc_error(cc, "expected type");
    return TYPE_INT;
  }
  case CC_TOK_STRUCT: {
    cc_token_t name_tok = cc_next(cc);
    if (name_tok.type != CC_TOK_IDENT) {
      cc_error(cc, "expected struct name");
      return TYPE_INT;
    }
    int si = cc_get_or_add_struct_tag(cc, name_tok.text);
    if (si < 0)
      return TYPE_INT;
    cc_last_type_struct_index = si;
    base = TYPE_STRUCT;
    break;
  }
  default:
    cc_error(cc, "expected type");
    return TYPE_INT;
  }

  /* Allow trailing qualifiers after base type (e.g. char const *). */
  while (cc_peek(cc).type == CC_TOK_CONST ||
         cc_peek(cc).type == CC_TOK_UNSIGNED ||
         cc_peek(cc).type == CC_TOK_VOLATILE)
    cc_next(cc);

  /* Pointer depth support: T*, T**, ... */
  int pointer_depth = 0;
  while (cc_peek(cc).type == CC_TOK_STAR) {
    cc_next(cc);
    pointer_depth++;
    /* Ignore pointer qualifiers: char *const, char *const * ... */
    while (cc_peek(cc).type == CC_TOK_CONST ||
           cc_peek(cc).type == CC_TOK_UNSIGNED ||
           cc_peek(cc).type == CC_TOK_VOLATILE) {
      cc_next(cc);
    }
  }

  if (pointer_depth <= 0)
    return base;

  if (pointer_depth == 1) {
    if (base == TYPE_INT)
      return TYPE_INT_PTR;
    if (base == TYPE_CHAR)
      return TYPE_CHAR_PTR;
    if (base == TYPE_STRUCT)
      return TYPE_STRUCT_PTR;
    return TYPE_PTR;
  }

  /* Depth >= 2 currently collapses to generic pointer type. */
  return TYPE_PTR;
}

static int32_t cc_align_up(int32_t value, int32_t align) {
  if (align <= 1)
    return value;
  return (value + align - 1) & ~(align - 1);
}

static int32_t cc_type_align(cc_state_t *cc, cc_type_t type, int struct_index) {
  switch (type) {
  case TYPE_CHAR:
    return 1;
  case TYPE_STRUCT:
    if (struct_index >= 0 && struct_index < cc->struct_count &&
        cc->structs[struct_index].align > 0)
      return cc->structs[struct_index].align;
    return 4;
  default:
    return 4;
  }
}

static int32_t cc_type_size(cc_state_t *cc, cc_type_t type, int struct_index) {
  switch (type) {
  case TYPE_CHAR:
    return 1;
  case TYPE_VOID:
    return 0;
  case TYPE_STRUCT:
    if (struct_index >= 0 && struct_index < cc->struct_count)
      return cc->structs[struct_index].total_size;
    return 0;
  default:
    return 4;
  }
}

static int32_t cc_sizeof_symbol_deref(cc_state_t *cc, cc_symbol_t *sym,
                                      int deref_count) {
  cc_type_t type = sym->type;
  int struct_index = sym->struct_index;
  int elem_size = sym->array_elem_size;
  int is_array = sym->is_array;

  int i;
  for (i = 0; i < deref_count; i++) {
    int last = (i == deref_count - 1);

    if (is_array) {
      if (type == TYPE_STRUCT_PTR) {
        if (last)
          return cc_type_size(cc, TYPE_STRUCT, struct_index);
        type = TYPE_STRUCT;
        is_array = 0;
        continue;
      }
      if (type == TYPE_CHAR_PTR) {
        if (elem_size > 1) {
          if (last)
            return elem_size; /* row size of char[][] */
          type = TYPE_CHAR_PTR;
          elem_size = 1;
          is_array = 0;
          continue;
        }
        if (last)
          return 1;
        type = TYPE_CHAR;
        is_array = 0;
        continue;
      }
      if (type == TYPE_INT_PTR) {
        if (elem_size > 4) {
          if (last)
            return elem_size; /* row size of int[][] */
          type = TYPE_INT_PTR;
          elem_size = 4;
          is_array = 0;
          continue;
        }
        if (last)
          return 4;
        type = TYPE_INT;
        is_array = 0;
        continue;
      }
    }

    if (type == TYPE_STRUCT_PTR) {
      if (last)
        return cc_type_size(cc, TYPE_STRUCT, struct_index);
      type = TYPE_STRUCT;
      continue;
    }
    if (type == TYPE_CHAR_PTR) {
      if (last)
        return 1;
      type = TYPE_CHAR;
      continue;
    }
    if (type == TYPE_INT_PTR || type == TYPE_PTR || type == TYPE_FUNC_PTR) {
      if (last)
        return 4;
      type = TYPE_INT;
      continue;
    }

    /* Non-pointer dereference is invalid (e.g., sizeof(*x) where x is int). */
    if (last)
      return 0;
    return 0;
  }

  return cc_type_size(cc, type, struct_index);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Symbol Table
 * ══════════════════════════════════════════════════════════════════════ */

void cc_sym_init(cc_state_t *cc) { cc->sym_count = 0; }

cc_symbol_t *cc_sym_find(cc_state_t *cc, const char *name) {
  /* Search backwards so locals shadow globals/kernel */
  for (int i = cc->sym_count - 1; i >= 0; i--) {
    if (strcmp(cc->symbols[i].name, name) == 0) {
      return &cc->symbols[i];
    }
  }
  return NULL;
}

cc_symbol_t *cc_sym_add(cc_state_t *cc, const char *name, cc_sym_kind_t kind,
                        cc_type_t type) {
  if (cc->sym_count >= CC_MAX_SYMBOLS) {
    cc_error(cc, "too many symbols");
    return NULL;
  }
  cc_symbol_t *sym = &cc->symbols[cc->sym_count++];
  memset(sym, 0, sizeof(*sym));
  int i = 0;
  while (name[i] && i < CC_MAX_IDENT - 1) {
    sym->name[i] = name[i];
    i++;
  }
  sym->name[i] = '\0';
  sym->kind = kind;
  sym->type = type;
  return sym;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Forward Declarations for Parser
 * ══════════════════════════════════════════════════════════════════════ */

static void cc_parse_statement(cc_state_t *cc);
static void cc_parse_block(cc_state_t *cc);
static void cc_parse_expression(cc_state_t *cc, int min_prec);
static void cc_parse_primary(cc_state_t *cc);

/* ══════════════════════════════════════════════════════════════════════
 *  Expression Types for Tracking
 * ══════════════════════════════════════════════════════════════════════ */

/* Track what kind of value the last expression produced —
 * (primary statics declared above, before cc_parse_type) */

/* ══════════════════════════════════════════════════════════════════════
 *  Operator Precedence
 * ══════════════════════════════════════════════════════════════════════ */

static int cc_precedence(cc_token_type_t op) {
  switch (op) {
  case CC_TOK_OR:
    return 1;
  case CC_TOK_AND:
    return 2;
  case CC_TOK_BOR:
    return 3;
  case CC_TOK_BXOR:
    return 4;
  case CC_TOK_AMP:
    return 5; /* bitwise AND */
  case CC_TOK_EQEQ:
  case CC_TOK_NE:
    return 6;
  case CC_TOK_LT:
  case CC_TOK_GT:
  case CC_TOK_LE:
  case CC_TOK_GE:
    return 7;
  case CC_TOK_SHL:
  case CC_TOK_SHR:
    return 8;
  case CC_TOK_PLUS:
  case CC_TOK_MINUS:
    return 9;
  case CC_TOK_STAR:
  case CC_TOK_SLASH:
  case CC_TOK_PERCENT:
    return 10;
  default:
    return -1;
  }
}

static int cc_is_binary_op(cc_token_type_t t) { return cc_precedence(t) > 0; }

/* ══════════════════════════════════════════════════════════════════════
 *  Expression Parsing
 * ══════════════════════════════════════════════════════════════════════ */

/* Emit binary operation: EBX = left, EAX = right → result in EAX */
static void cc_emit_binop(cc_state_t *cc, cc_token_type_t op) {
  /* Pop left operand into EBX */
  emit_pop_ebx(cc);

  switch (op) {
  case CC_TOK_PLUS:
    emit8(cc, 0x01);
    emit8(cc, 0xD8); /* add eax, ebx */
    break;
  case CC_TOK_MINUS:
    /* ebx - eax: sub ebx, eax then mov eax, ebx */
    emit8(cc, 0x29);
    emit8(cc, 0xC3); /* sub ebx, eax */
    emit8(cc, 0x89);
    emit8(cc, 0xD8); /* mov eax, ebx */
    break;
  case CC_TOK_STAR:
    emit8(cc, 0x0F);
    emit8(cc, 0xAF); /* imul eax, ebx */
    emit8(cc, 0xC3);
    break;
  case CC_TOK_SLASH:
    /* ebx / eax: swap, sign-extend, idiv */
    emit8(cc, 0x89);
    emit8(cc, 0xC1); /* mov ecx, eax */
    emit8(cc, 0x89);
    emit8(cc, 0xD8); /* mov eax, ebx */
    emit8(cc, 0x99); /* cdq (sign-extend eax→edx:eax) */
    emit8(cc, 0xF7);
    emit8(cc, 0xF9); /* idiv ecx */
    break;
  case CC_TOK_PERCENT:
    emit8(cc, 0x89);
    emit8(cc, 0xC1); /* mov ecx, eax */
    emit8(cc, 0x89);
    emit8(cc, 0xD8); /* mov eax, ebx */
    emit8(cc, 0x99); /* cdq */
    emit8(cc, 0xF7);
    emit8(cc, 0xF9); /* idiv ecx */
    emit8(cc, 0x89);
    emit8(cc, 0xD0); /* mov eax, edx (remainder) */
    break;

  /* Comparison operators: cmp ebx, eax; setcc al; movzx eax, al */
  case CC_TOK_EQEQ:
    emit8(cc, 0x39);
    emit8(cc, 0xC3); /* cmp ebx, eax */
    emit8(cc, 0x0F);
    emit8(cc, 0x94);
    emit8(cc, 0xC0); /* sete al */
    emit_movzx_eax_al(cc);
    break;
  case CC_TOK_NE:
    emit8(cc, 0x39);
    emit8(cc, 0xC3);
    emit8(cc, 0x0F);
    emit8(cc, 0x95);
    emit8(cc, 0xC0); /* setne al */
    emit_movzx_eax_al(cc);
    break;
  case CC_TOK_LT:
    emit8(cc, 0x39);
    emit8(cc, 0xC3);
    emit8(cc, 0x0F);
    emit8(cc, 0x9C);
    emit8(cc, 0xC0); /* setl al */
    emit_movzx_eax_al(cc);
    break;
  case CC_TOK_GT:
    emit8(cc, 0x39);
    emit8(cc, 0xC3);
    emit8(cc, 0x0F);
    emit8(cc, 0x9F);
    emit8(cc, 0xC0); /* setg al */
    emit_movzx_eax_al(cc);
    break;
  case CC_TOK_LE:
    emit8(cc, 0x39);
    emit8(cc, 0xC3);
    emit8(cc, 0x0F);
    emit8(cc, 0x9E);
    emit8(cc, 0xC0); /* setle al */
    emit_movzx_eax_al(cc);
    break;
  case CC_TOK_GE:
    emit8(cc, 0x39);
    emit8(cc, 0xC3);
    emit8(cc, 0x0F);
    emit8(cc, 0x9D);
    emit8(cc, 0xC0); /* setge al */
    emit_movzx_eax_al(cc);
    break;

  /* Bitwise */
  case CC_TOK_AMP:
    emit8(cc, 0x21);
    emit8(cc, 0xD8); /* and eax, ebx */
    break;
  case CC_TOK_BOR:
    emit8(cc, 0x09);
    emit8(cc, 0xD8); /* or eax, ebx */
    break;
  case CC_TOK_BXOR:
    emit8(cc, 0x31);
    emit8(cc, 0xD8); /* xor eax, ebx */
    break;
  case CC_TOK_SHL:
    /* ebx << eax: mov ecx, eax; mov eax, ebx; shl eax, cl */
    emit8(cc, 0x89);
    emit8(cc, 0xC1); /* mov ecx, eax */
    emit8(cc, 0x89);
    emit8(cc, 0xD8); /* mov eax, ebx */
    emit8(cc, 0xD3);
    emit8(cc, 0xE0); /* shl eax, cl */
    break;
  case CC_TOK_SHR:
    emit8(cc, 0x89);
    emit8(cc, 0xC1); /* mov ecx, eax */
    emit8(cc, 0x89);
    emit8(cc, 0xD8); /* mov eax, ebx */
    emit8(cc, 0xD3);
    emit8(cc, 0xF8); /* sar eax, cl */
    break;

  /* Logical */
  case CC_TOK_AND:
    /* Both operands already evaluated to 0 or non-0 */
    emit8(cc, 0x85);
    emit8(cc, 0xDB); /* test ebx, ebx */
    emit8(cc, 0x0F);
    emit8(cc, 0x94);
    emit8(cc, 0xC1); /* sete cl */
    emit8(cc, 0x85);
    emit8(cc, 0xC0); /* test eax, eax */
    emit8(cc, 0x0F);
    emit8(cc, 0x94);
    emit8(cc, 0xC0); /* sete al */
    emit8(cc, 0x08);
    emit8(cc, 0xC8); /* or al, cl */
    emit8(cc, 0x0F);
    emit8(cc, 0x94);
    emit8(cc, 0xC0); /* sete al */
    emit_movzx_eax_al(cc);
    break;
  case CC_TOK_OR:
    emit8(cc, 0x09);
    emit8(cc, 0xD8); /* or eax, ebx */
    /* normalize to 0/1 */
    emit8(cc, 0x85);
    emit8(cc, 0xC0); /* test eax, eax */
    emit8(cc, 0x0F);
    emit8(cc, 0x95);
    emit8(cc, 0xC0); /* setne al */
    emit_movzx_eax_al(cc);
    break;

  default:
    cc_error(cc, "unsupported operator");
    break;
  }
}

/* ── Parse variable reference or function call ───────────────────── */
static void cc_parse_ident_expr(cc_state_t *cc) {
  char name[CC_MAX_IDENT];
  int i = 0;
  while (cc->cur.text[i] && i < CC_MAX_IDENT - 1) {
    name[i] = cc->cur.text[i];
    i++;
  }
  name[i] = '\0';

  /* Function call? */
  if (cc_peek(cc).type == CC_TOK_LPAREN) {
    cc_next(cc); /* consume '(' */

    /* Count and push arguments (right to left by collecting first) */
    uint32_t arg_addrs[CC_MAX_PARAMS];
    int argc = 0;

    if (cc_peek(cc).type != CC_TOK_RPAREN) {
      /* Parse first argument */
      cc_parse_expression(cc, 1);
      emit_push_eax(cc);
      argc++;

      while (cc_match(cc, CC_TOK_COMMA)) {
        cc_parse_expression(cc, 1);
        emit_push_eax(cc);
        argc++;
      }
    }
    cc_expect(cc, CC_TOK_RPAREN);

    /* Reverse args on stack for cdecl (we pushed left-to-right,
     * need right-to-left). For simplicity in a single-pass compiler
     * we just accept left-to-right push and adjust parameter access. */
    /* Actually, cdecl pushes right-to-left, but since we parsed
     * left-to-right, let's reverse the top `argc` stack entries */
    if (argc > 1) {
      /* Use a reversal loop:
       * for each pair (i, argc-1-i) where i < argc/2, swap */
      for (int a = 0; a < argc / 2; a++) {
        int b = argc - 1 - a;
        int off_a = a * 4;
        int off_b = b * 4;
        /* mov ecx, [esp+off_a] */
        emit8(cc, 0x8B);
        emit8(cc, 0x8C);
        emit8(cc, 0x24);
        emit32(cc, (uint32_t)off_a);
        /* mov edx, [esp+off_b] */
        emit8(cc, 0x8B);
        emit8(cc, 0x94);
        emit8(cc, 0x24);
        emit32(cc, (uint32_t)off_b);
        /* mov [esp+off_a], edx */
        emit8(cc, 0x89);
        emit8(cc, 0x94);
        emit8(cc, 0x24);
        emit32(cc, (uint32_t)off_a);
        /* mov [esp+off_b], ecx */
        emit8(cc, 0x89);
        emit8(cc, 0x8C);
        emit8(cc, 0x24);
        emit32(cc, (uint32_t)off_b);
      }
    }
    (void)arg_addrs;

    /* Look up function */
    cc_symbol_t *sym = cc_sym_find(cc, name);
    if (sym) {
      if (sym->kind == SYM_KERNEL) {
        emit_call_abs(cc, sym->address);
      } else if (sym->kind == SYM_FUNC) {
        if (sym->is_defined) {
          /* Direct call to known address */
          uint32_t target = cc->code_base + (uint32_t)sym->offset;
          emit_call_abs(cc, target);
        } else {
          /* Forward reference — add patch */
          uint32_t patch_pos = emit_call_rel_placeholder(cc);
          if (cc->patch_count < CC_MAX_PATCHES) {
            cc_patch_t *p = &cc->patches[cc->patch_count++];
            p->code_offset = patch_pos;
            int pi = 0;
            while (name[pi] && pi < CC_MAX_IDENT - 1) {
              p->name[pi] = name[pi];
              pi++;
            }
            p->name[pi] = '\0';
          }
        }
      } else if ((sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM ||
                  sym->kind == SYM_GLOBAL) &&
                 !sym->is_array &&
                 (sym->type == TYPE_FUNC_PTR || sym->type == TYPE_PTR)) {
        /* Call through stored pointer (e.g. void* + cast pattern). */
        if (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM) {
          emit_load_local(cc, sym->offset);
        } else {
          emit8(cc, 0xA1); /* mov eax, [addr] */
          emit32(cc, sym->address);
        }
        emit8(cc, 0xFF);
        emit8(cc, 0xD0); /* call eax */
      } else {
        cc_error(cc, "not a function");
      }
    } else {
      /* Unknown function — create forward ref */
      cc_symbol_t *fsym = cc_sym_add(cc, name, SYM_FUNC, TYPE_INT);
      if (fsym) {
        fsym->param_count = argc;
        fsym->is_defined = 0;
      }
      uint32_t patch_pos = emit_call_rel_placeholder(cc);
      if (cc->patch_count < CC_MAX_PATCHES) {
        cc_patch_t *p = &cc->patches[cc->patch_count++];
        p->code_offset = patch_pos;
        int pi = 0;
        while (name[pi] && pi < CC_MAX_IDENT - 1) {
          p->name[pi] = name[pi];
          pi++;
        }
        p->name[pi] = '\0';
      }
    }

    /* Clean up arguments */
    if (argc > 0) {
      emit_add_esp(cc, (int32_t)(argc * 4));
    }

    cc_last_expr_type = TYPE_INT; /* assume int return */
    return;
  }

  /* Variable reference */
  cc_symbol_t *sym = cc_sym_find(cc, name);
  if (!sym) {
    cc_error(cc, "undefined variable");
    return;
  }

  if (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM) {
    if (sym->is_array || sym->type == TYPE_STRUCT) {
      /* Arrays and structs: load the base address via LEA, not the value */
      emit_lea_local(cc, sym->offset);
    } else {
      emit_load_local(cc, sym->offset);
    }
    cc_last_expr_type = sym->type;
    cc_last_expr_struct_index = sym->struct_index;
    if (sym->is_array && sym->array_elem_size > 0)
      cc_last_expr_elem_size = sym->array_elem_size;
    else if ((sym->type == TYPE_STRUCT_PTR || sym->type == TYPE_STRUCT) &&
             sym->struct_index >= 0 && sym->struct_index < cc->struct_count)
      cc_last_expr_elem_size = cc->structs[sym->struct_index].total_size;
    else if (sym->type == TYPE_CHAR_PTR || sym->type == TYPE_CHAR)
      cc_last_expr_elem_size = 1;
    else
      cc_last_expr_elem_size = 4;
  } else if (sym->kind == SYM_GLOBAL) {
    if (sym->is_array || sym->type == TYPE_STRUCT) {
      /* Arrays/structs: load the base address as immediate */
      emit_mov_eax_imm(cc, sym->address);
    } else {
      /* Scalar: load value from memory */
      emit8(cc, 0xA1); /* mov eax, [addr] */
      emit32(cc, sym->address);
    }
    cc_last_expr_type = sym->type;
    cc_last_expr_struct_index = sym->struct_index;
    if (sym->is_array && sym->array_elem_size > 0)
      cc_last_expr_elem_size = sym->array_elem_size;
    else if ((sym->type == TYPE_STRUCT_PTR || sym->type == TYPE_STRUCT) &&
             sym->struct_index >= 0 && sym->struct_index < cc->struct_count)
      cc_last_expr_elem_size = cc->structs[sym->struct_index].total_size;
    else if (sym->type == TYPE_CHAR_PTR || sym->type == TYPE_CHAR)
      cc_last_expr_elem_size = 1;
    else
      cc_last_expr_elem_size = 4;
  } else if (sym->kind == SYM_FUNC) {
    /* Load function address into eax */
    if (sym->is_defined) {
      emit_mov_eax_imm(cc, cc->code_base + (uint32_t)sym->offset);
    } else {
      emit_mov_eax_imm(cc, sym->address);
    }
    cc_last_expr_type = TYPE_PTR;
  } else if (sym->kind == SYM_KERNEL) {
    emit_mov_eax_imm(cc, sym->address);
    cc_last_expr_type = TYPE_PTR;
  }
}

/* ── Primary expression ──────────────────────────────────────────── */
static void cc_parse_primary(cc_state_t *cc) {
  if (cc->error)
    return;

  cc_token_t tok = cc_next(cc);
  cc_symbol_t *postfix_lvalue_sym = NULL;
  int postfix_lvalue_valid = 0;

  switch (tok.type) {
  case CC_TOK_NUMBER:
    emit_mov_eax_imm(cc, (uint32_t)tok.int_value);
    cc_last_expr_type = TYPE_INT;
    break;

  case CC_TOK_CHAR_LIT:
    emit_mov_eax_imm(cc, (uint32_t)tok.int_value);
    cc_last_expr_type = TYPE_CHAR;
    break;

  case CC_TOK_STRING: {
    /* Store string in data section, load address */
    uint32_t str_addr = cc->data_base + cc->data_pos;
    int si = 0;
    while (tok.text[si] && cc->data_pos < CC_MAX_DATA) {
      cc->data[cc->data_pos++] = (uint8_t)tok.text[si++];
    }
    if (cc->data_pos < CC_MAX_DATA) {
      cc->data[cc->data_pos++] = 0; /* null terminator */
    }
    emit_mov_eax_imm(cc, str_addr);
    cc_last_expr_type = TYPE_CHAR_PTR;
    break;
  }

  case CC_TOK_IDENT:
    if (cc_peek(cc).type != CC_TOK_LPAREN) {
      cc_symbol_t *sym = cc_sym_find(cc, tok.text);
      if (sym && !sym->is_array && sym->type != TYPE_STRUCT &&
          (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM ||
           sym->kind == SYM_GLOBAL)) {
        postfix_lvalue_sym = sym;
        postfix_lvalue_valid = 1;
      }
    }
    cc_parse_ident_expr(cc);
    break;

  case CC_TOK_SIZEOF: {
    /* sizeof(type) or sizeof(*ptr) */
    cc_expect(cc, CC_TOK_LPAREN);
    int32_t size = 0;
    cc_token_t p = cc_peek(cc);

    if (p.type == CC_TOK_STAR) {
      int deref_count = 0;
      while (cc_peek(cc).type == CC_TOK_STAR) {
        cc_next(cc);
        deref_count++;
      }
      cc_token_t id = cc_next(cc);
      if (id.type != CC_TOK_IDENT) {
        cc_error(cc, "sizeof: expected identifier after *");
      } else {
        cc_symbol_t *sym = cc_sym_find(cc, id.text);
        if (!sym) {
          cc_error(cc, "sizeof: undefined symbol");
        } else {
          size = cc_sizeof_symbol_deref(cc, sym, deref_count);
          if (size <= 0)
            cc_error(cc, "sizeof: invalid dereference");
        }
      }
    } else if (cc_is_type_or_typedef(cc, p)) {
      cc_type_t t = cc_parse_type(cc);
      int si = cc_last_type_struct_index;
      size = cc_type_size(cc, t, si);
      if (t == TYPE_STRUCT && !cc_struct_is_complete(cc, si))
        cc_error(cc, "sizeof: incomplete struct");
    } else {
      cc_error(cc, "sizeof: expected type or *ptr");
    }
    cc_expect(cc, CC_TOK_RPAREN);
    if (size < 0)
      size = 0;
    emit_mov_eax_imm(cc, (uint32_t)size);
    cc_last_expr_type = TYPE_INT;
    break;
  }

  case CC_TOK_LPAREN: {
    /* Check for type cast: (int)expr, (char*)expr, (struct Foo*)expr */
    cc_token_t p = cc_peek(cc);
    if (cc_is_type_or_typedef(cc, p)) {
      cc_type_t cast_type = cc_parse_type(cc);
      int cast_si = cc_last_type_struct_index;
      cc_expect(cc, CC_TOK_RPAREN);
      cc_parse_primary(cc);
      cc_last_expr_type = cast_type;
      cc_last_expr_struct_index = cast_si;
    } else {
      cc_parse_expression(cc, 1);
      cc_expect(cc, CC_TOK_RPAREN);
    }
    break;
  }

  case CC_TOK_STAR: {
    /* Dereference: *expr */
    cc_parse_primary(cc);
    cc_type_t ptr_type = cc_last_expr_type;
    if (ptr_type == TYPE_CHAR_PTR) {
      emit_deref_byte(cc);
      cc_last_expr_type = TYPE_CHAR;
    } else {
      emit_deref_dword(cc);
      cc_last_expr_type = TYPE_INT;
    }
    break;
  }

  case CC_TOK_AMP: {
    /* Address-of: &var */
    cc_token_t id = cc_next(cc);
    if (id.type != CC_TOK_IDENT) {
      cc_error(cc, "expected variable after &");
      return;
    }
    cc_symbol_t *sym = cc_sym_find(cc, id.text);
    if (!sym) {
      cc_error(cc, "undefined variable for &");
      return;
    }
    if (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM) {
      emit_lea_local(cc, sym->offset);
    } else if (sym->kind == SYM_GLOBAL) {
      emit_mov_eax_imm(cc, sym->address);
    } else {
      cc_error(cc, "cannot take address of function");
      return;
    }
    if (sym->type == TYPE_INT)
      cc_last_expr_type = TYPE_INT_PTR;
    else if (sym->type == TYPE_CHAR)
      cc_last_expr_type = TYPE_CHAR_PTR;
    else if (sym->type == TYPE_STRUCT || sym->type == TYPE_STRUCT_PTR)
      cc_last_expr_type = TYPE_STRUCT_PTR;
    else
      cc_last_expr_type = TYPE_PTR;
    cc_last_expr_struct_index = sym->struct_index;
    if (sym->is_array && sym->array_elem_size > 0)
      cc_last_expr_elem_size = sym->array_elem_size;
    else if ((sym->type == TYPE_STRUCT || sym->type == TYPE_STRUCT_PTR) &&
             sym->struct_index >= 0 && sym->struct_index < cc->struct_count)
      cc_last_expr_elem_size = cc->structs[sym->struct_index].total_size;
    else if (sym->type == TYPE_CHAR || sym->type == TYPE_CHAR_PTR)
      cc_last_expr_elem_size = 1;
    else
      cc_last_expr_elem_size = 4;
    break;
  }

  case CC_TOK_NOT: {
    /* Logical NOT: !expr */
    cc_parse_primary(cc);
    emit_cmp_eax_zero(cc);
    emit8(cc, 0x0F);
    emit8(cc, 0x94);
    emit8(cc, 0xC0); /* sete al */
    emit_movzx_eax_al(cc);
    cc_last_expr_type = TYPE_INT;
    break;
  }

  case CC_TOK_BNOT: {
    /* Bitwise NOT: ~expr */
    cc_parse_primary(cc);
    emit8(cc, 0xF7);
    emit8(cc, 0xD0); /* not eax */
    cc_last_expr_type = TYPE_INT;
    break;
  }

  case CC_TOK_MINUS: {
    /* Unary minus: -expr */
    cc_parse_primary(cc);
    emit8(cc, 0xF7);
    emit8(cc, 0xD8); /* neg eax */
    cc_last_expr_type = TYPE_INT;
    break;
  }

  case CC_TOK_PLUSPLUS: {
    /* Pre-increment: ++var */
    cc_token_t id = cc_next(cc);
    if (id.type != CC_TOK_IDENT) {
      cc_error(cc, "expected variable after ++");
      return;
    }
    cc_symbol_t *sym = cc_sym_find(cc, id.text);
    if (!sym) {
      cc_error(cc, "undefined variable");
      return;
    }
    if (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM) {
      emit_load_local(cc, sym->offset);
      emit8(cc, 0x40); /* inc eax */
      emit_store_local(cc, sym->offset);
    } else if (sym->kind == SYM_GLOBAL) {
      emit8(cc, 0xA1);
      emit32(cc, sym->address);
      emit8(cc, 0x40); /* inc eax */
      emit8(cc, 0xA3);
      emit32(cc, sym->address);
    }
    cc_last_expr_type = sym->type;
    break;
  }

  case CC_TOK_MINUSMINUS: {
    /* Pre-decrement: --var */
    cc_token_t id = cc_next(cc);
    if (id.type != CC_TOK_IDENT) {
      cc_error(cc, "expected variable after --");
      return;
    }
    cc_symbol_t *sym = cc_sym_find(cc, id.text);
    if (!sym) {
      cc_error(cc, "undefined variable");
      return;
    }
    if (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM) {
      emit_load_local(cc, sym->offset);
      emit8(cc, 0x48); /* dec eax */
      emit_store_local(cc, sym->offset);
    } else if (sym->kind == SYM_GLOBAL) {
      emit8(cc, 0xA1);
      emit32(cc, sym->address);
      emit8(cc, 0x48); /* dec eax */
      emit8(cc, 0xA3);
      emit32(cc, sym->address);
    }
    cc_last_expr_type = sym->type;
    break;
  }

  default:
    cc_error(cc, "expected expression");
    break;
  }

  /* Handle postfix operations: [index], .field, ->field, ++, -- */
  for (;;) {
    if (cc->error)
      return;
    cc_token_t next = cc_peek(cc);

    /* ── Struct member access: expr.field or expr->field ────── */
    if (next.type == CC_TOK_DOT || next.type == CC_TOK_ARROW) {
      postfix_lvalue_valid = 0;
      cc_next(cc); /* consume . or -> */
      cc_token_t field_tok = cc_next(cc);
      if (field_tok.type != CC_TOK_IDENT) {
        cc_error(cc, "expected field name");
        return;
      }
      int si = cc_last_expr_struct_index;
      cc_field_t *field = cc_find_field(cc, si, field_tok.text);
      if (!field) {
        cc_error(cc, "unknown struct field");
        return;
      }
      /* eax = base address of struct; add field offset */
      if (field->offset > 0) {
        emit8(cc, 0x05); /* add eax, imm32 */
        emit32(cc, (uint32_t)field->offset);
      }
      /* Determine result: if field is a sub-struct, keep address */
      if (field->array_count > 0) {
        /* Array field: address is already in eax, treat as pointer */
        if (field->type == TYPE_CHAR)
          cc_last_expr_type = TYPE_CHAR_PTR;
        else
          cc_last_expr_type = TYPE_PTR;
      } else if (field->type == TYPE_STRUCT) {
        cc_last_expr_type = TYPE_STRUCT;
        cc_last_expr_struct_index = field->struct_index;
      } else if (field->type == TYPE_STRUCT_PTR) {
        emit_deref_dword(cc);
        cc_last_expr_type = TYPE_STRUCT_PTR;
        cc_last_expr_struct_index = field->struct_index;
      } else if (field->type == TYPE_CHAR) {
        emit_deref_byte(cc);
        cc_last_expr_type = TYPE_CHAR;
      } else {
        emit_deref_dword(cc);
        cc_last_expr_type = field->type;
      }
      continue;
    }

    if (next.type == CC_TOK_LBRACK) {
      postfix_lvalue_valid = 0;
      /* Array subscript: expr[index] */
      cc_next(cc);
      cc_type_t base_type = cc_last_expr_type;
      int base_elem_size = cc_last_expr_elem_size;
      int base_si = cc_last_expr_struct_index;
      emit_push_eax(cc); /* push base address */

      cc_parse_expression(cc, 1);

      /* Scale index by element size */
      if (base_elem_size <= 1) {
        /* no scaling for byte elements */
      } else if (base_elem_size == 2) {
        emit8(cc, 0xC1);
        emit8(cc, 0xE0);
        emit8(cc, 0x01); /* shl eax, 1 */
      } else if (base_elem_size == 4) {
        emit8(cc, 0xC1);
        emit8(cc, 0xE0);
        emit8(cc, 0x02); /* shl eax, 2 */
      } else {
        /* imul eax, eax, imm32 */
        emit8(cc, 0x69);
        emit8(cc, 0xC0);
        emit32(cc, (uint32_t)base_elem_size);
      }

      emit_pop_ebx(cc); /* pop base into ebx */
      emit8(cc, 0x01);
      emit8(cc, 0xD8); /* add eax, ebx */

      /* Determine result type */
      if (base_type == TYPE_STRUCT_PTR) {
        /* Struct array/pointer subscript: address of element */
        cc_last_expr_type = TYPE_STRUCT;
        cc_last_expr_struct_index = base_si;
        cc_last_expr_elem_size = 4;
      } else if (base_type == TYPE_CHAR_PTR && base_elem_size > 1) {
        /* 2D char array first subscript: pointer to row */
        cc_last_expr_type = TYPE_CHAR_PTR;
        cc_last_expr_elem_size = 1;
      } else if (base_type == TYPE_CHAR_PTR) {
        emit_deref_byte(cc);
        cc_last_expr_type = TYPE_CHAR;
        cc_last_expr_elem_size = 0;
      } else if (base_type == TYPE_INT_PTR && base_elem_size > 4) {
        /* 2D int array first subscript: pointer to row */
        cc_last_expr_type = TYPE_INT_PTR;
        cc_last_expr_elem_size = 4;
      } else {
        emit_deref_dword(cc);
        cc_last_expr_type = TYPE_INT;
        cc_last_expr_elem_size = 0;
      }

      cc_expect(cc, CC_TOK_RBRACK);
      continue;
    }

    if (next.type == CC_TOK_PLUSPLUS) {
      cc_next(cc);
      if (postfix_lvalue_valid && postfix_lvalue_sym) {
        /* Keep old value in EAX for postfix expression result. */
        emit_push_eax(cc);
        if (postfix_lvalue_sym->kind == SYM_LOCAL ||
            postfix_lvalue_sym->kind == SYM_PARAM) {
          emit_load_local(cc, postfix_lvalue_sym->offset);
          emit8(cc, 0x40); /* inc eax */
          emit_store_local(cc, postfix_lvalue_sym->offset);
        } else if (postfix_lvalue_sym->kind == SYM_GLOBAL) {
          emit8(cc, 0xA1);
          emit32(cc, postfix_lvalue_sym->address);
          emit8(cc, 0x40); /* inc eax */
          emit8(cc, 0xA3);
          emit32(cc, postfix_lvalue_sym->address);
        }
        emit_pop_eax(cc);
        cc_last_expr_type = postfix_lvalue_sym->type;
      }
      break;
    }

    if (next.type == CC_TOK_MINUSMINUS) {
      cc_next(cc);
      if (postfix_lvalue_valid && postfix_lvalue_sym) {
        emit_push_eax(cc);
        if (postfix_lvalue_sym->kind == SYM_LOCAL ||
            postfix_lvalue_sym->kind == SYM_PARAM) {
          emit_load_local(cc, postfix_lvalue_sym->offset);
          emit8(cc, 0x48); /* dec eax */
          emit_store_local(cc, postfix_lvalue_sym->offset);
        } else if (postfix_lvalue_sym->kind == SYM_GLOBAL) {
          emit8(cc, 0xA1);
          emit32(cc, postfix_lvalue_sym->address);
          emit8(cc, 0x48); /* dec eax */
          emit8(cc, 0xA3);
          emit32(cc, postfix_lvalue_sym->address);
        }
        emit_pop_eax(cc);
        cc_last_expr_type = postfix_lvalue_sym->type;
      }
      break;
    }

    break;
  }
}

/* ── Expression with precedence climbing ─────────────────────────── */
static void cc_parse_expression(cc_state_t *cc, int min_prec) {
  if (cc->error)
    return;

  cc_parse_primary(cc);

  while (!cc->error) {
    cc_token_t op = cc_peek(cc);
    int prec = cc_precedence(op.type);
    if (prec < min_prec)
      break;
    if (!cc_is_binary_op(op.type))
      break;

    cc_next(cc); /* consume operator */

    emit_push_eax(cc); /* save left operand */
    cc_parse_expression(cc, prec + 1);
    cc_emit_binop(cc, op.type);
  }

  /* ── Ternary operator ?: (lowest precedence, below || which is 1) ── */
  if (!cc->error && min_prec <= 1) {
    cc_token_t maybe_q = cc_peek(cc);
    if (maybe_q.type == CC_TOK_QUESTION) {
      cc_next(cc); /* consume ? */
      /* EAX = condition; test it */
      emit8(cc, 0x85);
      emit8(cc, 0xC0); /* test eax, eax */
      uint32_t jz_off = cc->code_pos;
      emit8(cc, 0x0F);
      emit8(cc, 0x84);
      emit32(cc, 0); /* jz <false> placeholder */

      /* parse true branch */
      cc_parse_expression(cc, 1);
      /* skip false branch */
      uint32_t jmp_off = cc->code_pos;
      emit8(cc, 0xE9);
      emit32(cc, 0); /* jmp <end> placeholder */

      /* patch jz to here (false branch) */
      patch32(cc, jz_off + 2, (uint32_t)(cc->code_pos - (jz_off + 6)));

      /* expect ':' */
      cc_token_t colon = cc_next(cc);
      if (colon.type != CC_TOK_COLON) {
        cc_error(cc, "expected ':' in ternary");
        return;
      }
      /* parse false branch */
      cc_parse_expression(cc, 1);

      /* patch jmp to here (end) */
      patch32(cc, jmp_off + 1, (uint32_t)(cc->code_pos - (jmp_off + 5)));
    }
  }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Assignment Parsing
 * ══════════════════════════════════════════════════════════════════════ */

static int cc_is_assignment_op(cc_token_type_t t) {
  return t == CC_TOK_EQ || t == CC_TOK_PLUSEQ || t == CC_TOK_MINUSEQ ||
         t == CC_TOK_STAREQ || t == CC_TOK_SLASHEQ || t == CC_TOK_ANDEQ ||
         t == CC_TOK_OREQ || t == CC_TOK_XOREQ || t == CC_TOK_SHLEQ ||
         t == CC_TOK_SHREQ;
}

static void cc_emit_compound_from_rhs_old(cc_state_t *cc, cc_token_type_t op) {
  /* Input convention:
   *   eax = RHS value
   *   ebx = current LHS value
   * Output:
   *   eax = combined result
   */
  switch (op) {
  case CC_TOK_PLUSEQ:
    emit8(cc, 0x01);
    emit8(cc, 0xD8); /* add eax, ebx */
    break;
  case CC_TOK_MINUSEQ:
    emit8(cc, 0x29);
    emit8(cc, 0xC3); /* sub ebx, eax */
    emit8(cc, 0x89);
    emit8(cc, 0xD8); /* mov eax, ebx */
    break;
  case CC_TOK_STAREQ:
    emit8(cc, 0x0F);
    emit8(cc, 0xAF);
    emit8(cc, 0xC3); /* imul eax, ebx */
    break;
  case CC_TOK_SLASHEQ:
    emit8(cc, 0x89);
    emit8(cc, 0xC1); /* mov ecx, eax */
    emit8(cc, 0x89);
    emit8(cc, 0xD8); /* mov eax, ebx */
    emit8(cc, 0x99); /* cdq */
    emit8(cc, 0xF7);
    emit8(cc, 0xF9); /* idiv ecx */
    break;
  case CC_TOK_ANDEQ:
    emit8(cc, 0x21);
    emit8(cc, 0xD8); /* and eax, ebx */
    break;
  case CC_TOK_OREQ:
    emit8(cc, 0x09);
    emit8(cc, 0xD8); /* or eax, ebx */
    break;
  case CC_TOK_XOREQ:
    emit8(cc, 0x31);
    emit8(cc, 0xD8); /* xor eax, ebx */
    break;
  case CC_TOK_SHLEQ:
    emit8(cc, 0x89);
    emit8(cc, 0xC1); /* mov ecx, eax */
    emit8(cc, 0x89);
    emit8(cc, 0xD8); /* mov eax, ebx */
    emit8(cc, 0xD3);
    emit8(cc, 0xE0); /* shl eax, cl */
    break;
  case CC_TOK_SHREQ:
    emit8(cc, 0x89);
    emit8(cc, 0xC1); /* mov ecx, eax */
    emit8(cc, 0x89);
    emit8(cc, 0xD8); /* mov eax, ebx */
    emit8(cc, 0xD3);
    emit8(cc, 0xF8); /* sar eax, cl */
    break;
  default:
    break;
  }
}

/* Parse assignment: var = expr, var += expr, *ptr = expr, arr[i] = expr */
static void cc_parse_assignment(cc_state_t *cc, const char *name) {
  cc_symbol_t *sym = cc_sym_find(cc, name);
  if (!sym) {
    cc_error(cc, "undefined variable in assignment");
    return;
  }

  cc_token_t op = cc_next(cc); /* consume =, +=, etc. */

  cc_parse_expression(cc, 1);

  /* Handle compound assignment */
  if (op.type != CC_TOK_EQ) {
    /* Load current value into ebx */
    emit_push_eax(cc); /* save RHS */
    if (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM) {
      emit_load_local(cc, sym->offset);
    } else if (sym->kind == SYM_GLOBAL) {
      emit8(cc, 0xA1);
      emit32(cc, sym->address);
    }
    emit8(cc, 0x89);
    emit8(cc, 0xC3);  /* mov ebx, eax (current val) */
    emit_pop_eax(cc); /* restore RHS */
    cc_emit_compound_from_rhs_old(cc, op.type);
  }

  /* Store result */
  if (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM) {
    emit_store_local(cc, sym->offset);
  } else if (sym->kind == SYM_GLOBAL) {
    emit8(cc, 0xA3);
    emit32(cc, sym->address);
  }
}

/* Parse pointer dereference assignment: *expr = val */
static void cc_parse_deref_assignment(cc_state_t *cc) {
  /* Parse the pointer expression (target address) */
  cc_parse_primary(cc);
  cc_type_t ptr_type = cc_last_expr_type;

  cc_expect(cc, CC_TOK_EQ);

  emit_push_eax(cc); /* save address */
  cc_parse_expression(cc, 1);

  /* EAX = value, stack top = address */
  emit8(cc, 0x89);
  emit8(cc, 0xC3);  /* mov ebx, eax (value) */
  emit_pop_eax(cc); /* eax = address */

  if (ptr_type == TYPE_CHAR_PTR || ptr_type == TYPE_CHAR) {
    emit_store_byte_ptr(cc);
  } else {
    emit_store_dword_ptr(cc);
  }
}

/* Parse array subscript assignment: arr[i]=val, arr[i].f=val, arr[i][j]=val */
static void cc_parse_subscript_assignment(cc_state_t *cc, const char *name) {
  cc_symbol_t *sym = cc_sym_find(cc, name);
  if (!sym) {
    cc_error(cc, "undefined array");
    return;
  }

  /* Parse index */
  cc_parse_expression(cc, 1);

  /* Get element size for scaling */
  int elem_size;
  if (sym->is_array && sym->array_elem_size > 0)
    elem_size = sym->array_elem_size;
  else if (sym->type == TYPE_STRUCT_PTR && sym->struct_index >= 0 &&
           sym->struct_index < cc->struct_count)
    elem_size = cc->structs[sym->struct_index].total_size;
  else if (sym->type == TYPE_CHAR_PTR || sym->type == TYPE_CHAR)
    elem_size = 1;
  else
    elem_size = 4;

  /* Scale index by element size */
  if (elem_size <= 1) {
    /* no scaling */
  } else if (elem_size == 2) {
    emit8(cc, 0xC1);
    emit8(cc, 0xE0);
    emit8(cc, 0x01); /* shl eax, 1 */
  } else if (elem_size == 4) {
    emit8(cc, 0xC1);
    emit8(cc, 0xE0);
    emit8(cc, 0x02); /* shl eax, 2 */
  } else {
    /* imul eax, eax, imm32 */
    emit8(cc, 0x69);
    emit8(cc, 0xC0);
    emit32(cc, (uint32_t)elem_size);
  }

  /* Compute address = base + scaled_index */
  emit_push_eax(cc);

  if (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM) {
    if (sym->is_array) {
      emit_lea_local(cc, sym->offset);
    } else {
      emit_load_local(cc, sym->offset);
    }
  } else if (sym->kind == SYM_GLOBAL) {
    if (sym->is_array) {
      emit_mov_eax_imm(cc, sym->address);
    } else {
      emit8(cc, 0xA1);
      emit32(cc, sym->address);
    }
  }

  emit_pop_ebx(cc);
  emit8(cc, 0x01);
  emit8(cc, 0xD8); /* add eax, ebx */

  cc_expect(cc, CC_TOK_RBRACK);

  /* Determine final store type */
  int is_char = (sym->type == TYPE_CHAR_PTR || sym->type == TYPE_CHAR);

  /* Handle struct array element member chain: arr[i].field = val */
  if (sym->type == TYPE_STRUCT_PTR &&
      (cc_peek(cc).type == CC_TOK_DOT ||
       cc_peek(cc).type == CC_TOK_ARROW)) {
    int si = sym->struct_index;
    cc_type_t ftype = TYPE_INT;
    while (cc_peek(cc).type == CC_TOK_DOT ||
           cc_peek(cc).type == CC_TOK_ARROW) {
      cc_next(cc); /* consume . or -> */
      cc_token_t ftok = cc_next(cc);
      if (ftok.type != CC_TOK_IDENT) {
        cc_error(cc, "expected field");
        return;
      }
      cc_field_t *fld = cc_find_field(cc, si, ftok.text);
      if (!fld) {
        cc_error(cc, "unknown field");
        return;
      }
      if (fld->offset > 0) {
        emit8(cc, 0x05);
        emit32(cc, (uint32_t)fld->offset);
      }
      ftype = fld->type;
      if (fld->type == TYPE_STRUCT) {
        si = fld->struct_index;
      } else if (fld->type == TYPE_STRUCT_PTR) {
        si = fld->struct_index;
        if (cc_peek(cc).type == CC_TOK_DOT ||
            cc_peek(cc).type == CC_TOK_ARROW) {
          /* Continue traversal through pointer target. */
          emit_deref_dword(cc);
        } else {
          /* Leaf pointer field assignment needs the field slot address. */
          break;
        }
      } else if (fld->array_count > 0) {
        ftype = (fld->type == TYPE_CHAR) ? TYPE_CHAR_PTR : TYPE_PTR;
        break;
      } else {
        break;
      }
    }
    is_char = (ftype == TYPE_CHAR);

    /* Handle subscript on struct field: arr[i].field[j] = val */
    if (cc_peek(cc).type == CC_TOK_LBRACK) {
      cc_next(cc); /* consume '[' */
      emit_push_eax(cc);
      cc_parse_expression(cc, 1);
      if (ftype != TYPE_CHAR && ftype != TYPE_CHAR_PTR) {
        emit8(cc, 0xC1);
        emit8(cc, 0xE0);
        emit8(cc, 0x02); /* shl eax, 2 */
      }
      emit_pop_ebx(cc);
      emit8(cc, 0x01);
      emit8(cc, 0xD8); /* add eax, ebx */
      cc_expect(cc, CC_TOK_RBRACK);
      is_char = (ftype == TYPE_CHAR || ftype == TYPE_CHAR_PTR);
    }
  }
  /* Handle 2D char array second subscript: arr[i][j] = val */
  else if (is_char && elem_size > 1 &&
           cc_peek(cc).type == CC_TOK_LBRACK) {
    cc_next(cc); /* consume '[' */
    emit_push_eax(cc);
    cc_parse_expression(cc, 1);
    /* Inner elements are char (1 byte) — no scaling */
    emit_pop_ebx(cc);
    emit8(cc, 0x01);
    emit8(cc, 0xD8); /* add eax, ebx */
    cc_expect(cc, CC_TOK_RBRACK);
    is_char = 1;
  }
  /* Handle 2D int array second subscript */
  else if (!is_char && elem_size > 4 &&
           cc_peek(cc).type == CC_TOK_LBRACK) {
    cc_next(cc); /* consume '[' */
    emit_push_eax(cc);
    cc_parse_expression(cc, 1);
    emit8(cc, 0xC1);
    emit8(cc, 0xE0);
    emit8(cc, 0x02); /* shl eax, 2 */
    emit_pop_ebx(cc);
    emit8(cc, 0x01);
    emit8(cc, 0xD8); /* add eax, ebx */
    cc_expect(cc, CC_TOK_RBRACK);
    is_char = 0;
  }

  emit_push_eax(cc); /* save computed address */

  /* Expect = or compound assignment */
  cc_token_t assign_op = cc_next(cc);
  if (!cc_is_assignment_op(assign_op.type)) {
    cc_error(cc, "expected assignment operator");
    return;
  }

  if (assign_op.type != CC_TOK_EQ) {
    /* Compound assignment: load current value from [address] first */
    /* address is on the stack — peek at it */
    emit8(cc, 0x8B);
    emit8(cc, 0x04);
    emit8(cc, 0x24); /* mov eax, [esp] */
    if (is_char) {
      emit_deref_byte(cc);
    } else {
      emit_deref_dword(cc);
    }
    emit_push_eax(cc); /* push current value */
  }

  cc_parse_expression(cc, 1);

  if (assign_op.type != CC_TOK_EQ) {
    /* Pop old value into ebx, apply operation */
    emit_pop_ebx(cc);
    cc_emit_compound_from_rhs_old(cc, assign_op.type);
  }

  /* EAX = value, stack = address */
  emit8(cc, 0x89);
  emit8(cc, 0xC3);  /* mov ebx, eax */
  emit_pop_eax(cc); /* eax = address */

  if (is_char) {
    emit_store_byte_ptr(cc);
  } else {
    emit_store_dword_ptr(cc);
  }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Inline Assembly Parser
 * ══════════════════════════════════════════════════════════════════════ */

/* Parse a register name, returns register number (0-7) or -1 */
static int cc_parse_reg(const char *text) {
  if (strcmp(text, "eax") == 0)
    return 0;
  if (strcmp(text, "ecx") == 0)
    return 1;
  if (strcmp(text, "edx") == 0)
    return 2;
  if (strcmp(text, "ebx") == 0)
    return 3;
  if (strcmp(text, "esp") == 0)
    return 4;
  if (strcmp(text, "ebp") == 0)
    return 5;
  if (strcmp(text, "esi") == 0)
    return 6;
  if (strcmp(text, "edi") == 0)
    return 7;
  if (strcmp(text, "al") == 0)
    return 0;
  if (strcmp(text, "cl") == 0)
    return 1;
  if (strcmp(text, "dl") == 0)
    return 2;
  if (strcmp(text, "bl") == 0)
    return 3;
  return -1;
}

static void cc_parse_asm_block(cc_state_t *cc) {
  cc_expect(cc, CC_TOK_LBRACE);

  while (!cc->error && cc_peek(cc).type != CC_TOK_RBRACE &&
         cc_peek(cc).type != CC_TOK_EOF) {
    cc_token_t instr = cc_next(cc);
    if (instr.type != CC_TOK_IDENT) {
      cc_error(cc, "expected assembly instruction");
      return;
    }

    /* No-operand instructions */
    if (strcmp(instr.text, "cli") == 0) {
      emit8(cc, 0xFA);
    } else if (strcmp(instr.text, "sti") == 0) {
      emit8(cc, 0xFB);
    } else if (strcmp(instr.text, "hlt") == 0) {
      emit8(cc, 0xF4);
    } else if (strcmp(instr.text, "nop") == 0) {
      emit_nop(cc);
    } else if (strcmp(instr.text, "ret") == 0) {
      emit_ret(cc);
    } else if (strcmp(instr.text, "iret") == 0) {
      emit8(cc, 0xCF);
    } else if (strcmp(instr.text, "pushad") == 0) {
      emit8(cc, 0x60);
    } else if (strcmp(instr.text, "popad") == 0) {
      emit8(cc, 0x61);
    } else if (strcmp(instr.text, "cdq") == 0) {
      emit8(cc, 0x99);

      /* push reg / push imm */
    } else if (strcmp(instr.text, "push") == 0) {
      cc_token_t operand = cc_next(cc);
      int reg = cc_parse_reg(operand.text);
      if (reg >= 0) {
        emit8(cc, (uint8_t)(0x50 + reg));
      } else if (operand.type == CC_TOK_NUMBER) {
        emit_push_imm(cc, (uint32_t)operand.int_value);
      }

      /* pop reg */
    } else if (strcmp(instr.text, "pop") == 0) {
      cc_token_t operand = cc_next(cc);
      int reg = cc_parse_reg(operand.text);
      if (reg >= 0) {
        emit8(cc, (uint8_t)(0x58 + reg));
      }

      /* mov reg, imm / mov reg, reg */
    } else if (strcmp(instr.text, "mov") == 0) {
      cc_token_t dst = cc_next(cc);
      cc_expect(cc, CC_TOK_COMMA);
      cc_token_t src = cc_next(cc);

      int dreg = cc_parse_reg(dst.text);
      int sreg = cc_parse_reg(src.text);

      if (dreg >= 0 && src.type == CC_TOK_NUMBER) {
        emit8(cc, (uint8_t)(0xB8 + dreg));
        emit32(cc, (uint32_t)src.int_value);
      } else if (dreg >= 0 && sreg >= 0) {
        emit8(cc, 0x89);
        emit8(cc, (uint8_t)(0xC0 + sreg * 8 + dreg));
      }

      /* add reg, reg / add reg, imm */
    } else if (strcmp(instr.text, "add") == 0) {
      cc_token_t dst = cc_next(cc);
      cc_expect(cc, CC_TOK_COMMA);
      cc_token_t src = cc_next(cc);
      int dreg = cc_parse_reg(dst.text);
      int sreg = cc_parse_reg(src.text);
      if (dreg >= 0 && sreg >= 0) {
        emit8(cc, 0x01);
        emit8(cc, (uint8_t)(0xC0 + sreg * 8 + dreg));
      } else if (dreg == 0 && src.type == CC_TOK_NUMBER) {
        emit8(cc, 0x05);
        emit32(cc, (uint32_t)src.int_value);
      } else if (dreg >= 0 && src.type == CC_TOK_NUMBER) {
        emit8(cc, 0x81);
        emit8(cc, (uint8_t)(0xC0 + dreg));
        emit32(cc, (uint32_t)src.int_value);
      }

      /* sub reg, reg / sub reg, imm */
    } else if (strcmp(instr.text, "sub") == 0) {
      cc_token_t dst = cc_next(cc);
      cc_expect(cc, CC_TOK_COMMA);
      cc_token_t src = cc_next(cc);
      int dreg = cc_parse_reg(dst.text);
      int sreg = cc_parse_reg(src.text);
      if (dreg >= 0 && sreg >= 0) {
        emit8(cc, 0x29);
        emit8(cc, (uint8_t)(0xC0 + sreg * 8 + dreg));
      } else if (dreg == 0 && src.type == CC_TOK_NUMBER) {
        emit8(cc, 0x2D);
        emit32(cc, (uint32_t)src.int_value);
      } else if (dreg >= 0 && src.type == CC_TOK_NUMBER) {
        emit8(cc, 0x81);
        emit8(cc, (uint8_t)(0xE8 + dreg));
        emit32(cc, (uint32_t)src.int_value);
      }

      /* int imm8 (software interrupt) */
    } else if (strcmp(instr.text, "int") == 0) {
      cc_token_t operand = cc_next(cc);
      emit8(cc, 0xCD);
      emit8(cc, (uint8_t)operand.int_value);

      /* inc reg */
    } else if (strcmp(instr.text, "inc") == 0) {
      cc_token_t operand = cc_next(cc);
      int reg = cc_parse_reg(operand.text);
      if (reg >= 0)
        emit8(cc, (uint8_t)(0x40 + reg));

      /* dec reg */
    } else if (strcmp(instr.text, "dec") == 0) {
      cc_token_t operand = cc_next(cc);
      int reg = cc_parse_reg(operand.text);
      if (reg >= 0)
        emit8(cc, (uint8_t)(0x48 + reg));

      /* xor reg, reg */
    } else if (strcmp(instr.text, "xor") == 0) {
      cc_token_t dst = cc_next(cc);
      cc_expect(cc, CC_TOK_COMMA);
      cc_token_t src = cc_next(cc);
      int dreg = cc_parse_reg(dst.text);
      int sreg = cc_parse_reg(src.text);
      if (dreg >= 0 && sreg >= 0) {
        emit8(cc, 0x31);
        emit8(cc, (uint8_t)(0xC0 + sreg * 8 + dreg));
      }

      /* call reg / call imm */
    } else if (strcmp(instr.text, "call") == 0) {
      cc_token_t operand = cc_next(cc);
      int reg = cc_parse_reg(operand.text);
      if (reg >= 0) {
        emit8(cc, 0xFF);
        emit8(cc, (uint8_t)(0xD0 + reg));
      } else if (operand.type == CC_TOK_NUMBER) {
        emit_call_abs(cc, (uint32_t)operand.int_value);
      }

      /* cmp reg, reg / cmp reg, imm */
    } else if (strcmp(instr.text, "cmp") == 0) {
      cc_token_t dst = cc_next(cc);
      cc_expect(cc, CC_TOK_COMMA);
      cc_token_t src = cc_next(cc);
      int dreg = cc_parse_reg(dst.text);
      int sreg = cc_parse_reg(src.text);
      if (dreg >= 0 && sreg >= 0) {
        emit8(cc, 0x39);
        emit8(cc, (uint8_t)(0xC0 + sreg * 8 + dreg));
      } else if (dreg == 0 && src.type == CC_TOK_NUMBER) {
        emit8(cc, 0x3D);
        emit32(cc, (uint32_t)src.int_value);
      }

      /* out dx, al */
    } else if (strcmp(instr.text, "out") == 0) {
      cc_next(cc); /* dx */
      cc_expect(cc, CC_TOK_COMMA);
      cc_next(cc); /* al */
      emit8(cc, 0xEE);

      /* in al, dx */
    } else if (strcmp(instr.text, "in") == 0) {
      cc_next(cc); /* al */
      cc_expect(cc, CC_TOK_COMMA);
      cc_next(cc); /* dx */
      emit8(cc, 0xEC);

    } else {
      /* Unknown instruction — skip to semicolon */
      cc_error(cc, "unknown assembly instruction");
    }

    /* Consume optional semicolon between asm instructions */
    cc_match(cc, CC_TOK_SEMICOLON);
  }

  cc_expect(cc, CC_TOK_RBRACE);
}

/* ══════════════════════════════════════════════════════════════════════
 *  Statement Parsing
 * ══════════════════════════════════════════════════════════════════════ */

static int cc_skip_brace_initializer(cc_state_t *cc) {
  if (!cc_match(cc, CC_TOK_LBRACE)) {
    cc_error(cc, "expected '{' in initializer");
    return 0;
  }
  int depth = 1;
  while (!cc->error && depth > 0) {
    cc_token_t t = cc_next(cc);
    if (t.type == CC_TOK_LBRACE)
      depth++;
    else if (t.type == CC_TOK_RBRACE)
      depth--;
    else if (t.type == CC_TOK_EOF) {
      cc_error(cc, "unterminated initializer list");
      return 0;
    }
  }
  return !cc->error;
}

/* static local vars are lowered to data-backed globals with local scope. */
static void cc_parse_static_local_declaration(cc_state_t *cc, cc_type_t type) {
  int type_struct_index = cc_last_type_struct_index;
  cc_token_t name_tok = cc_next(cc);
  if (name_tok.type != CC_TOK_IDENT) {
    cc_error(cc, "expected variable name");
    return;
  }

  if (cc_peek(cc).type == CC_TOK_LBRACK) {
    cc_next(cc); /* '[' */
    cc_token_t size_tok = cc_next(cc);
    if (size_tok.type != CC_TOK_NUMBER) {
      cc_error(cc, "expected array size");
      return;
    }
    cc_expect(cc, CC_TOK_RBRACK);
    int32_t arr_elems = size_tok.int_value;
    int32_t inner_dim = 0;
    if (cc_peek(cc).type == CC_TOK_LBRACK) {
      cc_next(cc); /* '[' */
      cc_token_t inner_tok = cc_next(cc);
      if (inner_tok.type != CC_TOK_NUMBER) {
        cc_error(cc, "expected array size");
        return;
      }
      cc_expect(cc, CC_TOK_RBRACK);
      inner_dim = inner_tok.int_value;
    }

    int32_t total_bytes;
    int aes;
    cc_type_t arr_type;
    if (type == TYPE_STRUCT && type_struct_index >= 0 &&
        type_struct_index < cc->struct_count) {
      if (!cc_struct_is_complete(cc, type_struct_index)) {
        cc_error(cc, "array of incomplete struct type");
        return;
      }
      int32_t ssize = cc->structs[type_struct_index].total_size;
      total_bytes = arr_elems * ssize;
      aes = ssize;
      arr_type = TYPE_STRUCT_PTR;
    } else if (inner_dim > 0) {
      int base_elem = (type == TYPE_CHAR) ? 1 : 4;
      int32_t row_size = inner_dim * base_elem;
      total_bytes = arr_elems * row_size;
      aes = row_size;
      arr_type = (type == TYPE_CHAR) ? TYPE_CHAR_PTR : TYPE_INT_PTR;
    } else {
      int elem_size = (type == TYPE_CHAR) ? 1 : 4;
      total_bytes = arr_elems * elem_size;
      aes = elem_size;
      arr_type = (type == TYPE_CHAR) ? TYPE_CHAR_PTR : TYPE_INT_PTR;
    }
    total_bytes = (total_bytes + 3) & ~3;
    cc_symbol_t *sym = cc_sym_add(cc, name_tok.text, SYM_GLOBAL, arr_type);
    if (sym) {
      sym->address = cc->data_base + cc->data_pos;
      sym->is_array = 1;
      sym->struct_index = type_struct_index;
      sym->array_elem_size = aes;
      memset(cc->data + cc->data_pos, 0, (size_t)total_bytes);
      cc->data_pos += (uint32_t)total_bytes;
    }
    if (cc_match(cc, CC_TOK_EQ)) {
      if (!cc_skip_brace_initializer(cc))
        return;
    }
    cc_expect(cc, CC_TOK_SEMICOLON);
    return;
  }

  if (type == TYPE_STRUCT) {
    if (type_struct_index < 0 || type_struct_index >= cc->struct_count) {
      cc_error(cc, "invalid struct type");
      return;
    }
    if (!cc_struct_is_complete(cc, type_struct_index)) {
      cc_error(cc, "incomplete struct type");
      return;
    }
    int32_t ssize = cc->structs[type_struct_index].total_size;
    int32_t alloc_size = cc_align_up(ssize, 4);
    cc_symbol_t *sym = cc_sym_add(cc, name_tok.text, SYM_GLOBAL, TYPE_STRUCT);
    if (sym) {
      sym->address = cc->data_base + cc->data_pos;
      sym->struct_index = type_struct_index;
      memset(cc->data + cc->data_pos, 0, (size_t)alloc_size);
      cc->data_pos += (uint32_t)alloc_size;
    }
    if (cc_match(cc, CC_TOK_EQ)) {
      if (!cc_skip_brace_initializer(cc))
        return;
    }
    cc_expect(cc, CC_TOK_SEMICOLON);
    return;
  }

  cc_symbol_t *sym = cc_sym_add(cc, name_tok.text, SYM_GLOBAL, type);
  if (sym) {
    sym->address = cc->data_base + cc->data_pos;
    sym->struct_index = type_struct_index;
    memset(cc->data + cc->data_pos, 0, 4);
    cc->data_pos += 4;
  }

  if (cc_match(cc, CC_TOK_EQ)) {
    if (cc_peek(cc).type == CC_TOK_LBRACE) {
      if (!cc_skip_brace_initializer(cc))
        return;
    } else {
      cc_parse_expression(cc, 1);
      if (sym) {
        emit8(cc, 0xA3); /* mov [addr], eax */
        emit32(cc, sym->address);
      }
    }
  }

  cc_expect(cc, CC_TOK_SEMICOLON);
}

/* ── Variable declaration ────────────────────────────────────────── */
static void cc_parse_declaration(cc_state_t *cc, cc_type_t type) {
  int type_struct_index = cc_last_type_struct_index;
  cc_token_t name_tok = cc_next(cc);
  if (name_tok.type != CC_TOK_IDENT) {
    cc_error(cc, "expected variable name");
    return;
  }

  /* Check for array declaration: type name[size] or name[M][N] */
  if (cc_peek(cc).type == CC_TOK_LBRACK) {
    cc_next(cc); /* consume '[' */
    cc_token_t size_tok = cc_next(cc);
    if (size_tok.type != CC_TOK_NUMBER) {
      cc_error(cc, "expected array size");
      return;
    }
    cc_expect(cc, CC_TOK_RBRACK);

    int32_t arr_size = size_tok.int_value;
    int32_t inner_dim = 0;
    /* Check for 2D array: type name[M][N] */
    if (cc_peek(cc).type == CC_TOK_LBRACK) {
      cc_next(cc); /* consume '[' */
      cc_token_t inner_tok = cc_next(cc);
      if (inner_tok.type != CC_TOK_NUMBER) {
        cc_error(cc, "expected array size");
        return;
      }
      cc_expect(cc, CC_TOK_RBRACK);
      inner_dim = inner_tok.int_value;
    }

    int32_t total_bytes;
    int aes; /* array_elem_size for subscript scaling */
    cc_type_t arr_type;

    if (type == TYPE_STRUCT && type_struct_index >= 0 &&
        type_struct_index < cc->struct_count) {
      if (!cc_struct_is_complete(cc, type_struct_index)) {
        cc_error(cc, "array of incomplete struct type");
        return;
      }
      /* Array of structs */
      int32_t ssize = cc->structs[type_struct_index].total_size;
      total_bytes = arr_size * ssize;
      aes = ssize;
      arr_type = TYPE_STRUCT_PTR;
    } else if (inner_dim > 0) {
      /* 2D array */
      int base_elem = (type == TYPE_CHAR) ? 1 : 4;
      int32_t row_size = inner_dim * base_elem;
      total_bytes = arr_size * row_size;
      aes = row_size;
      arr_type = (type == TYPE_CHAR) ? TYPE_CHAR_PTR : TYPE_INT_PTR;
    } else {
      /* 1D array */
      int elem_size = (type == TYPE_CHAR) ? 1 : 4;
      total_bytes = arr_size * elem_size;
      aes = elem_size;
      arr_type = (type == TYPE_CHAR) ? TYPE_CHAR_PTR : TYPE_INT_PTR;
    }

    /* Align to 4 bytes */
    total_bytes = (total_bytes + 3) & ~3;

    cc->local_offset -= total_bytes;
    if (cc->local_offset < cc->max_local_offset)
      cc->max_local_offset = cc->local_offset;
    cc_symbol_t *sym = cc_sym_add(cc, name_tok.text, SYM_LOCAL, arr_type);
    if (sym) {
      sym->offset = cc->local_offset;
      sym->is_array = 1;
      sym->struct_index = type_struct_index;
      sym->array_elem_size = aes;
    }

    cc_expect(cc, CC_TOK_SEMICOLON);
    return;
  }

  /* Struct variable: allocate full struct size on stack */
  if (type == TYPE_STRUCT) {
    if (type_struct_index < 0 || type_struct_index >= cc->struct_count) {
      cc_error(cc, "invalid struct type");
      return;
    }
    if (!cc_struct_is_complete(cc, type_struct_index)) {
      cc_error(cc, "incomplete struct type");
      return;
    }
    int32_t ssize = cc->structs[type_struct_index].total_size;
    int32_t alloc_size = cc_align_up(ssize, 4);
    cc->local_offset -= alloc_size;
    if (cc->local_offset < cc->max_local_offset)
      cc->max_local_offset = cc->local_offset;
    cc_symbol_t *sym = cc_sym_add(cc, name_tok.text, SYM_LOCAL, TYPE_STRUCT);
    if (sym) {
      sym->offset = cc->local_offset;
      sym->struct_index = type_struct_index;
    }
    /* Zero-initialize the struct */
    emit_lea_local(cc, cc->local_offset);
    emit_push_eax(cc);
    emit_push_imm(cc, 0);
    emit_push_imm(cc, (uint32_t)alloc_size);
    /* Call memset(addr, 0, size) — push in reverse for cdecl */
    /* Actually we need: memset(ptr, val, size) with ptr first */
    /* Re-order: push size, push 0, push addr */
    emit_add_esp(cc, 12); /* undo the pushes */
    emit_lea_local(cc, cc->local_offset);
    emit_push_imm(cc, (uint32_t)alloc_size);
    emit_push_imm(cc, 0);
    emit_push_eax(cc);
    {
      cc_symbol_t *ms = cc_sym_find(cc, "memset");
      if (ms && ms->kind == SYM_KERNEL) {
        emit_call_abs(cc, ms->address);
      }
    }
    emit_add_esp(cc, 12);
    if (cc_match(cc, CC_TOK_EQ)) {
      /* Compatibility: parse list form, keep memset zero-init semantics. */
      if (!cc_skip_brace_initializer(cc))
        return;
    }
    cc_expect(cc, CC_TOK_SEMICOLON);
    return;
  }

  /* Regular variable */
  cc->local_offset -= 4;
  if (cc->local_offset < cc->max_local_offset)
    cc->max_local_offset = cc->local_offset;
  cc_symbol_t *sym = cc_sym_add(cc, name_tok.text, SYM_LOCAL, type);
  if (sym) {
    sym->offset = cc->local_offset;
    sym->struct_index = type_struct_index;
  }

  /* Check for initializer */
  if (cc_peek(cc).type == CC_TOK_EQ) {
    cc_next(cc); /* consume '=' */
    cc_parse_expression(cc, 1);
    emit_store_local(cc, cc->local_offset);
  } else {
    /* Zero-initialize */
    emit_mov_eax_imm(cc, 0);
    emit_store_local(cc, cc->local_offset);
  }

  cc_expect(cc, CC_TOK_SEMICOLON);
}

/* ── If statement ────────────────────────────────────────────────── */
static void cc_parse_if(cc_state_t *cc) {
  cc_expect(cc, CC_TOK_LPAREN);
  cc_parse_expression(cc, 1);
  cc_expect(cc, CC_TOK_RPAREN);

  /* test eax, eax; je else_label */
  emit_cmp_eax_zero(cc);
  uint32_t else_patch = emit_jcc_placeholder(cc, 0x84); /* je */

  cc_parse_statement(cc);

  if (cc_peek(cc).type == CC_TOK_ELSE) {
    cc_next(cc);
    uint32_t end_patch = emit_jmp_placeholder(cc);
    patch_jump(cc, else_patch);
    cc_parse_statement(cc);
    patch_jump(cc, end_patch);
  } else {
    patch_jump(cc, else_patch);
  }
}

/* ── While loop ──────────────────────────────────────────────────── */
static void cc_parse_while(cc_state_t *cc) {
  uint32_t loop_start = cc->code_pos;

  /* Push loop context */
  int old_depth = cc->loop_depth;
  if (cc->loop_depth < CC_MAX_BREAKS) {
    cc->break_counts[cc->loop_depth] = 0;
    cc->continue_targets[cc->loop_depth] = loop_start;
    cc->loop_depth++;
  }

  cc_expect(cc, CC_TOK_LPAREN);
  cc_parse_expression(cc, 1);
  cc_expect(cc, CC_TOK_RPAREN);

  emit_cmp_eax_zero(cc);
  uint32_t exit_patch = emit_jcc_placeholder(cc, 0x84); /* je */

  cc_parse_statement(cc);

  /* jmp loop_start */
  emit8(cc, 0xE9);
  int32_t rel = (int32_t)(loop_start - (cc->code_pos + 4));
  emit32(cc, (uint32_t)rel);

  patch_jump(cc, exit_patch);

  /* Patch all break targets */
  if (old_depth < CC_MAX_BREAKS && cc->loop_depth > old_depth) {
    for (int i = 0; i < cc->break_counts[old_depth]; i++) {
      patch_jump(cc, cc->break_patches[old_depth][i]);
    }
    cc->break_counts[old_depth] = 0;
  }
  cc->loop_depth = old_depth;
}

/* ── For loop ────────────────────────────────────────────────────── */
static void cc_parse_for(cc_state_t *cc) {
  cc_expect(cc, CC_TOK_LPAREN);

  /* Initializer */
  if (cc_peek(cc).type != CC_TOK_SEMICOLON) {
    if (cc_is_type_or_typedef(cc, cc_peek(cc))) {
      cc_type_t type = cc_parse_type(cc);
      cc_parse_declaration(cc, type);
      /* declaration already consumed semicolon */
    } else {
      /* Expression statement */
      cc_token_t id = cc_next(cc);
      if (id.type == CC_TOK_IDENT && cc_is_assignment_op(cc_peek(cc).type)) {
        cc_parse_assignment(cc, id.text);
      } else {
        /* Put token back and parse as expression */
        cc->has_peek = 1;
        cc->peek_buf = cc->cur;
        cc->cur = id;
        cc_parse_expression(cc, 1);
      }
      cc_expect(cc, CC_TOK_SEMICOLON);
    }
  } else {
    cc_next(cc); /* consume ';' */
  }

  uint32_t cond_start = cc->code_pos;

  /* Push loop context */
  int old_depth = cc->loop_depth;

  /* Condition */
  uint32_t exit_patch = 0;
  if (cc_peek(cc).type != CC_TOK_SEMICOLON) {
    cc_parse_expression(cc, 1);
    emit_cmp_eax_zero(cc);
    exit_patch = emit_jcc_placeholder(cc, 0x84); /* je */
  }
  cc_expect(cc, CC_TOK_SEMICOLON);

  /* Save increment expression position — we'll emit a jmp over it */
  uint32_t inc_jump = emit_jmp_placeholder(cc);
  uint32_t inc_start = cc->code_pos;

  /* Set continue target to increment */
  if (cc->loop_depth < CC_MAX_BREAKS) {
    cc->break_counts[cc->loop_depth] = 0;
    cc->continue_targets[cc->loop_depth] = inc_start;
    cc->loop_depth++;
  }

  /* Increment */
  if (cc_peek(cc).type != CC_TOK_RPAREN) {
    cc_token_t id = cc_next(cc);
    if (id.type == CC_TOK_IDENT && cc_is_assignment_op(cc_peek(cc).type)) {
      cc_parse_assignment(cc, id.text);
    } else if (id.type == CC_TOK_IDENT && cc_peek(cc).type == CC_TOK_PLUSPLUS) {
      cc_next(cc);
      cc_symbol_t *sym = cc_sym_find(cc, id.text);
      if (sym && (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM)) {
        emit_load_local(cc, sym->offset);
        emit8(cc, 0x40); /* inc eax */
        emit_store_local(cc, sym->offset);
      } else if (sym && sym->kind == SYM_GLOBAL) {
        emit8(cc, 0xA1);
        emit32(cc, sym->address);
        emit8(cc, 0x40); /* inc eax */
        emit8(cc, 0xA3);
        emit32(cc, sym->address);
      }
    } else if (id.type == CC_TOK_IDENT &&
               cc_peek(cc).type == CC_TOK_MINUSMINUS) {
      cc_next(cc);
      cc_symbol_t *sym = cc_sym_find(cc, id.text);
      if (sym && (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM)) {
        emit_load_local(cc, sym->offset);
        emit8(cc, 0x48); /* dec eax */
        emit_store_local(cc, sym->offset);
      } else if (sym && sym->kind == SYM_GLOBAL) {
        emit8(cc, 0xA1);
        emit32(cc, sym->address);
        emit8(cc, 0x48); /* dec eax */
        emit8(cc, 0xA3);
        emit32(cc, sym->address);
      }
    } else {
      cc->has_peek = 1;
      cc->peek_buf = cc->cur;
      cc->cur = id;
      cc_parse_expression(cc, 1);
    }
  }
  cc_expect(cc, CC_TOK_RPAREN);

  /* Jump back to condition */
  emit8(cc, 0xE9);
  {
    int32_t rel = (int32_t)(cond_start - (cc->code_pos + 4));
    emit32(cc, (uint32_t)rel);
  }

  /* Patch the jump over increment to body start */
  patch_jump(cc, inc_jump);

  /* Body */
  cc_parse_statement(cc);

  /* After body, jump to increment */
  emit8(cc, 0xE9);
  {
    int32_t rel = (int32_t)(inc_start - (cc->code_pos + 4));
    emit32(cc, (uint32_t)rel);
  }

  /* Patch exit */
  if (exit_patch) {
    patch_jump(cc, exit_patch);
  }

  /* Patch all break targets */
  if (old_depth < CC_MAX_BREAKS && cc->loop_depth > old_depth) {
    for (int i = 0; i < cc->break_counts[old_depth]; i++) {
      patch_jump(cc, cc->break_patches[old_depth][i]);
    }
    cc->break_counts[old_depth] = 0;
  }
  cc->loop_depth = old_depth;
}

/* ── Return statement ────────────────────────────────────────────── */
static void cc_parse_return(cc_state_t *cc) {
  if (cc_peek(cc).type != CC_TOK_SEMICOLON) {
    cc_parse_expression(cc, 1);
  }
  cc_expect(cc, CC_TOK_SEMICOLON);

  /* Emit epilogue (function cleanup + ret) */
  emit_epilogue(cc);
}

/* ── Statement dispatch ──────────────────────────────────────────── */
static void cc_parse_statement(cc_state_t *cc) {
  if (cc->error)
    return;

  cc_token_t tok = cc_peek(cc);

  if (tok.type == CC_TOK_STATIC) {
    cc_next(cc); /* drop storage class in function scope */
    cc_token_t next_tok = cc_peek(cc);
    if (!cc_is_type_or_typedef(cc, next_tok)) {
      cc_error(cc, "expected type after static");
      return;
    }
    cc_type_t type = cc_parse_type(cc);
    cc_parse_static_local_declaration(cc, type);
    return;
  }

  /* Variable declaration (including typedef aliases) */
  if (cc_is_type_or_typedef(cc, tok)) {
    cc_type_t type = cc_parse_type(cc);

    /* Check for function pointer: type (*name)(params) */
    if (cc_peek(cc).type == CC_TOK_LPAREN) {
      cc_next(cc); /* consume '(' */
      if (cc_peek(cc).type == CC_TOK_STAR) {
        cc_next(cc); /* consume '*' */
        cc_token_t fname_tok = cc_next(cc);
        if (fname_tok.type != CC_TOK_IDENT) {
          cc_error(cc, "expected function pointer name");
          return;
        }
        cc_expect(cc, CC_TOK_RPAREN);
        /* Skip parameter list: just consume ( ... ) */
        cc_expect(cc, CC_TOK_LPAREN);
        int depth = 1;
        while (depth > 0 && !cc->error) {
          cc_token_t t = cc_next(cc);
          if (t.type == CC_TOK_LPAREN)
            depth++;
          else if (t.type == CC_TOK_RPAREN)
            depth--;
          else if (t.type == CC_TOK_EOF) {
            cc_error(cc, "unexpected EOF");
            return;
          }
        }
        /* Allocate local variable of type TYPE_FUNC_PTR */
        cc->local_offset -= 4;
        if (cc->local_offset < cc->max_local_offset)
          cc->max_local_offset = cc->local_offset;
        cc_symbol_t *sym =
            cc_sym_add(cc, fname_tok.text, SYM_LOCAL, TYPE_FUNC_PTR);
        if (sym)
          sym->offset = cc->local_offset;
        /* Check for initializer */
        if (cc_peek(cc).type == CC_TOK_EQ) {
          cc_next(cc);
          cc_parse_expression(cc, 1);
          emit_store_local(cc, cc->local_offset);
        } else {
          emit_mov_eax_imm(cc, 0);
          emit_store_local(cc, cc->local_offset);
        }
        cc_expect(cc, CC_TOK_SEMICOLON);
        return;
      } else {
        /* Not a function pointer, put back the '(' - actually we can't easily
           undo, so this is a parse error for now (expressions starting with
           type are rare) */
        cc_error(cc, "unexpected ( after type");
        return;
      }
    }

    cc_parse_declaration(cc, type);
    return;
  }

  switch (tok.type) {
  case CC_TOK_IF:
    cc_next(cc);
    cc_parse_if(cc);
    break;

  case CC_TOK_WHILE:
    cc_next(cc);
    cc_parse_while(cc);
    break;

  case CC_TOK_FOR:
    cc_next(cc);
    cc_parse_for(cc);
    break;

  case CC_TOK_DO: {
    /* do { body } while (cond); */
    cc_next(cc);
    uint32_t loop_start = cc->code_pos;
    int old_depth = cc->loop_depth;
    if (cc->loop_depth < CC_MAX_BREAKS) {
      cc->break_counts[cc->loop_depth] = 0;
      cc->continue_targets[cc->loop_depth] = loop_start;
      cc->loop_depth++;
    }
    cc_parse_statement(cc);
    cc_expect(cc, CC_TOK_WHILE);
    cc_expect(cc, CC_TOK_LPAREN);
    cc_parse_expression(cc, 1);
    cc_expect(cc, CC_TOK_RPAREN);
    cc_expect(cc, CC_TOK_SEMICOLON);
    /* If condition is true (non-zero), jump back to loop_start */
    emit_cmp_eax_zero(cc);
    emit8(cc, 0x0F);
    emit8(cc, 0x85); /* jne rel32 */
    {
      int32_t rel = (int32_t)(loop_start - (cc->code_pos + 4));
      emit32(cc, (uint32_t)rel);
    }
    /* Patch all break targets */
    if (old_depth < CC_MAX_BREAKS && cc->loop_depth > old_depth) {
      for (int i = 0; i < cc->break_counts[old_depth]; i++) {
        patch_jump(cc, cc->break_patches[old_depth][i]);
      }
      cc->break_counts[old_depth] = 0;
    }
    cc->loop_depth = old_depth;
    break;
  }

  case CC_TOK_SWITCH: {
    /* switch (expr) { case N: ... break; default: ... } */
    cc_next(cc);
    cc_expect(cc, CC_TOK_LPAREN);
    cc_parse_expression(cc, 1);
    cc_expect(cc, CC_TOK_RPAREN);
    /* Save switch value on stack */
    emit_push_eax(cc);

    /* Use break mechanism for 'break' inside switch */
    int old_depth = cc->loop_depth;
    if (cc->loop_depth < CC_MAX_BREAKS) {
      cc->break_counts[cc->loop_depth] = 0;
      cc->loop_depth++;
    }

    cc_expect(cc, CC_TOK_LBRACE);

    uint32_t next_case_patch = 0; /* patch for jne to next case */
    int had_default = 0;

    while (!cc->error && cc_peek(cc).type != CC_TOK_RBRACE &&
           cc_peek(cc).type != CC_TOK_EOF) {
      if (cc_peek(cc).type == CC_TOK_CASE) {
        cc_next(cc);
        /* Patch previous case's skip jump to here */
        if (next_case_patch)
          patch_jump(cc, next_case_patch);
        /* Compare switch value with case constant */
        emit8(cc, 0x8B);
        emit8(cc, 0x04);
        emit8(cc, 0x24);
        /* mov eax, [esp] — reload switch val */
        cc_token_t cval = cc_next(cc);
        if (cval.type == CC_TOK_NUMBER || cval.type == CC_TOK_CHAR_LIT) {
          emit8(cc, 0x3D); /* cmp eax, imm32 */
          emit32(cc, (uint32_t)cval.int_value);
        } else {
          cc_error(cc, "case: expected constant");
          break;
        }
        cc_expect(cc, CC_TOK_COLON);
        next_case_patch = emit_jcc_placeholder(cc, 0x85); /* jne */
        /* Parse case body statements */
        while (!cc->error && cc_peek(cc).type != CC_TOK_CASE &&
               cc_peek(cc).type != CC_TOK_DEFAULT &&
               cc_peek(cc).type != CC_TOK_RBRACE &&
               cc_peek(cc).type != CC_TOK_EOF) {
          cc_parse_statement(cc);
        }
      } else if (cc_peek(cc).type == CC_TOK_DEFAULT) {
        cc_next(cc);
        cc_expect(cc, CC_TOK_COLON);
        if (next_case_patch)
          patch_jump(cc, next_case_patch);
        next_case_patch = 0;
        had_default = 1;
        while (!cc->error && cc_peek(cc).type != CC_TOK_CASE &&
               cc_peek(cc).type != CC_TOK_RBRACE &&
               cc_peek(cc).type != CC_TOK_EOF) {
          cc_parse_statement(cc);
        }
      } else {
        cc_error(cc, "expected case or default");
        break;
      }
    }
    cc_expect(cc, CC_TOK_RBRACE);
    /* Patch final case skip if no default */
    if (next_case_patch && !had_default)
      patch_jump(cc, next_case_patch);
    /* Pop switch value */
    emit_add_esp(cc, 4);
    /* Patch all break targets to here */
    if (old_depth < CC_MAX_BREAKS && cc->loop_depth > old_depth) {
      for (int i = 0; i < cc->break_counts[old_depth]; i++) {
        patch_jump(cc, cc->break_patches[old_depth][i]);
      }
      cc->break_counts[old_depth] = 0;
    }
    cc->loop_depth = old_depth;
    break;
  }

  case CC_TOK_RETURN:
    cc_next(cc);
    cc_parse_return(cc);
    break;

  case CC_TOK_BREAK:
    cc_next(cc);
    if (cc->loop_depth <= 0) {
      cc_error(cc, "break outside loop");
    } else {
      uint32_t patch = emit_jmp_placeholder(cc);
      int idx = cc->loop_depth - 1;
      if (cc->break_counts[idx] < CC_MAX_BREAKS_PER_LOOP) {
        cc->break_patches[idx][cc->break_counts[idx]] = patch;
        cc->break_counts[idx]++;
      } else {
        cc_error(cc, "too many break statements in loop");
      }
    }
    cc_expect(cc, CC_TOK_SEMICOLON);
    break;

  case CC_TOK_CONTINUE:
    cc_next(cc);
    if (cc->loop_depth <= 0) {
      cc_error(cc, "continue outside loop");
    } else {
      uint32_t target = cc->continue_targets[cc->loop_depth - 1];
      emit8(cc, 0xE9);
      int32_t rel = (int32_t)(target - (cc->code_pos + 4));
      emit32(cc, (uint32_t)rel);
    }
    cc_expect(cc, CC_TOK_SEMICOLON);
    break;

  case CC_TOK_ASM:
    cc_next(cc);
    cc_parse_asm_block(cc);
    break;

  case CC_TOK_LBRACE:
    cc_next(cc);
    cc_parse_block(cc);
    break;

  case CC_TOK_SEMICOLON:
    cc_next(cc); /* empty statement */
    break;

  case CC_TOK_STAR: {
    /* Dereference assignment: *ptr = val; */
    cc_next(cc);
    cc_parse_deref_assignment(cc);
    cc_expect(cc, CC_TOK_SEMICOLON);
    break;
  }

  case CC_TOK_IDENT: {
    cc_token_t id = cc_next(cc);
    cc_token_t next = cc_peek(cc);

    /* Assignment */
    if (cc_is_assignment_op(next.type)) {
      cc_parse_assignment(cc, id.text);
      cc_expect(cc, CC_TOK_SEMICOLON);
    }
    /* Struct member assignment: var.field = expr or var->field = expr */
    else if (next.type == CC_TOK_DOT || next.type == CC_TOK_ARROW) {
      cc_symbol_t *sym = cc_sym_find(cc, id.text);
      if (!sym) {
        cc_error(cc, "undefined variable");
        break;
      }
      /* Load base address: LEA for local struct, load imm for global */
      if (sym->kind == SYM_GLOBAL) {
        if (sym->type == TYPE_STRUCT) {
          emit_mov_eax_imm(cc, sym->address);
        } else {
          /* Pointer: load value */
          emit8(cc, 0xA1);
          emit32(cc, sym->address);
        }
      } else if (sym->type == TYPE_STRUCT) {
        emit_lea_local(cc, sym->offset);
      } else {
        emit_load_local(cc, sym->offset);
      }
      int si = sym->struct_index;
      /* Traverse member chain: a.b.c or a->b->c */
      cc_type_t ftype = TYPE_INT;
      while (cc_peek(cc).type == CC_TOK_DOT ||
             cc_peek(cc).type == CC_TOK_ARROW) {
        cc_next(cc); /* consume . or -> */
        cc_token_t ftok = cc_next(cc);
        if (ftok.type != CC_TOK_IDENT) {
          cc_error(cc, "expected field");
          break;
        }
        cc_field_t *fld = cc_find_field(cc, si, ftok.text);
        if (!fld) {
          cc_error(cc, "unknown field");
          break;
        }
        if (fld->offset > 0) {
          emit8(cc, 0x05);
          emit32(cc, (uint32_t)fld->offset);
        }
        ftype = fld->type;
        if (fld->type == TYPE_STRUCT) {
          si = fld->struct_index;
        } else if (fld->type == TYPE_STRUCT_PTR) {
          si = fld->struct_index;
          if (cc_peek(cc).type == CC_TOK_DOT ||
              cc_peek(cc).type == CC_TOK_ARROW) {
            /* Continue traversal through pointer target. */
            emit_deref_dword(cc);
          } else {
            /* Leaf pointer field assignment needs the field slot address. */
            break;
          }
        } else if (fld->array_count > 0) {
          /* Array field: keep address, break for subscript or use */
          ftype = (fld->type == TYPE_CHAR) ? TYPE_CHAR_PTR : TYPE_PTR;
          break;
        } else {
          /* Leaf field — next should be = */
          break;
        }
      }
      /* Expect assignment operator */
      cc_token_t assign_op = cc_peek(cc);
      if (!cc_is_assignment_op(assign_op.type)) {
        /* Not an assignment — this is an expression statement */
        /* (e.g., s.func_ptr(args);) — dereference and discard */
        if (ftype == TYPE_CHAR)
          emit_deref_byte(cc);
        else if (ftype != TYPE_STRUCT)
          emit_deref_dword(cc);
        cc_expect(cc, CC_TOK_SEMICOLON);
        break;
      }
      cc_next(cc);       /* consume assignment op */
      emit_push_eax(cc); /* save field address */

      if (assign_op.type != CC_TOK_EQ) {
        /* Load old field value from saved address on stack. */
        emit8(cc, 0x8B);
        emit8(cc, 0x04);
        emit8(cc, 0x24); /* mov eax, [esp] */
        if (ftype == TYPE_CHAR)
          emit_deref_byte(cc);
        else
          emit_deref_dword(cc);
        emit_push_eax(cc);
      }

      cc_parse_expression(cc, 1);

      if (assign_op.type != CC_TOK_EQ) {
        emit_pop_ebx(cc); /* old value */
        cc_emit_compound_from_rhs_old(cc, assign_op.type);
      }

      /* eax = value, stack = field address */
      emit8(cc, 0x89);
      emit8(cc, 0xC3);  /* mov ebx, eax */
      emit_pop_eax(cc); /* eax = address */
      if (ftype == TYPE_CHAR) {
        emit_store_byte_ptr(cc);
      } else {
        emit_store_dword_ptr(cc);
      }
      cc_expect(cc, CC_TOK_SEMICOLON);
    }
    /* Array subscript assignment */
    else if (next.type == CC_TOK_LBRACK) {
      cc_next(cc); /* consume '[' */
      cc_parse_subscript_assignment(cc, id.text);
      cc_expect(cc, CC_TOK_SEMICOLON);
    }
    /* Post-increment */
    else if (next.type == CC_TOK_PLUSPLUS) {
      cc_next(cc);
      cc_symbol_t *sym = cc_sym_find(cc, id.text);
      if (sym && (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM)) {
        emit_load_local(cc, sym->offset);
        emit8(cc, 0x40); /* inc eax */
        emit_store_local(cc, sym->offset);
      } else if (sym && sym->kind == SYM_GLOBAL) {
        emit8(cc, 0xA1);
        emit32(cc, sym->address);
        emit8(cc, 0x40); /* inc eax */
        emit8(cc, 0xA3);
        emit32(cc, sym->address);
      }
      cc_expect(cc, CC_TOK_SEMICOLON);
    }
    /* Post-decrement */
    else if (next.type == CC_TOK_MINUSMINUS) {
      cc_next(cc);
      cc_symbol_t *sym = cc_sym_find(cc, id.text);
      if (sym && (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM)) {
        emit_load_local(cc, sym->offset);
        emit8(cc, 0x48); /* dec eax */
        emit_store_local(cc, sym->offset);
      } else if (sym && sym->kind == SYM_GLOBAL) {
        emit8(cc, 0xA1);
        emit32(cc, sym->address);
        emit8(cc, 0x48); /* dec eax */
        emit8(cc, 0xA3);
        emit32(cc, sym->address);
      }
      cc_expect(cc, CC_TOK_SEMICOLON);
    }
    /* Expression statement (function call, etc.) */
    else {
      /* We already consumed the identifier, so set it back as
       * current and parse as expression */
      cc_parse_ident_expr(cc);
      cc_expect(cc, CC_TOK_SEMICOLON);
    }
    break;
  }

  default:
    cc_parse_expression(cc, 1);
    cc_expect(cc, CC_TOK_SEMICOLON);
    break;
  }
}

/* ── Block (compound statement) ──────────────────────────────────── */
static void cc_parse_block(cc_state_t *cc) {
  int saved_scope = cc->sym_count;
  int saved_offset = cc->local_offset;

  while (!cc->error && cc_peek(cc).type != CC_TOK_RBRACE &&
         cc_peek(cc).type != CC_TOK_EOF) {
    cc_parse_statement(cc);
  }

  cc_expect(cc, CC_TOK_RBRACE);

  /* Restore scope (pop local variables) */
  cc->sym_count = saved_scope;
  cc->local_offset = saved_offset;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Function Parsing
 * ══════════════════════════════════════════════════════════════════════ */

static void cc_parse_function(cc_state_t *cc) {
  cc_type_t ret_type = cc_parse_type(cc);
  if (ret_type == TYPE_STRUCT) {
    cc_error(cc, "struct return unsupported; use pointer-out parameter");
    return;
  }

  cc_token_t name_tok = cc_next(cc);
  if (name_tok.type != CC_TOK_IDENT) {
    cc_error(cc, "expected function name");
    return;
  }

  /* Register function symbol */
  cc_symbol_t *func_sym = cc_sym_find(cc, name_tok.text);
  if (!func_sym) {
    func_sym = cc_sym_add(cc, name_tok.text, SYM_FUNC, ret_type);
  }
  if (func_sym) {
    func_sym->kind = SYM_FUNC;
    func_sym->type = ret_type;
    func_sym->offset = (int32_t)cc->code_pos;
    func_sym->is_defined = 1;
  }

  /* Is this main()? */
  if (strcmp(name_tok.text, "main") == 0) {
    cc->entry_offset = cc->code_pos;
    cc->has_entry = 1;
  }

  cc_expect(cc, CC_TOK_LPAREN);

  /* Save scope state */
  int saved_scope = cc->sym_count;
  cc->local_offset = 0;
  cc->max_local_offset = 0;
  cc->param_count = 0;

  /* Parse parameters */
  if (cc_peek(cc).type != CC_TOK_RPAREN) {
    int param_offset = 8; /* first param at [ebp+8] */

    if (cc_peek(cc).type == CC_TOK_ELLIPSIS) {
      cc_next(cc); /* variadic-only parameter list */
    } else {
      cc_type_t ptype = cc_parse_type(cc);
      int psi = cc_last_type_struct_index;

      /* Special-case: foo(void) */
      if (!(ptype == TYPE_VOID && cc_peek(cc).type == CC_TOK_RPAREN)) {
        cc_token_t pname = cc_next(cc);
        if (pname.type != CC_TOK_IDENT) {
          cc_error(cc, "expected parameter name");
          return;
        }
        cc_symbol_t *psym = cc_sym_add(cc, pname.text, SYM_PARAM, ptype);
        if (psym) {
          psym->offset = param_offset;
          psym->struct_index = psi;
        }
        param_offset += 4;
        cc->param_count++;
      }

      while (cc_match(cc, CC_TOK_COMMA)) {
        if (cc_peek(cc).type == CC_TOK_ELLIPSIS) {
          cc_next(cc); /* consume ... and finish param list */
          break;
        }
        ptype = cc_parse_type(cc);
        psi = cc_last_type_struct_index;
        cc_token_t pname = cc_next(cc);
        if (pname.type != CC_TOK_IDENT) {
          cc_error(cc, "expected parameter name");
          return;
        }
        cc_symbol_t *psym = cc_sym_add(cc, pname.text, SYM_PARAM, ptype);
        if (psym) {
          psym->offset = param_offset;
          psym->struct_index = psi;
        }
        param_offset += 4;
        cc->param_count++;
      }
    }
  }

  cc_expect(cc, CC_TOK_RPAREN);

  if (func_sym) {
    func_sym->param_count = cc->param_count;
  }

  /* Emit function prologue */
  emit_prologue(cc);

  /* Reserve space for locals (we'll patch this after parsing the body) */
  uint32_t sub_esp_pos = cc->code_pos;
  emit_sub_esp(cc, 256); /* placeholder — generous allocation */

  /* Parse body */
  cc_expect(cc, CC_TOK_LBRACE);

  while (!cc->error && cc_peek(cc).type != CC_TOK_RBRACE &&
         cc_peek(cc).type != CC_TOK_EOF) {
    cc_parse_statement(cc);
  }
  cc_expect(cc, CC_TOK_RBRACE);

  /* Patch the sub esp with actual local space used */
  int32_t locals_size = -cc->max_local_offset;
  if (locals_size < 0)
    locals_size = 0;
  /* Round up to 16-byte alignment */
  locals_size = (locals_size + 15) & ~15;
  if (locals_size == 0)
    locals_size = 16; /* minimum */
  /* Patch: sub esp, imm32 at sub_esp_pos+2 */
  patch32(cc, sub_esp_pos + 2, (uint32_t)locals_size);

  /* Emit default epilogue (in case no return statement) */
  emit_mov_eax_imm(cc, 0);
  emit_epilogue(cc);

  /* Restore scope */
  cc->sym_count = saved_scope;
  /* Re-add function symbol (it was part of the saved scope) */
  if (func_sym) {
    cc_symbol_t *new_sym = cc_sym_add(cc, name_tok.text, SYM_FUNC, ret_type);
    if (new_sym) {
      new_sym->offset = func_sym->offset;
      new_sym->address = func_sym->address;
      new_sym->param_count = func_sym->param_count;
      new_sym->is_defined = 1;
    }
  }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Top-Level Program Parsing
 * ══════════════════════════════════════════════════════════════════════ */

void cc_parse_program(cc_state_t *cc) {
  cc->struct_count = 0;

  while (!cc->error && cc_peek(cc).type != CC_TOK_EOF) {
    cc_token_t tok = cc_peek(cc);
    if (tok.type == CC_TOK_STATIC) {
      /* File-scope static is accepted; linkage is not distinguished. */
      cc_next(cc);
      tok = cc_peek(cc);
    }

    /* ── Enum definition: enum { A, B = 5, C }; ─────────────── */
    if (tok.type == CC_TOK_ENUM) {
      cc_next(cc); /* consume 'enum' */
      /* Optional enum name (ignored — we just create constants) */
      if (cc_peek(cc).type == CC_TOK_IDENT) {
        cc_next(cc); /* consume optional name */
      }
      cc_expect(cc, CC_TOK_LBRACE);
      int32_t enum_val = 0;
      while (!cc->error && cc_peek(cc).type != CC_TOK_RBRACE &&
             cc_peek(cc).type != CC_TOK_EOF) {
        cc_token_t name_tok = cc_next(cc);
        if (name_tok.type != CC_TOK_IDENT) {
          cc_error(cc, "expected enum constant name");
          break;
        }
        /* Optional explicit value: NAME = value */
        if (cc_match(cc, CC_TOK_EQ)) {
          cc_token_t val_tok = cc_next(cc);
          int negate = 0;
          if (val_tok.type == CC_TOK_MINUS) {
            negate = 1;
            val_tok = cc_next(cc);
          }
          if (val_tok.type != CC_TOK_NUMBER) {
            cc_error(cc, "expected integer in enum");
            break;
          }
          enum_val = negate ? -val_tok.int_value : val_tok.int_value;
        }
        /* Register as global constant in data section */
        cc_symbol_t *gsym = cc_sym_add(cc, name_tok.text, SYM_GLOBAL, TYPE_INT);
        if (gsym) {
          gsym->address = cc->data_base + cc->data_pos;
          memset(cc->data + cc->data_pos, 0, 4);
          uint32_t v = (uint32_t)enum_val;
          cc->data[cc->data_pos] = (uint8_t)(v & 0xFF);
          cc->data[cc->data_pos + 1] = (uint8_t)((v >> 8) & 0xFF);
          cc->data[cc->data_pos + 2] = (uint8_t)((v >> 16) & 0xFF);
          cc->data[cc->data_pos + 3] = (uint8_t)((v >> 24) & 0xFF);
          cc->data_pos += 4;
        }
        enum_val++;
        /* Comma between values (optional before closing brace) */
        if (cc_peek(cc).type != CC_TOK_RBRACE) {
          cc_expect(cc, CC_TOK_COMMA);
        }
      }
      cc_expect(cc, CC_TOK_RBRACE);
      cc_expect(cc, CC_TOK_SEMICOLON);
      continue;
    }

    /* ── Typedef: typedef <type> <alias>; ────────────────────── */
    if (tok.type == CC_TOK_TYPEDEF) {
      cc_next(cc); /* consume 'typedef' */
      cc_type_t td_type = cc_parse_type(cc);
      cc_token_t alias_tok = cc_next(cc);
      if (alias_tok.type != CC_TOK_IDENT) {
        cc_error(cc, "expected typedef alias name");
        break;
      }
      cc_expect(cc, CC_TOK_SEMICOLON);
      if (cc->typedef_count < 16) {
        int ti = 0;
        while (alias_tok.text[ti] && ti < CC_MAX_IDENT - 1) {
          cc->typedef_names[cc->typedef_count][ti] = alias_tok.text[ti];
          ti++;
        }
        cc->typedef_names[cc->typedef_count][ti] = '\0';
        cc->typedef_types[cc->typedef_count] = td_type;
        cc->typedef_count++;
      }
      continue;
    }

    /* ── Struct definition: struct Name { fields... }; ────────── */
    if (tok.type == CC_TOK_STRUCT) {
      /* Peek further: struct Name { → definition, struct Name var → decl */
      int saved_pos = cc->pos;
      int saved_line = cc->line;
      int saved_has_peek = cc->has_peek;
      cc_token_t saved_peek = cc->peek_buf;
      cc_token_t saved_cur = cc->cur;

      cc_next(cc); /* consume 'struct' */
      cc_token_t sname = cc_next(cc);
      cc_token_t after = cc_peek(cc);
      (void)sname;

      /* Restore lexer state */
      cc->pos = saved_pos;
      cc->line = saved_line;
      cc->has_peek = saved_has_peek;
      cc->peek_buf = saved_peek;
      cc->cur = saved_cur;

      if (after.type == CC_TOK_LBRACE) {
        /* Struct definition */
        cc_next(cc); /* consume 'struct' */
        cc_token_t name_tok = cc_next(cc);
        if (name_tok.type != CC_TOK_IDENT) {
          cc_error(cc, "expected struct name");
          break;
        }
        int sidx = cc_get_or_add_struct_tag(cc, name_tok.text);
        if (sidx < 0) {
          break;
        }
        cc_struct_def_t *sd = &cc->structs[sidx];
        if (sd->is_complete) {
          cc_error(cc, "redefinition of struct");
          break;
        }
        sd->field_count = 0;
        sd->total_size = 0;
        sd->align = 1;
        sd->is_complete = 0;

        cc_expect(cc, CC_TOK_LBRACE);

        int32_t field_offset = 0;
        int32_t struct_align = 1;
        while (!cc->error && cc_peek(cc).type != CC_TOK_RBRACE &&
               cc_peek(cc).type != CC_TOK_EOF) {
          if (sd->field_count >= CC_MAX_FIELDS) {
            cc_error(cc, "too many fields in struct");
            break;
          }
          cc_type_t ftype = cc_parse_type(cc);
          int fsi = cc_last_type_struct_index;
          cc_token_t fname = cc_next(cc);
          if (fname.type != CC_TOK_IDENT) {
            cc_error(cc, "expected field name");
            break;
          }
          cc_field_t *f = &sd->fields[sd->field_count++];
          int fi = 0;
          while (fname.text[fi] && fi < CC_MAX_IDENT - 1) {
            f->name[fi] = fname.text[fi];
            fi++;
          }
          f->name[fi] = '\0';
          f->type = ftype;
          f->struct_index = fsi;
          f->array_count = 0;

          /* Check for array field: name[N] */
          if (cc_peek(cc).type == CC_TOK_LBRACK) {
            cc_next(cc); /* consume '[' */
            cc_token_t size_tok = cc_next(cc);
            if (size_tok.type != CC_TOK_NUMBER) {
              cc_error(cc, "expected array size");
              break;
            }
            f->array_count = size_tok.int_value;
            cc_expect(cc, CC_TOK_RBRACK);
          }

          if (ftype == TYPE_STRUCT && !cc_struct_is_complete(cc, fsi)) {
            cc_error(cc, "field has incomplete struct type");
            break;
          }

          /* Compute field size/alignment with natural padding. */
          int32_t elem_size = cc_type_size(cc, ftype, fsi);
          int32_t field_align = cc_type_align(cc, ftype, fsi);
          int32_t fsize = elem_size;
          if (f->array_count > 0) {
            fsize = elem_size * f->array_count;
          }

          field_offset = cc_align_up(field_offset, field_align);
          f->offset = field_offset;
          field_offset += fsize;
          if (field_align > struct_align)
            struct_align = field_align;

          cc_expect(cc, CC_TOK_SEMICOLON);
        }
        cc_expect(cc, CC_TOK_RBRACE);
        cc_expect(cc, CC_TOK_SEMICOLON);

        sd->align = struct_align;
        sd->total_size = cc_align_up(field_offset, struct_align);
        sd->is_complete = 1;

        serial_printf("[cupidc] Defined struct '%s': %d fields, %d bytes\\n",
                      sd->name, sd->field_count, sd->total_size);
        continue;
      }
      if (after.type == CC_TOK_SEMICOLON) {
        /* Forward tag declaration: struct Name; */
        cc_next(cc); /* consume 'struct' */
        cc_token_t name_tok = cc_next(cc);
        if (name_tok.type != CC_TOK_IDENT) {
          cc_error(cc, "expected struct name");
          break;
        }
        cc_expect(cc, CC_TOK_SEMICOLON);
        if (cc_get_or_add_struct_tag(cc, name_tok.text) < 0)
          break;
        continue;
      }
      /* Otherwise fall through: struct Name used as a type for
       * a function return or global variable — handled below */
    }

    if (cc_is_type_or_typedef(cc, tok)) {
      /* Could be function or global variable */
      /* Look ahead: type name ( → function, type name ; → global */
      /* Save lexer state */
      int saved_pos = cc->pos;
      int saved_line = cc->line;
      int saved_has_peek = cc->has_peek;
      cc_token_t saved_peek = cc->peek_buf;
      cc_token_t saved_cur = cc->cur;

      cc_type_t type = cc_parse_type(cc);
      cc_token_t name_tok = cc_next(cc);
      cc_token_t after = cc_peek(cc);

      /* Restore lexer state */
      cc->pos = saved_pos;
      cc->line = saved_line;
      cc->has_peek = saved_has_peek;
      cc->peek_buf = saved_peek;
      cc->cur = saved_cur;

      if (after.type == CC_TOK_LPAREN) {
        cc_parse_function(cc);
      } else {
        /* Global variable declaration */
        (void)type;
        (void)name_tok;
        cc_type_t gtype = cc_parse_type(cc);
        int gtype_si = cc_last_type_struct_index;
        cc_token_t gname = cc_next(cc);
        if (gname.type != CC_TOK_IDENT) {
          cc_error(cc, "expected variable name");
          break;
        }

        /* Global array: type name[size]; or name[M][N]; */
        if (cc_peek(cc).type == CC_TOK_LBRACK) {
          cc_next(cc); /* consume '[' */
          cc_token_t size_tok = cc_next(cc);
          if (size_tok.type != CC_TOK_NUMBER) {
            cc_error(cc, "expected array size");
            break;
          }
          cc_expect(cc, CC_TOK_RBRACK);
          int32_t arr_elems = size_tok.int_value;
          int32_t inner_dim = 0;
          /* Check for 2D array */
          if (cc_peek(cc).type == CC_TOK_LBRACK) {
            cc_next(cc); /* consume '[' */
            cc_token_t inner_tok = cc_next(cc);
            if (inner_tok.type != CC_TOK_NUMBER) {
              cc_error(cc, "expected array size");
              break;
            }
            cc_expect(cc, CC_TOK_RBRACK);
            inner_dim = inner_tok.int_value;
          }
          int32_t total_bytes;
          int aes;
          cc_type_t arr_type;
          if (gtype == TYPE_STRUCT && gtype_si >= 0 &&
              gtype_si < cc->struct_count) {
            if (!cc_struct_is_complete(cc, gtype_si)) {
              cc_error(cc, "array of incomplete struct type");
              break;
            }
            /* Array of structs */
            int32_t ssize = cc->structs[gtype_si].total_size;
            total_bytes = arr_elems * ssize;
            aes = ssize;
            arr_type = TYPE_STRUCT_PTR;
          } else if (inner_dim > 0) {
            /* 2D array */
            int base_elem = (gtype == TYPE_CHAR) ? 1 : 4;
            int32_t row_size = inner_dim * base_elem;
            total_bytes = arr_elems * row_size;
            aes = row_size;
            arr_type = (gtype == TYPE_CHAR) ? TYPE_CHAR_PTR : TYPE_INT_PTR;
          } else {
            /* 1D array */
            int elem_size = (gtype == TYPE_CHAR) ? 1 : 4;
            total_bytes = arr_elems * elem_size;
            aes = elem_size;
            arr_type = (gtype == TYPE_CHAR) ? TYPE_CHAR_PTR : TYPE_INT_PTR;
          }
          total_bytes = (total_bytes + 3) & ~3;
          cc_symbol_t *gsym = cc_sym_add(cc, gname.text, SYM_GLOBAL, arr_type);
          if (gsym) {
            gsym->address = cc->data_base + cc->data_pos;
            gsym->is_array = 1;
            gsym->struct_index = gtype_si;
            gsym->array_elem_size = aes;
            memset(cc->data + cc->data_pos, 0, (size_t)total_bytes);
            cc->data_pos += (uint32_t)total_bytes;
          }
          cc_expect(cc, CC_TOK_SEMICOLON);
        }
        /* Global struct variable */
        else if (gtype == TYPE_STRUCT && gtype_si >= 0) {
          if (!cc_struct_is_complete(cc, gtype_si)) {
            cc_error(cc, "incomplete struct type");
            break;
          }
          int32_t ssize = cc->structs[gtype_si].total_size;
          int32_t alloc_size = cc_align_up(ssize, 4);
          cc_symbol_t *gsym =
              cc_sym_add(cc, gname.text, SYM_GLOBAL, TYPE_STRUCT);
          if (gsym) {
            gsym->address = cc->data_base + cc->data_pos;
            gsym->struct_index = gtype_si;
            memset(cc->data + cc->data_pos, 0, (size_t)alloc_size);
            cc->data_pos += (uint32_t)alloc_size;
          }
          if (cc_match(cc, CC_TOK_EQ)) {
            if (!cc_skip_brace_initializer(cc))
              break;
          }
          cc_expect(cc, CC_TOK_SEMICOLON);
        }
        /* Scalar global variable */
        else {
          cc_symbol_t *gsym = cc_sym_add(cc, gname.text, SYM_GLOBAL, gtype);
          if (gsym) {
            gsym->address = cc->data_base + cc->data_pos;
            gsym->struct_index = gtype_si;
            memset(cc->data + cc->data_pos, 0, 4);
            cc->data_pos += 4;

            /* Handle initializer: int x = 42; int y = -1; char *s = "hi"; */
            if (cc_match(cc, CC_TOK_EQ)) {
              cc_token_t val = cc_next(cc);
              uint32_t addr_off = gsym->address - cc->data_base;
              /* Handle negative initializer: -NUMBER */
              int negate = 0;
              if (val.type == CC_TOK_MINUS) {
                negate = 1;
                val = cc_next(cc);
              }
              if (val.type == CC_TOK_NUMBER || val.type == CC_TOK_CHAR_LIT) {
                int32_t sv = negate ? -val.int_value : val.int_value;
                uint32_t v = (uint32_t)sv;
                cc->data[addr_off] = (uint8_t)(v & 0xFF);
                cc->data[addr_off + 1] = (uint8_t)((v >> 8) & 0xFF);
                cc->data[addr_off + 2] = (uint8_t)((v >> 16) & 0xFF);
                cc->data[addr_off + 3] = (uint8_t)((v >> 24) & 0xFF);
              } else if (val.type == CC_TOK_STRING) {
                /* Store string in data, save address at variable */
                uint32_t str_addr = cc->data_base + cc->data_pos;
                int si = 0;
                while (val.text[si] && cc->data_pos < CC_MAX_DATA) {
                  cc->data[cc->data_pos++] = (uint8_t)val.text[si++];
                }
                if (cc->data_pos < CC_MAX_DATA)
                  cc->data[cc->data_pos++] = 0;
                /* Align data_pos to 4 */
                cc->data_pos = (cc->data_pos + 3u) & ~3u;
                cc->data[addr_off] = (uint8_t)(str_addr & 0xFF);
                cc->data[addr_off + 1] = (uint8_t)((str_addr >> 8) & 0xFF);
                cc->data[addr_off + 2] = (uint8_t)((str_addr >> 16) & 0xFF);
                cc->data[addr_off + 3] = (uint8_t)((str_addr >> 24) & 0xFF);
              }
            }
          }
          cc_expect(cc, CC_TOK_SEMICOLON);
        }
      }
    } else {
      cc_error(cc, "expected function or global declaration");
      break;
    }
  }

  /* Resolve forward references */
  for (int i = 0; i < cc->patch_count; i++) {
    cc_patch_t *p = &cc->patches[i];
    cc_symbol_t *sym = cc_sym_find(cc, p->name);
    if (sym && sym->kind == SYM_FUNC && sym->is_defined) {
      uint32_t target = cc->code_base + (uint32_t)sym->offset;
      uint32_t from = cc->code_base + p->code_offset + 4;
      int32_t rel = (int32_t)(target - from);
      patch32(cc, p->code_offset, (uint32_t)rel);
    } else if (sym && sym->kind == SYM_KERNEL) {
      uint32_t target = sym->address;
      uint32_t from = cc->code_base + p->code_offset + 4;
      int32_t rel = (int32_t)(target - from);
      patch32(cc, p->code_offset, (uint32_t)rel);
    } else {
      serial_printf("[cupidc] Unresolved symbol: %s\n", p->name);
      /* Build descriptive error with symbol name */
      if (!cc->error) {
        cc->error = 1;
        int ei = 0;
        const char *pre = "CupidC Error: unresolved function '";
        int j = 0;
        while (pre[j] && ei < 100)
          cc->error_msg[ei++] = pre[j++];
        j = 0;
        while (p->name[j] && ei < 120)
          cc->error_msg[ei++] = p->name[j++];
        cc->error_msg[ei++] = '\'';
        cc->error_msg[ei++] = '\n';
        cc->error_msg[ei] = '\0';
      }
    }
  }
}
