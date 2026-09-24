#include "pe32.h"

#define PE32_DOS_HEADER_SIZE 128u
#define PE32_SIGNATURE_OFFSET 128u
#define PE32_COFF_HEADER_OFFSET 132u
#define PE32_OPTIONAL_HEADER_OFFSET 152u
#define PE32_OPTIONAL_HEADER_SIZE 224u
#define PE32_SECTION_HEADER_SIZE 40u
#define PE32_IMPORT_DESCRIPTOR_SIZE 20u
#define PE32_DIRECTORY_COUNT 16u
#define PE32_IMPORT_DIRECTORY 1u
#define PE32_IAT_DIRECTORY 12u
#define PE32_U32_MAX 4294967295u

typedef struct {
  ctool_u32 lookup_rva;
  ctool_u32 name_rva;
  ctool_u32 iat_rva;
  ctool_u32 lookup_offset;
  ctool_u32 iat_offset;
  ctool_u32 import_count;
} pe32_import_descriptor_t;

static const ctool_u8 pe32_dos_stub[PE32_DOS_HEADER_SIZE] = {
    0x4du, 0x5au, 0x90u, 0x00u, 0x03u, 0x00u, 0x00u, 0x00u,
    0x04u, 0x00u, 0x00u, 0x00u, 0xffu, 0xffu, 0x00u, 0x00u,
    0xb8u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x40u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
    0x00u, 0x00u, 0x00u, 0x00u, 0x80u, 0x00u, 0x00u, 0x00u,
    0x0eu, 0x1fu, 0xbau, 0x0eu, 0x00u, 0xb4u, 0x09u, 0xcdu,
    0x21u, 0xb8u, 0x01u, 0x4cu, 0xcdu, 0x21u, 0x54u, 0x68u,
    0x69u, 0x73u, 0x20u, 0x70u, 0x72u, 0x6fu, 0x67u, 0x72u,
    0x61u, 0x6du, 0x20u, 0x63u, 0x61u, 0x6eu, 0x6eu, 0x6fu,
    0x74u, 0x20u, 0x62u, 0x65u, 0x20u, 0x72u, 0x75u, 0x6eu,
    0x20u, 0x69u, 0x6eu, 0x20u, 0x44u, 0x4fu, 0x53u, 0x20u,
    0x6du, 0x6fu, 0x64u, 0x65u, 0x2eu, 0x0du, 0x0du, 0x0au,
    0x24u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u};

static void pe32_zero_image(ctool_pe32_image_t *image) {
  ctool_u8 *bytes = (ctool_u8 *)image;
  ctool_u32 index;
  for (index = 0u; index < (ctool_u32)sizeof(*image); index++) {
    bytes[index] = 0u;
  }
}

static ctool_bool pe32_range_fits(ctool_u32 offset, ctool_u32 size,
                                  ctool_u32 extent) {
  return offset <= extent && size <= extent - offset ? CTOOL_TRUE
                                                      : CTOOL_FALSE;
}

static ctool_bool pe32_add_overflows(ctool_u32 left, ctool_u32 right) {
  return left > PE32_U32_MAX - right ? CTOOL_TRUE : CTOOL_FALSE;
}

static ctool_bool pe32_mul_overflows(ctool_u32 left, ctool_u32 right) {
  return right != 0u && left > PE32_U32_MAX / right ? CTOOL_TRUE
                                                    : CTOOL_FALSE;
}

static ctool_u16 pe32_read_le16(ctool_bytes_t image, ctool_u32 offset) {
  return (ctool_u16)((ctool_u16)image.data[offset] |
                     ((ctool_u16)image.data[offset + 1u] << 8u));
}

static ctool_u32 pe32_read_le32(ctool_bytes_t image, ctool_u32 offset) {
  return (ctool_u32)image.data[offset] |
         ((ctool_u32)image.data[offset + 1u] << 8u) |
         ((ctool_u32)image.data[offset + 2u] << 16u) |
         ((ctool_u32)image.data[offset + 3u] << 24u);
}

static ctool_status_t pe32_align(ctool_u32 value, ctool_u32 alignment,
                                 ctool_u32 *aligned_out) {
  ctool_u32 mask = alignment - 1u;
  if (pe32_add_overflows(value, mask) == CTOOL_TRUE) {
    return CTOOL_ERR_OVERFLOW;
  }
  *aligned_out = (value + mask) & ~mask;
  return CTOOL_OK;
}

static ctool_status_t pe32_emit_diagnostic(ctool_job_t *job,
                                            ctool_u32 code,
                                            ctool_string_t path,
                                            ctool_u32 offset,
                                            const char *message,
                                            ctool_status_t status) {
  ctool_diagnostic_t diagnostic;
  ctool_status_t diagnostic_status;
  diagnostic.severity = CTOOL_DIAG_ERROR;
  diagnostic.code = code;
  diagnostic.path = path;
  diagnostic.line = 0u;
  diagnostic.column = offset;
  diagnostic.message = ctool_string(message);
  diagnostic_status = ctool_job_emit(job, &diagnostic);
  return diagnostic_status == CTOOL_OK ? status : diagnostic_status;
}

static ctool_status_t pe32_failure(
    ctool_job_t *job, const ctool_source_t *source, ctool_arena_t *arena,
    ctool_arena_mark_t mark, ctool_pe32_image_t *image_out,
    ctool_status_t status, ctool_u32 code, ctool_u32 offset,
    const char *message) {
  (void)ctool_arena_rewind(arena, mark);
  pe32_zero_image(image_out);
  if (status == CTOOL_ERR_NO_MEMORY) {
    return status;
  }
  return pe32_emit_diagnostic(job, code, source->path.text, offset, message,
                              status);
}

static ctool_status_t pe32_alloc_array(ctool_arena_t *arena,
                                        ctool_u32 count,
                                        ctool_u32 element_size,
                                        void **array_out) {
  *array_out = (void *)0;
  if (count == 0u) {
    return CTOOL_OK;
  }
  return ctool_arena_alloc_zero(arena, count, element_size,
                                (ctool_u32)sizeof(void *), array_out);
}

static ctool_bool pe32_bytes_are_zero(ctool_bytes_t image,
                                      ctool_u32 offset,
                                      ctool_u32 size) {
  ctool_u32 index;
  for (index = 0u; index < size; index++) {
    if (image.data[offset + index] != 0u) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_bool pe32_name_equals(ctool_string_t name,
                                   const char *literal) {
  ctool_string_t expected = ctool_string(literal);
  ctool_u32 index;
  if (name.size != expected.size) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < name.size; index++) {
    if (name.data[index] != expected.data[index]) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_u8 pe32_ascii_fold(ctool_u8 value) {
  return value >= (ctool_u8)'A' && value <= (ctool_u8)'Z'
             ? value + ((ctool_u8)'a' - (ctool_u8)'A')
             : value;
}

static ctool_i32 pe32_string_compare(ctool_string_t left,
                                     ctool_string_t right,
                                     ctool_bool fold_ascii) {
  ctool_u32 count = left.size < right.size ? left.size : right.size;
  ctool_u32 index;
  for (index = 0u; index < count; index++) {
    ctool_u8 left_byte = (ctool_u8)left.data[index];
    ctool_u8 right_byte = (ctool_u8)right.data[index];
    if (fold_ascii == CTOOL_TRUE) {
      left_byte = pe32_ascii_fold(left_byte);
      right_byte = pe32_ascii_fold(right_byte);
    }
    if (left_byte < right_byte) {
      return -1;
    }
    if (left_byte > right_byte) {
      return 1;
    }
  }
  if (left.size < right.size) {
    return -1;
  }
  if (left.size > right.size) {
    return 1;
  }
  return 0;
}

static ctool_status_t pe32_section_kind(ctool_string_t name,
                                        ctool_pe32_section_kind_t *kind_out,
                                        ctool_u32 *rank_out,
                                        ctool_u32 *characteristics_out) {
  if (pe32_name_equals(name, ".text") == CTOOL_TRUE) {
    *kind_out = CTOOL_PE32_SECTION_TEXT;
    *rank_out = 0u;
    *characteristics_out = CTOOL_PE32_SCN_CODE | CTOOL_PE32_SCN_EXECUTE |
                           CTOOL_PE32_SCN_READ;
  } else if (pe32_name_equals(name, ".rodata") == CTOOL_TRUE) {
    *kind_out = CTOOL_PE32_SECTION_RODATA;
    *rank_out = 1u;
    *characteristics_out = CTOOL_PE32_SCN_INITIALIZED_DATA |
                           CTOOL_PE32_SCN_READ;
  } else if (pe32_name_equals(name, ".data") == CTOOL_TRUE) {
    *kind_out = CTOOL_PE32_SECTION_DATA;
    *rank_out = 2u;
    *characteristics_out = CTOOL_PE32_SCN_INITIALIZED_DATA |
                           CTOOL_PE32_SCN_READ | CTOOL_PE32_SCN_WRITE;
  } else if (pe32_name_equals(name, ".bss") == CTOOL_TRUE) {
    *kind_out = CTOOL_PE32_SECTION_BSS;
    *rank_out = 3u;
    *characteristics_out = CTOOL_PE32_SCN_UNINITIALIZED_DATA |
                           CTOOL_PE32_SCN_READ | CTOOL_PE32_SCN_WRITE;
  } else if (pe32_name_equals(name, ".idata") == CTOOL_TRUE) {
    *kind_out = CTOOL_PE32_SECTION_IDATA;
    *rank_out = 4u;
    *characteristics_out = CTOOL_PE32_SCN_INITIALIZED_DATA |
                           CTOOL_PE32_SCN_READ | CTOOL_PE32_SCN_WRITE;
  } else {
    return CTOOL_ERR_UNSUPPORTED;
  }
  return CTOOL_OK;
}

static ctool_status_t pe32_idata_extent(
    const ctool_pe32_section_t *idata, ctool_u32 rva, ctool_u32 size,
    ctool_u32 *offset_out, ctool_u32 *end_out) {
  ctool_u32 relative;
  ctool_u32 available;
  if (rva < idata->virtual_address) {
    return CTOOL_ERR_INPUT;
  }
  relative = rva - idata->virtual_address;
  available = idata->virtual_size < idata->file_size
                  ? idata->virtual_size
                  : idata->file_size;
  if (relative > available || size > available - relative) {
    return CTOOL_ERR_INPUT;
  }
  *offset_out = idata->file_offset + relative;
  *end_out = idata->file_offset + available;
  return CTOOL_OK;
}

static ctool_status_t pe32_idata_string(
    ctool_bytes_t image, const ctool_pe32_section_t *idata, ctool_u32 rva,
    ctool_bool library_name, ctool_string_t *name_out) {
  ctool_u32 offset;
  ctool_u32 end;
  ctool_u32 cursor;
  ctool_status_t status =
      pe32_idata_extent(idata, rva, 1u, &offset, &end);
  if (status != CTOOL_OK) {
    return status;
  }
  cursor = offset;
  while (cursor < end && image.data[cursor] != 0u) {
    ctool_u8 value = image.data[cursor];
    if (value < 0x21u || value > 0x7eu ||
        (library_name == CTOOL_TRUE &&
         (value == (ctool_u8)'/' || value == (ctool_u8)'\\' ||
          value == (ctool_u8)':'))) {
      return CTOOL_ERR_INPUT;
    }
    cursor++;
  }
  if (cursor == offset || cursor == end) {
    return CTOOL_ERR_INPUT;
  }
  name_out->data = (const char *)(image.data + offset);
  name_out->size = cursor - offset;
  return CTOOL_OK;
}

static ctool_status_t pe32_parse_imports(
    ctool_job_t *job, const ctool_source_t *source, ctool_arena_t *arena,
    ctool_arena_mark_t mark, ctool_pe32_image_t *image_out,
    ctool_pe32_section_t *sections, ctool_u32 section_count,
    ctool_u32 import_rva, ctool_u32 import_size, ctool_u32 iat_rva,
    ctool_u32 iat_size) {
  ctool_bytes_t image = source->contents;
  ctool_pe32_section_t *idata = (ctool_pe32_section_t *)0;
  pe32_import_descriptor_t *descriptors =
      (pe32_import_descriptor_t *)0;
  ctool_pe32_import_library_t *libraries =
      (ctool_pe32_import_library_t *)0;
  ctool_pe32_import_t *imports = (ctool_pe32_import_t *)0;
  ctool_u32 library_count;
  ctool_u32 import_count = 0u;
  ctool_u32 descriptor_offset;
  ctool_u32 descriptor_end;
  ctool_u32 cursor;
  ctool_u32 first_iat;
  ctool_u32 index;
  ctool_u32 import_index;
  ctool_status_t status;
  if (import_rva == 0u && import_size == 0u && iat_rva == 0u &&
      iat_size == 0u) {
    return CTOOL_OK;
  }
  if (import_rva == 0u || import_size < PE32_IMPORT_DESCRIPTOR_SIZE * 2u ||
      import_size % PE32_IMPORT_DESCRIPTOR_SIZE != 0u || iat_rva == 0u ||
      iat_size == 0u) {
    return pe32_failure(job, source, arena, mark, image_out,
                        CTOOL_ERR_INPUT, CTOOL_PE32_DIAG_BAD_DIRECTORY,
                        PE32_OPTIONAL_HEADER_OFFSET + 96u +
                            PE32_IMPORT_DIRECTORY * 8u,
                        "PE32 import directories are incomplete");
  }
  for (index = 0u; index < section_count; index++) {
    if (sections[index].kind == CTOOL_PE32_SECTION_IDATA) {
      idata = &sections[index];
    }
  }
  if (idata == (ctool_pe32_section_t *)0 ||
      import_rva != idata->virtual_address) {
    return pe32_failure(job, source, arena, mark, image_out,
                        CTOOL_ERR_INPUT, CTOOL_PE32_DIAG_BAD_IMPORT,
                        import_rva,
                        "PE32 import directory is not rooted in .idata");
  }
  library_count = import_size / PE32_IMPORT_DESCRIPTOR_SIZE - 1u;
  status = pe32_idata_extent(idata, import_rva, import_size,
                             &descriptor_offset, &descriptor_end);
  if (status != CTOOL_OK) {
    return pe32_failure(job, source, arena, mark, image_out,
                        CTOOL_ERR_INPUT, CTOOL_PE32_DIAG_BAD_IMPORT,
                        import_rva, "PE32 import directory is out of range");
  }
  (void)descriptor_end;
  status = pe32_alloc_array(arena, library_count,
                            (ctool_u32)sizeof(*descriptors),
                            (void **)&descriptors);
  if (status == CTOOL_OK) {
    status = pe32_alloc_array(arena, library_count,
                              (ctool_u32)sizeof(*libraries),
                              (void **)&libraries);
  }
  if (status != CTOOL_OK) {
    return pe32_failure(job, source, arena, mark, image_out, status,
                        CTOOL_PE32_DIAG_LIMIT, import_rva,
                        "PE32 import-library metadata limit exceeded");
  }
  for (index = 0u; index < library_count; index++) {
    ctool_u32 offset =
        descriptor_offset + index * PE32_IMPORT_DESCRIPTOR_SIZE;
    descriptors[index].lookup_rva = pe32_read_le32(image, offset);
    descriptors[index].name_rva = pe32_read_le32(image, offset + 12u);
    descriptors[index].iat_rva = pe32_read_le32(image, offset + 16u);
    if (pe32_read_le32(image, offset + 4u) != 0u ||
        pe32_read_le32(image, offset + 8u) != 0u) {
      return pe32_failure(job, source, arena, mark, image_out,
                          CTOOL_ERR_UNSUPPORTED,
                          CTOOL_PE32_DIAG_BAD_IMPORT, offset,
                          "stateful PE32 import descriptors are unsupported");
    }
  }
  if (pe32_bytes_are_zero(
          image,
          descriptor_offset + library_count * PE32_IMPORT_DESCRIPTOR_SIZE,
          PE32_IMPORT_DESCRIPTOR_SIZE) == CTOOL_FALSE) {
    return pe32_failure(job, source, arena, mark, image_out,
                        CTOOL_ERR_INPUT, CTOOL_PE32_DIAG_BAD_IMPORT,
                        descriptor_offset +
                            library_count * PE32_IMPORT_DESCRIPTOR_SIZE,
                        "PE32 import descriptor table is not terminated");
  }
  cursor = import_size;
  for (index = 0u; index < library_count; index++) {
    ctool_u32 table_rva = idata->virtual_address + cursor;
    ctool_u32 table_offset;
    ctool_u32 table_end;
    ctool_u32 count = 0u;
    if (descriptors[index].lookup_rva != table_rva ||
        pe32_idata_extent(idata, table_rva, 4u, &table_offset,
                          &table_end) != CTOOL_OK) {
      return pe32_failure(job, source, arena, mark, image_out,
                          CTOOL_ERR_INPUT,
                          CTOOL_PE32_DIAG_BAD_IMPORT, table_rva,
                          "PE32 import lookup tables are not canonical");
    }
    while (table_offset + count * 4u <= table_end - 4u &&
           pe32_read_le32(image, table_offset + count * 4u) != 0u) {
      ctool_u32 lookup =
          pe32_read_le32(image, table_offset + count * 4u);
      if ((lookup & 0x80000000u) != 0u || count == PE32_U32_MAX) {
        return pe32_failure(job, source, arena, mark, image_out,
                            CTOOL_ERR_UNSUPPORTED,
                            CTOOL_PE32_DIAG_BAD_IMPORT,
                            table_offset + count * 4u,
                            "ordinal PE32 imports are unsupported");
      }
      count++;
    }
    if (count == 0u || table_offset + count * 4u > table_end - 4u ||
        pe32_add_overflows(import_count, count) == CTOOL_TRUE ||
        pe32_add_overflows(cursor, (count + 1u) * 4u) == CTOOL_TRUE) {
      return pe32_failure(job, source, arena, mark, image_out,
                          CTOOL_ERR_INPUT, CTOOL_PE32_DIAG_BAD_IMPORT,
                          table_offset,
                          "PE32 import lookup table is unterminated or empty");
    }
    descriptors[index].lookup_offset = table_offset;
    descriptors[index].import_count = count;
    import_count += count;
    cursor += (count + 1u) * 4u;
  }
  first_iat = idata->virtual_address + cursor;
  for (index = 0u; index < library_count; index++) {
    ctool_u32 size = (descriptors[index].import_count + 1u) * 4u;
    ctool_u32 table_rva = idata->virtual_address + cursor;
    ctool_u32 table_end;
    if (descriptors[index].iat_rva != table_rva ||
        pe32_idata_extent(idata, table_rva, size,
                          &descriptors[index].iat_offset,
                          &table_end) != CTOOL_OK) {
      return pe32_failure(job, source, arena, mark, image_out,
                          CTOOL_ERR_INPUT, CTOOL_PE32_DIAG_BAD_IMPORT,
                          table_rva,
                          "PE32 import address tables are not canonical");
    }
    (void)table_end;
    cursor += size;
  }
  if (iat_rva != first_iat || iat_size != idata->virtual_address + cursor -
                                           first_iat) {
    return pe32_failure(job, source, arena, mark, image_out,
                        CTOOL_ERR_INPUT, CTOOL_PE32_DIAG_BAD_DIRECTORY,
                        PE32_OPTIONAL_HEADER_OFFSET + 96u +
                            PE32_IAT_DIRECTORY * 8u,
                        "PE32 IAT directory does not match its tables");
  }
  import_index = 0u;
  for (index = 0u; index < library_count; index++) {
    ctool_string_t name;
    ctool_u32 name_offset;
    ctool_u32 name_end;
    if (descriptors[index].name_rva != idata->virtual_address + cursor ||
        pe32_idata_string(image, idata, descriptors[index].name_rva,
                          CTOOL_TRUE, &name) != CTOOL_OK ||
        pe32_idata_extent(idata, descriptors[index].name_rva,
                          name.size + 1u, &name_offset,
                          &name_end) != CTOOL_OK) {
      return pe32_failure(job, source, arena, mark, image_out,
                          CTOOL_ERR_INPUT, CTOOL_PE32_DIAG_BAD_IMPORT,
                          descriptors[index].name_rva,
                          "PE32 import library name is invalid");
    }
    (void)name_offset;
    (void)name_end;
    if (index != 0u &&
        pe32_string_compare(libraries[index - 1u].name, name,
                            CTOOL_TRUE) >= 0) {
      return pe32_failure(job, source, arena, mark, image_out,
                          CTOOL_ERR_INPUT, CTOOL_PE32_DIAG_BAD_IMPORT,
                          descriptors[index].name_rva,
                          "PE32 import libraries are not canonical");
    }
    libraries[index].file_index = index;
    libraries[index].name = name;
    libraries[index].lookup_rva = descriptors[index].lookup_rva;
    libraries[index].iat_rva = descriptors[index].iat_rva;
    libraries[index].import_first = import_index;
    libraries[index].import_count = descriptors[index].import_count;
    import_index += descriptors[index].import_count;
    cursor += name.size + 1u;
  }
  status = pe32_alloc_array(arena, import_count,
                            (ctool_u32)sizeof(*imports),
                            (void **)&imports);
  if (status != CTOOL_OK) {
    return pe32_failure(job, source, arena, mark, image_out, status,
                        CTOOL_PE32_DIAG_LIMIT, import_rva,
                        "PE32 import metadata limit exceeded");
  }
  import_index = 0u;
  for (index = 0u; index < library_count; index++) {
    ctool_u32 procedure_index;
    ctool_string_t previous_name;
    previous_name.data = (const char *)0;
    previous_name.size = 0u;
    for (procedure_index = 0u;
         procedure_index < descriptors[index].import_count;
         procedure_index++) {
      ctool_u32 lookup = pe32_read_le32(
          image, descriptors[index].lookup_offset + procedure_index * 4u);
      ctool_u32 iat = pe32_read_le32(
          image, descriptors[index].iat_offset + procedure_index * 4u);
      ctool_u32 hint_offset;
      ctool_u32 hint_end;
      ctool_string_t procedure;
      ctool_pe32_import_t *import = &imports[import_index];
      if ((cursor & 1u) != 0u) {
        ctool_u32 alignment_offset;
        ctool_u32 alignment_end;
        if (pe32_idata_extent(idata, idata->virtual_address + cursor,
                              1u, &alignment_offset,
                              &alignment_end) != CTOOL_OK ||
            image.data[alignment_offset] != 0u) {
          return pe32_failure(job, source, arena, mark, image_out,
                              CTOOL_ERR_INPUT,
                              CTOOL_PE32_DIAG_BAD_IMPORT,
                              idata->virtual_address + cursor,
                              "PE32 import-name alignment is invalid");
        }
        (void)alignment_end;
        cursor++;
      }
      if (lookup != idata->virtual_address + cursor || iat != lookup ||
          pe32_idata_extent(idata, lookup, 3u, &hint_offset,
                            &hint_end) != CTOOL_OK ||
          pe32_read_le16(image, hint_offset) != 0u ||
          pe32_idata_string(image, idata, lookup + 2u, CTOOL_FALSE,
                            &procedure) != CTOOL_OK) {
        return pe32_failure(job, source, arena, mark, image_out,
                            CTOOL_ERR_INPUT,
                            CTOOL_PE32_DIAG_BAD_IMPORT, lookup,
                            "PE32 import hint or procedure name is invalid");
      }
      (void)hint_end;
      if (previous_name.data != (const char *)0 &&
          pe32_string_compare(previous_name, procedure,
                              CTOOL_FALSE) >= 0) {
        return pe32_failure(job, source, arena, mark, image_out,
                            CTOOL_ERR_INPUT,
                            CTOOL_PE32_DIAG_BAD_IMPORT, lookup,
                            "PE32 import procedures are not canonical");
      }
      import->file_index = import_index;
      import->library_file_index = index;
      import->library_name = libraries[index].name;
      import->procedure_name = procedure;
      import->hint_name_rva = lookup;
      import->lookup_rva = descriptors[index].lookup_rva +
                           procedure_index * 4u;
      import->iat_rva = descriptors[index].iat_rva +
                        procedure_index * 4u;
      previous_name = procedure;
      import_index++;
      cursor += 2u + procedure.size + 1u;
    }
    if (pe32_read_le32(
            image, descriptors[index].lookup_offset +
                       descriptors[index].import_count * 4u) != 0u ||
        pe32_read_le32(
            image, descriptors[index].iat_offset +
                       descriptors[index].import_count * 4u) != 0u) {
      return pe32_failure(job, source, arena, mark, image_out,
                          CTOOL_ERR_INPUT, CTOOL_PE32_DIAG_BAD_IMPORT,
                          descriptors[index].lookup_rva,
                          "PE32 import thunk tables are not terminated");
    }
  }
  if (cursor != idata->virtual_size) {
    return pe32_failure(job, source, arena, mark, image_out,
                        CTOOL_ERR_INPUT, CTOOL_PE32_DIAG_BAD_IMPORT,
                        idata->virtual_address + cursor,
                        "PE32 import section extent is not canonical");
  }
  image_out->import_libraries = libraries;
  image_out->import_library_count = library_count;
  image_out->imports = imports;
  image_out->import_count = import_count;
  return CTOOL_OK;
}

ctool_status_t ctool_pe32_read(ctool_job_t *job,
                               const ctool_source_t *source,
                               ctool_pe32_image_t *image_out) {
  ctool_bytes_t image;
  ctool_arena_t *arena;
  ctool_arena_mark_t mark;
  ctool_pe32_section_t *sections;
  ctool_u32 section_count;
  ctool_u32 section_table;
  ctool_u32 section_table_size;
  ctool_u32 headers_size;
  ctool_u32 expected_headers_size;
  ctool_u32 image_size;
  ctool_u32 entry_rva;
  ctool_u32 expected_virtual_address;
  ctool_u32 expected_raw_offset;
  ctool_u32 greatest_virtual_end;
  ctool_u32 expected_code_size = 0u;
  ctool_u32 expected_initialized_size = 0u;
  ctool_u32 expected_uninitialized_size = 0u;
  ctool_u32 expected_base_of_code = 0u;
  ctool_u32 expected_base_of_data = 0u;
  ctool_u32 previous_rank = PE32_U32_MAX;
  ctool_bool entry_file_backed = CTOOL_FALSE;
  ctool_bool have_idata = CTOOL_FALSE;
  ctool_u32 directories[PE32_DIRECTORY_COUNT * 2u];
  ctool_u32 index;
  ctool_status_t status;
  if (image_out != (ctool_pe32_image_t *)0) {
    pe32_zero_image(image_out);
  }
  if (job == (ctool_job_t *)0 || source == (const ctool_source_t *)0 ||
      image_out == (ctool_pe32_image_t *)0 ||
      (source->contents.data == (const ctool_u8 *)0 &&
       source->contents.size != 0u) ||
      (source->path.text.data == (const char *)0 &&
       source->path.text.size != 0u)) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  image = source->contents;
  arena = ctool_job_arena(job);
  mark = ctool_arena_mark(arena);
  if (image.size < PE32_DOS_HEADER_SIZE) {
    return pe32_failure(job, source, arena, mark, image_out,
                        CTOOL_ERR_INPUT,
                        CTOOL_PE32_DIAG_BAD_DOS_HEADER, image.size,
                        "PE32 DOS header is truncated");
  }
  for (index = 0u; index < PE32_DOS_HEADER_SIZE; index++) {
    if (image.data[index] != pe32_dos_stub[index]) {
      return pe32_failure(job, source, arena, mark, image_out,
                          CTOOL_ERR_UNSUPPORTED,
                          CTOOL_PE32_DIAG_BAD_DOS_HEADER, index,
                          "PE32 DOS stub is not the CupidLD profile");
    }
  }
  if (pe32_read_le32(image, 60u) != PE32_SIGNATURE_OFFSET ||
      pe32_range_fits(PE32_SIGNATURE_OFFSET, 24u, image.size) ==
          CTOOL_FALSE) {
    return pe32_failure(job, source, arena, mark, image_out,
                        CTOOL_ERR_INPUT,
                        CTOOL_PE32_DIAG_BAD_SIGNATURE, 60u,
                        "PE32 signature or COFF header is truncated");
  }
  if (image.data[PE32_SIGNATURE_OFFSET] != (ctool_u8)'P' ||
      image.data[PE32_SIGNATURE_OFFSET + 1u] != (ctool_u8)'E' ||
      image.data[PE32_SIGNATURE_OFFSET + 2u] != 0u ||
      image.data[PE32_SIGNATURE_OFFSET + 3u] != 0u) {
    return pe32_failure(job, source, arena, mark, image_out,
                        CTOOL_ERR_INPUT,
                        CTOOL_PE32_DIAG_BAD_SIGNATURE,
                        PE32_SIGNATURE_OFFSET, "PE32 signature is invalid");
  }
  section_count = pe32_read_le16(image, PE32_COFF_HEADER_OFFSET + 2u);
  if (pe32_read_le16(image, PE32_COFF_HEADER_OFFSET) != 0x014cu ||
      section_count == 0u || section_count > 5u ||
      pe32_read_le32(image, PE32_COFF_HEADER_OFFSET + 4u) != 0u ||
      pe32_read_le32(image, PE32_COFF_HEADER_OFFSET + 8u) != 0u ||
      pe32_read_le32(image, PE32_COFF_HEADER_OFFSET + 12u) != 0u ||
      pe32_read_le16(image, PE32_COFF_HEADER_OFFSET + 16u) !=
          PE32_OPTIONAL_HEADER_SIZE ||
      pe32_read_le16(image, PE32_COFF_HEADER_OFFSET + 18u) != 0x0103u) {
    return pe32_failure(job, source, arena, mark, image_out,
                        CTOOL_ERR_UNSUPPORTED,
                        CTOOL_PE32_DIAG_UNSUPPORTED_COFF,
                        PE32_COFF_HEADER_OFFSET,
                        "PE32 COFF header is outside the CupidLD profile");
  }
  if (pe32_range_fits(PE32_OPTIONAL_HEADER_OFFSET,
                       PE32_OPTIONAL_HEADER_SIZE, image.size) ==
      CTOOL_FALSE) {
    return pe32_failure(job, source, arena, mark, image_out,
                        CTOOL_ERR_INPUT,
                        CTOOL_PE32_DIAG_BAD_OPTIONAL_HEADER, image.size,
                        "PE32 optional header is truncated");
  }
  if (pe32_read_le16(image, PE32_OPTIONAL_HEADER_OFFSET) != 0x010bu) {
    return pe32_failure(job, source, arena, mark, image_out,
                        CTOOL_ERR_UNSUPPORTED,
                        CTOOL_PE32_DIAG_BAD_OPTIONAL_HEADER,
                        PE32_OPTIONAL_HEADER_OFFSET,
                        "PE optional header is not PE32");
  }
  entry_rva = pe32_read_le32(image, PE32_OPTIONAL_HEADER_OFFSET + 16u);
  image_size = pe32_read_le32(image, PE32_OPTIONAL_HEADER_OFFSET + 56u);
  headers_size = pe32_read_le32(image, PE32_OPTIONAL_HEADER_OFFSET + 60u);
  if (image_size > CTOOL_PE32_IMAGE_SIZE_LIMIT) {
    return pe32_failure(job, source, arena, mark, image_out,
                        CTOOL_ERR_UNSUPPORTED,
                        CTOOL_PE32_DIAG_BAD_OPTIONAL_HEADER,
                        PE32_OPTIONAL_HEADER_OFFSET + 56u,
                        "PE32 image exceeds CupidLD's 2 GiB RVA range");
  }
  if (image.data[PE32_OPTIONAL_HEADER_OFFSET + 2u] != 0u ||
      image.data[PE32_OPTIONAL_HEADER_OFFSET + 3u] != 0u ||
      pe32_read_le32(image, PE32_OPTIONAL_HEADER_OFFSET + 28u) !=
          CTOOL_PE32_IMAGE_BASE ||
      pe32_read_le32(image, PE32_OPTIONAL_HEADER_OFFSET + 32u) !=
          CTOOL_PE32_SECTION_ALIGNMENT ||
      pe32_read_le32(image, PE32_OPTIONAL_HEADER_OFFSET + 36u) !=
          CTOOL_PE32_FILE_ALIGNMENT ||
      pe32_read_le16(image, PE32_OPTIONAL_HEADER_OFFSET + 40u) != 6u ||
      pe32_read_le16(image, PE32_OPTIONAL_HEADER_OFFSET + 42u) != 0u ||
      pe32_read_le16(image, PE32_OPTIONAL_HEADER_OFFSET + 44u) != 0u ||
      pe32_read_le16(image, PE32_OPTIONAL_HEADER_OFFSET + 46u) != 0u ||
      pe32_read_le16(image, PE32_OPTIONAL_HEADER_OFFSET + 48u) != 6u ||
      pe32_read_le16(image, PE32_OPTIONAL_HEADER_OFFSET + 50u) != 0u ||
      pe32_read_le32(image, PE32_OPTIONAL_HEADER_OFFSET + 52u) != 0u ||
      image_size == 0u ||
      (image_size & (CTOOL_PE32_SECTION_ALIGNMENT - 1u)) != 0u ||
      headers_size == 0u ||
      (headers_size & (CTOOL_PE32_FILE_ALIGNMENT - 1u)) != 0u ||
      headers_size > image.size ||
      pe32_read_le32(image, PE32_OPTIONAL_HEADER_OFFSET + 64u) != 0u ||
      pe32_read_le16(image, PE32_OPTIONAL_HEADER_OFFSET + 68u) != 3u ||
      pe32_read_le16(image, PE32_OPTIONAL_HEADER_OFFSET + 70u) != 0x0100u ||
      pe32_read_le32(image, PE32_OPTIONAL_HEADER_OFFSET + 72u) !=
          0x00100000u ||
      pe32_read_le32(image, PE32_OPTIONAL_HEADER_OFFSET + 76u) !=
          0x00100000u ||
      pe32_read_le32(image, PE32_OPTIONAL_HEADER_OFFSET + 80u) !=
          0x00100000u ||
      pe32_read_le32(image, PE32_OPTIONAL_HEADER_OFFSET + 84u) !=
          0x00001000u ||
      pe32_read_le32(image, PE32_OPTIONAL_HEADER_OFFSET + 88u) != 0u ||
      pe32_read_le32(image, PE32_OPTIONAL_HEADER_OFFSET + 92u) !=
          PE32_DIRECTORY_COUNT ||
      pe32_add_overflows(CTOOL_PE32_IMAGE_BASE, entry_rva) == CTOOL_TRUE) {
    return pe32_failure(job, source, arena, mark, image_out,
                        CTOOL_ERR_UNSUPPORTED,
                        CTOOL_PE32_DIAG_BAD_OPTIONAL_HEADER,
                        PE32_OPTIONAL_HEADER_OFFSET,
                        "PE32 optional header is outside the CupidLD profile");
  }
  for (index = 0u; index < PE32_DIRECTORY_COUNT; index++) {
    ctool_u32 offset =
        PE32_OPTIONAL_HEADER_OFFSET + 96u + index * 8u;
    directories[index * 2u] = pe32_read_le32(image, offset);
    directories[index * 2u + 1u] = pe32_read_le32(image, offset + 4u);
    if (index != PE32_IMPORT_DIRECTORY && index != PE32_IAT_DIRECTORY &&
        (directories[index * 2u] != 0u ||
         directories[index * 2u + 1u] != 0u)) {
      return pe32_failure(job, source, arena, mark, image_out,
                          CTOOL_ERR_UNSUPPORTED,
                          CTOOL_PE32_DIAG_BAD_DIRECTORY, offset,
                          "PE32 data directory is unsupported");
    }
  }
  section_table = PE32_OPTIONAL_HEADER_OFFSET + PE32_OPTIONAL_HEADER_SIZE;
  if (pe32_mul_overflows(section_count, PE32_SECTION_HEADER_SIZE) ==
      CTOOL_TRUE) {
    return pe32_failure(job, source, arena, mark, image_out,
                        CTOOL_ERR_OVERFLOW, CTOOL_PE32_DIAG_BAD_SECTION,
                        section_table, "PE32 section-table size overflows");
  }
  section_table_size = section_count * PE32_SECTION_HEADER_SIZE;
  if (pe32_range_fits(section_table, section_table_size, image.size) ==
      CTOOL_FALSE ||
      pe32_align(section_table + section_table_size,
                 CTOOL_PE32_FILE_ALIGNMENT,
                 &expected_headers_size) != CTOOL_OK ||
      headers_size != expected_headers_size ||
      pe32_bytes_are_zero(image, section_table + section_table_size,
                          headers_size - section_table -
                              section_table_size) == CTOOL_FALSE) {
    return pe32_failure(job, source, arena, mark, image_out,
                        CTOOL_ERR_INPUT, CTOOL_PE32_DIAG_BAD_SECTION,
                        section_table,
                        "PE32 section table or header extent is invalid");
  }
  status = pe32_alloc_array(arena, section_count,
                            (ctool_u32)sizeof(*sections),
                            (void **)&sections);
  if (status != CTOOL_OK) {
    return pe32_failure(job, source, arena, mark, image_out, status,
                        CTOOL_PE32_DIAG_LIMIT, section_table,
                        "PE32 section metadata limit exceeded");
  }
  expected_virtual_address = CTOOL_PE32_SECTION_ALIGNMENT;
  expected_raw_offset = headers_size;
  greatest_virtual_end = headers_size;
  for (index = 0u; index < section_count; index++) {
    ctool_pe32_section_t *section = &sections[index];
    ctool_u32 offset = section_table + index * PE32_SECTION_HEADER_SIZE;
    ctool_u32 name_size = 0u;
    ctool_u32 rank;
    ctool_u32 expected_characteristics;
    ctool_u32 virtual_end;
    ctool_u32 aligned_virtual_end;
    ctool_u32 expected_file_size;
    while (name_size < 8u && image.data[offset + name_size] != 0u) {
      name_size++;
    }
    section->file_index = index;
    section->name.data = (const char *)(image.data + offset);
    section->name.size = name_size;
    section->virtual_size = pe32_read_le32(image, offset + 8u);
    section->virtual_address = pe32_read_le32(image, offset + 12u);
    section->file_size = pe32_read_le32(image, offset + 16u);
    section->file_offset = pe32_read_le32(image, offset + 20u);
    section->characteristics = pe32_read_le32(image, offset + 36u);
    status = pe32_section_kind(section->name, &section->kind, &rank,
                               &expected_characteristics);
    if (status != CTOOL_OK || name_size == 0u ||
        (name_size < 8u &&
         pe32_bytes_are_zero(image, offset + name_size,
                             8u - name_size) == CTOOL_FALSE) ||
        (previous_rank != PE32_U32_MAX && rank <= previous_rank) ||
        section->characteristics != expected_characteristics ||
        pe32_read_le32(image, offset + 24u) != 0u ||
        pe32_read_le32(image, offset + 28u) != 0u ||
        pe32_read_le16(image, offset + 32u) != 0u ||
        pe32_read_le16(image, offset + 34u) != 0u) {
      return pe32_failure(job, source, arena, mark, image_out,
                          status == CTOOL_ERR_UNSUPPORTED
                              ? CTOOL_ERR_UNSUPPORTED
                              : CTOOL_ERR_INPUT,
                          CTOOL_PE32_DIAG_BAD_SECTION, offset,
                          "PE32 section profile is unsupported or invalid");
    }
    previous_rank = rank;
    if (section->virtual_size == 0u ||
        pe32_add_overflows(section->virtual_address,
                           section->virtual_size) == CTOOL_TRUE) {
      return pe32_failure(job, source, arena, mark, image_out,
                          CTOOL_ERR_INPUT, CTOOL_PE32_DIAG_BAD_SECTION,
                          offset + 8u,
                          "PE32 section virtual range is invalid");
    }
    virtual_end = section->virtual_address + section->virtual_size;
    if (virtual_end > image_size) {
      return pe32_failure(job, source, arena, mark, image_out,
                          CTOOL_ERR_INPUT, CTOOL_PE32_DIAG_BAD_SECTION,
                          offset + 12u,
                          "PE32 section virtual range is outside the image");
    }
    if (section->virtual_address < expected_virtual_address) {
      return pe32_failure(job, source, arena, mark, image_out,
                          CTOOL_ERR_INPUT, CTOOL_PE32_DIAG_BAD_SECTION,
                          offset + 12u,
                          "PE32 section virtual ranges overlap");
    }
    if (section->virtual_address != expected_virtual_address ||
        pe32_align(virtual_end, CTOOL_PE32_SECTION_ALIGNMENT,
                   &aligned_virtual_end) != CTOOL_OK) {
      return pe32_failure(job, source, arena, mark, image_out,
                          CTOOL_ERR_INPUT, CTOOL_PE32_DIAG_BAD_SECTION,
                          offset + 12u,
                          "PE32 section virtual layout is not canonical");
    }
    expected_virtual_address = aligned_virtual_end;
    if (virtual_end > greatest_virtual_end) {
      greatest_virtual_end = virtual_end;
    }
    if (section->kind == CTOOL_PE32_SECTION_BSS) {
      if (section->file_offset != 0u || section->file_size != 0u) {
        return pe32_failure(job, source, arena, mark, image_out,
                            CTOOL_ERR_INPUT,
                            CTOOL_PE32_DIAG_BAD_SECTION, offset + 16u,
                            "PE32 .bss section has file bytes");
      }
      section->contents.data = (const ctool_u8 *)0;
      section->contents.size = 0u;
      expected_uninitialized_size += section->virtual_size;
    } else {
      if (pe32_align(section->virtual_size, CTOOL_PE32_FILE_ALIGNMENT,
                     &expected_file_size) != CTOOL_OK ||
          section->file_offset < expected_raw_offset) {
        return pe32_failure(job, source, arena, mark, image_out,
                            CTOOL_ERR_INPUT,
                            CTOOL_PE32_DIAG_BAD_SECTION, offset + 20u,
                            "PE32 section file ranges overlap");
      }
      if (section->file_offset != expected_raw_offset ||
          section->file_size != expected_file_size ||
          pe32_range_fits(section->file_offset, section->file_size,
                          image.size) == CTOOL_FALSE ||
          pe32_bytes_are_zero(
              image, section->file_offset + section->virtual_size,
              section->file_size - section->virtual_size) == CTOOL_FALSE) {
        return pe32_failure(job, source, arena, mark, image_out,
                            CTOOL_ERR_INPUT,
                            CTOOL_PE32_DIAG_BAD_SECTION, offset + 16u,
                            "PE32 section file range is invalid");
      }
      section->contents.data = image.data + section->file_offset;
      section->contents.size = section->virtual_size;
      expected_raw_offset = section->file_offset + section->file_size;
      if (section->kind == CTOOL_PE32_SECTION_TEXT) {
        expected_code_size += section->file_size;
      } else {
        expected_initialized_size += section->file_size;
      }
    }
    if (section->kind == CTOOL_PE32_SECTION_TEXT) {
      expected_base_of_code = section->virtual_address;
    } else if (expected_base_of_data == 0u) {
      expected_base_of_data = section->virtual_address;
    }
    if (section->kind == CTOOL_PE32_SECTION_IDATA) {
      have_idata = CTOOL_TRUE;
    }
    if ((section->characteristics & CTOOL_PE32_SCN_EXECUTE) != 0u &&
        section->file_size != 0u &&
        entry_rva >= section->virtual_address &&
        entry_rva - section->virtual_address < section->virtual_size) {
      entry_file_backed = CTOOL_TRUE;
    }
  }
  if (pe32_align(greatest_virtual_end, CTOOL_PE32_SECTION_ALIGNMENT,
                 &expected_virtual_address) != CTOOL_OK ||
      expected_virtual_address != image_size || expected_raw_offset !=
                                                   image.size ||
      pe32_read_le32(image, PE32_OPTIONAL_HEADER_OFFSET + 4u) !=
          expected_code_size ||
      pe32_read_le32(image, PE32_OPTIONAL_HEADER_OFFSET + 8u) !=
          expected_initialized_size ||
      pe32_read_le32(image, PE32_OPTIONAL_HEADER_OFFSET + 12u) !=
          expected_uninitialized_size ||
      pe32_read_le32(image, PE32_OPTIONAL_HEADER_OFFSET + 20u) !=
          expected_base_of_code ||
      pe32_read_le32(image, PE32_OPTIONAL_HEADER_OFFSET + 24u) !=
          expected_base_of_data) {
    return pe32_failure(job, source, arena, mark, image_out,
                        CTOOL_ERR_INPUT, CTOOL_PE32_DIAG_BAD_SECTION,
                        section_table,
                        "PE32 image and section extents disagree");
  }
  if (entry_file_backed == CTOOL_FALSE) {
    return pe32_failure(job, source, arena, mark, image_out,
                        CTOOL_ERR_INPUT, CTOOL_PE32_DIAG_BAD_ENTRY,
                        PE32_OPTIONAL_HEADER_OFFSET + 16u,
                        "PE32 entry is not file-backed executable code");
  }
  if ((directories[PE32_IMPORT_DIRECTORY * 2u] != 0u) !=
          (have_idata == CTOOL_TRUE) ||
      (directories[PE32_IAT_DIRECTORY * 2u] != 0u) !=
          (have_idata == CTOOL_TRUE)) {
    return pe32_failure(job, source, arena, mark, image_out,
                        CTOOL_ERR_INPUT, CTOOL_PE32_DIAG_BAD_DIRECTORY,
                        PE32_OPTIONAL_HEADER_OFFSET + 96u,
                        "PE32 import directories and .idata disagree");
  }
  image_out->image = image;
  image_out->entry_rva = entry_rva;
  image_out->entry_point = CTOOL_PE32_IMAGE_BASE + entry_rva;
  image_out->image_base = CTOOL_PE32_IMAGE_BASE;
  image_out->base_of_code = expected_base_of_code;
  image_out->base_of_data = expected_base_of_data;
  image_out->code_size = expected_code_size;
  image_out->initialized_data_size = expected_initialized_size;
  image_out->uninitialized_data_size = expected_uninitialized_size;
  image_out->image_size = image_size;
  image_out->headers_size = headers_size;
  image_out->section_alignment = CTOOL_PE32_SECTION_ALIGNMENT;
  image_out->file_alignment = CTOOL_PE32_FILE_ALIGNMENT;
  image_out->subsystem = 3u;
  image_out->dll_characteristics = 0x0100u;
  image_out->import_directory_rva =
      directories[PE32_IMPORT_DIRECTORY * 2u];
  image_out->import_directory_size =
      directories[PE32_IMPORT_DIRECTORY * 2u + 1u];
  image_out->iat_directory_rva = directories[PE32_IAT_DIRECTORY * 2u];
  image_out->iat_directory_size =
      directories[PE32_IAT_DIRECTORY * 2u + 1u];
  image_out->sections = sections;
  image_out->section_count = section_count;
  status = pe32_parse_imports(
      job, source, arena, mark, image_out, sections, section_count,
      image_out->import_directory_rva, image_out->import_directory_size,
      image_out->iat_directory_rva, image_out->iat_directory_size);
  return status;
}
