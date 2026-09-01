#include "elf32.h"

#define ELF32_HEADER_SIZE 52u
#define ELF32_PROGRAM_HEADER_SIZE 32u
#define ELF32_SECTION_HEADER_SIZE 40u
#define ELF32_SYMBOL_SIZE 16u
#define ELF32_RELOCATION_SIZE 8u
#define ELF32_U32_MAX 4294967295u
#define ELF32_SHN_LORESERVE 0xff00u
#define ELF32_SHN_ABS 0xfff1u
#define ELF32_SHN_COMMON 0xfff2u
#define ELF32_SHT_NULL 0u
#define ELF32_SHT_SYMTAB 2u
#define ELF32_SHT_STRTAB 3u
#define ELF32_SHT_REL 9u
#define ELF32_SHT_DYNSYM 11u
#define ELF32_WRITER_SECTION_FLAGS                                           \
  (CTOOL_ELF32_SHF_WRITE | CTOOL_ELF32_SHF_ALLOC |                         \
   CTOOL_ELF32_SHF_EXECINSTR | CTOOL_ELF32_SHF_MERGE |                     \
   CTOOL_ELF32_SHF_STRINGS | CTOOL_ELF32_SHF_TLS | CTOOL_ELF32_SHF_EXCLUDE)

typedef struct {
  ctool_string_t name;
  ctool_u32 name_offset;
  ctool_u32 type;
  ctool_u32 flags;
  ctool_u32 offset;
  ctool_u32 size;
  ctool_u32 link;
  ctool_u32 info;
  ctool_u32 alignment;
  ctool_u32 entry_size;
} elf32_layout_section_t;

typedef struct {
  ctool_u32 target_section;
  ctool_u32 relocation_count;
  ctool_string_t name;
  ctool_u32 file_section;
} elf32_relocation_group_t;

typedef struct {
  ctool_string_t text;
  ctool_u32 offset;
} elf32_interned_string_t;

typedef struct {
  elf32_interned_string_t *entries;
  ctool_u32 count;
  ctool_u32 capacity;
  ctool_u32 size;
} elf32_string_table_t;

typedef struct {
  ctool_u32 offset;
  ctool_string_t *view;
} elf32_string_reference_t;

typedef struct {
  ctool_u32 offset;
  ctool_u32 record_offset;
} elf32_relocation_site_t;

static ctool_bool elf32_add_overflows(ctool_u32 left, ctool_u32 right) {
  return left > 0xffffffffu - right ? CTOOL_TRUE : CTOOL_FALSE;
}

static ctool_bool elf32_mul_overflows(ctool_u32 left, ctool_u32 right) {
  return right != 0u && left > 0xffffffffu / right ? CTOOL_TRUE
                                                   : CTOOL_FALSE;
}

static ctool_bool elf32_is_power_of_two(ctool_u32 value) {
  return value != 0u && (value & (value - 1u)) == 0u ? CTOOL_TRUE
                                                     : CTOOL_FALSE;
}

static ctool_bool elf32_string_is_valid(ctool_string_t string,
                                         ctool_bool allow_empty) {
  ctool_u32 index;
  if (string.data == (const char *)0) {
    return string.size == 0u && allow_empty == CTOOL_TRUE ? CTOOL_TRUE
                                                         : CTOOL_FALSE;
  }
  if (string.size == 0u && allow_empty == CTOOL_FALSE) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < string.size; index++) {
    if (string.data[index] == '\0') {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_bool elf32_strings_equal(ctool_string_t left,
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

static ctool_bool elf32_string_equals_literal(ctool_string_t string,
                                               const char *literal) {
  return elf32_strings_equal(string, ctool_string(literal));
}

static ctool_bool elf32_string_has_prefix(ctool_string_t string,
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

static ctool_status_t elf32_emit_diagnostic(ctool_job_t *job,
                                             ctool_u32 code,
                                             ctool_string_t path,
                                             ctool_u32 column,
                                             const char *message,
                                             ctool_status_t status) {
  ctool_diagnostic_t diagnostic;
  ctool_status_t diagnostic_status;
  diagnostic.severity = CTOOL_DIAG_ERROR;
  diagnostic.code = code;
  diagnostic.path = path;
  diagnostic.line = 0u;
  diagnostic.column = column;
  diagnostic.message = ctool_string(message);
  diagnostic_status = ctool_job_emit(job, &diagnostic);
  return diagnostic_status == CTOOL_OK ? status : diagnostic_status;
}

static ctool_status_t elf32_alloc_array(ctool_arena_t *arena,
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

static ctool_status_t elf32_align_buffer(ctool_buffer_t *buffer,
                                         ctool_u32 alignment) {
  ctool_u32 size = ctool_buffer_view(buffer).size;
  ctool_u32 padding = (0u - size) & (alignment - 1u);
  return ctool_buffer_fill(buffer, 0u, padding);
}

static ctool_status_t elf32_put_string(ctool_buffer_t *buffer,
                                       ctool_string_t string) {
  ctool_status_t status =
      ctool_buffer_append(buffer, ctool_bytes(string.data, string.size));
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_u8(buffer, 0u);
  }
  return status;
}

static ctool_status_t elf32_string_table_intern(
    elf32_string_table_t *table, ctool_string_t text, ctool_u32 *offset_out) {
  ctool_u32 index;
  if (text.size == 0u) {
    *offset_out = 0u;
    return CTOOL_OK;
  }
  for (index = 0u; index < table->count; index++) {
    if (elf32_strings_equal(table->entries[index].text, text) == CTOOL_TRUE) {
      *offset_out = table->entries[index].offset;
      return CTOOL_OK;
    }
  }
  if (table->count >= table->capacity ||
      elf32_add_overflows(text.size, 1u) == CTOOL_TRUE ||
      elf32_add_overflows(table->size, text.size + 1u) == CTOOL_TRUE) {
    return table->count >= table->capacity ? CTOOL_ERR_INTERNAL
                                           : CTOOL_ERR_OVERFLOW;
  }
  table->entries[table->count].text = text;
  table->entries[table->count].offset = table->size;
  *offset_out = table->size;
  table->count++;
  table->size += text.size + 1u;
  return CTOOL_OK;
}

static ctool_status_t elf32_put_string_table(
    ctool_buffer_t *buffer, const elf32_string_table_t *table) {
  ctool_u32 index;
  ctool_status_t status = ctool_buffer_put_u8(buffer, 0u);
  for (index = 0u; status == CTOOL_OK && index < table->count; index++) {
    status = elf32_put_string(buffer, table->entries[index].text);
  }
  return status;
}

static ctool_status_t elf32_put_section_header(
    ctool_buffer_t *buffer, const elf32_layout_section_t *section) {
  ctool_status_t status = ctool_buffer_put_le32(buffer, section->name_offset);
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(buffer, section->type);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(buffer, section->flags);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(buffer, 0u);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(buffer, section->offset);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(buffer, section->size);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(buffer, section->link);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(buffer, section->info);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(buffer, section->alignment);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(buffer, section->entry_size);
  }
  return status;
}

static ctool_status_t elf32_put_symbol(ctool_buffer_t *buffer,
                                       const ctool_elf32_symbol_spec_t *symbol,
                                       ctool_u32 name_offset) {
  ctool_u32 value = 0u;
  ctool_u16 section = 0u;
  ctool_status_t status;
  if (symbol->placement == CTOOL_ELF32_SYMBOL_DEFINED) {
    value = symbol->value;
    section = (ctool_u16)(symbol->section + 1u);
  } else if (symbol->placement == CTOOL_ELF32_SYMBOL_ABSOLUTE) {
    value = symbol->value;
    section = ELF32_SHN_ABS;
  } else if (symbol->placement == CTOOL_ELF32_SYMBOL_COMMON_STORAGE) {
    value = symbol->alignment;
    section = ELF32_SHN_COMMON;
  }
  status = ctool_buffer_put_le32(buffer, name_offset);
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(buffer, value);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(buffer, symbol->size);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_u8(
        buffer, (ctool_u8)(((ctool_u32)symbol->binding << 4u) |
                           (ctool_u32)symbol->type));
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_u8(buffer, (ctool_u8)symbol->visibility);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le16(buffer, section);
  }
  return status;
}

static ctool_status_t elf32_patch_header(ctool_buffer_t *output,
                                         ctool_u32 section_headers,
                                         ctool_u32 section_count,
                                         ctool_u32 shstrtab_index) {
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
    status = ctool_buffer_patch_le16(output, 16u, 1u);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le16(output, 18u, 3u);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le32(output, 20u, 1u);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le32(output, 32u, section_headers);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le16(output, 40u, ELF32_HEADER_SIZE);
  }
  if (status == CTOOL_OK) {
    status =
        ctool_buffer_patch_le16(output, 46u, ELF32_SECTION_HEADER_SIZE);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le16(output, 48u, (ctool_u16)section_count);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_patch_le16(output, 50u, (ctool_u16)shstrtab_index);
  }
  return status;
}

static ctool_bool elf32_symbol_type_is_valid(
    ctool_elf32_symbol_type_t type) {
  return type == CTOOL_ELF32_SYMBOL_NOTYPE ||
                 type == CTOOL_ELF32_SYMBOL_OBJECT ||
                 type == CTOOL_ELF32_SYMBOL_FUNCTION ||
                 type == CTOOL_ELF32_SYMBOL_SECTION ||
                 type == CTOOL_ELF32_SYMBOL_FILE ||
                 type == CTOOL_ELF32_SYMBOL_COMMON
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool elf32_section_merge_is_valid(
    const ctool_elf32_section_spec_t *section) {
  ctool_u32 index;
  if ((section->flags & ~ELF32_WRITER_SECTION_FLAGS) != 0u) {
    return CTOOL_FALSE;
  }
  if ((section->flags & CTOOL_ELF32_SHF_MERGE) != 0u &&
      (section->type != CTOOL_ELF32_SHT_PROGBITS ||
       section->entry_size == 0u)) {
    return CTOOL_FALSE;
  }
  if ((section->flags & CTOOL_ELF32_SHF_STRINGS) != 0u &&
      (section->flags & CTOOL_ELF32_SHF_MERGE) == 0u) {
    return CTOOL_FALSE;
  }
  if ((section->flags & CTOOL_ELF32_SHF_STRINGS) == 0u ||
      section->size == 0u) {
    return CTOOL_TRUE;
  }
  for (index = 0u; index < section->entry_size; index++) {
    if (section->contents.data[section->size - 1u - index] != 0u) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_bool elf32_symbol_is_valid(
    const ctool_elf32_object_spec_t *object,
    const ctool_elf32_symbol_spec_t *symbol) {
  if (elf32_string_is_valid(symbol->name, CTOOL_TRUE) == CTOOL_FALSE ||
      symbol->binding > CTOOL_ELF32_BIND_WEAK ||
      elf32_symbol_type_is_valid(symbol->type) == CTOOL_FALSE ||
      symbol->visibility > CTOOL_ELF32_VIS_PROTECTED) {
    return CTOOL_FALSE;
  }
  if (symbol->placement == CTOOL_ELF32_SYMBOL_DEFINED) {
    const ctool_elf32_section_spec_t *section;
    if (symbol->section >= object->section_count || symbol->alignment != 0u) {
      return CTOOL_FALSE;
    }
    section = &object->sections[symbol->section];
    if (symbol->value > section->size ||
        symbol->size > section->size - symbol->value) {
      return CTOOL_FALSE;
    }
  } else if (symbol->placement == CTOOL_ELF32_SYMBOL_UNDEFINED) {
    if (symbol->section != CTOOL_ELF32_NO_SECTION || symbol->value != 0u ||
        symbol->size != 0u || symbol->alignment != 0u ||
        symbol->binding == CTOOL_ELF32_BIND_LOCAL) {
      return CTOOL_FALSE;
    }
  } else if (symbol->placement == CTOOL_ELF32_SYMBOL_ABSOLUTE) {
    if (symbol->section != CTOOL_ELF32_NO_SECTION || symbol->alignment != 0u) {
      return CTOOL_FALSE;
    }
  } else if (symbol->placement == CTOOL_ELF32_SYMBOL_COMMON_STORAGE) {
    if (symbol->section != CTOOL_ELF32_NO_SECTION || symbol->value != 0u ||
        symbol->size == 0u ||
        elf32_is_power_of_two(symbol->alignment) == CTOOL_FALSE ||
        symbol->binding == CTOOL_ELF32_BIND_LOCAL ||
        (symbol->type != CTOOL_ELF32_SYMBOL_NOTYPE &&
         symbol->type != CTOOL_ELF32_SYMBOL_OBJECT &&
         symbol->type != CTOOL_ELF32_SYMBOL_COMMON)) {
      return CTOOL_FALSE;
    }
  } else {
    return CTOOL_FALSE;
  }
  if (symbol->type == CTOOL_ELF32_SYMBOL_SECTION &&
      (symbol->binding != CTOOL_ELF32_BIND_LOCAL ||
       symbol->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
       symbol->name.size != 0u || symbol->value != 0u || symbol->size != 0u)) {
    return CTOOL_FALSE;
  }
  if (symbol->type == CTOOL_ELF32_SYMBOL_FILE &&
      (symbol->binding != CTOOL_ELF32_BIND_LOCAL ||
       symbol->placement != CTOOL_ELF32_SYMBOL_ABSOLUTE ||
       symbol->name.size == 0u || symbol->value != 0u || symbol->size != 0u)) {
    return CTOOL_FALSE;
  }
  if (symbol->type == CTOOL_ELF32_SYMBOL_COMMON &&
      symbol->placement != CTOOL_ELF32_SYMBOL_COMMON_STORAGE) {
    return CTOOL_FALSE;
  }
  return CTOOL_TRUE;
}

static ctool_status_t elf32_validate_spec(
    ctool_job_t *job, const ctool_elf32_object_spec_t *object) {
  ctool_u32 index;
  ctool_u32 other;
  if ((object->sections == (const ctool_elf32_section_spec_t *)0 &&
       object->section_count != 0u) ||
      (object->symbols == (const ctool_elf32_symbol_spec_t *)0 &&
       object->symbol_count != 0u) ||
      (object->relocations == (const ctool_elf32_relocation_spec_t *)0 &&
       object->relocation_count != 0u)) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  if (object->symbol_count > 0x00ffffffu) {
    return elf32_emit_diagnostic(
        job, CTOOL_ELF32_DIAG_UNSUPPORTED_FEATURE, ctool_string(""), 0u,
        "ELF32 symbol indices exceed the supported range",
        CTOOL_ERR_UNSUPPORTED);
  }
  for (index = 0u; index < object->section_count; index++) {
    const ctool_elf32_section_spec_t *section = &object->sections[index];
    if (elf32_string_is_valid(section->name, CTOOL_FALSE) == CTOOL_FALSE ||
        elf32_is_power_of_two(section->alignment) == CTOOL_FALSE ||
        (section->type != CTOOL_ELF32_SHT_PROGBITS &&
         section->type != CTOOL_ELF32_SHT_NOBITS) ||
        (section->entry_size != 0u &&
         section->size % section->entry_size != 0u) ||
        (section->type == CTOOL_ELF32_SHT_PROGBITS &&
         (section->contents.size != section->size ||
          (section->contents.data == (const ctool_u8 *)0 &&
           section->contents.size != 0u))) ||
        (section->type == CTOOL_ELF32_SHT_NOBITS &&
          section->contents.size != 0u) ||
        elf32_section_merge_is_valid(section) == CTOOL_FALSE ||
        elf32_string_equals_literal(section->name, ".symtab") == CTOOL_TRUE ||
        elf32_string_equals_literal(section->name, ".strtab") == CTOOL_TRUE ||
        elf32_string_equals_literal(section->name, ".shstrtab") == CTOOL_TRUE ||
        elf32_string_has_prefix(section->name, ".rel.") == CTOOL_TRUE) {
      return elf32_emit_diagnostic(
          job, CTOOL_ELF32_DIAG_INVALID_SPEC, ctool_string(""), index + 1u,
          "invalid ELF32 section description", CTOOL_ERR_INPUT);
    }
  }
  for (index = 0u; index < object->symbol_count; index++) {
    if (elf32_symbol_is_valid(object, &object->symbols[index]) == CTOOL_FALSE) {
      return elf32_emit_diagnostic(
          job, CTOOL_ELF32_DIAG_INVALID_SPEC, ctool_string(""), index + 1u,
          "invalid ELF32 symbol description", CTOOL_ERR_INPUT);
    }
  }
  for (index = 0u; index < object->relocation_count; index++) {
    const ctool_elf32_relocation_spec_t *relocation =
        &object->relocations[index];
    if (relocation->target_section >= object->section_count ||
        relocation->symbol >= object->symbol_count ||
        (relocation->type != CTOOL_ELF32_R_386_32 &&
         relocation->type != CTOOL_ELF32_R_386_PC32) ||
        object->sections[relocation->target_section].type !=
            CTOOL_ELF32_SHT_PROGBITS ||
        relocation->offset >
            object->sections[relocation->target_section].size ||
        object->sections[relocation->target_section].size -
                relocation->offset <
            4u) {
      return elf32_emit_diagnostic(
          job, CTOOL_ELF32_DIAG_INVALID_SPEC, ctool_string(""), index + 1u,
          "invalid ELF32 relocation description", CTOOL_ERR_INPUT);
    }
    for (other = 0u; other < index; other++) {
      if (object->relocations[other].target_section ==
              relocation->target_section &&
          ((object->relocations[other].offset <= relocation->offset &&
            relocation->offset - object->relocations[other].offset < 4u) ||
           (object->relocations[other].offset > relocation->offset &&
            object->relocations[other].offset - relocation->offset < 4u))) {
        return elf32_emit_diagnostic(
            job, CTOOL_ELF32_DIAG_INVALID_SPEC, ctool_string(""), index + 1u,
            "ELF32 relocation fields overlap", CTOOL_ERR_INPUT);
      }
    }
  }
  return CTOOL_OK;
}

static ctool_status_t elf32_make_relocation_name(
    ctool_arena_t *arena, ctool_string_t section_name,
    ctool_string_t *name_out) {
  static const char prefix[] = ".rel";
  ctool_u32 size;
  ctool_u32 index;
  char *name;
  ctool_status_t status;
  if (elf32_add_overflows(4u, section_name.size) == CTOOL_TRUE ||
      elf32_add_overflows(4u + section_name.size, 1u) == CTOOL_TRUE) {
    return CTOOL_ERR_OVERFLOW;
  }
  size = 4u + section_name.size;
  status = ctool_arena_alloc(arena, size + 1u, 1u, (void **)&name);
  if (status != CTOOL_OK) {
    return status;
  }
  for (index = 0u; index < 4u; index++) {
    name[index] = prefix[index];
  }
  for (index = 0u; index < section_name.size; index++) {
    name[4u + index] = section_name.data[index];
  }
  name[size] = '\0';
  name_out->data = name;
  name_out->size = size;
  return CTOOL_OK;
}

ctool_status_t ctool_elf32_write(ctool_job_t *job,
                                  const ctool_elf32_object_spec_t *object,
                                  ctool_buffer_t *output) {
  ctool_arena_t *arena;
  ctool_arena_mark_t arena_mark;
  ctool_u32 output_mark;
  ctool_u32 *relocation_counts;
  ctool_u32 *symbol_order;
  ctool_u32 *symbol_map;
  ctool_u32 *symbol_name_offsets;
  elf32_relocation_group_t *groups;
  elf32_layout_section_t *layout;
  elf32_interned_string_t *section_strings;
  elf32_interned_string_t *symbol_strings;
  elf32_string_table_t shstrtab;
  elf32_string_table_t strtab;
  ctool_u32 group_count = 0u;
  ctool_u32 total_sections;
  ctool_u32 symtab_index;
  ctool_u32 strtab_index;
  ctool_u32 shstrtab_index;
  ctool_u32 local_count = 0u;
  ctool_u32 index;
  ctool_u32 other;
  ctool_u32 position;
  ctool_u32 section_headers;
  ctool_u32 table_size;
  ctool_mut_bytes_t header;
  ctool_u32 failure_code = 0u;
  const char *failure_message = (const char *)0;
  ctool_bool output_started = CTOOL_FALSE;
  ctool_status_t status;
  if (job == (ctool_job_t *)0 ||
      object == (const ctool_elf32_object_spec_t *)0 ||
      output == (ctool_buffer_t *)0 || ctool_buffer_view(output).size != 0u) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  status = elf32_validate_spec(job, object);
  if (status != CTOOL_OK) {
    return status;
  }
  arena = ctool_job_arena(job);
  arena_mark = ctool_arena_mark(arena);
  output_mark = ctool_buffer_mark(output);
  status = elf32_alloc_array(arena, object->section_count,
                             (ctool_u32)sizeof(ctool_u32),
                             (void **)&relocation_counts);
  if (status == CTOOL_OK) {
    status = elf32_alloc_array(arena, object->symbol_count,
                               (ctool_u32)sizeof(ctool_u32),
                               (void **)&symbol_order);
  }
  if (status == CTOOL_OK) {
    status = elf32_alloc_array(arena, object->symbol_count,
                               (ctool_u32)sizeof(ctool_u32),
                               (void **)&symbol_map);
  }
  if (status == CTOOL_OK) {
    status = elf32_alloc_array(arena, object->symbol_count,
                               (ctool_u32)sizeof(ctool_u32),
                               (void **)&symbol_name_offsets);
  }
  if (status != CTOOL_OK) {
    goto fail;
  }
  for (index = 0u; index < object->relocation_count; index++) {
    relocation_counts[object->relocations[index].target_section]++;
  }
  for (index = 0u; index < object->section_count; index++) {
    if (relocation_counts[index] != 0u) {
      group_count++;
    }
  }
  status = elf32_alloc_array(arena, group_count,
                             (ctool_u32)sizeof(elf32_relocation_group_t),
                             (void **)&groups);
  if (status != CTOOL_OK) {
    goto fail;
  }
  if (elf32_add_overflows(object->section_count, group_count) == CTOOL_TRUE ||
      elf32_add_overflows(object->section_count + group_count, 4u) ==
          CTOOL_TRUE) {
    status = CTOOL_ERR_OVERFLOW;
    goto fail;
  }
  total_sections = object->section_count + group_count + 4u;
  if (total_sections >= ELF32_SHN_LORESERVE) {
    status = CTOOL_ERR_UNSUPPORTED;
    failure_code = CTOOL_ELF32_DIAG_UNSUPPORTED_FEATURE;
    failure_message = "ELF32 extended section indices are not supported";
    goto fail;
  }
  status = elf32_alloc_array(arena, total_sections,
                             (ctool_u32)sizeof(elf32_layout_section_t),
                             (void **)&layout);
  if (status == CTOOL_OK) {
    status = elf32_alloc_array(arena, total_sections - 1u,
                               (ctool_u32)sizeof(elf32_interned_string_t),
                               (void **)&section_strings);
  }
  if (status == CTOOL_OK) {
    status = elf32_alloc_array(arena, object->symbol_count,
                               (ctool_u32)sizeof(elf32_interned_string_t),
                               (void **)&symbol_strings);
  }
  if (status != CTOOL_OK) {
    goto fail;
  }
  shstrtab.entries = section_strings;
  shstrtab.count = 0u;
  shstrtab.capacity = total_sections - 1u;
  shstrtab.size = 1u;
  strtab.entries = symbol_strings;
  strtab.count = 0u;
  strtab.capacity = object->symbol_count;
  strtab.size = 1u;

  layout[0].type = ELF32_SHT_NULL;
  for (index = 0u; index < object->section_count; index++) {
    const ctool_elf32_section_spec_t *section = &object->sections[index];
    elf32_layout_section_t *entry = &layout[index + 1u];
    entry->name = section->name;
    entry->type = (ctool_u32)section->type;
    entry->flags = section->flags;
    entry->size = section->size;
    entry->alignment = section->alignment;
    entry->entry_size = section->entry_size;
  }
  position = 0u;
  for (index = 0u; index < object->section_count; index++) {
    if (relocation_counts[index] != 0u) {
      groups[position].target_section = index;
      groups[position].relocation_count = relocation_counts[index];
      groups[position].file_section = 1u + object->section_count + position;
      status = elf32_make_relocation_name(arena, object->sections[index].name,
                                           &groups[position].name);
      if (status != CTOOL_OK) {
        goto fail;
      }
      layout[groups[position].file_section].name = groups[position].name;
      layout[groups[position].file_section].type = ELF32_SHT_REL;
      layout[groups[position].file_section].info = index + 1u;
      layout[groups[position].file_section].alignment = 4u;
      layout[groups[position].file_section].entry_size =
          ELF32_RELOCATION_SIZE;
      if (elf32_mul_overflows(relocation_counts[index],
                              ELF32_RELOCATION_SIZE) == CTOOL_TRUE) {
        status = CTOOL_ERR_OVERFLOW;
        goto fail;
      }
      layout[groups[position].file_section].size =
          relocation_counts[index] * ELF32_RELOCATION_SIZE;
      position++;
    }
  }
  symtab_index = 1u + object->section_count + group_count;
  strtab_index = symtab_index + 1u;
  shstrtab_index = strtab_index + 1u;
  layout[symtab_index].name = ctool_string(".symtab");
  layout[symtab_index].type = ELF32_SHT_SYMTAB;
  layout[symtab_index].link = strtab_index;
  layout[symtab_index].alignment = 4u;
  layout[symtab_index].entry_size = ELF32_SYMBOL_SIZE;
  layout[strtab_index].name = ctool_string(".strtab");
  layout[strtab_index].type = ELF32_SHT_STRTAB;
  layout[strtab_index].alignment = 1u;
  layout[shstrtab_index].name = ctool_string(".shstrtab");
  layout[shstrtab_index].type = ELF32_SHT_STRTAB;
  layout[shstrtab_index].alignment = 1u;
  for (index = 0u; index < group_count; index++) {
    layout[groups[index].file_section].link = symtab_index;
  }

  for (index = 1u; index < total_sections; index++) {
    status = elf32_string_table_intern(&shstrtab, layout[index].name,
                                       &layout[index].name_offset);
    if (status != CTOOL_OK) {
      goto fail;
    }
  }
  layout[shstrtab_index].size = shstrtab.size;

  position = 0u;
  for (index = 0u; index < object->symbol_count; index++) {
    if (object->symbols[index].binding == CTOOL_ELF32_BIND_LOCAL) {
      symbol_order[position++] = index;
      local_count++;
    }
  }
  for (index = 0u; index < object->symbol_count; index++) {
    if (object->symbols[index].binding != CTOOL_ELF32_BIND_LOCAL) {
      symbol_order[position++] = index;
    }
  }
  for (position = 0u; position < object->symbol_count; position++) {
    index = symbol_order[position];
    symbol_map[index] = position + 1u;
    status = elf32_string_table_intern(&strtab, object->symbols[index].name,
                                       &symbol_name_offsets[index]);
    if (status != CTOOL_OK) {
      goto fail;
    }
  }
  layout[symtab_index].info = local_count + 1u;
  if (elf32_add_overflows(object->symbol_count, 1u) == CTOOL_TRUE ||
      elf32_mul_overflows(object->symbol_count + 1u, ELF32_SYMBOL_SIZE) ==
          CTOOL_TRUE) {
    status = CTOOL_ERR_OVERFLOW;
    goto fail;
  }
  layout[symtab_index].size =
      (object->symbol_count + 1u) * ELF32_SYMBOL_SIZE;
  layout[strtab_index].size = strtab.size;

  output_started = CTOOL_TRUE;
  status = ctool_buffer_reserve_zero(output, ELF32_HEADER_SIZE, &index, &header);
  (void)index;
  (void)header;
  for (index = 0u; status == CTOOL_OK && index < object->section_count;
       index++) {
    const ctool_elf32_section_spec_t *section = &object->sections[index];
    elf32_layout_section_t *entry = &layout[index + 1u];
    status = elf32_align_buffer(output, section->alignment);
    entry->offset = ctool_buffer_view(output).size;
    if (status == CTOOL_OK && section->type == CTOOL_ELF32_SHT_PROGBITS) {
      status = ctool_buffer_append(output, section->contents);
    }
  }
  for (index = 0u; status == CTOOL_OK && index < object->relocation_count;
       index++) {
    const ctool_elf32_relocation_spec_t *relocation =
        &object->relocations[index];
    ctool_u32 patch_offset =
        layout[relocation->target_section + 1u].offset + relocation->offset;
    status = ctool_buffer_patch_le32(output, patch_offset,
                                     (ctool_u32)relocation->addend);
  }
  for (index = 0u; status == CTOOL_OK && index < group_count; index++) {
    const elf32_relocation_group_t *group = &groups[index];
    elf32_layout_section_t *entry = &layout[group->file_section];
    status = elf32_align_buffer(output, 4u);
    entry->offset = ctool_buffer_view(output).size;
    for (other = 0u;
         status == CTOOL_OK && other < object->relocation_count; other++) {
      const ctool_elf32_relocation_spec_t *relocation =
          &object->relocations[other];
      if (relocation->target_section == group->target_section) {
        ctool_u32 info =
            (symbol_map[relocation->symbol] << 8u) |
            (ctool_u32)relocation->type;
        status = ctool_buffer_put_le32(output, relocation->offset);
        if (status == CTOOL_OK) {
          status = ctool_buffer_put_le32(output, info);
        }
      }
    }
  }
  if (status == CTOOL_OK) {
    status = elf32_align_buffer(output, 4u);
  }
  layout[symtab_index].offset = ctool_buffer_view(output).size;
  if (status == CTOOL_OK) {
    status = ctool_buffer_fill(output, 0u, ELF32_SYMBOL_SIZE);
  }
  for (position = 0u;
       status == CTOOL_OK && position < object->symbol_count; position++) {
    index = symbol_order[position];
    status = elf32_put_symbol(output, &object->symbols[index],
                              symbol_name_offsets[index]);
  }
  layout[strtab_index].offset = ctool_buffer_view(output).size;
  if (status == CTOOL_OK) {
    status = elf32_put_string_table(output, &strtab);
  }
  layout[shstrtab_index].offset = ctool_buffer_view(output).size;
  if (status == CTOOL_OK) {
    status = elf32_put_string_table(output, &shstrtab);
  }
  if (status == CTOOL_OK) {
    status = elf32_align_buffer(output, 4u);
  }
  section_headers = ctool_buffer_view(output).size;
  for (index = 0u; status == CTOOL_OK && index < total_sections; index++) {
    status = elf32_put_section_header(output, &layout[index]);
  }
  if (status == CTOOL_OK) {
    status = elf32_patch_header(output, section_headers, total_sections,
                                shstrtab_index);
  }
  if (status != CTOOL_OK) {
    goto fail;
  }
  table_size = ctool_buffer_view(output).size;
  (void)table_size;
  (void)ctool_arena_rewind(arena, arena_mark);
  return CTOOL_OK;

fail:
  (void)ctool_buffer_rewind(output, output_mark);
  (void)ctool_arena_rewind(arena, arena_mark);
  if (failure_message != (const char *)0) {
    return elf32_emit_diagnostic(job, failure_code, ctool_string(""), 0u,
                                 failure_message, status);
  }
  if (status == CTOOL_ERR_LIMIT) {
    return elf32_emit_diagnostic(
        job, CTOOL_ELF32_DIAG_LIMIT, ctool_string(""), 0u,
        output_started == CTOOL_TRUE ? "ELF32 output limit exceeded"
                                     : "ELF32 object limit exceeded",
        status);
  }
  if (status == CTOOL_ERR_OVERFLOW) {
    return elf32_emit_diagnostic(
        job, CTOOL_ELF32_DIAG_LIMIT, ctool_string(""), 0u,
        "ELF32 object representation overflow", status);
  }
  return status;
}

static ctool_u16 elf32_read_le16(ctool_bytes_t image, ctool_u32 offset) {
  return (ctool_u16)((ctool_u16)image.data[offset] |
                     (ctool_u16)((ctool_u16)image.data[offset + 1u] << 8u));
}

static ctool_u32 elf32_read_le32(ctool_bytes_t image, ctool_u32 offset) {
  return (ctool_u32)image.data[offset] |
         ((ctool_u32)image.data[offset + 1u] << 8u) |
         ((ctool_u32)image.data[offset + 2u] << 16u) |
         ((ctool_u32)image.data[offset + 3u] << 24u);
}

static ctool_i32 elf32_signed_u32(ctool_u32 value) {
  ctool_u32 magnitude;
  if (value <= 0x7fffffffu) {
    return (ctool_i32)value;
  }
  magnitude = (~value) + 1u;
  if (magnitude == 0x80000000u) {
    return (-2147483647 - 1);
  }
  return -(ctool_i32)magnitude;
}

static ctool_bool elf32_range_fits(ctool_u32 offset, ctool_u32 size,
                                    ctool_u32 image_size) {
  return offset <= image_size && size <= image_size - offset ? CTOOL_TRUE
                                                              : CTOOL_FALSE;
}

static void elf32_zero_object(ctool_elf32_object_t *object) {
  object->image.data = (const ctool_u8 *)0;
  object->image.size = 0u;
  object->file_type = (ctool_elf32_file_type_t)0;
  object->entry_point = 0u;
  object->flags = 0u;
  object->program_headers = (const ctool_elf32_program_header_t *)0;
  object->program_header_count = 0u;
  object->sections = (const ctool_elf32_section_t *)0;
  object->section_count = 0u;
  object->symbols = (const ctool_elf32_symbol_t *)0;
  object->symbol_count = 0u;
  object->relocations = (const ctool_elf32_relocation_t *)0;
  object->relocation_count = 0u;
  object->symbol_table_section_file_index = 0u;
}

static void elf32_swap_string_references(elf32_string_reference_t *left,
                                          elf32_string_reference_t *right) {
  elf32_string_reference_t temporary = *left;
  *left = *right;
  *right = temporary;
}

static void elf32_sift_string_references(elf32_string_reference_t *references,
                                          ctool_u32 root,
                                          ctool_u32 count) {
  while (count > 1u && root <= (count - 2u) / 2u) {
    ctool_u32 child = root * 2u + 1u;
    if (child + 1u < count &&
        references[child].offset < references[child + 1u].offset) {
      child++;
    }
    if (references[root].offset >= references[child].offset) {
      return;
    }
    elf32_swap_string_references(&references[root], &references[child]);
    root = child;
  }
}

static void elf32_sort_string_references(
    elf32_string_reference_t *references, ctool_u32 count) {
  ctool_u32 index = count / 2u;
  ctool_u32 end = count;
  while (index != 0u) {
    index--;
    elf32_sift_string_references(references, index, count);
  }
  while (end > 1u) {
    elf32_swap_string_references(&references[0], &references[end - 1u]);
    end--;
    elf32_sift_string_references(references, 0u, end);
  }
}

static ctool_status_t elf32_resolve_string_references(
    ctool_bytes_t table, elf32_string_reference_t *references,
    ctool_u32 count) {
  ctool_u32 index;
  ctool_u32 end = 0u;
  ctool_bool have_end = CTOOL_FALSE;
  elf32_sort_string_references(references, count);
  for (index = 0u; index < count; index++) {
    ctool_u32 offset = references[index].offset;
    if (offset >= table.size) {
      return CTOOL_ERR_INPUT;
    }
    if (have_end == CTOOL_FALSE || offset > end) {
      end = offset;
      while (end < table.size && table.data[end] != 0u) {
        end++;
      }
      if (end >= table.size) {
        return CTOOL_ERR_INPUT;
      }
      have_end = CTOOL_TRUE;
    }
    references[index].view->data = (const char *)(table.data + offset);
    references[index].view->size = end - offset;
  }
  return CTOOL_OK;
}

static void elf32_swap_relocation_sites(elf32_relocation_site_t *left,
                                         elf32_relocation_site_t *right) {
  elf32_relocation_site_t temporary = *left;
  *left = *right;
  *right = temporary;
}

static void elf32_sift_relocation_sites(elf32_relocation_site_t *sites,
                                         ctool_u32 root,
                                         ctool_u32 count) {
  while (count > 1u && root <= (count - 2u) / 2u) {
    ctool_u32 child = root * 2u + 1u;
    if (child + 1u < count && sites[child].offset < sites[child + 1u].offset) {
      child++;
    }
    if (sites[root].offset >= sites[child].offset) {
      return;
    }
    elf32_swap_relocation_sites(&sites[root], &sites[child]);
    root = child;
  }
}

static void elf32_sort_relocation_sites(elf32_relocation_site_t *sites,
                                         ctool_u32 count) {
  ctool_u32 index = count / 2u;
  ctool_u32 end = count;
  while (index != 0u) {
    index--;
    elf32_sift_relocation_sites(sites, index, count);
  }
  while (end > 1u) {
    elf32_swap_relocation_sites(&sites[0], &sites[end - 1u]);
    end--;
    elf32_sift_relocation_sites(sites, 0u, end);
  }
}

static ctool_status_t elf32_read_failure(
    ctool_job_t *job, const ctool_source_t *source, ctool_arena_t *arena,
    ctool_arena_mark_t mark, ctool_elf32_object_t *object_out,
    ctool_status_t status, ctool_u32 code, ctool_u32 offset,
    const char *message) {
  (void)ctool_arena_rewind(arena, mark);
  elf32_zero_object(object_out);
  if (status == CTOOL_ERR_NO_MEMORY) {
    return status;
  }
  return elf32_emit_diagnostic(job, code, source->path.text, offset, message,
                               status);
}

static ctool_status_t elf32_validate_string_section(
    const ctool_elf32_section_t *section) {
  if (section->type != ELF32_SHT_STRTAB || section->contents.size == 0u ||
      section->contents.data[0] != 0u ||
      section->contents.data[section->contents.size - 1u] != 0u ||
      section->link != 0u || section->info != 0u || section->entry_size != 0u) {
    return CTOOL_ERR_INPUT;
  }
  return CTOOL_OK;
}

ctool_status_t ctool_elf32_read(ctool_job_t *job,
                                 const ctool_source_t *source,
                                 ctool_elf32_object_t *object_out) {
  ctool_bytes_t image;
  ctool_arena_t *arena;
  ctool_arena_mark_t mark;
  ctool_elf32_program_header_t *program_headers =
      (ctool_elf32_program_header_t *)0;
  ctool_elf32_section_t *sections;
  ctool_elf32_symbol_t *symbols = (ctool_elf32_symbol_t *)0;
  ctool_elf32_relocation_t *relocations =
      (ctool_elf32_relocation_t *)0;
  elf32_string_reference_t *string_references =
      (elf32_string_reference_t *)0;
  elf32_relocation_site_t *relocation_sites =
      (elf32_relocation_site_t *)0;
  ctool_u32 *relocation_counts = (ctool_u32 *)0;
  ctool_u32 *relocation_cursors = (ctool_u32 *)0;
  ctool_u32 file_type;
  ctool_u32 entry_point;
  ctool_u32 elf_flags;
  ctool_u32 program_header_table;
  ctool_u32 program_header_count;
  ctool_u32 section_headers;
  ctool_u32 section_count;
  ctool_u32 shstrtab_index;
  ctool_u32 symtab_index = 0u;
  ctool_u32 symtab_count = 0u;
  ctool_u32 relocation_count = 0u;
  ctool_u32 tls_base = ELF32_U32_MAX;
  ctool_bool have_tls = CTOOL_FALSE;
  ctool_u32 index;
  ctool_u32 other;
  ctool_u32 position;
  ctool_u32 offset;
  ctool_u32 size;
  ctool_u32 table_bytes;
  ctool_bytes_t shstrtab;
  ctool_bytes_t strtab;
  ctool_arena_mark_t scratch_mark;
  ctool_status_t status;
  if (object_out != (ctool_elf32_object_t *)0) {
    elf32_zero_object(object_out);
  }
  if (job == (ctool_job_t *)0 || source == (const ctool_source_t *)0 ||
      object_out == (ctool_elf32_object_t *)0 ||
      (source->contents.data == (const ctool_u8 *)0 &&
       source->contents.size != 0u) ||
      (source->path.text.data == (const char *)0 &&
       source->path.text.size != 0u)) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  image = source->contents;
  arena = ctool_job_arena(job);
  mark = ctool_arena_mark(arena);
  if (image.size < ELF32_HEADER_SIZE) {
    return elf32_read_failure(
        job, source, arena, mark, object_out, CTOOL_ERR_INPUT,
        CTOOL_ELF32_DIAG_BAD_HEADER, image.size,
        "ELF32 header is truncated");
  }
  if (image.data[0] != 0x7fu || image.data[1] != (ctool_u8)'E' ||
      image.data[2] != (ctool_u8)'L' || image.data[3] != (ctool_u8)'F') {
    return elf32_read_failure(job, source, arena, mark, object_out,
                              CTOOL_ERR_INPUT, CTOOL_ELF32_DIAG_BAD_MAGIC, 0u,
                              "ELF32 magic is invalid");
  }
  file_type = (ctool_u32)elf32_read_le16(image, 16u);
  if (image.data[4] != 1u || image.data[5] != 1u || image.data[6] != 1u ||
      (file_type != (ctool_u32)CTOOL_ELF32_ET_REL &&
       file_type != (ctool_u32)CTOOL_ELF32_ET_EXEC) ||
      elf32_read_le16(image, 18u) != 3u ||
      elf32_read_le32(image, 20u) != 1u) {
    return elf32_read_failure(
        job, source, arena, mark, object_out, CTOOL_ERR_UNSUPPORTED,
        CTOOL_ELF32_DIAG_UNSUPPORTED_DOMAIN, 4u,
        "ELF file is not little-endian i386 ET_REL or ET_EXEC");
  }
  entry_point = elf32_read_le32(image, 24u);
  program_header_table = elf32_read_le32(image, 28u);
  section_headers = elf32_read_le32(image, 32u);
  elf_flags = elf32_read_le32(image, 36u);
  program_header_count = (ctool_u32)elf32_read_le16(image, 44u);
  section_count = (ctool_u32)elf32_read_le16(image, 48u);
  shstrtab_index = (ctool_u32)elf32_read_le16(image, 50u);
  if (elf32_read_le16(image, 40u) != ELF32_HEADER_SIZE ||
      (file_type == (ctool_u32)CTOOL_ELF32_ET_REL &&
       (entry_point != 0u || program_header_table != 0u ||
        program_header_count != 0u)) ||
      (program_header_count == 0u && program_header_table != 0u) ||
      (program_header_count != 0u &&
       (elf32_read_le16(image, 42u) != ELF32_PROGRAM_HEADER_SIZE ||
         elf32_mul_overflows(program_header_count,
                             ELF32_PROGRAM_HEADER_SIZE) == CTOOL_TRUE)) ||
      (section_count == 0u &&
       (file_type != (ctool_u32)CTOOL_ELF32_ET_EXEC ||
         section_headers != 0u || shstrtab_index != 0u)) ||
      (section_count != 0u &&
       (elf32_read_le16(image, 46u) != ELF32_SECTION_HEADER_SIZE ||
         section_count >= ELF32_SHN_LORESERVE ||
         shstrtab_index >= section_count ||
         elf32_mul_overflows(section_count, ELF32_SECTION_HEADER_SIZE) ==
            CTOOL_TRUE))) {
    return elf32_read_failure(
        job, source, arena, mark, object_out, CTOOL_ERR_INPUT,
        CTOOL_ELF32_DIAG_BAD_HEADER, 32u, "ELF32 header fields are invalid");
  }
  table_bytes = program_header_count * ELF32_PROGRAM_HEADER_SIZE;
  if (elf32_range_fits(program_header_table, table_bytes, image.size) ==
      CTOOL_FALSE) {
    return elf32_read_failure(
        job, source, arena, mark, object_out, CTOOL_ERR_INPUT,
        CTOOL_ELF32_DIAG_BAD_HEADER, program_header_table,
        "ELF32 program header table is out of range");
  }
  table_bytes = section_count * ELF32_SECTION_HEADER_SIZE;
  if (elf32_range_fits(section_headers, table_bytes, image.size) ==
      CTOOL_FALSE) {
    return elf32_read_failure(
        job, source, arena, mark, object_out, CTOOL_ERR_INPUT,
        CTOOL_ELF32_DIAG_BAD_HEADER, section_headers,
        "ELF32 section header table is out of range");
  }
  status = elf32_alloc_array(
      arena, program_header_count,
      (ctool_u32)sizeof(ctool_elf32_program_header_t),
      (void **)&program_headers);
  if (status != CTOOL_OK) {
    return elf32_read_failure(job, source, arena, mark, object_out, status,
                              CTOOL_ELF32_DIAG_LIMIT, 0u,
                              "ELF32 program-header metadata limit exceeded");
  }
  for (index = 0u; index < program_header_count; index++) {
    ctool_elf32_program_header_t *program_header = &program_headers[index];
    offset = program_header_table + index * ELF32_PROGRAM_HEADER_SIZE;
    program_header->file_index = index;
    program_header->type = elf32_read_le32(image, offset);
    program_header->file_offset = elf32_read_le32(image, offset + 4u);
    program_header->virtual_address = elf32_read_le32(image, offset + 8u);
    program_header->physical_address = elf32_read_le32(image, offset + 12u);
    program_header->file_size = elf32_read_le32(image, offset + 16u);
    program_header->memory_size = elf32_read_le32(image, offset + 20u);
    program_header->flags = elf32_read_le32(image, offset + 24u);
    program_header->alignment = elf32_read_le32(image, offset + 28u);
    if ((program_header->alignment > 1u &&
         elf32_is_power_of_two(program_header->alignment) == CTOOL_FALSE) ||
        elf32_range_fits(program_header->file_offset,
                         program_header->file_size, image.size) ==
            CTOOL_FALSE ||
        (program_header->type == CTOOL_ELF32_PT_LOAD &&
         (program_header->file_size > program_header->memory_size ||
          (program_header->memory_size != 0u &&
           program_header->virtual_address >
               ELF32_U32_MAX - (program_header->memory_size - 1u)) ||
          (program_header->alignment > 1u &&
           ((program_header->file_offset &
             (program_header->alignment - 1u)) !=
            (program_header->virtual_address &
             (program_header->alignment - 1u))))))) {
      return elf32_read_failure(
          job, source, arena, mark, object_out, CTOOL_ERR_INPUT,
          CTOOL_ELF32_DIAG_BAD_PROGRAM_HEADER, offset,
          "ELF32 program header fields are invalid");
    }
    program_header->contents.data =
        image.data + program_header->file_offset;
    program_header->contents.size = program_header->file_size;
  }
  if (section_count == 0u) {
    object_out->image = image;
    object_out->file_type = (ctool_elf32_file_type_t)file_type;
    object_out->entry_point = entry_point;
    object_out->flags = elf_flags;
    object_out->program_headers = program_headers;
    object_out->program_header_count = program_header_count;
    return CTOOL_OK;
  }
  status = elf32_alloc_array(arena, section_count,
                             (ctool_u32)sizeof(ctool_elf32_section_t),
                             (void **)&sections);
  if (status != CTOOL_OK) {
    return elf32_read_failure(job, source, arena, mark, object_out, status,
                              CTOOL_ELF32_DIAG_LIMIT, 0u,
                              "ELF32 section metadata limit exceeded");
  }
  for (index = 0u; index < section_count; index++) {
    ctool_elf32_section_t *section = &sections[index];
    offset = section_headers + index * ELF32_SECTION_HEADER_SIZE;
    section->file_index = index;
    section->name.data = (const char *)0;
    section->name.size = 0u;
    section->type = elf32_read_le32(image, offset + 4u);
    section->flags = elf32_read_le32(image, offset + 8u);
    section->address = elf32_read_le32(image, offset + 12u);
    section->file_offset = elf32_read_le32(image, offset + 16u);
    section->size = elf32_read_le32(image, offset + 20u);
    section->link = elf32_read_le32(image, offset + 24u);
    section->info = elf32_read_le32(image, offset + 28u);
    section->alignment = elf32_read_le32(image, offset + 32u);
    section->entry_size = elf32_read_le32(image, offset + 36u);
    if (file_type == (ctool_u32)CTOOL_ELF32_ET_EXEC &&
        section->size != 0u &&
        section->address > ELF32_U32_MAX - (section->size - 1u)) {
      return elf32_read_failure(
          job, source, arena, mark, object_out, CTOOL_ERR_INPUT,
          CTOOL_ELF32_DIAG_BAD_SECTION, offset,
          "ELF32 executable section address range overflows");
    }
    section->relocation_first = 0u;
    section->relocation_count = 0u;
    if (section->alignment != 0u &&
        elf32_is_power_of_two(section->alignment) == CTOOL_FALSE) {
      return elf32_read_failure(
          job, source, arena, mark, object_out, CTOOL_ERR_INPUT,
          CTOOL_ELF32_DIAG_BAD_SECTION, offset + 32u,
          "ELF32 section alignment is invalid");
    }
    if (section->type == CTOOL_ELF32_SHT_NOBITS) {
      section->contents.data = (const ctool_u8 *)0;
      section->contents.size = 0u;
    } else {
      if (elf32_range_fits(section->file_offset, section->size, image.size) ==
          CTOOL_FALSE) {
        return elf32_read_failure(
            job, source, arena, mark, object_out, CTOOL_ERR_INPUT,
            CTOOL_ELF32_DIAG_BAD_SECTION, section->file_offset,
            "ELF32 section contents are out of range");
      }
      section->contents.data = image.data + section->file_offset;
      section->contents.size = section->size;
    }
  }
  if (file_type == (ctool_u32)CTOOL_ELF32_ET_EXEC) {
    for (index = 1u; index < section_count; index++) {
      if ((sections[index].flags & CTOOL_ELF32_SHF_TLS) != 0u &&
          (have_tls == CTOOL_FALSE || sections[index].address < tls_base)) {
        tls_base = sections[index].address;
        have_tls = CTOOL_TRUE;
      }
    }
  }
  for (offset = 0u; offset < ELF32_SECTION_HEADER_SIZE; offset++) {
    if (image.data[section_headers + offset] != 0u) {
      return elf32_read_failure(
          job, source, arena, mark, object_out, CTOOL_ERR_INPUT,
          CTOOL_ELF32_DIAG_BAD_SECTION, section_headers + offset,
          "ELF32 null section is not zero");
    }
  }
  if (shstrtab_index != 0u) {
    if (elf32_validate_string_section(&sections[shstrtab_index]) != CTOOL_OK) {
      return elf32_read_failure(
          job, source, arena, mark, object_out, CTOOL_ERR_INPUT,
          CTOOL_ELF32_DIAG_BAD_STRING, sections[shstrtab_index].file_offset,
          "ELF32 section-name table is invalid");
    }
    shstrtab = sections[shstrtab_index].contents;
    scratch_mark = ctool_arena_mark(arena);
    status = elf32_alloc_array(arena, section_count,
                               (ctool_u32)sizeof(elf32_string_reference_t),
                               (void **)&string_references);
    if (status != CTOOL_OK) {
      return elf32_read_failure(job, source, arena, mark, object_out, status,
                                CTOOL_ELF32_DIAG_LIMIT, 0u,
                                "ELF32 section-name metadata limit exceeded");
    }
    for (index = 0u; index < section_count; index++) {
      offset = section_headers + index * ELF32_SECTION_HEADER_SIZE;
      string_references[index].offset = elf32_read_le32(image, offset);
      string_references[index].view = &sections[index].name;
    }
    status = elf32_resolve_string_references(shstrtab, string_references,
                                              section_count);
    if (status != CTOOL_OK) {
      return elf32_read_failure(job, source, arena, mark, object_out,
                                CTOOL_ERR_INPUT, CTOOL_ELF32_DIAG_BAD_STRING,
                                section_headers,
                                "ELF32 section name is invalid");
    }
    (void)ctool_arena_rewind(arena, scratch_mark);
    string_references = (elf32_string_reference_t *)0;
  }
  for (index = 0u; index < section_count; index++) {
    if (sections[index].type == ELF32_SHT_DYNSYM) {
      return elf32_read_failure(
          job, source, arena, mark, object_out, CTOOL_ERR_UNSUPPORTED,
          CTOOL_ELF32_DIAG_UNSUPPORTED_FEATURE, sections[index].file_offset,
          "ELF32 dynamic symbol tables are not supported");
    }
    if (sections[index].type == ELF32_SHT_SYMTAB) {
      symtab_index = index;
      symtab_count++;
    }
  }
  if (symtab_count > 1u) {
    return elf32_read_failure(
        job, source, arena, mark, object_out, CTOOL_ERR_UNSUPPORTED,
        CTOOL_ELF32_DIAG_UNSUPPORTED_FEATURE, sections[symtab_index].file_offset,
        "multiple ELF32 symbol tables are not supported");
  }
  if (symtab_count == 1u) {
    const ctool_elf32_section_t *symtab = &sections[symtab_index];
    ctool_u32 first_nonlocal = symtab->info;
    if (symtab->entry_size != ELF32_SYMBOL_SIZE ||
        symtab->size % ELF32_SYMBOL_SIZE != 0u ||
        symtab->link >= section_count ||
        elf32_validate_string_section(&sections[symtab->link]) != CTOOL_OK) {
      return elf32_read_failure(
          job, source, arena, mark, object_out, CTOOL_ERR_INPUT,
          CTOOL_ELF32_DIAG_BAD_SYMBOL, symtab->file_offset,
          "ELF32 symbol table metadata is invalid");
    }
    size = symtab->size / ELF32_SYMBOL_SIZE;
    if (size == 0u || first_nonlocal == 0u || first_nonlocal > size) {
      return elf32_read_failure(
          job, source, arena, mark, object_out, CTOOL_ERR_INPUT,
          CTOOL_ELF32_DIAG_BAD_SYMBOL, symtab->file_offset,
          "ELF32 symbol partition is invalid");
    }
    status = elf32_alloc_array(arena, size,
                               (ctool_u32)sizeof(ctool_elf32_symbol_t),
                               (void **)&symbols);
    if (status != CTOOL_OK) {
      return elf32_read_failure(job, source, arena, mark, object_out, status,
                                CTOOL_ELF32_DIAG_LIMIT, symtab->file_offset,
                                "ELF32 symbol metadata limit exceeded");
    }
    strtab = sections[symtab->link].contents;
    scratch_mark = ctool_arena_mark(arena);
    status = elf32_alloc_array(arena, size,
                               (ctool_u32)sizeof(elf32_string_reference_t),
                               (void **)&string_references);
    if (status != CTOOL_OK) {
      return elf32_read_failure(job, source, arena, mark, object_out, status,
                                CTOOL_ELF32_DIAG_LIMIT, symtab->file_offset,
                                "ELF32 symbol-name metadata limit exceeded");
    }
    for (index = 0u; index < size; index++) {
      offset = symtab->file_offset + index * ELF32_SYMBOL_SIZE;
      string_references[index].offset = elf32_read_le32(image, offset);
      string_references[index].view = &symbols[index].name;
    }
    status =
        elf32_resolve_string_references(strtab, string_references, size);
    if (status != CTOOL_OK) {
      return elf32_read_failure(
          job, source, arena, mark, object_out, CTOOL_ERR_INPUT,
          CTOOL_ELF32_DIAG_BAD_SYMBOL, symtab->file_offset,
          "ELF32 symbol name is invalid");
    }
    (void)ctool_arena_rewind(arena, scratch_mark);
    string_references = (elf32_string_reference_t *)0;
    for (index = 0u; index < size; index++) {
      ctool_elf32_symbol_t *symbol = &symbols[index];
      ctool_u32 raw_info;
      ctool_u32 raw_other;
      ctool_u32 raw_section;
      offset = symtab->file_offset + index * ELF32_SYMBOL_SIZE;
      symbol->file_index = index;
      symbol->value = elf32_read_le32(image, offset + 4u);
      symbol->size = elf32_read_le32(image, offset + 8u);
      raw_info = (ctool_u32)image.data[offset + 12u];
      raw_other = (ctool_u32)image.data[offset + 13u];
      raw_section = (ctool_u32)elf32_read_le16(image, offset + 14u);
      symbol->binding = raw_info >> 4u;
      symbol->type = raw_info & 0x0fu;
      symbol->visibility = raw_other & 0x03u;
      symbol->alignment = 0u;
      symbol->section_file_index = CTOOL_ELF32_NO_SECTION;
      if ((raw_other & 0xfcu) != 0u ||
          (index < first_nonlocal &&
           symbol->binding != CTOOL_ELF32_BIND_LOCAL) ||
          (index >= first_nonlocal &&
           symbol->binding == CTOOL_ELF32_BIND_LOCAL)) {
        return elf32_read_failure(
            job, source, arena, mark, object_out, CTOOL_ERR_INPUT,
            CTOOL_ELF32_DIAG_BAD_SYMBOL, offset,
            "ELF32 symbol binding or visibility is invalid");
      }
      if (raw_section == 0u) {
        symbol->placement = CTOOL_ELF32_SYMBOL_UNDEFINED;
      } else if (raw_section == ELF32_SHN_ABS) {
        symbol->placement = CTOOL_ELF32_SYMBOL_ABSOLUTE;
      } else if (raw_section == ELF32_SHN_COMMON) {
        symbol->placement = CTOOL_ELF32_SYMBOL_COMMON_STORAGE;
        symbol->alignment = symbol->value;
        symbol->value = 0u;
        if (symbol->binding == CTOOL_ELF32_BIND_LOCAL || symbol->size == 0u ||
            elf32_is_power_of_two(symbol->alignment) == CTOOL_FALSE ||
            (symbol->type != CTOOL_ELF32_SYMBOL_NOTYPE &&
             symbol->type != CTOOL_ELF32_SYMBOL_OBJECT &&
             symbol->type != CTOOL_ELF32_SYMBOL_COMMON)) {
          return elf32_read_failure(
              job, source, arena, mark, object_out, CTOOL_ERR_INPUT,
              CTOOL_ELF32_DIAG_BAD_SYMBOL, offset,
              "ELF32 common symbol is invalid");
        }
      } else if (raw_section < section_count) {
        ctool_u32 section_value;
        ctool_bool linker_boundary = CTOOL_FALSE;
        symbol->placement = CTOOL_ELF32_SYMBOL_DEFINED;
        symbol->section_file_index = raw_section;
        section_value = symbol->value;
        if (symbol->type == CTOOL_ELF32_SYMBOL_TLS &&
            (sections[raw_section].flags & CTOOL_ELF32_SHF_TLS) == 0u) {
          return elf32_read_failure(
              job, source, arena, mark, object_out, CTOOL_ERR_INPUT,
              CTOOL_ELF32_DIAG_BAD_SYMBOL, offset,
              "ELF32 TLS symbol section is invalid");
        }
        if (file_type == (ctool_u32)CTOOL_ELF32_ET_EXEC) {
          if (symbol->type == CTOOL_ELF32_SYMBOL_TLS) {
            ctool_u32 section_tls_offset;
            if (have_tls == CTOOL_FALSE ||
                sections[raw_section].address < tls_base) {
              return elf32_read_failure(
                  job, source, arena, mark, object_out, CTOOL_ERR_INPUT,
                  CTOOL_ELF32_DIAG_BAD_SYMBOL, offset,
                  "ELF32 TLS symbol offset is invalid");
            }
            section_tls_offset = sections[raw_section].address - tls_base;
            if (section_value < section_tls_offset) {
              return elf32_read_failure(
                  job, source, arena, mark, object_out, CTOOL_ERR_INPUT,
                  CTOOL_ELF32_DIAG_BAD_SYMBOL, offset,
                  "ELF32 TLS symbol offset is invalid");
            }
            section_value -= section_tls_offset;
          } else {
            linker_boundary =
                symbol->type == CTOOL_ELF32_SYMBOL_NOTYPE && symbol->size == 0u
                    ? CTOOL_TRUE
                    : CTOOL_FALSE;
          }
          if (symbol->type != CTOOL_ELF32_SYMBOL_TLS &&
              linker_boundary == CTOOL_FALSE &&
              section_value < sections[raw_section].address) {
            return elf32_read_failure(
                job, source, arena, mark, object_out, CTOOL_ERR_INPUT,
                CTOOL_ELF32_DIAG_BAD_SYMBOL, offset,
                "ELF32 defined symbol is out of range");
          }
          if (symbol->type != CTOOL_ELF32_SYMBOL_TLS &&
              linker_boundary == CTOOL_FALSE) {
            section_value -= sections[raw_section].address;
          }
        }
        if (linker_boundary == CTOOL_FALSE &&
            (section_value > sections[raw_section].size ||
             symbol->size > sections[raw_section].size - section_value)) {
          return elf32_read_failure(
              job, source, arena, mark, object_out, CTOOL_ERR_INPUT,
              CTOOL_ELF32_DIAG_BAD_SYMBOL, offset,
              "ELF32 defined symbol is out of range");
        }
      } else {
        symbol->placement = CTOOL_ELF32_SYMBOL_RESERVED;
        return elf32_read_failure(
            job, source, arena, mark, object_out, CTOOL_ERR_UNSUPPORTED,
            CTOOL_ELF32_DIAG_UNSUPPORTED_FEATURE, offset,
            "ELF32 reserved symbol section index is not supported");
      }
      if (symbol->type == CTOOL_ELF32_SYMBOL_SECTION &&
          (symbol->binding != CTOOL_ELF32_BIND_LOCAL ||
           symbol->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
           symbol->name.size != 0u ||
           symbol->value !=
               (file_type == (ctool_u32)CTOOL_ELF32_ET_EXEC
                    ? sections[symbol->section_file_index].address
                    : 0u) ||
           symbol->size != 0u)) {
        return elf32_read_failure(
            job, source, arena, mark, object_out, CTOOL_ERR_INPUT,
            CTOOL_ELF32_DIAG_BAD_SYMBOL, offset,
            "ELF32 section symbol semantics are invalid");
      }
      if (symbol->type == CTOOL_ELF32_SYMBOL_FILE &&
          (symbol->binding != CTOOL_ELF32_BIND_LOCAL ||
           symbol->placement != CTOOL_ELF32_SYMBOL_ABSOLUTE ||
           symbol->name.size == 0u || symbol->value != 0u ||
           symbol->size != 0u)) {
        return elf32_read_failure(
            job, source, arena, mark, object_out, CTOOL_ERR_INPUT,
            CTOOL_ELF32_DIAG_BAD_SYMBOL, offset,
            "ELF32 file symbol semantics are invalid");
      }
      if (symbol->type == CTOOL_ELF32_SYMBOL_COMMON &&
          symbol->placement != CTOOL_ELF32_SYMBOL_COMMON_STORAGE) {
        return elf32_read_failure(
            job, source, arena, mark, object_out, CTOOL_ERR_INPUT,
            CTOOL_ELF32_DIAG_BAD_SYMBOL, offset,
            "ELF32 common symbol semantics are invalid");
      }
      if (symbol->type == CTOOL_ELF32_SYMBOL_TLS &&
          symbol->placement != CTOOL_ELF32_SYMBOL_DEFINED &&
          symbol->placement != CTOOL_ELF32_SYMBOL_UNDEFINED) {
        return elf32_read_failure(
            job, source, arena, mark, object_out, CTOOL_ERR_INPUT,
            CTOOL_ELF32_DIAG_BAD_SYMBOL, offset,
            "ELF32 TLS symbol placement is invalid");
      }
    }
    for (offset = 0u; offset < ELF32_SYMBOL_SIZE; offset++) {
      if (image.data[symtab->file_offset + offset] != 0u) {
        return elf32_read_failure(
            job, source, arena, mark, object_out, CTOOL_ERR_INPUT,
            CTOOL_ELF32_DIAG_BAD_SYMBOL, symtab->file_offset + offset,
            "ELF32 null symbol is not zero");
      }
    }
    object_out->symbol_count = size;
  }

  status = elf32_alloc_array(arena, section_count,
                             (ctool_u32)sizeof(ctool_u32),
                             (void **)&relocation_counts);
  if (status == CTOOL_OK) {
    status = elf32_alloc_array(arena, section_count,
                               (ctool_u32)sizeof(ctool_u32),
                               (void **)&relocation_cursors);
  }
  if (status != CTOOL_OK) {
    return elf32_read_failure(job, source, arena, mark, object_out, status,
                              CTOOL_ELF32_DIAG_LIMIT, 0u,
                              "ELF32 relocation metadata limit exceeded");
  }
  for (index = 1u; index < section_count; index++) {
    const ctool_elf32_section_t *rel_section = &sections[index];
    if (rel_section->type != ELF32_SHT_REL) {
      continue;
    }
    if (symtab_count != 1u || rel_section->entry_size != ELF32_RELOCATION_SIZE ||
        rel_section->size % ELF32_RELOCATION_SIZE != 0u ||
        rel_section->link != symtab_index || rel_section->info == 0u ||
        rel_section->info >= section_count) {
      return elf32_read_failure(
          job, source, arena, mark, object_out, CTOOL_ERR_INPUT,
          CTOOL_ELF32_DIAG_BAD_RELOCATION, rel_section->file_offset,
          "ELF32 relocation section metadata is invalid");
    }
    size = rel_section->size / ELF32_RELOCATION_SIZE;
    if (elf32_add_overflows(relocation_counts[rel_section->info], size) ==
            CTOOL_TRUE ||
        elf32_add_overflows(relocation_count, size) == CTOOL_TRUE) {
      return elf32_read_failure(
          job, source, arena, mark, object_out, CTOOL_ERR_INPUT,
          CTOOL_ELF32_DIAG_BAD_RELOCATION, rel_section->file_offset,
          "ELF32 relocation count overflows");
    }
    relocation_counts[rel_section->info] += size;
    relocation_count += size;
  }
  status = elf32_alloc_array(arena, relocation_count,
                             (ctool_u32)sizeof(ctool_elf32_relocation_t),
                             (void **)&relocations);
  if (status != CTOOL_OK) {
    return elf32_read_failure(job, source, arena, mark, object_out, status,
                              CTOOL_ELF32_DIAG_LIMIT, 0u,
                              "ELF32 relocation metadata limit exceeded");
  }
  position = 0u;
  for (index = 0u; index < section_count; index++) {
    sections[index].relocation_first = position;
    sections[index].relocation_count = relocation_counts[index];
    relocation_cursors[index] = position;
    position += relocation_counts[index];
  }
  for (index = 1u; index < section_count; index++) {
    const ctool_elf32_section_t *rel_section = &sections[index];
    ctool_u32 entry_count;
    if (rel_section->type != ELF32_SHT_REL) {
      continue;
    }
    entry_count = rel_section->size / ELF32_RELOCATION_SIZE;
    for (other = 0u; other < entry_count; other++) {
      ctool_u32 raw_info;
      ctool_u32 symbol_index;
      ctool_u32 type;
      ctool_elf32_relocation_t *relocation =
          &relocations[relocation_cursors[rel_section->info]++];
      offset = rel_section->file_offset + other * ELF32_RELOCATION_SIZE;
      relocation->relocation_section_file_index = index;
      relocation->entry_index = other;
      relocation->target_section_file_index = rel_section->info;
      relocation->offset = elf32_read_le32(image, offset);
      raw_info = elf32_read_le32(image, offset + 4u);
      symbol_index = raw_info >> 8u;
      type = raw_info & 0xffu;
      relocation->symbol_file_index = symbol_index;
      relocation->type = type;
      relocation->addend_known = CTOOL_FALSE;
      relocation->addend = 0;
      if (symbol_index >= object_out->symbol_count) {
        return elf32_read_failure(
            job, source, arena, mark, object_out, CTOOL_ERR_INPUT,
            CTOOL_ELF32_DIAG_BAD_RELOCATION, offset,
            "ELF32 relocation symbol index is invalid");
      }
      if (type == CTOOL_ELF32_R_386_32 ||
          type == CTOOL_ELF32_R_386_PC32) {
        const ctool_elf32_section_t *target = &sections[rel_section->info];
        ctool_u32 target_offset = relocation->offset;
        if (file_type == (ctool_u32)CTOOL_ELF32_ET_EXEC) {
          if (target_offset < target->address) {
            return elf32_read_failure(
                job, source, arena, mark, object_out, CTOOL_ERR_INPUT,
                CTOOL_ELF32_DIAG_BAD_RELOCATION, offset,
                "ELF32 relocation target is out of range");
          }
          target_offset -= target->address;
        }
        if (target->type == CTOOL_ELF32_SHT_NOBITS ||
            target_offset > target->size ||
            target->size - target_offset < 4u ||
            target->contents.size < target_offset + 4u) {
          return elf32_read_failure(
              job, source, arena, mark, object_out, CTOOL_ERR_INPUT,
              CTOOL_ELF32_DIAG_BAD_RELOCATION, offset,
              "ELF32 relocation target is out of range");
        }
        relocation->addend_known = CTOOL_TRUE;
        {
          ctool_u32 field_value =
              elf32_read_le32(target->contents, target_offset);
          if (file_type == (ctool_u32)CTOOL_ELF32_ET_EXEC) {
            const ctool_elf32_symbol_t *symbol =
                &symbols[symbol_index];
            if (symbol->placement != CTOOL_ELF32_SYMBOL_DEFINED &&
                symbol->placement != CTOOL_ELF32_SYMBOL_ABSOLUTE) {
              relocation->addend_known = CTOOL_FALSE;
            } else if (type == CTOOL_ELF32_R_386_32) {
              relocation->addend =
                  elf32_signed_u32(field_value - symbol->value);
            } else {
              relocation->addend = elf32_signed_u32(
                  field_value + relocation->offset - symbol->value);
            }
          } else {
            relocation->addend = elf32_signed_u32(field_value);
          }
        }
      } else {
        ctool_u32 target_offset = relocation->offset;
        if (file_type == (ctool_u32)CTOOL_ELF32_ET_EXEC) {
          if (target_offset < sections[rel_section->info].address) {
            return elf32_read_failure(
                job, source, arena, mark, object_out, CTOOL_ERR_INPUT,
                CTOOL_ELF32_DIAG_BAD_RELOCATION, offset,
                "ELF32 relocation offset is invalid");
          }
          target_offset -= sections[rel_section->info].address;
        }
        if (target_offset > sections[rel_section->info].size) {
          return elf32_read_failure(
              job, source, arena, mark, object_out, CTOOL_ERR_INPUT,
              CTOOL_ELF32_DIAG_BAD_RELOCATION, offset,
              "ELF32 relocation offset is invalid");
        }
      }
    }
  }
  for (index = 1u; index < section_count; index++) {
    ctool_u32 known_count = 0u;
    ctool_u32 first = sections[index].relocation_first;
    ctool_u32 count = sections[index].relocation_count;
    for (other = 0u; other < count; other++) {
      ctool_u32 type = relocations[first + other].type;
      if (type == CTOOL_ELF32_R_386_32 ||
          type == CTOOL_ELF32_R_386_PC32) {
        known_count++;
      }
    }
    if (known_count < 2u) {
      continue;
    }
    scratch_mark = ctool_arena_mark(arena);
    status = elf32_alloc_array(arena, known_count,
                               (ctool_u32)sizeof(elf32_relocation_site_t),
                               (void **)&relocation_sites);
    if (status != CTOOL_OK) {
      return elf32_read_failure(job, source, arena, mark, object_out, status,
                                CTOOL_ELF32_DIAG_LIMIT, 0u,
                                "ELF32 relocation-site limit exceeded");
    }
    position = 0u;
    for (other = 0u; other < count; other++) {
      const ctool_elf32_relocation_t *relocation =
          &relocations[first + other];
      if (relocation->type == CTOOL_ELF32_R_386_32 ||
          relocation->type == CTOOL_ELF32_R_386_PC32) {
        const ctool_elf32_section_t *relocation_section =
            &sections[relocation->relocation_section_file_index];
        relocation_sites[position].offset = relocation->offset;
        relocation_sites[position].record_offset =
            relocation_section->file_offset +
            relocation->entry_index * ELF32_RELOCATION_SIZE;
        position++;
      }
    }
    elf32_sort_relocation_sites(relocation_sites, known_count);
    for (other = 1u; other < known_count; other++) {
      if (relocation_sites[other].offset -
              relocation_sites[other - 1u].offset <
          4u) {
        return elf32_read_failure(
            job, source, arena, mark, object_out, CTOOL_ERR_INPUT,
            CTOOL_ELF32_DIAG_BAD_RELOCATION,
            relocation_sites[other].record_offset,
            "ELF32 relocation fields overlap");
      }
    }
    (void)ctool_arena_rewind(arena, scratch_mark);
    relocation_sites = (elf32_relocation_site_t *)0;
  }
  object_out->image = image;
  object_out->file_type = (ctool_elf32_file_type_t)file_type;
  object_out->entry_point = entry_point;
  object_out->flags = elf_flags;
  object_out->program_headers = program_headers;
  object_out->program_header_count = program_header_count;
  object_out->sections = sections;
  object_out->section_count = section_count;
  object_out->symbols = symbols;
  object_out->relocations = relocations;
  object_out->relocation_count = relocation_count;
  object_out->symbol_table_section_file_index = symtab_index;
  return CTOOL_OK;
}
