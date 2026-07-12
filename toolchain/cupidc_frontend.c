#include "cupidc_frontend.h"

#define CFRONT_U32_MAX 0xffffffffu
#define CFRONT_NONE 0xffffffffu
#define CFRONT_GNU_DEFAULT_ALIGNMENT 16u

typedef struct {
  ctool_buffer_t *storage;
  ctool_u32 element_size;
  ctool_u32 count;
} cfront_vector_t;

typedef struct {
  cfront_vector_t versions;
  ctool_buffer_t *logical_map;
  ctool_u32 count;
} cfront_type_store_t;

typedef enum {
  CFRONT_D_NAME = 1,
  CFRONT_D_POINTER,
  CFRONT_D_ARRAY,
  CFRONT_D_FUNCTION
} cfront_declarator_kind_t;

typedef struct {
  cfront_declarator_kind_t kind;
  ctool_u32 child;
  ctool_string_t name;
  ctool_c_pp_location_t location;
  ctool_c_pp_location_t physical_location;
  ctool_u32 qualifiers;
  ctool_c_array_bound_kind_t array_bound_kind;
  ctool_u32 element_count;
  ctool_u32 parameter_head;
  ctool_u32 parameter_count;
  ctool_bool has_prototype;
  ctool_bool variadic;
} cfront_declarator_t;

typedef struct {
  ctool_u32 qualifiers;
  ctool_c_pp_location_t location;
  ctool_c_pp_location_t physical_location;
} cfront_pointer_spec_t;

typedef struct {
  ctool_c_parameter_t parameter;
  ctool_u32 type;
  ctool_u32 previous;
} cfront_pending_parameter_t;

typedef struct {
  ctool_c_record_member_t member;
  ctool_u32 previous;
} cfront_pending_member_t;

typedef struct {
  ctool_string_t name;
  ctool_c_pp_location_t location;
  ctool_c_pp_location_t physical_location;
} cfront_visible_name_t;

typedef enum {
  CFRONT_STORAGE_NONE = 0,
  CFRONT_STORAGE_TYPEDEF,
  CFRONT_STORAGE_EXTERN,
  CFRONT_STORAGE_STATIC,
  CFRONT_STORAGE_AUTO,
  CFRONT_STORAGE_REGISTER
} cfront_storage_t;

static ctool_c_storage_class_t cfront_public_storage(
    cfront_storage_t storage) {
  switch (storage) {
  case CFRONT_STORAGE_TYPEDEF:
    return CTOOL_C_STORAGE_TYPEDEF;
  case CFRONT_STORAGE_EXTERN:
    return CTOOL_C_STORAGE_EXTERN;
  case CFRONT_STORAGE_STATIC:
    return CTOOL_C_STORAGE_STATIC;
  case CFRONT_STORAGE_AUTO:
    return CTOOL_C_STORAGE_AUTO;
  case CFRONT_STORAGE_REGISTER:
    return CTOOL_C_STORAGE_REGISTER;
  case CFRONT_STORAGE_NONE:
  default:
    return CTOOL_C_STORAGE_NONE;
  }
}

typedef struct {
  ctool_bool packed;
  const ctool_c_pp_token_t *packed_token;
  ctool_bool has_alignment;
  ctool_u32 alignment;
  const ctool_c_pp_token_t *alignment_token;
  ctool_bool noreturn;
  const ctool_c_pp_token_t *noreturn_token;
} cfront_attributes_t;

typedef struct {
  ctool_u32 attributes;
  ctool_u32 function_declaration_flags;
  ctool_u32 minimum_alignment;
} cfront_binding_semantics_t;

typedef struct {
  ctool_u32 type;
  cfront_storage_t storage;
  ctool_u32 function_declaration_flags;
  const ctool_c_pp_token_t *inline_token;
  ctool_u32 pack_alignment;
  ctool_c_pp_location_t location;
  ctool_c_pp_location_t physical_location;
  ctool_bool anonymous_record_definition;
  ctool_bool empty_declaration_valid;
  const ctool_c_pp_token_t *block_tag_specifier_token;
  cfront_attributes_t attributes;
} cfront_specifiers_t;

typedef enum {
  CFRONT_INTEGER_SIGNED_32 = 1,
  CFRONT_INTEGER_UNSIGNED_32,
  CFRONT_INTEGER_SIGNED_64,
  CFRONT_INTEGER_UNSIGNED_64
} cfront_integer_kind_t;

typedef struct {
  ctool_u64 bits;
  cfront_integer_kind_t kind;
} cfront_integer_t;

typedef struct {
  ctool_u32 expression;
  ctool_u32 type;
  ctool_bool is_lvalue;
  ctool_bool is_bit_field;
  ctool_u32 bit_width;
  ctool_bool address_forbidden;
} cfront_expression_value_t;

typedef struct {
  ctool_u32 member;
  ctool_u32 parent;
} cfront_member_search_item_t;

typedef struct {
  ctool_c_type_kind_t kind;
  ctool_u32 type;
  ctool_u32 rank;
  ctool_u32 width;
  ctool_bool is_unsigned;
} cfront_integer_type_t;

typedef struct {
  ctool_job_t *job;
  const ctool_c_pp_result_t *tape;
  const ctool_c_parse_request_t *request;
  ctool_u32 position;
  cfront_type_store_t types;
  cfront_vector_t members;
  cfront_vector_t parameter_types;
  cfront_vector_t parameters;
  cfront_vector_t bindings;
  cfront_vector_t tags;
  cfront_vector_t declarators;
  cfront_vector_t pointer_specs;
  cfront_vector_t pending_parameters;
  cfront_vector_t pending_members;
  cfront_vector_t temporary_indices;
  cfront_vector_t visible_names;
  cfront_vector_t prototype_names;
  cfront_vector_t prototype_binding_marks;
  cfront_vector_t prototype_name_marks;
  cfront_vector_t enum_binding_copies;
  cfront_vector_t flexible_seen;
  cfront_vector_t function_definitions;
  cfront_vector_t block_bindings;
  cfront_vector_t active_block_binding_indices;
  cfront_vector_t block_scope_marks;
  cfront_vector_t statements;
  cfront_vector_t statement_children;
  cfront_vector_t expressions;
  cfront_vector_t expression_children;
  ctool_u32 syntax_depth;
  ctool_u32 constant_evaluation_suppression_depth;
  ctool_u32 prototype_scope_depth;
  ctool_u32 prototype_binding_mark;
  ctool_u32 prototype_tag_mark;
  ctool_u32 prototype_name_mark;
  ctool_u32 active_function_type;
  ctool_bool in_function_body;
  ctool_u32 scalar_types[CTOOL_C_TYPE_LONG_DOUBLE + 1u];
} cfront_context_t;

static void cfront_zero(void *destination, ctool_u32 size) {
  ctool_u8 *bytes = (ctool_u8 *)destination;
  ctool_u32 index;
  for (index = 0u; index < size; index++) {
    bytes[index] = 0u;
  }
}

static void cfront_copy(void *destination, const void *source,
                        ctool_u32 size) {
  ctool_u8 *out = (ctool_u8 *)destination;
  const ctool_u8 *in = (const ctool_u8 *)source;
  ctool_u32 index;
  for (index = 0u; index < size; index++) {
    out[index] = in[index];
  }
}

static ctool_bool cfront_multiply_overflows(ctool_u32 left,
                                            ctool_u32 right) {
  return left != 0u && right > CFRONT_U32_MAX / left ? CTOOL_TRUE
                                                     : CTOOL_FALSE;
}

static ctool_bool cfront_bool_valid(ctool_bool value) {
  return value == CTOOL_FALSE || value == CTOOL_TRUE ? CTOOL_TRUE
                                                      : CTOOL_FALSE;
}

static ctool_bool cfront_string_equal(ctool_string_t left,
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

static ctool_bool cfront_string_literal(ctool_string_t value,
                                        const char *literal) {
  return cfront_string_equal(value, ctool_string(literal));
}

static ctool_u32 cfront_read_le32(const ctool_u8 *bytes) {
  return (ctool_u32)bytes[0] | ((ctool_u32)bytes[1] << 8u) |
         ((ctool_u32)bytes[2] << 16u) | ((ctool_u32)bytes[3] << 24u);
}

static ctool_status_t cfront_vector_open(cfront_context_t *context,
                                         cfront_vector_t *vector,
                                         ctool_u32 element_size) {
  const ctool_limits_t *limits = ctool_job_limits(context->job);
  ctool_u32 initial =
      limits->output_bytes < 256u ? limits->output_bytes : 256u;
  ctool_status_t status;
  cfront_zero(vector, (ctool_u32)sizeof(*vector));
  vector->element_size = element_size;
  if (initial == 0u) {
    return CTOOL_ERR_LIMIT;
  }
  status = ctool_job_open_buffer(context->job, initial, limits->output_bytes,
                                 &vector->storage);
  return status;
}

static void cfront_vector_close(cfront_vector_t *vector) {
  if (vector->storage != (ctool_buffer_t *)0) {
    ctool_buffer_close(vector->storage);
  }
  cfront_zero(vector, (ctool_u32)sizeof(*vector));
}

static ctool_status_t cfront_vector_append(cfront_vector_t *vector,
                                           const void *value,
                                           ctool_u32 *index_out) {
  ctool_status_t status;
  if (vector == (cfront_vector_t *)0 || value == (const void *)0 ||
      vector->storage == (ctool_buffer_t *)0 ||
      vector->count == CFRONT_U32_MAX) {
    return vector != (cfront_vector_t *)0 &&
                   vector->count == CFRONT_U32_MAX
               ? CTOOL_ERR_OVERFLOW
               : CTOOL_ERR_INVALID_ARGUMENT;
  }
  status = ctool_buffer_append(
      vector->storage, ctool_bytes(value, vector->element_size));
  if (status != CTOOL_OK) {
    return status;
  }
  if (index_out != (ctool_u32 *)0) {
    *index_out = vector->count;
  }
  vector->count++;
  return CTOOL_OK;
}

static ctool_status_t cfront_vector_get(const cfront_vector_t *vector,
                                        ctool_u32 index, void *value_out) {
  ctool_bytes_t bytes;
  ctool_u32 offset;
  if (vector == (const cfront_vector_t *)0 || value_out == (void *)0 ||
      index >= vector->count ||
      cfront_multiply_overflows(index, vector->element_size) == CTOOL_TRUE) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  offset = index * vector->element_size;
  bytes = ctool_buffer_view(vector->storage);
  if (offset > bytes.size || vector->element_size > bytes.size - offset) {
    return CTOOL_ERR_INTERNAL;
  }
  cfront_copy(value_out, bytes.data + offset, vector->element_size);
  return CTOOL_OK;
}

static ctool_status_t cfront_vector_replace(cfront_context_t *context,
                                            cfront_vector_t *vector,
                                            ctool_u32 index,
                                            const void *value) {
  const ctool_limits_t *limits;
  ctool_buffer_t *replacement = (ctool_buffer_t *)0;
  ctool_bytes_t bytes;
  ctool_u32 offset;
  ctool_u32 suffix_offset;
  ctool_u32 initial;
  ctool_status_t status;
  if (context == (cfront_context_t *)0 || vector == (cfront_vector_t *)0 ||
      value == (const void *)0 || vector->storage == (ctool_buffer_t *)0 ||
      index >= vector->count ||
      cfront_multiply_overflows(index, vector->element_size) == CTOOL_TRUE) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  bytes = ctool_buffer_view(vector->storage);
  offset = index * vector->element_size;
  if (offset > bytes.size || vector->element_size > bytes.size - offset) {
    return CTOOL_ERR_INTERNAL;
  }
  suffix_offset = offset + vector->element_size;
  limits = ctool_job_limits(context->job);
  initial = bytes.size < 256u ? bytes.size : 256u;
  if (initial == 0u) {
    initial = 1u;
  }
  status = ctool_job_open_buffer(context->job, initial,
                                 limits->output_bytes, &replacement);
  if (status == CTOOL_OK && offset != 0u) {
    status = ctool_buffer_append(replacement,
                                 ctool_bytes(bytes.data, offset));
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_append(
        replacement, ctool_bytes(value, vector->element_size));
  }
  if (status == CTOOL_OK && suffix_offset < bytes.size) {
    status = ctool_buffer_append(
        replacement, ctool_bytes(bytes.data + suffix_offset,
                                 bytes.size - suffix_offset));
  }
  if (status == CTOOL_OK) {
    ctool_buffer_close(vector->storage);
    vector->storage = replacement;
  } else {
    ctool_buffer_close(replacement);
  }
  return status;
}

static ctool_u32 cfront_vector_mark(const cfront_vector_t *vector) {
  return vector->count;
}

static ctool_status_t cfront_vector_rewind(cfront_vector_t *vector,
                                           ctool_u32 count) {
  ctool_u32 bytes;
  ctool_status_t status;
  if (count > vector->count ||
      cfront_multiply_overflows(count, vector->element_size) == CTOOL_TRUE) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  bytes = count * vector->element_size;
  status = ctool_buffer_rewind(vector->storage, bytes);
  if (status == CTOOL_OK) {
    vector->count = count;
  }
  return status;
}

static ctool_status_t cfront_type_store_open(cfront_context_t *context) {
  const ctool_limits_t *limits = ctool_job_limits(context->job);
  ctool_u32 initial =
      limits->output_bytes < 256u ? limits->output_bytes : 256u;
  ctool_status_t status = cfront_vector_open(
      context, &context->types.versions,
      (ctool_u32)sizeof(ctool_c_type_node_t));
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(context->job, initial,
                                   limits->output_bytes,
                                   &context->types.logical_map);
  }
  return status;
}

static void cfront_type_store_close(cfront_type_store_t *types) {
  cfront_vector_close(&types->versions);
  if (types->logical_map != (ctool_buffer_t *)0) {
    ctool_buffer_close(types->logical_map);
  }
  cfront_zero(types, (ctool_u32)sizeof(*types));
}

static ctool_status_t cfront_type_append(cfront_context_t *context,
                                         const ctool_c_type_node_t *node,
                                         ctool_u32 *type_out) {
  ctool_u32 version;
  ctool_status_t status;
  status = cfront_vector_append(&context->types.versions, node, &version);
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_le32(context->types.logical_map, version);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  if (type_out != (ctool_u32 *)0) {
    *type_out = context->types.count;
  }
  context->types.count++;
  return CTOOL_OK;
}

static ctool_status_t cfront_type_get(const cfront_context_t *context,
                                      ctool_u32 type,
                                      ctool_c_type_node_t *node_out) {
  ctool_bytes_t map;
  ctool_u32 offset;
  ctool_u32 version;
  if (type >= context->types.count ||
      cfront_multiply_overflows(type, 4u) == CTOOL_TRUE) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  offset = type * 4u;
  map = ctool_buffer_view(context->types.logical_map);
  if (offset > map.size || 4u > map.size - offset) {
    return CTOOL_ERR_INTERNAL;
  }
  version = cfront_read_le32(map.data + offset);
  return cfront_vector_get(&context->types.versions, version, node_out);
}

static ctool_status_t cfront_type_update(cfront_context_t *context,
                                         ctool_u32 type,
                                         const ctool_c_type_node_t *node) {
  ctool_u32 version;
  ctool_u32 offset;
  ctool_status_t status;
  if (type >= context->types.count ||
      cfront_multiply_overflows(type, 4u) == CTOOL_TRUE) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  status = cfront_vector_append(&context->types.versions, node, &version);
  if (status != CTOOL_OK) {
    return status;
  }
  offset = type * 4u;
  return ctool_buffer_patch_le32(context->types.logical_map, offset, version);
}

static void cfront_close_scratch(cfront_context_t *context) {
  cfront_vector_close(&context->expression_children);
  cfront_vector_close(&context->expressions);
  cfront_vector_close(&context->statement_children);
  cfront_vector_close(&context->statements);
  cfront_vector_close(&context->block_scope_marks);
  cfront_vector_close(&context->active_block_binding_indices);
  cfront_vector_close(&context->block_bindings);
  cfront_vector_close(&context->function_definitions);
  cfront_vector_close(&context->flexible_seen);
  cfront_vector_close(&context->enum_binding_copies);
  cfront_vector_close(&context->prototype_name_marks);
  cfront_vector_close(&context->prototype_binding_marks);
  cfront_vector_close(&context->prototype_names);
  cfront_vector_close(&context->visible_names);
  cfront_vector_close(&context->temporary_indices);
  cfront_vector_close(&context->pending_members);
  cfront_vector_close(&context->pending_parameters);
  cfront_vector_close(&context->pointer_specs);
  cfront_vector_close(&context->declarators);
  cfront_vector_close(&context->tags);
  cfront_vector_close(&context->bindings);
  cfront_vector_close(&context->parameters);
  cfront_vector_close(&context->parameter_types);
  cfront_vector_close(&context->members);
  cfront_type_store_close(&context->types);
}

static ctool_status_t cfront_open_scratch(cfront_context_t *context) {
  ctool_status_t status = cfront_type_store_open(context);
#define CFRONT_OPEN_VECTOR(FIELD, TYPE)                                      \
  if (status == CTOOL_OK) {                                                  \
    status = cfront_vector_open(context, &context->FIELD,                    \
                                (ctool_u32)sizeof(TYPE));                     \
  }
  CFRONT_OPEN_VECTOR(members, ctool_c_record_member_t)
  CFRONT_OPEN_VECTOR(parameter_types, ctool_u32)
  CFRONT_OPEN_VECTOR(parameters, ctool_c_parameter_t)
  CFRONT_OPEN_VECTOR(bindings, ctool_c_binding_t)
  CFRONT_OPEN_VECTOR(tags, ctool_c_tag_t)
  CFRONT_OPEN_VECTOR(declarators, cfront_declarator_t)
  CFRONT_OPEN_VECTOR(pointer_specs, cfront_pointer_spec_t)
  CFRONT_OPEN_VECTOR(pending_parameters, cfront_pending_parameter_t)
  CFRONT_OPEN_VECTOR(pending_members, cfront_pending_member_t)
  CFRONT_OPEN_VECTOR(temporary_indices, ctool_u32)
  CFRONT_OPEN_VECTOR(visible_names, cfront_visible_name_t)
  CFRONT_OPEN_VECTOR(prototype_names, ctool_string_t)
  CFRONT_OPEN_VECTOR(prototype_binding_marks, ctool_u32)
  CFRONT_OPEN_VECTOR(prototype_name_marks, ctool_u32)
  CFRONT_OPEN_VECTOR(enum_binding_copies, ctool_c_binding_t)
  CFRONT_OPEN_VECTOR(flexible_seen, ctool_u8)
  CFRONT_OPEN_VECTOR(function_definitions, ctool_c_function_definition_t)
  CFRONT_OPEN_VECTOR(block_bindings, ctool_c_block_binding_t)
  CFRONT_OPEN_VECTOR(active_block_binding_indices, ctool_u32)
  CFRONT_OPEN_VECTOR(block_scope_marks, ctool_u32)
  CFRONT_OPEN_VECTOR(statements, ctool_c_statement_t)
  CFRONT_OPEN_VECTOR(statement_children, ctool_u32)
  CFRONT_OPEN_VECTOR(expressions, ctool_c_expression_t)
  CFRONT_OPEN_VECTOR(expression_children, ctool_u32)
#undef CFRONT_OPEN_VECTOR
  return status;
}

static const ctool_c_pp_token_t *cfront_token(const cfront_context_t *context,
                                               ctool_u32 position) {
  return context->tape->tokens != (const ctool_c_pp_token_t *)0 &&
                 position < context->tape->token_count
             ? &context->tape->tokens[position]
             : (const ctool_c_pp_token_t *)0;
}

static const ctool_c_pp_token_t *cfront_peek(const cfront_context_t *context) {
  return cfront_token(context, context->position);
}

static ctool_bool cfront_token_is(const ctool_c_pp_token_t *token,
                                  const char *spelling) {
  return token != (const ctool_c_pp_token_t *)0 &&
                 cfront_string_literal(token->spelling, spelling) == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cfront_peek_is(const cfront_context_t *context,
                                 const char *spelling) {
  return cfront_token_is(cfront_peek(context), spelling);
}

static ctool_bool cfront_reserved_identifier(
    const cfront_context_t *context, const ctool_c_pp_token_t *token) {
  static const char *const c11_keywords[] = {
      "auto",       "break",      "case",        "char",
      "const",      "continue",   "default",     "do",
      "double",     "else",       "enum",        "extern",
      "float",      "for",        "goto",        "if",
      "inline",     "int",        "long",        "register",
      "restrict",   "return",     "short",       "signed",
      "sizeof",     "static",     "struct",      "switch",
      "typedef",    "union",      "unsigned",    "void",
      "volatile",   "while",      "_Alignas",    "_Alignof",
      "_Atomic",    "_Bool",      "_Complex",    "_Generic",
      "_Imaginary", "_Noreturn",  "_Pragma",     "_Static_assert",
      "_Thread_local"};
  static const char *const cupid_keywords[] = {
      "asm",    "class", "new",    "del",    "reg",   "noreg",
      "U0",     "U8",    "U16",    "U32",    "U64",   "I8",
      "I16",    "I32",   "I64",    "float4", "double2", "Bool",
      "bool"};
  ctool_u32 index;
  if (token == (const ctool_c_pp_token_t *)0 ||
      token->kind != CTOOL_C_PP_TOKEN_IDENTIFIER) {
    return CTOOL_FALSE;
  }
  for (index = 0u;
       index < (ctool_u32)(sizeof(c11_keywords) / sizeof(c11_keywords[0]));
       index++) {
    if (cfront_string_literal(token->spelling, c11_keywords[index]) ==
        CTOOL_TRUE) {
      return CTOOL_TRUE;
    }
  }
  if (context->request->mode == CTOOL_C_PP_MODE_CUPID) {
    for (index = 0u;
         index <
         (ctool_u32)(sizeof(cupid_keywords) / sizeof(cupid_keywords[0]));
         index++) {
      if (cfront_string_literal(token->spelling, cupid_keywords[index]) ==
          CTOOL_TRUE) {
        return CTOOL_TRUE;
      }
    }
  }
  return CTOOL_FALSE;
}

static const ctool_c_pp_token_t *cfront_advance(cfront_context_t *context) {
  const ctool_c_pp_token_t *token = cfront_peek(context);
  if (token != (const ctool_c_pp_token_t *)0) {
    context->position++;
  }
  return token;
}

static const ctool_c_pp_location_t *cfront_failure_location(
    const cfront_context_t *context, const ctool_c_pp_token_t *token) {
  const ctool_limits_t *limits = ctool_job_limits(context->job);
  if (token != (const ctool_c_pp_token_t *)0 &&
      (token->location.path.data != (const char *)0 ||
       token->location.path.size == 0u) &&
      token->location.path.size <= limits->path_bytes) {
    return &token->location;
  }
  if (context->tape->tokens != (const ctool_c_pp_token_t *)0 &&
      context->tape->token_count != 0u) {
    const ctool_c_pp_location_t *location =
        &context->tape->tokens[context->tape->token_count - 1u].location;
    if ((location->path.data != (const char *)0 || location->path.size == 0u) &&
        location->path.size <= limits->path_bytes) {
      return location;
    }
  }
  return (const ctool_c_pp_location_t *)0;
}

static ctool_status_t cfront_emit_failure_string(
    cfront_context_t *context, ctool_status_t status, ctool_u32 code,
    const ctool_c_pp_token_t *token, ctool_string_t message) {
  const ctool_c_pp_location_t *location;
  ctool_diagnostic_t diagnostic;
  ctool_status_t emitted;
  if (status == CTOOL_ERR_INPUT &&
      token != (const ctool_c_pp_token_t *)0 &&
      token->kind == CTOOL_C_PP_TOKEN_CUPID_EXE) {
    status = CTOOL_ERR_UNSUPPORTED;
    code = CTOOL_C_PARSE_DIAG_UNSUPPORTED;
    message = ctool_string(
        "Cupid #exe execution is outside the declaration frontend");
  }
  location = cfront_failure_location(context, token);
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
  diagnostic.message = message;
  emitted = ctool_job_emit(context->job, &diagnostic);
  return emitted == CTOOL_OK ? status : emitted;
}

static ctool_status_t cfront_emit_failure(
    cfront_context_t *context, ctool_status_t status, ctool_u32 code,
    const ctool_c_pp_token_t *token, const char *message) {
  return cfront_emit_failure_string(context, status, code, token,
                                    ctool_string(message));
}

static ctool_status_t cfront_validate_function_specifier_context(
    cfront_context_t *context, const cfront_specifiers_t *specifiers,
    ctool_bool function_declaration_allowed, const char *message) {
  if (specifiers->function_declaration_flags == 0u ||
      function_declaration_allowed == CTOOL_TRUE) {
    return CTOOL_OK;
  }
  return cfront_emit_failure(
      context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS,
      specifiers->inline_token, message);
}

static ctool_status_t cfront_enter_syntax(
    cfront_context_t *context, const ctool_c_pp_token_t *token) {
  if (context->syntax_depth >= CTOOL_C_PARSE_NESTING_LIMIT) {
    return cfront_emit_failure(
        context, CTOOL_ERR_LIMIT, CTOOL_C_PARSE_DIAG_LIMIT, token,
        "declaration syntax exceeds the public nesting limit");
  }
  context->syntax_depth++;
  return CTOOL_OK;
}

static void cfront_leave_syntax(cfront_context_t *context) {
  if (context->syntax_depth != 0u) {
    context->syntax_depth--;
  }
}

static ctool_status_t cfront_expected(cfront_context_t *context,
                                      const char *spelling) {
  if (cfront_peek_is(context, spelling) == CTOOL_FALSE) {
    return cfront_emit_failure(context, CTOOL_ERR_INPUT,
                               CTOOL_C_PARSE_DIAG_EXPECTED_TOKEN,
                               cfront_peek(context),
                               "expected declaration token is missing");
  }
  (void)cfront_advance(context);
  return CTOOL_OK;
}

static ctool_status_t cfront_storage_failure(cfront_context_t *context,
                                              ctool_status_t status) {
  return cfront_emit_failure(
      context, status,
      status == CTOOL_ERR_OVERFLOW ? CTOOL_C_PARSE_DIAG_OVERFLOW
                                   : CTOOL_C_PARSE_DIAG_LIMIT,
      cfront_peek(context),
      status == CTOOL_ERR_OVERFLOW
          ? "declaration frontend storage exceeds 32-bit size"
          : "declaration frontend storage limit exceeded");
}

static void cfront_node_init(ctool_c_type_node_t *node,
                             ctool_c_type_kind_t kind,
                             const ctool_c_pp_token_t *token) {
  cfront_zero(node, (ctool_u32)sizeof(*node));
  node->kind = kind;
  if (token != (const ctool_c_pp_token_t *)0) {
    node->location = token->location;
    node->physical_location = token->physical_location;
  }
  node->referenced_type = CTOOL_C_TYPE_NONE;
  node->array_bound_kind = CTOOL_C_ARRAY_FIXED;
  node->record_kind = CTOOL_C_RECORD_STRUCT;
}

static ctool_status_t cfront_scalar_type(cfront_context_t *context,
                                         ctool_c_type_kind_t kind,
                                         const ctool_c_pp_token_t *token,
                                         ctool_u32 *type_out) {
  ctool_c_type_node_t node;
  ctool_status_t status;
  if (kind < CTOOL_C_TYPE_VOID || kind > CTOOL_C_TYPE_LONG_DOUBLE) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  if (context->scalar_types[(ctool_u32)kind] != CFRONT_NONE) {
    *type_out = context->scalar_types[(ctool_u32)kind];
    return CTOOL_OK;
  }
  cfront_node_init(&node, kind, token);
  status = cfront_type_append(context, &node, type_out);
  if (status == CTOOL_OK) {
    context->scalar_types[(ctool_u32)kind] = *type_out;
  }
  return status;
}

static ctool_status_t cfront_qualified_type(
    cfront_context_t *context, ctool_u32 base, ctool_u32 qualifiers,
    const ctool_c_pp_token_t *token, ctool_u32 *type_out) {
  ctool_c_type_node_t node;
  if (qualifiers == 0u) {
    *type_out = base;
    return CTOOL_OK;
  }
  cfront_node_init(&node, CTOOL_C_TYPE_QUALIFIED, token);
  node.referenced_type = base;
  node.qualifiers = qualifiers;
  return cfront_type_append(context, &node, type_out);
}

static ctool_status_t cfront_aligned_type(
    cfront_context_t *context, ctool_u32 base, ctool_u32 alignment,
    const ctool_c_pp_token_t *token, ctool_u32 *type_out) {
  ctool_c_type_node_t node;
  cfront_node_init(&node, CTOOL_C_TYPE_ALIGNED, token);
  node.referenced_type = base;
  node.explicit_alignment = alignment;
  return cfront_type_append(context, &node, type_out);
}

static ctool_status_t cfront_underlying_type(
    const cfront_context_t *context, ctool_u32 type, ctool_u32 *base_out,
    ctool_u32 *qualifiers_out, ctool_c_type_node_t *node_out) {
  ctool_u32 qualifiers = 0u;
  ctool_u32 traversed = 0u;
  ctool_c_type_node_t node;
  ctool_status_t status;
  for (;;) {
    status = cfront_type_get(context, type, &node);
    if (status != CTOOL_OK) {
      return status;
    }
    if (node.kind != CTOOL_C_TYPE_QUALIFIED &&
        node.kind != CTOOL_C_TYPE_ALIGNED) {
      *base_out = type;
      *qualifiers_out = qualifiers;
      *node_out = node;
      return CTOOL_OK;
    }
    qualifiers |= node.qualifiers;
    type = node.referenced_type;
    traversed++;
    if (traversed > context->types.count) {
      return CTOOL_ERR_INTERNAL;
    }
  }
}

static ctool_status_t cfront_unqualified_parameter_type(
    cfront_context_t *context, ctool_u32 type, ctool_u32 *type_out) {
  ctool_c_type_node_t node;
  ctool_u32 preserved_qualifiers = 0u;
  ctool_u32 traversed = 0u;
  ctool_status_t status;
  for (;;) {
    status = cfront_type_get(context, type, &node);
    if (status != CTOOL_OK) {
      return status;
    }
    if (node.kind != CTOOL_C_TYPE_QUALIFIED) {
      break;
    }
    preserved_qualifiers |= node.qualifiers & CTOOL_C_QUAL_ATOMIC;
    type = node.referenced_type;
    traversed++;
    if (traversed > context->types.count) {
      return CTOOL_ERR_INTERNAL;
    }
  }
  preserved_qualifiers |= node.qualifiers & CTOOL_C_QUAL_ATOMIC;
  if (node.qualifiers == preserved_qualifiers) {
    *type_out = type;
    return CTOOL_OK;
  }
  node.qualifiers = preserved_qualifiers;
  return cfront_type_append(context, &node, type_out);
}

static ctool_status_t cfront_type_is_complete_object_now(
    const cfront_context_t *context, ctool_u32 type,
    ctool_bool *complete_out) {
  ctool_u32 traversed = 0u;
  for (;;) {
    ctool_u32 base;
    ctool_u32 qualifiers;
    ctool_c_type_node_t node;
    ctool_status_t status = cfront_underlying_type(
        context, type, &base, &qualifiers, &node);
    (void)base;
    (void)qualifiers;
    if (status != CTOOL_OK) {
      return status;
    }
    if (node.kind == CTOOL_C_TYPE_VOID ||
        node.kind == CTOOL_C_TYPE_FUNCTION) {
      *complete_out = CTOOL_FALSE;
      return CTOOL_OK;
    }
    if (node.kind == CTOOL_C_TYPE_RECORD) {
      *complete_out = node.record_complete;
      return CTOOL_OK;
    }
    if (node.kind == CTOOL_C_TYPE_ARRAY) {
      if (node.array_bound_kind != CTOOL_C_ARRAY_FIXED) {
        *complete_out = CTOOL_FALSE;
        return CTOOL_OK;
      }
      type = node.referenced_type;
      traversed++;
      if (traversed > context->types.count) {
        return CTOOL_ERR_INTERNAL;
      }
      continue;
    }
    *complete_out = CTOOL_TRUE;
    return CTOOL_OK;
  }
}

static ctool_status_t cfront_type_contains_flexible_array(
    cfront_context_t *context, ctool_u32 type, ctool_bool *contains_out) {
  ctool_u32 stack_mark = cfront_vector_mark(&context->temporary_indices);
  ctool_u32 seen_mark = cfront_vector_mark(&context->flexible_seen);
  ctool_u32 work_budget;
  ctool_u32 index;
  ctool_u8 unseen = 0u;
  ctool_status_t status = CTOOL_OK;
  *contains_out = CTOOL_FALSE;
  if (context->types.count > CFRONT_U32_MAX - context->members.count ||
      context->types.count + context->members.count == CFRONT_U32_MAX) {
    return CTOOL_ERR_OVERFLOW;
  }
  work_budget = context->types.count + context->members.count + 1u;
  for (index = 0u; status == CTOOL_OK && index < context->types.count;
       index++) {
    status = cfront_vector_append(&context->flexible_seen, &unseen,
                                  (ctool_u32 *)0);
  }
  if (status == CTOOL_OK) {
    status = cfront_vector_append(&context->temporary_indices, &type,
                                  (ctool_u32 *)0);
  }
  while (status == CTOOL_OK &&
         context->temporary_indices.count > stack_mark) {
    ctool_u32 current;
    ctool_u32 base;
    ctool_u32 qualifiers;
    ctool_c_type_node_t node;
    if (work_budget == 0u) {
      status = CTOOL_ERR_INTERNAL;
      break;
    }
    work_budget--;
    status = cfront_vector_get(&context->temporary_indices,
                               context->temporary_indices.count - 1u,
                               &current);
    if (status == CTOOL_OK) {
      status = cfront_vector_rewind(&context->temporary_indices,
                                    context->temporary_indices.count - 1u);
    }
    if (status == CTOOL_OK) {
      status = cfront_underlying_type(context, current, &base, &qualifiers,
                                      &node);
    }
    (void)base;
    (void)qualifiers;
    if (status != CTOOL_OK) {
      break;
    }
    {
      ctool_u8 seen;
      status = cfront_vector_get(&context->flexible_seen, base, &seen);
      if (status != CTOOL_OK) {
        break;
      }
      if (seen != 0u) {
        continue;
      }
      status = ctool_buffer_patch_u8(context->flexible_seen.storage,
                                     base, 1u);
      if (status != CTOOL_OK) {
        break;
      }
    }
    if (node.kind == CTOOL_C_TYPE_POINTER ||
        node.kind == CTOOL_C_TYPE_FUNCTION) {
      continue;
    }
    if (node.kind == CTOOL_C_TYPE_ARRAY) {
      if (node.array_bound_kind == CTOOL_C_ARRAY_UNSPECIFIED) {
        *contains_out = CTOOL_TRUE;
        break;
      }
      status = cfront_vector_append(&context->temporary_indices,
                                    &node.referenced_type,
                                    (ctool_u32 *)0);
    } else if (node.kind == CTOOL_C_TYPE_RECORD &&
               node.record_complete == CTOOL_TRUE) {
      ctool_u32 member_offset;
      for (member_offset = 0u;
           status == CTOOL_OK && member_offset < node.member_count;
           member_offset++) {
        ctool_c_record_member_t member;
        status = cfront_vector_get(&context->members,
                                   node.first_member + member_offset,
                                   &member);
        if (status == CTOOL_OK) {
          status = cfront_vector_append(&context->temporary_indices,
                                        &member.type, (ctool_u32 *)0);
        }
      }
    }
  }
  {
    ctool_status_t rewind_status =
        cfront_vector_rewind(&context->temporary_indices, stack_mark);
    if (status == CTOOL_OK && rewind_status != CTOOL_OK) {
      status = rewind_status;
    }
  }
  {
    ctool_status_t rewind_status =
        cfront_vector_rewind(&context->flexible_seen, seen_mark);
    if (status == CTOOL_OK && rewind_status != CTOOL_OK) {
      status = rewind_status;
    }
  }
  return status;
}

static ctool_status_t cfront_binding_get(const cfront_context_t *context,
                                         ctool_u32 index,
                                         ctool_c_binding_t *binding_out) {
  return cfront_vector_get(&context->bindings, index, binding_out);
}

static ctool_status_t cfront_block_binding_get(
    const cfront_context_t *context, ctool_u32 index,
    ctool_c_block_binding_t *binding_out) {
  return cfront_vector_get(&context->block_bindings, index, binding_out);
}

static ctool_bool cfront_find_active_block_binding(
    const cfront_context_t *context, ctool_string_t name,
    ctool_c_block_binding_t *binding_out, ctool_u32 *index_out) {
  ctool_u32 active = context->active_block_binding_indices.count;
  while (active != 0u) {
    ctool_c_block_binding_t binding;
    ctool_u32 index;
    active--;
    if (cfront_vector_get(&context->active_block_binding_indices, active,
                          &index) != CTOOL_OK ||
        cfront_block_binding_get(context, index, &binding) != CTOOL_OK) {
      return CTOOL_FALSE;
    }
    if (cfront_string_equal(binding.name, name) == CTOOL_TRUE) {
      if (binding_out != (ctool_c_block_binding_t *)0) {
        *binding_out = binding;
      }
      if (index_out != (ctool_u32 *)0) {
        *index_out = index;
      }
      return CTOOL_TRUE;
    }
  }
  return CTOOL_FALSE;
}

static ctool_bool cfront_find_active_parameter(
    const cfront_context_t *context, ctool_string_t name,
    ctool_u32 *parameter_out, ctool_u32 *type_out);

static ctool_bool cfront_prototype_name_exists_range(
    const cfront_context_t *context, ctool_u32 first, ctool_u32 end,
    ctool_string_t name) {
  ctool_u32 index = end;
  while (index > first) {
    ctool_string_t candidate;
    index--;
    if (cfront_vector_get(&context->prototype_names, index, &candidate) !=
        CTOOL_OK) {
      return CTOOL_FALSE;
    }
    if (cfront_string_equal(candidate, name) == CTOOL_TRUE) {
      return CTOOL_TRUE;
    }
  }
  return CTOOL_FALSE;
}

static ctool_bool cfront_prototype_name_exists_from(
    const cfront_context_t *context, ctool_u32 first,
    ctool_string_t name) {
  return cfront_prototype_name_exists_range(
      context, first, context->prototype_names.count, name);
}

static ctool_bool cfront_find_binding_range(
    const cfront_context_t *context, ctool_u32 first, ctool_u32 end,
    ctool_string_t name, ctool_c_binding_t *binding_out) {
  ctool_u32 index = end;
  while (index > first) {
    ctool_c_binding_t binding;
    index--;
    if (cfront_binding_get(context, index, &binding) != CTOOL_OK) {
      return CTOOL_FALSE;
    }
    if (cfront_string_equal(binding.name, name) == CTOOL_TRUE) {
      if (binding_out != (ctool_c_binding_t *)0) {
        *binding_out = binding;
      }
      return CTOOL_TRUE;
    }
  }
  return CTOOL_FALSE;
}

static ctool_bool cfront_find_binding_from(
    const cfront_context_t *context, ctool_u32 first, ctool_string_t name,
    ctool_c_binding_t *binding_out) {
  return cfront_find_binding_range(context, first, context->bindings.count,
                                   name, binding_out);
}

static ctool_bool cfront_find_binding(const cfront_context_t *context,
                                      ctool_string_t name,
                                      ctool_c_binding_t *binding_out) {
  ctool_u32 level = context->prototype_binding_marks.count;
  ctool_u32 file_binding_end = context->bindings.count;
  if (level != context->prototype_name_marks.count) {
    return CTOOL_FALSE;
  }
  while (level != 0u) {
    ctool_u32 binding_first;
    ctool_u32 binding_end = file_binding_end;
    ctool_u32 name_first;
    ctool_u32 name_end = context->prototype_names.count;
    if (cfront_vector_get(&context->prototype_binding_marks, level - 1u,
                          &binding_first) != CTOOL_OK ||
        cfront_vector_get(&context->prototype_name_marks, level - 1u,
                          &name_first) != CTOOL_OK) {
      return CTOOL_FALSE;
    }
    if (level < context->prototype_binding_marks.count) {
      if (cfront_vector_get(&context->prototype_binding_marks, level,
                            &binding_end) != CTOOL_OK ||
          cfront_vector_get(&context->prototype_name_marks, level,
                            &name_end) != CTOOL_OK) {
        return CTOOL_FALSE;
      }
    }
    if (cfront_find_binding_range(context, binding_first, binding_end, name,
                                  binding_out) == CTOOL_TRUE) {
      return CTOOL_TRUE;
    }
    if (cfront_prototype_name_exists_range(context, name_first, name_end,
                                           name) == CTOOL_TRUE) {
      return CTOOL_FALSE;
    }
    file_binding_end = binding_first;
    level--;
  }
  return cfront_find_binding_range(context, 0u, file_binding_end, name,
                                   binding_out);
}

static ctool_bool cfront_find_typedef(const cfront_context_t *context,
                                      ctool_string_t name,
                                      ctool_u32 *type_out) {
  ctool_c_binding_t binding;
  ctool_u32 parameter;
  ctool_u32 parameter_type;
  if (cfront_find_active_block_binding(
          context, name, (ctool_c_block_binding_t *)0,
          (ctool_u32 *)0) == CTOOL_TRUE ||
      cfront_find_active_parameter(context, name, &parameter,
                                   &parameter_type) == CTOOL_TRUE) {
    return CTOOL_FALSE;
  }
  if (cfront_find_binding(context, name, &binding) == CTOOL_TRUE &&
      binding.kind == CTOOL_C_BINDING_TYPEDEF) {
    *type_out = binding.type;
    return CTOOL_TRUE;
  }
  return CTOOL_FALSE;
}

typedef enum {
  CFRONT_TYPE_SAME = 1,
  CFRONT_TYPE_COMPATIBLE,
  CFRONT_TYPE_CAN_REPRESENT_COMPOSITE
} cfront_type_relation_t;

typedef struct {
  ctool_u32 type;
  ctool_u32 qualifiers;
} cfront_type_ref_t;

typedef struct {
  cfront_type_ref_t left;
  cfront_type_ref_t right;
  ctool_bool parameter;
} cfront_type_pair_t;

typedef struct {
  ctool_u32 type;
  ctool_c_type_node_t node;
  ctool_u32 qualifiers;
} cfront_type_view_t;

static ctool_status_t cfront_type_view(const cfront_context_t *context,
                                       cfront_type_ref_t reference,
                                       ctool_bool parameter,
                                       cfront_type_view_t *view_out) {
  ctool_u32 traversed = 0u;
  ctool_status_t status;
  for (;;) {
    status = cfront_type_get(context, reference.type, &view_out->node);
    if (status != CTOOL_OK) {
      return status;
    }
    if (view_out->node.kind != CTOOL_C_TYPE_QUALIFIED) {
      break;
    }
    reference.qualifiers |= view_out->node.qualifiers;
    reference.type = view_out->node.referenced_type;
    traversed++;
    if (traversed > context->types.count) {
      return CTOOL_ERR_INTERNAL;
    }
  }
  view_out->type = reference.type;
  view_out->qualifiers = reference.qualifiers | view_out->node.qualifiers;
  if (parameter == CTOOL_TRUE) {
    view_out->qualifiers &= CTOOL_C_QUAL_ATOMIC;
  }
  return CTOOL_OK;
}

static ctool_status_t cfront_alignment_transparent_qualifiers(
    const cfront_context_t *context, const cfront_type_view_t *view,
    ctool_bool parameter, ctool_u32 *qualifiers_out) {
  cfront_type_view_t current = *view;
  ctool_u32 qualifiers = current.qualifiers;
  ctool_u32 traversed = 0u;
  ctool_status_t status = CTOOL_OK;
  while (current.node.kind == CTOOL_C_TYPE_ALIGNED) {
    cfront_type_ref_t reference;
    reference.type = current.node.referenced_type;
    reference.qualifiers = 0u;
    status = cfront_type_view(context, reference, parameter, &current);
    if (status != CTOOL_OK) {
      return status;
    }
    qualifiers |= current.qualifiers;
    traversed++;
    if (traversed > context->types.count) {
      return CTOOL_ERR_INTERNAL;
    }
  }
  *qualifiers_out = qualifiers;
  return CTOOL_OK;
}

static ctool_bool cfront_type_pair_equal(const cfront_type_pair_t *left,
                                          const cfront_type_pair_t *right) {
  return left->left.type == right->left.type &&
                 left->left.qualifiers == right->left.qualifiers &&
                 left->right.type == right->right.type &&
                 left->right.qualifiers == right->right.qualifiers &&
                 left->parameter == right->parameter
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cfront_type_pair_seen(const cfront_vector_t *seen,
                                        const cfront_type_pair_t *pair) {
  ctool_u32 index;
  for (index = 0u; index < seen->count; index++) {
    cfront_type_pair_t candidate;
    if (cfront_vector_get(seen, index, &candidate) != CTOOL_OK) {
      return CTOOL_FALSE;
    }
    if (cfront_type_pair_equal(&candidate, pair) == CTOOL_TRUE) {
      return CTOOL_TRUE;
    }
  }
  return CTOOL_FALSE;
}

static ctool_bool cfront_integer_type_kind(ctool_c_type_kind_t kind) {
  return kind == CTOOL_C_TYPE_BOOL || kind == CTOOL_C_TYPE_CHAR ||
                 kind == CTOOL_C_TYPE_SIGNED_CHAR ||
                 kind == CTOOL_C_TYPE_UNSIGNED_CHAR ||
                 kind == CTOOL_C_TYPE_SIGNED_SHORT ||
                 kind == CTOOL_C_TYPE_UNSIGNED_SHORT ||
                 kind == CTOOL_C_TYPE_SIGNED_INT ||
                 kind == CTOOL_C_TYPE_UNSIGNED_INT ||
                 kind == CTOOL_C_TYPE_SIGNED_LONG ||
                 kind == CTOOL_C_TYPE_UNSIGNED_LONG ||
                 kind == CTOOL_C_TYPE_SIGNED_LONG_LONG ||
                 kind == CTOOL_C_TYPE_UNSIGNED_LONG_LONG
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t cfront_parameter_survives_default_promotion(
    const cfront_context_t *context, ctool_u32 type,
    ctool_bool *survives_out) {
  cfront_type_ref_t reference;
  cfront_type_view_t view;
  ctool_u32 traversed = 0u;
  ctool_status_t status;
  reference.type = type;
  reference.qualifiers = 0u;
  for (;;) {
    status = cfront_type_view(context, reference, CTOOL_TRUE, &view);
    if (status != CTOOL_OK || view.node.kind != CTOOL_C_TYPE_ALIGNED) {
      break;
    }
    reference.type = view.node.referenced_type;
    reference.qualifiers = view.qualifiers;
    traversed++;
    if (traversed > context->types.count) {
      return CTOOL_ERR_INTERNAL;
    }
  }
  if (status != CTOOL_OK) {
    return status;
  }
  switch (view.node.kind) {
  case CTOOL_C_TYPE_BOOL:
  case CTOOL_C_TYPE_CHAR:
  case CTOOL_C_TYPE_SIGNED_CHAR:
  case CTOOL_C_TYPE_UNSIGNED_CHAR:
  case CTOOL_C_TYPE_SIGNED_SHORT:
  case CTOOL_C_TYPE_UNSIGNED_SHORT:
  case CTOOL_C_TYPE_FLOAT:
    *survives_out = CTOOL_FALSE;
    break;
  default:
    *survives_out = CTOOL_TRUE;
    break;
  }
  return CTOOL_OK;
}

static ctool_status_t cfront_compare_type_refs(
    cfront_context_t *context, cfront_type_ref_t left,
    cfront_type_ref_t right, ctool_bool parameter,
    cfront_type_relation_t relation, ctool_bool *matches_out) {
  cfront_vector_t work;
  cfront_vector_t seen;
  cfront_type_pair_t initial;
  ctool_status_t status;
  ctool_bool matches = CTOOL_TRUE;
  cfront_zero(&work, (ctool_u32)sizeof(work));
  cfront_zero(&seen, (ctool_u32)sizeof(seen));
  initial.left = left;
  initial.right = right;
  initial.parameter = parameter;
  status = cfront_vector_open(context, &work,
                              (ctool_u32)sizeof(cfront_type_pair_t));
  if (status == CTOOL_OK) {
    status = cfront_vector_open(context, &seen,
                                (ctool_u32)sizeof(cfront_type_pair_t));
  }
  if (status == CTOOL_OK) {
    status = cfront_vector_append(&work, &initial, (ctool_u32 *)0);
  }
  while (status == CTOOL_OK && matches == CTOOL_TRUE && work.count != 0u) {
    cfront_type_pair_t pair;
    cfront_type_view_t left_view;
    cfront_type_view_t right_view;
    ctool_u32 left_qualifiers;
    ctool_u32 right_qualifiers;
    status = cfront_vector_get(&work, work.count - 1u, &pair);
    if (status == CTOOL_OK) {
      status = cfront_vector_rewind(&work, work.count - 1u);
    }
    if (status != CTOOL_OK ||
        cfront_type_pair_seen(&seen, &pair) == CTOOL_TRUE) {
      continue;
    }
    status = cfront_vector_append(&seen, &pair, (ctool_u32 *)0);
    if (status == CTOOL_OK) {
      status = cfront_type_view(context, pair.left, pair.parameter,
                                &left_view);
    }
    if (status == CTOOL_OK) {
      status = cfront_type_view(context, pair.right, pair.parameter,
                                &right_view);
    }
    if (status != CTOOL_OK) {
      break;
    }
    left_qualifiers = left_view.qualifiers;
    right_qualifiers = right_view.qualifiers;
    if (left_view.node.kind == CTOOL_C_TYPE_ALIGNED ||
        right_view.node.kind == CTOOL_C_TYPE_ALIGNED) {
      status = cfront_alignment_transparent_qualifiers(
          context, &left_view, pair.parameter, &left_qualifiers);
      if (status == CTOOL_OK) {
        status = cfront_alignment_transparent_qualifiers(
            context, &right_view, pair.parameter, &right_qualifiers);
      }
      if (status != CTOOL_OK) {
        break;
      }
    }
    if (left_view.node.kind != CTOOL_C_TYPE_ARRAY &&
        left_qualifiers != right_qualifiers) {
      matches = CTOOL_FALSE;
      break;
    }
    if (left_view.node.kind != right_view.node.kind) {
      cfront_type_pair_t child;
      if (left_view.node.kind == CTOOL_C_TYPE_ALIGNED ||
          right_view.node.kind == CTOOL_C_TYPE_ALIGNED) {
        ctool_u32 left_alignment =
            left_view.node.kind == CTOOL_C_TYPE_ALIGNED
                ? left_view.node.explicit_alignment
                : 0u;
        ctool_u32 right_alignment =
            right_view.node.kind == CTOOL_C_TYPE_ALIGNED
                ? right_view.node.explicit_alignment
                : 0u;
        if (relation == CFRONT_TYPE_CAN_REPRESENT_COMPOSITE &&
            left_alignment < right_alignment) {
          matches = CTOOL_FALSE;
          break;
        }
        child.left.type = left_view.node.kind == CTOOL_C_TYPE_ALIGNED
                              ? left_view.node.referenced_type
                              : left_view.type;
        child.right.type = right_view.node.kind == CTOOL_C_TYPE_ALIGNED
                               ? right_view.node.referenced_type
                               : right_view.type;
        child.left.qualifiers = left_view.qualifiers;
        child.right.qualifiers = right_view.qualifiers;
        child.parameter = pair.parameter;
        status = cfront_vector_append(&work, &child, (ctool_u32 *)0);
        continue;
      }
      ctool_bool left_enum =
          left_view.node.kind == CTOOL_C_TYPE_ENUM ? CTOOL_TRUE : CTOOL_FALSE;
      ctool_bool right_enum =
          right_view.node.kind == CTOOL_C_TYPE_ENUM ? CTOOL_TRUE : CTOOL_FALSE;
      ctool_bool enum_integer =
          left_enum == CTOOL_TRUE &&
                  cfront_integer_type_kind(right_view.node.kind) == CTOOL_TRUE
              ? CTOOL_TRUE
              : (right_enum == CTOOL_TRUE &&
                         cfront_integer_type_kind(left_view.node.kind) ==
                             CTOOL_TRUE
                     ? CTOOL_TRUE
                     : CTOOL_FALSE);
      if (relation == CFRONT_TYPE_SAME || enum_integer == CTOOL_FALSE) {
        matches = CTOOL_FALSE;
        break;
      }
      child.left.type = left_enum == CTOOL_TRUE
                            ? left_view.node.referenced_type
                            : left_view.type;
      child.right.type = right_enum == CTOOL_TRUE
                             ? right_view.node.referenced_type
                             : right_view.type;
      child.left.qualifiers = 0u;
      child.right.qualifiers = 0u;
      child.parameter = CTOOL_FALSE;
      status = cfront_vector_append(&work, &child, (ctool_u32 *)0);
      continue;
    }
    if (left_view.type == right_view.type &&
        (left_view.node.kind != CTOOL_C_TYPE_ARRAY ||
         left_qualifiers == right_qualifiers)) {
      continue;
    }
    switch (left_view.node.kind) {
    case CTOOL_C_TYPE_VOID:
    case CTOOL_C_TYPE_BOOL:
    case CTOOL_C_TYPE_CHAR:
    case CTOOL_C_TYPE_SIGNED_CHAR:
    case CTOOL_C_TYPE_UNSIGNED_CHAR:
    case CTOOL_C_TYPE_SIGNED_SHORT:
    case CTOOL_C_TYPE_UNSIGNED_SHORT:
    case CTOOL_C_TYPE_SIGNED_INT:
    case CTOOL_C_TYPE_UNSIGNED_INT:
    case CTOOL_C_TYPE_SIGNED_LONG:
    case CTOOL_C_TYPE_UNSIGNED_LONG:
    case CTOOL_C_TYPE_SIGNED_LONG_LONG:
    case CTOOL_C_TYPE_UNSIGNED_LONG_LONG:
    case CTOOL_C_TYPE_FLOAT:
    case CTOOL_C_TYPE_DOUBLE:
    case CTOOL_C_TYPE_LONG_DOUBLE:
      break;
    case CTOOL_C_TYPE_POINTER: {
      cfront_type_pair_t child;
      child.left.type = left_view.node.referenced_type;
      child.right.type = right_view.node.referenced_type;
      child.left.qualifiers = 0u;
      child.right.qualifiers = 0u;
      child.parameter = CTOOL_FALSE;
      status = cfront_vector_append(&work, &child, (ctool_u32 *)0);
      break;
    }
    case CTOOL_C_TYPE_ALIGNED: {
      cfront_type_pair_t child;
      if (relation == CFRONT_TYPE_CAN_REPRESENT_COMPOSITE &&
          (left_view.node.explicit_alignment <
               right_view.node.explicit_alignment ||
           left_view.qualifiers != right_view.qualifiers)) {
        matches = CTOOL_FALSE;
        break;
      }
      child.left.type = left_view.node.referenced_type;
      child.right.type = right_view.node.referenced_type;
      child.left.qualifiers = left_view.qualifiers;
      child.right.qualifiers = right_view.qualifiers;
      child.parameter = pair.parameter;
      status = cfront_vector_append(&work, &child, (ctool_u32 *)0);
      break;
    }
    case CTOOL_C_TYPE_VECTOR: {
      cfront_type_pair_t child;
      if (left_view.node.element_count != right_view.node.element_count) {
        matches = CTOOL_FALSE;
        break;
      }
      child.left.type = left_view.node.referenced_type;
      child.right.type = right_view.node.referenced_type;
      child.left.qualifiers = 0u;
      child.right.qualifiers = 0u;
      child.parameter = CTOOL_FALSE;
      status = cfront_vector_append(&work, &child, (ctool_u32 *)0);
      break;
    }
    case CTOOL_C_TYPE_ARRAY: {
      cfront_type_pair_t child;
      if (left_view.node.array_bound_kind == CTOOL_C_ARRAY_VARIABLE ||
          right_view.node.array_bound_kind == CTOOL_C_ARRAY_VARIABLE) {
        matches = CTOOL_FALSE;
        break;
      }
      if (relation == CFRONT_TYPE_SAME &&
          (left_view.node.array_bound_kind !=
               right_view.node.array_bound_kind ||
           (left_view.node.array_bound_kind == CTOOL_C_ARRAY_FIXED &&
            left_view.node.element_count != right_view.node.element_count))) {
        matches = CTOOL_FALSE;
        break;
      }
      if (relation == CFRONT_TYPE_COMPATIBLE &&
          left_view.node.array_bound_kind == CTOOL_C_ARRAY_FIXED &&
          right_view.node.array_bound_kind == CTOOL_C_ARRAY_FIXED &&
          left_view.node.element_count != right_view.node.element_count) {
        matches = CTOOL_FALSE;
        break;
      }
      if (relation == CFRONT_TYPE_CAN_REPRESENT_COMPOSITE &&
          right_view.node.array_bound_kind == CTOOL_C_ARRAY_FIXED &&
          (left_view.node.array_bound_kind != CTOOL_C_ARRAY_FIXED ||
           left_view.node.element_count != right_view.node.element_count)) {
        matches = CTOOL_FALSE;
        break;
      }
      child.left.type = left_view.node.referenced_type;
      child.right.type = right_view.node.referenced_type;
      child.left.qualifiers = left_qualifiers;
      child.right.qualifiers = right_qualifiers;
      child.parameter = CTOOL_FALSE;
      status = cfront_vector_append(&work, &child, (ctool_u32 *)0);
      break;
    }
    case CTOOL_C_TYPE_FUNCTION: {
      cfront_type_pair_t child;
      ctool_bool both_prototypes =
          left_view.node.has_prototype == CTOOL_TRUE &&
                  right_view.node.has_prototype == CTOOL_TRUE
              ? CTOOL_TRUE
              : CTOOL_FALSE;
      ctool_bool different_prototypes =
          left_view.node.has_prototype != right_view.node.has_prototype
              ? CTOOL_TRUE
              : CTOOL_FALSE;
      ctool_u32 index;
      if (relation == CFRONT_TYPE_SAME &&
          (different_prototypes == CTOOL_TRUE ||
           left_view.node.variadic != right_view.node.variadic ||
           left_view.node.parameter_count !=
               right_view.node.parameter_count)) {
        matches = CTOOL_FALSE;
        break;
      }
      if (different_prototypes == CTOOL_TRUE &&
          relation != CFRONT_TYPE_SAME) {
        const ctool_c_type_node_t *prototype =
            left_view.node.has_prototype == CTOOL_TRUE ? &left_view.node
                                                       : &right_view.node;
        if ((relation == CFRONT_TYPE_CAN_REPRESENT_COMPOSITE &&
             left_view.node.has_prototype == CTOOL_FALSE) ||
            prototype->variadic == CTOOL_TRUE) {
          matches = CTOOL_FALSE;
          break;
        }
        for (index = 0u; index < prototype->parameter_count; index++) {
          ctool_u32 parameter_type;
          ctool_bool survives = CTOOL_FALSE;
          status = cfront_vector_get(
              &context->parameter_types,
              prototype->first_parameter + index, &parameter_type);
          if (status == CTOOL_OK) {
            status = cfront_parameter_survives_default_promotion(
                context, parameter_type, &survives);
          }
          if (status != CTOOL_OK || survives == CTOOL_FALSE) {
            if (status == CTOOL_OK) {
              matches = CTOOL_FALSE;
            }
            break;
          }
        }
        if (status != CTOOL_OK || matches == CTOOL_FALSE) {
          break;
        }
      } else if (both_prototypes == CTOOL_TRUE) {
        if (left_view.node.variadic != right_view.node.variadic ||
            left_view.node.parameter_count !=
                right_view.node.parameter_count) {
          matches = CTOOL_FALSE;
          break;
        }
        for (index = 0u; index < left_view.node.parameter_count; index++) {
          ctool_u32 left_parameter;
          ctool_u32 right_parameter;
          status = cfront_vector_get(
              &context->parameter_types,
              left_view.node.first_parameter + index, &left_parameter);
          if (status == CTOOL_OK) {
            status = cfront_vector_get(
                &context->parameter_types,
                right_view.node.first_parameter + index, &right_parameter);
          }
          if (status != CTOOL_OK) {
            break;
          }
          child.left.type = left_parameter;
          child.right.type = right_parameter;
          child.left.qualifiers = 0u;
          child.right.qualifiers = 0u;
          child.parameter = CTOOL_TRUE;
          status = cfront_vector_append(&work, &child, (ctool_u32 *)0);
          if (status != CTOOL_OK) {
            break;
          }
        }
        if (status != CTOOL_OK) {
          break;
        }
      }
      child.left.type = left_view.node.referenced_type;
      child.right.type = right_view.node.referenced_type;
      child.left.qualifiers = 0u;
      child.right.qualifiers = 0u;
      child.parameter = CTOOL_FALSE;
      status = cfront_vector_append(&work, &child, (ctool_u32 *)0);
      break;
    }
    case CTOOL_C_TYPE_ENUM:
    case CTOOL_C_TYPE_RECORD:
      matches = CTOOL_FALSE;
      break;
    case CTOOL_C_TYPE_QUALIFIED:
    default:
      status = CTOOL_ERR_INTERNAL;
      break;
    }
  }
  cfront_vector_close(&seen);
  cfront_vector_close(&work);
  if (status == CTOOL_OK) {
    *matches_out = matches;
  }
  return status;
}

static ctool_status_t cfront_compare_types(
    cfront_context_t *context, ctool_u32 left, ctool_u32 right,
    cfront_type_relation_t relation, ctool_bool *matches_out) {
  cfront_type_ref_t left_ref;
  cfront_type_ref_t right_ref;
  left_ref.type = left;
  left_ref.qualifiers = 0u;
  right_ref.type = right;
  right_ref.qualifiers = 0u;
  return cfront_compare_type_refs(context, left_ref, right_ref, CTOOL_FALSE,
                                  relation, matches_out);
}

static ctool_status_t cfront_types_compatible(
    cfront_context_t *context, ctool_u32 left, ctool_u32 right,
    ctool_bool *compatible_out) {
  return cfront_compare_types(context, left, right,
                              CFRONT_TYPE_COMPATIBLE, compatible_out);
}

static ctool_status_t cfront_types_same(cfront_context_t *context,
                                        ctool_u32 left, ctool_u32 right,
                                        ctool_bool *same_out) {
  return cfront_compare_types(context, left, right, CFRONT_TYPE_SAME,
                              same_out);
}

typedef struct {
  cfront_type_pair_t pair;
  ctool_bool expanded;
  ctool_bool aligned_composite;
  cfront_type_pair_t aligned_child;
  ctool_u32 aligned_qualifiers;
} cfront_composite_frame_t;

typedef struct {
  cfront_type_pair_t pair;
  ctool_u32 type;
} cfront_composite_result_t;

static ctool_bool cfront_find_composite_result(
    const cfront_vector_t *results, const cfront_type_pair_t *pair,
    ctool_u32 *type_out) {
  ctool_u32 index;
  for (index = 0u; index < results->count; index++) {
    cfront_composite_result_t result;
    if (cfront_vector_get(results, index, &result) != CTOOL_OK) {
      return CTOOL_FALSE;
    }
    if (cfront_type_pair_equal(&result.pair, pair) == CTOOL_TRUE) {
      *type_out = result.type;
      return CTOOL_TRUE;
    }
  }
  return CTOOL_FALSE;
}

static ctool_status_t cfront_materialize_type_ref(
    cfront_context_t *context, cfront_type_ref_t reference,
    ctool_bool parameter, ctool_u32 *type_out) {
  cfront_type_view_t view;
  ctool_c_type_node_t node;
  ctool_status_t status;
  if (parameter == CTOOL_TRUE) {
    status = cfront_type_view(context, reference, CTOOL_TRUE, &view);
    if (status != CTOOL_OK) {
      return status;
    }
    if (view.node.qualifiers == view.qualifiers) {
      *type_out = view.type;
      return CTOOL_OK;
    }
    node = view.node;
    node.qualifiers = view.qualifiers;
    return cfront_type_append(context, &node, type_out);
  }
  if (reference.qualifiers == 0u) {
    *type_out = reference.type;
    return CTOOL_OK;
  }
  status = cfront_type_get(context, reference.type, &node);
  if (status != CTOOL_OK) {
    return status;
  }
  {
    ctool_c_type_node_t qualified;
    cfront_node_init(&qualified, CTOOL_C_TYPE_QUALIFIED,
                     (const ctool_c_pp_token_t *)0);
    qualified.location = node.location;
    qualified.physical_location = node.physical_location;
    qualified.referenced_type = reference.type;
    qualified.qualifiers = reference.qualifiers;
    return cfront_type_append(context, &qualified, type_out);
  }
}

static ctool_status_t cfront_type_ref_can_represent_composite(
    cfront_context_t *context, const cfront_type_pair_t *pair,
    ctool_bool reverse, ctool_bool *can_represent_out) {
  cfront_type_ref_t left =
      reverse == CTOOL_TRUE ? pair->right : pair->left;
  cfront_type_ref_t right =
      reverse == CTOOL_TRUE ? pair->left : pair->right;
  return cfront_compare_type_refs(
      context, left, right, pair->parameter,
      CFRONT_TYPE_CAN_REPRESENT_COMPOSITE, can_represent_out);
}

static cfront_type_pair_t cfront_child_pair(
    ctool_u32 left, ctool_u32 right, ctool_u32 left_qualifiers,
    ctool_u32 right_qualifiers, ctool_bool parameter) {
  cfront_type_pair_t pair;
  pair.left.type = left;
  pair.left.qualifiers = left_qualifiers;
  pair.right.type = right;
  pair.right.qualifiers = right_qualifiers;
  pair.parameter = parameter;
  return pair;
}

static ctool_status_t cfront_materialize_view_without_qualifiers(
    cfront_context_t *context, const cfront_type_view_t *view,
    ctool_u32 removed_qualifiers, ctool_u32 *type_out) {
  ctool_c_type_node_t node = view->node;
  ctool_u32 qualifiers = view->qualifiers & ~removed_qualifiers;
  ctool_u32 node_qualifiers = node.qualifiers & qualifiers;
  ctool_u32 type = view->type;
  ctool_status_t status = CTOOL_OK;
  if (node_qualifiers != node.qualifiers) {
    if (node.kind == CTOOL_C_TYPE_RECORD || node.kind == CTOOL_C_TYPE_ENUM) {
      return CTOOL_ERR_INTERNAL;
    }
    node.qualifiers = node_qualifiers;
    status = cfront_type_append(context, &node, &type);
  }
  if (status == CTOOL_OK && qualifiers != node_qualifiers) {
    ctool_c_type_node_t qualified;
    cfront_node_init(&qualified, CTOOL_C_TYPE_QUALIFIED,
                     (const ctool_c_pp_token_t *)0);
    qualified.location = view->node.location;
    qualified.physical_location = view->node.physical_location;
    qualified.referenced_type = type;
    qualified.qualifiers = qualifiers & ~node_qualifiers;
    status = cfront_type_append(context, &qualified, &type);
  }
  if (status == CTOOL_OK) {
    *type_out = type;
  }
  return status;
}

static ctool_status_t cfront_prepare_aligned_composite(
    cfront_context_t *context, const cfront_type_pair_t *pair,
    const cfront_type_view_t *left_view,
    const cfront_type_view_t *right_view, cfront_type_pair_t *child_out,
    ctool_u32 *qualifiers_out) {
  cfront_type_ref_t reference;
  cfront_type_view_t left_child_view;
  cfront_type_view_t right_child_view;
  ctool_u32 left_child_type;
  ctool_u32 right_child_type;
  ctool_u32 qualifiers;
  ctool_status_t status;
  ctool_bool left_aligned = left_view->node.kind == CTOOL_C_TYPE_ALIGNED
                                ? CTOOL_TRUE
                                : CTOOL_FALSE;
  ctool_bool right_aligned = right_view->node.kind == CTOOL_C_TYPE_ALIGNED
                                 ? CTOOL_TRUE
                                 : CTOOL_FALSE;
  if (left_aligned == CTOOL_FALSE && right_aligned == CTOOL_FALSE) {
    return CTOOL_ERR_INTERNAL;
  }
  qualifiers = left_aligned == CTOOL_TRUE && right_aligned == CTOOL_TRUE
                   ? left_view->qualifiers | right_view->qualifiers
                   : (left_aligned == CTOOL_TRUE ? left_view->qualifiers
                                                 : right_view->qualifiers);
  if (left_aligned == CTOOL_TRUE) {
    reference.type = left_view->node.referenced_type;
    reference.qualifiers = 0u;
    status = cfront_type_view(context, reference, pair->parameter,
                              &left_child_view);
  } else {
    left_child_view = *left_view;
    status = CTOOL_OK;
  }
  if (status == CTOOL_OK && right_aligned == CTOOL_TRUE) {
    reference.type = right_view->node.referenced_type;
    reference.qualifiers = 0u;
    status = cfront_type_view(context, reference, pair->parameter,
                              &right_child_view);
  } else if (status == CTOOL_OK) {
    right_child_view = *right_view;
  }
  if (status == CTOOL_OK) {
    status = cfront_materialize_view_without_qualifiers(
        context, &left_child_view, qualifiers, &left_child_type);
  }
  if (status == CTOOL_OK) {
    status = cfront_materialize_view_without_qualifiers(
        context, &right_child_view, qualifiers, &right_child_type);
  }
  if (status == CTOOL_OK) {
    *child_out = cfront_child_pair(left_child_type, right_child_type, 0u, 0u,
                                   pair->parameter);
    *qualifiers_out = qualifiers;
  }
  return status;
}

static ctool_status_t cfront_append_composite_result(
    cfront_vector_t *results, const cfront_type_pair_t *pair,
    ctool_u32 type) {
  cfront_composite_result_t result;
  result.pair = *pair;
  result.type = type;
  return cfront_vector_append(results, &result, (ctool_u32 *)0);
}

static ctool_status_t cfront_push_composite_children(
    cfront_context_t *context, cfront_vector_t *frames,
    const cfront_composite_frame_t *frame,
    const cfront_type_view_t *left_view,
    const cfront_type_view_t *right_view) {
  cfront_composite_frame_t child;
  ctool_status_t status = CTOOL_OK;
  cfront_zero(&child, (ctool_u32)sizeof(child));
  if (frame->aligned_composite == CTOOL_TRUE) {
    child.pair = frame->aligned_child;
    return cfront_vector_append(frames, &child, (ctool_u32 *)0);
  }
  switch (left_view->node.kind) {
  case CTOOL_C_TYPE_POINTER:
  case CTOOL_C_TYPE_ALIGNED:
  case CTOOL_C_TYPE_VECTOR:
    child.pair = cfront_child_pair(
        left_view->node.referenced_type, right_view->node.referenced_type,
        0u, 0u, CTOOL_FALSE);
    return cfront_vector_append(frames, &child, (ctool_u32 *)0);
  case CTOOL_C_TYPE_ARRAY:
    child.pair = cfront_child_pair(
        left_view->node.referenced_type, right_view->node.referenced_type,
        left_view->qualifiers, right_view->qualifiers, CTOOL_FALSE);
    return cfront_vector_append(frames, &child, (ctool_u32 *)0);
  case CTOOL_C_TYPE_FUNCTION: {
    ctool_u32 index;
    if (left_view->node.has_prototype == CTOOL_TRUE &&
        right_view->node.has_prototype == CTOOL_TRUE) {
      for (index = 0u; index < left_view->node.parameter_count; index++) {
        ctool_u32 left_parameter = 0u;
        ctool_u32 right_parameter = 0u;
        status = cfront_vector_get(
            &context->parameter_types,
            left_view->node.first_parameter + index, &left_parameter);
        if (status == CTOOL_OK) {
          status = cfront_vector_get(
              &context->parameter_types,
              right_view->node.first_parameter + index, &right_parameter);
        }
        if (status != CTOOL_OK) {
          return status;
        }
        child.pair = cfront_child_pair(left_parameter, right_parameter,
                                       0u, 0u, CTOOL_TRUE);
        status = cfront_vector_append(frames, &child, (ctool_u32 *)0);
        if (status != CTOOL_OK) {
          return status;
        }
      }
    }
    child.pair = cfront_child_pair(
        left_view->node.referenced_type, right_view->node.referenced_type,
        0u, 0u, CTOOL_FALSE);
    return cfront_vector_append(frames, &child, (ctool_u32 *)0);
  }
  default:
    (void)frame;
    return CTOOL_ERR_INTERNAL;
  }
}

static ctool_status_t cfront_build_composite_node(
    cfront_context_t *context, const cfront_composite_frame_t *frame,
    const cfront_type_view_t *left_view,
    const cfront_type_view_t *right_view,
    const cfront_vector_t *results, ctool_u32 *type_out) {
  ctool_c_type_node_t node = left_view->node;
  cfront_type_pair_t child;
  ctool_u32 child_type;
  ctool_status_t status;
  const cfront_type_view_t *aligned_view;
  if (frame->aligned_composite == CTOOL_TRUE) {
    if (left_view->node.kind == CTOOL_C_TYPE_ALIGNED &&
        right_view->node.kind == CTOOL_C_TYPE_ALIGNED) {
      aligned_view = left_view->node.explicit_alignment >=
                             right_view->node.explicit_alignment
                         ? left_view
                         : right_view;
    } else if (left_view->node.kind == CTOOL_C_TYPE_ALIGNED) {
      aligned_view = left_view;
    } else if (right_view->node.kind == CTOOL_C_TYPE_ALIGNED) {
      aligned_view = right_view;
    } else {
      return CTOOL_ERR_INTERNAL;
    }
    child = frame->aligned_child;
    if (cfront_find_composite_result(results, &child, &child_type) ==
        CTOOL_FALSE) {
      return CTOOL_ERR_INTERNAL;
    }
    node = aligned_view->node;
    node.referenced_type = child_type;
    node.qualifiers = frame->aligned_qualifiers;
    return cfront_type_append(context, &node, type_out);
  }
  node.location = left_view->node.location;
  node.physical_location = left_view->node.physical_location;
  switch (left_view->node.kind) {
  case CTOOL_C_TYPE_POINTER:
  case CTOOL_C_TYPE_VECTOR:
    child = cfront_child_pair(left_view->node.referenced_type,
                              right_view->node.referenced_type, 0u, 0u,
                              CTOOL_FALSE);
    if (cfront_find_composite_result(results, &child, &child_type) ==
        CTOOL_FALSE) {
      return CTOOL_ERR_INTERNAL;
    }
    node.referenced_type = child_type;
    node.qualifiers = left_view->qualifiers;
    return cfront_type_append(context, &node, type_out);
  case CTOOL_C_TYPE_ALIGNED:
    child = cfront_child_pair(left_view->node.referenced_type,
                              right_view->node.referenced_type, 0u, 0u,
                              CTOOL_FALSE);
    if (cfront_find_composite_result(results, &child, &child_type) ==
        CTOOL_FALSE) {
      return CTOOL_ERR_INTERNAL;
    }
    aligned_view = left_view->node.explicit_alignment >=
                           right_view->node.explicit_alignment
                       ? left_view
                       : right_view;
    node = aligned_view->node;
    node.referenced_type = child_type;
    node.qualifiers = aligned_view->qualifiers;
    return cfront_type_append(context, &node, type_out);
  case CTOOL_C_TYPE_ARRAY:
    child = cfront_child_pair(
        left_view->node.referenced_type, right_view->node.referenced_type,
        left_view->qualifiers, right_view->qualifiers, CTOOL_FALSE);
    if (cfront_find_composite_result(results, &child, &child_type) ==
        CTOOL_FALSE) {
      return CTOOL_ERR_INTERNAL;
    }
    node.referenced_type = child_type;
    node.qualifiers = 0u;
    if (right_view->node.array_bound_kind == CTOOL_C_ARRAY_FIXED) {
      node.array_bound_kind = CTOOL_C_ARRAY_FIXED;
      node.element_count = right_view->node.element_count;
    }
    return cfront_type_append(context, &node, type_out);
  case CTOOL_C_TYPE_FUNCTION: {
    const cfront_type_view_t *prototype_view = left_view;
    ctool_u32 index;
    child = cfront_child_pair(left_view->node.referenced_type,
                              right_view->node.referenced_type, 0u, 0u,
                              CTOOL_FALSE);
    if (cfront_find_composite_result(results, &child, &child_type) ==
        CTOOL_FALSE) {
      return CTOOL_ERR_INTERNAL;
    }
    if (left_view->node.has_prototype == CTOOL_FALSE &&
        right_view->node.has_prototype == CTOOL_TRUE) {
      prototype_view = right_view;
    }
    node = prototype_view->node;
    node.location = left_view->node.location;
    node.physical_location = left_view->node.physical_location;
    node.referenced_type = child_type;
    node.qualifiers = 0u;
    if (left_view->node.has_prototype == CTOOL_TRUE &&
        right_view->node.has_prototype == CTOOL_TRUE) {
      node.first_parameter = context->parameter_types.count;
      for (index = 0u; index < left_view->node.parameter_count; index++) {
        ctool_u32 left_parameter = 0u;
        ctool_u32 right_parameter = 0u;
        ctool_c_parameter_t parameter;
        status = cfront_vector_get(
            &context->parameter_types,
            left_view->node.first_parameter + index, &left_parameter);
        if (status == CTOOL_OK) {
          status = cfront_vector_get(
              &context->parameter_types,
              right_view->node.first_parameter + index, &right_parameter);
        }
        child = cfront_child_pair(left_parameter, right_parameter, 0u, 0u,
                                  CTOOL_TRUE);
        if (status == CTOOL_OK &&
            cfront_find_composite_result(results, &child, &child_type) ==
                CTOOL_FALSE) {
          status = CTOOL_ERR_INTERNAL;
        }
        if (status == CTOOL_OK) {
          status = cfront_vector_get(
              &context->parameters,
              left_view->node.first_parameter + index, &parameter);
        }
        if (status == CTOOL_OK) {
          status = cfront_vector_append(&context->parameter_types,
                                        &child_type, (ctool_u32 *)0);
        }
        if (status == CTOOL_OK) {
          status = cfront_vector_append(&context->parameters, &parameter,
                                        (ctool_u32 *)0);
        }
        if (status != CTOOL_OK) {
          return status;
        }
      }
    }
    return cfront_type_append(context, &node, type_out);
  }
  default:
    (void)frame;
    return CTOOL_ERR_INTERNAL;
  }
}

static ctool_status_t cfront_composite_type(cfront_context_t *context,
                                            ctool_u32 left,
                                            ctool_u32 right,
                                            ctool_u32 *composite_out) {
  cfront_vector_t frames;
  cfront_vector_t results;
  cfront_composite_frame_t initial;
  ctool_status_t status;
  cfront_zero(&frames, (ctool_u32)sizeof(frames));
  cfront_zero(&results, (ctool_u32)sizeof(results));
  cfront_zero(&initial, (ctool_u32)sizeof(initial));
  initial.pair = cfront_child_pair(left, right, 0u, 0u, CTOOL_FALSE);
  status = cfront_vector_open(context, &frames,
                              (ctool_u32)sizeof(cfront_composite_frame_t));
  if (status == CTOOL_OK) {
    status = cfront_vector_open(context, &results,
                                (ctool_u32)sizeof(cfront_composite_result_t));
  }
  if (status == CTOOL_OK) {
    status = cfront_vector_append(&frames, &initial, (ctool_u32 *)0);
  }
  while (status == CTOOL_OK && frames.count != 0u) {
    cfront_composite_frame_t frame;
    cfront_type_view_t left_view;
    cfront_type_view_t right_view;
    ctool_u32 result_type;
    ctool_bool can_represent = CTOOL_FALSE;
    status = cfront_vector_get(&frames, frames.count - 1u, &frame);
    if (status == CTOOL_OK) {
      status = cfront_vector_rewind(&frames, frames.count - 1u);
    }
    if (status != CTOOL_OK ||
        cfront_find_composite_result(&results, &frame.pair,
                                     &result_type) == CTOOL_TRUE) {
      continue;
    }
    if (frame.expanded == CTOOL_FALSE) {
      status = cfront_type_ref_can_represent_composite(
          context, &frame.pair, CTOOL_FALSE, &can_represent);
      if (status == CTOOL_OK && can_represent == CTOOL_TRUE) {
        status = cfront_materialize_type_ref(
            context, frame.pair.left, frame.pair.parameter, &result_type);
      } else if (status == CTOOL_OK) {
        status = cfront_type_ref_can_represent_composite(
            context, &frame.pair, CTOOL_TRUE, &can_represent);
        if (status == CTOOL_OK && can_represent == CTOOL_TRUE) {
          status = cfront_materialize_type_ref(
              context, frame.pair.right, frame.pair.parameter, &result_type);
        }
      }
      if (status != CTOOL_OK) {
        break;
      }
      if (can_represent == CTOOL_TRUE) {
        status = cfront_append_composite_result(
            &results, &frame.pair, result_type);
        continue;
      }
      status = cfront_type_view(context, frame.pair.left,
                                frame.pair.parameter, &left_view);
      if (status == CTOOL_OK) {
        status = cfront_type_view(context, frame.pair.right,
                                  frame.pair.parameter, &right_view);
      }
      if (status != CTOOL_OK ||
          (left_view.node.kind != right_view.node.kind &&
           left_view.node.kind != CTOOL_C_TYPE_ALIGNED &&
           right_view.node.kind != CTOOL_C_TYPE_ALIGNED)) {
        status = status == CTOOL_OK ? CTOOL_ERR_INTERNAL : status;
        break;
      }
      if (left_view.node.kind == CTOOL_C_TYPE_ALIGNED ||
          right_view.node.kind == CTOOL_C_TYPE_ALIGNED) {
        status = cfront_prepare_aligned_composite(
            context, &frame.pair, &left_view, &right_view,
            &frame.aligned_child, &frame.aligned_qualifiers);
        if (status != CTOOL_OK) {
          break;
        }
        frame.aligned_composite = CTOOL_TRUE;
      }
      frame.expanded = CTOOL_TRUE;
      status = cfront_vector_append(&frames, &frame, (ctool_u32 *)0);
      if (status == CTOOL_OK) {
        status = cfront_push_composite_children(
            context, &frames, &frame, &left_view, &right_view);
      }
      continue;
    }
    status = cfront_type_view(context, frame.pair.left,
                              frame.pair.parameter, &left_view);
    if (status == CTOOL_OK) {
      status = cfront_type_view(context, frame.pair.right,
                                frame.pair.parameter, &right_view);
    }
    if (status == CTOOL_OK) {
      status = cfront_build_composite_node(
          context, &frame, &left_view, &right_view, &results, &result_type);
    }
    if (status == CTOOL_OK) {
      status = cfront_append_composite_result(&results, &frame.pair,
                                              result_type);
    }
  }
  if (status == CTOOL_OK &&
      cfront_find_composite_result(&results, &initial.pair, composite_out) ==
          CTOOL_FALSE) {
    status = CTOOL_ERR_INTERNAL;
  }
  cfront_vector_close(&results);
  cfront_vector_close(&frames);
  return status;
}

static ctool_status_t cfront_type_attribute_alignment(
    const cfront_context_t *context, ctool_u32 type,
    ctool_u32 *alignment_out) {
  ctool_u32 traversed = 0u;
  for (;;) {
    ctool_c_type_node_t node;
    ctool_status_t status = cfront_type_get(context, type, &node);
    if (status != CTOOL_OK) {
      return status;
    }
    if (node.kind == CTOOL_C_TYPE_ALIGNED) {
      *alignment_out = node.explicit_alignment;
      return CTOOL_OK;
    }
    if (node.kind == CTOOL_C_TYPE_QUALIFIED) {
      type = node.referenced_type;
    } else {
      *alignment_out = 0u;
      return CTOOL_OK;
    }
    traversed++;
    if (traversed > context->types.count) {
      return CTOOL_ERR_INTERNAL;
    }
  }
}

static ctool_c_linkage_t cfront_binding_linkage(
    ctool_c_binding_kind_t kind, ctool_c_storage_class_t storage) {
  if (kind != CTOOL_C_BINDING_OBJECT &&
      kind != CTOOL_C_BINDING_FUNCTION) {
    return CTOOL_C_LINKAGE_NONE;
  }
  return storage == CTOOL_C_STORAGE_STATIC ? CTOOL_C_LINKAGE_INTERNAL
                                           : CTOOL_C_LINKAGE_EXTERNAL;
}

static ctool_bool cfront_redeclaration_linkage_compatible(
    const ctool_c_binding_t *existing, ctool_c_binding_kind_t kind,
    ctool_c_storage_class_t storage) {
  if (kind == CTOOL_C_BINDING_TYPEDEF) {
    return existing->storage == CTOOL_C_STORAGE_TYPEDEF &&
                   storage == CTOOL_C_STORAGE_TYPEDEF
               ? CTOOL_TRUE
               : CTOOL_FALSE;
  }
  if (kind != CTOOL_C_BINDING_OBJECT &&
      kind != CTOOL_C_BINDING_FUNCTION) {
    return CTOOL_FALSE;
  }
  if (storage == CTOOL_C_STORAGE_STATIC) {
    return existing->linkage == CTOOL_C_LINKAGE_INTERNAL ? CTOOL_TRUE
                                                         : CTOOL_FALSE;
  }
  if (storage == CTOOL_C_STORAGE_EXTERN ||
      (kind == CTOOL_C_BINDING_FUNCTION &&
       storage == CTOOL_C_STORAGE_NONE)) {
    return existing->linkage == CTOOL_C_LINKAGE_NONE ? CTOOL_FALSE
                                                     : CTOOL_TRUE;
  }
  if (kind == CTOOL_C_BINDING_OBJECT &&
      storage == CTOOL_C_STORAGE_NONE) {
    return existing->linkage == CTOOL_C_LINKAGE_EXTERNAL ? CTOOL_TRUE
                                                         : CTOOL_FALSE;
  }
  return CTOOL_FALSE;
}

static ctool_bool cfront_find_file_binding_index(
    const cfront_context_t *context, ctool_string_t name,
    ctool_c_binding_t *binding_out, ctool_u32 *index_out) {
  ctool_u32 index = context->bindings.count;
  while (index != 0u) {
    ctool_c_binding_t binding;
    index--;
    if (cfront_binding_get(context, index, &binding) != CTOOL_OK) {
      return CTOOL_FALSE;
    }
    if (cfront_string_equal(binding.name, name) == CTOOL_TRUE) {
      *binding_out = binding;
      *index_out = index;
      return CTOOL_TRUE;
    }
  }
  return CTOOL_FALSE;
}

static ctool_status_t cfront_append_binding(
    cfront_context_t *context, ctool_c_binding_kind_t kind,
    ctool_c_storage_class_t storage, ctool_string_t name, ctool_u32 type,
    cfront_binding_semantics_t semantics,
    const ctool_c_pp_token_t *token,
    const ctool_c_pp_location_t *location,
    const ctool_c_pp_location_t *physical_location, ctool_u64 integer_bits,
    ctool_bool integer_unsigned, ctool_u32 *binding_out) {
  ctool_c_binding_t existing;
  ctool_c_binding_t binding;
  ctool_u32 existing_index = CFRONT_NONE;
  ctool_bool duplicate = CTOOL_FALSE;
  if (name.size == 0u) {
    return cfront_emit_failure(context, CTOOL_ERR_INPUT,
                               CTOOL_C_PARSE_DIAG_DECLARATOR, token,
                               "declaration requires an identifier");
  }
  if (context->prototype_scope_depth != 0u) {
    duplicate =
        cfront_find_binding_from(context, context->prototype_binding_mark,
                                 name, &existing) == CTOOL_TRUE ||
                cfront_prototype_name_exists_from(
                    context, context->prototype_name_mark, name) == CTOOL_TRUE
            ? CTOOL_TRUE
            : CTOOL_FALSE;
  } else {
    duplicate = cfront_find_file_binding_index(
        context, name, &existing, &existing_index);
  }
  if (duplicate == CTOOL_TRUE) {
    if (context->prototype_scope_depth == 0u &&
        kind != CTOOL_C_BINDING_ENUMERATOR && existing.kind == kind &&
        cfront_redeclaration_linkage_compatible(&existing, kind, storage) ==
            CTOOL_TRUE) {
      ctool_bool compatible = CTOOL_FALSE;
      ctool_status_t status;
      if (kind == CTOOL_C_BINDING_TYPEDEF) {
        status = cfront_types_same(context, existing.type, type,
                                   &compatible);
      } else {
        status = cfront_types_compatible(context, existing.type, type,
                                         &compatible);
      }
      if (status == CTOOL_OK && compatible == CTOOL_TRUE) {
        ctool_u32 existing_type_alignment = 0u;
        ctool_u32 new_type_alignment = 0u;
        ctool_u32 composite = existing.type;
        status = cfront_type_attribute_alignment(
            context, existing.type, &existing_type_alignment);
        if (status == CTOOL_OK) {
          status = cfront_type_attribute_alignment(
              context, type, &new_type_alignment);
        }
        if (status == CTOOL_OK &&
            new_type_alignment > existing_type_alignment) {
          status = cfront_composite_type(context, type, existing.type,
                                         &composite);
        } else if (status == CTOOL_OK) {
          status = cfront_composite_type(context, existing.type, type,
                                         &composite);
        }
        if (status == CTOOL_OK &&
            (composite != existing.type ||
             (semantics.attributes & ~existing.attributes) != 0u ||
             (semantics.function_declaration_flags &
              ~existing.function_declaration_flags) != 0u ||
             semantics.minimum_alignment > existing.minimum_alignment)) {
          ctool_c_binding_t replacement = existing;
          replacement.type = composite;
          replacement.attributes |= semantics.attributes;
          replacement.function_declaration_flags |=
              semantics.function_declaration_flags;
          if (semantics.minimum_alignment > replacement.minimum_alignment) {
            replacement.minimum_alignment = semantics.minimum_alignment;
          }
          status = cfront_vector_replace(
              context, &context->bindings, existing_index, &replacement);
        }
      }
      if (status == CTOOL_OK && compatible == CTOOL_TRUE) {
        if (binding_out != (ctool_u32 *)0) {
          *binding_out = existing_index;
        }
        return CTOOL_OK;
      }
      if (status != CTOOL_OK) {
        if (status == CTOOL_ERR_LIMIT || status == CTOOL_ERR_OVERFLOW ||
            status == CTOOL_ERR_NO_MEMORY) {
          return cfront_storage_failure(context, status);
        }
        return cfront_emit_failure(context, status,
                                   CTOOL_C_PARSE_DIAG_INTERNAL, token,
                                   "declaration type compatibility check "
                                   "failed");
      }
    }
    return cfront_emit_failure(context, CTOOL_ERR_INPUT,
                               CTOOL_C_PARSE_DIAG_REDEFINITION, token,
                               "ordinary identifier has a conflicting "
                               "declaration");
  }
  cfront_zero(&binding, (ctool_u32)sizeof(binding));
  binding.name = name;
  binding.kind = kind;
  binding.storage = storage;
  binding.linkage = cfront_binding_linkage(kind, storage);
  binding.attributes = semantics.attributes;
  binding.function_declaration_flags = semantics.function_declaration_flags;
  binding.minimum_alignment = semantics.minimum_alignment;
  binding.type = type;
  if (location != (const ctool_c_pp_location_t *)0 &&
      physical_location != (const ctool_c_pp_location_t *)0) {
    binding.location = *location;
    binding.physical_location = *physical_location;
  }
  binding.integer_bits = integer_bits;
  binding.integer_unsigned = integer_unsigned;
  return cfront_vector_append(&context->bindings, &binding, binding_out);
}

static ctool_bool cfront_find_tag_from(const cfront_context_t *context,
                                       ctool_u32 first,
                                       ctool_string_t name,
                                       ctool_c_tag_t *tag_out) {
  ctool_u32 index = context->tags.count;
  while (index > first) {
    ctool_c_tag_t tag;
    index--;
    if (cfront_vector_get(&context->tags, index, &tag) != CTOOL_OK) {
      return CTOOL_FALSE;
    }
    if (cfront_string_equal(tag.name, name) == CTOOL_TRUE) {
      if (tag_out != (ctool_c_tag_t *)0) {
        *tag_out = tag;
      }
      return CTOOL_TRUE;
    }
  }
  return CTOOL_FALSE;
}

static ctool_bool cfront_find_tag(const cfront_context_t *context,
                                  ctool_string_t name,
                                  ctool_c_tag_t *tag_out) {
  return cfront_find_tag_from(context, 0u, name, tag_out);
}

static ctool_status_t cfront_append_tag(cfront_context_t *context,
                                        ctool_string_t name, ctool_u32 type,
                                        const ctool_c_pp_token_t *token) {
  ctool_c_tag_t tag;
  cfront_zero(&tag, (ctool_u32)sizeof(tag));
  tag.name = name;
  tag.type = type;
  tag.location = token->location;
  tag.physical_location = token->physical_location;
  return cfront_vector_append(&context->tags, &tag, (ctool_u32 *)0);
}

static ctool_bool cfront_digit_value(char value, ctool_u32 *digit_out) {
  if (value >= '0' && value <= '9') {
    *digit_out = (ctool_u32)(value - '0');
    return CTOOL_TRUE;
  }
  if (value >= 'a' && value <= 'f') {
    *digit_out = 10u + (ctool_u32)(value - 'a');
    return CTOOL_TRUE;
  }
  if (value >= 'A' && value <= 'F') {
    *digit_out = 10u + (ctool_u32)(value - 'A');
    return CTOOL_TRUE;
  }
  return CTOOL_FALSE;
}

static ctool_bool cfront_integer_unsigned(cfront_integer_kind_t kind) {
  return kind == CFRONT_INTEGER_UNSIGNED_32 ||
                 kind == CFRONT_INTEGER_UNSIGNED_64
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_u32 cfront_integer_width(cfront_integer_kind_t kind) {
  return kind == CFRONT_INTEGER_SIGNED_32 ||
                 kind == CFRONT_INTEGER_UNSIGNED_32
             ? 32u
             : 64u;
}

static ctool_u64 cfront_integer_mask(cfront_integer_kind_t kind) {
  return cfront_integer_width(kind) == 32u ? 0xffffffffull
                                           : 0xffffffffffffffffull;
}

static ctool_u64 cfront_integer_normalize_bits(
    ctool_u64 bits, cfront_integer_kind_t kind) {
  if (kind == CFRONT_INTEGER_UNSIGNED_32) {
    return bits & 0xffffffffull;
  }
  if (kind == CFRONT_INTEGER_SIGNED_32) {
    bits &= 0xffffffffull;
    return (bits & 0x80000000ull) != 0ull
               ? bits | 0xffffffff00000000ull
               : bits;
  }
  return bits;
}

static ctool_bool cfront_integer_negative(const cfront_integer_t *value) {
  if (cfront_integer_unsigned(value->kind) == CTOOL_TRUE) {
    return CTOOL_FALSE;
  }
  return (value->bits &
          (cfront_integer_width(value->kind) == 32u
               ? 0x80000000ull
               : 0x8000000000000000ull)) != 0ull
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_u64 cfront_integer_magnitude(const cfront_integer_t *value) {
  return cfront_integer_negative(value) == CTOOL_TRUE
             ? (0ull - value->bits) & cfront_integer_mask(value->kind)
             : value->bits & cfront_integer_mask(value->kind);
}

static cfront_integer_kind_t cfront_integer_usual_kind(
    cfront_integer_kind_t left, cfront_integer_kind_t right) {
  if (left == CFRONT_INTEGER_UNSIGNED_64 ||
      right == CFRONT_INTEGER_UNSIGNED_64) {
    return CFRONT_INTEGER_UNSIGNED_64;
  }
  if (left == CFRONT_INTEGER_SIGNED_64 ||
      right == CFRONT_INTEGER_SIGNED_64) {
    return CFRONT_INTEGER_SIGNED_64;
  }
  if (left == CFRONT_INTEGER_UNSIGNED_32 ||
      right == CFRONT_INTEGER_UNSIGNED_32) {
    return CFRONT_INTEGER_UNSIGNED_32;
  }
  return CFRONT_INTEGER_SIGNED_32;
}

static cfront_integer_t cfront_integer_convert(
    cfront_integer_t value, cfront_integer_kind_t kind) {
  value.kind = kind;
  value.bits = cfront_integer_normalize_bits(value.bits, kind);
  return value;
}

static ctool_status_t cfront_integer_overflow(
    cfront_context_t *context, const ctool_c_pp_token_t *token) {
  return cfront_emit_failure(
      context, CTOOL_ERR_OVERFLOW,
      CTOOL_C_PARSE_DIAG_OVERFLOW, token,
      "signed integer constant expression overflows its target type");
}

static ctool_status_t cfront_parse_integer_suffix(
    cfront_context_t *context, const ctool_c_pp_token_t *token,
    ctool_u32 index, ctool_u32 diagnostic_code, ctool_bool *unsigned_out,
    ctool_u32 *long_count_out) {
  ctool_u32 count = token->spelling.size - index;
  const char *suffix = token->spelling.data + index;
  ctool_bool first_u =
      count != 0u && (suffix[0] == 'u' || suffix[0] == 'U')
          ? CTOOL_TRUE
          : CTOOL_FALSE;
  ctool_bool last_u =
      count != 0u &&
              (suffix[count - 1u] == 'u' || suffix[count - 1u] == 'U')
          ? CTOOL_TRUE
          : CTOOL_FALSE;
  *unsigned_out = CTOOL_FALSE;
  *long_count_out = 0u;
  if (count == 0u) {
    return CTOOL_OK;
  }
  if (count > 3u || (first_u == CTOOL_TRUE && last_u == CTOOL_TRUE &&
                     count != 1u)) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INPUT, diagnostic_code, token,
        "integer constant suffix is invalid");
  }
  if (first_u == CTOOL_TRUE || last_u == CTOOL_TRUE) {
    *unsigned_out = CTOOL_TRUE;
    if (first_u == CTOOL_TRUE) {
      suffix++;
    }
    count--;
  }
  if (count == 0u) {
    return CTOOL_OK;
  }
  if (count > 2u ||
      (suffix[0] != 'l' && suffix[0] != 'L') ||
      (count == 2u && suffix[1] != suffix[0])) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INPUT, diagnostic_code, token,
        "integer constant suffix is invalid");
  }
  *long_count_out = count;
  return CTOOL_OK;
}

static ctool_status_t cfront_parse_number_token(
    cfront_context_t *context, const ctool_c_pp_token_t *token,
    ctool_u32 diagnostic_code, cfront_integer_t *value_out,
    ctool_c_type_kind_t *type_kind_out) {
  ctool_string_t spelling = token->spelling;
  ctool_u32 base = 10u;
  ctool_u32 index = 0u;
  ctool_u64 value = 0ull;
  ctool_bool saw_digit = CTOOL_FALSE;
  ctool_bool has_unsigned_suffix;
  ctool_u32 long_count;
  ctool_status_t status;
  if (token->kind != CTOOL_C_PP_TOKEN_NUMBER) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INPUT, diagnostic_code,
        token, "integer constant expression requires an integer token");
  }
  if (spelling.size >= 2u && spelling.data[0] == '0' &&
      (spelling.data[1] == 'x' || spelling.data[1] == 'X')) {
    base = 16u;
    index = 2u;
  } else if (spelling.size >= 2u && spelling.data[0] == '0' &&
             (spelling.data[1] == 'b' || spelling.data[1] == 'B') &&
             context->request->gnu_extensions == CTOOL_TRUE) {
    base = 2u;
    index = 2u;
  } else if (spelling.size > 1u && spelling.data[0] == '0') {
    base = 8u;
    index = 1u;
    saw_digit = CTOOL_TRUE;
  }
  while (index < spelling.size) {
    ctool_u32 digit;
    if (cfront_digit_value(spelling.data[index], &digit) == CTOOL_FALSE ||
        digit >= base) {
      break;
    }
    saw_digit = CTOOL_TRUE;
    if (value > (0xffffffffffffffffull - (ctool_u64)digit) /
                    (ctool_u64)base) {
      return cfront_emit_failure(
          context, CTOOL_ERR_OVERFLOW,
          CTOOL_C_PARSE_DIAG_OVERFLOW, token,
          "integer constant exceeds the frontend evaluation range");
    }
    value = value * (ctool_u64)base + (ctool_u64)digit;
    index++;
  }
  if (saw_digit == CTOOL_FALSE) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INPUT, diagnostic_code,
        token, "integer constant has no digits");
  }
  status = cfront_parse_integer_suffix(context, token, index,
                                       diagnostic_code,
                                       &has_unsigned_suffix, &long_count);
  if (status != CTOOL_OK) {
    return status;
  }
  if (base == 10u && has_unsigned_suffix == CTOOL_FALSE) {
    if (value <= 0x7fffffffull && long_count <= 1u) {
      value_out->kind = CFRONT_INTEGER_SIGNED_32;
    } else if (value <= 0x7fffffffffffffffull) {
      value_out->kind = CFRONT_INTEGER_SIGNED_64;
    } else {
      return cfront_emit_failure(
          context, CTOOL_ERR_OVERFLOW,
          CTOOL_C_PARSE_DIAG_OVERFLOW, token,
          "decimal integer constant has no representable target type");
    }
  } else if (has_unsigned_suffix == CTOOL_TRUE) {
    value_out->kind = value <= 0xffffffffull && long_count <= 1u
                          ? CFRONT_INTEGER_UNSIGNED_32
                          : CFRONT_INTEGER_UNSIGNED_64;
  } else if (long_count == 0u) {
    value_out->kind = value <= 0x7fffffffull
                          ? CFRONT_INTEGER_SIGNED_32
                          : (value <= 0xffffffffull
                                 ? CFRONT_INTEGER_UNSIGNED_32
                                 : (value <= 0x7fffffffffffffffull
                                        ? CFRONT_INTEGER_SIGNED_64
                                        : CFRONT_INTEGER_UNSIGNED_64));
  } else if (long_count == 1u) {
    value_out->kind = value <= 0x7fffffffull
                          ? CFRONT_INTEGER_SIGNED_32
                          : (value <= 0xffffffffull
                                 ? CFRONT_INTEGER_UNSIGNED_32
                                 : (value <= 0x7fffffffffffffffull
                                        ? CFRONT_INTEGER_SIGNED_64
                                        : CFRONT_INTEGER_UNSIGNED_64));
  } else {
    value_out->kind = value <= 0x7fffffffffffffffull
                          ? CFRONT_INTEGER_SIGNED_64
                          : CFRONT_INTEGER_UNSIGNED_64;
  }
  value_out->bits = cfront_integer_normalize_bits(value, value_out->kind);
  if (type_kind_out != (ctool_c_type_kind_t *)0) {
    if (value_out->kind == CFRONT_INTEGER_SIGNED_64) {
      *type_kind_out = CTOOL_C_TYPE_SIGNED_LONG_LONG;
    } else if (value_out->kind == CFRONT_INTEGER_UNSIGNED_64) {
      *type_kind_out = CTOOL_C_TYPE_UNSIGNED_LONG_LONG;
    } else if (value_out->kind == CFRONT_INTEGER_UNSIGNED_32) {
      *type_kind_out = long_count == 1u ? CTOOL_C_TYPE_UNSIGNED_LONG
                                        : CTOOL_C_TYPE_UNSIGNED_INT;
    } else {
      *type_kind_out = long_count == 1u ? CTOOL_C_TYPE_SIGNED_LONG
                                        : CTOOL_C_TYPE_SIGNED_INT;
    }
  }
  return CTOOL_OK;
}

static ctool_status_t cfront_parse_constant_logical_or(
    cfront_context_t *context, cfront_integer_t *value_out);
static ctool_status_t cfront_parse_body_unary(
    cfront_context_t *context, cfront_expression_value_t *value_out);
static ctool_status_t cfront_parse_body_expression(
    cfront_context_t *context, cfront_expression_value_t *value_out);
static ctool_bool cfront_starts_declaration_specifier(
    const cfront_context_t *context, const ctool_c_pp_token_t *token);
static ctool_status_t cfront_parse_type_name(cfront_context_t *context,
                                             ctool_u32 *type_out);
static ctool_status_t cfront_layout_query_now(
    cfront_context_t *context, ctool_u32 type,
    const cfront_vector_t *member_path,
    const ctool_c_pp_token_t *token, ctool_u32 diagnostic_code,
    const char *incomplete_message, ctool_c_type_layout_t *layout_out,
    ctool_u32 *member_offset_out, ctool_u32 *member_alignment_out);
static ctool_status_t cfront_resolve_member_path(
    cfront_context_t *context, ctool_u32 record_type, ctool_string_t name,
    const ctool_c_pp_token_t *member_token, cfront_vector_t *path);

static ctool_bool cfront_parenthesized_type_name_starts(
    const cfront_context_t *context) {
  const ctool_c_pp_token_t *after_open;
  if (cfront_peek_is(context, "(") == CTOOL_FALSE ||
      context->position == CFRONT_U32_MAX) {
    return CTOOL_FALSE;
  }
  after_open = cfront_token(context, context->position + 1u);
  return cfront_starts_declaration_specifier(context, after_open);
}

static ctool_status_t cfront_rewind_query_expressions(
    cfront_context_t *context, ctool_u32 expression_mark,
    ctool_u32 child_mark, ctool_arena_mark_t arena_mark,
    const ctool_c_pp_token_t *operator_token, ctool_status_t status) {
  ctool_status_t child_status = cfront_vector_rewind(
      &context->expression_children, child_mark);
  ctool_status_t expression_status = cfront_vector_rewind(
      &context->expressions, expression_mark);
  if (child_status != CTOOL_OK || expression_status != CTOOL_OK) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
        operator_token, "unevaluated expression scratch rewind failed");
  }
  if (status == CTOOL_OK &&
      ctool_arena_rewind(ctool_job_arena(context->job), arena_mark) !=
          CTOOL_OK) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
        operator_token, "unevaluated expression arena rewind failed");
  }
  return status;
}

static ctool_status_t cfront_expression_alignment_now(
    cfront_context_t *context, const cfront_expression_value_t *value,
    const ctool_c_pp_token_t *operator_token, ctool_u32 diagnostic_code,
    ctool_u32 *alignment_out) {
  ctool_c_expression_t expression;
  ctool_c_type_layout_t layout;
  cfront_vector_t member_path;
  ctool_u32 binding_reference = CTOOL_C_AST_NONE;
  ctool_u32 member_alignment = 0u;
  ctool_u32 diagnostic_count = ctool_job_diagnostic_count(context->job);
  ctool_status_t status = cfront_vector_get(
      &context->expressions, value->expression, &expression);
  cfront_zero(&layout, (ctool_u32)sizeof(layout));
  cfront_zero(&member_path, (ctool_u32)sizeof(member_path));
  if (status != CTOOL_OK) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
        operator_token, "alignment-query expression is unavailable");
  }
  if (expression.kind == CTOOL_C_EXPRESSION_IDENTIFIER) {
    binding_reference = expression.reference;
  }
  if (expression.kind == CTOOL_C_EXPRESSION_MEMBER) {
    status = cfront_vector_open(context, &member_path,
                                (ctool_u32)sizeof(ctool_u32));
    if (status == CTOOL_OK) {
      status = cfront_vector_append(&member_path, &expression.reference,
                                    (ctool_u32 *)0);
    }
    if (status == CTOOL_OK) {
      status = cfront_layout_query_now(
          context, value->type, &member_path, operator_token,
          diagnostic_code, "alignment query requires a complete object type",
          &layout, (ctool_u32 *)0, &member_alignment);
    }
    cfront_vector_close(&member_path);
    if (status != CTOOL_OK) {
      return status == CTOOL_ERR_LIMIT || status == CTOOL_ERR_OVERFLOW ||
                         status == CTOOL_ERR_NO_MEMORY
                 ? (ctool_job_diagnostic_count(context->job) ==
                            diagnostic_count
                        ? cfront_storage_failure(context, status)
                        : status)
                 : status;
    }
  } else {
    status = cfront_layout_query_now(
        context, value->type, (const cfront_vector_t *)0, operator_token,
        diagnostic_code, "alignment query requires a complete object type",
        &layout, (ctool_u32 *)0, (ctool_u32 *)0);
    if (status != CTOOL_OK) {
      return status;
    }
  }
  *alignment_out = member_alignment != 0u ? member_alignment : layout.alignment;
  if (binding_reference < context->bindings.count) {
    ctool_c_binding_t binding;
    status = cfront_binding_get(context, binding_reference, &binding);
    if (status != CTOOL_OK) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
          operator_token, "aligned object binding is unavailable");
    }
    if (binding.minimum_alignment > *alignment_out) {
      *alignment_out = binding.minimum_alignment;
    }
  }
  return CTOOL_OK;
}

static ctool_status_t cfront_parse_sizeof_query(
    cfront_context_t *context, const ctool_c_pp_token_t *operator_token,
    ctool_u32 diagnostic_code, ctool_u32 *size_out) {
  cfront_expression_value_t operand;
  ctool_c_type_layout_t layout;
  ctool_u32 expression_mark = cfront_vector_mark(&context->expressions);
  ctool_u32 child_mark = cfront_vector_mark(&context->expression_children);
  ctool_arena_mark_t arena_mark =
      ctool_arena_mark(ctool_job_arena(context->job));
  ctool_u32 operand_type = CTOOL_C_TYPE_NONE;
  ctool_bool expression_operand = CTOOL_FALSE;
  ctool_status_t status = cfront_enter_syntax(context, operator_token);
  cfront_zero(&operand, (ctool_u32)sizeof(operand));
  cfront_zero(&layout, (ctool_u32)sizeof(layout));
  if (status != CTOOL_OK) {
    return status;
  }
  if (cfront_parenthesized_type_name_starts(context) == CTOOL_TRUE) {
    (void)cfront_advance(context);
    status = cfront_parse_type_name(context, &operand_type);
    if (status == CTOOL_OK) {
      status = cfront_expected(context, ")");
    }
  } else {
    expression_operand = CTOOL_TRUE;
    status = cfront_parse_body_unary(context, &operand);
    if (status == CTOOL_OK) {
      operand_type = operand.type;
    }
  }
  if (status == CTOOL_OK && expression_operand == CTOOL_TRUE &&
      operand.is_bit_field == CTOOL_TRUE) {
    status = cfront_emit_failure(
        context, CTOOL_ERR_INPUT, diagnostic_code, operator_token,
        "sizeof cannot apply to a bit-field");
  }
  if (status == CTOOL_OK) {
    status = cfront_layout_query_now(
        context, operand_type, (const cfront_vector_t *)0, operator_token,
        diagnostic_code, "sizeof requires a complete object type", &layout,
        (ctool_u32 *)0, (ctool_u32 *)0);
  }
  if (status == CTOOL_OK) {
    *size_out = layout.size;
  }
  status = cfront_rewind_query_expressions(
      context, expression_mark, child_mark, arena_mark, operator_token,
      status);
  cfront_leave_syntax(context);
  return status;
}

static ctool_status_t cfront_parse_alignof_query(
    cfront_context_t *context, const ctool_c_pp_token_t *operator_token,
    ctool_u32 diagnostic_code, ctool_u32 *alignment_out) {
  cfront_expression_value_t operand;
  ctool_c_type_layout_t layout;
  ctool_u32 expression_mark = cfront_vector_mark(&context->expressions);
  ctool_u32 child_mark = cfront_vector_mark(&context->expression_children);
  ctool_arena_mark_t arena_mark =
      ctool_arena_mark(ctool_job_arena(context->job));
  ctool_u32 operand_type = CTOOL_C_TYPE_NONE;
  ctool_bool expression_operand = CTOOL_FALSE;
  ctool_bool standard =
      cfront_token_is(operator_token, "_Alignof") == CTOOL_TRUE;
  ctool_status_t status;
  cfront_zero(&operand, (ctool_u32)sizeof(operand));
  cfront_zero(&layout, (ctool_u32)sizeof(layout));
  if (standard == CTOOL_FALSE &&
      context->request->gnu_extensions == CTOOL_FALSE) {
    return cfront_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, diagnostic_code, operator_token,
        "GNU alignment queries require GNU extensions");
  }
  status = cfront_enter_syntax(context, operator_token);
  if (status != CTOOL_OK) {
    return status;
  }
  if (cfront_parenthesized_type_name_starts(context) == CTOOL_TRUE) {
    (void)cfront_advance(context);
    status = cfront_parse_type_name(context, &operand_type);
    if (status == CTOOL_OK) {
      status = cfront_expected(context, ")");
    }
  } else if (standard == CTOOL_TRUE) {
    status = cfront_emit_failure(
        context, CTOOL_ERR_INPUT, diagnostic_code, cfront_peek(context),
        "_Alignof requires a parenthesized type name");
  } else {
    expression_operand = CTOOL_TRUE;
    status = cfront_parse_body_unary(context, &operand);
    if (status == CTOOL_OK) {
      operand_type = operand.type;
    }
  }
  if (status == CTOOL_OK && expression_operand == CTOOL_TRUE &&
      operand.is_bit_field == CTOOL_TRUE) {
    status = cfront_emit_failure(
        context, CTOOL_ERR_INPUT, diagnostic_code, operator_token,
        "alignment query cannot apply to a bit-field");
  }
  if (status == CTOOL_OK && expression_operand == CTOOL_TRUE) {
    status = cfront_expression_alignment_now(
        context, &operand, operator_token, diagnostic_code, alignment_out);
  } else if (status == CTOOL_OK) {
    status = cfront_layout_query_now(
        context, operand_type, (const cfront_vector_t *)0, operator_token,
        diagnostic_code, "alignment query requires a complete object type",
        &layout, (ctool_u32 *)0, (ctool_u32 *)0);
    if (status == CTOOL_OK) {
      *alignment_out = layout.alignment;
    }
  }
  status = cfront_rewind_query_expressions(
      context, expression_mark, child_mark, arena_mark, operator_token,
      status);
  cfront_leave_syntax(context);
  return status;
}

static ctool_status_t cfront_parse_offsetof_query(
    cfront_context_t *context, const ctool_c_pp_token_t *builtin_token,
    ctool_u32 diagnostic_code, ctool_u32 *offset_out) {
  cfront_vector_t path;
  cfront_vector_t segment;
  ctool_c_type_layout_t record_layout;
  ctool_c_type_node_t root_node;
  ctool_c_record_member_t final_member;
  const ctool_c_pp_token_t *member_token = (const ctool_c_pp_token_t *)0;
  ctool_u32 root_type = CTOOL_C_TYPE_NONE;
  ctool_u32 current_type = CTOOL_C_TYPE_NONE;
  ctool_u32 root_base = CTOOL_C_TYPE_NONE;
  ctool_u32 root_qualifiers = 0u;
  ctool_bool have_final_member = CTOOL_FALSE;
  ctool_u32 diagnostic_count = ctool_job_diagnostic_count(context->job);
  ctool_status_t status;
  cfront_zero(&path, (ctool_u32)sizeof(path));
  cfront_zero(&segment, (ctool_u32)sizeof(segment));
  cfront_zero(&record_layout, (ctool_u32)sizeof(record_layout));
  cfront_zero(&root_node, (ctool_u32)sizeof(root_node));
  cfront_zero(&final_member, (ctool_u32)sizeof(final_member));
  if (context->request->gnu_extensions == CTOOL_FALSE) {
    return cfront_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, diagnostic_code, builtin_token,
        "__builtin_offsetof requires GNU extensions");
  }
  status = cfront_enter_syntax(context, builtin_token);
  if (status != CTOOL_OK) {
    return status;
  }
  status = cfront_expected(context, "(");
  if (status == CTOOL_OK) {
    status = cfront_parse_type_name(context, &root_type);
  }
  if (status == CTOOL_OK) {
    status = cfront_underlying_type(context, root_type, &root_base,
                                    &root_qualifiers, &root_node);
    (void)root_base;
    (void)root_qualifiers;
    if (status != CTOOL_OK) {
      status = cfront_emit_failure(
          context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
          builtin_token, "offsetof record type is unavailable");
    } else if (root_node.kind != CTOOL_C_TYPE_RECORD ||
               root_node.record_complete == CTOOL_FALSE) {
      status = cfront_emit_failure(
          context, CTOOL_ERR_INPUT, diagnostic_code, builtin_token,
          "offsetof requires a complete record or union type");
    }
  }
  if (status == CTOOL_OK) {
    status = cfront_expected(context, ",");
  }
  if (status == CTOOL_OK) {
    status = cfront_vector_open(context, &path,
                                (ctool_u32)sizeof(ctool_u32));
  }
  if (status == CTOOL_OK) {
    status = cfront_vector_open(context, &segment,
                                (ctool_u32)sizeof(ctool_u32));
  }
  current_type = root_type;
  while (status == CTOOL_OK) {
    ctool_u32 index;
    ctool_u32 member_index = CTOOL_C_AST_NONE;
    member_token = cfront_peek(context);
    if (member_token == (const ctool_c_pp_token_t *)0 ||
        member_token->kind != CTOOL_C_PP_TOKEN_IDENTIFIER) {
      status = cfront_emit_failure(
          context, CTOOL_ERR_INPUT, diagnostic_code, member_token,
          "offsetof member designator requires an identifier");
      break;
    }
    status = cfront_vector_rewind(&segment, 0u);
    if (status == CTOOL_OK) {
      status = cfront_resolve_member_path(
          context, current_type, member_token->spelling, member_token,
          &segment);
    }
    for (index = 0u; status == CTOOL_OK && index < segment.count; index++) {
      status = cfront_vector_get(&segment, index, &member_index);
      if (status == CTOOL_OK) {
        status = cfront_vector_append(&path, &member_index,
                                      (ctool_u32 *)0);
      }
    }
    if (status == CTOOL_OK && member_index != CTOOL_C_AST_NONE) {
      status = cfront_vector_get(&context->members, member_index,
                                 &final_member);
      if (status == CTOOL_OK) {
        current_type = final_member.type;
        have_final_member = CTOOL_TRUE;
      }
    }
    if (status != CTOOL_OK) {
      break;
    }
    (void)cfront_advance(context);
    if (cfront_peek_is(context, ".") == CTOOL_TRUE) {
      (void)cfront_advance(context);
      continue;
    }
    if (cfront_peek_is(context, "[") == CTOOL_TRUE) {
      status = cfront_emit_failure(
          context, CTOOL_ERR_UNSUPPORTED, diagnostic_code,
          cfront_peek(context),
          "offsetof array designators are outside this expression slice");
    } else if (cfront_peek_is(context, ")") == CTOOL_FALSE) {
      status = cfront_emit_failure(
          context, CTOOL_ERR_INPUT, diagnostic_code, cfront_peek(context),
          "builtin offsetof member designator is invalid");
    }
    break;
  }
  if (status == CTOOL_OK && have_final_member == CTOOL_FALSE) {
    status = cfront_emit_failure(
        context, CTOOL_ERR_INPUT, diagnostic_code, member_token,
        "offsetof requires a member designator");
  }
  if (status == CTOOL_OK && final_member.is_bit_field == CTOOL_TRUE) {
    status = cfront_emit_failure(
        context, CTOOL_ERR_INPUT, diagnostic_code, member_token,
        "offsetof cannot apply to a bit-field");
  }
  if (status == CTOOL_OK) {
    status = cfront_expected(context, ")");
  }
  if (status == CTOOL_OK) {
    status = cfront_layout_query_now(
        context, root_type, &path, builtin_token, diagnostic_code,
        "offsetof requires a complete record or union", &record_layout,
        offset_out, (ctool_u32 *)0);
  }
  cfront_vector_close(&segment);
  cfront_vector_close(&path);
  cfront_leave_syntax(context);
  if (status == CTOOL_ERR_LIMIT || status == CTOOL_ERR_OVERFLOW ||
      status == CTOOL_ERR_NO_MEMORY) {
    return ctool_job_diagnostic_count(context->job) == diagnostic_count
               ? cfront_storage_failure(context, status)
               : status;
  }
  return status;
}

static ctool_status_t cfront_parse_constant_primary(
    cfront_context_t *context, cfront_integer_t *value_out) {
  const ctool_c_pp_token_t *token = cfront_peek(context);
  ctool_c_binding_t binding;
  ctool_status_t status;
  if (token == (const ctool_c_pp_token_t *)0) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION,
        token, "integer constant expression is incomplete");
  }
  if (cfront_token_is(token, "(") == CTOOL_TRUE) {
    status = cfront_enter_syntax(context, token);
    if (status != CTOOL_OK) {
      return status;
    }
    (void)cfront_advance(context);
    status = cfront_parse_constant_logical_or(context, value_out);
    if (status == CTOOL_OK) {
      status = cfront_expected(context, ")");
    }
    cfront_leave_syntax(context);
    return status;
  }
  if (token->kind == CTOOL_C_PP_TOKEN_NUMBER) {
    (void)cfront_advance(context);
    return cfront_parse_number_token(
        context, token, CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION, value_out,
        (ctool_c_type_kind_t *)0);
  }
  if (token->kind == CTOOL_C_PP_TOKEN_IDENTIFIER &&
      cfront_find_binding(context, token->spelling, &binding) == CTOOL_TRUE &&
      binding.kind == CTOOL_C_BINDING_ENUMERATOR) {
    (void)cfront_advance(context);
    value_out->bits = binding.integer_bits;
    {
      ctool_u32 base;
      ctool_u32 qualifiers;
      ctool_c_type_node_t node;
      status = cfront_underlying_type(context, binding.type, &base,
                                      &qualifiers, &node);
      (void)base;
      (void)qualifiers;
      if (status != CTOOL_OK) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
            token, "enumerator type is unavailable");
      }
      if (node.kind == CTOOL_C_TYPE_SIGNED_LONG_LONG) {
        value_out->kind = CFRONT_INTEGER_SIGNED_64;
      } else if (node.kind == CTOOL_C_TYPE_UNSIGNED_LONG_LONG) {
        value_out->kind = CFRONT_INTEGER_UNSIGNED_64;
      } else if (node.kind == CTOOL_C_TYPE_UNSIGNED_INT ||
                 node.kind == CTOOL_C_TYPE_UNSIGNED_LONG) {
        value_out->kind = CFRONT_INTEGER_UNSIGNED_32;
      } else if (node.kind == CTOOL_C_TYPE_SIGNED_INT ||
                 node.kind == CTOOL_C_TYPE_SIGNED_LONG) {
        value_out->kind = CFRONT_INTEGER_SIGNED_32;
      } else {
        return cfront_emit_failure(
            context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
            token, "enumerator binding has a non-integer declared type");
      }
      value_out->bits = cfront_integer_normalize_bits(value_out->bits,
                                                      value_out->kind);
    }
    return CTOOL_OK;
  }
  return cfront_emit_failure(
      context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION,
      token, "integer constant expression operand is unsupported");
}

static ctool_status_t cfront_parse_constant_unary(
    cfront_context_t *context, cfront_integer_t *value_out) {
  if (cfront_peek_is(context, "!") == CTOOL_TRUE) {
    const ctool_c_pp_token_t *operator_token = cfront_peek(context);
    ctool_status_t status = cfront_enter_syntax(context, operator_token);
    if (status != CTOOL_OK) {
      return status;
    }
    (void)cfront_advance(context);
    status = cfront_parse_constant_unary(context, value_out);
    if (status == CTOOL_OK) {
      value_out->bits = value_out->bits == 0ull ? 1ull : 0ull;
      value_out->kind = CFRONT_INTEGER_SIGNED_32;
    }
    cfront_leave_syntax(context);
    return status;
  }
  if (cfront_peek_is(context, "sizeof") == CTOOL_TRUE) {
    const ctool_c_pp_token_t *operator_token = cfront_advance(context);
    ctool_u32 size = 0u;
    ctool_status_t status = cfront_parse_sizeof_query(
        context, operator_token, CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION,
        &size);
    if (status == CTOOL_OK) {
      value_out->bits = size;
      value_out->kind = CFRONT_INTEGER_UNSIGNED_32;
    }
    return status;
  }
  if (cfront_peek_is(context, "_Alignof") == CTOOL_TRUE ||
      cfront_peek_is(context, "__alignof") == CTOOL_TRUE ||
      cfront_peek_is(context, "__alignof__") == CTOOL_TRUE) {
    const ctool_c_pp_token_t *operator_token = cfront_advance(context);
    ctool_u32 alignment = 0u;
    ctool_status_t status = cfront_parse_alignof_query(
        context, operator_token, CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION,
        &alignment);
    if (status == CTOOL_OK) {
      value_out->bits = alignment;
      value_out->kind = CFRONT_INTEGER_UNSIGNED_32;
    }
    return status;
  }
  if (cfront_peek_is(context, "__builtin_offsetof") == CTOOL_TRUE) {
    const ctool_c_pp_token_t *builtin_token = cfront_advance(context);
    ctool_u32 offset = 0u;
    ctool_status_t status = cfront_parse_offsetof_query(
        context, builtin_token, CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION,
        &offset);
    if (status == CTOOL_OK) {
      value_out->bits = offset;
      value_out->kind = CFRONT_INTEGER_UNSIGNED_32;
    }
    return status;
  }
  if (cfront_peek_is(context, "+") == CTOOL_TRUE) {
    const ctool_c_pp_token_t *operator_token = cfront_peek(context);
    ctool_status_t status = cfront_enter_syntax(context, operator_token);
    if (status != CTOOL_OK) {
      return status;
    }
    (void)cfront_advance(context);
    status = cfront_parse_constant_unary(context, value_out);
    cfront_leave_syntax(context);
    return status;
  }
  if (cfront_peek_is(context, "-") == CTOOL_TRUE) {
    const ctool_c_pp_token_t *operator_token = cfront_peek(context);
    ctool_status_t status = cfront_enter_syntax(context, operator_token);
    if (status != CTOOL_OK) {
      return status;
    }
    (void)cfront_advance(context);
    status = cfront_parse_constant_unary(context, value_out);
    if (status == CTOOL_OK) {
      if (cfront_integer_unsigned(value_out->kind) == CTOOL_FALSE &&
          cfront_integer_magnitude(value_out) ==
              (cfront_integer_width(value_out->kind) == 32u
                   ? 0x80000000ull
                   : 0x8000000000000000ull) &&
          context->constant_evaluation_suppression_depth == 0u) {
        status = cfront_integer_overflow(context, operator_token);
      } else {
        value_out->bits = cfront_integer_normalize_bits(
            0ull - value_out->bits, value_out->kind);
      }
    }
    cfront_leave_syntax(context);
    return status;
  }
  if (cfront_peek_is(context, "~") == CTOOL_TRUE) {
    const ctool_c_pp_token_t *operator_token = cfront_peek(context);
    ctool_status_t status = cfront_enter_syntax(context, operator_token);
    if (status != CTOOL_OK) {
      return status;
    }
    (void)cfront_advance(context);
    status = cfront_parse_constant_unary(context, value_out);
    if (status == CTOOL_OK) {
      value_out->bits = cfront_integer_normalize_bits(~value_out->bits,
                                                      value_out->kind);
    }
    cfront_leave_syntax(context);
    return status;
  }
  return cfront_parse_constant_primary(context, value_out);
}

static ctool_status_t cfront_parse_constant_multiply(
    cfront_context_t *context, cfront_integer_t *value_out) {
  value_out->bits = 0ull;
  value_out->kind = CFRONT_INTEGER_SIGNED_32;
  ctool_status_t status = cfront_parse_constant_unary(context, value_out);
  while (status == CTOOL_OK &&
         (cfront_peek_is(context, "*") == CTOOL_TRUE ||
          cfront_peek_is(context, "/") == CTOOL_TRUE ||
          cfront_peek_is(context, "%") == CTOOL_TRUE)) {
    const ctool_c_pp_token_t *operator_token = cfront_advance(context);
    cfront_integer_t right = {0ull, CFRONT_INTEGER_SIGNED_32};
    status = cfront_parse_constant_unary(context, &right);
    if (status != CTOOL_OK) {
      break;
    }
    {
      cfront_integer_kind_t kind =
          cfront_integer_usual_kind(value_out->kind, right.kind);
      cfront_integer_t left = cfront_integer_convert(*value_out, kind);
      right = cfront_integer_convert(right, kind);
      value_out->kind = kind;
      if (cfront_token_is(operator_token, "*") == CTOOL_TRUE) {
        if (cfront_integer_unsigned(kind) == CTOOL_TRUE) {
          value_out->bits = cfront_integer_normalize_bits(
              left.bits * right.bits, kind);
        } else {
          ctool_bool negative =
              cfront_integer_negative(&left) != cfront_integer_negative(&right)
                  ? CTOOL_TRUE
                  : CTOOL_FALSE;
          ctool_u64 left_magnitude = cfront_integer_magnitude(&left);
          ctool_u64 right_magnitude = cfront_integer_magnitude(&right);
          ctool_u64 limit =
              negative == CTOOL_TRUE
                  ? (cfront_integer_width(kind) == 32u
                         ? 0x80000000ull
                         : 0x8000000000000000ull)
                  : (cfront_integer_width(kind) == 32u
                         ? 0x7fffffffull
                         : 0x7fffffffffffffffull);
          if (left_magnitude != 0ull &&
              right_magnitude > limit / left_magnitude &&
              context->constant_evaluation_suppression_depth == 0u) {
            return cfront_integer_overflow(context, operator_token);
          }
          value_out->bits = left_magnitude * right_magnitude;
          if (negative == CTOOL_TRUE) {
            value_out->bits = 0ull - value_out->bits;
          }
          value_out->bits =
              cfront_integer_normalize_bits(value_out->bits, kind);
        }
      } else {
        ctool_u64 divisor = cfront_integer_magnitude(&right);
        if (divisor == 0ull) {
          if (context->constant_evaluation_suppression_depth == 0u) {
            return cfront_emit_failure(
                context, CTOOL_ERR_INPUT,
                CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION, operator_token,
                "integer constant expression divides by zero");
          }
          value_out->bits = 0ull;
        } else if (cfront_integer_unsigned(kind) == CTOOL_TRUE) {
          value_out->bits =
              cfront_token_is(operator_token, "/") == CTOOL_TRUE
                  ? left.bits / right.bits
                  : left.bits % right.bits;
        } else {
          ctool_u64 dividend = cfront_integer_magnitude(&left);
          if (cfront_integer_negative(&left) == CTOOL_TRUE &&
              cfront_integer_negative(&right) == CTOOL_TRUE &&
              dividend == (cfront_integer_width(kind) == 32u
                                ? 0x80000000ull
                                : 0x8000000000000000ull) &&
              divisor == 1ull &&
              context->constant_evaluation_suppression_depth == 0u) {
            return cfront_integer_overflow(context, operator_token);
          }
          if (cfront_token_is(operator_token, "/") == CTOOL_TRUE) {
            ctool_bool negative =
                cfront_integer_negative(&left) !=
                        cfront_integer_negative(&right)
                    ? CTOOL_TRUE
                    : CTOOL_FALSE;
            ctool_u64 quotient = dividend / divisor;
            ctool_u64 limit =
                negative == CTOOL_TRUE
                    ? (cfront_integer_width(kind) == 32u
                           ? 0x80000000ull
                           : 0x8000000000000000ull)
                    : (cfront_integer_width(kind) == 32u
                           ? 0x7fffffffull
                           : 0x7fffffffffffffffull);
            if (quotient > limit &&
                context->constant_evaluation_suppression_depth == 0u) {
              return cfront_integer_overflow(context, operator_token);
            }
            value_out->bits =
                negative == CTOOL_TRUE ? 0ull - quotient : quotient;
          } else {
            ctool_u64 remainder = dividend % divisor;
            value_out->bits = cfront_integer_negative(&left) == CTOOL_TRUE
                                  ? 0ull - remainder
                                  : remainder;
          }
          value_out->bits =
              cfront_integer_normalize_bits(value_out->bits, kind);
        }
      }
    }
  }
  return status;
}

static ctool_status_t cfront_parse_specifiers(cfront_context_t *context,
                                               cfront_specifiers_t *spec_out);
static ctool_status_t cfront_parse_declarator(cfront_context_t *context,
                                              ctool_bool allow_abstract,
                                              ctool_u32 *root_out);
static ctool_status_t cfront_parse_declarator_body(
    cfront_context_t *context, ctool_bool allow_abstract,
    ctool_u32 *root_out);
static ctool_status_t cfront_build_declarator(
    cfront_context_t *context, ctool_u32 root, ctool_u32 base_type,
    ctool_u32 *type_out, ctool_string_t *name_out,
    ctool_c_pp_location_t *location_out,
    ctool_c_pp_location_t *physical_location_out);
static ctool_status_t cfront_parse_member_declaration(
    cfront_context_t *context, ctool_u32 *member_head,
    ctool_u32 *member_count);
static ctool_status_t cfront_validate_completed_record(
    cfront_context_t *context, ctool_c_record_kind_t record_kind,
    ctool_u32 first_member, ctool_u32 member_count,
    const ctool_c_pp_token_t *diagnostic_token);

static ctool_bool cfront_starts_attribute(
    const cfront_context_t *context) {
  return cfront_peek_is(context, "__attribute__") == CTOOL_TRUE ||
                 cfront_peek_is(context, "__attribute") == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_bool cfront_attributes_any(
    const cfront_attributes_t *attributes) {
  return attributes->packed == CTOOL_TRUE ||
                 attributes->has_alignment == CTOOL_TRUE ||
                 attributes->noreturn == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static const ctool_c_pp_token_t *cfront_first_attribute_token(
    const cfront_attributes_t *attributes) {
  if (attributes->packed_token != (const ctool_c_pp_token_t *)0) {
    return attributes->packed_token;
  }
  if (attributes->alignment_token != (const ctool_c_pp_token_t *)0) {
    return attributes->alignment_token;
  }
  return attributes->noreturn_token;
}

static ctool_status_t cfront_attribute_expected(
    cfront_context_t *context, const char *spelling, const char *message) {
  if (cfront_peek_is(context, spelling) == CTOOL_FALSE) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_ATTRIBUTE,
        cfront_peek(context), message);
  }
  (void)cfront_advance(context);
  return CTOOL_OK;
}

static ctool_status_t cfront_parse_attributes(
    cfront_context_t *context, cfront_attributes_t *attributes) {
  while (cfront_starts_attribute(context) == CTOOL_TRUE) {
    const ctool_c_pp_token_t *attribute = cfront_advance(context);
    ctool_status_t status;
    if (context->request->gnu_extensions == CTOOL_FALSE) {
      return cfront_emit_failure(
          context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_ATTRIBUTE,
          attribute, "GNU attributes require GNU extensions");
    }
    status = cfront_attribute_expected(
        context, "(", "GNU attribute requires two opening parentheses");
    if (status == CTOOL_OK) {
      status = cfront_attribute_expected(
          context, "(", "GNU attribute requires two opening parentheses");
    }
    if (status != CTOOL_OK) {
      return status;
    }
    while (cfront_peek_is(context, ")") == CTOOL_FALSE) {
      const ctool_c_pp_token_t *name = cfront_peek(context);
      if (name == (const ctool_c_pp_token_t *)0) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_ATTRIBUTE, attribute,
            "GNU attribute list is not terminated");
      }
      if (cfront_peek_is(context, ",") == CTOOL_TRUE) {
        (void)cfront_advance(context);
        continue;
      }
      if (cfront_token_is(name, "packed") == CTOOL_TRUE ||
          cfront_token_is(name, "__packed__") == CTOOL_TRUE) {
        attributes->packed = CTOOL_TRUE;
        if (attributes->packed_token == (const ctool_c_pp_token_t *)0) {
          attributes->packed_token = name;
        }
        (void)cfront_advance(context);
        if (cfront_peek_is(context, "(") == CTOOL_TRUE) {
          (void)cfront_advance(context);
          if (cfront_peek_is(context, ")") == CTOOL_FALSE) {
            return cfront_emit_failure(
                context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_ATTRIBUTE,
                cfront_peek(context),
                "packed attribute does not accept arguments");
          }
          (void)cfront_advance(context);
        }
      } else if (cfront_token_is(name, "aligned") == CTOOL_TRUE ||
                 cfront_token_is(name, "__aligned__") == CTOOL_TRUE) {
        cfront_integer_t alignment = {CFRONT_GNU_DEFAULT_ALIGNMENT,
                                      CFRONT_INTEGER_SIGNED_32};
        const ctool_c_pp_token_t *argument = name;
        (void)cfront_advance(context);
        if (cfront_peek_is(context, "(") == CTOOL_TRUE) {
          (void)cfront_advance(context);
          argument = cfront_peek(context);
          if (cfront_peek_is(context, ")") == CTOOL_FALSE) {
            status = cfront_parse_constant_logical_or(context, &alignment);
            if (status != CTOOL_OK) {
              return status;
            }
          }
          status = cfront_attribute_expected(
              context, ")", "aligned attribute argument is not terminated");
          if (status != CTOOL_OK) {
            return status;
          }
        }
        if (cfront_integer_negative(&alignment) == CTOOL_TRUE ||
            alignment.bits == 0ull || alignment.bits > CFRONT_U32_MAX ||
            (alignment.bits & (alignment.bits - 1ull)) != 0ull) {
          return cfront_emit_failure(
              context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_ATTRIBUTE, argument,
              "aligned attribute requires a positive power-of-two value");
        }
        if (attributes->has_alignment == CTOOL_FALSE ||
            alignment.bits > (ctool_u64)attributes->alignment) {
          attributes->alignment = (ctool_u32)alignment.bits;
          attributes->alignment_token = name;
        }
        attributes->has_alignment = CTOOL_TRUE;
      } else if (cfront_token_is(name, "noreturn") == CTOOL_TRUE ||
                 cfront_token_is(name, "__noreturn__") == CTOOL_TRUE) {
        attributes->noreturn = CTOOL_TRUE;
        if (attributes->noreturn_token == (const ctool_c_pp_token_t *)0) {
          attributes->noreturn_token = name;
        }
        (void)cfront_advance(context);
        if (cfront_peek_is(context, "(") == CTOOL_TRUE) {
          (void)cfront_advance(context);
          if (cfront_peek_is(context, ")") == CTOOL_FALSE) {
            return cfront_emit_failure(
                context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_ATTRIBUTE,
                cfront_peek(context),
                "noreturn attribute does not accept arguments");
          }
          (void)cfront_advance(context);
        }
      } else {
        return cfront_emit_failure(
            context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_ATTRIBUTE,
            name, "GNU attribute is not supported");
      }
      if (cfront_peek_is(context, ",") == CTOOL_FALSE) {
        break;
      }
      (void)cfront_advance(context);
    }
    status = cfront_attribute_expected(
        context, ")", "GNU attribute requires two closing parentheses");
    if (status == CTOOL_OK) {
      status = cfront_attribute_expected(
          context, ")", "GNU attribute requires two closing parentheses");
    }
    if (status != CTOOL_OK) {
      return status;
    }
  }
  return CTOOL_OK;
}

static ctool_status_t cfront_static_assert_expected(
    cfront_context_t *context, const char *spelling, const char *message) {
  if (cfront_peek_is(context, spelling) == CTOOL_FALSE) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_STATIC_ASSERT,
        cfront_peek(context), message);
  }
  (void)cfront_advance(context);
  return CTOOL_OK;
}

static ctool_status_t cfront_string_literal_interior(
    const ctool_c_pp_token_t *token, ctool_bytes_t *interior_out) {
  ctool_u32 first_quote = 0u;
  ctool_u32 last_quote;
  if (token == (const ctool_c_pp_token_t *)0 ||
      interior_out == (ctool_bytes_t *)0 ||
      token->kind != CTOOL_C_PP_TOKEN_STRING) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  last_quote = token->spelling.size;
  while (first_quote < token->spelling.size &&
         token->spelling.data[first_quote] != '"') {
    first_quote++;
  }
  while (last_quote > first_quote &&
         token->spelling.data[last_quote - 1u] != '"') {
    last_quote--;
  }
  if (first_quote >= token->spelling.size ||
      last_quote <= first_quote + 1u) {
    return CTOOL_ERR_INTERNAL;
  }
  *interior_out =
      ctool_bytes(token->spelling.data + first_quote + 1u,
                  last_quote - first_quote - 2u);
  return CTOOL_OK;
}

static ctool_status_t cfront_static_assert_false(
    cfront_context_t *context, const ctool_c_pp_token_t *keyword,
    ctool_u32 message_first, ctool_u32 message_end) {
  static const char prefix[] = "static assertion failed: ";
  const ctool_limits_t *limits = ctool_job_limits(context->job);
  ctool_arena_t *arena = ctool_job_arena(context->job);
  ctool_arena_mark_t mark = ctool_arena_mark(arena);
  char *message = (char *)0;
  ctool_u32 message_size = (ctool_u32)sizeof(prefix) - 1u;
  ctool_u32 cursor;
  ctool_u32 index;
  ctool_status_t status = CTOOL_OK;
  ctool_status_t rewind_status;
  for (index = message_first; index < message_end; index++) {
    const ctool_c_pp_token_t *token = cfront_token(context, index);
    ctool_bytes_t interior;
    status = cfront_string_literal_interior(token, &interior);
    if (status != CTOOL_OK) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL, keyword,
          "static assertion message token is malformed");
    }
    if (message_size > CFRONT_U32_MAX - interior.size) {
      return cfront_emit_failure(
          context, CTOOL_ERR_OVERFLOW, CTOOL_C_PARSE_DIAG_OVERFLOW, keyword,
          "static assertion diagnostic exceeds 32-bit size");
    }
    message_size += interior.size;
  }
  if (message_size > limits->diagnostic_message_bytes) {
    return cfront_emit_failure(
        context, CTOOL_ERR_LIMIT, CTOOL_C_PARSE_DIAG_LIMIT, keyword,
        "static assertion diagnostic exceeds configured limit");
  }
  status = ctool_arena_alloc(arena, message_size, 1u, (void **)&message);
  if (status == CTOOL_OK) {
    ctool_string_t text;
    cfront_copy(message, prefix, (ctool_u32)sizeof(prefix) - 1u);
    cursor = (ctool_u32)sizeof(prefix) - 1u;
    for (index = message_first; index < message_end; index++) {
      const ctool_c_pp_token_t *token = cfront_token(context, index);
      ctool_bytes_t interior;
      status = cfront_string_literal_interior(token, &interior);
      if (status != CTOOL_OK) {
        break;
      }
      cfront_copy(message + cursor, interior.data, interior.size);
      cursor += interior.size;
    }
    if (status == CTOOL_OK) {
      text.data = message;
      text.size = message_size;
      status = cfront_emit_failure_string(
          context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_STATIC_ASSERT,
          keyword, text);
    }
  } else {
    status = cfront_emit_failure(
        context, status,
        status == CTOOL_ERR_OVERFLOW ? CTOOL_C_PARSE_DIAG_OVERFLOW
                                     : CTOOL_C_PARSE_DIAG_LIMIT,
        keyword, status == CTOOL_ERR_OVERFLOW
                     ? "static assertion diagnostic exceeds 32-bit size"
                     : "static assertion diagnostic storage limit exceeded");
  }
  rewind_status = ctool_arena_rewind(arena, mark);
  if (rewind_status != CTOOL_OK) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL, keyword,
        "static assertion diagnostic scratch rewind failed");
  }
  return status;
}

static ctool_status_t cfront_parse_static_assert(
    cfront_context_t *context) {
  const ctool_c_pp_token_t *keyword = cfront_advance(context);
  cfront_integer_t value = {0ull, CFRONT_INTEGER_SIGNED_32};
  ctool_u32 message_first = 0u;
  ctool_u32 message_end = 0u;
  ctool_status_t status = cfront_enter_syntax(context, keyword);
  if (status != CTOOL_OK) {
    return status;
  }
  status = cfront_static_assert_expected(
      context, "(", "static assertion requires an opening parenthesis");
  if (status == CTOOL_OK) {
    status = cfront_parse_constant_logical_or(context, &value);
  }
  if (status == CTOOL_OK) {
    status = cfront_static_assert_expected(
        context, ",", "C11 static assertion requires a message");
  }
  if (status == CTOOL_OK) {
    const ctool_c_pp_token_t *message = cfront_peek(context);
    if (message == (const ctool_c_pp_token_t *)0 ||
        message->kind != CTOOL_C_PP_TOKEN_STRING) {
      status = cfront_emit_failure(
          context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_STATIC_ASSERT,
          message, "static assertion message must be a string literal");
    } else {
      message_first = context->position;
      do {
        (void)cfront_advance(context);
      } while (cfront_peek(context) != (const ctool_c_pp_token_t *)0 &&
               cfront_peek(context)->kind == CTOOL_C_PP_TOKEN_STRING);
      message_end = context->position;
    }
  }
  if (status == CTOOL_OK) {
    status = cfront_static_assert_expected(
        context, ")", "static assertion requires a closing parenthesis");
  }
  if (status == CTOOL_OK) {
    status = cfront_static_assert_expected(
        context, ";", "static assertion declaration requires a semicolon");
  }
  if (status == CTOOL_OK && value.bits == 0ull) {
    status = cfront_static_assert_false(context, keyword, message_first,
                                        message_end);
  }
  cfront_leave_syntax(context);
  return status;
}

static ctool_bool cfront_starts_declaration_specifier(
    const cfront_context_t *context, const ctool_c_pp_token_t *token) {
  ctool_u32 typedef_type;
  if (cfront_token_is(token, "__attribute__") == CTOOL_TRUE ||
      cfront_token_is(token, "__attribute") == CTOOL_TRUE ||
      cfront_token_is(token, "typedef") == CTOOL_TRUE ||
      cfront_token_is(token, "extern") == CTOOL_TRUE ||
      cfront_token_is(token, "static") == CTOOL_TRUE ||
      cfront_token_is(token, "auto") == CTOOL_TRUE ||
      cfront_token_is(token, "register") == CTOOL_TRUE ||
      cfront_token_is(token, "inline") == CTOOL_TRUE ||
      cfront_token_is(token, "const") == CTOOL_TRUE ||
      cfront_token_is(token, "volatile") == CTOOL_TRUE ||
      cfront_token_is(token, "restrict") == CTOOL_TRUE ||
      cfront_token_is(token, "_Atomic") == CTOOL_TRUE ||
      cfront_token_is(token, "void") == CTOOL_TRUE ||
      cfront_token_is(token, "_Bool") == CTOOL_TRUE ||
      cfront_token_is(token, "char") == CTOOL_TRUE ||
      cfront_token_is(token, "short") == CTOOL_TRUE ||
      cfront_token_is(token, "int") == CTOOL_TRUE ||
      cfront_token_is(token, "long") == CTOOL_TRUE ||
      cfront_token_is(token, "float") == CTOOL_TRUE ||
      cfront_token_is(token, "double") == CTOOL_TRUE ||
      cfront_token_is(token, "signed") == CTOOL_TRUE ||
      cfront_token_is(token, "unsigned") == CTOOL_TRUE ||
      cfront_token_is(token, "struct") == CTOOL_TRUE ||
      cfront_token_is(token, "union") == CTOOL_TRUE ||
      cfront_token_is(token, "enum") == CTOOL_TRUE ||
      (context->request->mode == CTOOL_C_PP_MODE_CUPID &&
       cfront_token_is(token, "class") == CTOOL_TRUE)) {
    return CTOOL_TRUE;
  }
  return token != (const ctool_c_pp_token_t *)0 &&
                 token->kind == CTOOL_C_PP_TOKEN_IDENTIFIER &&
                 cfront_find_typedef(context, token->spelling,
                                     &typedef_type) == CTOOL_TRUE
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t cfront_record_type_body(
    cfront_context_t *context, ctool_c_record_kind_t record_kind,
    ctool_u32 *type_out, ctool_bool *anonymous_definition_out);

static ctool_status_t cfront_record_type(
    cfront_context_t *context, ctool_c_record_kind_t record_kind,
    ctool_u32 *type_out, ctool_bool *anonymous_definition_out) {
  ctool_status_t status = cfront_enter_syntax(context, cfront_peek(context));
  if (status != CTOOL_OK) {
    return status;
  }
  status = cfront_record_type_body(context, record_kind, type_out,
                                   anonymous_definition_out);
  cfront_leave_syntax(context);
  return status;
}

static ctool_status_t cfront_record_type_body(
    cfront_context_t *context, ctool_c_record_kind_t record_kind,
    ctool_u32 *type_out, ctool_bool *anonymous_definition_out) {
  const ctool_c_pp_token_t *keyword = cfront_advance(context);
  const ctool_c_pp_token_t *name_token;
  ctool_string_t name = ctool_string("");
  ctool_c_tag_t existing_tag;
  ctool_c_type_node_t node;
  ctool_u32 type = CFRONT_NONE;
  ctool_bool has_existing = CTOOL_FALSE;
  cfront_attributes_t record_attributes;
  ctool_status_t status;
  cfront_zero(&record_attributes, (ctool_u32)sizeof(record_attributes));
  cfront_zero(&node, (ctool_u32)sizeof(node));
  *anonymous_definition_out = CTOOL_FALSE;
  status = cfront_parse_attributes(context, &record_attributes);
  if (status != CTOOL_OK) {
    return status;
  }
  name_token = cfront_peek(context);
  if (name_token != (const ctool_c_pp_token_t *)0 &&
      name_token->kind == CTOOL_C_PP_TOKEN_IDENTIFIER &&
      cfront_token_is(name_token, "{") == CTOOL_FALSE) {
    if (cfront_reserved_identifier(context, name_token) == CTOOL_TRUE) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_TYPE_NAME,
          name_token, "record tag cannot use a reserved word");
    }
    name = name_token->spelling;
    (void)cfront_advance(context);
    status = cfront_parse_attributes(context, &record_attributes);
    if (status != CTOOL_OK) {
      return status;
    }
    has_existing =
        context->prototype_scope_depth != 0u &&
                cfront_peek_is(context, "{") == CTOOL_TRUE
            ? cfront_find_tag_from(context, context->prototype_tag_mark,
                                   name, &existing_tag)
            : cfront_find_tag(context, name, &existing_tag);
    if (has_existing == CTOOL_TRUE) {
      status = cfront_type_get(context, existing_tag.type, &node);
      if (status != CTOOL_OK || node.kind != CTOOL_C_TYPE_RECORD ||
          node.record_kind != record_kind) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_REDEFINITION,
            name_token, "tag is redeclared with a different kind");
      }
      type = existing_tag.type;
    }
  }
  if (record_attributes.noreturn == CTOOL_TRUE) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_ATTRIBUTE,
        record_attributes.noreturn_token,
        "noreturn attribute cannot apply to a record type");
  }
  if (cfront_peek_is(context, "{") == CTOOL_FALSE) {
    if (name.size == 0u) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INPUT,
          CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS, keyword,
          "record specifier requires a tag or definition");
    }
    if (has_existing == CTOOL_FALSE) {
      cfront_node_init(&node, CTOOL_C_TYPE_RECORD, keyword);
      node.record_kind = record_kind;
      node.record_complete = CTOOL_FALSE;
      node.record_packed = record_attributes.packed;
      node.explicit_alignment = record_attributes.alignment;
      status = cfront_type_append(context, &node, &type);
      if (status == CTOOL_OK) {
        status = cfront_append_tag(context, name, type, name_token);
      }
      if (status != CTOOL_OK) {
        return cfront_storage_failure(context, status);
      }
    } else if ((record_attributes.packed == CTOOL_TRUE &&
                node.record_packed == CTOOL_FALSE) ||
               record_attributes.alignment > node.explicit_alignment) {
      if (record_attributes.packed == CTOOL_TRUE) {
        node.record_packed = CTOOL_TRUE;
      }
      if (record_attributes.alignment > node.explicit_alignment) {
        node.explicit_alignment = record_attributes.alignment;
      }
      status = cfront_type_update(context, type, &node);
      if (status != CTOOL_OK) {
        return cfront_storage_failure(context, status);
      }
    }
    *type_out = type;
    return CTOOL_OK;
  }
  if (has_existing == CTOOL_TRUE && node.record_complete == CTOOL_TRUE) {
    return cfront_emit_failure(context, CTOOL_ERR_INPUT,
                               CTOOL_C_PARSE_DIAG_REDEFINITION, name_token,
                               "record tag already has a definition");
  }
  if (has_existing == CTOOL_FALSE) {
    cfront_node_init(&node, CTOOL_C_TYPE_RECORD, keyword);
    node.record_kind = record_kind;
    node.record_complete = CTOOL_FALSE;
    status = cfront_type_append(context, &node, &type);
    if (status == CTOOL_OK && name.size != 0u) {
      status = cfront_append_tag(context, name, type, name_token);
    }
    if (status != CTOOL_OK) {
      return cfront_storage_failure(context, status);
    }
  }
  (void)cfront_advance(context);
  {
    ctool_u32 member_head = CFRONT_NONE;
    ctool_u32 member_count = 0u;
    ctool_u32 temporary_mark = cfront_vector_mark(&context->temporary_indices);
    ctool_u32 first_member;
    while (cfront_peek_is(context, "}") == CTOOL_FALSE) {
      if (cfront_peek(context) == (const ctool_c_pp_token_t *)0) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPECTED_TOKEN,
            cfront_peek(context), "record definition is missing a closing brace");
      }
      status = cfront_parse_member_declaration(context, &member_head,
                                               &member_count);
      if (status != CTOOL_OK) {
        return status;
      }
    }
    (void)cfront_advance(context);
    status = cfront_parse_attributes(context, &record_attributes);
    if (status != CTOOL_OK) {
      return status;
    }
    if (record_attributes.noreturn == CTOOL_TRUE) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_ATTRIBUTE,
          record_attributes.noreturn_token,
          "noreturn attribute cannot apply to a record type");
    }
    first_member = context->members.count;
    while (member_head != CFRONT_NONE) {
      cfront_pending_member_t pending;
      ctool_u32 index = member_head;
      status = cfront_vector_get(&context->pending_members, index, &pending);
      if (status != CTOOL_OK) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
            keyword, "record member chain is invalid");
      }
      status = cfront_vector_append(&context->temporary_indices, &index,
                                    (ctool_u32 *)0);
      if (status != CTOOL_OK) {
        return cfront_storage_failure(context, status);
      }
      member_head = pending.previous;
    }
    while (context->temporary_indices.count > temporary_mark) {
      ctool_u32 index;
      cfront_pending_member_t pending;
      status = cfront_vector_get(&context->temporary_indices,
                                 context->temporary_indices.count - 1u,
                                 &index);
      if (status == CTOOL_OK) {
        status = cfront_vector_rewind(&context->temporary_indices,
                                      context->temporary_indices.count - 1u);
      }
      if (status == CTOOL_OK) {
        status = cfront_vector_get(&context->pending_members, index, &pending);
      }
      if (status == CTOOL_OK) {
        status = cfront_vector_append(&context->members, &pending.member,
                                      (ctool_u32 *)0);
      }
      if (status != CTOOL_OK) {
        return cfront_storage_failure(context, status);
      }
    }
    status = cfront_type_get(context, type, &node);
    if (status != CTOOL_OK) {
      return cfront_emit_failure(context, CTOOL_ERR_INTERNAL,
                                 CTOOL_C_PARSE_DIAG_INTERNAL, keyword,
                                 "record placeholder is unavailable");
    }
    node.record_complete = CTOOL_TRUE;
    if (record_attributes.packed == CTOOL_TRUE) {
      node.record_packed = CTOOL_TRUE;
    }
    if (record_attributes.alignment > node.explicit_alignment) {
      node.explicit_alignment = record_attributes.alignment;
    }
    node.first_member = first_member;
    node.member_count = member_count;
    status = cfront_validate_completed_record(
        context, record_kind, first_member, member_count, keyword);
    if (status != CTOOL_OK) {
      return status;
    }
    status = cfront_type_update(context, type, &node);
    if (status != CTOOL_OK) {
      return cfront_storage_failure(context, status);
    }
  }
  *type_out = type;
  *anonymous_definition_out = name.size == 0u ? CTOOL_TRUE : CTOOL_FALSE;
  return CTOOL_OK;
}

static ctool_status_t cfront_append_declarator(
    cfront_context_t *context, const cfront_declarator_t *declarator,
    ctool_u32 *root_out) {
  ctool_status_t status = cfront_vector_append(
      &context->declarators, declarator, root_out);
  return status == CTOOL_OK ? CTOOL_OK : cfront_storage_failure(context, status);
}

static ctool_status_t cfront_adjust_parameter_type(
    cfront_context_t *context, ctool_u32 declared_type,
    const ctool_c_pp_token_t *token, ctool_u32 *object_type_out,
    ctool_u32 *function_type_out) {
  ctool_u32 base;
  ctool_u32 qualifiers;
  ctool_u32 adjusted = declared_type;
  ctool_c_type_node_t node;
  ctool_status_t status = cfront_underlying_type(
      context, declared_type, &base, &qualifiers, &node);
  (void)base;
  if (status != CTOOL_OK) {
    return cfront_emit_failure(context, CTOOL_ERR_INTERNAL,
                               CTOOL_C_PARSE_DIAG_INTERNAL, token,
                               "parameter type is unavailable");
  }
  if (node.kind == CTOOL_C_TYPE_ARRAY) {
    ctool_c_type_node_t pointer;
    ctool_u32 referenced = node.referenced_type;
    status = cfront_qualified_type(context, referenced, qualifiers, token,
                                   &referenced);
    if (status != CTOOL_OK) {
      return cfront_storage_failure(context, status);
    }
    cfront_node_init(&pointer, CTOOL_C_TYPE_POINTER, token);
    pointer.referenced_type = referenced;
    status = cfront_type_append(context, &pointer, &adjusted);
  } else if (node.kind == CTOOL_C_TYPE_FUNCTION) {
    ctool_c_type_node_t pointer;
    cfront_node_init(&pointer, CTOOL_C_TYPE_POINTER, token);
    pointer.referenced_type = declared_type;
    status = cfront_type_append(context, &pointer, &adjusted);
  }
  if (status == CTOOL_OK) {
    *object_type_out = adjusted;
    status = cfront_unqualified_parameter_type(context, adjusted,
                                               function_type_out);
  }
  return status == CTOOL_OK ? CTOOL_OK : cfront_storage_failure(context, status);
}

static ctool_status_t cfront_parse_function_suffix_body(
    cfront_context_t *context, ctool_u32 child,
    const ctool_c_pp_token_t *open_token, ctool_u32 prototype_mark,
    ctool_u32 *root_out) {
  cfront_declarator_t function;
  ctool_u32 head = CFRONT_NONE;
  ctool_u32 count = 0u;
  ctool_status_t status;
  cfront_zero(&function, (ctool_u32)sizeof(function));
  function.kind = CFRONT_D_FUNCTION;
  function.child = child;
  function.location = open_token->location;
  function.physical_location = open_token->physical_location;
  function.parameter_head = CFRONT_NONE;
  if (cfront_peek_is(context, ")") == CTOOL_TRUE) {
    (void)cfront_advance(context);
    function.has_prototype = CTOOL_FALSE;
    return cfront_append_declarator(context, &function, root_out);
  }
  if (cfront_peek_is(context, "void") == CTOOL_TRUE &&
      cfront_token_is(cfront_token(context, context->position + 1u), ")") ==
          CTOOL_TRUE) {
    context->position += 2u;
    function.has_prototype = CTOOL_TRUE;
    return cfront_append_declarator(context, &function, root_out);
  }
  function.has_prototype = CTOOL_TRUE;
  for (;;) {
    cfront_specifiers_t specifiers;
    cfront_attributes_t declarator_attributes;
    ctool_u32 parameter_root = CFRONT_NONE;
    ctool_u32 declared_type = CFRONT_NONE;
    ctool_u32 object_type = CFRONT_NONE;
    ctool_u32 adjusted_type = CFRONT_NONE;
    ctool_string_t name = ctool_string("");
    ctool_c_pp_location_t location;
    ctool_c_pp_location_t physical_location;
    cfront_pending_parameter_t pending;
    const ctool_c_pp_token_t *parameter_token = cfront_peek(context);
    cfront_zero(&location, (ctool_u32)sizeof(location));
    cfront_zero(&physical_location, (ctool_u32)sizeof(physical_location));
    cfront_zero(&declarator_attributes,
                (ctool_u32)sizeof(declarator_attributes));
    if (cfront_peek_is(context, "...") == CTOOL_TRUE) {
      if (count == 0u) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_DECLARATOR,
            cfront_peek(context),
            "variadic function requires a named parameter before ellipsis");
      }
      (void)cfront_advance(context);
      function.variadic = CTOOL_TRUE;
      status = cfront_expected(context, ")");
      if (status != CTOOL_OK) {
        return status;
      }
      break;
    }
    status = cfront_parse_specifiers(context, &specifiers);
    if (status != CTOOL_OK) {
      return status;
    }
    status = cfront_validate_function_specifier_context(
        context, &specifiers, CTOOL_FALSE,
        "function specifier cannot apply to a parameter declaration");
    if (status != CTOOL_OK) {
      return status;
    }
    if (cfront_attributes_any(&specifiers.attributes) == CTOOL_TRUE) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_ATTRIBUTE,
          cfront_first_attribute_token(&specifiers.attributes),
          "prototype parameter attributes are not represented");
    }
    if (specifiers.storage != CFRONT_STORAGE_NONE &&
        specifiers.storage != CFRONT_STORAGE_REGISTER) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_DECLARATOR,
          parameter_token, "prototype parameter has an invalid storage class");
    }
    status = cfront_parse_declarator(context, CTOOL_TRUE, &parameter_root);
    if (status != CTOOL_OK) {
      return status;
    }
    status = cfront_parse_attributes(context, &declarator_attributes);
    if (status != CTOOL_OK) {
      return status;
    }
    if (cfront_attributes_any(&declarator_attributes) == CTOOL_TRUE) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_ATTRIBUTE,
          cfront_first_attribute_token(&declarator_attributes),
          "prototype parameter attributes are not represented");
    }
    status = cfront_build_declarator(
        context, parameter_root, specifiers.type, &declared_type, &name,
        &location, &physical_location);
    if (status != CTOOL_OK) {
      return status;
    }
    {
      ctool_u32 declared_base;
      ctool_u32 declared_qualifiers;
      ctool_c_type_node_t declared_node;
      status = cfront_underlying_type(
          context, declared_type, &declared_base, &declared_qualifiers,
          &declared_node);
      (void)declared_base;
      if (status != CTOOL_OK) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
            parameter_token, "prototype parameter type is unavailable");
      }
      if (declared_node.kind == CTOOL_C_TYPE_VOID) {
        if (name.size == 0u && count == 0u &&
            declared_qualifiers == 0u &&
            cfront_peek_is(context, ")") == CTOOL_TRUE) {
          (void)cfront_advance(context);
          break;
        }
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_TYPE_NAME,
            parameter_token,
            "void must be the sole unnamed prototype parameter");
      }
    }
    status = cfront_adjust_parameter_type(
        context, declared_type, parameter_token, &object_type,
        &adjusted_type);
    if (status != CTOOL_OK) {
      return status;
    }
    if (name.size != 0u) {
      ctool_c_binding_t scoped_binding;
      ctool_u32 name_index = context->prototype_names.count;
      if (cfront_find_binding_from(
              context, context->prototype_binding_mark, name,
              &scoped_binding) == CTOOL_TRUE) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_REDEFINITION,
            parameter_token,
            "prototype parameter conflicts with an ordinary identifier");
      }
      while (name_index > prototype_mark) {
        ctool_string_t prior_name;
        name_index--;
        status = cfront_vector_get(&context->prototype_names, name_index,
                                   &prior_name);
        if (status != CTOOL_OK) {
          return cfront_emit_failure(
              context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
              parameter_token, "prototype name scope is unavailable");
        }
        if (cfront_string_equal(prior_name, name) == CTOOL_TRUE) {
          return cfront_emit_failure(
              context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_REDEFINITION,
              parameter_token,
              "prototype parameter name is already declared");
        }
      }
      status = cfront_vector_append(&context->prototype_names, &name,
                                    (ctool_u32 *)0);
      if (status != CTOOL_OK) {
        return cfront_storage_failure(context, status);
      }
    }
    cfront_zero(&pending, (ctool_u32)sizeof(pending));
    pending.parameter.name = name;
    pending.parameter.storage = cfront_public_storage(specifiers.storage);
    pending.parameter.type = object_type;
    pending.parameter.location = location;
    pending.parameter.physical_location = physical_location;
    pending.type = adjusted_type;
    pending.previous = head;
    status = cfront_vector_append(&context->pending_parameters, &pending,
                                  &head);
    if (status != CTOOL_OK) {
      return cfront_storage_failure(context, status);
    }
    count++;
    if (cfront_peek_is(context, ",") == CTOOL_TRUE) {
      (void)cfront_advance(context);
      continue;
    }
    status = cfront_expected(context, ")");
    if (status != CTOOL_OK) {
      return status;
    }
    break;
  }
  function.parameter_head = head;
  function.parameter_count = count;
  return cfront_append_declarator(context, &function, root_out);
}

static ctool_status_t cfront_parse_function_suffix(
    cfront_context_t *context, ctool_u32 child,
    const ctool_c_pp_token_t *open_token, ctool_u32 *root_out) {
  ctool_u32 tag_mark = cfront_vector_mark(&context->tags);
  ctool_u32 binding_mark = cfront_vector_mark(&context->bindings);
  ctool_u32 prototype_mark = cfront_vector_mark(&context->prototype_names);
  ctool_u32 previous_depth = context->prototype_scope_depth;
  ctool_u32 previous_tag_mark = context->prototype_tag_mark;
  ctool_u32 previous_binding_mark = context->prototype_binding_mark;
  ctool_u32 previous_name_mark = context->prototype_name_mark;
  ctool_u32 binding_scope_stack_mark =
      cfront_vector_mark(&context->prototype_binding_marks);
  ctool_u32 name_scope_stack_mark =
      cfront_vector_mark(&context->prototype_name_marks);
  ctool_status_t status;
  ctool_status_t rewind_status;
  status = cfront_vector_append(&context->prototype_binding_marks,
                                &binding_mark, (ctool_u32 *)0);
  if (status == CTOOL_OK) {
    status = cfront_vector_append(&context->prototype_name_marks,
                                  &prototype_mark, (ctool_u32 *)0);
  }
  if (status != CTOOL_OK) {
    (void)cfront_vector_rewind(&context->prototype_binding_marks,
                               binding_scope_stack_mark);
    (void)cfront_vector_rewind(&context->prototype_name_marks,
                               name_scope_stack_mark);
    return cfront_storage_failure(context, status);
  }
  context->prototype_scope_depth++;
  context->prototype_tag_mark = tag_mark;
  context->prototype_binding_mark = binding_mark;
  context->prototype_name_mark = prototype_mark;
  status = cfront_parse_function_suffix_body(
      context, child, open_token, prototype_mark, root_out);
  rewind_status = cfront_vector_rewind(&context->prototype_names,
                                       prototype_mark);
  if (rewind_status == CTOOL_OK) {
    rewind_status = cfront_vector_rewind(&context->tags, tag_mark);
  }
  if (rewind_status == CTOOL_OK) {
    rewind_status = cfront_vector_rewind(&context->bindings, binding_mark);
  }
  if (rewind_status == CTOOL_OK) {
    rewind_status = cfront_vector_rewind(
        &context->prototype_binding_marks, binding_scope_stack_mark);
  }
  if (rewind_status == CTOOL_OK) {
    rewind_status = cfront_vector_rewind(
        &context->prototype_name_marks, name_scope_stack_mark);
  }
  context->prototype_scope_depth = previous_depth;
  context->prototype_tag_mark = previous_tag_mark;
  context->prototype_binding_mark = previous_binding_mark;
  context->prototype_name_mark = previous_name_mark;
  if (status == CTOOL_OK && rewind_status != CTOOL_OK) {
    status = cfront_emit_failure(
        context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
        open_token, "prototype scope rewind failed");
  }
  return status;
}

static ctool_status_t cfront_parse_declarator(cfront_context_t *context,
                                              ctool_bool allow_abstract,
                                              ctool_u32 *root_out) {
  ctool_status_t status = cfront_enter_syntax(context, cfront_peek(context));
  if (status != CTOOL_OK) {
    return status;
  }
  status = cfront_parse_declarator_body(context, allow_abstract, root_out);
  cfront_leave_syntax(context);
  return status;
}

static ctool_status_t cfront_parse_declarator_body(
    cfront_context_t *context, ctool_bool allow_abstract,
    ctool_u32 *root_out) {
  ctool_u32 pointer_mark = cfront_vector_mark(&context->pointer_specs);
  ctool_u32 root = CFRONT_NONE;
  ctool_status_t status;
  while (cfront_peek_is(context, "*") == CTOOL_TRUE) {
    const ctool_c_pp_token_t *star = cfront_advance(context);
    cfront_pointer_spec_t pointer;
    cfront_zero(&pointer, (ctool_u32)sizeof(pointer));
    pointer.location = star->location;
    pointer.physical_location = star->physical_location;
    while (cfront_peek_is(context, "const") == CTOOL_TRUE ||
           cfront_peek_is(context, "volatile") == CTOOL_TRUE ||
           cfront_peek_is(context, "restrict") == CTOOL_TRUE ||
           cfront_peek_is(context, "_Atomic") == CTOOL_TRUE) {
      if (cfront_peek_is(context, "const") == CTOOL_TRUE) {
        pointer.qualifiers |= CTOOL_C_QUAL_CONST;
      } else if (cfront_peek_is(context, "volatile") == CTOOL_TRUE) {
        pointer.qualifiers |= CTOOL_C_QUAL_VOLATILE;
      } else if (cfront_peek_is(context, "restrict") == CTOOL_TRUE) {
        pointer.qualifiers |= CTOOL_C_QUAL_RESTRICT;
      } else {
        pointer.qualifiers |= CTOOL_C_QUAL_ATOMIC;
      }
      (void)cfront_advance(context);
    }
    status = cfront_vector_append(&context->pointer_specs, &pointer,
                                  (ctool_u32 *)0);
    if (status != CTOOL_OK) {
      return cfront_storage_failure(context, status);
    }
  }
  if (cfront_peek(context) != (const ctool_c_pp_token_t *)0 &&
      cfront_peek(context)->kind == CTOOL_C_PP_TOKEN_IDENTIFIER) {
    const ctool_c_pp_token_t *identifier = cfront_advance(context);
    cfront_declarator_t name;
    if (cfront_reserved_identifier(context, identifier) == CTOOL_TRUE) {
      status = cfront_emit_failure(
          context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_DECLARATOR,
          identifier, "declarator identifier is a reserved word");
      (void)cfront_vector_rewind(&context->pointer_specs, pointer_mark);
      return status;
    }
    cfront_zero(&name, (ctool_u32)sizeof(name));
    name.kind = CFRONT_D_NAME;
    name.child = CFRONT_NONE;
    name.name = identifier->spelling;
    name.location = identifier->location;
    name.physical_location = identifier->physical_location;
    status = cfront_append_declarator(context, &name, &root);
  } else if (cfront_peek_is(context, "(") == CTOOL_TRUE) {
    const ctool_c_pp_token_t *after_open =
        cfront_token(context, context->position + 1u);
    if (allow_abstract == CTOOL_TRUE &&
        (cfront_token_is(after_open, ")") == CTOOL_TRUE ||
         cfront_token_is(after_open, "...") == CTOOL_TRUE ||
         cfront_starts_declaration_specifier(context, after_open) ==
             CTOOL_TRUE)) {
      cfront_declarator_t name;
      cfront_zero(&name, (ctool_u32)sizeof(name));
      name.kind = CFRONT_D_NAME;
      name.child = CFRONT_NONE;
      name.location = cfront_peek(context)->location;
      name.physical_location = cfront_peek(context)->physical_location;
      status = cfront_append_declarator(context, &name, &root);
    } else {
      (void)cfront_advance(context);
      status = cfront_parse_declarator(context, CTOOL_TRUE, &root);
      if (status == CTOOL_OK) {
        status = cfront_expected(context, ")");
      }
    }
  } else if (allow_abstract == CTOOL_TRUE) {
    const ctool_c_pp_token_t *token = cfront_peek(context);
    cfront_declarator_t name;
    cfront_zero(&name, (ctool_u32)sizeof(name));
    name.kind = CFRONT_D_NAME;
    name.child = CFRONT_NONE;
    if (token != (const ctool_c_pp_token_t *)0) {
      name.location = token->location;
      name.physical_location = token->physical_location;
    }
    status = cfront_append_declarator(context, &name, &root);
  } else {
    status = cfront_emit_failure(
        context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_DECLARATOR,
        cfront_peek(context), "declarator requires an identifier");
  }
  if (status != CTOOL_OK) {
    (void)cfront_vector_rewind(&context->pointer_specs, pointer_mark);
    return status;
  }
  while (cfront_peek_is(context, "[") == CTOOL_TRUE ||
         cfront_peek_is(context, "(") == CTOOL_TRUE) {
    const ctool_c_pp_token_t *open = cfront_advance(context);
    if (cfront_token_is(open, "[") == CTOOL_TRUE) {
      cfront_declarator_t array;
      cfront_integer_t count = {0ull, CFRONT_INTEGER_SIGNED_32};
      cfront_zero(&array, (ctool_u32)sizeof(array));
      array.kind = CFRONT_D_ARRAY;
      array.child = root;
      array.location = open->location;
      array.physical_location = open->physical_location;
      if (cfront_peek_is(context, "]") == CTOOL_TRUE) {
        array.array_bound_kind = CTOOL_C_ARRAY_UNSPECIFIED;
      } else {
        array.array_bound_kind = CTOOL_C_ARRAY_FIXED;
        status = cfront_parse_constant_logical_or(context, &count);
        if (status != CTOOL_OK) {
          break;
        }
        if (cfront_integer_negative(&count) == CTOOL_TRUE ||
            count.bits > CFRONT_U32_MAX) {
          status = cfront_emit_failure(
              context, CTOOL_ERR_OVERFLOW,
              CTOOL_C_PARSE_DIAG_OVERFLOW, open,
              "array bound is outside the supported 32-bit object range");
          break;
        }
        array.element_count = (ctool_u32)count.bits;
      }
      status = cfront_expected(context, "]");
      if (status == CTOOL_OK) {
        status = cfront_append_declarator(context, &array, &root);
      }
    } else {
      status = cfront_parse_function_suffix(context, root, open, &root);
    }
    if (status != CTOOL_OK) {
      break;
    }
  }
  if (status == CTOOL_OK) {
    ctool_u32 index = context->pointer_specs.count;
    while (index > pointer_mark) {
      cfront_pointer_spec_t pointer;
      cfront_declarator_t declarator;
      index--;
      status = cfront_vector_get(&context->pointer_specs, index, &pointer);
      if (status != CTOOL_OK) {
        status = cfront_emit_failure(
            context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
            cfront_peek(context), "pointer declarator chain is invalid");
        break;
      }
      cfront_zero(&declarator, (ctool_u32)sizeof(declarator));
      declarator.kind = CFRONT_D_POINTER;
      declarator.child = root;
      declarator.location = pointer.location;
      declarator.physical_location = pointer.physical_location;
      declarator.qualifiers = pointer.qualifiers;
      status = cfront_append_declarator(context, &declarator, &root);
      if (status != CTOOL_OK) {
        break;
      }
    }
  }
  {
    ctool_status_t rewind_status =
        cfront_vector_rewind(&context->pointer_specs, pointer_mark);
    if (status == CTOOL_OK && rewind_status != CTOOL_OK) {
      status = cfront_emit_failure(
          context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
          cfront_peek(context), "pointer declarator scratch rewind failed");
    }
  }
  if (status == CTOOL_OK) {
    *root_out = root;
  }
  return status;
}

static void cfront_node_from_declarator(
    ctool_c_type_node_t *node, ctool_c_type_kind_t kind,
    const cfront_declarator_t *declarator) {
  cfront_zero(node, (ctool_u32)sizeof(*node));
  node->kind = kind;
  node->location = declarator->location;
  node->physical_location = declarator->physical_location;
  node->referenced_type = CTOOL_C_TYPE_NONE;
  node->array_bound_kind = CTOOL_C_ARRAY_FIXED;
  node->record_kind = CTOOL_C_RECORD_STRUCT;
}

static ctool_status_t cfront_flatten_parameters(
    cfront_context_t *context, const cfront_declarator_t *function,
    ctool_u32 *first_out) {
  ctool_u32 temporary_mark = cfront_vector_mark(&context->temporary_indices);
  ctool_u32 head = function->parameter_head;
  ctool_status_t status = CTOOL_OK;
  *first_out = context->parameter_types.count;
  while (head != CFRONT_NONE) {
    cfront_pending_parameter_t pending;
    ctool_u32 index = head;
    status = cfront_vector_get(&context->pending_parameters, index, &pending);
    if (status == CTOOL_OK) {
      status = cfront_vector_append(&context->temporary_indices, &index,
                                    (ctool_u32 *)0);
    }
    if (status != CTOOL_OK) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
          cfront_peek(context), "function parameter chain is invalid");
    }
    head = pending.previous;
  }
  while (context->temporary_indices.count > temporary_mark) {
    ctool_u32 index;
    cfront_pending_parameter_t pending;
    status = cfront_vector_get(&context->temporary_indices,
                               context->temporary_indices.count - 1u, &index);
    if (status == CTOOL_OK) {
      status = cfront_vector_rewind(&context->temporary_indices,
                                    context->temporary_indices.count - 1u);
    }
    if (status == CTOOL_OK) {
      status = cfront_vector_get(&context->pending_parameters, index, &pending);
    }
    if (status == CTOOL_OK) {
      status = cfront_vector_append(&context->parameter_types, &pending.type,
                                    (ctool_u32 *)0);
    }
    if (status == CTOOL_OK) {
      status = cfront_vector_append(&context->parameters, &pending.parameter,
                                    (ctool_u32 *)0);
    }
    if (status != CTOOL_OK) {
      return cfront_storage_failure(context, status);
    }
  }
  return CTOOL_OK;
}

static ctool_status_t cfront_build_declarator(
    cfront_context_t *context, ctool_u32 root, ctool_u32 base_type,
    ctool_u32 *type_out, ctool_string_t *name_out,
    ctool_c_pp_location_t *location_out,
    ctool_c_pp_location_t *physical_location_out) {
  ctool_u32 current = base_type;
  ctool_u32 node_index = root;
  ctool_status_t status;
  for (;;) {
    cfront_declarator_t declarator;
    ctool_c_type_node_t node;
    status = cfront_vector_get(&context->declarators, node_index, &declarator);
    if (status != CTOOL_OK) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
          cfront_peek(context), "declarator constructor is unavailable");
    }
    if (declarator.kind == CFRONT_D_NAME) {
      *type_out = current;
      *name_out = declarator.name;
      *location_out = declarator.location;
      *physical_location_out = declarator.physical_location;
      return CTOOL_OK;
    }
    if (declarator.kind == CFRONT_D_POINTER) {
      if ((declarator.qualifiers & CTOOL_C_QUAL_RESTRICT) != 0u) {
        ctool_u32 referenced;
        ctool_u32 referenced_qualifiers;
        ctool_c_type_node_t referenced_node;
        status = cfront_underlying_type(
            context, current, &referenced, &referenced_qualifiers,
            &referenced_node);
        (void)referenced;
        (void)referenced_qualifiers;
        if (status != CTOOL_OK) {
          return cfront_emit_failure(
              context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
              cfront_peek(context), "pointer referent is unavailable");
        }
        if (referenced_node.kind == CTOOL_C_TYPE_FUNCTION) {
          return cfront_emit_failure(
              context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_DECLARATOR,
              cfront_peek(context),
              "restrict qualifier cannot apply to a function pointer");
        }
      }
      cfront_node_from_declarator(&node, CTOOL_C_TYPE_POINTER, &declarator);
      node.referenced_type = current;
      node.qualifiers = declarator.qualifiers;
    } else if (declarator.kind == CFRONT_D_ARRAY) {
      ctool_bool element_complete = CTOOL_FALSE;
      status = cfront_type_is_complete_object_now(
          context, current, &element_complete);
      if (status != CTOOL_OK) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
            cfront_peek(context),
            "array element completeness is unavailable");
      }
      if (element_complete == CTOOL_FALSE) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_TYPE_NAME,
            cfront_peek(context),
            "array element requires a complete object type");
      }
      if (context->request->gnu_extensions == CTOOL_FALSE) {
        ctool_bool contains_flexible_array = CTOOL_FALSE;
        status = cfront_type_contains_flexible_array(
            context, current, &contains_flexible_array);
        if (status != CTOOL_OK) {
          return cfront_storage_failure(context, status);
        }
        if (contains_flexible_array == CTOOL_TRUE) {
          return cfront_emit_failure(
              context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_TYPE_NAME,
              cfront_peek(context),
              "array element cannot contain a flexible-array member");
        }
      }
      cfront_node_from_declarator(&node, CTOOL_C_TYPE_ARRAY, &declarator);
      node.referenced_type = current;
      node.array_bound_kind = declarator.array_bound_kind;
      node.element_count = declarator.element_count;
    } else if (declarator.kind == CFRONT_D_FUNCTION) {
      ctool_c_type_node_t result_node;
      ctool_u32 result_base;
      ctool_u32 result_qualifiers;
      ctool_u32 first_parameter;
      status = cfront_underlying_type(context, current, &result_base,
                                      &result_qualifiers, &result_node);
      (void)result_base;
      (void)result_qualifiers;
      if (status != CTOOL_OK) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
            cfront_peek(context), "function result type is unavailable");
      }
      if (result_node.kind == CTOOL_C_TYPE_ARRAY ||
          result_node.kind == CTOOL_C_TYPE_FUNCTION) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_DECLARATOR,
            cfront_peek(context),
            "function cannot return an array or function type");
      }
      status = cfront_flatten_parameters(context, &declarator,
                                         &first_parameter);
      if (status != CTOOL_OK) {
        return status;
      }
      cfront_node_from_declarator(&node, CTOOL_C_TYPE_FUNCTION, &declarator);
      node.referenced_type = current;
      node.first_parameter = first_parameter;
      node.parameter_count = declarator.parameter_count;
      node.has_prototype = declarator.has_prototype;
      node.variadic = declarator.variadic;
    } else {
      return cfront_emit_failure(
          context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
          cfront_peek(context), "declarator constructor kind is invalid");
    }
    status = cfront_type_append(context, &node, &current);
    if (status != CTOOL_OK) {
      return cfront_storage_failure(context, status);
    }
    if (declarator.child == CFRONT_NONE) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
          cfront_peek(context), "declarator constructor has no child");
    }
    node_index = declarator.child;
  }
}

static ctool_status_t cfront_parse_type_name(cfront_context_t *context,
                                             ctool_u32 *type_out) {
  const ctool_c_pp_token_t *token = cfront_peek(context);
  cfront_specifiers_t specifiers;
  ctool_u32 root = CFRONT_NONE;
  ctool_u32 type = CFRONT_NONE;
  ctool_string_t name = ctool_string("");
  ctool_c_pp_location_t location;
  ctool_c_pp_location_t physical_location;
  ctool_status_t status = cfront_parse_specifiers(context, &specifiers);
  if (status != CTOOL_OK) {
    return status;
  }
  status = cfront_validate_function_specifier_context(
      context, &specifiers, CTOOL_FALSE,
      "function specifier cannot appear in a type name");
  if (status != CTOOL_OK) {
    return status;
  }
  if (specifiers.storage != CFRONT_STORAGE_NONE) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_TYPE_NAME, token,
        "type name cannot contain a storage-class specifier");
  }
  if (cfront_attributes_any(&specifiers.attributes) == CTOOL_TRUE) {
    return cfront_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_ATTRIBUTE,
        cfront_first_attribute_token(&specifiers.attributes),
        "attributes in a type name are outside this declaration slice");
  }
  status = cfront_parse_declarator(context, CTOOL_TRUE, &root);
  if (status == CTOOL_OK) {
    status = cfront_build_declarator(
        context, root, specifiers.type, &type, &name, &location,
        &physical_location);
  }
  if (status != CTOOL_OK) {
    return status;
  }
  if (name.size != 0u) {
    return cfront_emit_failure(context, CTOOL_ERR_INPUT,
                               CTOOL_C_PARSE_DIAG_TYPE_NAME, token,
                               "type name cannot declare an identifier");
  }
  *type_out = type;
  return CTOOL_OK;
}

static ctool_bool cfront_pending_member_name_exists(
    const cfront_context_t *context, ctool_u32 head, ctool_string_t name) {
  while (head != CFRONT_NONE) {
    cfront_pending_member_t pending;
    if (cfront_vector_get(&context->pending_members, head, &pending) !=
        CTOOL_OK) {
      return CTOOL_FALSE;
    }
    if (name.size != 0u &&
        cfront_string_equal(pending.member.name, name) == CTOOL_TRUE) {
      return CTOOL_TRUE;
    }
    head = pending.previous;
  }
  return CTOOL_FALSE;
}

static ctool_status_t cfront_append_visible_name(
    cfront_context_t *context, ctool_u32 mark,
    const ctool_c_record_member_t *member,
    const ctool_c_pp_token_t *diagnostic_token) {
  ctool_u32 index = mark;
  cfront_visible_name_t visible;
  ctool_status_t status;
  while (index < context->visible_names.count) {
    cfront_visible_name_t prior;
    status = cfront_vector_get(&context->visible_names, index, &prior);
    if (status != CTOOL_OK) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
          diagnostic_token, "record visible-name set is unavailable");
    }
    if (cfront_string_equal(prior.name, member->name) == CTOOL_TRUE) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_REDEFINITION,
          diagnostic_token,
          "record member name conflicts through anonymous promotion");
    }
    index++;
  }
  visible.name = member->name;
  visible.location = member->location;
  visible.physical_location = member->physical_location;
  status = cfront_vector_append(&context->visible_names, &visible,
                                (ctool_u32 *)0);
  return status == CTOOL_OK ? CTOOL_OK
                            : cfront_storage_failure(context, status);
}

static ctool_status_t cfront_collect_visible_names(
    cfront_context_t *context, ctool_u32 first_member,
    ctool_u32 member_count, ctool_u32 visible_mark,
    const ctool_c_pp_token_t *diagnostic_token) {
  ctool_u32 stack_mark = cfront_vector_mark(&context->temporary_indices);
  ctool_u32 index = member_count;
  ctool_status_t status = CTOOL_OK;
  while (index != 0u) {
    ctool_u32 member_index = first_member + (index - 1u);
    status = cfront_vector_append(&context->temporary_indices, &member_index,
                                  (ctool_u32 *)0);
    if (status != CTOOL_OK) {
      return cfront_storage_failure(context, status);
    }
    index--;
  }
  while (context->temporary_indices.count > stack_mark) {
    ctool_u32 member_index;
    ctool_c_record_member_t member;
    status = cfront_vector_get(&context->temporary_indices,
                               context->temporary_indices.count - 1u,
                               &member_index);
    if (status == CTOOL_OK) {
      status = cfront_vector_rewind(&context->temporary_indices,
                                    context->temporary_indices.count - 1u);
    }
    if (status == CTOOL_OK) {
      status = cfront_vector_get(&context->members, member_index, &member);
    }
    if (status != CTOOL_OK) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
          diagnostic_token, "record visible-member traversal is invalid");
    }
    if (member.name.size != 0u) {
      status = cfront_append_visible_name(context, visible_mark, &member,
                                          diagnostic_token);
      if (status != CTOOL_OK) {
        return status;
      }
    } else if (member.anonymous == CTOOL_TRUE) {
      ctool_u32 base;
      ctool_u32 qualifiers;
      ctool_c_type_node_t record;
      status = cfront_underlying_type(context, member.type, &base,
                                      &qualifiers, &record);
      (void)base;
      (void)qualifiers;
      if (status != CTOOL_OK || record.kind != CTOOL_C_TYPE_RECORD ||
          record.record_complete == CTOOL_FALSE) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
            diagnostic_token,
            "anonymous member does not reference a complete record");
      }
      index = record.member_count;
      while (index != 0u) {
        ctool_u32 nested_index = record.first_member + (index - 1u);
        status = cfront_vector_append(&context->temporary_indices,
                                      &nested_index, (ctool_u32 *)0);
        if (status != CTOOL_OK) {
          return cfront_storage_failure(context, status);
        }
        index--;
      }
    }
  }
  return CTOOL_OK;
}

static ctool_status_t cfront_validate_completed_record(
    cfront_context_t *context, ctool_c_record_kind_t record_kind,
    ctool_u32 first_member, ctool_u32 member_count,
    const ctool_c_pp_token_t *diagnostic_token) {
  ctool_u32 visible_mark = cfront_vector_mark(&context->visible_names);
  ctool_u32 index;
  ctool_status_t status;
  if (member_count == 0u) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_DECLARATOR,
        diagnostic_token, "record definition requires at least one member");
  }
  status = cfront_collect_visible_names(context, first_member, member_count,
                                        visible_mark, diagnostic_token);
  if (status == CTOOL_OK) {
    for (index = 0u; index < member_count; index++) {
      ctool_c_record_member_t member;
      ctool_u32 base;
      ctool_u32 qualifiers;
      ctool_c_type_node_t member_node;
      status = cfront_vector_get(&context->members, first_member + index,
                                 &member);
      if (status == CTOOL_OK) {
        status = cfront_underlying_type(context, member.type, &base,
                                        &qualifiers, &member_node);
      }
      (void)base;
      (void)qualifiers;
      if (status != CTOOL_OK) {
        status = cfront_emit_failure(
            context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
            diagnostic_token, "record member type is unavailable");
        break;
      }
      if (member_node.kind == CTOOL_C_TYPE_ARRAY &&
          member_node.array_bound_kind == CTOOL_C_ARRAY_UNSPECIFIED) {
        if (record_kind != CTOOL_C_RECORD_STRUCT ||
            index + 1u != member_count) {
          status = cfront_emit_failure(
              context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_TYPE_NAME,
              diagnostic_token,
              "flexible array must be the final structure member");
          break;
        }
        if (context->visible_names.count - visible_mark <= 1u) {
          status = cfront_emit_failure(
              context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_DECLARATOR,
              diagnostic_token,
              "flexible-array structure needs more than one named member");
          break;
        }
      } else if (record_kind == CTOOL_C_RECORD_STRUCT &&
                 context->request->gnu_extensions == CTOOL_FALSE) {
        ctool_bool contains_flexible_array = CTOOL_FALSE;
        status = cfront_type_contains_flexible_array(
            context, member.type, &contains_flexible_array);
        if (status != CTOOL_OK) {
          status = cfront_storage_failure(context, status);
          break;
        }
        if (contains_flexible_array == CTOOL_TRUE) {
          status = cfront_emit_failure(
              context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_TYPE_NAME,
              diagnostic_token,
              "structure member cannot embed a flexible-array record");
          break;
        }
      }
    }
  }
  {
    ctool_status_t rewind_status =
        cfront_vector_rewind(&context->visible_names, visible_mark);
    if (status == CTOOL_OK && rewind_status != CTOOL_OK) {
      status = cfront_emit_failure(
          context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
          diagnostic_token, "record visible-name rewind failed");
    }
  }
  return status;
}

static ctool_status_t cfront_resolve_member_path(
    cfront_context_t *context, ctool_u32 record_type, ctool_string_t name,
    const ctool_c_pp_token_t *member_token, cfront_vector_t *path) {
  cfront_vector_t work;
  cfront_vector_t visited;
  cfront_vector_t chain;
  ctool_c_type_node_t record;
  ctool_u32 base;
  ctool_u32 qualifiers;
  ctool_u32 index;
  ctool_bool found = CTOOL_FALSE;
  ctool_status_t status;
  cfront_zero(&work, (ctool_u32)sizeof(work));
  cfront_zero(&visited, (ctool_u32)sizeof(visited));
  cfront_zero(&chain, (ctool_u32)sizeof(chain));
  status = cfront_underlying_type(context, record_type, &base, &qualifiers,
                                  &record);
  (void)base;
  (void)qualifiers;
  if (status != CTOOL_OK) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
        member_token, "member access type is unavailable");
  }
  if (record.kind != CTOOL_C_TYPE_RECORD ||
      record.record_complete == CTOOL_FALSE) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION,
        member_token, "member access requires a complete record or union");
  }
  status = cfront_vector_open(context, &work, (ctool_u32)sizeof(ctool_u32));
  if (status == CTOOL_OK) {
    status = cfront_vector_open(
        context, &visited, (ctool_u32)sizeof(cfront_member_search_item_t));
  }
  if (status == CTOOL_OK) {
    status = cfront_vector_open(context, &chain,
                                (ctool_u32)sizeof(ctool_u32));
  }
  index = record.member_count;
  while (status == CTOOL_OK && index != 0u) {
    cfront_member_search_item_t item;
    ctool_u32 item_index;
    item.member = record.first_member + (index - 1u);
    item.parent = CFRONT_NONE;
    status = cfront_vector_append(&visited, &item, &item_index);
    if (status == CTOOL_OK) {
      status = cfront_vector_append(&work, &item_index, (ctool_u32 *)0);
    }
    index--;
  }
  while (status == CTOOL_OK && work.count != 0u && found == CTOOL_FALSE) {
    cfront_member_search_item_t item;
    ctool_c_record_member_t member;
    ctool_u32 item_index;
    status = cfront_vector_get(&work, work.count - 1u, &item_index);
    if (status == CTOOL_OK) {
      status = cfront_vector_rewind(&work, work.count - 1u);
    }
    if (status == CTOOL_OK) {
      status = cfront_vector_get(&visited, item_index, &item);
    }
    if (status == CTOOL_OK) {
      status = cfront_vector_get(&context->members, item.member, &member);
    }
    if (status != CTOOL_OK) {
      break;
    }
    if (member.name.size != 0u &&
        cfront_string_equal(member.name, name) == CTOOL_TRUE) {
      ctool_u32 cursor = item_index;
      while (status == CTOOL_OK && cursor != CFRONT_NONE) {
        status = cfront_vector_get(&visited, cursor, &item);
        if (status == CTOOL_OK) {
          status = cfront_vector_append(&chain, &item.member,
                                        (ctool_u32 *)0);
          cursor = item.parent;
        }
      }
      index = chain.count;
      while (status == CTOOL_OK && index != 0u) {
        ctool_u32 member_index;
        status = cfront_vector_get(&chain, index - 1u, &member_index);
        if (status == CTOOL_OK) {
          status = cfront_vector_append(path, &member_index,
                                        (ctool_u32 *)0);
        }
        index--;
      }
      if (status == CTOOL_OK) {
        found = CTOOL_TRUE;
      }
    } else if (member.name.size == 0u &&
               member.anonymous == CTOOL_TRUE) {
      ctool_c_type_node_t nested;
      status = cfront_underlying_type(context, member.type, &base,
                                      &qualifiers, &nested);
      (void)base;
      (void)qualifiers;
      if (status != CTOOL_OK || nested.kind != CTOOL_C_TYPE_RECORD ||
          nested.record_complete == CTOOL_FALSE) {
        status = CTOOL_ERR_INTERNAL;
        break;
      }
      index = nested.member_count;
      while (status == CTOOL_OK && index != 0u) {
        cfront_member_search_item_t child;
        ctool_u32 child_index;
        child.member = nested.first_member + (index - 1u);
        child.parent = item_index;
        status = cfront_vector_append(&visited, &child, &child_index);
        if (status == CTOOL_OK) {
          status = cfront_vector_append(&work, &child_index,
                                        (ctool_u32 *)0);
        }
        index--;
      }
    }
  }
  cfront_vector_close(&chain);
  cfront_vector_close(&visited);
  cfront_vector_close(&work);
  if (status != CTOOL_OK) {
    if (status == CTOOL_ERR_LIMIT || status == CTOOL_ERR_OVERFLOW ||
        status == CTOOL_ERR_NO_MEMORY) {
      return cfront_storage_failure(context, status);
    }
    return cfront_emit_failure(
        context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
        member_token, "record member traversal is invalid");
  }
  if (found == CTOOL_FALSE) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION,
        member_token, "record or union has no member with this name");
  }
  return CTOOL_OK;
}

static ctool_status_t cfront_append_pending_member(
    cfront_context_t *context, ctool_u32 *head, ctool_u32 *count,
    const ctool_c_record_member_t *member,
    const ctool_c_pp_token_t *diagnostic_token) {
  cfront_pending_member_t pending;
  ctool_status_t status;
  if (cfront_pending_member_name_exists(context, *head, member->name) ==
      CTOOL_TRUE) {
    return cfront_emit_failure(context, CTOOL_ERR_INPUT,
                               CTOOL_C_PARSE_DIAG_REDEFINITION,
                               diagnostic_token,
                               "record member name is already declared");
  }
  pending.member = *member;
  pending.previous = *head;
  status = cfront_vector_append(&context->pending_members, &pending, head);
  if (status != CTOOL_OK) {
    return cfront_storage_failure(context, status);
  }
  if (*count == CFRONT_U32_MAX) {
    return cfront_storage_failure(context, CTOOL_ERR_OVERFLOW);
  }
  (*count)++;
  return CTOOL_OK;
}

static ctool_status_t cfront_parse_member_declaration(
    cfront_context_t *context, ctool_u32 *member_head,
    ctool_u32 *member_count) {
  const ctool_c_pp_token_t *declaration_token = cfront_peek(context);
  cfront_specifiers_t specifiers;
  ctool_status_t status;
  if (cfront_peek_is(context, "_Static_assert") == CTOOL_TRUE) {
    return cfront_parse_static_assert(context);
  }
  status = cfront_parse_specifiers(context, &specifiers);
  if (status != CTOOL_OK) {
    return status;
  }
  status = cfront_validate_function_specifier_context(
      context, &specifiers, CTOOL_FALSE,
      "function specifier cannot apply to a record member");
  if (status != CTOOL_OK) {
    return status;
  }
  if (specifiers.storage != CFRONT_STORAGE_NONE) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_DECLARATOR,
        declaration_token, "record member cannot have a storage class");
  }
  if (cfront_peek_is(context, ";") == CTOOL_TRUE) {
    if (cfront_attributes_any(&specifiers.attributes) == CTOOL_TRUE) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_ATTRIBUTE,
          cfront_first_attribute_token(&specifiers.attributes),
          "declaration attributes require a declarator or type placement");
    }
    if (specifiers.anonymous_record_definition == CTOOL_FALSE) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_DECLARATOR,
          declaration_token,
          "member declaration without a declarator is not anonymous");
    }
    {
      ctool_c_record_member_t member;
      cfront_zero(&member, (ctool_u32)sizeof(member));
      member.type = specifiers.type;
      member.location = specifiers.location;
      member.physical_location = specifiers.physical_location;
      member.pack_alignment = specifiers.pack_alignment;
      member.anonymous = CTOOL_TRUE;
      status = cfront_append_pending_member(context, member_head, member_count,
                                            &member, declaration_token);
    }
    (void)cfront_advance(context);
    return status;
  }
  for (;;) {
    ctool_u32 root = CFRONT_NONE;
    ctool_u32 type = CFRONT_NONE;
    ctool_string_t name = ctool_string("");
    ctool_c_pp_location_t location;
    ctool_c_pp_location_t physical_location;
    ctool_c_record_member_t member;
    cfront_attributes_t declarator_attributes;
    const ctool_c_pp_token_t *declarator_token = cfront_peek(context);
    declarator_attributes = specifiers.attributes;
    status = cfront_parse_declarator(context, CTOOL_TRUE, &root);
    if (status != CTOOL_OK) {
      return status;
    }
    status = cfront_parse_attributes(context, &declarator_attributes);
    if (status != CTOOL_OK) {
      return status;
    }
    if (declarator_attributes.noreturn == CTOOL_TRUE) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_ATTRIBUTE,
          declarator_attributes.noreturn_token,
          "noreturn attribute cannot apply to a record member");
    }
    status = cfront_build_declarator(
        context, root, specifiers.type, &type, &name, &location,
        &physical_location);
    if (status != CTOOL_OK) {
      return status;
    }
    {
      ctool_u32 base;
      ctool_u32 qualifiers;
      ctool_c_type_node_t member_node;
      ctool_bool complete = CTOOL_FALSE;
      status = cfront_underlying_type(context, type, &base, &qualifiers,
                                      &member_node);
      (void)base;
      (void)qualifiers;
      if (status != CTOOL_OK) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
            declarator_token, "record member type is unavailable");
      }
      if (member_node.kind == CTOOL_C_TYPE_ARRAY &&
          member_node.array_bound_kind == CTOOL_C_ARRAY_UNSPECIFIED) {
        status = cfront_type_is_complete_object_now(
            context, member_node.referenced_type, &complete);
      } else {
        status = cfront_type_is_complete_object_now(context, type, &complete);
      }
      if (status != CTOOL_OK) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
            declarator_token, "record member completeness is unavailable");
      }
      if (complete == CTOOL_FALSE) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_TYPE_NAME,
            declarator_token,
            "record member requires a complete object element type");
      }
    }
    cfront_zero(&member, (ctool_u32)sizeof(member));
    member.name = name;
    member.type = type;
    member.location = location;
    member.physical_location = physical_location;
    member.pack_alignment = specifiers.pack_alignment;
    member.member_packed = declarator_attributes.packed;
    member.explicit_alignment = declarator_attributes.alignment;
    if (cfront_peek_is(context, ":") == CTOOL_TRUE) {
      cfront_integer_t width = {0ull, CFRONT_INTEGER_SIGNED_32};
      (void)cfront_advance(context);
      status = cfront_parse_constant_logical_or(context, &width);
      if (status != CTOOL_OK) {
        return status;
      }
      if (cfront_integer_negative(&width) == CTOOL_TRUE ||
          width.bits > CFRONT_U32_MAX) {
        return cfront_emit_failure(
            context, CTOOL_ERR_OVERFLOW,
            CTOOL_C_PARSE_DIAG_OVERFLOW, declarator_token,
            "bit-field width is outside the 32-bit semantic range");
      }
      member.is_bit_field = CTOOL_TRUE;
      member.bit_width = (ctool_u32)width.bits;
    } else if (name.size == 0u) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_DECLARATOR,
          declarator_token,
          "ordinary record member requires an identifier");
    }
    status = cfront_append_pending_member(context, member_head, member_count,
                                          &member, declarator_token);
    if (status != CTOOL_OK) {
      return status;
    }
    if (cfront_peek_is(context, ",") == CTOOL_TRUE) {
      (void)cfront_advance(context);
      continue;
    }
    status = cfront_expected(context, ";");
    return status;
  }
}

static void cfront_expression_init(
    ctool_c_expression_t *expression, ctool_c_expression_kind_t kind,
    const ctool_c_pp_location_t *location,
    const ctool_c_pp_location_t *physical_location) {
  cfront_zero(expression, (ctool_u32)sizeof(*expression));
  expression->kind = kind;
  expression->reference = CTOOL_C_AST_NONE;
  expression->first_child = CTOOL_C_AST_NONE;
  expression->computation_type = CTOOL_C_TYPE_NONE;
  if (location != (const ctool_c_pp_location_t *)0 &&
      physical_location != (const ctool_c_pp_location_t *)0) {
    expression->location = *location;
    expression->physical_location = *physical_location;
  }
}

static void cfront_statement_init(
    ctool_c_statement_t *statement, ctool_c_statement_kind_t kind,
    const ctool_c_pp_token_t *token) {
  cfront_zero(statement, (ctool_u32)sizeof(*statement));
  statement->kind = kind;
  statement->first_child = CTOOL_C_AST_NONE;
  statement->expression = CTOOL_C_AST_NONE;
  statement->first_block_binding = CTOOL_C_AST_NONE;
  if (token != (const ctool_c_pp_token_t *)0) {
    statement->location = token->location;
    statement->physical_location = token->physical_location;
  }
}

static ctool_status_t cfront_append_expression(
    cfront_context_t *context, const ctool_c_expression_t *expression,
    ctool_u32 *expression_out) {
  ctool_status_t status = cfront_vector_append(
      &context->expressions, expression, expression_out);
  return status == CTOOL_OK ? CTOOL_OK
                            : cfront_storage_failure(context, status);
}

static ctool_status_t cfront_append_statement(
    cfront_context_t *context, const ctool_c_statement_t *statement,
    ctool_u32 *statement_out) {
  ctool_status_t status = cfront_vector_append(
      &context->statements, statement, statement_out);
  return status == CTOOL_OK ? CTOOL_OK
                            : cfront_storage_failure(context, status);
}

static ctool_status_t cfront_decode_narrow_string_token(
    cfront_context_t *context, const ctool_c_pp_token_t *token,
    ctool_buffer_t *bytes) {
  ctool_u32 index;
  if (token == (const ctool_c_pp_token_t *)0 ||
      token->kind != CTOOL_C_PP_TOKEN_STRING || token->spelling.size < 2u ||
      token->spelling.data[0] != '"' ||
      token->spelling.data[token->spelling.size - 1u] != '"') {
    return cfront_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_EXPRESSION,
        token, "only ordinary narrow string literals are supported in bodies");
  }
  index = 1u;
  while (index + 1u < token->spelling.size) {
    ctool_u32 value = (ctool_u8)token->spelling.data[index++];
    ctool_status_t status;
    if (value == (ctool_u32)'\\') {
      char escaped;
      if (index >= token->spelling.size - 1u) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION, token,
            "string literal escape is incomplete");
      }
      escaped = token->spelling.data[index++];
      switch (escaped) {
      case '\'':
      case '"':
      case '?':
      case '\\':
        value = (ctool_u8)escaped;
        break;
      case 'a':
        value = 7u;
        break;
      case 'b':
        value = 8u;
        break;
      case 'f':
        value = 12u;
        break;
      case 'n':
        value = 10u;
        break;
      case 'r':
        value = 13u;
        break;
      case 't':
        value = 9u;
        break;
      case 'v':
        value = 11u;
        break;
      case 'x': {
        ctool_u32 digit;
        ctool_bool saw_digit = CTOOL_FALSE;
        value = 0u;
        while (index + 1u < token->spelling.size &&
               cfront_digit_value(token->spelling.data[index], &digit) ==
                   CTOOL_TRUE) {
          saw_digit = CTOOL_TRUE;
          if (value > (255u - digit) / 16u) {
            return cfront_emit_failure(
                context, CTOOL_ERR_OVERFLOW,
                CTOOL_C_PARSE_DIAG_EXPRESSION, token,
                "narrow string hexadecimal escape exceeds one target byte");
          }
          value = value * 16u + digit;
          index++;
        }
        if (saw_digit == CTOOL_FALSE) {
          return cfront_emit_failure(
              context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION,
              token, "string hexadecimal escape requires a digit");
        }
        break;
      }
      default:
        if (escaped >= '0' && escaped <= '7') {
          ctool_u32 count = 1u;
          value = (ctool_u32)(escaped - '0');
          while (count < 3u && index + 1u < token->spelling.size &&
                 token->spelling.data[index] >= '0' &&
                 token->spelling.data[index] <= '7') {
            value = value * 8u +
                    (ctool_u32)(token->spelling.data[index] - '0');
            index++;
            count++;
          }
          if (value > 255u) {
            return cfront_emit_failure(
                context, CTOOL_ERR_OVERFLOW,
                CTOOL_C_PARSE_DIAG_EXPRESSION, token,
                "narrow string octal escape exceeds one target byte");
          }
        } else {
          return cfront_emit_failure(
              context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION,
              token, "string literal escape is unsupported");
        }
        break;
      }
    }
    status = ctool_buffer_put_u8(bytes, (ctool_u8)value);
    if (status != CTOOL_OK) {
      return cfront_storage_failure(context, status);
    }
  }
  return CTOOL_OK;
}

static ctool_status_t cfront_parse_body_string(
    cfront_context_t *context, cfront_expression_value_t *value_out) {
  const ctool_c_pp_token_t *first = cfront_peek(context);
  const ctool_limits_t *limits = ctool_job_limits(context->job);
  ctool_buffer_t *decoded = (ctool_buffer_t *)0;
  ctool_bytes_t decoded_view;
  ctool_bytes_t owned;
  ctool_c_type_node_t array;
  ctool_c_expression_t expression;
  ctool_u32 diagnostic_count = ctool_job_diagnostic_count(context->job);
  ctool_u32 character_type;
  ctool_u32 array_type;
  ctool_u32 initial = limits->output_bytes < 64u
                          ? limits->output_bytes
                          : 64u;
  ctool_status_t status = ctool_job_open_buffer(
      context->job, initial, limits->output_bytes, &decoded);
  while (status == CTOOL_OK && cfront_peek(context) !=
                                   (const ctool_c_pp_token_t *)0 &&
         cfront_peek(context)->kind == CTOOL_C_PP_TOKEN_STRING) {
    status = cfront_decode_narrow_string_token(context, cfront_peek(context),
                                               decoded);
    if (status == CTOOL_OK) {
      (void)cfront_advance(context);
    }
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_put_u8(decoded, 0u);
  }
  decoded_view = status == CTOOL_OK ? ctool_buffer_view(decoded)
                                    : ctool_bytes((const void *)0, 0u);
  if (status == CTOOL_OK) {
    status = ctool_arena_copy_bytes(ctool_job_arena(context->job),
                                    decoded_view, &owned);
  }
  if (decoded != (ctool_buffer_t *)0) {
    ctool_buffer_close(decoded);
  }
  if (status != CTOOL_OK) {
    if (ctool_job_diagnostic_count(context->job) != diagnostic_count) {
      return status;
    }
    return cfront_storage_failure(context, status);
  }
  status = cfront_scalar_type(context, CTOOL_C_TYPE_CHAR, first,
                              &character_type);
  if (status != CTOOL_OK) {
    return cfront_storage_failure(context, status);
  }
  cfront_node_init(&array, CTOOL_C_TYPE_ARRAY, first);
  array.referenced_type = character_type;
  array.array_bound_kind = CTOOL_C_ARRAY_FIXED;
  array.element_count = owned.size;
  status = cfront_type_append(context, &array, &array_type);
  if (status != CTOOL_OK) {
    return cfront_storage_failure(context, status);
  }
  cfront_expression_init(&expression, CTOOL_C_EXPRESSION_STRING,
                         &first->location, &first->physical_location);
  expression.type = array_type;
  expression.string_bytes = owned;
  status = cfront_append_expression(context, &expression,
                                    &value_out->expression);
  if (status == CTOOL_OK) {
    value_out->type = array_type;
    value_out->is_lvalue = CTOOL_TRUE;
    value_out->is_bit_field = CTOOL_FALSE;
    value_out->bit_width = 0u;
    value_out->address_forbidden = CTOOL_FALSE;
  }
  return status;
}

static ctool_bool cfront_find_active_parameter(
    const cfront_context_t *context, ctool_string_t name,
    ctool_u32 *parameter_out, ctool_u32 *type_out) {
  ctool_c_type_node_t function;
  ctool_u32 index;
  if (context->in_function_body == CTOOL_FALSE ||
      cfront_type_get(context, context->active_function_type, &function) !=
          CTOOL_OK ||
      function.kind != CTOOL_C_TYPE_FUNCTION) {
    return CTOOL_FALSE;
  }
  index = function.parameter_count;
  while (index != 0u) {
    ctool_c_parameter_t parameter;
    ctool_u32 parameter_index = function.first_parameter + (index - 1u);
    if (cfront_vector_get(&context->parameters, parameter_index, &parameter) !=
        CTOOL_OK) {
      return CTOOL_FALSE;
    }
    if (cfront_string_equal(parameter.name, name) == CTOOL_TRUE) {
      *parameter_out = parameter_index;
      *type_out = parameter.type;
      return CTOOL_TRUE;
    }
    index--;
  }
  return CTOOL_FALSE;
}

static ctool_status_t cfront_enter_block_scope(cfront_context_t *context) {
  ctool_u32 mark = context->active_block_binding_indices.count;
  ctool_status_t status =
      cfront_vector_append(&context->block_scope_marks, &mark,
                           (ctool_u32 *)0);
  return status == CTOOL_OK ? CTOOL_OK
                            : cfront_storage_failure(context, status);
}

static ctool_status_t cfront_leave_block_scope(cfront_context_t *context) {
  ctool_u32 mark;
  ctool_status_t status;
  if (context->block_scope_marks.count == 0u) {
    return CTOOL_ERR_INTERNAL;
  }
  status = cfront_vector_get(
      &context->block_scope_marks, context->block_scope_marks.count - 1u,
      &mark);
  if (status == CTOOL_OK) {
    status = cfront_vector_rewind(&context->active_block_binding_indices,
                                  mark);
  }
  if (status == CTOOL_OK) {
    status = cfront_vector_rewind(&context->block_scope_marks,
                                  context->block_scope_marks.count - 1u);
  }
  return status;
}

static ctool_bool cfront_current_block_name_exists(
    const cfront_context_t *context, ctool_string_t name) {
  ctool_u32 mark;
  ctool_u32 active;
  if (context->block_scope_marks.count == 0u ||
      cfront_vector_get(&context->block_scope_marks,
                        context->block_scope_marks.count - 1u,
                        &mark) != CTOOL_OK) {
    return CTOOL_FALSE;
  }
  active = context->active_block_binding_indices.count;
  while (active > mark) {
    ctool_u32 index;
    ctool_c_block_binding_t binding;
    active--;
    if (cfront_vector_get(&context->active_block_binding_indices, active,
                          &index) != CTOOL_OK ||
        cfront_block_binding_get(context, index, &binding) != CTOOL_OK) {
      return CTOOL_FALSE;
    }
    if (cfront_string_equal(binding.name, name) == CTOOL_TRUE) {
      return CTOOL_TRUE;
    }
  }
  return CTOOL_FALSE;
}

static ctool_status_t cfront_append_block_binding(
    cfront_context_t *context, ctool_string_t name, ctool_u32 type,
    cfront_storage_t storage, const ctool_c_pp_token_t *name_token,
    const ctool_c_pp_location_t *location,
    const ctool_c_pp_location_t *physical_location,
    ctool_u32 *binding_out) {
  ctool_c_block_binding_t binding;
  ctool_u32 parameter;
  ctool_u32 parameter_type;
  ctool_u32 index;
  ctool_status_t status;
  if (cfront_current_block_name_exists(context, name) == CTOOL_TRUE ||
      (context->block_scope_marks.count == 1u &&
       cfront_find_active_parameter(context, name, &parameter,
                                    &parameter_type) == CTOOL_TRUE)) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_REDEFINITION,
        name_token, "block-scope identifier is already declared in this scope");
  }
  cfront_zero(&binding, (ctool_u32)sizeof(binding));
  binding.name = name;
  binding.kind = CTOOL_C_BINDING_OBJECT;
  binding.storage = cfront_public_storage(storage);
  binding.type = type;
  binding.location = *location;
  binding.physical_location = *physical_location;
  status = cfront_vector_append(&context->block_bindings, &binding, &index);
  if (status == CTOOL_OK) {
    status = cfront_vector_append(&context->active_block_binding_indices,
                                  &index, (ctool_u32 *)0);
  }
  if (status != CTOOL_OK) {
    return cfront_storage_failure(context, status);
  }
  *binding_out = index;
  return CTOOL_OK;
}

static ctool_status_t cfront_integer_type(
    const cfront_context_t *context, ctool_u32 type,
    cfront_integer_type_t *integer_out, ctool_bool *is_integer_out) {
  ctool_c_type_node_t node;
  ctool_u32 base;
  ctool_u32 qualifiers;
  ctool_status_t status = cfront_underlying_type(
      context, type, &base, &qualifiers, &node);
  (void)qualifiers;
  *is_integer_out = CTOOL_FALSE;
  if (status != CTOOL_OK) {
    return status;
  }
  if (node.kind == CTOOL_C_TYPE_ENUM) {
    return cfront_integer_type(context, node.referenced_type, integer_out,
                               is_integer_out);
  }
  if (cfront_integer_type_kind(node.kind) == CTOOL_FALSE) {
    return CTOOL_OK;
  }
  cfront_zero(integer_out, (ctool_u32)sizeof(*integer_out));
  integer_out->kind = node.kind;
  integer_out->type = base;
  switch (node.kind) {
  case CTOOL_C_TYPE_BOOL:
    integer_out->rank = 1u;
    integer_out->width = 1u;
    integer_out->is_unsigned = CTOOL_TRUE;
    break;
  case CTOOL_C_TYPE_CHAR:
  case CTOOL_C_TYPE_SIGNED_CHAR:
    integer_out->rank = 2u;
    integer_out->width = 8u;
    break;
  case CTOOL_C_TYPE_UNSIGNED_CHAR:
    integer_out->rank = 2u;
    integer_out->width = 8u;
    integer_out->is_unsigned = CTOOL_TRUE;
    break;
  case CTOOL_C_TYPE_SIGNED_SHORT:
    integer_out->rank = 3u;
    integer_out->width = 16u;
    break;
  case CTOOL_C_TYPE_UNSIGNED_SHORT:
    integer_out->rank = 3u;
    integer_out->width = 16u;
    integer_out->is_unsigned = CTOOL_TRUE;
    break;
  case CTOOL_C_TYPE_SIGNED_INT:
    integer_out->rank = 4u;
    integer_out->width = 32u;
    break;
  case CTOOL_C_TYPE_UNSIGNED_INT:
    integer_out->rank = 4u;
    integer_out->width = 32u;
    integer_out->is_unsigned = CTOOL_TRUE;
    break;
  case CTOOL_C_TYPE_SIGNED_LONG:
    integer_out->rank = 5u;
    integer_out->width = 32u;
    break;
  case CTOOL_C_TYPE_UNSIGNED_LONG:
    integer_out->rank = 5u;
    integer_out->width = 32u;
    integer_out->is_unsigned = CTOOL_TRUE;
    break;
  case CTOOL_C_TYPE_SIGNED_LONG_LONG:
    integer_out->rank = 6u;
    integer_out->width = 64u;
    break;
  case CTOOL_C_TYPE_UNSIGNED_LONG_LONG:
    integer_out->rank = 6u;
    integer_out->width = 64u;
    integer_out->is_unsigned = CTOOL_TRUE;
    break;
  default:
    return CTOOL_ERR_INTERNAL;
  }
  *is_integer_out = CTOOL_TRUE;
  return CTOOL_OK;
}

static ctool_c_type_kind_t cfront_unsigned_integer_kind(
    ctool_c_type_kind_t kind) {
  switch (kind) {
  case CTOOL_C_TYPE_SIGNED_INT:
    return CTOOL_C_TYPE_UNSIGNED_INT;
  case CTOOL_C_TYPE_SIGNED_LONG:
    return CTOOL_C_TYPE_UNSIGNED_LONG;
  case CTOOL_C_TYPE_SIGNED_LONG_LONG:
    return CTOOL_C_TYPE_UNSIGNED_LONG_LONG;
  default:
    return kind;
  }
}

static ctool_status_t cfront_common_integer_type(
    cfront_context_t *context, const cfront_integer_type_t *left,
    const cfront_integer_type_t *right,
    const ctool_c_pp_token_t *operator_token, ctool_u32 *type_out) {
  ctool_c_type_kind_t kind;
  if (left->kind == right->kind) {
    kind = left->kind;
  } else if (left->is_unsigned == right->is_unsigned) {
    kind = left->rank > right->rank ? left->kind : right->kind;
  } else {
    const cfront_integer_type_t *unsigned_type =
        left->is_unsigned == CTOOL_TRUE ? left : right;
    const cfront_integer_type_t *signed_type =
        left->is_unsigned == CTOOL_TRUE ? right : left;
    if (unsigned_type->rank >= signed_type->rank) {
      kind = unsigned_type->kind;
    } else if (signed_type->width > unsigned_type->width) {
      kind = signed_type->kind;
    } else {
      kind = cfront_unsigned_integer_kind(signed_type->kind);
    }
  }
  return cfront_scalar_type(context, kind, operator_token, type_out);
}

static ctool_status_t cfront_floating_type(
    const cfront_context_t *context, ctool_u32 type,
    ctool_bool *is_floating_out) {
  ctool_c_type_node_t node;
  ctool_u32 base;
  ctool_u32 qualifiers;
  ctool_status_t status = cfront_underlying_type(
      context, type, &base, &qualifiers, &node);
  (void)base;
  (void)qualifiers;
  if (status == CTOOL_OK) {
    *is_floating_out =
        node.kind == CTOOL_C_TYPE_FLOAT ||
                node.kind == CTOOL_C_TYPE_DOUBLE ||
                node.kind == CTOOL_C_TYPE_LONG_DOUBLE
            ? CTOOL_TRUE
            : CTOOL_FALSE;
  }
  return status;
}

static ctool_bool cfront_decimal_digit(char value) {
  return value >= '0' && value <= '9' ? CTOOL_TRUE : CTOOL_FALSE;
}

static ctool_bool cfront_body_floating_constant(ctool_string_t spelling) {
  ctool_u32 index = 0u;
  ctool_u32 digit_count = 0u;
  ctool_bool has_point = CTOOL_FALSE;
  ctool_bool has_exponent = CTOOL_FALSE;
  if (spelling.size >= 2u && spelling.data[0] == '0' &&
      (spelling.data[1] == 'x' || spelling.data[1] == 'X')) {
    index = 2u;
    while (index < spelling.size) {
      ctool_u32 digit;
      if (cfront_digit_value(spelling.data[index], &digit) == CTOOL_FALSE ||
          digit >= 16u) {
        break;
      }
      digit_count++;
      index++;
    }
    if (index < spelling.size && spelling.data[index] == '.') {
      index++;
      while (index < spelling.size) {
        ctool_u32 digit;
        if (cfront_digit_value(spelling.data[index], &digit) == CTOOL_FALSE ||
            digit >= 16u) {
          break;
        }
        digit_count++;
        index++;
      }
    }
    if (digit_count == 0u || index >= spelling.size ||
        (spelling.data[index] != 'p' && spelling.data[index] != 'P')) {
      return CTOOL_FALSE;
    }
    has_exponent = CTOOL_TRUE;
    index++;
  } else {
    while (index < spelling.size &&
           cfront_decimal_digit(spelling.data[index]) == CTOOL_TRUE) {
      digit_count++;
      index++;
    }
    if (index < spelling.size && spelling.data[index] == '.') {
      has_point = CTOOL_TRUE;
      index++;
      while (index < spelling.size &&
             cfront_decimal_digit(spelling.data[index]) == CTOOL_TRUE) {
        digit_count++;
        index++;
      }
    }
    if (digit_count == 0u) {
      return CTOOL_FALSE;
    }
    if (index < spelling.size &&
        (spelling.data[index] == 'e' || spelling.data[index] == 'E')) {
      has_exponent = CTOOL_TRUE;
      index++;
    } else if (has_point == CTOOL_FALSE) {
      return CTOOL_FALSE;
    }
  }
  if (has_exponent == CTOOL_TRUE) {
    ctool_u32 exponent_digits = 0u;
    if (index < spelling.size &&
        (spelling.data[index] == '+' || spelling.data[index] == '-')) {
      index++;
    }
    while (index < spelling.size &&
           cfront_decimal_digit(spelling.data[index]) == CTOOL_TRUE) {
      exponent_digits++;
      index++;
    }
    if (exponent_digits == 0u) {
      return CTOOL_FALSE;
    }
  }
  if (index < spelling.size &&
      (spelling.data[index] == 'f' || spelling.data[index] == 'F' ||
       spelling.data[index] == 'l' || spelling.data[index] == 'L')) {
    index++;
  }
  return index == spelling.size ? CTOOL_TRUE : CTOOL_FALSE;
}

static ctool_status_t cfront_parse_body_expression(
    cfront_context_t *context, cfront_expression_value_t *value_out);

static ctool_status_t cfront_parse_body_integer_constant(
    cfront_context_t *context, cfront_expression_value_t *value_out) {
  const ctool_c_pp_token_t *token = cfront_peek(context);
  cfront_integer_t value = {0ull, CFRONT_INTEGER_SIGNED_32};
  ctool_c_type_kind_t kind = CTOOL_C_TYPE_SIGNED_INT;
  ctool_c_expression_t expression;
  ctool_u32 type;
  ctool_status_t status;
  if (cfront_body_floating_constant(token->spelling) == CTOOL_TRUE) {
    return cfront_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_EXPRESSION,
        token, "floating constants are outside this expression slice");
  }
  status = cfront_parse_number_token(
      context, token, CTOOL_C_PARSE_DIAG_EXPRESSION, &value, &kind);
  if (status != CTOOL_OK) {
    return status;
  }
  (void)cfront_advance(context);
  status = cfront_scalar_type(context, kind, token, &type);
  if (status != CTOOL_OK) {
    return cfront_storage_failure(context, status);
  }
  cfront_expression_init(&expression, CTOOL_C_EXPRESSION_INTEGER_CONSTANT,
                         &token->location, &token->physical_location);
  expression.type = type;
  expression.integer_bits = value.bits;
  status = cfront_append_expression(context, &expression,
                                    &value_out->expression);
  if (status == CTOOL_OK) {
    value_out->type = type;
    value_out->is_lvalue = CTOOL_FALSE;
    value_out->is_bit_field = CTOOL_FALSE;
    value_out->bit_width = 0u;
    value_out->address_forbidden = CTOOL_FALSE;
  }
  return status;
}

static ctool_status_t cfront_append_folded_u32_constant(
    cfront_context_t *context, const ctool_c_pp_token_t *operator_token,
    ctool_u32 bits, cfront_expression_value_t *value_out) {
  ctool_c_expression_t expression;
  ctool_u32 type;
  ctool_status_t status = cfront_scalar_type(
      context, CTOOL_C_TYPE_UNSIGNED_INT, operator_token, &type);
  if (status != CTOOL_OK) {
    return cfront_storage_failure(context, status);
  }
  cfront_expression_init(&expression, CTOOL_C_EXPRESSION_INTEGER_CONSTANT,
                         &operator_token->location,
                         &operator_token->physical_location);
  expression.type = type;
  expression.integer_bits = bits;
  status = cfront_append_expression(context, &expression,
                                    &value_out->expression);
  if (status == CTOOL_OK) {
    value_out->type = type;
    value_out->is_lvalue = CTOOL_FALSE;
    value_out->is_bit_field = CTOOL_FALSE;
    value_out->bit_width = 0u;
    value_out->address_forbidden = CTOOL_FALSE;
  }
  return status;
}

static ctool_status_t cfront_decode_body_character(
    cfront_context_t *context, const ctool_c_pp_token_t *token,
    ctool_u32 *value_out) {
  ctool_u32 index;
  ctool_u32 value;
  ctool_u32 end;
  if (token->spelling.size < 3u || token->spelling.data[0] != '\'' ||
      token->spelling.data[token->spelling.size - 1u] != '\'') {
    return cfront_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_EXPRESSION,
        token, "only ordinary narrow character constants are supported");
  }
  index = 1u;
  end = token->spelling.size - 1u;
  value = (ctool_u8)token->spelling.data[index++];
  if (value == (ctool_u32)'\\') {
    char escaped;
    if (index >= end) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION, token,
          "character constant escape is incomplete");
    }
    escaped = token->spelling.data[index++];
    switch (escaped) {
    case '\'':
    case '"':
    case '?':
    case '\\':
      value = (ctool_u8)escaped;
      break;
    case 'a':
      value = 7u;
      break;
    case 'b':
      value = 8u;
      break;
    case 'f':
      value = 12u;
      break;
    case 'n':
      value = 10u;
      break;
    case 'r':
      value = 13u;
      break;
    case 't':
      value = 9u;
      break;
    case 'v':
      value = 11u;
      break;
    case 'x': {
      ctool_u32 digit;
      ctool_bool saw_digit = CTOOL_FALSE;
      value = 0u;
      while (index < end &&
             cfront_digit_value(token->spelling.data[index], &digit) ==
                 CTOOL_TRUE) {
        saw_digit = CTOOL_TRUE;
        if (value > (255u - digit) / 16u) {
          return cfront_emit_failure(
              context, CTOOL_ERR_OVERFLOW, CTOOL_C_PARSE_DIAG_EXPRESSION,
              token,
              "narrow character hexadecimal escape exceeds one target byte");
        }
        value = value * 16u + digit;
        index++;
      }
      if (saw_digit == CTOOL_FALSE) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION, token,
            "character hexadecimal escape requires a digit");
      }
      break;
    }
    default:
      if (escaped >= '0' && escaped <= '7') {
        ctool_u32 count = 1u;
        value = (ctool_u32)(escaped - '0');
        while (count < 3u && index < end &&
               token->spelling.data[index] >= '0' &&
               token->spelling.data[index] <= '7') {
          value = value * 8u +
                  (ctool_u32)(token->spelling.data[index] - '0');
          index++;
          count++;
        }
        if (value > 255u) {
          return cfront_emit_failure(
              context, CTOOL_ERR_OVERFLOW, CTOOL_C_PARSE_DIAG_EXPRESSION,
              token, "narrow character octal escape exceeds one target byte");
        }
      } else {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION, token,
            "character constant escape is unsupported");
      }
      break;
    }
  }
  if (index != end) {
    return cfront_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_EXPRESSION,
        token, "multi-character constants are outside this body slice");
  }
  if ((value & 0x80u) != 0u) {
    value |= 0xffffff00u;
  }
  *value_out = value;
  return CTOOL_OK;
}

static ctool_status_t cfront_parse_body_character_constant(
    cfront_context_t *context, cfront_expression_value_t *value_out) {
  const ctool_c_pp_token_t *token = cfront_peek(context);
  ctool_c_expression_t expression;
  ctool_u32 value = 0u;
  ctool_u32 type;
  ctool_status_t status = cfront_decode_body_character(context, token, &value);
  if (status != CTOOL_OK) {
    return status;
  }
  (void)cfront_advance(context);
  status = cfront_scalar_type(context, CTOOL_C_TYPE_SIGNED_INT, token, &type);
  if (status != CTOOL_OK) {
    return cfront_storage_failure(context, status);
  }
  cfront_expression_init(&expression, CTOOL_C_EXPRESSION_INTEGER_CONSTANT,
                         &token->location, &token->physical_location);
  expression.type = type;
  expression.integer_bits = value;
  status = cfront_append_expression(context, &expression,
                                    &value_out->expression);
  if (status == CTOOL_OK) {
    value_out->type = type;
    value_out->is_lvalue = CTOOL_FALSE;
    value_out->is_bit_field = CTOOL_FALSE;
    value_out->bit_width = 0u;
    value_out->address_forbidden = CTOOL_FALSE;
  }
  return status;
}

static ctool_status_t cfront_parse_body_primary(
    cfront_context_t *context, cfront_expression_value_t *value_out) {
  const ctool_c_pp_token_t *token = cfront_peek(context);
  ctool_c_expression_t expression;
  ctool_c_binding_t binding;
  ctool_c_block_binding_t block_binding;
  ctool_u32 reference;
  ctool_u32 type;
  ctool_status_t status;
  if (token == (const ctool_c_pp_token_t *)0) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION, token,
        "function-body expression is incomplete");
  }
  if (token->kind == CTOOL_C_PP_TOKEN_STRING) {
    return cfront_parse_body_string(context, value_out);
  }
  if (token->kind == CTOOL_C_PP_TOKEN_NUMBER) {
    return cfront_parse_body_integer_constant(context, value_out);
  }
  if (token->kind == CTOOL_C_PP_TOKEN_CHARACTER) {
    return cfront_parse_body_character_constant(context, value_out);
  }
  if (cfront_token_is(token, "__builtin_offsetof") == CTOOL_TRUE) {
    ctool_u32 offset = 0u;
    (void)cfront_advance(context);
    status = cfront_parse_offsetof_query(
        context, token, CTOOL_C_PARSE_DIAG_EXPRESSION, &offset);
    return status == CTOOL_OK
               ? cfront_append_folded_u32_constant(context, token, offset,
                                                   value_out)
               : status;
  }
  if (cfront_token_is(token, "(") == CTOOL_TRUE) {
    ctool_status_t parenthesized = cfront_enter_syntax(context, token);
    if (parenthesized != CTOOL_OK) {
      return parenthesized;
    }
    (void)cfront_advance(context);
    parenthesized = cfront_parse_body_expression(context, value_out);
    if (parenthesized == CTOOL_OK) {
      parenthesized = cfront_expected(context, ")");
    }
    cfront_leave_syntax(context);
    return parenthesized;
  }
  if (token->kind != CTOOL_C_PP_TOKEN_IDENTIFIER) {
    return cfront_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_EXPRESSION, token,
        "expression form is outside this function-body slice");
  }
  (void)cfront_advance(context);
  if (cfront_find_active_block_binding(context, token->spelling,
                                       &block_binding, &reference) ==
      CTOOL_TRUE) {
    cfront_expression_init(&expression, CTOOL_C_EXPRESSION_BLOCK_BINDING,
                           &token->location, &token->physical_location);
    expression.type = block_binding.type;
    expression.reference = reference;
    type = block_binding.type;
    value_out->is_lvalue = CTOOL_TRUE;
    value_out->is_bit_field = CTOOL_FALSE;
    value_out->bit_width = 0u;
    value_out->address_forbidden =
        block_binding.storage == CTOOL_C_STORAGE_REGISTER ? CTOOL_TRUE
                                                          : CTOOL_FALSE;
  } else if (cfront_find_active_parameter(context, token->spelling, &reference,
                                   &type) == CTOOL_TRUE) {
    ctool_c_parameter_t parameter;
    cfront_expression_init(&expression, CTOOL_C_EXPRESSION_PARAMETER,
                           &token->location, &token->physical_location);
    expression.type = type;
    expression.reference = reference;
    value_out->is_lvalue = CTOOL_TRUE;
    value_out->is_bit_field = CTOOL_FALSE;
    value_out->bit_width = 0u;
    if (cfront_vector_get(&context->parameters, reference, &parameter) !=
        CTOOL_OK) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL, token,
          "parameter expression metadata is unavailable");
    }
    value_out->address_forbidden =
        parameter.storage == CTOOL_C_STORAGE_REGISTER ? CTOOL_TRUE
                                                       : CTOOL_FALSE;
  } else if (cfront_find_file_binding_index(context, token->spelling,
                                            &binding, &reference) ==
             CTOOL_TRUE &&
             binding.kind != CTOOL_C_BINDING_TYPEDEF) {
    cfront_expression_init(&expression, CTOOL_C_EXPRESSION_IDENTIFIER,
                           &token->location, &token->physical_location);
    expression.type = binding.type;
    expression.reference = reference;
    type = binding.type;
    value_out->is_lvalue =
        binding.kind == CTOOL_C_BINDING_OBJECT ? CTOOL_TRUE : CTOOL_FALSE;
    value_out->is_bit_field = CTOOL_FALSE;
    value_out->bit_width = 0u;
    value_out->address_forbidden = CTOOL_FALSE;
  } else {
    return cfront_emit_failure(
        context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION, token,
        "expression identifier is not declared");
  }
  status = cfront_append_expression(context, &expression,
                                    &value_out->expression);
  if (status == CTOOL_OK) {
    value_out->type = type;
  }
  return status;
}

static ctool_status_t cfront_append_conversion(
    cfront_context_t *context, ctool_c_conversion_kind_t conversion,
    ctool_u32 target_type, cfront_expression_value_t *value) {
  ctool_c_expression_t child;
  ctool_c_expression_t expression;
  ctool_u32 first_child;
  ctool_status_t status = cfront_vector_get(
      &context->expressions, value->expression, &child);
  if (status != CTOOL_OK) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
        cfront_peek(context), "expression conversion child is unavailable");
  }
  status = cfront_vector_append(&context->expression_children,
                                &value->expression, &first_child);
  if (status != CTOOL_OK) {
    return cfront_storage_failure(context, status);
  }
  cfront_expression_init(&expression, CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION,
                         &child.location, &child.physical_location);
  expression.type = target_type;
  expression.first_child = first_child;
  expression.child_count = 1u;
  expression.conversion = conversion;
  status = cfront_append_expression(context, &expression,
                                    &value->expression);
  if (status == CTOOL_OK) {
    value->type = target_type;
    value->is_lvalue = CTOOL_FALSE;
    value->is_bit_field = CTOOL_FALSE;
    value->bit_width = 0u;
    value->address_forbidden = CTOOL_FALSE;
  }
  return status;
}

static ctool_status_t cfront_append_one_child_expression(
    cfront_context_t *context, ctool_c_expression_kind_t kind,
    ctool_c_expression_operator_t operation,
    const ctool_c_pp_token_t *operator_token, ctool_u32 result_type,
    ctool_u32 reference, ctool_bool is_lvalue, ctool_bool is_bit_field,
    ctool_u32 bit_width, ctool_bool address_forbidden,
    cfront_expression_value_t *operand) {
  ctool_c_expression_t expression;
  ctool_u32 child = operand->expression;
  ctool_u32 first_child;
  ctool_status_t status = cfront_vector_append(
      &context->expression_children, &child, &first_child);
  if (status != CTOOL_OK) {
    return cfront_storage_failure(context, status);
  }
  cfront_expression_init(&expression, kind, &operator_token->location,
                         &operator_token->physical_location);
  expression.type = result_type;
  expression.first_child = first_child;
  expression.child_count = 1u;
  expression.operation = operation;
  expression.reference = reference;
  status = cfront_append_expression(context, &expression,
                                    &operand->expression);
  if (status == CTOOL_OK) {
    operand->type = result_type;
    operand->is_lvalue = is_lvalue;
    operand->is_bit_field = is_bit_field;
    operand->bit_width = bit_width;
    operand->address_forbidden = address_forbidden;
  }
  return status;
}

static ctool_status_t cfront_pointer_type_at(
    cfront_context_t *context, ctool_u32 referenced,
    const ctool_c_pp_token_t *token, ctool_u32 *type_out) {
  ctool_c_type_node_t pointer;
  cfront_node_init(&pointer, CTOOL_C_TYPE_POINTER, token);
  pointer.referenced_type = referenced;
  return cfront_type_append(context, &pointer, type_out);
}

static ctool_status_t cfront_pointer_conversion_type(
    cfront_context_t *context, ctool_u32 referenced,
    const ctool_c_expression_t *source, ctool_u32 *type_out) {
  ctool_c_type_node_t pointer;
  cfront_zero(&pointer, (ctool_u32)sizeof(pointer));
  pointer.kind = CTOOL_C_TYPE_POINTER;
  pointer.location = source->location;
  pointer.physical_location = source->physical_location;
  pointer.referenced_type = referenced;
  pointer.array_bound_kind = CTOOL_C_ARRAY_FIXED;
  pointer.record_kind = CTOOL_C_RECORD_STRUCT;
  return cfront_type_append(context, &pointer, type_out);
}

static ctool_status_t cfront_lvalue_conversion_type(
    cfront_context_t *context, ctool_u32 source_type,
    ctool_u32 *type_out) {
  cfront_vector_t alignments;
  cfront_type_ref_t reference;
  cfront_type_view_t view;
  ctool_u32 result = source_type;
  ctool_bool has_qualifiers = CTOOL_FALSE;
  ctool_status_t status;
  cfront_zero(&alignments, (ctool_u32)sizeof(alignments));
  status = cfront_vector_open(context, &alignments,
                              (ctool_u32)sizeof(ctool_c_type_node_t));
  reference.type = source_type;
  reference.qualifiers = 0u;
  while (status == CTOOL_OK) {
    status = cfront_type_view(context, reference, CTOOL_FALSE, &view);
    if (status != CTOOL_OK) {
      break;
    }
    if (view.qualifiers != 0u) {
      has_qualifiers = CTOOL_TRUE;
    }
    if (view.node.kind != CTOOL_C_TYPE_ALIGNED) {
      break;
    }
    status = cfront_vector_append(&alignments, &view.node,
                                  (ctool_u32 *)0);
    reference.type = view.node.referenced_type;
    reference.qualifiers = 0u;
  }
  if (status == CTOOL_OK && has_qualifiers == CTOOL_TRUE) {
    status = cfront_materialize_view_without_qualifiers(
        context, &view, CTOOL_C_QUAL_ALL, &result);
  }
  while (status == CTOOL_OK && has_qualifiers == CTOOL_TRUE &&
         alignments.count != 0u) {
    ctool_c_type_node_t alignment;
    status = cfront_vector_get(&alignments, alignments.count - 1u,
                               &alignment);
    if (status == CTOOL_OK) {
      status = cfront_vector_rewind(&alignments, alignments.count - 1u);
    }
    if (status == CTOOL_OK) {
      alignment.referenced_type = result;
      alignment.qualifiers = 0u;
      status = cfront_type_append(context, &alignment, &result);
    }
  }
  cfront_vector_close(&alignments);
  if (status == CTOOL_OK) {
    *type_out = result;
  }
  return status;
}

static ctool_status_t cfront_apply_default_conversion(
    cfront_context_t *context, cfront_expression_value_t *value) {
  ctool_c_expression_t source;
  ctool_c_type_node_t node;
  ctool_u32 base;
  ctool_u32 qualifiers;
  ctool_u32 target_type;
  ctool_status_t status = cfront_vector_get(
      &context->expressions, value->expression, &source);
  if (status == CTOOL_OK) {
    status = cfront_underlying_type(context, value->type, &base, &qualifiers,
                                    &node);
  }
  (void)base;
  if (status != CTOOL_OK) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
        cfront_peek(context), "expression conversion type is unavailable");
  }
  if (node.kind == CTOOL_C_TYPE_ARRAY) {
    ctool_c_pp_token_t source_token;
    ctool_u32 referenced = node.referenced_type;
    cfront_zero(&source_token, (ctool_u32)sizeof(source_token));
    source_token.location = source.location;
    source_token.physical_location = source.physical_location;
    status = cfront_qualified_type(context, referenced, qualifiers,
                                   &source_token, &referenced);
    if (status == CTOOL_OK) {
      status = cfront_pointer_conversion_type(
          context, referenced, &source, &target_type);
    }
    return status == CTOOL_OK
               ? cfront_append_conversion(
                     context, CTOOL_C_CONVERSION_ARRAY_TO_POINTER,
                     target_type, value)
               : cfront_storage_failure(context, status);
  }
  if (node.kind == CTOOL_C_TYPE_FUNCTION) {
    status = cfront_pointer_conversion_type(context, value->type, &source,
                                            &target_type);
    return status == CTOOL_OK
               ? cfront_append_conversion(
                     context, CTOOL_C_CONVERSION_FUNCTION_TO_POINTER,
                     target_type, value)
               : cfront_storage_failure(context, status);
  }
  if (value->is_lvalue == CTOOL_TRUE) {
    status = cfront_lvalue_conversion_type(context, value->type,
                                           &target_type);
    if (status != CTOOL_OK) {
      if (status == CTOOL_ERR_LIMIT || status == CTOOL_ERR_OVERFLOW ||
          status == CTOOL_ERR_NO_MEMORY) {
        return cfront_storage_failure(context, status);
      }
      return cfront_emit_failure(
          context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
          cfront_peek(context), "lvalue conversion type is unavailable");
    }
    return cfront_append_conversion(
        context, CTOOL_C_CONVERSION_LVALUE_TO_VALUE, target_type, value);
  }
  return CTOOL_OK;
}

static ctool_status_t cfront_append_integer_conversion_if_needed(
    cfront_context_t *context, ctool_c_conversion_kind_t conversion,
    ctool_u32 target_type, cfront_expression_value_t *value) {
  ctool_bool same = CTOOL_FALSE;
  ctool_status_t status = cfront_types_same(
      context, value->type, target_type, &same);
  if (status != CTOOL_OK) {
    return cfront_storage_failure(context, status);
  }
  return same == CTOOL_TRUE
             ? CTOOL_OK
             : cfront_append_conversion(context, conversion, target_type,
                                        value);
}

static ctool_status_t cfront_apply_integer_promotion(
    cfront_context_t *context, const ctool_c_pp_token_t *token,
    cfront_expression_value_t *value) {
  cfront_integer_type_t integer;
  ctool_c_type_node_t node;
  ctool_bool is_integer = CTOOL_FALSE;
  ctool_bool was_bit_field = value->is_bit_field;
  ctool_u32 base;
  ctool_u32 bit_width = value->bit_width;
  ctool_u32 qualifiers;
  ctool_u32 target;
  ctool_status_t status = cfront_apply_default_conversion(context, value);
  if (status != CTOOL_OK) {
    return status;
  }
  status = cfront_integer_type(context, value->type, &integer,
                               &is_integer);
  if (status != CTOOL_OK) {
    return cfront_storage_failure(context, status);
  }
  if (is_integer == CTOOL_FALSE) {
    status = cfront_underlying_type(context, value->type, &base, &qualifiers,
                                    &node);
    (void)base;
    (void)qualifiers;
    if (status != CTOOL_OK) {
      return cfront_storage_failure(context, status);
    }
    return cfront_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_EXPRESSION,
        token,
        node.kind == CTOOL_C_TYPE_POINTER &&
                (cfront_token_is(token, "+") == CTOOL_TRUE ||
                 cfront_token_is(token, "-") == CTOOL_TRUE)
            ? "pointer arithmetic is outside this expression slice"
            : "non-integer operators are outside this body slice");
  }
  if (was_bit_field == CTOOL_TRUE &&
      integer.kind == CTOOL_C_TYPE_UNSIGNED_INT &&
      bit_width < integer.width) {
    status = cfront_scalar_type(context, CTOOL_C_TYPE_SIGNED_INT, token,
                                &target);
  } else if (integer.rank < 4u) {
    status = cfront_scalar_type(context, CTOOL_C_TYPE_SIGNED_INT, token,
                                &target);
  } else {
    target = integer.type;
  }
  return status == CTOOL_OK
             ? cfront_append_integer_conversion_if_needed(
                   context, CTOOL_C_CONVERSION_INTEGER_PROMOTION, target,
                   value)
             : cfront_storage_failure(context, status);
}

static ctool_status_t cfront_apply_usual_integer_conversions(
    cfront_context_t *context, const ctool_c_pp_token_t *operator_token,
    cfront_expression_value_t *left, cfront_expression_value_t *right,
    ctool_u32 *type_out) {
  cfront_integer_type_t left_integer;
  cfront_integer_type_t right_integer;
  ctool_bool left_is_integer = CTOOL_FALSE;
  ctool_bool right_is_integer = CTOOL_FALSE;
  ctool_status_t status = cfront_apply_integer_promotion(
      context, operator_token, left);
  if (status != CTOOL_OK) {
    return status;
  }
  status = cfront_apply_integer_promotion(context, operator_token, right);
  if (status != CTOOL_OK) {
    return status;
  }
  status = cfront_integer_type(context, left->type, &left_integer,
                               &left_is_integer);
  if (status == CTOOL_OK) {
    status = cfront_integer_type(context, right->type, &right_integer,
                                 &right_is_integer);
  }
  if (status != CTOOL_OK) {
    return cfront_storage_failure(context, status);
  }
  if (left_is_integer == CTOOL_FALSE || right_is_integer == CTOOL_FALSE) {
    return cfront_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_EXPRESSION,
        operator_token, "non-integer operators are outside this body slice");
  }
  status = cfront_common_integer_type(
      context, &left_integer, &right_integer, operator_token, type_out);
  if (status != CTOOL_OK) {
    return cfront_storage_failure(context, status);
  }
  status = cfront_append_integer_conversion_if_needed(
      context, CTOOL_C_CONVERSION_USUAL_ARITHMETIC, *type_out, left);
  if (status != CTOOL_OK) {
    return status;
  }
  return cfront_append_integer_conversion_if_needed(
      context, CTOOL_C_CONVERSION_USUAL_ARITHMETIC, *type_out, right);
}

static ctool_status_t cfront_require_scalar_value(
    cfront_context_t *context, const ctool_c_pp_token_t *operator_token,
    cfront_expression_value_t *value) {
  cfront_integer_type_t integer;
  ctool_c_type_node_t node;
  ctool_bool is_integer = CTOOL_FALSE;
  ctool_u32 base;
  ctool_u32 qualifiers;
  ctool_status_t status = cfront_apply_default_conversion(context, value);
  if (status != CTOOL_OK) {
    return status;
  }
  status = cfront_integer_type(context, value->type, &integer,
                               &is_integer);
  if (status == CTOOL_OK && is_integer == CTOOL_FALSE) {
    status = cfront_underlying_type(context, value->type, &base, &qualifiers,
                                    &node);
  }
  (void)integer;
  (void)base;
  (void)qualifiers;
  if (status != CTOOL_OK) {
    return cfront_storage_failure(context, status);
  }
  if (is_integer == CTOOL_FALSE && node.kind != CTOOL_C_TYPE_POINTER) {
    return cfront_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_EXPRESSION,
        operator_token,
        node.kind == CTOOL_C_TYPE_FLOAT ||
                node.kind == CTOOL_C_TYPE_DOUBLE ||
                node.kind == CTOOL_C_TYPE_LONG_DOUBLE
            ? "floating logical operands are outside this body slice"
            : "non-scalar logical operands are outside this slice");
  }
  return CTOOL_OK;
}

static ctool_status_t cfront_apply_assignment_conversion(
    cfront_context_t *context, ctool_u32 target_type,
    const ctool_c_pp_token_t *source_token,
    const char *failure_message, cfront_expression_value_t *value) {
  cfront_integer_type_t target_integer;
  cfront_integer_type_t source_integer;
  ctool_bool target_is_integer = CTOOL_FALSE;
  ctool_bool source_is_integer = CTOOL_FALSE;
  ctool_bool target_is_floating = CTOOL_FALSE;
  ctool_bool source_is_floating = CTOOL_FALSE;
  ctool_bool same = CTOOL_FALSE;
  ctool_bool compatible = CTOOL_FALSE;
  ctool_status_t status = cfront_apply_default_conversion(context, value);
  if (status != CTOOL_OK) {
    return status;
  }
  status = cfront_floating_type(context, target_type,
                                &target_is_floating);
  if (status == CTOOL_OK) {
    status = cfront_floating_type(context, value->type,
                                  &source_is_floating);
  }
  if (status != CTOOL_OK) {
    return cfront_storage_failure(context, status);
  }
  if (target_is_floating == CTOOL_TRUE ||
      source_is_floating == CTOOL_TRUE) {
    return cfront_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_EXPRESSION,
        source_token,
        "floating assignment conversions are outside this body slice");
  }
  status = cfront_types_same(context, target_type, value->type, &same);
  if (status != CTOOL_OK) {
    return cfront_storage_failure(context, status);
  }
  if (same == CTOOL_TRUE) {
    return CTOOL_OK;
  }
  status = cfront_integer_type(context, target_type, &target_integer,
                               &target_is_integer);
  if (status == CTOOL_OK) {
    status = cfront_integer_type(context, value->type, &source_integer,
                                 &source_is_integer);
  }
  (void)target_integer;
  (void)source_integer;
  if (status != CTOOL_OK) {
    return cfront_storage_failure(context, status);
  }
  if (target_is_integer == CTOOL_TRUE && source_is_integer == CTOOL_TRUE) {
    return cfront_append_conversion(
        context, CTOOL_C_CONVERSION_ASSIGNMENT, target_type, value);
  }
  status = cfront_types_compatible(context, target_type, value->type,
                                   &compatible);
  if (status != CTOOL_OK) {
    return cfront_storage_failure(context, status);
  }
  if (compatible == CTOOL_TRUE) {
    return CTOOL_OK;
  }
  {
    ctool_c_type_node_t parameter_node;
    ctool_c_type_node_t argument_node;
    ctool_u32 parameter_base;
    ctool_u32 argument_base;
    ctool_u32 parameter_qualifiers;
    ctool_u32 argument_qualifiers;
    status = cfront_underlying_type(
        context, target_type, &parameter_base, &parameter_qualifiers,
        &parameter_node);
    if (status == CTOOL_OK) {
      status = cfront_underlying_type(
          context, value->type, &argument_base, &argument_qualifiers,
          &argument_node);
    }
    (void)parameter_base;
    (void)argument_base;
    (void)parameter_qualifiers;
    (void)argument_qualifiers;
    if (status != CTOOL_OK) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
          source_token, "assignment conversion type is unavailable");
    }
    if (parameter_node.kind == CTOOL_C_TYPE_POINTER &&
        argument_node.kind == CTOOL_C_TYPE_POINTER) {
      ctool_c_type_node_t parameter_referent;
      ctool_c_type_node_t argument_referent;
      ctool_u32 parameter_referent_base;
      ctool_u32 argument_referent_base;
      ctool_u32 parameter_referent_qualifiers;
      ctool_u32 argument_referent_qualifiers;
      status = cfront_underlying_type(
          context, parameter_node.referenced_type, &parameter_referent_base,
          &parameter_referent_qualifiers, &parameter_referent);
      if (status == CTOOL_OK) {
        status = cfront_underlying_type(
            context, argument_node.referenced_type, &argument_referent_base,
            &argument_referent_qualifiers, &argument_referent);
      }
      (void)parameter_referent;
      (void)argument_referent;
      if (status == CTOOL_OK) {
        status = cfront_types_compatible(
            context, parameter_referent_base, argument_referent_base,
            &compatible);
      }
      if (status != CTOOL_OK) {
        return cfront_storage_failure(context, status);
      }
      if (compatible == CTOOL_TRUE &&
          (argument_referent_qualifiers &
           ~parameter_referent_qualifiers) == 0u) {
        return cfront_append_conversion(
            context, CTOOL_C_CONVERSION_QUALIFICATION, target_type, value);
      }
    }
  }
  return cfront_emit_failure(
      context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION,
      source_token, failure_message);
}

static ctool_status_t cfront_append_member_path(
    cfront_context_t *context, const ctool_c_pp_token_t *member_token,
    const cfront_vector_t *path, cfront_expression_value_t *value) {
  ctool_u32 index;
  ctool_status_t status = CTOOL_OK;
  for (index = 0u; status == CTOOL_OK && index < path->count; index++) {
    ctool_c_record_member_t member;
    ctool_c_type_node_t record;
    ctool_u32 member_index;
    ctool_u32 record_base;
    ctool_u32 record_qualifiers;
    ctool_u32 result_type;
    status = cfront_vector_get(path, index, &member_index);
    if (status == CTOOL_OK) {
      status = cfront_vector_get(&context->members, member_index, &member);
    }
    if (status == CTOOL_OK) {
      status = cfront_underlying_type(context, value->type, &record_base,
                                      &record_qualifiers, &record);
    }
    (void)record_base;
    if (status != CTOOL_OK || record.kind != CTOOL_C_TYPE_RECORD ||
        record.record_complete == CTOOL_FALSE ||
        member_index < record.first_member ||
        member_index - record.first_member >= record.member_count) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
          member_token, "resolved member is not owned by its record operand");
    }
    record_qualifiers |= record.qualifiers;
    status = cfront_qualified_type(context, member.type, record_qualifiers,
                                   member_token, &result_type);
    if (status != CTOOL_OK) {
      return cfront_storage_failure(context, status);
    }
    status = cfront_append_one_child_expression(
        context, CTOOL_C_EXPRESSION_MEMBER,
        CTOOL_C_EXPRESSION_OPERATOR_NONE, member_token, result_type,
        member_index, value->is_lvalue, member.is_bit_field,
        member.bit_width, value->address_forbidden, value);
  }
  return status;
}

static ctool_status_t cfront_parse_body_postfix(
    cfront_context_t *context, cfront_expression_value_t *value) {
  for (;;) {
    if (cfront_peek_is(context, ".") == CTOOL_TRUE ||
        cfront_peek_is(context, "->") == CTOOL_TRUE) {
      const ctool_c_pp_token_t *operator_token = cfront_advance(context);
      const ctool_c_pp_token_t *member_token = cfront_peek(context);
      cfront_vector_t path;
      ctool_u32 member_diagnostic_count =
          ctool_job_diagnostic_count(context->job);
      ctool_status_t member_status;
      cfront_zero(&path, (ctool_u32)sizeof(path));
      if (member_token == (const ctool_c_pp_token_t *)0 ||
          member_token->kind != CTOOL_C_PP_TOKEN_IDENTIFIER) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION,
            member_token, "member access requires a member identifier");
      }
      if (cfront_token_is(operator_token, "->") == CTOOL_TRUE) {
        ctool_c_type_node_t pointer;
        ctool_u32 pointer_base;
        ctool_u32 pointer_qualifiers;
        member_status = cfront_apply_default_conversion(context, value);
        if (member_status == CTOOL_OK) {
          member_status = cfront_underlying_type(
              context, value->type, &pointer_base, &pointer_qualifiers,
              &pointer);
        }
        (void)pointer_base;
        (void)pointer_qualifiers;
        if (member_status != CTOOL_OK) {
          return cfront_storage_failure(context, member_status);
        }
        if (pointer.kind != CTOOL_C_TYPE_POINTER) {
          return cfront_emit_failure(
              context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION,
              operator_token,
              "arrow member access requires a pointer to a record or union");
        }
        member_status = cfront_append_one_child_expression(
            context, CTOOL_C_EXPRESSION_UNARY,
            CTOOL_C_EXPRESSION_OPERATOR_DEREFERENCE, operator_token,
            pointer.referenced_type, CTOOL_C_AST_NONE, CTOOL_TRUE,
            CTOOL_FALSE, 0u, CTOOL_FALSE, value);
        if (member_status != CTOOL_OK) {
          return member_status;
        }
      }
      member_status = cfront_vector_open(context, &path,
                                         (ctool_u32)sizeof(ctool_u32));
      if (member_status == CTOOL_OK) {
        member_status = cfront_resolve_member_path(
            context, value->type, member_token->spelling, member_token,
            &path);
      }
      if (member_status == CTOOL_OK) {
        member_status = cfront_append_member_path(
            context, member_token, &path, value);
      }
      cfront_vector_close(&path);
      if (member_status != CTOOL_OK) {
        if ((member_status == CTOOL_ERR_LIMIT ||
             member_status == CTOOL_ERR_OVERFLOW ||
             member_status == CTOOL_ERR_NO_MEMORY) &&
            ctool_job_diagnostic_count(context->job) ==
                member_diagnostic_count) {
          return cfront_storage_failure(context, member_status);
        }
        return member_status;
      }
      (void)cfront_advance(context);
      continue;
    }
    if (cfront_peek_is(context, "(") == CTOOL_FALSE) {
      break;
    }
    ctool_u32 diagnostic_count = ctool_job_diagnostic_count(context->job);
    const ctool_c_pp_token_t *open = cfront_advance(context);
    cfront_vector_t children;
    ctool_c_type_node_t pointer;
    ctool_c_type_node_t function;
    ctool_u32 pointer_base;
    ctool_u32 pointer_qualifiers;
    ctool_u32 function_base;
    ctool_u32 function_qualifiers;
    ctool_u32 argument_count = 0u;
    ctool_u32 first_child;
    ctool_u32 child_index;
    ctool_status_t status;
    cfront_zero(&children, (ctool_u32)sizeof(children));
    status = cfront_apply_default_conversion(context, value);
    if (status == CTOOL_OK) {
      status = cfront_underlying_type(
          context, value->type, &pointer_base, &pointer_qualifiers, &pointer);
    }
    (void)pointer_base;
    (void)pointer_qualifiers;
    if (status != CTOOL_OK) {
      return status;
    }
    if (pointer.kind != CTOOL_C_TYPE_POINTER ||
        cfront_underlying_type(context, pointer.referenced_type,
                               &function_base, &function_qualifiers,
                               &function) != CTOOL_OK ||
        function.kind != CTOOL_C_TYPE_FUNCTION) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION, open,
          "called expression does not have function or function-pointer type");
    }
    (void)function_base;
    (void)function_qualifiers;
    if (function.has_prototype == CTOOL_FALSE) {
      return cfront_emit_failure(
          context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_EXPRESSION,
          open, "calls without a prototype are outside this body slice");
    }
    status = cfront_enter_syntax(context, open);
    if (status != CTOOL_OK) {
      return status;
    }
    status = cfront_vector_open(context, &children,
                                (ctool_u32)sizeof(ctool_u32));
    if (status == CTOOL_OK) {
      status = cfront_vector_append(&children, &value->expression,
                                    (ctool_u32 *)0);
    }
    while (status == CTOOL_OK &&
           cfront_peek_is(context, ")") == CTOOL_FALSE) {
      const ctool_c_pp_token_t *argument_token = cfront_peek(context);
      cfront_expression_value_t argument;
      ctool_u32 parameter_type;
      cfront_zero(&argument, (ctool_u32)sizeof(argument));
      status = cfront_parse_body_expression(context, &argument);
      if (status == CTOOL_OK && argument_count < function.parameter_count) {
        status = cfront_vector_get(
            &context->parameter_types,
            function.first_parameter + argument_count, &parameter_type);
        if (status == CTOOL_OK) {
          status = cfront_apply_assignment_conversion(
              context, parameter_type, argument_token,
              "function call argument is not convertible to parameter type",
              &argument);
        }
      } else if (status == CTOOL_OK) {
        status = cfront_emit_failure(
            context,
            function.variadic == CTOOL_TRUE ? CTOOL_ERR_UNSUPPORTED
                                             : CTOOL_ERR_INPUT,
            CTOOL_C_PARSE_DIAG_EXPRESSION, argument_token,
            function.variadic == CTOOL_TRUE
                ? "variadic call arguments are outside this body slice"
                : "function call has too many arguments");
      }
      if (status == CTOOL_OK) {
        status = cfront_vector_append(&children, &argument.expression,
                                      (ctool_u32 *)0);
      }
      if (status == CTOOL_OK) {
        argument_count++;
        if (cfront_peek_is(context, ",") == CTOOL_TRUE) {
          (void)cfront_advance(context);
          if (cfront_peek_is(context, ")") == CTOOL_TRUE) {
            status = cfront_emit_failure(
                context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION,
                cfront_peek(context),
                "function call requires an argument after comma");
          }
        } else {
          break;
        }
      }
    }
    if (status == CTOOL_OK && argument_count < function.parameter_count) {
      status = cfront_emit_failure(
          context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION,
          cfront_peek(context), "function call has too few arguments");
    }
    if (status == CTOOL_OK) {
      status = cfront_expected(context, ")");
    }
    first_child = context->expression_children.count;
    for (child_index = 0u;
         status == CTOOL_OK && child_index < children.count; child_index++) {
      ctool_u32 child;
      status = cfront_vector_get(&children, child_index, &child);
      if (status == CTOOL_OK) {
        status = cfront_vector_append(&context->expression_children, &child,
                                      (ctool_u32 *)0);
      }
    }
    if (status == CTOOL_OK) {
      ctool_c_expression_t expression;
      cfront_expression_init(&expression, CTOOL_C_EXPRESSION_CALL,
                             &open->location, &open->physical_location);
      expression.type = function.referenced_type;
      expression.first_child = first_child;
      expression.child_count = children.count;
      status = cfront_append_expression(context, &expression,
                                        &value->expression);
      if (status == CTOOL_OK) {
        value->type = function.referenced_type;
        value->is_lvalue = CTOOL_FALSE;
        value->is_bit_field = CTOOL_FALSE;
        value->bit_width = 0u;
        value->address_forbidden = CTOOL_FALSE;
      }
    }
    cfront_vector_close(&children);
    cfront_leave_syntax(context);
    if (status != CTOOL_OK) {
      if (status == CTOOL_ERR_LIMIT || status == CTOOL_ERR_OVERFLOW ||
          status == CTOOL_ERR_NO_MEMORY) {
        if (ctool_job_diagnostic_count(context->job) != diagnostic_count) {
          return status;
        }
        return cfront_storage_failure(context, status);
      }
      return status;
    }
  }
  return CTOOL_OK;
}

typedef enum {
  CFRONT_BODY_BINARY_ARITHMETIC = 1,
  CFRONT_BODY_BINARY_SHIFT,
  CFRONT_BODY_BINARY_COMPARISON,
  CFRONT_BODY_BINARY_LOGICAL
} cfront_body_binary_semantics_t;

typedef struct {
  const char *spelling;
  ctool_c_expression_operator_t operation;
  cfront_body_binary_semantics_t semantics;
  ctool_u32 precedence;
} cfront_body_operator_t;

typedef struct {
  const ctool_c_pp_token_t *token;
  cfront_body_operator_t descriptor;
} cfront_pending_body_operator_t;

static ctool_status_t cfront_append_unary_expression(
    cfront_context_t *context, const ctool_c_pp_token_t *operator_token,
    ctool_c_expression_operator_t operation, ctool_u32 type,
    cfront_expression_value_t *operand) {
  return cfront_append_one_child_expression(
      context, CTOOL_C_EXPRESSION_UNARY, operation, operator_token, type,
      CTOOL_C_AST_NONE, CTOOL_FALSE, CTOOL_FALSE, 0u, CTOOL_FALSE, operand);
}

static ctool_status_t cfront_apply_address_operator(
    cfront_context_t *context, const ctool_c_pp_token_t *operator_token,
    cfront_expression_value_t *operand) {
  ctool_c_type_node_t node;
  ctool_u32 base;
  ctool_u32 qualifiers;
  ctool_u32 result_type;
  ctool_status_t status = cfront_underlying_type(
      context, operand->type, &base, &qualifiers, &node);
  (void)base;
  (void)qualifiers;
  if (status != CTOOL_OK) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
        operator_token, "address operand type is unavailable");
  }
  if (operand->is_bit_field == CTOOL_TRUE) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION,
        operator_token, "address operator cannot apply to a bit-field");
  }
  if (operand->address_forbidden == CTOOL_TRUE) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION,
        operator_token,
        "address operator cannot apply to a register object");
  }
  if (operand->is_lvalue == CTOOL_FALSE &&
      node.kind != CTOOL_C_TYPE_FUNCTION) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION,
        operator_token,
        "address operator requires an object lvalue or function designator");
  }
  status = cfront_pointer_type_at(context, operand->type, operator_token,
                                  &result_type);
  if (status != CTOOL_OK) {
    return cfront_storage_failure(context, status);
  }
  return cfront_append_one_child_expression(
      context, CTOOL_C_EXPRESSION_UNARY,
      CTOOL_C_EXPRESSION_OPERATOR_ADDRESS, operator_token, result_type,
      CTOOL_C_AST_NONE, CTOOL_FALSE, CTOOL_FALSE, 0u, CTOOL_FALSE, operand);
}

static ctool_status_t cfront_apply_dereference_operator(
    cfront_context_t *context, const ctool_c_pp_token_t *operator_token,
    cfront_expression_value_t *operand) {
  ctool_c_type_node_t pointer;
  ctool_c_type_node_t referent;
  ctool_u32 base;
  ctool_u32 qualifiers;
  ctool_u32 referent_base;
  ctool_u32 referent_qualifiers;
  ctool_status_t status = cfront_apply_default_conversion(context, operand);
  if (status == CTOOL_OK) {
    status = cfront_underlying_type(context, operand->type, &base,
                                    &qualifiers, &pointer);
  }
  (void)base;
  (void)qualifiers;
  if (status != CTOOL_OK) {
    return cfront_storage_failure(context, status);
  }
  if (pointer.kind != CTOOL_C_TYPE_POINTER) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION,
        operator_token, "dereference operator requires a pointer operand");
  }
  status = cfront_underlying_type(context, pointer.referenced_type,
                                  &referent_base, &referent_qualifiers,
                                  &referent);
  (void)referent_base;
  (void)referent_qualifiers;
  if (status != CTOOL_OK) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
        operator_token, "dereference referent type is unavailable");
  }
  if (referent.kind == CTOOL_C_TYPE_VOID) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION,
        operator_token,
        "dereference operator requires a pointer to an object or function");
  }
  return cfront_append_one_child_expression(
      context, CTOOL_C_EXPRESSION_UNARY,
      CTOOL_C_EXPRESSION_OPERATOR_DEREFERENCE, operator_token,
      pointer.referenced_type, CTOOL_C_AST_NONE,
      referent.kind == CTOOL_C_TYPE_FUNCTION ? CTOOL_FALSE : CTOOL_TRUE,
      CTOOL_FALSE, 0u, CTOOL_FALSE, operand);
}

static ctool_status_t cfront_apply_cast(
    cfront_context_t *context, const ctool_c_pp_token_t *cast_token,
    ctool_u32 target_type, cfront_expression_value_t *operand) {
  cfront_integer_type_t integer;
  ctool_c_type_node_t target;
  ctool_c_type_node_t source;
  ctool_u32 target_base;
  ctool_u32 source_base;
  ctool_u32 target_qualifiers;
  ctool_u32 source_qualifiers;
  ctool_bool target_integer = CTOOL_FALSE;
  ctool_bool source_integer = CTOOL_FALSE;
  ctool_status_t status = cfront_underlying_type(
      context, target_type, &target_base, &target_qualifiers, &target);
  (void)target_base;
  (void)target_qualifiers;
  if (status != CTOOL_OK) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL, cast_token,
        "cast destination type is unavailable");
  }
  status = cfront_apply_default_conversion(context, operand);
  if (status != CTOOL_OK) {
    return status;
  }
  status = cfront_underlying_type(context, operand->type, &source_base,
                                  &source_qualifiers, &source);
  (void)source_base;
  (void)source_qualifiers;
  if (status == CTOOL_OK) {
    status = cfront_integer_type(context, target_type, &integer,
                                 &target_integer);
  }
  if (status == CTOOL_OK) {
    status = cfront_integer_type(context, operand->type, &integer,
                                 &source_integer);
  }
  if (status != CTOOL_OK) {
    return cfront_storage_failure(context, status);
  }
  if (target.kind != CTOOL_C_TYPE_VOID &&
      (target.kind == CTOOL_C_TYPE_FLOAT ||
       target.kind == CTOOL_C_TYPE_DOUBLE ||
       target.kind == CTOOL_C_TYPE_LONG_DOUBLE ||
       source.kind == CTOOL_C_TYPE_FLOAT ||
       source.kind == CTOOL_C_TYPE_DOUBLE ||
       source.kind == CTOOL_C_TYPE_LONG_DOUBLE)) {
    return cfront_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_EXPRESSION,
        cast_token, "floating casts are outside this expression slice");
  }
  if (target.kind != CTOOL_C_TYPE_VOID &&
      target_integer == CTOOL_FALSE && target.kind != CTOOL_C_TYPE_POINTER) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION, cast_token,
        "cast destination must have scalar or void type");
  }
  if (target.kind != CTOOL_C_TYPE_VOID &&
      source_integer == CTOOL_FALSE && source.kind != CTOOL_C_TYPE_POINTER) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION, cast_token,
        "cast operand must have scalar type");
  }
  return cfront_append_one_child_expression(
      context, CTOOL_C_EXPRESSION_CAST, CTOOL_C_EXPRESSION_OPERATOR_NONE,
      cast_token, target_type, CTOOL_C_AST_NONE, CTOOL_FALSE, CTOOL_FALSE,
      0u, CTOOL_FALSE, operand);
}

static ctool_status_t cfront_append_binary_expression(
    cfront_context_t *context, const ctool_c_pp_token_t *operator_token,
    ctool_c_expression_kind_t kind,
    ctool_c_expression_operator_t operation, ctool_u32 type,
    ctool_u32 computation_type, cfront_expression_value_t *left,
    const cfront_expression_value_t *right) {
  ctool_c_expression_t expression;
  ctool_u32 first_child = context->expression_children.count;
  ctool_status_t status = cfront_vector_append(
      &context->expression_children, &left->expression, (ctool_u32 *)0);
  if (status == CTOOL_OK) {
    status = cfront_vector_append(&context->expression_children,
                                  &right->expression, (ctool_u32 *)0);
  }
  if (status != CTOOL_OK) {
    return cfront_storage_failure(context, status);
  }
  cfront_expression_init(&expression, kind, &operator_token->location,
                         &operator_token->physical_location);
  expression.type = type;
  expression.first_child = first_child;
  expression.child_count = 2u;
  expression.operation = operation;
  expression.computation_type = computation_type;
  status = cfront_append_expression(context, &expression,
                                    &left->expression);
  if (status == CTOOL_OK) {
    left->type = type;
    left->is_lvalue = CTOOL_FALSE;
    left->is_bit_field = CTOOL_FALSE;
    left->bit_width = 0u;
    left->address_forbidden = CTOOL_FALSE;
  }
  return status;
}

static ctool_status_t cfront_parse_body_unary(
    cfront_context_t *context, cfront_expression_value_t *value_out) {
  const ctool_c_pp_token_t *token = cfront_peek(context);
  ctool_c_expression_operator_t operation =
      CTOOL_C_EXPRESSION_OPERATOR_NONE;
  ctool_status_t status;
  if (cfront_token_is(token, "sizeof") == CTOOL_TRUE) {
    ctool_u32 size = 0u;
    (void)cfront_advance(context);
    status = cfront_parse_sizeof_query(
        context, token, CTOOL_C_PARSE_DIAG_EXPRESSION, &size);
    return status == CTOOL_OK
               ? cfront_append_folded_u32_constant(context, token, size,
                                                   value_out)
               : status;
  }
  if (cfront_token_is(token, "_Alignof") == CTOOL_TRUE ||
      cfront_token_is(token, "__alignof") == CTOOL_TRUE ||
      cfront_token_is(token, "__alignof__") == CTOOL_TRUE) {
    ctool_u32 alignment = 0u;
    (void)cfront_advance(context);
    status = cfront_parse_alignof_query(
        context, token, CTOOL_C_PARSE_DIAG_EXPRESSION, &alignment);
    return status == CTOOL_OK
               ? cfront_append_folded_u32_constant(
                     context, token, alignment, value_out)
               : status;
  }
  if (cfront_token_is(token, "(") == CTOOL_TRUE &&
      cfront_parenthesized_type_name_starts(context) == CTOOL_TRUE) {
    ctool_u32 target_type = CTOOL_C_TYPE_NONE;
    status = cfront_enter_syntax(context, token);
    if (status != CTOOL_OK) {
      return status;
    }
    (void)cfront_advance(context);
    status = cfront_parse_type_name(context, &target_type);
    if (status == CTOOL_OK) {
      status = cfront_expected(context, ")");
    }
    if (status == CTOOL_OK) {
      status = cfront_parse_body_unary(context, value_out);
    }
    if (status == CTOOL_OK) {
      status = cfront_apply_cast(context, token, target_type, value_out);
    }
    cfront_leave_syntax(context);
    return status;
  }
  if (cfront_token_is(token, "+") == CTOOL_TRUE) {
    operation = CTOOL_C_EXPRESSION_OPERATOR_UNARY_PLUS;
  } else if (cfront_token_is(token, "-") == CTOOL_TRUE) {
    operation = CTOOL_C_EXPRESSION_OPERATOR_UNARY_NEGATE;
  } else if (cfront_token_is(token, "~") == CTOOL_TRUE) {
    operation = CTOOL_C_EXPRESSION_OPERATOR_BITWISE_NOT;
  } else if (cfront_token_is(token, "!") == CTOOL_TRUE) {
    operation = CTOOL_C_EXPRESSION_OPERATOR_LOGICAL_NOT;
  } else if (cfront_token_is(token, "&") == CTOOL_TRUE) {
    operation = CTOOL_C_EXPRESSION_OPERATOR_ADDRESS;
  } else if (cfront_token_is(token, "*") == CTOOL_TRUE) {
    operation = CTOOL_C_EXPRESSION_OPERATOR_DEREFERENCE;
  }
  if (operation == CTOOL_C_EXPRESSION_OPERATOR_NONE) {
    status = cfront_parse_body_primary(context, value_out);
    return status == CTOOL_OK ? cfront_parse_body_postfix(context, value_out)
                              : status;
  }
  status = cfront_enter_syntax(context, token);
  if (status != CTOOL_OK) {
    return status;
  }
  (void)cfront_advance(context);
  status = cfront_parse_body_unary(context, value_out);
  if (status == CTOOL_OK &&
      operation == CTOOL_C_EXPRESSION_OPERATOR_LOGICAL_NOT) {
    status = cfront_require_scalar_value(context, token, value_out);
    if (status == CTOOL_OK) {
      status = cfront_scalar_type(context, CTOOL_C_TYPE_SIGNED_INT, token,
                                  &value_out->type);
    }
  } else if (status == CTOOL_OK &&
             operation == CTOOL_C_EXPRESSION_OPERATOR_ADDRESS) {
    status = cfront_apply_address_operator(context, token, value_out);
  } else if (status == CTOOL_OK &&
             operation == CTOOL_C_EXPRESSION_OPERATOR_DEREFERENCE) {
    status = cfront_apply_dereference_operator(context, token, value_out);
  } else if (status == CTOOL_OK) {
    status = cfront_apply_integer_promotion(context, token, value_out);
  }
  if (status == CTOOL_OK &&
      operation != CTOOL_C_EXPRESSION_OPERATOR_ADDRESS &&
      operation != CTOOL_C_EXPRESSION_OPERATOR_DEREFERENCE) {
    status = cfront_append_unary_expression(
        context, token, operation, value_out->type, value_out);
  }
  cfront_leave_syntax(context);
  return status;
}

static ctool_bool cfront_body_binary_operator(
    const ctool_c_pp_token_t *token, cfront_body_operator_t *operator_out) {
  static const cfront_body_operator_t operators[] = {
      {"||", CTOOL_C_EXPRESSION_OPERATOR_LOGICAL_OR,
       CFRONT_BODY_BINARY_LOGICAL, 1u},
      {"&&", CTOOL_C_EXPRESSION_OPERATOR_LOGICAL_AND,
       CFRONT_BODY_BINARY_LOGICAL, 2u},
      {"|", CTOOL_C_EXPRESSION_OPERATOR_BITWISE_OR,
       CFRONT_BODY_BINARY_ARITHMETIC, 3u},
      {"^", CTOOL_C_EXPRESSION_OPERATOR_BITWISE_XOR,
       CFRONT_BODY_BINARY_ARITHMETIC, 4u},
      {"&", CTOOL_C_EXPRESSION_OPERATOR_BITWISE_AND,
       CFRONT_BODY_BINARY_ARITHMETIC, 5u},
      {"==", CTOOL_C_EXPRESSION_OPERATOR_EQUAL,
       CFRONT_BODY_BINARY_COMPARISON, 6u},
      {"!=", CTOOL_C_EXPRESSION_OPERATOR_NOT_EQUAL,
       CFRONT_BODY_BINARY_COMPARISON, 6u},
      {"<", CTOOL_C_EXPRESSION_OPERATOR_LESS,
       CFRONT_BODY_BINARY_COMPARISON, 7u},
      {"<=", CTOOL_C_EXPRESSION_OPERATOR_LESS_EQUAL,
       CFRONT_BODY_BINARY_COMPARISON, 7u},
      {">", CTOOL_C_EXPRESSION_OPERATOR_GREATER,
       CFRONT_BODY_BINARY_COMPARISON, 7u},
      {">=", CTOOL_C_EXPRESSION_OPERATOR_GREATER_EQUAL,
       CFRONT_BODY_BINARY_COMPARISON, 7u},
      {"<<", CTOOL_C_EXPRESSION_OPERATOR_SHIFT_LEFT,
       CFRONT_BODY_BINARY_SHIFT, 8u},
      {">>", CTOOL_C_EXPRESSION_OPERATOR_SHIFT_RIGHT,
       CFRONT_BODY_BINARY_SHIFT, 8u},
      {"+", CTOOL_C_EXPRESSION_OPERATOR_ADD,
       CFRONT_BODY_BINARY_ARITHMETIC, 9u},
      {"-", CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT,
       CFRONT_BODY_BINARY_ARITHMETIC, 9u},
      {"*", CTOOL_C_EXPRESSION_OPERATOR_MULTIPLY,
       CFRONT_BODY_BINARY_ARITHMETIC, 10u},
      {"/", CTOOL_C_EXPRESSION_OPERATOR_DIVIDE,
       CFRONT_BODY_BINARY_ARITHMETIC, 10u},
      {"%", CTOOL_C_EXPRESSION_OPERATOR_REMAINDER,
       CFRONT_BODY_BINARY_ARITHMETIC, 10u}};
  ctool_u32 index;
  for (index = 0u;
       index < (ctool_u32)(sizeof(operators) / sizeof(operators[0]));
       index++) {
    if (cfront_token_is(token, operators[index].spelling) == CTOOL_TRUE) {
      *operator_out = operators[index];
      return CTOOL_TRUE;
    }
  }
  return CTOOL_FALSE;
}

static ctool_status_t cfront_reduce_body_binary(
    cfront_context_t *context, cfront_vector_t *values,
    cfront_vector_t *operators) {
  cfront_pending_body_operator_t pending;
  cfront_expression_value_t left;
  cfront_expression_value_t right;
  ctool_u32 result_type = CTOOL_C_TYPE_NONE;
  ctool_status_t status;
  cfront_zero(&pending, (ctool_u32)sizeof(pending));
  if (values->count < 2u || operators->count == 0u) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
        cfront_peek(context), "body expression reduction stack is invalid");
  }
  status = cfront_vector_get(operators, operators->count - 1u, &pending);
  if (status == CTOOL_OK) {
    status = cfront_vector_get(values, values->count - 2u, &left);
  }
  if (status == CTOOL_OK) {
    status = cfront_vector_get(values, values->count - 1u, &right);
  }
  if (status == CTOOL_OK) {
    status = cfront_vector_rewind(operators, operators->count - 1u);
  }
  if (status == CTOOL_OK) {
    status = cfront_vector_rewind(values, values->count - 2u);
  }
  if (status != CTOOL_OK) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
        pending.token, "body expression reduction stack is unavailable");
  }
  if (pending.descriptor.semantics == CFRONT_BODY_BINARY_ARITHMETIC ||
      pending.descriptor.semantics == CFRONT_BODY_BINARY_COMPARISON) {
    status = cfront_apply_usual_integer_conversions(
        context, pending.token, &left, &right, &result_type);
    if (status == CTOOL_OK && pending.descriptor.semantics ==
                                  CFRONT_BODY_BINARY_COMPARISON) {
      status = cfront_scalar_type(context, CTOOL_C_TYPE_SIGNED_INT,
                                  pending.token, &result_type);
      if (status != CTOOL_OK) {
        status = cfront_storage_failure(context, status);
      }
    }
  } else if (pending.descriptor.semantics == CFRONT_BODY_BINARY_SHIFT) {
    status = cfront_apply_integer_promotion(context, pending.token, &left);
    if (status == CTOOL_OK) {
      status = cfront_apply_integer_promotion(context, pending.token, &right);
    }
    result_type = left.type;
  } else {
    status = cfront_require_scalar_value(context, pending.token, &left);
    if (status == CTOOL_OK) {
      status = cfront_require_scalar_value(context, pending.token, &right);
    }
    if (status == CTOOL_OK) {
      status = cfront_scalar_type(context, CTOOL_C_TYPE_SIGNED_INT,
                                  pending.token, &result_type);
      if (status != CTOOL_OK) {
        status = cfront_storage_failure(context, status);
      }
    }
  }
  if (status == CTOOL_OK) {
    status = cfront_append_binary_expression(
        context, pending.token, CTOOL_C_EXPRESSION_BINARY,
        pending.descriptor.operation, result_type, CTOOL_C_TYPE_NONE, &left,
        &right);
  }
  if (status == CTOOL_OK) {
    status = cfront_vector_append(values, &left, (ctool_u32 *)0);
    if (status != CTOOL_OK) {
      status = cfront_storage_failure(context, status);
    }
  }
  return status;
}

static ctool_status_t cfront_parse_body_logical_or(
    cfront_context_t *context, cfront_expression_value_t *value_out) {
  cfront_vector_t values;
  cfront_vector_t operators;
  cfront_expression_value_t value;
  ctool_u32 diagnostic_count =
      ctool_job_diagnostic_count(context->job);
  ctool_status_t status;
  cfront_zero(&values, (ctool_u32)sizeof(values));
  cfront_zero(&operators, (ctool_u32)sizeof(operators));
  status = cfront_vector_open(context, &values,
                              (ctool_u32)sizeof(cfront_expression_value_t));
  if (status == CTOOL_OK) {
    status = cfront_vector_open(
        context, &operators,
        (ctool_u32)sizeof(cfront_pending_body_operator_t));
  }
  cfront_zero(&value, (ctool_u32)sizeof(value));
  if (status == CTOOL_OK) {
    status = cfront_parse_body_unary(context, &value);
  }
  if (status == CTOOL_OK) {
    status = cfront_vector_append(&values, &value, (ctool_u32 *)0);
  }
  while (status == CTOOL_OK) {
    cfront_pending_body_operator_t pending;
    cfront_pending_body_operator_t previous;
    if (cfront_body_binary_operator(cfront_peek(context),
                                    &pending.descriptor) == CTOOL_FALSE) {
      break;
    }
    pending.token = cfront_peek(context);
    while (status == CTOOL_OK && operators.count != 0u) {
      status = cfront_vector_get(&operators, operators.count - 1u,
                                 &previous);
      if (status != CTOOL_OK ||
          previous.descriptor.precedence < pending.descriptor.precedence) {
        break;
      }
      status = cfront_reduce_body_binary(context, &values, &operators);
    }
    if (status == CTOOL_OK) {
      status = cfront_vector_append(&operators, &pending, (ctool_u32 *)0);
    }
    if (status == CTOOL_OK) {
      (void)cfront_advance(context);
      cfront_zero(&value, (ctool_u32)sizeof(value));
      status = cfront_parse_body_unary(context, &value);
    }
    if (status == CTOOL_OK) {
      status = cfront_vector_append(&values, &value, (ctool_u32 *)0);
    }
  }
  while (status == CTOOL_OK && operators.count != 0u) {
    status = cfront_reduce_body_binary(context, &values, &operators);
  }
  if (status == CTOOL_OK && values.count == 1u) {
    status = cfront_vector_get(&values, 0u, value_out);
  } else if (status == CTOOL_OK) {
    status = cfront_emit_failure(
        context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
        cfront_peek(context), "body expression value stack is invalid");
  }
  cfront_vector_close(&operators);
  cfront_vector_close(&values);
  if (status != CTOOL_OK &&
      ctool_job_diagnostic_count(context->job) == diagnostic_count) {
    return cfront_storage_failure(context, status);
  }
  return status;
}

static ctool_status_t cfront_validate_assignment_target(
    cfront_context_t *context, const ctool_c_pp_token_t *operator_token,
    const cfront_expression_value_t *left, ctool_u32 *result_type_out) {
  cfront_integer_type_t integer;
  ctool_c_type_node_t node;
  ctool_bool is_integer = CTOOL_FALSE;
  ctool_u32 base;
  ctool_u32 qualifiers;
  ctool_status_t status;
  if (left->is_lvalue == CTOOL_FALSE) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION,
        operator_token, "assignment requires a modifiable lvalue");
  }
  status = cfront_underlying_type(context, left->type, &base, &qualifiers,
                                  &node);
  if (status == CTOOL_OK) {
    status = cfront_integer_type(context, left->type, &integer, &is_integer);
  }
  (void)base;
  (void)integer;
  if (status != CTOOL_OK) {
    return cfront_storage_failure(context, status);
  }
  qualifiers |= node.qualifiers;
  if ((qualifiers & CTOOL_C_QUAL_CONST) != 0u ||
      node.kind == CTOOL_C_TYPE_ARRAY || node.kind == CTOOL_C_TYPE_FUNCTION) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION,
        operator_token, "assignment requires a modifiable lvalue");
  }
  if (node.kind == CTOOL_C_TYPE_FLOAT ||
      node.kind == CTOOL_C_TYPE_DOUBLE ||
      node.kind == CTOOL_C_TYPE_LONG_DOUBLE) {
    return cfront_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_EXPRESSION,
        operator_token, "floating assignment is outside this body slice");
  }
  if (is_integer == CTOOL_FALSE && node.kind != CTOOL_C_TYPE_POINTER) {
    return cfront_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_EXPRESSION,
        operator_token,
        "non-scalar assignment is outside this function-body slice");
  }
  status = cfront_lvalue_conversion_type(context, left->type,
                                         result_type_out);
  return status == CTOOL_OK ? CTOOL_OK
                            : cfront_storage_failure(context, status);
}

static ctool_status_t cfront_parse_body_assignment(
    cfront_context_t *context, cfront_expression_value_t *value_out) {
  ctool_status_t status = cfront_parse_body_logical_or(context, value_out);
  if (status == CTOOL_OK && cfront_peek_is(context, "=") == CTOOL_TRUE) {
    const ctool_c_pp_token_t *operator_token = cfront_advance(context);
    const ctool_c_pp_token_t *right_token = cfront_peek(context);
    cfront_expression_value_t right;
    ctool_u32 result_type = CTOOL_C_TYPE_NONE;
    status = cfront_enter_syntax(context, operator_token);
    cfront_zero(&right, (ctool_u32)sizeof(right));
    if (status == CTOOL_OK) {
      status = cfront_validate_assignment_target(
          context, operator_token, value_out, &result_type);
    }
    if (status == CTOOL_OK) {
      status = cfront_parse_body_assignment(context, &right);
    }
    if (status == CTOOL_OK) {
      status = cfront_apply_assignment_conversion(
          context, result_type, right_token,
          "assignment right operand is not convertible to left operand type",
          &right);
    }
    if (status == CTOOL_OK) {
      status = cfront_append_binary_expression(
          context, operator_token, CTOOL_C_EXPRESSION_ASSIGNMENT,
          CTOOL_C_EXPRESSION_OPERATOR_ASSIGN, result_type, result_type,
          value_out, &right);
    }
    cfront_leave_syntax(context);
  } else if (status == CTOOL_OK &&
             (cfront_peek_is(context, "*=") == CTOOL_TRUE ||
              cfront_peek_is(context, "/=") == CTOOL_TRUE ||
              cfront_peek_is(context, "%=") == CTOOL_TRUE ||
              cfront_peek_is(context, "+=") == CTOOL_TRUE ||
              cfront_peek_is(context, "-=") == CTOOL_TRUE ||
              cfront_peek_is(context, "<<=") == CTOOL_TRUE ||
              cfront_peek_is(context, ">>=") == CTOOL_TRUE ||
              cfront_peek_is(context, "&=") == CTOOL_TRUE ||
              cfront_peek_is(context, "^=") == CTOOL_TRUE ||
              cfront_peek_is(context, "|=") == CTOOL_TRUE)) {
    status = cfront_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_EXPRESSION,
        cfront_peek(context),
        "compound assignment is outside this expression slice");
  }
  return status;
}

static ctool_status_t cfront_parse_body_expression(
    cfront_context_t *context, cfront_expression_value_t *value_out) {
  return cfront_parse_body_assignment(context, value_out);
}

static ctool_bool cfront_body_statement_keyword(
    const cfront_context_t *context) {
  static const char *const keywords[] = {
      "break", "case", "continue", "default", "do", "for", "goto",
      "if", "return", "switch", "while"};
  ctool_u32 index;
  for (index = 0u;
       index < (ctool_u32)(sizeof(keywords) / sizeof(keywords[0])); index++) {
    if (cfront_peek_is(context, keywords[index]) == CTOOL_TRUE) {
      return CTOOL_TRUE;
    }
  }
  return CTOOL_FALSE;
}

static ctool_bool cfront_body_starts_gnu_assembly(
    const cfront_context_t *context) {
  return context->request->gnu_extensions == CTOOL_TRUE &&
                 (cfront_peek_is(context, "asm") == CTOOL_TRUE ||
                  cfront_peek_is(context, "__asm") == CTOOL_TRUE ||
                  cfront_peek_is(context, "__asm__") == CTOOL_TRUE)
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t cfront_parse_block_declaration(
    cfront_context_t *context, ctool_u32 *statement_out) {
  const ctool_c_pp_token_t *declaration_token = cfront_peek(context);
  cfront_specifiers_t specifiers;
  ctool_u32 first_binding = context->block_bindings.count;
  ctool_u32 binding_count = 0u;
  ctool_status_t status = cfront_parse_specifiers(context, &specifiers);
  if (status != CTOOL_OK) {
    return status;
  }
  if (specifiers.block_tag_specifier_token !=
      (const ctool_c_pp_token_t *)0) {
    return cfront_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_STATEMENT,
        specifiers.block_tag_specifier_token,
        "block tag specifiers are outside this body slice");
  }
  if (specifiers.storage == CFRONT_STORAGE_TYPEDEF ||
      specifiers.storage == CFRONT_STORAGE_EXTERN ||
      specifiers.storage == CFRONT_STORAGE_STATIC) {
    return cfront_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_STATEMENT,
        declaration_token, "block storage class is outside this body slice");
  }
  if (specifiers.function_declaration_flags != 0u) {
    return cfront_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_STATEMENT,
        specifiers.inline_token,
        "block function specifiers are outside this body slice");
  }
  if (cfront_attributes_any(&specifiers.attributes) == CTOOL_TRUE) {
    return cfront_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_STATEMENT,
        cfront_first_attribute_token(&specifiers.attributes),
        "block declaration attributes are outside this body slice");
  }
  for (;;) {
    const ctool_c_pp_token_t *name_token = cfront_peek(context);
    cfront_attributes_t declarator_attributes;
    ctool_c_pp_location_t location;
    ctool_c_pp_location_t physical_location;
    ctool_c_type_node_t node;
    ctool_string_t name = ctool_string("");
    ctool_u32 root = CFRONT_NONE;
    ctool_u32 type = CFRONT_NONE;
    ctool_u32 base;
    ctool_u32 qualifiers;
    ctool_u32 binding;
    ctool_bool complete = CTOOL_FALSE;
    cfront_zero(&declarator_attributes,
                (ctool_u32)sizeof(declarator_attributes));
    status = cfront_parse_declarator(context, CTOOL_FALSE, &root);
    if (status == CTOOL_OK) {
      status = cfront_build_declarator(
          context, root, specifiers.type, &type, &name, &location,
          &physical_location);
    }
    if (status == CTOOL_OK) {
      status = cfront_parse_attributes(context, &declarator_attributes);
    }
    if (status != CTOOL_OK) {
      return status;
    }
    if (cfront_attributes_any(&declarator_attributes) == CTOOL_TRUE) {
      return cfront_emit_failure(
          context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_STATEMENT,
          cfront_first_attribute_token(&declarator_attributes),
          "block declaration attributes are outside this body slice");
    }
    if (cfront_peek_is(context, "=") == CTOOL_TRUE) {
      return cfront_emit_failure(
          context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_STATEMENT,
          cfront_peek(context),
          "block object initializers are outside this body slice");
    }
    status = cfront_underlying_type(context, type, &base, &qualifiers, &node);
    (void)base;
    (void)qualifiers;
    if (status != CTOOL_OK) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
          name_token, "block declaration type is unavailable");
    }
    if (node.kind == CTOOL_C_TYPE_FUNCTION) {
      return cfront_emit_failure(
          context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_STATEMENT,
          name_token,
          "block function declarations are outside this body slice");
    }
    status = cfront_type_is_complete_object_now(context, type, &complete);
    if (status != CTOOL_OK) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
          name_token, "block object completeness is unavailable");
    }
    if (complete == CTOOL_FALSE) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_TYPE_NAME,
          name_token, "block object requires a complete object type");
    }
    status = cfront_append_block_binding(
        context, name, type, specifiers.storage, name_token, &location,
        &physical_location, &binding);
    (void)binding;
    if (status != CTOOL_OK) {
      return status;
    }
    binding_count++;
    if (cfront_peek_is(context, ",") == CTOOL_TRUE) {
      (void)cfront_advance(context);
      continue;
    }
    status = cfront_expected(context, ";");
    break;
  }
  if (status == CTOOL_OK) {
    ctool_c_statement_t statement;
    cfront_statement_init(&statement, CTOOL_C_STATEMENT_DECLARATION,
                          declaration_token);
    statement.first_block_binding = first_binding;
    statement.block_binding_count = binding_count;
    status = cfront_append_statement(context, &statement, statement_out);
  }
  return status;
}

static ctool_status_t cfront_parse_compound_statement(
    cfront_context_t *context, ctool_u32 *statement_out);

static ctool_status_t cfront_parse_return_statement(
    cfront_context_t *context, ctool_u32 *statement_out) {
  const ctool_c_pp_token_t *return_token = cfront_advance(context);
  const ctool_c_pp_token_t *value_token = cfront_peek(context);
  ctool_c_type_node_t function;
  ctool_c_type_node_t result;
  cfront_integer_type_t result_integer;
  cfront_expression_value_t value;
  ctool_c_statement_t statement;
  ctool_u32 function_base;
  ctool_u32 function_qualifiers;
  ctool_u32 result_base;
  ctool_u32 result_qualifiers;
  ctool_bool result_is_integer = CTOOL_FALSE;
  ctool_bool has_value = cfront_peek_is(context, ";") == CTOOL_FALSE
                             ? CTOOL_TRUE
                             : CTOOL_FALSE;
  ctool_status_t status = cfront_underlying_type(
      context, context->active_function_type, &function_base,
      &function_qualifiers, &function);
  (void)function_base;
  (void)function_qualifiers;
  if (status == CTOOL_OK && function.kind == CTOOL_C_TYPE_FUNCTION) {
    status = cfront_underlying_type(
        context, function.referenced_type, &result_base, &result_qualifiers,
        &result);
  }
  (void)result_base;
  (void)result_qualifiers;
  if (status != CTOOL_OK || function.kind != CTOOL_C_TYPE_FUNCTION) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
        return_token, "active function result type is unavailable");
  }
  if (result.kind == CTOOL_C_TYPE_VOID) {
    if (has_value == CTOOL_TRUE) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_STATEMENT,
          value_token, "void function cannot return a value");
    }
  } else if (has_value == CTOOL_FALSE) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_STATEMENT,
        cfront_peek(context), "non-void function requires a return value");
  }
  if (has_value == CTOOL_TRUE) {
    if (result.kind == CTOOL_C_TYPE_FLOAT ||
        result.kind == CTOOL_C_TYPE_DOUBLE ||
        result.kind == CTOOL_C_TYPE_LONG_DOUBLE) {
      return cfront_emit_failure(
          context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_STATEMENT,
          return_token,
          "floating function returns are outside this body slice");
    }
    status = cfront_integer_type(context, function.referenced_type,
                                 &result_integer, &result_is_integer);
    (void)result_integer;
    if (status != CTOOL_OK) {
      return cfront_storage_failure(context, status);
    }
    if (result_is_integer == CTOOL_FALSE &&
        result.kind != CTOOL_C_TYPE_POINTER) {
      return cfront_emit_failure(
          context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_STATEMENT,
          return_token,
          "non-scalar function returns are outside this body slice");
    }
    cfront_zero(&value, (ctool_u32)sizeof(value));
    status = cfront_parse_body_expression(context, &value);
    if (status == CTOOL_OK && cfront_peek_is(context, ";") == CTOOL_FALSE) {
      status = cfront_emit_failure(
          context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_EXPRESSION,
          cfront_peek(context),
          "expression operator is outside this function-body slice");
    }
    if (status == CTOOL_OK) {
      status = cfront_apply_assignment_conversion(
          context, function.referenced_type, value_token,
          "return expression is not convertible to function result type",
          &value);
    }
  } else {
    cfront_zero(&value, (ctool_u32)sizeof(value));
    value.expression = CTOOL_C_AST_NONE;
  }
  if (status == CTOOL_OK) {
    status = cfront_expected(context, ";");
  }
  if (status == CTOOL_OK) {
    cfront_statement_init(&statement, CTOOL_C_STATEMENT_RETURN,
                          return_token);
    statement.expression = value.expression;
    status = cfront_append_statement(context, &statement, statement_out);
  }
  return status;
}

static ctool_status_t cfront_parse_body_statement(
    cfront_context_t *context, ctool_u32 *statement_out) {
  const ctool_c_pp_token_t *first = cfront_peek(context);
  cfront_expression_value_t value;
  ctool_c_statement_t statement;
  ctool_status_t status;
  if (cfront_peek_is(context, "{") == CTOOL_TRUE) {
    return cfront_parse_compound_statement(context, statement_out);
  }
  if (cfront_peek_is(context, "return") == CTOOL_TRUE) {
    return cfront_parse_return_statement(context, statement_out);
  }
  if (cfront_body_starts_gnu_assembly(context) == CTOOL_TRUE) {
    return cfront_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_STATEMENT, first,
        "GNU inline assembly is outside this function-body slice");
  }
  if (cfront_peek_is(context, "_Static_assert") == CTOOL_TRUE) {
    return cfront_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_STATEMENT, first,
        "block static assertions are outside this function-body slice");
  }
  if (cfront_starts_declaration_specifier(context, first) == CTOOL_TRUE) {
    return cfront_parse_block_declaration(context, statement_out);
  }
  if (cfront_body_statement_keyword(context) == CTOOL_TRUE) {
    return cfront_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_STATEMENT, first,
        "statement form is outside this function-body slice");
  }
  cfront_zero(&value, (ctool_u32)sizeof(value));
  status = cfront_parse_body_expression(context, &value);
  if (status == CTOOL_OK && cfront_peek_is(context, ";") == CTOOL_FALSE) {
    status = cfront_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_EXPRESSION,
        cfront_peek(context),
        "expression operator is outside this function-body slice");
  }
  if (status == CTOOL_OK) {
    status = cfront_apply_default_conversion(context, &value);
  }
  if (status == CTOOL_OK) {
    (void)cfront_advance(context);
    cfront_statement_init(&statement, CTOOL_C_STATEMENT_EXPRESSION, first);
    statement.expression = value.expression;
    status = cfront_append_statement(context, &statement, statement_out);
  }
  return status;
}

static ctool_status_t cfront_parse_compound_statement(
    cfront_context_t *context, ctool_u32 *statement_out) {
  const ctool_c_pp_token_t *open = cfront_peek(context);
  ctool_u32 diagnostic_count = ctool_job_diagnostic_count(context->job);
  cfront_vector_t children;
  ctool_u32 first_child;
  ctool_u32 index;
  ctool_bool scope_entered = CTOOL_FALSE;
  ctool_status_t status = cfront_enter_syntax(context, open);
  cfront_zero(&children, (ctool_u32)sizeof(children));
  if (status != CTOOL_OK) {
    return status;
  }
  status = cfront_expected(context, "{");
  if (status == CTOOL_OK) {
    status = cfront_enter_block_scope(context);
    if (status == CTOOL_OK) {
      scope_entered = CTOOL_TRUE;
    }
  }
  if (status == CTOOL_OK) {
    status = cfront_vector_open(context, &children, (ctool_u32)sizeof(ctool_u32));
  }
  while (status == CTOOL_OK && cfront_peek_is(context, "}") == CTOOL_FALSE) {
    ctool_u32 child;
    if (cfront_peek(context) == (const ctool_c_pp_token_t *)0) {
      status = cfront_emit_failure(
          context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_STATEMENT, open,
          "function compound statement is unterminated");
      break;
    }
    status = cfront_parse_body_statement(context, &child);
    if (status == CTOOL_OK) {
      status = cfront_vector_append(&children, &child, (ctool_u32 *)0);
    }
  }
  if (status == CTOOL_OK) {
    status = cfront_expected(context, "}");
  }
  first_child = context->statement_children.count;
  for (index = 0u; status == CTOOL_OK && index < children.count; index++) {
    ctool_u32 child;
    status = cfront_vector_get(&children, index, &child);
    if (status == CTOOL_OK) {
      status = cfront_vector_append(&context->statement_children, &child,
                                    (ctool_u32 *)0);
    }
  }
  if (status == CTOOL_OK) {
    ctool_c_statement_t statement;
    cfront_statement_init(&statement, CTOOL_C_STATEMENT_COMPOUND, open);
    statement.first_child = first_child;
    statement.child_count = children.count;
    status = cfront_append_statement(context, &statement, statement_out);
  }
  cfront_vector_close(&children);
  if (scope_entered == CTOOL_TRUE) {
    ctool_status_t scope_status = cfront_leave_block_scope(context);
    if (status == CTOOL_OK && scope_status != CTOOL_OK) {
      status = cfront_emit_failure(
          context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL, open,
          "block scope rewind failed");
    }
  }
  cfront_leave_syntax(context);
  if (status != CTOOL_OK &&
      (status == CTOOL_ERR_LIMIT || status == CTOOL_ERR_OVERFLOW ||
       status == CTOOL_ERR_NO_MEMORY) &&
      ctool_job_diagnostic_count(context->job) == diagnostic_count) {
    return cfront_storage_failure(context, status);
  }
  return status;
}

static ctool_status_t cfront_parse_function_definition(
    cfront_context_t *context, ctool_u32 binding, ctool_u32 declared_type,
    cfront_storage_t storage, ctool_u32 function_declaration_flags,
    const ctool_c_pp_token_t *name_token,
    const ctool_c_pp_location_t *location,
    const ctool_c_pp_location_t *physical_location) {
  ctool_c_type_node_t function;
  ctool_u32 base;
  ctool_u32 qualifiers;
  ctool_u32 index;
  ctool_u32 body = CTOOL_C_AST_NONE;
  ctool_u32 previous_type = context->active_function_type;
  ctool_bool previous_in_body = context->in_function_body;
  ctool_status_t status = cfront_underlying_type(
      context, declared_type, &base, &qualifiers, &function);
  (void)base;
  (void)qualifiers;
  if (status != CTOOL_OK || function.kind != CTOOL_C_TYPE_FUNCTION) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INPUT,
        CTOOL_C_PARSE_DIAG_FUNCTION_DEFINITION, name_token,
        "function definition requires a function declarator");
  }
  if (function.has_prototype == CTOOL_FALSE) {
    return cfront_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED,
        CTOOL_C_PARSE_DIAG_FUNCTION_DEFINITION, name_token,
        "old-style function definitions are outside this body slice");
  }
  for (index = 0u; index < context->function_definitions.count; index++) {
    ctool_c_function_definition_t existing;
    if (cfront_vector_get(&context->function_definitions, index, &existing) !=
        CTOOL_OK) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
          name_token, "function definition table is unavailable");
    }
    if (existing.binding == binding) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INPUT,
          CTOOL_C_PARSE_DIAG_FUNCTION_DEFINITION, name_token,
          "function already has a definition");
    }
  }
  for (index = 0u; index < function.parameter_count; index++) {
    ctool_c_parameter_t parameter;
    if (cfront_vector_get(&context->parameters,
                          function.first_parameter + index, &parameter) !=
        CTOOL_OK) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
          name_token, "function definition parameter is unavailable");
    }
    if (parameter.name.size == 0u) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INPUT,
          CTOOL_C_PARSE_DIAG_FUNCTION_DEFINITION, name_token,
          "function definition parameters require identifiers");
    }
  }
  context->active_function_type = declared_type;
  context->in_function_body = CTOOL_TRUE;
  status = cfront_parse_compound_statement(context, &body);
  context->active_function_type = previous_type;
  context->in_function_body = previous_in_body;
  if (status == CTOOL_OK) {
    ctool_c_function_definition_t definition;
    cfront_zero(&definition, (ctool_u32)sizeof(definition));
    definition.binding = binding;
    definition.declared_type = declared_type;
    definition.storage = cfront_public_storage(storage);
    definition.function_declaration_flags = function_declaration_flags;
    definition.body = body;
    definition.location = *location;
    definition.physical_location = *physical_location;
    status = cfront_vector_append(&context->function_definitions,
                                  &definition, (ctool_u32 *)0);
    if (status != CTOOL_OK) {
      return cfront_storage_failure(context, status);
    }
  }
  return status;
}

static ctool_status_t cfront_parse_external_declaration(
    cfront_context_t *context) {
  const ctool_c_pp_token_t *declaration_token = cfront_peek(context);
  cfront_specifiers_t specifiers;
  ctool_status_t status;
  if (cfront_peek_is(context, "_Static_assert") == CTOOL_TRUE) {
    return cfront_parse_static_assert(context);
  }
  if (declaration_token != (const ctool_c_pp_token_t *)0 &&
      declaration_token->kind == CTOOL_C_PP_TOKEN_CUPID_EXE) {
    return cfront_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_UNSUPPORTED,
        declaration_token,
        "Cupid #exe execution is outside the declaration frontend");
  }
  status = cfront_parse_specifiers(context, &specifiers);
  if (status != CTOOL_OK) {
    return status;
  }
  if (specifiers.storage == CFRONT_STORAGE_AUTO ||
      specifiers.storage == CFRONT_STORAGE_REGISTER) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INPUT,
        CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS, declaration_token,
        "file-scope declaration has an invalid storage class");
  }
  if (cfront_peek_is(context, ";") == CTOOL_TRUE) {
    status = cfront_validate_function_specifier_context(
        context, &specifiers, CTOOL_FALSE,
        "inline function specifier requires a function declarator");
    if (status != CTOOL_OK) {
      return status;
    }
    if (cfront_attributes_any(&specifiers.attributes) == CTOOL_TRUE) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_ATTRIBUTE,
          cfront_first_attribute_token(&specifiers.attributes),
          "declaration attributes require a declarator or type placement");
    }
    if (specifiers.storage != CFRONT_STORAGE_NONE ||
        specifiers.empty_declaration_valid == CTOOL_FALSE) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_DECLARATOR,
          declaration_token,
          "declaration does not declare an identifier, tag, or enumerator");
    }
    (void)cfront_advance(context);
    return CTOOL_OK;
  }
  ctool_bool has_prior_declarator = CTOOL_FALSE;
  for (;;) {
    ctool_u32 root = CFRONT_NONE;
    ctool_u32 type = CFRONT_NONE;
    ctool_string_t name = ctool_string("");
    ctool_c_pp_location_t location;
    ctool_c_pp_location_t physical_location;
    ctool_c_binding_kind_t kind;
    ctool_u32 binding_index = CFRONT_NONE;
    ctool_c_type_node_t type_node;
    ctool_u32 type_base;
    ctool_u32 type_qualifiers;
    cfront_binding_semantics_t binding_semantics;
    cfront_attributes_t declarator_attributes;
    const ctool_c_pp_token_t *name_token = cfront_peek(context);
    cfront_zero(&binding_semantics,
                (ctool_u32)sizeof(binding_semantics));
    declarator_attributes = specifiers.attributes;
    status = cfront_parse_declarator(context, CTOOL_FALSE, &root);
    if (status != CTOOL_OK) {
      return status;
    }
    status = cfront_build_declarator(
        context, root, specifiers.type, &type, &name, &location,
        &physical_location);
    if (status != CTOOL_OK) {
      return status;
    }
    status = cfront_parse_attributes(context, &declarator_attributes);
    if (status != CTOOL_OK) {
      return status;
    }
    if (declarator_attributes.packed == CTOOL_TRUE) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_ATTRIBUTE,
          declarator_attributes.packed_token,
          "packed attribute requires a record or record member");
    }
    if (declarator_attributes.has_alignment == CTOOL_TRUE) {
      if (specifiers.storage == CFRONT_STORAGE_TYPEDEF) {
        status = cfront_aligned_type(
            context, type, declarator_attributes.alignment,
            declarator_attributes.alignment_token, &type);
        if (status != CTOOL_OK) {
          return cfront_storage_failure(context, status);
        }
      } else {
        binding_semantics.minimum_alignment =
            declarator_attributes.alignment;
      }
    }
    if (cfront_peek_is(context, "=") == CTOOL_TRUE) {
      return cfront_emit_failure(
          context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_UNSUPPORTED,
          cfront_peek(context),
          "object initializer parsing is outside the declaration frontend");
    }
    status = cfront_underlying_type(context, type, &type_base,
                                    &type_qualifiers, &type_node);
    (void)type_base;
    (void)type_qualifiers;
    if (status != CTOOL_OK) {
      return cfront_emit_failure(context, CTOOL_ERR_INTERNAL,
                                 CTOOL_C_PARSE_DIAG_INTERNAL, name_token,
                                 "declared type is unavailable");
    }
    if (specifiers.storage == CFRONT_STORAGE_TYPEDEF) {
      kind = CTOOL_C_BINDING_TYPEDEF;
    } else if (type_node.kind == CTOOL_C_TYPE_FUNCTION) {
      kind = CTOOL_C_BINDING_FUNCTION;
    } else {
      if (type_node.kind == CTOOL_C_TYPE_VOID) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_TYPE_NAME,
            name_token, "object declaration requires an object type");
      }
      kind = CTOOL_C_BINDING_OBJECT;
    }
    if (declarator_attributes.noreturn == CTOOL_TRUE) {
      if (kind != CTOOL_C_BINDING_FUNCTION) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_ATTRIBUTE,
            declarator_attributes.noreturn_token,
            "noreturn attribute requires a function declaration");
      }
      binding_semantics.attributes |= CTOOL_C_DECL_ATTR_NORETURN;
    }
    status = cfront_validate_function_specifier_context(
        context, &specifiers,
        kind == CTOOL_C_BINDING_FUNCTION ? CTOOL_TRUE : CTOOL_FALSE,
        "inline function specifier requires a function declaration");
    if (status != CTOOL_OK) {
      return status;
    }
    binding_semantics.function_declaration_flags =
        specifiers.function_declaration_flags;
    status = cfront_append_binding(
        context, kind, cfront_public_storage(specifiers.storage), name, type,
        binding_semantics, name_token, &location, &physical_location, 0ull,
        CTOOL_FALSE, &binding_index);
    if (status != CTOOL_OK) {
      return status;
    }
    if (cfront_peek_is(context, "{") == CTOOL_TRUE) {
      if (kind != CTOOL_C_BINDING_FUNCTION) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT,
            CTOOL_C_PARSE_DIAG_FUNCTION_DEFINITION, cfront_peek(context),
            "function body requires a function declaration");
      }
      if (has_prior_declarator == CTOOL_TRUE) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT,
            CTOOL_C_PARSE_DIAG_FUNCTION_DEFINITION, cfront_peek(context),
            "function definition must be the only declaration declarator");
      }
      return cfront_parse_function_definition(
          context, binding_index, type, specifiers.storage,
          specifiers.function_declaration_flags, name_token, &location,
          &physical_location);
    }
    (void)location;
    (void)physical_location;
    if (cfront_peek_is(context, ",") == CTOOL_TRUE) {
      (void)cfront_advance(context);
      has_prior_declarator = CTOOL_TRUE;
      continue;
    }
    return cfront_expected(context, ";");
  }
}

static ctool_status_t cfront_enum_type(
    cfront_context_t *context, ctool_u32 *type_out,
    ctool_bool *anonymous_definition_out) {
  const ctool_c_pp_token_t *keyword = cfront_advance(context);
  const ctool_c_pp_token_t *name_token = cfront_peek(context);
  ctool_string_t name = ctool_string("");
  ctool_c_tag_t existing_tag;
  ctool_c_type_node_t node;
  ctool_u32 signed_int;
  ctool_u32 unsigned_int;
  ctool_u32 signed_long_long;
  ctool_u32 unsigned_long_long;
  ctool_u32 type = CFRONT_NONE;
  ctool_bool has_existing = CTOOL_FALSE;
  cfront_binding_semantics_t binding_semantics;
  ctool_status_t status;
  cfront_zero(&binding_semantics, (ctool_u32)sizeof(binding_semantics));
  *anonymous_definition_out = CTOOL_FALSE;
  status = cfront_scalar_type(context, CTOOL_C_TYPE_SIGNED_INT, keyword,
                              &signed_int);
  if (status == CTOOL_OK) {
    status = cfront_scalar_type(context, CTOOL_C_TYPE_UNSIGNED_INT, keyword,
                                &unsigned_int);
  }
  if (status == CTOOL_OK) {
    status = cfront_scalar_type(context, CTOOL_C_TYPE_SIGNED_LONG_LONG,
                                keyword, &signed_long_long);
  }
  if (status == CTOOL_OK) {
    status = cfront_scalar_type(context, CTOOL_C_TYPE_UNSIGNED_LONG_LONG,
                                keyword, &unsigned_long_long);
  }
  if (status != CTOOL_OK) {
    return cfront_storage_failure(context, status);
  }
  if (name_token != (const ctool_c_pp_token_t *)0 &&
      name_token->kind == CTOOL_C_PP_TOKEN_IDENTIFIER) {
    if (cfront_reserved_identifier(context, name_token) == CTOOL_TRUE) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_TYPE_NAME,
          name_token, "enum tag cannot use a reserved word");
    }
    name = name_token->spelling;
    (void)cfront_advance(context);
    has_existing =
        context->prototype_scope_depth != 0u &&
                cfront_peek_is(context, "{") == CTOOL_TRUE
            ? cfront_find_tag_from(context, context->prototype_tag_mark,
                                   name, &existing_tag)
            : cfront_find_tag(context, name, &existing_tag);
    if (has_existing == CTOOL_TRUE) {
      status = cfront_type_get(context, existing_tag.type, &node);
      if (status != CTOOL_OK || node.kind != CTOOL_C_TYPE_ENUM) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_REDEFINITION,
            name_token, "tag is redeclared with a different kind");
      }
      type = existing_tag.type;
    }
  }
  if (cfront_peek_is(context, "{") == CTOOL_FALSE) {
    if (name.size == 0u || has_existing == CTOOL_FALSE) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_TYPE_NAME, keyword,
          "enum reference requires a previously declared tag");
    }
    *type_out = type;
    return CTOOL_OK;
  }
  if (has_existing == CTOOL_TRUE) {
    return cfront_emit_failure(context, CTOOL_ERR_INPUT,
                               CTOOL_C_PARSE_DIAG_REDEFINITION, name_token,
                               "enum tag already has a definition");
  }
  cfront_node_init(&node, CTOOL_C_TYPE_ENUM, keyword);
  node.referenced_type = unsigned_int;
  status = cfront_type_append(context, &node, &type);
  if (status == CTOOL_OK && name.size != 0u) {
    status = cfront_append_tag(context, name, type, name_token);
  }
  if (status != CTOOL_OK) {
    return cfront_storage_failure(context, status);
  }
  (void)cfront_advance(context);
  {
    ctool_u32 binding_mark = cfront_vector_mark(&context->bindings);
    ctool_u32 copy_mark =
        cfront_vector_mark(&context->enum_binding_copies);
    cfront_integer_t previous_value = {0ull, CFRONT_INTEGER_SIGNED_32};
    ctool_bool saw_value = CTOOL_FALSE;
    ctool_bool has_negative = CTOOL_FALSE;
    ctool_u64 maximum_nonnegative = 0ull;
    ctool_u64 maximum_negative_magnitude = 0ull;
    while (cfront_peek_is(context, "}") == CTOOL_FALSE) {
      const ctool_c_pp_token_t *enumerator = cfront_peek(context);
      cfront_integer_t value = {0ull, CFRONT_INTEGER_SIGNED_32};
      ctool_u32 enumerator_type;
      if (enumerator == (const ctool_c_pp_token_t *)0 ||
          enumerator->kind != CTOOL_C_PP_TOKEN_IDENTIFIER) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT,
            CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION, enumerator,
            "enum definition requires an enumerator identifier");
      }
      if (cfront_reserved_identifier(context, enumerator) == CTOOL_TRUE) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_DECLARATOR,
            enumerator, "enumerator cannot use a reserved word");
      }
      (void)cfront_advance(context);
      if (cfront_peek_is(context, "=") == CTOOL_TRUE) {
        (void)cfront_advance(context);
        status = cfront_parse_constant_logical_or(context, &value);
        if (status != CTOOL_OK) {
          return status;
        }
      } else if (saw_value == CTOOL_TRUE) {
        ctool_u64 maximum =
            cfront_integer_unsigned(previous_value.kind) == CTOOL_TRUE
                ? cfront_integer_mask(previous_value.kind)
                : (cfront_integer_width(previous_value.kind) == 32u
                       ? 0x7fffffffull
                       : 0x7fffffffffffffffull);
        if (cfront_integer_negative(&previous_value) == CTOOL_FALSE &&
            previous_value.bits == maximum) {
          return cfront_emit_failure(
              context, CTOOL_ERR_OVERFLOW, CTOOL_C_PARSE_DIAG_OVERFLOW,
              enumerator,
              "implicit enumerator value overflows its target integer type");
        }
        value = previous_value;
        value.bits = cfront_integer_normalize_bits(value.bits + 1ull,
                                                    value.kind);
      }
      if (cfront_integer_negative(&value) == CTOOL_TRUE) {
        ctool_u64 magnitude = cfront_integer_magnitude(&value);
        has_negative = CTOOL_TRUE;
        if (magnitude > maximum_negative_magnitude) {
          maximum_negative_magnitude = magnitude;
        }
      } else if (value.bits > maximum_nonnegative) {
        maximum_nonnegative = value.bits;
      }
      if (context->request->gnu_extensions == CTOOL_FALSE &&
          ((cfront_integer_negative(&value) == CTOOL_TRUE &&
            cfront_integer_magnitude(&value) > 0x80000000ull) ||
           (cfront_integer_negative(&value) == CTOOL_FALSE &&
            value.bits > 0x7fffffffull))) {
        return cfront_emit_failure(
            context, CTOOL_ERR_OVERFLOW, CTOOL_C_PARSE_DIAG_OVERFLOW,
            enumerator,
            "C11 enumerator value is not representable as target int");
      }
      if ((cfront_integer_negative(&value) == CTOOL_TRUE &&
           cfront_integer_magnitude(&value) <= 0x80000000ull) ||
          (cfront_integer_negative(&value) == CTOOL_FALSE &&
           value.bits <= 0x7fffffffull)) {
        enumerator_type = signed_int;
      } else if (value.kind == CFRONT_INTEGER_SIGNED_64) {
        enumerator_type = signed_long_long;
      } else if (value.kind == CFRONT_INTEGER_UNSIGNED_64) {
        enumerator_type = unsigned_long_long;
      } else if (value.kind == CFRONT_INTEGER_UNSIGNED_32) {
        enumerator_type = unsigned_int;
      } else {
        enumerator_type = signed_int;
      }
      status = cfront_append_binding(
          context, CTOOL_C_BINDING_ENUMERATOR, CTOOL_C_STORAGE_NONE,
          enumerator->spelling, enumerator_type, binding_semantics, enumerator,
          &enumerator->location, &enumerator->physical_location, value.bits,
          cfront_integer_unsigned(value.kind), (ctool_u32 *)0);
      if (status != CTOOL_OK) {
        return status;
      }
      saw_value = CTOOL_TRUE;
      previous_value = value;
      if (cfront_peek_is(context, ",") == CTOOL_TRUE) {
        (void)cfront_advance(context);
        if (cfront_peek_is(context, "}") == CTOOL_TRUE) {
          break;
        }
      } else if (cfront_peek_is(context, "}") == CTOOL_FALSE) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPECTED_TOKEN,
            cfront_peek(context), "enum definition requires a comma");
      }
    }
    if (saw_value == CTOOL_FALSE) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INPUT,
          CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION, keyword,
          "enum definition requires at least one enumerator");
    }
    (void)cfront_advance(context);
    if (has_negative == CTOOL_TRUE) {
      if (maximum_negative_magnitude <= 0x80000000ull &&
          maximum_nonnegative <= 0x7fffffffull) {
        node.referenced_type = signed_int;
      } else if (maximum_negative_magnitude <= 0x8000000000000000ull &&
                 maximum_nonnegative <= 0x7fffffffffffffffull) {
        node.referenced_type = signed_long_long;
      } else {
        return cfront_emit_failure(
            context, CTOOL_ERR_OVERFLOW, CTOOL_C_PARSE_DIAG_OVERFLOW,
            keyword,
            "no target integer type represents the complete enum range");
      }
    } else {
      node.referenced_type = maximum_nonnegative <= 0xffffffffull
                                 ? unsigned_int
                                 : unsigned_long_long;
    }
    {
      ctool_bool wide_identifiers =
          maximum_negative_magnitude > 0x80000000ull ||
                  maximum_nonnegative > 0x7fffffffull
              ? CTOOL_TRUE
              : CTOOL_FALSE;
      ctool_u32 binding_index;
      for (binding_index = binding_mark;
           binding_index < context->bindings.count; binding_index++) {
        ctool_c_binding_t binding;
        status = cfront_vector_get(&context->bindings, binding_index,
                                   &binding);
        if (status == CTOOL_OK) {
          status = cfront_vector_append(&context->enum_binding_copies,
                                        &binding, (ctool_u32 *)0);
        }
        if (status != CTOOL_OK) {
          return cfront_storage_failure(context, status);
        }
      }
      status = cfront_vector_rewind(&context->bindings, binding_mark);
      if (status != CTOOL_OK) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
            keyword, "enumerator binding rewind failed");
      }
      for (binding_index = copy_mark;
           binding_index < context->enum_binding_copies.count;
           binding_index++) {
        ctool_c_binding_t binding;
        status = cfront_vector_get(&context->enum_binding_copies,
                                   binding_index, &binding);
        if (status == CTOOL_OK) {
          binding.type = wide_identifiers == CTOOL_TRUE
                             ? node.referenced_type
                             : signed_int;
          status = cfront_vector_append(&context->bindings, &binding,
                                        (ctool_u32 *)0);
        }
        if (status != CTOOL_OK) {
          return cfront_storage_failure(context, status);
        }
      }
      status =
          cfront_vector_rewind(&context->enum_binding_copies, copy_mark);
      if (status != CTOOL_OK) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
            keyword, "enumerator binding scratch rewind failed");
      }
    }
    status = cfront_type_update(context, type, &node);
    if (status != CTOOL_OK) {
      return cfront_storage_failure(context, status);
    }
  }
  *type_out = type;
  *anonymous_definition_out = name.size == 0u ? CTOOL_TRUE : CTOOL_FALSE;
  return CTOOL_OK;
}

static ctool_status_t cfront_alloc_array(cfront_context_t *context,
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
  if (cfront_multiply_overflows(count, element_size) == CTOOL_TRUE) {
    return CTOOL_ERR_OVERFLOW;
  }
  return ctool_arena_alloc_zero(ctool_job_arena(context->job), count,
                                element_size, (ctool_u32)sizeof(void *),
                                allocation_out);
}

static ctool_status_t cfront_layout_query_now(
    cfront_context_t *context, ctool_u32 type,
    const cfront_vector_t *member_path,
    const ctool_c_pp_token_t *token, ctool_u32 diagnostic_code,
    const char *incomplete_message, ctool_c_type_layout_t *layout_out,
    ctool_u32 *member_offset_out, ctool_u32 *member_alignment_out) {
  ctool_c_type_node_t *types = (ctool_c_type_node_t *)0;
  ctool_c_record_member_t *members = (ctool_c_record_member_t *)0;
  ctool_u32 *parameter_types = (ctool_u32 *)0;
  ctool_c_layout_request_t request;
  ctool_c_layout_result_t result;
  ctool_arena_t *arena = ctool_job_arena(context->job);
  ctool_arena_mark_t mark;
  ctool_u32 diagnostic_count;
  ctool_u32 index;
  ctool_u32 member_offset = 0u;
  ctool_u32 member_alignment = 0u;
  ctool_bool complete = CTOOL_FALSE;
  ctool_status_t rewind_status;
  ctool_status_t status = cfront_type_is_complete_object_now(
      context, type, &complete);
  if (status != CTOOL_OK) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL, token,
        "layout-query operand completeness is unavailable");
  }
  if (complete == CTOOL_FALSE) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INPUT, diagnostic_code, token, incomplete_message);
  }
  mark = ctool_arena_mark(arena);
  diagnostic_count = ctool_job_diagnostic_count(context->job);
  cfront_zero(&request, (ctool_u32)sizeof(request));
  cfront_zero(&result, (ctool_u32)sizeof(result));
  status = cfront_alloc_array(context, context->types.count,
                              (ctool_u32)sizeof(*types), (void **)&types);
  if (status == CTOOL_OK) {
    status = cfront_alloc_array(context, context->members.count,
                                (ctool_u32)sizeof(*members),
                                (void **)&members);
  }
  if (status == CTOOL_OK) {
    status = cfront_alloc_array(context, context->parameter_types.count,
                                (ctool_u32)sizeof(*parameter_types),
                                (void **)&parameter_types);
  }
  for (index = 0u; status == CTOOL_OK && index < context->types.count;
       index++) {
    status = cfront_type_get(context, index, &types[index]);
  }
  for (index = 0u; status == CTOOL_OK && index < context->members.count;
       index++) {
    status = cfront_vector_get(&context->members, index, &members[index]);
  }
  for (index = 0u;
       status == CTOOL_OK && index < context->parameter_types.count;
       index++) {
    status = cfront_vector_get(&context->parameter_types, index,
                               &parameter_types[index]);
  }
  if (status == CTOOL_OK) {
    request.location = token->location;
    request.physical_location = token->physical_location;
    request.types = types;
    request.type_count = context->types.count;
    request.members = members;
    request.member_count = context->members.count;
    request.parameter_types = parameter_types;
    request.parameter_type_count = context->parameter_types.count;
    status = ctool_c_layout_types(context->job, &request, &result);
  }
  if (status == CTOOL_OK &&
      (type >= result.type_count ||
       result.types == (const ctool_c_type_layout_t *)0)) {
    status = CTOOL_ERR_INTERNAL;
  }
  if (status == CTOOL_OK && layout_out != (ctool_c_type_layout_t *)0) {
    *layout_out = result.types[type];
  }
  for (index = 0u;
       status == CTOOL_OK && member_path != (const cfront_vector_t *)0 &&
       index < member_path->count;
       index++) {
    ctool_u32 member_index;
    status = cfront_vector_get(member_path, index, &member_index);
    if (status == CTOOL_OK &&
        (result.members == (const ctool_c_member_layout_t *)0 ||
         member_index >= result.member_count)) {
      status = CTOOL_ERR_INTERNAL;
    }
    if (status == CTOOL_OK) {
      if (member_offset >
          CFRONT_U32_MAX - result.members[member_index].byte_offset) {
        status = CTOOL_ERR_OVERFLOW;
      } else {
        member_offset += result.members[member_index].byte_offset;
        member_alignment = result.members[member_index].alignment;
      }
    }
  }
  if (status == CTOOL_OK && member_offset_out != (ctool_u32 *)0) {
    *member_offset_out = member_offset;
  }
  if (status == CTOOL_OK && member_alignment_out != (ctool_u32 *)0) {
    *member_alignment_out = member_alignment;
  }
  rewind_status = ctool_arena_rewind(arena, mark);
  if (rewind_status != CTOOL_OK) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL, token,
        "layout-query scratch rewind failed");
  }
  if (status != CTOOL_OK &&
      ctool_job_diagnostic_count(context->job) == diagnostic_count) {
    return cfront_storage_failure(context, status);
  }
  return status;
}

static ctool_status_t cfront_copy_string_owned(cfront_context_t *context,
                                               ctool_string_t source,
                                               ctool_string_t *copy_out) {
  return ctool_arena_copy_string(ctool_job_arena(context->job), source,
                                 copy_out);
}

static ctool_status_t cfront_copy_location_owned(
    cfront_context_t *context, const ctool_c_pp_location_t *source,
    ctool_c_pp_location_t *copy_out) {
  ctool_status_t status;
  *copy_out = *source;
  status = cfront_copy_string_owned(context, source->path, &copy_out->path);
  return status;
}

static ctool_status_t cfront_freeze(cfront_context_t *context,
                                    ctool_c_translation_unit_t *unit) {
  ctool_c_type_node_t *types = (ctool_c_type_node_t *)0;
  ctool_c_record_member_t *members = (ctool_c_record_member_t *)0;
  ctool_u32 *parameter_types = (ctool_u32 *)0;
  ctool_c_parameter_t *parameters = (ctool_c_parameter_t *)0;
  ctool_c_binding_t *bindings = (ctool_c_binding_t *)0;
  ctool_c_tag_t *tags = (ctool_c_tag_t *)0;
  ctool_c_block_binding_t *block_bindings =
      (ctool_c_block_binding_t *)0;
  ctool_c_function_definition_t *function_definitions =
      (ctool_c_function_definition_t *)0;
  ctool_c_statement_t *statements = (ctool_c_statement_t *)0;
  ctool_u32 *statement_children = (ctool_u32 *)0;
  ctool_c_expression_t *expressions = (ctool_c_expression_t *)0;
  ctool_u32 *expression_children = (ctool_u32 *)0;
  ctool_u32 index;
  if (context->active_block_binding_indices.count != 0u ||
      context->block_scope_marks.count != 0u) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
        cfront_peek(context), "block scope remained active at freeze");
  }
  ctool_status_t status = cfront_alloc_array(
      context, context->types.count, (ctool_u32)sizeof(*types),
      (void **)&types);
  if (status == CTOOL_OK) {
    status = cfront_alloc_array(context, context->members.count,
                                (ctool_u32)sizeof(*members),
                                (void **)&members);
  }
  if (status == CTOOL_OK) {
    status = cfront_alloc_array(context, context->parameter_types.count,
                                (ctool_u32)sizeof(*parameter_types),
                                (void **)&parameter_types);
  }
  if (status == CTOOL_OK) {
    status = cfront_alloc_array(context, context->parameters.count,
                                (ctool_u32)sizeof(*parameters),
                                (void **)&parameters);
  }
  if (status == CTOOL_OK) {
    status = cfront_alloc_array(context, context->bindings.count,
                                (ctool_u32)sizeof(*bindings),
                                (void **)&bindings);
  }
  if (status == CTOOL_OK) {
    status = cfront_alloc_array(context, context->tags.count,
                                (ctool_u32)sizeof(*tags), (void **)&tags);
  }
  if (status == CTOOL_OK) {
    status = cfront_alloc_array(context, context->block_bindings.count,
                                (ctool_u32)sizeof(*block_bindings),
                                (void **)&block_bindings);
  }
  if (status == CTOOL_OK) {
    status = cfront_alloc_array(
        context, context->function_definitions.count,
        (ctool_u32)sizeof(*function_definitions),
        (void **)&function_definitions);
  }
  if (status == CTOOL_OK) {
    status = cfront_alloc_array(context, context->statements.count,
                                (ctool_u32)sizeof(*statements),
                                (void **)&statements);
  }
  if (status == CTOOL_OK) {
    status = cfront_alloc_array(context, context->statement_children.count,
                                (ctool_u32)sizeof(*statement_children),
                                (void **)&statement_children);
  }
  if (status == CTOOL_OK) {
    status = cfront_alloc_array(context, context->expressions.count,
                                (ctool_u32)sizeof(*expressions),
                                (void **)&expressions);
  }
  if (status == CTOOL_OK) {
    status = cfront_alloc_array(context, context->expression_children.count,
                                (ctool_u32)sizeof(*expression_children),
                                (void **)&expression_children);
  }
  if (status != CTOOL_OK) {
    return cfront_storage_failure(context, status);
  }
  for (index = 0u; index < context->types.count; index++) {
    status = cfront_type_get(context, index, &types[index]);
    if (status == CTOOL_OK) {
      status = cfront_copy_location_owned(context, &types[index].location,
                                          &types[index].location);
    }
    if (status == CTOOL_OK) {
      status = cfront_copy_location_owned(
          context, &types[index].physical_location,
          &types[index].physical_location);
    }
    if (status != CTOOL_OK) {
      return cfront_storage_failure(context, status);
    }
  }
  for (index = 0u; index < context->members.count; index++) {
    status = cfront_vector_get(&context->members, index, &members[index]);
    if (status == CTOOL_OK) {
      status = cfront_copy_string_owned(context, members[index].name,
                                        &members[index].name);
    }
    if (status == CTOOL_OK) {
      status = cfront_copy_location_owned(context, &members[index].location,
                                          &members[index].location);
    }
    if (status == CTOOL_OK) {
      status = cfront_copy_location_owned(
          context, &members[index].physical_location,
          &members[index].physical_location);
    }
    if (status != CTOOL_OK) {
      return cfront_storage_failure(context, status);
    }
  }
  for (index = 0u; index < context->parameter_types.count; index++) {
    status = cfront_vector_get(&context->parameter_types, index,
                               &parameter_types[index]);
    if (status == CTOOL_OK) {
      status = cfront_vector_get(&context->parameters, index,
                                 &parameters[index]);
    }
    if (status == CTOOL_OK) {
      status = cfront_copy_string_owned(context, parameters[index].name,
                                        &parameters[index].name);
    }
    if (status == CTOOL_OK) {
      status = cfront_copy_location_owned(context, &parameters[index].location,
                                          &parameters[index].location);
    }
    if (status == CTOOL_OK) {
      status = cfront_copy_location_owned(
          context, &parameters[index].physical_location,
          &parameters[index].physical_location);
    }
    if (status != CTOOL_OK) {
      return cfront_storage_failure(context, status);
    }
  }
  for (index = 0u; index < context->bindings.count; index++) {
    status = cfront_vector_get(&context->bindings, index, &bindings[index]);
    if (status == CTOOL_OK) {
      status = cfront_copy_string_owned(context, bindings[index].name,
                                        &bindings[index].name);
    }
    if (status == CTOOL_OK) {
      status = cfront_copy_location_owned(context, &bindings[index].location,
                                          &bindings[index].location);
    }
    if (status == CTOOL_OK) {
      status = cfront_copy_location_owned(
          context, &bindings[index].physical_location,
          &bindings[index].physical_location);
    }
    if (status != CTOOL_OK) {
      return cfront_storage_failure(context, status);
    }
  }
  for (index = 0u; index < context->tags.count; index++) {
    status = cfront_vector_get(&context->tags, index, &tags[index]);
    if (status == CTOOL_OK) {
      status = cfront_copy_string_owned(context, tags[index].name,
                                        &tags[index].name);
    }
    if (status == CTOOL_OK) {
      status = cfront_copy_location_owned(context, &tags[index].location,
                                          &tags[index].location);
    }
    if (status == CTOOL_OK) {
      status = cfront_copy_location_owned(
          context, &tags[index].physical_location,
          &tags[index].physical_location);
    }
    if (status != CTOOL_OK) {
      return cfront_storage_failure(context, status);
    }
  }
  for (index = 0u; index < context->parameters.count; index++) {
    if (parameters[index].type >= context->types.count) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
          cfront_peek(context), "frozen parameter object type is invalid");
    }
  }
  for (index = 0u; index < context->block_bindings.count; index++) {
    status = cfront_vector_get(&context->block_bindings, index,
                               &block_bindings[index]);
    if (status == CTOOL_OK) {
      status = cfront_copy_string_owned(context, block_bindings[index].name,
                                        &block_bindings[index].name);
    }
    if (status == CTOOL_OK) {
      status = cfront_copy_location_owned(
          context, &block_bindings[index].location,
          &block_bindings[index].location);
    }
    if (status == CTOOL_OK) {
      status = cfront_copy_location_owned(
          context, &block_bindings[index].physical_location,
          &block_bindings[index].physical_location);
    }
    if (status != CTOOL_OK) {
      return cfront_storage_failure(context, status);
    }
  }
  for (index = 0u; index < context->function_definitions.count; index++) {
    status = cfront_vector_get(&context->function_definitions, index,
                               &function_definitions[index]);
    if (status == CTOOL_OK) {
      status = cfront_copy_location_owned(
          context, &function_definitions[index].location,
          &function_definitions[index].location);
    }
    if (status == CTOOL_OK) {
      status = cfront_copy_location_owned(
          context, &function_definitions[index].physical_location,
          &function_definitions[index].physical_location);
    }
    if (status != CTOOL_OK) {
      return cfront_storage_failure(context, status);
    }
  }
  for (index = 0u; index < context->statements.count; index++) {
    status = cfront_vector_get(&context->statements, index,
                               &statements[index]);
    if (status == CTOOL_OK) {
      status = cfront_copy_location_owned(context, &statements[index].location,
                                          &statements[index].location);
    }
    if (status == CTOOL_OK) {
      status = cfront_copy_location_owned(
          context, &statements[index].physical_location,
          &statements[index].physical_location);
    }
    if (status != CTOOL_OK) {
      return cfront_storage_failure(context, status);
    }
  }
  for (index = 0u; index < context->statement_children.count; index++) {
    status = cfront_vector_get(&context->statement_children, index,
                               &statement_children[index]);
    if (status != CTOOL_OK) {
      return cfront_storage_failure(context, status);
    }
  }
  for (index = 0u; index < context->expressions.count; index++) {
    status = cfront_vector_get(&context->expressions, index,
                               &expressions[index]);
    if (status == CTOOL_OK) {
      status = cfront_copy_location_owned(context, &expressions[index].location,
                                          &expressions[index].location);
    }
    if (status == CTOOL_OK) {
      status = cfront_copy_location_owned(
          context, &expressions[index].physical_location,
          &expressions[index].physical_location);
    }
    if (status != CTOOL_OK) {
      return cfront_storage_failure(context, status);
    }
  }
  for (index = 0u; index < context->expression_children.count; index++) {
    status = cfront_vector_get(&context->expression_children, index,
                               &expression_children[index]);
    if (status != CTOOL_OK) {
      return cfront_storage_failure(context, status);
    }
  }
  for (index = 0u; index < context->function_definitions.count; index++) {
    const ctool_c_function_definition_t *definition =
        &function_definitions[index];
    if (definition->binding >= context->bindings.count ||
        definition->declared_type >= context->types.count ||
        definition->body >= context->statements.count ||
        statements[definition->body].kind != CTOOL_C_STATEMENT_COMPOUND) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
          cfront_peek(context), "frozen function definition is invalid");
    }
  }
  for (index = 0u; index < context->block_bindings.count; index++) {
    const ctool_c_block_binding_t *binding = &block_bindings[index];
    if (binding->kind != CTOOL_C_BINDING_OBJECT ||
        (binding->storage != CTOOL_C_STORAGE_NONE &&
         binding->storage != CTOOL_C_STORAGE_AUTO &&
         binding->storage != CTOOL_C_STORAGE_REGISTER) ||
        binding->type >= context->types.count || binding->name.size == 0u) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
          cfront_peek(context), "frozen block binding is invalid");
    }
  }
  for (index = 0u; index < context->statements.count; index++) {
    const ctool_c_statement_t *statement = &statements[index];
    if (statement->kind == CTOOL_C_STATEMENT_COMPOUND) {
      ctool_u32 child_index;
      if (statement->expression != CTOOL_C_AST_NONE ||
          statement->first_block_binding != CTOOL_C_AST_NONE ||
          statement->block_binding_count != 0u ||
          statement->first_child > context->statement_children.count ||
          statement->child_count >
              context->statement_children.count - statement->first_child) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
            cfront_peek(context), "frozen statement child slice is invalid");
      }
      for (child_index = 0u; child_index < statement->child_count;
           child_index++) {
        if (statement_children[statement->first_child + child_index] >= index) {
          return cfront_emit_failure(
              context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
              cfront_peek(context),
              "frozen statements are not in postorder");
        }
      }
    } else if (statement->kind == CTOOL_C_STATEMENT_EXPRESSION) {
      if (statement->first_child != CTOOL_C_AST_NONE ||
          statement->child_count != 0u ||
          statement->first_block_binding != CTOOL_C_AST_NONE ||
          statement->block_binding_count != 0u ||
          statement->expression >= context->expressions.count) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
            cfront_peek(context), "frozen expression statement is invalid");
      }
    } else if (statement->kind == CTOOL_C_STATEMENT_DECLARATION) {
      if (statement->first_child != CTOOL_C_AST_NONE ||
          statement->child_count != 0u ||
          statement->expression != CTOOL_C_AST_NONE ||
          statement->first_block_binding > context->block_bindings.count ||
          statement->block_binding_count == 0u ||
          statement->block_binding_count >
              context->block_bindings.count -
                  statement->first_block_binding) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
            cfront_peek(context),
            "frozen block declaration statement is invalid");
      }
    } else if (statement->kind == CTOOL_C_STATEMENT_RETURN) {
      if (statement->first_child != CTOOL_C_AST_NONE ||
          statement->child_count != 0u ||
          statement->first_block_binding != CTOOL_C_AST_NONE ||
          statement->block_binding_count != 0u ||
          (statement->expression != CTOOL_C_AST_NONE &&
           statement->expression >= context->expressions.count)) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
            cfront_peek(context), "frozen return statement is invalid");
      }
    } else {
      return cfront_emit_failure(
          context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
          cfront_peek(context), "frozen statement kind is invalid");
    }
  }
  for (index = 0u; index < context->expressions.count; index++) {
    const ctool_c_expression_t *expression = &expressions[index];
    ctool_u32 child_index;
    if (expression->type >= context->types.count) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
          cfront_peek(context), "frozen expression type is invalid");
    }
    if (expression->kind == CTOOL_C_EXPRESSION_IDENTIFIER &&
        expression->reference >= context->bindings.count) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
          cfront_peek(context), "frozen identifier reference is invalid");
    }
    if (expression->kind == CTOOL_C_EXPRESSION_PARAMETER &&
        expression->reference >= context->parameters.count) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
          cfront_peek(context), "frozen parameter reference is invalid");
    }
    if (expression->kind == CTOOL_C_EXPRESSION_BLOCK_BINDING &&
        expression->reference >= context->block_bindings.count) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
          cfront_peek(context),
          "frozen block-binding reference is invalid");
    }
    if (expression->kind == CTOOL_C_EXPRESSION_MEMBER &&
        expression->reference >= context->members.count) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
          cfront_peek(context), "frozen member reference is invalid");
    }
    if (expression->child_count != 0u) {
      if (expression->first_child > context->expression_children.count ||
          expression->child_count >
              context->expression_children.count - expression->first_child) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
            cfront_peek(context), "frozen expression child slice is invalid");
      }
      for (child_index = 0u; child_index < expression->child_count;
           child_index++) {
        if (expression_children[expression->first_child + child_index] >=
            index) {
          return cfront_emit_failure(
              context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
              cfront_peek(context),
              "frozen expressions are not in postorder");
        }
      }
    }
    switch (expression->kind) {
    case CTOOL_C_EXPRESSION_IDENTIFIER:
    case CTOOL_C_EXPRESSION_PARAMETER:
    case CTOOL_C_EXPRESSION_BLOCK_BINDING:
      if (expression->child_count != 0u ||
          expression->first_child != CTOOL_C_AST_NONE ||
          expression->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
          expression->conversion != CTOOL_C_CONVERSION_NONE ||
          expression->computation_type != CTOOL_C_TYPE_NONE) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
            cfront_peek(context), "frozen binding expression is invalid");
      }
      break;
    case CTOOL_C_EXPRESSION_STRING:
    case CTOOL_C_EXPRESSION_INTEGER_CONSTANT:
      if (expression->child_count != 0u ||
          expression->first_child != CTOOL_C_AST_NONE ||
          expression->reference != CTOOL_C_AST_NONE ||
          expression->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
          expression->conversion != CTOOL_C_CONVERSION_NONE ||
          expression->computation_type != CTOOL_C_TYPE_NONE) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
            cfront_peek(context), "frozen literal expression is invalid");
      }
      break;
    case CTOOL_C_EXPRESSION_CALL:
      if (expression->child_count == 0u ||
          expression->reference != CTOOL_C_AST_NONE ||
          expression->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
          expression->conversion != CTOOL_C_CONVERSION_NONE ||
          expression->computation_type != CTOOL_C_TYPE_NONE) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
            cfront_peek(context), "frozen call expression is invalid");
      }
      break;
    case CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION:
      if (expression->child_count != 1u ||
          expression->reference != CTOOL_C_AST_NONE ||
          expression->conversion == CTOOL_C_CONVERSION_NONE ||
          expression->conversion > CTOOL_C_CONVERSION_ASSIGNMENT ||
          expression->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
          expression->computation_type != CTOOL_C_TYPE_NONE) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
            cfront_peek(context), "frozen implicit conversion is invalid");
      }
      break;
    case CTOOL_C_EXPRESSION_UNARY:
      if (expression->child_count != 1u ||
          expression->reference != CTOOL_C_AST_NONE ||
          expression->operation <
              CTOOL_C_EXPRESSION_OPERATOR_UNARY_PLUS ||
          expression->operation >
              CTOOL_C_EXPRESSION_OPERATOR_DEREFERENCE ||
          expression->conversion != CTOOL_C_CONVERSION_NONE ||
          expression->computation_type != CTOOL_C_TYPE_NONE) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
            cfront_peek(context), "frozen unary expression is invalid");
      }
      break;
    case CTOOL_C_EXPRESSION_CAST:
      if (expression->child_count != 1u ||
          expression->reference != CTOOL_C_AST_NONE ||
          expression->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
          expression->conversion != CTOOL_C_CONVERSION_NONE ||
          expression->computation_type != CTOOL_C_TYPE_NONE) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
            cfront_peek(context), "frozen cast expression is invalid");
      }
      break;
    case CTOOL_C_EXPRESSION_MEMBER: {
      ctool_c_record_member_t member;
      ctool_c_type_node_t record;
      ctool_c_type_node_t member_node;
      ctool_c_type_node_t result_node;
      ctool_u32 child;
      ctool_u32 record_base;
      ctool_u32 record_qualifiers;
      ctool_u32 member_base;
      ctool_u32 member_qualifiers;
      ctool_u32 result_base;
      ctool_u32 result_qualifiers;
      if (expression->child_count != 1u ||
          expression->reference >= context->members.count ||
          expression->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
          expression->conversion != CTOOL_C_CONVERSION_NONE ||
          expression->computation_type != CTOOL_C_TYPE_NONE) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
            cfront_peek(context), "frozen member expression is invalid");
      }
      child = expression_children[expression->first_child];
      if (child >= index ||
          cfront_vector_get(&context->members, expression->reference,
                            &member) != CTOOL_OK ||
          cfront_underlying_type(context, expressions[child].type,
                                 &record_base, &record_qualifiers,
                                 &record) != CTOOL_OK ||
          cfront_underlying_type(context, member.type, &member_base,
                                 &member_qualifiers, &member_node) !=
              CTOOL_OK ||
          cfront_underlying_type(context, expression->type, &result_base,
                                 &result_qualifiers, &result_node) !=
              CTOOL_OK ||
          record.kind != CTOOL_C_TYPE_RECORD ||
          expression->reference < record.first_member ||
          expression->reference - record.first_member >=
              record.member_count ||
          result_base != member_base ||
          result_qualifiers !=
              (member_qualifiers | record_qualifiers | record.qualifiers)) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
            cfront_peek(context),
            "frozen member is not owned by its operand record");
      }
      break;
    }
    case CTOOL_C_EXPRESSION_BINARY:
      if (expression->child_count != 2u ||
          expression->reference != CTOOL_C_AST_NONE ||
          expression->operation < CTOOL_C_EXPRESSION_OPERATOR_MULTIPLY ||
          expression->operation > CTOOL_C_EXPRESSION_OPERATOR_LOGICAL_OR ||
          expression->conversion != CTOOL_C_CONVERSION_NONE ||
          expression->computation_type != CTOOL_C_TYPE_NONE) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
            cfront_peek(context), "frozen binary expression is invalid");
      }
      break;
    case CTOOL_C_EXPRESSION_ASSIGNMENT:
      if (expression->child_count != 2u ||
          expression->reference != CTOOL_C_AST_NONE ||
          expression->operation != CTOOL_C_EXPRESSION_OPERATOR_ASSIGN ||
          expression->conversion != CTOOL_C_CONVERSION_NONE ||
          expression->computation_type >= context->types.count ||
          expression->computation_type != expression->type) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
            cfront_peek(context), "frozen assignment expression is invalid");
      }
      break;
    default:
      return cfront_emit_failure(
          context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL,
          cfront_peek(context), "frozen expression kind is invalid");
    }
  }
  if (context->tape->token_count != 0u) {
    status = cfront_copy_location_owned(
        context, &context->tape->tokens[0].location, &unit->graph.location);
    if (status == CTOOL_OK) {
      status = cfront_copy_location_owned(
          context, &context->tape->tokens[0].physical_location,
          &unit->graph.physical_location);
    }
    if (status != CTOOL_OK) {
      return cfront_storage_failure(context, status);
    }
  }
  unit->graph.types = types;
  unit->graph.type_count = context->types.count;
  unit->graph.members = members;
  unit->graph.member_count = context->members.count;
  unit->graph.parameter_types = parameter_types;
  unit->graph.parameter_type_count = context->parameter_types.count;
  unit->bindings = bindings;
  unit->binding_count = context->bindings.count;
  unit->tags = tags;
  unit->tag_count = context->tags.count;
  unit->parameters = parameters;
  unit->parameter_count = context->parameters.count;
  unit->block_bindings = block_bindings;
  unit->block_binding_count = context->block_bindings.count;
  unit->function_definitions = function_definitions;
  unit->function_definition_count = context->function_definitions.count;
  unit->statements = statements;
  unit->statement_count = context->statements.count;
  unit->statement_children = statement_children;
  unit->statement_child_count = context->statement_children.count;
  unit->expressions = expressions;
  unit->expression_count = context->expressions.count;
  unit->expression_children = expression_children;
  unit->expression_child_count = context->expression_children.count;
  return CTOOL_OK;
}

static ctool_bool cfront_pack_valid(ctool_u32 value) {
  return value == 0u || value == 1u || value == 2u || value == 4u ||
                 value == 8u || value == 16u
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static ctool_status_t cfront_validate_input(cfront_context_t *context) {
  ctool_u32 index;
  if (context->request->mode != CTOOL_C_PP_MODE_C11 &&
      context->request->mode != CTOOL_C_PP_MODE_CUPID) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INVALID_ARGUMENT,
        CTOOL_C_PARSE_DIAG_INVALID_REQUEST, cfront_peek(context),
        "declaration frontend language mode is invalid");
  }
  if (cfront_bool_valid(context->request->gnu_extensions) == CTOOL_FALSE) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INVALID_ARGUMENT,
        CTOOL_C_PARSE_DIAG_INVALID_REQUEST, cfront_peek(context),
        "declaration frontend GNU-extension flag is invalid");
  }
  if (context->tape->tokens == (const ctool_c_pp_token_t *)0 &&
      context->tape->token_count != 0u) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INVALID_ARGUMENT,
        CTOOL_C_PARSE_DIAG_INVALID_REQUEST, cfront_peek(context),
        "declaration frontend token array is missing");
  }
  for (index = 0u; index < context->tape->token_count; index++) {
    const ctool_c_pp_token_t *token = &context->tape->tokens[index];
    if (token->kind < CTOOL_C_PP_TOKEN_IDENTIFIER ||
        token->kind > CTOOL_C_PP_TOKEN_CUPID_EXE ||
        token->spelling.data == (const char *)0 || token->spelling.size == 0u ||
        (token->location.path.data == (const char *)0 &&
         token->location.path.size != 0u) ||
        (token->physical_location.path.data == (const char *)0 &&
         token->physical_location.path.size != 0u) ||
        token->location.line == 0u || token->location.column == 0u ||
        token->physical_location.line == 0u ||
        token->physical_location.column == 0u ||
        cfront_pack_valid(token->pack_alignment) == CTOOL_FALSE) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INVALID_ARGUMENT,
          CTOOL_C_PARSE_DIAG_INVALID_REQUEST, token,
          "declaration frontend token metadata is invalid");
    }
  }
  return CTOOL_OK;
}

ctool_status_t ctool_c_parse(ctool_job_t *job,
                             const ctool_c_pp_result_t *tape,
                             const ctool_c_parse_request_t *request,
                             ctool_c_translation_unit_t *unit_out) {
  ctool_c_pp_result_t empty_tape;
  cfront_context_t context;
  ctool_arena_t *arena;
  ctool_arena_mark_t mark;
  ctool_u32 diagnostic_start;
  const char *stage = "request validation";
  ctool_status_t status;
  if (unit_out != (ctool_c_translation_unit_t *)0) {
    cfront_zero(unit_out, (ctool_u32)sizeof(*unit_out));
  }
  if (job == (ctool_job_t *)0 || unit_out == (ctool_c_translation_unit_t *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  cfront_zero(&empty_tape, (ctool_u32)sizeof(empty_tape));
  cfront_zero(&context, (ctool_u32)sizeof(context));
  context.job = job;
  context.tape = tape != (const ctool_c_pp_result_t *)0 ? tape : &empty_tape;
  context.request = request;
  arena = ctool_job_arena(job);
  mark = ctool_arena_mark(arena);
  diagnostic_start = ctool_job_diagnostic_count(job);
  if (tape == (const ctool_c_pp_result_t *)0 ||
      request == (const ctool_c_parse_request_t *)0) {
    status = cfront_emit_failure(
        &context, CTOOL_ERR_INVALID_ARGUMENT,
        CTOOL_C_PARSE_DIAG_INVALID_REQUEST, (const ctool_c_pp_token_t *)0,
        tape == (const ctool_c_pp_result_t *)0
            ? "declaration frontend tape is missing"
            : "declaration frontend request is missing");
  } else {
    ctool_u32 index;
    for (index = 0u; index <= CTOOL_C_TYPE_LONG_DOUBLE; index++) {
      context.scalar_types[index] = CFRONT_NONE;
    }
    status = cfront_validate_input(&context);
    if (status == CTOOL_OK) {
      stage = "scratch initialization";
      status = cfront_open_scratch(&context);
    }
    if (status != CTOOL_OK && context.types.versions.storage ==
                                   (ctool_buffer_t *)0) {
      if (status != CTOOL_ERR_INPUT && status != CTOOL_ERR_INVALID_ARGUMENT) {
        status = cfront_storage_failure(&context, status);
      }
    } else {
      stage = "declaration parsing";
      while (status == CTOOL_OK && context.position < tape->token_count) {
        status = cfront_parse_external_declaration(&context);
      }
      if (status == CTOOL_OK) {
        stage = "semantic graph freeze";
        status = cfront_freeze(&context, unit_out);
      }
      cfront_close_scratch(&context);
      if (status == CTOOL_OK) {
        stage = "i386 type layout";
        status = ctool_c_layout_types(job, &unit_out->graph,
                                      &unit_out->layout);
      }
    }
  }
  if (status != CTOOL_OK) {
    ctool_status_t rewind_status;
    if (ctool_job_diagnostic_count(job) == diagnostic_start) {
      status = cfront_emit_failure(
          &context, status, CTOOL_C_PARSE_DIAG_INTERNAL,
          cfront_peek(&context), stage);
    }
    cfront_close_scratch(&context);
    rewind_status = ctool_arena_rewind(arena, mark);
    cfront_zero(unit_out, (ctool_u32)sizeof(*unit_out));
    return rewind_status == CTOOL_OK ? status : rewind_status;
  }
  return CTOOL_OK;
}

static ctool_status_t cfront_parse_specifiers(cfront_context_t *context,
                                               cfront_specifiers_t *spec_out) {
  const ctool_c_pp_token_t *first = cfront_peek(context);
  ctool_u32 qualifiers = 0u;
  ctool_u32 typedef_type = CFRONT_NONE;
  ctool_u32 long_count = 0u;
  ctool_bool saw_any = CTOOL_FALSE;
  ctool_bool saw_base = CTOOL_FALSE;
  ctool_bool saw_scalar = CTOOL_FALSE;
  ctool_bool saw_void = CTOOL_FALSE;
  ctool_bool saw_bool = CTOOL_FALSE;
  ctool_bool saw_char = CTOOL_FALSE;
  ctool_bool saw_short = CTOOL_FALSE;
  ctool_bool saw_int = CTOOL_FALSE;
  ctool_bool saw_float = CTOOL_FALSE;
  ctool_bool saw_double = CTOOL_FALSE;
  ctool_bool saw_signed = CTOOL_FALSE;
  ctool_bool saw_unsigned = CTOOL_FALSE;
  ctool_status_t status;
  cfront_zero(spec_out, (ctool_u32)sizeof(*spec_out));
  spec_out->storage = CFRONT_STORAGE_NONE;
  spec_out->type = CFRONT_NONE;
  if (first != (const ctool_c_pp_token_t *)0) {
    spec_out->location = first->location;
    spec_out->physical_location = first->physical_location;
    spec_out->pack_alignment = first->pack_alignment;
  }
  for (;;) {
    const ctool_c_pp_token_t *token = cfront_peek(context);
    cfront_storage_t storage = CFRONT_STORAGE_NONE;
    ctool_u32 qualifier = 0u;
    ctool_u32 found_typedef;
    if (cfront_starts_attribute(context) == CTOOL_TRUE) {
      status = cfront_parse_attributes(context, &spec_out->attributes);
      if (status != CTOOL_OK) {
        return status;
      }
      continue;
    }
    if (cfront_token_is(token, "typedef") == CTOOL_TRUE) {
      storage = CFRONT_STORAGE_TYPEDEF;
    } else if (cfront_token_is(token, "extern") == CTOOL_TRUE) {
      storage = CFRONT_STORAGE_EXTERN;
    } else if (cfront_token_is(token, "static") == CTOOL_TRUE) {
      storage = CFRONT_STORAGE_STATIC;
    } else if (cfront_token_is(token, "auto") == CTOOL_TRUE) {
      storage = CFRONT_STORAGE_AUTO;
    } else if (cfront_token_is(token, "register") == CTOOL_TRUE) {
      storage = CFRONT_STORAGE_REGISTER;
    }
    if (storage != CFRONT_STORAGE_NONE) {
      if (spec_out->storage != CFRONT_STORAGE_NONE) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT,
            CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS, token,
            "declaration repeats or conflicts with a storage class");
      }
      spec_out->storage = storage;
      saw_any = CTOOL_TRUE;
      (void)cfront_advance(context);
      continue;
    }
    if (cfront_token_is(token, "inline") == CTOOL_TRUE) {
      spec_out->function_declaration_flags |= CTOOL_C_FUNCTION_DECL_INLINE;
      if (spec_out->inline_token == (const ctool_c_pp_token_t *)0) {
        spec_out->inline_token = token;
      }
      saw_any = CTOOL_TRUE;
      (void)cfront_advance(context);
      continue;
    }
    if (cfront_token_is(token, "const") == CTOOL_TRUE) {
      qualifier = CTOOL_C_QUAL_CONST;
    } else if (cfront_token_is(token, "volatile") == CTOOL_TRUE) {
      qualifier = CTOOL_C_QUAL_VOLATILE;
    } else if (cfront_token_is(token, "restrict") == CTOOL_TRUE) {
      qualifier = CTOOL_C_QUAL_RESTRICT;
    } else if (cfront_token_is(token, "_Atomic") == CTOOL_TRUE) {
      qualifier = CTOOL_C_QUAL_ATOMIC;
    }
    if (qualifier != 0u) {
      qualifiers |= qualifier;
      saw_any = CTOOL_TRUE;
      (void)cfront_advance(context);
      continue;
    }
    if (cfront_token_is(token, "void") == CTOOL_TRUE) {
      if (saw_void == CTOOL_TRUE) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT,
            CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS, token,
            "declaration repeats a scalar type specifier");
      }
      saw_void = CTOOL_TRUE;
    } else if (cfront_token_is(token, "_Bool") == CTOOL_TRUE) {
      if (saw_bool == CTOOL_TRUE) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT,
            CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS, token,
            "declaration repeats a scalar type specifier");
      }
      saw_bool = CTOOL_TRUE;
    } else if (cfront_token_is(token, "char") == CTOOL_TRUE) {
      if (saw_char == CTOOL_TRUE) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT,
            CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS, token,
            "declaration repeats a scalar type specifier");
      }
      saw_char = CTOOL_TRUE;
    } else if (cfront_token_is(token, "short") == CTOOL_TRUE) {
      if (saw_short == CTOOL_TRUE) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT,
            CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS, token,
            "declaration repeats a scalar type specifier");
      }
      saw_short = CTOOL_TRUE;
    } else if (cfront_token_is(token, "int") == CTOOL_TRUE) {
      if (saw_int == CTOOL_TRUE) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT,
            CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS, token,
            "declaration repeats a scalar type specifier");
      }
      saw_int = CTOOL_TRUE;
    } else if (cfront_token_is(token, "long") == CTOOL_TRUE) {
      long_count++;
      if (long_count > 2u) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT,
            CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS, token,
            "declaration has too many long specifiers");
      }
    } else if (cfront_token_is(token, "float") == CTOOL_TRUE) {
      if (saw_float == CTOOL_TRUE) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT,
            CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS, token,
            "declaration repeats a scalar type specifier");
      }
      saw_float = CTOOL_TRUE;
    } else if (cfront_token_is(token, "double") == CTOOL_TRUE) {
      if (saw_double == CTOOL_TRUE) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT,
            CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS, token,
            "declaration repeats a scalar type specifier");
      }
      saw_double = CTOOL_TRUE;
    } else if (cfront_token_is(token, "signed") == CTOOL_TRUE) {
      if (saw_signed == CTOOL_TRUE) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT,
            CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS, token,
            "declaration repeats a scalar type specifier");
      }
      saw_signed = CTOOL_TRUE;
    } else if (cfront_token_is(token, "unsigned") == CTOOL_TRUE) {
      if (saw_unsigned == CTOOL_TRUE) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT,
            CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS, token,
            "declaration repeats a scalar type specifier");
      }
      saw_unsigned = CTOOL_TRUE;
    } else if (cfront_token_is(token, "struct") == CTOOL_TRUE ||
               cfront_token_is(token, "union") == CTOOL_TRUE ||
               (context->request->mode == CTOOL_C_PP_MODE_CUPID &&
                cfront_token_is(token, "class") == CTOOL_TRUE)) {
      ctool_c_record_kind_t record_kind =
          cfront_token_is(token, "union") == CTOOL_TRUE
              ? CTOOL_C_RECORD_UNION
              : (cfront_token_is(token, "class") == CTOOL_TRUE
                     ? CTOOL_C_RECORD_CLASS
                     : CTOOL_C_RECORD_STRUCT);
      if (saw_base == CTOOL_TRUE || saw_scalar == CTOOL_TRUE ||
          typedef_type != CFRONT_NONE) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT,
            CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS, token,
            "declaration has more than one base type");
      }
      spec_out->block_tag_specifier_token = token;
      status = cfront_record_type(
          context, record_kind, &typedef_type,
          &spec_out->anonymous_record_definition);
      if (status != CTOOL_OK) {
        return status;
      }
      saw_any = CTOOL_TRUE;
      saw_base = CTOOL_TRUE;
      spec_out->empty_declaration_valid =
          spec_out->anonymous_record_definition == CTOOL_TRUE ? CTOOL_FALSE
                                                               : CTOOL_TRUE;
      continue;
    } else if (cfront_token_is(token, "enum") == CTOOL_TRUE) {
      if (saw_base == CTOOL_TRUE || saw_scalar == CTOOL_TRUE ||
          typedef_type != CFRONT_NONE) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT,
            CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS, token,
            "declaration has more than one base type");
      }
      spec_out->block_tag_specifier_token = token;
      {
        ctool_bool anonymous_enum_definition = CTOOL_FALSE;
        status = cfront_enum_type(context, &typedef_type,
                                  &anonymous_enum_definition);
      }
      if (status != CTOOL_OK) {
        return status;
      }
      saw_any = CTOOL_TRUE;
      saw_base = CTOOL_TRUE;
      spec_out->empty_declaration_valid = CTOOL_TRUE;
      continue;
    } else if (token != (const ctool_c_pp_token_t *)0 &&
               token->kind == CTOOL_C_PP_TOKEN_IDENTIFIER &&
               saw_base == CTOOL_FALSE && typedef_type == CFRONT_NONE &&
               cfront_find_typedef(context, token->spelling,
                                   &found_typedef) == CTOOL_TRUE) {
      typedef_type = found_typedef;
      saw_any = CTOOL_TRUE;
      saw_base = CTOOL_TRUE;
      (void)cfront_advance(context);
      continue;
    } else {
      break;
    }
    if (typedef_type != CFRONT_NONE ||
        (saw_base == CTOOL_TRUE && saw_scalar == CTOOL_FALSE)) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INPUT,
          CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS, token,
          "declaration combines a typedef or tag with another base type");
    }
    saw_any = CTOOL_TRUE;
    saw_base = CTOOL_TRUE;
    saw_scalar = CTOOL_TRUE;
    (void)cfront_advance(context);
  }
  if (saw_any == CTOOL_FALSE ||
      (saw_base == CTOOL_FALSE && typedef_type == CFRONT_NONE)) {
    return cfront_emit_failure(
        context, CTOOL_ERR_INPUT,
        CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS, first,
        "declaration specifiers do not name a type");
  }
  if (typedef_type == CFRONT_NONE) {
    ctool_c_type_kind_t kind = CTOOL_C_TYPE_SIGNED_INT;
    if (saw_signed == CTOOL_TRUE && saw_unsigned == CTOOL_TRUE) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INPUT,
          CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS, first,
          "declaration cannot be both signed and unsigned");
    }
    if ((saw_void == CTOOL_TRUE &&
         (saw_bool == CTOOL_TRUE || saw_char == CTOOL_TRUE ||
          saw_short == CTOOL_TRUE || saw_int == CTOOL_TRUE ||
          saw_float == CTOOL_TRUE || saw_double == CTOOL_TRUE ||
          saw_signed == CTOOL_TRUE || saw_unsigned == CTOOL_TRUE ||
          long_count != 0u)) ||
        (saw_bool == CTOOL_TRUE &&
         (saw_char == CTOOL_TRUE || saw_short == CTOOL_TRUE ||
          saw_int == CTOOL_TRUE || saw_float == CTOOL_TRUE ||
          saw_double == CTOOL_TRUE || saw_signed == CTOOL_TRUE ||
          saw_unsigned == CTOOL_TRUE || long_count != 0u)) ||
        (saw_char == CTOOL_TRUE &&
         (saw_short == CTOOL_TRUE || saw_int == CTOOL_TRUE ||
          saw_float == CTOOL_TRUE || saw_double == CTOOL_TRUE ||
          long_count != 0u)) ||
        (saw_short == CTOOL_TRUE &&
         (saw_char == CTOOL_TRUE || saw_float == CTOOL_TRUE ||
          saw_double == CTOOL_TRUE || long_count != 0u)) ||
        (saw_float == CTOOL_TRUE &&
         (saw_int == CTOOL_TRUE || saw_double == CTOOL_TRUE ||
          saw_signed == CTOOL_TRUE || saw_unsigned == CTOOL_TRUE ||
          long_count != 0u)) ||
        (saw_double == CTOOL_TRUE &&
         (saw_int == CTOOL_TRUE || saw_signed == CTOOL_TRUE ||
          saw_unsigned == CTOOL_TRUE || long_count > 1u))) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INPUT,
          CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS, first,
          "declaration has an invalid scalar specifier combination");
    }
    if (saw_void == CTOOL_TRUE) {
      kind = CTOOL_C_TYPE_VOID;
    } else if (saw_bool == CTOOL_TRUE) {
      kind = CTOOL_C_TYPE_BOOL;
    } else if (saw_char == CTOOL_TRUE) {
      kind = saw_unsigned == CTOOL_TRUE
                 ? CTOOL_C_TYPE_UNSIGNED_CHAR
                 : (saw_signed == CTOOL_TRUE ? CTOOL_C_TYPE_SIGNED_CHAR
                                             : CTOOL_C_TYPE_CHAR);
    } else if (saw_short == CTOOL_TRUE) {
      kind = saw_unsigned == CTOOL_TRUE ? CTOOL_C_TYPE_UNSIGNED_SHORT
                                        : CTOOL_C_TYPE_SIGNED_SHORT;
    } else if (saw_float == CTOOL_TRUE) {
      kind = CTOOL_C_TYPE_FLOAT;
    } else if (saw_double == CTOOL_TRUE) {
      kind = long_count == 1u ? CTOOL_C_TYPE_LONG_DOUBLE
                              : CTOOL_C_TYPE_DOUBLE;
    } else if (long_count == 2u) {
      kind = saw_unsigned == CTOOL_TRUE
                 ? CTOOL_C_TYPE_UNSIGNED_LONG_LONG
                 : CTOOL_C_TYPE_SIGNED_LONG_LONG;
    } else if (long_count == 1u) {
      kind = saw_unsigned == CTOOL_TRUE ? CTOOL_C_TYPE_UNSIGNED_LONG
                                        : CTOOL_C_TYPE_SIGNED_LONG;
    } else {
      kind = saw_unsigned == CTOOL_TRUE ? CTOOL_C_TYPE_UNSIGNED_INT
                                        : CTOOL_C_TYPE_SIGNED_INT;
    }
    status = cfront_scalar_type(context, kind, first, &typedef_type);
    if (status != CTOOL_OK) {
      return cfront_storage_failure(context, status);
    }
  }
  {
    ctool_u32 underlying;
    ctool_u32 inherited_qualifiers;
    ctool_c_type_node_t underlying_node;
    status = cfront_underlying_type(context, typedef_type, &underlying,
                                    &inherited_qualifiers,
                                    &underlying_node);
    (void)underlying;
    (void)inherited_qualifiers;
    if (status != CTOOL_OK) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL, first,
          "declaration base type is unavailable");
    }
    if (underlying_node.kind == CTOOL_C_TYPE_FUNCTION && qualifiers != 0u) {
      return cfront_emit_failure(
          context, CTOOL_ERR_INPUT,
          CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS, first,
          "function type cannot be qualified");
    }
    if ((qualifiers & CTOOL_C_QUAL_RESTRICT) != 0u) {
      ctool_u32 referenced;
      ctool_u32 referenced_qualifiers;
      ctool_c_type_node_t referenced_node;
      if (underlying_node.kind != CTOOL_C_TYPE_POINTER) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT,
            CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS, first,
            "restrict qualifier requires a pointer type");
      }
      status = cfront_underlying_type(
          context, underlying_node.referenced_type, &referenced,
          &referenced_qualifiers, &referenced_node);
      (void)referenced;
      (void)referenced_qualifiers;
      if (status != CTOOL_OK) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INTERNAL, CTOOL_C_PARSE_DIAG_INTERNAL, first,
            "restrict-qualified referent is unavailable");
      }
      if (referenced_node.kind == CTOOL_C_TYPE_FUNCTION) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT,
            CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS, first,
            "restrict qualifier cannot apply to a function pointer");
      }
    }
  }
  status = cfront_qualified_type(context, typedef_type, qualifiers, first,
                                 &spec_out->type);
  if (status != CTOOL_OK) {
    return cfront_storage_failure(context, status);
  }
  return CTOOL_OK;
}

static ctool_status_t cfront_parse_constant_add(
    cfront_context_t *context, cfront_integer_t *value_out) {
  value_out->bits = 0ull;
  value_out->kind = CFRONT_INTEGER_SIGNED_32;
  ctool_status_t status = cfront_parse_constant_multiply(context, value_out);
  while (status == CTOOL_OK &&
         (cfront_peek_is(context, "+") == CTOOL_TRUE ||
          cfront_peek_is(context, "-") == CTOOL_TRUE)) {
    const ctool_c_pp_token_t *operator_token = cfront_advance(context);
    ctool_bool subtract = cfront_token_is(operator_token, "-");
    cfront_integer_t right = {0ull, CFRONT_INTEGER_SIGNED_32};
    status = cfront_parse_constant_multiply(context, &right);
    if (status == CTOOL_OK) {
      cfront_integer_kind_t kind =
          cfront_integer_usual_kind(value_out->kind, right.kind);
      cfront_integer_t left = cfront_integer_convert(*value_out, kind);
      cfront_integer_t converted_right = cfront_integer_convert(right, kind);
      ctool_u64 bits = subtract == CTOOL_TRUE
                           ? left.bits - converted_right.bits
                           : left.bits + converted_right.bits;
      bits = cfront_integer_normalize_bits(bits, kind);
      if (cfront_integer_unsigned(kind) == CTOOL_FALSE) {
        cfront_integer_t result = {bits, kind};
        ctool_bool left_negative = cfront_integer_negative(&left);
        ctool_bool right_negative =
            cfront_integer_negative(&converted_right);
        ctool_bool result_negative = cfront_integer_negative(&result);
        if ((subtract == CTOOL_FALSE &&
             left_negative == right_negative &&
             result_negative != left_negative) ||
            (subtract == CTOOL_TRUE &&
             left_negative != right_negative &&
             result_negative != left_negative)) {
          if (context->constant_evaluation_suppression_depth == 0u) {
            return cfront_integer_overflow(context, operator_token);
          }
        }
      }
      value_out->kind = kind;
      value_out->bits = bits;
    }
  }
  return status;
}

static ctool_status_t cfront_parse_constant_shift(
    cfront_context_t *context, cfront_integer_t *value_out) {
  value_out->bits = 0ull;
  value_out->kind = CFRONT_INTEGER_SIGNED_32;
  ctool_status_t status = cfront_parse_constant_add(context, value_out);
  while (status == CTOOL_OK &&
         (cfront_peek_is(context, "<<") == CTOOL_TRUE ||
          cfront_peek_is(context, ">>") == CTOOL_TRUE)) {
    ctool_bool left = cfront_peek_is(context, "<<");
    const ctool_c_pp_token_t *operator_token = cfront_advance(context);
    cfront_integer_t right = {0ull, CFRONT_INTEGER_SIGNED_32};
    status = cfront_parse_constant_add(context, &right);
    if (status != CTOOL_OK) {
      break;
    }
    if (cfront_integer_negative(&right) == CTOOL_TRUE ||
        right.bits >= (ctool_u64)cfront_integer_width(value_out->kind)) {
      if (context->constant_evaluation_suppression_depth == 0u) {
        return cfront_emit_failure(
            context, CTOOL_ERR_INPUT,
            CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION, operator_token,
            "integer constant shift count is outside the left operand width");
      }
      value_out->bits = 0ull;
      continue;
    }
    if (left == CTOOL_TRUE) {
      ctool_u32 count = (ctool_u32)right.bits;
      if (cfront_integer_unsigned(value_out->kind) == CTOOL_FALSE) {
        ctool_u64 maximum =
            cfront_integer_width(value_out->kind) == 32u
                ? 0x7fffffffull
                : 0x7fffffffffffffffull;
        if (cfront_integer_negative(value_out) == CTOOL_TRUE) {
          if (context->constant_evaluation_suppression_depth == 0u) {
            return cfront_emit_failure(
                context, CTOOL_ERR_INPUT,
                CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION, operator_token,
                "signed left shift requires a nonnegative left operand");
          }
          value_out->bits = 0ull;
          continue;
        }
        if (value_out->bits > (maximum >> count) &&
            context->constant_evaluation_suppression_depth == 0u) {
          return cfront_integer_overflow(context, operator_token);
        }
      }
      value_out->bits = cfront_integer_normalize_bits(
          value_out->bits << count, value_out->kind);
    } else if (cfront_integer_negative(value_out) == CTOOL_TRUE) {
      ctool_u32 count = (ctool_u32)right.bits;
      if (count != 0u) {
        ctool_u64 mask = cfront_integer_mask(value_out->kind);
        ctool_u64 low = value_out->bits & mask;
        value_out->bits =
            cfront_integer_normalize_bits((low >> count) |
                                              (mask ^ (mask >> count)),
                                          value_out->kind);
      }
    } else {
      value_out->bits >>= (ctool_u32)right.bits;
    }
  }
  return status;
}

static ctool_bool cfront_integer_less_than(cfront_integer_t left,
                                           cfront_integer_t right) {
  if (cfront_integer_unsigned(left.kind) == CTOOL_TRUE) {
    return left.bits < right.bits ? CTOOL_TRUE : CTOOL_FALSE;
  }
  {
    ctool_bool left_negative = cfront_integer_negative(&left);
    ctool_bool right_negative = cfront_integer_negative(&right);
    if (left_negative != right_negative) {
      return left_negative;
    }
    if (left_negative == CTOOL_TRUE) {
      return cfront_integer_magnitude(&left) >
                     cfront_integer_magnitude(&right)
                 ? CTOOL_TRUE
                 : CTOOL_FALSE;
    }
  }
  return left.bits < right.bits ? CTOOL_TRUE : CTOOL_FALSE;
}

static ctool_status_t cfront_parse_constant_relational(
    cfront_context_t *context, cfront_integer_t *value_out) {
  ctool_status_t status = cfront_parse_constant_shift(context, value_out);
  while (status == CTOOL_OK &&
         (cfront_peek_is(context, "<") == CTOOL_TRUE ||
          cfront_peek_is(context, ">") == CTOOL_TRUE ||
          cfront_peek_is(context, "<=") == CTOOL_TRUE ||
          cfront_peek_is(context, ">=") == CTOOL_TRUE)) {
    const ctool_c_pp_token_t *operator_token = cfront_advance(context);
    cfront_integer_t right = {0ull, CFRONT_INTEGER_SIGNED_32};
    cfront_integer_kind_t kind;
    cfront_integer_t left;
    ctool_bool result;
    status = cfront_parse_constant_shift(context, &right);
    if (status != CTOOL_OK) {
      break;
    }
    kind = cfront_integer_usual_kind(value_out->kind, right.kind);
    left = cfront_integer_convert(*value_out, kind);
    right = cfront_integer_convert(right, kind);
    if (cfront_token_is(operator_token, "<") == CTOOL_TRUE) {
      result = cfront_integer_less_than(left, right);
    } else if (cfront_token_is(operator_token, ">") == CTOOL_TRUE) {
      result = cfront_integer_less_than(right, left);
    } else if (cfront_token_is(operator_token, "<=") == CTOOL_TRUE) {
      result = cfront_integer_less_than(right, left) == CTOOL_TRUE
                   ? CTOOL_FALSE
                   : CTOOL_TRUE;
    } else {
      result = cfront_integer_less_than(left, right) == CTOOL_TRUE
                   ? CTOOL_FALSE
                   : CTOOL_TRUE;
    }
    value_out->bits = result == CTOOL_TRUE ? 1ull : 0ull;
    value_out->kind = CFRONT_INTEGER_SIGNED_32;
  }
  return status;
}

static ctool_status_t cfront_parse_constant_equality(
    cfront_context_t *context, cfront_integer_t *value_out) {
  ctool_status_t status = cfront_parse_constant_relational(context, value_out);
  while (status == CTOOL_OK &&
         (cfront_peek_is(context, "==") == CTOOL_TRUE ||
          cfront_peek_is(context, "!=") == CTOOL_TRUE)) {
    ctool_bool equal = cfront_peek_is(context, "==");
    cfront_integer_t right = {0ull, CFRONT_INTEGER_SIGNED_32};
    cfront_integer_kind_t kind;
    cfront_integer_t left;
    (void)cfront_advance(context);
    status = cfront_parse_constant_relational(context, &right);
    if (status != CTOOL_OK) {
      break;
    }
    kind = cfront_integer_usual_kind(value_out->kind, right.kind);
    left = cfront_integer_convert(*value_out, kind);
    right = cfront_integer_convert(right, kind);
    value_out->bits = (left.bits == right.bits) == (equal == CTOOL_TRUE)
                          ? 1ull
                          : 0ull;
    value_out->kind = CFRONT_INTEGER_SIGNED_32;
  }
  return status;
}

static ctool_status_t cfront_parse_constant_and(
    cfront_context_t *context, cfront_integer_t *value_out) {
  value_out->bits = 0ull;
  value_out->kind = CFRONT_INTEGER_SIGNED_32;
  ctool_status_t status = cfront_parse_constant_equality(context, value_out);
  while (status == CTOOL_OK && cfront_peek_is(context, "&") == CTOOL_TRUE) {
    cfront_integer_t right = {0ull, CFRONT_INTEGER_SIGNED_32};
    (void)cfront_advance(context);
    status = cfront_parse_constant_equality(context, &right);
    if (status == CTOOL_OK) {
      cfront_integer_kind_t kind =
          cfront_integer_usual_kind(value_out->kind, right.kind);
      cfront_integer_t left = cfront_integer_convert(*value_out, kind);
      right = cfront_integer_convert(right, kind);
      value_out->kind = kind;
      value_out->bits =
          cfront_integer_normalize_bits(left.bits & right.bits, kind);
    }
  }
  return status;
}

static ctool_status_t cfront_parse_constant_xor(
    cfront_context_t *context, cfront_integer_t *value_out) {
  value_out->bits = 0ull;
  value_out->kind = CFRONT_INTEGER_SIGNED_32;
  ctool_status_t status = cfront_parse_constant_and(context, value_out);
  while (status == CTOOL_OK && cfront_peek_is(context, "^") == CTOOL_TRUE) {
    cfront_integer_t right = {0ull, CFRONT_INTEGER_SIGNED_32};
    (void)cfront_advance(context);
    status = cfront_parse_constant_and(context, &right);
    if (status == CTOOL_OK) {
      cfront_integer_kind_t kind =
          cfront_integer_usual_kind(value_out->kind, right.kind);
      cfront_integer_t left = cfront_integer_convert(*value_out, kind);
      right = cfront_integer_convert(right, kind);
      value_out->kind = kind;
      value_out->bits =
          cfront_integer_normalize_bits(left.bits ^ right.bits, kind);
    }
  }
  return status;
}

static ctool_status_t cfront_parse_constant_bitwise_or(
    cfront_context_t *context, cfront_integer_t *value_out) {
  value_out->bits = 0ull;
  value_out->kind = CFRONT_INTEGER_SIGNED_32;
  ctool_status_t status = cfront_parse_constant_xor(context, value_out);
  while (status == CTOOL_OK && cfront_peek_is(context, "|") == CTOOL_TRUE) {
    cfront_integer_t right = {0ull, CFRONT_INTEGER_SIGNED_32};
    (void)cfront_advance(context);
    status = cfront_parse_constant_xor(context, &right);
    if (status == CTOOL_OK) {
      cfront_integer_kind_t kind =
          cfront_integer_usual_kind(value_out->kind, right.kind);
      cfront_integer_t left = cfront_integer_convert(*value_out, kind);
      right = cfront_integer_convert(right, kind);
      value_out->kind = kind;
      value_out->bits =
          cfront_integer_normalize_bits(left.bits | right.bits, kind);
    }
  }
  return status;
}

static ctool_status_t cfront_parse_constant_logical_and(
    cfront_context_t *context, cfront_integer_t *value_out) {
  ctool_status_t status =
      cfront_parse_constant_bitwise_or(context, value_out);
  while (status == CTOOL_OK &&
         cfront_peek_is(context, "&&") == CTOOL_TRUE) {
    cfront_integer_t right = {0ull, CFRONT_INTEGER_SIGNED_32};
    ctool_bool suppress_right =
        context->constant_evaluation_suppression_depth != 0u ||
                value_out->bits == 0ull
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    (void)cfront_advance(context);
    if (suppress_right == CTOOL_TRUE) {
      context->constant_evaluation_suppression_depth++;
    }
    status = cfront_parse_constant_bitwise_or(context, &right);
    if (suppress_right == CTOOL_TRUE) {
      context->constant_evaluation_suppression_depth--;
    }
    if (status == CTOOL_OK) {
      value_out->bits =
          value_out->bits != 0ull && right.bits != 0ull ? 1ull : 0ull;
      value_out->kind = CFRONT_INTEGER_SIGNED_32;
    }
  }
  return status;
}

static ctool_status_t cfront_parse_constant_logical_or(
    cfront_context_t *context, cfront_integer_t *value_out) {
  ctool_status_t status =
      cfront_parse_constant_logical_and(context, value_out);
  while (status == CTOOL_OK &&
         cfront_peek_is(context, "||") == CTOOL_TRUE) {
    cfront_integer_t right = {0ull, CFRONT_INTEGER_SIGNED_32};
    ctool_bool suppress_right =
        context->constant_evaluation_suppression_depth != 0u ||
                value_out->bits != 0ull
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    (void)cfront_advance(context);
    if (suppress_right == CTOOL_TRUE) {
      context->constant_evaluation_suppression_depth++;
    }
    status = cfront_parse_constant_logical_and(context, &right);
    if (suppress_right == CTOOL_TRUE) {
      context->constant_evaluation_suppression_depth--;
    }
    if (status == CTOOL_OK) {
      value_out->bits =
          value_out->bits != 0ull || right.bits != 0ull ? 1ull : 0ull;
      value_out->kind = CFRONT_INTEGER_SIGNED_32;
    }
  }
  if (status == CTOOL_OK && cfront_peek_is(context, "?") == CTOOL_TRUE) {
    return cfront_emit_failure(
        context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_UNSUPPORTED,
        cfront_peek(context),
        "conditional constant operators are outside this slice");
  }
  return status;
}
