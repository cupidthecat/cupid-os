#include "ctool.h"
#include "ctool_host.h"
#include "x86.h"

#include <stdio.h>
#include <string.h>

static int check_status(ctool_status_t actual, ctool_status_t expected,
                        const char *operation) {
  if (actual != expected) {
    (void)fprintf(stderr, "%s: expected %s, got %s\n", operation,
                  ctool_status_name(expected), ctool_status_name(actual));
    return 0;
  }
  return 1;
}

static int check_true(int condition, const char *operation) {
  if (!condition) {
    (void)fprintf(stderr, "%s: contract check failed\n", operation);
    return 0;
  }
  return 1;
}

static int open_job(ctool_host_adapter_t *adapter, ctool_job_t **job) {
  ctool_job_config_t config;
  ctool_status_t status = ctool_host_adapter_init(adapter, ".");
  if (!check_status(status, CTOOL_OK, "host adapter init")) {
    return 0;
  }
  config = ctool_host_job_config(adapter, ctool_default_limits());
  status = ctool_job_open(&config, job);
  return check_status(status, CTOOL_OK, "job open");
}

static ctool_x86_reg_t reg(ctool_x86_reg_class_t class_id, ctool_u8 index) {
  ctool_x86_reg_t result;
  result.class_id = class_id;
  result.index = index;
  return result;
}

static ctool_x86_value_t constant(ctool_u32 bits) {
  ctool_x86_value_t result;
  result.kind = CTOOL_X86_VALUE_CONSTANT;
  result.bits = bits;
  result.addend = 0;
  result.reference = 0u;
  return result;
}

static ctool_x86_value_t reference(ctool_u32 symbol, ctool_i32 addend) {
  ctool_x86_value_t result;
  result.kind = CTOOL_X86_VALUE_REFERENCE;
  result.bits = 0u;
  result.addend = addend;
  result.reference = symbol;
  return result;
}

static ctool_x86_operand_t register_operand(ctool_x86_reg_class_t class_id,
                                            ctool_u8 index) {
  ctool_x86_operand_t result;
  (void)memset(&result, 0, sizeof(result));
  result.kind = CTOOL_X86_OPERAND_REGISTER;
  result.as.reg = reg(class_id, index);
  return result;
}

static ctool_x86_operand_t value_operand(ctool_x86_operand_kind_t kind,
                                         ctool_u16 width_bits,
                                         ctool_u16 encoding_bits,
                                         ctool_x86_value_t value) {
  ctool_x86_operand_t result;
  (void)memset(&result, 0, sizeof(result));
  result.kind = kind;
  result.width_bits = width_bits;
  result.encoding_bits = encoding_bits;
  result.as.value = value;
  return result;
}

static ctool_x86_operand_t memory_operand(
    ctool_u16 width_bits, ctool_u16 address_bits,
    ctool_x86_reg_t segment, ctool_x86_reg_t base,
    ctool_x86_reg_t index, ctool_u8 scale, ctool_i32 displacement,
    ctool_u16 displacement_bits) {
  ctool_x86_operand_t result;
  (void)memset(&result, 0, sizeof(result));
  result.kind = CTOOL_X86_OPERAND_MEMORY;
  result.width_bits = width_bits;
  result.as.memory.address_bits = address_bits;
  result.as.memory.segment = segment;
  result.as.memory.base = base;
  result.as.memory.index = index;
  result.as.memory.scale = scale;
  result.as.memory.displacement = constant((ctool_u32)displacement);
  result.as.memory.displacement_bits = displacement_bits;
  return result;
}

static ctool_x86_operand_t far_operand(ctool_u16 offset_bits,
                                       ctool_x86_value_t offset,
                                       ctool_x86_value_t segment) {
  ctool_x86_operand_t result;
  (void)memset(&result, 0, sizeof(result));
  result.kind = CTOOL_X86_OPERAND_FAR_POINTER;
  result.width_bits = offset_bits;
  result.encoding_bits = offset_bits;
  result.as.far_pointer.offset = offset;
  result.as.far_pointer.segment = segment;
  return result;
}

static ctool_x86_instruction_t instruction(ctool_x86_mnemonic_t mnemonic,
                                           ctool_u16 operand_bits,
                                           ctool_u16 address_bits,
                                           ctool_u8 prefixes) {
  ctool_x86_instruction_t result;
  (void)memset(&result, 0, sizeof(result));
  result.mnemonic = mnemonic;
  result.operand_bits = operand_bits;
  result.address_bits = address_bits;
  result.prefixes = prefixes;
  return result;
}

static int bytes_equal(const ctool_x86_encoding_t *encoding,
                       const ctool_u8 *expected, ctool_u8 size,
                       const char *operation) {
  return check_true(encoding->size == size &&
                        memcmp(encoding->bytes, expected, size) == 0,
                    operation);
}

static int encoding_is_zero(const ctool_x86_encoding_t *encoding) {
  const unsigned char *bytes = (const unsigned char *)encoding;
  size_t index;
  for (index = 0u; index < sizeof(*encoding); index++) {
    if (bytes[index] != 0u) {
      return 0;
    }
  }
  return 1;
}

static int decoded_is_zero(const ctool_x86_decoded_t *decoded) {
  const unsigned char *bytes = (const unsigned char *)decoded;
  size_t index;
  for (index = 0u; index < sizeof(*decoded); index++) {
    if (bytes[index] != 0u) {
      return 0;
    }
  }
  return 1;
}

static int encode(ctool_job_t *job, ctool_x86_mode_t mode,
                  const ctool_x86_instruction_t *insn,
                  ctool_x86_encoding_t *encoding, const char *operation) {
  return check_status(ctool_x86_encode(job, mode, insn,
                                       CTOOL_X86_FORM_AUTO, encoding),
                      CTOOL_OK, operation);
}

static int run_model(void) {
  static const char *const required_mnemonics[] = {
      "adc",      "add",      "addps",    "addss",    "and",
      "bswap",    "call",     "clc",      "cld",      "cli",
      "clts",     "cmp",      "cmpxchg",  "cpuid",    "dec",
      "div",      "finit",    "fld",      "fninit",   "fsin",
      "fstp",     "fwait",    "fxrstor",  "fxsave",   "hlt",
      "in",       "inc",      "int",      "invd",     "invlpg",
      "iret",     "iretd",    "jb",       "jbe",      "jc",
      "je",       "jge",      "jl",       "jle",      "jmp",
      "jnc",      "jne",      "jng",      "jnl",      "jnz",
      "jpe",      "jpo",      "jz",       "ldmxcsr",  "lgdt",
      "lidt",     "lmsw",     "ltr",      "mov",      "movss",
      "movups",   "movzx",    "mul",      "mulps",    "or",
      "out",      "pop",      "popa",     "popfd",    "push",
      "pusha",    "pushfd",   "rdmsr",    "rdtsc",    "ret",
      "retf",     "sgdt",     "shl",      "shr",      "sidt",
      "sldt",     "smsw",     "sqrtss",   "stc",      "sti",
      "stmxcsr",  "str",      "sub",      "syscall",  "sysenter",
      "sysexit",  "test",     "wbinvd",   "wrmsr",    "xadd",
      "xor",      "idiv",     "imul",     "lea",      "leave",
      "movsx",    "neg",      "nop",      "not",      "sar",
      "sete",     "setne",    "setl",     "setg",     "setle",
      "setge",    "xchg",     "addsd",    "addpd",    "andps",
      "andpd",    "cmpps",    "cvtps2dq", "cvtdq2ps", "cvtsd2si",
      "cvtss2si", "cvtsi2sd", "cvtsi2ss", "cvtsd2ss", "cvtss2sd",
      "divpd",    "divps",    "divsd",    "divss",    "maxps",
      "maxss",    "minps",    "minss",    "movaps",   "movapd",
      "movd",     "movdqa",   "movdqu",   "movmskps", "movntdq",
      "movsd",    "movupd",   "mulpd",    "mulsd",    "packuswb",
      "paddusb",  "paddw",    "pmullw",   "pshufd",   "psrlw",
      "punpckhbw", "punpcklbw", "punpcklwd", "pxor",  "sfence",
      "shufps",   "sqrtpd",   "sqrtps",   "sqrtsd",   "subpd",
      "subps",    "subsd",    "subss",    "ucomisd",  "ucomiss",
      "f2xm1",    "faddp",    "fcos",     "fld1",     "fldcw",
      "fmulp",    "fnstcw",   "fnstsw",   "fpatan",   "fptan",
      "frndint",  "fscale",   "fsub",     "fsubr",    "fxch",
      "fyl2x",
      "pause",    "rdrand",   "setc",     "insw",     "outsw",
      "cbw",      "cdq",      "cmc",      "cwde",     "fadd",
      "fdiv",     "fdivp",    "fmul",     "fprem",    "movsb",
      "movsw",    "orpd",     "orps",     "shufpd",   "stosb",
      "stosd",    "stosw",    "xorpd",    "xorps"};
  static const char *const required_registers[] = {
      "al",   "cl",   "dl",   "bl",   "ah",   "ch",   "dh",
      "bh",   "ax",   "cx",   "dx",   "bx",   "sp",   "bp",
      "si",   "di",   "eax",  "ecx",  "edx",  "ebx",  "esp",
      "ebp",  "esi",  "edi",  "es",   "cs",   "ss",   "ds",
      "fs",   "gs",   "cr0",  "cr1",  "cr2",  "cr3",  "cr4",
      "cr5",  "cr6",  "cr7",  "st0",  "st1",  "st2",  "st3",
      "st4",  "st5",  "st6",  "st7",  "mm0",  "mm1",  "mm2",
      "mm3",  "mm4",  "mm5",  "mm6",  "mm7",  "xmm0", "xmm1",
      "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7"};
  ctool_host_adapter_t adapter;
  ctool_job_t *job;
  ctool_x86_model_info_t info;
  ctool_x86_mnemonic_t mnemonic;
  ctool_x86_reg_t found_reg;
  ctool_string_t mnemonic_name;
  ctool_u32 index;
  if (!open_job(&adapter, &job)) {
    return 1;
  }
  if (!check_status(ctool_x86_validate_model(job), CTOOL_OK,
                    "model validation")) {
    ctool_job_close(job);
    return 1;
  }
  info = ctool_x86_model_info();
  if (!check_true(info.form_count == 546u && info.mnemonic_count == 226u &&
                      info.register_count == 64u &&
                      info.fingerprint == 0x3159218eu,
                  "model inventory")) {
    ctool_job_close(job);
    return 1;
  }
  if (!check_status(ctool_x86_mnemonic_from_name(ctool_string("jnz"),
                                                  &mnemonic),
                    CTOOL_OK, "mnemonic alias") ||
      !check_true(mnemonic == CTOOL_X86_MN_JNE,
                  "mnemonic alias canonicalization") ||
      !check_true((mnemonic_name = ctool_x86_mnemonic_name(mnemonic)).size ==
                          3u &&
                      memcmp(mnemonic_name.data, "jne", 3u) == 0,
                  "mnemonic canonical name") ||
      !check_status(ctool_x86_register_from_name(ctool_string("xmm7"),
                                                  &found_reg),
                    CTOOL_OK, "register lookup") ||
      !check_true(found_reg.class_id == CTOOL_X86_REG_XMM &&
                      found_reg.index == 7u,
                  "register classification")) {
    ctool_job_close(job);
    return 1;
  }
  if (!check_status(ctool_x86_mnemonic_from_name(ctool_string("MOVSS"),
                                                  &mnemonic),
                    CTOOL_OK, "case-insensitive mnemonic") ||
      !check_true(mnemonic == CTOOL_X86_MN_MOVSS,
                  "case-insensitive mnemonic identity") ||
      !check_status(ctool_x86_mnemonic_from_name(ctool_string("not-an-op"),
                                                  &mnemonic),
                    CTOOL_ERR_NOT_FOUND, "unknown mnemonic") ||
      !check_status(ctool_x86_register_from_name(ctool_string("r8d"),
                                                  &found_reg),
                    CTOOL_ERR_NOT_FOUND, "out-of-domain register")) {
    ctool_job_close(job);
    return 1;
  }
  for (index = 0u;
       index < (ctool_u32)(sizeof(required_mnemonics) /
                           sizeof(required_mnemonics[0]));
       index++) {
    if (!check_status(
            ctool_x86_mnemonic_from_name(
                ctool_string(required_mnemonics[index]), &mnemonic),
            CTOOL_OK, required_mnemonics[index]) ||
        !check_true(ctool_x86_mnemonic_name(mnemonic).size != 0u,
                    "required mnemonic canonical name")) {
      ctool_job_close(job);
      return 1;
    }
  }
  for (index = 0u;
       index < (ctool_u32)(sizeof(required_registers) /
                           sizeof(required_registers[0]));
       index++) {
    if (!check_status(
            ctool_x86_register_from_name(
                ctool_string(required_registers[index]), &found_reg),
            CTOOL_OK, required_registers[index]) ||
        !check_true(ctool_x86_register_name(found_reg).size != 0u,
                    "required register canonical name")) {
      ctool_job_close(job);
      return 1;
    }
  }
  ctool_job_close(job);
  (void)printf("model: ok\n");
  return 0;
}

static int run_inventory(void) {
  ctool_x86_model_info_t info = ctool_x86_model_info();
  (void)printf("inventory: forms=%u mnemonics=%u registers=%u fingerprint=%08X\n",
               info.form_count, info.mnemonic_count, info.register_count,
               info.fingerprint);
  return 0;
}

static int run_integer(void) {
  static const ctool_u8 mov_bytes[] = {0xb8u, 0x78u, 0x56u, 0x34u, 0x12u};
  static const ctool_u8 add_bytes[] = {0x01u, 0xd8u};
  static const ctool_u8 lock_add_bytes[] = {0xf0u, 0x01u, 0x18u};
  static const ctool_u8 call_bytes[] = {0xe8u, 0xfcu, 0xffu, 0xffu, 0xffu};
  ctool_host_adapter_t adapter;
  ctool_job_t *job;
  ctool_x86_instruction_t insn;
  ctool_x86_encoding_t encoding;
  ctool_x86_encoding_t preserved;
  ctool_x86_decoded_t decoded;
  ctool_x86_form_t mov_form;
  ctool_status_t status;
  if (!open_job(&adapter, &job)) {
    return 1;
  }

  insn = instruction(CTOOL_X86_MN_MOV, 32u, 32u, 0u);
  insn.operand_count = 2u;
  insn.operands[0] = register_operand(CTOOL_X86_REG_GPR32, 0u);
  insn.operands[1] = value_operand(CTOOL_X86_OPERAND_IMMEDIATE, 32u, 32u,
                                   constant(0x12345678u));
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "mov encode") ||
      !bytes_equal(&encoding, mov_bytes, (ctool_u8)sizeof(mov_bytes),
                   "mov bytes")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                            ctool_bytes(mov_bytes,
                                        (ctool_u32)sizeof(mov_bytes)),
                            0u,
                            &decoded);
  if (!check_status(status, CTOOL_OK, "mov decode") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                      decoded.instruction.mnemonic == CTOOL_X86_MN_MOV &&
                      decoded.instruction.operand_bits == 32u &&
                      decoded.instruction.operand_count == 2u &&
                      decoded.instruction.operands[0].kind ==
                          CTOOL_X86_OPERAND_REGISTER &&
                      decoded.instruction.operands[0].as.reg.class_id ==
                          CTOOL_X86_REG_GPR32 &&
                      decoded.instruction.operands[0].as.reg.index == 0u &&
                      decoded.instruction.operands[1].kind ==
                          CTOOL_X86_OPERAND_IMMEDIATE &&
                      decoded.instruction.operands[1].as.value.bits ==
                          0x12345678u &&
                      decoded.encoding.form != CTOOL_X86_FORM_AUTO &&
                      decoded.encoding.size == sizeof(mov_bytes) &&
                      memcmp(decoded.encoding.bytes, mov_bytes,
                             sizeof(mov_bytes)) == 0 &&
                      decoded.encoding.field_count == 1u &&
                      decoded.encoding.fields[0].kind ==
                          CTOOL_X86_FIELD_IMMEDIATE &&
                      decoded.encoding.fields[0].relocation ==
                          CTOOL_X86_RELOC_NONE &&
                      decoded.encoding.fields[0].byte_offset == 1u &&
                      decoded.encoding.fields[0].byte_width == 4u &&
                      decoded.consumed == sizeof(mov_bytes),
                  "mov decode semantics")) {
    ctool_job_close(job);
    return 1;
  }
  mov_form = decoded.encoding.form;
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32,
                            &decoded.instruction, mov_form, &preserved);
  if (!check_status(status, CTOOL_OK, "same-form re-encode") ||
      !bytes_equal(&preserved, mov_bytes, (ctool_u8)sizeof(mov_bytes),
                   "same-form bytes")) {
    ctool_job_close(job);
    return 1;
  }

  insn = instruction(CTOOL_X86_MN_ADD, 32u, 32u, 0u);
  insn.operand_count = 2u;
  insn.operands[0] = register_operand(CTOOL_X86_REG_GPR32, 0u);
  insn.operands[1] = register_operand(CTOOL_X86_REG_GPR32, 3u);
  (void)memset(&preserved, 0xa5, sizeof(preserved));
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn, mov_form,
                            &preserved);
  if (!check_status(status, CTOOL_ERR_INPUT, "mismatched form") ||
      !check_true(encoding_is_zero(&preserved),
                  "mismatched form zeroed output")) {
    ctool_job_close(job);
    return 1;
  }
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "add encode") ||
      !bytes_equal(&encoding, add_bytes, (ctool_u8)sizeof(add_bytes),
                   "add bytes")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                            ctool_bytes(add_bytes,
                                        (ctool_u32)sizeof(add_bytes)),
                            0u, &decoded);
  if (!check_status(status, CTOOL_OK, "add decode") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                      decoded.instruction.mnemonic == CTOOL_X86_MN_ADD &&
                      decoded.instruction.operand_count == 2u &&
                      decoded.instruction.operands[0].as.reg.index == 0u &&
                      decoded.instruction.operands[1].as.reg.index == 3u,
                  "add ModRM direction")) {
    ctool_job_close(job);
    return 1;
  }

  insn = instruction(CTOOL_X86_MN_ADD, 32u, 32u,
                     CTOOL_X86_PREFIX_LOCK);
  insn.operand_count = 2u;
  insn.operands[0] = memory_operand(
      32u, 32u, reg(CTOOL_X86_REG_NONE, 0u),
      reg(CTOOL_X86_REG_GPR32, 0u), reg(CTOOL_X86_REG_NONE, 0u),
      1u, 0, 0u);
  insn.operands[1] = register_operand(CTOOL_X86_REG_GPR32, 3u);
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding,
              "lock add encode") ||
      !bytes_equal(&encoding, lock_add_bytes,
                   (ctool_u8)sizeof(lock_add_bytes), "lock add bytes")) {
    ctool_job_close(job);
    return 1;
  }

  insn = instruction(CTOOL_X86_MN_CALL, 32u, 32u, 0u);
  insn.operand_count = 1u;
  insn.operands[0] = value_operand(CTOOL_X86_OPERAND_RELATIVE, 32u, 32u,
                                   reference(7u, 0));
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "call encode") ||
      !bytes_equal(&encoding, call_bytes, (ctool_u8)sizeof(call_bytes),
                   "call bytes") ||
      !check_true(encoding.field_count == 1u &&
                      encoding.fields[0].kind == CTOOL_X86_FIELD_RELATIVE &&
                      encoding.fields[0].relocation ==
                          CTOOL_X86_RELOC_PC_RELATIVE &&
                      encoding.fields[0].byte_offset == 1u &&
                      encoding.fields[0].byte_width == 4u &&
                      encoding.fields[0].pc_bias == 4u &&
                      encoding.fields[0].reference == 7u &&
                      encoding.fields[0].encoded_addend == -4,
                  "call relocation field")) {
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)printf("integer: ok\n");
  return 0;
}

static int run_addressing(void) {
  static const ctool_u8 addr16_bytes[] = {0x8bu, 0x40u, 0x7fu};
  static const ctool_u8 sib_bytes[] = {0x8bu, 0x44u, 0x8bu, 0x10u};
  static const ctool_u8 override_bytes[] = {0x66u, 0x8bu, 0x45u, 0x00u};
  static const ctool_u8 negative_disp16[] = {0x8bu, 0x86u, 0u, 0xffu};
  ctool_host_adapter_t adapter;
  ctool_job_t *job;
  ctool_x86_instruction_t insn;
  ctool_x86_encoding_t encoding;
  ctool_x86_decoded_t decoded;
  ctool_x86_operand_t memory;
  ctool_status_t status;
  if (!open_job(&adapter, &job)) {
    return 1;
  }

  insn = instruction(CTOOL_X86_MN_MOV, 16u, 16u, 0u);
  insn.operand_count = 2u;
  insn.operands[0] = register_operand(CTOOL_X86_REG_GPR16, 0u);
  memory = memory_operand(16u, 16u, reg(CTOOL_X86_REG_NONE, 0u),
                          reg(CTOOL_X86_REG_GPR16, 3u),
                          reg(CTOOL_X86_REG_GPR16, 6u), 1u, 0x7f, 8u);
  insn.operands[1] = memory;
  if (!encode(job, CTOOL_X86_MODE_16, &insn, &encoding,
              "16-bit address encode") ||
      !bytes_equal(&encoding, addr16_bytes,
                   (ctool_u8)sizeof(addr16_bytes), "16-bit address bytes")) {
    ctool_job_close(job);
    return 1;
  }
  insn.operands[1] = memory_operand(
      16u, 16u, reg(CTOOL_X86_REG_NONE, 0u),
      reg(CTOOL_X86_REG_GPR16, 6u), reg(CTOOL_X86_REG_GPR16, 3u), 1u,
      0x7f, 8u);
  if (!encode(job, CTOOL_X86_MODE_16, &insn, &encoding,
              "commuted 16-bit address encode") ||
      !bytes_equal(&encoding, addr16_bytes,
                   (ctool_u8)sizeof(addr16_bytes),
                   "commuted 16-bit address bytes")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_16,
      ctool_bytes(addr16_bytes, (ctool_u32)sizeof(addr16_bytes)), 0u,
      &decoded);
  if (!check_status(status, CTOOL_OK, "16-bit address decode") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                      decoded.instruction.operand_bits == 16u &&
                      decoded.instruction.address_bits == 16u &&
                      decoded.instruction.operands[1].kind ==
                          CTOOL_X86_OPERAND_MEMORY &&
                      decoded.instruction.operands[1].as.memory.base.class_id ==
                          CTOOL_X86_REG_GPR16 &&
                      decoded.instruction.operands[1].as.memory.base.index ==
                          3u &&
                      decoded.instruction.operands[1].as.memory.index.index ==
                          6u &&
                      decoded.instruction.operands[1]
                              .as.memory.displacement.bits == 0x7fu,
                  "16-bit address semantics")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_16,
      ctool_bytes(negative_disp16, (ctool_u32)sizeof(negative_disp16)), 0u,
      &decoded);
  if (!check_status(status, CTOOL_OK, "signed disp16 decode") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                      decoded.instruction.operands[1].kind ==
                          CTOOL_X86_OPERAND_MEMORY &&
                      decoded.instruction.operands[1]
                              .as.memory.displacement.bits == 0xffffff00u &&
                      decoded.encoding.fields[0].encoded_addend == -256,
                  "signed disp16 semantics")) {
    ctool_job_close(job);
    return 1;
  }

  insn = instruction(CTOOL_X86_MN_MOV, 32u, 32u, 0u);
  insn.operand_count = 2u;
  insn.operands[0] = register_operand(CTOOL_X86_REG_GPR32, 0u);
  insn.operands[1] = memory_operand(
      32u, 32u, reg(CTOOL_X86_REG_NONE, 0u),
      reg(CTOOL_X86_REG_GPR32, 3u), reg(CTOOL_X86_REG_GPR32, 1u),
      4u, 0x10, 8u);
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "SIB encode") ||
      !bytes_equal(&encoding, sib_bytes, (ctool_u8)sizeof(sib_bytes),
                   "SIB bytes")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_32,
      ctool_bytes(sib_bytes, (ctool_u32)sizeof(sib_bytes)), 0u, &decoded);
  if (!check_status(status, CTOOL_OK, "SIB decode") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                      decoded.instruction.operands[1].as.memory.base.index ==
                          3u &&
                      decoded.instruction.operands[1].as.memory.index.index ==
                          1u &&
                      decoded.instruction.operands[1].as.memory.scale == 4u &&
                      decoded.instruction.operands[1]
                              .as.memory.displacement.bits == 0x10u,
                  "SIB decode semantics")) {
    ctool_job_close(job);
    return 1;
  }

  insn = instruction(CTOOL_X86_MN_MOV, 16u, 32u, 0u);
  insn.operand_count = 2u;
  insn.operands[0] = register_operand(CTOOL_X86_REG_GPR16, 0u);
  insn.operands[1] = memory_operand(
      16u, 32u, reg(CTOOL_X86_REG_NONE, 0u),
      reg(CTOOL_X86_REG_GPR32, 5u), reg(CTOOL_X86_REG_NONE, 0u),
      1u, 0, 0u);
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding,
              "operand override encode") ||
      !bytes_equal(&encoding, override_bytes,
                   (ctool_u8)sizeof(override_bytes),
                   "operand override bytes")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_32,
      ctool_bytes(override_bytes, (ctool_u32)sizeof(override_bytes)), 0u,
      &decoded);
  if (!check_status(status, CTOOL_OK, "operand override decode") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                      decoded.instruction.operand_bits == 16u &&
                      decoded.instruction.address_bits == 32u &&
                      decoded.encoding.size == sizeof(override_bytes) &&
                      memcmp(decoded.encoding.bytes, override_bytes,
                             sizeof(override_bytes)) == 0,
                  "operand override semantics")) {
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)printf("addressing: ok\n");
  return 0;
}

static int run_relocations(void) {
  static const ctool_u8 absolute_imm[] = {0xb8u, 4u, 0u, 0u, 0u};
  static const ctool_u8 absolute_disp[] = {0x8bu, 0x1du, 8u, 0u, 0u, 0u};
  static const ctool_u8 far_offset[] = {0xeau, 0u, 0u, 0u, 0u, 8u, 0u};
  static const ctool_u8 far_segment[] = {0xeau, 0x78u, 0x56u, 0x34u,
                                         0x12u, 2u, 0u};
  static const ctool_u8 short_backward[] = {0xebu, 0xfcu};
  static const ctool_u8 group_add[] = {0x81u, 0xc0u, 1u, 0u, 0u, 0u};
  static const ctool_u8 short_add[] = {0x83u, 0xc0u, 1u};
  static const ctool_u8 accumulator_add[] = {0x05u, 0x78u, 0x56u,
                                              0x34u, 0x12u};
  ctool_host_adapter_t adapter;
  ctool_job_t *job;
  ctool_x86_instruction_t insn;
  ctool_x86_encoding_t encoding;
  ctool_x86_decoded_t decoded;
  ctool_status_t status;
  if (!open_job(&adapter, &job)) {
    return 1;
  }

  insn = instruction(CTOOL_X86_MN_MOV, 32u, 32u, 0u);
  insn.operand_count = 2u;
  insn.operands[0] = register_operand(CTOOL_X86_REG_GPR32, 0u);
  insn.operands[1] = value_operand(CTOOL_X86_OPERAND_IMMEDIATE, 32u, 32u,
                                   reference(3u, 4));
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding,
              "absolute immediate") ||
      !bytes_equal(&encoding, absolute_imm,
                   (ctool_u8)sizeof(absolute_imm),
                   "absolute immediate bytes") ||
      !check_true(encoding.field_count == 1u &&
                      encoding.fields[0].kind ==
                          CTOOL_X86_FIELD_IMMEDIATE &&
                      encoding.fields[0].relocation ==
                          CTOOL_X86_RELOC_ABSOLUTE &&
                      encoding.fields[0].byte_offset == 1u &&
                      encoding.fields[0].byte_width == 4u &&
                      encoding.fields[0].reference == 3u &&
                      encoding.fields[0].encoded_addend == 4,
                  "absolute immediate field")) {
    ctool_job_close(job);
    return 1;
  }

  insn.operands[0] = register_operand(CTOOL_X86_REG_GPR32, 3u);
  insn.operands[1] = memory_operand(
      32u, 32u, reg(CTOOL_X86_REG_NONE, 0u),
      reg(CTOOL_X86_REG_NONE, 0u), reg(CTOOL_X86_REG_NONE, 0u), 1u, 0, 32u);
  insn.operands[1].as.memory.displacement = reference(5u, 8);
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding,
              "absolute displacement") ||
      !bytes_equal(&encoding, absolute_disp,
                   (ctool_u8)sizeof(absolute_disp),
                   "absolute displacement bytes") ||
      !check_true(encoding.field_count == 1u &&
                      encoding.fields[0].kind ==
                          CTOOL_X86_FIELD_DISPLACEMENT &&
                      encoding.fields[0].relocation ==
                          CTOOL_X86_RELOC_ABSOLUTE &&
                      encoding.fields[0].operand_index == 1u &&
                      encoding.fields[0].byte_offset == 2u &&
                      encoding.fields[0].byte_width == 4u &&
                      encoding.fields[0].reference == 5u &&
                      encoding.fields[0].encoded_addend == 8,
                  "absolute displacement field")) {
    ctool_job_close(job);
    return 1;
  }

  insn = instruction(CTOOL_X86_MN_JMP, 32u, 32u, 0u);
  insn.operand_count = 1u;
  insn.operands[0] = far_operand(32u, reference(7u, 0), constant(8u));
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding,
              "far offset relocation") ||
      !bytes_equal(&encoding, far_offset, (ctool_u8)sizeof(far_offset),
                   "far offset bytes") ||
      !check_true(encoding.field_count == 2u &&
                      encoding.fields[0].kind ==
                          CTOOL_X86_FIELD_FAR_OFFSET &&
                      encoding.fields[0].relocation ==
                          CTOOL_X86_RELOC_ABSOLUTE &&
                      encoding.fields[0].byte_offset == 1u &&
                      encoding.fields[0].byte_width == 4u &&
                      encoding.fields[0].reference == 7u &&
                      encoding.fields[1].kind ==
                          CTOOL_X86_FIELD_FAR_SEGMENT &&
                      encoding.fields[1].relocation == CTOOL_X86_RELOC_NONE &&
                      encoding.fields[1].byte_offset == 5u &&
                      encoding.fields[1].byte_width == 2u,
                  "far offset fields")) {
    ctool_job_close(job);
    return 1;
  }

  insn.operands[0] =
      far_operand(32u, constant(0x12345678u), reference(9u, 2));
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding,
              "far segment relocation") ||
      !bytes_equal(&encoding, far_segment,
                   (ctool_u8)sizeof(far_segment),
                   "far segment bytes") ||
      !check_true(encoding.field_count == 2u &&
                      encoding.fields[1].relocation ==
                          CTOOL_X86_RELOC_ABSOLUTE &&
                      encoding.fields[1].reference == 9u &&
                      encoding.fields[1].encoded_addend == 2,
                  "far segment field")) {
    ctool_job_close(job);
    return 1;
  }

  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_32,
      ctool_bytes(short_backward, (ctool_u32)sizeof(short_backward)), 0u,
      &decoded);
  if (!check_status(status, CTOOL_OK, "short backward decode") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                      decoded.instruction.operand_bits == 32u &&
                      decoded.instruction.operands[0].encoding_bits == 8u &&
                      decoded.instruction.operands[0].as.value.bits ==
                          0xfffffffcu &&
                      decoded.encoding.fields[0].encoded_addend == -4,
                  "short backward semantics")) {
    ctool_job_close(job);
    return 1;
  }

  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_32,
      ctool_bytes(group_add, (ctool_u32)sizeof(group_add)), 0u, &decoded);
  if (!check_status(status, CTOOL_OK, "group add decode") ||
      !check_status(ctool_x86_encode(job, CTOOL_X86_MODE_32,
                                     &decoded.instruction,
                                     decoded.encoding.form, &encoding),
                    CTOOL_OK, "group add replay") ||
      !bytes_equal(&encoding, group_add, (ctool_u8)sizeof(group_add),
                   "group add replay bytes")) {
    ctool_job_close(job);
    return 1;
  }

  insn = instruction(CTOOL_X86_MN_ADD, 32u, 32u, 0u);
  insn.operand_count = 2u;
  insn.operands[0] = register_operand(CTOOL_X86_REG_GPR32, 0u);
  insn.operands[1] = value_operand(CTOOL_X86_OPERAND_IMMEDIATE, 32u, 0u,
                                   constant(1u));
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding,
              "shortest add") ||
      !bytes_equal(&encoding, short_add, (ctool_u8)sizeof(short_add),
                   "shortest add bytes")) {
    ctool_job_close(job);
    return 1;
  }
  insn.operands[1] = value_operand(CTOOL_X86_OPERAND_IMMEDIATE, 32u, 32u,
                                   constant(0x12345678u));
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding,
              "accumulator add") ||
      !bytes_equal(&encoding, accumulator_add,
                   (ctool_u8)sizeof(accumulator_add),
                   "accumulator add bytes")) {
    ctool_job_close(job);
    return 1;
  }

  ctool_job_close(job);
  (void)printf("relocations: ok\n");
  return 0;
}

static int run_system_simd(void) {
  static const ctool_u8 cr_bytes[] = {0x0fu, 0x22u, 0xc0u};
  static const ctool_u8 fxsave_bytes[] = {0x0fu, 0xaeu, 0x40u, 0x10u};
  static const ctool_u8 fsin_bytes[] = {0xd9u, 0xfeu};
  static const ctool_u8 movss_bytes[] = {0xf3u, 0x0fu, 0x10u, 0x00u};
  static const ctool_u8 pxor_bytes[] = {0x66u, 0x0fu, 0xefu, 0xcau};
  ctool_host_adapter_t adapter;
  ctool_job_t *job;
  ctool_x86_instruction_t insn;
  ctool_x86_encoding_t encoding;
  ctool_x86_decoded_t decoded;
  ctool_status_t status;
  if (!open_job(&adapter, &job)) {
    return 1;
  }

  insn = instruction(CTOOL_X86_MN_MOV, 32u, 32u, 0u);
  insn.operand_count = 2u;
  insn.operands[0] = register_operand(CTOOL_X86_REG_CONTROL, 0u);
  insn.operands[1] = register_operand(CTOOL_X86_REG_GPR32, 0u);
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "CR0 move") ||
      !bytes_equal(&encoding, cr_bytes, (ctool_u8)sizeof(cr_bytes),
                   "CR0 move bytes")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_32,
      ctool_bytes(cr_bytes, (ctool_u32)sizeof(cr_bytes)), 0u, &decoded);
  if (!check_status(status, CTOOL_OK, "CR0 move decode") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                      decoded.instruction.mnemonic == CTOOL_X86_MN_MOV &&
                      decoded.instruction.operands[0].as.reg.class_id ==
                          CTOOL_X86_REG_CONTROL &&
                      decoded.instruction.operands[1].as.reg.class_id ==
                          CTOOL_X86_REG_GPR32,
                  "CR0 move decode semantics")) {
    ctool_job_close(job);
    return 1;
  }

  insn = instruction(CTOOL_X86_MN_FXSAVE, 32u, 32u, 0u);
  insn.operand_count = 1u;
  insn.operands[0] = memory_operand(
      0u, 32u, reg(CTOOL_X86_REG_NONE, 0u),
      reg(CTOOL_X86_REG_GPR32, 0u), reg(CTOOL_X86_REG_NONE, 0u),
      1u, 0x10, 8u);
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "fxsave") ||
      !bytes_equal(&encoding, fxsave_bytes,
                   (ctool_u8)sizeof(fxsave_bytes), "fxsave bytes")) {
    ctool_job_close(job);
    return 1;
  }

  insn = instruction(CTOOL_X86_MN_FSIN, 32u, 32u, 0u);
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "fsin") ||
      !bytes_equal(&encoding, fsin_bytes, (ctool_u8)sizeof(fsin_bytes),
                   "fsin bytes")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_32,
      ctool_bytes(fsin_bytes, (ctool_u32)sizeof(fsin_bytes)), 0u,
      &decoded);
  if (!check_status(status, CTOOL_OK, "fsin decode") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                      decoded.instruction.mnemonic == CTOOL_X86_MN_FSIN,
                  "fsin decode semantics")) {
    ctool_job_close(job);
    return 1;
  }

  insn = instruction(CTOOL_X86_MN_MOVSS, 32u, 32u, 0u);
  insn.operand_count = 2u;
  insn.operands[0] = register_operand(CTOOL_X86_REG_XMM, 0u);
  insn.operands[1] = memory_operand(
      32u, 32u, reg(CTOOL_X86_REG_NONE, 0u),
      reg(CTOOL_X86_REG_GPR32, 0u), reg(CTOOL_X86_REG_NONE, 0u),
      1u, 0, 0u);
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "movss") ||
      !bytes_equal(&encoding, movss_bytes,
                   (ctool_u8)sizeof(movss_bytes), "movss bytes")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_32,
      ctool_bytes(movss_bytes, (ctool_u32)sizeof(movss_bytes)), 0u,
      &decoded);
  if (!check_status(status, CTOOL_OK, "movss decode") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                      decoded.instruction.mnemonic == CTOOL_X86_MN_MOVSS &&
                      decoded.instruction.prefixes == 0u &&
                      decoded.instruction.operands[0].as.reg.class_id ==
                          CTOOL_X86_REG_XMM &&
                      decoded.instruction.operands[1].kind ==
                          CTOOL_X86_OPERAND_MEMORY,
                  "MOVSS mandatory-prefix semantics")) {
    ctool_job_close(job);
    return 1;
  }

  insn = instruction(CTOOL_X86_MN_PXOR, 32u, 32u, 0u);
  insn.operand_count = 2u;
  insn.operands[0] = register_operand(CTOOL_X86_REG_XMM, 1u);
  insn.operands[1] = register_operand(CTOOL_X86_REG_XMM, 2u);
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "pxor") ||
      !bytes_equal(&encoding, pxor_bytes, (ctool_u8)sizeof(pxor_bytes),
                   "pxor bytes")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_32,
      ctool_bytes(pxor_bytes, (ctool_u32)sizeof(pxor_bytes)), 0u,
      &decoded);
  if (!check_status(status, CTOOL_OK, "pxor decode") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                      decoded.instruction.mnemonic == CTOOL_X86_MN_PXOR &&
                      decoded.instruction.prefixes == 0u &&
                      decoded.instruction.operands[0].as.reg.index == 1u &&
                      decoded.instruction.operands[1].as.reg.index == 2u,
                  "PXOR mandatory-prefix semantics")) {
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)printf("system-simd: ok\n");
  return 0;
}

typedef struct {
  const char *name;
  ctool_x86_mode_t mode;
  ctool_x86_mnemonic_t mnemonic;
  ctool_u8 size;
  ctool_u8 bytes[CTOOL_X86_MAX_INSTRUCTION_BYTES];
} active_vector_t;

static int check_active_vectors(ctool_job_t *job,
                                const active_vector_t *vectors,
                                ctool_u32 vector_count) {
  ctool_u32 index;
  for (index = 0u; index < vector_count; index++) {
    const active_vector_t *vector = &vectors[index];
    ctool_x86_decoded_t decoded;
    ctool_x86_encoding_t reencoded;
    ctool_status_t status = ctool_x86_decode(
        job, vector->mode, ctool_bytes(vector->bytes, vector->size), 0u,
        &decoded);
    ctool_u32 field;
    if (!check_status(status, CTOOL_OK, vector->name) ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                        decoded.instruction.mnemonic == vector->mnemonic &&
                        decoded.consumed == vector->size &&
                        decoded.encoding.size == vector->size &&
                        decoded.encoding.form != CTOOL_X86_FORM_AUTO &&
                        memcmp(decoded.encoding.bytes, vector->bytes,
                               vector->size) == 0,
                    vector->name)) {
      return 0;
    }
    for (field = 0u; field < (ctool_u32)decoded.encoding.field_count;
         field++) {
      if (!check_true(decoded.encoding.fields[field].relocation ==
                          CTOOL_X86_RELOC_NONE,
                      "raw decode never invents relocation ownership")) {
        return 0;
      }
    }
    status = ctool_x86_encode(job, vector->mode, &decoded.instruction,
                              decoded.encoding.form, &reencoded);
    if (!check_status(status, CTOOL_OK, vector->name) ||
        !bytes_equal(&reencoded, vector->bytes, vector->size, vector->name)) {
      return 0;
    }
  }
  return 1;
}

static int check_gnu_string_prefix_order(ctool_job_t *job) {
  static const ctool_u8 gnu_bytes[][3] = {
      {0x66u, 0xf3u, 0x6du}, {0x66u, 0xf3u, 0x6fu}};
  static const ctool_u8 canonical_bytes[][3] = {
      {0xf3u, 0x66u, 0x6du}, {0xf3u, 0x66u, 0x6fu}};
  static const ctool_x86_mnemonic_t mnemonics[] = {
      CTOOL_X86_MN_INSW, CTOOL_X86_MN_OUTSW};
  ctool_u32 index;
  for (index = 0u; index < 2u; index++) {
    ctool_x86_decoded_t decoded;
    ctool_x86_encoding_t reencoded;
    ctool_status_t status = ctool_x86_decode(
        job, CTOOL_X86_MODE_32, ctool_bytes(gnu_bytes[index], 3u), 0u,
        &decoded);
    if (!check_status(status, CTOOL_OK, "GNU string prefix order") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                        decoded.instruction.mnemonic == mnemonics[index] &&
                        decoded.instruction.operand_bits == 16u &&
                        decoded.instruction.prefixes == CTOOL_X86_PREFIX_REP &&
                        decoded.consumed == 3u &&
                        memcmp(decoded.encoding.bytes, gnu_bytes[index], 3u) ==
                            0,
                    "GNU string prefix decode") ||
        !check_status(ctool_x86_encode(job, CTOOL_X86_MODE_32,
                                       &decoded.instruction,
                                       decoded.encoding.form, &reencoded),
                      CTOOL_OK, "GNU string prefix canonicalization") ||
        !bytes_equal(&reencoded, canonical_bytes[index], 3u,
                     "GNU string canonical prefix order")) {
      return 0;
    }
  }
  return 1;
}

static int run_active_surface(void) {
  static const active_vector_t vectors[] = {
      {"adc", CTOOL_X86_MODE_32, CTOOL_X86_MN_ADC, 2u, {0x11u, 0xd8u}},
      {"add", CTOOL_X86_MODE_32, CTOOL_X86_MN_ADD, 2u, {0x01u, 0xd8u}},
      {"addps", CTOOL_X86_MODE_32, CTOOL_X86_MN_ADDPS, 3u,
       {0x0fu, 0x58u, 0xc1u}},
      {"addss", CTOOL_X86_MODE_32, CTOOL_X86_MN_ADDSS, 4u,
       {0xf3u, 0x0fu, 0x58u, 0xc1u}},
      {"and", CTOOL_X86_MODE_32, CTOOL_X86_MN_AND, 2u, {0x21u, 0xd8u}},
      {"bswap", CTOOL_X86_MODE_32, CTOOL_X86_MN_BSWAP, 2u,
       {0x0fu, 0xc8u}},
      {"call", CTOOL_X86_MODE_32, CTOOL_X86_MN_CALL, 5u,
       {0xe8u, 0u, 0u, 0u, 0u}},
      {"clc", CTOOL_X86_MODE_32, CTOOL_X86_MN_CLC, 1u, {0xf8u}},
      {"cld", CTOOL_X86_MODE_32, CTOOL_X86_MN_CLD, 1u, {0xfcu}},
      {"cli", CTOOL_X86_MODE_32, CTOOL_X86_MN_CLI, 1u, {0xfau}},
      {"clts", CTOOL_X86_MODE_32, CTOOL_X86_MN_CLTS, 2u,
       {0x0fu, 0x06u}},
      {"cmp", CTOOL_X86_MODE_32, CTOOL_X86_MN_CMP, 2u, {0x39u, 0xd8u}},
      {"cmpxchg", CTOOL_X86_MODE_32, CTOOL_X86_MN_CMPXCHG, 3u,
       {0x0fu, 0xb1u, 0xc1u}},
      {"cpuid", CTOOL_X86_MODE_32, CTOOL_X86_MN_CPUID, 2u,
       {0x0fu, 0xa2u}},
      {"dec", CTOOL_X86_MODE_32, CTOOL_X86_MN_DEC, 1u, {0x49u}},
      {"div", CTOOL_X86_MODE_32, CTOOL_X86_MN_DIV, 2u, {0xf7u, 0xf1u}},
      {"finit", CTOOL_X86_MODE_32, CTOOL_X86_MN_FINIT, 3u,
       {0x9bu, 0xdbu, 0xe3u}},
      {"fld", CTOOL_X86_MODE_32, CTOOL_X86_MN_FLD, 2u, {0xd9u, 0u}},
      {"fld-m64", CTOOL_X86_MODE_32, CTOOL_X86_MN_FLD, 2u,
       {0xddu, 0u}},
      {"fninit", CTOOL_X86_MODE_32, CTOOL_X86_MN_FNINIT, 2u,
       {0xdbu, 0xe3u}},
      {"fsin", CTOOL_X86_MODE_32, CTOOL_X86_MN_FSIN, 2u,
       {0xd9u, 0xfeu}},
      {"fstp", CTOOL_X86_MODE_32, CTOOL_X86_MN_FSTP, 2u,
       {0xd9u, 0x18u}},
      {"fstp-m64", CTOOL_X86_MODE_32, CTOOL_X86_MN_FSTP, 2u,
       {0xddu, 0x18u}},
      {"fstp-st0", CTOOL_X86_MODE_32, CTOOL_X86_MN_FSTP, 2u,
       {0xddu, 0xd8u}},
      {"fstp-st1", CTOOL_X86_MODE_32, CTOOL_X86_MN_FSTP, 2u,
       {0xddu, 0xd9u}},
      {"fsubr-st1-st0", CTOOL_X86_MODE_32, CTOOL_X86_MN_FSUBR, 2u,
       {0xdcu, 0xe1u}},
      {"fwait", CTOOL_X86_MODE_32, CTOOL_X86_MN_FWAIT, 1u, {0x9bu}},
      {"fxrstor", CTOOL_X86_MODE_32, CTOOL_X86_MN_FXRSTOR, 3u,
       {0x0fu, 0xaeu, 0x08u}},
      {"fxsave", CTOOL_X86_MODE_32, CTOOL_X86_MN_FXSAVE, 3u,
       {0x0fu, 0xaeu, 0u}},
      {"hlt", CTOOL_X86_MODE_32, CTOOL_X86_MN_HLT, 1u, {0xf4u}},
      {"in", CTOOL_X86_MODE_32, CTOOL_X86_MN_IN, 1u, {0xedu}},
      {"inc", CTOOL_X86_MODE_32, CTOOL_X86_MN_INC, 1u, {0x41u}},
      {"int", CTOOL_X86_MODE_16, CTOOL_X86_MN_INT, 2u, {0xcdu, 0x13u}},
      {"invd", CTOOL_X86_MODE_32, CTOOL_X86_MN_INVD, 2u,
       {0x0fu, 0x08u}},
      {"invlpg", CTOOL_X86_MODE_32, CTOOL_X86_MN_INVLPG, 3u,
       {0x0fu, 0x01u, 0x38u}},
      {"iret", CTOOL_X86_MODE_32, CTOOL_X86_MN_IRET, 1u, {0xcfu}},
      {"jb", CTOOL_X86_MODE_32, CTOOL_X86_MN_JB, 2u, {0x72u, 0u}},
      {"jbe", CTOOL_X86_MODE_32, CTOOL_X86_MN_JBE, 2u, {0x76u, 0u}},
      {"je", CTOOL_X86_MODE_32, CTOOL_X86_MN_JE, 2u, {0x74u, 0u}},
      {"jge", CTOOL_X86_MODE_32, CTOOL_X86_MN_JGE, 2u, {0x7du, 0u}},
      {"jl", CTOOL_X86_MODE_32, CTOOL_X86_MN_JL, 2u, {0x7cu, 0u}},
      {"jle", CTOOL_X86_MODE_32, CTOOL_X86_MN_JLE, 2u, {0x7eu, 0u}},
      {"jmp", CTOOL_X86_MODE_32, CTOOL_X86_MN_JMP, 2u, {0xebu, 0u}},
      {"jae", CTOOL_X86_MODE_32, CTOOL_X86_MN_JAE, 2u, {0x73u, 0u}},
      {"jne", CTOOL_X86_MODE_32, CTOOL_X86_MN_JNE, 2u, {0x75u, 0u}},
      {"jp", CTOOL_X86_MODE_32, CTOOL_X86_MN_JP, 2u, {0x7au, 0u}},
      {"jnp", CTOOL_X86_MODE_32, CTOOL_X86_MN_JNP, 2u, {0x7bu, 0u}},
      {"ldmxcsr", CTOOL_X86_MODE_32, CTOOL_X86_MN_LDMXCSR, 3u,
       {0x0fu, 0xaeu, 0x10u}},
      {"lgdt", CTOOL_X86_MODE_32, CTOOL_X86_MN_LGDT, 3u,
       {0x0fu, 0x01u, 0x10u}},
      {"lidt", CTOOL_X86_MODE_32, CTOOL_X86_MN_LIDT, 3u,
       {0x0fu, 0x01u, 0x18u}},
      {"lmsw", CTOOL_X86_MODE_32, CTOOL_X86_MN_LMSW, 3u,
       {0x0fu, 0x01u, 0xf0u}},
      {"ltr", CTOOL_X86_MODE_32, CTOOL_X86_MN_LTR, 3u,
       {0x0fu, 0u, 0xd8u}},
      {"mov", CTOOL_X86_MODE_32, CTOOL_X86_MN_MOV, 2u, {0x89u, 0xd8u}},
      {"movss", CTOOL_X86_MODE_32, CTOOL_X86_MN_MOVSS, 4u,
       {0xf3u, 0x0fu, 0x10u, 0xc1u}},
      {"movss-store-register", CTOOL_X86_MODE_32, CTOOL_X86_MN_MOVSS, 4u,
       {0xf3u, 0x0fu, 0x11u, 0xc1u}},
      {"movups", CTOOL_X86_MODE_32, CTOOL_X86_MN_MOVUPS, 3u,
       {0x0fu, 0x10u, 0xc1u}},
      {"movups-store-register", CTOOL_X86_MODE_32, CTOOL_X86_MN_MOVUPS, 3u,
       {0x0fu, 0x11u, 0xc1u}},
      {"movzx", CTOOL_X86_MODE_32, CTOOL_X86_MN_MOVZX, 3u,
       {0x0fu, 0xb7u, 0xc1u}},
      {"mul", CTOOL_X86_MODE_32, CTOOL_X86_MN_MUL, 2u, {0xf7u, 0xe3u}},
      {"mulps", CTOOL_X86_MODE_32, CTOOL_X86_MN_MULPS, 3u,
       {0x0fu, 0x59u, 0xc1u}},
      {"or", CTOOL_X86_MODE_32, CTOOL_X86_MN_OR, 2u, {0x09u, 0xd8u}},
      {"out", CTOOL_X86_MODE_16, CTOOL_X86_MN_OUT, 1u, {0xefu}},
      {"pop", CTOOL_X86_MODE_32, CTOOL_X86_MN_POP, 1u, {0x58u}},
      {"popa", CTOOL_X86_MODE_32, CTOOL_X86_MN_POPA, 1u, {0x61u}},
      {"popf", CTOOL_X86_MODE_32, CTOOL_X86_MN_POPF, 1u, {0x9du}},
      {"push", CTOOL_X86_MODE_32, CTOOL_X86_MN_PUSH, 1u, {0x50u}},
      {"pusha", CTOOL_X86_MODE_32, CTOOL_X86_MN_PUSHA, 1u, {0x60u}},
      {"pushf", CTOOL_X86_MODE_32, CTOOL_X86_MN_PUSHF, 1u, {0x9cu}},
      {"rdmsr", CTOOL_X86_MODE_32, CTOOL_X86_MN_RDMSR, 2u,
       {0x0fu, 0x32u}},
      {"rdtsc", CTOOL_X86_MODE_32, CTOOL_X86_MN_RDTSC, 2u,
       {0x0fu, 0x31u}},
      {"ret", CTOOL_X86_MODE_32, CTOOL_X86_MN_RET, 1u, {0xc3u}},
      {"retf", CTOOL_X86_MODE_32, CTOOL_X86_MN_RETF, 1u, {0xcbu}},
      {"sgdt", CTOOL_X86_MODE_32, CTOOL_X86_MN_SGDT, 3u,
       {0x0fu, 0x01u, 0u}},
      {"shl", CTOOL_X86_MODE_32, CTOOL_X86_MN_SHL, 3u,
       {0xc1u, 0xe0u, 7u}},
      {"shr", CTOOL_X86_MODE_32, CTOOL_X86_MN_SHR, 3u,
       {0xc1u, 0xe8u, 24u}},
      {"sidt", CTOOL_X86_MODE_32, CTOOL_X86_MN_SIDT, 3u,
       {0x0fu, 0x01u, 0x08u}},
      {"sldt", CTOOL_X86_MODE_32, CTOOL_X86_MN_SLDT, 4u,
       {0x66u, 0x0fu, 0u, 0xc0u}},
      {"smsw", CTOOL_X86_MODE_32, CTOOL_X86_MN_SMSW, 4u,
       {0x66u, 0x0fu, 0x01u, 0xe0u}},
      {"sqrtss", CTOOL_X86_MODE_32, CTOOL_X86_MN_SQRTSS, 4u,
       {0xf3u, 0x0fu, 0x51u, 0xc0u}},
      {"stc", CTOOL_X86_MODE_32, CTOOL_X86_MN_STC, 1u, {0xf9u}},
      {"sti", CTOOL_X86_MODE_32, CTOOL_X86_MN_STI, 1u, {0xfbu}},
      {"stmxcsr", CTOOL_X86_MODE_32, CTOOL_X86_MN_STMXCSR, 3u,
       {0x0fu, 0xaeu, 0x18u}},
      {"str", CTOOL_X86_MODE_32, CTOOL_X86_MN_STR, 4u,
       {0x66u, 0x0fu, 0u, 0xc8u}},
      {"sub", CTOOL_X86_MODE_32, CTOOL_X86_MN_SUB, 2u, {0x29u, 0xd8u}},
      {"syscall", CTOOL_X86_MODE_32, CTOOL_X86_MN_SYSCALL, 2u,
       {0x0fu, 0x05u}},
      {"sysenter", CTOOL_X86_MODE_32, CTOOL_X86_MN_SYSENTER, 2u,
       {0x0fu, 0x34u}},
      {"sysexit", CTOOL_X86_MODE_32, CTOOL_X86_MN_SYSEXIT, 2u,
       {0x0fu, 0x35u}},
      {"test", CTOOL_X86_MODE_32, CTOOL_X86_MN_TEST, 2u, {0x85u, 0xc0u}},
      {"wbinvd", CTOOL_X86_MODE_32, CTOOL_X86_MN_WBINVD, 2u,
       {0x0fu, 0x09u}},
      {"wrmsr", CTOOL_X86_MODE_32, CTOOL_X86_MN_WRMSR, 2u,
       {0x0fu, 0x30u}},
      {"xadd", CTOOL_X86_MODE_32, CTOOL_X86_MN_XADD, 3u,
       {0x0fu, 0xc1u, 0xc3u}},
      {"xor", CTOOL_X86_MODE_32, CTOOL_X86_MN_XOR, 2u, {0x31u, 0xd8u}},
      {"moffs16", CTOOL_X86_MODE_16, CTOOL_X86_MN_MOV, 4u,
       {0x66u, 0xa1u, 0u, 0x05u}},
      {"absolute-rm32", CTOOL_X86_MODE_32, CTOOL_X86_MN_MOV, 7u,
       {0xc6u, 0x05u, 0u, 0x80u, 0x0bu, 0u, 0x50u}},
      {"far-jump", CTOOL_X86_MODE_16, CTOOL_X86_MN_JMP, 5u,
       {0xeau, 0x34u, 0x12u, 0u, 0u}},
      {"push-ds", CTOOL_X86_MODE_32, CTOOL_X86_MN_PUSH, 1u, {0x1eu}},
      {"pop-ds", CTOOL_X86_MODE_32, CTOOL_X86_MN_POP, 1u, {0x1fu}},
      {"lock-inc", CTOOL_X86_MODE_32, CTOOL_X86_MN_INC, 3u,
       {0xf0u, 0xffu, 0u}},
      {"lock-cmpxchg", CTOOL_X86_MODE_32, CTOOL_X86_MN_CMPXCHG, 4u,
       {0xf0u, 0x0fu, 0xb1u, 0x18u}},
      {"control-mov", CTOOL_X86_MODE_32, CTOOL_X86_MN_MOV, 3u,
       {0x0fu, 0x22u, 0xc0u}},
      {"cdq", CTOOL_X86_MODE_32, CTOOL_X86_MN_CDQ, 1u, {0x99u}},
      {"lea", CTOOL_X86_MODE_32, CTOOL_X86_MN_LEA, 3u,
       {0x8du, 0x45u, 0xfcu}},
      {"sar-cl", CTOOL_X86_MODE_32, CTOOL_X86_MN_SAR, 2u,
       {0xd3u, 0xf8u}},
      {"sete", CTOOL_X86_MODE_32, CTOOL_X86_MN_SETE, 3u,
       {0x0fu, 0x94u, 0xc0u}},
      {"setne", CTOOL_X86_MODE_32, CTOOL_X86_MN_SETNE, 3u,
       {0x0fu, 0x95u, 0xc0u}},
      {"setl", CTOOL_X86_MODE_32, CTOOL_X86_MN_SETL, 3u,
       {0x0fu, 0x9cu, 0xc0u}},
      {"setg", CTOOL_X86_MODE_32, CTOOL_X86_MN_SETG, 3u,
       {0x0fu, 0x9fu, 0xc0u}},
      {"setle", CTOOL_X86_MODE_32, CTOOL_X86_MN_SETLE, 3u,
       {0x0fu, 0x9eu, 0xc0u}},
      {"setge", CTOOL_X86_MODE_32, CTOOL_X86_MN_SETGE, 3u,
       {0x0fu, 0x9du, 0xc0u}},
      {"movsx", CTOOL_X86_MODE_32, CTOOL_X86_MN_MOVSX, 3u,
       {0x0fu, 0xbeu, 0xc0u}},
      {"movaps", CTOOL_X86_MODE_32, CTOOL_X86_MN_MOVAPS, 3u,
       {0x0fu, 0x28u, 0xc1u}},
      {"movaps-store-register", CTOOL_X86_MODE_32, CTOOL_X86_MN_MOVAPS, 3u,
       {0x0fu, 0x29u, 0xc1u}},
      {"movsd-sse", CTOOL_X86_MODE_32, CTOOL_X86_MN_MOVSD, 4u,
       {0xf2u, 0x0fu, 0x10u, 0xc1u}},
      {"movsd-store-register", CTOOL_X86_MODE_32, CTOOL_X86_MN_MOVSD, 4u,
       {0xf2u, 0x0fu, 0x11u, 0xc1u}},
      {"movapd-store-register", CTOOL_X86_MODE_32, CTOOL_X86_MN_MOVAPD, 4u,
       {0x66u, 0x0fu, 0x29u, 0xc1u}},
      {"movupd-store-register", CTOOL_X86_MODE_32, CTOOL_X86_MN_MOVUPD, 4u,
       {0x66u, 0x0fu, 0x11u, 0xc1u}},
      {"movdqa-store-register", CTOOL_X86_MODE_32, CTOOL_X86_MN_MOVDQA, 4u,
       {0x66u, 0x0fu, 0x7fu, 0xc1u}},
      {"movdqu-store-register", CTOOL_X86_MODE_32, CTOOL_X86_MN_MOVDQU, 4u,
       {0xf3u, 0x0fu, 0x7fu, 0xc1u}},
      {"addsd", CTOOL_X86_MODE_32, CTOOL_X86_MN_ADDSD, 4u,
       {0xf2u, 0x0fu, 0x58u, 0xc1u}},
      {"subss", CTOOL_X86_MODE_32, CTOOL_X86_MN_SUBSS, 4u,
       {0xf3u, 0x0fu, 0x5cu, 0xc1u}},
      {"subsd", CTOOL_X86_MODE_32, CTOOL_X86_MN_SUBSD, 4u,
       {0xf2u, 0x0fu, 0x5cu, 0xc1u}},
      {"mulss", CTOOL_X86_MODE_32, CTOOL_X86_MN_MULSS, 4u,
       {0xf3u, 0x0fu, 0x59u, 0xc1u}},
      {"mulsd", CTOOL_X86_MODE_32, CTOOL_X86_MN_MULSD, 4u,
       {0xf2u, 0x0fu, 0x59u, 0xc1u}},
      {"divss", CTOOL_X86_MODE_32, CTOOL_X86_MN_DIVSS, 4u,
       {0xf3u, 0x0fu, 0x5eu, 0xc1u}},
      {"divsd", CTOOL_X86_MODE_32, CTOOL_X86_MN_DIVSD, 4u,
       {0xf2u, 0x0fu, 0x5eu, 0xc1u}},
      {"cvtsi2ss", CTOOL_X86_MODE_32, CTOOL_X86_MN_CVTSI2SS, 4u,
       {0xf3u, 0x0fu, 0x2au, 0xc0u}},
      {"cvtsi2sd", CTOOL_X86_MODE_32, CTOOL_X86_MN_CVTSI2SD, 4u,
       {0xf2u, 0x0fu, 0x2au, 0xc0u}},
      {"cvttss2si", CTOOL_X86_MODE_32, CTOOL_X86_MN_CVTTSS2SI, 4u,
       {0xf3u, 0x0fu, 0x2cu, 0xc0u}},
      {"cvttsd2si", CTOOL_X86_MODE_32, CTOOL_X86_MN_CVTTSD2SI, 4u,
       {0xf2u, 0x0fu, 0x2cu, 0xc0u}},
      {"cvtss2sd", CTOOL_X86_MODE_32, CTOOL_X86_MN_CVTSS2SD, 4u,
       {0xf3u, 0x0fu, 0x5au, 0xc1u}},
      {"cvtsd2ss", CTOOL_X86_MODE_32, CTOOL_X86_MN_CVTSD2SS, 4u,
       {0xf2u, 0x0fu, 0x5au, 0xc1u}},
      {"addpd", CTOOL_X86_MODE_32, CTOOL_X86_MN_ADDPD, 4u,
       {0x66u, 0x0fu, 0x58u, 0xc1u}},
      {"subps", CTOOL_X86_MODE_32, CTOOL_X86_MN_SUBPS, 3u,
       {0x0fu, 0x5cu, 0xc1u}},
      {"mulpd", CTOOL_X86_MODE_32, CTOOL_X86_MN_MULPD, 4u,
       {0x66u, 0x0fu, 0x59u, 0xc1u}},
      {"divps", CTOOL_X86_MODE_32, CTOOL_X86_MN_DIVPS, 3u,
       {0x0fu, 0x5eu, 0xc1u}},
      {"sqrtps", CTOOL_X86_MODE_32, CTOOL_X86_MN_SQRTPS, 3u,
       {0x0fu, 0x51u, 0xc1u}},
      {"andps", CTOOL_X86_MODE_32, CTOOL_X86_MN_ANDPS, 3u,
       {0x0fu, 0x54u, 0xc1u}},
      {"orps", CTOOL_X86_MODE_32, CTOOL_X86_MN_ORPS, 3u,
       {0x0fu, 0x56u, 0xc1u}},
      {"xorps", CTOOL_X86_MODE_32, CTOOL_X86_MN_XORPS, 3u,
       {0x0fu, 0x57u, 0xc1u}},
      {"orpd", CTOOL_X86_MODE_32, CTOOL_X86_MN_ORPD, 4u,
       {0x66u, 0x0fu, 0x56u, 0xc1u}},
      {"xorpd", CTOOL_X86_MODE_32, CTOOL_X86_MN_XORPD, 4u,
       {0x66u, 0x0fu, 0x57u, 0xc1u}},
      {"cmpps", CTOOL_X86_MODE_32, CTOOL_X86_MN_CMPPS, 4u,
       {0x0fu, 0xc2u, 0xc1u, 1u}},
      {"movmskps", CTOOL_X86_MODE_32, CTOOL_X86_MN_MOVMSKPS, 3u,
       {0x0fu, 0x50u, 0xc1u}},
      {"shufps", CTOOL_X86_MODE_32, CTOOL_X86_MN_SHUFPS, 4u,
       {0x0fu, 0xc6u, 0xc1u, 0x1bu}},
      {"shufpd", CTOOL_X86_MODE_32, CTOOL_X86_MN_SHUFPD, 5u,
       {0x66u, 0x0fu, 0xc6u, 0xc1u, 1u}},
      {"psrlw-register", CTOOL_X86_MODE_32, CTOOL_X86_MN_PSRLW, 5u,
       {0x66u, 0x0fu, 0x71u, 0xd0u, 4u}},
      {"pause", CTOOL_X86_MODE_32, CTOOL_X86_MN_PAUSE, 2u,
       {0xf3u, 0x90u}},
      {"rep-movsd", CTOOL_X86_MODE_32, CTOOL_X86_MN_MOVSD, 2u,
       {0xf3u, 0xa5u}},
      {"iretd-mode16", CTOOL_X86_MODE_16, CTOOL_X86_MN_IRETD, 2u,
       {0x66u, 0xcfu}},
      {"pushad-mode16", CTOOL_X86_MODE_16, CTOOL_X86_MN_PUSHAD, 2u,
       {0x66u, 0x60u}},
      {"popad-mode16", CTOOL_X86_MODE_16, CTOOL_X86_MN_POPAD, 2u,
       {0x66u, 0x61u}},
      {"pushfd-mode16", CTOOL_X86_MODE_16, CTOOL_X86_MN_PUSHFD, 2u,
       {0x66u, 0x9cu}},
      {"popfd-mode16", CTOOL_X86_MODE_16, CTOOL_X86_MN_POPFD, 2u,
       {0x66u, 0x9du}}};
#define X86_ACTIVE_CASE(name, mode, mnemonic, size, ...)                    \
  {(name), (mode), (mnemonic), (size), __VA_ARGS__},
  static const active_vector_t source_cases[] = {
#include "x86_active_cases.inc"
  };
  static const active_vector_t inline_cases[] = {
#include "x86_inline_cases.inc"
  };
#undef X86_ACTIVE_CASE
  ctool_host_adapter_t adapter;
  ctool_job_t *job;
  if (!open_job(&adapter, &job)) {
    return 1;
  }
  if (!check_true(
          (ctool_u32)(sizeof(source_cases) / sizeof(source_cases[0])) ==
              182u,
          "active source manifest inventory") ||
      !check_true(
          (ctool_u32)(sizeof(inline_cases) / sizeof(inline_cases[0])) ==
              129u,
          "active inline manifest inventory") ||
      !check_active_vectors(
          job, vectors,
          (ctool_u32)(sizeof(vectors) / sizeof(vectors[0]))) ||
      !check_active_vectors(
          job, source_cases,
          (ctool_u32)(sizeof(source_cases) / sizeof(source_cases[0]))) ||
      !check_active_vectors(
          job, inline_cases,
          (ctool_u32)(sizeof(inline_cases) / sizeof(inline_cases[0]))) ||
      !check_gnu_string_prefix_order(job)) {
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)printf("active-surface: ok\n");
  return 0;
}

static int run_errors(void) {
  static const ctool_u8 truncated[] = {0x0fu};
  static const ctool_u8 unknown[] = {0xd6u};
  static const ctool_u8 unknown_psraw[] = {0x66u, 0x0fu, 0x71u, 0xe0u,
                                           0x04u};
  static const ctool_u8 unknown_lldt[] = {0x0fu, 0x00u, 0xd0u};
  static const ctool_u8 unknown_cvtpd2ps[] = {0x66u, 0x0fu, 0x5au, 0xc1u};
  static const ctool_u8 unknown_fnop[] = {0xd9u, 0xd0u};
  static const ctool_u8 lock_nop[] = {0xf0u, 0x90u};
  static const ctool_u8 lock_register[] = {0xf0u, 0x01u, 0xd8u};
  static const ctool_u8 reserved_group[] = {0x0fu, 0x00u, 0xf0u};
  static const ctool_u8 mov_cs[] = {0x8eu, 0xc8u};
  static const ctool_u8 mov_cr1[] = {0x0fu, 0x22u, 0xc8u};
  static const ctool_u8 invlpg_register[] = {0x0fu, 0x01u, 0xf8u};
  static const ctool_u8 duplicate_prefix[] = {0x66u, 0x66u, 0x90u};
  static const ctool_u8 psrlw_memory[] = {0x66u, 0x0fu, 0x71u, 0x10u, 4u};
  static const ctool_u8 trunc_mov[] = {0xb8u, 0x78u, 0x56u, 0x34u, 0x12u};
  static const ctool_u8 trunc_sib[] = {0x8bu, 0x44u, 0x8bu, 0x10u};
  static const ctool_u8 trunc_sse[] = {0xf3u, 0x0fu, 0x10u, 0u};
  static const ctool_u8 trunc_far[] = {0xeau, 0x78u, 0x56u, 0x34u,
                                       0x12u, 8u, 0u};
  static const ctool_u8 trunc_group[] = {0x81u, 0xc0u, 0x78u, 0x56u,
                                         0x34u, 0x12u};
  static const struct {
    const char *name;
    const ctool_u8 *bytes;
    ctool_u32 size;
  } invalid_vectors[] = {
      {"lock nop", lock_nop, (ctool_u32)sizeof(lock_nop)},
      {"lock register", lock_register,
       (ctool_u32)sizeof(lock_register)},
      {"reserved group", reserved_group,
       (ctool_u32)sizeof(reserved_group)},
      {"mov cs", mov_cs, (ctool_u32)sizeof(mov_cs)},
      {"mov cr1", mov_cr1, (ctool_u32)sizeof(mov_cr1)},
      {"invlpg register", invlpg_register,
       (ctool_u32)sizeof(invlpg_register)},
      {"duplicate prefix", duplicate_prefix,
       (ctool_u32)sizeof(duplicate_prefix)},
      {"psrlw memory", psrlw_memory, (ctool_u32)sizeof(psrlw_memory)}};
  static const struct {
    const ctool_u8 *bytes;
    ctool_u32 size;
  } truncation_vectors[] = {
      {trunc_mov, (ctool_u32)sizeof(trunc_mov)},
      {trunc_sib, (ctool_u32)sizeof(trunc_sib)},
      {trunc_sse, (ctool_u32)sizeof(trunc_sse)},
      {trunc_far, (ctool_u32)sizeof(trunc_far)},
      {trunc_group, (ctool_u32)sizeof(trunc_group)}};
  static const struct {
    const char *name;
    const ctool_u8 *bytes;
    ctool_u32 size;
  } unknown_vectors[] = {
      {"unknown opcode", unknown, (ctool_u32)sizeof(unknown)},
      {"unsupported PSRAW", unknown_psraw,
       (ctool_u32)sizeof(unknown_psraw)},
      {"unsupported LLDT", unknown_lldt,
       (ctool_u32)sizeof(unknown_lldt)},
      {"unsupported CVTPD2PS", unknown_cvtpd2ps,
       (ctool_u32)sizeof(unknown_cvtpd2ps)},
      {"unsupported FNOP", unknown_fnop,
       (ctool_u32)sizeof(unknown_fnop)}};
  ctool_host_adapter_t adapter;
  ctool_job_t *job;
  ctool_x86_instruction_t insn;
  ctool_x86_encoding_t encoding;
  ctool_x86_decoded_t decoded;
  ctool_status_t status;
  ctool_u32 vector_index;
  ctool_u32 cut;
  if (!open_job(&adapter, &job)) {
    return 1;
  }
  insn = instruction(CTOOL_X86_MN_MOV, 32u, 32u, 0u);
  insn.operand_count = 2u;
  insn.operands[0] = register_operand(CTOOL_X86_REG_GPR32, 8u);
  insn.operands[1] = register_operand(CTOOL_X86_REG_GPR32, 0u);
  (void)memset(&encoding, 0xa5, sizeof(encoding));
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn,
                            CTOOL_X86_FORM_AUTO, &encoding);
  if (!check_status(status, CTOOL_ERR_INPUT, "invalid register") ||
      !check_true(encoding_is_zero(&encoding),
                  "invalid register zeroed output")) {
    ctool_job_close(job);
    return 1;
  }
  insn.operands[0] = register_operand(CTOOL_X86_REG_GPR32, 0u);
  insn.operands[1] = memory_operand(
      32u, 32u, reg(CTOOL_X86_REG_NONE, 0u),
      reg(CTOOL_X86_REG_GPR32, 3u), reg(CTOOL_X86_REG_GPR32, 1u),
      3u, 0, 0u);
  (void)memset(&encoding, 0xa5, sizeof(encoding));
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn,
                            CTOOL_X86_FORM_AUTO, &encoding);
  if (!check_status(status, CTOOL_ERR_INPUT, "invalid scale") ||
      !check_true(encoding_is_zero(&encoding),
                  "invalid scale zeroed output")) {
    ctool_job_close(job);
    return 1;
  }
  insn.operands[1] = memory_operand(
      32u, 32u, reg(CTOOL_X86_REG_NONE, 0u),
      reg(CTOOL_X86_REG_NONE, 0u), reg(CTOOL_X86_REG_NONE, 0u), 2u, 0, 32u);
  (void)memset(&encoding, 0xa5, sizeof(encoding));
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn,
                            CTOOL_X86_FORM_AUTO, &encoding);
  if (!check_status(status, CTOOL_ERR_INPUT, "meaningless scale") ||
      !check_true(encoding_is_zero(&encoding),
                  "meaningless scale zeroed output")) {
    ctool_job_close(job);
    return 1;
  }
  insn.operands[1] = memory_operand(
      32u, 32u, reg(CTOOL_X86_REG_NONE, 0u),
      reg(CTOOL_X86_REG_GPR32, 3u), reg(CTOOL_X86_REG_NONE, 0u), 1u, 128,
      8u);
  (void)memset(&encoding, 0xa5, sizeof(encoding));
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn,
                            CTOOL_X86_FORM_AUTO, &encoding);
  if (!check_status(status, CTOOL_ERR_INPUT, "disp8 overflow") ||
      !check_true(encoding_is_zero(&encoding),
                  "disp8 overflow zeroed output")) {
    ctool_job_close(job);
    return 1;
  }
  insn = instruction(CTOOL_X86_MN_MOV, 32u, 32u,
                     CTOOL_X86_PREFIX_LOCK);
  insn.operand_count = 2u;
  insn.operands[0] = register_operand(CTOOL_X86_REG_GPR32, 0u);
  insn.operands[1] = register_operand(CTOOL_X86_REG_GPR32, 3u);
  (void)memset(&encoding, 0xa5, sizeof(encoding));
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn,
                            CTOOL_X86_FORM_AUTO, &encoding);
  if (!check_status(status, CTOOL_ERR_INPUT, "illegal lock") ||
      !check_true(encoding_is_zero(&encoding),
                  "illegal lock zeroed output")) {
    ctool_job_close(job);
    return 1;
  }
  insn = instruction(CTOOL_X86_MN_MOV, 16u, 32u, 0u);
  insn.operand_count = 2u;
  insn.operands[0] = register_operand(CTOOL_X86_REG_SEGMENT, 1u);
  insn.operands[1] = register_operand(CTOOL_X86_REG_GPR16, 0u);
  (void)memset(&encoding, 0xa5, sizeof(encoding));
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn,
                            CTOOL_X86_FORM_AUTO, &encoding);
  if (!check_status(status, CTOOL_ERR_INPUT, "mov cs destination") ||
      !check_true(encoding_is_zero(&encoding),
                  "mov cs zeroed output")) {
    ctool_job_close(job);
    return 1;
  }
  insn = instruction(CTOOL_X86_MN_MOV, 32u, 32u, 0u);
  insn.operand_count = 2u;
  insn.operands[0] = register_operand(CTOOL_X86_REG_CONTROL, 1u);
  insn.operands[1] = register_operand(CTOOL_X86_REG_GPR32, 0u);
  (void)memset(&encoding, 0xa5, sizeof(encoding));
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn,
                            CTOOL_X86_FORM_AUTO, &encoding);
  if (!check_status(status, CTOOL_ERR_INPUT, "reserved control register") ||
      !check_true(encoding_is_zero(&encoding),
                  "reserved control register zeroed output")) {
    ctool_job_close(job);
    return 1;
  }
  insn = instruction(CTOOL_X86_MN_PSRLW, 0u, 32u, 0u);
  insn.operand_count = 2u;
  insn.operands[0] = memory_operand(
      128u, 32u, reg(CTOOL_X86_REG_NONE, 0u),
      reg(CTOOL_X86_REG_GPR32, 0u), reg(CTOOL_X86_REG_NONE, 0u),
      1u, 0, 0u);
  insn.operands[1] = value_operand(CTOOL_X86_OPERAND_IMMEDIATE, 8u, 8u,
                                   constant(4u));
  (void)memset(&encoding, 0xa5, sizeof(encoding));
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn,
                            CTOOL_X86_FORM_AUTO, &encoding);
  if (!check_status(status, CTOOL_ERR_INPUT, "psrlw memory") ||
      !check_true(encoding_is_zero(&encoding),
                  "psrlw memory zeroed output")) {
    ctool_job_close(job);
    return 1;
  }
  (void)memset(&encoding, 0xa5, sizeof(encoding));
  status = ctool_x86_encode(job, (ctool_x86_mode_t)64, &insn,
                            CTOOL_X86_FORM_AUTO, &encoding);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT, "invalid mode") ||
      !check_true(encoding_is_zero(&encoding),
                  "invalid mode zeroed output")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                            ctool_bytes(truncated,
                                        (ctool_u32)sizeof(truncated)),
                            0u,
                            &decoded);
  if (!check_status(status, CTOOL_OK, "truncated decode") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_TRUNCATED &&
                      decoded.consumed == 0u &&
                      decoded.encoding.size == sizeof(truncated) &&
                      decoded.encoding.bytes[0] == truncated[0],
                  "truncated classification")) {
    ctool_job_close(job);
    return 1;
  }
  for (vector_index = 0u;
       vector_index <
       (ctool_u32)(sizeof(truncation_vectors) /
                   sizeof(truncation_vectors[0]));
       vector_index++) {
    for (cut = 1u; cut < truncation_vectors[vector_index].size; cut++) {
      status = ctool_x86_decode(
          job, CTOOL_X86_MODE_32,
          ctool_bytes(truncation_vectors[vector_index].bytes, cut), 0u,
          &decoded);
      if (!check_status(status, CTOOL_OK, "truncation boundary") ||
          !check_true(decoded.kind == CTOOL_X86_DECODE_TRUNCATED &&
                          decoded.consumed == 0u &&
                          decoded.encoding.size == cut &&
                          memcmp(decoded.encoding.bytes,
                                 truncation_vectors[vector_index].bytes,
                                 cut) == 0,
                      "truncation boundary retention")) {
        ctool_job_close(job);
        return 1;
      }
    }
  }
  for (vector_index = 0u;
       vector_index <
       (ctool_u32)(sizeof(invalid_vectors) / sizeof(invalid_vectors[0]));
       vector_index++) {
    status = ctool_x86_decode(
        job, CTOOL_X86_MODE_32,
        ctool_bytes(invalid_vectors[vector_index].bytes,
                    invalid_vectors[vector_index].size),
        0u, &decoded);
    if (!check_status(status, CTOOL_OK, invalid_vectors[vector_index].name) ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_INVALID &&
                        decoded.consumed == 1u &&
                        decoded.encoding.size == 1u &&
                        decoded.encoding.bytes[0] ==
                            invalid_vectors[vector_index].bytes[0],
                    invalid_vectors[vector_index].name)) {
      ctool_job_close(job);
      return 1;
    }
  }
  (void)memset(&decoded, 0xa5, sizeof(decoded));
  status = ctool_x86_decode(job, (ctool_x86_mode_t)64,
                            ctool_bytes(unknown,
                                        (ctool_u32)sizeof(unknown)),
                            0u, &decoded);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "invalid decode mode") ||
      !check_true(decoded_is_zero(&decoded),
                  "invalid decode zeroed output")) {
    ctool_job_close(job);
    return 1;
  }
  for (vector_index = 0u;
       vector_index <
       (ctool_u32)(sizeof(unknown_vectors) / sizeof(unknown_vectors[0]));
       vector_index++) {
    status = ctool_x86_decode(
        job, CTOOL_X86_MODE_32,
        ctool_bytes(unknown_vectors[vector_index].bytes,
                    unknown_vectors[vector_index].size),
        0u, &decoded);
    if (!check_status(status, CTOOL_OK, unknown_vectors[vector_index].name) ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_UNKNOWN &&
                        decoded.consumed == 1u &&
                        decoded.encoding.size == 1u &&
                        decoded.encoding.bytes[0] ==
                            unknown_vectors[vector_index].bytes[0],
                    unknown_vectors[vector_index].name)) {
      ctool_job_close(job);
      return 1;
    }
  }
  ctool_job_close(job);
  (void)printf("errors: ok\n");
  return 0;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    (void)fprintf(stderr,
                  "usage: x86-contract inventory|model|integer|addressing|relocations|system-simd|active-surface|errors\n");
    return 2;
  }
  if (strcmp(argv[1], "model") == 0) {
    return run_model();
  }
  if (strcmp(argv[1], "inventory") == 0) {
    return run_inventory();
  }
  if (strcmp(argv[1], "integer") == 0) {
    return run_integer();
  }
  if (strcmp(argv[1], "addressing") == 0) {
    return run_addressing();
  }
  if (strcmp(argv[1], "relocations") == 0) {
    return run_relocations();
  }
  if (strcmp(argv[1], "system-simd") == 0) {
    return run_system_simd();
  }
  if (strcmp(argv[1], "active-surface") == 0) {
    return run_active_surface();
  }
  if (strcmp(argv[1], "errors") == 0) {
    return run_errors();
  }
  (void)fprintf(stderr, "unknown x86 contract mode: %s\n", argv[1]);
  return 2;
}
