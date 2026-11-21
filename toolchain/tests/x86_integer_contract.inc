/**
 * @file x86_integer_contract.inc
 * @brief Check integer instruction forms, padding, and condition codes.
 */

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
    insn.operands[1] = value_operand(CTOOL_X86_OPERAND_IMMEDIATE, 32u, 32u, constant(0x12345678u));
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "mov encode") ||
        !bytes_equal(&encoding, mov_bytes, (ctool_u8)sizeof(mov_bytes), "mov bytes")) {
        ctool_job_close(job);
        return 1;
    }
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                              ctool_bytes(mov_bytes, (ctool_u32)sizeof(mov_bytes)), 0u, &decoded);
    if (!check_status(status, CTOOL_OK, "mov decode") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                        decoded.instruction.mnemonic == CTOOL_X86_MN_MOV &&
                        decoded.instruction.operand_bits == 32u &&
                        decoded.instruction.operand_count == 2u &&
                        decoded.instruction.operands[0].kind == CTOOL_X86_OPERAND_REGISTER &&
                        decoded.instruction.operands[0].as.reg.class_id == CTOOL_X86_REG_GPR32 &&
                        decoded.instruction.operands[0].as.reg.index == 0u &&
                        decoded.instruction.operands[1].kind == CTOOL_X86_OPERAND_IMMEDIATE &&
                        decoded.instruction.operands[1].as.value.bits == 0x12345678u &&
                        decoded.encoding.form != CTOOL_X86_FORM_AUTO &&
                        decoded.encoding.size == sizeof(mov_bytes) &&
                        memcmp(decoded.encoding.bytes, mov_bytes, sizeof(mov_bytes)) == 0 &&
                        decoded.encoding.field_count == 1u &&
                        decoded.encoding.fields[0].kind == CTOOL_X86_FIELD_IMMEDIATE &&
                        decoded.encoding.fields[0].relocation == CTOOL_X86_RELOC_NONE &&
                        decoded.encoding.fields[0].byte_offset == 1u &&
                        decoded.encoding.fields[0].byte_width == 4u &&
                        decoded.consumed == sizeof(mov_bytes),
                    "mov decode semantics")) {
        ctool_job_close(job);
        return 1;
    }
    mov_form = decoded.encoding.form;
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &decoded.instruction, mov_form, &preserved);
    if (!check_status(status, CTOOL_OK, "same-form re-encode") ||
        !bytes_equal(&preserved, mov_bytes, (ctool_u8)sizeof(mov_bytes), "same-form bytes")) {
        ctool_job_close(job);
        return 1;
    }

    insn = instruction(CTOOL_X86_MN_ADD, 32u, 32u, 0u);
    insn.operand_count = 2u;
    insn.operands[0] = register_operand(CTOOL_X86_REG_GPR32, 0u);
    insn.operands[1] = register_operand(CTOOL_X86_REG_GPR32, 3u);
    (void)memset(&preserved, 0xa5, sizeof(preserved));
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn, mov_form, &preserved);
    if (!check_status(status, CTOOL_ERR_INPUT, "mismatched form") ||
        !check_true(encoding_is_zero(&preserved), "mismatched form zeroed output")) {
        ctool_job_close(job);
        return 1;
    }
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "add encode") ||
        !bytes_equal(&encoding, add_bytes, (ctool_u8)sizeof(add_bytes), "add bytes")) {
        ctool_job_close(job);
        return 1;
    }
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                              ctool_bytes(add_bytes, (ctool_u32)sizeof(add_bytes)), 0u, &decoded);
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

    insn = instruction(CTOOL_X86_MN_ADD, 32u, 32u, CTOOL_X86_PREFIX_LOCK);
    insn.operand_count = 2u;
    insn.operands[0] =
        memory_operand(32u, 32u, reg(CTOOL_X86_REG_NONE, 0u), reg(CTOOL_X86_REG_GPR32, 0u),
                       reg(CTOOL_X86_REG_NONE, 0u), 1u, 0, 0u);
    insn.operands[1] = register_operand(CTOOL_X86_REG_GPR32, 3u);
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "lock add encode") ||
        !bytes_equal(&encoding, lock_add_bytes, (ctool_u8)sizeof(lock_add_bytes),
                     "lock add bytes")) {
        ctool_job_close(job);
        return 1;
    }

    insn = instruction(CTOOL_X86_MN_CALL, 32u, 32u, 0u);
    insn.operand_count = 1u;
    insn.operands[0] = value_operand(CTOOL_X86_OPERAND_RELATIVE, 32u, 32u, reference(7u, 0));
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "call encode") ||
        !bytes_equal(&encoding, call_bytes, (ctool_u8)sizeof(call_bytes), "call bytes") ||
        !check_true(
            encoding.field_count == 1u && encoding.fields[0].kind == CTOOL_X86_FIELD_RELATIVE &&
                encoding.fields[0].relocation == CTOOL_X86_RELOC_PC_RELATIVE &&
                encoding.fields[0].byte_offset == 1u && encoding.fields[0].byte_width == 4u &&
                encoding.fields[0].pc_bias == 4u && encoding.fields[0].reference == 7u &&
                encoding.fields[0].encoded_addend == -4,
            "call relocation field")) {
        ctool_job_close(job);
        return 1;
    }
    insn = instruction(CTOOL_X86_MN_RET, 32u, 32u, 0u);
    insn.operand_count = 1u;
    insn.operands[0] = value_operand(CTOOL_X86_OPERAND_IMMEDIATE, 16u, 16u, constant(4u));
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "return with stack cleanup") ||
        !bytes_equal(&encoding, ret_bytes, (ctool_u8)sizeof(ret_bytes), "return cleanup bytes")) {
        ctool_job_close(job);
        return 1;
    }
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                              ctool_bytes(ret_bytes, (ctool_u32)sizeof(ret_bytes)), 0u, &decoded);
    if (!check_status(status, CTOOL_OK, "return cleanup decode") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                        decoded.instruction.mnemonic == CTOOL_X86_MN_RET &&
                        decoded.instruction.operand_count == 1u &&
                        decoded.instruction.operands[0].kind == CTOOL_X86_OPERAND_IMMEDIATE &&
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
    static const ctool_u8 full32[] = {0x69u, 0xc1u, 0x28u, 0x02u, 0x00u, 0x00u};
    static const ctool_u8 short32[] = {0x6bu, 0xc1u, 0xf9u};
    static const ctool_u8 full16[] = {0x69u, 0xc1u, 0x34u, 0x12u};
    static const ctool_u8 short16_memory32[] = {0x66u, 0x6bu, 0x53u, 0x7fu, 0xfeu};
    static const ctool_u8 full32_memory16[] = {0x66u, 0x69u, 0x40u, 0x7fu,
                                               0x78u, 0x56u, 0x34u, 0x12u};

    static const struct {
        ctool_u32 value;
        ctool_u8 bytes[6];
        ctool_u8 size;
        const char *name;
    } signed_byte_boundaries[] = {{0x7fu,
                                   {0x6bu, 0xc1u, 0x7fu, 0u, 0u, 0u},
                                   3u,
                                   "immediate IMUL positive signed-byte boundary"},
                                  {0xffffff80u,
                                   {0x6bu, 0xc1u, 0x80u, 0u, 0u, 0u},
                                   3u,
                                   "immediate IMUL negative signed-byte boundary"},
                                  {0x80u,
                                   {0x69u, 0xc1u, 0x80u, 0u, 0u, 0u},
                                   6u,
                                   "immediate IMUL positive full-width boundary"},
                                  {0xffffff7fu,
                                   {0x69u, 0xc1u, 0x7fu, 0xffu, 0xffu, 0xffu},
                                   6u,
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
    static const ctool_u8 semantic_prefixes[] = {CTOOL_X86_PREFIX_LOCK, CTOOL_X86_PREFIX_REP,
                                                 CTOOL_X86_PREFIX_REPNE};
    if (!open_job(&adapter, &job)) {
        return 1;
    }

    insn = instruction(CTOOL_X86_MN_IMUL, 32u, 32u, 0u);
    insn.operand_count = 3u;
    insn.operands[0] = register_operand(CTOOL_X86_REG_GPR32, 0u);
    insn.operands[1] = register_operand(CTOOL_X86_REG_GPR32, 1u);
    insn.operands[2] = value_operand(CTOOL_X86_OPERAND_IMMEDIATE, 32u, 0u, constant(0x228u));
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "immediate IMUL full-width encode") ||
        !bytes_equal(&encoding, full32, (ctool_u8)sizeof(full32),
                     "immediate IMUL full-width bytes")) {
        ctool_job_close(job);
        return 1;
    }

    insn.operands[2] = value_operand(CTOOL_X86_OPERAND_IMMEDIATE, 32u, 0u, constant(0xfffffff9u));
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "immediate IMUL short encode") ||
        !bytes_equal(&encoding, short32, (ctool_u8)sizeof(short32), "immediate IMUL short bytes")) {
        ctool_job_close(job);
        return 1;
    }
    for (boundary_index = 0u; boundary_index < (ctool_u32)(sizeof(signed_byte_boundaries) /
                                                           sizeof(signed_byte_boundaries[0]));
         boundary_index++) {
        insn.operands[2] = value_operand(CTOOL_X86_OPERAND_IMMEDIATE, 32u, 0u,
                                         constant(signed_byte_boundaries[boundary_index].value));
        if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding,
                    signed_byte_boundaries[boundary_index].name) ||
            !bytes_equal(&encoding, signed_byte_boundaries[boundary_index].bytes,
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
    insn.operands[2] = value_operand(CTOOL_X86_OPERAND_IMMEDIATE, 16u, 0u, constant(0x1234u));
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
    insn.operands[1] =
        memory_operand(16u, 32u, reg(CTOOL_X86_REG_NONE, 0u), reg(CTOOL_X86_REG_GPR32, 3u),
                       reg(CTOOL_X86_REG_NONE, 0u), 1u, 0x7f, 8u);
    insn.operands[2] = value_operand(CTOOL_X86_OPERAND_IMMEDIATE, 16u, 0u, constant(0xfffffffeu));
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "16-bit immediate IMUL memory encode") ||
        !bytes_equal(&encoding, short16_memory32, (ctool_u8)sizeof(short16_memory32),
                     "16-bit immediate IMUL memory bytes")) {
        ctool_job_close(job);
        return 1;
    }

    insn = instruction(CTOOL_X86_MN_IMUL, 32u, 16u, 0u);
    insn.operand_count = 3u;
    insn.operands[0] = register_operand(CTOOL_X86_REG_GPR32, 0u);
    insn.operands[1] =
        memory_operand(32u, 16u, reg(CTOOL_X86_REG_NONE, 0u), reg(CTOOL_X86_REG_GPR16, 3u),
                       reg(CTOOL_X86_REG_GPR16, 6u), 1u, 0x7f, 8u);
    insn.operands[2] = value_operand(CTOOL_X86_OPERAND_IMMEDIATE, 32u, 0u, constant(0x12345678u));
    if (!encode(job, CTOOL_X86_MODE_16, &insn, &encoding,
                "32-bit immediate IMUL 16-bit address encode") ||
        !bytes_equal(&encoding, full32_memory16, (ctool_u8)sizeof(full32_memory16),
                     "32-bit immediate IMUL 16-bit address bytes")) {
        ctool_job_close(job);
        return 1;
    }

    status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                              ctool_bytes(full32, (ctool_u32)sizeof(full32)), 0u, &decoded);
    if (!check_status(status, CTOOL_OK, "immediate IMUL full decode") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN && decoded.consumed == sizeof(full32) &&
                        decoded.instruction.mnemonic == CTOOL_X86_MN_IMUL &&
                        decoded.instruction.operand_bits == 32u &&
                        decoded.instruction.operand_count == 3u &&
                        decoded.instruction.operands[0].kind == CTOOL_X86_OPERAND_REGISTER &&
                        decoded.instruction.operands[0].as.reg.class_id == CTOOL_X86_REG_GPR32 &&
                        decoded.instruction.operands[0].as.reg.index == 0u &&
                        decoded.instruction.operands[1].kind == CTOOL_X86_OPERAND_REGISTER &&
                        decoded.instruction.operands[1].as.reg.index == 1u &&
                        decoded.instruction.operands[2].kind == CTOOL_X86_OPERAND_IMMEDIATE &&
                        decoded.instruction.operands[2].width_bits == 32u &&
                        decoded.instruction.operands[2].encoding_bits == 32u &&
                        decoded.instruction.operands[2].as.value.bits == 0x228u &&
                        decoded.encoding.field_count == 1u &&
                        decoded.encoding.fields[0].kind == CTOOL_X86_FIELD_IMMEDIATE &&
                        decoded.encoding.fields[0].operand_index == 2u &&
                        decoded.encoding.fields[0].byte_offset == 2u &&
                        decoded.encoding.fields[0].byte_width == 4u &&
                        decoded.encoding.fields[0].encoded_addend == 0x228,
                    "immediate IMUL full decode semantics")) {
        ctool_job_close(job);
        return 1;
    }
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &decoded.instruction, decoded.encoding.form,
                              &replay);
    if (!check_status(status, CTOOL_OK, "immediate IMUL full requested-form replay") ||
        !bytes_equal(&replay, full32, (ctool_u8)sizeof(full32),
                     "immediate IMUL full replay bytes")) {
        ctool_job_close(job);
        return 1;
    }

    status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                              ctool_bytes(short32, (ctool_u32)sizeof(short32)), 0u, &decoded);
    if (!check_status(status, CTOOL_OK, "immediate IMUL short decode") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN && decoded.consumed == sizeof(short32) &&
                        decoded.instruction.operand_bits == 32u &&
                        decoded.instruction.operands[2].width_bits == 32u &&
                        decoded.instruction.operands[2].encoding_bits == 8u &&
                        decoded.instruction.operands[2].as.value.bits == 0xfffffff9u &&
                        decoded.encoding.fields[0].byte_offset == 2u &&
                        decoded.encoding.fields[0].byte_width == 1u &&
                        decoded.encoding.fields[0].encoded_addend == -7,
                    "immediate IMUL short decode semantics")) {
        ctool_job_close(job);
        return 1;
    }
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &decoded.instruction, decoded.encoding.form,
                              &replay);
    if (!check_status(status, CTOOL_OK, "immediate IMUL short requested-form replay") ||
        !bytes_equal(&replay, short32, (ctool_u8)sizeof(short32),
                     "immediate IMUL short replay bytes")) {
        ctool_job_close(job);
        return 1;
    }

    status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                              ctool_bytes(short16_memory32, (ctool_u32)sizeof(short16_memory32)),
                              0u, &decoded);
    if (!check_status(status, CTOOL_OK, "immediate IMUL memory decode") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                        decoded.consumed == sizeof(short16_memory32) &&
                        decoded.instruction.operand_bits == 16u &&
                        decoded.instruction.address_bits == 32u &&
                        decoded.instruction.operands[0].as.reg.class_id == CTOOL_X86_REG_GPR16 &&
                        decoded.instruction.operands[0].as.reg.index == 2u &&
                        decoded.instruction.operands[1].kind == CTOOL_X86_OPERAND_MEMORY &&
                        decoded.instruction.operands[1].width_bits == 16u &&
                        decoded.instruction.operands[1].as.memory.base.index == 3u &&
                        decoded.instruction.operands[1].as.memory.displacement.bits == 0x7fu &&
                        decoded.instruction.operands[2].as.value.bits == 0xfffffffeu,
                    "immediate IMUL memory decode semantics")) {
        ctool_job_close(job);
        return 1;
    }

    for (cut = 1u; cut < (ctool_u32)sizeof(full32_memory16); cut++) {
        status = ctool_x86_decode(job, CTOOL_X86_MODE_16, ctool_bytes(full32_memory16, cut), 0u,
                                  &decoded);
        if (!check_status(status, CTOOL_OK, "immediate IMUL every-byte truncation") ||
            !check_true(decoded.kind == CTOOL_X86_DECODE_TRUNCATED && decoded.consumed == 0u &&
                            decoded.encoding.size == cut &&
                            memcmp(decoded.encoding.bytes, full32_memory16, (size_t)cut) == 0,
                        "immediate IMUL truncation retention")) {
            ctool_job_close(job);
            return 1;
        }
    }

    insn = instruction(CTOOL_X86_MN_IMUL, 32u, 32u, 0u);
    insn.operand_count = 3u;
    insn.operands[0] =
        memory_operand(32u, 32u, reg(CTOOL_X86_REG_NONE, 0u), reg(CTOOL_X86_REG_GPR32, 0u),
                       reg(CTOOL_X86_REG_NONE, 0u), 1u, 0, 0u);
    insn.operands[1] = register_operand(CTOOL_X86_REG_GPR32, 1u);
    insn.operands[2] = value_operand(CTOOL_X86_OPERAND_IMMEDIATE, 32u, 0u, constant(2u));
    (void)memset(&encoding, 0xa5, sizeof(encoding));
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn, CTOOL_X86_FORM_AUTO, &encoding);
    if (!check_status(status, CTOOL_ERR_INPUT, "immediate IMUL memory destination") ||
        !check_true(encoding_is_zero(&encoding), "immediate IMUL memory destination rollback")) {
        ctool_job_close(job);
        return 1;
    }

    insn.operands[0] = register_operand(CTOOL_X86_REG_GPR32, 0u);
    insn.operands[1] = register_operand(CTOOL_X86_REG_GPR16, 1u);
    (void)memset(&encoding, 0xa5, sizeof(encoding));
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn, CTOOL_X86_FORM_AUTO, &encoding);
    if (!check_status(status, CTOOL_ERR_INPUT, "immediate IMUL source width mismatch") ||
        !check_true(encoding_is_zero(&encoding), "immediate IMUL source width rollback")) {
        ctool_job_close(job);
        return 1;
    }

    insn.operands[1] = register_operand(CTOOL_X86_REG_GPR32, 1u);
    insn.operands[2] = value_operand(CTOOL_X86_OPERAND_IMMEDIATE, 32u, 16u, constant(2u));
    (void)memset(&encoding, 0xa5, sizeof(encoding));
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn, CTOOL_X86_FORM_AUTO, &encoding);
    if (!check_status(status, CTOOL_ERR_INPUT, "immediate IMUL serialized width mismatch") ||
        !check_true(encoding_is_zero(&encoding), "immediate IMUL serialized width rollback")) {
        ctool_job_close(job);
        return 1;
    }

    insn.operands[2] = value_operand(CTOOL_X86_OPERAND_IMMEDIATE, 32u, 0u, constant(2u));
    for (prefix_index = 0u;
         prefix_index < (ctool_u32)(sizeof(prefix_bytes) / sizeof(prefix_bytes[0]));
         prefix_index++) {
        ctool_u8 prefixed[4];
        prefixed[0] = prefix_bytes[prefix_index];
        prefixed[1] = short32[0];
        prefixed[2] = short32[1];
        prefixed[3] = 2u;
        insn.prefixes = semantic_prefixes[prefix_index];
        (void)memset(&encoding, 0xa5, sizeof(encoding));
        status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn, CTOOL_X86_FORM_AUTO, &encoding);
        if (!check_status(status, CTOOL_ERR_INPUT, "immediate IMUL semantic prefix") ||
            !check_true(encoding_is_zero(&encoding), "immediate IMUL semantic prefix rollback")) {
            ctool_job_close(job);
            return 1;
        }
        status = ctool_x86_decode(job, CTOOL_X86_MODE_32, ctool_bytes(prefixed, 4u), 0u, &decoded);
        if (!check_status(status, CTOOL_OK, "immediate IMUL illegal prefix decode") ||
            !check_true(decoded.kind == (prefix_index == 0u ? CTOOL_X86_DECODE_INVALID
                                                            : CTOOL_X86_DECODE_UNKNOWN) &&
                            decoded.consumed == 1u && decoded.encoding.size == 1u &&
                            decoded.encoding.bytes[0] == prefixed[0],
                        "immediate IMUL illegal prefix classification")) {
            ctool_job_close(job);
            return 1;
        }
        status = ctool_x86_decode(job, CTOOL_X86_MODE_32, ctool_bytes(prefixed, 4u), 1u, &decoded);
        if (!check_status(status, CTOOL_OK, "immediate IMUL post-prefix recovery") ||
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
    } cases[] = {{CTOOL_X86_MODE_16,
                  16u,
                  CTOOL_FALSE,
                  CTOOL_FALSE,
                  7u,
                  {0x0fu, 0xacu, 0xf8u, 0x07u, 0u},
                  4u,
                  "16-bit register SHRD immediate"},
                 {CTOOL_X86_MODE_16,
                  16u,
                  CTOOL_FALSE,
                  CTOOL_TRUE,
                  7u,
                  {0x0fu, 0xadu, 0xf8u, 0u, 0u},
                  3u,
                  "16-bit register SHRD CL"},
                 {CTOOL_X86_MODE_16,
                  16u,
                  CTOOL_TRUE,
                  CTOOL_FALSE,
                  2u,
                  {0x0fu, 0xacu, 0x50u, 0x7fu, 0x07u},
                  5u,
                  "16-bit memory SHRD immediate"},
                 {CTOOL_X86_MODE_16,
                  16u,
                  CTOOL_TRUE,
                  CTOOL_TRUE,
                  2u,
                  {0x0fu, 0xadu, 0x50u, 0x7fu, 0u},
                  4u,
                  "16-bit memory SHRD CL"},
                 {CTOOL_X86_MODE_32,
                  32u,
                  CTOOL_FALSE,
                  CTOOL_FALSE,
                  7u,
                  {0x0fu, 0xacu, 0xf8u, 0x07u, 0u},
                  4u,
                  "32-bit register SHRD immediate"},
                 {CTOOL_X86_MODE_32,
                  32u,
                  CTOOL_FALSE,
                  CTOOL_TRUE,
                  7u,
                  {0x0fu, 0xadu, 0xf8u, 0u, 0u},
                  3u,
                  "32-bit register SHRD CL"},
                 {CTOOL_X86_MODE_32,
                  32u,
                  CTOOL_TRUE,
                  CTOOL_FALSE,
                  6u,
                  {0x0fu, 0xacu, 0x73u, 0x04u, 0x07u},
                  5u,
                  "32-bit memory SHRD immediate"},
                 {CTOOL_X86_MODE_32,
                  32u,
                  CTOOL_TRUE,
                  CTOOL_TRUE,
                  6u,
                  {0x0fu, 0xadu, 0x73u, 0x04u, 0u},
                  4u,
                  "32-bit memory SHRD CL"}};

    static const ctool_u8 wide_memory16[] = {0x66u, 0x0fu, 0xacu, 0x30u, 0x1fu};
    static const ctool_u8 wide_address32_in_16[] = {0x66u, 0x67u, 0x0fu, 0xacu,
                                                    0x73u, 0x04u, 0x1fu};
    static const ctool_u8 narrow_register32[] = {0x66u, 0x0fu, 0xadu, 0xf8u};
    static const ctool_u8 narrow_address16_in_32[] = {0x66u, 0x67u, 0x0fu, 0xadu, 0x50u, 0x7fu};
    static const ctool_u8 active_stream[] = {0x0fu, 0xadu, 0xf8u, 0x0fu, 0xadu, 0xf8u, 0xc3u};
    static const ctool_u8 locked_active[] = {0xf0u, 0x0fu, 0xadu, 0xf8u, 0xc3u};
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
    if (!check_true(canonical.size == 4u && memcmp(canonical.data, "shrd", 4u) == 0,
                    "SHRD canonical mnemonic")) {
        ctool_job_close(job);
        return 1;
    }

    for (case_index = 0u; case_index < (ctool_u32)(sizeof(cases) / sizeof(cases[0]));
         case_index++) {
        ctool_x86_reg_class_t register_class =
            cases[case_index].operand_bits == 16u ? CTOOL_X86_REG_GPR16 : CTOOL_X86_REG_GPR32;
        insn = instruction(CTOOL_X86_MN_SHRD, cases[case_index].operand_bits,
                           cases[case_index].operand_bits, 0u);
        insn.operand_count = 3u;
        if (cases[case_index].memory_destination == CTOOL_TRUE) {
            if (cases[case_index].operand_bits == 16u) {
                insn.operands[0] = memory_operand(16u, 16u, reg(CTOOL_X86_REG_NONE, 0u),
                                                  reg(CTOOL_X86_REG_GPR16, 3u),
                                                  reg(CTOOL_X86_REG_GPR16, 6u), 1u, 0x7f, 8u);
            } else {
                insn.operands[0] = memory_operand(32u, 32u, reg(CTOOL_X86_REG_NONE, 0u),
                                                  reg(CTOOL_X86_REG_GPR32, 3u),
                                                  reg(CTOOL_X86_REG_NONE, 0u), 1u, 4, 8u);
            }
        } else {
            insn.operands[0] = register_operand(register_class, 0u);
        }
        insn.operands[1] = register_operand(register_class, cases[case_index].source_index);
        insn.operands[2] = cases[case_index].cl_count == CTOOL_TRUE
                               ? register_operand(CTOOL_X86_REG_GPR8, 1u)
                               : value_operand(CTOOL_X86_OPERAND_IMMEDIATE,
                                               cases[case_index].operand_bits, 0u, constant(7u));
        if (!encode(job, cases[case_index].mode, &insn, &encoding, cases[case_index].name) ||
            !bytes_equal(&encoding, cases[case_index].bytes, cases[case_index].size,
                         cases[case_index].name)) {
            ctool_job_close(job);
            return 1;
        }

        status = ctool_x86_decode(job, cases[case_index].mode,
                                  ctool_bytes(cases[case_index].bytes, cases[case_index].size), 0u,
                                  &decoded);
        if (!check_status(status, CTOOL_OK, cases[case_index].name) ||
            !check_true(
                decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                    decoded.consumed == cases[case_index].size &&
                    decoded.instruction.mnemonic == CTOOL_X86_MN_SHRD &&
                    decoded.instruction.operand_bits == cases[case_index].operand_bits &&
                    decoded.instruction.address_bits == cases[case_index].operand_bits &&
                    decoded.instruction.operand_count == 3u &&
                    decoded.instruction.operands[0].kind ==
                        (cases[case_index].memory_destination == CTOOL_TRUE
                             ? CTOOL_X86_OPERAND_MEMORY
                             : CTOOL_X86_OPERAND_REGISTER) &&
                    decoded.instruction.operands[1].kind == CTOOL_X86_OPERAND_REGISTER &&
                    decoded.instruction.operands[1].as.reg.class_id == register_class &&
                    decoded.instruction.operands[1].as.reg.index ==
                        cases[case_index].source_index &&
                    decoded.instruction.operands[2].kind ==
                        (cases[case_index].cl_count == CTOOL_TRUE ? CTOOL_X86_OPERAND_REGISTER
                                                                  : CTOOL_X86_OPERAND_IMMEDIATE) &&
                    (cases[case_index].cl_count == CTOOL_TRUE
                         ? decoded.instruction.operands[2].as.reg.class_id == CTOOL_X86_REG_GPR8 &&
                               decoded.instruction.operands[2].as.reg.index == 1u &&
                               decoded.encoding.field_count ==
                                   (cases[case_index].memory_destination == CTOOL_TRUE ? 1u : 0u)
                         : decoded.instruction.operands[2].width_bits ==
                                   cases[case_index].operand_bits &&
                               decoded.instruction.operands[2].encoding_bits == 8u &&
                               decoded.instruction.operands[2].as.value.bits == 7u &&
                               decoded.encoding.field_count ==
                                   (cases[case_index].memory_destination == CTOOL_TRUE ? 2u : 1u) &&
                               decoded.encoding.fields[decoded.encoding.field_count - 1u].kind ==
                                   CTOOL_X86_FIELD_IMMEDIATE &&
                               decoded.encoding.fields[decoded.encoding.field_count - 1u]
                                       .operand_index == 2u &&
                               decoded.encoding.fields[decoded.encoding.field_count - 1u]
                                           .byte_offset +
                                       1u ==
                                   cases[case_index].size &&
                               decoded.encoding.fields[decoded.encoding.field_count - 1u]
                                       .byte_width == 1u),
                cases[case_index].name)) {
            ctool_job_close(job);
            return 1;
        }
        if (cases[case_index].memory_destination == CTOOL_TRUE) {
            if (!check_true(decoded.instruction.operands[0].width_bits ==
                                    cases[case_index].operand_bits &&
                                decoded.instruction.operands[0].as.memory.address_bits ==
                                    cases[case_index].operand_bits &&
                                decoded.instruction.operands[0].as.memory.displacement.bits ==
                                    (cases[case_index].operand_bits == 16u ? 0x7fu : 4u),
                            cases[case_index].name)) {
                ctool_job_close(job);
                return 1;
            }
        } else if (!check_true(decoded.instruction.operands[0].as.reg.class_id == register_class &&
                                   decoded.instruction.operands[0].as.reg.index == 0u,
                               cases[case_index].name)) {
            ctool_job_close(job);
            return 1;
        }
        status = ctool_x86_encode(job, cases[case_index].mode, &decoded.instruction,
                                  decoded.encoding.form, &replay);
        if (!check_status(status, CTOOL_OK, cases[case_index].name) ||
            !bytes_equal(&replay, cases[case_index].bytes, cases[case_index].size,
                         cases[case_index].name)) {
            ctool_job_close(job);
            return 1;
        }
    }

    insn = instruction(CTOOL_X86_MN_SHRD, 32u, 16u, 0u);
    insn.operand_count = 3u;
    insn.operands[0] =
        memory_operand(32u, 16u, reg(CTOOL_X86_REG_NONE, 0u), reg(CTOOL_X86_REG_GPR16, 3u),
                       reg(CTOOL_X86_REG_GPR16, 6u), 1u, 0, 0u);
    insn.operands[1] = register_operand(CTOOL_X86_REG_GPR32, 6u);
    insn.operands[2] = value_operand(CTOOL_X86_OPERAND_IMMEDIATE, 32u, 0u, constant(0x1fu));
    if (!encode(job, CTOOL_X86_MODE_16, &insn, &encoding, "32-bit SHRD in 16-bit mode") ||
        !bytes_equal(&encoding, wide_memory16, (ctool_u8)sizeof(wide_memory16),
                     "32-bit SHRD in 16-bit mode bytes")) {
        ctool_job_close(job);
        return 1;
    }
    status = ctool_x86_decode(job, CTOOL_X86_MODE_16,
                              ctool_bytes(wide_memory16, (ctool_u32)sizeof(wide_memory16)), 0u,
                              &decoded);
    if (!check_status(status, CTOOL_OK, "32-bit SHRD in 16-bit mode decode") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                        decoded.instruction.mnemonic == CTOOL_X86_MN_SHRD &&
                        decoded.instruction.operand_bits == 32u &&
                        decoded.instruction.address_bits == 16u &&
                        decoded.instruction.operands[0].kind == CTOOL_X86_OPERAND_MEMORY &&
                        decoded.instruction.operands[1].as.reg.index == 6u &&
                        decoded.instruction.operands[2].as.value.bits == 0x1fu,
                    "32-bit SHRD in 16-bit mode semantics")) {
        ctool_job_close(job);
        return 1;
    }

    insn = instruction(CTOOL_X86_MN_SHRD, 32u, 32u, 0u);
    insn.operand_count = 3u;
    insn.operands[0] =
        memory_operand(32u, 32u, reg(CTOOL_X86_REG_NONE, 0u), reg(CTOOL_X86_REG_GPR32, 3u),
                       reg(CTOOL_X86_REG_NONE, 0u), 1u, 4, 8u);
    insn.operands[1] = register_operand(CTOOL_X86_REG_GPR32, 6u);
    insn.operands[2] = value_operand(CTOOL_X86_OPERAND_IMMEDIATE, 32u, 0u, constant(0x1fu));
    if (!encode(job, CTOOL_X86_MODE_16, &insn, &encoding, "32-bit-address SHRD in 16-bit mode") ||
        !bytes_equal(&encoding, wide_address32_in_16, (ctool_u8)sizeof(wide_address32_in_16),
                     "32-bit-address SHRD in 16-bit mode bytes")) {
        ctool_job_close(job);
        return 1;
    }
    status = ctool_x86_decode(
        job, CTOOL_X86_MODE_16,
        ctool_bytes(wide_address32_in_16, (ctool_u32)sizeof(wide_address32_in_16)), 0u, &decoded);
    if (!check_status(status, CTOOL_OK, "32-bit-address SHRD in 16-bit mode decode") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                        decoded.instruction.mnemonic == CTOOL_X86_MN_SHRD &&
                        decoded.instruction.operand_bits == 32u &&
                        decoded.instruction.address_bits == 32u &&
                        decoded.instruction.operands[0].kind == CTOOL_X86_OPERAND_MEMORY &&
                        decoded.instruction.operands[0].as.memory.address_bits == 32u &&
                        decoded.instruction.operands[0].as.memory.base.class_id ==
                            CTOOL_X86_REG_GPR32 &&
                        decoded.instruction.operands[0].as.memory.base.index == 3u &&
                        decoded.instruction.operands[1].as.reg.index == 6u &&
                        decoded.instruction.operands[2].as.value.bits == 0x1fu,
                    "32-bit-address SHRD in 16-bit mode semantics")) {
        ctool_job_close(job);
        return 1;
    }
    status = ctool_x86_encode(job, CTOOL_X86_MODE_16, &decoded.instruction, decoded.encoding.form,
                              &replay);
    if (!check_status(status, CTOOL_OK, "32-bit-address SHRD in 16-bit mode replay") ||
        !bytes_equal(&replay, wide_address32_in_16, (ctool_u8)sizeof(wide_address32_in_16),
                     "32-bit-address SHRD in 16-bit mode replay bytes")) {
        ctool_job_close(job);
        return 1;
    }

    insn = instruction(CTOOL_X86_MN_SHRD, 16u, 32u, 0u);
    insn.operand_count = 3u;
    insn.operands[0] = register_operand(CTOOL_X86_REG_GPR16, 0u);
    insn.operands[1] = register_operand(CTOOL_X86_REG_GPR16, 7u);
    insn.operands[2] = register_operand(CTOOL_X86_REG_GPR8, 1u);
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "16-bit SHRD in 32-bit mode") ||
        !bytes_equal(&encoding, narrow_register32, (ctool_u8)sizeof(narrow_register32),
                     "16-bit SHRD in 32-bit mode bytes")) {
        ctool_job_close(job);
        return 1;
    }
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                              ctool_bytes(narrow_register32, (ctool_u32)sizeof(narrow_register32)),
                              0u, &decoded);
    if (!check_status(status, CTOOL_OK, "16-bit SHRD in 32-bit mode decode") ||
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
    insn.operands[0] =
        memory_operand(16u, 16u, reg(CTOOL_X86_REG_NONE, 0u), reg(CTOOL_X86_REG_GPR16, 3u),
                       reg(CTOOL_X86_REG_GPR16, 6u), 1u, 0x7f, 8u);
    insn.operands[1] = register_operand(CTOOL_X86_REG_GPR16, 2u);
    insn.operands[2] = register_operand(CTOOL_X86_REG_GPR8, 1u);
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "16-bit-address SHRD in 32-bit mode") ||
        !bytes_equal(&encoding, narrow_address16_in_32, (ctool_u8)sizeof(narrow_address16_in_32),
                     "16-bit-address SHRD in 32-bit mode bytes")) {
        ctool_job_close(job);
        return 1;
    }
    status = ctool_x86_decode(
        job, CTOOL_X86_MODE_32,
        ctool_bytes(narrow_address16_in_32, (ctool_u32)sizeof(narrow_address16_in_32)), 0u,
        &decoded);
    if (!check_status(status, CTOOL_OK, "16-bit-address SHRD in 32-bit mode decode") ||
        !check_true(
            decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                decoded.instruction.mnemonic == CTOOL_X86_MN_SHRD &&
                decoded.instruction.operand_bits == 16u &&
                decoded.instruction.address_bits == 16u &&
                decoded.instruction.operands[0].kind == CTOOL_X86_OPERAND_MEMORY &&
                decoded.instruction.operands[0].as.memory.address_bits == 16u &&
                decoded.instruction.operands[0].as.memory.base.class_id == CTOOL_X86_REG_GPR16 &&
                decoded.instruction.operands[0].as.memory.base.index == 3u &&
                decoded.instruction.operands[0].as.memory.index.class_id == CTOOL_X86_REG_GPR16 &&
                decoded.instruction.operands[0].as.memory.index.index == 6u &&
                decoded.instruction.operands[1].as.reg.index == 2u &&
                decoded.instruction.operands[2].as.reg.index == 1u,
            "16-bit-address SHRD in 32-bit mode semantics")) {
        ctool_job_close(job);
        return 1;
    }
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &decoded.instruction, decoded.encoding.form,
                              &replay);
    if (!check_status(status, CTOOL_OK, "16-bit-address SHRD in 32-bit mode replay") ||
        !bytes_equal(&replay, narrow_address16_in_32, (ctool_u8)sizeof(narrow_address16_in_32),
                     "16-bit-address SHRD in 32-bit mode replay bytes")) {
        ctool_job_close(job);
        return 1;
    }

    for (cut = 1u; cut < (ctool_u32)sizeof(wide_memory16); cut++) {
        status =
            ctool_x86_decode(job, CTOOL_X86_MODE_16, ctool_bytes(wide_memory16, cut), 0u, &decoded);
        if (!check_status(status, CTOOL_OK, "SHRD every-byte truncation") ||
            !check_true(decoded.kind == CTOOL_X86_DECODE_TRUNCATED && decoded.consumed == 0u &&
                            decoded.encoding.size == cut &&
                            memcmp(decoded.encoding.bytes, wide_memory16, (size_t)cut) == 0,
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
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn, CTOOL_X86_FORM_AUTO, &encoding);
    if (!check_status(status, CTOOL_ERR_INPUT, "SHRD width mismatch") ||
        !check_true(encoding_is_zero(&encoding), "SHRD width mismatch rollback")) {
        ctool_job_close(job);
        return 1;
    }

    insn.operands[1] =
        memory_operand(32u, 32u, reg(CTOOL_X86_REG_NONE, 0u), reg(CTOOL_X86_REG_GPR32, 7u),
                       reg(CTOOL_X86_REG_NONE, 0u), 1u, 0, 0u);
    (void)memset(&encoding, 0xa5, sizeof(encoding));
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn, CTOOL_X86_FORM_AUTO, &encoding);
    if (!check_status(status, CTOOL_ERR_INPUT, "SHRD memory source") ||
        !check_true(encoding_is_zero(&encoding), "SHRD memory source rollback")) {
        ctool_job_close(job);
        return 1;
    }

    insn.operands[1] = register_operand(CTOOL_X86_REG_GPR32, 7u);
    insn.operands[2] = register_operand(CTOOL_X86_REG_GPR8, 2u);
    (void)memset(&encoding, 0xa5, sizeof(encoding));
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn, CTOOL_X86_FORM_AUTO, &encoding);
    if (!check_status(status, CTOOL_ERR_INPUT, "SHRD wrong count register") ||
        !check_true(encoding_is_zero(&encoding), "SHRD wrong count register rollback")) {
        ctool_job_close(job);
        return 1;
    }

    insn.operands[2] = value_operand(CTOOL_X86_OPERAND_IMMEDIATE, 32u, 16u, constant(7u));
    (void)memset(&encoding, 0xa5, sizeof(encoding));
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn, CTOOL_X86_FORM_AUTO, &encoding);
    if (!check_status(status, CTOOL_ERR_INPUT, "SHRD serialized count width") ||
        !check_true(encoding_is_zero(&encoding), "SHRD serialized count width rollback")) {
        ctool_job_close(job);
        return 1;
    }

    insn.prefixes = CTOOL_X86_PREFIX_LOCK;
    insn.operands[2] = register_operand(CTOOL_X86_REG_GPR8, 1u);
    (void)memset(&encoding, 0xa5, sizeof(encoding));
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn, CTOOL_X86_FORM_AUTO, &encoding);
    if (!check_status(status, CTOOL_ERR_INPUT, "locked SHRD") ||
        !check_true(encoding_is_zero(&encoding), "locked SHRD rollback")) {
        ctool_job_close(job);
        return 1;
    }

    insn.prefixes = 0u;
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "SHRD same-job encode recovery") ||
        !bytes_equal(&encoding, active_stream, 3u, "SHRD same-job recovered bytes")) {
        ctool_job_close(job);
        return 1;
    }
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                              ctool_bytes(active_stream, (ctool_u32)sizeof(active_stream)), 3u,
                              &decoded);
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

    status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                              ctool_bytes(locked_active, (ctool_u32)sizeof(locked_active)), 0u,
                              &decoded);
    if (!check_status(status, CTOOL_OK, "locked SHRD decode") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_INVALID && decoded.consumed == 1u &&
                        decoded.encoding.size == 1u && decoded.encoding.bytes[0] == 0xf0u,
                    "locked SHRD invalid classification")) {
        ctool_job_close(job);
        return 1;
    }
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                              ctool_bytes(locked_active, (ctool_u32)sizeof(locked_active)), 1u,
                              &decoded);
    if (!check_status(status, CTOOL_OK, "post-lock SHRD recovery") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                        decoded.instruction.mnemonic == CTOOL_X86_MN_SHRD && decoded.consumed == 3u,
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
    static const ctool_u8 nop_memory16_canonical[] = {0x2eu, 0x66u, 0x0fu, 0x1fu, 0x84u,
                                                      0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
    static const ctool_u8 nop_address16[] = {0x67u, 0x0fu, 0x1fu, 0x40u, 0x7fu};
    static const ctool_u8 nop_mode16_wide[] = {0x66u, 0x67u, 0x0fu, 0x1fu, 0x84u,
                                               0x8bu, 0x78u, 0x56u, 0x34u, 0x12u};
    static const ctool_u8 compiler3[] = {0x0fu, 0x1fu, 0x00u};
    static const ctool_u8 compiler4[] = {0x0fu, 0x1fu, 0x40u, 0x00u};
    static const ctool_u8 compiler5[] = {0x0fu, 0x1fu, 0x44u, 0x00u, 0x00u};
    static const ctool_u8 compiler6[] = {0x66u, 0x0fu, 0x1fu, 0x44u, 0x00u, 0x00u};
    static const ctool_u8 compiler7[] = {0x0fu, 0x1fu, 0x80u, 0x00u, 0x00u, 0x00u, 0x00u};
    static const ctool_u8 compiler8[] = {0x0fu, 0x1fu, 0x84u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
    static const ctool_u8 compiler9[] = {0x66u, 0x0fu, 0x1fu, 0x84u, 0x00u,
                                         0x00u, 0x00u, 0x00u, 0x00u};
    static const ctool_u8 compiler10[] = {0x66u, 0x2eu, 0x0fu, 0x1fu, 0x84u,
                                          0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
    static const ctool_u8 lock_recovery[] = {0xf0u, 0x0fu, 0x1fu, 0x00u, 0xc3u};
    static const ctool_u8 repeat_nop[] = {0xf2u, 0x0fu, 0x1fu, 0x00u};
    static const ctool_u8 wrong_digit[] = {0x0fu, 0x1fu, 0x08u};
    static const ctool_u8 pause_bytes[] = {0xf3u, 0x90u};

    static const struct {
        const ctool_u8 *bytes;
        ctool_u32 size;
        const char *name;
    } compiler_forms[] = {
        {compiler3, (ctool_u32)sizeof(compiler3), "three-byte compiler NOP"},
        {compiler4, (ctool_u32)sizeof(compiler4), "four-byte compiler NOP"},
        {compiler5, (ctool_u32)sizeof(compiler5), "five-byte compiler NOP"},
        {compiler6, (ctool_u32)sizeof(compiler6), "six-byte compiler NOP"},
        {compiler7, (ctool_u32)sizeof(compiler7), "seven-byte compiler NOP"},
        {compiler8, (ctool_u32)sizeof(compiler8), "eight-byte compiler NOP"},
        {compiler9, (ctool_u32)sizeof(compiler9), "nine-byte compiler NOP"},
        {compiler10, (ctool_u32)sizeof(compiler10), "ten-byte compiler NOP"},
        {nop_override, (ctool_u32)sizeof(nop_override), "operand-size compiler NOP"}};

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
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "default NOP encode") ||
        !bytes_equal(&encoding, nop_default, (ctool_u8)sizeof(nop_default), "default NOP bytes")) {
        ctool_job_close(job);
        return 1;
    }

    insn = instruction(CTOOL_X86_MN_NOP, 16u, 32u, 0u);
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "32-bit mode operand-size NOP encode") ||
        !bytes_equal(&encoding, nop_override, (ctool_u8)sizeof(nop_override),
                     "32-bit mode operand-size NOP bytes")) {
        ctool_job_close(job);
        return 1;
    }
    insn = instruction(CTOOL_X86_MN_NOP, 32u, 16u, 0u);
    if (!encode(job, CTOOL_X86_MODE_16, &insn, &encoding, "16-bit mode operand-size NOP encode") ||
        !bytes_equal(&encoding, nop_override, (ctool_u8)sizeof(nop_override),
                     "16-bit mode operand-size NOP bytes")) {
        ctool_job_close(job);
        return 1;
    }

    insn = instruction(CTOOL_X86_MN_NOP, 32u, 32u, 0u);
    insn.operand_count = 1u;
    insn.operands[0] = register_operand(CTOOL_X86_REG_GPR32, 0u);
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "register NOP encode") ||
        !bytes_equal(&encoding, nop_register32, (ctool_u8)sizeof(nop_register32),
                     "register NOP bytes")) {
        ctool_job_close(job);
        return 1;
    }
    insn = instruction(CTOOL_X86_MN_NOP, 16u, 32u, 0u);
    insn.operand_count = 1u;
    insn.operands[0] = register_operand(CTOOL_X86_REG_GPR16, 0u);
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "word register NOP encode") ||
        !bytes_equal(&encoding, nop_register16, (ctool_u8)sizeof(nop_register16),
                     "word register NOP bytes")) {
        ctool_job_close(job);
        return 1;
    }

    insn = instruction(CTOOL_X86_MN_NOP, 32u, 32u, 0u);
    insn.operand_count = 1u;
    insn.operands[0] =
        memory_operand(32u, 32u, reg(CTOOL_X86_REG_NONE, 0u), reg(CTOOL_X86_REG_GPR32, 0u),
                       reg(CTOOL_X86_REG_NONE, 0u), 1u, 0, 0u);
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "memory NOP encode") ||
        !bytes_equal(&encoding, nop_memory32, (ctool_u8)sizeof(nop_memory32), "memory NOP bytes")) {
        ctool_job_close(job);
        return 1;
    }

    insn = instruction(CTOOL_X86_MN_NOP, 16u, 32u, 0u);
    insn.operand_count = 1u;
    insn.operands[0] =
        memory_operand(16u, 32u, reg(CTOOL_X86_REG_SEGMENT, 1u), reg(CTOOL_X86_REG_GPR32, 0u),
                       reg(CTOOL_X86_REG_GPR32, 0u), 1u, 0, 32u);
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "word compiler-shaped NOP encode") ||
        !bytes_equal(&encoding, nop_memory16_canonical, (ctool_u8)sizeof(nop_memory16_canonical),
                     "word compiler-shaped NOP bytes")) {
        ctool_job_close(job);
        return 1;
    }

    insn = instruction(CTOOL_X86_MN_NOP, 32u, 16u, 0u);
    insn.operand_count = 1u;
    insn.operands[0] =
        memory_operand(32u, 16u, reg(CTOOL_X86_REG_NONE, 0u), reg(CTOOL_X86_REG_GPR16, 3u),
                       reg(CTOOL_X86_REG_GPR16, 6u), 1u, 0x7f, 8u);
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "address-size NOP encode") ||
        !bytes_equal(&encoding, nop_address16, (ctool_u8)sizeof(nop_address16),
                     "address-size NOP bytes")) {
        ctool_job_close(job);
        return 1;
    }

    insn = instruction(CTOOL_X86_MN_NOP, 32u, 32u, 0u);
    insn.operand_count = 1u;
    insn.operands[0] =
        memory_operand(32u, 32u, reg(CTOOL_X86_REG_NONE, 0u), reg(CTOOL_X86_REG_GPR32, 3u),
                       reg(CTOOL_X86_REG_GPR32, 1u), 4u, 0x12345678, 32u);
    if (!encode(job, CTOOL_X86_MODE_16, &insn, &encoding, "16-bit mode wide NOP encode") ||
        !bytes_equal(&encoding, nop_mode16_wide, (ctool_u8)sizeof(nop_mode16_wide),
                     "16-bit mode wide NOP bytes")) {
        ctool_job_close(job);
        return 1;
    }

    for (index = 0u; index < (ctool_u32)(sizeof(compiler_forms) / sizeof(compiler_forms[0]));
         index++) {
        status = ctool_x86_decode(
            job, CTOOL_X86_MODE_32,
            ctool_bytes(compiler_forms[index].bytes, compiler_forms[index].size), 0u, &decoded);
        if (!check_status(status, CTOOL_OK, compiler_forms[index].name) ||
            !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                            decoded.instruction.mnemonic == CTOOL_X86_MN_NOP &&
                            decoded.consumed == compiler_forms[index].size &&
                            decoded.encoding.size == compiler_forms[index].size &&
                            decoded.encoding.form != CTOOL_X86_FORM_AUTO &&
                            memcmp(decoded.encoding.bytes, compiler_forms[index].bytes,
                                   compiler_forms[index].size) == 0,
                        compiler_forms[index].name)) {
            ctool_job_close(job);
            return 1;
        }
    }

    status = ctool_x86_decode(
        job, CTOOL_X86_MODE_32,
        ctool_bytes(nop_memory16_canonical, (ctool_u32)sizeof(nop_memory16_canonical)), 0u,
        &decoded);
    if (!check_status(status, CTOOL_OK, "compiler-shaped NOP semantic decode") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                        decoded.instruction.operand_bits == 16u &&
                        decoded.instruction.address_bits == 32u &&
                        decoded.instruction.operand_count == 1u &&
                        decoded.instruction.operands[0].kind == CTOOL_X86_OPERAND_MEMORY &&
                        decoded.instruction.operands[0].width_bits == 16u &&
                        decoded.instruction.operands[0].as.memory.segment.class_id ==
                            CTOOL_X86_REG_SEGMENT &&
                        decoded.instruction.operands[0].as.memory.segment.index == 1u &&
                        decoded.instruction.operands[0].as.memory.base.index == 0u &&
                        decoded.instruction.operands[0].as.memory.index.index == 0u &&
                        decoded.instruction.operands[0].as.memory.displacement.bits == 0u &&
                        decoded.instruction.operands[0].as.memory.displacement_bits == 32u &&
                        decoded.encoding.field_count == 1u &&
                        decoded.encoding.fields[0].kind == CTOOL_X86_FIELD_DISPLACEMENT &&
                        decoded.encoding.fields[0].byte_offset == 6u &&
                        decoded.encoding.fields[0].byte_width == 4u,
                    "compiler-shaped NOP semantics")) {
        ctool_job_close(job);
        return 1;
    }
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &decoded.instruction, decoded.encoding.form,
                              &replay);
    if (!check_status(status, CTOOL_OK, "compiler-shaped NOP requested-form replay") ||
        !bytes_equal(&replay, nop_memory16_canonical, (ctool_u8)sizeof(nop_memory16_canonical),
                     "compiler-shaped NOP requested-form bytes")) {
        ctool_job_close(job);
        return 1;
    }

    status =
        ctool_x86_decode(job, CTOOL_X86_MODE_16,
                         ctool_bytes(nop_override, (ctool_u32)sizeof(nop_override)), 0u, &decoded);
    if (!check_status(status, CTOOL_OK, "16-bit mode operand-size NOP decode") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                        decoded.instruction.operand_bits == 32u &&
                        decoded.instruction.operand_count == 0u,
                    "16-bit mode operand-size NOP semantics")) {
        ctool_job_close(job);
        return 1;
    }
    status = ctool_x86_encode(job, CTOOL_X86_MODE_16, &decoded.instruction, decoded.encoding.form,
                              &replay);
    if (!check_status(status, CTOOL_OK, "operand-size NOP requested-form replay") ||
        !bytes_equal(&replay, nop_override, (ctool_u8)sizeof(nop_override),
                     "operand-size NOP requested-form bytes")) {
        ctool_job_close(job);
        return 1;
    }

    for (cut = 1u; cut < (ctool_u32)sizeof(compiler10); cut++) {
        status =
            ctool_x86_decode(job, CTOOL_X86_MODE_32, ctool_bytes(compiler10, cut), 0u, &decoded);
        if (!check_status(status, CTOOL_OK, "padding NOP every-byte truncation") ||
            !check_true(decoded.kind == CTOOL_X86_DECODE_TRUNCATED && decoded.consumed == 0u &&
                            decoded.encoding.size == cut &&
                            memcmp(decoded.encoding.bytes, compiler10, (size_t)cut) == 0,
                        "padding NOP truncation retention")) {
            ctool_job_close(job);
            return 1;
        }
    }

    insn = instruction(CTOOL_X86_MN_NOP, 32u, 32u, CTOOL_X86_PREFIX_LOCK);
    insn.operand_count = 1u;
    insn.operands[0] =
        memory_operand(32u, 32u, reg(CTOOL_X86_REG_NONE, 0u), reg(CTOOL_X86_REG_GPR32, 0u),
                       reg(CTOOL_X86_REG_NONE, 0u), 1u, 0, 0u);
    (void)memset(&encoding, 0xa5, sizeof(encoding));
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn, CTOOL_X86_FORM_AUTO, &encoding);
    if (!check_status(status, CTOOL_ERR_INPUT, "locked padding NOP") ||
        !check_true(encoding_is_zero(&encoding), "locked padding NOP rollback")) {
        ctool_job_close(job);
        return 1;
    }
    insn.prefixes = CTOOL_X86_PREFIX_REPNE;
    (void)memset(&encoding, 0xa5, sizeof(encoding));
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn, CTOOL_X86_FORM_AUTO, &encoding);
    if (!check_status(status, CTOOL_ERR_INPUT, "repeat padding NOP") ||
        !check_true(encoding_is_zero(&encoding), "repeat padding NOP rollback")) {
        ctool_job_close(job);
        return 1;
    }
    insn = instruction(CTOOL_X86_MN_NOP, 16u, 32u, 0u);
    insn.operand_count = 1u;
    insn.operands[0] =
        memory_operand(32u, 32u, reg(CTOOL_X86_REG_NONE, 0u), reg(CTOOL_X86_REG_GPR32, 0u),
                       reg(CTOOL_X86_REG_NONE, 0u), 1u, 0, 0u);
    (void)memset(&encoding, 0xa5, sizeof(encoding));
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn, CTOOL_X86_FORM_AUTO, &encoding);
    if (!check_status(status, CTOOL_ERR_INPUT, "padding NOP width mismatch") ||
        !check_true(encoding_is_zero(&encoding), "padding NOP width mismatch rollback")) {
        ctool_job_close(job);
        return 1;
    }
    insn = instruction(CTOOL_X86_MN_NOP, 8u, 32u, 0u);
    insn.operand_count = 1u;
    insn.operands[0] =
        memory_operand(8u, 32u, reg(CTOOL_X86_REG_NONE, 0u), reg(CTOOL_X86_REG_GPR32, 0u),
                       reg(CTOOL_X86_REG_NONE, 0u), 1u, 0, 0u);
    (void)memset(&encoding, 0xa5, sizeof(encoding));
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn, CTOOL_X86_FORM_AUTO, &encoding);
    if (!check_status(status, CTOOL_ERR_INPUT, "byte padding NOP") ||
        !check_true(encoding_is_zero(&encoding), "byte padding NOP rollback")) {
        ctool_job_close(job);
        return 1;
    }

    status =
        ctool_x86_decode(job, CTOOL_X86_MODE_32,
                         ctool_bytes(wrong_digit, (ctool_u32)sizeof(wrong_digit)), 0u, &decoded);
    if (!check_status(status, CTOOL_OK, "padding NOP wrong ModRM digit") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_UNKNOWN && decoded.consumed == 1u,
                    "padding NOP wrong ModRM digit classification")) {
        ctool_job_close(job);
        return 1;
    }
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                              ctool_bytes(repeat_nop, (ctool_u32)sizeof(repeat_nop)), 0u, &decoded);
    if (!check_status(status, CTOOL_OK, "padding NOP repeat prefix") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_UNKNOWN && decoded.consumed == 1u,
                    "padding NOP repeat prefix classification")) {
        ctool_job_close(job);
        return 1;
    }
    status =
        ctool_x86_decode(job, CTOOL_X86_MODE_32,
                         ctool_bytes(pause_bytes, (ctool_u32)sizeof(pause_bytes)), 0u, &decoded);
    if (!check_status(status, CTOOL_OK, "PAUSE remains distinct") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                        decoded.instruction.mnemonic == CTOOL_X86_MN_PAUSE &&
                        decoded.consumed == sizeof(pause_bytes),
                    "PAUSE remains distinct from NOP")) {
        ctool_job_close(job);
        return 1;
    }

    status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                              ctool_bytes(lock_recovery, (ctool_u32)sizeof(lock_recovery)), 0u,
                              &decoded);
    if (!check_status(status, CTOOL_OK, "locked padding NOP decode") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_INVALID && decoded.consumed == 1u,
                    "locked padding NOP classification")) {
        ctool_job_close(job);
        return 1;
    }
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                              ctool_bytes(lock_recovery, (ctool_u32)sizeof(lock_recovery)), 1u,
                              &decoded);
    if (!check_status(status, CTOOL_OK, "padding NOP recovery decode") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                        decoded.instruction.mnemonic == CTOOL_X86_MN_NOP && decoded.consumed == 3u,
                    "padding NOP recovery semantics")) {
        ctool_job_close(job);
        return 1;
    }
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                              ctool_bytes(lock_recovery, (ctool_u32)sizeof(lock_recovery)), 4u,
                              &decoded);
    if (!check_status(status, CTOOL_OK, "padding NOP following return") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                        decoded.instruction.mnemonic == CTOOL_X86_MN_RET && decoded.consumed == 1u,
                    "padding NOP following return semantics")) {
        ctool_job_close(job);
        return 1;
    }

    ctool_job_close(job);
    (void)puts("padding-nops: ok");
    return 0;
}

static int run_clang_padding_nops(void) {
    static const ctool_u8 longest[] = {0x66u, 0x66u, 0x66u, 0x66u, 0x66u, 0x66u, 0x2eu, 0x0fu,
                                       0x1fu, 0x84u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
    static const ctool_u8 canonical[] = {0x2eu, 0x66u, 0x0fu, 0x1fu, 0x84u,
                                         0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
    static const ctool_u8 wrong_segment[] = {0x66u, 0x66u, 0x3eu, 0x0fu, 0x1fu, 0x84u,
                                             0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
    static const ctool_u8 wrong_opcode[] = {0x66u, 0x66u, 0x2eu, 0x0fu, 0x1eu, 0x84u,
                                            0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
    static const ctool_u8 wrong_modrm[] = {0x66u, 0x66u, 0x2eu, 0x0fu, 0x1fu, 0x8cu,
                                           0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
    static const ctool_u8 wrong_sib[] = {0x66u, 0x66u, 0x2eu, 0x0fu, 0x1fu, 0x84u,
                                         0x01u, 0x00u, 0x00u, 0x00u, 0x00u};
    static const ctool_u8 nonzero_displacement[] = {0x66u, 0x66u, 0x2eu, 0x0fu, 0x1fu, 0x84u,
                                                    0x00u, 0x01u, 0x00u, 0x00u, 0x00u};
    static const ctool_u8 missing_segment[] = {0x66u, 0x66u, 0x0fu, 0x1fu, 0x84u,
                                               0x00u, 0x00u, 0x00u, 0x00u, 0x00u};
    static const ctool_u8 seven_prefixes[] = {0x66u, 0x66u, 0x66u, 0x66u, 0x66u, 0x66u,
                                              0x66u, 0x2eu, 0x0fu, 0x1fu, 0x84u, 0x00u,
                                              0x00u, 0x00u, 0x00u, 0x00u};
    static const ctool_u8 duplicate_short_nop[] = {0x66u, 0x66u, 0x90u};

    static const struct {
        const ctool_u8 *bytes;
        ctool_u32 size;
        const char *name;
    } near_misses[] = {
        {wrong_segment, (ctool_u32)sizeof(wrong_segment), "Clang padding wrong segment"},
        {wrong_opcode, (ctool_u32)sizeof(wrong_opcode), "Clang padding wrong opcode"},
        {wrong_modrm, (ctool_u32)sizeof(wrong_modrm), "Clang padding wrong ModRM"},
        {wrong_sib, (ctool_u32)sizeof(wrong_sib), "Clang padding wrong SIB"},
        {nonzero_displacement, (ctool_u32)sizeof(nonzero_displacement),
         "Clang padding nonzero displacement"},
        {missing_segment, (ctool_u32)sizeof(missing_segment), "Clang padding missing segment"},
        {seven_prefixes, (ctool_u32)sizeof(seven_prefixes), "Clang padding seven prefixes"},
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
        status = ctool_x86_decode(job, CTOOL_X86_MODE_32, ctool_bytes(pattern, pattern_size), 0u,
                                  &decoded);
        if (!check_status(status, CTOOL_OK, "exact Clang padding decode") ||
            !check_true(
                decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                    decoded.instruction.mnemonic == CTOOL_X86_MN_NOP &&
                    decoded.instruction.operand_bits == 16u &&
                    decoded.instruction.address_bits == 32u && decoded.instruction.prefixes == 0u &&
                    decoded.instruction.operand_count == 1u &&
                    decoded.instruction.operands[0].kind == CTOOL_X86_OPERAND_MEMORY &&
                    decoded.instruction.operands[0].width_bits == 16u &&
                    decoded.instruction.operands[0].as.memory.address_bits == 32u &&
                    decoded.instruction.operands[0].as.memory.segment.class_id ==
                        CTOOL_X86_REG_SEGMENT &&
                    decoded.instruction.operands[0].as.memory.segment.index == 1u &&
                    decoded.instruction.operands[0].as.memory.base.class_id ==
                        CTOOL_X86_REG_GPR32 &&
                    decoded.instruction.operands[0].as.memory.base.index == 0u &&
                    decoded.instruction.operands[0].as.memory.index.class_id ==
                        CTOOL_X86_REG_GPR32 &&
                    decoded.instruction.operands[0].as.memory.index.index == 0u &&
                    decoded.instruction.operands[0].as.memory.scale == 1u &&
                    decoded.instruction.operands[0].as.memory.displacement.kind ==
                        CTOOL_X86_VALUE_CONSTANT &&
                    decoded.instruction.operands[0].as.memory.displacement.bits == 0u &&
                    decoded.instruction.operands[0].as.memory.displacement_bits == 32u &&
                    decoded.encoding.form == CTOOL_X86_FORM_AUTO &&
                    decoded.encoding.size == pattern_size && decoded.encoding.field_count == 1u &&
                    decoded.encoding.fields[0].kind == CTOOL_X86_FIELD_DISPLACEMENT &&
                    decoded.encoding.fields[0].relocation == CTOOL_X86_RELOC_NONE &&
                    decoded.encoding.fields[0].operand_index == 0u &&
                    decoded.encoding.fields[0].byte_offset == prefix_count + 5u &&
                    decoded.encoding.fields[0].byte_width == 4u &&
                    decoded.consumed == pattern_size &&
                    memcmp(decoded.encoding.bytes, pattern, (size_t)pattern_size) == 0,
                "exact Clang padding semantics")) {
            ctool_job_close(job);
            return 1;
        }

        status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &decoded.instruction,
                                  decoded.encoding.form, &encoding);
        if (!check_status(status, CTOOL_OK, "Clang padding canonical re-encode") ||
            !bytes_equal(&encoding, canonical, (ctool_u8)sizeof(canonical),
                         "Clang padding canonical bytes") ||
            !check_true(encoding.size != pattern_size, "Clang padding is decode-only")) {
            ctool_job_close(job);
            return 1;
        }

        (void)memcpy(with_return, pattern, (size_t)pattern_size);
        with_return[pattern_size] = 0xc3u;
        status =
            ctool_x86_decode(job, CTOOL_X86_MODE_32, ctool_bytes(with_return, pattern_size + 1u),
                             pattern_size, &decoded);
        if (!check_status(status, CTOOL_OK, "Clang padding following return decode") ||
            !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                            decoded.instruction.mnemonic == CTOOL_X86_MN_RET &&
                            decoded.consumed == 1u,
                        "Clang padding following return boundary")) {
            ctool_job_close(job);
            return 1;
        }

        status = ctool_x86_decode(job, CTOOL_X86_MODE_16, ctool_bytes(pattern, pattern_size), 0u,
                                  &decoded);
        if (!check_status(status, CTOOL_OK, "Clang padding 16-bit mode rejection") ||
            !check_true(decoded.kind == CTOOL_X86_DECODE_INVALID && decoded.consumed == 1u &&
                            decoded.encoding.size == 1u && decoded.encoding.bytes[0] == 0x66u,
                        "Clang padding remains mode-specific")) {
            ctool_job_close(job);
            return 1;
        }

        for (cut = 1u; cut < pattern_size; cut++) {
            status =
                ctool_x86_decode(job, CTOOL_X86_MODE_32, ctool_bytes(pattern, cut), 0u, &decoded);
            if (!check_status(status, CTOOL_OK, "Clang padding every-byte cut") ||
                !check_true(cut == 1u ? decoded.kind == CTOOL_X86_DECODE_TRUNCATED &&
                                            decoded.consumed == 0u && decoded.encoding.size == 1u &&
                                            decoded.encoding.bytes[0] == 0x66u
                                      : decoded.kind == CTOOL_X86_DECODE_INVALID &&
                                            decoded.consumed == 1u && decoded.encoding.size == 1u &&
                                            decoded.encoding.bytes[0] == 0x66u,
                            "Clang padding cut classification")) {
                ctool_job_close(job);
                return 1;
            }
        }
    }

    for (index = 0u; index < (ctool_u32)(sizeof(near_misses) / sizeof(near_misses[0])); index++) {
        status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                                  ctool_bytes(near_misses[index].bytes, near_misses[index].size),
                                  0u, &decoded);
        if (!check_status(status, CTOOL_OK, near_misses[index].name) ||
            !check_true(decoded.kind == CTOOL_X86_DECODE_INVALID && decoded.consumed == 1u &&
                            decoded.encoding.size == 1u && decoded.encoding.bytes[0] == 0x66u,
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

static int expect_conditional_move_encode_failure(ctool_job_t *job,
                                                  const ctool_x86_instruction_t *insn,
                                                  const char *operation) {
    ctool_x86_encoding_t encoding;
    ctool_status_t status;
    (void)memset(&encoding, 0xa5, sizeof(encoding));
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, insn, CTOOL_X86_FORM_AUTO, &encoding);
    return check_status(status, CTOOL_ERR_INPUT, operation) &&
           check_true(encoding_is_zero(&encoding), operation);
}

static int run_conditional_moves(void) {
    static const conditional_move_case_t cases[] = {
        {CTOOL_X86_MN_CMOVO, "cmovo", 0x40u},   {CTOOL_X86_MN_CMOVNO, "cmovno", 0x41u},
        {CTOOL_X86_MN_CMOVB, "cmovb", 0x42u},   {CTOOL_X86_MN_CMOVAE, "cmovae", 0x43u},
        {CTOOL_X86_MN_CMOVE, "cmove", 0x44u},   {CTOOL_X86_MN_CMOVNE, "cmovne", 0x45u},
        {CTOOL_X86_MN_CMOVBE, "cmovbe", 0x46u}, {CTOOL_X86_MN_CMOVA, "cmova", 0x47u},
        {CTOOL_X86_MN_CMOVS, "cmovs", 0x48u},   {CTOOL_X86_MN_CMOVNS, "cmovns", 0x49u},
        {CTOOL_X86_MN_CMOVP, "cmovp", 0x4au},   {CTOOL_X86_MN_CMOVNP, "cmovnp", 0x4bu},
        {CTOOL_X86_MN_CMOVL, "cmovl", 0x4cu},   {CTOOL_X86_MN_CMOVGE, "cmovge", 0x4du},
        {CTOOL_X86_MN_CMOVLE, "cmovle", 0x4eu}, {CTOOL_X86_MN_CMOVG, "cmovg", 0x4fu}};
    static const conditional_move_alias_t aliases[] = {
        {"cmovc", CTOOL_X86_MN_CMOVB},   {"cmovnae", CTOOL_X86_MN_CMOVB},
        {"cmovnc", CTOOL_X86_MN_CMOVAE}, {"cmovnb", CTOOL_X86_MN_CMOVAE},
        {"cmovz", CTOOL_X86_MN_CMOVE},   {"cmovnz", CTOOL_X86_MN_CMOVNE},
        {"cmovna", CTOOL_X86_MN_CMOVBE}, {"cmovnbe", CTOOL_X86_MN_CMOVA},
        {"cmovpe", CTOOL_X86_MN_CMOVP},  {"cmovpo", CTOOL_X86_MN_CMOVNP},
        {"cmovnge", CTOOL_X86_MN_CMOVL}, {"cmovnl", CTOOL_X86_MN_CMOVGE},
        {"cmovng", CTOOL_X86_MN_CMOVLE}, {"cmovnle", CTOOL_X86_MN_CMOVG}};
    static const ctool_u8 illegal_prefix_bytes[] = {0xf0u, 0xf3u, 0xf2u};
    static const ctool_u8 semantic_prefixes[] = {CTOOL_X86_PREFIX_LOCK, CTOOL_X86_PREFIX_REP,
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

    for (alias_index = 0u; alias_index < (ctool_u32)(sizeof(aliases) / sizeof(aliases[0]));
         alias_index++) {
        status = ctool_x86_mnemonic_from_name(ctool_string(aliases[alias_index].name), &found);
        if (!check_status(status, CTOOL_OK, aliases[alias_index].name) ||
            !check_true(found == aliases[alias_index].mnemonic,
                        "conditional move alias canonicalization")) {
            ctool_job_close(job);
            return 1;
        }
    }

    for (case_index = 0u; case_index < (ctool_u32)(sizeof(cases) / sizeof(cases[0]));
         case_index++) {
        ctool_string_t canonical;
        status = ctool_x86_mnemonic_from_name(ctool_string(cases[case_index].name), &found);
        canonical = ctool_x86_mnemonic_name(cases[case_index].mnemonic);
        if (!check_status(status, CTOOL_OK, cases[case_index].name) ||
            !check_true(found == cases[case_index].mnemonic &&
                            canonical.size == strlen(cases[case_index].name) &&
                            memcmp(canonical.data, cases[case_index].name, canonical.size) == 0,
                        "conditional move canonical name")) {
            ctool_job_close(job);
            return 1;
        }
        for (mode_index = 0u; mode_index < 2u; mode_index++) {
            ctool_x86_mode_t mode = mode_index == 0u ? CTOOL_X86_MODE_16 : CTOOL_X86_MODE_32;
            ctool_u16 address_bits = mode == CTOOL_X86_MODE_16 ? 16u : 32u;
            for (width_index = 0u; width_index < 2u; width_index++) {
                ctool_u16 width_bits = width_index == 0u ? 16u : 32u;
                ctool_x86_reg_class_t register_class =
                    width_bits == 16u ? CTOOL_X86_REG_GPR16 : CTOOL_X86_REG_GPR32;
                for (source_index = 0u; source_index < 2u; source_index++) {
                    ctool_u8 expected[6];
                    ctool_u8 expected_size = 0u;
                    ctool_bool memory_source = source_index == 0u ? CTOOL_FALSE : CTOOL_TRUE;
                    ctool_x86_form_t form;
                    insn = instruction(cases[case_index].mnemonic, width_bits, address_bits, 0u);
                    insn.operand_count = 2u;
                    insn.operands[0] = register_operand(register_class, 0u);
                    if (memory_source == CTOOL_FALSE) {
                        insn.operands[1] = register_operand(register_class, 1u);
                    } else if (mode == CTOOL_X86_MODE_16) {
                        insn.operands[1] =
                            memory_operand(width_bits, 16u, reg(CTOOL_X86_REG_NONE, 0u),
                                           reg(CTOOL_X86_REG_GPR16, 3u),
                                           reg(CTOOL_X86_REG_GPR16, 6u), 1u, 0x7f, 8u);
                    } else {
                        insn.operands[1] =
                            memory_operand(width_bits, 32u, reg(CTOOL_X86_REG_NONE, 0u),
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
                        expected[expected_size++] = mode == CTOOL_X86_MODE_16 ? 0x40u : 0x43u;
                        expected[expected_size++] = 0x7fu;
                    }
                    if (!encode(job, mode, &insn, &encoding, "conditional move encode") ||
                        !bytes_equal(&encoding, expected, expected_size,
                                     "conditional move exact bytes")) {
                        (void)fprintf(stderr, "conditional move case failed: %s\n",
                                      cases[case_index].name);
                        ctool_job_close(job);
                        return 1;
                    }
                    status = ctool_x86_decode(job, mode, ctool_bytes(expected, expected_size), 0u,
                                              &decoded);
                    if (!check_status(status, CTOOL_OK, "conditional move decode") ||
                        !check_true(
                            decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                                decoded.consumed == expected_size &&
                                decoded.instruction.mnemonic == cases[case_index].mnemonic &&
                                decoded.instruction.operand_bits == width_bits &&
                                decoded.instruction.operand_count == 2u &&
                                decoded.instruction.operands[0].kind ==
                                    CTOOL_X86_OPERAND_REGISTER &&
                                decoded.instruction.operands[0].as.reg.class_id == register_class &&
                                decoded.instruction.operands[0].as.reg.index == 0u &&
                                decoded.instruction.operands[1].kind ==
                                    (memory_source == CTOOL_FALSE ? CTOOL_X86_OPERAND_REGISTER
                                                                  : CTOOL_X86_OPERAND_MEMORY) &&
                                decoded.encoding.form != CTOOL_X86_FORM_AUTO,
                            "conditional move decode semantics")) {
                        ctool_job_close(job);
                        return 1;
                    }
                    if (memory_source == CTOOL_FALSE) {
                        if (!check_true(decoded.instruction.operands[1].as.reg.class_id ==
                                                register_class &&
                                            decoded.instruction.operands[1].as.reg.index == 1u,
                                        "conditional move register source")) {
                            ctool_job_close(job);
                            return 1;
                        }
                    } else if (!check_true(
                                   decoded.instruction.operands[1].width_bits == width_bits &&
                                       decoded.instruction.operands[1].as.memory.address_bits ==
                                           address_bits,
                                   "conditional move memory source")) {
                        ctool_job_close(job);
                        return 1;
                    }
                    form = decoded.encoding.form;
                    status = ctool_x86_encode(job, mode, &decoded.instruction, form, &replay);
                    if (!check_status(status, CTOOL_OK, "conditional move requested form") ||
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
    if (!expect_conditional_move_encode_failure(job, &insn, "conditional move byte operands")) {
        ctool_job_close(job);
        return 1;
    }
    insn = instruction(CTOOL_X86_MN_CMOVNE, 32u, 32u, 0u);
    insn.operand_count = 2u;
    insn.operands[0] = register_operand(CTOOL_X86_REG_GPR32, 0u);
    insn.operands[1] = value_operand(CTOOL_X86_OPERAND_IMMEDIATE, 32u, 32u, constant(1u));
    if (!expect_conditional_move_encode_failure(job, &insn, "conditional move immediate source")) {
        ctool_job_close(job);
        return 1;
    }
    insn.operands[1] = register_operand(CTOOL_X86_REG_GPR16, 1u);
    if (!expect_conditional_move_encode_failure(job, &insn, "conditional move width mismatch")) {
        ctool_job_close(job);
        return 1;
    }
    insn.operands[1] = register_operand(CTOOL_X86_REG_GPR32, 1u);
    for (prefix_index = 0u;
         prefix_index < (ctool_u32)(sizeof(semantic_prefixes) / sizeof(semantic_prefixes[0]));
         prefix_index++) {
        insn.prefixes = semantic_prefixes[prefix_index];
        if (!expect_conditional_move_encode_failure(job, &insn,
                                                    "conditional move semantic prefix")) {
            ctool_job_close(job);
            return 1;
        }
    }

    status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                              ctool_bytes(truncated_opcode, (ctool_u32)sizeof(truncated_opcode)),
                              0u, &decoded);
    if (!check_status(status, CTOOL_OK, "conditional move truncated opcode") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_TRUNCATED && decoded.consumed == 0u &&
                        decoded.encoding.size == sizeof(truncated_opcode),
                    "conditional move truncated opcode retention")) {
        ctool_job_close(job);
        return 1;
    }
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                              ctool_bytes(truncated_modrm, (ctool_u32)sizeof(truncated_modrm)), 0u,
                              &decoded);
    if (!check_status(status, CTOOL_OK, "conditional move truncated ModRM") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_TRUNCATED && decoded.consumed == 0u &&
                        decoded.encoding.size == sizeof(truncated_modrm),
                    "conditional move truncated ModRM retention")) {
        ctool_job_close(job);
        return 1;
    }
    for (prefix_index = 0u;
         prefix_index < (ctool_u32)(sizeof(illegal_prefix_bytes) / sizeof(illegal_prefix_bytes[0]));
         prefix_index++) {
        ctool_u8 illegal[4];
        illegal[0] = illegal_prefix_bytes[prefix_index];
        illegal[1] = 0x0fu;
        illegal[2] = 0x45u;
        illegal[3] = 0xc1u;
        status = ctool_x86_decode(job, CTOOL_X86_MODE_32, ctool_bytes(illegal, 4u), 0u, &decoded);
        if (!check_status(status, CTOOL_OK, "conditional move illegal prefix decode") ||
            !check_true(decoded.kind == (prefix_index == 0u ? CTOOL_X86_DECODE_INVALID
                                                            : CTOOL_X86_DECODE_UNKNOWN) &&
                            decoded.consumed == 1u && decoded.encoding.size == 1u &&
                            decoded.encoding.bytes[0] == illegal[0],
                        "conditional move illegal prefix classification")) {
            ctool_job_close(job);
            return 1;
        }
    }

    status =
        ctool_x86_decode(job, CTOOL_X86_MODE_32,
                         ctool_bytes(valid_bytes, (ctool_u32)sizeof(valid_bytes)), 0u, &decoded);
    if (!check_status(status, CTOOL_OK, "conditional move same-job recovery") ||
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
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn, valid_form, &encoding);
    if (!check_status(status, CTOOL_ERR_INPUT, "conditional move mismatched requested form") ||
        !check_true(encoding_is_zero(&encoding),
                    "conditional move mismatched form zeroed output")) {
        ctool_job_close(job);
        return 1;
    }
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &decoded.instruction, valid_form, &encoding);
    if (!check_status(status, CTOOL_OK, "conditional move recovery requested form") ||
        !bytes_equal(&encoding, valid_bytes, (ctool_u8)sizeof(valid_bytes),
                     "conditional move recovered exact bytes")) {
        ctool_job_close(job);
        return 1;
    }

    ctool_job_close(job);
    (void)printf("conditional-moves: ok\n");
    return 0;
}

static int expect_parity_setcc_encode_failure(ctool_job_t *job, const ctool_x86_instruction_t *insn,
                                              const char *operation) {
    ctool_x86_encoding_t encoding;
    ctool_status_t status;
    (void)memset(&encoding, 0xa5, sizeof(encoding));
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, insn, CTOOL_X86_FORM_AUTO, &encoding);
    return check_status(status, CTOOL_ERR_INPUT, operation) &&
           check_true(encoding_is_zero(&encoding), operation);
}

static int run_parity_setcc(void) {
    static const char *const names[] = {"setp", "setnp"};
    static const char *const excluded_aliases[] = {"setpe", "setpo"};
    static const ctool_u8 opcodes[] = {0x9au, 0x9bu};
    static const ctool_u8 semantic_prefixes[] = {CTOOL_X86_PREFIX_LOCK, CTOOL_X86_PREFIX_REP,
                                                 CTOOL_X86_PREFIX_REPNE};
    static const ctool_u8 truncated_memory[] = {0x67u, 0x0fu, 0x9au, 0x84u, 0x8bu,
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
        status = ctool_x86_mnemonic_from_name(ctool_string(names[mnemonic_index]),
                                              &mnemonics[mnemonic_index]);
        if (!check_status(status, CTOOL_OK, names[mnemonic_index])) {
            ctool_job_close(job);
            return 1;
        }
        canonical = ctool_x86_mnemonic_name(mnemonics[mnemonic_index]);
        if (!check_true(canonical.size == strlen(names[mnemonic_index]) &&
                            memcmp(canonical.data, names[mnemonic_index], canonical.size) == 0,
                        "parity SETcc canonical name")) {
            ctool_job_close(job);
            return 1;
        }
    }

    for (alias_index = 0u;
         alias_index < (ctool_u32)(sizeof(excluded_aliases) / sizeof(excluded_aliases[0]));
         alias_index++) {
        status =
            ctool_x86_mnemonic_from_name(ctool_string(excluded_aliases[alias_index]), &excluded);
        if (!check_status(status, CTOOL_ERR_NOT_FOUND, "parity SETcc excluded alias")) {
            ctool_job_close(job);
            return 1;
        }
    }

    for (mnemonic_index = 0u; mnemonic_index < 2u; mnemonic_index++) {
        for (mode_index = 0u; mode_index < 2u; mode_index++) {
            ctool_x86_mode_t mode = mode_index == 0u ? CTOOL_X86_MODE_16 : CTOOL_X86_MODE_32;
            for (operand_index = 0u; operand_index < 3u; operand_index++) {
                ctool_u8 expected[10];
                ctool_u8 expected_size = 0u;
                ctool_u16 address_bits = mode == CTOOL_X86_MODE_16 ? 16u : 32u;
                ctool_x86_operand_kind_t expected_kind =
                    operand_index == 0u ? CTOOL_X86_OPERAND_REGISTER : CTOOL_X86_OPERAND_MEMORY;
                insn = instruction(mnemonics[mnemonic_index], 8u, address_bits, 0u);
                insn.operand_count = 1u;
                if (operand_index == 0u) {
                    insn.operands[0] = register_operand(CTOOL_X86_REG_GPR8, 2u);
                } else if ((operand_index == 1u && mode == CTOOL_X86_MODE_16) ||
                           (operand_index == 2u && mode == CTOOL_X86_MODE_32)) {
                    address_bits = 16u;
                    insn.address_bits = address_bits;
                    insn.operands[0] = memory_operand(8u, address_bits, reg(CTOOL_X86_REG_NONE, 0u),
                                                      reg(CTOOL_X86_REG_GPR16, 3u),
                                                      reg(CTOOL_X86_REG_GPR16, 6u), 1u, 0x7f, 8u);
                } else {
                    address_bits = 32u;
                    insn.address_bits = address_bits;
                    insn.operands[0] = memory_operand(
                        8u, address_bits, reg(CTOOL_X86_REG_NONE, 0u), reg(CTOOL_X86_REG_GPR32, 3u),
                        reg(CTOOL_X86_REG_GPR32, 1u), 4u, 0x12345678, 32u);
                }
                if (operand_index != 0u && ((mode == CTOOL_X86_MODE_16 && address_bits == 32u) ||
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
                    !bytes_equal(&encoding, expected, expected_size, "parity SETcc exact bytes")) {
                    ctool_job_close(job);
                    return 1;
                }
                status =
                    ctool_x86_decode(job, mode, ctool_bytes(expected, expected_size), 0u, &decoded);
                if (!check_status(status, CTOOL_OK, "parity SETcc decode") ||
                    !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
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
                    if (!check_true(decoded.instruction.operands[0].as.reg.class_id ==
                                            CTOOL_X86_REG_GPR8 &&
                                        decoded.instruction.operands[0].as.reg.index == 2u,
                                    "parity SETcc register operand")) {
                        ctool_job_close(job);
                        return 1;
                    }
                } else if (!check_true(decoded.instruction.operands[0].width_bits == 8u &&
                                           decoded.instruction.operands[0].as.memory.address_bits ==
                                               address_bits,
                                       "parity SETcc memory operand")) {
                    ctool_job_close(job);
                    return 1;
                }
                status = ctool_x86_encode(job, mode, &decoded.instruction, decoded.encoding.form,
                                          &replay);
                if (!check_status(status, CTOOL_OK, "parity SETcc requested form") ||
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
    if (!expect_parity_setcc_encode_failure(job, &insn, "parity SETcc non-byte register")) {
        ctool_job_close(job);
        return 1;
    }
    insn = instruction(mnemonics[1], 8u, 32u, 0u);
    insn.operand_count = 1u;
    insn.operands[0] = value_operand(CTOOL_X86_OPERAND_IMMEDIATE, 8u, 8u, constant(1u));
    if (!expect_parity_setcc_encode_failure(job, &insn, "parity SETcc immediate operand")) {
        ctool_job_close(job);
        return 1;
    }
    insn.operand_count = 0u;
    if (!expect_parity_setcc_encode_failure(job, &insn, "parity SETcc missing operand")) {
        ctool_job_close(job);
        return 1;
    }
    insn = instruction(mnemonics[0], 8u, 32u, 0u);
    insn.operand_count = 1u;
    insn.operands[0] = register_operand(CTOOL_X86_REG_GPR8, 2u);
    for (prefix_index = 0u;
         prefix_index < (ctool_u32)(sizeof(semantic_prefixes) / sizeof(semantic_prefixes[0]));
         prefix_index++) {
        insn.prefixes = semantic_prefixes[prefix_index];
        if (!expect_parity_setcc_encode_failure(job, &insn, "parity SETcc semantic prefix")) {
            ctool_job_close(job);
            return 1;
        }
    }

    for (cut = 1u; cut < (ctool_u32)sizeof(truncated_memory); cut++) {
        status = ctool_x86_decode(job, CTOOL_X86_MODE_16, ctool_bytes(truncated_memory, cut), 0u,
                                  &decoded);
        if (!check_status(status, CTOOL_OK, "parity SETcc every-byte truncation") ||
            !check_true(decoded.kind == CTOOL_X86_DECODE_TRUNCATED && decoded.consumed == 0u &&
                            decoded.encoding.size == cut &&
                            memcmp(decoded.encoding.bytes, truncated_memory, (size_t)cut) == 0,
                        "parity SETcc truncation retention")) {
            ctool_job_close(job);
            return 1;
        }
    }

    status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
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
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn, setp_form, &encoding);
    if (!check_status(status, CTOOL_ERR_INPUT, "parity SETcc mismatched requested form") ||
        !check_true(encoding_is_zero(&encoding), "parity SETcc mismatched form rollback")) {
        ctool_job_close(job);
        return 1;
    }
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &decoded.instruction, setp_form, &encoding);
    if (!check_status(status, CTOOL_OK, "parity SETcc same-job recovery") ||
        !bytes_equal(&encoding, valid_setp, (ctool_u8)sizeof(valid_setp),
                     "parity SETcc recovered exact bytes")) {
        ctool_job_close(job);
        return 1;
    }

    ctool_job_close(job);
    (void)puts("parity-setcc: ok");
    return 0;
}
