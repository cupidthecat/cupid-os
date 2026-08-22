/**
 * cupidc_parse.cc - Parser and x86 code generator for CupidC
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

#include "serial.h"
#include "cupidc.h"
#include "kernel.h"
#include "string.h"

static cc_type_t cc_last_expr_type;

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
 * imm32.  Callers may pass >127 when args include doubles.*/
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
 * Unconditionally 16-byte align ESP so subsequent MOVAPS/MOVDQA
 * (used for SIMD, and potentially also for libm) is safe.  Cost is 3
 * extra bytes per function; in exchange we don't need to track whether
 * a given function touches SSE.  Local-frame size is rounded up to a
 * multiple of 16 (see emit_sub_esp patching) so ESP stays 16-aligned
 * after the SUB ESP, <local_frame>.*/
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
 * Scalar float/double arithmetic. Strategy:
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

/* MOVSS/MOVSD xmm, [disp32] - load from absolute data-segment address. */
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

/* MOVUPS xmm, [disp32] and MOVUPS [disp32], xmm. Direct global, block-static,
 * and persistent REPL vectors use unaligned moves just like automatic SIMD
 * objects and indexed leaves. */
static void emit_movups_xmm_disp32(cc_state_t *cc, int xmm, uint32_t addr) {
  emit8(cc, 0x0F);
  emit8(cc, 0x10);
  emit8(cc, cc_xmm_modrm_disp32(xmm));
  emit32(cc, addr);
}

static void emit_movups_disp32_xmm(cc_state_t *cc, int xmm, uint32_t addr) {
  emit8(cc, 0x0F);
  emit8(cc, 0x11);
  emit8(cc, cc_xmm_modrm_disp32(xmm));
  emit32(cc, addr);
}

/* MOVSS/MOVSD [disp32], xmm - store to an absolute data address. */
static void emit_movss_disp32_xmm(cc_state_t *cc, int xmm, uint32_t addr) {
  emit8(cc, 0xF3);
  emit8(cc, 0x0F);
  emit8(cc, 0x11);
  emit8(cc, cc_xmm_modrm_disp32(xmm));
  emit32(cc, addr);
}

static void emit_movsd_disp32_xmm(cc_state_t *cc, int xmm, uint32_t addr) {
  emit8(cc, 0xF2);
  emit8(cc, 0x0F);
  emit8(cc, 0x11);
  emit8(cc, cc_xmm_modrm_disp32(xmm));
  emit32(cc, addr);
}

/* MOVSS/MOVSD xmm, [ebp + disp32] - load FP local/param into XMM reg. */
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

/* MOVSS/MOVSD [ebp + disp32], xmm - store XMM reg into FP local/param. */
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

/* MOVSS/MOVSD xmm, [eax] and MOVSS/MOVSD [eax], xmm. Fixed floating
 * arrays keep their computed element address in EAX, so these forms let
 * subscripting preserve the scalar SSE value lane used everywhere else. */
static void emit_movss_xmm_eax(cc_state_t *cc, int xmm) {
  emit8(cc, 0xF3);
  emit8(cc, 0x0F);
  emit8(cc, 0x10);
  emit8(cc, (uint8_t)((xmm & 7) << 3));
}
static void emit_movsd_xmm_eax(cc_state_t *cc, int xmm) {
  emit8(cc, 0xF2);
  emit8(cc, 0x0F);
  emit8(cc, 0x10);
  emit8(cc, (uint8_t)((xmm & 7) << 3));
}
static void emit_movss_eax_xmm(cc_state_t *cc, int xmm) {
  emit8(cc, 0xF3);
  emit8(cc, 0x0F);
  emit8(cc, 0x11);
  emit8(cc, (uint8_t)((xmm & 7) << 3));
}
static void emit_movsd_eax_xmm(cc_state_t *cc, int xmm) {
  emit8(cc, 0xF2);
  emit8(cc, 0x0F);
  emit8(cc, 0x11);
  emit8(cc, (uint8_t)((xmm & 7) << 3));
}

/* MOVUPS xmm, [eax] and MOVUPS [eax], xmm keep vector array access safe
 * for storage that does not promise sixteen-byte alignment. */
static void emit_movups_xmm_eax(cc_state_t *cc, int xmm) {
  emit8(cc, 0x0F);
  emit8(cc, 0x10);
  emit8(cc, (uint8_t)((xmm & 7) << 3));
}
static void emit_movups_eax_xmm(cc_state_t *cc, int xmm) {
  emit8(cc, 0x0F);
  emit8(cc, 0x11);
  emit8(cc, (uint8_t)((xmm & 7) << 3));
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

/* Negate a scalar in XMM0 by flipping its IEEE-754 sign bit. This preserves
 * signed zero, NaN payloads, and every other payload bit. */
static void emit_negate_xmm0_scalar(cc_state_t *cc, int is_double) {
  emit8(cc, 0x83); /* sub esp, 8 */
  emit8(cc, 0xEC);
  emit8(cc, 0x08);
  if (is_double)
    emit_movsd_esp_xmm(cc, 0);
  else
    emit_movss_esp_xmm(cc, 0);

  emit8(cc, 0x81); /* xor dword ptr [esp + sign_word], 0x80000000 */
  if (is_double) {
    emit8(cc, 0x74);
    emit8(cc, 0x24);
    emit8(cc, 0x04);
  } else {
    emit8(cc, 0x34);
    emit8(cc, 0x24);
  }
  emit32(cc, 0x80000000u);

  if (is_double)
    emit_movsd_xmm_esp(cc, 0);
  else
    emit_movss_xmm_esp(cc, 0);
  emit8(cc, 0x83); /* add esp, 8 */
  emit8(cc, 0xC4);
  emit8(cc, 0x08);
}

/* Push an XMM float onto the stack: SUB ESP,4 + MOVSS [ESP], xmm.
 * Used by function-call arg-push loop when arg is TYPE_FLOAT.*/
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
 * Used by function-call arg-push loop when arg is TYPE_DOUBLE.*/
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

/* Place one complete packed value in an unaligned-safe cdecl stack slot. */
static void emit_push_xmm_vector(cc_state_t *cc, int xmm) {
  emit8(cc, 0x83); /* sub esp, 16 */
  emit8(cc, 0xEC);
  emit8(cc, 0x10);
  emit8(cc, 0x0F); /* movups [esp], xmm */
  emit8(cc, 0x11);
  emit8(cc, (uint8_t)(0x04 | ((xmm & 7) << 3)));
  emit8(cc, 0x24);
}

/* MOVAPS xmm_dst, xmm_src: 0F 28 /r (mod=11).  Used to move an
 * FP return value into XMM0 before emitting the epilogue.*/
static void emit_movaps_xmm_xmm(cc_state_t *cc, int dst, int src) {
  if (dst == src)
    return;
  emit8(cc, 0x0F);
  emit8(cc, 0x28);
  emit8(cc, (uint8_t)(0xC0 | ((dst & 7) << 3) | (src & 7)));
}

/* MOVUPS xmm, [ebp + disp32] - unaligned 16-byte load of SIMD local/param.
 * Encoding: 0F 10 /r with ModR/M mod=10, reg=xmm, r/m=101 (EBP) + disp32.
 * Materializes a float4/double2 local into XMM0.
 * Uses MOVUPS (0F 10) rather than MOVAPS (0F 28) because
 * [ebp + disp] alignment isn't guaranteed - the prologue does
 * `push ebp; mov ebp, esp; and esp, -16`, which aligns ESP but leaves
 * EBP holding the pre-AND value (which is off by 4 from the aligned
 * boundary because of the PUSH EBP). MOVUPS tolerates unaligned
 * addresses and is cheap on modern x86, so it's the safer choice.*/
static void emit_movups_xmm_local(cc_state_t *cc, int xmm, int32_t offset) {
  emit8(cc, 0x0F);
  emit8(cc, 0x10);
  emit8(cc, cc_xmm_modrm_ebp(xmm));
  emit32(cc, (uint32_t)offset);
}

/* MOVUPS [ebp + disp32], xmm - unaligned 16-byte store of SIMD XMM reg.
 * Encoding: 0F 11 /r with ModR/M mod=10, reg=xmm, r/m=101 (EBP) + disp32.
 * Reserved for full-vector stores; init-list codegen currently
 * stores element-by-element via MOVSS/MOVSD.
 * See emit_movups_xmm_local for why we use MOVUPS, not MOVAPS.*/
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

/* Compare the scalar left operand in XMM1 with the right operand in XMM0.
 * UCOMISS and UCOMISD report an unordered comparison by setting PF, ZF, and
 * CF. The parity checks below keep NaN unequal to every value, including
 * itself, while making every ordered relation false. */
static void emit_compare_xmm1_xmm0(cc_state_t *cc, int is_double,
                                   cc_token_type_t op) {
  if (is_double)
    emit8(cc, 0x66);
  emit8(cc, 0x0F);
  emit8(cc, 0x2E); /* UCOMISS/UCOMISD xmm1, xmm0 */
  emit8(cc, 0xC8);

  switch (op) {
  case CC_TOK_EQEQ:
    emit8(cc, 0x0F);
    emit8(cc, 0x94);
    emit8(cc, 0xC0); /* sete al */
    emit8(cc, 0x0F);
    emit8(cc, 0x9B);
    emit8(cc, 0xC2); /* setnp dl */
    emit8(cc, 0x20);
    emit8(cc, 0xD0); /* and al, dl */
    break;
  case CC_TOK_NE:
    emit8(cc, 0x0F);
    emit8(cc, 0x95);
    emit8(cc, 0xC0); /* setne al */
    emit8(cc, 0x0F);
    emit8(cc, 0x9A);
    emit8(cc, 0xC2); /* setp dl */
    emit8(cc, 0x08);
    emit8(cc, 0xD0); /* or al, dl */
    break;
  case CC_TOK_LT:
    emit8(cc, 0x0F);
    emit8(cc, 0x92);
    emit8(cc, 0xC0); /* setb al */
    emit8(cc, 0x0F);
    emit8(cc, 0x9B);
    emit8(cc, 0xC2); /* setnp dl */
    emit8(cc, 0x20);
    emit8(cc, 0xD0); /* and al, dl */
    break;
  case CC_TOK_GT:
    emit8(cc, 0x0F);
    emit8(cc, 0x97);
    emit8(cc, 0xC0); /* seta al */
    break;
  case CC_TOK_LE:
    emit8(cc, 0x0F);
    emit8(cc, 0x96);
    emit8(cc, 0xC0); /* setbe al */
    emit8(cc, 0x0F);
    emit8(cc, 0x9B);
    emit8(cc, 0xC2); /* setnp dl */
    emit8(cc, 0x20);
    emit8(cc, 0xD0); /* and al, dl */
    break;
  case CC_TOK_GE:
    emit8(cc, 0x0F);
    emit8(cc, 0x93);
    emit8(cc, 0xC0); /* setae al */
    break;
  default:
    return;
  }
  emit_movzx_eax_al(cc);
}

/* Convert the scalar floating value in XMM0 to C truth in EAX.
 * UCOMISS/UCOMISD sets ZF for either signed zero and PF for unordered
 * operands. Combining SETNE with SETP therefore makes both zero encodings
 * false while keeping every nonzero value, including NaN, true. */
static void emit_scalar_truth_xmm0(cc_state_t *cc, int is_double) {
  emit8(cc, 0x0F);
  emit8(cc, 0x57);
  emit8(cc, 0xC9); /* xorps xmm1, xmm1 */

  if (is_double)
    emit8(cc, 0x66);
  emit8(cc, 0x0F);
  emit8(cc, 0x2E);
  emit8(cc, 0xC1); /* ucomiss/ucomisd xmm0, xmm1 */

  emit8(cc, 0x0F);
  emit8(cc, 0x95);
  emit8(cc, 0xC0); /* setne al */
  emit8(cc, 0x0F);
  emit8(cc, 0x9A);
  emit8(cc, 0xC2); /* setp dl */
  emit8(cc, 0x08);
  emit8(cc, 0xD0); /* or al, dl */
  emit_movzx_eax_al(cc);
}

/* Forward decl: defined just below the error-handling block. */
static int cc_data_reserve(cc_state_t *cc, uint32_t bytes);

/* Emit raw bytes into the data segment and return the absolute address.
 * Returns 0 and sets error on overflow.*/
static uint32_t cc_emit_data_bytes(cc_state_t *cc, const uint8_t *bytes,
                                   uint32_t n) {
  /* 4-byte align the data position so float/double live on natural
   * alignment where possible.*/
  cc->data_pos = (cc->data_pos + 3u) & ~3u;
  if (!cc_data_reserve(cc, n))
    return 0;
  uint32_t addr = cc->data_base + cc->data_pos;
  for (uint32_t i = 0; i < n; i++) {
    cc->data[cc->data_pos++] = bytes[i];
  }
  return addr;
}

/* int <-> float <-> double conversion helpers.
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

/* CVTSI2SS xmm, EAX - int in EAX to float in xmm. */
static void emit_cvtsi2ss(cc_state_t *cc, int xmm) {
  emit8(cc, 0xF3);
  emit8(cc, 0x0F);
  emit8(cc, 0x2A);
  /* ModR/M: mod=11, reg=xmm, r/m=000 (EAX). */
  emit8(cc, (uint8_t)(0xC0 | ((xmm & 7) << 3) | 0));
}

/* CVTSI2SD xmm, EAX - int in EAX to double in xmm. */
static void emit_cvtsi2sd(cc_state_t *cc, int xmm) {
  emit8(cc, 0xF2);
  emit8(cc, 0x0F);
  emit8(cc, 0x2A);
  emit8(cc, (uint8_t)(0xC0 | ((xmm & 7) << 3) | 0));
}

/* CVTTSS2SI EAX, xmm - truncating float to int (EAX). */
static void emit_cvttss2si(cc_state_t *cc, int xmm) {
  emit8(cc, 0xF3);
  emit8(cc, 0x0F);
  emit8(cc, 0x2C);
  /* ModR/M: mod=11, reg=000 (EAX), r/m=xmm. */
  emit8(cc, (uint8_t)(0xC0 | (0 << 3) | (xmm & 7)));
}

/* CVTTSD2SI EAX, xmm - truncating double to int (EAX). */
static void emit_cvttsd2si(cc_state_t *cc, int xmm) {
  emit8(cc, 0xF2);
  emit8(cc, 0x0F);
  emit8(cc, 0x2C);
  emit8(cc, (uint8_t)(0xC0 | (0 << 3) | (xmm & 7)));
}

/* Convert a scalar floating value in XMM0 to one unsigned i386 word.
 * Values below 2^31 fit the signed truncation instruction directly. For the
 * upper half, subtract the exact scalar value 2^31, truncate the remainder,
 * and restore bit 31. C defines this conversion for values in (-1, 2^32). */
static void emit_cvtfp_to_ui32(cc_state_t *cc, int is_double) {
  uint32_t lower_half_patch;
  uint32_t done_patch;

  emit_mov_eax_imm(cc, 0x40000000u);
  if (is_double)
    emit_cvtsi2sd(cc, 1);
  else
    emit_cvtsi2ss(cc, 1);
  emit_sse_scalar_op(cc, is_double, 0x58, 1, 1); /* xmm1 = 2^31 */

  if (is_double)
    emit8(cc, 0x66);
  emit8(cc, 0x0F);
  emit8(cc, 0x2E);
  emit8(cc, 0xC1); /* ucomiss/ucomisd xmm0, xmm1 */
  lower_half_patch = emit_jcc_placeholder(cc, 0x82); /* jb */

  emit_sse_scalar_op(cc, is_double, 0x5C, 0, 1);
  if (is_double)
    emit_cvttsd2si(cc, 0);
  else
    emit_cvttss2si(cc, 0);
  emit8(cc, 0x35); /* xor eax, 0x80000000 */
  emit32(cc, 0x80000000u);
  done_patch = emit_jmp_placeholder(cc);

  patch_jump(cc, lower_half_patch);
  if (is_double)
    emit_cvttsd2si(cc, 0);
  else
    emit_cvttss2si(cc, 0);
  patch_jump(cc, done_patch);
}

/* CVTSS2SD xmm_dst, xmm_src - float to double (scalar, in XMM). */
static void emit_cvtss2sd(cc_state_t *cc, int xmm_dst, int xmm_src) {
  emit8(cc, 0xF3);
  emit8(cc, 0x0F);
  emit8(cc, 0x5A);
  emit8(cc, (uint8_t)(0xC0 | ((xmm_dst & 7) << 3) | (xmm_src & 7)));
}

/* CVTSD2SS xmm_dst, xmm_src - double to float (scalar, in XMM). */
static void emit_cvtsd2ss(cc_state_t *cc, int xmm_dst, int xmm_src) {
  emit8(cc, 0xF2);
  emit8(cc, 0x0F);
  emit8(cc, 0x5A);
  emit8(cc, (uint8_t)(0xC0 | ((xmm_dst & 7) << 3) | (xmm_src & 7)));
}

/* Convert the full uint32_t range without treating bit 31 as a sign bit.
 * Halving first leaves a signed-positive value that CVTSI2SD can represent
 * exactly. Reattaching the low bit in double precision reconstructs the
 * original integer exactly. XMM7 is private scratch for this short sequence. */
static void emit_cvtui32_to_sd(cc_state_t *cc, int xmm_dst) {
  emit8(cc, 0x89);
  emit8(cc, 0xC2); /* mov edx, eax */
  emit8(cc, 0xD1);
  emit8(cc, 0xE8); /* shr eax, 1 */
  emit8(cc, 0x83);
  emit8(cc, 0xE2);
  emit8(cc, 0x01); /* and edx, 1 */
  emit_cvtsi2sd(cc, xmm_dst);
  emit_sse_scalar_op(cc, 1, 0x58, xmm_dst, xmm_dst);
  emit8(cc, 0x89);
  emit8(cc, 0xD0); /* mov eax, edx */
  emit_cvtsi2sd(cc, 7);
  emit_sse_scalar_op(cc, 1, 0x58, xmm_dst, 7);
}

static void emit_cvtui32_to_ss(cc_state_t *cc, int xmm_dst) {
  emit_cvtui32_to_sd(cc, xmm_dst);
  emit_cvtsd2ss(cc, xmm_dst, xmm_dst);
}

static void cc_emit_integer_to_fp(cc_state_t *cc, cc_type_t source_type,
                                  cc_type_t target_type, int xmm_dst) {
  if (source_type == TYPE_UINT) {
    if (target_type == TYPE_DOUBLE)
      emit_cvtui32_to_sd(cc, xmm_dst);
    else
      emit_cvtui32_to_ss(cc, xmm_dst);
  } else if (target_type == TYPE_DOUBLE) {
    emit_cvtsi2sd(cc, xmm_dst);
  } else {
    emit_cvtsi2ss(cc, xmm_dst);
  }
}

static double cc_numeric_initializer_value(cc_token_t token, int negate) {
  if (token.type == CC_TOK_FLIT)
    return negate ? -token.fval : token.fval;
  if (token.int_is_unsigned) {
    uint32_t value = (uint32_t)token.int_value;
    if (negate)
      value = 0u - value;
    /* The checked seed builds this compiler before it learns the conversion
     * emitted below. Keep the bootstrap implementation valid for that seed by
     * converting only the signed-positive half and restoring bit 31. */
    double number = (double)(value & 0x7fffffffu);
    if ((value & 0x80000000u) != 0u)
      number = number + 2147483648.0;
    return number;
  }
  return negate ? -(double)token.int_value : (double)token.int_value;
}

static uint32_t cc_numeric_initializer_unsigned_value(cc_token_t token,
                                                       int negate) {
  double number = cc_numeric_initializer_value(token, negate);
  if (!(number > -1.0) || !(number < 4294967296.0))
    return 0u;
  if (number < 2147483648.0)
    return (uint32_t)(int32_t)number;
  return 0x80000000u +
         (uint32_t)(int32_t)(number - 2147483648.0);
}

/* Error Handling */

static void cc_error(cc_state_t *cc, const char *msg) {
  /* Log each failure to serial, but keep the first public diagnostic. Top-level
   * recovery clears cc->error temporarily and leaves error_msg as the record. */
  int line = cc->cur.line;
  if (line == 0) line = cc->line;
  serial_printf("[cupidc] error (line %d): %s\n", line, msg);
  if (cc->error)
    return;
  cc->error = 1;
  if (cc->error_msg[0] != '\0')
    return;

  /* Build error message */
  int i = 0;
  const char *prefix = "CupidC Error (line ";
  while (prefix[i] && i < 100) {
    cc->error_msg[i] = prefix[i];
    i++;
  }

  /* line number (already computed above) */
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

static int cc_patch_data32(cc_state_t *cc, uint32_t offset,
                           uint32_t value) {
  if (offset > cc->data_pos || cc->data_pos - offset < 4u) {
    cc_error(cc, "data patch is outside initialized storage");
    return 0;
  }
  cc->data[offset] = (uint8_t)(value & 0xFF);
  cc->data[offset + 1] = (uint8_t)((value >> 8) & 0xFF);
  cc->data[offset + 2] = (uint8_t)((value >> 16) & 0xFF);
  cc->data[offset + 3] = (uint8_t)((value >> 24) & 0xFF);
  return 1;
}

static int cc_coerce_unsigned_conversion(cc_state_t *cc,
                                         cc_type_t target_type,
                                         cc_type_t source_type) {
  if (target_type == TYPE_UINT &&
      (source_type == TYPE_FLOAT || source_type == TYPE_DOUBLE)) {
    emit_cvtfp_to_ui32(cc, source_type == TYPE_DOUBLE);
    cc_last_expr_type = TYPE_UINT;
  }
  return 1;
}

static int cc_coerce_unsigned_assignment(cc_state_t *cc,
                                         cc_type_t target_type,
                                         cc_type_t source_type,
                                         cc_token_type_t operation) {
  if (target_type == TYPE_UINT && operation != CC_TOK_EQ &&
      (source_type == TYPE_FLOAT || source_type == TYPE_DOUBLE)) {
    cc_error(cc,
             "floating compound assignment to unsigned is not supported");
    return 0;
  }
  return cc_coerce_unsigned_conversion(cc, target_type, source_type);
}

/* Arguments are evaluated and pushed from left to right. This leaves their
 * cdecl blocks in reverse order at ESP. Reorder the complete stack words in
 * place while retaining the low-to-high word order inside each value. */
static int cc_emit_cdecl_argument_layout(cc_state_t *cc,
                                         const int *argument_sizes,
                                         int argument_count) {
  int current_words[CC_MAX_PARAMS * 4];
  int target_words[CC_MAX_PARAMS * 4];
  int word_count = 0;
  int argument;
  int destination;

  if (argument_count < 0 || argument_count > CC_MAX_PARAMS) {
    cc_error(cc, "invalid cdecl argument count");
    return 0;
  }

  for (argument = 0; argument < argument_count; argument++) {
    int size = argument_sizes[argument];
    int word;
    if (size != 4 && size != 8 && size != 16) {
      cc_error(cc, "cdecl argument must occupy four, eight, or sixteen bytes");
      return 0;
    }
    for (word = 0; word < size / 4; word++) {
      target_words[word_count++] = argument * 4 + word;
    }
  }

  destination = 0;
  for (argument = argument_count - 1; argument >= 0; argument--) {
    int word;
    for (word = 0; word < argument_sizes[argument] / 4; word++) {
      current_words[destination++] = argument * 4 + word;
    }
  }

  for (destination = 0; destination < word_count; destination++) {
    int source = destination;
    while (source < word_count &&
           current_words[source] != target_words[destination]) {
      source++;
    }
    if (source >= word_count) {
      cc_error(cc, "cdecl argument layout is inconsistent");
      return 0;
    }
    if (source != destination) {
      int destination_offset = destination * 4;
      int source_offset = source * 4;
      int saved_word = current_words[destination];

      emit8(cc, 0x8B);
      emit8(cc, 0x8C);
      emit8(cc, 0x24);
      emit32(cc, (uint32_t)destination_offset);
      emit8(cc, 0x8B);
      emit8(cc, 0x94);
      emit8(cc, 0x24);
      emit32(cc, (uint32_t)source_offset);
      emit8(cc, 0x89);
      emit8(cc, 0x94);
      emit8(cc, 0x24);
      emit32(cc, (uint32_t)destination_offset);
      emit8(cc, 0x89);
      emit8(cc, 0x8C);
      emit8(cc, 0x24);
      emit32(cc, (uint32_t)source_offset);

      current_words[destination] = current_words[source];
      current_words[source] = saved_word;
    }
  }
  return 1;
}

static int cc_data_reserve(cc_state_t *cc, uint32_t bytes) {
  if (bytes > (CC_MAX_DATA - cc->data_pos)) {
    cc_error(cc, "data section overflow");
    return 0;
  }
  return 1;
}

/* Token Helpers */

static cc_token_t cc_next(cc_state_t *cc) {
  cc_token_t token = cc_lex_next(cc);
  if (token.type == CC_TOK_ERROR)
    cc_error(cc, token.text[0] != '\0' ? token.text : "invalid token");
  return token;
}

static cc_token_t cc_peek(cc_state_t *cc) { return cc_lex_peek(cc); }

typedef struct {
  int pos;
  int line;
  cc_token_t cur;
  cc_token_t peek_buf;
  int has_peek;
} cc_lexer_checkpoint_t;

static void cc_checkpoint_lexer(cc_state_t *cc,
                                cc_lexer_checkpoint_t *checkpoint) {
  checkpoint->pos = cc->pos;
  checkpoint->line = cc->line;
  checkpoint->cur = cc->cur;
  checkpoint->peek_buf = cc->peek_buf;
  checkpoint->has_peek = cc->has_peek;
}

static void cc_restore_lexer(cc_state_t *cc,
                             const cc_lexer_checkpoint_t *checkpoint) {
  cc->pos = checkpoint->pos;
  cc->line = checkpoint->line;
  cc->cur = checkpoint->cur;
  cc->peek_buf = checkpoint->peek_buf;
  cc->has_peek = checkpoint->has_peek;
}

static int cc_expect(cc_state_t *cc, cc_token_type_t type) {
  cc_token_t tok = cc_next(cc);
  if (tok.type != type) {
    /* Include the bad token's text and a numeric type tag so callers
     * can diagnose which token cupidc choked on. The previous bare
     * "unexpected token" was not actionable.*/
    char buf[96];
    int p = 0;
    const char *prefix = "unexpected token: '";
    while (prefix[p] && p < 19) { buf[p] = prefix[p]; p++; }
    int t = 0;
    while (tok.text[t] && p < 90 && t < 64) { buf[p++] = tok.text[t++]; }
    buf[p++] = '\'';
    buf[p++] = ' ';
    buf[p++] = '(';
    buf[p++] = 't';
    buf[p++] = 'y';
    buf[p++] = 'p';
    buf[p++] = 'e';
    buf[p++] = '=';
    /* Append type as decimal */
    int tt = (int)tok.type;
    char num[12];
    int n = 0;
    if (tt == 0) { num[n++] = '0'; }
    else {
        int neg = 0;
        if (tt < 0) { neg = 1; tt = -tt; }
        char rev[12]; int r = 0;
        while (tt > 0 && r < 11) { rev[r++] = (char)('0' + (tt % 10)); tt /= 10; }
        if (neg) num[n++] = '-';
        while (r > 0) num[n++] = rev[--r];
    }
    for (int k = 0; k < n && p < 94; k++) buf[p++] = num[k];
    buf[p++] = ')';
    buf[p] = 0;
    cc_error(cc, buf);
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

/* Store one C string literal, including every adjacent literal token.
 * Individual tokens stay bounded so lexer state remains compact, while the
 * joined string can use the compiler's full data section. */
static uint32_t cc_emit_adjacent_string_literal(cc_state_t *cc,
                                                cc_token_t first) {
  uint32_t str_addr = cc->data_base + cc->data_pos;
  cc_token_t part = first;
  for (;;) {
    uint32_t length = (uint32_t)part.int_value;
    /* Keep one byte available for the final terminator at every step. */
    if (!cc_data_reserve(cc, length + 1u))
      return 0;
    for (uint32_t i = 0; i < length; i++) {
      cc->data[cc->data_pos++] = (uint8_t)part.text[i];
    }
    if (cc_peek(cc).type != CC_TOK_STRING)
      break;
    part = cc_next(cc);
  }
  cc->data[cc->data_pos++] = 0;
  return str_addr;
}

static void cc_skip_attributes(cc_state_t *cc) {
  while (cc_peek(cc).type == CC_TOK_ATTRIBUTE) {
    cc_next(cc);
    if (cc_peek(cc).type == CC_TOK_LPAREN) {
      int depth = 0;
      do {
        cc_token_t t = cc_next(cc);
        if (t.type == CC_TOK_LPAREN)
          depth++;
        else if (t.type == CC_TOK_RPAREN)
          depth--;
        else if (t.type == CC_TOK_EOF) {
          cc_error(cc, "unterminated attribute");
          return;
        }
      } while (depth > 0 && !cc->error);
    }
  }
}

static int cc_is_type_prefix(cc_token_type_t t) {
  return t == CC_TOK_CONST || t == CC_TOK_UNSIGNED ||
         t == CC_TOK_SIGNED || t == CC_TOK_LONG || t == CC_TOK_SHORT ||
         t == CC_TOK_VOLATILE || t == CC_TOK_REG ||
         t == CC_TOK_NOREG || t == CC_TOK_EXTERN ||
         t == CC_TOK_INLINE || t == CC_TOK_REGISTER ||
         t == CC_TOK_RESTRICT || t == CC_TOK_STATIC ||
         t == CC_TOK_ATTRIBUTE;
}

static int cc_is_concrete_type(cc_token_type_t t) {
  return t == CC_TOK_INT || t == CC_TOK_CHAR || t == CC_TOK_VOID ||
         t == CC_TOK_U0 || t == CC_TOK_U8 || t == CC_TOK_U16 ||
         t == CC_TOK_U32 || t == CC_TOK_U64 ||
         t == CC_TOK_I8 || t == CC_TOK_I16 ||
         t == CC_TOK_I32 || t == CC_TOK_I64 ||
         t == CC_TOK_FLOAT || t == CC_TOK_DOUBLE ||
         t == CC_TOK_FLOAT4 || t == CC_TOK_DOUBLE2 ||
         t == CC_TOK_STRUCT || t == CC_TOK_BOOL;
}

static int cc_is_type(cc_token_type_t t) {
  return cc_is_concrete_type(t) || cc_is_type_prefix(t);
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

static int cc_find_typedef_index(cc_state_t *cc, const char *name) {
  int i;
  for (i = 0; i < cc->typedef_count; i++) {
    if (strcmp(cc->typedef_names[i], name) == 0)
      return i;
  }
  return -1;
}

static int cc_find_typedef_struct_index(cc_state_t *cc, const char *name) {
  int i;
  for (i = 0; i < cc->typedef_count; i++) {
    if (strcmp(cc->typedef_names[i], name) == 0)
      return cc->typedef_struct_indices[i];
  }
  return -1;
}

static int cc_find_typedef_array_count(cc_state_t *cc, const char *name) {
  int i;
  for (i = 0; i < cc->typedef_count; i++) {
    if (strcmp(cc->typedef_names[i], name) == 0)
      return cc->typedef_array_counts[i];
  }
  return 0;
}

static int cc_find_typedef_is_const_qualified(cc_state_t *cc,
                                              const char *name) {
  int i;
  for (i = 0; i < cc->typedef_count; i++) {
    if (strcmp(cc->typedef_names[i], name) == 0)
      return cc->typedef_is_const_qualified[i];
  }
  return 0;
}

static void cc_add_typedef_alias(cc_state_t *cc, const char *name,
                                 cc_type_t type, int struct_index,
                                 int array_count, int is_const_qualified) {
  if (cc->typedef_count >= CC_MAX_TYPEDEFS) {
    cc_error(cc, "too many typedef aliases");
    return;
  }

  int name_index = 0;
  while (name[name_index] && name_index < CC_MAX_IDENT - 1) {
    cc->typedef_names[cc->typedef_count][name_index] = name[name_index];
    name_index++;
  }
  cc->typedef_names[cc->typedef_count][name_index] = '\0';
  cc->typedef_types[cc->typedef_count] = type;
  cc->typedef_struct_indices[cc->typedef_count] = struct_index;
  cc->typedef_array_counts[cc->typedef_count] = array_count;
  cc->typedef_is_const_qualified[cc->typedef_count] = is_const_qualified;
  cc->typedef_function_pointer_signature_valid[cc->typedef_count] = 0;
  cc->typedef_function_pointer_return_types[cc->typedef_count] = TYPE_VOID;
  cc->typedef_function_pointer_return_struct_indices[cc->typedef_count] = -1;
  cc->typedef_function_pointer_param_counts[cc->typedef_count] = 0;
  memset(cc->typedef_function_pointer_param_types[cc->typedef_count], 0,
         sizeof(cc->typedef_function_pointer_param_types[cc->typedef_count]));
  memset(cc->typedef_function_pointer_param_struct_indices[cc->typedef_count],
         -1,
         sizeof(cc->typedef_function_pointer_param_struct_indices[
             cc->typedef_count]));
  cc->typedef_function_pointer_has_param_types[cc->typedef_count] = 0;
  cc->typedef_function_pointer_is_variadic[cc->typedef_count] = 0;
  cc->typedef_count++;
}

static int cc_copy_function_pointer_typedef_signature(
    cc_state_t *cc, int typedef_index, cc_symbol_t *symbol) {
  if (!symbol || typedef_index < 0 || typedef_index >= cc->typedef_count ||
      !cc->typedef_function_pointer_signature_valid[typedef_index])
    return 0;
  symbol->function_pointer_return_type =
      cc->typedef_function_pointer_return_types[typedef_index];
  symbol->struct_index =
      cc->typedef_function_pointer_return_struct_indices[typedef_index];
  symbol->param_count =
      cc->typedef_function_pointer_param_counts[typedef_index];
  memcpy(symbol->param_types,
         cc->typedef_function_pointer_param_types[typedef_index],
         sizeof(symbol->param_types));
  memcpy(symbol->param_struct_indices,
         cc->typedef_function_pointer_param_struct_indices[typedef_index],
         sizeof(symbol->param_struct_indices));
  symbol->has_param_types =
      cc->typedef_function_pointer_has_param_types[typedef_index];
  symbol->is_variadic =
      cc->typedef_function_pointer_is_variadic[typedef_index];
  return 1;
}

static int cc_get_function_pointer_signature(
    cc_state_t *cc, int signature_handle,
    cc_function_pointer_signature_t *signature) {
  int raw_index;
  if (!signature)
    return 0;
  if (signature_handle >= 0 &&
      signature_handle < CC_RAW_FUNCTION_POINTER_SIGNATURE_BASE) {
    if (signature_handle >= cc->typedef_count ||
        !cc->typedef_function_pointer_signature_valid[signature_handle])
      return 0;
    signature->return_type =
        cc->typedef_function_pointer_return_types[signature_handle];
    signature->return_struct_index =
        cc->typedef_function_pointer_return_struct_indices[signature_handle];
    signature->param_count =
        cc->typedef_function_pointer_param_counts[signature_handle];
    memcpy(signature->param_types,
           cc->typedef_function_pointer_param_types[signature_handle],
           sizeof(signature->param_types));
    memcpy(
        signature->param_struct_indices,
        cc->typedef_function_pointer_param_struct_indices[signature_handle],
        sizeof(signature->param_struct_indices));
    signature->has_param_types =
        cc->typedef_function_pointer_has_param_types[signature_handle];
    signature->is_variadic =
        cc->typedef_function_pointer_is_variadic[signature_handle];
    return 1;
  }
  raw_index =
      signature_handle - CC_RAW_FUNCTION_POINTER_SIGNATURE_BASE;
  if (raw_index < 0 ||
      raw_index >= cc->raw_function_pointer_signature_count)
    return 0;
  *signature = cc->raw_function_pointer_signatures[raw_index];
  return 1;
}

static int cc_copy_function_pointer_signature_handle(
    cc_state_t *cc, int signature_handle, cc_symbol_t *symbol) {
  cc_function_pointer_signature_t signature;
  if (!symbol ||
      !cc_get_function_pointer_signature(cc, signature_handle, &signature))
    return 0;
  symbol->function_pointer_return_type = signature.return_type;
  symbol->struct_index = signature.return_struct_index;
  symbol->param_count = signature.param_count;
  memcpy(symbol->param_types, signature.param_types,
         sizeof(symbol->param_types));
  memcpy(symbol->param_struct_indices, signature.param_struct_indices,
         sizeof(symbol->param_struct_indices));
  symbol->has_param_types = signature.has_param_types;
  symbol->is_variadic = signature.is_variadic;
  return 1;
}

static int cc_function_pointer_signature_handles_match(
    cc_state_t *cc, int left_handle, int right_handle) {
  cc_function_pointer_signature_t left;
  cc_function_pointer_signature_t right;
  int parameter_index;
  if (!cc_get_function_pointer_signature(cc, left_handle, &left) ||
      !cc_get_function_pointer_signature(cc, right_handle, &right))
    return 1;
  if (left.return_type != right.return_type ||
      (left.return_type == TYPE_STRUCT_PTR &&
       left.return_struct_index != right.return_struct_index))
    return 0;
  if (!left.has_param_types || !right.has_param_types)
    return 1;
  if (left.param_count != right.param_count ||
      left.is_variadic != right.is_variadic)
    return 0;
  for (parameter_index = 0; parameter_index < left.param_count;
       parameter_index++) {
    if (left.param_types[parameter_index] !=
            right.param_types[parameter_index] ||
        (left.param_types[parameter_index] == TYPE_STRUCT_PTR &&
         left.param_struct_indices[parameter_index] !=
             right.param_struct_indices[parameter_index]))
      return 0;
  }
  return 1;
}

static int cc_find_struct(cc_state_t *cc, const char *name);

static int cc_is_type_or_typedef(cc_state_t *cc, cc_token_t tok) {
  return cc_is_type(tok.type) ||
         (tok.type == CC_TOK_IDENT &&
          ((int)cc_find_typedef(cc, tok.text) >= 0 ||
           cc_find_struct(cc, tok.text) >= 0));
}

/* Track what kind of value the last expression produced */
static int cc_last_expr_struct_index; /* which struct, if TYPE_STRUCT */
static int cc_last_expr_indirect_lvalue;
static cc_symbol_t *cc_last_expr_direct_lvalue_sym;
static cc_symbol_t *cc_last_expr_function_signature_sym;
static cc_symbol_t *
    cc_last_expr_function_signature_candidates[CC_MAX_PARAMS];
static int cc_last_expr_function_signature_count;
static int cc_last_expr_function_signature_erased;
static int cc_last_expr_is_null_pointer_constant;
static int cc_last_expr_is_integer_constant_expression;
static uint32_t cc_last_expr_integer_constant_value;
static int cc_last_expr_integer_constant_is_unsigned;
static int cc_last_expr_simd_lane;
static int cc_last_expr_const_lvalue;
/* Inner-dimension stride for 3D-array expressions. When > 0, the
 * current expression still has another dimension to index into and
 * cc_last_expr_elem_size is the FIRST stride (rows); cc_last_expr_dim2
 * is the SECOND stride (middle). After a single [i] subscript on a 3D
 * array the parser propagates dim2 -> elem_size and zeroes dim2 so the
 * next [j] takes the right stride.*/
static int cc_last_expr_dim2;
/* Fixed-array rank remains semantic even when a row contains one element and
 * therefore has the same byte size as its leaf. Each subscript consumes one
 * rank before the final scalar or vector load. */
static int cc_last_expr_array_rank;
static int cc_last_type_struct_index; /* set by cc_parse_type */
static cc_type_t cc_last_type_base;
static int cc_last_type_pointer_depth;
static int cc_last_type_array_count;
static int cc_last_type_is_const_qualified;
static int cc_last_type_typedef_index;
static int cc_last_expr_elem_size;    /* element size for array subscripts */
/* Size of the array object produced by the most recent subscript. The next
 * subscript uses cc_last_expr_elem_size as its stride, while sizeof needs the
 * complete remaining row. */
static int cc_last_expr_array_object_size;
/* The pointer-shaped expression type cannot distinguish int, float, and
 * double fixed arrays. Carry the declared element type alongside the base
 * expression until the first subscript materializes the scalar value. */
static cc_type_t cc_last_expr_array_elem_type;
static int cc_expression_depth;
static int cc_sizeof_simd_row_depth;
static int cc_grouped_simd_row_depth;

static void cc_clear_expr_function_signatures(void) {
  cc_last_expr_function_signature_sym = NULL;
  cc_last_expr_function_signature_count = 0;
}

static void cc_clear_expr_callable_provenance(void) {
  cc_clear_expr_function_signatures();
  cc_last_expr_function_signature_erased = 0;
  cc_last_expr_is_null_pointer_constant = 0;
  cc_last_expr_is_integer_constant_expression = 0;
  cc_last_expr_integer_constant_value = 0;
  cc_last_expr_integer_constant_is_unsigned = 0;
}

static void cc_publish_integer_constant_expression(uint32_t value,
                                                   int is_unsigned) {
  cc_last_expr_is_integer_constant_expression = 1;
  cc_last_expr_integer_constant_value = value;
  cc_last_expr_integer_constant_is_unsigned = is_unsigned;
  cc_last_expr_is_null_pointer_constant = value == 0;
}

static int cc_append_expr_function_signature(cc_symbol_t *symbol) {
  int candidate_index;

  if (!symbol)
    return 1;
  for (candidate_index = 0;
       candidate_index < cc_last_expr_function_signature_count;
       candidate_index++) {
    if (cc_last_expr_function_signature_candidates[candidate_index] == symbol)
      return 1;
  }
  if (cc_last_expr_function_signature_count >= CC_MAX_PARAMS)
    return 0;
  cc_last_expr_function_signature_candidates
      [cc_last_expr_function_signature_count++] = symbol;
  cc_last_expr_function_signature_sym =
      cc_last_expr_function_signature_candidates[0];
  return 1;
}

static void cc_set_expr_function_signature(cc_symbol_t *symbol) {
  cc_clear_expr_function_signatures();
  (void)cc_append_expr_function_signature(symbol);
}

static int cc_has_incomplete_simd_row(void) {
  return cc_last_expr_array_rank > 0 &&
         (cc_last_expr_array_elem_type == TYPE_FLOAT4 ||
          cc_last_expr_array_elem_type == TYPE_DOUBLE2);
}

static int cc_reject_incomplete_simd_row(cc_state_t *cc) {
  if (!cc_has_incomplete_simd_row())
    return 0;
  cc_error(cc, "SIMD array row values are not supported");
  return 1;
}

static int cc_is_object_pointer_type(cc_type_t type) {
  return type == TYPE_PTR || type == TYPE_INT_PTR ||
         type == TYPE_UINT_PTR ||
         type == TYPE_CHAR_PTR || type == TYPE_STRUCT_PTR ||
         type == TYPE_FLOAT_PTR || type == TYPE_DOUBLE_PTR;
}

static cc_type_t cc_pointed_object_type(cc_type_t type) {
  if (type == TYPE_CHAR_PTR)
    return TYPE_CHAR;
  if (type == TYPE_STRUCT_PTR)
    return TYPE_STRUCT;
  if (type == TYPE_FLOAT_PTR)
    return TYPE_FLOAT;
  if (type == TYPE_DOUBLE_PTR)
    return TYPE_DOUBLE;
  if (type == TYPE_UINT_PTR)
    return TYPE_UINT;
  if (type == TYPE_INT_PTR || type == TYPE_PTR || type == TYPE_FUNC_PTR)
    return TYPE_INT;
  return TYPE_VOID;
}

static cc_type_t cc_object_pointer_type(cc_type_t type) {
  if (type == TYPE_CHAR)
    return TYPE_CHAR_PTR;
  if (type == TYPE_STRUCT)
    return TYPE_STRUCT_PTR;
  if (type == TYPE_FLOAT)
    return TYPE_FLOAT_PTR;
  if (type == TYPE_DOUBLE)
    return TYPE_DOUBLE_PTR;
  if (type == TYPE_UINT)
    return TYPE_UINT_PTR;
  if (type == TYPE_INT)
    return TYPE_INT_PTR;
  return TYPE_PTR;
}

static int cc_parse_const_int_expr(cc_state_t *cc, int32_t *out);

static cc_type_t cc_apply_pointer_declarator(cc_state_t *cc,
                                             cc_type_t base,
                                             int pointer_depth) {
  if (pointer_depth <= 0)
    return base;
  if (base == TYPE_FLOAT4 || base == TYPE_DOUBLE2) {
    cc_error(cc, "SIMD pointer types are not supported");
    return TYPE_PTR;
  }
  if (pointer_depth == 1)
    return cc_object_pointer_type(base);
  if (base == TYPE_FLOAT || base == TYPE_DOUBLE) {
    cc_error(cc, "floating pointer depth greater than one is not supported");
    return TYPE_PTR;
  }
  return TYPE_PTR;
}

static cc_type_t cc_decay_array_parameter_type(cc_state_t *cc,
                                                cc_type_t element_type) {
  if (element_type == TYPE_FLOAT4 || element_type == TYPE_DOUBLE2) {
    cc_error(cc, "SIMD array parameters are not supported");
    return TYPE_PTR;
  }
  return cc_object_pointer_type(element_type);
}

static cc_type_t cc_adjust_array_parameter_declarator(
    cc_state_t *cc, cc_type_t element_type, int typedef_array_count) {
  if (typedef_array_count > 0) {
    if (cc_peek(cc).type == CC_TOK_LBRACK) {
      cc_error(cc, "pointer to typedef array is not supported");
      return TYPE_PTR;
    }
    return cc_decay_array_parameter_type(cc, element_type);
  }
  if (cc_peek(cc).type != CC_TOK_LBRACK)
    return element_type;

  cc_next(cc);
  if (cc_peek(cc).type != CC_TOK_RBRACK) {
    int32_t ignored_count;
    cc_parse_const_int_expr(cc, &ignored_count);
  }
  cc_expect(cc, CC_TOK_RBRACK);
  return cc_decay_array_parameter_type(cc, element_type);
}

static void cc_reset_expr_subscript_metadata(void) {
  cc_last_expr_dim2 = 0;
  cc_last_expr_array_rank = 0;
  cc_last_expr_elem_size = 0;
  cc_last_expr_array_object_size = 0;
  cc_last_expr_array_elem_type = TYPE_INT;
  cc_last_expr_simd_lane = 0;
}

/* A call or cast returns a scalar type rather than an array symbol, but a
 * following subscript still needs the ordinary pointee stride. Keep that
 * baseline separate from fixed-array metadata so an unrelated array
 * expression cannot leak its wider stride into the result. */
static void cc_seed_pointer_subscript_metadata(cc_state_t *cc,
                                               cc_type_t type,
                                               int struct_index) {
  cc_reset_expr_subscript_metadata();
  cc_last_expr_struct_index = struct_index;
  if (type == TYPE_CHAR_PTR) {
    cc_last_expr_elem_size = 1;
    cc_last_expr_array_elem_type = TYPE_CHAR;
  } else if (type == TYPE_FLOAT_PTR) {
    cc_last_expr_elem_size = 4;
    cc_last_expr_array_elem_type = TYPE_FLOAT;
  } else if (type == TYPE_DOUBLE_PTR) {
    cc_last_expr_elem_size = 8;
    cc_last_expr_array_elem_type = TYPE_DOUBLE;
  } else if (type == TYPE_UINT_PTR) {
    cc_last_expr_elem_size = 4;
    cc_last_expr_array_elem_type = TYPE_UINT;
  } else if (type == TYPE_INT_PTR || type == TYPE_PTR ||
             type == TYPE_FUNC_PTR) {
    cc_last_expr_elem_size = 4;
  } else if (type == TYPE_STRUCT_PTR) {
    if (struct_index >= 0 && struct_index < cc->struct_count &&
        cc->structs[struct_index].total_size > 0)
      cc_last_expr_elem_size = cc->structs[struct_index].total_size;
    else
      cc_last_expr_elem_size = 4;
  }
}

/* XMM register allocator for floating-point expression evaluation.
 * Reset at the start of each function. XMM0-7 available. Spilling (when
 * all 8 are in use) is not implemented - any expression too complex for
 * 8 XMMs will cc_error.  In the current Task-16 scheme only XMM0/XMM1
 * are actually used, but we keep the general allocator ready for later
 * callers (SIMD, libm).*/
static uint8_t cc_xmm_inuse = 0;
/* Which XMM register holds the current FP expression result (mirrors EAX).
 * Generally XMM0 in scalar codegen. Kept for SIMD.*/
__attribute__((unused))
static int cc_last_xmm = 0;

/* Currently unused in scalar codegen (all FP ops run through XMM0/XMM1
 * with spill-to-stack) but exist for SIMD codegen.*/
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

/* Forward declarations needed by the struct-body path inside cc_parse_type.
 * The helpers are defined further down. */
static int32_t cc_align_up(int32_t value, int32_t align);
static int32_t cc_type_align(cc_state_t *cc, cc_type_t type, int struct_index);
static int32_t cc_type_size(cc_state_t *cc, cc_type_t type, int struct_index);
static int cc_checked_array_bytes(cc_state_t *cc, int32_t count,
                                  int32_t stride, int32_t *out);
static cc_type_t cc_parse_type(cc_state_t *cc);

static int cc_checked_record_field_layout(cc_state_t *cc,
                                          int32_t current_size,
                                          int32_t field_size,
                                          int32_t field_align,
                                          int32_t *field_offset,
                                          int32_t *next_size) {
  /* Every record allocation is rounded to a four-byte boundary. Keep the
   * unrounded size inside the range where that final alignment is safe. */
  const int32_t max_size = 0x7ffffffc;
  if (current_size < 0 || field_size < 0 || field_align <= 0) {
    cc_error(cc, "record field size is invalid");
    return 0;
  }
  int32_t remainder = current_size % field_align;
  int32_t padding = remainder == 0 ? 0 : field_align - remainder;
  if (current_size > max_size - padding) {
    cc_error(cc, "record size overflow");
    return 0;
  }
  int32_t aligned_size = current_size + padding;
  if (field_size > max_size - aligned_size) {
    cc_error(cc, "record size overflow");
    return 0;
  }
  *field_offset = aligned_size;
  *next_size = aligned_size + field_size;
  return 1;
}

/* Parse a compile-time integer constant expression. Accepts numeric
 * literals combined with + - * / and parentheses. Used for array sizes
 * so `char buf[4 + 32768]` and friends parse cleanly. Returns 1 on
 * success and stores result via *out; 0 on parse error.*/
typedef struct {
  int32_t value;
  int is_unsigned;
} cc_const_int_value_t;

static int cc_last_const_int_is_unsigned;
static int cc_parse_const_int_value_expr(cc_state_t *cc,
                                         cc_const_int_value_t *out);
static int cc_parse_const_int_expr(cc_state_t *cc, int32_t *out);

static int cc_const_int_overflow(cc_state_t *cc) {
  cc_error(cc, "constant integer expression overflow");
  return 0;
}

static int cc_checked_const_int_negate(cc_state_t *cc, int32_t value,
                                       int32_t *out) {
  const int32_t min_value = (-2147483647 - 1);
  if (value == min_value)
    return cc_const_int_overflow(cc);
  *out = -value;
  return 1;
}

static int cc_checked_const_int_add(cc_state_t *cc, int32_t lhs, int32_t rhs,
                                    int32_t *out) {
  const int32_t min_value = (-2147483647 - 1);
  const int32_t max_value = 2147483647;
  if ((rhs > 0 && lhs > max_value - rhs) ||
      (rhs < 0 && lhs < min_value - rhs))
    return cc_const_int_overflow(cc);
  *out = lhs + rhs;
  return 1;
}

static int cc_checked_const_int_subtract(cc_state_t *cc, int32_t lhs,
                                         int32_t rhs, int32_t *out) {
  const int32_t min_value = (-2147483647 - 1);
  const int32_t max_value = 2147483647;
  if ((rhs < 0 && lhs > max_value + rhs) ||
      (rhs > 0 && lhs < min_value + rhs))
    return cc_const_int_overflow(cc);
  *out = lhs - rhs;
  return 1;
}

static int cc_checked_const_int_multiply(cc_state_t *cc, int32_t lhs,
                                         int32_t rhs, int32_t *out) {
  const int32_t min_value = (-2147483647 - 1);
  const int32_t max_value = 2147483647;
  if (lhs == 0 || rhs == 0) {
    *out = 0;
    return 1;
  }
  if (lhs > 0) {
    if ((rhs > 0 && lhs > max_value / rhs) ||
        (rhs < 0 && rhs < min_value / lhs))
      return cc_const_int_overflow(cc);
  } else {
    if ((rhs > 0 && lhs < min_value / rhs) ||
        (rhs < 0 && lhs < max_value / rhs))
      return cc_const_int_overflow(cc);
  }
  *out = lhs * rhs;
  return 1;
}

static int cc_checked_const_int_divide(cc_state_t *cc, int32_t lhs,
                                       int32_t rhs, int32_t *out) {
  const int32_t min_value = (-2147483647 - 1);
  if (rhs == 0) {
    cc_error(cc, "constant integer expression division by zero");
    return 0;
  }
  if (lhs == min_value && rhs == -1)
    return cc_const_int_overflow(cc);
  *out = lhs / rhs;
  return 1;
}

static int cc_apply_const_int_negate(cc_state_t *cc,
                                     cc_const_int_value_t value,
                                     cc_const_int_value_t *out) {
  out->is_unsigned = value.is_unsigned;
  if (value.is_unsigned) {
    out->value = (int32_t)(0u - (uint32_t)value.value);
    return 1;
  }
  return cc_checked_const_int_negate(cc, value.value, &out->value);
}

static int cc_apply_const_int_binary(cc_state_t *cc, cc_token_type_t op,
                                     cc_const_int_value_t lhs,
                                     cc_const_int_value_t rhs,
                                     cc_const_int_value_t *out) {
  out->is_unsigned = lhs.is_unsigned || rhs.is_unsigned;
  if (out->is_unsigned) {
    uint32_t left = (uint32_t)lhs.value;
    uint32_t right = (uint32_t)rhs.value;
    if (op == CC_TOK_PLUS)
      out->value = (int32_t)(left + right);
    else if (op == CC_TOK_MINUS)
      out->value = (int32_t)(left - right);
    else if (op == CC_TOK_STAR)
      out->value = (int32_t)(left * right);
    else {
      if (right == 0u) {
        cc_error(cc, "constant integer expression division by zero");
        return 0;
      }
      out->value = (int32_t)(left / right);
    }
    return 1;
  }
  if (op == CC_TOK_PLUS)
    return cc_checked_const_int_add(cc, lhs.value, rhs.value, &out->value);
  if (op == CC_TOK_MINUS)
    return cc_checked_const_int_subtract(cc, lhs.value, rhs.value,
                                         &out->value);
  if (op == CC_TOK_STAR)
    return cc_checked_const_int_multiply(cc, lhs.value, rhs.value,
                                         &out->value);
  return cc_checked_const_int_divide(cc, lhs.value, rhs.value, &out->value);
}

static int cc_parse_const_int_value_primary(cc_state_t *cc,
                                            cc_const_int_value_t *out) {
  if (cc_peek(cc).type == CC_TOK_LPAREN) {
    cc_next(cc);
    if (!cc_parse_const_int_value_expr(cc, out)) return 0;
    if (!cc_expect(cc, CC_TOK_RPAREN)) return 0;
    return 1;
  }
  if (cc_peek(cc).type == CC_TOK_MINUS) {
    cc_next(cc);
    cc_const_int_value_t value;
    if (!cc_parse_const_int_value_primary(cc, &value)) return 0;
    return cc_apply_const_int_negate(cc, value, out);
  }
  cc_token_t t = cc_next(cc);
  if (t.type == CC_TOK_NUMBER) {
    out->value = t.int_value;
    out->is_unsigned = t.int_is_unsigned;
    return 1;
  }
  if (t.type == CC_TOK_IDENT) {
    cc_symbol_t *s = cc_sym_find(cc, t.text);
    if (s && s->is_const_int) {
      out->value = s->const_int_value;
      out->is_unsigned = s->const_int_is_unsigned;
      return 1;
    }
  }
  cc_error(cc, "expected constant integer");
  return 0;
}

static int cc_parse_const_int_value_mul(cc_state_t *cc,
                                        cc_const_int_value_t *out) {
  cc_const_int_value_t lhs;
  if (!cc_parse_const_int_value_primary(cc, &lhs)) return 0;
  while (cc_peek(cc).type == CC_TOK_STAR || cc_peek(cc).type == CC_TOK_SLASH) {
    cc_token_t op = cc_next(cc);
    cc_const_int_value_t rhs;
    cc_const_int_value_t result;
    if (!cc_parse_const_int_value_primary(cc, &rhs)) return 0;
    if (!cc_apply_const_int_binary(cc, op.type, lhs, rhs, &result)) return 0;
    lhs = result;
  }
  *out = lhs;
  return 1;
}

static int cc_parse_const_int_value_expr(cc_state_t *cc,
                                         cc_const_int_value_t *out) {
  cc_const_int_value_t lhs;
  if (!cc_parse_const_int_value_mul(cc, &lhs)) return 0;
  while (cc_peek(cc).type == CC_TOK_PLUS || cc_peek(cc).type == CC_TOK_MINUS) {
    cc_token_t op = cc_next(cc);
    cc_const_int_value_t rhs;
    cc_const_int_value_t result;
    if (!cc_parse_const_int_value_mul(cc, &rhs)) return 0;
    if (!cc_apply_const_int_binary(cc, op.type, lhs, rhs, &result)) return 0;
    lhs = result;
  }
  *out = lhs;
  return 1;
}

static int cc_parse_const_int_expr(cc_state_t *cc, int32_t *out) {
  cc_const_int_value_t value;
  if (!cc_parse_const_int_value_expr(cc, &value)) return 0;
  *out = value.value;
  cc_last_const_int_is_unsigned = value.is_unsigned;
  return 1;
}

static void cc_discard_array_declarator_suffixes(cc_state_t *cc) {
  while (cc_peek(cc).type == CC_TOK_LBRACK) {
    cc_next(cc);
    while (cc_peek(cc).type != CC_TOK_RBRACK &&
           cc_peek(cc).type != CC_TOK_EOF)
      cc_next(cc);
    if (cc_peek(cc).type == CC_TOK_RBRACK)
      cc_next(cc);
  }
}

static int cc_parse_typedef_array_declarator(cc_state_t *cc,
                                             cc_type_t element_type,
                                             int struct_index,
                                             int inherited_count,
                                             int *array_count) {
  *array_count = inherited_count;
  if (cc_peek(cc).type != CC_TOK_LBRACK)
    return 1;
  if (inherited_count > 0) {
    cc_error(cc, "multidimensional typedef arrays are not supported");
    cc_discard_array_declarator_suffixes(cc);
    return 0;
  }

  cc_next(cc);
  if (cc_peek(cc).type == CC_TOK_RBRACK) {
    cc_next(cc);
    cc_error(cc, "typedef array size is required");
    return 0;
  }
  int32_t count;
  if (!cc_parse_const_int_expr(cc, &count)) {
    cc_error(cc, "expected array size");
    return 0;
  }
  if (!cc_expect(cc, CC_TOK_RBRACK))
    return 0;
  if (count <= 0) {
    cc_error(cc, "array size must be positive");
    return 0;
  }
  if (cc_peek(cc).type == CC_TOK_LBRACK) {
    cc_error(cc, "multidimensional typedef arrays are not supported");
    cc_discard_array_declarator_suffixes(cc);
    return 0;
  }
  if (element_type == TYPE_STRUCT &&
      !cc_struct_is_complete(cc, struct_index)) {
    cc_error(cc, "array of incomplete struct type");
    return 0;
  }
  int32_t element_size = cc_type_size(cc, element_type, struct_index);
  int32_t total_size;
  if (element_size <= 0) {
    cc_error(cc, "invalid array element type");
    return 0;
  }
  if (!cc_checked_array_bytes(cc, count, element_size, &total_size))
    return 0;
  *array_count = count;
  return 1;
}

/* Parse a struct body after its opening brace. Both anonymous and tagged
 * typedef declarations use this path so their layout and diagnostics agree. */
static int cc_parse_struct_body(cc_state_t *cc, int struct_index) {
  if (struct_index < 0 || struct_index >= cc->struct_count)
    return 0;
  cc_struct_def_t *sd = &cc->structs[struct_index];
  if (sd->is_complete) {
    cc_error(cc, "redefinition of struct");
    return 0;
  }

  sd->field_count = 0;
  sd->total_size = 0;
  sd->align = 1;
  sd->is_complete = 0;
  int32_t field_offset = 0;
  int32_t struct_align = 1;
  while (!cc->error && cc_peek(cc).type != CC_TOK_RBRACE &&
         cc_peek(cc).type != CC_TOK_EOF) {
    if (sd->field_count >= CC_MAX_FIELDS) {
      cc_error(cc, "too many fields in struct");
      return 0;
    }
    cc_type_t field_type = cc_parse_type(cc);
    int field_struct_index = cc_last_type_struct_index;
    int field_array_count = cc_last_type_array_count;
    cc_token_t field_name = cc_next(cc);
    if (field_name.type != CC_TOK_IDENT) {
      cc_error(cc, "expected field name");
      return 0;
    }

    cc_field_t *field = &sd->fields[sd->field_count++];
    int name_index = 0;
    while (field_name.text[name_index] && name_index < CC_MAX_IDENT - 1) {
      field->name[name_index] = field_name.text[name_index];
      name_index++;
    }
    field->name[name_index] = '\0';
    field->type = field_type;
    field->struct_index = field_struct_index;
    field->array_count = field_array_count;

    if (cc_peek(cc).type == CC_TOK_LBRACK) {
      if (field_array_count > 0) {
        cc_error(cc,
                 "array declarator after typedef array is not supported");
        return 0;
      }
      cc_next(cc);
      int32_t array_count;
      if (!cc_parse_const_int_expr(cc, &array_count)) {
        cc_error(cc, "expected array size");
        return 0;
      }
      if (array_count <= 0) {
        cc_error(cc, "array size must be positive");
        return 0;
      }
      field->array_count = array_count;
      if (!cc_expect(cc, CC_TOK_RBRACK))
        return 0;
    }
    if (field_type == TYPE_STRUCT &&
        !cc_struct_is_complete(cc, field_struct_index)) {
      cc_error(cc, "field has incomplete struct type");
      return 0;
    }
    if (field->array_count > 0 &&
        (field_type == TYPE_FLOAT4 || field_type == TYPE_DOUBLE2)) {
      cc_error(cc, "SIMD struct field arrays are not supported");
      return 0;
    }

    int32_t element_size =
        cc_type_size(cc, field_type, field_struct_index);
    int32_t field_align =
        cc_type_align(cc, field_type, field_struct_index);
    int32_t field_size = element_size;
    if (field->array_count > 0 &&
        !cc_checked_array_bytes(cc, field->array_count,
                                element_size, &field_size))
      return 0;
    int32_t next_field_offset;
    if (!cc_checked_record_field_layout(cc, field_offset, field_size,
                                        field_align, &field->offset,
                                        &next_field_offset))
      return 0;
    field_offset = next_field_offset;
    if (field_align > struct_align)
      struct_align = field_align;
    if (!cc_expect(cc, CC_TOK_SEMICOLON))
      return 0;
  }

  if (!cc_expect(cc, CC_TOK_RBRACE))
    return 0;
  int32_t final_offset;
  int32_t final_size;
  if (!cc_checked_record_field_layout(cc, field_offset, 0, struct_align,
                                      &final_offset, &final_size))
    return 0;
  sd->align = struct_align;
  sd->total_size = final_size;
  sd->is_complete = 1;
  return 1;
}

static cc_type_t cc_parse_type(cc_state_t *cc) {
  cc_token_t tok = cc_next(cc);
  cc_type_t base;
  int saw_signed = 0;
  int saw_unsigned = 0;
  cc_last_type_struct_index = -1;
  cc_last_type_base = TYPE_INT;
  cc_last_type_pointer_depth = 0;
  cc_last_type_array_count = 0;
  cc_last_type_is_const_qualified = 0;
  cc_last_type_typedef_index = -1;

  /* Accept storage classes, qualifiers, and width modifiers around the base
   * type. Signed and unsigned integers keep distinct 32-bit semantics. Widths
   * beyond the current backend still use its 32-bit representation. */
  while (cc_is_type_prefix(tok.type)) {
    if (tok.type == CC_TOK_ATTRIBUTE) {
      cc->has_peek = 1;
      cc->peek_buf = tok;
      cc_skip_attributes(cc);
      tok = cc_next(cc);
      continue;
    }
    if (tok.type == CC_TOK_CONST)
      cc_last_type_is_const_qualified = 1;
    else if (tok.type == CC_TOK_SIGNED)
      saw_signed = 1;
    else if (tok.type == CC_TOK_UNSIGNED)
      saw_unsigned = 1;
    if (saw_signed && saw_unsigned) {
      cc_error(cc, "type cannot be both signed and unsigned");
      return TYPE_INT;
    }
    if (tok.type == CC_TOK_LONG || tok.type == CC_TOK_SHORT ||
        tok.type == CC_TOK_SIGNED || tok.type == CC_TOK_UNSIGNED) {
      cc_token_type_t nt = cc_peek(cc).type;
      if (!cc_is_concrete_type(nt) && !cc_is_type_prefix(nt)) {
        base = TYPE_INT;
        goto have_base;
      }
    }
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
  case CC_TOK_U64:
    base = TYPE_INT;
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
  case CC_TOK_I64:
    base = TYPE_INT;
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
      cc_last_type_typedef_index = cc_find_typedef_index(cc, tok.text);
      cc_last_type_struct_index =
          cc_find_typedef_struct_index(cc, tok.text);
      cc_last_type_array_count = cc_find_typedef_array_count(cc, tok.text);
      if (cc_find_typedef_is_const_qualified(cc, tok.text))
        cc_last_type_is_const_qualified = 1;
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
    /* Anonymous struct: `struct { fields }` - typically used inside
     * `typedef struct { ... } Name;`. Generate a synthetic tag and
     * parse the body inline so the alias machinery sees a complete
     * TYPE_STRUCT. Each anon struct gets a unique name via a static
     * counter so multiple anonymous structs don't collide.*/
    if (name_tok.type == CC_TOK_LBRACE) {
      static int anon_counter = 0;
      char anon_name[32];
      const char *prefix = "__anon_struct_";
      int ai = 0;
      while (prefix[ai] && ai < 24) { anon_name[ai] = prefix[ai]; ai++; }
      int n_local = anon_counter++;
      char digits[12]; int dn = 0;
      if (n_local == 0) digits[dn++] = '0';
      else {
        char rev[12]; int rn = 0;
        while (n_local > 0 && rn < 11) {
          rev[rn++] = (char)('0' + (n_local % 10));
          n_local /= 10;
        }
        while (rn > 0) digits[dn++] = rev[--rn];
      }
      for (int k = 0; k < dn && ai < 31; k++) anon_name[ai++] = digits[k];
      anon_name[ai] = 0;
      int si = cc_get_or_add_struct_tag(cc, anon_name);
      if (si < 0) return TYPE_INT;
      if (!cc_parse_struct_body(cc, si)) return TYPE_INT;
      cc_last_type_struct_index = si;
      cc_last_type_array_count = 0;
      base = TYPE_STRUCT;
      break;
    }
    if (name_tok.type != CC_TOK_IDENT) {
      cc_error(cc, "expected struct name");
      return TYPE_INT;
    }
    int si = cc_get_or_add_struct_tag(cc, name_tok.text);
    if (si < 0)
      return TYPE_INT;
    if (cc_peek(cc).type == CC_TOK_LBRACE) {
      cc_next(cc);
      if (!cc_parse_struct_body(cc, si)) return TYPE_INT;
    }
    cc_last_type_struct_index = si;
    cc_last_type_array_count = 0;
    base = TYPE_STRUCT;
    break;
  }
  default:
    cc_error(cc, "expected type");
    return TYPE_INT;
  }

have_base:
  /* Allow trailing qualifiers after base type (e.g. char const *). */
  while (cc_is_type_prefix(cc_peek(cc).type)) {
    cc_token_type_t prefix = cc_peek(cc).type;
    if (prefix == CC_TOK_CONST)
      cc_last_type_is_const_qualified = 1;
    else if (prefix == CC_TOK_SIGNED)
      saw_signed = 1;
    else if (prefix == CC_TOK_UNSIGNED)
      saw_unsigned = 1;
    if (saw_signed && saw_unsigned) {
      cc_error(cc, "type cannot be both signed and unsigned");
      return TYPE_INT;
    }
    if (prefix == CC_TOK_ATTRIBUTE)
      cc_skip_attributes(cc);
    else
      cc_next(cc);
  }

  if (saw_unsigned) {
    if (base == TYPE_INT)
      base = TYPE_UINT;
    else if (base != TYPE_UINT && base != TYPE_CHAR) {
      cc_error(cc, "unsigned requires an integer type");
      return TYPE_INT;
    }
  }

  cc_last_type_base = base;

  /* Pointer depth support: T*, T**, ... */
  int pointer_depth = 0;
  while (cc_peek(cc).type == CC_TOK_STAR) {
    cc_next(cc);
    pointer_depth++;
    /* Ignore pointer qualifiers: char *const, char *const * ... */
    while (cc_is_type_prefix(cc_peek(cc).type)) {
      if (cc_peek(cc).type == CC_TOK_ATTRIBUTE)
        cc_skip_attributes(cc);
      else
        cc_next(cc);
    }
  }
  cc_last_type_pointer_depth = pointer_depth;
  if (pointer_depth > 0 && cc_last_type_array_count > 0) {
    cc_error(cc, "pointer to typedef array is not supported");
    return TYPE_PTR;
  }
  return cc_apply_pointer_declarator(cc, base, pointer_depth);
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

/* Keep every array byte calculation inside signed parser limits before the
 * result is aligned or subtracted from a frame offset. The reserve helpers
 * can reject a large positive request, but they cannot recognize a product
 * that already wrapped back to a small value. */
static int cc_checked_array_bytes(cc_state_t *cc, int32_t count,
                                  int32_t stride, int32_t *out) {
  const int32_t max_aligned_bytes = 0x7ffffffc;
  if (count <= 0 || stride <= 0) {
    cc_error(cc, "array size must be positive");
    return 0;
  }
  if (count > max_aligned_bytes / stride) {
    cc_error(cc, "array allocation size overflow");
    return 0;
  }
  *out = count * stride;
  return 1;
}

static int cc_reserve_local_frame(cc_state_t *cc, int32_t bytes,
                                  int32_t align, int32_t *out_offset) {
  /* Function epilogues round the final frame to 16 bytes. This ceiling keeps
   * that last addition in range as locals accumulate. */
  const int32_t max_frame_bytes = 0x7ffffff0;
  if (bytes < 0 || align <= 0 || cc->local_offset > 0) {
    cc_error(cc, "local frame allocation is invalid");
    return 0;
  }
  if (bytes == 0) {
    *out_offset = cc->local_offset;
    return 1;
  }
  int32_t used = 0 - cc->local_offset;
  int32_t remainder = used % align;
  int32_t padding = remainder == 0 ? 0 : align - remainder;
  if (used > max_frame_bytes - padding) {
    cc_error(cc, "local frame size overflow");
    return 0;
  }
  int32_t aligned_used = used + padding;
  if (bytes > max_frame_bytes - aligned_used) {
    cc_error(cc, "local frame size overflow");
    return 0;
  }
  cc->local_offset = 0 - (aligned_used + bytes);
  if (cc->local_offset < cc->max_local_offset)
    cc->max_local_offset = cc->local_offset;
  *out_offset = cc->local_offset;
  return 1;
}

/* Private CupidC passes ordinary values in complete cdecl stack slots.
 * Aggregates still need a separate ABI before they can cross a boundary. */
static int32_t cc_cdecl_slot_size(cc_type_t type) {
  switch (type) {
  case TYPE_INT:
  case TYPE_UINT:
  case TYPE_CHAR:
  case TYPE_PTR:
  case TYPE_INT_PTR:
  case TYPE_UINT_PTR:
  case TYPE_CHAR_PTR:
  case TYPE_STRUCT_PTR:
  case TYPE_FUNC_PTR:
  case TYPE_FLOAT_PTR:
  case TYPE_DOUBLE_PTR:
  case TYPE_FLOAT:
    return 4;
  case TYPE_DOUBLE:
    return 8;
  case TYPE_FLOAT4:
  case TYPE_DOUBLE2:
    return 16;
  default:
    return 0;
  }
}

static int cc_emit_cdecl_argument_push(cc_state_t *cc, cc_type_t type,
                                       int *slot_size) {
  int size;

  if (cc->error)
    return 0;

  size = cc_cdecl_slot_size(type);
  if (size == 0) {
    cc_error(cc, "cdecl call argument type is not supported");
    return 0;
  }

  if (type == TYPE_FLOAT) {
    emit_push_xmm_float(cc, 0);
  } else if (type == TYPE_DOUBLE) {
    emit_push_xmm_double(cc, 0);
  } else if (type == TYPE_FLOAT4 || type == TYPE_DOUBLE2) {
    emit_push_xmm_vector(cc, 0);
  } else {
    emit_push_eax(cc);
  }
  *slot_size = size;
  return 1;
}

/* Apply the conversions required by a known fixed parameter before its
 * value is assigned a cdecl slot. Calls without parsed parameter metadata
 * retain the existing source-width behavior. */
static int cc_coerce_cdecl_argument(cc_state_t *cc, cc_type_t target_type) {
  cc_type_t source_type = cc_last_expr_type;

  if (cc_cdecl_slot_size(source_type) == 0) {
    cc_error(cc, "cdecl call argument type is not supported");
    return 0;
  }
  if (source_type == target_type)
    return 1;
  if (target_type == TYPE_UINT &&
      (source_type == TYPE_FLOAT || source_type == TYPE_DOUBLE))
    return cc_coerce_unsigned_conversion(cc, target_type, source_type);
  if ((source_type == TYPE_INT || source_type == TYPE_UINT ||
       source_type == TYPE_CHAR) &&
      target_type == TYPE_FLOAT) {
    cc_emit_integer_to_fp(cc, source_type, target_type, 0);
  } else if ((source_type == TYPE_INT || source_type == TYPE_UINT ||
              source_type == TYPE_CHAR) &&
             target_type == TYPE_DOUBLE) {
    cc_emit_integer_to_fp(cc, source_type, target_type, 0);
  } else if (source_type == TYPE_FLOAT && target_type == TYPE_DOUBLE) {
    emit_cvtss2sd(cc, 0, 0);
  } else if (source_type == TYPE_DOUBLE && target_type == TYPE_FLOAT) {
    emit_cvtsd2ss(cc, 0, 0);
  } else if (source_type == TYPE_FLOAT &&
             (target_type == TYPE_INT || target_type == TYPE_UINT ||
              target_type == TYPE_CHAR)) {
    emit_cvttss2si(cc, 0);
  } else if (source_type == TYPE_DOUBLE &&
             (target_type == TYPE_INT || target_type == TYPE_UINT ||
              target_type == TYPE_CHAR)) {
    emit_cvttsd2si(cc, 0);
  } else if ((source_type == TYPE_INT || source_type == TYPE_UINT ||
              source_type == TYPE_CHAR) &&
             (target_type == TYPE_INT || target_type == TYPE_UINT ||
              target_type == TYPE_CHAR)) {
    /* Both private integer spellings use one four-byte cdecl slot. */
  } else if ((source_type == TYPE_INT || source_type == TYPE_UINT ||
              source_type == TYPE_CHAR ||
              cc_is_object_pointer_type(source_type) ||
              source_type == TYPE_FUNC_PTR) &&
             (cc_is_object_pointer_type(target_type) ||
              target_type == TYPE_FUNC_PTR)) {
    /* Represented pointers and integer null forms share a four-byte slot. */
  } else if (cc_is_object_pointer_type(source_type) &&
             (target_type == TYPE_INT || target_type == TYPE_UINT)) {
    /* Cupid word parameters may carry represented object addresses. */
  } else {
    cc_error(cc, "cdecl argument type does not match fixed parameter");
    return 0;
  }

  cc_last_expr_type = target_type;
  if (target_type == TYPE_FLOAT || target_type == TYPE_DOUBLE)
    cc_last_xmm = 0;
  return 1;
}

static int cc_symbol_has_cdecl_parameter_metadata(
    const cc_symbol_t *symbol) {
  if (!symbol || !symbol->has_param_types)
    return 0;
  if (symbol->kind == SYM_FUNC)
    return 1;
  return (symbol->kind == SYM_LOCAL || symbol->kind == SYM_PARAM ||
          symbol->kind == SYM_GLOBAL) &&
         symbol->type == TYPE_FUNC_PTR;
}

static int cc_validate_named_function_pointer_arity(
    cc_state_t *cc, const cc_symbol_t *symbol, int argument_count) {
  if (!symbol || symbol->type != TYPE_FUNC_PTR ||
      !cc_symbol_has_cdecl_parameter_metadata(symbol))
    return 1;
  if (argument_count < symbol->param_count) {
    cc_error(cc, "function-pointer call has too few arguments");
    return 0;
  }
  if (!symbol->is_variadic && argument_count > symbol->param_count) {
    cc_error(cc, "function-pointer call has too many arguments");
    return 0;
  }
  return 1;
}

static int cc_validate_function_pointer_argument_value(
    cc_state_t *cc, int function_pointer_signature_handle);

static int cc_emit_call_argument_push(cc_state_t *cc,
                                      cc_symbol_t *callee,
                                      int argument_index,
                                      int *slot_size) {
  cc_type_t slot_type = cc_last_expr_type;
  int has_parameter_metadata =
      cc_symbol_has_cdecl_parameter_metadata(callee);
  int has_fixed_parameter =
      has_parameter_metadata &&
      argument_index < callee->param_count;
  if (has_fixed_parameter) {
    slot_type = (cc_type_t)callee->param_types[argument_index];
    if (slot_type == TYPE_FUNC_PTR &&
        !cc_validate_function_pointer_argument_value(
            cc,
            callee->param_struct_indices[argument_index]))
      return 0;
    if (!cc_coerce_cdecl_argument(cc, slot_type))
      return 0;
  } else if (has_parameter_metadata && callee->is_variadic &&
             argument_index >= callee->param_count) {
    /* C's default argument promotions apply beyond the fixed prefix. */
    if (slot_type == TYPE_FLOAT) {
      emit_cvtss2sd(cc, 0, 0);
      slot_type = TYPE_DOUBLE;
      cc_last_expr_type = TYPE_DOUBLE;
      cc_last_xmm = 0;
    } else if (slot_type == TYPE_CHAR) {
      slot_type = TYPE_INT;
      cc_last_expr_type = TYPE_INT;
    }
  }
  if ((slot_type == TYPE_FLOAT4 || slot_type == TYPE_DOUBLE2) &&
      !has_fixed_parameter) {
    cc_error(cc, "SIMD call arguments require a fixed parameter type");
    return 0;
  }
  return cc_emit_cdecl_argument_push(cc, slot_type, slot_size);
}

static int cc_bind_cdecl_parameter(cc_state_t *cc, const char *name,
                                   cc_type_t type, int struct_index,
                                   int is_const_qualified,
                                   int function_pointer_signature_handle,
                                   int32_t offset) {
  int slot_size = cc_cdecl_slot_size(type);
  cc_symbol_t *symbol;

  if (slot_size == 0) {
    cc_error(cc, "cdecl parameter type is not supported");
    return 0;
  }

  symbol = cc_sym_add(cc, name, SYM_PARAM, type);
  if (!symbol)
    return 0;
  symbol->offset = offset;
  symbol->is_const_qualified = is_const_qualified;
  symbol->struct_index = struct_index;
  if (type == TYPE_FUNC_PTR)
    (void)cc_copy_function_pointer_signature_handle(
        cc, function_pointer_signature_handle, symbol);
  return slot_size;
}

static int cc_is_arithmetic_scalar_type(cc_type_t type) {
  return type == TYPE_INT || type == TYPE_UINT || type == TYPE_CHAR ||
         type == TYPE_FLOAT || type == TYPE_DOUBLE;
}

static int cc_is_scalar_truth_type(cc_type_t type) {
  return type == TYPE_INT || type == TYPE_UINT || type == TYPE_CHAR ||
         type == TYPE_PTR || type == TYPE_INT_PTR ||
         type == TYPE_UINT_PTR ||
         type == TYPE_CHAR_PTR || type == TYPE_STRUCT_PTR ||
         type == TYPE_FLOAT_PTR || type == TYPE_DOUBLE_PTR ||
         type == TYPE_FUNC_PTR || type == TYPE_FLOAT ||
         type == TYPE_DOUBLE;
}

/* Materialize a scalar expression's C truth value in EAX. Integer and
 * pointer expressions already use EAX. Floating expressions use XMM0 and
 * need an explicit comparison with zero before control-flow code tests EAX. */
static int cc_materialize_scalar_truth(cc_state_t *cc, cc_type_t type) {
  if (!cc_is_scalar_truth_type(type)) {
    cc_error(cc, "truth test requires a scalar operand");
    return 0;
  }
  if (type == TYPE_FLOAT || type == TYPE_DOUBLE)
    emit_scalar_truth_xmm0(cc, type == TYPE_DOUBLE);
  return 1;
}

static int cc_is_simd_value_type(cc_type_t type) {
  return type == TYPE_FLOAT4 || type == TYPE_DOUBLE2;
}

static int cc_is_direct_update_type(cc_type_t type) {
  return type == TYPE_INT || type == TYPE_UINT || type == TYPE_CHAR ||
         type == TYPE_PTR || type == TYPE_INT_PTR ||
         type == TYPE_UINT_PTR ||
         type == TYPE_CHAR_PTR || type == TYPE_STRUCT_PTR ||
         type == TYPE_FLOAT_PTR || type == TYPE_DOUBLE_PTR ||
         type == TYPE_FLOAT || type == TYPE_DOUBLE ||
         cc_is_simd_value_type(type);
}

static void cc_error_simd_update_target(cc_state_t *cc) {
  cc_error(cc,
           "SIMD increment or decrement requires a modifiable "
           "whole-vector lvalue");
}

static void cc_error_simd_assignment_target(cc_state_t *cc) {
  cc_error(cc,
           "SIMD assignment requires a modifiable whole-vector lvalue");
}

static int cc_validate_variable_update(cc_state_t *cc, cc_symbol_t *sym) {
  if (sym && sym->is_const_qualified && cc_is_simd_value_type(sym->type)) {
    cc_error_simd_update_target(cc);
    return 0;
  }
  if (!sym || sym->is_array ||
      (sym->kind != SYM_LOCAL && sym->kind != SYM_PARAM &&
       sym->kind != SYM_GLOBAL) ||
       !cc_is_direct_update_type(sym->type)) {
    cc_error(cc, "increment or decrement requires a scalar variable");
    return 0;
  }
  return 1;
}

static int32_t cc_variable_update_step(cc_state_t *cc, cc_symbol_t *sym) {
  if (sym->type == TYPE_CHAR_PTR || sym->type == TYPE_PTR)
    return 1;
  if (sym->type == TYPE_INT_PTR || sym->type == TYPE_UINT_PTR ||
      sym->type == TYPE_FLOAT_PTR)
    return 4;
  if (sym->type == TYPE_DOUBLE_PTR)
    return 8;
  if (sym->type == TYPE_STRUCT_PTR) {
    if (sym->struct_index < 0 || sym->struct_index >= cc->struct_count ||
        cc->structs[sym->struct_index].total_size <= 0) {
      cc_error(cc,
               "struct pointer update requires a complete pointed-to type");
      return 0;
    }
    return cc->structs[sym->struct_index].total_size;
  }
  return 1;
}

/* Add or subtract one from the scalar floating value in XMM0. Converting the
 * integer constant in EAX avoids a data-segment literal and gives XMM1 the
 * exact 1.0 value for either width. */
static void emit_update_xmm0_scalar(cc_state_t *cc, int is_double,
                                    int decrement) {
  emit_mov_eax_imm(cc, 1);
  if (is_double)
    emit_cvtsi2sd(cc, 1);
  else
    emit_cvtsi2ss(cc, 1);
  emit_sse_scalar_op(cc, is_double, decrement ? 0x5C : 0x58, 0, 1);
}

/* Add or subtract a packed 1.0 from every lane in XMM0. The conversion and
 * shuffle avoid a data literal while producing the exact scalar one for both
 * vector widths. */
static void emit_update_xmm0_vector(cc_state_t *cc, cc_type_t vector_type,
                                    int decrement) {
  int is_double = vector_type == TYPE_DOUBLE2;

  emit_mov_eax_imm(cc, 1);
  if (is_double)
    emit_cvtsi2sd(cc, 1);
  else
    emit_cvtsi2ss(cc, 1);
  if (is_double)
    emit8(cc, 0x66);
  emit8(cc, 0x0F);
  emit8(cc, 0xC6); /* shufps/shufpd xmm1, xmm1, 0 */
  emit8(cc, 0xC9);
  emit8(cc, 0x00);
  if (is_double)
    emit8(cc, 0x66);
  emit8(cc, 0x0F);
  emit8(cc, decrement ? 0x5C : 0x58); /* subps/subpd or addps/addpd */
  emit8(cc, 0xC1);
}

static int cc_emit_indirect_scalar_store(cc_state_t *cc,
                                         cc_type_t object_type);

/* Update a direct local, parameter, or global variable. A postfix floating or
 * SIMD expression keeps its original payload in XMM2 while XMM0 is updated
 * and stored. The integer path uses the existing stack preservation. */
static int cc_emit_variable_update(cc_state_t *cc, cc_symbol_t *sym,
                                   int decrement, int preserve_old) {
  if (!cc_validate_variable_update(cc, sym))
    return 0;

  if (cc_is_simd_value_type(sym->type)) {
    if (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM)
      emit_movups_xmm_local(cc, 0, sym->offset);
    else
      emit_movups_xmm_disp32(cc, 0, sym->address);

    if (preserve_old)
      emit_movaps_xmm_xmm(cc, 2, 0);
    emit_update_xmm0_vector(cc, sym->type, decrement);

    if (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM)
      emit_movups_local_xmm(cc, 0, sym->offset);
    else
      emit_movups_disp32_xmm(cc, 0, sym->address);
    if (preserve_old)
      emit_movaps_xmm_xmm(cc, 0, 2);
    cc_last_xmm = 0;
  } else if (sym->type == TYPE_FLOAT || sym->type == TYPE_DOUBLE) {
    int is_double = sym->type == TYPE_DOUBLE;
    if (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM) {
      if (is_double)
        emit_movsd_xmm_local(cc, 0, sym->offset);
      else
        emit_movss_xmm_local(cc, 0, sym->offset);
    } else {
      if (is_double)
        emit_movsd_xmm_disp32(cc, 0, sym->address);
      else
        emit_movss_xmm_disp32(cc, 0, sym->address);
    }

    if (preserve_old)
      emit_movaps_xmm_xmm(cc, 2, 0);
    emit_update_xmm0_scalar(cc, is_double, decrement);

    if (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM) {
      if (is_double)
        emit_movsd_local_xmm(cc, 0, sym->offset);
      else
        emit_movss_local_xmm(cc, 0, sym->offset);
    } else {
      emit8(cc, is_double ? 0xF2 : 0xF3);
      emit8(cc, 0x0F);
      emit8(cc, 0x11);
      emit8(cc, 0x05); /* movss/movsd [disp32], xmm0 */
      emit32(cc, sym->address);
    }

    if (preserve_old)
      emit_movaps_xmm_xmm(cc, 0, 2);
    cc_last_xmm = 0;
  } else {
    int32_t update_step = cc_variable_update_step(cc, sym);
    if (update_step <= 0)
      return 0;
    if (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM) {
      emit_load_local(cc, sym->offset);
    } else {
      emit8(cc, 0xA1);
      emit32(cc, sym->address);
    }

    if (preserve_old)
      emit_push_eax(cc);
    if (update_step == 1) {
      emit8(cc, decrement ? 0x48 : 0x40); /* dec/inc eax */
    } else {
      emit8(cc, decrement ? 0x2D : 0x05); /* sub/add eax, imm32 */
      emit32(cc, (uint32_t)update_step);
    }

    if (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM) {
      emit_store_local(cc, sym->offset);
    } else {
      emit8(cc, 0xA3);
      emit32(cc, sym->address);
    }
    if (preserve_old)
      emit_pop_eax(cc);
  }

  cc_last_expr_type = sym->type;
  return 1;
}

/* Update a floating or SIMD object whose evaluated address remains in EAX and
 * whose current value is already in XMM0. The address is saved before the
 * update helper uses EAX for the exact integer one. */
static int cc_emit_indirect_fp_update(cc_state_t *cc,
                                      cc_type_t object_type,
                                      int decrement,
                                      int preserve_old) {
  if (cc_last_expr_const_lvalue && cc_is_simd_value_type(object_type)) {
    cc_error_simd_update_target(cc);
    return 0;
  }
  if (object_type != TYPE_FLOAT && object_type != TYPE_DOUBLE &&
      !cc_is_simd_value_type(object_type)) {
    cc_error(cc, "indirect increment or decrement is not supported");
    return 0;
  }

  emit_push_eax(cc);
  if (preserve_old)
    emit_movaps_xmm_xmm(cc, 2, 0);
  if (cc_is_simd_value_type(object_type)) {
    emit_update_xmm0_vector(cc, object_type, decrement);
  } else {
    emit_update_xmm0_scalar(cc, object_type == TYPE_DOUBLE, decrement);
  }
  emit_pop_eax(cc);
  if (cc_is_simd_value_type(object_type))
    emit_movups_eax_xmm(cc, 0);
  else if (!cc_emit_indirect_scalar_store(cc, object_type))
    return 0;
  if (preserve_old)
    emit_movaps_xmm_xmm(cc, 0, 2);

  cc_last_expr_type = object_type;
  cc_last_expr_indirect_lvalue = 0;
  cc_last_expr_direct_lvalue_sym = NULL;
  cc_last_expr_simd_lane = 0;
  cc_last_expr_const_lvalue = 0;
  cc_last_xmm = 0;
  return 1;
}

static void cc_error_update_target(cc_state_t *cc) {
  if (cc_last_expr_simd_lane) {
    cc_error(cc, "SIMD lane increment or decrement is not supported");
  } else if (cc_is_simd_value_type(cc_last_expr_type)) {
    cc_error_simd_update_target(cc);
  } else if (cc_last_expr_indirect_lvalue) {
    cc_error(cc, "indirect increment or decrement is not supported");
  } else {
    cc_error(cc, "increment or decrement requires a scalar variable");
  }
}

/* Promote binary-op types per scalar hierarchy. Rules:
 *  - SIMD (float4/double2) must match exactly on both sides.
 *  - double > float > int for scalar ops.
 *  - char undergoes the C integer promotions before arithmetic.
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
  if ((a == TYPE_INT || a == TYPE_UINT || a == TYPE_CHAR) &&
      (b == TYPE_INT || b == TYPE_UINT || b == TYPE_CHAR))
    return a == TYPE_UINT || b == TYPE_UINT ? TYPE_UINT : TYPE_INT;
  /* Preserve the existing pointer-shaped result for integer-only paths. */
  return a;
}

static cc_type_t cc_integer_operation_type(cc_state_t *cc,
                                            cc_token_type_t op,
                                            cc_type_t left_type,
                                            cc_type_t right_type) {
  if (op == CC_TOK_SHL || op == CC_TOK_SHR || op == CC_TOK_SHLEQ ||
      op == CC_TOK_SHREQ)
    return left_type == TYPE_UINT ? TYPE_UINT : TYPE_INT;
  return cc_promote(cc, left_type, right_type);
}

/* Preserve only integer constant expressions whose value can be reproduced
 * without invoking undefined arithmetic. The emitted runtime expression is
 * still authoritative; this side channel exists solely for C null pointer
 * constant rules and never folds or replaces generated code. */
static int cc_eval_integer_constant_binary(
    cc_token_type_t op, uint32_t left_value, int left_is_unsigned,
    uint32_t right_value, int right_is_unsigned,
    cc_type_t operation_type, uint32_t *result, int *result_is_unsigned) {
  int is_unsigned = operation_type == TYPE_UINT ||
                    left_is_unsigned || right_is_unsigned;
  int32_t left_signed = (int32_t)left_value;
  int32_t right_signed = (int32_t)right_value;
  const int32_t min_value = (-2147483647 - 1);
  const int32_t max_value = 2147483647;

  *result_is_unsigned = is_unsigned;
  switch (op) {
  case CC_TOK_PLUS:
    if (!is_unsigned &&
        ((right_signed > 0 && left_signed > max_value - right_signed) ||
         (right_signed < 0 && left_signed < min_value - right_signed)))
      return 0;
    *result = left_value + right_value;
    return 1;
  case CC_TOK_MINUS:
    if (!is_unsigned &&
        ((right_signed < 0 && left_signed > max_value + right_signed) ||
         (right_signed > 0 && left_signed < min_value + right_signed)))
      return 0;
    *result = left_value - right_value;
    return 1;
  case CC_TOK_STAR:
    if (!is_unsigned) {
      if (left_signed != 0 && right_signed != 0) {
        if (left_signed > 0) {
          if ((right_signed > 0 && left_signed > max_value / right_signed) ||
              (right_signed < 0 && right_signed < min_value / left_signed))
            return 0;
        } else if ((right_signed > 0 &&
                    left_signed < min_value / right_signed) ||
                   (right_signed < 0 &&
                    left_signed < max_value / right_signed)) {
          return 0;
        }
      }
    }
    *result = left_value * right_value;
    return 1;
  case CC_TOK_SLASH:
  case CC_TOK_PERCENT:
    if (right_value == 0)
      return 0;
    if (!is_unsigned && left_signed == min_value && right_signed == -1)
      return 0;
    if (is_unsigned) {
      *result = op == CC_TOK_SLASH
                    ? left_value / right_value
                    : left_value % right_value;
    } else {
      *result = (uint32_t)(op == CC_TOK_SLASH
                               ? left_signed / right_signed
                               : left_signed % right_signed);
    }
    return 1;
  case CC_TOK_AMP:
    *result = left_value & right_value;
    return 1;
  case CC_TOK_BOR:
    *result = left_value | right_value;
    return 1;
  case CC_TOK_BXOR:
    *result = left_value ^ right_value;
    return 1;
  case CC_TOK_SHL: {
    int32_t shift = right_signed;
    *result_is_unsigned = operation_type == TYPE_UINT;
    if (shift < 0 || shift >= 32)
      return 0;
    if (!*result_is_unsigned &&
        (left_signed < 0 ||
         (shift > 0 && left_signed > (max_value >> shift))))
      return 0;
    *result = left_value << (uint32_t)shift;
    return 1;
  }
  case CC_TOK_SHR: {
    int32_t shift = right_signed;
    *result_is_unsigned = operation_type == TYPE_UINT;
    if (shift < 0 || shift >= 32)
      return 0;
    *result = *result_is_unsigned
                  ? left_value >> (uint32_t)shift
                  : (uint32_t)(left_signed >> (uint32_t)shift);
    return 1;
  }
  case CC_TOK_EQEQ:
    *result = left_value == right_value;
    *result_is_unsigned = 0;
    return 1;
  case CC_TOK_NE:
    *result = left_value != right_value;
    *result_is_unsigned = 0;
    return 1;
  case CC_TOK_LT:
    *result = is_unsigned ? left_value < right_value
                          : left_signed < right_signed;
    *result_is_unsigned = 0;
    return 1;
  case CC_TOK_GT:
    *result = is_unsigned ? left_value > right_value
                          : left_signed > right_signed;
    *result_is_unsigned = 0;
    return 1;
  case CC_TOK_LE:
    *result = is_unsigned ? left_value <= right_value
                          : left_signed <= right_signed;
    *result_is_unsigned = 0;
    return 1;
  case CC_TOK_GE:
    *result = is_unsigned ? left_value >= right_value
                          : left_signed >= right_signed;
    *result_is_unsigned = 0;
    return 1;
  case CC_TOK_AND:
    *result = left_value != 0 && right_value != 0;
    *result_is_unsigned = 0;
    return 1;
  case CC_TOK_OR:
    *result = left_value != 0 || right_value != 0;
    *result_is_unsigned = 0;
    return 1;
  default:
    return 0;
  }
}

static int32_t cc_sizeof_symbol_deref(cc_state_t *cc, cc_symbol_t *sym,
                                      int deref_count) {
  cc_type_t type = sym->type;
  cc_type_t array_elem_type = sym->array_elem_type;
  int struct_index = sym->struct_index;
  int elem_size = sym->array_elem_size;
  int dim2 = sym->array_dim2;
  int array_rank = sym->array_rank;
  int is_array = sym->is_array;

  int i;
  for (i = 0; i < deref_count; i++) {
    int last = (i == deref_count - 1);

    if (is_array) {
      if (array_elem_type == TYPE_FLOAT ||
          array_elem_type == TYPE_DOUBLE ||
          array_elem_type == TYPE_FLOAT4 ||
          array_elem_type == TYPE_DOUBLE2) {
        int scalar_size = cc_type_size(cc, array_elem_type, -1);
        if (last)
          return elem_size > 0 ? elem_size : scalar_size;
        if (array_rank > 1) {
          elem_size = array_rank > 2 ? dim2 : scalar_size;
          dim2 = 0;
          array_rank--;
          continue;
        }
        type = array_elem_type;
        is_array = 0;
        continue;
      }
      if (type == TYPE_STRUCT_PTR) {
        if (last)
          return cc_type_size(cc, TYPE_STRUCT, struct_index);
        type = TYPE_STRUCT;
        is_array = 0;
        continue;
      }
      if (type == TYPE_CHAR_PTR) {
        if (last)
          return elem_size > 0 ? elem_size : 1;
        if (array_rank > 1) {
          elem_size = array_rank > 2 ? dim2 : 1;
          dim2 = 0;
          array_rank--;
          continue;
        }
        type = TYPE_CHAR;
        is_array = 0;
        continue;
      }
      if (type == TYPE_INT_PTR) {
        if (last)
          return elem_size > 0 ? elem_size : 4;
        if (array_rank > 1) {
          elem_size = array_rank > 2 ? dim2 : 4;
          dim2 = 0;
          array_rank--;
          continue;
        }
        type = TYPE_INT;
        is_array = 0;
        continue;
      }
      if (type == TYPE_UINT_PTR) {
        if (last)
          return elem_size > 0 ? elem_size : 4;
        if (array_rank > 1) {
          elem_size = array_rank > 2 ? dim2 : 4;
          dim2 = 0;
          array_rank--;
          continue;
        }
        type = TYPE_UINT;
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
    if (type == TYPE_INT_PTR || type == TYPE_UINT_PTR ||
        type == TYPE_PTR || type == TYPE_FUNC_PTR) {
      if (last)
        return 4;
      type = type == TYPE_UINT_PTR ? TYPE_UINT : TYPE_INT;
      continue;
    }
    if (type == TYPE_FLOAT_PTR || type == TYPE_DOUBLE_PTR) {
      if (last)
        return cc_type_size(cc, cc_pointed_object_type(type), -1);
      type = cc_pointed_object_type(type);
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

/* A complete-source parse may reuse function and kernel-binding symbols from
 * an earlier successful unit. Keep their original values in the unused tail
 * of the existing symbol arena until the source commits. They stay outside
 * sym_count, so ordinary lookup cannot observe the snapshots. */
static cc_state_t *cc_program_symbol_snapshot_owner;
static int cc_program_symbol_snapshot_checkpoint;
static int cc_program_symbol_snapshot_indices[CC_MAX_SYMBOLS];
static int cc_program_symbol_snapshot_count;

void cc_sym_init(cc_state_t *cc) {
  cc->sym_count = 0;
  cc->current_return_type = TYPE_INT;
}

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
  int reserved_snapshots =
      cc_program_symbol_snapshot_owner == cc
          ? cc_program_symbol_snapshot_count
          : 0;
  if (cc->sym_count >= CC_MAX_SYMBOLS - reserved_snapshots) {
    cc_error(cc, "too many symbols");
    return NULL;
  }
  cc_symbol_t *sym = &cc->symbols[cc->sym_count++];
  memset(sym, 0, sizeof(*sym));
  memset(sym->param_struct_indices, -1,
         sizeof(sym->param_struct_indices));
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

static int cc_begin_program_symbol_transaction(
    cc_state_t *cc, int symbol_checkpoint) {
  if (cc_program_symbol_snapshot_owner) {
    cc_error(cc, "nested program-symbol transaction");
    return 0;
  }
  cc_program_symbol_snapshot_owner = cc;
  cc_program_symbol_snapshot_checkpoint = symbol_checkpoint;
  cc_program_symbol_snapshot_count = 0;
  return 1;
}

static int cc_snapshot_program_symbol(cc_state_t *cc,
                                      cc_symbol_t *symbol) {
  int symbol_index;
  int snapshot_index;
  int snapshot_slot;

  if (cc_program_symbol_snapshot_owner != cc || !symbol)
    return 1;
  symbol_index = (int)(symbol - cc->symbols);
  if (symbol_index < 0 || symbol_index >= cc->sym_count) {
    cc_error(cc, "program-symbol transaction target is invalid");
    return 0;
  }
  if (symbol_index >= cc_program_symbol_snapshot_checkpoint)
    return 1;
  for (snapshot_index = 0;
       snapshot_index < cc_program_symbol_snapshot_count;
       snapshot_index++) {
    if (cc_program_symbol_snapshot_indices[snapshot_index] == symbol_index)
      return 1;
  }
  if (cc->sym_count >=
      CC_MAX_SYMBOLS - cc_program_symbol_snapshot_count) {
    cc_error(cc, "too many symbols for program rollback");
    return 0;
  }
  snapshot_slot =
      CC_MAX_SYMBOLS - 1 - cc_program_symbol_snapshot_count;
  cc->symbols[snapshot_slot] = *symbol;
  cc_program_symbol_snapshot_indices[cc_program_symbol_snapshot_count] =
      symbol_index;
  cc_program_symbol_snapshot_count++;
  return 1;
}

static void cc_finish_program_symbol_transaction(cc_state_t *cc,
                                                 int rollback) {
  int snapshot_index;

  if (cc_program_symbol_snapshot_owner != cc)
    return;
  if (rollback) {
    for (snapshot_index = cc_program_symbol_snapshot_count - 1;
         snapshot_index >= 0; snapshot_index--) {
      int snapshot_slot = CC_MAX_SYMBOLS - 1 - snapshot_index;
      cc->symbols[cc_program_symbol_snapshot_indices[snapshot_index]] =
          cc->symbols[snapshot_slot];
    }
  }
  cc_program_symbol_snapshot_count = 0;
  cc_program_symbol_snapshot_checkpoint = 0;
  cc_program_symbol_snapshot_owner = NULL;
}

static void cc_labels_reset(cc_state_t *cc) { cc->label_count = 0; }

static cc_label_t *cc_label_find(cc_state_t *cc, const char *name) {
  for (int i = 0; i < cc->label_count; i++) {
    if (strcmp(cc->labels[i].name, name) == 0)
      return &cc->labels[i];
  }
  return NULL;
}

static cc_label_t *cc_label_get(cc_state_t *cc, const char *name) {
  cc_label_t *lbl = cc_label_find(cc, name);
  if (lbl)
    return lbl;
  if (cc->label_count >= CC_MAX_LABELS) {
    cc_error(cc, "too many labels");
    return NULL;
  }
  lbl = &cc->labels[cc->label_count++];
  memset(lbl, 0, sizeof(*lbl));
  int i = 0;
  while (name[i] && i < CC_MAX_IDENT - 1) {
    lbl->name[i] = name[i];
    i++;
  }
  lbl->name[i] = '\0';
  return lbl;
}

static void cc_patch_goto_to(cc_state_t *cc, uint32_t patch_pos,
                             uint32_t target_pos) {
  int32_t rel = (int32_t)(target_pos - (patch_pos + 4));
  patch32(cc, patch_pos, (uint32_t)rel);
}

static void cc_define_label(cc_state_t *cc, const char *name) {
  cc_label_t *lbl = cc_label_get(cc, name);
  if (!lbl)
    return;
  if (lbl->is_defined) {
    cc_error(cc, "duplicate label");
    return;
  }
  lbl->is_defined = 1;
  lbl->code_offset = cc->code_pos;
  for (int i = 0; i < lbl->patch_count; i++) {
    cc_patch_goto_to(cc, lbl->patches[i], lbl->code_offset);
  }
}

static void cc_emit_goto(cc_state_t *cc, const char *name) {
  cc_label_t *lbl = cc_label_get(cc, name);
  uint32_t patch_pos = emit_jmp_placeholder(cc);
  if (!lbl)
    return;
  if (lbl->is_defined) {
    cc_patch_goto_to(cc, patch_pos, lbl->code_offset);
  } else if (lbl->patch_count < CC_MAX_LABEL_PATCHES) {
    lbl->patches[lbl->patch_count++] = patch_pos;
  } else {
    cc_error(cc, "too many gotos to label");
  }
}

static void cc_resolve_labels(cc_state_t *cc) {
  for (int i = 0; i < cc->label_count; i++) {
    if (!cc->labels[i].is_defined) {
      cc_error(cc, "unresolved goto label");
      return;
    }
  }
}

/* Forward Declarations for Parser */

static void cc_parse_statement(cc_state_t *cc);
static void cc_parse_block(cc_state_t *cc);
static void cc_parse_expression(cc_state_t *cc, int min_prec);
static void cc_parse_expression_impl(cc_state_t *cc, int min_prec);
static void cc_parse_primary(cc_state_t *cc);
static int cc_parse_function_pointer_local_initializer(
    cc_state_t *cc, cc_symbol_t *pointer, int32_t local_slot);
static int cc_emit_indirect_scalar_load(cc_state_t *cc,
                                        cc_type_t object_type);
static int cc_function_pointer_signatures_match(
    const cc_symbol_t *left, const cc_symbol_t *right);
static int cc_validate_function_pointer_assignment_value(
    cc_state_t *cc, const cc_symbol_t *pointer);
static int cc_apply_function_pointer_initializer_candidates(
    cc_state_t *cc, const cc_symbol_t *pointer,
    cc_symbol_t *const *targets, int target_count);

static int cc_is_prescan_type_token(cc_token_type_t t) {
  return t == CC_TOK_INT || t == CC_TOK_CHAR || t == CC_TOK_VOID ||
         t == CC_TOK_U0 || t == CC_TOK_U8 || t == CC_TOK_U16 ||
         t == CC_TOK_U32 || t == CC_TOK_U64 ||
         t == CC_TOK_I8 || t == CC_TOK_I16 ||
         t == CC_TOK_I32 || t == CC_TOK_I64 ||
         t == CC_TOK_FLOAT || t == CC_TOK_DOUBLE ||
         t == CC_TOK_FLOAT4 || t == CC_TOK_DOUBLE2 ||
         t == CC_TOK_BOOL || t == CC_TOK_STRUCT ||
         t == CC_TOK_LONG || t == CC_TOK_SHORT ||
         t == CC_TOK_SIGNED || t == CC_TOK_UNSIGNED;
}

static void cc_prescan_add_function(cc_state_t *cc, const char *name) {
  cc_symbol_t *sym = cc_sym_find(cc, name);
  if (sym && !cc_snapshot_program_symbol(cc, sym))
    return;
  if (!sym) {
    sym = cc_sym_add(cc, name, SYM_FUNC, TYPE_INT);
  }
  if (sym && sym->kind != SYM_KERNEL) {
    sym->kind = SYM_FUNC;
    if (!sym->is_defined) {
      sym->offset = 0;
      sym->address = 0;
      if (!sym->has_param_types &&
          !sym->function_signature_is_provisional)
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

    if (tok.type == CC_TOK_STATIC || tok.type == CC_TOK_EXTERN ||
        tok.type == CC_TOK_INLINE || tok.type == CC_TOK_REGISTER ||
        tok.type == CC_TOK_CONST || tok.type == CC_TOK_UNSIGNED ||
        tok.type == CC_TOK_SIGNED || tok.type == CC_TOK_VOLATILE ||
        tok.type == CC_TOK_RESTRICT) {
      tok = cc_lex_next(cc);
    }
    while (tok.type == CC_TOK_CONST || tok.type == CC_TOK_UNSIGNED ||
           tok.type == CC_TOK_SIGNED || tok.type == CC_TOK_LONG ||
           tok.type == CC_TOK_SHORT || tok.type == CC_TOK_VOLATILE ||
           tok.type == CC_TOK_EXTERN || tok.type == CC_TOK_INLINE ||
           tok.type == CC_TOK_REGISTER || tok.type == CC_TOK_RESTRICT) {
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
 * (primary statics declared above, before cc_parse_type)*/

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

/* Emit binary operation: EBX = left, EAX = right -> result in EAX */
static void cc_emit_binop(cc_state_t *cc, cc_token_type_t op,
                          cc_type_t operation_type) {
  /* Pop left operand into EBX */
  emit_pop_ebx(cc);
  int is_unsigned = operation_type == TYPE_UINT;

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
    /* ebx / eax: swap, extend the dividend, then divide */
    emit8(cc, 0x89);
    emit8(cc, 0xC1); /* mov ecx, eax */
    emit8(cc, 0x89);
    emit8(cc, 0xD8); /* mov eax, ebx */
    if (is_unsigned) {
      emit8(cc, 0x31);
      emit8(cc, 0xD2); /* xor edx, edx */
      emit8(cc, 0xF7);
      emit8(cc, 0xF1); /* div ecx */
    } else {
      emit8(cc, 0x99); /* cdq (sign-extend eax->edx:eax) */
      emit8(cc, 0xF7);
      emit8(cc, 0xF9); /* idiv ecx */
    }
    break;
  case CC_TOK_PERCENT:
    emit8(cc, 0x89);
    emit8(cc, 0xC1); /* mov ecx, eax */
    emit8(cc, 0x89);
    emit8(cc, 0xD8); /* mov eax, ebx */
    if (is_unsigned) {
      emit8(cc, 0x31);
      emit8(cc, 0xD2); /* xor edx, edx */
      emit8(cc, 0xF7);
      emit8(cc, 0xF1); /* div ecx */
    } else {
      emit8(cc, 0x99); /* cdq */
      emit8(cc, 0xF7);
      emit8(cc, 0xF9); /* idiv ecx */
    }
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
    emit8(cc, is_unsigned ? 0x92 : 0x9C);
    emit8(cc, 0xC0); /* setb/setl al */
    emit_movzx_eax_al(cc);
    break;
  case CC_TOK_GT:
    emit8(cc, 0x39);
    emit8(cc, 0xC3);
    emit8(cc, 0x0F);
    emit8(cc, is_unsigned ? 0x97 : 0x9F);
    emit8(cc, 0xC0); /* seta/setg al */
    emit_movzx_eax_al(cc);
    break;
  case CC_TOK_LE:
    emit8(cc, 0x39);
    emit8(cc, 0xC3);
    emit8(cc, 0x0F);
    emit8(cc, is_unsigned ? 0x96 : 0x9E);
    emit8(cc, 0xC0); /* setbe/setle al */
    emit_movzx_eax_al(cc);
    break;
  case CC_TOK_GE:
    emit8(cc, 0x39);
    emit8(cc, 0xC3);
    emit8(cc, 0x0F);
    emit8(cc, is_unsigned ? 0x93 : 0x9D);
    emit8(cc, 0xC0); /* setae/setge al */
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
    emit8(cc, is_unsigned ? 0xE8 : 0xF8); /* shr/sar eax, cl */
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

/*  *  SSE packed intrinsics (_mm_*_ps)
 *
 *  Recognized by identifier at call-expression parse time and inlined
 *  as a single SSE instruction (no function-call overhead). See
 *  kernel/simd_intrin.h for the user-facing declarations.
 **/

/* Flag bits for cc_intrin_t.flags */
#define CC_INTR_COMMUT    0x01  /* op is commutative: xmm0 <op>= xmm1 */
#define CC_INTR_SWAP      0x02  /* swap operands before emitting (gt/ge) */
#define CC_INTR_RET_INT   0x04  /* result type is int (movemask) */
#define CC_INTR_SET1      0x08  /* _mm_set1_{ps,pd}: scalar broadcast */
#define CC_INTR_MOVEMASK  0x10  /* _mm_movemask_ps: MOVMSKPS xmm->EAX */
#define CC_INTR_PD        0x20  /* double-precision packed (double2) */

typedef struct {
  const char *name;
  uint8_t prefix;   /* 0x66, 0xF3, 0xF2, or 0x00 (none) */
  uint8_t opcode;   /* primary SSE opcode after 0x0F */
  uint8_t arity;    /* 1 = unary (sqrt/set1/movemask), 2 = binary */
  int8_t  imm8;     /* -1 = no imm; 0..7 = CMPPS predicate to append */
  uint8_t flags;    /* CC_INTR_* bitmask */
} cc_intrin_t;

static const cc_intrin_t cc_intrin_table[] = {
    /* Arithmetic (ADDPS/SUBPS/MULPS/DIVPS/MINPS/MAXPS), all 0x0F xx.
     * MINPS and MAXPS keep source order because NaN and signed zero select
     * the second machine operand. */
    { "_mm_add_ps",    0x00, 0x58, 2, -1, 0 },
    { "_mm_sub_ps",    0x00, 0x5C, 2, -1, 0 },
    { "_mm_mul_ps",    0x00, 0x59, 2, -1, 0 },
    { "_mm_div_ps",    0x00, 0x5E, 2, -1, 0 },
    { "_mm_min_ps",    0x00, 0x5D, 2, -1, 0 },
    { "_mm_max_ps",    0x00, 0x5F, 2, -1, 0 },
    { "_mm_sqrt_ps",   0x00, 0x51, 1, -1, 0 },

    /* Bitwise (ANDPS/ORPS/XORPS) - all commutative. */
    { "_mm_and_ps",    0x00, 0x54, 2, -1, CC_INTR_COMMUT },
    { "_mm_or_ps",     0x00, 0x56, 2, -1, CC_INTR_COMMUT },
    { "_mm_xor_ps",    0x00, 0x57, 2, -1, CC_INTR_COMMUT },

    /* Compare (CMPPS 0x0F C2 /r ib). Predicates:
     *   0=eq, 1=lt, 2=le, 3=unord, 4=neq, 5=nlt, 6=nle, 7=ord.
     * Commutative in the sense that operand ordering doesn't change the
     * lane-wise result for eq/neq.  cmpgt/cmpge are synthesised by
     * swapping operands and reusing cmplt/cmple.*/
    { "_mm_cmpeq_ps",  0x00, 0xC2, 2, 0, CC_INTR_COMMUT },
    { "_mm_cmplt_ps",  0x00, 0xC2, 2, 1, 0 },
    { "_mm_cmple_ps",  0x00, 0xC2, 2, 2, 0 },
    { "_mm_cmpneq_ps", 0x00, 0xC2, 2, 4, CC_INTR_COMMUT },
    { "_mm_cmpgt_ps",  0x00, 0xC2, 2, 1, CC_INTR_SWAP },
    { "_mm_cmpge_ps",  0x00, 0xC2, 2, 2, CC_INTR_SWAP },

    /* Broadcast + movemask (special codegen paths). */
    { "_mm_set1_ps",     0x00, 0x00, 1, -1, CC_INTR_SET1 },
    { "_mm_movemask_ps", 0x00, 0x50, 1, -1, CC_INTR_MOVEMASK | CC_INTR_RET_INT },

    /* Double-precision packed counterparts.
     * Same opcodes as the _ps ops but with a 0x66 operand-size prefix.
     * Arg and result type is double2 (two 64-bit lanes).*/
    { "_mm_add_pd",    0x66, 0x58, 2, -1, CC_INTR_PD },
    { "_mm_sub_pd",    0x66, 0x5C, 2, -1, CC_INTR_PD },
    { "_mm_mul_pd",    0x66, 0x59, 2, -1, CC_INTR_PD },
    { "_mm_div_pd",    0x66, 0x5E, 2, -1, CC_INTR_PD },
    { "_mm_sqrt_pd",   0x66, 0x51, 1, -1, CC_INTR_PD },
    { "_mm_min_pd",    0x66, 0x5D, 2, -1, CC_INTR_PD },
    { "_mm_max_pd",    0x66, 0x5F, 2, -1, CC_INTR_PD },
    /* Bitwise double-precision (ANDPD/ORPD/XORPD share opcodes with _ps
     * variants; the 0x66 prefix selects the pd form).*/
    { "_mm_and_pd",    0x66, 0x54, 2, -1, CC_INTR_COMMUT | CC_INTR_PD },
    { "_mm_or_pd",     0x66, 0x56, 2, -1, CC_INTR_COMMUT | CC_INTR_PD },
    { "_mm_xor_pd",    0x66, 0x57, 2, -1, CC_INTR_COMMUT | CC_INTR_PD },
    /* Broadcast: scalar double into both lanes via SHUFPD xmm0,xmm0,0. */
    { "_mm_set1_pd",   0x66, 0x00, 1, -1, CC_INTR_SET1 | CC_INTR_PD },

    { NULL, 0, 0, 0, 0, 0 }
};

/* Look up name in the intrinsic table.  Returns NULL if not an intrinsic.
 * Requires the name to start with "_mm_" to keep the hot path cheap.*/
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

  /* pd intrinsics carry CC_INTR_PD and return double2; _ps intrinsics
   * return float4. Movemask is handled explicitly below.*/
  int is_pd = (intr->flags & CC_INTR_PD) != 0;
  cc_type_t vec_type = is_pd ? TYPE_DOUBLE2 : TYPE_FLOAT4;

  /* _mm_set1_{ps,pd} takes a scalar float/int/double and broadcasts;
   * everything else takes a vector (float4/double2) first argument.*/
  if (intr->flags & CC_INTR_SET1) {
    if (is_pd) {
      /* _mm_set1_pd - broadcast a scalar double into both 64-bit lanes. */
      if (cc_last_expr_type == TYPE_INT || cc_last_expr_type == TYPE_UINT) {
        cc_emit_integer_to_fp(cc, cc_last_expr_type, TYPE_DOUBLE, 0);
        cc_last_expr_type = TYPE_DOUBLE;
      }
      if (cc_last_expr_type == TYPE_FLOAT) {
        /* Widen float to double: CVTSS2SD xmm0, xmm0. */
        emit_cvtss2sd(cc, 0, 0);
        cc_last_expr_type = TYPE_DOUBLE;
      }
      if (cc_last_expr_type != TYPE_DOUBLE) {
        cc_error(cc,
                 "_mm_set1_pd requires a double/float/integer scalar argument");
        return;
      }
      /* SHUFPD xmm0, xmm0, 0x00 : 66 0F C6 C0 00 - imm8=0 replicates the
       * low 64-bit lane into both slots.*/
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
    if (cc_last_expr_type == TYPE_INT || cc_last_expr_type == TYPE_UINT) {
      cc_emit_integer_to_fp(cc, cc_last_expr_type, TYPE_FLOAT, 0);
      cc_last_expr_type = TYPE_FLOAT;
    }
    if (cc_last_expr_type != TYPE_FLOAT && cc_last_expr_type != TYPE_DOUBLE) {
      cc_error(cc, "_mm_set1_ps requires a float/integer scalar argument");
      return;
    }
    if (cc_last_expr_type == TYPE_DOUBLE) {
      /* Narrow double->float: CVTSD2SS xmm0, xmm0 */
      emit_cvtsd2ss(cc, 0, 0);
    }
    /* Broadcast lane 0 to all four lanes: SHUFPS xmm0, xmm0, 0x00
     * (imm=0 replicates lane 0 into all four 32-bit slots).*/
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
   * their precision (float4 for _ps, double2 for _pd).*/
  if (cc_last_expr_type != vec_type) {
    cc_error(cc, is_pd ? "_mm_*_pd intrinsic expects a double2 argument"
                       : "_mm_*_ps intrinsic expects a float4 argument");
    return;
  }

  if (intr->arity == 1) {
    /* Unary: XMM0 = OP(XMM0).  Covers sqrt_ps/pd and movemask_ps. */
    if (intr->flags & CC_INTR_MOVEMASK) {
      /* MOVMSKPS eax, xmm0 : 0F 50 /r, reg=EAX=0, r/m=xmm0=0 -> 0xC0.
       * Result is a 4-bit sign mask in EAX - type becomes int.*/
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
    /* Commutative: xmm0 <op>= xmm1 - result directly in XMM0. */
    dst = 0;
    src = 1;
  } else if (intr->flags & CC_INTR_SWAP) {
    /* cmpgt(a,b) == cmplt(b,a): XMM1 (old arg0=a) vs XMM0 (arg1=b).
     * After swap we want the "a vs b" semantics mapped onto cmplt of
     * b and a, so compute (XMM0 op XMM1) directly and leave result
     * in XMM0.  That is: XMM0 op= XMM1 using the base opcode.*/
    dst = 0;
    src = 1;
  } else {
    /* Non-commutative (sub, div, cmplt, cmple, cmpneq-strict).
     * _mm_sub_ps(a, b) = a - b.  XMM1 holds a, XMM0 holds b. So
     * we compute XMM1 <op>= XMM0 (writing into XMM1), then move
     * XMM1 into XMM0 via MOVAPS so callers see the usual
     * XMM0-accumulator convention.*/
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

static int cc_extract_simd_lane(cc_state_t *cc, cc_type_t vector_type) {
  cc_next(cc); /* consume '.' */
  cc_token_t field_token = cc_next(cc);
  if (field_token.type != CC_TOK_IDENT) {
    cc_error(cc, "expected SIMD lane name after '.'");
    return 0;
  }

  char field = field_token.text[0];
  if (field == '\0' || field_token.text[1] != '\0') {
    cc_error(cc, "invalid SIMD lane name");
    return 0;
  }

  if (vector_type == TYPE_FLOAT4) {
    int lane;
    if (field == 'x') lane = 0;
    else if (field == 'y') lane = 1;
    else if (field == 'z') lane = 2;
    else if (field == 'w') lane = 3;
    else {
      cc_error(cc, "float4 lane must be .x, .y, .z, or .w");
      return 0;
    }
    if (lane != 0) {
      uint8_t immediate = (uint8_t)(lane | (lane << 2) |
                                    (lane << 4) | (lane << 6));
      emit8(cc, 0x0F);
      emit8(cc, 0xC6); /* SHUFPS */
      emit8(cc, 0xC0);
      emit8(cc, immediate);
    }
    cc_last_expr_type = TYPE_FLOAT;
    cc_last_expr_elem_size = 4;
  } else {
    if (field == 'y') {
      emit8(cc, 0x66);
      emit8(cc, 0x0F);
      emit8(cc, 0xC6); /* SHUFPD */
      emit8(cc, 0xC0);
      emit8(cc, 0x01);
    } else if (field != 'x') {
      cc_error(cc, "double2 lane must be .x or .y");
      return 0;
    }
    cc_last_expr_type = TYPE_DOUBLE;
    cc_last_expr_elem_size = 8;
  }

  cc_last_expr_struct_index = -1;
  cc_last_expr_dim2 = 0;
  cc_last_expr_array_rank = 0;
  cc_last_expr_array_elem_type = TYPE_INT;
  cc_last_expr_simd_lane = 1;
  cc_last_xmm = 0;
  return 1;
}

static void cc_parse_ident_expr(cc_state_t *cc) {
  char name[CC_MAX_IDENT];
  int i = 0;
  cc_reset_expr_subscript_metadata();
  cc_clear_expr_callable_provenance();
  while (cc->cur.text[i] && i < CC_MAX_IDENT - 1) {
    name[i] = cc->cur.text[i];
    i++;
  }
  name[i] = '\0';

  /* Function call? */
  if (cc_peek(cc).type == CC_TOK_LPAREN) {
    /* Short-circuit recognised SSE intrinsics (`_mm_*_ps`).
     * These inline as a single SSE opcode instead of a call.  Keep this
     * check before any argument parsing so we don't push-then-discard.*/
    const cc_intrin_t *intr = cc_intrin_lookup(name);
    if (intr) {
      cc_next(cc); /* consume '(' */
      cc_emit_intrinsic(cc, intr);
      cc_reset_expr_subscript_metadata();
      cc_last_expr_direct_lvalue_sym = NULL;
      cc_clear_expr_callable_provenance();
      cc_last_expr_indirect_lvalue = 0;
      return;
    }
    cc_symbol_t *call_sym = cc_sym_find(cc, name);
    if (call_sym &&
        (call_sym->kind == SYM_LOCAL || call_sym->kind == SYM_PARAM ||
         call_sym->kind == SYM_GLOBAL) &&
        call_sym->type == TYPE_FUNC_PTR &&
        !cc_symbol_has_cdecl_parameter_metadata(call_sym) &&
        cc_is_simd_value_type(call_sym->function_pointer_return_type)) {
      cc_error(cc, "SIMD function-pointer returns are not supported");
      return;
    }
    cc_next(cc); /* consume '(' */

    /* Evaluate arguments in source order and retain their stack widths. */
    int argc = 0;
    int arg_sizes[CC_MAX_PARAMS];
    int total_arg_bytes = 0;

    if (cc_peek(cc).type != CC_TOK_RPAREN) {
      /* Parse first argument */
      cc_parse_expression(cc, 1);
      if (!cc_emit_call_argument_push(cc, call_sym, argc,
                                      &arg_sizes[argc]))
        return;
      total_arg_bytes += arg_sizes[argc];
      argc++;

      while (cc_match(cc, CC_TOK_COMMA)) {
        cc_parse_expression(cc, 1);
        if (argc >= CC_MAX_PARAMS) {
          cc_error(cc, "too many call arguments");
          break;
        }
        if (!cc_emit_call_argument_push(cc, call_sym, argc,
                                        &arg_sizes[argc]))
          return;
        total_arg_bytes += arg_sizes[argc];
        argc++;
      }
    }
    cc_expect(cc, CC_TOK_RPAREN);

    if (!cc_validate_named_function_pointer_arity(cc, call_sym, argc))
      return;

    if (!cc_emit_cdecl_argument_layout(cc, arg_sizes, argc))
      return;

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
      cc_reset_expr_subscript_metadata();
      return;
    }

    /* Look up function */
    cc_symbol_t *sym = call_sym;
    /* Remember callee's return type so we can set cc_last_expr_type
     * correctly after cleanup (default is TYPE_INT for unknown/forward refs).*/
    cc_type_t call_ret_type = TYPE_INT;
    int call_ret_struct_index = -1;
    if (sym && (sym->kind == SYM_FUNC || sym->kind == SYM_KERNEL)) {
      call_ret_type = sym->type;
      call_ret_struct_index = sym->struct_index;
    } else if (sym &&
               (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM ||
                sym->kind == SYM_GLOBAL) &&
               sym->type == TYPE_FUNC_PTR) {
      call_ret_type = sym->function_pointer_return_type;
      call_ret_struct_index = sym->struct_index;
    }
    if (sym) {
      /* HolyC-style auto-main: if the user explicitly calls main() at the
       * top level, suppress the post-parse auto-call so main doesn't run
       * twice. Only flag for SYM_FUNC (a kernel binding called "main"
       * would be unusual and shouldn't toggle this).*/
      if (cc->in_top_level && sym->kind == SYM_FUNC &&
          strcmp(name, "main") == 0) {
        cc->main_called_top_level = 1;
      }
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
            p->buffer_offset = patch_pos;
            p->kind = CC_PATCH_CODE_RELATIVE;
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
      if (cc->in_top_level && strcmp(name, "main") == 0) {
        cc->main_called_top_level = 1;
      }
      cc_symbol_t *fsym = cc_sym_add(cc, name, SYM_FUNC, TYPE_INT);
      if (fsym) {
        fsym->param_count = argc;
        fsym->is_defined = 0;
      }
      uint32_t patch_pos = emit_call_rel_placeholder(cc);
      if (cc->patch_count < CC_MAX_PATCHES) {
        cc_patch_t *p = &cc->patches[cc->patch_count++];
        p->buffer_offset = patch_pos;
        p->kind = CC_PATCH_CODE_RELATIVE;
        int pi = 0;
        while (name[pi] && pi < CC_MAX_IDENT - 1) {
          p->name[pi] = name[pi];
          pi++;
        }
        p->name[pi] = '\0';
      }
    }

    /* Clean up the complete outgoing area. Four, eight, and sixteen-byte
     * slots all contribute their actual width to total_arg_bytes. */
    if (total_arg_bytes > 0) {
      emit_add_esp(cc, (int32_t)total_arg_bytes);
    }

    if (strcmp(name, "print") == 0 || strcmp(name, "println") == 0) {
      cc_last_expr_type = TYPE_VOID;
    } else {
      /* Floating and packed results live in XMM0. Other ordinary results
       * use EAX. */
      cc_last_expr_type = call_ret_type;
      if (call_ret_type == TYPE_FLOAT || call_ret_type == TYPE_DOUBLE ||
          call_ret_type == TYPE_FLOAT4 || call_ret_type == TYPE_DOUBLE2) {
        cc_last_xmm = 0;
      }
    }
    cc_seed_pointer_subscript_metadata(cc, cc_last_expr_type,
                                       call_ret_struct_index);
    cc_last_expr_direct_lvalue_sym = NULL;
    cc_clear_expr_callable_provenance();
    cc_last_expr_indirect_lvalue = 0;
    return;
  }

  /* Variable reference */
  cc_symbol_t *sym = cc_sym_find(cc, name);
  if (!sym) {
    cc_error(cc, "undefined variable");
    return;
  }

  if (sym->type == TYPE_FUNC_PTR)
    cc_set_expr_function_signature(sym);

  if (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM) {
    if (sym->is_array || sym->type == TYPE_STRUCT) {
      /* Arrays and structs: load the base address via LEA, not the value */
      emit_lea_local(cc, sym->offset);
    } else if (sym->type == TYPE_FLOAT) {
      /* Load float local into XMM0. */
      emit_movss_xmm_local(cc, 0, sym->offset);
      cc_last_xmm = 0;
    } else if (sym->type == TYPE_DOUBLE) {
      emit_movsd_xmm_local(cc, 0, sym->offset);
      cc_last_xmm = 0;
    } else if (sym->type == TYPE_FLOAT4 || sym->type == TYPE_DOUBLE2) {
      /* Load 16-byte SIMD local into XMM0 via MOVUPS.
       * (MOVUPS instead of MOVAPS because EBP-relative offsets aren't
       * guaranteed 16-aligned under the prologue - see
       * emit_movups_xmm_local comment.)
       *
       * If the next token is '.', extract a scalar element
       * (.x/.y/.z/.w for float4, .x/.y for double2) and leave that
       * scalar in XMM0's low lane.  SHUFPS imm8 = lane*0x55 broadcasts
       * a given 32-bit lane into position 0 (and the other three, but
       * scalar math only reads the low lane).  SHUFPD imm8 = 0x01
       * swaps the two 64-bit lanes of double2.*/
      emit_movups_xmm_local(cc, 0, sym->offset);
      cc_last_xmm = 0;
      cc_last_expr_type = sym->type;
      cc_last_expr_struct_index = sym->struct_index;
      cc_last_expr_elem_size = 16;

      if (cc_peek(cc).type == CC_TOK_DOT &&
          !cc_extract_simd_lane(cc, sym->type))
        return;
      return;
    } else {
      emit_load_local(cc, sym->offset);
    }
    cc_last_expr_type = sym->type;
    cc_last_expr_struct_index = sym->struct_index;
    cc_last_expr_dim2 = (sym->is_array) ? sym->array_dim2 : 0;
    cc_last_expr_array_rank = sym->is_array ? sym->array_rank : 0;
    cc_last_expr_array_elem_type =
        sym->is_array ? sym->array_elem_type : TYPE_INT;
    cc_last_expr_array_object_size =
        sym->is_array ? sym->array_object_size : 0;
    if (sym->is_array && sym->array_elem_size > 0)
      cc_last_expr_elem_size = sym->array_elem_size;
    else if ((sym->type == TYPE_STRUCT_PTR || sym->type == TYPE_STRUCT) &&
             sym->struct_index >= 0 && sym->struct_index < cc->struct_count)
      cc_last_expr_elem_size = cc->structs[sym->struct_index].total_size;
    else if (sym->type == TYPE_CHAR_PTR || sym->type == TYPE_CHAR)
      cc_last_expr_elem_size = 1;
    else if (sym->type == TYPE_FLOAT_PTR) {
      cc_last_expr_elem_size = 4;
      cc_last_expr_array_elem_type = TYPE_FLOAT;
    } else if (sym->type == TYPE_DOUBLE_PTR) {
      cc_last_expr_elem_size = 8;
      cc_last_expr_array_elem_type = TYPE_DOUBLE;
    } else if (sym->type == TYPE_UINT_PTR) {
      cc_last_expr_elem_size = 4;
      cc_last_expr_array_elem_type = TYPE_UINT;
    } else if (cc_is_simd_value_type(sym->type)) {
      cc_last_expr_elem_size = 16;
    } else
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
    } else if (sym->type == TYPE_FLOAT4 || sym->type == TYPE_DOUBLE2) {
      emit_movups_xmm_disp32(cc, 0, sym->address);
      cc_last_xmm = 0;
    } else {
      /* Scalar: load value from memory */
      emit8(cc, 0xA1); /* mov eax, [addr] */
      emit32(cc, sym->address);
    }
    cc_last_expr_type = sym->type;
    cc_last_expr_struct_index = sym->struct_index;
    cc_last_expr_dim2 = (sym->is_array) ? sym->array_dim2 : 0;
    cc_last_expr_array_rank = sym->is_array ? sym->array_rank : 0;
    cc_last_expr_array_elem_type =
        sym->is_array ? sym->array_elem_type : TYPE_INT;
    cc_last_expr_array_object_size =
        sym->is_array ? sym->array_object_size : 0;
    if (sym->is_array && sym->array_elem_size > 0)
      cc_last_expr_elem_size = sym->array_elem_size;
    else if ((sym->type == TYPE_STRUCT_PTR || sym->type == TYPE_STRUCT) &&
             sym->struct_index >= 0 && sym->struct_index < cc->struct_count)
      cc_last_expr_elem_size = cc->structs[sym->struct_index].total_size;
    else if (sym->type == TYPE_CHAR_PTR || sym->type == TYPE_CHAR)
      cc_last_expr_elem_size = 1;
    else if (sym->type == TYPE_FLOAT_PTR) {
      cc_last_expr_elem_size = 4;
      cc_last_expr_array_elem_type = TYPE_FLOAT;
    } else if (sym->type == TYPE_DOUBLE_PTR) {
      cc_last_expr_elem_size = 8;
      cc_last_expr_array_elem_type = TYPE_DOUBLE;
    } else if (sym->type == TYPE_UINT_PTR) {
      cc_last_expr_elem_size = 4;
      cc_last_expr_array_elem_type = TYPE_UINT;
    } else if (cc_is_simd_value_type(sym->type)) {
      cc_last_expr_elem_size = 16;
    } else
      cc_last_expr_elem_size = 4;
  } else if (sym->kind == SYM_FUNC) {
    /* Load function address into eax */
    if (sym->is_defined) {
      emit_mov_eax_imm(cc, cc->code_base + (uint32_t)sym->offset);
    } else {
      uint32_t patch_pos;
      emit8(cc, 0xB8); /* mov eax, imm32 */
      patch_pos = cc->code_pos;
      emit32(cc, 0);
      if (cc->patch_count >= CC_MAX_PATCHES) {
        cc_error(cc, "too many forward function references");
        return;
      }
      {
        cc_patch_t *patch = &cc->patches[cc->patch_count++];
        int name_index = 0;
        patch->buffer_offset = patch_pos;
        patch->kind = CC_PATCH_CODE_ABSOLUTE;
        while (name[name_index] && name_index < CC_MAX_IDENT - 1) {
          patch->name[name_index] = name[name_index];
          name_index++;
        }
        patch->name[name_index] = '\0';
      }
    }
    cc_last_expr_type = TYPE_FUNC_PTR;
    cc_set_expr_function_signature(sym);
  } else if (sym->kind == SYM_KERNEL) {
    emit_mov_eax_imm(cc, sym->address);
    cc_last_expr_type = TYPE_FUNC_PTR;
    cc_set_expr_function_signature(sym);
  }
}

static void cc_parse_primary(cc_state_t *cc) {
  if (cc->error)
    return;

  cc_reset_expr_subscript_metadata();
  cc_last_expr_indirect_lvalue = 0;
  cc_last_expr_direct_lvalue_sym = NULL;
  cc_clear_expr_callable_provenance();
  cc_last_expr_simd_lane = 0;
  cc_last_expr_const_lvalue = 0;
  cc_token_t tok = cc_next(cc);
  int address_of_array_element = 0;
  int address_of_member = 0;

  switch (tok.type) {
  case CC_TOK_NUMBER:
    emit_mov_eax_imm(cc, (uint32_t)tok.int_value);
    cc_last_expr_type = tok.int_is_unsigned ? TYPE_UINT : TYPE_INT;
    cc_publish_integer_constant_expression((uint32_t)tok.int_value,
                                           tok.int_is_unsigned);
    break;

  case CC_TOK_FLIT: {
    /* Emit the raw bits into the data segment and load them
     * into XMM0 via MOVSS (float) or MOVSD (double).  XMM0 is the
     * "FP accumulator" mirroring EAX for the integer path.*/
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
    cc_publish_integer_constant_expression((uint32_t)tok.int_value, 0);
    break;

  case CC_TOK_STRING: {
    /* ISO C §6.4.5p5 adjacent string literal concatenation:
     * "foo" "bar" parses as a single literal "foobar".*/
    uint32_t str_addr = cc_emit_adjacent_string_literal(cc, tok);
    if (str_addr == 0) {
      return;
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
        cc_last_expr_direct_lvalue_sym = sym;
      }
      if (sym)
        cc_last_expr_const_lvalue = sym->is_const_qualified;
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
      int type_array_count = cc_last_type_array_count;
      size = cc_type_size(cc, t, si);
      if (t == TYPE_STRUCT && !cc_struct_is_complete(cc, si))
        cc_error(cc, "sizeof: incomplete struct");
      if (!cc->error && type_array_count > 0) {
        int32_t array_size;
        if (!cc_checked_array_bytes(cc, type_array_count, size, &array_size))
          break;
        size = array_size;
      }
    } else {
      uint32_t saved_code_pos = cc->code_pos;
      uint32_t saved_data_pos = cc->data_pos;
      int saved_sym_count = cc->sym_count;
      int saved_struct_count = cc->struct_count;
      int saved_local_offset = cc->local_offset;
      int saved_max_local_offset = cc->max_local_offset;
      int saved_scope_start = cc->scope_start;
      int saved_param_count = cc->param_count;
      int saved_patch_count = cc->patch_count;
      int saved_label_count = cc->label_count;
      int saved_control_depth = cc->control_depth;
      int saved_statement_depth = cc->statement_depth;
      uint32_t saved_entry_offset = cc->entry_offset;
      int saved_has_entry = cc->has_entry;
      int saved_typedef_count = cc->typedef_count;
      int saved_in_top_level = cc->in_top_level;
      int saved_main_called = cc->main_called_top_level;
      int saved_last_type_struct_index = cc_last_type_struct_index;
      uint8_t saved_xmm_inuse = cc_xmm_inuse;
      int saved_last_xmm = cc_last_xmm;
      cc_type_t operand_type;
      int operand_struct_index;
      int operand_array_object_size;

      /* Type-check the operand, then discard every emission side effect.
       * A diagnostic from the operand remains active. A raw SIMD row may
       * reach this exact expression boundary because sizeof does not decay
       * an array operand. Recursive operators and calls still reject it. */
      int saved_sizeof_simd_row_depth = cc_sizeof_simd_row_depth;
      cc_sizeof_simd_row_depth = cc_expression_depth + 1;
      cc_parse_expression(cc, 1);
      cc_sizeof_simd_row_depth = saved_sizeof_simd_row_depth;
      operand_type = cc_last_expr_type;
      operand_struct_index = cc_last_expr_struct_index;
      operand_array_object_size = cc_last_expr_array_object_size;
      cc->code_pos = saved_code_pos;
      cc->data_pos = saved_data_pos;
      cc->sym_count = saved_sym_count;
      cc->struct_count = saved_struct_count;
      cc->local_offset = saved_local_offset;
      cc->max_local_offset = saved_max_local_offset;
      cc->scope_start = saved_scope_start;
      cc->param_count = saved_param_count;
      cc->patch_count = saved_patch_count;
      cc->label_count = saved_label_count;
      cc->control_depth = saved_control_depth;
      cc->statement_depth = saved_statement_depth;
      cc->entry_offset = saved_entry_offset;
      cc->has_entry = saved_has_entry;
      cc->typedef_count = saved_typedef_count;
      cc->in_top_level = saved_in_top_level;
      cc->main_called_top_level = saved_main_called;
      cc_last_type_struct_index = saved_last_type_struct_index;
      cc_xmm_inuse = saved_xmm_inuse;
      cc_last_xmm = saved_last_xmm;

      if (!cc->error) {
        size = operand_array_object_size > 0
                   ? operand_array_object_size
                   : cc_type_size(cc, operand_type, operand_struct_index);
        if (size <= 0)
          cc_error(cc, "sizeof: operand has no object size");
      }
    }
    cc_expect(cc, CC_TOK_RPAREN);
    if (cc->error)
      break;
    if (size < 0)
      size = 0;
    emit_mov_eax_imm(cc, (uint32_t)size);
    cc_last_expr_type = TYPE_UINT;
    cc_last_expr_struct_index = -1;
    cc_last_expr_indirect_lvalue = 0;
    cc_last_expr_direct_lvalue_sym = NULL;
    cc_reset_expr_subscript_metadata();
    cc_clear_expr_callable_provenance();
    cc_publish_integer_constant_expression((uint32_t)size, 1);
    break;
  }

  case CC_TOK_LPAREN: {
    /* Check for type cast: (int)expr, (char*)expr, (struct Foo*)expr */
    cc_token_t p = cc_peek(cc);
    if (cc_is_type_or_typedef(cc, p)) {
      cc_type_t cast_type = cc_parse_type(cc);
      int cast_si = cc_last_type_struct_index;
      int cast_array_count = cc_last_type_array_count;
      cc_expect(cc, CC_TOK_RPAREN);
      cc_parse_primary(cc);
      if (cc_reject_incomplete_simd_row(cc))
        break;
      cc_type_t src_type = cc_last_expr_type;
      int source_is_integer_constant =
          cc_last_expr_is_integer_constant_expression;
      uint32_t source_integer_constant =
          cc_last_expr_integer_constant_value;
      /* Integer <-> float <-> double explicit casts. Character values use
       * the same EAX representation as integers after byte loads, so their
       * FP conversions use the scalar integer CVT instructions too. */
      if (src_type != cast_type) {
        if (cast_type == TYPE_UINT &&
            (src_type == TYPE_FLOAT || src_type == TYPE_DOUBLE)) {
          if (!cc_coerce_unsigned_conversion(cc, cast_type, src_type))
            break;
        } else if (cast_type == TYPE_UINT &&
                   src_type != TYPE_INT && src_type != TYPE_CHAR &&
                   !cc_is_object_pointer_type(src_type) &&
                   src_type != TYPE_FUNC_PTR) {
          cc_error(cc,
                   "conversion to unsigned requires a scalar word or floating value");
          break;
        } else if ((src_type == TYPE_FLOAT || src_type == TYPE_DOUBLE) &&
                   (cc_is_object_pointer_type(cast_type) ||
                    cast_type == TYPE_FUNC_PTR)) {
          cc_error(cc, "floating to pointer conversion is not supported");
          break;
        } else if ((src_type == TYPE_INT || src_type == TYPE_UINT ||
             src_type == TYPE_CHAR) &&
            cast_type == TYPE_FLOAT) {
          cc_emit_integer_to_fp(cc, src_type, cast_type, 0);
          cc_last_xmm = 0;
        } else if ((src_type == TYPE_INT || src_type == TYPE_UINT ||
                    src_type == TYPE_CHAR) &&
                   cast_type == TYPE_DOUBLE) {
          cc_emit_integer_to_fp(cc, src_type, cast_type, 0);
          cc_last_xmm = 0;
        } else if (src_type == TYPE_FLOAT &&
                   (cast_type == TYPE_INT || cast_type == TYPE_CHAR)) {
          /* float in XMM0 -> integer value in EAX (truncating) */
          emit_cvttss2si(cc, 0);
        } else if (src_type == TYPE_DOUBLE &&
                   (cast_type == TYPE_INT || cast_type == TYPE_CHAR)) {
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
         * to/from pointer via a cast is NOT supported - intermediate
         * (int) cast is required.*/
      }
      if (cast_array_count > 0) {
        cc_error(cc, "cast target cannot be an array type");
        break;
      }
      cc_seed_pointer_subscript_metadata(cc, cast_type, cast_si);
      cc_last_expr_type = cast_type;
      cc_last_expr_struct_index = cast_si;
      cc_last_expr_direct_lvalue_sym = NULL;
      cc_clear_expr_callable_provenance();
      cc_last_expr_function_signature_erased = cast_type == TYPE_PTR;
      if (source_is_integer_constant &&
          (cast_type == TYPE_INT || cast_type == TYPE_UINT ||
           cast_type == TYPE_CHAR)) {
        uint32_t cast_value = source_integer_constant;
        if (cast_type == TYPE_CHAR)
          cast_value &= 0xffu;
        cc_publish_integer_constant_expression(
            cast_value, cast_type == TYPE_UINT);
      }
      cc_last_expr_indirect_lvalue = 0;
    } else {
      int saved_sizeof_simd_row_depth = cc_sizeof_simd_row_depth;
      int saved_grouped_simd_row_depth = cc_grouped_simd_row_depth;
      if (cc_sizeof_simd_row_depth == cc_expression_depth)
        cc_sizeof_simd_row_depth = cc_expression_depth + 1;
      cc_grouped_simd_row_depth = cc_expression_depth + 1;
      cc_parse_expression(cc, 1);
      cc_grouped_simd_row_depth = saved_grouped_simd_row_depth;
      cc_sizeof_simd_row_depth = saved_sizeof_simd_row_depth;
      cc_expect(cc, CC_TOK_RPAREN);
    }
    break;
  }

  case CC_TOK_STAR: {
    /* Dereference: *expr */
    cc_parse_primary(cc);
    if (cc_reject_incomplete_simd_row(cc))
      break;
    cc_type_t ptr_type = cc_last_expr_type;
    cc_type_t object_type = cc_pointed_object_type(ptr_type);
    if (object_type == TYPE_VOID) {
      cc_error(cc, "dereference requires a supported pointer");
      return;
    }
    (void)cc_emit_indirect_scalar_load(cc, object_type);
    cc_last_expr_indirect_lvalue = 1;
    cc_last_expr_direct_lvalue_sym = NULL;
    break;
  }

  case CC_TOK_AMP: {
    /* Address-of: &var, &record.field, or &pointer->field. */
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
    if (sym->type == TYPE_FLOAT4 || sym->type == TYPE_DOUBLE2 ||
        (sym->is_array &&
         (sym->array_elem_type == TYPE_FLOAT4 ||
          sym->array_elem_type == TYPE_DOUBLE2))) {
      cc_error(cc, "SIMD pointer expressions are not supported");
      return;
    }
    cc_token_type_t designator = cc_peek(cc).type;
    address_of_member = designator == CC_TOK_DOT ||
                        designator == CC_TOK_ARROW;
    if (designator == CC_TOK_ARROW) {
      if (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM) {
        emit_load_local(cc, sym->offset);
      } else if (sym->kind == SYM_GLOBAL) {
        emit8(cc, 0xA1);
        emit32(cc, sym->address);
      } else {
        cc_error(cc, "cannot take address of function member");
        return;
      }
    } else if (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM) {
      emit_lea_local(cc, sym->offset);
    } else if (sym->kind == SYM_GLOBAL) {
      emit_mov_eax_imm(cc, sym->address);
    } else {
      cc_error(cc, "cannot take address of function");
      return;
    }
    if (designator == CC_TOK_ARROW) {
      cc_last_expr_type = sym->type;
    } else if (sym->is_array && designator == CC_TOK_LBRACK) {
      /* Keep the array's pointer-shaped type until its subscripts have
       * produced the selected address. The postfix path must not load the
       * final element for an address-of expression. */
      cc_last_expr_type = sym->type;
      address_of_array_element = 1;
    } else {
      cc_last_expr_type = cc_object_pointer_type(sym->type);
    }
    cc_last_expr_struct_index = sym->struct_index;
    cc_last_expr_direct_lvalue_sym = NULL;
    cc_clear_expr_callable_provenance();
    cc_last_expr_indirect_lvalue = 0;
    cc_last_expr_simd_lane = 0;
    cc_last_expr_dim2 = (sym->is_array) ? sym->array_dim2 : 0;
    cc_last_expr_array_rank = sym->is_array ? sym->array_rank : 0;
    cc_last_expr_array_elem_type =
        sym->is_array ? sym->array_elem_type : TYPE_INT;
    if (sym->is_array && sym->array_elem_size > 0)
      cc_last_expr_elem_size = sym->array_elem_size;
    else if ((sym->type == TYPE_STRUCT || sym->type == TYPE_STRUCT_PTR) &&
             sym->struct_index >= 0 && sym->struct_index < cc->struct_count)
      cc_last_expr_elem_size = cc->structs[sym->struct_index].total_size;
    else if (sym->type == TYPE_CHAR || sym->type == TYPE_CHAR_PTR)
      cc_last_expr_elem_size = 1;
    else if (sym->type == TYPE_FLOAT) {
      cc_last_expr_elem_size = 4;
      cc_last_expr_array_elem_type = TYPE_FLOAT;
    } else if (sym->type == TYPE_DOUBLE) {
      cc_last_expr_elem_size = 8;
      cc_last_expr_array_elem_type = TYPE_DOUBLE;
    } else if (sym->type == TYPE_UINT || sym->type == TYPE_UINT_PTR) {
      cc_last_expr_elem_size = 4;
      cc_last_expr_array_elem_type = TYPE_UINT;
    }
    else
      cc_last_expr_elem_size = 4;
    break;
  }

  case CC_TOK_NOT: {
    /* Logical NOT: !expr */
    cc_parse_primary(cc);
    int operand_is_integer_constant =
        cc_last_expr_is_integer_constant_expression;
    uint32_t operand_integer_constant =
        cc_last_expr_integer_constant_value;
    if (cc_reject_incomplete_simd_row(cc))
      break;
    if (cc->error ||
        !cc_materialize_scalar_truth(cc, cc_last_expr_type))
      return;
    emit_cmp_eax_zero(cc);
    emit8(cc, 0x0F);
    emit8(cc, 0x94);
    emit8(cc, 0xC0); /* sete al */
    emit_movzx_eax_al(cc);
    cc_last_expr_type = TYPE_INT;
    cc_reset_expr_subscript_metadata();
    cc_last_expr_direct_lvalue_sym = NULL;
    cc_clear_expr_callable_provenance();
    if (operand_is_integer_constant)
      cc_publish_integer_constant_expression(
          operand_integer_constant == 0, 0);
    cc_last_expr_indirect_lvalue = 0;
    break;
  }

  case CC_TOK_BNOT: {
    /* Bitwise NOT: ~expr */
    cc_parse_primary(cc);
    int operand_is_integer_constant =
        cc_last_expr_is_integer_constant_expression;
    uint32_t operand_integer_constant =
        cc_last_expr_integer_constant_value;
    if (cc_reject_incomplete_simd_row(cc))
      break;
    cc_type_t operand_type = cc_last_expr_type;
    if (operand_type != TYPE_INT && operand_type != TYPE_UINT &&
        operand_type != TYPE_CHAR) {
      cc_error(cc, "bitwise not requires an integer operand");
      return;
    }
    emit8(cc, 0xF7);
    emit8(cc, 0xD0); /* not eax */
    cc_last_expr_type = operand_type == TYPE_UINT ? TYPE_UINT : TYPE_INT;
    cc_reset_expr_subscript_metadata();
    cc_last_expr_direct_lvalue_sym = NULL;
    cc_clear_expr_callable_provenance();
    if (operand_is_integer_constant)
      cc_publish_integer_constant_expression(
          ~operand_integer_constant, operand_type == TYPE_UINT);
    cc_last_expr_indirect_lvalue = 0;
    break;
  }

  case CC_TOK_PLUS:
  case CC_TOK_MINUS: {
    /* Unary signs apply to arithmetic scalar operands. Floating results live
     * in XMM0, while integer results live in EAX. */
    int negate = tok.type == CC_TOK_MINUS;
    cc_parse_primary(cc);
    int operand_is_integer_constant =
        cc_last_expr_is_integer_constant_expression;
    uint32_t operand_integer_constant =
        cc_last_expr_integer_constant_value;
    int operand_integer_constant_is_unsigned =
        cc_last_expr_integer_constant_is_unsigned;
    if (cc->error)
      return;
    if (cc_reject_incomplete_simd_row(cc))
      return;

    if (cc_last_expr_type == TYPE_FLOAT) {
      if (negate)
        emit_negate_xmm0_scalar(cc, 0);
      cc_last_xmm = 0;
    } else if (cc_last_expr_type == TYPE_DOUBLE) {
      if (negate)
        emit_negate_xmm0_scalar(cc, 1);
      cc_last_xmm = 0;
    } else if (cc_last_expr_type == TYPE_INT ||
               cc_last_expr_type == TYPE_UINT ||
               cc_last_expr_type == TYPE_CHAR) {
      cc_type_t result_type = cc_last_expr_type == TYPE_UINT
                                  ? TYPE_UINT
                                  : TYPE_INT;
      if (negate) {
        emit8(cc, 0xF7);
        emit8(cc, 0xD8); /* neg eax */
      }
      cc_last_expr_type = result_type;
    } else {
      cc_error(cc, "unary sign requires an arithmetic scalar operand");
      return;
    }
    cc_reset_expr_subscript_metadata();
    cc_last_expr_direct_lvalue_sym = NULL;
    cc_clear_expr_callable_provenance();
    if (operand_is_integer_constant &&
        (operand_integer_constant_is_unsigned || !negate ||
         operand_integer_constant != 0x80000000u)) {
      cc_publish_integer_constant_expression(
          negate ? 0u - operand_integer_constant
                 : operand_integer_constant,
          operand_integer_constant_is_unsigned);
    }
    cc_last_expr_indirect_lvalue = 0;
    break;
  }

  case CC_TOK_NEW: {
    cc_type_t alloc_type = cc_parse_type(cc);
    int alloc_si = cc_last_type_struct_index;
    int alloc_array_count = cc_last_type_array_count;
    int32_t elem_size = cc_type_size(cc, alloc_type, alloc_si);

    if (alloc_array_count > 0) {
      cc_error(cc, "new does not support typedef array types");
      return;
    }

    if (alloc_type == TYPE_FLOAT4 || alloc_type == TYPE_DOUBLE2) {
      cc_error(cc, "SIMD allocation with new is not supported");
      return;
    }

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

    cc_reset_expr_subscript_metadata();
    if (alloc_type == TYPE_CHAR) {
      cc_last_expr_type = TYPE_CHAR_PTR;
      cc_last_expr_elem_size = 1;
    } else if (alloc_type == TYPE_STRUCT) {
      cc_last_expr_type = TYPE_STRUCT_PTR;
      cc_last_expr_struct_index = alloc_si;
      cc_last_expr_elem_size = elem_size;
    } else {
      cc_last_expr_type = cc_object_pointer_type(alloc_type);
      cc_last_expr_elem_size = elem_size;
      cc_last_expr_array_elem_type = alloc_type;
    }
    cc_last_expr_direct_lvalue_sym = NULL;
    cc_last_expr_indirect_lvalue = 0;
    cc_clear_expr_callable_provenance();

    break;
  }

  case CC_TOK_PLUSPLUS: {
    /* Prefix update parses one complete primary designator. Direct variables
     * use their symbol path. A derived floating or SIMD lvalue keeps its
     * address in EAX until the indirect update helper saves it. */
    cc_parse_primary(cc);
    if (cc->error)
      return;
    if (cc_reject_incomplete_simd_row(cc))
      return;
    if (cc_last_expr_indirect_lvalue) {
      if (!cc_emit_indirect_fp_update(
              cc, cc_last_expr_type, 0, 0))
        return;
    } else if (cc_last_expr_direct_lvalue_sym) {
      if (!cc_emit_variable_update(
              cc, cc_last_expr_direct_lvalue_sym, 0, 0))
        return;
      cc_last_expr_direct_lvalue_sym = NULL;
    } else {
      cc_error_update_target(cc);
      return;
    }
    break;
  }

  case CC_TOK_MINUSMINUS: {
    cc_parse_primary(cc);
    if (cc->error)
      return;
    if (cc_reject_incomplete_simd_row(cc))
      return;
    if (cc_last_expr_indirect_lvalue) {
      if (!cc_emit_indirect_fp_update(
              cc, cc_last_expr_type, 1, 0))
        return;
    } else if (cc_last_expr_direct_lvalue_sym) {
      if (!cc_emit_variable_update(
              cc, cc_last_expr_direct_lvalue_sym, 1, 0))
        return;
      cc_last_expr_direct_lvalue_sym = NULL;
    } else {
      cc_error_update_target(cc);
      return;
    }
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

    if (next.type == CC_TOK_DOT &&
        (cc_last_expr_type == TYPE_FLOAT4 ||
         cc_last_expr_type == TYPE_DOUBLE2)) {
      cc_last_expr_direct_lvalue_sym = NULL;
      cc_last_expr_indirect_lvalue = 0;
      if (!cc_extract_simd_lane(cc, cc_last_expr_type))
        return;
      continue;
    }

    /* Struct member access: expr.field or expr->field */
    if (next.type == CC_TOK_DOT || next.type == CC_TOK_ARROW) {
      if (cc_reject_incomplete_simd_row(cc))
        return;
      cc_last_expr_direct_lvalue_sym = NULL;
      cc_last_expr_indirect_lvalue = 0;
      cc_next(cc); /* consume . or -> */
      cc_token_t field_tok = cc_next(cc);
      if (field_tok.type != CC_TOK_IDENT) {
        cc_error(cc, "expected field name");
        return;
      }
      int si = cc_last_expr_struct_index;

      /* Method call sugar: obj.Method(args) -> Class_Method(&obj, args)
       * or ptr->Method(args) -> Class_Method(ptr, args).*/
      if ((cc_last_expr_type == TYPE_STRUCT ||
           cc_last_expr_type == TYPE_STRUCT_PTR) &&
          cc_peek(cc).type == CC_TOK_LPAREN && si >= 0 &&
          si < cc->struct_count) {
        char method_sym_name[CC_MAX_IDENT];
        cc_make_method_symbol(method_sym_name, cc->structs[si].name,
                              field_tok.text);

        cc_symbol_t *msym = cc_sym_find(cc, method_sym_name);

        cc_next(cc); /* consume '(' */

        /* First implicit argument is self pointer in eax. */
        int argc = 0;
        /* Track each stack width for layout and cleanup. */
        int arg_sizes[CC_MAX_PARAMS];
        int total_arg_bytes = 0;
        cc_last_expr_type = TYPE_STRUCT_PTR;
        if (!cc_emit_call_argument_push(cc, msym, argc, &arg_sizes[argc]))
          return;
        total_arg_bytes += arg_sizes[argc];
        argc++;

        if (cc_peek(cc).type != CC_TOK_RPAREN) {
          cc_parse_expression(cc, 1);
          if (argc < CC_MAX_PARAMS) {
            if (!cc_emit_call_argument_push(cc, msym, argc,
                                            &arg_sizes[argc]))
              return;
            total_arg_bytes += arg_sizes[argc];
            argc++;
          }

          while (cc_match(cc, CC_TOK_COMMA)) {
            cc_parse_expression(cc, 1);
            if (argc >= CC_MAX_PARAMS) {
              cc_error(cc, "too many call arguments");
              break;
            }
            if (!cc_emit_call_argument_push(cc, msym, argc,
                                            &arg_sizes[argc]))
              return;
            total_arg_bytes += arg_sizes[argc];
            argc++;
          }
        }
        cc_expect(cc, CC_TOK_RPAREN);

        if (!cc_emit_cdecl_argument_layout(cc, arg_sizes, argc))
          return;

        {
          if (msym) {
            if (msym->kind == SYM_FUNC && msym->is_defined) {
              emit_call_abs(cc, cc->code_base + (uint32_t)msym->offset);
            } else if (msym->kind == SYM_KERNEL) {
              emit_call_abs(cc, msym->address);
            } else if (msym->kind == SYM_FUNC) {
              uint32_t patch_pos = emit_call_rel_placeholder(cc);
              if (cc->patch_count < CC_MAX_PATCHES) {
                cc_patch_t *p = &cc->patches[cc->patch_count++];
                p->buffer_offset = patch_pos;
                p->kind = CC_PATCH_CODE_RELATIVE;
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
                p->buffer_offset = patch_pos;
                p->kind = CC_PATCH_CODE_RELATIVE;
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

        /* Propagate floating and packed return types through XMM0. */
        {
          cc_symbol_t *msym2 = cc_sym_find(cc, method_sym_name);
          cc_type_t mret = TYPE_INT;
          int mret_si = -1;
          if (msym2 && (msym2->kind == SYM_FUNC || msym2->kind == SYM_KERNEL)) {
            mret = msym2->type;
            mret_si = msym2->struct_index;
          }
          cc_last_expr_type = mret;
          if (mret == TYPE_FLOAT || mret == TYPE_DOUBLE ||
              mret == TYPE_FLOAT4 || mret == TYPE_DOUBLE2) {
            cc_last_xmm = 0;
          }
          cc_seed_pointer_subscript_metadata(cc, mret, mret_si);
          cc_last_expr_direct_lvalue_sym = NULL;
          cc_last_expr_indirect_lvalue = 0;
          cc_clear_expr_callable_provenance();
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
      cc_token_type_t after_field = cc_peek(cc).type;
      if (address_of_member && after_field != CC_TOK_DOT &&
          after_field != CC_TOK_ARROW && after_field != CC_TOK_LBRACK) {
        cc_reset_expr_subscript_metadata();
        cc_last_expr_type = cc_object_pointer_type(field->type);
        cc_last_expr_struct_index = field->struct_index;
        address_of_member = 0;
        continue;
      }
      if (address_of_member && after_field == CC_TOK_LBRACK) {
        address_of_array_element = 1;
        address_of_member = 0;
      }
      cc_reset_expr_subscript_metadata();
      /* Determine result: if field is a sub-struct, keep address */
      if (field->array_count > 0) {
        int32_t array_object_size;
        /* Array field: address is already in eax, treat as pointer */
        cc_last_expr_type = cc_object_pointer_type(field->type);
        cc_last_expr_elem_size =
            cc_type_size(cc, field->type, field->struct_index);
        if (!cc_checked_array_bytes(cc, field->array_count,
                                    cc_last_expr_elem_size,
                                    &array_object_size))
          return;
        cc_last_expr_array_object_size = array_object_size;
        cc_last_expr_array_elem_type = field->type;
        cc_last_expr_struct_index = field->struct_index;
      } else if (field->type == TYPE_STRUCT) {
        cc_last_expr_type = TYPE_STRUCT;
        cc_last_expr_struct_index = field->struct_index;
      } else if (field->type == TYPE_STRUCT_PTR) {
        emit_deref_dword(cc);
        cc_last_expr_type = TYPE_STRUCT_PTR;
        cc_last_expr_struct_index = field->struct_index;
      } else if (cc_is_simd_value_type(field->type)) {
        cc_error(cc, after_field == CC_TOK_PLUSPLUS ||
                             after_field == CC_TOK_MINUSMINUS
                         ? "SIMD record-field increment or decrement is not "
                           "supported"
                         : "SIMD record-field values are not supported");
        return;
      } else {
        if (!cc_emit_indirect_scalar_load(cc, field->type))
          return;
        if (field->type == TYPE_FLOAT || field->type == TYPE_DOUBLE)
          cc_last_expr_indirect_lvalue = 1;
      }
      continue;
    }

    if (next.type == CC_TOK_LBRACK) {
      cc_last_expr_direct_lvalue_sym = NULL;
      cc_last_expr_indirect_lvalue = 0;
      /* Array subscript: expr[index] */
      cc_next(cc);
      cc_type_t base_type = cc_last_expr_type;
      cc_type_t base_array_elem_type = cc_last_expr_array_elem_type;
      int base_elem_size = cc_last_expr_elem_size;
      int base_dim2 = cc_last_expr_dim2;
      int base_array_rank = cc_last_expr_array_rank;
      int base_si = cc_last_expr_struct_index;
      int base_is_const = cc_last_expr_const_lvalue;
      emit_push_eax(cc); /* push base address */

      cc_parse_expression(cc, 1);
      cc_last_expr_const_lvalue = base_is_const;

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
      cc_last_expr_direct_lvalue_sym = NULL;
      cc_last_expr_indirect_lvalue = 0;

      /* Determine result type */
      if (base_type == TYPE_STRUCT_PTR) {
        /* Struct array/pointer subscript: address of element */
        cc_last_expr_type = TYPE_STRUCT;
        cc_last_expr_struct_index = base_si;
        cc_last_expr_elem_size = 4;
        cc_last_expr_dim2 = 0;
        cc_last_expr_array_rank = 0;
        cc_last_expr_array_object_size = 0;
        cc_last_expr_array_elem_type = TYPE_INT;
      } else if (base_type == TYPE_CHAR_PTR && base_array_rank > 1) {
        /* Multi-D char first subscript: pointer to row. For 3D arrays
         * the second-stride (dim2) becomes the new elem_size so the
         * NEXT [j] scales correctly.*/
        cc_last_expr_type = TYPE_CHAR_PTR;
        cc_last_expr_elem_size = (base_dim2 > 0) ? base_dim2 : 1;
        cc_last_expr_dim2 = 0;
        cc_last_expr_array_rank = base_array_rank - 1;
        cc_last_expr_array_object_size = base_elem_size;
        cc_last_expr_array_elem_type = TYPE_CHAR;
      } else if (base_type == TYPE_CHAR_PTR) {
        if (!address_of_array_element)
          emit_deref_byte(cc);
        cc_last_expr_type = TYPE_CHAR;
        cc_last_expr_elem_size = 0;
        cc_last_expr_dim2 = 0;
        cc_last_expr_array_rank = 0;
        cc_last_expr_array_object_size = 0;
        cc_last_expr_array_elem_type = TYPE_INT;
      } else if (base_array_elem_type == TYPE_FLOAT ||
                 base_array_elem_type == TYPE_DOUBLE) {
        int scalar_size = cc_type_size(cc, base_array_elem_type, -1);
        if (base_array_rank > 1) {
          cc_last_expr_type = base_type;
          cc_last_expr_elem_size =
              (base_dim2 > 0) ? base_dim2 : scalar_size;
          cc_last_expr_dim2 = 0;
          cc_last_expr_array_rank = base_array_rank - 1;
          cc_last_expr_array_object_size = base_elem_size;
          cc_last_expr_array_elem_type = base_array_elem_type;
        } else {
          if (address_of_array_element) {
            cc_last_expr_type = base_array_elem_type;
            cc_reset_expr_subscript_metadata();
          } else if (!cc_emit_indirect_scalar_load(
                         cc, base_array_elem_type)) {
            return;
          } else {
            cc_last_expr_indirect_lvalue = 1;
          }
        }
      } else if (base_array_elem_type == TYPE_FLOAT4 ||
                 base_array_elem_type == TYPE_DOUBLE2) {
        int vector_size = cc_type_size(cc, base_array_elem_type, -1);
        if (base_array_rank > 1) {
          cc_last_expr_type = base_type;
          cc_last_expr_elem_size =
              (base_dim2 > 0) ? base_dim2 : vector_size;
          cc_last_expr_dim2 = 0;
          cc_last_expr_array_rank = base_array_rank - 1;
          cc_last_expr_array_object_size = base_elem_size;
          cc_last_expr_array_elem_type = base_array_elem_type;
        } else {
          emit_movups_xmm_eax(cc, 0);
          cc_last_xmm = 0;
          cc_last_expr_type = base_array_elem_type;
          cc_last_expr_elem_size = vector_size;
          cc_last_expr_indirect_lvalue = 1;
          cc_last_expr_dim2 = 0;
          cc_last_expr_array_rank = 0;
          cc_last_expr_array_object_size = 0;
          cc_last_expr_array_elem_type = TYPE_INT;
        }
      } else if ((base_type == TYPE_INT_PTR ||
                  base_type == TYPE_UINT_PTR) && base_array_rank > 1) {
        /* Multi-D integer first subscript: pointer to row. */
        cc_last_expr_type = base_type;
        cc_last_expr_elem_size = (base_dim2 > 0) ? base_dim2 : 4;
        cc_last_expr_dim2 = 0;
        cc_last_expr_array_rank = base_array_rank - 1;
        cc_last_expr_array_object_size = base_elem_size;
        cc_last_expr_array_elem_type =
            base_type == TYPE_UINT_PTR ? TYPE_UINT : TYPE_INT;
      } else {
        if (!address_of_array_element)
          emit_deref_dword(cc);
        cc_last_expr_type =
            base_array_elem_type == TYPE_UINT ||
                    base_type == TYPE_UINT_PTR
                ? TYPE_UINT
                : TYPE_INT;
        cc_last_expr_elem_size = 0;
        cc_last_expr_dim2 = 0;
        cc_last_expr_array_rank = 0;
        cc_last_expr_array_object_size = 0;
        cc_last_expr_array_elem_type = TYPE_INT;
      }

      cc_expect(cc, CC_TOK_RBRACK);
      cc_clear_expr_callable_provenance();
      if (address_of_array_element &&
          cc_peek(cc).type != CC_TOK_LBRACK) {
        cc_type_t selected_type = cc_last_expr_type;
        int selected_struct_index = cc_last_expr_struct_index;
        if (selected_type == TYPE_CHAR || selected_type == TYPE_INT ||
            selected_type == TYPE_UINT ||
            selected_type == TYPE_FLOAT || selected_type == TYPE_DOUBLE ||
            selected_type == TYPE_STRUCT) {
          cc_last_expr_type = cc_object_pointer_type(selected_type);
          cc_seed_pointer_subscript_metadata(
              cc, cc_last_expr_type, selected_struct_index);
        } else {
          cc_error(cc, "address of an array row is not supported");
          return;
        }
        cc_last_expr_direct_lvalue_sym = NULL;
        cc_last_expr_indirect_lvalue = 0;
        address_of_array_element = 0;
      }
      continue;
    }

    if (next.type == CC_TOK_PLUSPLUS) {
      if (cc_reject_incomplete_simd_row(cc))
        return;
      cc_next(cc);
      if (!cc_last_expr_direct_lvalue_sym &&
          cc_last_expr_indirect_lvalue &&
          (cc_last_expr_type == TYPE_FLOAT ||
           cc_last_expr_type == TYPE_DOUBLE ||
           cc_is_simd_value_type(cc_last_expr_type))) {
        if (!cc_emit_indirect_fp_update(
                cc, cc_last_expr_type, 0, 1))
          return;
        break;
      }
      if (!cc_last_expr_direct_lvalue_sym) {
        cc_error_update_target(cc);
        return;
      }
      if (!cc_emit_variable_update(
              cc, cc_last_expr_direct_lvalue_sym, 0, 1))
        return;
      cc_last_expr_direct_lvalue_sym = NULL;
      cc_last_expr_indirect_lvalue = 0;
      break;
    }

    if (next.type == CC_TOK_MINUSMINUS) {
      if (cc_reject_incomplete_simd_row(cc))
        return;
      cc_next(cc);
      if (!cc_last_expr_direct_lvalue_sym &&
          cc_last_expr_indirect_lvalue &&
          (cc_last_expr_type == TYPE_FLOAT ||
           cc_last_expr_type == TYPE_DOUBLE ||
           cc_is_simd_value_type(cc_last_expr_type))) {
        if (!cc_emit_indirect_fp_update(
                cc, cc_last_expr_type, 1, 1))
          return;
        break;
      }
      if (!cc_last_expr_direct_lvalue_sym) {
        cc_error_update_target(cc);
        return;
      }
      if (!cc_emit_variable_update(
              cc, cc_last_expr_direct_lvalue_sym, 1, 1))
        return;
      cc_last_expr_direct_lvalue_sym = NULL;
      cc_last_expr_indirect_lvalue = 0;
      break;
    }

    break;
  }
}

static void cc_parse_expression_impl(cc_state_t *cc, int min_prec) {
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
    if (cc_reject_incomplete_simd_row(cc))
      return;

    cc_next(cc); /* consume operator */

    cc_type_t left_type = cc_last_expr_type;
    int left_is_integer_constant =
        cc_last_expr_is_integer_constant_expression;
    uint32_t left_integer_constant =
        cc_last_expr_integer_constant_value;
    int left_integer_constant_is_unsigned =
        cc_last_expr_integer_constant_is_unsigned;
    int left_is_fp = (left_type == TYPE_FLOAT || left_type == TYPE_DOUBLE);
    int left_is_simd =
        (left_type == TYPE_FLOAT4 || left_type == TYPE_DOUBLE2);
    if (left_is_simd) {
      cc_intr_spill_xmm0(cc);
    } else if (left_is_fp) {
      /* Spill XMM0 (the FP accumulator) onto the stack.  Reserve 8 bytes
       * regardless of type so ESP stays 4-byte aligned in both cases.*/
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
    int right_is_integer_constant =
        cc_last_expr_is_integer_constant_expression;
    uint32_t right_integer_constant =
        cc_last_expr_integer_constant_value;
    int right_integer_constant_is_unsigned =
        cc_last_expr_integer_constant_is_unsigned;
    int right_is_fp = (right_type == TYPE_FLOAT || right_type == TYPE_DOUBLE);
    int right_is_simd =
        (right_type == TYPE_FLOAT4 || right_type == TYPE_DOUBLE2);

    cc_last_expr_direct_lvalue_sym = NULL;
    cc_clear_expr_callable_provenance();
    cc_last_expr_indirect_lvalue = 0;
    cc_last_expr_simd_lane = 0;

    /* A binary expression is a computed value, not the array object that a
     * subscript operand may have produced. */
    cc_last_expr_array_object_size = 0;

    if (left_is_simd || right_is_simd) {
      if (!left_is_simd || !right_is_simd || left_type != right_type) {
        cc_error(cc,
                 "SIMD operator requires matching float4 or double2 operands");
        return;
      }

      uint8_t op_byte;
      switch (op.type) {
      case CC_TOK_PLUS:  op_byte = 0x58; break;
      case CC_TOK_MINUS: op_byte = 0x5C; break;
      case CC_TOK_STAR:  op_byte = 0x59; break;
      case CC_TOK_SLASH: op_byte = 0x5E; break;
      default:
        cc_error(cc, "SIMD operator supports only +, -, *, and /");
        return;
      }

      cc_intr_restore_xmm1(cc);
      uint8_t prefix = (left_type == TYPE_DOUBLE2) ? 0x66 : 0x00;
      /* Keep the source-language left operand in the machine destination for
       * every direct packed operator. This makes the emitted order stable even
       * for ADD and MUL. */
      cc_intr_emit_op_rr(cc, prefix, op_byte, 1, 0, -1);
      emit_movaps_xmm_xmm(cc, 0, 1);
      cc_last_expr_type = left_type;
      cc_last_xmm = 0;
      continue;
    }

    if (left_is_fp || right_is_fp) {
      if (!cc_is_arithmetic_scalar_type(left_type) ||
          !cc_is_arithmetic_scalar_type(right_type)) {
        cc_error(cc, "floating operator requires arithmetic scalar operands");
        return;
      }

      /* Scalar SSE arithmetic and comparison.
       *
       * Implicit promotion for mixed int/FP and mixed float/double.
       * The promoted type is determined by cc_promote().
       * Each mismatched case arranges for:
       *    - LHS loaded into XMM1 in the promoted type
       *    - RHS loaded into XMM0 in the promoted type
       *    - ESP restored (spill slot discarded)
       * after which the common SSE-op emit below applies unchanged.
       *
       * Spill-slot layout:
       *    left_is_fp  -> sub esp,8; movs{s,d} [esp], xmm0  (8 bytes)
       *    !left_is_fp -> push eax                         (4 bytes)
*/
      cc_type_t fp_result_type = cc_promote(cc, left_type, right_type);
      int is_double = (fp_result_type == TYPE_DOUBLE);
      uint8_t op_byte = 0;
      int is_comparison = 0;
      int ok = 1;
      switch (op.type) {
      case CC_TOK_PLUS:  op_byte = 0x58; break; /* ADDSS/ADDSD */
      case CC_TOK_MINUS: op_byte = 0x5C; break; /* SUBSS/SUBSD */
      case CC_TOK_STAR:  op_byte = 0x59; break; /* MULSS/MULSD */
      case CC_TOK_SLASH: op_byte = 0x5E; break; /* DIVSS/DIVSD */
      case CC_TOK_EQEQ:
      case CC_TOK_NE:
      case CC_TOK_LT:
      case CC_TOK_GT:
      case CC_TOK_LE:
      case CC_TOK_GE:
        is_comparison = 1;
        break;
      default:
        cc_error(cc, "invalid operator for floating-point operands");
        ok = 0;
        break;
      }

      int need_default_reload = 1;
      if (ok && left_type != right_type) {
        /* Mismatched types - need at least one conversion. */
        if (!left_is_fp) {
          /* Case A/B: LHS int on stack (4 bytes via push eax),
           * RHS FP in XMM0.  Pop LHS, convert to promoted FP into XMM1.
           * Promoted type == RHS type here (int + float -> float,
           * int + double -> double).*/
          emit_pop_eax(cc);
          cc_emit_integer_to_fp(cc, left_type, fp_result_type, 1);
          need_default_reload = 0;
        } else if (!right_is_fp) {
          /* Case C/D: LHS FP on stack (8-byte slot), RHS int in EAX.
           * Convert RHS int in EAX -> promoted FP in XMM0, then reload
           * LHS from stack into XMM1.  LHS was spilled as its original
           * FP type (float=4 bytes, double=8 bytes); if LHS is float
           * but the promoted type is double (can't occur: result is
           * driven by LHS being float with int RHS -> float), we'd
           * need to widen.  In practice LHS-FP + RHS-int always
           * promotes to LHS's FP type.*/
          cc_emit_integer_to_fp(cc, right_type, fp_result_type, 0);
          if (is_double) {
            emit_movsd_xmm_esp(cc, 1);
          } else {
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
           *      Widen RHS (XMM0) from float to double.*/
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
         * Reload LHS into XMM1 and discard spill slot.*/
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
        if (is_comparison) {
          emit_compare_xmm1_xmm0(cc, is_double, op.type);
          cc_last_expr_type = TYPE_INT;
          continue;
        }

        /* Result = LHS OP RHS must land in XMM0.
         *   For + and *: commutative, XMM0 := XMM0 OP XMM1.
         *   For - and /: non-commutative, compute XMM1 OP= XMM0 then
         *                MOVAPS XMM0, XMM1.*/
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

    cc_type_t integer_operation_type = cc_integer_operation_type(
        cc, op.type, left_type, right_type);
    cc_emit_binop(cc, op.type, integer_operation_type);
    /* Arithmetic, bitwise, and shift operations keep their integer type.
     * Comparisons and logical operations produce int. */
    if (op.type == CC_TOK_PLUS || op.type == CC_TOK_MINUS ||
        op.type == CC_TOK_STAR || op.type == CC_TOK_SLASH ||
        op.type == CC_TOK_PERCENT || op.type == CC_TOK_AMP ||
        op.type == CC_TOK_BOR || op.type == CC_TOK_BXOR ||
        op.type == CC_TOK_SHL || op.type == CC_TOK_SHR) {
      cc_last_expr_type = integer_operation_type;
    } else {
      cc_last_expr_type = TYPE_INT;
    }
    if (left_is_integer_constant && right_is_integer_constant) {
      uint32_t constant_result;
      int constant_result_is_unsigned;
      if (cc_eval_integer_constant_binary(
              op.type, left_integer_constant,
              left_integer_constant_is_unsigned, right_integer_constant,
              right_integer_constant_is_unsigned, integer_operation_type,
              &constant_result, &constant_result_is_unsigned)) {
        cc_publish_integer_constant_expression(
            constant_result, constant_result_is_unsigned);
      }
    }
  }

  /* Ternary operator ?: (lowest precedence; right-associative).
   * We keep this outside binary-op precedence handling and only allow
   * it when caller accepts lowest-precedence expressions (min_prec <= 1).*/
  while (!cc->error && min_prec <= 1 && cc_peek(cc).type == CC_TOK_QUESTION) {
    cc_type_t condition_type = cc_last_expr_type;
    int condition_is_integer_constant =
        cc_last_expr_is_integer_constant_expression;
    uint32_t condition_integer_constant =
        cc_last_expr_integer_constant_value;
    if (cc_reject_incomplete_simd_row(cc))
      return;
    cc_next(cc); /* consume ? */

    if (!cc_materialize_scalar_truth(cc, condition_type))
      return;

    /* EAX now holds the condition's normalized scalar truth value. */
    emit8(cc, 0x85);
    emit8(cc, 0xC0); /* test eax, eax */
    uint32_t jz_off = cc->code_pos;
    emit8(cc, 0x0F);
    emit8(cc, 0x84);
    emit32(cc, 0); /* jz <false> placeholder */

    /* Parse true arm. */
    cc_parse_expression(cc, 1);
    cc_type_t true_type = cc_last_expr_type;
    int true_signature_erased =
        cc_last_expr_function_signature_erased;
    int true_is_null_pointer_constant =
        cc_last_expr_is_null_pointer_constant;
    int true_is_integer_constant =
        cc_last_expr_is_integer_constant_expression;
    uint32_t true_integer_constant =
        cc_last_expr_integer_constant_value;
    int true_integer_constant_is_unsigned =
        cc_last_expr_integer_constant_is_unsigned;
    cc_symbol_t *true_function_signatures[CC_MAX_PARAMS];
    int true_function_signature_count =
        cc_last_expr_function_signature_count;
    memcpy(true_function_signatures,
           cc_last_expr_function_signature_candidates,
           sizeof(*true_function_signatures) *
               (size_t)true_function_signature_count);

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
     * a ? b : c ? d : e  => a ? b : (c ? d : e).*/
    cc_parse_expression(cc, 1);
    cc_type_t false_type = cc_last_expr_type;
    int false_signature_erased =
        cc_last_expr_function_signature_erased;
    int false_is_null_pointer_constant =
        cc_last_expr_is_null_pointer_constant;
    int false_is_integer_constant =
        cc_last_expr_is_integer_constant_expression;
    uint32_t false_integer_constant =
        cc_last_expr_integer_constant_value;
    int false_integer_constant_is_unsigned =
        cc_last_expr_integer_constant_is_unsigned;
    int false_function_signature_count =
        cc_last_expr_function_signature_count;
    cc_last_expr_direct_lvalue_sym = NULL;
    cc_last_expr_indirect_lvalue = 0;

    /* End of ternary expression. */
    patch32(cc, jmp_off + 1, (uint32_t)(cc->code_pos - (jmp_off + 5)));
    if ((true_type == TYPE_INT || true_type == TYPE_UINT ||
         true_type == TYPE_CHAR) &&
        (false_type == TYPE_INT || false_type == TYPE_UINT ||
         false_type == TYPE_CHAR)) {
      cc_last_expr_type = cc_promote(cc, true_type, false_type);
    } else if (cc_is_object_pointer_type(true_type) &&
               false_is_null_pointer_constant) {
      cc_last_expr_type = true_type;
    } else if (true_is_null_pointer_constant &&
               cc_is_object_pointer_type(false_type)) {
      cc_last_expr_type = false_type;
    }
    if (true_type == TYPE_FUNC_PTR && false_type == TYPE_FUNC_PTR) {
      int true_candidate_index;
      int false_candidate_index;

      for (true_candidate_index = 0;
           true_candidate_index < true_function_signature_count;
           true_candidate_index++) {
        for (false_candidate_index = 0;
             false_candidate_index < false_function_signature_count;
             false_candidate_index++) {
          if (!cc_function_pointer_signatures_match(
                  true_function_signatures[true_candidate_index],
                  cc_last_expr_function_signature_candidates
                      [false_candidate_index])) {
            cc_error(
                cc, "conditional function-pointer signatures do not match");
            return;
          }
        }
      }
      cc_last_expr_type = TYPE_FUNC_PTR;
      if (true_function_signature_count == 0 ||
          false_function_signature_count == 0) {
        cc_clear_expr_function_signatures();
      } else {
        for (true_candidate_index = 0;
             true_candidate_index < true_function_signature_count;
             true_candidate_index++) {
          if (!cc_append_expr_function_signature(
                  true_function_signatures[true_candidate_index])) {
            cc_error(cc, "too many conditional function-pointer candidates");
            return;
          }
        }
      }
    } else if (true_type == TYPE_FUNC_PTR &&
               false_is_null_pointer_constant) {
      int true_candidate_index;
      cc_last_expr_type = TYPE_FUNC_PTR;
      cc_clear_expr_function_signatures();
      for (true_candidate_index = 0;
           true_candidate_index < true_function_signature_count;
           true_candidate_index++) {
        if (!cc_append_expr_function_signature(
                true_function_signatures[true_candidate_index])) {
          cc_error(cc, "too many conditional function-pointer candidates");
          return;
        }
      }
    } else if (true_is_null_pointer_constant &&
               false_type == TYPE_FUNC_PTR) {
      cc_last_expr_type = TYPE_FUNC_PTR;
    } else {
      cc_clear_expr_function_signatures();
    }
    if (cc_is_object_pointer_type(true_type) &&
        cc_is_object_pointer_type(false_type)) {
      cc_last_expr_function_signature_erased =
          true_signature_erased && false_signature_erased;
    } else if (cc_is_object_pointer_type(true_type) &&
               false_is_null_pointer_constant) {
      cc_last_expr_function_signature_erased = true_signature_erased;
    } else if (true_is_null_pointer_constant &&
               cc_is_object_pointer_type(false_type)) {
      cc_last_expr_function_signature_erased = false_signature_erased;
    } else {
      cc_last_expr_function_signature_erased = 0;
    }
    cc_last_expr_is_null_pointer_constant = 0;
    cc_last_expr_is_integer_constant_expression = 0;
    cc_last_expr_integer_constant_value = 0;
    cc_last_expr_integer_constant_is_unsigned = 0;
    if (condition_is_integer_constant &&
        true_is_integer_constant && false_is_integer_constant &&
        (cc_last_expr_type == TYPE_INT ||
         cc_last_expr_type == TYPE_UINT ||
         cc_last_expr_type == TYPE_CHAR)) {
      if (condition_integer_constant != 0) {
        cc_publish_integer_constant_expression(
            true_integer_constant, true_integer_constant_is_unsigned);
      } else {
        cc_publish_integer_constant_expression(
            false_integer_constant, false_integer_constant_is_unsigned);
      }
    }
    cc_last_expr_array_object_size = 0;
  }
}

static void cc_parse_expression(cc_state_t *cc, int min_prec) {
  int expression_depth;
  if (cc->error)
    return;
  cc_expression_depth++;
  expression_depth = cc_expression_depth;
  cc_parse_expression_impl(cc, min_prec);
  if (!cc->error && cc_has_incomplete_simd_row() &&
      expression_depth != cc_sizeof_simd_row_depth &&
      expression_depth != cc_grouped_simd_row_depth)
    cc_error(cc, "SIMD array row values are not supported");
  cc_expression_depth--;
}

/* Assignment Parsing */

static int cc_is_assignment_op(cc_token_type_t t) {
  return t == CC_TOK_EQ || t == CC_TOK_PLUSEQ || t == CC_TOK_MINUSEQ ||
         t == CC_TOK_STAREQ || t == CC_TOK_SLASHEQ ||
         t == CC_TOK_PERCENTEQ || t == CC_TOK_ANDEQ || t == CC_TOK_OREQ ||
         t == CC_TOK_XOREQ || t == CC_TOK_SHLEQ || t == CC_TOK_SHREQ;
}

static void cc_emit_compound_from_rhs_old(cc_state_t *cc, cc_token_type_t op,
                                          cc_type_t operation_type) {
  /* Input convention:
   *   eax = RHS value
   *   ebx = current LHS value
   * Output:
   *   eax = combined result
*/
  int is_unsigned = operation_type == TYPE_UINT;

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
    if (is_unsigned) {
      emit8(cc, 0x31);
      emit8(cc, 0xD2); /* xor edx, edx */
      emit8(cc, 0xF7);
      emit8(cc, 0xF1); /* div ecx */
    } else {
      emit8(cc, 0x99); /* cdq */
      emit8(cc, 0xF7);
      emit8(cc, 0xF9); /* idiv ecx */
    }
    break;
  case CC_TOK_PERCENTEQ:
    emit8(cc, 0x89);
    emit8(cc, 0xC1); /* mov ecx, eax */
    emit8(cc, 0x89);
    emit8(cc, 0xD8); /* mov eax, ebx */
    if (is_unsigned) {
      emit8(cc, 0x31);
      emit8(cc, 0xD2); /* xor edx, edx */
      emit8(cc, 0xF7);
      emit8(cc, 0xF1); /* div ecx */
    } else {
      emit8(cc, 0x99); /* cdq */
      emit8(cc, 0xF7);
      emit8(cc, 0xF9); /* idiv ecx */
    }
    emit8(cc, 0x89);
    emit8(cc, 0xD0); /* mov eax, edx */
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
    emit8(cc, is_unsigned ? 0xE8 : 0xF8); /* shr/sar eax, cl */
    break;
  default:
    break;
  }
}

static int cc_coerce_fp_assignment(cc_state_t *cc, cc_type_t target_type) {
  if (cc_last_expr_type == target_type)
    return 1;
  if ((cc_last_expr_type == TYPE_INT || cc_last_expr_type == TYPE_UINT ||
       cc_last_expr_type == TYPE_CHAR) &&
      target_type == TYPE_FLOAT) {
    cc_emit_integer_to_fp(cc, cc_last_expr_type, target_type, 0);
  } else if ((cc_last_expr_type == TYPE_INT ||
              cc_last_expr_type == TYPE_UINT ||
              cc_last_expr_type == TYPE_CHAR) &&
             target_type == TYPE_DOUBLE) {
    cc_emit_integer_to_fp(cc, cc_last_expr_type, target_type, 0);
  } else if (cc_last_expr_type == TYPE_FLOAT && target_type == TYPE_DOUBLE) {
    emit_cvtss2sd(cc, 0, 0);
  } else if (cc_last_expr_type == TYPE_DOUBLE && target_type == TYPE_FLOAT) {
    emit_cvtsd2ss(cc, 0, 0);
  } else {
    cc_error(cc, "FP assignment type mismatch"
                 " (no implicit conversion from this source type)");
    return 0;
  }
  cc_last_xmm = 0;
  cc_last_expr_type = target_type;
  return 1;
}

/* Load one scalar object through the address in EAX. Integer values stay in
 * EAX. Floating values use XMM0, which is the private compiler's scalar lane. */
static int cc_emit_indirect_scalar_load(cc_state_t *cc,
                                        cc_type_t object_type) {
  int struct_index = cc_last_expr_struct_index;

  if (object_type == TYPE_CHAR) {
    emit_deref_byte(cc);
  } else if (object_type == TYPE_FLOAT) {
    emit_movss_xmm_eax(cc, 0);
    cc_last_xmm = 0;
  } else if (object_type == TYPE_DOUBLE) {
    emit_movsd_xmm_eax(cc, 0);
    cc_last_xmm = 0;
  } else if (object_type == TYPE_INT || object_type == TYPE_UINT ||
             cc_is_object_pointer_type(object_type) ||
             object_type == TYPE_FUNC_PTR) {
    emit_deref_dword(cc);
  } else {
    cc_error(cc, "indirect load requires a supported scalar object");
    return 0;
  }
  cc_last_expr_type = object_type;
  cc_last_expr_direct_lvalue_sym = NULL;
  cc_seed_pointer_subscript_metadata(cc, object_type, struct_index);
  return 1;
}

/* Store one scalar through the address in EAX. Integer and pointer payloads
 * arrive in EBX because EAX owns the address. Floating payloads stay in XMM0. */
static int cc_emit_indirect_scalar_store(cc_state_t *cc,
                                         cc_type_t object_type) {
  if (object_type == TYPE_CHAR) {
    emit_store_byte_ptr(cc);
  } else if (object_type == TYPE_FLOAT) {
    emit_movss_eax_xmm(cc, 0);
  } else if (object_type == TYPE_DOUBLE) {
    emit_movsd_eax_xmm(cc, 0);
  } else if (object_type == TYPE_INT || object_type == TYPE_UINT ||
             cc_is_object_pointer_type(object_type) ||
             object_type == TYPE_FUNC_PTR) {
    emit_store_dword_ptr(cc);
  } else {
    cc_error(cc, "indirect store requires a supported scalar object");
    return 0;
  }
  cc_last_expr_type = object_type;
  if (object_type == TYPE_FLOAT || object_type == TYPE_DOUBLE)
    cc_last_xmm = 0;
  return 1;
}

/* Finish an assignment whose destination address is already saved on the
 * stack. The address is evaluated once, including for compound assignment. */
static int cc_finish_indirect_assignment(cc_state_t *cc,
                                         cc_type_t object_type,
                                         cc_token_type_t op,
                                         const char *fp_operator_error) {
  int is_fp = object_type == TYPE_FLOAT || object_type == TYPE_DOUBLE;

  if (is_fp) {
    int is_compound = op == CC_TOK_PLUSEQ || op == CC_TOK_MINUSEQ ||
                      op == CC_TOK_STAREQ || op == CC_TOK_SLASHEQ;
    int is_double = object_type == TYPE_DOUBLE;
    uint8_t op_byte = 0;

    if (op != CC_TOK_EQ && !is_compound) {
      cc_error(cc, op == CC_TOK_PERCENTEQ
                       ? "remainder compound assignment requires an integer lvalue"
                       : fp_operator_error);
      return 0;
    }

    cc_parse_expression(cc, 1);
    if (!cc_coerce_fp_assignment(cc, object_type))
      return 0;

    if (is_compound) {
      emit8(cc, 0x83);
      emit8(cc, 0xEC);
      emit8(cc, 0x08); /* sub esp, 8 */
      if (is_double)
        emit_movsd_esp_xmm(cc, 0);
      else
        emit_movss_esp_xmm(cc, 0);

      emit8(cc, 0x8B);
      emit8(cc, 0x44);
      emit8(cc, 0x24);
      emit8(cc, 0x08); /* mov eax, [esp + 8] */
      if (!cc_emit_indirect_scalar_load(cc, object_type))
        return 0;
      if (is_double)
        emit_movsd_xmm_esp(cc, 1);
      else
        emit_movss_xmm_esp(cc, 1);
      emit8(cc, 0x83);
      emit8(cc, 0xC4);
      emit8(cc, 0x08); /* add esp, 8 */

      switch (op) {
      case CC_TOK_PLUSEQ:  op_byte = 0x58; break;
      case CC_TOK_MINUSEQ: op_byte = 0x5C; break;
      case CC_TOK_STAREQ:  op_byte = 0x59; break;
      case CC_TOK_SLASHEQ: op_byte = 0x5E; break;
      default: break;
      }
      emit_sse_scalar_op(cc, is_double, op_byte, 0, 1);
    }

    emit_pop_eax(cc);
    return cc_emit_indirect_scalar_store(cc, object_type);
  }

  if (op != CC_TOK_EQ) {
    emit8(cc, 0x8B);
    emit8(cc, 0x04);
    emit8(cc, 0x24); /* mov eax, [esp] */
    if (!cc_emit_indirect_scalar_load(cc, object_type))
      return 0;
    emit_push_eax(cc);
  }

  cc_parse_expression(cc, 1);
  if (!cc_coerce_unsigned_assignment(cc, object_type,
                                     cc_last_expr_type, op))
    return 0;
  if (op != CC_TOK_EQ) {
    cc_type_t operation_type = cc_integer_operation_type(
        cc, op, object_type, cc_last_expr_type);
    emit_pop_ebx(cc);
    cc_emit_compound_from_rhs_old(cc, op, operation_type);
  }

  emit8(cc, 0x89);
  emit8(cc, 0xC3); /* mov ebx, eax */
  emit_pop_eax(cc);
  return cc_emit_indirect_scalar_store(cc, object_type);
}

/* Parse assignment: var = expr, var += expr, *ptr = expr, arr[i] = expr */
static void cc_parse_assignment(cc_state_t *cc, const char *name) {
  cc_symbol_t *sym = cc_sym_find(cc, name);
  cc_symbol_t *assignment_targets[CC_MAX_PARAMS];
  int assignment_target_count = 0;
  if (!sym) {
    cc_error(cc, "undefined variable in assignment");
    return;
  }
  if (sym->is_const_qualified && cc_is_simd_value_type(sym->type)) {
    cc_error_simd_assignment_target(cc);
    return;
  }

  cc_token_t op = cc_next(cc); /* consume =, +=, etc. */

  cc_parse_expression(cc, 1);
  if (!cc_coerce_unsigned_assignment(cc, sym->type,
                                     cc_last_expr_type, op.type))
    return;

  if (sym->type == TYPE_FUNC_PTR) {
    if (op.type != CC_TOK_EQ) {
      cc_error(cc, "function-pointer assignment requires plain =");
      return;
    }
    if (!cc_validate_function_pointer_assignment_value(cc, sym))
      return;
    if (!cc_last_expr_is_null_pointer_constant &&
        !cc_last_expr_function_signature_erased &&
        cc_last_expr_type == TYPE_FUNC_PTR) {
      assignment_target_count = cc_last_expr_function_signature_count;
      memcpy(assignment_targets,
             cc_last_expr_function_signature_candidates,
             sizeof(*assignment_targets) *
                 (size_t)assignment_target_count);
    }
  }

  /* SIMD assignment path - MOVUPS xmm0 to the 16-byte destination.
   * Plain '=' and the four arithmetic compound ops (+=, -=, *=, /=) are
   * supported via the packed-vector instructions ADDPS/ADDPD etc.
   * RHS must be the same SIMD type - no implicit conversions across
   * float4/double2.  Locals, params, and globals are all valid targets;
   * struct fields are not (would need l-value address computation).
   *    float4 s;
   *    s = _mm_add_ps(a, b);     // plain assign
   *    s += other_v4;            // compound, packed-add
*/
  if (sym->type == TYPE_FLOAT4 || sym->type == TYPE_DOUBLE2) {
    int simd_compound = (op.type == CC_TOK_PLUSEQ ||
                         op.type == CC_TOK_MINUSEQ ||
                         op.type == CC_TOK_STAREQ ||
                         op.type == CC_TOK_SLASHEQ);
    if (op.type != CC_TOK_EQ && !simd_compound) {
      cc_error(cc, "SIMD compound op must be +=, -=, *=, or /=");
      return;
    }
    if (cc_last_expr_type != sym->type) {
      cc_error(cc, "SIMD assignment type mismatch");
      return;
    }

    int is_pd = (sym->type == TYPE_DOUBLE2);

    /* Compound: combine current value with RHS using packed op. */
    if (simd_compound) {
      /* Spill RHS (XMM0) into a 16-byte stack slot. */
      emit8(cc, 0x83); emit8(cc, 0xEC); emit8(cc, 0x10); /* sub esp, 16 */
      /* MOVUPS [esp], xmm0 : 0F 11 04 24 */
      emit8(cc, 0x0F); emit8(cc, 0x11); emit8(cc, 0x04); emit8(cc, 0x24);

      /* Load current LHS into XMM0. */
      if (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM) {
        emit_movups_xmm_local(cc, 0, sym->offset);
      } else if (sym->kind == SYM_GLOBAL) {
        /* MOVUPS xmm0, [disp32] : 0F 10 05 dd dd dd dd */
        emit8(cc, 0x0F); emit8(cc, 0x10); emit8(cc, 0x05);
        emit32(cc, sym->address);
      } else {
        cc_error(cc, "SIMD compound on unsupported symbol kind");
        return;
      }

      /* Reload spilled RHS into XMM1 and free slot. */
      /* MOVUPS xmm1, [esp] : 0F 10 0C 24 */
      emit8(cc, 0x0F); emit8(cc, 0x10); emit8(cc, 0x0C); emit8(cc, 0x24);
      emit8(cc, 0x83); emit8(cc, 0xC4); emit8(cc, 0x10); /* add esp, 16 */

      /* Packed op: XMM0 = XMM0 OP XMM1.
       * ADDPS/SUBPS/MULPS/DIVPS: 0F (58|5C|59|5E) /r
       * ADDPD/SUBPD/MULPD/DIVPD: 66 0F same opcode /r*/
      uint8_t op_byte = 0;
      switch (op.type) {
      case CC_TOK_PLUSEQ:  op_byte = 0x58; break;
      case CC_TOK_MINUSEQ: op_byte = 0x5C; break;
      case CC_TOK_STAREQ:  op_byte = 0x59; break;
      case CC_TOK_SLASHEQ: op_byte = 0x5E; break;
      default: break;
      }
      if (is_pd) emit8(cc, 0x66);
      emit8(cc, 0x0F);
      emit8(cc, op_byte);
      emit8(cc, 0xC1); /* mod=11, reg=0(xmm0), r/m=1(xmm1) */
      cc_last_xmm = 0;
    }

    if (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM) {
      emit_movups_local_xmm(cc, 0, sym->offset);
    } else if (sym->kind == SYM_GLOBAL) {
      /* MOVUPS [disp32], xmm0 : 0F 11 05 dd dd dd dd */
      emit8(cc, 0x0F); emit8(cc, 0x11); emit8(cc, 0x05);
      emit32(cc, sym->address);
    } else {
      cc_error(cc, "SIMD assignment to unsupported symbol kind");
    }
    return;
  }

  /* FP assignment path - store XMM0 to the destination.  Plain '=' and
   * the four arithmetic compound ops (+=, -=, *=, /=) are supported.
   * Bitwise/shift compound ops are rejected on FP types.  Implicit
   * promotion of the RHS when it differs from the target's FP type.*/
  if (sym->type == TYPE_FLOAT || sym->type == TYPE_DOUBLE) {
    int is_compound_fp = (op.type == CC_TOK_PLUSEQ ||
                          op.type == CC_TOK_MINUSEQ ||
                          op.type == CC_TOK_STAREQ ||
                          op.type == CC_TOK_SLASHEQ);
    if (op.type != CC_TOK_EQ && !is_compound_fp) {
      cc_error(cc, op.type == CC_TOK_PERCENTEQ
                       ? "remainder compound assignment requires an integer lvalue"
                       : "bitwise/shift compound assignment not valid on FP types");
      return;
    }
    /* Coerce RHS into the destination's FP type when possible. */
    if (!cc_coerce_fp_assignment(cc, sym->type))
      return;

    int is_double = (sym->type == TYPE_DOUBLE);

    /* Compound FP: combine current value of sym with the RHS already in
     * XMM0.  Sequence: spill RHS, load LHS into XMM0, reload RHS into
     * XMM1, op XMM0,XMM1, store.  All four ops use XMM0 as accumulator
     * (XMM0 := XMM0 op XMM1) which matches the natural read direction
     * for both commutative (+,*) and non-commutative (-,/) cases.*/
    if (is_compound_fp) {
      /* Spill RHS (currently in XMM0). 8-byte slot keeps ESP aligned. */
      emit8(cc, 0x83);
      emit8(cc, 0xEC);
      emit8(cc, 0x08); /* sub esp, 8 */
      if (is_double) emit_movsd_esp_xmm(cc, 0);
      else           emit_movss_esp_xmm(cc, 0);

      /* Load current value of sym into XMM0. */
      if (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM) {
        if (is_double) emit_movsd_xmm_local(cc, 0, sym->offset);
        else           emit_movss_xmm_local(cc, 0, sym->offset);
      } else if (sym->kind == SYM_GLOBAL) {
        if (is_double) emit_movsd_xmm_disp32(cc, 0, sym->address);
        else           emit_movss_xmm_disp32(cc, 0, sym->address);
      } else {
        cc_error(cc, "FP compound on unsupported symbol kind");
        return;
      }

      /* Reload spilled RHS into XMM1, drop spill slot. */
      if (is_double) emit_movsd_xmm_esp(cc, 1);
      else           emit_movss_xmm_esp(cc, 1);
      emit8(cc, 0x83);
      emit8(cc, 0xC4);
      emit8(cc, 0x08); /* add esp, 8 */

      uint8_t op_byte = 0;
      switch (op.type) {
      case CC_TOK_PLUSEQ:  op_byte = 0x58; break;
      case CC_TOK_MINUSEQ: op_byte = 0x5C; break;
      case CC_TOK_STAREQ:  op_byte = 0x59; break;
      case CC_TOK_SLASHEQ: op_byte = 0x5E; break;
      default: break; /* unreachable */
      }
      emit_sse_scalar_op(cc, is_double, op_byte, 0, 1);
      cc_last_xmm = 0;
    }

    if (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM) {
      if (is_double) emit_movsd_local_xmm(cc, 0, sym->offset);
      else           emit_movss_local_xmm(cc, 0, sym->offset);
    } else if (sym->kind == SYM_GLOBAL) {
      /* MOVSS/MOVSD [disp32], xmm0: prefix + 0F 11 /0 + mod=00,r/m=101 */
      emit8(cc, is_double ? 0xF2 : 0xF3);
      emit8(cc, 0x0F);
      emit8(cc, 0x11);
      emit8(cc, 0x05); /* mod=00, reg=0, r/m=101 (disp32) */
      emit32(cc, sym->address);
    }
    return;
  }

  /* Handle compound assignment */
  if (op.type != CC_TOK_EQ) {
    cc_type_t operation_type = cc_integer_operation_type(
        cc, op.type, sym->type, cc_last_expr_type);
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
    cc_emit_compound_from_rhs_old(cc, op.type, operation_type);
  }

  /* Store result */
  if (sym->kind == SYM_LOCAL || sym->kind == SYM_PARAM) {
    emit_store_local(cc, sym->offset);
  } else if (sym->kind == SYM_GLOBAL) {
    emit8(cc, 0xA3);
    emit32(cc, sym->address);
  }
  if (assignment_target_count > 0)
    (void)cc_apply_function_pointer_initializer_candidates(
        cc, sym, assignment_targets, assignment_target_count);
}

/* Parse pointer dereference assignment: *expr = val */
static void cc_parse_deref_assignment(cc_state_t *cc) {
  /* Parse the pointer expression once and keep its target address. */
  cc_parse_primary(cc);
  if (cc_reject_incomplete_simd_row(cc))
    return;
  cc_type_t ptr_type = cc_last_expr_type;
  cc_type_t object_type = cc_pointed_object_type(ptr_type);
  cc_token_t op;

  if (object_type == TYPE_VOID) {
    cc_error(cc, "dereference assignment requires a supported pointer");
    return;
  }

  op = cc_next(cc);
  if (!cc_is_assignment_op(op.type)) {
    cc_error(cc, "expected assignment operator after dereference");
    return;
  }

  emit_push_eax(cc);
  (void)cc_finish_indirect_assignment(
      cc, object_type, op.type,
      "bitwise or shift compound assignment requires an integer lvalue");
}

static void cc_emit_scale_eax_by_stride(cc_state_t *cc, int32_t stride) {
  if (stride <= 1) {
    return;
  }
  if (stride == 2) {
    emit8(cc, 0xC1);
    emit8(cc, 0xE0);
    emit8(cc, 0x01); /* shl eax, 1 */
    return;
  }
  if (stride == 4) {
    emit8(cc, 0xC1);
    emit8(cc, 0xE0);
    emit8(cc, 0x02); /* shl eax, 2 */
    return;
  }
  emit8(cc, 0x69);
  emit8(cc, 0xC0);
  emit32(cc, (uint32_t)stride); /* imul eax, eax, stride */
}

/* EAX starts as the address of a record. Consume a member lvalue chain and
 * leave EAX at the selected scalar or aggregate slot. Array fields require
 * one index, after which traversal may continue through a record element. */
static int cc_parse_member_lvalue_chain(cc_state_t *cc, int struct_index,
                                        cc_type_t *leaf_type) {
  cc_type_t current_type = TYPE_STRUCT;
  int current_struct_index = struct_index;

  while (cc_peek(cc).type == CC_TOK_DOT ||
         cc_peek(cc).type == CC_TOK_ARROW) {
    cc_field_t *field;
    cc_token_t field_token;

    cc_next(cc); /* consume . or -> */
    field_token = cc_next(cc);
    if (field_token.type != CC_TOK_IDENT) {
      cc_error(cc, "expected field");
      return 0;
    }
    field = cc_find_field(cc, current_struct_index, field_token.text);
    if (field == NULL) {
      cc_error(cc, "unknown field");
      return 0;
    }
    if (field->offset > 0) {
      emit8(cc, 0x05);
      emit32(cc, (uint32_t)field->offset);
    }

    current_type = field->type;
    current_struct_index = field->struct_index;
    if (field->array_count > 0) {
      int32_t stride =
          cc_type_size(cc, current_type, current_struct_index);
      if (!cc_match(cc, CC_TOK_LBRACK)) {
        cc_error(cc, "array field assignment requires an index");
        return 0;
      }
      emit_push_eax(cc);
      cc_parse_expression(cc, 1);
      if (cc->error)
        return 0;
      cc_emit_scale_eax_by_stride(cc, stride);
      emit_pop_ebx(cc);
      emit8(cc, 0x01);
      emit8(cc, 0xD8); /* add eax, ebx */
      cc_expect(cc, CC_TOK_RBRACK);
      if (cc->error)
        return 0;
      if (current_type == TYPE_STRUCT_PTR &&
          (cc_peek(cc).type == CC_TOK_DOT ||
           cc_peek(cc).type == CC_TOK_ARROW)) {
        emit_deref_dword(cc);
        continue;
      }
      if (current_type == TYPE_STRUCT &&
          (cc_peek(cc).type == CC_TOK_DOT ||
           cc_peek(cc).type == CC_TOK_ARROW))
        continue;
      break;
    }

    if (current_type == TYPE_STRUCT_PTR &&
        (cc_peek(cc).type == CC_TOK_DOT ||
         cc_peek(cc).type == CC_TOK_ARROW)) {
      emit_deref_dword(cc);
      continue;
    }
    if (current_type == TYPE_STRUCT &&
        (cc_peek(cc).type == CC_TOK_DOT ||
         cc_peek(cc).type == CC_TOK_ARROW))
      continue;
    break;
  }

  if (cc_is_simd_value_type(current_type) &&
      (cc_peek(cc).type == CC_TOK_PLUSPLUS ||
       cc_peek(cc).type == CC_TOK_MINUSMINUS)) {
    cc_error(cc, "SIMD record-field increment or decrement is not supported");
    return 0;
  }

  *leaf_type = current_type;
  return 1;
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
  cc_type_t elem_type = sym->is_array
                            ? sym->array_elem_type
                            : cc_pointed_object_type(sym->type);
  if (elem_type == TYPE_VOID)
    elem_type = TYPE_INT;
  int is_fp = elem_type == TYPE_FLOAT || elem_type == TYPE_DOUBLE;
  int is_simd = elem_type == TYPE_FLOAT4 || elem_type == TYPE_DOUBLE2;
  if (sym->is_array && sym->array_elem_size > 0)
    elem_size = sym->array_elem_size;
  else if (sym->type == TYPE_STRUCT_PTR && sym->struct_index >= 0 &&
           sym->struct_index < cc->struct_count)
    elem_size = cc->structs[sym->struct_index].total_size;
  else if (sym->type == TYPE_CHAR_PTR || sym->type == TYPE_CHAR)
    elem_size = 1;
  else if (sym->type == TYPE_FLOAT_PTR)
    elem_size = 4;
  else if (sym->type == TYPE_DOUBLE_PTR)
    elem_size = 8;
  else
    elem_size = 4;
  int array_rank_remaining =
      sym->is_array && sym->array_rank > 0 ? sym->array_rank - 1 : 0;

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

  /* Handle struct array element member chains, including array fields whose
   * elements are records: arr[i].field[j].member = value. */
  if ((elem_type == TYPE_STRUCT || sym->type == TYPE_STRUCT_PTR) &&
      (cc_peek(cc).type == CC_TOK_DOT ||
       cc_peek(cc).type == CC_TOK_ARROW)) {
    int si = sym->struct_index;
    if (!cc_parse_member_lvalue_chain(cc, si, &elem_type))
      return;
    is_char = elem_type == TYPE_CHAR;
    is_fp = elem_type == TYPE_FLOAT || elem_type == TYPE_DOUBLE;
    is_simd = elem_type == TYPE_FLOAT4 || elem_type == TYPE_DOUBLE2;
  }
  /* Handle 2D/3D char array second subscript: arr[i][j] = val */
  else if (is_char && array_rank_remaining > 0 &&
           cc_peek(cc).type == CC_TOK_LBRACK) {
    cc_next(cc); /* consume '[' */
    emit_push_eax(cc);
    cc_parse_expression(cc, 1);
    /* For 3D char arrays, the middle stride (dim2) scales the second
     * subscript. For pure 2D, inner is char (1 byte) and no scale.*/
    if (sym->array_dim2 > 0) {
      int j_stride = sym->array_dim2;
      if (j_stride == 1) {
        /* no scaling */
      } else if (j_stride == 2) {
        emit8(cc, 0xC1); emit8(cc, 0xE0); emit8(cc, 0x01);
      } else if (j_stride == 4) {
        emit8(cc, 0xC1); emit8(cc, 0xE0); emit8(cc, 0x02);
      } else {
        emit8(cc, 0x69); emit8(cc, 0xC0); emit32(cc, (uint32_t)j_stride);
      }
    }
    emit_pop_ebx(cc);
    emit8(cc, 0x01);
    emit8(cc, 0xD8); /* add eax, ebx */
    cc_expect(cc, CC_TOK_RBRACK);
    array_rank_remaining--;
    is_char = 1;
    /* Optional third subscript for 3D char arrays. */
    if (array_rank_remaining > 0 &&
        cc_peek(cc).type == CC_TOK_LBRACK) {
      cc_next(cc);
      emit_push_eax(cc);
      cc_parse_expression(cc, 1);
      /* Innermost is char (1 byte) - no scaling. */
      emit_pop_ebx(cc);
      emit8(cc, 0x01);
      emit8(cc, 0xD8);
      cc_expect(cc, CC_TOK_RBRACK);
      array_rank_remaining--;
    }
  }
  /* Descend through remaining floating or SIMD array rows. */
  else if ((is_fp || is_simd) &&
           array_rank_remaining > 0 &&
           cc_peek(cc).type == CC_TOK_LBRACK) {
    int scalar_size = cc_type_size(cc, elem_type, -1);
    int j_stride = sym->array_dim2 > 0 ? sym->array_dim2 : scalar_size;
    cc_next(cc);
    emit_push_eax(cc);
    cc_parse_expression(cc, 1);
    if (j_stride == 2) {
      emit8(cc, 0xC1); emit8(cc, 0xE0); emit8(cc, 0x01);
    } else if (j_stride == 4) {
      emit8(cc, 0xC1); emit8(cc, 0xE0); emit8(cc, 0x02);
    } else if (j_stride > 1) {
      emit8(cc, 0x69); emit8(cc, 0xC0); emit32(cc, (uint32_t)j_stride);
    }
    emit_pop_ebx(cc);
    emit8(cc, 0x01);
    emit8(cc, 0xD8);
    cc_expect(cc, CC_TOK_RBRACK);
    array_rank_remaining--;

    if (array_rank_remaining > 0 &&
        cc_peek(cc).type == CC_TOK_LBRACK) {
      cc_next(cc);
      emit_push_eax(cc);
      cc_parse_expression(cc, 1);
      if (scalar_size == 2) {
        emit8(cc, 0xC1); emit8(cc, 0xE0); emit8(cc, 0x01);
      } else if (scalar_size == 4) {
        emit8(cc, 0xC1); emit8(cc, 0xE0); emit8(cc, 0x02);
      } else if (scalar_size > 1) {
        emit8(cc, 0x69); emit8(cc, 0xC0);
        emit32(cc, (uint32_t)scalar_size);
      }
      emit_pop_ebx(cc);
      emit8(cc, 0x01);
      emit8(cc, 0xD8);
      cc_expect(cc, CC_TOK_RBRACK);
      array_rank_remaining--;
    }
  }
  /* Handle 2D/3D int array second subscript */
  else if (!is_char && !is_fp && !is_simd &&
           array_rank_remaining > 0 &&
           cc_peek(cc).type == CC_TOK_LBRACK) {
    cc_next(cc); /* consume '[' */
    emit_push_eax(cc);
    cc_parse_expression(cc, 1);
    /* For 3D int arrays, scale by dim2 (middle stride). Pure 2D scales
     * by 4 (a row of 32-bit ints).*/
    int j_stride = (sym->array_dim2 > 0) ? sym->array_dim2 : 4;
    if (j_stride == 4) {
      emit8(cc, 0xC1); emit8(cc, 0xE0); emit8(cc, 0x02); /* shl eax, 2 */
    } else if (j_stride == 1) {
      /* no scaling */
    } else if (j_stride == 2) {
      emit8(cc, 0xC1); emit8(cc, 0xE0); emit8(cc, 0x01);
    } else {
      emit8(cc, 0x69); emit8(cc, 0xC0); emit32(cc, (uint32_t)j_stride);
    }
    emit_pop_ebx(cc);
    emit8(cc, 0x01);
    emit8(cc, 0xD8); /* add eax, ebx */
    cc_expect(cc, CC_TOK_RBRACK);
    array_rank_remaining--;
    is_char = 0;
    /* Optional third subscript for 3D int arrays. Innermost stride = 4. */
    if (array_rank_remaining > 0 &&
        cc_peek(cc).type == CC_TOK_LBRACK) {
      cc_next(cc);
      emit_push_eax(cc);
      cc_parse_expression(cc, 1);
      emit8(cc, 0xC1); emit8(cc, 0xE0); emit8(cc, 0x02); /* shl eax, 2 */
      emit_pop_ebx(cc);
      emit8(cc, 0x01);
      emit8(cc, 0xD8);
      cc_expect(cc, CC_TOK_RBRACK);
      array_rank_remaining--;
    }
  }

  if (array_rank_remaining > 0) {
    if (is_simd && (cc_peek(cc).type == CC_TOK_PLUSPLUS ||
                    cc_peek(cc).type == CC_TOK_MINUSMINUS)) {
      cc_error(cc, "SIMD array row values are not supported");
    } else {
      cc_error(cc, is_simd
                       ? "SIMD array assignment requires every subscript"
                       : "array assignment requires every subscript");
    }
    return;
  }

  if (sym->is_const_qualified && is_simd) {
    cc_token_type_t mutation = cc_peek(cc).type;
    if (mutation == CC_TOK_PLUSPLUS || mutation == CC_TOK_MINUSMINUS)
      cc_error_simd_update_target(cc);
    else if (cc_is_assignment_op(mutation))
      cc_error_simd_assignment_target(cc);
    else
      cc_error(cc, "expected assignment operator");
    return;
  }

  if (cc_peek(cc).type == CC_TOK_PLUSPLUS ||
      cc_peek(cc).type == CC_TOK_MINUSMINUS) {
    int decrement = cc_next(cc).type == CC_TOK_MINUSMINUS;
    if (!is_fp && !is_simd) {
      cc_error(cc, "indirect increment or decrement is not supported");
      return;
    }
    if (is_simd) {
      emit_movups_xmm_eax(cc, 0);
      cc_last_expr_type = elem_type;
      cc_last_xmm = 0;
    } else if (!cc_emit_indirect_scalar_load(cc, elem_type)) {
      return;
    }
    cc_last_expr_indirect_lvalue = 1;
    cc_last_expr_const_lvalue = sym->is_const_qualified;
    if (!cc_emit_indirect_fp_update(cc, elem_type, decrement, 0))
      return;
    return;
  }

  emit_push_eax(cc); /* save computed address */

  /* Expect = or compound assignment */
  cc_token_t assign_op = cc_next(cc);
  if (!cc_is_assignment_op(assign_op.type)) {
    cc_error(cc, "expected assignment operator");
    return;
  }

  if (is_simd) {
    int is_compound_simd =
        assign_op.type == CC_TOK_PLUSEQ ||
        assign_op.type == CC_TOK_MINUSEQ ||
        assign_op.type == CC_TOK_STAREQ ||
        assign_op.type == CC_TOK_SLASHEQ;
    if (assign_op.type != CC_TOK_EQ && !is_compound_simd) {
      cc_error(cc,
               "SIMD array compound assignment supports only +=, -=, *=, and /=");
      return;
    }

    cc_parse_expression(cc, 1);
    if (cc_last_expr_type != elem_type) {
      cc_error(cc, elem_type == TYPE_FLOAT4
                       ? "float4 array assignment requires a float4 value"
                       : "double2 array assignment requires a double2 value");
      return;
    }

    if (is_compound_simd) {
      uint8_t op_byte = 0;
      cc_intr_spill_xmm0(cc);
      emit8(cc, 0x8B);
      emit8(cc, 0x44);
      emit8(cc, 0x24);
      emit8(cc, 0x10); /* mov eax, [esp + 16] */
      emit_movups_xmm_eax(cc, 0);
      cc_intr_restore_xmm1(cc);

      switch (assign_op.type) {
      case CC_TOK_PLUSEQ:  op_byte = 0x58; break;
      case CC_TOK_MINUSEQ: op_byte = 0x5C; break;
      case CC_TOK_STAREQ:  op_byte = 0x59; break;
      case CC_TOK_SLASHEQ: op_byte = 0x5E; break;
      default: break;
      }
      cc_intr_emit_op_rr(cc,
                        elem_type == TYPE_DOUBLE2 ? 0x66 : 0x00,
                        op_byte, 0, 1, -1);
    }

    emit_pop_eax(cc);
    emit_movups_eax_xmm(cc, 0);
    cc_last_expr_type = elem_type;
    cc_last_xmm = 0;
    return;
  }

  if (is_fp) {
    (void)cc_finish_indirect_assignment(
        cc, elem_type, assign_op.type,
        "bitwise/shift compound assignment not valid on FP arrays");
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

  if (!cc_coerce_unsigned_assignment(cc, elem_type,
                                     cc_last_expr_type, assign_op.type))
    return;

  if (assign_op.type != CC_TOK_EQ) {
    cc_type_t operation_type = cc_integer_operation_type(
        cc, assign_op.type, elem_type, cc_last_expr_type);
    /* Pop old value into ebx, apply operation */
    emit_pop_ebx(cc);
    cc_emit_compound_from_rhs_old(cc, assign_op.type, operation_type);
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
  uint32_t address; /* absolute address when is_local=0 */
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
 * opcode-extension digit for x87).*/
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
 * prefix=0x00 means no legacy prefix (PS variant).*/
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

/* Try to encode a FPU/SSE opcode. Returns 1 if matched (and either
 * encoded or errored), 0 if the mnemonic wasn't one we handle here so the
 * caller can fall through to the integer dispatcher.*/
static int cc_parse_asm_fpu_opcode(cc_state_t *cc, const char *mn) {
  /* No-operand x87 / FPU state-control */
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

  /* x87 memory-operand opcodes (m32fp only).
   * Matches standalone CupidASM behavior: no size-prefix keyword
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

  /* MXCSR save/restore (mem32).
   * STMXCSR m32 = 0F AE /3   |   LDMXCSR m32 = 0F AE /2
   * Required by the #XF provocation drill so user-space CupidC
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

  /* SSE scalar "xmm, xmm" opcodes */
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

  /* MOVSS / MOVSD / MOVUPS / MOVAPS: bidirectional mem<->xmm.
   * Shape: peek first operand; if it's an XMM reg the direction is load
   * (xmm <- [mem]) with opcode 0x10; if it's '[' or a size-prefix keyword
   * the direction is store ([mem] <- xmm) with opcode 0x11.*/
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
      /* FPU/SSE opcode handled (x87 + SSE scalar + SSE packed). */
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
  int type_array_count = cc_last_type_array_count;
  int type_is_const = cc_last_type_is_const_qualified;
  cc_skip_attributes(cc);
  cc_token_t name_tok = cc_next(cc);
  if (name_tok.type != CC_TOK_IDENT) {
    cc_error(cc, "expected variable name");
    return;
  }

  if (type_array_count > 0 || cc_peek(cc).type == CC_TOK_LBRACK) {
    int uses_typedef_array = type_array_count > 0;
    if (uses_typedef_array && cc_peek(cc).type == CC_TOK_LBRACK) {
      cc_error(cc, "array declarator after typedef array is not supported");
      return;
    }
    if (!uses_typedef_array)
      cc_next(cc); /* '[' */
    int32_t arr_elems;
    if (uses_typedef_array) {
      arr_elems = type_array_count;
    } else {
      if (!cc_parse_const_int_expr(cc, &arr_elems)) {
        cc_error(cc, "expected array size");
        return;
      }
      if (arr_elems <= 0) {
        cc_error(cc, "array size must be positive");
        return;
      }
      cc_expect(cc, CC_TOK_RBRACK);
    }
    int32_t inner_dim = 0;
    int32_t inner_dim2 = 0;
    int has_inner_dim = 0;
    int has_inner_dim2 = 0;
    if (cc_peek(cc).type == CC_TOK_LBRACK) {
      has_inner_dim = 1;
      cc_next(cc); /* '[' */
      if (!cc_parse_const_int_expr(cc, &inner_dim)) {
        cc_error(cc, "expected array size");
        return;
      }
      if (inner_dim <= 0) {
        cc_error(cc, "array size must be positive");
        return;
      }
      cc_expect(cc, CC_TOK_RBRACK);
      if (cc_peek(cc).type == CC_TOK_LBRACK) {
        has_inner_dim2 = 1;
        cc_next(cc);
        if (!cc_parse_const_int_expr(cc, &inner_dim2)) {
          cc_error(cc, "expected array size");
          return;
        }
        if (inner_dim2 <= 0) {
          cc_error(cc, "array size must be positive");
          return;
        }
        cc_expect(cc, CC_TOK_RBRACK);
      }
    }

    int32_t total_bytes;
    int aes;
    int dim2 = 0;
    cc_type_t arr_type;
    if (type == TYPE_STRUCT && (has_inner_dim || has_inner_dim2)) {
      cc_error(cc, "struct arrays support one dimension");
      return;
    }
    if (type == TYPE_STRUCT && type_struct_index >= 0 &&
        type_struct_index < cc->struct_count) {
      if (!cc_struct_is_complete(cc, type_struct_index)) {
        cc_error(cc, "array of incomplete struct type");
        return;
      }
      int32_t ssize = cc->structs[type_struct_index].total_size;
      if (!cc_checked_array_bytes(cc, arr_elems, ssize, &total_bytes))
        return;
      aes = ssize;
      arr_type = TYPE_STRUCT_PTR;
    } else if (has_inner_dim2) {
      int base_elem = cc_type_size(cc, type, type_struct_index);
      int32_t middle_stride;
      int32_t row_size;
      if (!cc_checked_array_bytes(cc, inner_dim2, base_elem,
                                  &middle_stride) ||
          !cc_checked_array_bytes(cc, inner_dim, middle_stride, &row_size) ||
          !cc_checked_array_bytes(cc, arr_elems, row_size, &total_bytes))
        return;
      aes = row_size;
      dim2 = middle_stride;
      arr_type = cc_object_pointer_type(type);
    } else if (has_inner_dim) {
      int base_elem = cc_type_size(cc, type, type_struct_index);
      int32_t row_size;
      if (!cc_checked_array_bytes(cc, inner_dim, base_elem, &row_size) ||
          !cc_checked_array_bytes(cc, arr_elems, row_size, &total_bytes))
        return;
      aes = row_size;
      arr_type = cc_object_pointer_type(type);
    } else {
      int elem_size = cc_type_size(cc, type, type_struct_index);
      if (elem_size <= 0) {
        cc_error(cc, "invalid array element type");
        return;
      }
      if (!cc_checked_array_bytes(cc, arr_elems, elem_size, &total_bytes))
        return;
      aes = elem_size;
      arr_type = cc_object_pointer_type(type);
    }
    int32_t array_object_size = total_bytes;
    total_bytes = (total_bytes + 3) & ~3;
    cc_symbol_t *sym = cc_sym_add(cc, name_tok.text, SYM_GLOBAL, arr_type);
    if (sym) {
      if (!cc_data_reserve(cc, (uint32_t)total_bytes))
        return;
      sym->address = cc->data_base + cc->data_pos;
      sym->is_array = 1;
      sym->is_const_qualified = type_is_const;
      sym->struct_index = type_struct_index;
      sym->array_elem_size = aes;
      sym->array_object_size = array_object_size;
      sym->array_rank = 1 + has_inner_dim + has_inner_dim2;
      sym->array_dim2 = dim2;
      sym->array_elem_type = type;
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
      sym->is_const_qualified = type_is_const;
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

  int32_t scalar_size = cc_type_size(cc, type, type_struct_index);
  if (scalar_size <= 0 ||
      (scalar_size > 8 && !cc_is_simd_value_type(type))) {
    cc_error(cc, "static scalar type is not supported");
    return;
  }
  scalar_size = cc_align_up(scalar_size, 4);
  cc_symbol_t *sym = cc_sym_add(cc, name_tok.text, SYM_GLOBAL, type);
  if (sym) {
    if (!cc_data_reserve(cc, (uint32_t)scalar_size))
      return;
    sym->address = cc->data_base + cc->data_pos;
    sym->is_const_qualified = type_is_const;
    sym->struct_index = type_struct_index;
    memset(cc->data + cc->data_pos, 0, (size_t)scalar_size);
    cc->data_pos += (uint32_t)scalar_size;
  }

  if (cc_match(cc, CC_TOK_EQ)) {
    if (cc_peek(cc).type == CC_TOK_LBRACE) {
      if (!cc_skip_brace_initializer(cc))
        return;
    } else {
      cc_parse_expression(cc, 1);
      if (!cc_coerce_unsigned_conversion(cc, type,
                                         cc_last_expr_type))
        return;
      if (sym) {
        if (type == TYPE_FLOAT || type == TYPE_DOUBLE) {
          if (!cc_coerce_fp_assignment(cc, type))
            return;
          if (type == TYPE_DOUBLE)
            emit_movsd_disp32_xmm(cc, 0, sym->address);
          else
            emit_movss_disp32_xmm(cc, 0, sym->address);
        } else {
          emit8(cc, 0xA3); /* mov [addr], eax */
          emit32(cc, sym->address);
        }
      }
    }
  }

  cc_expect(cc, CC_TOK_SEMICOLON);
}

static void cc_parse_declaration(cc_state_t *cc, cc_type_t type) {
  int type_struct_index = cc_last_type_struct_index;
  int type_array_count = cc_last_type_array_count;
  int type_is_const = cc_last_type_is_const_qualified;
  int type_typedef_index = cc_last_type_typedef_index;
  int type_has_function_pointer_signature = 0;
  cc_skip_attributes(cc);
  cc_token_t name_tok = cc_next(cc);
  if (name_tok.type != CC_TOK_IDENT) {
    cc_error(cc, "expected variable name");
    return;
  }

  /* Check for array declaration: type name[size] or name[M][N].
   * Also supports comma-separated array decls of the same base type, e.g.
   *   char keyC[64], keyD[64];
   * Mixing array and scalar in one statement is not supported.*/
  if (type_array_count > 0 || cc_peek(cc).type == CC_TOK_LBRACK) {
    int uses_typedef_array = type_array_count > 0;
    if (uses_typedef_array && cc_peek(cc).type == CC_TOK_LBRACK) {
      cc_error(cc, "array declarator after typedef array is not supported");
      return;
    }
    if (!uses_typedef_array)
      cc_next(cc); /* consume '[' */
    while (1) {
      int32_t arr_size;
      if (uses_typedef_array) {
        arr_size = type_array_count;
      } else {
        if (!cc_parse_const_int_expr(cc, &arr_size)) {
          cc_error(cc, "expected array size");
          return;
        }
        if (arr_size <= 0) {
          cc_error(cc, "array size must be positive");
          return;
        }
        cc_expect(cc, CC_TOK_RBRACK);
      }

      int32_t inner_dim = 0;
      int32_t inner_dim2 = 0;
      int has_inner_dim = 0;
      int has_inner_dim2 = 0;
      /* Check for 2D array: type name[M][N] */
      if (cc_peek(cc).type == CC_TOK_LBRACK) {
        has_inner_dim = 1;
        cc_next(cc); /* consume '[' */
        if (!cc_parse_const_int_expr(cc, &inner_dim)) {
          cc_error(cc, "expected array size");
          return;
        }
        if (inner_dim <= 0) {
          cc_error(cc, "array size must be positive");
          return;
        }
        cc_expect(cc, CC_TOK_RBRACK);
        if (cc_peek(cc).type == CC_TOK_LBRACK) {
          has_inner_dim2 = 1;
          cc_next(cc);
          if (!cc_parse_const_int_expr(cc, &inner_dim2)) {
            cc_error(cc, "expected array size");
            return;
          }
          if (inner_dim2 <= 0) {
            cc_error(cc, "array size must be positive");
            return;
          }
          cc_expect(cc, CC_TOK_RBRACK);
        }
      }

      int32_t total_bytes;
      int aes; /* array_elem_size for subscript scaling */
      int dim2 = 0;
      cc_type_t arr_type;

      if (type == TYPE_STRUCT && (has_inner_dim || has_inner_dim2)) {
        cc_error(cc, "struct arrays support one dimension");
        return;
      }

      if (type == TYPE_STRUCT && type_struct_index >= 0 &&
          type_struct_index < cc->struct_count) {
        if (!cc_struct_is_complete(cc, type_struct_index)) {
          cc_error(cc, "array of incomplete struct type");
          return;
        }
        int32_t ssize = cc->structs[type_struct_index].total_size;
        if (!cc_checked_array_bytes(cc, arr_size, ssize, &total_bytes))
          return;
        aes = ssize;
        arr_type = TYPE_STRUCT_PTR;
      } else if (has_inner_dim2) {
        int base_elem = cc_type_size(cc, type, type_struct_index);
        int32_t middle_stride;
        int32_t row_size;
        if (!cc_checked_array_bytes(cc, inner_dim2, base_elem,
                                    &middle_stride) ||
            !cc_checked_array_bytes(cc, inner_dim, middle_stride, &row_size) ||
            !cc_checked_array_bytes(cc, arr_size, row_size, &total_bytes))
          return;
        aes = row_size;
        dim2 = middle_stride;
        arr_type = cc_object_pointer_type(type);
      } else if (has_inner_dim) {
        int base_elem = cc_type_size(cc, type, type_struct_index);
        int32_t row_size;
        if (!cc_checked_array_bytes(cc, inner_dim, base_elem, &row_size) ||
            !cc_checked_array_bytes(cc, arr_size, row_size, &total_bytes))
          return;
        aes = row_size;
        arr_type = cc_object_pointer_type(type);
      } else {
        int elem_size = cc_type_size(cc, type, type_struct_index);
        if (elem_size <= 0) {
          cc_error(cc, "invalid array element type");
          return;
        }
        if (!cc_checked_array_bytes(cc, arr_size, elem_size, &total_bytes))
          return;
        aes = elem_size;
        arr_type = cc_object_pointer_type(type);
      }

      int32_t array_object_size = total_bytes;
      total_bytes = (total_bytes + 3) & ~3;
      int32_t local_slot;
      int32_t array_align = cc_type_align(cc, type, type_struct_index);
      if (array_align < 4) array_align = 4;
      if (!cc_reserve_local_frame(cc, total_bytes, array_align, &local_slot))
        return;
      cc_symbol_t *sym = cc_sym_add(cc, name_tok.text, SYM_LOCAL, arr_type);
      if (sym) {
        sym->offset = local_slot;
        sym->is_array = 1;
        sym->is_const_qualified = type_is_const;
        sym->struct_index = type_struct_index;
        sym->array_elem_size = aes;
        sym->array_object_size = array_object_size;
        sym->array_rank = 1 + has_inner_dim + has_inner_dim2;
        sym->array_dim2 = dim2;
        sym->array_elem_type = type;
      }

      if (!cc_match(cc, CC_TOK_COMMA)) break;
      name_tok = cc_next(cc);
      if (name_tok.type != CC_TOK_IDENT) {
        cc_error(cc, "expected variable name");
        return;
      }
      if (uses_typedef_array) {
        if (cc_peek(cc).type == CC_TOK_LBRACK) {
          cc_error(cc,
                   "array declarator after typedef array is not supported");
          return;
        }
      } else if (!cc_match(cc, CC_TOK_LBRACK)) {
        cc_error(cc, "expected array size for additional declarator");
        return;
      }
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
    int32_t local_slot;
    int32_t struct_align = cc->structs[type_struct_index].align;
    if (struct_align < 4) struct_align = 4;
    if (!cc_reserve_local_frame(cc, alloc_size, struct_align, &local_slot))
      return;
    cc_symbol_t *sym = cc_sym_add(cc, name_tok.text, SYM_LOCAL, TYPE_STRUCT);
    if (sym) {
      sym->offset = local_slot;
      sym->is_const_qualified = type_is_const;
      sym->struct_index = type_struct_index;
    }
    /* Zero-initialize the struct */
    emit_lea_local(cc, local_slot);
    emit_push_eax(cc);
    emit_push_imm(cc, 0);
    emit_push_imm(cc, (uint32_t)alloc_size);
    /* Call memset(addr, 0, size) - push in reverse for cdecl */
    /* Actually we need: memset(ptr, val, size) with ptr first */
    /* Re-order: push size, push 0, push addr */
    emit_add_esp(cc, 12); /* undo the pushes */
    emit_lea_local(cc, local_slot);
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
   * The prologue guarantees ESP is 16-byte aligned on entry
   * (AND ESP,-16), so rounding the frame offset DOWN to a multiple of
   * 16 keeps [ebp + offset] aligned for MOVAPS.*/
  if (type == TYPE_FLOAT4 || type == TYPE_DOUBLE2) {
    int32_t local_slot;
    if (!cc_reserve_local_frame(cc, 16, 16, &local_slot))
      return;
    cc_symbol_t *sym = cc_sym_add(cc, name_tok.text, SYM_LOCAL, type);
    if (sym) {
      sym->offset = local_slot;
      sym->is_const_qualified = type_is_const;
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
          if (cc_last_expr_type == TYPE_INT ||
              cc_last_expr_type == TYPE_UINT ||
              cc_last_expr_type == TYPE_CHAR) {
            cc_emit_integer_to_fp(cc, cc_last_expr_type, TYPE_FLOAT, 0);
          } else if (cc_last_expr_type == TYPE_DOUBLE) {
            emit_cvtsd2ss(cc, 0, 0);
          } else if (cc_last_expr_type != TYPE_FLOAT) {
            cc_error(cc,
                     "float4 initializer element must be an arithmetic scalar");
            return;
          }
          cc_last_xmm = 0;
          emit_movss_local_xmm(cc, 0, local_slot + i * elem_size);
        } else {
          if (cc_last_expr_type == TYPE_INT ||
              cc_last_expr_type == TYPE_UINT ||
              cc_last_expr_type == TYPE_CHAR) {
            cc_emit_integer_to_fp(cc, cc_last_expr_type, TYPE_DOUBLE, 0);
          } else if (cc_last_expr_type == TYPE_FLOAT) {
            emit_cvtss2sd(cc, 0, 0);
          } else if (cc_last_expr_type != TYPE_DOUBLE) {
            cc_error(cc,
                     "double2 initializer element must be an arithmetic scalar");
            return;
          }
          cc_last_xmm = 0;
          emit_movsd_local_xmm(cc, 0, local_slot + i * elem_size);
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
          emit_movss_local_xmm(cc, 0, local_slot + i * elem_size);
        else
          emit_movsd_local_xmm(cc, 0, local_slot + i * elem_size);
      }
    }

    cc_expect(cc, CC_TOK_SEMICOLON);
    return;
  }

  /* Regular variable - allocate stack slot sized to the type. */
  int local_size = 4;
  if (type == TYPE_DOUBLE)
    local_size = 8;
  /* float stays at 4 bytes; other scalar types also 4 bytes. */
  int32_t local_slot;
  if (!cc_reserve_local_frame(cc, local_size, 4, &local_slot))
    return;
  cc_symbol_t *sym = cc_sym_add(cc, name_tok.text, SYM_LOCAL, type);
  if (sym) {
    sym->offset = local_slot;
    sym->is_const_qualified = type_is_const;
    sym->struct_index = type_struct_index;
    if (type == TYPE_FUNC_PTR)
      type_has_function_pointer_signature =
          cc_copy_function_pointer_typedef_signature(
              cc, type_typedef_index, sym);
  }

  if (type == TYPE_FUNC_PTR && type_has_function_pointer_signature) {
    if (!cc_parse_function_pointer_local_initializer(
            cc, sym, local_slot))
      return;
    while (cc_match(cc, CC_TOK_COMMA)) {
      cc_token_t next_name = cc_next(cc);
      int32_t next_local_slot;
      cc_symbol_t *next_symbol;

      if (next_name.type != CC_TOK_IDENT) {
        cc_error(cc, "expected variable name after ','");
        return;
      }
      if (!cc_reserve_local_frame(cc, 4, 4, &next_local_slot))
        return;
      next_symbol = cc_sym_add(
          cc, next_name.text, SYM_LOCAL, TYPE_FUNC_PTR);
      if (next_symbol) {
        next_symbol->offset = next_local_slot;
        next_symbol->is_const_qualified = type_is_const;
        next_symbol->struct_index = type_struct_index;
        (void)cc_copy_function_pointer_typedef_signature(
            cc, type_typedef_index, next_symbol);
      }
      if (!cc_parse_function_pointer_local_initializer(
              cc, next_symbol, next_local_slot))
        return;
    }
    cc_expect(cc, CC_TOK_SEMICOLON);
    return;
  }

  /* Check for initializer */
  if (cc_peek(cc).type == CC_TOK_EQ) {
    cc_next(cc); /* consume '=' */
    cc_parse_expression(cc, 1);
    if (!cc_coerce_unsigned_conversion(cc, type,
                                       cc_last_expr_type))
      return;
    if (type == TYPE_FLOAT) {
      /* Coerce initializer into float if needed. */
      if (cc_last_expr_type == TYPE_INT ||
          cc_last_expr_type == TYPE_UINT ||
          cc_last_expr_type == TYPE_CHAR) {
        cc_emit_integer_to_fp(cc, cc_last_expr_type, TYPE_FLOAT, 0);
        cc_last_xmm = 0;
      } else if (cc_last_expr_type == TYPE_DOUBLE) {
        emit_cvtsd2ss(cc, 0, 0);
        cc_last_xmm = 0;
      } else if (cc_last_expr_type != TYPE_FLOAT) {
        cc_error(cc, "float initializer requires an arithmetic scalar"
                     " (non-scalar initializer not supported)");
      }
      emit_movss_local_xmm(cc, 0, local_slot);
    } else if (type == TYPE_DOUBLE) {
      if (cc_last_expr_type == TYPE_INT ||
          cc_last_expr_type == TYPE_UINT ||
          cc_last_expr_type == TYPE_CHAR) {
        cc_emit_integer_to_fp(cc, cc_last_expr_type, TYPE_DOUBLE, 0);
        cc_last_xmm = 0;
      } else if (cc_last_expr_type == TYPE_FLOAT) {
        emit_cvtss2sd(cc, 0, 0);
        cc_last_xmm = 0;
      } else if (cc_last_expr_type != TYPE_DOUBLE) {
        cc_error(cc, "double initializer requires an arithmetic scalar"
                     " (non-scalar initializer not supported)");
      }
      emit_movsd_local_xmm(cc, 0, local_slot);
    } else {
      emit_store_local(cc, local_slot);
    }
  } else {
    /* Zero-initialize */
    if (type == TYPE_FLOAT) {
      /* XORPS xmm0, xmm0 then MOVSS [ebp+disp], xmm0 */
      emit8(cc, 0x0F);
      emit8(cc, 0x57);
      emit8(cc, 0xC0);
      emit_movss_local_xmm(cc, 0, local_slot);
    } else if (type == TYPE_DOUBLE) {
      emit8(cc, 0x0F);
      emit8(cc, 0x57);
      emit8(cc, 0xC0); /* XORPS xmm0, xmm0 */
      emit_movsd_local_xmm(cc, 0, local_slot);
    } else {
      emit_mov_eax_imm(cc, 0);
      emit_store_local(cc, local_slot);
    }
  }

  /* Multi-declarator support: `int a = 0, b = 0, c;` parses each
   * comma-separated declarator with the same base type. Float/double
   * multi-decl is intentionally not supported here (rare); fall back to
   * separate statements for those.*/
  while (cc_peek(cc).type == CC_TOK_COMMA) {
    if (type == TYPE_FLOAT || type == TYPE_DOUBLE ||
        type == TYPE_FLOAT4 || type == TYPE_DOUBLE2 ||
        type == TYPE_STRUCT) {
      break;     /* fall through to SEMICOLON expect (will likely error) */
    }
    cc_next(cc);     /* consume ',' */
    cc_token_t next_name = cc_next(cc);
    if (next_name.type != CC_TOK_IDENT) {
      cc_error(cc, "expected variable name after ','");
      return;
    }
    int32_t next_local_slot;
    if (!cc_reserve_local_frame(cc, local_size, 4, &next_local_slot))
      return;
    cc_symbol_t *sym2 = cc_sym_add(cc, next_name.text, SYM_LOCAL, type);
    if (sym2) {
      sym2->offset = next_local_slot;
      sym2->is_const_qualified = type_is_const;
      sym2->struct_index = type_struct_index;
    }
    if (cc_peek(cc).type == CC_TOK_EQ) {
      cc_next(cc);
      cc_parse_expression(cc, 1);
      if (!cc_coerce_unsigned_conversion(cc, type,
                                         cc_last_expr_type))
        return;
      emit_store_local(cc, next_local_slot);
    } else {
      emit_mov_eax_imm(cc, 0);
      emit_store_local(cc, next_local_slot);
    }
  }

  cc_expect(cc, CC_TOK_SEMICOLON);
}

static cc_token_type_t cc_statement_token_type(cc_state_t *cc) {
  cc_token_t tok = cc_peek(cc);
  return tok.type;
}

static void cc_consume_statement_keyword(cc_state_t *cc) {
  cc_next(cc);
}

static void cc_parse_if(cc_state_t *cc) {
  cc_expect(cc, CC_TOK_LPAREN);
  cc_parse_expression(cc, 1);
  if (cc->error ||
      !cc_materialize_scalar_truth(cc, cc_last_expr_type))
    return;
  cc_expect(cc, CC_TOK_RPAREN);

  /* test eax, eax; je else_label */
  emit_cmp_eax_zero(cc);
  uint32_t else_patch = emit_jcc_placeholder(cc, 0x84); /* je */

  cc_parse_statement(cc);

  if (cc_statement_token_type(cc) == CC_TOK_ELSE) {
    cc_next(cc);
    uint32_t end_patch = emit_jmp_placeholder(cc);
    patch_jump(cc, else_patch);
    cc_parse_statement(cc);
    patch_jump(cc, end_patch);
  } else {
    patch_jump(cc, else_patch);
  }
}

static int cc_begin_control(cc_state_t *cc, cc_control_kind_t kind,
                            uint32_t continue_target) {
  int depth = cc->control_depth;

  if (depth >= CC_MAX_CONTROL_DEPTH) {
    cc_error(cc, "control nesting too deep");
    return 0;
  }

  cc->break_counts[depth] = 0;
  cc->control_kinds[depth] = kind;
  cc->continue_targets[depth] = continue_target;
  cc->control_depth = depth + 1;
  return 1;
}

static void cc_finish_control(cc_state_t *cc, int depth) {
  if (depth < CC_MAX_CONTROL_DEPTH && cc->control_depth > depth) {
    for (int i = 0; i < cc->break_counts[depth]; i++) {
      patch_jump(cc, cc->break_patches[depth][i]);
    }
    cc->break_counts[depth] = 0;
  }
  cc->control_depth = depth;
}

static void cc_parse_while(cc_state_t *cc) {
  uint32_t loop_start = cc->code_pos;

  /* Push loop context */
  int old_depth = cc->control_depth;
  if (!cc_begin_control(cc, CC_CONTROL_LOOP, loop_start))
    return;

  cc_expect(cc, CC_TOK_LPAREN);
  cc_parse_expression(cc, 1);
  if (cc->error ||
      !cc_materialize_scalar_truth(cc, cc_last_expr_type))
    return;
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
  cc_finish_control(cc, old_depth);
}

static void cc_parse_for_initializer(cc_state_t *cc) {
  if (cc_peek(cc).type != CC_TOK_SEMICOLON) {
    if (cc_is_type_or_typedef(cc, cc_peek(cc))) {
      cc_type_t type = cc_parse_type(cc);
      cc_parse_declaration(cc, type);
      /* declaration already consumed semicolon */
    } else {
      /* Expression statement */
      cc_lexer_checkpoint_t checkpoint;
      cc_checkpoint_lexer(cc, &checkpoint);
      cc_token_t id = cc_next(cc);
      if (id.type == CC_TOK_IDENT && cc_is_assignment_op(cc_peek(cc).type)) {
        cc_parse_assignment(cc, id.text);
      } else {
        cc_restore_lexer(cc, &checkpoint);
        cc_parse_expression(cc, 1);
      }
      cc_expect(cc, CC_TOK_SEMICOLON);
    }
  } else {
    cc_next(cc); /* consume ';' */
  }
}

static void cc_parse_for_increment(cc_state_t *cc) {
  if (cc_peek(cc).type != CC_TOK_RPAREN) {
    cc_lexer_checkpoint_t checkpoint;
    cc_checkpoint_lexer(cc, &checkpoint);
    cc_token_t id = cc_next(cc);
    if (id.type == CC_TOK_IDENT && cc_is_assignment_op(cc_peek(cc).type)) {
      cc_parse_assignment(cc, id.text);
    } else if (id.type == CC_TOK_IDENT && cc_peek(cc).type == CC_TOK_PLUSPLUS) {
      cc_next(cc);
      cc_symbol_t *sym = cc_sym_find(cc, id.text);
      cc_emit_variable_update(cc, sym, 0, 0);
    } else if (id.type == CC_TOK_IDENT &&
               cc_peek(cc).type == CC_TOK_MINUSMINUS) {
      cc_next(cc);
      cc_symbol_t *sym = cc_sym_find(cc, id.text);
      cc_emit_variable_update(cc, sym, 1, 0);
    } else {
      cc_restore_lexer(cc, &checkpoint);
      cc_parse_expression(cc, 1);
    }
  }
}

static void cc_parse_for(cc_state_t *cc) {
  cc_expect(cc, CC_TOK_LPAREN);
  int old_depth = cc->control_depth;
  if (old_depth >= CC_MAX_CONTROL_DEPTH) {
    cc_error(cc, "control nesting too deep");
    return;
  }

  cc_parse_for_initializer(cc);

  uint32_t cond_start = cc->code_pos;

  /* Push loop context */
  /* Condition */
  uint32_t exit_patch = 0;
  if (cc_statement_token_type(cc) != CC_TOK_SEMICOLON) {
    cc_parse_expression(cc, 1);
    if (cc->error ||
        !cc_materialize_scalar_truth(cc, cc_last_expr_type))
      return;
    emit_cmp_eax_zero(cc);
    exit_patch = emit_jcc_placeholder(cc, 0x84); /* je */
  }
  cc_expect(cc, CC_TOK_SEMICOLON);

  /* Save increment expression position - we'll emit a jmp over it */
  uint32_t inc_jump = emit_jmp_placeholder(cc);
  uint32_t inc_start = cc->code_pos;

  /* Set continue target to increment */
  if (!cc_begin_control(cc, CC_CONTROL_LOOP, inc_start))
    return;

  cc_parse_for_increment(cc);
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
  cc_finish_control(cc, old_depth);
}

static void cc_parse_return(cc_state_t *cc) {
  if (cc_peek(cc).type == CC_TOK_SEMICOLON &&
      cc_is_simd_value_type(cc->current_return_type)) {
    cc_error(cc, "SIMD return requires a matching float4 or double2 value");
    return;
  }
  if (cc_peek(cc).type != CC_TOK_SEMICOLON) {
    cc_parse_expression(cc, 1);
    if ((cc->current_return_type == TYPE_FLOAT4 ||
         cc->current_return_type == TYPE_DOUBLE2 ||
         cc_last_expr_type == TYPE_FLOAT4 ||
         cc_last_expr_type == TYPE_DOUBLE2) &&
        cc_last_expr_type != cc->current_return_type) {
      cc_error(cc,
               "SIMD return requires a matching float4 or double2 value");
      return;
    }
    if (cc_cdecl_slot_size(cc->current_return_type) > 0 &&
        !cc_coerce_cdecl_argument(cc, cc->current_return_type))
      return;
    /* Floating and packed return values live in XMM0. If a later pass routes
     * one through another XMM register, restore the ABI register here. */
    if ((cc_last_expr_type == TYPE_FLOAT ||
         cc_last_expr_type == TYPE_DOUBLE ||
         cc_last_expr_type == TYPE_FLOAT4 ||
         cc_last_expr_type == TYPE_DOUBLE2) &&
        cc_last_xmm != 0) {
      emit_movaps_xmm_xmm(cc, 0, cc_last_xmm);
      cc_last_xmm = 0;
    }
  }
  cc_expect(cc, CC_TOK_SEMICOLON);

  /* Emit epilogue (function cleanup + ret) */
  emit_epilogue(cc);
}

static void cc_parse_do(cc_state_t *cc) {
  /* The condition follows the body, so continue uses a patched trampoline. */
  uint32_t body_entry_patch = emit_jmp_placeholder(cc);
  uint32_t continue_target = cc->code_pos;
  uint32_t condition_patch = emit_jmp_placeholder(cc);
  uint32_t loop_start = cc->code_pos;
  int old_depth = cc->control_depth;

  patch_jump(cc, body_entry_patch);
  if (!cc_begin_control(cc, CC_CONTROL_LOOP, continue_target))
    return;

  cc_parse_statement(cc);
  if (!cc->error) {
    cc_expect(cc, CC_TOK_WHILE);
    cc_expect(cc, CC_TOK_LPAREN);
    patch_jump(cc, condition_patch);
    cc_parse_expression(cc, 1);
    if (cc->error ||
        !cc_materialize_scalar_truth(cc, cc_last_expr_type))
      return;
    cc_expect(cc, CC_TOK_RPAREN);
    cc_expect(cc, CC_TOK_SEMICOLON);
    emit_cmp_eax_zero(cc);
    emit8(cc, 0x0F);
    emit8(cc, 0x85); /* jne rel32 */
    {
      int32_t rel = (int32_t)(loop_start - (cc->code_pos + 4));
      emit32(cc, (uint32_t)rel);
    }
  }

  cc_finish_control(cc, old_depth);
}

static void cc_parse_switch(cc_state_t *cc) {
  int old_depth = cc->control_depth;
  uint32_t next_case_patch = 0;
  int had_default = 0;

  if (!cc_begin_control(cc, CC_CONTROL_SWITCH, 0))
    return;

  cc_expect(cc, CC_TOK_LPAREN);
  cc_parse_expression(cc, 1);
  cc_expect(cc, CC_TOK_RPAREN);
  emit_push_eax(cc);
  cc_expect(cc, CC_TOK_LBRACE);

  while (!cc->error && cc_statement_token_type(cc) != CC_TOK_RBRACE &&
         cc_statement_token_type(cc) != CC_TOK_EOF) {
    if (cc_statement_token_type(cc) == CC_TOK_CASE) {
      cc_next(cc);
      if (next_case_patch)
        patch_jump(cc, next_case_patch);

      emit8(cc, 0x8B);
      emit8(cc, 0x04);
      emit8(cc, 0x24); /* mov eax, [esp] */
      {
        cc_token_t cval = cc_next(cc);
        if (cval.type == CC_TOK_NUMBER || cval.type == CC_TOK_CHAR_LIT) {
          emit8(cc, 0x3D); /* cmp eax, imm32 */
          emit32(cc, (uint32_t)cval.int_value);
        } else {
          cc_error(cc, "case: expected constant");
          break;
        }
      }
      cc_expect(cc, CC_TOK_COLON);
      next_case_patch = emit_jcc_placeholder(cc, 0x85); /* jne */
      while (!cc->error && cc_statement_token_type(cc) != CC_TOK_CASE &&
             cc_statement_token_type(cc) != CC_TOK_DEFAULT &&
             cc_statement_token_type(cc) != CC_TOK_RBRACE &&
             cc_statement_token_type(cc) != CC_TOK_EOF) {
        cc_parse_statement(cc);
      }
    } else if (cc_statement_token_type(cc) == CC_TOK_DEFAULT) {
      cc_next(cc);
      cc_expect(cc, CC_TOK_COLON);
      if (next_case_patch)
        patch_jump(cc, next_case_patch);
      next_case_patch = 0;
      had_default = 1;
      while (!cc->error && cc_statement_token_type(cc) != CC_TOK_CASE &&
             cc_statement_token_type(cc) != CC_TOK_RBRACE &&
             cc_statement_token_type(cc) != CC_TOK_EOF) {
        cc_parse_statement(cc);
      }
    } else {
      cc_error(cc, "expected case or default");
    }
  }

  if (!cc->error) {
    cc_expect(cc, CC_TOK_RBRACE);
    if (next_case_patch && !had_default)
      patch_jump(cc, next_case_patch);
    emit_add_esp(cc, 4);
  }
  cc_finish_control(cc, old_depth);
}

static void cc_parse_simple_statement(cc_state_t *cc);

static void cc_parse_statement(cc_state_t *cc) {
  cc_token_type_t type;

  if (cc->error)
    return;
  if (cc->statement_depth >= CC_MAX_STATEMENT_DEPTH) {
    cc_error(cc, "statement nesting too deep");
    return;
  }

  cc->statement_depth++;
  type = cc_statement_token_type(cc);
  switch (type) {
  case CC_TOK_IF:
    cc_consume_statement_keyword(cc);
    cc_parse_if(cc);
    break;
  case CC_TOK_WHILE:
    cc_consume_statement_keyword(cc);
    cc_parse_while(cc);
    break;
  case CC_TOK_FOR:
    cc_consume_statement_keyword(cc);
    cc_parse_for(cc);
    break;
  case CC_TOK_DO:
    cc_consume_statement_keyword(cc);
    cc_parse_do(cc);
    break;
  case CC_TOK_SWITCH:
    cc_consume_statement_keyword(cc);
    cc_parse_switch(cc);
    break;
  case CC_TOK_LBRACE:
    cc_consume_statement_keyword(cc);
    cc_parse_block(cc);
    break;
  default:
    cc_parse_simple_statement(cc);
    break;
  }
  cc->statement_depth--;
}

static int cc_skip_balanced_declarator_tokens(cc_state_t *cc,
                                               cc_token_type_t open,
                                               cc_token_type_t close) {
  int depth = 1;
  if (cc_peek(cc).type != open) {
    cc_error(cc, "expected declarator delimiter");
    return 0;
  }
  cc_next(cc);
  while (depth > 0 && !cc->error) {
    cc_token_t token = cc_next(cc);
    if (token.type == open)
      depth++;
    else if (token.type == close)
      depth--;
    else if (token.type == CC_TOK_EOF) {
      cc_error(cc, "unexpected EOF in function-pointer declarator");
      return 0;
    }
  }
  return !cc->error;
}

static int cc_parse_function_pointer_parameter_type(
    cc_state_t *cc, cc_type_t *out_type, int8_t *out_struct_index) {
  cc_type_t parameter_type = cc_parse_type(cc);
  int parameter_struct_index = cc_last_type_struct_index;
  int typedef_array_count = cc_last_type_array_count;
  if (cc->error)
    return 0;

  if (cc_peek(cc).type == CC_TOK_LPAREN) {
    int pointer_depth = 0;
    cc_next(cc);
    while (cc_peek(cc).type == CC_TOK_STAR) {
      cc_next(cc);
      pointer_depth++;
      while (cc_is_type_prefix(cc_peek(cc).type)) {
        if (cc_peek(cc).type == CC_TOK_ATTRIBUTE)
          cc_skip_attributes(cc);
        else
          cc_next(cc);
      }
    }
    if (pointer_depth == 0) {
      cc_error(cc, "function-pointer parameter declarator requires '*'");
      return 0;
    }
    if (cc_peek(cc).type == CC_TOK_IDENT)
      cc_next(cc); /* optional parameter name */
    cc_expect(cc, CC_TOK_RPAREN);
    if (cc->error)
      return 0;

    if (cc_peek(cc).type == CC_TOK_LPAREN) {
      /* A nested function-pointer parameter is one four-byte ABI value.
       * Its own signature is not needed to lay out the outer call. */
      if (!cc_skip_balanced_declarator_tokens(
              cc, CC_TOK_LPAREN, CC_TOK_RPAREN))
        return 0;
      parameter_type = TYPE_FUNC_PTR;
    } else {
      parameter_type = cc_apply_pointer_declarator(
          cc, parameter_type, pointer_depth);
      if (cc->error)
        return 0;
      /* Pointer-to-array parameters also occupy one pointer slot. */
      while (cc_peek(cc).type == CC_TOK_LBRACK) {
        if (!cc_skip_balanced_declarator_tokens(
                cc, CC_TOK_LBRACK, CC_TOK_RBRACK))
          return 0;
      }
    }
  } else {
    if (cc_peek(cc).type == CC_TOK_IDENT)
      cc_next(cc); /* optional parameter name */
    parameter_type = cc_adjust_array_parameter_declarator(
        cc, parameter_type, typedef_array_count);
    if (cc->error)
      return 0;
  }

  if (cc_peek(cc).type == CC_TOK_ATTRIBUTE)
    cc_skip_attributes(cc);
  if (cc_cdecl_slot_size(parameter_type) == 0) {
    cc_error(cc, "function-pointer parameter type is not supported");
    return 0;
  }
  *out_type = parameter_type;
  *out_struct_index = parameter_type == TYPE_STRUCT_PTR
                          ? (int8_t)parameter_struct_index
                          : (int8_t)-1;
  return 1;
}

static int cc_parse_function_pointer_signature(
    cc_state_t *cc, uint8_t param_types[CC_MAX_PARAMS],
    int8_t param_struct_indices[CC_MAX_PARAMS], int *param_count,
    int *has_param_types, int *is_variadic) {
  cc_lexer_checkpoint_t checkpoint;
  *param_count = 0;
  *has_param_types = 0;
  *is_variadic = 0;
  memset(param_types, 0, CC_MAX_PARAMS * sizeof(param_types[0]));
  memset(param_struct_indices, -1,
         CC_MAX_PARAMS * sizeof(param_struct_indices[0]));

  cc_expect(cc, CC_TOK_LPAREN);
  if (cc->error)
    return 0;
  if (cc_peek(cc).type == CC_TOK_RPAREN) {
    cc_next(cc); /* C's empty, unprototyped parameter list */
    return 1;
  }

  /* A sole void is a complete zero-parameter prototype. */
  if (cc_peek(cc).type == CC_TOK_VOID) {
    cc_type_t void_type;
    cc_checkpoint_lexer(cc, &checkpoint);
    void_type = cc_parse_type(cc);
    if (!cc->error && void_type == TYPE_VOID &&
        cc_peek(cc).type == CC_TOK_RPAREN) {
      cc_next(cc);
      *has_param_types = 1;
      return 1;
    }
    cc_restore_lexer(cc, &checkpoint);
  }

  while (!cc->error) {
    cc_type_t parameter_type;
    int8_t parameter_struct_index;
    if (cc_peek(cc).type == CC_TOK_ELLIPSIS) {
      cc_next(cc);
      *has_param_types = 1;
      *is_variadic = 1;
      cc_expect(cc, CC_TOK_RPAREN);
      return !cc->error;
    }
    if (*param_count >= CC_MAX_PARAMS) {
      cc_error(cc, "too many function-pointer parameters");
      return 0;
    }
    if (!cc_parse_function_pointer_parameter_type(
            cc, &parameter_type, &parameter_struct_index))
      return 0;
    param_types[*param_count] = (uint8_t)parameter_type;
    param_struct_indices[*param_count] = parameter_struct_index;
    (*param_count)++;
    *has_param_types = 1;

    if (cc_peek(cc).type == CC_TOK_RPAREN) {
      cc_next(cc);
      return 1;
    }
    cc_expect(cc, CC_TOK_COMMA);
    if (cc->error)
      return 0;
  }
  return 0;
}

typedef struct {
  cc_token_t name;
  cc_function_pointer_signature_t signature;
} cc_named_function_pointer_declarator_t;

static int cc_parse_named_function_pointer_declarator(
    cc_state_t *cc, cc_type_t return_type, int return_struct_index,
    int return_array_count,
    cc_named_function_pointer_declarator_t *declarator) {
  if (!declarator)
    return 0;
  memset(declarator, 0, sizeof(*declarator));
  declarator->signature.return_type = return_type;
  declarator->signature.return_struct_index =
      return_type == TYPE_STRUCT_PTR ? return_struct_index : -1;
  memset(declarator->signature.param_struct_indices, -1,
         sizeof(declarator->signature.param_struct_indices));

  cc_expect(cc, CC_TOK_LPAREN);
  if (cc->error)
    return 0;
  cc_expect(cc, CC_TOK_STAR);
  if (cc->error)
    return 0;
  declarator->name = cc_next(cc);
  if (declarator->name.type != CC_TOK_IDENT) {
    cc_error(cc, "expected function pointer name");
    return 0;
  }
  cc_expect(cc, CC_TOK_RPAREN);
  if (cc->error)
    return 0;
  if (return_type == TYPE_STRUCT) {
    cc_error(
        cc,
        "function-pointer struct result is not supported; use pointer result");
    return 0;
  }
  if (return_array_count > 0) {
    cc_error(cc, "function-pointer array result is not supported");
    return 0;
  }
  return cc_parse_function_pointer_signature(
      cc, declarator->signature.param_types,
      declarator->signature.param_struct_indices,
      &declarator->signature.param_count,
      &declarator->signature.has_param_types,
      &declarator->signature.is_variadic);
}

static void cc_apply_named_function_pointer_declarator(
    cc_symbol_t *symbol,
    const cc_named_function_pointer_declarator_t *declarator) {
  if (!symbol || !declarator)
    return;
  symbol->function_pointer_return_type = declarator->signature.return_type;
  symbol->struct_index = declarator->signature.return_struct_index;
  symbol->param_count = declarator->signature.param_count;
  memcpy(symbol->param_types, declarator->signature.param_types,
         sizeof(symbol->param_types));
  memcpy(symbol->param_struct_indices,
         declarator->signature.param_struct_indices,
         sizeof(symbol->param_struct_indices));
  symbol->has_param_types = declarator->signature.has_param_types;
  symbol->is_variadic = declarator->signature.is_variadic;
}

static int cc_raw_function_pointer_signatures_equal(
    const cc_function_pointer_signature_t *left,
    const cc_function_pointer_signature_t *right) {
  int parameter_index;
  if (left->return_type != right->return_type ||
      left->return_struct_index != right->return_struct_index ||
      left->param_count != right->param_count ||
      left->has_param_types != right->has_param_types ||
      left->is_variadic != right->is_variadic)
    return 0;
  for (parameter_index = 0; parameter_index < left->param_count;
       parameter_index++) {
    if (left->param_types[parameter_index] !=
            right->param_types[parameter_index] ||
        left->param_struct_indices[parameter_index] !=
            right->param_struct_indices[parameter_index])
      return 0;
  }
  return 1;
}

static int cc_intern_raw_function_pointer_signature(
    cc_state_t *cc, const cc_function_pointer_signature_t *signature) {
  int index;
  for (index = 0; index < cc->raw_function_pointer_signature_count;
       index++) {
    if (cc_raw_function_pointer_signatures_equal(
            &cc->raw_function_pointer_signatures[index], signature))
      return CC_RAW_FUNCTION_POINTER_SIGNATURE_BASE + index;
  }
  if (cc->raw_function_pointer_signature_count >=
      CC_MAX_RAW_FUNCTION_POINTER_SIGNATURES) {
    cc_error(cc, "too many raw function-pointer signatures");
    return -1;
  }
  index = cc->raw_function_pointer_signature_count++;
  cc->raw_function_pointer_signatures[index] = *signature;
  return CC_RAW_FUNCTION_POINTER_SIGNATURE_BASE + index;
}

typedef struct {
  cc_token_t name;
  cc_type_t type;
  int struct_index;
  int is_const_qualified;
  int function_pointer_signature_handle;
} cc_named_parameter_declarator_t;

static int cc_parse_named_free_function_parameter(
    cc_state_t *cc, cc_type_t base_type, int struct_index,
    int typedef_array_count, int is_const_qualified, int typedef_index,
    cc_named_parameter_declarator_t *parameter) {
  if (!parameter)
    return 0;
  memset(parameter, 0, sizeof(*parameter));
  parameter->type = base_type;
  parameter->struct_index = struct_index;
  parameter->is_const_qualified = is_const_qualified;
  parameter->function_pointer_signature_handle = typedef_index;

  if (cc_peek(cc).type == CC_TOK_LPAREN) {
    cc_named_function_pointer_declarator_t declarator;
    if (!cc_parse_named_function_pointer_declarator(
            cc, base_type, struct_index, typedef_array_count, &declarator))
      return 0;
    parameter->name = declarator.name;
    parameter->type = TYPE_FUNC_PTR;
    parameter->struct_index = declarator.signature.return_struct_index;
    parameter->function_pointer_signature_handle =
        cc_intern_raw_function_pointer_signature(
            cc, &declarator.signature);
    return parameter->function_pointer_signature_handle >= 0;
  }

  parameter->name = cc_next(cc);
  if (parameter->name.type != CC_TOK_IDENT) {
    cc_error(cc, "expected parameter name");
    return 0;
  }
  parameter->type = cc_adjust_array_parameter_declarator(
      cc, base_type, typedef_array_count);
  return !cc->error;
}

/* Parse the declaration tail after a consumed typedef token. Program parsing
 * requires the terminator; the REPL keeps its existing optional terminator. */
static int cc_parse_typedef_declaration(cc_state_t *cc,
                                        int require_semicolon) {
  cc_type_t declared_type = cc_parse_type(cc);
  cc_type_t base_type = cc_last_type_base;
  int struct_index = cc_last_type_struct_index;
  int array_count = cc_last_type_array_count;
  int is_const = cc_last_type_is_const_qualified;
  int pointer_depth;
  int semicolon_consumed = 0;

  if (cc->error)
    return 0;
  pointer_depth = cc_last_type_pointer_depth;

  if (cc_peek(cc).type == CC_TOK_LPAREN) {
    uint8_t parameter_types[CC_MAX_PARAMS];
    int8_t parameter_struct_indices[CC_MAX_PARAMS];
    int parameter_count;
    int has_parameter_types;
    int is_variadic;
    int alias_index;
    cc_token_t alias_tok;

    cc_next(cc);
    cc_expect(cc, CC_TOK_STAR);
    alias_tok = cc_next(cc);
    if (alias_tok.type != CC_TOK_IDENT) {
      cc_error(cc, "expected function-pointer typedef name");
      return 0;
    }
    cc_expect(cc, CC_TOK_RPAREN);
    if (declared_type == TYPE_STRUCT) {
      cc_error(
          cc,
          "function-pointer struct result is not supported; use pointer result");
      return 0;
    }
    if (array_count > 0) {
      cc_error(cc, "function-pointer array result is not supported");
      return 0;
    }
    if (!cc_parse_function_pointer_signature(
            cc, parameter_types, parameter_struct_indices, &parameter_count,
            &has_parameter_types, &is_variadic))
      return 0;
    alias_index = cc->typedef_count;
    cc_add_typedef_alias(cc, alias_tok.text, TYPE_FUNC_PTR, struct_index, 0,
                         is_const);
    if (cc->error)
      return 0;
    cc->typedef_function_pointer_signature_valid[alias_index] = 1;
    cc->typedef_function_pointer_return_types[alias_index] = declared_type;
    cc->typedef_function_pointer_return_struct_indices[alias_index] =
        declared_type == TYPE_STRUCT_PTR ? struct_index : -1;
    cc->typedef_function_pointer_param_counts[alias_index] = parameter_count;
    memcpy(cc->typedef_function_pointer_param_types[alias_index],
           parameter_types, sizeof(parameter_types));
    memcpy(cc->typedef_function_pointer_param_struct_indices[alias_index],
           parameter_struct_indices, sizeof(parameter_struct_indices));
    cc->typedef_function_pointer_has_param_types[alias_index] =
        has_parameter_types;
    cc->typedef_function_pointer_is_variadic[alias_index] = is_variadic;
    if (require_semicolon)
      cc_expect(cc, CC_TOK_SEMICOLON);
    else if (cc_peek(cc).type == CC_TOK_SEMICOLON)
      cc_next(cc);
    return !cc->error;
  }

  while (!cc->error) {
    cc_token_t alias_tok;
    cc_type_t alias_type;
    int alias_array_count;

    while (cc_match(cc, CC_TOK_STAR))
      pointer_depth++;
    alias_tok = cc_next(cc);
    if (alias_tok.type != CC_TOK_IDENT) {
      semicolon_consumed = alias_tok.type == CC_TOK_SEMICOLON;
      cc_error(cc, "expected typedef alias name");
      break;
    }
    if (pointer_depth > 0 && array_count > 0) {
      cc_error(cc, "pointer to typedef array is not supported");
      break;
    }
    alias_type = cc_apply_pointer_declarator(cc, base_type, pointer_depth);
    if (cc->error)
      break;
    if (!cc_parse_typedef_array_declarator(
            cc, alias_type, struct_index, array_count, &alias_array_count))
      break;
    cc_add_typedef_alias(cc, alias_tok.text, alias_type, struct_index,
                         alias_array_count, is_const);
    if (cc->error || !cc_match(cc, CC_TOK_COMMA))
      break;
    pointer_depth = 0;
  }
  if (!cc->error && require_semicolon && !semicolon_consumed)
    cc_expect(cc, CC_TOK_SEMICOLON);
  else if (!cc->error && !require_semicolon &&
           cc_peek(cc).type == CC_TOK_SEMICOLON)
    cc_next(cc);
  return !cc->error;
}

typedef enum {
  CC_FP_INITIALIZER_OTHER = 0,
  CC_FP_INITIALIZER_DESIGNATOR,
  CC_FP_INITIALIZER_ZERO,
  CC_FP_INITIALIZER_EXPLICIT_CAST
} cc_function_pointer_initializer_kind_t;

/* Classify the few initializer shapes whose function-pointer meaning the
 * private type model can prove. Grouped designators and zero are retained.
 * A cast through void * deliberately erases the source signature. */
static cc_function_pointer_initializer_kind_t
cc_probe_function_pointer_initializer(cc_state_t *cc,
                                      cc_symbol_t **out_target) {
  cc_lexer_checkpoint_t checkpoint;
  cc_token_t token;
  cc_symbol_t *target = NULL;
  int grouping_depth = 0;

  *out_target = NULL;

  cc_checkpoint_lexer(cc, &checkpoint);
  while (cc_lex_peek(cc).type == CC_TOK_LPAREN) {
    (void)cc_lex_next(cc);
    grouping_depth++;
  }

  token = cc_lex_next(cc);
  if (token.type != CC_TOK_IDENT && token.type != CC_TOK_NUMBER)
    goto done;
  while (grouping_depth > 0) {
    if (cc_lex_next(cc).type != CC_TOK_RPAREN)
      goto done;
    grouping_depth--;
  }
  if (cc_lex_peek(cc).type != CC_TOK_SEMICOLON)
    goto done;

  if (token.type == CC_TOK_NUMBER && token.int_value == 0) {
    cc_restore_lexer(cc, &checkpoint);
    return CC_FP_INITIALIZER_ZERO;
  }
  if (token.type != CC_TOK_IDENT)
    goto done;

  target = cc_sym_find(cc, token.text);
  if (!target ||
      ((target->kind != SYM_FUNC && target->kind != SYM_KERNEL) &&
       !((target->kind == SYM_LOCAL || target->kind == SYM_PARAM ||
          target->kind == SYM_GLOBAL) &&
         target->type == TYPE_FUNC_PTR)))
    goto done;

  cc_restore_lexer(cc, &checkpoint);
  *out_target = target;
  return CC_FP_INITIALIZER_DESIGNATOR;

done:
  cc_restore_lexer(cc, &checkpoint);
  return CC_FP_INITIALIZER_OTHER;
}

static cc_type_t cc_function_pointer_designator_result_type(
    const cc_symbol_t *target) {
  if (target && target->type == TYPE_FUNC_PTR)
    return target->function_pointer_return_type;
  return target ? target->type : TYPE_VOID;
}

static int cc_function_signature_is_prescan_unknown(
    const cc_symbol_t *target) {
  return target && target->kind == SYM_FUNC && !target->is_defined &&
         !target->has_param_types &&
         !target->function_signature_is_provisional;
}

static int cc_function_pointer_signatures_match(
    const cc_symbol_t *left, const cc_symbol_t *right) {
  cc_type_t left_result;
  cc_type_t right_result;
  int parameter_index;

  if (!left || !right)
    return 0;
  if (cc_function_signature_is_prescan_unknown(left) ||
      cc_function_signature_is_prescan_unknown(right))
    return 1;
  left_result = cc_function_pointer_designator_result_type(left);
  right_result = cc_function_pointer_designator_result_type(right);
  if (left_result != right_result ||
      (left_result == TYPE_STRUCT_PTR &&
       left->struct_index != right->struct_index))
    return 0;
  if (!left->has_param_types || !right->has_param_types)
    return 1;
  if (left->param_count != right->param_count ||
      left->is_variadic != right->is_variadic)
    return 0;
  for (parameter_index = 0; parameter_index < left->param_count;
       parameter_index++) {
    if (left->param_types[parameter_index] !=
            right->param_types[parameter_index] ||
        (left->param_types[parameter_index] == TYPE_STRUCT_PTR &&
         left->param_struct_indices[parameter_index] !=
             right->param_struct_indices[parameter_index]))
      return 0;
  }
  return 1;
}

typedef enum {
  CC_SIGNATURE_ROLLBACK_PRESCAN_UNKNOWN,
  CC_SIGNATURE_ROLLBACK_UNTYPED_PROVISIONAL
} cc_signature_rollback_kind_t;

typedef struct {
  int symbol_index;
  cc_signature_rollback_kind_t kind;
  cc_type_t prior_type;
  int prior_struct_index;
} cc_signature_rollback_entry_t;

static cc_state_t *cc_signature_journal_owner;
static cc_signature_rollback_entry_t
    cc_signature_journal[CC_MAX_SYMBOLS];
static int cc_signature_journal_count;

static void cc_restore_signature_journal_entry(
    cc_state_t *cc, const cc_signature_rollback_entry_t *entry) {
  cc_symbol_t *target = &cc->symbols[entry->symbol_index];
  target->param_count = 0;
  memset(target->param_types, 0, sizeof(target->param_types));
  if (entry->kind == CC_SIGNATURE_ROLLBACK_PRESCAN_UNKNOWN) {
    memset(target->param_struct_indices, -1,
           sizeof(target->param_struct_indices));
    target->type = entry->prior_type;
    target->struct_index = entry->prior_struct_index;
    target->has_param_types = 0;
    target->function_signature_is_provisional = 0;
    target->is_variadic = 0;
  } else {
    memset(target->param_struct_indices, -1,
           sizeof(target->param_struct_indices));
    target->has_param_types = 0;
    target->is_variadic = 0;
  }
}

static int cc_begin_function_signature_transaction(cc_state_t *cc) {
  if (cc_signature_journal_owner) {
    cc_error(cc, "nested function-signature transaction");
    return 0;
  }
  cc_signature_journal_owner = cc;
  cc_signature_journal_count = 0;
  return 1;
}

static int cc_journal_function_signature(
    cc_state_t *cc, cc_symbol_t *target,
    cc_signature_rollback_kind_t kind) {
  int symbol_index;
  int journal_index;

  if (cc_signature_journal_owner != cc)
    return 1;
  symbol_index = (int)(target - cc->symbols);
  if (symbol_index < 0 || symbol_index >= cc->sym_count) {
    cc_error(cc, "function-signature transaction target is invalid");
    return 0;
  }
  for (journal_index = 0;
       journal_index < cc_signature_journal_count;
       journal_index++) {
    if (cc_signature_journal[journal_index].symbol_index == symbol_index)
      return 1;
  }
  if (cc_signature_journal_count >= CC_MAX_SYMBOLS) {
    cc_error(cc, "too many inferred function signatures");
    return 0;
  }
  cc_signature_journal[cc_signature_journal_count].symbol_index =
      symbol_index;
  cc_signature_journal[cc_signature_journal_count].kind = kind;
  cc_signature_journal[cc_signature_journal_count].prior_type =
      target->type;
  cc_signature_journal[cc_signature_journal_count].prior_struct_index =
      target->struct_index;
  cc_signature_journal_count++;
  return 1;
}

static void cc_finish_function_signature_transaction(cc_state_t *cc,
                                                     int rollback) {
  int journal_index;

  if (cc_signature_journal_owner != cc)
    return;
  if (rollback) {
    for (journal_index = cc_signature_journal_count - 1;
         journal_index >= 0; journal_index--) {
      cc_restore_signature_journal_entry(
          cc, &cc_signature_journal[journal_index]);
    }
  }
  cc_signature_journal_count = 0;
  cc_signature_journal_owner = NULL;
}

static void cc_seed_provisional_function_signature(
    cc_symbol_t *target, const cc_symbol_t *pointer) {
  target->type = pointer->function_pointer_return_type;
  target->struct_index = pointer->struct_index;
  target->param_count = pointer->param_count;
  memcpy(target->param_types, pointer->param_types,
         sizeof(target->param_types));
  memcpy(target->param_struct_indices, pointer->param_struct_indices,
         sizeof(target->param_struct_indices));
  target->has_param_types = pointer->has_param_types;
  target->is_variadic = pointer->is_variadic;
  target->function_signature_is_provisional = 1;
}

static int cc_check_function_pointer_value_compatibility(
    cc_state_t *cc, const cc_symbol_t *pointer,
    const cc_symbol_t *target, const char *result_diagnostic,
    const char *parameters_diagnostic) {
  int parameter_index;
  cc_type_t target_result_type;

  if (!pointer || !target)
    return 0;

  /* A prescan-only function has a placeholder result and no parameter
   * metadata. Retain this declaration as a provisional signature, then check
   * it when the real declaration or definition is parsed. */
  if (cc_function_signature_is_prescan_unknown(target))
    return 1;

  target_result_type = cc_function_pointer_designator_result_type(target);
  if (pointer->function_pointer_return_type != target_result_type ||
      (target_result_type == TYPE_STRUCT_PTR &&
       pointer->struct_index != target->struct_index)) {
    cc_error(cc, result_diagnostic);
    return 0;
  }

  if (target->kind == SYM_FUNC &&
      target->function_signature_is_provisional &&
      pointer->has_param_types && !target->has_param_types)
    return 1;

  /* An unprototyped destination deliberately erases fixed-parameter metadata.
   * Kernel bindings may also lack a source prototype. Other typed local
   * copies must not silently acquire a signature they did not carry. */
  if (!pointer->has_param_types)
    return 1;
  if (!target->has_param_types) {
    if (target->kind == SYM_KERNEL)
      return 1;
    cc_error(cc, parameters_diagnostic);
    return 0;
  }
  if (pointer->param_count != target->param_count ||
      pointer->is_variadic != target->is_variadic) {
    cc_error(cc, parameters_diagnostic);
    return 0;
  }
  for (parameter_index = 0; parameter_index < pointer->param_count;
       parameter_index++) {
    if (pointer->param_types[parameter_index] !=
            target->param_types[parameter_index] ||
        (pointer->param_types[parameter_index] == TYPE_STRUCT_PTR &&
         pointer->param_struct_indices[parameter_index] !=
             target->param_struct_indices[parameter_index])) {
      cc_error(cc, parameters_diagnostic);
      return 0;
    }
  }
  return 1;
}

static int cc_check_function_pointer_initializer(
    cc_state_t *cc, const cc_symbol_t *pointer,
    const cc_symbol_t *target) {
  return cc_check_function_pointer_value_compatibility(
      cc, pointer, target,
      "function-pointer initializer result does not match declaration",
      "function-pointer initializer parameters do not match declaration");
}

static int cc_apply_function_pointer_initializer(
    cc_state_t *cc, const cc_symbol_t *pointer, cc_symbol_t *target) {
  if (cc_function_signature_is_prescan_unknown(target)) {
    if (!cc_snapshot_program_symbol(cc, target))
      return 0;
    if (!cc_journal_function_signature(
            cc, target, CC_SIGNATURE_ROLLBACK_PRESCAN_UNKNOWN))
      return 0;
    cc_seed_provisional_function_signature(target, pointer);
    return 1;
  }
  if (target->kind == SYM_FUNC &&
      target->function_signature_is_provisional &&
      pointer->has_param_types && !target->has_param_types) {
    if (!cc_snapshot_program_symbol(cc, target))
      return 0;
    if (!cc_journal_function_signature(
            cc, target, CC_SIGNATURE_ROLLBACK_UNTYPED_PROVISIONAL))
      return 0;
    target->param_count = pointer->param_count;
    memcpy(target->param_types, pointer->param_types,
           sizeof(target->param_types));
    memcpy(target->param_struct_indices, pointer->param_struct_indices,
           sizeof(target->param_struct_indices));
    target->has_param_types = 1;
    target->is_variadic = pointer->is_variadic;
  }
  return 1;
}

static int cc_check_function_pointer_initializer_candidates(
    cc_state_t *cc, const cc_symbol_t *pointer,
    cc_symbol_t *const *targets, int target_count) {
  int target_index;

  if (target_count <= 0)
    return 0;
  for (target_index = 0; target_index < target_count; target_index++) {
    if (!cc_check_function_pointer_initializer(
            cc, pointer, targets[target_index]))
      return 0;
  }
  return 1;
}

static int cc_apply_function_pointer_initializer_candidates(
    cc_state_t *cc, const cc_symbol_t *pointer,
    cc_symbol_t *const *targets, int target_count) {
  int target_index;
  for (target_index = 0; target_index < target_count; target_index++) {
    if (!cc_apply_function_pointer_initializer(
            cc, pointer, targets[target_index]))
      return 0;
  }
  return 1;
}

static int cc_validate_function_pointer_argument_value(
    cc_state_t *cc, int function_pointer_signature_handle) {
  cc_function_pointer_signature_t signature;
  cc_symbol_t expected;
  int target_index;

  if (!cc_get_function_pointer_signature(
          cc, function_pointer_signature_handle, &signature))
    return 1;
  if (cc_last_expr_is_null_pointer_constant)
    return 1;
  if (cc_last_expr_function_signature_erased &&
      (cc_is_object_pointer_type(cc_last_expr_type) ||
       cc_last_expr_type == TYPE_FUNC_PTR))
    return 1;
  if (cc_last_expr_type != TYPE_FUNC_PTR ||
      cc_last_expr_function_signature_count <= 0) {
    cc_error(
        cc,
        "function-pointer argument requires a function, zero, or explicit pointer cast");
    return 0;
  }

  memset(&expected, 0, sizeof(expected));
  expected.type = TYPE_FUNC_PTR;
  if (!cc_copy_function_pointer_signature_handle(
          cc, function_pointer_signature_handle, &expected))
    return 1;

  for (target_index = 0;
       target_index < cc_last_expr_function_signature_count;
       target_index++) {
    if (!cc_check_function_pointer_value_compatibility(
            cc, &expected,
            cc_last_expr_function_signature_candidates[target_index],
            "function-pointer argument result does not match parameter type",
            "function-pointer argument parameters do not match parameter type"))
      return 0;
  }
  return cc_apply_function_pointer_initializer_candidates(
      cc, &expected, cc_last_expr_function_signature_candidates,
      cc_last_expr_function_signature_count);
}

static int cc_validate_function_pointer_initializer_value(
    cc_state_t *cc, const cc_symbol_t *pointer,
    cc_function_pointer_initializer_kind_t initializer_kind,
    cc_symbol_t *const *targets, int target_count) {
  if (initializer_kind == CC_FP_INITIALIZER_DESIGNATOR)
    return cc_check_function_pointer_initializer_candidates(
        cc, pointer, targets, target_count);
  if (initializer_kind == CC_FP_INITIALIZER_ZERO)
    return 1;
  if (initializer_kind == CC_FP_INITIALIZER_EXPLICIT_CAST &&
      (cc_is_object_pointer_type(cc_last_expr_type) ||
       cc_last_expr_type == TYPE_FUNC_PTR))
    return 1;
  cc_error(
      cc,
      "function-pointer initializer requires a function, zero, or explicit pointer cast");
  return 0;
}

static int cc_validate_function_pointer_assignment_value(
    cc_state_t *cc, const cc_symbol_t *pointer) {
  int target_index;

  if (cc_last_expr_is_null_pointer_constant)
    return 1;
  if (cc_last_expr_function_signature_erased &&
      (cc_is_object_pointer_type(cc_last_expr_type) ||
       cc_last_expr_type == TYPE_FUNC_PTR))
    return 1;
  if (cc_last_expr_type != TYPE_FUNC_PTR ||
      cc_last_expr_function_signature_count <= 0) {
    cc_error(
        cc,
        "function-pointer assignment requires a function, zero, or explicit pointer cast");
    return 0;
  }
  for (target_index = 0;
       target_index < cc_last_expr_function_signature_count;
       target_index++) {
    if (!cc_check_function_pointer_value_compatibility(
            cc, pointer,
            cc_last_expr_function_signature_candidates[target_index],
            "function-pointer assignment result does not match destination",
            "function-pointer assignment parameters do not match destination"))
      return 0;
  }
  return 1;
}

static int cc_parse_global_function_pointer_null_inner(
    cc_state_t *cc, int depth) {
  cc_token_t token;

  if (depth >= CC_MAX_PARAMS)
    return 0;
  token = cc_peek(cc);
  if (token.type == CC_TOK_NUMBER && token.int_value == 0) {
    cc_next(cc);
    return 1;
  }
  if (token.type != CC_TOK_LPAREN)
    return 0;

  cc_next(cc);
  if (cc_peek(cc).type == CC_TOK_VOID) {
    cc_next(cc);
    if (!cc_match(cc, CC_TOK_STAR) || !cc_match(cc, CC_TOK_RPAREN))
      return 0;
    return cc_parse_global_function_pointer_null_inner(cc, depth + 1);
  }
  if (!cc_parse_global_function_pointer_null_inner(cc, depth + 1) ||
      !cc_match(cc, CC_TOK_RPAREN))
    return 0;
  return 1;
}

static int cc_parse_global_function_pointer_initializer(
    cc_state_t *cc, cc_symbol_t *pointer, uint32_t data_offset) {
  cc_symbol_t *target = NULL;
  cc_function_pointer_initializer_kind_t initializer_kind =
      cc_probe_function_pointer_initializer(cc, &target);

  if (initializer_kind == CC_FP_INITIALIZER_DESIGNATOR && target &&
      (target->kind == SYM_FUNC || target->kind == SYM_KERNEL)) {
    int grouping_depth = 0;

    if (!cc_check_function_pointer_initializer(cc, pointer, target))
      return 0;

    while (cc_match(cc, CC_TOK_LPAREN))
      grouping_depth++;
    if (cc_next(cc).type != CC_TOK_IDENT) {
      cc_error(cc, "expected function name in global initializer");
      return 0;
    }
    while (grouping_depth > 0) {
      cc_expect(cc, CC_TOK_RPAREN);
      grouping_depth--;
    }
    if (cc->error)
      return 0;
    if (!cc_apply_function_pointer_initializer(cc, pointer, target))
      return 0;

    if (target->kind == SYM_KERNEL || target->is_defined) {
      uint32_t target_address =
          target->kind == SYM_KERNEL
              ? target->address
              : cc->code_base + (uint32_t)target->offset;
      return cc_patch_data32(cc, data_offset, target_address);
    }

    if (cc->patch_count >= CC_MAX_PATCHES) {
      cc_error(cc, "too many function address fixups");
      return 0;
    }
    {
      cc_patch_t *patch = &cc->patches[cc->patch_count++];
      int name_index = 0;
      patch->buffer_offset = data_offset;
      patch->kind = CC_PATCH_DATA_ABSOLUTE;
      while (target->name[name_index] && name_index < CC_MAX_IDENT - 1) {
        patch->name[name_index] = target->name[name_index];
        name_index++;
      }
      patch->name[name_index] = '\0';
    }
    return 1;
  }

  if (cc_parse_global_function_pointer_null_inner(cc, 0))
    return 1;
  cc_error(cc,
           "global function-pointer initializer requires a function or null");
  return 0;
}

static int cc_parse_function_pointer_local_initializer(
    cc_state_t *cc, cc_symbol_t *pointer, int32_t local_slot) {
  cc_symbol_t *initializer_targets[CC_MAX_PARAMS];
  int initializer_target_count = 0;

  if (!pointer || cc->error)
    return 0;
  if (!cc_match(cc, CC_TOK_EQ)) {
    emit_mov_eax_imm(cc, 0);
    emit_store_local(cc, local_slot);
    return !cc->error;
  }

  {
    cc_symbol_t *initializer_target;
    cc_function_pointer_initializer_kind_t initializer_kind;
    uint32_t initializer_code_pos = cc->code_pos;
    uint32_t initializer_data_pos = cc->data_pos;
    int initializer_patch_count = cc->patch_count;

    initializer_kind = cc_probe_function_pointer_initializer(
        cc, &initializer_target);
    cc_parse_expression(cc, 1);
    if (cc_last_expr_function_signature_erased) {
      initializer_kind = CC_FP_INITIALIZER_EXPLICIT_CAST;
      initializer_target = NULL;
    } else if (cc_last_expr_is_null_pointer_constant) {
      initializer_kind = CC_FP_INITIALIZER_ZERO;
      initializer_target = NULL;
    } else if (cc_last_expr_type == TYPE_FUNC_PTR &&
               cc_last_expr_function_signature_count > 0) {
      initializer_kind = CC_FP_INITIALIZER_DESIGNATOR;
      initializer_target = cc_last_expr_function_signature_sym;
    }
    if (initializer_kind == CC_FP_INITIALIZER_DESIGNATOR &&
        cc_last_expr_function_signature_count == 0 && initializer_target)
      cc_set_expr_function_signature(initializer_target);
    if (cc->error ||
        !cc_validate_function_pointer_initializer_value(
            cc, pointer, initializer_kind,
            cc_last_expr_function_signature_candidates,
            cc_last_expr_function_signature_count)) {
      cc->code_pos = initializer_code_pos;
      cc->data_pos = initializer_data_pos;
      cc->patch_count = initializer_patch_count;
      return 0;
    }
    if (initializer_kind == CC_FP_INITIALIZER_DESIGNATOR) {
      initializer_target_count = cc_last_expr_function_signature_count;
      memcpy(initializer_targets,
             cc_last_expr_function_signature_candidates,
             sizeof(*initializer_targets) *
                 (size_t)initializer_target_count);
    }
    emit_store_local(cc, local_slot);
  }

  if (initializer_target_count > 0 &&
      !cc_apply_function_pointer_initializer_candidates(
          cc, pointer, initializer_targets, initializer_target_count))
    return 0;
  return !cc->error;
}

static void cc_parse_simple_statement(cc_state_t *cc) {
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
    int function_pointer_return_struct_index = cc_last_type_struct_index;
    int function_pointer_return_array_count = cc_last_type_array_count;
    cc_skip_attributes(cc);

    /* Check for function pointer: type (*name)(params) */
    if (cc_peek(cc).type == CC_TOK_LPAREN) {
      cc_named_function_pointer_declarator_t declarator;
      int32_t local_slot;
      cc_symbol_t *sym;
      if (!cc_parse_named_function_pointer_declarator(
              cc, type, function_pointer_return_struct_index,
              function_pointer_return_array_count, &declarator))
        return;
      if (!cc_reserve_local_frame(cc, 4, 4, &local_slot))
        return;
      sym = cc_sym_add(cc, declarator.name.text, SYM_LOCAL, TYPE_FUNC_PTR);
      if (sym) {
        sym->offset = local_slot;
        cc_apply_named_function_pointer_declarator(sym, &declarator);
      }
      if (!cc_parse_function_pointer_local_initializer(cc, sym, local_slot))
        return;
      cc_expect(cc, CC_TOK_SEMICOLON);
      return;
    }

    cc_parse_declaration(cc, type);
    return;
  }

  switch (tok.type) {
  case CC_TOK_RETURN:
    cc_next(cc);
    cc_parse_return(cc);
    break;

  case CC_TOK_BREAK:
    cc_next(cc);
    if (cc->control_depth <= 0) {
      cc_error(cc, "break outside loop or switch");
    } else {
      int idx = cc->control_depth - 1;
      if (cc->control_kinds[idx] == CC_CONTROL_SWITCH)
        emit_add_esp(cc, 4);
      uint32_t patch = emit_jmp_placeholder(cc);
      if (cc->break_counts[idx] < CC_MAX_BREAKS_PER_CONTROL) {
        cc->break_patches[idx][cc->break_counts[idx]] = patch;
        cc->break_counts[idx]++;
      } else {
        cc_error(cc, "too many break statements in control construct");
      }
    }
    cc_expect(cc, CC_TOK_SEMICOLON);
    break;

  case CC_TOK_CONTINUE:
    cc_next(cc);
    {
      int idx = cc->control_depth - 1;
      int switch_count = 0;
      while (idx >= 0 && cc->control_kinds[idx] != CC_CONTROL_LOOP) {
        if (cc->control_kinds[idx] == CC_CONTROL_SWITCH)
          switch_count++;
        idx--;
      }
      if (idx < 0) {
        cc_error(cc, "continue outside loop");
      } else {
        uint32_t target = cc->continue_targets[idx];
        if (switch_count > 0)
          emit_add_esp(cc, switch_count * 4);
        emit8(cc, 0xE9);
        int32_t rel = (int32_t)(target - (cc->code_pos + 4));
        emit32(cc, (uint32_t)rel);
      }
    }
    cc_expect(cc, CC_TOK_SEMICOLON);
    break;

  case CC_TOK_GOTO: {
    cc_next(cc);
    cc_token_t label_tok = cc_next(cc);
    if (label_tok.type != CC_TOK_IDENT) {
      cc_error(cc, "expected label after goto");
      break;
    }
    cc_emit_goto(cc, label_tok.text);
    cc_expect(cc, CC_TOK_SEMICOLON);
    break;
  }

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

    /* Local label: name: */
    if (next.type == CC_TOK_COLON) {
      cc_next(cc);
      cc_define_label(cc, id.text);
    }
    /* Assignment */
    else if (cc_is_assignment_op(next.type)) {
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
      if (cc_is_simd_value_type(sym->type)) {
        cc_lexer_checkpoint_t checkpoint;
        cc_checkpoint_lexer(cc, &checkpoint);
        cc_next(cc); /* consume '.' */
        cc_token_t lane = cc_next(cc);
        cc_token_type_t lane_op = cc_peek(cc).type;
        cc_restore_lexer(cc, &checkpoint);
        if (lane.type == CC_TOK_IDENT &&
            (lane_op == CC_TOK_PLUSPLUS ||
             lane_op == CC_TOK_MINUSMINUS)) {
          cc_error(cc, "SIMD lane increment or decrement is not supported");
          break;
        }
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
          cc_symbol_t *method_sym = cc_sym_find(cc, method_sym_name);

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
            /* Track each stack width for layout and cleanup. */
            int arg_sizes[CC_MAX_PARAMS];
            int total_arg_bytes = 0;

            cc_last_expr_type = TYPE_STRUCT_PTR;
            if (!cc_emit_call_argument_push(cc, method_sym, argc,
                                            &arg_sizes[argc]))
              return;
            total_arg_bytes += arg_sizes[argc];
            argc++;

            if (cc_peek(cc).type != CC_TOK_RPAREN) {
              cc_parse_expression(cc, 1);
              if (argc < CC_MAX_PARAMS) {
                if (!cc_emit_call_argument_push(cc, method_sym, argc,
                                                &arg_sizes[argc]))
                  return;
                total_arg_bytes += arg_sizes[argc];
                argc++;
              }
              while (cc_match(cc, CC_TOK_COMMA)) {
                cc_parse_expression(cc, 1);
                if (argc >= CC_MAX_PARAMS) {
                  cc_error(cc, "too many call arguments");
                  break;
                }
                if (!cc_emit_call_argument_push(cc, method_sym, argc,
                                                &arg_sizes[argc]))
                  return;
                total_arg_bytes += arg_sizes[argc];
                argc++;
              }
            }
            cc_expect(cc, CC_TOK_RPAREN);

            if (!cc_emit_cdecl_argument_layout(cc, arg_sizes, argc))
              return;

            {
              cc_symbol_t *msym = method_sym;
              if (msym) {
                if (msym->kind == SYM_FUNC && msym->is_defined) {
                  emit_call_abs(cc, cc->code_base + (uint32_t)msym->offset);
                } else if (msym->kind == SYM_KERNEL) {
                  emit_call_abs(cc, msym->address);
                } else if (msym->kind == SYM_FUNC) {
                  uint32_t patch_pos = emit_call_rel_placeholder(cc);
                  if (cc->patch_count < CC_MAX_PATCHES) {
                    cc_patch_t *p = &cc->patches[cc->patch_count++];
                    p->buffer_offset = patch_pos;
                    p->kind = CC_PATCH_CODE_RELATIVE;
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
                    p->buffer_offset = patch_pos;
                    p->kind = CC_PATCH_CODE_RELATIVE;
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
      /* Traverse member and indexed-record chains once, retaining the final
       * slot address for the assignment below. */
      cc_type_t ftype = TYPE_INT;
      if (!cc_parse_member_lvalue_chain(cc, si, &ftype))
        break;

      /* Expect assignment operator */
      cc_token_t assign_op = cc_peek(cc);
      if (assign_op.type == CC_TOK_PLUSPLUS ||
          assign_op.type == CC_TOK_MINUSMINUS) {
        int decrement = cc_next(cc).type == CC_TOK_MINUSMINUS;
        if (ftype != TYPE_FLOAT && ftype != TYPE_DOUBLE) {
          cc_error(cc,
                   "indirect increment or decrement is not supported");
          break;
        }
        if (!cc_emit_indirect_scalar_load(cc, ftype))
          break;
        cc_last_expr_indirect_lvalue = 1;
        if (!cc_emit_indirect_fp_update(
                cc, ftype, decrement, 0))
          break;
        cc_expect(cc, CC_TOK_SEMICOLON);
        break;
      }
      if (!cc_is_assignment_op(assign_op.type)) {
        if (ftype != TYPE_STRUCT)
          (void)cc_emit_indirect_scalar_load(cc, ftype);
        cc_expect(cc, CC_TOK_SEMICOLON);
        break;
      }
      cc_next(cc);       /* consume assignment op */
      emit_push_eax(cc); /* save field address */
      (void)cc_finish_indirect_assignment(
          cc, ftype, assign_op.type,
          "bitwise or shift compound assignment requires an integer lvalue");
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
      cc_emit_variable_update(cc, sym, 0, 0);
      cc_expect(cc, CC_TOK_SEMICOLON);
    }
    /* Post-decrement */
    else if (next.type == CC_TOK_MINUSMINUS) {
      cc_next(cc);
      cc_symbol_t *sym = cc_sym_find(cc, id.text);
      cc_emit_variable_update(cc, sym, 1, 0);
      cc_expect(cc, CC_TOK_SEMICOLON);
    }
    /* Expression statement (function call, etc.) */
    else {
      /* We already consumed the identifier, so set it back as
       * current and parse as expression*/
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

  while (!cc->error && cc_statement_token_type(cc) != CC_TOK_RBRACE &&
         cc_statement_token_type(cc) != CC_TOK_EOF) {
    cc_parse_statement(cc);
  }

  cc_expect(cc, CC_TOK_RBRACE);

  /* Restore scope (pop local variables) */
  cc->sym_count = saved_scope;
  cc->local_offset = saved_offset;
}

/* Function Parsing */

static void cc_parse_function(cc_state_t *cc) {
  uint32_t function_code_checkpoint = cc->code_pos;
  uint32_t function_data_checkpoint = cc->data_pos;
  int function_patch_checkpoint = cc->patch_count;
  cc_type_t ret_type = cc_parse_type(cc);
  cc_type_t saved_return_type = cc->current_return_type;
  int ret_struct_index = cc_last_type_struct_index;
  int ret_array_count = cc_last_type_array_count;
  int symbol_count_before_registration;
  int saved_scope;
  uint32_t saved_entry_offset = cc->entry_offset;
  int saved_has_entry = cc->has_entry;
  int saved_label_count = cc->label_count;
  int saved_control_depth = cc->control_depth;
  int saved_statement_depth = cc->statement_depth;
  int had_prior_symbol = 0;
  int had_prior_signature = 0;
  int prior_signature_was_provisional = 0;
  int function_signature_transaction_active = 0;
  cc_symbol_t prior_function_symbol;
  if (ret_type == TYPE_STRUCT && ret_array_count == 0) {
    cc_error(cc, "struct return unsupported; use pointer-out parameter");
    return;
  }
  cc_skip_attributes(cc);

  cc_token_t name_tok = cc_next(cc);
  if (name_tok.type != CC_TOK_IDENT) {
    cc_error(cc, "expected function name");
    return;
  }

  /* Register function symbol */
  symbol_count_before_registration = cc->sym_count;
  cc_symbol_t *func_sym = cc_sym_find(cc, name_tok.text);
  if (func_sym && !cc_snapshot_program_symbol(cc, func_sym))
    return;
  if (func_sym) {
    prior_function_symbol = *func_sym;
    had_prior_symbol = 1;
  }
  if (!func_sym) {
    func_sym = cc_sym_add(cc, name_tok.text, SYM_FUNC, ret_type);
  }
  if (func_sym && func_sym->kind == SYM_FUNC &&
      (func_sym->function_signature_is_provisional ||
       func_sym->has_param_types)) {
    had_prior_signature = 1;
    prior_signature_was_provisional =
        func_sym->function_signature_is_provisional;
  }
  if (func_sym) {
    func_sym->kind = SYM_FUNC;
    func_sym->type = ret_type;
    func_sym->struct_index = ret_struct_index;
    func_sym->offset = (int32_t)cc->code_pos;
    func_sym->is_defined = 1;
    func_sym->has_param_types = 1;
    func_sym->function_signature_is_provisional = 0;
    func_sym->is_variadic = 0;
    memset(func_sym->param_types, 0, sizeof(func_sym->param_types));
    memset(func_sym->param_struct_indices, -1,
           sizeof(func_sym->param_struct_indices));
  }

  /* Is this main()? */
  if (strcmp(name_tok.text, "main") == 0) {
    cc->entry_offset = cc->code_pos;
    cc->has_entry = 1;
  }

  /* Save scope state */
  saved_scope = cc->sym_count;
  cc_expect(cc, CC_TOK_LPAREN);
  if (cc->error)
    goto function_failure;

  cc->local_offset = 0;
  cc->max_local_offset = 0;
  cc->param_count = 0;
  cc_xmm_reset();

  /* Parse parameters */
  if (cc_peek(cc).type != CC_TOK_RPAREN) {
    int param_offset = 8; /* first param at [ebp+8] */

    if (cc_peek(cc).type == CC_TOK_ELLIPSIS) {
      cc_next(cc); /* variadic-only parameter list */
      if (func_sym)
        func_sym->is_variadic = 1;
    } else {
      cc_type_t ptype = cc_parse_type(cc);
      int psi = cc_last_type_struct_index;
      int ptype_array_count = cc_last_type_array_count;
      int ptype_is_const = cc_last_type_is_const_qualified;
      int ptype_typedef_index = cc_last_type_typedef_index;

      /* Special-case: foo(void) */
      if (!(ptype == TYPE_VOID && cc_peek(cc).type == CC_TOK_RPAREN)) {
        cc_named_parameter_declarator_t parameter;
        if (!cc_parse_named_free_function_parameter(
                cc, ptype, psi, ptype_array_count, ptype_is_const,
                ptype_typedef_index, &parameter))
          goto function_failure;
        /* `T name[N]` decays to a pointer per C99 §6.7.5.3p7. Consume
         * the dimension (its value is irrelevant - we only track the
         * pointer type).*/
        ptype = parameter.type;
        psi = parameter.struct_index;
        ptype_is_const = parameter.is_const_qualified;
        ptype_typedef_index =
            parameter.function_pointer_signature_handle;
        if (cc->param_count >= CC_MAX_PARAMS) {
          cc_error(cc, "too many parameters");
          goto function_failure;
        }
        if (func_sym)
          func_sym->param_types[cc->param_count] = (uint8_t)ptype;
        if (func_sym && ptype == TYPE_STRUCT_PTR)
          func_sym->param_struct_indices[cc->param_count] = (int8_t)psi;
        if (func_sym && ptype == TYPE_FUNC_PTR &&
            ptype_typedef_index >= 0)
          func_sym->param_struct_indices[cc->param_count] =
              (int8_t)ptype_typedef_index;
        {
          int slot_size = cc_bind_cdecl_parameter(
              cc, parameter.name.text, ptype, psi, ptype_is_const,
              ptype_typedef_index, param_offset);
          if (slot_size == 0)
            goto function_failure;
          param_offset += slot_size;
        }
        cc->param_count++;
      }

      while (cc_match(cc, CC_TOK_COMMA)) {
        if (cc_peek(cc).type == CC_TOK_ELLIPSIS) {
          cc_next(cc); /* consume ... and finish param list */
          if (func_sym)
            func_sym->is_variadic = 1;
          break;
        }
        ptype = cc_parse_type(cc);
        psi = cc_last_type_struct_index;
        ptype_array_count = cc_last_type_array_count;
        ptype_is_const = cc_last_type_is_const_qualified;
        ptype_typedef_index = cc_last_type_typedef_index;
        cc_named_parameter_declarator_t parameter;
        if (!cc_parse_named_free_function_parameter(
                cc, ptype, psi, ptype_array_count, ptype_is_const,
                ptype_typedef_index, &parameter))
          goto function_failure;
        ptype = parameter.type;
        psi = parameter.struct_index;
        ptype_is_const = parameter.is_const_qualified;
        ptype_typedef_index =
            parameter.function_pointer_signature_handle;
        if (cc->param_count >= CC_MAX_PARAMS) {
          cc_error(cc, "too many parameters");
          goto function_failure;
        }
        if (func_sym)
          func_sym->param_types[cc->param_count] = (uint8_t)ptype;
        if (func_sym && ptype == TYPE_STRUCT_PTR)
          func_sym->param_struct_indices[cc->param_count] = (int8_t)psi;
        if (func_sym && ptype == TYPE_FUNC_PTR &&
            ptype_typedef_index >= 0)
          func_sym->param_struct_indices[cc->param_count] =
              (int8_t)ptype_typedef_index;
        {
          int slot_size = cc_bind_cdecl_parameter(
              cc, parameter.name.text, ptype, psi, ptype_is_const,
              ptype_typedef_index, param_offset);
          if (slot_size == 0)
            goto function_failure;
          param_offset += slot_size;
        }
        cc->param_count++;
      }
    }
  }

  cc_expect(cc, CC_TOK_RPAREN);
  if (cc->error)
    goto function_failure;

  if (func_sym) {
    func_sym->param_count = cc->param_count;
  }

  if (had_prior_signature) {
    int signature_matches =
        prior_function_symbol.type == ret_type &&
        (ret_type != TYPE_STRUCT_PTR ||
         prior_function_symbol.struct_index == ret_struct_index);
    if (signature_matches && prior_function_symbol.has_param_types) {
      signature_matches =
          func_sym && func_sym->has_param_types &&
          prior_function_symbol.param_count == func_sym->param_count &&
          prior_function_symbol.is_variadic == func_sym->is_variadic;
      for (int parameter_index = 0;
           signature_matches &&
           parameter_index < prior_function_symbol.param_count;
           parameter_index++) {
        signature_matches =
            prior_function_symbol.param_types[parameter_index] ==
                func_sym->param_types[parameter_index] &&
            (prior_function_symbol.param_types[parameter_index] !=
                 TYPE_STRUCT_PTR ||
             prior_function_symbol.param_struct_indices[parameter_index] ==
                  func_sym->param_struct_indices[parameter_index]) &&
            (prior_function_symbol.param_types[parameter_index] !=
                 TYPE_FUNC_PTR ||
             cc_function_pointer_signature_handles_match(
                 cc,
                 prior_function_symbol.param_struct_indices[
                     parameter_index],
                 func_sym->param_struct_indices[parameter_index]));
      }
    }
    if (!signature_matches) {
      cc_error(
          cc,
          prior_signature_was_provisional
              ? "function definition does not match prior function-pointer initializer"
              : "function declaration does not match prior declaration");
      goto function_failure;
    }
  }

  /* Forward function declaration: `T name(params);` with no body. The
   * symbol is registered with is_defined=0 so a later definition fills
   * in the offset, and use sites compile via the forward-reference
   * patch table (cc->patches). The cupidc prescan already discovers
   * top-level functions, but explicit forward decls let authors write
   * mutually-recursive helpers and split sigs from bodies.*/
  if (cc_peek(cc).type == CC_TOK_SEMICOLON) {
    cc_next(cc);
    if (func_sym) {
      if (had_prior_symbol &&
          (prior_function_symbol.kind == SYM_KERNEL ||
           prior_function_symbol.is_defined ||
           (prior_function_symbol.has_param_types &&
            !prior_function_symbol.function_signature_is_provisional))) {
        *func_sym = prior_function_symbol;
      } else {
        func_sym->is_defined = 0;
        func_sym->offset = 0;
      }
    }
    cc->sym_count = saved_scope;
    if (ret_array_count > 0) {
      cc_error(cc, "function return type cannot be an array");
      goto function_failure;
    }
    return;
  }

  if (had_prior_symbol && prior_function_symbol.is_defined) {
    cc_error(cc, "redefinition of function");
    goto function_failure;
  }

  if (!cc_begin_function_signature_transaction(cc))
    goto function_failure;
  function_signature_transaction_active = 1;

  cc_labels_reset(cc);
  cc->current_return_type = ret_type;

  /* Emit function prologue */
  emit_prologue(cc);

  /* Reserve space for locals (we'll patch this after parsing the body) */
  uint32_t sub_esp_pos = cc->code_pos;
  emit_sub_esp(cc, 256); /* placeholder - generous allocation */

  /* Parse body */
  cc_expect(cc, CC_TOK_LBRACE);
  if (cc->error)
    goto function_failure;

  while (!cc->error && cc_peek(cc).type != CC_TOK_RBRACE &&
         cc_peek(cc).type != CC_TOK_EOF) {
    cc_parse_statement(cc);
  }
  cc_expect(cc, CC_TOK_RBRACE);
  if (cc->error)
    goto function_failure;
  cc_resolve_labels(cc);
  if (cc->error)
    goto function_failure;

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
  cc->current_return_type = saved_return_type;
  /* Re-add function symbol (it was part of the saved scope) */
  if (func_sym) {
    cc_symbol_t *new_sym = cc_sym_add(cc, name_tok.text, SYM_FUNC, ret_type);
    if (new_sym) {
      new_sym->offset = func_sym->offset;
      new_sym->address = func_sym->address;
      new_sym->struct_index = func_sym->struct_index;
      new_sym->param_count = func_sym->param_count;
      memcpy(new_sym->param_types, func_sym->param_types,
             sizeof(new_sym->param_types));
      memcpy(new_sym->param_struct_indices, func_sym->param_struct_indices,
             sizeof(new_sym->param_struct_indices));
      new_sym->has_param_types = func_sym->has_param_types;
      new_sym->is_variadic = func_sym->is_variadic;
      new_sym->is_defined = 1;
    }
  }
  if (ret_array_count > 0) {
    cc_error(cc, "function return type cannot be an array");
    goto function_failure;
  }
  cc_finish_function_signature_transaction(cc, 0);
  function_signature_transaction_active = 0;
  return;

function_failure:
  if (function_signature_transaction_active)
    cc_finish_function_signature_transaction(cc, 1);
  if (had_prior_symbol && func_sym)
    *func_sym = prior_function_symbol;
  cc->code_pos = function_code_checkpoint;
  cc->data_pos = function_data_checkpoint;
  cc->patch_count = function_patch_checkpoint;
  cc->sym_count = had_prior_symbol ? saved_scope
                                   : symbol_count_before_registration;
  cc->entry_offset = saved_entry_offset;
  cc->has_entry = saved_has_entry;
  cc->label_count = saved_label_count;
  cc->control_depth = saved_control_depth;
  cc->statement_depth = saved_statement_depth;
  cc->local_offset = 0;
  cc->max_local_offset = 0;
  cc->param_count = 0;
  cc->current_return_type = saved_return_type;
}

static void cc_parse_class_method(cc_state_t *cc, int class_index,
                                  cc_type_t ret_type,
                                  const char *method_name) {
  uint32_t method_code_checkpoint = cc->code_pos;
  uint32_t method_data_checkpoint = cc->data_pos;
  int method_patch_checkpoint = cc->patch_count;
  int method_symbol_checkpoint = cc->sym_count;
  cc_type_t saved_return_type = cc->current_return_type;
  int saved_local_offset = cc->local_offset;
  int saved_max_local_offset = cc->max_local_offset;
  int saved_param_count = cc->param_count;
  int saved_scope_start = cc->scope_start;
  int saved_label_count = cc->label_count;
  int saved_control_depth = cc->control_depth;
  int saved_statement_depth = cc->statement_depth;
  int had_prior_symbol = 0;
  int signature_transaction_active = 0;
  cc_symbol_t prior_function_symbol;
  char full_name[CC_MAX_IDENT];
  cc_make_method_symbol(full_name, cc->structs[class_index].name, method_name);

  cc_symbol_t *func_sym = cc_sym_find(cc, full_name);
  if (func_sym && !cc_snapshot_program_symbol(cc, func_sym))
    return;
  if (func_sym) {
    prior_function_symbol = *func_sym;
    had_prior_symbol = 1;
  }
  if (!func_sym) {
    func_sym = cc_sym_add(cc, full_name, SYM_FUNC, ret_type);
  }
  if (func_sym) {
    func_sym->kind = SYM_FUNC;
    func_sym->type = ret_type;
    func_sym->offset = (int32_t)cc->code_pos;
    func_sym->is_defined = 1;
    func_sym->has_param_types = 1;
    func_sym->is_variadic = 0;
    memset(func_sym->param_types, 0, sizeof(func_sym->param_types));
    memset(func_sym->param_struct_indices, -1,
           sizeof(func_sym->param_struct_indices));
  }

  cc_expect(cc, CC_TOK_LPAREN);
  if (cc->error)
    goto method_failure;

  int saved_scope = cc->sym_count;
  cc->local_offset = 0;
  cc->max_local_offset = 0;
  cc->param_count = 0;
  cc_xmm_reset();

  /* Implicit self parameter at [ebp+8]. */
  {
    if (func_sym)
      func_sym->param_types[0] = (uint8_t)TYPE_STRUCT_PTR;
    if (func_sym)
      func_sym->param_struct_indices[0] = (int8_t)class_index;
    if (cc_bind_cdecl_parameter(cc, "self", TYPE_STRUCT_PTR, class_index, 0,
                                -1, 8) == 0)
      goto method_failure;
    cc->param_count = 1;
  }

  if (cc_peek(cc).type != CC_TOK_RPAREN) {
    int param_offset = 12; /* after implicit self */

    if (cc_peek(cc).type == CC_TOK_ELLIPSIS) {
      cc_next(cc);
      if (func_sym)
        func_sym->is_variadic = 1;
    } else {
      cc_type_t ptype = cc_parse_type(cc);
      int psi = cc_last_type_struct_index;
      int ptype_array_count = cc_last_type_array_count;
      int ptype_is_const = cc_last_type_is_const_qualified;
      int ptype_typedef_index = cc_last_type_typedef_index;

      if (!(ptype == TYPE_VOID && cc_peek(cc).type == CC_TOK_RPAREN)) {
        cc_token_t pname = cc_next(cc);
        if (pname.type != CC_TOK_IDENT) {
          cc_error(cc, "expected parameter name");
          goto method_failure;
        }
        ptype = cc_adjust_array_parameter_declarator(
            cc, ptype, ptype_array_count);
        if (cc->error)
          goto method_failure;
        if (cc->param_count >= CC_MAX_PARAMS) {
          cc_error(cc, "too many parameters");
          goto method_failure;
        }
        if (func_sym)
          func_sym->param_types[cc->param_count] = (uint8_t)ptype;
        if (func_sym && ptype == TYPE_STRUCT_PTR)
          func_sym->param_struct_indices[cc->param_count] = (int8_t)psi;
        if (func_sym && ptype == TYPE_FUNC_PTR &&
            ptype_typedef_index >= 0 &&
            ptype_typedef_index < cc->typedef_count &&
            cc->typedef_function_pointer_signature_valid[
                ptype_typedef_index])
          func_sym->param_struct_indices[cc->param_count] =
              (int8_t)ptype_typedef_index;
        {
          int slot_size = cc_bind_cdecl_parameter(
              cc, pname.text, ptype, psi, ptype_is_const,
              ptype_typedef_index, param_offset);
          if (slot_size == 0)
            goto method_failure;
          param_offset += slot_size;
        }
        cc->param_count++;
      }

      while (cc_match(cc, CC_TOK_COMMA)) {
        if (cc_peek(cc).type == CC_TOK_ELLIPSIS) {
          cc_next(cc);
          if (func_sym)
            func_sym->is_variadic = 1;
          break;
        }
        ptype = cc_parse_type(cc);
        psi = cc_last_type_struct_index;
        ptype_array_count = cc_last_type_array_count;
        ptype_is_const = cc_last_type_is_const_qualified;
        ptype_typedef_index = cc_last_type_typedef_index;
        cc_token_t pname = cc_next(cc);
        if (pname.type != CC_TOK_IDENT) {
          cc_error(cc, "expected parameter name");
          goto method_failure;
        }
        ptype = cc_adjust_array_parameter_declarator(
            cc, ptype, ptype_array_count);
        if (cc->error)
          goto method_failure;
        if (cc->param_count >= CC_MAX_PARAMS) {
          cc_error(cc, "too many parameters");
          goto method_failure;
        }
        if (func_sym)
          func_sym->param_types[cc->param_count] = (uint8_t)ptype;
        if (func_sym && ptype == TYPE_STRUCT_PTR)
          func_sym->param_struct_indices[cc->param_count] = (int8_t)psi;
        if (func_sym && ptype == TYPE_FUNC_PTR &&
            ptype_typedef_index >= 0 &&
            ptype_typedef_index < cc->typedef_count &&
            cc->typedef_function_pointer_signature_valid[
                ptype_typedef_index])
          func_sym->param_struct_indices[cc->param_count] =
              (int8_t)ptype_typedef_index;
        {
          int slot_size = cc_bind_cdecl_parameter(
              cc, pname.text, ptype, psi, ptype_is_const,
              ptype_typedef_index, param_offset);
          if (slot_size == 0)
            goto method_failure;
          param_offset += slot_size;
        }
        cc->param_count++;
      }
    }
  }

  cc_expect(cc, CC_TOK_RPAREN);
  if (cc->error)
    goto method_failure;

  if (func_sym) {
    func_sym->param_count = cc->param_count;
  }

  if (had_prior_symbol && prior_function_symbol.is_defined) {
    cc_error(cc, "redefinition of class method");
    goto method_failure;
  }
  if (!cc_begin_function_signature_transaction(cc))
    goto method_failure;
  signature_transaction_active = 1;

  cc->current_return_type = ret_type;
  emit_prologue(cc);
  {
    uint32_t sub_esp_pos = cc->code_pos;
    emit_sub_esp(cc, 256);

    cc_expect(cc, CC_TOK_LBRACE);
    if (cc->error)
      goto method_failure;
    while (!cc->error && cc_peek(cc).type != CC_TOK_RBRACE &&
           cc_peek(cc).type != CC_TOK_EOF) {
      cc_parse_statement(cc);
    }
    cc_expect(cc, CC_TOK_RBRACE);
    if (cc->error)
      goto method_failure;

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
  cc->current_return_type = saved_return_type;

  if (func_sym) {
    cc_symbol_t *new_sym = cc_sym_add(cc, full_name, SYM_FUNC, ret_type);
    if (new_sym) {
      new_sym->offset = func_sym->offset;
      new_sym->address = func_sym->address;
      new_sym->param_count = func_sym->param_count;
      memcpy(new_sym->param_types, func_sym->param_types,
             sizeof(new_sym->param_types));
      memcpy(new_sym->param_struct_indices, func_sym->param_struct_indices,
             sizeof(new_sym->param_struct_indices));
      new_sym->has_param_types = func_sym->has_param_types;
      new_sym->is_variadic = func_sym->is_variadic;
      new_sym->is_defined = 1;
    }
  }
  cc_finish_function_signature_transaction(cc, 0);
  return;

method_failure:
  if (signature_transaction_active)
    cc_finish_function_signature_transaction(cc, 1);
  if (had_prior_symbol && func_sym)
    *func_sym = prior_function_symbol;
  cc->code_pos = method_code_checkpoint;
  cc->data_pos = method_data_checkpoint;
  cc->patch_count = method_patch_checkpoint;
  cc->sym_count = method_symbol_checkpoint;
  cc->local_offset = saved_local_offset;
  cc->max_local_offset = saved_max_local_offset;
  cc->param_count = saved_param_count;
  cc->scope_start = saved_scope_start;
  cc->label_count = saved_label_count;
  cc->control_depth = saved_control_depth;
  cc->statement_depth = saved_statement_depth;
  cc->current_return_type = saved_return_type;
}

static void cc_apply_function_patch(cc_state_t *cc, const cc_patch_t *patch,
                                    uint32_t target) {
  if (patch->kind == CC_PATCH_DATA_ABSOLUTE) {
    (void)cc_patch_data32(cc, patch->buffer_offset, target);
    return;
  }
  if (patch->kind == CC_PATCH_CODE_ABSOLUTE) {
    patch32(cc, patch->buffer_offset, target);
  } else if (patch->kind == CC_PATCH_CODE_RELATIVE) {
    uint32_t from = cc->code_base + patch->buffer_offset + 4;
    int32_t relative = (int32_t)(target - from);
    patch32(cc, patch->buffer_offset, (uint32_t)relative);
  } else {
    cc_error(cc, "function patch has an invalid kind");
  }
}

/* Top-Level Program Parsing */

void cc_parse_program(cc_state_t *cc) {
  uint32_t program_code_checkpoint = cc->code_pos;
  uint32_t program_data_checkpoint = cc->data_pos;
  int program_patch_checkpoint = cc->patch_count;
  int program_symbol_checkpoint = cc->sym_count;
  int program_typedef_checkpoint = cc->typedef_count;
  int program_raw_function_pointer_signature_checkpoint =
      cc->raw_function_pointer_signature_count;
  uint32_t program_entry_checkpoint = cc->entry_offset;
  int program_has_entry_checkpoint = cc->has_entry;
  int program_local_offset_checkpoint = cc->local_offset;
  int program_max_local_offset_checkpoint = cc->max_local_offset;
  int program_param_count_checkpoint = cc->param_count;
  cc_type_t program_return_type_checkpoint = cc->current_return_type;
  int program_scope_start_checkpoint = cc->scope_start;
  int program_in_top_level_checkpoint = cc->in_top_level;
  int program_main_called_checkpoint = cc->main_called_top_level;
  int program_label_count_checkpoint = cc->label_count;
  int program_control_depth_checkpoint = cc->control_depth;
  int program_statement_depth_checkpoint = cc->statement_depth;
  cc->struct_count = 0;

  if (!cc_begin_program_symbol_transaction(
          cc, program_symbol_checkpoint))
    return;

  /* Pass 1: collect top-level function symbols for use-before-define. */
  cc_prescan_functions(cc);

  int has_top_level_statements = 0;
  int top_level_started = 0;
  uint32_t top_level_offset = 0;
  uint32_t top_level_sub_esp_pos = 0;
  uint32_t parse_iter = 0;
  /* Buffer of additional error messages so parser can keep reporting
   * after the first failure. Top-level recovery skips past the failing
   * declaration and resumes; cc->error gets cleared so subsequent
   * errors are not muted by the early-return in cc_error.*/
  int extra_errors = 0;

  while (cc_peek(cc).type != CC_TOK_EOF) {
    /* Top-level error recovery: skip the rest of the offending
     * declaration (until next ';' or matching '}') and resume parsing
     * so a single bad line doesn't hide the rest of the program.*/
    if (cc->error) {
      extra_errors = extra_errors + 1;
      int brace_depth = 0;
      while (cc_peek(cc).type != CC_TOK_EOF) {
        cc_token_t st = cc_peek(cc);
        if (st.type == CC_TOK_LBRACE) {
          brace_depth = brace_depth + 1;
          cc_next(cc);
          continue;
        }
        if (st.type == CC_TOK_RBRACE) {
          if (brace_depth > 0) {
            brace_depth = brace_depth - 1;
            cc_next(cc);
            if (brace_depth == 0) break;
            continue;
          }
          cc_next(cc);
          break;
        }
        if (st.type == CC_TOK_SEMICOLON && brace_depth == 0) {
          cc_next(cc);
          break;
        }
        cc_next(cc);
      }
      cc->error = 0;
      /* Bound recovery to a reasonable count so a corrupt file can't
       * hold the kernel parser hostage. After ~16 errors we stop and
       * surface the diagnostic; cc->error stays 1 so the caller bails.*/
      if (extra_errors > 16) {
        cc->error = 1;
        break;
      }
      continue;
    }
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
      int enum_is_unsigned = 0;
      int enum_next_overflow = 0;
      while (!cc->error && cc_peek(cc).type != CC_TOK_RBRACE &&
             cc_peek(cc).type != CC_TOK_EOF) {
        cc_token_t name_tok = cc_next(cc);
        if (name_tok.type != CC_TOK_IDENT) {
          cc_error(cc, "expected enum constant name");
          break;
        }
        /* Optional explicit value: NAME = value */
        if (cc_match(cc, CC_TOK_EQ)) {
          int32_t explicit_value;
          if (!cc_parse_const_int_expr(cc, &explicit_value)) {
            cc_error(cc, "expected integer in enum");
            break;
          }
          enum_val = explicit_value;
          enum_is_unsigned = cc_last_const_int_is_unsigned;
          enum_next_overflow = 0;
        } else if (enum_next_overflow) {
          cc_error(cc, "enum value overflow");
          break;
        }
        /* Register as global constant in data section */
        cc_symbol_t *gsym = cc_sym_add(
            cc, name_tok.text, SYM_GLOBAL,
            enum_is_unsigned ? TYPE_UINT : TYPE_INT);
        if (gsym) {
          if (!cc_data_reserve(cc, 4))
            break;
          gsym->address = cc->data_base + cc->data_pos;
          gsym->is_const_int = 1;
          gsym->const_int_value = enum_val;
          gsym->const_int_is_unsigned = enum_is_unsigned;
          memset(cc->data + cc->data_pos, 0, 4);
          uint32_t v = (uint32_t)enum_val;
          cc->data[cc->data_pos] = (uint8_t)(v & 0xFF);
          cc->data[cc->data_pos + 1] = (uint8_t)((v >> 8) & 0xFF);
          cc->data[cc->data_pos + 2] = (uint8_t)((v >> 16) & 0xFF);
          cc->data[cc->data_pos + 3] = (uint8_t)((v >> 24) & 0xFF);
          cc->data_pos += 4;
        }
        if (enum_is_unsigned) {
          enum_val = (int32_t)((uint32_t)enum_val + 1u);
          enum_next_overflow = 0;
        } else if (enum_val == 2147483647) {
          enum_next_overflow = 1;
        } else {
          enum_val++;
          enum_next_overflow = 0;
        }
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
      if (!cc_parse_typedef_declaration(cc, 1))
        break;
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
        int has_array_return_method = 0;

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
            int mret_array_count = cc_last_type_array_count;
            cc_token_t mname = cc_next(cc);
            if (mname.type != CC_TOK_IDENT) {
              cc_error(cc, "expected method name");
              break;
            }
            cc_parse_class_method(cc, sidx, mret, mname.text);
            if (cc->error)
              break;
            if (mret_array_count > 0)
              has_array_return_method = 1;
            continue;
          }

          if (sd->field_count >= CC_MAX_FIELDS) {
            cc_error(cc, "too many fields in class");
            break;
          }

          {
            cc_type_t ftype = cc_parse_type(cc);
            int fsi = cc_last_type_struct_index;
            int ftype_array_count = cc_last_type_array_count;
            if (cc->error)
              break;
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
            f->array_count = ftype_array_count;

            if (cc_peek(cc).type == CC_TOK_LBRACK) {
              if (ftype_array_count > 0) {
                cc_error(
                    cc,
                    "array declarator after typedef array is not supported");
                break;
              }
              cc_next(cc);
              int32_t array_count;
              if (!cc_parse_const_int_expr(cc, &array_count)) {
                cc_error(cc, "expected array size");
                break;
              }
              if (array_count <= 0) {
                cc_error(cc, "array size must be positive");
                break;
              }
              f->array_count = array_count;
              cc_expect(cc, CC_TOK_RBRACK);
            }

            if (f->array_count > 0 &&
                (ftype == TYPE_FLOAT4 || ftype == TYPE_DOUBLE2)) {
              cc_error(cc, "SIMD struct field arrays are not supported");
              break;
            }

            if (ftype == TYPE_STRUCT && !cc_struct_is_complete(cc, fsi)) {
              cc_error(cc, "field has incomplete struct type");
              break;
            }

            {
              int32_t elem_size = cc_type_size(cc, ftype, fsi);
              int32_t field_align = cc_type_align(cc, ftype, fsi);
              int32_t fsize = elem_size;
              if (f->array_count > 0 &&
                  !cc_checked_array_bytes(cc, f->array_count, elem_size,
                                          &fsize))
                break;

              int32_t next_field_offset;
              if (!cc_checked_record_field_layout(
                      cc, field_offset, fsize, field_align, &f->offset,
                      &next_field_offset))
                break;
              field_offset = next_field_offset;
              if (field_align > struct_align)
                struct_align = field_align;
            }

            cc_expect(cc, CC_TOK_SEMICOLON);
          }
        }

        if (cc->error)
          break;
        cc_expect(cc, CC_TOK_RBRACE);
        if (cc->error)
          break;
        cc_expect(cc, CC_TOK_SEMICOLON);
        if (cc->error)
          break;

        int32_t final_offset;
        int32_t final_size;
        if (!cc_checked_record_field_layout(
                cc, field_offset, 0, struct_align, &final_offset,
                &final_size))
          break;
        sd->align = struct_align;
        sd->total_size = final_size;
        sd->is_complete = 1;
        if (has_array_return_method) {
          cc_error(cc, "method return type cannot be an array");
          break;
        }
      }

      serial_printf("[cupidc] Defined class '%s': %d fields, %d bytes\n",
                    sd->name, sd->field_count, sd->total_size);
      continue;
    }

    /* Struct definition: struct Name { fields... }; */
    if (tok.type == CC_TOK_STRUCT) {
      /* Peek further: struct Name { -> definition, struct Name var -> decl */
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
          int ftype_array_count = cc_last_type_array_count;
          if (cc->error)
            break;
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
          f->array_count = ftype_array_count;

          /* Check for array field: name[N] */
          if (cc_peek(cc).type == CC_TOK_LBRACK) {
            if (ftype_array_count > 0) {
              cc_error(
                  cc,
                  "array declarator after typedef array is not supported");
              break;
            }
            cc_next(cc); /* consume '[' */
            int32_t array_count;
            if (!cc_parse_const_int_expr(cc, &array_count)) {
              cc_error(cc, "expected array size");
              break;
            }
            if (array_count <= 0) {
              cc_error(cc, "array size must be positive");
              break;
            }
            f->array_count = array_count;
            cc_expect(cc, CC_TOK_RBRACK);
          }

          if (f->array_count > 0 &&
              (ftype == TYPE_FLOAT4 || ftype == TYPE_DOUBLE2)) {
            cc_error(cc, "SIMD struct field arrays are not supported");
            break;
          }

          if (ftype == TYPE_STRUCT && !cc_struct_is_complete(cc, fsi)) {
            cc_error(cc, "field has incomplete struct type");
            break;
          }

          /* Compute field size/alignment with natural padding. */
          int32_t elem_size = cc_type_size(cc, ftype, fsi);
          int32_t field_align = cc_type_align(cc, ftype, fsi);
          int32_t fsize = elem_size;
          if (f->array_count > 0 &&
              !cc_checked_array_bytes(cc, f->array_count, elem_size, &fsize))
            break;

          int32_t next_field_offset;
          if (!cc_checked_record_field_layout(
                  cc, field_offset, fsize, field_align, &f->offset,
                  &next_field_offset))
            break;
          field_offset = next_field_offset;
          if (field_align > struct_align)
            struct_align = field_align;

          cc_expect(cc, CC_TOK_SEMICOLON);
        }
        if (cc->error)
          break;
        cc_expect(cc, CC_TOK_RBRACE);
        if (cc->error)
          break;
        cc_expect(cc, CC_TOK_SEMICOLON);
        if (cc->error)
          break;

        int32_t final_offset;
        int32_t final_size;
        if (!cc_checked_record_field_layout(
                cc, field_offset, 0, struct_align, &final_offset,
                &final_size))
          break;
        sd->align = struct_align;
        sd->total_size = final_size;
        sd->is_complete = 1;

        serial_printf("[cupidc] Defined struct '%s': %d fields, %d bytes\n",
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
       * a function return or global variable - handled below*/
    }

    if (cc_is_type_or_typedef(cc, tok)) {
      /* Could be function or global variable */
      /* Look ahead: type name ( -> function, type name ; -> global */
      /* Save lexer state */
      int saved_pos = cc->pos;
      int saved_line = cc->line;
      int saved_has_peek = cc->has_peek;
      cc_token_t saved_peek = cc->peek_buf;
      cc_token_t saved_cur = cc->cur;

      cc_type_t type = cc_parse_type(cc);
      cc_skip_attributes(cc);
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
         * into them as straight-line code.*/
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
        int gtype_array_count = cc_last_type_array_count;
        int gtype_is_const = cc_last_type_is_const_qualified;
        int gtype_typedef_index = cc_last_type_typedef_index;
        int has_raw_function_pointer_declarator = 0;
        cc_named_function_pointer_declarator_t raw_function_pointer;
        cc_token_t gname;
        cc_skip_attributes(cc);
        if (cc_peek(cc).type == CC_TOK_LPAREN) {
          if (!cc_parse_named_function_pointer_declarator(
                  cc, gtype, gtype_si, gtype_array_count,
                  &raw_function_pointer))
            break;
          has_raw_function_pointer_declarator = 1;
          gname = raw_function_pointer.name;
          gtype = TYPE_FUNC_PTR;
          gtype_si = raw_function_pointer.signature.return_struct_index;
          gtype_array_count = 0;
          gtype_typedef_index = -1;
        } else {
          gname = cc_next(cc);
          if (gname.type != CC_TOK_IDENT) {
            cc_error(cc, "expected variable name");
            break;
          }
        }

        /* Global array: type name[size]; or name[M][N]; */
        if (gtype_array_count > 0 ||
            cc_peek(cc).type == CC_TOK_LBRACK) {
          int uses_typedef_array = gtype_array_count > 0;
          if (uses_typedef_array &&
              cc_peek(cc).type == CC_TOK_LBRACK) {
            cc_error(cc,
                     "array declarator after typedef array is not supported");
            break;
          }
          if (!uses_typedef_array)
            cc_next(cc); /* consume '[' */
          int32_t arr_elems;
          if (uses_typedef_array) {
            arr_elems = gtype_array_count;
          } else {
            if (!cc_parse_const_int_expr(cc, &arr_elems)) {
              cc_error(cc, "expected array size");
              break;
            }
            if (arr_elems <= 0) {
              cc_error(cc, "array size must be positive");
              break;
            }
            cc_expect(cc, CC_TOK_RBRACK);
          }
          int32_t inner_dim = 0;
          int32_t inner_dim2 = 0;
          int has_inner_dim = 0;
          int has_inner_dim2 = 0;
          /* Check for 2D array */
          if (cc_peek(cc).type == CC_TOK_LBRACK) {
            has_inner_dim = 1;
            cc_next(cc); /* consume '[' */
            if (!cc_parse_const_int_expr(cc, &inner_dim)) {
              cc_error(cc, "expected array size");
              break;
            }
            if (inner_dim <= 0) {
              cc_error(cc, "array size must be positive");
              break;
            }
            cc_expect(cc, CC_TOK_RBRACK);
            /* Check for 3D array: type name[A][B][C]; */
            if (cc_peek(cc).type == CC_TOK_LBRACK) {
              has_inner_dim2 = 1;
              cc_next(cc); /* consume '[' */
              if (!cc_parse_const_int_expr(cc, &inner_dim2)) {
                cc_error(cc, "expected array size");
                break;
              }
              if (inner_dim2 <= 0) {
                cc_error(cc, "array size must be positive");
                break;
              }
              cc_expect(cc, CC_TOK_RBRACK);
            }
          }
          int32_t total_bytes;
          int aes;
          int dim2 = 0;
          cc_type_t arr_type;
          if (gtype == TYPE_STRUCT &&
              (has_inner_dim || has_inner_dim2)) {
            cc_error(cc, "struct arrays support one dimension");
            break;
          }
          if (gtype == TYPE_STRUCT && gtype_si >= 0 &&
              gtype_si < cc->struct_count) {
            if (!cc_struct_is_complete(cc, gtype_si)) {
              cc_error(cc, "array of incomplete struct type");
              break;
            }
            /* Array of structs */
            int32_t ssize = cc->structs[gtype_si].total_size;
            if (!cc_checked_array_bytes(cc, arr_elems, ssize, &total_bytes))
              break;
            aes = ssize;
            arr_type = TYPE_STRUCT_PTR;
          } else if (has_inner_dim2) {
            /* 3D array name[A][B][C]: outer stride = B*C*base; middle
             * stride = C*base; innermost element = base.*/
            int base_elem = cc_type_size(cc, gtype, gtype_si);
            int32_t middle_stride;
            int32_t row_size;
            if (!cc_checked_array_bytes(cc, inner_dim2, base_elem,
                                        &middle_stride) ||
                !cc_checked_array_bytes(cc, inner_dim, middle_stride,
                                        &row_size) ||
                !cc_checked_array_bytes(cc, arr_elems, row_size,
                                        &total_bytes))
              break;
            aes = row_size;
            dim2 = middle_stride;
            arr_type = cc_object_pointer_type(gtype);
          } else if (has_inner_dim) {
            /* 2D array */
            int base_elem = cc_type_size(cc, gtype, gtype_si);
            int32_t row_size;
            if (!cc_checked_array_bytes(cc, inner_dim, base_elem, &row_size) ||
                !cc_checked_array_bytes(cc, arr_elems, row_size,
                                        &total_bytes))
              break;
            aes = row_size;
            arr_type = cc_object_pointer_type(gtype);
          } else {
            /* 1D array */
            int elem_size = cc_type_size(cc, gtype, gtype_si);
            if (elem_size <= 0) {
              cc_error(cc, "invalid array element type");
              break;
            }
            if (!cc_checked_array_bytes(cc, arr_elems, elem_size,
                                        &total_bytes))
              break;
            aes = elem_size;
            arr_type = cc_object_pointer_type(gtype);
          }
          int32_t array_object_size = total_bytes;
          total_bytes = (total_bytes + 3) & ~3;
          cc_symbol_t *gsym = cc_sym_add(cc, gname.text, SYM_GLOBAL, arr_type);
          if (gsym) {
            if (!cc_data_reserve(cc, (uint32_t)total_bytes))
              break;
            gsym->address = cc->data_base + cc->data_pos;
            gsym->is_array = 1;
            gsym->is_const_qualified = gtype_is_const;
            gsym->struct_index = gtype_si;
            gsym->array_elem_size = aes;
            gsym->array_object_size = array_object_size;
            gsym->array_rank = 1 + has_inner_dim + has_inner_dim2;
            gsym->array_dim2 = dim2;
            gsym->array_elem_type = gtype;
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
            gsym->is_const_qualified = gtype_is_const;
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
        /* Global scalar or whole-vector variable */
        else {
          int32_t scalar_size = cc_type_size(cc, gtype, gtype_si);
          if (scalar_size <= 0 ||
              (scalar_size > 8 && !cc_is_simd_value_type(gtype))) {
            cc_error(cc, "global scalar type is not supported");
            break;
          }
          scalar_size = cc_align_up(scalar_size, 4);
          cc_symbol_t *gsym = cc_sym_add(cc, gname.text, SYM_GLOBAL, gtype);
          if (gsym) {
            if (!cc_data_reserve(cc, (uint32_t)scalar_size))
              break;
            gsym->address = cc->data_base + cc->data_pos;
            gsym->is_const_qualified = gtype_is_const;
            gsym->struct_index = gtype_si;
            if (has_raw_function_pointer_declarator)
              cc_apply_named_function_pointer_declarator(
                  gsym, &raw_function_pointer);
            else if (gtype == TYPE_FUNC_PTR)
              (void)cc_copy_function_pointer_typedef_signature(
                  cc, gtype_typedef_index, gsym);
            memset(cc->data + cc->data_pos, 0, (size_t)scalar_size);
            cc->data_pos += (uint32_t)scalar_size;

            /* Handle initializer: int x = 42; int y = -1; char *s = "hi"; */
            if (cc_match(cc, CC_TOK_EQ)) {
              uint32_t addr_off = gsym->address - cc->data_base;
              cc_token_t val;
              if (gtype == TYPE_FUNC_PTR) {
                if (!cc_parse_global_function_pointer_initializer(
                        cc, gsym, addr_off))
                  break;
                cc_expect(cc, CC_TOK_SEMICOLON);
                continue;
              }
              val = cc_next(cc);
              /* Handle negative initializer: -NUMBER */
              int negate = 0;
              if (val.type == CC_TOK_MINUS) {
                negate = 1;
                val = cc_next(cc);
              }
              if (gtype == TYPE_UINT && val.type == CC_TOK_FLIT) {
                uint32_t v =
                    cc_numeric_initializer_unsigned_value(val, negate);
                cc->data[addr_off] = (uint8_t)(v & 0xFF);
                cc->data[addr_off + 1] = (uint8_t)((v >> 8) & 0xFF);
                cc->data[addr_off + 2] = (uint8_t)((v >> 16) & 0xFF);
                cc->data[addr_off + 3] = (uint8_t)((v >> 24) & 0xFF);
              } else if ((gtype == TYPE_FLOAT || gtype == TYPE_DOUBLE) &&
                  (val.type == CC_TOK_NUMBER ||
                   val.type == CC_TOK_CHAR_LIT ||
                   val.type == CC_TOK_FLIT)) {
                double number = cc_numeric_initializer_value(val, negate);
                if (gtype == TYPE_FLOAT) {
                  float narrowed = (float)number;
                  memcpy(cc->data + addr_off, &narrowed, 4);
                } else {
                  memcpy(cc->data + addr_off, &number, 8);
                }
              } else if (val.type == CC_TOK_NUMBER ||
                         val.type == CC_TOK_CHAR_LIT) {
                int32_t sv = negate ? -val.int_value : val.int_value;
                uint32_t v = (uint32_t)sv;
                cc->data[addr_off] = (uint8_t)(v & 0xFF);
                cc->data[addr_off + 1] = (uint8_t)((v >> 8) & 0xFF);
                cc->data[addr_off + 2] = (uint8_t)((v >> 16) & 0xFF);
                cc->data[addr_off + 3] = (uint8_t)((v >> 24) & 0xFF);
              } else if (val.type == CC_TOK_STRING &&
                         gtype != TYPE_FLOAT && gtype != TYPE_DOUBLE) {
                /* Store string in data, save address at variable */
                uint32_t str_addr =
                    cc_emit_adjacent_string_literal(cc, val);
                if (str_addr == 0) {
                  break;
                }
                /* Align data_pos to 4 */
                cc->data_pos = (cc->data_pos + 3u) & ~3u;
                cc->data[addr_off] = (uint8_t)(str_addr & 0xFF);
                cc->data[addr_off + 1] = (uint8_t)((str_addr >> 8) & 0xFF);
                cc->data[addr_off + 2] = (uint8_t)((str_addr >> 16) & 0xFF);
                cc->data[addr_off + 3] = (uint8_t)((str_addr >> 24) & 0xFF);
              } else {
                cc_error(cc, "unsupported global scalar initializer");
                break;
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
        if (start_sym && !cc_snapshot_program_symbol(cc, start_sym))
          break;
        if (start_sym) {
          start_sym->kind = SYM_FUNC;
          start_sym->type = TYPE_VOID;
          start_sym->offset = (int32_t)cc->code_pos;
          start_sym->is_defined = 1;
          start_sym->param_count = 0;
          memset(start_sym->param_types, 0,
                 sizeof(start_sym->param_types));
          memset(start_sym->param_struct_indices, -1,
                 sizeof(start_sym->param_struct_indices));
          start_sym->has_param_types = 1;
          start_sym->function_signature_is_provisional = 0;
          start_sym->is_variadic = 0;
        }

        top_level_offset = cc->code_pos;
        cc->local_offset = 0;
        cc->max_local_offset = 0;
        cc->param_count = 0;
        cc_xmm_reset();
        cc_labels_reset(cc);

        emit_prologue(cc);
        top_level_sub_esp_pos = cc->code_pos;
        emit_sub_esp(cc, 256); /* placeholder, patched at end */

        top_level_started = 1;
      }

      has_top_level_statements = 1;
      cc->in_top_level = 1;
      cc_parse_statement(cc);
      cc->in_top_level = 0;
    }
  }

  /* If recovery accumulated errors, surface that to the caller. */
  if (extra_errors > 0) {
    cc->error = 1;
    serial_printf("[cupidc] %d additional error(s) suppressed during recovery\n",
                  extra_errors);
  }

  if (!cc->error && top_level_started) {
    cc_resolve_labels(cc);

    /* If main() exists and the user did NOT already invoke it from a
     * top-level statement, run it after top-level for legacy programs that
     * defined main but didn't call it. Skipping when the user *did* call
     * main themselves prevents the body from running twice.*/
    cc_symbol_t *main_sym = cc_sym_find(cc, "main");
    if (main_sym && main_sym->kind == SYM_FUNC && main_sym->is_defined &&
        !cc->main_called_top_level) {
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

  /* A failed source must not revisit or rewrite committed patch sites. */
  for (int i = program_patch_checkpoint;
       !cc->error && i < cc->patch_count; i++) {
    cc_patch_t *p = &cc->patches[i];
    cc_symbol_t *sym = cc_sym_find(cc, p->name);
    if (sym && sym->kind == SYM_FUNC && sym->is_defined) {
      uint32_t target = cc->code_base + (uint32_t)sym->offset;
      cc_apply_function_patch(cc, p, target);
    } else if (sym && sym->kind == SYM_KERNEL) {
      uint32_t target = sym->address;
      cc_apply_function_patch(cc, p, target);
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
  if (cc->error) {
    cc_finish_program_symbol_transaction(cc, 1);
    cc->code_pos = program_code_checkpoint;
    cc->data_pos = program_data_checkpoint;
    cc->patch_count = program_patch_checkpoint;
    cc->sym_count = program_symbol_checkpoint;
    cc->typedef_count = program_typedef_checkpoint;
    cc->raw_function_pointer_signature_count =
        program_raw_function_pointer_signature_checkpoint;
    cc->entry_offset = program_entry_checkpoint;
    cc->has_entry = program_has_entry_checkpoint;
    cc->local_offset = program_local_offset_checkpoint;
    cc->max_local_offset = program_max_local_offset_checkpoint;
    cc->param_count = program_param_count_checkpoint;
    cc->current_return_type = program_return_type_checkpoint;
    cc->scope_start = program_scope_start_checkpoint;
    cc->in_top_level = program_in_top_level_checkpoint;
    cc->main_called_top_level = program_main_called_checkpoint;
    cc->label_count = program_label_count_checkpoint;
    cc->control_depth = program_control_depth_checkpoint;
    cc->statement_depth = program_statement_depth_checkpoint;
  } else {
    cc_finish_program_symbol_transaction(cc, 0);
  }
}

/*  *  REPL Line Parsing - TempleOS-style direct statement compilation
 **/

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
        p->buffer_offset = patch_pos;
        p->kind = CC_PATCH_CODE_RELATIVE;
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

void cc_repl_checkpoint_structs(repl_state_t *state) {
  if (!state || !state->cc)
    return;

  state->struct_committed = state->cc->struct_count;
  memcpy(state->structs_committed, state->cc->structs,
         sizeof(state->structs_committed));
}

void cc_repl_restore_structs(repl_state_t *state) {
  if (!state || !state->cc)
    return;

  memcpy(state->cc->structs, state->structs_committed,
         sizeof(state->structs_committed));
  state->cc->struct_count = state->struct_committed;
}

void cc_parse_repl_line(cc_state_t *cc, int *is_expr) {
  *is_expr = 0;

  if (cc->error || cc_peek(cc).type == CC_TOK_EOF)
    return;

  cc_token_t tok = cc_peek(cc);

  /* Static qualifier */
  if (tok.type == CC_TOK_STATIC) {
    cc_next(cc);
    tok = cc_peek(cc);
  }

  /* Enum definition: enum { A, B = 5, C }; */
  if (tok.type == CC_TOK_ENUM) {
    cc_next(cc);
    if (cc_peek(cc).type == CC_TOK_IDENT)
      cc_next(cc);
    cc_expect(cc, CC_TOK_LBRACE);
    int32_t enum_val = 0;
    int enum_is_unsigned = 0;
    int enum_next_overflow = 0;
    while (!cc->error && cc_peek(cc).type != CC_TOK_RBRACE &&
           cc_peek(cc).type != CC_TOK_EOF) {
      cc_token_t name_tok = cc_next(cc);
      if (name_tok.type != CC_TOK_IDENT) {
        cc_error(cc, "expected enum constant name");
        return;
      }
      if (cc_match(cc, CC_TOK_EQ)) {
        int32_t explicit_value;
        if (!cc_parse_const_int_expr(cc, &explicit_value)) {
          cc_error(cc, "expected integer in enum");
          return;
        }
        enum_val = explicit_value;
        enum_is_unsigned = cc_last_const_int_is_unsigned;
        enum_next_overflow = 0;
      } else if (enum_next_overflow) {
        cc_error(cc, "enum value overflow");
        return;
      }
      cc_symbol_t *gsym = cc_sym_add(
          cc, name_tok.text, SYM_GLOBAL,
          enum_is_unsigned ? TYPE_UINT : TYPE_INT);
      if (gsym) {
        if (!cc_data_reserve(cc, 4))
          return;
        gsym->address = cc->data_base + cc->data_pos;
        gsym->is_const_int = 1;
        gsym->const_int_value = enum_val;
        gsym->const_int_is_unsigned = enum_is_unsigned;
        memset(cc->data + cc->data_pos, 0, 4);
        uint32_t v = (uint32_t)enum_val;
        cc->data[cc->data_pos] = (uint8_t)(v & 0xFF);
        cc->data[cc->data_pos + 1] = (uint8_t)((v >> 8) & 0xFF);
        cc->data[cc->data_pos + 2] = (uint8_t)((v >> 16) & 0xFF);
        cc->data[cc->data_pos + 3] = (uint8_t)((v >> 24) & 0xFF);
        cc->data_pos += 4;
      }
      if (enum_is_unsigned) {
        enum_val = (int32_t)((uint32_t)enum_val + 1u);
        enum_next_overflow = 0;
      } else if (enum_val == 2147483647) {
        enum_next_overflow = 1;
      } else {
        enum_val++;
        enum_next_overflow = 0;
      }
      if (cc_peek(cc).type != CC_TOK_RBRACE)
        cc_expect(cc, CC_TOK_COMMA);
    }
    cc_expect(cc, CC_TOK_RBRACE);
    if (cc_peek(cc).type == CC_TOK_SEMICOLON)
      cc_next(cc);
    return;
  }

  /* Typedef: typedef <type> <alias>; */
  if (tok.type == CC_TOK_TYPEDEF) {
    cc_next(cc);
    (void)cc_parse_typedef_declaration(cc, 0);
    return;
  }

  /* Struct definition: struct Name { fields... }; */
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
        int ftype_array_count = cc_last_type_array_count;
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
        f->array_count = ftype_array_count;
        if (cc_peek(cc).type == CC_TOK_LBRACK) {
          if (ftype_array_count > 0) {
            cc_error(
                cc,
                "array declarator after typedef array is not supported");
            return;
          }
          cc_next(cc);
          int32_t array_count;
          if (!cc_parse_const_int_expr(cc, &array_count)) {
            cc_error(cc, "expected array size");
            return;
          }
          if (array_count <= 0) {
            cc_error(cc, "array size must be positive");
            return;
          }
          f->array_count = array_count;
          cc_expect(cc, CC_TOK_RBRACK);
        }
        if (ftype == TYPE_STRUCT && !cc_struct_is_complete(cc, fsi)) {
          cc_error(cc, "field has incomplete struct type");
          return;
        }
        if (f->array_count > 0 &&
            (ftype == TYPE_FLOAT4 || ftype == TYPE_DOUBLE2)) {
          cc_error(cc, "SIMD struct field arrays are not supported");
          return;
        }
        int32_t elem_size = cc_type_size(cc, ftype, fsi);
        int32_t field_align_val = cc_type_align(cc, ftype, fsi);
        int32_t fsize = elem_size;
        if (f->array_count > 0 &&
            !cc_checked_array_bytes(cc, f->array_count, elem_size, &fsize))
          return;
        int32_t next_field_offset;
        if (!cc_checked_record_field_layout(
                cc, field_offset, fsize, field_align_val, &f->offset,
                &next_field_offset))
          return;
        field_offset = next_field_offset;
        if (field_align_val > struct_align)
          struct_align = field_align_val;
        cc_expect(cc, CC_TOK_SEMICOLON);
      }
      cc_expect(cc, CC_TOK_RBRACE);
      if (cc_peek(cc).type == CC_TOK_SEMICOLON)
        cc_next(cc);
      int32_t final_offset;
      int32_t final_size;
      if (!cc_checked_record_field_layout(cc, field_offset, 0,
                                          struct_align, &final_offset,
                                          &final_size))
        return;
      sd->align = struct_align;
      sd->total_size = final_size;
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
    /* Fall through - struct used as type for variable or function */
  }

  /* Check if line starts with a type (function def or global var) */
  if (cc_is_type_or_typedef(cc, tok)) {
    int saved_pos = cc->pos;
    int saved_line = cc->line;
    int saved_has_peek = cc->has_peek;
    cc_token_t saved_peek = cc->peek_buf;
    cc_token_t saved_cur = cc->cur;

    cc_type_t type = cc_parse_type(cc);
    cc_skip_attributes(cc);
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
          cc_apply_function_patch(cc, p, target);
        } else if (sym && sym->kind == SYM_KERNEL) {
          uint32_t target = sym->address;
          cc_apply_function_patch(cc, p, target);
        }
      }
      return;
    }

    (void)type;
    (void)name_tok;
    cc_type_t gtype = cc_parse_type(cc);
    int gtype_si = cc_last_type_struct_index;
    int gtype_array_count = cc_last_type_array_count;
    int gtype_is_const = cc_last_type_is_const_qualified;
    int gtype_typedef_index = cc_last_type_typedef_index;
    int has_raw_function_pointer_declarator = 0;
    cc_named_function_pointer_declarator_t raw_function_pointer;
    cc_token_t gname;
    cc_skip_attributes(cc);
    if (cc_peek(cc).type == CC_TOK_LPAREN) {
      if (!cc_parse_named_function_pointer_declarator(
              cc, gtype, gtype_si, gtype_array_count,
              &raw_function_pointer))
        return;
      has_raw_function_pointer_declarator = 1;
      gname = raw_function_pointer.name;
      gtype = TYPE_FUNC_PTR;
      gtype_si = raw_function_pointer.signature.return_struct_index;
      gtype_array_count = 0;
      gtype_typedef_index = -1;
    } else {
      gname = cc_next(cc);
      if (gname.type != CC_TOK_IDENT) {
        cc_error(cc, "expected variable name");
        return;
      }
    }

    if (gtype_array_count > 0 || cc_peek(cc).type == CC_TOK_LBRACK) {
      int uses_typedef_array = gtype_array_count > 0;
      if (uses_typedef_array && cc_peek(cc).type == CC_TOK_LBRACK) {
        cc_error(cc,
                 "array declarator after typedef array is not supported");
        return;
      }
      if (!uses_typedef_array)
        cc_next(cc);
      int32_t arr_elems;
      if (uses_typedef_array) {
        arr_elems = gtype_array_count;
      } else {
        if (!cc_parse_const_int_expr(cc, &arr_elems)) {
          cc_error(cc, "expected array size");
          return;
        }
        if (arr_elems <= 0) {
          cc_error(cc, "array size must be positive");
          return;
        }
        cc_expect(cc, CC_TOK_RBRACK);
      }
      int32_t inner_dim = 0;
      int32_t inner_dim2 = 0;
      int has_inner_dim = 0;
      int has_inner_dim2 = 0;
      if (cc_peek(cc).type == CC_TOK_LBRACK) {
        has_inner_dim = 1;
        cc_next(cc);
        if (!cc_parse_const_int_expr(cc, &inner_dim)) {
          cc_error(cc, "expected array size");
          return;
        }
        if (inner_dim <= 0) {
          cc_error(cc, "array size must be positive");
          return;
        }
        cc_expect(cc, CC_TOK_RBRACK);
        if (cc_peek(cc).type == CC_TOK_LBRACK) {
          has_inner_dim2 = 1;
          cc_next(cc);
          if (!cc_parse_const_int_expr(cc, &inner_dim2)) {
            cc_error(cc, "expected array size");
            return;
          }
          if (inner_dim2 <= 0) {
            cc_error(cc, "array size must be positive");
            return;
          }
          cc_expect(cc, CC_TOK_RBRACK);
        }
      }
      if (gtype == TYPE_STRUCT && (has_inner_dim || has_inner_dim2)) {
        cc_error(cc, "struct arrays support one dimension");
        return;
      }
      int32_t total_bytes;
      int32_t elem_size;
      int32_t dim2 = 0;
      cc_type_t arr_type;
      if (gtype == TYPE_STRUCT && gtype_si >= 0 && gtype_si < cc->struct_count) {
        elem_size = cc->structs[gtype_si].total_size;
        arr_type = TYPE_STRUCT_PTR;
        if (!cc_checked_array_bytes(cc, arr_elems, elem_size, &total_bytes))
          return;
      } else if (has_inner_dim2) {
        int32_t base_elem = cc_type_size(cc, gtype, gtype_si);
        int32_t middle_stride;
        if (!cc_checked_array_bytes(cc, inner_dim2, base_elem,
                                    &middle_stride) ||
            !cc_checked_array_bytes(cc, inner_dim, middle_stride,
                                    &elem_size) ||
            !cc_checked_array_bytes(cc, arr_elems, elem_size, &total_bytes))
          return;
        dim2 = middle_stride;
        arr_type = cc_object_pointer_type(gtype);
      } else if (has_inner_dim) {
        int32_t base_elem = cc_type_size(cc, gtype, gtype_si);
        if (!cc_checked_array_bytes(cc, inner_dim, base_elem, &elem_size) ||
            !cc_checked_array_bytes(cc, arr_elems, elem_size, &total_bytes))
          return;
        arr_type = cc_object_pointer_type(gtype);
      } else {
        elem_size = cc_type_size(cc, gtype, gtype_si);
        if (elem_size <= 0) {
          cc_error(cc, "invalid array element type");
          return;
        }
        if (!cc_checked_array_bytes(cc, arr_elems, elem_size, &total_bytes))
          return;
        arr_type = cc_object_pointer_type(gtype);
      }
      int32_t array_object_size = total_bytes;
      total_bytes = (total_bytes + 3) & ~3;
      cc_symbol_t *gsym = cc_sym_add(cc, gname.text, SYM_GLOBAL, arr_type);
      if (gsym) {
        if (!cc_data_reserve(cc, (uint32_t)total_bytes))
          return;
        gsym->address = cc->data_base + cc->data_pos;
        gsym->is_array = 1;
        gsym->is_const_qualified = gtype_is_const;
        gsym->struct_index = gtype_si;
        gsym->array_elem_size = elem_size;
        gsym->array_object_size = array_object_size;
        gsym->array_rank = 1 + has_inner_dim + has_inner_dim2;
        gsym->array_dim2 = dim2;
        gsym->array_elem_type = gtype;
        memset(cc->data + cc->data_pos, 0, (size_t)total_bytes);
        cc->data_pos += (uint32_t)total_bytes;
      }
      cc_expect(cc, CC_TOK_SEMICOLON);
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
        if (!cc_data_reserve(cc, (uint32_t)alloc_size))
          return;
        gsym->address = cc->data_base + cc->data_pos;
        gsym->is_const_qualified = gtype_is_const;
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
      int32_t scalar_size = cc_type_size(cc, gtype, gtype_si);
      if (scalar_size <= 0 ||
          (scalar_size > 8 && !cc_is_simd_value_type(gtype))) {
        cc_error(cc, "global scalar type is not supported");
        return;
      }
      scalar_size = cc_align_up(scalar_size, 4);
      cc_symbol_t *gsym = cc_sym_add(cc, gname.text, SYM_GLOBAL, gtype);
      if (gsym) {
        if (!cc_data_reserve(cc, (uint32_t)scalar_size))
          return;
        gsym->address = cc->data_base + cc->data_pos;
        gsym->is_const_qualified = gtype_is_const;
        gsym->struct_index = gtype_si;
        if (has_raw_function_pointer_declarator)
          cc_apply_named_function_pointer_declarator(
              gsym, &raw_function_pointer);
        else if (gtype == TYPE_FUNC_PTR)
          (void)cc_copy_function_pointer_typedef_signature(
              cc, gtype_typedef_index, gsym);
        memset(cc->data + cc->data_pos, 0, (size_t)scalar_size);
        cc->data_pos += (uint32_t)scalar_size;

        if (cc_match(cc, CC_TOK_EQ)) {
          uint32_t addr_off = gsym->address - cc->data_base;
          cc_token_t val;
          if (gtype == TYPE_FUNC_PTR) {
            if (!cc_parse_global_function_pointer_initializer(
                    cc, gsym, addr_off))
              return;
            if (cc_peek(cc).type == CC_TOK_SEMICOLON)
              cc_next(cc);
            return;
          }
          val = cc_next(cc);
          int negate = 0;
          if (val.type == CC_TOK_MINUS) {
            negate = 1;
            val = cc_next(cc);
          }
          if (gtype == TYPE_UINT && val.type == CC_TOK_FLIT) {
            uint32_t v =
                cc_numeric_initializer_unsigned_value(val, negate);
            cc->data[addr_off] = (uint8_t)(v & 0xFF);
            cc->data[addr_off + 1] = (uint8_t)((v >> 8) & 0xFF);
            cc->data[addr_off + 2] = (uint8_t)((v >> 16) & 0xFF);
            cc->data[addr_off + 3] = (uint8_t)((v >> 24) & 0xFF);
          } else if ((gtype == TYPE_FLOAT || gtype == TYPE_DOUBLE) &&
              (val.type == CC_TOK_NUMBER || val.type == CC_TOK_CHAR_LIT ||
               val.type == CC_TOK_FLIT)) {
            double number = cc_numeric_initializer_value(val, negate);
            if (gtype == TYPE_FLOAT) {
              float narrowed = (float)number;
              memcpy(cc->data + addr_off, &narrowed, 4);
            } else {
              memcpy(cc->data + addr_off, &number, 8);
            }
          } else if (val.type == CC_TOK_NUMBER ||
                     val.type == CC_TOK_CHAR_LIT) {
            int32_t sv = negate ? -val.int_value : val.int_value;
            uint32_t v = (uint32_t)sv;
            cc->data[addr_off] = (uint8_t)(v & 0xFF);
            cc->data[addr_off + 1] = (uint8_t)((v >> 8) & 0xFF);
            cc->data[addr_off + 2] = (uint8_t)((v >> 16) & 0xFF);
            cc->data[addr_off + 3] = (uint8_t)((v >> 24) & 0xFF);
          } else if (val.type == CC_TOK_STRING &&
                     gtype != TYPE_FLOAT && gtype != TYPE_DOUBLE) {
            uint32_t str_addr = cc_emit_adjacent_string_literal(cc, val);
            if (str_addr == 0)
              return;
            cc->data_pos = (cc->data_pos + 3u) & ~3u;
            cc->data[addr_off] = (uint8_t)(str_addr & 0xFF);
            cc->data[addr_off + 1] = (uint8_t)((str_addr >> 8) & 0xFF);
            cc->data[addr_off + 2] = (uint8_t)((str_addr >> 16) & 0xFF);
            cc->data[addr_off + 3] = (uint8_t)((str_addr >> 24) & 0xFF);
          } else {
            cc_error(cc, "unsupported global scalar initializer");
            return;
          }
        }
      }
      if (cc_peek(cc).type == CC_TOK_SEMICOLON)
        cc_next(cc);
      return;
    }
  }

  /* Statement / Expression: emit executable code */
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
        cc_apply_function_patch(cc, p, target);
      } else if (sym && sym->kind == SYM_KERNEL) {
        uint32_t target = sym->address;
        cc_apply_function_patch(cc, p, target);
      }
    }
  }
}
