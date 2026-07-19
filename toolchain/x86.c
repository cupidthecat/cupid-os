#include "x86.h"

#define X86_MODE16_MASK 0x01u
#define X86_MODE32_MASK 0x02u
#define X86_MODE_BOTH (X86_MODE16_MASK | X86_MODE32_MASK)

#define X86_NO_OPERAND (-1)
#define X86_NO_DIGIT 8u

#define X86_FORM_LOCKABLE 0x0001u
#define X86_FORM_MEMORY_ONLY 0x0002u
#define X86_FORM_REP_ALLOWED 0x0004u
#define X86_FORM_SIGN_EXTENDED 0x0008u
#define X86_FORM_FIXED_REG0 0x0010u
#define X86_FORM_FIXED_REG1 0x0020u
#define X86_FORM_FIXED_DX0 0x0040u
#define X86_FORM_FIXED_DX1 0x0080u
#define X86_FORM_FIXED_SEG_ES 0x0100u
#define X86_FORM_FIXED_SEG_CS 0x0200u
#define X86_FORM_FIXED_SEG_SS 0x0400u
#define X86_FORM_FIXED_SEG_DS 0x0800u
#define X86_FORM_FIXED_SEG_FS 0x1000u
#define X86_FORM_FIXED_SEG_GS 0x2000u
#define X86_FORM_REGISTER_ONLY 0x4000u
#define X86_FORM_SEGMENT_DEST 0x8000u
#define X86_FORM_DECODE_ALIAS 0x00010000u
#define X86_FORM_MOFFS 0x00020000u
#define X86_FORM_FIXED_CL1 0x00040000u
#define X86_FORM_INVALID_ENCODING 0x00080000u

typedef enum {
  X86_ISA_8086 = 0,
  X86_ISA_386,
  X86_ISA_X87,
  X86_ISA_MMX,
  X86_ISA_SSE,
  X86_ISA_SSE2,
  X86_ISA_LATER
} x86_isa_t;

typedef enum {
  X86_OC_NONE = 0,
  X86_OC_GPR8,
  X86_OC_GPR16,
  X86_OC_GPR32,
  X86_OC_RM8,
  X86_OC_RM16,
  X86_OC_RM32,
  X86_OC_MEM,
  X86_OC_MEM8,
  X86_OC_MEM16,
  X86_OC_MEM32,
  X86_OC_MEM48,
  X86_OC_MEM64,
  X86_OC_MEM128,
  X86_OC_SEGMENT,
  X86_OC_CONTROL,
  X86_OC_X87,
  X86_OC_MMX,
  X86_OC_MMX_RM32,
  X86_OC_MMX_RM64,
  X86_OC_XMM,
  X86_OC_XMM_RM32,
  X86_OC_XMM_RM64,
  X86_OC_XMM_RM128,
  X86_OC_IMM8,
  X86_OC_IMM16,
  X86_OC_IMM32,
  X86_OC_REL8,
  X86_OC_REL16,
  X86_OC_REL32,
  X86_OC_FAR16_16,
  X86_OC_FAR16_32
} x86_operand_class_t;

typedef struct {
  ctool_x86_mnemonic_t mnemonic;
  ctool_u8 modes;
  ctool_u8 isa;
  ctool_u16 operand_bits;
  ctool_u8 mandatory_prefix;
  ctool_u8 opcode_length;
  ctool_u8 opcode[3];
  ctool_u8 final_opcode_mask;
  ctool_u8 operand_count;
  ctool_u8 operand_classes[CTOOL_X86_MAX_OPERANDS];
  ctool_i32 opcode_reg_operand;
  ctool_i32 modrm_reg_operand;
  ctool_i32 modrm_rm_operand;
  ctool_u8 modrm_digit;
  ctool_i32 value_operand;
  ctool_u8 value_bits;
  ctool_x86_field_kind_t value_kind;
  ctool_u32 flags;
} x86_form_row_t;

typedef struct {
  const char *name;
  ctool_u8 length;
  ctool_x86_mnemonic_t mnemonic;
  ctool_bool canonical;
} x86_mnemonic_name_t;

typedef struct {
  const char *name;
  ctool_u8 length;
  ctool_x86_reg_t reg;
  ctool_bool canonical;
} x86_register_name_t;

#define X86_MN_CANON(text, value) \
  { (text), (ctool_u8)(sizeof(text) - 1u), (value), CTOOL_TRUE }
#define X86_MN_ALIAS(text, value) \
  { (text), (ctool_u8)(sizeof(text) - 1u), (value), CTOOL_FALSE }

static const x86_mnemonic_name_t x86_mnemonic_names[] = {
  X86_MN_CANON("adc", CTOOL_X86_MN_ADC),
  X86_MN_CANON("add", CTOOL_X86_MN_ADD),
  X86_MN_CANON("addps", CTOOL_X86_MN_ADDPS),
  X86_MN_CANON("addss", CTOOL_X86_MN_ADDSS),
  X86_MN_CANON("addsd", CTOOL_X86_MN_ADDSD),
  X86_MN_CANON("addpd", CTOOL_X86_MN_ADDPD),
  X86_MN_CANON("and", CTOOL_X86_MN_AND),
  X86_MN_CANON("andps", CTOOL_X86_MN_ANDPS),
  X86_MN_CANON("andpd", CTOOL_X86_MN_ANDPD),
  X86_MN_CANON("bswap", CTOOL_X86_MN_BSWAP),
  X86_MN_CANON("call", CTOOL_X86_MN_CALL),
  X86_MN_CANON("cbw", CTOOL_X86_MN_CBW),
  X86_MN_CANON("cdq", CTOOL_X86_MN_CDQ),
  X86_MN_CANON("clc", CTOOL_X86_MN_CLC),
  X86_MN_CANON("cld", CTOOL_X86_MN_CLD),
  X86_MN_CANON("cli", CTOOL_X86_MN_CLI),
  X86_MN_CANON("clts", CTOOL_X86_MN_CLTS),
  X86_MN_CANON("cmc", CTOOL_X86_MN_CMC),
  X86_MN_CANON("cmp", CTOOL_X86_MN_CMP),
  X86_MN_CANON("cmpxchg", CTOOL_X86_MN_CMPXCHG),
  X86_MN_CANON("cmpps", CTOOL_X86_MN_CMPPS),
  X86_MN_CANON("cpuid", CTOOL_X86_MN_CPUID),
  X86_MN_CANON("cvtps2dq", CTOOL_X86_MN_CVTPS2DQ),
  X86_MN_CANON("cvtdq2ps", CTOOL_X86_MN_CVTDQ2PS),
  X86_MN_CANON("cvtsd2si", CTOOL_X86_MN_CVTSD2SI),
  X86_MN_CANON("cvtss2si", CTOOL_X86_MN_CVTSS2SI),
  X86_MN_CANON("cvtsi2sd", CTOOL_X86_MN_CVTSI2SD),
  X86_MN_CANON("cvtsi2ss", CTOOL_X86_MN_CVTSI2SS),
  X86_MN_CANON("cvtsd2ss", CTOOL_X86_MN_CVTSD2SS),
  X86_MN_CANON("cvtss2sd", CTOOL_X86_MN_CVTSS2SD),
  X86_MN_CANON("cvttsd2si", CTOOL_X86_MN_CVTTSD2SI),
  X86_MN_CANON("cvttss2si", CTOOL_X86_MN_CVTTSS2SI),
  X86_MN_CANON("cwde", CTOOL_X86_MN_CWDE),
  X86_MN_CANON("dec", CTOOL_X86_MN_DEC),
  X86_MN_CANON("div", CTOOL_X86_MN_DIV),
  X86_MN_CANON("divpd", CTOOL_X86_MN_DIVPD),
  X86_MN_CANON("divps", CTOOL_X86_MN_DIVPS),
  X86_MN_CANON("divsd", CTOOL_X86_MN_DIVSD),
  X86_MN_CANON("divss", CTOOL_X86_MN_DIVSS),
  X86_MN_CANON("f2xm1", CTOOL_X86_MN_F2XM1),
  X86_MN_CANON("fabs", CTOOL_X86_MN_FABS),
  X86_MN_CANON("fadd", CTOOL_X86_MN_FADD),
  X86_MN_CANON("faddp", CTOOL_X86_MN_FADDP),
  X86_MN_CANON("fchs", CTOOL_X86_MN_FCHS),
  X86_MN_CANON("fcos", CTOOL_X86_MN_FCOS),
  X86_MN_CANON("finit", CTOOL_X86_MN_FINIT),
  X86_MN_CANON("fdiv", CTOOL_X86_MN_FDIV),
  X86_MN_CANON("fdivp", CTOOL_X86_MN_FDIVP),
  X86_MN_CANON("fld", CTOOL_X86_MN_FLD),
  X86_MN_CANON("fld1", CTOOL_X86_MN_FLD1),
  X86_MN_CANON("fldcw", CTOOL_X86_MN_FLDCW),
  X86_MN_CANON("fmul", CTOOL_X86_MN_FMUL),
  X86_MN_CANON("fmulp", CTOOL_X86_MN_FMULP),
  X86_MN_CANON("fninit", CTOOL_X86_MN_FNINIT),
  X86_MN_CANON("fnstcw", CTOOL_X86_MN_FNSTCW),
  X86_MN_CANON("fnstsw", CTOOL_X86_MN_FNSTSW),
  X86_MN_CANON("fpatan", CTOOL_X86_MN_FPATAN),
  X86_MN_CANON("fprem", CTOOL_X86_MN_FPREM),
  X86_MN_CANON("fptan", CTOOL_X86_MN_FPTAN),
  X86_MN_CANON("frndint", CTOOL_X86_MN_FRNDINT),
  X86_MN_CANON("fscale", CTOOL_X86_MN_FSCALE),
  X86_MN_CANON("fsin", CTOOL_X86_MN_FSIN),
  X86_MN_CANON("fsqrt", CTOOL_X86_MN_FSQRT),
  X86_MN_CANON("fst", CTOOL_X86_MN_FST),
  X86_MN_CANON("fstp", CTOOL_X86_MN_FSTP),
  X86_MN_CANON("fsub", CTOOL_X86_MN_FSUB),
  X86_MN_CANON("fsubp", CTOOL_X86_MN_FSUBP),
  X86_MN_CANON("fsubr", CTOOL_X86_MN_FSUBR),
  X86_MN_CANON("fwait", CTOOL_X86_MN_FWAIT),
  X86_MN_CANON("fxch", CTOOL_X86_MN_FXCH),
  X86_MN_CANON("fxrstor", CTOOL_X86_MN_FXRSTOR),
  X86_MN_CANON("fxsave", CTOOL_X86_MN_FXSAVE),
  X86_MN_CANON("fyl2x", CTOOL_X86_MN_FYL2X),
  X86_MN_CANON("hlt", CTOOL_X86_MN_HLT),
  X86_MN_CANON("idiv", CTOOL_X86_MN_IDIV),
  X86_MN_CANON("imul", CTOOL_X86_MN_IMUL),
  X86_MN_CANON("in", CTOOL_X86_MN_IN),
  X86_MN_CANON("inc", CTOOL_X86_MN_INC),
  X86_MN_CANON("insw", CTOOL_X86_MN_INSW),
  X86_MN_CANON("int", CTOOL_X86_MN_INT),
  X86_MN_CANON("int3", CTOOL_X86_MN_INT3),
  X86_MN_CANON("invd", CTOOL_X86_MN_INVD),
  X86_MN_CANON("invlpg", CTOOL_X86_MN_INVLPG),
  X86_MN_CANON("iret", CTOOL_X86_MN_IRET),
  X86_MN_CANON("iretd", CTOOL_X86_MN_IRETD),
  X86_MN_CANON("ja", CTOOL_X86_MN_JA),
  X86_MN_CANON("jae", CTOOL_X86_MN_JAE),
  X86_MN_CANON("jb", CTOOL_X86_MN_JB),
  X86_MN_CANON("jbe", CTOOL_X86_MN_JBE),
  X86_MN_CANON("je", CTOOL_X86_MN_JE),
  X86_MN_CANON("jg", CTOOL_X86_MN_JG),
  X86_MN_CANON("jge", CTOOL_X86_MN_JGE),
  X86_MN_CANON("jl", CTOOL_X86_MN_JL),
  X86_MN_CANON("jle", CTOOL_X86_MN_JLE),
  X86_MN_CANON("jmp", CTOOL_X86_MN_JMP),
  X86_MN_CANON("jne", CTOOL_X86_MN_JNE),
  X86_MN_CANON("jno", CTOOL_X86_MN_JNO),
  X86_MN_CANON("jnp", CTOOL_X86_MN_JNP),
  X86_MN_CANON("jns", CTOOL_X86_MN_JNS),
  X86_MN_CANON("jo", CTOOL_X86_MN_JO),
  X86_MN_CANON("jp", CTOOL_X86_MN_JP),
  X86_MN_CANON("js", CTOOL_X86_MN_JS),
  X86_MN_CANON("ldmxcsr", CTOOL_X86_MN_LDMXCSR),
  X86_MN_CANON("lea", CTOOL_X86_MN_LEA),
  X86_MN_CANON("leave", CTOOL_X86_MN_LEAVE),
  X86_MN_CANON("lgdt", CTOOL_X86_MN_LGDT),
  X86_MN_CANON("lidt", CTOOL_X86_MN_LIDT),
  X86_MN_CANON("lmsw", CTOOL_X86_MN_LMSW),
  X86_MN_CANON("ltr", CTOOL_X86_MN_LTR),
  X86_MN_CANON("maxpd", CTOOL_X86_MN_MAXPD),
  X86_MN_CANON("maxps", CTOOL_X86_MN_MAXPS),
  X86_MN_CANON("maxss", CTOOL_X86_MN_MAXSS),
  X86_MN_CANON("minpd", CTOOL_X86_MN_MINPD),
  X86_MN_CANON("minps", CTOOL_X86_MN_MINPS),
  X86_MN_CANON("minss", CTOOL_X86_MN_MINSS),
  X86_MN_CANON("mov", CTOOL_X86_MN_MOV),
  X86_MN_CANON("movapd", CTOOL_X86_MN_MOVAPD),
  X86_MN_CANON("movaps", CTOOL_X86_MN_MOVAPS),
  X86_MN_CANON("movd", CTOOL_X86_MN_MOVD),
  X86_MN_CANON("movdqa", CTOOL_X86_MN_MOVDQA),
  X86_MN_CANON("movdqu", CTOOL_X86_MN_MOVDQU),
  X86_MN_CANON("movmskps", CTOOL_X86_MN_MOVMSKPS),
  X86_MN_CANON("movntdq", CTOOL_X86_MN_MOVNTDQ),
  X86_MN_CANON("movsb", CTOOL_X86_MN_MOVSB),
  X86_MN_CANON("movsd", CTOOL_X86_MN_MOVSD),
  X86_MN_CANON("movss", CTOOL_X86_MN_MOVSS),
  X86_MN_CANON("movsw", CTOOL_X86_MN_MOVSW),
  X86_MN_CANON("movsx", CTOOL_X86_MN_MOVSX),
  X86_MN_CANON("movupd", CTOOL_X86_MN_MOVUPD),
  X86_MN_CANON("movups", CTOOL_X86_MN_MOVUPS),
  X86_MN_CANON("movzx", CTOOL_X86_MN_MOVZX),
  X86_MN_CANON("mul", CTOOL_X86_MN_MUL),
  X86_MN_CANON("mulpd", CTOOL_X86_MN_MULPD),
  X86_MN_CANON("mulps", CTOOL_X86_MN_MULPS),
  X86_MN_CANON("mulsd", CTOOL_X86_MN_MULSD),
  X86_MN_CANON("mulss", CTOOL_X86_MN_MULSS),
  X86_MN_CANON("neg", CTOOL_X86_MN_NEG),
  X86_MN_CANON("nop", CTOOL_X86_MN_NOP),
  X86_MN_CANON("not", CTOOL_X86_MN_NOT),
  X86_MN_CANON("or", CTOOL_X86_MN_OR),
  X86_MN_CANON("orpd", CTOOL_X86_MN_ORPD),
  X86_MN_CANON("orps", CTOOL_X86_MN_ORPS),
  X86_MN_CANON("out", CTOOL_X86_MN_OUT),
  X86_MN_CANON("outsw", CTOOL_X86_MN_OUTSW),
  X86_MN_CANON("packuswb", CTOOL_X86_MN_PACKUSWB),
  X86_MN_CANON("paddusb", CTOOL_X86_MN_PADDUSB),
  X86_MN_CANON("paddw", CTOOL_X86_MN_PADDW),
  X86_MN_CANON("pause", CTOOL_X86_MN_PAUSE),
  X86_MN_CANON("pmullw", CTOOL_X86_MN_PMULLW),
  X86_MN_CANON("pop", CTOOL_X86_MN_POP),
  X86_MN_CANON("popa", CTOOL_X86_MN_POPA),
  X86_MN_CANON("popad", CTOOL_X86_MN_POPAD),
  X86_MN_CANON("popf", CTOOL_X86_MN_POPF),
  X86_MN_CANON("popfd", CTOOL_X86_MN_POPFD),
  X86_MN_CANON("pshufd", CTOOL_X86_MN_PSHUFD),
  X86_MN_CANON("psrlw", CTOOL_X86_MN_PSRLW),
  X86_MN_CANON("punpckhbw", CTOOL_X86_MN_PUNPCKHBW),
  X86_MN_CANON("punpcklbw", CTOOL_X86_MN_PUNPCKLBW),
  X86_MN_CANON("punpcklwd", CTOOL_X86_MN_PUNPCKLWD),
  X86_MN_CANON("push", CTOOL_X86_MN_PUSH),
  X86_MN_CANON("pusha", CTOOL_X86_MN_PUSHA),
  X86_MN_CANON("pushad", CTOOL_X86_MN_PUSHAD),
  X86_MN_CANON("pushf", CTOOL_X86_MN_PUSHF),
  X86_MN_CANON("pushfd", CTOOL_X86_MN_PUSHFD),
  X86_MN_CANON("pxor", CTOOL_X86_MN_PXOR),
  X86_MN_CANON("rcl", CTOOL_X86_MN_RCL),
  X86_MN_CANON("rcr", CTOOL_X86_MN_RCR),
  X86_MN_CANON("rdrand", CTOOL_X86_MN_RDRAND),
  X86_MN_CANON("rdmsr", CTOOL_X86_MN_RDMSR),
  X86_MN_CANON("rdtsc", CTOOL_X86_MN_RDTSC),
  X86_MN_CANON("ret", CTOOL_X86_MN_RET),
  X86_MN_CANON("retf", CTOOL_X86_MN_RETF),
  X86_MN_CANON("rol", CTOOL_X86_MN_ROL),
  X86_MN_CANON("ror", CTOOL_X86_MN_ROR),
  X86_MN_CANON("sar", CTOOL_X86_MN_SAR),
  X86_MN_CANON("sbb", CTOOL_X86_MN_SBB),
  X86_MN_CANON("seta", CTOOL_X86_MN_SETA),
  X86_MN_CANON("setae", CTOOL_X86_MN_SETAE),
  X86_MN_CANON("setb", CTOOL_X86_MN_SETB),
  X86_MN_CANON("setbe", CTOOL_X86_MN_SETBE),
  X86_MN_CANON("setc", CTOOL_X86_MN_SETC),
  X86_MN_CANON("sete", CTOOL_X86_MN_SETE),
  X86_MN_CANON("setg", CTOOL_X86_MN_SETG),
  X86_MN_CANON("setge", CTOOL_X86_MN_SETGE),
  X86_MN_CANON("setl", CTOOL_X86_MN_SETL),
  X86_MN_CANON("setle", CTOOL_X86_MN_SETLE),
  X86_MN_CANON("setne", CTOOL_X86_MN_SETNE),
  X86_MN_CANON("sfence", CTOOL_X86_MN_SFENCE),
  X86_MN_CANON("sgdt", CTOOL_X86_MN_SGDT),
  X86_MN_CANON("shl", CTOOL_X86_MN_SHL),
  X86_MN_CANON("shr", CTOOL_X86_MN_SHR),
  X86_MN_CANON("shufpd", CTOOL_X86_MN_SHUFPD),
  X86_MN_CANON("shufps", CTOOL_X86_MN_SHUFPS),
  X86_MN_CANON("sidt", CTOOL_X86_MN_SIDT),
  X86_MN_CANON("sldt", CTOOL_X86_MN_SLDT),
  X86_MN_CANON("smsw", CTOOL_X86_MN_SMSW),
  X86_MN_CANON("sqrtpd", CTOOL_X86_MN_SQRTPD),
  X86_MN_CANON("sqrtps", CTOOL_X86_MN_SQRTPS),
  X86_MN_CANON("sqrtsd", CTOOL_X86_MN_SQRTSD),
  X86_MN_CANON("sqrtss", CTOOL_X86_MN_SQRTSS),
  X86_MN_CANON("stc", CTOOL_X86_MN_STC),
  X86_MN_CANON("std", CTOOL_X86_MN_STD),
  X86_MN_CANON("sti", CTOOL_X86_MN_STI),
  X86_MN_CANON("stmxcsr", CTOOL_X86_MN_STMXCSR),
  X86_MN_CANON("stosb", CTOOL_X86_MN_STOSB),
  X86_MN_CANON("stosd", CTOOL_X86_MN_STOSD),
  X86_MN_CANON("stosw", CTOOL_X86_MN_STOSW),
  X86_MN_CANON("str", CTOOL_X86_MN_STR),
  X86_MN_CANON("sub", CTOOL_X86_MN_SUB),
  X86_MN_CANON("subpd", CTOOL_X86_MN_SUBPD),
  X86_MN_CANON("subps", CTOOL_X86_MN_SUBPS),
  X86_MN_CANON("subsd", CTOOL_X86_MN_SUBSD),
  X86_MN_CANON("subss", CTOOL_X86_MN_SUBSS),
  X86_MN_CANON("syscall", CTOOL_X86_MN_SYSCALL),
  X86_MN_CANON("sysenter", CTOOL_X86_MN_SYSENTER),
  X86_MN_CANON("sysexit", CTOOL_X86_MN_SYSEXIT),
  X86_MN_CANON("test", CTOOL_X86_MN_TEST),
  X86_MN_CANON("ucomisd", CTOOL_X86_MN_UCOMISD),
  X86_MN_CANON("ucomiss", CTOOL_X86_MN_UCOMISS),
  X86_MN_CANON("wbinvd", CTOOL_X86_MN_WBINVD),
  X86_MN_CANON("wrmsr", CTOOL_X86_MN_WRMSR),
  X86_MN_CANON("xadd", CTOOL_X86_MN_XADD),
  X86_MN_CANON("xchg", CTOOL_X86_MN_XCHG),
  X86_MN_CANON("xor", CTOOL_X86_MN_XOR),
  X86_MN_CANON("xorpd", CTOOL_X86_MN_XORPD),
  X86_MN_CANON("xorps", CTOOL_X86_MN_XORPS),
  X86_MN_ALIAS("jc", CTOOL_X86_MN_JB),
  X86_MN_ALIAS("jnae", CTOOL_X86_MN_JB),
  X86_MN_ALIAS("jnc", CTOOL_X86_MN_JAE),
  X86_MN_ALIAS("jnb", CTOOL_X86_MN_JAE),
  X86_MN_ALIAS("jz", CTOOL_X86_MN_JE),
  X86_MN_ALIAS("jnz", CTOOL_X86_MN_JNE),
  X86_MN_ALIAS("jna", CTOOL_X86_MN_JBE),
  X86_MN_ALIAS("jnbe", CTOOL_X86_MN_JA),
  X86_MN_ALIAS("jnge", CTOOL_X86_MN_JL),
  X86_MN_ALIAS("jnl", CTOOL_X86_MN_JGE),
  X86_MN_ALIAS("jng", CTOOL_X86_MN_JLE),
  X86_MN_ALIAS("jnle", CTOOL_X86_MN_JG),
  X86_MN_ALIAS("jpe", CTOOL_X86_MN_JP),
  X86_MN_ALIAS("jpo", CTOOL_X86_MN_JNP),
  X86_MN_ALIAS("sal", CTOOL_X86_MN_SHL),
  X86_MN_ALIAS("fstcw", CTOOL_X86_MN_FNSTCW),
  X86_MN_ALIAS("fstsw", CTOOL_X86_MN_FNSTSW),
  X86_MN_ALIAS("retn", CTOOL_X86_MN_RET)
};

#undef X86_MN_CANON
#undef X86_MN_ALIAS

#define X86_REG_CANON(text, cls, number) \
  { (text), (ctool_u8)(sizeof(text) - 1u), {(cls), (number)}, CTOOL_TRUE }
#define X86_REG_ALIAS(text, cls, number) \
  { (text), (ctool_u8)(sizeof(text) - 1u), {(cls), (number)}, CTOOL_FALSE }

static const x86_register_name_t x86_register_names[] = {
  X86_REG_CANON("al", CTOOL_X86_REG_GPR8, 0u),
  X86_REG_CANON("cl", CTOOL_X86_REG_GPR8, 1u),
  X86_REG_CANON("dl", CTOOL_X86_REG_GPR8, 2u),
  X86_REG_CANON("bl", CTOOL_X86_REG_GPR8, 3u),
  X86_REG_CANON("ah", CTOOL_X86_REG_GPR8, 4u),
  X86_REG_CANON("ch", CTOOL_X86_REG_GPR8, 5u),
  X86_REG_CANON("dh", CTOOL_X86_REG_GPR8, 6u),
  X86_REG_CANON("bh", CTOOL_X86_REG_GPR8, 7u),
  X86_REG_CANON("ax", CTOOL_X86_REG_GPR16, 0u),
  X86_REG_CANON("cx", CTOOL_X86_REG_GPR16, 1u),
  X86_REG_CANON("dx", CTOOL_X86_REG_GPR16, 2u),
  X86_REG_CANON("bx", CTOOL_X86_REG_GPR16, 3u),
  X86_REG_CANON("sp", CTOOL_X86_REG_GPR16, 4u),
  X86_REG_CANON("bp", CTOOL_X86_REG_GPR16, 5u),
  X86_REG_CANON("si", CTOOL_X86_REG_GPR16, 6u),
  X86_REG_CANON("di", CTOOL_X86_REG_GPR16, 7u),
  X86_REG_CANON("eax", CTOOL_X86_REG_GPR32, 0u),
  X86_REG_CANON("ecx", CTOOL_X86_REG_GPR32, 1u),
  X86_REG_CANON("edx", CTOOL_X86_REG_GPR32, 2u),
  X86_REG_CANON("ebx", CTOOL_X86_REG_GPR32, 3u),
  X86_REG_CANON("esp", CTOOL_X86_REG_GPR32, 4u),
  X86_REG_CANON("ebp", CTOOL_X86_REG_GPR32, 5u),
  X86_REG_CANON("esi", CTOOL_X86_REG_GPR32, 6u),
  X86_REG_CANON("edi", CTOOL_X86_REG_GPR32, 7u),
  X86_REG_CANON("es", CTOOL_X86_REG_SEGMENT, 0u),
  X86_REG_CANON("cs", CTOOL_X86_REG_SEGMENT, 1u),
  X86_REG_CANON("ss", CTOOL_X86_REG_SEGMENT, 2u),
  X86_REG_CANON("ds", CTOOL_X86_REG_SEGMENT, 3u),
  X86_REG_CANON("fs", CTOOL_X86_REG_SEGMENT, 4u),
  X86_REG_CANON("gs", CTOOL_X86_REG_SEGMENT, 5u),
  X86_REG_CANON("cr0", CTOOL_X86_REG_CONTROL, 0u),
  X86_REG_CANON("cr1", CTOOL_X86_REG_CONTROL, 1u),
  X86_REG_CANON("cr2", CTOOL_X86_REG_CONTROL, 2u),
  X86_REG_CANON("cr3", CTOOL_X86_REG_CONTROL, 3u),
  X86_REG_CANON("cr4", CTOOL_X86_REG_CONTROL, 4u),
  X86_REG_CANON("cr5", CTOOL_X86_REG_CONTROL, 5u),
  X86_REG_CANON("cr6", CTOOL_X86_REG_CONTROL, 6u),
  X86_REG_CANON("cr7", CTOOL_X86_REG_CONTROL, 7u),
  X86_REG_CANON("st0", CTOOL_X86_REG_X87, 0u),
  X86_REG_CANON("st1", CTOOL_X86_REG_X87, 1u),
  X86_REG_CANON("st2", CTOOL_X86_REG_X87, 2u),
  X86_REG_CANON("st3", CTOOL_X86_REG_X87, 3u),
  X86_REG_CANON("st4", CTOOL_X86_REG_X87, 4u),
  X86_REG_CANON("st5", CTOOL_X86_REG_X87, 5u),
  X86_REG_CANON("st6", CTOOL_X86_REG_X87, 6u),
  X86_REG_CANON("st7", CTOOL_X86_REG_X87, 7u),
  X86_REG_CANON("mm0", CTOOL_X86_REG_MMX, 0u),
  X86_REG_CANON("mm1", CTOOL_X86_REG_MMX, 1u),
  X86_REG_CANON("mm2", CTOOL_X86_REG_MMX, 2u),
  X86_REG_CANON("mm3", CTOOL_X86_REG_MMX, 3u),
  X86_REG_CANON("mm4", CTOOL_X86_REG_MMX, 4u),
  X86_REG_CANON("mm5", CTOOL_X86_REG_MMX, 5u),
  X86_REG_CANON("mm6", CTOOL_X86_REG_MMX, 6u),
  X86_REG_CANON("mm7", CTOOL_X86_REG_MMX, 7u),
  X86_REG_CANON("xmm0", CTOOL_X86_REG_XMM, 0u),
  X86_REG_CANON("xmm1", CTOOL_X86_REG_XMM, 1u),
  X86_REG_CANON("xmm2", CTOOL_X86_REG_XMM, 2u),
  X86_REG_CANON("xmm3", CTOOL_X86_REG_XMM, 3u),
  X86_REG_CANON("xmm4", CTOOL_X86_REG_XMM, 4u),
  X86_REG_CANON("xmm5", CTOOL_X86_REG_XMM, 5u),
  X86_REG_CANON("xmm6", CTOOL_X86_REG_XMM, 6u),
  X86_REG_CANON("xmm7", CTOOL_X86_REG_XMM, 7u),
  X86_REG_ALIAS("st", CTOOL_X86_REG_X87, 0u),
  X86_REG_CANON("eflags", CTOOL_X86_REG_FLAGS, 0u),
  X86_REG_ALIAS("flags", CTOOL_X86_REG_FLAGS, 0u),
  X86_REG_CANON("eip", CTOOL_X86_REG_INSTRUCTION_POINTER, 0u),
  X86_REG_ALIAS("ip", CTOOL_X86_REG_INSTRUCTION_POINTER, 0u)
};

#undef X86_REG_CANON
#undef X86_REG_ALIAS

#define X86_FORM(mn, modes_value, isa_value, bits_value, prefix_value,       \
                 length_value, op0, op1, op2, mask_value, count_value,       \
                 class0, class1, class2, opcode_reg, modrm_reg, modrm_rm,    \
                 digit_value, value_index, serialized_bits, field_value,     \
                 flag_value)                                                 \
  {                                                                           \
    (mn), (modes_value), (isa_value), (bits_value), (prefix_value),           \
        (length_value), {(op0), (op1), (op2)}, (mask_value), (count_value),   \
        {(class0), (class1), (class2)}, (opcode_reg), (modrm_reg),            \
        (modrm_rm), (digit_value), (value_index), (serialized_bits),          \
        (field_value), (flag_value)                                           \
  }

#define X86_FIXED(mn, isa_value, length_value, op0, op1, op2)                \
  X86_FORM((mn), X86_MODE_BOTH, (isa_value), 0u, 0u, (length_value), (op0),  \
           (op1), (op2), 0xffu, 0u, X86_OC_NONE, X86_OC_NONE, X86_OC_NONE,  \
           X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_DIGIT,    \
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u)

#define X86_REL(mn, bits_value, op0, op1, length_value)                       \
  X86_FORM((mn), X86_MODE_BOTH, X86_ISA_386,                                 \
           ((bits_value) == 8u ? 0u : (bits_value)), 0u,                     \
           (length_value), (op0), (op1), 0u, 0xffu, 1u,                      \
           ((bits_value) == 8u ? X86_OC_REL8                                 \
                               : ((bits_value) == 16u ? X86_OC_REL16          \
                                                      : X86_OC_REL32)),       \
           X86_OC_NONE, X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND,        \
           X86_NO_OPERAND, X86_NO_DIGIT, 0, (bits_value),                    \
           CTOOL_X86_FIELD_RELATIVE, 0u)

#define X86_JCC(mn, condition)                                                \
  X86_REL((mn), 8u, (ctool_u8)(0x70u + (condition)), 0u, 1u),                \
  X86_REL((mn), 16u, 0x0fu, (ctool_u8)(0x80u + (condition)), 2u),            \
  X86_REL((mn), 32u, 0x0fu, (ctool_u8)(0x80u + (condition)), 2u)

#define X86_RM_REG(mn, bits_value, opcode_value, lock_flags)                  \
  X86_FORM((mn), X86_MODE_BOTH, X86_ISA_386, (bits_value), 0u, 1u,           \
           (opcode_value), 0u, 0u, 0xffu, 2u,                                \
           ((bits_value) == 8u                                                \
                ? X86_OC_RM8                                                  \
                : ((bits_value) == 16u ? X86_OC_RM16 : X86_OC_RM32)),        \
           ((bits_value) == 8u                                                \
                ? X86_OC_GPR8                                                 \
                : ((bits_value) == 16u ? X86_OC_GPR16 : X86_OC_GPR32)),      \
           X86_OC_NONE, X86_NO_OPERAND, 1, 0, X86_NO_DIGIT,                  \
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, (lock_flags))

#define X86_REG_RM(mn, bits_value, opcode_value)                              \
  X86_FORM((mn), X86_MODE_BOTH, X86_ISA_386, (bits_value), 0u, 1u,           \
           (opcode_value), 0u, 0u, 0xffu, 2u,                                \
           ((bits_value) == 8u                                                \
                ? X86_OC_GPR8                                                 \
                : ((bits_value) == 16u ? X86_OC_GPR16 : X86_OC_GPR32)),      \
           ((bits_value) == 8u                                                \
                ? X86_OC_RM8                                                  \
                : ((bits_value) == 16u ? X86_OC_RM16 : X86_OC_RM32)),        \
           X86_OC_NONE, X86_NO_OPERAND, 0, 1, X86_NO_DIGIT,                  \
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u)

#define X86_RM_IMM(mn, bits_value, encoded_bits, opcode_value, digit_value,   \
                   extra_flags)                                               \
  X86_FORM((mn), X86_MODE_BOTH, X86_ISA_386, (bits_value), 0u, 1u,           \
           (opcode_value), 0u, 0u, 0xffu, 2u,                                \
           ((bits_value) == 8u                                                \
                ? X86_OC_RM8                                                  \
                : ((bits_value) == 16u ? X86_OC_RM16 : X86_OC_RM32)),        \
           ((encoded_bits) == 8u                                              \
                ? X86_OC_IMM8                                                 \
                : ((encoded_bits) == 16u ? X86_OC_IMM16 : X86_OC_IMM32)),    \
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, 0, (digit_value), 1, \
           (encoded_bits), CTOOL_X86_FIELD_IMMEDIATE, (extra_flags))

#define X86_ALU(mn, base_opcode, digit_value, lock_flags)                     \
  X86_RM_REG((mn), 8u, (base_opcode), (lock_flags)),                          \
  X86_RM_REG((mn), 16u, (ctool_u8)((base_opcode) + 1u), (lock_flags)),        \
  X86_RM_REG((mn), 32u, (ctool_u8)((base_opcode) + 1u), (lock_flags)),        \
  X86_REG_RM((mn), 8u, (ctool_u8)((base_opcode) + 2u)),                       \
  X86_REG_RM((mn), 16u, (ctool_u8)((base_opcode) + 3u)),                      \
  X86_REG_RM((mn), 32u, (ctool_u8)((base_opcode) + 3u)),                      \
  X86_RM_IMM((mn), 8u, 8u, 0x80u, (digit_value), (lock_flags)),              \
  X86_RM_IMM((mn), 16u, 16u, 0x81u, (digit_value), (lock_flags)),            \
  X86_RM_IMM((mn), 32u, 32u, 0x81u, (digit_value), (lock_flags)),            \
  X86_RM_IMM((mn), 16u, 8u, 0x83u, (digit_value),                            \
             (ctool_u16)((lock_flags) | X86_FORM_SIGN_EXTENDED)),            \
  X86_RM_IMM((mn), 32u, 8u, 0x83u, (digit_value),                            \
             (ctool_u16)((lock_flags) | X86_FORM_SIGN_EXTENDED))

#define X86_ACC_IMM(mn, bits_value, opcode_value)                             \
  X86_FORM((mn), X86_MODE_BOTH, X86_ISA_386, (bits_value), 0u, 1u,           \
           (opcode_value), 0u, 0u, 0xffu, 2u,                                \
           ((bits_value) == 8u                                                \
                ? X86_OC_GPR8                                                 \
                : ((bits_value) == 16u ? X86_OC_GPR16 : X86_OC_GPR32)),      \
           ((bits_value) == 8u                                                \
                ? X86_OC_IMM8                                                 \
                : ((bits_value) == 16u ? X86_OC_IMM16 : X86_OC_IMM32)),      \
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND,      \
           X86_NO_DIGIT, 1, (bits_value), CTOOL_X86_FIELD_IMMEDIATE,         \
           X86_FORM_FIXED_REG0)

#define X86_SSE_RM(mn, isa_value, prefix_value, opcode_value, rm_class)       \
  X86_FORM((mn), X86_MODE_BOTH, (isa_value), 0u, (prefix_value), 2u, 0x0fu,  \
           (opcode_value), 0u, 0xffu, 2u, X86_OC_XMM, (rm_class),            \
           X86_OC_NONE, X86_NO_OPERAND, 0, 1, X86_NO_DIGIT,                  \
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u)

#define X86_SSE_STORE(mn, isa_value, prefix_value, opcode_value, rm_class)    \
  X86_FORM((mn), X86_MODE_BOTH, (isa_value), 0u, (prefix_value), 2u, 0x0fu,  \
           (opcode_value), 0u, 0xffu, 2u, (rm_class), X86_OC_XMM,            \
           X86_OC_NONE, X86_NO_OPERAND, 1, 0, X86_NO_DIGIT,                  \
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u)

#define X86_GROUP_RM(mn, bits_value, opcode_value, digit_value, extra_flags) \
  X86_FORM((mn), X86_MODE_BOTH, X86_ISA_386, (bits_value), 0u, 1u,           \
           (opcode_value), 0u, 0u, 0xffu, 1u,                                \
           ((bits_value) == 8u                                                \
                ? X86_OC_RM8                                                  \
                : ((bits_value) == 16u ? X86_OC_RM16 : X86_OC_RM32)),        \
           X86_OC_NONE, X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, 0,     \
           (digit_value), X86_NO_OPERAND, 0u,                                \
           CTOOL_X86_FIELD_IMMEDIATE, (extra_flags))

#define X86_SHIFT(mn, digit_value)                                            \
  X86_GROUP_RM((mn), 8u, 0xd0u, (digit_value), 0u),                           \
  X86_GROUP_RM((mn), 16u, 0xd1u, (digit_value), 0u),                          \
  X86_GROUP_RM((mn), 32u, 0xd1u, (digit_value), 0u),                          \
  X86_FORM((mn), X86_MODE_BOTH, X86_ISA_386, 8u, 0u, 1u, 0xd2u, 0u, 0u,    \
           0xffu, 2u, X86_OC_RM8, X86_OC_GPR8, X86_OC_NONE,                 \
           X86_NO_OPERAND, X86_NO_OPERAND, 0, (digit_value),                \
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,                   \
           X86_FORM_FIXED_CL1),                                               \
  X86_FORM((mn), X86_MODE_BOTH, X86_ISA_386, 16u, 0u, 1u, 0xd3u, 0u, 0u,   \
           0xffu, 2u, X86_OC_RM16, X86_OC_GPR8, X86_OC_NONE,                \
           X86_NO_OPERAND, X86_NO_OPERAND, 0, (digit_value),                \
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,                   \
           X86_FORM_FIXED_CL1),                                               \
  X86_FORM((mn), X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 1u, 0xd3u, 0u, 0u,   \
           0xffu, 2u, X86_OC_RM32, X86_OC_GPR8, X86_OC_NONE,                \
           X86_NO_OPERAND, X86_NO_OPERAND, 0, (digit_value),                \
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,                   \
           X86_FORM_FIXED_CL1),                                               \
  X86_RM_IMM((mn), 8u, 8u, 0xc0u, (digit_value), 0u),                        \
  X86_RM_IMM((mn), 16u, 8u, 0xc1u, (digit_value), 0u),                       \
  X86_RM_IMM((mn), 32u, 8u, 0xc1u, (digit_value), 0u)

#define X86_SETCC(mn, condition, extra_flags)                                \
  X86_FORM((mn), X86_MODE_BOTH, X86_ISA_386, 8u, 0u, 2u, 0x0fu,            \
           (ctool_u8)(0x90u + (condition)), 0u, 0xffu, 1u, X86_OC_RM8,      \
           X86_OC_NONE, X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, 0,    \
           0u, X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,              \
           (extra_flags))

#define X86_SSE_XMM_GPR(mn, isa_value, prefix_value, opcode_value)           \
  X86_FORM((mn), X86_MODE_BOTH, (isa_value), 0u, (prefix_value), 2u,         \
           0x0fu, (opcode_value), 0u, 0xffu, 2u, X86_OC_XMM, X86_OC_RM32,  \
           X86_OC_NONE, X86_NO_OPERAND, 0, 1, X86_NO_DIGIT,                \
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u)

#define X86_SSE_GPR_XMM(mn, isa_value, prefix_value, opcode_value, rm_class) \
  X86_FORM((mn), X86_MODE_BOTH, (isa_value), 0u, (prefix_value), 2u,         \
           0x0fu, (opcode_value), 0u, 0xffu, 2u, X86_OC_GPR32, (rm_class),  \
           X86_OC_NONE, X86_NO_OPERAND, 0, 1, X86_NO_DIGIT,                \
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u)

#define X86_SSE_IMM(mn, isa_value, prefix_value, opcode_value)               \
  X86_FORM((mn), X86_MODE_BOTH, (isa_value), 0u, (prefix_value), 2u,         \
           0x0fu, (opcode_value), 0u, 0xffu, 3u, X86_OC_XMM,                \
           X86_OC_XMM_RM128, X86_OC_IMM8, X86_NO_OPERAND, 0, 1,            \
           X86_NO_DIGIT, 2, 8u, CTOOL_X86_FIELD_IMMEDIATE, 0u)

#define X86_X87_MEM(mn, opcode_value, digit_value, memory_class)             \
  X86_FORM((mn), X86_MODE_BOTH, X86_ISA_X87, 0u, 0u, 1u, (opcode_value),    \
           0u, 0u, 0xffu, 1u, (memory_class), X86_OC_NONE, X86_OC_NONE,    \
           X86_NO_OPERAND, X86_NO_OPERAND, 0, (digit_value),               \
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,                  \
           X86_FORM_MEMORY_ONLY)

#define X86_X87_ST(mn, opcode0, opcode1, extra_flags)                        \
  X86_FORM((mn), X86_MODE_BOTH, X86_ISA_X87, 0u, 0u, 2u, (opcode0),         \
           (opcode1), 0u, 0xf8u, 1u, X86_OC_X87, X86_OC_NONE,              \
           X86_OC_NONE, 0, X86_NO_OPERAND, X86_NO_OPERAND,                 \
           X86_NO_DIGIT, X86_NO_OPERAND, 0u,                               \
           CTOOL_X86_FIELD_IMMEDIATE, (extra_flags))

#define X86_STRING(mn, bits_value, opcode_value)                             \
  X86_FORM((mn), X86_MODE_BOTH, X86_ISA_386, (bits_value), 0u, 1u,          \
           (opcode_value), 0u, 0u, 0xffu, 0u, X86_OC_NONE, X86_OC_NONE,    \
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND,                    \
           X86_NO_OPERAND, X86_NO_DIGIT, X86_NO_OPERAND, 0u,               \
           CTOOL_X86_FIELD_IMMEDIATE, X86_FORM_REP_ALLOWED)

#define X86_INVALID_FIXED(length_value, op0, op1, op2)                       \
  X86_FORM(CTOOL_X86_MN_INVALID, X86_MODE_BOTH, X86_ISA_LATER, 0u, 0u,     \
           (length_value), (op0), (op1), (op2), 0xffu, 0u, X86_OC_NONE,    \
           X86_OC_NONE, X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND,       \
           X86_NO_OPERAND, X86_NO_DIGIT, X86_NO_OPERAND, 0u,               \
           CTOOL_X86_FIELD_IMMEDIATE, X86_FORM_INVALID_ENCODING)

#define X86_INVALID_GROUP(op0, op1, digit_value)                             \
  X86_FORM(CTOOL_X86_MN_INVALID, X86_MODE_BOTH, X86_ISA_386, 0u, 0u, 2u,  \
           (op0), (op1), 0u, 0xffu, 1u, X86_OC_RM16, X86_OC_NONE,          \
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, 0, (digit_value),  \
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,                  \
           X86_FORM_INVALID_ENCODING)

static const x86_form_row_t x86_forms[] = {
  X86_FIXED(CTOOL_X86_MN_CLC, X86_ISA_8086, 1u, 0xf8u, 0u, 0u),
  X86_FIXED(CTOOL_X86_MN_CLD, X86_ISA_8086, 1u, 0xfcu, 0u, 0u),
  X86_FIXED(CTOOL_X86_MN_CLI, X86_ISA_8086, 1u, 0xfau, 0u, 0u),
  X86_FIXED(CTOOL_X86_MN_HLT, X86_ISA_8086, 1u, 0xf4u, 0u, 0u),
  X86_FIXED(CTOOL_X86_MN_INT3, X86_ISA_8086, 1u, 0xccu, 0u, 0u),
  X86_FIXED(CTOOL_X86_MN_IRET, X86_ISA_8086, 1u, 0xcfu, 0u, 0u),
  X86_FORM(CTOOL_X86_MN_IRETD, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 1u,
           0xcfu, 0u, 0u, 0xffu, 0u, X86_OC_NONE, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND,
           X86_NO_DIGIT, X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_DECODE_ALIAS),
  X86_FIXED(CTOOL_X86_MN_LEAVE, X86_ISA_386, 1u, 0xc9u, 0u, 0u),
  X86_FIXED(CTOOL_X86_MN_NOP, X86_ISA_8086, 1u, 0x90u, 0u, 0u),
  X86_FIXED(CTOOL_X86_MN_POPA, X86_ISA_8086, 1u, 0x61u, 0u, 0u),
  X86_FORM(CTOOL_X86_MN_POPAD, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 1u,
           0x61u, 0u, 0u, 0xffu, 0u, X86_OC_NONE, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND,
           X86_NO_DIGIT, X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_DECODE_ALIAS),
  X86_FIXED(CTOOL_X86_MN_POPF, X86_ISA_8086, 1u, 0x9du, 0u, 0u),
  X86_FORM(CTOOL_X86_MN_POPFD, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 1u,
           0x9du, 0u, 0u, 0xffu, 0u, X86_OC_NONE, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND,
           X86_NO_DIGIT, X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_DECODE_ALIAS),
  X86_FIXED(CTOOL_X86_MN_PUSHA, X86_ISA_8086, 1u, 0x60u, 0u, 0u),
  X86_FORM(CTOOL_X86_MN_PUSHAD, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 1u,
           0x60u, 0u, 0u, 0xffu, 0u, X86_OC_NONE, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND,
           X86_NO_DIGIT, X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_DECODE_ALIAS),
  X86_FIXED(CTOOL_X86_MN_PUSHF, X86_ISA_8086, 1u, 0x9cu, 0u, 0u),
  X86_FORM(CTOOL_X86_MN_PUSHFD, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 1u,
           0x9cu, 0u, 0u, 0xffu, 0u, X86_OC_NONE, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND,
           X86_NO_DIGIT, X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_DECODE_ALIAS),
  X86_FIXED(CTOOL_X86_MN_RET, X86_ISA_8086, 1u, 0xc3u, 0u, 0u),
  X86_FORM(CTOOL_X86_MN_RET, X86_MODE_BOTH, X86_ISA_8086, 0u, 0u, 1u,
           0xc2u, 0u, 0u, 0xffu, 1u, X86_OC_IMM16, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND,
           X86_NO_DIGIT, 0, 16u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FIXED(CTOOL_X86_MN_RETF, X86_ISA_8086, 1u, 0xcbu, 0u, 0u),
  X86_FIXED(CTOOL_X86_MN_STC, X86_ISA_8086, 1u, 0xf9u, 0u, 0u),
  X86_FIXED(CTOOL_X86_MN_STD, X86_ISA_8086, 1u, 0xfdu, 0u, 0u),
  X86_FIXED(CTOOL_X86_MN_STI, X86_ISA_8086, 1u, 0xfbu, 0u, 0u),
  X86_FIXED(CTOOL_X86_MN_CMC, X86_ISA_8086, 1u, 0xf5u, 0u, 0u),
  X86_FORM(CTOOL_X86_MN_CBW, X86_MODE_BOTH, X86_ISA_8086, 16u, 0u, 1u,
           0x98u, 0u, 0u, 0xffu, 0u, X86_OC_NONE, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND,
           X86_NO_DIGIT, X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FORM(CTOOL_X86_MN_CWDE, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 1u,
           0x98u, 0u, 0u, 0xffu, 0u, X86_OC_NONE, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND,
           X86_NO_DIGIT, X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FORM(CTOOL_X86_MN_CDQ, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 1u,
           0x99u, 0u, 0u, 0xffu, 0u, X86_OC_NONE, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND,
           X86_NO_DIGIT, X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FIXED(CTOOL_X86_MN_CLTS, X86_ISA_386, 2u, 0x0fu, 0x06u, 0u),
  X86_FIXED(CTOOL_X86_MN_CPUID, X86_ISA_LATER, 2u, 0x0fu, 0xa2u, 0u),
  X86_FIXED(CTOOL_X86_MN_INVD, X86_ISA_386, 2u, 0x0fu, 0x08u, 0u),
  X86_FIXED(CTOOL_X86_MN_RDMSR, X86_ISA_LATER, 2u, 0x0fu, 0x32u, 0u),
  X86_FIXED(CTOOL_X86_MN_RDTSC, X86_ISA_LATER, 2u, 0x0fu, 0x31u, 0u),
  X86_FIXED(CTOOL_X86_MN_SYSCALL, X86_ISA_LATER, 2u, 0x0fu, 0x05u, 0u),
  X86_FIXED(CTOOL_X86_MN_SYSENTER, X86_ISA_LATER, 2u, 0x0fu, 0x34u, 0u),
  X86_FIXED(CTOOL_X86_MN_SYSEXIT, X86_ISA_LATER, 2u, 0x0fu, 0x35u, 0u),
  X86_FIXED(CTOOL_X86_MN_WBINVD, X86_ISA_386, 2u, 0x0fu, 0x09u, 0u),
  X86_FIXED(CTOOL_X86_MN_WRMSR, X86_ISA_LATER, 2u, 0x0fu, 0x30u, 0u),
  X86_FORM(CTOOL_X86_MN_PAUSE, X86_MODE_BOTH, X86_ISA_LATER, 0u, 0xf3u,
           1u, 0x90u, 0u, 0u, 0xffu, 0u, X86_OC_NONE, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND,
           X86_NO_DIGIT, X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FIXED(CTOOL_X86_MN_FWAIT, X86_ISA_X87, 1u, 0x9bu, 0u, 0u),
  X86_FIXED(CTOOL_X86_MN_FNINIT, X86_ISA_X87, 2u, 0xdbu, 0xe3u, 0u),
  X86_FIXED(CTOOL_X86_MN_FINIT, X86_ISA_X87, 3u, 0x9bu, 0xdbu, 0xe3u),
  X86_FIXED(CTOOL_X86_MN_F2XM1, X86_ISA_X87, 2u, 0xd9u, 0xf0u, 0u),
  X86_FIXED(CTOOL_X86_MN_FABS, X86_ISA_X87, 2u, 0xd9u, 0xe1u, 0u),
  X86_FIXED(CTOOL_X86_MN_FCHS, X86_ISA_X87, 2u, 0xd9u, 0xe0u, 0u),
  X86_FIXED(CTOOL_X86_MN_FCOS, X86_ISA_X87, 2u, 0xd9u, 0xffu, 0u),
  X86_FIXED(CTOOL_X86_MN_FLD1, X86_ISA_X87, 2u, 0xd9u, 0xe8u, 0u),
  X86_FIXED(CTOOL_X86_MN_FPATAN, X86_ISA_X87, 2u, 0xd9u, 0xf3u, 0u),
  X86_FIXED(CTOOL_X86_MN_FPREM, X86_ISA_X87, 2u, 0xd9u, 0xf8u, 0u),
  X86_FIXED(CTOOL_X86_MN_FPTAN, X86_ISA_X87, 2u, 0xd9u, 0xf2u, 0u),
  X86_FIXED(CTOOL_X86_MN_FRNDINT, X86_ISA_X87, 2u, 0xd9u, 0xfcu, 0u),
  X86_FIXED(CTOOL_X86_MN_FSCALE, X86_ISA_X87, 2u, 0xd9u, 0xfdu, 0u),
  X86_FIXED(CTOOL_X86_MN_FSIN, X86_ISA_X87, 2u, 0xd9u, 0xfeu, 0u),
  X86_FIXED(CTOOL_X86_MN_FSQRT, X86_ISA_X87, 2u, 0xd9u, 0xfau, 0u),
  X86_FIXED(CTOOL_X86_MN_FYL2X, X86_ISA_X87, 2u, 0xd9u, 0xf1u, 0u),

  X86_STRING(CTOOL_X86_MN_MOVSB, 8u, 0xa4u),
  X86_STRING(CTOOL_X86_MN_MOVSW, 16u, 0xa5u),
  X86_STRING(CTOOL_X86_MN_MOVSD, 32u, 0xa5u),
  X86_STRING(CTOOL_X86_MN_STOSB, 8u, 0xaau),
  X86_STRING(CTOOL_X86_MN_STOSW, 16u, 0xabu),
  X86_STRING(CTOOL_X86_MN_STOSD, 32u, 0xabu),
  X86_STRING(CTOOL_X86_MN_INSW, 16u, 0x6du),
  X86_STRING(CTOOL_X86_MN_OUTSW, 16u, 0x6fu),

  X86_ALU(CTOOL_X86_MN_ADD, 0x00u, 0u, X86_FORM_LOCKABLE),
  X86_ALU(CTOOL_X86_MN_OR, 0x08u, 1u, X86_FORM_LOCKABLE),
  X86_ALU(CTOOL_X86_MN_ADC, 0x10u, 2u, X86_FORM_LOCKABLE),
  X86_ALU(CTOOL_X86_MN_SBB, 0x18u, 3u, X86_FORM_LOCKABLE),
  X86_ALU(CTOOL_X86_MN_AND, 0x20u, 4u, X86_FORM_LOCKABLE),
  X86_ALU(CTOOL_X86_MN_SUB, 0x28u, 5u, X86_FORM_LOCKABLE),
  X86_ALU(CTOOL_X86_MN_XOR, 0x30u, 6u, X86_FORM_LOCKABLE),
  X86_ALU(CTOOL_X86_MN_CMP, 0x38u, 7u, 0u),
  X86_ACC_IMM(CTOOL_X86_MN_ADD, 8u, 0x04u),
  X86_ACC_IMM(CTOOL_X86_MN_ADD, 16u, 0x05u),
  X86_ACC_IMM(CTOOL_X86_MN_ADD, 32u, 0x05u),
  X86_ACC_IMM(CTOOL_X86_MN_OR, 8u, 0x0cu),
  X86_ACC_IMM(CTOOL_X86_MN_OR, 16u, 0x0du),
  X86_ACC_IMM(CTOOL_X86_MN_OR, 32u, 0x0du),
  X86_ACC_IMM(CTOOL_X86_MN_ADC, 8u, 0x14u),
  X86_ACC_IMM(CTOOL_X86_MN_ADC, 16u, 0x15u),
  X86_ACC_IMM(CTOOL_X86_MN_ADC, 32u, 0x15u),
  X86_ACC_IMM(CTOOL_X86_MN_SBB, 8u, 0x1cu),
  X86_ACC_IMM(CTOOL_X86_MN_SBB, 16u, 0x1du),
  X86_ACC_IMM(CTOOL_X86_MN_SBB, 32u, 0x1du),
  X86_ACC_IMM(CTOOL_X86_MN_AND, 8u, 0x24u),
  X86_ACC_IMM(CTOOL_X86_MN_AND, 16u, 0x25u),
  X86_ACC_IMM(CTOOL_X86_MN_AND, 32u, 0x25u),
  X86_ACC_IMM(CTOOL_X86_MN_SUB, 8u, 0x2cu),
  X86_ACC_IMM(CTOOL_X86_MN_SUB, 16u, 0x2du),
  X86_ACC_IMM(CTOOL_X86_MN_SUB, 32u, 0x2du),
  X86_ACC_IMM(CTOOL_X86_MN_XOR, 8u, 0x34u),
  X86_ACC_IMM(CTOOL_X86_MN_XOR, 16u, 0x35u),
  X86_ACC_IMM(CTOOL_X86_MN_XOR, 32u, 0x35u),
  X86_ACC_IMM(CTOOL_X86_MN_CMP, 8u, 0x3cu),
  X86_ACC_IMM(CTOOL_X86_MN_CMP, 16u, 0x3du),
  X86_ACC_IMM(CTOOL_X86_MN_CMP, 32u, 0x3du),

  X86_JCC(CTOOL_X86_MN_JO, 0u),
  X86_JCC(CTOOL_X86_MN_JNO, 1u),
  X86_JCC(CTOOL_X86_MN_JB, 2u),
  X86_JCC(CTOOL_X86_MN_JAE, 3u),
  X86_JCC(CTOOL_X86_MN_JE, 4u),
  X86_JCC(CTOOL_X86_MN_JNE, 5u),
  X86_JCC(CTOOL_X86_MN_JBE, 6u),
  X86_JCC(CTOOL_X86_MN_JA, 7u),
  X86_JCC(CTOOL_X86_MN_JS, 8u),
  X86_JCC(CTOOL_X86_MN_JNS, 9u),
  X86_JCC(CTOOL_X86_MN_JP, 10u),
  X86_JCC(CTOOL_X86_MN_JNP, 11u),
  X86_JCC(CTOOL_X86_MN_JL, 12u),
  X86_JCC(CTOOL_X86_MN_JGE, 13u),
  X86_JCC(CTOOL_X86_MN_JLE, 14u),
  X86_JCC(CTOOL_X86_MN_JG, 15u),
  X86_REL(CTOOL_X86_MN_CALL, 16u, 0xe8u, 0u, 1u),
  X86_REL(CTOOL_X86_MN_CALL, 32u, 0xe8u, 0u, 1u),
  X86_REL(CTOOL_X86_MN_JMP, 8u, 0xebu, 0u, 1u),
  X86_REL(CTOOL_X86_MN_JMP, 16u, 0xe9u, 0u, 1u),
  X86_REL(CTOOL_X86_MN_JMP, 32u, 0xe9u, 0u, 1u),

  X86_FORM(CTOOL_X86_MN_MOV, X86_MODE_BOTH, X86_ISA_386, 8u, 0u, 1u,
           0xb0u, 0u, 0u, 0xf8u, 2u, X86_OC_GPR8, X86_OC_IMM8,
           X86_OC_NONE, 0, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_DIGIT, 1,
           8u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FORM(CTOOL_X86_MN_MOV, X86_MODE_BOTH, X86_ISA_386, 16u, 0u, 1u,
           0xb8u, 0u, 0u, 0xf8u, 2u, X86_OC_GPR16, X86_OC_IMM16,
           X86_OC_NONE, 0, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_DIGIT, 1,
           16u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FORM(CTOOL_X86_MN_MOV, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 1u,
           0xb8u, 0u, 0u, 0xf8u, 2u, X86_OC_GPR32, X86_OC_IMM32,
           X86_OC_NONE, 0, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_DIGIT, 1,
           32u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_RM_REG(CTOOL_X86_MN_MOV, 8u, 0x88u, 0u),
  X86_RM_REG(CTOOL_X86_MN_MOV, 16u, 0x89u, 0u),
  X86_RM_REG(CTOOL_X86_MN_MOV, 32u, 0x89u, 0u),
  X86_REG_RM(CTOOL_X86_MN_MOV, 8u, 0x8au),
  X86_REG_RM(CTOOL_X86_MN_MOV, 16u, 0x8bu),
  X86_REG_RM(CTOOL_X86_MN_MOV, 32u, 0x8bu),
  X86_RM_IMM(CTOOL_X86_MN_MOV, 8u, 8u, 0xc6u, 0u, 0u),
  X86_RM_IMM(CTOOL_X86_MN_MOV, 16u, 16u, 0xc7u, 0u, 0u),
  X86_RM_IMM(CTOOL_X86_MN_MOV, 32u, 32u, 0xc7u, 0u, 0u),
  X86_FORM(CTOOL_X86_MN_MOV, X86_MODE_BOTH, X86_ISA_8086, 8u, 0u, 1u,
           0xa0u, 0u, 0u, 0xffu, 2u, X86_OC_GPR8, X86_OC_MEM8,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND,
           X86_NO_DIGIT, X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_DISPLACEMENT,
           X86_FORM_FIXED_REG0 | X86_FORM_MOFFS),
  X86_FORM(CTOOL_X86_MN_MOV, X86_MODE_BOTH, X86_ISA_8086, 16u, 0u, 1u,
           0xa1u, 0u, 0u, 0xffu, 2u, X86_OC_GPR16, X86_OC_MEM16,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND,
           X86_NO_DIGIT, X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_DISPLACEMENT,
           X86_FORM_FIXED_REG0 | X86_FORM_MOFFS),
  X86_FORM(CTOOL_X86_MN_MOV, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 1u,
           0xa1u, 0u, 0u, 0xffu, 2u, X86_OC_GPR32, X86_OC_MEM32,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND,
           X86_NO_DIGIT, X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_DISPLACEMENT,
           X86_FORM_FIXED_REG0 | X86_FORM_MOFFS),
  X86_FORM(CTOOL_X86_MN_MOV, X86_MODE_BOTH, X86_ISA_8086, 8u, 0u, 1u,
           0xa2u, 0u, 0u, 0xffu, 2u, X86_OC_MEM8, X86_OC_GPR8,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND,
           X86_NO_DIGIT, X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_DISPLACEMENT,
           X86_FORM_FIXED_REG1 | X86_FORM_MOFFS),
  X86_FORM(CTOOL_X86_MN_MOV, X86_MODE_BOTH, X86_ISA_8086, 16u, 0u, 1u,
           0xa3u, 0u, 0u, 0xffu, 2u, X86_OC_MEM16, X86_OC_GPR16,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND,
           X86_NO_DIGIT, X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_DISPLACEMENT,
           X86_FORM_FIXED_REG1 | X86_FORM_MOFFS),
  X86_FORM(CTOOL_X86_MN_MOV, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 1u,
           0xa3u, 0u, 0u, 0xffu, 2u, X86_OC_MEM32, X86_OC_GPR32,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND,
           X86_NO_DIGIT, X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_DISPLACEMENT,
           X86_FORM_FIXED_REG1 | X86_FORM_MOFFS),
  X86_FORM(CTOOL_X86_MN_MOV, X86_MODE_BOTH, X86_ISA_386, 0u, 0u, 2u,
           0x0fu, 0x22u, 0u, 0xffu, 2u, X86_OC_CONTROL, X86_OC_GPR32,
           X86_OC_NONE, X86_NO_OPERAND, 0, 1, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_REGISTER_ONLY),
  X86_FORM(CTOOL_X86_MN_MOV, X86_MODE_BOTH, X86_ISA_386, 0u, 0u, 2u,
           0x0fu, 0x20u, 0u, 0xffu, 2u, X86_OC_GPR32, X86_OC_CONTROL,
           X86_OC_NONE, X86_NO_OPERAND, 1, 0, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_REGISTER_ONLY),

  X86_FORM(CTOOL_X86_MN_FXSAVE, X86_MODE_BOTH, X86_ISA_SSE, 0u, 0u, 2u,
           0x0fu, 0xaeu, 0u, 0xffu, 1u, X86_OC_MEM, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, 0, 0u,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_MEMORY_ONLY),
  X86_FORM(CTOOL_X86_MN_FXRSTOR, X86_MODE_BOTH, X86_ISA_SSE, 0u, 0u, 2u,
           0x0fu, 0xaeu, 0u, 0xffu, 1u, X86_OC_MEM, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, 0, 1u,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_MEMORY_ONLY),
  X86_FORM(CTOOL_X86_MN_LDMXCSR, X86_MODE_BOTH, X86_ISA_SSE, 0u, 0u, 2u,
           0x0fu, 0xaeu, 0u, 0xffu, 1u, X86_OC_MEM32, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, 0, 2u,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_MEMORY_ONLY),
  X86_FORM(CTOOL_X86_MN_STMXCSR, X86_MODE_BOTH, X86_ISA_SSE, 0u, 0u, 2u,
           0x0fu, 0xaeu, 0u, 0xffu, 1u, X86_OC_MEM32, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, 0, 3u,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_MEMORY_ONLY),
  X86_SSE_RM(CTOOL_X86_MN_MOVSS, X86_ISA_SSE, 0xf3u, 0x10u,
             X86_OC_XMM_RM32),
  X86_SSE_STORE(CTOOL_X86_MN_MOVSS, X86_ISA_SSE, 0xf3u, 0x11u,
                X86_OC_XMM_RM32),
  X86_SSE_RM(CTOOL_X86_MN_MOVSD, X86_ISA_SSE2, 0xf2u, 0x10u,
             X86_OC_XMM_RM64),
  X86_SSE_STORE(CTOOL_X86_MN_MOVSD, X86_ISA_SSE2, 0xf2u, 0x11u,
                X86_OC_XMM_RM64),
  X86_SSE_RM(CTOOL_X86_MN_MOVUPS, X86_ISA_SSE, 0u, 0x10u,
             X86_OC_XMM_RM128),
  X86_SSE_STORE(CTOOL_X86_MN_MOVUPS, X86_ISA_SSE, 0u, 0x11u,
                X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_ADDSS, X86_ISA_SSE, 0xf3u, 0x58u,
             X86_OC_XMM_RM32),
  X86_SSE_RM(CTOOL_X86_MN_ADDPS, X86_ISA_SSE, 0u, 0x58u,
             X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_MULPS, X86_ISA_SSE, 0u, 0x59u,
             X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_SQRTSS, X86_ISA_SSE, 0xf3u, 0x51u,
             X86_OC_XMM_RM32),
  X86_SSE_RM(CTOOL_X86_MN_PXOR, X86_ISA_SSE2, 0x66u, 0xefu,
             X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_MOVDQA, X86_ISA_SSE2, 0x66u, 0x6fu,
             X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_MOVDQU, X86_ISA_SSE2, 0xf3u, 0x6fu,
             X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_PACKUSWB, X86_ISA_SSE2, 0x66u, 0x67u,
             X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_PADDUSB, X86_ISA_SSE2, 0x66u, 0xdcu,
             X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_PADDW, X86_ISA_SSE2, 0x66u, 0xfdu,
             X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_PMULLW, X86_ISA_SSE2, 0x66u, 0xd5u,
             X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_PUNPCKHBW, X86_ISA_SSE2, 0x66u, 0x68u,
             X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_PUNPCKLBW, X86_ISA_SSE2, 0x66u, 0x60u,
             X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_PUNPCKLWD, X86_ISA_SSE2, 0x66u, 0x61u,
             X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_ADDSD, X86_ISA_SSE2, 0xf2u, 0x58u,
             X86_OC_XMM_RM64),
  X86_SSE_RM(CTOOL_X86_MN_ADDPD, X86_ISA_SSE2, 0x66u, 0x58u,
             X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_SUBSS, X86_ISA_SSE, 0xf3u, 0x5cu,
             X86_OC_XMM_RM32),
  X86_SSE_RM(CTOOL_X86_MN_SUBSD, X86_ISA_SSE2, 0xf2u, 0x5cu,
             X86_OC_XMM_RM64),
  X86_SSE_RM(CTOOL_X86_MN_SUBPS, X86_ISA_SSE, 0u, 0x5cu,
             X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_SUBPD, X86_ISA_SSE2, 0x66u, 0x5cu,
             X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_MULSS, X86_ISA_SSE, 0xf3u, 0x59u,
             X86_OC_XMM_RM32),
  X86_SSE_RM(CTOOL_X86_MN_MULSD, X86_ISA_SSE2, 0xf2u, 0x59u,
             X86_OC_XMM_RM64),
  X86_SSE_RM(CTOOL_X86_MN_MULPD, X86_ISA_SSE2, 0x66u, 0x59u,
             X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_DIVSS, X86_ISA_SSE, 0xf3u, 0x5eu,
             X86_OC_XMM_RM32),
  X86_SSE_RM(CTOOL_X86_MN_DIVSD, X86_ISA_SSE2, 0xf2u, 0x5eu,
             X86_OC_XMM_RM64),
  X86_SSE_RM(CTOOL_X86_MN_DIVPS, X86_ISA_SSE, 0u, 0x5eu,
             X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_DIVPD, X86_ISA_SSE2, 0x66u, 0x5eu,
             X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_MINSS, X86_ISA_SSE, 0xf3u, 0x5du,
             X86_OC_XMM_RM32),
  X86_SSE_RM(CTOOL_X86_MN_MINPS, X86_ISA_SSE, 0u, 0x5du,
             X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_MINPD, X86_ISA_SSE2, 0x66u, 0x5du,
             X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_MAXSS, X86_ISA_SSE, 0xf3u, 0x5fu,
             X86_OC_XMM_RM32),
  X86_SSE_RM(CTOOL_X86_MN_MAXPS, X86_ISA_SSE, 0u, 0x5fu,
             X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_MAXPD, X86_ISA_SSE2, 0x66u, 0x5fu,
             X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_SQRTSD, X86_ISA_SSE2, 0xf2u, 0x51u,
             X86_OC_XMM_RM64),
  X86_SSE_RM(CTOOL_X86_MN_SQRTPS, X86_ISA_SSE, 0u, 0x51u,
             X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_SQRTPD, X86_ISA_SSE2, 0x66u, 0x51u,
             X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_ANDPS, X86_ISA_SSE, 0u, 0x54u,
             X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_ANDPD, X86_ISA_SSE2, 0x66u, 0x54u,
             X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_ORPS, X86_ISA_SSE, 0u, 0x56u,
             X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_ORPD, X86_ISA_SSE2, 0x66u, 0x56u,
             X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_XORPS, X86_ISA_SSE, 0u, 0x57u,
             X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_XORPD, X86_ISA_SSE2, 0x66u, 0x57u,
             X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_MOVAPS, X86_ISA_SSE, 0u, 0x28u,
             X86_OC_XMM_RM128),
  X86_SSE_STORE(CTOOL_X86_MN_MOVAPS, X86_ISA_SSE, 0u, 0x29u,
                X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_MOVAPD, X86_ISA_SSE2, 0x66u, 0x28u,
             X86_OC_XMM_RM128),
  X86_SSE_STORE(CTOOL_X86_MN_MOVAPD, X86_ISA_SSE2, 0x66u, 0x29u,
                X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_MOVUPD, X86_ISA_SSE2, 0x66u, 0x10u,
             X86_OC_XMM_RM128),
  X86_SSE_STORE(CTOOL_X86_MN_MOVUPD, X86_ISA_SSE2, 0x66u, 0x11u,
                X86_OC_XMM_RM128),
  X86_SSE_STORE(CTOOL_X86_MN_MOVDQA, X86_ISA_SSE2, 0x66u, 0x7fu,
                X86_OC_XMM_RM128),
  X86_SSE_STORE(CTOOL_X86_MN_MOVDQU, X86_ISA_SSE2, 0xf3u, 0x7fu,
                X86_OC_XMM_RM128),
  X86_SSE_XMM_GPR(CTOOL_X86_MN_CVTSI2SS, X86_ISA_SSE, 0xf3u, 0x2au),
  X86_SSE_XMM_GPR(CTOOL_X86_MN_CVTSI2SD, X86_ISA_SSE2, 0xf2u, 0x2au),
  X86_SSE_GPR_XMM(CTOOL_X86_MN_CVTSS2SI, X86_ISA_SSE, 0xf3u, 0x2du,
                  X86_OC_XMM_RM32),
  X86_SSE_GPR_XMM(CTOOL_X86_MN_CVTSD2SI, X86_ISA_SSE2, 0xf2u, 0x2du,
                  X86_OC_XMM_RM64),
  X86_SSE_GPR_XMM(CTOOL_X86_MN_CVTTSS2SI, X86_ISA_SSE, 0xf3u, 0x2cu,
                  X86_OC_XMM_RM32),
  X86_SSE_GPR_XMM(CTOOL_X86_MN_CVTTSD2SI, X86_ISA_SSE2, 0xf2u, 0x2cu,
                  X86_OC_XMM_RM64),
  X86_SSE_RM(CTOOL_X86_MN_CVTSS2SD, X86_ISA_SSE2, 0xf3u, 0x5au,
             X86_OC_XMM_RM32),
  X86_SSE_RM(CTOOL_X86_MN_CVTSD2SS, X86_ISA_SSE2, 0xf2u, 0x5au,
             X86_OC_XMM_RM64),
  X86_SSE_RM(CTOOL_X86_MN_CVTPS2DQ, X86_ISA_SSE2, 0x66u, 0x5bu,
             X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_CVTDQ2PS, X86_ISA_SSE2, 0u, 0x5bu,
             X86_OC_XMM_RM128),
  X86_SSE_RM(CTOOL_X86_MN_UCOMISS, X86_ISA_SSE, 0u, 0x2eu,
             X86_OC_XMM_RM32),
  X86_SSE_RM(CTOOL_X86_MN_UCOMISD, X86_ISA_SSE2, 0x66u, 0x2eu,
             X86_OC_XMM_RM64),
  X86_SSE_GPR_XMM(CTOOL_X86_MN_MOVMSKPS, X86_ISA_SSE, 0u, 0x50u,
                  X86_OC_XMM),
  X86_SSE_IMM(CTOOL_X86_MN_CMPPS, X86_ISA_SSE, 0u, 0xc2u),
  X86_SSE_IMM(CTOOL_X86_MN_SHUFPS, X86_ISA_SSE, 0u, 0xc6u),
  X86_SSE_IMM(CTOOL_X86_MN_SHUFPD, X86_ISA_SSE2, 0x66u, 0xc6u),
  X86_SSE_IMM(CTOOL_X86_MN_PSHUFD, X86_ISA_SSE2, 0x66u, 0x70u),
  X86_FORM(CTOOL_X86_MN_PSRLW, X86_MODE_BOTH, X86_ISA_SSE2, 0u, 0x66u,
           2u, 0x0fu, 0x71u, 0u, 0xffu, 2u, X86_OC_XMM,
           X86_OC_IMM8, X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, 0,
           2u, 1, 8u, CTOOL_X86_FIELD_IMMEDIATE, X86_FORM_REGISTER_ONLY),
  X86_FORM(CTOOL_X86_MN_MOVD, X86_MODE_BOTH, X86_ISA_SSE2, 0u, 0x66u,
           2u, 0x0fu, 0x6eu, 0u, 0xffu, 2u, X86_OC_XMM, X86_OC_RM32,
           X86_OC_NONE, X86_NO_OPERAND, 0, 1, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FORM(CTOOL_X86_MN_MOVD, X86_MODE_BOTH, X86_ISA_SSE2, 0u, 0x66u,
           2u, 0x0fu, 0x7eu, 0u, 0xffu, 2u, X86_OC_RM32, X86_OC_XMM,
           X86_OC_NONE, X86_NO_OPERAND, 1, 0, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FORM(CTOOL_X86_MN_MOVNTDQ, X86_MODE_BOTH, X86_ISA_SSE2, 0u, 0x66u,
           2u, 0x0fu, 0xe7u, 0u, 0xffu, 2u, X86_OC_MEM128, X86_OC_XMM,
           X86_OC_NONE, X86_NO_OPERAND, 1, 0, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_MEMORY_ONLY),
  X86_FIXED(CTOOL_X86_MN_SFENCE, X86_ISA_SSE, 3u, 0x0fu, 0xaeu, 0xf8u),

  X86_FORM(CTOOL_X86_MN_BSWAP, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 2u,
           0x0fu, 0xc8u, 0u, 0xf8u, 1u, X86_OC_GPR32, X86_OC_NONE,
           X86_OC_NONE, 0, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FORM(CTOOL_X86_MN_INC, X86_MODE_BOTH, X86_ISA_386, 16u, 0u, 1u,
           0x40u, 0u, 0u, 0xf8u, 1u, X86_OC_GPR16, X86_OC_NONE,
           X86_OC_NONE, 0, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FORM(CTOOL_X86_MN_INC, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 1u,
           0x40u, 0u, 0u, 0xf8u, 1u, X86_OC_GPR32, X86_OC_NONE,
           X86_OC_NONE, 0, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FORM(CTOOL_X86_MN_DEC, X86_MODE_BOTH, X86_ISA_386, 16u, 0u, 1u,
           0x48u, 0u, 0u, 0xf8u, 1u, X86_OC_GPR16, X86_OC_NONE,
           X86_OC_NONE, 0, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FORM(CTOOL_X86_MN_DEC, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 1u,
           0x48u, 0u, 0u, 0xf8u, 1u, X86_OC_GPR32, X86_OC_NONE,
           X86_OC_NONE, 0, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FORM(CTOOL_X86_MN_INC, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 1u,
           0xffu, 0u, 0u, 0xffu, 1u, X86_OC_RM32, X86_OC_NONE, X86_OC_NONE,
           X86_NO_OPERAND, X86_NO_OPERAND, 0, 0u, X86_NO_OPERAND, 0u,
           CTOOL_X86_FIELD_IMMEDIATE, X86_FORM_LOCKABLE),
  X86_FORM(CTOOL_X86_MN_DEC, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 1u,
           0xffu, 0u, 0u, 0xffu, 1u, X86_OC_RM32, X86_OC_NONE, X86_OC_NONE,
           X86_NO_OPERAND, X86_NO_OPERAND, 0, 1u, X86_NO_OPERAND, 0u,
           CTOOL_X86_FIELD_IMMEDIATE, X86_FORM_LOCKABLE),
  X86_GROUP_RM(CTOOL_X86_MN_INC, 8u, 0xfeu, 0u, X86_FORM_LOCKABLE),
  X86_GROUP_RM(CTOOL_X86_MN_INC, 16u, 0xffu, 0u, X86_FORM_LOCKABLE),
  X86_GROUP_RM(CTOOL_X86_MN_DEC, 8u, 0xfeu, 1u, X86_FORM_LOCKABLE),
  X86_GROUP_RM(CTOOL_X86_MN_DEC, 16u, 0xffu, 1u, X86_FORM_LOCKABLE),

  X86_FORM(CTOOL_X86_MN_MUL, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 1u,
           0xf7u, 0u, 0u, 0xffu, 1u, X86_OC_RM32, X86_OC_NONE, X86_OC_NONE,
           X86_NO_OPERAND, X86_NO_OPERAND, 0, 4u, X86_NO_OPERAND, 0u,
           CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FORM(CTOOL_X86_MN_IMUL, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 2u,
           0x0fu, 0xafu, 0u, 0xffu, 2u, X86_OC_GPR32, X86_OC_RM32,
           X86_OC_NONE, X86_NO_OPERAND, 0, 1, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FORM(CTOOL_X86_MN_DIV, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 1u,
           0xf7u, 0u, 0u, 0xffu, 1u, X86_OC_RM32, X86_OC_NONE, X86_OC_NONE,
           X86_NO_OPERAND, X86_NO_OPERAND, 0, 6u, X86_NO_OPERAND, 0u,
           CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FORM(CTOOL_X86_MN_IDIV, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 1u,
           0xf7u, 0u, 0u, 0xffu, 1u, X86_OC_RM32, X86_OC_NONE, X86_OC_NONE,
           X86_NO_OPERAND, X86_NO_OPERAND, 0, 7u, X86_NO_OPERAND, 0u,
           CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_GROUP_RM(CTOOL_X86_MN_MUL, 8u, 0xf6u, 4u, 0u),
  X86_GROUP_RM(CTOOL_X86_MN_MUL, 16u, 0xf7u, 4u, 0u),
  X86_GROUP_RM(CTOOL_X86_MN_IMUL, 8u, 0xf6u, 5u, 0u),
  X86_GROUP_RM(CTOOL_X86_MN_IMUL, 16u, 0xf7u, 5u, 0u),
  X86_GROUP_RM(CTOOL_X86_MN_IMUL, 32u, 0xf7u, 5u, 0u),
  X86_FORM(CTOOL_X86_MN_IMUL, X86_MODE_BOTH, X86_ISA_386, 16u, 0u, 2u,
           0x0fu, 0xafu, 0u, 0xffu, 2u, X86_OC_GPR16, X86_OC_RM16,
           X86_OC_NONE, X86_NO_OPERAND, 0, 1, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_GROUP_RM(CTOOL_X86_MN_DIV, 8u, 0xf6u, 6u, 0u),
  X86_GROUP_RM(CTOOL_X86_MN_DIV, 16u, 0xf7u, 6u, 0u),
  X86_GROUP_RM(CTOOL_X86_MN_IDIV, 8u, 0xf6u, 7u, 0u),
  X86_GROUP_RM(CTOOL_X86_MN_IDIV, 16u, 0xf7u, 7u, 0u),

  X86_SHIFT(CTOOL_X86_MN_ROL, 0u),
  X86_SHIFT(CTOOL_X86_MN_ROR, 1u),
  X86_SHIFT(CTOOL_X86_MN_RCL, 2u),
  X86_SHIFT(CTOOL_X86_MN_RCR, 3u),
  X86_SHIFT(CTOOL_X86_MN_SHL, 4u),
  X86_SHIFT(CTOOL_X86_MN_SHR, 5u),
  X86_SHIFT(CTOOL_X86_MN_SAR, 7u),
  X86_RM_REG(CTOOL_X86_MN_TEST, 8u, 0x84u, 0u),
  X86_RM_REG(CTOOL_X86_MN_TEST, 16u, 0x85u, 0u),
  X86_RM_REG(CTOOL_X86_MN_TEST, 32u, 0x85u, 0u),
  X86_ACC_IMM(CTOOL_X86_MN_TEST, 8u, 0xa8u),
  X86_ACC_IMM(CTOOL_X86_MN_TEST, 16u, 0xa9u),
  X86_ACC_IMM(CTOOL_X86_MN_TEST, 32u, 0xa9u),
  X86_RM_IMM(CTOOL_X86_MN_TEST, 8u, 8u, 0xf6u, 0u, 0u),
  X86_RM_IMM(CTOOL_X86_MN_TEST, 16u, 16u, 0xf7u, 0u, 0u),
  X86_RM_IMM(CTOOL_X86_MN_TEST, 32u, 32u, 0xf7u, 0u, 0u),
  X86_GROUP_RM(CTOOL_X86_MN_NOT, 8u, 0xf6u, 2u, X86_FORM_LOCKABLE),
  X86_GROUP_RM(CTOOL_X86_MN_NOT, 16u, 0xf7u, 2u, X86_FORM_LOCKABLE),
  X86_GROUP_RM(CTOOL_X86_MN_NOT, 32u, 0xf7u, 2u, X86_FORM_LOCKABLE),
  X86_GROUP_RM(CTOOL_X86_MN_NEG, 8u, 0xf6u, 3u, X86_FORM_LOCKABLE),
  X86_GROUP_RM(CTOOL_X86_MN_NEG, 16u, 0xf7u, 3u, X86_FORM_LOCKABLE),
  X86_GROUP_RM(CTOOL_X86_MN_NEG, 32u, 0xf7u, 3u, X86_FORM_LOCKABLE),

  X86_FORM(CTOOL_X86_MN_MOVZX, X86_MODE_BOTH, X86_ISA_386, 16u, 0u, 2u,
           0x0fu, 0xb6u, 0u, 0xffu, 2u, X86_OC_GPR16, X86_OC_RM8,
           X86_OC_NONE, X86_NO_OPERAND, 0, 1, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FORM(CTOOL_X86_MN_MOVZX, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 2u,
           0x0fu, 0xb6u, 0u, 0xffu, 2u, X86_OC_GPR32, X86_OC_RM8,
           X86_OC_NONE, X86_NO_OPERAND, 0, 1, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FORM(CTOOL_X86_MN_MOVZX, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 2u,
           0x0fu, 0xb7u, 0u, 0xffu, 2u, X86_OC_GPR32, X86_OC_RM16,
           X86_OC_NONE, X86_NO_OPERAND, 0, 1, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FORM(CTOOL_X86_MN_MOVSX, X86_MODE_BOTH, X86_ISA_386, 16u, 0u, 2u,
           0x0fu, 0xbeu, 0u, 0xffu, 2u, X86_OC_GPR16, X86_OC_RM8,
           X86_OC_NONE, X86_NO_OPERAND, 0, 1, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FORM(CTOOL_X86_MN_MOVSX, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 2u,
           0x0fu, 0xbeu, 0u, 0xffu, 2u, X86_OC_GPR32, X86_OC_RM8,
           X86_OC_NONE, X86_NO_OPERAND, 0, 1, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FORM(CTOOL_X86_MN_MOVSX, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 2u,
           0x0fu, 0xbfu, 0u, 0xffu, 2u, X86_OC_GPR32, X86_OC_RM16,
           X86_OC_NONE, X86_NO_OPERAND, 0, 1, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FORM(CTOOL_X86_MN_LEA, X86_MODE_BOTH, X86_ISA_386, 16u, 0u, 1u,
           0x8du, 0u, 0u, 0xffu, 2u, X86_OC_GPR16, X86_OC_MEM,
           X86_OC_NONE, X86_NO_OPERAND, 0, 1, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_MEMORY_ONLY),
  X86_FORM(CTOOL_X86_MN_LEA, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 1u,
           0x8du, 0u, 0u, 0xffu, 2u, X86_OC_GPR32, X86_OC_MEM,
           X86_OC_NONE, X86_NO_OPERAND, 0, 1, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_MEMORY_ONLY),

  X86_FORM(CTOOL_X86_MN_PUSH, X86_MODE_BOTH, X86_ISA_386, 16u, 0u, 1u,
           0x50u, 0u, 0u, 0xf8u, 1u, X86_OC_GPR16, X86_OC_NONE,
           X86_OC_NONE, 0, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FORM(CTOOL_X86_MN_PUSH, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 1u,
           0x50u, 0u, 0u, 0xf8u, 1u, X86_OC_GPR32, X86_OC_NONE,
           X86_OC_NONE, 0, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FORM(CTOOL_X86_MN_POP, X86_MODE_BOTH, X86_ISA_386, 16u, 0u, 1u,
           0x58u, 0u, 0u, 0xf8u, 1u, X86_OC_GPR16, X86_OC_NONE,
           X86_OC_NONE, 0, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FORM(CTOOL_X86_MN_POP, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 1u,
           0x58u, 0u, 0u, 0xf8u, 1u, X86_OC_GPR32, X86_OC_NONE,
           X86_OC_NONE, 0, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FORM(CTOOL_X86_MN_PUSH, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 1u,
           0x6au, 0u, 0u, 0xffu, 1u, X86_OC_IMM8, X86_OC_NONE, X86_OC_NONE,
           X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_DIGIT, 0,
           8u, CTOOL_X86_FIELD_IMMEDIATE, X86_FORM_SIGN_EXTENDED),
  X86_FORM(CTOOL_X86_MN_PUSH, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 1u,
           0x68u, 0u, 0u, 0xffu, 1u, X86_OC_IMM32, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND,
           X86_NO_DIGIT, 0, 32u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FORM(CTOOL_X86_MN_PUSH, X86_MODE_BOTH, X86_ISA_386, 16u, 0u, 1u,
           0x6au, 0u, 0u, 0xffu, 1u, X86_OC_IMM8, X86_OC_NONE, X86_OC_NONE,
           X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_DIGIT, 0,
           8u, CTOOL_X86_FIELD_IMMEDIATE, X86_FORM_SIGN_EXTENDED),
  X86_FORM(CTOOL_X86_MN_PUSH, X86_MODE_BOTH, X86_ISA_386, 16u, 0u, 1u,
           0x68u, 0u, 0u, 0xffu, 1u, X86_OC_IMM16, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND,
           X86_NO_DIGIT, 0, 16u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_GROUP_RM(CTOOL_X86_MN_PUSH, 16u, 0xffu, 6u, 0u),
  X86_GROUP_RM(CTOOL_X86_MN_PUSH, 32u, 0xffu, 6u, 0u),
  X86_GROUP_RM(CTOOL_X86_MN_POP, 16u, 0x8fu, 0u, 0u),
  X86_GROUP_RM(CTOOL_X86_MN_POP, 32u, 0x8fu, 0u, 0u),

  X86_FORM(CTOOL_X86_MN_IN, X86_MODE_BOTH, X86_ISA_386, 8u, 0u, 1u, 0xe4u,
           0u, 0u, 0xffu, 2u, X86_OC_GPR8, X86_OC_IMM8, X86_OC_NONE,
           X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_DIGIT, 1,
           8u, CTOOL_X86_FIELD_IMMEDIATE, X86_FORM_FIXED_REG0),
  X86_FORM(CTOOL_X86_MN_IN, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 1u, 0xedu,
           0u, 0u, 0xffu, 2u, X86_OC_GPR32, X86_OC_GPR16, X86_OC_NONE,
           X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_FIXED_REG0 | X86_FORM_FIXED_DX1),
  X86_FORM(CTOOL_X86_MN_IN, X86_MODE_BOTH, X86_ISA_386, 16u, 0u, 1u, 0xe5u,
           0u, 0u, 0xffu, 2u, X86_OC_GPR16, X86_OC_IMM8, X86_OC_NONE,
           X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_DIGIT, 1,
           8u, CTOOL_X86_FIELD_IMMEDIATE, X86_FORM_FIXED_REG0),
  X86_FORM(CTOOL_X86_MN_IN, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 1u, 0xe5u,
           0u, 0u, 0xffu, 2u, X86_OC_GPR32, X86_OC_IMM8, X86_OC_NONE,
           X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_DIGIT, 1,
           8u, CTOOL_X86_FIELD_IMMEDIATE, X86_FORM_FIXED_REG0),
  X86_FORM(CTOOL_X86_MN_IN, X86_MODE_BOTH, X86_ISA_386, 8u, 0u, 1u, 0xecu,
           0u, 0u, 0xffu, 2u, X86_OC_GPR8, X86_OC_GPR16, X86_OC_NONE,
           X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_FIXED_REG0 | X86_FORM_FIXED_DX1),
  X86_FORM(CTOOL_X86_MN_IN, X86_MODE_BOTH, X86_ISA_386, 16u, 0u, 1u, 0xedu,
           0u, 0u, 0xffu, 2u, X86_OC_GPR16, X86_OC_GPR16, X86_OC_NONE,
           X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_FIXED_REG0 | X86_FORM_FIXED_DX1),
  X86_FORM(CTOOL_X86_MN_OUT, X86_MODE_BOTH, X86_ISA_386, 8u, 0u, 1u, 0xe6u,
           0u, 0u, 0xffu, 2u, X86_OC_IMM8, X86_OC_GPR8, X86_OC_NONE,
           X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_DIGIT, 0,
           8u, CTOOL_X86_FIELD_IMMEDIATE, X86_FORM_FIXED_REG1),
  X86_FORM(CTOOL_X86_MN_OUT, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 1u, 0xefu,
           0u, 0u, 0xffu, 2u, X86_OC_GPR16, X86_OC_GPR32, X86_OC_NONE,
           X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_FIXED_DX0 | X86_FORM_FIXED_REG1),
  X86_FORM(CTOOL_X86_MN_OUT, X86_MODE_BOTH, X86_ISA_386, 16u, 0u, 1u, 0xe7u,
           0u, 0u, 0xffu, 2u, X86_OC_IMM8, X86_OC_GPR16, X86_OC_NONE,
           X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_DIGIT, 0,
           8u, CTOOL_X86_FIELD_IMMEDIATE, X86_FORM_FIXED_REG1),
  X86_FORM(CTOOL_X86_MN_OUT, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 1u, 0xe7u,
           0u, 0u, 0xffu, 2u, X86_OC_IMM8, X86_OC_GPR32, X86_OC_NONE,
           X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_DIGIT, 0,
           8u, CTOOL_X86_FIELD_IMMEDIATE, X86_FORM_FIXED_REG1),
  X86_FORM(CTOOL_X86_MN_OUT, X86_MODE_BOTH, X86_ISA_386, 8u, 0u, 1u, 0xeeu,
           0u, 0u, 0xffu, 2u, X86_OC_GPR16, X86_OC_GPR8, X86_OC_NONE,
           X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_FIXED_DX0 | X86_FORM_FIXED_REG1),
  X86_FORM(CTOOL_X86_MN_OUT, X86_MODE_BOTH, X86_ISA_386, 16u, 0u, 1u, 0xefu,
           0u, 0u, 0xffu, 2u, X86_OC_GPR16, X86_OC_GPR16, X86_OC_NONE,
           X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_FIXED_DX0 | X86_FORM_FIXED_REG1),
  X86_FORM(CTOOL_X86_MN_INT, X86_MODE_BOTH, X86_ISA_8086, 0u, 0u, 1u, 0xcdu,
           0u, 0u, 0xffu, 1u, X86_OC_IMM8, X86_OC_NONE, X86_OC_NONE,
           X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_DIGIT, 0,
           8u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FORM(CTOOL_X86_MN_CALL, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 1u,
           0xffu, 0u, 0u, 0xffu, 1u, X86_OC_RM32, X86_OC_NONE, X86_OC_NONE,
           X86_NO_OPERAND, X86_NO_OPERAND, 0, 2u, X86_NO_OPERAND, 0u,
           CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_GROUP_RM(CTOOL_X86_MN_CALL, 16u, 0xffu, 2u, 0u),
  X86_FORM(CTOOL_X86_MN_CALL, X86_MODE_BOTH, X86_ISA_386, 16u, 0u, 1u,
           0xffu, 0u, 0u, 0xffu, 1u, X86_OC_MEM32, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, 0, 3u,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_MEMORY_ONLY),
  X86_FORM(CTOOL_X86_MN_CALL, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 1u,
           0xffu, 0u, 0u, 0xffu, 1u, X86_OC_MEM48, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, 0, 3u,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_MEMORY_ONLY),
  X86_FORM(CTOOL_X86_MN_JMP, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 1u,
           0xffu, 0u, 0u, 0xffu, 1u, X86_OC_RM32, X86_OC_NONE, X86_OC_NONE,
           X86_NO_OPERAND, X86_NO_OPERAND, 0, 4u, X86_NO_OPERAND, 0u,
           CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_GROUP_RM(CTOOL_X86_MN_JMP, 16u, 0xffu, 4u, 0u),
  X86_FORM(CTOOL_X86_MN_JMP, X86_MODE_BOTH, X86_ISA_386, 16u, 0u, 1u,
           0xffu, 0u, 0u, 0xffu, 1u, X86_OC_MEM32, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, 0, 5u,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_MEMORY_ONLY),
  X86_FORM(CTOOL_X86_MN_JMP, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 1u,
           0xffu, 0u, 0u, 0xffu, 1u, X86_OC_MEM48, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, 0, 5u,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_MEMORY_ONLY),

  X86_FORM(CTOOL_X86_MN_CMPXCHG, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 2u,
           0x0fu, 0xb1u, 0u, 0xffu, 2u, X86_OC_RM32, X86_OC_GPR32,
           X86_OC_NONE, X86_NO_OPERAND, 1, 0, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_LOCKABLE),
  X86_FORM(CTOOL_X86_MN_XADD, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 2u,
           0x0fu, 0xc1u, 0u, 0xffu, 2u, X86_OC_RM32, X86_OC_GPR32,
           X86_OC_NONE, X86_NO_OPERAND, 1, 0, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_LOCKABLE),
  X86_FORM(CTOOL_X86_MN_CMPXCHG, X86_MODE_BOTH, X86_ISA_386, 8u, 0u, 2u,
           0x0fu, 0xb0u, 0u, 0xffu, 2u, X86_OC_RM8, X86_OC_GPR8,
           X86_OC_NONE, X86_NO_OPERAND, 1, 0, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_LOCKABLE),
  X86_FORM(CTOOL_X86_MN_CMPXCHG, X86_MODE_BOTH, X86_ISA_386, 16u, 0u, 2u,
           0x0fu, 0xb1u, 0u, 0xffu, 2u, X86_OC_RM16, X86_OC_GPR16,
           X86_OC_NONE, X86_NO_OPERAND, 1, 0, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_LOCKABLE),
  X86_FORM(CTOOL_X86_MN_XADD, X86_MODE_BOTH, X86_ISA_386, 8u, 0u, 2u,
           0x0fu, 0xc0u, 0u, 0xffu, 2u, X86_OC_RM8, X86_OC_GPR8,
           X86_OC_NONE, X86_NO_OPERAND, 1, 0, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_LOCKABLE),
  X86_FORM(CTOOL_X86_MN_XADD, X86_MODE_BOTH, X86_ISA_386, 16u, 0u, 2u,
           0x0fu, 0xc1u, 0u, 0xffu, 2u, X86_OC_RM16, X86_OC_GPR16,
           X86_OC_NONE, X86_NO_OPERAND, 1, 0, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_LOCKABLE),
  X86_RM_REG(CTOOL_X86_MN_XCHG, 8u, 0x86u, X86_FORM_LOCKABLE),
  X86_RM_REG(CTOOL_X86_MN_XCHG, 16u, 0x87u, X86_FORM_LOCKABLE),
  X86_RM_REG(CTOOL_X86_MN_XCHG, 32u, 0x87u, X86_FORM_LOCKABLE),
  X86_SETCC(CTOOL_X86_MN_SETA, 7u, 0u),
  X86_SETCC(CTOOL_X86_MN_SETAE, 3u, 0u),
  X86_SETCC(CTOOL_X86_MN_SETB, 2u, 0u),
  X86_SETCC(CTOOL_X86_MN_SETBE, 6u, 0u),
  X86_SETCC(CTOOL_X86_MN_SETC, 2u, X86_FORM_DECODE_ALIAS),
  X86_SETCC(CTOOL_X86_MN_SETE, 4u, 0u),
  X86_SETCC(CTOOL_X86_MN_SETG, 15u, 0u),
  X86_SETCC(CTOOL_X86_MN_SETGE, 13u, 0u),
  X86_SETCC(CTOOL_X86_MN_SETL, 12u, 0u),
  X86_SETCC(CTOOL_X86_MN_SETLE, 14u, 0u),
  X86_SETCC(CTOOL_X86_MN_SETNE, 5u, 0u),
  X86_FORM(CTOOL_X86_MN_RDRAND, X86_MODE_BOTH, X86_ISA_LATER, 16u, 0u, 2u,
           0x0fu, 0xc7u, 0u, 0xffu, 1u, X86_OC_RM16, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, 0, 6u,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_REGISTER_ONLY),
  X86_FORM(CTOOL_X86_MN_RDRAND, X86_MODE_BOTH, X86_ISA_LATER, 32u, 0u, 2u,
           0x0fu, 0xc7u, 0u, 0xffu, 1u, X86_OC_RM32, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, 0, 6u,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_REGISTER_ONLY),
  X86_FORM(CTOOL_X86_MN_INVLPG, X86_MODE_BOTH, X86_ISA_386, 0u, 0u, 2u,
           0x0fu, 0x01u, 0u, 0xffu, 1u, X86_OC_MEM, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, 0, 7u,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_MEMORY_ONLY),
  X86_FORM(CTOOL_X86_MN_SGDT, X86_MODE_BOTH, X86_ISA_386, 0u, 0u, 2u,
           0x0fu, 0x01u, 0u, 0xffu, 1u, X86_OC_MEM48, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, 0, 0u,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_MEMORY_ONLY),
  X86_FORM(CTOOL_X86_MN_SIDT, X86_MODE_BOTH, X86_ISA_386, 0u, 0u, 2u,
           0x0fu, 0x01u, 0u, 0xffu, 1u, X86_OC_MEM48, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, 0, 1u,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_MEMORY_ONLY),
  X86_FORM(CTOOL_X86_MN_LGDT, X86_MODE_BOTH, X86_ISA_386, 0u, 0u, 2u,
           0x0fu, 0x01u, 0u, 0xffu, 1u, X86_OC_MEM48, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, 0, 2u,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_MEMORY_ONLY),
  X86_FORM(CTOOL_X86_MN_LIDT, X86_MODE_BOTH, X86_ISA_386, 0u, 0u, 2u,
           0x0fu, 0x01u, 0u, 0xffu, 1u, X86_OC_MEM48, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, 0, 3u,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_MEMORY_ONLY),
  X86_FORM(CTOOL_X86_MN_SLDT, X86_MODE_BOTH, X86_ISA_386, 16u, 0u, 2u,
           0x0fu, 0x00u, 0u, 0xffu, 1u, X86_OC_RM16, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, 0, 0u,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FORM(CTOOL_X86_MN_STR, X86_MODE_BOTH, X86_ISA_386, 16u, 0u, 2u,
           0x0fu, 0x00u, 0u, 0xffu, 1u, X86_OC_RM16, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, 0, 1u,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FORM(CTOOL_X86_MN_LTR, X86_MODE_BOTH, X86_ISA_386, 0u, 0u, 2u,
           0x0fu, 0x00u, 0u, 0xffu, 1u, X86_OC_RM16, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, 0, 3u,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FORM(CTOOL_X86_MN_SMSW, X86_MODE_BOTH, X86_ISA_386, 16u, 0u, 2u,
           0x0fu, 0x01u, 0u, 0xffu, 1u, X86_OC_RM16, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, 0, 4u,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FORM(CTOOL_X86_MN_LMSW, X86_MODE_BOTH, X86_ISA_386, 0u, 0u, 2u,
           0x0fu, 0x01u, 0u, 0xffu, 1u, X86_OC_RM16, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, 0, 6u,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FORM(CTOOL_X86_MN_FLD, X86_MODE_BOTH, X86_ISA_X87, 0u, 0u, 1u,
           0xd9u, 0u, 0u, 0xffu, 1u, X86_OC_MEM32, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, 0, 0u,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_MEMORY_ONLY),
  X86_FORM(CTOOL_X86_MN_FSTP, X86_MODE_BOTH, X86_ISA_X87, 0u, 0u, 1u,
           0xd9u, 0u, 0u, 0xffu, 1u, X86_OC_MEM32, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, 0, 3u,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_MEMORY_ONLY),
  X86_X87_MEM(CTOOL_X86_MN_FLD, 0xddu, 0u, X86_OC_MEM64),
  X86_X87_MEM(CTOOL_X86_MN_FSTP, 0xddu, 3u, X86_OC_MEM64),
  X86_X87_ST(CTOOL_X86_MN_FSTP, 0xddu, 0xd8u, 0u),
  X86_X87_ST(CTOOL_X86_MN_FLD, 0xd9u, 0xc0u, 0u),
  X86_X87_MEM(CTOOL_X86_MN_FST, 0xd9u, 2u, X86_OC_MEM32),
  X86_X87_MEM(CTOOL_X86_MN_FADD, 0xd8u, 0u, X86_OC_MEM32),
  X86_X87_ST(CTOOL_X86_MN_FADD, 0xd8u, 0xc0u, 0u),
  X86_X87_ST(CTOOL_X86_MN_FADDP, 0xdeu, 0xc0u, 0u),
  X86_X87_MEM(CTOOL_X86_MN_FMUL, 0xd8u, 1u, X86_OC_MEM32),
  X86_X87_ST(CTOOL_X86_MN_FMUL, 0xd8u, 0xc8u, 0u),
  X86_X87_ST(CTOOL_X86_MN_FMULP, 0xdeu, 0xc8u, 0u),
  X86_X87_MEM(CTOOL_X86_MN_FSUB, 0xd8u, 4u, X86_OC_MEM32),
  X86_X87_ST(CTOOL_X86_MN_FSUB, 0xd8u, 0xe0u, 0u),
  X86_FORM(CTOOL_X86_MN_FSUBR, X86_MODE_BOTH, X86_ISA_X87, 0u, 0u, 2u,
           0xdcu, 0xe0u, 0u, 0xf8u, 2u, X86_OC_X87, X86_OC_X87,
           X86_OC_NONE, 0, X86_NO_OPERAND, X86_NO_OPERAND,
           X86_NO_DIGIT, X86_NO_OPERAND, 0u,
           CTOOL_X86_FIELD_IMMEDIATE, X86_FORM_FIXED_REG1),
  X86_X87_ST(CTOOL_X86_MN_FSUBP, 0xdeu, 0xe8u, 0u),
  X86_X87_MEM(CTOOL_X86_MN_FDIV, 0xd8u, 6u, X86_OC_MEM32),
  X86_X87_ST(CTOOL_X86_MN_FDIV, 0xd8u, 0xf0u, 0u),
  X86_X87_ST(CTOOL_X86_MN_FDIVP, 0xdeu, 0xf8u, 0u),
  X86_X87_MEM(CTOOL_X86_MN_FLDCW, 0xd9u, 5u, X86_OC_MEM16),
  X86_X87_MEM(CTOOL_X86_MN_FNSTCW, 0xd9u, 7u, X86_OC_MEM16),
  X86_X87_MEM(CTOOL_X86_MN_FNSTSW, 0xddu, 7u, X86_OC_MEM16),
  X86_FORM(CTOOL_X86_MN_FNSTSW, X86_MODE_BOTH, X86_ISA_X87, 0u, 0u, 2u,
           0xdfu, 0xe0u, 0u, 0xffu, 1u, X86_OC_GPR16, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND,
           X86_NO_DIGIT, X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_FIXED_REG0),
  X86_X87_ST(CTOOL_X86_MN_FXCH, 0xd9u, 0xc8u, 0u),
  X86_FORM(CTOOL_X86_MN_MOV, X86_MODE_BOTH, X86_ISA_386, 0u, 0u, 1u,
           0x8eu, 0u, 0u, 0xffu, 2u, X86_OC_SEGMENT, X86_OC_RM16,
           X86_OC_NONE, X86_NO_OPERAND, 0, 1, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_SEGMENT_DEST),
  X86_FORM(CTOOL_X86_MN_MOV, X86_MODE_BOTH, X86_ISA_386, 16u, 0u, 1u,
           0x8cu, 0u, 0u, 0xffu, 2u, X86_OC_RM16, X86_OC_SEGMENT,
           X86_OC_NONE, X86_NO_OPERAND, 1, 0, X86_NO_DIGIT,
           X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE, 0u),
  X86_FORM(CTOOL_X86_MN_JMP, X86_MODE_BOTH, X86_ISA_386, 16u, 0u, 1u,
           0xeau, 0u, 0u, 0xffu, 1u, X86_OC_FAR16_16, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND,
           X86_NO_DIGIT, X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_FAR_OFFSET,
           0u),
  X86_FORM(CTOOL_X86_MN_JMP, X86_MODE_BOTH, X86_ISA_386, 32u, 0u, 1u,
           0xeau, 0u, 0u, 0xffu, 1u, X86_OC_FAR16_32, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND,
           X86_NO_DIGIT, X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_FAR_OFFSET,
           0u),
  X86_FORM(CTOOL_X86_MN_PUSH, X86_MODE_BOTH, X86_ISA_8086, 0u, 0u, 1u,
           0x06u, 0u, 0u, 0xffu, 1u, X86_OC_SEGMENT, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND,
           X86_NO_DIGIT, X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_FIXED_SEG_ES),
  X86_FORM(CTOOL_X86_MN_PUSH, X86_MODE_BOTH, X86_ISA_8086, 0u, 0u, 1u,
           0x0eu, 0u, 0u, 0xffu, 1u, X86_OC_SEGMENT, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND,
           X86_NO_DIGIT, X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_FIXED_SEG_CS),
  X86_FORM(CTOOL_X86_MN_PUSH, X86_MODE_BOTH, X86_ISA_8086, 0u, 0u, 1u,
           0x16u, 0u, 0u, 0xffu, 1u, X86_OC_SEGMENT, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND,
           X86_NO_DIGIT, X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_FIXED_SEG_SS),
  X86_FORM(CTOOL_X86_MN_PUSH, X86_MODE_BOTH, X86_ISA_8086, 0u, 0u, 1u,
           0x1eu, 0u, 0u, 0xffu, 1u, X86_OC_SEGMENT, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND,
           X86_NO_DIGIT, X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_FIXED_SEG_DS),
  X86_FORM(CTOOL_X86_MN_PUSH, X86_MODE_BOTH, X86_ISA_386, 0u, 0u, 2u,
           0x0fu, 0xa0u, 0u, 0xffu, 1u, X86_OC_SEGMENT, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND,
           X86_NO_DIGIT, X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_FIXED_SEG_FS),
  X86_FORM(CTOOL_X86_MN_PUSH, X86_MODE_BOTH, X86_ISA_386, 0u, 0u, 2u,
           0x0fu, 0xa8u, 0u, 0xffu, 1u, X86_OC_SEGMENT, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND,
           X86_NO_DIGIT, X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_FIXED_SEG_GS),
  X86_FORM(CTOOL_X86_MN_POP, X86_MODE_BOTH, X86_ISA_8086, 0u, 0u, 1u,
           0x07u, 0u, 0u, 0xffu, 1u, X86_OC_SEGMENT, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND,
           X86_NO_DIGIT, X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_FIXED_SEG_ES),
  X86_FORM(CTOOL_X86_MN_POP, X86_MODE_BOTH, X86_ISA_8086, 0u, 0u, 1u,
           0x17u, 0u, 0u, 0xffu, 1u, X86_OC_SEGMENT, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND,
           X86_NO_DIGIT, X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_FIXED_SEG_SS),
  X86_FORM(CTOOL_X86_MN_POP, X86_MODE_BOTH, X86_ISA_8086, 0u, 0u, 1u,
           0x1fu, 0u, 0u, 0xffu, 1u, X86_OC_SEGMENT, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND,
           X86_NO_DIGIT, X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_FIXED_SEG_DS),
  X86_FORM(CTOOL_X86_MN_POP, X86_MODE_BOTH, X86_ISA_386, 0u, 0u, 2u,
           0x0fu, 0xa1u, 0u, 0xffu, 1u, X86_OC_SEGMENT, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND,
           X86_NO_DIGIT, X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_FIXED_SEG_FS),
  X86_FORM(CTOOL_X86_MN_POP, X86_MODE_BOTH, X86_ISA_386, 0u, 0u, 2u,
           0x0fu, 0xa9u, 0u, 0xffu, 1u, X86_OC_SEGMENT, X86_OC_NONE,
           X86_OC_NONE, X86_NO_OPERAND, X86_NO_OPERAND, X86_NO_OPERAND,
           X86_NO_DIGIT, X86_NO_OPERAND, 0u, CTOOL_X86_FIELD_IMMEDIATE,
           X86_FORM_FIXED_SEG_GS),

  /* Reserved or mode-illegal spellings recognized by this catalogue. */
  X86_INVALID_GROUP(0x0fu, 0x00u, 6u),
  X86_INVALID_FIXED(3u, 0x0fu, 0x01u, 0xf8u)
};

#undef X86_INVALID_GROUP
#undef X86_INVALID_FIXED
#undef X86_SSE_STORE
#undef X86_SSE_RM
#undef X86_ACC_IMM
#undef X86_ALU
#undef X86_RM_IMM
#undef X86_REG_RM
#undef X86_RM_REG
#undef X86_JCC
#undef X86_REL
#undef X86_FIXED
#undef X86_FORM

static ctool_u32 x86_array_count_forms(void) {
  return (ctool_u32)(sizeof(x86_forms) / sizeof(x86_forms[0]));
}

static ctool_u32 x86_array_count_mnemonics(void) {
  return (ctool_u32)(sizeof(x86_mnemonic_names) /
                     sizeof(x86_mnemonic_names[0]));
}

static ctool_u32 x86_array_count_registers(void) {
  return (ctool_u32)(sizeof(x86_register_names) /
                     sizeof(x86_register_names[0]));
}

static ctool_bool x86_ascii_equal(ctool_string_t left, const char *right,
                                  ctool_u8 right_length) {
  ctool_u32 index;
  if (left.data == (const char *)0 || left.size != (ctool_u32)right_length) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < left.size; index++) {
    char a = left.data[index];
    char b = right[index];
    if (a >= 'A' && a <= 'Z') {
      a = (char)(a + ('a' - 'A'));
    }
    if (b >= 'A' && b <= 'Z') {
      b = (char)(b + ('a' - 'A'));
    }
    if (a != b) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_status_t x86_emit_error(ctool_job_t *job, ctool_u32 code,
                                     const char *message,
                                     ctool_status_t status) {
  ctool_diagnostic_t diagnostic;
  ctool_status_t emit_status;
  if (job == (ctool_job_t *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  diagnostic.severity = CTOOL_DIAG_ERROR;
  diagnostic.code = code;
  diagnostic.path = ctool_string("");
  diagnostic.line = 0u;
  diagnostic.column = 0u;
  diagnostic.message = ctool_string(message);
  emit_status = ctool_job_emit(job, &diagnostic);
  return emit_status == CTOOL_OK ? status : emit_status;
}

ctool_status_t ctool_x86_mnemonic_from_name(ctool_string_t name,
                                             ctool_x86_mnemonic_t *out) {
  ctool_u32 index;
  if (out == (ctool_x86_mnemonic_t *)0 ||
      (name.data == (const char *)0 && name.size != 0u)) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  *out = CTOOL_X86_MN_INVALID;
  for (index = 0u; index < x86_array_count_mnemonics(); index++) {
    if (x86_ascii_equal(name, x86_mnemonic_names[index].name,
                        x86_mnemonic_names[index].length) == CTOOL_TRUE) {
      *out = x86_mnemonic_names[index].mnemonic;
      return CTOOL_OK;
    }
  }
  return CTOOL_ERR_NOT_FOUND;
}

ctool_string_t ctool_x86_mnemonic_name(ctool_x86_mnemonic_t mnemonic) {
  ctool_u32 index;
  for (index = 0u; index < x86_array_count_mnemonics(); index++) {
    if (x86_mnemonic_names[index].mnemonic == mnemonic &&
        x86_mnemonic_names[index].canonical == CTOOL_TRUE) {
      ctool_string_t result;
      result.data = x86_mnemonic_names[index].name;
      result.size = (ctool_u32)x86_mnemonic_names[index].length;
      return result;
    }
  }
  return ctool_string("");
}

ctool_status_t ctool_x86_register_from_name(ctool_string_t name,
                                             ctool_x86_reg_t *out) {
  ctool_u32 index;
  if (out == (ctool_x86_reg_t *)0 ||
      (name.data == (const char *)0 && name.size != 0u)) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  out->class_id = CTOOL_X86_REG_NONE;
  out->index = 0u;
  for (index = 0u; index < x86_array_count_registers(); index++) {
    if (x86_ascii_equal(name, x86_register_names[index].name,
                        x86_register_names[index].length) == CTOOL_TRUE) {
      *out = x86_register_names[index].reg;
      return CTOOL_OK;
    }
  }
  return CTOOL_ERR_NOT_FOUND;
}

ctool_string_t ctool_x86_register_name(ctool_x86_reg_t reg_value) {
  ctool_u32 index;
  for (index = 0u; index < x86_array_count_registers(); index++) {
    if (x86_register_names[index].reg.class_id == reg_value.class_id &&
        x86_register_names[index].reg.index == reg_value.index &&
        x86_register_names[index].canonical == CTOOL_TRUE) {
      ctool_string_t result;
      result.data = x86_register_names[index].name;
      result.size = (ctool_u32)x86_register_names[index].length;
      return result;
    }
  }
  return ctool_string("");
}

static ctool_u32 x86_hash_byte(ctool_u32 hash, ctool_u8 value) {
  return (hash ^ (ctool_u32)value) * 16777619u;
}

static ctool_u32 x86_hash_u32(ctool_u32 hash, ctool_u32 value) {
  hash = x86_hash_byte(hash, (ctool_u8)(value & 0xffu));
  hash = x86_hash_byte(hash, (ctool_u8)((value >> 8u) & 0xffu));
  hash = x86_hash_byte(hash, (ctool_u8)((value >> 16u) & 0xffu));
  return x86_hash_byte(hash, (ctool_u8)((value >> 24u) & 0xffu));
}

static ctool_u32 x86_model_fingerprint(void) {
  ctool_u32 hash = 2166136261u;
  ctool_u32 index;
  for (index = 0u; index < x86_array_count_forms(); index++) {
    const x86_form_row_t *row = &x86_forms[index];
    ctool_u32 operand;
    hash = x86_hash_u32(hash, (ctool_u32)row->mnemonic);
    hash = x86_hash_byte(hash, row->modes);
    hash = x86_hash_byte(hash, row->isa);
    hash = x86_hash_u32(hash, (ctool_u32)row->operand_bits);
    hash = x86_hash_byte(hash, row->mandatory_prefix);
    hash = x86_hash_byte(hash, row->opcode_length);
    hash = x86_hash_byte(hash, row->opcode[0]);
    hash = x86_hash_byte(hash, row->opcode[1]);
    hash = x86_hash_byte(hash, row->opcode[2]);
    hash = x86_hash_byte(hash, row->final_opcode_mask);
    hash = x86_hash_byte(hash, row->operand_count);
    for (operand = 0u; operand < CTOOL_X86_MAX_OPERANDS; operand++) {
      hash = x86_hash_byte(hash, row->operand_classes[operand]);
    }
    hash = x86_hash_u32(hash, (ctool_u32)row->opcode_reg_operand);
    hash = x86_hash_u32(hash, (ctool_u32)row->modrm_reg_operand);
    hash = x86_hash_u32(hash, (ctool_u32)row->modrm_rm_operand);
    hash = x86_hash_byte(hash, row->modrm_digit);
    hash = x86_hash_u32(hash, (ctool_u32)row->value_operand);
    hash = x86_hash_byte(hash, row->value_bits);
    hash = x86_hash_u32(hash, (ctool_u32)row->value_kind);
    hash = x86_hash_u32(hash, (ctool_u32)row->flags);
  }
  hash = x86_hash_u32(hash, x86_array_count_mnemonics());
  for (index = 0u; index < x86_array_count_mnemonics(); index++) {
    const x86_mnemonic_name_t *name = &x86_mnemonic_names[index];
    ctool_u32 character;
    hash = x86_hash_byte(hash, name->length);
    for (character = 0u; character < (ctool_u32)name->length; character++) {
      hash = x86_hash_byte(hash, (ctool_u8)name->name[character]);
    }
    hash = x86_hash_u32(hash, (ctool_u32)name->mnemonic);
    hash = x86_hash_byte(hash, (ctool_u8)name->canonical);
  }
  hash = x86_hash_u32(hash, x86_array_count_registers());
  for (index = 0u; index < x86_array_count_registers(); index++) {
    const x86_register_name_t *name = &x86_register_names[index];
    ctool_u32 character;
    hash = x86_hash_byte(hash, name->length);
    for (character = 0u; character < (ctool_u32)name->length; character++) {
      hash = x86_hash_byte(hash, (ctool_u8)name->name[character]);
    }
    hash = x86_hash_u32(hash, (ctool_u32)name->reg.class_id);
    hash = x86_hash_byte(hash, name->reg.index);
    hash = x86_hash_byte(hash, (ctool_u8)name->canonical);
  }
  return hash;
}

ctool_x86_model_info_t ctool_x86_model_info(void) {
  ctool_x86_model_info_t result;
  ctool_u32 index;
  result.form_count = x86_array_count_forms();
  result.mnemonic_count = 0u;
  result.register_count = 0u;
  for (index = 0u; index < x86_array_count_mnemonics(); index++) {
    if (x86_mnemonic_names[index].canonical == CTOOL_TRUE) {
      result.mnemonic_count++;
    }
  }
  for (index = 0u; index < x86_array_count_registers(); index++) {
    if (x86_register_names[index].canonical == CTOOL_TRUE) {
      result.register_count++;
    }
  }
  result.fingerprint = x86_model_fingerprint();
  return result;
}

static ctool_bool x86_role_valid(ctool_i32 role, ctool_u8 operand_count) {
  return role == X86_NO_OPERAND ||
                 (role >= 0 && (ctool_u32)role < (ctool_u32)operand_count)
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool x86_rows_same_decode_key(const x86_form_row_t *left,
                                           const x86_form_row_t *right) {
  ctool_u32 index;
  if (left->modes != right->modes ||
      left->operand_bits != right->operand_bits ||
      left->mandatory_prefix != right->mandatory_prefix ||
      left->opcode_length != right->opcode_length ||
      left->final_opcode_mask != right->final_opcode_mask ||
      left->operand_count != right->operand_count ||
      left->opcode_reg_operand != right->opcode_reg_operand ||
      left->modrm_reg_operand != right->modrm_reg_operand ||
      left->modrm_rm_operand != right->modrm_rm_operand ||
      left->modrm_digit != right->modrm_digit ||
      left->value_operand != right->value_operand ||
      left->value_bits != right->value_bits) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < 3u; index++) {
    if (left->opcode[index] != right->opcode[index] ||
        left->operand_classes[index] != right->operand_classes[index]) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

ctool_status_t ctool_x86_validate_model(ctool_job_t *job) {
  ctool_u32 index;
  const ctool_u32 known_flags =
      X86_FORM_LOCKABLE | X86_FORM_MEMORY_ONLY | X86_FORM_REP_ALLOWED |
      X86_FORM_SIGN_EXTENDED | X86_FORM_FIXED_REG0 |
      X86_FORM_FIXED_REG1 | X86_FORM_FIXED_DX0 | X86_FORM_FIXED_DX1 |
      X86_FORM_FIXED_SEG_ES | X86_FORM_FIXED_SEG_CS |
      X86_FORM_FIXED_SEG_SS | X86_FORM_FIXED_SEG_DS |
      X86_FORM_FIXED_SEG_FS | X86_FORM_FIXED_SEG_GS |
      X86_FORM_REGISTER_ONLY | X86_FORM_SEGMENT_DEST |
      X86_FORM_DECODE_ALIAS | X86_FORM_MOFFS | X86_FORM_FIXED_CL1 |
      X86_FORM_INVALID_ENCODING;
  if (job == (ctool_job_t *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  if (x86_array_count_forms() < ctool_x86_model_info().mnemonic_count ||
      ctool_x86_model_info().mnemonic_count !=
          (ctool_u32)CTOOL_X86_MN_COUNT - 1u ||
      ctool_x86_model_info().register_count != 64u) {
    return x86_emit_error(job, CTOOL_X86_DIAG_INVALID_MODEL,
                          "x86 model inventory is incomplete",
                          CTOOL_ERR_INTERNAL);
  }
  for (index = 0u; index < x86_array_count_forms(); index++) {
    const x86_form_row_t *row = &x86_forms[index];
    ctool_bool invalid_encoding =
        (row->flags & X86_FORM_INVALID_ENCODING) != 0u ? CTOOL_TRUE
                                                       : CTOOL_FALSE;
    ctool_u32 operand;
    if ((invalid_encoding == CTOOL_TRUE &&
         (row->mnemonic != CTOOL_X86_MN_INVALID ||
          row->flags != X86_FORM_INVALID_ENCODING)) ||
        (invalid_encoding == CTOOL_FALSE &&
         row->mnemonic <= CTOOL_X86_MN_INVALID) ||
        row->mnemonic >= CTOOL_X86_MN_COUNT || row->modes == 0u ||
        (row->modes & (ctool_u8)~X86_MODE_BOTH) != 0u ||
        row->isa > X86_ISA_LATER ||
        (row->operand_bits != 0u && row->operand_bits != 8u &&
         row->operand_bits != 16u && row->operand_bits != 32u) ||
        (row->mandatory_prefix != 0u && row->mandatory_prefix != 0x66u &&
         row->mandatory_prefix != 0xf2u &&
         row->mandatory_prefix != 0xf3u) ||
        row->opcode_length == 0u || row->opcode_length > 3u ||
        (row->final_opcode_mask != 0xffu &&
         row->final_opcode_mask != 0xf8u) ||
        row->operand_count > CTOOL_X86_MAX_OPERANDS ||
        x86_role_valid(row->opcode_reg_operand, row->operand_count) ==
            CTOOL_FALSE ||
        x86_role_valid(row->modrm_reg_operand, row->operand_count) ==
            CTOOL_FALSE ||
        x86_role_valid(row->modrm_rm_operand, row->operand_count) ==
            CTOOL_FALSE ||
        x86_role_valid(row->value_operand, row->operand_count) ==
            CTOOL_FALSE ||
        (row->modrm_digit > 7u && row->modrm_digit != X86_NO_DIGIT) ||
        (row->value_bits != 0u && row->value_bits != 8u &&
         row->value_bits != 16u && row->value_bits != 32u) ||
        (row->flags & ~known_flags) != 0u ||
        (row->opcode_reg_operand >= 0 &&
         row->final_opcode_mask != 0xf8u) ||
        (row->opcode_reg_operand < 0 &&
         row->final_opcode_mask != 0xffu) ||
        (row->modrm_rm_operand < 0 &&
         (row->modrm_reg_operand >= 0 || row->modrm_digit != X86_NO_DIGIT)) ||
        (row->modrm_rm_operand >= 0 && row->modrm_reg_operand >= 0 &&
         row->modrm_digit != X86_NO_DIGIT) ||
        (row->value_operand < 0 && row->value_bits != 0u) ||
        (row->value_operand >= 0 && row->value_bits == 0u) ||
        ((row->flags & X86_FORM_MOFFS) != 0u &&
         row->modrm_rm_operand >= 0)) {
      return x86_emit_error(job, CTOOL_X86_DIAG_INVALID_MODEL,
                            "x86 form catalogue invariant failed",
                            CTOOL_ERR_INTERNAL);
    }
    for (operand = 0u; operand < CTOOL_X86_MAX_OPERANDS; operand++) {
      if ((operand < (ctool_u32)row->operand_count &&
           row->operand_classes[operand] == X86_OC_NONE) ||
          (operand >= (ctool_u32)row->operand_count &&
           row->operand_classes[operand] != X86_OC_NONE) ||
          row->operand_classes[operand] > X86_OC_FAR16_32) {
        return x86_emit_error(job, CTOOL_X86_DIAG_INVALID_MODEL,
                              "x86 operand catalogue invariant failed",
                              CTOOL_ERR_INTERNAL);
      }
    }
    for (operand = 0u; operand < index; operand++) {
      const x86_form_row_t *earlier = &x86_forms[operand];
      if (row->mnemonic != earlier->mnemonic &&
          x86_rows_same_decode_key(row, earlier) == CTOOL_TRUE &&
          (row->flags & X86_FORM_DECODE_ALIAS) == 0u &&
          (earlier->flags & X86_FORM_DECODE_ALIAS) == 0u) {
        return x86_emit_error(job, CTOOL_X86_DIAG_INVALID_MODEL,
                              "x86 decode-key collision is not declared",
                              CTOOL_ERR_INTERNAL);
      }
    }
  }
  for (index = 0u; index < x86_array_count_mnemonics(); index++) {
    ctool_u32 form;
    ctool_bool found = CTOOL_FALSE;
    if (x86_mnemonic_names[index].canonical == CTOOL_FALSE) {
      continue;
    }
    for (form = 0u; form < x86_array_count_forms(); form++) {
      if (x86_forms[form].mnemonic == x86_mnemonic_names[index].mnemonic) {
        found = CTOOL_TRUE;
        break;
      }
    }
    if (found == CTOOL_FALSE) {
      return x86_emit_error(job, CTOOL_X86_DIAG_INVALID_MODEL,
                            "x86 canonical mnemonic has no instruction form",
                            CTOOL_ERR_INTERNAL);
    }
  }
  return CTOOL_OK;
}

static ctool_u8 x86_mode_mask(ctool_x86_mode_t mode) {
  if (mode == CTOOL_X86_MODE_16) {
    return X86_MODE16_MASK;
  }
  if (mode == CTOOL_X86_MODE_32) {
    return X86_MODE32_MASK;
  }
  return 0u;
}

static ctool_bool x86_reg_valid(ctool_x86_reg_t reg_value) {
  switch (reg_value.class_id) {
    case CTOOL_X86_REG_NONE:
      return reg_value.index == 0u ? CTOOL_TRUE : CTOOL_FALSE;
    case CTOOL_X86_REG_GPR8:
    case CTOOL_X86_REG_GPR16:
    case CTOOL_X86_REG_GPR32:
    case CTOOL_X86_REG_CONTROL:
    case CTOOL_X86_REG_DEBUG:
    case CTOOL_X86_REG_X87:
    case CTOOL_X86_REG_MMX:
    case CTOOL_X86_REG_XMM:
      return reg_value.index < 8u ? CTOOL_TRUE : CTOOL_FALSE;
    case CTOOL_X86_REG_SEGMENT:
      return reg_value.index < 6u ? CTOOL_TRUE : CTOOL_FALSE;
    case CTOOL_X86_REG_FLAGS:
    case CTOOL_X86_REG_INSTRUCTION_POINTER:
      return reg_value.index == 0u ? CTOOL_TRUE : CTOOL_FALSE;
  }
  return CTOOL_FALSE;
}

static ctool_bool x86_form_reg_allowed(const x86_form_row_t *row,
                                       ctool_u32 operand_index,
                                       ctool_x86_reg_t reg_value) {
  if (x86_reg_valid(reg_value) == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  if (reg_value.class_id == CTOOL_X86_REG_CONTROL && reg_value.index != 0u &&
      reg_value.index != 2u && reg_value.index != 3u &&
      reg_value.index != 4u) {
    return CTOOL_FALSE;
  }
  if ((row->flags & X86_FORM_SEGMENT_DEST) != 0u && operand_index == 0u &&
      reg_value.class_id == CTOOL_X86_REG_SEGMENT && reg_value.index == 1u) {
    return CTOOL_FALSE;
  }
  return CTOOL_TRUE;
}

static ctool_u16 x86_class_width(x86_operand_class_t class_id) {
  switch (class_id) {
    case X86_OC_GPR8:
    case X86_OC_RM8:
    case X86_OC_MEM8:
    case X86_OC_IMM8:
    case X86_OC_REL8:
      return 8u;
    case X86_OC_GPR16:
    case X86_OC_RM16:
    case X86_OC_MEM16:
    case X86_OC_IMM16:
    case X86_OC_REL16:
    case X86_OC_FAR16_16:
      return 16u;
    case X86_OC_GPR32:
    case X86_OC_RM32:
    case X86_OC_MEM32:
    case X86_OC_MMX_RM32:
    case X86_OC_XMM_RM32:
    case X86_OC_IMM32:
    case X86_OC_REL32:
    case X86_OC_FAR16_32:
      return 32u;
    case X86_OC_MEM64:
    case X86_OC_MMX_RM64:
    case X86_OC_XMM_RM64:
      return 64u;
    case X86_OC_MEM48:
      return 48u;
    case X86_OC_MEM128:
    case X86_OC_XMM_RM128:
      return 128u;
    default:
      return 0u;
  }
}

static ctool_bool x86_memory_class_matches(const ctool_x86_operand_t *operand,
                                           x86_operand_class_t class_id) {
  ctool_u16 expected = x86_class_width(class_id);
  if (operand->kind != CTOOL_X86_OPERAND_MEMORY) {
    return CTOOL_FALSE;
  }
  if (class_id == X86_OC_MEM || expected == 0u) {
    return CTOOL_TRUE;
  }
  return operand->width_bits == expected ? CTOOL_TRUE : CTOOL_FALSE;
}

static ctool_bool x86_operand_matches(const ctool_x86_operand_t *operand,
                                      x86_operand_class_t class_id) {
  if (operand == (const ctool_x86_operand_t *)0) {
    return CTOOL_FALSE;
  }
  switch (class_id) {
    case X86_OC_NONE:
      return operand->kind == CTOOL_X86_OPERAND_NONE ? CTOOL_TRUE
                                                      : CTOOL_FALSE;
    case X86_OC_GPR8:
      return operand->kind == CTOOL_X86_OPERAND_REGISTER &&
                     operand->as.reg.class_id == CTOOL_X86_REG_GPR8 &&
                     x86_reg_valid(operand->as.reg) == CTOOL_TRUE
                 ? CTOOL_TRUE
                 : CTOOL_FALSE;
    case X86_OC_GPR16:
      return operand->kind == CTOOL_X86_OPERAND_REGISTER &&
                     operand->as.reg.class_id == CTOOL_X86_REG_GPR16 &&
                     x86_reg_valid(operand->as.reg) == CTOOL_TRUE
                 ? CTOOL_TRUE
                 : CTOOL_FALSE;
    case X86_OC_GPR32:
      return operand->kind == CTOOL_X86_OPERAND_REGISTER &&
                     operand->as.reg.class_id == CTOOL_X86_REG_GPR32 &&
                     x86_reg_valid(operand->as.reg) == CTOOL_TRUE
                 ? CTOOL_TRUE
                 : CTOOL_FALSE;
    case X86_OC_RM8:
      return x86_operand_matches(operand, X86_OC_GPR8) == CTOOL_TRUE ||
                     x86_memory_class_matches(operand, X86_OC_MEM8) ==
                         CTOOL_TRUE
                 ? CTOOL_TRUE
                 : CTOOL_FALSE;
    case X86_OC_RM16:
      return x86_operand_matches(operand, X86_OC_GPR16) == CTOOL_TRUE ||
                     x86_memory_class_matches(operand, X86_OC_MEM16) ==
                         CTOOL_TRUE
                 ? CTOOL_TRUE
                 : CTOOL_FALSE;
    case X86_OC_RM32:
      return x86_operand_matches(operand, X86_OC_GPR32) == CTOOL_TRUE ||
                     x86_memory_class_matches(operand, X86_OC_MEM32) ==
                         CTOOL_TRUE
                 ? CTOOL_TRUE
                 : CTOOL_FALSE;
    case X86_OC_MEM:
    case X86_OC_MEM8:
    case X86_OC_MEM16:
    case X86_OC_MEM32:
    case X86_OC_MEM48:
    case X86_OC_MEM64:
    case X86_OC_MEM128:
      return x86_memory_class_matches(operand, class_id);
    case X86_OC_SEGMENT:
      return operand->kind == CTOOL_X86_OPERAND_REGISTER &&
                     operand->as.reg.class_id == CTOOL_X86_REG_SEGMENT &&
                     x86_reg_valid(operand->as.reg) == CTOOL_TRUE
                 ? CTOOL_TRUE
                 : CTOOL_FALSE;
    case X86_OC_CONTROL:
      return operand->kind == CTOOL_X86_OPERAND_REGISTER &&
                     operand->as.reg.class_id == CTOOL_X86_REG_CONTROL &&
                     x86_reg_valid(operand->as.reg) == CTOOL_TRUE
                 ? CTOOL_TRUE
                 : CTOOL_FALSE;
    case X86_OC_X87:
      return operand->kind == CTOOL_X86_OPERAND_REGISTER &&
                     operand->as.reg.class_id == CTOOL_X86_REG_X87 &&
                     x86_reg_valid(operand->as.reg) == CTOOL_TRUE
                 ? CTOOL_TRUE
                 : CTOOL_FALSE;
    case X86_OC_MMX:
      return operand->kind == CTOOL_X86_OPERAND_REGISTER &&
                     operand->as.reg.class_id == CTOOL_X86_REG_MMX &&
                     x86_reg_valid(operand->as.reg) == CTOOL_TRUE
                 ? CTOOL_TRUE
                 : CTOOL_FALSE;
    case X86_OC_MMX_RM32:
    case X86_OC_MMX_RM64:
      return x86_operand_matches(operand, X86_OC_MMX) == CTOOL_TRUE ||
                     x86_memory_class_matches(
                         operand, class_id == X86_OC_MMX_RM32
                                      ? X86_OC_MEM32
                                      : X86_OC_MEM64) == CTOOL_TRUE
                 ? CTOOL_TRUE
                 : CTOOL_FALSE;
    case X86_OC_XMM:
      return operand->kind == CTOOL_X86_OPERAND_REGISTER &&
                     operand->as.reg.class_id == CTOOL_X86_REG_XMM &&
                     x86_reg_valid(operand->as.reg) == CTOOL_TRUE
                 ? CTOOL_TRUE
                 : CTOOL_FALSE;
    case X86_OC_XMM_RM32:
    case X86_OC_XMM_RM64:
    case X86_OC_XMM_RM128: {
      x86_operand_class_t memory_class = X86_OC_MEM128;
      if (class_id == X86_OC_XMM_RM32) {
        memory_class = X86_OC_MEM32;
      } else if (class_id == X86_OC_XMM_RM64) {
        memory_class = X86_OC_MEM64;
      }
      return x86_operand_matches(operand, X86_OC_XMM) == CTOOL_TRUE ||
                     x86_memory_class_matches(operand, memory_class) ==
                         CTOOL_TRUE
                 ? CTOOL_TRUE
                 : CTOOL_FALSE;
    }
    case X86_OC_IMM8:
    case X86_OC_IMM16:
    case X86_OC_IMM32:
      return operand->kind == CTOOL_X86_OPERAND_IMMEDIATE &&
                     !(operand->as.value.kind == CTOOL_X86_VALUE_REFERENCE &&
                       operand->encoding_bits == 0u &&
                       x86_class_width(class_id) != 32u) &&
                     (operand->encoding_bits == 0u ||
                      operand->encoding_bits == x86_class_width(class_id))
                 ? CTOOL_TRUE
                 : CTOOL_FALSE;
    case X86_OC_REL8:
    case X86_OC_REL16:
    case X86_OC_REL32:
      return operand->kind == CTOOL_X86_OPERAND_RELATIVE &&
                     !(operand->as.value.kind == CTOOL_X86_VALUE_REFERENCE &&
                       operand->encoding_bits == 0u &&
                       x86_class_width(class_id) != 32u) &&
                     (operand->encoding_bits == 0u ||
                      operand->encoding_bits == x86_class_width(class_id))
                 ? CTOOL_TRUE
                 : CTOOL_FALSE;
    case X86_OC_FAR16_16:
    case X86_OC_FAR16_32:
      return operand->kind == CTOOL_X86_OPERAND_FAR_POINTER &&
                     (operand->encoding_bits == 0u ||
                      operand->encoding_bits == x86_class_width(class_id))
                 ? CTOOL_TRUE
                 : CTOOL_FALSE;
  }
  return CTOOL_FALSE;
}

static ctool_bool x86_form_matches(ctool_x86_mode_t mode,
                                   const ctool_x86_instruction_t *instruction,
                                   const x86_form_row_t *row) {
  ctool_u32 index;
  if ((row->modes & x86_mode_mask(mode)) == 0u ||
      row->mnemonic != instruction->mnemonic ||
      row->operand_count != instruction->operand_count) {
    return CTOOL_FALSE;
  }
  if (row->operand_bits != 0u && instruction->operand_bits != 0u &&
      row->operand_bits != instruction->operand_bits) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < (ctool_u32)row->operand_count; index++) {
    if (x86_operand_matches(&instruction->operands[index],
                            (x86_operand_class_t)row->operand_classes[index]) ==
        CTOOL_FALSE) {
      return CTOOL_FALSE;
    }
    if (instruction->operands[index].kind == CTOOL_X86_OPERAND_REGISTER &&
        x86_form_reg_allowed(row, index,
                             instruction->operands[index].as.reg) ==
            CTOOL_FALSE) {
      return CTOOL_FALSE;
    }
  }
  for (; index < CTOOL_X86_MAX_OPERANDS; index++) {
    if (instruction->operands[index].kind != CTOOL_X86_OPERAND_NONE) {
      return CTOOL_FALSE;
    }
  }
  if ((row->flags & X86_FORM_FIXED_REG0) != 0u &&
      instruction->operands[0].as.reg.index != 0u) {
    return CTOOL_FALSE;
  }
  if ((row->flags & X86_FORM_FIXED_REG1) != 0u &&
      instruction->operands[1].as.reg.index != 0u) {
    return CTOOL_FALSE;
  }
  if ((row->flags & X86_FORM_FIXED_DX0) != 0u &&
      instruction->operands[0].as.reg.index != 2u) {
    return CTOOL_FALSE;
  }
  if ((row->flags & X86_FORM_FIXED_DX1) != 0u &&
      instruction->operands[1].as.reg.index != 2u) {
    return CTOOL_FALSE;
  }
  if ((row->flags & X86_FORM_FIXED_CL1) != 0u &&
      instruction->operands[1].as.reg.index != 1u) {
    return CTOOL_FALSE;
  }
  if ((row->flags & X86_FORM_SEGMENT_DEST) != 0u &&
      instruction->operands[0].as.reg.index == 1u) {
    return CTOOL_FALSE;
  }
  if ((row->flags & X86_FORM_REGISTER_ONLY) != 0u &&
      row->modrm_rm_operand >= 0 &&
      instruction->operands[(ctool_u32)row->modrm_rm_operand].kind !=
          CTOOL_X86_OPERAND_REGISTER) {
    return CTOOL_FALSE;
  }
  if ((row->flags & X86_FORM_FIXED_SEG_ES) != 0u &&
      instruction->operands[0].as.reg.index != 0u) {
    return CTOOL_FALSE;
  }
  if ((row->flags & X86_FORM_FIXED_SEG_CS) != 0u &&
      instruction->operands[0].as.reg.index != 1u) {
    return CTOOL_FALSE;
  }
  if ((row->flags & X86_FORM_FIXED_SEG_SS) != 0u &&
      instruction->operands[0].as.reg.index != 2u) {
    return CTOOL_FALSE;
  }
  if ((row->flags & X86_FORM_FIXED_SEG_DS) != 0u &&
      instruction->operands[0].as.reg.index != 3u) {
    return CTOOL_FALSE;
  }
  if ((row->flags & X86_FORM_FIXED_SEG_FS) != 0u &&
      instruction->operands[0].as.reg.index != 4u) {
    return CTOOL_FALSE;
  }
  if ((row->flags & X86_FORM_FIXED_SEG_GS) != 0u &&
      instruction->operands[0].as.reg.index != 5u) {
    return CTOOL_FALSE;
  }
  return CTOOL_TRUE;
}

static ctool_status_t x86_put_u8(ctool_x86_encoding_t *encoding,
                                 ctool_u8 value) {
  if (encoding->size >= CTOOL_X86_MAX_INSTRUCTION_BYTES) {
    return CTOOL_ERR_LIMIT;
  }
  encoding->bytes[encoding->size] = value;
  encoding->size++;
  return CTOOL_OK;
}

static ctool_status_t x86_put_value(ctool_x86_encoding_t *encoding,
                                    ctool_u32 value, ctool_u8 byte_width) {
  ctool_u8 index;
  for (index = 0u; index < byte_width; index++) {
    ctool_status_t status =
        x86_put_u8(encoding, (ctool_u8)((value >> (8u * index)) & 0xffu));
    if (status != CTOOL_OK) {
      return status;
    }
  }
  return CTOOL_OK;
}

static ctool_status_t x86_add_field(ctool_x86_encoding_t *encoding,
                                    ctool_x86_field_kind_t kind,
                                    ctool_x86_relocation_t relocation,
                                    ctool_u8 operand_index,
                                    ctool_u8 byte_offset,
                                    ctool_u8 byte_width, ctool_u8 pc_bias,
                                    ctool_u32 reference,
                                    ctool_i32 encoded_addend) {
  ctool_x86_field_t *field;
  if (encoding->field_count >= CTOOL_X86_MAX_FIELDS) {
    return CTOOL_ERR_LIMIT;
  }
  field = &encoding->fields[encoding->field_count];
  field->kind = kind;
  field->relocation = relocation;
  field->operand_index = operand_index;
  field->byte_offset = byte_offset;
  field->byte_width = byte_width;
  field->pc_bias = pc_bias;
  field->reference = reference;
  field->encoded_addend = encoded_addend;
  encoding->field_count++;
  return CTOOL_OK;
}

static ctool_u8 x86_segment_prefix(ctool_x86_reg_t segment) {
  static const ctool_u8 prefixes[6] = {0x26u, 0x2eu, 0x36u,
                                       0x3eu, 0x64u, 0x65u};
  if (segment.class_id != CTOOL_X86_REG_SEGMENT || segment.index >= 6u) {
    return 0u;
  }
  return prefixes[segment.index];
}

static ctool_bool x86_fits_signed8(ctool_u32 bits) {
  ctool_i32 value = (ctool_i32)bits;
  return value >= -128 && value <= 127 ? CTOOL_TRUE : CTOOL_FALSE;
}

static ctool_bool x86_fits_signed16(ctool_u32 bits) {
  ctool_i32 value = (ctool_i32)bits;
  return value >= -32768 && value <= 32767 ? CTOOL_TRUE : CTOOL_FALSE;
}

static ctool_status_t x86_validate_memory(const ctool_x86_memory_t *memory,
                                          ctool_u16 address_bits) {
  if (memory == (const ctool_x86_memory_t *)0 ||
      (address_bits != 16u && address_bits != 32u) ||
      (memory->address_bits != 0u && memory->address_bits != address_bits) ||
      x86_reg_valid(memory->segment) == CTOOL_FALSE ||
      x86_reg_valid(memory->base) == CTOOL_FALSE ||
      x86_reg_valid(memory->index) == CTOOL_FALSE ||
      (memory->displacement.kind != CTOOL_X86_VALUE_CONSTANT &&
       memory->displacement.kind != CTOOL_X86_VALUE_REFERENCE) ||
      (memory->displacement_bits != 0u && memory->displacement_bits != 8u &&
       memory->displacement_bits != 16u && memory->displacement_bits != 32u)) {
    return CTOOL_ERR_INPUT;
  }
  if (memory->segment.class_id != CTOOL_X86_REG_NONE &&
      memory->segment.class_id != CTOOL_X86_REG_SEGMENT) {
    return CTOOL_ERR_INPUT;
  }
  if (address_bits == 32u) {
    if ((memory->base.class_id != CTOOL_X86_REG_NONE &&
         memory->base.class_id != CTOOL_X86_REG_GPR32) ||
        (memory->index.class_id != CTOOL_X86_REG_NONE &&
         memory->index.class_id != CTOOL_X86_REG_GPR32) ||
         memory->index.index == 4u ||
        (memory->index.class_id == CTOOL_X86_REG_NONE &&
         memory->scale != 1u) ||
        (memory->scale != 1u && memory->scale != 2u && memory->scale != 4u &&
         memory->scale != 8u)) {
      return CTOOL_ERR_INPUT;
    }
  } else {
    if ((memory->base.class_id != CTOOL_X86_REG_NONE &&
         memory->base.class_id != CTOOL_X86_REG_GPR16) ||
        (memory->index.class_id != CTOOL_X86_REG_NONE &&
         memory->index.class_id != CTOOL_X86_REG_GPR16) ||
        memory->scale != 1u || memory->displacement_bits == 32u) {
      return CTOOL_ERR_INPUT;
    }
  }
  return CTOOL_OK;
}

static ctool_status_t x86_emit_displacement(
    ctool_x86_encoding_t *encoding, const ctool_x86_memory_t *memory,
    ctool_u8 operand_index, ctool_u8 byte_width) {
  ctool_u8 offset = encoding->size;
  ctool_u32 serialized = memory->displacement.bits;
  ctool_x86_relocation_t relocation = CTOOL_X86_RELOC_NONE;
  ctool_u32 reference = 0u;
  ctool_i32 addend = (ctool_i32)memory->displacement.bits;
  ctool_status_t status;
  if (memory->displacement.kind == CTOOL_X86_VALUE_REFERENCE) {
    relocation = CTOOL_X86_RELOC_ABSOLUTE;
    reference = memory->displacement.reference;
    addend = memory->displacement.addend;
    serialized = (ctool_u32)addend;
  } else if ((byte_width == 1u &&
              x86_fits_signed8(serialized) == CTOOL_FALSE) ||
             (byte_width == 2u &&
              memory->base.class_id == CTOOL_X86_REG_NONE &&
              memory->index.class_id == CTOOL_X86_REG_NONE &&
              serialized > 0xffffu) ||
             (byte_width == 2u &&
              (memory->base.class_id != CTOOL_X86_REG_NONE ||
               memory->index.class_id != CTOOL_X86_REG_NONE) &&
              x86_fits_signed16(serialized) == CTOOL_FALSE)) {
    return CTOOL_ERR_INPUT;
  }
  status = x86_put_value(encoding, serialized, byte_width);
  if (status != CTOOL_OK) {
    return status;
  }
  return x86_add_field(encoding, CTOOL_X86_FIELD_DISPLACEMENT, relocation,
                       operand_index, offset, byte_width, 0u, reference,
                       addend);
}

static ctool_status_t x86_encode_moffs(ctool_x86_encoding_t *encoding,
                                       const ctool_x86_operand_t *operand,
                                       ctool_u8 operand_index,
                                       ctool_u16 address_bits) {
  const ctool_x86_memory_t *memory;
  if (operand == (const ctool_x86_operand_t *)0 ||
      operand->kind != CTOOL_X86_OPERAND_MEMORY) {
    return CTOOL_ERR_INPUT;
  }
  memory = &operand->as.memory;
  if (x86_validate_memory(memory, address_bits) != CTOOL_OK ||
      memory->base.class_id != CTOOL_X86_REG_NONE ||
      memory->index.class_id != CTOOL_X86_REG_NONE || memory->scale != 1u ||
      (memory->displacement_bits != 0u &&
       memory->displacement_bits != address_bits)) {
    return CTOOL_ERR_INPUT;
  }
  return x86_emit_displacement(encoding, memory, operand_index,
                               (ctool_u8)(address_bits / 8u));
}

static ctool_status_t x86_encode_rm32(ctool_x86_encoding_t *encoding,
                                      const ctool_x86_memory_t *memory,
                                      ctool_u8 operand_index,
                                      ctool_u8 reg_field) {
  ctool_bool has_base = memory->base.class_id != CTOOL_X86_REG_NONE;
  ctool_bool has_index = memory->index.class_id != CTOOL_X86_REG_NONE;
  ctool_bool use_sib = has_index == CTOOL_TRUE ||
                       (has_base == CTOOL_TRUE && memory->base.index == 4u);
  ctool_u8 displacement_bytes = 0u;
  ctool_u8 mod = 0u;
  ctool_u8 rm;
  ctool_u32 displacement = memory->displacement.bits;
  ctool_status_t status;
  if (memory->displacement.kind == CTOOL_X86_VALUE_REFERENCE) {
    displacement_bytes = 4u;
  } else if (memory->displacement_bits != 0u) {
    displacement_bytes = (ctool_u8)(memory->displacement_bits / 8u);
  } else if (has_base == CTOOL_FALSE) {
    displacement_bytes = 4u;
  } else if (displacement == 0u && memory->base.index != 5u) {
    displacement_bytes = 0u;
  } else if (x86_fits_signed8(displacement) == CTOOL_TRUE) {
    displacement_bytes = 1u;
  } else {
    displacement_bytes = 4u;
  }
  if (has_base == CTOOL_FALSE) {
    if (displacement_bytes != 4u) {
      return CTOOL_ERR_INPUT;
    }
    mod = 0u;
  } else if (displacement_bytes == 0u) {
    mod = 0u;
  } else if (displacement_bytes == 1u) {
    mod = 1u;
  } else if (displacement_bytes == 4u) {
    mod = 2u;
  } else {
    return CTOOL_ERR_INPUT;
  }
  rm = use_sib == CTOOL_TRUE
           ? 4u
           : (has_base == CTOOL_TRUE ? memory->base.index : 5u);
  status = x86_put_u8(
      encoding,
      (ctool_u8)((ctool_u8)(mod << 6u) | (ctool_u8)(reg_field << 3u) | rm));
  if (status != CTOOL_OK) {
    return status;
  }
  if (use_sib == CTOOL_TRUE) {
    ctool_u8 scale_bits = 0u;
    ctool_u8 index_bits = has_index == CTOOL_TRUE ? memory->index.index : 4u;
    ctool_u8 base_bits = has_base == CTOOL_TRUE ? memory->base.index : 5u;
    if (memory->scale == 2u) {
      scale_bits = 1u;
    } else if (memory->scale == 4u) {
      scale_bits = 2u;
    } else if (memory->scale == 8u) {
      scale_bits = 3u;
    }
    status = x86_put_u8(
        encoding, (ctool_u8)((ctool_u8)(scale_bits << 6u) |
                             (ctool_u8)(index_bits << 3u) | base_bits));
    if (status != CTOOL_OK) {
      return status;
    }
  }
  if (displacement_bytes != 0u) {
    return x86_emit_displacement(encoding, memory, operand_index,
                                 displacement_bytes);
  }
  return CTOOL_OK;
}

static int x86_rm16_code(ctool_x86_reg_t base, ctool_x86_reg_t index) {
  if (base.class_id == CTOOL_X86_REG_NONE &&
      index.class_id == CTOOL_X86_REG_NONE) {
    return 6;
  }
  if (((base.class_id == CTOOL_X86_REG_GPR16 && base.index == 3u &&
        index.class_id == CTOOL_X86_REG_GPR16 && index.index == 6u) ||
       (base.class_id == CTOOL_X86_REG_GPR16 && base.index == 6u &&
        index.class_id == CTOOL_X86_REG_GPR16 && index.index == 3u))) {
    return 0;
  }
  if (((base.class_id == CTOOL_X86_REG_GPR16 && base.index == 3u &&
        index.class_id == CTOOL_X86_REG_GPR16 && index.index == 7u) ||
       (base.class_id == CTOOL_X86_REG_GPR16 && base.index == 7u &&
        index.class_id == CTOOL_X86_REG_GPR16 && index.index == 3u))) {
    return 1;
  }
  if (((base.class_id == CTOOL_X86_REG_GPR16 && base.index == 5u &&
        index.class_id == CTOOL_X86_REG_GPR16 && index.index == 6u) ||
       (base.class_id == CTOOL_X86_REG_GPR16 && base.index == 6u &&
        index.class_id == CTOOL_X86_REG_GPR16 && index.index == 5u))) {
    return 2;
  }
  if (((base.class_id == CTOOL_X86_REG_GPR16 && base.index == 5u &&
        index.class_id == CTOOL_X86_REG_GPR16 && index.index == 7u) ||
       (base.class_id == CTOOL_X86_REG_GPR16 && base.index == 7u &&
        index.class_id == CTOOL_X86_REG_GPR16 && index.index == 5u))) {
    return 3;
  }
  if (index.class_id == CTOOL_X86_REG_NONE &&
      base.class_id == CTOOL_X86_REG_GPR16) {
    if (base.index == 6u) {
      return 4;
    }
    if (base.index == 7u) {
      return 5;
    }
    if (base.index == 5u) {
      return 6;
    }
    if (base.index == 3u) {
      return 7;
    }
  }
  return -1;
}

static ctool_status_t x86_encode_rm16(ctool_x86_encoding_t *encoding,
                                      const ctool_x86_memory_t *memory,
                                      ctool_u8 operand_index,
                                      ctool_u8 reg_field) {
  int rm_code = x86_rm16_code(memory->base, memory->index);
  ctool_bool direct = memory->base.class_id == CTOOL_X86_REG_NONE &&
                      memory->index.class_id == CTOOL_X86_REG_NONE;
  ctool_u8 displacement_bytes = 0u;
  ctool_u8 mod;
  ctool_status_t status;
  if (rm_code < 0) {
    return CTOOL_ERR_INPUT;
  }
  if (memory->displacement.kind == CTOOL_X86_VALUE_REFERENCE) {
    displacement_bytes = 2u;
  } else if (memory->displacement_bits != 0u) {
    displacement_bytes = (ctool_u8)(memory->displacement_bits / 8u);
  } else if (direct == CTOOL_TRUE) {
    displacement_bytes = 2u;
  } else if (memory->displacement.bits == 0u && rm_code != 6) {
    displacement_bytes = 0u;
  } else if (x86_fits_signed8(memory->displacement.bits) == CTOOL_TRUE) {
    displacement_bytes = 1u;
  } else {
    displacement_bytes = 2u;
  }
  if (direct == CTOOL_TRUE) {
    if (displacement_bytes != 2u) {
      return CTOOL_ERR_INPUT;
    }
    mod = 0u;
  } else if (displacement_bytes == 0u) {
    mod = 0u;
  } else if (displacement_bytes == 1u) {
    mod = 1u;
  } else if (displacement_bytes == 2u) {
    mod = 2u;
  } else {
    return CTOOL_ERR_INPUT;
  }
  status = x86_put_u8(
      encoding, (ctool_u8)((ctool_u8)(mod << 6u) |
                           (ctool_u8)(reg_field << 3u) |
                           (ctool_u8)rm_code));
  if (status != CTOOL_OK) {
    return status;
  }
  if (displacement_bytes != 0u) {
    return x86_emit_displacement(encoding, memory, operand_index,
                                 displacement_bytes);
  }
  return CTOOL_OK;
}

static ctool_status_t x86_encode_rm(ctool_x86_encoding_t *encoding,
                                    const ctool_x86_operand_t *operand,
                                    ctool_u8 operand_index,
                                    ctool_u8 reg_field,
                                    ctool_u16 address_bits) {
  if (operand->kind == CTOOL_X86_OPERAND_REGISTER) {
    return x86_put_u8(
        encoding,
        (ctool_u8)(0xc0u | (ctool_u8)(reg_field << 3u) |
                   operand->as.reg.index));
  }
  if (operand->kind != CTOOL_X86_OPERAND_MEMORY) {
    return CTOOL_ERR_INPUT;
  }
  if (x86_validate_memory(&operand->as.memory, address_bits) != CTOOL_OK) {
    return CTOOL_ERR_INPUT;
  }
  return address_bits == 16u
             ? x86_encode_rm16(encoding, &operand->as.memory, operand_index,
                               reg_field)
             : x86_encode_rm32(encoding, &operand->as.memory, operand_index,
                               reg_field);
}

static ctool_status_t x86_encode_value_operand(
    ctool_x86_encoding_t *encoding, const x86_form_row_t *row,
    const ctool_x86_operand_t *operand, ctool_u8 operand_index) {
  ctool_u8 byte_width = (ctool_u8)(row->value_bits / 8u);
  ctool_u8 byte_offset = encoding->size;
  ctool_u32 serialized;
  ctool_i32 encoded_addend;
  ctool_u32 reference = 0u;
  ctool_x86_relocation_t relocation = CTOOL_X86_RELOC_NONE;
  ctool_status_t status;
  if (byte_width == 0u || operand->as.value.kind < CTOOL_X86_VALUE_CONSTANT ||
      operand->as.value.kind > CTOOL_X86_VALUE_REFERENCE) {
    return CTOOL_ERR_INPUT;
  }
  if (operand->as.value.kind == CTOOL_X86_VALUE_REFERENCE) {
    if (byte_width != 4u) {
      return CTOOL_ERR_INPUT;
    }
    reference = operand->as.value.reference;
    encoded_addend = operand->as.value.addend;
    if (row->value_kind == CTOOL_X86_FIELD_RELATIVE) {
      if (encoded_addend < (-2147483647 - 1) + (ctool_i32)byte_width) {
        return CTOOL_ERR_INPUT;
      }
      relocation = CTOOL_X86_RELOC_PC_RELATIVE;
      encoded_addend -= (ctool_i32)byte_width;
    } else {
      relocation = CTOOL_X86_RELOC_ABSOLUTE;
    }
    serialized = (ctool_u32)encoded_addend;
  } else {
    serialized = operand->as.value.bits;
    encoded_addend = (ctool_i32)serialized;
    if (row->value_kind == CTOOL_X86_FIELD_RELATIVE &&
        row->value_bits == 8u &&
        x86_fits_signed8(serialized) == CTOOL_FALSE) {
      return CTOOL_ERR_INPUT;
    }
    if (row->value_kind == CTOOL_X86_FIELD_RELATIVE &&
        row->value_bits == 16u &&
        x86_fits_signed16(serialized) == CTOOL_FALSE) {
      return CTOOL_ERR_INPUT;
    }
    if (row->value_bits == 8u &&
        (row->flags & X86_FORM_SIGN_EXTENDED) != 0u &&
        x86_fits_signed8(serialized) == CTOOL_FALSE) {
      return CTOOL_ERR_INPUT;
    }
    if (row->value_bits == 8u &&
        row->value_kind != CTOOL_X86_FIELD_RELATIVE &&
        (row->flags & X86_FORM_SIGN_EXTENDED) == 0u &&
        serialized > 0xffu &&
        x86_fits_signed8(serialized) == CTOOL_FALSE) {
      return CTOOL_ERR_INPUT;
    }
    if (row->value_bits == 16u &&
        row->value_kind != CTOOL_X86_FIELD_RELATIVE &&
        serialized > 0xffffu &&
        x86_fits_signed16(serialized) == CTOOL_FALSE) {
      return CTOOL_ERR_INPUT;
    }
  }
  status = x86_put_value(encoding, serialized, byte_width);
  if (status != CTOOL_OK) {
    return status;
  }
  return x86_add_field(encoding, row->value_kind, relocation, operand_index,
                       byte_offset, byte_width,
                       row->value_kind == CTOOL_X86_FIELD_RELATIVE
                           ? byte_width
                           : 0u,
                       reference, encoded_addend);
}

static ctool_status_t x86_encode_far_field(
    ctool_x86_encoding_t *encoding, const ctool_x86_value_t *value,
    ctool_x86_field_kind_t kind, ctool_u8 operand_index,
    ctool_u8 byte_width) {
  ctool_u8 byte_offset = encoding->size;
  ctool_u32 serialized;
  ctool_u32 reference = 0u;
  ctool_i32 addend;
  ctool_x86_relocation_t relocation = CTOOL_X86_RELOC_NONE;
  ctool_status_t status;
  if (value->kind == CTOOL_X86_VALUE_REFERENCE) {
    serialized = (ctool_u32)value->addend;
    addend = value->addend;
    reference = value->reference;
    relocation = CTOOL_X86_RELOC_ABSOLUTE;
  } else if (value->kind == CTOOL_X86_VALUE_CONSTANT) {
    serialized = value->bits;
    addend = (ctool_i32)value->bits;
  } else {
    return CTOOL_ERR_INPUT;
  }
  if (byte_width == 2u && serialized > 0xffffu &&
      x86_fits_signed16(serialized) == CTOOL_FALSE) {
    return CTOOL_ERR_INPUT;
  }
  status = x86_put_value(encoding, serialized, byte_width);
  if (status != CTOOL_OK) {
    return status;
  }
  return x86_add_field(encoding, kind, relocation, operand_index, byte_offset,
                       byte_width, 0u, reference, addend);
}

static ctool_status_t x86_encode_far_pointer(
    ctool_x86_encoding_t *encoding, const ctool_x86_operand_t *operand,
    ctool_u8 offset_width) {
  ctool_status_t status = x86_encode_far_field(
      encoding, &operand->as.far_pointer.offset, CTOOL_X86_FIELD_FAR_OFFSET,
      0u, offset_width);
  if (status != CTOOL_OK) {
    return status;
  }
  return x86_encode_far_field(
      encoding, &operand->as.far_pointer.segment,
      CTOOL_X86_FIELD_FAR_SEGMENT, 0u, 2u);
}

static ctool_status_t x86_instruction_address_bits(
    ctool_x86_mode_t mode, const ctool_x86_instruction_t *instruction,
    ctool_u16 *address_bits_out, ctool_i32 *memory_operand_out) {
  ctool_u16 address_bits = instruction->address_bits;
  ctool_i32 memory_operand = X86_NO_OPERAND;
  ctool_u32 index;
  if (address_bits == 0u) {
    address_bits = (ctool_u16)mode;
  }
  if (address_bits != 16u && address_bits != 32u) {
    return CTOOL_ERR_INPUT;
  }
  for (index = 0u; index < (ctool_u32)instruction->operand_count; index++) {
    const ctool_x86_operand_t *operand = &instruction->operands[index];
    if (operand->kind == CTOOL_X86_OPERAND_MEMORY) {
      ctool_u16 memory_bits = operand->as.memory.address_bits;
      if (memory_operand != X86_NO_OPERAND) {
        return CTOOL_ERR_INPUT;
      }
      memory_operand = (ctool_i32)index;
      if (memory_bits != 0u && instruction->address_bits != 0u &&
          memory_bits != instruction->address_bits) {
        return CTOOL_ERR_INPUT;
      }
      if (memory_bits != 0u) {
        address_bits = memory_bits;
      }
    }
  }
  *address_bits_out = address_bits;
  *memory_operand_out = memory_operand;
  return CTOOL_OK;
}

static ctool_status_t x86_encode_form(
    ctool_x86_mode_t mode, const ctool_x86_instruction_t *instruction,
    const x86_form_row_t *row, ctool_x86_form_t form,
    ctool_x86_encoding_t *encoding) {
  ctool_u16 address_bits;
  ctool_i32 memory_operand;
  ctool_u8 reg_field = 0u;
  ctool_u32 opcode_index;
  ctool_status_t status;
  if (x86_instruction_address_bits(mode, instruction, &address_bits,
                                   &memory_operand) != CTOOL_OK) {
    return CTOOL_ERR_INPUT;
  }
  if ((instruction->prefixes &
       (ctool_u8)~(CTOOL_X86_PREFIX_LOCK | CTOOL_X86_PREFIX_REP |
                   CTOOL_X86_PREFIX_REPNE)) != 0u ||
      ((instruction->prefixes & CTOOL_X86_PREFIX_REP) != 0u &&
       (instruction->prefixes & CTOOL_X86_PREFIX_REPNE) != 0u)) {
    return CTOOL_ERR_INPUT;
  }
  if ((instruction->prefixes & CTOOL_X86_PREFIX_LOCK) != 0u) {
    if ((row->flags & X86_FORM_LOCKABLE) == 0u ||
        row->modrm_rm_operand < 0 ||
        instruction->operands[(ctool_u32)row->modrm_rm_operand].kind !=
            CTOOL_X86_OPERAND_MEMORY) {
      return CTOOL_ERR_INPUT;
    }
    status = x86_put_u8(encoding, 0xf0u);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  if ((instruction->prefixes &
       (CTOOL_X86_PREFIX_REP | CTOOL_X86_PREFIX_REPNE)) != 0u) {
    if ((row->flags & X86_FORM_REP_ALLOWED) == 0u ||
        row->mandatory_prefix != 0u) {
      return CTOOL_ERR_INPUT;
    }
    status = x86_put_u8(
        encoding, (instruction->prefixes & CTOOL_X86_PREFIX_REP) != 0u
                      ? 0xf3u
                      : 0xf2u);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  if (memory_operand >= 0) {
    ctool_u8 segment = x86_segment_prefix(
        instruction->operands[(ctool_u32)memory_operand].as.memory.segment);
    if (instruction->operands[(ctool_u32)memory_operand]
            .as.memory.segment.class_id != CTOOL_X86_REG_NONE &&
        segment == 0u) {
      return CTOOL_ERR_INPUT;
    }
    if (segment != 0u) {
      status = x86_put_u8(encoding, segment);
      if (status != CTOOL_OK) {
        return status;
      }
    }
  }
  if ((row->operand_bits == 16u || row->operand_bits == 32u) &&
      row->operand_bits != (ctool_u16)mode) {
    status = x86_put_u8(encoding, 0x66u);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  if (memory_operand >= 0 && address_bits != (ctool_u16)mode) {
    status = x86_put_u8(encoding, 0x67u);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  if (row->mandatory_prefix != 0u) {
    status = x86_put_u8(encoding, row->mandatory_prefix);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  for (opcode_index = 0u; opcode_index < (ctool_u32)row->opcode_length;
       opcode_index++) {
    ctool_u8 opcode = row->opcode[opcode_index];
    if (opcode_index + 1u == (ctool_u32)row->opcode_length &&
        row->opcode_reg_operand >= 0) {
      opcode = (ctool_u8)(opcode +
                          instruction->operands
                              [(ctool_u32)row->opcode_reg_operand]
                                  .as.reg.index);
    }
    status = x86_put_u8(encoding, opcode);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  if ((row->flags & X86_FORM_MOFFS) != 0u) {
    if (memory_operand < 0) {
      return CTOOL_ERR_INPUT;
    }
    status = x86_encode_moffs(
        encoding, &instruction->operands[(ctool_u32)memory_operand],
        (ctool_u8)memory_operand, address_bits);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  if (row->modrm_reg_operand >= 0) {
    reg_field = instruction->operands[(ctool_u32)row->modrm_reg_operand]
                    .as.reg.index;
  } else if (row->modrm_digit != X86_NO_DIGIT) {
    reg_field = row->modrm_digit;
  }
  if (row->modrm_rm_operand >= 0) {
    status = x86_encode_rm(
        encoding,
        &instruction->operands[(ctool_u32)row->modrm_rm_operand],
        (ctool_u8)row->modrm_rm_operand, reg_field, address_bits);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  if (row->value_operand >= 0) {
    status = x86_encode_value_operand(
        encoding, row,
        &instruction->operands[(ctool_u32)row->value_operand],
        (ctool_u8)row->value_operand);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  if (row->operand_count == 1u &&
      (row->operand_classes[0] == X86_OC_FAR16_16 ||
       row->operand_classes[0] == X86_OC_FAR16_32)) {
    status = x86_encode_far_pointer(
        encoding, &instruction->operands[0],
        row->operand_classes[0] == X86_OC_FAR16_16 ? 2u : 4u);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  encoding->form = form;
  return CTOOL_OK;
}

ctool_status_t ctool_x86_encode(ctool_job_t *job, ctool_x86_mode_t mode,
                                 const ctool_x86_instruction_t *instruction,
                                 ctool_x86_form_t requested_form,
                                 ctool_x86_encoding_t *encoding_out) {
  ctool_u32 first;
  ctool_u32 end;
  ctool_u32 index;
  ctool_status_t failure = CTOOL_ERR_INPUT;
  ctool_x86_encoding_t best;
  ctool_bool has_best = CTOOL_FALSE;
  if (encoding_out == (ctool_x86_encoding_t *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  {
    ctool_u8 *bytes = (ctool_u8 *)encoding_out;
    for (index = 0u; index < (ctool_u32)sizeof(*encoding_out); index++) {
      bytes[index] = 0u;
    }
  }
  if (job == (ctool_job_t *)0 || instruction == (const ctool_x86_instruction_t *)0 ||
      x86_mode_mask(mode) == 0u) {
    return job == (ctool_job_t *)0
               ? CTOOL_ERR_INVALID_ARGUMENT
               : x86_emit_error(job, CTOOL_X86_DIAG_INVALID_INSTRUCTION,
                                "x86 encode arguments are invalid",
                                CTOOL_ERR_INVALID_ARGUMENT);
  }
  if (instruction->mnemonic <= CTOOL_X86_MN_INVALID ||
      instruction->mnemonic >= CTOOL_X86_MN_COUNT ||
      instruction->operand_count > CTOOL_X86_MAX_OPERANDS) {
    return x86_emit_error(job, CTOOL_X86_DIAG_INVALID_INSTRUCTION,
                          "x86 instruction is invalid", CTOOL_ERR_INPUT);
  }
  if (requested_form == CTOOL_X86_FORM_AUTO) {
    first = 0u;
    end = x86_array_count_forms();
  } else if (requested_form <= x86_array_count_forms()) {
    first = requested_form - 1u;
    end = first + 1u;
  } else {
    return x86_emit_error(job, CTOOL_X86_DIAG_NO_ENCODING,
                          "x86 form identity is outside this model",
                          CTOOL_ERR_INPUT);
  }
  for (index = first; index < end; index++) {
    ctool_x86_encoding_t candidate;
    ctool_u32 clear_index;
    const x86_form_row_t *row = &x86_forms[index];
    for (clear_index = 0u; clear_index < (ctool_u32)sizeof(candidate);
         clear_index++) {
      ((ctool_u8 *)&candidate)[clear_index] = 0u;
    }
    if (x86_form_matches(mode, instruction, row) == CTOOL_FALSE) {
      continue;
    }
    failure = x86_encode_form(mode, instruction, row, index + 1u, &candidate);
    if (failure == CTOOL_OK) {
      if (requested_form != CTOOL_X86_FORM_AUTO) {
        *encoding_out = candidate;
        return CTOOL_OK;
      }
      if (has_best == CTOOL_FALSE || candidate.size < best.size) {
        best = candidate;
        has_best = CTOOL_TRUE;
      }
    }
  }
  if (has_best == CTOOL_TRUE) {
    *encoding_out = best;
    return CTOOL_OK;
  }
  return x86_emit_error(job, CTOOL_X86_DIAG_NO_ENCODING,
                        "no x86 form matches the instruction", failure);
}

typedef struct {
  ctool_u32 opcode_offset;
  ctool_bool has_66;
  ctool_bool has_67;
  ctool_bool has_lock;
  ctool_bool has_rep;
  ctool_bool has_repne;
  ctool_x86_reg_t segment;
  ctool_bool invalid;
} x86_decoded_prefixes_t;

typedef enum {
  X86_PARSE_OK = 0,
  X86_PARSE_NO_MATCH,
  X86_PARSE_TRUNCATED,
  X86_PARSE_INVALID
} x86_parse_result_t;

static ctool_u32 x86_read_value(const ctool_u8 *data, ctool_u8 byte_width) {
  ctool_u32 value = 0u;
  ctool_u8 index;
  for (index = 0u; index < byte_width; index++) {
    value |= (ctool_u32)data[index] << (8u * index);
  }
  return value;
}

static x86_decoded_prefixes_t x86_decode_prefixes(ctool_bytes_t bytes,
                                                   ctool_u32 start) {
  x86_decoded_prefixes_t result;
  ctool_u32 offset = start;
  result.opcode_offset = start;
  result.has_66 = CTOOL_FALSE;
  result.has_67 = CTOOL_FALSE;
  result.has_lock = CTOOL_FALSE;
  result.has_rep = CTOOL_FALSE;
  result.has_repne = CTOOL_FALSE;
  result.segment.class_id = CTOOL_X86_REG_NONE;
  result.segment.index = 0u;
  result.invalid = CTOOL_FALSE;
  while (offset < bytes.size && offset - start < CTOOL_X86_MAX_INSTRUCTION_BYTES) {
    ctool_u8 byte = bytes.data[offset];
    ctool_bool recognized = CTOOL_TRUE;
    if (byte == 0x66u) {
      if (result.has_66 == CTOOL_TRUE) {
        result.invalid = CTOOL_TRUE;
      }
      result.has_66 = CTOOL_TRUE;
    } else if (byte == 0x67u) {
      if (result.has_67 == CTOOL_TRUE) {
        result.invalid = CTOOL_TRUE;
      }
      result.has_67 = CTOOL_TRUE;
    } else if (byte == 0xf0u) {
      if (result.has_lock == CTOOL_TRUE) {
        result.invalid = CTOOL_TRUE;
      }
      result.has_lock = CTOOL_TRUE;
    } else if (byte == 0xf3u) {
      if (result.has_rep == CTOOL_TRUE || result.has_repne == CTOOL_TRUE) {
        result.invalid = CTOOL_TRUE;
      }
      result.has_rep = CTOOL_TRUE;
    } else if (byte == 0xf2u) {
      if (result.has_rep == CTOOL_TRUE || result.has_repne == CTOOL_TRUE) {
        result.invalid = CTOOL_TRUE;
      }
      result.has_repne = CTOOL_TRUE;
    } else if (byte == 0x26u || byte == 0x2eu || byte == 0x36u ||
               byte == 0x3eu || byte == 0x64u || byte == 0x65u) {
      static const ctool_u8 prefix_bytes[6] = {0x26u, 0x2eu, 0x36u,
                                               0x3eu, 0x64u, 0x65u};
      ctool_u8 index;
      if (result.segment.class_id != CTOOL_X86_REG_NONE) {
        result.invalid = CTOOL_TRUE;
      }
      result.segment.class_id = CTOOL_X86_REG_SEGMENT;
      result.segment.index = 0u;
      for (index = 0u; index < 6u; index++) {
        if (prefix_bytes[index] == byte) {
          result.segment.index = index;
        }
      }
    } else {
      recognized = CTOOL_FALSE;
    }
    if (recognized == CTOOL_FALSE) {
      break;
    }
    offset++;
  }
  if (offset - start >= CTOOL_X86_MAX_INSTRUCTION_BYTES) {
    result.invalid = CTOOL_TRUE;
  }
  result.opcode_offset = offset;
  return result;
}

static ctool_bool x86_prefixes_match(const x86_decoded_prefixes_t *prefixes,
                                     ctool_x86_mode_t mode,
                                     const x86_form_row_t *row) {
  ctool_bool mandatory_66 =
      row->mandatory_prefix == 0x66u ? CTOOL_TRUE : CTOOL_FALSE;
  ctool_bool mandatory_f2 =
      row->mandatory_prefix == 0xf2u ? CTOOL_TRUE : CTOOL_FALSE;
  ctool_bool mandatory_f3 =
      row->mandatory_prefix == 0xf3u ? CTOOL_TRUE : CTOOL_FALSE;
  ctool_bool operand_66 =
      row->operand_bits != 0u && row->operand_bits != 8u &&
              row->operand_bits != (ctool_u16)mode
          ? CTOOL_TRUE
          : CTOOL_FALSE;
  if (prefixes->invalid == CTOOL_TRUE ||
      prefixes->has_66 !=
          (mandatory_66 == CTOOL_TRUE || operand_66 == CTOOL_TRUE
               ? CTOOL_TRUE
               : CTOOL_FALSE)) {
    return CTOOL_FALSE;
  }
  if (mandatory_f2 == CTOOL_TRUE) {
    if (prefixes->has_repne == CTOOL_FALSE ||
        prefixes->has_rep == CTOOL_TRUE) {
      return CTOOL_FALSE;
    }
  } else if (mandatory_f3 == CTOOL_TRUE) {
    if (prefixes->has_rep == CTOOL_FALSE ||
        prefixes->has_repne == CTOOL_TRUE) {
      return CTOOL_FALSE;
    }
  } else if ((prefixes->has_rep == CTOOL_TRUE ||
              prefixes->has_repne == CTOOL_TRUE) &&
             (row->flags & X86_FORM_REP_ALLOWED) == 0u) {
    return CTOOL_FALSE;
  }
  return CTOOL_TRUE;
}

static ctool_x86_reg_t x86_register_for_class(x86_operand_class_t class_id,
                                               ctool_u8 index) {
  ctool_x86_reg_t result;
  result.class_id = CTOOL_X86_REG_NONE;
  result.index = 0u;
  switch (class_id) {
    case X86_OC_GPR8:
    case X86_OC_RM8:
      result.class_id = CTOOL_X86_REG_GPR8;
      break;
    case X86_OC_GPR16:
    case X86_OC_RM16:
      result.class_id = CTOOL_X86_REG_GPR16;
      break;
    case X86_OC_GPR32:
    case X86_OC_RM32:
      result.class_id = CTOOL_X86_REG_GPR32;
      break;
    case X86_OC_SEGMENT:
      result.class_id = CTOOL_X86_REG_SEGMENT;
      break;
    case X86_OC_CONTROL:
      result.class_id = CTOOL_X86_REG_CONTROL;
      break;
    case X86_OC_X87:
      result.class_id = CTOOL_X86_REG_X87;
      break;
    case X86_OC_MMX:
    case X86_OC_MMX_RM32:
    case X86_OC_MMX_RM64:
      result.class_id = CTOOL_X86_REG_MMX;
      break;
    case X86_OC_XMM:
    case X86_OC_XMM_RM32:
    case X86_OC_XMM_RM64:
    case X86_OC_XMM_RM128:
      result.class_id = CTOOL_X86_REG_XMM;
      break;
    default:
      break;
  }
  result.index = index;
  return result;
}

static void x86_set_memory_width(ctool_x86_operand_t *operand,
                                 x86_operand_class_t class_id) {
  operand->width_bits = x86_class_width(class_id);
  if (operand->width_bits == 0u && class_id == X86_OC_MEM) {
    operand->width_bits = 0u;
  }
}

static ctool_bool x86_class_allows_memory(x86_operand_class_t class_id) {
  switch (class_id) {
    case X86_OC_RM8:
    case X86_OC_RM16:
    case X86_OC_RM32:
    case X86_OC_MEM:
    case X86_OC_MEM8:
    case X86_OC_MEM16:
    case X86_OC_MEM32:
    case X86_OC_MEM48:
    case X86_OC_MEM64:
    case X86_OC_MEM128:
    case X86_OC_MMX_RM32:
    case X86_OC_MMX_RM64:
    case X86_OC_XMM_RM32:
    case X86_OC_XMM_RM64:
    case X86_OC_XMM_RM128:
      return CTOOL_TRUE;
    default:
      return CTOOL_FALSE;
  }
}

static ctool_bool x86_class_allows_register(x86_operand_class_t class_id) {
  switch (class_id) {
    case X86_OC_GPR8:
    case X86_OC_GPR16:
    case X86_OC_GPR32:
    case X86_OC_RM8:
    case X86_OC_RM16:
    case X86_OC_RM32:
    case X86_OC_SEGMENT:
    case X86_OC_CONTROL:
    case X86_OC_X87:
    case X86_OC_MMX:
    case X86_OC_MMX_RM32:
    case X86_OC_MMX_RM64:
    case X86_OC_XMM:
    case X86_OC_XMM_RM32:
    case X86_OC_XMM_RM64:
    case X86_OC_XMM_RM128:
      return CTOOL_TRUE;
    default:
      return CTOOL_FALSE;
  }
}

static void x86_decode_fixed_register(const x86_form_row_t *row,
                                      ctool_u32 operand_index,
                                      ctool_u8 register_index,
                                      ctool_x86_decoded_t *decoded) {
  ctool_x86_operand_t *operand =
      &decoded->instruction.operands[operand_index];
  x86_operand_class_t class_id =
      (x86_operand_class_t)row->operand_classes[operand_index];
  operand->kind = CTOOL_X86_OPERAND_REGISTER;
  operand->as.reg = x86_register_for_class(class_id, register_index);
  operand->width_bits = x86_class_width(class_id);
}

static void x86_decode_fixed_operands(const x86_form_row_t *row,
                                      ctool_x86_decoded_t *decoded) {
  if ((row->flags & X86_FORM_FIXED_REG0) != 0u) {
    x86_decode_fixed_register(row, 0u, 0u, decoded);
  }
  if ((row->flags & X86_FORM_FIXED_REG1) != 0u) {
    x86_decode_fixed_register(row, 1u, 0u, decoded);
  }
  if ((row->flags & X86_FORM_FIXED_DX0) != 0u) {
    x86_decode_fixed_register(row, 0u, 2u, decoded);
  }
  if ((row->flags & X86_FORM_FIXED_DX1) != 0u) {
    x86_decode_fixed_register(row, 1u, 2u, decoded);
  }
  if ((row->flags & X86_FORM_FIXED_CL1) != 0u) {
    x86_decode_fixed_register(row, 1u, 1u, decoded);
  }
  if ((row->flags & X86_FORM_FIXED_SEG_ES) != 0u) {
    x86_decode_fixed_register(row, 0u, 0u, decoded);
  } else if ((row->flags & X86_FORM_FIXED_SEG_CS) != 0u) {
    x86_decode_fixed_register(row, 0u, 1u, decoded);
  } else if ((row->flags & X86_FORM_FIXED_SEG_SS) != 0u) {
    x86_decode_fixed_register(row, 0u, 2u, decoded);
  } else if ((row->flags & X86_FORM_FIXED_SEG_DS) != 0u) {
    x86_decode_fixed_register(row, 0u, 3u, decoded);
  } else if ((row->flags & X86_FORM_FIXED_SEG_FS) != 0u) {
    x86_decode_fixed_register(row, 0u, 4u, decoded);
  } else if ((row->flags & X86_FORM_FIXED_SEG_GS) != 0u) {
    x86_decode_fixed_register(row, 0u, 5u, decoded);
  }
}

static x86_parse_result_t x86_decode_moffs(
    ctool_bytes_t bytes, ctool_u32 start, ctool_u32 *offset,
    ctool_u16 address_bits, ctool_x86_reg_t segment,
    const x86_form_row_t *row, ctool_x86_decoded_t *decoded) {
  ctool_u32 operand_index;
  ctool_u8 byte_width = (ctool_u8)(address_bits / 8u);
  ctool_u32 value;
  ctool_u8 field_offset;
  for (operand_index = 0u; operand_index < (ctool_u32)row->operand_count;
       operand_index++) {
    x86_operand_class_t class_id =
        (x86_operand_class_t)row->operand_classes[operand_index];
    if (class_id == X86_OC_MEM8 || class_id == X86_OC_MEM16 ||
        class_id == X86_OC_MEM32) {
      ctool_x86_operand_t *operand =
          &decoded->instruction.operands[operand_index];
      if (*offset > bytes.size || bytes.size - *offset < byte_width) {
        return X86_PARSE_TRUNCATED;
      }
      field_offset = (ctool_u8)(*offset - start);
      value = x86_read_value(bytes.data + *offset, byte_width);
      operand->kind = CTOOL_X86_OPERAND_MEMORY;
      operand->width_bits = x86_class_width(class_id);
      operand->as.memory.address_bits = address_bits;
      operand->as.memory.segment = segment;
      operand->as.memory.base.class_id = CTOOL_X86_REG_NONE;
      operand->as.memory.base.index = 0u;
      operand->as.memory.index.class_id = CTOOL_X86_REG_NONE;
      operand->as.memory.index.index = 0u;
      operand->as.memory.scale = 1u;
      operand->as.memory.displacement.kind = CTOOL_X86_VALUE_CONSTANT;
      operand->as.memory.displacement.bits = value;
      operand->as.memory.displacement.addend = 0;
      operand->as.memory.displacement.reference = 0u;
      operand->as.memory.displacement_bits = address_bits;
      *offset += byte_width;
      if (x86_add_field(&decoded->encoding,
                        CTOOL_X86_FIELD_DISPLACEMENT,
                        CTOOL_X86_RELOC_NONE, (ctool_u8)operand_index,
                        field_offset, byte_width, 0u, 0u,
                        (ctool_i32)value) != CTOOL_OK) {
        return X86_PARSE_INVALID;
      }
      return X86_PARSE_OK;
    }
  }
  return X86_PARSE_INVALID;
}

static x86_parse_result_t x86_decode_rm32(
    ctool_bytes_t bytes, ctool_u32 start, ctool_u32 *offset,
    ctool_u8 mod, ctool_u8 rm, ctool_x86_reg_t segment,
    ctool_x86_operand_t *operand, ctool_x86_encoding_t *encoding,
    ctool_u8 operand_index) {
  ctool_u8 displacement_bytes = 0u;
  ctool_bool has_base = CTOOL_TRUE;
  ctool_u8 base = rm;
  operand->kind = CTOOL_X86_OPERAND_MEMORY;
  operand->as.memory.address_bits = 32u;
  operand->as.memory.segment = segment;
  operand->as.memory.base.class_id = CTOOL_X86_REG_GPR32;
  operand->as.memory.base.index = rm;
  operand->as.memory.index.class_id = CTOOL_X86_REG_NONE;
  operand->as.memory.index.index = 0u;
  operand->as.memory.scale = 1u;
  operand->as.memory.displacement.kind = CTOOL_X86_VALUE_CONSTANT;
  operand->as.memory.displacement.bits = 0u;
  operand->as.memory.displacement.addend = 0;
  operand->as.memory.displacement.reference = 0u;
  operand->as.memory.displacement_bits = 0u;
  if (rm == 4u) {
    ctool_u8 sib;
    ctool_u8 scale;
    ctool_u8 index;
    if (*offset >= bytes.size) {
      return X86_PARSE_TRUNCATED;
    }
    sib = bytes.data[*offset];
    *offset += 1u;
    scale = (ctool_u8)((sib >> 6u) & 3u);
    index = (ctool_u8)((sib >> 3u) & 7u);
    base = (ctool_u8)(sib & 7u);
    operand->as.memory.scale = (ctool_u8)(1u << scale);
    if (index != 4u) {
      operand->as.memory.index.class_id = CTOOL_X86_REG_GPR32;
      operand->as.memory.index.index = index;
    }
    if (mod == 0u && base == 5u) {
      has_base = CTOOL_FALSE;
    }
  } else if (mod == 0u && rm == 5u) {
    has_base = CTOOL_FALSE;
  }
  if (has_base == CTOOL_TRUE) {
    operand->as.memory.base.class_id = CTOOL_X86_REG_GPR32;
    operand->as.memory.base.index = base;
  } else {
    operand->as.memory.base.class_id = CTOOL_X86_REG_NONE;
    operand->as.memory.base.index = 0u;
  }
  if (mod == 1u) {
    displacement_bytes = 1u;
  } else if (mod == 2u || has_base == CTOOL_FALSE) {
    displacement_bytes = 4u;
  }
  if (displacement_bytes != 0u) {
    ctool_u32 value;
    ctool_u8 field_offset;
    if (*offset > bytes.size || bytes.size - *offset < displacement_bytes) {
      return X86_PARSE_TRUNCATED;
    }
    field_offset = (ctool_u8)(*offset - start);
    value = x86_read_value(bytes.data + *offset, displacement_bytes);
    if (displacement_bytes == 1u && (value & 0x80u) != 0u) {
      value |= 0xffffff00u;
    }
    operand->as.memory.displacement.bits = value;
    operand->as.memory.displacement_bits =
        (ctool_u16)(displacement_bytes * 8u);
    *offset += displacement_bytes;
    if (x86_add_field(encoding, CTOOL_X86_FIELD_DISPLACEMENT,
                      CTOOL_X86_RELOC_NONE, operand_index, field_offset,
                      displacement_bytes, 0u, 0u, (ctool_i32)value) !=
        CTOOL_OK) {
      return X86_PARSE_INVALID;
    }
  }
  return X86_PARSE_OK;
}

static x86_parse_result_t x86_decode_rm16(
    ctool_bytes_t bytes, ctool_u32 start, ctool_u32 *offset,
    ctool_u8 mod, ctool_u8 rm, ctool_x86_reg_t segment,
    ctool_x86_operand_t *operand, ctool_x86_encoding_t *encoding,
    ctool_u8 operand_index) {
  static const ctool_u8 bases[8] = {3u, 3u, 5u, 5u, 6u, 7u, 5u, 3u};
  static const ctool_u8 indexes[8] = {6u, 7u, 6u, 7u, 0xffu, 0xffu,
                                      0xffu, 0xffu};
  ctool_u8 displacement_bytes = 0u;
  ctool_bool direct = mod == 0u && rm == 6u ? CTOOL_TRUE : CTOOL_FALSE;
  operand->kind = CTOOL_X86_OPERAND_MEMORY;
  operand->as.memory.address_bits = 16u;
  operand->as.memory.segment = segment;
  operand->as.memory.scale = 1u;
  operand->as.memory.displacement.kind = CTOOL_X86_VALUE_CONSTANT;
  operand->as.memory.displacement.bits = 0u;
  operand->as.memory.displacement.addend = 0;
  operand->as.memory.displacement.reference = 0u;
  operand->as.memory.displacement_bits = 0u;
  if (direct == CTOOL_TRUE) {
    operand->as.memory.base.class_id = CTOOL_X86_REG_NONE;
    operand->as.memory.base.index = 0u;
    operand->as.memory.index.class_id = CTOOL_X86_REG_NONE;
    operand->as.memory.index.index = 0u;
    displacement_bytes = 2u;
  } else {
    operand->as.memory.base.class_id = CTOOL_X86_REG_GPR16;
    operand->as.memory.base.index = bases[rm];
    if (indexes[rm] != 0xffu) {
      operand->as.memory.index.class_id = CTOOL_X86_REG_GPR16;
      operand->as.memory.index.index = indexes[rm];
    } else {
      operand->as.memory.index.class_id = CTOOL_X86_REG_NONE;
      operand->as.memory.index.index = 0u;
    }
    if (mod == 1u) {
      displacement_bytes = 1u;
    } else if (mod == 2u) {
      displacement_bytes = 2u;
    }
  }
  if (displacement_bytes != 0u) {
    ctool_u32 value;
    ctool_u8 field_offset;
    if (*offset > bytes.size || bytes.size - *offset < displacement_bytes) {
      return X86_PARSE_TRUNCATED;
    }
    field_offset = (ctool_u8)(*offset - start);
    value = x86_read_value(bytes.data + *offset, displacement_bytes);
    if ((displacement_bytes == 1u && (value & 0x80u) != 0u) ||
        (displacement_bytes == 2u && direct == CTOOL_FALSE &&
         (value & 0x8000u) != 0u)) {
      value |= displacement_bytes == 2u ? 0xffff0000u : 0xffffff00u;
    }
    operand->as.memory.displacement.bits = value;
    operand->as.memory.displacement_bits =
        (ctool_u16)(displacement_bytes * 8u);
    *offset += displacement_bytes;
    if (x86_add_field(encoding, CTOOL_X86_FIELD_DISPLACEMENT,
                      CTOOL_X86_RELOC_NONE, operand_index, field_offset,
                      displacement_bytes, 0u, 0u, (ctool_i32)value) !=
        CTOOL_OK) {
      return X86_PARSE_INVALID;
    }
  }
  return X86_PARSE_OK;
}

static x86_parse_result_t x86_decode_far_pointer(
    ctool_bytes_t bytes, ctool_u32 start, ctool_u32 *offset,
    ctool_u8 offset_width, ctool_x86_operand_t *operand,
    ctool_x86_encoding_t *encoding) {
  ctool_u8 field_offset;
  ctool_u32 offset_value;
  ctool_u32 segment_value;
  if (*offset > bytes.size || bytes.size - *offset < offset_width + 2u) {
    return X86_PARSE_TRUNCATED;
  }
  field_offset = (ctool_u8)(*offset - start);
  offset_value = x86_read_value(bytes.data + *offset, offset_width);
  operand->kind = CTOOL_X86_OPERAND_FAR_POINTER;
  operand->width_bits = (ctool_u16)(offset_width * 8u);
  operand->encoding_bits = (ctool_u16)(offset_width * 8u);
  operand->as.far_pointer.offset.kind = CTOOL_X86_VALUE_CONSTANT;
  operand->as.far_pointer.offset.bits = offset_value;
  operand->as.far_pointer.offset.addend = 0;
  operand->as.far_pointer.offset.reference = 0u;
  *offset += offset_width;
  if (x86_add_field(encoding, CTOOL_X86_FIELD_FAR_OFFSET,
                    CTOOL_X86_RELOC_NONE, 0u, field_offset, offset_width, 0u,
                    0u, (ctool_i32)offset_value) != CTOOL_OK) {
    return X86_PARSE_INVALID;
  }
  field_offset = (ctool_u8)(*offset - start);
  segment_value = x86_read_value(bytes.data + *offset, 2u);
  operand->as.far_pointer.segment.kind = CTOOL_X86_VALUE_CONSTANT;
  operand->as.far_pointer.segment.bits = segment_value;
  operand->as.far_pointer.segment.addend = 0;
  operand->as.far_pointer.segment.reference = 0u;
  *offset += 2u;
  if (x86_add_field(encoding, CTOOL_X86_FIELD_FAR_SEGMENT,
                    CTOOL_X86_RELOC_NONE, 0u, field_offset, 2u, 0u, 0u,
                    (ctool_i32)segment_value) != CTOOL_OK) {
    return X86_PARSE_INVALID;
  }
  return X86_PARSE_OK;
}

static x86_parse_result_t x86_decode_row(
    ctool_bytes_t bytes, ctool_u32 start, ctool_x86_mode_t mode,
    const x86_decoded_prefixes_t *prefixes, const x86_form_row_t *row,
    ctool_x86_form_t form, ctool_x86_decoded_t *decoded) {
  ctool_u32 offset = prefixes->opcode_offset;
  ctool_u32 opcode_index;
  ctool_u8 opcode_reg = 0u;
  ctool_u8 mod = 0u;
  ctool_u8 reg_field = 0u;
  ctool_u8 rm = 0u;
  ctool_u16 address_bits = prefixes->has_67 == CTOOL_TRUE
                               ? (mode == CTOOL_X86_MODE_16 ? 32u : 16u)
                               : (ctool_u16)mode;
  ctool_u32 operand_index;
  for (opcode_index = 0u; opcode_index < (ctool_u32)row->opcode_length;
       opcode_index++) {
    ctool_u8 expected = row->opcode[opcode_index];
    ctool_u8 actual;
    if (offset >= bytes.size) {
      return X86_PARSE_TRUNCATED;
    }
    actual = bytes.data[offset];
    if (opcode_index + 1u == (ctool_u32)row->opcode_length &&
        row->opcode_reg_operand >= 0) {
      if ((actual & row->final_opcode_mask) != expected) {
        return X86_PARSE_NO_MATCH;
      }
      opcode_reg = (ctool_u8)(actual & (ctool_u8)~row->final_opcode_mask);
    } else if (actual != expected) {
      return X86_PARSE_NO_MATCH;
    }
    offset++;
  }
  if (x86_prefixes_match(prefixes, mode, row) == CTOOL_FALSE) {
    return X86_PARSE_NO_MATCH;
  }
  if (prefixes->has_lock == CTOOL_TRUE &&
      (row->flags & X86_FORM_LOCKABLE) == 0u) {
    return X86_PARSE_INVALID;
  }
  if ((row->flags & X86_FORM_INVALID_ENCODING) != 0u &&
      row->modrm_rm_operand < 0) {
    return X86_PARSE_INVALID;
  }
  decoded->instruction.mnemonic = row->mnemonic;
  decoded->instruction.operand_bits =
      row->operand_bits == 0u ? (ctool_u16)mode : row->operand_bits;
  decoded->instruction.address_bits = address_bits;
  decoded->instruction.prefixes = 0u;
  if (prefixes->has_lock == CTOOL_TRUE) {
    decoded->instruction.prefixes |= CTOOL_X86_PREFIX_LOCK;
  }
  if (row->mandatory_prefix != 0xf3u && prefixes->has_rep == CTOOL_TRUE) {
    decoded->instruction.prefixes |= CTOOL_X86_PREFIX_REP;
  }
  if (row->mandatory_prefix != 0xf2u && prefixes->has_repne == CTOOL_TRUE) {
    decoded->instruction.prefixes |= CTOOL_X86_PREFIX_REPNE;
  }
  decoded->instruction.operand_count = row->operand_count;
  for (operand_index = 0u; operand_index < CTOOL_X86_MAX_OPERANDS;
       operand_index++) {
    decoded->instruction.operands[operand_index].kind =
        CTOOL_X86_OPERAND_NONE;
    decoded->instruction.operands[operand_index].width_bits = 0u;
    decoded->instruction.operands[operand_index].encoding_bits = 0u;
  }
  x86_decode_fixed_operands(row, decoded);
  if ((row->flags & X86_FORM_MOFFS) != 0u) {
    x86_parse_result_t moffs_result =
        x86_decode_moffs(bytes, start, &offset, address_bits,
                         prefixes->segment, row, decoded);
    if (moffs_result != X86_PARSE_OK) {
      return moffs_result;
    }
  }
  if (row->opcode_reg_operand >= 0) {
    ctool_x86_operand_t *operand =
        &decoded->instruction.operands[(ctool_u32)row->opcode_reg_operand];
    operand->kind = CTOOL_X86_OPERAND_REGISTER;
    operand->as.reg = x86_register_for_class(
        (x86_operand_class_t)
            row->operand_classes[(ctool_u32)row->opcode_reg_operand],
        opcode_reg);
    operand->width_bits = x86_class_width(
        (x86_operand_class_t)
            row->operand_classes[(ctool_u32)row->opcode_reg_operand]);
    if (x86_form_reg_allowed(row, (ctool_u32)row->opcode_reg_operand,
                             operand->as.reg) == CTOOL_FALSE) {
      return X86_PARSE_INVALID;
    }
  }
  if (row->modrm_rm_operand >= 0) {
    ctool_x86_operand_t *rm_operand;
    x86_operand_class_t rm_class =
        (x86_operand_class_t)
            row->operand_classes[(ctool_u32)row->modrm_rm_operand];
    x86_parse_result_t parse_result;
    ctool_u8 modrm;
    if (offset >= bytes.size) {
      return X86_PARSE_TRUNCATED;
    }
    modrm = bytes.data[offset];
    offset++;
    mod = (ctool_u8)((modrm >> 6u) & 3u);
    reg_field = (ctool_u8)((modrm >> 3u) & 7u);
    rm = (ctool_u8)(modrm & 7u);
    if (row->modrm_digit != X86_NO_DIGIT &&
        reg_field != row->modrm_digit) {
      return X86_PARSE_NO_MATCH;
    }
    if ((row->flags & X86_FORM_INVALID_ENCODING) != 0u) {
      return X86_PARSE_INVALID;
    }
    rm_operand =
        &decoded->instruction.operands[(ctool_u32)row->modrm_rm_operand];
    if (mod == 3u) {
      if (x86_class_allows_register(rm_class) == CTOOL_FALSE ||
          (row->flags & X86_FORM_MEMORY_ONLY) != 0u) {
        return X86_PARSE_NO_MATCH;
      }
      rm_operand->kind = CTOOL_X86_OPERAND_REGISTER;
      rm_operand->as.reg = x86_register_for_class(rm_class, rm);
      rm_operand->width_bits = x86_class_width(rm_class);
      if (x86_form_reg_allowed(row, (ctool_u32)row->modrm_rm_operand,
                               rm_operand->as.reg) == CTOOL_FALSE ||
          prefixes->has_lock == CTOOL_TRUE) {
        return X86_PARSE_INVALID;
      }
    } else {
      if (x86_class_allows_memory(rm_class) == CTOOL_FALSE ||
          (row->flags & X86_FORM_REGISTER_ONLY) != 0u) {
        return X86_PARSE_INVALID;
      }
      x86_set_memory_width(rm_operand, rm_class);
      parse_result = address_bits == 16u
                         ? x86_decode_rm16(bytes, start, &offset, mod, rm,
                                           prefixes->segment, rm_operand,
                                           &decoded->encoding,
                                           (ctool_u8)row->modrm_rm_operand)
                         : x86_decode_rm32(bytes, start, &offset, mod, rm,
                                           prefixes->segment, rm_operand,
                                           &decoded->encoding,
                                           (ctool_u8)row->modrm_rm_operand);
      if (parse_result != X86_PARSE_OK) {
        return parse_result;
      }
    }
    if (row->modrm_reg_operand >= 0) {
      ctool_x86_operand_t *reg_operand =
          &decoded->instruction
               .operands[(ctool_u32)row->modrm_reg_operand];
      x86_operand_class_t reg_class =
          (x86_operand_class_t)
              row->operand_classes[(ctool_u32)row->modrm_reg_operand];
      reg_operand->kind = CTOOL_X86_OPERAND_REGISTER;
      reg_operand->as.reg = x86_register_for_class(reg_class, reg_field);
      reg_operand->width_bits = x86_class_width(reg_class);
      if (x86_form_reg_allowed(row, (ctool_u32)row->modrm_reg_operand,
                               reg_operand->as.reg) == CTOOL_FALSE) {
        return X86_PARSE_INVALID;
      }
    }
  }
  if (row->value_operand >= 0) {
    ctool_u8 byte_width = (ctool_u8)(row->value_bits / 8u);
    ctool_x86_operand_t *operand =
        &decoded->instruction.operands[(ctool_u32)row->value_operand];
    ctool_u32 value;
    ctool_u8 field_offset;
    if (offset > bytes.size || bytes.size - offset < byte_width) {
      return X86_PARSE_TRUNCATED;
    }
    field_offset = (ctool_u8)(offset - start);
    value = x86_read_value(bytes.data + offset, byte_width);
    if (byte_width == 1u &&
        ((row->flags & X86_FORM_SIGN_EXTENDED) != 0u ||
         row->value_kind == CTOOL_X86_FIELD_RELATIVE) &&
        (value & 0x80u) != 0u) {
      value |= 0xffffff00u;
    } else if (byte_width == 2u &&
               row->value_kind == CTOOL_X86_FIELD_RELATIVE &&
               (value & 0x8000u) != 0u) {
      value |= 0xffff0000u;
    }
    operand->kind = row->value_kind == CTOOL_X86_FIELD_RELATIVE
                        ? CTOOL_X86_OPERAND_RELATIVE
                        : CTOOL_X86_OPERAND_IMMEDIATE;
    operand->width_bits = row->operand_bits == 0u
                              ? row->value_bits
                              : row->operand_bits;
    operand->encoding_bits = row->value_bits;
    operand->as.value.kind = CTOOL_X86_VALUE_CONSTANT;
    operand->as.value.bits = value;
    operand->as.value.addend = 0;
    operand->as.value.reference = 0u;
    offset += byte_width;
    if (x86_add_field(&decoded->encoding, row->value_kind,
                      CTOOL_X86_RELOC_NONE,
                      (ctool_u8)row->value_operand, field_offset, byte_width,
                      row->value_kind == CTOOL_X86_FIELD_RELATIVE
                          ? byte_width
                          : 0u,
                      0u, (ctool_i32)value) != CTOOL_OK) {
      return X86_PARSE_INVALID;
    }
  }
  if (row->operand_count == 1u &&
      (row->operand_classes[0] == X86_OC_FAR16_16 ||
       row->operand_classes[0] == X86_OC_FAR16_32)) {
    x86_parse_result_t far_result = x86_decode_far_pointer(
        bytes, start, &offset,
        row->operand_classes[0] == X86_OC_FAR16_16 ? 2u : 4u,
        &decoded->instruction.operands[0], &decoded->encoding);
    if (far_result != X86_PARSE_OK) {
      return far_result;
    }
  }
  if (offset - start > CTOOL_X86_MAX_INSTRUCTION_BYTES) {
    return X86_PARSE_INVALID;
  }
  decoded->encoding.form = form;
  decoded->encoding.size = (ctool_u8)(offset - start);
  for (operand_index = 0u;
       operand_index < (ctool_u32)decoded->encoding.size; operand_index++) {
    decoded->encoding.bytes[operand_index] = bytes.data[start + operand_index];
  }
  decoded->consumed = decoded->encoding.size;
  decoded->kind = CTOOL_X86_DECODE_KNOWN;
  return X86_PARSE_OK;
}

static void x86_zero_decoded(ctool_x86_decoded_t *decoded) {
  ctool_u8 *bytes = (ctool_u8 *)decoded;
  ctool_u32 index;
  for (index = 0u; index < (ctool_u32)sizeof(*decoded); index++) {
    bytes[index] = 0u;
  }
}

static void x86_copy_available(ctool_bytes_t bytes, ctool_u32 start,
                               ctool_x86_encoding_t *encoding) {
  ctool_u32 available = bytes.size - start;
  ctool_u32 index;
  if (available > CTOOL_X86_MAX_INSTRUCTION_BYTES) {
    available = CTOOL_X86_MAX_INSTRUCTION_BYTES;
  }
  encoding->size = (ctool_u8)available;
  for (index = 0u; index < available; index++) {
    encoding->bytes[index] = bytes.data[start + index];
  }
}

ctool_status_t ctool_x86_decode(ctool_job_t *job, ctool_x86_mode_t mode,
                                 ctool_bytes_t bytes, ctool_u32 address,
                                 ctool_x86_decoded_t *decoded_out) {
  x86_decoded_prefixes_t prefixes;
  ctool_u32 index;
  ctool_bool saw_truncated = CTOOL_FALSE;
  ctool_bool saw_invalid = CTOOL_FALSE;
  ctool_bool has_best = CTOOL_FALSE;
  ctool_bool best_is_alias = CTOOL_FALSE;
  ctool_x86_decoded_t best;
  if (decoded_out == (ctool_x86_decoded_t *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  x86_zero_decoded(decoded_out);
  if (job == (ctool_job_t *)0 || x86_mode_mask(mode) == 0u ||
      (bytes.data == (const ctool_u8 *)0 && bytes.size != 0u) ||
      address >= bytes.size) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  prefixes = x86_decode_prefixes(bytes, address);
  if (prefixes.invalid == CTOOL_TRUE) {
    decoded_out->kind = CTOOL_X86_DECODE_INVALID;
    decoded_out->consumed = 1u;
    decoded_out->encoding.size = 1u;
    decoded_out->encoding.bytes[0] = bytes.data[address];
    return CTOOL_OK;
  }
  if (prefixes.opcode_offset >= bytes.size) {
    decoded_out->kind = CTOOL_X86_DECODE_TRUNCATED;
    decoded_out->consumed = 0u;
    x86_copy_available(bytes, address, &decoded_out->encoding);
    return CTOOL_OK;
  }
  for (index = 0u; index < x86_array_count_forms(); index++) {
    ctool_x86_decoded_t candidate;
    x86_parse_result_t result;
    if ((x86_forms[index].modes & x86_mode_mask(mode)) == 0u) {
      continue;
    }
    x86_zero_decoded(&candidate);
    result = x86_decode_row(bytes, address, mode, &prefixes,
                            &x86_forms[index], index + 1u, &candidate);
    if (result == X86_PARSE_OK) {
      ctool_bool candidate_is_alias =
          (x86_forms[index].flags & X86_FORM_DECODE_ALIAS) != 0u
              ? CTOOL_TRUE
              : CTOOL_FALSE;
      if (has_best == CTOOL_FALSE || candidate.encoding.size > best.encoding.size ||
          (candidate.encoding.size == best.encoding.size &&
           best_is_alias == CTOOL_TRUE &&
           candidate_is_alias == CTOOL_FALSE)) {
        best = candidate;
        best_is_alias = candidate_is_alias;
        has_best = CTOOL_TRUE;
      }
    }
    if (result == X86_PARSE_TRUNCATED) {
      saw_truncated = CTOOL_TRUE;
    } else if (result == X86_PARSE_INVALID) {
      saw_invalid = CTOOL_TRUE;
    }
  }
  if (has_best == CTOOL_TRUE) {
    *decoded_out = best;
    return CTOOL_OK;
  }
  if (saw_truncated == CTOOL_TRUE) {
    decoded_out->kind = CTOOL_X86_DECODE_TRUNCATED;
    decoded_out->consumed = 0u;
    x86_copy_available(bytes, address, &decoded_out->encoding);
    return CTOOL_OK;
  }
  if (saw_invalid == CTOOL_TRUE) {
    decoded_out->kind = CTOOL_X86_DECODE_INVALID;
    decoded_out->consumed = 1u;
    decoded_out->encoding.size = 1u;
    decoded_out->encoding.bytes[0] = bytes.data[address];
    return CTOOL_OK;
  }
  decoded_out->kind = CTOOL_X86_DECODE_UNKNOWN;
  decoded_out->consumed = 1u;
  decoded_out->encoding.size = 1u;
  decoded_out->encoding.bytes[0] = bytes.data[address];
  return CTOOL_OK;
}
