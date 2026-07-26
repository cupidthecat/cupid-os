#include "cupiddis.h"

#define DIS_U32_MAX 4294967295u
#define DIS_ELF32_SHT_NULL 0u
#define DIS_ELF32_SHT_SYMTAB 2u
#define DIS_ELF32_SHT_STRTAB 3u
#define DIS_ELF32_SHT_REL 9u
#define DIS_ELF32_PT_GNU_STACK 0x6474e551u

static ctool_status_t dis_prepare_report_orders(ctool_job_t *job,
                                                 ctool_dis_report_t *report);
static ctool_status_t dis_prepare_raw_label_order(ctool_job_t *job,
                                                   ctool_dis_report_t *report);

static void dis_zero_report(ctool_dis_report_t *report) {
  ctool_u8 *bytes = (ctool_u8 *)report;
  ctool_u32 index;
  for (index = 0u; index < (ctool_u32)sizeof(*report); index++) {
    bytes[index] = 0u;
  }
}

static ctool_status_t dis_emit(ctool_job_t *job, ctool_string_t path,
                               ctool_u32 code, ctool_u32 column,
                               const char *message, ctool_status_t status) {
  ctool_diagnostic_t diagnostic;
  ctool_status_t emitted;
  diagnostic.severity = CTOOL_DIAG_ERROR;
  diagnostic.code = code;
  diagnostic.path = path;
  diagnostic.line = 0u;
  diagnostic.column = column;
  diagnostic.message = ctool_string(message);
  emitted = ctool_job_emit(job, &diagnostic);
  return emitted == CTOOL_OK ? status : emitted;
}

static ctool_status_t dis_bad_request(ctool_job_t *job,
                                      const ctool_source_t *source,
                                      const char *message) {
  ctool_string_t path = ctool_string("");
  if (source != (const ctool_source_t *)0) {
    path = source->path.text;
  }
  return dis_emit(job, path, CTOOL_DIS_DIAG_INVALID_REQUEST, 0u, message,
                  CTOOL_ERR_INVALID_ARGUMENT);
}

typedef enum {
  DIS_RAW_MAP_VALID = 0,
  DIS_RAW_MAP_NO_RANGES,
  DIS_RAW_MAP_MISSING_STORAGE,
  DIS_RAW_MAP_EMPTY_INPUT,
  DIS_RAW_MAP_TOO_MANY_RANGES,
  DIS_RAW_MAP_NONZERO_START,
  DIS_RAW_MAP_INVALID_MODE,
  DIS_RAW_MAP_OUTSIDE_INPUT,
  DIS_RAW_MAP_UNORDERED
} dis_raw_map_issue_t;

static ctool_bool dis_x86_mode_valid(ctool_x86_mode_t mode) {
  return mode == CTOOL_X86_MODE_16 || mode == CTOOL_X86_MODE_32
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static dis_raw_map_issue_t dis_raw_map_issue(
    ctool_u32 source_size, const ctool_dis_raw_range_t *ranges,
    ctool_u32 range_count) {
  ctool_u32 index;
  if (range_count == 0u) {
    return DIS_RAW_MAP_NO_RANGES;
  }
  if (ranges == (const ctool_dis_raw_range_t *)0) {
    return DIS_RAW_MAP_MISSING_STORAGE;
  }
  if (source_size == 0u) {
    return DIS_RAW_MAP_EMPTY_INPUT;
  }
  if (range_count > source_size) {
    return DIS_RAW_MAP_TOO_MANY_RANGES;
  }
  if (ranges[0].offset != 0u) {
    return DIS_RAW_MAP_NONZERO_START;
  }
  for (index = 0u; index < range_count; index++) {
    if (dis_x86_mode_valid(ranges[index].mode) == CTOOL_FALSE) {
      return DIS_RAW_MAP_INVALID_MODE;
    }
    if (ranges[index].offset >= source_size) {
      return DIS_RAW_MAP_OUTSIDE_INPUT;
    }
    if (index != 0u && ranges[index].offset <= ranges[index - 1u].offset) {
      return DIS_RAW_MAP_UNORDERED;
    }
  }
  return DIS_RAW_MAP_VALID;
}

static const char *dis_raw_map_message(dis_raw_map_issue_t issue) {
  switch (issue) {
  case DIS_RAW_MAP_NO_RANGES:
    return "raw mode map requires at least one range";
  case DIS_RAW_MAP_MISSING_STORAGE:
    return "raw mode map storage is missing";
  case DIS_RAW_MAP_EMPTY_INPUT:
    return "raw mode map requires nonempty input";
  case DIS_RAW_MAP_TOO_MANY_RANGES:
    return "raw mode map has too many ranges";
  case DIS_RAW_MAP_NONZERO_START:
    return "raw mode map must start at offset zero";
  case DIS_RAW_MAP_INVALID_MODE:
    return "raw mode map requires 16-bit or 32-bit range modes";
  case DIS_RAW_MAP_OUTSIDE_INPUT:
    return "raw mode map offset is outside input";
  case DIS_RAW_MAP_UNORDERED:
    return "raw mode map offsets must increase";
  case DIS_RAW_MAP_VALID:
  default:
    return "raw mode map is invalid";
  }
}

ctool_status_t ctool_dis_inspect(ctool_job_t *job,
                                  const ctool_source_t *source,
                                  const ctool_dis_request_t *request,
                                  ctool_dis_report_t *report_out) {
  ctool_status_t status;
  ctool_u32 index;
  ctool_arena_t *arena;
  ctool_arena_mark_t mark;
  if (report_out != (ctool_dis_report_t *)0) {
    dis_zero_report(report_out);
  }
  if (job == (ctool_job_t *)0 || source == (const ctool_source_t *)0 ||
      request == (const ctool_dis_request_t *)0 ||
      report_out == (ctool_dis_report_t *)0 ||
      (source->contents.data == (const ctool_u8 *)0 &&
       source->contents.size != 0u) ||
      (source->path.text.data == (const char *)0 &&
       source->path.text.size != 0u)) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  if (request->views == 0u ||
      (request->views & ~CTOOL_DIS_VIEW_ALL) != 0u) {
    return dis_bad_request(job, source, "CupidDis view selection is invalid");
  }
  if (request->input == CTOOL_DIS_INPUT_RAW) {
    dis_raw_map_issue_t map_issue = DIS_RAW_MAP_VALID;
    if (request->views != CTOOL_DIS_VIEW_DISASSEMBLY) {
      return dis_bad_request(job, source,
                             "raw input only supports disassembly");
    }
    if (request->raw_mode == CTOOL_DIS_RAW_MODE_MAP) {
      map_issue = dis_raw_map_issue(source->contents.size,
                                    request->raw_ranges,
                                    request->raw_range_count);
      if (map_issue != DIS_RAW_MAP_VALID) {
        return dis_bad_request(job, source, dis_raw_map_message(map_issue));
      }
    } else if (dis_x86_mode_valid(request->raw_mode) == CTOOL_FALSE) {
      return dis_bad_request(job, source,
                             "raw input requires 16-bit or 32-bit mode");
    } else if (request->raw_ranges !=
                   (const ctool_dis_raw_range_t *)0 ||
               request->raw_range_count != 0u) {
      return dis_bad_request(job, source,
                             "raw mode ranges require mapped mode");
    }
    if (request->label_count != 0u &&
        request->labels == (const ctool_dis_label_t *)0) {
      return dis_bad_request(job, source, "raw label storage is missing");
    }
    for (index = 0u; index < request->label_count; index++) {
      if (request->labels[index].name.data == (const char *)0 &&
          request->labels[index].name.size != 0u) {
        return dis_bad_request(job, source, "raw label name is invalid");
      }
    }
    if (source->contents.size != 0u &&
        request->raw_base_address >
            DIS_U32_MAX - (source->contents.size - 1u)) {
      return dis_emit(job, source->path.text,
                      CTOOL_DIS_DIAG_ADDRESS_OVERFLOW, 0u,
                      "raw disassembly address range overflows",
                      CTOOL_ERR_OVERFLOW);
    }
    report_out->source = source;
    report_out->input = request->input;
    report_out->views = request->views;
    report_out->mode = request->raw_mode;
    report_out->base_address = request->raw_base_address;
    if (request->raw_mode == CTOOL_DIS_RAW_MODE_MAP) {
      report_out->raw_ranges = request->raw_ranges;
      report_out->raw_range_count = request->raw_range_count;
    }
    report_out->labels = request->labels;
    report_out->label_count = request->label_count;
    arena = ctool_job_arena(job);
    mark = ctool_arena_mark(arena);
    status = dis_prepare_raw_label_order(job, report_out);
    if (status != CTOOL_OK) {
      (void)ctool_arena_rewind(arena, mark);
      dis_zero_report(report_out);
    }
    return status;
  }
  if (request->input != CTOOL_DIS_INPUT_ELF32) {
    return dis_bad_request(job, source, "CupidDis input kind is invalid");
  }
  if (request->label_count != 0u ||
      request->labels != (const ctool_dis_label_t *)0) {
    return dis_bad_request(job, source,
                           "ELF input cannot carry raw labels");
  }
  arena = ctool_job_arena(job);
  mark = ctool_arena_mark(arena);
  status = ctool_elf32_read(job, source, &report_out->elf32);
  if (status != CTOOL_OK) {
    dis_zero_report(report_out);
    return status;
  }
  if ((request->views & CTOOL_DIS_VIEW_DISASSEMBLY) != 0u) {
    for (index = 0u; index < report_out->elf32.section_count; index++) {
      const ctool_elf32_section_t *section =
          &report_out->elf32.sections[index];
      if (section->type == CTOOL_ELF32_SHT_PROGBITS &&
          (section->flags & CTOOL_ELF32_SHF_EXECINSTR) != 0u &&
          section->contents.size != 0u &&
          section->address > DIS_U32_MAX - (section->contents.size - 1u)) {
        (void)ctool_arena_rewind(arena, mark);
        dis_zero_report(report_out);
        return dis_emit(job, source->path.text,
                        CTOOL_DIS_DIAG_ADDRESS_OVERFLOW,
                        section->file_offset,
                        "ELF disassembly address range overflows",
                        CTOOL_ERR_OVERFLOW);
      }
    }
  }
  if (report_out->elf32.file_type == CTOOL_ELF32_ET_REL &&
      (request->views &
       (CTOOL_DIS_VIEW_SYMBOLS | CTOOL_DIS_VIEW_DISASSEMBLY)) != 0u) {
    for (index = 0u; index < report_out->elf32.symbol_count; index++) {
      const ctool_elf32_symbol_t *symbol =
          &report_out->elf32.symbols[index];
      const ctool_elf32_section_t *section;
      if (symbol->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
          ((request->views & CTOOL_DIS_VIEW_SYMBOLS) == 0u &&
           symbol->type != CTOOL_ELF32_SYMBOL_FUNCTION)) {
        continue;
      }
      section = symbol->section_file_index < report_out->elf32.section_count
                    ? &report_out->elf32
                           .sections[symbol->section_file_index]
                    : (const ctool_elf32_section_t *)0;
      if (section != (const ctool_elf32_section_t *)0 &&
          section->address > DIS_U32_MAX - symbol->value) {
        (void)ctool_arena_rewind(arena, mark);
        dis_zero_report(report_out);
        return dis_emit(job, source->path.text,
                        CTOOL_DIS_DIAG_ADDRESS_OVERFLOW,
                        symbol->file_index,
                        "ELF symbol address overflows", CTOOL_ERR_OVERFLOW);
      }
    }
  }
  report_out->source = source;
  report_out->input = request->input;
  report_out->views = request->views;
  report_out->mode = CTOOL_X86_MODE_32;
  status = dis_prepare_report_orders(job, report_out);
  if (status != CTOOL_OK) {
    (void)ctool_arena_rewind(arena, mark);
    dis_zero_report(report_out);
  }
  return status;
}

static ctool_status_t dis_write(ctool_text_sink_t output, const void *data,
                                ctool_u32 size) {
  if (size == 0u) {
    return CTOOL_OK;
  }
  return output.write(output.context, ctool_bytes(data, size));
}

static ctool_status_t dis_literal(ctool_text_sink_t output,
                                  const char *text) {
  ctool_string_t value = ctool_string(text);
  return dis_write(output, value.data, value.size);
}

static ctool_status_t dis_string(ctool_text_sink_t output,
                                 ctool_string_t text) {
  return dis_write(output, text.data, text.size);
}

static ctool_u32 dis_decimal_chars(char *output, ctool_u32 value) {
  char reverse[10];
  ctool_u32 count = 0u;
  ctool_u32 index;
  do {
    reverse[count] = (char)('0' + (char)(value % 10u));
    count++;
    value /= 10u;
  } while (value != 0u);
  for (index = 0u; index < count; index++) {
    output[index] = reverse[count - index - 1u];
  }
  return count;
}

static ctool_status_t dis_decimal(ctool_text_sink_t output, ctool_u32 value) {
  char text[10];
  ctool_u32 count = dis_decimal_chars(text, value);
  return dis_write(output, text, count);
}

static ctool_status_t dis_hex_fixed(ctool_text_sink_t output, ctool_u32 value,
                                    ctool_u32 digits) {
  static const char hex[] = "0123456789ABCDEF";
  char text[8];
  ctool_u32 index;
  for (index = 0u; index < digits; index++) {
    ctool_u32 shift = (digits - index - 1u) * 4u;
    text[index] = hex[(value >> shift) & 0x0fu];
  }
  return dis_write(output, text, digits);
}

static ctool_status_t dis_hex_compact(ctool_text_sink_t output,
                                      ctool_u32 value) {
  ctool_u32 digits = 1u;
  ctool_u32 probe = value;
  while (probe > 0x0fu) {
    digits++;
    probe >>= 4u;
  }
  return dis_hex_fixed(output, value, digits);
}

static ctool_status_t dis_hex_u32(ctool_text_sink_t output, ctool_u32 value) {
  ctool_status_t status = dis_literal(output, "0x");
  return status == CTOOL_OK ? dis_hex_fixed(output, value, 8u) : status;
}

static ctool_status_t dis_hex_value(ctool_text_sink_t output,
                                    ctool_u32 value) {
  ctool_status_t status = dis_literal(output, "0x");
  return status == CTOOL_OK ? dis_hex_compact(output, value) : status;
}

static ctool_status_t dis_space(ctool_text_sink_t output) {
  return dis_literal(output, " ");
}

static const char *dis_file_type_name(ctool_u32 file_type) {
  if (file_type == (ctool_u32)CTOOL_ELF32_ET_REL) {
    return "REL";
  }
  if (file_type == (ctool_u32)CTOOL_ELF32_ET_EXEC) {
    return "EXEC";
  }
  return "UNKNOWN";
}

static const char *dis_section_type_name(ctool_u32 type) {
  switch (type) {
  case DIS_ELF32_SHT_NULL:
    return "NULL";
  case CTOOL_ELF32_SHT_PROGBITS:
    return "PROGBITS";
  case DIS_ELF32_SHT_SYMTAB:
    return "SYMTAB";
  case DIS_ELF32_SHT_STRTAB:
    return "STRTAB";
  case CTOOL_ELF32_SHT_NOBITS:
    return "NOBITS";
  case DIS_ELF32_SHT_REL:
    return "REL";
  default:
    return "UNKNOWN";
  }
}

static const char *dis_program_type_name(ctool_u32 type) {
  if (type == CTOOL_ELF32_PT_LOAD) {
    return "LOAD";
  }
  if (type == CTOOL_ELF32_PT_TLS) {
    return "TLS";
  }
  if (type == DIS_ELF32_PT_GNU_STACK) {
    return "GNU_STACK";
  }
  return "UNKNOWN";
}

static const char *dis_binding_name(ctool_u32 binding) {
  switch (binding) {
  case CTOOL_ELF32_BIND_LOCAL:
    return "LOCAL";
  case CTOOL_ELF32_BIND_GLOBAL:
    return "GLOBAL";
  case CTOOL_ELF32_BIND_WEAK:
    return "WEAK";
  default:
    return "UNKNOWN";
  }
}

static const char *dis_symbol_type_name(ctool_u32 type) {
  switch (type) {
  case CTOOL_ELF32_SYMBOL_NOTYPE:
    return "NOTYPE";
  case CTOOL_ELF32_SYMBOL_OBJECT:
    return "OBJECT";
  case CTOOL_ELF32_SYMBOL_FUNCTION:
    return "FUNC";
  case CTOOL_ELF32_SYMBOL_SECTION:
    return "SECTION";
  case CTOOL_ELF32_SYMBOL_FILE:
    return "FILE";
  case CTOOL_ELF32_SYMBOL_COMMON:
    return "COMMON";
  case CTOOL_ELF32_SYMBOL_TLS:
    return "TLS";
  default:
    return "UNKNOWN";
  }
}

static const char *dis_visibility_name(ctool_u32 visibility) {
  switch (visibility) {
  case CTOOL_ELF32_VIS_DEFAULT:
    return "DEFAULT";
  case CTOOL_ELF32_VIS_INTERNAL:
    return "INTERNAL";
  case CTOOL_ELF32_VIS_HIDDEN:
    return "HIDDEN";
  case CTOOL_ELF32_VIS_PROTECTED:
    return "PROTECTED";
  default:
    return "UNKNOWN";
  }
}

static const char *dis_relocation_name(ctool_u32 type) {
  if (type == CTOOL_ELF32_R_386_32) {
    return "R_386_32";
  }
  if (type == CTOOL_ELF32_R_386_PC32) {
    return "R_386_PC32";
  }
  return "R_386_UNKNOWN";
}

static const ctool_elf32_section_t *dis_section(
    const ctool_elf32_object_t *object, ctool_u32 file_index) {
  if (file_index >= object->section_count) {
    return (const ctool_elf32_section_t *)0;
  }
  return &object->sections[file_index];
}

static const ctool_elf32_symbol_t *dis_symbol(
    const ctool_elf32_object_t *object, ctool_u32 file_index) {
  if (file_index >= object->symbol_count) {
    return (const ctool_elf32_symbol_t *)0;
  }
  return &object->symbols[file_index];
}

static ctool_status_t dis_render_header(const ctool_dis_report_t *report,
                                        ctool_text_sink_t output) {
  const ctool_elf32_object_t *object = &report->elf32;
  ctool_status_t status = dis_literal(output, "ELF32 ");
  if (status == CTOOL_OK) {
    status = dis_literal(output, dis_file_type_name((ctool_u32)object->file_type));
  }
  if (status == CTOOL_OK) {
    status = dis_literal(output, " i386 entry=");
  }
  if (status == CTOOL_OK) {
    status = dis_hex_u32(output, object->entry_point);
  }
  if (status == CTOOL_OK) {
    status = dis_literal(output, " flags=");
  }
  if (status == CTOOL_OK) {
    status = dis_hex_u32(output, object->flags);
  }
  if (status == CTOOL_OK) {
    status = dis_literal(output, " phnum=");
  }
  if (status == CTOOL_OK) {
    status = dis_decimal(output, object->program_header_count);
  }
  if (status == CTOOL_OK) {
    status = dis_literal(output, " shnum=");
  }
  if (status == CTOOL_OK) {
    status = dis_decimal(output, object->section_count);
  }
  if (status == CTOOL_OK) {
    status = dis_literal(output, "\n");
  }
  if (status == CTOOL_OK && object->program_header_count != 0u) {
    ctool_u32 index;
    status = dis_literal(output, "[program headers]\n");
    for (index = 0u; status == CTOOL_OK &&
                     index < object->program_header_count;
         index++) {
      const ctool_elf32_program_header_t *header =
          &object->program_headers[index];
      status = dis_literal(output, "[");
      if (status == CTOOL_OK) {
        status = dis_decimal(output, header->file_index);
      }
      if (status == CTOOL_OK) {
        status = dis_literal(output, "] ");
      }
      if (status == CTOOL_OK) {
        status = dis_literal(output, dis_program_type_name(header->type));
      }
      if (status == CTOOL_OK) {
        status = dis_literal(output, " off=");
      }
      if (status == CTOOL_OK) {
        status = dis_hex_u32(output, header->file_offset);
      }
      if (status == CTOOL_OK) {
        status = dis_literal(output, " vaddr=");
      }
      if (status == CTOOL_OK) {
        status = dis_hex_u32(output, header->virtual_address);
      }
      if (status == CTOOL_OK) {
        status = dis_literal(output, " filesz=");
      }
      if (status == CTOOL_OK) {
        status = dis_hex_u32(output, header->file_size);
      }
      if (status == CTOOL_OK) {
        status = dis_literal(output, " memsz=");
      }
      if (status == CTOOL_OK) {
        status = dis_hex_u32(output, header->memory_size);
      }
      if (status == CTOOL_OK) {
        status = dis_literal(output, " flags=");
      }
      if (status == CTOOL_OK && (header->flags & 4u) != 0u) {
        status = dis_literal(output, "R");
      }
      if (status == CTOOL_OK && (header->flags & 2u) != 0u) {
        status = dis_literal(output, "W");
      }
      if (status == CTOOL_OK && (header->flags & 1u) != 0u) {
        status = dis_literal(output, "X");
      }
      if (status == CTOOL_OK) {
        status = dis_literal(output, " align=");
      }
      if (status == CTOOL_OK) {
        status = dis_decimal(output, header->alignment);
      }
      if (status == CTOOL_OK) {
        status = dis_literal(output, "\n");
      }
    }
  }
  return status;
}

static ctool_status_t dis_render_sections(const ctool_dis_report_t *report,
                                          ctool_text_sink_t output) {
  const ctool_elf32_object_t *object = &report->elf32;
  ctool_u32 index;
  ctool_status_t status = dis_literal(output, "[sections]\n");
  for (index = 0u; status == CTOOL_OK && index < object->section_count;
       index++) {
    const ctool_elf32_section_t *section = &object->sections[index];
    status = dis_literal(output, "[");
    if (status == CTOOL_OK) {
      status = dis_decimal(output, section->file_index);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, "] ");
    }
    if (status == CTOOL_OK) {
      status = section->name.size == 0u ? dis_literal(output, "<null>")
                                        : dis_string(output, section->name);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, " type=");
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, dis_section_type_name(section->type));
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, " flags=");
    }
    if (status == CTOOL_OK && (section->flags & CTOOL_ELF32_SHF_WRITE) != 0u) {
      status = dis_literal(output, "W");
    }
    if (status == CTOOL_OK && (section->flags & CTOOL_ELF32_SHF_ALLOC) != 0u) {
      status = dis_literal(output, "A");
    }
    if (status == CTOOL_OK &&
        (section->flags & CTOOL_ELF32_SHF_EXECINSTR) != 0u) {
      status = dis_literal(output, "X");
    }
    if (status == CTOOL_OK &&
        (section->flags & CTOOL_ELF32_SHF_MERGE) != 0u) {
      status = dis_literal(output, "M");
    }
    if (status == CTOOL_OK &&
        (section->flags & CTOOL_ELF32_SHF_STRINGS) != 0u) {
      status = dis_literal(output, "S");
    }
    if (status == CTOOL_OK && section->flags == 0u) {
      status = dis_literal(output, "-");
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, " addr=");
    }
    if (status == CTOOL_OK) {
      status = dis_hex_u32(output, section->address);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, " off=");
    }
    if (status == CTOOL_OK) {
      status = dis_hex_u32(output, section->file_offset);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, " size=");
    }
    if (status == CTOOL_OK) {
      status = dis_hex_u32(output, section->size);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, " align=");
    }
    if (status == CTOOL_OK) {
      status = dis_decimal(output, section->alignment);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, "\n");
    }
  }
  return status;
}

static ctool_status_t dis_render_symbol_section(
    const ctool_elf32_object_t *object, const ctool_elf32_symbol_t *symbol,
    ctool_text_sink_t output) {
  const ctool_elf32_section_t *section;
  switch (symbol->placement) {
  case CTOOL_ELF32_SYMBOL_UNDEFINED:
    return dis_literal(output, "UND");
  case CTOOL_ELF32_SYMBOL_ABSOLUTE:
    return dis_literal(output, "ABS");
  case CTOOL_ELF32_SYMBOL_COMMON_STORAGE:
    return dis_literal(output, "COMMON");
  case CTOOL_ELF32_SYMBOL_RESERVED:
    return dis_literal(output, "RESERVED");
  case CTOOL_ELF32_SYMBOL_DEFINED:
    section = dis_section(object, symbol->section_file_index);
    if (section == (const ctool_elf32_section_t *)0) {
      return dis_literal(output, "<bad-section>");
    }
    if (section->name.size == 0u) {
      return dis_literal(output, "<null>");
    }
    return dis_string(output, section->name);
  default:
    return dis_literal(output, "UNKNOWN");
  }
}

static ctool_status_t dis_render_symbols(const ctool_dis_report_t *report,
                                         ctool_text_sink_t output) {
  const ctool_elf32_object_t *object = &report->elf32;
  ctool_u32 index;
  ctool_status_t status = dis_literal(output, "[symbols]\n");
  for (index = 0u; status == CTOOL_OK && index < object->symbol_count;
       index++) {
    const ctool_elf32_symbol_t *symbol = &object->symbols[index];
    status = dis_literal(output, "[");
    if (status == CTOOL_OK) {
      status = dis_decimal(output, symbol->file_index);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, "] ");
    }
    if (status == CTOOL_OK) {
      status = dis_hex_u32(output, symbol->value);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, " size=");
    }
    if (status == CTOOL_OK) {
      status = dis_decimal(output, symbol->size);
    }
    if (status == CTOOL_OK) {
      status = dis_space(output);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, dis_binding_name(symbol->binding));
    }
    if (status == CTOOL_OK) {
      status = dis_space(output);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, dis_symbol_type_name(symbol->type));
    }
    if (status == CTOOL_OK) {
      status = dis_space(output);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, dis_visibility_name(symbol->visibility));
    }
    if (status == CTOOL_OK) {
      status = dis_space(output);
    }
    if (status == CTOOL_OK) {
      status = dis_render_symbol_section(object, symbol, output);
    }
    if (status == CTOOL_OK) {
      status = dis_space(output);
    }
    if (status == CTOOL_OK) {
      status = symbol->name.size == 0u ? dis_literal(output, "<anonymous>")
                                       : dis_string(output, symbol->name);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, "\n");
    }
  }
  return status;
}

static ctool_status_t dis_render_symbol_addend(
    ctool_text_sink_t output, const ctool_elf32_object_t *object,
    const ctool_elf32_symbol_t *symbol, ctool_bool addend_known,
    ctool_i32 addend) {
  ctool_status_t status;
  if (symbol == (const ctool_elf32_symbol_t *)0) {
    status = dis_literal(output, "<symbol>");
  } else if (symbol->name.size != 0u) {
    status = dis_string(output, symbol->name);
  } else if (symbol->type == CTOOL_ELF32_SYMBOL_SECTION &&
             symbol->placement == CTOOL_ELF32_SYMBOL_DEFINED) {
    const ctool_elf32_section_t *section =
        dis_section(object, symbol->section_file_index);
    status = section == (const ctool_elf32_section_t *)0 ||
                     section->name.size == 0u
                 ? dis_literal(output, "<section>")
                 : dis_string(output, section->name);
  } else {
    status = dis_literal(output, "<symbol>");
  }
  if (status != CTOOL_OK || addend_known == CTOOL_FALSE || addend == 0) {
    return status;
  }
  status = dis_literal(output, addend < 0 ? "-" : "+");
  if (status != CTOOL_OK) {
    return status;
  }
  if (addend < 0) {
    ctool_u32 magnitude = (ctool_u32)(-(addend + 1)) + 1u;
    return dis_decimal(output, magnitude);
  }
  return dis_decimal(output, (ctool_u32)addend);
}

static ctool_status_t dis_render_relocations(const ctool_dis_report_t *report,
                                             ctool_text_sink_t output) {
  const ctool_elf32_object_t *object = &report->elf32;
  ctool_u32 index;
  ctool_status_t status = dis_literal(output, "[relocations]\n");
  for (index = 0u;
       status == CTOOL_OK && index < report->relocation_order_count;
       index++) {
    const ctool_elf32_relocation_t *relocation =
        &object->relocations[report->relocation_order[index]];
    const ctool_elf32_section_t *relocation_section =
        dis_section(object, relocation->relocation_section_file_index);
    const ctool_elf32_section_t *target =
        dis_section(object, relocation->target_section_file_index);
    const ctool_elf32_symbol_t *symbol_value =
        dis_symbol(object, relocation->symbol_file_index);
    status = dis_literal(output, "[");
    if (status == CTOOL_OK) {
      status = relocation_section == (const ctool_elf32_section_t *)0 ||
                       relocation_section->name.size == 0u
                   ? dis_literal(output, "<rel>")
                   : dis_string(output, relocation_section->name);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, ":");
    }
    if (status == CTOOL_OK) {
      status = dis_decimal(output, relocation->entry_index);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, "] ");
    }
    if (status == CTOOL_OK) {
      status = target == (const ctool_elf32_section_t *)0 ||
                       target->name.size == 0u
                   ? dis_literal(output, "<target>")
                   : dis_string(output, target->name);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, "+");
    }
    if (status == CTOOL_OK) {
      status = dis_hex_u32(output, relocation->offset);
    }
    if (status == CTOOL_OK) {
      status = dis_space(output);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, dis_relocation_name(relocation->type));
    }
    if (status == CTOOL_OK) {
      status = dis_space(output);
    }
    if (status == CTOOL_OK) {
      status = dis_render_symbol_addend(output, object, symbol_value,
                                        relocation->addend_known,
                                        relocation->addend);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, "\n");
    }
  }
  return status;
}

static ctool_i32 dis_signed_bits(ctool_u32 value) {
  if (value <= 0x7fffffffu) {
    return (ctool_i32)value;
  }
  if (value == 0x80000000u) {
    return (-2147483647 - 1);
  }
  return -(ctool_i32)((~value) + 1u);
}

static ctool_status_t dis_render_register(ctool_text_sink_t output,
                                          ctool_x86_reg_t reg_value) {
  ctool_string_t name = ctool_x86_register_name(reg_value);
  return name.size == 0u ? dis_literal(output, "<reg>")
                         : dis_string(output, name);
}

static const char *dis_memory_width(ctool_u16 width_bits) {
  switch (width_bits) {
  case 8u:
    return "byte ";
  case 16u:
    return "word ";
  case 32u:
    return "dword ";
  case 64u:
    return "qword ";
  case 80u:
    return "tword ";
  case 128u:
    return "oword ";
  default:
    return "";
  }
}

static ctool_status_t dis_render_memory(
    ctool_text_sink_t output, const ctool_x86_operand_t *operand,
    const ctool_elf32_object_t *object,
    const ctool_elf32_relocation_t *relocation) {
  const ctool_x86_memory_t *memory = &operand->as.memory;
  ctool_bool have_term = CTOOL_FALSE;
  ctool_status_t status = dis_literal(output,
                                      dis_memory_width(operand->width_bits));
  if (status == CTOOL_OK) {
    status = dis_literal(output, "[");
  }
  if (status == CTOOL_OK && memory->segment.class_id != CTOOL_X86_REG_NONE) {
    status = dis_render_register(output, memory->segment);
    if (status == CTOOL_OK) {
      status = dis_literal(output, ":");
    }
  }
  if (status == CTOOL_OK && memory->base.class_id != CTOOL_X86_REG_NONE) {
    status = dis_render_register(output, memory->base);
    have_term = CTOOL_TRUE;
  }
  if (status == CTOOL_OK && memory->index.class_id != CTOOL_X86_REG_NONE) {
    if (have_term == CTOOL_TRUE) {
      status = dis_literal(output, "+");
    }
    if (status == CTOOL_OK) {
      status = dis_render_register(output, memory->index);
    }
    if (status == CTOOL_OK && memory->scale > 1u) {
      status = dis_literal(output, "*");
      if (status == CTOOL_OK) {
        status = dis_decimal(output, (ctool_u32)memory->scale);
      }
    }
    have_term = CTOOL_TRUE;
  }
  if (status == CTOOL_OK &&
      relocation != (const ctool_elf32_relocation_t *)0) {
    if (have_term == CTOOL_TRUE) {
      status = dis_literal(output, "+");
    }
    if (status == CTOOL_OK) {
      status = dis_render_symbol_addend(
          output, object, dis_symbol(object, relocation->symbol_file_index),
          relocation->addend_known, relocation->addend);
    }
    have_term = CTOOL_TRUE;
  } else if (status == CTOOL_OK && memory->displacement.kind ==
                                CTOOL_X86_VALUE_REFERENCE) {
    if (have_term == CTOOL_TRUE) {
      status = dis_literal(output, "+");
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, "<reference>");
    }
    have_term = CTOOL_TRUE;
  } else if (status == CTOOL_OK &&
             (memory->displacement_bits != 0u || have_term == CTOOL_FALSE)) {
    if (have_term == CTOOL_FALSE) {
      status = dis_hex_value(output, memory->displacement.bits);
    } else {
      ctool_i32 displacement = dis_signed_bits(memory->displacement.bits);
      status = dis_literal(output, displacement < 0 ? "-" : "+");
      if (status == CTOOL_OK) {
        ctool_u32 magnitude = displacement < 0
                                  ? (ctool_u32)(-(displacement + 1)) + 1u
                                  : (ctool_u32)displacement;
        status = dis_hex_value(output, magnitude);
      }
    }
  }
  if (status == CTOOL_OK) {
    status = dis_literal(output, "]");
  }
  return status;
}

static const ctool_elf32_relocation_t *dis_operand_relocation(
    const ctool_dis_report_t *report, const ctool_elf32_object_t *object,
    ctool_u32 section_file_index, ctool_u32 logical_address,
    ctool_u32 instruction_offset, const ctool_x86_decoded_t *decoded,
    ctool_u32 operand_index) {
  const ctool_elf32_section_t *section =
      dis_section(object, section_file_index);
  ctool_u32 field_index;
  if (object->file_type == CTOOL_ELF32_ET_REL &&
      section == (const ctool_elf32_section_t *)0) {
    return (const ctool_elf32_relocation_t *)0;
  }
  for (field_index = 0u; field_index < decoded->encoding.field_count;
       field_index++) {
    const ctool_x86_field_t *field = &decoded->encoding.fields[field_index];
    ctool_u32 first = 0u;
    ctool_u32 last = report->relocation_site_order_count;
    ctool_u32 site;
    if ((ctool_u32)field->operand_index != operand_index) {
      continue;
    }
    if (object->file_type == CTOOL_ELF32_ET_EXEC) {
      if (logical_address >
          DIS_U32_MAX - (ctool_u32)field->byte_offset) {
        continue;
      }
      site = logical_address + (ctool_u32)field->byte_offset;
    } else {
      if (instruction_offset >
          DIS_U32_MAX - (ctool_u32)field->byte_offset) {
        continue;
      }
      site = instruction_offset + (ctool_u32)field->byte_offset;
    }
    while (first < last) {
      ctool_u32 middle = first + (last - first) / 2u;
      const ctool_elf32_relocation_t *relocation =
          &object->relocations[report->relocation_site_order[middle]];
      if ((object->file_type == CTOOL_ELF32_ET_EXEC &&
           relocation->offset < site) ||
          (object->file_type == CTOOL_ELF32_ET_REL &&
           (relocation->target_section_file_index < section_file_index ||
            (relocation->target_section_file_index == section_file_index &&
             relocation->offset < site)))) {
        first = middle + 1u;
      } else {
        last = middle;
      }
    }
    while (first < report->relocation_site_order_count) {
      const ctool_elf32_relocation_t *relocation =
          &object->relocations[report->relocation_site_order[first]];
      ctool_bool matches = CTOOL_FALSE;
      if (relocation->offset != site ||
          (object->file_type == CTOOL_ELF32_ET_REL &&
           relocation->target_section_file_index != section_file_index)) {
        break;
      }
      if (field->byte_width == 4u &&
          relocation->type == CTOOL_ELF32_R_386_PC32 &&
          field->kind == CTOOL_X86_FIELD_RELATIVE) {
        matches = CTOOL_TRUE;
      } else if (field->byte_width == 4u &&
                 relocation->type == CTOOL_ELF32_R_386_32 &&
                 field->kind != CTOOL_X86_FIELD_RELATIVE) {
        matches = CTOOL_TRUE;
      }
      if (matches == CTOOL_TRUE) {
        return relocation;
      }
      first++;
    }
  }
  return (const ctool_elf32_relocation_t *)0;
}

static ctool_status_t dis_render_operand(
    ctool_text_sink_t output, const ctool_x86_operand_t *operand,
    ctool_u32 logical_address, ctool_u32 instruction_size,
    const ctool_dis_report_t *report, const ctool_elf32_object_t *object,
    ctool_u32 section_file_index, ctool_u32 instruction_offset,
    const ctool_x86_decoded_t *decoded, ctool_u32 operand_index) {
  const ctool_elf32_relocation_t *relocation =
      object == (const ctool_elf32_object_t *)0
          ? (const ctool_elf32_relocation_t *)0
          : dis_operand_relocation(report, object, section_file_index,
                                   logical_address, instruction_offset,
                                   decoded, operand_index);
  if (relocation != (const ctool_elf32_relocation_t *)0 &&
      operand->kind != CTOOL_X86_OPERAND_MEMORY) {
    return dis_render_symbol_addend(
        output, object, dis_symbol(object, relocation->symbol_file_index),
        relocation->addend_known, relocation->addend);
  }
  switch (operand->kind) {
  case CTOOL_X86_OPERAND_REGISTER:
    return dis_render_register(output, operand->as.reg);
  case CTOOL_X86_OPERAND_IMMEDIATE:
    if (operand->as.value.kind == CTOOL_X86_VALUE_REFERENCE) {
      return dis_literal(output, "<reference>");
    }
    return dis_hex_value(output, operand->as.value.bits);
  case CTOOL_X86_OPERAND_RELATIVE:
    if (operand->as.value.kind == CTOOL_X86_VALUE_REFERENCE) {
      return dis_literal(output, "<reference>");
    }
    {
      ctool_u32 target =
          logical_address + instruction_size +
          (ctool_u32)dis_signed_bits(operand->as.value.bits);
      if (decoded->instruction.operand_bits == 16u) {
        target &= 0xffffu;
      }
      return dis_hex_u32(output, target);
    }
  case CTOOL_X86_OPERAND_MEMORY:
    return dis_render_memory(output, operand, object, relocation);
  case CTOOL_X86_OPERAND_FAR_POINTER: {
    ctool_status_t status =
        dis_hex_value(output, operand->as.far_pointer.segment.bits);
    if (status == CTOOL_OK) {
      status = dis_literal(output, ":");
    }
    if (status == CTOOL_OK) {
      status = dis_hex_value(output, operand->as.far_pointer.offset.bits);
    }
    return status;
  }
  case CTOOL_X86_OPERAND_NONE:
  default:
    return dis_literal(output, "<operand>");
  }
}

static ctool_status_t dis_render_data_bytes(ctool_text_sink_t output,
                                            const ctool_x86_decoded_t *decoded) {
  ctool_u32 index;
  ctool_status_t status = dis_literal(output, "db ");
  for (index = 0u; status == CTOOL_OK && index < decoded->encoding.size;
       index++) {
    if (index != 0u) {
      status = dis_literal(output, ", ");
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, "0x");
    }
    if (status == CTOOL_OK) {
      status = dis_hex_fixed(output,
                             (ctool_u32)decoded->encoding.bytes[index], 2u);
    }
  }
  return status;
}

static ctool_status_t dis_render_instruction(
    ctool_text_sink_t output, ctool_u32 logical_address,
    ctool_u32 instruction_offset, const ctool_x86_decoded_t *decoded,
    const ctool_dis_report_t *report, const ctool_elf32_object_t *object,
    ctool_u32 section_file_index) {
  ctool_u32 index;
  ctool_status_t status = dis_hex_fixed(output, logical_address, 8u);
  if (status == CTOOL_OK) {
    status = dis_literal(output, ":  ");
  }
  for (index = 0u; status == CTOOL_OK && index < decoded->encoding.size;
       index++) {
    status = dis_hex_fixed(output, (ctool_u32)decoded->encoding.bytes[index],
                           2u);
    if (status == CTOOL_OK) {
      status = dis_space(output);
    }
  }
  if (status == CTOOL_OK) {
    status = dis_space(output);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  if (decoded->kind != CTOOL_X86_DECODE_KNOWN) {
    status = dis_render_data_bytes(output, decoded);
  } else {
    ctool_string_t mnemonic =
        ctool_x86_mnemonic_name(decoded->instruction.mnemonic);
    if ((decoded->instruction.prefixes & CTOOL_X86_PREFIX_LOCK) != 0u) {
      status = dis_literal(output, "lock ");
    }
    if (status == CTOOL_OK &&
        (decoded->instruction.prefixes & CTOOL_X86_PREFIX_REP) != 0u) {
      status = dis_literal(output, "rep ");
    }
    if (status == CTOOL_OK &&
        (decoded->instruction.prefixes & CTOOL_X86_PREFIX_REPNE) != 0u) {
      status = dis_literal(output, "repne ");
    }
    if (status == CTOOL_OK) {
      status = mnemonic.size == 0u ? dis_literal(output, "<unknown>")
                                   : dis_string(output, mnemonic);
    }
    if (status == CTOOL_OK && decoded->instruction.operand_count != 0u) {
      status = dis_space(output);
    }
    for (index = 0u; status == CTOOL_OK &&
                     index < decoded->instruction.operand_count;
         index++) {
      if (index != 0u) {
        status = dis_literal(output, ", ");
      }
      if (status == CTOOL_OK) {
        status = dis_render_operand(
            output, &decoded->instruction.operands[index], logical_address,
            (ctool_u32)decoded->encoding.size, report, object,
            section_file_index, instruction_offset, decoded, index);
      }
    }
  }
  if (status == CTOOL_OK) {
    status = dis_literal(output, "\n");
  }
  return status;
}

static ctool_status_t dis_render_raw_labels(
    const ctool_dis_report_t *report, ctool_u32 address, ctool_u32 *cursor,
    ctool_text_sink_t output) {
  ctool_status_t status = CTOOL_OK;
  while (*cursor < report->raw_label_order_count &&
         report->labels[report->raw_label_order[*cursor]].address < address) {
    (*cursor)++;
  }
  while (status == CTOOL_OK && *cursor < report->raw_label_order_count &&
         report->labels[report->raw_label_order[*cursor]].address == address) {
    const ctool_dis_label_t *label =
        &report->labels[report->raw_label_order[*cursor]];
    status = dis_hex_fixed(output, address, 8u);
    if (status == CTOOL_OK) {
      status = dis_literal(output, " <");
    }
    if (status == CTOOL_OK) {
      status = dis_string(output, label->name);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, ">:\n");
    }
    (*cursor)++;
  }
  return status;
}

static ctool_u32 dis_symbol_address(const ctool_elf32_object_t *object,
                                    const ctool_elf32_symbol_t *symbol) {
  if (symbol->placement != CTOOL_ELF32_SYMBOL_DEFINED) {
    return symbol->value;
  }
  if (object->file_type == CTOOL_ELF32_ET_REL) {
    const ctool_elf32_section_t *section =
        dis_section(object, symbol->section_file_index);
    return section == (const ctool_elf32_section_t *)0
               ? symbol->value
               : section->address + symbol->value;
  }
  return symbol->value;
}

static ctool_status_t dis_render_elf_labels(
    const ctool_dis_report_t *report, ctool_u32 section_file_index,
    ctool_u32 address, ctool_bool section_specific, ctool_u32 *cursor,
    ctool_text_sink_t output) {
  const ctool_elf32_object_t *object = &report->elf32;
  ctool_status_t status = CTOOL_OK;
  while (*cursor < report->function_order_count) {
    const ctool_elf32_symbol_t *symbol =
        &object->symbols[report->function_order[*cursor]];
    if (section_specific == CTOOL_TRUE &&
        symbol->section_file_index < section_file_index) {
      (*cursor)++;
    } else if (section_specific == CTOOL_TRUE &&
               symbol->section_file_index > section_file_index) {
      return status;
    } else if (dis_symbol_address(object, symbol) < address) {
      (*cursor)++;
    } else {
      break;
    }
  }
  while (status == CTOOL_OK && *cursor < report->function_order_count) {
    const ctool_elf32_symbol_t *symbol =
        &object->symbols[report->function_order[*cursor]];
    if ((section_specific == CTOOL_TRUE &&
         symbol->section_file_index != section_file_index) ||
        dis_symbol_address(object, symbol) != address) {
      break;
    }
    status = dis_hex_fixed(output, address, 8u);
    if (status == CTOOL_OK) {
      status = dis_literal(output, " <");
    }
    if (status == CTOOL_OK) {
      status = dis_string(output, symbol->name);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, ">:\n");
    }
    (*cursor)++;
  }
  return status;
}

static ctool_status_t dis_render_region(
    ctool_job_t *job, const ctool_dis_report_t *report, ctool_bytes_t bytes,
    ctool_u32 base_address, ctool_x86_mode_t mode,
    const ctool_elf32_object_t *object, ctool_u32 section_file_index,
    ctool_bool section_specific, ctool_u32 *label_cursor,
    ctool_text_sink_t output) {
  ctool_u32 offset = 0u;
  ctool_status_t status = CTOOL_OK;
  if (bytes.size == 0u) {
    return CTOOL_OK;
  }
  while (status == CTOOL_OK && offset < bytes.size) {
    ctool_x86_decoded_t decoded;
    ctool_u32 address = base_address + offset;
    if (object == (const ctool_elf32_object_t *)0) {
      status = dis_render_raw_labels(report, address, label_cursor, output);
    } else {
      status = dis_render_elf_labels(report, section_file_index, address,
                                     section_specific, label_cursor, output);
    }
    if (status == CTOOL_OK) {
      status = ctool_x86_decode(job, mode, bytes, offset, &decoded);
    }
    if (status != CTOOL_OK) {
      break;
    }
    if (decoded.kind == CTOOL_X86_DECODE_TRUNCATED) {
      decoded.consumed = decoded.encoding.size;
    }
    if (decoded.consumed == 0u || decoded.consumed > bytes.size - offset) {
      return CTOOL_ERR_INTERNAL;
    }
    status = dis_render_instruction(output, address, offset, &decoded, report,
                                    object, section_file_index);
    offset += (ctool_u32)decoded.consumed;
  }
  return status;
}

static ctool_u32 dis_function_lower_bound(const ctool_dis_report_t *report,
                                           ctool_u32 address) {
  ctool_u32 first = 0u;
  ctool_u32 last = report->function_order_count;
  while (first < last) {
    ctool_u32 middle = first + (last - first) / 2u;
    const ctool_elf32_symbol_t *symbol =
        &report->elf32.symbols[report->function_order[middle]];
    if (dis_symbol_address(&report->elf32, symbol) < address) {
      first = middle + 1u;
    } else {
      last = middle;
    }
  }
  return first;
}

static ctool_status_t dis_render_disassembly(ctool_job_t *job,
                                             const ctool_dis_report_t *report,
                                             ctool_text_sink_t output) {
  ctool_status_t status;
  ctool_u32 index;
  ctool_u32 label_cursor = 0u;
  if (report->input == CTOOL_DIS_INPUT_RAW) {
    status = dis_literal(output, "[disassembly raw]\n");
    if (status == CTOOL_OK && report->mode != CTOOL_DIS_RAW_MODE_MAP) {
      status = dis_render_region(job, report, report->source->contents,
                                 report->base_address, report->mode,
                                 (const ctool_elf32_object_t *)0, 0u,
                                 CTOOL_FALSE, &label_cursor, output);
    }
    for (index = 0u;
         status == CTOOL_OK && report->mode == CTOOL_DIS_RAW_MODE_MAP &&
         index < report->raw_range_count;
         index++) {
      ctool_u32 first = report->raw_ranges[index].offset;
      ctool_u32 last = index + 1u < report->raw_range_count
                           ? report->raw_ranges[index + 1u].offset
                           : report->source->contents.size;
      ctool_bytes_t bytes = ctool_bytes(report->source->contents.data + first,
                                        last - first);
      status = dis_render_region(
          job, report, bytes, report->base_address + first,
          report->raw_ranges[index].mode,
          (const ctool_elf32_object_t *)0, 0u, CTOOL_FALSE, &label_cursor,
          output);
    }
    return status;
  }
  if (report->elf32.file_type == CTOOL_ELF32_ET_EXEC) {
    status = CTOOL_OK;
    for (index = 0u; status == CTOOL_OK &&
                     index < report->elf32.program_header_count;
         index++) {
      const ctool_elf32_program_header_t *program =
          &report->elf32.program_headers[index];
      if (program->type != CTOOL_ELF32_PT_LOAD ||
          (program->flags & CTOOL_ELF32_PF_X) == 0u ||
          program->contents.size == 0u) {
        continue;
      }
      status = dis_literal(output, "[disassembly LOAD#");
      if (status == CTOOL_OK) {
        status = dis_decimal(output, program->file_index);
      }
      if (status == CTOOL_OK) {
        status = dis_literal(output, "]\n");
      }
      if (status == CTOOL_OK) {
        label_cursor =
            dis_function_lower_bound(report, program->virtual_address);
        status = dis_render_region(
            job, report, program->contents, program->virtual_address,
            report->mode, &report->elf32, 0u, CTOOL_FALSE, &label_cursor,
            output);
      }
    }
    return status;
  }
  status = CTOOL_OK;
  for (index = 0u; status == CTOOL_OK &&
                   index < report->elf32.section_count;
       index++) {
    const ctool_elf32_section_t *section = &report->elf32.sections[index];
    if (section->type != CTOOL_ELF32_SHT_PROGBITS ||
        (section->flags & CTOOL_ELF32_SHF_EXECINSTR) == 0u ||
        section->contents.size == 0u) {
      continue;
    }
    status = dis_literal(output, "[disassembly ");
    if (status == CTOOL_OK) {
      status = section->name.size == 0u ? dis_literal(output, "<section>")
                                        : dis_string(output, section->name);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, "]\n");
    }
    if (status == CTOOL_OK) {
      status = dis_render_region(job, report, section->contents,
                                  section->address, report->mode,
                                  &report->elf32,
                                  section->file_index, CTOOL_TRUE,
                                  &label_cursor, output);
    }
  }
  return status;
}

static char dis_nm_type(const ctool_elf32_object_t *object,
                         const ctool_elf32_symbol_t *symbol) {
  char type;
  const ctool_elf32_section_t *section;
  if (symbol->placement == CTOOL_ELF32_SYMBOL_UNDEFINED) {
    if (symbol->binding != CTOOL_ELF32_BIND_WEAK) {
      return 'U';
    }
    return symbol->type == CTOOL_ELF32_SYMBOL_OBJECT ||
                   symbol->type == CTOOL_ELF32_SYMBOL_COMMON ||
                   symbol->type == CTOOL_ELF32_SYMBOL_TLS
               ? 'v'
               : 'w';
  }
  if (symbol->placement == CTOOL_ELF32_SYMBOL_COMMON_STORAGE) {
    return 'C';
  }
  if (symbol->placement == CTOOL_ELF32_SYMBOL_ABSOLUTE) {
    type = 'A';
  } else {
    section = dis_section(object, symbol->section_file_index);
    if (section == (const ctool_elf32_section_t *)0) {
      type = '?';
    } else if ((section->flags & CTOOL_ELF32_SHF_EXECINSTR) != 0u ||
               symbol->type == CTOOL_ELF32_SYMBOL_FUNCTION) {
      type = 'T';
    } else if (section->type == CTOOL_ELF32_SHT_NOBITS) {
      type = 'B';
    } else if ((section->flags & CTOOL_ELF32_SHF_WRITE) != 0u) {
      type = 'D';
    } else {
      type = 'R';
    }
  }
  if (symbol->binding == CTOOL_ELF32_BIND_WEAK) {
    return symbol->type == CTOOL_ELF32_SYMBOL_OBJECT ||
                   symbol->type == CTOOL_ELF32_SYMBOL_COMMON ||
                   symbol->type == CTOOL_ELF32_SYMBOL_TLS
               ? 'V'
               : 'W';
  }
  if (symbol->binding == CTOOL_ELF32_BIND_LOCAL && type >= 'A' && type <= 'Z') {
    return (char)(type + ('a' - 'A'));
  }
  return type;
}

static ctool_bool dis_nm_before(const ctool_elf32_object_t *object,
                                ctool_u32 left, ctool_u32 right) {
  const ctool_elf32_symbol_t *left_symbol = &object->symbols[left];
  const ctool_elf32_symbol_t *right_symbol = &object->symbols[right];
  ctool_u32 left_address = dis_symbol_address(object, left_symbol);
  ctool_u32 right_address = dis_symbol_address(object, right_symbol);
  if (left_address != right_address) {
    return left_address < right_address ? CTOOL_TRUE : CTOOL_FALSE;
  }
  return left_symbol->file_index < right_symbol->file_index ? CTOOL_TRUE
                                                             : CTOOL_FALSE;
}

static void dis_index_swap(ctool_u32 *left, ctool_u32 *right) {
  ctool_u32 temporary = *left;
  *left = *right;
  *right = temporary;
}

static void dis_nm_sift(const ctool_elf32_object_t *object,
                        ctool_u32 *indices, ctool_u32 root,
                        ctool_u32 count) {
  while (count > 1u && root <= (count - 2u) / 2u) {
    ctool_u32 child = root * 2u + 1u;
    if (child + 1u < count &&
        dis_nm_before(object, indices[child], indices[child + 1u]) ==
            CTOOL_TRUE) {
      child++;
    }
    if (dis_nm_before(object, indices[root], indices[child]) == CTOOL_FALSE) {
      return;
    }
    dis_index_swap(&indices[root], &indices[child]);
    root = child;
  }
}

static void dis_sort_nm(const ctool_elf32_object_t *object,
                        ctool_u32 *indices, ctool_u32 count) {
  ctool_u32 index = count / 2u;
  ctool_u32 end = count;
  while (index != 0u) {
    index--;
    dis_nm_sift(object, indices, index, count);
  }
  while (end > 1u) {
    dis_index_swap(&indices[0], &indices[end - 1u]);
    end--;
    dis_nm_sift(object, indices, 0u, end);
  }
}

static ctool_bool dis_relocation_before(const ctool_elf32_object_t *object,
                                        ctool_u32 left, ctool_u32 right) {
  const ctool_elf32_relocation_t *left_relocation =
      &object->relocations[left];
  const ctool_elf32_relocation_t *right_relocation =
      &object->relocations[right];
  if (left_relocation->relocation_section_file_index !=
      right_relocation->relocation_section_file_index) {
    return left_relocation->relocation_section_file_index <
                   right_relocation->relocation_section_file_index
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  if (left_relocation->entry_index != right_relocation->entry_index) {
    return left_relocation->entry_index < right_relocation->entry_index
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  return left < right ? CTOOL_TRUE : CTOOL_FALSE;
}

static void dis_relocation_sift(const ctool_elf32_object_t *object,
                                ctool_u32 *indices, ctool_u32 root,
                                ctool_u32 count) {
  while (count > 1u && root <= (count - 2u) / 2u) {
    ctool_u32 child = root * 2u + 1u;
    if (child + 1u < count &&
        dis_relocation_before(object, indices[child], indices[child + 1u]) ==
            CTOOL_TRUE) {
      child++;
    }
    if (dis_relocation_before(object, indices[root], indices[child]) ==
        CTOOL_FALSE) {
      return;
    }
    dis_index_swap(&indices[root], &indices[child]);
    root = child;
  }
}

static void dis_sort_relocations(const ctool_elf32_object_t *object,
                                 ctool_u32 *indices, ctool_u32 count) {
  ctool_u32 index = count / 2u;
  ctool_u32 end = count;
  while (index != 0u) {
    index--;
    dis_relocation_sift(object, indices, index, count);
  }
  while (end > 1u) {
    dis_index_swap(&indices[0], &indices[end - 1u]);
    end--;
    dis_relocation_sift(object, indices, 0u, end);
  }
}

static ctool_bool dis_relocation_site_before(
    const ctool_elf32_object_t *object, ctool_u32 left, ctool_u32 right) {
  const ctool_elf32_relocation_t *left_relocation =
      &object->relocations[left];
  const ctool_elf32_relocation_t *right_relocation =
      &object->relocations[right];
  if (object->file_type == CTOOL_ELF32_ET_EXEC &&
      left_relocation->offset != right_relocation->offset) {
    return left_relocation->offset < right_relocation->offset ? CTOOL_TRUE
                                                               : CTOOL_FALSE;
  }
  if (left_relocation->target_section_file_index !=
      right_relocation->target_section_file_index) {
    return left_relocation->target_section_file_index <
                   right_relocation->target_section_file_index
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  if (object->file_type == CTOOL_ELF32_ET_REL &&
      left_relocation->offset != right_relocation->offset) {
    return left_relocation->offset < right_relocation->offset ? CTOOL_TRUE
                                                               : CTOOL_FALSE;
  }
  return left < right ? CTOOL_TRUE : CTOOL_FALSE;
}

static void dis_relocation_site_sift(const ctool_elf32_object_t *object,
                                     ctool_u32 *indices, ctool_u32 root,
                                     ctool_u32 count) {
  while (count > 1u && root <= (count - 2u) / 2u) {
    ctool_u32 child = root * 2u + 1u;
    if (child + 1u < count &&
        dis_relocation_site_before(object, indices[child],
                                   indices[child + 1u]) == CTOOL_TRUE) {
      child++;
    }
    if (dis_relocation_site_before(object, indices[root], indices[child]) ==
        CTOOL_FALSE) {
      return;
    }
    dis_index_swap(&indices[root], &indices[child]);
    root = child;
  }
}

static void dis_sort_relocation_sites(const ctool_elf32_object_t *object,
                                      ctool_u32 *indices, ctool_u32 count) {
  ctool_u32 index = count / 2u;
  ctool_u32 end = count;
  while (index != 0u) {
    index--;
    dis_relocation_site_sift(object, indices, index, count);
  }
  while (end > 1u) {
    dis_index_swap(&indices[0], &indices[end - 1u]);
    end--;
    dis_relocation_site_sift(object, indices, 0u, end);
  }
}

static ctool_bool dis_raw_label_before(const ctool_dis_report_t *report,
                                       ctool_u32 left, ctool_u32 right) {
  if (report->labels[left].address != report->labels[right].address) {
    return report->labels[left].address < report->labels[right].address
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  return left < right ? CTOOL_TRUE : CTOOL_FALSE;
}

static void dis_raw_label_sift(const ctool_dis_report_t *report,
                               ctool_u32 *indices, ctool_u32 root,
                               ctool_u32 count) {
  while (count > 1u && root <= (count - 2u) / 2u) {
    ctool_u32 child = root * 2u + 1u;
    if (child + 1u < count &&
        dis_raw_label_before(report, indices[child], indices[child + 1u]) ==
            CTOOL_TRUE) {
      child++;
    }
    if (dis_raw_label_before(report, indices[root], indices[child]) ==
        CTOOL_FALSE) {
      return;
    }
    dis_index_swap(&indices[root], &indices[child]);
    root = child;
  }
}

static void dis_sort_raw_labels(const ctool_dis_report_t *report,
                                ctool_u32 *indices, ctool_u32 count) {
  ctool_u32 index = count / 2u;
  ctool_u32 end = count;
  while (index != 0u) {
    index--;
    dis_raw_label_sift(report, indices, index, count);
  }
  while (end > 1u) {
    dis_index_swap(&indices[0], &indices[end - 1u]);
    end--;
    dis_raw_label_sift(report, indices, 0u, end);
  }
}

static ctool_status_t dis_prepare_raw_label_order(ctool_job_t *job,
                                                   ctool_dis_report_t *report) {
  ctool_u32 *order = (ctool_u32 *)0;
  ctool_u32 index;
  ctool_u32 count = 0u;
  ctool_status_t status;
  if (report->label_count == 0u) {
    return CTOOL_OK;
  }
  status = ctool_arena_alloc_zero(
      ctool_job_arena(job), report->label_count, (ctool_u32)sizeof(ctool_u32),
      (ctool_u32)sizeof(ctool_u32), (void **)&order);
  if (status != CTOOL_OK) {
    return status;
  }
  for (index = 0u; index < report->label_count; index++) {
    if (report->labels[index].name.size != 0u) {
      order[count] = index;
      count++;
    }
  }
  dis_sort_raw_labels(report, order, count);
  report->raw_label_order = order;
  report->raw_label_order_count = count;
  return CTOOL_OK;
}

static ctool_bool dis_function_before(const ctool_elf32_object_t *object,
                                      ctool_u32 left, ctool_u32 right) {
  const ctool_elf32_symbol_t *left_symbol = &object->symbols[left];
  const ctool_elf32_symbol_t *right_symbol = &object->symbols[right];
  ctool_u32 left_address;
  ctool_u32 right_address;
  if (object->file_type == CTOOL_ELF32_ET_REL &&
      left_symbol->section_file_index != right_symbol->section_file_index) {
    return left_symbol->section_file_index < right_symbol->section_file_index
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  left_address = dis_symbol_address(object, left_symbol);
  right_address = dis_symbol_address(object, right_symbol);
  if (left_address != right_address) {
    return left_address < right_address ? CTOOL_TRUE : CTOOL_FALSE;
  }
  return left_symbol->file_index < right_symbol->file_index ? CTOOL_TRUE
                                                             : CTOOL_FALSE;
}

static void dis_function_sift(const ctool_elf32_object_t *object,
                              ctool_u32 *indices, ctool_u32 root,
                              ctool_u32 count) {
  while (count > 1u && root <= (count - 2u) / 2u) {
    ctool_u32 child = root * 2u + 1u;
    if (child + 1u < count &&
        dis_function_before(object, indices[child], indices[child + 1u]) ==
            CTOOL_TRUE) {
      child++;
    }
    if (dis_function_before(object, indices[root], indices[child]) ==
        CTOOL_FALSE) {
      return;
    }
    dis_index_swap(&indices[root], &indices[child]);
    root = child;
  }
}

static void dis_sort_functions(const ctool_elf32_object_t *object,
                               ctool_u32 *indices, ctool_u32 count) {
  ctool_u32 index = count / 2u;
  ctool_u32 end = count;
  while (index != 0u) {
    index--;
    dis_function_sift(object, indices, index, count);
  }
  while (end > 1u) {
    dis_index_swap(&indices[0], &indices[end - 1u]);
    end--;
    dis_function_sift(object, indices, 0u, end);
  }
}

static ctool_status_t dis_prepare_report_orders(ctool_job_t *job,
                                                 ctool_dis_report_t *report) {
  ctool_arena_t *arena = ctool_job_arena(job);
  ctool_u32 *symbol_order = (ctool_u32 *)0;
  ctool_u32 *function_order = (ctool_u32 *)0;
  ctool_u32 *relocation_order = (ctool_u32 *)0;
  ctool_u32 *relocation_site_order = (ctool_u32 *)0;
  ctool_u32 index;
  ctool_u32 count = 0u;
  ctool_u32 function_count = 0u;
  ctool_status_t status = CTOOL_OK;
  if ((report->views & CTOOL_DIS_VIEW_DISASSEMBLY) != 0u) {
    for (index = 0u; index < report->elf32.symbol_count; index++) {
      const ctool_elf32_symbol_t *symbol = &report->elf32.symbols[index];
      if (symbol->placement == CTOOL_ELF32_SYMBOL_DEFINED &&
          symbol->type == CTOOL_ELF32_SYMBOL_FUNCTION &&
          symbol->name.size != 0u) {
        function_count++;
      }
    }
  }
  if (report->views == CTOOL_DIS_VIEW_SYMBOLS &&
      report->elf32.symbol_count != 0u) {
    status = ctool_arena_alloc_zero(
        arena, report->elf32.symbol_count, (ctool_u32)sizeof(ctool_u32),
        (ctool_u32)sizeof(ctool_u32), (void **)&symbol_order);
  }
  if (status == CTOOL_OK && function_count != 0u) {
    status = ctool_arena_alloc_zero(
        arena, function_count, (ctool_u32)sizeof(ctool_u32),
        (ctool_u32)sizeof(ctool_u32), (void **)&function_order);
  }
  if (status == CTOOL_OK &&
      (report->views & CTOOL_DIS_VIEW_RELOCATIONS) != 0u &&
      report->elf32.relocation_count != 0u) {
    status = ctool_arena_alloc_zero(
        arena, report->elf32.relocation_count, (ctool_u32)sizeof(ctool_u32),
        (ctool_u32)sizeof(ctool_u32), (void **)&relocation_order);
  }
  if (status == CTOOL_OK &&
      (report->views & CTOOL_DIS_VIEW_DISASSEMBLY) != 0u &&
      report->elf32.relocation_count != 0u) {
    status = ctool_arena_alloc_zero(
        arena, report->elf32.relocation_count, (ctool_u32)sizeof(ctool_u32),
        (ctool_u32)sizeof(ctool_u32), (void **)&relocation_site_order);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  if (report->views == CTOOL_DIS_VIEW_SYMBOLS) {
    for (index = 0u; index < report->elf32.symbol_count; index++) {
      if (report->elf32.symbols[index].name.size != 0u) {
        symbol_order[count] = index;
        count++;
      }
    }
    dis_sort_nm(&report->elf32, symbol_order, count);
    report->symbol_order = symbol_order;
    report->symbol_order_count = count;
  }
  count = 0u;
  if (function_count != 0u) {
    for (index = 0u; index < report->elf32.symbol_count; index++) {
      const ctool_elf32_symbol_t *symbol = &report->elf32.symbols[index];
      if (symbol->placement == CTOOL_ELF32_SYMBOL_DEFINED &&
          symbol->type == CTOOL_ELF32_SYMBOL_FUNCTION &&
          symbol->name.size != 0u) {
        function_order[count] = index;
        count++;
      }
    }
    dis_sort_functions(&report->elf32, function_order, count);
    report->function_order = function_order;
    report->function_order_count = count;
  }
  if ((report->views & CTOOL_DIS_VIEW_RELOCATIONS) != 0u) {
    for (index = 0u; index < report->elf32.relocation_count; index++) {
      relocation_order[index] = index;
    }
    dis_sort_relocations(&report->elf32, relocation_order,
                         report->elf32.relocation_count);
    report->relocation_order = relocation_order;
    report->relocation_order_count = report->elf32.relocation_count;
  }
  if ((report->views & CTOOL_DIS_VIEW_DISASSEMBLY) != 0u) {
    for (index = 0u; index < report->elf32.relocation_count; index++) {
      relocation_site_order[index] = index;
    }
    dis_sort_relocation_sites(&report->elf32, relocation_site_order,
                              report->elf32.relocation_count);
    report->relocation_site_order = relocation_site_order;
    report->relocation_site_order_count = report->elf32.relocation_count;
  }
  return CTOOL_OK;
}

static ctool_status_t dis_render_nm(const ctool_dis_report_t *report,
                                    ctool_text_sink_t output) {
  const ctool_elf32_object_t *object = &report->elf32;
  ctool_u32 index;
  ctool_status_t status = CTOOL_OK;
  for (index = 0u;
       status == CTOOL_OK && index < report->symbol_order_count; index++) {
    const ctool_elf32_symbol_t *symbol =
        &object->symbols[report->symbol_order[index]];
    char type = dis_nm_type(object, symbol);
    status = symbol->placement == CTOOL_ELF32_SYMBOL_UNDEFINED
                 ? dis_literal(output, "        ")
                 : dis_hex_fixed(output, dis_symbol_address(object, symbol),
                                 8u);
    if (status == CTOOL_OK) {
      status = dis_space(output);
    }
    if (status == CTOOL_OK) {
      status = dis_write(output, &type, 1u);
    }
    if (status == CTOOL_OK) {
      status = dis_space(output);
    }
    if (status == CTOOL_OK) {
      status = dis_string(output, symbol->name);
    }
    if (status == CTOOL_OK) {
      status = dis_literal(output, "\n");
    }
  }
  return status;
}

static ctool_bool dis_report_shape_valid(const ctool_dis_report_t *report) {
  ctool_u32 index;
  if (report->source == (const ctool_source_t *)0 ||
      (report->source->contents.data == (const ctool_u8 *)0 &&
       report->source->contents.size != 0u) ||
      (report->source->path.text.data == (const char *)0 &&
       report->source->path.text.size != 0u) ||
      report->views == 0u || (report->views & ~CTOOL_DIS_VIEW_ALL) != 0u) {
    return CTOOL_FALSE;
  }
  if (report->input == CTOOL_DIS_INPUT_RAW) {
    if (report->views != CTOOL_DIS_VIEW_DISASSEMBLY ||
        (report->label_count != 0u &&
         report->labels == (const ctool_dis_label_t *)0) ||
        (report->source->contents.size != 0u &&
         report->base_address >
             DIS_U32_MAX - (report->source->contents.size - 1u))) {
      return CTOOL_FALSE;
    }
    if (report->mode == CTOOL_DIS_RAW_MODE_MAP) {
      if (dis_raw_map_issue(report->source->contents.size,
                            report->raw_ranges,
                            report->raw_range_count) != DIS_RAW_MAP_VALID) {
        return CTOOL_FALSE;
      }
    } else if (dis_x86_mode_valid(report->mode) == CTOOL_FALSE ||
               report->raw_ranges != (const ctool_dis_raw_range_t *)0 ||
               report->raw_range_count != 0u) {
      return CTOOL_FALSE;
    }
    for (index = 0u; index < report->label_count; index++) {
      if (report->labels[index].name.data == (const char *)0 &&
          report->labels[index].name.size != 0u) {
        return CTOOL_FALSE;
      }
    }
    if (report->raw_label_order_count > report->label_count ||
        (report->raw_label_order_count != 0u &&
         report->raw_label_order == (const ctool_u32 *)0)) {
      return CTOOL_FALSE;
    }
    for (index = 0u; index < report->raw_label_order_count; index++) {
      if (report->raw_label_order[index] >= report->label_count) {
        return CTOOL_FALSE;
      }
    }
    return CTOOL_TRUE;
  }
  if (report->input != CTOOL_DIS_INPUT_ELF32 ||
      report->mode != CTOOL_X86_MODE_32 ||
      (report->elf32.file_type != CTOOL_ELF32_ET_REL &&
       report->elf32.file_type != CTOOL_ELF32_ET_EXEC) ||
      report->elf32.image.data != report->source->contents.data ||
      report->elf32.image.size != report->source->contents.size ||
      (report->elf32.program_header_count != 0u &&
       report->elf32.program_headers ==
           (const ctool_elf32_program_header_t *)0) ||
      (report->elf32.section_count != 0u &&
       report->elf32.sections == (const ctool_elf32_section_t *)0) ||
      (report->elf32.symbol_count != 0u &&
       report->elf32.symbols == (const ctool_elf32_symbol_t *)0) ||
      (report->elf32.relocation_count != 0u &&
       report->elf32.relocations == (const ctool_elf32_relocation_t *)0) ||
      (report->views != CTOOL_DIS_VIEW_SYMBOLS &&
       (report->symbol_order != (const ctool_u32 *)0 ||
        report->symbol_order_count != 0u)) ||
      report->symbol_order_count > report->elf32.symbol_count ||
      (report->symbol_order_count != 0u &&
       report->symbol_order == (const ctool_u32 *)0) ||
      ((report->views & CTOOL_DIS_VIEW_DISASSEMBLY) == 0u &&
       (report->function_order != (const ctool_u32 *)0 ||
        report->function_order_count != 0u)) ||
      report->function_order_count > report->elf32.symbol_count ||
      (report->function_order_count != 0u &&
       report->function_order == (const ctool_u32 *)0) ||
      ((report->views & CTOOL_DIS_VIEW_RELOCATIONS) == 0u &&
       (report->relocation_order != (const ctool_u32 *)0 ||
        report->relocation_order_count != 0u)) ||
      ((report->views & CTOOL_DIS_VIEW_RELOCATIONS) != 0u &&
       report->relocation_order_count != report->elf32.relocation_count) ||
      (report->relocation_order_count != 0u &&
       report->relocation_order == (const ctool_u32 *)0) ||
      ((report->views & CTOOL_DIS_VIEW_DISASSEMBLY) == 0u &&
       (report->relocation_site_order != (const ctool_u32 *)0 ||
        report->relocation_site_order_count != 0u)) ||
      ((report->views & CTOOL_DIS_VIEW_DISASSEMBLY) != 0u &&
       report->relocation_site_order_count !=
           report->elf32.relocation_count) ||
      (report->relocation_site_order_count != 0u &&
       report->relocation_site_order == (const ctool_u32 *)0)) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < report->elf32.program_header_count; index++) {
    if (report->elf32.program_headers[index].contents.data ==
            (const ctool_u8 *)0 &&
        report->elf32.program_headers[index].contents.size != 0u) {
      return CTOOL_FALSE;
    }
  }
  for (index = 0u; index < report->elf32.section_count; index++) {
    const ctool_elf32_section_t *section = &report->elf32.sections[index];
    if ((section->name.data == (const char *)0 && section->name.size != 0u) ||
        (section->contents.data == (const ctool_u8 *)0 &&
         section->contents.size != 0u) ||
        section->relocation_first > report->elf32.relocation_count ||
        section->relocation_count >
            report->elf32.relocation_count - section->relocation_first) {
      return CTOOL_FALSE;
    }
  }
  for (index = 0u; index < report->elf32.symbol_count; index++) {
    const ctool_elf32_symbol_t *symbol = &report->elf32.symbols[index];
    if ((symbol->name.data == (const char *)0 && symbol->name.size != 0u) ||
        (symbol->placement == CTOOL_ELF32_SYMBOL_DEFINED &&
         symbol->section_file_index >= report->elf32.section_count)) {
      return CTOOL_FALSE;
    }
  }
  for (index = 0u; index < report->symbol_order_count; index++) {
    if (report->symbol_order[index] >= report->elf32.symbol_count) {
      return CTOOL_FALSE;
    }
  }
  for (index = 0u; index < report->function_order_count; index++) {
    if (report->function_order[index] >= report->elf32.symbol_count) {
      return CTOOL_FALSE;
    }
  }
  for (index = 0u; index < report->elf32.relocation_count; index++) {
    const ctool_elf32_relocation_t *relocation =
        &report->elf32.relocations[index];
    if (relocation->relocation_section_file_index >=
            report->elf32.section_count ||
        relocation->target_section_file_index >=
            report->elf32.section_count ||
        relocation->symbol_file_index >= report->elf32.symbol_count) {
      return CTOOL_FALSE;
    }
  }
  for (index = 0u; index < report->relocation_order_count; index++) {
    if (report->relocation_order[index] >= report->elf32.relocation_count) {
      return CTOOL_FALSE;
    }
  }
  for (index = 0u; index < report->relocation_site_order_count; index++) {
    if (report->relocation_site_order[index] >=
        report->elf32.relocation_count) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

ctool_status_t ctool_dis_render(ctool_job_t *job,
                                 const ctool_dis_report_t *report,
                                 ctool_dis_text_t text,
                                 ctool_text_sink_t output) {
  ctool_status_t status = CTOOL_OK;
  if (job == (ctool_job_t *)0 || report == (const ctool_dis_report_t *)0 ||
      output.write == (ctool_status_t (*)(void *, ctool_bytes_t))0 ||
      (text != CTOOL_DIS_TEXT_CUPID && text != CTOOL_DIS_TEXT_NM) ||
      dis_report_shape_valid(report) == CTOOL_FALSE) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  if (text == CTOOL_DIS_TEXT_NM) {
    if (report->input != CTOOL_DIS_INPUT_ELF32 ||
        report->views != CTOOL_DIS_VIEW_SYMBOLS) {
      return dis_bad_request(job, report->source,
                             "nm text requires an ELF symbols-only report");
    }
    status = dis_render_nm(report, output);
  } else {
    if ((report->views & CTOOL_DIS_VIEW_HEADER) != 0u) {
      status = dis_render_header(report, output);
    }
    if (status == CTOOL_OK &&
        (report->views & CTOOL_DIS_VIEW_SECTIONS) != 0u) {
      status = dis_render_sections(report, output);
    }
    if (status == CTOOL_OK &&
        (report->views & CTOOL_DIS_VIEW_SYMBOLS) != 0u) {
      status = dis_render_symbols(report, output);
    }
    if (status == CTOOL_OK &&
        (report->views & CTOOL_DIS_VIEW_RELOCATIONS) != 0u) {
      status = dis_render_relocations(report, output);
    }
    if (status == CTOOL_OK &&
        (report->views & CTOOL_DIS_VIEW_DISASSEMBLY) != 0u) {
      status = dis_render_disassembly(job, report, output);
    }
  }
  if (status != CTOOL_OK) {
    ctool_status_t emitted =
        dis_emit(job, report->source->path.text, CTOOL_DIS_DIAG_OUTPUT, 0u,
                 "CupidDis could not complete report output", status);
    if (emitted != status) {
      return emitted;
    }
  }
  return status;
}
