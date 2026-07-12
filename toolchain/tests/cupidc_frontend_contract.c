#include "ctool.h"
#include "ctool_host.h"
#include "cupidc_frontend.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_COUNT(VALUES)                                                    \
  ((ctool_u32)(sizeof(VALUES) / sizeof((VALUES)[0])))

typedef enum {
  ACTIVE_PROFILE = 1,
  ACTIVE_INCLUDE_ROOT,
  ACTIVE_MACRO,
  ACTIVE_FORCED_INCLUDE,
  ACTIVE_CASE,
  ACTIVE_GENERATED_CASE,
  ACTIVE_INCLUDE_ONLY,
  ACTIVE_NON_ROOT,
  ACTIVE_DEFERRED_HOSTED
} active_kind_t;

typedef struct {
  active_kind_t kind;
  const char *profile;
  const char *first;
  const char *second;
  ctool_u32 value;
  ctool_bool flag;
  ctool_bool hosted_environment;
} active_row_t;

static const active_row_t active_rows[] = {
#define CUPIDC_PP_PROFILE(NAME, MODE, GNU_BOOL, HOSTED_BOOL)                   \
  {ACTIVE_PROFILE, #NAME, NULL, NULL, (ctool_u32)(MODE), (GNU_BOOL),           \
   (HOSTED_BOOL)},
#define CUPIDC_PP_INCLUDE_ROOT(NAME, PATH, FORMS)                              \
  {ACTIVE_INCLUDE_ROOT, #NAME, (PATH), NULL, (FORMS), CTOOL_FALSE,             \
   CTOOL_FALSE},
#define CUPIDC_PP_MACRO(NAME, MACRO_NAME, REPLACEMENT)                        \
  {ACTIVE_MACRO, #NAME, (MACRO_NAME), (REPLACEMENT), 0u, CTOOL_FALSE,          \
   CTOOL_FALSE},
#define CUPIDC_PP_FORCED_INCLUDE(NAME, PATH)                                  \
  {ACTIVE_FORCED_INCLUDE, #NAME, (PATH), NULL, 0u, CTOOL_FALSE, CTOOL_FALSE},
#define CUPIDC_PP_ACTIVE_CASE(NAME, PATH)                                     \
  {ACTIVE_CASE, #NAME, (PATH), NULL, 0u, CTOOL_FALSE, CTOOL_FALSE},
#define CUPIDC_PP_GENERATED_CASE(NAME, PATH)                                  \
  {ACTIVE_GENERATED_CASE, #NAME, (PATH), NULL, 0u, CTOOL_FALSE, CTOOL_FALSE},
#define CUPIDC_PP_INCLUDE_ONLY(PATH, OWNER)                                   \
  {ACTIVE_INCLUDE_ONLY, NULL, (PATH), (OWNER), 0u, CTOOL_FALSE, CTOOL_FALSE},
#define CUPIDC_PP_NON_ROOT(PATH, REASON)                                      \
  {ACTIVE_NON_ROOT, NULL, (PATH), (REASON), 0u, CTOOL_FALSE, CTOOL_FALSE},
#define CUPIDC_PP_DEFERRED_HOSTED(PATH, REASON)                               \
  {ACTIVE_DEFERRED_HOSTED, NULL, (PATH), (REASON), 0u, CTOOL_FALSE,            \
   CTOOL_FALSE},
#include "cupidc_pp_active_cases.inc"
#undef CUPIDC_PP_PROFILE
#undef CUPIDC_PP_INCLUDE_ROOT
#undef CUPIDC_PP_MACRO
#undef CUPIDC_PP_FORCED_INCLUDE
#undef CUPIDC_PP_ACTIVE_CASE
#undef CUPIDC_PP_GENERATED_CASE
#undef CUPIDC_PP_INCLUDE_ONLY
#undef CUPIDC_PP_NON_ROOT
#undef CUPIDC_PP_DEFERRED_HOSTED
};

typedef struct {
  const char *name;
  ctool_c_binding_kind_t kind;
  const char *path;
  ctool_u32 line;
  ctool_u64 integer_bits;
} binding_oracle_t;

static const binding_oracle_t binding_oracles[] = {
    {"false", CTOOL_C_BINDING_ENUMERATOR, "/kernel/core/types.h", 8u, 0u},
    {"true", CTOOL_C_BINDING_ENUMERATOR, "/kernel/core/types.h", 8u, 1u},
    {"bool", CTOOL_C_BINDING_TYPEDEF, "/kernel/core/types.h", 8u, 0u},
    {"uint8_t", CTOOL_C_BINDING_TYPEDEF, "/kernel/core/types.h", 11u, 0u},
    {"uint16_t", CTOOL_C_BINDING_TYPEDEF, "/kernel/core/types.h", 12u, 0u},
    {"uint32_t", CTOOL_C_BINDING_TYPEDEF, "/kernel/core/types.h", 13u, 0u},
    {"uint64_t", CTOOL_C_BINDING_TYPEDEF, "/kernel/core/types.h", 14u, 0u},
    {"size_t", CTOOL_C_BINDING_TYPEDEF, "/kernel/core/types.h", 15u, 0u},
    {"int8_t", CTOOL_C_BINDING_TYPEDEF, "/kernel/core/types.h", 17u, 0u},
    {"int16_t", CTOOL_C_BINDING_TYPEDEF, "/kernel/core/types.h", 18u, 0u},
    {"int32_t", CTOOL_C_BINDING_TYPEDEF, "/kernel/core/types.h", 19u, 0u},
    {"int64_t", CTOOL_C_BINDING_TYPEDEF, "/kernel/core/types.h", 20u, 0u},
    {"KEY_UP", CTOOL_C_BINDING_ENUMERATOR, "/kernel/core/types.h", 27u, 0u},
    {"KEY_DOWN", CTOOL_C_BINDING_ENUMERATOR, "/kernel/core/types.h", 28u, 1u},
    {"KEY_HELD", CTOOL_C_BINDING_ENUMERATOR, "/kernel/core/types.h", 29u, 2u},
    {"key_state_t", CTOOL_C_BINDING_TYPEDEF, "/kernel/core/types.h", 30u, 0u},
    {"key_event_t", CTOOL_C_BINDING_TYPEDEF, "/kernel/core/types.h", 38u, 0u},
    {"keyboard_buffer_t", CTOOL_C_BINDING_TYPEDEF, "/kernel/core/types.h", 46u, 0u},
    {"keyboard_state_t", CTOOL_C_BINDING_TYPEDEF, "/kernel/core/types.h", 54u, 0u},
    {"timer_state_t", CTOOL_C_BINDING_TYPEDEF, "/kernel/core/types.h", 62u, 0u},
    {"timer_measure_t", CTOOL_C_BINDING_TYPEDEF, "/kernel/core/types.h", 68u, 0u},
    {"block_device_t", CTOOL_C_BINDING_TYPEDEF, "/kernel/fs/blockdev.h", 15u, 0u},
    {"blkdev_init", CTOOL_C_BINDING_FUNCTION, "/kernel/fs/blockdev.h", 17u, 0u},
    {"blkdev_register", CTOOL_C_BINDING_FUNCTION, "/kernel/fs/blockdev.h", 18u, 0u},
    {"blkdev_get", CTOOL_C_BINDING_FUNCTION, "/kernel/fs/blockdev.h", 19u, 0u},
    {"blkdev_count", CTOOL_C_BINDING_FUNCTION, "/kernel/fs/blockdev.h", 20u, 0u},
    {"blkdev_read", CTOOL_C_BINDING_FUNCTION, "/kernel/fs/blockdev.h", 21u, 0u},
    {"blkdev_write", CTOOL_C_BINDING_FUNCTION, "/kernel/fs/blockdev.h", 22u, 0u},
    {"mbr_partition_t", CTOOL_C_BINDING_TYPEDEF, "/kernel/fs/fat16.h", 35u, 0u},
    {"mbr_t", CTOOL_C_BINDING_TYPEDEF, "/kernel/fs/fat16.h", 41u, 0u},
    {"fat16_boot_sector_t", CTOOL_C_BINDING_TYPEDEF, "/kernel/fs/fat16.h", 58u, 0u},
    {"fat16_dir_entry_t", CTOOL_C_BINDING_TYPEDEF, "/kernel/fs/fat16.h", 74u, 0u},
    {"fat16_fs_t", CTOOL_C_BINDING_TYPEDEF, "/kernel/fs/fat16.h", 90u, 0u},
    {"fat16_file_t", CTOOL_C_BINDING_TYPEDEF, "/kernel/fs/fat16.h", 100u, 0u},
    {"fat16_enum_callback_t", CTOOL_C_BINDING_TYPEDEF, "/kernel/fs/fat16.h", 104u, 0u},
    {"fat16_init", CTOOL_C_BINDING_FUNCTION, "/kernel/fs/fat16.h", 108u, 0u},
    {"fat16_is_initialized", CTOOL_C_BINDING_FUNCTION, "/kernel/fs/fat16.h", 109u, 0u},
    {"fat16_open", CTOOL_C_BINDING_FUNCTION, "/kernel/fs/fat16.h", 110u, 0u},
    {"fat16_read", CTOOL_C_BINDING_FUNCTION, "/kernel/fs/fat16.h", 111u, 0u},
    {"fat16_close", CTOOL_C_BINDING_FUNCTION, "/kernel/fs/fat16.h", 112u, 0u},
    {"fat16_list_root", CTOOL_C_BINDING_FUNCTION, "/kernel/fs/fat16.h", 113u, 0u},
    {"fat16_write_file", CTOOL_C_BINDING_FUNCTION, "/kernel/fs/fat16.h", 114u, 0u},
    {"fat16_delete_file", CTOOL_C_BINDING_FUNCTION, "/kernel/fs/fat16.h", 115u, 0u},
    {"fat16_set_output", CTOOL_C_BINDING_FUNCTION, "/kernel/fs/fat16.h", 116u, 0u},
    {"fat16_enumerate_root", CTOOL_C_BINDING_FUNCTION, "/kernel/fs/fat16.h", 117u, 0u},
    {"fat16_mkdir", CTOOL_C_BINDING_FUNCTION, "/kernel/fs/fat16.h", 118u, 0u},
    {"fat16_is_dir", CTOOL_C_BINDING_FUNCTION, "/kernel/fs/fat16.h", 119u, 0u},
    {"fat16_enumerate_subdir", CTOOL_C_BINDING_FUNCTION, "/kernel/fs/fat16.h", 120u, 0u},
    {"fat16_total_bytes", CTOOL_C_BINDING_FUNCTION, "/kernel/fs/fat16.h", 124u, 0u},
    {"fat16_free_bytes", CTOOL_C_BINDING_FUNCTION, "/kernel/fs/fat16.h", 125u, 0u}};

typedef struct {
  const char *binding;
  ctool_u32 size;
  ctool_u32 alignment;
  const char *const *member_names;
  const ctool_u32 *member_offsets;
  ctool_u32 member_count;
  ctool_u32 pack_alignment;
} record_oracle_t;

static const char *const key_event_names[] = {
    "scancode", "character", "pressed", "timestamp"};
static const ctool_u32 key_event_offsets[] = {0u, 1u, 4u, 8u};
static const char *const keyboard_buffer_names[] = {
    "events", "head", "tail", "count"};
static const ctool_u32 keyboard_buffer_offsets[] = {0u, 3072u, 3073u,
                                                    3074u};
static const char *const keyboard_state_names[] = {
    "key_states", "modifier_states", "last_keypress_time", "buffer"};
static const ctool_u32 keyboard_state_offsets[] = {0u, 1024u, 1056u, 2080u};
static const char *const timer_state_names[] = {
    "ticks", "frequency", "ms_per_tick", "is_calibrated"};
static const ctool_u32 timer_state_offsets[] = {0u, 8u, 12u, 16u};
static const char *const timer_measure_names[] = {"start_tick", "duration_ms"};
static const ctool_u32 timer_measure_offsets[] = {0u, 8u};
static const char *const block_device_names[] = {
    "name", "sector_count", "sector_size", "driver_data", "read", "write"};
static const ctool_u32 block_device_offsets[] = {0u, 4u, 8u, 12u, 16u, 20u};
static const char *const mbr_partition_names[] = {
    "status", "chs_start", "type", "chs_end", "lba_start", "sector_count"};
static const ctool_u32 mbr_partition_offsets[] = {0u, 1u, 4u, 5u, 8u, 12u};
static const char *const mbr_names[] = {"boot_code", "partitions", "signature"};
static const ctool_u32 mbr_offsets[] = {0u, 446u, 510u};
static const char *const boot_sector_names[] = {
    "jump", "oem", "bytes_per_sector", "sectors_per_cluster",
    "reserved_sectors", "num_fats", "root_dir_entries", "total_sectors_16",
    "media_type", "sectors_per_fat", "sectors_per_track", "num_heads",
    "hidden_sectors", "total_sectors_32"};
static const ctool_u32 boot_sector_offsets[] = {
    0u, 3u, 11u, 13u, 14u, 16u, 17u, 19u, 21u, 22u, 24u, 26u, 28u, 32u};
static const char *const dir_entry_names[] = {
    "filename", "ext", "attributes", "reserved", "create_time_tenths",
    "create_time", "create_date", "access_date", "first_cluster_high",
    "modify_time", "modify_date", "first_cluster", "file_size"};
static const ctool_u32 dir_entry_offsets[] = {
    0u, 8u, 11u, 12u, 13u, 14u, 16u, 18u, 20u, 22u, 24u, 26u, 28u};
static const char *const fat_fs_names[] = {
    "partition_lba", "fat_start", "root_dir_start", "data_start",
    "bytes_per_sector", "sectors_per_cluster", "reserved_sectors",
    "num_fats", "root_dir_entries", "total_sectors", "sectors_per_fat"};
static const ctool_u32 fat_fs_offsets[] = {0u, 4u, 8u, 12u, 16u, 18u,
                                          20u, 22u, 24u, 28u, 32u};
static const char *const fat_file_names[] = {
    "first_cluster", "file_size", "position", "is_open", "cached_cluster",
    "cached_cluster_index", "cache_valid"};
static const ctool_u32 fat_file_offsets[] = {0u, 4u, 8u, 12u, 14u, 16u, 20u};

static const record_oracle_t record_oracles[] = {
    {"key_event_t", 12u, 4u, key_event_names, key_event_offsets,
     ARRAY_COUNT(key_event_names), 0u},
    {"keyboard_buffer_t", 3076u, 4u, keyboard_buffer_names,
     keyboard_buffer_offsets, ARRAY_COUNT(keyboard_buffer_names), 0u},
    {"keyboard_state_t", 5156u, 4u, keyboard_state_names,
     keyboard_state_offsets, ARRAY_COUNT(keyboard_state_names), 0u},
    {"timer_state_t", 20u, 4u, timer_state_names, timer_state_offsets,
     ARRAY_COUNT(timer_state_names), 0u},
    {"timer_measure_t", 16u, 4u, timer_measure_names, timer_measure_offsets,
     ARRAY_COUNT(timer_measure_names), 0u},
    {"block_device_t", 24u, 4u, block_device_names, block_device_offsets,
     ARRAY_COUNT(block_device_names), 0u},
    {"mbr_partition_t", 16u, 1u, mbr_partition_names, mbr_partition_offsets,
     ARRAY_COUNT(mbr_partition_names), 1u},
    {"mbr_t", 512u, 1u, mbr_names, mbr_offsets, ARRAY_COUNT(mbr_names), 1u},
    {"fat16_boot_sector_t", 36u, 1u, boot_sector_names, boot_sector_offsets,
     ARRAY_COUNT(boot_sector_names), 1u},
    {"fat16_dir_entry_t", 32u, 1u, dir_entry_names, dir_entry_offsets,
     ARRAY_COUNT(dir_entry_names), 1u},
    {"fat16_fs_t", 36u, 4u, fat_fs_names, fat_fs_offsets,
     ARRAY_COUNT(fat_fs_names), 0u},
    {"fat16_file_t", 24u, 4u, fat_file_names, fat_file_offsets,
     ARRAY_COUNT(fat_file_names), 0u}};

static int string_equal(ctool_string_t actual, const char *expected) {
  size_t expected_size = strlen(expected);
  return actual.size == (ctool_u32)expected_size &&
                 (expected_size == 0u ||
                  memcmp(actual.data, expected, expected_size) == 0)
             ? 1
             : 0;
}

static int location_matches(const ctool_c_pp_location_t *location,
                            const char *path, ctool_u32 line) {
  return location != NULL && string_equal(location->path, path) != 0 &&
                 location->line == line && location->column != 0u
             ? 1
             : 0;
}

static int dual_location_matches(const ctool_c_pp_location_t *presumed,
                                 const ctool_c_pp_location_t *physical,
                                 const char *path, ctool_u32 line) {
  return location_matches(presumed, path, line) != 0 &&
                 location_matches(physical, path, line) != 0 &&
                 presumed->column == physical->column
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

static int unit_is_zero(const ctool_c_translation_unit_t *unit) {
  return unit->graph.types == NULL && unit->graph.type_count == 0u &&
                 unit->graph.members == NULL && unit->graph.member_count == 0u &&
                 unit->graph.parameter_types == NULL &&
                 unit->graph.parameter_type_count == 0u &&
                 unit->layout.types == NULL && unit->layout.type_count == 0u &&
                 unit->layout.members == NULL &&
                 unit->layout.member_count == 0u && unit->bindings == NULL &&
                 unit->binding_count == 0u && unit->tags == NULL &&
                 unit->tag_count == 0u && unit->parameters == NULL &&
                 unit->parameter_count == 0u &&
                 unit->block_bindings == NULL &&
                 unit->block_binding_count == 0u &&
                 unit->function_definitions == NULL &&
                 unit->function_definition_count == 0u &&
                 unit->statements == NULL && unit->statement_count == 0u &&
                 unit->statement_children == NULL &&
                 unit->statement_child_count == 0u &&
                 unit->expressions == NULL && unit->expression_count == 0u &&
                 unit->expression_children == NULL &&
                 unit->expression_child_count == 0u
             ? 1
             : 0;
}

static int build_kernel_profile(
    ctool_c_pp_request_t *request, ctool_c_pp_include_root_t *include_roots,
    ctool_c_pp_macro_action_t *macro_actions,
    ctool_path_t *forced_includes) {
  ctool_u32 row_index;
  ctool_u32 profile_count = 0u;

  (void)memset(request, 0, sizeof(*request));
  for (row_index = 0u; row_index < ARRAY_COUNT(active_rows); row_index++) {
    const active_row_t *row = &active_rows[row_index];
    if (row->profile == NULL || strcmp(row->profile, "KERNEL_I386") != 0) {
      continue;
    }
    if (row->kind == ACTIVE_PROFILE) {
      request->mode = (ctool_c_pp_mode_t)row->value;
      request->gnu_extensions = row->flag;
      request->hosted_environment = row->hosted_environment;
      profile_count++;
    } else if (row->kind == ACTIVE_INCLUDE_ROOT) {
      include_roots[request->include_root_count].directory.text =
          ctool_string(row->first);
      include_roots[request->include_root_count].forms = row->value;
      request->include_root_count++;
    } else if (row->kind == ACTIVE_MACRO) {
      macro_actions[request->macro_action_count].kind =
          CTOOL_C_PP_MACRO_DEFINE;
      macro_actions[request->macro_action_count].name =
          ctool_string(row->first);
      macro_actions[request->macro_action_count].replacement =
          ctool_string(row->second);
      request->macro_action_count++;
    } else if (row->kind == ACTIVE_FORCED_INCLUDE) {
      forced_includes[request->forced_include_count].text =
          ctool_string(row->first);
      request->forced_include_count++;
    }
  }
  request->include_roots = include_roots;
  request->macro_actions = macro_actions;
  request->forced_includes = forced_includes;
  if (profile_count != 1u || request->mode != CTOOL_C_PP_MODE_C11 ||
      request->gnu_extensions != CTOOL_TRUE ||
      request->hosted_environment != CTOOL_FALSE ||
      request->include_root_count != 18u || request->macro_action_count != 8u ||
      request->forced_include_count != 0u) {
    (void)fprintf(stderr, "fat16: KERNEL_I386 profile differs\n");
    return 1;
  }
  return 0;
}

static int open_job(const char *mode, const char *host_root,
                    ctool_u32 arena_bytes, ctool_host_adapter_t *adapter,
                    ctool_job_t **job_out) {
  ctool_limits_t limits = ctool_default_limits();
  ctool_job_config_t config;
  ctool_status_t status;

  limits.arena_bytes = arena_bytes;
  status = ctool_host_adapter_init(adapter, host_root);
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "%s: host adapter: %s\n", mode,
                  ctool_status_name(status));
    return 1;
  }
  config = ctool_host_job_config(adapter, limits);
  status = ctool_job_open(&config, job_out);
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "%s: job open: %s\n", mode,
                  ctool_status_name(status));
    return 1;
  }
  return 0;
}

static const ctool_c_binding_t *
find_binding(const ctool_c_translation_unit_t *unit, const char *name) {
  ctool_u32 index;
  for (index = 0u; index < unit->binding_count; index++) {
    if (string_equal(unit->bindings[index].name, name) != 0) {
      return &unit->bindings[index];
    }
  }
  return NULL;
}

static ctool_u32 find_binding_index(const ctool_c_translation_unit_t *unit,
                                    const char *name) {
  ctool_u32 index;
  for (index = 0u; index < unit->binding_count; index++) {
    if (string_equal(unit->bindings[index].name, name) != 0) {
      return index;
    }
  }
  return CTOOL_C_AST_NONE;
}

static const ctool_c_type_node_t *
type_node(const ctool_c_translation_unit_t *unit, ctool_u32 type) {
  return type < unit->graph.type_count ? &unit->graph.types[type] : NULL;
}

static const ctool_c_type_layout_t *
type_layout(const ctool_c_translation_unit_t *unit, ctool_u32 type) {
  return type < unit->layout.type_count ? &unit->layout.types[type] : NULL;
}

static const ctool_c_record_member_t *
find_record_member(const ctool_c_translation_unit_t *unit,
                   const ctool_c_type_node_t *record, const char *name,
                   ctool_u32 *member_index_out) {
  ctool_u32 index;
  for (index = 0u; index < record->member_count; index++) {
    ctool_u32 member_index = record->first_member + index;
    if (member_index >= unit->graph.member_count) {
      return NULL;
    }
    if (string_equal(unit->graph.members[member_index].name, name) != 0) {
      *member_index_out = member_index;
      return &unit->graph.members[member_index];
    }
  }
  return NULL;
}

static const ctool_c_type_node_t *
function_from_pointer(const ctool_c_translation_unit_t *unit,
                      ctool_u32 pointer_type) {
  const ctool_c_type_node_t *pointer = type_node(unit, pointer_type);
  if (pointer == NULL || pointer->kind != CTOOL_C_TYPE_POINTER) {
    return NULL;
  }
  pointer = type_node(unit, pointer->referenced_type);
  return pointer != NULL && pointer->kind == CTOOL_C_TYPE_FUNCTION ? pointer
                                                                  : NULL;
}

static int validate_fat16_tape(const ctool_c_pp_result_t *tape) {
  ctool_u32 kinds[CTOOL_C_PP_TOKEN_CUPID_EXE + 1u];
  ctool_u32 types_count = 0u;
  ctool_u32 block_count = 0u;
  ctool_u32 fat_count = 0u;
  ctool_u32 pack_zero_count = 0u;
  ctool_u32 pack_one_count = 0u;
  ctool_u32 index;

  (void)memset(kinds, 0, sizeof(kinds));
  if (tape->tokens == NULL || tape->token_count != 710u) {
    (void)fprintf(stderr, "fat16: expected 710 preprocessing tokens\n");
    return 1;
  }
  for (index = 0u; index < tape->token_count; index++) {
    const ctool_c_pp_token_t *token = &tape->tokens[index];
    const char *expected_path;
    ctool_u32 expected_pack = index >= 298u && index <= 453u ? 1u : 0u;

    if (index <= 168u) {
      expected_path = "/kernel/core/types.h";
      types_count++;
    } else if (index <= 297u) {
      expected_path = "/kernel/fs/blockdev.h";
      block_count++;
    } else {
      expected_path = "/kernel/fs/fat16.h";
      fat_count++;
    }
    if (token->kind < CTOOL_C_PP_TOKEN_IDENTIFIER ||
        token->kind > CTOOL_C_PP_TOKEN_CUPID_EXE ||
        !string_equal(token->location.path, expected_path) ||
        !string_equal(token->physical_location.path, expected_path) ||
        token->location.line != token->physical_location.line ||
        token->location.column != token->physical_location.column ||
        token->location.line == 0u || token->location.column == 0u ||
        token->pack_alignment != expected_pack) {
      (void)fprintf(stderr, "fat16: token %u metadata differs\n", index);
      return 1;
    }
    kinds[(ctool_u32)token->kind]++;
    if (token->pack_alignment == 0u) {
      pack_zero_count++;
    } else {
      pack_one_count++;
    }
  }
  if (types_count != 169u || block_count != 129u || fat_count != 412u ||
      kinds[CTOOL_C_PP_TOKEN_IDENTIFIER] != 386u ||
      kinds[CTOOL_C_PP_TOKEN_NUMBER] != 17u ||
      kinds[CTOOL_C_PP_TOKEN_CHARACTER] != 0u ||
      kinds[CTOOL_C_PP_TOKEN_STRING] != 0u ||
      kinds[CTOOL_C_PP_TOKEN_PUNCTUATOR] != 307u ||
      kinds[CTOOL_C_PP_TOKEN_CUPID_EXE] != 0u || pack_zero_count != 554u ||
      pack_one_count != 156u) {
    (void)fprintf(stderr, "fat16: preprocessing tape inventory differs\n");
    return 1;
  }
  if (tape->tokens[97].kind != CTOOL_C_PP_TOKEN_NUMBER ||
      !string_equal(tape->tokens[97].spelling, "256") ||
      !dual_location_matches(&tape->tokens[97].location,
                             &tape->tokens[97].physical_location,
                             "/kernel/core/types.h", 42u) ||
      tape->tokens[97].physical_location.column != 24u ||
      !string_equal(tape->tokens[298].spelling, "typedef") ||
      !dual_location_matches(&tape->tokens[298].location,
                             &tape->tokens[298].physical_location,
                             "/kernel/fs/fat16.h", 28u) ||
      !string_equal(tape->tokens[453].spelling, ";") ||
      !dual_location_matches(&tape->tokens[453].location,
                             &tape->tokens[453].physical_location,
                             "/kernel/fs/fat16.h", 74u) ||
      !string_equal(tape->tokens[454].spelling, "typedef") ||
      !dual_location_matches(&tape->tokens[454].location,
                             &tape->tokens[454].physical_location,
                             "/kernel/fs/fat16.h", 78u)) {
    (void)fprintf(stderr, "fat16: preprocessing tape anchors differ\n");
    return 1;
  }
  return 0;
}

static int validate_bindings(const ctool_c_translation_unit_t *unit) {
  ctool_u32 kind_counts[CTOOL_C_BINDING_ENUMERATOR + 1u];
  ctool_u32 index;

  (void)memset(kind_counts, 0, sizeof(kind_counts));
  if (unit->bindings == NULL ||
      unit->binding_count != ARRAY_COUNT(binding_oracles)) {
    (void)fprintf(stderr, "fat16: expected 50 ordinary bindings\n");
    return 1;
  }
  for (index = 0u; index < unit->binding_count; index++) {
    const ctool_c_binding_t *binding = &unit->bindings[index];
    const binding_oracle_t *oracle = &binding_oracles[index];
    const ctool_c_type_node_t *node = type_node(unit, binding->type);

    if (!string_equal(binding->name, oracle->name) ||
        binding->kind != oracle->kind || node == NULL ||
        !dual_location_matches(&binding->location,
                               &binding->physical_location, oracle->path,
                               oracle->line)) {
      (void)fprintf(stderr, "fat16: binding %u (%s) differs\n", index,
                    oracle->name);
      return 1;
    }
    kind_counts[(ctool_u32)binding->kind]++;
    if (binding->kind == CTOOL_C_BINDING_ENUMERATOR) {
      if (binding->integer_bits != oracle->integer_bits ||
          binding->integer_unsigned != CTOOL_FALSE ||
          node->kind != CTOOL_C_TYPE_SIGNED_INT) {
        (void)fprintf(stderr, "fat16: enumerator %s differs\n", oracle->name);
        return 1;
      }
    } else if (binding->integer_bits != 0u ||
               binding->integer_unsigned != CTOOL_FALSE) {
      (void)fprintf(stderr, "fat16: non-enumerator %s has integer payload\n",
                    oracle->name);
      return 1;
    }
  }
  if (kind_counts[CTOOL_C_BINDING_TYPEDEF] != 24u ||
      kind_counts[CTOOL_C_BINDING_OBJECT] != 0u ||
      kind_counts[CTOOL_C_BINDING_FUNCTION] != 21u ||
      kind_counts[CTOOL_C_BINDING_ENUMERATOR] != 5u) {
    (void)fprintf(stderr, "fat16: binding kinds differ\n");
    return 1;
  }
  if (unit->tags != NULL || unit->tag_count != 0u) {
    (void)fprintf(stderr, "fat16: anonymous definitions invented tags\n");
    return 1;
  }
  return 0;
}

static int validate_graph_inventory(const ctool_c_translation_unit_t *unit) {
  ctool_u32 record_count = 0u;
  ctool_u32 enum_count = 0u;
  ctool_u32 function_count = 0u;
  ctool_u32 unnamed_parameter_count = 0u;
  ctool_u32 index;

  if (unit->graph.types == NULL || unit->graph.type_count == 0u ||
      unit->layout.types == NULL ||
      unit->layout.type_count != unit->graph.type_count ||
      unit->graph.members == NULL || unit->graph.member_count != 78u ||
      unit->layout.members == NULL || unit->layout.member_count != 78u ||
      unit->graph.parameter_types == NULL ||
      unit->graph.parameter_type_count != 44u || unit->parameters == NULL ||
      unit->parameter_count != 44u) {
    (void)fprintf(stderr, "fat16: frozen graph inventory differs\n");
    return 1;
  }
  for (index = 0u; index < unit->graph.type_count; index++) {
    const ctool_c_type_node_t *node = &unit->graph.types[index];
    if (node->kind == CTOOL_C_TYPE_RECORD) {
      if (node->record_kind != CTOOL_C_RECORD_STRUCT ||
          node->record_complete != CTOOL_TRUE) {
        (void)fprintf(stderr, "fat16: non-structure record in closure\n");
        return 1;
      }
      record_count++;
    } else if (node->kind == CTOOL_C_TYPE_ENUM) {
      enum_count++;
    } else if (node->kind == CTOOL_C_TYPE_FUNCTION) {
      if (node->has_prototype != CTOOL_TRUE ||
          node->variadic != CTOOL_FALSE ||
          node->first_parameter > unit->graph.parameter_type_count ||
          node->parameter_count >
              unit->graph.parameter_type_count - node->first_parameter) {
        (void)fprintf(stderr, "fat16: function type %u differs\n", index);
        return 1;
      }
      function_count++;
    }
  }
  for (index = 0u; index < unit->parameter_count; index++) {
    if (unit->graph.parameter_types[index] >= unit->graph.type_count ||
        unit->parameters[index].location.path.data == NULL ||
        unit->parameters[index].physical_location.path.data == NULL ||
        unit->parameters[index].location.line == 0u ||
        unit->parameters[index].physical_location.line == 0u) {
      (void)fprintf(stderr, "fat16: parameter %u metadata differs\n", index);
      return 1;
    }
    if (unit->parameters[index].name.size == 0u) {
      unnamed_parameter_count++;
    }
  }
  if (record_count != 12u || enum_count != 2u || function_count != 27u ||
      unnamed_parameter_count != 3u) {
    (void)fprintf(stderr,
                  "fat16: expected 12 records, 2 enums, 27 functions, and "
                  "3 abstract parameters\n");
    return 1;
  }
  return 0;
}

static int validate_enum_compatibility(const ctool_c_translation_unit_t *unit) {
  static const char *const enum_names[] = {"bool", "key_state_t"};
  ctool_u32 index;
  for (index = 0u; index < ARRAY_COUNT(enum_names); index++) {
    const ctool_c_binding_t *binding = find_binding(unit, enum_names[index]);
    const ctool_c_type_node_t *node;
    const ctool_c_type_node_t *compatible;
    const ctool_c_type_layout_t *layout;
    if (binding == NULL || binding->kind != CTOOL_C_BINDING_TYPEDEF) {
      (void)fprintf(stderr, "fat16: missing enum typedef %s\n",
                    enum_names[index]);
      return 1;
    }
    node = type_node(unit, binding->type);
    if (node == NULL || node->kind != CTOOL_C_TYPE_ENUM) {
      (void)fprintf(stderr, "fat16: %s is not an enum\n", enum_names[index]);
      return 1;
    }
    compatible = type_node(unit, node->referenced_type);
    layout = type_layout(unit, binding->type);
    if (compatible == NULL || compatible->kind != CTOOL_C_TYPE_UNSIGNED_INT ||
        layout == NULL || layout->size != 4u || layout->alignment != 4u ||
        layout->is_integer != CTOOL_TRUE || layout->is_signed != CTOOL_FALSE) {
      (void)fprintf(stderr,
                    "fat16: %s did not select unsigned-int compatibility\n",
                    enum_names[index]);
      return 1;
    }
  }
  return 0;
}

static int validate_record_layouts(const ctool_c_translation_unit_t *unit) {
  ctool_u32 packed_member_count = 0u;
  ctool_u32 oracle_index;

  for (oracle_index = 0u; oracle_index < ARRAY_COUNT(record_oracles);
       oracle_index++) {
    const record_oracle_t *oracle = &record_oracles[oracle_index];
    const ctool_c_binding_t *binding = find_binding(unit, oracle->binding);
    const ctool_c_type_node_t *record;
    const ctool_c_type_layout_t *layout;
    ctool_u32 member_index;

    if (binding == NULL || binding->kind != CTOOL_C_BINDING_TYPEDEF) {
      (void)fprintf(stderr, "fat16: missing record typedef %s\n",
                    oracle->binding);
      return 1;
    }
    record = type_node(unit, binding->type);
    layout = type_layout(unit, binding->type);
    if (record == NULL || record->kind != CTOOL_C_TYPE_RECORD ||
        record->record_kind != CTOOL_C_RECORD_STRUCT ||
        record->record_complete != CTOOL_TRUE ||
        record->record_packed != CTOOL_FALSE ||
        record->explicit_alignment != 0u ||
        record->member_count != oracle->member_count || layout == NULL ||
        layout->size != oracle->size || layout->alignment != oracle->alignment ||
        layout->is_complete_object != CTOOL_TRUE ||
        layout->is_object != CTOOL_TRUE || layout->is_integer != CTOOL_FALSE ||
        record->first_member > unit->graph.member_count ||
        record->member_count >
            unit->graph.member_count - record->first_member) {
      (void)fprintf(stderr, "fat16: record %s layout differs\n",
                    oracle->binding);
      return 1;
    }
    for (member_index = 0u; member_index < record->member_count;
         member_index++) {
      ctool_u32 flat_index = record->first_member + member_index;
      const ctool_c_record_member_t *member = &unit->graph.members[flat_index];
      const ctool_c_member_layout_t *member_layout =
          &unit->layout.members[flat_index];
      if (!string_equal(member->name, oracle->member_names[member_index]) ||
          member->type >= unit->graph.type_count ||
          member->pack_alignment != oracle->pack_alignment ||
          member->is_bit_field != CTOOL_FALSE ||
          member->anonymous != CTOOL_FALSE ||
          member->member_packed != CTOOL_FALSE ||
          member->explicit_alignment != 0u ||
          member_layout->byte_offset != oracle->member_offsets[member_index] ||
          member_layout->bit_offset != 0u || member_layout->bit_width != 0u) {
        (void)fprintf(stderr, "fat16: record %s member %u differs\n",
                      oracle->binding, member_index);
        return 1;
      }
      if (member->pack_alignment == 1u) {
        packed_member_count++;
      }
    }
  }
  if (packed_member_count != 36u) {
    (void)fprintf(stderr, "fat16: expected 36 pack-capped members\n");
    return 1;
  }
  return 0;
}

static const ctool_c_type_node_t *
function_binding(const ctool_c_translation_unit_t *unit, const char *name) {
  const ctool_c_binding_t *binding = find_binding(unit, name);
  const ctool_c_type_node_t *node;
  if (binding == NULL || binding->kind != CTOOL_C_BINDING_FUNCTION) {
    return NULL;
  }
  node = type_node(unit, binding->type);
  return node != NULL && node->kind == CTOOL_C_TYPE_FUNCTION ? node : NULL;
}

static int is_pointer_to_const(const ctool_c_translation_unit_t *unit,
                               ctool_u32 type,
                               ctool_c_type_kind_t base_kind) {
  const ctool_c_type_node_t *pointer = type_node(unit, type);
  const ctool_c_type_node_t *qualified;
  const ctool_c_type_node_t *base;
  if (pointer == NULL || pointer->kind != CTOOL_C_TYPE_POINTER ||
      pointer->qualifiers != 0u) {
    return 0;
  }
  qualified = type_node(unit, pointer->referenced_type);
  if (qualified == NULL || qualified->kind != CTOOL_C_TYPE_QUALIFIED ||
      qualified->qualifiers != CTOOL_C_QUAL_CONST) {
    return 0;
  }
  base = type_node(unit, qualified->referenced_type);
  return base != NULL && base->kind == base_kind ? 1 : 0;
}

static int validate_declarators(const ctool_c_translation_unit_t *unit) {
  static const char *const set_output_parameter_names[] = {
      "print_fn", "putchar_fn", "print_int_fn"};
  const ctool_c_binding_t *block_binding = find_binding(unit, "block_device_t");
  const ctool_c_binding_t *callback_binding =
      find_binding(unit, "fat16_enum_callback_t");
  const ctool_c_type_node_t *block;
  const ctool_c_type_node_t *callback;
  const ctool_c_type_node_t *set_output;
  const ctool_c_type_node_t *empty_function;
  const ctool_c_record_member_t *member;
  ctool_u32 member_index = 0u;
  ctool_u32 parameter_index;

  if (block_binding == NULL || callback_binding == NULL) {
    (void)fprintf(stderr, "fat16: declarator bindings are missing\n");
    return 1;
  }
  block = type_node(unit, block_binding->type);
  callback = function_from_pointer(unit, callback_binding->type);
  set_output = function_binding(unit, "fat16_set_output");
  empty_function = function_binding(unit, "fat16_init");
  if (block == NULL || block->kind != CTOOL_C_TYPE_RECORD || callback == NULL ||
      callback->parameter_count != 4u || set_output == NULL ||
      set_output->parameter_count != 3u || empty_function == NULL ||
      empty_function->parameter_count != 0u ||
      empty_function->has_prototype != CTOOL_TRUE) {
    (void)fprintf(stderr, "fat16: function declarator precedence differs\n");
    return 1;
  }

  member = find_record_member(unit, block, "name", &member_index);
  if (member == NULL ||
      is_pointer_to_const(unit, member->type, CTOOL_C_TYPE_CHAR) == 0) {
    (void)fprintf(stderr, "fat16: const char member qualification differs\n");
    return 1;
  }
  member = find_record_member(unit, block, "read", &member_index);
  if (member == NULL || function_from_pointer(unit, member->type) == NULL ||
      function_from_pointer(unit, member->type)->parameter_count != 4u) {
    (void)fprintf(stderr, "fat16: read member is not a four-parameter callback\n");
    return 1;
  }
  member = find_record_member(unit, block, "write", &member_index);
  if (member == NULL) {
    (void)fprintf(stderr, "fat16: write member is missing\n");
    return 1;
  }
  callback = function_from_pointer(unit, member->type);
  if (callback == NULL || callback->parameter_count != 4u ||
      is_pointer_to_const(
          unit,
          unit->graph.parameter_types[callback->first_parameter + 3u],
          CTOOL_C_TYPE_VOID) == 0) {
    (void)fprintf(stderr, "fat16: write callback const-void parameter differs\n");
    return 1;
  }

  for (parameter_index = 0u; parameter_index < set_output->parameter_count;
       parameter_index++) {
    ctool_u32 flat_index = set_output->first_parameter + parameter_index;
    const ctool_c_type_node_t *nested;
    if (flat_index >= unit->parameter_count ||
        !string_equal(unit->parameters[flat_index].name,
                      set_output_parameter_names[parameter_index])) {
      (void)fprintf(stderr, "fat16: set-output parameter %u differs\n",
                    parameter_index);
      return 1;
    }
    nested = function_from_pointer(unit, unit->graph.parameter_types[flat_index]);
    if (nested == NULL || nested->parameter_count != 1u ||
        nested->has_prototype != CTOOL_TRUE) {
      (void)fprintf(stderr, "fat16: set-output callback %u differs\n",
                    parameter_index);
      return 1;
    }
  }

  callback = function_from_pointer(unit, callback_binding->type);
  if (callback == NULL || callback->parameter_count != 4u ||
      is_pointer_to_const(
          unit, unit->graph.parameter_types[callback->first_parameter],
          CTOOL_C_TYPE_CHAR) == 0) {
    (void)fprintf(stderr, "fat16: callback typedef differs\n");
    return 1;
  }
  return 0;
}

static int validate_fat16_unit(const ctool_c_translation_unit_t *unit) {
  return validate_bindings(unit) != 0 || validate_graph_inventory(unit) != 0 ||
                 validate_enum_compatibility(unit) != 0 ||
                 validate_record_layouts(unit) != 0 ||
                 validate_declarators(unit) != 0
             ? 1
             : 0;
}

static int validate_mutant_layout(const ctool_c_translation_unit_t *original,
                                  const ctool_c_translation_unit_t *mutant) {
  const ctool_c_binding_t *buffer_binding =
      find_binding(mutant, "keyboard_buffer_t");
  const ctool_c_binding_t *state_binding =
      find_binding(mutant, "keyboard_state_t");
  const ctool_c_binding_t *original_buffer =
      find_binding(original, "keyboard_buffer_t");
  const ctool_c_binding_t *original_state =
      find_binding(original, "keyboard_state_t");
  const ctool_c_type_node_t *buffer;
  const ctool_c_type_node_t *state;
  const ctool_c_record_member_t *events;
  const ctool_c_type_node_t *events_array;
  ctool_u32 member_index = 0u;

  if (buffer_binding == NULL || state_binding == NULL ||
      original_buffer == NULL || original_state == NULL ||
      type_layout(original, original_buffer->type)->size != 3076u ||
      type_layout(original, original_state->type)->size != 5156u ||
      type_layout(mutant, buffer_binding->type)->size != 28u ||
      type_layout(mutant, buffer_binding->type)->alignment != 4u ||
      type_layout(mutant, state_binding->type)->size != 2108u ||
      type_layout(mutant, state_binding->type)->alignment != 4u) {
    (void)fprintf(stderr, "fat16: macro-expanded bound mutation was ignored\n");
    return 1;
  }
  buffer = type_node(mutant, buffer_binding->type);
  state = type_node(mutant, state_binding->type);
  if (buffer == NULL || state == NULL) {
    (void)fprintf(stderr, "fat16: mutant records are missing\n");
    return 1;
  }
  events = find_record_member(mutant, buffer, "events", &member_index);
  events_array = events == NULL ? NULL : type_node(mutant, events->type);
  if (events_array == NULL || events_array->kind != CTOOL_C_TYPE_ARRAY ||
      events_array->array_bound_kind != CTOOL_C_ARRAY_FIXED ||
      events_array->element_count != 2u ||
      mutant->layout.members[buffer->first_member].byte_offset != 0u ||
      mutant->layout.members[buffer->first_member + 1u].byte_offset != 24u ||
      mutant->layout.members[buffer->first_member + 2u].byte_offset != 25u ||
      mutant->layout.members[buffer->first_member + 3u].byte_offset != 26u ||
      mutant->layout.members[state->first_member].byte_offset != 0u ||
      mutant->layout.members[state->first_member + 1u].byte_offset != 1024u ||
      mutant->layout.members[state->first_member + 2u].byte_offset != 1056u ||
      mutant->layout.members[state->first_member + 3u].byte_offset != 2080u) {
    (void)fprintf(stderr, "fat16: mutant record graph differs\n");
    return 1;
  }
  return 0;
}

static int run_fat16(const char *host_root) {
  ctool_c_pp_include_root_t include_roots[ARRAY_COUNT(active_rows)];
  ctool_c_pp_macro_action_t macro_actions[ARRAY_COUNT(active_rows)];
  ctool_path_t forced_includes[ARRAY_COUNT(active_rows)];
  ctool_c_pp_request_t pp_request;
  ctool_c_parse_request_t parse_request;
  ctool_c_pp_result_t tape;
  ctool_c_pp_result_t mutant_tape;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t mutant_unit;
  ctool_c_pp_token_t *snapshot = NULL;
  ctool_c_pp_token_t *mutant_tokens = NULL;
  ctool_c_pp_token_t *mutant_snapshot = NULL;
  ctool_host_adapter_t adapter;
  ctool_job_t *job = NULL;
  ctool_path_t path;
  ctool_source_t source;
  ctool_status_t status;
  size_t token_bytes;
  int failed = 1;

  if (build_kernel_profile(&pp_request, include_roots, macro_actions,
                           forced_includes) != 0 ||
      open_job("fat16", host_root, 256u * 1024u * 1024u, &adapter, &job) != 0) {
    return 1;
  }
  path.text = ctool_string("/kernel/fs/fat16.h");
  status = ctool_job_load_source(job, &path, &source);
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "fat16: source load: %s\n",
                  ctool_status_name(status));
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  (void)memset(&tape, 0xa5, sizeof(tape));
  status = ctool_c_preprocess(job, &source, &pp_request, &tape);
  if (status != CTOOL_OK || ctool_job_diagnostic_count(job) != 0u ||
      validate_fat16_tape(&tape) != 0) {
    (void)fprintf(stderr, "fat16: preprocess: %s\n",
                  ctool_status_name(status));
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }

  token_bytes = (size_t)tape.token_count * sizeof(*snapshot);
  snapshot = (ctool_c_pp_token_t *)malloc(token_bytes);
  mutant_tokens = (ctool_c_pp_token_t *)malloc(token_bytes);
  mutant_snapshot = (ctool_c_pp_token_t *)malloc(token_bytes);
  if (snapshot == NULL || mutant_tokens == NULL || mutant_snapshot == NULL) {
    (void)fprintf(stderr, "fat16: token snapshot allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(snapshot, tape.tokens, token_bytes);

  parse_request.mode = CTOOL_C_PP_MODE_C11;
  parse_request.gnu_extensions = CTOOL_TRUE;
  (void)memset(&unit, 0xa5, sizeof(unit));
  status = ctool_c_parse(job, &tape, &parse_request, &unit);
  if (status != CTOOL_OK || ctool_job_diagnostic_count(job) != 0u ||
      memcmp(snapshot, tape.tokens, token_bytes) != 0 ||
      validate_fat16_unit(&unit) != 0) {
    (void)fprintf(stderr, "fat16: parse: %s\n", ctool_status_name(status));
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }

  (void)memcpy(mutant_tokens, tape.tokens, token_bytes);
  mutant_tokens[97].spelling = ctool_string("2");
  (void)memcpy(mutant_snapshot, mutant_tokens, token_bytes);
  mutant_tape.tokens = mutant_tokens;
  mutant_tape.token_count = tape.token_count;
  (void)memset(&mutant_unit, 0xa5, sizeof(mutant_unit));
  status = ctool_c_parse(job, &mutant_tape, &parse_request, &mutant_unit);
  if (status != CTOOL_OK || ctool_job_diagnostic_count(job) != 0u ||
      memcmp(mutant_snapshot, mutant_tokens, token_bytes) != 0 ||
      memcmp(snapshot, tape.tokens, token_bytes) != 0 ||
      mutant_unit.binding_count != 50u || mutant_unit.graph.member_count != 78u ||
      mutant_unit.graph.parameter_type_count != 44u ||
      validate_mutant_layout(&unit, &mutant_unit) != 0) {
    (void)fprintf(stderr, "fat16: mutant parse: %s\n",
                  ctool_status_name(status));
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }

  failed = 0;
cleanup:
  free(mutant_snapshot);
  free(mutant_tokens);
  free(snapshot);
  ctool_job_close(job);
  if (failed == 0) {
    (void)printf("fat16: ok\n");
  }
  return failed;
}

typedef struct {
  const char *path;
  ctool_u32 token_count;
  ctool_u32 binding_count;
  const char *binding_name;
  const char *declaration_path;
  ctool_u32 declaration_line;
  ctool_c_binding_kind_t kind;
  ctool_c_linkage_t linkage;
} redeclaration_header_oracle_t;

static int parse_unchanged_declaration_header(
    ctool_job_t *job, const ctool_c_pp_request_t *pp_request,
    const ctool_c_parse_request_t *parse_request,
    const redeclaration_header_oracle_t *oracle) {
  ctool_c_pp_result_t tape;
  ctool_c_translation_unit_t unit;
  ctool_c_pp_token_t *snapshot = NULL;
  ctool_path_t path;
  ctool_source_t source;
  ctool_status_t status;
  ctool_u32 diagnostic_count = ctool_job_diagnostic_count(job);
  ctool_u32 binding_count = 0u;
  const ctool_c_binding_t *binding = NULL;
  ctool_u32 index;
  size_t token_bytes;
  int failed = 1;

  path.text = ctool_string(oracle->path);
  status = ctool_job_load_source(job, &path, &source);
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "redeclarations: load %s: %s\n", oracle->path,
                  ctool_status_name(status));
    return 1;
  }
  (void)memset(&tape, 0xa5, sizeof(tape));
  status = ctool_c_preprocess(job, &source, pp_request, &tape);
  if (status != CTOOL_OK || tape.tokens == NULL || tape.token_count == 0u ||
      ctool_job_diagnostic_count(job) != diagnostic_count) {
    (void)fprintf(stderr, "redeclarations: preprocess %s: %s\n", oracle->path,
                  ctool_status_name(status));
    (void)ctool_job_render_diagnostics(job);
    return 1;
  }
  token_bytes = (size_t)tape.token_count * sizeof(*snapshot);
  snapshot = (ctool_c_pp_token_t *)malloc(token_bytes);
  if (snapshot == NULL) {
    (void)fprintf(stderr, "redeclarations: snapshot %s failed\n",
                  oracle->path);
    return 1;
  }
  (void)memcpy(snapshot, tape.tokens, token_bytes);
  (void)memset(&unit, 0xa5, sizeof(unit));
  status = ctool_c_parse(job, &tape, parse_request, &unit);
  if (status != CTOOL_OK ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      memcmp(snapshot, tape.tokens, token_bytes) != 0) {
    (void)fprintf(stderr, "redeclarations: parse %s: %s\n", oracle->path,
                  ctool_status_name(status));
    (void)ctool_job_render_diagnostics(job);
  } else {
    for (index = 0u; index < unit.binding_count; index++) {
      if (string_equal(unit.bindings[index].name, oracle->binding_name) != 0) {
        binding = &unit.bindings[index];
        binding_count++;
      }
    }
    if (tape.token_count != oracle->token_count ||
        unit.binding_count != oracle->binding_count || binding_count != 1u ||
        binding == NULL || binding->kind != oracle->kind ||
        binding->linkage != oracle->linkage ||
        !dual_location_matches(&binding->location,
                               &binding->physical_location,
                               oracle->declaration_path,
                               oracle->declaration_line)) {
      (void)fprintf(stderr,
                    "redeclarations: oracle %s differs\n", oracle->path);
    } else {
      failed = 0;
    }
  }
  free(snapshot);
  return failed;
}

static int run_header_sweep(const char *host_root, int header_count,
                            char *const *header_paths) {
  ctool_c_pp_include_root_t include_roots[ARRAY_COUNT(active_rows)];
  ctool_c_pp_macro_action_t macro_actions[ARRAY_COUNT(active_rows)];
  ctool_path_t forced_includes[ARRAY_COUNT(active_rows)];
  ctool_c_pp_request_t pp_request;
  ctool_c_parse_request_t parse_request;
  ctool_u32 passed = 0u;
  ctool_u32 failed = 0u;
  int index;
  if (header_count <= 0 ||
      build_kernel_profile(&pp_request, include_roots, macro_actions,
                           forced_includes) != 0) {
    return 1;
  }
  parse_request.mode = CTOOL_C_PP_MODE_C11;
  parse_request.gnu_extensions = CTOOL_TRUE;
  for (index = 0; index < header_count; index++) {
    ctool_host_adapter_t adapter;
    ctool_job_t *job = NULL;
    ctool_path_t path;
    ctool_source_t source;
    ctool_c_pp_result_t tape;
    ctool_c_translation_unit_t unit;
    const ctool_diagnostic_t *diagnostic;
    ctool_status_t status;
    if (open_job("header-sweep", host_root, 256u * 1024u * 1024u,
                 &adapter, &job) != 0) {
      return 1;
    }
    path.text = ctool_string(header_paths[index]);
    status = ctool_job_load_source(job, &path, &source);
    if (status == CTOOL_OK) {
      (void)memset(&tape, 0xa5, sizeof(tape));
      status = ctool_c_preprocess(job, &source, &pp_request, &tape);
    }
    if (status != CTOOL_OK || tape.tokens == NULL ||
        tape.token_count == 0u || ctool_job_diagnostic_count(job) != 0u) {
      (void)fprintf(stderr, "header-sweep: prepare %s: %s\n",
                    header_paths[index], ctool_status_name(status));
      (void)ctool_job_render_diagnostics(job);
      ctool_job_close(job);
      return 1;
    }
    (void)memset(&unit, 0xa5, sizeof(unit));
    status = ctool_c_parse(job, &tape, &parse_request, &unit);
    if (status == CTOOL_OK) {
      if (ctool_job_diagnostic_count(job) != 0u) {
        ctool_job_close(job);
        return 1;
      }
      (void)printf("PASS\t%s\n", header_paths[index]);
      passed++;
    } else {
      diagnostic = ctool_job_diagnostic(job, 0u);
      if (ctool_job_diagnostic_count(job) != 1u || diagnostic == NULL ||
          unit_is_zero(&unit) == 0) {
        ctool_job_close(job);
        return 1;
      }
      (void)printf("FAIL\t%s\t%s\t0x%08x\t%.*s\t%u\t%u\n",
                   header_paths[index], ctool_status_name(status),
                   diagnostic->code, (int)diagnostic->path.size,
                   diagnostic->path.data != NULL ? diagnostic->path.data : "",
                   diagnostic->line, diagnostic->column);
      failed++;
    }
    ctool_job_close(job);
  }
  (void)printf("header-sweep: ok %u %u\n", passed, failed);
  return 0;
}

static int run_redeclarations(const char *host_root) {
  static const redeclaration_header_oracle_t additional_headers[] = {
      {"/kernel/cpu/irq.h", 512u, 57u, "irq_handler_t",
       "/kernel/cpu/isr.h", 18u, CTOOL_C_BINDING_TYPEDEF,
       CTOOL_C_LINKAGE_NONE},
      {"/kernel/lang/cupidscript.h", 1867u, 177u, "script_context_t",
       "/kernel/lang/cupidscript_streams.h", 13u,
       CTOOL_C_BINDING_TYPEDEF, CTOOL_C_LINKAGE_NONE},
      {"/kernel/lang/shell.h", 591u, 74u,
       "shell_jit_program_is_running", "/kernel/lang/shell.h", 81u,
       CTOOL_C_BINDING_FUNCTION, CTOOL_C_LINKAGE_EXTERNAL}};
  ctool_c_pp_include_root_t include_roots[ARRAY_COUNT(active_rows)];
  ctool_c_pp_macro_action_t macro_actions[ARRAY_COUNT(active_rows)];
  ctool_path_t forced_includes[ARRAY_COUNT(active_rows)];
  ctool_c_pp_request_t pp_request;
  ctool_c_parse_request_t parse_request;
  ctool_c_pp_result_t tape;
  ctool_c_translation_unit_t unit;
  ctool_c_pp_token_t *snapshot = NULL;
  ctool_host_adapter_t adapter;
  ctool_job_t *job = NULL;
  ctool_path_t path;
  ctool_source_t source;
  const ctool_c_binding_t *print_binding = NULL;
  const ctool_c_binding_t *cursor_binding;
  const ctool_c_binding_t *handler_binding;
  const ctool_c_type_node_t *print_type;
  ctool_status_t status;
  ctool_u32 print_count = 0u;
  ctool_u32 index;
  size_t token_bytes;
  int failed = 1;

  if (build_kernel_profile(&pp_request, include_roots, macro_actions,
                           forced_includes) != 0 ||
      open_job("redeclarations", host_root, 256u * 1024u * 1024u,
               &adapter, &job) != 0) {
    return 1;
  }
  path.text = ctool_string("/kernel/core/kernel.h");
  status = ctool_job_load_source(job, &path, &source);
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "redeclarations: source load: %s\n",
                  ctool_status_name(status));
    goto cleanup;
  }
  (void)memset(&tape, 0xa5, sizeof(tape));
  status = ctool_c_preprocess(job, &source, &pp_request, &tape);
  if (status != CTOOL_OK || tape.tokens == NULL || tape.token_count == 0u ||
      ctool_job_diagnostic_count(job) != 0u) {
    (void)fprintf(stderr, "redeclarations: preprocess: %s\n",
                  ctool_status_name(status));
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  token_bytes = (size_t)tape.token_count * sizeof(*snapshot);
  snapshot = (ctool_c_pp_token_t *)malloc(token_bytes);
  if (snapshot == NULL) {
    (void)fprintf(stderr, "redeclarations: tape snapshot failed\n");
    goto cleanup;
  }
  (void)memcpy(snapshot, tape.tokens, token_bytes);
  parse_request.mode = CTOOL_C_PP_MODE_C11;
  parse_request.gnu_extensions = CTOOL_TRUE;
  (void)memset(&unit, 0xa5, sizeof(unit));
  status = ctool_c_parse(job, &tape, &parse_request, &unit);
  if (status != CTOOL_OK || ctool_job_diagnostic_count(job) != 0u ||
      memcmp(snapshot, tape.tokens, token_bytes) != 0) {
    (void)fprintf(stderr, "redeclarations: parse: %s\n",
                  ctool_status_name(status));
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  for (index = 0u; index < unit.binding_count; index++) {
    if (string_equal(unit.bindings[index].name, "print") != 0) {
      print_binding = &unit.bindings[index];
      print_count++;
    }
  }
  cursor_binding = find_binding(&unit, "cursor_x");
  handler_binding = find_binding(&unit, "irq_handler_t");
  print_type = print_binding == NULL
                   ? NULL
                   : type_node(&unit, print_binding->type);
  if (tape.token_count != 569u || unit.binding_count != 69u ||
      print_count != 1u || print_binding == NULL ||
      print_binding->kind != CTOOL_C_BINDING_FUNCTION ||
      print_binding->storage != CTOOL_C_STORAGE_NONE ||
      print_binding->linkage != CTOOL_C_LINKAGE_EXTERNAL ||
      !dual_location_matches(&print_binding->location,
                             &print_binding->physical_location,
                             "/kernel/cpu/isr.h", 7u) ||
      print_type == NULL || print_type->kind != CTOOL_C_TYPE_FUNCTION ||
      print_type->has_prototype != CTOOL_TRUE ||
      print_type->parameter_count != 1u || cursor_binding == NULL ||
      cursor_binding->kind != CTOOL_C_BINDING_OBJECT ||
      cursor_binding->storage != CTOOL_C_STORAGE_EXTERN ||
      cursor_binding->linkage != CTOOL_C_LINKAGE_EXTERNAL ||
      handler_binding == NULL ||
      handler_binding->kind != CTOOL_C_BINDING_TYPEDEF ||
      handler_binding->linkage != CTOOL_C_LINKAGE_NONE) {
    (void)fprintf(stderr,
                  "redeclarations: unchanged kernel header differs\n");
    goto cleanup;
  }
  for (index = 0u; index < ARRAY_COUNT(additional_headers); index++) {
    if (parse_unchanged_declaration_header(
            job, &pp_request, &parse_request, &additional_headers[index]) !=
        0) {
      goto cleanup;
    }
  }
  failed = 0;
cleanup:
  free(snapshot);
  ctool_job_close(job);
  if (failed == 0) {
    (void)printf("redeclarations: ok\n");
  }
  return failed;
}

typedef struct {
  const char *name;
  const char *source;
  ctool_status_t status;
  ctool_u32 diagnostic_code;
} frontend_failure_case_t;

typedef struct {
  frontend_failure_case_t failure;
  ctool_u32 line;
  ctool_u32 column;
  const char *message;
} frontend_exact_failure_case_t;

static int append_scale_text(char *text, size_t capacity, size_t *used,
                             const char *suffix);

typedef struct {
  const char *mode;
  ctool_host_adapter_t adapter;
  ctool_job_t *job;
  ctool_c_pp_request_t pp_request;
  ctool_c_parse_request_t parse_request;
  ctool_c_translation_unit_t anchor;
  const ctool_c_binding_t *anchor_bindings;
  const ctool_c_type_node_t *anchor_types;
  const ctool_c_type_layout_t *anchor_layouts;
} frontend_fixture_t;

static int preprocess_fixture(frontend_fixture_t *fixture, const char *path,
                              const char *text,
                              ctool_c_pp_result_t *tape_out) {
  ctool_source_t source;
  ctool_status_t status;
  ctool_u32 diagnostic_count;
  size_t text_size = strlen(text);

  if (text_size > 0xffffffffu) {
    (void)fprintf(stderr, "%s: %s exceeds the source-size contract\n",
                  fixture->mode, path);
    return 1;
  }
  source.path.text = ctool_string(path);
  source.contents = ctool_bytes(text, (ctool_u32)text_size);
  diagnostic_count = ctool_job_diagnostic_count(fixture->job);
  (void)memset(tape_out, 0xa5, sizeof(*tape_out));
  status = ctool_c_preprocess(fixture->job, &source, &fixture->pp_request,
                              tape_out);
  if (status != CTOOL_OK || tape_out->tokens == NULL ||
      tape_out->token_count == 0u ||
      ctool_job_diagnostic_count(fixture->job) != diagnostic_count) {
    (void)fprintf(stderr, "%s: preprocessing %s failed: %s\n",
                  fixture->mode, path, ctool_status_name(status));
    (void)ctool_job_render_diagnostics(fixture->job);
    return 1;
  }
  return 0;
}

static int parse_valid_fixture(frontend_fixture_t *fixture, const char *path,
                               const char *text,
                               ctool_c_translation_unit_t *unit_out) {
  ctool_c_pp_result_t tape;
  ctool_c_pp_token_t *snapshot;
  ctool_status_t status;
  ctool_u32 diagnostic_count;
  size_t token_bytes;
  int failed = 1;

  if (preprocess_fixture(fixture, path, text, &tape) != 0) {
    return 1;
  }
  token_bytes = (size_t)tape.token_count * sizeof(*snapshot);
  snapshot = (ctool_c_pp_token_t *)malloc(token_bytes);
  if (snapshot == NULL) {
    (void)fprintf(stderr, "%s: valid tape snapshot allocation failed\n",
                  fixture->mode);
    return 1;
  }
  (void)memcpy(snapshot, tape.tokens, token_bytes);
  diagnostic_count = ctool_job_diagnostic_count(fixture->job);
  (void)memset(unit_out, 0xa5, sizeof(*unit_out));
  status = ctool_c_parse(fixture->job, &tape, &fixture->parse_request,
                         unit_out);
  if (status != CTOOL_OK ||
      ctool_job_diagnostic_count(fixture->job) != diagnostic_count ||
      memcmp(snapshot, tape.tokens, token_bytes) != 0) {
    (void)fprintf(stderr, "%s: valid parse %s failed: %s\n", fixture->mode,
                  path, ctool_status_name(status));
    (void)ctool_job_render_diagnostics(fixture->job);
  } else {
    failed = 0;
  }
  free(snapshot);
  return failed;
}

static int parse_loaded_fixture(frontend_fixture_t *fixture,
                                const char *path_text,
                                const char *stop_spelling,
                                ctool_u32 expected_token_count,
                                ctool_c_translation_unit_t *unit_out) {
  ctool_path_t path;
  ctool_source_t source;
  ctool_c_pp_result_t tape;
  ctool_c_pp_token_t *snapshot = NULL;
  ctool_status_t status;
  ctool_u32 diagnostic_count;
  ctool_u32 token_index;
  size_t token_bytes;
  int failed = 1;

  path.text = ctool_string(path_text);
  status = ctool_job_load_source(fixture->job, &path, &source);
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "%s: load %s failed: %s\n", fixture->mode,
                  path_text, ctool_status_name(status));
    return 1;
  }
  diagnostic_count = ctool_job_diagnostic_count(fixture->job);
  (void)memset(&tape, 0xa5, sizeof(tape));
  status = ctool_c_preprocess(fixture->job, &source, &fixture->pp_request, &tape);
  if (status != CTOOL_OK || tape.tokens == NULL || tape.token_count == 0u ||
      ctool_job_diagnostic_count(fixture->job) != diagnostic_count) {
    (void)fprintf(stderr, "%s: preprocess %s failed: %s\n", fixture->mode,
                  path_text, ctool_status_name(status));
    (void)ctool_job_render_diagnostics(fixture->job);
    return 1;
  }
  if (expected_token_count != 0u &&
      tape.token_count != expected_token_count) {
    (void)fprintf(stderr,
                  "%s: %s token inventory differs: expected %u, got %u\n",
                  fixture->mode, path_text, expected_token_count,
                  tape.token_count);
    return 1;
  }
  if (stop_spelling != NULL) {
    for (token_index = 0u; token_index < tape.token_count; token_index++) {
      if (string_equal(tape.tokens[token_index].spelling, stop_spelling) != 0 &&
          string_equal(tape.tokens[token_index].physical_location.path,
                       path_text) != 0) {
        break;
      }
    }
    if (token_index == tape.token_count) {
      (void)fprintf(stderr, "%s: stop token %s is absent from %s\n",
                    fixture->mode, stop_spelling, path_text);
      return 1;
    }
    while (token_index != 0u &&
           tape.tokens[token_index - 1u].physical_location.line ==
               tape.tokens[token_index].physical_location.line &&
           string_equal(
               tape.tokens[token_index - 1u].physical_location.path,
               path_text) != 0) {
      token_index--;
    }
    tape.token_count = token_index;
  }
  token_bytes = (size_t)tape.token_count * sizeof(*snapshot);
  snapshot = (ctool_c_pp_token_t *)malloc(token_bytes);
  if (snapshot == NULL) {
    (void)fprintf(stderr, "%s: snapshot %s failed\n", fixture->mode,
                  path_text);
    return 1;
  }
  (void)memcpy(snapshot, tape.tokens, token_bytes);
  (void)memset(unit_out, 0xa5, sizeof(*unit_out));
  status = ctool_c_parse(fixture->job, &tape, &fixture->parse_request, unit_out);
  if (status != CTOOL_OK ||
      ctool_job_diagnostic_count(fixture->job) != diagnostic_count ||
      memcmp(snapshot, tape.tokens, token_bytes) != 0) {
    (void)fprintf(stderr, "%s: parse %s failed: %s\n", fixture->mode,
                  path_text, ctool_status_name(status));
    (void)ctool_job_render_diagnostics(fixture->job);
  } else {
    failed = 0;
  }
  free(snapshot);
  return failed;
}

static int validate_anchor(const frontend_fixture_t *fixture) {
  const ctool_c_binding_t *binding;
  const ctool_c_type_node_t *type;
  const ctool_c_type_layout_t *layout;

  if (fixture->anchor.bindings != fixture->anchor_bindings ||
      fixture->anchor.graph.types != fixture->anchor_types ||
      fixture->anchor.layout.types != fixture->anchor_layouts ||
      fixture->anchor.binding_count != 1u ||
      fixture->anchor.graph.type_count == 0u ||
      fixture->anchor.layout.type_count != fixture->anchor.graph.type_count) {
    return 1;
  }
  binding = &fixture->anchor.bindings[0];
  if (binding->kind != CTOOL_C_BINDING_TYPEDEF ||
      binding->storage != CTOOL_C_STORAGE_TYPEDEF ||
      !string_equal(binding->name, "anchor_t") ||
      binding->function_declaration_flags != 0u ||
      binding->type >= fixture->anchor.graph.type_count) {
    return 1;
  }
  type = &fixture->anchor.graph.types[binding->type];
  layout = &fixture->anchor.layout.types[binding->type];
  return type->kind == CTOOL_C_TYPE_UNSIGNED_INT && layout->size == 4u &&
                 layout->alignment == 4u &&
                 layout->is_complete_object == CTOOL_TRUE
             ? 0
             : 1;
}

static int begin_frontend_fixture(frontend_fixture_t *fixture,
                                  const char *mode, const char *host_root,
                                  ctool_u32 arena_bytes) {
  (void)memset(fixture, 0, sizeof(*fixture));
  fixture->mode = mode;
  if (open_job(mode, host_root, arena_bytes, &fixture->adapter,
               &fixture->job) != 0) {
    return 1;
  }
  fixture->pp_request.mode = CTOOL_C_PP_MODE_C11;
  fixture->pp_request.gnu_extensions = CTOOL_TRUE;
  fixture->parse_request.mode = CTOOL_C_PP_MODE_C11;
  fixture->parse_request.gnu_extensions = CTOOL_TRUE;
  if (parse_valid_fixture(fixture, "/anchor.c",
                          "typedef unsigned int anchor_t;\n",
                          &fixture->anchor) != 0) {
    ctool_job_close(fixture->job);
    fixture->job = NULL;
    return 1;
  }
  fixture->anchor_bindings = fixture->anchor.bindings;
  fixture->anchor_types = fixture->anchor.graph.types;
  fixture->anchor_layouts = fixture->anchor.layout.types;
  if (validate_anchor(fixture) != 0) {
    (void)fprintf(stderr, "%s: anchor translation unit differs\n", mode);
    ctool_job_close(fixture->job);
    fixture->job = NULL;
    return 1;
  }
  return 0;
}

static int expect_frontend_failure_at_message(
    frontend_fixture_t *fixture, const frontend_failure_case_t *test_case,
    const char *path, ctool_u32 expected_line,
    ctool_u32 expected_column, const char *expected_message) {
  ctool_c_pp_result_t tape;
  ctool_c_translation_unit_t unit;
  ctool_c_pp_token_t *snapshot;
  ctool_arena_mark_t mark;
  ctool_status_t status;
  ctool_u32 diagnostic_count;
  ctool_u32 diagnostic_count_after;
  const ctool_diagnostic_t *diagnostic;
  size_t token_bytes;
  int anchor_valid;
  int arena_unchanged;
  int tape_unchanged;
  int unit_zero;
  int failed = 0;

  if (preprocess_fixture(fixture, path, test_case->source, &tape) != 0) {
    return 1;
  }
  token_bytes = (size_t)tape.token_count * sizeof(*snapshot);
  snapshot = (ctool_c_pp_token_t *)malloc(token_bytes);
  if (snapshot == NULL) {
    (void)fprintf(stderr, "%s: %s tape snapshot allocation failed\n",
                  fixture->mode, test_case->name);
    return 1;
  }
  (void)memcpy(snapshot, tape.tokens, token_bytes);
  diagnostic_count = ctool_job_diagnostic_count(fixture->job);
  mark = ctool_arena_mark(ctool_job_arena(fixture->job));
  (void)memset(&unit, 0xa5, sizeof(unit));
  status = ctool_c_parse(fixture->job, &tape, &fixture->parse_request, &unit);
  diagnostic = ctool_job_diagnostic(fixture->job, diagnostic_count);
  diagnostic_count_after = ctool_job_diagnostic_count(fixture->job);
  unit_zero = unit_is_zero(&unit);
  arena_unchanged = arena_marks_equal(
      mark, ctool_arena_mark(ctool_job_arena(fixture->job)));
  tape_unchanged = memcmp(snapshot, tape.tokens, token_bytes) == 0;
  anchor_valid = validate_anchor(fixture) == 0;
  if (status != test_case->status || unit_zero == 0 ||
      diagnostic_count_after != diagnostic_count + 1u ||
      diagnostic == NULL || diagnostic->code != test_case->diagnostic_code ||
      !string_equal(diagnostic->path, path) ||
      (expected_line == 0u ? diagnostic->line == 0u
                           : diagnostic->line != expected_line) ||
      (expected_column == 0u ? diagnostic->column == 0u
                             : diagnostic->column != expected_column) ||
      (expected_message != NULL &&
       !string_equal(diagnostic->message, expected_message)) ||
      arena_unchanged == 0 || tape_unchanged == 0 || anchor_valid == 0) {
    (void)fprintf(stderr,
                  "%s: %s expected %s/0x%08x transactionally, got %s",
                  fixture->mode, test_case->name,
                  ctool_status_name(test_case->status),
                  test_case->diagnostic_code, ctool_status_name(status));
    if (diagnostic != NULL) {
      (void)fprintf(stderr, "/0x%08x", diagnostic->code);
      (void)fprintf(stderr, " at %.*s:%u:%u: %.*s",
                    (int)diagnostic->path.size, diagnostic->path.data,
                    diagnostic->line, diagnostic->column,
                    (int)diagnostic->message.size,
                    diagnostic->message.data);
    }
    (void)fprintf(stderr,
                  " (diagnostics +%u, unit_zero=%d, arena_unchanged=%d, "
                  "tape_unchanged=%d, anchor_valid=%d)",
                  diagnostic_count_after - diagnostic_count, unit_zero,
                  arena_unchanged, tape_unchanged, anchor_valid);
    (void)fprintf(stderr, "\n");
    failed = 1;
  }
  free(snapshot);
  return failed;
}

static int expect_frontend_failure_at(
    frontend_fixture_t *fixture, const frontend_failure_case_t *test_case,
    const char *path, ctool_u32 expected_line,
    ctool_u32 expected_column) {
  return expect_frontend_failure_at_message(
      fixture, test_case, path, expected_line, expected_column, NULL);
}

static int expect_frontend_failure(frontend_fixture_t *fixture,
                                   const frontend_failure_case_t *test_case,
                                   const char *path) {
  return expect_frontend_failure_at(fixture, test_case, path, 0u, 0u);
}

static int finish_frontend_fixture(frontend_fixture_t *fixture) {
  ctool_c_translation_unit_t recovered;
  const ctool_c_binding_t *binding;
  int failed = 0;

  if (parse_valid_fixture(fixture, "/recovery.c",
                          "typedef signed long recovery_t;\n",
                          &recovered) != 0) {
    failed = 1;
  } else {
    binding = find_binding(&recovered, "recovery_t");
    if (recovered.binding_count != 1u || binding == NULL ||
        binding->kind != CTOOL_C_BINDING_TYPEDEF ||
        binding->type >= recovered.graph.type_count ||
        recovered.graph.types[binding->type].kind !=
            CTOOL_C_TYPE_SIGNED_LONG ||
        validate_anchor(fixture) != 0) {
      (void)fprintf(stderr, "%s: same-job recovery or prior result failed\n",
                    fixture->mode);
      failed = 1;
    }
  }
  ctool_job_close(fixture->job);
  fixture->job = NULL;
  return failed;
}

static const ctool_c_tag_t *
find_tag(const ctool_c_translation_unit_t *unit, const char *name) {
  ctool_u32 index;
  for (index = 0u; index < unit->tag_count; index++) {
    if (string_equal(unit->tags[index].name, name) != 0) {
      return &unit->tags[index];
    }
  }
  return NULL;
}

static int validate_attribute_storage_limit(frontend_fixture_t *fixture,
                                            const char *host_root);
static char *build_depth_source(const char *kind, ctool_u32 depth);

static char *build_static_assert_snapshot_source(void) {
  static const char prefix[] = "typedef int ";
  static const char suffix[] =
      " snapshot_type_t;\n"
      "_Static_assert(sizeof(snapshot_type_t) == 4, \"snapshot\");\n";
  const ctool_u32 pointer_count = 96u;
  size_t size = sizeof(prefix) - 1u + pointer_count + sizeof(suffix);
  char *source = (char *)malloc(size);
  size_t cursor = 0u;
  ctool_u32 index;
  if (source == NULL) {
    return NULL;
  }
  (void)memcpy(source + cursor, prefix, sizeof(prefix) - 1u);
  cursor += sizeof(prefix) - 1u;
  for (index = 0u; index < pointer_count; index++) {
    source[cursor++] = '*';
  }
  (void)memcpy(source + cursor, suffix, sizeof(suffix));
  return source;
}

static int validate_static_assert_limits(frontend_fixture_t *fixture,
                                         const char *host_root) {
  static const char anchor_source[] =
      "typedef unsigned int static_assert_limit_anchor_t;\n";
  static const char short_source[] =
      "_Static_assert(0, \"small output\");\n";
  static const char long_source[] =
      "_Static_assert(0, \"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ\");\n";
  char *snapshot_source = build_static_assert_snapshot_source();
  ctool_c_pp_result_t anchor_tape;
  ctool_c_pp_result_t short_tape;
  ctool_c_pp_result_t long_tape;
  ctool_c_pp_result_t snapshot_tape;
  ctool_c_translation_unit_t control_unit;
  ctool_c_translation_unit_t anchor_unit;
  ctool_c_translation_unit_t failed_unit;
  ctool_c_pp_token_t *short_copy = NULL;
  ctool_c_pp_token_t *long_copy = NULL;
  ctool_c_pp_token_t *snapshot_copy = NULL;
  ctool_limits_t limits = ctool_default_limits();
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_arena_mark_t mark;
  const ctool_diagnostic_t *diagnostic;
  const ctool_c_binding_t *anchor_binding;
  ctool_status_t status;
  size_t short_bytes;
  size_t long_bytes;
  size_t snapshot_bytes;
  int failed = 1;

  if (snapshot_source == NULL ||
      parse_valid_fixture(fixture, "/static-assert-snapshot-success.c",
                          snapshot_source, &control_unit) != 0 ||
      find_binding(&control_unit, "snapshot_type_t") == NULL ||
      preprocess_fixture(fixture, "/static-assert-limit-anchor.c",
                         anchor_source, &anchor_tape) != 0 ||
      preprocess_fixture(fixture, "/static-assert-small-output.c",
                         short_source, &short_tape) != 0 ||
      preprocess_fixture(fixture, "/static-assert-long-message.c",
                         long_source, &long_tape) != 0 ||
      preprocess_fixture(fixture, "/static-assert-snapshot-limit.c",
                         snapshot_source, &snapshot_tape) != 0) {
    (void)fprintf(stderr, "static-asserts: limit control differs\n");
    goto cleanup;
  }
  short_bytes = (size_t)short_tape.token_count * sizeof(*short_copy);
  long_bytes = (size_t)long_tape.token_count * sizeof(*long_copy);
  snapshot_bytes =
      (size_t)snapshot_tape.token_count * sizeof(*snapshot_copy);
  short_copy = (ctool_c_pp_token_t *)malloc(short_bytes);
  long_copy = (ctool_c_pp_token_t *)malloc(long_bytes);
  snapshot_copy = (ctool_c_pp_token_t *)malloc(snapshot_bytes);
  if (short_copy == NULL || long_copy == NULL || snapshot_copy == NULL) {
    goto cleanup;
  }
  (void)memcpy(short_copy, short_tape.tokens, short_bytes);
  (void)memcpy(long_copy, long_tape.tokens, long_bytes);
  (void)memcpy(snapshot_copy, snapshot_tape.tokens, snapshot_bytes);

  limits.output_bytes = 256u;
  limits.diagnostic_message_bytes = 512u;
  status = ctool_host_adapter_init(&adapter, host_root);
  if (status != CTOOL_OK) {
    goto cleanup;
  }
  config = ctool_host_job_config(&adapter, limits);
  status = ctool_job_open(&config, &job);
  if (status != CTOOL_OK) {
    goto cleanup;
  }
  (void)memset(&anchor_unit, 0xa5, sizeof(anchor_unit));
  status = ctool_c_parse(job, &anchor_tape, &fixture->parse_request,
                         &anchor_unit);
  anchor_binding = find_binding(&anchor_unit,
                                "static_assert_limit_anchor_t");
  if (status != CTOOL_OK || anchor_binding == NULL ||
      anchor_unit.binding_count != 1u) {
    (void)fprintf(stderr, "static-asserts: small-output anchor failed\n");
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  (void)memset(&failed_unit, 0xa5, sizeof(failed_unit));
  status = ctool_c_parse(job, &short_tape, &fixture->parse_request,
                         &failed_unit);
  diagnostic = ctool_job_diagnostic(job, 0u);
  anchor_binding = find_binding(&anchor_unit,
                                "static_assert_limit_anchor_t");
  if (status != CTOOL_ERR_INPUT || unit_is_zero(&failed_unit) == 0 ||
      ctool_job_diagnostic_count(job) != 1u || diagnostic == NULL ||
      diagnostic->code != CTOOL_C_PARSE_DIAG_STATIC_ASSERT ||
      !string_equal(diagnostic->message,
                    "static assertion failed: small output") ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      memcmp(short_copy, short_tape.tokens, short_bytes) != 0 ||
      anchor_binding == NULL || anchor_unit.binding_count != 1u) {
    (void)fprintf(stderr,
                  "static-asserts: output-independent diagnostic differs\n");
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  (void)memset(&failed_unit, 0xa5, sizeof(failed_unit));
  status = ctool_c_parse(job, &long_tape, &fixture->parse_request,
                         &failed_unit);
  diagnostic = ctool_job_diagnostic(job, 1u);
  anchor_binding = find_binding(&anchor_unit,
                                "static_assert_limit_anchor_t");
  if (status != CTOOL_ERR_LIMIT || unit_is_zero(&failed_unit) == 0 ||
      ctool_job_diagnostic_count(job) != 2u || diagnostic == NULL ||
      diagnostic->code != CTOOL_C_PARSE_DIAG_LIMIT ||
      !string_equal(diagnostic->path, "/static-assert-long-message.c") ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      memcmp(long_copy, long_tape.tokens, long_bytes) != 0 ||
      anchor_binding == NULL || anchor_unit.binding_count != 1u) {
    (void)fprintf(stderr,
                  "static-asserts: message-limit rollback differs\n");
    goto cleanup;
  }
  ctool_job_close(job);
  job = NULL;

  limits = ctool_default_limits();
  limits.arena_block_bytes = 4096u;
  limits.arena_bytes = 4096u;
  status = ctool_host_adapter_init(&adapter, host_root);
  if (status != CTOOL_OK) {
    goto cleanup;
  }
  config = ctool_host_job_config(&adapter, limits);
  status = ctool_job_open(&config, &job);
  if (status != CTOOL_OK) {
    goto cleanup;
  }
  (void)memset(&anchor_unit, 0xa5, sizeof(anchor_unit));
  status = ctool_c_parse(job, &anchor_tape, &fixture->parse_request,
                         &anchor_unit);
  anchor_binding = find_binding(&anchor_unit,
                                "static_assert_limit_anchor_t");
  if (status != CTOOL_OK || anchor_binding == NULL ||
      anchor_unit.binding_count != 1u) {
    (void)fprintf(stderr, "static-asserts: arena-limit anchor failed\n");
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  (void)memset(&failed_unit, 0xa5, sizeof(failed_unit));
  status = ctool_c_parse(job, &snapshot_tape, &fixture->parse_request,
                         &failed_unit);
  diagnostic = ctool_job_diagnostic(job, 0u);
  anchor_binding = find_binding(&anchor_unit,
                                "static_assert_limit_anchor_t");
  if (status != CTOOL_ERR_LIMIT || unit_is_zero(&failed_unit) == 0 ||
      ctool_job_diagnostic_count(job) != 1u || diagnostic == NULL ||
      diagnostic->code != CTOOL_C_PARSE_DIAG_LIMIT ||
      !string_equal(diagnostic->path, "/static-assert-snapshot-limit.c") ||
      diagnostic->line != 2u || diagnostic->column != 40u ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      memcmp(snapshot_copy, snapshot_tape.tokens, snapshot_bytes) != 0 ||
      anchor_binding == NULL || anchor_unit.binding_count != 1u) {
    (void)fprintf(stderr,
                  "static-asserts: layout-snapshot limit differs: %s\n",
                  ctool_status_name(status));
    goto cleanup;
  }
  failed = 0;

cleanup:
  if (job != NULL) {
    ctool_job_close(job);
  }
  free(snapshot_copy);
  free(long_copy);
  free(short_copy);
  free(snapshot_source);
  return failed;
}

static int run_attributes(const char *host_root) {
  static const frontend_failure_case_t failure_cases[] = {
      {"unknown attribute",
       "struct unknown_attr { int value; } __attribute__((mystery));\n",
       CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_ATTRIBUTE},
      {"malformed attribute",
       "struct malformed_attr { int value; } __attribute__((packed);\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_ATTRIBUTE},
      {"unterminated attribute",
       "struct unterminated_attr { int value; } __attribute__((\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_ATTRIBUTE},
      {"zero alignment",
       "struct zero_align { int value __attribute__((aligned(0))); };\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_ATTRIBUTE},
      {"non-power alignment",
       "struct odd_align { int value __attribute__((aligned(3))); };\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_ATTRIBUTE},
      {"negative alignment",
       "struct negative_align { int value __attribute__((aligned(-8))); };\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_ATTRIBUTE},
      {"nonconstant alignment",
       "int alignment_value; int value __attribute__((aligned(alignment_value)));\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION},
      {"packed argument", "struct packed_arg { int value; } "
                          "__attribute__((packed(1)));\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_ATTRIBUTE},
      {"noreturn argument",
       "void bad_noreturn(void) __attribute__((noreturn(1)));\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_ATTRIBUTE},
      {"packed scalar", "unsigned int packed_scalar __attribute__((packed));\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_ATTRIBUTE},
      {"noreturn object", "unsigned int returning_object "
                          "__attribute__((noreturn));\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_ATTRIBUTE},
      {"noreturn record",
       "struct bad_record { int value; } __attribute__((noreturn));\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_ATTRIBUTE},
      {"noreturn member", "struct bad_member { int value "
                          "__attribute__((noreturn)); };\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_ATTRIBUTE},
      {"attributed parameter specifier",
       "void bad_parameter(int __attribute__((aligned(16))) value);\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_ATTRIBUTE},
      {"attributed parameter declarator",
       "void bad_parameter(int value __attribute__((aligned(16))));\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_ATTRIBUTE},
      {"old-style aligned char promotion mismatch",
       "typedef char aligned_char_t __attribute__((aligned(16))); "
       "int promoted(); int promoted(aligned_char_t value);\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_REDEFINITION},
      {"reverse old-style aligned char promotion mismatch",
       "typedef char aligned_char_t __attribute__((aligned(16))); "
       "int promoted(aligned_char_t value); int promoted();\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_REDEFINITION},
      {"misplaced record alignment",
       "__attribute__((aligned(16))) struct misplaced_record { int value; };\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_ATTRIBUTE}};
  static const frontend_failure_case_t disabled_case = {
      "disabled GNU attribute",
      "unsigned int disabled __attribute__((aligned(8)));\n",
      CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_ATTRIBUTE};
  static const char *const exec_types[] = {
      "cupd_header_t", "elf32_ehdr_t", "elf32_phdr_t", "elf32_shdr_t",
      "elf32_sym_t"};
  static const ctool_u32 exec_sizes[] = {20u, 52u, 32u, 40u, 16u};
  frontend_fixture_t fixture;
  ctool_c_pp_include_root_t include_roots[ARRAY_COUNT(active_rows)];
  ctool_c_pp_macro_action_t macro_actions[ARRAY_COUNT(active_rows)];
  ctool_path_t forced_includes[ARRAY_COUNT(active_rows)];
  ctool_c_translation_unit_t unit;
  const ctool_c_tag_t *tag;
  const ctool_c_type_node_t *record;
  const ctool_c_type_layout_t *layout;
  ctool_u32 payload_index;
  ctool_u32 failure_index;
  ctool_u32 active_index;
  int failed = 1;

  if (begin_frontend_fixture(&fixture, "attributes", host_root,
                             8u * 1024u * 1024u) != 0) {
    return 1;
  }
  if (parse_valid_fixture(
          &fixture, "/attributes-packed.c",
          "struct descriptor {\n"
          "  unsigned char tag;\n"
          "  unsigned int payload;\n"
          "} __attribute__((packed));\n",
          &unit) != 0) {
    goto cleanup;
  }
  tag = find_tag(&unit, "descriptor");
  record = tag == NULL ? NULL : type_node(&unit, tag->type);
  layout = tag == NULL ? NULL : type_layout(&unit, tag->type);
  payload_index = record == NULL ? unit.graph.member_count
                                 : record->first_member + 1u;
  if (unit.binding_count != 0u || unit.tag_count != 1u || record == NULL ||
      record->kind != CTOOL_C_TYPE_RECORD ||
      record->record_packed != CTOOL_TRUE || record->member_count != 2u ||
      layout == NULL || layout->size != 5u || layout->alignment != 1u ||
      payload_index >= unit.graph.member_count ||
      unit.graph.members[payload_index].member_packed != CTOOL_FALSE ||
      unit.layout.members[payload_index].byte_offset != 1u ||
      unit.layout.members[payload_index].alignment != 1u) {
    (void)fprintf(stderr, "attributes: packed record semantics differ\n");
    goto cleanup;
  }
  if (parse_valid_fixture(
          &fixture, "/attributes-record-prefix.c",
          "typedef struct __attribute__((packed, aligned(16))) descriptor16 {\n"
          "  unsigned char tag;\n"
          "  unsigned int payload;\n"
          "} descriptor16_t;\n",
          &unit) != 0) {
    goto cleanup;
  }
  {
    const ctool_c_binding_t *binding = find_binding(&unit, "descriptor16_t");
    const ctool_c_tag_t *prefix_tag = find_tag(&unit, "descriptor16");
    const ctool_c_type_node_t *prefix_record =
        prefix_tag == NULL ? NULL : type_node(&unit, prefix_tag->type);
    const ctool_c_type_layout_t *prefix_layout =
        prefix_tag == NULL ? NULL : type_layout(&unit, prefix_tag->type);
    ctool_u32 prefix_payload =
        prefix_record == NULL ? unit.graph.member_count
                              : prefix_record->first_member + 1u;
    if (binding == NULL || binding->kind != CTOOL_C_BINDING_TYPEDEF ||
        prefix_tag == NULL || binding->type != prefix_tag->type ||
        prefix_record == NULL || prefix_record->record_packed != CTOOL_TRUE ||
        prefix_record->explicit_alignment != 16u || prefix_layout == NULL ||
        prefix_layout->size != 16u || prefix_layout->alignment != 16u ||
        prefix_payload >= unit.graph.member_count ||
        unit.layout.members[prefix_payload].byte_offset != 1u ||
        unit.layout.members[prefix_payload].alignment != 1u) {
      (void)fprintf(stderr, "attributes: prefix record semantics differ\n");
      goto cleanup;
    }
  }
  if (parse_valid_fixture(
          &fixture, "/attributes-forward-record.c",
          "struct __attribute__((packed, aligned(16))) forward_attr;\n"
          "struct forward_attr { unsigned char tag; unsigned int payload; };\n"
          "struct later_attr;\n"
          "struct __attribute__((packed, aligned(32))) later_attr {\n"
          "  unsigned char tag; unsigned int payload;\n"
          "};\n",
          &unit) != 0) {
    goto cleanup;
  }
  {
    static const char *const tag_names[] = {"forward_attr", "later_attr"};
    static const ctool_u32 tag_alignments[] = {16u, 32u};
    for (active_index = 0u; active_index < ARRAY_COUNT(tag_names);
         active_index++) {
      const ctool_c_tag_t *forward_tag =
          find_tag(&unit, tag_names[active_index]);
      const ctool_c_type_node_t *forward_record =
          forward_tag == NULL ? NULL : type_node(&unit, forward_tag->type);
      const ctool_c_type_layout_t *forward_layout =
          forward_tag == NULL ? NULL : type_layout(&unit, forward_tag->type);
      if (forward_record == NULL || forward_layout == NULL ||
          forward_record->record_complete != CTOOL_TRUE ||
          forward_record->record_packed != CTOOL_TRUE ||
          forward_record->explicit_alignment != tag_alignments[active_index] ||
          forward_layout->size != tag_alignments[active_index] ||
          forward_layout->alignment != tag_alignments[active_index]) {
        (void)fprintf(stderr,
                      "attributes: forward record attributes differ\n");
        goto cleanup;
      }
    }
  }
  if (parse_valid_fixture(
          &fixture, "/attributes-aligned-compatibility.c",
          "typedef int aligned_int_t __attribute__((aligned(16)));\n"
          "extern aligned_int_t aligned_object_first;\n"
          "extern int aligned_object_first;\n"
          "extern int aligned_object_later;\n"
          "extern aligned_int_t aligned_object_later;\n"
          "extern aligned_int_t *aligned_pointer_first;\n"
          "extern int *aligned_pointer_first;\n"
          "extern int *aligned_pointer_later;\n"
          "extern aligned_int_t *aligned_pointer_later;\n"
          "typedef int repeated_aligned_t __attribute__((aligned(8)));\n"
          "typedef int repeated_aligned_t __attribute__((aligned(32)));\n"
          "typedef int repeated_aligned_t;\n"
          "typedef int *repeated_pointer_t;\n"
          "typedef aligned_int_t *repeated_pointer_t;\n"
          "typedef int aligned_incomplete_array_t[] "
          "__attribute__((aligned(16)));\n"
          "extern aligned_incomplete_array_t combined_aligned_array;\n"
          "extern int combined_aligned_array[4];\n"
          "typedef int nested_inner_t __attribute__((aligned(64)));\n"
          "typedef nested_inner_t nested_effective_t "
          "__attribute__((aligned(16)));\n"
          "typedef int nested_effective_t __attribute__((aligned(32)));\n"
          "typedef int *outer_aligned_pointer_t "
          "__attribute__((aligned(8)));\n"
          "typedef int inner_aligned_t __attribute__((aligned(16)));\n"
          "extern outer_aligned_pointer_t mixed_alignment_first;\n"
          "extern inner_aligned_t *mixed_alignment_first;\n"
          "extern inner_aligned_t *mixed_alignment_later;\n"
          "extern outer_aligned_pointer_t mixed_alignment_later;\n"
          "extern outer_aligned_pointer_t const qualified_mixed_first;\n"
          "extern inner_aligned_t * const qualified_mixed_first;\n"
          "extern inner_aligned_t * const qualified_mixed_later;\n"
          "extern outer_aligned_pointer_t const qualified_mixed_later;\n"
          "typedef int atomic_inner_t __attribute__((aligned(16)));\n"
          "typedef atomic_inner_t * _Atomic atomic_left_t;\n"
          "typedef int *atomic_pointer_t __attribute__((aligned(1)));\n"
          "typedef _Atomic atomic_pointer_t atomic_right_t;\n"
          "extern atomic_left_t *atomic_mixed_first;\n"
          "extern atomic_right_t *atomic_mixed_first;\n"
          "extern atomic_right_t *atomic_mixed_later;\n"
          "extern atomic_left_t *atomic_mixed_later;\n"
          "typedef int *same_aligned_pointer_t;\n"
          "typedef same_aligned_pointer_t same_aligned_t "
          "__attribute__((aligned(1)));\n"
          "typedef _Atomic same_aligned_t same_atomic_outer_t;\n"
          "typedef int * _Atomic same_atomic_pointer_t;\n"
          "typedef same_atomic_pointer_t same_atomic_inner_t "
          "__attribute__((aligned(1)));\n"
          "extern same_atomic_outer_t atomic_same_first;\n"
          "extern same_atomic_inner_t atomic_same_first;\n"
          "extern same_atomic_inner_t atomic_same_later;\n"
          "extern same_atomic_outer_t atomic_same_later;\n",
          &unit) != 0) {
    goto cleanup;
  }
  {
    static const char *const aligned_names[] = {
        "aligned_object_first", "aligned_object_later", "repeated_aligned_t"};
    static const ctool_u32 aligned_values[] = {16u, 16u, 32u};
    for (active_index = 0u; active_index < ARRAY_COUNT(aligned_names);
         active_index++) {
      const ctool_c_binding_t *binding =
          find_binding(&unit, aligned_names[active_index]);
      const ctool_c_type_node_t *aligned_node =
          binding == NULL ? NULL : type_node(&unit, binding->type);
      const ctool_c_type_layout_t *aligned_layout =
          binding == NULL ? NULL : type_layout(&unit, binding->type);
      if (binding == NULL || aligned_node == NULL || aligned_layout == NULL ||
          aligned_node->kind != CTOOL_C_TYPE_ALIGNED ||
          aligned_node->explicit_alignment != aligned_values[active_index] ||
          aligned_layout->size != 4u ||
          aligned_layout->alignment != aligned_values[active_index]) {
        (void)fprintf(stderr,
                      "attributes: aligned type compatibility differs\n");
        goto cleanup;
      }
    }
    {
      static const char *const pointer_names[] = {
          "aligned_pointer_first", "aligned_pointer_later"};
      for (active_index = 0u; active_index < ARRAY_COUNT(pointer_names);
           active_index++) {
        const ctool_c_binding_t *binding =
            find_binding(&unit, pointer_names[active_index]);
        const ctool_c_type_node_t *pointer =
            binding == NULL ? NULL : type_node(&unit, binding->type);
        const ctool_c_type_node_t *referent =
            pointer == NULL ? NULL : type_node(&unit, pointer->referenced_type);
        if (pointer == NULL || pointer->kind != CTOOL_C_TYPE_POINTER ||
            referent == NULL || referent->kind != CTOOL_C_TYPE_ALIGNED ||
            referent->explicit_alignment != 16u) {
          (void)fprintf(
              stderr,
              "attributes: recursive aligned compatibility differs\n");
          goto cleanup;
        }
      }
    }
    {
      const ctool_c_binding_t *binding =
          find_binding(&unit, "repeated_pointer_t");
      const ctool_c_type_node_t *pointer =
          binding == NULL ? NULL : type_node(&unit, binding->type);
      const ctool_c_type_node_t *referent =
          pointer == NULL ? NULL : type_node(&unit, pointer->referenced_type);
      if (binding == NULL || binding->kind != CTOOL_C_BINDING_TYPEDEF ||
          pointer == NULL || pointer->kind != CTOOL_C_TYPE_POINTER ||
          referent == NULL || referent->kind != CTOOL_C_TYPE_ALIGNED ||
          referent->explicit_alignment != 16u) {
        (void)fprintf(stderr,
                      "attributes: repeated aligned typedef differs\n");
        goto cleanup;
      }
    }
    {
      const ctool_c_binding_t *binding =
          find_binding(&unit, "combined_aligned_array");
      const ctool_c_type_node_t *aligned =
          binding == NULL ? NULL : type_node(&unit, binding->type);
      const ctool_c_type_node_t *array =
          aligned == NULL ? NULL : type_node(&unit, aligned->referenced_type);
      const ctool_c_type_layout_t *combined_layout =
          binding == NULL ? NULL : type_layout(&unit, binding->type);
      if (binding == NULL || binding->kind != CTOOL_C_BINDING_OBJECT ||
          aligned == NULL || aligned->kind != CTOOL_C_TYPE_ALIGNED ||
          aligned->explicit_alignment != 16u || array == NULL ||
          array->kind != CTOOL_C_TYPE_ARRAY ||
          array->array_bound_kind != CTOOL_C_ARRAY_FIXED ||
          array->element_count != 4u || combined_layout == NULL ||
          combined_layout->size != 16u ||
          combined_layout->alignment != 16u) {
        (void)fprintf(stderr,
                      "attributes: aligned array composite differs\n");
        goto cleanup;
      }
    }
    {
      const ctool_c_binding_t *binding =
          find_binding(&unit, "nested_effective_t");
      const ctool_c_type_node_t *aligned =
          binding == NULL ? NULL : type_node(&unit, binding->type);
      const ctool_c_type_layout_t *nested_layout =
          binding == NULL ? NULL : type_layout(&unit, binding->type);
      if (binding == NULL || binding->kind != CTOOL_C_BINDING_TYPEDEF ||
          aligned == NULL || aligned->kind != CTOOL_C_TYPE_ALIGNED ||
          aligned->explicit_alignment != 32u || nested_layout == NULL ||
          nested_layout->size != 4u || nested_layout->alignment != 32u) {
        (void)fprintf(stderr,
                      "attributes: nested effective alignment differs\n");
        goto cleanup;
      }
    }
    {
      static const char *const mixed_names[] = {
          "mixed_alignment_first", "mixed_alignment_later"};
      for (active_index = 0u; active_index < ARRAY_COUNT(mixed_names);
           active_index++) {
        const ctool_c_binding_t *binding =
            find_binding(&unit, mixed_names[active_index]);
        const ctool_c_type_node_t *outer =
            binding == NULL ? NULL : type_node(&unit, binding->type);
        const ctool_c_type_node_t *pointer =
            outer == NULL ? NULL : type_node(&unit, outer->referenced_type);
        const ctool_c_type_node_t *inner =
            pointer == NULL ? NULL
                            : type_node(&unit, pointer->referenced_type);
        const ctool_c_type_layout_t *mixed_layout =
            binding == NULL ? NULL : type_layout(&unit, binding->type);
        if (binding == NULL || binding->kind != CTOOL_C_BINDING_OBJECT ||
            outer == NULL || outer->kind != CTOOL_C_TYPE_ALIGNED ||
            outer->explicit_alignment != 8u || pointer == NULL ||
            pointer->kind != CTOOL_C_TYPE_POINTER || inner == NULL ||
            inner->kind != CTOOL_C_TYPE_ALIGNED ||
            inner->explicit_alignment != 16u || mixed_layout == NULL ||
            mixed_layout->size != 4u || mixed_layout->alignment != 8u) {
          (void)fprintf(stderr,
                        "attributes: opposing aligned composite differs\n");
          goto cleanup;
        }
      }
    }
    {
      static const char *const qualified_names[] = {
          "qualified_mixed_first", "qualified_mixed_later"};
      for (active_index = 0u; active_index < ARRAY_COUNT(qualified_names);
           active_index++) {
        const ctool_c_binding_t *binding =
            find_binding(&unit, qualified_names[active_index]);
        const ctool_c_type_node_t *outer =
            binding == NULL ? NULL : type_node(&unit, binding->type);
        const ctool_c_type_node_t *pointer =
            outer == NULL ? NULL : type_node(&unit, outer->referenced_type);
        const ctool_c_type_node_t *inner =
            pointer == NULL ? NULL
                            : type_node(&unit, pointer->referenced_type);
        if (binding == NULL || binding->kind != CTOOL_C_BINDING_OBJECT ||
            outer == NULL || outer->kind != CTOOL_C_TYPE_ALIGNED ||
            outer->explicit_alignment != 8u ||
            (outer->qualifiers & CTOOL_C_QUAL_CONST) == 0u ||
            pointer == NULL || pointer->kind != CTOOL_C_TYPE_POINTER ||
            inner == NULL || inner->kind != CTOOL_C_TYPE_ALIGNED ||
            inner->explicit_alignment != 16u) {
          (void)fprintf(stderr,
                        "attributes: qualified aligned composite differs\n");
          goto cleanup;
        }
      }
    }
    {
      static const char *const atomic_names[] = {
          "atomic_mixed_first", "atomic_mixed_later"};
      for (active_index = 0u; active_index < ARRAY_COUNT(atomic_names);
           active_index++) {
        const ctool_c_binding_t *binding =
            find_binding(&unit, atomic_names[active_index]);
        const ctool_c_type_node_t *outer_pointer =
            binding == NULL ? NULL : type_node(&unit, binding->type);
        const ctool_c_type_node_t *atomic_aligned =
            outer_pointer == NULL
                ? NULL
                : type_node(&unit, outer_pointer->referenced_type);
        const ctool_c_type_node_t *inner_pointer =
            atomic_aligned == NULL
                ? NULL
                : type_node(&unit, atomic_aligned->referenced_type);
        const ctool_c_type_node_t *inner_aligned =
            inner_pointer == NULL
                ? NULL
                : type_node(&unit, inner_pointer->referenced_type);
        const ctool_c_type_layout_t *atomic_layout =
            outer_pointer == NULL
                ? NULL
                : type_layout(&unit, outer_pointer->referenced_type);
        if (binding == NULL || binding->kind != CTOOL_C_BINDING_OBJECT ||
            outer_pointer == NULL ||
            outer_pointer->kind != CTOOL_C_TYPE_POINTER ||
            atomic_aligned == NULL ||
            atomic_aligned->kind != CTOOL_C_TYPE_ALIGNED ||
            atomic_aligned->explicit_alignment != 1u ||
            (atomic_aligned->qualifiers & CTOOL_C_QUAL_ATOMIC) == 0u ||
            inner_pointer == NULL ||
            inner_pointer->kind != CTOOL_C_TYPE_POINTER ||
            inner_aligned == NULL ||
            inner_aligned->kind != CTOOL_C_TYPE_ALIGNED ||
            inner_aligned->explicit_alignment != 16u ||
            atomic_layout == NULL || atomic_layout->size != 4u ||
            atomic_layout->alignment != 4u) {
          (void)fprintf(stderr,
                        "attributes: atomic aligned composite differs\n");
          goto cleanup;
        }
      }
    }
    {
      static const char *const atomic_same_names[] = {
          "atomic_same_first", "atomic_same_later"};
      for (active_index = 0u; active_index < ARRAY_COUNT(atomic_same_names);
           active_index++) {
        const ctool_c_binding_t *binding =
            find_binding(&unit, atomic_same_names[active_index]);
        const ctool_c_type_node_t *aligned =
            binding == NULL ? NULL : type_node(&unit, binding->type);
        const ctool_c_type_node_t *pointer =
            aligned == NULL ? NULL : type_node(&unit, aligned->referenced_type);
        const ctool_c_type_layout_t *same_layout =
            binding == NULL ? NULL : type_layout(&unit, binding->type);
        if (binding == NULL || binding->kind != CTOOL_C_BINDING_OBJECT ||
            aligned == NULL || aligned->kind != CTOOL_C_TYPE_ALIGNED ||
            aligned->explicit_alignment != 1u ||
            (aligned->qualifiers & CTOOL_C_QUAL_ATOMIC) == 0u ||
            pointer == NULL || pointer->kind != CTOOL_C_TYPE_POINTER ||
            same_layout == NULL || same_layout->size != 4u ||
            same_layout->alignment != 4u) {
          (void)fprintf(
              stderr,
              "attributes: same-layer atomic alignment differs\n");
          goto cleanup;
        }
      }
    }
  }
  if (parse_valid_fixture(
          &fixture, "/attributes-normalized-spellings.c",
          "typedef struct __attribute((, __packed__(), aligned(), "
          "__aligned__(32),)) normalized {\n"
          "  unsigned char tag;\n"
          "  unsigned int payload;\n"
          "} normalized_t;\n"
          "__attribute((__noreturn__())) void normalized_fatal(void);\n",
          &unit) != 0) {
    goto cleanup;
  }
  {
    const ctool_c_binding_t *binding = find_binding(&unit, "normalized_t");
    const ctool_c_binding_t *function =
        find_binding(&unit, "normalized_fatal");
    const ctool_c_tag_t *normalized_tag = find_tag(&unit, "normalized");
    const ctool_c_type_node_t *normalized_record =
        normalized_tag == NULL ? NULL : type_node(&unit, normalized_tag->type);
    const ctool_c_type_layout_t *normalized_layout =
        normalized_tag == NULL ? NULL : type_layout(&unit, normalized_tag->type);
    if (binding == NULL || function == NULL || normalized_tag == NULL ||
        normalized_record == NULL || normalized_layout == NULL ||
        binding->type != normalized_tag->type ||
        normalized_record->record_packed != CTOOL_TRUE ||
        normalized_record->explicit_alignment != 32u ||
        normalized_layout->size != 32u || normalized_layout->alignment != 32u ||
        (function->attributes & CTOOL_C_DECL_ATTR_NORETURN) == 0u) {
      (void)fprintf(stderr,
                    "attributes: normalized GNU spellings differ\n");
      goto cleanup;
    }
  }
  if (parse_valid_fixture(
          &fixture, "/attributes-prefix-declaration.c",
          "__attribute__((noreturn)) void leading_fatal(void);\n"
          "static __attribute__((aligned(16))) unsigned char leading_object;\n"
          "static unsigned char bare_aligned_object "
          "__attribute__((aligned));\n"
          "void aligned_function(void) __attribute__((aligned(32)));\n",
          &unit) != 0) {
    goto cleanup;
  }
  {
    const ctool_c_binding_t *function = find_binding(&unit, "leading_fatal");
    const ctool_c_binding_t *object = find_binding(&unit, "leading_object");
    const ctool_c_binding_t *bare =
        find_binding(&unit, "bare_aligned_object");
    const ctool_c_binding_t *aligned_function =
        find_binding(&unit, "aligned_function");
    if (unit.binding_count != 4u || function == NULL || object == NULL ||
        bare == NULL || aligned_function == NULL ||
        function->kind != CTOOL_C_BINDING_FUNCTION ||
        (function->attributes & CTOOL_C_DECL_ATTR_NORETURN) == 0u ||
        function->minimum_alignment != 0u ||
        object->kind != CTOOL_C_BINDING_OBJECT ||
        object->storage != CTOOL_C_STORAGE_STATIC ||
        object->linkage != CTOOL_C_LINKAGE_INTERNAL ||
        object->attributes != 0u || object->minimum_alignment != 16u ||
        bare->kind != CTOOL_C_BINDING_OBJECT ||
        bare->minimum_alignment != 16u ||
        aligned_function->kind != CTOOL_C_BINDING_FUNCTION ||
        aligned_function->attributes != 0u ||
        aligned_function->minimum_alignment != 32u) {
      (void)fprintf(stderr,
                    "attributes: prefix declaration semantics differ\n");
      goto cleanup;
    }
  }
  if (parse_valid_fixture(
          &fixture, "/attributes-object-aligned.c",
          "unsigned char aligned_object __attribute__((aligned(32)));\n"
          "unsigned char plain_object;\n"
          "unsigned char comma_aligned __attribute__((aligned(16))), comma_plain;\n"
          "extern unsigned char aligned_later;\n"
          "extern unsigned char aligned_later __attribute__((aligned(8)));\n"
          "extern unsigned char aligned_first __attribute__((aligned(4)));\n"
          "extern unsigned char aligned_first;\n",
          &unit) != 0) {
    goto cleanup;
  }
  {
    const ctool_c_binding_t *aligned = find_binding(&unit, "aligned_object");
    const ctool_c_binding_t *plain = find_binding(&unit, "plain_object");
    const ctool_c_binding_t *comma_aligned =
        find_binding(&unit, "comma_aligned");
    const ctool_c_binding_t *comma_plain = find_binding(&unit, "comma_plain");
    const ctool_c_binding_t *later = find_binding(&unit, "aligned_later");
    const ctool_c_binding_t *first = find_binding(&unit, "aligned_first");
    if (unit.binding_count != 6u || aligned == NULL || plain == NULL ||
        comma_aligned == NULL || comma_plain == NULL || later == NULL ||
        first == NULL || aligned->kind != CTOOL_C_BINDING_OBJECT ||
        aligned->type != plain->type || aligned->minimum_alignment != 32u ||
        plain->minimum_alignment != 0u ||
        comma_aligned->minimum_alignment != 16u ||
        comma_plain->minimum_alignment != 0u ||
        later->minimum_alignment != 8u || first->minimum_alignment != 4u) {
      (void)fprintf(stderr, "attributes: aligned object semantics differ\n");
      goto cleanup;
    }
  }
  if (parse_valid_fixture(
          &fixture, "/attributes-member-aligned.c",
          "struct aligned_member {\n"
          "  unsigned char tag;\n"
          "  unsigned char payload[3] __attribute__((aligned(16)));\n"
          "  unsigned char tail;\n"
          "};\n",
          &unit) != 0) {
    goto cleanup;
  }
  tag = find_tag(&unit, "aligned_member");
  record = tag == NULL ? NULL : type_node(&unit, tag->type);
  layout = tag == NULL ? NULL : type_layout(&unit, tag->type);
  payload_index = record == NULL ? unit.graph.member_count
                                 : record->first_member + 1u;
  if (unit.binding_count != 0u || unit.tag_count != 1u || record == NULL ||
      record->kind != CTOOL_C_TYPE_RECORD || record->member_count != 3u ||
      layout == NULL || layout->size != 32u || layout->alignment != 16u ||
      payload_index >= unit.graph.member_count ||
      unit.graph.members[payload_index].explicit_alignment != 16u ||
      unit.layout.members[payload_index].byte_offset != 16u ||
      unit.layout.members[payload_index].size != 3u ||
      unit.layout.members[payload_index].alignment != 16u ||
      unit.layout.members[payload_index + 1u].byte_offset != 19u) {
    (void)fprintf(stderr, "attributes: aligned member semantics differ\n");
    goto cleanup;
  }
  if (parse_valid_fixture(
          &fixture, "/attributes-member-packed.c",
          "struct packed_member {\n"
          "  unsigned char tag;\n"
          "  unsigned int payload __attribute__((packed));\n"
          "  unsigned char tail;\n"
          "};\n",
          &unit) != 0) {
    goto cleanup;
  }
  tag = find_tag(&unit, "packed_member");
  record = tag == NULL ? NULL : type_node(&unit, tag->type);
  layout = tag == NULL ? NULL : type_layout(&unit, tag->type);
  payload_index = record == NULL ? unit.graph.member_count
                                 : record->first_member + 1u;
  if (unit.binding_count != 0u || unit.tag_count != 1u || record == NULL ||
      record->kind != CTOOL_C_TYPE_RECORD ||
      record->record_packed != CTOOL_FALSE || record->member_count != 3u ||
      layout == NULL || layout->size != 6u || layout->alignment != 1u ||
      payload_index >= unit.graph.member_count ||
      unit.graph.members[payload_index].member_packed != CTOOL_TRUE ||
      unit.layout.members[payload_index].byte_offset != 1u ||
      unit.layout.members[payload_index].size != 4u ||
      unit.layout.members[payload_index].alignment != 1u ||
      unit.layout.members[payload_index + 1u].byte_offset != 5u) {
    (void)fprintf(stderr, "attributes: packed member semantics differ\n");
    goto cleanup;
  }
  if (parse_valid_fixture(
          &fixture, "/attributes-typedef-aligned.c",
          "typedef struct aligned_alias {\n"
          "  unsigned char bytes[64];\n"
          "} aligned_alias_t __attribute__((aligned(64)));\n",
          &unit) != 0) {
    goto cleanup;
  }
  {
    const ctool_c_binding_t *binding = find_binding(&unit, "aligned_alias_t");
    const ctool_c_tag_t *record_tag = find_tag(&unit, "aligned_alias");
    const ctool_c_type_node_t *aligned =
        binding == NULL ? NULL : type_node(&unit, binding->type);
    const ctool_c_type_node_t *natural_record =
        record_tag == NULL ? NULL : type_node(&unit, record_tag->type);
    const ctool_c_type_layout_t *aligned_layout =
        binding == NULL ? NULL : type_layout(&unit, binding->type);
    const ctool_c_type_layout_t *natural_layout =
        record_tag == NULL ? NULL : type_layout(&unit, record_tag->type);
    if (unit.binding_count != 1u || unit.tag_count != 1u || binding == NULL ||
        binding->kind != CTOOL_C_BINDING_TYPEDEF || aligned == NULL ||
        aligned->kind != CTOOL_C_TYPE_ALIGNED ||
        aligned->explicit_alignment != 64u || record_tag == NULL ||
        aligned->referenced_type != record_tag->type || natural_record == NULL ||
        natural_record->kind != CTOOL_C_TYPE_RECORD ||
        natural_record->explicit_alignment != 0u || aligned_layout == NULL ||
        aligned_layout->size != 64u || aligned_layout->alignment != 64u ||
        natural_layout == NULL || natural_layout->size != 64u ||
        natural_layout->alignment != 1u) {
      (void)fprintf(stderr, "attributes: aligned typedef semantics differ\n");
      goto cleanup;
    }
  }
  if (parse_valid_fixture(
          &fixture, "/attributes-noreturn.c",
          "void fatal_first(const char *message) __attribute__((noreturn));\n"
          "void fatal_first(const char *message);\n"
          "void fatal_later(void);\n"
          "void fatal_later(void) __attribute__((__noreturn__));\n",
          &unit) != 0) {
    goto cleanup;
  }
  {
    const ctool_c_binding_t *first = find_binding(&unit, "fatal_first");
    const ctool_c_binding_t *later = find_binding(&unit, "fatal_later");
    if (unit.binding_count != 2u || first == NULL || later == NULL ||
        first->kind != CTOOL_C_BINDING_FUNCTION ||
        later->kind != CTOOL_C_BINDING_FUNCTION ||
        (first->attributes & CTOOL_C_DECL_ATTR_NORETURN) == 0u ||
        (later->attributes & CTOOL_C_DECL_ATTR_NORETURN) == 0u ||
        !dual_location_matches(&first->location, &first->physical_location,
                               "/attributes-noreturn.c", 1u) ||
        !dual_location_matches(&later->location, &later->physical_location,
                               "/attributes-noreturn.c", 3u)) {
      (void)fprintf(stderr, "attributes: noreturn binding semantics differ\n");
      goto cleanup;
    }
  }
  if (build_kernel_profile(&fixture.pp_request, include_roots, macro_actions,
                           forced_includes) != 0) {
    goto cleanup;
  }
  if (parse_loaded_fixture(&fixture, "/kernel/core/process.h", NULL, 0u,
                           &unit) != 0) {
    goto cleanup;
  }
  {
    const ctool_c_binding_t *binding = find_binding(&unit, "process_t");
    const ctool_c_type_node_t *process =
        binding == NULL ? NULL : type_node(&unit, binding->type);
    const ctool_c_type_layout_t *process_layout =
        binding == NULL ? NULL : type_layout(&unit, binding->type);
    ctool_u32 member_index = unit.graph.member_count;
    const ctool_c_record_member_t *member =
        process == NULL
            ? NULL
            : find_record_member(&unit, process, "fp_state", &member_index);
    if (binding == NULL || binding->kind != CTOOL_C_BINDING_TYPEDEF ||
        process == NULL || process->kind != CTOOL_C_TYPE_RECORD ||
        process_layout == NULL || process_layout->size != 656u ||
        process_layout->alignment != 16u || member == NULL ||
        member->explicit_alignment != 16u ||
        member_index >= unit.layout.member_count ||
        unit.layout.members[member_index].byte_offset != 80u ||
        unit.layout.members[member_index].size != 512u ||
        unit.layout.members[member_index].alignment != 16u) {
      (void)fprintf(stderr, "attributes: unchanged process ABI differs\n");
      goto cleanup;
    }
  }
  if (parse_loaded_fixture(&fixture, "/kernel/smp/percpu.h", "_Static_assert",
                           0u, &unit) != 0) {
    goto cleanup;
  }
  {
    const ctool_c_binding_t *binding = find_binding(&unit, "per_cpu_t");
    const ctool_c_type_node_t *aligned =
        binding == NULL ? NULL : type_node(&unit, binding->type);
    const ctool_c_type_node_t *body =
        aligned == NULL ? NULL : type_node(&unit, aligned->referenced_type);
    const ctool_c_type_layout_t *aligned_layout =
        binding == NULL ? NULL : type_layout(&unit, binding->type);
    const ctool_c_type_layout_t *body_layout =
        aligned == NULL ? NULL : type_layout(&unit, aligned->referenced_type);
    if (binding == NULL || binding->kind != CTOOL_C_BINDING_TYPEDEF ||
        aligned == NULL || aligned->kind != CTOOL_C_TYPE_ALIGNED ||
        aligned->explicit_alignment != 64u || body == NULL ||
        body->kind != CTOOL_C_TYPE_RECORD || body->explicit_alignment != 0u ||
        aligned_layout == NULL || aligned_layout->size != 128u ||
        aligned_layout->alignment != 64u || body_layout == NULL ||
        body_layout->size != 128u || body_layout->alignment != 4u ||
        !dual_location_matches(&binding->location,
                               &binding->physical_location,
                               "/kernel/smp/percpu.h", 33u)) {
      (void)fprintf(stderr, "attributes: unchanged per-CPU ABI differs\n");
      goto cleanup;
    }
  }
  if (parse_loaded_fixture(&fixture, "/drivers/e1000.c", "e1000_init", 0u,
                           &unit) != 0) {
    goto cleanup;
  }
  {
    static const char *const descriptor_names[] = {
        "e1000_rx_desc_t", "e1000_tx_desc_t"};
    for (active_index = 0u;
         active_index < ARRAY_COUNT(descriptor_names); active_index++) {
      const ctool_c_binding_t *binding =
          find_binding(&unit, descriptor_names[active_index]);
      const ctool_c_type_node_t *descriptor =
          binding == NULL ? NULL : type_node(&unit, binding->type);
      const ctool_c_type_layout_t *descriptor_layout =
          binding == NULL ? NULL : type_layout(&unit, binding->type);
      if (binding == NULL || binding->kind != CTOOL_C_BINDING_TYPEDEF ||
          descriptor == NULL || descriptor->kind != CTOOL_C_TYPE_RECORD ||
          descriptor->record_packed != CTOOL_TRUE ||
          descriptor->explicit_alignment != 16u || descriptor_layout == NULL ||
          descriptor_layout->size != 16u ||
          descriptor_layout->alignment != 16u) {
        (void)fprintf(stderr,
                      "attributes: unchanged E1000 descriptor ABI differs\n");
        goto cleanup;
      }
    }
  }
  if (parse_loaded_fixture(&fixture, "/kernel/cpu/idt.h", NULL, 0u, &unit) !=
      0) {
    goto cleanup;
  }
  {
    static const char *const idt_tags[] = {"idt_entry", "idt_ptr"};
    static const ctool_u32 idt_sizes[] = {8u, 6u};
    for (active_index = 0u; active_index < ARRAY_COUNT(idt_tags);
         active_index++) {
      const ctool_c_tag_t *active_tag =
          find_tag(&unit, idt_tags[active_index]);
      const ctool_c_type_node_t *active_record =
          active_tag == NULL ? NULL : type_node(&unit, active_tag->type);
      const ctool_c_type_layout_t *record_layout =
          active_tag == NULL ? NULL : type_layout(&unit, active_tag->type);
      if (active_record == NULL ||
          active_record->kind != CTOOL_C_TYPE_RECORD ||
          active_record->record_packed != CTOOL_TRUE || record_layout == NULL ||
          record_layout->size != idt_sizes[active_index] ||
          record_layout->alignment != 1u) {
        (void)fprintf(stderr, "attributes: unchanged IDT ABI differs\n");
        goto cleanup;
      }
    }
  }
  if (parse_loaded_fixture(&fixture, "/kernel/lang/exec.h", NULL, 0u, &unit) !=
      0) {
    goto cleanup;
  }
  for (active_index = 0u; active_index < ARRAY_COUNT(exec_types);
       active_index++) {
    const ctool_c_binding_t *binding =
        find_binding(&unit, exec_types[active_index]);
    const ctool_c_type_node_t *active_record =
        binding == NULL ? NULL : type_node(&unit, binding->type);
    const ctool_c_type_layout_t *record_layout =
        binding == NULL ? NULL : type_layout(&unit, binding->type);
    if (binding == NULL || binding->kind != CTOOL_C_BINDING_TYPEDEF ||
        active_record == NULL ||
        active_record->kind != CTOOL_C_TYPE_RECORD ||
        active_record->record_packed != CTOOL_TRUE || record_layout == NULL ||
        record_layout->size != exec_sizes[active_index] ||
        record_layout->alignment != 1u) {
      (void)fprintf(stderr, "attributes: unchanged exec ABI differs\n");
      goto cleanup;
    }
  }
  if (parse_loaded_fixture(&fixture, "/kernel/core/panic.h", NULL, 0u,
                           &unit) != 0) {
    goto cleanup;
  }
  {
    static const char *const noreturn_functions[] = {
        "kernel_panic", "kernel_panic_regs", "panic_fpu"};
    const ctool_c_binding_t *ordinary =
        find_binding(&unit, "print_stack_trace");
    for (active_index = 0u;
         active_index < ARRAY_COUNT(noreturn_functions); active_index++) {
      const ctool_c_binding_t *binding =
          find_binding(&unit, noreturn_functions[active_index]);
      if (binding == NULL || binding->kind != CTOOL_C_BINDING_FUNCTION ||
          (binding->attributes & CTOOL_C_DECL_ATTR_NORETURN) == 0u) {
        (void)fprintf(stderr,
                      "attributes: unchanged panic semantics differ\n");
        goto cleanup;
      }
    }
    if (ordinary == NULL || ordinary->kind != CTOOL_C_BINDING_FUNCTION ||
        ordinary->attributes != 0u) {
      (void)fprintf(stderr,
                    "attributes: ordinary panic declaration differs\n");
      goto cleanup;
    }
  }
  if (parse_valid_fixture(
          &fixture, "/attributes-body.c",
          "void attributed_body(void) __attribute__((noreturn)) { return; }\n",
          &unit) != 0) {
    goto cleanup;
  }
  {
    const ctool_c_binding_t *attributed =
        find_binding(&unit, "attributed_body");
    const ctool_c_function_definition_t *definition =
        unit.function_definition_count == 1u
            ? &unit.function_definitions[0]
            : NULL;
    const ctool_c_statement_t *body_statement =
        definition != NULL && definition->body < unit.statement_count
            ? &unit.statements[definition->body]
            : NULL;
    ctool_u32 return_index =
        body_statement != NULL && body_statement->child_count == 1u &&
                body_statement->first_child < unit.statement_child_count
            ? unit.statement_children[body_statement->first_child]
            : CTOOL_C_AST_NONE;
    const ctool_c_statement_t *return_statement =
        return_index < unit.statement_count ? &unit.statements[return_index]
                                            : NULL;
    if (unit.binding_count != 1u || attributed == NULL ||
        attributed->kind != CTOOL_C_BINDING_FUNCTION ||
        attributed->attributes != CTOOL_C_DECL_ATTR_NORETURN ||
        definition == NULL ||
        definition->binding != find_binding_index(&unit, "attributed_body") ||
        body_statement == NULL ||
        body_statement->kind != CTOOL_C_STATEMENT_COMPOUND ||
        return_statement == NULL ||
        return_statement->kind != CTOOL_C_STATEMENT_RETURN ||
        return_statement->expression != CTOOL_C_AST_NONE) {
      (void)fprintf(stderr,
                    "attributes: attributed function body differs\n");
      goto cleanup;
    }
  }
  for (failure_index = 0u; failure_index < ARRAY_COUNT(failure_cases);
       failure_index++) {
    if (expect_frontend_failure(&fixture, &failure_cases[failure_index],
                                "/attributes-invalid.c") != 0) {
      goto cleanup;
    }
  }
  fixture.pp_request.gnu_extensions = CTOOL_FALSE;
  fixture.parse_request.gnu_extensions = CTOOL_FALSE;
  if (expect_frontend_failure(&fixture, &disabled_case,
                              "/attributes-disabled.c") != 0) {
    fixture.pp_request.gnu_extensions = CTOOL_TRUE;
    fixture.parse_request.gnu_extensions = CTOOL_TRUE;
    goto cleanup;
  }
  fixture.pp_request.gnu_extensions = CTOOL_TRUE;
  fixture.parse_request.gnu_extensions = CTOOL_TRUE;
  if (validate_attribute_storage_limit(&fixture, host_root) != 0) {
    goto cleanup;
  }
  failed = 0;

cleanup:
  if (finish_frontend_fixture(&fixture) != 0) {
    failed = 1;
  }
  if (failed == 0) {
    (void)printf("attributes: ok\n");
  }
  return failed;
}

static ctool_bool frontend_find_source_text(ctool_bytes_t source,
                                            const char *text,
                                            ctool_u32 first,
                                            ctool_u32 *offset_out) {
  size_t text_size_host = strlen(text);
  ctool_u32 text_size;
  ctool_u32 offset;
  if (text_size_host == 0u || text_size_host > 0xffffffffu ||
      first > source.size) {
    return CTOOL_FALSE;
  }
  text_size = (ctool_u32)text_size_host;
  if (text_size > source.size - first) {
    return CTOOL_FALSE;
  }
  offset = first;
  for (;;) {
    if (memcmp(source.data + offset, text, text_size) == 0) {
      *offset_out = offset;
      return CTOOL_TRUE;
    }
    if (offset == source.size - text_size) {
      break;
    }
    offset++;
  }
  return CTOOL_FALSE;
}

static char *build_active_assertion_fixture(
    frontend_fixture_t *fixture, const char *source_path,
    const char *prefix, const char *first_text, const char *last_text,
    ctool_u32 expected_assertions) {
  ctool_path_t path;
  ctool_source_t source;
  ctool_status_t status;
  ctool_u32 first;
  ctool_u32 last;
  ctool_u32 last_size;
  ctool_u32 span_size;
  ctool_u32 assertion_count = 0u;
  ctool_u32 assertion_offset;
  ctool_u32 cursor;
  size_t prefix_size = strlen(prefix);
  size_t last_size_host = strlen(last_text);
  size_t total_size;
  char *result;
  path.text = ctool_string(source_path);
  status = ctool_job_load_source(fixture->job, &path, &source);
  if (status != CTOOL_OK || prefix_size > 0xffffffffu ||
      last_size_host == 0u || last_size_host > 0xffffffffu ||
      frontend_find_source_text(source.contents, first_text, 0u, &first) ==
          CTOOL_FALSE ||
      frontend_find_source_text(source.contents, last_text, first, &last) ==
          CTOOL_FALSE) {
    (void)fprintf(stderr,
                  "%s: active assertion span is unavailable in %s\n",
                  fixture->mode, source_path);
    return NULL;
  }
  last_size = (ctool_u32)last_size_host;
  if (last > source.contents.size ||
      last_size > source.contents.size - last) {
    return NULL;
  }
  last += last_size;
  while (last < source.contents.size &&
         (source.contents.data[last] == '\r' ||
          source.contents.data[last] == '\n')) {
    last++;
  }
  span_size = last - first;
  cursor = first;
  while (frontend_find_source_text(source.contents, "_Static_assert", cursor,
                                   &assertion_offset) == CTOOL_TRUE &&
         assertion_offset < last) {
    assertion_count++;
    cursor = assertion_offset + 1u;
  }
  if (assertion_count != expected_assertions) {
    (void)fprintf(stderr,
                  "%s: expected %u active assertions in %s, found %u\n",
                  fixture->mode, expected_assertions, source_path,
                  assertion_count);
    return NULL;
  }
  if ((size_t)span_size > SIZE_MAX - prefix_size - 1u) {
    return NULL;
  }
  total_size = prefix_size + (size_t)span_size;
  if (total_size >= 0xffffffffu) {
    return NULL;
  }
  result = (char *)malloc(total_size + 1u);
  if (result == NULL) {
    return NULL;
  }
  (void)memcpy(result, prefix, prefix_size);
  (void)memcpy(result + prefix_size, source.contents.data + first,
               (size_t)span_size);
  result[total_size] = '\0';
  return result;
}

static int run_static_asserts(const char *host_root) {
  static const frontend_failure_case_t failure_cases[] = {
      {"false static assertion",
       "_Static_assert(sizeof(int) == 8, \"int width drift\");\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_STATIC_ASSERT},
      {"zero static assertion",
       "_Static_assert(0, \"zero is false\");\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_STATIC_ASSERT},
      {"missing static assertion message", "_Static_assert(1);\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_STATIC_ASSERT},
      {"non-string static assertion message", "_Static_assert(1, 42);\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_STATIC_ASSERT},
      {"nonconstant static assertion",
       "int value; _Static_assert(value == 0, \"not constant\");\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION},
      {"comparison result width overflow",
       "_Static_assert((0 == 0) + 0x7fffffff > 0, \"comparison width\");\n",
       CTOOL_ERR_OVERFLOW, CTOOL_C_PARSE_DIAG_OVERFLOW},
      {"incomplete array sizeof",
       "typedef int incomplete_array_t[]; "
       "_Static_assert(sizeof(incomplete_array_t) == 0, \"incomplete\");\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION},
      {"incomplete record sizeof",
       "struct incomplete_record; "
       "_Static_assert(sizeof(struct incomplete_record) == 0, "
       "\"incomplete\"); "
       "struct incomplete_record { int value; };\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION},
      {"void sizeof", "_Static_assert(sizeof(void) == 0, \"void\");\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION},
      {"function sizeof",
       "typedef int function_t(void); "
       "_Static_assert(sizeof(function_t) == 0, \"function\");\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION},
      {"self record sizeof",
       "struct self_sized { "
       "_Static_assert(sizeof(struct self_sized) == 4, \"self\"); "
       "int value; };\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION},
      {"storage class in sizeof type name",
       "_Static_assert(sizeof(static int) == 4, \"storage\");\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_TYPE_NAME},
      {"short-circuited unknown enumerator",
       "_Static_assert(!(0 && missing_value), \"unknown\");\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION},
      {"short-circuited invalid sizeof",
       "_Static_assert(1 || sizeof(void), \"invalid type\");\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION},
      {"selected logical-and divide by zero",
       "_Static_assert(1 && (1 / 0), \"selected divide\");\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION},
      {"selected logical-or overflow",
       "_Static_assert(0 || (50000 * 50000), \"selected overflow\");\n",
       CTOOL_ERR_OVERFLOW, CTOOL_C_PARSE_DIAG_OVERFLOW},
      {"conditional expression pending",
       "_Static_assert(1 ? 1 : 0, \"conditional\");\n",
       CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_UNSUPPORTED},
      {"floating assertion condition",
       "_Static_assert(1.0, \"floating\");\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION},
      {"pointer assertion condition",
       "_Static_assert((void *)0, \"pointer\");\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION},
      {"missing static assertion semicolon",
       "_Static_assert(1, \"semicolon\")\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_STATIC_ASSERT},
      {"record false static assertion",
       "struct failed_record { "
       "_Static_assert(sizeof(int) == 8, \"record failure\"); int value; };\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_STATIC_ASSERT}};
  static const char source[] =
      "typedef struct assertion_record {\n"
      "  unsigned char tag;\n"
      "  unsigned int value;\n"
      "} assertion_record_t;\n"
      "typedef int assertion_array_t[4];\n"
      "typedef const assertion_array_t qualified_assertion_array_t;\n"
      "typedef int aligned_assertion_int_t __attribute__((aligned(16)));\n"
      "typedef unsigned char comparison_array_t[2 < 3];\n"
      "enum { assertion_comparison_kind = (0xffffffffU == -1) - 2 };\n"
      "struct assertion_bit_field { unsigned int value : 3 >= 2; };\n"
      "_Static_assert(sizeof(assertion_record_t) == 8, \"record size\");\n"
      "_Static_assert(sizeof(assertion_array_t) != 15, \"array size\");\n"
      "_Static_assert(sizeof(qualified_assertion_array_t) == 16, "
      "\"qualified array\");\n"
      "_Static_assert(sizeof(comparison_array_t) == 1, "
      "\"comparison array\");\n"
      "_Static_assert(sizeof(int[3]) == 12, \"abstract array\");\n"
      "_Static_assert(sizeof(int (*)[3]) == 4, \"array pointer\");\n"
      "_Static_assert(sizeof(int (*)(void)) == 4, \"function pointer\");\n"
      "_Static_assert(sizeof(const int) == 4, \"qualified type\");\n"
      "_Static_assert(sizeof(aligned_assertion_int_t) >= 4, "
      "\"aligned \" \"size\");\n"
      "_Static_assert(sizeof(unsigned long long) > sizeof(unsigned int), "
      "\"rank sizes\");\n"
      "_Static_assert(sizeof(unsigned int) <= sizeof(unsigned long), "
      "\"relational equality\");\n"
      "_Static_assert(sizeof(int *) == 4u, \"pointer size\");\n"
      "_Static_assert(sizeof(int) + 0xffffffffU == 3U, "
      "\"size_t width\");\n"
      "_Static_assert((1 < 1) == 0, \"strict less boundary\");\n"
      "_Static_assert((1 > 1) == 0, \"strict greater boundary\");\n"
      "_Static_assert(1 < 2 == 1, \"comparison precedence\");\n"
      "_Static_assert(-1 < 1u == 0, \"usual conversions\");\n"
      "_Static_assert(-1 == 0xffffffffU, \"equality conversion\");\n"
      "_Static_assert(-1LL < 1U, \"wider signed conversion\");\n"
      "_Static_assert(assertion_comparison_kind < 0, \"signed result\");\n"
      "_Static_assert(((0xffffffffU == -1) - 2) < 0, "
      "\"comparison result is int\");\n"
      "_Static_assert(sizeof(int) - 5 > 0, \"sizeof is unsigned\");\n"
      "_Static_assert(1 | 2 == 2, \"operator precedence\");\n"
      "_Static_assert(!0, \"logical not\");\n"
      "_Static_assert((1 == 1) && (2 == 2), \"logical and\");\n"
      "_Static_assert(0 || 9, \"logical or\");\n"
      "_Static_assert(!(0 && (1 / 0)), \"logical and short circuit\");\n"
      "_Static_assert(!(0 && (1 % 0)), "
      "\"logical and suppresses remainder by zero\");\n"
      "_Static_assert(!(0 && (2147483647 + 1)), "
      "\"logical and suppresses overflow\");\n"
      "_Static_assert(!(0 && (-2147483647 - 2)), "
      "\"logical and suppresses subtraction overflow\");\n"
      "_Static_assert(!(0 && (50000 * 50000)), "
      "\"logical and suppresses multiply overflow\");\n"
      "_Static_assert(!(0 && ((-2147483647 - 1) / -1)), "
      "\"logical and suppresses divide overflow\");\n"
      "_Static_assert(!(0 && ((-2147483647 - 1) % -1)), "
      "\"logical and suppresses remainder overflow\");\n"
      "_Static_assert(!(0 && (-(-2147483647 - 1))), "
      "\"logical and suppresses unary overflow\");\n"
      "_Static_assert(!(0 && (1 << 32)), "
      "\"logical and suppresses invalid shift\");\n"
      "_Static_assert(!(0 && (-1 << 1)), "
      "\"logical and suppresses invalid left operand\");\n"
      "_Static_assert(!(0 && (1 << 31)), "
      "\"logical and suppresses shift overflow\");\n"
      "_Static_assert(1 || (1 / 0), \"logical or short circuit\");\n"
      "_Static_assert(!(1 | 0 && 0), \"bitwise logical precedence\");\n"
      "_Static_assert(-7, \"nonzero is true\");\n"
      "_Static_assert(1, \"\");\n"
      "struct pending_assertion_record;\n"
      "_Static_assert(sizeof(struct pending_assertion_record *) == 4, "
      "L\"pointer to incomplete\");\n"
      "struct pending_assertion_record { int value; };\n"
      "struct assertion_container {\n"
      "  _Static_assert(sizeof(assertion_record_t) <= 8, "
      "\"member \" \"scope\");\n"
      "  unsigned char value;\n"
      "};\n";
  static const char expression_source[] =
      "typedef struct assertion_designator {\n"
      "  int lead;\n"
      "  int values[4] __attribute__((aligned(16)));\n"
      "  struct { int promoted; };\n"
      "} assertion_designator_t;\n"
      "_Static_assert(sizeof(((assertion_designator_t *)0)->values) == 16, \"member array size\");\n"
      "_Static_assert(sizeof(1 / 0) == 4, \"unevaluated arithmetic\");\n"
      "_Static_assert(_Alignof(assertion_designator_t) == 16, \"standard alignment\");\n"
      "_Static_assert(__alignof__(((assertion_designator_t *)0)->values) == 16, \"GNU member alignment\");\n"
      "_Static_assert(__builtin_offsetof(assertion_designator_t, values) == 16, \"direct offset\");\n"
      "_Static_assert(__builtin_offsetof(assertion_designator_t, promoted) == 32, \"promoted offset\");\n";
  frontend_fixture_t fixture;
  ctool_c_pp_include_root_t include_roots[ARRAY_COUNT(active_rows)];
  ctool_c_pp_macro_action_t macro_actions[ARRAY_COUNT(active_rows)];
  ctool_path_t forced_includes[ARRAY_COUNT(active_rows)];
  ctool_c_translation_unit_t unit;
  char *active_process_assertions = NULL;
  char *active_syscall_assertions = NULL;
  ctool_u32 failure_index;
  int failed = 1;

  if (begin_frontend_fixture(&fixture, "static-asserts", host_root,
                             8u * 1024u * 1024u) != 0) {
    return 1;
  }
  if (parse_valid_fixture(&fixture, "/static-asserts.c", source, &unit) != 0) {
    goto cleanup;
  }
  {
    const ctool_c_binding_t *record_binding =
        find_binding(&unit, "assertion_record_t");
    const ctool_c_binding_t *array_binding =
        find_binding(&unit, "assertion_array_t");
    const ctool_c_binding_t *aligned_binding =
        find_binding(&unit, "aligned_assertion_int_t");
    const ctool_c_binding_t *qualified_binding =
        find_binding(&unit, "qualified_assertion_array_t");
    const ctool_c_binding_t *comparison_binding =
        find_binding(&unit, "assertion_comparison_kind");
    const ctool_c_binding_t *comparison_array_binding =
        find_binding(&unit, "comparison_array_t");
    const ctool_c_tag_t *container_tag =
        find_tag(&unit, "assertion_container");
    const ctool_c_tag_t *bit_field_tag =
        find_tag(&unit, "assertion_bit_field");
    const ctool_c_type_node_t *container =
        container_tag == NULL ? NULL : type_node(&unit, container_tag->type);
    const ctool_c_type_layout_t *record_layout =
        record_binding == NULL ? NULL : type_layout(&unit, record_binding->type);
    const ctool_c_type_layout_t *array_layout =
        array_binding == NULL ? NULL : type_layout(&unit, array_binding->type);
    const ctool_c_type_layout_t *aligned_layout =
        aligned_binding == NULL
            ? NULL
            : type_layout(&unit, aligned_binding->type);
    const ctool_c_type_layout_t *qualified_layout =
        qualified_binding == NULL
            ? NULL
            : type_layout(&unit, qualified_binding->type);
    const ctool_c_type_layout_t *container_layout =
        container_tag == NULL ? NULL : type_layout(&unit, container_tag->type);
    const ctool_c_type_layout_t *comparison_array_layout =
        comparison_array_binding == NULL
            ? NULL
            : type_layout(&unit, comparison_array_binding->type);
    const ctool_c_type_node_t *bit_field =
        bit_field_tag == NULL ? NULL : type_node(&unit, bit_field_tag->type);
    const ctool_c_type_layout_t *bit_field_layout =
        bit_field_tag == NULL ? NULL : type_layout(&unit, bit_field_tag->type);
    const ctool_c_record_member_t *bit_field_member =
        bit_field == NULL || bit_field->member_count != 1u
            ? NULL
            : &unit.graph.members[bit_field->first_member];
    if (unit.binding_count != 6u || unit.tag_count != 4u ||
        unit.graph.member_count != 5u || record_binding == NULL ||
        record_binding->kind != CTOOL_C_BINDING_TYPEDEF ||
        array_binding == NULL ||
        array_binding->kind != CTOOL_C_BINDING_TYPEDEF ||
        aligned_binding == NULL ||
        aligned_binding->kind != CTOOL_C_BINDING_TYPEDEF ||
        qualified_binding == NULL ||
        qualified_binding->kind != CTOOL_C_BINDING_TYPEDEF ||
        comparison_binding == NULL ||
        comparison_binding->kind != CTOOL_C_BINDING_ENUMERATOR ||
        comparison_binding->integer_unsigned != CTOOL_FALSE ||
        comparison_binding->integer_bits != 0xffffffffffffffffull ||
        comparison_array_binding == NULL ||
        comparison_array_binding->kind != CTOOL_C_BINDING_TYPEDEF ||
        comparison_array_layout == NULL ||
        comparison_array_layout->size != 1u ||
        comparison_array_layout->alignment != 1u || bit_field == NULL ||
        bit_field->kind != CTOOL_C_TYPE_RECORD || bit_field_member == NULL ||
        bit_field_member->is_bit_field != CTOOL_TRUE ||
        bit_field_member->bit_width != 1u || bit_field_layout == NULL ||
        bit_field_layout->size != 4u || bit_field_layout->alignment != 4u ||
        record_layout == NULL || record_layout->size != 8u ||
        record_layout->alignment != 4u || array_layout == NULL ||
        array_layout->size != 16u || array_layout->alignment != 4u ||
        aligned_layout == NULL || aligned_layout->size != 4u ||
        aligned_layout->alignment != 16u || qualified_layout == NULL ||
        qualified_layout->size != 16u || qualified_layout->alignment != 4u ||
        container == NULL ||
        container->kind != CTOOL_C_TYPE_RECORD ||
        container->member_count != 1u || container_layout == NULL ||
        container_layout->size != 1u || container_layout->alignment != 1u) {
      (void)fprintf(stderr,
                    "static-asserts: semantic graph or layout differs\n");
      goto cleanup;
    }
  }
  if (parse_valid_fixture(&fixture, "/static-assert-expressions.c",
                          expression_source, &unit) != 0) {
    goto cleanup;
  }
  {
    const ctool_c_binding_t *binding =
        find_binding(&unit, "assertion_designator_t");
    const ctool_c_type_layout_t *layout =
        binding == NULL ? NULL : type_layout(&unit, binding->type);
    if (unit.binding_count != 1u || unit.tag_count != 1u ||
        unit.graph.member_count != 4u || binding == NULL || layout == NULL ||
        layout->size != 48u || layout->alignment != 16u ||
        unit.function_definition_count != 0u || unit.statement_count != 0u ||
        unit.statement_child_count != 0u || unit.expression_count != 0u ||
        unit.expression_child_count != 0u || unit.block_binding_count != 0u) {
      (void)fprintf(
          stderr,
          "static-asserts: typed unevaluated-expression fixture differs\n");
      goto cleanup;
    }
  }
  if (build_kernel_profile(&fixture.pp_request, include_roots, macro_actions,
                           forced_includes) != 0 ||
      parse_loaded_fixture(&fixture, "/kernel/smp/percpu.h", "static", 0u,
                           &unit) != 0) {
    goto cleanup;
  }
  {
    const ctool_c_binding_t *binding = find_binding(&unit, "per_cpu_t");
    const ctool_c_type_layout_t *percpu_layout =
        binding == NULL ? NULL : type_layout(&unit, binding->type);
    if (binding == NULL || binding->kind != CTOOL_C_BINDING_TYPEDEF ||
        percpu_layout == NULL || percpu_layout->size != 128u ||
        percpu_layout->alignment != 64u ||
        find_binding(&unit, "cpus") == NULL ||
        find_binding(&unit, "smp_cpu_count_var") == NULL) {
      (void)fprintf(stderr,
                    "static-asserts: unchanged per-CPU assertion differs\n");
      goto cleanup;
    }
  }
  if (parse_loaded_fixture(&fixture, "/kernel/lang/exec.c",
                           "elf_image_regions", 0u, &unit) != 0) {
    goto cleanup;
  }
  {
    const ctool_c_binding_t *region =
        find_binding(&unit, "elf_image_region_t");
    const ctool_c_binding_t *plan = find_binding(&unit, "elf_load_plan_t");
    const ctool_c_type_layout_t *region_layout =
        region == NULL ? NULL : type_layout(&unit, region->type);
    const ctool_c_type_layout_t *plan_layout =
        plan == NULL ? NULL : type_layout(&unit, plan->type);
    if (region == NULL || region->kind != CTOOL_C_BINDING_TYPEDEF ||
        plan == NULL || plan->kind != CTOOL_C_BINDING_TYPEDEF ||
        region_layout == NULL || region_layout->size != 16u ||
        region_layout->alignment != 4u || plan_layout == NULL ||
        plan_layout->size != 24u || plan_layout->alignment != 4u) {
      (void)fprintf(stderr,
                    "static-asserts: unchanged exec assertion prefix differs\n");
      goto cleanup;
    }
  }
  active_process_assertions = build_active_assertion_fixture(
      &fixture, "/kernel/core/process.c",
      "#include \"process.h\"\n#line 39 \"/kernel/core/process.c\"\n",
      "_Static_assert(__alignof__(((process_t *)0)->fp_state)",
      "(PCB_FP_STATE_OFFSET=80)\");", 6u);
  active_syscall_assertions = build_active_assertion_fixture(
      &fixture, "/kernel/core/syscall.c",
      "#include \"syscall.h\"\n#line 174 \"/kernel/core/syscall.c\"\n",
      "#define SC_OFF(field)", "#undef SC_OFF", 12u);
  if (active_process_assertions == NULL || active_syscall_assertions == NULL) {
    goto cleanup;
  }
  if (parse_valid_fixture(&fixture, "/active-process-assertions.c",
                          active_process_assertions, &unit) != 0) {
    goto cleanup;
  }
  {
    const ctool_c_binding_t *binding = find_binding(&unit, "process_t");
    const ctool_c_type_node_t *process =
        binding == NULL ? NULL : type_node(&unit, binding->type);
    const ctool_c_type_layout_t *layout =
        binding == NULL ? NULL : type_layout(&unit, binding->type);
    ctool_u32 member_index = unit.graph.member_count;
    const ctool_c_record_member_t *member =
        process == NULL
            ? NULL
            : find_record_member(&unit, process, "fp_state", &member_index);
    if (binding == NULL || process == NULL || layout == NULL ||
        layout->size != 656u || layout->alignment != 16u || member == NULL ||
        member_index >= unit.layout.member_count ||
        unit.layout.members[member_index].byte_offset != 80u ||
        unit.layout.members[member_index].size != 512u ||
        unit.layout.members[member_index].alignment != 16u ||
        unit.function_definition_count != 0u || unit.statement_count != 0u ||
        unit.statement_child_count != 0u || unit.expression_count != 0u ||
        unit.expression_child_count != 0u || unit.block_binding_count != 0u) {
      (void)fprintf(stderr,
                    "static-asserts: active process assertions differ\n");
      goto cleanup;
    }
  }
  if (parse_valid_fixture(&fixture, "/active-syscall-assertions.c",
                          active_syscall_assertions, &unit) != 0) {
    goto cleanup;
  }
  if (find_binding(&unit, "cupid_syscall_table_t") == NULL ||
      unit.function_definition_count != 0u || unit.statement_count != 0u ||
      unit.statement_child_count != 0u || unit.expression_count != 0u ||
      unit.expression_child_count != 0u || unit.block_binding_count != 0u) {
    (void)fprintf(stderr,
                  "static-asserts: active syscall assertions differ\n");
    goto cleanup;
  }
  for (failure_index = 0u; failure_index < ARRAY_COUNT(failure_cases);
       failure_index++) {
    if (expect_frontend_failure(&fixture, &failure_cases[failure_index],
                                "/static-assert-failure.c") != 0) {
      goto cleanup;
    }
  }
  {
    static const frontend_failure_case_t message_case = {
        "concatenated false static assertion message",
        "_Static_assert(0, \"alpha \" \"beta\");\n", CTOOL_ERR_INPUT,
        CTOOL_C_PARSE_DIAG_STATIC_ASSERT};
    ctool_u32 diagnostic_index =
        ctool_job_diagnostic_count(fixture.job);
    const ctool_diagnostic_t *diagnostic;
    if (expect_frontend_failure(&fixture, &message_case,
                                "/static-assert-message.c") != 0) {
      goto cleanup;
    }
    diagnostic = ctool_job_diagnostic(fixture.job, diagnostic_index);
    if (diagnostic == NULL ||
        !string_equal(diagnostic->message,
                      "static assertion failed: alpha beta")) {
      (void)fprintf(stderr,
                    "static-asserts: concatenated diagnostic differs\n");
      goto cleanup;
    }
  }
  {
    frontend_failure_case_t depth_case;
    char *depth_source = build_depth_source(
        "static-assert", CTOOL_C_PARSE_NESTING_LIMIT - 1u);
    if (depth_source == NULL) {
      (void)fprintf(stderr,
                    "static-asserts: nesting source construction failed\n");
      goto cleanup;
    }
    depth_case.name = "static assertion at occupied nesting limit";
    depth_case.source = depth_source;
    depth_case.status = CTOOL_ERR_LIMIT;
    depth_case.diagnostic_code = CTOOL_C_PARSE_DIAG_LIMIT;
    if (expect_frontend_failure(&fixture, &depth_case,
                                "/static-assert-depth.c") != 0) {
      free(depth_source);
      goto cleanup;
    }
    free(depth_source);
  }
  if (validate_static_assert_limits(&fixture, host_root) != 0) {
    goto cleanup;
  }
  failed = 0;

cleanup:
  free(active_syscall_assertions);
  free(active_process_assertions);
  if (finish_frontend_fixture(&fixture) != 0) {
    failed = 1;
  }
  if (failed == 0) {
    (void)printf("static-asserts: ok\n");
  }
  return failed;
}

static int validate_debug_body_unit(const ctool_c_translation_unit_t *unit) {
  const ctool_u32 definition_bindings[] = {69u, 70u};
  const ctool_u32 definition_types[] = {80u, 88u};
  const ctool_u32 definition_bodies[] = {3u, 7u};
  const ctool_u32 definition_lines[] = {8u, 15u};
  ctool_u32 identifier_count = 0u;
  ctool_u32 parameter_count = 0u;
  ctool_u32 string_count = 0u;
  ctool_u32 call_count = 0u;
  ctool_u32 conversion_count = 0u;
  ctool_u32 index;
  if (unit->graph.type_count != 94u || unit->graph.member_count != 37u ||
      unit->parameter_count != 19u || unit->binding_count != 71u ||
      unit->tag_count != 1u || unit->function_definition_count != 2u ||
      unit->statement_count != 8u || unit->statement_child_count != 6u ||
      unit->expression_count != 32u || unit->expression_child_count != 26u ||
      find_binding_index(unit, "print") != 21u ||
      find_binding_index(unit, "print_int") != 60u ||
      find_binding_index(unit, "print_hex") != 62u ||
      find_binding_index(unit, "debug_print_int") != 69u ||
      find_binding_index(unit, "debug_print_hex") != 70u) {
    (void)fprintf(stderr,
                  "function-bodies: unchanged debug inventory differs\n");
    return 1;
  }
  for (index = 0u; index < 2u; index++) {
    const ctool_c_function_definition_t *definition =
        &unit->function_definitions[index];
    const ctool_c_statement_t *body =
        definition->body < unit->statement_count
            ? &unit->statements[definition->body]
            : NULL;
    const ctool_c_type_node_t *function =
        type_node(unit, definition->declared_type);
    if (definition->binding != definition_bindings[index] ||
        definition->declared_type != definition_types[index] ||
        definition->body != definition_bodies[index] ||
        definition->storage != CTOOL_C_STORAGE_STATIC ||
        definition->function_declaration_flags !=
            CTOOL_C_FUNCTION_DECL_INLINE ||
        definition->location.line != definition_lines[index] ||
        definition->location.column != 20u ||
        !dual_location_matches(&definition->location,
                               &definition->physical_location,
                               "/kernel/core/debug.h",
                               definition_lines[index]) ||
        body == NULL || body->kind != CTOOL_C_STATEMENT_COMPOUND ||
        body->location.line != definition_lines[index] ||
        body->location.column != 71u || body->child_count != 3u ||
        function == NULL || function->kind != CTOOL_C_TYPE_FUNCTION ||
        function->referenced_type != 20u || function->parameter_count != 2u ||
        function->first_parameter + 1u >= unit->parameter_count ||
        !string_equal(unit->parameters[function->first_parameter].name,
                      "label") ||
        !string_equal(unit->parameters[function->first_parameter + 1u].name,
                      "value") ||
        unit->parameters[function->first_parameter].storage !=
            CTOOL_C_STORAGE_NONE ||
        unit->parameters[function->first_parameter + 1u].storage !=
            CTOOL_C_STORAGE_NONE) {
      (void)fprintf(stderr,
                    "function-bodies: unchanged debug definition %u differs\n",
                    index);
      return 1;
    }
  }
  for (index = 0u; index < unit->expression_count; index++) {
    const ctool_c_expression_t *expression = &unit->expressions[index];
    if (!dual_location_matches(&expression->location,
                               &expression->physical_location,
                               "/kernel/core/debug.h",
                               expression->location.line)) {
      (void)fprintf(stderr,
                    "function-bodies: debug expression location differs\n");
      return 1;
    }
    if (expression->kind == CTOOL_C_EXPRESSION_IDENTIFIER) {
      identifier_count++;
    } else if (expression->kind == CTOOL_C_EXPRESSION_PARAMETER) {
      parameter_count++;
    } else if (expression->kind == CTOOL_C_EXPRESSION_STRING) {
      const ctool_c_type_node_t *literal = type_node(unit, expression->type);
      const ctool_c_type_layout_t *layout =
          type_layout(unit, expression->type);
      string_count++;
      if (expression->string_bytes.size != 2u ||
          expression->string_bytes.data == NULL ||
          expression->string_bytes.data[0] != (ctool_u8)'\n' ||
          expression->string_bytes.data[1] != 0u || literal == NULL ||
          literal->kind != CTOOL_C_TYPE_ARRAY ||
          literal->element_count != 2u || layout == NULL ||
          layout->size != 2u || layout->alignment != 1u) {
        (void)fprintf(stderr,
                      "function-bodies: debug string literal differs\n");
        return 1;
      }
    } else if (expression->kind == CTOOL_C_EXPRESSION_CALL) {
      const ctool_c_type_node_t *result = type_node(unit, expression->type);
      call_count++;
      if (expression->child_count != 2u || result == NULL ||
          result->kind != CTOOL_C_TYPE_VOID) {
        (void)fprintf(stderr, "function-bodies: debug call differs\n");
        return 1;
      }
    } else if (expression->kind ==
               CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION) {
      const ctool_c_expression_t *child;
      conversion_count++;
      if (expression->child_count != 1u ||
          expression->first_child >= unit->expression_child_count ||
          unit->expression_children[expression->first_child] >= index) {
        (void)fprintf(stderr,
                      "function-bodies: debug conversion differs\n");
        return 1;
      }
      child = &unit->expressions[
          unit->expression_children[expression->first_child]];
      if (expression->conversion == CTOOL_C_CONVERSION_LVALUE_TO_VALUE &&
          expression->type != child->type) {
        (void)fprintf(stderr,
                      "function-bodies: debug lvalue conversion differs\n");
        return 1;
      }
    } else {
      (void)fprintf(stderr,
                    "function-bodies: debug expression kind differs\n");
      return 1;
    }
  }
  if (identifier_count != 6u || parameter_count != 4u || string_count != 2u ||
      call_count != 6u || conversion_count != 14u) {
    (void)fprintf(stderr,
                  "function-bodies: debug expression inventory differs\n");
    return 1;
  }
  return 0;
}

static int validate_qualified_lvalue_unit(
    const ctool_c_translation_unit_t *unit) {
  ctool_u32 sink_binding = find_binding_index(unit, "sink");
  const ctool_c_function_definition_t *definition;
  const ctool_c_statement_t *body;
  const ctool_c_statement_t *statement;
  const ctool_c_expression_t *call;
  const ctool_c_expression_t *conversion;
  const ctool_c_expression_t *source;
  const ctool_c_type_node_t *source_type;
  const ctool_c_type_node_t *sink_type;
  ctool_u32 statement_index;
  ctool_u32 conversion_index;
  ctool_u32 source_index;
  ctool_u32 parameter_type;
  if (sink_binding == CTOOL_C_AST_NONE ||
      unit->function_definition_count != 1u || unit->statement_count != 2u ||
      unit->statement_child_count != 1u || unit->expression_count != 5u ||
      unit->expression_child_count != 4u) {
    (void)fprintf(stderr,
                  "function-bodies: qualified lvalue inventory differs\n");
    return 1;
  }
  definition = &unit->function_definitions[0];
  body = definition->body < unit->statement_count
             ? &unit->statements[definition->body]
             : NULL;
  if (body == NULL || body->kind != CTOOL_C_STATEMENT_COMPOUND ||
      body->child_count != 1u ||
      body->first_child >= unit->statement_child_count) {
    (void)fprintf(stderr,
                  "function-bodies: qualified lvalue body differs\n");
    return 1;
  }
  statement_index = unit->statement_children[body->first_child];
  statement = statement_index < unit->statement_count
                  ? &unit->statements[statement_index]
                  : NULL;
  call = statement != NULL &&
                 statement->kind == CTOOL_C_STATEMENT_EXPRESSION &&
                 statement->expression < unit->expression_count
             ? &unit->expressions[statement->expression]
             : NULL;
  if (call == NULL || call->kind != CTOOL_C_EXPRESSION_CALL ||
      call->child_count != 2u ||
      call->first_child > unit->expression_child_count ||
      call->child_count >
          unit->expression_child_count - call->first_child) {
    (void)fprintf(stderr,
                  "function-bodies: qualified lvalue call differs\n");
    return 1;
  }
  conversion_index = unit->expression_children[call->first_child + 1u];
  conversion = conversion_index < unit->expression_count
                   ? &unit->expressions[conversion_index]
                   : NULL;
  source_index = conversion != NULL && conversion->child_count == 1u &&
                         conversion->first_child <
                             unit->expression_child_count
                     ? unit->expression_children[conversion->first_child]
                     : CTOOL_C_AST_NONE;
  source = source_index < unit->expression_count
               ? &unit->expressions[source_index]
               : NULL;
  source_type = source != NULL ? type_node(unit, source->type) : NULL;
  sink_type = type_node(unit, unit->bindings[sink_binding].type);
  parameter_type = sink_type != NULL &&
                           sink_type->kind == CTOOL_C_TYPE_FUNCTION &&
                           sink_type->parameter_count == 1u &&
                           sink_type->first_parameter <
                               unit->graph.parameter_type_count
                       ? unit->graph
                             .parameter_types[sink_type->first_parameter]
                       : CTOOL_C_AST_NONE;
  if (conversion == NULL ||
      conversion->kind != CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION ||
      conversion->conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      source == NULL || source->kind != CTOOL_C_EXPRESSION_IDENTIFIER ||
      source_type == NULL || source_type->kind != CTOOL_C_TYPE_QUALIFIED ||
      source_type->qualifiers !=
          (CTOOL_C_QUAL_CONST | CTOOL_C_QUAL_VOLATILE |
           CTOOL_C_QUAL_ATOMIC) ||
      conversion->type != parameter_type || source->type == conversion->type) {
    (void)fprintf(stderr,
                  "function-bodies: qualified lvalue conversion differs\n");
    return 1;
  }
  return 0;
}

static int run_function_bodies(const char *host_root) {
  static const frontend_exact_failure_case_t failure_cases[] = {
      {{"too few call arguments",
        "void one(int value);\nvoid bad(void) { one(); }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       2u, 22u, "function call has too few arguments"},
      {{"too many call arguments",
        "void one(int value);\n"
        "void bad(int value) { one(value, value); }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       2u, 34u, "function call has too many arguments"},
      {{"variadic call argument boundary",
        "void variadic(int first, ...);\n"
        "void bad(int value) { variadic(value, value); }\n",
        CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_EXPRESSION},
       2u, 39u, "variadic call arguments are outside this body slice"},
      {{"incompatible call argument",
        "void pointer(const char *value);\n"
        "void bad(unsigned int value) { pointer(value); }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       2u, 40u,
       "function call argument is not convertible to parameter type"},
      {{"undeclared body identifier", "void bad(void) { missing(); }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       1u, 18u, "expression identifier is not declared"},
      {{"duplicate function definition",
        "void duplicate(void) { }\nvoid duplicate(void) { }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_FUNCTION_DEFINITION},
       2u, 6u, "function already has a definition"},
      {{"definition in a declarator list",
        "void prior(void), bad(void) { }\n", CTOOL_ERR_INPUT,
        CTOOL_C_PARSE_DIAG_FUNCTION_DEFINITION},
       1u, 29u,
       "function definition must be the only declaration declarator"},
      {{"oversized narrow string escape",
        "void sink(const char *value);\n"
        "void bad(void) { sink(\"\\777\"); }\n",
        CTOOL_ERR_OVERFLOW, CTOOL_C_PARSE_DIAG_EXPRESSION},
       2u, 23u,
       "narrow string octal escape exceeds one target byte"}};
  static const char source[] =
      "typedef unsigned int u32;\n"
      "void print(const char *text, ...);\n"
      "void print_u32(u32 value);\n"
      "const char *label;\n"
      "static inline void helper(const char *old_label, u32 old_value);\n"
      "static void helper(const char *label, register u32 value) {\n"
      "  print(label);\n"
      "  print_u32(value);\n"
      "  { print(\"\" \"\\n\"); }\n"
      "}\n";
  static const char qualified_source[] =
      "typedef unsigned int u32;\n"
      "const volatile _Atomic u32 sample;\n"
      "void sink(u32 value);\n"
      "void convert(void) { sink(sample); }\n";
  frontend_fixture_t fixture;
  ctool_c_pp_include_root_t include_roots[ARRAY_COUNT(active_rows)];
  ctool_c_pp_macro_action_t macro_actions[ARRAY_COUNT(active_rows)];
  ctool_path_t forced_includes[ARRAY_COUNT(active_rows)];
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t qualified_unit;
  ctool_c_translation_unit_t debug_unit;
  ctool_u32 helper_binding;
  ctool_u32 print_binding;
  ctool_u32 print_u32_binding;
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function;
  const ctool_c_statement_t *body;
  const ctool_c_expression_t *calls[3];
  const ctool_c_expression_t *callees[3];
  const ctool_c_expression_t *arguments[3];
  ctool_u32 index;
  ctool_u32 failure_index;
  int failed = 1;

  if (begin_frontend_fixture(&fixture, "function-bodies", host_root,
                             8u * 1024u * 1024u) != 0) {
    return 1;
  }
  if (parse_valid_fixture(&fixture, "/function-bodies.c", source, &unit) !=
      0) {
    goto cleanup;
  }
  helper_binding = find_binding_index(&unit, "helper");
  print_binding = find_binding_index(&unit, "print");
  print_u32_binding = find_binding_index(&unit, "print_u32");
  if (helper_binding == CTOOL_C_AST_NONE ||
      print_binding == CTOOL_C_AST_NONE ||
      print_u32_binding == CTOOL_C_AST_NONE ||
      unit.function_definition_count != 1u || unit.statement_count != 5u ||
      unit.statement_child_count != 4u || unit.expression_count != 16u ||
      unit.expression_child_count != 13u) {
    (void)fprintf(stderr, "function-bodies: AST inventory differs\n");
    goto cleanup;
  }
  definition = &unit.function_definitions[0];
  function = type_node(&unit, definition->declared_type);
  body = definition->body < unit.statement_count
             ? &unit.statements[definition->body]
             : NULL;
  if (definition->binding != helper_binding ||
      definition->storage != CTOOL_C_STORAGE_STATIC ||
      definition->function_declaration_flags != 0u ||
      unit.bindings[helper_binding].function_declaration_flags !=
          CTOOL_C_FUNCTION_DECL_INLINE ||
      unit.bindings[helper_binding].location.line != 5u ||
      definition->location.line != 6u ||
      function == NULL || function->kind != CTOOL_C_TYPE_FUNCTION ||
      function->has_prototype != CTOOL_TRUE ||
      function->parameter_count != 2u ||
      function->first_parameter > unit.parameter_count ||
      function->parameter_count >
          unit.parameter_count - function->first_parameter ||
      body == NULL || body->kind != CTOOL_C_STATEMENT_COMPOUND ||
      body->first_child > unit.statement_child_count ||
      body->child_count != 3u ||
      body->child_count > unit.statement_child_count - body->first_child ||
      !string_equal(unit.parameters[function->first_parameter].name,
                    "label") ||
      !string_equal(unit.parameters[function->first_parameter + 1u].name,
                    "value") ||
      unit.parameters[function->first_parameter].storage !=
          CTOOL_C_STORAGE_NONE ||
      unit.parameters[function->first_parameter + 1u].storage !=
          CTOOL_C_STORAGE_REGISTER) {
    (void)fprintf(stderr,
                  "function-bodies: definition metadata differs\n");
    goto cleanup;
  }
  for (index = 0u; index < 3u; index++) {
    ctool_u32 statement_index =
        unit.statement_children[body->first_child + index];
    const ctool_c_statement_t *statement =
        statement_index < unit.statement_count
            ? &unit.statements[statement_index]
            : NULL;
    if (index == 2u) {
      if (statement == NULL ||
          statement->kind != CTOOL_C_STATEMENT_COMPOUND ||
          statement->child_count != 1u ||
          statement->first_child >= unit.statement_child_count) {
        (void)fprintf(stderr,
                      "function-bodies: nested compound differs\n");
        goto cleanup;
      }
      statement_index = unit.statement_children[statement->first_child];
      statement = statement_index < unit.statement_count
                      ? &unit.statements[statement_index]
                      : NULL;
    }
    if (statement == NULL ||
        statement->kind != CTOOL_C_STATEMENT_EXPRESSION ||
        statement->expression >= unit.expression_count) {
      (void)fprintf(stderr,
                    "function-bodies: expression statement %u differs\n",
                    index);
      goto cleanup;
    }
    calls[index] = &unit.expressions[statement->expression];
    if (calls[index]->kind != CTOOL_C_EXPRESSION_CALL ||
        calls[index]->first_child > unit.expression_child_count ||
        calls[index]->child_count != 2u ||
        calls[index]->child_count >
            unit.expression_child_count - calls[index]->first_child) {
      (void)fprintf(stderr, "function-bodies: call %u differs\n", index);
      goto cleanup;
    }
    {
      const ctool_c_expression_t *callee_conversion = &unit.expressions[
          unit.expression_children[calls[index]->first_child]];
      const ctool_c_expression_t *argument_conversion = &unit.expressions[
          unit.expression_children[calls[index]->first_child + 1u]];
      if (callee_conversion->kind !=
              CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION ||
          callee_conversion->conversion !=
              CTOOL_C_CONVERSION_FUNCTION_TO_POINTER ||
          callee_conversion->child_count != 1u ||
          callee_conversion->first_child >= unit.expression_child_count ||
          argument_conversion->kind !=
              CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION) {
        (void)fprintf(stderr,
                      "function-bodies: call conversion %u differs\n",
                      index);
        goto cleanup;
      }
      callees[index] = &unit.expressions[unit.expression_children[
          callee_conversion->first_child]];
      arguments[index] = argument_conversion;
      {
        const ctool_c_type_node_t *converted_callee =
            type_node(&unit, callee_conversion->type);
        if (converted_callee == NULL ||
            converted_callee->kind != CTOOL_C_TYPE_POINTER ||
            converted_callee->referenced_type != callees[index]->type) {
          (void)fprintf(stderr,
                        "function-bodies: callee result type %u differs\n",
                        index);
          goto cleanup;
        }
      }
    }
    if (callees[index]->kind != CTOOL_C_EXPRESSION_IDENTIFIER ||
        (index == 1u ? callees[index]->reference != print_u32_binding
                     : callees[index]->reference != print_binding) ||
        type_node(&unit, calls[index]->type) == NULL ||
        type_node(&unit, calls[index]->type)->kind != CTOOL_C_TYPE_VOID) {
      (void)fprintf(stderr,
                    "function-bodies: typed callee %u differs\n", index);
      goto cleanup;
    }
  }
  if (arguments[0]->conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      arguments[0]->child_count != 1u ||
      arguments[1]->conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      arguments[1]->child_count != 1u ||
      arguments[2]->conversion != CTOOL_C_CONVERSION_QUALIFICATION ||
      arguments[2]->child_count != 1u) {
    (void)fprintf(stderr,
                  "function-bodies: argument conversions differ\n");
    goto cleanup;
  }
  {
    const ctool_c_expression_t *label = &unit.expressions[
        unit.expression_children[arguments[0]->first_child]];
    const ctool_c_expression_t *value = &unit.expressions[
        unit.expression_children[arguments[1]->first_child]];
    const ctool_c_expression_t *array_decay = &unit.expressions[
        unit.expression_children[arguments[2]->first_child]];
    const ctool_c_expression_t *literal;
    const ctool_c_type_node_t *print_function =
        type_node(&unit, unit.bindings[print_binding].type);
    if (label->kind != CTOOL_C_EXPRESSION_PARAMETER ||
        label->reference != function->first_parameter ||
        value->kind != CTOOL_C_EXPRESSION_PARAMETER ||
        value->reference != function->first_parameter + 1u ||
        array_decay->kind != CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION ||
        array_decay->conversion != CTOOL_C_CONVERSION_ARRAY_TO_POINTER ||
        array_decay->child_count != 1u ||
        arguments[0]->type != label->type ||
        arguments[1]->type != value->type || print_function == NULL ||
        print_function->kind != CTOOL_C_TYPE_FUNCTION ||
        print_function->parameter_count != 1u ||
        print_function->variadic != CTOOL_TRUE ||
        arguments[2]->type !=
            unit.graph.parameter_types[print_function->first_parameter]) {
      (void)fprintf(stderr,
                    "function-bodies: parameter or decay source differs\n");
      goto cleanup;
    }
    literal = &unit.expressions[
        unit.expression_children[array_decay->first_child]];
    if (literal->kind != CTOOL_C_EXPRESSION_STRING ||
        literal->string_bytes.size != 2u ||
        literal->string_bytes.data == NULL ||
        literal->string_bytes.data[0] != (ctool_u8)'\n' ||
        literal->string_bytes.data[1] != 0u) {
      (void)fprintf(stderr,
                    "function-bodies: string literal bytes differ\n");
      goto cleanup;
    }
    const ctool_c_type_node_t *literal_type =
        type_node(&unit, literal->type);
    const ctool_c_type_layout_t *literal_layout =
        type_layout(&unit, literal->type);
    const ctool_c_type_node_t *decayed_type =
        type_node(&unit, array_decay->type);
    if (literal_type == NULL || literal_type->kind != CTOOL_C_TYPE_ARRAY ||
        literal_type->array_bound_kind != CTOOL_C_ARRAY_FIXED ||
        literal_type->element_count != 2u || literal_layout == NULL ||
        literal_layout->size != 2u || literal_layout->alignment != 1u ||
        decayed_type == NULL || decayed_type->kind != CTOOL_C_TYPE_POINTER ||
        decayed_type->referenced_type != literal_type->referenced_type) {
      (void)fprintf(stderr,
                    "function-bodies: string literal type differs\n");
      goto cleanup;
    }
  }
  if (parse_valid_fixture(&fixture, "/function-body-qualified.c",
                          qualified_source, &qualified_unit) != 0 ||
      validate_qualified_lvalue_unit(&qualified_unit) != 0) {
    goto cleanup;
  }
  {
    frontend_failure_case_t depth_case;
    char *depth_source = build_depth_source(
        "body-call", CTOOL_C_PARSE_NESTING_LIMIT);
    int depth_failed;
    if (depth_source == NULL) {
      (void)fprintf(stderr,
                    "function-bodies: call-depth source construction failed\n");
      goto cleanup;
    }
    depth_case.name = "call expression at occupied nesting limit";
    depth_case.source = depth_source;
    depth_case.status = CTOOL_ERR_LIMIT;
    depth_case.diagnostic_code = CTOOL_C_PARSE_DIAG_LIMIT;
    depth_failed = expect_frontend_failure(
        &fixture, &depth_case, "/function-body-depth.c");
    free(depth_source);
    if (depth_failed != 0) {
      goto cleanup;
    }
  }
  if (build_kernel_profile(&fixture.pp_request, include_roots, macro_actions,
                           forced_includes) != 0 ||
      parse_loaded_fixture(&fixture, "/kernel/core/debug.h", NULL, 629u,
                           &debug_unit) != 0 ||
      validate_debug_body_unit(&debug_unit) != 0) {
    goto cleanup;
  }
  for (failure_index = 0u; failure_index < ARRAY_COUNT(failure_cases);
       failure_index++) {
    const frontend_exact_failure_case_t *test_case =
        &failure_cases[failure_index];
    if (expect_frontend_failure_at_message(
            &fixture, &test_case->failure, "/function-body-failure.c",
            test_case->line, test_case->column, test_case->message) != 0) {
      goto cleanup;
    }
  }
  if (unit.function_definition_count != 1u ||
      validate_debug_body_unit(&debug_unit) != 0) {
    (void)fprintf(stderr,
                  "function-bodies: prior AST did not survive failures\n");
    goto cleanup;
  }
  failed = 0;

cleanup:
  if (finish_frontend_fixture(&fixture) != 0) {
    failed = 1;
  }
  if (failed == 0) {
    (void)printf("function-bodies: ok\n");
  }
  return failed;
}

static const ctool_c_expression_t *
expression_terminal(const ctool_c_translation_unit_t *unit,
                    const ctool_c_expression_t *expression) {
  ctool_u32 traversed = 0u;
  while (expression != NULL &&
         expression->kind == CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION) {
    ctool_u32 child;
    if (expression->child_count != 1u ||
        expression->first_child >= unit->expression_child_count) {
      return NULL;
    }
    child = unit->expression_children[expression->first_child];
    if (child >= unit->expression_count || traversed++ >= unit->expression_count) {
      return NULL;
    }
    expression = &unit->expressions[child];
  }
  return expression;
}

static int validate_block_binding_unit(
    const ctool_c_translation_unit_t *unit) {
  static const char *const names[] = {
      "file_value", "outer", "pointer", "nested",
      "parameter", "outer", "word", "tail", "file_value"};
  static const ctool_c_storage_class_t storage[] = {
      CTOOL_C_STORAGE_NONE, CTOOL_C_STORAGE_NONE, CTOOL_C_STORAGE_NONE,
      CTOOL_C_STORAGE_AUTO, CTOOL_C_STORAGE_REGISTER, CTOOL_C_STORAGE_NONE,
      CTOOL_C_STORAGE_NONE, CTOOL_C_STORAGE_NONE, CTOOL_C_STORAGE_NONE};
  static const ctool_u32 lines[] = {
      7u, 7u, 7u, 11u, 12u, 13u, 14u, 20u, 27u};
  static const ctool_u32 declaration_first[] = {
      0u, 3u, 4u, 5u, 6u, 7u, 8u};
  static const ctool_u32 declaration_count[] = {
      3u, 1u, 1u, 1u, 1u, 1u, 1u};
  static const ctool_c_expression_kind_t argument_kinds[] = {
      CTOOL_C_EXPRESSION_IDENTIFIER,
      CTOOL_C_EXPRESSION_BLOCK_BINDING,
      CTOOL_C_EXPRESSION_BLOCK_BINDING,
      CTOOL_C_EXPRESSION_BLOCK_BINDING,
      CTOOL_C_EXPRESSION_BLOCK_BINDING,
      CTOOL_C_EXPRESSION_BLOCK_BINDING,
      CTOOL_C_EXPRESSION_BLOCK_BINDING,
      CTOOL_C_EXPRESSION_BLOCK_BINDING,
      CTOOL_C_EXPRESSION_PARAMETER,
      CTOOL_C_EXPRESSION_BLOCK_BINDING,
      CTOOL_C_EXPRESSION_BLOCK_BINDING,
      CTOOL_C_EXPRESSION_BLOCK_BINDING};
  static const ctool_u32 argument_references[] = {
      0u, 0u, 2u, 3u, 4u, 6u, 5u, 7u, 0u, 0u, 1u, 8u};
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function;
  ctool_u32 file_value;
  ctool_u32 declaration_index = 0u;
  ctool_u32 call_index = 0u;
  ctool_u32 index;

  if (unit->block_binding_count != ARRAY_COUNT(names) ||
      unit->statement_count != 22u || unit->statement_child_count != 20u ||
      unit->expression_count != 60u ||
      unit->expression_child_count != 48u ||
      unit->function_definition_count != 2u) {
    (void)fprintf(stderr, "block-bindings: AST inventory differs\n");
    return 1;
  }
  definition = &unit->function_definitions[0];
  function = type_node(unit, definition->declared_type);
  file_value = find_binding_index(unit, "file_value");
  if (function == NULL || function->kind != CTOOL_C_TYPE_FUNCTION ||
      function->parameter_count != 1u ||
      function->first_parameter >= unit->parameter_count ||
      file_value == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr,
                  "block-bindings: definition metadata differs\n");
    return 1;
  }
  for (index = 0u; index < unit->block_binding_count; index++) {
    const ctool_c_block_binding_t *binding = &unit->block_bindings[index];
    const ctool_c_type_layout_t *layout = type_layout(unit, binding->type);
    if (!string_equal(binding->name, names[index]) ||
        binding->kind != CTOOL_C_BINDING_OBJECT ||
        binding->storage != storage[index] || binding->type >= unit->graph.type_count ||
        binding->initializer != CTOOL_C_AST_NONE ||
        layout == NULL || layout->is_complete_object != CTOOL_TRUE ||
        !dual_location_matches(&binding->location,
                               &binding->physical_location,
                               "/block-bindings.c", lines[index])) {
      (void)fprintf(stderr,
                    "block-bindings: binding %u metadata differs\n", index);
      return 1;
    }
  }
  {
    const ctool_c_type_node_t *pointer =
        type_node(unit, unit->block_bindings[2].type);
    if (pointer == NULL || pointer->kind != CTOOL_C_TYPE_POINTER) {
      (void)fprintf(stderr,
                    "block-bindings: mixed declarator type differs\n");
      return 1;
    }
  }
  for (index = 0u; index < unit->statement_count; index++) {
    const ctool_c_statement_t *statement = &unit->statements[index];
    if (statement->kind != CTOOL_C_STATEMENT_DECLARATION) {
      continue;
    }
    if (declaration_index >= ARRAY_COUNT(declaration_first) ||
        statement->first_block_binding !=
            declaration_first[declaration_index] ||
        statement->block_binding_count !=
            declaration_count[declaration_index] ||
        statement->first_child != CTOOL_C_AST_NONE ||
        statement->expression != CTOOL_C_AST_NONE) {
      (void)fprintf(stderr,
                    "block-bindings: declaration slice %u differs\n",
                    declaration_index);
      return 1;
    }
    declaration_index++;
  }
  if (declaration_index != ARRAY_COUNT(declaration_first)) {
    (void)fprintf(stderr,
                  "block-bindings: declaration inventory differs\n");
    return 1;
  }
  for (index = 0u; index < unit->expression_count; index++) {
    const ctool_c_expression_t *call = &unit->expressions[index];
    const ctool_c_expression_t *argument;
    ctool_u32 argument_index;
    ctool_u32 expected_reference;
    if (call->kind != CTOOL_C_EXPRESSION_CALL) {
      continue;
    }
    if (call_index >= ARRAY_COUNT(argument_kinds) ||
        call->child_count != 2u ||
        call->first_child > unit->expression_child_count ||
        call->child_count >
            unit->expression_child_count - call->first_child) {
      (void)fprintf(stderr, "block-bindings: call %u differs\n", call_index);
      return 1;
    }
    argument_index = unit->expression_children[call->first_child + 1u];
    argument = argument_index < unit->expression_count
                   ? expression_terminal(unit, &unit->expressions[argument_index])
                   : NULL;
    expected_reference = argument_references[call_index];
    if (call_index == 0u) {
      expected_reference = file_value;
    } else if (call_index == 8u) {
      expected_reference = function->first_parameter;
    }
    if (argument == NULL || argument->kind != argument_kinds[call_index] ||
        argument->reference != expected_reference) {
      (void)fprintf(stderr,
                    "block-bindings: call argument %u resolution differs\n",
                    call_index);
      return 1;
    }
    call_index++;
  }
  if (call_index != ARRAY_COUNT(argument_kinds)) {
    (void)fprintf(stderr, "block-bindings: call inventory differs\n");
    return 1;
  }
  return 0;
}

static int run_block_bindings(const char *host_root) {
  static const char source[] =
      "typedef unsigned int word;\n"
      "void sink(word value);\n"
      "void sink_pointer(word *value);\n"
      "word file_value;\n"
      "void locals(register word parameter) {\n"
      "  sink(file_value);\n"
      "  word file_value, outer, *pointer;\n"
      "  sink(file_value);\n"
      "  sink_pointer(pointer);\n"
      "  {\n"
      "    auto word nested;\n"
      "    register word parameter;\n"
      "    word outer;\n"
      "    word word;\n"
      "    sink(nested);\n"
      "    sink(parameter);\n"
      "    sink(word);\n"
      "    sink(outer);\n"
      "  }\n"
      "  word tail;\n"
      "  sink(tail);\n"
      "  sink(parameter);\n"
      "  sink(file_value);\n"
      "  sink(outer);\n"
      "}\n"
      "void second(void) {\n"
      "  word file_value;\n"
      "  sink(file_value);\n"
      "}\n";
  static const frontend_exact_failure_case_t failure_cases[] = {
      {{"duplicate local in declarator list",
        "void bad(void) { int local, local; }\n", CTOOL_ERR_INPUT,
        CTOOL_C_PARSE_DIAG_REDEFINITION},
       1u, 29u, "block-scope identifier is already declared in this scope"},
      {{"outer local duplicates parameter",
        "void bad(int value) { int value; }\n", CTOOL_ERR_INPUT,
        CTOOL_C_PARSE_DIAG_REDEFINITION},
       1u, 27u, "block-scope identifier is already declared in this scope"},
      {{"duplicate local across declarations",
        "void bad(void) { int local; int local; }\n", CTOOL_ERR_INPUT,
        CTOOL_C_PARSE_DIAG_REDEFINITION},
       1u, 33u, "block-scope identifier is already declared in this scope"},
      {{"static local boundary", "void bad(void) { static int local; }\n",
        CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_STATEMENT},
       1u, 18u, "block storage class is outside this body slice"},
      {{"extern local boundary", "void bad(void) { extern int local; }\n",
        CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_STATEMENT},
       1u, 18u, "block storage class is outside this body slice"},
      {{"block typedef boundary", "void bad(void) { typedef int local; }\n",
        CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_STATEMENT},
       1u, 18u, "block storage class is outside this body slice"},
      {{"block function declaration boundary",
        "void bad(void) { int local(void); }\n", CTOOL_ERR_UNSUPPORTED,
        CTOOL_C_PARSE_DIAG_STATEMENT},
       1u, 22u, "block function declarations are outside this body slice"},
      {{"void block object", "void bad(void) { void local; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_TYPE_NAME},
       1u, 23u, "block object requires a complete object type"},
      {{"incomplete block array", "void bad(void) { int local[]; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_TYPE_NAME},
       1u, 22u, "block object requires a complete object type"},
      {{"incomplete block object",
        "struct S;\nvoid bad(void) { struct S local; }\n",
        CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_STATEMENT},
       2u, 18u, "block tag specifiers are outside this body slice"},
      {{"block static assertion boundary",
        "void bad(void) { _Static_assert(1, \"ok\"); }\n",
        CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_STATEMENT},
       1u, 18u,
       "block static assertions are outside this function-body slice"},
      {{"block attribute boundary",
        "void bad(void) { int local __attribute__((aligned(8))); }\n",
        CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_STATEMENT},
       1u, 43u, "block declaration attributes are outside this body slice"},
      {{"GNU assembly boundary",
        "void bad(void) { __asm__(\"nop\"); }\n",
        CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_STATEMENT},
       1u, 18u, "GNU inline assembly is outside this function-body slice"},
      {{"expired local use",
        "void sink(int value);\n"
        "void bad(void) { { int local; sink(local); } sink(local); }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       2u, 51u, "expression identifier is not declared"}};
  frontend_fixture_t fixture;
  ctool_c_translation_unit_t unit;
  ctool_u32 index;
  int failed = 1;

  if (begin_frontend_fixture(&fixture, "block-bindings", host_root,
                             8u * 1024u * 1024u) != 0) {
    return 1;
  }
  if (parse_valid_fixture(&fixture, "/block-bindings.c", source, &unit) != 0 ||
      validate_block_binding_unit(&unit) != 0) {
    goto cleanup;
  }
  for (index = 0u; index < ARRAY_COUNT(failure_cases); index++) {
    const frontend_exact_failure_case_t *test_case = &failure_cases[index];
    if (expect_frontend_failure_at_message(
            &fixture, &test_case->failure, "/block-binding-failure.c",
            test_case->line, test_case->column, test_case->message) != 0 ||
        validate_block_binding_unit(&unit) != 0) {
      goto cleanup;
    }
  }
  failed = 0;

cleanup:
  if (finish_frontend_fixture(&fixture) != 0) {
    failed = 1;
  }
  if (failed == 0) {
    (void)printf("block-bindings: ok\n");
  }
  return failed;
}

static int validate_scalar_initializer_unit(
    const ctool_c_translation_unit_t *unit) {
  static const char *const names[] = {
      "uninitialized", "first", "second", "narrowed",
      "choice", "pointer", "qualified", "array", "decayed",
      "callback", "registered", "atomic", "observed", "cast_pointer",
      "hidden", "self_size", "self_reference", "braced", "trailing"};
  static const ctool_bool initialized[] = {
      CTOOL_FALSE, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE,
      CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE,
      CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE,
      CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE,
      CTOOL_TRUE, CTOOL_TRUE, CTOOL_TRUE};
  ctool_u32 first;
  ctool_u32 second;
  ctool_u32 narrowed;
  ctool_u32 choice;
  ctool_u32 pointer;
  ctool_u32 qualified;
  ctool_u32 decayed;
  ctool_u32 callback;
  ctool_u32 registered;
  ctool_u32 atomic;
  ctool_u32 observed;
  ctool_u32 cast_pointer;
  ctool_u32 hidden;
  ctool_u32 self_size;
  ctool_u32 self_reference;
  ctool_u32 braced;
  ctool_u32 trailing;
  ctool_u32 enum_one;
  ctool_u32 sink;
  ctool_u32 index;

  if (unit->block_binding_count != ARRAY_COUNT(names) ||
      unit->function_definition_count != 1u) {
    (void)fprintf(stderr, "scalar-initializers: binding inventory differs\n");
    return 1;
  }
  for (index = 0u; index < ARRAY_COUNT(names); index++) {
    const ctool_c_block_binding_t *binding = &unit->block_bindings[index];
    if (!string_equal(binding->name, names[index]) ||
        binding->kind != CTOOL_C_BINDING_OBJECT ||
        binding->type >= unit->graph.type_count ||
        (initialized[index] == CTOOL_TRUE &&
         binding->initializer >= unit->expression_count) ||
        (initialized[index] == CTOOL_FALSE &&
         binding->initializer != CTOOL_C_AST_NONE)) {
      (void)fprintf(stderr,
                    "scalar-initializers: binding %u differs\n", index);
      return 1;
    }
  }
  if (unit->block_bindings[1].storage != CTOOL_C_STORAGE_AUTO ||
      unit->block_bindings[2].storage != CTOOL_C_STORAGE_AUTO ||
      unit->block_bindings[10].storage != CTOOL_C_STORAGE_REGISTER ||
      unit->block_bindings[3].storage != CTOOL_C_STORAGE_NONE) {
    (void)fprintf(stderr,
                  "scalar-initializers: source storage differs\n");
    return 1;
  }
  first = unit->block_bindings[1].initializer;
  second = unit->block_bindings[2].initializer;
  narrowed = unit->block_bindings[3].initializer;
  choice = unit->block_bindings[4].initializer;
  pointer = unit->block_bindings[5].initializer;
  qualified = unit->block_bindings[6].initializer;
  decayed = unit->block_bindings[8].initializer;
  callback = unit->block_bindings[9].initializer;
  registered = unit->block_bindings[10].initializer;
  atomic = unit->block_bindings[11].initializer;
  observed = unit->block_bindings[12].initializer;
  cast_pointer = unit->block_bindings[13].initializer;
  hidden = unit->block_bindings[14].initializer;
  self_size = unit->block_bindings[15].initializer;
  self_reference = unit->block_bindings[16].initializer;
  braced = unit->block_bindings[17].initializer;
  trailing = unit->block_bindings[18].initializer;
  enum_one = find_binding_index(unit, "ENUM_ONE");
  sink = find_binding_index(unit, "sink");
  if (unit->expressions[first].kind != CTOOL_C_EXPRESSION_INTEGER_CONSTANT ||
      unit->expressions[first].integer_bits != 1ull ||
      unit->expressions[second].kind != CTOOL_C_EXPRESSION_BINARY ||
      unit->expressions[second].operation != CTOOL_C_EXPRESSION_OPERATOR_ADD ||
      unit->expressions[narrowed].kind !=
          CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION ||
      unit->expressions[narrowed].conversion != CTOOL_C_CONVERSION_ASSIGNMENT ||
      unit->expressions[choice].kind !=
          CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION ||
      unit->expressions[choice].conversion != CTOOL_C_CONVERSION_ASSIGNMENT ||
      unit->expressions[pointer].kind != CTOOL_C_EXPRESSION_UNARY ||
      unit->expressions[pointer].operation !=
          CTOOL_C_EXPRESSION_OPERATOR_ADDRESS ||
      unit->expressions[qualified].kind !=
          CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION ||
      unit->expressions[qualified].conversion !=
          CTOOL_C_CONVERSION_QUALIFICATION ||
      unit->expressions[decayed].kind !=
          CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION ||
      unit->expressions[decayed].conversion !=
          CTOOL_C_CONVERSION_ARRAY_TO_POINTER ||
      unit->expressions[callback].kind !=
          CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION ||
      unit->expressions[callback].conversion !=
          CTOOL_C_CONVERSION_FUNCTION_TO_POINTER ||
      unit->expressions[cast_pointer].kind != CTOOL_C_EXPRESSION_CAST ||
      unit->expressions[hidden].kind !=
          CTOOL_C_EXPRESSION_INTEGER_CONSTANT ||
      unit->expressions[hidden].integer_bits != 4ull ||
      unit->expressions[self_size].kind !=
          CTOOL_C_EXPRESSION_INTEGER_CONSTANT ||
      unit->expressions[self_size].integer_bits != 4ull ||
      unit->expressions[trailing].kind !=
          CTOOL_C_EXPRESSION_INTEGER_CONSTANT ||
      unit->expressions[trailing].integer_bits != 1ull) {
    (void)fprintf(stderr,
                  "scalar-initializers: converted initializer AST differs "
                  "(%u/%llu %u/%u %u/%u %u/%u %u/%u %u/%llu)\n",
                  (unsigned int)unit->expressions[first].kind,
                  (unsigned long long)unit->expressions[first].integer_bits,
                  (unsigned int)unit->expressions[second].kind,
                  (unsigned int)unit->expressions[second].operation,
                  (unsigned int)unit->expressions[narrowed].kind,
                  (unsigned int)unit->expressions[narrowed].conversion,
                  (unsigned int)unit->expressions[choice].kind,
                  (unsigned int)unit->expressions[choice].reference,
                  (unsigned int)unit->expressions[pointer].kind,
                  (unsigned int)unit->expressions[pointer].operation,
                  (unsigned int)unit->expressions[self_size].kind,
                  (unsigned long long)unit->expressions[self_size].integer_bits);
    return 1;
  }
  {
    static const ctool_c_expression_kind_t kinds[] = {
        CTOOL_C_EXPRESSION_BLOCK_BINDING,
        CTOOL_C_EXPRESSION_BLOCK_BINDING,
        CTOOL_C_EXPRESSION_IDENTIFIER,
        CTOOL_C_EXPRESSION_BLOCK_BINDING,
        CTOOL_C_EXPRESSION_BLOCK_BINDING,
        CTOOL_C_EXPRESSION_BLOCK_BINDING};
    static const ctool_u32 references[] = {
        5u, 7u, CTOOL_C_AST_NONE, 2u, 10u, 11u};
    const ctool_u32 expressions[] = {
        qualified, decayed, callback, registered, atomic, observed};
    for (index = 0u; index < ARRAY_COUNT(expressions); index++) {
      const ctool_c_expression_t *terminal =
          expression_terminal(unit, &unit->expressions[expressions[index]]);
      ctool_u32 expected_reference =
          references[index] == CTOOL_C_AST_NONE ? sink : references[index];
      if (terminal == NULL || terminal->kind != kinds[index] ||
          terminal->reference != expected_reference) {
        (void)fprintf(stderr,
                      "scalar-initializers: conversion source %u differs\n",
                      (unsigned int)index);
        return 1;
      }
    }
  }
  {
    const ctool_c_type_node_t *narrowed_object =
        type_node(unit, unit->block_bindings[3].type);
    const ctool_c_type_node_t *atomic_object =
        type_node(unit, unit->block_bindings[11].type);
    const ctool_c_type_node_t *volatile_object =
        type_node(unit, unit->block_bindings[12].type);
    if (narrowed_object == NULL ||
        narrowed_object->kind != CTOOL_C_TYPE_QUALIFIED ||
        (narrowed_object->qualifiers & CTOOL_C_QUAL_CONST) == 0u ||
        atomic_object == NULL || atomic_object->kind != CTOOL_C_TYPE_QUALIFIED ||
        (atomic_object->qualifiers & CTOOL_C_QUAL_ATOMIC) == 0u ||
        volatile_object == NULL ||
        volatile_object->kind != CTOOL_C_TYPE_QUALIFIED ||
        (volatile_object->qualifiers & CTOOL_C_QUAL_VOLATILE) == 0u ||
        unit->expressions[narrowed].type == unit->block_bindings[3].type ||
        unit->expressions[atomic].type == unit->block_bindings[11].type ||
        unit->expressions[observed].type == unit->block_bindings[12].type) {
      (void)fprintf(stderr,
                    "scalar-initializers: qualified target conversion differs\n");
      return 1;
    }
  }
  {
    const ctool_c_expression_t *terminal =
        expression_terminal(unit, &unit->expressions[choice]);
    if (terminal == NULL ||
        terminal->kind != CTOOL_C_EXPRESSION_IDENTIFIER ||
        terminal->reference != enum_one) {
      (void)fprintf(stderr,
                    "scalar-initializers: enum initializer differs\n");
      return 1;
    }
  }
  {
    const ctool_c_expression_t *terminal =
        expression_terminal(unit, &unit->expressions[braced]);
    if (terminal == NULL ||
        terminal->kind != CTOOL_C_EXPRESSION_BLOCK_BINDING ||
        terminal->reference != 2u) {
      (void)fprintf(stderr,
                    "scalar-initializers: braced initializer differs\n");
      return 1;
    }
  }
  {
    const ctool_c_expression_t *terminal =
        expression_terminal(unit, &unit->expressions[self_reference]);
    if (terminal == NULL ||
        terminal->kind != CTOOL_C_EXPRESSION_BLOCK_BINDING ||
        terminal->reference != 16u) {
      (void)fprintf(stderr,
                    "scalar-initializers: pending self reference differs\n");
      return 1;
    }
  }
  return 0;
}

static int validate_toolchain_for_frontier(const char *host_root) {
  static const char *const paths[] = {
      "/toolchain/ctool.c", "/toolchain/cupiddis.c",
      "/toolchain/cupidld.c", "/toolchain/cupidobj.c",
      "/toolchain/cupidc_type.c"};
  static const ctool_u32 lines[] = {71u, 30u, 152u, 24u, 40u};
  static const ctool_u32 diagnostic_codes[] = {
      CTOOL_C_PARSE_DIAG_EXPRESSION, CTOOL_C_PARSE_DIAG_EXPRESSION,
      CTOOL_C_PARSE_DIAG_EXPRESSION, CTOOL_C_PARSE_DIAG_STATEMENT,
      CTOOL_C_PARSE_DIAG_STATEMENT};
  static const char *const messages[] = {
      "expression operator is outside this function-body slice",
      "non-scalar assignment is outside this function-body slice",
      "expression operator is outside this function-body slice",
      "statement form is outside this function-body slice",
      "statement form is outside this function-body slice"};
  ctool_u32 index;
  for (index = 0u; index < ARRAY_COUNT(paths); index++) {
    ctool_host_adapter_t adapter;
    ctool_job_t *job = NULL;
    ctool_path_t path;
    ctool_source_t source;
    ctool_c_pp_macro_action_t pointer_width;
    ctool_c_pp_request_t pp_request;
    ctool_c_parse_request_t parse_request;
    ctool_c_pp_result_t tape;
    ctool_c_translation_unit_t unit;
    ctool_c_pp_token_t *snapshot = NULL;
    const ctool_diagnostic_t *diagnostic;
    ctool_arena_mark_t mark;
    ctool_status_t status;
    size_t token_bytes;
    int failed = 1;

    if (open_job("for-statements", host_root,
                 256u * 1024u * 1024u, &adapter, &job) != 0) {
      return 1;
    }
    (void)memset(&pointer_width, 0, sizeof(pointer_width));
    pointer_width.kind = CTOOL_C_PP_MACRO_DEFINE;
    pointer_width.name = ctool_string("__SIZEOF_POINTER__");
    pointer_width.replacement = ctool_string("8");
    (void)memset(&pp_request, 0, sizeof(pp_request));
    pp_request.mode = CTOOL_C_PP_MODE_C11;
    pp_request.gnu_extensions = CTOOL_FALSE;
    pp_request.hosted_environment = CTOOL_TRUE;
    pp_request.macro_actions = &pointer_width;
    pp_request.macro_action_count = 1u;
    parse_request.mode = CTOOL_C_PP_MODE_C11;
    parse_request.gnu_extensions = CTOOL_FALSE;
    path.text = ctool_string(paths[index]);
    status = ctool_job_load_source(job, &path, &source);
    (void)memset(&tape, 0xa5, sizeof(tape));
    if (status == CTOOL_OK) {
      status = ctool_c_preprocess(job, &source, &pp_request, &tape);
    }
    if (status != CTOOL_OK || tape.tokens == NULL || tape.token_count == 0u ||
        ctool_job_diagnostic_count(job) != 0u) {
      (void)fprintf(stderr,
                    "for-statements: prepare frontier %s failed: %s\n",
                    paths[index], ctool_status_name(status));
      (void)ctool_job_render_diagnostics(job);
      ctool_job_close(job);
      return 1;
    }
    token_bytes = (size_t)tape.token_count * sizeof(*snapshot);
    snapshot = (ctool_c_pp_token_t *)malloc(token_bytes);
    if (snapshot != NULL) {
      (void)memcpy(snapshot, tape.tokens, token_bytes);
      mark = ctool_arena_mark(ctool_job_arena(job));
      (void)memset(&unit, 0xa5, sizeof(unit));
      status = ctool_c_parse(job, &tape, &parse_request, &unit);
      diagnostic = ctool_job_diagnostic(job, 0u);
      if (status == CTOOL_ERR_UNSUPPORTED && unit_is_zero(&unit) != 0 &&
          ctool_job_diagnostic_count(job) == 1u && diagnostic != NULL &&
          diagnostic->code == diagnostic_codes[index] &&
          string_equal(diagnostic->path, paths[index]) != 0 &&
          diagnostic->line == lines[index] && diagnostic->column != 0u &&
          string_equal(diagnostic->message, messages[index]) != 0 &&
          arena_marks_equal(mark,
                            ctool_arena_mark(ctool_job_arena(job))) != 0 &&
          memcmp(snapshot, tape.tokens, token_bytes) == 0) {
        failed = 0;
      }
    }
    if (failed != 0) {
      (void)fprintf(stderr,
                    "for-statements: frontier %s differs: %s\n",
                    paths[index], ctool_status_name(status));
      (void)ctool_job_render_diagnostics(job);
    }
    free(snapshot);
    ctool_job_close(job);
    if (failed != 0) {
      return 1;
    }
  }
  return 0;
}

static char *build_scalar_initializer_limit_source(ctool_bool initialized) {
  const size_t capacity = 32768u;
  char *source = (char *)malloc(capacity);
  size_t used = 0u;
  ctool_u32 index;
  if (source == NULL) {
    return NULL;
  }
  source[0] = '\0';
  if (append_scale_text(source, capacity, &used,
                        "void initializer_limit(void) {\n") != 0) {
    free(source);
    return NULL;
  }
  for (index = 0u; index < 128u; index++) {
    char declaration[256];
    int written = snprintf(
        declaration, sizeof(declaration),
        initialized == CTOOL_TRUE
            ? "  int value_%03u = 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8;\n"
            : "  int value_%03u;\n",
        (unsigned int)index);
    if (written <= 0 || (size_t)written >= sizeof(declaration) ||
        append_scale_text(source, capacity, &used, declaration) != 0) {
      free(source);
      return NULL;
    }
  }
  if (append_scale_text(source, capacity, &used, "}\n") != 0) {
    free(source);
    return NULL;
  }
  return source;
}

static ctool_u32 scalar_initializer_unit_max_bytes(
    const ctool_c_translation_unit_t *unit) {
  ctool_u64 maximum = 0ull;
#define INITIALIZER_MAX_BYTES(count, type)                                  \
  do {                                                                       \
    ctool_u64 candidate = (ctool_u64)(count) * (ctool_u64)sizeof(type);      \
    if (candidate > maximum) {                                               \
      maximum = candidate;                                                   \
    }                                                                        \
  } while (0)
  INITIALIZER_MAX_BYTES(unit->graph.type_count, ctool_c_type_node_t);
  INITIALIZER_MAX_BYTES(unit->graph.member_count, ctool_c_record_member_t);
  INITIALIZER_MAX_BYTES(unit->graph.parameter_type_count, ctool_u32);
  INITIALIZER_MAX_BYTES(unit->parameter_count, ctool_c_parameter_t);
  INITIALIZER_MAX_BYTES(unit->binding_count, ctool_c_binding_t);
  INITIALIZER_MAX_BYTES(unit->tag_count, ctool_c_tag_t);
  INITIALIZER_MAX_BYTES(unit->block_binding_count, ctool_c_block_binding_t);
  INITIALIZER_MAX_BYTES(unit->function_definition_count,
                        ctool_c_function_definition_t);
  INITIALIZER_MAX_BYTES(unit->statement_count, ctool_c_statement_t);
  INITIALIZER_MAX_BYTES(unit->statement_child_count, ctool_u32);
  INITIALIZER_MAX_BYTES(unit->expression_count, ctool_c_expression_t);
  INITIALIZER_MAX_BYTES(unit->expression_child_count, ctool_u32);
#undef INITIALIZER_MAX_BYTES
  return maximum <= 0xffffffffull ? (ctool_u32)maximum : 0u;
}

static int validate_scalar_initializer_storage_limit(
    frontend_fixture_t *fixture, const char *host_root) {
  char *control_source = build_scalar_initializer_limit_source(CTOOL_FALSE);
  char *initializer_source =
      build_scalar_initializer_limit_source(CTOOL_TRUE);
  ctool_c_translation_unit_t control_oracle;
  ctool_c_translation_unit_t initializer_oracle;
  ctool_c_translation_unit_t control;
  ctool_c_translation_unit_t failed_unit;
  ctool_c_translation_unit_t recovered;
  ctool_c_pp_result_t control_tape;
  ctool_c_pp_result_t initializer_tape;
  ctool_c_pp_token_t *snapshot = NULL;
  ctool_limits_t limits = ctool_default_limits();
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_arena_mark_t mark;
  const ctool_diagnostic_t *diagnostic;
  ctool_status_t status;
  ctool_u32 output_limit;
  size_t token_bytes;
  int failed = 1;

  if (control_source == NULL || initializer_source == NULL ||
      parse_valid_fixture(fixture, "/initializer-limit-control.c",
                          control_source, &control_oracle) != 0 ||
      parse_valid_fixture(fixture, "/initializer-limit-success.c",
                          initializer_source, &initializer_oracle) != 0 ||
      control_oracle.block_binding_count != 128u ||
      initializer_oracle.block_binding_count != 128u ||
      initializer_oracle.expression_count <= control_oracle.expression_count ||
      preprocess_fixture(fixture, "/initializer-limit-control.c",
                         control_source, &control_tape) != 0 ||
      preprocess_fixture(fixture, "/initializer-limit.c",
                         initializer_source, &initializer_tape) != 0) {
    (void)fprintf(stderr,
                  "scalar-initializers: storage-limit controls differ\n");
    goto cleanup;
  }
  output_limit = scalar_initializer_unit_max_bytes(&control_oracle);
  if (output_limit == 0u || output_limit > 0xffffffffu / 4u) {
    (void)fprintf(stderr,
                  "scalar-initializers: storage-limit measurement differs\n");
    goto cleanup;
  }
  output_limit *= 4u;
  if (
      (ctool_u64)initializer_oracle.expression_count *
              sizeof(ctool_c_expression_t) <=
          output_limit) {
    (void)fprintf(stderr,
                  "scalar-initializers: storage-limit measurement differs\n");
    goto cleanup;
  }
  token_bytes =
      (size_t)initializer_tape.token_count * sizeof(*snapshot);
  snapshot = (ctool_c_pp_token_t *)malloc(token_bytes);
  if (snapshot == NULL) {
    goto cleanup;
  }
  (void)memcpy(snapshot, initializer_tape.tokens, token_bytes);
  limits.output_bytes = output_limit;
  status = ctool_host_adapter_init(&adapter, host_root);
  if (status != CTOOL_OK) {
    goto cleanup;
  }
  config = ctool_host_job_config(&adapter, limits);
  status = ctool_job_open(&config, &job);
  if (status != CTOOL_OK) {
    goto cleanup;
  }
  (void)memset(&control, 0xa5, sizeof(control));
  status = ctool_c_parse(job, &control_tape, &fixture->parse_request,
                         &control);
  if (status != CTOOL_OK || control.block_binding_count != 128u ||
      control.block_bindings[0].initializer != CTOOL_C_AST_NONE ||
      control.block_bindings[127].initializer != CTOOL_C_AST_NONE) {
    (void)fprintf(stderr,
                  "scalar-initializers: limited control failed: %s/%u\n",
                  ctool_status_name(status), (unsigned int)output_limit);
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  (void)memset(&failed_unit, 0xa5, sizeof(failed_unit));
  status = ctool_c_parse(job, &initializer_tape, &fixture->parse_request,
                         &failed_unit);
  diagnostic = ctool_job_diagnostic(job, 0u);
  if (status != CTOOL_ERR_LIMIT || unit_is_zero(&failed_unit) == 0 ||
      ctool_job_diagnostic_count(job) != 1u || diagnostic == NULL ||
      diagnostic->code != CTOOL_C_PARSE_DIAG_LIMIT ||
      !string_equal(diagnostic->path, "/initializer-limit.c") ||
      diagnostic->line == 0u || diagnostic->column == 0u ||
      !string_equal(diagnostic->message,
                    "declaration frontend storage limit exceeded") ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      memcmp(snapshot, initializer_tape.tokens, token_bytes) != 0 ||
      control.block_binding_count != 128u ||
      control.block_bindings[0].initializer != CTOOL_C_AST_NONE ||
      control.block_bindings[127].initializer != CTOOL_C_AST_NONE) {
    (void)fprintf(stderr,
                  "scalar-initializers: limited rollback differs: %s/%u\n",
                  ctool_status_name(status), (unsigned int)output_limit);
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  (void)memset(&recovered, 0xa5, sizeof(recovered));
  status = ctool_c_parse(job, &control_tape, &fixture->parse_request,
                         &recovered);
  if (status != CTOOL_OK || recovered.block_binding_count != 128u ||
      recovered.block_bindings[0].initializer != CTOOL_C_AST_NONE ||
      recovered.block_bindings[127].initializer != CTOOL_C_AST_NONE ||
      control.block_binding_count != 128u ||
      ctool_job_diagnostic_count(job) != 1u) {
    (void)fprintf(stderr,
                  "scalar-initializers: limited recovery differs\n");
    goto cleanup;
  }
  failed = 0;

cleanup:
  if (job != NULL) {
    ctool_job_close(job);
  }
  free(snapshot);
  free(initializer_source);
  free(control_source);
  return failed;
}

static int run_scalar_initializers(const char *host_root) {
  static const char source[] =
      "typedef unsigned char byte;\n"
      "typedef unsigned int word;\n"
      "typedef byte hidden;\n"
      "typedef enum { ENUM_ZERO, ENUM_ONE } choice_t;\n"
      "void sink(word value);\n"
      "void initialized(word parameter) {\n"
      "  word uninitialized;\n"
      "  auto word first = 1u, second = first + parameter;\n"
      "  const byte narrowed = second;\n"
      "  choice_t choice = ENUM_ONE;\n"
      "  word *pointer = &first;\n"
      "  const word *qualified = pointer;\n"
      "  word array[2];\n"
      "  word *decayed = array;\n"
      "  void (*callback)(word) = sink;\n"
      "  register word registered = second;\n"
      "  _Atomic word atomic = registered;\n"
      "  volatile word observed = atomic;\n"
      "  byte *cast_pointer = (byte *)pointer;\n"
      "  word hidden = sizeof(hidden);\n"
      "  word self_size = sizeof self_size;\n"
      "  word self_reference = self_reference;\n"
      "  word braced = { second };\n"
      "  word trailing = {{ 1u },};\n"
      "}\n";
  static const frontend_exact_failure_case_t failure_cases[] = {
      {{"aggregate automatic initializer",
         "typedef struct { int member; } item_t;\n"
         "void bad(void) { item_t item = { 1 }; }\n",
         CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_STATEMENT},
        2u, 30u,
        "aggregate block object initializers are outside this body slice"},
      {{"unknown-bound array initializer boundary",
         "void bad(void) { int values[] = { 1 }; }\n",
         CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_STATEMENT},
        1u, 31u,
        "aggregate block object initializers are outside this body slice"},
      {{"incompatible scalar initializer",
         "void bad(void) { int value = \"text\"; }\n",
         CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
        1u, 30u,
        "initializer expression is not convertible to block object type"},
      {{"later declarator is not visible",
        "void bad(void) { int first = later, later = 1; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       1u, 30u, "expression identifier is not declared"},
      {{"pending register binding retains address restriction",
        "void bad(void) { register int value = *(&value); }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       1u, 41u, "address operator cannot apply to a register object"},
      {{"missing scalar initializer expression",
        "void bad(void) { int value = ; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_STATEMENT},
       1u, 30u, "scalar initializer requires an expression"},
      {{"empty scalar initializer",
         "void bad(void) { int value = { }; }\n",
         CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_STATEMENT},
        1u, 32u, "scalar initializer list requires one expression"},
      {{"excess scalar initializers",
         "void bad(void) { int value = { 1, 2 }; }\n",
         CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_STATEMENT},
        1u, 35u, "scalar initializer list has excess elements"},
      {{"floating scalar initializer boundary",
         "void bad(void) { double value = 1.0; }\n",
         CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_EXPRESSION},
        1u, 33u, "floating constants are outside this expression slice"}};
  frontend_fixture_t fixture;
  ctool_c_translation_unit_t unit;
  ctool_u32 index;
  int failed = 1;

  if (begin_frontend_fixture(&fixture, "scalar-initializers", host_root,
                             8u * 1024u * 1024u) != 0) {
    return 1;
  }
  fixture.pp_request.gnu_extensions = CTOOL_FALSE;
  fixture.parse_request.gnu_extensions = CTOOL_FALSE;
  if (parse_valid_fixture(&fixture, "/scalar-initializers.c", source, &unit) !=
          0 ||
      validate_scalar_initializer_unit(&unit) != 0) {
    goto cleanup;
  }
  for (index = 0u; index < ARRAY_COUNT(failure_cases); index++) {
    const frontend_exact_failure_case_t *test_case = &failure_cases[index];
    if (expect_frontend_failure_at_message(
            &fixture, &test_case->failure, "/scalar-initializer-failure.c",
            test_case->line, test_case->column, test_case->message) != 0 ||
        validate_scalar_initializer_unit(&unit) != 0) {
      goto cleanup;
    }
  }
  if (validate_scalar_initializer_storage_limit(&fixture, host_root) != 0) {
    goto cleanup;
  }
  failed = 0;

cleanup:
  if (finish_frontend_fixture(&fixture) != 0) {
    failed = 1;
  }
  if (failed == 0) {
    (void)printf("scalar-initializers: ok\n");
  }
  return failed;
}

static ctool_u32 scalar_expression_child(
    const ctool_c_translation_unit_t *unit,
    const ctool_c_expression_t *expression, ctool_u32 child);
static ctool_u32 scalar_unwrap_conversions(
    const ctool_c_translation_unit_t *unit, ctool_u32 expression);
static ctool_bool scalar_conversion_chain_has(
    const ctool_c_translation_unit_t *unit, ctool_u32 expression,
    ctool_c_conversion_kind_t conversion);
static ctool_c_type_kind_t scalar_type_kind(
    const ctool_c_translation_unit_t *unit, ctool_u32 type,
    ctool_u32 *qualifiers_out);

static int for_expect_terminal_reference(
    const ctool_c_translation_unit_t *unit, ctool_u32 expression,
    ctool_c_expression_kind_t kind, ctool_u32 reference) {
  expression = scalar_unwrap_conversions(unit, expression);
  return expression < unit->expression_count &&
                 unit->expressions[expression].kind == kind &&
                 unit->expressions[expression].reference == reference
             ? 0
             : 1;
}

static int for_expect_call_argument(
    const ctool_c_translation_unit_t *unit, ctool_u32 statement,
    ctool_u32 binding) {
  const ctool_c_statement_t *node;
  const ctool_c_expression_t *call;
  ctool_u32 argument;
  if (statement >= unit->statement_count) {
    return 1;
  }
  node = &unit->statements[statement];
  if (node->kind != CTOOL_C_STATEMENT_EXPRESSION ||
      node->expression >= unit->expression_count) {
    return 1;
  }
  call = &unit->expressions[node->expression];
  if (call->kind != CTOOL_C_EXPRESSION_CALL || call->child_count != 2u) {
    return 1;
  }
  argument = scalar_expression_child(unit, call, 1u);
  return for_expect_terminal_reference(
      unit, argument, CTOOL_C_EXPRESSION_BLOCK_BINDING, binding);
}

static int validate_for_statement_unit(
    const ctool_c_translation_unit_t *unit) {
  static const ctool_c_statement_kind_t expected_kinds[] = {
      CTOOL_C_STATEMENT_DECLARATION, CTOOL_C_STATEMENT_DECLARATION,
      CTOOL_C_STATEMENT_DECLARATION, CTOOL_C_STATEMENT_DECLARATION,
      CTOOL_C_STATEMENT_EXPRESSION,  CTOOL_C_STATEMENT_CONTINUE,
      CTOOL_C_STATEMENT_COMPOUND,    CTOOL_C_STATEMENT_FOR,
      CTOOL_C_STATEMENT_DECLARATION, CTOOL_C_STATEMENT_DECLARATION,
      CTOOL_C_STATEMENT_EXPRESSION,  CTOOL_C_STATEMENT_BREAK,
      CTOOL_C_STATEMENT_COMPOUND,    CTOOL_C_STATEMENT_FOR,
      CTOOL_C_STATEMENT_EXPRESSION,  CTOOL_C_STATEMENT_EXPRESSION,
      CTOOL_C_STATEMENT_FOR,         CTOOL_C_STATEMENT_BREAK,
      CTOOL_C_STATEMENT_FOR,         CTOOL_C_STATEMENT_BREAK,
      CTOOL_C_STATEMENT_FOR,         CTOOL_C_STATEMENT_BREAK,
      CTOOL_C_STATEMENT_FOR,         CTOOL_C_STATEMENT_BREAK,
      CTOOL_C_STATEMENT_FOR,         CTOOL_C_STATEMENT_COMPOUND};
  static const ctool_u32 expected_outer_children[] = {
      0u, 1u, 2u, 3u, 7u, 13u, 14u, 16u, 18u, 20u, 22u, 24u};
  static const char *const binding_names[] = {
      "index", "inner", "keep", "values", "inner", "step", "inner"};
  static const ctool_u32 binding_lines[] = {3u, 4u, 5u, 6u, 10u, 10u, 11u};
  const ctool_c_function_definition_t *definition;
  const ctool_c_statement_t *first_for;
  const ctool_c_statement_t *declaration_for;
  const ctool_c_statement_t *pointer_for;
  const ctool_c_statement_t *volatile_for;
  const ctool_c_statement_t *array_for;
  const ctool_c_statement_t *function_for;
  const ctool_c_statement_t *empty_for;
  ctool_u32 pointer_parameter = CTOOL_C_AST_NONE;
  ctool_u32 observe_binding = find_binding_index(unit, "observe");
  ctool_u32 qualifiers = 0u;
  ctool_u32 terminal;
  ctool_u32 index;

  if (unit->function_definition_count != 1u ||
      unit->block_binding_count != ARRAY_COUNT(binding_names) ||
      unit->statement_count != ARRAY_COUNT(expected_kinds) ||
      unit->statement_child_count != 16u || unit->expression_count != 44u ||
      unit->expression_child_count != 27u ||
      observe_binding == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr, "for-statements: AST inventory differs\n");
    return 1;
  }
  definition = &unit->function_definitions[0];
  if (definition->body != 25u || definition->binding >= unit->binding_count ||
      !string_equal(unit->bindings[definition->binding].name, "counted") ||
      unit->statements[definition->body].kind != CTOOL_C_STATEMENT_COMPOUND ||
      unit->statements[definition->body].first_child != 4u ||
      unit->statements[definition->body].child_count !=
          ARRAY_COUNT(expected_outer_children)) {
    (void)fprintf(stderr, "for-statements: definition body differs\n");
    return 1;
  }
  for (index = 0u; index < ARRAY_COUNT(expected_kinds); index++) {
    const ctool_c_statement_t *statement = &unit->statements[index];
    if (statement->kind != expected_kinds[index] ||
        statement->location.path.data == NULL ||
        !string_equal(statement->location.path, "/for-statements.c") ||
        !string_equal(statement->physical_location.path,
                      "/for-statements.c")) {
      (void)fprintf(stderr, "for-statements: statement %u differs\n", index);
      return 1;
    }
  }
  for (index = 0u; index < ARRAY_COUNT(expected_outer_children); index++) {
    if (unit->statement_children[4u + index] !=
        expected_outer_children[index]) {
      (void)fprintf(stderr, "for-statements: outer child %u differs\n", index);
      return 1;
    }
  }
  if (unit->statement_children[0] != 5u ||
      unit->statement_children[1] != 9u ||
      unit->statement_children[2] != 10u ||
      unit->statement_children[3] != 11u) {
    (void)fprintf(stderr, "for-statements: nested children differ\n");
    return 1;
  }
  for (index = 0u; index < ARRAY_COUNT(binding_names); index++) {
    const ctool_c_block_binding_t *binding = &unit->block_bindings[index];
    if (!string_equal(binding->name, binding_names[index]) ||
        binding->location.line != binding_lines[index] ||
        binding->physical_location.line != binding_lines[index]) {
      (void)fprintf(stderr, "for-statements: binding %u differs\n", index);
      return 1;
    }
  }
  for (index = 0u; index < unit->parameter_count; index++) {
    if (string_equal(unit->parameters[index].name, "pointer") != 0) {
      pointer_parameter = index;
    }
  }
  if (pointer_parameter == CTOOL_C_AST_NONE ||
      for_expect_call_argument(unit, 10u, 6u) != 0 ||
      for_expect_call_argument(unit, 14u, 1u) != 0) {
    (void)fprintf(stderr, "for-statements: lexical shadowing differs\n");
    return 1;
  }

  first_for = &unit->statements[7];
  declaration_for = &unit->statements[13];
  pointer_for = &unit->statements[16];
  volatile_for = &unit->statements[18];
  array_for = &unit->statements[20];
  function_for = &unit->statements[22];
  empty_for = &unit->statements[24];
  if (declaration_for->condition >= unit->expression_count ||
      declaration_for->iteration >= unit->expression_count ||
      for_expect_terminal_reference(
          unit,
          scalar_expression_child(
              unit, &unit->expressions[declaration_for->condition], 0u),
          CTOOL_C_EXPRESSION_BLOCK_BINDING, 4u) != 0 ||
      for_expect_terminal_reference(
          unit,
          scalar_expression_child(
              unit, &unit->expressions[declaration_for->iteration], 0u),
          CTOOL_C_EXPRESSION_BLOCK_BINDING, 4u) != 0 ||
      for_expect_terminal_reference(
          unit,
          scalar_expression_child(
              unit, &unit->expressions[declaration_for->iteration], 1u),
          CTOOL_C_EXPRESSION_BLOCK_BINDING, 5u) != 0) {
    (void)fprintf(stderr, "for-statements: declaration loop scope differs\n");
    return 1;
  }
  if (first_for->initializer_statement != 4u || first_for->condition != 9u ||
      first_for->iteration != 11u || first_for->body != 6u ||
      declaration_for->initializer_statement != 8u ||
      declaration_for->condition != 18u ||
      declaration_for->iteration != 22u || declaration_for->body != 12u ||
      pointer_for->initializer_statement != CTOOL_C_AST_NONE ||
      pointer_for->condition != 35u || pointer_for->iteration != 37u ||
      pointer_for->body != 15u ||
      unit->statements[15].expression != CTOOL_C_AST_NONE ||
      volatile_for->condition != 39u ||
      volatile_for->iteration != CTOOL_C_AST_NONE ||
      volatile_for->body != 17u || array_for->condition != 41u ||
      array_for->body != 19u || function_for->condition != 43u ||
      function_for->body != 21u ||
      empty_for->initializer_statement != CTOOL_C_AST_NONE ||
      empty_for->condition != CTOOL_C_AST_NONE ||
      empty_for->iteration != CTOOL_C_AST_NONE || empty_for->body != 23u) {
    (void)fprintf(stderr, "for-statements: for-node payload differs\n");
    return 1;
  }
  if (unit->expressions[first_for->condition].kind !=
          CTOOL_C_EXPRESSION_BINARY ||
      unit->expressions[first_for->condition].operation !=
          CTOOL_C_EXPRESSION_OPERATOR_LESS ||
      unit->expressions[first_for->iteration].kind !=
          CTOOL_C_EXPRESSION_UPDATE ||
      unit->expressions[first_for->iteration].operation !=
          CTOOL_C_EXPRESSION_OPERATOR_POSTFIX_INCREMENT ||
      unit->expressions[pointer_for->iteration].kind !=
          CTOOL_C_EXPRESSION_UPDATE ||
      unit->expressions[pointer_for->iteration].operation !=
          CTOOL_C_EXPRESSION_OPERATOR_POSTFIX_INCREMENT) {
    (void)fprintf(stderr, "for-statements: typed clauses differ\n");
    return 1;
  }
  if (for_expect_terminal_reference(unit, pointer_for->condition,
                                    CTOOL_C_EXPRESSION_PARAMETER,
                                    pointer_parameter) != 0 ||
      scalar_conversion_chain_has(unit, pointer_for->condition,
                                  CTOOL_C_CONVERSION_LVALUE_TO_VALUE) ==
          CTOOL_FALSE ||
      for_expect_terminal_reference(unit, volatile_for->condition,
                                    CTOOL_C_EXPRESSION_BLOCK_BINDING, 2u) !=
          0 ||
      scalar_conversion_chain_has(unit, volatile_for->condition,
                                  CTOOL_C_CONVERSION_LVALUE_TO_VALUE) ==
          CTOOL_FALSE ||
      for_expect_terminal_reference(unit, array_for->condition,
                                    CTOOL_C_EXPRESSION_BLOCK_BINDING, 3u) !=
          0 ||
      scalar_conversion_chain_has(unit, array_for->condition,
                                  CTOOL_C_CONVERSION_ARRAY_TO_POINTER) ==
          CTOOL_FALSE ||
      for_expect_terminal_reference(unit, function_for->condition,
                                    CTOOL_C_EXPRESSION_IDENTIFIER,
                                    observe_binding) != 0 ||
      scalar_conversion_chain_has(unit, function_for->condition,
                                  CTOOL_C_CONVERSION_FUNCTION_TO_POINTER) ==
          CTOOL_FALSE) {
    (void)fprintf(stderr, "for-statements: condition conversions differ\n");
    return 1;
  }
  terminal = scalar_unwrap_conversions(unit, volatile_for->condition);
  if (terminal >= unit->expression_count ||
      scalar_type_kind(unit, unit->expressions[terminal].type, &qualifiers) !=
          CTOOL_C_TYPE_SIGNED_INT ||
      (qualifiers & CTOOL_C_QUAL_VOLATILE) == 0u ||
      scalar_type_kind(unit, unit->expressions[volatile_for->condition].type,
                       &qualifiers) != CTOOL_C_TYPE_SIGNED_INT ||
      qualifiers != 0u) {
    (void)fprintf(stderr, "for-statements: volatile condition differs\n");
    return 1;
  }
  return 0;
}

static int validate_nested_for_statement_unit(
    const ctool_c_translation_unit_t *unit) {
  const ctool_c_statement_t *inner;
  const ctool_c_statement_t *outer;
  if (unit->function_definition_count != 1u ||
      unit->block_binding_count != 2u || unit->statement_count != 9u ||
      unit->statement_child_count != 4u ||
      unit->function_definitions[0].body != 8u ||
      unit->statements[2].kind != CTOOL_C_STATEMENT_CONTINUE ||
      unit->statements[3].kind != CTOOL_C_STATEMENT_COMPOUND ||
      unit->statements[4].kind != CTOOL_C_STATEMENT_FOR ||
      unit->statements[5].kind != CTOOL_C_STATEMENT_BREAK ||
      unit->statements[6].kind != CTOOL_C_STATEMENT_COMPOUND ||
      unit->statements[7].kind != CTOOL_C_STATEMENT_FOR ||
      unit->statements[8].kind != CTOOL_C_STATEMENT_COMPOUND ||
      unit->statement_children[0] != 2u ||
      unit->statement_children[1] != 4u ||
      unit->statement_children[2] != 5u ||
      unit->statement_children[3] != 7u) {
    (void)fprintf(stderr, "for-statements: nested loop inventory differs\n");
    return 1;
  }
  inner = &unit->statements[4];
  outer = &unit->statements[7];
  if (inner->initializer_statement != 1u ||
      inner->condition == CTOOL_C_AST_NONE ||
      inner->iteration == CTOOL_C_AST_NONE || inner->body != 3u ||
      outer->initializer_statement != 0u ||
      outer->condition == CTOOL_C_AST_NONE ||
      outer->iteration == CTOOL_C_AST_NONE || outer->body != 6u ||
      inner->body >= 4u || inner->initializer_statement >= 4u ||
      outer->body >= 7u || outer->initializer_statement >= 7u) {
    (void)fprintf(stderr, "for-statements: nested loop postorder differs\n");
    return 1;
  }
  return 0;
}

static char *build_for_statement_depth_source(ctool_u32 depth) {
  size_t capacity = (size_t)depth * 9u + 32u;
  size_t used = 0u;
  char *source = (char *)malloc(capacity);
  ctool_u32 index;
  if (source == NULL) {
    return NULL;
  }
  source[0] = '\0';
  if (append_scale_text(source, capacity, &used, "void deep(void) { ") != 0) {
    free(source);
    return NULL;
  }
  for (index = 0u; index < depth; index++) {
    if (append_scale_text(source, capacity, &used, "for (;;) ") != 0) {
      free(source);
      return NULL;
    }
  }
  if (append_scale_text(source, capacity, &used, "; }\n") != 0) {
    free(source);
    return NULL;
  }
  return source;
}

static char *build_for_statement_limit_source(ctool_bool loops) {
  const size_t capacity = 8192u;
  size_t used = 0u;
  char *source = (char *)malloc(capacity);
  ctool_u32 index;
  if (source == NULL) {
    return NULL;
  }
  source[0] = '\0';
  if (append_scale_text(source, capacity, &used,
                        "void for_limit(void) {\n") != 0) {
    free(source);
    return NULL;
  }
  for (index = 0u; index < 128u; index++) {
    if (append_scale_text(source, capacity, &used,
                          loops == CTOOL_TRUE ? "  for (;;);\n" : "  ;\n") !=
        0) {
      free(source);
      return NULL;
    }
  }
  if (append_scale_text(source, capacity, &used, "}\n") != 0) {
    free(source);
    return NULL;
  }
  return source;
}

static int validate_for_statement_storage_limit(
    frontend_fixture_t *fixture, const char *host_root) {
  char *control_source = build_for_statement_limit_source(CTOOL_FALSE);
  char *loop_source = build_for_statement_limit_source(CTOOL_TRUE);
  ctool_c_translation_unit_t control_oracle;
  ctool_c_translation_unit_t loop_oracle;
  ctool_c_translation_unit_t control;
  ctool_c_translation_unit_t failed_unit;
  ctool_c_translation_unit_t recovered;
  ctool_c_pp_result_t control_tape;
  ctool_c_pp_result_t loop_tape;
  ctool_c_pp_token_t *snapshot = NULL;
  ctool_limits_t limits = ctool_default_limits();
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_arena_mark_t mark;
  const ctool_diagnostic_t *diagnostic;
  ctool_status_t status;
  ctool_u32 output_limit =
      256u * (ctool_u32)sizeof(ctool_c_statement_t);
  size_t token_bytes;
  int failed = 1;

  if (control_source == NULL || loop_source == NULL ||
      parse_valid_fixture(fixture, "/for-limit-control.c", control_source,
                          &control_oracle) != 0 ||
      parse_valid_fixture(fixture, "/for-limit-success.c", loop_source,
                          &loop_oracle) != 0 ||
      control_oracle.statement_count != 129u ||
      loop_oracle.statement_count != 257u ||
      (ctool_u64)control_oracle.statement_count *
              sizeof(ctool_c_statement_t) >
          output_limit ||
      (ctool_u64)loop_oracle.statement_count *
              sizeof(ctool_c_statement_t) <=
          output_limit ||
      preprocess_fixture(fixture, "/for-limit-control.c", control_source,
                         &control_tape) != 0 ||
      preprocess_fixture(fixture, "/for-limit.c", loop_source, &loop_tape) !=
          0) {
    (void)fprintf(stderr, "for-statements: storage-limit controls differ\n");
    goto cleanup;
  }
  token_bytes = (size_t)loop_tape.token_count * sizeof(*snapshot);
  snapshot = (ctool_c_pp_token_t *)malloc(token_bytes);
  if (snapshot == NULL) {
    goto cleanup;
  }
  (void)memcpy(snapshot, loop_tape.tokens, token_bytes);
  limits.output_bytes = output_limit;
  status = ctool_host_adapter_init(&adapter, host_root);
  if (status != CTOOL_OK) {
    goto cleanup;
  }
  config = ctool_host_job_config(&adapter, limits);
  status = ctool_job_open(&config, &job);
  if (status != CTOOL_OK) {
    goto cleanup;
  }
  (void)memset(&control, 0xa5, sizeof(control));
  status = ctool_c_parse(job, &control_tape, &fixture->parse_request, &control);
  if (status != CTOOL_OK || control.statement_count != 129u ||
      control.statements[0].kind != CTOOL_C_STATEMENT_EXPRESSION ||
      control.statements[0].expression != CTOOL_C_AST_NONE) {
    (void)fprintf(stderr,
                  "for-statements: limited control failed: %s/%u\n",
                  ctool_status_name(status), (unsigned int)output_limit);
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  (void)memset(&failed_unit, 0xa5, sizeof(failed_unit));
  status = ctool_c_parse(job, &loop_tape, &fixture->parse_request,
                         &failed_unit);
  diagnostic = ctool_job_diagnostic(job, 0u);
  if (status != CTOOL_ERR_LIMIT || unit_is_zero(&failed_unit) == 0 ||
      ctool_job_diagnostic_count(job) != 1u || diagnostic == NULL ||
      diagnostic->code != CTOOL_C_PARSE_DIAG_LIMIT ||
      !string_equal(diagnostic->path, "/for-limit.c") ||
      diagnostic->line == 0u || diagnostic->column == 0u ||
      !string_equal(diagnostic->message,
                    "declaration frontend storage limit exceeded") ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      memcmp(snapshot, loop_tape.tokens, token_bytes) != 0 ||
      control.statement_count != 129u ||
      control.statements[0].expression != CTOOL_C_AST_NONE) {
    (void)fprintf(stderr,
                  "for-statements: limited rollback differs: %s/%u\n",
                  ctool_status_name(status), (unsigned int)output_limit);
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  (void)memset(&recovered, 0xa5, sizeof(recovered));
  status =
      ctool_c_parse(job, &control_tape, &fixture->parse_request, &recovered);
  if (status != CTOOL_OK || recovered.statement_count != 129u ||
      recovered.statements[0].expression != CTOOL_C_AST_NONE ||
      control.statement_count != 129u ||
      ctool_job_diagnostic_count(job) != 1u) {
    (void)fprintf(stderr, "for-statements: limited recovery differs\n");
    goto cleanup;
  }
  failed = 0;

cleanup:
  if (job != NULL) {
    ctool_job_close(job);
  }
  free(snapshot);
  free(loop_source);
  free(control_source);
  return failed;
}

static int run_for_statements(const char *host_root) {
  static const char source[] =
      "int observe(int value);\n"
      "void counted(int limit, int *pointer) {\n"
      "  int index;\n"
      "  int inner = 7;\n"
      "  volatile int keep = 1;\n"
      "  int values[1];\n"
      "  for (index = 0; index < limit; index++) {\n"
      "    continue;\n"
      "  }\n"
      "  for (int inner = 0, step = 1; inner < limit; inner += step) {\n"
      "    int inner = 2;\n"
      "    observe(inner);\n"
      "    break;\n"
      "  }\n"
      "  observe(inner);\n"
      "  for (; pointer; pointer++) ;\n"
      "  for (; keep; ) break;\n"
      "  for (; values; ) break;\n"
      "  for (; observe; ) break;\n"
      "  for (;;) break;\n"
      "}\n";
  static const char storage_source[] =
      "void register_loop(int limit) {\n"
      "  for (register int index = 0; index < limit; index++) break;\n"
      "}\n"
      "void auto_loop(int limit) {\n"
      "  for (auto int index = 0; index < limit; index++) break;\n"
      "}\n"
      "void parameter_shadow(int value) {\n"
      "  for (int value = 0; value < 1; value++) break;\n"
      "}\n";
  static const char nested_source[] =
      "void nested(int outer_limit, int inner_limit) {\n"
      "  for (int outer = 0; outer < outer_limit; outer++) {\n"
      "    for (int inner = 0; inner < inner_limit; inner++) {\n"
      "      continue;\n"
      "    }\n"
      "    break;\n"
      "  }\n"
      "}\n";
  static const frontend_exact_failure_case_t failure_cases[] = {
      {{"break outside loop", "void bad(void) { break; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_STATEMENT},
       1u, 18u, "break statement requires an enclosing loop or switch"},
      {{"continue outside loop", "void bad(void) { continue; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_STATEMENT},
       1u, 18u, "continue statement requires an enclosing loop"},
      {{"aggregate controlling expression",
        "typedef struct { int value; } item_t;\n"
        "void bad(void) {\n"
        "  item_t value;\n"
        "  for (; value; ) { }\n"
        "}\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       4u, 10u, "controlling expression requires scalar type"},
      {{"void controlling expression",
        "void sink(void);\n"
        "void bad(void) {\n"
        "  for (; sink(); ) { }\n"
        "}\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       3u, 10u, "controlling expression requires scalar type"},
      {{"floating controlling expression",
        "void bad(double value) {\n"
        "  for (; value; ) { }\n"
        "}\n",
        CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_EXPRESSION},
       2u, 10u,
       "floating controlling expressions are outside this body slice"},
      {{"declaration is not a loop body",
        "void bad(void) {\n"
        "  for (;;) int value;\n"
        "}\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_STATEMENT},
       2u, 12u, "declaration is not a statement; use a compound statement"},
      {{"expired loop initializer",
        "void bad(void) {\n"
        "  for (int index = 0; index < 1; index++) { }\n"
        "  index;\n"
        "}\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       3u, 3u, "expression identifier is not declared"},
      {{"static for initializer",
        "void bad(void) {\n"
        "  for (static int index = 0; index < 1; index++) { }\n"
        "}\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_STATEMENT},
       2u, 8u,
       "for initializer declaration requires automatic or register storage"},
      {{"extern for initializer",
        "void bad(void) {\n"
        "  for (extern int index; index < 1; index++) { }\n"
        "}\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_STATEMENT},
       2u, 8u,
       "for initializer declaration requires automatic or register storage"},
      {{"typedef for initializer",
        "void bad(void) {\n"
        "  for (typedef int item; ; ) { }\n"
        "}\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_STATEMENT},
       2u, 8u,
       "for initializer declaration requires automatic or register storage"},
      {{"function for initializer",
        "void bad(void) {\n"
        "  for (int callback(void); ; ) { }\n"
        "}\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_STATEMENT},
       2u, 12u, "for initializer cannot declare a function"},
      {{"function specifier for initializer",
        "void bad(void) {\n"
        "  for (inline int index = 0; ; ) { }\n"
        "}\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_STATEMENT},
       2u, 8u, "for initializer cannot use a function specifier"},
      {{"static assertion for initializer boundary",
        "void bad(void) {\n"
        "  for (_Static_assert(1, \"ok\"); ; ) { }\n"
        "}\n",
        CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_STATEMENT},
       2u, 8u,
       "for initializer static assertions are outside this body slice"},
      {{"missing for opening parenthesis",
        "void bad(void) {\n"
        "  int index;\n"
        "  for index = 0; index < 1; index++) { }\n"
        "}\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPECTED_TOKEN},
       3u, 7u, "for statement requires an opening parenthesis"},
      {{"missing initializer semicolon",
        "void bad(void) {\n"
        "  int index;\n"
        "  for (index = 0 index < 1; index++) { }\n"
        "}\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPECTED_TOKEN},
       3u, 18u, "for initializer requires a semicolon"},
      {{"missing condition semicolon",
        "void bad(void) {\n"
        "  int index;\n"
        "  for (index = 0; index < 1 index++) { }\n"
        "}\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPECTED_TOKEN},
       3u, 29u, "for controlling expression requires a semicolon"},
      {{"missing break semicolon",
        "void bad(void) {\n"
        "  for (;;) { break }\n"
        "}\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPECTED_TOKEN},
       2u, 20u, "break statement requires a semicolon"},
      {{"comma initializer boundary",
        "void bad(void) {\n"
        "  int index;\n"
        "  for (index = 0, index = 1; index < 2; index++) { }\n"
        "}\n",
        CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_EXPRESSION},
       3u, 17u,
       "expression operator is outside this function-body slice"}};
  frontend_fixture_t fixture;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t storage_unit;
  ctool_c_translation_unit_t nested_unit;
  char *depth_source = NULL;
  ctool_u32 index;
  int failed = 1;

  if (begin_frontend_fixture(&fixture, "for-statements", host_root,
                             8u * 1024u * 1024u) != 0) {
    return 1;
  }
  fixture.pp_request.gnu_extensions = CTOOL_FALSE;
  fixture.parse_request.gnu_extensions = CTOOL_FALSE;
  if (parse_valid_fixture(&fixture, "/for-statements.c", source, &unit) !=
          0 ||
      validate_for_statement_unit(&unit) != 0) {
    goto cleanup;
  }
  if (parse_valid_fixture(&fixture, "/for-storage.c", storage_source,
                          &storage_unit) != 0 ||
      storage_unit.function_definition_count != 3u ||
      storage_unit.block_binding_count != 3u ||
      storage_unit.statement_count != 12u ||
      storage_unit.block_bindings[0].storage != CTOOL_C_STORAGE_REGISTER ||
      storage_unit.block_bindings[1].storage != CTOOL_C_STORAGE_AUTO ||
      storage_unit.block_bindings[2].storage != CTOOL_C_STORAGE_NONE ||
      !string_equal(storage_unit.block_bindings[2].name, "value") ||
      storage_unit.statements[10].kind != CTOOL_C_STATEMENT_FOR ||
      storage_unit.statements[10].condition >=
          storage_unit.expression_count ||
      storage_unit.statements[10].iteration >=
          storage_unit.expression_count) {
    (void)fprintf(stderr,
                  "for-statements: initializer storage or shadowing differs\n");
    goto cleanup;
  }
  if (for_expect_terminal_reference(
          &storage_unit,
          scalar_expression_child(
              &storage_unit,
              &storage_unit.expressions[storage_unit.statements[10].condition],
              0u),
          CTOOL_C_EXPRESSION_BLOCK_BINDING, 2u) != 0 ||
      for_expect_terminal_reference(
          &storage_unit,
          scalar_expression_child(
              &storage_unit,
              &storage_unit.expressions[storage_unit.statements[10].iteration],
              0u),
          CTOOL_C_EXPRESSION_BLOCK_BINDING, 2u) != 0) {
    (void)fprintf(stderr,
                  "for-statements: parameter loop shadowing differs\n");
    goto cleanup;
  }
  if (parse_valid_fixture(&fixture, "/for-nested.c", nested_source,
                          &nested_unit) != 0 ||
      validate_nested_for_statement_unit(&nested_unit) != 0) {
    goto cleanup;
  }
  for (index = 0u; index < ARRAY_COUNT(failure_cases); index++) {
    const frontend_exact_failure_case_t *test_case = &failure_cases[index];
    if (expect_frontend_failure_at_message(
            &fixture, &test_case->failure, "/for-statement-failure.c",
            test_case->line, test_case->column, test_case->message) != 0 ||
        validate_for_statement_unit(&unit) != 0) {
      goto cleanup;
    }
  }
  depth_source = build_for_statement_depth_source(256u);
  if (depth_source == NULL) {
    (void)fprintf(stderr, "for-statements: depth source construction failed\n");
    goto cleanup;
  }
  {
    const frontend_failure_case_t depth_failure = {
        "nested for statement limit", depth_source, CTOOL_ERR_LIMIT,
        CTOOL_C_PARSE_DIAG_LIMIT};
    if (expect_frontend_failure_at_message(
            &fixture, &depth_failure, "/for-statement-depth.c", 1u, 2314u,
            "source syntax exceeds the public nesting limit") != 0 ||
        validate_for_statement_unit(&unit) != 0) {
      goto cleanup;
    }
  }
  if (validate_toolchain_for_frontier(host_root) != 0 ||
      validate_for_statement_storage_limit(&fixture, host_root) != 0 ||
      validate_for_statement_unit(&unit) != 0) {
    goto cleanup;
  }
  failed = 0;

cleanup:
  free(depth_source);
  if (finish_frontend_fixture(&fixture) != 0) {
    failed = 1;
  }
  if (failed == 0) {
    (void)printf("for-statements: ok\n");
  }
  return failed;
}

static const ctool_c_function_definition_t *
find_function_definition(const ctool_c_translation_unit_t *unit,
                         const char *name) {
  ctool_u32 binding = find_binding_index(unit, name);
  ctool_u32 index;
  if (binding == CTOOL_C_AST_NONE) {
    return NULL;
  }
  for (index = 0u; index < unit->function_definition_count; index++) {
    if (unit->function_definitions[index].binding == binding) {
      return &unit->function_definitions[index];
    }
  }
  return NULL;
}

static ctool_u32 scalar_expression_child(
    const ctool_c_translation_unit_t *unit,
    const ctool_c_expression_t *expression, ctool_u32 child) {
  if (expression == NULL || child >= expression->child_count ||
      expression->first_child > unit->expression_child_count ||
      expression->child_count >
          unit->expression_child_count - expression->first_child) {
    return CTOOL_C_AST_NONE;
  }
  child = unit->expression_children[expression->first_child + child];
  return child < unit->expression_count ? child : CTOOL_C_AST_NONE;
}

static ctool_u32 scalar_unwrap_conversions(
    const ctool_c_translation_unit_t *unit, ctool_u32 expression) {
  ctool_u32 traversed = 0u;
  while (expression < unit->expression_count &&
         unit->expressions[expression].kind ==
             CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION) {
    expression = scalar_expression_child(unit, &unit->expressions[expression],
                                         0u);
    if (expression == CTOOL_C_AST_NONE ||
        traversed++ >= unit->expression_count) {
      return CTOOL_C_AST_NONE;
    }
  }
  return expression < unit->expression_count ? expression : CTOOL_C_AST_NONE;
}

static ctool_bool scalar_conversion_chain_has(
    const ctool_c_translation_unit_t *unit, ctool_u32 expression,
    ctool_c_conversion_kind_t conversion) {
  ctool_u32 traversed = 0u;
  while (expression < unit->expression_count &&
         unit->expressions[expression].kind ==
             CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION) {
    const ctool_c_expression_t *node = &unit->expressions[expression];
    if (node->conversion == conversion) {
      return CTOOL_TRUE;
    }
    expression = scalar_expression_child(unit, node, 0u);
    if (expression == CTOOL_C_AST_NONE ||
        traversed++ >= unit->expression_count) {
      break;
    }
  }
  return CTOOL_FALSE;
}

static ctool_c_type_kind_t scalar_type_kind(
    const ctool_c_translation_unit_t *unit, ctool_u32 type,
    ctool_u32 *qualifiers_out) {
  const ctool_c_type_node_t *node = type_node(unit, type);
  ctool_u32 qualifiers = 0u;
  ctool_u32 traversed = 0u;
  while (node != NULL &&
         (node->kind == CTOOL_C_TYPE_QUALIFIED ||
          node->kind == CTOOL_C_TYPE_ALIGNED)) {
    qualifiers |= node->qualifiers;
    node = type_node(unit, node->referenced_type);
    if (traversed++ >= unit->graph.type_count) {
      node = NULL;
      break;
    }
  }
  if (qualifiers_out != NULL) {
    *qualifiers_out = qualifiers;
  }
  return node == NULL ? (ctool_c_type_kind_t)0 : node->kind;
}

static const ctool_c_statement_t *scalar_return_statement(
    const ctool_c_translation_unit_t *unit, const char *function_name) {
  const ctool_c_function_definition_t *definition =
      find_function_definition(unit, function_name);
  const ctool_c_statement_t *body;
  const ctool_c_statement_t *result = NULL;
  ctool_u32 index;
  if (definition == NULL || definition->body >= unit->statement_count) {
    return NULL;
  }
  body = &unit->statements[definition->body];
  if (body->kind != CTOOL_C_STATEMENT_COMPOUND ||
      body->first_child > unit->statement_child_count ||
      body->child_count > unit->statement_child_count - body->first_child) {
    return NULL;
  }
  for (index = 0u; index < body->child_count; index++) {
    ctool_u32 child = unit->statement_children[body->first_child + index];
    if (child >= unit->statement_count) {
      return NULL;
    }
    if (unit->statements[child].kind == CTOOL_C_STATEMENT_RETURN) {
      if (result != NULL) {
        return NULL;
      }
      result = &unit->statements[child];
    }
  }
  return result;
}

static ctool_u32 scalar_return_value(
    const ctool_c_translation_unit_t *unit, const char *function_name,
    ctool_c_type_kind_t expected_result) {
  const ctool_c_statement_t *statement =
      scalar_return_statement(unit, function_name);
  const ctool_c_expression_t *conversion;
  if (statement == NULL || statement->expression >= unit->expression_count) {
    return CTOOL_C_AST_NONE;
  }
  conversion = &unit->expressions[statement->expression];
  if (scalar_type_kind(unit, conversion->type, NULL) != expected_result) {
    return CTOOL_C_AST_NONE;
  }
  if (conversion->kind == CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION &&
      conversion->conversion == CTOOL_C_CONVERSION_ASSIGNMENT &&
      conversion->child_count == 1u) {
    return scalar_expression_child(unit, conversion, 0u);
  }
  return statement->expression;
}

static int scalar_expect_operator(const ctool_c_translation_unit_t *unit,
                                  ctool_u32 expression,
                                  ctool_c_expression_kind_t kind,
                                  ctool_c_expression_operator_t operation,
                                  ctool_u32 child_count) {
  expression = scalar_unwrap_conversions(unit, expression);
  return expression != CTOOL_C_AST_NONE &&
                 unit->expressions[expression].kind == kind &&
                 unit->expressions[expression].operation == operation &&
                 unit->expressions[expression].child_count == child_count
             ? 0
             : 1;
}

static ctool_u32 scalar_operator_child(
    const ctool_c_translation_unit_t *unit, ctool_u32 expression,
    ctool_u32 child) {
  expression = scalar_unwrap_conversions(unit, expression);
  return expression == CTOOL_C_AST_NONE
             ? CTOOL_C_AST_NONE
             : scalar_expression_child(unit, &unit->expressions[expression],
                                       child);
}

static ctool_u32 scalar_count_integer_constants(
    const ctool_c_translation_unit_t *unit, ctool_u32 line, ctool_u64 bits,
    ctool_c_type_kind_t kind) {
  ctool_u32 count = 0u;
  ctool_u32 index;
  for (index = 0u; index < unit->expression_count; index++) {
    const ctool_c_expression_t *expression = &unit->expressions[index];
    if (expression->kind == CTOOL_C_EXPRESSION_INTEGER_CONSTANT &&
        expression->integer_bits == bits &&
        scalar_type_kind(unit, expression->type, NULL) == kind &&
        expression->location.line == line &&
        string_equal(expression->location.path, "/scalar-returns.c") != 0 &&
        expression->physical_location.line == line &&
        string_equal(expression->physical_location.path,
                     "/scalar-returns.c") != 0) {
      count++;
    }
  }
  return count;
}

static char *build_scalar_operator_chain(ctool_u32 operator_count) {
  const size_t capacity = (size_t)operator_count * 4u + 80u;
  char *source = (char *)malloc(capacity);
  size_t used = 0u;
  ctool_u32 index;
  if (source == NULL) {
    return NULL;
  }
  source[0] = '\0';
  if (append_scale_text(source, capacity, &used,
                        "int long_chain(void) { return 0") != 0) {
    free(source);
    return NULL;
  }
  for (index = 0u; index < operator_count; index++) {
    if (append_scale_text(source, capacity, &used, " + 1") != 0) {
      free(source);
      return NULL;
    }
  }
  if (append_scale_text(source, capacity, &used, "; }\n") != 0) {
    free(source);
    return NULL;
  }
  return source;
}

static int validate_scalar_operator_chain(
    const ctool_c_translation_unit_t *unit, ctool_u32 operator_count) {
  const ctool_c_statement_t *statement =
      scalar_return_statement(unit, "long_chain");
  ctool_u32 expression;
  ctool_u32 index;
  if (unit->function_definition_count != 1u || unit->statement_count != 2u ||
      unit->statement_child_count != 1u ||
      unit->expression_count != operator_count * 2u + 1u ||
      unit->expression_child_count != operator_count * 2u ||
      statement == NULL || statement->expression >= unit->expression_count) {
    (void)fprintf(stderr, "scalar-returns: long-chain inventory differs\n");
    return 1;
  }
  expression = statement->expression;
  for (index = 0u; index < operator_count; index++) {
    const ctool_c_expression_t *node;
    const ctool_c_expression_t *right;
    ctool_u32 left_index;
    ctool_u32 right_index;
    expression = scalar_unwrap_conversions(unit, expression);
    if (expression == CTOOL_C_AST_NONE) {
      (void)fprintf(stderr, "scalar-returns: long-chain root is invalid\n");
      return 1;
    }
    node = &unit->expressions[expression];
    left_index = scalar_expression_child(unit, node, 0u);
    right_index = scalar_expression_child(unit, node, 1u);
    right = right_index < unit->expression_count
                ? &unit->expressions[right_index]
                : NULL;
    if (node->kind != CTOOL_C_EXPRESSION_BINARY ||
        node->operation != CTOOL_C_EXPRESSION_OPERATOR_ADD ||
        node->child_count != 2u || left_index >= expression ||
        right_index >= expression || right == NULL ||
        right->kind != CTOOL_C_EXPRESSION_INTEGER_CONSTANT ||
        right->integer_bits != 1ull ||
        scalar_type_kind(unit, right->type, NULL) !=
            CTOOL_C_TYPE_SIGNED_INT) {
      (void)fprintf(stderr,
                    "scalar-returns: long-chain reduction %u differs\n",
                    index);
      return 1;
    }
    expression = left_index;
  }
  expression = scalar_unwrap_conversions(unit, expression);
  if (expression == CTOOL_C_AST_NONE ||
      unit->expressions[expression].kind !=
          CTOOL_C_EXPRESSION_INTEGER_CONSTANT ||
      unit->expressions[expression].integer_bits != 0ull) {
    (void)fprintf(stderr, "scalar-returns: long-chain left seed differs\n");
    return 1;
  }
  return 0;
}

typedef struct {
  ctool_allocator_t base;
  ctool_u64 outstanding_bytes;
  ctool_u32 invalid_releases;
} scalar_tracking_allocator_t;

static void *scalar_tracking_allocate(void *context, ctool_u32 bytes) {
  scalar_tracking_allocator_t *tracking =
      (scalar_tracking_allocator_t *)context;
  void *allocation = tracking->base.allocate(tracking->base.context, bytes);
  if (allocation != NULL) {
    tracking->outstanding_bytes += (ctool_u64)bytes;
  }
  return allocation;
}

static void scalar_tracking_release(void *context, void *allocation,
                                    ctool_u32 bytes) {
  scalar_tracking_allocator_t *tracking =
      (scalar_tracking_allocator_t *)context;
  if (allocation != NULL) {
    if (tracking->outstanding_bytes < (ctool_u64)bytes) {
      tracking->invalid_releases++;
    } else {
      tracking->outstanding_bytes -= (ctool_u64)bytes;
    }
  }
  tracking->base.release(tracking->base.context, allocation, bytes);
}

static int validate_scalar_operator_storage_limit(
    frontend_fixture_t *fixture, const char *host_root,
    const char *chain_source) {
  static const char anchor_source[] =
      "typedef unsigned int scalar_chain_anchor_t;\n";
  ctool_limits_t limits = ctool_default_limits();
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  scalar_tracking_allocator_t tracking;
  ctool_c_pp_result_t chain_tape;
  ctool_c_pp_result_t anchor_tape;
  ctool_c_pp_token_t *snapshot = NULL;
  ctool_c_translation_unit_t anchor;
  ctool_c_translation_unit_t failed_unit;
  ctool_c_translation_unit_t recovered;
  ctool_arena_mark_t mark;
  const ctool_diagnostic_t *diagnostic;
  const ctool_c_binding_t *anchor_binding;
  ctool_status_t status;
  ctool_u64 outstanding_before_failure;
  size_t token_bytes;
  int failed = 1;

  if (preprocess_fixture(fixture, "/scalar-chain-limit.c", chain_source,
                         &chain_tape) != 0 ||
      preprocess_fixture(fixture, "/scalar-chain-anchor.c", anchor_source,
                         &anchor_tape) != 0) {
    return 1;
  }
  token_bytes = (size_t)chain_tape.token_count * sizeof(*snapshot);
  snapshot = (ctool_c_pp_token_t *)malloc(token_bytes);
  if (snapshot == NULL) {
    return 1;
  }
  (void)memcpy(snapshot, chain_tape.tokens, token_bytes);
  limits.output_bytes = 256u;
  status = ctool_host_adapter_init(&adapter, host_root);
  if (status != CTOOL_OK) {
    goto cleanup;
  }
  config = ctool_host_job_config(&adapter, limits);
  (void)memset(&tracking, 0, sizeof(tracking));
  tracking.base = config.allocator;
  config.allocator.context = &tracking;
  config.allocator.allocate = scalar_tracking_allocate;
  config.allocator.release = scalar_tracking_release;
  status = ctool_job_open(&config, &job);
  if (status != CTOOL_OK) {
    goto cleanup;
  }
  (void)memset(&anchor, 0xa5, sizeof(anchor));
  status = ctool_c_parse(job, &anchor_tape, &fixture->parse_request, &anchor);
  anchor_binding = find_binding(&anchor, "scalar_chain_anchor_t");
  if (status != CTOOL_OK || anchor_binding == NULL ||
      anchor_binding->kind != CTOOL_C_BINDING_TYPEDEF) {
    (void)fprintf(stderr, "scalar-returns: limited chain anchor failed\n");
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  outstanding_before_failure = tracking.outstanding_bytes;
  (void)memset(&failed_unit, 0xa5, sizeof(failed_unit));
  status = ctool_c_parse(job, &chain_tape, &fixture->parse_request,
                         &failed_unit);
  diagnostic = ctool_job_diagnostic(job, 0u);
  anchor_binding = find_binding(&anchor, "scalar_chain_anchor_t");
  if (status != CTOOL_ERR_LIMIT || unit_is_zero(&failed_unit) == 0 ||
      ctool_job_diagnostic_count(job) != 1u || diagnostic == NULL ||
      diagnostic->code != CTOOL_C_PARSE_DIAG_LIMIT ||
      !string_equal(diagnostic->path, "/scalar-chain-limit.c") ||
      diagnostic->line != 1u || diagnostic->column == 0u ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      tracking.outstanding_bytes != outstanding_before_failure ||
      tracking.invalid_releases != 0u ||
      memcmp(snapshot, chain_tape.tokens, token_bytes) != 0 ||
      anchor_binding == NULL ||
      anchor_binding->kind != CTOOL_C_BINDING_TYPEDEF) {
    (void)fprintf(
        stderr,
        "scalar-returns: chain scratch rollback differs: %s diagnostics=%u\n",
        ctool_status_name(status), ctool_job_diagnostic_count(job));
    goto cleanup;
  }
  (void)memset(&recovered, 0xa5, sizeof(recovered));
  status = ctool_c_parse(job, &anchor_tape, &fixture->parse_request,
                         &recovered);
  if (status != CTOOL_OK ||
      find_binding(&recovered, "scalar_chain_anchor_t") == NULL ||
      find_binding(&anchor, "scalar_chain_anchor_t") == NULL ||
      ctool_job_diagnostic_count(job) != 1u) {
    (void)fprintf(stderr,
                  "scalar-returns: limited chain recovery differs\n");
    goto cleanup;
  }
  failed = 0;

cleanup:
  if (job != NULL) {
    ctool_job_close(job);
    job = NULL;
    if (tracking.outstanding_bytes != 0ull ||
        tracking.invalid_releases != 0u) {
      (void)fprintf(stderr,
                    "scalar-returns: chain scratch allocation leaked\n");
      failed = 1;
    }
  }
  free(snapshot);
  return failed;
}

static int validate_scalar_update_storage_limit(
    frontend_fixture_t *fixture, const char *host_root) {
  static const char control_source[] =
      "unsigned char update_limit(unsigned char *value) { return *value; }\n";
  static const char update_source[] =
      "unsigned char update_limit(unsigned char *value) { return (*value)++; }\n";
  static const char anchor_source[] =
      "typedef unsigned char update_limit_anchor_t;\n";
  ctool_c_translation_unit_t control_unit;
  ctool_c_translation_unit_t update_unit;
  ctool_c_translation_unit_t anchor_unit;
  ctool_c_translation_unit_t failed_unit;
  ctool_c_translation_unit_t recovered_unit;
  ctool_c_pp_result_t update_tape;
  ctool_c_pp_result_t anchor_tape;
  ctool_limits_t limits = ctool_default_limits();
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  scalar_tracking_allocator_t tracking;
  ctool_c_pp_token_t *snapshot = NULL;
  ctool_arena_mark_t mark;
  const ctool_diagnostic_t *diagnostic;
  const ctool_c_binding_t *anchor_binding;
  ctool_status_t status;
  ctool_u64 outstanding_before_failure;
  size_t token_bytes;
  int failed = 1;

  if (parse_valid_fixture(fixture, "/scalar-update-limit-control.c",
                          control_source, &control_unit) != 0 ||
      parse_valid_fixture(fixture, "/scalar-update-limit-success.c",
                          update_source, &update_unit) != 0 ||
      update_unit.graph.type_count != control_unit.graph.type_count + 1u ||
      preprocess_fixture(fixture, "/scalar-update-limit.c", update_source,
                         &update_tape) != 0 ||
      preprocess_fixture(fixture, "/scalar-update-limit-anchor.c",
                         anchor_source, &anchor_tape) != 0 ||
      (size_t)control_unit.graph.type_count *
              sizeof(ctool_c_type_node_t) >
          0xffffffffu) {
    (void)fprintf(stderr, "scalar-updates: storage-limit control differs\n");
    goto cleanup;
  }
  limits.output_bytes =
      control_unit.graph.type_count * (ctool_u32)sizeof(ctool_c_type_node_t);
  token_bytes = (size_t)update_tape.token_count * sizeof(*snapshot);
  snapshot = (ctool_c_pp_token_t *)malloc(token_bytes);
  if (snapshot == NULL) {
    goto cleanup;
  }
  (void)memcpy(snapshot, update_tape.tokens, token_bytes);
  status = ctool_host_adapter_init(&adapter, host_root);
  if (status != CTOOL_OK) {
    goto cleanup;
  }
  config = ctool_host_job_config(&adapter, limits);
  (void)memset(&tracking, 0, sizeof(tracking));
  tracking.base = config.allocator;
  config.allocator.context = &tracking;
  config.allocator.allocate = scalar_tracking_allocate;
  config.allocator.release = scalar_tracking_release;
  status = ctool_job_open(&config, &job);
  if (status != CTOOL_OK) {
    goto cleanup;
  }
  (void)memset(&anchor_unit, 0xa5, sizeof(anchor_unit));
  status = ctool_c_parse(job, &anchor_tape, &fixture->parse_request,
                         &anchor_unit);
  anchor_binding = find_binding(&anchor_unit, "update_limit_anchor_t");
  if (status != CTOOL_OK || anchor_binding == NULL ||
      anchor_binding->kind != CTOOL_C_BINDING_TYPEDEF) {
    (void)fprintf(stderr, "scalar-updates: limited anchor failed\n");
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  outstanding_before_failure = tracking.outstanding_bytes;
  (void)memset(&failed_unit, 0xa5, sizeof(failed_unit));
  status = ctool_c_parse(job, &update_tape, &fixture->parse_request,
                         &failed_unit);
  diagnostic = ctool_job_diagnostic(job, 0u);
  anchor_binding = find_binding(&anchor_unit, "update_limit_anchor_t");
  if (status != CTOOL_ERR_LIMIT || unit_is_zero(&failed_unit) == 0 ||
      ctool_job_diagnostic_count(job) != 1u || diagnostic == NULL ||
      diagnostic->code != CTOOL_C_PARSE_DIAG_LIMIT ||
      !string_equal(diagnostic->path, "/scalar-update-limit.c") ||
      !string_equal(diagnostic->message,
                    "declaration frontend storage limit exceeded") ||
      diagnostic->line != 1u || diagnostic->column == 0u ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      tracking.outstanding_bytes != outstanding_before_failure ||
      tracking.invalid_releases != 0u ||
      memcmp(snapshot, update_tape.tokens, token_bytes) != 0 ||
      anchor_binding == NULL ||
      anchor_binding->kind != CTOOL_C_BINDING_TYPEDEF) {
    (void)fprintf(
        stderr,
        "scalar-updates: update promotion rollback differs: %s diagnostics=%u\n",
        ctool_status_name(status), ctool_job_diagnostic_count(job));
    goto cleanup;
  }
  (void)memset(&recovered_unit, 0xa5, sizeof(recovered_unit));
  status = ctool_c_parse(job, &anchor_tape, &fixture->parse_request,
                         &recovered_unit);
  if (status != CTOOL_OK ||
      find_binding(&recovered_unit, "update_limit_anchor_t") == NULL ||
      find_binding(&anchor_unit, "update_limit_anchor_t") == NULL ||
      ctool_job_diagnostic_count(job) != 1u) {
    (void)fprintf(stderr,
                  "scalar-updates: update-limit recovery differs\n");
    goto cleanup;
  }
  failed = 0;

cleanup:
  if (job != NULL) {
    ctool_job_close(job);
    if (tracking.outstanding_bytes != 0ull ||
        tracking.invalid_releases != 0u) {
      (void)fprintf(stderr,
                    "scalar-updates: update-limit allocation leaked\n");
      failed = 1;
    }
  }
  free(snapshot);
  return failed;
}

static int validate_scalar_return_unit(
    const ctool_c_translation_unit_t *unit) {
  static const ctool_c_expression_operator_t binary_operators[] = {
      CTOOL_C_EXPRESSION_OPERATOR_MULTIPLY,
      CTOOL_C_EXPRESSION_OPERATOR_DIVIDE,
      CTOOL_C_EXPRESSION_OPERATOR_REMAINDER,
      CTOOL_C_EXPRESSION_OPERATOR_ADD,
      CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT,
      CTOOL_C_EXPRESSION_OPERATOR_SHIFT_LEFT,
      CTOOL_C_EXPRESSION_OPERATOR_SHIFT_RIGHT,
      CTOOL_C_EXPRESSION_OPERATOR_LESS,
      CTOOL_C_EXPRESSION_OPERATOR_LESS_EQUAL,
      CTOOL_C_EXPRESSION_OPERATOR_GREATER,
      CTOOL_C_EXPRESSION_OPERATOR_GREATER_EQUAL,
      CTOOL_C_EXPRESSION_OPERATOR_EQUAL,
      CTOOL_C_EXPRESSION_OPERATOR_NOT_EQUAL,
      CTOOL_C_EXPRESSION_OPERATOR_BITWISE_AND,
      CTOOL_C_EXPRESSION_OPERATOR_BITWISE_XOR,
      CTOOL_C_EXPRESSION_OPERATOR_BITWISE_OR,
      CTOOL_C_EXPRESSION_OPERATOR_LOGICAL_AND,
      CTOOL_C_EXPRESSION_OPERATOR_LOGICAL_OR};
  static const struct {
    const char *name;
    ctool_u32 line;
    ctool_c_type_kind_t kind;
  } literal_functions[] = {
      {"literal_int", 2u, CTOOL_C_TYPE_SIGNED_INT},
      {"literal_unsigned", 3u, CTOOL_C_TYPE_UNSIGNED_INT},
      {"literal_long", 4u, CTOOL_C_TYPE_SIGNED_LONG},
      {"literal_unsigned_long", 5u, CTOOL_C_TYPE_UNSIGNED_LONG},
      {"literal_long_long", 6u, CTOOL_C_TYPE_SIGNED_LONG_LONG},
      {"literal_unsigned_long_long", 7u,
       CTOOL_C_TYPE_UNSIGNED_LONG_LONG}};
  const ctool_c_statement_t *statement;
  const ctool_c_function_definition_t *definition;
  const ctool_c_statement_t *body;
  const ctool_c_expression_t *expression;
  ctool_u32 root;
  ctool_u32 left;
  ctool_u32 right;
  ctool_u32 index;
  ctool_u32 return_count = 0u;
  ctool_u32 compound_count = 0u;
  ctool_u32 expression_statement_count = 0u;

  if (unit->function_definition_count != 22u ||
      unit->block_binding_count != 1u) {
    (void)fprintf(stderr, "scalar-returns: definition inventory differs\n");
    return 1;
  }
  for (index = 0u; index < unit->statement_count; index++) {
    if (unit->statements[index].kind == CTOOL_C_STATEMENT_RETURN) {
      return_count++;
    } else if (unit->statements[index].kind == CTOOL_C_STATEMENT_COMPOUND) {
      compound_count++;
    } else if (unit->statements[index].kind ==
               CTOOL_C_STATEMENT_EXPRESSION) {
      expression_statement_count++;
    }
  }
  if (return_count != 22u || compound_count != 22u ||
      expression_statement_count != 2u || unit->statement_count != 47u) {
    (void)fprintf(stderr, "scalar-returns: statement inventory differs\n");
    return 1;
  }

  statement = scalar_return_statement(unit, "returns_void");
  if (statement == NULL || statement->expression != CTOOL_C_AST_NONE ||
      !dual_location_matches(&statement->location,
                             &statement->physical_location,
                             "/scalar-returns.c", 1u)) {
    (void)fprintf(stderr, "scalar-returns: void return differs\n");
    return 1;
  }
  for (index = 0u; index < ARRAY_COUNT(literal_functions); index++) {
    root = scalar_return_value(unit, literal_functions[index].name,
                               literal_functions[index].kind);
    root = scalar_unwrap_conversions(unit, root);
    if (root == CTOOL_C_AST_NONE ||
        unit->expressions[root].kind != CTOOL_C_EXPRESSION_INTEGER_CONSTANT ||
        unit->expressions[root].integer_bits != 42ull ||
        scalar_type_kind(unit, unit->expressions[root].type, NULL) !=
            literal_functions[index].kind ||
        scalar_count_integer_constants(unit, literal_functions[index].line,
                                       42ull,
                                       literal_functions[index].kind) != 1u) {
      (void)fprintf(stderr,
                    "scalar-returns: exact literal rank %u differs\n", index);
      return 1;
    }
  }
  if (scalar_count_integer_constants(unit, 9u, 65ull,
                                     CTOOL_C_TYPE_SIGNED_INT) != 2u ||
      scalar_count_integer_constants(unit, 9u, 10ull,
                                     CTOOL_C_TYPE_SIGNED_INT) != 1u) {
    (void)fprintf(stderr, "scalar-returns: character constants differ\n");
    return 1;
  }
  if (scalar_count_integer_constants(unit, 22u, 0xffffffffull,
                                     CTOOL_C_TYPE_SIGNED_INT) != 1u) {
    (void)fprintf(stderr,
                  "scalar-returns: signed high-bit character differs\n");
    return 1;
  }

  statement = scalar_return_statement(unit, "widened_return");
  if (statement == NULL || statement->expression >= unit->expression_count) {
    (void)fprintf(stderr, "scalar-returns: widened return is absent\n");
    return 1;
  }
  expression = &unit->expressions[statement->expression];
  if (expression->kind != CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION ||
      expression->conversion != CTOOL_C_CONVERSION_ASSIGNMENT ||
      scalar_type_kind(unit, expression->type, NULL) !=
          CTOOL_C_TYPE_SIGNED_LONG) {
    (void)fprintf(stderr, "scalar-returns: return assignment conversion differs\n");
    return 1;
  }

  root = scalar_return_value(unit, "precedence", CTOOL_C_TYPE_SIGNED_INT);
  if (scalar_expect_operator(unit, root, CTOOL_C_EXPRESSION_BINARY,
                             CTOOL_C_EXPRESSION_OPERATOR_LOGICAL_OR, 2u) != 0) {
    (void)fprintf(stderr, "scalar-returns: logical-or root differs\n");
    return 1;
  }
  left = scalar_operator_child(unit, root, 0u);
  if (scalar_expect_operator(unit, left, CTOOL_C_EXPRESSION_BINARY,
                             CTOOL_C_EXPRESSION_OPERATOR_LOGICAL_AND, 2u) !=
      0) {
    (void)fprintf(stderr, "scalar-returns: logical-and precedence differs\n");
    return 1;
  }
  left = scalar_operator_child(unit, left, 0u);
  if (scalar_expect_operator(unit, left, CTOOL_C_EXPRESSION_BINARY,
                             CTOOL_C_EXPRESSION_OPERATOR_EQUAL, 2u) != 0) {
    (void)fprintf(stderr, "scalar-returns: equality precedence differs\n");
    return 1;
  }
  right = scalar_operator_child(unit, left, 1u);
  if (scalar_expect_operator(unit, right, CTOOL_C_EXPRESSION_UNARY,
                             CTOOL_C_EXPRESSION_OPERATOR_LOGICAL_NOT, 1u) !=
      0) {
    (void)fprintf(stderr, "scalar-returns: logical-not precedence differs\n");
    return 1;
  }
  left = scalar_operator_child(unit, left, 0u);
  if (scalar_expect_operator(unit, left, CTOOL_C_EXPRESSION_BINARY,
                             CTOOL_C_EXPRESSION_OPERATOR_SHIFT_LEFT, 2u) != 0) {
    (void)fprintf(stderr, "scalar-returns: shift precedence differs\n");
    return 1;
  }
  left = scalar_operator_child(unit, left, 0u);
  if (scalar_expect_operator(unit, left, CTOOL_C_EXPRESSION_BINARY,
                             CTOOL_C_EXPRESSION_OPERATOR_ADD, 2u) != 0) {
    (void)fprintf(stderr, "scalar-returns: additive precedence differs\n");
    return 1;
  }
  right = scalar_operator_child(unit, left, 1u);
  if (scalar_expect_operator(unit, scalar_operator_child(unit, left, 0u),
                             CTOOL_C_EXPRESSION_UNARY,
                             CTOOL_C_EXPRESSION_OPERATOR_UNARY_PLUS, 1u) != 0 ||
      scalar_expect_operator(unit, right, CTOOL_C_EXPRESSION_BINARY,
                             CTOOL_C_EXPRESSION_OPERATOR_MULTIPLY, 2u) != 0 ||
      scalar_expect_operator(unit, scalar_operator_child(unit, right, 0u),
                             CTOOL_C_EXPRESSION_UNARY,
                             CTOOL_C_EXPRESSION_OPERATOR_UNARY_NEGATE, 1u) !=
          0 ||
      scalar_expect_operator(unit, scalar_operator_child(unit, right, 1u),
                             CTOOL_C_EXPRESSION_UNARY,
                             CTOOL_C_EXPRESSION_OPERATOR_BITWISE_NOT, 1u) !=
          0) {
    (void)fprintf(stderr, "scalar-returns: unary/multiplicative precedence differs\n");
    return 1;
  }
  for (index = 0u; index < ARRAY_COUNT(binary_operators); index++) {
    ctool_u32 expression_index;
    ctool_bool found = CTOOL_FALSE;
    for (expression_index = 0u;
         expression_index < unit->expression_count; expression_index++) {
      const ctool_c_expression_t *candidate =
          &unit->expressions[expression_index];
      if (candidate->kind == CTOOL_C_EXPRESSION_BINARY &&
          candidate->operation == binary_operators[index]) {
        found = CTOOL_TRUE;
        break;
      }
    }
    if (found == CTOOL_FALSE) {
      (void)fprintf(stderr,
                    "scalar-returns: binary operator %u is absent\n", index);
      return 1;
    }
  }

  root = scalar_return_value(unit, "promotion", CTOOL_C_TYPE_SIGNED_INT);
  root = scalar_unwrap_conversions(unit, root);
  left = root == CTOOL_C_AST_NONE
             ? CTOOL_C_AST_NONE
             : scalar_expression_child(unit, &unit->expressions[root], 0u);
  if (scalar_expect_operator(unit, root, CTOOL_C_EXPRESSION_BINARY,
                             CTOOL_C_EXPRESSION_OPERATOR_ADD, 2u) != 0 ||
      left == CTOOL_C_AST_NONE ||
      scalar_conversion_chain_has(unit, left,
                                  CTOOL_C_CONVERSION_INTEGER_PROMOTION) !=
          CTOOL_TRUE ||
      scalar_type_kind(unit, unit->expressions[left].type, NULL) !=
          CTOOL_C_TYPE_SIGNED_INT) {
    (void)fprintf(stderr, "scalar-returns: integer promotion differs\n");
    return 1;
  }

  root = scalar_return_value(unit, "mixed", CTOOL_C_TYPE_UNSIGNED_LONG);
  root = scalar_unwrap_conversions(unit, root);
  left = root == CTOOL_C_AST_NONE
             ? CTOOL_C_AST_NONE
             : scalar_expression_child(unit, &unit->expressions[root], 0u);
  right = root == CTOOL_C_AST_NONE
              ? CTOOL_C_AST_NONE
              : scalar_expression_child(unit, &unit->expressions[root], 1u);
  if (scalar_expect_operator(unit, root, CTOOL_C_EXPRESSION_BINARY,
                             CTOOL_C_EXPRESSION_OPERATOR_ADD, 2u) != 0 ||
      scalar_type_kind(unit, unit->expressions[root].type, NULL) !=
          CTOOL_C_TYPE_UNSIGNED_LONG ||
      left == CTOOL_C_AST_NONE || right == CTOOL_C_AST_NONE ||
      scalar_conversion_chain_has(unit, left,
                                  CTOOL_C_CONVERSION_USUAL_ARITHMETIC) !=
          CTOOL_TRUE ||
      scalar_conversion_chain_has(unit, right,
                                  CTOOL_C_CONVERSION_USUAL_ARITHMETIC) !=
          CTOOL_TRUE ||
      scalar_type_kind(unit, unit->expressions[left].type, NULL) !=
          CTOOL_C_TYPE_UNSIGNED_LONG ||
      scalar_type_kind(unit, unit->expressions[right].type, NULL) !=
          CTOOL_C_TYPE_UNSIGNED_LONG) {
    (void)fprintf(stderr, "scalar-returns: usual arithmetic conversion differs\n");
    return 1;
  }

  root = scalar_return_value(unit, "assignment", CTOOL_C_TYPE_SIGNED_LONG);
  root = scalar_unwrap_conversions(unit, root);
  left = root == CTOOL_C_AST_NONE
             ? CTOOL_C_AST_NONE
             : scalar_expression_child(unit, &unit->expressions[root], 0u);
  right = root == CTOOL_C_AST_NONE
              ? CTOOL_C_AST_NONE
              : scalar_expression_child(unit, &unit->expressions[root], 1u);
  if (scalar_expect_operator(unit, root, CTOOL_C_EXPRESSION_ASSIGNMENT,
                             CTOOL_C_EXPRESSION_OPERATOR_ASSIGN, 2u) != 0 ||
      unit->expressions[root].computation_type != unit->expressions[root].type ||
      left == CTOOL_C_AST_NONE ||
      unit->expressions[left].kind != CTOOL_C_EXPRESSION_PARAMETER ||
      scalar_conversion_chain_has(unit, right,
                                  CTOOL_C_CONVERSION_ASSIGNMENT) !=
          CTOOL_TRUE ||
      scalar_expect_operator(unit, right, CTOOL_C_EXPRESSION_ASSIGNMENT,
                             CTOOL_C_EXPRESSION_OPERATOR_ASSIGN, 2u) != 0) {
    (void)fprintf(stderr, "scalar-returns: right-associative assignment differs\n");
    return 1;
  }
  right = scalar_unwrap_conversions(unit, right);
  left = scalar_expression_child(unit, &unit->expressions[right], 0u);
  root = scalar_expression_child(unit, &unit->expressions[right], 1u);
  if (left == CTOOL_C_AST_NONE ||
      unit->expressions[left].kind != CTOOL_C_EXPRESSION_PARAMETER ||
      root == CTOOL_C_AST_NONE ||
      scalar_conversion_chain_has(unit, root,
                                  CTOOL_C_CONVERSION_ASSIGNMENT) !=
          CTOOL_TRUE) {
    (void)fprintf(stderr, "scalar-returns: assignment lvalue conversion differs\n");
    return 1;
  }

  definition = find_function_definition(unit, "volatile_read");
  if (definition == NULL || definition->body >= unit->statement_count) {
    (void)fprintf(stderr, "scalar-returns: volatile definition differs\n");
    return 1;
  }
  body = &unit->statements[definition->body];
  if (body->kind != CTOOL_C_STATEMENT_COMPOUND || body->child_count != 3u ||
      body->first_child > unit->statement_child_count ||
      body->child_count > unit->statement_child_count - body->first_child) {
    (void)fprintf(stderr, "scalar-returns: volatile body differs\n");
    return 1;
  }
  index = unit->statement_children[body->first_child];
  if (index >= unit->statement_count ||
      unit->statements[index].kind != CTOOL_C_STATEMENT_DECLARATION ||
      unit->statements[index].first_block_binding != 0u ||
      unit->statements[index].block_binding_count != 1u ||
      !string_equal(unit->block_bindings[0].name, "value") ||
      unit->block_bindings[0].kind != CTOOL_C_BINDING_OBJECT ||
      unit->block_bindings[0].storage != CTOOL_C_STORAGE_NONE) {
    (void)fprintf(stderr, "scalar-returns: volatile declaration differs\n");
    return 1;
  }
  index = unit->statement_children[body->first_child + 1u];
  if (index >= unit->statement_count ||
      unit->statements[index].kind != CTOOL_C_STATEMENT_EXPRESSION ||
      unit->statements[index].expression >= unit->expression_count) {
    (void)fprintf(stderr, "scalar-returns: volatile expression statement differs\n");
    return 1;
  }
  expression = &unit->expressions[unit->statements[index].expression];
  if (expression->kind != CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION ||
      expression->conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      scalar_type_kind(unit, expression->type, NULL) !=
          CTOOL_C_TYPE_SIGNED_INT) {
    (void)fprintf(stderr, "scalar-returns: volatile final lvalue conversion differs\n");
    return 1;
  }
  root = scalar_expression_child(unit, expression, 0u);
  if (root == CTOOL_C_AST_NONE ||
      unit->expressions[root].kind != CTOOL_C_EXPRESSION_BLOCK_BINDING ||
      unit->expressions[root].reference != 0u) {
    (void)fprintf(stderr, "scalar-returns: volatile source differs\n");
    return 1;
  }
  {
    ctool_u32 qualifiers = 0u;
    if (scalar_type_kind(unit, unit->expressions[root].type, &qualifiers) !=
            CTOOL_C_TYPE_SIGNED_INT ||
        (qualifiers & CTOOL_C_QUAL_VOLATILE) == 0u) {
      (void)fprintf(stderr,
                    "scalar-returns: volatile source qualification differs\n");
      return 1;
    }
  }
  statement = scalar_return_statement(unit, "volatile_read");
  if (statement == NULL || statement->expression != CTOOL_C_AST_NONE) {
    (void)fprintf(stderr, "scalar-returns: trailing void return differs\n");
    return 1;
  }

  statement = scalar_return_statement(unit, "enum_return");
  if (statement == NULL || statement->expression >= unit->expression_count) {
    (void)fprintf(stderr, "scalar-returns: enum return is absent\n");
    return 1;
  }

  definition = find_function_definition(unit, "converted_call");
  if (definition == NULL || definition->body >= unit->statement_count) {
    (void)fprintf(stderr, "scalar-returns: converted call is absent\n");
    return 1;
  }
  body = &unit->statements[definition->body];
  if (body->kind != CTOOL_C_STATEMENT_COMPOUND || body->child_count != 2u ||
      body->first_child > unit->statement_child_count ||
      body->child_count > unit->statement_child_count - body->first_child) {
    (void)fprintf(stderr, "scalar-returns: converted call body differs\n");
    return 1;
  }
  index = unit->statement_children[body->first_child];
  if (index >= unit->statement_count ||
      unit->statements[index].kind != CTOOL_C_STATEMENT_EXPRESSION ||
      unit->statements[index].expression >= unit->expression_count) {
    (void)fprintf(stderr, "scalar-returns: converted call statement differs\n");
    return 1;
  }
  expression = &unit->expressions[unit->statements[index].expression];
  root = scalar_expression_child(unit, expression, 1u);
  if (expression->kind != CTOOL_C_EXPRESSION_CALL ||
      expression->child_count != 2u || root == CTOOL_C_AST_NONE ||
      unit->expressions[root].kind !=
          CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION ||
      unit->expressions[root].conversion != CTOOL_C_CONVERSION_ASSIGNMENT ||
      scalar_type_kind(unit, unit->expressions[root].type, NULL) !=
          CTOOL_C_TYPE_SIGNED_LONG) {
    (void)fprintf(stderr,
                  "scalar-returns: fixed call assignment conversion differs\n");
    return 1;
  }
  left = scalar_expression_child(unit, &unit->expressions[root], 0u);
  if (left == CTOOL_C_AST_NONE ||
      unit->expressions[left].kind != CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION ||
      unit->expressions[left].conversion !=
          CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      scalar_type_kind(unit, unit->expressions[left].type, NULL) !=
          CTOOL_C_TYPE_SIGNED_INT) {
    (void)fprintf(stderr,
                  "scalar-returns: fixed call source conversion differs\n");
    return 1;
  }
  expression = &unit->expressions[statement->expression];
  root = scalar_expression_child(unit, expression, 0u);
  if (expression->kind != CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION ||
      expression->conversion != CTOOL_C_CONVERSION_ASSIGNMENT ||
      scalar_type_kind(unit, expression->type, NULL) != CTOOL_C_TYPE_ENUM ||
      root == CTOOL_C_AST_NONE ||
      scalar_type_kind(unit, unit->expressions[root].type, NULL) !=
          CTOOL_C_TYPE_UNSIGNED_INT) {
    (void)fprintf(stderr,
                  "scalar-returns: enum assignment conversion differs\n");
    return 1;
  }

  definition = find_function_definition(unit, "qualified_parameter");
  statement = scalar_return_statement(unit, "qualified_parameter");
  if (definition == NULL || statement == NULL ||
      statement->expression >= unit->expression_count) {
    (void)fprintf(stderr, "scalar-returns: qualified parameter is absent\n");
    return 1;
  }
  {
    const ctool_c_type_node_t *function =
        type_node(unit, definition->declared_type);
    const ctool_c_parameter_t *parameter;
    ctool_u32 parameter_type;
    ctool_u32 parameter_qualifiers = 0u;
    ctool_u32 function_qualifiers = 0u;
    if (function == NULL || function->kind != CTOOL_C_TYPE_FUNCTION ||
        function->parameter_count != 1u ||
        function->first_parameter >= unit->parameter_count ||
        function->first_parameter >= unit->graph.parameter_type_count) {
      (void)fprintf(stderr,
                    "scalar-returns: qualified parameter slice differs\n");
      return 1;
    }
    parameter = &unit->parameters[function->first_parameter];
    parameter_type = unit->graph.parameter_types[function->first_parameter];
    expression = &unit->expressions[statement->expression];
    root = scalar_expression_child(unit, expression, 0u);
    if (!string_equal(parameter->name, "value") ||
        parameter->storage != CTOOL_C_STORAGE_NONE ||
        scalar_type_kind(unit, parameter->type, &parameter_qualifiers) !=
            CTOOL_C_TYPE_SIGNED_INT ||
        (parameter_qualifiers &
         (CTOOL_C_QUAL_CONST | CTOOL_C_QUAL_VOLATILE)) !=
            (CTOOL_C_QUAL_CONST | CTOOL_C_QUAL_VOLATILE) ||
        scalar_type_kind(unit, parameter_type, &function_qualifiers) !=
            CTOOL_C_TYPE_SIGNED_INT ||
        function_qualifiers != 0u ||
        expression->kind != CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION ||
        expression->conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
        root == CTOOL_C_AST_NONE ||
        unit->expressions[root].kind != CTOOL_C_EXPRESSION_PARAMETER ||
        unit->expressions[root].reference != function->first_parameter ||
        unit->expressions[root].type != parameter->type) {
      (void)fprintf(stderr,
                    "scalar-returns: parameter object qualification differs\n");
      return 1;
    }
  }

  root = scalar_return_value(unit, "divide_zero", CTOOL_C_TYPE_SIGNED_INT);
  if (scalar_expect_operator(unit, root, CTOOL_C_EXPRESSION_BINARY,
                             CTOOL_C_EXPRESSION_OPERATOR_DIVIDE, 2u) != 0) {
    (void)fprintf(stderr, "scalar-returns: ordinary divide-by-zero was folded\n");
    return 1;
  }
  root = scalar_return_value(unit, "signed_overflow",
                             CTOOL_C_TYPE_SIGNED_INT);
  if (scalar_expect_operator(unit, root, CTOOL_C_EXPRESSION_BINARY,
                             CTOOL_C_EXPRESSION_OPERATOR_ADD, 2u) != 0) {
    (void)fprintf(stderr, "scalar-returns: ordinary signed overflow was folded\n");
    return 1;
  }
  root = scalar_return_value(unit, "overshift", CTOOL_C_TYPE_SIGNED_INT);
  if (scalar_expect_operator(unit, root, CTOOL_C_EXPRESSION_BINARY,
                             CTOOL_C_EXPRESSION_OPERATOR_SHIFT_LEFT, 2u) != 0) {
    (void)fprintf(stderr, "scalar-returns: ordinary overshift was folded\n");
    return 1;
  }
  return 0;
}

static int run_scalar_returns(const char *host_root) {
  const ctool_u32 chain_operator_count = 4096u;
  static const char source[] =
      "void returns_void(void) { return; }\n"
      "int literal_int(void) { return 42; }\n"
      "unsigned int literal_unsigned(void) { return 42u; }\n"
      "long literal_long(void) { return 42L; }\n"
      "unsigned long literal_unsigned_long(void) { return 42UL; }\n"
      "long long literal_long_long(void) { return 42LL; }\n"
      "unsigned long long literal_unsigned_long_long(void) { return 42ULL; }\n"
      "long widened_return(int value) { return value; }\n"
      "int character_literals(void) { return 'A' + '\\n' + '\\x41'; }\n"
      "int precedence(int a, int b, int c) { return +a + -b * ~c << 1 == !0 && a || b; }\n"
      "int operator_inventory(int a, int b, int c) { return a * b / c % 3 + a - b << 1 >> 1 < b <= c > a >= b == c != a & b ^ c | a && b || c; }\n"
      "int promotion(signed char value) { return value + 1; }\n"
      "unsigned long mixed(long left, unsigned int right) { return left + right; }\n"
      "long assignment(long left, int middle, signed char right) { return left = middle = right; }\n"
      "void volatile_read(void) { volatile int value; value; return; }\n"
      "int qualified_parameter(const volatile int value) { return value; }\n"
      "int divide_zero(void) { return 1 / 0; }\n"
      "int signed_overflow(void) { return 2147483647 + 1; }\n"
      "int overshift(void) { return 1 << 32; }\n"
      "enum scalar_code { SCALAR_CODE_ZERO, SCALAR_CODE_ONE };\n"
      "enum scalar_code enum_return(unsigned int value) { return value; }\n"
      "int high_character(void) { return '\\xff'; }\n"
      "void take_long(long value);\n"
      "void converted_call(int value) { take_long(value); return; }\n";
  static const frontend_exact_failure_case_t failure_cases[] = {
      {{"missing return value", "int bad(void) { return; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_STATEMENT},
       1u, 23u, "non-void function requires a return value"},
      {{"value returned from void function",
        "void bad(void) { return 1; }\n", CTOOL_ERR_INPUT,
        CTOOL_C_PARSE_DIAG_STATEMENT},
       1u, 25u, "void function cannot return a value"},
      {{"assignment to const lvalue",
        "void bad(void) { const int value; value = 1; }\n", CTOOL_ERR_INPUT,
        CTOOL_C_PARSE_DIAG_EXPRESSION},
       1u, 41u, "assignment requires a modifiable lvalue"},
      {{"assignment to non-lvalue",
        "void bad(int value) { (value + 1) = 2; }\n", CTOOL_ERR_INPUT,
        CTOOL_C_PARSE_DIAG_EXPRESSION},
       1u, 35u, "assignment requires a modifiable lvalue"},
      {{"incompatible return", "int bad(int *value) { return value; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       1u, 30u, "return expression is not convertible to function result type"},
      {{"incompatible assignment",
        "void bad(int *pointer, int value) { pointer = value; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       1u, 47u,
       "assignment right operand is not convertible to left operand type"},
      {{"malformed integer literal", "int bad(void) { return 09; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       1u, 24u, "integer constant suffix is invalid"},
      {{"deferred decimal floating literal",
        "int bad(void) { return 1.0; }\n", CTOOL_ERR_UNSUPPORTED,
        CTOOL_C_PARSE_DIAG_EXPRESSION},
       1u, 24u, "floating constants are outside this expression slice"},
      {{"deferred hexadecimal floating literal",
        "int bad(void) { return 0x1p0; }\n", CTOOL_ERR_UNSUPPORTED,
        CTOOL_C_PARSE_DIAG_EXPRESSION},
       1u, 24u, "floating constants are outside this expression slice"},
      {{"deferred floating return conversion",
        "int bad(float value) { return value; }\n", CTOOL_ERR_UNSUPPORTED,
        CTOOL_C_PARSE_DIAG_EXPRESSION},
       1u, 31u,
       "floating assignment conversions are outside this body slice"},
      {{"deferred floating logical operand",
        "int bad(float value) { return !value; }\n", CTOOL_ERR_UNSUPPORTED,
        CTOOL_C_PARSE_DIAG_EXPRESSION},
       1u, 31u, "floating logical operands are outside this body slice"},
      {{"malformed character literal",
        "int bad(void) { return '\\x'; }\n", CTOOL_ERR_INPUT,
        CTOOL_C_PARSE_DIAG_EXPRESSION},
       1u, 24u, "character hexadecimal escape requires a digit"},
  };
  frontend_fixture_t fixture;
  ctool_c_translation_unit_t unit;
  ctool_c_translation_unit_t chain_unit;
  char *chain_source = NULL;
  ctool_u32 index;
  int failed = 1;

  if (begin_frontend_fixture(&fixture, "scalar-returns", host_root,
                             64u * 1024u * 1024u) != 0) {
    return 1;
  }
  if (parse_valid_fixture(&fixture, "/scalar-returns.c", source, &unit) != 0 ||
      validate_scalar_return_unit(&unit) != 0) {
    goto cleanup;
  }
  chain_source = build_scalar_operator_chain(chain_operator_count);
  if (chain_source == NULL ||
      parse_valid_fixture(&fixture, "/scalar-chain.c", chain_source,
                          &chain_unit) != 0 ||
      validate_scalar_operator_chain(&chain_unit, chain_operator_count) != 0 ||
      validate_scalar_operator_storage_limit(&fixture, host_root,
                                             chain_source) != 0) {
    goto cleanup;
  }
  for (index = 0u; index < ARRAY_COUNT(failure_cases); index++) {
    const frontend_exact_failure_case_t *test_case = &failure_cases[index];
    if (expect_frontend_failure_at_message(
            &fixture, &test_case->failure, "/scalar-return-failure.c",
            test_case->line, test_case->column, test_case->message) != 0 ||
        validate_scalar_return_unit(&unit) != 0 ||
        validate_scalar_operator_chain(&chain_unit,
                                       chain_operator_count) != 0) {
      goto cleanup;
    }
  }
  failed = 0;

cleanup:
  free(chain_source);
  if (finish_frontend_fixture(&fixture) != 0) {
    failed = 1;
  }
  if (failed == 0) {
    (void)printf("scalar-returns: ok\n");
  }
  return failed;
}

static ctool_u32 pointer_return_root(const ctool_c_translation_unit_t *unit,
                                     const char *function_name) {
  const ctool_c_statement_t *statement =
      scalar_return_statement(unit, function_name);
  return statement == NULL ? CTOOL_C_AST_NONE
                           : scalar_unwrap_conversions(unit,
                                                       statement->expression);
}

static const ctool_c_statement_t *pointer_expression_statement(
    const ctool_c_translation_unit_t *unit, const char *function_name) {
  const ctool_c_function_definition_t *definition =
      find_function_definition(unit, function_name);
  const ctool_c_statement_t *body;
  ctool_u32 index;
  if (definition == NULL || definition->body >= unit->statement_count) {
    return NULL;
  }
  body = &unit->statements[definition->body];
  if (body->kind != CTOOL_C_STATEMENT_COMPOUND ||
      body->first_child > unit->statement_child_count ||
      body->child_count > unit->statement_child_count - body->first_child) {
    return NULL;
  }
  for (index = 0u; index < body->child_count; index++) {
    ctool_u32 child = unit->statement_children[body->first_child + index];
    if (child >= unit->statement_count) {
      return NULL;
    }
    if (unit->statements[child].kind == CTOOL_C_STATEMENT_EXPRESSION) {
      return &unit->statements[child];
    }
  }
  return NULL;
}

static int pointer_expression_shape(const ctool_c_translation_unit_t *unit,
                                    ctool_u32 expression,
                                    ctool_c_expression_kind_t kind,
                                    ctool_c_expression_operator_t operation,
                                    ctool_u32 child_count) {
  const ctool_c_expression_t *node =
      expression < unit->expression_count ? &unit->expressions[expression]
                                          : NULL;
  return node != NULL && node->kind == kind && node->operation == operation &&
                 node->child_count == child_count
             ? 0
             : 1;
}

static int pointer_folded_return(const ctool_c_translation_unit_t *unit,
                                 const char *function_name,
                                 ctool_u64 expected_bits) {
  ctool_u32 root = pointer_return_root(unit, function_name);
  const ctool_c_expression_t *expression =
      root < unit->expression_count ? &unit->expressions[root] : NULL;
  return expression != NULL &&
                 expression->kind == CTOOL_C_EXPRESSION_INTEGER_CONSTANT &&
                 expression->integer_bits == expected_bits &&
                 scalar_type_kind(unit, expression->type, NULL) ==
                     CTOOL_C_TYPE_UNSIGNED_INT
             ? 0
             : 1;
}

static int validate_pointer_expression_unit(
    const ctool_c_translation_unit_t *unit) {
  const ctool_c_binding_t *aggregate_binding =
      find_binding(unit, "aggregate_t");
  const ctool_c_binding_t *packed_binding =
      find_binding(unit, "promoted_pack_t");
  const ctool_c_binding_t *wide_binding = find_binding(unit, "wide_bits_t");
  const ctool_c_type_node_t *aggregate =
      aggregate_binding == NULL ? NULL
                                : type_node(unit, aggregate_binding->type);
  const ctool_c_type_node_t *packed =
      packed_binding == NULL ? NULL : type_node(unit, packed_binding->type);
  const ctool_c_type_node_t *wide =
      wide_binding == NULL ? NULL : type_node(unit, wide_binding->type);
  const ctool_c_type_layout_t *aggregate_layout =
      aggregate_binding == NULL ? NULL
                                : type_layout(unit, aggregate_binding->type);
  const ctool_c_record_member_t *direct;
  const ctool_c_record_member_t *locked;
  const ctool_c_record_member_t *values;
  const ctool_c_record_member_t *anonymous;
  const ctool_c_record_member_t *promoted;
  const ctool_c_record_member_t *packed_anonymous;
  const ctool_c_record_member_t *packed_promoted;
  const ctool_c_record_member_t *named;
  const ctool_c_record_member_t *named_value;
  const ctool_c_record_member_t *bits;
  const ctool_c_record_member_t *wide_bits;
  const ctool_c_type_node_t *anonymous_record;
  const ctool_c_type_node_t *packed_anonymous_record;
  const ctool_c_type_node_t *named_record;
  ctool_u32 direct_index = CTOOL_C_AST_NONE;
  ctool_u32 locked_index = CTOOL_C_AST_NONE;
  ctool_u32 values_index = CTOOL_C_AST_NONE;
  ctool_u32 anonymous_index = CTOOL_C_AST_NONE;
  ctool_u32 promoted_index = CTOOL_C_AST_NONE;
  ctool_u32 packed_anonymous_index = CTOOL_C_AST_NONE;
  ctool_u32 packed_promoted_index = CTOOL_C_AST_NONE;
  ctool_u32 named_index = CTOOL_C_AST_NONE;
  ctool_u32 named_value_index = CTOOL_C_AST_NONE;
  ctool_u32 bits_index = CTOOL_C_AST_NONE;
  ctool_u32 wide_bits_index = CTOOL_C_AST_NONE;
  ctool_u32 root;
  ctool_u32 child;
  ctool_u32 index;
  ctool_u32 qualifiers = 0u;
  const ctool_c_expression_t *node;
  const ctool_c_type_node_t *decay_pointer;
  const ctool_c_statement_t *statement;

  if (unit->binding_count != 37u || unit->tag_count != 1u ||
      unit->graph.member_count != 11u ||
      unit->function_definition_count != 33u || unit->statement_count != 68u ||
      unit->statement_child_count != 35u || unit->block_binding_count != 0u ||
      aggregate_binding == NULL ||
      aggregate_binding->kind != CTOOL_C_BINDING_TYPEDEF || aggregate == NULL ||
      aggregate->kind != CTOOL_C_TYPE_RECORD || aggregate->member_count != 6u ||
      aggregate_layout == NULL || aggregate_layout->size != 48u ||
      aggregate_layout->alignment != 16u || packed_binding == NULL ||
      packed_binding->kind != CTOOL_C_BINDING_TYPEDEF || packed == NULL ||
      packed->kind != CTOOL_C_TYPE_RECORD || packed->member_count != 1u ||
      wide_binding == NULL ||
      wide_binding->kind != CTOOL_C_BINDING_TYPEDEF || wide == NULL ||
      wide->kind != CTOOL_C_TYPE_RECORD || wide->member_count != 1u) {
    (void)fprintf(stderr,
                  "pointer-expressions: translation-unit inventory differs\n");
    return 1;
  }
  direct = find_record_member(unit, aggregate, "direct", &direct_index);
  locked = find_record_member(unit, aggregate, "locked", &locked_index);
  values = find_record_member(unit, aggregate, "values", &values_index);
  bits = find_record_member(unit, aggregate, "bits", &bits_index);
  wide_bits = find_record_member(unit, wide, "wide_bits", &wide_bits_index);
  anonymous = &unit->graph.members[aggregate->first_member + 3u];
  anonymous_index = aggregate->first_member + 3u;
  anonymous_record = type_node(unit, anonymous->type);
  promoted = anonymous_record == NULL
                 ? NULL
                 : find_record_member(unit, anonymous_record, "promoted",
                                      &promoted_index);
  packed_anonymous_index = packed->first_member;
  packed_anonymous = &unit->graph.members[packed_anonymous_index];
  packed_anonymous_record = type_node(unit, packed_anonymous->type);
  packed_promoted =
      packed_anonymous_record == NULL
          ? NULL
          : find_record_member(unit, packed_anonymous_record,
                               "packed_promoted", &packed_promoted_index);
  named = find_record_member(unit, aggregate, "named", &named_index);
  named_record = named == NULL ? NULL : type_node(unit, named->type);
  named_value = named_record == NULL
                    ? NULL
                    : find_record_member(unit, named_record, "named_value",
                                         &named_value_index);
  if (direct == NULL || locked == NULL || values == NULL || bits == NULL ||
      wide_bits == NULL || anonymous == NULL ||
      anonymous->anonymous != CTOOL_TRUE || anonymous->name.size != 0u ||
      anonymous_record == NULL ||
      anonymous_record->kind != CTOOL_C_TYPE_RECORD || promoted == NULL ||
      packed_anonymous == NULL ||
      packed_anonymous->anonymous != CTOOL_TRUE ||
      packed_anonymous_record == NULL || packed_promoted == NULL ||
      named == NULL || named_record == NULL || named_value == NULL ||
      bits->is_bit_field != CTOOL_TRUE || bits->bit_width != 3u ||
      wide_bits->is_bit_field != CTOOL_TRUE || wide_bits->bit_width != 32u) {
    (void)fprintf(stderr,
                  "pointer-expressions: aggregate member metadata differs\n");
    return 1;
  }
  if (unit->layout.members[values_index].byte_offset != 16u ||
      unit->layout.members[values_index].size != 16u ||
      unit->layout.members[values_index].alignment != 16u ||
      unit->layout.members[anonymous_index].byte_offset != 32u ||
      unit->layout.members[promoted_index].byte_offset != 0u ||
      unit->layout.members[bits_index].byte_offset != 40u ||
      unit->layout.members[wide_bits_index].byte_offset != 0u) {
    (void)fprintf(stderr,
                  "pointer-expressions: aggregate base layout differs\n");
    return 1;
  }
  if (unit->layout.members[packed_anonymous_index].byte_offset != 0u ||
      unit->layout.members[packed_anonymous_index].alignment != 1u ||
      unit->layout.members[packed_promoted_index].alignment != 4u ||
      unit->layout.members[named_index].byte_offset != 36u ||
      unit->layout.members[named_index].alignment != 1u ||
      unit->layout.members[named_value_index].alignment != 4u) {
    (void)fprintf(
        stderr,
        "pointer-expressions: packed member layout differs (%u/%u, %u, %u/%u, %u)\n",
        (unsigned)unit->layout.members[packed_anonymous_index].byte_offset,
        (unsigned)unit->layout.members[packed_anonymous_index].alignment,
        (unsigned)unit->layout.members[packed_promoted_index].alignment,
        (unsigned)unit->layout.members[named_index].byte_offset,
        (unsigned)unit->layout.members[named_index].alignment,
        (unsigned)unit->layout.members[named_value_index].alignment);
    return 1;
  }

  root = pointer_return_root(unit, "read_direct");
  if (pointer_expression_shape(unit, root, CTOOL_C_EXPRESSION_MEMBER,
                               CTOOL_C_EXPRESSION_OPERATOR_NONE, 1u) != 0 ||
      unit->expressions[root].reference != direct_index) {
    (void)fprintf(stderr,
                  "pointer-expressions: direct member expression differs\n");
    return 1;
  }
  root = pointer_return_root(unit, "read_arrow");
  child = scalar_expression_child(unit, &unit->expressions[root], 0u);
  if (pointer_expression_shape(unit, root, CTOOL_C_EXPRESSION_MEMBER,
                               CTOOL_C_EXPRESSION_OPERATOR_NONE, 1u) != 0 ||
      unit->expressions[root].reference != direct_index ||
      pointer_expression_shape(unit, child, CTOOL_C_EXPRESSION_UNARY,
                               CTOOL_C_EXPRESSION_OPERATOR_DEREFERENCE, 1u) !=
          0) {
    (void)fprintf(stderr,
                  "pointer-expressions: arrow normalization differs\n");
    return 1;
  }
  root = pointer_return_root(unit, "read_promoted");
  child = scalar_expression_child(unit, &unit->expressions[root], 0u);
  if (pointer_expression_shape(unit, root, CTOOL_C_EXPRESSION_MEMBER,
                               CTOOL_C_EXPRESSION_OPERATOR_NONE, 1u) != 0 ||
      unit->expressions[root].reference != promoted_index ||
      pointer_expression_shape(unit, child, CTOOL_C_EXPRESSION_MEMBER,
                               CTOOL_C_EXPRESSION_OPERATOR_NONE, 1u) != 0 ||
      unit->expressions[child].reference != anonymous_index) {
    (void)fprintf(stderr,
                  "pointer-expressions: anonymous member promotion differs\n");
    return 1;
  }
  root = pointer_return_root(unit, "read_locked");
  if (pointer_expression_shape(unit, root, CTOOL_C_EXPRESSION_MEMBER,
                               CTOOL_C_EXPRESSION_OPERATOR_NONE, 1u) != 0 ||
      unit->expressions[root].reference != locked_index ||
      scalar_type_kind(unit, unit->expressions[root].type, &qualifiers) !=
          CTOOL_C_TYPE_SIGNED_INT ||
      (qualifiers & CTOOL_C_QUAL_CONST) == 0u) {
    (void)fprintf(stderr,
                  "pointer-expressions: qualified member expression differs\n");
    return 1;
  }
  root = pointer_return_root(unit, "read_const");
  qualifiers = 0u;
  if (pointer_expression_shape(unit, root, CTOOL_C_EXPRESSION_MEMBER,
                               CTOOL_C_EXPRESSION_OPERATOR_NONE, 1u) != 0 ||
      unit->expressions[root].reference != direct_index ||
      scalar_type_kind(unit, unit->expressions[root].type, &qualifiers) !=
          CTOOL_C_TYPE_SIGNED_INT ||
      (qualifiers & CTOOL_C_QUAL_CONST) == 0u) {
    (void)fprintf(stderr,
                  "pointer-expressions: inherited const member differs\n");
    return 1;
  }
  root = pointer_return_root(unit, "read_volatile");
  qualifiers = 0u;
  if (pointer_expression_shape(unit, root, CTOOL_C_EXPRESSION_MEMBER,
                               CTOOL_C_EXPRESSION_OPERATOR_NONE, 1u) != 0 ||
      unit->expressions[root].reference != direct_index ||
      scalar_type_kind(unit, unit->expressions[root].type, &qualifiers) !=
          CTOOL_C_TYPE_SIGNED_INT ||
      (qualifiers & CTOOL_C_QUAL_VOLATILE) == 0u) {
    (void)fprintf(
        stderr,
        "pointer-expressions: inherited volatile member differs\n");
    return 1;
  }
  statement = scalar_return_statement(unit, "decay_const");
  root = statement == NULL ? CTOOL_C_AST_NONE : statement->expression;
  for (index = 0u;
       root < unit->expression_count &&
       unit->expressions[root].kind ==
           CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION &&
       unit->expressions[root].conversion !=
           CTOOL_C_CONVERSION_ARRAY_TO_POINTER &&
       index < unit->expression_count;
       index++) {
    root = scalar_expression_child(unit, &unit->expressions[root], 0u);
  }
  node = root < unit->expression_count ? &unit->expressions[root] : NULL;
  decay_pointer = node == NULL ? NULL : type_node(unit, node->type);
  child = node == NULL ? CTOOL_C_AST_NONE
                       : scalar_expression_child(unit, node, 0u);
  qualifiers = 0u;
  if (node == NULL ||
      node->kind != CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION ||
      node->conversion != CTOOL_C_CONVERSION_ARRAY_TO_POINTER ||
      decay_pointer == NULL || decay_pointer->kind != CTOOL_C_TYPE_POINTER ||
      scalar_type_kind(unit, decay_pointer->referenced_type, &qualifiers) !=
          CTOOL_C_TYPE_SIGNED_INT ||
      (qualifiers & CTOOL_C_QUAL_CONST) == 0u ||
      child >= unit->expression_count ||
      unit->expressions[child].kind != CTOOL_C_EXPRESSION_MEMBER ||
      unit->expressions[child].reference != values_index) {
    (void)fprintf(stderr,
                  "pointer-expressions: qualified array decay differs\n");
    return 1;
  }
  root = pointer_return_root(unit, "address_member");
  child = scalar_expression_child(unit, &unit->expressions[root], 0u);
  if (pointer_expression_shape(unit, root, CTOOL_C_EXPRESSION_UNARY,
                               CTOOL_C_EXPRESSION_OPERATOR_ADDRESS, 1u) != 0 ||
      pointer_expression_shape(unit, child, CTOOL_C_EXPRESSION_MEMBER,
                               CTOOL_C_EXPRESSION_OPERATOR_NONE, 1u) != 0 ||
      unit->expressions[child].reference != direct_index) {
    (void)fprintf(stderr,
                  "pointer-expressions: address-of member differs\n");
    return 1;
  }
  root = pointer_return_root(unit, "read_deref");
  if (pointer_expression_shape(unit, root, CTOOL_C_EXPRESSION_UNARY,
                               CTOOL_C_EXPRESSION_OPERATOR_DEREFERENCE, 1u) !=
      0) {
    (void)fprintf(stderr,
                  "pointer-expressions: dereference expression differs\n");
    return 1;
  }
  root = pointer_return_root(unit, "promote_bit_field");
  child = scalar_operator_child(unit, root, 0u);
  node = child < unit->expression_count ? &unit->expressions[child] : NULL;
  if (pointer_expression_shape(unit, root, CTOOL_C_EXPRESSION_BINARY,
                               CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT, 2u) != 0 ||
      scalar_type_kind(unit, unit->expressions[root].type, NULL) !=
          CTOOL_C_TYPE_SIGNED_INT ||
      node == NULL ||
      node->kind != CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION ||
      node->conversion != CTOOL_C_CONVERSION_INTEGER_PROMOTION ||
      scalar_type_kind(unit, node->type, NULL) != CTOOL_C_TYPE_SIGNED_INT) {
    (void)fprintf(stderr,
                  "pointer-expressions: bit-field promotion differs\n");
    return 1;
  }
  child = scalar_expression_child(unit, node, 0u);
  node = child < unit->expression_count ? &unit->expressions[child] : NULL;
  index = node == NULL ? CTOOL_C_AST_NONE
                       : scalar_expression_child(unit, node, 0u);
  if (node == NULL ||
      node->kind != CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION ||
      node->conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      scalar_type_kind(unit, node->type, NULL) != CTOOL_C_TYPE_UNSIGNED_INT ||
      index >= unit->expression_count ||
      unit->expressions[index].kind != CTOOL_C_EXPRESSION_MEMBER ||
      unit->expressions[index].reference != bits_index) {
    (void)fprintf(
        stderr,
        "pointer-expressions: narrow bit-field conversion order differs\n");
    return 1;
  }
  root = pointer_return_root(unit, "preserve_wide_bit_field");
  child = scalar_operator_child(unit, root, 0u);
  node = child < unit->expression_count ? &unit->expressions[child] : NULL;
  index = node == NULL ? CTOOL_C_AST_NONE
                       : scalar_expression_child(unit, node, 0u);
  if (pointer_expression_shape(unit, root, CTOOL_C_EXPRESSION_UNARY,
                               CTOOL_C_EXPRESSION_OPERATOR_UNARY_PLUS, 1u) !=
          0 ||
      scalar_type_kind(unit, unit->expressions[root].type, NULL) !=
          CTOOL_C_TYPE_UNSIGNED_INT ||
      node == NULL ||
      node->kind != CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION ||
      node->conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      scalar_type_kind(unit, node->type, NULL) != CTOOL_C_TYPE_UNSIGNED_INT ||
      scalar_conversion_chain_has(unit, child,
                                  CTOOL_C_CONVERSION_INTEGER_PROMOTION) ==
          CTOOL_TRUE ||
      index >= unit->expression_count ||
      unit->expressions[index].kind != CTOOL_C_EXPRESSION_MEMBER ||
      unit->expressions[index].reference != wide_bits_index) {
    (void)fprintf(stderr,
                  "pointer-expressions: full-width bit-field promotion differs\n");
    return 1;
  }

  statement = pointer_expression_statement(unit, "assign_deref");
  root = statement == NULL ? CTOOL_C_AST_NONE : statement->expression;
  child = root < unit->expression_count
              ? scalar_expression_child(unit, &unit->expressions[root], 0u)
              : CTOOL_C_AST_NONE;
  if (pointer_expression_shape(unit, root, CTOOL_C_EXPRESSION_ASSIGNMENT,
                               CTOOL_C_EXPRESSION_OPERATOR_ASSIGN, 2u) != 0 ||
      pointer_expression_shape(unit, child, CTOOL_C_EXPRESSION_UNARY,
                               CTOOL_C_EXPRESSION_OPERATOR_DEREFERENCE, 1u) !=
          0) {
    (void)fprintf(stderr,
                  "pointer-expressions: dereference assignment differs\n");
    return 1;
  }
  statement = pointer_expression_statement(unit, "assign_member");
  root = statement == NULL ? CTOOL_C_AST_NONE : statement->expression;
  child = root < unit->expression_count
              ? scalar_expression_child(unit, &unit->expressions[root], 0u)
              : CTOOL_C_AST_NONE;
  if (pointer_expression_shape(unit, root, CTOOL_C_EXPRESSION_ASSIGNMENT,
                               CTOOL_C_EXPRESSION_OPERATOR_ASSIGN, 2u) != 0 ||
      pointer_expression_shape(unit, child, CTOOL_C_EXPRESSION_MEMBER,
                               CTOOL_C_EXPRESSION_OPERATOR_NONE, 1u) != 0 ||
      unit->expressions[child].reference != direct_index) {
    (void)fprintf(stderr,
                  "pointer-expressions: member assignment differs\n");
    return 1;
  }

  for (index = 0u; index < 3u; index++) {
    static const char *const cast_functions[] = {
        "cast_null", "cast_void", "cast_integer"};
    root = pointer_return_root(unit, cast_functions[index]);
    node = root < unit->expression_count ? &unit->expressions[root] : NULL;
    child = node == NULL ? CTOOL_C_AST_NONE
                         : scalar_expression_child(unit, node, 0u);
    if (node == NULL || node->kind != CTOOL_C_EXPRESSION_CAST ||
        node->child_count != 1u || node->operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
        node->conversion != CTOOL_C_CONVERSION_NONE ||
        node->computation_type != CTOOL_C_TYPE_NONE ||
        node->reference != CTOOL_C_AST_NONE || child >= root) {
      (void)fprintf(stderr,
                    "pointer-expressions: explicit cast %u differs\n", index);
      return 1;
    }
  }
  root = pointer_return_root(unit, "cast_null");
  child = scalar_expression_child(unit, &unit->expressions[root], 0u);
  if (child >= unit->expression_count ||
      unit->expressions[child].kind != CTOOL_C_EXPRESSION_INTEGER_CONSTANT ||
      unit->expressions[child].integer_bits != 0ull) {
    (void)fprintf(stderr, "pointer-expressions: null cast child differs\n");
    return 1;
  }

  {
    static const char *const discard_functions[] = {
        "discard_volatile", "discard_aggregate", "discard_array",
        "discard_function", "discard_float"};
    static const ctool_c_conversion_kind_t discard_conversions[] = {
        CTOOL_C_CONVERSION_LVALUE_TO_VALUE,
        CTOOL_C_CONVERSION_LVALUE_TO_VALUE,
        CTOOL_C_CONVERSION_ARRAY_TO_POINTER,
        CTOOL_C_CONVERSION_FUNCTION_TO_POINTER,
        CTOOL_C_CONVERSION_LVALUE_TO_VALUE};
    for (index = 0u; index < ARRAY_COUNT(discard_functions); index++) {
      ctool_u32 source;
      statement = pointer_expression_statement(unit, discard_functions[index]);
      root = statement == NULL ? CTOOL_C_AST_NONE : statement->expression;
      child = root < unit->expression_count
                  ? scalar_expression_child(unit, &unit->expressions[root], 0u)
                  : CTOOL_C_AST_NONE;
      source = child < unit->expression_count
                   ? scalar_expression_child(unit, &unit->expressions[child],
                                             0u)
                   : CTOOL_C_AST_NONE;
      if (pointer_expression_shape(unit, root, CTOOL_C_EXPRESSION_CAST,
                                   CTOOL_C_EXPRESSION_OPERATOR_NONE, 1u) != 0 ||
          scalar_type_kind(unit, unit->expressions[root].type, NULL) !=
              CTOOL_C_TYPE_VOID ||
          child >= unit->expression_count ||
          unit->expressions[child].kind !=
              CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION ||
          unit->expressions[child].conversion != discard_conversions[index] ||
          source >= child) {
        (void)fprintf(stderr,
                      "pointer-expressions: void discard %u differs\n",
                      index);
        return 1;
      }
      if (index == 0u) {
        qualifiers = 0u;
        if (scalar_type_kind(unit, unit->expressions[source].type,
                             &qualifiers) != CTOOL_C_TYPE_SIGNED_INT ||
            (qualifiers & CTOOL_C_QUAL_VOLATILE) == 0u) {
          (void)fprintf(
              stderr,
              "pointer-expressions: volatile discard source differs\n");
          return 1;
        }
      }
      if (index == 4u) {
        qualifiers = 0u;
        if (scalar_type_kind(unit, unit->expressions[source].type,
                             &qualifiers) != CTOOL_C_TYPE_FLOAT ||
            (qualifiers & CTOOL_C_QUAL_VOLATILE) == 0u) {
          (void)fprintf(
              stderr,
              "pointer-expressions: volatile float discard source differs\n");
          return 1;
        }
      }
    }
  }

  if (pointer_folded_return(unit, "size_array", 16ull) != 0 ||
      pointer_folded_return(unit, "size_ub", 4ull) != 0 ||
      pointer_folded_return(unit, "size_string", 4ull) != 0 ||
      pointer_folded_return(unit, "type_alignment", 16ull) != 0 ||
      pointer_folded_return(unit, "expression_alignment", 16ull) != 0 ||
      pointer_folded_return(unit, "string_alignment", 1ull) != 0 ||
      pointer_folded_return(unit, "offset_values", 16ull) != 0 ||
      pointer_folded_return(unit, "offset_promoted", 32ull) != 0 ||
      pointer_folded_return(unit, "promoted_alignment", 4ull) != 0 ||
      pointer_folded_return(unit, "named_alignment", 4ull) != 0 ||
      pointer_folded_return(unit, "aligned_member_alignment", 4ull) != 0 ||
      pointer_folded_return(unit, "aligned_object_alignment", 64ull) != 0) {
    (void)fprintf(stderr,
                  "pointer-expressions: folded query result differs\n");
    return 1;
  }

  for (index = 0u; index < unit->expression_count; index++) {
    node = &unit->expressions[index];
    if (node->child_count == 0u) {
      if (node->first_child != CTOOL_C_AST_NONE) {
        (void)fprintf(
            stderr,
            "pointer-expressions: leaf expression child slice differs\n");
        return 1;
      }
      continue;
    }
    if (node->first_child > unit->expression_child_count ||
        node->child_count >
            unit->expression_child_count - node->first_child) {
      (void)fprintf(stderr,
                    "pointer-expressions: expression child slice differs\n");
      return 1;
    }
    for (child = 0u; child < node->child_count; child++) {
      if (unit->expression_children[node->first_child + child] >= index) {
        (void)fprintf(stderr,
                      "pointer-expressions: expression order differs\n");
        return 1;
      }
    }
  }
  return 0;
}

static char *build_unevaluated_string_scale(void) {
  static const char define_prefix[] = "#define QUERY_STRING \"";
  static const char define_suffix[] = "\"\n";
  static const char query[] =
      "_Static_assert(sizeof(QUERY_STRING) == 16385, \"query\");\n";
  const ctool_u32 query_count = 512u;
  const size_t literal_size = 16384u;
  size_t capacity;
  size_t used = 0u;
  char *source;
  ctool_u32 index;
  if ((size_t)query_count >
      ((size_t)-1 - literal_size - sizeof(define_prefix) -
       sizeof(define_suffix)) /
          (sizeof(query) - 1u)) {
    return NULL;
  }
  capacity = sizeof(define_prefix) - 1u + literal_size +
             sizeof(define_suffix) - 1u +
             (size_t)query_count * (sizeof(query) - 1u) + 1u;
  source = (char *)malloc(capacity);
  if (source == NULL) {
    return NULL;
  }
  source[0] = '\0';
  if (append_scale_text(source, capacity, &used, define_prefix) != 0 ||
      literal_size >= capacity - used) {
    free(source);
    return NULL;
  }
  (void)memset(source + used, 'x', literal_size);
  used += literal_size;
  source[used] = '\0';
  if (append_scale_text(source, capacity, &used, define_suffix) != 0) {
    free(source);
    return NULL;
  }
  for (index = 0u; index < query_count; index++) {
    if (append_scale_text(source, capacity, &used, query) != 0) {
      free(source);
      return NULL;
    }
  }
  return source;
}

static int validate_unevaluated_string_arena(const char *host_root) {
  frontend_fixture_t fixture;
  ctool_c_translation_unit_t unit;
  char *source = build_unevaluated_string_scale();
  int failed = 1;
  if (source == NULL) {
    (void)fprintf(stderr,
                  "pointer-expressions: string scale source failed\n");
    return 1;
  }
  if (begin_frontend_fixture(&fixture, "pointer-expression-string-arena",
                             host_root, 3u * 1024u * 1024u) != 0) {
    free(source);
    return 1;
  }
  if (parse_valid_fixture(&fixture, "/unevaluated-string-scale.c", source,
                          &unit) == 0 &&
      unit.binding_count == 0u && unit.tag_count == 0u &&
      unit.function_definition_count == 0u && unit.statement_count == 0u &&
      unit.expression_count == 0u && unit.expression_child_count == 0u &&
      validate_anchor(&fixture) == 0) {
    failed = 0;
  } else {
    (void)fprintf(
        stderr,
        "pointer-expressions: unevaluated string arena regression failed\n");
  }
  free(source);
  if (finish_frontend_fixture(&fixture) != 0) {
    failed = 1;
  }
  return failed;
}

static int run_pointer_expressions(const char *host_root) {
  static const char source[] =
      "typedef struct aggregate {\n"
      "  int direct;\n"
      "  const int locked;\n"
      "  int values[4] __attribute__((aligned(16)));\n"
      "  struct { int promoted; };\n"
      "  struct { int named_value; } named __attribute__((packed));\n"
      "  unsigned int bits : 3;\n"
      "} aggregate_t;\n"
      "typedef struct __attribute__((packed)) { struct { int packed_promoted; }; } promoted_pack_t;\n"
      "typedef struct { unsigned int wide_bits : 32; } wide_bits_t;\n"
      "aggregate_t aligned_object __attribute__((aligned(64)));\n"
      "int read_direct(aggregate_t value) { return value.direct; }\n"
      "int read_arrow(aggregate_t *value) { return value->direct; }\n"
      "int read_promoted(aggregate_t *value) { return value->promoted; }\n"
      "int read_locked(aggregate_t *value) { return value->locked; }\n"
      "int read_const(const aggregate_t *value) { return value->direct; }\n"
      "int read_volatile(volatile aggregate_t *value) { return value->direct; }\n"
      "const int *decay_const(const aggregate_t *value) { return value->values; }\n"
      "int *address_member(aggregate_t *value) { return &value->direct; }\n"
      "int read_deref(int *value) { return *value; }\n"
      "void assign_deref(int *value) { *value = 4; return; }\n"
      "void assign_member(aggregate_t *value) { value->direct = 5; return; }\n"
      "aggregate_t *cast_null(void) { return (aggregate_t *)0; }\n"
      "void *cast_void(aggregate_t *value) { return (void *)value; }\n"
      "unsigned int cast_integer(signed char value) { return (unsigned int)value; }\n"
      "void discard_volatile(volatile int *value) { (void)*value; }\n"
      "void discard_aggregate(aggregate_t *value) { (void)*value; }\n"
      "void discard_array(aggregate_t *value) { (void)value->values; }\n"
      "void discard_function(void) { (void)read_direct; }\n"
      "void discard_float(volatile float *value) { (void)*value; }\n"
      "int promote_bit_field(aggregate_t value) { return value.bits - 2; }\n"
      "unsigned int preserve_wide_bit_field(wide_bits_t value) { return +value.wide_bits; }\n"
      "unsigned int size_array(aggregate_t *value) { return sizeof(value->values); }\n"
      "unsigned int size_ub(void) { return sizeof(1 / 0); }\n"
      "unsigned int size_string(void) { return sizeof(\"abc\"); }\n"
      "unsigned int type_alignment(void) { return _Alignof(aggregate_t); }\n"
      "unsigned int expression_alignment(aggregate_t *value) { return __alignof__(value->values); }\n"
      "unsigned int string_alignment(void) { return __alignof__(\"abc\"); }\n"
      "unsigned int offset_values(void) { return __builtin_offsetof(aggregate_t, values); }\n"
      "unsigned int offset_promoted(void) { return __builtin_offsetof(aggregate_t, promoted); }\n"
      "unsigned int promoted_alignment(promoted_pack_t *value) { return __alignof__(value->packed_promoted); }\n"
      "unsigned int named_alignment(aggregate_t *value) { return __alignof__(value->named.named_value); }\n"
      "unsigned int aligned_member_alignment(void) { return __alignof__(aligned_object.direct); }\n"
      "unsigned int aligned_object_alignment(void) { return __alignof__(aligned_object); }\n";
  static const frontend_exact_failure_case_t failure_cases[] = {
      {{"dot scalar operand",
        "int bad(int value) { return value.member; }\n", CTOOL_ERR_INPUT,
        CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u, "member access requires a complete record or union"},
      {{"arrow aggregate operand",
        "struct value { int member; }; int bad(struct value value) { return value->member; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u,
       "arrow member access requires a pointer to a record or union"},
      {{"arrow scalar pointer operand",
        "int bad(int *value) { return value->member; }\n", CTOOL_ERR_INPUT,
        CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u,
       "member access requires a complete record or union"},
      {{"missing member",
        "struct value { int member; }; int bad(struct value *value) { return value->missing; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u, "record or union has no member with this name"},
      {{"address bit-field",
        "struct value { unsigned int bits : 3; }; unsigned int *bad(struct value *value) { return &value->bits; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u, "address operator cannot apply to a bit-field"},
      {{"address register object",
        "int *bad(register int value) { return &value; }\n", CTOOL_ERR_INPUT,
        CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u, "address operator cannot apply to a register object"},
      {{"address register promoted member",
        "struct value { struct { int member; }; }; int *bad(register struct value value) { return &value.member; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u, "address operator cannot apply to a register object"},
      {{"address rvalue",
        "int *bad(int value) { return &(value + 1); }\n", CTOOL_ERR_INPUT,
        CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u,
       "address operator requires an object lvalue or function designator"},
      {{"dereference nonpointer", "int bad(int value) { return *value; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u, "dereference operator requires a pointer operand"},
      {{"dereference void pointer",
        "int bad(void *value) { return *value; }\n", CTOOL_ERR_INPUT,
        CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u,
       "dereference operator requires a pointer to an object or function"},
      {{"aggregate cast source",
        "struct value { int member; }; int bad(struct value value) { return (int)value; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u, "cast operand must have scalar type"},
      {{"aggregate cast destination",
        "struct value { int member; }; int bad(int value) { return (struct value)value; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u, "cast destination must have scalar or void type"},
      {{"floating cast",
        "int bad(float value) { return (int)value; }\n", CTOOL_ERR_UNSUPPORTED,
        CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u, "floating casts are outside this expression slice"},
      {{"sizeof incomplete expression",
        "struct pending; unsigned int bad(struct pending *value) { return sizeof(*value); }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u, "sizeof requires a complete object type"},
      {{"sizeof function expression",
        "int target(void); unsigned int bad(void) { return sizeof(target); }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u, "sizeof requires a complete object type"},
      {{"sizeof bit-field",
        "struct value { unsigned int bits : 3; }; unsigned int bad(struct value value) { return sizeof(value.bits); }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u, "sizeof cannot apply to a bit-field"},
      {{"offsetof non-record",
        "unsigned int bad(void) { return __builtin_offsetof(int, value); }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u, "offsetof requires a complete record or union type"},
      {{"offsetof missing member",
        "struct value { int member; }; unsigned int bad(void) { return __builtin_offsetof(struct value, missing); }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u, "record or union has no member with this name"},
      {{"offsetof bit-field",
        "struct value { unsigned int bits : 3; }; unsigned int bad(void) { return __builtin_offsetof(struct value, bits); }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u, "offsetof cannot apply to a bit-field"},
      {{"offsetof digraph array designator boundary",
        "struct value { int values[2]; }; unsigned int bad(void) { return __builtin_offsetof(struct value, values<:0:>); }\n",
        CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u,
       "offsetof array designators are outside this expression slice"},
      {{"offsetof invalid designator",
        "struct value { int values[2]; }; unsigned int bad(void) { return __builtin_offsetof(struct value, values + 1); }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u, "builtin offsetof member designator is invalid"}};
  static const frontend_exact_failure_case_t disabled_gnu_case = {
      {"GNU alignof disabled",
       "unsigned int bad(int value) { return __alignof__(value); }\n",
       CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_EXPRESSION},
      0u, 0u, "GNU alignment queries require GNU extensions"};
  frontend_fixture_t fixture;
  ctool_c_translation_unit_t unit;
  ctool_u32 index;
  int failed = 1;

  if (begin_frontend_fixture(&fixture, "pointer-expressions", host_root,
                             32u * 1024u * 1024u) != 0) {
    return 1;
  }
  if (parse_valid_fixture(&fixture, "/pointer-expressions.c", source, &unit) !=
          0 ||
      validate_pointer_expression_unit(&unit) != 0) {
    goto cleanup;
  }
  for (index = 0u; index < ARRAY_COUNT(failure_cases); index++) {
    const frontend_exact_failure_case_t *test_case = &failure_cases[index];
    if (expect_frontend_failure_at_message(
            &fixture, &test_case->failure, "/pointer-expression-failure.c",
            test_case->line, test_case->column, test_case->message) != 0 ||
        validate_pointer_expression_unit(&unit) != 0) {
      goto cleanup;
    }
  }
  fixture.pp_request.gnu_extensions = CTOOL_FALSE;
  fixture.parse_request.gnu_extensions = CTOOL_FALSE;
  if (expect_frontend_failure_at_message(
          &fixture, &disabled_gnu_case.failure,
          "/pointer-expression-gnu-disabled.c", disabled_gnu_case.line,
          disabled_gnu_case.column, disabled_gnu_case.message) != 0 ||
      validate_pointer_expression_unit(&unit) != 0) {
    goto cleanup;
  }
  fixture.pp_request.gnu_extensions = CTOOL_TRUE;
  fixture.parse_request.gnu_extensions = CTOOL_TRUE;
  if (validate_unevaluated_string_arena(host_root) != 0) {
    goto cleanup;
  }
  failed = 0;

cleanup:
  fixture.pp_request.gnu_extensions = CTOOL_TRUE;
  fixture.parse_request.gnu_extensions = CTOOL_TRUE;
  if (finish_frontend_fixture(&fixture) != 0) {
    failed = 1;
  }
  if (failed == 0) {
    (void)printf("pointer-expressions: ok\n");
  }
  return failed;
}

static int validate_pointer_arithmetic_unit(
    const ctool_c_translation_unit_t *unit) {
  static const char *const subscript_names[] = {
      "read_index", "read_reverse"};
  ctool_u32 root = pointer_return_root(unit, "advance");
  ctool_u32 child;
  ctool_u32 subscript_index;
  const ctool_c_expression_t *addition =
      root < unit->expression_count ? &unit->expressions[root] : NULL;
  const ctool_c_expression_t *expression;
  const ctool_c_type_node_t *result =
      addition == NULL ? NULL : type_node(unit, addition->type);
  if (unit->function_definition_count != 6u || addition == NULL ||
      addition->kind != CTOOL_C_EXPRESSION_BINARY ||
      addition->operation != CTOOL_C_EXPRESSION_OPERATOR_ADD ||
      addition->child_count != 2u || result == NULL ||
      result->kind != CTOOL_C_TYPE_POINTER ||
      scalar_type_kind(unit, result->referenced_type, NULL) !=
          CTOOL_C_TYPE_SIGNED_INT) {
    (void)fprintf(stderr,
                  "pointer-arithmetic: pointer addition AST differs\n");
    return 1;
  }

  root = pointer_return_root(unit, "retreat");
  expression =
      root < unit->expression_count ? &unit->expressions[root] : NULL;
  result = expression == NULL ? NULL : type_node(unit, expression->type);
  if (expression == NULL || expression->kind != CTOOL_C_EXPRESSION_BINARY ||
      expression->operation != CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT ||
      expression->child_count != 2u || result == NULL ||
      result->kind != CTOOL_C_TYPE_POINTER) {
    (void)fprintf(stderr,
                  "pointer-arithmetic: pointer subtraction AST differs\n");
    return 1;
  }

  root = pointer_return_root(unit, "distance");
  expression =
      root < unit->expression_count ? &unit->expressions[root] : NULL;
  if (expression == NULL || expression->kind != CTOOL_C_EXPRESSION_BINARY ||
      expression->operation != CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT ||
      expression->child_count != 2u ||
      scalar_type_kind(unit, expression->type, NULL) !=
          CTOOL_C_TYPE_SIGNED_INT) {
    (void)fprintf(stderr,
                  "pointer-arithmetic: pointer difference AST differs\n");
    return 1;
  }

  for (subscript_index = 0u;
       subscript_index < ARRAY_COUNT(subscript_names); subscript_index++) {
    root = pointer_return_root(unit, subscript_names[subscript_index]);
    expression =
        root < unit->expression_count ? &unit->expressions[root] : NULL;
    if (expression != NULL &&
        expression->kind == CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION) {
      root = scalar_expression_child(unit, expression, 0u);
      expression =
          root < unit->expression_count ? &unit->expressions[root] : NULL;
    }
    child = expression == NULL
                ? CTOOL_C_AST_NONE
                : scalar_expression_child(unit, expression, 0u);
    if (expression == NULL ||
        expression->kind != CTOOL_C_EXPRESSION_UNARY ||
        expression->operation != CTOOL_C_EXPRESSION_OPERATOR_DEREFERENCE ||
        expression->child_count != 1u || child >= unit->expression_count ||
        unit->expressions[child].kind != CTOOL_C_EXPRESSION_BINARY ||
        unit->expressions[child].operation !=
            CTOOL_C_EXPRESSION_OPERATOR_ADD) {
      (void)fprintf(
          stderr,
          "pointer-arithmetic: subscript normalization %u differs\n",
          (unsigned int)subscript_index);
      return 1;
    }
  }

  {
    const ctool_c_statement_t *return_statement =
        scalar_return_statement(unit, "select_row");
    root = return_statement == NULL ? CTOOL_C_AST_NONE
                                    : return_statement->expression;
  }
  expression =
      root < unit->expression_count ? &unit->expressions[root] : NULL;
  if (expression == NULL ||
      expression->kind != CTOOL_C_EXPRESSION_IMPLICIT_CONVERSION ||
      expression->conversion != CTOOL_C_CONVERSION_ARRAY_TO_POINTER ||
      type_node(unit, expression->type) == NULL ||
      type_node(unit, expression->type)->kind != CTOOL_C_TYPE_POINTER) {
    (void)fprintf(stderr,
                  "pointer-arithmetic: subscript array decay differs\n");
    return 1;
  }
  return 0;
}

static int run_pointer_arithmetic(const char *host_root) {
  static const char source[] =
      "int *advance(int *pointer, int index) { return pointer + index; }\n"
      "int *retreat(int *pointer, short index) { return pointer - index; }\n"
      "int distance(int *end, const int *begin) { return end - begin; }\n"
      "int read_index(int *pointer, unsigned char index) { return pointer[index:>; }\n"
      "int read_reverse(int *pointer, unsigned char index) { return index<:pointer]; }\n"
      "const int *select_row(const int (*rows)<:4:>, int index) { return rows<:index:>; }\n";
  static const frontend_exact_failure_case_t failure_cases[] = {
      {{"void pointer addition",
        "void *bad(void *pointer) { return pointer + 1; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u,
       "pointer arithmetic requires a pointer to a complete object type"},
      {{"function pointer addition",
        "int target(void); int (*bad(int (*pointer)(void)))(void) { return pointer + 1; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u,
       "pointer arithmetic requires a pointer to a complete object type"},
      {{"incomplete pointer subtraction",
        "struct pending; struct pending *bad(struct pending *pointer) { return pointer - 1; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u,
       "pointer arithmetic requires a pointer to a complete object type"},
      {{"two-pointer addition",
        "int *bad(int *left, int *right) { return left + right; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u,
       "pointer addition requires one pointer and one integer operand"},
      {{"integer minus pointer",
        "int *bad(int value, int *pointer) { return value - pointer; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u,
       "pointer subtraction requires a pointer left operand and an integer right operand"},
      {{"incompatible pointer difference",
        "int bad(int *left, long *right) { return left - right; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u,
       "pointer subtraction requires compatible pointed-to types"},
      {{"non-pointer digraph subscript",
        "int bad(int left, int right) { return left<:right:>; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u,
       "subscript requires one pointer and one integer operand"},
      {{"unterminated digraph subscript",
        "int bad(int *pointer, int index) { return pointer<:index; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPECTED_TOKEN},
       1u, 57u, "expected source token is missing"}};
  frontend_fixture_t fixture;
  ctool_c_translation_unit_t unit;
  ctool_u32 index;
  int failed = 1;

  if (begin_frontend_fixture(&fixture, "pointer-arithmetic", host_root,
                             16u * 1024u * 1024u) != 0) {
    return 1;
  }
  fixture.pp_request.gnu_extensions = CTOOL_FALSE;
  fixture.parse_request.gnu_extensions = CTOOL_FALSE;
  if (parse_valid_fixture(&fixture, "/pointer-arithmetic.c", source, &unit) ==
          0 &&
      validate_pointer_arithmetic_unit(&unit) == 0) {
    failed = 0;
  }
  for (index = 0u; failed == 0 && index < ARRAY_COUNT(failure_cases);
       index++) {
    const frontend_exact_failure_case_t *test_case = &failure_cases[index];
    if (expect_frontend_failure_at_message(
            &fixture, &test_case->failure, "/pointer-arithmetic-failure.c",
            test_case->line, test_case->column, test_case->message) != 0 ||
        validate_pointer_arithmetic_unit(&unit) != 0) {
      failed = 1;
    }
  }
  if (finish_frontend_fixture(&fixture) != 0) {
    failed = 1;
  }
  if (failed == 0) {
    (void)printf("pointer-arithmetic: ok\n");
  }
  return failed;
}

static int validate_scalar_update_unit(
    const ctool_c_translation_unit_t *unit) {
  static const char *const function_names[] = {
      "multiply_assign", "divide_assign", "remainder_assign",
      "add_assign",      "subtract_assign", "shift_left_assign",
      "shift_right_assign", "and_assign", "xor_assign", "or_assign"};
  static const ctool_c_expression_operator_t operations[] = {
      CTOOL_C_EXPRESSION_OPERATOR_MULTIPLY_ASSIGN,
      CTOOL_C_EXPRESSION_OPERATOR_DIVIDE_ASSIGN,
      CTOOL_C_EXPRESSION_OPERATOR_REMAINDER_ASSIGN,
      CTOOL_C_EXPRESSION_OPERATOR_ADD_ASSIGN,
      CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT_ASSIGN,
      CTOOL_C_EXPRESSION_OPERATOR_SHIFT_LEFT_ASSIGN,
      CTOOL_C_EXPRESSION_OPERATOR_SHIFT_RIGHT_ASSIGN,
      CTOOL_C_EXPRESSION_OPERATOR_BITWISE_AND_ASSIGN,
      CTOOL_C_EXPRESSION_OPERATOR_BITWISE_XOR_ASSIGN,
      CTOOL_C_EXPRESSION_OPERATOR_BITWISE_OR_ASSIGN};
  static const char *const update_names[] = {
      "prefix_increment", "prefix_decrement", "postfix_increment",
      "postfix_decrement"};
  static const ctool_c_expression_operator_t update_operations[] = {
      CTOOL_C_EXPRESSION_OPERATOR_PREFIX_INCREMENT,
      CTOOL_C_EXPRESSION_OPERATOR_PREFIX_DECREMENT,
      CTOOL_C_EXPRESSION_OPERATOR_POSTFIX_INCREMENT,
      CTOOL_C_EXPRESSION_OPERATOR_POSTFIX_DECREMENT};
  const ctool_c_statement_t *statement =
      pointer_expression_statement(unit, "add_assign");
  const ctool_c_expression_t *assignment =
      statement == NULL || statement->expression >= unit->expression_count
          ? NULL
          : &unit->expressions[statement->expression];
  ctool_u32 left = assignment == NULL
                       ? CTOOL_C_AST_NONE
                       : scalar_expression_child(unit, assignment, 0u);
  ctool_u32 call_count = 0u;
  ctool_u32 qualifiers;
  ctool_u32 index;
  if (unit->function_definition_count != 26u || assignment == NULL ||
      assignment->kind != CTOOL_C_EXPRESSION_ASSIGNMENT ||
      assignment->operation != CTOOL_C_EXPRESSION_OPERATOR_ADD_ASSIGN ||
      assignment->child_count != 2u ||
      scalar_type_kind(unit, assignment->type, NULL) !=
          CTOOL_C_TYPE_SIGNED_INT ||
      scalar_type_kind(unit, assignment->computation_type, NULL) !=
          CTOOL_C_TYPE_SIGNED_INT ||
      left >= unit->expression_count ||
      unit->expressions[left].kind != CTOOL_C_EXPRESSION_UNARY ||
      unit->expressions[left].operation !=
          CTOOL_C_EXPRESSION_OPERATOR_DEREFERENCE) {
    (void)fprintf(stderr, "scalar-updates: add assignment AST differs\n");
    return 1;
  }
  for (index = 0u; index < ARRAY_COUNT(function_names); index++) {
    statement = pointer_expression_statement(unit, function_names[index]);
    assignment =
        statement == NULL || statement->expression >= unit->expression_count
            ? NULL
            : &unit->expressions[statement->expression];
    if (assignment == NULL ||
        assignment->kind != CTOOL_C_EXPRESSION_ASSIGNMENT ||
        assignment->operation != operations[index] ||
        assignment->child_count != 2u ||
        scalar_type_kind(unit, assignment->type, NULL) !=
            CTOOL_C_TYPE_SIGNED_INT ||
        scalar_type_kind(unit, assignment->computation_type, NULL) !=
            CTOOL_C_TYPE_SIGNED_INT) {
      (void)fprintf(stderr,
                    "scalar-updates: compound assignment %u differs\n",
                    (unsigned)index);
      return 1;
    }
  }
  statement = scalar_return_statement(unit, "narrow_add_assign");
  assignment =
      statement == NULL || statement->expression >= unit->expression_count
          ? NULL
          : &unit->expressions[statement->expression];
  if (assignment == NULL ||
      assignment->kind != CTOOL_C_EXPRESSION_ASSIGNMENT ||
      assignment->operation != CTOOL_C_EXPRESSION_OPERATOR_ADD_ASSIGN ||
      scalar_type_kind(unit, assignment->type, NULL) !=
          CTOOL_C_TYPE_UNSIGNED_CHAR ||
      scalar_type_kind(unit, assignment->computation_type, NULL) !=
          CTOOL_C_TYPE_UNSIGNED_INT) {
    (void)fprintf(stderr,
                  "scalar-updates: narrow compound computation differs\n");
    return 1;
  }
  for (index = 0u; index < ARRAY_COUNT(update_names); index++) {
    const ctool_c_expression_t *update;
    ctool_u32 update_child;
    statement = scalar_return_statement(unit, update_names[index]);
    update = statement == NULL ||
                     statement->expression >= unit->expression_count
                 ? NULL
                 : &unit->expressions[statement->expression];
    update_child = update == NULL
                       ? CTOOL_C_AST_NONE
                       : scalar_expression_child(unit, update, 0u);
    if (update == NULL || update->kind != CTOOL_C_EXPRESSION_UPDATE ||
        update->operation != update_operations[index] ||
        update->child_count != 1u ||
        scalar_type_kind(unit, update->type, NULL) !=
            CTOOL_C_TYPE_SIGNED_INT ||
        scalar_type_kind(unit, update->computation_type, NULL) !=
            CTOOL_C_TYPE_SIGNED_INT ||
        update_child >= unit->expression_count ||
        unit->expressions[update_child].kind != CTOOL_C_EXPRESSION_UNARY ||
        unit->expressions[update_child].operation !=
            CTOOL_C_EXPRESSION_OPERATOR_DEREFERENCE) {
      (void)fprintf(stderr, "scalar-updates: update %u differs\n",
                    (unsigned)index);
      return 1;
    }
  }
  statement = scalar_return_statement(unit, "narrow_postfix");
  assignment =
      statement == NULL || statement->expression >= unit->expression_count
          ? NULL
          : &unit->expressions[statement->expression];
  if (assignment == NULL || assignment->kind != CTOOL_C_EXPRESSION_UPDATE ||
      assignment->operation != CTOOL_C_EXPRESSION_OPERATOR_POSTFIX_INCREMENT ||
      scalar_type_kind(unit, assignment->type, NULL) !=
          CTOOL_C_TYPE_UNSIGNED_CHAR ||
      scalar_type_kind(unit, assignment->computation_type, NULL) !=
          CTOOL_C_TYPE_SIGNED_INT) {
    (void)fprintf(stderr, "scalar-updates: narrow update differs\n");
    return 1;
  }
  for (index = 0u; index < 2u; index++) {
    static const char *const pointer_names[] = {
        "pointer_prefix", "pointer_postfix"};
    const ctool_c_expression_t *update;
    const ctool_c_type_node_t *result_type;
    const ctool_c_type_node_t *computation_type;
    statement = scalar_return_statement(unit, pointer_names[index]);
    update = statement == NULL ||
                     statement->expression >= unit->expression_count
                 ? NULL
                 : &unit->expressions[statement->expression];
    result_type = update == NULL ? NULL : type_node(unit, update->type);
    computation_type =
        update == NULL ? NULL : type_node(unit, update->computation_type);
    if (update == NULL || update->kind != CTOOL_C_EXPRESSION_UPDATE ||
        result_type == NULL || result_type->kind != CTOOL_C_TYPE_POINTER ||
        computation_type == NULL ||
        computation_type->kind != CTOOL_C_TYPE_POINTER) {
      (void)fprintf(stderr, "scalar-updates: pointer update %u differs\n",
                    (unsigned)index);
      return 1;
    }
  }
  for (index = 0u; index < 2u; index++) {
    static const char *const pointer_assignment_names[] = {
        "pointer_add_assign", "pointer_subtract_assign"};
    static const ctool_c_expression_operator_t pointer_assignment_operations[] = {
        CTOOL_C_EXPRESSION_OPERATOR_ADD_ASSIGN,
        CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT_ASSIGN};
    const ctool_c_expression_t *pointer_assignment;
    const ctool_c_type_node_t *result_type;
    const ctool_c_type_node_t *computation_type;
    statement = pointer_expression_statement(
        unit, pointer_assignment_names[index]);
    pointer_assignment =
        statement == NULL || statement->expression >= unit->expression_count
            ? NULL
            : &unit->expressions[statement->expression];
    result_type = pointer_assignment == NULL
                      ? NULL
                      : type_node(unit, pointer_assignment->type);
    computation_type =
        pointer_assignment == NULL
            ? NULL
            : type_node(unit, pointer_assignment->computation_type);
    if (pointer_assignment == NULL ||
        pointer_assignment->kind != CTOOL_C_EXPRESSION_ASSIGNMENT ||
        pointer_assignment->operation !=
            pointer_assignment_operations[index] ||
        result_type == NULL || result_type->kind != CTOOL_C_TYPE_POINTER ||
        computation_type == NULL ||
        computation_type->kind != CTOOL_C_TYPE_POINTER) {
      (void)fprintf(stderr,
                    "scalar-updates: pointer assignment %u differs\n",
                    (unsigned)index);
      return 1;
    }
  }
  statement = scalar_return_statement(unit, "indexed_postfix");
  assignment =
      statement == NULL || statement->expression >= unit->expression_count
          ? NULL
          : &unit->expressions[statement->expression];
  left = assignment == NULL
             ? CTOOL_C_AST_NONE
             : scalar_expression_child(unit, assignment, 0u);
  if (assignment == NULL || assignment->kind != CTOOL_C_EXPRESSION_UPDATE ||
      left >= unit->expression_count ||
      unit->expressions[left].kind != CTOOL_C_EXPRESSION_UNARY ||
      unit->expressions[left].operation !=
          CTOOL_C_EXPRESSION_OPERATOR_DEREFERENCE) {
    (void)fprintf(stderr, "scalar-updates: indexed update differs\n");
    return 1;
  }
  statement = pointer_expression_statement(unit, "volatile_postfix");
  assignment =
      statement == NULL || statement->expression >= unit->expression_count
          ? NULL
          : &unit->expressions[statement->expression];
  left = assignment == NULL
             ? CTOOL_C_AST_NONE
             : scalar_expression_child(unit, assignment, 0u);
  qualifiers = 0u;
  if (assignment == NULL || assignment->kind != CTOOL_C_EXPRESSION_UPDATE ||
      left >= unit->expression_count ||
      scalar_type_kind(unit, unit->expressions[left].type, &qualifiers) !=
          CTOOL_C_TYPE_SIGNED_INT ||
      (qualifiers & CTOOL_C_QUAL_VOLATILE) == 0u) {
    (void)fprintf(stderr,
                  "scalar-updates: volatile raw designator differs\n");
    return 1;
  }
  statement = scalar_return_statement(unit, "atomic_prefix");
  assignment =
      statement == NULL || statement->expression >= unit->expression_count
          ? NULL
          : &unit->expressions[statement->expression];
  left = assignment == NULL
             ? CTOOL_C_AST_NONE
             : scalar_expression_child(unit, assignment, 0u);
  qualifiers = 0u;
  if (assignment == NULL || assignment->kind != CTOOL_C_EXPRESSION_UPDATE ||
      assignment->operation !=
          CTOOL_C_EXPRESSION_OPERATOR_PREFIX_INCREMENT ||
      left >= unit->expression_count ||
      scalar_type_kind(unit, unit->expressions[left].type, &qualifiers) !=
          CTOOL_C_TYPE_SIGNED_INT ||
      (qualifiers & CTOOL_C_QUAL_ATOMIC) == 0u) {
    (void)fprintf(stderr,
                  "scalar-updates: atomic raw designator differs\n");
    return 1;
  }
  statement = scalar_return_statement(unit, "bitfield_postfix");
  assignment =
      statement == NULL || statement->expression >= unit->expression_count
          ? NULL
          : &unit->expressions[statement->expression];
  left = assignment == NULL
             ? CTOOL_C_AST_NONE
             : scalar_expression_child(unit, assignment, 0u);
  if (assignment == NULL || assignment->kind != CTOOL_C_EXPRESSION_UPDATE ||
      assignment->operation !=
          CTOOL_C_EXPRESSION_OPERATOR_POSTFIX_INCREMENT ||
      scalar_type_kind(unit, assignment->type, NULL) !=
          CTOOL_C_TYPE_UNSIGNED_INT ||
      scalar_type_kind(unit, assignment->computation_type, NULL) !=
          CTOOL_C_TYPE_SIGNED_INT ||
      left >= unit->expression_count ||
      unit->expressions[left].kind != CTOOL_C_EXPRESSION_MEMBER ||
      unit->expressions[left].reference >= unit->graph.member_count ||
      unit->graph.members[unit->expressions[left].reference].is_bit_field !=
          CTOOL_TRUE ||
      unit->graph.members[unit->expressions[left].reference].bit_width != 3u) {
    (void)fprintf(stderr,
                  "scalar-updates: bit-field provenance differs\n");
    return 1;
  }
  statement = scalar_return_statement(unit, "side_effect_index");
  assignment =
      statement == NULL || statement->expression >= unit->expression_count
          ? NULL
          : &unit->expressions[statement->expression];
  left = assignment == NULL
             ? CTOOL_C_AST_NONE
             : scalar_expression_child(unit, assignment, 0u);
  index = left >= unit->expression_count
              ? CTOOL_C_AST_NONE
              : scalar_expression_child(unit, &unit->expressions[left], 0u);
  if (assignment == NULL || assignment->kind != CTOOL_C_EXPRESSION_UPDATE ||
      assignment->child_count != 1u || left >= unit->expression_count ||
      unit->expressions[left].kind != CTOOL_C_EXPRESSION_UNARY ||
      unit->expressions[left].operation !=
          CTOOL_C_EXPRESSION_OPERATOR_DEREFERENCE ||
      index >= unit->expression_count ||
      unit->expressions[index].kind != CTOOL_C_EXPRESSION_BINARY ||
      unit->expressions[index].operation != CTOOL_C_EXPRESSION_OPERATOR_ADD ||
      scalar_expression_child(unit, &unit->expressions[index], 0u) >=
          unit->expression_count ||
      unit->expressions[scalar_expression_child(
          unit, &unit->expressions[index], 0u)].kind != CTOOL_C_EXPRESSION_CALL) {
    (void)fprintf(stderr,
                  "scalar-updates: side-effecting designator was duplicated\n");
    return 1;
  }
  for (index = 0u; index < unit->expression_count; index++) {
    if (unit->expressions[index].kind == CTOOL_C_EXPRESSION_CALL) {
      call_count++;
    }
  }
  if (call_count != 1u) {
    (void)fprintf(stderr,
                  "scalar-updates: side-effecting call count differs\n");
    return 1;
  }
  statement = pointer_expression_statement(unit, "volatile_add_assign");
  assignment =
      statement == NULL || statement->expression >= unit->expression_count
          ? NULL
          : &unit->expressions[statement->expression];
  left = assignment == NULL
             ? CTOOL_C_AST_NONE
             : scalar_expression_child(unit, assignment, 0u);
  qualifiers = 0u;
  if (assignment == NULL ||
      assignment->kind != CTOOL_C_EXPRESSION_ASSIGNMENT ||
      assignment->operation != CTOOL_C_EXPRESSION_OPERATOR_ADD_ASSIGN ||
      left >= unit->expression_count ||
      scalar_type_kind(unit, unit->expressions[left].type, &qualifiers) !=
          CTOOL_C_TYPE_SIGNED_INT ||
      (qualifiers & CTOOL_C_QUAL_VOLATILE) == 0u) {
    (void)fprintf(stderr,
                  "scalar-updates: volatile compound designator differs\n");
    return 1;
  }
  return 0;
}

static int run_scalar_updates(const char *host_root) {
  static const char source[] =
      "void multiply_assign(int *value, int right) { *value *= right; }\n"
      "void divide_assign(int *value, int right) { *value /= right; }\n"
      "void remainder_assign(int *value, int right) { *value %= right; }\n"
      "void add_assign(int *value, unsigned char right) { *value += right; }\n"
      "void subtract_assign(int *value, int right) { *value -= right; }\n"
      "void shift_left_assign(int *value, int right) { *value <<= right; }\n"
      "void shift_right_assign(int *value, int right) { *value >>= right; }\n"
      "void and_assign(int *value, int right) { *value &= right; }\n"
      "void xor_assign(int *value, int right) { *value ^= right; }\n"
      "void or_assign(int *value, int right) { *value |= right; }\n"
      "unsigned char narrow_add_assign(unsigned char *value, unsigned int right) { return *value += right; }\n"
      "int prefix_increment(int *value) { return ++*value; }\n"
      "int prefix_decrement(int *value) { return --*value; }\n"
      "int postfix_increment(int *value) { return (*value)++; }\n"
      "int postfix_decrement(int *value) { return (*value)--; }\n"
      "unsigned char narrow_postfix(unsigned char *value) { return (*value)++; }\n"
      "int *pointer_prefix(int **value) { return ++*value; }\n"
      "int *pointer_postfix(int **value) { return (*value)--; }\n"
      "void pointer_add_assign(int **value, unsigned char right) { *value += right; }\n"
      "void pointer_subtract_assign(int **value, unsigned char right) { *value -= right; }\n"
      "int indexed_postfix(int *values, int index) { return values[index]++; }\n"
      "void volatile_postfix(volatile int *value) { (*value)++; }\n"
      "void volatile_add_assign(volatile int *value, int right) { *value += right; }\n"
      "int atomic_prefix(_Atomic int *value) { return ++*value; }\n"
      "typedef struct { unsigned int bits : 3; } update_bits_t;\n"
      "unsigned int bitfield_postfix(update_bits_t *value) { return value->bits++; }\n"
      "int *next_pointer(void);\n"
      "int side_effect_index(int index) { return next_pointer()[index]++; }\n";
  static const frontend_exact_failure_case_t failure_cases[] = {
      {{"prefix update of rvalue",
        "int bad(int value) { return ++(value + 1); }\n", CTOOL_ERR_INPUT,
        CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u, "assignment requires a modifiable lvalue"},
      {{"postfix update of const object",
        "int bad(const int value) { return value++; }\n", CTOOL_ERR_INPUT,
        CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u, "assignment requires a modifiable lvalue"},
      {{"postfix update of const pointer object",
        "int *bad(int *const pointer) { return pointer++; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u, "assignment requires a modifiable lvalue"},
      {{"array update",
        "int bad(void) { int value[2]; return value++; }\n", CTOOL_ERR_INPUT,
        CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u, "assignment requires a modifiable lvalue"},
      {{"void pointer update",
        "void *bad(void *pointer) { return pointer++; }\n", CTOOL_ERR_INPUT,
        CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u,
       "pointer update requires a pointer to a complete object type"},
      {{"pointer multiply assignment",
        "void bad(int **pointer, int right) { *pointer *= right; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u, "pointer compound assignment supports only += and -="},
      {{"pointer compound pointer offset",
        "void bad(int **left, int *right) { *left += right; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u,
       "pointer compound assignment requires an integer right operand"},
      {{"scalar add assignment with pointer right operand",
        "void bad(int *left, int *right) { *left += right; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u, "compound assignment requires arithmetic operands"},
      {{"scalar multiply assignment with pointer right operand",
        "void bad(int *left, int *right) { *left *= right; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u, "compound assignment requires arithmetic operands"},
      {{"floating compound assignment boundary",
        "void bad(int *left, float right) { *left += right; }\n",
        CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u,
       "floating compound assignment is outside this body slice"},
      {{"floating divide assignment boundary",
        "void bad(int *left, float right) { *left /= right; }\n",
        CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u,
       "floating compound assignment is outside this body slice"},
      {{"floating left compound assignment boundary",
        "void bad(float *left, int right) { *left *= right; }\n",
        CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u,
       "floating compound assignment is outside this body slice"},
      {{"floating left subtract assignment boundary",
        "void bad(float *left, int right) { *left -= right; }\n",
        CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u,
       "floating compound assignment is outside this body slice"},
      {{"floating remainder right operand",
        "void bad(int *left, float right) { *left %= right; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u, "compound assignment operator requires integer operands"},
      {{"floating shift right operand",
        "void bad(int *left, float right) { *left <<= right; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u, "compound assignment operator requires integer operands"},
      {{"floating bitwise right operand",
        "void bad(int *left, float right) { *left &= right; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u, "compound assignment operator requires integer operands"},
      {{"floating left remainder assignment",
        "void bad(float *left, int right) { *left %= right; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u, "compound assignment operator requires integer operands"},
      {{"floating compound pointer right operand",
        "void bad(float *left, int *right) { *left += right; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u, "compound assignment requires arithmetic operands"},
      {{"aggregate update operand",
        "struct item { int value; }; void bad(struct item value) { value++; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u, "update requires a real or pointer operand"},
      {{"aggregate simple assignment remains deferred",
        "struct item { int value; }; void bad(struct item *left, struct item right) { *left = right; }\n",
        CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u,
       "non-scalar assignment is outside this function-body slice"},
      {{"aggregate compound assignment operand",
        "struct item { int value; }; void bad(struct item value) { value += value; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u,
       "compound assignment requires an arithmetic or pointer left operand"},
      {{"incomplete pointer compound assignment",
        "struct pending; void bad(struct pending **pointer) { *pointer += 1; }\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u,
       "pointer compound assignment requires a pointer to a complete object type"},
      {{"floating update boundary",
        "void bad(float value) { value++; }\n",
        CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_EXPRESSION},
       0u, 0u, "floating update is outside this body slice"}};
  frontend_fixture_t fixture;
  ctool_c_translation_unit_t unit;
  ctool_u32 index;
  int failed = 1;

  if (begin_frontend_fixture(&fixture, "scalar-updates", host_root,
                             16u * 1024u * 1024u) != 0) {
    return 1;
  }
  if (parse_valid_fixture(&fixture, "/scalar-updates.c", source, &unit) ==
          0 &&
      validate_scalar_update_unit(&unit) == 0) {
    failed = 0;
  }
  for (index = 0u; failed == 0 && index < ARRAY_COUNT(failure_cases);
       index++) {
    const frontend_exact_failure_case_t *test_case = &failure_cases[index];
    if (expect_frontend_failure_at_message(
            &fixture, &test_case->failure, "/scalar-update-failure.c",
            test_case->line, test_case->column, test_case->message) != 0 ||
        validate_scalar_update_unit(&unit) != 0) {
      failed = 1;
    }
  }
  if (failed == 0 &&
      validate_scalar_update_storage_limit(&fixture, host_root) != 0) {
    failed = 1;
  }
  if (finish_frontend_fixture(&fixture) != 0) {
    failed = 1;
  }
  if (failed == 0) {
    (void)printf("scalar-updates: ok\n");
  }
  return failed;
}

static const ctool_c_type_node_t *
strip_qualified(const ctool_c_translation_unit_t *unit, ctool_u32 type,
                ctool_u32 *qualifiers_out) {
  const ctool_c_type_node_t *node = type_node(unit, type);
  ctool_u32 qualifiers = 0u;
  while (node != NULL && node->kind == CTOOL_C_TYPE_QUALIFIED) {
    qualifiers |= node->qualifiers;
    node = type_node(unit, node->referenced_type);
  }
  if (qualifiers_out != NULL) {
    *qualifiers_out = qualifiers;
  }
  return node;
}

static int validate_prototype_scope_unit(
    const ctool_c_translation_unit_t *unit) {
  const ctool_c_binding_t *scoped = find_binding(unit, "scoped");
  const ctool_c_binding_t *outer = find_binding(unit, "OUTER_VALUE");
  const ctool_c_tag_t *outer_record = find_tag(unit, "PrototypeRecord");
  const ctool_c_tag_t *outer_enum = find_tag(unit, "PrototypeEnum");
  const ctool_c_type_node_t *function =
      scoped == NULL ? NULL : type_node(unit, scoped->type);

  if (unit->binding_count != 2u || unit->tag_count != 2u ||
      scoped == NULL || scoped->kind != CTOOL_C_BINDING_FUNCTION ||
      outer == NULL || outer->kind != CTOOL_C_BINDING_ENUMERATOR ||
      find_binding(unit, "PROTOTYPE_VALUE") != NULL ||
      outer_record == NULL || outer_enum == NULL || function == NULL ||
      function->kind != CTOOL_C_TYPE_FUNCTION ||
      function->has_prototype != CTOOL_TRUE ||
      function->parameter_count != 2u || unit->parameter_count != 2u ||
      function->first_parameter > unit->graph.parameter_type_count ||
      2u > unit->graph.parameter_type_count - function->first_parameter ||
      unit->graph.parameter_types[function->first_parameter] ==
          outer_record->type ||
      unit->graph.parameter_types[function->first_parameter + 1u] ==
          outer_enum->type) {
    (void)fprintf(stderr,
                  "semantics: prototype-scope tags or enumerators leaked\n");
    return 1;
  }
  return 0;
}

static int validate_prototype_shadow_unit(
    const ctool_c_translation_unit_t *unit) {
  const ctool_c_binding_t *outer_value = find_binding(unit, "A");
  const ctool_c_binding_t *local_tag = find_binding(unit, "local_tag");
  const ctool_c_binding_t *local_enum = find_binding(unit, "local_enum");
  const ctool_c_binding_t *nested = find_binding(unit, "nested");
  const ctool_c_binding_t *outer_object =
      find_binding(unit, "outer_object");
  const ctool_c_binding_t *outer_enum_object =
      find_binding(unit, "outer_enum_object");
  const ctool_c_tag_t *outer_record = find_tag(unit, "SharedTag");
  const ctool_c_tag_t *outer_enum = find_tag(unit, "OuterEnum");
  const ctool_c_type_node_t *outer_record_node =
      outer_record == NULL ? NULL : type_node(unit, outer_record->type);
  const ctool_c_type_node_t *outer_enum_node =
      outer_enum == NULL ? NULL : type_node(unit, outer_enum->type);
  const ctool_c_type_node_t *local_tag_function =
      local_tag == NULL ? NULL : type_node(unit, local_tag->type);
  const ctool_c_type_node_t *local_enum_function =
      local_enum == NULL ? NULL : type_node(unit, local_enum->type);
  const ctool_c_type_node_t *nested_function =
      nested == NULL ? NULL : type_node(unit, nested->type);
  const ctool_c_type_node_t *prototype_record = NULL;
  const ctool_c_type_node_t *prototype_enum = NULL;
  const ctool_c_type_node_t *callback_pointer = NULL;
  const ctool_c_type_node_t *callback_function = NULL;
  const ctool_c_type_node_t *nested_enum = NULL;
  ctool_u32 prototype_member_index = 0u;
  ctool_u32 outer_member_index = 0u;

  if (local_tag_function != NULL &&
      local_tag_function->kind == CTOOL_C_TYPE_FUNCTION &&
      local_tag_function->parameter_count == 1u &&
      local_tag_function->first_parameter <
          unit->graph.parameter_type_count) {
    prototype_record = type_node(
        unit,
        unit->graph.parameter_types[local_tag_function->first_parameter]);
  }
  if (local_enum_function != NULL &&
      local_enum_function->kind == CTOOL_C_TYPE_FUNCTION &&
      local_enum_function->parameter_count == 1u &&
      local_enum_function->first_parameter <
          unit->graph.parameter_type_count) {
    prototype_enum = type_node(
        unit,
        unit->graph.parameter_types[local_enum_function->first_parameter]);
  }
  if (nested_function != NULL &&
      nested_function->kind == CTOOL_C_TYPE_FUNCTION &&
      nested_function->parameter_count == 2u &&
      nested_function->first_parameter <=
          unit->graph.parameter_type_count &&
      2u <= unit->graph.parameter_type_count -
                nested_function->first_parameter) {
    callback_pointer = type_node(
        unit,
        unit->graph.parameter_types[nested_function->first_parameter + 1u]);
  }
  if (callback_pointer != NULL &&
      callback_pointer->kind == CTOOL_C_TYPE_POINTER) {
    callback_function = type_node(unit, callback_pointer->referenced_type);
  }
  if (callback_function != NULL &&
      callback_function->kind == CTOOL_C_TYPE_FUNCTION &&
      callback_function->parameter_count == 1u &&
      callback_function->first_parameter <
          unit->graph.parameter_type_count) {
    nested_enum = type_node(
        unit,
        unit->graph.parameter_types[callback_function->first_parameter]);
  }

  if (unit->binding_count != 6u || unit->tag_count != 2u ||
      outer_value == NULL ||
      outer_value->kind != CTOOL_C_BINDING_ENUMERATOR ||
      outer_value->integer_bits != 11ull ||
      outer_value->integer_unsigned != CTOOL_FALSE || local_tag == NULL ||
      local_tag->kind != CTOOL_C_BINDING_FUNCTION || local_enum == NULL ||
      local_enum->kind != CTOOL_C_BINDING_FUNCTION || nested == NULL ||
      nested->kind != CTOOL_C_BINDING_FUNCTION || outer_object == NULL ||
      outer_object->kind != CTOOL_C_BINDING_OBJECT ||
      outer_enum_object == NULL ||
      outer_enum_object->kind != CTOOL_C_BINDING_OBJECT ||
      outer_record == NULL || outer_enum == NULL ||
      find_tag(unit, "LocalEnum") != NULL ||
      find_tag(unit, "NestedEnum") != NULL || outer_record_node == NULL ||
      outer_record_node->kind != CTOOL_C_TYPE_RECORD ||
      outer_record_node->record_complete != CTOOL_TRUE ||
      find_record_member(unit, outer_record_node, "outer_member",
                         &outer_member_index) == NULL ||
      outer_object->type != outer_record->type || outer_enum_node == NULL ||
      outer_enum_node->kind != CTOOL_C_TYPE_ENUM ||
      outer_enum_object->type != outer_enum->type ||
      prototype_record == NULL ||
      prototype_record->kind != CTOOL_C_TYPE_RECORD ||
      prototype_record->record_complete != CTOOL_TRUE ||
      prototype_record == outer_record_node ||
      find_record_member(unit, prototype_record, "prototype_member",
                         &prototype_member_index) == NULL ||
      prototype_enum == NULL || prototype_enum->kind != CTOOL_C_TYPE_ENUM ||
      prototype_enum == outer_enum_node || nested_enum == NULL ||
      nested_enum->kind != CTOOL_C_TYPE_ENUM || nested_enum == outer_enum_node ||
      nested_function == NULL ||
      nested_function->first_parameter >= unit->parameter_count ||
      !string_equal(unit->parameters[nested_function->first_parameter].name,
                    "A") ||
      callback_function == NULL ||
      callback_function->first_parameter >= unit->parameter_count ||
      !string_equal(
          unit->parameters[callback_function->first_parameter].name,
          "value")) {
    (void)fprintf(stderr,
                  "semantics: nested prototype shadowing or scope rewind "
                  "differs\n");
    return 1;
  }
  return 0;
}

static int validate_incomplete_declarations_unit(
    const ctool_c_translation_unit_t *unit) {
  const ctool_c_tag_t *forward = find_tag(unit, "Forward");
  const ctool_c_binding_t *accept = find_binding(unit, "accept_forward");
  const ctool_c_binding_t *values = find_binding(unit, "values");
  const ctool_c_type_node_t *forward_node =
      forward == NULL ? NULL : type_node(unit, forward->type);
  const ctool_c_type_layout_t *forward_layout =
      forward == NULL ? NULL : type_layout(unit, forward->type);
  const ctool_c_type_node_t *function =
      accept == NULL ? NULL : type_node(unit, accept->type);
  const ctool_c_type_node_t *array =
      values == NULL ? NULL : type_node(unit, values->type);
  const ctool_c_type_layout_t *array_layout =
      values == NULL ? NULL : type_layout(unit, values->type);

  if (unit->binding_count != 2u || unit->tag_count != 1u ||
      unit->parameter_count != 1u || forward_node == NULL ||
      forward_node->kind != CTOOL_C_TYPE_RECORD ||
      forward_node->record_complete != CTOOL_FALSE ||
      forward_layout == NULL ||
      forward_layout->is_complete_object != CTOOL_FALSE || accept == NULL ||
      accept->kind != CTOOL_C_BINDING_FUNCTION || function == NULL ||
      function->kind != CTOOL_C_TYPE_FUNCTION ||
      function->has_prototype != CTOOL_TRUE ||
      function->parameter_count != 1u ||
      function->first_parameter >= unit->graph.parameter_type_count ||
      unit->graph.parameter_types[function->first_parameter] != forward->type ||
      !string_equal(unit->parameters[function->first_parameter].name,
                    "value") ||
      values == NULL || values->kind != CTOOL_C_BINDING_OBJECT ||
      values->storage != CTOOL_C_STORAGE_EXTERN || array == NULL ||
      array->kind != CTOOL_C_TYPE_ARRAY ||
      array->array_bound_kind != CTOOL_C_ARRAY_UNSPECIFIED ||
      array_layout == NULL ||
      array_layout->is_complete_object != CTOOL_FALSE) {
    (void)fprintf(stderr,
                  "semantics: incomplete prototype or external array "
                  "differs\n");
    return 1;
  }
  return 0;
}

static int validate_strict_flexible_union_unit(
    const ctool_c_translation_unit_t *unit) {
  const ctool_c_tag_t *leaf = find_tag(unit, "FlexibleLeaf");
  const ctool_c_tag_t *container = find_tag(unit, "FlexibleUnion");
  const ctool_c_type_node_t *leaf_node =
      leaf == NULL ? NULL : type_node(unit, leaf->type);
  const ctool_c_type_node_t *container_node =
      container == NULL ? NULL : type_node(unit, container->type);
  const ctool_c_type_layout_t *leaf_layout =
      leaf == NULL ? NULL : type_layout(unit, leaf->type);
  const ctool_c_type_layout_t *container_layout =
      container == NULL ? NULL : type_layout(unit, container->type);
  ctool_u32 member_index = 0u;
  const ctool_c_record_member_t *member =
      container_node == NULL
          ? NULL
          : find_record_member(unit, container_node, "leaf", &member_index);

  if (unit->binding_count != 0u || unit->tag_count != 2u ||
      leaf_node == NULL || leaf_node->kind != CTOOL_C_TYPE_RECORD ||
      leaf_node->record_kind != CTOOL_C_RECORD_STRUCT ||
      leaf_node->member_count != 2u || leaf_layout == NULL ||
      leaf_layout->size != 4u || leaf_layout->alignment != 4u ||
      container_node == NULL ||
      container_node->kind != CTOOL_C_TYPE_RECORD ||
      container_node->record_kind != CTOOL_C_RECORD_UNION ||
      container_node->member_count != 2u || container_layout == NULL ||
      container_layout->size != 4u || container_layout->alignment != 4u ||
      member == NULL || member->type != leaf->type) {
    (void)fprintf(stderr,
                  "semantics: strict flexible-array union allowance "
                  "differs\n");
    return 1;
  }
  return 0;
}

static int validate_qualified_unit(const ctool_c_translation_unit_t *unit) {
  const ctool_c_binding_t *normalized = find_binding(unit, "normalized");
  const ctool_c_binding_t *array_object = find_binding(unit, "array_object");
  const ctool_c_binding_t *declared_function =
      find_binding(unit, "declared_function");
  const ctool_c_binding_t *scalar_return =
      find_binding(unit, "scalar_return");
  const ctool_c_binding_t *consume = find_binding(unit, "consume");
  const ctool_c_binding_t *valid_fam = find_binding(unit, "valid_fam_t");
  const ctool_c_binding_t *c_mode_class = find_binding(unit, "class");
  const ctool_c_binding_t *old_abstract = find_binding(unit, "old");
  const ctool_c_binding_t *prototype_abstract = find_binding(unit, "proto");
  const ctool_c_type_node_t *normalized_type;
  const ctool_c_type_node_t *array_type;
  const ctool_c_type_node_t *function_type;
  const ctool_c_type_node_t *consume_type;
  const ctool_c_type_node_t *parameter_pointer;
  const ctool_c_type_node_t *parameter_element;
  const ctool_c_type_layout_t *array_layout;
  const ctool_c_type_layout_t *fam_layout;
  const ctool_c_type_node_t *old_function;
  const ctool_c_type_node_t *prototype_function;
  const ctool_c_type_node_t *old_pointer;
  const ctool_c_type_node_t *prototype_pointer;
  const ctool_c_type_node_t *old_parameter_function;
  const ctool_c_type_node_t *prototype_parameter_function;
  ctool_u32 qualifiers = 0u;

  normalized_type = normalized == NULL ? NULL : type_node(unit, normalized->type);
  array_type = array_object == NULL
                   ? NULL
                   : strip_qualified(unit, array_object->type, &qualifiers);
  function_type = declared_function == NULL
                      ? NULL
                      : strip_qualified(unit, declared_function->type, NULL);
  consume_type = consume == NULL ? NULL : type_node(unit, consume->type);
  parameter_pointer =
      consume_type == NULL || consume_type->parameter_count != 1u ||
              consume_type->first_parameter >
                  unit->graph.parameter_type_count ||
              1u > unit->graph.parameter_type_count -
                       consume_type->first_parameter
          ? NULL
          : type_node(unit,
                      unit->graph.parameter_types[consume_type->first_parameter]);
  parameter_element =
      parameter_pointer == NULL ||
              parameter_pointer->kind != CTOOL_C_TYPE_POINTER
          ? NULL
          : type_node(unit, parameter_pointer->referenced_type);
  array_layout =
      array_object == NULL ? NULL : type_layout(unit, array_object->type);
  fam_layout = valid_fam == NULL ? NULL : type_layout(unit, valid_fam->type);
  old_function =
      old_abstract == NULL ? NULL : type_node(unit, old_abstract->type);
  prototype_function = prototype_abstract == NULL
                           ? NULL
                           : type_node(unit, prototype_abstract->type);
  old_pointer =
      old_function == NULL || old_function->parameter_count != 1u ||
              old_function->first_parameter >
                  unit->graph.parameter_type_count ||
              1u > unit->graph.parameter_type_count -
                       old_function->first_parameter
          ? NULL
          : type_node(unit,
                      unit->graph.parameter_types[old_function->first_parameter]);
  prototype_pointer =
      prototype_function == NULL ||
              prototype_function->parameter_count != 1u ||
              prototype_function->first_parameter >
                  unit->graph.parameter_type_count ||
              1u > unit->graph.parameter_type_count -
                       prototype_function->first_parameter
          ? NULL
          : type_node(
                unit,
                unit->graph.parameter_types[prototype_function->first_parameter]);
  old_parameter_function =
      old_pointer == NULL || old_pointer->kind != CTOOL_C_TYPE_POINTER
          ? NULL
          : type_node(unit, old_pointer->referenced_type);
  prototype_parameter_function =
      prototype_pointer == NULL ||
              prototype_pointer->kind != CTOOL_C_TYPE_POINTER
          ? NULL
          : type_node(unit, prototype_pointer->referenced_type);

  if (normalized == NULL || normalized->kind != CTOOL_C_BINDING_FUNCTION ||
      normalized_type == NULL ||
      normalized_type->kind != CTOOL_C_TYPE_FUNCTION ||
      normalized_type->has_prototype != CTOOL_TRUE ||
      normalized_type->parameter_count != 0u || array_object == NULL ||
      array_object->kind != CTOOL_C_BINDING_OBJECT || array_type == NULL ||
      array_type->kind != CTOOL_C_TYPE_ARRAY ||
      qualifiers != CTOOL_C_QUAL_CONST || array_layout == NULL ||
      array_layout->size != 16u || array_layout->alignment != 4u ||
      array_layout->is_complete_object != CTOOL_TRUE ||
      declared_function == NULL ||
      declared_function->kind != CTOOL_C_BINDING_FUNCTION ||
      function_type == NULL || function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      scalar_return == NULL ||
      scalar_return->kind != CTOOL_C_BINDING_FUNCTION || consume == NULL ||
      consume->kind != CTOOL_C_BINDING_FUNCTION || parameter_pointer == NULL ||
      parameter_element == NULL ||
      parameter_element->kind != CTOOL_C_TYPE_QUALIFIED ||
      parameter_element->qualifiers != CTOOL_C_QUAL_CONST ||
      strip_qualified(unit, parameter_element->referenced_type, NULL) == NULL ||
      strip_qualified(unit, parameter_element->referenced_type, NULL)->kind !=
          CTOOL_C_TYPE_SIGNED_INT ||
      valid_fam == NULL || valid_fam->kind != CTOOL_C_BINDING_TYPEDEF ||
      fam_layout == NULL || fam_layout->size != 4u ||
      fam_layout->alignment != 4u || c_mode_class == NULL ||
      c_mode_class->kind != CTOOL_C_BINDING_OBJECT || old_abstract == NULL ||
      old_abstract->kind != CTOOL_C_BINDING_FUNCTION ||
      prototype_abstract == NULL ||
      prototype_abstract->kind != CTOOL_C_BINDING_FUNCTION ||
      old_function == NULL || old_function->kind != CTOOL_C_TYPE_FUNCTION ||
      old_function->has_prototype != CTOOL_TRUE ||
      prototype_function == NULL ||
      prototype_function->kind != CTOOL_C_TYPE_FUNCTION ||
      prototype_function->has_prototype != CTOOL_TRUE ||
      old_parameter_function == NULL ||
      old_parameter_function->kind != CTOOL_C_TYPE_FUNCTION ||
      old_parameter_function->has_prototype != CTOOL_FALSE ||
      old_parameter_function->parameter_count != 0u ||
      prototype_parameter_function == NULL ||
      prototype_parameter_function->kind != CTOOL_C_TYPE_FUNCTION ||
      prototype_parameter_function->has_prototype != CTOOL_TRUE ||
      prototype_parameter_function->parameter_count != 0u) {
    (void)fprintf(stderr,
                  "semantics: void normalization, qualification, or FAM "
                  "classification differs\n");
    return 1;
  }

  {
    const ctool_c_binding_t *external = find_binding(unit, "external_object");
    const ctool_c_binding_t *internal = find_binding(unit, "internal_object");
    const ctool_c_binding_t *internal_second =
        find_binding(unit, "internal_object_second");
    if (external == NULL || internal == NULL || internal_second == NULL ||
        external->storage != CTOOL_C_STORAGE_EXTERN ||
        internal->storage != CTOOL_C_STORAGE_STATIC ||
        internal_second->storage != CTOOL_C_STORAGE_STATIC) {
      (void)fprintf(stderr,
                    "semantics: public binding storage classes differ\n");
      return 1;
    }
  }
  return 0;
}

static int expect_direct_parse_failure(
    frontend_fixture_t *fixture, const char *name,
    const ctool_c_pp_result_t *tape, const ctool_c_parse_request_t *request,
    ctool_status_t expected_status, ctool_u32 expected_code,
    const char *expected_path, ctool_u32 expected_line,
    ctool_u32 expected_column) {
  ctool_c_translation_unit_t unit;
  ctool_c_pp_token_t *snapshot = NULL;
  ctool_arena_mark_t mark;
  const ctool_diagnostic_t *diagnostic;
  ctool_status_t status;
  ctool_u32 diagnostic_count;
  size_t token_bytes = 0u;
  int failed = 0;

  if (tape != NULL && tape->tokens != NULL && tape->token_count != 0u) {
    token_bytes = (size_t)tape->token_count * sizeof(*snapshot);
    snapshot = (ctool_c_pp_token_t *)malloc(token_bytes);
    if (snapshot == NULL) {
      (void)fprintf(stderr, "boundaries: %s snapshot allocation failed\n",
                    name);
      return 1;
    }
    (void)memcpy(snapshot, tape->tokens, token_bytes);
  }
  diagnostic_count = ctool_job_diagnostic_count(fixture->job);
  mark = ctool_arena_mark(ctool_job_arena(fixture->job));
  (void)memset(&unit, 0xa5, sizeof(unit));
  status = ctool_c_parse(fixture->job, tape, request, &unit);
  diagnostic = ctool_job_diagnostic(fixture->job, diagnostic_count);
  if (status != expected_status || unit_is_zero(&unit) == 0 ||
      ctool_job_diagnostic_count(fixture->job) != diagnostic_count + 1u ||
      diagnostic == NULL || diagnostic->code != expected_code ||
      !string_equal(diagnostic->path, expected_path) ||
      diagnostic->line != expected_line ||
      diagnostic->column != expected_column ||
      arena_marks_equal(mark,
                        ctool_arena_mark(ctool_job_arena(fixture->job))) == 0 ||
      (snapshot != NULL &&
       memcmp(snapshot, tape->tokens, token_bytes) != 0) ||
      validate_anchor(fixture) != 0) {
    (void)fprintf(stderr,
                  "boundaries: %s expected %s/0x%08x at %s:%u:%u, got %s",
                  name, ctool_status_name(expected_status), expected_code,
                  expected_path, expected_line, expected_column,
                  ctool_status_name(status));
    if (diagnostic != NULL) {
      (void)fprintf(stderr, "/0x%08x at %.*s:%u:%u", diagnostic->code,
                    (int)diagnostic->path.size,
                    diagnostic->path.data != NULL ? diagnostic->path.data : "",
                    diagnostic->line, diagnostic->column);
    }
    (void)fprintf(stderr, "\n");
    failed = 1;
  }
  free(snapshot);
  return failed;
}

typedef struct {
  char *spelling;
  char *presumed_path;
  char *physical_path;
} borrowed_token_storage_t;

static char *copy_counted_text(ctool_string_t text) {
  char *copy = (char *)malloc((size_t)text.size + 1u);
  if (copy == NULL) {
    return NULL;
  }
  if (text.size != 0u) {
    (void)memcpy(copy, text.data, text.size);
  }
  copy[text.size] = '\0';
  return copy;
}

static void destroy_borrowed_tape(ctool_c_pp_token_t *tokens,
                                  borrowed_token_storage_t *storage,
                                  ctool_u32 token_count) {
  ctool_u32 index;
  if (storage != NULL) {
    for (index = 0u; index < token_count; index++) {
      if (storage[index].spelling != NULL) {
        (void)memset(storage[index].spelling, 0x58,
                     tokens[index].spelling.size);
      }
      if (storage[index].presumed_path != NULL) {
        (void)memset(storage[index].presumed_path, 0x59,
                     tokens[index].location.path.size);
      }
      if (storage[index].physical_path != NULL) {
        (void)memset(storage[index].physical_path, 0x5a,
                     tokens[index].physical_location.path.size);
      }
      free(storage[index].physical_path);
      free(storage[index].presumed_path);
      free(storage[index].spelling);
    }
  }
  if (tokens != NULL) {
    (void)memset(tokens, 0xa5, (size_t)token_count * sizeof(*tokens));
  }
  free(storage);
  free(tokens);
}

static int validate_owned_unit(const ctool_c_translation_unit_t *unit) {
  const ctool_c_binding_t *binding = find_binding(unit, "owned_function");
  const ctool_c_tag_t *tag = find_tag(unit, "OwnedTag");
  const ctool_c_type_node_t *record =
      tag == NULL ? NULL : type_node(unit, tag->type);
  const ctool_c_function_definition_t *definition =
      unit->function_definition_count == 1u
          ? &unit->function_definitions[0]
          : NULL;
  const ctool_c_type_node_t *function =
      definition == NULL ? NULL : type_node(unit, definition->declared_type);
  const ctool_c_record_member_t *member;
  const ctool_c_parameter_t *parameter;
  const ctool_c_type_node_t *parameter_type_node;
  const ctool_c_block_binding_t *local =
      unit->block_binding_count == 1u ? &unit->block_bindings[0] : NULL;
  const ctool_c_expression_t *literal = NULL;
  const ctool_c_expression_t *addition = NULL;
  const ctool_c_expression_t *addition_left = NULL;
  const ctool_c_expression_t *addition_right = NULL;
  ctool_u32 member_index = 0u;
  ctool_u32 qualifiers = 0u;
  ctool_u32 index;

  member = record == NULL
               ? NULL
               : find_record_member(unit, record, "owned_member",
                                    &member_index);
  parameter =
      function == NULL || function->parameter_count != 1u ||
              function->first_parameter > unit->parameter_count ||
              1u > unit->parameter_count - function->first_parameter
          ? NULL
          : &unit->parameters[function->first_parameter];
  parameter_type_node =
      parameter == NULL ? NULL : type_node(unit, parameter->type);
  for (index = 0u; index < unit->expression_count; index++) {
    if (unit->expressions[index].kind == CTOOL_C_EXPRESSION_STRING) {
      literal = &unit->expressions[index];
      break;
    }
  }
  if (local != NULL && local->initializer < unit->expression_count) {
    ctool_u32 addition_index = local->initializer;
    ctool_u32 addition_left_index;
    ctool_u32 addition_right_index;
    addition_index = scalar_unwrap_conversions(unit, addition_index);
    if (addition_index < unit->expression_count) {
      addition = &unit->expressions[addition_index];
      addition_left_index = scalar_expression_child(unit, addition, 0u);
      addition_right_index = scalar_expression_child(unit, addition, 1u);
      if (addition_left_index < unit->expression_count) {
        addition_left = &unit->expressions[addition_left_index];
      }
      if (addition_right_index < unit->expression_count) {
        addition_right = &unit->expressions[addition_right_index];
      }
    }
  }
  if (unit->binding_count != 2u || unit->tag_count != 1u ||
      unit->graph.member_count != 1u || unit->parameter_count != 2u ||
      unit->block_binding_count != 1u ||
      unit->function_definition_count != 1u || unit->statement_count != 6u ||
      unit->statement_child_count != 4u || unit->expression_count != 9u ||
      unit->expression_child_count != 7u ||
      binding == NULL || binding->kind != CTOOL_C_BINDING_FUNCTION ||
      !dual_location_matches(&binding->location,
                             &binding->physical_location, "/borrowed.c", 3u) ||
      definition == NULL ||
      definition->binding != find_binding_index(unit, "owned_function") ||
      definition->storage != CTOOL_C_STORAGE_STATIC ||
      definition->function_declaration_flags != CTOOL_C_FUNCTION_DECL_INLINE ||
      !dual_location_matches(&definition->location,
                             &definition->physical_location,
                             "/borrowed.c", 3u) ||
      tag == NULL ||
      !dual_location_matches(&tag->location, &tag->physical_location,
                             "/borrowed.c", 1u) ||
      record == NULL || record->kind != CTOOL_C_TYPE_RECORD || member == NULL ||
      !string_equal(member->name, "owned_member") ||
      !dual_location_matches(&member->location, &member->physical_location,
                             "/borrowed.c", 1u) ||
      member_index >= unit->layout.member_count || parameter == NULL ||
      parameter_type_node == NULL ||
      parameter_type_node->kind != CTOOL_C_TYPE_RECORD ||
      !string_equal(parameter->name, "owned_parameter") ||
      !dual_location_matches(&parameter->location,
                             &parameter->physical_location, "/borrowed.c",
                             3u) ||
      local == NULL || !string_equal(local->name, "owned_local") ||
      local->kind != CTOOL_C_BINDING_OBJECT ||
      local->storage != CTOOL_C_STORAGE_NONE ||
      scalar_type_kind(unit, local->type, &qualifiers) !=
          CTOOL_C_TYPE_SIGNED_INT ||
      (qualifiers & CTOOL_C_QUAL_VOLATILE) == 0u ||
      !dual_location_matches(&local->location,
                             &local->physical_location, "/borrowed.c", 4u) ||
      local->initializer >= unit->expression_count ||
      unit->statements[0].kind != CTOOL_C_STATEMENT_DECLARATION ||
      unit->statements[0].first_block_binding != 0u ||
      unit->statements[0].block_binding_count != 1u ||
      addition == NULL || addition->kind != CTOOL_C_EXPRESSION_BINARY ||
      addition->operation != CTOOL_C_EXPRESSION_OPERATOR_ADD ||
      addition_left == NULL ||
      addition_left->kind != CTOOL_C_EXPRESSION_INTEGER_CONSTANT ||
      addition_left->integer_bits != 1ull || addition_right == NULL ||
      addition_right->kind != CTOOL_C_EXPRESSION_INTEGER_CONSTANT ||
      addition_right->integer_bits != 65ull ||
      !dual_location_matches(&addition_left->location,
                               &addition_left->physical_location,
                               "/borrowed.c", 4u) ||
      !dual_location_matches(&addition_right->location,
                             &addition_right->physical_location,
                             "/borrowed.c", 4u) ||
      unit->statements[2].kind != CTOOL_C_STATEMENT_BREAK ||
      unit->statements[3].kind != CTOOL_C_STATEMENT_FOR ||
      unit->statements[3].initializer_statement != CTOOL_C_AST_NONE ||
      unit->statements[3].condition != CTOOL_C_AST_NONE ||
      unit->statements[3].iteration != CTOOL_C_AST_NONE ||
      unit->statements[3].body != 2u ||
      unit->statements[4].kind != CTOOL_C_STATEMENT_RETURN ||
      unit->statements[4].expression != CTOOL_C_AST_NONE ||
      literal == NULL || literal->string_bytes.size != 7u ||
      literal->string_bytes.data == NULL ||
      memcmp(literal->string_bytes.data, "owned\n\0", 7u) != 0 ||
      !dual_location_matches(&literal->location,
                             &literal->physical_location, "/borrowed.c",
                             5u)) {
    (void)fprintf(stderr,
                  "boundaries: copied names or dual locations did not survive "
                  "(statements=%u/%u expressions=%u/%u initializer=%u)\n",
                  (unsigned int)unit->statement_count,
                  (unsigned int)unit->statement_child_count,
                  (unsigned int)unit->expression_count,
                  (unsigned int)unit->expression_child_count,
                  local == NULL ? 0xffffffffu
                                : (unsigned int)local->initializer);
    return 1;
  }
  for (index = 0u; index < unit->statement_count; index++) {
    if (!dual_location_matches(&unit->statements[index].location,
                               &unit->statements[index].physical_location,
                               "/borrowed.c",
                               unit->statements[index].location.line)) {
      (void)fprintf(stderr,
                    "boundaries: copied statement locations did not survive\n");
      return 1;
    }
  }
  return 0;
}

static int parse_owned_tape(frontend_fixture_t *fixture,
                            ctool_c_translation_unit_t *unit_out) {
  static const char source[] =
      "struct OwnedTag { int owned_member; };\n"
      "void owned_sink(const char *text);\n"
      "static inline void owned_function(struct OwnedTag owned_parameter) {\n"
      "  volatile int owned_local = 1 + 'A';\n"
      "  owned_sink(\"owned\\n\");\n"
      "  for (;;) break;\n"
      "  return;\n"
      "}\n";
  ctool_c_pp_result_t original;
  ctool_c_pp_result_t borrowed;
  ctool_c_pp_token_t *tokens = NULL;
  borrowed_token_storage_t *storage = NULL;
  ctool_status_t status;
  ctool_u32 diagnostic_count;
  ctool_u32 index;
  int failed = 1;

  if (preprocess_fixture(fixture, "/borrowed.c", source, &original) != 0) {
    return 1;
  }
  tokens = (ctool_c_pp_token_t *)malloc(
      (size_t)original.token_count * sizeof(*tokens));
  storage = (borrowed_token_storage_t *)calloc(
      original.token_count, sizeof(*storage));
  if (tokens == NULL || storage == NULL) {
    (void)fprintf(stderr,
                  "boundaries: borrowed token allocation failed\n");
    free(storage);
    free(tokens);
    return 1;
  }
  (void)memcpy(tokens, original.tokens,
               (size_t)original.token_count * sizeof(*tokens));
  for (index = 0u; index < original.token_count; index++) {
    storage[index].spelling = copy_counted_text(tokens[index].spelling);
    storage[index].presumed_path =
        copy_counted_text(tokens[index].location.path);
    storage[index].physical_path =
        copy_counted_text(tokens[index].physical_location.path);
    if (storage[index].spelling == NULL ||
        storage[index].presumed_path == NULL ||
        storage[index].physical_path == NULL) {
      (void)fprintf(stderr,
                    "boundaries: borrowed token text allocation failed\n");
      destroy_borrowed_tape(tokens, storage, original.token_count);
      return 1;
    }
    tokens[index].spelling.data = storage[index].spelling;
    tokens[index].location.path.data = storage[index].presumed_path;
    tokens[index].physical_location.path.data = storage[index].physical_path;
  }
  borrowed.tokens = tokens;
  borrowed.token_count = original.token_count;
  diagnostic_count = ctool_job_diagnostic_count(fixture->job);
  (void)memset(unit_out, 0xa5, sizeof(*unit_out));
  status = ctool_c_parse(fixture->job, &borrowed, &fixture->parse_request,
                         unit_out);
  if (status != CTOOL_OK ||
      ctool_job_diagnostic_count(fixture->job) != diagnostic_count) {
    (void)fprintf(stderr, "boundaries: borrowed tape parse failed: %s\n",
                  ctool_status_name(status));
  } else {
    destroy_borrowed_tape(tokens, storage, original.token_count);
    tokens = NULL;
    storage = NULL;
    if (validate_owned_unit(unit_out) == 0 && validate_anchor(fixture) == 0) {
      failed = 0;
    }
  }
  destroy_borrowed_tape(tokens, storage, original.token_count);
  return failed;
}

static char *build_attribute_limit_source(ctool_bool aligned) {
  const size_t capacity = 4096u;
  char *source = (char *)malloc(capacity);
  size_t used = 0u;
  ctool_u32 index;
  if (source == NULL) {
    return NULL;
  }
  source[0] = '\0';
  if (append_scale_text(source, capacity, &used, "typedef int ") != 0) {
    free(source);
    return NULL;
  }
  for (index = 0u; index < 64u; index++) {
    if (append_scale_text(source, capacity, &used, "*") != 0) {
      free(source);
      return NULL;
    }
  }
  if (append_scale_text(
          source, capacity, &used,
          aligned == CTOOL_TRUE
              ? "limited_t __attribute__((aligned(64)));\n"
              : "limited_t;\n") != 0) {
    free(source);
    return NULL;
  }
  return source;
}

static int validate_attribute_storage_limit(frontend_fixture_t *fixture,
                                            const char *host_root) {
  static const char anchor_source[] =
      "typedef unsigned int attribute_limit_anchor_t;\n";
  char *control_source = build_attribute_limit_source(CTOOL_FALSE);
  char *aligned_source = build_attribute_limit_source(CTOOL_TRUE);
  ctool_c_translation_unit_t control_unit;
  ctool_c_translation_unit_t aligned_unit;
  ctool_c_translation_unit_t anchor_unit;
  ctool_c_translation_unit_t failed_unit;
  ctool_c_pp_result_t aligned_tape;
  ctool_c_pp_result_t anchor_tape;
  ctool_limits_t limits = ctool_default_limits();
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_pp_token_t *snapshot = NULL;
  ctool_arena_mark_t mark;
  const ctool_diagnostic_t *diagnostic;
  const ctool_c_binding_t *anchor_binding;
  ctool_status_t status;
  size_t token_bytes;
  int failed = 1;

  if (control_source == NULL || aligned_source == NULL ||
      parse_valid_fixture(fixture, "/attribute-control.c", control_source,
                          &control_unit) != 0 ||
      parse_valid_fixture(fixture, "/attribute-success.c", aligned_source,
                          &aligned_unit) != 0 ||
      aligned_unit.graph.type_count != control_unit.graph.type_count + 1u ||
      preprocess_fixture(fixture, "/attribute-limit.c", aligned_source,
                         &aligned_tape) != 0 ||
      preprocess_fixture(fixture, "/attribute-limit-anchor.c", anchor_source,
                         &anchor_tape) != 0 ||
      (size_t)control_unit.graph.type_count *
              sizeof(ctool_c_type_node_t) >
          0xffffffffu) {
    (void)fprintf(stderr, "attributes: storage-limit control differs\n");
    goto cleanup;
  }
  limits.output_bytes =
      control_unit.graph.type_count * (ctool_u32)sizeof(ctool_c_type_node_t);
  token_bytes = (size_t)aligned_tape.token_count * sizeof(*snapshot);
  snapshot = (ctool_c_pp_token_t *)malloc(token_bytes);
  if (snapshot == NULL) {
    goto cleanup;
  }
  (void)memcpy(snapshot, aligned_tape.tokens, token_bytes);
  status = ctool_host_adapter_init(&adapter, host_root);
  if (status != CTOOL_OK) {
    goto cleanup;
  }
  config = ctool_host_job_config(&adapter, limits);
  status = ctool_job_open(&config, &job);
  if (status != CTOOL_OK) {
    goto cleanup;
  }
  (void)memset(&anchor_unit, 0xa5, sizeof(anchor_unit));
  status = ctool_c_parse(job, &anchor_tape, &fixture->parse_request,
                         &anchor_unit);
  anchor_binding = find_binding(&anchor_unit, "attribute_limit_anchor_t");
  if (status != CTOOL_OK || anchor_binding == NULL ||
      anchor_unit.binding_count != 1u) {
    (void)fprintf(stderr, "attributes: limited anchor failed\n");
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  (void)memset(&failed_unit, 0xa5, sizeof(failed_unit));
  status = ctool_c_parse(job, &aligned_tape, &fixture->parse_request,
                         &failed_unit);
  diagnostic = ctool_job_diagnostic(job, 0u);
  anchor_binding = find_binding(&anchor_unit, "attribute_limit_anchor_t");
  if (status != CTOOL_ERR_LIMIT || unit_is_zero(&failed_unit) == 0 ||
      ctool_job_diagnostic_count(job) != 1u || diagnostic == NULL ||
      diagnostic->code != CTOOL_C_PARSE_DIAG_LIMIT ||
      !string_equal(diagnostic->path, "/attribute-limit.c") ||
      diagnostic->line != 1u || diagnostic->column == 0u ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      memcmp(snapshot, aligned_tape.tokens, token_bytes) != 0 ||
      anchor_binding == NULL || anchor_unit.binding_count != 1u ||
      anchor_binding->kind != CTOOL_C_BINDING_TYPEDEF) {
    (void)fprintf(stderr, "attributes: storage-limit rollback differs: %s\n",
                  ctool_status_name(status));
    goto cleanup;
  }
  failed = 0;

cleanup:
  if (job != NULL) {
    ctool_job_close(job);
  }
  free(snapshot);
  free(aligned_source);
  free(control_source);
  return failed;
}

static int validate_configured_storage_limit(
    frontend_fixture_t *fixture, const char *host_root,
    const ctool_c_pp_result_t *tape) {
  ctool_limits_t limits = ctool_default_limits();
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_translation_unit_t unit;
  ctool_c_pp_token_t *snapshot;
  ctool_arena_mark_t mark;
  const ctool_diagnostic_t *diagnostic;
  ctool_status_t status;
  size_t token_bytes = (size_t)tape->token_count * sizeof(*snapshot);
  int failed = 1;

  snapshot = (ctool_c_pp_token_t *)malloc(token_bytes);
  if (snapshot == NULL) {
    return 1;
  }
  (void)memcpy(snapshot, tape->tokens, token_bytes);
  limits.output_bytes = 1u;
  status = ctool_host_adapter_init(&adapter, host_root);
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "boundaries: limited adapter failed\n");
    goto cleanup;
  }
  config = ctool_host_job_config(&adapter, limits);
  status = ctool_job_open(&config, &job);
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "boundaries: limited job failed\n");
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  (void)memset(&unit, 0xa5, sizeof(unit));
  status = ctool_c_parse(job, tape, &fixture->parse_request, &unit);
  diagnostic = ctool_job_diagnostic(job, 0u);
  if (status != CTOOL_ERR_LIMIT || unit_is_zero(&unit) == 0 ||
      ctool_job_diagnostic_count(job) != 1u || diagnostic == NULL ||
      diagnostic->code != CTOOL_C_PARSE_DIAG_LIMIT ||
      !string_equal(diagnostic->path, "/limit.c") ||
      diagnostic->line != 1u || diagnostic->column != 5u ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      memcmp(snapshot, tape->tokens, token_bytes) != 0 ||
      validate_anchor(fixture) != 0) {
    (void)fprintf(stderr,
                  "boundaries: configured storage limit differs: "
                  "status=%s unit_zero=%d diagnostics=%u code=0x%08x "
                  "path=%.*s line=%u column=%u mark=%d tape=%d anchor=%d\n",
                  ctool_status_name(status), unit_is_zero(&unit),
                  ctool_job_diagnostic_count(job),
                  diagnostic != NULL ? diagnostic->code : 0u,
                  diagnostic != NULL ? (int)diagnostic->path.size : 0,
                  diagnostic != NULL && diagnostic->path.data != NULL
                      ? diagnostic->path.data
                      : "",
                  diagnostic != NULL ? diagnostic->line : 0u,
                  diagnostic != NULL ? diagnostic->column : 0u,
                  arena_marks_equal(mark,
                                    ctool_arena_mark(ctool_job_arena(job))),
                  memcmp(snapshot, tape->tokens, token_bytes) == 0 ? 1 : 0,
                  validate_anchor(fixture) == 0 ? 1 : 0);
    goto cleanup;
  }
  failed = 0;
cleanup:
  if (job != NULL) {
    ctool_job_close(job);
  }
  free(snapshot);
  return failed;
}

static char *build_composite_limit_source(ctool_u32 padding_count,
                                          ctool_bool merge) {
  const size_t capacity = 32u * 1024u;
  char *source = (char *)malloc(capacity);
  size_t used = 0u;
  ctool_u32 index;
  if (source == NULL) {
    return NULL;
  }
  source[0] = '\0';
  for (index = 0u; index < padding_count; index++) {
    char line[64];
    int written = snprintf(line, sizeof(line), "const int pad%03u;\n",
                           (unsigned int)index);
    if (written <= 0 || (size_t)written >= sizeof(line) ||
        append_scale_text(source, capacity, &used, line) != 0) {
      free(source);
      return NULL;
    }
  }
  if (append_scale_text(
          source, capacity, &used,
          merge == CTOOL_TRUE
              ? "int combined(int (*first)[4], int (*second)[]);\n"
                "int combined(int (*first)[], int (*second)[5]);\n"
              : "int left(int (*first)[4], int (*second)[]);\n"
                "int right(int (*first)[], int (*second)[5]);\n") != 0) {
    free(source);
    return NULL;
  }
  return source;
}

static int validate_composite_storage_limit(frontend_fixture_t *fixture,
                                            const char *host_root) {
  const ctool_u32 padding_count = 128u;
  static const char anchor_source[] =
      "typedef unsigned int limited_anchor_t;\n";
  char *control_source =
      build_composite_limit_source(padding_count, CTOOL_FALSE);
  char *merge_source =
      build_composite_limit_source(padding_count, CTOOL_TRUE);
  ctool_c_pp_result_t merge_tape;
  ctool_c_pp_result_t anchor_tape;
  ctool_c_translation_unit_t control_unit;
  ctool_c_translation_unit_t merge_unit;
  ctool_c_translation_unit_t anchor_unit;
  ctool_c_translation_unit_t failed_unit;
  ctool_limits_t limits = ctool_default_limits();
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_c_pp_token_t *snapshot = NULL;
  ctool_arena_mark_t mark;
  const ctool_diagnostic_t *diagnostic;
  const ctool_c_binding_t *anchor_binding;
  ctool_status_t status;
  size_t token_bytes;
  int failed = 1;

  if (control_source == NULL || merge_source == NULL) {
    free(merge_source);
    free(control_source);
    return 1;
  }
  if (parse_valid_fixture(fixture, "/composite-control.c", control_source,
                          &control_unit) != 0 ||
      parse_valid_fixture(fixture, "/composite-success.c", merge_source,
                          &merge_unit) != 0 ||
      merge_unit.graph.type_count != control_unit.graph.type_count + 1u ||
      merge_unit.binding_count != padding_count + 1u ||
      preprocess_fixture(fixture, "/composite-limit.c", merge_source,
                         &merge_tape) != 0 ||
      preprocess_fixture(fixture, "/limited-anchor.c", anchor_source,
                         &anchor_tape) != 0) {
    (void)fprintf(stderr,
                  "boundaries: composite limit control differs\n");
    goto cleanup;
  }
  if ((size_t)control_unit.graph.type_count *
          sizeof(ctool_c_type_node_t) >
      0xffffffffu) {
    goto cleanup;
  }
  limits.output_bytes =
      control_unit.graph.type_count * (ctool_u32)sizeof(ctool_c_type_node_t);
  token_bytes = (size_t)merge_tape.token_count * sizeof(*snapshot);
  snapshot = (ctool_c_pp_token_t *)malloc(token_bytes);
  if (snapshot == NULL) {
    goto cleanup;
  }
  (void)memcpy(snapshot, merge_tape.tokens, token_bytes);
  status = ctool_host_adapter_init(&adapter, host_root);
  if (status != CTOOL_OK) {
    goto cleanup;
  }
  config = ctool_host_job_config(&adapter, limits);
  status = ctool_job_open(&config, &job);
  if (status != CTOOL_OK) {
    goto cleanup;
  }
  (void)memset(&anchor_unit, 0xa5, sizeof(anchor_unit));
  status = ctool_c_parse(job, &anchor_tape, &fixture->parse_request,
                         &anchor_unit);
  anchor_binding = find_binding(&anchor_unit, "limited_anchor_t");
  if (status != CTOOL_OK || anchor_binding == NULL ||
      anchor_unit.binding_count != 1u) {
    (void)fprintf(stderr,
                  "boundaries: limited composite anchor failed\n");
    goto cleanup;
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  (void)memset(&failed_unit, 0xa5, sizeof(failed_unit));
  status = ctool_c_parse(job, &merge_tape, &fixture->parse_request,
                         &failed_unit);
  diagnostic = ctool_job_diagnostic(job, 0u);
  anchor_binding = find_binding(&anchor_unit, "limited_anchor_t");
  if (status != CTOOL_ERR_LIMIT || unit_is_zero(&failed_unit) == 0 ||
      ctool_job_diagnostic_count(job) != 1u || diagnostic == NULL ||
      diagnostic->code != CTOOL_C_PARSE_DIAG_LIMIT ||
      !string_equal(diagnostic->path, "/composite-limit.c") ||
      diagnostic->line != padding_count + 2u || diagnostic->column == 0u ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      memcmp(snapshot, merge_tape.tokens, token_bytes) != 0 ||
      anchor_binding == NULL || anchor_unit.binding_count != 1u ||
      anchor_binding->kind != CTOOL_C_BINDING_TYPEDEF) {
    (void)fprintf(stderr,
                  "boundaries: composite storage rollback differs: %s\n",
                  ctool_status_name(status));
    goto cleanup;
  }
  failed = 0;
cleanup:
  if (job != NULL) {
    ctool_job_close(job);
  }
  free(snapshot);
  free(merge_source);
  free(control_source);
  return failed;
}

static int run_boundaries(const char *host_root) {
  static const frontend_failure_case_t initializer = {
      "object initializer boundary", "int boundary_object = 1;\n",
      CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_UNSUPPORTED};
  static const frontend_failure_case_t body = {
      "control statement boundary",
      "int boundary_function(void) { if (1) return 0; }\n",
      CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_STATEMENT};
  static const frontend_failure_case_t exe = {
      "Cupid #exe boundary", "#exe { }\n", CTOOL_ERR_UNSUPPORTED,
      CTOOL_C_PARSE_DIAG_UNSUPPORTED};
  static const frontend_failure_case_t exe_after_specifier = {
      "Cupid #exe after declaration specifier", "int\n#exe { }\n",
      CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_UNSUPPORTED};
  static const frontend_failure_case_t exe_in_record = {
      "Cupid #exe inside record", "struct S {\n#exe { }\n};\n",
      CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_UNSUPPORTED};
  static const frontend_failure_case_t layout = {
      "layout-stage zero array", "int zero_length[0];\n", CTOOL_ERR_INPUT,
      CTOOL_C_TYPE_DIAG_ARRAY};
  frontend_fixture_t fixture;
  ctool_c_translation_unit_t owned_unit;
  ctool_c_pp_result_t request_tape;
  ctool_c_pp_result_t missing_tokens;
  ctool_c_pp_result_t metadata_tape;
  ctool_c_pp_result_t limit_tape;
  ctool_c_pp_token_t *metadata_tokens = NULL;
  ctool_c_parse_request_t invalid_request;
  size_t token_bytes;
  int owned_valid = 0;
  int failed = 0;

  if (begin_frontend_fixture(&fixture, "boundaries", host_root,
                             64u * 1024u * 1024u) != 0) {
    return 1;
  }
  if (parse_owned_tape(&fixture, &owned_unit) != 0) {
    failed = 1;
  } else {
    owned_valid = 1;
  }
  if (preprocess_fixture(&fixture, "/invalid-request.c",
                         "int request_probe;\n", &request_tape) != 0) {
    failed = 1;
    goto finish;
  }
  if (expect_direct_parse_failure(
          &fixture, "missing tape", NULL, &fixture.parse_request,
          CTOOL_ERR_INVALID_ARGUMENT, CTOOL_C_PARSE_DIAG_INVALID_REQUEST, "",
          0u, 0u) != 0 ||
      expect_direct_parse_failure(
          &fixture, "missing request", &request_tape, NULL,
          CTOOL_ERR_INVALID_ARGUMENT, CTOOL_C_PARSE_DIAG_INVALID_REQUEST,
          "/invalid-request.c", 1u, 18u) != 0) {
    failed = 1;
  }
  invalid_request = fixture.parse_request;
  invalid_request.mode = (ctool_c_pp_mode_t)0;
  if (expect_direct_parse_failure(
          &fixture, "invalid mode", &request_tape, &invalid_request,
          CTOOL_ERR_INVALID_ARGUMENT, CTOOL_C_PARSE_DIAG_INVALID_REQUEST,
          "/invalid-request.c", 1u, 1u) != 0) {
    failed = 1;
  }
  invalid_request = fixture.parse_request;
  invalid_request.gnu_extensions = 2;
  if (expect_direct_parse_failure(
          &fixture, "invalid boolean", &request_tape, &invalid_request,
          CTOOL_ERR_INVALID_ARGUMENT, CTOOL_C_PARSE_DIAG_INVALID_REQUEST,
          "/invalid-request.c", 1u, 1u) != 0) {
    failed = 1;
  }
  missing_tokens.tokens = NULL;
  missing_tokens.token_count = 1u;
  if (expect_direct_parse_failure(
          &fixture, "missing token array", &missing_tokens,
          &fixture.parse_request, CTOOL_ERR_INVALID_ARGUMENT,
          CTOOL_C_PARSE_DIAG_INVALID_REQUEST, "", 0u, 0u) != 0) {
    failed = 1;
  }
  token_bytes =
      (size_t)request_tape.token_count * sizeof(*metadata_tokens);
  metadata_tokens = (ctool_c_pp_token_t *)malloc(token_bytes);
  if (metadata_tokens == NULL) {
    failed = 1;
  } else {
    (void)memcpy(metadata_tokens, request_tape.tokens, token_bytes);
    metadata_tokens[0].pack_alignment = 3u;
    metadata_tape.tokens = metadata_tokens;
    metadata_tape.token_count = request_tape.token_count;
    if (expect_direct_parse_failure(
            &fixture, "invalid token metadata", &metadata_tape,
            &fixture.parse_request, CTOOL_ERR_INVALID_ARGUMENT,
            CTOOL_C_PARSE_DIAG_INVALID_REQUEST, "/invalid-request.c", 1u,
            1u) != 0) {
      failed = 1;
    }
  }
  if (expect_frontend_failure_at(&fixture, &initializer,
                                 "/unsupported-initializer.c", 1u, 21u) !=
          0 ||
      expect_frontend_failure_at(&fixture, &body, "/unsupported-body.c", 1u,
                                 31u) != 0 ||
      expect_frontend_failure_at(&fixture, &layout, "/layout-invalid.c", 1u,
                                 16u) != 0) {
    failed = 1;
  }
  fixture.pp_request.mode = CTOOL_C_PP_MODE_CUPID;
  fixture.parse_request.mode = CTOOL_C_PP_MODE_CUPID;
  if (expect_frontend_failure_at(&fixture, &exe, "/unsupported-exe.cc", 1u,
                                 1u) != 0 ||
      expect_frontend_failure_at(
          &fixture, &exe_after_specifier, "/unsupported-exe-specifier.cc",
          2u, 1u) != 0 ||
      expect_frontend_failure_at(
          &fixture, &exe_in_record, "/unsupported-exe-record.cc", 2u,
          1u) != 0) {
    failed = 1;
  }
  fixture.pp_request.mode = CTOOL_C_PP_MODE_C11;
  fixture.parse_request.mode = CTOOL_C_PP_MODE_C11;
  if (preprocess_fixture(&fixture, "/limit.c", "int limited;\n",
                         &limit_tape) != 0 ||
      validate_configured_storage_limit(&fixture, host_root, &limit_tape) !=
          0 ||
      validate_composite_storage_limit(&fixture, host_root) != 0) {
    failed = 1;
  }
  if (owned_valid == 0 || validate_owned_unit(&owned_unit) != 0 ||
      validate_anchor(&fixture) != 0) {
    failed = 1;
  }
finish:
  free(metadata_tokens);
  if (finish_frontend_fixture(&fixture) != 0) {
    failed = 1;
  }
  if (failed == 0) {
    (void)printf("boundaries: ok\n");
  }
  return failed;
}

static char *build_shared_dag_source(ctool_u32 depth,
                                     ctool_bool flexible_leaf) {
  const size_t capacity = 64u * 1024u;
  char *text = (char *)malloc(capacity);
  size_t used = 0u;
  ctool_u32 index;

  if (text == NULL || depth == 0u) {
    free(text);
    return NULL;
  }
  text[0] = '\0';
  if (append_scale_text(text, capacity, &used,
                        flexible_leaf == CTOOL_TRUE
                            ? "struct DagLeaf { int count; int values[]; };\n"
                            : "struct DagLeaf { int value; };\n") != 0 ||
      append_scale_text(
          text, capacity, &used,
          "union Dag000 { struct DagLeaf left; struct DagLeaf right; };\n") !=
          0) {
    free(text);
    return NULL;
  }
  for (index = 1u; index < depth; index++) {
    char line[160];
    int written = snprintf(
        line, sizeof(line),
        "union Dag%03u { union Dag%03u left; union Dag%03u right; };\n",
        (unsigned int)index, (unsigned int)(index - 1u),
        (unsigned int)(index - 1u));
    if (written <= 0 || (size_t)written >= sizeof(line) ||
        append_scale_text(text, capacity, &used, line) != 0) {
      free(text);
      return NULL;
    }
  }
  {
    char line[96];
    int written = snprintf(
        line, sizeof(line),
        flexible_leaf == CTOOL_TRUE
            ? "struct DagBad { union Dag%03u member; };\n"
            : "struct DagGood { union Dag%03u member; };\n",
        (unsigned int)(depth - 1u));
    if (written <= 0 || (size_t)written >= sizeof(line) ||
        append_scale_text(text, capacity, &used, line) != 0) {
      free(text);
      return NULL;
    }
  }
  return text;
}

static char *build_compatibility_dag_source(ctool_u32 depth) {
  const size_t capacity = 64u * 1024u;
  char *text = (char *)malloc(capacity);
  size_t used = 0u;
  ctool_u32 index;
  if (text == NULL || depth == 0u) {
    free(text);
    return NULL;
  }
  text[0] = '\0';
  for (index = 0u; index < depth; index++) {
    char line[256];
    int written;
    if (index == 0u) {
      written = snprintf(
          line, sizeof(line),
          "typedef int LeftFn%03u(int, int);\n"
          "typedef int RightFn%03u(int, int);\n",
          (unsigned int)index, (unsigned int)index);
    } else {
      written = snprintf(
          line, sizeof(line),
          "typedef int LeftFn%03u(LeftPtr%03u, LeftPtr%03u);\n"
          "typedef int RightFn%03u(RightPtr%03u, RightPtr%03u);\n",
          (unsigned int)index, (unsigned int)(index - 1u),
          (unsigned int)(index - 1u), (unsigned int)index,
          (unsigned int)(index - 1u), (unsigned int)(index - 1u));
    }
    if (written <= 0 || (size_t)written >= sizeof(line) ||
        append_scale_text(text, capacity, &used, line) != 0) {
      free(text);
      return NULL;
    }
    written = snprintf(
        line, sizeof(line),
        "typedef LeftFn%03u *LeftPtr%03u;\n"
        "typedef RightFn%03u *RightPtr%03u;\n",
        (unsigned int)index, (unsigned int)index, (unsigned int)index,
        (unsigned int)index);
    if (written <= 0 || (size_t)written >= sizeof(line) ||
        append_scale_text(text, capacity, &used, line) != 0) {
      free(text);
      return NULL;
    }
  }
  {
    char line[192];
    int written = snprintf(
        line, sizeof(line),
        "extern LeftPtr%03u compatibility_dag;\n"
        "extern RightPtr%03u compatibility_dag;\n",
        (unsigned int)(depth - 1u), (unsigned int)(depth - 1u));
    if (written <= 0 || (size_t)written >= sizeof(line) ||
        append_scale_text(text, capacity, &used, line) != 0) {
      free(text);
      return NULL;
    }
  }
  return text;
}

static char *build_deep_pointer_redeclaration_source(ctool_u32 depth) {
  const size_t capacity = (size_t)depth * 2u + 128u;
  char *text = (char *)malloc(capacity);
  size_t used = 0u;
  ctool_u32 declaration;
  if (text == NULL || depth == 0u) {
    free(text);
    return NULL;
  }
  text[0] = '\0';
  for (declaration = 0u; declaration < 2u; declaration++) {
    ctool_u32 index;
    if (append_scale_text(text, capacity, &used, "extern int ") != 0) {
      free(text);
      return NULL;
    }
    for (index = 0u; index < depth; index++) {
      if (used + 1u >= capacity) {
        free(text);
        return NULL;
      }
      text[used++] = '*';
      text[used] = '\0';
    }
    if (append_scale_text(text, capacity, &used, "deep_pointer;\n") != 0) {
      free(text);
      return NULL;
    }
  }
  return text;
}

static int validate_compatible_redeclarations_unit(
    const ctool_c_translation_unit_t *unit) {
  const ctool_c_binding_t *values = find_binding(unit, "values");
  const ctool_c_binding_t *word = find_binding(unit, "word_t");
  const ctool_c_binding_t *transform = find_binding(unit, "transform");
  const ctool_c_binding_t *local = find_binding(unit, "local_object");
  const ctool_c_binding_t *internal_object =
      find_binding(unit, "internal_object");
  const ctool_c_binding_t *internal_function =
      find_binding(unit, "internal_function");
  const ctool_c_binding_t *incomplete_alias =
      find_binding(unit, "incomplete_array_t");
  const ctool_c_binding_t *completed = find_binding(unit, "completed");
  const ctool_c_binding_t *still_incomplete =
      find_binding(unit, "still_incomplete");
  const ctool_c_binding_t *promoted = find_binding(unit, "promoted");
  const ctool_c_binding_t *no_arguments =
      find_binding(unit, "no_arguments");
  const ctool_c_binding_t *qualified = find_binding(unit, "qualified");
  const ctool_c_binding_t *pointer_qualified =
      find_binding(unit, "pointer_qualified");
  const ctool_c_binding_t *duplicate_const =
      find_binding(unit, "duplicate_const");
  const ctool_c_binding_t *const_pointer =
      find_binding(unit, "const_pointer");
  const ctool_c_binding_t *qualified_completed =
      find_binding(unit, "qualified_completed");
  const ctool_c_binding_t *qualified_still_incomplete =
      find_binding(unit, "qualified_still_incomplete");
  const ctool_c_binding_t *qualified_function =
      find_binding(unit, "qualified_function_t");
  const ctool_c_binding_t *qualified_array_alias =
      find_binding(unit, "qualified_array_t");
  const ctool_c_binding_t *reverse_promoted =
      find_binding(unit, "reverse_promoted");
  const ctool_c_binding_t *promoted_double =
      find_binding(unit, "promoted_double");
  const ctool_c_binding_t *promoted_pointer =
      find_binding(unit, "promoted_pointer");
  const ctool_c_binding_t *combined = find_binding(unit, "combined");
  const ctool_c_binding_t *atomic_qualified =
      find_binding(unit, "atomic_qualified");
  const ctool_c_binding_t *atomic_old_style =
      find_binding(unit, "atomic_old_style");
  const ctool_c_type_node_t *values_type =
      values == NULL ? NULL : type_node(unit, values->type);
  const ctool_c_type_node_t *transform_type =
      transform == NULL ? NULL : type_node(unit, transform->type);
  const ctool_c_type_node_t *incomplete_alias_type =
      incomplete_alias == NULL ? NULL : type_node(unit, incomplete_alias->type);
  const ctool_c_type_node_t *completed_type =
      completed == NULL ? NULL : type_node(unit, completed->type);
  const ctool_c_type_node_t *promoted_type =
      promoted == NULL ? NULL : type_node(unit, promoted->type);
  const ctool_c_type_node_t *no_arguments_type =
      no_arguments == NULL ? NULL : type_node(unit, no_arguments->type);
  const ctool_c_type_node_t *qualified_type =
      qualified == NULL ? NULL : type_node(unit, qualified->type);
  const ctool_c_type_node_t *pointer_qualified_type =
      pointer_qualified == NULL ? NULL : type_node(unit,
                                                   pointer_qualified->type);
  const ctool_c_type_node_t *qualified_parameter_type = NULL;
  const ctool_c_type_node_t *pointer_qualified_parameter_type = NULL;
  const ctool_c_type_node_t *qualified_completed_type =
      qualified_completed == NULL
          ? NULL
          : type_node(unit, qualified_completed->type);
  const ctool_c_type_node_t *qualified_still_type =
      qualified_still_incomplete == NULL
          ? NULL
          : strip_qualified(unit, qualified_still_incomplete->type, NULL);
  const ctool_c_type_node_t *combined_type =
      combined == NULL ? NULL : type_node(unit, combined->type);
  const ctool_c_type_node_t *atomic_qualified_type =
      atomic_qualified == NULL ? NULL : type_node(unit,
                                                 atomic_qualified->type);
  const ctool_c_type_node_t *atomic_parameter_type = NULL;
  const ctool_c_type_node_t *combined_first_pointer = NULL;
  const ctool_c_type_node_t *combined_second_pointer = NULL;
  const ctool_c_type_node_t *combined_first_array = NULL;
  const ctool_c_type_node_t *combined_second_array = NULL;
  if (atomic_qualified_type != NULL &&
      atomic_qualified_type->kind == CTOOL_C_TYPE_FUNCTION &&
      atomic_qualified_type->parameter_count == 1u &&
      atomic_qualified_type->first_parameter <
          unit->graph.parameter_type_count) {
    atomic_parameter_type = type_node(
        unit,
        unit->graph.parameter_types[atomic_qualified_type->first_parameter]);
  }
  if (qualified_type != NULL &&
      qualified_type->kind == CTOOL_C_TYPE_FUNCTION &&
      qualified_type->parameter_count == 1u &&
      qualified_type->first_parameter < unit->graph.parameter_type_count) {
    qualified_parameter_type = type_node(
        unit, unit->graph.parameter_types[qualified_type->first_parameter]);
  }
  if (pointer_qualified_type != NULL &&
      pointer_qualified_type->kind == CTOOL_C_TYPE_FUNCTION &&
      pointer_qualified_type->parameter_count == 1u &&
      pointer_qualified_type->first_parameter <
          unit->graph.parameter_type_count) {
    pointer_qualified_parameter_type = type_node(
        unit,
        unit->graph
            .parameter_types[pointer_qualified_type->first_parameter]);
  }
  if (combined_type != NULL &&
      combined_type->kind == CTOOL_C_TYPE_FUNCTION &&
      combined_type->parameter_count == 2u &&
      combined_type->first_parameter <= unit->graph.parameter_type_count &&
      2u <= unit->graph.parameter_type_count -
                combined_type->first_parameter) {
    combined_first_pointer = type_node(
        unit, unit->graph.parameter_types[combined_type->first_parameter]);
    combined_second_pointer = type_node(
        unit,
        unit->graph.parameter_types[combined_type->first_parameter + 1u]);
    if (combined_first_pointer != NULL &&
        combined_first_pointer->kind == CTOOL_C_TYPE_POINTER) {
      combined_first_array =
          type_node(unit, combined_first_pointer->referenced_type);
    }
    if (combined_second_pointer != NULL &&
        combined_second_pointer->kind == CTOOL_C_TYPE_POINTER) {
      combined_second_array =
          type_node(unit, combined_second_pointer->referenced_type);
    }
  }

  if (unit->binding_count != 31u || values == NULL || word == NULL ||
      transform == NULL || local == NULL || internal_object == NULL ||
      internal_function == NULL || incomplete_alias == NULL ||
      completed == NULL || still_incomplete == NULL || promoted == NULL ||
      no_arguments == NULL || qualified == NULL || pointer_qualified == NULL ||
      duplicate_const == NULL || const_pointer == NULL ||
      qualified_completed == NULL || qualified_still_incomplete == NULL ||
      qualified_function == NULL || qualified_array_alias == NULL ||
      reverse_promoted == NULL || promoted_double == NULL ||
      promoted_pointer == NULL || combined == NULL ||
      atomic_qualified == NULL || atomic_old_style == NULL ||
      values_type == NULL ||
      values->kind != CTOOL_C_BINDING_OBJECT ||
      values->storage != CTOOL_C_STORAGE_EXTERN ||
      values->linkage != CTOOL_C_LINKAGE_EXTERNAL ||
      values_type->kind != CTOOL_C_TYPE_ARRAY ||
      values_type->array_bound_kind != CTOOL_C_ARRAY_FIXED ||
      values_type->element_count != 4u ||
      unit->layout.types[values->type].size != 16u ||
      word->kind != CTOOL_C_BINDING_TYPEDEF ||
      word->linkage != CTOOL_C_LINKAGE_NONE || transform_type == NULL ||
      transform->kind != CTOOL_C_BINDING_FUNCTION ||
      transform->linkage != CTOOL_C_LINKAGE_EXTERNAL ||
      transform_type->kind != CTOOL_C_TYPE_FUNCTION ||
      transform_type->has_prototype != CTOOL_TRUE ||
      transform_type->parameter_count != 1u ||
      local->kind != CTOOL_C_BINDING_OBJECT ||
      local->storage != CTOOL_C_STORAGE_STATIC ||
      local->linkage != CTOOL_C_LINKAGE_INTERNAL ||
      internal_object->kind != CTOOL_C_BINDING_OBJECT ||
      internal_object->storage != CTOOL_C_STORAGE_STATIC ||
      internal_object->linkage != CTOOL_C_LINKAGE_INTERNAL ||
      internal_function->kind != CTOOL_C_BINDING_FUNCTION ||
      internal_function->storage != CTOOL_C_STORAGE_STATIC ||
      internal_function->linkage != CTOOL_C_LINKAGE_INTERNAL ||
      incomplete_alias->kind != CTOOL_C_BINDING_TYPEDEF ||
      incomplete_alias_type == NULL ||
      incomplete_alias_type->kind != CTOOL_C_TYPE_ARRAY ||
      incomplete_alias_type->array_bound_kind !=
          CTOOL_C_ARRAY_UNSPECIFIED ||
      completed->kind != CTOOL_C_BINDING_OBJECT || completed_type == NULL ||
      completed_type->kind != CTOOL_C_TYPE_ARRAY ||
      completed_type->array_bound_kind != CTOOL_C_ARRAY_FIXED ||
      completed_type->element_count != 4u ||
      unit->layout.types[completed->type].size != 16u ||
      still_incomplete->type != incomplete_alias->type ||
      promoted_type == NULL || promoted_type->kind != CTOOL_C_TYPE_FUNCTION ||
      promoted_type->has_prototype != CTOOL_TRUE ||
      promoted_type->parameter_count != 1u || no_arguments_type == NULL ||
      no_arguments_type->kind != CTOOL_C_TYPE_FUNCTION ||
      no_arguments_type->has_prototype != CTOOL_TRUE ||
      no_arguments_type->parameter_count != 0u ||
      qualified->kind != CTOOL_C_BINDING_FUNCTION || qualified_type == NULL ||
      qualified_type->kind != CTOOL_C_TYPE_FUNCTION ||
      qualified_type->parameter_count != 1u ||
      qualified_parameter_type == NULL ||
      qualified_parameter_type->kind != CTOOL_C_TYPE_SIGNED_INT ||
      pointer_qualified->kind != CTOOL_C_BINDING_FUNCTION ||
      pointer_qualified_type == NULL ||
      pointer_qualified_type->kind != CTOOL_C_TYPE_FUNCTION ||
      pointer_qualified_type->parameter_count != 1u ||
      pointer_qualified_parameter_type == NULL ||
      pointer_qualified_parameter_type->kind != CTOOL_C_TYPE_POINTER ||
      pointer_qualified_parameter_type->qualifiers != 0u ||
      duplicate_const->kind != CTOOL_C_BINDING_OBJECT ||
      const_pointer->kind != CTOOL_C_BINDING_OBJECT ||
      qualified_completed_type == NULL ||
      qualified_completed_type->kind != CTOOL_C_TYPE_ARRAY ||
      qualified_completed_type->array_bound_kind != CTOOL_C_ARRAY_FIXED ||
      qualified_completed_type->element_count != 4u ||
      qualified_still_type == NULL ||
      qualified_still_type->kind != CTOOL_C_TYPE_ARRAY ||
      qualified_still_type->array_bound_kind !=
          CTOOL_C_ARRAY_UNSPECIFIED ||
      qualified_function->kind != CTOOL_C_BINDING_TYPEDEF ||
      qualified_array_alias->kind != CTOOL_C_BINDING_TYPEDEF ||
      reverse_promoted->kind != CTOOL_C_BINDING_FUNCTION ||
      promoted_double->kind != CTOOL_C_BINDING_FUNCTION ||
      promoted_pointer->kind != CTOOL_C_BINDING_FUNCTION ||
      combined_type == NULL || combined_type->kind != CTOOL_C_TYPE_FUNCTION ||
      combined_type->parameter_count != 2u ||
      combined_first_array == NULL || combined_second_array == NULL ||
      combined_first_array->kind != CTOOL_C_TYPE_ARRAY ||
      combined_first_array->array_bound_kind != CTOOL_C_ARRAY_FIXED ||
      combined_first_array->element_count != 4u ||
      combined_second_array->kind != CTOOL_C_TYPE_ARRAY ||
      combined_second_array->array_bound_kind != CTOOL_C_ARRAY_FIXED ||
      combined_second_array->element_count != 5u ||
      atomic_qualified_type == NULL ||
      atomic_qualified_type->kind != CTOOL_C_TYPE_FUNCTION ||
      atomic_parameter_type == NULL ||
      atomic_parameter_type->kind != CTOOL_C_TYPE_SIGNED_INT ||
      atomic_parameter_type->qualifiers != CTOOL_C_QUAL_ATOMIC ||
      atomic_old_style->kind != CTOOL_C_BINDING_FUNCTION) {
    (void)fprintf(stderr,
                  "semantics: compatible redeclarations differ\n");
    return 1;
  }
  return 0;
}

static int run_function_specifiers(const char *host_root) {
  static const char source[] =
      "static inline int local_helper(void);\n"
      "static int local_helper(void);\n"
      "inline inline int repeated(int value);\n"
      "extern int repeated(int renamed);\n"
      "int later_inline(void);\n"
      "int inline later_inline(void);\n"
      "__attribute__((noreturn)) inline void inline_fatal(void);\n"
      "int ordinary(void);\n"
      "static inline int body(void) { return 1; }\n";
  static const frontend_exact_failure_case_t invalid_cases[] = {
      {{"inline object", "inline int object;\n", CTOOL_ERR_INPUT,
        CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS},
       1u, 1u, "inline function specifier requires a function declaration"},
      {{"inline function-pointer object",
        "inline int (*function_pointer)(void);\n", CTOOL_ERR_INPUT,
        CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS},
       1u, 1u, "inline function specifier requires a function declaration"},
      {{"inline typedef", "inline typedef int alias_t;\n", CTOOL_ERR_INPUT,
        CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS},
       1u, 1u, "inline function specifier requires a function declaration"},
      {{"inline empty tag declaration", "inline struct Tag;\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS},
       1u, 1u, "inline function specifier requires a function declarator"},
      {{"inline parameter", "int consume(inline int callback(void));\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS},
       1u, 13u,
       "function specifier cannot apply to a parameter declaration"},
      {{"inline record member", "struct Record { inline int member; };\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS},
       1u, 17u, "function specifier cannot apply to a record member"},
      {{"inline type name",
        "_Static_assert(sizeof(inline int) == 4, \"inline type name\");\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS},
       1u, 23u, "function specifier cannot appear in a type name"},
      {{"mixed inline declarators", "inline int function(void), object;\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS},
       1u, 1u, "inline function specifier requires a function declaration"}};
  frontend_fixture_t fixture;
  ctool_c_translation_unit_t unit;
  const ctool_c_binding_t *local_helper;
  const ctool_c_binding_t *repeated;
  const ctool_c_binding_t *later_inline;
  const ctool_c_binding_t *inline_fatal;
  const ctool_c_binding_t *ordinary;
  const ctool_c_binding_t *body_binding;
  const ctool_c_function_definition_t *body_definition;
  const ctool_c_statement_t *body_return;
  ctool_u32 body_value;
  ctool_u32 index;
  int failed = 1;

  if (begin_frontend_fixture(&fixture, "function-specifiers", host_root,
                             32u * 1024u * 1024u) != 0) {
    return 1;
  }
  if (parse_valid_fixture(&fixture, "/function-specifiers.c", source,
                          &unit) != 0) {
    goto cleanup;
  }
  local_helper = find_binding(&unit, "local_helper");
  repeated = find_binding(&unit, "repeated");
  later_inline = find_binding(&unit, "later_inline");
  inline_fatal = find_binding(&unit, "inline_fatal");
  ordinary = find_binding(&unit, "ordinary");
  body_binding = find_binding(&unit, "body");
  body_definition = find_function_definition(&unit, "body");
  body_return = scalar_return_statement(&unit, "body");
  body_value = scalar_return_value(&unit, "body", CTOOL_C_TYPE_SIGNED_INT);
  body_value = scalar_unwrap_conversions(&unit, body_value);
  if (local_helper == NULL || repeated == NULL || later_inline == NULL ||
      inline_fatal == NULL || ordinary == NULL || body_binding == NULL ||
      local_helper->kind != CTOOL_C_BINDING_FUNCTION ||
      local_helper->linkage != CTOOL_C_LINKAGE_INTERNAL ||
      local_helper->function_declaration_flags !=
          CTOOL_C_FUNCTION_DECL_INLINE ||
      repeated->kind != CTOOL_C_BINDING_FUNCTION ||
      repeated->linkage != CTOOL_C_LINKAGE_EXTERNAL ||
      repeated->function_declaration_flags != CTOOL_C_FUNCTION_DECL_INLINE ||
      later_inline->function_declaration_flags !=
          CTOOL_C_FUNCTION_DECL_INLINE ||
      later_inline->storage != CTOOL_C_STORAGE_NONE ||
      later_inline->location.line != 5u ||
      inline_fatal->function_declaration_flags !=
          CTOOL_C_FUNCTION_DECL_INLINE ||
      inline_fatal->attributes != CTOOL_C_DECL_ATTR_NORETURN ||
      ordinary->function_declaration_flags != 0u ||
      body_binding->linkage != CTOOL_C_LINKAGE_INTERNAL ||
      body_binding->function_declaration_flags !=
          CTOOL_C_FUNCTION_DECL_INLINE ||
      body_definition == NULL ||
      body_definition->storage != CTOOL_C_STORAGE_STATIC ||
      body_definition->function_declaration_flags !=
          CTOOL_C_FUNCTION_DECL_INLINE ||
      body_return == NULL || body_return->expression == CTOOL_C_AST_NONE ||
      body_value == CTOOL_C_AST_NONE ||
      unit.expressions[body_value].kind !=
          CTOOL_C_EXPRESSION_INTEGER_CONSTANT ||
      unit.expressions[body_value].integer_bits != 1ull) {
    (void)fprintf(stderr,
                  "function-specifiers: retained declaration metadata "
                  "differs\n");
    goto cleanup;
  }
  failed = 0;
  for (index = 0u; index < ARRAY_COUNT(invalid_cases); index++) {
    const frontend_exact_failure_case_t *test_case = &invalid_cases[index];
    if (expect_frontend_failure_at_message(
            &fixture, &test_case->failure,
            "/function-specifier-invalid.c", test_case->line,
            test_case->column, test_case->message) != 0) {
      failed = 1;
    }
  }
cleanup:
  if (finish_frontend_fixture(&fixture) != 0) {
    failed = 1;
  }
  if (failed == 0) {
    (void)printf("function-specifiers: ok\n");
  }
  return failed;
}

static int run_semantics(const char *host_root) {
  static const frontend_failure_case_t invalid_cases[] = {
      {"sole flexible-array member", "struct S { int only[]; };\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_DECLARATOR},
      {"promoted anonymous-member collision",
       "struct S { struct { int value; }; int value; };\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_REDEFINITION},
      {"anonymous enum member", "struct S { enum { VALUE }; };\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_DECLARATOR},
      {"repeated int scalar", "int int value;\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS},
      {"bool-int scalar combination", "_Bool int value;\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS},
      {"char-int scalar combination", "char int value;\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS},
      {"double-int scalar combination", "double int value;\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS},
      {"signed-double scalar combination", "signed double value;\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS},
      {"typedef-name scalar combination",
       "typedef int value_t; value_t unsigned value;\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS},
      {"duplicate typedef storage", "typedef typedef int value_t;\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS},
      {"duplicate static storage", "static static int value;\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS},
      {"duplicate extern storage", "extern extern int value;\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS},
      {"conflicting storage", "static extern int value;\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS},
      {"file-scope auto storage", "auto int value;\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS},
      {"file-scope register storage", "register int value;\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS},
      {"restrict-qualified scalar", "restrict int value;\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS},
      {"named void parameter", "int function(void named);\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_TYPE_NAME},
      {"void parameter followed by another",
       "int function(void, int other);\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_TYPE_NAME},
      {"typedef-void named parameter",
       "typedef void V; int function(V named);\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_TYPE_NAME},
      {"duplicate parameter names",
       "int function(int duplicate, int duplicate);\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_REDEFINITION},
      {"parameter hides typedef",
       "typedef int T; int function(int T, T other);\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS},
      {"scalar declaration without declarator", "int;\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_DECLARATOR},
      {"qualified scalar declaration without declarator", "const int;\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_DECLARATOR},
      {"typedef-name declaration without declarator",
       "typedef int T; T;\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_DECLARATOR},
      {"void object", "void object;\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_TYPE_NAME},
      {"typedef-void object", "typedef void V; V object;\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_TYPE_NAME},
      {"void record member", "struct S { void member; };\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_TYPE_NAME},
      {"array of void", "void values[1];\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_TYPE_NAME},
      {"temporally incomplete record member",
       "struct A; struct B { struct A member; }; "
       "struct A { int value; };\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_TYPE_NAME},
      {"temporally incomplete record-array member",
       "struct A; struct B { struct A members[2]; }; "
       "struct A { int value; };\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_TYPE_NAME},
      {"temporally incomplete typedef array",
       "struct Incomplete; "
       "typedef struct Incomplete Premature[2]; "
       "struct Incomplete { int value; };\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_TYPE_NAME},
      {"qualified array return",
       "typedef int A[4]; const A returns_array(void);\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_DECLARATOR},
      {"qualified function return",
       "typedef int F(void); const F returns_function(void);\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS},
      {"qualified function declaration",
       "typedef int F(void); const F qualified_function;\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS},
      {"ordinary reserved identifier", "int for;\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_DECLARATOR},
      {"reserved tag identifier", "struct while { int value; };\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_TYPE_NAME},
      {"reserved member identifier", "struct S { int return; };\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_DECLARATOR},
      {"reserved parameter identifier", "int f(int switch);\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_DECLARATOR},
      {"conflicting object redeclaration", "extern int value; long value;\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_REDEFINITION},
      {"conflicting array redeclaration",
       "extern int values[2]; extern int values[3];\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_REDEFINITION},
      {"conflicting function redeclaration",
       "int function(int value); int function(long value);\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_REDEFINITION},
      {"conflicting linkage redeclaration",
       "extern int value; static int value;\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_REDEFINITION},
      {"object loses internal linkage",
       "static int value; int value;\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_REDEFINITION},
      {"conflicting typedef redeclaration",
       "typedef int word_t; typedef long word_t;\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_REDEFINITION},
      {"typedef incomplete/fixed mismatch",
       "typedef int array_t[]; typedef int array_t[4];\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_REDEFINITION},
      {"old-style char promotion mismatch",
       "int function(); int function(char value);\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_REDEFINITION},
      {"old-style float promotion mismatch",
       "int function(); int function(float value);\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_REDEFINITION},
      {"old-style variadic mismatch",
       "int function(); int function(int value, ...);\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_REDEFINITION},
      {"pointer referent qualifier mismatch",
       "typedef int *pointer_t; extern const pointer_t value; "
       "extern const int *value;\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_REDEFINITION},
      {"atomic scalar parameter mismatch",
       "int function(_Atomic int value); int function(int value);\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_REDEFINITION},
      {"atomic pointer parameter mismatch",
       "int function(int *_Atomic value); int function(int *value);\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_REDEFINITION},
      {"old-style atomic char promotion mismatch",
       "int function(); int function(_Atomic char value);\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_REDEFINITION}};
  static const char *const keywords[] = {
      "auto",          "break",       "case",       "char",
      "const",         "continue",    "default",    "do",
      "double",        "else",        "enum",       "extern",
      "float",         "for",         "goto",       "if",
      "inline",        "int",         "long",       "register",
      "restrict",      "return",      "short",      "signed",
      "sizeof",        "static",      "struct",     "switch",
      "typedef",       "union",       "unsigned",   "void",
      "volatile",      "while",       "_Alignas",   "_Alignof",
      "_Atomic",       "_Bool",       "_Complex",   "_Generic",
      "_Imaginary",    "_Noreturn",   "_Pragma",    "_Static_assert",
      "_Thread_local"};
  static const char prototype_scope_source[] =
      "int scoped(struct PrototypeRecord { int inner; } object,\n"
      "           enum PrototypeEnum { PROTOTYPE_VALUE = 1 } choice);\n"
      "struct PrototypeRecord { int outer; };\n"
      "enum PrototypeEnum { OUTER_VALUE = 2 };\n";
  static const char prototype_shadow_source[] =
      "struct SharedTag { int outer_member; };\n"
      "enum OuterEnum { A = 11 };\n"
      "int local_tag(struct SharedTag { int prototype_member; } value);\n"
      "int local_enum(enum LocalEnum { A = 23 } value);\n"
      "int nested(int A,\n"
      "           int callback(enum NestedEnum { A = 31 } value));\n"
      "struct SharedTag outer_object;\n"
      "enum OuterEnum outer_enum_object;\n";
  static const char incomplete_declarations_source[] =
      "struct Forward;\n"
      "int accept_forward(struct Forward value);\n"
      "extern int values[];\n";
  static const char compatible_redeclarations_source[] =
      "extern int values[];\n"
      "extern int values[4];\n"
      "extern int values[];\n"
      "typedef int word_t;\n"
      "typedef int word_t;\n"
      "int transform(int value);\n"
      "extern int transform(int renamed);\n"
      "static int local_object;\n"
      "static int local_object;\n"
      "static int internal_object;\n"
      "extern int internal_object;\n"
      "static int internal_function(void);\n"
      "int internal_function(void);\n"
      "extern int internal_function(void);\n"
      "typedef int incomplete_array_t[];\n"
      "extern incomplete_array_t completed;\n"
      "extern int completed[4];\n"
      "extern incomplete_array_t still_incomplete;\n"
      "int promoted();\n"
      "int promoted(int value);\n"
      "int no_arguments();\n"
      "int no_arguments(void);\n"
      "int qualified(const int value);\n"
      "int qualified(int renamed);\n"
      "int pointer_qualified(int *restrict value);\n"
      "int pointer_qualified(int *renamed);\n"
      "typedef const int const_int_t;\n"
      "extern const const_int_t duplicate_const;\n"
      "extern const int duplicate_const;\n"
      "typedef int *pointer_alias_t;\n"
      "extern const pointer_alias_t const_pointer;\n"
      "extern int *const const_pointer;\n"
      "typedef int fixed_array_t[4];\n"
      "extern const fixed_array_t qualified_array;\n"
      "extern const int qualified_array[4];\n"
      "typedef int incomplete_qualified_array_t[];\n"
      "extern const incomplete_qualified_array_t qualified_completed;\n"
      "extern const int qualified_completed[4];\n"
      "extern const incomplete_qualified_array_t qualified_still_incomplete;\n"
      "typedef int qualified_function_t(const int);\n"
      "typedef int qualified_function_t(int);\n"
      "typedef int base_array_t[4];\n"
      "typedef const base_array_t qualified_array_t;\n"
      "typedef const int qualified_array_t[4];\n"
      "int reverse_promoted(int value);\n"
      "int reverse_promoted();\n"
      "int promoted_double();\n"
      "int promoted_double(double value);\n"
      "int promoted_pointer();\n"
      "int promoted_pointer(int *value);\n"
      "int combined(int (*first)[4], int (*second)[]);\n"
      "int combined(int (*first)[], int (*second)[5]);\n"
      "int atomic_qualified(_Atomic const int value);\n"
      "int atomic_qualified(_Atomic int renamed);\n"
      "int atomic_old_style();\n"
      "int atomic_old_style(_Atomic int value);\n";
  static const char strict_flexible_union_source[] =
      "struct FlexibleLeaf { int count; int values[]; };\n"
      "union FlexibleUnion { struct FlexibleLeaf leaf; int alternate; };\n";
  static const frontend_failure_case_t strict_invalid_cases[] = {
      {"structure embeds flexible-array record",
       "struct FlexibleLeaf { int count; int values[]; };\n"
       "struct Invalid { struct FlexibleLeaf member; };\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_TYPE_NAME},
      {"structure recursively embeds flexible-array union",
       "struct FlexibleLeaf { int count; int values[]; };\n"
       "union FlexibleUnion { struct FlexibleLeaf leaf; int alternate; };\n"
       "struct Invalid { union FlexibleUnion member; };\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_TYPE_NAME},
      {"array element is flexible-array record",
       "struct FlexibleLeaf { int count; int values[]; };\n"
       "extern struct FlexibleLeaf invalid_values[2];\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_TYPE_NAME},
      {"array element recursively contains flexible-array union",
       "struct FlexibleLeaf { int count; int values[]; };\n"
       "union FlexibleUnion { struct FlexibleLeaf leaf; int alternate; };\n"
       "extern union FlexibleUnion invalid_values[2];\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_TYPE_NAME},
      {"typedef array element is flexible-array record",
       "struct FlexibleLeaf { int count; int values[]; };\n"
       "typedef struct FlexibleLeaf InvalidArray[2];\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_TYPE_NAME},
      {"parameter array element is flexible-array record",
       "struct FlexibleLeaf { int count; int values[]; };\n"
       "int consume(struct FlexibleLeaf values[2]);\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_TYPE_NAME},
      {"union array element is flexible-array record",
       "struct FlexibleLeaf { int count; int values[]; };\n"
       "union Invalid { struct FlexibleLeaf values[2]; int alternate; };\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_TYPE_NAME}};
  static const char qualified_source[] =
      "typedef void VoidAlias;\n"
      "int normalized(VoidAlias);\n"
      "typedef int ArrayAlias[4];\n"
      "typedef int FunctionAlias(void);\n"
      "const ArrayAlias array_object;\n"
      "FunctionAlias declared_function;\n"
      "const int scalar_return(void);\n"
      "int consume(const ArrayAlias values);\n"
      "typedef struct { struct { int promoted; }; int tail[]; } valid_fam_t;\n"
      "typedef int Hidden;\n"
      "int hidden_ok(int Hidden, int other);\n"
      "int old(int ());\n"
      "int proto(int (void));\n"
      "int class;\n"
      "extern int external_object;\n"
      "static int internal_object, internal_object_second;\n";
  frontend_fixture_t fixture;
  ctool_c_translation_unit_t unit;
  ctool_u32 index;
  int failed = 0;

  if (begin_frontend_fixture(&fixture, "semantics", host_root,
                             128u * 1024u * 1024u) != 0) {
    return 1;
  }
  if (parse_valid_fixture(&fixture, "/prototype-scope.c",
                          prototype_scope_source, &unit) != 0 ||
      validate_prototype_scope_unit(&unit) != 0) {
    failed = 1;
  }
  if (parse_valid_fixture(&fixture, "/prototype-shadow.c",
                          prototype_shadow_source, &unit) != 0 ||
      validate_prototype_shadow_unit(&unit) != 0) {
    failed = 1;
  }
  if (parse_valid_fixture(&fixture, "/incomplete-declarations.c",
                          incomplete_declarations_source, &unit) != 0 ||
      validate_incomplete_declarations_unit(&unit) != 0) {
    failed = 1;
  }
  if (parse_valid_fixture(&fixture, "/compatible-redeclarations.c",
                          compatible_redeclarations_source, &unit) != 0 ||
      validate_compatible_redeclarations_unit(&unit) != 0) {
    failed = 1;
  }
  {
    char *compatibility_dag = build_compatibility_dag_source(24u);
    char *deep_pointer = build_deep_pointer_redeclaration_source(512u);
    if (compatibility_dag == NULL || deep_pointer == NULL) {
      (void)fprintf(stderr,
                    "semantics: redeclaration scale fixture failed\n");
      failed = 1;
    } else {
      if (parse_valid_fixture(&fixture, "/compatibility-dag.c",
                              compatibility_dag, &unit) != 0 ||
          find_binding(&unit, "compatibility_dag") == NULL) {
        failed = 1;
      }
      if (parse_valid_fixture(&fixture, "/deep-pointer.c", deep_pointer,
                              &unit) != 0 ||
          unit.binding_count != 1u ||
          find_binding(&unit, "deep_pointer") == NULL) {
        failed = 1;
      }
    }
    free(compatibility_dag);
    free(deep_pointer);
  }
  if (parse_valid_fixture(&fixture, "/qualified.c", qualified_source, &unit) !=
          0 ||
      validate_qualified_unit(&unit) != 0) {
    failed = 1;
  }
  for (index = 0u; index < ARRAY_COUNT(invalid_cases); index++) {
    if (expect_frontend_failure(&fixture, &invalid_cases[index],
                                "/semantic-invalid.c") != 0) {
      failed = 1;
    }
  }
  fixture.pp_request.gnu_extensions = CTOOL_FALSE;
  fixture.parse_request.gnu_extensions = CTOOL_FALSE;
  if (parse_valid_fixture(&fixture, "/strict-flexible-union.c",
                          strict_flexible_union_source, &unit) != 0 ||
      validate_strict_flexible_union_unit(&unit) != 0) {
    failed = 1;
  }
  for (index = 0u; index < ARRAY_COUNT(strict_invalid_cases); index++) {
    ctool_u32 expected_line = index == 1u || index == 3u ? 3u : 2u;
    if (expect_frontend_failure_at(
            &fixture, &strict_invalid_cases[index], "/strict-invalid.c",
            expected_line, 0u) != 0) {
      failed = 1;
    }
  }
  {
    const ctool_u32 dag_depth = 24u;
    char *plain_dag_source =
        build_shared_dag_source(dag_depth, CTOOL_FALSE);
    char *dag_source = build_shared_dag_source(dag_depth, CTOOL_TRUE);
    frontend_failure_case_t dag_case;
    if (plain_dag_source == NULL || dag_source == NULL) {
      (void)fprintf(stderr, "semantics: flexible-array DAG overflow\n");
      failed = 1;
    } else {
      if (parse_valid_fixture(&fixture, "/strict-plain-dag.c",
                              plain_dag_source, &unit) != 0) {
        failed = 1;
      }
      dag_case.name = "shared flexible-array record DAG";
      dag_case.source = dag_source;
      dag_case.status = CTOOL_ERR_INPUT;
      dag_case.diagnostic_code = CTOOL_C_PARSE_DIAG_TYPE_NAME;
      if (expect_frontend_failure_at(
              &fixture, &dag_case, "/strict-dag.c", dag_depth + 2u,
              0u) != 0) {
        failed = 1;
      }
    }
    free(plain_dag_source);
    free(dag_source);
  }
  fixture.pp_request.gnu_extensions = CTOOL_TRUE;
  fixture.parse_request.gnu_extensions = CTOOL_TRUE;
  for (index = 0u; index < ARRAY_COUNT(keywords); index++) {
    char source[128];
    frontend_failure_case_t test_case;
    int written = snprintf(source, sizeof(source),
                           "enum Keyword { %s = 1 };\n", keywords[index]);
    if (written <= 0 || (size_t)written >= sizeof(source)) {
      (void)fprintf(stderr, "semantics: keyword fixture overflow\n");
      failed = 1;
      continue;
    }
    test_case.name = keywords[index];
    test_case.source = source;
    test_case.status = CTOOL_ERR_INPUT;
    test_case.diagnostic_code = CTOOL_C_PARSE_DIAG_DECLARATOR;
    if (expect_frontend_failure(&fixture, &test_case, "/keyword.c") != 0) {
      failed = 1;
    }
  }
  {
    static const frontend_failure_case_t cupid_keyword = {
        "Cupid-mode class identifier", "enum CupidKeyword { class = 1 };\n",
        CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_DECLARATOR};
    fixture.pp_request.mode = CTOOL_C_PP_MODE_CUPID;
    fixture.parse_request.mode = CTOOL_C_PP_MODE_CUPID;
    if (expect_frontend_failure(&fixture, &cupid_keyword,
                                "/cupid-keyword.cc") != 0) {
      failed = 1;
    }
    fixture.pp_request.mode = CTOOL_C_PP_MODE_C11;
    fixture.parse_request.mode = CTOOL_C_PP_MODE_C11;
  }
  if (finish_frontend_fixture(&fixture) != 0) {
    failed = 1;
  }
  if (failed == 0) {
    (void)printf("semantics: ok\n");
  }
  return failed;
}

static int validate_constant_unit(const ctool_c_translation_unit_t *unit) {
  typedef struct {
    const char *name;
    ctool_u64 bits;
    ctool_bool is_unsigned;
    ctool_c_type_kind_t binding_type;
  } constant_oracle_t;
  static const constant_oracle_t oracles[] = {
      {"NEGATIVE_DIVIDE", 0xfffffffffffffffeull, CTOOL_FALSE,
       CTOOL_C_TYPE_SIGNED_INT},
      {"NEGATIVE_REMAINDER", 0xffffffffffffffffull, CTOOL_FALSE,
       CTOOL_C_TYPE_SIGNED_INT},
      {"NEGATIVE_SHIFT", 0xfffffffffffffffeull, CTOOL_FALSE,
       CTOOL_C_TYPE_SIGNED_INT},
      {"POSITIVE_NEGATIVE_DIVIDE", 0xfffffffffffffffeull, CTOOL_FALSE,
       CTOOL_C_TYPE_SIGNED_INT},
      {"POSITIVE_NEGATIVE_REMAINDER", 1ull, CTOOL_FALSE,
       CTOOL_C_TYPE_SIGNED_INT},
      {"UNSIGNED_WRAP", 0ull, CTOOL_TRUE, CTOOL_C_TYPE_SIGNED_INT},
      {"UNSIGNED_SHIFT", 1ull, CTOOL_TRUE, CTOOL_C_TYPE_SIGNED_INT},
      {"LOGICAL_NOT", 0ull, CTOOL_FALSE, CTOOL_C_TYPE_SIGNED_INT},
      {"LOGICAL_AND", 1ull, CTOOL_FALSE, CTOOL_C_TYPE_SIGNED_INT},
      {"LOGICAL_OR", 1ull, CTOOL_FALSE, CTOOL_C_TYPE_SIGNED_INT},
      {"LOGICAL_PRECEDENCE", 1ull, CTOOL_FALSE,
       CTOOL_C_TYPE_SIGNED_INT},
      {"LOGICAL_BITWISE_PRECEDENCE", 1ull, CTOOL_FALSE,
       CTOOL_C_TYPE_SIGNED_INT},
      {"LOGICAL_SHORT_AND", 0ull, CTOOL_FALSE,
       CTOOL_C_TYPE_SIGNED_INT},
      {"LOGICAL_SHORT_OR", 1ull, CTOOL_FALSE,
       CTOOL_C_TYPE_SIGNED_INT},
      {"MIXED_FIRST_NEGATIVE", 0xffffffffffffffffull, CTOOL_FALSE,
       CTOOL_C_TYPE_SIGNED_LONG_LONG},
      {"MIXED_FIRST_LARGE", 0xffffffffull, CTOOL_TRUE,
       CTOOL_C_TYPE_SIGNED_LONG_LONG},
      {"MIXED_LAST_LARGE", 0xffffffffull, CTOOL_TRUE,
       CTOOL_C_TYPE_SIGNED_LONG_LONG},
      {"MIXED_LAST_NEGATIVE", 0xffffffffffffffffull, CTOOL_FALSE,
       CTOOL_C_TYPE_SIGNED_LONG_LONG},
      {"DECLARED_A", 1ull, CTOOL_TRUE, CTOOL_C_TYPE_SIGNED_INT},
      {"DECLARED_B", 0xffffffffffffffffull, CTOOL_FALSE,
       CTOOL_C_TYPE_SIGNED_INT}};
  static const char *const signed_int_enums[] = {"SignedOperations",
                                                 "DeclaredExpression"};
  static const char *const signed_long_long_enums[] = {"MixedFirst",
                                                       "MixedLast"};
  ctool_u32 index;

  for (index = 0u; index < ARRAY_COUNT(oracles); index++) {
    const ctool_c_binding_t *binding = find_binding(unit, oracles[index].name);
    const ctool_c_type_node_t *binding_type =
        binding == NULL ? NULL : type_node(unit, binding->type);
    if (binding == NULL || binding->kind != CTOOL_C_BINDING_ENUMERATOR ||
        binding->integer_bits != oracles[index].bits ||
        binding->integer_unsigned != oracles[index].is_unsigned ||
        binding_type == NULL ||
        binding_type->kind != oracles[index].binding_type) {
      (void)fprintf(stderr, "constants: enumerator %s differs\n",
                    oracles[index].name);
      return 1;
    }
  }
  for (index = 0u; index < ARRAY_COUNT(signed_int_enums); index++) {
    const ctool_c_tag_t *tag = find_tag(unit, signed_int_enums[index]);
    const ctool_c_type_node_t *enumeration =
        tag == NULL ? NULL : type_node(unit, tag->type);
    const ctool_c_type_node_t *compatible =
        enumeration == NULL ? NULL
                            : type_node(unit, enumeration->referenced_type);
    const ctool_c_type_layout_t *layout =
        tag == NULL ? NULL : type_layout(unit, tag->type);
    if (enumeration == NULL || enumeration->kind != CTOOL_C_TYPE_ENUM ||
        compatible == NULL || compatible->kind != CTOOL_C_TYPE_SIGNED_INT ||
        layout == NULL || layout->size != 4u || layout->alignment != 4u ||
        layout->is_signed != CTOOL_TRUE) {
      (void)fprintf(stderr, "constants: %s compatibility differs\n",
                    signed_int_enums[index]);
      return 1;
    }
  }
  for (index = 0u; index < ARRAY_COUNT(signed_long_long_enums); index++) {
    const ctool_c_tag_t *tag = find_tag(unit, signed_long_long_enums[index]);
    const ctool_c_type_node_t *enumeration =
        tag == NULL ? NULL : type_node(unit, tag->type);
    const ctool_c_type_node_t *compatible =
        enumeration == NULL ? NULL
                            : type_node(unit, enumeration->referenced_type);
    const ctool_c_type_layout_t *layout =
        tag == NULL ? NULL : type_layout(unit, tag->type);
    if (enumeration == NULL || enumeration->kind != CTOOL_C_TYPE_ENUM ||
        compatible == NULL ||
        compatible->kind != CTOOL_C_TYPE_SIGNED_LONG_LONG || layout == NULL ||
        layout->size != 8u || layout->alignment != 4u ||
        layout->is_signed != CTOOL_TRUE) {
      (void)fprintf(stderr, "constants: %s compatibility differs\n",
                    signed_long_long_enums[index]);
      return 1;
    }
  }
  return 0;
}

static int run_constants(const char *host_root) {
  static const char source[] =
      "enum SignedOperations {\n"
      "  NEGATIVE_DIVIDE = -7 / 3,\n"
      "  NEGATIVE_REMAINDER = -7 % 3,\n"
      "  NEGATIVE_SHIFT = -8 >> 2,\n"
      "  POSITIVE_NEGATIVE_DIVIDE = 7 / -3,\n"
      "  POSITIVE_NEGATIVE_REMAINDER = 7 % -3,\n"
      "  UNSIGNED_WRAP = 0xffffffffu + 1u,\n"
      "  UNSIGNED_SHIFT = 0xffffffffu >> 31,\n"
      "  LOGICAL_NOT = !1,\n"
      "  LOGICAL_AND = 2 && -3,\n"
      "  LOGICAL_OR = 0 || 4,\n"
      "  LOGICAL_PRECEDENCE = 0 || 1 && 1,\n"
      "  LOGICAL_BITWISE_PRECEDENCE = !(1 | 0 && 0),\n"
      "  LOGICAL_SHORT_AND = 0 && (1 / 0),\n"
      "  LOGICAL_SHORT_OR = 1 || (1 / 0)\n"
      "};\n"
      "enum MixedFirst { MIXED_FIRST_NEGATIVE = -1,\n"
      "                  MIXED_FIRST_LARGE = 0xffffffffu };\n"
      "enum MixedLast { MIXED_LAST_LARGE = 0xffffffffu,\n"
      "                 MIXED_LAST_NEGATIVE = -1 };\n"
      "enum DeclaredExpression { DECLARED_A = 1u,\n"
      "                          DECLARED_B = DECLARED_A - 2 };\n";
  static const frontend_failure_case_t invalid_cases[] = {
      {"duplicate unsigned suffix", "enum E { X = 1uu };\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION},
      {"split long suffix", "enum E { X = 1lul };\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION},
      {"three-long suffix", "enum E { X = 1lll };\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION},
      {"empty hexadecimal literal", "enum E { X = 0x };\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION},
      {"unsigned literal overflow",
       "enum E { X = 18446744073709551616ull };\n", CTOOL_ERR_OVERFLOW,
       CTOOL_C_PARSE_DIAG_OVERFLOW},
      {"signed-suffix literal overflow",
       "enum E { X = 9223372036854775808ll };\n", CTOOL_ERR_OVERFLOW,
       CTOOL_C_PARSE_DIAG_OVERFLOW},
      {"signed addition overflow", "enum E { X = 2147483647 + 1 };\n",
       CTOOL_ERR_OVERFLOW, CTOOL_C_PARSE_DIAG_OVERFLOW},
      {"signed multiplication overflow", "enum E { X = 50000 * 50000 };\n",
       CTOOL_ERR_OVERFLOW, CTOOL_C_PARSE_DIAG_OVERFLOW},
      {"signed division overflow",
       "enum E { X = (-2147483647 - 1) / -1 };\n", CTOOL_ERR_OVERFLOW,
       CTOOL_C_PARSE_DIAG_OVERFLOW},
      {"signed remainder overflow",
       "enum E { X = (-2147483647 - 1) % -1 };\n", CTOOL_ERR_OVERFLOW,
       CTOOL_C_PARSE_DIAG_OVERFLOW},
      {"signed left-shift overflow", "enum E { X = 1 << 31 };\n",
       CTOOL_ERR_OVERFLOW, CTOOL_C_PARSE_DIAG_OVERFLOW},
      {"negative left-shift operand", "enum E { X = -1 << 1 };\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION},
      {"shift count reaches width", "enum E { X = 1 << 32 };\n",
       CTOOL_ERR_INPUT, CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION},
      {"division by zero", "enum E { X = 1 / 0 };\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION},
      {"remainder by zero", "enum E { X = 1 % 0 };\n", CTOOL_ERR_INPUT,
       CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION},
      {"enumerator increment overflow",
       "enum E { X = 0x7fffffffffffffffll, Y };\n", CTOOL_ERR_OVERFLOW,
       CTOOL_C_PARSE_DIAG_OVERFLOW}};
  frontend_fixture_t fixture;
  ctool_c_translation_unit_t unit;
  ctool_u32 index;
  int failed = 0;

  if (begin_frontend_fixture(&fixture, "constants", host_root,
                             64u * 1024u * 1024u) != 0) {
    return 1;
  }
  if (parse_valid_fixture(&fixture, "/constant-values.c", source, &unit) != 0 ||
      validate_constant_unit(&unit) != 0) {
    failed = 1;
  }
  for (index = 0u; index < ARRAY_COUNT(invalid_cases); index++) {
    if (expect_frontend_failure(&fixture, &invalid_cases[index],
                                "/constant-invalid.c") != 0) {
      failed = 1;
    }
  }
  if (finish_frontend_fixture(&fixture) != 0) {
    failed = 1;
  }
  if (failed == 0) {
    (void)printf("constants: ok\n");
  }
  return failed;
}

static char *build_depth_source(const char *kind, ctool_u32 depth) {
  const size_t capacity = 256u * 1024u;
  char *text = (char *)malloc(capacity);
  size_t used = 0u;
  ctool_u32 index;

  if (text == NULL) {
    return NULL;
  }
  text[0] = '\0';
  if (strcmp(kind, "declarator") == 0) {
    if (append_scale_text(text, capacity, &used, "int ") != 0) {
      free(text);
      return NULL;
    }
    for (index = 0u; index < depth; index++) {
      if (append_scale_text(text, capacity, &used, "(") != 0) {
        free(text);
        return NULL;
      }
    }
    if (append_scale_text(text, capacity, &used, "deep_identifier") != 0) {
      free(text);
      return NULL;
    }
    for (index = 0u; index < depth; index++) {
      if (append_scale_text(text, capacity, &used, ")") != 0) {
        free(text);
        return NULL;
      }
    }
    if (append_scale_text(text, capacity, &used, ";\n") != 0) {
      free(text);
      return NULL;
    }
  } else if (strcmp(kind, "constant") == 0) {
    if (append_scale_text(text, capacity, &used,
                          "enum Deep { DEEP_VALUE = ") != 0) {
      free(text);
      return NULL;
    }
    for (index = 0u; index < depth; index++) {
      if (append_scale_text(text, capacity, &used, "(") != 0) {
        free(text);
        return NULL;
      }
    }
    if (append_scale_text(text, capacity, &used, "1") != 0) {
      free(text);
      return NULL;
    }
    for (index = 0u; index < depth; index++) {
      if (append_scale_text(text, capacity, &used, ")") != 0) {
        free(text);
        return NULL;
      }
    }
    if (append_scale_text(text, capacity, &used, " };\n") != 0) {
      free(text);
      return NULL;
    }
  } else if (strcmp(kind, "record") == 0) {
    if (append_scale_text(text, capacity, &used, "struct Root {\n") != 0) {
      free(text);
      return NULL;
    }
    for (index = 0u; index < depth; index++) {
      if (append_scale_text(text, capacity, &used, "struct {\n") != 0) {
        free(text);
        return NULL;
      }
    }
    if (append_scale_text(text, capacity, &used, "int deep_member;\n") != 0) {
      free(text);
      return NULL;
    }
    for (index = 0u; index < depth; index++) {
      if (append_scale_text(text, capacity, &used, "};\n") != 0) {
        free(text);
        return NULL;
      }
    }
    if (append_scale_text(text, capacity, &used, "};\n") != 0) {
      free(text);
      return NULL;
    }
  } else if (strcmp(kind, "static-assert") == 0) {
    if (append_scale_text(text, capacity, &used, "struct Root {\n") != 0) {
      free(text);
      return NULL;
    }
    for (index = 0u; index < depth; index++) {
      if (append_scale_text(text, capacity, &used, "struct {\n") != 0) {
        free(text);
        return NULL;
      }
    }
    if (append_scale_text(
            text, capacity, &used,
            "_Static_assert(1, \"nesting boundary\");\n") != 0) {
      free(text);
      return NULL;
    }
    for (index = 0u; index < depth; index++) {
      if (append_scale_text(text, capacity, &used, "};\n") != 0) {
        free(text);
        return NULL;
      }
    }
    if (append_scale_text(text, capacity, &used, "};\n") != 0) {
      free(text);
      return NULL;
    }
  } else if (strcmp(kind, "body-call") == 0) {
    if (append_scale_text(
            text, capacity, &used,
            "int id(int value);\nvoid bad(int value) { ") != 0) {
      free(text);
      return NULL;
    }
    for (index = 0u; index < depth; index++) {
      if (append_scale_text(text, capacity, &used, "id(") != 0) {
        free(text);
        return NULL;
      }
    }
    if (append_scale_text(text, capacity, &used, "value") != 0) {
      free(text);
      return NULL;
    }
    for (index = 0u; index < depth; index++) {
      if (append_scale_text(text, capacity, &used, ")") != 0) {
        free(text);
        return NULL;
      }
    }
    if (append_scale_text(text, capacity, &used, "; }\n") != 0) {
      free(text);
      return NULL;
    }
  } else {
    free(text);
    return NULL;
  }
  return text;
}

static int run_depth(const char *host_root, const char *kind) {
  frontend_fixture_t fixture;
  frontend_failure_case_t test_case;
  char mode[64];
  char *source;
  int written;
  int failed = 0;

  written = snprintf(mode, sizeof(mode), "depth-%s", kind);
  if (written <= 0 || (size_t)written >= sizeof(mode)) {
    return 1;
  }
  source = build_depth_source(kind, 3000u);
  if (source == NULL) {
    (void)fprintf(stderr, "%s: source construction failed\n", mode);
    return 1;
  }
  if (begin_frontend_fixture(&fixture, mode, host_root,
                             128u * 1024u * 1024u) != 0) {
    free(source);
    return 1;
  }
  test_case.name = "3,000-level nesting";
  test_case.source = source;
  test_case.status = CTOOL_ERR_LIMIT;
  test_case.diagnostic_code = CTOOL_C_PARSE_DIAG_LIMIT;
  if (expect_frontend_failure(&fixture, &test_case, "/depth.c") != 0) {
    failed = 1;
  }
  free(source);
  if (finish_frontend_fixture(&fixture) != 0) {
    failed = 1;
  }
  if (failed == 0) {
    (void)printf("%s: ok\n", mode);
  }
  return failed;
}

static int run_errors(const char *host_root) {
  static const char source_text[] = "typedef unsigned int good_t;\n";
  ctool_c_pp_request_t pp_request;
  ctool_c_parse_request_t parse_request;
  ctool_c_pp_result_t tape;
  ctool_c_pp_result_t invalid_tape;
  ctool_c_translation_unit_t invalid_unit;
  ctool_c_translation_unit_t recovered_unit;
  ctool_c_pp_token_t *invalid_tokens = NULL;
  ctool_c_pp_token_t *invalid_snapshot = NULL;
  ctool_c_pp_token_t *valid_snapshot = NULL;
  ctool_host_adapter_t adapter;
  ctool_job_t *job = NULL;
  ctool_source_t source;
  ctool_status_t status;
  ctool_arena_mark_t mark;
  const ctool_diagnostic_t *diagnostic;
  size_t token_bytes;
  int failed = 1;

  if (open_job("errors", host_root, 8u * 1024u * 1024u, &adapter, &job) != 0) {
    return 1;
  }
  source.path.text = ctool_string("/invalid.c");
  source.contents =
      ctool_bytes(source_text, (ctool_u32)(sizeof(source_text) - 1u));
  (void)memset(&pp_request, 0, sizeof(pp_request));
  pp_request.mode = CTOOL_C_PP_MODE_C11;
  pp_request.gnu_extensions = CTOOL_TRUE;
  (void)memset(&tape, 0xa5, sizeof(tape));
  status = ctool_c_preprocess(job, &source, &pp_request, &tape);
  if (status != CTOOL_OK || tape.tokens == NULL || tape.token_count != 5u ||
      ctool_job_diagnostic_count(job) != 0u) {
    (void)fprintf(stderr, "errors: preprocessing fixture failed\n");
    goto cleanup;
  }
  token_bytes = (size_t)tape.token_count * sizeof(*invalid_tokens);
  invalid_tokens = (ctool_c_pp_token_t *)malloc(token_bytes);
  invalid_snapshot = (ctool_c_pp_token_t *)malloc(token_bytes);
  valid_snapshot = (ctool_c_pp_token_t *)malloc(token_bytes);
  if (invalid_tokens == NULL || invalid_snapshot == NULL ||
      valid_snapshot == NULL) {
    (void)fprintf(stderr, "errors: token snapshot allocation failed\n");
    goto cleanup;
  }
  (void)memcpy(valid_snapshot, tape.tokens, token_bytes);
  (void)memcpy(invalid_tokens, tape.tokens, token_bytes);
  invalid_tokens[4].spelling = ctool_string(")");
  (void)memcpy(invalid_snapshot, invalid_tokens, token_bytes);
  invalid_tape.tokens = invalid_tokens;
  invalid_tape.token_count = tape.token_count;
  parse_request.mode = CTOOL_C_PP_MODE_C11;
  parse_request.gnu_extensions = CTOOL_TRUE;

  mark = ctool_arena_mark(ctool_job_arena(job));
  (void)memset(&invalid_unit, 0xa5, sizeof(invalid_unit));
  status = ctool_c_parse(job, &invalid_tape, &parse_request, &invalid_unit);
  diagnostic = ctool_job_diagnostic(job, 0u);
  if (status != CTOOL_ERR_INPUT || unit_is_zero(&invalid_unit) == 0 ||
      diagnostic == NULL ||
      diagnostic->code != CTOOL_C_PARSE_DIAG_EXPECTED_TOKEN ||
      !string_equal(diagnostic->path, "/invalid.c") || diagnostic->line != 1u ||
      diagnostic->column != 28u ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      memcmp(invalid_snapshot, invalid_tokens, token_bytes) != 0 ||
      memcmp(valid_snapshot, tape.tokens, token_bytes) != 0) {
    (void)fprintf(stderr, "errors: invalid declaration was not transactional\n");
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }

  (void)memset(&recovered_unit, 0xa5, sizeof(recovered_unit));
  status = ctool_c_parse(job, &tape, &parse_request, &recovered_unit);
  if (status != CTOOL_OK || recovered_unit.binding_count != 1u ||
      recovered_unit.bindings == NULL ||
      recovered_unit.bindings[0].kind != CTOOL_C_BINDING_TYPEDEF ||
      !string_equal(recovered_unit.bindings[0].name, "good_t") ||
      recovered_unit.bindings[0].type >= recovered_unit.graph.type_count ||
      recovered_unit.layout.type_count != recovered_unit.graph.type_count ||
      ctool_job_diagnostic_count(job) != 1u ||
      memcmp(valid_snapshot, tape.tokens, token_bytes) != 0) {
    (void)fprintf(stderr, "errors: same-job recovery failed: %s\n",
                  ctool_status_name(status));
    goto cleanup;
  }
  failed = 0;
cleanup:
  free(valid_snapshot);
  free(invalid_snapshot);
  free(invalid_tokens);
  ctool_job_close(job);
  if (failed == 0) {
    (void)printf("errors: ok\n");
  }
  return failed;
}

static int append_scale_line(char *text, size_t capacity, size_t *used,
                             const char *format, unsigned int index) {
  int written;
  size_t remaining;
  if (*used > capacity) {
    return 1;
  }
  remaining = capacity - *used;
  written = snprintf(text + *used, remaining, format, index, index);
  if (written < 0 || (size_t)written >= remaining) {
    return 1;
  }
  *used += (size_t)written;
  return 0;
}

static int append_scale_text(char *text, size_t capacity, size_t *used,
                             const char *suffix) {
  size_t suffix_size = strlen(suffix);
  if (*used > capacity || suffix_size >= capacity - *used) {
    return 1;
  }
  (void)memcpy(text + *used, suffix, suffix_size);
  *used += suffix_size;
  text[*used] = '\0';
  return 0;
}

static int run_scale(const char *host_root) {
  const size_t source_capacity = 65536u;
  char *text = NULL;
  size_t used = 0u;
  ctool_c_pp_request_t pp_request;
  ctool_c_parse_request_t parse_request;
  ctool_c_pp_result_t tape;
  ctool_c_translation_unit_t unit;
  ctool_host_adapter_t adapter;
  ctool_job_t *job = NULL;
  ctool_source_t source;
  ctool_status_t status;
  const ctool_c_binding_t *big_binding;
  const ctool_c_type_node_t *big_record;
  const ctool_c_type_layout_t *big_layout;
  ctool_u32 index;
  int failed = 1;

  text = (char *)malloc(source_capacity);
  if (text == NULL) {
    (void)fprintf(stderr, "scale: source allocation failed\n");
    return 1;
  }
  text[0] = '\0';
  for (index = 0u; index < 256u; index++) {
    if (append_scale_line(text, source_capacity, &used,
                          "typedef unsigned int type_%03u;\n",
                          (unsigned int)index) != 0) {
      (void)fprintf(stderr, "scale: typedef source overflow\n");
      goto cleanup;
    }
  }
  if (append_scale_text(text, source_capacity, &used, "typedef struct {\n") !=
      0) {
    (void)fprintf(stderr, "scale: record source overflow\n");
    goto cleanup;
  }
  for (index = 0u; index < 256u; index++) {
    if (append_scale_line(text, source_capacity, &used,
                          "  type_%03u field_%03u;\n",
                          (unsigned int)index) != 0) {
      (void)fprintf(stderr, "scale: member source overflow\n");
      goto cleanup;
    }
  }
  if (append_scale_text(text, source_capacity, &used, "} big_t;\n") != 0 ||
      used > 0xffffffffu) {
    (void)fprintf(stderr, "scale: final source overflow\n");
    goto cleanup;
  }
  if (open_job("scale", host_root, 64u * 1024u * 1024u, &adapter, &job) != 0) {
    goto cleanup;
  }
  source.path.text = ctool_string("/scale.c");
  source.contents = ctool_bytes(text, (ctool_u32)used);
  (void)memset(&pp_request, 0, sizeof(pp_request));
  pp_request.mode = CTOOL_C_PP_MODE_C11;
  pp_request.gnu_extensions = CTOOL_TRUE;
  (void)memset(&tape, 0xa5, sizeof(tape));
  status = ctool_c_preprocess(job, &source, &pp_request, &tape);
  if (status != CTOOL_OK || tape.token_count != 2054u ||
      ctool_job_diagnostic_count(job) != 0u) {
    (void)fprintf(stderr, "scale: preprocessing failed: %s (%u tokens)\n",
                  ctool_status_name(status), tape.token_count);
    goto cleanup;
  }
  parse_request.mode = CTOOL_C_PP_MODE_C11;
  parse_request.gnu_extensions = CTOOL_TRUE;
  (void)memset(&unit, 0xa5, sizeof(unit));
  status = ctool_c_parse(job, &tape, &parse_request, &unit);
  if (status != CTOOL_OK || unit.binding_count != 257u ||
      unit.graph.member_count != 256u || unit.graph.parameter_type_count != 0u ||
      unit.parameter_count != 0u || unit.tag_count != 0u ||
      ctool_job_diagnostic_count(job) != 0u) {
    (void)fprintf(stderr, "scale: parse failed: %s\n",
                  ctool_status_name(status));
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  for (index = 0u; index < 256u; index++) {
    char expected[32];
    int written = snprintf(expected, sizeof(expected), "type_%03u",
                           (unsigned int)index);
    if (written <= 0 || unit.bindings[index].kind != CTOOL_C_BINDING_TYPEDEF ||
        !string_equal(unit.bindings[index].name, expected)) {
      (void)fprintf(stderr, "scale: typedef binding %u differs\n", index);
      goto cleanup;
    }
  }
  big_binding = find_binding(&unit, "big_t");
  big_record = big_binding == NULL ? NULL : type_node(&unit, big_binding->type);
  big_layout = big_binding == NULL ? NULL : type_layout(&unit, big_binding->type);
  if (big_binding == NULL || big_binding->kind != CTOOL_C_BINDING_TYPEDEF ||
      big_record == NULL || big_record->kind != CTOOL_C_TYPE_RECORD ||
      big_record->member_count != 256u || big_layout == NULL ||
      big_layout->size != 1024u || big_layout->alignment != 4u) {
    (void)fprintf(stderr, "scale: large record layout differs\n");
    goto cleanup;
  }
  for (index = 0u; index < 256u; index++) {
    char expected[32];
    ctool_u32 flat_index = big_record->first_member + index;
    int written = snprintf(expected, sizeof(expected), "field_%03u",
                           (unsigned int)index);
    if (written <= 0 || flat_index >= unit.graph.member_count ||
        !string_equal(unit.graph.members[flat_index].name, expected) ||
        unit.graph.members[flat_index].pack_alignment != 0u ||
        unit.layout.members[flat_index].byte_offset != index * 4u) {
      (void)fprintf(stderr, "scale: record member %u differs\n", index);
      goto cleanup;
    }
  }
  failed = 0;
cleanup:
  if (job != NULL) {
    ctool_job_close(job);
  }
  free(text);
  if (failed == 0) {
    (void)printf("scale: ok\n");
  }
  return failed;
}

int main(int argc, char **argv) {
  if (argc >= 4 && strcmp(argv[1], "header-sweep") == 0) {
    return run_header_sweep(argv[2], argc - 3, &argv[3]);
  }
  if (argc != 3) {
    (void)fprintf(stderr,
                  "usage: cupidc-frontend-contract "
                   "fat16|redeclarations|attributes|static-asserts|"
                   "function-bodies|block-bindings|scalar-initializers|"
                   "scalar-returns|for-statements|"
                   "pointer-expressions|pointer-arithmetic|scalar-updates|"
                   "function-specifiers|errors|scale|semantics|constants|"
                  "boundaries|"
                  "depth-declarator|depth-constant|depth-record "
                  "<repository-root>\n"
                  "       cupidc-frontend-contract header-sweep "
                  "<repository-root> <logical-header>...\n");
    return 2;
  }
  if (strcmp(argv[1], "fat16") == 0) {
    return run_fat16(argv[2]);
  }
  if (strcmp(argv[1], "redeclarations") == 0) {
    return run_redeclarations(argv[2]);
  }
  if (strcmp(argv[1], "attributes") == 0) {
    return run_attributes(argv[2]);
  }
  if (strcmp(argv[1], "static-asserts") == 0) {
    return run_static_asserts(argv[2]);
  }
  if (strcmp(argv[1], "function-bodies") == 0) {
    return run_function_bodies(argv[2]);
  }
  if (strcmp(argv[1], "block-bindings") == 0) {
    return run_block_bindings(argv[2]);
  }
  if (strcmp(argv[1], "scalar-initializers") == 0) {
    return run_scalar_initializers(argv[2]);
  }
  if (strcmp(argv[1], "scalar-returns") == 0) {
    return run_scalar_returns(argv[2]);
  }
  if (strcmp(argv[1], "for-statements") == 0) {
    return run_for_statements(argv[2]);
  }
  if (strcmp(argv[1], "pointer-expressions") == 0) {
    return run_pointer_expressions(argv[2]);
  }
  if (strcmp(argv[1], "pointer-arithmetic") == 0) {
    return run_pointer_arithmetic(argv[2]);
  }
  if (strcmp(argv[1], "scalar-updates") == 0) {
    return run_scalar_updates(argv[2]);
  }
  if (strcmp(argv[1], "function-specifiers") == 0) {
    return run_function_specifiers(argv[2]);
  }
  if (strcmp(argv[1], "errors") == 0) {
    return run_errors(argv[2]);
  }
  if (strcmp(argv[1], "scale") == 0) {
    return run_scale(argv[2]);
  }
  if (strcmp(argv[1], "semantics") == 0) {
    return run_semantics(argv[2]);
  }
  if (strcmp(argv[1], "constants") == 0) {
    return run_constants(argv[2]);
  }
  if (strcmp(argv[1], "boundaries") == 0) {
    return run_boundaries(argv[2]);
  }
  if (strcmp(argv[1], "depth-declarator") == 0) {
    return run_depth(argv[2], "declarator");
  }
  if (strcmp(argv[1], "depth-constant") == 0) {
    return run_depth(argv[2], "constant");
  }
  if (strcmp(argv[1], "depth-record") == 0) {
    return run_depth(argv[2], "record");
  }
  (void)fprintf(stderr, "unknown mode: %s\n", argv[1]);
  return 2;
}
