#include "cupidc_type.h"

#define CTYPE_U32_MAX 0xffffffffu
#define CTYPE_U32_MAX_64 0xffffffffull

typedef enum {
  CTYPE_WHITE = 0,
  CTYPE_GRAY,
  CTYPE_BLACK
} ctype_visit_t;

typedef struct {
  ctool_u32 type;
  ctool_u32 next_edge;
} ctype_frame_t;

typedef struct {
  ctool_job_t *job;
  const ctool_c_layout_request_t *request;
  ctool_c_type_layout_t *types;
  ctool_c_member_layout_t *members;
  ctool_arena_mark_t scratch_mark;
  ctool_u8 *visits;
  ctool_u8 *bool_types;
  ctool_u8 *atomic_types;
  ctool_u32 *atomic_alignments;
  ctool_u32 *member_owners;
  ctype_frame_t *frames;
} ctype_context_t;

static void ctype_zero(void *destination, ctool_u32 size) {
  ctool_u8 *bytes = (ctool_u8 *)destination;
  ctool_u32 index;
  for (index = 0u; index < size; index++) {
    bytes[index] = 0u;
  }
}

static void ctype_zero_result(ctool_c_layout_result_t *result) {
  if (result != (ctool_c_layout_result_t *)0) {
    ctype_zero(result, (ctool_u32)sizeof(*result));
  }
}

static ctool_bool ctype_bool_valid(ctool_bool value) {
  return value == CTOOL_FALSE || value == CTOOL_TRUE ? CTOOL_TRUE
                                                      : CTOOL_FALSE;
}

static ctool_bool ctype_power_of_two(ctool_u32 value) {
  return value != 0u && (value & (value - 1u)) == 0u ? CTOOL_TRUE
                                                     : CTOOL_FALSE;
}

static ctool_bool ctype_alignment_valid(ctool_u32 alignment) {
  return alignment == 0u || ctype_power_of_two(alignment) == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool ctype_pack_alignment_valid(ctool_u32 alignment) {
  return alignment == 0u || alignment == 1u || alignment == 2u ||
                 alignment == 4u || alignment == 8u || alignment == 16u
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool ctype_add_overflows(ctool_u32 left, ctool_u32 right) {
  return left > CTYPE_U32_MAX - right ? CTOOL_TRUE : CTOOL_FALSE;
}

static ctool_bool ctype_multiply_overflows(ctool_u32 left,
                                           ctool_u32 right) {
  return left != 0u && right > CTYPE_U32_MAX / left ? CTOOL_TRUE
                                                    : CTOOL_FALSE;
}

static ctool_status_t ctype_align_up(ctool_u32 value, ctool_u32 alignment,
                                     ctool_u32 *result_out) {
  ctool_u32 remainder;
  ctool_u32 addition;
  if (result_out == (ctool_u32 *)0 ||
      ctype_power_of_two(alignment) == CTOOL_FALSE) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  remainder = value & (alignment - 1u);
  addition = remainder == 0u ? 0u : alignment - remainder;
  if (ctype_add_overflows(value, addition) == CTOOL_TRUE) {
    return CTOOL_ERR_OVERFLOW;
  }
  *result_out = value + addition;
  return CTOOL_OK;
}

static ctool_status_t ctype_emit_failure(
    ctool_job_t *job, ctool_status_t status, ctool_u32 code,
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
  emitted = ctool_job_emit(job, &diagnostic);
  return emitted == CTOOL_OK ? status : emitted;
}

static ctool_status_t ctype_fail_request(ctype_context_t *context,
                                         ctool_status_t status,
                                         ctool_u32 code,
                                         const char *message) {
  return ctype_emit_failure(context->job, status, code,
                            &context->request->location, message);
}

static ctool_status_t ctype_fail_type(ctype_context_t *context,
                                      ctool_u32 type, ctool_status_t status,
                                      ctool_u32 code, const char *message) {
  return ctype_emit_failure(context->job, status, code,
                            &context->request->types[type].location, message);
}

static ctool_status_t ctype_fail_member(ctype_context_t *context,
                                        ctool_u32 member,
                                        ctool_status_t status,
                                        ctool_u32 code,
                                        const char *message) {
  return ctype_emit_failure(context->job, status, code,
                            &context->request->members[member].location,
                            message);
}

static ctool_status_t ctype_alloc_array(ctool_arena_t *arena,
                                        ctool_u32 count,
                                        ctool_u32 element_size,
                                        void **allocation_out) {
  if (allocation_out == (void **)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  *allocation_out = (void *)0;
  if (count == 0u) {
    return CTOOL_OK;
  }
  if (ctype_multiply_overflows(count, element_size) == CTOOL_TRUE) {
    return CTOOL_ERR_OVERFLOW;
  }
  return ctool_arena_alloc_zero(arena, count, element_size,
                                (ctool_u32)sizeof(void *), allocation_out);
}

static ctool_status_t ctype_allocate_work(ctype_context_t *context) {
  ctool_arena_t *arena = ctool_job_arena(context->job);
  ctool_status_t status;
  status = ctype_alloc_array(arena, context->request->type_count,
                             (ctool_u32)sizeof(*context->types),
                             (void **)&context->types);
  if (status == CTOOL_OK) {
    status = ctype_alloc_array(arena, context->request->member_count,
                               (ctool_u32)sizeof(*context->members),
                               (void **)&context->members);
  }
  if (status == CTOOL_OK) {
    context->scratch_mark = ctool_arena_mark(arena);
  }
  if (status == CTOOL_OK) {
    status = ctype_alloc_array(arena, context->request->type_count,
                               (ctool_u32)sizeof(*context->visits),
                               (void **)&context->visits);
  }
  if (status == CTOOL_OK) {
    status = ctype_alloc_array(arena, context->request->type_count,
                               (ctool_u32)sizeof(*context->bool_types),
                               (void **)&context->bool_types);
  }
  if (status == CTOOL_OK) {
    status = ctype_alloc_array(arena, context->request->type_count,
                               (ctool_u32)sizeof(*context->atomic_types),
                               (void **)&context->atomic_types);
  }
  if (status == CTOOL_OK) {
    status = ctype_alloc_array(arena, context->request->type_count,
                               (ctool_u32)sizeof(*context->atomic_alignments),
                               (void **)&context->atomic_alignments);
  }
  if (status == CTOOL_OK) {
    status = ctype_alloc_array(arena, context->request->member_count,
                               (ctool_u32)sizeof(*context->member_owners),
                               (void **)&context->member_owners);
  }
  if (status == CTOOL_OK) {
    status = ctype_alloc_array(arena, context->request->type_count,
                               (ctool_u32)sizeof(*context->frames),
                               (void **)&context->frames);
  }
  if (status != CTOOL_OK) {
    return ctype_fail_request(
        context,
        status == CTOOL_ERR_OVERFLOW ? CTOOL_ERR_OVERFLOW : status,
        status == CTOOL_ERR_OVERFLOW ? CTOOL_C_TYPE_DIAG_OVERFLOW
                                     : CTOOL_C_TYPE_DIAG_LIMIT,
        status == CTOOL_ERR_OVERFLOW
            ? "type layout work table exceeds 32-bit size"
            : "type layout work storage limit exceeded");
  }
  return CTOOL_OK;
}

static ctool_bool ctype_kind_is_scalar(ctool_c_type_kind_t kind) {
  return kind >= CTOOL_C_TYPE_VOID && kind <= CTOOL_C_TYPE_LONG_DOUBLE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t ctype_validate_reference(ctype_context_t *context,
                                               ctool_u32 type,
                                               ctool_u32 reference) {
  if (reference >= context->request->type_count) {
    return ctype_fail_type(context, type, CTOOL_ERR_INPUT,
                           CTOOL_C_TYPE_DIAG_INVALID_REFERENCE,
                           "type reference is out of range");
  }
  return CTOOL_OK;
}

static ctool_status_t ctype_validate_function(ctype_context_t *context,
                                              ctool_u32 type) {
  const ctool_c_type_node_t *node = &context->request->types[type];
  ctool_u32 index;
  ctool_status_t status;
  if (node->referenced_type == CTOOL_C_TYPE_NONE ||
      node->referenced_type >= context->request->type_count) {
    return ctype_fail_type(context, type, CTOOL_ERR_INPUT,
                           CTOOL_C_TYPE_DIAG_INVALID_REFERENCE,
                           "type reference is out of range");
  }
  if (node->first_parameter > context->request->parameter_type_count ||
      node->parameter_count >
          context->request->parameter_type_count - node->first_parameter) {
    return ctype_fail_type(context, type, CTOOL_ERR_INPUT,
                           CTOOL_C_TYPE_DIAG_INVALID_REFERENCE,
                           "function parameter slice is out of range");
  }
  if (node->has_prototype == CTOOL_FALSE &&
      (node->parameter_count != 0u || node->variadic == CTOOL_TRUE)) {
    return ctype_fail_type(context, type, CTOOL_ERR_INPUT,
                           CTOOL_C_TYPE_DIAG_INVALID_TYPE,
                           "function without a prototype cannot list parameters");
  }
  if (node->variadic == CTOOL_TRUE && node->parameter_count == 0u) {
    return ctype_fail_type(context, type, CTOOL_ERR_INPUT,
                           CTOOL_C_TYPE_DIAG_INVALID_TYPE,
                           "variadic function requires a named parameter");
  }
  for (index = 0u; index < node->parameter_count; index++) {
    ctool_u32 parameter =
        context->request->parameter_types[node->first_parameter + index];
    if (parameter >= context->request->type_count) {
      return ctype_fail_type(context, type, CTOOL_ERR_INPUT,
                             CTOOL_C_TYPE_DIAG_INVALID_REFERENCE,
                             "function parameter type is out of range");
    }
  }
  status = ctype_validate_reference(context, type, node->referenced_type);
  return status;
}

static ctool_status_t ctype_validate_node(ctype_context_t *context,
                                          ctool_u32 type) {
  const ctool_c_type_node_t *node = &context->request->types[type];
  ctool_status_t status = CTOOL_OK;
  if ((node->qualifiers & ~CTOOL_C_QUAL_ALL) != 0u) {
    return ctype_fail_type(context, type, CTOOL_ERR_INPUT,
                           CTOOL_C_TYPE_DIAG_INVALID_TYPE,
                           "type qualifier mask is invalid");
  }
  if ((node->qualifiers & CTOOL_C_QUAL_ATOMIC) != 0u) {
    if (node->kind == CTOOL_C_TYPE_VOID ||
        node->kind == CTOOL_C_TYPE_FUNCTION ||
        node->kind == CTOOL_C_TYPE_ARRAY) {
      return ctype_fail_type(context, type, CTOOL_ERR_INPUT,
                             CTOOL_C_TYPE_DIAG_INVALID_TYPE,
                             "atomic qualifier requires an object type");
    }
    if (node->kind == CTOOL_C_TYPE_RECORD ||
        node->kind == CTOOL_C_TYPE_VECTOR) {
      return ctype_fail_type(context, type, CTOOL_ERR_UNSUPPORTED,
                             CTOOL_C_TYPE_DIAG_INVALID_TYPE,
                             "atomic aggregate layout is unsupported");
    }
  }
  if (node->kind != CTOOL_C_TYPE_RECORD &&
      node->kind != CTOOL_C_TYPE_ALIGNED &&
      node->explicit_alignment != 0u) {
    return ctype_fail_type(context, type, CTOOL_ERR_INPUT,
                           CTOOL_C_TYPE_DIAG_ALIGNMENT,
                           "explicit type alignment requires an aligned type");
  }
  if (ctype_kind_is_scalar(node->kind) == CTOOL_TRUE) {
    return CTOOL_OK;
  }
  switch (node->kind) {
  case CTOOL_C_TYPE_FUNCTION:
    if (ctype_bool_valid(node->has_prototype) == CTOOL_FALSE ||
        ctype_bool_valid(node->variadic) == CTOOL_FALSE) {
      status = ctype_fail_type(context, type, CTOOL_ERR_INPUT,
                               CTOOL_C_TYPE_DIAG_INVALID_TYPE,
                               "function boolean field is invalid");
    } else {
      status = ctype_validate_function(context, type);
    }
    break;
  case CTOOL_C_TYPE_POINTER:
    status = ctype_validate_reference(context, type, node->referenced_type);
    break;
  case CTOOL_C_TYPE_ARRAY:
    status = ctype_validate_reference(context, type, node->referenced_type);
    if (status != CTOOL_OK) {
      break;
    }
    if (node->array_bound_kind == CTOOL_C_ARRAY_FIXED) {
      if (node->element_count == 0u) {
        status = ctype_fail_type(context, type, CTOOL_ERR_INPUT,
                                 CTOOL_C_TYPE_DIAG_ARRAY,
                                 "array element count must be nonzero");
      }
    } else if (node->array_bound_kind == CTOOL_C_ARRAY_UNSPECIFIED) {
      if (node->element_count != 0u) {
        status = ctype_fail_type(context, type, CTOOL_ERR_INPUT,
                                 CTOOL_C_TYPE_DIAG_ARRAY,
                                 "unspecified array cannot have an element count");
      }
    } else if (node->array_bound_kind == CTOOL_C_ARRAY_VARIABLE) {
      status = ctype_fail_type(context, type, CTOOL_ERR_UNSUPPORTED,
                               CTOOL_C_TYPE_DIAG_ARRAY,
                               "variable array layout is unsupported");
    } else {
      status = ctype_fail_type(context, type, CTOOL_ERR_INPUT,
                               CTOOL_C_TYPE_DIAG_ARRAY,
                               "array bound kind is invalid");
    }
    break;
  case CTOOL_C_TYPE_ENUM:
  case CTOOL_C_TYPE_VECTOR:
    status = ctype_validate_reference(context, type, node->referenced_type);
    break;
  case CTOOL_C_TYPE_ALIGNED:
    status = ctype_validate_reference(context, type, node->referenced_type);
    if (status == CTOOL_OK &&
        (node->explicit_alignment == 0u ||
         ctype_alignment_valid(node->explicit_alignment) == CTOOL_FALSE)) {
      status = ctype_fail_type(context, type, CTOOL_ERR_INPUT,
                               CTOOL_C_TYPE_DIAG_ALIGNMENT,
                               "aligned type requires a power-of-two alignment");
    }
    break;
  case CTOOL_C_TYPE_QUALIFIED:
    status = ctype_validate_reference(context, type, node->referenced_type);
    if (status == CTOOL_OK && node->qualifiers == 0u) {
      status = ctype_fail_type(
          context, type, CTOOL_ERR_INPUT, CTOOL_C_TYPE_DIAG_INVALID_TYPE,
          "qualified type requires at least one qualifier");
    }
    break;
  case CTOOL_C_TYPE_RECORD:
    if (node->record_kind != CTOOL_C_RECORD_STRUCT &&
        node->record_kind != CTOOL_C_RECORD_UNION &&
        node->record_kind != CTOOL_C_RECORD_CLASS) {
      status = ctype_fail_type(context, type, CTOOL_ERR_INPUT,
                               CTOOL_C_TYPE_DIAG_RECORD,
                               "record kind is invalid");
    } else if (ctype_bool_valid(node->record_complete) == CTOOL_FALSE ||
               ctype_bool_valid(node->record_packed) == CTOOL_FALSE) {
      status = ctype_fail_type(context, type, CTOOL_ERR_INPUT,
                               CTOOL_C_TYPE_DIAG_RECORD,
                               "record boolean field is invalid");
    } else if (ctype_alignment_valid(node->explicit_alignment) ==
               CTOOL_FALSE) {
      status = ctype_fail_type(context, type, CTOOL_ERR_INPUT,
                               CTOOL_C_TYPE_DIAG_ALIGNMENT,
                               "alignment must be zero or a power of two");
    } else if (node->first_member > context->request->member_count ||
               node->member_count >
                   context->request->member_count - node->first_member) {
      status = ctype_fail_type(context, type, CTOOL_ERR_INPUT,
                               CTOOL_C_TYPE_DIAG_MEMBER_SLICE,
                               "record member slice is out of range");
    } else if (node->record_complete == CTOOL_FALSE &&
               node->member_count != 0u) {
      status = ctype_fail_type(context, type, CTOOL_ERR_INPUT,
                               CTOOL_C_TYPE_DIAG_RECORD,
                               "incomplete record cannot contain members");
    }
    break;
  default:
    status = ctype_fail_type(context, type, CTOOL_ERR_INPUT,
                             CTOOL_C_TYPE_DIAG_INVALID_TYPE,
                             "type kind is invalid");
    break;
  }
  return status;
}

static ctool_status_t ctype_validate_members(ctype_context_t *context) {
  ctool_u32 type;
  ctool_u32 member;
  for (type = 0u; type < context->request->type_count; type++) {
    const ctool_c_type_node_t *node = &context->request->types[type];
    if (node->kind != CTOOL_C_TYPE_RECORD) {
      continue;
    }
    for (member = 0u; member < node->member_count; member++) {
      ctool_u32 index = node->first_member + member;
      if (context->member_owners[index] != 0u) {
        return ctype_fail_type(context, type, CTOOL_ERR_INPUT,
                               CTOOL_C_TYPE_DIAG_MEMBER_OVERLAP,
                               "record member slices overlap");
      }
      context->member_owners[index] = type + 1u;
    }
  }
  for (member = 0u; member < context->request->member_count; member++) {
    const ctool_c_record_member_t *spec = &context->request->members[member];
    if (context->member_owners[member] == 0u) {
      return ctype_fail_member(context, member, CTOOL_ERR_INPUT,
                               CTOOL_C_TYPE_DIAG_MEMBER_SLICE,
                               "record member is outside every record slice");
    }
    if (spec->type >= context->request->type_count) {
      return ctype_fail_member(context, member, CTOOL_ERR_INPUT,
                               CTOOL_C_TYPE_DIAG_INVALID_REFERENCE,
                               "record member type is out of range");
    }
    if ((spec->name.data == (const char *)0 && spec->name.size != 0u) ||
        ctype_bool_valid(spec->is_bit_field) == CTOOL_FALSE ||
        ctype_bool_valid(spec->anonymous) == CTOOL_FALSE ||
        ctype_bool_valid(spec->member_packed) == CTOOL_FALSE) {
      return ctype_fail_member(context, member, CTOOL_ERR_INPUT,
                               CTOOL_C_TYPE_DIAG_RECORD,
                               "record member description is invalid");
    }
    if (ctype_pack_alignment_valid(spec->pack_alignment) == CTOOL_FALSE) {
      return ctype_fail_member(
          context, member, CTOOL_ERR_INPUT, CTOOL_C_TYPE_DIAG_ALIGNMENT,
          "pack alignment must be 0, 1, 2, 4, 8, or 16");
    }
    if (ctype_alignment_valid(spec->explicit_alignment) == CTOOL_FALSE) {
      return ctype_fail_member(context, member, CTOOL_ERR_INPUT,
                               CTOOL_C_TYPE_DIAG_ALIGNMENT,
                               "alignment must be zero or a power of two");
    }
    if (spec->is_bit_field == CTOOL_FALSE && spec->bit_width != 0u) {
      return ctype_fail_member(context, member, CTOOL_ERR_INPUT,
                               CTOOL_C_TYPE_DIAG_BIT_FIELD,
                               "ordinary member cannot have a bit-field width");
    }
  }
  return CTOOL_OK;
}

static ctool_status_t ctype_validate_request(ctype_context_t *context) {
  ctool_u32 type;
  ctool_status_t status;
  if (context->request->types == (const ctool_c_type_node_t *)0 &&
      context->request->type_count != 0u) {
    return ctype_fail_request(context, CTOOL_ERR_INVALID_ARGUMENT,
                              CTOOL_C_TYPE_DIAG_INVALID_REQUEST,
                              "type array is missing");
  }
  if (context->request->members == (const ctool_c_record_member_t *)0 &&
      context->request->member_count != 0u) {
    return ctype_fail_request(context, CTOOL_ERR_INVALID_ARGUMENT,
                              CTOOL_C_TYPE_DIAG_INVALID_REQUEST,
                              "record member array is missing");
  }
  if (context->request->parameter_types == (const ctool_u32 *)0 &&
      context->request->parameter_type_count != 0u) {
    return ctype_fail_request(context, CTOOL_ERR_INVALID_ARGUMENT,
                              CTOOL_C_TYPE_DIAG_INVALID_REQUEST,
                              "function parameter type array is missing");
  }
  status = ctype_allocate_work(context);
  if (status != CTOOL_OK) {
    return status;
  }
  for (type = 0u; type < context->request->type_count; type++) {
    status = ctype_validate_node(context, type);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  return ctype_validate_members(context);
}

static void ctype_set_layout(ctool_c_type_layout_t *layout, ctool_u32 size,
                             ctool_u32 alignment, ctool_bool complete,
                             ctool_bool object, ctool_bool integer,
                             ctool_bool signed_value) {
  layout->size = size;
  layout->alignment = alignment;
  layout->is_complete_object = complete;
  layout->is_object = object;
  layout->is_integer = integer;
  layout->is_signed = signed_value;
}

static ctool_status_t ctype_layout_scalar(ctype_context_t *context,
                                          ctool_u32 type) {
  ctool_c_type_layout_t *layout = &context->types[type];
  ctool_u32 atomic_alignment;
  context->bool_types[type] =
      context->request->types[type].kind == CTOOL_C_TYPE_BOOL ? 1u : 0u;
  switch (context->request->types[type].kind) {
  case CTOOL_C_TYPE_VOID:
    ctype_set_layout(layout, 0u, 1u, CTOOL_FALSE, CTOOL_FALSE, CTOOL_FALSE,
                     CTOOL_FALSE);
    break;
  case CTOOL_C_TYPE_BOOL:
    ctype_set_layout(layout, 1u, 1u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE,
                     CTOOL_FALSE);
    break;
  case CTOOL_C_TYPE_CHAR:
  case CTOOL_C_TYPE_SIGNED_CHAR:
    ctype_set_layout(layout, 1u, 1u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE,
                     CTOOL_TRUE);
    break;
  case CTOOL_C_TYPE_UNSIGNED_CHAR:
    ctype_set_layout(layout, 1u, 1u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE,
                     CTOOL_FALSE);
    break;
  case CTOOL_C_TYPE_SIGNED_SHORT:
    ctype_set_layout(layout, 2u, 2u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE,
                     CTOOL_TRUE);
    break;
  case CTOOL_C_TYPE_UNSIGNED_SHORT:
    ctype_set_layout(layout, 2u, 2u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE,
                     CTOOL_FALSE);
    break;
  case CTOOL_C_TYPE_SIGNED_INT:
  case CTOOL_C_TYPE_SIGNED_LONG:
    ctype_set_layout(layout, 4u, 4u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE,
                     CTOOL_TRUE);
    break;
  case CTOOL_C_TYPE_UNSIGNED_INT:
  case CTOOL_C_TYPE_UNSIGNED_LONG:
    ctype_set_layout(layout, 4u, 4u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE,
                     CTOOL_FALSE);
    break;
  case CTOOL_C_TYPE_SIGNED_LONG_LONG:
    ctype_set_layout(layout, 8u, 4u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE,
                     CTOOL_TRUE);
    break;
  case CTOOL_C_TYPE_UNSIGNED_LONG_LONG:
    ctype_set_layout(layout, 8u, 4u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE,
                     CTOOL_FALSE);
    break;
  case CTOOL_C_TYPE_FLOAT:
    ctype_set_layout(layout, 4u, 4u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE,
                     CTOOL_FALSE);
    break;
  case CTOOL_C_TYPE_DOUBLE:
    ctype_set_layout(layout, 8u, 4u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE,
                     CTOOL_FALSE);
    break;
  case CTOOL_C_TYPE_LONG_DOUBLE:
    ctype_set_layout(layout, 12u, 4u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE,
                     CTOOL_FALSE);
    break;
  default:
    return ctype_fail_type(context, type, CTOOL_ERR_INTERNAL,
                           CTOOL_C_TYPE_DIAG_INVALID_TYPE,
                           "internal scalar layout mismatch");
  }
  atomic_alignment = layout->alignment;
  if (context->request->types[type].kind == CTOOL_C_TYPE_VOID) {
    atomic_alignment = 0u;
  }
  if (context->request->types[type].kind ==
          CTOOL_C_TYPE_SIGNED_LONG_LONG ||
      context->request->types[type].kind ==
          CTOOL_C_TYPE_UNSIGNED_LONG_LONG ||
      context->request->types[type].kind == CTOOL_C_TYPE_DOUBLE) {
    atomic_alignment = 8u;
  }
  context->atomic_alignments[type] = atomic_alignment;
  context->atomic_types[type] =
      (context->request->types[type].qualifiers & CTOOL_C_QUAL_ATOMIC) != 0u
          ? 1u
          : 0u;
  if (context->atomic_types[type] != 0u &&
      atomic_alignment > layout->alignment) {
    layout->alignment = atomic_alignment;
  }
  return CTOOL_OK;
}

static ctool_u32 ctype_effective_member_alignment(
    const ctool_c_type_node_t *record,
    const ctool_c_record_member_t *member,
    const ctool_c_type_layout_t *type) {
  ctool_u32 alignment = type->alignment;
  if (record->record_packed == CTOOL_TRUE ||
      member->member_packed == CTOOL_TRUE) {
    alignment = 1u;
  }
  if (member->explicit_alignment > alignment) {
    alignment = member->explicit_alignment;
  }
  if (member->pack_alignment != 0u &&
      alignment > member->pack_alignment) {
    alignment = member->pack_alignment;
  }
  return alignment;
}

static ctool_bool ctype_is_unspecified_array(ctype_context_t *context,
                                             ctool_u32 type) {
  const ctool_c_type_node_t *node = &context->request->types[type];
  return node->kind == CTOOL_C_TYPE_ARRAY &&
                 node->array_bound_kind == CTOOL_C_ARRAY_UNSPECIFIED
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t ctype_layout_bit_field(
    ctype_context_t *context, ctool_u32 record_type, ctool_u32 member_index,
    ctool_u64 *bit_cursor, ctool_u32 *record_alignment,
    ctool_u32 *union_size) {
  const ctool_c_type_node_t *record =
      &context->request->types[record_type];
  const ctool_c_record_member_t *member =
      &context->request->members[member_index];
  const ctool_c_type_layout_t *base = &context->types[member->type];
  ctool_c_member_layout_t *layout = &context->members[member_index];
  ctool_u32 alignment;
  ctool_u64 storage_bits;
  ctool_u64 alignment_bits;
  ctool_u64 unit_start;
  ctool_u64 position;
  ctool_u64 end;
  ctool_u64 maximum_width;
  ctool_bool named = member->name.size != 0u ? CTOOL_TRUE : CTOOL_FALSE;
  if (base->is_integer == CTOOL_FALSE || base->is_complete_object == CTOOL_FALSE ||
      base->size == 0u || base->size > 8u) {
    return ctype_fail_member(context, member_index, CTOOL_ERR_INPUT,
                             CTOOL_C_TYPE_DIAG_BIT_FIELD,
                             "bit-field requires a complete integer type");
  }
  storage_bits = (ctool_u64)base->size * 8ull;
  maximum_width = context->bool_types[member->type] != 0u ? 1ull : storage_bits;
  if ((ctool_u64)member->bit_width > maximum_width) {
    return ctype_fail_member(context, member_index, CTOOL_ERR_INPUT,
                             CTOOL_C_TYPE_DIAG_BIT_FIELD,
                             "bit-field width exceeds its storage type");
  }
  if (member->bit_width == 0u && named == CTOOL_TRUE) {
    return ctype_fail_member(context, member_index, CTOOL_ERR_INPUT,
                             CTOOL_C_TYPE_DIAG_BIT_FIELD,
                             "zero-width bit-field must be unnamed");
  }
  alignment = ctype_effective_member_alignment(record, member, base);
  layout->size = base->size;
  layout->alignment = alignment;
  layout->bit_width = member->bit_width;
  if (record->record_kind == CTOOL_C_RECORD_UNION) {
    layout->byte_offset = 0u;
    layout->bit_offset = 0u;
    if (member->bit_width != 0u) {
      ctool_u32 occupied = (member->bit_width + 7u) / 8u;
      if (named == CTOOL_TRUE && occupied < alignment) {
        occupied = alignment;
      }
      if (occupied > *union_size) {
        *union_size = occupied;
      }
    }
  } else if (member->bit_width == 0u) {
    ctool_u32 byte_cursor;
    ctool_u32 aligned;
    ctool_u32 zero_alignment = base->alignment;
    ctool_u64 rounded = (*bit_cursor + 7ull) / 8ull;
    if (member->explicit_alignment > zero_alignment) {
      zero_alignment = member->explicit_alignment;
    }
    if (rounded > CTYPE_U32_MAX_64) {
      return ctype_fail_member(context, member_index, CTOOL_ERR_OVERFLOW,
                               CTOOL_C_TYPE_DIAG_OVERFLOW,
                               "type layout exceeds 32-bit size");
    }
    byte_cursor = (ctool_u32)rounded;
    if (ctype_align_up(byte_cursor, zero_alignment, &aligned) != CTOOL_OK) {
      return ctype_fail_member(context, member_index, CTOOL_ERR_OVERFLOW,
                               CTOOL_C_TYPE_DIAG_OVERFLOW,
                               "type layout exceeds 32-bit size");
    }
    *bit_cursor = (ctool_u64)aligned * 8ull;
    layout->byte_offset = aligned;
    layout->bit_offset = 0u;
  } else {
    alignment_bits = (ctool_u64)alignment * 8ull;
    position = *bit_cursor;
    if (member->explicit_alignment != 0u &&
        (member->pack_alignment == 0u ||
         member->explicit_alignment <= member->pack_alignment)) {
      ctool_u64 byte_position = (position + 7ull) / 8ull;
      ctool_u32 aligned;
      if (byte_position > CTYPE_U32_MAX_64 ||
          ctype_align_up((ctool_u32)byte_position,
                         member->explicit_alignment, &aligned) != CTOOL_OK) {
        return ctype_fail_member(context, member_index, CTOOL_ERR_OVERFLOW,
                                 CTOOL_C_TYPE_DIAG_OVERFLOW,
                                 "type layout exceeds 32-bit size");
      }
      position = (ctool_u64)aligned * 8ull;
    }
    unit_start = (position / alignment_bits) * alignment_bits;
    if (position + (ctool_u64)member->bit_width > unit_start + storage_bits) {
      ctool_u64 byte_position = (position + 7ull) / 8ull;
      ctool_u32 aligned;
      if (byte_position > CTYPE_U32_MAX_64 ||
          ctype_align_up((ctool_u32)byte_position, alignment, &aligned) !=
              CTOOL_OK) {
        return ctype_fail_member(context, member_index, CTOOL_ERR_OVERFLOW,
                                 CTOOL_C_TYPE_DIAG_OVERFLOW,
                                 "type layout exceeds 32-bit size");
      }
      unit_start = (ctool_u64)aligned * 8ull;
      position = unit_start;
    }
    end = position + (ctool_u64)member->bit_width;
    if (unit_start / 8ull > CTYPE_U32_MAX_64 ||
        position - unit_start > CTYPE_U32_MAX_64) {
      return ctype_fail_member(context, member_index, CTOOL_ERR_OVERFLOW,
                               CTOOL_C_TYPE_DIAG_OVERFLOW,
                               "type layout exceeds 32-bit size");
    }
    layout->byte_offset = (ctool_u32)(unit_start / 8ull);
    layout->bit_offset = (ctool_u32)(position - unit_start);
    *bit_cursor = end;
  }
  if (named == CTOOL_TRUE && alignment > *record_alignment) {
    *record_alignment = alignment;
  }
  return CTOOL_OK;
}

static ctool_status_t ctype_layout_record(ctype_context_t *context,
                                          ctool_u32 type) {
  const ctool_c_type_node_t *record = &context->request->types[type];
  ctool_c_type_layout_t *result = &context->types[type];
  ctool_u64 bit_cursor = 0ull;
  ctool_u32 union_size = 0u;
  ctool_u32 record_alignment = 1u;
  ctool_u32 local;
  ctool_status_t status;
  if (record->record_complete == CTOOL_FALSE) {
    ctype_set_layout(result, 0u, 1u, CTOOL_FALSE, CTOOL_TRUE, CTOOL_FALSE,
                     CTOOL_FALSE);
    return CTOOL_OK;
  }
  for (local = 0u; local < record->member_count; local++) {
    ctool_u32 index = record->first_member + local;
    const ctool_c_record_member_t *member = &context->request->members[index];
    const ctool_c_type_layout_t *member_type = &context->types[member->type];
    ctool_c_member_layout_t *member_layout = &context->members[index];
    ctool_u32 alignment;
    ctool_u32 byte_cursor;
    ctool_u32 offset;
    ctool_u32 end;
    ctool_bool flexible = ctype_is_unspecified_array(context, member->type);
    if (flexible == CTOOL_TRUE) {
      if (record->record_kind != CTOOL_C_RECORD_STRUCT ||
          local + 1u != record->member_count) {
        return ctype_fail_member(
            context, index, CTOOL_ERR_INPUT,
            CTOOL_C_TYPE_DIAG_FLEXIBLE_ARRAY,
            local + 1u != record->member_count
                ? "flexible array member must be last"
                : "flexible array member requires a structure");
      }
    } else if (member_type->is_object == CTOOL_FALSE ||
               member_type->is_complete_object == CTOOL_FALSE) {
      return ctype_fail_member(context, index, CTOOL_ERR_INPUT,
                               CTOOL_C_TYPE_DIAG_INCOMPLETE,
                               "record member has incomplete type");
    }
    if (member->is_bit_field == CTOOL_TRUE) {
      status = ctype_layout_bit_field(context, type, index, &bit_cursor,
                                      &record_alignment, &union_size);
      if (status != CTOOL_OK) {
        return status;
      }
      continue;
    }
    alignment = ctype_effective_member_alignment(record, member, member_type);
    member_layout->size = member_type->size;
    member_layout->alignment = alignment;
    if (record->record_kind == CTOOL_C_RECORD_UNION) {
      member_layout->byte_offset = 0u;
      if (member_type->size > union_size) {
        union_size = member_type->size;
      }
    } else {
      ctool_u64 rounded = (bit_cursor + 7ull) / 8ull;
      if (rounded > CTYPE_U32_MAX_64) {
        return ctype_fail_member(context, index, CTOOL_ERR_OVERFLOW,
                                 CTOOL_C_TYPE_DIAG_OVERFLOW,
                                 "type layout exceeds 32-bit size");
      }
      byte_cursor = (ctool_u32)rounded;
      status = ctype_align_up(byte_cursor, alignment, &offset);
      if (status != CTOOL_OK ||
          ctype_add_overflows(offset, member_type->size) == CTOOL_TRUE) {
        return ctype_fail_member(context, index, CTOOL_ERR_OVERFLOW,
                                 CTOOL_C_TYPE_DIAG_OVERFLOW,
                                 "type layout exceeds 32-bit size");
      }
      end = offset + member_type->size;
      member_layout->byte_offset = offset;
      bit_cursor = (ctool_u64)end * 8ull;
    }
    if (alignment > record_alignment) {
      record_alignment = alignment;
    }
  }
  if (record->explicit_alignment > record_alignment) {
    record_alignment = record->explicit_alignment;
  }
  if (record->record_kind == CTOOL_C_RECORD_UNION) {
    status = ctype_align_up(union_size, record_alignment, &union_size);
    if (status != CTOOL_OK) {
      return ctype_fail_type(context, type, CTOOL_ERR_OVERFLOW,
                             CTOOL_C_TYPE_DIAG_OVERFLOW,
                             "type layout exceeds 32-bit size");
    }
    ctype_set_layout(result, union_size, record_alignment, CTOOL_TRUE,
                     CTOOL_TRUE, CTOOL_FALSE, CTOOL_FALSE);
  } else {
    ctool_u64 rounded = (bit_cursor + 7ull) / 8ull;
    ctool_u32 size;
    if (rounded > CTYPE_U32_MAX_64 ||
        ctype_align_up((ctool_u32)rounded, record_alignment, &size) !=
            CTOOL_OK) {
      return ctype_fail_type(context, type, CTOOL_ERR_OVERFLOW,
                             CTOOL_C_TYPE_DIAG_OVERFLOW,
                             "type layout exceeds 32-bit size");
    }
    ctype_set_layout(result, size, record_alignment, CTOOL_TRUE, CTOOL_TRUE,
                     CTOOL_FALSE, CTOOL_FALSE);
  }
  return CTOOL_OK;
}

static ctool_status_t ctype_layout_node(ctype_context_t *context,
                                        ctool_u32 type) {
  const ctool_c_type_node_t *node = &context->request->types[type];
  ctool_c_type_layout_t *layout = &context->types[type];
  const ctool_c_type_layout_t *referenced;
  ctool_bool introduces_atomic;
  ctool_u32 atomic_alignment;
  ctool_u32 size;
  if (ctype_kind_is_scalar(node->kind) == CTOOL_TRUE) {
    return ctype_layout_scalar(context, type);
  }
  switch (node->kind) {
  case CTOOL_C_TYPE_FUNCTION:
    ctype_set_layout(layout, 0u, 1u, CTOOL_FALSE, CTOOL_FALSE, CTOOL_FALSE,
                     CTOOL_FALSE);
    return CTOOL_OK;
  case CTOOL_C_TYPE_POINTER:
    ctype_set_layout(layout, 4u, 4u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE,
                     CTOOL_FALSE);
    context->atomic_alignments[type] = 4u;
    context->atomic_types[type] =
        (node->qualifiers & CTOOL_C_QUAL_ATOMIC) != 0u ? 1u : 0u;
    return CTOOL_OK;
  case CTOOL_C_TYPE_ARRAY:
    referenced = &context->types[node->referenced_type];
    if (referenced->is_object == CTOOL_FALSE ||
        referenced->is_complete_object == CTOOL_FALSE) {
      return ctype_fail_type(context, type, CTOOL_ERR_INPUT,
                             CTOOL_C_TYPE_DIAG_INCOMPLETE,
                             "array element type is incomplete");
    }
    if (referenced->size % referenced->alignment != 0u) {
      return ctype_fail_type(
          context, type, CTOOL_ERR_INPUT, CTOOL_C_TYPE_DIAG_ARRAY,
          "array element size is not a multiple of its alignment");
    }
    if (node->array_bound_kind == CTOOL_C_ARRAY_UNSPECIFIED) {
      ctype_set_layout(layout, 0u, referenced->alignment, CTOOL_FALSE,
                       CTOOL_TRUE, CTOOL_FALSE, CTOOL_FALSE);
      return CTOOL_OK;
    }
    if (ctype_multiply_overflows(referenced->size, node->element_count) ==
        CTOOL_TRUE) {
      return ctype_fail_type(context, type, CTOOL_ERR_OVERFLOW,
                             CTOOL_C_TYPE_DIAG_OVERFLOW,
                             "type layout exceeds 32-bit size");
    }
    size = referenced->size * node->element_count;
    ctype_set_layout(layout, size, referenced->alignment, CTOOL_TRUE,
                     CTOOL_TRUE, CTOOL_FALSE, CTOOL_FALSE);
    return CTOOL_OK;
  case CTOOL_C_TYPE_ENUM:
    referenced = &context->types[node->referenced_type];
    if (referenced->is_integer == CTOOL_FALSE ||
        referenced->is_complete_object == CTOOL_FALSE) {
      return ctype_fail_type(context, type, CTOOL_ERR_INPUT,
                             CTOOL_C_TYPE_DIAG_INVALID_TYPE,
                             "enum requires a complete integer type");
    }
    ctype_set_layout(layout, referenced->size, referenced->alignment,
                     CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE,
                     referenced->is_signed);
    context->atomic_alignments[type] =
        layout->size == 8u ? 8u : layout->alignment;
    context->atomic_types[type] =
        (node->qualifiers & CTOOL_C_QUAL_ATOMIC) != 0u ? 1u : 0u;
    if (context->atomic_types[type] != 0u &&
        context->atomic_alignments[type] > layout->alignment) {
      layout->alignment = context->atomic_alignments[type];
    }
    return CTOOL_OK;
  case CTOOL_C_TYPE_VECTOR:
    referenced = &context->types[node->referenced_type];
    if (!((context->request->types[node->referenced_type].kind ==
               CTOOL_C_TYPE_FLOAT &&
           node->element_count == 4u) ||
          (context->request->types[node->referenced_type].kind ==
               CTOOL_C_TYPE_DOUBLE &&
           node->element_count == 2u))) {
      return ctype_fail_type(context, type, CTOOL_ERR_UNSUPPORTED,
                             CTOOL_C_TYPE_DIAG_INVALID_TYPE,
                             "Cupid vector must be float4 or double2");
    }
    (void)referenced;
    ctype_set_layout(layout, 16u, 16u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE,
                     CTOOL_FALSE);
    return CTOOL_OK;
  case CTOOL_C_TYPE_RECORD:
    return ctype_layout_record(context, type);
  case CTOOL_C_TYPE_ALIGNED:
    referenced = &context->types[node->referenced_type];
    if (referenced->is_object == CTOOL_FALSE ||
        referenced->is_complete_object == CTOOL_FALSE) {
      return ctype_fail_type(context, type, CTOOL_ERR_INPUT,
                             CTOOL_C_TYPE_DIAG_INCOMPLETE,
                             "aligned type requires a complete object type");
    }
    *layout = *referenced;
    introduces_atomic =
        context->atomic_types[node->referenced_type] == 0u &&
                (node->qualifiers & CTOOL_C_QUAL_ATOMIC) != 0u
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    context->atomic_types[type] =
        context->atomic_types[node->referenced_type] != 0u ||
                (node->qualifiers & CTOOL_C_QUAL_ATOMIC) != 0u
            ? 1u
            : 0u;
    if (introduces_atomic == CTOOL_TRUE) {
      atomic_alignment = context->atomic_alignments[node->referenced_type];
      if (atomic_alignment == 0u) {
        return ctype_fail_type(
            context, type, CTOOL_ERR_UNSUPPORTED,
            CTOOL_C_TYPE_DIAG_INVALID_TYPE,
            "atomic aligned type requires a supported scalar, pointer, or enum type");
      }
      layout->alignment = node->explicit_alignment > atomic_alignment
                              ? node->explicit_alignment
                              : atomic_alignment;
    } else {
      layout->alignment = node->explicit_alignment;
    }
    context->bool_types[type] = context->bool_types[node->referenced_type];
    context->atomic_alignments[type] =
        context->atomic_alignments[node->referenced_type];
    return CTOOL_OK;
  case CTOOL_C_TYPE_QUALIFIED:
    referenced = &context->types[node->referenced_type];
    *layout = *referenced;
    context->bool_types[type] = context->bool_types[node->referenced_type];
    context->atomic_alignments[type] =
        context->atomic_alignments[node->referenced_type];
    introduces_atomic =
        context->atomic_types[node->referenced_type] == 0u &&
                (node->qualifiers & CTOOL_C_QUAL_ATOMIC) != 0u
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    context->atomic_types[type] =
        context->atomic_types[node->referenced_type] != 0u ||
                (node->qualifiers & CTOOL_C_QUAL_ATOMIC) != 0u
            ? 1u
            : 0u;
    if (introduces_atomic == CTOOL_TRUE) {
      if (context->atomic_alignments[type] == 0u) {
        return ctype_fail_type(
            context, type, CTOOL_ERR_UNSUPPORTED,
            CTOOL_C_TYPE_DIAG_INVALID_TYPE,
            "atomic qualified type requires a supported scalar, pointer, or enum type");
      }
      if (context->atomic_alignments[type] > layout->alignment) {
        layout->alignment = context->atomic_alignments[type];
      }
    }
    return CTOOL_OK;
  default:
    return ctype_fail_type(context, type, CTOOL_ERR_INTERNAL,
                           CTOOL_C_TYPE_DIAG_INVALID_TYPE,
                           "internal type layout mismatch");
  }
}

static ctool_bool ctype_next_strong_edge(
    ctype_context_t *context, ctool_u32 type, ctool_u32 edge,
    ctool_u32 *dependency_out, const ctool_c_pp_location_t **location_out) {
  const ctool_c_type_node_t *node = &context->request->types[type];
  if (node->kind == CTOOL_C_TYPE_ARRAY || node->kind == CTOOL_C_TYPE_ENUM ||
      node->kind == CTOOL_C_TYPE_VECTOR ||
      node->kind == CTOOL_C_TYPE_ALIGNED ||
      node->kind == CTOOL_C_TYPE_QUALIFIED) {
    if (edge == 0u) {
      *dependency_out = node->referenced_type;
      *location_out = &node->location;
      return CTOOL_TRUE;
    }
    return CTOOL_FALSE;
  }
  if (node->kind == CTOOL_C_TYPE_RECORD &&
      node->record_complete == CTOOL_TRUE && edge < node->member_count) {
    ctool_u32 member = node->first_member + edge;
    *dependency_out = context->request->members[member].type;
    *location_out = &context->request->members[member].location;
    return CTOOL_TRUE;
  }
  return CTOOL_FALSE;
}

static ctool_status_t ctype_layout_graph(ctype_context_t *context) {
  ctool_u32 root;
  for (root = 0u; root < context->request->type_count; root++) {
    ctool_u32 depth;
    if (context->visits[root] == (ctool_u8)CTYPE_BLACK) {
      continue;
    }
    depth = 1u;
    context->frames[0].type = root;
    context->frames[0].next_edge = 0u;
    context->visits[root] = (ctool_u8)CTYPE_GRAY;
    while (depth != 0u) {
      ctype_frame_t *frame = &context->frames[depth - 1u];
      ctool_u32 dependency = 0u;
      const ctool_c_pp_location_t *edge_location =
          (const ctool_c_pp_location_t *)0;
      if (ctype_next_strong_edge(context, frame->type, frame->next_edge,
                                 &dependency, &edge_location) == CTOOL_TRUE) {
        frame->next_edge++;
        if (context->visits[dependency] == (ctool_u8)CTYPE_GRAY) {
          return ctype_emit_failure(
              context->job, CTOOL_ERR_INPUT, CTOOL_C_TYPE_DIAG_CYCLE,
              edge_location, "type graph contains a by-value cycle");
        }
        if (context->visits[dependency] == (ctool_u8)CTYPE_WHITE) {
          context->frames[depth].type = dependency;
          context->frames[depth].next_edge = 0u;
          context->visits[dependency] = (ctool_u8)CTYPE_GRAY;
          depth++;
        }
        continue;
      }
      {
        ctool_status_t status = ctype_layout_node(context, frame->type);
        if (status != CTOOL_OK) {
          return status;
        }
      }
      context->visits[frame->type] = (ctool_u8)CTYPE_BLACK;
      depth--;
    }
  }
  return CTOOL_OK;
}

ctool_status_t ctool_c_layout_types(ctool_job_t *job,
                                     const ctool_c_layout_request_t *request,
                                     ctool_c_layout_result_t *result_out) {
  ctype_context_t context;
  ctool_arena_t *arena;
  ctool_arena_mark_t mark;
  ctool_status_t status;
  ctype_zero_result(result_out);
  if (job == (ctool_job_t *)0 || result_out == (ctool_c_layout_result_t *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  arena = ctool_job_arena(job);
  mark = ctool_arena_mark(arena);
  ctype_zero(&context, (ctool_u32)sizeof(context));
  context.job = job;
  context.request = request;
  if (request == (const ctool_c_layout_request_t *)0) {
    status = ctype_emit_failure(job, CTOOL_ERR_INVALID_ARGUMENT,
                                CTOOL_C_TYPE_DIAG_INVALID_REQUEST,
                                (const ctool_c_pp_location_t *)0,
                                "layout request is missing");
  } else {
    status = ctype_validate_request(&context);
    if (status == CTOOL_OK) {
      status = ctype_layout_graph(&context);
    }
  }
  if (status != CTOOL_OK) {
    ctool_status_t rewind_status = ctool_arena_rewind(arena, mark);
    ctype_zero_result(result_out);
    return rewind_status == CTOOL_OK ? status : rewind_status;
  }
  status = ctool_arena_rewind(arena, context.scratch_mark);
  if (status != CTOOL_OK) {
    ctool_status_t rewind_status;
    status = ctype_emit_failure(
        job, CTOOL_ERR_INTERNAL, CTOOL_C_TYPE_DIAG_INTERNAL,
        &request->location, "type layout scratch rewind failed");
    rewind_status = ctool_arena_rewind(arena, mark);
    ctype_zero_result(result_out);
    return rewind_status == CTOOL_OK ? status : rewind_status;
  }
  result_out->types = context.types;
  result_out->type_count = request->type_count;
  result_out->members = context.members;
  result_out->member_count = request->member_count;
  return CTOOL_OK;
}
