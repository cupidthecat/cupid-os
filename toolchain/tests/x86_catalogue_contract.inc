#undef ctool_x86_decode

#define ctool_x86_decoder_prepare catalogue_copy_decoder_prepare
#define ctool_x86_mnemonic_from_name catalogue_copy_mnemonic_from_name
#define ctool_x86_mnemonic_name catalogue_copy_mnemonic_name
#define ctool_x86_register_from_name catalogue_copy_register_from_name
#define ctool_x86_register_name catalogue_copy_register_name
#define ctool_x86_model_info catalogue_copy_model_info
#define ctool_x86_validate_model catalogue_copy_validate_model
#define ctool_x86_encode catalogue_copy_encode
#define ctool_x86_decode catalogue_copy_decode
#define ctool_x86_decode_indexed catalogue_copy_decode_indexed

ctool_status_t catalogue_copy_decoder_prepare(
    ctool_job_t *job, const ctool_x86_decoder_t **decoder_out);
ctool_status_t catalogue_copy_mnemonic_from_name(
    ctool_string_t name, ctool_x86_mnemonic_t *out);
ctool_string_t catalogue_copy_mnemonic_name(
    ctool_x86_mnemonic_t mnemonic);
ctool_status_t catalogue_copy_register_from_name(
    ctool_string_t name, ctool_x86_reg_t *out);
ctool_string_t catalogue_copy_register_name(ctool_x86_reg_t reg_value);
ctool_x86_model_info_t catalogue_copy_model_info(void);
ctool_status_t catalogue_copy_validate_model(ctool_job_t *job);
ctool_status_t catalogue_copy_encode(
    ctool_job_t *job, ctool_x86_mode_t mode,
    const ctool_x86_instruction_t *instruction,
    ctool_x86_form_t requested_form, ctool_x86_encoding_t *encoding_out);
ctool_status_t catalogue_copy_decode(
    ctool_job_t *job, ctool_x86_mode_t mode, ctool_bytes_t bytes,
    ctool_u32 address, ctool_x86_decoded_t *decoded_out);
ctool_status_t catalogue_copy_decode_indexed(
    ctool_job_t *job, const ctool_x86_decoder_t *decoder,
    ctool_x86_mode_t mode, ctool_bytes_t bytes, ctool_u32 address,
    ctool_x86_decoded_t *decoded_out);

#include "../x86.cc"

#undef ctool_x86_decoder_prepare
#undef ctool_x86_mnemonic_from_name
#undef ctool_x86_mnemonic_name
#undef ctool_x86_register_from_name
#undef ctool_x86_register_name
#undef ctool_x86_model_info
#undef ctool_x86_validate_model
#undef ctool_x86_encode
#undef ctool_x86_decode
#undef ctool_x86_decode_indexed

#define ctool_x86_decode decode_with_index_equivalence

#define CATALOGUE_FORM_COUNT 604u
#define CATALOGUE_FINGERPRINT 0x55a8970fu
#define CATALOGUE_ENCODABLE_CASES 1202u
#define CATALOGUE_ALIAS_CASES 12u
#define CATALOGUE_ALIAS_SELF_CASES 5u
#define CATALOGUE_INVALID_CASES 4u
#define CATALOGUE_ILLEGAL_MODE_CHECKS 2u
#define CATALOGUE_PREFIX_CHECKS 2641u
#define CATALOGUE_KNOWN_PREFIXES 4u
#define CATALOGUE_WITNESS_HASH 0x8c570035u
#define CATALOGUE_SUPPORTED_FLAGS                                           \
  (X86_FORM_LOCKABLE | X86_FORM_MEMORY_ONLY | X86_FORM_REP_ALLOWED |       \
   X86_FORM_SIGN_EXTENDED | X86_FORM_FIXED_REG0 | X86_FORM_FIXED_REG1 |    \
   X86_FORM_FIXED_DX0 | X86_FORM_FIXED_DX1 | X86_FORM_FIXED_SEG_ES |       \
   X86_FORM_FIXED_SEG_CS | X86_FORM_FIXED_SEG_SS | X86_FORM_FIXED_SEG_DS | \
   X86_FORM_FIXED_SEG_FS | X86_FORM_FIXED_SEG_GS | X86_FORM_REGISTER_ONLY |\
   X86_FORM_SEGMENT_DEST | X86_FORM_DECODE_ALIAS | X86_FORM_MOFFS |        \
   X86_FORM_FIXED_CL1 | X86_FORM_INVALID_ENCODING |                        \
   X86_FORM_DEFAULT_OPERAND_ONLY | X86_FORM_FIXED_CL2)

typedef struct {
  ctool_u32 encodable_cases;
  ctool_u32 alias_cases;
  ctool_u32 alias_self_cases;
  ctool_u32 invalid_cases;
  ctool_u32 illegal_mode_checks;
  ctool_u32 prefix_checks;
  ctool_u32 known_prefixes;
  ctool_u32 seen_flags;
  ctool_u32 witness_hash;
} catalogue_stats_t;

static ctool_u32 catalogue_class_width(x86_operand_class_t class_id) {
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
    case X86_OC_MEM48:
      return 48u;
    case X86_OC_MEM64:
    case X86_OC_MMX_RM64:
    case X86_OC_XMM_RM64:
      return 64u;
    case X86_OC_MEM80:
      return 80u;
    case X86_OC_MEM128:
    case X86_OC_XMM_RM128:
      return 128u;
    case X86_OC_NONE:
    case X86_OC_MEM:
    case X86_OC_SEGMENT:
    case X86_OC_CONTROL:
    case X86_OC_X87:
    case X86_OC_MMX:
    case X86_OC_XMM:
      return 0u;
  }
  return 0xffffffffu;
}

static ctool_bool catalogue_class_allows_memory(
    x86_operand_class_t class_id) {
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
    case X86_OC_MEM80:
    case X86_OC_MEM128:
    case X86_OC_MMX_RM32:
    case X86_OC_MMX_RM64:
    case X86_OC_XMM_RM32:
    case X86_OC_XMM_RM64:
    case X86_OC_XMM_RM128:
      return CTOOL_TRUE;
    case X86_OC_NONE:
    case X86_OC_GPR8:
    case X86_OC_GPR16:
    case X86_OC_GPR32:
    case X86_OC_SEGMENT:
    case X86_OC_CONTROL:
    case X86_OC_X87:
    case X86_OC_MMX:
    case X86_OC_XMM:
    case X86_OC_IMM8:
    case X86_OC_IMM16:
    case X86_OC_IMM32:
    case X86_OC_REL8:
    case X86_OC_REL16:
    case X86_OC_REL32:
    case X86_OC_FAR16_16:
    case X86_OC_FAR16_32:
      return CTOOL_FALSE;
  }
  return CTOOL_FALSE;
}

static ctool_x86_reg_class_t catalogue_register_class(
    x86_operand_class_t class_id) {
  switch (class_id) {
    case X86_OC_GPR8:
    case X86_OC_RM8:
      return CTOOL_X86_REG_GPR8;
    case X86_OC_GPR16:
    case X86_OC_RM16:
      return CTOOL_X86_REG_GPR16;
    case X86_OC_GPR32:
    case X86_OC_RM32:
      return CTOOL_X86_REG_GPR32;
    case X86_OC_SEGMENT:
      return CTOOL_X86_REG_SEGMENT;
    case X86_OC_CONTROL:
      return CTOOL_X86_REG_CONTROL;
    case X86_OC_X87:
      return CTOOL_X86_REG_X87;
    case X86_OC_MMX:
    case X86_OC_MMX_RM32:
    case X86_OC_MMX_RM64:
      return CTOOL_X86_REG_MMX;
    case X86_OC_XMM:
    case X86_OC_XMM_RM32:
    case X86_OC_XMM_RM64:
    case X86_OC_XMM_RM128:
      return CTOOL_X86_REG_XMM;
    default:
      return CTOOL_X86_REG_NONE;
  }
}

static ctool_u8 catalogue_register_index(const x86_form_row_t *row,
                                         ctool_u32 operand_index,
                                         x86_operand_class_t class_id) {
  ctool_u8 index = (ctool_u8)(operand_index + 2u);
  if (class_id == X86_OC_CONTROL) {
    index = 2u;
  } else if (class_id == X86_OC_SEGMENT) {
    index = 3u;
  }
  if (operand_index == 0u) {
    if ((row->flags & X86_FORM_FIXED_REG0) != 0u) {
      index = 0u;
    }
    if ((row->flags & X86_FORM_FIXED_DX0) != 0u) {
      index = 2u;
    }
    if ((row->flags & X86_FORM_FIXED_SEG_ES) != 0u) {
      index = 0u;
    } else if ((row->flags & X86_FORM_FIXED_SEG_CS) != 0u) {
      index = 1u;
    } else if ((row->flags & X86_FORM_FIXED_SEG_SS) != 0u) {
      index = 2u;
    } else if ((row->flags & X86_FORM_FIXED_SEG_DS) != 0u) {
      index = 3u;
    } else if ((row->flags & X86_FORM_FIXED_SEG_FS) != 0u) {
      index = 4u;
    } else if ((row->flags & X86_FORM_FIXED_SEG_GS) != 0u) {
      index = 5u;
    }
  } else if (operand_index == 1u) {
    if ((row->flags & X86_FORM_FIXED_REG1) != 0u) {
      index = 0u;
    }
    if ((row->flags & X86_FORM_FIXED_DX1) != 0u) {
      index = 2u;
    }
    if ((row->flags & X86_FORM_FIXED_CL1) != 0u) {
      index = 1u;
    }
  } else if (operand_index == 2u &&
             (row->flags & X86_FORM_FIXED_CL2) != 0u) {
    index = 1u;
  }
  return index;
}

static int catalogue_build_operand(const x86_form_row_t *row,
                                   ctool_x86_mode_t mode,
                                   ctool_u32 operand_index,
                                   ctool_x86_operand_t *operand) {
  x86_operand_class_t class_id =
      (x86_operand_class_t)row->operand_classes[operand_index];
  ctool_u32 width = catalogue_class_width(class_id);
  ctool_x86_reg_class_t register_class;
  ctool_bool force_memory =
      (row->flags & (X86_FORM_MEMORY_ONLY | X86_FORM_MOFFS)) != 0u &&
              row->modrm_rm_operand == (ctool_i32)operand_index
          ? CTOOL_TRUE
          : CTOOL_FALSE;
  if ((row->flags & X86_FORM_MOFFS) != 0u &&
      catalogue_class_allows_memory(class_id) == CTOOL_TRUE) {
    force_memory = CTOOL_TRUE;
  }
  if (width == 0xffffffffu || class_id == X86_OC_NONE) {
    return 0;
  }
  if (catalogue_class_allows_memory(class_id) == CTOOL_TRUE &&
      (force_memory == CTOOL_TRUE ||
       catalogue_register_class(class_id) == CTOOL_X86_REG_NONE)) {
    *operand = memory_operand(
        (ctool_u16)width, (ctool_u16)mode,
        reg(CTOOL_X86_REG_NONE, 0u), reg(CTOOL_X86_REG_NONE, 0u),
        reg(CTOOL_X86_REG_NONE, 0u), 1u, 0, (ctool_u16)mode);
    return 1;
  }
  register_class = catalogue_register_class(class_id);
  if (register_class != CTOOL_X86_REG_NONE) {
    *operand = register_operand(
        register_class,
        catalogue_register_index(row, operand_index, class_id));
    return 1;
  }
  switch (class_id) {
    case X86_OC_IMM8:
    case X86_OC_IMM16:
    case X86_OC_IMM32:
      *operand = value_operand(CTOOL_X86_OPERAND_IMMEDIATE,
                               (ctool_u16)width, (ctool_u16)width,
                               constant(0u));
      return 1;
    case X86_OC_REL8:
    case X86_OC_REL16:
    case X86_OC_REL32:
      *operand = value_operand(CTOOL_X86_OPERAND_RELATIVE,
                               (ctool_u16)width, (ctool_u16)width,
                               constant(0u));
      return 1;
    case X86_OC_FAR16_16:
    case X86_OC_FAR16_32:
      *operand = far_operand((ctool_u16)width, constant(0u), constant(0u));
      return 1;
    default:
      return 0;
  }
}

static int catalogue_build_instruction(const x86_form_row_t *row,
                                       ctool_x86_mode_t mode,
                                       ctool_x86_instruction_t *result) {
  ctool_u32 operand;
  *result = instruction(row->mnemonic, row->operand_bits,
                        (ctool_u16)mode, 0u);
  result->operand_count = row->operand_count;
  for (operand = 0u; operand < (ctool_u32)row->operand_count; operand++) {
    if (!catalogue_build_operand(row, mode, operand,
                                 &result->operands[operand])) {
      (void)fprintf(stderr,
                    "catalogue form has unsupported operand class: form=%lu mode=%u operand=%lu class=%u\n",
                    (unsigned long)(row - x86_forms + 1), (unsigned)mode,
                    (unsigned long)operand,
                    (unsigned)row->operand_classes[operand]);
      return 0;
    }
  }
  return 1;
}

static ctool_u32 catalogue_hash_byte(ctool_u32 hash, ctool_u8 value) {
  return (hash ^ (ctool_u32)value) * 16777619u;
}

static ctool_u32 catalogue_hash_u32(ctool_u32 hash, ctool_u32 value) {
  hash = catalogue_hash_byte(hash, (ctool_u8)(value & 0xffu));
  hash = catalogue_hash_byte(hash, (ctool_u8)((value >> 8u) & 0xffu));
  hash = catalogue_hash_byte(hash, (ctool_u8)((value >> 16u) & 0xffu));
  return catalogue_hash_byte(hash, (ctool_u8)((value >> 24u) & 0xffu));
}

static int catalogue_decode_pair(
    ctool_job_t *job, const ctool_x86_decoder_t *decoder,
    ctool_x86_mode_t mode, ctool_bytes_t bytes,
    ctool_x86_decoded_t *decoded, ctool_u32 form,
    const char *operation) {
  ctool_x86_decoded_t exhaustive;
  ctool_x86_decoded_t indexed;
  ctool_status_t exhaustive_status =
      exhaustive_decode(job, mode, bytes, 0u, &exhaustive);
  ctool_status_t indexed_status =
      ctool_x86_decode_indexed(job, decoder, mode, bytes, 0u, &indexed);
  if (exhaustive_status != CTOOL_OK || indexed_status != CTOOL_OK ||
      !decoded_equal(&exhaustive, &indexed)) {
    (void)fprintf(stderr,
                  "catalogue decoder mismatch: form=%lu mode=%u check=%s exhaustive=%s kind=%u consumed=%u selected=%lu size=%u mnemonic=%u operands=%u indexed=%s kind=%u consumed=%u selected=%lu size=%u mnemonic=%u operands=%u\n",
                  (unsigned long)form, (unsigned)mode, operation,
                  ctool_status_name(exhaustive_status),
                  (unsigned)exhaustive.kind,
                  (unsigned)exhaustive.consumed,
                  (unsigned long)exhaustive.encoding.form,
                  (unsigned)exhaustive.encoding.size,
                  (unsigned)exhaustive.instruction.mnemonic,
                  (unsigned)exhaustive.instruction.operand_count,
                  ctool_status_name(indexed_status),
                  (unsigned)indexed.kind, (unsigned)indexed.consumed,
                  (unsigned long)indexed.encoding.form,
                  (unsigned)indexed.encoding.size,
                  (unsigned)indexed.instruction.mnemonic,
                  (unsigned)indexed.instruction.operand_count);
    return 0;
  }
  *decoded = exhaustive;
  return 1;
}

static int catalogue_check_prefixes(
    ctool_job_t *job, const ctool_x86_decoder_t *decoder,
    ctool_x86_mode_t mode, const ctool_u8 *bytes, ctool_u8 size,
    ctool_u32 form, catalogue_stats_t *stats) {
  ctool_u8 prefix_size;
  for (prefix_size = 1u; prefix_size < size; prefix_size++) {
    ctool_x86_decoded_t decoded;
    if (!catalogue_decode_pair(
            job, decoder, mode, ctool_bytes(bytes, prefix_size), &decoded,
            form, "proper prefix")) {
      return 0;
    }
    stats->prefix_checks++;
    if (decoded.kind == CTOOL_X86_DECODE_KNOWN) {
      if (decoded.consumed == 0u || decoded.consumed > prefix_size ||
          decoded.encoding.size != decoded.consumed) {
        (void)fprintf(stderr,
                      "catalogue known prefix made no progress: form=%lu mode=%u prefix=%u\n",
                      (unsigned long)form, (unsigned)mode,
                      (unsigned)prefix_size);
        return 0;
      }
      stats->known_prefixes++;
    } else if (decoded.kind != CTOOL_X86_DECODE_TRUNCATED ||
               decoded.consumed != 0u ||
               decoded.encoding.size != prefix_size ||
               memcmp(decoded.encoding.bytes, bytes, prefix_size) != 0) {
      (void)fprintf(stderr,
                    "catalogue proper prefix was not retained as truncated input: form=%lu mode=%u prefix=%u kind=%u\n",
                    (unsigned long)form, (unsigned)mode,
                    (unsigned)prefix_size, (unsigned)decoded.kind);
      return 0;
    }
  }
  return 1;
}

static int catalogue_check_encodable(
    ctool_job_t *job, const ctool_x86_decoder_t *decoder,
    const x86_form_row_t *row, ctool_u32 form, ctool_x86_mode_t mode,
    catalogue_stats_t *stats) {
  ctool_x86_instruction_t source;
  ctool_x86_encoding_t encoded;
  ctool_x86_encoding_t replay;
  ctool_x86_decoded_t decoded;
  ctool_status_t status;
  ctool_u32 byte;
  if (!catalogue_build_instruction(row, mode, &source)) {
    return 0;
  }
  status = ctool_x86_encode(job, mode, &source, form, &encoded);
  if (status != CTOOL_OK || encoded.form != form || encoded.size == 0u) {
    (void)fprintf(stderr,
                  "catalogue form was not reachable: form=%lu mode=%u status=%s size=%u selected=%lu\n",
                  (unsigned long)form, (unsigned)mode,
                  ctool_status_name(status), (unsigned)encoded.size,
                  (unsigned long)encoded.form);
    return 0;
  }
  if (!catalogue_decode_pair(
          job, decoder, mode, ctool_bytes(encoded.bytes, encoded.size),
          &decoded, form, "full witness") ||
      decoded.kind != CTOOL_X86_DECODE_KNOWN ||
      decoded.consumed != encoded.size ||
      decoded.encoding.size != encoded.size ||
      memcmp(decoded.encoding.bytes, encoded.bytes, encoded.size) != 0 ||
      decoded.encoding.form == CTOOL_X86_FORM_AUTO) {
    (void)fprintf(stderr,
                  "catalogue witness did not decode as one known instruction: form=%lu mode=%u kind=%u consumed=%u decoded_form=%lu\n",
                  (unsigned long)form, (unsigned)mode,
                  (unsigned)decoded.kind, (unsigned)decoded.consumed,
                  (unsigned long)decoded.encoding.form);
    return 0;
  }
  if ((row->flags & X86_FORM_DECODE_ALIAS) == 0u &&
      decoded.encoding.form != form) {
    (void)fprintf(stderr,
                  "catalogue canonical form decoded to another identity: form=%lu mode=%u decoded_form=%lu\n",
                  (unsigned long)form, (unsigned)mode,
                  (unsigned long)decoded.encoding.form);
    return 0;
  }
  if ((row->flags & X86_FORM_DECODE_ALIAS) != 0u &&
      decoded.encoding.form == form) {
    stats->alias_self_cases++;
  }
  status = ctool_x86_encode(job, mode, &decoded.instruction,
                            decoded.encoding.form, &replay);
  if (status != CTOOL_OK || replay.form != decoded.encoding.form ||
      replay.size != encoded.size ||
      memcmp(replay.bytes, encoded.bytes, encoded.size) != 0) {
    (void)fprintf(stderr,
                  "catalogue decoded form did not re-encode exactly: form=%lu mode=%u decoded_form=%lu status=%s\n",
                  (unsigned long)form, (unsigned)mode,
                  (unsigned long)decoded.encoding.form,
                  ctool_status_name(status));
    return 0;
  }
  if (!catalogue_check_prefixes(job, decoder, mode, encoded.bytes,
                                encoded.size, form, stats)) {
    return 0;
  }
  stats->encodable_cases++;
  if ((row->flags & X86_FORM_DECODE_ALIAS) != 0u) {
    stats->alias_cases++;
  }
  stats->witness_hash = catalogue_hash_u32(stats->witness_hash, form);
  stats->witness_hash =
      catalogue_hash_byte(stats->witness_hash, (ctool_u8)mode);
  stats->witness_hash =
      catalogue_hash_u32(stats->witness_hash, decoded.encoding.form);
  stats->witness_hash =
      catalogue_hash_byte(stats->witness_hash, encoded.size);
  for (byte = 0u; byte < (ctool_u32)encoded.size; byte++) {
    stats->witness_hash =
        catalogue_hash_byte(stats->witness_hash, encoded.bytes[byte]);
  }
  return 1;
}

static int catalogue_invalid_bytes(const x86_form_row_t *row,
                                   ctool_x86_mode_t mode,
                                   ctool_x86_encoding_t *encoded) {
  ctool_u32 opcode;
  (void)memset(encoded, 0, sizeof(*encoded));
  if ((row->operand_bits == 16u || row->operand_bits == 32u) &&
      row->operand_bits != (ctool_u16)mode) {
    encoded->bytes[encoded->size++] = 0x66u;
  }
  if (row->mandatory_prefix != 0u) {
    encoded->bytes[encoded->size++] = row->mandatory_prefix;
  }
  for (opcode = 0u; opcode < (ctool_u32)row->opcode_length; opcode++) {
    ctool_u8 value = row->opcode[opcode];
    if (opcode + 1u == (ctool_u32)row->opcode_length &&
        row->opcode_reg_operand >= 0) {
      value = (ctool_u8)(value + 2u);
    }
    encoded->bytes[encoded->size++] = value;
  }
  if (row->modrm_rm_operand >= 0) {
    ctool_u8 reg_field = row->modrm_digit == X86_NO_DIGIT
                             ? 2u
                             : row->modrm_digit;
    encoded->bytes[encoded->size++] =
        (ctool_u8)(0xc2u | (ctool_u8)(reg_field << 3u));
  }
  for (opcode = 0u; opcode < (ctool_u32)(row->value_bits / 8u); opcode++) {
    encoded->bytes[encoded->size++] = 0u;
  }
  return encoded->size <= CTOOL_X86_MAX_INSTRUCTION_BYTES;
}

static int catalogue_check_invalid(
    ctool_job_t *job, const ctool_x86_decoder_t *decoder,
    const x86_form_row_t *row, ctool_u32 form, ctool_x86_mode_t mode,
    catalogue_stats_t *stats) {
  ctool_x86_instruction_t source;
  ctool_x86_encoding_t encoded;
  ctool_x86_encoding_t rejected;
  ctool_x86_decoded_t decoded;
  ctool_status_t status;
  ctool_u32 byte;
  if (!catalogue_build_instruction(row, mode, &source) ||
      !catalogue_invalid_bytes(row, mode, &encoded)) {
    return 0;
  }
  status = ctool_x86_encode(job, mode, &source, form, &rejected);
  if (status == CTOOL_OK || !encoding_is_zero(&rejected)) {
    (void)fprintf(stderr,
                  "catalogue invalid row was encodable: form=%lu mode=%u\n",
                  (unsigned long)form, (unsigned)mode);
    return 0;
  }
  if (!catalogue_decode_pair(
          job, decoder, mode, ctool_bytes(encoded.bytes, encoded.size),
          &decoded, form, "invalid witness") ||
      decoded.kind != CTOOL_X86_DECODE_INVALID || decoded.consumed != 1u ||
      decoded.encoding.size != 1u ||
      decoded.encoding.bytes[0] != encoded.bytes[0]) {
    (void)fprintf(stderr,
                  "catalogue invalid row was not rejected by decode: form=%lu mode=%u kind=%u\n",
                  (unsigned long)form, (unsigned)mode,
                  (unsigned)decoded.kind);
    return 0;
  }
  if (!catalogue_check_prefixes(job, decoder, mode, encoded.bytes,
                                encoded.size, form, stats)) {
    return 0;
  }
  stats->invalid_cases++;
  stats->witness_hash = catalogue_hash_u32(stats->witness_hash, form);
  stats->witness_hash =
      catalogue_hash_byte(stats->witness_hash, (ctool_u8)mode);
  stats->witness_hash = catalogue_hash_byte(stats->witness_hash, 0xffu);
  stats->witness_hash =
      catalogue_hash_byte(stats->witness_hash, encoded.size);
  for (byte = 0u; byte < (ctool_u32)encoded.size; byte++) {
    stats->witness_hash =
        catalogue_hash_byte(stats->witness_hash, encoded.bytes[byte]);
  }
  return 1;
}

static int catalogue_check_illegal_mode(ctool_job_t *job,
                                        const x86_form_row_t *row,
                                        ctool_u32 form,
                                        catalogue_stats_t *stats) {
  ctool_x86_mode_t mode = row->modes == X86_MODE16_MASK
                              ? CTOOL_X86_MODE_32
                              : CTOOL_X86_MODE_16;
  ctool_x86_instruction_t source;
  ctool_x86_encoding_t rejected;
  ctool_status_t status;
  if (!catalogue_build_instruction(row, mode, &source)) {
    return 0;
  }
  status = ctool_x86_encode(job, mode, &source, form, &rejected);
  if (status == CTOOL_OK || !encoding_is_zero(&rejected)) {
    (void)fprintf(stderr,
                  "catalogue form reached an illegal mode: form=%lu mode=%u\n",
                  (unsigned long)form, (unsigned)mode);
    return 0;
  }
  stats->illegal_mode_checks++;
  return 1;
}

static int run_catalogue(void) {
  ctool_host_adapter_t adapter;
  ctool_job_t *job;
  const ctool_x86_decoder_t *decoder = (const ctool_x86_decoder_t *)0;
  ctool_x86_model_info_t info = ctool_x86_model_info();
  catalogue_stats_t stats;
  ctool_u32 form_index;
  ctool_u32 private_form_count =
      (ctool_u32)(sizeof(x86_forms) / sizeof(x86_forms[0]));
  (void)memset(&stats, 0, sizeof(stats));
  stats.witness_hash = 2166136261u;
  if (!check_true(info.form_count == CATALOGUE_FORM_COUNT &&
                      info.fingerprint == CATALOGUE_FINGERPRINT &&
                      private_form_count == CATALOGUE_FORM_COUNT,
                  "catalogue fingerprint gate")) {
    return 1;
  }
  if (!open_job(&adapter, &job)) {
    return 1;
  }
  if (!check_status(ctool_x86_validate_model(job), CTOOL_OK,
                    "catalogue model validation") ||
      !check_status(ctool_x86_decoder_prepare(job, &decoder), CTOOL_OK,
                    "catalogue decoder preparation")) {
    ctool_job_close(job);
    return 1;
  }
  for (form_index = 0u; form_index < private_form_count; form_index++) {
    const x86_form_row_t *row = &x86_forms[form_index];
    ctool_x86_form_t form = form_index + 1u;
    ctool_u8 known_mode_bits = row->modes & X86_MODE_BOTH;
    stats.seen_flags |= row->flags;
    if (known_mode_bits != row->modes ||
        (row->flags & ~CATALOGUE_SUPPORTED_FLAGS) != 0u) {
      (void)fprintf(stderr,
                    "catalogue row uses an unsupported mode or flag: form=%lu modes=%u flags=%lu\n",
                    (unsigned long)form, (unsigned)row->modes,
                    (unsigned long)row->flags);
      ctool_job_close(job);
      return 1;
    }
    if ((row->modes & X86_MODE16_MASK) != 0u) {
      if ((row->flags & X86_FORM_INVALID_ENCODING) != 0u
              ? !catalogue_check_invalid(job, decoder, row, form,
                                         CTOOL_X86_MODE_16, &stats)
              : !catalogue_check_encodable(job, decoder, row, form,
                                           CTOOL_X86_MODE_16, &stats)) {
        ctool_job_close(job);
        return 1;
      }
    }
    if ((row->modes & X86_MODE32_MASK) != 0u) {
      if ((row->flags & X86_FORM_INVALID_ENCODING) != 0u
              ? !catalogue_check_invalid(job, decoder, row, form,
                                         CTOOL_X86_MODE_32, &stats)
              : !catalogue_check_encodable(job, decoder, row, form,
                                           CTOOL_X86_MODE_32, &stats)) {
        ctool_job_close(job);
        return 1;
      }
    }
    if (row->modes != X86_MODE_BOTH &&
        (row->flags & X86_FORM_INVALID_ENCODING) == 0u &&
        !catalogue_check_illegal_mode(job, row, form, &stats)) {
      ctool_job_close(job);
      return 1;
    }
  }
  {
    ctool_x86_instruction_t source =
        instruction(CTOOL_X86_MN_NOP, 0u, 32u, 0u);
    ctool_x86_encoding_t rejected;
    if (ctool_x86_encode(job, CTOOL_X86_MODE_32, &source,
                         CATALOGUE_FORM_COUNT + 1u, &rejected) == CTOOL_OK ||
        !encoding_is_zero(&rejected)) {
      (void)fprintf(stderr,
                    "catalogue accepted an identity beyond its fingerprint\n");
      ctool_job_close(job);
      return 1;
    }
  }
  if (stats.seen_flags != CATALOGUE_SUPPORTED_FLAGS ||
      stats.encodable_cases != CATALOGUE_ENCODABLE_CASES ||
      stats.alias_cases != CATALOGUE_ALIAS_CASES ||
      stats.alias_self_cases != CATALOGUE_ALIAS_SELF_CASES ||
      stats.invalid_cases != CATALOGUE_INVALID_CASES ||
      stats.illegal_mode_checks != CATALOGUE_ILLEGAL_MODE_CHECKS ||
      stats.prefix_checks != CATALOGUE_PREFIX_CHECKS ||
      stats.known_prefixes != CATALOGUE_KNOWN_PREFIXES ||
      stats.witness_hash != CATALOGUE_WITNESS_HASH) {
    (void)fprintf(stderr,
                  "catalogue proof did not account for every row category: encodable=%lu aliases=%lu alias_self=%lu invalid=%lu illegal_modes=%lu prefixes=%lu known_prefixes=%lu flags=%08lX hash=%08lX\n",
                  (unsigned long)stats.encodable_cases,
                  (unsigned long)stats.alias_cases,
                  (unsigned long)stats.alias_self_cases,
                  (unsigned long)stats.invalid_cases,
                  (unsigned long)stats.illegal_mode_checks,
                  (unsigned long)stats.prefix_checks,
                  (unsigned long)stats.known_prefixes,
                  (unsigned long)stats.seen_flags,
                  (unsigned long)stats.witness_hash);
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)printf("catalogue: ok\n");
  return 0;
}

#undef CATALOGUE_FORM_COUNT
#undef CATALOGUE_FINGERPRINT
#undef CATALOGUE_ENCODABLE_CASES
#undef CATALOGUE_ALIAS_CASES
#undef CATALOGUE_ALIAS_SELF_CASES
#undef CATALOGUE_INVALID_CASES
#undef CATALOGUE_ILLEGAL_MODE_CHECKS
#undef CATALOGUE_PREFIX_CHECKS
#undef CATALOGUE_KNOWN_PREFIXES
#undef CATALOGUE_WITNESS_HASH
#undef CATALOGUE_SUPPORTED_FLAGS
