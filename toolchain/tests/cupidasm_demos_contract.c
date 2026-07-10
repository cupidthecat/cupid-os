#include "ctool.h"
#include "ctool_host.h"
#include "cupidasm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_COUNT(values)                                                \
  ((ctool_u32)(sizeof(values) / sizeof((values)[0])))
#define ABSOLUTE(name_value)                                               \
  { name_value, CTOOL_ASM_DEFINE_ABSOLUTE, 0u }
#define CONSTANT(name_value, constant_value)                               \
  { name_value, CTOOL_ASM_DEFINE_CONSTANT, constant_value }

#define DEMO_COUNT 22u
#define MAX_DEFINITIONS 16u
#define FIXED_CODE_BASE 0x01a00000u
#define FIXED_DATA_BASE 0x01b00000u
#define FIXED_REGION_BYTES 0x00100000u

typedef struct {
  const char *name;
  ctool_asm_definition_kind_t kind;
  ctool_u32 value;
} binding_spec_t;

typedef struct {
  const char *path;
  const binding_spec_t *bindings;
  ctool_u32 binding_count;
} demo_spec_t;

typedef struct {
  ctool_u8 *bytes;
  ctool_u32 byte_count;
  ctool_u32 code_file_size;
  ctool_u32 code_memory_size;
  ctool_u32 data_file_size;
  ctool_u32 data_memory_size;
  ctool_u32 entry_address;
} artifact_snapshot_t;

static const binding_spec_t print_and_int[] = {
    ABSOLUTE("print"), ABSOLUTE("print_int")};
static const binding_spec_t print_only[] = {ABSOLUTE("print")};
static const binding_spec_t data_bindings[] = {
    ABSOLUTE("print"), ABSOLUTE("print_int"), ABSOLUTE("strlen")};
static const binding_spec_t fs_bindings[] = {
    ABSOLUTE("getpid"),    ABSOLUTE("print"),      ABSOLUTE("print_int"),
    ABSOLUTE("strcmp"),    ABSOLUTE("strlen"),     ABSOLUTE("vfs_close"),
    ABSOLUTE("vfs_open"),  ABSOLUTE("vfs_read"),   ABSOLUTE("vfs_unlink"),
    ABSOLUTE("vfs_write"), CONSTANT("O_CREAT", 0x0100u),
    CONSTANT("O_RDONLY", 0u), CONSTANT("O_TRUNC", 0x0200u),
    CONSTANT("O_WRONLY", 1u)};
static const binding_spec_t math_bindings[] = {
    ABSOLUTE("print"), ABSOLUTE("print_hex"), ABSOLUTE("print_int")};
static const binding_spec_t parity_core_bindings[] = {
    ABSOLUTE("__cc_PrintLine"),       ABSOLUTE("date_short_string"),
    ABSOLUTE("get_cpu_mhz"),          ABSOLUTE("get_cwd"),
    ABSOLUTE("memstats"),             ABSOLUTE("mount_count"),
    ABSOLUTE("print"),                ABSOLUTE("print_int"),
    ABSOLUTE("process_count"),        ABSOLUTE("rtc_epoch"),
    ABSOLUTE("time_string"),          ABSOLUTE("timer_get_frequency")};
static const binding_spec_t parity_diag_bindings[] = {
    ABSOLUTE("__cc_PrintLine"),       ABSOLUTE("detect_memory_leaks"),
    ABSOLUTE("dump_registers"),       ABSOLUTE("dump_stack_trace"),
    ABSOLUTE("get_log_level_name"),   ABSOLUTE("heap_check_integrity"),
    ABSOLUTE("print"),                ABSOLUTE("print_hex_byte"),
    ABSOLUTE("print_log_buffer")};
static const binding_spec_t parity_gfx2d_bindings[] = {
    ABSOLUTE("gfx2d_circle_fill"),
    ABSOLUTE("gfx2d_clear"),
    ABSOLUTE("gfx2d_getpixel"),
    ABSOLUTE("gfx2d_init"),
    ABSOLUTE("gfx2d_rect_fill"),
    ABSOLUTE("gfx2d_rect_round_fill"),
    ABSOLUTE("gfx2d_surface_alloc"),
    ABSOLUTE("gfx2d_surface_free"),
    ABSOLUTE("gfx2d_surface_set_active"),
    ABSOLUTE("gfx2d_surface_unset_active"),
    ABSOLUTE("gfx2d_text"),
    ABSOLUTE("is_gui_mode"),
    ABSOLUTE("print")};
static const binding_spec_t syscall_table_bindings[] = {
    ABSOLUTE("getpid"), ABSOLUTE("print"), ABSOLUTE("print_int"),
    ABSOLUTE("syscall_get_table"), CONSTANT("SYS_TABLE_SIZE", 4u),
    CONSTANT("SYS_VERSION", 0u)};
static const binding_spec_t syscall_vfs_bindings[] = {
    ABSOLUTE("print"), ABSOLUTE("syscall_get_table"),
    CONSTANT("SYS_VFS_COPY_FILE", 96u), CONSTANT("SYS_VFS_UNLINK", 88u),
    CONSTANT("SYS_VFS_WRITE_TEXT", 112u)};

static const demo_spec_t demos[] = {
    {"demos/asm_compat_reserve.asm", print_and_int,
     ARRAY_COUNT(print_and_int)},
    {"demos/bubblesort.asm", print_and_int, ARRAY_COUNT(print_and_int)},
    {"demos/data.asm", data_bindings, ARRAY_COUNT(data_bindings)},
    {"demos/factorial.asm", print_and_int, ARRAY_COUNT(print_and_int)},
    {"demos/fibonacci.asm", print_and_int, ARRAY_COUNT(print_and_int)},
    {"demos/fpu_kernel.asm", print_only, ARRAY_COUNT(print_only)},
    {"demos/fs_syscalls.asm", fs_bindings, ARRAY_COUNT(fs_bindings)},
    {"demos/hello.asm", print_only, ARRAY_COUNT(print_only)},
    {"demos/include_feature.asm", print_only, ARRAY_COUNT(print_only)},
    {"demos/include_helper.asm", print_only, ARRAY_COUNT(print_only)},
    {"demos/jcc_aliases.asm", print_only, ARRAY_COUNT(print_only)},
    {"demos/loop.asm", print_and_int, ARRAY_COUNT(print_and_int)},
    {"demos/math.asm", math_bindings, ARRAY_COUNT(math_bindings)},
    {"demos/parity_core.asm", parity_core_bindings,
     ARRAY_COUNT(parity_core_bindings)},
    {"demos/parity_diag.asm", parity_diag_bindings,
     ARRAY_COUNT(parity_diag_bindings)},
    {"demos/parity_gfx2d.asm", parity_gfx2d_bindings,
     ARRAY_COUNT(parity_gfx2d_bindings)},
    {"demos/parity_priv.asm", print_only, ARRAY_COUNT(print_only)},
    {"demos/reserve_directives.asm", print_and_int,
     ARRAY_COUNT(print_and_int)},
    {"demos/simd_blur.asm", print_only, ARRAY_COUNT(print_only)},
    {"demos/stack.asm", print_and_int, ARRAY_COUNT(print_and_int)},
    {"demos/syscall_table_demo.asm", syscall_table_bindings,
     ARRAY_COUNT(syscall_table_bindings)},
    {"demos/syscall_vfs_extended_demo.asm", syscall_vfs_bindings,
     ARRAY_COUNT(syscall_vfs_bindings)}};

static ctool_bool string_is(ctool_string_t actual, const char *expected) {
  size_t expected_size = strlen(expected);
  return (size_t)actual.size == expected_size &&
                 memcmp(actual.data, expected, expected_size) == 0
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static const ctool_asm_region_t *find_region(
    const ctool_asm_result_t *result, const char *name) {
  ctool_u32 index;
  for (index = 0u; index < result->region_count; index++) {
    if (string_is(result->regions[index].name, name) != CTOOL_FALSE) {
      return &result->regions[index];
    }
  }
  return (const ctool_asm_region_t *)0;
}

static ctool_u32 fake_absolute_address(const char *name) {
  ctool_u32 hash = 2166136261u;
  const unsigned char *cursor = (const unsigned char *)name;
  while (*cursor != 0u) {
    hash ^= (ctool_u32)*cursor;
    hash *= 16777619u;
    cursor++;
  }
  return 0x00100000u + ((hash & 0x0000ffffu) << 4u);
}

static int validate_demo_table(void) {
  ctool_u32 outer;
  ctool_u32 inner;
  if (ARRAY_COUNT(demos) != DEMO_COUNT) {
    (void)fprintf(stderr, "demo table has %u entries, expected %u\n",
                  (unsigned int)ARRAY_COUNT(demos),
                  (unsigned int)DEMO_COUNT);
    return 0;
  }
  for (outer = 0u; outer < ARRAY_COUNT(demos); outer++) {
    if (strncmp(demos[outer].path, "demos/", 6u) != 0 ||
        demos[outer].binding_count > MAX_DEFINITIONS) {
      (void)fprintf(stderr, "invalid demo contract entry: %s\n",
                    demos[outer].path);
      return 0;
    }
    for (inner = outer + 1u; inner < ARRAY_COUNT(demos); inner++) {
      if (strcmp(demos[outer].path, demos[inner].path) == 0) {
        (void)fprintf(stderr, "duplicate demo contract entry: %s\n",
                      demos[outer].path);
        return 0;
      }
    }
  }
  return 1;
}

static int copy_snapshot(const ctool_asm_result_t *result,
                         ctool_bytes_t bytes,
                         const ctool_asm_region_t *code,
                         const ctool_asm_region_t *data,
                         artifact_snapshot_t *snapshot) {
  snapshot->bytes = (ctool_u8 *)malloc((size_t)bytes.size);
  if (snapshot->bytes == (ctool_u8 *)0) {
    (void)fprintf(stderr, "could not retain demo artifact\n");
    return 0;
  }
  (void)memcpy(snapshot->bytes, bytes.data, (size_t)bytes.size);
  snapshot->byte_count = bytes.size;
  snapshot->code_file_size = code->file_size;
  snapshot->code_memory_size = code->memory_size;
  snapshot->data_file_size = data->file_size;
  snapshot->data_memory_size = data->memory_size;
  snapshot->entry_address = result->entry_address;
  return 1;
}

static int assemble_demo(const demo_spec_t *demo,
                         artifact_snapshot_t *snapshot) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_buffer_t *output;
  ctool_path_t root;
  ctool_path_t source_path;
  ctool_source_t source;
  ctool_asm_definition_t definitions[MAX_DEFINITIONS];
  ctool_string_t entry_candidates[2];
  ctool_asm_request_t request;
  ctool_asm_result_t result;
  const ctool_asm_region_t *code;
  const ctool_asm_region_t *data;
  ctool_bytes_t bytes;
  ctool_status_t status;
  ctool_u32 index;
  int valid = 0;

  (void)memset(snapshot, 0, sizeof(*snapshot));
  status = ctool_host_adapter_init(&adapter, "..");
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "%s: host adapter init: %s\n", demo->path,
                  ctool_status_name(status));
    return 0;
  }
  config = ctool_host_job_config(&adapter, ctool_default_limits());
  status = ctool_job_open(&config, &job);
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "%s: job open: %s\n", demo->path,
                  ctool_status_name(status));
    return 0;
  }
  output = (ctool_buffer_t *)0;
  status = ctool_path_root(ctool_job_arena(job), &root);
  if (status == CTOOL_OK) {
    status = ctool_path_resolve(ctool_job_arena(job), &root,
                                ctool_string(demo->path),
                                config.limits.path_bytes, &source_path);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_load_source(job, &source_path, &source);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 4096u, config.limits.output_bytes,
                                   &output);
  }
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "%s: source setup: %s\n", demo->path,
                  ctool_status_name(status));
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 0;
  }

  for (index = 0u; index < demo->binding_count; index++) {
    definitions[index].name = ctool_string(demo->bindings[index].name);
    definitions[index].kind = demo->bindings[index].kind;
    definitions[index].value =
        demo->bindings[index].kind == CTOOL_ASM_DEFINE_ABSOLUTE
            ? fake_absolute_address(demo->bindings[index].name)
            : demo->bindings[index].value;
  }
  entry_candidates[0] = ctool_string("main");
  entry_candidates[1] = ctool_string("_start");
  (void)memset(&request, 0, sizeof(request));
  request.artifact = CTOOL_ASM_ARTIFACT_FIXED_IMAGE;
  request.initial_mode = CTOOL_X86_MODE_32;
  request.definitions = definitions;
  request.definition_count = demo->binding_count;
  request.include_roots = &root;
  request.include_root_count = 1u;
  request.entry_candidates = entry_candidates;
  request.entry_candidate_count = ARRAY_COUNT(entry_candidates);
  request.allow_implicit_externs = CTOOL_FALSE;
  request.as.fixed.code.base_address = FIXED_CODE_BASE;
  request.as.fixed.code.maximum_bytes = FIXED_REGION_BYTES;
  request.as.fixed.data.base_address = FIXED_DATA_BASE;
  request.as.fixed.data.maximum_bytes = FIXED_REGION_BYTES;
  (void)memset(&result, 0xa5, sizeof(result));

  status = ctool_asm_assemble(job, &source, &request, output, &result);
  bytes = ctool_buffer_view(output);
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "%s: assembly failed: %s\n", demo->path,
                  ctool_status_name(status));
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 0;
  }
  code = find_region(&result, ".text");
  data = find_region(&result, ".data");
  if (result.artifact != CTOOL_ASM_ARTIFACT_FIXED_IMAGE ||
      result.bytes.data != bytes.data || result.bytes.size != bytes.size ||
      bytes.size == 0u || result.region_count != 2u ||
      code == (const ctool_asm_region_t *)0 ||
      data == (const ctool_asm_region_t *)0 ||
      code->address != FIXED_CODE_BASE || code->output_offset != 0u ||
      code->file_size == 0u || code->memory_size < code->file_size ||
      code->memory_size > FIXED_REGION_BYTES ||
      code->flags != (CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR) ||
      data->address != FIXED_DATA_BASE ||
      data->output_offset != code->file_size || data->file_size == 0u ||
      data->memory_size < data->file_size ||
      data->memory_size > FIXED_REGION_BYTES ||
      data->flags != (CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE) ||
      bytes.size != data->output_offset + data->file_size ||
      result.has_entry == CTOOL_FALSE ||
      result.entry_address < code->address ||
      result.entry_address >= code->address + code->memory_size ||
      ctool_job_diagnostic_count(job) != 0u) {
    (void)fprintf(stderr, "%s: fixed-image invariants differ\n", demo->path);
    (void)ctool_job_render_diagnostics(job);
  } else {
    valid = copy_snapshot(&result, bytes, code, data, snapshot);
  }

  ctool_buffer_close(output);
  ctool_job_close(job);
  return valid;
}

static int snapshots_equal(const artifact_snapshot_t *first,
                           const artifact_snapshot_t *second) {
  return first->byte_count == second->byte_count &&
         first->code_file_size == second->code_file_size &&
         first->code_memory_size == second->code_memory_size &&
         first->data_file_size == second->data_file_size &&
         first->data_memory_size == second->data_memory_size &&
         first->entry_address == second->entry_address &&
         memcmp(first->bytes, second->bytes, (size_t)first->byte_count) == 0;
}

static int run_all_demos(void) {
  ctool_u32 index;
  artifact_snapshot_t first;
  artifact_snapshot_t second;
  if (!validate_demo_table()) {
    return 1;
  }
  for (index = 0u; index < ARRAY_COUNT(demos); index++) {
    if (!assemble_demo(&demos[index], &first)) {
      return 1;
    }
    if (!assemble_demo(&demos[index], &second)) {
      free(first.bytes);
      return 1;
    }
    if (!snapshots_equal(&first, &second)) {
      (void)fprintf(stderr, "%s: repeated assembly is not deterministic\n",
                    demos[index].path);
      free(second.bytes);
      free(first.bytes);
      return 1;
    }
    free(second.bytes);
    free(first.bytes);
    (void)printf("%s: ok\n", demos[index].path);
  }
  (void)printf("demos: %u ok\n", (unsigned int)ARRAY_COUNT(demos));
  return 0;
}

static int list_demos(void) {
  ctool_u32 index;
  if (!validate_demo_table()) {
    return 1;
  }
  for (index = 0u; index < ARRAY_COUNT(demos); index++) {
    (void)puts(demos[index].path);
  }
  return 0;
}

int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "all") == 0) {
    return run_all_demos();
  }
  if (argc == 2 && strcmp(argv[1], "list") == 0) {
    return list_demos();
  }
  (void)fprintf(stderr, "usage: cupidasm-demos-contract all|list\n");
  return 2;
}
