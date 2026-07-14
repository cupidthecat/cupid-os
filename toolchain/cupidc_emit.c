#include "cupidc_emit.h"

#include "elf32.h"

#define CEMIT_SECTION_RODATA 0u
#define CEMIT_SECTION_DATA 1u
#define CEMIT_SECTION_BSS 2u
#define CEMIT_SECTION_COUNT 3u

typedef struct {
  ctool_job_t *job;
  const ctool_c_translation_unit_t *unit;
  ctool_arena_t *arena;
  ctool_buffer_t *rodata;
  ctool_buffer_t *data;
  ctool_buffer_t *object_output;
  ctool_u32 bss_size;
  ctool_u32 section_alignment[CEMIT_SECTION_COUNT];
  ctool_elf32_symbol_spec_t *symbols;
  ctool_u32 symbol_count;
  ctool_u32 symbol_capacity;
  ctool_elf32_relocation_spec_t *relocations;
  ctool_u32 relocation_count;
  ctool_u32 relocation_capacity;
  ctool_u32 *binding_symbols;
  ctool_u32 *binding_definitions;
  ctool_bool *binding_needed;
  ctool_bool *initializer_is_zero;
  ctool_u32 literal_count;
  ctool_bool failure_reported;
} cemit_context_t;

static void cemit_zero(void *destination, ctool_u32 size) {
  ctool_u8 *bytes = (ctool_u8 *)destination;
  ctool_u32 index;
  for (index = 0u; index < size; index++) {
    bytes[index] = 0u;
  }
}

static ctool_bool cemit_add_overflows(ctool_u32 left, ctool_u32 right) {
  return left > 0xffffffffu - right ? CTOOL_TRUE : CTOOL_FALSE;
}

static ctool_bool cemit_multiply_overflows(ctool_u32 left,
                                            ctool_u32 right) {
  return left != 0u && right > 0xffffffffu / left ? CTOOL_TRUE
                                                  : CTOOL_FALSE;
}

static ctool_bool cemit_power_of_two(ctool_u32 value) {
  return value != 0u && (value & (value - 1u)) == 0u ? CTOOL_TRUE
                                                     : CTOOL_FALSE;
}

static ctool_status_t cemit_align_value(ctool_u32 value,
                                         ctool_u32 alignment,
                                         ctool_u32 *aligned_out) {
  ctool_u32 padding;
  if (aligned_out == (ctool_u32 *)0 ||
      cemit_power_of_two(alignment) == CTOOL_FALSE) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  padding = (0u - value) & (alignment - 1u);
  if (cemit_add_overflows(value, padding) == CTOOL_TRUE) {
    return CTOOL_ERR_OVERFLOW;
  }
  *aligned_out = value + padding;
  return CTOOL_OK;
}

static ctool_status_t cemit_emit_failure(
    cemit_context_t *context, ctool_status_t status, ctool_u32 code,
    const ctool_c_pp_location_t *location, const char *message) {
  ctool_diagnostic_t diagnostic;
  ctool_status_t emitted;
  diagnostic.severity = CTOOL_DIAG_ERROR;
  diagnostic.code = code;
  diagnostic.path = location != (const ctool_c_pp_location_t *)0
                        ? location->path
                        : ctool_string("");
  diagnostic.line = location != (const ctool_c_pp_location_t *)0
                        ? location->line
                        : 0u;
  diagnostic.column = location != (const ctool_c_pp_location_t *)0
                          ? location->column
                          : 0u;
  diagnostic.message = ctool_string(message);
  context->failure_reported = CTOOL_TRUE;
  emitted = ctool_job_emit(context->job, &diagnostic);
  return emitted == CTOOL_OK ? status : emitted;
}

static ctool_status_t cemit_invalid_unit(
    cemit_context_t *context, const ctool_c_pp_location_t *location) {
  return cemit_emit_failure(
      context, CTOOL_ERR_INPUT, CTOOL_C_EMIT_DIAG_INVALID_UNIT, location,
      "CupidC object emission received an invalid translation unit");
}

static ctool_status_t cemit_alloc_array(cemit_context_t *context,
                                         ctool_u32 count,
                                         ctool_u32 element_size,
                                         void **array_out) {
  *array_out = (void *)0;
  if (count == 0u) {
    return CTOOL_OK;
  }
  return ctool_arena_alloc_zero(context->arena, count, element_size,
                                (ctool_u32)sizeof(void *), array_out);
}

static const ctool_c_type_node_t *cemit_type_node(
    const cemit_context_t *context, ctool_u32 type) {
  return type < context->unit->graph.type_count
             ? &context->unit->graph.types[type]
             : (const ctool_c_type_node_t *)0;
}

static const ctool_c_type_node_t *cemit_unwrapped_type(
    const cemit_context_t *context, ctool_u32 type) {
  const ctool_c_type_node_t *node = cemit_type_node(context, type);
  ctool_u32 traversed = 0u;
  while (node != (const ctool_c_type_node_t *)0 &&
         (node->kind == CTOOL_C_TYPE_ALIGNED ||
          node->kind == CTOOL_C_TYPE_QUALIFIED)) {
    if (traversed++ >= context->unit->graph.type_count) {
      return (const ctool_c_type_node_t *)0;
    }
    node = cemit_type_node(context, node->referenced_type);
  }
  return node;
}

static ctool_bool cemit_type_is_const(const cemit_context_t *context,
                                       ctool_u32 type) {
  ctool_u32 traversed = 0u;
  const ctool_c_type_node_t *node = cemit_type_node(context, type);
  while (node != (const ctool_c_type_node_t *)0 &&
         traversed++ < context->unit->graph.type_count) {
    if ((node->qualifiers & CTOOL_C_QUAL_CONST) != 0u) {
      return CTOOL_TRUE;
    }
    if (node->kind == CTOOL_C_TYPE_ALIGNED ||
        node->kind == CTOOL_C_TYPE_QUALIFIED ||
        node->kind == CTOOL_C_TYPE_ARRAY) {
      node = cemit_type_node(context, node->referenced_type);
    } else {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_FALSE;
}

static ctool_status_t cemit_validate_unit_shape(cemit_context_t *context) {
  const ctool_c_translation_unit_t *unit = context->unit;
  if ((unit->graph.type_count != 0u &&
       unit->graph.types == (const ctool_c_type_node_t *)0) ||
      (unit->graph.member_count != 0u &&
       unit->graph.members == (const ctool_c_record_member_t *)0) ||
      (unit->layout.type_count != unit->graph.type_count) ||
      (unit->layout.member_count != unit->graph.member_count) ||
      (unit->layout.type_count != 0u &&
       unit->layout.types == (const ctool_c_type_layout_t *)0) ||
      (unit->layout.member_count != 0u &&
       unit->layout.members == (const ctool_c_member_layout_t *)0) ||
      (unit->binding_count != 0u &&
       unit->bindings == (const ctool_c_binding_t *)0) ||
      (unit->object_definition_count != 0u &&
       unit->object_definitions ==
           (const ctool_c_object_definition_t *)0) ||
      (unit->initializer_count != 0u &&
       unit->initializers == (const ctool_c_initializer_t *)0) ||
      (unit->initializer_element_count != 0u &&
       unit->initializer_elements ==
           (const ctool_c_initializer_element_t *)0) ||
      (unit->function_definition_count != 0u &&
       unit->function_definitions ==
           (const ctool_c_function_definition_t *)0)) {
    return cemit_invalid_unit(context,
                              (const ctool_c_pp_location_t *)0);
  }
  return CTOOL_OK;
}

static ctool_status_t cemit_index_definitions(cemit_context_t *context) {
  ctool_u32 index;
  for (index = 0u; index < context->unit->binding_count; index++) {
    context->binding_symbols[index] = CTOOL_C_AST_NONE;
    context->binding_definitions[index] = CTOOL_C_AST_NONE;
  }
  for (index = 0u; index < context->unit->object_definition_count; index++) {
    const ctool_c_object_definition_t *definition =
        &context->unit->object_definitions[index];
    const ctool_c_binding_t *binding;
    if (definition->binding >= context->unit->binding_count ||
        definition->declared_type >= context->unit->graph.type_count ||
        definition->initializer >= context->unit->initializer_count ||
        context->binding_definitions[definition->binding] !=
            CTOOL_C_AST_NONE) {
      return cemit_invalid_unit(context, &definition->location);
    }
    binding = &context->unit->bindings[definition->binding];
    if (binding->kind != CTOOL_C_BINDING_OBJECT ||
        (definition->kind != CTOOL_C_OBJECT_DEFINITION_EXPLICIT &&
         definition->kind != CTOOL_C_OBJECT_DEFINITION_TENTATIVE)) {
      return cemit_invalid_unit(context, &definition->location);
    }
    context->binding_definitions[definition->binding] = index;
    context->binding_needed[definition->binding] = CTOOL_TRUE;
  }
  return CTOOL_OK;
}

static ctool_bool cemit_bytes_are_zero(ctool_bytes_t bytes) {
  ctool_u32 index;
  if (bytes.data == (const ctool_u8 *)0 && bytes.size != 0u) {
    return CTOOL_FALSE;
  }
  for (index = 0u; index < bytes.size; index++) {
    if (bytes.data[index] != 0u) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_status_t cemit_index_initializers(cemit_context_t *context) {
  ctool_u32 element_cursor = 0u;
  ctool_u32 index;
  for (index = 0u; index < context->unit->initializer_count; index++) {
    const ctool_c_initializer_t *initializer =
        &context->unit->initializers[index];
    ctool_bool is_zero = CTOOL_FALSE;
    ctool_u32 edge_index;
    if (initializer->type >= context->unit->graph.type_count) {
      return cemit_invalid_unit(context, &initializer->location);
    }
    if (initializer->kind == CTOOL_C_INITIALIZER_EXPRESSION) {
      return cemit_emit_failure(
          context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_EMIT_DIAG_INITIALIZER,
          &initializer->location,
          "CupidC object emission requires static initializer values");
    }
    if (initializer->kind == CTOOL_C_INITIALIZER_ZERO) {
      is_zero = CTOOL_TRUE;
    } else if (initializer->kind == CTOOL_C_INITIALIZER_INTEGER) {
      is_zero = initializer->integer_bits == 0u ? CTOOL_TRUE : CTOOL_FALSE;
    } else if (initializer->kind == CTOOL_C_INITIALIZER_STRING) {
      if (initializer->string_bytes.data == (const ctool_u8 *)0 &&
          initializer->string_bytes.size != 0u) {
        return cemit_invalid_unit(context, &initializer->location);
      }
      is_zero = cemit_bytes_are_zero(initializer->string_bytes);
    } else if (initializer->kind == CTOOL_C_INITIALIZER_ADDRESS) {
      if ((initializer->address_kind ==
               CTOOL_C_INITIALIZER_ADDRESS_STRING &&
           (initializer->address_reference != CTOOL_C_AST_NONE ||
            initializer->string_bytes.data == (const ctool_u8 *)0 ||
            initializer->string_bytes.size == 0u)) ||
          (initializer->address_kind ==
               CTOOL_C_INITIALIZER_ADDRESS_BINDING &&
           initializer->address_reference >= context->unit->binding_count) ||
          (initializer->address_kind !=
               CTOOL_C_INITIALIZER_ADDRESS_STRING &&
           initializer->address_kind !=
               CTOOL_C_INITIALIZER_ADDRESS_BINDING)) {
        return cemit_invalid_unit(context, &initializer->location);
      }
      if (initializer->address_kind ==
          CTOOL_C_INITIALIZER_ADDRESS_BINDING) {
        context->binding_needed[initializer->address_reference] = CTOOL_TRUE;
      }
    } else if (initializer->kind == CTOOL_C_INITIALIZER_LIST) {
      const ctool_c_type_node_t *parent_type =
          cemit_unwrapped_type(context, initializer->type);
      if (parent_type == (const ctool_c_type_node_t *)0 ||
          initializer->element_count == 0u ||
          initializer->first_element != element_cursor ||
          initializer->first_element >
              context->unit->initializer_element_count ||
          initializer->element_count >
              context->unit->initializer_element_count -
                  initializer->first_element) {
        return cemit_invalid_unit(context, &initializer->location);
      }
      if (parent_type->kind == CTOOL_C_TYPE_ARRAY) {
        if (parent_type->array_bound_kind != CTOOL_C_ARRAY_FIXED ||
            parent_type->referenced_type >=
                context->unit->graph.type_count) {
          return cemit_invalid_unit(context, &initializer->location);
        }
      } else if (parent_type->kind == CTOOL_C_TYPE_RECORD) {
        if (parent_type->record_kind != CTOOL_C_RECORD_STRUCT) {
          return cemit_emit_failure(
              context, CTOOL_ERR_UNSUPPORTED,
              CTOOL_C_EMIT_DIAG_INITIALIZER, &initializer->location,
              "CupidC object emission does not yet support this aggregate initializer");
        }
        if (parent_type->record_complete == CTOOL_FALSE ||
            parent_type->first_member >
                context->unit->graph.member_count ||
            parent_type->member_count >
                context->unit->graph.member_count -
                    parent_type->first_member) {
          return cemit_invalid_unit(context, &initializer->location);
        }
      } else {
        return cemit_invalid_unit(context, &initializer->location);
      }
      is_zero = CTOOL_TRUE;
      for (edge_index = 0u; edge_index < initializer->element_count;
           edge_index++) {
        const ctool_c_initializer_element_t *edge =
            &context->unit->initializer_elements
                 [initializer->first_element + edge_index];
        const ctool_c_initializer_t *child;
        ctool_u32 child_type = CTOOL_C_TYPE_NONE;
        ctool_u32 previous;
        if (edge->initializer >= index) {
          return cemit_invalid_unit(context, &initializer->location);
        }
        child = &context->unit->initializers[edge->initializer];
        for (previous = 0u; previous < edge_index; previous++) {
          const ctool_c_initializer_element_t *previous_edge =
              &context->unit->initializer_elements
                   [initializer->first_element + previous];
          if (previous_edge->subobject == edge->subobject) {
            return cemit_invalid_unit(context, &initializer->location);
          }
        }
        if (parent_type->kind == CTOOL_C_TYPE_ARRAY) {
          if (edge->subobject >= parent_type->element_count) {
            return cemit_invalid_unit(context, &initializer->location);
          }
          child_type = parent_type->referenced_type;
        } else {
          const ctool_c_record_member_t *member;
          const ctool_c_type_node_t *member_type;
          if (edge->subobject < parent_type->first_member ||
              edge->subobject - parent_type->first_member >=
                  parent_type->member_count ||
              edge->subobject >= context->unit->layout.member_count) {
            return cemit_invalid_unit(context, &initializer->location);
          }
          member = &context->unit->graph.members[edge->subobject];
          if (member->type >= context->unit->graph.type_count ||
              (member->is_bit_field == CTOOL_TRUE &&
               member->name.size == 0u)) {
            return cemit_invalid_unit(context, &initializer->location);
          }
          member_type = cemit_unwrapped_type(context, member->type);
          if (member_type == (const ctool_c_type_node_t *)0 ||
              (edge->subobject + 1u ==
                   parent_type->first_member + parent_type->member_count &&
               member_type->kind == CTOOL_C_TYPE_ARRAY &&
               member_type->array_bound_kind ==
                   CTOOL_C_ARRAY_UNSPECIFIED)) {
            return cemit_invalid_unit(context, &initializer->location);
          }
          child_type = member->type;
        }
        if (child->type != child_type) {
          return cemit_invalid_unit(context, &initializer->location);
        }
        ctool_bool child_is_zero =
            context->initializer_is_zero[edge->initializer];
        if (parent_type->kind == CTOOL_C_TYPE_RECORD &&
            context->unit->graph.members[edge->subobject].is_bit_field ==
                CTOOL_TRUE) {
          ctool_u32 width =
              context->unit->layout.members[edge->subobject].bit_width;
          if (width == 0u || width > 64u ||
              (child->kind != CTOOL_C_INITIALIZER_ZERO &&
               child->kind != CTOOL_C_INITIALIZER_INTEGER)) {
            return cemit_invalid_unit(context, &initializer->location);
          }
          if (child->kind == CTOOL_C_INITIALIZER_INTEGER) {
            ctool_u64 mask = width == 64u
                                 ? ~(ctool_u64)0u
                                 : (((ctool_u64)1u << width) - 1u);
            child_is_zero = (child->integer_bits & mask) == 0u
                                ? CTOOL_TRUE
                                : CTOOL_FALSE;
          }
        }
        if (child_is_zero == CTOOL_FALSE) {
          is_zero = CTOOL_FALSE;
        }
      }
      element_cursor += initializer->element_count;
    } else {
      return cemit_invalid_unit(context, &initializer->location);
    }
    context->initializer_is_zero[index] = is_zero;
  }
  if (element_cursor != context->unit->initializer_element_count) {
    return cemit_invalid_unit(context,
                              (const ctool_c_pp_location_t *)0);
  }
  return CTOOL_OK;
}

static ctool_status_t cemit_make_literal_name(cemit_context_t *context,
                                               ctool_string_t *name_out) {
  char reversed[10];
  char *name;
  ctool_u32 value = context->literal_count;
  ctool_u32 digits = 0u;
  ctool_u32 index;
  ctool_status_t status;
  do {
    reversed[digits++] = (char)('0' + (char)(value % 10u));
    value /= 10u;
  } while (value != 0u);
  status = ctool_arena_alloc(context->arena, 3u + digits + 1u, 1u,
                             (void **)&name);
  if (status != CTOOL_OK) {
    return status;
  }
  name[0] = '.';
  name[1] = 'L';
  name[2] = 'C';
  for (index = 0u; index < digits; index++) {
    name[3u + index] = reversed[digits - 1u - index];
  }
  name[3u + digits] = '\0';
  name_out->data = name;
  name_out->size = 3u + digits;
  context->literal_count++;
  return CTOOL_OK;
}

static ctool_status_t cemit_ensure_binding_symbol(
    cemit_context_t *context, ctool_u32 binding_index,
    ctool_u32 *symbol_out) {
  const ctool_c_binding_t *binding;
  ctool_elf32_symbol_spec_t *symbol;
  ctool_u32 symbol_index;
  if (binding_index >= context->unit->binding_count) {
    return cemit_invalid_unit(context,
                              (const ctool_c_pp_location_t *)0);
  }
  if (context->binding_symbols[binding_index] != CTOOL_C_AST_NONE) {
    *symbol_out = context->binding_symbols[binding_index];
    return CTOOL_OK;
  }
  binding = &context->unit->bindings[binding_index];
  if ((binding->kind != CTOOL_C_BINDING_OBJECT &&
       binding->kind != CTOOL_C_BINDING_FUNCTION) ||
      (binding->linkage != CTOOL_C_LINKAGE_INTERNAL &&
       binding->linkage != CTOOL_C_LINKAGE_EXTERNAL) ||
      binding->name.data == (const char *)0 || binding->name.size == 0u) {
    return cemit_emit_failure(
        context, CTOOL_ERR_INPUT, CTOOL_C_EMIT_DIAG_SYMBOL,
        &binding->location,
        "CupidC cannot create an ELF symbol for this binding");
  }
  if (context->symbol_count >= context->symbol_capacity) {
    return CTOOL_ERR_INTERNAL;
  }
  symbol_index = context->symbol_count++;
  symbol = &context->symbols[symbol_index];
  symbol->name = binding->name;
  symbol->binding = binding->linkage == CTOOL_C_LINKAGE_INTERNAL
                        ? CTOOL_ELF32_BIND_LOCAL
                        : CTOOL_ELF32_BIND_GLOBAL;
  symbol->type = binding->kind == CTOOL_C_BINDING_FUNCTION
                     ? CTOOL_ELF32_SYMBOL_FUNCTION
                     : CTOOL_ELF32_SYMBOL_OBJECT;
  symbol->visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbol->placement = CTOOL_ELF32_SYMBOL_UNDEFINED;
  symbol->section = CTOOL_ELF32_NO_SECTION;
  context->binding_symbols[binding_index] = symbol_index;
  *symbol_out = symbol_index;
  return CTOOL_OK;
}

static ctool_status_t cemit_index_symbols(cemit_context_t *context) {
  ctool_u32 binding;
  for (binding = 0u; binding < context->unit->binding_count; binding++) {
    if (context->binding_needed[binding] == CTOOL_TRUE) {
      ctool_u32 symbol;
      ctool_status_t status =
          cemit_ensure_binding_symbol(context, binding, &symbol);
      if (status != CTOOL_OK) {
        return status;
      }
    }
  }
  return CTOOL_OK;
}

static ctool_buffer_t *cemit_section_buffer(cemit_context_t *context,
                                             ctool_u32 section) {
  if (section == CEMIT_SECTION_RODATA) {
    return context->rodata;
  }
  return section == CEMIT_SECTION_DATA ? context->data
                                       : (ctool_buffer_t *)0;
}

static ctool_status_t cemit_align_buffer(cemit_context_t *context,
                                          ctool_u32 section,
                                          ctool_u32 alignment) {
  ctool_buffer_t *buffer = cemit_section_buffer(context, section);
  ctool_u32 aligned;
  ctool_u32 size;
  ctool_status_t status;
  if (buffer == (ctool_buffer_t *)0) {
    return CTOOL_ERR_INTERNAL;
  }
  size = ctool_buffer_view(buffer).size;
  status = cemit_align_value(size, alignment, &aligned);
  if (status != CTOOL_OK) {
    return status;
  }
  return ctool_buffer_fill(buffer, 0u, aligned - size);
}

static ctool_status_t cemit_patch_integer(cemit_context_t *context,
                                           ctool_u32 section,
                                           ctool_u32 offset,
                                           ctool_u32 size,
                                           ctool_u64 bits) {
  ctool_buffer_t *buffer = cemit_section_buffer(context, section);
  ctool_bytes_t view;
  ctool_u32 index;
  ctool_status_t status = CTOOL_OK;
  if (buffer == (ctool_buffer_t *)0 || size == 0u || size > 8u) {
    return CTOOL_ERR_INTERNAL;
  }
  view = ctool_buffer_view(buffer);
  if (offset > view.size || size > view.size - offset) {
    return CTOOL_ERR_INTERNAL;
  }
  for (index = 0u; index < size && status == CTOOL_OK; index++) {
    status = ctool_buffer_patch_u8(
        buffer, offset + index,
        (ctool_u8)((bits >> (index * 8u)) & 0xffu));
  }
  return status;
}

static ctool_status_t cemit_add_relocation(
    cemit_context_t *context, ctool_u32 section, ctool_u32 offset,
    ctool_u32 symbol, ctool_i32 addend) {
  ctool_elf32_relocation_spec_t *relocation;
  ctool_status_t status;
  if (context->relocation_count >= context->relocation_capacity ||
      symbol >= context->symbol_count) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cemit_patch_integer(context, section, offset, 4u,
                               (ctool_u64)(ctool_u32)addend);
  if (status != CTOOL_OK) {
    return status;
  }
  relocation = &context->relocations[context->relocation_count++];
  relocation->target_section = section;
  relocation->offset = offset;
  relocation->symbol = symbol;
  relocation->type = CTOOL_ELF32_R_386_32;
  relocation->addend = addend;
  return CTOOL_OK;
}

static ctool_status_t cemit_add_string_literal(
    cemit_context_t *context, const ctool_c_initializer_t *initializer,
    ctool_u32 *symbol_out) {
  ctool_elf32_symbol_spec_t *symbol;
  ctool_u32 offset = ctool_buffer_view(context->rodata).size;
  ctool_status_t status;
  if (context->symbol_count >= context->symbol_capacity) {
    return CTOOL_ERR_INTERNAL;
  }
  status = ctool_buffer_append(context->rodata, initializer->string_bytes);
  if (status != CTOOL_OK) {
    return status;
  }
  symbol = &context->symbols[context->symbol_count];
  status = cemit_make_literal_name(context, &symbol->name);
  if (status != CTOOL_OK) {
    return status;
  }
  symbol->binding = CTOOL_ELF32_BIND_LOCAL;
  symbol->type = CTOOL_ELF32_SYMBOL_OBJECT;
  symbol->visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbol->placement = CTOOL_ELF32_SYMBOL_DEFINED;
  symbol->section = CEMIT_SECTION_RODATA;
  symbol->value = offset;
  symbol->size = initializer->string_bytes.size;
  symbol->alignment = 0u;
  *symbol_out = context->symbol_count++;
  if (context->section_alignment[CEMIT_SECTION_RODATA] < 1u) {
    context->section_alignment[CEMIT_SECTION_RODATA] = 1u;
  }
  return CTOOL_OK;
}

static ctool_status_t cemit_encode_initializer(
    cemit_context_t *context, ctool_u32 initializer_index,
    ctool_u32 section, ctool_u32 offset, ctool_u32 depth);

static ctool_status_t cemit_encode_bit_field(
    cemit_context_t *context, const ctool_c_initializer_t *initializer,
    const ctool_c_member_layout_t *layout, ctool_u32 section,
    ctool_u32 offset) {
  ctool_buffer_t *buffer = cemit_section_buffer(context, section);
  ctool_bytes_t view;
  ctool_u64 storage = 0u;
  ctool_u64 mask;
  ctool_u64 value;
  ctool_u32 index;
  if (buffer == (ctool_buffer_t *)0 || layout->size == 0u ||
      layout->size > 8u || layout->bit_width == 0u ||
      layout->bit_width > 64u ||
      layout->bit_offset > layout->size * 8u ||
      layout->bit_width > layout->size * 8u - layout->bit_offset ||
      (initializer->kind != CTOOL_C_INITIALIZER_ZERO &&
       initializer->kind != CTOOL_C_INITIALIZER_INTEGER)) {
    return cemit_invalid_unit(context, &initializer->location);
  }
  view = ctool_buffer_view(buffer);
  if (offset > view.size || layout->size > view.size - offset) {
    return cemit_invalid_unit(context, &initializer->location);
  }
  for (index = 0u; index < layout->size; index++) {
    storage |= (ctool_u64)view.data[offset + index] << (index * 8u);
  }
  mask = layout->bit_width == 64u
             ? ~(ctool_u64)0u
             : (((ctool_u64)1u << layout->bit_width) - 1u);
  value = initializer->kind == CTOOL_C_INITIALIZER_INTEGER
              ? initializer->integer_bits & mask
              : 0u;
  storage &= ~(mask << layout->bit_offset);
  storage |= value << layout->bit_offset;
  return cemit_patch_integer(context, section, offset, layout->size,
                             storage);
}

static ctool_status_t cemit_encode_list(
    cemit_context_t *context, const ctool_c_initializer_t *initializer,
    ctool_u32 section, ctool_u32 offset, ctool_u32 depth) {
  const ctool_c_type_node_t *type =
      cemit_unwrapped_type(context, initializer->type);
  ctool_u32 edge_index;
  if (type == (const ctool_c_type_node_t *)0) {
    return cemit_invalid_unit(context, &initializer->location);
  }
  for (edge_index = 0u; edge_index < initializer->element_count;
       edge_index++) {
    const ctool_c_initializer_element_t *edge =
        &context->unit->initializer_elements
             [initializer->first_element + edge_index];
    const ctool_c_initializer_t *child =
        &context->unit->initializers[edge->initializer];
    ctool_u32 child_offset;
    ctool_status_t status;
    if (type->kind == CTOOL_C_TYPE_ARRAY) {
      const ctool_c_type_layout_t *element_layout;
      if (type->referenced_type >= context->unit->layout.type_count ||
          edge->subobject >= type->element_count) {
        return cemit_invalid_unit(context, &initializer->location);
      }
      element_layout = &context->unit->layout.types[type->referenced_type];
      if (cemit_multiply_overflows(edge->subobject, element_layout->size) ==
              CTOOL_TRUE ||
          cemit_add_overflows(offset,
                              edge->subobject * element_layout->size) ==
              CTOOL_TRUE) {
        return cemit_invalid_unit(context, &initializer->location);
      }
      child_offset = offset + edge->subobject * element_layout->size;
    } else if (type->kind == CTOOL_C_TYPE_RECORD &&
               type->record_kind == CTOOL_C_RECORD_STRUCT) {
      const ctool_c_record_member_t *member;
      const ctool_c_member_layout_t *member_layout;
      if (type->first_member > context->unit->graph.member_count ||
          type->member_count >
              context->unit->graph.member_count - type->first_member ||
          edge->subobject < type->first_member ||
          edge->subobject - type->first_member >= type->member_count ||
          edge->subobject >= context->unit->layout.member_count) {
        return cemit_invalid_unit(context, &initializer->location);
      }
      member = &context->unit->graph.members[edge->subobject];
      member_layout = &context->unit->layout.members[edge->subobject];
      if (cemit_add_overflows(offset, member_layout->byte_offset) ==
          CTOOL_TRUE) {
        return cemit_invalid_unit(context, &initializer->location);
      }
      child_offset = offset + member_layout->byte_offset;
      if (member->is_bit_field == CTOOL_TRUE) {
        status = cemit_encode_bit_field(context, child, member_layout,
                                        section, child_offset);
        if (status != CTOOL_OK) {
          return status;
        }
        continue;
      }
    } else {
      return cemit_emit_failure(
          context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_EMIT_DIAG_INITIALIZER,
          &initializer->location,
          "CupidC object emission does not yet support this aggregate initializer");
    }
    status = cemit_encode_initializer(context, edge->initializer, section,
                                      child_offset, depth + 1u);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  return CTOOL_OK;
}

static ctool_status_t cemit_encode_initializer(
    cemit_context_t *context, ctool_u32 initializer_index,
    ctool_u32 section, ctool_u32 offset, ctool_u32 depth) {
  const ctool_c_initializer_t *initializer;
  const ctool_c_type_layout_t *layout;
  ctool_buffer_t *buffer;
  ctool_status_t status;
  ctool_u32 symbol = CTOOL_C_AST_NONE;
  ctool_u32 index;
  if (initializer_index >= context->unit->initializer_count ||
      depth > CTOOL_C_PARSE_NESTING_LIMIT) {
    return cemit_invalid_unit(context,
                              (const ctool_c_pp_location_t *)0);
  }
  initializer = &context->unit->initializers[initializer_index];
  layout = &context->unit->layout.types[initializer->type];
  if (initializer->kind == CTOOL_C_INITIALIZER_ZERO) {
    return CTOOL_OK;
  }
  if (initializer->kind == CTOOL_C_INITIALIZER_INTEGER) {
    return cemit_patch_integer(context, section, offset, layout->size,
                               initializer->integer_bits);
  }
  if (initializer->kind == CTOOL_C_INITIALIZER_STRING) {
    buffer = cemit_section_buffer(context, section);
    if (buffer == (ctool_buffer_t *)0 ||
        initializer->string_bytes.size > layout->size) {
      return cemit_invalid_unit(context, &initializer->location);
    }
    for (index = 0u; index < initializer->string_bytes.size; index++) {
      status = ctool_buffer_patch_u8(
          buffer, offset + index, initializer->string_bytes.data[index]);
      if (status != CTOOL_OK) {
        return status;
      }
    }
    return CTOOL_OK;
  }
  if (initializer->kind == CTOOL_C_INITIALIZER_ADDRESS) {
    if (layout->size != 4u) {
      return cemit_invalid_unit(context, &initializer->location);
    }
    if (initializer->address_kind ==
        CTOOL_C_INITIALIZER_ADDRESS_BINDING) {
      status = cemit_ensure_binding_symbol(
          context, initializer->address_reference, &symbol);
    } else {
      status = cemit_add_string_literal(context, initializer, &symbol);
    }
    if (status != CTOOL_OK) {
      return status;
    }
    if (symbol == CTOOL_C_AST_NONE) {
      return CTOOL_ERR_INTERNAL;
    }
    return cemit_add_relocation(context, section, offset, symbol,
                                initializer->address_addend);
  }
  if (initializer->kind == CTOOL_C_INITIALIZER_LIST) {
    return cemit_encode_list(context, initializer, section, offset, depth);
  }
  return cemit_invalid_unit(context, &initializer->location);
}

static ctool_status_t cemit_place_definition(
    cemit_context_t *context, ctool_u32 definition_index) {
  const ctool_c_object_definition_t *definition =
      &context->unit->object_definitions[definition_index];
  const ctool_c_binding_t *binding =
      &context->unit->bindings[definition->binding];
  const ctool_c_type_layout_t *layout =
      &context->unit->layout.types[definition->declared_type];
  ctool_u32 alignment = layout->alignment;
  ctool_u32 section;
  ctool_u32 offset;
  ctool_u32 symbol_index = CTOOL_C_AST_NONE;
  ctool_status_t status;
  ctool_mut_bytes_t reserved;
  if (layout->is_complete_object == CTOOL_FALSE ||
      layout->is_object == CTOOL_FALSE ||
      layout->size == 0u || cemit_power_of_two(alignment) == CTOOL_FALSE) {
    return cemit_invalid_unit(context, &definition->location);
  }
  if (binding->minimum_alignment > alignment) {
    alignment = binding->minimum_alignment;
  }
  if (cemit_power_of_two(alignment) == CTOOL_FALSE) {
    return cemit_invalid_unit(context, &definition->location);
  }
  if (cemit_type_is_const(context, definition->declared_type) == CTOOL_TRUE) {
    section = CEMIT_SECTION_RODATA;
  } else if (context->initializer_is_zero[definition->initializer] ==
             CTOOL_TRUE) {
    section = CEMIT_SECTION_BSS;
  } else {
    section = CEMIT_SECTION_DATA;
  }
  if (section == CEMIT_SECTION_BSS) {
    status = cemit_align_value(context->bss_size, alignment, &offset);
    if (status == CTOOL_OK &&
        cemit_add_overflows(offset, layout->size) == CTOOL_TRUE) {
      status = CTOOL_ERR_OVERFLOW;
    }
    if (status != CTOOL_OK) {
      return status;
    }
    context->bss_size = offset + layout->size;
  } else {
    ctool_buffer_t *buffer = cemit_section_buffer(context, section);
    status = cemit_align_buffer(context, section, alignment);
    if (status != CTOOL_OK) {
      return status;
    }
    status = ctool_buffer_reserve_zero(buffer, layout->size, &offset,
                                       &reserved);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  if (context->section_alignment[section] < alignment) {
    context->section_alignment[section] = alignment;
  }
  status = cemit_ensure_binding_symbol(context, definition->binding,
                                       &symbol_index);
  if (status != CTOOL_OK) {
    return status;
  }
  if (symbol_index == CTOOL_C_AST_NONE) {
    return CTOOL_ERR_INTERNAL;
  }
  context->symbols[symbol_index].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  context->symbols[symbol_index].section = section;
  context->symbols[symbol_index].value = offset;
  context->symbols[symbol_index].size = layout->size;
  context->symbols[symbol_index].alignment = 0u;
  if (section != CEMIT_SECTION_BSS) {
    status = cemit_encode_initializer(
        context, definition->initializer, section, offset, 0u);
  }
  return status;
}

static ctool_status_t cemit_build_sections(
    cemit_context_t *context, ctool_elf32_section_spec_t *sections,
    ctool_u32 *section_count_out) {
  ctool_u32 section_map[CEMIT_SECTION_COUNT];
  ctool_u32 section_count = 0u;
  ctool_u32 logical;
  ctool_u32 index;
  for (logical = 0u; logical < CEMIT_SECTION_COUNT; logical++) {
    ctool_u32 size = logical == CEMIT_SECTION_BSS
                         ? context->bss_size
                         : ctool_buffer_view(
                               cemit_section_buffer(context, logical))
                               .size;
    section_map[logical] = CTOOL_ELF32_NO_SECTION;
    if (size != 0u) {
      ctool_elf32_section_spec_t *section = &sections[section_count];
      section_map[logical] = section_count++;
      section->name = logical == CEMIT_SECTION_RODATA
                          ? ctool_string(".rodata")
                          : logical == CEMIT_SECTION_DATA
                                ? ctool_string(".data")
                                : ctool_string(".bss");
      section->type = logical == CEMIT_SECTION_BSS
                          ? CTOOL_ELF32_SHT_NOBITS
                          : CTOOL_ELF32_SHT_PROGBITS;
      section->flags = logical == CEMIT_SECTION_RODATA
                           ? CTOOL_ELF32_SHF_ALLOC
                           : CTOOL_ELF32_SHF_ALLOC |
                                 CTOOL_ELF32_SHF_WRITE;
      section->alignment = context->section_alignment[logical];
      section->entry_size = 0u;
      section->size = size;
      section->contents = logical == CEMIT_SECTION_BSS
                              ? ctool_bytes((const void *)0, 0u)
                              : ctool_buffer_view(
                                    cemit_section_buffer(context, logical));
    }
  }
  for (index = 0u; index < context->symbol_count; index++) {
    ctool_elf32_symbol_spec_t *symbol = &context->symbols[index];
    if (symbol->placement == CTOOL_ELF32_SYMBOL_DEFINED) {
      if (symbol->section >= CEMIT_SECTION_COUNT ||
          section_map[symbol->section] == CTOOL_ELF32_NO_SECTION) {
        return CTOOL_ERR_INTERNAL;
      }
      symbol->section = section_map[symbol->section];
    } else if (symbol->binding == CTOOL_ELF32_BIND_LOCAL) {
      return cemit_emit_failure(
          context, CTOOL_ERR_INPUT, CTOOL_C_EMIT_DIAG_SYMBOL,
          (const ctool_c_pp_location_t *)0,
          "CupidC found an unresolved internal-linkage symbol");
    }
  }
  for (index = 0u; index < context->relocation_count; index++) {
    ctool_elf32_relocation_spec_t *relocation =
        &context->relocations[index];
    if (relocation->target_section >= CEMIT_SECTION_COUNT ||
        section_map[relocation->target_section] ==
            CTOOL_ELF32_NO_SECTION) {
      return CTOOL_ERR_INTERNAL;
    }
    relocation->target_section = section_map[relocation->target_section];
  }
  *section_count_out = section_count;
  return CTOOL_OK;
}

static ctool_status_t cemit_open_buffers(cemit_context_t *context) {
  const ctool_limits_t *limits = ctool_job_limits(context->job);
  ctool_u32 initial_capacity =
      limits->output_bytes < 256u ? limits->output_bytes : 256u;
  ctool_status_t status = ctool_job_open_buffer(
      context->job, initial_capacity, limits->output_bytes,
      &context->rodata);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(context->job, initial_capacity,
                                   limits->output_bytes, &context->data);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(context->job, initial_capacity,
                                   limits->output_bytes,
                                   &context->object_output);
  }
  return status;
}

ctool_status_t ctool_c_emit_object(
    ctool_job_t *job, const ctool_c_translation_unit_t *unit,
    ctool_buffer_t *output) {
  cemit_context_t context;
  ctool_arena_mark_t mark;
  ctool_elf32_section_spec_t sections[CEMIT_SECTION_COUNT];
  ctool_elf32_object_spec_t object;
  ctool_u32 section_count = 0u;
  ctool_u32 symbol_capacity;
  ctool_u32 index;
  ctool_u32 diagnostic_count;
  ctool_status_t status;
  ctool_status_t rewind_status;
  if (job == (ctool_job_t *)0 ||
      unit == (const ctool_c_translation_unit_t *)0 ||
      output == (ctool_buffer_t *)0 || ctool_buffer_view(output).size != 0u) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  cemit_zero(&context, (ctool_u32)sizeof(context));
  cemit_zero(sections, (ctool_u32)sizeof(sections));
  cemit_zero(&object, (ctool_u32)sizeof(object));
  context.job = job;
  context.unit = unit;
  context.arena = ctool_job_arena(job);
  mark = ctool_arena_mark(context.arena);
  diagnostic_count = ctool_job_diagnostic_count(job);
  status = cemit_validate_unit_shape(&context);
  if (status == CTOOL_OK && unit->function_definition_count != 0u) {
    status = cemit_emit_failure(
        &context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_EMIT_DIAG_UNSUPPORTED,
        &unit->function_definitions[0].location,
        "CupidC object emission does not yet support function definitions");
  }
  if (status == CTOOL_OK &&
      cemit_add_overflows(unit->binding_count, unit->initializer_count) ==
          CTOOL_TRUE) {
    status = CTOOL_ERR_OVERFLOW;
  }
  symbol_capacity = status == CTOOL_OK
                        ? unit->binding_count + unit->initializer_count
                        : 0u;
  context.symbol_capacity = symbol_capacity;
  context.relocation_capacity = unit->initializer_count;
  if (status == CTOOL_OK) {
    status = cemit_alloc_array(
        &context, symbol_capacity,
        (ctool_u32)sizeof(ctool_elf32_symbol_spec_t),
        (void **)&context.symbols);
  }
  if (status == CTOOL_OK) {
    status = cemit_alloc_array(
        &context, unit->initializer_count,
        (ctool_u32)sizeof(ctool_elf32_relocation_spec_t),
        (void **)&context.relocations);
  }
  if (status == CTOOL_OK) {
    status = cemit_alloc_array(&context, unit->binding_count,
                               (ctool_u32)sizeof(ctool_u32),
                               (void **)&context.binding_symbols);
  }
  if (status == CTOOL_OK) {
    status = cemit_alloc_array(&context, unit->binding_count,
                               (ctool_u32)sizeof(ctool_u32),
                               (void **)&context.binding_definitions);
  }
  if (status == CTOOL_OK) {
    status = cemit_alloc_array(&context, unit->binding_count,
                               (ctool_u32)sizeof(ctool_bool),
                               (void **)&context.binding_needed);
  }
  if (status == CTOOL_OK) {
    status = cemit_alloc_array(&context, unit->initializer_count,
                               (ctool_u32)sizeof(ctool_bool),
                               (void **)&context.initializer_is_zero);
  }
  if (status == CTOOL_OK) {
    status = cemit_index_definitions(&context);
  }
  if (status == CTOOL_OK) {
    status = cemit_index_initializers(&context);
  }
  if (status == CTOOL_OK) {
    status = cemit_index_symbols(&context);
  }
  if (status == CTOOL_OK) {
    status = cemit_open_buffers(&context);
  }
  for (index = 0u; status == CTOOL_OK &&
                    index < unit->object_definition_count;
       index++) {
    status = cemit_place_definition(&context, index);
  }
  if (status == CTOOL_OK) {
    status = cemit_build_sections(&context, sections, &section_count);
  }
  if (status == CTOOL_OK) {
    object.sections = sections;
    object.section_count = section_count;
    object.symbols = context.symbols;
    object.symbol_count = context.symbol_count;
    object.relocations = context.relocations;
    object.relocation_count = context.relocation_count;
    status = ctool_elf32_write(job, &object, context.object_output);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_append(output,
                                 ctool_buffer_view(context.object_output));
  }
  if (status != CTOOL_OK && context.failure_reported == CTOOL_FALSE &&
      ctool_job_diagnostic_count(job) == diagnostic_count) {
    if (status == CTOOL_ERR_LIMIT || status == CTOOL_ERR_NO_MEMORY ||
        status == CTOOL_ERR_OVERFLOW) {
      status = cemit_emit_failure(
          &context, status, CTOOL_C_EMIT_DIAG_LIMIT,
          (const ctool_c_pp_location_t *)0,
          "CupidC object emission exceeded a configured resource limit");
    } else {
      status = cemit_emit_failure(
          &context, status, CTOOL_C_EMIT_DIAG_INTERNAL,
          (const ctool_c_pp_location_t *)0,
          "CupidC object emission failed before writing an object");
    }
  }
  ctool_buffer_close(context.object_output);
  ctool_buffer_close(context.data);
  ctool_buffer_close(context.rodata);
  rewind_status = ctool_arena_rewind(context.arena, mark);
  if (status == CTOOL_OK && rewind_status != CTOOL_OK) {
    status = rewind_status;
  }
  return status;
}
