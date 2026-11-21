/**
 * @file x86_operand_contract.inc
 * @brief Check addressing, relocations, and system and SIMD operands.
 */

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
    memory = memory_operand(16u, 16u, reg(CTOOL_X86_REG_NONE, 0u), reg(CTOOL_X86_REG_GPR16, 3u),
                            reg(CTOOL_X86_REG_GPR16, 6u), 1u, 0x7f, 8u);
    insn.operands[1] = memory;
    if (!encode(job, CTOOL_X86_MODE_16, &insn, &encoding, "16-bit address encode") ||
        !bytes_equal(&encoding, addr16_bytes, (ctool_u8)sizeof(addr16_bytes),
                     "16-bit address bytes")) {
        ctool_job_close(job);
        return 1;
    }
    insn.operands[1] =
        memory_operand(16u, 16u, reg(CTOOL_X86_REG_NONE, 0u), reg(CTOOL_X86_REG_GPR16, 6u),
                       reg(CTOOL_X86_REG_GPR16, 3u), 1u, 0x7f, 8u);
    if (!encode(job, CTOOL_X86_MODE_16, &insn, &encoding, "commuted 16-bit address encode") ||
        !bytes_equal(&encoding, addr16_bytes, (ctool_u8)sizeof(addr16_bytes),
                     "commuted 16-bit address bytes")) {
        ctool_job_close(job);
        return 1;
    }
    status =
        ctool_x86_decode(job, CTOOL_X86_MODE_16,
                         ctool_bytes(addr16_bytes, (ctool_u32)sizeof(addr16_bytes)), 0u, &decoded);
    if (!check_status(status, CTOOL_OK, "16-bit address decode") ||
        !check_true(
            decoded.kind == CTOOL_X86_DECODE_KNOWN && decoded.instruction.operand_bits == 16u &&
                decoded.instruction.address_bits == 16u &&
                decoded.instruction.operands[1].kind == CTOOL_X86_OPERAND_MEMORY &&
                decoded.instruction.operands[1].as.memory.base.class_id == CTOOL_X86_REG_GPR16 &&
                decoded.instruction.operands[1].as.memory.base.index == 3u &&
                decoded.instruction.operands[1].as.memory.index.index == 6u &&
                decoded.instruction.operands[1].as.memory.displacement.bits == 0x7fu,
            "16-bit address semantics")) {
        ctool_job_close(job);
        return 1;
    }
    status = ctool_x86_decode(job, CTOOL_X86_MODE_16,
                              ctool_bytes(negative_disp16, (ctool_u32)sizeof(negative_disp16)), 0u,
                              &decoded);
    if (!check_status(status, CTOOL_OK, "signed disp16 decode") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                        decoded.instruction.operands[1].kind == CTOOL_X86_OPERAND_MEMORY &&
                        decoded.instruction.operands[1].as.memory.displacement.bits ==
                            0xffffff00u &&
                        decoded.encoding.fields[0].encoded_addend == -256,
                    "signed disp16 semantics")) {
        ctool_job_close(job);
        return 1;
    }

    insn = instruction(CTOOL_X86_MN_MOV, 32u, 32u, 0u);
    insn.operand_count = 2u;
    insn.operands[0] = register_operand(CTOOL_X86_REG_GPR32, 0u);
    insn.operands[1] =
        memory_operand(32u, 32u, reg(CTOOL_X86_REG_NONE, 0u), reg(CTOOL_X86_REG_GPR32, 3u),
                       reg(CTOOL_X86_REG_GPR32, 1u), 4u, 0x10, 8u);
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "SIB encode") ||
        !bytes_equal(&encoding, sib_bytes, (ctool_u8)sizeof(sib_bytes), "SIB bytes")) {
        ctool_job_close(job);
        return 1;
    }
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                              ctool_bytes(sib_bytes, (ctool_u32)sizeof(sib_bytes)), 0u, &decoded);
    if (!check_status(status, CTOOL_OK, "SIB decode") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                        decoded.instruction.operands[1].as.memory.base.index == 3u &&
                        decoded.instruction.operands[1].as.memory.index.index == 1u &&
                        decoded.instruction.operands[1].as.memory.scale == 4u &&
                        decoded.instruction.operands[1].as.memory.displacement.bits == 0x10u,
                    "SIB decode semantics")) {
        ctool_job_close(job);
        return 1;
    }

    insn = instruction(CTOOL_X86_MN_MOV, 16u, 32u, 0u);
    insn.operand_count = 2u;
    insn.operands[0] = register_operand(CTOOL_X86_REG_GPR16, 0u);
    insn.operands[1] =
        memory_operand(16u, 32u, reg(CTOOL_X86_REG_NONE, 0u), reg(CTOOL_X86_REG_GPR32, 5u),
                       reg(CTOOL_X86_REG_NONE, 0u), 1u, 0, 0u);
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "operand override encode") ||
        !bytes_equal(&encoding, override_bytes, (ctool_u8)sizeof(override_bytes),
                     "operand override bytes")) {
        ctool_job_close(job);
        return 1;
    }
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                              ctool_bytes(override_bytes, (ctool_u32)sizeof(override_bytes)), 0u,
                              &decoded);
    if (!check_status(status, CTOOL_OK, "operand override decode") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                        decoded.instruction.operand_bits == 16u &&
                        decoded.instruction.address_bits == 32u &&
                        decoded.encoding.size == sizeof(override_bytes) &&
                        memcmp(decoded.encoding.bytes, override_bytes, sizeof(override_bytes)) == 0,
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
    static const ctool_u8 far_segment[] = {0xeau, 0x78u, 0x56u, 0x34u, 0x12u, 2u, 0u};
    static const ctool_u8 short_backward[] = {0xebu, 0xfcu};
    static const ctool_u8 group_add[] = {0x81u, 0xc0u, 1u, 0u, 0u, 0u};
    static const ctool_u8 short_add[] = {0x83u, 0xc0u, 1u};
    static const ctool_u8 accumulator_add[] = {0x05u, 0x78u, 0x56u, 0x34u, 0x12u};
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
    insn.operands[1] = value_operand(CTOOL_X86_OPERAND_IMMEDIATE, 32u, 32u, reference(3u, 4));
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "absolute immediate") ||
        !bytes_equal(&encoding, absolute_imm, (ctool_u8)sizeof(absolute_imm),
                     "absolute immediate bytes") ||
        !check_true(
            encoding.field_count == 1u && encoding.fields[0].kind == CTOOL_X86_FIELD_IMMEDIATE &&
                encoding.fields[0].relocation == CTOOL_X86_RELOC_ABSOLUTE &&
                encoding.fields[0].byte_offset == 1u && encoding.fields[0].byte_width == 4u &&
                encoding.fields[0].reference == 3u && encoding.fields[0].encoded_addend == 4,
            "absolute immediate field")) {
        ctool_job_close(job);
        return 1;
    }

    insn.operands[0] = register_operand(CTOOL_X86_REG_GPR32, 3u);
    insn.operands[1] =
        memory_operand(32u, 32u, reg(CTOOL_X86_REG_NONE, 0u), reg(CTOOL_X86_REG_NONE, 0u),
                       reg(CTOOL_X86_REG_NONE, 0u), 1u, 0, 32u);
    insn.operands[1].as.memory.displacement = reference(5u, 8);
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "absolute displacement") ||
        !bytes_equal(&encoding, absolute_disp, (ctool_u8)sizeof(absolute_disp),
                     "absolute displacement bytes") ||
        !check_true(
            encoding.field_count == 1u && encoding.fields[0].kind == CTOOL_X86_FIELD_DISPLACEMENT &&
                encoding.fields[0].relocation == CTOOL_X86_RELOC_ABSOLUTE &&
                encoding.fields[0].operand_index == 1u && encoding.fields[0].byte_offset == 2u &&
                encoding.fields[0].byte_width == 4u && encoding.fields[0].reference == 5u &&
                encoding.fields[0].encoded_addend == 8,
            "absolute displacement field")) {
        ctool_job_close(job);
        return 1;
    }

    insn = instruction(CTOOL_X86_MN_JMP, 32u, 32u, 0u);
    insn.operand_count = 1u;
    insn.operands[0] = far_operand(32u, reference(7u, 0), constant(8u));
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "far offset relocation") ||
        !bytes_equal(&encoding, far_offset, (ctool_u8)sizeof(far_offset), "far offset bytes") ||
        !check_true(encoding.field_count == 2u &&
                        encoding.fields[0].kind == CTOOL_X86_FIELD_FAR_OFFSET &&
                        encoding.fields[0].relocation == CTOOL_X86_RELOC_ABSOLUTE &&
                        encoding.fields[0].byte_offset == 1u &&
                        encoding.fields[0].byte_width == 4u && encoding.fields[0].reference == 7u &&
                        encoding.fields[1].kind == CTOOL_X86_FIELD_FAR_SEGMENT &&
                        encoding.fields[1].relocation == CTOOL_X86_RELOC_NONE &&
                        encoding.fields[1].byte_offset == 5u && encoding.fields[1].byte_width == 2u,
                    "far offset fields")) {
        ctool_job_close(job);
        return 1;
    }

    insn.operands[0] = far_operand(32u, constant(0x12345678u), reference(9u, 2));
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "far segment relocation") ||
        !bytes_equal(&encoding, far_segment, (ctool_u8)sizeof(far_segment), "far segment bytes") ||
        !check_true(encoding.field_count == 2u &&
                        encoding.fields[1].relocation == CTOOL_X86_RELOC_ABSOLUTE &&
                        encoding.fields[1].reference == 9u &&
                        encoding.fields[1].encoded_addend == 2,
                    "far segment field")) {
        ctool_job_close(job);
        return 1;
    }

    status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                              ctool_bytes(short_backward, (ctool_u32)sizeof(short_backward)), 0u,
                              &decoded);
    if (!check_status(status, CTOOL_OK, "short backward decode") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                        decoded.instruction.operand_bits == 32u &&
                        decoded.instruction.operands[0].encoding_bits == 8u &&
                        decoded.instruction.operands[0].as.value.bits == 0xfffffffcu &&
                        decoded.encoding.fields[0].encoded_addend == -4,
                    "short backward semantics")) {
        ctool_job_close(job);
        return 1;
    }

    status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                              ctool_bytes(group_add, (ctool_u32)sizeof(group_add)), 0u, &decoded);
    if (!check_status(status, CTOOL_OK, "group add decode") ||
        !check_status(ctool_x86_encode(job, CTOOL_X86_MODE_32, &decoded.instruction,
                                       decoded.encoding.form, &encoding),
                      CTOOL_OK, "group add replay") ||
        !bytes_equal(&encoding, group_add, (ctool_u8)sizeof(group_add), "group add replay bytes")) {
        ctool_job_close(job);
        return 1;
    }

    insn = instruction(CTOOL_X86_MN_ADD, 32u, 32u, 0u);
    insn.operand_count = 2u;
    insn.operands[0] = register_operand(CTOOL_X86_REG_GPR32, 0u);
    insn.operands[1] = value_operand(CTOOL_X86_OPERAND_IMMEDIATE, 32u, 0u, constant(1u));
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "shortest add") ||
        !bytes_equal(&encoding, short_add, (ctool_u8)sizeof(short_add), "shortest add bytes")) {
        ctool_job_close(job);
        return 1;
    }
    insn.operands[1] = value_operand(CTOOL_X86_OPERAND_IMMEDIATE, 32u, 32u, constant(0x12345678u));
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "accumulator add") ||
        !bytes_equal(&encoding, accumulator_add, (ctool_u8)sizeof(accumulator_add),
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
        !bytes_equal(&encoding, cr_bytes, (ctool_u8)sizeof(cr_bytes), "CR0 move bytes")) {
        ctool_job_close(job);
        return 1;
    }
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                              ctool_bytes(cr_bytes, (ctool_u32)sizeof(cr_bytes)), 0u, &decoded);
    if (!check_status(status, CTOOL_OK, "CR0 move decode") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                        decoded.instruction.mnemonic == CTOOL_X86_MN_MOV &&
                        decoded.instruction.operands[0].as.reg.class_id == CTOOL_X86_REG_CONTROL &&
                        decoded.instruction.operands[1].as.reg.class_id == CTOOL_X86_REG_GPR32,
                    "CR0 move decode semantics")) {
        ctool_job_close(job);
        return 1;
    }

    insn = instruction(CTOOL_X86_MN_FXSAVE, 32u, 32u, 0u);
    insn.operand_count = 1u;
    insn.operands[0] =
        memory_operand(0u, 32u, reg(CTOOL_X86_REG_NONE, 0u), reg(CTOOL_X86_REG_GPR32, 0u),
                       reg(CTOOL_X86_REG_NONE, 0u), 1u, 0x10, 8u);
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "fxsave") ||
        !bytes_equal(&encoding, fxsave_bytes, (ctool_u8)sizeof(fxsave_bytes), "fxsave bytes")) {
        ctool_job_close(job);
        return 1;
    }

    insn = instruction(CTOOL_X86_MN_FSIN, 32u, 32u, 0u);
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "fsin") ||
        !bytes_equal(&encoding, fsin_bytes, (ctool_u8)sizeof(fsin_bytes), "fsin bytes")) {
        ctool_job_close(job);
        return 1;
    }
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                              ctool_bytes(fsin_bytes, (ctool_u32)sizeof(fsin_bytes)), 0u, &decoded);
    if (!check_status(status, CTOOL_OK, "fsin decode") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                        decoded.instruction.mnemonic == CTOOL_X86_MN_FSIN,
                    "fsin decode semantics")) {
        ctool_job_close(job);
        return 1;
    }

    insn = instruction(CTOOL_X86_MN_FLDZ, 32u, 32u, 0u);
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "fldz") ||
        !bytes_equal(&encoding, fldz_bytes, (ctool_u8)sizeof(fldz_bytes), "fldz bytes")) {
        ctool_job_close(job);
        return 1;
    }
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                              ctool_bytes(fldz_bytes, (ctool_u32)sizeof(fldz_bytes)), 0u, &decoded);
    if (!check_status(status, CTOOL_OK, "fldz decode") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                        decoded.instruction.mnemonic == CTOOL_X86_MN_FLDZ &&
                        decoded.instruction.operand_count == 0u,
                    "fldz decode semantics")) {
        ctool_job_close(job);
        return 1;
    }

    for (index = 0u;
         index < (ctool_u32)(sizeof(x87_integer_vectors) / sizeof(x87_integer_vectors[0]));
         index++) {
        const x87_integer_memory_vector_t *vector = &x87_integer_vectors[index];
        insn = instruction(vector->mnemonic, 32u, 32u, 0u);
        insn.operand_count = 1u;
        insn.operands[0] =
            memory_operand(vector->width_bits, 32u, reg(CTOOL_X86_REG_NONE, 0u),
                           reg(CTOOL_X86_REG_GPR32, 0u), reg(CTOOL_X86_REG_NONE, 0u), 1u, 0, 0u);
        if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, vector->name) ||
            !bytes_equal(&encoding, vector->bytes, (ctool_u8)sizeof(vector->bytes), vector->name)) {
            ctool_job_close(job);
            return 1;
        }
        status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                                  ctool_bytes(vector->bytes, (ctool_u32)sizeof(vector->bytes)), 0u,
                                  &decoded);
        if (!check_status(status, CTOOL_OK, vector->name) ||
            !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                            decoded.instruction.mnemonic == vector->mnemonic &&
                            decoded.instruction.operand_count == 1u &&
                            decoded.instruction.operands[0].kind == CTOOL_X86_OPERAND_MEMORY &&
                            decoded.instruction.operands[0].width_bits == vector->width_bits,
                        vector->name)) {
            ctool_job_close(job);
            return 1;
        }
    }

    insn = instruction(CTOOL_X86_MN_FILD, 32u, 32u, 0u);
    insn.operand_count = 1u;
    insn.operands[0] = register_operand(CTOOL_X86_REG_GPR32, 0u);
    (void)memset(&encoding, 0xa5, sizeof(encoding));
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn, CTOOL_X86_FORM_AUTO, &encoding);
    if (!check_status(status, CTOOL_ERR_INPUT, "fild register operand") ||
        !check_true(encoding_is_zero(&encoding), "fild register operand zeroed output")) {
        ctool_job_close(job);
        return 1;
    }

    insn = instruction(CTOOL_X86_MN_FISTP, 32u, 32u, 0u);
    insn.operand_count = 1u;
    insn.operands[0] =
        memory_operand(80u, 32u, reg(CTOOL_X86_REG_NONE, 0u), reg(CTOOL_X86_REG_GPR32, 0u),
                       reg(CTOOL_X86_REG_NONE, 0u), 1u, 0, 0u);
    (void)memset(&encoding, 0xa5, sizeof(encoding));
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn, CTOOL_X86_FORM_AUTO, &encoding);
    if (!check_status(status, CTOOL_ERR_INPUT, "fistp tword operand") ||
        !check_true(encoding_is_zero(&encoding), "fistp tword operand zeroed output")) {
        ctool_job_close(job);
        return 1;
    }

    insn = instruction(CTOOL_X86_MN_FUCOMIP, 32u, 32u, 0u);
    insn.operand_count = 2u;
    insn.operands[0] = register_operand(CTOOL_X86_REG_X87, 0u);
    insn.operands[1] = register_operand(CTOOL_X86_REG_X87, 1u);
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "fucomip st0, st1") ||
        !bytes_equal(&encoding, fucomip_bytes, (ctool_u8)sizeof(fucomip_bytes), "fucomip bytes")) {
        ctool_job_close(job);
        return 1;
    }
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                              ctool_bytes(fucomip_bytes, (ctool_u32)sizeof(fucomip_bytes)), 0u,
                              &decoded);
    if (!check_status(status, CTOOL_OK, "fucomip decode") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                        decoded.instruction.mnemonic == CTOOL_X86_MN_FUCOMIP &&
                        decoded.instruction.operand_count == 2u &&
                        decoded.instruction.operands[0].kind == CTOOL_X86_OPERAND_REGISTER &&
                        decoded.instruction.operands[0].as.reg.class_id == CTOOL_X86_REG_X87 &&
                        decoded.instruction.operands[0].as.reg.index == 0u &&
                        decoded.instruction.operands[1].kind == CTOOL_X86_OPERAND_REGISTER &&
                        decoded.instruction.operands[1].as.reg.class_id == CTOOL_X86_REG_X87 &&
                        decoded.instruction.operands[1].as.reg.index == 1u,
                    "fucomip decode semantics")) {
        ctool_job_close(job);
        return 1;
    }

    insn = instruction(CTOOL_X86_MN_FSUB, 32u, 32u, 0u);
    insn.operand_count = 2u;
    insn.operands[0] = register_operand(CTOOL_X86_REG_X87, 1u);
    insn.operands[1] = register_operand(CTOOL_X86_REG_X87, 0u);
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "fsub st1, st0") ||
        !bytes_equal(&encoding, fsub_st1_st0_bytes, (ctool_u8)sizeof(fsub_st1_st0_bytes),
                     "fsub st1, st0 bytes")) {
        ctool_job_close(job);
        return 1;
    }
    status = ctool_x86_decode(
        job, CTOOL_X86_MODE_32,
        ctool_bytes(fsub_st1_st0_bytes, (ctool_u32)sizeof(fsub_st1_st0_bytes)), 0u, &decoded);
    if (!check_status(status, CTOOL_OK, "fsub st1, st0 decode") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                        decoded.instruction.mnemonic == CTOOL_X86_MN_FSUB &&
                        decoded.instruction.operand_count == 2u &&
                        decoded.instruction.operands[0].kind == CTOOL_X86_OPERAND_REGISTER &&
                        decoded.instruction.operands[0].as.reg.class_id == CTOOL_X86_REG_X87 &&
                        decoded.instruction.operands[0].as.reg.index == 1u &&
                        decoded.instruction.operands[1].kind == CTOOL_X86_OPERAND_REGISTER &&
                        decoded.instruction.operands[1].as.reg.class_id == CTOOL_X86_REG_X87 &&
                        decoded.instruction.operands[1].as.reg.index == 0u,
                    "fsub st1, st0 decode semantics")) {
        ctool_job_close(job);
        return 1;
    }

    insn = instruction(CTOOL_X86_MN_FSUB, 16u, 16u, 0u);
    insn.operand_count = 2u;
    insn.operands[0] = register_operand(CTOOL_X86_REG_X87, 1u);
    insn.operands[1] = register_operand(CTOOL_X86_REG_X87, 0u);
    if (!encode(job, CTOOL_X86_MODE_16, &insn, &encoding, "fsub st1, st0 mode16") ||
        !bytes_equal(&encoding, fsub_st1_st0_bytes, (ctool_u8)sizeof(fsub_st1_st0_bytes),
                     "fsub st1, st0 mode16 bytes")) {
        ctool_job_close(job);
        return 1;
    }
    status = ctool_x86_decode(
        job, CTOOL_X86_MODE_16,
        ctool_bytes(fsub_st1_st0_bytes, (ctool_u32)sizeof(fsub_st1_st0_bytes)), 0u, &decoded);
    if (!check_status(status, CTOOL_OK, "fsub st1, st0 mode16 decode") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                        decoded.instruction.mnemonic == CTOOL_X86_MN_FSUB &&
                        decoded.instruction.operand_count == 2u &&
                        decoded.instruction.operands[0].kind == CTOOL_X86_OPERAND_REGISTER &&
                        decoded.instruction.operands[0].as.reg.class_id == CTOOL_X86_REG_X87 &&
                        decoded.instruction.operands[0].as.reg.index == 1u &&
                        decoded.instruction.operands[1].kind == CTOOL_X86_OPERAND_REGISTER &&
                        decoded.instruction.operands[1].as.reg.class_id == CTOOL_X86_REG_X87 &&
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
    status = ctool_x86_encode(job, CTOOL_X86_MODE_32, &insn, CTOOL_X86_FORM_AUTO, &encoding);
    if (!check_status(status, CTOOL_ERR_INPUT, "fsub fixed destination register") ||
        !check_true(encoding_is_zero(&encoding), "fsub fixed destination zeroed output")) {
        ctool_job_close(job);
        return 1;
    }

    insn = instruction(CTOOL_X86_MN_MOVSS, 32u, 32u, 0u);
    insn.operand_count = 2u;
    insn.operands[0] = register_operand(CTOOL_X86_REG_XMM, 0u);
    insn.operands[1] =
        memory_operand(32u, 32u, reg(CTOOL_X86_REG_NONE, 0u), reg(CTOOL_X86_REG_GPR32, 0u),
                       reg(CTOOL_X86_REG_NONE, 0u), 1u, 0, 0u);
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "movss") ||
        !bytes_equal(&encoding, movss_bytes, (ctool_u8)sizeof(movss_bytes), "movss bytes")) {
        ctool_job_close(job);
        return 1;
    }
    status =
        ctool_x86_decode(job, CTOOL_X86_MODE_32,
                         ctool_bytes(movss_bytes, (ctool_u32)sizeof(movss_bytes)), 0u, &decoded);
    if (!check_status(status, CTOOL_OK, "movss decode") ||
        !check_true(decoded.kind == CTOOL_X86_DECODE_KNOWN &&
                        decoded.instruction.mnemonic == CTOOL_X86_MN_MOVSS &&
                        decoded.instruction.prefixes == 0u &&
                        decoded.instruction.operands[0].as.reg.class_id == CTOOL_X86_REG_XMM &&
                        decoded.instruction.operands[1].kind == CTOOL_X86_OPERAND_MEMORY,
                    "MOVSS mandatory-prefix semantics")) {
        ctool_job_close(job);
        return 1;
    }

    insn = instruction(CTOOL_X86_MN_PXOR, 32u, 32u, 0u);
    insn.operand_count = 2u;
    insn.operands[0] = register_operand(CTOOL_X86_REG_XMM, 1u);
    insn.operands[1] = register_operand(CTOOL_X86_REG_XMM, 2u);
    if (!encode(job, CTOOL_X86_MODE_32, &insn, &encoding, "pxor") ||
        !bytes_equal(&encoding, pxor_bytes, (ctool_u8)sizeof(pxor_bytes), "pxor bytes")) {
        ctool_job_close(job);
        return 1;
    }
    status = ctool_x86_decode(job, CTOOL_X86_MODE_32,
                              ctool_bytes(pxor_bytes, (ctool_u32)sizeof(pxor_bytes)), 0u, &decoded);
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
