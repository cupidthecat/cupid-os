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

static ctool_status_t exhaustive_decode(
    ctool_job_t *job, ctool_x86_mode_t mode, ctool_bytes_t bytes,
    ctool_u32 address, ctool_x86_decoded_t *decoded_out) {
  return ctool_x86_decode(job, mode, bytes, address, decoded_out);
}

static ctool_status_t decode_with_index_equivalence(
    ctool_job_t *job, ctool_x86_mode_t mode, ctool_bytes_t bytes,
    ctool_u32 address, ctool_x86_decoded_t *decoded_out) {
  ctool_arena_t *arena;
  ctool_arena_mark_t mark;
  const ctool_x86_decoder_t *decoder = (const ctool_x86_decoder_t *)0;
  ctool_x86_decoded_t exhaustive;
  ctool_x86_decoded_t indexed;
  ctool_status_t prepare_status;
  ctool_status_t exhaustive_status;
  ctool_status_t indexed_status;
  ctool_status_t rewind_status;
  if (job == (ctool_job_t *)0) {
    return exhaustive_decode(job, mode, bytes, address, decoded_out);
  }
  arena = ctool_job_arena(job);
  mark = ctool_arena_mark(arena);
  prepare_status = ctool_x86_decoder_prepare(job, &decoder);
  if (prepare_status != CTOOL_OK) {
    (void)fprintf(
        stderr,
        "x86 decoder preparation failed during equivalence check\n");
    return prepare_status;
  }
  (void)memset(&exhaustive, 0xa5, sizeof(exhaustive));
  (void)memset(&indexed, 0xa5, sizeof(indexed));
  exhaustive_status = exhaustive_decode(
      job, mode, bytes, address,
      decoded_out == (ctool_x86_decoded_t *)0 ? (ctool_x86_decoded_t *)0
                                               : &exhaustive);
  indexed_status = ctool_x86_decode_indexed(
      job, decoder, mode, bytes, address,
      decoded_out == (ctool_x86_decoded_t *)0 ? (ctool_x86_decoded_t *)0
                                               : &indexed);
  if (exhaustive_status != indexed_status ||
      (decoded_out != (ctool_x86_decoded_t *)0 &&
       memcmp(&exhaustive, &indexed, sizeof(exhaustive)) != 0)) {
    (void)fprintf(
        stderr,
        "prepared x86 decoder diverged from exhaustive decode\n");
    (void)ctool_arena_rewind(arena, mark);
    return CTOOL_ERR_INTERNAL;
  }
  if (decoded_out != (ctool_x86_decoded_t *)0) {
    *decoded_out = exhaustive;
  }
  rewind_status = ctool_arena_rewind(arena, mark);
  return rewind_status == CTOOL_OK ? exhaustive_status : rewind_status;
}

#define ctool_x86_decode decode_with_index_equivalence

static int compare_prepared_decode(
    ctool_job_t *job, const ctool_x86_decoder_t *decoder,
    ctool_x86_mode_t mode, ctool_bytes_t bytes, ctool_u32 address,
    const char *operation) {
  ctool_x86_decoded_t exhaustive;
  ctool_x86_decoded_t indexed;
  ctool_status_t exhaustive_status =
      exhaustive_decode(job, mode, bytes, address, &exhaustive);
  ctool_status_t indexed_status =
      ctool_x86_decode_indexed(job, decoder, mode, bytes, address, &indexed);
  if (exhaustive_status != indexed_status ||
      memcmp(&exhaustive, &indexed, sizeof(exhaustive)) != 0) {
    (void)fprintf(stderr, "%s: prepared decode mismatch\n", operation);
    return 0;
  }
  return 1;
}

static int run_decoder_index(void) {
  ctool_host_adapter_t owner_adapter;
  ctool_host_adapter_t caller_adapter;
  ctool_host_adapter_t limited_adapter;
  ctool_job_t *owner_job;
  ctool_job_t *caller_job;
  ctool_job_t *limited_job;
  const ctool_x86_decoder_t *decoder =
      (const ctool_x86_decoder_t *)(const void *)1;
  ctool_limits_t limited_limits = ctool_default_limits();
  ctool_job_config_t limited_config;
  ctool_x86_decoded_t decoded;
  ctool_u8 bytes[CTOOL_X86_MAX_INSTRUCTION_BYTES];
  void *recovery = (void *)0;
  ctool_status_t status;
  ctool_u32 mode_index;
  ctool_u32 opcode;
  ctool_u32 index;
  static const ctool_x86_mode_t modes[] = {
      CTOOL_X86_MODE_16, CTOOL_X86_MODE_32};

  status = ctool_x86_decoder_prepare((ctool_job_t *)0, &decoder);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "decoder prepare null job") ||
      !check_true(decoder == (const ctool_x86_decoder_t *)0,
                  "decoder prepare null job clears output") ||
      !open_job(&owner_adapter, &owner_job)) {
    return 1;
  }
  status = ctool_x86_decoder_prepare(
      owner_job, (const ctool_x86_decoder_t **)0);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "decoder prepare null output")) {
    ctool_job_close(owner_job);
    return 1;
  }
  status = ctool_x86_decoder_prepare(owner_job, &decoder);
  if (!check_status(status, CTOOL_OK, "decoder prepare") ||
      !check_true(decoder != (const ctool_x86_decoder_t *)0,
                  "decoder prepare output") ||
      !open_job(&caller_adapter, &caller_job)) {
    ctool_job_close(owner_job);
    return 1;
  }

  for (index = 0u; index < CTOOL_X86_MAX_INSTRUCTION_BYTES; index++) {
    bytes[index] = 0u;
  }
  bytes[1] = 0xc0u;
  for (mode_index = 0u;
       mode_index < (ctool_u32)(sizeof(modes) / sizeof(modes[0]));
       mode_index++) {
    for (opcode = 0u; opcode < 256u; opcode++) {
      bytes[0] = (ctool_u8)opcode;
      if (!compare_prepared_decode(
              caller_job, decoder, modes[mode_index],
              ctool_bytes(bytes, CTOOL_X86_MAX_INSTRUCTION_BYTES), 0u,
              "first-opcode bucket")) {
        ctool_job_close(caller_job);
        ctool_job_close(owner_job);
        return 1;
      }
    }
    bytes[0] = 0x0fu;
    for (opcode = 0u; opcode < 256u; opcode++) {
      bytes[1] = (ctool_u8)opcode;
      bytes[2] = 0xc0u;
      if (!compare_prepared_decode(
              caller_job, decoder, modes[mode_index],
              ctool_bytes(bytes, CTOOL_X86_MAX_INSTRUCTION_BYTES), 0u,
              "two-byte opcode bucket")) {
        ctool_job_close(caller_job);
        ctool_job_close(owner_job);
        return 1;
      }
    }
  }
  bytes[0] = 0x90u;
  (void)memset(&decoded, 0xa5, sizeof(decoded));
  status = ctool_x86_decode_indexed(
      caller_job, (const ctool_x86_decoder_t *)0, CTOOL_X86_MODE_32,
      ctool_bytes(bytes, 1u), 0u, &decoded);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "indexed decode null decoder") ||
      !check_true(decoded_is_zero(&decoded),
                  "indexed decode null decoder zeroes output")) {
    ctool_job_close(caller_job);
    ctool_job_close(owner_job);
    return 1;
  }
  (void)memset(&decoded, 0xa5, sizeof(decoded));
  status = ctool_x86_decode_indexed(
      caller_job, decoder, (ctool_x86_mode_t)64,
      ctool_bytes(bytes, 1u), 0u, &decoded);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "indexed decode invalid mode") ||
      !check_true(decoded_is_zero(&decoded),
                  "indexed decode invalid mode zeroes output") ||
      !check_status(ctool_x86_decode_indexed(
                        caller_job, decoder, CTOOL_X86_MODE_32,
                        ctool_bytes(bytes, 1u), 0u,
                        (ctool_x86_decoded_t *)0),
                    CTOOL_ERR_INVALID_ARGUMENT,
                    "indexed decode null output")) {
    ctool_job_close(caller_job);
    ctool_job_close(owner_job);
    return 1;
  }
  ctool_job_close(caller_job);

  limited_limits.arena_block_bytes = 64u;
  limited_limits.arena_bytes = 64u;
  status = ctool_host_adapter_init(&limited_adapter, ".");
  limited_config = ctool_host_job_config(&limited_adapter, limited_limits);
  if (!check_status(status, CTOOL_OK, "limited host adapter init") ||
      !check_status(ctool_job_open(&limited_config, &limited_job), CTOOL_OK,
                    "limited job open")) {
    ctool_job_close(owner_job);
    return 1;
  }
  decoder = (const ctool_x86_decoder_t *)(const void *)1;
  status = ctool_x86_decoder_prepare(limited_job, &decoder);
  if (!check_status(status, CTOOL_ERR_LIMIT, "decoder prepare limit") ||
      !check_true(decoder == (const ctool_x86_decoder_t *)0,
                  "decoder prepare limit clears output") ||
      !check_true(ctool_job_diagnostic_count(limited_job) == 1u &&
                      ctool_job_diagnostic(limited_job, 0u)->code ==
                          CTOOL_X86_DIAG_LIMIT,
                  "decoder prepare limit diagnostic") ||
      !check_status(ctool_arena_alloc(ctool_job_arena(limited_job), 32u, 4u,
                                      &recovery),
                    CTOOL_OK, "decoder prepare limit rewinds arena")) {
    ctool_job_close(limited_job);
    ctool_job_close(owner_job);
    return 1;
  }
  ctool_job_close(limited_job);
  ctool_job_close(owner_job);
  (void)printf("decoder-index: ok\n");
  return 0;
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
      "div",      "finit",    "fild",     "fistp",    "fld",
      "fldz",     "fninit",   "fsin",     "fstp",     "fwait",
      "fxrstor",  "fxsave",   "hlt",
      "in",       "inc",      "int",      "invd",     "invlpg",
      "iret",     "iretd",    "jb",       "jbe",      "jc",
      "je",       "jge",      "jl",       "jle",      "jmp",
      "jnc",      "jne",      "jng",      "jnl",      "jnz",
      "jpe",      "jpo",      "jz",       "ldmxcsr",  "lgdt",
      "lidt",     "lmsw",     "ltr",      "mov",      "movss",
      "movups",   "movzx",    "mul",      "mulps",    "or",
      "out",      "pop",      "popa",     "popfd",    "push",
      "pusha",    "pushfd",   "rdmsr",    "rdtsc",    "ret",
      "retf",     "sgdt",     "shl",      "shr",      "shrd",
      "sidt",
      "sldt",     "smsw",     "sqrtss",   "stc",      "sti",
      "stmxcsr",  "str",      "sub",      "syscall",  "sysenter",
      "sysexit",  "test",     "wbinvd",   "wrmsr",    "xadd",
      "xor",      "idiv",     "imul",     "lea",      "leave",
      "movsx",    "neg",      "nop",      "not",      "sar",
      "sete",     "setne",    "setnp",    "setp",     "setl",
      "setg",     "setle",    "setge",    "xchg",     "addsd",
      "addpd",    "andps",
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
  if (!check_true(info.form_count == 604u && info.mnemonic_count == 249u &&
                      info.register_count == 64u &&
                      info.fingerprint == 0x55a8970fu,
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
  static const ctool_u8 ret_bytes[] = {0xc2u, 0x04u, 0x00u};
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
  insn = instruction(CTOOL_X86_MN_RET, 32u, 32u, 0u);
  insn.operand_count = 1u;
  insn.operands[0] = value_operand(
      CTOOL_X86_OPERAND_IMMEDIATE, 16u, 16u, constant(4u));
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding,
              "return with stack cleanup") ||
      !bytes_equal(&encoding, ret_bytes, (ctool_u8)sizeof(ret_bytes),
                   "return cleanup bytes")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_32,
      ctool_bytes(ret_bytes, (ctool_u32)sizeof(ret_bytes)), 0u, &decoded);
  if (!check_status(status, CTOOL_OK, "return cleanup decode") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                      decoded.instruction.mnemonic == CTOOL_X86_MN_RET &&
                      decoded.instruction.operand_count == 1u &&
                      decoded.instruction.operands[0].kind ==
                          CTOOL_X86_OPERAND_IMMEDIATE &&
                      decoded.instruction.operands[0].width_bits == 16u &&
                      decoded.instruction.operands[0].encoding_bits == 16u &&
                      decoded.instruction.operands[0].as.value.bits == 4u &&
                      decoded.consumed == sizeof(ret_bytes),
                  "return cleanup semantics")) {
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)printf("integer: ok\n");
  return 0;
}

static int run_immediate_imul(void) {
  static const ctool_u8 full32[] = {
      0x69u, 0xc1u, 0x28u, 0x02u, 0x00u, 0x00u};
  static const ctool_u8 short32[] = {0x6bu, 0xc1u, 0xf9u};
  static const ctool_u8 full16[] = {0x69u, 0xc1u, 0x34u, 0x12u};
  static const ctool_u8 short16_memory32[] = {
      0x66u, 0x6bu, 0x53u, 0x7fu, 0xfeu};
  static const ctool_u8 full32_memory16[] = {
      0x66u, 0x69u, 0x40u, 0x7fu, 0x78u, 0x56u, 0x34u, 0x12u};
  static const struct {
    ctool_u32 value;
    ctool_u8 bytes[6];
    ctool_u8 size;
    const char *name;
  } signed_byte_boundaries[] = {
      {0x7fu, {0x6bu, 0xc1u, 0x7fu, 0u, 0u, 0u}, 3u,
       "immediate IMUL positive signed-byte boundary"},
      {0xffffff80u, {0x6bu, 0xc1u, 0x80u, 0u, 0u, 0u}, 3u,
       "immediate IMUL negative signed-byte boundary"},
      {0x80u, {0x69u, 0xc1u, 0x80u, 0u, 0u, 0u}, 6u,
       "immediate IMUL positive full-width boundary"},
      {0xffffff7fu, {0x69u, 0xc1u, 0x7fu, 0xffu, 0xffu, 0xffu}, 6u,
       "immediate IMUL negative full-width boundary"}};
  ctool_host_adapter_t adapter;
  ctool_job_t *job;
  ctool_x86_instruction_t insn;
  ctool_x86_encoding_t encoding;
  ctool_x86_encoding_t replay;
  ctool_x86_decoded_t decoded;
  ctool_status_t status;
  ctool_u32 cut;
  ctool_u32 boundary_index;
  ctool_u32 prefix_index;
  static const ctool_u8 prefix_bytes[] = {0xf0u, 0xf3u, 0xf2u};
  static const ctool_u8 semantic_prefixes[] = {
      CTOOL_X86_PREFIX_LOCK, CTOOL_X86_PREFIX_REP,
      CTOOL_X86_PREFIX_REPNE};
  if (!open_job(&adapter, &job)) {
    return 1;
  }

  insn = instruction(CTOOL_X86_MN_IMUL, 32u, 32u, 0u);
  insn.operand_count = 3u;
  insn.operands[0] = register_operand(CTOOL_X86_REG_GPR32, 0u);
  insn.operands[1] = register_operand(CTOOL_X86_REG_GPR32, 1u);
  insn.operands[2] = value_operand(
      CTOOL_X86_OPERAND_IMMEDIATE, 32u, 0u, constant(0x228u));
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding,
              "immediate IMUL full-width encode") ||
      !bytes_equal(&encoding, full32, (ctool_u8)sizeof(full32),
                   "immediate IMUL full-width bytes")) {
    ctool_job_close(job);
    return 1;
  }

  insn.operands[2] = value_operand(
      CTOOL_X86_OPERAND_IMMEDIATE, 32u, 0u, constant(0xfffffff9u));
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding,
              "immediate IMUL short encode") ||
      !bytes_equal(&encoding, short32, (ctool_u8)sizeof(short32),
                   "immediate IMUL short bytes")) {
    ctool_job_close(job);
    return 1;
  }
  for (boundary_index = 0u;
       boundary_index <
       (ctool_u32)(sizeof(signed_byte_boundaries) /
                   sizeof(signed_byte_boundaries[0]));
       boundary_index++) {
    insn.operands[2] = value_operand(
        CTOOL_X86_OPERAND_IMMEDIATE, 32u, 0u,
        constant(signed_byte_boundaries[boundary_index].value));
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding,
                signed_byte_boundaries[boundary_index].name) ||
        !bytes_equal(&encoding,
                     signed_byte_boundaries[boundary_index].bytes,
                     signed_byte_boundaries[boundary_index].size,
                     signed_byte_boundaries[boundary_index].name)) {
      ctool_job_close(job);
      return 1;
    }
  }

  insn = instruction(CTOOL_X86_MN_IMUL, 16u, 16u, 0u);
  insn.operand_count = 3u;
  insn.operands[0] = register_operand(CTOOL_X86_REG_GPR16, 0u);
  insn.operands[1] = register_operand(CTOOL_X86_REG_GPR16, 1u);
  insn.operands[2] = value_operand(
      CTOOL_X86_OPERAND_IMMEDIATE, 16u, 0u, constant(0x1234u));
  if (!encode(job, CTOOL_X86_MODE_16, &insn, &encoding,
              "16-bit immediate IMUL full-width encode") ||
      !bytes_equal(&encoding, full16, (ctool_u8)sizeof(full16),
                   "16-bit immediate IMUL full-width bytes")) {
    ctool_job_close(job);
    return 1;
  }

  insn = instruction(CTOOL_X86_MN_IMUL, 16u, 32u, 0u);
  insn.operand_count = 3u;
  insn.operands[0] = register_operand(CTOOL_X86_REG_GPR16, 2u);
  insn.operands[1] = memory_operand(
      16u, 32u, reg(CTOOL_X86_REG_NONE, 0u),
      reg(CTOOL_X86_REG_GPR32, 3u), reg(CTOOL_X86_REG_NONE, 0u),
      1u, 0x7f, 8u);
  insn.operands[2] = value_operand(
      CTOOL_X86_OPERAND_IMMEDIATE, 16u, 0u, constant(0xfffffffeu));
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding,
              "16-bit immediate IMUL memory encode") ||
      !bytes_equal(&encoding, short16_memory32,
                   (ctool_u8)sizeof(short16_memory32),
                   "16-bit immediate IMUL memory bytes")) {
    ctool_job_close(job);
    return 1;
  }

  insn = instruction(CTOOL_X86_MN_IMUL, 32u, 16u, 0u);
  insn.operand_count = 3u;
  insn.operands[0] = register_operand(CTOOL_X86_REG_GPR32, 0u);
  insn.operands[1] = memory_operand(
      32u, 16u, reg(CTOOL_X86_REG_NONE, 0u),
      reg(CTOOL_X86_REG_GPR16, 3u), reg(CTOOL_X86_REG_GPR16, 6u),
      1u, 0x7f, 8u);
  insn.operands[2] = value_operand(
      CTOOL_X86_OPERAND_IMMEDIATE, 32u, 0u, constant(0x12345678u));
  if (!encode(job, CTOOL_X86_MODE_16, &insn, &encoding,
              "32-bit immediate IMUL 16-bit address encode") ||
      !bytes_equal(&encoding, full32_memory16,
                   (ctool_u8)sizeof(full32_memory16),
                   "32-bit immediate IMUL 16-bit address bytes")) {
    ctool_job_close(job);
    return 1;
  }

  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_32,
      ctool_bytes(full32, (ctool_u32)sizeof(full32)), 0u, &decoded);
  if (!check_status(status, CTOOL_OK, "immediate IMUL full decode") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                      decoded.consumed == sizeof(full32) &&
                      decoded.instruction.mnemonic == CTOOL_X86_MN_IMUL &&
                      decoded.instruction.operand_bits == 32u &&
                      decoded.instruction.operand_count == 3u &&
                      decoded.instruction.operands[0].kind ==
                          CTOOL_X86_OPERAND_REGISTER &&
                      decoded.instruction.operands[0].as.reg.class_id ==
                          CTOOL_X86_REG_GPR32 &&
                      decoded.instruction.operands[0].as.reg.index == 0u &&
                      decoded.instruction.operands[1].kind ==
                          CTOOL_X86_OPERAND_REGISTER &&
                      decoded.instruction.operands[1].as.reg.index == 1u &&
                      decoded.instruction.operands[2].kind ==
                          CTOOL_X86_OPERAND_IMMEDIATE &&
                      decoded.instruction.operands[2].width_bits == 32u &&
                      decoded.instruction.operands[2].encoding_bits == 32u &&
                      decoded.instruction.operands[2].as.value.bits ==
                          0x228u &&
                      decoded.encoding.field_count == 1u &&
                      decoded.encoding.fields[0].kind ==
                          CTOOL_X86_FIELD_IMMEDIATE &&
                      decoded.encoding.fields[0].operand_index == 2u &&
                      decoded.encoding.fields[0].byte_offset == 2u &&
                      decoded.encoding.fields[0].byte_width == 4u &&
                      decoded.encoding.fields[0].encoded_addend == 0x228,
                  "immediate IMUL full decode semantics")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32,
                            &decoded.instruction, decoded.encoding.form,
                            &replay);
  if (!check_status(status, CTOOL_OK,
                    "immediate IMUL full requested-form replay") ||
      !bytes_equal(&replay, full32, (ctool_u8)sizeof(full32),
                   "immediate IMUL full replay bytes")) {
    ctool_job_close(job);
    return 1;
  }

  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_32,
      ctool_bytes(short32, (ctool_u32)sizeof(short32)), 0u, &decoded);
  if (!check_status(status, CTOOL_OK, "immediate IMUL short decode") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                      decoded.consumed == sizeof(short32) &&
                      decoded.instruction.operand_bits == 32u &&
                      decoded.instruction.operands[2].width_bits == 32u &&
                      decoded.instruction.operands[2].encoding_bits == 8u &&
                      decoded.instruction.operands[2].as.value.bits ==
                          0xfffffff9u &&
                      decoded.encoding.fields[0].byte_offset == 2u &&
                      decoded.encoding.fields[0].byte_width == 1u &&
                      decoded.encoding.fields[0].encoded_addend == -7,
                  "immediate IMUL short decode semantics")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32,
                            &decoded.instruction, decoded.encoding.form,
                            &replay);
  if (!check_status(status, CTOOL_OK,
                    "immediate IMUL short requested-form replay") ||
      !bytes_equal(&replay, short32, (ctool_u8)sizeof(short32),
                   "immediate IMUL short replay bytes")) {
    ctool_job_close(job);
    return 1;
  }

  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_32,
      ctool_bytes(short16_memory32,
                  (ctool_u32)sizeof(short16_memory32)),
      0u, &decoded);
  if (!check_status(status, CTOOL_OK, "immediate IMUL memory decode") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                      decoded.consumed == sizeof(short16_memory32) &&
                      decoded.instruction.operand_bits == 16u &&
                      decoded.instruction.address_bits == 32u &&
                      decoded.instruction.operands[0].as.reg.class_id ==
                          CTOOL_X86_REG_GPR16 &&
                      decoded.instruction.operands[0].as.reg.index == 2u &&
                      decoded.instruction.operands[1].kind ==
                          CTOOL_X86_OPERAND_MEMORY &&
                      decoded.instruction.operands[1].width_bits == 16u &&
                      decoded.instruction.operands[1].as.memory.base.index ==
                          3u &&
                      decoded.instruction.operands[1]
                              .as.memory.displacement.bits == 0x7fu &&
                      decoded.instruction.operands[2].as.value.bits ==
                          0xfffffffeu,
                  "immediate IMUL memory decode semantics")) {
    ctool_job_close(job);
    return 1;
  }

  for (cut = 1u; cut < (ctool_u32)sizeof(full32_memory16); cut++) {
    status = ctool_x86_decode(
        job, CTOOL_X86_MODE_16, ctool_bytes(full32_memory16, cut),
        0u, &decoded);
    if (!check_status(status, CTOOL_OK,
                      "immediate IMUL every-byte truncation") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_TRUNCATED &&
                        decoded.consumed == 0u &&
                        decoded.encoding.size == cut &&
                        memcmp(decoded.encoding.bytes, full32_memory16,
                               (size_t)cut) == 0,
                    "immediate IMUL truncation retention")) {
      ctool_job_close(job);
      return 1;
    }
  }

  insn = instruction(CTOOL_X86_MN_IMUL, 32u, 32u, 0u);
  insn.operand_count = 3u;
  insn.operands[0] = memory_operand(
      32u, 32u, reg(CTOOL_X86_REG_NONE, 0u),
      reg(CTOOL_X86_REG_GPR32, 0u), reg(CTOOL_X86_REG_NONE, 0u),
      1u, 0, 0u);
  insn.operands[1] = register_operand(CTOOL_X86_REG_GPR32, 1u);
  insn.operands[2] = value_operand(
      CTOOL_X86_OPERAND_IMMEDIATE, 32u, 0u, constant(2u));
  (void)memset(&encoding, 0xa5, sizeof(encoding));
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn,
                            CTOOL_X86_FORM_AUTO, &encoding);
  if (!check_status(status, CTOOL_ERR_INPUT,
                    "immediate IMUL memory destination") ||
      !check_true(encoding_is_zero(&encoding),
                  "immediate IMUL memory destination rollback")) {
    ctool_job_close(job);
    return 1;
  }

  insn.operands[0] = register_operand(CTOOL_X86_REG_GPR32, 0u);
  insn.operands[1] = register_operand(CTOOL_X86_REG_GPR16, 1u);
  (void)memset(&encoding, 0xa5, sizeof(encoding));
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn,
                            CTOOL_X86_FORM_AUTO, &encoding);
  if (!check_status(status, CTOOL_ERR_INPUT,
                    "immediate IMUL source width mismatch") ||
      !check_true(encoding_is_zero(&encoding),
                  "immediate IMUL source width rollback")) {
    ctool_job_close(job);
    return 1;
  }

  insn.operands[1] = register_operand(CTOOL_X86_REG_GPR32, 1u);
  insn.operands[2] = value_operand(
      CTOOL_X86_OPERAND_IMMEDIATE, 32u, 16u, constant(2u));
  (void)memset(&encoding, 0xa5, sizeof(encoding));
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn,
                            CTOOL_X86_FORM_AUTO, &encoding);
  if (!check_status(status, CTOOL_ERR_INPUT,
                    "immediate IMUL serialized width mismatch") ||
      !check_true(encoding_is_zero(&encoding),
                  "immediate IMUL serialized width rollback")) {
    ctool_job_close(job);
    return 1;
  }

  insn.operands[2] = value_operand(
      CTOOL_X86_OPERAND_IMMEDIATE, 32u, 0u, constant(2u));
  for (prefix_index = 0u;
       prefix_index <
       (ctool_u32)(sizeof(prefix_bytes) / sizeof(prefix_bytes[0]));
       prefix_index++) {
    ctool_u8 prefixed[4];
    prefixed[0] = prefix_bytes[prefix_index];
    prefixed[1] = short32[0];
    prefixed[2] = short32[1];
    prefixed[3] = 2u;
    insn.prefixes = semantic_prefixes[prefix_index];
    (void)memset(&encoding, 0xa5, sizeof(encoding));
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn,
                              CTOOL_X86_FORM_AUTO, &encoding);
    if (!check_status(status, CTOOL_ERR_INPUT,
                      "immediate IMUL semantic prefix") ||
        !check_true(encoding_is_zero(&encoding),
                    "immediate IMUL semantic prefix rollback")) {
      ctool_job_close(job);
      return 1;
    }
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                              ctool_bytes(prefixed, 4u), 0u, &decoded);
    if (!check_status(status, CTOOL_OK,
                      "immediate IMUL illegal prefix decode") ||
        !check_true(decoded.kind ==
                            (prefix_index == 0u
                                 ? CTOOL_X86_DECODE_INVALID
                                 : CTOOL_X86_DECODE_UNKNOWN) &&
                        decoded.consumed == 1u &&
                        decoded.encoding.size == 1u &&
                        decoded.encoding.bytes[0] == prefixed[0],
                    "immediate IMUL illegal prefix classification")) {
      ctool_job_close(job);
      return 1;
    }
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                              ctool_bytes(prefixed, 4u), 1u, &decoded);
    if (!check_status(status, CTOOL_OK,
                      "immediate IMUL post-prefix recovery") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                        decoded.instruction.mnemonic == CTOOL_X86_MN_IMUL &&
                        decoded.instruction.operands[2].as.value.bits == 2u,
                    "immediate IMUL recovered decode")) {
      ctool_job_close(job);
      return 1;
    }
  }

  ctool_job_close(job);
  (void)puts("immediate-imul: ok");
  return 0;
}

static int run_double_shift(void) {
  static const struct {
    ctool_x86_mode_t mode;
    ctool_u16 operand_bits;
    ctool_bool memory_destination;
    ctool_bool cl_count;
    ctool_u8 source_index;
    ctool_u8 bytes[5];
    ctool_u8 size;
    const char *name;
  } cases[] = {
      {CTOOL_X86_MODE_16, 16u, CTOOL_FALSE, CTOOL_FALSE, 7u,
       {0x0fu, 0xacu, 0xf8u, 0x07u, 0u}, 4u,
       "16-bit register SHRD immediate"},
      {CTOOL_X86_MODE_16, 16u, CTOOL_FALSE, CTOOL_TRUE, 7u,
       {0x0fu, 0xadu, 0xf8u, 0u, 0u}, 3u,
       "16-bit register SHRD CL"},
      {CTOOL_X86_MODE_16, 16u, CTOOL_TRUE, CTOOL_FALSE, 2u,
       {0x0fu, 0xacu, 0x50u, 0x7fu, 0x07u}, 5u,
       "16-bit memory SHRD immediate"},
      {CTOOL_X86_MODE_16, 16u, CTOOL_TRUE, CTOOL_TRUE, 2u,
       {0x0fu, 0xadu, 0x50u, 0x7fu, 0u}, 4u,
       "16-bit memory SHRD CL"},
      {CTOOL_X86_MODE_32, 32u, CTOOL_FALSE, CTOOL_FALSE, 7u,
       {0x0fu, 0xacu, 0xf8u, 0x07u, 0u}, 4u,
       "32-bit register SHRD immediate"},
      {CTOOL_X86_MODE_32, 32u, CTOOL_FALSE, CTOOL_TRUE, 7u,
       {0x0fu, 0xadu, 0xf8u, 0u, 0u}, 3u,
       "32-bit register SHRD CL"},
      {CTOOL_X86_MODE_32, 32u, CTOOL_TRUE, CTOOL_FALSE, 6u,
       {0x0fu, 0xacu, 0x73u, 0x04u, 0x07u}, 5u,
       "32-bit memory SHRD immediate"},
      {CTOOL_X86_MODE_32, 32u, CTOOL_TRUE, CTOOL_TRUE, 6u,
       {0x0fu, 0xadu, 0x73u, 0x04u, 0u}, 4u,
       "32-bit memory SHRD CL"}};
  static const ctool_u8 wide_memory16[] = {
      0x66u, 0x0fu, 0xacu, 0x30u, 0x1fu};
  static const ctool_u8 wide_address32_in_16[] = {
      0x66u, 0x67u, 0x0fu, 0xacu, 0x73u, 0x04u, 0x1fu};
  static const ctool_u8 narrow_register32[] = {
      0x66u, 0x0fu, 0xadu, 0xf8u};
  static const ctool_u8 narrow_address16_in_32[] = {
      0x66u, 0x67u, 0x0fu, 0xadu, 0x50u, 0x7fu};
  static const ctool_u8 active_stream[] = {
      0x0fu, 0xadu, 0xf8u, 0x0fu, 0xadu, 0xf8u, 0xc3u};
  static const ctool_u8 locked_active[] = {
      0xf0u, 0x0fu, 0xadu, 0xf8u, 0xc3u};
  ctool_host_adapter_t adapter;
  ctool_job_t *job;
  ctool_x86_instruction_t insn;
  ctool_x86_encoding_t encoding;
  ctool_x86_encoding_t replay;
  ctool_x86_decoded_t decoded;
  ctool_status_t status;
  ctool_string_t canonical;
  ctool_u32 case_index;
  ctool_u32 cut;
  if (!open_job(&adapter, &job)) {
    return 1;
  }

  canonical = ctool_x86_mnemonic_name(CTOOL_X86_MN_SHRD);
  if (!check_true(canonical.size == 4u &&
                      memcmp(canonical.data, "shrd", 4u) == 0,
                  "SHRD canonical mnemonic")) {
    ctool_job_close(job);
    return 1;
  }

  for (case_index = 0u;
       case_index < (ctool_u32)(sizeof(cases) / sizeof(cases[0]));
       case_index++) {
    ctool_x86_reg_class_t register_class =
        cases[case_index].operand_bits == 16u ? CTOOL_X86_REG_GPR16
                                             : CTOOL_X86_REG_GPR32;
    insn = instruction(CTOOL_X86_MN_SHRD,
                       cases[case_index].operand_bits,
                       cases[case_index].operand_bits, 0u);
    insn.operand_count = 3u;
    if (cases[case_index].memory_destination == CTOOL_TRUE) {
      if (cases[case_index].operand_bits == 16u) {
        insn.operands[0] = memory_operand(
            16u, 16u, reg(CTOOL_X86_REG_NONE, 0u),
            reg(CTOOL_X86_REG_GPR16, 3u),
            reg(CTOOL_X86_REG_GPR16, 6u), 1u, 0x7f, 8u);
      } else {
        insn.operands[0] = memory_operand(
            32u, 32u, reg(CTOOL_X86_REG_NONE, 0u),
            reg(CTOOL_X86_REG_GPR32, 3u),
            reg(CTOOL_X86_REG_NONE, 0u), 1u, 4, 8u);
      }
    } else {
      insn.operands[0] = register_operand(register_class, 0u);
    }
    insn.operands[1] = register_operand(
        register_class, cases[case_index].source_index);
    insn.operands[2] =
        cases[case_index].cl_count == CTOOL_TRUE
            ? register_operand(CTOOL_X86_REG_GPR8, 1u)
            : value_operand(CTOOL_X86_OPERAND_IMMEDIATE,
                            cases[case_index].operand_bits, 0u,
                            constant(7u));
    if (!encode(job, cases[case_index].mode, &insn, &encoding,
                cases[case_index].name) ||
        !bytes_equal(&encoding, cases[case_index].bytes,
                     cases[case_index].size, cases[case_index].name)) {
      ctool_job_close(job);
      return 1;
    }

    status = ctool_x86_decode(
        job, cases[case_index].mode,
        ctool_bytes(cases[case_index].bytes,
                    cases[case_index].size),
        0u, &decoded);
    if (!check_status(status, CTOOL_OK, cases[case_index].name) ||
        !check_true(
            decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                decoded.consumed == cases[case_index].size &&
                decoded.instruction.mnemonic == CTOOL_X86_MN_SHRD &&
                decoded.instruction.operand_bits ==
                    cases[case_index].operand_bits &&
                decoded.instruction.address_bits ==
                    cases[case_index].operand_bits &&
                decoded.instruction.operand_count == 3u &&
                decoded.instruction.operands[0].kind ==
                    (cases[case_index].memory_destination == CTOOL_TRUE
                         ? CTOOL_X86_OPERAND_MEMORY
                         : CTOOL_X86_OPERAND_REGISTER) &&
                decoded.instruction.operands[1].kind ==
                    CTOOL_X86_OPERAND_REGISTER &&
                decoded.instruction.operands[1].as.reg.class_id ==
                    register_class &&
                decoded.instruction.operands[1].as.reg.index ==
                    cases[case_index].source_index &&
                decoded.instruction.operands[2].kind ==
                    (cases[case_index].cl_count == CTOOL_TRUE
                         ? CTOOL_X86_OPERAND_REGISTER
                         : CTOOL_X86_OPERAND_IMMEDIATE) &&
                (cases[case_index].cl_count == CTOOL_TRUE
                     ? decoded.instruction.operands[2].as.reg.class_id ==
                               CTOOL_X86_REG_GPR8 &&
                           decoded.instruction.operands[2].as.reg.index == 1u &&
                           decoded.encoding.field_count ==
                               (cases[case_index].memory_destination ==
                                        CTOOL_TRUE
                                    ? 1u
                                    : 0u)
                     : decoded.instruction.operands[2].width_bits ==
                               cases[case_index].operand_bits &&
                           decoded.instruction.operands[2].encoding_bits == 8u &&
                           decoded.instruction.operands[2].as.value.bits == 7u &&
                           decoded.encoding.field_count ==
                               (cases[case_index].memory_destination ==
                                        CTOOL_TRUE
                                    ? 2u
                                    : 1u) &&
                           decoded.encoding
                                   .fields[decoded.encoding.field_count - 1u]
                                   .kind ==
                               CTOOL_X86_FIELD_IMMEDIATE &&
                           decoded.encoding
                                   .fields[decoded.encoding.field_count - 1u]
                                   .operand_index == 2u &&
                           decoded.encoding
                                       .fields[decoded.encoding.field_count -
                                               1u]
                                       .byte_offset +
                                   1u ==
                               cases[case_index].size &&
                           decoded.encoding
                                   .fields[decoded.encoding.field_count - 1u]
                                   .byte_width == 1u),
            cases[case_index].name)) {
      ctool_job_close(job);
      return 1;
    }
    if (cases[case_index].memory_destination == CTOOL_TRUE) {
      if (!check_true(
              decoded.instruction.operands[0].width_bits ==
                      cases[case_index].operand_bits &&
                  decoded.instruction.operands[0].as.memory.address_bits ==
                      cases[case_index].operand_bits &&
                  decoded.instruction.operands[0]
                          .as.memory.displacement.bits ==
                      (cases[case_index].operand_bits == 16u ? 0x7fu : 4u),
              cases[case_index].name)) {
        ctool_job_close(job);
        return 1;
      }
    } else if (!check_true(
                   decoded.instruction.operands[0].as.reg.class_id ==
                           register_class &&
                       decoded.instruction.operands[0].as.reg.index == 0u,
                   cases[case_index].name)) {
      ctool_job_close(job);
      return 1;
    }
    status = ctool_x86_encode(job, cases[case_index].mode,
                              &decoded.instruction,
                              decoded.encoding.form, &replay);
    if (!check_status(status, CTOOL_OK, cases[case_index].name) ||
        !bytes_equal(&replay, cases[case_index].bytes,
                     cases[case_index].size, cases[case_index].name)) {
      ctool_job_close(job);
      return 1;
    }
  }

  insn = instruction(CTOOL_X86_MN_SHRD, 32u, 16u, 0u);
  insn.operand_count = 3u;
  insn.operands[0] = memory_operand(
      32u, 16u, reg(CTOOL_X86_REG_NONE, 0u),
      reg(CTOOL_X86_REG_GPR16, 3u), reg(CTOOL_X86_REG_GPR16, 6u),
      1u, 0, 0u);
  insn.operands[1] = register_operand(CTOOL_X86_REG_GPR32, 6u);
  insn.operands[2] = value_operand(
      CTOOL_X86_OPERAND_IMMEDIATE, 32u, 0u, constant(0x1fu));
  if (!encode(job, CTOOL_X86_MODE_16, &insn, &encoding,
              "32-bit SHRD in 16-bit mode") ||
      !bytes_equal(&encoding, wide_memory16,
                   (ctool_u8)sizeof(wide_memory16),
                   "32-bit SHRD in 16-bit mode bytes")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_16,
      ctool_bytes(wide_memory16, (ctool_u32)sizeof(wide_memory16)),
      0u, &decoded);
  if (!check_status(status, CTOOL_OK,
                     "32-bit SHRD in 16-bit mode decode") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                      decoded.instruction.mnemonic == CTOOL_X86_MN_SHRD &&
                      decoded.instruction.operand_bits == 32u &&
                      decoded.instruction.address_bits == 16u &&
                      decoded.instruction.operands[0].kind ==
                          CTOOL_X86_OPERAND_MEMORY &&
                      decoded.instruction.operands[1].as.reg.index == 6u &&
                      decoded.instruction.operands[2].as.value.bits == 0x1fu,
                  "32-bit SHRD in 16-bit mode semantics")) {
    ctool_job_close(job);
    return 1;
  }

  insn = instruction(CTOOL_X86_MN_SHRD, 32u, 32u, 0u);
  insn.operand_count = 3u;
  insn.operands[0] = memory_operand(
      32u, 32u, reg(CTOOL_X86_REG_NONE, 0u),
      reg(CTOOL_X86_REG_GPR32, 3u), reg(CTOOL_X86_REG_NONE, 0u),
      1u, 4, 8u);
  insn.operands[1] = register_operand(CTOOL_X86_REG_GPR32, 6u);
  insn.operands[2] = value_operand(
      CTOOL_X86_OPERAND_IMMEDIATE, 32u, 0u, constant(0x1fu));
  if (!encode(job, CTOOL_X86_MODE_16, &insn, &encoding,
              "32-bit-address SHRD in 16-bit mode") ||
      !bytes_equal(&encoding, wide_address32_in_16,
                   (ctool_u8)sizeof(wide_address32_in_16),
                   "32-bit-address SHRD in 16-bit mode bytes")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_16,
      ctool_bytes(wide_address32_in_16,
                  (ctool_u32)sizeof(wide_address32_in_16)),
      0u, &decoded);
  if (!check_status(status, CTOOL_OK,
                    "32-bit-address SHRD in 16-bit mode decode") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                      decoded.instruction.mnemonic == CTOOL_X86_MN_SHRD &&
                      decoded.instruction.operand_bits == 32u &&
                      decoded.instruction.address_bits == 32u &&
                      decoded.instruction.operands[0].kind ==
                          CTOOL_X86_OPERAND_MEMORY &&
                      decoded.instruction.operands[0]
                              .as.memory.address_bits == 32u &&
                      decoded.instruction.operands[0]
                              .as.memory.base.class_id ==
                          CTOOL_X86_REG_GPR32 &&
                      decoded.instruction.operands[0].as.memory.base.index ==
                          3u &&
                      decoded.instruction.operands[1].as.reg.index == 6u &&
                      decoded.instruction.operands[2].as.value.bits == 0x1fu,
                  "32-bit-address SHRD in 16-bit mode semantics")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_encode(job, CTOOL_X86_MODE_16,
                            &decoded.instruction,
                            decoded.encoding.form, &replay);
  if (!check_status(status, CTOOL_OK,
                    "32-bit-address SHRD in 16-bit mode replay") ||
      !bytes_equal(&replay, wide_address32_in_16,
                   (ctool_u8)sizeof(wide_address32_in_16),
                   "32-bit-address SHRD in 16-bit mode replay bytes")) {
    ctool_job_close(job);
    return 1;
  }

  insn = instruction(CTOOL_X86_MN_SHRD, 16u, 32u, 0u);
  insn.operand_count = 3u;
  insn.operands[0] = register_operand(CTOOL_X86_REG_GPR16, 0u);
  insn.operands[1] = register_operand(CTOOL_X86_REG_GPR16, 7u);
  insn.operands[2] = register_operand(CTOOL_X86_REG_GPR8, 1u);
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding,
              "16-bit SHRD in 32-bit mode") ||
      !bytes_equal(&encoding, narrow_register32,
                   (ctool_u8)sizeof(narrow_register32),
                   "16-bit SHRD in 32-bit mode bytes")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_32,
      ctool_bytes(narrow_register32,
                  (ctool_u32)sizeof(narrow_register32)),
      0u, &decoded);
  if (!check_status(status, CTOOL_OK,
                     "16-bit SHRD in 32-bit mode decode") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                      decoded.instruction.operand_bits == 16u &&
                      decoded.instruction.address_bits == 32u &&
                      decoded.instruction.operands[2].as.reg.index == 1u,
                  "16-bit SHRD in 32-bit mode semantics")) {
    ctool_job_close(job);
    return 1;
  }

  insn = instruction(CTOOL_X86_MN_SHRD, 16u, 16u, 0u);
  insn.operand_count = 3u;
  insn.operands[0] = memory_operand(
      16u, 16u, reg(CTOOL_X86_REG_NONE, 0u),
      reg(CTOOL_X86_REG_GPR16, 3u), reg(CTOOL_X86_REG_GPR16, 6u),
      1u, 0x7f, 8u);
  insn.operands[1] = register_operand(CTOOL_X86_REG_GPR16, 2u);
  insn.operands[2] = register_operand(CTOOL_X86_REG_GPR8, 1u);
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding,
              "16-bit-address SHRD in 32-bit mode") ||
      !bytes_equal(&encoding, narrow_address16_in_32,
                   (ctool_u8)sizeof(narrow_address16_in_32),
                   "16-bit-address SHRD in 32-bit mode bytes")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_32,
      ctool_bytes(narrow_address16_in_32,
                  (ctool_u32)sizeof(narrow_address16_in_32)),
      0u, &decoded);
  if (!check_status(status, CTOOL_OK,
                    "16-bit-address SHRD in 32-bit mode decode") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                      decoded.instruction.mnemonic == CTOOL_X86_MN_SHRD &&
                      decoded.instruction.operand_bits == 16u &&
                      decoded.instruction.address_bits == 16u &&
                      decoded.instruction.operands[0].kind ==
                          CTOOL_X86_OPERAND_MEMORY &&
                      decoded.instruction.operands[0]
                              .as.memory.address_bits == 16u &&
                      decoded.instruction.operands[0]
                              .as.memory.base.class_id ==
                          CTOOL_X86_REG_GPR16 &&
                      decoded.instruction.operands[0].as.memory.base.index ==
                          3u &&
                      decoded.instruction.operands[0]
                              .as.memory.index.class_id ==
                          CTOOL_X86_REG_GPR16 &&
                      decoded.instruction.operands[0].as.memory.index.index ==
                          6u &&
                      decoded.instruction.operands[1].as.reg.index == 2u &&
                      decoded.instruction.operands[2].as.reg.index == 1u,
                  "16-bit-address SHRD in 32-bit mode semantics")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32,
                            &decoded.instruction,
                            decoded.encoding.form, &replay);
  if (!check_status(status, CTOOL_OK,
                    "16-bit-address SHRD in 32-bit mode replay") ||
      !bytes_equal(&replay, narrow_address16_in_32,
                   (ctool_u8)sizeof(narrow_address16_in_32),
                   "16-bit-address SHRD in 32-bit mode replay bytes")) {
    ctool_job_close(job);
    return 1;
  }

  for (cut = 1u; cut < (ctool_u32)sizeof(wide_memory16); cut++) {
    status = ctool_x86_decode(
        job, CTOOL_X86_MODE_16, ctool_bytes(wide_memory16, cut),
        0u, &decoded);
    if (!check_status(status, CTOOL_OK, "SHRD every-byte truncation") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_TRUNCATED &&
                        decoded.consumed == 0u &&
                        decoded.encoding.size == cut &&
                        memcmp(decoded.encoding.bytes, wide_memory16,
                               (size_t)cut) == 0,
                    "SHRD truncation retention")) {
      ctool_job_close(job);
      return 1;
    }
  }

  insn = instruction(CTOOL_X86_MN_SHRD, 32u, 32u, 0u);
  insn.operand_count = 3u;
  insn.operands[0] = register_operand(CTOOL_X86_REG_GPR32, 0u);
  insn.operands[1] = register_operand(CTOOL_X86_REG_GPR16, 7u);
  insn.operands[2] = register_operand(CTOOL_X86_REG_GPR8, 1u);
  (void)memset(&encoding, 0xa5, sizeof(encoding));
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn,
                            CTOOL_X86_FORM_AUTO, &encoding);
  if (!check_status(status, CTOOL_ERR_INPUT, "SHRD width mismatch") ||
      !check_true(encoding_is_zero(&encoding),
                  "SHRD width mismatch rollback")) {
    ctool_job_close(job);
    return 1;
  }

  insn.operands[1] = memory_operand(
      32u, 32u, reg(CTOOL_X86_REG_NONE, 0u),
      reg(CTOOL_X86_REG_GPR32, 7u), reg(CTOOL_X86_REG_NONE, 0u),
      1u, 0, 0u);
  (void)memset(&encoding, 0xa5, sizeof(encoding));
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn,
                            CTOOL_X86_FORM_AUTO, &encoding);
  if (!check_status(status, CTOOL_ERR_INPUT, "SHRD memory source") ||
      !check_true(encoding_is_zero(&encoding),
                  "SHRD memory source rollback")) {
    ctool_job_close(job);
    return 1;
  }

  insn.operands[1] = register_operand(CTOOL_X86_REG_GPR32, 7u);
  insn.operands[2] = register_operand(CTOOL_X86_REG_GPR8, 2u);
  (void)memset(&encoding, 0xa5, sizeof(encoding));
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn,
                            CTOOL_X86_FORM_AUTO, &encoding);
  if (!check_status(status, CTOOL_ERR_INPUT, "SHRD wrong count register") ||
      !check_true(encoding_is_zero(&encoding),
                  "SHRD wrong count register rollback")) {
    ctool_job_close(job);
    return 1;
  }

  insn.operands[2] = value_operand(
      CTOOL_X86_OPERAND_IMMEDIATE, 32u, 16u, constant(7u));
  (void)memset(&encoding, 0xa5, sizeof(encoding));
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn,
                            CTOOL_X86_FORM_AUTO, &encoding);
  if (!check_status(status, CTOOL_ERR_INPUT,
                    "SHRD serialized count width") ||
      !check_true(encoding_is_zero(&encoding),
                  "SHRD serialized count width rollback")) {
    ctool_job_close(job);
    return 1;
  }

  insn.prefixes = CTOOL_X86_PREFIX_LOCK;
  insn.operands[2] = register_operand(CTOOL_X86_REG_GPR8, 1u);
  (void)memset(&encoding, 0xa5, sizeof(encoding));
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn,
                            CTOOL_X86_FORM_AUTO, &encoding);
  if (!check_status(status, CTOOL_ERR_INPUT, "locked SHRD") ||
      !check_true(encoding_is_zero(&encoding), "locked SHRD rollback")) {
    ctool_job_close(job);
    return 1;
  }

  insn.prefixes = 0u;
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding,
              "SHRD same-job encode recovery") ||
      !bytes_equal(&encoding, active_stream, 3u,
                   "SHRD same-job recovered bytes")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_32,
      ctool_bytes(active_stream, (ctool_u32)sizeof(active_stream)),
      3u, &decoded);
  if (!check_status(status, CTOOL_OK, "second active SHRD decode") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                      decoded.instruction.mnemonic == CTOOL_X86_MN_SHRD &&
                      decoded.consumed == 3u &&
                      decoded.instruction.operands[0].as.reg.index == 0u &&
                      decoded.instruction.operands[1].as.reg.index == 7u &&
                      decoded.instruction.operands[2].as.reg.index == 1u,
                  "second active SHRD semantics")) {
    ctool_job_close(job);
    return 1;
  }

  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_32,
      ctool_bytes(locked_active, (ctool_u32)sizeof(locked_active)),
      0u, &decoded);
  if (!check_status(status, CTOOL_OK, "locked SHRD decode") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_INVALID &&
                      decoded.consumed == 1u &&
                      decoded.encoding.size == 1u &&
                      decoded.encoding.bytes[0] == 0xf0u,
                  "locked SHRD invalid classification")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_32,
      ctool_bytes(locked_active, (ctool_u32)sizeof(locked_active)),
      1u, &decoded);
  if (!check_status(status, CTOOL_OK, "post-lock SHRD recovery") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                      decoded.instruction.mnemonic == CTOOL_X86_MN_SHRD &&
                      decoded.consumed == 3u,
                  "post-lock SHRD recovered decode")) {
    ctool_job_close(job);
    return 1;
  }

  ctool_job_close(job);
  (void)puts("double-shift: ok");
  return 0;
}

static int run_padding_nops(void) {
  static const ctool_u8 nop_default[] = {0x90u};
  static const ctool_u8 nop_override[] = {0x66u, 0x90u};
  static const ctool_u8 nop_register32[] = {0x0fu, 0x1fu, 0xc0u};
  static const ctool_u8 nop_register16[] = {0x66u, 0x0fu, 0x1fu, 0xc0u};
  static const ctool_u8 nop_memory32[] = {0x0fu, 0x1fu, 0x00u};
  static const ctool_u8 nop_memory16_canonical[] = {
      0x2eu, 0x66u, 0x0fu, 0x1fu, 0x84u,
      0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
  static const ctool_u8 nop_address16[] = {
      0x67u, 0x0fu, 0x1fu, 0x40u, 0x7fu};
  static const ctool_u8 nop_mode16_wide[] = {
      0x66u, 0x67u, 0x0fu, 0x1fu, 0x84u,
      0x8bu, 0x78u, 0x56u, 0x34u, 0x12u};
  static const ctool_u8 compiler3[] = {0x0fu, 0x1fu, 0x00u};
  static const ctool_u8 compiler4[] = {0x0fu, 0x1fu, 0x40u, 0x00u};
  static const ctool_u8 compiler5[] = {
      0x0fu, 0x1fu, 0x44u, 0x00u, 0x00u};
  static const ctool_u8 compiler6[] = {
      0x66u, 0x0fu, 0x1fu, 0x44u, 0x00u, 0x00u};
  static const ctool_u8 compiler7[] = {
      0x0fu, 0x1fu, 0x80u, 0x00u, 0x00u, 0x00u, 0x00u};
  static const ctool_u8 compiler8[] = {
      0x0fu, 0x1fu, 0x84u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
  static const ctool_u8 compiler9[] = {
      0x66u, 0x0fu, 0x1fu, 0x84u, 0x00u,
      0x00u, 0x00u, 0x00u, 0x00u};
  static const ctool_u8 compiler10[] = {
      0x66u, 0x2eu, 0x0fu, 0x1fu, 0x84u,
      0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
  static const ctool_u8 lock_recovery[] = {
      0xf0u, 0x0fu, 0x1fu, 0x00u, 0xc3u};
  static const ctool_u8 repeat_nop[] = {
      0xf2u, 0x0fu, 0x1fu, 0x00u};
  static const ctool_u8 wrong_digit[] = {0x0fu, 0x1fu, 0x08u};
  static const ctool_u8 pause_bytes[] = {0xf3u, 0x90u};
  static const struct {
    const ctool_u8 *bytes;
    ctool_u32 size;
    const char *name;
  } compiler_forms[] = {
      {compiler3, (ctool_u32)sizeof(compiler3),
       "three-byte compiler NOP"},
      {compiler4, (ctool_u32)sizeof(compiler4),
       "four-byte compiler NOP"},
      {compiler5, (ctool_u32)sizeof(compiler5),
       "five-byte compiler NOP"},
      {compiler6, (ctool_u32)sizeof(compiler6),
       "six-byte compiler NOP"},
      {compiler7, (ctool_u32)sizeof(compiler7),
       "seven-byte compiler NOP"},
      {compiler8, (ctool_u32)sizeof(compiler8),
       "eight-byte compiler NOP"},
      {compiler9, (ctool_u32)sizeof(compiler9),
       "nine-byte compiler NOP"},
      {compiler10, (ctool_u32)sizeof(compiler10),
       "ten-byte compiler NOP"},
      {nop_override, (ctool_u32)sizeof(nop_override),
       "operand-size compiler NOP"}};
  ctool_host_adapter_t adapter;
  ctool_job_t *job;
  ctool_x86_instruction_t insn;
  ctool_x86_encoding_t encoding;
  ctool_x86_encoding_t replay;
  ctool_x86_decoded_t decoded;
  ctool_status_t status;
  ctool_u32 index;
  ctool_u32 cut;
  if (!open_job(&adapter, &job)) {
    return 1;
  }

  insn = instruction(CTOOL_X86_MN_NOP, 0u, 32u, 0u);
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding,
              "default NOP encode") ||
      !bytes_equal(&encoding, nop_default, (ctool_u8)sizeof(nop_default),
                   "default NOP bytes")) {
    ctool_job_close(job);
    return 1;
  }

  insn = instruction(CTOOL_X86_MN_NOP, 16u, 32u, 0u);
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding,
              "32-bit mode operand-size NOP encode") ||
      !bytes_equal(&encoding, nop_override,
                   (ctool_u8)sizeof(nop_override),
                   "32-bit mode operand-size NOP bytes")) {
    ctool_job_close(job);
    return 1;
  }
  insn = instruction(CTOOL_X86_MN_NOP, 32u, 16u, 0u);
  if (!encode(job, CTOOL_X86_MODE_16, &insn, &encoding,
              "16-bit mode operand-size NOP encode") ||
      !bytes_equal(&encoding, nop_override,
                   (ctool_u8)sizeof(nop_override),
                   "16-bit mode operand-size NOP bytes")) {
    ctool_job_close(job);
    return 1;
  }

  insn = instruction(CTOOL_X86_MN_NOP, 32u, 32u, 0u);
  insn.operand_count = 1u;
  insn.operands[0] = register_operand(CTOOL_X86_REG_GPR32, 0u);
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding,
              "register NOP encode") ||
      !bytes_equal(&encoding, nop_register32,
                   (ctool_u8)sizeof(nop_register32),
                   "register NOP bytes")) {
    ctool_job_close(job);
    return 1;
  }
  insn = instruction(CTOOL_X86_MN_NOP, 16u, 32u, 0u);
  insn.operand_count = 1u;
  insn.operands[0] = register_operand(CTOOL_X86_REG_GPR16, 0u);
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding,
              "word register NOP encode") ||
      !bytes_equal(&encoding, nop_register16,
                   (ctool_u8)sizeof(nop_register16),
                   "word register NOP bytes")) {
    ctool_job_close(job);
    return 1;
  }

  insn = instruction(CTOOL_X86_MN_NOP, 32u, 32u, 0u);
  insn.operand_count = 1u;
  insn.operands[0] = memory_operand(
      32u, 32u, reg(CTOOL_X86_REG_NONE, 0u),
      reg(CTOOL_X86_REG_GPR32, 0u), reg(CTOOL_X86_REG_NONE, 0u),
      1u, 0, 0u);
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding,
              "memory NOP encode") ||
      !bytes_equal(&encoding, nop_memory32,
                   (ctool_u8)sizeof(nop_memory32),
                   "memory NOP bytes")) {
    ctool_job_close(job);
    return 1;
  }

  insn = instruction(CTOOL_X86_MN_NOP, 16u, 32u, 0u);
  insn.operand_count = 1u;
  insn.operands[0] = memory_operand(
      16u, 32u, reg(CTOOL_X86_REG_SEGMENT, 1u),
      reg(CTOOL_X86_REG_GPR32, 0u), reg(CTOOL_X86_REG_GPR32, 0u),
      1u, 0, 32u);
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding,
              "word compiler-shaped NOP encode") ||
      !bytes_equal(&encoding, nop_memory16_canonical,
                   (ctool_u8)sizeof(nop_memory16_canonical),
                   "word compiler-shaped NOP bytes")) {
    ctool_job_close(job);
    return 1;
  }

  insn = instruction(CTOOL_X86_MN_NOP, 32u, 16u, 0u);
  insn.operand_count = 1u;
  insn.operands[0] = memory_operand(
      32u, 16u, reg(CTOOL_X86_REG_NONE, 0u),
      reg(CTOOL_X86_REG_GPR16, 3u), reg(CTOOL_X86_REG_GPR16, 6u),
      1u, 0x7f, 8u);
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding,
              "address-size NOP encode") ||
      !bytes_equal(&encoding, nop_address16,
                   (ctool_u8)sizeof(nop_address16),
                   "address-size NOP bytes")) {
    ctool_job_close(job);
    return 1;
  }

  insn = instruction(CTOOL_X86_MN_NOP, 32u, 32u, 0u);
  insn.operand_count = 1u;
  insn.operands[0] = memory_operand(
      32u, 32u, reg(CTOOL_X86_REG_NONE, 0u),
      reg(CTOOL_X86_REG_GPR32, 3u), reg(CTOOL_X86_REG_GPR32, 1u),
      4u, 0x12345678, 32u);
  if (!encode(job, CTOOL_X86_MODE_16, &insn, &encoding,
              "16-bit mode wide NOP encode") ||
      !bytes_equal(&encoding, nop_mode16_wide,
                   (ctool_u8)sizeof(nop_mode16_wide),
                   "16-bit mode wide NOP bytes")) {
    ctool_job_close(job);
    return 1;
  }

  for (index = 0u;
       index <
       (ctool_u32)(sizeof(compiler_forms) / sizeof(compiler_forms[0]));
       index++) {
    status = ctool_x86_decode(
        job, CTOOL_X86_MODE_32,
        ctool_bytes(compiler_forms[index].bytes, compiler_forms[index].size),
        0u, &decoded);
    if (!check_status(status, CTOOL_OK, compiler_forms[index].name) ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                        decoded.instruction.mnemonic == CTOOL_X86_MN_NOP &&
                        decoded.consumed == compiler_forms[index].size &&
                        decoded.encoding.size == compiler_forms[index].size &&
                        decoded.encoding.form != CTOOL_X86_FORM_AUTO &&
                        memcmp(decoded.encoding.bytes,
                               compiler_forms[index].bytes,
                               compiler_forms[index].size) == 0,
                    compiler_forms[index].name)) {
      ctool_job_close(job);
      return 1;
    }
  }

  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_32,
      ctool_bytes(nop_memory16_canonical,
                  (ctool_u32)sizeof(nop_memory16_canonical)),
      0u, &decoded);
  if (!check_status(status, CTOOL_OK,
                    "compiler-shaped NOP semantic decode") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                      decoded.instruction.operand_bits == 16u &&
                      decoded.instruction.address_bits == 32u &&
                      decoded.instruction.operand_count == 1u &&
                      decoded.instruction.operands[0].kind ==
                          CTOOL_X86_OPERAND_MEMORY &&
                      decoded.instruction.operands[0].width_bits == 16u &&
                      decoded.instruction.operands[0]
                              .as.memory.segment.class_id ==
                          CTOOL_X86_REG_SEGMENT &&
                      decoded.instruction.operands[0]
                              .as.memory.segment.index == 1u &&
                      decoded.instruction.operands[0]
                              .as.memory.base.index == 0u &&
                      decoded.instruction.operands[0]
                              .as.memory.index.index == 0u &&
                      decoded.instruction.operands[0]
                              .as.memory.displacement.bits == 0u &&
                      decoded.instruction.operands[0]
                              .as.memory.displacement_bits == 32u &&
                      decoded.encoding.field_count == 1u &&
                      decoded.encoding.fields[0].kind ==
                          CTOOL_X86_FIELD_DISPLACEMENT &&
                      decoded.encoding.fields[0].byte_offset == 6u &&
                      decoded.encoding.fields[0].byte_width == 4u,
                  "compiler-shaped NOP semantics")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32,
                            &decoded.instruction, decoded.encoding.form,
                            &replay);
  if (!check_status(status, CTOOL_OK,
                    "compiler-shaped NOP requested-form replay") ||
      !bytes_equal(&replay, nop_memory16_canonical,
                   (ctool_u8)sizeof(nop_memory16_canonical),
                   "compiler-shaped NOP requested-form bytes")) {
    ctool_job_close(job);
    return 1;
  }

  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_16,
      ctool_bytes(nop_override, (ctool_u32)sizeof(nop_override)),
      0u, &decoded);
  if (!check_status(status, CTOOL_OK,
                    "16-bit mode operand-size NOP decode") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                      decoded.instruction.operand_bits == 32u &&
                      decoded.instruction.operand_count == 0u,
                  "16-bit mode operand-size NOP semantics")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_encode(job, CTOOL_X86_MODE_16,
                            &decoded.instruction, decoded.encoding.form,
                            &replay);
  if (!check_status(status, CTOOL_OK,
                    "operand-size NOP requested-form replay") ||
      !bytes_equal(&replay, nop_override,
                   (ctool_u8)sizeof(nop_override),
                   "operand-size NOP requested-form bytes")) {
    ctool_job_close(job);
    return 1;
  }

  for (cut = 1u; cut < (ctool_u32)sizeof(compiler10); cut++) {
    status = ctool_x86_decode(
        job, CTOOL_X86_MODE_32, ctool_bytes(compiler10, cut), 0u,
        &decoded);
    if (!check_status(status, CTOOL_OK,
                      "padding NOP every-byte truncation") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_TRUNCATED &&
                        decoded.consumed == 0u &&
                        decoded.encoding.size == cut &&
                        memcmp(decoded.encoding.bytes, compiler10,
                               (size_t)cut) == 0,
                    "padding NOP truncation retention")) {
      ctool_job_close(job);
      return 1;
    }
  }

  insn = instruction(CTOOL_X86_MN_NOP, 32u, 32u,
                     CTOOL_X86_PREFIX_LOCK);
  insn.operand_count = 1u;
  insn.operands[0] = memory_operand(
      32u, 32u, reg(CTOOL_X86_REG_NONE, 0u),
      reg(CTOOL_X86_REG_GPR32, 0u), reg(CTOOL_X86_REG_NONE, 0u),
      1u, 0, 0u);
  (void)memset(&encoding, 0xa5, sizeof(encoding));
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn,
                            CTOOL_X86_FORM_AUTO, &encoding);
  if (!check_status(status, CTOOL_ERR_INPUT, "locked padding NOP") ||
      !check_true(encoding_is_zero(&encoding),
                  "locked padding NOP rollback")) {
    ctool_job_close(job);
    return 1;
  }
  insn.prefixes = CTOOL_X86_PREFIX_REPNE;
  (void)memset(&encoding, 0xa5, sizeof(encoding));
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn,
                            CTOOL_X86_FORM_AUTO, &encoding);
  if (!check_status(status, CTOOL_ERR_INPUT, "repeat padding NOP") ||
      !check_true(encoding_is_zero(&encoding),
                  "repeat padding NOP rollback")) {
    ctool_job_close(job);
    return 1;
  }
  insn = instruction(CTOOL_X86_MN_NOP, 16u, 32u, 0u);
  insn.operand_count = 1u;
  insn.operands[0] = memory_operand(
      32u, 32u, reg(CTOOL_X86_REG_NONE, 0u),
      reg(CTOOL_X86_REG_GPR32, 0u), reg(CTOOL_X86_REG_NONE, 0u),
      1u, 0, 0u);
  (void)memset(&encoding, 0xa5, sizeof(encoding));
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn,
                            CTOOL_X86_FORM_AUTO, &encoding);
  if (!check_status(status, CTOOL_ERR_INPUT,
                    "padding NOP width mismatch") ||
      !check_true(encoding_is_zero(&encoding),
                  "padding NOP width mismatch rollback")) {
    ctool_job_close(job);
    return 1;
  }
  insn = instruction(CTOOL_X86_MN_NOP, 8u, 32u, 0u);
  insn.operand_count = 1u;
  insn.operands[0] = memory_operand(
      8u, 32u, reg(CTOOL_X86_REG_NONE, 0u),
      reg(CTOOL_X86_REG_GPR32, 0u), reg(CTOOL_X86_REG_NONE, 0u),
      1u, 0, 0u);
  (void)memset(&encoding, 0xa5, sizeof(encoding));
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn,
                            CTOOL_X86_FORM_AUTO, &encoding);
  if (!check_status(status, CTOOL_ERR_INPUT, "byte padding NOP") ||
      !check_true(encoding_is_zero(&encoding),
                  "byte padding NOP rollback")) {
    ctool_job_close(job);
    return 1;
  }

  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_32,
      ctool_bytes(wrong_digit, (ctool_u32)sizeof(wrong_digit)), 0u,
      &decoded);
  if (!check_status(status, CTOOL_OK, "padding NOP wrong ModRM digit") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_UNKNOWN &&
                      decoded.consumed == 1u,
                  "padding NOP wrong ModRM digit classification")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_32,
      ctool_bytes(repeat_nop, (ctool_u32)sizeof(repeat_nop)), 0u,
      &decoded);
  if (!check_status(status, CTOOL_OK, "padding NOP repeat prefix") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_UNKNOWN &&
                      decoded.consumed == 1u,
                  "padding NOP repeat prefix classification")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_32,
      ctool_bytes(pause_bytes, (ctool_u32)sizeof(pause_bytes)), 0u,
      &decoded);
  if (!check_status(status, CTOOL_OK, "PAUSE remains distinct") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                      decoded.instruction.mnemonic == CTOOL_X86_MN_PAUSE &&
                      decoded.consumed == sizeof(pause_bytes),
                  "PAUSE remains distinct from NOP")) {
    ctool_job_close(job);
    return 1;
  }

  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_32,
      ctool_bytes(lock_recovery, (ctool_u32)sizeof(lock_recovery)), 0u,
      &decoded);
  if (!check_status(status, CTOOL_OK, "locked padding NOP decode") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_INVALID &&
                      decoded.consumed == 1u,
                  "locked padding NOP classification")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_32,
      ctool_bytes(lock_recovery, (ctool_u32)sizeof(lock_recovery)), 1u,
      &decoded);
  if (!check_status(status, CTOOL_OK, "padding NOP recovery decode") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                      decoded.instruction.mnemonic == CTOOL_X86_MN_NOP &&
                      decoded.consumed == 3u,
                  "padding NOP recovery semantics")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_32,
      ctool_bytes(lock_recovery, (ctool_u32)sizeof(lock_recovery)), 4u,
      &decoded);
  if (!check_status(status, CTOOL_OK, "padding NOP following return") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                      decoded.instruction.mnemonic == CTOOL_X86_MN_RET &&
                      decoded.consumed == 1u,
                  "padding NOP following return semantics")) {
    ctool_job_close(job);
    return 1;
  }

  ctool_job_close(job);
  (void)puts("padding-nops: ok");
  return 0;
}

static int run_clang_padding_nops(void) {
  static const ctool_u8 longest[] = {
      0x66u, 0x66u, 0x66u, 0x66u, 0x66u, 0x66u,
      0x2eu, 0x0fu, 0x1fu, 0x84u, 0x00u,
      0x00u, 0x00u, 0x00u, 0x00u};
  static const ctool_u8 canonical[] = {
      0x2eu, 0x66u, 0x0fu, 0x1fu, 0x84u,
      0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
  static const ctool_u8 wrong_segment[] = {
      0x66u, 0x66u, 0x3eu, 0x0fu, 0x1fu, 0x84u,
      0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
  static const ctool_u8 wrong_opcode[] = {
      0x66u, 0x66u, 0x2eu, 0x0fu, 0x1eu, 0x84u,
      0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
  static const ctool_u8 wrong_modrm[] = {
      0x66u, 0x66u, 0x2eu, 0x0fu, 0x1fu, 0x8cu,
      0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
  static const ctool_u8 wrong_sib[] = {
      0x66u, 0x66u, 0x2eu, 0x0fu, 0x1fu, 0x84u,
      0x01u, 0x00u, 0x00u, 0x00u, 0x00u};
  static const ctool_u8 nonzero_displacement[] = {
      0x66u, 0x66u, 0x2eu, 0x0fu, 0x1fu, 0x84u,
      0x00u, 0x01u, 0x00u, 0x00u, 0x00u};
  static const ctool_u8 missing_segment[] = {
      0x66u, 0x66u, 0x0fu, 0x1fu, 0x84u,
      0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
  static const ctool_u8 seven_prefixes[] = {
      0x66u, 0x66u, 0x66u, 0x66u, 0x66u, 0x66u, 0x66u,
      0x2eu, 0x0fu, 0x1fu, 0x84u, 0x00u,
      0x00u, 0x00u, 0x00u, 0x00u};
  static const ctool_u8 duplicate_short_nop[] = {
      0x66u, 0x66u, 0x90u};
  static const struct {
    const ctool_u8 *bytes;
    ctool_u32 size;
    const char *name;
  } near_misses[] = {
      {wrong_segment, (ctool_u32)sizeof(wrong_segment),
       "Clang padding wrong segment"},
      {wrong_opcode, (ctool_u32)sizeof(wrong_opcode),
       "Clang padding wrong opcode"},
      {wrong_modrm, (ctool_u32)sizeof(wrong_modrm),
       "Clang padding wrong ModRM"},
      {wrong_sib, (ctool_u32)sizeof(wrong_sib),
       "Clang padding wrong SIB"},
      {nonzero_displacement, (ctool_u32)sizeof(nonzero_displacement),
       "Clang padding nonzero displacement"},
      {missing_segment, (ctool_u32)sizeof(missing_segment),
       "Clang padding missing segment"},
      {seven_prefixes, (ctool_u32)sizeof(seven_prefixes),
       "Clang padding seven prefixes"},
      {duplicate_short_nop, (ctool_u32)sizeof(duplicate_short_nop),
       "ordinary duplicate-prefix NOP"}};
  ctool_host_adapter_t adapter;
  ctool_job_t *job;
  ctool_x86_decoded_t decoded;
  ctool_x86_encoding_t encoding;
  ctool_status_t status;
  ctool_u8 with_return[CTOOL_X86_MAX_INSTRUCTION_BYTES + 1u];
  ctool_u32 prefix_count;
  ctool_u32 cut;
  ctool_u32 index;
  if (!open_job(&adapter, &job)) {
    return 1;
  }

  for (prefix_count = 2u; prefix_count <= 6u; prefix_count++) {
    const ctool_u8 *pattern = longest + (6u - prefix_count);
    ctool_u32 pattern_size = prefix_count + 9u;
    status = ctool_x86_decode(
        job, CTOOL_X86_MODE_32, ctool_bytes(pattern, pattern_size), 0u,
        &decoded);
    if (!check_status(status, CTOOL_OK, "exact Clang padding decode") ||
        !check_true(
            decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                decoded.instruction.mnemonic == CTOOL_X86_MN_NOP &&
                decoded.instruction.operand_bits == 16u &&
                decoded.instruction.address_bits == 32u &&
                decoded.instruction.prefixes == 0u &&
                decoded.instruction.operand_count == 1u &&
                decoded.instruction.operands[0].kind ==
                    CTOOL_X86_OPERAND_MEMORY &&
                decoded.instruction.operands[0].width_bits == 16u &&
                decoded.instruction.operands[0].as.memory.address_bits ==
                    32u &&
                decoded.instruction.operands[0]
                        .as.memory.segment.class_id ==
                    CTOOL_X86_REG_SEGMENT &&
                decoded.instruction.operands[0].as.memory.segment.index ==
                    1u &&
                decoded.instruction.operands[0].as.memory.base.class_id ==
                    CTOOL_X86_REG_GPR32 &&
                decoded.instruction.operands[0].as.memory.base.index == 0u &&
                decoded.instruction.operands[0].as.memory.index.class_id ==
                    CTOOL_X86_REG_GPR32 &&
                decoded.instruction.operands[0].as.memory.index.index == 0u &&
                decoded.instruction.operands[0].as.memory.scale == 1u &&
                decoded.instruction.operands[0]
                        .as.memory.displacement.kind ==
                    CTOOL_X86_VALUE_CONSTANT &&
                decoded.instruction.operands[0]
                        .as.memory.displacement.bits == 0u &&
                decoded.instruction.operands[0]
                        .as.memory.displacement_bits == 32u &&
                decoded.encoding.form == CTOOL_X86_FORM_AUTO &&
                decoded.encoding.size == pattern_size &&
                decoded.encoding.field_count == 1u &&
                decoded.encoding.fields[0].kind ==
                    CTOOL_X86_FIELD_DISPLACEMENT &&
                decoded.encoding.fields[0].relocation ==
                    CTOOL_X86_RELOC_NONE &&
                decoded.encoding.fields[0].operand_index == 0u &&
                decoded.encoding.fields[0].byte_offset ==
                    prefix_count + 5u &&
                decoded.encoding.fields[0].byte_width == 4u &&
                decoded.consumed == pattern_size &&
                memcmp(decoded.encoding.bytes, pattern,
                       (size_t)pattern_size) == 0,
            "exact Clang padding semantics")) {
      ctool_job_close(job);
      return 1;
    }

    status = ctool_x86_encode(job, CTOOL_X86_MODE_32,
                              &decoded.instruction, decoded.encoding.form,
                              &encoding);
    if (!check_status(status, CTOOL_OK,
                      "Clang padding canonical re-encode") ||
        !bytes_equal(&encoding, canonical, (ctool_u8)sizeof(canonical),
                     "Clang padding canonical bytes") ||
        !check_true(encoding.size != pattern_size,
                    "Clang padding is decode-only")) {
      ctool_job_close(job);
      return 1;
    }

    (void)memcpy(with_return, pattern, (size_t)pattern_size);
    with_return[pattern_size] = 0xc3u;
    status = ctool_x86_decode(
        job, CTOOL_X86_MODE_32,
        ctool_bytes(with_return, pattern_size + 1u), pattern_size,
        &decoded);
    if (!check_status(status, CTOOL_OK,
                      "Clang padding following return decode") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                        decoded.instruction.mnemonic == CTOOL_X86_MN_RET &&
                        decoded.consumed == 1u,
                    "Clang padding following return boundary")) {
      ctool_job_close(job);
      return 1;
    }

    status = ctool_x86_decode(
        job, CTOOL_X86_MODE_16, ctool_bytes(pattern, pattern_size), 0u,
        &decoded);
    if (!check_status(status, CTOOL_OK,
                      "Clang padding 16-bit mode rejection") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_INVALID &&
                        decoded.consumed == 1u &&
                        decoded.encoding.size == 1u &&
                        decoded.encoding.bytes[0] == 0x66u,
                    "Clang padding remains mode-specific")) {
      ctool_job_close(job);
      return 1;
    }

    for (cut = 1u; cut < pattern_size; cut++) {
      status = ctool_x86_decode(
          job, CTOOL_X86_MODE_32, ctool_bytes(pattern, cut), 0u,
          &decoded);
      if (!check_status(status, CTOOL_OK,
                        "Clang padding every-byte cut") ||
          !check_true(
              cut == 1u
                  ? decoded.kind == CTOOL_X86_DECODE_TRUNCATED &&
                        decoded.consumed == 0u &&
                        decoded.encoding.size == 1u &&
                        decoded.encoding.bytes[0] == 0x66u
                  : decoded.kind == CTOOL_X86_DECODE_INVALID &&
                        decoded.consumed == 1u &&
                        decoded.encoding.size == 1u &&
                        decoded.encoding.bytes[0] == 0x66u,
              "Clang padding cut classification")) {
        ctool_job_close(job);
        return 1;
      }
    }
  }

  for (index = 0u;
       index <
       (ctool_u32)(sizeof(near_misses) / sizeof(near_misses[0]));
       index++) {
    status = ctool_x86_decode(
        job, CTOOL_X86_MODE_32,
        ctool_bytes(near_misses[index].bytes, near_misses[index].size),
        0u, &decoded);
    if (!check_status(status, CTOOL_OK, near_misses[index].name) ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_INVALID &&
                        decoded.consumed == 1u &&
                        decoded.encoding.size == 1u &&
                        decoded.encoding.bytes[0] == 0x66u,
                    near_misses[index].name)) {
      ctool_job_close(job);
      return 1;
    }
  }

  ctool_job_close(job);
  (void)puts("clang-padding-nops: ok");
  return 0;
}

typedef struct {
  ctool_x86_mnemonic_t mnemonic;
  const char *name;
  ctool_u8 opcode;
} conditional_move_case_t;

typedef struct {
  const char *name;
  ctool_x86_mnemonic_t mnemonic;
} conditional_move_alias_t;

static int expect_conditional_move_encode_failure(
    ctool_job_t *job, const ctool_x86_instruction_t *insn,
    const char *operation) {
  ctool_x86_encoding_t encoding;
  ctool_status_t status;
  (void)memset(&encoding, 0xa5, sizeof(encoding));
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32, insn,
                            CTOOL_X86_FORM_AUTO, &encoding);
  return check_status(status, CTOOL_ERR_INPUT, operation) &&
         check_true(encoding_is_zero(&encoding), operation);
}

static int run_conditional_moves(void) {
  static const conditional_move_case_t cases[] = {
      {CTOOL_X86_MN_CMOVO, "cmovo", 0x40u},
      {CTOOL_X86_MN_CMOVNO, "cmovno", 0x41u},
      {CTOOL_X86_MN_CMOVB, "cmovb", 0x42u},
      {CTOOL_X86_MN_CMOVAE, "cmovae", 0x43u},
      {CTOOL_X86_MN_CMOVE, "cmove", 0x44u},
      {CTOOL_X86_MN_CMOVNE, "cmovne", 0x45u},
      {CTOOL_X86_MN_CMOVBE, "cmovbe", 0x46u},
      {CTOOL_X86_MN_CMOVA, "cmova", 0x47u},
      {CTOOL_X86_MN_CMOVS, "cmovs", 0x48u},
      {CTOOL_X86_MN_CMOVNS, "cmovns", 0x49u},
      {CTOOL_X86_MN_CMOVP, "cmovp", 0x4au},
      {CTOOL_X86_MN_CMOVNP, "cmovnp", 0x4bu},
      {CTOOL_X86_MN_CMOVL, "cmovl", 0x4cu},
      {CTOOL_X86_MN_CMOVGE, "cmovge", 0x4du},
      {CTOOL_X86_MN_CMOVLE, "cmovle", 0x4eu},
      {CTOOL_X86_MN_CMOVG, "cmovg", 0x4fu}};
  static const conditional_move_alias_t aliases[] = {
      {"cmovc", CTOOL_X86_MN_CMOVB},
      {"cmovnae", CTOOL_X86_MN_CMOVB},
      {"cmovnc", CTOOL_X86_MN_CMOVAE},
      {"cmovnb", CTOOL_X86_MN_CMOVAE},
      {"cmovz", CTOOL_X86_MN_CMOVE},
      {"cmovnz", CTOOL_X86_MN_CMOVNE},
      {"cmovna", CTOOL_X86_MN_CMOVBE},
      {"cmovnbe", CTOOL_X86_MN_CMOVA},
      {"cmovpe", CTOOL_X86_MN_CMOVP},
      {"cmovpo", CTOOL_X86_MN_CMOVNP},
      {"cmovnge", CTOOL_X86_MN_CMOVL},
      {"cmovnl", CTOOL_X86_MN_CMOVGE},
      {"cmovng", CTOOL_X86_MN_CMOVLE},
      {"cmovnle", CTOOL_X86_MN_CMOVG}};
  static const ctool_u8 illegal_prefix_bytes[] = {0xf0u, 0xf3u, 0xf2u};
  static const ctool_u8 semantic_prefixes[] = {
      CTOOL_X86_PREFIX_LOCK, CTOOL_X86_PREFIX_REP,
      CTOOL_X86_PREFIX_REPNE};
  static const ctool_u8 truncated_opcode[] = {0x0fu};
  static const ctool_u8 truncated_modrm[] = {0x0fu, 0x45u};
  static const ctool_u8 valid_bytes[] = {0x0fu, 0x45u, 0xc1u};
  ctool_host_adapter_t adapter;
  ctool_job_t *job;
  ctool_x86_instruction_t insn;
  ctool_x86_encoding_t encoding;
  ctool_x86_encoding_t replay;
  ctool_x86_decoded_t decoded;
  ctool_x86_mnemonic_t found;
  ctool_x86_form_t valid_form;
  ctool_status_t status;
  ctool_u32 case_index;
  ctool_u32 alias_index;
  ctool_u32 mode_index;
  ctool_u32 width_index;
  ctool_u32 source_index;
  ctool_u32 prefix_index;
  if (!open_job(&adapter, &job)) {
    return 1;
  }

  for (alias_index = 0u;
       alias_index <
       (ctool_u32)(sizeof(aliases) / sizeof(aliases[0]));
       alias_index++) {
    status = ctool_x86_mnemonic_from_name(
        ctool_string(aliases[alias_index].name), &found);
    if (!check_status(status, CTOOL_OK, aliases[alias_index].name) ||
        !check_true(found == aliases[alias_index].mnemonic,
                    "conditional move alias canonicalization")) {
      ctool_job_close(job);
      return 1;
    }
  }

  for (case_index = 0u;
       case_index < (ctool_u32)(sizeof(cases) / sizeof(cases[0]));
       case_index++) {
    ctool_string_t canonical;
    status = ctool_x86_mnemonic_from_name(ctool_string(cases[case_index].name),
                                           &found);
    canonical = ctool_x86_mnemonic_name(cases[case_index].mnemonic);
    if (!check_status(status, CTOOL_OK, cases[case_index].name) ||
        !check_true(found == cases[case_index].mnemonic &&
                        canonical.size == strlen(cases[case_index].name) &&
                        memcmp(canonical.data, cases[case_index].name,
                               canonical.size) == 0,
                    "conditional move canonical name")) {
      ctool_job_close(job);
      return 1;
    }
    for (mode_index = 0u; mode_index < 2u; mode_index++) {
      ctool_x86_mode_t mode = mode_index == 0u ? CTOOL_X86_MODE_16
                                               : CTOOL_X86_MODE_32;
      ctool_u16 address_bits = mode == CTOOL_X86_MODE_16 ? 16u : 32u;
      for (width_index = 0u; width_index < 2u; width_index++) {
        ctool_u16 width_bits = width_index == 0u ? 16u : 32u;
        ctool_x86_reg_class_t register_class =
            width_bits == 16u ? CTOOL_X86_REG_GPR16 : CTOOL_X86_REG_GPR32;
        for (source_index = 0u; source_index < 2u; source_index++) {
          ctool_u8 expected[6];
          ctool_u8 expected_size = 0u;
          ctool_bool memory_source =
              source_index == 0u ? CTOOL_FALSE : CTOOL_TRUE;
          ctool_x86_form_t form;
          insn = instruction(cases[case_index].mnemonic, width_bits,
                             address_bits, 0u);
          insn.operand_count = 2u;
          insn.operands[0] = register_operand(register_class, 0u);
          if (memory_source == CTOOL_FALSE) {
            insn.operands[1] = register_operand(register_class, 1u);
          } else if (mode == CTOOL_X86_MODE_16) {
            insn.operands[1] = memory_operand(
                width_bits, 16u, reg(CTOOL_X86_REG_NONE, 0u),
                reg(CTOOL_X86_REG_GPR16, 3u),
                reg(CTOOL_X86_REG_GPR16, 6u), 1u, 0x7f, 8u);
          } else {
            insn.operands[1] = memory_operand(
                width_bits, 32u, reg(CTOOL_X86_REG_NONE, 0u),
                reg(CTOOL_X86_REG_GPR32, 3u),
                reg(CTOOL_X86_REG_NONE, 0u), 1u, 0x7f, 8u);
          }
          if ((mode == CTOOL_X86_MODE_16 && width_bits == 32u) ||
              (mode == CTOOL_X86_MODE_32 && width_bits == 16u)) {
            expected[expected_size++] = 0x66u;
          }
          expected[expected_size++] = 0x0fu;
          expected[expected_size++] = cases[case_index].opcode;
          if (memory_source == CTOOL_FALSE) {
            expected[expected_size++] = 0xc1u;
          } else {
            expected[expected_size++] =
                mode == CTOOL_X86_MODE_16 ? 0x40u : 0x43u;
            expected[expected_size++] = 0x7fu;
          }
          if (!encode(job, mode, &insn, &encoding,
                      "conditional move encode") ||
              !bytes_equal(&encoding, expected, expected_size,
                           "conditional move exact bytes")) {
            (void)fprintf(stderr, "conditional move case failed: %s\n",
                          cases[case_index].name);
            ctool_job_close(job);
            return 1;
          }
          status = ctool_x86_decode(
              job, mode, ctool_bytes(expected, expected_size), 0u,
              &decoded);
          if (!check_status(status, CTOOL_OK,
                            "conditional move decode") ||
              !check_true(
                  decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                      decoded.consumed == expected_size &&
                      decoded.instruction.mnemonic ==
                          cases[case_index].mnemonic &&
                      decoded.instruction.operand_bits == width_bits &&
                      decoded.instruction.operand_count == 2u &&
                      decoded.instruction.operands[0].kind ==
                          CTOOL_X86_OPERAND_REGISTER &&
                      decoded.instruction.operands[0].as.reg.class_id ==
                          register_class &&
                      decoded.instruction.operands[0].as.reg.index == 0u &&
                      decoded.instruction.operands[1].kind ==
                          (memory_source == CTOOL_FALSE
                               ? CTOOL_X86_OPERAND_REGISTER
                               : CTOOL_X86_OPERAND_MEMORY) &&
                      decoded.encoding.form != CTOOL_X86_FORM_AUTO,
                  "conditional move decode semantics")) {
            ctool_job_close(job);
            return 1;
          }
          if (memory_source == CTOOL_FALSE) {
            if (!check_true(
                    decoded.instruction.operands[1].as.reg.class_id ==
                            register_class &&
                        decoded.instruction.operands[1].as.reg.index == 1u,
                    "conditional move register source")) {
              ctool_job_close(job);
              return 1;
            }
          } else if (!check_true(
                         decoded.instruction.operands[1].width_bits ==
                                 width_bits &&
                             decoded.instruction.operands[1]
                                     .as.memory.address_bits == address_bits,
                         "conditional move memory source")) {
            ctool_job_close(job);
            return 1;
          }
          form = decoded.encoding.form;
          status = ctool_x86_encode(job, mode, &decoded.instruction, form,
                                    &replay);
          if (!check_status(status, CTOOL_OK,
                            "conditional move requested form") ||
              !bytes_equal(&replay, expected, expected_size,
                           "conditional move requested-form bytes")) {
            ctool_job_close(job);
            return 1;
          }
        }
      }
    }
  }

  insn = instruction(CTOOL_X86_MN_CMOVNE, 8u, 32u, 0u);
  insn.operand_count = 2u;
  insn.operands[0] = register_operand(CTOOL_X86_REG_GPR8, 0u);
  insn.operands[1] = register_operand(CTOOL_X86_REG_GPR8, 1u);
  if (!expect_conditional_move_encode_failure(
          job, &insn, "conditional move byte operands")) {
    ctool_job_close(job);
    return 1;
  }
  insn = instruction(CTOOL_X86_MN_CMOVNE, 32u, 32u, 0u);
  insn.operand_count = 2u;
  insn.operands[0] = register_operand(CTOOL_X86_REG_GPR32, 0u);
  insn.operands[1] = value_operand(CTOOL_X86_OPERAND_IMMEDIATE, 32u, 32u,
                                   constant(1u));
  if (!expect_conditional_move_encode_failure(
          job, &insn, "conditional move immediate source")) {
    ctool_job_close(job);
    return 1;
  }
  insn.operands[1] = register_operand(CTOOL_X86_REG_GPR16, 1u);
  if (!expect_conditional_move_encode_failure(
          job, &insn, "conditional move width mismatch")) {
    ctool_job_close(job);
    return 1;
  }
  insn.operands[1] = register_operand(CTOOL_X86_REG_GPR32, 1u);
  for (prefix_index = 0u;
       prefix_index <
       (ctool_u32)(sizeof(semantic_prefixes) /
                   sizeof(semantic_prefixes[0]));
       prefix_index++) {
    insn.prefixes = semantic_prefixes[prefix_index];
    if (!expect_conditional_move_encode_failure(
            job, &insn, "conditional move semantic prefix")) {
      ctool_job_close(job);
      return 1;
    }
  }

  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_32,
      ctool_bytes(truncated_opcode,
                  (ctool_u32)sizeof(truncated_opcode)),
      0u, &decoded);
  if (!check_status(status, CTOOL_OK,
                    "conditional move truncated opcode") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_TRUNCATED &&
                      decoded.consumed == 0u &&
                      decoded.encoding.size == sizeof(truncated_opcode),
                  "conditional move truncated opcode retention")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_32,
      ctool_bytes(truncated_modrm,
                  (ctool_u32)sizeof(truncated_modrm)),
      0u, &decoded);
  if (!check_status(status, CTOOL_OK,
                    "conditional move truncated ModRM") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_TRUNCATED &&
                      decoded.consumed == 0u &&
                      decoded.encoding.size == sizeof(truncated_modrm),
                  "conditional move truncated ModRM retention")) {
    ctool_job_close(job);
    return 1;
  }
  for (prefix_index = 0u;
       prefix_index <
       (ctool_u32)(sizeof(illegal_prefix_bytes) /
                   sizeof(illegal_prefix_bytes[0]));
       prefix_index++) {
    ctool_u8 illegal[4];
    illegal[0] = illegal_prefix_bytes[prefix_index];
    illegal[1] = 0x0fu;
    illegal[2] = 0x45u;
    illegal[3] = 0xc1u;
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                              ctool_bytes(illegal, 4u), 0u, &decoded);
    if (!check_status(status, CTOOL_OK,
                      "conditional move illegal prefix decode") ||
        !check_true(decoded.kind ==
                            (prefix_index == 0u
                                 ? CTOOL_X86_DECODE_INVALID
                                 : CTOOL_X86_DECODE_UNKNOWN) &&
                        decoded.consumed == 1u &&
                        decoded.encoding.size == 1u &&
                        decoded.encoding.bytes[0] == illegal[0],
                    "conditional move illegal prefix classification")) {
      ctool_job_close(job);
      return 1;
    }
  }

  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_32,
      ctool_bytes(valid_bytes, (ctool_u32)sizeof(valid_bytes)), 0u,
      &decoded);
  if (!check_status(status, CTOOL_OK,
                    "conditional move same-job recovery") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                      decoded.instruction.mnemonic == CTOOL_X86_MN_CMOVNE,
                  "conditional move recovered decode")) {
    ctool_job_close(job);
    return 1;
  }
  valid_form = decoded.encoding.form;
  insn = decoded.instruction;
  insn.mnemonic = CTOOL_X86_MN_CMOVE;
  (void)memset(&encoding, 0xa5, sizeof(encoding));
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn, valid_form,
                            &encoding);
  if (!check_status(status, CTOOL_ERR_INPUT,
                    "conditional move mismatched requested form") ||
      !check_true(encoding_is_zero(&encoding),
                  "conditional move mismatched form zeroed output")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32,
                            &decoded.instruction, valid_form, &encoding);
  if (!check_status(status, CTOOL_OK,
                    "conditional move recovery requested form") ||
      !bytes_equal(&encoding, valid_bytes,
                   (ctool_u8)sizeof(valid_bytes),
                   "conditional move recovered exact bytes")) {
    ctool_job_close(job);
    return 1;
  }

  ctool_job_close(job);
  (void)printf("conditional-moves: ok\n");
  return 0;
}

static int expect_parity_setcc_encode_failure(
    ctool_job_t *job, const ctool_x86_instruction_t *insn,
    const char *operation) {
  ctool_x86_encoding_t encoding;
  ctool_status_t status;
  (void)memset(&encoding, 0xa5, sizeof(encoding));
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32, insn,
                            CTOOL_X86_FORM_AUTO, &encoding);
  return check_status(status, CTOOL_ERR_INPUT, operation) &&
         check_true(encoding_is_zero(&encoding), operation);
}

static int run_parity_setcc(void) {
  static const char *const names[] = {"setp", "setnp"};
  static const char *const excluded_aliases[] = {"setpe", "setpo"};
  static const ctool_u8 opcodes[] = {0x9au, 0x9bu};
  static const ctool_u8 semantic_prefixes[] = {
      CTOOL_X86_PREFIX_LOCK, CTOOL_X86_PREFIX_REP,
      CTOOL_X86_PREFIX_REPNE};
  static const ctool_u8 truncated_memory[] = {
      0x67u, 0x0fu, 0x9au, 0x84u, 0x8bu,
      0x78u, 0x56u, 0x34u, 0x12u};
  static const ctool_u8 valid_setp[] = {0x0fu, 0x9au, 0xc2u};
  ctool_host_adapter_t adapter;
  ctool_job_t *job;
  ctool_x86_mnemonic_t mnemonics[2];
  ctool_x86_mnemonic_t excluded;
  ctool_x86_instruction_t insn;
  ctool_x86_encoding_t encoding;
  ctool_x86_encoding_t replay;
  ctool_x86_decoded_t decoded;
  ctool_x86_form_t setp_form;
  ctool_status_t status;
  ctool_u32 mnemonic_index;
  ctool_u32 alias_index;
  ctool_u32 mode_index;
  ctool_u32 operand_index;
  ctool_u32 prefix_index;
  ctool_u32 cut;
  if (!open_job(&adapter, &job)) {
    return 1;
  }

  for (mnemonic_index = 0u; mnemonic_index < 2u; mnemonic_index++) {
    ctool_string_t canonical;
    status = ctool_x86_mnemonic_from_name(
        ctool_string(names[mnemonic_index]), &mnemonics[mnemonic_index]);
    if (!check_status(status, CTOOL_OK, names[mnemonic_index])) {
      ctool_job_close(job);
      return 1;
    }
    canonical = ctool_x86_mnemonic_name(mnemonics[mnemonic_index]);
    if (!check_true(canonical.size == strlen(names[mnemonic_index]) &&
                        memcmp(canonical.data, names[mnemonic_index],
                               canonical.size) == 0,
                    "parity SETcc canonical name")) {
      ctool_job_close(job);
      return 1;
    }
  }

  for (alias_index = 0u;
       alias_index <
       (ctool_u32)(sizeof(excluded_aliases) / sizeof(excluded_aliases[0]));
       alias_index++) {
    status = ctool_x86_mnemonic_from_name(
        ctool_string(excluded_aliases[alias_index]), &excluded);
    if (!check_status(status, CTOOL_ERR_NOT_FOUND,
                      "parity SETcc excluded alias")) {
      ctool_job_close(job);
      return 1;
    }
  }

  for (mnemonic_index = 0u; mnemonic_index < 2u; mnemonic_index++) {
    for (mode_index = 0u; mode_index < 2u; mode_index++) {
      ctool_x86_mode_t mode = mode_index == 0u ? CTOOL_X86_MODE_16
                                               : CTOOL_X86_MODE_32;
      for (operand_index = 0u; operand_index < 3u; operand_index++) {
        ctool_u8 expected[10];
        ctool_u8 expected_size = 0u;
        ctool_u16 address_bits = mode == CTOOL_X86_MODE_16 ? 16u : 32u;
        ctool_x86_operand_kind_t expected_kind =
            operand_index == 0u ? CTOOL_X86_OPERAND_REGISTER
                                : CTOOL_X86_OPERAND_MEMORY;
        insn = instruction(mnemonics[mnemonic_index], 8u, address_bits, 0u);
        insn.operand_count = 1u;
        if (operand_index == 0u) {
          insn.operands[0] = register_operand(CTOOL_X86_REG_GPR8, 2u);
        } else if ((operand_index == 1u && mode == CTOOL_X86_MODE_16) ||
                   (operand_index == 2u && mode == CTOOL_X86_MODE_32)) {
          address_bits = 16u;
          insn.address_bits = address_bits;
          insn.operands[0] = memory_operand(
              8u, address_bits, reg(CTOOL_X86_REG_NONE, 0u),
              reg(CTOOL_X86_REG_GPR16, 3u),
              reg(CTOOL_X86_REG_GPR16, 6u), 1u, 0x7f, 8u);
        } else {
          address_bits = 32u;
          insn.address_bits = address_bits;
          insn.operands[0] = memory_operand(
              8u, address_bits, reg(CTOOL_X86_REG_NONE, 0u),
              reg(CTOOL_X86_REG_GPR32, 3u),
              reg(CTOOL_X86_REG_GPR32, 1u), 4u, 0x12345678, 32u);
        }
        if (operand_index != 0u &&
            ((mode == CTOOL_X86_MODE_16 && address_bits == 32u) ||
             (mode == CTOOL_X86_MODE_32 && address_bits == 16u))) {
          expected[expected_size++] = 0x67u;
        }
        expected[expected_size++] = 0x0fu;
        expected[expected_size++] = opcodes[mnemonic_index];
        if (operand_index == 0u) {
          expected[expected_size++] = 0xc2u;
        } else if (address_bits == 16u) {
          expected[expected_size++] = 0x40u;
          expected[expected_size++] = 0x7fu;
        } else {
          expected[expected_size++] = 0x84u;
          expected[expected_size++] = 0x8bu;
          expected[expected_size++] = 0x78u;
          expected[expected_size++] = 0x56u;
          expected[expected_size++] = 0x34u;
          expected[expected_size++] = 0x12u;
        }
        if (!encode(job, mode, &insn, &encoding, "parity SETcc encode") ||
            !bytes_equal(&encoding, expected, expected_size,
                         "parity SETcc exact bytes")) {
          ctool_job_close(job);
          return 1;
        }
        status = ctool_x86_decode(
            job, mode, ctool_bytes(expected, expected_size), 0u, &decoded);
        if (!check_status(status, CTOOL_OK, "parity SETcc decode") ||
            !check_true(
                decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                    decoded.consumed == expected_size &&
                    decoded.instruction.mnemonic == mnemonics[mnemonic_index] &&
                    decoded.instruction.operand_bits == 8u &&
                    decoded.instruction.operand_count == 1u &&
                    decoded.instruction.operands[0].kind == expected_kind &&
                    decoded.encoding.form != CTOOL_X86_FORM_AUTO,
                "parity SETcc decode semantics")) {
          ctool_job_close(job);
          return 1;
        }
        if (expected_kind == CTOOL_X86_OPERAND_REGISTER) {
          if (!check_true(
                  decoded.instruction.operands[0].as.reg.class_id ==
                          CTOOL_X86_REG_GPR8 &&
                      decoded.instruction.operands[0].as.reg.index == 2u,
                  "parity SETcc register operand")) {
            ctool_job_close(job);
            return 1;
          }
        } else if (!check_true(
                       decoded.instruction.operands[0].width_bits == 8u &&
                           decoded.instruction.operands[0]
                                   .as.memory.address_bits == address_bits,
                       "parity SETcc memory operand")) {
          ctool_job_close(job);
          return 1;
        }
        status = ctool_x86_encode(job, mode, &decoded.instruction,
                                  decoded.encoding.form, &replay);
        if (!check_status(status, CTOOL_OK,
                          "parity SETcc requested form") ||
            !bytes_equal(&replay, expected, expected_size,
                         "parity SETcc requested-form bytes")) {
          ctool_job_close(job);
          return 1;
        }
      }
    }
  }

  insn = instruction(mnemonics[0], 32u, 32u, 0u);
  insn.operand_count = 1u;
  insn.operands[0] = register_operand(CTOOL_X86_REG_GPR32, 0u);
  if (!expect_parity_setcc_encode_failure(
          job, &insn, "parity SETcc non-byte register")) {
    ctool_job_close(job);
    return 1;
  }
  insn = instruction(mnemonics[1], 8u, 32u, 0u);
  insn.operand_count = 1u;
  insn.operands[0] = value_operand(
      CTOOL_X86_OPERAND_IMMEDIATE, 8u, 8u, constant(1u));
  if (!expect_parity_setcc_encode_failure(
          job, &insn, "parity SETcc immediate operand")) {
    ctool_job_close(job);
    return 1;
  }
  insn.operand_count = 0u;
  if (!expect_parity_setcc_encode_failure(
          job, &insn, "parity SETcc missing operand")) {
    ctool_job_close(job);
    return 1;
  }
  insn = instruction(mnemonics[0], 8u, 32u, 0u);
  insn.operand_count = 1u;
  insn.operands[0] = register_operand(CTOOL_X86_REG_GPR8, 2u);
  for (prefix_index = 0u;
       prefix_index <
       (ctool_u32)(sizeof(semantic_prefixes) /
                   sizeof(semantic_prefixes[0]));
       prefix_index++) {
    insn.prefixes = semantic_prefixes[prefix_index];
    if (!expect_parity_setcc_encode_failure(
            job, &insn, "parity SETcc semantic prefix")) {
      ctool_job_close(job);
      return 1;
    }
  }

  for (cut = 1u; cut < (ctool_u32)sizeof(truncated_memory); cut++) {
    status = ctool_x86_decode(
        job, CTOOL_X86_MODE_16, ctool_bytes(truncated_memory, cut), 0u,
        &decoded);
    if (!check_status(status, CTOOL_OK,
                      "parity SETcc every-byte truncation") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_TRUNCATED &&
                        decoded.consumed == 0u &&
                        decoded.encoding.size == cut &&
                        memcmp(decoded.encoding.bytes, truncated_memory,
                               (size_t)cut) == 0,
                    "parity SETcc truncation retention")) {
      ctool_job_close(job);
      return 1;
    }
  }

  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_32,
      ctool_bytes(valid_setp, (ctool_u32)sizeof(valid_setp)), 0u, &decoded);
  if (!check_status(status, CTOOL_OK, "parity SETcc recovery decode") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                      decoded.instruction.mnemonic == mnemonics[0],
                  "parity SETcc recovered decode")) {
    ctool_job_close(job);
    return 1;
  }
  setp_form = decoded.encoding.form;
  insn = decoded.instruction;
  insn.mnemonic = mnemonics[1];
  (void)memset(&encoding, 0xa5, sizeof(encoding));
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn, setp_form,
                            &encoding);
  if (!check_status(status, CTOOL_ERR_INPUT,
                    "parity SETcc mismatched requested form") ||
      !check_true(encoding_is_zero(&encoding),
                  "parity SETcc mismatched form rollback")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32,
                            &decoded.instruction, setp_form, &encoding);
  if (!check_status(status, CTOOL_OK,
                    "parity SETcc same-job recovery") ||
      !bytes_equal(&encoding, valid_setp, (ctool_u8)sizeof(valid_setp),
                   "parity SETcc recovered exact bytes")) {
    ctool_job_close(job);
    return 1;
  }

  ctool_job_close(job);
  (void)puts("parity-setcc: ok");
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
  typedef struct {
    ctool_x86_mnemonic_t mnemonic;
    ctool_u16 width_bits;
    ctool_u8 bytes[2];
    const char *name;
  } x87_integer_memory_vector_t;
  static const x87_integer_memory_vector_t x87_integer_vectors[] = {
      {CTOOL_X86_MN_FILD, 16u, {0xdfu, 0x00u}, "fild word"},
      {CTOOL_X86_MN_FILD, 32u, {0xdbu, 0x00u}, "fild dword"},
      {CTOOL_X86_MN_FILD, 64u, {0xdfu, 0x28u}, "fild qword"},
      {CTOOL_X86_MN_FISTP, 16u, {0xdfu, 0x18u}, "fistp word"},
      {CTOOL_X86_MN_FISTP, 32u, {0xdbu, 0x18u}, "fistp dword"},
      {CTOOL_X86_MN_FISTP, 64u, {0xdfu, 0x38u}, "fistp qword"},
  };
  static const ctool_u8 cr_bytes[] = {0x0fu, 0x22u, 0xc0u};
  static const ctool_u8 fldz_bytes[] = {0xd9u, 0xeeu};
  static const ctool_u8 fsub_st1_st0_bytes[] = {0xdcu, 0xe9u};
  static const ctool_u8 fxsave_bytes[] = {0x0fu, 0xaeu, 0x40u, 0x10u};
  static const ctool_u8 fucomip_bytes[] = {0xdfu, 0xe9u};
  static const ctool_u8 fsin_bytes[] = {0xd9u, 0xfeu};
  static const ctool_u8 movss_bytes[] = {0xf3u, 0x0fu, 0x10u, 0x00u};
  static const ctool_u8 pxor_bytes[] = {0x66u, 0x0fu, 0xefu, 0xcau};
  ctool_host_adapter_t adapter;
  ctool_job_t *job;
  ctool_x86_instruction_t insn;
  ctool_x86_encoding_t encoding;
  ctool_x86_decoded_t decoded;
  ctool_status_t status;
  ctool_u32 index;
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

  insn = instruction(CTOOL_X86_MN_FLDZ, 32u, 32u, 0u);
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "fldz") ||
      !bytes_equal(&encoding, fldz_bytes, (ctool_u8)sizeof(fldz_bytes),
                   "fldz bytes")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_32,
      ctool_bytes(fldz_bytes, (ctool_u32)sizeof(fldz_bytes)), 0u,
      &decoded);
  if (!check_status(status, CTOOL_OK, "fldz decode") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                      decoded.instruction.mnemonic == CTOOL_X86_MN_FLDZ &&
                      decoded.instruction.operand_count == 0u,
                  "fldz decode semantics")) {
    ctool_job_close(job);
    return 1;
  }

  for (index = 0u;
       index < (ctool_u32)(sizeof(x87_integer_vectors) /
                           sizeof(x87_integer_vectors[0]));
       index++) {
    const x87_integer_memory_vector_t *vector =
        &x87_integer_vectors[index];
    insn = instruction(vector->mnemonic, 32u, 32u, 0u);
    insn.operand_count = 1u;
    insn.operands[0] = memory_operand(
        vector->width_bits, 32u, reg(CTOOL_X86_REG_NONE, 0u),
        reg(CTOOL_X86_REG_GPR32, 0u), reg(CTOOL_X86_REG_NONE, 0u),
        1u, 0, 0u);
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, vector->name) ||
        !bytes_equal(&encoding, vector->bytes,
                     (ctool_u8)sizeof(vector->bytes), vector->name)) {
      ctool_job_close(job);
      return 1;
    }
    status = ctool_x86_decode(
        job, CTOOL_X86_MODE_32,
        ctool_bytes(vector->bytes, (ctool_u32)sizeof(vector->bytes)), 0u,
        &decoded);
    if (!check_status(status, CTOOL_OK, vector->name) ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                        decoded.instruction.mnemonic == vector->mnemonic &&
                        decoded.instruction.operand_count == 1u &&
                        decoded.instruction.operands[0].kind ==
                            CTOOL_X86_OPERAND_MEMORY &&
                        decoded.instruction.operands[0].width_bits ==
                            vector->width_bits,
                    vector->name)) {
      ctool_job_close(job);
      return 1;
    }
  }

  insn = instruction(CTOOL_X86_MN_FILD, 32u, 32u, 0u);
  insn.operand_count = 1u;
  insn.operands[0] = register_operand(CTOOL_X86_REG_GPR32, 0u);
  (void)memset(&encoding, 0xa5, sizeof(encoding));
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn,
                            CTOOL_X86_FORM_AUTO, &encoding);
  if (!check_status(status, CTOOL_ERR_INPUT, "fild register operand") ||
      !check_true(encoding_is_zero(&encoding),
                  "fild register operand zeroed output")) {
    ctool_job_close(job);
    return 1;
  }

  insn = instruction(CTOOL_X86_MN_FISTP, 32u, 32u, 0u);
  insn.operand_count = 1u;
  insn.operands[0] = memory_operand(
      80u, 32u, reg(CTOOL_X86_REG_NONE, 0u),
      reg(CTOOL_X86_REG_GPR32, 0u), reg(CTOOL_X86_REG_NONE, 0u),
      1u, 0, 0u);
  (void)memset(&encoding, 0xa5, sizeof(encoding));
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn,
                            CTOOL_X86_FORM_AUTO, &encoding);
  if (!check_status(status, CTOOL_ERR_INPUT, "fistp tword operand") ||
      !check_true(encoding_is_zero(&encoding),
                  "fistp tword operand zeroed output")) {
    ctool_job_close(job);
    return 1;
  }

  insn = instruction(CTOOL_X86_MN_FUCOMIP, 32u, 32u, 0u);
  insn.operand_count = 2u;
  insn.operands[0] = register_operand(CTOOL_X86_REG_X87, 0u);
  insn.operands[1] = register_operand(CTOOL_X86_REG_X87, 1u);
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding,
              "fucomip st0, st1") ||
      !bytes_equal(&encoding, fucomip_bytes,
                   (ctool_u8)sizeof(fucomip_bytes), "fucomip bytes")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_32,
      ctool_bytes(fucomip_bytes, (ctool_u32)sizeof(fucomip_bytes)), 0u,
      &decoded);
  if (!check_status(status, CTOOL_OK, "fucomip decode") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                      decoded.instruction.mnemonic == CTOOL_X86_MN_FUCOMIP &&
                      decoded.instruction.operand_count == 2u &&
                      decoded.instruction.operands[0].kind ==
                          CTOOL_X86_OPERAND_REGISTER &&
                      decoded.instruction.operands[0].as.reg.class_id ==
                          CTOOL_X86_REG_X87 &&
                      decoded.instruction.operands[0].as.reg.index == 0u &&
                      decoded.instruction.operands[1].kind ==
                          CTOOL_X86_OPERAND_REGISTER &&
                      decoded.instruction.operands[1].as.reg.class_id ==
                          CTOOL_X86_REG_X87 &&
                      decoded.instruction.operands[1].as.reg.index == 1u,
                  "fucomip decode semantics")) {
    ctool_job_close(job);
    return 1;
  }

  insn = instruction(CTOOL_X86_MN_FSUB, 32u, 32u, 0u);
  insn.operand_count = 2u;
  insn.operands[0] = register_operand(CTOOL_X86_REG_X87, 1u);
  insn.operands[1] = register_operand(CTOOL_X86_REG_X87, 0u);
  if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding,
              "fsub st1, st0") ||
      !bytes_equal(&encoding, fsub_st1_st0_bytes,
                   (ctool_u8)sizeof(fsub_st1_st0_bytes),
                   "fsub st1, st0 bytes")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_32,
      ctool_bytes(fsub_st1_st0_bytes,
                  (ctool_u32)sizeof(fsub_st1_st0_bytes)),
      0u, &decoded);
  if (!check_status(status, CTOOL_OK, "fsub st1, st0 decode") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                      decoded.instruction.mnemonic == CTOOL_X86_MN_FSUB &&
                      decoded.instruction.operand_count == 2u &&
                      decoded.instruction.operands[0].kind ==
                          CTOOL_X86_OPERAND_REGISTER &&
                      decoded.instruction.operands[0].as.reg.class_id ==
                          CTOOL_X86_REG_X87 &&
                      decoded.instruction.operands[0].as.reg.index == 1u &&
                      decoded.instruction.operands[1].kind ==
                          CTOOL_X86_OPERAND_REGISTER &&
                      decoded.instruction.operands[1].as.reg.class_id ==
                          CTOOL_X86_REG_X87 &&
                      decoded.instruction.operands[1].as.reg.index == 0u,
                  "fsub st1, st0 decode semantics")) {
    ctool_job_close(job);
    return 1;
  }

  insn = instruction(CTOOL_X86_MN_FSUB, 16u, 16u, 0u);
  insn.operand_count = 2u;
  insn.operands[0] = register_operand(CTOOL_X86_REG_X87, 1u);
  insn.operands[1] = register_operand(CTOOL_X86_REG_X87, 0u);
  if (!encode(job, CTOOL_X86_MODE_16, &insn, &encoding,
              "fsub st1, st0 mode16") ||
      !bytes_equal(&encoding, fsub_st1_st0_bytes,
                   (ctool_u8)sizeof(fsub_st1_st0_bytes),
                   "fsub st1, st0 mode16 bytes")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_decode(
      job, CTOOL_X86_MODE_16,
      ctool_bytes(fsub_st1_st0_bytes,
                  (ctool_u32)sizeof(fsub_st1_st0_bytes)),
      0u, &decoded);
  if (!check_status(status, CTOOL_OK,
                    "fsub st1, st0 mode16 decode") ||
      !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                      decoded.instruction.mnemonic == CTOOL_X86_MN_FSUB &&
                      decoded.instruction.operand_count == 2u &&
                      decoded.instruction.operands[0].kind ==
                          CTOOL_X86_OPERAND_REGISTER &&
                      decoded.instruction.operands[0].as.reg.class_id ==
                          CTOOL_X86_REG_X87 &&
                      decoded.instruction.operands[0].as.reg.index == 1u &&
                      decoded.instruction.operands[1].kind ==
                          CTOOL_X86_OPERAND_REGISTER &&
                      decoded.instruction.operands[1].as.reg.class_id ==
                          CTOOL_X86_REG_X87 &&
                      decoded.instruction.operands[1].as.reg.index == 0u,
                  "fsub st1, st0 mode16 decode semantics")) {
    ctool_job_close(job);
    return 1;
  }

  insn = instruction(CTOOL_X86_MN_FSUB, 32u, 32u, 0u);
  insn.operand_count = 2u;
  insn.operands[0] = register_operand(CTOOL_X86_REG_X87, 0u);
  insn.operands[1] = register_operand(CTOOL_X86_REG_X87, 1u);
  (void)memset(&encoding, 0xa5, sizeof(encoding));
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn,
                            CTOOL_X86_FORM_AUTO, &encoding);
  if (!check_status(status, CTOOL_ERR_INPUT,
                    "fsub fixed destination register") ||
      !check_true(encoding_is_zero(&encoding),
                  "fsub fixed destination zeroed output")) {
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
      {"fld-m80", CTOOL_X86_MODE_32, CTOOL_X86_MN_FLD, 2u,
       {0xdbu, 0x28u}},
      {"fldz", CTOOL_X86_MODE_32, CTOOL_X86_MN_FLDZ, 2u,
       {0xd9u, 0xeeu}},
      {"fninit", CTOOL_X86_MODE_32, CTOOL_X86_MN_FNINIT, 2u,
       {0xdbu, 0xe3u}},
      {"fsin", CTOOL_X86_MODE_32, CTOOL_X86_MN_FSIN, 2u,
       {0xd9u, 0xfeu}},
      {"fstp", CTOOL_X86_MODE_32, CTOOL_X86_MN_FSTP, 2u,
       {0xd9u, 0x18u}},
      {"fstp-m64", CTOOL_X86_MODE_32, CTOOL_X86_MN_FSTP, 2u,
       {0xddu, 0x18u}},
      {"fstp-m80", CTOOL_X86_MODE_32, CTOOL_X86_MN_FSTP, 2u,
       {0xdbu, 0x38u}},
      {"fstp-st0", CTOOL_X86_MODE_32, CTOOL_X86_MN_FSTP, 2u,
       {0xddu, 0xd8u}},
      {"fstp-st1", CTOOL_X86_MODE_32, CTOOL_X86_MN_FSTP, 2u,
       {0xddu, 0xd9u}},
      {"fsub-st1-st0", CTOOL_X86_MODE_32, CTOOL_X86_MN_FSUB, 2u,
       {0xdcu, 0xe9u}},
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
      {"setnp", CTOOL_X86_MODE_32, CTOOL_X86_MN_SETNP, 3u,
       {0x0fu, 0x9bu, 0xc2u}},
      {"setp", CTOOL_X86_MODE_32, CTOOL_X86_MN_SETP, 3u,
       {0x0fu, 0x9au, 0xc2u}},
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
              189u,
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
  static const ctool_u8 trunc_ret[] = {0xc2u, 0x04u, 0x00u};
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
      {trunc_group, (ctool_u32)sizeof(trunc_group)},
      {trunc_ret, (ctool_u32)sizeof(trunc_ret)}};
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
  insn = instruction(CTOOL_X86_MN_FLD, 0u, 32u, 0u);
  insn.operand_count = 1u;
  insn.operands[0] = memory_operand(
      96u, 32u, reg(CTOOL_X86_REG_NONE, 0u),
      reg(CTOOL_X86_REG_GPR32, 0u), reg(CTOOL_X86_REG_NONE, 0u),
      1u, 0, 0u);
  (void)memset(&encoding, 0xa5, sizeof(encoding));
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn,
                            CTOOL_X86_FORM_AUTO, &encoding);
  if (!check_status(status, CTOOL_ERR_INPUT,
                    "unsupported x87 real width") ||
      !check_true(encoding_is_zero(&encoding),
                  "unsupported x87 real width zeroed output")) {
    ctool_job_close(job);
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
  insn = instruction(CTOOL_X86_MN_RET, 32u, 32u, 0u);
  insn.operand_count = 1u;
  insn.operands[0] = value_operand(
      CTOOL_X86_OPERAND_IMMEDIATE, 16u, 16u, constant(0x10000u));
  (void)memset(&encoding, 0xa5, sizeof(encoding));
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn,
                            CTOOL_X86_FORM_AUTO, &encoding);
  if (!check_status(status, CTOOL_ERR_INPUT, "return cleanup overflow") ||
      !check_true(encoding_is_zero(&encoding),
                  "return cleanup overflow zeroed output")) {
    ctool_job_close(job);
    return 1;
  }
  insn.operands[0] = value_operand(
      CTOOL_X86_OPERAND_IMMEDIATE, 32u, 32u, constant(4u));
  (void)memset(&encoding, 0xa5, sizeof(encoding));
  status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn,
                            CTOOL_X86_FORM_AUTO, &encoding);
  if (!check_status(status, CTOOL_ERR_INPUT, "return cleanup width") ||
      !check_true(encoding_is_zero(&encoding),
                  "return cleanup width zeroed output")) {
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
                  "usage: x86-contract inventory|model|decoder-index|integer|conditional-moves|parity-setcc|immediate-imul|double-shift|padding-nops|clang-padding-nops|addressing|relocations|system-simd|active-surface|errors\n");
    return 2;
  }
  if (strcmp(argv[1], "model") == 0) {
    return run_model();
  }
  if (strcmp(argv[1], "inventory") == 0) {
    return run_inventory();
  }
  if (strcmp(argv[1], "decoder-index") == 0) {
    return run_decoder_index();
  }
  if (strcmp(argv[1], "integer") == 0) {
    return run_integer();
  }
  if (strcmp(argv[1], "conditional-moves") == 0) {
    return run_conditional_moves();
  }
  if (strcmp(argv[1], "parity-setcc") == 0) {
    return run_parity_setcc();
  }
  if (strcmp(argv[1], "immediate-imul") == 0) {
    return run_immediate_imul();
  }
  if (strcmp(argv[1], "double-shift") == 0) {
    return run_double_shift();
  }
  if (strcmp(argv[1], "padding-nops") == 0) {
    return run_padding_nops();
  }
  if (strcmp(argv[1], "clang-padding-nops") == 0) {
    return run_clang_padding_nops();
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
