#include "as_elf.h"

#define AS_ELF32_HEADER_BYTES 52u
#define AS_ELF32_PROGRAM_HEADER_BYTES 32u
#define AS_ELF32_CODE_OFFSET 0x80u
#define AS_ELF32_ALIGNMENT 4u
#define AS_ELF32_U32_MAX 4294967295u

static ctool_status_t as_elf32_rollback(ctool_buffer_t *output,
                                        ctool_u32 mark,
                                        ctool_status_t status) {
  ctool_status_t rewind_status = ctool_buffer_rewind(output, mark);
  return rewind_status == CTOOL_OK ? status : rewind_status;
}

static ctool_status_t as_elf32_patch_header(ctool_buffer_t *output,
                                            ctool_u32 entry,
                                            ctool_u16 program_count) {
  ctool_status_t status = ctool_buffer_patch_u8(output, 0u, 0x7fu);
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_u8(output, 1u, (ctool_u8)'E');
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_u8(output, 2u, (ctool_u8)'L');
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_u8(output, 3u, (ctool_u8)'F');
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_u8(output, 4u, 1u);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_u8(output, 5u, 1u);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_u8(output, 6u, 1u);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le16(output, 16u, 2u);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le16(output, 18u, 3u);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le32(output, 20u, 1u);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le32(output, 24u, entry);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le32(output, 28u, AS_ELF32_HEADER_BYTES);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le16(output, 40u, AS_ELF32_HEADER_BYTES);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le16(output, 42u,
                                     AS_ELF32_PROGRAM_HEADER_BYTES);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le16(output, 44u, program_count);
  }
  return status;
}

static ctool_status_t as_elf32_patch_program_header(
    ctool_buffer_t *output, ctool_u32 table_offset, ctool_u32 file_offset,
    const ctool_asm_region_t *region, ctool_u32 flags) {
  ctool_status_t status = ctool_buffer_patch_le32(output, table_offset, 1u);
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le32(output, table_offset + 4u, file_offset);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le32(output, table_offset + 8u,
                                     region->address);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le32(output, table_offset + 12u,
                                     region->address);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le32(output, table_offset + 16u,
                                     region->file_size);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le32(output, table_offset + 20u,
                                     region->memory_size);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le32(output, table_offset + 24u, flags);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le32(output, table_offset + 28u,
                                     AS_ELF32_ALIGNMENT);
  }
  return status;
}

static ctool_status_t as_elf32_align_up(ctool_u32 value,
                                        ctool_u32 alignment,
                                        ctool_u32 *aligned_out) {
  ctool_u32 remainder = value & (alignment - 1u);
  ctool_u32 addition = remainder == 0u ? 0u : alignment - remainder;
  if (value > AS_ELF32_U32_MAX - addition) {
    return CTOOL_ERR_OVERFLOW;
  }
  *aligned_out = value + addition;
  return CTOOL_OK;
}

ctool_status_t as_elf32_exec_write(const ctool_asm_result_t *artifact,
                                   ctool_buffer_t *output) {
  const ctool_asm_region_t *code;
  const ctool_asm_region_t *data = (const ctool_asm_region_t *)0;
  ctool_u32 code_end;
  ctool_u32 code_file_end;
  ctool_u32 data_end = 0u;
  ctool_u32 data_file_offset = 0u;
  ctool_u32 data_program_flags = CTOOL_ELF32_PF_R;
  ctool_u32 payload_size;
  ctool_u32 mark;
  ctool_status_t status;
  if (artifact == (const ctool_asm_result_t *)0 ||
      output == (ctool_buffer_t *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  if (ctool_buffer_view(output).size != 0u) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  mark = ctool_buffer_mark(output);
  if (artifact->artifact != CTOOL_ASM_ARTIFACT_FIXED_IMAGE ||
      artifact->regions == (const ctool_asm_region_t *)0 ||
      (artifact->region_count != 1u && artifact->region_count != 2u) ||
      artifact->has_entry != CTOOL_TRUE ||
      (artifact->bytes.data == (const ctool_u8 *)0 &&
       artifact->bytes.size != 0u)) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  code = &artifact->regions[0];
  if ((code->flags & (CTOOL_ELF32_SHF_ALLOC |
                      CTOOL_ELF32_SHF_EXECINSTR)) !=
          (CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR) ||
      (code->flags & CTOOL_ELF32_SHF_WRITE) != 0u ||
      code->output_offset != 0u || code->file_size == 0u ||
      code->memory_size < code->file_size ||
      (code->address & (AS_ELF32_ALIGNMENT - 1u)) != 0u) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  if (code->address > AS_ELF32_U32_MAX - code->memory_size ||
      AS_ELF32_CODE_OFFSET > AS_ELF32_U32_MAX - code->file_size) {
    return CTOOL_ERR_OVERFLOW;
  }
  code_end = code->address + code->memory_size;
  code_file_end = AS_ELF32_CODE_OFFSET + code->file_size;
  payload_size = code->file_size;
  if (artifact->region_count == 2u) {
    data = &artifact->regions[1];
    if ((data->flags & CTOOL_ELF32_SHF_ALLOC) == 0u ||
        (data->flags & CTOOL_ELF32_SHF_EXECINSTR) != 0u ||
        data->output_offset != code->file_size || data->memory_size == 0u ||
        data->memory_size < data->file_size ||
        (data->address & (AS_ELF32_ALIGNMENT - 1u)) != 0u) {
      return CTOOL_ERR_INVALID_ARGUMENT;
    }
    if (payload_size > AS_ELF32_U32_MAX - data->file_size) {
      return CTOOL_ERR_OVERFLOW;
    }
    payload_size += data->file_size;
    if (data->address > AS_ELF32_U32_MAX - data->memory_size) {
      return CTOOL_ERR_OVERFLOW;
    }
    data_end = data->address + data->memory_size;
    if (code->address < data_end && data->address < code_end) {
      return CTOOL_ERR_INVALID_ARGUMENT;
    }
    if ((data->flags & CTOOL_ELF32_SHF_WRITE) != 0u) {
      data_program_flags |= CTOOL_ELF32_PF_W;
    }
    status = as_elf32_align_up(code_file_end, AS_ELF32_ALIGNMENT,
                               &data_file_offset);
    if (status != CTOOL_OK ||
        data_file_offset > AS_ELF32_U32_MAX - data->file_size) {
      return CTOOL_ERR_OVERFLOW;
    }
  }
  if (payload_size != artifact->bytes.size) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  if (artifact->entry_address < code->address ||
      artifact->entry_address - code->address >= code->file_size) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }

  status = ctool_buffer_fill(output, 0u,
                             AS_ELF32_HEADER_BYTES +
                                 AS_ELF32_PROGRAM_HEADER_BYTES *
                                     artifact->region_count);
  if (status == CTOOL_OK) {
    status = as_elf32_patch_header(
        output, artifact->entry_address,
        (ctool_u16)artifact->region_count);
  }
  if (status == CTOOL_OK) {
    status = as_elf32_patch_program_header(
        output, AS_ELF32_HEADER_BYTES, AS_ELF32_CODE_OFFSET, code,
        CTOOL_ELF32_PF_R | CTOOL_ELF32_PF_X);
  }
  if (status == CTOOL_OK && data != (const ctool_asm_region_t *)0) {
    status = as_elf32_patch_program_header(
        output, AS_ELF32_HEADER_BYTES + AS_ELF32_PROGRAM_HEADER_BYTES,
        data_file_offset, data, data_program_flags);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_fill(
        output, 0u, AS_ELF32_CODE_OFFSET - ctool_buffer_view(output).size);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_append(
        output, ctool_bytes(artifact->bytes.data + code->output_offset,
                            code->file_size));
  }
  if (status == CTOOL_OK && data != (const ctool_asm_region_t *)0) {
    status = ctool_buffer_fill(
        output, 0u, data_file_offset - ctool_buffer_view(output).size);
  }
  if (status == CTOOL_OK && data != (const ctool_asm_region_t *)0 &&
      data->file_size != 0u) {
    status = ctool_buffer_append(
        output, ctool_bytes(artifact->bytes.data + data->output_offset,
                            data->file_size));
  }
  return status == CTOOL_OK ? CTOOL_OK
                            : as_elf32_rollback(output, mark, status);
}
