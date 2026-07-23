#include "cupidobj.h"

#define CTOOL_OBJ_WRAP_FLAGS                                                \
  (CTOOL_ELF32_SHF_WRITE | CTOOL_ELF32_SHF_ALLOC |                        \
   CTOOL_ELF32_SHF_EXECINSTR | CTOOL_ELF32_SHF_TLS |                      \
   CTOOL_ELF32_SHF_EXCLUDE)

typedef struct {
  ctool_u32 address;
  ctool_u32 order;
  ctool_bytes_t contents;
} obj_flat_region_t;

static void obj_zero(void *destination, ctool_u32 size) {
  ctool_u8 *bytes = (ctool_u8 *)destination;
  ctool_u32 index;
  for (index = 0u; index < size; index++) {
    bytes[index] = 0u;
  }
}

static ctool_bool obj_string_valid(ctool_string_t string) {
  ctool_u32 index;
  if (string.data == (const char *)0 || string.size == 0u) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < string.size; index++) {
    if (string.data[index] == '\0') {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_bool obj_string_equal(ctool_string_t left,
                                    ctool_string_t right) {
  ctool_u32 index;
  if (left.size != right.size) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < left.size; index++) {
    if (left.data[index] != right.data[index]) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_bool obj_string_has_prefix(ctool_string_t string,
                                        const char *prefix) {
  ctool_string_t prefix_string = ctool_string(prefix);
  ctool_u32 index;
  if (string.size < prefix_string.size) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < prefix_string.size; index++) {
    if (string.data[index] != prefix_string.data[index]) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_bool obj_power_of_two(ctool_u32 value) {
  return value != 0u && (value & (value - 1u)) == 0u ? CTOOL_TRUE
                                                     : CTOOL_FALSE;
}

static ctool_status_t obj_emit_failure(ctool_job_t *job,
                                        const ctool_source_t *source,
                                        ctool_status_t status,
                                        ctool_u32 code,
                                        ctool_u32 column,
                                        const char *message) {
  ctool_diagnostic_t diagnostic;
  ctool_status_t diagnostic_status;
  if (job == (ctool_job_t *)0) {
    return status;
  }
  diagnostic.severity = CTOOL_DIAG_ERROR;
  diagnostic.code = code;
  diagnostic.path = source != (const ctool_source_t *)0
                        ? source->path.text
                        : ctool_string("");
  diagnostic.line = 0u;
  diagnostic.column = column;
  diagnostic.message = ctool_string(message);
  diagnostic_status = ctool_job_emit(job, &diagnostic);
  return diagnostic_status == CTOOL_OK ? status : diagnostic_status;
}

static ctool_bool obj_region_less(const obj_flat_region_t *left,
                                  const obj_flat_region_t *right) {
  return left->address < right->address ||
                 (left->address == right->address && left->order < right->order)
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static void obj_region_swap(obj_flat_region_t *left,
                            obj_flat_region_t *right) {
  obj_flat_region_t temporary = *left;
  *left = *right;
  *right = temporary;
}

static void obj_region_sift_down(obj_flat_region_t *regions,
                                 ctool_u32 root, ctool_u32 count) {
  for (;;) {
    ctool_u32 child;
    ctool_u32 selected;
    if (root >= count / 2u) {
      return;
    }
    child = root * 2u + 1u;
    selected = root;
    if (obj_region_less(&regions[selected], &regions[child]) == CTOOL_TRUE) {
      selected = child;
    }
    if (child + 1u < count &&
        obj_region_less(&regions[selected], &regions[child + 1u]) ==
            CTOOL_TRUE) {
      selected = child + 1u;
    }
    if (selected == root) {
      return;
    }
    obj_region_swap(&regions[root], &regions[selected]);
    root = selected;
  }
}

static void obj_region_sort(obj_flat_region_t *regions, ctool_u32 count) {
  ctool_u32 start = count / 2u;
  ctool_u32 end = count;
  while (start != 0u) {
    start--;
    obj_region_sift_down(regions, start, count);
  }
  while (end > 1u) {
    obj_region_swap(&regions[0], &regions[end - 1u]);
    end--;
    obj_region_sift_down(regions, 0u, end);
  }
}

static ctool_status_t obj_extract_failure(
    ctool_job_t *job, const ctool_source_t *source, ctool_arena_t *arena,
    ctool_arena_mark_t mark, ctool_status_t status, ctool_u32 code,
    ctool_u32 column, const char *message) {
  ctool_status_t rewind_status = ctool_arena_rewind(arena, mark);
  if (rewind_status != CTOOL_OK) {
    return rewind_status;
  }
  return obj_emit_failure(job, source, status, code, column, message);
}

static ctool_status_t obj_wrap(
    ctool_job_t *job, const ctool_obj_request_t *request,
    ctool_buffer_t *output, ctool_obj_result_t *result_out) {
  const ctool_obj_wrap_binary_request_t *wrap = &request->as.wrap_binary;
  ctool_arena_t *arena = ctool_job_arena(job);
  ctool_arena_mark_t arena_mark = ctool_arena_mark(arena);
  ctool_bytes_t contents = request->input->contents;
  ctool_elf32_section_spec_t section;
  ctool_elf32_symbol_spec_t symbols[3];
  ctool_elf32_object_spec_t object;
  ctool_status_t status;
  ctool_status_t rewind_status;
  ctool_u32 index;
  ctool_u32 removed = 0u;

  if (request->input->contents.data == (const ctool_u8 *)0 &&
      request->input->contents.size != 0u) {
    return obj_emit_failure(job, request->input, CTOOL_ERR_INVALID_ARGUMENT,
                            CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
                            "CupidObj input bytes are invalid");
  }
  if (obj_string_valid(wrap->section_name) == CTOOL_FALSE ||
      obj_power_of_two(wrap->section_alignment) == CTOOL_FALSE ||
      (wrap->section_flags & ~CTOOL_OBJ_WRAP_FLAGS) != 0u ||
      obj_string_equal(wrap->section_name, ctool_string(".symtab")) ==
          CTOOL_TRUE ||
      obj_string_equal(wrap->section_name, ctool_string(".strtab")) ==
          CTOOL_TRUE ||
      obj_string_equal(wrap->section_name, ctool_string(".shstrtab")) ==
          CTOOL_TRUE ||
      obj_string_has_prefix(wrap->section_name, ".rel.") == CTOOL_TRUE) {
    return obj_emit_failure(job, request->input, CTOOL_ERR_INPUT,
                            CTOOL_OBJ_DIAG_INVALID_SECTION, 0u,
                            "CupidObj wrapped section description is invalid");
  }
  if (obj_string_valid(wrap->start_symbol) == CTOOL_FALSE ||
      obj_string_valid(wrap->end_symbol) == CTOOL_FALSE ||
      obj_string_valid(wrap->size_symbol) == CTOOL_FALSE) {
    return obj_emit_failure(job, request->input, CTOOL_ERR_INPUT,
                            CTOOL_OBJ_DIAG_INVALID_SYMBOL, 0u,
                            "CupidObj wrapped symbol name is invalid");
  }
  if (obj_string_equal(wrap->start_symbol, wrap->end_symbol) == CTOOL_TRUE ||
      obj_string_equal(wrap->start_symbol, wrap->size_symbol) == CTOOL_TRUE ||
      obj_string_equal(wrap->end_symbol, wrap->size_symbol) == CTOOL_TRUE) {
    return obj_emit_failure(job, request->input, CTOOL_ERR_INPUT,
                            CTOOL_OBJ_DIAG_SYMBOL_COLLISION, 0u,
                            "CupidObj wrapped symbol names collide");
  }

  if (request->operation == CTOOL_OBJ_WRAP_TEXT) {
    ctool_u8 *normalized;
    ctool_u32 write_index = 0u;
    for (index = 0u; index + 1u < contents.size; index++) {
      if (contents.data[index] == (ctool_u8)'\r' &&
          contents.data[index + 1u] == (ctool_u8)'\n') {
        removed++;
        index++;
      }
    }
    if (removed != 0u) {
      status = ctool_arena_alloc(arena, contents.size - removed, 1u,
                                 (void **)&normalized);
      if (status != CTOOL_OK) {
        rewind_status = ctool_arena_rewind(arena, arena_mark);
        if (rewind_status != CTOOL_OK) {
          return rewind_status;
        }
        return obj_emit_failure(job, request->input, status,
                                CTOOL_OBJ_DIAG_LIMIT, 0u,
                                "CupidObj could not normalize text input");
      }
      for (index = 0u; index < contents.size; index++) {
        if (contents.data[index] == (ctool_u8)'\r' &&
            index + 1u < contents.size &&
            contents.data[index + 1u] == (ctool_u8)'\n') {
          continue;
        }
        normalized[write_index] = contents.data[index];
        write_index++;
      }
      contents = ctool_bytes(normalized, write_index);
    }
  }

  obj_zero(&section, (ctool_u32)sizeof(section));
  obj_zero(symbols, (ctool_u32)sizeof(symbols));
  obj_zero(&object, (ctool_u32)sizeof(object));
  section.name = wrap->section_name;
  section.type = CTOOL_ELF32_SHT_PROGBITS;
  section.flags = wrap->section_flags;
  section.alignment = wrap->section_alignment;
  section.size = contents.size;
  section.contents = contents;

  symbols[0].name = wrap->start_symbol;
  symbols[0].binding = CTOOL_ELF32_BIND_GLOBAL;
  symbols[0].type = CTOOL_ELF32_SYMBOL_NOTYPE;
  symbols[0].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[0].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  symbols[0].section = 0u;

  symbols[1] = symbols[0];
  symbols[1].name = wrap->end_symbol;
  symbols[1].value = contents.size;

  symbols[2] = symbols[0];
  symbols[2].name = wrap->size_symbol;
  symbols[2].placement = CTOOL_ELF32_SYMBOL_ABSOLUTE;
  symbols[2].section = CTOOL_ELF32_NO_SECTION;
  symbols[2].value = contents.size;

  object.sections = &section;
  object.section_count = 1u;
  object.symbols = symbols;
  object.symbol_count = 3u;
  status = ctool_elf32_write(job, &object, output);
  rewind_status = ctool_arena_rewind(arena, arena_mark);
  if (rewind_status != CTOOL_OK) {
    return rewind_status;
  }
  if (status != CTOOL_OK) {
    ctool_u32 code = status == CTOOL_ERR_LIMIT || status == CTOOL_ERR_OVERFLOW ||
                             status == CTOOL_ERR_NO_MEMORY
                         ? CTOOL_OBJ_DIAG_LIMIT
                         : CTOOL_OBJ_DIAG_OUTPUT;
    return obj_emit_failure(job, request->input, status, code, 0u,
                            "CupidObj could not emit the wrapped object");
  }
  result_out->bytes = ctool_buffer_view(output);
  return CTOOL_OK;
}

static ctool_status_t obj_extract_flat(
    ctool_job_t *job, const ctool_obj_request_t *request,
    ctool_buffer_t *output, ctool_obj_result_t *result_out) {
  ctool_arena_t *arena = ctool_job_arena(job);
  ctool_arena_mark_t mark = ctool_arena_mark(arena);
  ctool_elf32_object_t object;
  obj_flat_region_t *regions = (obj_flat_region_t *)0;
  ctool_u32 load_count = 0u;
  ctool_u32 file_region_count = 0u;
  ctool_u32 index;
  ctool_u32 position = 0u;
  ctool_u32 cursor;
  ctool_u32 end_address;
  ctool_status_t status = ctool_elf32_read(job, request->input, &object);

  if (status != CTOOL_OK) {
    /* The ELF reader has already rewound its temporary state and committed
     * its own precise diagnostic after that rewind. */
    return obj_emit_failure(job, request->input, status,
                            CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
                            "CupidObj input is not a valid static i386 ELF");
  }
  if (object.file_type != CTOOL_ELF32_ET_EXEC) {
    return obj_extract_failure(job, request->input, arena, mark,
                               CTOOL_ERR_UNSUPPORTED,
                               CTOOL_OBJ_DIAG_UNSUPPORTED, 0u,
                               "CupidObj flat extraction requires ET_EXEC");
  }
  for (index = 0u; index < object.program_header_count; index++) {
    if (object.program_headers[index].type == CTOOL_ELF32_PT_LOAD) {
      load_count++;
      if (object.program_headers[index].file_size != 0u) {
        file_region_count++;
      }
    }
  }
  if (load_count == 0u) {
    for (index = 0u; index < object.section_count; index++) {
      const ctool_elf32_section_t *section = &object.sections[index];
      if ((section->flags & CTOOL_ELF32_SHF_ALLOC) == 0u) {
        continue;
      }
      if (section->type == CTOOL_ELF32_SHT_NOBITS) {
        continue;
      }
      if (section->type != CTOOL_ELF32_SHT_PROGBITS) {
        return obj_extract_failure(
            job, request->input, arena, mark, CTOOL_ERR_UNSUPPORTED,
            CTOOL_OBJ_DIAG_UNSUPPORTED, section->file_offset,
            "CupidObj section fallback found unsupported allocated content");
      }
      if (section->size != 0u) {
        file_region_count++;
      }
    }
  }
  if (file_region_count == 0u) {
    return obj_extract_failure(job, request->input, arena, mark,
                               CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_NO_LOAD, 0u,
                               "CupidObj executable has no initialized load");
  }
  status = ctool_arena_alloc_zero(
      arena, file_region_count, (ctool_u32)sizeof(*regions),
      (ctool_u32)sizeof(void *), (void **)&regions);
  if (status != CTOOL_OK) {
    return obj_extract_failure(job, request->input, arena, mark, status,
                               CTOOL_OBJ_DIAG_LIMIT, 0u,
                               "CupidObj flat region limit exceeded");
  }
  if (load_count != 0u) {
    for (index = 0u; index < object.program_header_count; index++) {
      const ctool_elf32_program_header_t *header =
          &object.program_headers[index];
      if (header->type != CTOOL_ELF32_PT_LOAD || header->file_size == 0u) {
        continue;
      }
      regions[position].address = header->physical_address;
      regions[position].order = header->file_index;
      regions[position].contents = header->contents;
      position++;
    }
  } else {
    for (index = 0u; index < object.section_count; index++) {
      const ctool_elf32_section_t *section = &object.sections[index];
      if ((section->flags & CTOOL_ELF32_SHF_ALLOC) == 0u ||
          section->type != CTOOL_ELF32_SHT_PROGBITS || section->size == 0u) {
        continue;
      }
      regions[position].address = section->address;
      regions[position].order = section->file_index;
      regions[position].contents = section->contents;
      position++;
    }
  }
  obj_region_sort(regions, file_region_count);
  cursor = regions[0].address;
  for (index = 0u; index < file_region_count; index++) {
    const obj_flat_region_t *region = &regions[index];
    if (region->address > 0xffffffffu - region->contents.size) {
      return obj_extract_failure(job, request->input, arena, mark,
                                 CTOOL_ERR_OVERFLOW,
                                 CTOOL_OBJ_DIAG_ADDRESS_OVERFLOW,
                                 region->address,
                                 "CupidObj flat address range overflows");
    }
    end_address = region->address + region->contents.size;
    if (region->address < cursor) {
      return obj_extract_failure(job, request->input, arena, mark,
                                 CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_OVERLAP,
                                 region->address,
                                 "CupidObj initialized load ranges overlap");
    }
    status = ctool_buffer_fill(output, 0u, region->address - cursor);
    if (status == CTOOL_OK) {
      status = ctool_buffer_append(output, region->contents);
    }
    if (status != CTOOL_OK) {
      ctool_u32 code = status == CTOOL_ERR_LIMIT ||
                               status == CTOOL_ERR_OVERFLOW ||
                               status == CTOOL_ERR_NO_MEMORY
                           ? CTOOL_OBJ_DIAG_LIMIT
                           : CTOOL_OBJ_DIAG_OUTPUT;
      return obj_extract_failure(job, request->input, arena, mark, status,
                                 code, region->address,
                                 "CupidObj could not emit the flat image");
    }
    cursor = end_address;
  }
  result_out->base_address = regions[0].address;
  result_out->end_address = cursor;
  status = ctool_arena_rewind(arena, mark);
  if (status != CTOOL_OK) {
    return status;
  }
  result_out->bytes = ctool_buffer_view(output);
  return CTOOL_OK;
}

ctool_status_t ctool_obj_transform(ctool_job_t *job,
                                    const ctool_obj_request_t *request,
                                    ctool_buffer_t *output,
                                    ctool_obj_result_t *result_out) {
  const ctool_source_t *source =
      request != (const ctool_obj_request_t *)0 ? request->input
                                                : (const ctool_source_t *)0;
  ctool_u32 output_mark;
  ctool_status_t status;
  if (result_out == (ctool_obj_result_t *)0) {
    return obj_emit_failure(job, source, CTOOL_ERR_INVALID_ARGUMENT,
                            CTOOL_OBJ_DIAG_INVALID_REQUEST, 0u,
                            "CupidObj result is required");
  }
  obj_zero(result_out, (ctool_u32)sizeof(*result_out));
  if (job == (ctool_job_t *)0 ||
      request == (const ctool_obj_request_t *)0 ||
      request->input == (const ctool_source_t *)0 ||
      output == (ctool_buffer_t *)0 || ctool_buffer_view(output).size != 0u) {
    return obj_emit_failure(job, source, CTOOL_ERR_INVALID_ARGUMENT,
                            CTOOL_OBJ_DIAG_INVALID_REQUEST, 0u,
                            "CupidObj request and empty output are required");
  }
  output_mark = ctool_buffer_mark(output);
  if (request->operation == CTOOL_OBJ_WRAP_BINARY ||
      request->operation == CTOOL_OBJ_WRAP_TEXT) {
    status = obj_wrap(job, request, output, result_out);
  } else if (request->operation == CTOOL_OBJ_EXTRACT_FLAT) {
    status = obj_extract_flat(job, request, output, result_out);
  } else {
    status = obj_emit_failure(job, request->input,
                              CTOOL_ERR_INVALID_ARGUMENT,
                              CTOOL_OBJ_DIAG_INVALID_REQUEST, 0u,
                              "CupidObj operation is invalid");
  }
  if (status != CTOOL_OK) {
    (void)ctool_buffer_rewind(output, output_mark);
    obj_zero(result_out, (ctool_u32)sizeof(*result_out));
  }
  return status;
}
