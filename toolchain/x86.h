#ifndef CUPID_TOOLCHAIN_X86_H
#define CUPID_TOOLCHAIN_X86_H

#include "ctool.h"

/* Freestanding semantic model for the Cupid OS 16/32-bit x86 domain. */

#define CTOOL_X86_MAX_INSTRUCTION_BYTES 15u
#define CTOOL_X86_MAX_OPERANDS 3u
#define CTOOL_X86_MAX_FIELDS 3u

typedef enum {
  CTOOL_X86_MODE_16 = 16,
  CTOOL_X86_MODE_32 = 32
} ctool_x86_mode_t;

typedef enum {
  CTOOL_X86_REG_NONE = 0,
  CTOOL_X86_REG_GPR8,
  CTOOL_X86_REG_GPR16,
  CTOOL_X86_REG_GPR32,
  CTOOL_X86_REG_SEGMENT,
  CTOOL_X86_REG_CONTROL,
  CTOOL_X86_REG_DEBUG,
  CTOOL_X86_REG_X87,
  CTOOL_X86_REG_MMX,
  CTOOL_X86_REG_XMM,
  CTOOL_X86_REG_FLAGS,
  CTOOL_X86_REG_INSTRUCTION_POINTER
} ctool_x86_reg_class_t;

typedef struct {
  ctool_x86_reg_class_t class_id;
  ctool_u8 index;
} ctool_x86_reg_t;

typedef enum {
  CTOOL_X86_VALUE_CONSTANT = 0,
  CTOOL_X86_VALUE_REFERENCE
} ctool_x86_value_kind_t;

typedef struct {
  ctool_x86_value_kind_t kind;
  ctool_u32 bits;
  ctool_i32 addend;
  ctool_u32 reference;
} ctool_x86_value_t;

typedef struct {
  ctool_u16 address_bits;
  ctool_x86_reg_t segment;
  ctool_x86_reg_t base;
  ctool_x86_reg_t index;
  ctool_u8 scale;
  ctool_x86_value_t displacement;
  ctool_u16 displacement_bits;
} ctool_x86_memory_t;

typedef struct {
  ctool_x86_value_t offset;
  ctool_x86_value_t segment;
} ctool_x86_far_pointer_t;

typedef enum {
  CTOOL_X86_OPERAND_NONE = 0,
  CTOOL_X86_OPERAND_REGISTER,
  CTOOL_X86_OPERAND_IMMEDIATE,
  CTOOL_X86_OPERAND_RELATIVE,
  CTOOL_X86_OPERAND_MEMORY,
  CTOOL_X86_OPERAND_FAR_POINTER
} ctool_x86_operand_kind_t;

typedef struct {
  ctool_x86_operand_kind_t kind;
  /* Semantic data width and serialized immediate/relative width.  Either may
   * be zero when the selected form supplies the only meaningful default. */
  ctool_u16 width_bits;
  ctool_u16 encoding_bits;
  union {
    ctool_x86_reg_t reg;
    ctool_x86_value_t value;
    ctool_x86_memory_t memory;
    ctool_x86_far_pointer_t far_pointer;
  } as;
} ctool_x86_operand_t;

typedef enum {
  CTOOL_X86_MN_INVALID = 0,
  CTOOL_X86_MN_ADC, CTOOL_X86_MN_ADD, CTOOL_X86_MN_ADDPS,
  CTOOL_X86_MN_ADDSS, CTOOL_X86_MN_ADDSD, CTOOL_X86_MN_ADDPD,
  CTOOL_X86_MN_AND, CTOOL_X86_MN_ANDPS, CTOOL_X86_MN_ANDPD,
  CTOOL_X86_MN_BSWAP, CTOOL_X86_MN_CALL, CTOOL_X86_MN_CBW,
  CTOOL_X86_MN_CDQ, CTOOL_X86_MN_CLC, CTOOL_X86_MN_CLD,
  CTOOL_X86_MN_CLI, CTOOL_X86_MN_CLTS, CTOOL_X86_MN_CMC,
  CTOOL_X86_MN_CMOVA, CTOOL_X86_MN_CMOVAE, CTOOL_X86_MN_CMOVB,
  CTOOL_X86_MN_CMOVBE, CTOOL_X86_MN_CMOVE, CTOOL_X86_MN_CMOVG,
  CTOOL_X86_MN_CMOVGE, CTOOL_X86_MN_CMOVL, CTOOL_X86_MN_CMOVLE,
  CTOOL_X86_MN_CMOVNE, CTOOL_X86_MN_CMOVNO, CTOOL_X86_MN_CMOVNP,
  CTOOL_X86_MN_CMOVNS, CTOOL_X86_MN_CMOVO, CTOOL_X86_MN_CMOVP,
  CTOOL_X86_MN_CMOVS,
  CTOOL_X86_MN_CMP, CTOOL_X86_MN_CMPXCHG, CTOOL_X86_MN_CMPPS,
  CTOOL_X86_MN_CPUID, CTOOL_X86_MN_CVTPS2DQ, CTOOL_X86_MN_CVTDQ2PS,
  CTOOL_X86_MN_CVTSD2SI, CTOOL_X86_MN_CVTSS2SI,
  CTOOL_X86_MN_CVTSI2SD, CTOOL_X86_MN_CVTSI2SS,
  CTOOL_X86_MN_CVTSD2SS, CTOOL_X86_MN_CVTSS2SD,
  CTOOL_X86_MN_CVTTSD2SI, CTOOL_X86_MN_CVTTSS2SI, CTOOL_X86_MN_CWDE,
  CTOOL_X86_MN_DEC, CTOOL_X86_MN_DIV, CTOOL_X86_MN_DIVPD,
  CTOOL_X86_MN_DIVPS, CTOOL_X86_MN_DIVSD, CTOOL_X86_MN_DIVSS,
  CTOOL_X86_MN_F2XM1, CTOOL_X86_MN_FABS, CTOOL_X86_MN_FADD,
  CTOOL_X86_MN_FADDP,
  CTOOL_X86_MN_FCHS, CTOOL_X86_MN_FCOS, CTOOL_X86_MN_FINIT,
  CTOOL_X86_MN_FDIV, CTOOL_X86_MN_FDIVP, CTOOL_X86_MN_FLD,
  CTOOL_X86_MN_FLD1, CTOOL_X86_MN_FLDCW, CTOOL_X86_MN_FMUL,
  CTOOL_X86_MN_FMULP, CTOOL_X86_MN_FNINIT, CTOOL_X86_MN_FNSTCW,
  CTOOL_X86_MN_FNSTSW, CTOOL_X86_MN_FPATAN, CTOOL_X86_MN_FPREM,
  CTOOL_X86_MN_FPTAN,
  CTOOL_X86_MN_FRNDINT, CTOOL_X86_MN_FSCALE, CTOOL_X86_MN_FSIN,
  CTOOL_X86_MN_FSQRT, CTOOL_X86_MN_FST, CTOOL_X86_MN_FSTP,
  CTOOL_X86_MN_FSUB, CTOOL_X86_MN_FSUBP, CTOOL_X86_MN_FSUBR,
  CTOOL_X86_MN_FWAIT,
  CTOOL_X86_MN_FXCH, CTOOL_X86_MN_FXRSTOR, CTOOL_X86_MN_FXSAVE,
  CTOOL_X86_MN_FYL2X, CTOOL_X86_MN_HLT, CTOOL_X86_MN_IDIV,
  CTOOL_X86_MN_IMUL, CTOOL_X86_MN_IN, CTOOL_X86_MN_INC,
  CTOOL_X86_MN_INSW, CTOOL_X86_MN_INT, CTOOL_X86_MN_INT3,
  CTOOL_X86_MN_INVD, CTOOL_X86_MN_INVLPG, CTOOL_X86_MN_IRET,
  CTOOL_X86_MN_IRETD, CTOOL_X86_MN_JA, CTOOL_X86_MN_JAE,
  CTOOL_X86_MN_JB, CTOOL_X86_MN_JBE, CTOOL_X86_MN_JE,
  CTOOL_X86_MN_JG, CTOOL_X86_MN_JGE, CTOOL_X86_MN_JL,
  CTOOL_X86_MN_JLE, CTOOL_X86_MN_JMP, CTOOL_X86_MN_JNE,
  CTOOL_X86_MN_JNO, CTOOL_X86_MN_JNP, CTOOL_X86_MN_JNS,
  CTOOL_X86_MN_JO, CTOOL_X86_MN_JP, CTOOL_X86_MN_JS,
  CTOOL_X86_MN_LDMXCSR, CTOOL_X86_MN_LEA, CTOOL_X86_MN_LEAVE,
  CTOOL_X86_MN_LGDT, CTOOL_X86_MN_LIDT, CTOOL_X86_MN_LMSW,
  CTOOL_X86_MN_LTR, CTOOL_X86_MN_MAXPD, CTOOL_X86_MN_MAXPS,
  CTOOL_X86_MN_MAXSS, CTOOL_X86_MN_MINPD, CTOOL_X86_MN_MINPS,
  CTOOL_X86_MN_MINSS, CTOOL_X86_MN_MOV, CTOOL_X86_MN_MOVAPD,
  CTOOL_X86_MN_MOVAPS, CTOOL_X86_MN_MOVD, CTOOL_X86_MN_MOVDQA,
  CTOOL_X86_MN_MOVDQU, CTOOL_X86_MN_MOVMSKPS, CTOOL_X86_MN_MOVNTDQ,
  CTOOL_X86_MN_MOVSB, CTOOL_X86_MN_MOVSD, CTOOL_X86_MN_MOVSS,
  CTOOL_X86_MN_MOVSW, CTOOL_X86_MN_MOVSX,
  CTOOL_X86_MN_MOVUPD, CTOOL_X86_MN_MOVUPS, CTOOL_X86_MN_MOVZX,
  CTOOL_X86_MN_MUL, CTOOL_X86_MN_MULPD, CTOOL_X86_MN_MULPS,
  CTOOL_X86_MN_MULSD, CTOOL_X86_MN_MULSS, CTOOL_X86_MN_NEG,
  CTOOL_X86_MN_NOP, CTOOL_X86_MN_NOT, CTOOL_X86_MN_OR,
  CTOOL_X86_MN_ORPD, CTOOL_X86_MN_ORPS,
  CTOOL_X86_MN_OUT, CTOOL_X86_MN_OUTSW, CTOOL_X86_MN_PACKUSWB,
  CTOOL_X86_MN_PADDUSB, CTOOL_X86_MN_PADDW, CTOOL_X86_MN_PAUSE,
  CTOOL_X86_MN_PMULLW, CTOOL_X86_MN_POP, CTOOL_X86_MN_POPA,
  CTOOL_X86_MN_POPAD, CTOOL_X86_MN_POPF, CTOOL_X86_MN_POPFD,
  CTOOL_X86_MN_PSHUFD, CTOOL_X86_MN_PSRLW, CTOOL_X86_MN_PUNPCKHBW,
  CTOOL_X86_MN_PUNPCKLBW, CTOOL_X86_MN_PUNPCKLWD,
  CTOOL_X86_MN_PUSH, CTOOL_X86_MN_PUSHA, CTOOL_X86_MN_PUSHAD,
  CTOOL_X86_MN_PUSHF, CTOOL_X86_MN_PUSHFD, CTOOL_X86_MN_PXOR,
  CTOOL_X86_MN_RCL, CTOOL_X86_MN_RCR,
  CTOOL_X86_MN_RDRAND, CTOOL_X86_MN_RDMSR, CTOOL_X86_MN_RDTSC,
  CTOOL_X86_MN_RET, CTOOL_X86_MN_RETF, CTOOL_X86_MN_ROL,
  CTOOL_X86_MN_ROR, CTOOL_X86_MN_SAR, CTOOL_X86_MN_SBB,
  CTOOL_X86_MN_SETA, CTOOL_X86_MN_SETAE, CTOOL_X86_MN_SETB,
  CTOOL_X86_MN_SETBE, CTOOL_X86_MN_SETC, CTOOL_X86_MN_SETE,
  CTOOL_X86_MN_SETG, CTOOL_X86_MN_SETGE, CTOOL_X86_MN_SETL,
  CTOOL_X86_MN_SETLE, CTOOL_X86_MN_SETNE, CTOOL_X86_MN_SFENCE,
  CTOOL_X86_MN_SGDT, CTOOL_X86_MN_SHL, CTOOL_X86_MN_SHR,
  CTOOL_X86_MN_SHUFPD, CTOOL_X86_MN_SHUFPS, CTOOL_X86_MN_SIDT,
  CTOOL_X86_MN_SLDT,
  CTOOL_X86_MN_SMSW, CTOOL_X86_MN_SQRTPD, CTOOL_X86_MN_SQRTPS,
  CTOOL_X86_MN_SQRTSD, CTOOL_X86_MN_SQRTSS, CTOOL_X86_MN_STC,
  CTOOL_X86_MN_STD, CTOOL_X86_MN_STI, CTOOL_X86_MN_STMXCSR,
  CTOOL_X86_MN_STOSB, CTOOL_X86_MN_STOSD, CTOOL_X86_MN_STOSW,
  CTOOL_X86_MN_STR, CTOOL_X86_MN_SUB, CTOOL_X86_MN_SUBPD,
  CTOOL_X86_MN_SUBPS, CTOOL_X86_MN_SUBSD, CTOOL_X86_MN_SUBSS,
  CTOOL_X86_MN_SYSCALL, CTOOL_X86_MN_SYSENTER, CTOOL_X86_MN_SYSEXIT,
  CTOOL_X86_MN_TEST, CTOOL_X86_MN_UCOMISD, CTOOL_X86_MN_UCOMISS,
  CTOOL_X86_MN_WBINVD, CTOOL_X86_MN_WRMSR, CTOOL_X86_MN_XADD,
  CTOOL_X86_MN_XCHG, CTOOL_X86_MN_XOR, CTOOL_X86_MN_XORPD,
  CTOOL_X86_MN_XORPS,
  CTOOL_X86_MN_COUNT
} ctool_x86_mnemonic_t;

#define CTOOL_X86_PREFIX_LOCK 0x01u
#define CTOOL_X86_PREFIX_REP 0x02u
#define CTOOL_X86_PREFIX_REPNE 0x04u

typedef struct {
  ctool_x86_mnemonic_t mnemonic;
  /* Zero requests the execution mode's default.  Address width is independent
   * of operand width and is also overridden by a memory operand when present. */
  ctool_u16 operand_bits;
  ctool_u16 address_bits;
  ctool_u8 prefixes;
  ctool_u8 operand_count;
  ctool_x86_operand_t operands[CTOOL_X86_MAX_OPERANDS];
} ctool_x86_instruction_t;

typedef ctool_u32 ctool_x86_form_t;
#define CTOOL_X86_FORM_AUTO 0u

typedef enum {
  CTOOL_X86_FIELD_IMMEDIATE = 0,
  CTOOL_X86_FIELD_RELATIVE,
  CTOOL_X86_FIELD_DISPLACEMENT,
  CTOOL_X86_FIELD_FAR_OFFSET,
  CTOOL_X86_FIELD_FAR_SEGMENT
} ctool_x86_field_kind_t;

typedef enum {
  CTOOL_X86_RELOC_NONE = 0,
  CTOOL_X86_RELOC_ABSOLUTE,
  CTOOL_X86_RELOC_PC_RELATIVE
} ctool_x86_relocation_t;

typedef struct {
  ctool_x86_field_kind_t kind;
  ctool_x86_relocation_t relocation;
  ctool_u8 operand_index;
  ctool_u8 byte_offset;
  ctool_u8 byte_width;
  ctool_u8 pc_bias;
  ctool_u32 reference;
  ctool_i32 encoded_addend;
} ctool_x86_field_t;

typedef struct {
  ctool_u8 bytes[CTOOL_X86_MAX_INSTRUCTION_BYTES];
  ctool_u8 size;
  ctool_x86_form_t form;
  ctool_u8 field_count;
  ctool_x86_field_t fields[CTOOL_X86_MAX_FIELDS];
} ctool_x86_encoding_t;

typedef enum {
  /* Fully represented by the current catalogue. */
  CTOOL_X86_DECODE_KNOWN = 0,
  /* Outside the catalogue; consumes one byte to guarantee inspector progress. */
  CTOOL_X86_DECODE_UNKNOWN,
  /* A recognized path needs more bytes; consumes zero and retains all input. */
  CTOOL_X86_DECODE_TRUNCATED,
  /* A recognized family has an illegal prefix, register, or reserved form. */
  CTOOL_X86_DECODE_INVALID
} ctool_x86_decode_kind_t;

typedef struct {
  ctool_x86_decode_kind_t kind;
  ctool_x86_instruction_t instruction;
  ctool_x86_encoding_t encoding;
  ctool_u8 consumed;
} ctool_x86_decoded_t;

typedef struct {
  ctool_u32 form_count;
  ctool_u32 mnemonic_count;
  ctool_u32 register_count;
  /* Fingerprints forms, mnemonic names/aliases, and register names/aliases. */
  ctool_u32 fingerprint;
} ctool_x86_model_info_t;

typedef enum {
  CTOOL_X86_DIAG_INVALID_MODEL = 0x04000001u,
  CTOOL_X86_DIAG_INVALID_INSTRUCTION = 0x04000002u,
  CTOOL_X86_DIAG_NO_ENCODING = 0x04000003u,
  CTOOL_X86_DIAG_INVALID_REGISTER = 0x04000004u,
  CTOOL_X86_DIAG_INVALID_MEMORY = 0x04000005u,
  CTOOL_X86_DIAG_ILLEGAL_PREFIX = 0x04000006u,
  CTOOL_X86_DIAG_LIMIT = 0x04000007u
} ctool_x86_diag_code_t;

ctool_status_t ctool_x86_mnemonic_from_name(ctool_string_t name,
                                             ctool_x86_mnemonic_t *out);
ctool_string_t ctool_x86_mnemonic_name(ctool_x86_mnemonic_t mnemonic);
ctool_status_t ctool_x86_register_from_name(ctool_string_t name,
                                             ctool_x86_reg_t *out);
ctool_string_t ctool_x86_register_name(ctool_x86_reg_t reg_value);

ctool_x86_model_info_t ctool_x86_model_info(void);
ctool_status_t ctool_x86_validate_model(ctool_job_t *job);

ctool_status_t ctool_x86_encode(ctool_job_t *job, ctool_x86_mode_t mode,
                                 const ctool_x86_instruction_t *instruction,
                                 ctool_x86_form_t requested_form,
                                 ctool_x86_encoding_t *encoding_out);
ctool_status_t ctool_x86_decode(ctool_job_t *job, ctool_x86_mode_t mode,
                                 ctool_bytes_t bytes, ctool_u32 address,
                                 ctool_x86_decoded_t *decoded_out);

/* Instruction/operand inputs are borrowed for a call.  Encoded and decoded
 * values are self-contained.  Raw decode fields never claim relocation
 * ownership.  A nonzero form identity is meaningful only together with the
 * model fingerprint that produced it; zero requests the shortest deterministic
 * canonical encoding.  Repeated prefixes from one legacy prefix group are
 * classified as invalid in this model. */

#endif
