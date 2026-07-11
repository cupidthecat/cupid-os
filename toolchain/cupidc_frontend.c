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
  ctool_u32 type;
  cfront_storage_t storage;
  ctool_u32 pack_alignment;
  ctool_c_pp_location_t location;
  ctool_c_pp_location_t physical_location;
  ctool_bool anonymous_record_definition;
  ctool_bool empty_declaration_valid;
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
  ctool_u32 syntax_depth;
  ctool_u32 prototype_scope_depth;
  ctool_u32 prototype_binding_mark;
  ctool_u32 prototype_tag_mark;
  ctool_u32 prototype_name_mark;
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

static ctool_status_t cfront_emit_failure(
    cfront_context_t *context, ctool_status_t status, ctool_u32 code,
    const ctool_c_pp_token_t *token, const char *message) {
  const ctool_c_pp_location_t *location;
  ctool_diagnostic_t diagnostic;
  ctool_status_t emitted;
  if (status == CTOOL_ERR_INPUT &&
      token != (const ctool_c_pp_token_t *)0 &&
      token->kind == CTOOL_C_PP_TOKEN_CUPID_EXE) {
    status = CTOOL_ERR_UNSUPPORTED;
    code = CTOOL_C_PARSE_DIAG_UNSUPPORTED;
    message = "Cupid #exe execution is outside the declaration frontend";
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
  diagnostic.message = ctool_string(message);
  emitted = ctool_job_emit(context->job, &diagnostic);
  return emitted == CTOOL_OK ? status : emitted;
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
    ctool_u32 attributes, ctool_u32 minimum_alignment,
    const ctool_c_pp_token_t *token,
    const ctool_c_pp_location_t *location,
    const ctool_c_pp_location_t *physical_location, ctool_u64 integer_bits,
    ctool_bool integer_unsigned) {
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
             (attributes & ~existing.attributes) != 0u ||
             minimum_alignment > existing.minimum_alignment)) {
          ctool_c_binding_t replacement = existing;
          replacement.type = composite;
          replacement.attributes |= attributes;
          if (minimum_alignment > replacement.minimum_alignment) {
            replacement.minimum_alignment = minimum_alignment;
          }
          status = cfront_vector_replace(
              context, &context->bindings, existing_index, &replacement);
        }
      }
      if (status == CTOOL_OK && compatible == CTOOL_TRUE) {
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
  binding.attributes = attributes;
  binding.minimum_alignment = minimum_alignment;
  binding.type = type;
  if (location != (const ctool_c_pp_location_t *)0 &&
      physical_location != (const ctool_c_pp_location_t *)0) {
    binding.location = *location;
    binding.physical_location = *physical_location;
  }
  binding.integer_bits = integer_bits;
  binding.integer_unsigned = integer_unsigned;
  return cfront_vector_append(&context->bindings, &binding,
                              (ctool_u32 *)0);
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
    ctool_u32 index, ctool_bool *unsigned_out, ctool_u32 *long_count_out) {
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
        context, CTOOL_ERR_INPUT,
        CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION, token,
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
        context, CTOOL_ERR_INPUT,
        CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION, token,
        "integer constant suffix is invalid");
  }
  *long_count_out = count;
  return CTOOL_OK;
}

static ctool_status_t cfront_parse_number_token(
    cfront_context_t *context, const ctool_c_pp_token_t *token,
    cfront_integer_t *value_out) {
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
        context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION,
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
        context, CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION,
        token, "integer constant has no digits");
  }
  status = cfront_parse_integer_suffix(context, token, index,
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
  return CTOOL_OK;
}

static ctool_status_t cfront_parse_constant_or(
    cfront_context_t *context, cfront_integer_t *value_out);

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
    status = cfront_parse_constant_or(context, value_out);
    if (status == CTOOL_OK) {
      status = cfront_expected(context, ")");
    }
    cfront_leave_syntax(context);
    return status;
  }
  if (token->kind == CTOOL_C_PP_TOKEN_NUMBER) {
    (void)cfront_advance(context);
    return cfront_parse_number_token(context, token, value_out);
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
                   : 0x8000000000000000ull)) {
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
              right_magnitude > limit / left_magnitude) {
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
          return cfront_emit_failure(
              context, CTOOL_ERR_INPUT,
              CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION, operator_token,
              "integer constant expression divides by zero");
        }
        if (cfront_integer_unsigned(kind) == CTOOL_TRUE) {
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
              divisor == 1ull) {
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
            if (quotient > limit) {
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
            status = cfront_parse_constant_or(context, &alignment);
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
    const ctool_c_pp_token_t *token, ctool_u32 *adjusted_out) {
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
    status = cfront_unqualified_parameter_type(context, adjusted,
                                               adjusted_out);
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
    status = cfront_adjust_parameter_type(context, declared_type,
                                          parameter_token, &adjusted_type);
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
        status = cfront_parse_constant_or(context, &count);
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
  ctool_status_t status = cfront_parse_specifiers(context, &specifiers);
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
      status = cfront_parse_constant_or(context, &width);
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

static ctool_status_t cfront_parse_external_declaration(
    cfront_context_t *context) {
  const ctool_c_pp_token_t *declaration_token = cfront_peek(context);
  cfront_specifiers_t specifiers;
  ctool_status_t status;
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
  for (;;) {
    ctool_u32 root = CFRONT_NONE;
    ctool_u32 type = CFRONT_NONE;
    ctool_string_t name = ctool_string("");
    ctool_c_pp_location_t location;
    ctool_c_pp_location_t physical_location;
    ctool_c_binding_kind_t kind;
    ctool_c_type_node_t type_node;
    ctool_u32 type_base;
    ctool_u32 type_qualifiers;
    ctool_u32 binding_attributes = 0u;
    ctool_u32 binding_alignment = 0u;
    cfront_attributes_t declarator_attributes;
    const ctool_c_pp_token_t *name_token = cfront_peek(context);
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
        binding_alignment = declarator_attributes.alignment;
      }
    }
    if (cfront_peek_is(context, "=") == CTOOL_TRUE) {
      return cfront_emit_failure(
          context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_UNSUPPORTED,
          cfront_peek(context),
          "object initializer parsing is outside the declaration frontend");
    }
    if (cfront_peek_is(context, "{") == CTOOL_TRUE) {
      return cfront_emit_failure(
          context, CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_UNSUPPORTED,
          cfront_peek(context),
          "function body parsing is outside the declaration frontend");
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
      binding_attributes |= CTOOL_C_DECL_ATTR_NORETURN;
    }
    status = cfront_append_binding(
        context, kind, cfront_public_storage(specifiers.storage), name, type,
        binding_attributes, binding_alignment, name_token, &location,
        &physical_location, 0ull, CTOOL_FALSE);
    if (status != CTOOL_OK) {
      return status;
    }
    (void)location;
    (void)physical_location;
    if (cfront_peek_is(context, ",") == CTOOL_TRUE) {
      (void)cfront_advance(context);
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
  ctool_status_t status;
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
        status = cfront_parse_constant_or(context, &value);
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
          enumerator->spelling, enumerator_type, 0u, 0u, enumerator,
          &enumerator->location, &enumerator->physical_location, value.bits,
          cfront_integer_unsigned(value.kind));
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
  ctool_u32 index;
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
          return cfront_integer_overflow(context, operator_token);
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
      return cfront_emit_failure(
          context, CTOOL_ERR_INPUT,
          CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION, operator_token,
          "integer constant shift count is outside the left operand width");
    }
    if (left == CTOOL_TRUE) {
      ctool_u32 count = (ctool_u32)right.bits;
      if (cfront_integer_unsigned(value_out->kind) == CTOOL_FALSE) {
        ctool_u64 maximum =
            cfront_integer_width(value_out->kind) == 32u
                ? 0x7fffffffull
                : 0x7fffffffffffffffull;
        if (cfront_integer_negative(value_out) == CTOOL_TRUE) {
          return cfront_emit_failure(
              context, CTOOL_ERR_INPUT,
              CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION, operator_token,
              "signed left shift requires a nonnegative left operand");
        }
        if (value_out->bits > (maximum >> count)) {
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

static ctool_status_t cfront_parse_constant_and(
    cfront_context_t *context, cfront_integer_t *value_out) {
  value_out->bits = 0ull;
  value_out->kind = CFRONT_INTEGER_SIGNED_32;
  ctool_status_t status = cfront_parse_constant_shift(context, value_out);
  while (status == CTOOL_OK && cfront_peek_is(context, "&") == CTOOL_TRUE) {
    cfront_integer_t right = {0ull, CFRONT_INTEGER_SIGNED_32};
    (void)cfront_advance(context);
    status = cfront_parse_constant_shift(context, &right);
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

static ctool_status_t cfront_parse_constant_or(
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
