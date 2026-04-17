/**
 * cupidc_parse.c - Parser and x86 code generator for CupidC
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

/* x86 Machine Code Emission Helpers */

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

/* call relative (placeholder - returns offset of the rel32 for patching) */
static uint32_t emit_call_rel_placeholder(cc_state_t *cc) {
  emit8(cc, 0xE8);
  uint32_t patch_pos = cc->code_pos;
  emit32(cc, 0); /* placeholder */
  return patch_pos;
}

/* jmp rel32 (unconditional) - returns offset for patching */
static uint32_t emit_jmp_placeholder(cc_state_t *cc) {
  emit8(cc, 0xE9);
  uint32_t patch_pos = cc->code_pos;
  emit32(cc, 0);
  return patch_pos;
}

/* jcc rel32 (conditional jump) - returns offset for patching */
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

/* add esp, imm (clean up stack args).  Uses imm8 form when possible, else
 * imm32.  Task 18: callers may pass >127 when args include doubles. */
static void emit_add_esp(cc_state_t *cc, int32_t val) {
  if (val == 0)
    return;
  if (val >= -128 && val <= 127) {
    emit8(cc, 0x83);
    emit8(cc, 0xC4);
    emit8(cc, (uint8_t)(val & 0xFF));
  } else {
    emit8(cc, 0x81);
    emit8(cc, 0xC4);
    emit32(cc, (uint32_t)val);
  }
}

/* sub esp, imm32 (allocate locals) */
static void emit_sub_esp(cc_state_t *cc, uint32_t val) {
  if (val == 0)
    return;
  emit8(cc, 0x81);
  emit8(cc, 0xEC);
  emit32(cc, val);
}

/* Function prologue: push ebp; mov ebp, esp; and esp, -16.
 *
 * Task 18: unconditionally 16-byte align ESP so subsequent MOVAPS/MOVDQA
 * (used for SIMD, and potentially also for libm) is safe.  Cost is 3
 * extra bytes per function; in exchange we don't need to track whether
 * a given function touches SSE.  Local-frame size is rounded up to a
 * multiple of 16 (see emit_sub_esp patching) so ESP stays 16-aligned
 * after the SUB ESP, <local_frame>. */
static void emit_prologue(cc_state_t *cc) {
  emit8(cc, 0x55); /* push ebp */
  emit8(cc, 0x89); /* mov ebp, esp */
  emit8(cc, 0xE5);
  emit8(cc, 0x83); /* and esp, 0xFFFFFFF0 */
  emit8(cc, 0xE4);
  emit8(cc, 0xF0);
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

/* SSE Scalar FP Codegen Helpers
 *
 * Task 16: scalar float/double arithmetic. Strategy:
 *  - XMM0 is the FP "accumulator" mirroring EAX in the integer code path.
 *  - For binops, the left operand is spilled to the stack as 8 bytes
 *    (sub esp,8; movsd [esp], xmm0), the right operand is evaluated into
 *    XMM0, then the left is reloaded into XMM1 and the SSE op produces
 *    the result back in XMM0.  This mirrors push/pop EAX/EBX flow.
 *  - MOVSS is used for float, MOVSD for double.  Both only require
 *    natural-width alignment so no AND ESP,-16 is needed for scalar.
 *
 * The 8-byte spill slot is used for float and double alike to keep ESP
 * 4-byte aligned regardless of type.  Stack in this compiler is never
 * guaranteed to be more than 4-byte aligned.  MOVSD is fine with 8-byte
 * aligned addresses; 4-byte alignment may generate #GP on some models,
 * but QEMU (and all x86 we target) tolerates misaligned MOVSD.
 */

/* Emit a ModR/M byte for [ebp + disp32] form:
 *   mod=10 (disp32), reg=xmm, r/m=101 (EBP) -> 0x85 | (xmm<<3)
 */
static uint8_t cc_xmm_modrm_ebp(int xmm) {
  return (uint8_t)(0x85 | ((xmm & 7) << 3));
}

/* Emit a ModR/M byte for [disp32] form (mod=00, r/m=101):
 *   0x05 | (xmm<<3)
 */
static uint8_t cc_xmm_modrm_disp32(int xmm) {
  return (uint8_t)(0x05 | ((xmm & 7) << 3));
}

/* MOVSS/MOVSD xmm, [disp32] — load from absolute data-segment address. */
static void emit_movss_xmm_disp32(cc_state_t *cc, int xmm, uint32_t addr) {
  emit8(cc, 0xF3); /* SS prefix */
  emit8(cc, 0x0F);
  emit8(cc, 0x10); /* MOVSS xmm, m32 */
  emit8(cc, cc_xmm_modrm_disp32(xmm));
  emit32(cc, addr);
}
static void emit_movsd_xmm_disp32(cc_state_t *cc, int xmm, uint32_t addr) {
  emit8(cc, 0xF2); /* SD prefix */
  emit8(cc, 0x0F);
  emit8(cc, 0x10); /* MOVSD xmm, m64 */
  emit8(cc, cc_xmm_modrm_disp32(xmm));
  emit32(cc, addr);
}

/* MOVSS/MOVSD xmm, [ebp + disp32] — load FP local/param into XMM reg. */
static void emit_movss_xmm_local(cc_state_t *cc, int xmm, int32_t offset) {
  emit8(cc, 0xF3);
  emit8(cc, 0x0F);
  emit8(cc, 0x10);
  emit8(cc, cc_xmm_modrm_ebp(xmm));
  emit32(cc, (uint32_t)offset);
}
static void emit_movsd_xmm_local(cc_state_t *cc, int xmm, int32_t offset) {
  emit8(cc, 0xF2);
  emit8(cc, 0x0F);
  emit8(cc, 0x10);
  emit8(cc, cc_xmm_modrm_ebp(xmm));
  emit32(cc, (uint32_t)offset);
}

/* MOVSS/MOVSD [ebp + disp32], xmm — store XMM reg into FP local/param. */
static void emit_movss_local_xmm(cc_state_t *cc, int xmm, int32_t offset) {
  emit8(cc, 0xF3);
  emit8(cc, 0x0F);
  emit8(cc, 0x11); /* MOVSS m32, xmm */
  emit8(cc, cc_xmm_modrm_ebp(xmm));
  emit32(cc, (uint32_t)offset);
}
static void emit_movsd_local_xmm(cc_state_t *cc, int xmm, int32_t offset) {
  emit8(cc, 0xF2);
  emit8(cc, 0x0F);
  emit8(cc, 0x11); /* MOVSD m64, xmm */
  emit8(cc, cc_xmm_modrm_ebp(xmm));
  emit32(cc, (uint32_t)offset);
}

/* MOVSS/MOVSD [esp], xmm  and  MOVSS/MOVSD xmm, [esp].
 * ModR/M: mod=00, reg=xmm, r/m=100 (SIB) + SIB byte 0x24 ([esp]).
 */
static void emit_movss_esp_xmm(cc_state_t *cc, int xmm) {
  emit8(cc, 0xF3);
  emit8(cc, 0x0F);
  emit8(cc, 0x11);
  emit8(cc, (uint8_t)(0x04 | ((xmm & 7) << 3)));
  emit8(cc, 0x24);
}
static void emit_movsd_esp_xmm(cc_state_t *cc, int xmm) {
  emit8(cc, 0xF2);
  emit8(cc, 0x0F);
  emit8(cc, 0x11);
  emit8(cc, (uint8_t)(0x04 | ((xmm & 7) << 3)));
  emit8(cc, 0x24);
}
static void emit_movss_xmm_esp(cc_state_t *cc, int xmm) {
  emit8(cc, 0xF3);
  emit8(cc, 0x0F);
  emit8(cc, 0x10);
  emit8(cc, (uint8_t)(0x04 | ((xmm & 7) << 3)));
  emit8(cc, 0x24);
}
static void emit_movsd_xmm_esp(cc_state_t *cc, int xmm) {
  emit8(cc, 0xF2);
  emit8(cc, 0x0F);
  emit8(cc, 0x10);
  emit8(cc, (uint8_t)(0x04 | ((xmm & 7) << 3)));
  emit8(cc, 0x24);
}

/* Push an XMM float onto the stack: SUB ESP,4 + MOVSS [ESP], xmm.
 * Task 18: used by function-call arg-push loop when arg is TYPE_FLOAT. */
static void emit_push_xmm_float(cc_state_t *cc, int xmm) {
  emit8(cc, 0x83); /* sub esp, 4 */
  emit8(cc, 0xEC);
  emit8(cc, 0x04);
  emit8(cc, 0xF3); /* movss [esp], xmm */
  emit8(cc, 0x0F);
  emit8(cc, 0x11);
  emit8(cc, (uint8_t)(0x04 | ((xmm & 7) << 3)));
  emit8(cc, 0x24);
}

/* Push an XMM double onto the stack: SUB ESP,8 + MOVSD [ESP], xmm.
 * Task 18: used by function-call arg-push loop when arg is TYPE_DOUBLE. */
static void emit_push_xmm_double(cc_state_t *cc, int xmm) {
  emit8(cc, 0x83); /* sub esp, 8 */
  emit8(cc, 0xEC);
  emit8(cc, 0x08);
  emit8(cc, 0xF2); /* movsd [esp], xmm */
  emit8(cc, 0x0F);
  emit8(cc, 0x11);
  emit8(cc, (uint8_t)(0x04 | ((xmm & 7) << 3)));
  emit8(cc, 0x24);
}

/* MOVAPS xmm_dst, xmm_src: 0F 28 /r (mod=11).  Task 18: used to move an
 * FP return value into XMM0 before emitting the epilogue. */
static void emit_movaps_xmm_xmm(cc_state_t *cc, int dst, int src) {
  if (dst == src)
    return;
  emit8(cc, 0x0F);
  emit8(cc, 0x28);
  emit8(cc, (uint8_t)(0xC0 | ((dst & 7) << 3) | (src & 7)));
}

/* MOVUPS xmm, [ebp + disp32] — unaligned 16-byte load of SIMD local/param.
 * Encoding: 0F 10 /r with ModR/M mod=10, reg=xmm, r/m=101 (EBP) + disp32.
 * Task 30: materializes a float4/double2 local into XMM0.
 * Task 31: switched from MOVAPS (0F 28) to MOVUPS (0F 10) because
 * [ebp + disp] alignment isn't guaranteed — Task 18's prologue does
 * `push ebp; mov ebp, esp; and esp, -16`, which aligns ESP but leaves
 * EBP holding the pre-AND value (which is off by 4 from the aligned
 * boundary because of the PUSH EBP). MOVUPS tolerates unaligned
 * addresses and is cheap on modern x86, so it's the safer choice. */
static void emit_movups_xmm_local(cc_state_t *cc, int xmm, int32_t offset) {
  emit8(cc, 0x0F);
  emit8(cc, 0x10);
  emit8(cc, cc_xmm_modrm_ebp(xmm));
  emit32(cc, (uint32_t)offset);
}

/* MOVUPS [ebp + disp32], xmm — unaligned 16-byte store of SIMD XMM reg.
 * Encoding: 0F 11 /r with ModR/M mod=10, reg=xmm, r/m=101 (EBP) + disp32.
 * Task 30: reserved for full-vector stores; init-list codegen currently
 * stores element-by-element via MOVSS/MOVSD.
 * Task 31: see emit_movups_xmm_local for why we use MOVUPS, not MOVAPS. */
__attribute__((unused))
static void emit_movups_local_xmm(cc_state_t *cc, int xmm, int32_t offset) {
  emit8(cc, 0x0F);
  emit8(cc, 0x11);
  emit8(cc, cc_xmm_modrm_ebp(xmm));
  emit32(cc, (uint32_t)offset);
}

/* ADDSS/SUBSS/MULSS/DIVSS  and  SD variants: xmm_dst OP= xmm_src.
 * Prefix 0xF3 (SS) or 0xF2 (SD), then 0x0F + op_byte + ModR/M.
 * ModR/M: mod=11, reg=dst, r/m=src -> 0xC0 | (dst<<3) | src.
 */
static void emit_sse_scalar_op(cc_state_t *cc, int is_double, uint8_t op_byte,
                               int xmm_dst, int xmm_src) {
  emit8(cc, is_double ? 0xF2 : 0xF3);
  emit8(cc, 0x0F);
  emit8(cc, op_byte);
  emit8(cc, (uint8_t)(0xC0 | ((xmm_dst & 7) << 3) | (xmm_src & 7)));
}

/* Forward decl: defined just below the error-handling block. */
static int cc_data_reserve(cc_state_t *cc, uint32_t bytes);

/* Emit raw bytes into the data segment and return the absolute address.
 * Returns 0 and sets error on overflow. */
static uint32_t cc_emit_data_bytes(cc_state_t *cc, const uint8_t *bytes,
                                   uint32_t n) {
  /* 4-byte align the data position so float/double live on natural
   * alignment where possible. */
  cc->data_pos = (cc->data_pos + 3u) & ~3u;
  if (!cc_data_reserve(cc, n))
    return 0;
  uint32_t addr = cc->data_base + cc->data_pos;
  for (uint32_t i = 0; i < n; i++) {
    cc->data[cc->data_pos++] = bytes[i];
  }
  return addr;
}

/* Task 17: int <-> float <-> double conversion helpers.
 *
 * All six conversion opcodes share a common layout:
 *   <prefix> 0F <op> <ModR/M>
 * where prefix selects SS (0xF3) or SD (0xF2) variants.  ModR/M uses
 * mod=11 (register-direct) throughout.
 *
 *   CVTSI2SS xmm, eax     F3 0F 2A /r   int (EAX)       -> float (xmm)
 *   CVTSI2SD xmm, eax     F2 0F 2A /r   int (EAX)       -> double (xmm)
 *   CVTTSS2SI eax, xmm    F3 0F 2C /r   float (xmm)     -> int (EAX), trunc
 *   CVTTSD2SI eax, xmm    F2 0F 2C /r   double (xmm)    -> int (EAX), trunc
 *   CVTSS2SD xmm_d, xmm_s F3 0F 5A /r   float (xmm_s)   -> double (xmm_d)
 *   CVTSD2SS xmm_d, xmm_s F2 0F 5A /r   double (xmm_s)  -> float (xmm_d)
 *
 * The truncating SI variants (CVTTSS2SI / CVTTSD2SI) are used rather
 * than the rounding ones (CVTSS2SI / CVTSD2SI) to match C semantics:
 * `(int)3.7` must yield 3, not the current-rounding-mode result.
 */

/* CVTSI2SS xmm, EAX — int in EAX to float in xmm. */
static void emit_cvtsi2ss(cc_state_t *cc, int xmm) {
  emit8(cc, 0xF3);
  emit8(cc, 0x0F);
  emit8(cc, 0x2A);
  /* ModR/M: mod=11, reg=xmm, r/m=000 (EAX). */
  emit8(cc, (uint8_t)(0xC0 | ((xmm & 7) << 3) | 0));
}

/* CVTSI2SD xmm, EAX — int in EAX to double in xmm. */
static void emit_cvtsi2sd(cc_state_t *cc, int xmm) {
  emit8(cc, 0xF2);
  emit8(cc, 0x0F);
  emit8(cc, 0x2A);
  emit8(cc, (uint8_t)(0xC0 | ((xmm & 7) << 3) | 0));
}

/* CVTTSS2SI EAX, xmm — truncating float to int (EAX). */
static void emit_cvttss2si(cc_state_t *cc, int xmm) {
  emit8(cc, 0xF3);
  emit8(cc, 0x0F);
  emit8(cc, 0x2C);
  /* ModR/M: mod=11, reg=000 (EAX), r/m=xmm. */
  emit8(cc, (uint8_t)(0xC0 | (0 << 3) | (xmm & 7)));
}

/* CVTTSD2SI EAX, xmm — truncating double to int (EAX). */
static void emit_cvttsd2si(cc_state_t *cc, int xmm) {
  emit8(cc, 0xF2);
  emit8(cc, 0x0F);
  emit8(cc, 0x2C);
  emit8(cc, (uint8_t)(0xC0 | (0 << 3) | (xmm & 7)));
}

/* CVTSS2SD xmm_dst, xmm_src — float to double (scalar, in XMM). */
static void emit_cvtss2sd(cc_state_t *cc, int xmm_dst, int xmm_src) {
  emit8(cc, 0xF3);
  emit8(cc, 0x0F);
  emit8(cc, 0x5A);
  emit8(cc, (uint8_t)(0xC0 | ((xmm_dst & 7) << 3) | (xmm_src & 7)));
}

/* CVTSD2SS xmm_dst, xmm_src — double to float (scalar, in XMM). */
static void emit_cvtsd2ss(cc_state_t *cc, int xmm_dst, int xmm_src) {
  emit8(cc, 0xF2);
  emit8(cc, 0x0F);
  emit8(cc, 0x5A);
  emit8(cc, (uint8_t)(0xC0 | ((xmm_dst & 7) << 3) | (xmm_src & 7)));
}

/* Error Handling */

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

static int cc_data_reserve(cc_state_t *cc, uint32_t bytes) {
  if (bytes > (CC_MAX_DATA - cc->data_pos)) {
    cc_error(cc, "data section overflow");
    return 0;
  }
  return 1;
}

/* Token Helpers */

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

static int cc_is_type(cc_token_type_t t) {
  return t == CC_TOK_INT || t == CC_TOK_CHAR || t == CC_TOK_VOID ||
         t == CC_TOK_U0 || t == CC_TOK_U8 || t == CC_TOK_U16 ||
         t == CC_TOK_U32 || t == CC_TOK_I8 || t == CC_TOK_I16 ||
         t == CC_TOK_I32 ||
         t == CC_TOK_FLOAT || t == CC_TOK_DOUBLE ||
         t == CC_TOK_FLOAT4 || t == CC_TOK_DOUBLE2 ||
         t == CC_TOK_STRUCT || t == CC_TOK_BOOL || t == CC_TOK_UNSIGNED ||
         t == CC_TOK_CONST || t == CC_TOK_VOLATILE || t == CC_TOK_REG ||
         t == CC_TOK_NOREG;
}

static cc_type_t cc_find_typedef(cc_state_t *cc, const char *name) {
  int i;
  for (i = 0; i < cc->typedef_count; i++) {
    if (strcmp(cc->typedef_names[i], name) == 0) {
      return cc->typedef_types[i];
    }
  }
  return (cc_type_t)-1;
}

static int cc_find_struct(cc_state_t *cc, const char *name);

static int cc_is_type_or_typedef(cc_state_t *cc, cc_token_t tok) {
  return cc_is_type(tok.type) ||
         (tok.type == CC_TOK_IDENT &&
          ((int)cc_find_typedef(cc, tok.text) >= 0 ||
           cc_find_struct(cc, tok.text) >= 0));
}

/* Track what kind of value the last expression produced */
static cc_type_t cc_last_expr_type;
static int cc_last_expr_struct_index; /* which struct, if TYPE_STRUCT */
static int cc_last_type_struct_index; /* set by cc_parse_type */
static int cc_last_expr_elem_size;    /* element size for array subscripts */

/* XMM register allocator for floating-point expression evaluation.
 * Reset at the start of each function. XMM0-7 available. Spilling (when
 * all 8 are in use) is not implemented — any expression too complex for
 * 8 XMMs will cc_error.  In the current Task-16 scheme only XMM0/XMM1
 * are actually used, but we keep the general allocator ready for later
 * phases (SIMD, libm). */
static uint8_t cc_xmm_inuse = 0;
/* Which XMM register holds the current FP expression result (mirrors EAX).
 * Generally XMM0 in this Task-16 implementation. Kept for Phase F. */
__attribute__((unused))
static int cc_last_xmm = 0;

/* These are currently unused in Task 16 (all FP ops run through XMM0/XMM1
 * with spill-to-stack) but exist for Phase F SIMD codegen. */
__attribute__((unused))
static int cc_xmm_alloc(cc_state_t *cc) {
  for (int i = 0; i < 8; i++) {
    if (!(cc_xmm_inuse & (1u << i))) {
      cc_xmm_inuse |= (uint8_t)(1u << i);
      return i;
    }
  }
  cc_error(cc, "out of XMM registers (expression too complex)");
  return 0;
}
__attribute__((unused))
static void cc_xmm_free(int i) {
  cc_xmm_inuse &= (uint8_t)~(1u << (i & 7));
}
static void cc_xmm_reset(void) {
  cc_xmm_inuse = 0;
  cc_last_xmm = 0;
}

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

static void cc_make_method_symbol(char *out, const char *class_name,
                                  const char *method_name) {
  int i = 0;
  int j = 0;
  while (class_name[i] && j < CC_MAX_IDENT - 1) {
    out[j++] = class_name[i++];
  }
  if (j < CC_MAX_IDENT - 1)
    out[j++] = '_';
  i = 0;
  while (method_name[i] && j < CC_MAX_IDENT - 1) {
    out[j++] = method_name[i++];
  }
  out[j] = '\0';
}

static cc_type_t cc_parse_type(cc_state_t *cc) {
  cc_token_t tok = cc_next(cc);
  cc_type_t base;
  cc_last_type_struct_index = -1;

  /* Strip qualifiers: const, unsigned, volatile (order-agnostic). */
    while (tok.type == CC_TOK_CONST || tok.type == CC_TOK_UNSIGNED ||
      tok.type == CC_TOK_VOLATILE || tok.type == CC_TOK_REG ||
      tok.type == CC_TOK_NOREG) {
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
  case CC_TOK_U0:
    base = TYPE_U0;
    break;
  case CC_TOK_U8:
    base = TYPE_U8;
    break;
  case CC_TOK_U16:
    base = TYPE_U16;
    break;
  case CC_TOK_U32:
    base = TYPE_U32;
    break;
  case CC_TOK_I8:
    base = TYPE_I8;
    break;
  case CC_TOK_I16:
    base = TYPE_I16;
    break;
  case CC_TOK_I32:
    base = TYPE_I32;
    break;
  case CC_TOK_BOOL:
    base = TYPE_BOOL;
    break; /* Bool/bool is alias for int */
  case CC_TOK_FLOAT:
    base = TYPE_FLOAT;
    break;
  case CC_TOK_DOUBLE:
    base = TYPE_DOUBLE;
    break;
  case CC_TOK_FLOAT4:
    base = TYPE_FLOAT4;
    break;
  case CC_TOK_DOUBLE2:
    base = TYPE_DOUBLE2;
    break;
  case CC_TOK_IDENT: {
    /* Check if this is a typedef alias */
    cc_type_t td = cc_find_typedef(cc, tok.text);
    if ((int)td >= 0) {
      base = td;
      break;
    }
    {
      int si = cc_find_struct(cc, tok.text);
      if (si >= 0) {
        cc_last_type_struct_index = si;
        base = TYPE_STRUCT;
        break;
      }
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
      cc_peek(cc).type == CC_TOK_VOLATILE ||
      cc_peek(cc).type == CC_TOK_REG ||
      cc_peek(cc).type == CC_TOK_NOREG)
    cc_next(cc);

  /* Pointer depth support: T*, T**, ... */
  int pointer_depth = 0;
  while (cc_peek(cc).type == CC_TOK_STAR) {
    cc_next(cc);
    pointer_depth++;
    /* Ignore pointer qualifiers: char *const, char *const * ... */
        while (cc_peek(cc).type == CC_TOK_CONST ||
          cc_peek(cc).type == CC_TOK_UNSIGNED ||
          cc_peek(cc).type == CC_TOK_VOLATILE ||
          cc_peek(cc).type == CC_TOK_REG ||
          cc_peek(cc).type == CC_TOK_NOREG) {
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
  case TYPE_FLOAT:
    return 4;
  case TYPE_DOUBLE:
    return 8;
  case TYPE_FLOAT4:
    return 16;
  case TYPE_DOUBLE2:
    return 16;
  default:
    return 4;
  }
}

/* Promote binary-op types per FP hierarchy. Rules:
 *  - SIMD (float4/double2) must match exactly on both sides.
 *  - double > float > int for scalar ops.
 *  - int + float -> float (int promoted via CVTSI2SS).
 *  - float + double -> double.
 *  - pointer arithmetic stays int-only.
 */
static cc_type_t cc_promote(cc_state_t *cc, cc_type_t a, cc_type_t b) {
  /* Reject scalar-with-SIMD mixing */
  if (a == TYPE_FLOAT4 || b == TYPE_FLOAT4) {
    if (a != b)
      cc_error(cc, "mixing float4 with non-float4");
    return TYPE_FLOAT4;
  }
  if (a == TYPE_DOUBLE2 || b == TYPE_DOUBLE2) {
    if (a != b)
      cc_error(cc, "mixing double2 with non-double2");
    return TYPE_DOUBLE2;
  }
  /* Scalar FP hierarchy */
  if (a == TYPE_DOUBLE || b == TYPE_DOUBLE)
    return TYPE_DOUBLE;
  if (a == TYPE_FLOAT || b == TYPE_FLOAT)
    return TYPE_FLOAT;
  /* Otherwise keep existing int behavior */
  return a;
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

/* Symbol Table */

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

/* Forward Declarations for Parser */

static void cc_parse_statement(cc_state_t *cc);
static void cc_parse_block(cc_state_t *cc);
static void cc_parse_expression(cc_state_t *cc, int min_prec);
static void cc_parse_primary(cc_state_t *cc);

static int cc_is_prescan_type_token(cc_token_type_t t) {
  return t == CC_TOK_INT || t == CC_TOK_CHAR || t == CC_TOK_VOID ||
         t == CC_TOK_U0 || t == CC_TOK_U8 || t == CC_TOK_U16 ||
         t == CC_TOK_U32 || t == CC_TOK_I8 || t == CC_TOK_I16 ||
         t == CC_TOK_I32 || t == CC_TOK_FLOAT || t == CC_TOK_DOUBLE ||
         t == CC_TOK_FLOAT4 || t == CC_TOK_DOUBLE2 ||
         t == CC_TOK_BOOL || t == CC_TOK_STRUCT;
}

static void cc_prescan_add_function(cc_state_t *cc, const char *name) {
  cc_symbol_t *sym = cc_sym_find(cc, name);
  if (!sym) {
    sym = cc_sym_add(cc, name, SYM_FUNC, TYPE_INT);
  }
  if (sym && sym->kind != SYM_KERNEL) {
    sym->kind = SYM_FUNC;
    if (!sym->is_defined) {
      sym->offset = 0;
      sym->address = 0;
      sym->param_count = 0;
    }
  }
}

static void cc_prescan_functions(cc_state_t *cc) {
  int saved_pos = cc->pos;
  int saved_line = cc->line;
  int saved_has_peek = cc->has_peek;
  cc_token_t saved_peek = cc->peek_buf;
  cc_token_t saved_cur = cc->cur;

  cc->pos = 0;
  cc->line = 1;
  cc->has_peek = 0;

  int brace_depth = 0;

  while (!cc->error) {
    cc_token_t tok = cc_lex_next(cc);
    if (tok.type == CC_TOK_EOF || tok.type == CC_TOK_ERROR)
      break;

    if (tok.type == CC_TOK_LBRACE) {
      brace_depth++;
      continue;
    }
    if (tok.type == CC_TOK_RBRACE) {
      if (brace_depth > 0)
        brace_depth--;
      continue;
    }
    if (brace_depth != 0)
      continue;

    if (tok.type == CC_TOK_STATIC || tok.type == CC_TOK_CONST ||
        tok.type == CC_TOK_UNSIGNED || tok.type == CC_TOK_VOLATILE) {
      tok = cc_lex_next(cc);
    }
    while (tok.type == CC_TOK_CONST || tok.type == CC_TOK_UNSIGNED ||
           tok.type == CC_TOK_VOLATILE) {
      tok = cc_lex_next(cc);
    }

    if (!cc_is_prescan_type_token(tok.type))
      continue;

    if (tok.type == CC_TOK_STRUCT) {
      cc_token_t sname = cc_lex_next(cc);
      if (sname.type != CC_TOK_IDENT)
        continue;
      tok = cc_lex_next(cc);
    } else {
      tok = cc_lex_next(cc);
    }

    while (tok.type == CC_TOK_STAR) {
      tok = cc_lex_next(cc);
    }

    if (tok.type != CC_TOK_IDENT)
      continue;
    char fname[CC_MAX_IDENT];
    int fi = 0;
    while (tok.text[fi] && fi < CC_MAX_IDENT - 1) {
      fname[fi] = tok.text[fi];
      fi++;
    }
    fname[fi] = '\0';

    tok = cc_lex_next(cc);
    if (tok.type != CC_TOK_LPAREN)
      continue;

    cc_prescan_add_function(cc, fname);

    int paren_depth = 1;
    while (paren_depth > 0) {
      tok = cc_lex_next(cc);
      if (tok.type == CC_TOK_EOF || tok.type == CC_TOK_ERROR)
        break;
      if (tok.type == CC_TOK_LPAREN)
        paren_depth++;
      else if (tok.type == CC_TOK_RPAREN)
        paren_depth--;
    }
  }

  cc->pos = saved_pos;
  cc->line = saved_line;
  cc->has_peek = saved_has_peek;
  cc->peek_buf = saved_peek;
  cc->cur = saved_cur;
}

/* Expression Types for Tracking */

/* Track what kind of value the last expression produced -
 * (primary statics declared above, before cc_parse_type) */

/* Operator Precedence */

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

/* Expression Parsing */

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

/* ══════════════════════════════════════════════════════════════════════
 *  Task 32: SSE packed intrinsics (_mm_*_ps)
 *
 *  Recognized by identifier at call-expression parse time and inlined
 *  as a single SSE instruction (no function-call overhead). See
 *  kernel/simd_intrin.h for the user-facing declarations.
 * ══════════════════════════════════════════════════════════════════════ */

/* Flag bits for cc_intrin_t.flags */
#define CC_INTR_COMMUT    0x01  /* op is commutative: xmm0 <op>= xmm1      */
#define CC_INTR_SWAP      0x02  /* swap operands before emitting (gt/ge)    */
#define CC_INTR_RET_INT   0x04  /* result type is int (movemask)           */
#define CC_INTR_SET1      0x08  /* _mm_set1_{ps,pd}: scalar broadcast       */
#define CC_INTR_MOVEMASK  0x10  /* _mm_movemask_ps: MOVMSKPS xmm->EAX        */
#define CC_INTR_PD        0x20  /* Task 33: double-precision packed (double2) */

typedef struct {
  const char *name;
  uint8_t prefix;   /* 0x66, 0xF3, 0xF2, or 0x00 (none) */
  uint8_t opcode;   /* primary SSE opcode after 0x0F    */
  uint8_t arity;    /* 1 = unary (sqrt/set1/movemask), 2 = binary */
  int8_t  imm8;     /* -1 = no imm; 0..7 = CMPPS predicate to append */
  uint8_t flags;    /* CC_INTR_* bitmask                */
} cc_intrin_t;

static const cc_intrin_t cc_intrin_table[] = {
    /* Arithmetic (ADDPS/SUBPS/MULPS/DIVPS/MINPS/MAXPS) — all 0x0F xx.
     * ADDPS/MULPS/MINPS/MAXPS are commutative; SUBPS/DIVPS are not. */
    { "_mm_add_ps",    0x00, 0x58, 2, -1, CC_INTR_COMMUT },
    { "_mm_sub_ps",    0x00, 0x5C, 2, -1, 0 },
    { "_mm_mul_ps",    0x00, 0x59, 2, -1, CC_INTR_COMMUT },
    { "_mm_div_ps",    0x00, 0x5E, 2, -1, 0 },
    { "_mm_min_ps",    0x00, 0x5D, 2, -1, CC_INTR_COMMUT },
    { "_mm_max_ps",    0x00, 0x5F, 2, -1, CC_INTR_COMMUT },
    { "_mm_sqrt_ps",   0x00, 0x51, 1, -1, 0 },

    /* Bitwise (ANDPS/ORPS/XORPS) — all commutative. */
    { "_mm_and_ps",    0x00, 0x54, 2, -1, CC_INTR_COMMUT },
    { "_mm_or_ps",     0x00, 0x56, 2, -1, CC_INTR_COMMUT },
    { "_mm_xor_ps",    0x00, 0x57, 2, -1, CC_INTR_COMMUT },

    /* Compare (CMPPS 0x0F C2 /r ib). Predicates:
     *   0=eq, 1=lt, 2=le, 3=unord, 4=neq, 5=nlt, 6=nle, 7=ord.
     * Commutative in the sense that operand ordering doesn't change the
     * lane-wise result for eq/neq.  cmpgt/cmpge are synthesised by
     * swapping operands and reusing cmplt/cmple. */
    { "_mm_cmpeq_ps",  0x00, 0xC2, 2, 0, CC_INTR_COMMUT },
    { "_mm_cmplt_ps",  0x00, 0xC2, 2, 1, 0 },
    { "_mm_cmple_ps",  0x00, 0xC2, 2, 2, 0 },
    { "_mm_cmpneq_ps", 0x00, 0xC2, 2, 4, CC_INTR_COMMUT },
    { "_mm_cmpgt_ps",  0x00, 0xC2, 2, 1, CC_INTR_SWAP },
    { "_mm_cmpge_ps",  0x00, 0xC2, 2, 2, CC_INTR_SWAP },

    /* Broadcast + movemask (special codegen paths). */
    { "_mm_set1_ps",     0x00, 0x00, 1, -1, CC_INTR_SET1 },
    { "_mm_movemask_ps", 0x00, 0x50, 1, -1, CC_INTR_MOVEMASK | CC_INTR_RET_INT },

    /* Task 33: double-precision packed counterparts.
     * Same opcodes as the _ps ops but with a 0x66 operand-size prefix.
     * Arg and result type is double2 (two 64-bit lanes). */
    { "_mm_add_pd",    0x66, 0x58, 2, -1, CC_INTR_COMMUT | CC_INTR_PD },
    { "_mm_sub_pd",    0x66, 0x5C, 2, -1, CC_INTR_PD },
    { "_mm_mul_pd",    0x66, 0x59, 2, -1, CC_INTR_COMMUT | CC_INTR_PD },
    { "_mm_div_pd",    0x66, 0x5E, 2, -1, CC_INTR_PD },
    { "_mm_sqrt_pd",   0x66, 0x51, 1, -1, CC_INTR_PD },
    { "_mm_min_pd",    0x66, 0x5D, 2, -1, CC_INTR_COMMUT | CC_INTR_PD },
    { "_mm_max_pd",    0x66, 0x5F, 2, -1, CC_INTR_COMMUT | CC_INTR_PD },
    /* Bitwise double-precision (ANDPD/ORPD/XORPD share opcodes with _ps
     * variants; the 0x66 prefix selects the pd form). */
    { "_mm_and_pd",    0x66, 0x54, 2, -1, CC_INTR_COMMUT | CC_INTR_PD },
    { "_mm_or_pd",     0x66, 0x56, 2, -1, CC_INTR_COMMUT | CC_INTR_PD },
    { "_mm_xor_pd",    0x66, 0x57, 2, -1, CC_INTR_COMMUT | CC_INTR_PD },
    /* Broadcast: scalar double into both lanes via SHUFPD xmm0,xmm0,0. */
    { "_mm_set1_pd",   0x66, 0x00, 1, -1, CC_INTR_SET1 | CC_INTR_PD },

    { NULL, 0, 0, 0, 0, 0 }
};

/* Look up name in the intrinsic table.  Returns NULL if not an intrinsic.
 * Requires the name to start with "_mm_" to keep the hot path cheap. */
static const cc_intrin_t *cc_intrin_lookup(const char *name) {
  if (name[0] != '_' || name[1] != 'm' || name[2] != 'm' || name[3] != '_')
    return NULL;
  for (int idx = 0; cc_intrin_table[idx].name; idx++) {
    if (strcmp(cc_intrin_table[idx].name, name) == 0)
      return &cc_intrin_table[idx];
  }
  return NULL;
}

/* Spill XMM0 into 16 bytes at [ESP].  Caller must pair with xmm_restore. */
static void cc_intr_spill_xmm0(cc_state_t *cc) {
  /* sub esp, 16 */
  emit8(cc, 0x83); emit8(cc, 0xEC); emit8(cc, 16);
  /* movups [esp], xmm0 : 0F 11 04 24 */
  emit8(cc, 0x0F); emit8(cc, 0x11); emit8(cc, 0x04); emit8(cc, 0x24);
}

/* Restore spilled 16 bytes from [ESP] into XMM1 and release the slot. */
static void cc_intr_restore_xmm1(cc_state_t *cc) {
  /* movups xmm1, [esp] : 0F 10 0C 24 */
  emit8(cc, 0x0F); emit8(cc, 0x10); emit8(cc, 0x0C); emit8(cc, 0x24);
  /* add esp, 16 */
  emit8(cc, 0x83); emit8(cc, 0xC4); emit8(cc, 16);
}

/* Emit a two-register SSE op with ModR/M mod=11, reg=dst, r/m=src. */
static void cc_intr_emit_op_rr(cc_state_t *cc, uint8_t prefix, uint8_t opcode,
                               int dst, int src, int imm8) {
  if (prefix) emit8(cc, prefix);
  emit8(cc, 0x0F);
  emit8(cc, opcode);
  emit8(cc, (uint8_t)(0xC0 | ((dst & 7) << 3) | (src & 7)));
  if (imm8 >= 0)
    emit8(cc, (uint8_t)imm8);
}

/* Parse + emit a recognised intrinsic call.  The caller has already
 * consumed the identifier and the opening '('.  This function parses
 * the argument list up to and including ')', emits the correct SSE
 * bytes, and sets cc_last_expr_type / cc_last_xmm appropriately.
 *
 * Calling convention inside the intrinsic body:
 *   - Each argument evaluated via cc_parse_expression leaves its
 *     float4 result in XMM0 (see cc_parse_ident_expr / variable load).
 *   - Two-arg intrinsics spill arg0 onto 16 bytes of stack, evaluate
 *     arg1 into XMM0, and reload arg0 into XMM1. Result lands in XMM0.
 */
static void cc_emit_intrinsic(cc_state_t *cc, const cc_intrin_t *intr) {
  /* Parse arg 0. */
  cc_parse_expression(cc, 1);
  if (cc->error) return;

  /* Task 33: pd intrinsics carry CC_INTR_PD and return double2; _ps
   * intrinsics return float4. Movemask is handled explicitly below. */
  int is_pd = (intr->flags & CC_INTR_PD) != 0;
  cc_type_t vec_type = is_pd ? TYPE_DOUBLE2 : TYPE_FLOAT4;

  /* _mm_set1_{ps,pd} takes a scalar float/int/double and broadcasts;
   * everything else takes a vector (float4/double2) first argument. */
  if (intr->flags & CC_INTR_SET1) {
    if (is_pd) {
      /* _mm_set1_pd — broadcast a scalar double into both 64-bit lanes. */
      if (cc_last_expr_type == TYPE_INT) {
        /* Promote int to double via CVTSI2SD xmm0, eax. */
        emit_cvtsi2sd(cc, 0);
        cc_last_expr_type = TYPE_DOUBLE;
      }
      if (cc_last_expr_type == TYPE_FLOAT) {
        /* Widen float to double: CVTSS2SD xmm0, xmm0. */
        emit_cvtss2sd(cc, 0, 0);
        cc_last_expr_type = TYPE_DOUBLE;
      }
      if (cc_last_expr_type != TYPE_DOUBLE) {
        cc_error(cc, "_mm_set1_pd requires a double/float/int scalar argument");
        return;
      }
      /* SHUFPD xmm0, xmm0, 0x00 : 66 0F C6 C0 00 — imm8=0 replicates the
       * low 64-bit lane into both slots. */
      emit8(cc, 0x66);
      emit8(cc, 0x0F);
      emit8(cc, 0xC6); /* SHUFPD */
      emit8(cc, 0xC0); /* mod=11, reg=xmm0, r/m=xmm0 */
      emit8(cc, 0x00);
      cc_expect(cc, CC_TOK_RPAREN);
      cc_last_expr_type = TYPE_DOUBLE2;
      cc_last_xmm = 0;
      return;
    }
    /* _mm_set1_ps */
    if (cc_last_expr_type == TYPE_INT) {
      /* Promote int to float via CVTSI2SS xmm0, eax (Task 16 helper). */
      emit_cvtsi2ss(cc, 0);
      cc_last_expr_type = TYPE_FLOAT;
    }
    if (cc_last_expr_type != TYPE_FLOAT && cc_last_expr_type != TYPE_DOUBLE) {
      cc_error(cc, "_mm_set1_ps requires a float/int scalar argument");
      return;
    }
    if (cc_last_expr_type == TYPE_DOUBLE) {
      /* Narrow double->float: CVTSD2SS xmm0, xmm0 */
      emit_cvtsd2ss(cc, 0, 0);
    }
    /* Broadcast lane 0 to all four lanes: SHUFPS xmm0, xmm0, 0x00
     * (imm=0 replicates lane 0 into all four 32-bit slots). */
    emit8(cc, 0x0F);
    emit8(cc, 0xC6); /* SHUFPS */
    emit8(cc, 0xC0); /* mod=11, reg=xmm0, r/m=xmm0 */
    emit8(cc, 0x00);
    cc_expect(cc, CC_TOK_RPAREN);
    cc_last_expr_type = TYPE_FLOAT4;
    cc_last_xmm = 0;
    return;
  }

  /* All other intrinsics expect a SIMD vector first argument matching
   * their precision (float4 for _ps, double2 for _pd). */
  if (cc_last_expr_type != vec_type) {
    cc_error(cc, is_pd ? "_mm_*_pd intrinsic expects a double2 argument"
                       : "_mm_*_ps intrinsic expects a float4 argument");
    return;
  }

  if (intr->arity == 1) {
    /* Unary: XMM0 = OP(XMM0).  Covers sqrt_ps/pd and movemask_ps. */
    if (intr->flags & CC_INTR_MOVEMASK) {
      /* MOVMSKPS eax, xmm0 : 0F 50 /r, reg=EAX=0, r/m=xmm0=0 -> 0xC0.
       * Result is a 4-bit sign mask in EAX — type becomes int. */
      emit8(cc, 0x0F);
      emit8(cc, 0x50);
      emit8(cc, 0xC0);
      cc_expect(cc, CC_TOK_RPAREN);
      cc_last_expr_type = TYPE_INT;
      return;
    }
    /* SQRTPS xmm0, xmm0 : 0F 51 C0   (or SQRTPD: 66 0F 51 C0) */
    cc_intr_emit_op_rr(cc, intr->prefix, intr->opcode, 0, 0, intr->imm8);
    cc_expect(cc, CC_TOK_RPAREN);
    cc_last_expr_type = vec_type;
    cc_last_xmm = 0;
    return;
  }

  /* Binary: spill arg0 (in XMM0), parse arg1, reload into XMM1. */
  cc_intr_spill_xmm0(cc);
  cc_expect(cc, CC_TOK_COMMA);
  cc_parse_expression(cc, 1);
  if (cc->error) return;
  if (cc_last_expr_type != vec_type) {
    cc_error(cc, is_pd ? "_mm_*_pd intrinsic second arg must be double2"
                       : "_mm_*_ps intrinsic second arg must be float4");
    return;
  }
  cc_intr_restore_xmm1(cc);
  /* Now XMM0 = arg1, XMM1 = arg0. */

  int dst, src;
  if (intr->flags & CC_INTR_COMMUT) {
    /* Commutative: xmm0 <op>= xmm1 — result directly in XMM0. */
    dst = 0;
    src = 1;
  } else if (intr->flags & CC_INTR_SWAP) {
    /* cmpgt(a,b) == cmplt(b,a): XMM1 (old arg0=a) vs XMM0 (arg1=b).
     * After swap we want the "a vs b" semantics mapped onto cmplt of
     * b and a, so compute (XMM0 op XMM1) directly and leave result
     * in XMM0.  That is: XMM0 op= XMM1 using the base opcode. */
    dst = 0;
    src = 1;
  } else {
    /* Non-commutative (sub, div, cmplt, cmple, cmpneq-strict).
     * _mm_sub_ps(a, b) = a - b.  XMM1 holds a, XMM0 holds b. So
     * we compute XMM1 <op>= XMM0 (writing into XMM1), then move
     * XMM1 into XMM0 via MOVAPS so callers see the usual
     * XMM0-accumulator convention. */
    dst = 1;
    src = 0;
  }

  cc_intr_emit_op_rr(cc, intr->prefix, intr->opcode, dst, src, intr->imm8);

  if (dst == 1) {
    /* MOVAPS xmm0, xmm1 : 0F 28 C1 (ModR/M mod=11, reg=xmm0=0, r/m=xmm1=1). */
    emit8(cc, 0x0F);
    emit8(cc, 0x28);
    emit8(cc, 0xC1);
  }

  cc_expect(cc, CC_TOK_RPAREN);
  cc_last_expr_type = (intr->flags & CC_INTR_RET_INT) ? TYPE_INT : vec_type;
  if (cc_last_expr_type != TYPE_INT)
    cc_last_xmm = 0;
}

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
    /* Task 32: short-circuit recognised SSE intrinsics (`_mm_*_ps`).
     * These inline as a single SSE opcode instead of a call.  Keep this
     * check before any argument parsing so we don't push-then-discard. */
    const cc_intrin_t *intr = cc_intrin_lookup(name);
    if (intr) {
      cc_next(cc); /* consume '(' */
      cc_emit_intrinsic(cc, intr);
      return;
    }
    cc_next(cc); /* consume '(' */

    /* Count and push arguments (right to left by collecting first) */
    uint32_t arg_addrs[CC_MAX_PARAMS];
    int argc = 0;
    /* Task 18: track size (4 or 8 bytes) of each pushed arg so we can do
     * a size-aware reversal and emit the correct cleanup ADD ESP. */
    int arg_sizes[CC_MAX_PARAMS];
    int total_arg_bytes = 0;

    if (cc_peek(cc).type != CC_TOK_RPAREN) {
      /* Parse first argument */
      cc_parse_expression(cc, 1);
      if (cc_last_expr_type == TYPE_FLOAT) {
        emit_push_xmm_float(cc, 0);
        arg_sizes[argc] = 4;
      } else if (cc_last_expr_type == TYPE_DOUBLE) {
        emit_push_xmm_double(cc, 0);
        arg_sizes[argc] = 8;
      } else {
        emit_push_eax(cc);
        arg_sizes[argc] = 4;
      }
      total_arg_bytes += arg_sizes[argc];
      argc++;

      while (cc_match(cc, CC_TOK_COMMA)) {
        cc_parse_expression(cc, 1);
        if (argc >= CC_MAX_PARAMS) {
          cc_error(cc, "too many call arguments");
          break;
        }
        if (cc_last_expr_type == TYPE_FLOAT) {
          emit_push_xmm_float(cc, 0);
          arg_sizes[argc] = 4;
        } else if (cc_last_expr_type == TYPE_DOUBLE) {
          emit_push_xmm_double(cc, 0);
          arg_sizes[argc] = 8;
        } else {
          emit_push_eax(cc);
          arg_sizes[argc] = 4;
        }
        total_arg_bytes += arg_sizes[argc];
        argc++;
      }
    }
    cc_expect(cc, CC_TOK_RPAREN);

    /* Reverse args on stack for cdecl (we pushed left-to-right, need
     * right-to-left).  With all-4-byte args (int/float) we can swap
     * pairs of 4-byte slots directly.  With 8-byte args (doubles) we
     * need to swap pairs whose total byte layout matches.
     *
     * Layout after left-to-right push (low addr = top of stack):
     *    [arg_{argc-1}] [arg_{argc-2}] ... [arg_1] [arg_0]
     * Target cdecl layout (low addr first):
     *    [arg_0] [arg_1] ... [arg_{argc-1}]
     *
     * For each pair (a, b) where a < b = argc-1-a, we swap the bytes
     * belonging to arg_a and arg_b.  The 4-byte same-size fast path
     * handles both int-only and float-only calls (Task 19 uses floats).
     * Doubles and mixed sizes are handled by a size-aware swap. */
    if (argc > 1) {
      /* Compute byte-offset (from current ESP) where arg_i lives after
       * left-to-right push.  arg at index i is pushed i-th, so its
       * bytes end up at [prefix(i+1) .. total) counting from the end
       * (high addr).  From ESP (low addr), arg_i's low byte is at:
       *    src_off[i] = total - sum(sizes[0..i]) - sizes[i]
       * Equivalently: src_off[i] = sum(sizes[i+1..argc-1]).
       * The target layout has arg_i at dst_off[i] = sum(sizes[0..i-1]). */
      int src_off[CC_MAX_PARAMS];
      {
        int running = 0;
        for (int k = argc - 1; k >= 0; k--) {
          src_off[k] = running;
          running += arg_sizes[k];
        }
      }

      for (int a = 0; a < argc / 2; a++) {
        int b = argc - 1 - a;
        int sa = arg_sizes[a];
        int sb = arg_sizes[b];
        int off_a = src_off[a];
        int off_b = src_off[b];

        if (sa == 4 && sb == 4) {
          /* 4-byte swap via ECX/EDX (original Task 1-era fast path). */
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
        } else if (sa == 8 && sb == 8) {
          /* 8-byte swap: two 4-byte swaps of adjacent dwords.
           * Slot a occupies [off_a, off_a+4), slot b occupies
           * [off_b, off_b+4) in the same pattern. */
          for (int d = 0; d < 2; d++) {
            int oa = off_a + d * 4;
            int ob = off_b + d * 4;
            emit8(cc, 0x8B); emit8(cc, 0x8C); emit8(cc, 0x24);
            emit32(cc, (uint32_t)oa); /* mov ecx, [esp+oa] */
            emit8(cc, 0x8B); emit8(cc, 0x94); emit8(cc, 0x24);
            emit32(cc, (uint32_t)ob); /* mov edx, [esp+ob] */
            emit8(cc, 0x89); emit8(cc, 0x94); emit8(cc, 0x24);
            emit32(cc, (uint32_t)oa); /* mov [esp+oa], edx */
            emit8(cc, 0x89); emit8(cc, 0x8C); emit8(cc, 0x24);
            emit32(cc, (uint32_t)ob); /* mov [esp+ob], ecx */
          }
        } else {
          /* Mixed-size pair (e.g. f(double, int)).  A correct swap
           * requires a variable-width block reverse.  Task 18 punts
           * on this rare case until a clear use case appears. */
          cc_error(cc,
                   "mixed int/double args in same call not yet supported");
          break;
        }
      }
    }
    (void)arg_addrs;

    /* Builtins: Print(fmt, ...) and PrintLine(fmt, ...) */
    if (strcmp(name, "Print") == 0 || strcmp(name, "PrintLine") == 0) {
      if (argc <= 0) {
        cc_error(cc, "Print/PrintLine require at least a format argument");
        return;
      }

      cc_symbol_t *printf_sym =
          cc_sym_find(cc, (strcmp(name, "Print") == 0) ? "__cc_Print"
                                                       : "__cc_PrintLine");
      if (!printf_sym || printf_sym->kind != SYM_KERNEL) {
        cc_error(cc, "Print builtin binding missing");
        return;
      }

      emit_call_abs(cc, printf_sym->address);
      emit_add_esp(cc, (int32_t)total_arg_bytes);

      cc_last_expr_type = TYPE_VOID;
      return;
    }

    /* Look up function */
    cc_symbol_t *sym = cc_sym_find(cc, name);
    /* Task 18: remember callee's return type so we can set cc_last_expr_type
     * correctly after cleanup (default is TYPE_INT for unknown/forward refs). */
    cc_type_t call_ret_type = TYPE_INT;
    if (sym && (sym->kind == SYM_FUNC || sym->kind == SYM_KERNEL)) {
      call_ret_type = sym->type;
    }
    if (sym) {
      if (sym->kind == SYM_KERNEL) {
        emit_call_abs(cc, sym->address);
      } else if (sym->kind == SYM_FUNC) {
        if (sym->is_defined) {
          /* Direct call to known address */
          uint32_t target = cc->code_base + (uint32_t)sym->offset;
          emit_call_abs(cc, target);
        } else {
          /* Forward reference - add patch */
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
      /* Unknown function - create forward ref */
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

    /* Clean up arguments.  Task 18: use total_arg_bytes instead of
     * argc*4 so that doubles (8 bytes) are correctly cleaned up. */
    if (total_arg_bytes > 0) {
      emit_add_esp(cc, (int32_t)total_arg_bytes);
    }

    if (strcmp(name, "print") == 0 || strcmp(name, "println") == 0) {
      cc_last_expr_type = TYPE_VOID;
    } else {
      /* Task 18: if callee returns float/double, result lives in XMM0;
       * otherwise it's in EAX. */
      cc_last_expr_type = call_ret_type;
      if (call_ret_type == TYPE_FLOAT || call_ret_type == TYPE_DOUBLE) {
        cc_last_xmm = 0;
      }
    }
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
    } else if (sym->type == TYPE_FLOAT) {
      /* Task 16: load float local into XMM0. */
      emit_movss_xmm_local(cc, 0, sym->offset);
      cc_last_xmm = 0;
    } else if (sym->type == TYPE_DOUBLE) {
      emit_movsd_xmm_local(cc, 0, sym->offset);
      cc_last_xmm = 0;
    } else if (sym->type == TYPE_FLOAT4 || sym->type == TYPE_DOUBLE2) {
      /* Task 30/31: load 16-byte SIMD local into XMM0 via MOVUPS.
       * (MOVUPS instead of MOVAPS because EBP-relative offsets aren't
       * guaranteed 16-aligned under Task 18's prologue — see
       * emit_movups_xmm_local comment.)
       *
       * Task 31: if the next token is '.', extract a scalar element
       * (.x/.y/.z/.w for float4, .x/.y for double2) and leave that
       * scalar in XMM0's low lane.  SHUFPS imm8 = lane*0x55 broadcasts
       * a given 32-bit lane into position 0 (and the other three, but
       * scalar math only reads the low lane).  SHUFPD imm8 = 0x01
       * swaps the two 64-bit lanes of double2. */
      emit_movups_xmm_local(cc, 0, sym->offset);
      cc_last_xmm = 0;
      cc_last_expr_type = sym->type;
      cc_last_expr_struct_index = sym->struct_index;
      cc_last_expr_elem_size = 16;

      if (cc_peek(cc).type == CC_TOK_DOT) {
        cc_next(cc); /* consume '.' */
        cc_token_t ftok = cc_next(cc);
        if (ftok.type != CC_TOK_IDENT) {
          cc_error(cc, "expected field name after '.'");
          return;
        }
        char field = ftok.text[0];
        /* Ensure single-character field name. */
        if (field == '\0' || ftok.text[1] != '\0') {
          cc_error(cc, "invalid SIMD field name");
          return;
        }
        if (sym->type == TYPE_FLOAT4) {
          int lane;
          if (field == 'x') lane = 0;
          else if (field == 'y') lane = 1;
          else if (field == 'z') lane = 2;
          else if (field == 'w') lane = 3;
          else {
            cc_error(cc, "float4 field must be .x/.y/.z/.w");
            return;
          }
          if (lane != 0) {
            /* SHUFPS xmm0, xmm0, imm8 — broadcast lane `lane` to all four
             * 32-bit slots of XMM0 so the scalar result sits in the low
             * lane. */
            uint8_t imm = (uint8_t)(lane | (lane << 2) |
                                    (lane << 4) | (lane << 6));
            emit8(cc, 0x0F);
            emit8(cc, 0xC6); /* SHUFPS */
            emit8(cc, 0xC0); /* mod=11, reg=xmm0, r/m=xmm0 */
            emit8(cc, imm);
          }
          cc_last_expr_type = TYPE_FLOAT;
          cc_last_expr_struct_index = -1;
          cc_last_expr_elem_size = 4;
          cc_last_xmm = 0;
        } else { /* TYPE_DOUBLE2 */
          if (field == 'x') {
            /* lane 0 already in the low 8 bytes of XMM0 — no-op. */
          } else if (field == 'y') {
            /* SHUFPD xmm0, xmm0, 0x01 — imm8 bit 0 selects src high
             * lane for dst low lane, so lane 1 ends up in the low
             * 8 bytes of XMM0 where scalar double math reads it. */
            emit8(cc, 0x66);
            emit8(cc, 0x0F);
            emit8(cc, 0xC6); /* SHUFPD */
            emit8(cc, 0xC0); /* mod=11, reg=xmm0, r/m=xmm0 */
            emit8(cc, 0x01);
          } else {
            cc_error(cc, "double2 field must be .x or .y");
            return;
          }
          cc_last_expr_type = TYPE_DOUBLE;
          cc_last_expr_struct_index = -1;
          cc_last_expr_elem_size = 8;
          cc_last_xmm = 0;
        }
      }
      return;
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
    } else if (sym->type == TYPE_FLOAT) {
      emit_movss_xmm_disp32(cc, 0, sym->address);
      cc_last_xmm = 0;
    } else if (sym->type == TYPE_DOUBLE) {
      emit_movsd_xmm_disp32(cc, 0, sym->address);
      cc_last_xmm = 0;
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
    cc_last_expr_type = TYPE_FUNC_PTR;
  } else if (sym->kind == SYM_KERNEL) {
    emit_mov_eax_imm(cc, sym->address);
    cc_last_expr_type = TYPE_FUNC_PTR;
  }
}

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

  case CC_TOK_FLIT: {
    /* Task 16: emit the raw bits into the data segment and load them
     * into XMM0 via MOVSS (float) or MOVSD (double).  XMM0 is the
     * "FP accumulator" mirroring EAX for the integer path. */
    if (tok.flit_bits == 32) {
      float f = (float)tok.fval;
      uint8_t bytes[4];
      memcpy(bytes, &f, 4);
      uint32_t addr = cc_emit_data_bytes(cc, bytes, 4);
      emit_movss_xmm_disp32(cc, 0, addr);
      cc_last_expr_type = TYPE_FLOAT;
    } else {
      double d = tok.fval;
      uint8_t bytes[8];
      memcpy(bytes, &d, 8);
      uint32_t addr = cc_emit_data_bytes(cc, bytes, 8);
      emit_movsd_xmm_disp32(cc, 0, addr);
      cc_last_expr_type = TYPE_DOUBLE;
    }
    cc_last_xmm = 0;
    break;
  }

  case CC_TOK_CHAR_LIT:
    emit_mov_eax_imm(cc, (uint32_t)tok.int_value);
    cc_last_expr_type = TYPE_CHAR;
    break;

  case CC_TOK_STRING: {
    /* Store string in data section, load address */
    int slen = 0;
    while (tok.text[slen])
      slen++;
    if (!cc_data_reserve(cc, (uint32_t)(slen + 1))) {
      return;
    }
    uint32_t str_addr = cc->data_base + cc->data_pos;
    int si = 0;
    while (tok.text[si]) {
      cc->data[cc->data_pos++] = (uint8_t)tok.text[si++];
    }
    cc->data[cc->data_pos++] = 0; /* null terminator */
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
      cc_type_t src_type = cc_last_expr_type;
      /* Task 17: int <-> float <-> double explicit casts.
       * The six combinations among {TYPE_INT, TYPE_FLOAT, TYPE_DOUBLE}
       * each lower to a single CVT* opcode.  Casts to/from pointer,
       * char, struct, or SIMD types remain pure retagging (no code). */
      if (src_type != cast_type) {
        if (src_type == TYPE_INT && cast_type == TYPE_FLOAT) {
          /* int in EAX -> float in XMM0 */
          emit_cvtsi2ss(cc, 0);
          cc_last_xmm = 0;
        } else if (src_type == TYPE_INT && cast_type == TYPE_DOUBLE) {
          emit_cvtsi2sd(cc, 0);
          cc_last_xmm = 0;
        } else if (src_type == TYPE_FLOAT && cast_type == TYPE_INT) {
          /* float in XMM0 -> int in EAX (truncating) */
          emit_cvttss2si(cc, 0);
        } else if (src_type == TYPE_DOUBLE && cast_type == TYPE_INT) {
          emit_cvttsd2si(cc, 0);
        } else if (src_type == TYPE_FLOAT && cast_type == TYPE_DOUBLE) {
          emit_cvtss2sd(cc, 0, 0);
          cc_last_xmm = 0;
        } else if (src_type == TYPE_DOUBLE && cast_type == TYPE_FLOAT) {
          emit_cvtsd2ss(cc, 0, 0);
          cc_last_xmm = 0;
        }
        /* Any other type transition (int<->ptr, float<->ptr, etc.) is
         * a pure retag; the bit pattern in EAX is reused as-is.  FP
         * to/from pointer via a cast is NOT supported — intermediate
         * (int) cast is required. */
      }
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

  case CC_TOK_NEW: {
    cc_type_t alloc_type = cc_parse_type(cc);
    int alloc_si = cc_last_type_struct_index;
    int32_t elem_size = cc_type_size(cc, alloc_type, alloc_si);

    if (alloc_type == TYPE_VOID || elem_size <= 0) {
      cc_error(cc, "invalid type for new");
      return;
    }

    if (alloc_type == TYPE_STRUCT && !cc_struct_is_complete(cc, alloc_si)) {
      cc_error(cc, "new of incomplete struct type");
      return;
    }

    if (cc_match(cc, CC_TOK_LBRACK)) {
      cc_parse_expression(cc, 1);
      cc_expect(cc, CC_TOK_RBRACK);

      if (elem_size == 1) {
        /* no scale */
      } else if (elem_size == 2) {
        emit8(cc, 0xC1);
        emit8(cc, 0xE0);
        emit8(cc, 0x01); /* shl eax,1 */
      } else if (elem_size == 4) {
        emit8(cc, 0xC1);
        emit8(cc, 0xE0);
        emit8(cc, 0x02); /* shl eax,2 */
      } else {
        emit8(cc, 0x69);
        emit8(cc, 0xC0); /* imul eax,eax,imm32 */
        emit32(cc, (uint32_t)elem_size);
      }
    } else {
      emit_mov_eax_imm(cc, (uint32_t)elem_size);
    }

    /* size in eax */
    emit_push_eax(cc); /* save size */
    emit_push_eax(cc); /* kmalloc(size) */

    {
      cc_symbol_t *kmalloc_sym = cc_sym_find(cc, "kmalloc");
      if (!kmalloc_sym || kmalloc_sym->kind != SYM_KERNEL) {
        cc_error(cc, "kmalloc binding missing");
        return;
      }
      emit_call_abs(cc, kmalloc_sym->address);
    }
    emit_add_esp(cc, 4);

    emit_pop_ebx(cc);  /* ebx=size */
    emit_push_eax(cc); /* preserve ptr as expression result */

    /* memset(ptr, 0, size) */
    emit8(cc, 0x53);      /* push ebx (size) */
    emit_push_imm(cc, 0); /* c = 0 */
    emit_push_eax(cc);    /* ptr */
    {
      cc_symbol_t *memset_sym = cc_sym_find(cc, "memset");
      if (!memset_sym || memset_sym->kind != SYM_KERNEL) {
        cc_error(cc, "memset binding missing");
        return;
      }
      emit_call_abs(cc, memset_sym->address);
    }
    emit_add_esp(cc, 12);

    emit_pop_eax(cc); /* ptr result */

    if (alloc_type == TYPE_CHAR)
      cc_last_expr_type = TYPE_CHAR_PTR;
    else if (alloc_type == TYPE_STRUCT) {
      cc_last_expr_type = TYPE_STRUCT_PTR;
      cc_last_expr_struct_index = alloc_si;
    } else
      cc_last_expr_type = TYPE_PTR;

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

    /* Struct member access: expr.field or expr->field */
    if (next.type == CC_TOK_DOT || next.type == CC_TOK_ARROW) {
      postfix_lvalue_valid = 0;
      cc_next(cc); /* consume . or -> */
      cc_token_t field_tok = cc_next(cc);
      if (field_tok.type != CC_TOK_IDENT) {
        cc_error(cc, "expected field name");
        return;
      }
      int si = cc_last_expr_struct_index;

      /* Method call sugar: obj.Method(args) -> Class_Method(&obj, args)
       * or ptr->Method(args) -> Class_Method(ptr, args). */
      if ((cc_last_expr_type == TYPE_STRUCT ||
           cc_last_expr_type == TYPE_STRUCT_PTR) &&
          cc_peek(cc).type == CC_TOK_LPAREN && si >= 0 &&
          si < cc->struct_count) {
        char method_sym_name[CC_MAX_IDENT];
        cc_make_method_symbol(method_sym_name, cc->structs[si].name,
                              field_tok.text);

        cc_next(cc); /* consume '(' */

        /* First implicit argument is self pointer in eax. */
        int argc = 0;
        /* Task 18: track per-arg sizes for size-aware reversal. */
        int arg_sizes[CC_MAX_PARAMS];
        int total_arg_bytes = 0;
        emit_push_eax(cc);
        arg_sizes[argc] = 4;
        total_arg_bytes += 4;
        argc++;

        if (cc_peek(cc).type != CC_TOK_RPAREN) {
          cc_parse_expression(cc, 1);
          if (argc < CC_MAX_PARAMS) {
            if (cc_last_expr_type == TYPE_FLOAT) {
              emit_push_xmm_float(cc, 0);
              arg_sizes[argc] = 4;
            } else if (cc_last_expr_type == TYPE_DOUBLE) {
              emit_push_xmm_double(cc, 0);
              arg_sizes[argc] = 8;
            } else {
              emit_push_eax(cc);
              arg_sizes[argc] = 4;
            }
            total_arg_bytes += arg_sizes[argc];
            argc++;
          }

          while (cc_match(cc, CC_TOK_COMMA)) {
            cc_parse_expression(cc, 1);
            if (argc >= CC_MAX_PARAMS) {
              cc_error(cc, "too many call arguments");
              break;
            }
            if (cc_last_expr_type == TYPE_FLOAT) {
              emit_push_xmm_float(cc, 0);
              arg_sizes[argc] = 4;
            } else if (cc_last_expr_type == TYPE_DOUBLE) {
              emit_push_xmm_double(cc, 0);
              arg_sizes[argc] = 8;
            } else {
              emit_push_eax(cc);
              arg_sizes[argc] = 4;
            }
            total_arg_bytes += arg_sizes[argc];
            argc++;
          }
        }
        cc_expect(cc, CC_TOK_RPAREN);

        /* Reverse pushed args (same convention as normal calls).
         * Task 18: size-aware swap — see cc_parse_ident_expr call-site
         * for identical logic and rationale. */
        if (argc > 1) {
          int src_off[CC_MAX_PARAMS];
          {
            int running = 0;
            for (int i = argc - 1; i >= 0; i--) {
              src_off[i] = running;
              running += arg_sizes[i];
            }
          }
          int a;
          for (a = 0; a < argc / 2; a++) {
            int b = argc - 1 - a;
            int sa = arg_sizes[a];
            int sb = arg_sizes[b];
            int off_a = src_off[a];
            int off_b = src_off[b];
            if (sa == 4 && sb == 4) {
              emit8(cc, 0x8B); emit8(cc, 0x8C); emit8(cc, 0x24);
              emit32(cc, (uint32_t)off_a); /* mov ecx, [esp+off_a] */
              emit8(cc, 0x8B); emit8(cc, 0x94); emit8(cc, 0x24);
              emit32(cc, (uint32_t)off_b); /* mov edx, [esp+off_b] */
              emit8(cc, 0x89); emit8(cc, 0x94); emit8(cc, 0x24);
              emit32(cc, (uint32_t)off_a); /* mov [esp+off_a], edx */
              emit8(cc, 0x89); emit8(cc, 0x8C); emit8(cc, 0x24);
              emit32(cc, (uint32_t)off_b); /* mov [esp+off_b], ecx */
            } else if (sa == 8 && sb == 8) {
              for (int d = 0; d < 2; d++) {
                int oa = off_a + d * 4;
                int ob = off_b + d * 4;
                emit8(cc, 0x8B); emit8(cc, 0x8C); emit8(cc, 0x24);
                emit32(cc, (uint32_t)oa);
                emit8(cc, 0x8B); emit8(cc, 0x94); emit8(cc, 0x24);
                emit32(cc, (uint32_t)ob);
                emit8(cc, 0x89); emit8(cc, 0x94); emit8(cc, 0x24);
                emit32(cc, (uint32_t)oa);
                emit8(cc, 0x89); emit8(cc, 0x8C); emit8(cc, 0x24);
                emit32(cc, (uint32_t)ob);
              }
            } else {
              cc_error(cc,
                       "mixed int/double args in same method call not yet "
                       "supported");
              break;
            }
          }
        }

        {
          cc_symbol_t *msym = cc_sym_find(cc, method_sym_name);
          if (msym) {
            if (msym->kind == SYM_FUNC && msym->is_defined) {
              emit_call_abs(cc, cc->code_base + (uint32_t)msym->offset);
            } else if (msym->kind == SYM_KERNEL) {
              emit_call_abs(cc, msym->address);
            } else if (msym->kind == SYM_FUNC) {
              uint32_t patch_pos = emit_call_rel_placeholder(cc);
              if (cc->patch_count < CC_MAX_PATCHES) {
                cc_patch_t *p = &cc->patches[cc->patch_count++];
                p->code_offset = patch_pos;
                int mi = 0;
                while (method_sym_name[mi] && mi < CC_MAX_IDENT - 1) {
                  p->name[mi] = method_sym_name[mi];
                  mi++;
                }
                p->name[mi] = '\0';
              }
            } else {
              cc_error(cc, "not a method");
              return;
            }
          } else {
            cc_symbol_t *fsym =
                cc_sym_add(cc, method_sym_name, SYM_FUNC, TYPE_INT);
            if (fsym) {
              fsym->param_count = argc;
              fsym->is_defined = 0;
            }
            {
              uint32_t patch_pos = emit_call_rel_placeholder(cc);
              if (cc->patch_count < CC_MAX_PATCHES) {
                cc_patch_t *p = &cc->patches[cc->patch_count++];
                p->code_offset = patch_pos;
                int mi = 0;
                while (method_sym_name[mi] && mi < CC_MAX_IDENT - 1) {
                  p->name[mi] = method_sym_name[mi];
                  mi++;
                }
                p->name[mi] = '\0';
              }
            }
          }
        }

        if (total_arg_bytes > 0) {
          emit_add_esp(cc, (int32_t)total_arg_bytes);
        }

        /* Task 18: propagate FP return type so callers can spill via XMM0. */
        {
          cc_symbol_t *msym2 = cc_sym_find(cc, method_sym_name);
          cc_type_t mret = TYPE_INT;
          if (msym2 && (msym2->kind == SYM_FUNC || msym2->kind == SYM_KERNEL)) {
            mret = msym2->type;
          }
          cc_last_expr_type = mret;
          if (mret == TYPE_FLOAT || mret == TYPE_DOUBLE) {
            cc_last_xmm = 0;
          }
        }
        continue;
      }

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

    cc_type_t left_type = cc_last_expr_type;
    int left_is_fp = (left_type == TYPE_FLOAT || left_type == TYPE_DOUBLE);
    if (left_is_fp) {
      /* Spill XMM0 (the FP accumulator) onto the stack.  Reserve 8 bytes
       * regardless of type so ESP stays 4-byte aligned in both cases. */
      emit8(cc, 0x83);
      emit8(cc, 0xEC);
      emit8(cc, 0x08); /* sub esp, 8 */
      if (left_type == TYPE_DOUBLE) {
        emit_movsd_esp_xmm(cc, 0);
      } else {
        emit_movss_esp_xmm(cc, 0);
      }
    } else {
      emit_push_eax(cc); /* save integer left operand */
    }
    cc_parse_expression(cc, prec + 1);
    cc_type_t right_type = cc_last_expr_type;
    int right_is_fp = (right_type == TYPE_FLOAT || right_type == TYPE_DOUBLE);

    if (left_is_fp || right_is_fp) {
      /* Scalar SSE binary op. Only + - * / supported in Task 16/17.
       *
       * Task 17 adds implicit promotion for mixed int/FP and mixed
       * float/double.  The promoted type is determined by cc_promote().
       * Each mismatched case arranges for:
       *    - LHS loaded into XMM1 in the promoted type
       *    - RHS loaded into XMM0 in the promoted type
       *    - ESP restored (spill slot discarded)
       * after which the common SSE-op emit below applies unchanged.
       *
       * Spill-slot layout from Task 16:
       *    left_is_fp  -> sub esp,8; movs{s,d} [esp], xmm0  (8 bytes)
       *    !left_is_fp -> push eax                         (4 bytes)
       */
      cc_type_t fp_result_type = cc_promote(cc, left_type, right_type);
      int is_double = (fp_result_type == TYPE_DOUBLE);
      uint8_t op_byte = 0;
      int ok = 1;
      switch (op.type) {
      case CC_TOK_PLUS:  op_byte = 0x58; break; /* ADDSS/ADDSD */
      case CC_TOK_MINUS: op_byte = 0x5C; break; /* SUBSS/SUBSD */
      case CC_TOK_STAR:  op_byte = 0x59; break; /* MULSS/MULSD */
      case CC_TOK_SLASH: op_byte = 0x5E; break; /* DIVSS/DIVSD */
      default:
        cc_error(cc, "invalid operator for floating-point operands");
        ok = 0;
        break;
      }

      int need_default_reload = 1;
      if (ok && left_type != right_type) {
        /* Mismatched types — need at least one conversion. */
        if (!left_is_fp) {
          /* Case A/B: LHS int on stack (4 bytes via push eax),
           * RHS FP in XMM0.  Pop LHS, convert to promoted FP into XMM1.
           * Promoted type == RHS type here (int + float -> float,
           * int + double -> double). */
          emit_pop_eax(cc);
          if (is_double) {
            emit_cvtsi2sd(cc, 1);
          } else {
            emit_cvtsi2ss(cc, 1);
          }
          need_default_reload = 0;
        } else if (!right_is_fp) {
          /* Case C/D: LHS FP on stack (8-byte slot), RHS int in EAX.
           * Convert RHS int in EAX -> promoted FP in XMM0, then reload
           * LHS from stack into XMM1.  LHS was spilled as its original
           * FP type (float=4 bytes, double=8 bytes); if LHS is float
           * but the promoted type is double (can't occur: result is
           * driven by LHS being float with int RHS -> float), we'd
           * need to widen.  In practice LHS-FP + RHS-int always
           * promotes to LHS's FP type. */
          if (is_double) {
            emit_cvtsi2sd(cc, 0);
            emit_movsd_xmm_esp(cc, 1);
          } else {
            emit_cvtsi2ss(cc, 0);
            emit_movss_xmm_esp(cc, 1);
          }
          emit8(cc, 0x83);
          emit8(cc, 0xC4);
          emit8(cc, 0x08); /* add esp, 8 */
          need_default_reload = 0;
        } else {
          /* Case E/F: both FP but different precision.
           *   E: LHS float, RHS double -> promote to double.
           *      Stack has 4 bytes of float in an 8-byte slot.
           *      Load as float into XMM1, widen to double.
           *   F: LHS double, RHS float -> promote to double.
           *      Stack has 8 bytes of double. Load as double into XMM1.
           *      Widen RHS (XMM0) from float to double. */
          if (left_type == TYPE_FLOAT && right_type == TYPE_DOUBLE) {
            emit_movss_xmm_esp(cc, 1);
            emit_cvtss2sd(cc, 1, 1);
          } else { /* left double, right float */
            emit_movsd_xmm_esp(cc, 1);
            emit_cvtss2sd(cc, 0, 0); /* widen RHS */
          }
          emit8(cc, 0x83);
          emit8(cc, 0xC4);
          emit8(cc, 0x08); /* add esp, 8 */
          need_default_reload = 0;
        }
      }

      if (ok && need_default_reload) {
        /* Same-type fast path: XMM0 holds RHS; [esp] holds LHS.
         * Reload LHS into XMM1 and discard spill slot. */
        if (is_double) {
          emit_movsd_xmm_esp(cc, 1);
        } else {
          emit_movss_xmm_esp(cc, 1);
        }
        emit8(cc, 0x83);
        emit8(cc, 0xC4);
        emit8(cc, 0x08); /* add esp, 8  discard spill slot */
      }

      if (ok) {
        /* Result = LHS OP RHS must land in XMM0.
         *   For + and *: commutative, XMM0 := XMM0 OP XMM1.
         *   For - and /: non-commutative, compute XMM1 OP= XMM0 then
         *                MOVAPS XMM0, XMM1. */
        if (op.type == CC_TOK_PLUS || op.type == CC_TOK_STAR) {
          emit_sse_scalar_op(cc, is_double, op_byte, 0, 1);
        } else {
          emit_sse_scalar_op(cc, is_double, op_byte, 1, 0);
          /* MOVAPS xmm0, xmm1: 0F 28 C1 (mod=11,reg=0,r/m=1). */
          emit8(cc, 0x0F);
          emit8(cc, 0x28);
          emit8(cc, 0xC1);
        }
        cc_last_xmm = 0;
      }
      cc_last_expr_type = fp_result_type;
      continue; /* skip the int binop path below */
    }

    cc_emit_binop(cc, op.type);
    /* Track the promoted FP/int type for arithmetic ops only.
     * Comparison/logical/bitwise results stay int (0/1 or bit pattern).
     * Task 16 will use this type to select SSE vs. integer opcodes. */
    if (op.type == CC_TOK_PLUS || op.type == CC_TOK_MINUS ||
        op.type == CC_TOK_STAR || op.type == CC_TOK_SLASH ||
        op.type == CC_TOK_PERCENT) {
      cc_last_expr_type = cc_promote(cc, left_type, right_type);
    } else {
      cc_last_expr_type = TYPE_INT;
    }
  }

  /* Ternary operator ?: (lowest precedence; right-associative).
   * We keep this outside binary-op precedence handling and only allow
   * it when caller accepts lowest-precedence expressions (min_prec <= 1). */
  while (!cc->error && min_prec <= 1 && cc_peek(cc).type == CC_TOK_QUESTION) {
    cc_next(cc); /* consume ? */

    /* EAX currently holds condition value. */
    emit8(cc, 0x85);
    emit8(cc, 0xC0); /* test eax, eax */
    uint32_t jz_off = cc->code_pos;
    emit8(cc, 0x0F);
    emit8(cc, 0x84);
    emit32(cc, 0); /* jz <false> placeholder */

    /* Parse true arm. */
    cc_parse_expression(cc, 1);

    /* Jump over false arm. */
    uint32_t jmp_off = cc->code_pos;
    emit8(cc, 0xE9);
    emit32(cc, 0); /* jmp <end> placeholder */

    /* False arm starts here. */
    patch32(cc, jz_off + 2, (uint32_t)(cc->code_pos - (jz_off + 6)));

    cc_token_t colon = cc_next(cc);
    if (colon.type != CC_TOK_COLON) {
      cc_error(cc, "expected ':' in ternary");
      return;
    }

    /* Parse false arm. Using min_prec=1 keeps right-associative chaining:
     * a ? b : c ? d : e  => a ? b : (c ? d : e). */
    cc_parse_expression(cc, 1);

    /* End of ternary expression. */
    patch32(cc, jmp_off + 1, (uint32_t)(cc->code_pos - (jmp_off + 5)));
  }
}

/* Assignment Parsing */

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

  /* SIMD assignment path — MOVUPS xmm0 to the 16-byte local.  Only plain
   * '=' is supported; compound ops on float4/double2 are not a thing.
   * RHS must be the same SIMD type — no implicit conversions across
   * float4/double2.  This enables patterns like:
   *    float4 s;
   *    s = _mm_add_ps(a, b);
   * where the intrinsic leaves its result in XMM0. */
  if (sym->type == TYPE_FLOAT4 || sym->type == TYPE_DOUBLE2) {
    if (op.type != CC_TOK_EQ) {
      cc_error(cc, "SIMD compound assignment not supported");
      return;
    }
    if (cc_last_expr_type != sym->type) {
      cc_error(cc, "SIMD assignment type mismatch");
      return;
    }
    if (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM) {
      emit_movups_local_xmm(cc, 0, sym->offset);
    } else {
      cc_error(cc, "SIMD assignment to non-local not supported");
    }
    return;
  }

  /* FP assignment path — store XMM0 directly.  Only plain '=' is
   * supported for FP; compound FP assignment (+=, -=, *=, /=) is still
   * deferred to Task 18.  Task 17 allows implicit promotion of the RHS
   * when it differs from the target's FP type. */
  if (sym->type == TYPE_FLOAT || sym->type == TYPE_DOUBLE) {
    if (op.type != CC_TOK_EQ) {
      cc_error(cc, "FP compound assignment not yet supported (Task 18)");
      return;
    }
    /* Coerce RHS into the destination's FP type when possible. */
    if (cc_last_expr_type != sym->type) {
      if (cc_last_expr_type == TYPE_INT && sym->type == TYPE_FLOAT) {
        emit_cvtsi2ss(cc, 0);
        cc_last_xmm = 0;
        cc_last_expr_type = TYPE_FLOAT;
      } else if (cc_last_expr_type == TYPE_INT && sym->type == TYPE_DOUBLE) {
        emit_cvtsi2sd(cc, 0);
        cc_last_xmm = 0;
        cc_last_expr_type = TYPE_DOUBLE;
      } else if (cc_last_expr_type == TYPE_FLOAT && sym->type == TYPE_DOUBLE) {
        emit_cvtss2sd(cc, 0, 0);
        cc_last_xmm = 0;
        cc_last_expr_type = TYPE_DOUBLE;
      } else if (cc_last_expr_type == TYPE_DOUBLE && sym->type == TYPE_FLOAT) {
        emit_cvtsd2ss(cc, 0, 0);
        cc_last_xmm = 0;
        cc_last_expr_type = TYPE_FLOAT;
      } else {
        cc_error(cc, "FP assignment type mismatch"
                     " (no implicit conversion from this source type)");
        return;
      }
    }
    if (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM) {
      if (sym->type == TYPE_DOUBLE)
        emit_movsd_local_xmm(cc, 0, sym->offset);
      else
        emit_movss_local_xmm(cc, 0, sym->offset);
    } else if (sym->kind == SYM_GLOBAL) {
      /* MOVSS/MOVSD [disp32], xmm0: prefix + 0F 11 /0 + mod=00,r/m=101 */
      emit8(cc, sym->type == TYPE_DOUBLE ? 0xF2 : 0xF3);
      emit8(cc, 0x0F);
      emit8(cc, 0x11);
      emit8(cc, 0x05); /* mod=00, reg=0, r/m=101 (disp32) */
      emit32(cc, sym->address);
    }
    return;
  }

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
    /* Inner elements are char (1 byte) - no scaling */
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
    /* address is on the stack - peek at it */
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

/* Inline Assembly Parser */

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

/* Parse an XMM register name (xmm0..xmm7). Returns 0-7 or -1. */
static int cc_parse_xmm_reg(const char *text) {
  if (text[0] != 'x' || text[1] != 'm' || text[2] != 'm' || text[3] == '\0' ||
      text[4] != '\0')
    return -1;
  if (text[3] < '0' || text[3] > '7')
    return -1;
  return text[3] - '0';
}

/* Memory operand resolved from [ident] syntax in inline asm.
 *   is_local=1 : [ebp + offset] addressing (local/param)
 *   is_local=0 : absolute [disp32] addressing (global/kernel)
 */
typedef struct {
  int is_local;
  int32_t offset;   /* ebp-relative offset when is_local=1 */
  uint32_t address; /* absolute address when is_local=0   */
  int ok;
} cc_asm_mem_t;

/* Parse "[ identifier ]" and resolve it to either ebp-relative or absolute
 * addressing via the symbol table. Size-prefix keywords `dword`/`qword`/
 * `word` are accepted (and ignored) before the '[', e.g. `fld qword [x]`,
 * to remain source-compatible with code written for the standalone CupidASM.
 */
static cc_asm_mem_t cc_parse_asm_mem(cc_state_t *cc) {
  cc_asm_mem_t mem;
  mem.is_local = 0;
  mem.offset = 0;
  mem.address = 0;
  mem.ok = 0;

  /* Optional size-prefix keyword: dword / qword / word (ignored). */
  cc_token_t p = cc_peek(cc);
  if (p.type == CC_TOK_IDENT &&
      (strcmp(p.text, "dword") == 0 || strcmp(p.text, "qword") == 0 ||
       strcmp(p.text, "word") == 0 || strcmp(p.text, "byte") == 0)) {
    cc_next(cc);
  }

  if (!cc_match(cc, CC_TOK_LBRACK)) {
    cc_error(cc, "expected '[' for memory operand");
    return mem;
  }
  cc_token_t id = cc_next(cc);
  if (id.type != CC_TOK_IDENT) {
    cc_error(cc, "expected identifier inside '[' ... ']'");
    return mem;
  }
  if (!cc_expect(cc, CC_TOK_RBRACK))
    return mem;

  cc_symbol_t *sym = cc_sym_find(cc, id.text);
  if (!sym) {
    cc_error(cc, "unknown symbol in inline asm memory operand");
    return mem;
  }
  if (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM) {
    mem.is_local = 1;
    mem.offset = sym->offset;
  } else {
    mem.is_local = 0;
    mem.address = sym->address;
  }
  mem.ok = 1;
  return mem;
}

/* Emit a ModR/M byte + displacement for `[ebp + disp32]` or `[disp32]`
 * addressing, parameterized by the ModR/M reg field (XMM index for SSE,
 * opcode-extension digit for x87). */
static void cc_asm_emit_mem_modrm(cc_state_t *cc, int reg_field,
                                  const cc_asm_mem_t *mem) {
  if (mem->is_local) {
    /* mod=10, reg=reg_field, r/m=101 (EBP) + disp32 */
    emit8(cc, (uint8_t)(0x85 | ((reg_field & 7) << 3)));
    emit32(cc, (uint32_t)mem->offset);
  } else {
    /* mod=00, reg=reg_field, r/m=101 (disp32) */
    emit8(cc, (uint8_t)(0x05 | ((reg_field & 7) << 3)));
    emit32(cc, mem->address);
  }
}

/* Emit the SSE "xmm, xmm" form: <prefix> 0F <opcode> modrm(11, dst, src).
 * prefix=0x00 means no legacy prefix (PS variant). */
static void cc_asm_emit_sse_rr(cc_state_t *cc, uint8_t prefix, uint8_t opcode,
                               int xmm_dst, int xmm_src) {
  if (prefix)
    emit8(cc, prefix);
  emit8(cc, 0x0F);
  emit8(cc, opcode);
  emit8(cc, (uint8_t)(0xC0 | ((xmm_dst & 7) << 3) | (xmm_src & 7)));
}

/* Emit an SSE "xmm, [mem]" or "[mem], xmm" form.
 *   <prefix> 0F <opcode> <modrm+disp>
 */
static void cc_asm_emit_sse_mem(cc_state_t *cc, uint8_t prefix, uint8_t opcode,
                                int xmm, const cc_asm_mem_t *mem) {
  if (prefix)
    emit8(cc, prefix);
  emit8(cc, 0x0F);
  emit8(cc, opcode);
  cc_asm_emit_mem_modrm(cc, xmm, mem);
}

/* Try to encode a Phase B FPU/SSE opcode. Returns 1 if matched (and either
 * encoded or errored), 0 if the mnemonic wasn't one we handle here so the
 * caller can fall through to the integer dispatcher. */
static int cc_parse_asm_fpu_opcode(cc_state_t *cc, const char *mn) {
  /* ───────── No-operand x87 / FPU state-control ───────── */
  if (strcmp(mn, "fsin") == 0)   { emit8(cc, 0xD9); emit8(cc, 0xFE); return 1; }
  if (strcmp(mn, "fcos") == 0)   { emit8(cc, 0xD9); emit8(cc, 0xFF); return 1; }
  if (strcmp(mn, "fsqrt") == 0)  { emit8(cc, 0xD9); emit8(cc, 0xFA); return 1; }
  if (strcmp(mn, "fabs") == 0)   { emit8(cc, 0xD9); emit8(cc, 0xE1); return 1; }
  if (strcmp(mn, "fchs") == 0)   { emit8(cc, 0xD9); emit8(cc, 0xE0); return 1; }
  /* FINIT = FWAIT + FNINIT: 9B DB E3 (3 bytes) */
  if (strcmp(mn, "finit") == 0) {
    emit8(cc, 0x9B); emit8(cc, 0xDB); emit8(cc, 0xE3); return 1;
  }
  if (strcmp(mn, "fninit") == 0) {
    emit8(cc, 0xDB); emit8(cc, 0xE3); return 1;
  }

  /* ───────── x87 memory-operand opcodes (m32fp only) ─────────
   * Matches standalone CupidASM Task 11 behavior: no size-prefix keyword
   * support, so FLD/FST/FSTP always emit the D9 base (m32fp single-precision).
   */
  if (strcmp(mn, "fld") == 0 || strcmp(mn, "fst") == 0 ||
      strcmp(mn, "fstp") == 0) {
    cc_asm_mem_t mem = cc_parse_asm_mem(cc);
    if (!mem.ok) return 1;
    emit8(cc, 0xD9);
    int digit = (strcmp(mn, "fld") == 0) ? 0 : (strcmp(mn, "fst") == 0 ? 2 : 3);
    cc_asm_emit_mem_modrm(cc, digit, &mem);
    return 1;
  }

  /* ───────── MXCSR save/restore (mem32) ─────────
   * STMXCSR m32 = 0F AE /3   |   LDMXCSR m32 = 0F AE /2
   * Required by the Task 38 #XF provocation drill so user-space CupidC
   * can unmask SIMD FP exceptions before deliberately dividing by zero.
   */
  if (strcmp(mn, "stmxcsr") == 0 || strcmp(mn, "ldmxcsr") == 0) {
    cc_asm_mem_t mem = cc_parse_asm_mem(cc);
    if (!mem.ok) return 1;
    emit8(cc, 0x0F);
    emit8(cc, 0xAE);
    int digit = (strcmp(mn, "stmxcsr") == 0) ? 3 : 2;
    cc_asm_emit_mem_modrm(cc, digit, &mem);
    return 1;
  }

  /* ───────── SSE scalar "xmm, xmm" opcodes ───────── */
  struct { const char *mn; uint8_t prefix; uint8_t op; } sse_rr[] = {
      {"addss", 0xF3, 0x58}, {"addsd", 0xF2, 0x58},
      {"subss", 0xF3, 0x5C}, {"subsd", 0xF2, 0x5C},
      {"mulss", 0xF3, 0x59}, {"mulsd", 0xF2, 0x59},
      {"divss", 0xF3, 0x5E}, {"divsd", 0xF2, 0x5E},
      {"sqrtss", 0xF3, 0x51}, {"sqrtsd", 0xF2, 0x51},
      {"minss", 0xF3, 0x5D}, {"maxss", 0xF3, 0x5F},
      /* Packed */
      {"addps", 0x00, 0x58}, {"addpd", 0x66, 0x58},
      {"subps", 0x00, 0x5C}, {"subpd", 0x66, 0x5C},
      {"mulps", 0x00, 0x59}, {"mulpd", 0x66, 0x59},
      {"divps", 0x00, 0x5E}, {"divpd", 0x66, 0x5E},
      {"sqrtps", 0x00, 0x51}, {"sqrtpd", 0x66, 0x51},
      {"andps", 0x00, 0x54}, {"orps", 0x00, 0x56}, {"xorps", 0x00, 0x57},
  };
  for (unsigned i = 0; i < sizeof(sse_rr) / sizeof(sse_rr[0]); i++) {
    if (strcmp(mn, sse_rr[i].mn) == 0) {
      cc_token_t dst = cc_next(cc);
      int dreg = cc_parse_xmm_reg(dst.text);
      if (dreg < 0) { cc_error(cc, "expected XMM register"); return 1; }
      cc_expect(cc, CC_TOK_COMMA);
      cc_token_t src = cc_next(cc);
      int sreg = cc_parse_xmm_reg(src.text);
      if (sreg < 0) { cc_error(cc, "expected XMM register"); return 1; }
      cc_asm_emit_sse_rr(cc, sse_rr[i].prefix, sse_rr[i].op, dreg, sreg);
      return 1;
    }
  }

  /* ───────── MOVSS / MOVSD / MOVUPS / MOVAPS: bidirectional mem<->xmm ─────
   * Shape: peek first operand; if it's an XMM reg the direction is load
   * (xmm <- [mem]) with opcode 0x10; if it's '[' or a size-prefix keyword
   * the direction is store ([mem] <- xmm) with opcode 0x11. */
  struct { const char *mn; uint8_t prefix; } mov_variants[] = {
      {"movss", 0xF3}, {"movsd", 0xF2},
      {"movups", 0x00}, {"movupd", 0x66},
      {"movaps", 0x00}, {"movapd", 0x66},
  };
  for (unsigned i = 0; i < sizeof(mov_variants) / sizeof(mov_variants[0]); i++) {
    if (strcmp(mn, mov_variants[i].mn) == 0) {
      uint8_t prefix = mov_variants[i].prefix;
      cc_token_t first = cc_peek(cc);
      int dst_is_xmm =
          (first.type == CC_TOK_IDENT && cc_parse_xmm_reg(first.text) >= 0);
      if (dst_is_xmm) {
        /* xmm, [mem]  -- load, opcode 0x10 */
        cc_next(cc); /* consume xmm name */
        int xmm = cc_parse_xmm_reg(first.text);
        cc_expect(cc, CC_TOK_COMMA);
        cc_asm_mem_t mem = cc_parse_asm_mem(cc);
        if (!mem.ok) return 1;
        /* MOVAPS reg<->reg would use 0x28, but the mem-form uses 0x10. */
        uint8_t op = (strcmp(mn, "movaps") == 0 ||
                      strcmp(mn, "movapd") == 0) ? 0x28 : 0x10;
        cc_asm_emit_sse_mem(cc, prefix, op, xmm, &mem);
      } else {
        /* [mem], xmm  -- store, opcode 0x11 */
        cc_asm_mem_t mem = cc_parse_asm_mem(cc);
        if (!mem.ok) return 1;
        cc_expect(cc, CC_TOK_COMMA);
        cc_token_t src = cc_next(cc);
        int xmm = cc_parse_xmm_reg(src.text);
        if (xmm < 0) { cc_error(cc, "expected XMM register"); return 1; }
        uint8_t op = (strcmp(mn, "movaps") == 0 ||
                      strcmp(mn, "movapd") == 0) ? 0x29 : 0x11;
        cc_asm_emit_sse_mem(cc, prefix, op, xmm, &mem);
      }
      return 1;
    }
  }

  return 0;
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

    } else if (cc_parse_asm_fpu_opcode(cc, instr.text)) {
      /* Phase B FPU/SSE opcode handled (x87 + SSE scalar + SSE packed). */
    } else {
      /* Unknown instruction - skip to semicolon */
      cc_error(cc, "unknown assembly instruction");
    }

    /* Consume optional semicolon between asm instructions */
    cc_match(cc, CC_TOK_SEMICOLON);
  }

  cc_expect(cc, CC_TOK_RBRACE);
}

/* Statement Parsing */

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
      if (!cc_data_reserve(cc, (uint32_t)total_bytes))
        return;
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
      if (!cc_data_reserve(cc, (uint32_t)alloc_size))
        return;
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
    if (!cc_data_reserve(cc, 4))
      return;
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
    /* Call memset(addr, 0, size) - push in reverse for cdecl */
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

  /* SIMD variables (float4/double2): 16-byte aligned 16-byte slot.
   * Task 30: the prologue guarantees ESP is 16-byte aligned on entry
   * (AND ESP,-16), so rounding the frame offset DOWN to a multiple of
   * 16 keeps [ebp + offset] aligned for MOVAPS. */
  if (type == TYPE_FLOAT4 || type == TYPE_DOUBLE2) {
    /* Round DOWN (i.e. make more negative) to the next multiple of 16.
     * local_offset is negative and decreasing. */
    int32_t aligned = -((-cc->local_offset + 15) & ~15);
    cc->local_offset = aligned - 16;
    if (cc->local_offset < cc->max_local_offset)
      cc->max_local_offset = cc->local_offset;
    cc_symbol_t *sym = cc_sym_add(cc, name_tok.text, SYM_LOCAL, type);
    if (sym) {
      sym->offset = cc->local_offset;
      sym->struct_index = -1;
    }

    int expected_count = (type == TYPE_FLOAT4) ? 4 : 2;
    int elem_size = (type == TYPE_FLOAT4) ? 4 : 8;
    cc_type_t elem_type = (type == TYPE_FLOAT4) ? TYPE_FLOAT : TYPE_DOUBLE;

    if (cc_peek(cc).type == CC_TOK_EQ) {
      cc_next(cc); /* consume '=' */
      cc_expect(cc, CC_TOK_LBRACE);
      for (int i = 0; i < expected_count && !cc->error; i++) {
        cc_parse_expression(cc, 1);
        /* Coerce element into the lane's scalar FP type. */
        if (elem_type == TYPE_FLOAT) {
          if (cc_last_expr_type == TYPE_INT) {
            emit_cvtsi2ss(cc, 0);
          } else if (cc_last_expr_type == TYPE_DOUBLE) {
            emit_cvtsd2ss(cc, 0, 0);
          } else if (cc_last_expr_type != TYPE_FLOAT) {
            cc_error(cc,
                     "float4 initializer element must be int/float/double");
            return;
          }
          cc_last_xmm = 0;
          emit_movss_local_xmm(cc, 0, cc->local_offset + i * elem_size);
        } else {
          if (cc_last_expr_type == TYPE_INT) {
            emit_cvtsi2sd(cc, 0);
          } else if (cc_last_expr_type == TYPE_FLOAT) {
            emit_cvtss2sd(cc, 0, 0);
          } else if (cc_last_expr_type != TYPE_DOUBLE) {
            cc_error(cc,
                     "double2 initializer element must be int/float/double");
            return;
          }
          cc_last_xmm = 0;
          emit_movsd_local_xmm(cc, 0, cc->local_offset + i * elem_size);
        }
        if (i < expected_count - 1)
          cc_expect(cc, CC_TOK_COMMA);
      }
      cc_expect(cc, CC_TOK_RBRACE);
    } else {
      /* Zero-initialize: XORPS xmm0,xmm0 then MOVSS/MOVSD to each lane. */
      emit8(cc, 0x0F);
      emit8(cc, 0x57);
      emit8(cc, 0xC0); /* XORPS xmm0, xmm0 */
      for (int i = 0; i < expected_count; i++) {
        if (elem_type == TYPE_FLOAT)
          emit_movss_local_xmm(cc, 0, cc->local_offset + i * elem_size);
        else
          emit_movsd_local_xmm(cc, 0, cc->local_offset + i * elem_size);
      }
    }

    cc_expect(cc, CC_TOK_SEMICOLON);
    return;
  }

  /* Regular variable — allocate stack slot sized to the type. */
  int local_size = 4;
  if (type == TYPE_DOUBLE)
    local_size = 8;
  /* float stays at 4 bytes; other scalar types also 4 bytes. */
  cc->local_offset -= local_size;
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
    if (type == TYPE_FLOAT) {
      /* Coerce initializer into float if needed (Task 17). */
      if (cc_last_expr_type == TYPE_INT) {
        emit_cvtsi2ss(cc, 0);
        cc_last_xmm = 0;
      } else if (cc_last_expr_type == TYPE_DOUBLE) {
        emit_cvtsd2ss(cc, 0, 0);
        cc_last_xmm = 0;
      } else if (cc_last_expr_type != TYPE_FLOAT) {
        cc_error(cc, "float initializer requires int/float/double"
                     " (non-scalar initializer not supported)");
      }
      emit_movss_local_xmm(cc, 0, cc->local_offset);
    } else if (type == TYPE_DOUBLE) {
      if (cc_last_expr_type == TYPE_INT) {
        emit_cvtsi2sd(cc, 0);
        cc_last_xmm = 0;
      } else if (cc_last_expr_type == TYPE_FLOAT) {
        emit_cvtss2sd(cc, 0, 0);
        cc_last_xmm = 0;
      } else if (cc_last_expr_type != TYPE_DOUBLE) {
        cc_error(cc, "double initializer requires int/float/double"
                     " (non-scalar initializer not supported)");
      }
      emit_movsd_local_xmm(cc, 0, cc->local_offset);
    } else {
      emit_store_local(cc, cc->local_offset);
    }
  } else {
    /* Zero-initialize */
    if (type == TYPE_FLOAT) {
      /* XORPS xmm0, xmm0 then MOVSS [ebp+disp], xmm0 */
      emit8(cc, 0x0F);
      emit8(cc, 0x57);
      emit8(cc, 0xC0);
      emit_movss_local_xmm(cc, 0, cc->local_offset);
    } else if (type == TYPE_DOUBLE) {
      emit8(cc, 0x0F);
      emit8(cc, 0x57);
      emit8(cc, 0xC0); /* XORPS xmm0, xmm0 */
      emit_movsd_local_xmm(cc, 0, cc->local_offset);
    } else {
      emit_mov_eax_imm(cc, 0);
      emit_store_local(cc, cc->local_offset);
    }
  }

  cc_expect(cc, CC_TOK_SEMICOLON);
}

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

  /* Save increment expression position - we'll emit a jmp over it */
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

static void cc_parse_return(cc_state_t *cc) {
  if (cc_peek(cc).type != CC_TOK_SEMICOLON) {
    cc_parse_expression(cc, 1);
    /* Task 18: float/double return values live in XMM0.  Task 16's
     * expression codegen places FP results in XMM0 (cc_last_xmm=0),
     * but if a future pass routes them to a different XMM reg we
     * MOVAPS the value into XMM0 before the epilogue. */
    if ((cc_last_expr_type == TYPE_FLOAT ||
         cc_last_expr_type == TYPE_DOUBLE) &&
        cc_last_xmm != 0) {
      emit_movaps_xmm_xmm(cc, 0, cc_last_xmm);
      cc_last_xmm = 0;
    }
  }
  cc_expect(cc, CC_TOK_SEMICOLON);

  /* Emit epilogue (function cleanup + ret) */
  emit_epilogue(cc);
}

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
        /* mov eax, [esp] - reload switch val */
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

  case CC_TOK_DEL: {
    cc_next(cc); /* consume del */
    cc_token_t id = cc_next(cc);
    if (id.type != CC_TOK_IDENT) {
      cc_error(cc, "del expects a pointer variable");
      break;
    }

    cc_symbol_t *sym = cc_sym_find(cc, id.text);
    if (!sym) {
      cc_error(cc, "undefined variable");
      break;
    }

    if (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM) {
      emit_load_local(cc, sym->offset);
    } else if (sym->kind == SYM_GLOBAL) {
      emit8(cc, 0xA1);
      emit32(cc, sym->address);
    } else {
      cc_error(cc, "del expects a variable");
      break;
    }

    emit_push_eax(cc);
    {
      cc_symbol_t *kfree_sym = cc_sym_find(cc, "kfree");
      if (!kfree_sym || kfree_sym->kind != SYM_KERNEL) {
        cc_error(cc, "kfree binding missing");
        break;
      }
      emit_call_abs(cc, kfree_sym->address);
    }
    emit_add_esp(cc, 4);

    emit_mov_eax_imm(cc, 0);
    if (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM) {
      emit_store_local(cc, sym->offset);
    } else if (sym->kind == SYM_GLOBAL) {
      emit8(cc, 0xA3);
      emit32(cc, sym->address);
    }

    cc_expect(cc, CC_TOK_SEMICOLON);
    break;
  }

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

      /* Method-call statement sugar: obj.Method(args); / ptr->Method(args); */
      {
        int saved_pos = cc->pos;
        int saved_line = cc->line;
        int saved_has_peek = cc->has_peek;
        cc_token_t saved_peek = cc->peek_buf;
        cc_token_t saved_cur = cc->cur;

        cc_token_t dot_or_arrow_tok = cc_next(cc);
        cc_token_t member_tok = cc_next(cc);
        int is_method_stmt =
            ((dot_or_arrow_tok.type == CC_TOK_DOT ||
              dot_or_arrow_tok.type == CC_TOK_ARROW) &&
             member_tok.type == CC_TOK_IDENT &&
             cc_peek(cc).type == CC_TOK_LPAREN &&
             sym->struct_index >= 0 && sym->struct_index < cc->struct_count &&
             (sym->type == TYPE_STRUCT || sym->type == TYPE_STRUCT_PTR));

        cc->pos = saved_pos;
        cc->line = saved_line;
        cc->has_peek = saved_has_peek;
        cc->peek_buf = saved_peek;
        cc->cur = saved_cur;

        if (is_method_stmt) {
          char method_sym_name[CC_MAX_IDENT];
          cc_make_method_symbol(method_sym_name,
                                cc->structs[sym->struct_index].name,
                                member_tok.text);

          /* Load object/self pointer into eax. */
          if (sym->kind == SYM_GLOBAL) {
            if (sym->type == TYPE_STRUCT)
              emit_mov_eax_imm(cc, sym->address);
            else {
              emit8(cc, 0xA1);
              emit32(cc, sym->address);
            }
          } else if (sym->type == TYPE_STRUCT) {
            emit_lea_local(cc, sym->offset);
          } else {
            emit_load_local(cc, sym->offset);
          }

          cc_next(cc); /* consume . or -> */
          cc_next(cc); /* consume method name */
          cc_expect(cc, CC_TOK_LPAREN);

          {
            int argc = 0;
            /* Task 18: track per-arg sizes for size-aware reversal. */
            int arg_sizes[CC_MAX_PARAMS];
            int total_arg_bytes = 0;

            emit_push_eax(cc); /* implicit self */
            arg_sizes[argc] = 4;
            total_arg_bytes += 4;
            argc++;

            if (cc_peek(cc).type != CC_TOK_RPAREN) {
              cc_parse_expression(cc, 1);
              if (argc < CC_MAX_PARAMS) {
                if (cc_last_expr_type == TYPE_FLOAT) {
                  emit_push_xmm_float(cc, 0);
                  arg_sizes[argc] = 4;
                } else if (cc_last_expr_type == TYPE_DOUBLE) {
                  emit_push_xmm_double(cc, 0);
                  arg_sizes[argc] = 8;
                } else {
                  emit_push_eax(cc);
                  arg_sizes[argc] = 4;
                }
                total_arg_bytes += arg_sizes[argc];
                argc++;
              }
              while (cc_match(cc, CC_TOK_COMMA)) {
                cc_parse_expression(cc, 1);
                if (argc >= CC_MAX_PARAMS) {
                  cc_error(cc, "too many call arguments");
                  break;
                }
                if (cc_last_expr_type == TYPE_FLOAT) {
                  emit_push_xmm_float(cc, 0);
                  arg_sizes[argc] = 4;
                } else if (cc_last_expr_type == TYPE_DOUBLE) {
                  emit_push_xmm_double(cc, 0);
                  arg_sizes[argc] = 8;
                } else {
                  emit_push_eax(cc);
                  arg_sizes[argc] = 4;
                }
                total_arg_bytes += arg_sizes[argc];
                argc++;
              }
            }
            cc_expect(cc, CC_TOK_RPAREN);

            if (argc > 1) {
              int src_off[CC_MAX_PARAMS];
              {
                int running = 0;
                for (int i = argc - 1; i >= 0; i--) {
                  src_off[i] = running;
                  running += arg_sizes[i];
                }
              }
              int a;
              for (a = 0; a < argc / 2; a++) {
                int b = argc - 1 - a;
                int sa = arg_sizes[a];
                int sb = arg_sizes[b];
                int off_a = src_off[a];
                int off_b = src_off[b];
                if (sa == 4 && sb == 4) {
                  emit8(cc, 0x8B); emit8(cc, 0x8C); emit8(cc, 0x24);
                  emit32(cc, (uint32_t)off_a);
                  emit8(cc, 0x8B); emit8(cc, 0x94); emit8(cc, 0x24);
                  emit32(cc, (uint32_t)off_b);
                  emit8(cc, 0x89); emit8(cc, 0x94); emit8(cc, 0x24);
                  emit32(cc, (uint32_t)off_a);
                  emit8(cc, 0x89); emit8(cc, 0x8C); emit8(cc, 0x24);
                  emit32(cc, (uint32_t)off_b);
                } else if (sa == 8 && sb == 8) {
                  for (int d = 0; d < 2; d++) {
                    int oa = off_a + d * 4;
                    int ob = off_b + d * 4;
                    emit8(cc, 0x8B); emit8(cc, 0x8C); emit8(cc, 0x24);
                    emit32(cc, (uint32_t)oa);
                    emit8(cc, 0x8B); emit8(cc, 0x94); emit8(cc, 0x24);
                    emit32(cc, (uint32_t)ob);
                    emit8(cc, 0x89); emit8(cc, 0x94); emit8(cc, 0x24);
                    emit32(cc, (uint32_t)oa);
                    emit8(cc, 0x89); emit8(cc, 0x8C); emit8(cc, 0x24);
                    emit32(cc, (uint32_t)ob);
                  }
                } else {
                  cc_error(cc,
                           "mixed int/double args in same method call not "
                           "yet supported");
                  break;
                }
              }
            }

            {
              cc_symbol_t *msym = cc_sym_find(cc, method_sym_name);
              if (msym) {
                if (msym->kind == SYM_FUNC && msym->is_defined) {
                  emit_call_abs(cc, cc->code_base + (uint32_t)msym->offset);
                } else if (msym->kind == SYM_KERNEL) {
                  emit_call_abs(cc, msym->address);
                } else if (msym->kind == SYM_FUNC) {
                  uint32_t patch_pos = emit_call_rel_placeholder(cc);
                  if (cc->patch_count < CC_MAX_PATCHES) {
                    cc_patch_t *p = &cc->patches[cc->patch_count++];
                    p->code_offset = patch_pos;
                    int mi = 0;
                    while (method_sym_name[mi] && mi < CC_MAX_IDENT - 1) {
                      p->name[mi] = method_sym_name[mi];
                      mi++;
                    }
                    p->name[mi] = '\0';
                  }
                } else {
                  cc_error(cc, "not a method");
                  break;
                }
              } else {
                cc_symbol_t *fsym =
                    cc_sym_add(cc, method_sym_name, SYM_FUNC, TYPE_INT);
                if (fsym) {
                  fsym->param_count = argc;
                  fsym->is_defined = 0;
                }
                {
                  uint32_t patch_pos = emit_call_rel_placeholder(cc);
                  if (cc->patch_count < CC_MAX_PATCHES) {
                    cc_patch_t *p = &cc->patches[cc->patch_count++];
                    p->code_offset = patch_pos;
                    int mi = 0;
                    while (method_sym_name[mi] && mi < CC_MAX_IDENT - 1) {
                      p->name[mi] = method_sym_name[mi];
                      mi++;
                    }
                    p->name[mi] = '\0';
                  }
                }
              }
            }

            if (total_arg_bytes > 0)
              emit_add_esp(cc, (int32_t)total_arg_bytes);
          }

          cc_expect(cc, CC_TOK_SEMICOLON);
          break;
        }
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
          /* Leaf field - next should be = */
          break;
        }
      }
      /* Expect assignment operator */
      cc_token_t assign_op = cc_peek(cc);
      if (!cc_is_assignment_op(assign_op.type)) {
        /* Not an assignment - this is an expression statement */
        /* (e.g., s.func_ptr(args);) - dereference and discard */
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

/* Function Parsing */

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
  cc_xmm_reset();

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
  emit_sub_esp(cc, 256); /* placeholder - generous allocation */

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

static void cc_parse_class_method(cc_state_t *cc, int class_index,
                                  cc_type_t ret_type,
                                  const char *method_name) {
  char full_name[CC_MAX_IDENT];
  cc_make_method_symbol(full_name, cc->structs[class_index].name, method_name);

  cc_symbol_t *func_sym = cc_sym_find(cc, full_name);
  if (!func_sym) {
    func_sym = cc_sym_add(cc, full_name, SYM_FUNC, ret_type);
  }
  if (func_sym) {
    func_sym->kind = SYM_FUNC;
    func_sym->type = ret_type;
    func_sym->offset = (int32_t)cc->code_pos;
    func_sym->is_defined = 1;
  }

  cc_expect(cc, CC_TOK_LPAREN);

  int saved_scope = cc->sym_count;
  cc->local_offset = 0;
  cc->max_local_offset = 0;
  cc->param_count = 0;
  cc_xmm_reset();

  /* Implicit self parameter at [ebp+8]. */
  {
    cc_symbol_t *self_sym = cc_sym_add(cc, "self", SYM_PARAM, TYPE_STRUCT_PTR);
    if (self_sym) {
      self_sym->offset = 8;
      self_sym->struct_index = class_index;
    }
    cc->param_count = 1;
  }

  if (cc_peek(cc).type != CC_TOK_RPAREN) {
    int param_offset = 12; /* after implicit self */

    if (cc_peek(cc).type == CC_TOK_ELLIPSIS) {
      cc_next(cc);
    } else {
      cc_type_t ptype = cc_parse_type(cc);
      int psi = cc_last_type_struct_index;

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
          cc_next(cc);
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

  emit_prologue(cc);
  {
    uint32_t sub_esp_pos = cc->code_pos;
    emit_sub_esp(cc, 256);

    cc_expect(cc, CC_TOK_LBRACE);
    while (!cc->error && cc_peek(cc).type != CC_TOK_RBRACE &&
           cc_peek(cc).type != CC_TOK_EOF) {
      cc_parse_statement(cc);
    }
    cc_expect(cc, CC_TOK_RBRACE);

    {
      int32_t locals_size = -cc->max_local_offset;
      if (locals_size < 0)
        locals_size = 0;
      locals_size = (locals_size + 15) & ~15;
      if (locals_size == 0)
        locals_size = 16;
      patch32(cc, sub_esp_pos + 2, (uint32_t)locals_size);
    }
  }

  emit_mov_eax_imm(cc, 0);
  emit_epilogue(cc);

  cc->sym_count = saved_scope;

  if (func_sym) {
    cc_symbol_t *new_sym = cc_sym_add(cc, full_name, SYM_FUNC, ret_type);
    if (new_sym) {
      new_sym->offset = func_sym->offset;
      new_sym->address = func_sym->address;
      new_sym->param_count = func_sym->param_count;
      new_sym->is_defined = 1;
    }
  }
}

/* Top-Level Program Parsing */

void cc_parse_program(cc_state_t *cc) {
  cc->struct_count = 0;

  /* Pass 1: collect top-level function symbols for use-before-define. */
  cc_prescan_functions(cc);

  int has_top_level_statements = 0;
  int top_level_started = 0;
  uint32_t top_level_offset = 0;
  uint32_t top_level_sub_esp_pos = 0;
  uint32_t parse_iter = 0;

  while (!cc->error && cc_peek(cc).type != CC_TOK_EOF) {
    parse_iter++;
    if ((parse_iter & 2047u) == 0u) {
      cc_token_t pt = cc_peek(cc);
      serial_printf("[cupidc] parse iter=%u line=%d tok=%d\n", parse_iter,
                    pt.line, (int)pt.type);
    }
    if (parse_iter > 500000u) {
      cc_error(cc, "parser runaway");
      break;
    }

    cc_token_t tok = cc_peek(cc);
    if (tok.type == CC_TOK_STATIC) {
      /* File-scope static is accepted; linkage is not distinguished. */
      cc_next(cc);
      tok = cc_peek(cc);
    }

    /* Enum definition: enum { A, B = 5, C }; */
    if (tok.type == CC_TOK_ENUM) {
      cc_next(cc); /* consume 'enum' */
      /* Optional enum name (ignored - we just create constants) */
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
          if (!cc_data_reserve(cc, 4))
            break;
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

    /* Typedef: typedef <type> <alias>; */
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

    /* Class definition: class Name { fields... methods... }; */
    if (tok.type == CC_TOK_CLASS) {
      cc_next(cc); /* consume 'class' */
      cc_token_t name_tok = cc_next(cc);
      if (name_tok.type != CC_TOK_IDENT) {
        cc_error(cc, "expected class name");
        break;
      }
      int sidx = cc_get_or_add_struct_tag(cc, name_tok.text);
      if (sidx < 0)
        break;

      cc_struct_def_t *sd = &cc->structs[sidx];
      if (sd->is_complete) {
        cc_error(cc, "redefinition of class");
        break;
      }
      sd->field_count = 0;
      sd->total_size = 0;
      sd->align = 1;
      sd->is_complete = 0;

      cc_expect(cc, CC_TOK_LBRACE);

      {
        int32_t field_offset = 0;
        int32_t struct_align = 1;

        while (!cc->error && cc_peek(cc).type != CC_TOK_RBRACE &&
               cc_peek(cc).type != CC_TOK_EOF) {
          if (!cc_is_type_or_typedef(cc, cc_peek(cc))) {
            cc_error(cc, "expected class field or method declaration");
            break;
          }

          /* Look ahead: <type> <name> '(' => method, else field */
          int saved_pos = cc->pos;
          int saved_line = cc->line;
          int saved_has_peek = cc->has_peek;
          cc_token_t saved_peek = cc->peek_buf;
          cc_token_t saved_cur = cc->cur;

          cc_parse_type(cc);
          cc_token_t member_name = cc_next(cc);
          cc_token_t after_member = cc_peek(cc);

          cc->pos = saved_pos;
          cc->line = saved_line;
          cc->has_peek = saved_has_peek;
          cc->peek_buf = saved_peek;
          cc->cur = saved_cur;

          if (member_name.type != CC_TOK_IDENT) {
            cc_error(cc, "expected class member name");
            break;
          }

          if (after_member.type == CC_TOK_LPAREN) {
            cc_type_t mret = cc_parse_type(cc);
            cc_token_t mname = cc_next(cc);
            if (mname.type != CC_TOK_IDENT) {
              cc_error(cc, "expected method name");
              break;
            }
            cc_parse_class_method(cc, sidx, mret, mname.text);
            if (cc->error)
              break;
            continue;
          }

          if (sd->field_count >= CC_MAX_FIELDS) {
            cc_error(cc, "too many fields in class");
            break;
          }

          {
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

            if (cc_peek(cc).type == CC_TOK_LBRACK) {
              cc_next(cc);
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

            {
              int32_t elem_size = cc_type_size(cc, ftype, fsi);
              int32_t field_align = cc_type_align(cc, ftype, fsi);
              int32_t fsize = elem_size;
              if (f->array_count > 0)
                fsize = elem_size * f->array_count;

              field_offset = cc_align_up(field_offset, field_align);
              f->offset = field_offset;
              field_offset += fsize;
              if (field_align > struct_align)
                struct_align = field_align;
            }

            cc_expect(cc, CC_TOK_SEMICOLON);
          }
        }

        cc_expect(cc, CC_TOK_RBRACE);
        cc_expect(cc, CC_TOK_SEMICOLON);

        sd->align = struct_align;
        sd->total_size = cc_align_up(field_offset, struct_align);
        sd->is_complete = 1;
      }

      serial_printf("[cupidc] Defined class '%s': %d fields, %d bytes\n",
                    sd->name, sd->field_count, sd->total_size);
      continue;
    }

    /* Struct definition: struct Name { fields... }; */
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
       * a function return or global variable - handled below */
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
        /* If we're in implicit top-level execution mode, emitted function
         * bodies must be skipped by __start so execution doesn't fall-through
         * into them as straight-line code. */
        uint32_t skip_func_jmp = 0;
        int has_skip_jmp = 0;
        if (top_level_started) {
          skip_func_jmp = emit_jmp_placeholder(cc);
          has_skip_jmp = 1;
        }

        cc_parse_function(cc);

        if (has_skip_jmp) {
          patch_jump(cc, skip_func_jmp);
        }
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
            if (!cc_data_reserve(cc, (uint32_t)total_bytes))
              break;
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
            if (!cc_data_reserve(cc, (uint32_t)alloc_size))
              break;
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
            if (!cc_data_reserve(cc, 4))
              break;
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
                int slen = 0;
                while (val.text[slen])
                  slen++;
                if (!cc_data_reserve(cc, (uint32_t)(slen + 1))) {
                  break;
                }
                uint32_t str_addr = cc->data_base + cc->data_pos;
                int si = 0;
                while (val.text[si]) {
                  cc->data[cc->data_pos++] = (uint8_t)val.text[si++];
                }
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
      /* Top-level executable statement (HolyC-style script mode).
       * We compile these into an implicit __start() thunk and execute it.
       */
      if (!top_level_started) {
        cc_symbol_t *start_sym = cc_sym_find(cc, "__start");
        if (!start_sym) {
          start_sym = cc_sym_add(cc, "__start", SYM_FUNC, TYPE_VOID);
        }
        if (start_sym) {
          start_sym->kind = SYM_FUNC;
          start_sym->type = TYPE_VOID;
          start_sym->offset = (int32_t)cc->code_pos;
          start_sym->is_defined = 1;
          start_sym->param_count = 0;
        }

        top_level_offset = cc->code_pos;
        cc->local_offset = 0;
        cc->max_local_offset = 0;
        cc->param_count = 0;
        cc_xmm_reset();

        emit_prologue(cc);
        top_level_sub_esp_pos = cc->code_pos;
        emit_sub_esp(cc, 256); /* placeholder, patched at end */

        top_level_started = 1;
      }

      has_top_level_statements = 1;
      cc_parse_statement(cc);
    }
  }

  if (!cc->error && top_level_started) {
    /* If main() exists, run it after top-level statements for compatibility. */
    cc_symbol_t *main_sym = cc_sym_find(cc, "main");
    if (main_sym && main_sym->kind == SYM_FUNC && main_sym->is_defined) {
      uint32_t target = cc->code_base + (uint32_t)main_sym->offset;
      emit_call_abs(cc, target);
    }

    /* Default return from implicit __start. */
    emit_mov_eax_imm(cc, 0);
    emit_epilogue(cc);

    /* Patch stack allocation for locals used by top-level statements. */
    int32_t locals_size = -cc->max_local_offset;
    if (locals_size < 0)
      locals_size = 0;
    locals_size = (locals_size + 15) & ~15;
    if (locals_size == 0)
      locals_size = 16;
    patch32(cc, top_level_sub_esp_pos + 2, (uint32_t)locals_size);

    /* Drop top-level locals/params while preserving globals/functions. */
    {
      int write_i = 0;
      int read_i;
      for (read_i = 0; read_i < cc->sym_count; read_i++) {
        cc_symbol_t *s = &cc->symbols[read_i];
        if (s->kind == SYM_LOCAL || s->kind == SYM_PARAM) {
          continue;
        }
        if (write_i != read_i) {
          cc->symbols[write_i] = cc->symbols[read_i];
        }
        write_i++;
      }
      cc->sym_count = write_i;
    }
  }

  if (!cc->error && has_top_level_statements) {
    cc->entry_offset = top_level_offset;
    cc->has_entry = 1;
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

/* ══════════════════════════════════════════════════════════════════════
 *  REPL Line Parsing — TempleOS-style direct statement compilation
 * ══════════════════════════════════════════════════════════════════════ */

static int cc_repl_try_zero_arg_call(cc_state_t *cc, int *is_expr) {
  int saved_pos = cc->pos;
  int saved_line = cc->line;
  int saved_has_peek = cc->has_peek;
  cc_token_t saved_peek = cc->peek_buf;
  cc_token_t saved_cur = cc->cur;
  cc_token_t ident_tok;
  cc_token_t after_tok;
  cc_symbol_t *sym;

  if (cc_peek(cc).type != CC_TOK_IDENT)
    return 0;

  ident_tok = cc_next(cc);
  after_tok = cc_peek(cc);

  cc->pos = saved_pos;
  cc->line = saved_line;
  cc->has_peek = saved_has_peek;
  cc->peek_buf = saved_peek;
  cc->cur = saved_cur;

  if (after_tok.type != CC_TOK_SEMICOLON && after_tok.type != CC_TOK_EOF)
    return 0;

  sym = cc_sym_find(cc, ident_tok.text);
  if (!sym || sym->param_count != 0)
    return 0;
  if (sym->kind != SYM_FUNC && sym->kind != SYM_KERNEL)
    return 0;

  cc_next(cc); /* consume identifier */

  if (sym->kind == SYM_KERNEL) {
    emit_call_abs(cc, sym->address);
    cc_last_expr_type = TYPE_VOID;
    *is_expr = 0;
  } else {
    if (sym->is_defined) {
      uint32_t target = cc->code_base + (uint32_t)sym->offset;
      emit_call_abs(cc, target);
    } else {
      uint32_t patch_pos = emit_call_rel_placeholder(cc);
      if (cc->patch_count < CC_MAX_PATCHES) {
        cc_patch_t *p = &cc->patches[cc->patch_count++];
        p->code_offset = patch_pos;
        int pi = 0;
        while (ident_tok.text[pi] && pi < CC_MAX_IDENT - 1) {
          p->name[pi] = ident_tok.text[pi];
          pi++;
        }
        p->name[pi] = '\0';
      }
    }
    cc_last_expr_type = sym->type;
    *is_expr = sym->type != TYPE_VOID;
  }

  if (cc_peek(cc).type == CC_TOK_SEMICOLON)
    cc_next(cc);
  return 1;
}

void cc_parse_repl_line(cc_state_t *cc, int *is_expr) {
  *is_expr = 0;

  if (cc->error || cc_peek(cc).type == CC_TOK_EOF)
    return;

  cc_token_t tok = cc_peek(cc);

  /* ── Static qualifier ─────────────────────────────────────────── */
  if (tok.type == CC_TOK_STATIC) {
    cc_next(cc);
    tok = cc_peek(cc);
  }

  /* ── Enum definition: enum { A, B = 5, C }; ─────────────────── */
  if (tok.type == CC_TOK_ENUM) {
    cc_next(cc);
    if (cc_peek(cc).type == CC_TOK_IDENT)
      cc_next(cc);
    cc_expect(cc, CC_TOK_LBRACE);
    int32_t enum_val = 0;
    while (!cc->error && cc_peek(cc).type != CC_TOK_RBRACE &&
           cc_peek(cc).type != CC_TOK_EOF) {
      cc_token_t name_tok = cc_next(cc);
      if (name_tok.type != CC_TOK_IDENT) {
        cc_error(cc, "expected enum constant name");
        return;
      }
      if (cc_match(cc, CC_TOK_EQ)) {
        cc_token_t val_tok = cc_next(cc);
        int negate = 0;
        if (val_tok.type == CC_TOK_MINUS) {
          negate = 1;
          val_tok = cc_next(cc);
        }
        if (val_tok.type != CC_TOK_NUMBER) {
          cc_error(cc, "expected integer in enum");
          return;
        }
        enum_val = negate ? -val_tok.int_value : val_tok.int_value;
      }
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
      if (cc_peek(cc).type != CC_TOK_RBRACE)
        cc_expect(cc, CC_TOK_COMMA);
    }
    cc_expect(cc, CC_TOK_RBRACE);
    if (cc_peek(cc).type == CC_TOK_SEMICOLON)
      cc_next(cc);
    return;
  }

  /* ── Typedef: typedef <type> <alias>; ──────────────────────────── */
  if (tok.type == CC_TOK_TYPEDEF) {
    cc_next(cc);
    cc_type_t td_type = cc_parse_type(cc);
    cc_token_t alias_tok = cc_next(cc);
    if (alias_tok.type != CC_TOK_IDENT) {
      cc_error(cc, "expected typedef alias name");
      return;
    }
    if (cc_peek(cc).type == CC_TOK_SEMICOLON)
      cc_next(cc);
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
    return;
  }

  /* ── Struct definition: struct Name { fields... }; ──────────────── */
  if (tok.type == CC_TOK_STRUCT) {
    int saved_pos = cc->pos;
    int saved_line = cc->line;
    int saved_has_peek = cc->has_peek;
    cc_token_t saved_peek = cc->peek_buf;
    cc_token_t saved_cur = cc->cur;

    cc_next(cc);
    cc_token_t sname = cc_next(cc);
    cc_token_t after = cc_peek(cc);
    (void)sname;

    cc->pos = saved_pos;
    cc->line = saved_line;
    cc->has_peek = saved_has_peek;
    cc->peek_buf = saved_peek;
    cc->cur = saved_cur;

    if (after.type == CC_TOK_LBRACE) {
      cc_next(cc);
      cc_token_t name_tok = cc_next(cc);
      if (name_tok.type != CC_TOK_IDENT) {
        cc_error(cc, "expected struct name");
        return;
      }
      int sidx = cc_get_or_add_struct_tag(cc, name_tok.text);
      if (sidx < 0)
        return;
      cc_struct_def_t *sd = &cc->structs[sidx];
      if (sd->is_complete) {
        cc_error(cc, "redefinition of struct");
        return;
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
          return;
        }
        cc_type_t ftype = cc_parse_type(cc);
        int fsi = cc_last_type_struct_index;
        cc_token_t fname = cc_next(cc);
        if (fname.type != CC_TOK_IDENT) {
          cc_error(cc, "expected field name");
          return;
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
        if (cc_peek(cc).type == CC_TOK_LBRACK) {
          cc_next(cc);
          cc_token_t size_tok = cc_next(cc);
          if (size_tok.type != CC_TOK_NUMBER) {
            cc_error(cc, "expected array size");
            return;
          }
          f->array_count = size_tok.int_value;
          cc_expect(cc, CC_TOK_RBRACK);
        }
        if (ftype == TYPE_STRUCT && !cc_struct_is_complete(cc, fsi)) {
          cc_error(cc, "field has incomplete struct type");
          return;
        }
        int32_t elem_size = cc_type_size(cc, ftype, fsi);
        int32_t field_align_val = cc_type_align(cc, ftype, fsi);
        int32_t fsize = elem_size;
        if (f->array_count > 0)
          fsize = elem_size * f->array_count;
        field_offset = cc_align_up(field_offset, field_align_val);
        f->offset = field_offset;
        field_offset += fsize;
        if (field_align_val > struct_align)
          struct_align = field_align_val;
        cc_expect(cc, CC_TOK_SEMICOLON);
      }
      cc_expect(cc, CC_TOK_RBRACE);
      if (cc_peek(cc).type == CC_TOK_SEMICOLON)
        cc_next(cc);
      sd->align = struct_align;
      sd->total_size = cc_align_up(field_offset, struct_align);
      sd->is_complete = 1;
      return;
    }
    if (after.type == CC_TOK_SEMICOLON) {
      cc_next(cc);
      cc_token_t name_tok = cc_next(cc);
      if (name_tok.type != CC_TOK_IDENT) {
        cc_error(cc, "expected struct name");
        return;
      }
      cc_expect(cc, CC_TOK_SEMICOLON);
      cc_get_or_add_struct_tag(cc, name_tok.text);
      return;
    }
    /* Fall through — struct used as type for variable or function */
  }

  /* ── Check if line starts with a type (function def or global var) ── */
  if (cc_is_type_or_typedef(cc, tok)) {
    int saved_pos = cc->pos;
    int saved_line = cc->line;
    int saved_has_peek = cc->has_peek;
    cc_token_t saved_peek = cc->peek_buf;
    cc_token_t saved_cur = cc->cur;

    cc_type_t type = cc_parse_type(cc);
    cc_token_t name_tok = cc_next(cc);
    cc_token_t after = cc_peek(cc);

    cc->pos = saved_pos;
    cc->line = saved_line;
    cc->has_peek = saved_has_peek;
    cc->peek_buf = saved_peek;
    cc->cur = saved_cur;

    if (after.type == CC_TOK_LPAREN) {
      cc_parse_function(cc);

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
        }
      }
      return;
    }

    (void)type;
    (void)name_tok;
    cc_type_t gtype = cc_parse_type(cc);
    int gtype_si = cc_last_type_struct_index;
    cc_token_t gname = cc_next(cc);
    if (gname.type != CC_TOK_IDENT) {
      cc_error(cc, "expected variable name");
      return;
    }

    if (cc_peek(cc).type == CC_TOK_LBRACK) {
      cc_next(cc);
      cc_token_t size_tok = cc_next(cc);
      if (size_tok.type != CC_TOK_NUMBER) {
        cc_error(cc, "expected array size");
        return;
      }
      cc_expect(cc, CC_TOK_RBRACK);
      int32_t arr_elems = size_tok.int_value;
      int32_t elem_size = (gtype == TYPE_CHAR) ? 1 : 4;
      cc_type_t arr_type = (gtype == TYPE_CHAR) ? TYPE_CHAR_PTR : TYPE_INT_PTR;
      if (gtype == TYPE_STRUCT && gtype_si >= 0 && gtype_si < cc->struct_count) {
        elem_size = cc->structs[gtype_si].total_size;
        arr_type = TYPE_STRUCT_PTR;
      }
      int32_t total_bytes = (arr_elems * elem_size + 3) & ~3;
      cc_symbol_t *gsym = cc_sym_add(cc, gname.text, SYM_GLOBAL, arr_type);
      if (gsym) {
        gsym->address = cc->data_base + cc->data_pos;
        gsym->is_array = 1;
        gsym->struct_index = gtype_si;
        gsym->array_elem_size = elem_size;
        memset(cc->data + cc->data_pos, 0, (size_t)total_bytes);
        cc->data_pos += (uint32_t)total_bytes;
      }
      if (cc_peek(cc).type == CC_TOK_SEMICOLON)
        cc_next(cc);
      return;
    }

    if (gtype == TYPE_STRUCT && gtype_si >= 0) {
      if (!cc_struct_is_complete(cc, gtype_si)) {
        cc_error(cc, "incomplete struct type");
        return;
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
          return;
      }
      if (cc_peek(cc).type == CC_TOK_SEMICOLON)
        cc_next(cc);
      return;
    }

    {
      cc_symbol_t *gsym = cc_sym_add(cc, gname.text, SYM_GLOBAL, gtype);
      if (gsym) {
        gsym->address = cc->data_base + cc->data_pos;
        gsym->struct_index = gtype_si;
        memset(cc->data + cc->data_pos, 0, 4);
        cc->data_pos += 4;

        if (cc_match(cc, CC_TOK_EQ)) {
          cc_token_t val = cc_next(cc);
          uint32_t addr_off = gsym->address - cc->data_base;
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
            uint32_t str_addr = cc->data_base + cc->data_pos;
            int si = 0;
            while (val.text[si] && cc->data_pos < CC_MAX_DATA) {
              cc->data[cc->data_pos++] = (uint8_t)val.text[si++];
            }
            if (cc->data_pos < CC_MAX_DATA)
              cc->data[cc->data_pos++] = 0;
            cc->data_pos = (cc->data_pos + 3u) & ~3u;
            cc->data[addr_off] = (uint8_t)(str_addr & 0xFF);
            cc->data[addr_off + 1] = (uint8_t)((str_addr >> 8) & 0xFF);
            cc->data[addr_off + 2] = (uint8_t)((str_addr >> 16) & 0xFF);
            cc->data[addr_off + 3] = (uint8_t)((str_addr >> 24) & 0xFF);
          }
        }
      }
      if (cc_peek(cc).type == CC_TOK_SEMICOLON)
        cc_next(cc);
      return;
    }
  }

  /* ── Statement / Expression — emit executable code ─────────────── */
  {
    cc->entry_offset = cc->code_pos;
    cc->has_entry = 1;

    emit_prologue(cc);
    uint32_t sub_esp_pos = cc->code_pos;
    emit_sub_esp(cc, 256);

    int saved_scope = cc->sym_count;
    cc->local_offset = 0;
    cc->max_local_offset = 0;
    cc->param_count = 0;
    cc_xmm_reset();

    int last_was_expr = 0;

    while (!cc->error && cc_peek(cc).type != CC_TOK_EOF) {
      last_was_expr = 0;
      cc_token_t next = cc_peek(cc);

      if (cc_is_type_or_typedef(cc, next)) {
        cc_parse_statement(cc);
        continue;
      }

      if (next.type == CC_TOK_IF || next.type == CC_TOK_WHILE ||
          next.type == CC_TOK_FOR || next.type == CC_TOK_DO ||
          next.type == CC_TOK_SWITCH || next.type == CC_TOK_LBRACE) {
        cc_parse_statement(cc);
        continue;
      }

      if (cc_repl_try_zero_arg_call(cc, &last_was_expr))
        continue;

      cc_parse_expression(cc, 1);
      last_was_expr = 1;
      if (cc_peek(cc).type == CC_TOK_SEMICOLON)
        cc_next(cc);
    }

    *is_expr = last_was_expr && cc_last_expr_type != TYPE_VOID &&
               cc_last_expr_type != TYPE_FUNC_PTR;

    int32_t locals_size = -cc->max_local_offset;
    if (locals_size < 0)
      locals_size = 0;
    locals_size = (locals_size + 15) & ~15;
    if (locals_size == 0)
      locals_size = 16;
    patch32(cc, sub_esp_pos + 2, (uint32_t)locals_size);

    emit_epilogue(cc);

    cc->sym_count = saved_scope;

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
      }
    }
  }
}
