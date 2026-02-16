/**
 * as_parse.c — Parser + x86 encoder for the CupidASM assembler
 *
 * Single-pass parser that reads tokens from the lexer and emits
 * x86-32 machine code directly into code/data buffers.  Forward
 * references are resolved in a second pass over the patch table.
 *
 * Encoding is table-driven: each instruction variant maps to opcode
 * bytes, ModRM /digit, and immediate size.
 */

#include "as.h"
#include "string.h"
#include "memory.h"
#include "vfs.h"
#include "shell.h"
#include "../drivers/serial.h"

typedef struct {
  const char *source;
  int pos;
  int line;
  as_token_t cur;
  as_token_t peek_buf;
  int has_peek;
} as_lex_snapshot_t;

/* ══════════════════════════════════════════════════════════════════════
 *  Code / Data Emission Helpers
 * ══════════════════════════════════════════════════════════════════════ */

static void as_error(as_state_t *as, const char *msg) {
  if (as->error) return;
  as->error = 1;
  /* Format: "asm: line N: msg\n" */
  char *d = as->error_msg;
  const char *prefix = "asm: line ";
  while (*prefix) *d++ = *prefix++;
  /* itoa for line */
  int line = as->line;
  char num[12];
  int ni = 0;
  if (line == 0) { num[ni++] = '0'; }
  else {
    int tmp = line;
    while (tmp > 0) { num[ni++] = (char)('0' + (tmp % 10)); tmp /= 10; }
  }
  for (int i = ni - 1; i >= 0; i--) *d++ = num[i];
  *d++ = ':'; *d++ = ' ';
  while (*msg && d < as->error_msg + 120) *d++ = *msg++;
  *d++ = '\n'; *d = '\0';
}

static void emit8(as_state_t *as, uint8_t b) {
  if (as->error) return;
  if (as->code_pos >= AS_MAX_CODE) {
    as_error(as, "code buffer overflow");
    return;
  }
  as->code[as->code_pos++] = b;
}

static void emit16(as_state_t *as, uint16_t v) {
  emit8(as, (uint8_t)(v & 0xFF));
  emit8(as, (uint8_t)((v >> 8) & 0xFF));
}

static void emit32(as_state_t *as, uint32_t v) {
  emit8(as, (uint8_t)(v & 0xFF));
  emit8(as, (uint8_t)((v >> 8) & 0xFF));
  emit8(as, (uint8_t)((v >> 16) & 0xFF));
  emit8(as, (uint8_t)((v >> 24) & 0xFF));
}

static void patch32(as_state_t *as, uint32_t offset, uint32_t value) {
  if (offset + 4 > AS_MAX_CODE) return;
  as->code[offset]     = (uint8_t)(value & 0xFF);
  as->code[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
  as->code[offset + 2] = (uint8_t)((value >> 16) & 0xFF);
  as->code[offset + 3] = (uint8_t)((value >> 24) & 0xFF);
}

static void patch8(as_state_t *as, uint32_t offset, uint8_t value) {
  if (offset >= AS_MAX_CODE) return;
  as->code[offset] = value;
}

static uint32_t as_code_addr(as_state_t *as) {
  return as->code_base + as->code_pos;
}

static uint32_t as_data_addr(as_state_t *as) {
  return as->data_base + as->data_pos;
}

/* Emit a byte to the data section */
static void emit_data8(as_state_t *as, uint8_t b) {
  if (as->error) return;
  if (as->data_pos >= AS_MAX_DATA) {
    as_error(as, "data buffer overflow");
    return;
  }
  as->data[as->data_pos++] = b;
}

static void emit_data16(as_state_t *as, uint16_t v) {
  emit_data8(as, (uint8_t)(v & 0xFF));
  emit_data8(as, (uint8_t)((v >> 8) & 0xFF));
}

static void emit_data32(as_state_t *as, uint32_t v) {
  emit_data8(as, (uint8_t)(v & 0xFF));
  emit_data8(as, (uint8_t)((v >> 8) & 0xFF));
  emit_data8(as, (uint8_t)((v >> 16) & 0xFF));
  emit_data8(as, (uint8_t)((v >> 24) & 0xFF));
}

/* ── ModRM byte builder ──────────────────────────────────────────── */

static uint8_t modrm(int mod, int reg, int rm) {
  return (uint8_t)(((mod & 3) << 6) | ((reg & 7) << 3) | (rm & 7));
}

/* ══════════════════════════════════════════════════════════════════════
 *  Label Table Helpers
 * ══════════════════════════════════════════════════════════════════════ */

/* Case-insensitive comparison */
static int as_label_strcmp(const char *a, const char *b) {
  while (*a && *b) {
    char ca = *a, cb = *b;
    if (ca >= 'A' && ca <= 'Z') ca += 32;
    if (cb >= 'A' && cb <= 'Z') cb += 32;
    if (ca != cb) return 1;
    a++; b++;
  }
  return (*a != *b) ? 1 : 0;
}

static as_label_t *as_find_label(as_state_t *as, const char *name) {
  for (int i = 0; i < as->label_count; i++) {
    if (as_label_strcmp(as->labels[i].name, name) == 0)
      return &as->labels[i];
  }
  return NULL;
}

static as_label_t *as_add_label(as_state_t *as, const char *name,
                                uint32_t addr, int defined, int is_equ) {
  /* Check if it already exists */
  as_label_t *lbl = as_find_label(as, name);
  if (lbl) {
    if (defined) {
      if (lbl->defined && !lbl->is_equ) {
        as_error(as, "duplicate label");
        return NULL;
      }
      lbl->address = addr;
      lbl->defined = 1;
      lbl->is_equ = is_equ;
    }
    return lbl;
  }

  if (as->label_count >= AS_MAX_LABELS) {
    as_error(as, "too many labels");
    return NULL;
  }

  lbl = &as->labels[as->label_count++];
  int i = 0;
  while (name[i] && i < AS_MAX_IDENT - 1) {
    lbl->name[i] = name[i];
    i++;
  }
  lbl->name[i] = '\0';
  lbl->address = addr;
  lbl->defined = defined;
  lbl->is_equ = is_equ;
  return lbl;
}

/* Add a forward-reference patch entry */
static void as_add_patch(as_state_t *as, uint32_t code_offset,
                          const char *name, int rel, int width) {
  if (as->patch_count >= AS_MAX_PATCHES) {
    as_error(as, "too many forward references");
    return;
  }
  as_patch_t *p = &as->patches[as->patch_count++];
  p->code_offset = code_offset;
  p->rel = rel;
  p->width = width;
  int i = 0;
  while (name[i] && i < AS_MAX_IDENT - 1) {
    p->name[i] = name[i];
    i++;
  }
  p->name[i] = '\0';
}

/* ══════════════════════════════════════════════════════════════════════
 *  Token Consumption Helpers
 * ══════════════════════════════════════════════════════════════════════ */

static as_token_t as_advance(as_state_t *as) {
  return as_lex_next(as);
}

static void as_expect_newline_or_eof(as_state_t *as) {
  as_token_t tok = as_lex_peek(as);
  if (tok.type != AS_TOK_NEWLINE && tok.type != AS_TOK_EOF) {
    as_error(as, "expected end of line");
    /* Consume rest of line */
    while (tok.type != AS_TOK_NEWLINE && tok.type != AS_TOK_EOF) {
      as_advance(as);
      tok = as_lex_peek(as);
    }
  }
  if (tok.type == AS_TOK_NEWLINE) as_advance(as);
}

static char *as_read_include_source(const char *raw_path) {
  char path[AS_MAX_STRING];

  if (!raw_path || raw_path[0] == '\0') return NULL;

  if (raw_path[0] == '/') {
    int i = 0;
    while (raw_path[i] && i < AS_MAX_STRING - 1) {
      path[i] = raw_path[i];
      i++;
    }
    path[i] = '\0';
  } else {
    const char *cwd = shell_get_cwd();
    int p = 0;
    int i = 0;
    while (cwd && cwd[i] && p < AS_MAX_STRING - 1) {
      path[p++] = cwd[i++];
    }
    if (p > 0 && path[p - 1] != '/' && p < AS_MAX_STRING - 1) {
      path[p++] = '/';
    }
    i = 0;
    while (raw_path[i] && p < AS_MAX_STRING - 1) {
      path[p++] = raw_path[i++];
    }
    path[p] = '\0';
  }

  int fd = vfs_open(path, O_RDONLY);
  if (fd < 0) return NULL;

  vfs_stat_t st;
  if (vfs_stat(path, &st) < 0 || st.size == 0 || st.size > 256u * 1024u) {
    vfs_close(fd);
    return NULL;
  }

  char *source = kmalloc(st.size + 1);
  if (!source) {
    vfs_close(fd);
    return NULL;
  }

  uint32_t total = 0;
  while (total < st.size) {
    uint32_t chunk = st.size - total;
    if (chunk > 512) chunk = 512;
    int r = vfs_read(fd, source + total, chunk);
    if (r <= 0) break;
    total += (uint32_t)r;
  }
  source[total] = '\0';

  vfs_close(fd);
  return source;
}

/* Skip blank lines / newline tokens */
static void as_skip_newlines(as_state_t *as) {
  while (as_lex_peek(as).type == AS_TOK_NEWLINE) {
    as_advance(as);
  }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Memory Operand Parser
 *
 *  Parses: [reg], [reg+disp], [reg-disp], [reg+reg], [addr]
 *  Returns base register index, displacement, and flags.
 * ══════════════════════════════════════════════════════════════════════ */

typedef struct {
  int has_base;      /* 1 if base register present */
  int base_reg;      /* register index (0-7) */
  int has_index;     /* 1 if index register present (SIB) */
  int index_reg;
  int32_t disp;      /* displacement value */
  int has_disp;      /* 1 if displacement present */
  int disp_is_label; /* 1 if displacement is a label reference */
  char label_name[AS_MAX_IDENT]; /* label name for patching */
} as_mem_operand_t;

static as_mem_operand_t as_parse_mem(as_state_t *as) {
  as_mem_operand_t mem;
  memset(&mem, 0, sizeof(mem));

  /* Already consumed '[' */
  as_token_t tok = as_advance(as);

  if (tok.type == AS_TOK_REGISTER) {
    mem.has_base = 1;
    mem.base_reg = tok.reg_index;

    tok = as_lex_peek(as);

    /* [reg+...] or [reg-...] */
    if (tok.type == AS_TOK_PLUS || tok.type == AS_TOK_MINUS) {
      int sign = (tok.type == AS_TOK_PLUS) ? 1 : -1;
      as_advance(as);
      tok = as_advance(as);

      if (tok.type == AS_TOK_REGISTER) {
        /* [reg+reg] — SIB encoding */
        mem.has_index = 1;
        mem.index_reg = tok.reg_index;
      } else if (tok.type == AS_TOK_NUMBER) {
        mem.has_disp = 1;
        mem.disp = tok.int_value * sign;
      } else if (tok.type == AS_TOK_IDENT) {
        /* [reg+label] — label as displacement */
        mem.has_disp = 1;
        mem.disp_is_label = 1;
        int i = 0;
        while (tok.text[i] && i < AS_MAX_IDENT - 1) {
          mem.label_name[i] = tok.text[i];
          i++;
        }
        mem.label_name[i] = '\0';
      }
    }
  } else if (tok.type == AS_TOK_NUMBER) {
    /* [imm32] — direct memory access */
    mem.has_disp = 1;
    mem.disp = tok.int_value;
  } else if (tok.type == AS_TOK_IDENT) {
    /* [label] — label as address */
    mem.has_disp = 1;
    mem.disp_is_label = 1;
    int i = 0;
    while (tok.text[i] && i < AS_MAX_IDENT - 1) {
      mem.label_name[i] = tok.text[i];
      i++;
    }
    mem.label_name[i] = '\0';
  }

  /* Expect ']' */
  tok = as_lex_peek(as);
  if (tok.type == AS_TOK_RBRACK) {
    as_advance(as);
  } else {
    as_error(as, "expected ']'");
  }

  return mem;
}

/* Emit ModRM + optional SIB + displacement for a memory operand */
static void as_emit_modrm_mem(as_state_t *as, int reg_or_digit,
                               as_mem_operand_t *mem) {
  if (!mem->has_base && mem->has_disp) {
    /* [disp32] — mod=00, rm=5 (direct addressing) */
    emit8(as, modrm(0, reg_or_digit, 5));
    if (mem->disp_is_label) {
      as_label_t *lbl = as_find_label(as, mem->label_name);
      if (lbl && lbl->defined) {
        emit32(as, lbl->address);
      } else {
        as_add_patch(as, as->code_pos, mem->label_name, 0, 4);
        emit32(as, 0);
      }
    } else {
      emit32(as, (uint32_t)mem->disp);
    }
    return;
  }

  if (mem->has_base) {
    int base = mem->base_reg;

    /* ESP (4) requires SIB byte */
    if (base == 4) {
      if (!mem->has_disp || mem->disp == 0) {
        /* [esp] — mod=00, rm=4 (SIB), SIB = 0x24 */
        emit8(as, modrm(0, reg_or_digit, 4));
        emit8(as, 0x24);
      } else if (mem->disp >= -128 && mem->disp <= 127) {
        /* [esp+disp8] */
        emit8(as, modrm(1, reg_or_digit, 4));
        emit8(as, 0x24);
        emit8(as, (uint8_t)(int8_t)mem->disp);
      } else {
        /* [esp+disp32] */
        emit8(as, modrm(2, reg_or_digit, 4));
        emit8(as, 0x24);
        emit32(as, (uint32_t)mem->disp);
      }
      return;
    }

    /* EBP (5) with no displacement encodes as [ebp+disp8(0)] */
    if (base == 5 && (!mem->has_disp || mem->disp == 0)) {
      emit8(as, modrm(1, reg_or_digit, 5));
      emit8(as, 0x00);
      return;
    }

    if (!mem->has_disp || mem->disp == 0) {
      /* [reg] — mod=00 */
      emit8(as, modrm(0, reg_or_digit, base));
    } else if (mem->disp >= -128 && mem->disp <= 127 && !mem->disp_is_label) {
      /* [reg+disp8] — mod=01 */
      emit8(as, modrm(1, reg_or_digit, base));
      emit8(as, (uint8_t)(int8_t)mem->disp);
    } else {
      /* [reg+disp32] — mod=10 */
      emit8(as, modrm(2, reg_or_digit, base));
      if (mem->disp_is_label) {
        as_label_t *lbl = as_find_label(as, mem->label_name);
        if (lbl && lbl->defined) {
          emit32(as, lbl->address);
        } else {
          as_add_patch(as, as->code_pos, mem->label_name, 0, 4);
          emit32(as, 0);
        }
      } else {
        emit32(as, (uint32_t)mem->disp);
      }
    }
  }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Resolve an identifier to its immediate value.
 *  If it's an equ, returns its value.  If it's a defined label,
 *  returns its address.  Otherwise returns 0 and sets *needs_patch.
 * ══════════════════════════════════════════════════════════════════════ */

static uint32_t as_resolve_ident(as_state_t *as, const char *name,
                                  int *needs_patch) {
  *needs_patch = 0;
  as_label_t *lbl = as_find_label(as, name);
  if (lbl && lbl->defined) {
    return lbl->address;
  }
  /* Not yet defined — needs a patch */
  *needs_patch = 1;
  /* Ensure label exists in table for later definition */
  if (!lbl) {
    as_add_label(as, name, 0, 0, 0);
  }
  return 0;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Instruction Encoders
 * ══════════════════════════════════════════════════════════════════════ */

/* ── NOP / RET / HLT / CLI / STI / LEAVE / etc. (no operands) ──── */
static void as_encode_noops(as_state_t *as, const char *mn) {
  if (strcmp(mn, "nop") == 0)    { emit8(as, 0x90); return; }
  if (strcmp(mn, "ret") == 0)    { emit8(as, 0xC3); return; }
  if (strcmp(mn, "hlt") == 0)    { emit8(as, 0xF4); return; }
  if (strcmp(mn, "cli") == 0)    { emit8(as, 0xFA); return; }
  if (strcmp(mn, "sti") == 0)    { emit8(as, 0xFB); return; }
  if (strcmp(mn, "leave") == 0)  { emit8(as, 0xC9); return; }
  if (strcmp(mn, "iret") == 0)   { emit8(as, 0xCF); return; }
  if (strcmp(mn, "pushad") == 0) { emit8(as, 0x60); return; }
  if (strcmp(mn, "popad") == 0)  { emit8(as, 0x61); return; }
  if (strcmp(mn, "pushfd") == 0) { emit8(as, 0x9C); return; }
  if (strcmp(mn, "popfd") == 0)  { emit8(as, 0x9D); return; }
  if (strcmp(mn, "cdq") == 0)    { emit8(as, 0x99); return; }
  if (strcmp(mn, "cbw") == 0)    { emit8(as, 0x66); emit8(as, 0x98); return; }
  if (strcmp(mn, "cwde") == 0)   { emit8(as, 0x98); return; }
  if (strcmp(mn, "movsb") == 0)  { emit8(as, 0xA4); return; }
  if (strcmp(mn, "movsd") == 0)  { emit8(as, 0xA5); return; }
  if (strcmp(mn, "movsw") == 0)  { emit8(as, 0x66); emit8(as, 0xA5); return; }
  if (strcmp(mn, "stosb") == 0)  { emit8(as, 0xAA); return; }
  if (strcmp(mn, "stosd") == 0)  { emit8(as, 0xAB); return; }
  if (strcmp(mn, "stosw") == 0)  { emit8(as, 0x66); emit8(as, 0xAB); return; }
  if (strcmp(mn, "cld") == 0)    { emit8(as, 0xFC); return; }
  if (strcmp(mn, "std") == 0)    { emit8(as, 0xFD); return; }
  if (strcmp(mn, "clc") == 0)    { emit8(as, 0xF8); return; }
  if (strcmp(mn, "stc") == 0)    { emit8(as, 0xF9); return; }
  if (strcmp(mn, "cmc") == 0)    { emit8(as, 0xF5); return; }
  if (strcmp(mn, "int3") == 0)   { emit8(as, 0xCC); return; }
  if (strcmp(mn, "pushf") == 0)  { emit8(as, 0x9C); return; }
  if (strcmp(mn, "popf") == 0)   { emit8(as, 0x9D); return; }
  if (strcmp(mn, "pusha") == 0)  { emit8(as, 0x60); return; }
  if (strcmp(mn, "popa") == 0)   { emit8(as, 0x61); return; }
  as_error(as, "unknown no-operand instruction");
}

/* ── REP prefix ──────────────────────────────────────────────────── */
static void as_encode_rep(as_state_t *as) {
  emit8(as, 0xF3);
  /* Next token should be a string instruction */
  as_token_t tok = as_advance(as);
  if (tok.type == AS_TOK_MNEMONIC) {
    as_encode_noops(as, tok.text);
  } else {
    as_error(as, "expected string instruction after rep");
  }
}

/* ── PUSH ────────────────────────────────────────────────────────── */
static void as_encode_push(as_state_t *as) {
  as_token_t tok = as_lex_peek(as);

  if (tok.type == AS_TOK_REGISTER) {
    as_advance(as);
    if (tok.reg_size == 4) {
      emit8(as, (uint8_t)(0x50 + tok.reg_index)); /* push r32 */
    } else {
      emit8(as, 0x66);
      emit8(as, (uint8_t)(0x50 + tok.reg_index)); /* push r16 */
    }
    return;
  }

  if (tok.type == AS_TOK_NUMBER) {
    as_advance(as);
    int32_t val = tok.int_value;
    if (val >= -128 && val <= 127) {
      emit8(as, 0x6A); /* push imm8 */
      emit8(as, (uint8_t)(int8_t)val);
    } else {
      emit8(as, 0x68); /* push imm32 */
      emit32(as, (uint32_t)val);
    }
    return;
  }

  if (tok.type == AS_TOK_IDENT) {
    as_advance(as);
    int needs_patch = 0;
    uint32_t val = as_resolve_ident(as, tok.text, &needs_patch);
    emit8(as, 0x68); /* push imm32 */
    if (needs_patch) {
      as_add_patch(as, as->code_pos, tok.text, 0, 4);
    }
    emit32(as, val);
    return;
  }

  if (tok.type == AS_TOK_LBRACK) {
    as_advance(as);
    as_mem_operand_t mem = as_parse_mem(as);
    emit8(as, 0xFF); /* push m32 */
    as_emit_modrm_mem(as, 6, &mem); /* /6 */
    return;
  }

  as_error(as, "invalid operand for push");
}

/* ── POP ─────────────────────────────────────────────────────────── */
static void as_encode_pop(as_state_t *as) {
  as_token_t tok = as_advance(as);
  if (tok.type == AS_TOK_REGISTER && tok.reg_size == 4) {
    emit8(as, (uint8_t)(0x58 + tok.reg_index)); /* pop r32 */
    return;
  }
  as_error(as, "invalid operand for pop");
}

/* ── INC / DEC ───────────────────────────────────────────────────── */
static void as_encode_incdec(as_state_t *as, const char *mn) {
  as_token_t tok = as_advance(as);
  if (tok.type == AS_TOK_REGISTER && tok.reg_size == 4) {
    if (strcmp(mn, "inc") == 0)
      emit8(as, (uint8_t)(0x40 + tok.reg_index));
    else
      emit8(as, (uint8_t)(0x48 + tok.reg_index));
    return;
  }
  if (tok.type == AS_TOK_REGISTER && tok.reg_size == 1) {
    if (strcmp(mn, "inc") == 0) {
      emit8(as, 0xFE);
      emit8(as, modrm(3, 0, tok.reg_index));
    } else {
      emit8(as, 0xFE);
      emit8(as, modrm(3, 1, tok.reg_index));
    }
    return;
  }
  as_error(as, "invalid operand for inc/dec");
}

/* ── NOT / NEG ───────────────────────────────────────────────────── */
static void as_encode_not_neg(as_state_t *as, const char *mn) {
  as_token_t tok = as_advance(as);
  if (tok.type == AS_TOK_REGISTER && tok.reg_size == 4) {
    emit8(as, 0xF7);
    int digit = (strcmp(mn, "not") == 0) ? 2 : 3;
    emit8(as, modrm(3, digit, tok.reg_index));
    return;
  }
  as_error(as, "invalid operand for not/neg");
}

/* ── MUL / DIV / IMUL / IDIV (single reg operand, uses eax/edx) ── */
static void as_encode_muldiv(as_state_t *as, const char *mn) {
  as_token_t tok = as_advance(as);
  if (tok.type != AS_TOK_REGISTER || tok.reg_size != 4) {
    as_error(as, "expected 32-bit register");
    return;
  }
  int digit;
  if (strcmp(mn, "mul") == 0)       digit = 4;
  else if (strcmp(mn, "imul") == 0) digit = 5;
  else if (strcmp(mn, "div") == 0)  digit = 6;
  else                              digit = 7; /* idiv */
  emit8(as, 0xF7);
  emit8(as, modrm(3, digit, tok.reg_index));
}

/* ── INT (interrupt) ─────────────────────────────────────────────── */
static void as_encode_int(as_state_t *as) {
  as_token_t tok = as_advance(as);
  if (tok.type != AS_TOK_NUMBER) {
    as_error(as, "expected interrupt number");
    return;
  }
  emit8(as, 0xCD);
  emit8(as, (uint8_t)tok.int_value);
}

/* ── CALL ────────────────────────────────────────────────────────── */
static void as_encode_call(as_state_t *as) {
  as_token_t tok = as_lex_peek(as);

  if (tok.type == AS_TOK_IDENT || tok.type == AS_TOK_MNEMONIC) {
    as_advance(as);
    emit8(as, 0xE8); /* call rel32 */
    as_label_t *lbl = as_find_label(as, tok.text);
    if (lbl && lbl->defined) {
      uint32_t target = lbl->address;
      uint32_t next_ip = as_code_addr(as) + 4;
      emit32(as, target - next_ip);
    } else {
      as_add_patch(as, as->code_pos, tok.text, 1, 4);
      emit32(as, 0);
      if (!lbl) as_add_label(as, tok.text, 0, 0, 0);
    }
    return;
  }

  if (tok.type == AS_TOK_REGISTER) {
    as_advance(as);
    emit8(as, 0xFF);
    emit8(as, modrm(3, 2, tok.reg_index)); /* call r32 */
    return;
  }

  if (tok.type == AS_TOK_LBRACK) {
    as_advance(as);
    as_mem_operand_t mem = as_parse_mem(as);
    emit8(as, 0xFF);
    as_emit_modrm_mem(as, 2, &mem); /* call m32 */
    return;
  }

  as_error(as, "invalid operand for call");
}

/* ── JMP and Jcc (conditional jumps) ─────────────────────────────── */
static void as_encode_jmp(as_state_t *as, const char *mn) {
  as_token_t tok = as_lex_peek(as);

  /* Determine opcode(s) */
  int is_short = 0;  /* "jmp short" */
  int jcc_code = -1; /* for conditional jumps */

  if (strcmp(mn, "jmp") == 0) {
    /* Check for "short" keyword */
    if (tok.type == AS_TOK_IDENT && strcmp(tok.text, "short") == 0) {
      as_advance(as);
      is_short = 1;
      tok = as_lex_peek(as);
    }
  }

  /* Conditional jumps — all use near (rel32) by default */
  if (strcmp(mn, "je") == 0 || strcmp(mn, "jz") == 0)    jcc_code = 0x84;
  if (strcmp(mn, "jne") == 0 || strcmp(mn, "jnz") == 0)  jcc_code = 0x85;
  if (strcmp(mn, "jc") == 0 || strcmp(mn, "jnae") == 0)  jcc_code = 0x82;
  if (strcmp(mn, "jnc") == 0 || strcmp(mn, "jnb") == 0)  jcc_code = 0x83;
  if (strcmp(mn, "jna") == 0)  jcc_code = 0x86;
  if (strcmp(mn, "jnbe") == 0) jcc_code = 0x87;
  if (strcmp(mn, "jl") == 0)   jcc_code = 0x8C;
  if (strcmp(mn, "jg") == 0)   jcc_code = 0x8F;
  if (strcmp(mn, "jle") == 0)  jcc_code = 0x8E;
  if (strcmp(mn, "jge") == 0)  jcc_code = 0x8D;
  if (strcmp(mn, "jnge") == 0) jcc_code = 0x8C;
  if (strcmp(mn, "jnl") == 0)  jcc_code = 0x8D;
  if (strcmp(mn, "jng") == 0)  jcc_code = 0x8E;
  if (strcmp(mn, "jnle") == 0) jcc_code = 0x8F;
  if (strcmp(mn, "jb") == 0)   jcc_code = 0x82;
  if (strcmp(mn, "jbe") == 0)  jcc_code = 0x86;
  if (strcmp(mn, "ja") == 0)   jcc_code = 0x87;
  if (strcmp(mn, "jae") == 0)  jcc_code = 0x83;
  if (strcmp(mn, "js") == 0)   jcc_code = 0x88;
  if (strcmp(mn, "jns") == 0)  jcc_code = 0x89;
  if (strcmp(mn, "jp") == 0 || strcmp(mn, "jpe") == 0) jcc_code = 0x8A;
  if (strcmp(mn, "jnp") == 0 || strcmp(mn, "jpo") == 0) jcc_code = 0x8B;
  if (strcmp(mn, "jo") == 0)   jcc_code = 0x80;
  if (strcmp(mn, "jno") == 0)  jcc_code = 0x81;

  /* Target: label/ident or register */
  if (tok.type == AS_TOK_IDENT || tok.type == AS_TOK_MNEMONIC) {
    as_advance(as);

    if (strcmp(mn, "jmp") == 0 && is_short) {
      /* jmp short label → EB rel8 */
      emit8(as, 0xEB);
      as_label_t *lbl = as_find_label(as, tok.text);
      if (lbl && lbl->defined) {
        int32_t rel = (int32_t)(lbl->address - (as_code_addr(as) + 1));
        emit8(as, (uint8_t)(int8_t)rel);
      } else {
        as_add_patch(as, as->code_pos, tok.text, 1, 1);
        emit8(as, 0);
        if (!lbl) as_add_label(as, tok.text, 0, 0, 0);
      }
      return;
    }

    if (strcmp(mn, "jmp") == 0) {
      /* jmp label → E9 rel32 */
      emit8(as, 0xE9);
    } else if (jcc_code >= 0) {
      /* Jcc label → 0F 8x rel32 */
      emit8(as, 0x0F);
      emit8(as, (uint8_t)jcc_code);
    } else {
      as_error(as, "unknown jump mnemonic");
      return;
    }

    as_label_t *lbl = as_find_label(as, tok.text);
    if (lbl && lbl->defined) {
      uint32_t target = lbl->address;
      uint32_t next_ip = as_code_addr(as) + 4;
      emit32(as, target - next_ip);
    } else {
      as_add_patch(as, as->code_pos, tok.text, 1, 4);
      emit32(as, 0);
      if (!lbl) as_add_label(as, tok.text, 0, 0, 0);
    }
    return;
  }

  /* jmp reg */
  if (tok.type == AS_TOK_REGISTER) {
    as_advance(as);
    emit8(as, 0xFF);
    emit8(as, modrm(3, 4, tok.reg_index)); /* jmp r32 */
    return;
  }

  /* jmp [mem] */
  if (tok.type == AS_TOK_LBRACK) {
    as_advance(as);
    as_mem_operand_t mem = as_parse_mem(as);
    emit8(as, 0xFF);
    as_emit_modrm_mem(as, 4, &mem); /* jmp m32 */
    return;
  }

  as_error(as, "invalid operand for jump");
}

/* Helper for getting the second operand (after comma) as imm/ident */
static int32_t __attribute__((unused))
as_parse_imm_operand(as_state_t *as, int *needs_patch,
                     char *patch_name) {
  *needs_patch = 0;
  as_token_t tok = as_advance(as);

  if (tok.type == AS_TOK_NUMBER) {
    return tok.int_value;
  }

  if (tok.type == AS_TOK_IDENT) {
    uint32_t val = as_resolve_ident(as, tok.text, needs_patch);
    if (*needs_patch && patch_name) {
      int i = 0;
      while (tok.text[i] && i < AS_MAX_IDENT - 1) {
        patch_name[i] = tok.text[i];
        i++;
      }
      patch_name[i] = '\0';
    }
    return (int32_t)val;
  }

  as_error(as, "expected immediate or identifier");
  return 0;
}

/* ── MOV ─────────────────────────────────────────────────────────── */
static void as_encode_mov(as_state_t *as) {
  as_token_t dst = as_advance(as);

  /* mov reg, ... */
  if (dst.type == AS_TOK_REGISTER) {
    as_token_t comma = as_advance(as);
    if (comma.type != AS_TOK_COMMA) {
      as_error(as, "expected comma after register");
      return;
    }

    as_token_t src = as_lex_peek(as);

    /* mov reg, reg */
    if (src.type == AS_TOK_REGISTER) {
      as_advance(as);
      if (dst.reg_size == 4 && src.reg_size == 4) {
        emit8(as, 0x89);
        emit8(as, modrm(3, src.reg_index, dst.reg_index));
      } else if (dst.reg_size == 1 && src.reg_size == 1) {
        emit8(as, 0x88);
        emit8(as, modrm(3, src.reg_index, dst.reg_index));
      } else if (dst.reg_size == 2 && src.reg_size == 2) {
        emit8(as, 0x66); /* operand size prefix */
        emit8(as, 0x89);
        emit8(as, modrm(3, src.reg_index, dst.reg_index));
      } else {
        as_error(as, "register size mismatch in mov");
      }
      return;
    }

    /* mov reg, imm */
    if (src.type == AS_TOK_NUMBER) {
      as_advance(as);
      if (dst.reg_size == 4) {
        emit8(as, (uint8_t)(0xB8 + dst.reg_index)); /* mov r32, imm32 */
        emit32(as, (uint32_t)src.int_value);
      } else if (dst.reg_size == 1) {
        emit8(as, (uint8_t)(0xB0 + dst.reg_index)); /* mov r8, imm8 */
        emit8(as, (uint8_t)src.int_value);
      } else {
        emit8(as, 0x66);
        emit8(as, (uint8_t)(0xB8 + dst.reg_index)); /* mov r16, imm16 */
        emit16(as, (uint16_t)src.int_value);
      }
      return;
    }

    /* mov reg, ident (label/equ address) */
    if (src.type == AS_TOK_IDENT) {
      as_advance(as);
      int needs_patch = 0;
      uint32_t val = as_resolve_ident(as, src.text, &needs_patch);
      if (dst.reg_size == 4) {
        emit8(as, (uint8_t)(0xB8 + dst.reg_index)); /* mov r32, imm32 */
        if (needs_patch) {
          as_add_patch(as, as->code_pos, src.text, 0, 4);
        }
        emit32(as, val);
      } else {
        as_error(as, "label/equ requires 32-bit register");
      }
      return;
    }

    /* mov reg, [mem] */
    if (src.type == AS_TOK_LBRACK) {
      as_advance(as);
      as_mem_operand_t mem = as_parse_mem(as);
      if (dst.reg_size == 4) {
        emit8(as, 0x8B); /* mov r32, m32 */
      } else if (dst.reg_size == 1) {
        emit8(as, 0x8A); /* mov r8, m8 */
      } else {
        emit8(as, 0x66);
        emit8(as, 0x8B); /* mov r16, m16 */
      }
      as_emit_modrm_mem(as, dst.reg_index, &mem);
      return;
    }

    as_error(as, "invalid source operand for mov");
    return;
  }

  /* mov [mem], ... */
  if (dst.type == AS_TOK_LBRACK) {
    as_mem_operand_t mem = as_parse_mem(as);

    as_token_t comma = as_advance(as);
    if (comma.type != AS_TOK_COMMA) {
      as_error(as, "expected comma");
      return;
    }

    as_token_t src = as_lex_peek(as);

    /* mov [mem], reg */
    if (src.type == AS_TOK_REGISTER) {
      as_advance(as);
      if (src.reg_size == 4) {
        emit8(as, 0x89); /* mov m32, r32 */
      } else if (src.reg_size == 1) {
        emit8(as, 0x88); /* mov m8, r8 */
      } else {
        emit8(as, 0x66);
        emit8(as, 0x89); /* mov m16, r16 */
      }
      as_emit_modrm_mem(as, src.reg_index, &mem);
      return;
    }

    /* mov [mem], imm */
    if (src.type == AS_TOK_NUMBER) {
      as_advance(as);
      emit8(as, 0xC7); /* mov m32, imm32 */
      as_emit_modrm_mem(as, 0, &mem);
      emit32(as, (uint32_t)src.int_value);
      return;
    }

    /* mov [mem], ident */
    if (src.type == AS_TOK_IDENT) {
      as_advance(as);
      int needs_patch = 0;
      uint32_t val = as_resolve_ident(as, src.text, &needs_patch);
      emit8(as, 0xC7);
      as_emit_modrm_mem(as, 0, &mem);
      if (needs_patch) {
        as_add_patch(as, as->code_pos, src.text, 0, 4);
      }
      emit32(as, val);
      return;
    }

    as_error(as, "invalid source for mov [mem], ...");
    return;
  }

  as_error(as, "invalid operand for mov");
}

/* ── LEA ─────────────────────────────────────────────────────────── */
static void as_encode_lea(as_state_t *as) {
  as_token_t dst = as_advance(as);
  if (dst.type != AS_TOK_REGISTER || dst.reg_size != 4) {
    as_error(as, "lea requires 32-bit register");
    return;
  }

  as_token_t comma = as_advance(as);
  if (comma.type != AS_TOK_COMMA) {
    as_error(as, "expected comma");
    return;
  }

  as_token_t lbrack = as_advance(as);
  if (lbrack.type != AS_TOK_LBRACK) {
    as_error(as, "lea requires memory operand");
    return;
  }

  as_mem_operand_t mem = as_parse_mem(as);
  emit8(as, 0x8D); /* lea r32, m32 */
  as_emit_modrm_mem(as, dst.reg_index, &mem);
}

/* ── XCHG ────────────────────────────────────────────────────────── */
static void as_encode_xchg(as_state_t *as) {
  as_token_t dst = as_advance(as);
  if (dst.type != AS_TOK_REGISTER || dst.reg_size != 4) {
    as_error(as, "xchg requires 32-bit register");
    return;
  }
  as_token_t comma = as_advance(as);
  if (comma.type != AS_TOK_COMMA) {
    as_error(as, "expected comma");
    return;
  }
  as_token_t src = as_advance(as);
  if (src.type != AS_TOK_REGISTER || src.reg_size != 4) {
    as_error(as, "xchg requires 32-bit register");
    return;
  }
  /* xchg eax, reg can use short form */
  if (dst.reg_index == 0) {
    emit8(as, (uint8_t)(0x90 + src.reg_index));
  } else if (src.reg_index == 0) {
    emit8(as, (uint8_t)(0x90 + dst.reg_index));
  } else {
    emit8(as, 0x87);
    emit8(as, modrm(3, dst.reg_index, src.reg_index));
  }
}

/* ── MOVZX / MOVSX ───────────────────────────────────────────────── */
static void as_encode_movzx_sx(as_state_t *as, const char *mn) {
  as_token_t dst = as_advance(as);
  if (dst.type != AS_TOK_REGISTER || dst.reg_size != 4) {
    as_error(as, "movzx/movsx requires 32-bit dest register");
    return;
  }
  as_token_t comma = as_advance(as);
  if (comma.type != AS_TOK_COMMA) {
    as_error(as, "expected comma");
    return;
  }
  as_token_t src = as_advance(as);
  if (src.type == AS_TOK_REGISTER) {
    /* movzx/movsx r32, r8 or r16 */
    uint8_t op2;
    if (src.reg_size == 1) {
      op2 = (strcmp(mn, "movzx") == 0) ? 0xB6 : 0xBE;
    } else {
      op2 = (strcmp(mn, "movzx") == 0) ? 0xB7 : 0xBF;
    }
    emit8(as, 0x0F);
    emit8(as, op2);
    emit8(as, modrm(3, dst.reg_index, src.reg_index));
    return;
  }
  as_error(as, "invalid source for movzx/movsx");
}

/* ── ALU: ADD, SUB, AND, OR, XOR, CMP, TEST, SHL, SHR, SAR ─────── */
static void as_encode_alu(as_state_t *as, const char *mn) {
  as_token_t dst = as_advance(as);

  /* ALU reg, ... */
  if (dst.type == AS_TOK_REGISTER) {
    as_token_t comma = as_advance(as);
    if (comma.type != AS_TOK_COMMA) {
      as_error(as, "expected comma");
      return;
    }

    as_token_t src = as_lex_peek(as);

    /* Determine ALU operation digit/opcode */
    int alu_digit = 0;
    uint8_t alu_rr_op = 0; /* reg,reg opcode */

    if (strcmp(mn, "add") == 0)  { alu_digit = 0; alu_rr_op = 0x01; }
    if (strcmp(mn, "or") == 0)   { alu_digit = 1; alu_rr_op = 0x09; }
    if (strcmp(mn, "and") == 0)  { alu_digit = 4; alu_rr_op = 0x21; }
    if (strcmp(mn, "sub") == 0)  { alu_digit = 5; alu_rr_op = 0x29; }
    if (strcmp(mn, "xor") == 0)  { alu_digit = 6; alu_rr_op = 0x31; }
    if (strcmp(mn, "cmp") == 0)  { alu_digit = 7; alu_rr_op = 0x39; }

    /* Shift instructions */
    if (strcmp(mn, "shl") == 0 || strcmp(mn, "shr") == 0 ||
        strcmp(mn, "sar") == 0 || strcmp(mn, "rol") == 0 ||
        strcmp(mn, "ror") == 0) {
      int shift_digit = 4;
      if (strcmp(mn, "shr") == 0) shift_digit = 5;
      if (strcmp(mn, "sar") == 0) shift_digit = 7;
      if (strcmp(mn, "rol") == 0) shift_digit = 0;
      if (strcmp(mn, "ror") == 0) shift_digit = 1;

      if (src.type == AS_TOK_NUMBER) {
        as_advance(as);
        if (src.int_value == 1) {
          emit8(as, 0xD1);
          emit8(as, modrm(3, shift_digit, dst.reg_index));
        } else {
          emit8(as, 0xC1);
          emit8(as, modrm(3, shift_digit, dst.reg_index));
          emit8(as, (uint8_t)src.int_value);
        }
      } else if (src.type == AS_TOK_REGISTER &&
                 src.reg_index == 1 && src.reg_size == 1) {
        /* shl reg, cl */
        as_advance(as);
        emit8(as, 0xD3);
        emit8(as, modrm(3, shift_digit, dst.reg_index));
      } else {
        as_error(as, "shift requires imm8 or cl");
      }
      return;
    }

    /* TEST is special */
    if (strcmp(mn, "test") == 0) {
      if (src.type == AS_TOK_REGISTER) {
        as_advance(as);
        if (dst.reg_size == 4 && src.reg_size == 4) {
          emit8(as, 0x85);
          emit8(as, modrm(3, src.reg_index, dst.reg_index));
        } else if (dst.reg_size == 1 && src.reg_size == 1) {
          emit8(as, 0x84);
          emit8(as, modrm(3, src.reg_index, dst.reg_index));
        } else {
          as_error(as, "register size mismatch");
        }
      } else if (src.type == AS_TOK_NUMBER) {
        as_advance(as);
        if (dst.reg_size == 4) {
          if (dst.reg_index == 0) {
            emit8(as, 0xA9); /* test eax, imm32 */
          } else {
            emit8(as, 0xF7);
            emit8(as, modrm(3, 0, dst.reg_index));
          }
          emit32(as, (uint32_t)src.int_value);
        } else if (dst.reg_size == 1) {
          if (dst.reg_index == 0) {
            emit8(as, 0xA8); /* test al, imm8 */
          } else {
            emit8(as, 0xF6);
            emit8(as, modrm(3, 0, dst.reg_index));
          }
          emit8(as, (uint8_t)src.int_value);
        } else {
          as_error(as, "invalid test operand size");
        }
      } else {
        as_error(as, "invalid test operand");
      }
      return;
    }

    /* ALU reg, reg */
    if (src.type == AS_TOK_REGISTER) {
      as_advance(as);
      if (dst.reg_size == 4 && src.reg_size == 4) {
        emit8(as, alu_rr_op);
        emit8(as, modrm(3, src.reg_index, dst.reg_index));
      } else if (dst.reg_size == 1 && src.reg_size == 1) {
        emit8(as, (uint8_t)(alu_rr_op - 1)); /* 8-bit variant */
        emit8(as, modrm(3, src.reg_index, dst.reg_index));
      } else {
        as_error(as, "register size mismatch");
      }
      return;
    }

    /* ALU reg, imm */
    if (src.type == AS_TOK_NUMBER || src.type == AS_TOK_IDENT) {
      int needs_patch = 0;
      char pname[AS_MAX_IDENT];
      pname[0] = '\0';
      int32_t val;
      if (src.type == AS_TOK_NUMBER) {
        as_advance(as);
        val = src.int_value;
      } else {
        as_advance(as);
        val = (int32_t)as_resolve_ident(as, src.text, &needs_patch);
        if (needs_patch) {
          int i = 0;
          while (src.text[i] && i < AS_MAX_IDENT - 1) {
            pname[i] = src.text[i]; i++;
          }
          pname[i] = '\0';
        }
      }

      if (dst.reg_size == 4) {
        /* Try short form: 83 /digit imm8 */
        if (!needs_patch && val >= -128 && val <= 127) {
          emit8(as, 0x83);
          emit8(as, modrm(3, alu_digit, dst.reg_index));
          emit8(as, (uint8_t)(int8_t)val);
        } else {
          /* EAX has short form for some ALU ops */
          if (dst.reg_index == 0 && !needs_patch) {
            uint8_t short_ops[] = {0x05, 0x0D, 0, 0, 0x25, 0x2D, 0x35, 0x3D};
            if (short_ops[alu_digit]) {
              emit8(as, short_ops[alu_digit]);
              emit32(as, (uint32_t)val);
              return;
            }
          }
          emit8(as, 0x81);
          emit8(as, modrm(3, alu_digit, dst.reg_index));
          if (needs_patch) {
            as_add_patch(as, as->code_pos, pname, 0, 4);
          }
          emit32(as, (uint32_t)val);
        }
      } else if (dst.reg_size == 1) {
        emit8(as, 0x80);
        emit8(as, modrm(3, alu_digit, dst.reg_index));
        emit8(as, (uint8_t)val);
      } else {
        as_error(as, "unsupported ALU operand size");
      }
      return;
    }

    /* ALU reg, [mem] */
    if (src.type == AS_TOK_LBRACK) {
      as_advance(as);
      as_mem_operand_t mem = as_parse_mem(as);
      /* Use the /r form: opcode+2 for reg,mem */
      emit8(as, (uint8_t)(alu_rr_op + 2));
      as_emit_modrm_mem(as, dst.reg_index, &mem);
      return;
    }

    as_error(as, "invalid ALU source operand");
    return;
  }

  /* ALU [mem], ... */
  if (dst.type == AS_TOK_LBRACK) {
    as_mem_operand_t mem = as_parse_mem(as);

    as_token_t comma = as_advance(as);
    if (comma.type != AS_TOK_COMMA) {
      as_error(as, "expected comma");
      return;
    }

    int alu_digit = 0;
    uint8_t alu_rr_op = 0;
    if (strcmp(mn, "add") == 0)  { alu_digit = 0; alu_rr_op = 0x01; }
    if (strcmp(mn, "or") == 0)   { alu_digit = 1; alu_rr_op = 0x09; }
    if (strcmp(mn, "and") == 0)  { alu_digit = 4; alu_rr_op = 0x21; }
    if (strcmp(mn, "sub") == 0)  { alu_digit = 5; alu_rr_op = 0x29; }
    if (strcmp(mn, "xor") == 0)  { alu_digit = 6; alu_rr_op = 0x31; }
    if (strcmp(mn, "cmp") == 0)  { alu_digit = 7; alu_rr_op = 0x39; }

    as_token_t src = as_lex_peek(as);

    if (src.type == AS_TOK_REGISTER) {
      as_advance(as);
      emit8(as, alu_rr_op);  /* ALU [mem], reg */
      as_emit_modrm_mem(as, src.reg_index, &mem);
      return;
    }

    if (src.type == AS_TOK_NUMBER) {
      as_advance(as);
      emit8(as, 0x81);
      as_emit_modrm_mem(as, alu_digit, &mem);
      emit32(as, (uint32_t)src.int_value);
      return;
    }

    as_error(as, "invalid ALU operand for memory dest");
    return;
  }

  as_error(as, "invalid ALU destination");
}

/* ── IN / OUT ────────────────────────────────────────────────────── */
static void as_encode_in(as_state_t *as) {
  as_token_t dst = as_advance(as);
  if (dst.type != AS_TOK_REGISTER) {
    as_error(as, "in requires register operand");
    return;
  }
  as_token_t comma = as_advance(as);
  if (comma.type != AS_TOK_COMMA) {
    as_error(as, "expected comma");
    return;
  }
  as_token_t src = as_advance(as);

  if (src.type == AS_TOK_NUMBER) {
    if (dst.reg_size == 4) {
      emit8(as, 0xE5); /* in eax, imm8 */
    } else {
      emit8(as, 0xE4); /* in al, imm8 */
    }
    emit8(as, (uint8_t)src.int_value);
  } else if (src.type == AS_TOK_REGISTER && src.reg_index == 2 &&
             src.reg_size == 2) {
    /* in al/eax, dx */
    if (dst.reg_size == 4) {
      emit8(as, 0xED); /* in eax, dx */
    } else {
      emit8(as, 0xEC); /* in al, dx */
    }
  } else {
    as_error(as, "invalid source for in");
  }
}

static void as_encode_out(as_state_t *as) {
  as_token_t dst = as_advance(as);

  if (dst.type == AS_TOK_NUMBER) {
    as_token_t comma = as_advance(as);
    if (comma.type != AS_TOK_COMMA) { as_error(as, "expected comma"); return; }
    as_token_t src = as_advance(as);
    if (src.type != AS_TOK_REGISTER) {
      as_error(as, "out requires register source");
      return;
    }
    if (src.reg_size == 4) {
      emit8(as, 0xE7); /* out imm8, eax */
    } else {
      emit8(as, 0xE6); /* out imm8, al */
    }
    emit8(as, (uint8_t)dst.int_value);
  } else if (dst.type == AS_TOK_REGISTER && dst.reg_index == 2 &&
             dst.reg_size == 2) {
    /* out dx, al/eax */
    as_token_t comma = as_advance(as);
    if (comma.type != AS_TOK_COMMA) { as_error(as, "expected comma"); return; }
    as_token_t src = as_advance(as);
    if (src.type != AS_TOK_REGISTER) {
      as_error(as, "out requires register source");
      return;
    }
    if (src.reg_size == 4) {
      emit8(as, 0xEF); /* out dx, eax */
    } else {
      emit8(as, 0xEE); /* out dx, al */
    }
  } else {
    as_error(as, "invalid destination for out");
  }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Directive Handlers
 * ══════════════════════════════════════════════════════════════════════ */

/* db "string", 0   or   db 0x41, 0x42, 0 */
static void as_handle_db(as_state_t *as) {
  for (;;) {
    as_token_t tok = as_lex_peek(as);

    if (tok.type == AS_TOK_STRING) {
      as_advance(as);
      for (int i = 0; tok.text[i]; i++) {
        emit_data8(as, (uint8_t)tok.text[i]);
      }
    } else if (tok.type == AS_TOK_NUMBER) {
      as_advance(as);
      emit_data8(as, (uint8_t)tok.int_value);
    } else if (tok.type == AS_TOK_IDENT) {
      as_advance(as);
      int needs_patch = 0;
      uint32_t val = as_resolve_ident(as, tok.text, &needs_patch);
      emit_data8(as, (uint8_t)val);
      /* Note: forward refs for data bytes are uncommon */
    } else {
      break;
    }

    /* Check for comma */
    tok = as_lex_peek(as);
    if (tok.type == AS_TOK_COMMA) {
      as_advance(as);
    } else {
      break;
    }
  }
}

static void as_handle_dw(as_state_t *as) {
  for (;;) {
    as_token_t tok = as_lex_peek(as);
    if (tok.type == AS_TOK_NUMBER) {
      as_advance(as);
      emit_data16(as, (uint16_t)tok.int_value);
    } else if (tok.type == AS_TOK_IDENT) {
      as_advance(as);
      int needs_patch = 0;
      uint32_t val = as_resolve_ident(as, tok.text, &needs_patch);
      emit_data16(as, (uint16_t)val);
    } else {
      break;
    }
    tok = as_lex_peek(as);
    if (tok.type == AS_TOK_COMMA) { as_advance(as); } else { break; }
  }
}

static void as_handle_dd(as_state_t *as) {
  for (;;) {
    as_token_t tok = as_lex_peek(as);
    if (tok.type == AS_TOK_NUMBER) {
      as_advance(as);
      emit_data32(as, (uint32_t)tok.int_value);
    } else if (tok.type == AS_TOK_IDENT) {
      as_advance(as);
      int needs_patch = 0;
      uint32_t val = as_resolve_ident(as, tok.text, &needs_patch);
      /* Data section patches: we use code_offset here but the data
       * is in the data buffer.  Mark as absolute. */
      if (needs_patch) {
        /* Store data_pos offset with a flag so patcher knows it's data */
        /* For simplicity, store the absolute offset in data buffer
         * and handle in patch resolution by checking range */
        as_add_patch(as, 0x80000000u | (as->data_pos), tok.text, 0, 4);
      }
      emit_data32(as, val);
    } else {
      break;
    }
    tok = as_lex_peek(as);
    if (tok.type == AS_TOK_COMMA) { as_advance(as); } else { break; }
  }
}

static void as_handle_resb(as_state_t *as) {
  as_token_t tok = as_advance(as);
  if (tok.type != AS_TOK_NUMBER) {
    as_error(as, "resb requires count");
    return;
  }
  for (int32_t i = 0; i < tok.int_value; i++) {
    emit_data8(as, 0);
  }
}

static void as_handle_resw(as_state_t *as) {
  as_token_t tok = as_advance(as);
  if (tok.type != AS_TOK_NUMBER) {
    as_error(as, "resw requires count");
    return;
  }
  for (int32_t i = 0; i < tok.int_value; i++) {
    emit_data16(as, 0);
  }
}

static void as_handle_resd(as_state_t *as) {
  as_token_t tok = as_advance(as);
  if (tok.type != AS_TOK_NUMBER) {
    as_error(as, "resd requires count");
    return;
  }
  for (int32_t i = 0; i < tok.int_value; i++) {
    emit_data32(as, 0);
  }
}

static void as_handle_times(as_state_t *as) {
  as_token_t count_tok = as_advance(as);
  if (count_tok.type != AS_TOK_NUMBER) {
    as_error(as, "times requires count");
    return;
  }
  int32_t count = count_tok.int_value;
  as_token_t what = as_advance(as);

  if (what.type == AS_TOK_DIRECTIVE && strcmp(what.text, "db") == 0) {
    as_token_t val = as_advance(as);
    for (int32_t i = 0; i < count; i++) {
      emit_data8(as, (uint8_t)val.int_value);
    }
  } else if (what.type == AS_TOK_MNEMONIC && strcmp(what.text, "nop") == 0) {
    for (int32_t i = 0; i < count; i++) {
      emit8(as, 0x90);
    }
  } else {
    as_error(as, "unsupported times target");
  }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Forward-Reference Patch Resolution (second pass)
 * ══════════════════════════════════════════════════════════════════════ */

static void as_resolve_patches(as_state_t *as) {
  for (int i = 0; i < as->patch_count; i++) {
    as_patch_t *p = &as->patches[i];
    as_label_t *lbl = as_find_label(as, p->name);

    if (!lbl || !lbl->defined) {
      serial_printf("[asm] ERROR: undefined label '%s'\n", p->name);
      as_error(as, "undefined label");
      return;
    }

    uint32_t addr = lbl->address;

    /* Check if this is a data section patch (high bit set) */
    if (p->code_offset & 0x80000000u) {
      uint32_t data_off = p->code_offset & 0x7FFFFFFFu;
      if (data_off + 4 <= AS_MAX_DATA) {
        as->data[data_off]     = (uint8_t)(addr & 0xFF);
        as->data[data_off + 1] = (uint8_t)((addr >> 8) & 0xFF);
        as->data[data_off + 2] = (uint8_t)((addr >> 16) & 0xFF);
        as->data[data_off + 3] = (uint8_t)((addr >> 24) & 0xFF);
      }
      continue;
    }

    if (p->rel) {
      /* Relative: target - (patch_location + width) */
      uint32_t patch_addr = as->code_base + p->code_offset;
      if (p->width == 4) {
        uint32_t rel = addr - (patch_addr + 4);
        patch32(as, p->code_offset, rel);
      } else if (p->width == 1) {
        int32_t rel = (int32_t)(addr - (patch_addr + 1));
        if (rel < -128 || rel > 127) {
          serial_printf("[asm] ERROR: short jump out of range for '%s'\n",
                        p->name);
          as_error(as, "short jump out of range");
          return;
        }
        patch8(as, p->code_offset, (uint8_t)(int8_t)rel);
      }
    } else {
      /* Absolute */
      if (p->width == 4) {
        patch32(as, p->code_offset, addr);
      } else if (p->width == 1) {
        patch8(as, p->code_offset, (uint8_t)addr);
      }
    }
  }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Main Parser — as_parse_program()
 *
 *  Reads one statement per line:
 *    label_def | directive | mnemonic [operands] | blank line
 * ══════════════════════════════════════════════════════════════════════ */

void as_parse_program(as_state_t *as) {
  /* NOTE: do NOT reset label_count here — kernel bindings and equ
   * constants were registered during as_init_state() and must survive. */
  if (as->include_depth == 0) {
    as->patch_count = 0;
    as->current_section = 0; /* start in .text */
    as->has_entry = 0;
  }

  for (;;) {
    if (as->error) return;

    as_skip_newlines(as);
    as_token_t tok = as_lex_peek(as);

    if (tok.type == AS_TOK_EOF) break;

    /* ── Label definition ── */
    if (tok.type == AS_TOK_LABEL_DEF) {
      as_advance(as);
      uint32_t addr;
      if (as->current_section == 0) {
        addr = as_code_addr(as);
      } else {
        addr = as_data_addr(as);
      }
      as_add_label(as, tok.text, addr, 1, 0);

      /* Check for "main" or "_start" entry point */
      if (as_label_strcmp(tok.text, "main") == 0 ||
          as_label_strcmp(tok.text, "_start") == 0) {
        as->entry_offset = as->code_pos;
        as->has_entry = 1;
      }
      continue;
    }

    /* ── Identifier followed by equ or : ── */
    if (tok.type == AS_TOK_IDENT) {
      /* Peek further to see if next token is 'equ' directive */
      as_token_t ident_tok = as_advance(as);
      as_token_t next = as_lex_peek(as);

      if (next.type == AS_TOK_DIRECTIVE && strcmp(next.text, "equ") == 0) {
        /* name equ value */
        as_advance(as); /* consume "equ" */
        as_token_t val = as_advance(as);
        if (val.type == AS_TOK_NUMBER) {
          as_add_label(as, ident_tok.text, (uint32_t)val.int_value, 1, 1);
        } else {
          as_error(as, "equ requires numeric value");
        }
        as_expect_newline_or_eof(as);
        continue;
      }

       /* Identifier followed by db/dw/dd/res* — data label */
      if (next.type == AS_TOK_DIRECTIVE &&
          (strcmp(next.text, "db") == 0 || strcmp(next.text, "dw") == 0 ||
           strcmp(next.text, "dd") == 0 || strcmp(next.text, "resb") == 0 ||
           strcmp(next.text, "resw") == 0 || strcmp(next.text, "resd") == 0 ||
         strcmp(next.text, "rb") == 0 || strcmp(next.text, "rw") == 0 ||
         strcmp(next.text, "rd") == 0 ||
           strcmp(next.text, "reserve") == 0)) {
        /* Define label at current data position */
        as_add_label(as, ident_tok.text, as_data_addr(as), 1, 0);
        as_advance(as); /* consume directive */
        if (strcmp(next.text, "db") == 0)      as_handle_db(as);
        else if (strcmp(next.text, "dw") == 0)  as_handle_dw(as);
        else if (strcmp(next.text, "dd") == 0)  as_handle_dd(as);
        else if (strcmp(next.text, "resb") == 0 || strcmp(next.text, "rb") == 0) as_handle_resb(as);
        else if (strcmp(next.text, "resw") == 0 || strcmp(next.text, "rw") == 0) as_handle_resw(as);
        else if (strcmp(next.text, "resd") == 0 || strcmp(next.text, "rd") == 0) as_handle_resd(as);
        else                                       as_handle_resb(as); /* reserve */
        as_expect_newline_or_eof(as);
        continue;
      }

      /* Identifier followed by colon — label definition */
      if (next.type == AS_TOK_COLON) {
        as_advance(as); /* consume : */
        uint32_t addr;
        if (as->current_section == 0) {
          addr = as_code_addr(as);
        } else {
          addr = as_data_addr(as);
        }
        as_add_label(as, ident_tok.text, addr, 1, 0);
        if (as_label_strcmp(ident_tok.text, "main") == 0 ||
            as_label_strcmp(ident_tok.text, "_start") == 0) {
          as->entry_offset = as->code_pos;
          as->has_entry = 1;
        }

        /* NASM-style inline data declaration after label:
         *   name: db ...
         *   buf:  resb 64
         */
        as_token_t after_colon = as_lex_peek(as);
        if (after_colon.type == AS_TOK_DIRECTIVE) {
          as_advance(as);
          if (strcmp(after_colon.text, "db") == 0) { as_handle_db(as); as_expect_newline_or_eof(as); continue; }
          if (strcmp(after_colon.text, "dw") == 0) { as_handle_dw(as); as_expect_newline_or_eof(as); continue; }
          if (strcmp(after_colon.text, "dd") == 0) { as_handle_dd(as); as_expect_newline_or_eof(as); continue; }
          if (strcmp(after_colon.text, "resb") == 0 || strcmp(after_colon.text, "rb") == 0 || strcmp(after_colon.text, "reserve") == 0) {
            as_handle_resb(as); as_expect_newline_or_eof(as); continue;
          }
          if (strcmp(after_colon.text, "resw") == 0 || strcmp(after_colon.text, "rw") == 0) {
            as_handle_resw(as); as_expect_newline_or_eof(as); continue;
          }
          if (strcmp(after_colon.text, "resd") == 0 || strcmp(after_colon.text, "rd") == 0) {
            as_handle_resd(as); as_expect_newline_or_eof(as); continue;
          }
        }

        continue;
      }

      /* Unexpected identifier without context */
      as_error(as, "unexpected identifier");
      continue;
    }

    /* ── Directives ── */
    if (tok.type == AS_TOK_DIRECTIVE) {
      as_advance(as);

      if (strcmp(tok.text, "section") == 0) {
        as_token_t sec = as_advance(as);
        if (sec.type == AS_TOK_IDENT || sec.type == AS_TOK_DIRECTIVE) {
          if (as_label_strcmp(sec.text, ".text") == 0) {
            as->current_section = 0;
          } else if (as_label_strcmp(sec.text, ".data") == 0 ||
                     as_label_strcmp(sec.text, ".bss") == 0) {
            as->current_section = 1;
          }
        }
        as_expect_newline_or_eof(as);
        continue;
      }

      if (strcmp(tok.text, "db") == 0)   { as_handle_db(as); as_expect_newline_or_eof(as); continue; }
      if (strcmp(tok.text, "dw") == 0)   { as_handle_dw(as); as_expect_newline_or_eof(as); continue; }
      if (strcmp(tok.text, "dd") == 0)   { as_handle_dd(as); as_expect_newline_or_eof(as); continue; }
      if (strcmp(tok.text, "resb") == 0) { as_handle_resb(as); as_expect_newline_or_eof(as); continue; }
      if (strcmp(tok.text, "resw") == 0) { as_handle_resw(as); as_expect_newline_or_eof(as); continue; }
      if (strcmp(tok.text, "resd") == 0) { as_handle_resd(as); as_expect_newline_or_eof(as); continue; }
      if (strcmp(tok.text, "rb") == 0) { as_handle_resb(as); as_expect_newline_or_eof(as); continue; }
      if (strcmp(tok.text, "rw") == 0) { as_handle_resw(as); as_expect_newline_or_eof(as); continue; }
      if (strcmp(tok.text, "rd") == 0) { as_handle_resd(as); as_expect_newline_or_eof(as); continue; }
      if (strcmp(tok.text, "reserve") == 0) { as_handle_resb(as); as_expect_newline_or_eof(as); continue; }
      if (strcmp(tok.text, "times") == 0) { as_handle_times(as); as_expect_newline_or_eof(as); continue; }
      if (strcmp(tok.text, "global") == 0 || strcmp(tok.text, "extern") == 0) {
        /* Just consume the identifier — we don't do linking yet */
        as_advance(as);
        as_expect_newline_or_eof(as);
        continue;
      }

      /* %include */
      if (strcmp(tok.text, "%include") == 0) {
        as_token_t file = as_advance(as);
        if (file.type != AS_TOK_STRING && file.type != AS_TOK_IDENT) {
          as_error(as, "%include requires a file path");
          as_expect_newline_or_eof(as);
          continue;
        }

        if (as->include_depth >= AS_MAX_INCLUDE_DEPTH) {
          as_error(as, "%include depth exceeded");
          as_expect_newline_or_eof(as);
          continue;
        }

        char *inc_src = as_read_include_source(file.text);
        if (!inc_src) {
          as_error(as, "failed to read include file");
          as_expect_newline_or_eof(as);
          continue;
        }

        as_lex_snapshot_t snap;
        snap.source = as->source;
        snap.pos = as->pos;
        snap.line = as->line;
        snap.cur = as->cur;
        snap.peek_buf = as->peek_buf;
        snap.has_peek = as->has_peek;

        as->include_depth++;
        as_lex_init(as, inc_src);
        as_parse_program(as);
        as->include_depth--;

        as->source = snap.source;
        as->pos = snap.pos;
        as->line = snap.line;
        as->cur = snap.cur;
        as->peek_buf = snap.peek_buf;
        as->has_peek = snap.has_peek;

        kfree(inc_src);
        as_expect_newline_or_eof(as);
        continue;
      }

      as_expect_newline_or_eof(as);
      continue;
    }

    /* ── Mnemonics (instructions) ── */
    if (tok.type == AS_TOK_MNEMONIC) {
      as_advance(as);
      const char *mn = tok.text;

      /* No-operand instructions */
      if (strcmp(mn, "nop") == 0 || strcmp(mn, "ret") == 0 ||
          strcmp(mn, "hlt") == 0 || strcmp(mn, "cli") == 0 ||
          strcmp(mn, "sti") == 0 || strcmp(mn, "leave") == 0 ||
          strcmp(mn, "iret") == 0 || strcmp(mn, "pushad") == 0 ||
          strcmp(mn, "popad") == 0 || strcmp(mn, "pushfd") == 0 ||
          strcmp(mn, "popfd") == 0 || strcmp(mn, "cdq") == 0 ||
          strcmp(mn, "cbw") == 0 || strcmp(mn, "cwde") == 0 ||
          strcmp(mn, "movsb") == 0 || strcmp(mn, "movsd") == 0 ||
          strcmp(mn, "movsw") == 0 || strcmp(mn, "stosb") == 0 ||
          strcmp(mn, "stosd") == 0 || strcmp(mn, "stosw") == 0 ||
          strcmp(mn, "cld") == 0 || strcmp(mn, "std") == 0 ||
          strcmp(mn, "clc") == 0 || strcmp(mn, "stc") == 0 ||
          strcmp(mn, "cmc") == 0 || strcmp(mn, "int3") == 0 ||
          strcmp(mn, "pushf") == 0 || strcmp(mn, "popf") == 0 ||
          strcmp(mn, "pusha") == 0 || strcmp(mn, "popa") == 0) {
        as_encode_noops(as, mn);
        as_expect_newline_or_eof(as);
        continue;
      }

      if (strcmp(mn, "rep") == 0) {
        as_encode_rep(as);
        as_expect_newline_or_eof(as);
        continue;
      }

      if (strcmp(mn, "push") == 0) {
        as_encode_push(as);
        as_expect_newline_or_eof(as);
        continue;
      }

      if (strcmp(mn, "pop") == 0) {
        as_encode_pop(as);
        as_expect_newline_or_eof(as);
        continue;
      }

      if (strcmp(mn, "mov") == 0) {
        as_encode_mov(as);
        as_expect_newline_or_eof(as);
        continue;
      }

      if (strcmp(mn, "lea") == 0) {
        as_encode_lea(as);
        as_expect_newline_or_eof(as);
        continue;
      }

      if (strcmp(mn, "xchg") == 0) {
        as_encode_xchg(as);
        as_expect_newline_or_eof(as);
        continue;
      }

      if (strcmp(mn, "movzx") == 0 || strcmp(mn, "movsx") == 0) {
        as_encode_movzx_sx(as, mn);
        as_expect_newline_or_eof(as);
        continue;
      }

      if (strcmp(mn, "call") == 0) {
        as_encode_call(as);
        as_expect_newline_or_eof(as);
        continue;
      }

      if (strcmp(mn, "jmp") == 0 || strcmp(mn, "je") == 0 ||
          strcmp(mn, "jne") == 0 || strcmp(mn, "jz") == 0 ||
          strcmp(mn, "jnz") == 0 || strcmp(mn, "jl") == 0 ||
          strcmp(mn, "jg") == 0 || strcmp(mn, "jle") == 0 ||
          strcmp(mn, "jge") == 0 || strcmp(mn, "jb") == 0 ||
          strcmp(mn, "jbe") == 0 || strcmp(mn, "ja") == 0 ||
          strcmp(mn, "jae") == 0 || strcmp(mn, "js") == 0 ||
          strcmp(mn, "jns") == 0 || strcmp(mn, "jo") == 0 ||
          strcmp(mn, "jno") == 0 || strcmp(mn, "jc") == 0 ||
          strcmp(mn, "jnc") == 0 || strcmp(mn, "jnae") == 0 ||
          strcmp(mn, "jnb") == 0 || strcmp(mn, "jna") == 0 ||
          strcmp(mn, "jnbe") == 0 || strcmp(mn, "jnge") == 0 ||
          strcmp(mn, "jnl") == 0 || strcmp(mn, "jng") == 0 ||
          strcmp(mn, "jnle") == 0 || strcmp(mn, "jp") == 0 ||
          strcmp(mn, "jpe") == 0 || strcmp(mn, "jnp") == 0 ||
          strcmp(mn, "jpo") == 0) {
        as_encode_jmp(as, mn);
        as_expect_newline_or_eof(as);
        continue;
      }

      if (strcmp(mn, "inc") == 0 || strcmp(mn, "dec") == 0) {
        as_encode_incdec(as, mn);
        as_expect_newline_or_eof(as);
        continue;
      }

      if (strcmp(mn, "not") == 0 || strcmp(mn, "neg") == 0) {
        as_encode_not_neg(as, mn);
        as_expect_newline_or_eof(as);
        continue;
      }

      if (strcmp(mn, "mul") == 0 || strcmp(mn, "div") == 0 ||
          strcmp(mn, "imul") == 0 || strcmp(mn, "idiv") == 0) {
        as_encode_muldiv(as, mn);
        as_expect_newline_or_eof(as);
        continue;
      }

      if (strcmp(mn, "int") == 0) {
        as_encode_int(as);
        as_expect_newline_or_eof(as);
        continue;
      }

      if (strcmp(mn, "in") == 0) {
        as_encode_in(as);
        as_expect_newline_or_eof(as);
        continue;
      }

      if (strcmp(mn, "out") == 0) {
        as_encode_out(as);
        as_expect_newline_or_eof(as);
        continue;
      }

      /* ALU instructions */
      if (strcmp(mn, "add") == 0 || strcmp(mn, "sub") == 0 ||
          strcmp(mn, "and") == 0 || strcmp(mn, "or") == 0 ||
          strcmp(mn, "xor") == 0 || strcmp(mn, "cmp") == 0 ||
          strcmp(mn, "test") == 0 || strcmp(mn, "shl") == 0 ||
          strcmp(mn, "shr") == 0 || strcmp(mn, "sar") == 0 ||
          strcmp(mn, "rol") == 0 || strcmp(mn, "ror") == 0) {
        as_encode_alu(as, mn);
        as_expect_newline_or_eof(as);
        continue;
      }

      as_error(as, "unimplemented instruction");
      as_expect_newline_or_eof(as);
      continue;
    }

    /* Unknown token — skip line */
    as_error(as, "unexpected token");
    as_advance(as);
    while (as_lex_peek(as).type != AS_TOK_NEWLINE &&
           as_lex_peek(as).type != AS_TOK_EOF) {
      as_advance(as);
    }
  }

  /* Resolve forward references */
  if (!as->error && as->include_depth == 0) {
    as_resolve_patches(as);
  }
}
