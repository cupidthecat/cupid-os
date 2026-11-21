#include "ctool.h"
#include "ctool_host.h"
#include "x86.h"

#include <stdio.h>
#include <string.h>

static int check_status(ctool_status_t actual, ctool_status_t expected, const char *operation) {
    if (actual != expected) {
        (void)fprintf(stderr, "%s: expected %s, got %s\n", operation, ctool_status_name(expected),
                      ctool_status_name(actual));
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

static ctool_x86_operand_t register_operand(ctool_x86_reg_class_t class_id, ctool_u8 index) {
    ctool_x86_operand_t result;
    (void)memset(&result, 0, sizeof(result));
    result.kind = CTOOL_X86_OPERAND_REGISTER;
    result.as.reg = reg(class_id, index);
    return result;
}

static ctool_x86_operand_t value_operand(ctool_x86_operand_kind_t kind, ctool_u16 width_bits,
                                         ctool_u16 encoding_bits, ctool_x86_value_t value) {
    ctool_x86_operand_t result;
    (void)memset(&result, 0, sizeof(result));
    result.kind = kind;
    result.width_bits = width_bits;
    result.encoding_bits = encoding_bits;
    result.as.value = value;
    return result;
}

static ctool_x86_operand_t memory_operand(ctool_u16 width_bits, ctool_u16 address_bits,
                                          ctool_x86_reg_t segment, ctool_x86_reg_t base,
                                          ctool_x86_reg_t index, ctool_u8 scale,
                                          ctool_i32 displacement, ctool_u16 displacement_bits) {
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

static ctool_x86_operand_t far_operand(ctool_u16 offset_bits, ctool_x86_value_t offset,
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

static ctool_x86_instruction_t instruction(ctool_x86_mnemonic_t mnemonic, ctool_u16 operand_bits,
                                           ctool_u16 address_bits, ctool_u8 prefixes) {
    ctool_x86_instruction_t result;
    (void)memset(&result, 0, sizeof(result));
    result.mnemonic = mnemonic;
    result.operand_bits = operand_bits;
    result.address_bits = address_bits;
    result.prefixes = prefixes;
    return result;
}

static int bytes_equal(const ctool_x86_encoding_t *encoding, const ctool_u8 *expected,
                       ctool_u8 size, const char *operation) {
    return check_true(encoding->size == size && memcmp(encoding->bytes, expected, size) == 0,
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

static int decoded_register_equal(const ctool_x86_reg_t *left, const ctool_x86_reg_t *right) {
    return left->class_id == right->class_id && left->index == right->index;
}

static int decoded_value_equal(const ctool_x86_value_t *left, const ctool_x86_value_t *right) {
    return left->kind == right->kind && left->bits == right->bits &&
           left->addend == right->addend && left->reference == right->reference;
}

static int decoded_operand_equal(const ctool_x86_operand_t *left,
                                 const ctool_x86_operand_t *right) {
    if (left->kind != right->kind || left->width_bits != right->width_bits ||
        left->encoding_bits != right->encoding_bits) {
        return 0;
    }
    switch (left->kind) {
        case CTOOL_X86_OPERAND_NONE:
            return 1;
        case CTOOL_X86_OPERAND_REGISTER:
            return decoded_register_equal(&left->as.reg, &right->as.reg);
        case CTOOL_X86_OPERAND_IMMEDIATE:
        case CTOOL_X86_OPERAND_RELATIVE:
            return decoded_value_equal(&left->as.value, &right->as.value);
        case CTOOL_X86_OPERAND_MEMORY:
            return left->as.memory.address_bits == right->as.memory.address_bits &&
                   decoded_register_equal(&left->as.memory.segment, &right->as.memory.segment) &&
                   decoded_register_equal(&left->as.memory.base, &right->as.memory.base) &&
                   decoded_register_equal(&left->as.memory.index, &right->as.memory.index) &&
                   left->as.memory.scale == right->as.memory.scale &&
                   decoded_value_equal(&left->as.memory.displacement,
                                       &right->as.memory.displacement) &&
                   left->as.memory.displacement_bits == right->as.memory.displacement_bits;
        case CTOOL_X86_OPERAND_FAR_POINTER:
            return decoded_value_equal(&left->as.far_pointer.offset,
                                       &right->as.far_pointer.offset) &&
                   decoded_value_equal(&left->as.far_pointer.segment,
                                       &right->as.far_pointer.segment);
    }
    return 0;
}

static int decoded_instruction_equal(const ctool_x86_instruction_t *left,
                                     const ctool_x86_instruction_t *right) {
    ctool_u32 operand;
    if (left->mnemonic != right->mnemonic || left->operand_bits != right->operand_bits ||
        left->address_bits != right->address_bits || left->prefixes != right->prefixes ||
        left->operand_count != right->operand_count ||
        left->operand_count > CTOOL_X86_MAX_OPERANDS) {
        return 0;
    }
    for (operand = 0u; operand < (ctool_u32)left->operand_count; operand++) {
        if (!decoded_operand_equal(&left->operands[operand], &right->operands[operand])) {
            return 0;
        }
    }
    return 1;
}

static int decoded_field_equal(const ctool_x86_field_t *left, const ctool_x86_field_t *right) {
    return left->kind == right->kind && left->relocation == right->relocation &&
           left->operand_index == right->operand_index && left->byte_offset == right->byte_offset &&
           left->byte_width == right->byte_width && left->pc_bias == right->pc_bias &&
           left->reference == right->reference && left->encoded_addend == right->encoded_addend;
}

static int decoded_encoding_equal(const ctool_x86_encoding_t *left,
                                  const ctool_x86_encoding_t *right) {
    ctool_u32 field;
    if (left->size != right->size || left->form != right->form ||
        left->field_count != right->field_count || left->size > CTOOL_X86_MAX_INSTRUCTION_BYTES ||
        left->field_count > CTOOL_X86_MAX_FIELDS ||
        memcmp(left->bytes, right->bytes, left->size) != 0) {
        return 0;
    }
    for (field = 0u; field < (ctool_u32)left->field_count; field++) {
        if (!decoded_field_equal(&left->fields[field], &right->fields[field])) {
            return 0;
        }
    }
    return 1;
}

static int decoded_equal(const ctool_x86_decoded_t *left, const ctool_x86_decoded_t *right) {
    return left->kind == right->kind && left->consumed == right->consumed &&
           decoded_instruction_equal(&left->instruction, &right->instruction) &&
           decoded_encoding_equal(&left->encoding, &right->encoding);
}

static ctool_status_t exhaustive_decode(ctool_job_t *job, ctool_x86_mode_t mode,
                                        ctool_bytes_t bytes, ctool_u32 address,
                                        ctool_x86_decoded_t *decoded_out) {
    return ctool_x86_decode(job, mode, bytes, address, decoded_out);
}

static ctool_status_t decode_with_index_equivalence(ctool_job_t *job, ctool_x86_mode_t mode,
                                                    ctool_bytes_t bytes, ctool_u32 address,
                                                    ctool_x86_decoded_t *decoded_out) {
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
        (void)fprintf(stderr, "x86 decoder preparation failed during equivalence check\n");
        return prepare_status;
    }
    (void)memset(&exhaustive, 0xa5, sizeof(exhaustive));
    (void)memset(&indexed, 0xa5, sizeof(indexed));
    exhaustive_status = exhaustive_decode(
        job, mode, bytes, address,
        decoded_out == (ctool_x86_decoded_t *)0 ? (ctool_x86_decoded_t *)0 : &exhaustive);
    indexed_status = ctool_x86_decode_indexed(
        job, decoder, mode, bytes, address,
        decoded_out == (ctool_x86_decoded_t *)0 ? (ctool_x86_decoded_t *)0 : &indexed);
    if (exhaustive_status != indexed_status ||
        (decoded_out != (ctool_x86_decoded_t *)0 && !decoded_equal(&exhaustive, &indexed))) {
        (void)fprintf(stderr, "prepared x86 decoder diverged from exhaustive decode\n");
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

static int compare_prepared_decode(ctool_job_t *job, const ctool_x86_decoder_t *decoder,
                                   ctool_x86_mode_t mode, ctool_bytes_t bytes, ctool_u32 address,
                                   const char *operation) {
    ctool_x86_decoded_t exhaustive;
    ctool_x86_decoded_t indexed;
    ctool_status_t exhaustive_status = exhaustive_decode(job, mode, bytes, address, &exhaustive);
    ctool_status_t indexed_status =
        ctool_x86_decode_indexed(job, decoder, mode, bytes, address, &indexed);
    if (exhaustive_status != indexed_status || !decoded_equal(&exhaustive, &indexed)) {
        (void)fprintf(stderr, "%s: prepared decode mismatch\n", operation);
        return 0;
    }
    return 1;
}

#include "x86_catalogue_contract.inc"

static int run_decoder_index(void) {
    ctool_host_adapter_t owner_adapter;
    ctool_host_adapter_t caller_adapter;
    ctool_host_adapter_t limited_adapter;
    ctool_job_t *owner_job;
    ctool_job_t *caller_job;
    ctool_job_t *limited_job;
    const ctool_x86_decoder_t *decoder = (const ctool_x86_decoder_t *)(const void *)1;
    ctool_limits_t limited_limits = ctool_default_limits();
    ctool_job_config_t limited_config;
    ctool_x86_decoded_t decoded;
    ctool_u8 bytes[CTOOL_X86_MAX_INSTRUCTION_BYTES];
    void *recovery = (void *)0;
    ctool_status_t status;
    ctool_u32 mode_index;
    ctool_u32 opcode;
    ctool_u32 index;
    static const ctool_x86_mode_t modes[] = {CTOOL_X86_MODE_16, CTOOL_X86_MODE_32};

    {
        ctool_x86_decoded_t zero_padding;
        ctool_x86_decoded_t other_padding;
        (void)memset(&zero_padding, 0, sizeof(zero_padding));
        (void)memset(&other_padding, 0xa5, sizeof(other_padding));
        other_padding.kind = CTOOL_X86_DECODE_KNOWN;
        other_padding.instruction.mnemonic = CTOOL_X86_MN_INVALID;
        other_padding.instruction.operand_bits = 0u;
        other_padding.instruction.address_bits = 0u;
        other_padding.instruction.prefixes = 0u;
        other_padding.instruction.operand_count = 0u;
        other_padding.encoding.size = 0u;
        other_padding.encoding.form = CTOOL_X86_FORM_AUTO;
        other_padding.encoding.field_count = 0u;
        other_padding.consumed = 0u;
        if (!check_true(memcmp(&zero_padding, &other_padding, sizeof(zero_padding)) != 0 &&
                            decoded_equal(&zero_padding, &other_padding),
                        "decoder equivalence ignores inactive storage and padding")) {
            return 1;
        }
    }

    status = ctool_x86_decoder_prepare((ctool_job_t *)0, &decoder);
    if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT, "decoder prepare null job") ||
        !check_true(decoder == (const ctool_x86_decoder_t *)0,
                    "decoder prepare null job clears output") ||
        !open_job(&owner_adapter, &owner_job)) {
        return 1;
    }
    status = ctool_x86_decoder_prepare(owner_job, (const ctool_x86_decoder_t **)0);
    if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT, "decoder prepare null output")) {
        ctool_job_close(owner_job);
        return 1;
    }
    status = ctool_x86_decoder_prepare(owner_job, &decoder);
    if (!check_status(status, CTOOL_OK, "decoder prepare") ||
        !check_true(decoder != (const ctool_x86_decoder_t *)0, "decoder prepare output") ||
        !open_job(&caller_adapter, &caller_job)) {
        ctool_job_close(owner_job);
        return 1;
    }

    {
        static const struct {
            const char *name;
            ctool_u8 bytes[4];
            ctool_u8 size;
            ctool_x86_decode_kind_t kind;
            ctool_x86_mnemonic_t mnemonic;
            ctool_u8 consumed;
            ctool_u8 fields;
        } sequence[] = {{"relative branch before no-field instruction",
                         {0xebu, 0xfeu},
                         2u,
                         CTOOL_X86_DECODE_KNOWN,
                         CTOOL_X86_MN_JMP,
                         2u,
                         1u},
                        {"nop clears preceding relative field",
                         {0x90u},
                         1u,
                         CTOOL_X86_DECODE_KNOWN,
                         CTOOL_X86_MN_NOP,
                         1u,
                         0u},
                        {"unmatched escaped opcode",
                         {0x0fu, 0x0cu},
                         2u,
                         CTOOL_X86_DECODE_UNKNOWN,
                         CTOOL_X86_MN_INVALID,
                         1u,
                         0u},
                        {"immediate after opcode rejection",
                         {0xb0u, 0x07u},
                         2u,
                         CTOOL_X86_DECODE_KNOWN,
                         CTOOL_X86_MN_MOV,
                         2u,
                         1u},
                        {"lock prefix rejects nop",
                         {0xf0u, 0x90u},
                         2u,
                         CTOOL_X86_DECODE_INVALID,
                         CTOOL_X86_MN_INVALID,
                         1u,
                         0u},
                        {"nop after prefix rejection",
                         {0x90u},
                         1u,
                         CTOOL_X86_DECODE_KNOWN,
                         CTOOL_X86_MN_NOP,
                         1u,
                         0u},
                        {"truncated opcode escape",
                         {0x0fu},
                         1u,
                         CTOOL_X86_DECODE_TRUNCATED,
                         CTOOL_X86_MN_INVALID,
                         0u,
                         0u},
                        {"nop after truncated escape",
                         {0x90u},
                         1u,
                         CTOOL_X86_DECODE_KNOWN,
                         CTOOL_X86_MN_NOP,
                         1u,
                         0u},
                        {"displacement field after no-field instruction",
                         {0x8bu, 0x40u, 0x7fu},
                         3u,
                         CTOOL_X86_DECODE_KNOWN,
                         CTOOL_X86_MN_MOV,
                         3u,
                         1u},
                        {"truncated ModRM after displacement",
                         {0x8bu},
                         1u,
                         CTOOL_X86_DECODE_TRUNCATED,
                         CTOOL_X86_MN_INVALID,
                         0u,
                         0u},
                        {"nop after truncated ModRM",
                         {0x90u},
                         1u,
                         CTOOL_X86_DECODE_KNOWN,
                         CTOOL_X86_MN_NOP,
                         1u,
                         0u},
                        {"truncated immediate",
                         {0xb0u},
                         1u,
                         CTOOL_X86_DECODE_TRUNCATED,
                         CTOOL_X86_MN_INVALID,
                         0u,
                         0u},
                        {"mandatory rep prefix selects movdqu",
                         {0xf3u, 0x0fu, 0x6fu, 0xc0u},
                         4u,
                         CTOOL_X86_DECODE_KNOWN,
                         CTOOL_X86_MN_MOVDQU,
                         4u,
                         0u},
                        {"mandatory operand prefix selects movdqa",
                         {0x66u, 0x0fu, 0x6fu, 0xc0u},
                         4u,
                         CTOOL_X86_DECODE_KNOWN,
                         CTOOL_X86_MN_MOVDQA,
                         4u,
                         0u},
                        {"nop after mandatory-prefix alternatives",
                         {0x90u},
                         1u,
                         CTOOL_X86_DECODE_KNOWN,
                         CTOOL_X86_MN_NOP,
                         1u,
                         0u}};

        ctool_u32 sequence_index;
        for (mode_index = 0u; mode_index < (ctool_u32)(sizeof(modes) / sizeof(modes[0]));
             mode_index++) {
            for (sequence_index = 0u;
                 sequence_index < (ctool_u32)(sizeof(sequence) / sizeof(sequence[0]));
                 sequence_index++) {
                ctool_u8 expected_size = sequence[sequence_index].kind == CTOOL_X86_DECODE_TRUNCATED
                                             ? sequence[sequence_index].size
                                             : sequence[sequence_index].consumed;
                (void)memset(&decoded, 0xa5, sizeof(decoded));
                status = ctool_x86_decode_indexed(
                    caller_job, decoder, modes[mode_index],
                    ctool_bytes(sequence[sequence_index].bytes, sequence[sequence_index].size), 0u,
                    &decoded);
                if (!check_status(status, CTOOL_OK, sequence[sequence_index].name) ||
                    !check_true(
                        decoded.kind == sequence[sequence_index].kind &&
                            decoded.instruction.mnemonic == sequence[sequence_index].mnemonic &&
                            decoded.consumed == sequence[sequence_index].consumed &&
                            decoded.encoding.size == expected_size &&
                            decoded.encoding.field_count == sequence[sequence_index].fields &&
                            memcmp(decoded.encoding.bytes, sequence[sequence_index].bytes,
                                   expected_size) == 0,
                        sequence[sequence_index].name)) {
                    ctool_job_close(caller_job);
                    ctool_job_close(owner_job);
                    return 1;
                }
            }
        }
    }

    for (index = 0u; index < CTOOL_X86_MAX_INSTRUCTION_BYTES; index++) {
        bytes[index] = 0u;
    }
    bytes[1] = 0xc0u;
    for (mode_index = 0u; mode_index < (ctool_u32)(sizeof(modes) / sizeof(modes[0]));
         mode_index++) {
        for (opcode = 0u; opcode < 256u; opcode++) {
            bytes[0] = (ctool_u8)opcode;
            if (!compare_prepared_decode(caller_job, decoder, modes[mode_index],
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
            if (!compare_prepared_decode(caller_job, decoder, modes[mode_index],
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
    status = ctool_x86_decode_indexed(caller_job, (const ctool_x86_decoder_t *)0, CTOOL_X86_MODE_32,
                                      ctool_bytes(bytes, 1u), 0u, &decoded);
    if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT, "indexed decode null decoder") ||
        !check_true(decoded_is_zero(&decoded), "indexed decode null decoder zeroes output")) {
        ctool_job_close(caller_job);
        ctool_job_close(owner_job);
        return 1;
    }
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode_indexed(caller_job, decoder, (ctool_x86_mode_t)64,
                                      ctool_bytes(bytes, 1u), 0u, &decoded);
    if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT, "indexed decode invalid mode") ||
        !check_true(decoded_is_zero(&decoded), "indexed decode invalid mode zeroes output") ||
        !check_status(ctool_x86_decode_indexed(caller_job, decoder, CTOOL_X86_MODE_32,
                                               ctool_bytes(bytes, 1u), 0u,
                                               (ctool_x86_decoded_t *)0),
                      CTOOL_ERR_INVALID_ARGUMENT, "indexed decode null output")) {
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
                        ctool_job_diagnostic(limited_job, 0u)->code == CTOOL_X86_DIAG_LIMIT,
                    "decoder prepare limit diagnostic") ||
        !check_status(ctool_arena_alloc(ctool_job_arena(limited_job), 32u, 4u, &recovery), CTOOL_OK,
                      "decoder prepare limit rewinds arena")) {
        ctool_job_close(limited_job);
        ctool_job_close(owner_job);
        return 1;
    }
    ctool_job_close(limited_job);
    ctool_job_close(owner_job);
    (void)printf("decoder-index: ok\n");
    return 0;
}

static int encode(ctool_job_t *job, ctool_x86_mode_t mode, const ctool_x86_instruction_t *insn,
                  ctool_x86_encoding_t *encoding, const char *operation) {
    return check_status(ctool_x86_encode(job, mode, insn, CTOOL_X86_FORM_AUTO, encoding), CTOOL_OK,
                        operation);
}

static int run_model(void) {
    static const char *const required_mnemonics[] = {
        "adc",      "add",      "addps",    "addss",    "and",       "bswap",     "call",
        "clc",      "cld",      "cli",      "clts",     "cmp",       "cmpxchg",   "cpuid",
        "dec",      "div",      "finit",    "fild",     "fistp",     "fld",       "fldz",
        "fninit",   "fsin",     "fstp",     "fwait",    "fxrstor",   "fxsave",    "hlt",
        "in",       "inc",      "int",      "invd",     "invlpg",    "iret",      "iretd",
        "jb",       "jbe",      "jc",       "je",       "jge",       "jl",        "jle",
        "jmp",      "jnc",      "jne",      "jng",      "jnl",       "jnz",       "jpe",
        "jpo",      "jz",       "ldmxcsr",  "lgdt",     "lidt",      "lmsw",      "ltr",
        "mov",      "movss",    "movups",   "movzx",    "mul",       "mulps",     "or",
        "out",      "pop",      "popa",     "popfd",    "push",      "pusha",     "pushfd",
        "rdmsr",    "rdtsc",    "ret",      "retf",     "sgdt",      "shl",       "shr",
        "shrd",     "sidt",     "sldt",     "smsw",     "sqrtss",    "stc",       "sti",
        "stmxcsr",  "str",      "sub",      "syscall",  "sysenter",  "sysexit",   "test",
        "wbinvd",   "wrmsr",    "xadd",     "xor",      "idiv",      "imul",      "lea",
        "leave",    "movsx",    "neg",      "nop",      "not",       "sar",       "sete",
        "setne",    "setnp",    "setp",     "setl",     "setg",      "setle",     "setge",
        "xchg",     "addsd",    "addpd",    "andps",    "andpd",     "cmpps",     "cvtps2dq",
        "cvtdq2ps", "cvtsd2si", "cvtss2si", "cvtsi2sd", "cvtsi2ss",  "cvtsd2ss",  "cvtss2sd",
        "divpd",    "divps",    "divsd",    "divss",    "maxps",     "maxss",     "minps",
        "minss",    "movaps",   "movapd",   "movd",     "movdqa",    "movdqu",    "movmskps",
        "movntdq",  "movsd",    "movupd",   "mulpd",    "mulsd",     "packuswb",  "paddusb",
        "paddw",    "pmullw",   "pshufd",   "psrlw",    "punpckhbw", "punpcklbw", "punpcklwd",
        "pxor",     "sfence",   "shufps",   "sqrtpd",   "sqrtps",    "sqrtsd",    "subpd",
        "subps",    "subsd",    "subss",    "ucomisd",  "ucomiss",   "f2xm1",     "faddp",
        "fcos",     "fld1",     "fldcw",    "fmulp",    "fnstcw",    "fnstsw",    "fpatan",
        "fptan",    "frndint",  "fscale",   "fsub",     "fsubr",     "fxch",      "fyl2x",
        "pause",    "rdrand",   "setc",     "insw",     "outsw",     "cbw",       "cdq",
        "cmc",      "cwde",     "fadd",     "fdiv",     "fdivp",     "fmul",      "fprem",
        "movsb",    "movsw",    "orpd",     "orps",     "shufpd",    "stosb",     "stosd",
        "stosw",    "xorpd",    "xorps"};
    static const char *const required_registers[] = {
        "al",   "cl",   "dl",   "bl",   "ah",   "ch",   "dh",  "bh",  "ax",  "cx",  "dx",
        "bx",   "sp",   "bp",   "si",   "di",   "eax",  "ecx", "edx", "ebx", "esp", "ebp",
        "esi",  "edi",  "es",   "cs",   "ss",   "ds",   "fs",  "gs",  "cr0", "cr1", "cr2",
        "cr3",  "cr4",  "cr5",  "cr6",  "cr7",  "st0",  "st1", "st2", "st3", "st4", "st5",
        "st6",  "st7",  "mm0",  "mm1",  "mm2",  "mm3",  "mm4", "mm5", "mm6", "mm7", "xmm0",
        "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7"};
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
    if (!check_status(ctool_x86_validate_model(job), CTOOL_OK, "model validation")) {
        ctool_job_close(job);
        return 1;
    }
    info = ctool_x86_model_info();
    if (!check_true(info.form_count == 604u && info.mnemonic_count == 249u &&
                        info.register_count == 64u && info.fingerprint == 0x55a8970fu,
                    "model inventory")) {
        ctool_job_close(job);
        return 1;
    }
    if (!check_status(ctool_x86_mnemonic_from_name(ctool_string("jnz"), &mnemonic), CTOOL_OK,
                      "mnemonic alias") ||
        !check_true(mnemonic == CTOOL_X86_MN_JNE, "mnemonic alias canonicalization") ||
        !check_true((mnemonic_name = ctool_x86_mnemonic_name(mnemonic)).size == 3u &&
                        memcmp(mnemonic_name.data, "jne", 3u) == 0,
                    "mnemonic canonical name") ||
        !check_status(ctool_x86_register_from_name(ctool_string("xmm7"), &found_reg), CTOOL_OK,
                      "register lookup") ||
        !check_true(found_reg.class_id == CTOOL_X86_REG_XMM && found_reg.index == 7u,
                    "register classification")) {
        ctool_job_close(job);
        return 1;
    }
    if (!check_status(ctool_x86_mnemonic_from_name(ctool_string("MOVSS"), &mnemonic), CTOOL_OK,
                      "case-insensitive mnemonic") ||
        !check_true(mnemonic == CTOOL_X86_MN_MOVSS, "case-insensitive mnemonic identity") ||
        !check_status(ctool_x86_mnemonic_from_name(ctool_string("not-an-op"), &mnemonic),
                      CTOOL_ERR_NOT_FOUND, "unknown mnemonic") ||
        !check_status(ctool_x86_register_from_name(ctool_string("r8d"), &found_reg),
                      CTOOL_ERR_NOT_FOUND, "out-of-domain register")) {
        ctool_job_close(job);
        return 1;
    }
    for (index = 0u;
         index < (ctool_u32)(sizeof(required_mnemonics) / sizeof(required_mnemonics[0])); index++) {
        if (!check_status(
                ctool_x86_mnemonic_from_name(ctool_string(required_mnemonics[index]), &mnemonic),
                CTOOL_OK, required_mnemonics[index]) ||
            !check_true(ctool_x86_mnemonic_name(mnemonic).size != 0u,
                        "required mnemonic canonical name")) {
            ctool_job_close(job);
            return 1;
        }
    }
    for (index = 0u;
         index < (ctool_u32)(sizeof(required_registers) / sizeof(required_registers[0])); index++) {
        if (!check_status(
                ctool_x86_register_from_name(ctool_string(required_registers[index]), &found_reg),
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
                 info.form_count, info.mnemonic_count, info.register_count, info.fingerprint);
    return 0;
}

#include "x86_integer_contract.inc"

#include "x86_operand_contract.inc"

typedef struct {
    const char *name;
    ctool_x86_mode_t mode;
    ctool_x86_mnemonic_t mnemonic;
    ctool_u8 size;
    ctool_u8 bytes[CTOOL_X86_MAX_INSTRUCTION_BYTES];
} active_vector_t;

static int check_active_vectors(ctool_job_t *job, const active_vector_t *vectors,
                                ctool_u32 vector_count) {
    ctool_u32 index;
    for (index = 0u; index < vector_count; index++) {
        const active_vector_t *vector = &vectors[index];
        ctool_x86_decoded_t decoded;
        ctool_x86_encoding_t reencoded;
        ctool_status_t status = ctool_x86_decode(
            job, vector->mode, ctool_bytes(vector->bytes, vector->size), 0u, &decoded);
        ctool_u32 field;
        if (!check_status(status, CTOOL_OK, vector->name) ||
            !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                            decoded.instruction.mnemonic == vector->mnemonic &&
                            decoded.consumed == vector->size &&
                            decoded.encoding.size == vector->size &&
                            decoded.encoding.form != CTOOL_X86_FORM_AUTO &&
                            memcmp(decoded.encoding.bytes, vector->bytes, vector->size) == 0,
                        vector->name)) {
            return 0;
        }
        for (field = 0u; field < (ctool_u32)decoded.encoding.field_count; field++) {
            if (!check_true(decoded.encoding.fields[field].relocation == CTOOL_X86_RELOC_NONE,
                            "raw decode never invents relocation ownership")) {
                return 0;
            }
        }
        status = ctool_x86_encode(job, vector->mode, &decoded.instruction, decoded.encoding.form,
                                  &reencoded);
        if (!check_status(status, CTOOL_OK, vector->name) ||
            !bytes_equal(&reencoded, vector->bytes, vector->size, vector->name)) {
            return 0;
        }
    }
    return 1;
}

static int check_gnu_string_prefix_order(ctool_job_t *job) {
    static const ctool_u8 gnu_bytes[][3] = {{0x66u, 0xf3u, 0x6du}, {0x66u, 0xf3u, 0x6fu}};
    static const ctool_u8 canonical_bytes[][3] = {{0xf3u, 0x66u, 0x6du}, {0xf3u, 0x66u, 0x6fu}};
    static const ctool_x86_mnemonic_t mnemonics[] = {CTOOL_X86_MN_INSW, CTOOL_X86_MN_OUTSW};
    ctool_u32 index;
    for (index = 0u; index < 2u; index++) {
        ctool_x86_decoded_t decoded;
        ctool_x86_encoding_t reencoded;
        ctool_status_t status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                                                 ctool_bytes(gnu_bytes[index], 3u), 0u, &decoded);
        if (!check_status(status, CTOOL_OK, "GNU string prefix order") ||
            !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                            decoded.instruction.mnemonic == mnemonics[index] &&
                            decoded.instruction.operand_bits == 16u &&
                            decoded.instruction.prefixes == CTOOL_X86_PREFIX_REP &&
                            decoded.consumed == 3u &&
                            memcmp(decoded.encoding.bytes, gnu_bytes[index], 3u) == 0,
                        "GNU string prefix decode") ||
            !check_status(ctool_x86_encode(job, CTOOL_X86_MODE_32, &decoded.instruction,
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
        {"addps", CTOOL_X86_MODE_32, CTOOL_X86_MN_ADDPS, 3u, {0x0fu, 0x58u, 0xc1u}},
        {"addss", CTOOL_X86_MODE_32, CTOOL_X86_MN_ADDSS, 4u, {0xf3u, 0x0fu, 0x58u, 0xc1u}},
        {"and", CTOOL_X86_MODE_32, CTOOL_X86_MN_AND, 2u, {0x21u, 0xd8u}},
        {"bswap", CTOOL_X86_MODE_32, CTOOL_X86_MN_BSWAP, 2u, {0x0fu, 0xc8u}},
        {"call", CTOOL_X86_MODE_32, CTOOL_X86_MN_CALL, 5u, {0xe8u, 0u, 0u, 0u, 0u}},
        {"clc", CTOOL_X86_MODE_32, CTOOL_X86_MN_CLC, 1u, {0xf8u}},
        {"cld", CTOOL_X86_MODE_32, CTOOL_X86_MN_CLD, 1u, {0xfcu}},
        {"cli", CTOOL_X86_MODE_32, CTOOL_X86_MN_CLI, 1u, {0xfau}},
        {"clts", CTOOL_X86_MODE_32, CTOOL_X86_MN_CLTS, 2u, {0x0fu, 0x06u}},
        {"cmp", CTOOL_X86_MODE_32, CTOOL_X86_MN_CMP, 2u, {0x39u, 0xd8u}},
        {"cmpxchg", CTOOL_X86_MODE_32, CTOOL_X86_MN_CMPXCHG, 3u, {0x0fu, 0xb1u, 0xc1u}},
        {"cpuid", CTOOL_X86_MODE_32, CTOOL_X86_MN_CPUID, 2u, {0x0fu, 0xa2u}},
        {"dec", CTOOL_X86_MODE_32, CTOOL_X86_MN_DEC, 1u, {0x49u}},
        {"div", CTOOL_X86_MODE_32, CTOOL_X86_MN_DIV, 2u, {0xf7u, 0xf1u}},
        {"finit", CTOOL_X86_MODE_32, CTOOL_X86_MN_FINIT, 3u, {0x9bu, 0xdbu, 0xe3u}},
        {"fld", CTOOL_X86_MODE_32, CTOOL_X86_MN_FLD, 2u, {0xd9u, 0u}},
        {"fld-m64", CTOOL_X86_MODE_32, CTOOL_X86_MN_FLD, 2u, {0xddu, 0u}},
        {"fld-m80", CTOOL_X86_MODE_32, CTOOL_X86_MN_FLD, 2u, {0xdbu, 0x28u}},
        {"fldz", CTOOL_X86_MODE_32, CTOOL_X86_MN_FLDZ, 2u, {0xd9u, 0xeeu}},
        {"fninit", CTOOL_X86_MODE_32, CTOOL_X86_MN_FNINIT, 2u, {0xdbu, 0xe3u}},
        {"fsin", CTOOL_X86_MODE_32, CTOOL_X86_MN_FSIN, 2u, {0xd9u, 0xfeu}},
        {"fstp", CTOOL_X86_MODE_32, CTOOL_X86_MN_FSTP, 2u, {0xd9u, 0x18u}},
        {"fstp-m64", CTOOL_X86_MODE_32, CTOOL_X86_MN_FSTP, 2u, {0xddu, 0x18u}},
        {"fstp-m80", CTOOL_X86_MODE_32, CTOOL_X86_MN_FSTP, 2u, {0xdbu, 0x38u}},
        {"fstp-st0", CTOOL_X86_MODE_32, CTOOL_X86_MN_FSTP, 2u, {0xddu, 0xd8u}},
        {"fstp-st1", CTOOL_X86_MODE_32, CTOOL_X86_MN_FSTP, 2u, {0xddu, 0xd9u}},
        {"fsub-st1-st0", CTOOL_X86_MODE_32, CTOOL_X86_MN_FSUB, 2u, {0xdcu, 0xe9u}},
        {"fsubr-st1-st0", CTOOL_X86_MODE_32, CTOOL_X86_MN_FSUBR, 2u, {0xdcu, 0xe1u}},
        {"fwait", CTOOL_X86_MODE_32, CTOOL_X86_MN_FWAIT, 1u, {0x9bu}},
        {"fxrstor", CTOOL_X86_MODE_32, CTOOL_X86_MN_FXRSTOR, 3u, {0x0fu, 0xaeu, 0x08u}},
        {"fxsave", CTOOL_X86_MODE_32, CTOOL_X86_MN_FXSAVE, 3u, {0x0fu, 0xaeu, 0u}},
        {"hlt", CTOOL_X86_MODE_32, CTOOL_X86_MN_HLT, 1u, {0xf4u}},
        {"in", CTOOL_X86_MODE_32, CTOOL_X86_MN_IN, 1u, {0xedu}},
        {"inc", CTOOL_X86_MODE_32, CTOOL_X86_MN_INC, 1u, {0x41u}},
        {"int", CTOOL_X86_MODE_16, CTOOL_X86_MN_INT, 2u, {0xcdu, 0x13u}},
        {"invd", CTOOL_X86_MODE_32, CTOOL_X86_MN_INVD, 2u, {0x0fu, 0x08u}},
        {"invlpg", CTOOL_X86_MODE_32, CTOOL_X86_MN_INVLPG, 3u, {0x0fu, 0x01u, 0x38u}},
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
        {"ldmxcsr", CTOOL_X86_MODE_32, CTOOL_X86_MN_LDMXCSR, 3u, {0x0fu, 0xaeu, 0x10u}},
        {"lgdt", CTOOL_X86_MODE_32, CTOOL_X86_MN_LGDT, 3u, {0x0fu, 0x01u, 0x10u}},
        {"lidt", CTOOL_X86_MODE_32, CTOOL_X86_MN_LIDT, 3u, {0x0fu, 0x01u, 0x18u}},
        {"lmsw", CTOOL_X86_MODE_32, CTOOL_X86_MN_LMSW, 3u, {0x0fu, 0x01u, 0xf0u}},
        {"ltr", CTOOL_X86_MODE_32, CTOOL_X86_MN_LTR, 3u, {0x0fu, 0u, 0xd8u}},
        {"mov", CTOOL_X86_MODE_32, CTOOL_X86_MN_MOV, 2u, {0x89u, 0xd8u}},
        {"movss", CTOOL_X86_MODE_32, CTOOL_X86_MN_MOVSS, 4u, {0xf3u, 0x0fu, 0x10u, 0xc1u}},
        {"movss-store-register",
         CTOOL_X86_MODE_32,
         CTOOL_X86_MN_MOVSS,
         4u,
         {0xf3u, 0x0fu, 0x11u, 0xc1u}},
        {"movups", CTOOL_X86_MODE_32, CTOOL_X86_MN_MOVUPS, 3u, {0x0fu, 0x10u, 0xc1u}},
        {"movups-store-register",
         CTOOL_X86_MODE_32,
         CTOOL_X86_MN_MOVUPS,
         3u,
         {0x0fu, 0x11u, 0xc1u}},
        {"movzx", CTOOL_X86_MODE_32, CTOOL_X86_MN_MOVZX, 3u, {0x0fu, 0xb7u, 0xc1u}},
        {"mul", CTOOL_X86_MODE_32, CTOOL_X86_MN_MUL, 2u, {0xf7u, 0xe3u}},
        {"mulps", CTOOL_X86_MODE_32, CTOOL_X86_MN_MULPS, 3u, {0x0fu, 0x59u, 0xc1u}},
        {"or", CTOOL_X86_MODE_32, CTOOL_X86_MN_OR, 2u, {0x09u, 0xd8u}},
        {"out", CTOOL_X86_MODE_16, CTOOL_X86_MN_OUT, 1u, {0xefu}},
        {"pop", CTOOL_X86_MODE_32, CTOOL_X86_MN_POP, 1u, {0x58u}},
        {"popa", CTOOL_X86_MODE_32, CTOOL_X86_MN_POPA, 1u, {0x61u}},
        {"popf", CTOOL_X86_MODE_32, CTOOL_X86_MN_POPF, 1u, {0x9du}},
        {"push", CTOOL_X86_MODE_32, CTOOL_X86_MN_PUSH, 1u, {0x50u}},
        {"pusha", CTOOL_X86_MODE_32, CTOOL_X86_MN_PUSHA, 1u, {0x60u}},
        {"pushf", CTOOL_X86_MODE_32, CTOOL_X86_MN_PUSHF, 1u, {0x9cu}},
        {"rdmsr", CTOOL_X86_MODE_32, CTOOL_X86_MN_RDMSR, 2u, {0x0fu, 0x32u}},
        {"rdtsc", CTOOL_X86_MODE_32, CTOOL_X86_MN_RDTSC, 2u, {0x0fu, 0x31u}},
        {"ret", CTOOL_X86_MODE_32, CTOOL_X86_MN_RET, 1u, {0xc3u}},
        {"retf", CTOOL_X86_MODE_32, CTOOL_X86_MN_RETF, 1u, {0xcbu}},
        {"sgdt", CTOOL_X86_MODE_32, CTOOL_X86_MN_SGDT, 3u, {0x0fu, 0x01u, 0u}},
        {"shl", CTOOL_X86_MODE_32, CTOOL_X86_MN_SHL, 3u, {0xc1u, 0xe0u, 7u}},
        {"shr", CTOOL_X86_MODE_32, CTOOL_X86_MN_SHR, 3u, {0xc1u, 0xe8u, 24u}},
        {"sidt", CTOOL_X86_MODE_32, CTOOL_X86_MN_SIDT, 3u, {0x0fu, 0x01u, 0x08u}},
        {"sldt", CTOOL_X86_MODE_32, CTOOL_X86_MN_SLDT, 4u, {0x66u, 0x0fu, 0u, 0xc0u}},
        {"smsw", CTOOL_X86_MODE_32, CTOOL_X86_MN_SMSW, 4u, {0x66u, 0x0fu, 0x01u, 0xe0u}},
        {"sqrtss", CTOOL_X86_MODE_32, CTOOL_X86_MN_SQRTSS, 4u, {0xf3u, 0x0fu, 0x51u, 0xc0u}},
        {"stc", CTOOL_X86_MODE_32, CTOOL_X86_MN_STC, 1u, {0xf9u}},
        {"sti", CTOOL_X86_MODE_32, CTOOL_X86_MN_STI, 1u, {0xfbu}},
        {"stmxcsr", CTOOL_X86_MODE_32, CTOOL_X86_MN_STMXCSR, 3u, {0x0fu, 0xaeu, 0x18u}},
        {"str", CTOOL_X86_MODE_32, CTOOL_X86_MN_STR, 4u, {0x66u, 0x0fu, 0u, 0xc8u}},
        {"sub", CTOOL_X86_MODE_32, CTOOL_X86_MN_SUB, 2u, {0x29u, 0xd8u}},
        {"syscall", CTOOL_X86_MODE_32, CTOOL_X86_MN_SYSCALL, 2u, {0x0fu, 0x05u}},
        {"sysenter", CTOOL_X86_MODE_32, CTOOL_X86_MN_SYSENTER, 2u, {0x0fu, 0x34u}},
        {"sysexit", CTOOL_X86_MODE_32, CTOOL_X86_MN_SYSEXIT, 2u, {0x0fu, 0x35u}},
        {"test", CTOOL_X86_MODE_32, CTOOL_X86_MN_TEST, 2u, {0x85u, 0xc0u}},
        {"wbinvd", CTOOL_X86_MODE_32, CTOOL_X86_MN_WBINVD, 2u, {0x0fu, 0x09u}},
        {"wrmsr", CTOOL_X86_MODE_32, CTOOL_X86_MN_WRMSR, 2u, {0x0fu, 0x30u}},
        {"xadd", CTOOL_X86_MODE_32, CTOOL_X86_MN_XADD, 3u, {0x0fu, 0xc1u, 0xc3u}},
        {"xor", CTOOL_X86_MODE_32, CTOOL_X86_MN_XOR, 2u, {0x31u, 0xd8u}},
        {"moffs16", CTOOL_X86_MODE_16, CTOOL_X86_MN_MOV, 4u, {0x66u, 0xa1u, 0u, 0x05u}},
        {"absolute-rm32",
         CTOOL_X86_MODE_32,
         CTOOL_X86_MN_MOV,
         7u,
         {0xc6u, 0x05u, 0u, 0x80u, 0x0bu, 0u, 0x50u}},
        {"far-jump", CTOOL_X86_MODE_16, CTOOL_X86_MN_JMP, 5u, {0xeau, 0x34u, 0x12u, 0u, 0u}},
        {"push-ds", CTOOL_X86_MODE_32, CTOOL_X86_MN_PUSH, 1u, {0x1eu}},
        {"pop-ds", CTOOL_X86_MODE_32, CTOOL_X86_MN_POP, 1u, {0x1fu}},
        {"lock-inc", CTOOL_X86_MODE_32, CTOOL_X86_MN_INC, 3u, {0xf0u, 0xffu, 0u}},
        {"lock-cmpxchg", CTOOL_X86_MODE_32, CTOOL_X86_MN_CMPXCHG, 4u, {0xf0u, 0x0fu, 0xb1u, 0x18u}},
        {"control-mov", CTOOL_X86_MODE_32, CTOOL_X86_MN_MOV, 3u, {0x0fu, 0x22u, 0xc0u}},
        {"cdq", CTOOL_X86_MODE_32, CTOOL_X86_MN_CDQ, 1u, {0x99u}},
        {"lea", CTOOL_X86_MODE_32, CTOOL_X86_MN_LEA, 3u, {0x8du, 0x45u, 0xfcu}},
        {"sar-cl", CTOOL_X86_MODE_32, CTOOL_X86_MN_SAR, 2u, {0xd3u, 0xf8u}},
        {"sete", CTOOL_X86_MODE_32, CTOOL_X86_MN_SETE, 3u, {0x0fu, 0x94u, 0xc0u}},
        {"setne", CTOOL_X86_MODE_32, CTOOL_X86_MN_SETNE, 3u, {0x0fu, 0x95u, 0xc0u}},
        {"setnp", CTOOL_X86_MODE_32, CTOOL_X86_MN_SETNP, 3u, {0x0fu, 0x9bu, 0xc2u}},
        {"setp", CTOOL_X86_MODE_32, CTOOL_X86_MN_SETP, 3u, {0x0fu, 0x9au, 0xc2u}},
        {"setl", CTOOL_X86_MODE_32, CTOOL_X86_MN_SETL, 3u, {0x0fu, 0x9cu, 0xc0u}},
        {"setg", CTOOL_X86_MODE_32, CTOOL_X86_MN_SETG, 3u, {0x0fu, 0x9fu, 0xc0u}},
        {"setle", CTOOL_X86_MODE_32, CTOOL_X86_MN_SETLE, 3u, {0x0fu, 0x9eu, 0xc0u}},
        {"setge", CTOOL_X86_MODE_32, CTOOL_X86_MN_SETGE, 3u, {0x0fu, 0x9du, 0xc0u}},
        {"movsx", CTOOL_X86_MODE_32, CTOOL_X86_MN_MOVSX, 3u, {0x0fu, 0xbeu, 0xc0u}},
        {"movaps", CTOOL_X86_MODE_32, CTOOL_X86_MN_MOVAPS, 3u, {0x0fu, 0x28u, 0xc1u}},
        {"movaps-store-register",
         CTOOL_X86_MODE_32,
         CTOOL_X86_MN_MOVAPS,
         3u,
         {0x0fu, 0x29u, 0xc1u}},
        {"movsd-sse", CTOOL_X86_MODE_32, CTOOL_X86_MN_MOVSD, 4u, {0xf2u, 0x0fu, 0x10u, 0xc1u}},
        {"movsd-store-register",
         CTOOL_X86_MODE_32,
         CTOOL_X86_MN_MOVSD,
         4u,
         {0xf2u, 0x0fu, 0x11u, 0xc1u}},
        {"movapd-store-register",
         CTOOL_X86_MODE_32,
         CTOOL_X86_MN_MOVAPD,
         4u,
         {0x66u, 0x0fu, 0x29u, 0xc1u}},
        {"movupd-store-register",
         CTOOL_X86_MODE_32,
         CTOOL_X86_MN_MOVUPD,
         4u,
         {0x66u, 0x0fu, 0x11u, 0xc1u}},
        {"movdqa-store-register",
         CTOOL_X86_MODE_32,
         CTOOL_X86_MN_MOVDQA,
         4u,
         {0x66u, 0x0fu, 0x7fu, 0xc1u}},
        {"movdqu-store-register",
         CTOOL_X86_MODE_32,
         CTOOL_X86_MN_MOVDQU,
         4u,
         {0xf3u, 0x0fu, 0x7fu, 0xc1u}},
        {"addsd", CTOOL_X86_MODE_32, CTOOL_X86_MN_ADDSD, 4u, {0xf2u, 0x0fu, 0x58u, 0xc1u}},
        {"subss", CTOOL_X86_MODE_32, CTOOL_X86_MN_SUBSS, 4u, {0xf3u, 0x0fu, 0x5cu, 0xc1u}},
        {"subsd", CTOOL_X86_MODE_32, CTOOL_X86_MN_SUBSD, 4u, {0xf2u, 0x0fu, 0x5cu, 0xc1u}},
        {"mulss", CTOOL_X86_MODE_32, CTOOL_X86_MN_MULSS, 4u, {0xf3u, 0x0fu, 0x59u, 0xc1u}},
        {"mulsd", CTOOL_X86_MODE_32, CTOOL_X86_MN_MULSD, 4u, {0xf2u, 0x0fu, 0x59u, 0xc1u}},
        {"divss", CTOOL_X86_MODE_32, CTOOL_X86_MN_DIVSS, 4u, {0xf3u, 0x0fu, 0x5eu, 0xc1u}},
        {"divsd", CTOOL_X86_MODE_32, CTOOL_X86_MN_DIVSD, 4u, {0xf2u, 0x0fu, 0x5eu, 0xc1u}},
        {"cvtsi2ss", CTOOL_X86_MODE_32, CTOOL_X86_MN_CVTSI2SS, 4u, {0xf3u, 0x0fu, 0x2au, 0xc0u}},
        {"cvtsi2sd", CTOOL_X86_MODE_32, CTOOL_X86_MN_CVTSI2SD, 4u, {0xf2u, 0x0fu, 0x2au, 0xc0u}},
        {"cvttss2si", CTOOL_X86_MODE_32, CTOOL_X86_MN_CVTTSS2SI, 4u, {0xf3u, 0x0fu, 0x2cu, 0xc0u}},
        {"cvttsd2si", CTOOL_X86_MODE_32, CTOOL_X86_MN_CVTTSD2SI, 4u, {0xf2u, 0x0fu, 0x2cu, 0xc0u}},
        {"cvtss2sd", CTOOL_X86_MODE_32, CTOOL_X86_MN_CVTSS2SD, 4u, {0xf3u, 0x0fu, 0x5au, 0xc1u}},
        {"cvtsd2ss", CTOOL_X86_MODE_32, CTOOL_X86_MN_CVTSD2SS, 4u, {0xf2u, 0x0fu, 0x5au, 0xc1u}},
        {"addpd", CTOOL_X86_MODE_32, CTOOL_X86_MN_ADDPD, 4u, {0x66u, 0x0fu, 0x58u, 0xc1u}},
        {"subps", CTOOL_X86_MODE_32, CTOOL_X86_MN_SUBPS, 3u, {0x0fu, 0x5cu, 0xc1u}},
        {"mulpd", CTOOL_X86_MODE_32, CTOOL_X86_MN_MULPD, 4u, {0x66u, 0x0fu, 0x59u, 0xc1u}},
        {"divps", CTOOL_X86_MODE_32, CTOOL_X86_MN_DIVPS, 3u, {0x0fu, 0x5eu, 0xc1u}},
        {"sqrtps", CTOOL_X86_MODE_32, CTOOL_X86_MN_SQRTPS, 3u, {0x0fu, 0x51u, 0xc1u}},
        {"andps", CTOOL_X86_MODE_32, CTOOL_X86_MN_ANDPS, 3u, {0x0fu, 0x54u, 0xc1u}},
        {"orps", CTOOL_X86_MODE_32, CTOOL_X86_MN_ORPS, 3u, {0x0fu, 0x56u, 0xc1u}},
        {"xorps", CTOOL_X86_MODE_32, CTOOL_X86_MN_XORPS, 3u, {0x0fu, 0x57u, 0xc1u}},
        {"orpd", CTOOL_X86_MODE_32, CTOOL_X86_MN_ORPD, 4u, {0x66u, 0x0fu, 0x56u, 0xc1u}},
        {"xorpd", CTOOL_X86_MODE_32, CTOOL_X86_MN_XORPD, 4u, {0x66u, 0x0fu, 0x57u, 0xc1u}},
        {"cmpps", CTOOL_X86_MODE_32, CTOOL_X86_MN_CMPPS, 4u, {0x0fu, 0xc2u, 0xc1u, 1u}},
        {"movmskps", CTOOL_X86_MODE_32, CTOOL_X86_MN_MOVMSKPS, 3u, {0x0fu, 0x50u, 0xc1u}},
        {"shufps", CTOOL_X86_MODE_32, CTOOL_X86_MN_SHUFPS, 4u, {0x0fu, 0xc6u, 0xc1u, 0x1bu}},
        {"shufpd", CTOOL_X86_MODE_32, CTOOL_X86_MN_SHUFPD, 5u, {0x66u, 0x0fu, 0xc6u, 0xc1u, 1u}},
        {"psrlw-register",
         CTOOL_X86_MODE_32,
         CTOOL_X86_MN_PSRLW,
         5u,
         {0x66u, 0x0fu, 0x71u, 0xd0u, 4u}},
        {"pause", CTOOL_X86_MODE_32, CTOOL_X86_MN_PAUSE, 2u, {0xf3u, 0x90u}},
        {"rep-movsd", CTOOL_X86_MODE_32, CTOOL_X86_MN_MOVSD, 2u, {0xf3u, 0xa5u}},
        {"iretd-mode16", CTOOL_X86_MODE_16, CTOOL_X86_MN_IRETD, 2u, {0x66u, 0xcfu}},
        {"pushad-mode16", CTOOL_X86_MODE_16, CTOOL_X86_MN_PUSHAD, 2u, {0x66u, 0x60u}},
        {"popad-mode16", CTOOL_X86_MODE_16, CTOOL_X86_MN_POPAD, 2u, {0x66u, 0x61u}},
        {"pushfd-mode16", CTOOL_X86_MODE_16, CTOOL_X86_MN_PUSHFD, 2u, {0x66u, 0x9cu}},
        {"popfd-mode16", CTOOL_X86_MODE_16, CTOOL_X86_MN_POPFD, 2u, {0x66u, 0x9du}}};
#define X86_ACTIVE_CASE(name, mode, mnemonic, size, ...)                                           \
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
    if (!check_true((ctool_u32)(sizeof(source_cases) / sizeof(source_cases[0])) == 194u,
                    "active source manifest inventory") ||
        !check_true((ctool_u32)(sizeof(inline_cases) / sizeof(inline_cases[0])) == 129u,
                    "active inline manifest inventory") ||
        !check_active_vectors(job, vectors, (ctool_u32)(sizeof(vectors) / sizeof(vectors[0]))) ||
        !check_active_vectors(job, source_cases,
                              (ctool_u32)(sizeof(source_cases) / sizeof(source_cases[0]))) ||
        !check_active_vectors(job, inline_cases,
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
    static const ctool_u8 unknown_psraw[] = {0x66u, 0x0fu, 0x71u, 0xe0u, 0x04u};
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
    static const ctool_u8 trunc_far[] = {0xeau, 0x78u, 0x56u, 0x34u, 0x12u, 8u, 0u};
    static const ctool_u8 trunc_group[] = {0x81u, 0xc0u, 0x78u, 0x56u, 0x34u, 0x12u};

    static const struct {
        const char *name;
        const ctool_u8 *bytes;
        ctool_u32 size;
    } invalid_vectors[] = {
        {"lock nop", lock_nop, (ctool_u32)sizeof(lock_nop)},
        {"lock register", lock_register, (ctool_u32)sizeof(lock_register)},
        {"reserved group", reserved_group, (ctool_u32)sizeof(reserved_group)},
        {"mov cs", mov_cs, (ctool_u32)sizeof(mov_cs)},
        {"mov cr1", mov_cr1, (ctool_u32)sizeof(mov_cr1)},
        {"invlpg register", invlpg_register, (ctool_u32)sizeof(invlpg_register)},
        {"duplicate prefix", duplicate_prefix, (ctool_u32)sizeof(duplicate_prefix)},
        {"psrlw memory", psrlw_memory, (ctool_u32)sizeof(psrlw_memory)}};

    static const struct {
        const ctool_u8 *bytes;
        ctool_u32 size;
    } truncation_vectors[] = {
        {trunc_mov, (ctool_u32)sizeof(trunc_mov)},     {trunc_sib, (ctool_u32)sizeof(trunc_sib)},
        {trunc_sse, (ctool_u32)sizeof(trunc_sse)},     {trunc_far, (ctool_u32)sizeof(trunc_far)},
        {trunc_group, (ctool_u32)sizeof(trunc_group)}, {trunc_ret, (ctool_u32)sizeof(trunc_ret)}};

    static const struct {
        const char *name;
        const ctool_u8 *bytes;
        ctool_u32 size;
    } unknown_vectors[] = {
        {"unknown opcode", unknown, (ctool_u32)sizeof(unknown)},
        {"unsupported PSRAW", unknown_psraw, (ctool_u32)sizeof(unknown_psraw)},
        {"unsupported LLDT", unknown_lldt, (ctool_u32)sizeof(unknown_lldt)},
        {"unsupported CVTPD2PS", unknown_cvtpd2ps, (ctool_u32)sizeof(unknown_cvtpd2ps)},
        {"unsupported FNOP", unknown_fnop, (ctool_u32)sizeof(unknown_fnop)}};

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
    insn.operands[0] =
        memory_operand(96u, 32u, reg(CTOOL_X86_REG_NONE, 0u), reg(CTOOL_X86_REG_GPR32, 0u),
                       reg(CTOOL_X86_REG_NONE, 0u), 1u, 0, 0u);
    (void)memset(&encoding, 0xa5, sizeof(encoding));
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn, CTOOL_X86_FORM_AUTO, &encoding);
    if (!check_status(status, CTOOL_ERR_INPUT, "unsupported x87 real width") ||
        !check_true(encoding_is_zero(&encoding), "unsupported x87 real width zeroed output")) {
        ctool_job_close(job);
        return 1;
    }
    insn = instruction(CTOOL_X86_MN_MOV, 32u, 32u, 0u);
    insn.operand_count = 2u;
    insn.operands[0] = register_operand(CTOOL_X86_REG_GPR32, 8u);
    insn.operands[1] = register_operand(CTOOL_X86_REG_GPR32, 0u);
    (void)memset(&encoding, 0xa5, sizeof(encoding));
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn, CTOOL_X86_FORM_AUTO, &encoding);
    if (!check_status(status, CTOOL_ERR_INPUT, "invalid register") ||
        !check_true(encoding_is_zero(&encoding), "invalid register zeroed output")) {
        ctool_job_close(job);
        return 1;
    }
    insn.operands[0] = register_operand(CTOOL_X86_REG_GPR32, 0u);
    insn.operands[1] =
        memory_operand(32u, 32u, reg(CTOOL_X86_REG_NONE, 0u), reg(CTOOL_X86_REG_GPR32, 3u),
                       reg(CTOOL_X86_REG_GPR32, 1u), 3u, 0, 0u);
    (void)memset(&encoding, 0xa5, sizeof(encoding));
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn, CTOOL_X86_FORM_AUTO, &encoding);
    if (!check_status(status, CTOOL_ERR_INPUT, "invalid scale") ||
        !check_true(encoding_is_zero(&encoding), "invalid scale zeroed output")) {
        ctool_job_close(job);
        return 1;
    }
    insn.operands[1] =
        memory_operand(32u, 32u, reg(CTOOL_X86_REG_NONE, 0u), reg(CTOOL_X86_REG_NONE, 0u),
                       reg(CTOOL_X86_REG_NONE, 0u), 2u, 0, 32u);
    (void)memset(&encoding, 0xa5, sizeof(encoding));
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn, CTOOL_X86_FORM_AUTO, &encoding);
    if (!check_status(status, CTOOL_ERR_INPUT, "meaningless scale") ||
        !check_true(encoding_is_zero(&encoding), "meaningless scale zeroed output")) {
        ctool_job_close(job);
        return 1;
    }
    insn.operands[1] =
        memory_operand(32u, 32u, reg(CTOOL_X86_REG_NONE, 0u), reg(CTOOL_X86_REG_GPR32, 3u),
                       reg(CTOOL_X86_REG_NONE, 0u), 1u, 128, 8u);
    (void)memset(&encoding, 0xa5, sizeof(encoding));
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn, CTOOL_X86_FORM_AUTO, &encoding);
    if (!check_status(status, CTOOL_ERR_INPUT, "disp8 overflow") ||
        !check_true(encoding_is_zero(&encoding), "disp8 overflow zeroed output")) {
        ctool_job_close(job);
        return 1;
    }
    insn = instruction(CTOOL_X86_MN_MOV, 32u, 32u, CTOOL_X86_PREFIX_LOCK);
    insn.operand_count = 2u;
    insn.operands[0] = register_operand(CTOOL_X86_REG_GPR32, 0u);
    insn.operands[1] = register_operand(CTOOL_X86_REG_GPR32, 3u);
    (void)memset(&encoding, 0xa5, sizeof(encoding));
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn, CTOOL_X86_FORM_AUTO, &encoding);
    if (!check_status(status, CTOOL_ERR_INPUT, "illegal lock") ||
        !check_true(encoding_is_zero(&encoding), "illegal lock zeroed output")) {
        ctool_job_close(job);
        return 1;
    }
    insn = instruction(CTOOL_X86_MN_MOV, 16u, 32u, 0u);
    insn.operand_count = 2u;
    insn.operands[0] = register_operand(CTOOL_X86_REG_SEGMENT, 1u);
    insn.operands[1] = register_operand(CTOOL_X86_REG_GPR16, 0u);
    (void)memset(&encoding, 0xa5, sizeof(encoding));
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn, CTOOL_X86_FORM_AUTO, &encoding);
    if (!check_status(status, CTOOL_ERR_INPUT, "mov cs destination") ||
        !check_true(encoding_is_zero(&encoding), "mov cs zeroed output")) {
        ctool_job_close(job);
        return 1;
    }
    insn = instruction(CTOOL_X86_MN_MOV, 32u, 32u, 0u);
    insn.operand_count = 2u;
    insn.operands[0] = register_operand(CTOOL_X86_REG_CONTROL, 1u);
    insn.operands[1] = register_operand(CTOOL_X86_REG_GPR32, 0u);
    (void)memset(&encoding, 0xa5, sizeof(encoding));
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn, CTOOL_X86_FORM_AUTO, &encoding);
    if (!check_status(status, CTOOL_ERR_INPUT, "reserved control register") ||
        !check_true(encoding_is_zero(&encoding), "reserved control register zeroed output")) {
        ctool_job_close(job);
        return 1;
    }
    insn = instruction(CTOOL_X86_MN_PSRLW, 0u, 32u, 0u);
    insn.operand_count = 2u;
    insn.operands[0] =
        memory_operand(128u, 32u, reg(CTOOL_X86_REG_NONE, 0u), reg(CTOOL_X86_REG_GPR32, 0u),
                       reg(CTOOL_X86_REG_NONE, 0u), 1u, 0, 0u);
    insn.operands[1] = value_operand(CTOOL_X86_OPERAND_IMMEDIATE, 8u, 8u, constant(4u));
    (void)memset(&encoding, 0xa5, sizeof(encoding));
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn, CTOOL_X86_FORM_AUTO, &encoding);
    if (!check_status(status, CTOOL_ERR_INPUT, "psrlw memory") ||
        !check_true(encoding_is_zero(&encoding), "psrlw memory zeroed output")) {
        ctool_job_close(job);
        return 1;
    }
    insn = instruction(CTOOL_X86_MN_RET, 32u, 32u, 0u);
    insn.operand_count = 1u;
    insn.operands[0] = value_operand(CTOOL_X86_OPERAND_IMMEDIATE, 16u, 16u, constant(0x10000u));
    (void)memset(&encoding, 0xa5, sizeof(encoding));
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn, CTOOL_X86_FORM_AUTO, &encoding);
    if (!check_status(status, CTOOL_ERR_INPUT, "return cleanup overflow") ||
        !check_true(encoding_is_zero(&encoding), "return cleanup overflow zeroed output")) {
        ctool_job_close(job);
        return 1;
    }
    insn.operands[0] = value_operand(CTOOL_X86_OPERAND_IMMEDIATE, 32u, 32u, constant(4u));
    (void)memset(&encoding, 0xa5, sizeof(encoding));
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn, CTOOL_X86_FORM_AUTO, &encoding);
    if (!check_status(status, CTOOL_ERR_INPUT, "return cleanup width") ||
        !check_true(encoding_is_zero(&encoding), "return cleanup width zeroed output")) {
        ctool_job_close(job);
        return 1;
    }
    (void)memset(&encoding, 0xa5, sizeof(encoding));
    status = ctool_x86_encode(job, (ctool_x86_mode_t)64, &insn, CTOOL_X86_FORM_AUTO, &encoding);
    if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT, "invalid mode") ||
        !check_true(encoding_is_zero(&encoding), "invalid mode zeroed output")) {
        ctool_job_close(job);
        return 1;
    }
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                              ctool_bytes(truncated, (ctool_u32)sizeof(truncated)), 0u, &decoded);
    if (!check_status(status, CTOOL_OK, "truncated decode") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_TRUNCATED && decoded.consumed == 0u &&
                        decoded.encoding.size == sizeof(truncated) &&
                        decoded.encoding.bytes[0] == truncated[0],
                    "truncated classification")) {
        ctool_job_close(job);
        return 1;
    }
    for (vector_index = 0u;
         vector_index < (ctool_u32)(sizeof(truncation_vectors) / sizeof(truncation_vectors[0]));
         vector_index++) {
        for (cut = 1u; cut < truncation_vectors[vector_index].size; cut++) {
            status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                                      ctool_bytes(truncation_vectors[vector_index].bytes, cut), 0u,
                                      &decoded);
            if (!check_status(status, CTOOL_OK, "truncation boundary") ||
                !check_true(decoded.kind == CTOOL_X86_DECODE_TRUNCATED && decoded.consumed == 0u &&
                                decoded.encoding.size == cut &&
                                memcmp(decoded.encoding.bytes,
                                       truncation_vectors[vector_index].bytes, cut) == 0,
                            "truncation boundary retention")) {
                ctool_job_close(job);
                return 1;
            }
        }
    }
    for (vector_index = 0u;
         vector_index < (ctool_u32)(sizeof(invalid_vectors) / sizeof(invalid_vectors[0]));
         vector_index++) {
        status = ctool_x86_decode(
            job, CTOOL_X86_MODE_32,
            ctool_bytes(invalid_vectors[vector_index].bytes, invalid_vectors[vector_index].size),
            0u, &decoded);
        if (!check_status(status, CTOOL_OK, invalid_vectors[vector_index].name) ||
            !check_true(decoded.kind == CTOOL_X86_DECODE_INVALID && decoded.consumed == 1u &&
                            decoded.encoding.size == 1u &&
                            decoded.encoding.bytes[0] == invalid_vectors[vector_index].bytes[0],
                        invalid_vectors[vector_index].name)) {
            ctool_job_close(job);
            return 1;
        }
    }
    (void)memset(&decoded, 0xa5, sizeof(decoded));
    status = ctool_x86_decode(job, (ctool_x86_mode_t)64,
                              ctool_bytes(unknown, (ctool_u32)sizeof(unknown)), 0u, &decoded);
    if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT, "invalid decode mode") ||
        !check_true(decoded_is_zero(&decoded), "invalid decode zeroed output")) {
        ctool_job_close(job);
        return 1;
    }
    for (vector_index = 0u;
         vector_index < (ctool_u32)(sizeof(unknown_vectors) / sizeof(unknown_vectors[0]));
         vector_index++) {
        status = ctool_x86_decode(
            job, CTOOL_X86_MODE_32,
            ctool_bytes(unknown_vectors[vector_index].bytes, unknown_vectors[vector_index].size),
            0u, &decoded);
        if (!check_status(status, CTOOL_OK, unknown_vectors[vector_index].name) ||
            !check_true(decoded.kind == CTOOL_X86_DECODE_UNKNOWN && decoded.consumed == 1u &&
                            decoded.encoding.size == 1u &&
                            decoded.encoding.bytes[0] == unknown_vectors[vector_index].bytes[0],
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
        (void)fprintf(stderr, "usage: x86-contract "
                              "inventory|model|catalogue|decoder-index|integer|conditional-moves|"
                              "parity-setcc|immediate-imul|double-shift|padding-nops|clang-padding-"
                              "nops|addressing|relocations|system-simd|active-surface|errors\n");
        return 2;
    }
    if (strcmp(argv[1], "model") == 0) {
        return run_model();
    }
    if (strcmp(argv[1], "inventory") == 0) {
        return run_inventory();
    }
    if (strcmp(argv[1], "catalogue") == 0) {
        return run_catalogue();
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
