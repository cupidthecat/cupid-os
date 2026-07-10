#include "cupidld.h"

#include "elf32.h"

#define LD_NONE 0xffffffffu
#define LD_U32_MAX 0xffffffffu
#define LD_PAGE_SIZE 4096u
#define LD_ELF_HEADER_SIZE 52u
#define LD_PROGRAM_HEADER_SIZE 32u
#define LD_SECTION_HEADER_SIZE 40u
#define LD_SYMBOL_SIZE 16u
#define LD_SHT_NULL 0u
#define LD_SHT_SYMTAB 2u
#define LD_SHT_STRTAB 3u
#define LD_SHN_ABS 0xfff1u
#define LD_PT_GNU_STACK 0x6474e551u

typedef struct {
  ctool_u32 output_index;
  ctool_u32 base_offset;
  ctool_u32 *offset_map;
  ctool_bool matched;
} ld_section_map_t;

typedef struct {
  const ctool_source_t *source;
  ctool_elf32_object_t object;
  ld_section_map_t *section_maps;
} ld_object_t;

typedef struct {
  ctool_string_t name;
  ctool_u32 type;
  ctool_u32 flags;
  ctool_u32 alignment;
  ctool_u32 address;
  ctool_u32 size;
  ctool_u32 file_size;
  ctool_u32 staging_offset;
  ctool_u32 file_offset;
  ctool_u32 name_offset;
} ld_output_section_t;

typedef struct {
  ctool_u32 output_index;
  ctool_u32 entry_size;
  ctool_u32 size;
  ctool_u32 output_offset;
  ctool_bool strings;
  ctool_bool has_relocation;
} ld_merge_atom_t;

typedef enum {
  LD_DEFINITION_NONE = 0,
  LD_DEFINITION_WEAK = 1,
  LD_DEFINITION_COMMON = 2,
  LD_DEFINITION_STRONG = 3,
  LD_DEFINITION_SCRIPT = 4
} ld_definition_rank_t;

typedef struct {
  ctool_string_t name;
  ld_definition_rank_t rank;
  ctool_bool required;
  ctool_bool value_ready;
  ctool_bool script_symbol;
  ctool_u32 first_object;
  ctool_u32 first_symbol;
  ctool_u32 winner_object;
  ctool_u32 winner_symbol;
  ctool_u32 common_size;
  ctool_u32 common_alignment;
  ctool_u32 value;
  ctool_u32 output_index;
  ctool_u32 size;
  ctool_u32 binding;
  ctool_u32 type;
  ctool_u32 visibility;
} ld_global_t;

typedef struct {
  ctool_job_t *job;
  const ctool_ld_request_t *request;
  ctool_arena_t *arena;
  ld_object_t *objects;
  ctool_u32 object_count;
  ctool_u32 total_sections;
  ctool_u32 total_symbols;
  ctool_u32 total_relocations;
  ld_global_t *globals;
  ctool_u32 global_count;
  ctool_u32 global_capacity;
  ctool_u32 *global_slots;
  ctool_u32 global_slot_count;
  ld_output_section_t *outputs;
  ctool_u32 output_count;
  ctool_u32 output_capacity;
  ld_merge_atom_t *merge_atoms;
  ctool_u32 merge_atom_count;
  ctool_u32 merge_atom_capacity;
  ctool_buffer_t *payload;
  ctool_u32 dot;
  ctool_u32 current_output;
  ctool_u32 last_output;
  ctool_string_t entry_symbol;
  ctool_u32 applied_relocations;
} ld_context_t;

typedef enum {
  LD_TOKEN_EOF = 0,
  LD_TOKEN_NAME,
  LD_TOKEN_NUMBER,
  LD_TOKEN_STRING,
  LD_TOKEN_LEFT_BRACE,
  LD_TOKEN_RIGHT_BRACE,
  LD_TOKEN_LEFT_PAREN,
  LD_TOKEN_RIGHT_PAREN,
  LD_TOKEN_COLON,
  LD_TOKEN_SEMICOLON,
  LD_TOKEN_STAR,
  LD_TOKEN_ASSIGN,
  LD_TOKEN_COMMA,
  LD_TOKEN_LESS_EQUAL
} ld_token_kind_t;

typedef struct {
  ld_token_kind_t kind;
  ctool_string_t text;
  ctool_u32 number;
  ctool_u32 line;
  ctool_u32 column;
} ld_token_t;

typedef struct {
  ld_context_t *link;
  const ctool_source_t *source;
  ctool_u32 offset;
  ctool_u32 line;
  ctool_u32 column;
  ld_token_t token;
} ld_parser_t;

static void ld_zero(void *value, ctool_u32 size) {
  ctool_u8 *bytes = (ctool_u8 *)value;
  ctool_u32 index;
  for (index = 0u; index < size; index++) {
    bytes[index] = 0u;
  }
}

static ctool_bool ld_add_overflows(ctool_u32 left, ctool_u32 right) {
  return left > LD_U32_MAX - right ? CTOOL_TRUE : CTOOL_FALSE;
}

static ctool_bool ld_multiply_overflows(ctool_u32 left, ctool_u32 right) {
  return left != 0u && right > LD_U32_MAX / left ? CTOOL_TRUE : CTOOL_FALSE;
}

static ctool_bool ld_is_power_of_two(ctool_u32 value) {
  return value != 0u && (value & (value - 1u)) == 0u ? CTOOL_TRUE
                                                     : CTOOL_FALSE;
}

static ctool_status_t ld_align_value(ctool_u32 value, ctool_u32 alignment,
                                     ctool_u32 *result_out) {
  ctool_u32 mask;
  if (result_out == (ctool_u32 *)0 ||
      ld_is_power_of_two(alignment) == CTOOL_FALSE) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  mask = alignment - 1u;
  if (value > LD_U32_MAX - mask) {
    return CTOOL_ERR_OVERFLOW;
  }
  *result_out = (value + mask) & ~mask;
  return CTOOL_OK;
}

static ctool_status_t ld_alloc_array(ctool_arena_t *arena, ctool_u32 count,
                                     ctool_u32 element_size,
                                     void **allocation_out) {
  if (allocation_out == (void **)0 ||
      ld_multiply_overflows(count, element_size) == CTOOL_TRUE) {
    return CTOOL_ERR_OVERFLOW;
  }
  if (count == 0u) {
    *allocation_out = (void *)0;
    return CTOOL_OK;
  }
  return ctool_arena_alloc_zero(arena, count, element_size,
                                (ctool_u32)sizeof(void *), allocation_out);
}

static ctool_bool ld_string_equal(ctool_string_t left,
                                  ctool_string_t right) {
  ctool_u32 index;
  if (left.size != right.size ||
      (left.data == (const char *)0 && left.size != 0u) ||
      (right.data == (const char *)0 && right.size != 0u)) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < left.size; index++) {
    if (left.data[index] != right.data[index]) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_bool ld_string_literal(ctool_string_t value,
                                    const char *literal) {
  return ld_string_equal(value, ctool_string(literal));
}

static ctool_bool ld_string_prefix(ctool_string_t value,
                                   ctool_string_t prefix) {
  ctool_u32 index;
  if (value.size < prefix.size || prefix.data == (const char *)0) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < prefix.size; index++) {
    if (value.data[index] != prefix.data[index]) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_u32 ld_hash_string(ctool_string_t value) {
  ctool_u32 hash = 2166136261u;
  ctool_u32 index;
  for (index = 0u; index < value.size; index++) {
    hash ^= (ctool_u32)(ctool_u8)value.data[index];
    hash *= 16777619u;
  }
  return hash;
}

static ctool_status_t ld_diagnostic_text(ctool_job_t *job, ctool_u32 code,
                                         ctool_string_t path, ctool_u32 line,
                                         ctool_u32 column,
                                         ctool_string_t message,
                                         ctool_status_t status) {
  ctool_diagnostic_t diagnostic;
  diagnostic.severity = CTOOL_DIAG_ERROR;
  diagnostic.code = code;
  diagnostic.path = path;
  diagnostic.line = line;
  diagnostic.column = column;
  diagnostic.message = message;
  if (ctool_job_emit(job, &diagnostic) != CTOOL_OK) {
    return CTOOL_ERR_LIMIT;
  }
  return status;
}

static ctool_status_t ld_diagnostic(ctool_job_t *job, ctool_u32 code,
                                    ctool_string_t path, ctool_u32 line,
                                    ctool_u32 column, const char *message,
                                    ctool_status_t status) {
  return ld_diagnostic_text(job, code, path, line, column,
                            ctool_string(message), status);
}

static ctool_status_t ld_buffer_pad_to(ctool_buffer_t *buffer,
                                       ctool_u32 offset) {
  ctool_u32 size = ctool_buffer_view(buffer).size;
  if (offset < size) {
    return CTOOL_ERR_INTERNAL;
  }
  return ctool_buffer_fill(buffer, 0u, offset - size);
}

static ctool_status_t ld_payload_pad_to(ld_context_t *link,
                                        const ld_output_section_t *output,
                                        ctool_u32 offset,
                                        ctool_u32 flags) {
  ctool_u32 size = ctool_buffer_view(link->payload).size;
  ctool_u8 fill = (flags & CTOOL_ELF32_SHF_EXECINSTR) != 0u ? 0xccu : 0u;
  if (offset < size || offset < output->staging_offset) {
    return CTOOL_ERR_INTERNAL;
  }
  return ctool_buffer_fill(link->payload, fill, offset - size);
}

static ctool_status_t ld_add_count(ctool_u32 *total, ctool_u32 value) {
  if (ld_add_overflows(*total, value) == CTOOL_TRUE) {
    return CTOOL_ERR_OVERFLOW;
  }
  *total += value;
  return CTOOL_OK;
}

static ctool_status_t ld_validate_allocated_section(
    ld_context_t *link, const ld_object_t *object, ctool_u32 section_index) {
  const ctool_elf32_section_t *section =
      &object->object.sections[section_index];
  const ctool_u32 supported_flags =
      CTOOL_ELF32_SHF_WRITE | CTOOL_ELF32_SHF_ALLOC |
      CTOOL_ELF32_SHF_EXECINSTR | CTOOL_ELF32_SHF_MERGE |
      CTOOL_ELF32_SHF_STRINGS;
  if ((section->flags & CTOOL_ELF32_SHF_ALLOC) == 0u) {
    return CTOOL_OK;
  }
  if (section->type != (ctool_u32)CTOOL_ELF32_SHT_PROGBITS &&
      section->type != (ctool_u32)CTOOL_ELF32_SHT_NOBITS) {
    return ld_diagnostic(link->job, CTOOL_LD_DIAG_UNSUPPORTED_INPUT,
                         object->source->path.text, 0u, section_index,
                         "CupidLD allocated section type is unsupported",
                         CTOOL_ERR_UNSUPPORTED);
  }
  if ((section->flags & ~supported_flags) != 0u) {
    return ld_diagnostic(link->job, CTOOL_LD_DIAG_UNSUPPORTED_INPUT,
                         object->source->path.text, 0u, section_index,
                         "CupidLD allocated section flags are unsupported",
                         CTOOL_ERR_UNSUPPORTED);
  }
  return CTOOL_OK;
}

static ctool_status_t ld_read_objects(ld_context_t *link) {
  ctool_u32 object_index;
  ctool_status_t status;
  status = ld_alloc_array(link->arena, link->object_count,
                          (ctool_u32)sizeof(ld_object_t),
                          (void **)&link->objects);
  if (status != CTOOL_OK) {
    return status;
  }
  for (object_index = 0u; object_index < link->object_count; object_index++) {
    ld_object_t *object = &link->objects[object_index];
    ctool_u32 section_index;
    object->source = &link->request->objects[object_index];
    status = ctool_elf32_read(link->job, object->source, &object->object);
    if (status != CTOOL_OK) {
      (void)ld_diagnostic(
          link->job, CTOOL_LD_DIAG_UNSUPPORTED_INPUT,
          object->source->path.text, 0u, 0u,
          "CupidLD input is not a supported i386 relocatable object", status);
      return status;
    }
    if (object->object.file_type != CTOOL_ELF32_ET_REL) {
      return ld_diagnostic(link->job, CTOOL_LD_DIAG_UNSUPPORTED_INPUT,
                           object->source->path.text, 0u, 0u,
                           "CupidLD requires ELF32 relocatable input",
                           CTOOL_ERR_UNSUPPORTED);
    }
    status = ld_add_count(&link->total_sections, object->object.section_count);
    if (status == CTOOL_OK) {
      status = ld_add_count(&link->total_symbols, object->object.symbol_count);
    }
    if (status == CTOOL_OK) {
      status =
          ld_add_count(&link->total_relocations,
                       object->object.relocation_count);
    }
    if (status != CTOOL_OK) {
      return ld_diagnostic(link->job, CTOOL_LD_DIAG_OVERFLOW,
                           object->source->path.text, 0u, 0u,
                           "CupidLD input counts overflow", status);
    }
    status = ld_alloc_array(link->arena, object->object.section_count,
                            (ctool_u32)sizeof(ld_section_map_t),
                            (void **)&object->section_maps);
    if (status != CTOOL_OK) {
      return status;
    }
    for (section_index = 0u; section_index < object->object.section_count;
         section_index++) {
      const ctool_elf32_section_t *section =
          &object->object.sections[section_index];
      object->section_maps[section_index].output_index = LD_NONE;
      status = ld_validate_allocated_section(link, object, section_index);
      if (status != CTOOL_OK) {
        return status;
      }
      if ((section->flags & CTOOL_ELF32_SHF_ALLOC) != 0u &&
          (section->flags & CTOOL_ELF32_SHF_MERGE) != 0u) {
        status = ld_add_count(&link->merge_atom_capacity, section->size);
        if (status != CTOOL_OK) {
          return status;
        }
      }
    }
  }
  return CTOOL_OK;
}

static ctool_status_t ld_find_global(ld_context_t *link, ctool_string_t name,
                                     ctool_bool create,
                                     ctool_u32 *index_out) {
  ctool_u32 mask = link->global_slot_count - 1u;
  ctool_u32 slot = ld_hash_string(name) & mask;
  ctool_u32 probes;
  for (probes = 0u; probes < link->global_slot_count; probes++) {
    ctool_u32 stored = link->global_slots[slot];
    if (stored == 0u) {
      if (create == CTOOL_FALSE) {
        return CTOOL_ERR_NOT_FOUND;
      }
      if (link->global_count >= link->global_capacity) {
        return CTOOL_ERR_LIMIT;
      }
      link->globals[link->global_count].name = name;
      link->globals[link->global_count].first_object = LD_NONE;
      link->globals[link->global_count].first_symbol = LD_NONE;
      link->globals[link->global_count].winner_object = LD_NONE;
      link->globals[link->global_count].winner_symbol = LD_NONE;
      link->globals[link->global_count].output_index = LD_NONE;
      link->global_slots[slot] = link->global_count + 1u;
      *index_out = link->global_count;
      link->global_count++;
      return CTOOL_OK;
    }
    if (ld_string_equal(link->globals[stored - 1u].name, name) == CTOOL_TRUE) {
      *index_out = stored - 1u;
      return CTOOL_OK;
    }
    slot = (slot + 1u) & mask;
  }
  return CTOOL_ERR_LIMIT;
}

static ctool_status_t ld_prepare_globals(ld_context_t *link) {
  ctool_u32 requested_capacity = link->total_symbols;
  ctool_u32 slots = 8u;
  ctool_u32 object_index;
  ctool_status_t status;
  if (link->request->layout.kind == CTOOL_LD_LAYOUT_SCRIPT &&
      link->request->layout.as.script != (const ctool_source_t *)0) {
    status = ld_add_count(&requested_capacity,
                          link->request->layout.as.script->contents.size);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  if (ld_add_overflows(requested_capacity, 1u) == CTOOL_TRUE) {
    return CTOOL_ERR_OVERFLOW;
  }
  link->global_capacity = requested_capacity + 1u;
  while (slots < link->global_capacity && slots <= 0x40000000u) {
    slots <<= 1u;
  }
  if (slots < link->global_capacity) {
    return CTOOL_ERR_OVERFLOW;
  }
  link->global_slot_count = slots;
  status = ld_alloc_array(link->arena, link->global_capacity,
                          (ctool_u32)sizeof(ld_global_t),
                          (void **)&link->globals);
  if (status == CTOOL_OK) {
    status = ld_alloc_array(link->arena, slots, (ctool_u32)sizeof(ctool_u32),
                            (void **)&link->global_slots);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  for (object_index = 0u; object_index < link->object_count; object_index++) {
    const ld_object_t *object = &link->objects[object_index];
    ctool_u32 symbol_index;
    for (symbol_index = 1u; symbol_index < object->object.symbol_count;
         symbol_index++) {
      const ctool_elf32_symbol_t *symbol =
          &object->object.symbols[symbol_index];
      ld_definition_rank_t rank = LD_DEFINITION_NONE;
      ctool_u32 global_index;
      ld_global_t *global;
      if (symbol->binding == CTOOL_ELF32_BIND_LOCAL) {
        continue;
      }
      if (symbol->binding != CTOOL_ELF32_BIND_GLOBAL &&
          symbol->binding != CTOOL_ELF32_BIND_WEAK) {
        return ld_diagnostic(link->job, CTOOL_LD_DIAG_UNSUPPORTED_INPUT,
                             object->source->path.text, 0u, symbol_index,
                             "CupidLD symbol binding is unsupported",
                             CTOOL_ERR_UNSUPPORTED);
      }
      status = ld_find_global(link, symbol->name, CTOOL_TRUE, &global_index);
      if (status != CTOOL_OK) {
        return status;
      }
      global = &link->globals[global_index];
      if (global->first_object == LD_NONE) {
        global->first_object = object_index;
        global->first_symbol = symbol_index;
      }
      if (symbol->placement == CTOOL_ELF32_SYMBOL_UNDEFINED) {
        if (symbol->binding == CTOOL_ELF32_BIND_GLOBAL) {
          global->required = CTOOL_TRUE;
        }
        continue;
      }
      if (symbol->placement == CTOOL_ELF32_SYMBOL_COMMON_STORAGE) {
        rank = LD_DEFINITION_COMMON;
      } else if (symbol->binding == CTOOL_ELF32_BIND_WEAK) {
        rank = LD_DEFINITION_WEAK;
      } else if (symbol->placement == CTOOL_ELF32_SYMBOL_DEFINED ||
                 symbol->placement == CTOOL_ELF32_SYMBOL_ABSOLUTE) {
        rank = LD_DEFINITION_STRONG;
      } else {
        return ld_diagnostic(link->job, CTOOL_LD_DIAG_UNSUPPORTED_INPUT,
                             object->source->path.text, 0u, symbol_index,
                             "CupidLD symbol placement is unsupported",
                             CTOOL_ERR_UNSUPPORTED);
      }
      if (rank == LD_DEFINITION_STRONG &&
          global->rank >= LD_DEFINITION_STRONG) {
        return ld_diagnostic(link->job, CTOOL_LD_DIAG_DUPLICATE_SYMBOL,
                             object->source->path.text, 0u, symbol_index,
                             "CupidLD found multiple strong definitions",
                             CTOOL_ERR_INPUT);
      }
      if (rank == LD_DEFINITION_COMMON &&
          global->rank == LD_DEFINITION_COMMON) {
        if (symbol->size > global->common_size) {
          global->common_size = symbol->size;
        }
        if (symbol->alignment > global->common_alignment) {
          global->common_alignment = symbol->alignment;
        }
      }
      if (rank > global->rank) {
        global->rank = rank;
        global->winner_object = object_index;
        global->winner_symbol = symbol_index;
        global->size = symbol->size;
        global->binding = symbol->binding;
        global->type = symbol->type;
        global->visibility = symbol->visibility;
        if (rank == LD_DEFINITION_COMMON) {
          global->common_size = symbol->size;
          global->common_alignment = symbol->alignment;
        }
      }
    }
  }
  return CTOOL_OK;
}

static ctool_status_t ld_prepare_layout_storage(ld_context_t *link) {
  ctool_u32 extra = 8u;
  ctool_status_t status;
  if (link->request->layout.kind == CTOOL_LD_LAYOUT_SCRIPT &&
      link->request->layout.as.script != (const ctool_source_t *)0) {
    extra = link->request->layout.as.script->contents.size;
  }
  if (ld_add_overflows(link->total_sections, extra) == CTOOL_TRUE) {
    return CTOOL_ERR_OVERFLOW;
  }
  link->output_capacity = link->total_sections + extra;
  status = ld_alloc_array(link->arena, link->output_capacity,
                          (ctool_u32)sizeof(ld_output_section_t),
                          (void **)&link->outputs);
  if (status == CTOOL_OK) {
    status = ld_alloc_array(link->arena, link->merge_atom_capacity,
                            (ctool_u32)sizeof(ld_merge_atom_t),
                            (void **)&link->merge_atoms);
  }
  return status;
}

static ctool_status_t ld_begin_output(ld_context_t *link, ctool_string_t name,
                                      ctool_u32 address,
                                      ctool_u32 *output_index_out) {
  ld_output_section_t *output;
  if (link->output_count >= link->output_capacity) {
    return CTOOL_ERR_LIMIT;
  }
  output = &link->outputs[link->output_count];
  output->name = name;
  output->type = 0u;
  output->flags = 0u;
  output->alignment = 1u;
  output->address = address;
  output->staging_offset = ctool_buffer_view(link->payload).size;
  *output_index_out = link->output_count;
  link->last_output = link->output_count;
  link->output_count++;
  return CTOOL_OK;
}

static ctool_status_t ld_raise_script_output_alignment(
    ld_context_t *link, ctool_u32 output_index, ctool_u32 alignment) {
  ld_output_section_t *output = &link->outputs[output_index];
  ctool_u32 address;
  ctool_u32 delta;
  ctool_u32 global_index;
  ctool_status_t status;
  if (alignment == 0u) {
    alignment = 1u;
  }
  if (alignment <= output->alignment) {
    return CTOOL_OK;
  }
  status = ld_align_value(output->address, alignment, &address);
  if (status != CTOOL_OK) {
    return status;
  }
  delta = address - output->address;
  if (delta != 0u) {
    for (global_index = 0u; global_index < link->global_count;
         global_index++) {
      ld_global_t *global = &link->globals[global_index];
      if (global->script_symbol == CTOOL_TRUE &&
          global->output_index == output_index) {
        if (ld_add_overflows(global->value, delta) == CTOOL_TRUE) {
          return CTOOL_ERR_OVERFLOW;
        }
        global->value += delta;
      }
    }
    if (link->current_output == output_index) {
      if (ld_add_overflows(link->dot, delta) == CTOOL_TRUE) {
        return CTOOL_ERR_OVERFLOW;
      }
      link->dot += delta;
    }
    output->address = address;
  }
  output->alignment = alignment;
  return CTOOL_OK;
}

static ctool_bool ld_source_equals_payload(ctool_bytes_t source,
                                           ctool_u32 source_offset,
                                           ctool_bytes_t payload,
                                           ctool_u32 payload_offset,
                                           ctool_u32 size) {
  ctool_u32 index;
  if (source_offset > source.size || size > source.size - source_offset ||
      payload_offset > payload.size || size > payload.size - payload_offset) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < size; index++) {
    if (source.data[source_offset + index] !=
        payload.data[payload_offset + index]) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_status_t ld_merge_atom_size(const ctool_elf32_section_t *section,
                                         ctool_u32 offset,
                                         ctool_u32 *size_out) {
  ctool_u32 cursor;
  if (section->entry_size == 0u ||
      section->size % section->entry_size != 0u) {
    return CTOOL_ERR_INPUT;
  }
  if ((section->flags & CTOOL_ELF32_SHF_STRINGS) == 0u) {
    *size_out = section->entry_size;
    return offset <= section->size &&
                   section->entry_size <= section->size - offset
               ? CTOOL_OK
               : CTOOL_ERR_INPUT;
  }
  cursor = offset;
  while (cursor < section->size) {
    ctool_bool terminator = CTOOL_TRUE;
    ctool_u32 unit;
    if (section->entry_size > section->size - cursor) {
      return CTOOL_ERR_INPUT;
    }
    for (unit = 0u; unit < section->entry_size; unit++) {
      if (section->contents.data[cursor + unit] != 0u) {
        terminator = CTOOL_FALSE;
      }
    }
    cursor += section->entry_size;
    if (terminator == CTOOL_TRUE) {
      *size_out = cursor - offset;
      return CTOOL_OK;
    }
  }
  return CTOOL_ERR_INPUT;
}

static ctool_bool ld_merge_atom_has_relocation(
    const ld_object_t *object, ctool_u32 section_index, ctool_u32 offset,
    ctool_u32 size) {
  const ctool_elf32_section_t *section =
      &object->object.sections[section_index];
  ctool_u32 end = offset + size;
  ctool_u32 index;
  for (index = 0u; index < section->relocation_count; index++) {
    const ctool_elf32_relocation_t *relocation =
        &object->object.relocations[section->relocation_first + index];
    if (relocation->offset < end &&
        (relocation->offset >= offset ||
         offset - relocation->offset < 4u)) {
      return CTOOL_TRUE;
    }
  }
  return CTOOL_FALSE;
}

static ctool_status_t ld_add_file_section(ld_context_t *link,
                                          ctool_u32 object_index,
                                          ctool_u32 section_index,
                                          ctool_u32 output_index) {
  ld_object_t *object = &link->objects[object_index];
  const ctool_elf32_section_t *section =
      &object->object.sections[section_index];
  ld_section_map_t *mapping = &object->section_maps[section_index];
  ld_output_section_t *output = &link->outputs[output_index];
  ctool_u32 aligned_size;
  ctool_u32 section_alignment =
      section->alignment == 0u ? 1u : section->alignment;
  ctool_status_t status;
  ctool_u32 effective_type =
      section->type == (ctool_u32)CTOOL_ELF32_SHT_NOBITS
          ? (ctool_u32)CTOOL_ELF32_SHT_NOBITS
          : (ctool_u32)CTOOL_ELF32_SHT_PROGBITS;
  if (mapping->matched == CTOOL_TRUE ||
      (section->flags & CTOOL_ELF32_SHF_ALLOC) == 0u ||
      (section->flags & CTOOL_ELF32_SHF_MERGE) != 0u) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  if (output->type != 0u && output->type != effective_type) {
    return ld_diagnostic(link->job, CTOOL_LD_DIAG_BAD_LAYOUT,
                         object->source->path.text, 0u, section_index,
                         "CupidLD output section mixes file and NOBITS data",
                         CTOOL_ERR_INPUT);
  }
  if (effective_type == (ctool_u32)CTOOL_ELF32_SHT_PROGBITS &&
      section->contents.size != section->size) {
    return ld_diagnostic(link->job, CTOOL_LD_DIAG_UNSUPPORTED_INPUT,
                         object->source->path.text, 0u, section_index,
                         "CupidLD allocated section has no file contents",
                         CTOOL_ERR_UNSUPPORTED);
  }
  if (link->request->layout.kind == CTOOL_LD_LAYOUT_SCRIPT) {
    status = ld_raise_script_output_alignment(link, output_index,
                                              section_alignment);
    if (status != CTOOL_OK) {
      return status;
    }
  } else if (section_alignment > output->alignment) {
    output->alignment = section_alignment;
  }
  if (ld_add_overflows(output->address, output->size) == CTOOL_TRUE) {
    return CTOOL_ERR_OVERFLOW;
  }
  status = ld_align_value(output->address + output->size, section_alignment,
                          &aligned_size);
  if (status != CTOOL_OK) {
    return status;
  }
  aligned_size -= output->address;
  output->type = effective_type;
  output->flags |= section->flags;
  mapping->matched = CTOOL_TRUE;
  mapping->output_index = output_index;
  mapping->base_offset = aligned_size;
  if (effective_type == (ctool_u32)CTOOL_ELF32_SHT_NOBITS) {
    if (ld_add_overflows(aligned_size, section->size) == CTOOL_TRUE) {
      return CTOOL_ERR_OVERFLOW;
    }
    output->size = aligned_size + section->size;
    return CTOOL_OK;
  }
  status = ld_payload_pad_to(link, output,
                             output->staging_offset + aligned_size,
                             output->flags);
  if (status == CTOOL_ERR_INTERNAL) {
    return ld_diagnostic(
        link->job, CTOOL_LD_DIAG_BAD_LAYOUT, object->source->path.text, 0u,
        section_index,
        "CupidLD output-section staging offsets overlap",
        CTOOL_ERR_INTERNAL);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  status = ctool_buffer_append(link->payload, section->contents);
  if (status != CTOOL_OK) {
    return status;
  }
  if (ld_add_overflows(aligned_size, section->size) == CTOOL_TRUE) {
    return CTOOL_ERR_OVERFLOW;
  }
  output->size = aligned_size + section->size;
  output->file_size = output->size;
  return CTOOL_OK;
}

typedef ctool_bool (*ld_section_selector_t)(
    const ctool_elf32_section_t *section, const void *context);

static ctool_bool ld_same_merge_group(
    const ctool_elf32_section_t *left,
    const ctool_elf32_section_t *right) {
  return left->entry_size == right->entry_size &&
                 ((left->flags & CTOOL_ELF32_SHF_STRINGS) != 0u) ==
                     ((right->flags & CTOOL_ELF32_SHF_STRINGS) != 0u)
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t ld_add_selected_merge_group(
    ld_context_t *link, ctool_u32 output_index, ctool_u32 seed_object,
    ctool_u32 seed_section, ld_section_selector_t selector,
    const void *selector_context) {
  const ctool_elf32_section_t *seed =
      &link->objects[seed_object].object.sections[seed_section];
  ld_output_section_t *output = &link->outputs[output_index];
  ctool_u32 group_alignment = 1u;
  ctool_u32 object_index;
  ctool_u32 aligned_size;
  ctool_status_t status;
  for (object_index = 0u; object_index < link->object_count; object_index++) {
    const ld_object_t *object = &link->objects[object_index];
    ctool_u32 section_index;
    for (section_index = 1u; section_index < object->object.section_count;
         section_index++) {
      const ctool_elf32_section_t *section =
          &object->object.sections[section_index];
      ctool_u32 alignment = section->alignment == 0u ? 1u
                                                      : section->alignment;
      if (object->section_maps[section_index].matched == CTOOL_FALSE &&
          (section->flags & CTOOL_ELF32_SHF_ALLOC) != 0u &&
          (section->flags & CTOOL_ELF32_SHF_MERGE) != 0u &&
          selector(section, selector_context) == CTOOL_TRUE &&
          ld_same_merge_group(section, seed) == CTOOL_TRUE) {
        if (section->type != (ctool_u32)CTOOL_ELF32_SHT_PROGBITS ||
            section->contents.size != section->size) {
          return ld_diagnostic(
              link->job, CTOOL_LD_DIAG_UNSUPPORTED_INPUT,
              object->source->path.text, 0u, section_index,
              "CupidLD merge section is not file-backed data",
              CTOOL_ERR_UNSUPPORTED);
        }
        if (alignment > group_alignment) {
          group_alignment = alignment;
        }
      }
    }
  }
  if (link->request->layout.kind == CTOOL_LD_LAYOUT_SCRIPT) {
    status = ld_raise_script_output_alignment(link, output_index,
                                              group_alignment);
    if (status != CTOOL_OK) {
      return status;
    }
  } else if (group_alignment > output->alignment) {
    output->alignment = group_alignment;
  }
  if (output->type != 0u &&
      output->type != (ctool_u32)CTOOL_ELF32_SHT_PROGBITS) {
    return ld_diagnostic(link->job, CTOOL_LD_DIAG_BAD_LAYOUT,
                         ctool_string(""), 0u, 0u,
                         "CupidLD output section mixes file and NOBITS data",
                         CTOOL_ERR_INPUT);
  }
  if (ld_add_overflows(output->address, output->size) == CTOOL_TRUE) {
    return CTOOL_ERR_OVERFLOW;
  }
  status = ld_align_value(output->address + output->size, group_alignment,
                          &aligned_size);
  if (status != CTOOL_OK) {
    return status;
  }
  aligned_size -= output->address;
  status = ld_payload_pad_to(link, output,
                             output->staging_offset + aligned_size,
                             output->flags | seed->flags);
  if (status != CTOOL_OK) {
    return status;
  }
  output->size = aligned_size;
  output->type = (ctool_u32)CTOOL_ELF32_SHT_PROGBITS;
  for (object_index = 0u; object_index < link->object_count; object_index++) {
    ld_object_t *object = &link->objects[object_index];
    ctool_u32 section_index;
    for (section_index = 1u; section_index < object->object.section_count;
         section_index++) {
      const ctool_elf32_section_t *section =
          &object->object.sections[section_index];
      ld_section_map_t *mapping = &object->section_maps[section_index];
      ctool_u32 source_offset = 0u;
      ctool_u32 mapped_end = output->size;
      if (mapping->matched == CTOOL_TRUE ||
          (section->flags & CTOOL_ELF32_SHF_ALLOC) == 0u ||
          (section->flags & CTOOL_ELF32_SHF_MERGE) == 0u ||
          selector(section, selector_context) == CTOOL_FALSE ||
          ld_same_merge_group(section, seed) == CTOOL_FALSE) {
        continue;
      }
      mapping->matched = CTOOL_TRUE;
      mapping->output_index = output_index;
      mapping->base_offset = output->size;
      output->flags |= section->flags;
      if (ld_add_overflows(section->size, 1u) == CTOOL_TRUE) {
        return CTOOL_ERR_OVERFLOW;
      }
      status = ld_alloc_array(link->arena, section->size + 1u,
                              (ctool_u32)sizeof(ctool_u32),
                              (void **)&mapping->offset_map);
      if (status != CTOOL_OK) {
        return status;
      }
      while (source_offset < section->size) {
        ctool_u32 atom_size;
        ctool_u32 atom_index;
        ctool_u32 chosen = LD_NONE;
        ctool_bytes_t payload_view;
        ctool_bool has_relocation;
        status = ld_merge_atom_size(section, source_offset, &atom_size);
        if (status != CTOOL_OK) {
          return ld_diagnostic(link->job, CTOOL_LD_DIAG_UNSUPPORTED_INPUT,
                               object->source->path.text, 0u, section_index,
                               "CupidLD merge section entries are invalid",
                               CTOOL_ERR_INPUT);
        }
        has_relocation = ld_merge_atom_has_relocation(
            object, section_index, source_offset, atom_size);
        payload_view = ctool_buffer_view(link->payload);
        for (atom_index = 0u; atom_index < link->merge_atom_count;
             atom_index++) {
          const ld_merge_atom_t *atom = &link->merge_atoms[atom_index];
          /* Raw bytes are not a complete interning key when either atom will
             be patched.  Keep relocated atoms distinct until relocation
             equivalence is represented explicitly. */
          if (has_relocation == CTOOL_FALSE &&
              atom->has_relocation == CTOOL_FALSE &&
              atom->output_index == output_index &&
              atom->entry_size == section->entry_size &&
              atom->strings ==
                  (((section->flags & CTOOL_ELF32_SHF_STRINGS) != 0u)
                       ? CTOOL_TRUE
                       : CTOOL_FALSE) &&
              atom->size == atom_size &&
              ld_source_equals_payload(
                  section->contents, source_offset, payload_view,
                  output->staging_offset + atom->output_offset,
                  atom_size) == CTOOL_TRUE) {
            chosen = atom->output_offset;
            break;
          }
        }
        if (chosen == LD_NONE) {
          ld_merge_atom_t *atom;
          if (link->merge_atom_count >= link->merge_atom_capacity) {
            return CTOOL_ERR_LIMIT;
          }
          chosen = output->size;
          status = ctool_buffer_append(
              link->payload,
              ctool_bytes(section->contents.data + source_offset,
                          atom_size));
          if (status != CTOOL_OK ||
              ld_add_overflows(output->size, atom_size) == CTOOL_TRUE) {
            return status == CTOOL_OK ? CTOOL_ERR_OVERFLOW : status;
          }
          atom = &link->merge_atoms[link->merge_atom_count++];
          atom->output_index = output_index;
          atom->entry_size = section->entry_size;
          atom->size = atom_size;
          atom->output_offset = chosen;
          atom->strings =
              (section->flags & CTOOL_ELF32_SHF_STRINGS) != 0u
                  ? CTOOL_TRUE
                  : CTOOL_FALSE;
          atom->has_relocation = has_relocation;
          output->size += atom_size;
        }
        for (atom_index = 0u; atom_index < atom_size; atom_index++) {
          mapping->offset_map[source_offset + atom_index] =
              chosen + atom_index;
        }
        mapped_end = chosen + atom_size;
        source_offset += atom_size;
      }
      mapping->offset_map[section->size] = mapped_end;
    }
  }
  output->file_size = output->size;
  return CTOOL_OK;
}

static ctool_status_t ld_add_common(ld_context_t *link,
                                    ctool_u32 output_index) {
  ld_output_section_t *output = &link->outputs[output_index];
  ctool_u32 global_index;
  if (output->type != 0u &&
      output->type != (ctool_u32)CTOOL_ELF32_SHT_NOBITS) {
    return ld_diagnostic(link->job, CTOOL_LD_DIAG_BAD_LAYOUT,
                         ctool_string(""), 0u, 0u,
                         "CupidLD COMMON must be in a NOBITS output section",
                         CTOOL_ERR_INPUT);
  }
  output->type = (ctool_u32)CTOOL_ELF32_SHT_NOBITS;
  output->flags |= CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE;
  for (global_index = 0u; global_index < link->global_count; global_index++) {
    ld_global_t *global = &link->globals[global_index];
    ctool_u32 aligned;
    ctool_u32 absolute_end;
    ctool_status_t status;
    if (global->rank != LD_DEFINITION_COMMON || global->value_ready == CTOOL_TRUE) {
      continue;
    }
    if (ld_add_overflows(output->address, output->size) == CTOOL_TRUE) {
      return CTOOL_ERR_OVERFLOW;
    }
    status = ld_align_value(output->address + output->size,
                            global->common_alignment, &absolute_end);
    if (status != CTOOL_OK || absolute_end < output->address) {
      return CTOOL_ERR_OVERFLOW;
    }
    aligned = absolute_end - output->address;
    if (ld_add_overflows(aligned, global->common_size) == CTOOL_TRUE ||
        ld_add_overflows(output->address, aligned) == CTOOL_TRUE) {
      return CTOOL_ERR_OVERFLOW;
    }
    global->value = output->address + aligned;
    global->value_ready = CTOOL_TRUE;
    global->output_index = output_index;
    global->size = global->common_size;
    global->binding = CTOOL_ELF32_BIND_GLOBAL;
    output->size = aligned + global->common_size;
    if (global->common_alignment > output->alignment &&
        (output->address & (global->common_alignment - 1u)) == 0u) {
      output->alignment = global->common_alignment;
    }
  }
  return CTOOL_OK;
}

static ctool_u32 ld_map_offset(const ld_section_map_t *mapping,
                               ctool_u32 input_offset) {
  return mapping->offset_map != (ctool_u32 *)0
             ? mapping->offset_map[input_offset]
             : mapping->base_offset + input_offset;
}

static ctool_bool ld_fixed_category_matches(
    const ctool_elf32_section_t *section, ctool_u32 category) {
  ctool_bool executable =
      (section->flags & CTOOL_ELF32_SHF_EXECINSTR) != 0u ? CTOOL_TRUE
                                                         : CTOOL_FALSE;
  ctool_bool writable =
      (section->flags & CTOOL_ELF32_SHF_WRITE) != 0u ? CTOOL_TRUE
                                                     : CTOOL_FALSE;
  ctool_bool nobits =
      section->type == (ctool_u32)CTOOL_ELF32_SHT_NOBITS ? CTOOL_TRUE
                                                          : CTOOL_FALSE;
  if ((section->flags & CTOOL_ELF32_SHF_ALLOC) == 0u) {
    return CTOOL_FALSE;
  }
  if (category == 0u) {
    return executable;
  }
  if (category == 1u) {
    return executable == CTOOL_FALSE && writable == CTOOL_FALSE &&
                   nobits == CTOOL_FALSE
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  if (category == 2u) {
    return executable == CTOOL_FALSE && writable == CTOOL_TRUE &&
                   nobits == CTOOL_FALSE
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  return executable == CTOOL_FALSE && nobits == CTOOL_TRUE ? CTOOL_TRUE
                                                            : CTOOL_FALSE;
}

typedef struct {
  ctool_u32 category;
} ld_fixed_selector_t;

static ctool_bool ld_fixed_selector_matches(
    const ctool_elf32_section_t *section, const void *context) {
  const ld_fixed_selector_t *selector =
      (const ld_fixed_selector_t *)context;
  return ld_fixed_category_matches(section, selector->category);
}

static ctool_bool ld_fixed_category_has_input(const ld_context_t *link,
                                              ctool_u32 category) {
  ctool_u32 object_index;
  for (object_index = 0u; object_index < link->object_count; object_index++) {
    const ld_object_t *object = &link->objects[object_index];
    ctool_u32 section_index;
    for (section_index = 1u; section_index < object->object.section_count;
         section_index++) {
      if (ld_fixed_category_matches(&object->object.sections[section_index],
                                    category) == CTOOL_TRUE) {
        return CTOOL_TRUE;
      }
    }
  }
  if (category == 3u) {
    ctool_u32 global_index;
    for (global_index = 0u; global_index < link->global_count; global_index++) {
      if (link->globals[global_index].rank == LD_DEFINITION_COMMON) {
        return CTOOL_TRUE;
      }
    }
  }
  return CTOOL_FALSE;
}

static ctool_status_t ld_layout_fixed(ld_context_t *link) {
  static const char *names[4] = {".text", ".rodata", ".data", ".bss"};
  ctool_u32 category;
  ctool_u32 address = link->request->layout.as.fixed_text.base_address;
  ctool_status_t status;
  link->entry_symbol =
      link->request->layout.as.fixed_text.entry_symbol;
  if (link->entry_symbol.data == (const char *)0 ||
      link->entry_symbol.size == 0u) {
    return ld_diagnostic(link->job, CTOOL_LD_DIAG_BAD_LAYOUT,
                         ctool_string(""), 0u, 0u,
                         "CupidLD fixed layout entry symbol is invalid",
                         CTOOL_ERR_INPUT);
  }
  for (category = 0u; category < 4u; category++) {
    ld_fixed_selector_t selector;
    ctool_u32 output_index;
    ctool_u32 object_index;
    ld_output_section_t *output;
    if (ld_fixed_category_has_input(link, category) == CTOOL_FALSE) {
      continue;
    }
    if (link->output_count != 0u) {
      status = ld_align_value(address, LD_PAGE_SIZE, &address);
      if (status != CTOOL_OK) {
        return status;
      }
    }
    status = ld_begin_output(link, ctool_string(names[category]), address,
                             &output_index);
    if (status != CTOOL_OK) {
      return status;
    }
    output = &link->outputs[output_index];
    selector.category = category;
    for (object_index = 0u; object_index < link->object_count;
         object_index++) {
      const ld_object_t *object = &link->objects[object_index];
      ctool_u32 section_index;
      for (section_index = 1u; section_index < object->object.section_count;
           section_index++) {
        if (object->section_maps[section_index].matched == CTOOL_FALSE &&
            ld_fixed_category_matches(
                &object->object.sections[section_index], category) ==
            CTOOL_TRUE) {
          if ((object->object.sections[section_index].flags &
               CTOOL_ELF32_SHF_MERGE) != 0u) {
            status = ld_add_selected_merge_group(
                link, output_index, object_index, section_index,
                ld_fixed_selector_matches, &selector);
          } else {
            status = ld_add_file_section(link, object_index, section_index,
                                         output_index);
          }
          if (status != CTOOL_OK) {
            return status;
          }
        }
      }
    }
    if (category == 3u) {
      status = ld_add_common(link, output_index);
      if (status != CTOOL_OK) {
        return status;
      }
    }
    if (output->type == 0u) {
      output->type = category == 3u
                         ? (ctool_u32)CTOOL_ELF32_SHT_NOBITS
                         : (ctool_u32)CTOOL_ELF32_SHT_PROGBITS;
    }
    if (ld_add_overflows(output->address, output->size) == CTOOL_TRUE) {
      return CTOOL_ERR_OVERFLOW;
    }
    address = output->address + output->size;
  }
  link->dot = address;
  return CTOOL_OK;
}

static ctool_status_t ld_defined_symbol_value(
    const ld_context_t *link, ctool_u32 object_index, ctool_u32 symbol_index,
    ctool_u32 *value_out, ctool_u32 *output_index_out) {
  const ld_object_t *object;
  const ctool_elf32_symbol_t *symbol;
  const ld_section_map_t *mapping;
  const ld_output_section_t *output;
  ctool_u32 mapped;
  if (object_index >= link->object_count) {
    return CTOOL_ERR_INTERNAL;
  }
  object = &link->objects[object_index];
  if (symbol_index >= object->object.symbol_count) {
    return CTOOL_ERR_INTERNAL;
  }
  symbol = &object->object.symbols[symbol_index];
  if (symbol->placement == CTOOL_ELF32_SYMBOL_ABSOLUTE) {
    *value_out = symbol->value;
    *output_index_out = LD_NONE;
    return CTOOL_OK;
  }
  if (symbol->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
      symbol->section_file_index >= object->object.section_count) {
    return CTOOL_ERR_NOT_FOUND;
  }
  mapping = &object->section_maps[symbol->section_file_index];
  if (mapping->matched == CTOOL_FALSE || mapping->output_index == LD_NONE) {
    return CTOOL_ERR_NOT_FOUND;
  }
  if (symbol->value >
      object->object.sections[symbol->section_file_index].size) {
    return CTOOL_ERR_INPUT;
  }
  output = &link->outputs[mapping->output_index];
  mapped = ld_map_offset(mapping, symbol->value);
  if (mapped > output->size ||
      ld_add_overflows(output->address, mapped) == CTOOL_TRUE) {
    return CTOOL_ERR_OVERFLOW;
  }
  *value_out = output->address + mapped;
  *output_index_out = mapping->output_index;
  return CTOOL_OK;
}

static ctool_status_t ld_compute_global_value(ld_context_t *link,
                                              ctool_u32 global_index) {
  ld_global_t *global = &link->globals[global_index];
  ctool_status_t status;
  if (global->value_ready == CTOOL_TRUE) {
    return CTOOL_OK;
  }
  if (global->rank == LD_DEFINITION_NONE) {
    if (global->required == CTOOL_TRUE) {
      const ld_object_t *first = &link->objects[global->first_object];
      return ld_diagnostic(link->job, CTOOL_LD_DIAG_UNDEFINED_SYMBOL,
                           first->source->path.text, 0u,
                           global->first_symbol,
                           "CupidLD found an unresolved strong symbol",
                           CTOOL_ERR_INPUT);
    }
    global->value = 0u;
    global->output_index = LD_NONE;
    global->value_ready = CTOOL_TRUE;
    return CTOOL_OK;
  }
  if (global->rank == LD_DEFINITION_COMMON) {
    return ld_diagnostic(link->job, CTOOL_LD_DIAG_UNMATCHED_SECTION,
                         ctool_string(""), 0u, 0u,
                         "CupidLD layout did not place COMMON storage",
                         CTOOL_ERR_INPUT);
  }
  status = ld_defined_symbol_value(
      link, global->winner_object, global->winner_symbol, &global->value,
      &global->output_index);
  if (status != CTOOL_OK) {
    const ld_object_t *winner = &link->objects[global->winner_object];
    return ld_diagnostic(link->job, CTOOL_LD_DIAG_UNMATCHED_SECTION,
                         winner->source->path.text, 0u,
                         global->winner_symbol,
                         "CupidLD winning symbol belongs to an unmatched section",
                         CTOOL_ERR_INPUT);
  }
  global->value_ready = CTOOL_TRUE;
  return CTOOL_OK;
}

static ctool_status_t ld_finalize_globals(ld_context_t *link) {
  ctool_u32 global_index;
  for (global_index = 0u; global_index < link->global_count; global_index++) {
    ctool_status_t status = ld_compute_global_value(link, global_index);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  return CTOOL_OK;
}

static ctool_status_t ld_symbol_value(ld_context_t *link,
                                      ctool_u32 object_index,
                                      ctool_u32 symbol_index,
                                      ctool_u32 *value_out) {
  const ld_object_t *object = &link->objects[object_index];
  const ctool_elf32_symbol_t *symbol;
  ctool_u32 ignored_output;
  if (symbol_index >= object->object.symbol_count) {
    return CTOOL_ERR_INPUT;
  }
  symbol = &object->object.symbols[symbol_index];
  if (symbol->binding != CTOOL_ELF32_BIND_LOCAL) {
    ctool_u32 global_index;
    ctool_status_t status =
        ld_find_global(link, symbol->name, CTOOL_FALSE, &global_index);
    if (status != CTOOL_OK) {
      return CTOOL_ERR_INTERNAL;
    }
    status = ld_compute_global_value(link, global_index);
    if (status != CTOOL_OK) {
      return status;
    }
    *value_out = link->globals[global_index].value;
    return CTOOL_OK;
  }
  return ld_defined_symbol_value(link, object_index, symbol_index, value_out,
                                 &ignored_output);
}

static ctool_status_t ld_relocated_symbol_value(
    ld_context_t *link, ctool_u32 object_index, ctool_u32 symbol_index,
    ctool_i32 addend, ctool_u32 *value_out) {
  const ctool_elf32_symbol_t *reference =
      &link->objects[object_index].object.symbols[symbol_index];
  ctool_u32 definition_object = object_index;
  ctool_u32 definition_symbol = symbol_index;
  const ctool_elf32_symbol_t *definition;
  const ld_section_map_t *mapping;
  ctool_u32 combined;
  ctool_bool combined_valid = CTOOL_TRUE;
  ctool_status_t status;
  if (reference->binding != CTOOL_ELF32_BIND_LOCAL) {
    ctool_u32 global_index;
    status = ld_find_global(link, reference->name, CTOOL_FALSE,
                            &global_index);
    if (status == CTOOL_OK &&
        link->globals[global_index].winner_object != LD_NONE &&
        link->globals[global_index].winner_symbol != LD_NONE) {
      definition_object = link->globals[global_index].winner_object;
      definition_symbol = link->globals[global_index].winner_symbol;
    }
  }
  definition =
      &link->objects[definition_object].object.symbols[definition_symbol];
  /* A section-symbol addend selects an input-section byte, so map S+A as one
     offset. Named symbols obey ordinary ELF S+A semantics: map S, then add A. */
  if (definition->type == CTOOL_ELF32_SYMBOL_SECTION &&
      definition->placement == CTOOL_ELF32_SYMBOL_DEFINED &&
      definition->section_file_index <
          link->objects[definition_object].object.section_count) {
    mapping = &link->objects[definition_object]
                   .section_maps[definition->section_file_index];
    if (mapping->offset_map != (ctool_u32 *)0) {
      if (addend < 0) {
        ctool_u32 magnitude = 0u - (ctool_u32)addend;
        if (magnitude > definition->value) {
          combined_valid = CTOOL_FALSE;
        } else {
          combined = definition->value - magnitude;
        }
      } else if (ld_add_overflows(definition->value,
                                  (ctool_u32)addend) == CTOOL_TRUE) {
        combined_valid = CTOOL_FALSE;
      } else {
        combined = definition->value + (ctool_u32)addend;
      }
      if (combined_valid == CTOOL_TRUE &&
          combined <= link->objects[definition_object]
                          .object.sections[definition->section_file_index]
                          .size) {
        const ld_output_section_t *output =
            &link->outputs[mapping->output_index];
        ctool_u32 mapped = ld_map_offset(mapping, combined);
        if (mapped > output->size ||
            ld_add_overflows(output->address, mapped) == CTOOL_TRUE) {
          return CTOOL_ERR_OVERFLOW;
        }
        *value_out = output->address + mapped;
        return CTOOL_OK;
      }
    }
  }
  status = ld_symbol_value(link, object_index, symbol_index, value_out);
  if (status == CTOOL_OK) {
    *value_out += (ctool_u32)addend;
  }
  return status;
}

static ctool_status_t ld_apply_relocations(ld_context_t *link) {
  ctool_u32 object_index;
  for (object_index = 0u; object_index < link->object_count; object_index++) {
    const ld_object_t *object = &link->objects[object_index];
    ctool_u32 relocation_index;
    for (relocation_index = 0u;
         relocation_index < object->object.relocation_count;
         relocation_index++) {
      const ctool_elf32_relocation_t *relocation =
          &object->object.relocations[relocation_index];
      const ctool_elf32_section_t *target;
      const ld_section_map_t *mapping;
      const ld_output_section_t *output;
      ctool_u32 target_offset;
      ctool_u32 symbol_value;
      ctool_u32 place;
      ctool_u32 value;
      ctool_status_t status;
      if (relocation->target_section_file_index >=
          object->object.section_count) {
        return CTOOL_ERR_INPUT;
      }
      target =
          &object->object.sections[relocation->target_section_file_index];
      if ((target->flags & CTOOL_ELF32_SHF_ALLOC) == 0u) {
        continue;
      }
      mapping =
          &object->section_maps[relocation->target_section_file_index];
      if (mapping->matched == CTOOL_FALSE ||
          mapping->output_index == LD_NONE) {
        return ld_diagnostic(link->job, CTOOL_LD_DIAG_UNMATCHED_SECTION,
                             object->source->path.text, 0u,
                             relocation_index,
                             "CupidLD relocation target is unmatched",
                             CTOOL_ERR_INPUT);
      }
      if ((relocation->type != CTOOL_ELF32_R_386_32 &&
           relocation->type != CTOOL_ELF32_R_386_PC32) ||
          relocation->addend_known == CTOOL_FALSE) {
        return ld_diagnostic(link->job,
                             CTOOL_LD_DIAG_UNSUPPORTED_RELOCATION,
                             object->source->path.text, 0u,
                             relocation_index,
                             "CupidLD relocation type is unsupported",
                             CTOOL_ERR_UNSUPPORTED);
      }
      if (relocation->offset > target->size ||
          target->size - relocation->offset < 4u) {
        return CTOOL_ERR_INPUT;
      }
      output = &link->outputs[mapping->output_index];
      if (output->type != (ctool_u32)CTOOL_ELF32_SHT_PROGBITS) {
        return CTOOL_ERR_INPUT;
      }
      target_offset = ld_map_offset(mapping, relocation->offset);
      if (target_offset > output->file_size ||
          output->file_size - target_offset < 4u) {
        return CTOOL_ERR_INPUT;
      }
      if (mapping->offset_map != (ctool_u32 *)0 &&
          (ld_map_offset(mapping, relocation->offset + 1u) !=
               target_offset + 1u ||
           ld_map_offset(mapping, relocation->offset + 2u) !=
               target_offset + 2u ||
           ld_map_offset(mapping, relocation->offset + 3u) !=
               target_offset + 3u)) {
        return ld_diagnostic(link->job,
                             CTOOL_LD_DIAG_UNSUPPORTED_RELOCATION,
                             object->source->path.text, 0u,
                             relocation_index,
                             "CupidLD relocation crosses merged entries",
                             CTOOL_ERR_UNSUPPORTED);
      }
      status = ld_relocated_symbol_value(
          link, object_index, relocation->symbol_file_index,
          relocation->addend, &symbol_value);
      if (status != CTOOL_OK ||
          ld_add_overflows(output->address, target_offset) == CTOOL_TRUE) {
        return status != CTOOL_OK ? status : CTOOL_ERR_OVERFLOW;
      }
      place = output->address + target_offset;
      value = symbol_value;
      if (relocation->type == CTOOL_ELF32_R_386_PC32) {
        value -= place;
      }
      status = ctool_buffer_patch_le32(
          link->payload, output->staging_offset + target_offset, value);
      if (status != CTOOL_OK) {
        return status;
      }
      link->applied_relocations++;
    }
  }
  return CTOOL_OK;
}

static ctool_status_t ld_append_string(ctool_buffer_t *table,
                                       ctool_string_t value,
                                       ctool_u32 *offset_out) {
  ctool_status_t status;
  *offset_out = ctool_buffer_view(table).size;
  status = ctool_buffer_append(table, ctool_bytes(value.data, value.size));
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_u8(table, 0u);
  }
  return status;
}

static ctool_status_t ld_put_symbol(ctool_buffer_t *symtab,
                                    ctool_u32 name_offset, ctool_u32 value,
                                    ctool_u32 size, ctool_u32 binding,
                                    ctool_u32 type, ctool_u32 visibility,
                                    ctool_u32 section_index) {
  ctool_status_t status = ctool_buffer_put_le32(symtab, name_offset);
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(symtab, value);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(symtab, size);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_u8(
        symtab, (ctool_u8)(((binding & 0x0fu) << 4u) | (type & 0x0fu)));
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_u8(symtab, (ctool_u8)(visibility & 0x03u));
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le16(symtab, (ctool_u16)section_index);
  }
  return status;
}

static ctool_status_t ld_build_symbol_tables(
    ld_context_t *link, ctool_buffer_t *symtab, ctool_buffer_t *strtab,
    ctool_u32 *local_count_out, ctool_u32 *symbol_count_out) {
  ctool_u32 object_index;
  ctool_u32 local_count = 0u;
  ctool_u32 symbol_count = 0u;
  ctool_status_t status = ctool_buffer_put_u8(strtab, 0u);
  if (status == CTOOL_OK) {
    status = ctool_buffer_fill(symtab, 0u, LD_SYMBOL_SIZE);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  for (object_index = 0u; object_index < link->object_count; object_index++) {
    const ld_object_t *object = &link->objects[object_index];
    ctool_u32 symbol_index;
    for (symbol_index = 1u; symbol_index < object->object.symbol_count;
         symbol_index++) {
      const ctool_elf32_symbol_t *symbol =
          &object->object.symbols[symbol_index];
      ctool_u32 value;
      ctool_u32 output_index;
      ctool_u32 name_offset;
      if (symbol->binding != CTOOL_ELF32_BIND_LOCAL ||
          (symbol->type != CTOOL_ELF32_SYMBOL_FUNCTION &&
           symbol->type != CTOOL_ELF32_SYMBOL_NOTYPE) ||
          symbol->placement != CTOOL_ELF32_SYMBOL_DEFINED) {
        continue;
      }
      status = ld_defined_symbol_value(link, object_index, symbol_index,
                                       &value, &output_index);
      if (status == CTOOL_ERR_NOT_FOUND) {
        continue;
      }
      if (status != CTOOL_OK) {
        return status;
      }
      if (symbol->type == CTOOL_ELF32_SYMBOL_NOTYPE &&
          (link->outputs[output_index].flags &
           CTOOL_ELF32_SHF_EXECINSTR) == 0u) {
        continue;
      }
      status = ld_append_string(strtab, symbol->name, &name_offset);
      if (status == CTOOL_OK) {
        status = ld_put_symbol(symtab, name_offset, value, symbol->size,
                               CTOOL_ELF32_BIND_LOCAL, symbol->type,
                               symbol->visibility, output_index + 1u);
      }
      if (status != CTOOL_OK) {
        return status;
      }
      local_count++;
      symbol_count++;
    }
  }
  {
    ctool_u32 global_index;
    for (global_index = 0u; global_index < link->global_count; global_index++) {
      const ld_global_t *global = &link->globals[global_index];
      ctool_u32 name_offset;
      ctool_u32 section_index;
      ctool_u32 binding;
      ctool_u32 type;
      ctool_u32 visibility;
      if (global->rank == LD_DEFINITION_NONE ||
          global->value_ready == CTOOL_FALSE) {
        continue;
      }
      section_index = global->output_index == LD_NONE
                          ? LD_SHN_ABS
                          : global->output_index + 1u;
      binding = global->script_symbol == CTOOL_TRUE
                    ? (ctool_u32)CTOOL_ELF32_BIND_GLOBAL
                    : global->binding;
      type = global->script_symbol == CTOOL_TRUE
                 ? (ctool_u32)CTOOL_ELF32_SYMBOL_NOTYPE
                 : global->type;
      visibility = global->script_symbol == CTOOL_TRUE
                       ? (ctool_u32)CTOOL_ELF32_VIS_DEFAULT
                       : global->visibility;
      status = ld_append_string(strtab, global->name, &name_offset);
      if (status == CTOOL_OK) {
        status = ld_put_symbol(symtab, name_offset, global->value,
                               global->size, binding, type, visibility,
                               section_index);
      }
      if (status != CTOOL_OK) {
        return status;
      }
      symbol_count++;
    }
  }
  *local_count_out = local_count;
  *symbol_count_out = symbol_count;
  return CTOOL_OK;
}

static ctool_status_t ld_congruent_offset(ctool_u32 cursor,
                                          ctool_u32 address,
                                          ctool_u32 *offset_out) {
  ctool_u32 addition = (address - cursor) & (LD_PAGE_SIZE - 1u);
  if (ld_add_overflows(cursor, addition) == CTOOL_TRUE) {
    return CTOOL_ERR_OVERFLOW;
  }
  *offset_out = cursor + addition;
  return CTOOL_OK;
}

static ctool_status_t ld_put_elf_header(
    ctool_buffer_t *output, ctool_u32 entry, ctool_u32 section_headers,
    ctool_u32 program_count, ctool_u32 section_count,
    ctool_u32 shstrtab_index) {
  static const ctool_u8 ident[16] = {0x7fu, (ctool_u8)'E', (ctool_u8)'L',
                                     (ctool_u8)'F', 1u, 1u, 1u, 0u,
                                     0u,    0u,            0u,  0u,
                                     0u,    0u,            0u,  0u};
  ctool_status_t status = ctool_buffer_append(output, ctool_bytes(ident, 16u));
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le16(output, 2u);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le16(output, 3u);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(output, 1u);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(output, entry);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(output, LD_ELF_HEADER_SIZE);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(output, section_headers);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(output, 0u);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le16(output, (ctool_u16)LD_ELF_HEADER_SIZE);
  }
  if (status == CTOOL_OK) {
    status =
        ctool_buffer_put_le16(output, (ctool_u16)LD_PROGRAM_HEADER_SIZE);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le16(output, (ctool_u16)program_count);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le16(output, (ctool_u16)LD_SECTION_HEADER_SIZE);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le16(output, (ctool_u16)section_count);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le16(output, (ctool_u16)shstrtab_index);
  }
  return status;
}

static ctool_status_t ld_put_program_header(
    ctool_buffer_t *output, const ld_output_section_t *section) {
  ctool_u32 flags = CTOOL_ELF32_PF_R;
  ctool_status_t status;
  if ((section->flags & CTOOL_ELF32_SHF_WRITE) != 0u) {
    flags |= CTOOL_ELF32_PF_W;
  }
  if ((section->flags & CTOOL_ELF32_SHF_EXECINSTR) != 0u) {
    flags |= CTOOL_ELF32_PF_X;
  }
  status = ctool_buffer_put_le32(output, CTOOL_ELF32_PT_LOAD);
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(output, section->file_offset);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(output, section->address);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(output, section->address);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(output, section->file_size);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(output, section->size);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(output, flags);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(output, LD_PAGE_SIZE);
  }
  return status;
}

static ctool_status_t ld_put_gnu_stack_header(ctool_buffer_t *output) {
  ctool_status_t status = ctool_buffer_put_le32(output, LD_PT_GNU_STACK);
  ctool_u32 field;
  for (field = 0u; status == CTOOL_OK && field < 5u; field++) {
    status = ctool_buffer_put_le32(output, 0u);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(output,
                                   CTOOL_ELF32_PF_R | CTOOL_ELF32_PF_W);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(output, 0u);
  }
  return status;
}

static ctool_status_t ld_put_section_header(
    ctool_buffer_t *output, ctool_u32 name, ctool_u32 type, ctool_u32 flags,
    ctool_u32 address, ctool_u32 file_offset, ctool_u32 size, ctool_u32 link,
    ctool_u32 info, ctool_u32 alignment, ctool_u32 entry_size) {
  ctool_status_t status = ctool_buffer_put_le32(output, name);
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(output, type);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(output, flags);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(output, address);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(output, file_offset);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(output, size);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(output, link);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(output, info);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(output, alignment);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(output, entry_size);
  }
  return status;
}

static ctool_status_t ld_find_entry(ld_context_t *link,
                                    ctool_u32 *entry_out) {
  ctool_u32 global_index;
  ctool_status_t status = ld_find_global(link, link->entry_symbol, CTOOL_FALSE,
                                         &global_index);
  const ld_global_t *global;
  const ld_output_section_t *output;
  if (status != CTOOL_OK) {
    return ld_diagnostic(link->job, CTOOL_LD_DIAG_BAD_ENTRY,
                         ctool_string(""), 0u, 0u,
                         "CupidLD entry symbol is missing", CTOOL_ERR_INPUT);
  }
  status = ld_compute_global_value(link, global_index);
  if (status != CTOOL_OK) {
    return status;
  }
  global = &link->globals[global_index];
  if (global->output_index == LD_NONE ||
      global->output_index >= link->output_count) {
    return ld_diagnostic(link->job, CTOOL_LD_DIAG_BAD_ENTRY,
                         ctool_string(""), 0u, 0u,
                         "CupidLD entry is not in an output section",
                         CTOOL_ERR_INPUT);
  }
  output = &link->outputs[global->output_index];
  if ((output->flags & CTOOL_ELF32_SHF_EXECINSTR) == 0u ||
      output->type != (ctool_u32)CTOOL_ELF32_SHT_PROGBITS ||
      global->value < output->address ||
      global->value - output->address >= output->file_size) {
    return ld_diagnostic(
        link->job, CTOOL_LD_DIAG_BAD_ENTRY, ctool_string(""), 0u, 0u,
        "CupidLD entry is not file-backed executable code", CTOOL_ERR_INPUT);
  }
  *entry_out = global->value;
  return CTOOL_OK;
}

static ctool_status_t ld_serialize_exec(ld_context_t *link,
                                        ctool_buffer_t *output,
                                        ctool_ld_result_t *result_out) {
  ctool_buffer_t *symtab = (ctool_buffer_t *)0;
  ctool_buffer_t *strtab = (ctool_buffer_t *)0;
  ctool_buffer_t *shstrtab = (ctool_buffer_t *)0;
  ctool_u32 entry;
  ctool_u32 local_count;
  ctool_u32 symbol_count;
  ctool_u32 symtab_name;
  ctool_u32 strtab_name;
  ctool_u32 shstrtab_name;
  ctool_u32 cursor;
  ctool_u32 symtab_offset;
  ctool_u32 strtab_offset;
  ctool_u32 shstrtab_offset;
  ctool_u32 section_headers;
  ctool_u32 section_count;
  ctool_u32 program_count;
  ctool_u32 load_address = LD_U32_MAX;
  ctool_u32 loaded_end = 0u;
  ctool_u32 memory_end = 0u;
  ctool_u32 index;
  ctool_u32 diagnostics_before = ctool_job_diagnostic_count(link->job);
  const char *failure_phase = "CupidLD entry serialization failed";
  ctool_status_t status = ld_find_entry(link, &entry);
  if (status != CTOOL_OK) {
    goto done;
  }
  failure_phase = "CupidLD metadata buffer setup failed";
  status = ctool_job_open_buffer(
      link->job,
      link->request->maximum_image_span < 256u
          ? link->request->maximum_image_span
          : 256u,
                                 link->request->maximum_image_span, &symtab);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(
        link->job,
        link->request->maximum_image_span < 256u
            ? link->request->maximum_image_span
            : 256u,
                                   link->request->maximum_image_span,
                                   &strtab);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(
        link->job,
        link->request->maximum_image_span < 256u
            ? link->request->maximum_image_span
            : 256u,
                                   link->request->maximum_image_span,
                                   &shstrtab);
  }
  if (status != CTOOL_OK) {
    goto done;
  }
  failure_phase = "CupidLD section-name construction failed";
  status = ctool_buffer_put_u8(shstrtab, 0u);
  for (index = 0u; status == CTOOL_OK && index < link->output_count; index++) {
    status = ld_append_string(shstrtab, link->outputs[index].name,
                              &link->outputs[index].name_offset);
  }
  failure_phase = "CupidLD symbol-table construction failed";
  if (status == CTOOL_OK) {
    status = ld_append_string(shstrtab, ctool_string(".symtab"),
                              &symtab_name);
  }
  if (status == CTOOL_OK) {
    status = ld_append_string(shstrtab, ctool_string(".strtab"),
                              &strtab_name);
  }
  if (status == CTOOL_OK) {
    status = ld_append_string(shstrtab, ctool_string(".shstrtab"),
                              &shstrtab_name);
  }
  if (status == CTOOL_OK) {
    status = ld_build_symbol_tables(link, symtab, strtab, &local_count,
                                    &symbol_count);
  }
  if (status != CTOOL_OK) {
    goto done;
  }
  program_count = link->output_count + 1u;
  if (link->output_count >= 0xfffcu ||
      ld_multiply_overflows(program_count, LD_PROGRAM_HEADER_SIZE) ==
          CTOOL_TRUE ||
      ld_add_overflows(LD_ELF_HEADER_SIZE,
                       program_count * LD_PROGRAM_HEADER_SIZE) ==
          CTOOL_TRUE) {
    status = CTOOL_ERR_OVERFLOW;
    goto done;
  }
  failure_phase = "CupidLD executable file layout failed";
  cursor = LD_ELF_HEADER_SIZE +
           program_count * LD_PROGRAM_HEADER_SIZE;
  for (index = 0u; index < link->output_count; index++) {
    ld_output_section_t *section = &link->outputs[index];
    ctool_u32 end;
    status = ld_congruent_offset(cursor, section->address,
                                 &section->file_offset);
    if (status != CTOOL_OK ||
        ld_add_overflows(section->address, section->size) == CTOOL_TRUE) {
      status = CTOOL_ERR_OVERFLOW;
      goto done;
    }
    end = section->address + section->size;
    if (section->address < load_address) {
      load_address = section->address;
    }
    if (end > memory_end) {
      memory_end = end;
    }
    if (section->type == (ctool_u32)CTOOL_ELF32_SHT_PROGBITS) {
      if (ld_add_overflows(section->file_offset, section->file_size) ==
          CTOOL_TRUE) {
        status = CTOOL_ERR_OVERFLOW;
        goto done;
      }
      cursor = section->file_offset + section->file_size;
      if (end > loaded_end) {
        loaded_end = end;
      }
    } else {
      cursor = section->file_offset;
    }
  }
  if (load_address == LD_U32_MAX || memory_end <= load_address ||
      memory_end - load_address > link->request->maximum_image_span) {
    status = ld_diagnostic(link->job, CTOOL_LD_DIAG_LIMIT,
                           ctool_string(""), 0u, 0u,
                           "CupidLD image exceeds the requested maximum span",
                           CTOOL_ERR_LIMIT);
    goto done;
  }
  status = ld_align_value(cursor, 4u, &symtab_offset);
  if (status == CTOOL_OK &&
      ld_add_overflows(symtab_offset, ctool_buffer_view(symtab).size) ==
          CTOOL_FALSE) {
    strtab_offset = symtab_offset + ctool_buffer_view(symtab).size;
  } else {
    status = CTOOL_ERR_OVERFLOW;
    goto done;
  }
  if (ld_add_overflows(strtab_offset, ctool_buffer_view(strtab).size) ==
      CTOOL_TRUE) {
    status = CTOOL_ERR_OVERFLOW;
    goto done;
  }
  shstrtab_offset = strtab_offset + ctool_buffer_view(strtab).size;
  if (ld_add_overflows(shstrtab_offset, ctool_buffer_view(shstrtab).size) ==
      CTOOL_TRUE) {
    status = CTOOL_ERR_OVERFLOW;
    goto done;
  }
  status = ld_align_value(shstrtab_offset + ctool_buffer_view(shstrtab).size,
                          4u, &section_headers);
  if (status != CTOOL_OK) {
    goto done;
  }
  section_count = link->output_count + 4u;
  failure_phase = "CupidLD executable header write failed";
  status = ld_put_elf_header(output, entry, section_headers,
                             program_count, section_count,
                             section_count - 1u);
  failure_phase = "CupidLD program-header write failed";
  for (index = 0u; status == CTOOL_OK && index < link->output_count; index++) {
    status = ld_put_program_header(output, &link->outputs[index]);
  }
  if (status == CTOOL_OK) {
    status = ld_put_gnu_stack_header(output);
  }
  failure_phase = "CupidLD output-section write failed";
  for (index = 0u; status == CTOOL_OK && index < link->output_count; index++) {
    const ld_output_section_t *section = &link->outputs[index];
    if (section->type == (ctool_u32)CTOOL_ELF32_SHT_PROGBITS) {
      ctool_bytes_t payload = ctool_buffer_view(link->payload);
      status = ld_buffer_pad_to(output, section->file_offset);
      if (status == CTOOL_ERR_INTERNAL) {
        status = ld_diagnostic(
            link->job, CTOOL_LD_DIAG_OVERFLOW, ctool_string(""), 0u, index,
            "CupidLD output-section file offsets overlap",
            CTOOL_ERR_INTERNAL);
      }
      if (status == CTOOL_OK) {
        status = ctool_buffer_append(
            output,
            ctool_bytes(payload.data + section->staging_offset,
                        section->file_size));
      }
    }
  }
  failure_phase = "CupidLD metadata write failed";
  if (status == CTOOL_OK) {
    status = ld_buffer_pad_to(output, symtab_offset);
    if (status == CTOOL_ERR_INTERNAL) {
      status = ld_diagnostic(link->job, CTOOL_LD_DIAG_OVERFLOW,
                             ctool_string(""), 0u, 0u,
                             "CupidLD symbol table overlaps section data",
                             CTOOL_ERR_INTERNAL);
    }
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_append(output, ctool_buffer_view(symtab));
  }
  if (status == CTOOL_OK) {
    status = ld_buffer_pad_to(output, strtab_offset);
    if (status == CTOOL_ERR_INTERNAL) {
      status = ld_diagnostic(link->job, CTOOL_LD_DIAG_OVERFLOW,
                             ctool_string(""), 0u, 0u,
                             "CupidLD string table overlaps symbol data",
                             CTOOL_ERR_INTERNAL);
    }
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_append(output, ctool_buffer_view(strtab));
  }
  if (status == CTOOL_OK) {
    status = ld_buffer_pad_to(output, shstrtab_offset);
    if (status == CTOOL_ERR_INTERNAL) {
      status = ld_diagnostic(link->job, CTOOL_LD_DIAG_OVERFLOW,
                             ctool_string(""), 0u, 0u,
                             "CupidLD section-name table overlaps string data",
                             CTOOL_ERR_INTERNAL);
    }
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_append(output, ctool_buffer_view(shstrtab));
  }
  if (status == CTOOL_OK) {
    status = ld_buffer_pad_to(output, section_headers);
    if (status == CTOOL_ERR_INTERNAL) {
      status = ld_diagnostic(link->job, CTOOL_LD_DIAG_OVERFLOW,
                             ctool_string(""), 0u, 0u,
                             "CupidLD section headers overlap name data",
                             CTOOL_ERR_INTERNAL);
    }
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_fill(output, 0u, LD_SECTION_HEADER_SIZE);
  }
  failure_phase = "CupidLD section-header write failed";
  for (index = 0u; status == CTOOL_OK && index < link->output_count; index++) {
    const ld_output_section_t *section = &link->outputs[index];
    ctool_u32 flags =
        section->flags &
        ~(CTOOL_ELF32_SHF_MERGE | CTOOL_ELF32_SHF_STRINGS);
    status = ld_put_section_header(
        output, section->name_offset, section->type, flags,
        section->address, section->file_offset, section->size, 0u, 0u,
        section->alignment, 0u);
  }
  if (status == CTOOL_OK) {
    status = ld_put_section_header(
        output, symtab_name, LD_SHT_SYMTAB, 0u, 0u, symtab_offset,
        ctool_buffer_view(symtab).size, link->output_count + 2u,
        local_count + 1u, 4u, LD_SYMBOL_SIZE);
  }
  if (status == CTOOL_OK) {
    status = ld_put_section_header(
        output, strtab_name, LD_SHT_STRTAB, 0u, 0u, strtab_offset,
        ctool_buffer_view(strtab).size, 0u, 0u, 1u, 0u);
  }
  if (status == CTOOL_OK) {
    status = ld_put_section_header(
        output, shstrtab_name, LD_SHT_STRTAB, 0u, 0u, shstrtab_offset,
        ctool_buffer_view(shstrtab).size, 0u, 0u, 1u, 0u);
  }
  if (status == CTOOL_OK) {
    result_out->bytes = ctool_buffer_view(output).size;
    result_out->entry = entry;
    result_out->load_address = load_address;
    result_out->loaded_end = loaded_end;
    result_out->memory_end = memory_end;
    result_out->output_section_count = link->output_count;
    result_out->resolved_symbol_count = symbol_count;
    result_out->applied_relocation_count = link->applied_relocations;
  }

done:
  if (status != CTOOL_OK &&
      ctool_job_diagnostic_count(link->job) == diagnostics_before) {
    (void)ld_diagnostic(link->job, CTOOL_LD_DIAG_LIMIT, ctool_string(""), 0u,
                        0u, failure_phase, status);
  }
  if (shstrtab != (ctool_buffer_t *)0) {
    ctool_buffer_close(shstrtab);
  }
  if (strtab != (ctool_buffer_t *)0) {
    ctool_buffer_close(strtab);
  }
  if (symtab != (ctool_buffer_t *)0) {
    ctool_buffer_close(symtab);
  }
  return status;
}

static ctool_bool ld_name_start(ctool_u8 value) {
  return ((value >= (ctool_u8)'a' && value <= (ctool_u8)'z') ||
          (value >= (ctool_u8)'A' && value <= (ctool_u8)'Z') ||
          value == (ctool_u8)'_' || value == (ctool_u8)'.' ||
          value == (ctool_u8)'$')
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool ld_name_continue(ctool_u8 value) {
  return ld_name_start(value) == CTOOL_TRUE ||
                 (value >= (ctool_u8)'0' && value <= (ctool_u8)'9') ||
                 value == (ctool_u8)'-'
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static void ld_parser_take(ld_parser_t *parser) {
  ctool_u8 value = parser->source->contents.data[parser->offset++];
  if (value == (ctool_u8)'\n') {
    parser->line++;
    parser->column = 1u;
  } else {
    parser->column++;
  }
}

static ctool_status_t ld_parser_error(ld_parser_t *parser,
                                      const char *message) {
  return ld_diagnostic(parser->link->job, CTOOL_LD_DIAG_BAD_LAYOUT,
                       parser->source->path.text, parser->token.line,
                       parser->token.column, message, CTOOL_ERR_INPUT);
}

static ctool_status_t ld_parser_next(ld_parser_t *parser) {
  ctool_bytes_t input = parser->source->contents;
  ctool_u32 start;
  ctool_u8 value;
  ld_zero(&parser->token, (ctool_u32)sizeof(parser->token));
  for (;;) {
    while (parser->offset < input.size) {
      value = input.data[parser->offset];
      if (value != (ctool_u8)' ' && value != (ctool_u8)'\t' &&
          value != (ctool_u8)'\r' && value != (ctool_u8)'\n') {
        break;
      }
      ld_parser_take(parser);
    }
    if (parser->offset + 1u < input.size &&
        input.data[parser->offset] == (ctool_u8)'/' &&
        input.data[parser->offset + 1u] == (ctool_u8)'*') {
      ld_parser_take(parser);
      ld_parser_take(parser);
      while (parser->offset + 1u < input.size &&
             !(input.data[parser->offset] == (ctool_u8)'*' &&
               input.data[parser->offset + 1u] == (ctool_u8)'/')) {
        ld_parser_take(parser);
      }
      if (parser->offset + 1u >= input.size) {
        parser->token.line = parser->line;
        parser->token.column = parser->column;
        return ld_parser_error(parser, "unterminated CupidLD comment");
      }
      ld_parser_take(parser);
      ld_parser_take(parser);
      continue;
    }
    break;
  }
  parser->token.line = parser->line;
  parser->token.column = parser->column;
  if (parser->offset >= input.size) {
    parser->token.kind = LD_TOKEN_EOF;
    return CTOOL_OK;
  }
  value = input.data[parser->offset];
  if (ld_name_start(value) == CTOOL_TRUE) {
    start = parser->offset;
    do {
      ld_parser_take(parser);
    } while (parser->offset < input.size &&
             ld_name_continue(input.data[parser->offset]) == CTOOL_TRUE);
    parser->token.kind = LD_TOKEN_NAME;
    parser->token.text.data = (const char *)(input.data + start);
    parser->token.text.size = parser->offset - start;
    return CTOOL_OK;
  }
  if (value >= (ctool_u8)'0' && value <= (ctool_u8)'9') {
    ctool_u32 number = 0u;
    ctool_u32 base = 10u;
    ctool_bool have_digit = CTOOL_FALSE;
    if (value == (ctool_u8)'0' && parser->offset + 1u < input.size &&
        (input.data[parser->offset + 1u] == (ctool_u8)'x' ||
         input.data[parser->offset + 1u] == (ctool_u8)'X')) {
      base = 16u;
      ld_parser_take(parser);
      ld_parser_take(parser);
    }
    while (parser->offset < input.size) {
      ctool_u8 digit_value = input.data[parser->offset];
      ctool_u32 digit;
      if (digit_value >= (ctool_u8)'0' && digit_value <= (ctool_u8)'9') {
        digit = (ctool_u32)(digit_value - (ctool_u8)'0');
      } else if (base == 16u && digit_value >= (ctool_u8)'a' &&
                 digit_value <= (ctool_u8)'f') {
        digit = 10u + (ctool_u32)(digit_value - (ctool_u8)'a');
      } else if (base == 16u && digit_value >= (ctool_u8)'A' &&
                 digit_value <= (ctool_u8)'F') {
        digit = 10u + (ctool_u32)(digit_value - (ctool_u8)'A');
      } else {
        break;
      }
      if (digit >= base || number > (LD_U32_MAX - digit) / base) {
        return ld_parser_error(parser, "CupidLD number overflows");
      }
      number = number * base + digit;
      have_digit = CTOOL_TRUE;
      ld_parser_take(parser);
    }
    if (have_digit == CTOOL_FALSE) {
      return ld_parser_error(parser, "CupidLD number is invalid");
    }
    parser->token.kind = LD_TOKEN_NUMBER;
    parser->token.number = number;
    return CTOOL_OK;
  }
  if (value == (ctool_u8)'\"') {
    ld_parser_take(parser);
    start = parser->offset;
    while (parser->offset < input.size &&
           input.data[parser->offset] != (ctool_u8)'\"') {
      if (input.data[parser->offset] == (ctool_u8)'\n') {
        return ld_parser_error(parser, "CupidLD string is unterminated");
      }
      if (input.data[parser->offset] == (ctool_u8)'\\' &&
          parser->offset + 1u < input.size) {
        ld_parser_take(parser);
      }
      ld_parser_take(parser);
    }
    if (parser->offset >= input.size) {
      return ld_parser_error(parser, "CupidLD string is unterminated");
    }
    parser->token.kind = LD_TOKEN_STRING;
    parser->token.text.data = (const char *)(input.data + start);
    parser->token.text.size = parser->offset - start;
    ld_parser_take(parser);
    return CTOOL_OK;
  }
  ld_parser_take(parser);
  switch (value) {
  case (ctool_u8)'{':
    parser->token.kind = LD_TOKEN_LEFT_BRACE;
    return CTOOL_OK;
  case (ctool_u8)'}':
    parser->token.kind = LD_TOKEN_RIGHT_BRACE;
    return CTOOL_OK;
  case (ctool_u8)'(':
    parser->token.kind = LD_TOKEN_LEFT_PAREN;
    return CTOOL_OK;
  case (ctool_u8)')':
    parser->token.kind = LD_TOKEN_RIGHT_PAREN;
    return CTOOL_OK;
  case (ctool_u8)':':
    parser->token.kind = LD_TOKEN_COLON;
    return CTOOL_OK;
  case (ctool_u8)';':
    parser->token.kind = LD_TOKEN_SEMICOLON;
    return CTOOL_OK;
  case (ctool_u8)'*':
    parser->token.kind = LD_TOKEN_STAR;
    return CTOOL_OK;
  case (ctool_u8)'=':
    parser->token.kind = LD_TOKEN_ASSIGN;
    return CTOOL_OK;
  case (ctool_u8)',':
    parser->token.kind = LD_TOKEN_COMMA;
    return CTOOL_OK;
  case (ctool_u8)'<':
    if (parser->offset < input.size &&
        input.data[parser->offset] == (ctool_u8)'=') {
      ld_parser_take(parser);
      parser->token.kind = LD_TOKEN_LESS_EQUAL;
      return CTOOL_OK;
    }
    break;
  default:
    break;
  }
  return ld_parser_error(parser, "CupidLD script character is unsupported");
}

static ctool_status_t ld_parser_expect(ld_parser_t *parser,
                                       ld_token_kind_t kind,
                                       const char *message) {
  ctool_status_t status;
  if (parser->token.kind != kind) {
    return ld_parser_error(parser, message);
  }
  status = ld_parser_next(parser);
  return status;
}

static ctool_status_t ld_parse_expression(ld_parser_t *parser,
                                          ctool_u32 *value_out) {
  ctool_status_t status;
  if (parser->token.kind == LD_TOKEN_NUMBER) {
    *value_out = parser->token.number;
    return ld_parser_next(parser);
  }
  if (parser->token.kind != LD_TOKEN_NAME) {
    return ld_parser_error(parser, "CupidLD expression is invalid");
  }
  if (ld_string_literal(parser->token.text, ".") == CTOOL_TRUE) {
    *value_out = parser->link->dot;
    return ld_parser_next(parser);
  }
  if (ld_string_literal(parser->token.text, "ALIGN") == CTOOL_TRUE) {
    ctool_u32 alignment;
    status = ld_parser_next(parser);
    if (status == CTOOL_OK) {
      status = ld_parser_expect(parser, LD_TOKEN_LEFT_PAREN,
                                "CupidLD ALIGN requires '('");
    }
    if (status == CTOOL_OK) {
      status = ld_parse_expression(parser, &alignment);
    }
    if (status == CTOOL_OK) {
      status = ld_parser_expect(parser, LD_TOKEN_RIGHT_PAREN,
                                "CupidLD ALIGN requires ')'");
    }
    if (status != CTOOL_OK ||
        ld_is_power_of_two(alignment) == CTOOL_FALSE) {
      return status != CTOOL_OK
                 ? status
                 : ld_parser_error(parser,
                                   "CupidLD ALIGN value is invalid");
    }
    return ld_align_value(parser->link->dot, alignment, value_out);
  }
  {
    ctool_u32 global_index;
    ctool_string_t name = parser->token.text;
    status = ld_parser_next(parser);
    if (status != CTOOL_OK) {
      return status;
    }
    status = ld_find_global(parser->link, name, CTOOL_FALSE, &global_index);
    if (status != CTOOL_OK) {
      return ld_parser_error(parser, "CupidLD expression symbol is unknown");
    }
    status = ld_compute_global_value(parser->link, global_index);
    if (status != CTOOL_OK) {
      return status;
    }
    *value_out = parser->link->globals[global_index].value;
    return CTOOL_OK;
  }
}

static ctool_status_t ld_define_script_symbol(ld_parser_t *parser,
                                              ctool_string_t name,
                                              ctool_u32 value) {
  ld_context_t *link = parser->link;
  ctool_u32 global_index;
  ld_global_t *global;
  ctool_status_t status =
      ld_find_global(link, name, CTOOL_TRUE, &global_index);
  if (status != CTOOL_OK) {
    return status;
  }
  global = &link->globals[global_index];
  if (global->rank >= LD_DEFINITION_STRONG) {
    return ld_diagnostic(link->job, CTOOL_LD_DIAG_DUPLICATE_SYMBOL,
                         parser->source->path.text, parser->token.line,
                         parser->token.column,
                         "CupidLD script symbol has a strong definition",
                         CTOOL_ERR_INPUT);
  }
  global->rank = LD_DEFINITION_SCRIPT;
  global->script_symbol = CTOOL_TRUE;
  global->value_ready = CTOOL_TRUE;
  global->value = value;
  global->size = 0u;
  global->binding = CTOOL_ELF32_BIND_GLOBAL;
  global->type = CTOOL_ELF32_SYMBOL_NOTYPE;
  global->visibility = CTOOL_ELF32_VIS_DEFAULT;
  global->winner_object = LD_NONE;
  global->winner_symbol = LD_NONE;
  global->output_index = LD_NONE;
  if (link->current_output != LD_NONE) {
    global->output_index = link->current_output;
  } else if (link->last_output != LD_NONE) {
    global->output_index = link->last_output;
  }
  return CTOOL_OK;
}

typedef struct {
  ctool_string_t pattern;
  ctool_bool prefix;
} ld_pattern_selector_t;

static ctool_bool ld_pattern_selector_matches(
    const ctool_elf32_section_t *section, const void *context) {
  const ld_pattern_selector_t *selector =
      (const ld_pattern_selector_t *)context;
  return selector->prefix == CTOOL_TRUE
             ? ld_string_prefix(section->name, selector->pattern)
             : ld_string_equal(section->name, selector->pattern);
}

static ctool_status_t ld_select_sections(ld_parser_t *parser,
                                         ctool_string_t pattern,
                                         ctool_bool prefix) {
  ld_context_t *link = parser->link;
  ld_pattern_selector_t selector;
  ctool_u32 object_index;
  ctool_status_t status;
  selector.pattern = pattern;
  selector.prefix = prefix;
  for (object_index = 0u; object_index < link->object_count; object_index++) {
    const ld_object_t *object = &link->objects[object_index];
    ctool_u32 section_index;
    for (section_index = 1u; section_index < object->object.section_count;
         section_index++) {
      const ctool_elf32_section_t *section =
          &object->object.sections[section_index];
      ctool_bool matches =
          prefix == CTOOL_TRUE ? ld_string_prefix(section->name, pattern)
                               : ld_string_equal(section->name, pattern);
      if ((section->flags & CTOOL_ELF32_SHF_ALLOC) != 0u &&
          object->section_maps[section_index].matched == CTOOL_FALSE &&
          matches == CTOOL_TRUE) {
        if ((section->flags & CTOOL_ELF32_SHF_MERGE) != 0u) {
          status = ld_add_selected_merge_group(
              link, link->current_output, object_index, section_index,
              ld_pattern_selector_matches, &selector);
        } else {
          status = ld_add_file_section(link, object_index, section_index,
                                       link->current_output);
        }
        if (status != CTOOL_OK) {
          return status;
        }
      }
    }
  }
  return CTOOL_OK;
}

static ctool_status_t ld_parse_output_body(ld_parser_t *parser) {
  ctool_status_t status;
  while (parser->token.kind != LD_TOKEN_RIGHT_BRACE) {
    if (parser->token.kind == LD_TOKEN_SEMICOLON) {
      status = ld_parser_next(parser);
      if (status != CTOOL_OK) {
        return status;
      }
      continue;
    }
    if (parser->token.kind == LD_TOKEN_STAR) {
      ctool_string_t pattern;
      ctool_bool prefix = CTOOL_FALSE;
      status = ld_parser_next(parser);
      if (status == CTOOL_OK) {
        status = ld_parser_expect(parser, LD_TOKEN_LEFT_PAREN,
                                  "CupidLD selector requires '('");
      }
      if (status != CTOOL_OK || parser->token.kind != LD_TOKEN_NAME) {
        return status != CTOOL_OK
                   ? status
                   : ld_parser_error(parser,
                                     "CupidLD selector name is missing");
      }
      pattern = parser->token.text;
      status = ld_parser_next(parser);
      if (status != CTOOL_OK) {
        return status;
      }
      if (ld_string_literal(pattern, "COMMON") == CTOOL_TRUE) {
        status = ld_parser_expect(parser, LD_TOKEN_RIGHT_PAREN,
                                  "CupidLD COMMON requires ')'");
        if (status == CTOOL_OK) {
          status = ld_add_common(parser->link,
                                 parser->link->current_output);
        }
      } else {
        if (parser->token.kind == LD_TOKEN_STAR) {
          prefix = CTOOL_TRUE;
          status = ld_parser_next(parser);
        }
        if (status == CTOOL_OK) {
          status = ld_parser_expect(parser, LD_TOKEN_RIGHT_PAREN,
                                    "CupidLD selector requires ')'");
        }
        if (status == CTOOL_OK) {
          status = ld_select_sections(parser, pattern, prefix);
        }
      }
      if (status != CTOOL_OK) {
        return status;
      }
      if (ld_add_overflows(
              parser->link->outputs[parser->link->current_output].address,
              parser->link->outputs[parser->link->current_output].size) ==
          CTOOL_TRUE) {
        return CTOOL_ERR_OVERFLOW;
      }
      parser->link->dot =
          parser->link->outputs[parser->link->current_output].address +
          parser->link->outputs[parser->link->current_output].size;
      continue;
    }
    if (parser->token.kind == LD_TOKEN_NAME &&
        ld_string_literal(parser->token.text, ".") == CTOOL_FALSE) {
      ctool_string_t name = parser->token.text;
      ctool_u32 value;
      status = ld_parser_next(parser);
      if (status == CTOOL_OK) {
        status = ld_parser_expect(parser, LD_TOKEN_ASSIGN,
                                  "CupidLD symbol requires '='");
      }
      if (status == CTOOL_OK) {
        status = ld_parse_expression(parser, &value);
      }
      if (status == CTOOL_OK) {
        status = ld_define_script_symbol(parser, name, value);
      }
      if (status != CTOOL_OK) {
        return status;
      }
      continue;
    }
    return ld_parser_error(parser,
                           "CupidLD output-section statement is invalid");
  }
  return ld_parser_next(parser);
}

static ctool_status_t ld_parse_assert(ld_parser_t *parser) {
  ctool_u32 left;
  ctool_u32 right;
  ctool_string_t message = ctool_string("CupidLD ASSERT failed");
  ctool_u32 line = parser->token.line;
  ctool_u32 column = parser->token.column;
  ctool_status_t status = ld_parser_next(parser);
  if (status == CTOOL_OK) {
    status = ld_parser_expect(parser, LD_TOKEN_LEFT_PAREN,
                              "CupidLD ASSERT requires '('");
  }
  if (status == CTOOL_OK) {
    status = ld_parse_expression(parser, &left);
  }
  if (status == CTOOL_OK) {
    status = ld_parser_expect(parser, LD_TOKEN_LESS_EQUAL,
                              "CupidLD ASSERT requires '<='");
  }
  if (status == CTOOL_OK) {
    status = ld_parse_expression(parser, &right);
  }
  if (status == CTOOL_OK) {
    status = ld_parser_expect(parser, LD_TOKEN_COMMA,
                              "CupidLD ASSERT requires a message");
  }
  if (status == CTOOL_OK) {
    if (parser->token.kind != LD_TOKEN_STRING) {
      status = ld_parser_error(parser,
                               "CupidLD ASSERT message is invalid");
    } else {
      message = parser->token.text;
      status = ld_parser_next(parser);
    }
  }
  if (status == CTOOL_OK) {
    status = ld_parser_expect(parser, LD_TOKEN_RIGHT_PAREN,
                              "CupidLD ASSERT requires ')'");
  }
  if (status == CTOOL_OK && left > right) {
    status = ld_diagnostic_text(parser->link->job,
                                CTOOL_LD_DIAG_ASSERTION_FAILED,
                                parser->source->path.text, line, column,
                                message, CTOOL_ERR_INPUT);
  }
  return status;
}

static ctool_status_t ld_parse_output_section(ld_parser_t *parser,
                                              ctool_string_t name) {
  ld_context_t *link = parser->link;
  ctool_u32 address = link->dot;
  ctool_u32 output_alignment = 1u;
  ctool_u32 output_index;
  ctool_status_t status;
  if (parser->token.kind == LD_TOKEN_NAME &&
      ld_string_literal(parser->token.text, "ALIGN") == CTOOL_TRUE) {
    ctool_u32 alignment;
    status = ld_parser_next(parser);
    if (status == CTOOL_OK) {
      status = ld_parser_expect(parser, LD_TOKEN_LEFT_PAREN,
                                "CupidLD output ALIGN requires '('");
    }
    if (status == CTOOL_OK) {
      status = ld_parse_expression(parser, &alignment);
    }
    if (status == CTOOL_OK) {
      status = ld_parser_expect(parser, LD_TOKEN_RIGHT_PAREN,
                                "CupidLD output ALIGN requires ')'");
    }
    if (status != CTOOL_OK ||
        ld_is_power_of_two(alignment) == CTOOL_FALSE) {
      return status != CTOOL_OK
                 ? status
                 : ld_parser_error(parser,
                                   "CupidLD output ALIGN is invalid");
    }
    status = ld_align_value(link->dot, alignment, &address);
    if (status != CTOOL_OK) {
      return status;
    }
    output_alignment = alignment;
    link->dot = address;
  }
  status = ld_parser_expect(parser, LD_TOKEN_COLON,
                            "CupidLD output section requires ':'");
  if (status == CTOOL_OK) {
    status = ld_parser_expect(parser, LD_TOKEN_LEFT_BRACE,
                              "CupidLD output section requires '{'");
  }
  if (status == CTOOL_OK) {
    status = ld_begin_output(link, name, address, &output_index);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  link->outputs[output_index].alignment = output_alignment;
  link->current_output = output_index;
  status = ld_parse_output_body(parser);
  if (status != CTOOL_OK) {
    return status;
  }
  if (link->outputs[output_index].type == 0u) {
    link->outputs[output_index].type =
        (ctool_u32)CTOOL_ELF32_SHT_PROGBITS;
  }
  if (ld_add_overflows(link->outputs[output_index].address,
                       link->outputs[output_index].size) == CTOOL_TRUE) {
    return CTOOL_ERR_OVERFLOW;
  }
  link->dot = link->outputs[output_index].address +
              link->outputs[output_index].size;
  link->current_output = LD_NONE;
  return CTOOL_OK;
}

static ctool_status_t ld_layout_script(ld_context_t *link) {
  ld_parser_t parser;
  const ctool_source_t *source = link->request->layout.as.script;
  ctool_status_t status;
  if (source == (const ctool_source_t *)0 ||
      source->contents.data == (const ctool_u8 *)0 ||
      source->contents.size == 0u) {
    return ld_diagnostic(link->job, CTOOL_LD_DIAG_BAD_LAYOUT,
                         ctool_string(""), 0u, 0u,
                         "CupidLD script source is invalid", CTOOL_ERR_INPUT);
  }
  ld_zero(&parser, (ctool_u32)sizeof(parser));
  parser.link = link;
  parser.source = source;
  parser.line = 1u;
  parser.column = 1u;
  status = ld_parser_next(&parser);
  if (status != CTOOL_OK || parser.token.kind != LD_TOKEN_NAME ||
      ld_string_literal(parser.token.text, "ENTRY") == CTOOL_FALSE) {
    return status != CTOOL_OK
               ? status
               : ld_parser_error(&parser,
                                 "CupidLD script must begin with ENTRY");
  }
  status = ld_parser_next(&parser);
  if (status == CTOOL_OK) {
    status = ld_parser_expect(&parser, LD_TOKEN_LEFT_PAREN,
                              "CupidLD ENTRY requires '('");
  }
  if (status != CTOOL_OK || parser.token.kind != LD_TOKEN_NAME) {
    return status != CTOOL_OK
               ? status
               : ld_parser_error(&parser,
                                 "CupidLD ENTRY symbol is missing");
  }
  link->entry_symbol = parser.token.text;
  status = ld_parser_next(&parser);
  if (status == CTOOL_OK) {
    status = ld_parser_expect(&parser, LD_TOKEN_RIGHT_PAREN,
                              "CupidLD ENTRY requires ')'");
  }
  if (status != CTOOL_OK || parser.token.kind != LD_TOKEN_NAME ||
      ld_string_literal(parser.token.text, "SECTIONS") == CTOOL_FALSE) {
    return status != CTOOL_OK
               ? status
               : ld_parser_error(&parser,
                                 "CupidLD script requires SECTIONS");
  }
  status = ld_parser_next(&parser);
  if (status == CTOOL_OK) {
    status = ld_parser_expect(&parser, LD_TOKEN_LEFT_BRACE,
                              "CupidLD SECTIONS requires '{'");
  }
  while (status == CTOOL_OK &&
         parser.token.kind != LD_TOKEN_RIGHT_BRACE) {
    if (parser.token.kind == LD_TOKEN_SEMICOLON) {
      status = ld_parser_next(&parser);
      continue;
    }
    if (parser.token.kind != LD_TOKEN_NAME) {
      return ld_parser_error(&parser,
                             "CupidLD SECTIONS statement is invalid");
    }
    if (ld_string_literal(parser.token.text, "ASSERT") == CTOOL_TRUE) {
      status = ld_parse_assert(&parser);
      continue;
    }
    {
      ctool_string_t name = parser.token.text;
      ctool_bool dot_assignment =
          ld_string_literal(name, ".") == CTOOL_TRUE ? CTOOL_TRUE
                                                      : CTOOL_FALSE;
      status = ld_parser_next(&parser);
      if (status != CTOOL_OK) {
        break;
      }
      if (parser.token.kind == LD_TOKEN_ASSIGN) {
        ctool_u32 value;
        status = ld_parser_next(&parser);
        if (status == CTOOL_OK) {
          status = ld_parse_expression(&parser, &value);
        }
        if (status == CTOOL_OK && dot_assignment == CTOOL_TRUE) {
          if (value < link->dot) {
            status = ld_diagnostic(link->job,
                                   CTOOL_LD_DIAG_BACKWARD_DOT,
                                   source->path.text, parser.token.line,
                                   parser.token.column,
                                   "CupidLD location counter moved backward",
                                   CTOOL_ERR_INPUT);
          } else {
            link->dot = value;
          }
        } else if (status == CTOOL_OK) {
          status = ld_define_script_symbol(&parser, name, value);
        }
      } else if (dot_assignment == CTOOL_TRUE) {
        status = ld_parser_error(&parser,
                                 "CupidLD location counter requires '='");
      } else {
        status = ld_parse_output_section(&parser, name);
      }
    }
  }
  if (status == CTOOL_OK) {
    status = ld_parser_expect(&parser, LD_TOKEN_RIGHT_BRACE,
                              "CupidLD SECTIONS requires '}'");
  }
  if (status == CTOOL_OK && parser.token.kind != LD_TOKEN_EOF) {
    status = ld_parser_error(&parser,
                             "CupidLD script has trailing tokens");
  }
  if (status == CTOOL_OK && link->output_count == 0u) {
    status = ld_parser_error(&parser,
                             "CupidLD script has no output sections");
  }
  return status;
}

static ctool_status_t ld_reject_unmatched_allocated(ld_context_t *link) {
  ctool_u32 object_index;
  for (object_index = 0u; object_index < link->object_count; object_index++) {
    const ld_object_t *object = &link->objects[object_index];
    ctool_u32 section_index;
    for (section_index = 1u; section_index < object->object.section_count;
         section_index++) {
      const ctool_elf32_section_t *section =
          &object->object.sections[section_index];
      if ((section->flags & CTOOL_ELF32_SHF_ALLOC) != 0u &&
          object->section_maps[section_index].matched == CTOOL_FALSE) {
        return ld_diagnostic(link->job, CTOOL_LD_DIAG_UNMATCHED_SECTION,
                             object->source->path.text, 0u, section_index,
                             "CupidLD layout left an allocated section unmatched",
                             CTOOL_ERR_INPUT);
      }
    }
  }
  return CTOOL_OK;
}

ctool_status_t ctool_ld_link(ctool_job_t *job,
                             const ctool_ld_request_t *request,
                             ctool_buffer_t *output,
                             ctool_ld_result_t *result_out) {
  ld_context_t link;
  ctool_ld_result_t result;
  ctool_arena_mark_t arena_mark;
  ctool_u32 output_mark;
  ctool_u32 diagnostics_before;
  const char *phase = "CupidLD request setup failed";
  ctool_status_t status;
  if (result_out == (ctool_ld_result_t *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  ld_zero(result_out, (ctool_u32)sizeof(*result_out));
  if (job == (ctool_job_t *)0 || request == (const ctool_ld_request_t *)0 ||
      output == (ctool_buffer_t *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  if (request->objects == (const ctool_source_t *)0 ||
      request->object_count == 0u || request->maximum_image_span == 0u ||
      request->layout.kind > CTOOL_LD_LAYOUT_FIXED_TEXT) {
    return ld_diagnostic(job, CTOOL_LD_DIAG_INVALID_REQUEST,
                         ctool_string(""), 0u, 0u,
                         "CupidLD request is invalid",
                         CTOOL_ERR_INVALID_ARGUMENT);
  }
  output_mark = ctool_buffer_mark(output);
  if (output_mark != 0u) {
    return ld_diagnostic(job, CTOOL_LD_DIAG_NONEMPTY_OUTPUT,
                         ctool_string(""), 0u, 0u,
                         "CupidLD output buffer must be empty",
                         CTOOL_ERR_INPUT);
  }
  ld_zero(&link, (ctool_u32)sizeof(link));
  ld_zero(&result, (ctool_u32)sizeof(result));
  link.job = job;
  link.request = request;
  link.arena = ctool_job_arena(job);
  link.object_count = request->object_count;
  link.current_output = LD_NONE;
  link.last_output = LD_NONE;
  arena_mark = ctool_arena_mark(link.arena);
  diagnostics_before = ctool_job_diagnostic_count(job);
  status = ctool_job_open_buffer(
      job,
      request->maximum_image_span < 256u ? request->maximum_image_span : 256u,
      request->maximum_image_span, &link.payload);
  if (status == CTOOL_OK) {
    phase = "CupidLD input object loading failed";
    status = ld_read_objects(&link);
  }
  if (status == CTOOL_OK) {
    phase = "CupidLD symbol resolution setup failed";
    status = ld_prepare_globals(&link);
  }
  if (status == CTOOL_OK) {
    phase = "CupidLD layout storage setup failed";
    status = ld_prepare_layout_storage(&link);
  }
  if (status == CTOOL_OK) {
    phase = "CupidLD layout execution failed";
    if (request->layout.kind == CTOOL_LD_LAYOUT_FIXED_TEXT) {
      status = ld_layout_fixed(&link);
    } else {
      status = ld_layout_script(&link);
    }
  }
  if (status == CTOOL_OK) {
    phase = "CupidLD unmatched-section validation failed";
    status = ld_reject_unmatched_allocated(&link);
  }
  if (status == CTOOL_OK) {
    phase = "CupidLD final symbol resolution failed";
    status = ld_finalize_globals(&link);
  }
  if (status == CTOOL_OK) {
    phase = "CupidLD relocation application failed";
    status = ld_apply_relocations(&link);
  }
  if (status == CTOOL_OK) {
    phase = "CupidLD executable serialization failed";
    status = ld_serialize_exec(&link, output, &result);
  }
  if (status != CTOOL_OK &&
      ctool_job_diagnostic_count(job) == diagnostics_before) {
    ctool_u32 code = status == CTOOL_ERR_OVERFLOW
                         ? CTOOL_LD_DIAG_OVERFLOW
                         : CTOOL_LD_DIAG_LIMIT;
    (void)ld_diagnostic(job, code, ctool_string(""), 0u, 0u, phase, status);
  }
  if (link.payload != (ctool_buffer_t *)0) {
    ctool_buffer_close(link.payload);
  }
  if (status != CTOOL_OK) {
    ctool_status_t rewind_status;
    (void)ctool_buffer_rewind(output, output_mark);
    rewind_status = ctool_arena_rewind(link.arena, arena_mark);
    ld_zero(result_out, (ctool_u32)sizeof(*result_out));
    return rewind_status == CTOOL_OK ? status : rewind_status;
  }
  status = ctool_arena_rewind(link.arena, arena_mark);
  if (status != CTOOL_OK) {
    (void)ctool_buffer_rewind(output, output_mark);
    ld_zero(result_out, (ctool_u32)sizeof(*result_out));
    return status;
  }
  *result_out = result;
  return CTOOL_OK;
}
