#include "ctool.h"
#include "ctool_host.h"
#include "cupidc_frontend.h"

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
} active_row_t;

static const active_row_t active_rows[] = {
#define CUPIDC_PP_PROFILE(NAME, MODE, GNU_BOOL)                                \
  {ACTIVE_PROFILE, #NAME, NULL, NULL, (ctool_u32)(MODE), (GNU_BOOL)},
#define CUPIDC_PP_INCLUDE_ROOT(NAME, PATH, FORMS)                              \
  {ACTIVE_INCLUDE_ROOT, #NAME, (PATH), NULL, (FORMS), CTOOL_FALSE},
#define CUPIDC_PP_MACRO(NAME, MACRO_NAME, REPLACEMENT)                        \
  {ACTIVE_MACRO, #NAME, (MACRO_NAME), (REPLACEMENT), 0u, CTOOL_FALSE},
#define CUPIDC_PP_FORCED_INCLUDE(NAME, PATH)                                  \
  {ACTIVE_FORCED_INCLUDE, #NAME, (PATH), NULL, 0u, CTOOL_FALSE},
#define CUPIDC_PP_ACTIVE_CASE(NAME, PATH)                                     \
  {ACTIVE_CASE, #NAME, (PATH), NULL, 0u, CTOOL_FALSE},
#define CUPIDC_PP_GENERATED_CASE(NAME, PATH)                                  \
  {ACTIVE_GENERATED_CASE, #NAME, (PATH), NULL, 0u, CTOOL_FALSE},
#define CUPIDC_PP_INCLUDE_ONLY(PATH, OWNER)                                   \
  {ACTIVE_INCLUDE_ONLY, NULL, (PATH), (OWNER), 0u, CTOOL_FALSE},
#define CUPIDC_PP_NON_ROOT(PATH, REASON)                                      \
  {ACTIVE_NON_ROOT, NULL, (PATH), (REASON), 0u, CTOOL_FALSE},
#define CUPIDC_PP_DEFERRED_HOSTED(PATH, REASON)                               \
  {ACTIVE_DEFERRED_HOSTED, NULL, (PATH), (REASON), 0u, CTOOL_FALSE},
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
                 unit->parameter_count == 0u
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

static int expect_frontend_failure_at(
    frontend_fixture_t *fixture, const frontend_failure_case_t *test_case,
    const char *path, ctool_u32 expected_line,
    ctool_u32 expected_column) {
  ctool_c_pp_result_t tape;
  ctool_c_translation_unit_t unit;
  ctool_c_pp_token_t *snapshot;
  ctool_arena_mark_t mark;
  ctool_status_t status;
  ctool_u32 diagnostic_count;
  const ctool_diagnostic_t *diagnostic;
  size_t token_bytes;
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
  if (status != test_case->status || unit_is_zero(&unit) == 0 ||
      ctool_job_diagnostic_count(fixture->job) != diagnostic_count + 1u ||
      diagnostic == NULL || diagnostic->code != test_case->diagnostic_code ||
      !string_equal(diagnostic->path, path) ||
      (expected_line == 0u ? diagnostic->line == 0u
                           : diagnostic->line != expected_line) ||
      (expected_column == 0u ? diagnostic->column == 0u
                             : diagnostic->column != expected_column) ||
      arena_marks_equal(mark,
                        ctool_arena_mark(ctool_job_arena(fixture->job))) == 0 ||
      memcmp(snapshot, tape.tokens, token_bytes) != 0 ||
      validate_anchor(fixture) != 0) {
    (void)fprintf(stderr,
                  "%s: %s expected %s/0x%08x transactionally, got %s",
                  fixture->mode, test_case->name,
                  ctool_status_name(test_case->status),
                  test_case->diagnostic_code, ctool_status_name(status));
    if (diagnostic != NULL) {
      (void)fprintf(stderr, "/0x%08x", diagnostic->code);
    }
    (void)fprintf(stderr, "\n");
    failed = 1;
  }
  free(snapshot);
  return failed;
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
  const ctool_c_type_node_t *function =
      binding == NULL ? NULL : type_node(unit, binding->type);
  const ctool_c_record_member_t *member;
  const ctool_c_parameter_t *parameter;
  ctool_u32 member_index = 0u;

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
  if (unit->binding_count != 1u || unit->tag_count != 1u ||
      unit->graph.member_count != 1u || unit->parameter_count != 1u ||
      binding == NULL || binding->kind != CTOOL_C_BINDING_FUNCTION ||
      !dual_location_matches(&binding->location,
                             &binding->physical_location, "/borrowed.c", 2u) ||
      tag == NULL ||
      !dual_location_matches(&tag->location, &tag->physical_location,
                             "/borrowed.c", 1u) ||
      record == NULL || record->kind != CTOOL_C_TYPE_RECORD || member == NULL ||
      !string_equal(member->name, "owned_member") ||
      !dual_location_matches(&member->location, &member->physical_location,
                             "/borrowed.c", 1u) ||
      member_index >= unit->layout.member_count || parameter == NULL ||
      !string_equal(parameter->name, "owned_parameter") ||
      !dual_location_matches(&parameter->location,
                             &parameter->physical_location, "/borrowed.c",
                             2u)) {
    (void)fprintf(stderr,
                  "boundaries: copied names or dual locations did not survive\n");
    return 1;
  }
  return 0;
}

static int parse_owned_tape(frontend_fixture_t *fixture,
                            ctool_c_translation_unit_t *unit_out) {
  static const char source[] =
      "struct OwnedTag { int owned_member; };\n"
      "int owned_function(struct OwnedTag owned_parameter);\n";
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
      "function body boundary", "int boundary_function(void) { }\n",
      CTOOL_ERR_UNSUPPORTED, CTOOL_C_PARSE_DIAG_UNSUPPORTED};
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
                                 29u) != 0 ||
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
      "  UNSIGNED_SHIFT = 0xffffffffu >> 31\n"
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
                  "fat16|redeclarations|errors|scale|semantics|constants|"
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
