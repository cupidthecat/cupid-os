#include "ctool.h"
#include "ctool_host.h"
#include "cupidc_type.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_COUNT(values) ((ctool_u32)(sizeof(values) / sizeof((values)[0])))

typedef struct {
  ctool_u32 size;
  ctool_u32 alignment;
  ctool_bool complete;
  ctool_bool object;
  ctool_bool integer;
  ctool_bool signed_value;
} expected_type_layout_t;

static int string_equal(ctool_string_t actual, const char *expected) {
  size_t expected_size = strlen(expected);
  return actual.size == (ctool_u32)expected_size &&
                 (expected_size == 0u ||
                  memcmp(actual.data, expected, expected_size) == 0)
             ? 1
             : 0;
}

static int arena_marks_equal(ctool_arena_mark_t left,
                             ctool_arena_mark_t right) {
  return left.owner == right.owner && left.block == right.block &&
                 left.used == right.used && left.generation == right.generation
             ? 1
             : 0;
}

static ctool_c_pp_location_t source_location(const char *path, ctool_u32 line,
                                             ctool_u32 column) {
  ctool_c_pp_location_t location;
  location.path = ctool_string(path);
  location.line = line;
  location.column = column;
  return location;
}

static ctool_c_type_node_t type_node(ctool_c_type_kind_t kind, const char *path,
                                     ctool_u32 line) {
  ctool_c_type_node_t node;
  (void)memset(&node, 0, sizeof(node));
  node.kind = kind;
  node.location = source_location(path, line, 1u);
  node.physical_location = node.location;
  node.referenced_type = CTOOL_C_TYPE_NONE;
  node.array_bound_kind = CTOOL_C_ARRAY_FIXED;
  node.record_kind = CTOOL_C_RECORD_STRUCT;
  return node;
}

static ctool_c_record_member_t record_member(const char *name, ctool_u32 type,
                                             const char *path, ctool_u32 line,
                                             ctool_u32 pack_alignment) {
  ctool_c_record_member_t member;
  (void)memset(&member, 0, sizeof(member));
  member.name = ctool_string(name);
  member.type = type;
  member.location = source_location(path, line, 5u);
  member.physical_location = member.location;
  member.pack_alignment = pack_alignment;
  return member;
}

static ctool_c_layout_request_t
layout_request(const ctool_c_type_node_t *types, ctool_u32 type_count,
               const ctool_c_record_member_t *members, ctool_u32 member_count,
               const char *path) {
  ctool_c_layout_request_t request;
  (void)memset(&request, 0, sizeof(request));
  request.location = source_location(path, 1u, 1u);
  request.physical_location = request.location;
  request.types = types;
  request.type_count = type_count;
  request.members = members;
  request.member_count = member_count;
  return request;
}

static int open_job(const char *mode, ctool_host_adapter_t *adapter,
                    ctool_job_t **job_out) {
  ctool_job_config_t config;
  ctool_status_t status = ctool_host_adapter_init(adapter, ".");
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "%s: host adapter: %s\n", mode,
                  ctool_status_name(status));
    return 1;
  }
  config = ctool_host_job_config(adapter, ctool_default_limits());
  status = ctool_job_open(&config, job_out);
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "%s: job open: %s\n", mode,
                  ctool_status_name(status));
    return 1;
  }
  return 0;
}

static int open_limited_job(const char *mode, ctool_u32 arena_block_bytes,
                            ctool_u32 arena_bytes,
                            ctool_host_adapter_t *adapter,
                            ctool_job_t **job_out) {
  ctool_job_config_t config;
  ctool_limits_t limits;
  ctool_status_t status = ctool_host_adapter_init(adapter, ".");
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "%s: host adapter: %s\n", mode,
                  ctool_status_name(status));
    return 1;
  }
  limits = ctool_default_limits();
  limits.arena_block_bytes = arena_block_bytes;
  limits.arena_bytes = arena_bytes;
  config = ctool_host_job_config(adapter, limits);
  status = ctool_job_open(&config, job_out);
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "%s: limited job open: %s\n", mode,
                  ctool_status_name(status));
    return 1;
  }
  return 0;
}

static int result_is_zero(const ctool_c_layout_result_t *result) {
  return result->types == NULL && result->type_count == 0u &&
                 result->members == NULL && result->member_count == 0u
             ? 1
             : 0;
}

static int expect_type(const char *mode, const ctool_c_layout_result_t *result,
                       ctool_u32 index, expected_type_layout_t expected) {
  const ctool_c_type_layout_t *actual;
  if (result->types == NULL || index >= result->type_count) {
    (void)fprintf(stderr, "%s: missing type layout %u\n", mode, index);
    return 1;
  }
  actual = &result->types[index];
  if (actual->size != expected.size ||
      actual->alignment != expected.alignment ||
      actual->is_complete_object != expected.complete ||
      actual->is_object != expected.object ||
      actual->is_integer != expected.integer ||
      actual->is_signed != expected.signed_value) {
    (void)fprintf(stderr, "%s: type %u differs: got %u/%u c%u o%u i%u s%u\n",
                  mode, index, actual->size, actual->alignment,
                  (ctool_u32)actual->is_complete_object,
                  (ctool_u32)actual->is_object, (ctool_u32)actual->is_integer,
                  (ctool_u32)actual->is_signed);
    return 1;
  }
  return 0;
}

static int expect_member(const char *mode,
                         const ctool_c_layout_result_t *result, ctool_u32 index,
                         ctool_u32 byte_offset, ctool_u32 bit_offset,
                         ctool_u32 bit_width, ctool_u32 size,
                         ctool_u32 alignment) {
  const ctool_c_member_layout_t *actual;
  if (result->members == NULL || index >= result->member_count) {
    (void)fprintf(stderr, "%s: missing member layout %u\n", mode, index);
    return 1;
  }
  actual = &result->members[index];
  if (actual->byte_offset != byte_offset || actual->bit_offset != bit_offset ||
      actual->bit_width != bit_width || actual->size != size ||
      actual->alignment != alignment) {
    (void)fprintf(
        stderr,
        "%s: member %u differs: got byte=%u bit=%u width=%u size=%u align=%u\n",
        mode, index, actual->byte_offset, actual->bit_offset, actual->bit_width,
        actual->size, actual->alignment);
    return 1;
  }
  return 0;
}

static int run_scalars(void) {
  enum {
    T_VOID,
    T_BOOL,
    T_CHAR,
    T_SIGNED_CHAR,
    T_UNSIGNED_CHAR,
    T_SIGNED_SHORT,
    T_UNSIGNED_SHORT,
    T_SIGNED_INT,
    T_UNSIGNED_INT,
    T_SIGNED_LONG,
    T_UNSIGNED_LONG,
    T_SIGNED_LONG_LONG,
    T_UNSIGNED_LONG_LONG,
    T_FLOAT,
    T_DOUBLE,
    T_LONG_DOUBLE,
    T_FUNCTION,
    T_VOID_POINTER,
    T_FUNCTION_POINTER,
    T_INT_ARRAY,
    T_ENUM,
    T_FLOAT4,
    T_DOUBLE2,
    T_FUNCTION_VOID_PROTOTYPE,
    T_FUNCTION_OLD_STYLE,
    T_QUALIFIED_INT,
    T_ATOMIC_SIGNED_LONG_LONG,
    T_ATOMIC_DOUBLE,
    T_ATOMIC_VOID_POINTER,
    T_ATOMIC_ENUM_LONG_LONG,
    T_ATOMIC_ALIGNED_LONG_LONG,
    T_ATOMIC_ALIGNED_INT,
    T_ALIGNED_INT_ONE,
    T_QUALIFIED_ATOMIC_ALIGNED_INT,
    T_REALIGNED_ATOMIC_INT,
    T_CONST_REALIGNED_ATOMIC_INT,
    T_COUNT
  };
  static const ctool_c_type_kind_t builtin_kinds[] = {
      CTOOL_C_TYPE_VOID,
      CTOOL_C_TYPE_BOOL,
      CTOOL_C_TYPE_CHAR,
      CTOOL_C_TYPE_SIGNED_CHAR,
      CTOOL_C_TYPE_UNSIGNED_CHAR,
      CTOOL_C_TYPE_SIGNED_SHORT,
      CTOOL_C_TYPE_UNSIGNED_SHORT,
      CTOOL_C_TYPE_SIGNED_INT,
      CTOOL_C_TYPE_UNSIGNED_INT,
      CTOOL_C_TYPE_SIGNED_LONG,
      CTOOL_C_TYPE_UNSIGNED_LONG,
      CTOOL_C_TYPE_SIGNED_LONG_LONG,
      CTOOL_C_TYPE_UNSIGNED_LONG_LONG,
      CTOOL_C_TYPE_FLOAT,
      CTOOL_C_TYPE_DOUBLE,
      CTOOL_C_TYPE_LONG_DOUBLE};
  static const expected_type_layout_t expected[] = {
      {0u, 1u, CTOOL_FALSE, CTOOL_FALSE, CTOOL_FALSE, CTOOL_FALSE},
      {1u, 1u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE},
      {1u, 1u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE},
      {1u, 1u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE},
      {1u, 1u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE},
      {2u, 2u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE},
      {2u, 2u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE},
      {4u, 4u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE},
      {4u, 4u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE},
      {4u, 4u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE},
      {4u, 4u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE},
      {8u, 4u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE},
      {8u, 4u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE},
      {4u, 4u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE, CTOOL_FALSE},
      {8u, 4u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE, CTOOL_FALSE},
      {12u, 4u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE, CTOOL_FALSE},
      {0u, 1u, CTOOL_FALSE, CTOOL_FALSE, CTOOL_FALSE, CTOOL_FALSE},
      {4u, 4u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE, CTOOL_FALSE},
      {4u, 4u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE, CTOOL_FALSE},
      {28u, 4u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE, CTOOL_FALSE},
      {4u, 4u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE},
      {16u, 16u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE, CTOOL_FALSE},
      {16u, 16u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE, CTOOL_FALSE},
      {0u, 1u, CTOOL_FALSE, CTOOL_FALSE, CTOOL_FALSE, CTOOL_FALSE},
      {0u, 1u, CTOOL_FALSE, CTOOL_FALSE, CTOOL_FALSE, CTOOL_FALSE},
      {4u, 4u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE},
      {8u, 8u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE},
      {8u, 8u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE, CTOOL_FALSE},
      {4u, 4u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE, CTOOL_FALSE},
      {8u, 8u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE},
      {8u, 8u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE},
      {4u, 8u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE},
      {4u, 1u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE},
      {4u, 4u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE},
      {4u, 1u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE},
      {4u, 1u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE}};
  ctool_c_type_node_t nodes[T_COUNT];
  ctool_c_type_node_t node_copy[T_COUNT];
  ctool_u32 parameter_types[2];
  ctool_u32 parameter_copy[2];
  ctool_c_layout_request_t request;
  ctool_c_layout_result_t result;
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_status_t status;
  ctool_u32 index;

  if (open_job("scalars", &adapter, &job) != 0) {
    return 1;
  }
  (void)memset(nodes, 0, sizeof(nodes));
  for (index = 0u; index < ARRAY_COUNT(builtin_kinds); index++) {
    nodes[index] = type_node(builtin_kinds[index], "/scalars.c", index + 1u);
  }
  nodes[T_FUNCTION] =
      type_node(CTOOL_C_TYPE_FUNCTION, "/scalars.c", T_FUNCTION + 1u);
  nodes[T_FUNCTION].referenced_type = T_SIGNED_INT;
  nodes[T_FUNCTION].has_prototype = CTOOL_TRUE;
  nodes[T_FUNCTION].parameter_count = ARRAY_COUNT(parameter_types);
  nodes[T_FUNCTION].variadic = CTOOL_TRUE;
  nodes[T_VOID_POINTER] =
      type_node(CTOOL_C_TYPE_POINTER, "/scalars.c", T_VOID_POINTER + 1u);
  nodes[T_VOID_POINTER].referenced_type = T_VOID;
  nodes[T_VOID_POINTER].qualifiers = CTOOL_C_QUAL_RESTRICT;
  nodes[T_FUNCTION_POINTER] =
      type_node(CTOOL_C_TYPE_POINTER, "/scalars.c", T_FUNCTION_POINTER + 1u);
  nodes[T_FUNCTION_POINTER].referenced_type = T_FUNCTION;
  nodes[T_INT_ARRAY] =
      type_node(CTOOL_C_TYPE_ARRAY, "/scalars.c", T_INT_ARRAY + 1u);
  nodes[T_INT_ARRAY].referenced_type = T_SIGNED_INT;
  nodes[T_INT_ARRAY].element_count = 7u;
  nodes[T_ENUM] = type_node(CTOOL_C_TYPE_ENUM, "/scalars.c", T_ENUM + 1u);
  nodes[T_ENUM].referenced_type = T_SIGNED_INT;
  nodes[T_FLOAT4] = type_node(CTOOL_C_TYPE_VECTOR, "/scalars.c", T_FLOAT4 + 1u);
  nodes[T_FLOAT4].referenced_type = T_FLOAT;
  nodes[T_FLOAT4].element_count = 4u;
  nodes[T_DOUBLE2] =
      type_node(CTOOL_C_TYPE_VECTOR, "/scalars.c", T_DOUBLE2 + 1u);
  nodes[T_DOUBLE2].referenced_type = T_DOUBLE;
  nodes[T_DOUBLE2].element_count = 2u;
  nodes[T_FUNCTION_VOID_PROTOTYPE] = type_node(
      CTOOL_C_TYPE_FUNCTION, "/scalars.c", T_FUNCTION_VOID_PROTOTYPE + 1u);
  nodes[T_FUNCTION_VOID_PROTOTYPE].referenced_type = T_SIGNED_INT;
  nodes[T_FUNCTION_VOID_PROTOTYPE].has_prototype = CTOOL_TRUE;
  nodes[T_FUNCTION_OLD_STYLE] =
      type_node(CTOOL_C_TYPE_FUNCTION, "/scalars.c", T_FUNCTION_OLD_STYLE + 1u);
  nodes[T_FUNCTION_OLD_STYLE].referenced_type = T_SIGNED_INT;
  nodes[T_QUALIFIED_INT] =
      type_node(CTOOL_C_TYPE_SIGNED_INT, "/scalars.c", T_QUALIFIED_INT + 1u);
  nodes[T_QUALIFIED_INT].qualifiers =
      CTOOL_C_QUAL_CONST | CTOOL_C_QUAL_VOLATILE | CTOOL_C_QUAL_ATOMIC;
  nodes[T_ATOMIC_SIGNED_LONG_LONG] =
      type_node(CTOOL_C_TYPE_SIGNED_LONG_LONG, "/scalars.c",
                T_ATOMIC_SIGNED_LONG_LONG + 1u);
  nodes[T_ATOMIC_SIGNED_LONG_LONG].qualifiers = CTOOL_C_QUAL_ATOMIC;
  nodes[T_ATOMIC_DOUBLE] =
      type_node(CTOOL_C_TYPE_DOUBLE, "/scalars.c", T_ATOMIC_DOUBLE + 1u);
  nodes[T_ATOMIC_DOUBLE].qualifiers = CTOOL_C_QUAL_ATOMIC;
  nodes[T_ATOMIC_VOID_POINTER] = type_node(
      CTOOL_C_TYPE_POINTER, "/scalars.c", T_ATOMIC_VOID_POINTER + 1u);
  nodes[T_ATOMIC_VOID_POINTER].referenced_type = T_VOID;
  nodes[T_ATOMIC_VOID_POINTER].qualifiers = CTOOL_C_QUAL_ATOMIC;
  nodes[T_ATOMIC_ENUM_LONG_LONG] = type_node(
      CTOOL_C_TYPE_ENUM, "/scalars.c", T_ATOMIC_ENUM_LONG_LONG + 1u);
  nodes[T_ATOMIC_ENUM_LONG_LONG].referenced_type = T_SIGNED_LONG_LONG;
  nodes[T_ATOMIC_ENUM_LONG_LONG].qualifiers = CTOOL_C_QUAL_ATOMIC;
  nodes[T_ATOMIC_ALIGNED_LONG_LONG] = type_node(
      CTOOL_C_TYPE_ALIGNED, "/scalars.c", T_ATOMIC_ALIGNED_LONG_LONG + 1u);
  nodes[T_ATOMIC_ALIGNED_LONG_LONG].referenced_type = T_SIGNED_LONG_LONG;
  nodes[T_ATOMIC_ALIGNED_LONG_LONG].explicit_alignment = 1u;
  nodes[T_ATOMIC_ALIGNED_LONG_LONG].qualifiers = CTOOL_C_QUAL_ATOMIC;
  nodes[T_ATOMIC_ALIGNED_INT] =
      type_node(CTOOL_C_TYPE_ALIGNED, "/scalars.c", T_ATOMIC_ALIGNED_INT + 1u);
  nodes[T_ATOMIC_ALIGNED_INT].referenced_type = T_SIGNED_INT;
  nodes[T_ATOMIC_ALIGNED_INT].explicit_alignment = 8u;
  nodes[T_ATOMIC_ALIGNED_INT].qualifiers = CTOOL_C_QUAL_ATOMIC;
  nodes[T_ALIGNED_INT_ONE] =
      type_node(CTOOL_C_TYPE_ALIGNED, "/scalars.c", T_ALIGNED_INT_ONE + 1u);
  nodes[T_ALIGNED_INT_ONE].referenced_type = T_SIGNED_INT;
  nodes[T_ALIGNED_INT_ONE].explicit_alignment = 1u;
  nodes[T_QUALIFIED_ATOMIC_ALIGNED_INT] = type_node(
      CTOOL_C_TYPE_QUALIFIED, "/scalars.c",
      T_QUALIFIED_ATOMIC_ALIGNED_INT + 1u);
  nodes[T_QUALIFIED_ATOMIC_ALIGNED_INT].referenced_type = T_ALIGNED_INT_ONE;
  nodes[T_QUALIFIED_ATOMIC_ALIGNED_INT].qualifiers = CTOOL_C_QUAL_ATOMIC;
  nodes[T_REALIGNED_ATOMIC_INT] = type_node(
      CTOOL_C_TYPE_ALIGNED, "/scalars.c", T_REALIGNED_ATOMIC_INT + 1u);
  nodes[T_REALIGNED_ATOMIC_INT].referenced_type =
      T_QUALIFIED_ATOMIC_ALIGNED_INT;
  nodes[T_REALIGNED_ATOMIC_INT].explicit_alignment = 1u;
  nodes[T_CONST_REALIGNED_ATOMIC_INT] = type_node(
      CTOOL_C_TYPE_QUALIFIED, "/scalars.c",
      T_CONST_REALIGNED_ATOMIC_INT + 1u);
  nodes[T_CONST_REALIGNED_ATOMIC_INT].referenced_type =
      T_REALIGNED_ATOMIC_INT;
  nodes[T_CONST_REALIGNED_ATOMIC_INT].qualifiers = CTOOL_C_QUAL_CONST;

  parameter_types[0] = T_SIGNED_INT;
  parameter_types[1] = T_VOID_POINTER;
  (void)memcpy(node_copy, nodes, sizeof(nodes));
  (void)memcpy(parameter_copy, parameter_types, sizeof(parameter_types));

  request = layout_request(nodes, T_COUNT, NULL, 0u, "/scalars.c");
  request.parameter_types = parameter_types;
  request.parameter_type_count = ARRAY_COUNT(parameter_types);
  (void)memset(&result, 0xa5, sizeof(result));
  status = ctool_c_layout_types(job, &request, &result);
  if (status != CTOOL_OK || result.type_count != T_COUNT ||
      result.member_count != 0u || result.members != NULL ||
      memcmp(nodes, node_copy, sizeof(nodes)) != 0 ||
      memcmp(parameter_types, parameter_copy, sizeof(parameter_types)) != 0) {
    (void)fprintf(stderr,
                  "scalars: operation failed or result shape differs\n");
    ctool_job_close(job);
    return 1;
  }
  for (index = 0u; index < T_COUNT; index++) {
    if (expect_type("scalars", &result, index, expected[index]) != 0) {
      ctool_job_close(job);
      return 1;
    }
  }
  ctool_job_close(job);
  (void)printf("scalars: ok\n");
  return 0;
}

/* This graph is a manual semantic transcription of the active
 * kernel/fs/fat16.h declarations.  The parser-driven unchanged-source gate
 * belongs to the next frontend commit; these fixed literals are independent
 * i386 ABI oracles for the type-layout seam. */
static int run_fat16(void) {
  enum {
    T_U8,
    T_CHAR,
    T_U16,
    T_U32,
    T_U8_3,
    T_MBR_PARTITION,
    T_U8_446,
    T_MBR_PARTITION_4,
    T_MBR,
    T_CHAR_8,
    T_BOOT_SECTOR,
    T_CHAR_3,
    T_DIR_ENTRY,
    T_FS,
    T_FILE,
    T_COUNT
  };
  static const ctool_u32 record_types[] = {
      T_MBR_PARTITION, T_MBR, T_BOOT_SECTOR, T_DIR_ENTRY, T_FS, T_FILE};
  static const ctool_u32 record_sizes[] = {16u, 512u, 36u, 32u, 36u, 24u};
  static const ctool_u32 record_alignments[] = {1u, 1u, 1u, 1u, 4u, 4u};
  static const ctool_u32 expected_offsets[] = {
      0u,  1u,  4u,  5u,  8u,  12u, 0u,  446u, 510u, 0u,  3u,  11u, 13u, 14u,
      16u, 17u, 19u, 21u, 22u, 24u, 26u, 28u,  32u,  0u,  8u,  11u, 12u, 13u,
      14u, 16u, 18u, 20u, 22u, 24u, 26u, 28u,  0u,   4u,  8u,  12u, 16u, 18u,
      20u, 22u, 24u, 28u, 32u, 0u,  4u,  8u,   12u,  14u, 16u, 20u};
  ctool_c_type_node_t nodes[T_COUNT];
  ctool_c_record_member_t members[54];
  ctool_c_layout_request_t request;
  ctool_c_layout_result_t result;
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_status_t status;
  ctool_u32 member_count = 0u;
  ctool_u32 index;

#define ADD_MEMBER(NAME, TYPE, LINE, PACK)                                     \
  do {                                                                         \
    members[member_count] =                                                    \
        record_member((NAME), (TYPE), "/kernel/fs/fat16.h", (LINE), (PACK));   \
    member_count++;                                                            \
  } while (0)
#define BEGIN_RECORD(TYPE, FIRST, COUNT, LINE)                                 \
  do {                                                                         \
    nodes[(TYPE)] =                                                            \
        type_node(CTOOL_C_TYPE_RECORD, "/kernel/fs/fat16.h", (LINE));          \
    nodes[(TYPE)].record_kind = CTOOL_C_RECORD_STRUCT;                         \
    nodes[(TYPE)].record_complete = CTOOL_TRUE;                                \
    nodes[(TYPE)].first_member = (FIRST);                                      \
    nodes[(TYPE)].member_count = (COUNT);                                      \
  } while (0)

  if (open_job("fat16", &adapter, &job) != 0) {
    return 1;
  }
  (void)memset(nodes, 0, sizeof(nodes));
  (void)memset(members, 0, sizeof(members));
  nodes[T_U8] =
      type_node(CTOOL_C_TYPE_UNSIGNED_CHAR, "/kernel/fs/fat16.h", 29u);
  nodes[T_CHAR] = type_node(CTOOL_C_TYPE_CHAR, "/kernel/fs/fat16.h", 44u);
  nodes[T_U16] =
      type_node(CTOOL_C_TYPE_UNSIGNED_SHORT, "/kernel/fs/fat16.h", 40u);
  nodes[T_U32] =
      type_node(CTOOL_C_TYPE_UNSIGNED_INT, "/kernel/fs/fat16.h", 33u);
  nodes[T_U8_3] = type_node(CTOOL_C_TYPE_ARRAY, "/kernel/fs/fat16.h", 30u);
  nodes[T_U8_3].referenced_type = T_U8;
  nodes[T_U8_3].element_count = 3u;
  nodes[T_U8_446] = type_node(CTOOL_C_TYPE_ARRAY, "/kernel/fs/fat16.h", 38u);
  nodes[T_U8_446].referenced_type = T_U8;
  nodes[T_U8_446].element_count = 446u;
  nodes[T_MBR_PARTITION_4] =
      type_node(CTOOL_C_TYPE_ARRAY, "/kernel/fs/fat16.h", 39u);
  nodes[T_MBR_PARTITION_4].referenced_type = T_MBR_PARTITION;
  nodes[T_MBR_PARTITION_4].element_count = 4u;
  nodes[T_CHAR_8] = type_node(CTOOL_C_TYPE_ARRAY, "/kernel/fs/fat16.h", 45u);
  nodes[T_CHAR_8].referenced_type = T_CHAR;
  nodes[T_CHAR_8].element_count = 8u;
  nodes[T_CHAR_3] = type_node(CTOOL_C_TYPE_ARRAY, "/kernel/fs/fat16.h", 62u);
  nodes[T_CHAR_3].referenced_type = T_CHAR;
  nodes[T_CHAR_3].element_count = 3u;

  BEGIN_RECORD(T_MBR_PARTITION, 0u, 6u, 28u);
  ADD_MEMBER("status", T_U8, 29u, 1u);
  ADD_MEMBER("chs_start", T_U8_3, 30u, 1u);
  ADD_MEMBER("type", T_U8, 31u, 1u);
  ADD_MEMBER("chs_end", T_U8_3, 32u, 1u);
  ADD_MEMBER("lba_start", T_U32, 33u, 1u);
  ADD_MEMBER("sector_count", T_U32, 34u, 1u);

  BEGIN_RECORD(T_MBR, 6u, 3u, 37u);
  ADD_MEMBER("boot_code", T_U8_446, 38u, 1u);
  ADD_MEMBER("partitions", T_MBR_PARTITION_4, 39u, 1u);
  ADD_MEMBER("signature", T_U16, 40u, 1u);

  BEGIN_RECORD(T_BOOT_SECTOR, 9u, 14u, 43u);
  ADD_MEMBER("jump", T_U8_3, 44u, 1u);
  ADD_MEMBER("oem", T_CHAR_8, 45u, 1u);
  ADD_MEMBER("bytes_per_sector", T_U16, 46u, 1u);
  ADD_MEMBER("sectors_per_cluster", T_U8, 47u, 1u);
  ADD_MEMBER("reserved_sectors", T_U16, 48u, 1u);
  ADD_MEMBER("num_fats", T_U8, 49u, 1u);
  ADD_MEMBER("root_dir_entries", T_U16, 50u, 1u);
  ADD_MEMBER("total_sectors_16", T_U16, 51u, 1u);
  ADD_MEMBER("media_type", T_U8, 52u, 1u);
  ADD_MEMBER("sectors_per_fat", T_U16, 53u, 1u);
  ADD_MEMBER("sectors_per_track", T_U16, 54u, 1u);
  ADD_MEMBER("num_heads", T_U16, 55u, 1u);
  ADD_MEMBER("hidden_sectors", T_U32, 56u, 1u);
  ADD_MEMBER("total_sectors_32", T_U32, 57u, 1u);

  BEGIN_RECORD(T_DIR_ENTRY, 23u, 13u, 60u);
  ADD_MEMBER("filename", T_CHAR_8, 61u, 1u);
  ADD_MEMBER("ext", T_CHAR_3, 62u, 1u);
  ADD_MEMBER("attributes", T_U8, 63u, 1u);
  ADD_MEMBER("reserved", T_U8, 64u, 1u);
  ADD_MEMBER("create_time_tenths", T_U8, 65u, 1u);
  ADD_MEMBER("create_time", T_U16, 66u, 1u);
  ADD_MEMBER("create_date", T_U16, 67u, 1u);
  ADD_MEMBER("access_date", T_U16, 68u, 1u);
  ADD_MEMBER("first_cluster_high", T_U16, 69u, 1u);
  ADD_MEMBER("modify_time", T_U16, 70u, 1u);
  ADD_MEMBER("modify_date", T_U16, 71u, 1u);
  ADD_MEMBER("first_cluster", T_U16, 72u, 1u);
  ADD_MEMBER("file_size", T_U32, 73u, 1u);

  BEGIN_RECORD(T_FS, 36u, 11u, 78u);
  ADD_MEMBER("partition_lba", T_U32, 79u, 0u);
  ADD_MEMBER("fat_start", T_U32, 80u, 0u);
  ADD_MEMBER("root_dir_start", T_U32, 81u, 0u);
  ADD_MEMBER("data_start", T_U32, 82u, 0u);
  ADD_MEMBER("bytes_per_sector", T_U16, 83u, 0u);
  ADD_MEMBER("sectors_per_cluster", T_U8, 84u, 0u);
  ADD_MEMBER("reserved_sectors", T_U16, 85u, 0u);
  ADD_MEMBER("num_fats", T_U8, 86u, 0u);
  ADD_MEMBER("root_dir_entries", T_U16, 87u, 0u);
  ADD_MEMBER("total_sectors", T_U32, 88u, 0u);
  ADD_MEMBER("sectors_per_fat", T_U16, 89u, 0u);

  BEGIN_RECORD(T_FILE, 47u, 7u, 92u);
  ADD_MEMBER("first_cluster", T_U16, 93u, 0u);
  ADD_MEMBER("file_size", T_U32, 94u, 0u);
  ADD_MEMBER("position", T_U32, 95u, 0u);
  ADD_MEMBER("is_open", T_U8, 96u, 0u);
  ADD_MEMBER("cached_cluster", T_U16, 97u, 0u);
  ADD_MEMBER("cached_cluster_index", T_U32, 98u, 0u);
  ADD_MEMBER("cache_valid", T_U8, 99u, 0u);

  if (member_count != ARRAY_COUNT(members) ||
      ARRAY_COUNT(expected_offsets) != member_count) {
    (void)fprintf(stderr, "fat16: fixture shape differs\n");
    ctool_job_close(job);
    return 1;
  }
  request = layout_request(nodes, T_COUNT, members, member_count,
                           "/kernel/fs/fat16.h");
  (void)memset(&result, 0xa5, sizeof(result));
  status = ctool_c_layout_types(job, &request, &result);
  if (status != CTOOL_OK || result.type_count != T_COUNT ||
      result.member_count != member_count) {
    (void)fprintf(stderr, "fat16: layout operation failed\n");
    ctool_job_close(job);
    return 1;
  }
  for (index = 0u; index < ARRAY_COUNT(record_types); index++) {
    expected_type_layout_t record_expected = {
        record_sizes[index], record_alignments[index],
        CTOOL_TRUE,          CTOOL_TRUE,
        CTOOL_FALSE,         CTOOL_FALSE};
    if (expect_type("fat16", &result, record_types[index], record_expected) !=
        0) {
      ctool_job_close(job);
      return 1;
    }
  }
  for (index = 0u; index < member_count; index++) {
    ctool_u32 expected_alignment = index < 36u ? 1u : 0u;
    if (expected_alignment == 0u) {
      ctool_u32 member_type = members[index].type;
      expected_alignment = result.types[member_type].alignment;
    }
    if (expect_member("fat16", &result, index, expected_offsets[index], 0u, 0u,
                      result.types[members[index].type].size,
                      expected_alignment) != 0) {
      ctool_job_close(job);
      return 1;
    }
  }
  ctool_job_close(job);
  (void)printf("fat16: ok\n");
  return 0;

#undef BEGIN_RECORD
#undef ADD_MEMBER
}

static int run_records(void) {
  enum {
    T_U8,
    T_CHAR,
    T_BOOL,
    T_I32,
    T_U16,
    T_U32,
    T_U8_6,
    T_INNER,
    T_CONST_INNER,
    T_POINTER_TO_CONST_INNER,
    T_CONST_POINTER_TO_INNER,
    T_OUTER,
    T_UNION,
    T_BITS,
    T_DESCRIPTOR,
    T_ALIGNED,
    T_FLEX_U8,
    T_FLEX_RECORD,
    T_CLASS,
    T_MEMBER_PACKED,
    T_PACK_TRANSITION,
    T_SELF_RECORD,
    T_SELF_POINTER,
    T_ALIGNED_BITS,
    T_UNNAMED_BIT_UNION,
    T_PACKED_BIT_UNION,
    T_ZERO_WIDTH_BITS,
    T_LOWER_ALIGNED,
    T_LOWER_RECORD,
    T_PROMOTED_INNER,
    T_PROMOTED_OUTER,
    T_COUNT
  };
  ctool_c_type_node_t nodes[T_COUNT];
  ctool_c_record_member_t members[42];
  ctool_c_layout_request_t request;
  ctool_c_layout_result_t result;
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_status_t status;
  ctool_u32 index;
  expected_type_layout_t expected_record;

  if (open_job("records", &adapter, &job) != 0) {
    return 1;
  }
  (void)memset(nodes, 0, sizeof(nodes));
  (void)memset(members, 0, sizeof(members));
  nodes[T_U8] = type_node(CTOOL_C_TYPE_UNSIGNED_CHAR, "/records.c", 1u);
  nodes[T_CHAR] = type_node(CTOOL_C_TYPE_CHAR, "/records.c", 2u);
  nodes[T_BOOL] = type_node(CTOOL_C_TYPE_BOOL, "/records.c", 3u);
  nodes[T_I32] = type_node(CTOOL_C_TYPE_SIGNED_INT, "/records.c", 4u);
  nodes[T_U16] = type_node(CTOOL_C_TYPE_UNSIGNED_SHORT, "/records.c", 2u);
  nodes[T_U32] = type_node(CTOOL_C_TYPE_UNSIGNED_INT, "/records.c", 3u);
  nodes[T_U8_6] = type_node(CTOOL_C_TYPE_ARRAY, "/records.c", 4u);
  nodes[T_U8_6].referenced_type = T_U8;
  nodes[T_U8_6].element_count = 6u;

  nodes[T_INNER] = type_node(CTOOL_C_TYPE_RECORD, "/records.c", 10u);
  nodes[T_INNER].record_complete = CTOOL_TRUE;
  nodes[T_INNER].first_member = 0u;
  nodes[T_INNER].member_count = 2u;
  members[0] = record_member("a", T_U8, "/records.c", 11u, 0u);
  members[1] = record_member("b", T_U32, "/records.c", 12u, 0u);
  nodes[T_CONST_INNER] =
      type_node(CTOOL_C_TYPE_QUALIFIED, "/records.c", 13u);
  nodes[T_CONST_INNER].referenced_type = T_INNER;
  nodes[T_CONST_INNER].qualifiers = CTOOL_C_QUAL_CONST;
  nodes[T_POINTER_TO_CONST_INNER] =
      type_node(CTOOL_C_TYPE_POINTER, "/records.c", 14u);
  nodes[T_POINTER_TO_CONST_INNER].referenced_type = T_CONST_INNER;
  nodes[T_CONST_POINTER_TO_INNER] =
      type_node(CTOOL_C_TYPE_POINTER, "/records.c", 15u);
  nodes[T_CONST_POINTER_TO_INNER].referenced_type = T_INNER;
  nodes[T_CONST_POINTER_TO_INNER].qualifiers = CTOOL_C_QUAL_CONST;

  nodes[T_OUTER] = type_node(CTOOL_C_TYPE_RECORD, "/records.c", 20u);
  nodes[T_OUTER].record_complete = CTOOL_TRUE;
  nodes[T_OUTER].first_member = 2u;
  nodes[T_OUTER].member_count = 3u;
  members[2] = record_member("head", T_U16, "/records.c", 21u, 0u);
  members[3] = record_member("", T_INNER, "/records.c", 22u, 0u);
  members[3].anonymous = CTOOL_TRUE;
  members[4] = record_member("tail", T_U8, "/records.c", 23u, 0u);

  nodes[T_UNION] = type_node(CTOOL_C_TYPE_RECORD, "/records.c", 30u);
  nodes[T_UNION].record_kind = CTOOL_C_RECORD_UNION;
  nodes[T_UNION].record_complete = CTOOL_TRUE;
  nodes[T_UNION].first_member = 5u;
  nodes[T_UNION].member_count = 3u;
  members[5] = record_member("byte", T_U8, "/records.c", 31u, 0u);
  members[6] = record_member("word", T_U32, "/records.c", 32u, 0u);
  members[7] = record_member("bytes", T_U8_6, "/records.c", 33u, 0u);

  nodes[T_BITS] = type_node(CTOOL_C_TYPE_RECORD, "/i_video.h", 141u);
  nodes[T_BITS].record_complete = CTOOL_TRUE;
  nodes[T_BITS].first_member = 8u;
  nodes[T_BITS].member_count = 4u;
  members[8] = record_member("b", T_U32, "/i_video.h", 142u, 0u);
  members[9] = record_member("g", T_U32, "/i_video.h", 143u, 0u);
  members[10] = record_member("r", T_U32, "/i_video.h", 144u, 0u);
  members[11] = record_member("a", T_U32, "/i_video.h", 145u, 0u);
  for (index = 8u; index < 12u; index++) {
    members[index].is_bit_field = CTOOL_TRUE;
    members[index].bit_width = 8u;
  }

  nodes[T_DESCRIPTOR] = type_node(CTOOL_C_TYPE_RECORD, "/descriptor.c", 1u);
  nodes[T_DESCRIPTOR].record_complete = CTOOL_TRUE;
  nodes[T_DESCRIPTOR].record_packed = CTOOL_TRUE;
  nodes[T_DESCRIPTOR].explicit_alignment = 16u;
  nodes[T_DESCRIPTOR].first_member = 12u;
  nodes[T_DESCRIPTOR].member_count = 3u;
  members[12] = record_member("status", T_U8, "/descriptor.c", 2u, 0u);
  members[13] = record_member("address", T_U32, "/descriptor.c", 3u, 0u);
  members[14] = record_member("length", T_U16, "/descriptor.c", 4u, 0u);

  nodes[T_ALIGNED] = type_node(CTOOL_C_TYPE_RECORD, "/aligned.c", 1u);
  nodes[T_ALIGNED].record_complete = CTOOL_TRUE;
  nodes[T_ALIGNED].explicit_alignment = 32u;
  nodes[T_ALIGNED].first_member = 15u;
  nodes[T_ALIGNED].member_count = 3u;
  members[15] = record_member("lead", T_U8, "/aligned.c", 2u, 0u);
  members[16] = record_member("payload", T_U32, "/aligned.c", 3u, 0u);
  members[16].explicit_alignment = 8u;
  members[17] = record_member("tail", T_U8, "/aligned.c", 4u, 0u);

  nodes[T_FLEX_U8] = type_node(CTOOL_C_TYPE_ARRAY, "/flex.c", 2u);
  nodes[T_FLEX_U8].referenced_type = T_U8;
  nodes[T_FLEX_U8].array_bound_kind = CTOOL_C_ARRAY_UNSPECIFIED;
  nodes[T_FLEX_RECORD] = type_node(CTOOL_C_TYPE_RECORD, "/flex.c", 1u);
  nodes[T_FLEX_RECORD].record_complete = CTOOL_TRUE;
  nodes[T_FLEX_RECORD].first_member = 18u;
  nodes[T_FLEX_RECORD].member_count = 2u;
  members[18] = record_member("count", T_U32, "/flex.c", 2u, 0u);
  members[19] = record_member("data", T_FLEX_U8, "/flex.c", 3u, 0u);

  nodes[T_CLASS] = type_node(CTOOL_C_TYPE_RECORD, "/class.cc", 1u);
  nodes[T_CLASS].record_kind = CTOOL_C_RECORD_CLASS;
  nodes[T_CLASS].record_complete = CTOOL_TRUE;
  nodes[T_CLASS].first_member = 20u;
  nodes[T_CLASS].member_count = 2u;
  members[20] = record_member("tag", T_U8, "/class.cc", 2u, 0u);
  members[21] = record_member("value", T_U32, "/class.cc", 3u, 0u);

  nodes[T_MEMBER_PACKED] =
      type_node(CTOOL_C_TYPE_RECORD, "/member-packed.c", 1u);
  nodes[T_MEMBER_PACKED].record_complete = CTOOL_TRUE;
  nodes[T_MEMBER_PACKED].first_member = 22u;
  nodes[T_MEMBER_PACKED].member_count = 3u;
  members[22] = record_member("head", T_U8, "/member-packed.c", 2u, 0u);
  members[23] = record_member("payload", T_U32, "/member-packed.c", 3u, 0u);
  members[23].member_packed = CTOOL_TRUE;
  members[24] = record_member("tail", T_U16, "/member-packed.c", 4u, 0u);

  nodes[T_PACK_TRANSITION] =
      type_node(CTOOL_C_TYPE_RECORD, "/pack-transition.c", 1u);
  nodes[T_PACK_TRANSITION].record_complete = CTOOL_TRUE;
  nodes[T_PACK_TRANSITION].first_member = 25u;
  nodes[T_PACK_TRANSITION].member_count = 2u;
  members[25] = record_member("a", T_CHAR, "/pack-transition.c", 2u, 1u);
  members[26] = record_member("b", T_U32, "/pack-transition.c", 4u, 0u);

  nodes[T_SELF_RECORD] = type_node(CTOOL_C_TYPE_RECORD, "/weak-cycle.c", 1u);
  nodes[T_SELF_RECORD].record_complete = CTOOL_TRUE;
  nodes[T_SELF_RECORD].first_member = 27u;
  nodes[T_SELF_RECORD].member_count = 1u;
  nodes[T_SELF_POINTER] = type_node(CTOOL_C_TYPE_POINTER, "/weak-cycle.c", 2u);
  nodes[T_SELF_POINTER].referenced_type = T_SELF_RECORD;
  members[27] = record_member("next", T_SELF_POINTER, "/weak-cycle.c", 3u, 0u);

  nodes[T_ALIGNED_BITS] = type_node(CTOOL_C_TYPE_RECORD, "/aligned-bits.c", 1u);
  nodes[T_ALIGNED_BITS].record_complete = CTOOL_TRUE;
  nodes[T_ALIGNED_BITS].first_member = 28u;
  nodes[T_ALIGNED_BITS].member_count = 3u;
  members[28] = record_member("head", T_CHAR, "/aligned-bits.c", 2u, 0u);
  members[29] = record_member("b", T_U32, "/aligned-bits.c", 3u, 0u);
  members[29].is_bit_field = CTOOL_TRUE;
  members[29].bit_width = 8u;
  members[29].explicit_alignment = 8u;
  members[30] = record_member("tail", T_CHAR, "/aligned-bits.c", 4u, 0u);

  nodes[T_UNNAMED_BIT_UNION] =
      type_node(CTOOL_C_TYPE_RECORD, "/unnamed-bit-union.c", 1u);
  nodes[T_UNNAMED_BIT_UNION].record_kind = CTOOL_C_RECORD_UNION;
  nodes[T_UNNAMED_BIT_UNION].record_complete = CTOOL_TRUE;
  nodes[T_UNNAMED_BIT_UNION].first_member = 31u;
  nodes[T_UNNAMED_BIT_UNION].member_count = 1u;
  members[31] = record_member("", T_U32, "/unnamed-bit-union.c", 2u, 0u);
  members[31].is_bit_field = CTOOL_TRUE;
  members[31].bit_width = 8u;

  nodes[T_PACKED_BIT_UNION] =
      type_node(CTOOL_C_TYPE_RECORD, "/packed-bit-union.c", 1u);
  nodes[T_PACKED_BIT_UNION].record_kind = CTOOL_C_RECORD_UNION;
  nodes[T_PACKED_BIT_UNION].record_complete = CTOOL_TRUE;
  nodes[T_PACKED_BIT_UNION].record_packed = CTOOL_TRUE;
  nodes[T_PACKED_BIT_UNION].first_member = 32u;
  nodes[T_PACKED_BIT_UNION].member_count = 1u;
  members[32] = record_member("named", T_U32, "/packed-bit-union.c", 2u, 0u);
  members[32].is_bit_field = CTOOL_TRUE;
  members[32].bit_width = 8u;

  nodes[T_ZERO_WIDTH_BITS] =
      type_node(CTOOL_C_TYPE_RECORD, "/zero-width-bits.c", 1u);
  nodes[T_ZERO_WIDTH_BITS].record_complete = CTOOL_TRUE;
  nodes[T_ZERO_WIDTH_BITS].first_member = 33u;
  nodes[T_ZERO_WIDTH_BITS].member_count = 3u;
  members[33] = record_member("head", T_CHAR, "/zero-width-bits.c", 2u, 0u);
  members[34] = record_member("", T_U32, "/zero-width-bits.c", 3u, 0u);
  members[34].is_bit_field = CTOOL_TRUE;
  members[34].explicit_alignment = 8u;
  members[35] = record_member("tail", T_CHAR, "/zero-width-bits.c", 4u, 0u);

  nodes[T_LOWER_ALIGNED] =
      type_node(CTOOL_C_TYPE_ALIGNED, "/lower-aligned.c", 1u);
  nodes[T_LOWER_ALIGNED].referenced_type = T_I32;
  nodes[T_LOWER_ALIGNED].explicit_alignment = 1u;
  nodes[T_LOWER_RECORD] =
      type_node(CTOOL_C_TYPE_RECORD, "/lower-aligned.c", 2u);
  nodes[T_LOWER_RECORD].record_complete = CTOOL_TRUE;
  nodes[T_LOWER_RECORD].first_member = 36u;
  nodes[T_LOWER_RECORD].member_count = 3u;
  members[36] = record_member("c", T_CHAR, "/lower-aligned.c", 3u, 0u);
  members[37] = record_member("x", T_LOWER_ALIGNED, "/lower-aligned.c", 4u, 0u);
  members[38] = record_member("d", T_CHAR, "/lower-aligned.c", 5u, 0u);

  nodes[T_PROMOTED_INNER] =
      type_node(CTOOL_C_TYPE_RECORD, "/promoted-flex.c", 1u);
  nodes[T_PROMOTED_INNER].record_complete = CTOOL_TRUE;
  nodes[T_PROMOTED_INNER].first_member = 39u;
  nodes[T_PROMOTED_INNER].member_count = 1u;
  members[39] = record_member("n", T_I32, "/promoted-flex.c", 2u, 0u);
  nodes[T_PROMOTED_OUTER] =
      type_node(CTOOL_C_TYPE_RECORD, "/promoted-flex.c", 4u);
  nodes[T_PROMOTED_OUTER].record_complete = CTOOL_TRUE;
  nodes[T_PROMOTED_OUTER].first_member = 40u;
  nodes[T_PROMOTED_OUTER].member_count = 2u;
  members[40] = record_member("", T_PROMOTED_INNER, "/promoted-flex.c", 5u, 0u);
  members[40].anonymous = CTOOL_TRUE;
  members[41] = record_member("data", T_FLEX_U8, "/promoted-flex.c", 6u, 0u);

  request = layout_request(nodes, T_COUNT, members, ARRAY_COUNT(members),
                           "/records.c");
  status = ctool_c_layout_types(job, &request, &result);
  if (status != CTOOL_OK || result.type_count != T_COUNT ||
      result.member_count != ARRAY_COUNT(members)) {
    (void)fprintf(stderr, "records: layout operation failed\n");
    ctool_job_close(job);
    return 1;
  }

  expected_record = (expected_type_layout_t){
      8u, 4u, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE, CTOOL_FALSE};
  if (expect_type("records", &result, T_INNER, expected_record) != 0 ||
      expect_member("records", &result, 0u, 0u, 0u, 0u, 1u, 1u) != 0 ||
      expect_member("records", &result, 1u, 4u, 0u, 0u, 4u, 4u) != 0) {
    ctool_job_close(job);
    return 1;
  }
  if (expect_type("records", &result, T_CONST_INNER, expected_record) != 0 ||
      expect_type("records", &result, T_POINTER_TO_CONST_INNER,
                  (expected_type_layout_t){4u, 4u, CTOOL_TRUE, CTOOL_TRUE,
                                           CTOOL_FALSE, CTOOL_FALSE}) != 0 ||
      expect_type("records", &result, T_CONST_POINTER_TO_INNER,
                  (expected_type_layout_t){4u, 4u, CTOOL_TRUE, CTOOL_TRUE,
                                           CTOOL_FALSE, CTOOL_FALSE}) != 0) {
    ctool_job_close(job);
    return 1;
  }
  expected_record.size = 16u;
  if (expect_type("records", &result, T_OUTER, expected_record) != 0 ||
      expect_member("records", &result, 2u, 0u, 0u, 0u, 2u, 2u) != 0 ||
      expect_member("records", &result, 3u, 4u, 0u, 0u, 8u, 4u) != 0 ||
      expect_member("records", &result, 4u, 12u, 0u, 0u, 1u, 1u) != 0) {
    ctool_job_close(job);
    return 1;
  }
  expected_record.size = 8u;
  if (expect_type("records", &result, T_UNION, expected_record) != 0 ||
      expect_member("records", &result, 5u, 0u, 0u, 0u, 1u, 1u) != 0 ||
      expect_member("records", &result, 6u, 0u, 0u, 0u, 4u, 4u) != 0 ||
      expect_member("records", &result, 7u, 0u, 0u, 0u, 6u, 1u) != 0) {
    ctool_job_close(job);
    return 1;
  }
  expected_record.size = 4u;
  for (index = 0u; index < 4u; index++) {
    if (expect_member("records", &result, 8u + index, 0u, index * 8u, 8u, 4u,
                      4u) != 0) {
      ctool_job_close(job);
      return 1;
    }
  }
  if (expect_type("records", &result, T_BITS, expected_record) != 0) {
    ctool_job_close(job);
    return 1;
  }
  expected_record.size = 16u;
  expected_record.alignment = 16u;
  if (expect_type("records", &result, T_DESCRIPTOR, expected_record) != 0 ||
      expect_member("records", &result, 12u, 0u, 0u, 0u, 1u, 1u) != 0 ||
      expect_member("records", &result, 13u, 1u, 0u, 0u, 4u, 1u) != 0 ||
      expect_member("records", &result, 14u, 5u, 0u, 0u, 2u, 1u) != 0) {
    ctool_job_close(job);
    return 1;
  }
  expected_record.size = 32u;
  expected_record.alignment = 32u;
  if (expect_type("records", &result, T_ALIGNED, expected_record) != 0 ||
      expect_member("records", &result, 15u, 0u, 0u, 0u, 1u, 1u) != 0 ||
      expect_member("records", &result, 16u, 8u, 0u, 0u, 4u, 8u) != 0 ||
      expect_member("records", &result, 17u, 12u, 0u, 0u, 1u, 1u) != 0) {
    ctool_job_close(job);
    return 1;
  }
  if (expect_type("records", &result, T_FLEX_U8,
                  (expected_type_layout_t){0u, 1u, CTOOL_FALSE, CTOOL_TRUE,
                                           CTOOL_FALSE, CTOOL_FALSE}) != 0 ||
      expect_type("records", &result, T_FLEX_RECORD,
                  (expected_type_layout_t){4u, 4u, CTOOL_TRUE, CTOOL_TRUE,
                                           CTOOL_FALSE, CTOOL_FALSE}) != 0 ||
      expect_member("records", &result, 18u, 0u, 0u, 0u, 4u, 4u) != 0 ||
      expect_member("records", &result, 19u, 4u, 0u, 0u, 0u, 1u) != 0) {
    ctool_job_close(job);
    return 1;
  }
  if (expect_type("records", &result, T_CLASS,
                  (expected_type_layout_t){8u, 4u, CTOOL_TRUE, CTOOL_TRUE,
                                           CTOOL_FALSE, CTOOL_FALSE}) != 0 ||
      expect_member("records", &result, 20u, 0u, 0u, 0u, 1u, 1u) != 0 ||
      expect_member("records", &result, 21u, 4u, 0u, 0u, 4u, 4u) != 0) {
    ctool_job_close(job);
    return 1;
  }
  if (expect_type("records", &result, T_MEMBER_PACKED,
                  (expected_type_layout_t){8u, 2u, CTOOL_TRUE, CTOOL_TRUE,
                                           CTOOL_FALSE, CTOOL_FALSE}) != 0 ||
      expect_member("records", &result, 22u, 0u, 0u, 0u, 1u, 1u) != 0 ||
      expect_member("records", &result, 23u, 1u, 0u, 0u, 4u, 1u) != 0 ||
      expect_member("records", &result, 24u, 6u, 0u, 0u, 2u, 2u) != 0) {
    ctool_job_close(job);
    return 1;
  }
  if (expect_type("records", &result, T_PACK_TRANSITION,
                  (expected_type_layout_t){8u, 4u, CTOOL_TRUE, CTOOL_TRUE,
                                           CTOOL_FALSE, CTOOL_FALSE}) != 0 ||
      expect_member("records", &result, 25u, 0u, 0u, 0u, 1u, 1u) != 0 ||
      expect_member("records", &result, 26u, 4u, 0u, 0u, 4u, 4u) != 0) {
    ctool_job_close(job);
    return 1;
  }
  if (expect_type("records", &result, T_SELF_RECORD,
                  (expected_type_layout_t){4u, 4u, CTOOL_TRUE, CTOOL_TRUE,
                                           CTOOL_FALSE, CTOOL_FALSE}) != 0 ||
      expect_type("records", &result, T_SELF_POINTER,
                  (expected_type_layout_t){4u, 4u, CTOOL_TRUE, CTOOL_TRUE,
                                           CTOOL_FALSE, CTOOL_FALSE}) != 0 ||
      expect_member("records", &result, 27u, 0u, 0u, 0u, 4u, 4u) != 0) {
    ctool_job_close(job);
    return 1;
  }
  if (expect_type("records", &result, T_ALIGNED_BITS,
                  (expected_type_layout_t){16u, 8u, CTOOL_TRUE, CTOOL_TRUE,
                                           CTOOL_FALSE, CTOOL_FALSE}) != 0 ||
      expect_member("records", &result, 28u, 0u, 0u, 0u, 1u, 1u) != 0 ||
      expect_member("records", &result, 29u, 8u, 0u, 8u, 4u, 8u) != 0 ||
      expect_member("records", &result, 30u, 9u, 0u, 0u, 1u, 1u) != 0) {
    ctool_job_close(job);
    return 1;
  }
  if (expect_type("records", &result, T_UNNAMED_BIT_UNION,
                  (expected_type_layout_t){1u, 1u, CTOOL_TRUE, CTOOL_TRUE,
                                           CTOOL_FALSE, CTOOL_FALSE}) != 0 ||
      expect_member("records", &result, 31u, 0u, 0u, 8u, 4u, 4u) != 0) {
    ctool_job_close(job);
    return 1;
  }
  if (expect_type("records", &result, T_PACKED_BIT_UNION,
                  (expected_type_layout_t){1u, 1u, CTOOL_TRUE, CTOOL_TRUE,
                                           CTOOL_FALSE, CTOOL_FALSE}) != 0 ||
      expect_member("records", &result, 32u, 0u, 0u, 8u, 4u, 1u) != 0) {
    ctool_job_close(job);
    return 1;
  }
  if (expect_type("records", &result, T_ZERO_WIDTH_BITS,
                  (expected_type_layout_t){9u, 1u, CTOOL_TRUE, CTOOL_TRUE,
                                           CTOOL_FALSE, CTOOL_FALSE}) != 0 ||
      expect_member("records", &result, 33u, 0u, 0u, 0u, 1u, 1u) != 0 ||
      expect_member("records", &result, 34u, 8u, 0u, 0u, 4u, 8u) != 0 ||
      expect_member("records", &result, 35u, 8u, 0u, 0u, 1u, 1u) != 0) {
    ctool_job_close(job);
    return 1;
  }
  if (expect_type("records", &result, T_LOWER_ALIGNED,
                  (expected_type_layout_t){4u, 1u, CTOOL_TRUE, CTOOL_TRUE,
                                           CTOOL_TRUE, CTOOL_TRUE}) != 0 ||
      expect_type("records", &result, T_LOWER_RECORD,
                  (expected_type_layout_t){6u, 1u, CTOOL_TRUE, CTOOL_TRUE,
                                           CTOOL_FALSE, CTOOL_FALSE}) != 0 ||
      expect_member("records", &result, 36u, 0u, 0u, 0u, 1u, 1u) != 0 ||
      expect_member("records", &result, 37u, 1u, 0u, 0u, 4u, 1u) != 0 ||
      expect_member("records", &result, 38u, 5u, 0u, 0u, 1u, 1u) != 0) {
    ctool_job_close(job);
    return 1;
  }
  if (expect_type("records", &result, T_PROMOTED_INNER,
                  (expected_type_layout_t){4u, 4u, CTOOL_TRUE, CTOOL_TRUE,
                                           CTOOL_FALSE, CTOOL_FALSE}) != 0 ||
      expect_type("records", &result, T_PROMOTED_OUTER,
                  (expected_type_layout_t){4u, 4u, CTOOL_TRUE, CTOOL_TRUE,
                                           CTOOL_FALSE, CTOOL_FALSE}) != 0 ||
      expect_member("records", &result, 39u, 0u, 0u, 0u, 4u, 4u) != 0 ||
      expect_member("records", &result, 40u, 0u, 0u, 0u, 4u, 4u) != 0 ||
      expect_member("records", &result, 41u, 4u, 0u, 0u, 0u, 1u) != 0) {
    ctool_job_close(job);
    return 1;
  }

  ctool_job_close(job);
  (void)printf("records: ok\n");
  return 0;
}

static int expect_failure(ctool_job_t *job,
                          const ctool_c_layout_request_t *request,
                          ctool_status_t expected_status,
                          ctool_u32 expected_code, const char *expected_path,
                          ctool_u32 expected_line, ctool_u32 expected_column,
                          const char *expected_message,
                          ctool_u32 diagnostic_index) {
  ctool_c_layout_result_t result;
  ctool_arena_mark_t mark = ctool_arena_mark(ctool_job_arena(job));
  const ctool_diagnostic_t *diagnostic;
  ctool_status_t status;
  (void)memset(&result, 0xa5, sizeof(result));
  status = ctool_c_layout_types(job, request, &result);
  diagnostic = ctool_job_diagnostic(job, diagnostic_index);
  if (status != expected_status || result_is_zero(&result) == 0 ||
      ctool_job_diagnostic_count(job) != diagnostic_index + 1u ||
      diagnostic == NULL || diagnostic->severity != CTOOL_DIAG_ERROR ||
      diagnostic->code != expected_code ||
      !string_equal(diagnostic->path, expected_path) ||
      diagnostic->line != expected_line ||
      diagnostic->column != expected_column ||
      !string_equal(diagnostic->message, expected_message) ||
      !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
    (void)fprintf(stderr, "errors: failure case %u differs\n",
                  diagnostic_index);
    return 1;
  }
  return 0;
}

static int run_errors(void) {
  ctool_c_type_node_t nodes[4];
  ctool_c_type_node_t borrowed_node;
  ctool_c_record_member_t members[3];
  ctool_u32 parameter_types[1];
  ctool_c_layout_request_t request;
  ctool_c_layout_result_t result;
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_status_t status;

  if (open_job("errors", &adapter, &job) != 0) {
    return 1;
  }

  request = layout_request(NULL, 1u, NULL, 0u, "/request.c");
  request.location = source_location("/request.c", 3u, 5u);
  if (expect_failure(job, &request, CTOOL_ERR_INVALID_ARGUMENT,
                     CTOOL_C_TYPE_DIAG_INVALID_REQUEST, "/request.c", 3u, 5u,
                     "type array is missing", 0u) != 0) {
    ctool_job_close(job);
    return 1;
  }

  nodes[0] = type_node(CTOOL_C_TYPE_POINTER, "/physical/invalid-ref.c", 7u);
  nodes[0].location = source_location("/presumed/invalid-ref.c", 70u, 9u);
  nodes[0].physical_location =
      source_location("/physical/invalid-ref.c", 7u, 2u);
  nodes[0].referenced_type = 1u;
  borrowed_node = nodes[0];
  request = layout_request(nodes, 1u, NULL, 0u, "/presumed/invalid-ref.c");
  if (expect_failure(job, &request, CTOOL_ERR_INPUT,
                     CTOOL_C_TYPE_DIAG_INVALID_REFERENCE,
                     "/presumed/invalid-ref.c", 70u, 9u,
                     "type reference is out of range", 1u) != 0 ||
      memcmp(&nodes[0], &borrowed_node, sizeof(borrowed_node)) != 0) {
    (void)fprintf(stderr,
                  "errors: presumed/physical borrowed metadata changed\n");
    ctool_job_close(job);
    return 1;
  }

  nodes[0] = type_node(CTOOL_C_TYPE_RECORD, "/slice.c", 11u);
  nodes[0].record_complete = CTOOL_TRUE;
  nodes[0].first_member = 1u;
  nodes[0].member_count = 1u;
  members[0] = record_member("x", 0u, "/slice.c", 12u, 0u);
  request = layout_request(nodes, 1u, members, 1u, "/slice.c");
  if (expect_failure(job, &request, CTOOL_ERR_INPUT,
                     CTOOL_C_TYPE_DIAG_MEMBER_SLICE, "/slice.c", 11u, 1u,
                     "record member slice is out of range", 2u) != 0) {
    ctool_job_close(job);
    return 1;
  }

  nodes[0] = type_node(CTOOL_C_TYPE_UNSIGNED_CHAR, "/overlap.c", 1u);
  nodes[1] = type_node(CTOOL_C_TYPE_RECORD, "/overlap.c", 10u);
  nodes[1].record_complete = CTOOL_TRUE;
  nodes[1].first_member = 0u;
  nodes[1].member_count = 2u;
  nodes[2] = type_node(CTOOL_C_TYPE_RECORD, "/overlap.c", 20u);
  nodes[2].record_complete = CTOOL_TRUE;
  nodes[2].first_member = 1u;
  nodes[2].member_count = 1u;
  members[0] = record_member("a", 0u, "/overlap.c", 11u, 0u);
  members[1] = record_member("b", 0u, "/overlap.c", 21u, 0u);
  request = layout_request(nodes, 3u, members, 2u, "/overlap.c");
  if (expect_failure(job, &request, CTOOL_ERR_INPUT,
                     CTOOL_C_TYPE_DIAG_MEMBER_OVERLAP, "/overlap.c", 20u, 1u,
                     "record member slices overlap", 3u) != 0) {
    ctool_job_close(job);
    return 1;
  }

  nodes[0] = type_node(CTOOL_C_TYPE_UNSIGNED_CHAR, "/array.c", 1u);
  nodes[1] = type_node(CTOOL_C_TYPE_ARRAY, "/array.c", 8u);
  nodes[1].location.column = 4u;
  nodes[1].referenced_type = 0u;
  request = layout_request(nodes, 2u, NULL, 0u, "/array.c");
  if (expect_failure(job, &request, CTOOL_ERR_INPUT, CTOOL_C_TYPE_DIAG_ARRAY,
                     "/array.c", 8u, 4u, "array element count must be nonzero",
                     4u) != 0) {
    ctool_job_close(job);
    return 1;
  }

  nodes[0] = type_node(CTOOL_C_TYPE_UNSIGNED_CHAR, "/alignment.c", 1u);
  nodes[1] = type_node(CTOOL_C_TYPE_RECORD, "/alignment.c", 5u);
  nodes[1].record_complete = CTOOL_TRUE;
  nodes[1].member_count = 1u;
  members[0] = record_member("x", 0u, "/alignment.c", 6u, 3u);
  request = layout_request(nodes, 2u, members, 1u, "/alignment.c");
  if (expect_failure(job, &request, CTOOL_ERR_INPUT,
                     CTOOL_C_TYPE_DIAG_ALIGNMENT, "/alignment.c", 6u, 5u,
                     "pack alignment must be 0, 1, 2, 4, 8, or 16", 5u) != 0) {
    ctool_job_close(job);
    return 1;
  }

  nodes[0] = type_node(CTOOL_C_TYPE_UNSIGNED_INT, "/bitfield.c", 1u);
  nodes[1] = type_node(CTOOL_C_TYPE_RECORD, "/bitfield.c", 5u);
  nodes[1].record_complete = CTOOL_TRUE;
  nodes[1].member_count = 1u;
  members[0] = record_member("wide", 0u, "/bitfield.c", 9u, 0u);
  members[0].is_bit_field = CTOOL_TRUE;
  members[0].bit_width = 33u;
  request = layout_request(nodes, 2u, members, 1u, "/bitfield.c");
  if (expect_failure(job, &request, CTOOL_ERR_INPUT,
                     CTOOL_C_TYPE_DIAG_BIT_FIELD, "/bitfield.c", 9u, 5u,
                     "bit-field width exceeds its storage type", 6u) != 0) {
    ctool_job_close(job);
    return 1;
  }

  nodes[0] = type_node(CTOOL_C_TYPE_UNSIGNED_CHAR, "/flexible.c", 1u);
  nodes[1] = type_node(CTOOL_C_TYPE_ARRAY, "/flexible.c", 2u);
  nodes[1].referenced_type = 0u;
  nodes[1].array_bound_kind = CTOOL_C_ARRAY_UNSPECIFIED;
  nodes[2] = type_node(CTOOL_C_TYPE_RECORD, "/flexible.c", 3u);
  nodes[2].record_complete = CTOOL_TRUE;
  nodes[2].member_count = 2u;
  members[0] = record_member("data", 1u, "/flexible.c", 4u, 0u);
  members[1] = record_member("tail", 0u, "/flexible.c", 5u, 0u);
  request = layout_request(nodes, 3u, members, 2u, "/flexible.c");
  if (expect_failure(job, &request, CTOOL_ERR_INPUT,
                     CTOOL_C_TYPE_DIAG_FLEXIBLE_ARRAY, "/flexible.c", 4u, 5u,
                     "flexible array member must be last", 7u) != 0) {
    ctool_job_close(job);
    return 1;
  }

  nodes[0] = type_node(CTOOL_C_TYPE_RECORD, "/cycle.c", 2u);
  nodes[0].record_complete = CTOOL_TRUE;
  nodes[0].member_count = 1u;
  members[0] = record_member("self", 0u, "/cycle.c", 3u, 0u);
  request = layout_request(nodes, 1u, members, 1u, "/cycle.c");
  if (expect_failure(job, &request, CTOOL_ERR_INPUT, CTOOL_C_TYPE_DIAG_CYCLE,
                     "/cycle.c", 3u, 5u, "type graph contains a by-value cycle",
                     8u) != 0) {
    ctool_job_close(job);
    return 1;
  }

  nodes[0] = type_node(CTOOL_C_TYPE_RECORD, "/incomplete.c", 1u);
  nodes[0].record_complete = CTOOL_FALSE;
  nodes[1] = type_node(CTOOL_C_TYPE_RECORD, "/incomplete.c", 4u);
  nodes[1].record_complete = CTOOL_TRUE;
  nodes[1].member_count = 1u;
  members[0] = record_member("value", 0u, "/incomplete.c", 5u, 0u);
  request = layout_request(nodes, 2u, members, 1u, "/incomplete.c");
  if (expect_failure(job, &request, CTOOL_ERR_INPUT,
                     CTOOL_C_TYPE_DIAG_INCOMPLETE, "/incomplete.c", 5u, 5u,
                     "record member has incomplete type", 9u) != 0) {
    ctool_job_close(job);
    return 1;
  }

  nodes[0] = type_node(CTOOL_C_TYPE_UNSIGNED_INT, "/overflow.c", 1u);
  nodes[1] = type_node(CTOOL_C_TYPE_ARRAY, "/overflow.c", 6u);
  nodes[1].location.column = 7u;
  nodes[1].referenced_type = 0u;
  nodes[1].element_count = 0x40000000u;
  request = layout_request(nodes, 2u, NULL, 0u, "/overflow.c");
  if (expect_failure(job, &request, CTOOL_ERR_OVERFLOW,
                     CTOOL_C_TYPE_DIAG_OVERFLOW, "/overflow.c", 6u, 7u,
                     "type layout exceeds 32-bit size", 10u) != 0) {
    ctool_job_close(job);
    return 1;
  }

  nodes[0] = type_node(CTOOL_C_TYPE_SIGNED_INT, "/function-slice.c", 1u);
  nodes[1] = type_node(CTOOL_C_TYPE_FUNCTION, "/function-slice.c", 10u);
  nodes[1].location.column = 3u;
  nodes[1].referenced_type = 0u;
  nodes[1].has_prototype = CTOOL_TRUE;
  nodes[1].first_parameter = 1u;
  nodes[1].parameter_count = 1u;
  parameter_types[0] = 0u;
  request = layout_request(nodes, 2u, NULL, 0u, "/function-slice.c");
  request.parameter_types = parameter_types;
  request.parameter_type_count = ARRAY_COUNT(parameter_types);
  if (expect_failure(job, &request, CTOOL_ERR_INPUT,
                     CTOOL_C_TYPE_DIAG_INVALID_REFERENCE, "/function-slice.c",
                     10u, 3u, "function parameter slice is out of range",
                     11u) != 0) {
    ctool_job_close(job);
    return 1;
  }

  nodes[1] = type_node(CTOOL_C_TYPE_FUNCTION, "/function-ref.c", 12u);
  nodes[1].location.column = 4u;
  nodes[1].referenced_type = 0u;
  nodes[1].has_prototype = CTOOL_TRUE;
  nodes[1].parameter_count = 1u;
  parameter_types[0] = 2u;
  request = layout_request(nodes, 2u, NULL, 0u, "/function-ref.c");
  request.parameter_types = parameter_types;
  request.parameter_type_count = ARRAY_COUNT(parameter_types);
  if (expect_failure(job, &request, CTOOL_ERR_INPUT,
                     CTOOL_C_TYPE_DIAG_INVALID_REFERENCE, "/function-ref.c",
                     12u, 4u, "function parameter type is out of range",
                     12u) != 0) {
    ctool_job_close(job);
    return 1;
  }

  nodes[0] = type_node(CTOOL_C_TYPE_UNSIGNED_CHAR, "/variable-array.c", 1u);
  nodes[1] = type_node(CTOOL_C_TYPE_ARRAY, "/variable-array.c", 9u);
  nodes[1].location.column = 6u;
  nodes[1].referenced_type = 0u;
  nodes[1].array_bound_kind = CTOOL_C_ARRAY_VARIABLE;
  request = layout_request(nodes, 2u, NULL, 0u, "/variable-array.c");
  if (expect_failure(job, &request, CTOOL_ERR_UNSUPPORTED,
                     CTOOL_C_TYPE_DIAG_ARRAY, "/variable-array.c", 9u, 6u,
                     "variable array layout is unsupported", 13u) != 0) {
    ctool_job_close(job);
    return 1;
  }

  nodes[0] = type_node(CTOOL_C_TYPE_UNSIGNED_CHAR, "/orphan.c", 1u);
  members[0] = record_member("orphan", 0u, "/orphan.c", 7u, 0u);
  request = layout_request(nodes, 1u, members, 1u, "/orphan.c");
  if (expect_failure(job, &request, CTOOL_ERR_INPUT,
                     CTOOL_C_TYPE_DIAG_MEMBER_SLICE, "/orphan.c", 7u, 5u,
                     "record member is outside every record slice", 14u) != 0) {
    ctool_job_close(job);
    return 1;
  }

  nodes[0] = type_node(CTOOL_C_TYPE_BOOL, "/bool-bitfield.c", 1u);
  nodes[1] = type_node(CTOOL_C_TYPE_RECORD, "/bool-bitfield.c", 2u);
  nodes[1].record_complete = CTOOL_TRUE;
  nodes[1].member_count = 1u;
  members[0] = record_member("wide", 0u, "/bool-bitfield.c", 3u, 0u);
  members[0].is_bit_field = CTOOL_TRUE;
  members[0].bit_width = 2u;
  request = layout_request(nodes, 2u, members, 1u, "/bool-bitfield.c");
  if (expect_failure(job, &request, CTOOL_ERR_INPUT,
                     CTOOL_C_TYPE_DIAG_BIT_FIELD, "/bool-bitfield.c", 3u, 5u,
                     "bit-field width exceeds its storage type", 15u) != 0) {
    ctool_job_close(job);
    return 1;
  }

  nodes[0] = type_node(CTOOL_C_TYPE_UNSIGNED_CHAR, "/pack32.c", 1u);
  nodes[1] = type_node(CTOOL_C_TYPE_RECORD, "/pack32.c", 2u);
  nodes[1].record_complete = CTOOL_TRUE;
  nodes[1].member_count = 1u;
  members[0] = record_member("value", 0u, "/pack32.c", 3u, 32u);
  request = layout_request(nodes, 2u, members, 1u, "/pack32.c");
  if (expect_failure(job, &request, CTOOL_ERR_INPUT,
                     CTOOL_C_TYPE_DIAG_ALIGNMENT, "/pack32.c", 3u, 5u,
                     "pack alignment must be 0, 1, 2, 4, 8, or 16", 16u) != 0) {
    ctool_job_close(job);
    return 1;
  }

  nodes[0] = type_node(CTOOL_C_TYPE_UNSIGNED_CHAR, "/array-kind.c", 1u);
  nodes[1] = type_node(CTOOL_C_TYPE_ARRAY, "/array-kind.c", 2u);
  nodes[1].referenced_type = 0u;
  nodes[1].element_count = 1u;
  nodes[1].array_bound_kind = (ctool_c_array_bound_kind_t)0;
  request = layout_request(nodes, 2u, NULL, 0u, "/array-kind.c");
  if (expect_failure(job, &request, CTOOL_ERR_INPUT, CTOOL_C_TYPE_DIAG_ARRAY,
                     "/array-kind.c", 2u, 1u, "array bound kind is invalid",
                     17u) != 0) {
    ctool_job_close(job);
    return 1;
  }

  nodes[0] = type_node(CTOOL_C_TYPE_UNSIGNED_CHAR, "/union-flex.c", 1u);
  nodes[1] = type_node(CTOOL_C_TYPE_ARRAY, "/union-flex.c", 2u);
  nodes[1].referenced_type = 0u;
  nodes[1].array_bound_kind = CTOOL_C_ARRAY_UNSPECIFIED;
  nodes[2] = type_node(CTOOL_C_TYPE_RECORD, "/union-flex.c", 3u);
  nodes[2].record_kind = CTOOL_C_RECORD_UNION;
  nodes[2].record_complete = CTOOL_TRUE;
  nodes[2].member_count = 2u;
  members[0] = record_member("tag", 0u, "/union-flex.c", 4u, 0u);
  members[1] = record_member("data", 1u, "/union-flex.c", 5u, 0u);
  request = layout_request(nodes, 3u, members, 2u, "/union-flex.c");
  if (expect_failure(job, &request, CTOOL_ERR_INPUT,
                     CTOOL_C_TYPE_DIAG_FLEXIBLE_ARRAY, "/union-flex.c", 5u, 5u,
                     "flexible array member requires a structure", 18u) != 0) {
    ctool_job_close(job);
    return 1;
  }

  nodes[0] = type_node(CTOOL_C_TYPE_RECORD, "/atomic-record.c", 2u);
  nodes[0].record_complete = CTOOL_TRUE;
  nodes[0].qualifiers = CTOOL_C_QUAL_ATOMIC;
  request = layout_request(nodes, 1u, NULL, 0u, "/atomic-record.c");
  if (expect_failure(job, &request, CTOOL_ERR_UNSUPPORTED,
                     CTOOL_C_TYPE_DIAG_INVALID_TYPE, "/atomic-record.c", 2u, 1u,
                     "atomic aggregate layout is unsupported", 19u) != 0) {
    ctool_job_close(job);
    return 1;
  }

  nodes[0] = type_node(CTOOL_C_TYPE_RECORD, "/atomic-aligned-record.c", 2u);
  nodes[0].record_complete = CTOOL_TRUE;
  nodes[1] = type_node(CTOOL_C_TYPE_ALIGNED, "/atomic-aligned-record.c", 5u);
  nodes[1].location.column = 3u;
  nodes[1].referenced_type = 0u;
  nodes[1].explicit_alignment = 16u;
  nodes[1].qualifiers = CTOOL_C_QUAL_ATOMIC;
  request = layout_request(nodes, 2u, NULL, 0u, "/atomic-aligned-record.c");
  if (expect_failure(job, &request, CTOOL_ERR_UNSUPPORTED,
                     CTOOL_C_TYPE_DIAG_INVALID_TYPE, "/atomic-aligned-record.c",
                     5u, 3u,
                     "atomic aligned type requires a supported scalar, "
                     "pointer, or enum type",
                     20u) != 0) {
    ctool_job_close(job);
    return 1;
  }

  nodes[0] = type_node(CTOOL_C_TYPE_UNSIGNED_CHAR, "/aligned-zero.c", 1u);
  nodes[1] = type_node(CTOOL_C_TYPE_ALIGNED, "/aligned-zero.c", 5u);
  nodes[1].location.column = 7u;
  nodes[1].referenced_type = 0u;
  request = layout_request(nodes, 2u, NULL, 0u, "/aligned-zero.c");
  if (expect_failure(job, &request, CTOOL_ERR_INPUT,
                     CTOOL_C_TYPE_DIAG_ALIGNMENT, "/aligned-zero.c", 5u, 7u,
                     "aligned type requires a power-of-two alignment",
                     21u) != 0) {
    ctool_job_close(job);
    return 1;
  }

  nodes[0] = type_node(CTOOL_C_TYPE_UNSIGNED_CHAR, "/aligned-nonpower.c", 1u);
  nodes[1] = type_node(CTOOL_C_TYPE_ALIGNED, "/aligned-nonpower.c", 6u);
  nodes[1].location.column = 8u;
  nodes[1].referenced_type = 0u;
  nodes[1].explicit_alignment = 3u;
  request = layout_request(nodes, 2u, NULL, 0u, "/aligned-nonpower.c");
  if (expect_failure(job, &request, CTOOL_ERR_INPUT,
                     CTOOL_C_TYPE_DIAG_ALIGNMENT, "/aligned-nonpower.c", 6u, 8u,
                     "aligned type requires a power-of-two alignment",
                     22u) != 0) {
    ctool_job_close(job);
    return 1;
  }

  nodes[0] = type_node(CTOOL_C_TYPE_VOID, "/aligned-void.c", 1u);
  nodes[1] = type_node(CTOOL_C_TYPE_ALIGNED, "/aligned-void.c", 9u);
  nodes[1].location.column = 4u;
  nodes[1].referenced_type = 0u;
  nodes[1].explicit_alignment = 64u;
  request = layout_request(nodes, 2u, NULL, 0u, "/aligned-void.c");
  if (expect_failure(job, &request, CTOOL_ERR_INPUT,
                     CTOOL_C_TYPE_DIAG_INCOMPLETE, "/aligned-void.c", 9u, 4u,
                     "aligned type requires a complete object type",
                     23u) != 0) {
    ctool_job_close(job);
    return 1;
  }

  nodes[0] = type_node(CTOOL_C_TYPE_SIGNED_INT, "/aligned-array.c", 1u);
  nodes[1] = type_node(CTOOL_C_TYPE_ALIGNED, "/aligned-array.c", 4u);
  nodes[1].referenced_type = 0u;
  nodes[1].explicit_alignment = 16u;
  nodes[2] = type_node(CTOOL_C_TYPE_ARRAY, "/aligned-array.c", 7u);
  nodes[2].location.column = 6u;
  nodes[2].referenced_type = 1u;
  nodes[2].element_count = 2u;
  request = layout_request(nodes, 3u, NULL, 0u, "/aligned-array.c");
  if (expect_failure(job, &request, CTOOL_ERR_INPUT, CTOOL_C_TYPE_DIAG_ARRAY,
                     "/aligned-array.c", 7u, 6u,
                     "array element size is not a multiple of its alignment",
                     24u) != 0) {
    ctool_job_close(job);
    return 1;
  }

  nodes[0] = type_node(CTOOL_C_TYPE_SIGNED_INT, "/qualified-empty.c", 1u);
  nodes[1] = type_node(CTOOL_C_TYPE_QUALIFIED, "/qualified-empty.c", 8u);
  nodes[1].location.column = 3u;
  nodes[1].referenced_type = 0u;
  request = layout_request(nodes, 2u, NULL, 0u, "/qualified-empty.c");
  if (expect_failure(job, &request, CTOOL_ERR_INPUT,
                     CTOOL_C_TYPE_DIAG_INVALID_TYPE, "/qualified-empty.c", 8u,
                     3u, "qualified type requires at least one qualifier",
                     25u) != 0) {
    ctool_job_close(job);
    return 1;
  }

  nodes[0] = type_node(CTOOL_C_TYPE_RECORD, "/qualified-atomic-record.c", 2u);
  nodes[0].record_complete = CTOOL_TRUE;
  nodes[1] =
      type_node(CTOOL_C_TYPE_QUALIFIED, "/qualified-atomic-record.c", 6u);
  nodes[1].location.column = 4u;
  nodes[1].referenced_type = 0u;
  nodes[1].qualifiers = CTOOL_C_QUAL_ATOMIC;
  request =
      layout_request(nodes, 2u, NULL, 0u, "/qualified-atomic-record.c");
  if (expect_failure(
          job, &request, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_TYPE_DIAG_INVALID_TYPE, "/qualified-atomic-record.c", 6u,
          4u,
          "atomic qualified type requires a supported scalar, pointer, or "
          "enum type",
          26u) != 0) {
    ctool_job_close(job);
    return 1;
  }

  nodes[0] = type_node(CTOOL_C_TYPE_SIGNED_INT, "/recovery.c", 1u);
  request = layout_request(nodes, 1u, NULL, 0u, "/recovery.c");
  status = ctool_c_layout_types(job, &request, &result);
  if (status != CTOOL_OK || ctool_job_diagnostic_count(job) != 27u ||
      expect_type("errors", &result, 0u,
                  (expected_type_layout_t){4u, 4u, CTOOL_TRUE, CTOOL_TRUE,
                                           CTOOL_TRUE, CTOOL_TRUE}) != 0) {
    (void)fprintf(stderr, "errors: same-job recovery failed\n");
    ctool_job_close(job);
    return 1;
  }

  ctool_job_close(job);
  (void)printf("errors: ok\n");
  return 0;
}

static int type_layouts_equal(const ctool_c_type_layout_t *left,
                              const ctool_c_type_layout_t *right) {
  return left->size == right->size && left->alignment == right->alignment &&
                 left->is_complete_object == right->is_complete_object &&
                 left->is_object == right->is_object &&
                 left->is_integer == right->is_integer &&
                 left->is_signed == right->is_signed
             ? 1
             : 0;
}

static int member_layouts_equal(const ctool_c_member_layout_t *left,
                                const ctool_c_member_layout_t *right) {
  return left->byte_offset == right->byte_offset &&
                 left->bit_offset == right->bit_offset &&
                 left->bit_width == right->bit_width &&
                 left->size == right->size &&
                 left->alignment == right->alignment
             ? 1
             : 0;
}

static int check_active_shapes(void) {
  enum {
    P_U8,
    P_CHAR,
    P_I32,
    P_U32,
    P_ENUM,
    P_VOID,
    P_VOID_POINTER,
    P_U8_512,
    P_CHAR_32,
    P_CPU_CONTEXT,
    P_PROCESS,
    P_COUNT
  };
  enum { S_VOID, S_FUNCTION, S_FUNCTION_POINTER, S_TABLE, S_COUNT };
  enum {
    C_U8,
    C_U32,
    C_U64,
    C_VOID,
    C_VOID_POINTER,
    C_FUNCTION,
    C_FUNCTION_POINTER,
    C_U8_74,
    C_RECORD,
    C_ALIGNED,
    C_COUNT
  };
  enum { D_U8, D_U16, D_U64, D_RECORD, D_COUNT };
  ctool_c_type_node_t process_types[P_COUNT];
  ctool_c_record_member_t process_members[30];
  ctool_c_type_node_t syscall_types[S_COUNT];
  ctool_c_record_member_t syscall_members[103];
  ctool_c_type_node_t percpu_types[C_COUNT];
  ctool_c_record_member_t percpu_members[19];
  ctool_c_type_node_t e1000_types[D_COUNT];
  ctool_c_record_member_t e1000_members[6];
  ctool_c_layout_request_t request;
  ctool_c_layout_result_t process_result;
  ctool_c_layout_result_t syscall_result;
  ctool_c_layout_result_t percpu_result;
  ctool_c_layout_result_t e1000_result;
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_status_t status;
  ctool_u32 index;

  if (open_job("adversarial", &adapter, &job) != 0) {
    return 1;
  }
  (void)memset(process_types, 0, sizeof(process_types));
  (void)memset(process_members, 0, sizeof(process_members));
  process_types[P_U8] =
      type_node(CTOOL_C_TYPE_UNSIGNED_CHAR, "/kernel/core/process.h", 63u);
  process_types[P_CHAR] =
      type_node(CTOOL_C_TYPE_CHAR, "/kernel/core/process.h", 75u);
  process_types[P_I32] =
      type_node(CTOOL_C_TYPE_SIGNED_INT, "/kernel/core/process.h", 25u);
  process_types[P_U32] =
      type_node(CTOOL_C_TYPE_UNSIGNED_INT, "/kernel/core/process.h", 49u);
  process_types[P_ENUM] =
      type_node(CTOOL_C_TYPE_ENUM, "/kernel/core/process.h", 25u);
  process_types[P_ENUM].referenced_type = P_I32;
  process_types[P_VOID] =
      type_node(CTOOL_C_TYPE_VOID, "/kernel/core/process.h", 68u);
  process_types[P_VOID_POINTER] =
      type_node(CTOOL_C_TYPE_POINTER, "/kernel/core/process.h", 68u);
  process_types[P_VOID_POINTER].referenced_type = P_VOID;
  process_types[P_U8_512] =
      type_node(CTOOL_C_TYPE_ARRAY, "/kernel/core/process.h", 66u);
  process_types[P_U8_512].referenced_type = P_U8;
  process_types[P_U8_512].element_count = 512u;
  process_types[P_CHAR_32] =
      type_node(CTOOL_C_TYPE_ARRAY, "/kernel/core/process.h", 75u);
  process_types[P_CHAR_32].referenced_type = P_CHAR;
  process_types[P_CHAR_32].element_count = 32u;
  process_types[P_CPU_CONTEXT] =
      type_node(CTOOL_C_TYPE_RECORD, "/kernel/core/process.h", 55u);
  process_types[P_CPU_CONTEXT].record_complete = CTOOL_TRUE;
  process_types[P_CPU_CONTEXT].first_member = 0u;
  process_types[P_CPU_CONTEXT].member_count = 16u;
  for (index = 0u; index < 16u; index++) {
    process_members[index] = record_member(
        "register", P_U32, "/kernel/core/process.h", 56u + index / 4u, 0u);
  }
  process_types[P_PROCESS] =
      type_node(CTOOL_C_TYPE_RECORD, "/kernel/core/process.h", 62u);
  process_types[P_PROCESS].record_complete = CTOOL_TRUE;
  process_types[P_PROCESS].first_member = 16u;
  process_types[P_PROCESS].member_count = 14u;
  process_members[16] =
      record_member("pid", P_U32, "/kernel/core/process.h", 63u, 0u);
  process_members[17] =
      record_member("state", P_ENUM, "/kernel/core/process.h", 64u, 0u);
  process_members[18] = record_member("context", P_CPU_CONTEXT,
                                      "/kernel/core/process.h", 65u, 0u);
  process_members[19] =
      record_member("fp_state", P_U8_512, "/kernel/core/process.h", 66u, 0u);
  process_members[19].explicit_alignment = 16u;
  process_members[20] = record_member("stack_base", P_VOID_POINTER,
                                      "/kernel/core/process.h", 68u, 0u);
  process_members[21] =
      record_member("stack_size", P_U32, "/kernel/core/process.h", 69u, 0u);
  process_members[22] =
      record_member("image_base", P_U32, "/kernel/core/process.h", 70u, 0u);
  process_members[23] =
      record_member("image_size", P_U32, "/kernel/core/process.h", 71u, 0u);
  process_members[24] = record_member("image_ownership", P_ENUM,
                                      "/kernel/core/process.h", 72u, 0u);
  process_members[25] = record_member("image_lease_generation", P_U32,
                                      "/kernel/core/process.h", 73u, 0u);
  process_members[26] =
      record_member("domain", P_ENUM, "/kernel/core/process.h", 74u, 0u);
  process_members[27] =
      record_member("name", P_CHAR_32, "/kernel/core/process.h", 75u, 0u);
  process_members[28] =
      record_member("on_cpu", P_U8, "/kernel/core/process.h", 76u, 0u);
  process_members[29] =
      record_member("last_cpu", P_U8, "/kernel/core/process.h", 79u, 0u);

  request =
      layout_request(process_types, P_COUNT, process_members,
                     ARRAY_COUNT(process_members), "/kernel/core/process.h");
  status = ctool_c_layout_types(job, &request, &process_result);
  if (status != CTOOL_OK ||
      expect_type("adversarial", &process_result, P_CPU_CONTEXT,
                  (expected_type_layout_t){64u, 4u, CTOOL_TRUE, CTOOL_TRUE,
                                           CTOOL_FALSE, CTOOL_FALSE}) != 0 ||
      expect_type("adversarial", &process_result, P_PROCESS,
                  (expected_type_layout_t){656u, 16u, CTOOL_TRUE, CTOOL_TRUE,
                                           CTOOL_FALSE, CTOOL_FALSE}) != 0 ||
      expect_member("adversarial", &process_result, 19u, 80u, 0u, 0u, 512u,
                    16u) != 0 ||
      expect_member("adversarial", &process_result, 27u, 620u, 0u, 0u, 32u,
                    1u) != 0 ||
      expect_member("adversarial", &process_result, 29u, 653u, 0u, 0u, 1u,
                    1u) != 0) {
    (void)fprintf(stderr, "adversarial: process_t ABI oracle differs\n");
    ctool_job_close(job);
    return 1;
  }

  (void)memset(syscall_types, 0, sizeof(syscall_types));
  (void)memset(syscall_members, 0, sizeof(syscall_members));
  syscall_types[S_VOID] =
      type_node(CTOOL_C_TYPE_VOID, "/kernel/core/syscall.c", 174u);
  syscall_types[S_FUNCTION] =
      type_node(CTOOL_C_TYPE_FUNCTION, "/kernel/core/syscall.c", 174u);
  syscall_types[S_FUNCTION].referenced_type = S_VOID;
  syscall_types[S_FUNCTION].has_prototype = CTOOL_TRUE;
  syscall_types[S_FUNCTION_POINTER] =
      type_node(CTOOL_C_TYPE_POINTER, "/kernel/core/syscall.c", 174u);
  syscall_types[S_FUNCTION_POINTER].referenced_type = S_FUNCTION;
  syscall_types[S_TABLE] =
      type_node(CTOOL_C_TYPE_RECORD, "/user/cupid.h", 100u);
  syscall_types[S_TABLE].record_complete = CTOOL_TRUE;
  syscall_types[S_TABLE].member_count = ARRAY_COUNT(syscall_members);
  for (index = 0u; index < ARRAY_COUNT(syscall_members); index++) {
    syscall_members[index] = record_member("function", S_FUNCTION_POINTER,
                                           "/user/cupid.h", 101u + index, 0u);
  }
  request = layout_request(syscall_types, S_COUNT, syscall_members,
                           ARRAY_COUNT(syscall_members), "/user/cupid.h");
  status = ctool_c_layout_types(job, &request, &syscall_result);
  if (status != CTOOL_OK ||
      expect_type("adversarial", &syscall_result, S_TABLE,
                  (expected_type_layout_t){412u, 4u, CTOOL_TRUE, CTOOL_TRUE,
                                           CTOOL_FALSE, CTOOL_FALSE}) != 0) {
    (void)fprintf(stderr, "adversarial: syscall table ABI oracle differs\n");
    ctool_job_close(job);
    return 1;
  }
  for (index = 0u; index < ARRAY_COUNT(syscall_members); index++) {
    if (expect_member("adversarial", &syscall_result, index, index * 4u, 0u, 0u,
                      4u, 4u) != 0) {
      ctool_job_close(job);
      return 1;
    }
  }
  if (expect_type("adversarial", &process_result, P_PROCESS,
                  (expected_type_layout_t){656u, 16u, CTOOL_TRUE, CTOOL_TRUE,
                                           CTOOL_FALSE, CTOOL_FALSE}) != 0 ||
      expect_member("adversarial", &process_result, 19u, 80u, 0u, 0u, 512u,
                    16u) != 0) {
    (void)fprintf(stderr,
                  "adversarial: later operation invalidated prior result\n");
    ctool_job_close(job);
    return 1;
  }

  (void)memset(percpu_types, 0, sizeof(percpu_types));
  (void)memset(percpu_members, 0, sizeof(percpu_members));
  percpu_types[C_U8] =
      type_node(CTOOL_C_TYPE_UNSIGNED_CHAR, "/kernel/smp/percpu.h", 12u);
  percpu_types[C_U32] =
      type_node(CTOOL_C_TYPE_UNSIGNED_INT, "/kernel/smp/percpu.h", 18u);
  percpu_types[C_U64] =
      type_node(CTOOL_C_TYPE_UNSIGNED_LONG_LONG, "/kernel/smp/percpu.h", 19u);
  percpu_types[C_VOID] =
      type_node(CTOOL_C_TYPE_VOID, "/kernel/smp/percpu.h", 23u);
  percpu_types[C_VOID_POINTER] =
      type_node(CTOOL_C_TYPE_POINTER, "/kernel/smp/percpu.h", 11u);
  percpu_types[C_VOID_POINTER].referenced_type = C_VOID;
  percpu_types[C_FUNCTION] =
      type_node(CTOOL_C_TYPE_FUNCTION, "/kernel/smp/percpu.h", 23u);
  percpu_types[C_FUNCTION].referenced_type = C_VOID;
  percpu_types[C_FUNCTION].has_prototype = CTOOL_TRUE;
  percpu_types[C_FUNCTION_POINTER] =
      type_node(CTOOL_C_TYPE_POINTER, "/kernel/smp/percpu.h", 23u);
  percpu_types[C_FUNCTION_POINTER].referenced_type = C_FUNCTION;
  percpu_types[C_U8_74] =
      type_node(CTOOL_C_TYPE_ARRAY, "/kernel/smp/percpu.h", 32u);
  percpu_types[C_U8_74].referenced_type = C_U8;
  percpu_types[C_U8_74].element_count = 74u;
  percpu_types[C_RECORD] =
      type_node(CTOOL_C_TYPE_RECORD, "/kernel/smp/percpu.h", 10u);
  percpu_types[C_RECORD].record_complete = CTOOL_TRUE;
  percpu_types[C_RECORD].member_count = ARRAY_COUNT(percpu_members);
  percpu_types[C_ALIGNED] =
      type_node(CTOOL_C_TYPE_ALIGNED, "/kernel/smp/percpu.h", 33u);
  percpu_types[C_ALIGNED].referenced_type = C_RECORD;
  percpu_types[C_ALIGNED].explicit_alignment = 64u;

  percpu_members[0] = record_member("self_ptr", C_VOID_POINTER,
                                    "/kernel/smp/percpu.h", 11u, 0u);
  percpu_members[1] =
      record_member("cpu_id", C_U8, "/kernel/smp/percpu.h", 12u, 0u);
  percpu_members[2] =
      record_member("apic_id", C_U8, "/kernel/smp/percpu.h", 13u, 0u);
  percpu_members[3] =
      record_member("bootstrap", C_U8, "/kernel/smp/percpu.h", 14u, 0u);
  percpu_members[4] =
      record_member("online", C_U8, "/kernel/smp/percpu.h", 15u, 0u);
  percpu_members[5] =
      record_member("current", C_VOID_POINTER, "/kernel/smp/percpu.h", 16u, 0u);
  percpu_members[6] =
      record_member("idle", C_VOID_POINTER, "/kernel/smp/percpu.h", 17u, 0u);
  percpu_members[7] =
      record_member("bkl_depth", C_U32, "/kernel/smp/percpu.h", 18u, 0u);
  percpu_members[8] =
      record_member("preempt_count", C_U64, "/kernel/smp/percpu.h", 19u, 0u);
  percpu_members[9] = record_member("idle_stack_top", C_VOID_POINTER,
                                    "/kernel/smp/percpu.h", 20u, 0u);
  percpu_members[10] =
      record_member("bkl_eflags_saved", C_U32, "/kernel/smp/percpu.h", 21u, 0u);
  percpu_members[11] =
      record_member("current_pid", C_U32, "/kernel/smp/percpu.h", 22u, 0u);
  percpu_members[12] = record_member("call_fn", C_FUNCTION_POINTER,
                                     "/kernel/smp/percpu.h", 23u, 0u);
  percpu_members[13] = record_member("call_arg", C_VOID_POINTER,
                                     "/kernel/smp/percpu.h", 24u, 0u);
  percpu_members[14] =
      record_member("call_pending", C_U8, "/kernel/smp/percpu.h", 25u, 0u);
  percpu_members[15] =
      record_member("call_done", C_U8, "/kernel/smp/percpu.h", 26u, 0u);
  percpu_members[16] = record_member("reschedule_pending", C_U8,
                                     "/kernel/smp/percpu.h", 27u, 0u);
  percpu_members[17] =
      record_member("interrupt_depth", C_U8, "/kernel/smp/percpu.h", 28u, 0u);
  percpu_members[18] =
      record_member("_pad", C_U8_74, "/kernel/smp/percpu.h", 32u, 0u);

  request = layout_request(percpu_types, C_COUNT, percpu_members,
                           ARRAY_COUNT(percpu_members), "/kernel/smp/percpu.h");
  status = ctool_c_layout_types(job, &request, &percpu_result);
  if (status != CTOOL_OK ||
      expect_type("adversarial", &percpu_result, C_RECORD,
                  (expected_type_layout_t){128u, 4u, CTOOL_TRUE, CTOOL_TRUE,
                                           CTOOL_FALSE, CTOOL_FALSE}) != 0 ||
      expect_type("adversarial", &percpu_result, C_ALIGNED,
                  (expected_type_layout_t){128u, 64u, CTOOL_TRUE, CTOOL_TRUE,
                                           CTOOL_FALSE, CTOOL_FALSE}) != 0 ||
      expect_member("adversarial", &percpu_result, 8u, 20u, 0u, 0u, 8u, 4u) !=
          0 ||
      expect_member("adversarial", &percpu_result, 12u, 40u, 0u, 0u, 4u, 4u) !=
          0 ||
      expect_member("adversarial", &percpu_result, 18u, 52u, 0u, 0u, 74u, 1u) !=
          0) {
    (void)fprintf(stderr, "adversarial: per_cpu_t ABI oracle differs\n");
    ctool_job_close(job);
    return 1;
  }

  (void)memset(e1000_types, 0, sizeof(e1000_types));
  (void)memset(e1000_members, 0, sizeof(e1000_members));
  e1000_types[D_U8] =
      type_node(CTOOL_C_TYPE_UNSIGNED_CHAR, "/drivers/e1000.c", 37u);
  e1000_types[D_U16] =
      type_node(CTOOL_C_TYPE_UNSIGNED_SHORT, "/drivers/e1000.c", 35u);
  e1000_types[D_U64] =
      type_node(CTOOL_C_TYPE_UNSIGNED_LONG_LONG, "/drivers/e1000.c", 34u);
  e1000_types[D_RECORD] =
      type_node(CTOOL_C_TYPE_RECORD, "/drivers/e1000.c", 33u);
  e1000_types[D_RECORD].record_complete = CTOOL_TRUE;
  e1000_types[D_RECORD].record_packed = CTOOL_TRUE;
  e1000_types[D_RECORD].explicit_alignment = 16u;
  e1000_types[D_RECORD].member_count = ARRAY_COUNT(e1000_members);
  e1000_members[0] = record_member("addr", D_U64, "/drivers/e1000.c", 34u, 0u);
  e1000_members[1] = record_member("len", D_U16, "/drivers/e1000.c", 35u, 0u);
  e1000_members[2] = record_member("csum", D_U16, "/drivers/e1000.c", 36u, 0u);
  e1000_members[3] = record_member("status", D_U8, "/drivers/e1000.c", 37u, 0u);
  e1000_members[4] = record_member("err", D_U8, "/drivers/e1000.c", 38u, 0u);
  e1000_members[5] =
      record_member("special", D_U16, "/drivers/e1000.c", 39u, 0u);

  request = layout_request(e1000_types, D_COUNT, e1000_members,
                           ARRAY_COUNT(e1000_members), "/drivers/e1000.c");
  status = ctool_c_layout_types(job, &request, &e1000_result);
  if (status != CTOOL_OK ||
      expect_type("adversarial", &e1000_result, D_RECORD,
                  (expected_type_layout_t){16u, 16u, CTOOL_TRUE, CTOOL_TRUE,
                                           CTOOL_FALSE, CTOOL_FALSE}) != 0 ||
      expect_member("adversarial", &e1000_result, 0u, 0u, 0u, 0u, 8u, 1u) !=
          0 ||
      expect_member("adversarial", &e1000_result, 1u, 8u, 0u, 0u, 2u, 1u) !=
          0 ||
      expect_member("adversarial", &e1000_result, 2u, 10u, 0u, 0u, 2u, 1u) !=
          0 ||
      expect_member("adversarial", &e1000_result, 3u, 12u, 0u, 0u, 1u, 1u) !=
          0 ||
      expect_member("adversarial", &e1000_result, 4u, 13u, 0u, 0u, 1u, 1u) !=
          0 ||
      expect_member("adversarial", &e1000_result, 5u, 14u, 0u, 0u, 2u, 1u) !=
          0) {
    (void)fprintf(stderr, "adversarial: e1000_rx_desc_t ABI oracle differs\n");
    ctool_job_close(job);
    return 1;
  }
  if (expect_type("adversarial", &process_result, P_PROCESS,
                  (expected_type_layout_t){656u, 16u, CTOOL_TRUE, CTOOL_TRUE,
                                           CTOOL_FALSE, CTOOL_FALSE}) != 0 ||
      expect_type("adversarial", &syscall_result, S_TABLE,
                  (expected_type_layout_t){412u, 4u, CTOOL_TRUE, CTOOL_TRUE,
                                           CTOOL_FALSE, CTOOL_FALSE}) != 0) {
    (void)fprintf(stderr,
                  "adversarial: aligned operation invalidated prior result\n");
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  return 0;
}

static int check_repeated_success(void) {
  const ctool_u32 array_count = 128u;
  const ctool_u32 record_count = 128u;
  const ctool_u32 type_count = 1u + array_count + record_count;
  const ctool_u32 repeat_count = 24u;
  ctool_c_type_node_t *nodes = (ctool_c_type_node_t *)0;
  ctool_c_record_member_t *members = (ctool_c_record_member_t *)0;
  ctool_c_layout_result_t results[24];
  ctool_c_layout_request_t request;
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_status_t status;
  ctool_u32 index;
  ctool_u32 prior;
  int failed = 0;

  nodes = (ctool_c_type_node_t *)calloc((size_t)type_count, sizeof(*nodes));
  members =
      (ctool_c_record_member_t *)calloc((size_t)record_count, sizeof(*members));
  if (nodes == NULL || members == NULL) {
    (void)fprintf(stderr, "adversarial: repeated fixture allocation failed\n");
    failed = 1;
    goto cleanup;
  }
  nodes[0] = type_node(CTOOL_C_TYPE_UNSIGNED_CHAR, "/repeated.c", 1u);
  for (index = 0u; index < array_count; index++) {
    ctool_u32 type = 1u + index;
    nodes[type] = type_node(CTOOL_C_TYPE_ARRAY, "/repeated.c", type + 1u);
    nodes[type].referenced_type = 0u;
    nodes[type].element_count = 1u;
  }
  for (index = 0u; index < record_count; index++) {
    ctool_u32 type = 1u + array_count + index;
    nodes[type] = type_node(CTOOL_C_TYPE_RECORD, "/repeated.c", type + 1u);
    nodes[type].record_complete = CTOOL_TRUE;
    nodes[type].first_member = index;
    nodes[type].member_count = 1u;
    members[index] =
        record_member("value", 1u + index, "/repeated.c", index + 2u, 0u);
  }
  if (open_limited_job("adversarial", 4096u, 256u * 1024u, &adapter, &job) !=
      0) {
    failed = 1;
    goto cleanup;
  }
  request =
      layout_request(nodes, type_count, members, record_count, "/repeated.c");
  for (index = 0u; index < repeat_count; index++) {
    status = ctool_c_layout_types(job, &request, &results[index]);
    if (status != CTOOL_OK || results[index].type_count != type_count ||
        results[index].member_count != record_count) {
      (void)fprintf(stderr,
                    "adversarial: repeated success stopped at operation %u\n",
                    index);
      failed = 1;
      break;
    }
    for (prior = 0u; prior <= index; prior++) {
      ctool_u32 last_type = type_count - 1u;
      if (results[prior].types[last_type].size != 1u ||
          results[prior].types[last_type].alignment != 1u ||
          results[prior].members[record_count - 1u].byte_offset != 0u) {
        (void)fprintf(
            stderr,
            "adversarial: operation %u invalidated preserved result %u\n",
            index, prior);
        failed = 1;
        break;
      }
    }
    if (failed != 0) {
      break;
    }
  }
  if (failed == 0 && ctool_job_diagnostic_count(job) != 0u) {
    (void)fprintf(stderr,
                  "adversarial: repeated success emitted diagnostics\n");
    failed = 1;
  }

cleanup:
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  free(members);
  free(nodes);
  return failed;
}

static int run_adversarial(void) {
  if (check_active_shapes() != 0 || check_repeated_success() != 0) {
    return 1;
  }
  (void)printf("adversarial: ok\n");
  return 0;
}

static int check_bool_wrapper_scale(void) {
  const ctool_u32 wrapper_count = 4096u;
  const ctool_u32 field_count = 4096u;
  const ctool_u32 bool_type = wrapper_count;
  const ctool_u32 record_type = wrapper_count + 1u;
  const ctool_u32 type_count = wrapper_count + 2u;
  ctool_c_type_node_t *nodes = (ctool_c_type_node_t *)0;
  ctool_c_type_node_t *node_copy = (ctool_c_type_node_t *)0;
  ctool_c_record_member_t *members = (ctool_c_record_member_t *)0;
  ctool_c_record_member_t *member_copy = (ctool_c_record_member_t *)0;
  ctool_c_layout_request_t request;
  ctool_c_layout_result_t first;
  ctool_c_layout_result_t second;
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_status_t status;
  ctool_u32 index;
  int failed = 0;

  nodes = (ctool_c_type_node_t *)calloc((size_t)type_count, sizeof(*nodes));
  node_copy =
      (ctool_c_type_node_t *)calloc((size_t)type_count, sizeof(*node_copy));
  members =
      (ctool_c_record_member_t *)calloc((size_t)field_count, sizeof(*members));
  member_copy = (ctool_c_record_member_t *)calloc((size_t)field_count,
                                                  sizeof(*member_copy));
  if (nodes == NULL || node_copy == NULL || members == NULL ||
      member_copy == NULL) {
    (void)fprintf(stderr, "scale: bool-wrapper fixture allocation failed\n");
    failed = 1;
    goto cleanup;
  }
  for (index = 0u; index < wrapper_count; index++) {
    nodes[index] =
        type_node(CTOOL_C_TYPE_ALIGNED, "/bool-wrapper-scale.c", index + 1u);
    nodes[index].referenced_type = index + 1u;
    nodes[index].explicit_alignment = 1u;
  }
  nodes[bool_type] =
      type_node(CTOOL_C_TYPE_BOOL, "/bool-wrapper-scale.c", bool_type + 1u);
  nodes[record_type] =
      type_node(CTOOL_C_TYPE_RECORD, "/bool-wrapper-scale.c", record_type + 1u);
  nodes[record_type].record_complete = CTOOL_TRUE;
  nodes[record_type].member_count = field_count;
  for (index = 0u; index < field_count; index++) {
    members[index] = record_member("flag", 0u, "/bool-wrapper-scale.c",
                                   record_type + index + 2u, 0u);
    members[index].is_bit_field = CTOOL_TRUE;
    members[index].bit_width = 1u;
  }
  (void)memcpy(node_copy, nodes, (size_t)type_count * sizeof(*nodes));
  (void)memcpy(member_copy, members, (size_t)field_count * sizeof(*members));
  if (open_job("scale", &adapter, &job) != 0) {
    failed = 1;
    goto cleanup;
  }
  request = layout_request(nodes, type_count, members, field_count,
                           "/bool-wrapper-scale.c");
  status = ctool_c_layout_types(job, &request, &first);
  if (status == CTOOL_OK) {
    status = ctool_c_layout_types(job, &request, &second);
  }
  if (status != CTOOL_OK || first.type_count != type_count ||
      second.type_count != type_count || first.member_count != field_count ||
      second.member_count != field_count ||
      memcmp(nodes, node_copy, (size_t)type_count * sizeof(*nodes)) != 0 ||
      memcmp(members, member_copy, (size_t)field_count * sizeof(*members)) !=
          0 ||
      ctool_job_diagnostic_count(job) != 0u) {
    (void)fprintf(stderr,
                  "scale: bool-wrapper operation shape or ownership differs\n");
    failed = 1;
  }
  if (failed == 0) {
    for (index = 0u; index < type_count; index++) {
      if (type_layouts_equal(&first.types[index], &second.types[index]) == 0) {
        (void)fprintf(
            stderr, "scale: bool-wrapper type %u is nondeterministic\n", index);
        failed = 1;
        break;
      }
    }
  }
  if (failed == 0 &&
      (first.types[0].size != 1u || first.types[0].alignment != 1u ||
       first.types[0].is_integer != CTOOL_TRUE ||
       first.types[record_type].size != field_count / 8u ||
       first.types[record_type].alignment != 1u)) {
    (void)fprintf(stderr, "scale: bool-wrapper type layout differs\n");
    failed = 1;
  }
  if (failed == 0) {
    for (index = 0u; index < field_count; index++) {
      const ctool_c_member_layout_t *layout = &first.members[index];
      if (member_layouts_equal(layout, &second.members[index]) == 0 ||
          layout->byte_offset != index / 8u ||
          layout->bit_offset != index % 8u || layout->bit_width != 1u ||
          layout->size != 1u || layout->alignment != 1u) {
        (void)fprintf(stderr, "scale: bool-wrapper field %u differs\n", index);
        failed = 1;
        break;
      }
    }
  }

cleanup:
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  free(member_copy);
  free(members);
  free(node_copy);
  free(nodes);
  return failed;
}

static int run_scale(void) {
  const ctool_u32 array_count = 4096u;
  const ctool_u32 record_count = 4096u;
  const ctool_u32 type_count = 1u + array_count + record_count;
  ctool_c_type_node_t *nodes = (ctool_c_type_node_t *)0;
  ctool_c_type_node_t *node_copy = (ctool_c_type_node_t *)0;
  ctool_c_record_member_t *members = (ctool_c_record_member_t *)0;
  ctool_c_record_member_t *member_copy = (ctool_c_record_member_t *)0;
  ctool_c_layout_request_t request;
  ctool_c_layout_result_t first;
  ctool_c_layout_result_t second;
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_status_t status;
  ctool_u32 index;
  int failed = 0;

  nodes = (ctool_c_type_node_t *)calloc((size_t)type_count, sizeof(*nodes));
  node_copy =
      (ctool_c_type_node_t *)calloc((size_t)type_count, sizeof(*node_copy));
  members =
      (ctool_c_record_member_t *)calloc((size_t)record_count, sizeof(*members));
  member_copy = (ctool_c_record_member_t *)calloc((size_t)record_count,
                                                  sizeof(*member_copy));
  if (nodes == NULL || node_copy == NULL || members == NULL ||
      member_copy == NULL) {
    (void)fprintf(stderr, "scale: fixture allocation failed\n");
    failed = 1;
    goto cleanup;
  }
  nodes[0] = type_node(CTOOL_C_TYPE_UNSIGNED_CHAR, "/scale.c", 1u);
  for (index = 0u; index < array_count; index++) {
    ctool_u32 type_index = 1u + index;
    nodes[type_index] =
        type_node(CTOOL_C_TYPE_ARRAY, "/scale.c", type_index + 1u);
    nodes[type_index].referenced_type =
        index + 1u < array_count ? type_index + 1u : 0u;
    nodes[type_index].element_count = 1u;
  }
  for (index = 0u; index < record_count; index++) {
    ctool_u32 type_index = 1u + array_count + index;
    nodes[type_index] =
        type_node(CTOOL_C_TYPE_RECORD, "/scale.c", type_index + 1u);
    nodes[type_index].record_complete = CTOOL_TRUE;
    nodes[type_index].first_member = index;
    nodes[type_index].member_count = 1u;
    members[index] =
        record_member("value", 1u + index, "/scale.c", index + 2u, 0u);
  }
  (void)memcpy(node_copy, nodes, (size_t)type_count * sizeof(*nodes));
  (void)memcpy(member_copy, members, (size_t)record_count * sizeof(*members));
  if (open_job("scale", &adapter, &job) != 0) {
    failed = 1;
    goto cleanup;
  }
  request =
      layout_request(nodes, type_count, members, record_count, "/scale.c");
  status = ctool_c_layout_types(job, &request, &first);
  if (status == CTOOL_OK) {
    status = ctool_c_layout_types(job, &request, &second);
  }
  if (status != CTOOL_OK || first.type_count != type_count ||
      second.type_count != type_count || first.member_count != record_count ||
      second.member_count != record_count ||
      memcmp(nodes, node_copy, (size_t)type_count * sizeof(*nodes)) != 0 ||
      memcmp(members, member_copy, (size_t)record_count * sizeof(*members)) !=
          0) {
    (void)fprintf(stderr,
                  "scale: operation shape or input ownership differs\n");
    failed = 1;
  }
  if (failed == 0) {
    for (index = 0u; index < type_count; index++) {
      if (type_layouts_equal(&first.types[index], &second.types[index]) == 0) {
        (void)fprintf(stderr, "scale: type %u is nondeterministic\n", index);
        failed = 1;
        break;
      }
    }
  }
  if (failed == 0) {
    for (index = 0u; index < record_count; index++) {
      ctool_u32 type_index = 1u + array_count + index;
      if (member_layouts_equal(&first.members[index], &second.members[index]) ==
              0 ||
          first.members[index].byte_offset != 0u ||
          first.types[type_index].size != 1u ||
          first.types[type_index].alignment != 1u) {
        (void)fprintf(stderr, "scale: record %u differs\n", index);
        failed = 1;
        break;
      }
    }
  }
  ctool_job_close(job);
  job = (ctool_job_t *)0;

cleanup:
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  free(member_copy);
  free(members);
  free(node_copy);
  free(nodes);
  if (failed != 0) {
    return 1;
  }
  if (check_bool_wrapper_scale() != 0) {
    return 1;
  }
  (void)printf("scale: ok\n");
  return 0;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    (void)fprintf(stderr, "usage: cupidc-type-contract "
                          "scalars|fat16|records|errors|scale|adversarial\n");
    return 2;
  }
  if (strcmp(argv[1], "scalars") == 0) {
    return run_scalars();
  }
  if (strcmp(argv[1], "fat16") == 0) {
    return run_fat16();
  }
  if (strcmp(argv[1], "records") == 0) {
    return run_records();
  }
  if (strcmp(argv[1], "errors") == 0) {
    return run_errors();
  }
  if (strcmp(argv[1], "scale") == 0) {
    return run_scale();
  }
  if (strcmp(argv[1], "adversarial") == 0) {
    return run_adversarial();
  }
  (void)fprintf(stderr, "unknown mode: %s\n", argv[1]);
  return 2;
}
