#include "../contract_parse_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  unsigned char *bytes;
  size_t size;
} file_image_t;

static void file_image_release(file_image_t *image) {
  free(image->bytes);
  image->bytes = (unsigned char *)0;
  image->size = 0u;
}

static int read_request_file(error_context_t *context, const char *path, file_image_t *image) {
  FILE *stream;
  long length;
  size_t read_size;
  image->bytes = (unsigned char *)0;
  image->size = 0u;
  stream = fopen(path, "rb");
  if (stream == (FILE *)0) {
    return cupid_contract_set_error(context, "request file is unavailable");
  }
  if (fseek(stream, 0L, SEEK_END) != 0) {
    (void)fclose(stream);
    return cupid_contract_set_error(context, "cannot measure the request file");
  }
  length = ftell(stream);
  if (length < 0L || fseek(stream, 0L, 0) != 0) {
    (void)fclose(stream);
    return cupid_contract_set_error(context, "request file has an unsupported size");
  }
  image->size = (size_t)length;
  image->bytes = (unsigned char *)malloc(image->size == 0u ? 1u : image->size);
  if (image->bytes == (unsigned char *)0) {
    (void)fclose(stream);
    image->size = 0u;
    return cupid_contract_set_error(context, "cannot allocate the request file");
  }
  read_size = fread(image->bytes, 1u, image->size, stream);
  if (read_size != image->size || fclose(stream) != 0) {
    file_image_release(image);
    return cupid_contract_set_error(context, "cannot read the request file");
  }
  return 1;
}

static const unsigned char manifest_request_magic[8] = {
    'C', 'U', 'P', 'M', 'A', 'N', '2', 0};
static const unsigned char manifest_author_request_magic[8] = {
    'C', 'U', 'P', 'M', 'A', 'N', '4', 0};
static const char manifest_schema[] = "cupid.toolchain-contracts.v3";
static const char manifest_report_schema[] =
    "cupid.toolchain-manifest-verification.v1";

#define MANIFEST_ARTIFACT_COUNT 22u
#define MANIFEST_INPUT_LIMIT 256u
#define MANIFEST_EXPECTED_INPUT_COUNT 87u
#define MANIFEST_EXPECTED_BOOTSTRAP_FILE_COUNT 73u
#define MANIFEST_COMPARISON_COUNT 16u
#define MANIFEST_OBJECT_COMPARISON_COUNT 17u
#define MANIFEST_BOOTSTRAP_C_OBJECT_COUNT 25u
#define MANIFEST_BOOTSTRAP_STARTUP_OBJECT_COUNT 1u
#define MANIFEST_BOOTSTRAP_OBJECT_COUNT                                      \
  (MANIFEST_BOOTSTRAP_C_OBJECT_COUNT +                                      \
   MANIFEST_BOOTSTRAP_STARTUP_OBJECT_COUNT)
#define MANIFEST_BOOTSTRAP_TOOL_COUNT 6u

static const char manifest_expected_seed_path[] =
    "bootstrap/seeds/i386-linux/manifest.json";
static const char manifest_expected_build_plan_sha256[] =
    "fc1c7634d4cb6a9106c523fe7c5c82f38e2b8e3eb3b3dbce9166e93daa4116fe";
static const char manifest_expected_seed_build_plan_sha256[] =
    "fc1c7634d4cb6a9106c523fe7c5c82f38e2b8e3eb3b3dbce9166e93daa4116fe";
static const char manifest_expected_seed_manifest_sha256[] =
    "da26556401dd20d039ed1175f3bf857c4c8bebd50fb52b3bd75a06b95fdf41ed";

static const char *const
    manifest_expected_input_paths[MANIFEST_EXPECTED_INPUT_COUNT] = {
    "kernel/core/syscall.cc",
    "kernel/core/syscall.h",
    "kernel/core/types.h",
    "kernel/fs/vfs.h",
    "kernel/lang/as_elf.cc",
    "kernel/lang/as_elf.h",
    "kernel/network/socket.h",
    "toolchain/Makefile",
    "toolchain/artifact_size_policy.h",
    "toolchain/contract_parse_internal.cc",
    "toolchain/contract_parse_internal.h",
    "toolchain/ctool.h",
    "toolchain/ctool_host.h",
    "toolchain/cupidasm.h",
    "toolchain/cupidbuild.h",
    "toolchain/cupidbuild_host.h",
    "toolchain/cupidc_emit.h",
    "toolchain/cupidc_frontend.h",
    "toolchain/cupidc_ir.h",
    "toolchain/cupidc_pp.h",
    "toolchain/cupidc_type.h",
    "toolchain/cupiddis.h",
    "toolchain/cupidld.h",
    "toolchain/cupidobj.h",
    "toolchain/elf32.h",
    "toolchain/hosted/i386-linux/include/cupid_host_abi.h",
    "toolchain/hosted/i386-linux/include/direct.h",
    "toolchain/hosted/i386-linux/include/errno.h",
    "toolchain/hosted/i386-linux/include/stddef.h",
    "toolchain/hosted/i386-linux/include/stdint.h",
    "toolchain/hosted/i386-linux/include/stdio.h",
    "toolchain/hosted/i386-linux/include/stdlib.h",
    "toolchain/hosted/i386-linux/include/string.h",
    "toolchain/hosted/i386-linux/include/unistd.h",
    "toolchain/hosted/i386-linux/include/windows.h",
    "toolchain/hosted/i386-windows/cupidbuild_start.asm",
    "toolchain/hosted/i386-windows/publication_runtime.cc",
    "toolchain/hosted/i386-windows/publication_start.asm",
    "toolchain/hosted/i386-windows/runtime.cc",
    "toolchain/hosted/i386-windows/start.asm",
    "toolchain/hosted/i386-windows/tool_start.asm",
    "toolchain/hosted/i386-windows/utf8_cupidbuild_start.asm",
    "toolchain/hosted/i386-windows/utf8_publication_start.asm",
    "toolchain/hosted/i386-windows/utf8_tool_start.asm",
    "toolchain/hosted/i386-windows/windows_utf8.cc",
    "toolchain/native_utf8.h",
    "toolchain/path_encoding.cc",
    "toolchain/path_encoding.h",
    "toolchain/pe32.h",
    "toolchain/pe32_impl.h",
    "toolchain/seed_manifest.h",
    "toolchain/seed_release.h",
    "toolchain/tests/core_contract.cc",
    "toolchain/tests/cupidasm_contract.cc",
    "toolchain/tests/cupidasm_demos_contract.cc",
    "toolchain/tests/cupidasm_kernel_elf_contract.cc",
    "toolchain/tests/cupidc_exact_floating_literal_fixture.h",
    "toolchain/tests/cupidc_frontend_contract.cc",
    "toolchain/tests/cupidc_ir_contract.cc",
    "toolchain/tests/cupidc_kernel_simd_fixture.h",
    "toolchain/tests/cupidc_object_contract.cc",
    "toolchain/tests/cupidc_pp_active_cases.inc",
    "toolchain/tests/cupidc_pp_conditional_cases.inc",
    "toolchain/tests/cupidc_pp_contract.cc",
    "toolchain/tests/cupidc_static_long_double_arithmetic_fixture.h",
    "toolchain/tests/cupidc_static_long_double_control_fixture.h",
    "toolchain/tests/cupidc_static_long_double_integer_fixture.h",
    "toolchain/tests/cupidc_type_contract.cc",
    "toolchain/tests/cupiddis_contract.cc",
    "toolchain/tests/cupidld_contract.cc",
    "toolchain/tests/cupidobj_contract.cc",
    "toolchain/tests/elf32_contract.cc",
    "toolchain/tests/hosted_i386_runtime_contract.cc",
    "toolchain/tests/hosted_i386_windows_contract.cc",
    "toolchain/tests/hosted_i386_windows_runtime_contract.cc",
    "toolchain/tests/toolchain_manifest_contract.cc",
    "toolchain/tests/user_syscall_abi_contract.cc",
    "toolchain/tests/x86_active_cases.inc",
    "toolchain/tests/x86_catalogue_contract.inc",
    "toolchain/tests/x86_contract.cc",
    "toolchain/tests/x86_inline_cases.inc",
    "toolchain/x86.cc",
    "toolchain/x86.h",
    "tools/bootstrap_toolchain.py",
    "tools/cupidc_toolchain_contracts.py",
    "tools/user_syscall_abi.py",
    "user/cupid.h",
};

static const char *const manifest_expected_bootstrap_paths
    [MANIFEST_EXPECTED_BOOTSTRAP_FILE_COUNT] = {
    "link.ld",
    "toolchain/artifact_size_policy.h",
    "toolchain/contract_parse_internal.cc",
    "toolchain/contract_parse_internal.h",
    "toolchain/ctool.cc",
    "toolchain/ctool.h",
    "toolchain/ctool_host.cc",
    "toolchain/ctool_host.h",
    "toolchain/cupidasm.cc",
    "toolchain/cupidasm.h",
    "toolchain/cupidasm_main.cc",
    "toolchain/cupidbuild.cc",
    "toolchain/cupidbuild.h",
    "toolchain/cupidbuild_host.cc",
    "toolchain/cupidbuild_host.h",
    "toolchain/cupidbuild_main.cc",
    "toolchain/cupidc_emit.cc",
    "toolchain/cupidc_emit.h",
    "toolchain/cupidc_frontend.cc",
    "toolchain/cupidc_frontend.h",
    "toolchain/cupidc_ir.cc",
    "toolchain/cupidc_ir.h",
    "toolchain/cupidc_main.cc",
    "toolchain/cupidc_pp.cc",
    "toolchain/cupidc_pp.h",
    "toolchain/cupidc_type.cc",
    "toolchain/cupidc_type.h",
    "toolchain/cupiddis.cc",
    "toolchain/cupiddis.h",
    "toolchain/cupiddis_main.cc",
    "toolchain/cupidld.cc",
    "toolchain/cupidld.h",
    "toolchain/cupidld_main.cc",
    "toolchain/cupidobj.cc",
    "toolchain/cupidobj.h",
    "toolchain/cupidobj_main.cc",
    "toolchain/elf32.cc",
    "toolchain/elf32.h",
    "toolchain/hosted/i386-linux/include/cupid_host_abi.h",
    "toolchain/hosted/i386-linux/include/direct.h",
    "toolchain/hosted/i386-linux/include/errno.h",
    "toolchain/hosted/i386-linux/include/stddef.h",
    "toolchain/hosted/i386-linux/include/stdint.h",
    "toolchain/hosted/i386-linux/include/stdio.h",
    "toolchain/hosted/i386-linux/include/stdlib.h",
    "toolchain/hosted/i386-linux/include/string.h",
    "toolchain/hosted/i386-linux/include/unistd.h",
    "toolchain/hosted/i386-linux/include/windows.h",
    "toolchain/hosted/i386-linux/runtime.cc",
    "toolchain/hosted/i386-linux/start.asm",
    "toolchain/hosted/i386-windows/cupidbuild_start.asm",
    "toolchain/hosted/i386-windows/publication_runtime.cc",
    "toolchain/hosted/i386-windows/publication_start.asm",
    "toolchain/hosted/i386-windows/runtime.cc",
    "toolchain/hosted/i386-windows/start.asm",
    "toolchain/hosted/i386-windows/tool_start.asm",
    "toolchain/hosted/i386-windows/utf8_cupidbuild_start.asm",
    "toolchain/hosted/i386-windows/utf8_publication_start.asm",
    "toolchain/hosted/i386-windows/utf8_tool_start.asm",
    "toolchain/hosted/i386-windows/windows_utf8.cc",
    "toolchain/native_utf8.h",
    "toolchain/path_encoding.cc",
    "toolchain/path_encoding.h",
    "toolchain/pe32.h",
    "toolchain/pe32_impl.h",
    "toolchain/seed_manifest.cc",
    "toolchain/seed_manifest.h",
    "toolchain/seed_release.cc",
    "toolchain/seed_release.h",
    "toolchain/tests/hosted_i386_windows_contract.cc",
    "toolchain/tests/hosted_i386_windows_runtime_contract.cc",
    "toolchain/x86.cc",
    "toolchain/x86.h",
};

static int manifest_parse_string_literal(error_context_t *context, json_reader_t *reader,
                                         const char *expected,
                                         const char *message);

typedef struct {
  text_t path;
  text_t sha256;
  uint64_t size;
} manifest_artifact_t;

typedef struct {
  text_t path;
  text_t sha256;
  uint64_t size;
} manifest_bootstrap_file_t;

typedef struct {
  text_t path;
  text_t sha256;
  uint64_t size;
} manifest_input_t;

typedef struct {
  text_t sha256;
  uint64_t size;
} manifest_digest_size_t;

typedef struct {
  manifest_artifact_t artifacts[MANIFEST_ARTIFACT_COUNT];
  size_t artifact_count;
  uint64_t artifact_total_bytes;
  manifest_input_t inputs[MANIFEST_INPUT_LIMIT];
  size_t input_count;
  uint64_t declared_input_count;
  manifest_bootstrap_file_t bootstrap_files[MANIFEST_INPUT_LIMIT];
  size_t bootstrap_file_count;
  uint64_t declared_bootstrap_file_count;
  text_t seed_manifest_path;
  text_t seed_manifest_sha256;
  text_t build_plan_sha256;
  text_t comparisons[MANIFEST_COMPARISON_COUNT];
  manifest_digest_size_t
      object_comparisons[MANIFEST_OBJECT_COMPARISON_COUNT];
} manifest_state_t;

static const char *const manifest_artifact_names[MANIFEST_ARTIFACT_COUNT] = {
    "core-contract.elf",
    "user-syscall-abi-contract.elf",
    "cupidc-pp-contract.elf",
    "cupidc-type-contract.elf",
    "cupidc-frontend-contract.elf",
    "cupidc-ir-contract.elf",
    "cupidc-object-contract.elf",
    "elf32-contract.elf",
    "x86-contract.elf",
    "cupiddis-contract.elf",
    "cupidasm-contract.elf",
    "cupidasm-demos-contract.elf",
    "cupidasm-kernel-elf-contract.elf",
    "cupidobj-contract.elf",
    "cupidld-contract.elf",
    "cupidc-runtime-contract.elf",
    "cupidc-cupidasm.elf",
    "cupidc-cupiddis.elf",
    "cupidc-cupidld.elf",
    "cupidc-cupidobj.elf",
    "cupidc-cupidc.elf",
    "cupidc-cupidbuild.elf",
};

static const unsigned int
    manifest_artifact_output_order[MANIFEST_ARTIFACT_COUNT] = {
        0u, 10u, 11u, 12u, 16u, 21u, 20u, 17u, 18u, 19u, 4u,
        5u, 6u,  2u,  15u, 3u,  9u,  14u, 13u, 7u,  1u,  8u,
};

static const char *const manifest_comparison_names[MANIFEST_COMPARISON_COUNT] = {
    "core",          "user-syscall-abi", "cupidc-pp", "cupidc-type",
    "cupidc-frontend", "cupidc-ir",       "cupidc-object",
    "elf32",         "x86",              "cupiddis",  "cupidasm",
    "cupidasm-demos", "cupidasm-kernel-elf", "cupidobj", "cupidld",
    "runtime",
};

static const char *const
    manifest_comparison_artifacts[MANIFEST_COMPARISON_COUNT] = {
        "core-contract.elf",
        "user-syscall-abi-contract.elf",
        "cupidc-pp-contract.elf",
        "cupidc-type-contract.elf",
        "cupidc-frontend-contract.elf",
        "cupidc-ir-contract.elf",
        "cupidc-object-contract.elf",
        "elf32-contract.elf",
        "x86-contract.elf",
        "cupiddis-contract.elf",
        "cupidasm-contract.elf",
        "cupidasm-demos-contract.elf",
        "cupidasm-kernel-elf-contract.elf",
        "cupidobj-contract.elf",
        "cupidld-contract.elf",
        "cupidc-runtime-contract.elf",
};

static const char *const
    manifest_object_comparison_names[MANIFEST_OBJECT_COMPARISON_COUNT] = {
        "core",          "user-syscall-abi", "cupidc-pp", "cupidc-type",
        "cupidc-frontend", "cupidc-ir",       "cupidc-object",
        "elf32",         "x86",              "cupiddis",  "cupidasm",
        "cupidasm-demos", "cupidasm-kernel-elf", "cupidobj", "cupidld",
        "as_elf",        "runtime",
};

static const char *const
    manifest_bootstrap_object_names[MANIFEST_BOOTSTRAP_OBJECT_COUNT] = {
        "runtime",       "ctool",         "ctool_host",     "elf32",
        "x86",           "cupidasm",      "cupidasm_main",  "cupiddis",
        "cupiddis_main", "cupidobj",      "cupidobj_main",  "cupidld",
        "cupidld_main",  "cupidc_pp",     "cupidc_type",    "cupidc_frontend",
        "cupidc_ir",     "cupidc_emit",   "cupidc_main",    "cupidbuild",
        "cupidbuild_host", "cupidbuild_main", "seed_manifest", "seed_release",
        "contract_parse_internal", "start",
};

static const char *const
    manifest_bootstrap_tool_names[MANIFEST_BOOTSTRAP_TOOL_COUNT] = {
        "cupidasm", "cupiddis", "cupidld", "cupidobj", "cupidc",
        "cupidbuild",
};

static const unsigned int
    manifest_comparison_output_order[MANIFEST_COMPARISON_COUNT] = {
        0u, 10u, 11u, 12u, 4u, 5u, 6u, 2u,
        3u, 9u,  14u, 13u, 7u, 15u, 1u, 8u,
};

static const unsigned int
    manifest_object_comparison_output_order
        [MANIFEST_OBJECT_COMPARISON_COUNT] = {
            15u, 0u, 10u, 11u, 12u, 4u, 5u, 6u, 2u,
            3u,  9u, 14u, 13u, 7u, 16u, 1u, 8u,
};

static void manifest_artifact_release(manifest_artifact_t *artifact) {
  cupid_contract_text_release(&artifact->path);
  cupid_contract_text_release(&artifact->sha256);
  artifact->size = 0u;
}

static void manifest_state_release(manifest_state_t *state) {
  size_t index;
  for (index = 0u; index < state->artifact_count; index++) {
    manifest_artifact_release(&state->artifacts[index]);
  }
  for (index = 0u; index < state->input_count; index++) {
    cupid_contract_text_release(&state->inputs[index].path);
    cupid_contract_text_release(&state->inputs[index].sha256);
  }
  for (index = 0u; index < state->bootstrap_file_count; index++) {
    cupid_contract_text_release(&state->bootstrap_files[index].path);
    cupid_contract_text_release(&state->bootstrap_files[index].sha256);
  }
  state->artifact_count = 0u;
  state->artifact_total_bytes = 0u;
  state->input_count = 0u;
  state->declared_input_count = 0u;
  state->bootstrap_file_count = 0u;
  state->declared_bootstrap_file_count = 0u;
  cupid_contract_text_release(&state->seed_manifest_path);
  cupid_contract_text_release(&state->seed_manifest_sha256);
  cupid_contract_text_release(&state->build_plan_sha256);
  for (index = 0u; index < MANIFEST_COMPARISON_COUNT; index++) {
    cupid_contract_text_release(&state->comparisons[index]);
  }
  for (index = 0u; index < MANIFEST_OBJECT_COMPARISON_COUNT; index++) {
    cupid_contract_text_release(&state->object_comparisons[index].sha256);
  }
}

static int manifest_expected_inventories_match(error_context_t *context,
    const manifest_state_t *state) {
  size_t expected_index;
  size_t actual_index;
  for (expected_index = 0u;
       expected_index < MANIFEST_EXPECTED_INPUT_COUNT; expected_index++) {
    int found = 0;
    for (actual_index = 0u; actual_index < state->input_count;
         actual_index++) {
      if (cupid_contract_text_equals_literal(
              &state->inputs[actual_index].path,
              manifest_expected_input_paths[expected_index])) {
        found = 1;
        break;
      }
    }
    if (!found) {
      return cupid_contract_set_error(context, "manifest input path inventory differs");
    }
  }
  for (expected_index = 0u;
       expected_index < MANIFEST_EXPECTED_BOOTSTRAP_FILE_COUNT;
       expected_index++) {
    int found = 0;
    for (actual_index = 0u; actual_index < state->bootstrap_file_count;
         actual_index++) {
      if (cupid_contract_text_equals_literal(
              &state->bootstrap_files[actual_index].path,
              manifest_expected_bootstrap_paths[expected_index])) {
        found = 1;
        break;
      }
    }
    if (!found) {
      return cupid_contract_set_error(context, "manifest bootstrap path inventory differs");
    }
  }
  if (!cupid_contract_text_equals_literal(&state->seed_manifest_path,
                           manifest_expected_seed_path) ||
      !cupid_contract_text_equals_literal(&state->seed_manifest_sha256,
                           manifest_expected_seed_manifest_sha256) ||
      !cupid_contract_text_equals_literal(&state->build_plan_sha256,
                           manifest_expected_build_plan_sha256)) {
    return cupid_contract_set_error(context, "manifest bootstrap plan identity differs");
  }
  return 1;
}

static int manifest_sha256_valid(const text_t *digest) {
  size_t index;
  if (digest->size != 64u) {
    return 0;
  }
  for (index = 0u; index < digest->size; index++) {
    unsigned char value = digest->bytes[index];
    if (!((value >= (unsigned char)'0' && value <= (unsigned char)'9') ||
          (value >= (unsigned char)'a' && value <= (unsigned char)'f'))) {
      return 0;
    }
  }
  return 1;
}

static int manifest_slice_sha256_valid(const byte_slice_t *digest) {
  size_t index;
  if (digest->size != 64u) {
    return 0;
  }
  for (index = 0u; index < digest->size; index++) {
    unsigned char value = digest->bytes[index];
    if (!((value >= (unsigned char)'0' && value <= (unsigned char)'9') ||
          (value >= (unsigned char)'a' && value <= (unsigned char)'f'))) {
      return 0;
    }
  }
  return 1;
}

static int manifest_text_copy_slice(error_context_t *context, text_t *target,
                                    const byte_slice_t *source) {
  target->bytes = (unsigned char *)malloc(
      source->size == 0u ? 1u : source->size);
  if (target->bytes == (unsigned char *)0) {
    target->size = 0u;
    return cupid_contract_set_error(context, "cannot allocate manifest author evidence");
  }
  if (source->size != 0u) {
    (void)memcpy(target->bytes, source->bytes, source->size);
  }
  target->size = source->size;
  return 1;
}

static int manifest_slice_literal_index(const byte_slice_t *value,
                                        const char *const *names,
                                        size_t count) {
  size_t index;
  for (index = 0u; index < count; index++) {
    size_t length = strlen(names[index]);
    if (value->size == length &&
        memcmp(value->bytes, names[index], length) == 0) {
      return (int)index;
    }
  }
  return -1;
}

static int manifest_slice_equals_literal(const byte_slice_t *value,
                                         const char *literal) {
  size_t length = strlen(literal);
  return value->size == length &&
         memcmp(value->bytes, literal, length) == 0;
}

static int manifest_basename_valid(const text_t *path) {
  size_t index;
  if (path->size == 0u ||
      (path->size == 1u && path->bytes[0] == (unsigned char)'.') ||
      (path->size == 2u && path->bytes[0] == (unsigned char)'.' &&
       path->bytes[1] == (unsigned char)'.')) {
    return 0;
  }
  for (index = 0u; index < path->size; index++) {
    unsigned char value = path->bytes[index];
    if (value == (unsigned char)'/' || value == (unsigned char)'\\' ||
        value == 0u) {
      return 0;
    }
  }
  return cupid_contract_utf8_valid(path->bytes, path->size);
}

static int manifest_parse_nonnegative_u64(error_context_t *context, json_reader_t *reader,
                                          uint64_t *result) {
  uint64_t value = 0u;
  uint64_t maximum = ~(uint64_t)0u;
  size_t start;
  size_t digits = 0u;
  cupid_contract_json_skip_space(reader);
  start = reader->position;
  while (reader->position < reader->size &&
         reader->bytes[reader->position] >= (unsigned char)'0' &&
         reader->bytes[reader->position] <= (unsigned char)'9') {
    uint32_t digit =
        (uint32_t)(reader->bytes[reader->position] - (unsigned char)'0');
    if (value > (maximum - (uint64_t)digit) / 10u) {
      return cupid_contract_set_error(context, "JSON integer exceeds the unsigned 64-bit range");
    }
    value = value * 10u + (uint64_t)digit;
    reader->position++;
    digits++;
  }
  if (digits == 0u ||
      (digits > 1u && reader->bytes[start] == (unsigned char)'0') ||
      (reader->position < reader->size &&
       (reader->bytes[reader->position] == (unsigned char)'.' ||
        reader->bytes[reader->position] == (unsigned char)'e' ||
        reader->bytes[reader->position] == (unsigned char)'E'))) {
    return cupid_contract_set_error(context, "an exact nonnegative JSON integer is required");
  }
  *result = value;
  return 1;
}

static uint32_t manifest_rotate_right(uint32_t value, uint32_t count) {
  return (value >> count) | (value << (32u - count));
}

static void manifest_sha256_block(uint32_t state[8],
                                  const unsigned char block[64]) {
  static const uint32_t constants[64] = {
      0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
      0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
      0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
      0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
      0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
      0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
      0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
      0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
      0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
      0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
      0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
      0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
      0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
      0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
      0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
      0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
  uint32_t words[64];
  uint32_t a;
  uint32_t b;
  uint32_t c;
  uint32_t d;
  uint32_t e;
  uint32_t f;
  uint32_t g;
  uint32_t h;
  uint32_t index;
  for (index = 0u; index < 16u; index++) {
    uint32_t offset = index * 4u;
    words[index] = ((uint32_t)block[offset] << 24u) |
                   ((uint32_t)block[offset + 1u] << 16u) |
                   ((uint32_t)block[offset + 2u] << 8u) |
                   (uint32_t)block[offset + 3u];
  }
  for (index = 16u; index < 64u; index++) {
    uint32_t left = words[index - 15u];
    uint32_t right = words[index - 2u];
    uint32_t small_zero = manifest_rotate_right(left, 7u) ^
                          manifest_rotate_right(left, 18u) ^ (left >> 3u);
    uint32_t small_one = manifest_rotate_right(right, 17u) ^
                         manifest_rotate_right(right, 19u) ^ (right >> 10u);
    words[index] = words[index - 16u] + small_zero + words[index - 7u] +
                   small_one;
  }
  a = state[0];
  b = state[1];
  c = state[2];
  d = state[3];
  e = state[4];
  f = state[5];
  g = state[6];
  h = state[7];
  for (index = 0u; index < 64u; index++) {
    uint32_t large_one = manifest_rotate_right(e, 6u) ^
                         manifest_rotate_right(e, 11u) ^
                         manifest_rotate_right(e, 25u);
    uint32_t choose = (e & f) ^ ((~e) & g);
    uint32_t temporary_one =
        h + large_one + choose + constants[index] + words[index];
    uint32_t large_zero = manifest_rotate_right(a, 2u) ^
                          manifest_rotate_right(a, 13u) ^
                          manifest_rotate_right(a, 22u);
    uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    uint32_t temporary_two = large_zero + majority;
    h = g;
    g = f;
    f = e;
    e = d + temporary_one;
    d = c;
    c = b;
    b = a;
    a = temporary_one + temporary_two;
  }
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}

static void manifest_sha256(const unsigned char *contents, size_t size,
                            unsigned char digest[32]) {
  uint32_t state[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u,
                       0xa54ff53au, 0x510e527fu, 0x9b05688cu,
                       0x1f83d9abu, 0x5be0cd19u};
  unsigned char tail[128];
  size_t offset = 0u;
  size_t remaining;
  size_t tail_size;
  size_t index;
  uint64_t bit_length = (uint64_t)size * 8u;
  while (size - offset >= 64u) {
    manifest_sha256_block(state, contents + offset);
    offset += 64u;
  }
  remaining = size - offset;
  (void)memset(tail, 0, sizeof(tail));
  (void)memcpy(tail, contents + offset, remaining);
  tail[remaining] = 0x80u;
  tail_size = remaining < 56u ? 64u : 128u;
  for (index = 0u; index < 8u; index++) {
    tail[tail_size - 1u - index] =
        (unsigned char)((bit_length >> (index * 8u)) & 0xffu);
  }
  manifest_sha256_block(state, tail);
  if (tail_size == 128u) {
    manifest_sha256_block(state, tail + 64u);
  }
  for (index = 0u; index < 8u; index++) {
    digest[index * 4u] = (unsigned char)((state[index] >> 24u) & 0xffu);
    digest[index * 4u + 1u] =
        (unsigned char)((state[index] >> 16u) & 0xffu);
    digest[index * 4u + 2u] =
        (unsigned char)((state[index] >> 8u) & 0xffu);
    digest[index * 4u + 3u] = (unsigned char)(state[index] & 0xffu);
  }
}

static void manifest_digest_hex(const unsigned char *contents, size_t size,
                                char output[65]) {
  static const char hex[] = "0123456789abcdef";
  unsigned char digest[32];
  size_t index;
  manifest_sha256(contents, size, digest);
  for (index = 0u; index < sizeof(digest); index++) {
    output[index * 2u] = hex[digest[index] >> 4u];
    output[index * 2u + 1u] = hex[digest[index] & 15u];
  }
  output[64] = '\0';
}

static int manifest_expected_artifact_index(const text_t *path) {
  size_t index;
  for (index = 0u; index < MANIFEST_ARTIFACT_COUNT; index++) {
    if (cupid_contract_text_equals_literal(path, manifest_artifact_names[index])) {
      return (int)index;
    }
  }
  return -1;
}

static int manifest_parse_artifact(error_context_t *context, json_reader_t *reader,
                                   manifest_artifact_t *artifact) {
  unsigned int fields = 0u;
  (void)memset(artifact, 0, sizeof(*artifact));
  if (!cupid_contract_json_take(context, reader, (unsigned char)'{')) {
    return cupid_contract_set_error(context, "manifest artifact is not an object");
  }
  cupid_contract_json_skip_space(reader);
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == (unsigned char)'}') {
    return cupid_contract_set_error(context, "manifest artifact fields are missing");
  }
  for (;;) {
    text_t key = {(unsigned char *)0, 0u};
    int ok = cupid_contract_json_parse_string(context, reader, &key) &&
             cupid_contract_json_take(context, reader, (unsigned char)':');
    unsigned int field = 0u;
    if (!ok) {
      cupid_contract_text_release(&key);
      return 0;
    }
    if (cupid_contract_text_equals_literal(&key, "path")) {
      field = 1u;
      ok = cupid_contract_json_parse_string(context, reader, &artifact->path);
    } else if (cupid_contract_text_equals_literal(&key, "sha256")) {
      field = 2u;
      ok = cupid_contract_json_parse_string(context, reader, &artifact->sha256);
    } else if (cupid_contract_text_equals_literal(&key, "size")) {
      field = 4u;
      ok = manifest_parse_nonnegative_u64(context, reader, &artifact->size);
    } else {
      cupid_contract_text_release(&key);
      return cupid_contract_set_error(context, "manifest artifact has an unknown field");
    }
    cupid_contract_text_release(&key);
    if (!ok) {
      return 0;
    }
    if ((fields & field) != 0u) {
      return cupid_contract_set_error(context, "manifest artifact field is duplicated");
    }
    fields |= field;
    cupid_contract_json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)'}') {
      reader->position++;
      break;
    }
    if (!cupid_contract_json_take(context, reader, (unsigned char)',')) {
      return 0;
    }
  }
  if (fields != 7u || !manifest_basename_valid(&artifact->path) ||
      !manifest_sha256_valid(&artifact->sha256)) {
    return cupid_contract_set_error(context, "manifest artifact record differs");
  }
  return 1;
}

static int manifest_parse_artifacts(error_context_t *context, json_reader_t *reader,
                                    manifest_state_t *state) {
  int seen[MANIFEST_ARTIFACT_COUNT];
  size_t index;
  (void)memset(seen, 0, sizeof(seen));
  if (!cupid_contract_json_take(context, reader, (unsigned char)'[')) {
    return cupid_contract_set_error(context, "manifest artifacts are not a list");
  }
  cupid_contract_json_skip_space(reader);
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == (unsigned char)']') {
    reader->position++;
    return cupid_contract_set_error(context, "manifest artifacts are missing");
  }
  for (;;) {
    manifest_artifact_t artifact;
    int expected_index;
    if (state->artifact_count >= MANIFEST_ARTIFACT_COUNT) {
      return cupid_contract_set_error(context, "manifest has too many artifacts");
    }
    if (!manifest_parse_artifact(context, reader, &artifact)) {
      manifest_artifact_release(&artifact);
      return 0;
    }
    expected_index = manifest_expected_artifact_index(&artifact.path);
    if (expected_index < 0 || seen[(size_t)expected_index] != 0) {
      manifest_artifact_release(&artifact);
      return cupid_contract_set_error(context, "manifest artifact inventory differs");
    }
    if (state->artifact_total_bytes >
        ~(uint64_t)0u - artifact.size) {
      manifest_artifact_release(&artifact);
      return cupid_contract_set_error(context, "manifest artifact byte total overflows");
    }
    seen[(size_t)expected_index] = 1;
    state->artifact_total_bytes += artifact.size;
    state->artifacts[state->artifact_count] = artifact;
    state->artifact_count++;
    cupid_contract_json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)']') {
      reader->position++;
      break;
    }
    if (!cupid_contract_json_take(context, reader, (unsigned char)',')) {
      return 0;
    }
  }
  if (state->artifact_count != MANIFEST_ARTIFACT_COUNT) {
    return cupid_contract_set_error(context, "manifest artifact inventory differs");
  }
  for (index = 0u; index < MANIFEST_ARTIFACT_COUNT; index++) {
    if (seen[index] == 0) {
      return cupid_contract_set_error(context, "manifest artifact inventory differs");
    }
  }
  return 1;
}

static const manifest_artifact_t *manifest_find_artifact(
    const manifest_state_t *state, const char *name) {
  size_t index;
  for (index = 0u; index < state->artifact_count; index++) {
    if (cupid_contract_text_equals_literal(&state->artifacts[index].path, name)) {
      return &state->artifacts[index];
    }
  }
  return (const manifest_artifact_t *)0;
}

static int manifest_digest_map_index(const text_t *key,
                                     const char *const *names,
                                     size_t count) {
  size_t index;
  for (index = 0u; index < count; index++) {
    if (cupid_contract_text_equals_literal(key, names[index])) {
      return (int)index;
    }
  }
  return -1;
}

static int manifest_parse_digest_map(error_context_t *context, json_reader_t *reader,
                                     const char *const *names,
                                     text_t *retained_digests,
                                     size_t expected_count) {
  int seen[MANIFEST_OBJECT_COMPARISON_COUNT];
  size_t count = 0u;
  size_t index;
  (void)memset(seen, 0, sizeof(seen));
  if (expected_count > MANIFEST_OBJECT_COMPARISON_COUNT ||
      !cupid_contract_json_take(context, reader, (unsigned char)'{')) {
    return cupid_contract_set_error(context, "manifest comparison map is not an object");
  }
  cupid_contract_json_skip_space(reader);
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == (unsigned char)'}') {
    reader->position++;
    return cupid_contract_set_error(context, "manifest comparison map is empty");
  }
  for (;;) {
    text_t key = {(unsigned char *)0, 0u};
    text_t digest = {(unsigned char *)0, 0u};
    int map_index;
    int ok = cupid_contract_json_parse_string(context, reader, &key) &&
             cupid_contract_json_take(context, reader, (unsigned char)':') &&
             cupid_contract_json_parse_string(context, reader, &digest);
    if (!ok) {
      cupid_contract_text_release(&key);
      cupid_contract_text_release(&digest);
      return 0;
    }
    map_index = manifest_digest_map_index(&key, names, expected_count);
    if (map_index < 0 || seen[(size_t)map_index] != 0 ||
        !manifest_sha256_valid(&digest)) {
      cupid_contract_text_release(&key);
      cupid_contract_text_release(&digest);
      return cupid_contract_set_error(context, "manifest comparison record differs");
    }
    if (retained_digests != (text_t *)0) {
      retained_digests[(size_t)map_index] = digest;
      digest.bytes = (unsigned char *)0;
      digest.size = 0u;
    }
    seen[(size_t)map_index] = 1;
    count++;
    cupid_contract_text_release(&key);
    cupid_contract_text_release(&digest);
    cupid_contract_json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)'}') {
      reader->position++;
      break;
    }
    if (!cupid_contract_json_take(context, reader, (unsigned char)',')) {
      return 0;
    }
  }
  if (count != expected_count) {
    return cupid_contract_set_error(context, "manifest comparison inventory differs");
  }
  for (index = 0u; index < expected_count; index++) {
    if (seen[index] == 0) {
      return cupid_contract_set_error(context, "manifest comparison inventory differs");
    }
  }
  return 1;
}

static int manifest_parse_digest_size_record(error_context_t *context,
    json_reader_t *reader, manifest_digest_size_t *record,
    const char *message) {
  unsigned int fields = 0u;
  (void)memset(record, 0, sizeof(*record));
  if (!cupid_contract_json_take(context, reader, (unsigned char)'{')) {
    return cupid_contract_set_error(context, message);
  }
  cupid_contract_json_skip_space(reader);
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == (unsigned char)'}') {
    return cupid_contract_set_error(context, message);
  }
  for (;;) {
    text_t key = {(unsigned char *)0, 0u};
    unsigned int field = 0u;
    int ok = cupid_contract_json_parse_string(context, reader, &key) &&
             cupid_contract_json_take(context, reader, (unsigned char)':');
    if (!ok) {
      cupid_contract_text_release(&key);
      return 0;
    }
    if (cupid_contract_text_equals_literal(&key, "sha256")) {
      field = 1u;
      ok = cupid_contract_json_parse_string(context, reader, &record->sha256);
    } else if (cupid_contract_text_equals_literal(&key, "size")) {
      field = 2u;
      ok = manifest_parse_nonnegative_u64(context, reader, &record->size);
    } else {
      cupid_contract_text_release(&key);
      return cupid_contract_set_error(context, message);
    }
    cupid_contract_text_release(&key);
    if (!ok) {
      return 0;
    }
    if ((fields & field) != 0u) {
      return cupid_contract_set_error(context, message);
    }
    fields |= field;
    cupid_contract_json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)'}') {
      reader->position++;
      break;
    }
    if (!cupid_contract_json_take(context, reader, (unsigned char)',')) {
      return 0;
    }
  }
  if (fields != 3u || !manifest_sha256_valid(&record->sha256)) {
    return cupid_contract_set_error(context, message);
  }
  return 1;
}

static int manifest_parse_digest_size_map(error_context_t *context,
    json_reader_t *reader, const char *const *names,
    manifest_digest_size_t *records, size_t expected_count) {
  int seen[MANIFEST_OBJECT_COMPARISON_COUNT];
  size_t count = 0u;
  size_t index;
  (void)memset(seen, 0, sizeof(seen));
  if (expected_count > MANIFEST_OBJECT_COMPARISON_COUNT ||
      !cupid_contract_json_take(context, reader, (unsigned char)'{')) {
    return cupid_contract_set_error(context, "manifest object comparison map is not an object");
  }
  cupid_contract_json_skip_space(reader);
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == (unsigned char)'}') {
    return cupid_contract_set_error(context, "manifest object comparison map is empty");
  }
  for (;;) {
    text_t key = {(unsigned char *)0, 0u};
    manifest_digest_size_t record = {
        {(unsigned char *)0, 0u},
        0u,
    };
    int map_index;
    int ok = cupid_contract_json_parse_string(context, reader, &key) &&
             cupid_contract_json_take(context, reader, (unsigned char)':') &&
             manifest_parse_digest_size_record(context,
                 reader, &record,
                 "manifest object comparison record differs");
    if (!ok) {
      cupid_contract_text_release(&key);
      cupid_contract_text_release(&record.sha256);
      return 0;
    }
    if (record.size == 0u) {
      cupid_contract_text_release(&key);
      cupid_contract_text_release(&record.sha256);
      return cupid_contract_set_error(context, "manifest object comparison record differs");
    }
    map_index = manifest_digest_map_index(&key, names, expected_count);
    if (map_index < 0 || seen[(size_t)map_index] != 0) {
      cupid_contract_text_release(&key);
      cupid_contract_text_release(&record.sha256);
      return cupid_contract_set_error(context, "manifest object comparison record differs");
    }
    records[(size_t)map_index] = record;
    seen[(size_t)map_index] = 1;
    count++;
    cupid_contract_text_release(&key);
    cupid_contract_json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)'}') {
      reader->position++;
      break;
    }
    if (!cupid_contract_json_take(context, reader, (unsigned char)',')) {
      return 0;
    }
  }
  if (count != expected_count) {
    return cupid_contract_set_error(context, "manifest object comparison inventory differs");
  }
  for (index = 0u; index < expected_count; index++) {
    if (seen[index] == 0) {
      return cupid_contract_set_error(context, "manifest object comparison inventory differs");
    }
  }
  return 1;
}

static int manifest_comparison_digests_match(error_context_t *context,
    const manifest_state_t *state) {
  size_t index;
  for (index = 0u; index < MANIFEST_COMPARISON_COUNT; index++) {
    const manifest_artifact_t *artifact = manifest_find_artifact(
        state, manifest_comparison_artifacts[index]);
    if (artifact == (const manifest_artifact_t *)0 ||
        cupid_contract_text_compare(&state->comparisons[index], &artifact->sha256) != 0) {
      return cupid_contract_set_error(context, "manifest comparison digest differs");
    }
  }
  return 1;
}

static int manifest_parse_inputs(error_context_t *context, json_reader_t *reader,
                                 manifest_state_t *state) {
  if (!cupid_contract_json_take(context, reader, (unsigned char)'{')) {
    return cupid_contract_set_error(context, "manifest inputs are not an object");
  }
  cupid_contract_json_skip_space(reader);
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == (unsigned char)'}') {
    reader->position++;
    return 1;
  }
  for (;;) {
    text_t path = {(unsigned char *)0, 0u};
    manifest_digest_size_t record;
    size_t index;
    int ok;
    (void)memset(&record, 0, sizeof(record));
    if (state->input_count >= MANIFEST_INPUT_LIMIT) {
      return cupid_contract_set_error(context, "manifest input inventory exceeds its checked limit");
    }
    ok = cupid_contract_json_parse_string(context, reader, &path) &&
         cupid_contract_json_take(context, reader, (unsigned char)':') &&
         manifest_parse_digest_size_record(context,
             reader, &record, "manifest input record differs");
    if (!ok) {
      cupid_contract_text_release(&path);
      cupid_contract_text_release(&record.sha256);
      return 0;
    }
    if (!cupid_contract_logical_path_valid(path.bytes, path.size)) {
      cupid_contract_text_release(&path);
      cupid_contract_text_release(&record.sha256);
      return cupid_contract_set_error(context, "manifest input record differs");
    }
    for (index = 0u; index < state->input_count; index++) {
      if (cupid_contract_text_compare(&path, &state->inputs[index].path) == 0) {
        cupid_contract_text_release(&path);
        cupid_contract_text_release(&record.sha256);
        return cupid_contract_set_error(context, "manifest input path is duplicated");
      }
    }
    state->inputs[state->input_count].path = path;
    state->inputs[state->input_count].sha256 = record.sha256;
    state->inputs[state->input_count].size = record.size;
    state->input_count++;
    cupid_contract_json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)'}') {
      reader->position++;
      break;
    }
    if (!cupid_contract_json_take(context, reader, (unsigned char)',')) {
      return 0;
    }
  }
  return 1;
}

static int manifest_snapshot_path_valid(const text_t *path) {
  size_t index;
  if (!cupid_contract_logical_path_valid(path->bytes, path->size)) {
    return 0;
  }
  for (index = 0u; index < path->size; index++) {
    unsigned char value = path->bytes[index];
    if (value < 0x20u || value > 0x7eu || value == (unsigned char)'"' ||
        value == (unsigned char)'\\') {
      return 0;
    }
  }
  return 1;
}

static int manifest_parse_bootstrap_file_record(error_context_t *context,
    json_reader_t *reader, manifest_bootstrap_file_t *file) {
  unsigned int fields = 0u;
  if (!cupid_contract_json_take(context, reader, (unsigned char)'{')) {
    return cupid_contract_set_error(context, "bootstrap source file record is not an object");
  }
  cupid_contract_json_skip_space(reader);
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == (unsigned char)'}') {
    return cupid_contract_set_error(context, "bootstrap source file fields are missing");
  }
  for (;;) {
    text_t key = {(unsigned char *)0, 0u};
    int ok = cupid_contract_json_parse_string(context, reader, &key) &&
             cupid_contract_json_take(context, reader, (unsigned char)':');
    unsigned int field = 0u;
    if (!ok) {
      cupid_contract_text_release(&key);
      return 0;
    }
    if (cupid_contract_text_equals_literal(&key, "sha256")) {
      field = 1u;
      ok = cupid_contract_json_parse_string(context, reader, &file->sha256);
      if (ok && !manifest_sha256_valid(&file->sha256)) {
        ok = cupid_contract_set_error(context, "bootstrap source file digest differs");
      }
    } else if (cupid_contract_text_equals_literal(&key, "size")) {
      field = 2u;
      ok = manifest_parse_nonnegative_u64(context, reader, &file->size);
    } else {
      cupid_contract_text_release(&key);
      return cupid_contract_set_error(context, "bootstrap source file has an unknown field");
    }
    cupid_contract_text_release(&key);
    if (!ok) {
      return 0;
    }
    if ((fields & field) != 0u) {
      return cupid_contract_set_error(context, "bootstrap source file field is duplicated");
    }
    fields |= field;
    cupid_contract_json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)'}') {
      reader->position++;
      break;
    }
    if (!cupid_contract_json_take(context, reader, (unsigned char)',')) {
      return 0;
    }
  }
  if (fields != 3u) {
    return cupid_contract_set_error(context, "bootstrap source file fields are missing");
  }
  return 1;
}

static int manifest_parse_bootstrap_files(error_context_t *context, json_reader_t *reader,
                                          manifest_state_t *state) {
  if (!cupid_contract_json_take(context, reader, (unsigned char)'{')) {
    return cupid_contract_set_error(context, "bootstrap source files are not an object");
  }
  cupid_contract_json_skip_space(reader);
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == (unsigned char)'}') {
    reader->position++;
    return 1;
  }
  for (;;) {
    manifest_bootstrap_file_t file;
    size_t index;
    (void)memset(&file, 0, sizeof(file));
    if (state->bootstrap_file_count >= MANIFEST_INPUT_LIMIT) {
      return cupid_contract_set_error(context, "bootstrap source inventory exceeds its checked limit");
    }
    if (!cupid_contract_json_parse_string(context, reader, &file.path) ||
        !cupid_contract_json_take(context, reader, (unsigned char)':') ||
        !manifest_parse_bootstrap_file_record(context, reader, &file)) {
      cupid_contract_text_release(&file.path);
      cupid_contract_text_release(&file.sha256);
      return 0;
    }
    if (!manifest_snapshot_path_valid(&file.path)) {
      cupid_contract_text_release(&file.path);
      cupid_contract_text_release(&file.sha256);
      return cupid_contract_set_error(context, "bootstrap source file path is unsafe");
    }
    for (index = 0u; index < state->bootstrap_file_count; index++) {
      if (cupid_contract_text_compare(&file.path, &state->bootstrap_files[index].path) == 0) {
        cupid_contract_text_release(&file.path);
        cupid_contract_text_release(&file.sha256);
        return cupid_contract_set_error(context, "bootstrap source file path is duplicated");
      }
    }
    state->bootstrap_files[state->bootstrap_file_count] = file;
    state->bootstrap_file_count++;
    cupid_contract_json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)'}') {
      reader->position++;
      break;
    }
    if (!cupid_contract_json_take(context, reader, (unsigned char)',')) {
      return 0;
    }
  }
  return 1;
}

static void manifest_sort_bootstrap_files(manifest_state_t *state) {
  size_t index;
  for (index = 1u; index < state->bootstrap_file_count; index++) {
    manifest_bootstrap_file_t selected = state->bootstrap_files[index];
    size_t position = index;
    while (position > 0u &&
           cupid_contract_text_compare(&state->bootstrap_files[position - 1u].path,
                        &selected.path) > 0) {
      state->bootstrap_files[position] =
          state->bootstrap_files[position - 1u];
      position--;
    }
    state->bootstrap_files[position] = selected;
  }
}

static int manifest_bootstrap_snapshot_digest(error_context_t *context, manifest_state_t *state,
                                              char digest[65]) {
  static const char record_prefix[] = "\":{\"sha256\":\"";
  static const char size_prefix[] = "\",\"size\":";
  size_t capacity = 2u;
  size_t position = 0u;
  size_t index;
  unsigned char *canonical;
  manifest_sort_bootstrap_files(state);
  for (index = 0u; index < state->bootstrap_file_count; index++) {
    size_t path_size = state->bootstrap_files[index].path.size;
    if (path_size > ~(size_t)0u - capacity - 128u) {
      return cupid_contract_set_error(context, "bootstrap source snapshot is too large");
    }
    capacity += path_size + 128u;
  }
  canonical = (unsigned char *)malloc(capacity);
  if (canonical == (unsigned char *)0) {
    return cupid_contract_set_error(context, "cannot allocate the bootstrap source snapshot");
  }
  canonical[position++] = (unsigned char)'{';
  for (index = 0u; index < state->bootstrap_file_count; index++) {
    manifest_bootstrap_file_t *file = &state->bootstrap_files[index];
    char size_text[32];
    int size_length = snprintf(size_text, sizeof(size_text), "%llu",
                               (unsigned long long)file->size);
    if (size_length < 1 || (size_t)size_length >= sizeof(size_text)) {
      free(canonical);
      return cupid_contract_set_error(context, "bootstrap source size cannot be serialized");
    }
    if (index != 0u) {
      canonical[position++] = (unsigned char)',';
    }
    canonical[position++] = (unsigned char)'"';
    (void)memcpy(canonical + position, file->path.bytes, file->path.size);
    position += file->path.size;
    (void)memcpy(canonical + position, record_prefix,
                 sizeof(record_prefix) - 1u);
    position += sizeof(record_prefix) - 1u;
    (void)memcpy(canonical + position, file->sha256.bytes,
                 file->sha256.size);
    position += file->sha256.size;
    (void)memcpy(canonical + position, size_prefix,
                 sizeof(size_prefix) - 1u);
    position += sizeof(size_prefix) - 1u;
    (void)memcpy(canonical + position, size_text, (size_t)size_length);
    position += (size_t)size_length;
    canonical[position++] = (unsigned char)'}';
  }
  canonical[position++] = (unsigned char)'}';
  if (position > capacity) {
    free(canonical);
    return cupid_contract_set_error(context, "bootstrap source snapshot serialization overflowed");
  }
  manifest_digest_hex(canonical, position, digest);
  free(canonical);
  return 1;
}

static int manifest_parse_source_inputs(error_context_t *context, json_reader_t *reader,
                                        manifest_state_t *state) {
  unsigned int fields = 0u;
  text_t expected_digest = {(unsigned char *)0, 0u};
  char actual_digest[65];
  if (!cupid_contract_json_take(context, reader, (unsigned char)'{')) {
    return cupid_contract_set_error(context, "bootstrap source inventory is not an object");
  }
  cupid_contract_json_skip_space(reader);
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == (unsigned char)'}') {
    return cupid_contract_set_error(context, "bootstrap source inventory fields are missing");
  }
  for (;;) {
    text_t key = {(unsigned char *)0, 0u};
    int ok = cupid_contract_json_parse_string(context, reader, &key) &&
             cupid_contract_json_take(context, reader, (unsigned char)':');
    unsigned int field = 0u;
    if (!ok) {
      cupid_contract_text_release(&key);
      cupid_contract_text_release(&expected_digest);
      return 0;
    }
    if (cupid_contract_text_equals_literal(&key, "count")) {
      field = 1u;
      ok = manifest_parse_nonnegative_u64(context,
          reader, &state->declared_bootstrap_file_count);
    } else if (cupid_contract_text_equals_literal(&key, "files")) {
      field = 2u;
      ok = manifest_parse_bootstrap_files(context, reader, state);
    } else if (cupid_contract_text_equals_literal(&key, "sha256")) {
      field = 4u;
      ok = cupid_contract_json_parse_string(context, reader, &expected_digest);
      if (ok && !manifest_sha256_valid(&expected_digest)) {
        ok = cupid_contract_set_error(context, "bootstrap source inventory digest differs");
      }
    } else {
      cupid_contract_text_release(&key);
      cupid_contract_text_release(&expected_digest);
      return cupid_contract_set_error(context, "bootstrap source inventory has an unknown field");
    }
    cupid_contract_text_release(&key);
    if (!ok) {
      cupid_contract_text_release(&expected_digest);
      return 0;
    }
    if ((fields & field) != 0u) {
      cupid_contract_text_release(&expected_digest);
      return cupid_contract_set_error(context, "bootstrap source inventory field is duplicated");
    }
    fields |= field;
    cupid_contract_json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)'}') {
      reader->position++;
      break;
    }
    if (!cupid_contract_json_take(context, reader, (unsigned char)',')) {
      cupid_contract_text_release(&expected_digest);
      return 0;
    }
  }
  if (fields != 7u ||
      state->bootstrap_file_count !=
          MANIFEST_EXPECTED_BOOTSTRAP_FILE_COUNT ||
      state->declared_bootstrap_file_count !=
          (uint64_t)state->bootstrap_file_count ||
      !manifest_bootstrap_snapshot_digest(context, state, actual_digest) ||
      expected_digest.size != 64u ||
      memcmp(expected_digest.bytes, actual_digest, 64u) != 0) {
    cupid_contract_text_release(&expected_digest);
    if (!context->has_error) {
      return cupid_contract_set_error(context, "bootstrap source inventory differs");
    }
    return 0;
  }
  cupid_contract_text_release(&expected_digest);
  return 1;
}

static int manifest_parse_seed_record(error_context_t *context, json_reader_t *reader,
                                      manifest_state_t *state) {
  unsigned int fields = 0u;
  if (!cupid_contract_json_take(context, reader, (unsigned char)'{')) {
    return cupid_contract_set_error(context, "bootstrap seed record is not an object");
  }
  cupid_contract_json_skip_space(reader);
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == (unsigned char)'}') {
    return cupid_contract_set_error(context, "bootstrap seed fields are missing");
  }
  for (;;) {
    text_t key = {(unsigned char *)0, 0u};
    text_t value = {(unsigned char *)0, 0u};
    int ok = cupid_contract_json_parse_string(context, reader, &key) &&
             cupid_contract_json_take(context, reader, (unsigned char)':') &&
             cupid_contract_json_parse_string(context, reader, &value);
    unsigned int field = 0u;
    if (!ok) {
      cupid_contract_text_release(&key);
      cupid_contract_text_release(&value);
      return 0;
    }
    if (cupid_contract_text_equals_literal(&key, "path")) {
      field = 1u;
      if ((fields & field) != 0u) {
        ok = cupid_contract_set_error(context, "bootstrap seed field is duplicated");
      } else if (!cupid_contract_logical_path_valid(value.bytes, value.size)) {
        ok = cupid_contract_set_error(context, "bootstrap seed path is unsafe");
      } else {
        state->seed_manifest_path = value;
        value.bytes = (unsigned char *)0;
        value.size = 0u;
      }
    } else if (cupid_contract_text_equals_literal(&key, "sha256")) {
      field = 2u;
      if ((fields & field) != 0u) {
        ok = cupid_contract_set_error(context, "bootstrap seed field is duplicated");
      } else if (!manifest_sha256_valid(&value)) {
        ok = cupid_contract_set_error(context, "bootstrap seed digest differs");
      } else {
        state->seed_manifest_sha256 = value;
        value.bytes = (unsigned char *)0;
        value.size = 0u;
      }
    } else {
      cupid_contract_text_release(&key);
      cupid_contract_text_release(&value);
      return cupid_contract_set_error(context, "bootstrap seed record has an unknown field");
    }
    cupid_contract_text_release(&key);
    cupid_contract_text_release(&value);
    if (!ok) {
      return 0;
    }
    if ((fields & field) != 0u) {
      return cupid_contract_set_error(context, "bootstrap seed field is duplicated");
    }
    fields |= field;
    cupid_contract_json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)'}') {
      reader->position++;
      break;
    }
    if (!cupid_contract_json_take(context, reader, (unsigned char)',')) {
      return 0;
    }
  }
  if (fields != 3u) {
    return cupid_contract_set_error(context, "bootstrap seed fields are missing");
  }
  return 1;
}

static int manifest_parse_bootstrap(error_context_t *context, json_reader_t *reader,
                                    manifest_state_t *state) {
  unsigned int fields = 0u;
  if (!cupid_contract_json_take(context, reader, (unsigned char)'{')) {
    return cupid_contract_set_error(context, "manifest bootstrap record is not an object");
  }
  cupid_contract_json_skip_space(reader);
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == (unsigned char)'}') {
    return cupid_contract_set_error(context, "manifest bootstrap fields are missing");
  }
  for (;;) {
    text_t key = {(unsigned char *)0, 0u};
    int ok = cupid_contract_json_parse_string(context, reader, &key) &&
             cupid_contract_json_take(context, reader, (unsigned char)':');
    unsigned int field = 0u;
    if (!ok) {
      cupid_contract_text_release(&key);
      return 0;
    }
    if (cupid_contract_text_equals_literal(&key, "build_plan_sha256")) {
      text_t digest = {(unsigned char *)0, 0u};
      field = 1u;
      ok = cupid_contract_json_parse_string(context, reader, &digest);
      if (ok && (fields & field) != 0u) {
        ok = cupid_contract_set_error(context, "manifest bootstrap field is duplicated");
      } else if (ok && !manifest_sha256_valid(&digest)) {
        ok = cupid_contract_set_error(context, "bootstrap build plan digest differs");
      } else if (ok) {
        state->build_plan_sha256 = digest;
        digest.bytes = (unsigned char *)0;
        digest.size = 0u;
      }
      cupid_contract_text_release(&digest);
    } else if (cupid_contract_text_equals_literal(&key, "seed_manifest")) {
      field = 2u;
      ok = manifest_parse_seed_record(context, reader, state);
    } else if (cupid_contract_text_equals_literal(&key, "source_inputs")) {
      field = 4u;
      ok = manifest_parse_source_inputs(context, reader, state);
    } else {
      cupid_contract_text_release(&key);
      return cupid_contract_set_error(context, "manifest bootstrap record has an unknown field");
    }
    cupid_contract_text_release(&key);
    if (!ok) {
      return 0;
    }
    if ((fields & field) != 0u) {
      return cupid_contract_set_error(context, "manifest bootstrap field is duplicated");
    }
    fields |= field;
    cupid_contract_json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)'}') {
      reader->position++;
      break;
    }
    if (!cupid_contract_json_take(context, reader, (unsigned char)',')) {
      return 0;
    }
  }
  if (fields != 7u) {
    return cupid_contract_set_error(context, "manifest bootstrap fields are missing");
  }
  return 1;
}

typedef struct {
  text_t files[SEED_ARTIFACT_COUNT];
  text_t sha256[SEED_ARTIFACT_COUNT];
  uint64_t sizes[SEED_ARTIFACT_COUNT];
  int seen[SEED_ARTIFACT_COUNT];
  text_t build_plan_sha256;
} manifest_seed_closure_t;

static void manifest_seed_closure_release(manifest_seed_closure_t *closure) {
  size_t index;
  for (index = 0u; index < SEED_ARTIFACT_COUNT; index++) {
    cupid_contract_text_release(&closure->files[index]);
    cupid_contract_text_release(&closure->sha256[index]);
  }
  cupid_contract_text_release(&closure->build_plan_sha256);
}

static int manifest_parse_seed_closure_artifact(error_context_t *context,
    json_reader_t *reader, manifest_seed_closure_t *closure) {
  unsigned int fields = 0u;
  text_t name = {(unsigned char *)0, 0u};
  text_t file = {(unsigned char *)0, 0u};
  text_t digest = {(unsigned char *)0, 0u};
  uint64_t size = 0u;
  int seed_index;
  if (!cupid_contract_json_take(context, reader, (unsigned char)'{')) {
    return cupid_contract_set_error(context, "seed artifact is not an object");
  }
  cupid_contract_json_skip_space(reader);
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == (unsigned char)'}') {
    return cupid_contract_set_error(context, "seed artifact fields are missing");
  }
  for (;;) {
    text_t key = {(unsigned char *)0, 0u};
    unsigned int field = 0u;
    int ok = cupid_contract_json_parse_string(context, reader, &key) &&
             cupid_contract_json_take(context, reader, (unsigned char)':');
    if (!ok) {
      cupid_contract_text_release(&key);
      cupid_contract_text_release(&name);
      cupid_contract_text_release(&file);
      cupid_contract_text_release(&digest);
      return 0;
    }
    if (cupid_contract_text_equals_literal(&key, "name")) {
      field = 1u;
      ok = cupid_contract_json_parse_string(context, reader, &name);
    } else if (cupid_contract_text_equals_literal(&key, "file")) {
      field = 2u;
      ok = cupid_contract_json_parse_string(context, reader, &file);
    } else if (cupid_contract_text_equals_literal(&key, "sha256")) {
      field = 4u;
      ok = cupid_contract_json_parse_string(context, reader, &digest);
    } else if (cupid_contract_text_equals_literal(&key, "size")) {
      field = 8u;
      ok = manifest_parse_nonnegative_u64(context, reader, &size);
    } else {
      ok = cupid_contract_json_skip_value(context, reader, 1u);
    }
    cupid_contract_text_release(&key);
    if (!ok) {
      cupid_contract_text_release(&name);
      cupid_contract_text_release(&file);
      cupid_contract_text_release(&digest);
      return 0;
    }
    if (field != 0u && (fields & field) != 0u) {
      cupid_contract_text_release(&name);
      cupid_contract_text_release(&file);
      cupid_contract_text_release(&digest);
      return cupid_contract_set_error(context, "seed artifact field is duplicated");
    }
    fields |= field;
    cupid_contract_json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)'}') {
      reader->position++;
      break;
    }
    if (!cupid_contract_json_take(context, reader, (unsigned char)',')) {
      cupid_contract_text_release(&name);
      cupid_contract_text_release(&file);
      cupid_contract_text_release(&digest);
      return 0;
    }
  }
  seed_index = cupid_contract_seed_index_for_name(&name);
  if (fields != 15u || seed_index < 0 || size == 0u ||
      closure->seen[(size_t)seed_index] != 0 ||
      !cupid_contract_text_equals_literal(&file, cupid_contract_seed_files[(size_t)seed_index]) ||
      !manifest_sha256_valid(&digest)) {
    cupid_contract_text_release(&name);
    cupid_contract_text_release(&file);
    cupid_contract_text_release(&digest);
    return cupid_contract_set_error(context, "seed artifact record differs");
  }
  closure->files[(size_t)seed_index] = file;
  closure->sha256[(size_t)seed_index] = digest;
  closure->sizes[(size_t)seed_index] = size;
  closure->seen[(size_t)seed_index] = 1;
  cupid_contract_text_release(&name);
  return 1;
}

static int manifest_parse_seed_closure_artifacts(error_context_t *context,
    json_reader_t *reader, manifest_seed_closure_t *closure) {
  size_t count = 0u;
  if (!cupid_contract_json_take(context, reader, (unsigned char)'[')) {
    return cupid_contract_set_error(context, "seed artifacts are not a list");
  }
  cupid_contract_json_skip_space(reader);
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == (unsigned char)']') {
    return cupid_contract_set_error(context, "seed artifacts are missing");
  }
  for (;;) {
    if (count >= SEED_ARTIFACT_COUNT) {
      return cupid_contract_set_error(context, "seed artifact inventory has too many entries");
    }
    if (!manifest_parse_seed_closure_artifact(context, reader, closure)) {
      return 0;
    }
    count++;
    cupid_contract_json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)']') {
      reader->position++;
      break;
    }
    if (!cupid_contract_json_take(context, reader, (unsigned char)',')) {
      return 0;
    }
  }
  if (count != SEED_ARTIFACT_COUNT) {
    return cupid_contract_set_error(context, "seed artifact inventory differs");
  }
  return 1;
}

static int manifest_parse_seed_closure(error_context_t *context,
    byte_slice_t source, manifest_seed_closure_t *closure) {
  json_reader_t reader = {source.bytes, source.size, 0u};
  unsigned int fields = 0u;
  (void)memset(closure, 0, sizeof(*closure));
  if (!cupid_contract_json_take(context, &reader, (unsigned char)'{')) {
    return cupid_contract_set_error(context, "seed manifest is not an object");
  }
  cupid_contract_json_skip_space(&reader);
  if (reader.position < reader.size &&
      reader.bytes[reader.position] == (unsigned char)'}') {
    return cupid_contract_set_error(context, "seed manifest fields are missing");
  }
  for (;;) {
    text_t key = {(unsigned char *)0, 0u};
    unsigned int field = 0u;
    int ok = cupid_contract_json_parse_string(context, &reader, &key) &&
             cupid_contract_json_take(context, &reader, (unsigned char)':');
    if (!ok) {
      cupid_contract_text_release(&key);
      return 0;
    }
    if (cupid_contract_text_equals_literal(&key, "schema")) {
      field = 1u;
      ok = manifest_parse_string_literal(context,
          &reader, "cupid.bootstrap-seed.v2", "seed manifest schema differs");
    } else if (cupid_contract_text_equals_literal(&key, "artifacts")) {
      field = 2u;
      ok = manifest_parse_seed_closure_artifacts(context, &reader, closure);
    } else if (cupid_contract_text_equals_literal(&key, "build_plan_sha256")) {
      field = 4u;
      ok = cupid_contract_json_parse_string(context, &reader, &closure->build_plan_sha256);
      if (ok && !manifest_sha256_valid(&closure->build_plan_sha256)) {
        ok = cupid_contract_set_error(context, "seed build plan digest differs");
      }
    } else {
      ok = cupid_contract_json_skip_value(context, &reader, 1u);
    }
    cupid_contract_text_release(&key);
    if (!ok) {
      return 0;
    }
    if (field != 0u && (fields & field) != 0u) {
      return cupid_contract_set_error(context, "seed manifest field is duplicated");
    }
    fields |= field;
    cupid_contract_json_skip_space(&reader);
    if (reader.position < reader.size &&
        reader.bytes[reader.position] == (unsigned char)'}') {
      reader.position++;
      break;
    }
    if (!cupid_contract_json_take(context, &reader, (unsigned char)',')) {
      return 0;
    }
  }
  if (fields != 7u) {
    return cupid_contract_set_error(context, "seed manifest fields are missing");
  }
  return cupid_contract_json_finish(context, &reader);
}

static int manifest_parse_string_literal(error_context_t *context, json_reader_t *reader,
                                         const char *expected,
                                         const char *message) {
  text_t value = {(unsigned char *)0, 0u};
  int ok = cupid_contract_json_parse_string(context, reader, &value);
  if (ok && !cupid_contract_text_equals_literal(&value, expected)) {
    ok = cupid_contract_set_error(context, message);
  }
  cupid_contract_text_release(&value);
  return ok;
}

static int manifest_parse_target(error_context_t *context, json_reader_t *reader) {
  unsigned int fields = 0u;
  if (!cupid_contract_json_take(context, reader, (unsigned char)'{')) {
    return cupid_contract_set_error(context, "manifest target is not an object");
  }
  cupid_contract_json_skip_space(reader);
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == (unsigned char)'}') {
    return cupid_contract_set_error(context, "manifest target fields are missing");
  }
  for (;;) {
    text_t key = {(unsigned char *)0, 0u};
    int ok = cupid_contract_json_parse_string(context, reader, &key) &&
             cupid_contract_json_take(context, reader, (unsigned char)':');
    unsigned int field = 0u;
    if (!ok) {
      cupid_contract_text_release(&key);
      return 0;
    }
    if (cupid_contract_text_equals_literal(&key, "architecture")) {
      field = 1u;
      ok = manifest_parse_string_literal(context,
          reader, "i386", "manifest target architecture differs");
    } else if (cupid_contract_text_equals_literal(&key, "entry")) {
      uint64_t entry = 0u;
      field = 2u;
      ok = cupid_contract_json_parse_positive_u64(context, reader, &entry);
      if (ok && entry != 0x08048000u) {
        ok = cupid_contract_set_error(context, "manifest target entry differs");
      }
    } else if (cupid_contract_text_equals_literal(&key, "linkage")) {
      field = 4u;
      ok = manifest_parse_string_literal(context,
          reader, "static", "manifest target linkage differs");
    } else if (cupid_contract_text_equals_literal(&key, "operating_system")) {
      field = 8u;
      ok = manifest_parse_string_literal(context,
          reader, "linux", "manifest target operating system differs");
    } else {
      cupid_contract_text_release(&key);
      return cupid_contract_set_error(context, "manifest target has an unknown field");
    }
    cupid_contract_text_release(&key);
    if (!ok) {
      return 0;
    }
    if ((fields & field) != 0u) {
      return cupid_contract_set_error(context, "manifest target field is duplicated");
    }
    fields |= field;
    cupid_contract_json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)'}') {
      reader->position++;
      break;
    }
    if (!cupid_contract_json_take(context, reader, (unsigned char)',')) {
      return 0;
    }
  }
  if (fields != 15u) {
    return cupid_contract_set_error(context, "manifest target fields are missing");
  }
  return 1;
}

static int manifest_parse_expected_u64(error_context_t *context, json_reader_t *reader,
                                       uint64_t expected,
                                       const char *message) {
  uint64_t value = 0u;
  if (!cupid_contract_json_parse_positive_u64(context, reader, &value)) {
    return 0;
  }
  if (value != expected) {
    return cupid_contract_set_error(context, message);
  }
  return 1;
}

static int manifest_parse_generations(error_context_t *context, json_reader_t *reader) {
  if (!cupid_contract_json_take(context, reader, (unsigned char)'[') ||
      !manifest_parse_string_literal(context,
          reader, "stage-three", "manifest compared generation differs") ||
      !cupid_contract_json_take(context, reader, (unsigned char)',') ||
      !manifest_parse_string_literal(context,
          reader, "stage-four", "manifest compared generation differs") ||
      !cupid_contract_json_take(context, reader, (unsigned char)']')) {
    return 0;
  }
  return 1;
}

static int manifest_parse_fixed_point(error_context_t *context, json_reader_t *reader) {
  unsigned int fields = 0u;
  if (!cupid_contract_json_take(context, reader, (unsigned char)'{')) {
    return cupid_contract_set_error(context, "manifest fixed-point record is not an object");
  }
  cupid_contract_json_skip_space(reader);
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == (unsigned char)'}') {
    return cupid_contract_set_error(context, "manifest fixed-point fields are missing");
  }
  for (;;) {
    text_t key = {(unsigned char *)0, 0u};
    int ok = cupid_contract_json_parse_string(context, reader, &key) &&
             cupid_contract_json_take(context, reader, (unsigned char)':');
    unsigned int field = 0u;
    if (!ok) {
      cupid_contract_text_release(&key);
      return 0;
    }
    if (cupid_contract_text_equals_literal(&key, "all_equal")) {
      field = 1u;
      cupid_contract_json_skip_space(reader);
      ok = cupid_contract_json_match_literal(reader, "true");
      if (!ok) {
        ok = cupid_contract_set_error(context, "manifest fixed-point equality differs");
      }
    } else if (cupid_contract_text_equals_literal(&key, "c_objects")) {
      field = 2u;
      ok = manifest_parse_expected_u64(context,
          reader, MANIFEST_BOOTSTRAP_C_OBJECT_COUNT,
          "manifest fixed-point C object count differs");
    } else if (cupid_contract_text_equals_literal(&key, "compared_generations")) {
      field = 4u;
      ok = manifest_parse_generations(context, reader);
    } else if (cupid_contract_text_equals_literal(&key, "startup_objects")) {
      field = 8u;
      ok = manifest_parse_expected_u64(context,
          reader, 1u, "manifest startup object count differs");
    } else if (cupid_contract_text_equals_literal(&key, "tool_images")) {
      field = 16u;
      ok = manifest_parse_expected_u64(context,
          reader, MANIFEST_BOOTSTRAP_TOOL_COUNT,
          "manifest tool image count differs");
    } else {
      cupid_contract_text_release(&key);
      return cupid_contract_set_error(context, "manifest fixed-point record has an unknown field");
    }
    cupid_contract_text_release(&key);
    if (!ok) {
      return 0;
    }
    if ((fields & field) != 0u) {
      return cupid_contract_set_error(context, "manifest fixed-point field is duplicated");
    }
    fields |= field;
    cupid_contract_json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)'}') {
      reader->position++;
      break;
    }
    if (!cupid_contract_json_take(context, reader, (unsigned char)',')) {
      return 0;
    }
  }
  if (fields != 31u) {
    return cupid_contract_set_error(context, "manifest fixed-point fields are missing");
  }
  return 1;
}

static int manifest_parse(error_context_t *context, byte_slice_t source, manifest_state_t *state) {
  json_reader_t reader = {source.bytes, source.size, 0u};
  unsigned int fields = 0u;
  if (!cupid_contract_json_take(context, &reader, (unsigned char)'{')) {
    return cupid_contract_set_error(context, "manifest is not a JSON object");
  }
  cupid_contract_json_skip_space(&reader);
  if (reader.position < reader.size &&
      reader.bytes[reader.position] == (unsigned char)'}') {
    return cupid_contract_set_error(context, "manifest fields are missing");
  }
  for (;;) {
    text_t key = {(unsigned char *)0, 0u};
    int ok = cupid_contract_json_parse_string(context, &reader, &key) &&
             cupid_contract_json_take(context, &reader, (unsigned char)':');
    unsigned int field = 0u;
    if (!ok) {
      cupid_contract_text_release(&key);
      return 0;
    }
    if (cupid_contract_text_equals_literal(&key, "artifacts")) {
      field = 1u;
      ok = manifest_parse_artifacts(context, &reader, state);
    } else if (cupid_contract_text_equals_literal(&key, "bootstrap")) {
      field = 2u;
      ok = manifest_parse_bootstrap(context, &reader, state);
    } else if (cupid_contract_text_equals_literal(&key, "comparisons")) {
      field = 4u;
      ok = manifest_parse_digest_map(context,
          &reader, manifest_comparison_names, state->comparisons,
          MANIFEST_COMPARISON_COUNT);
    } else if (cupid_contract_text_equals_literal(&key, "input_count")) {
      field = 8u;
      ok = manifest_parse_nonnegative_u64(context,
          &reader, &state->declared_input_count);
    } else if (cupid_contract_text_equals_literal(&key, "inputs")) {
      field = 16u;
      ok = manifest_parse_inputs(context, &reader, state);
    } else if (cupid_contract_text_equals_literal(&key, "object_comparisons")) {
      field = 32u;
      ok = manifest_parse_digest_size_map(context,
          &reader, manifest_object_comparison_names,
          state->object_comparisons,
          MANIFEST_OBJECT_COMPARISON_COUNT);
    } else if (cupid_contract_text_equals_literal(&key, "schema")) {
      field = 64u;
      ok = manifest_parse_string_literal(context,
          &reader, manifest_schema, "manifest schema differs");
    } else if (cupid_contract_text_equals_literal(&key, "status")) {
      field = 128u;
      ok = manifest_parse_string_literal(context,
          &reader, "pass", "manifest status differs");
    } else if (cupid_contract_text_equals_literal(&key, "target")) {
      field = 256u;
      ok = manifest_parse_target(context, &reader);
    } else if (cupid_contract_text_equals_literal(&key, "tool_fixed_point")) {
      field = 512u;
      ok = manifest_parse_fixed_point(context, &reader);
    } else {
      cupid_contract_text_release(&key);
      return cupid_contract_set_error(context, "manifest has an unknown field");
    }
    cupid_contract_text_release(&key);
    if (!ok) {
      return 0;
    }
    if ((fields & field) != 0u) {
      return cupid_contract_set_error(context, "manifest field is duplicated");
    }
    fields |= field;
    cupid_contract_json_skip_space(&reader);
    if (reader.position < reader.size &&
        reader.bytes[reader.position] == (unsigned char)'}') {
      reader.position++;
      break;
    }
    if (!cupid_contract_json_take(context, &reader, (unsigned char)',')) {
      return 0;
    }
  }
  if (fields != 1023u) {
    return cupid_contract_set_error(context, "manifest fields are missing");
  }
  if (state->declared_input_count != (uint64_t)state->input_count ||
      state->input_count != MANIFEST_EXPECTED_INPUT_COUNT) {
    return cupid_contract_set_error(context, "manifest input count differs from its inventory");
  }
  if (state->bootstrap_file_count !=
          MANIFEST_EXPECTED_BOOTSTRAP_FILE_COUNT ||
      !manifest_expected_inventories_match(context, state)) {
    return 0;
  }
  if (!manifest_comparison_digests_match(context, state)) {
    return 0;
  }
  return cupid_contract_json_finish(context, &reader);
}

static int manifest_validate_request(error_context_t *context, const file_image_t *request,
                                     manifest_state_t *state) {
  binary_reader_t reader = {request->bytes, request->size, 0u};
  byte_slice_t manifest_source;
  int matched[MANIFEST_ARTIFACT_COUNT];
  int input_matched[MANIFEST_INPUT_LIMIT];
  int bootstrap_matched[MANIFEST_INPUT_LIMIT];
  int seed_matched[SEED_ARTIFACT_COUNT];
  uint32_t observation_count;
  size_t index;
  (void)memset(state, 0, sizeof(*state));
  (void)memset(matched, 0, sizeof(matched));
  (void)memset(input_matched, 0, sizeof(input_matched));
  (void)memset(bootstrap_matched, 0, sizeof(bootstrap_matched));
  (void)memset(seed_matched, 0, sizeof(seed_matched));
  if (reader.size < sizeof(manifest_request_magic) ||
      memcmp(reader.bytes, manifest_request_magic,
             sizeof(manifest_request_magic)) != 0) {
    return cupid_contract_set_error(context, "request magic differs from CUPMAN2");
  }
  reader.position = sizeof(manifest_request_magic);
  if (!cupid_contract_binary_read_slice(context, &reader, &manifest_source)) {
    return 0;
  }
  if (!manifest_parse(context, manifest_source, state) ||
      !cupid_contract_binary_read_u32(context, &reader, &observation_count)) {
    return 0;
  }
  if (observation_count != MANIFEST_ARTIFACT_COUNT) {
    return cupid_contract_set_error(context, "request artifact observation count differs");
  }
  for (index = 0u; index < MANIFEST_ARTIFACT_COUNT; index++) {
    byte_slice_t name;
    byte_slice_t digest;
    uint32_t kind;
    uint64_t size;
    size_t artifact_index;
    int found = -1;
    if (!cupid_contract_binary_read_slice(context, &reader, &name) ||
        !cupid_contract_binary_read_u32(context, &reader, &kind) ||
        !cupid_contract_binary_read_u64(context, &reader, &size) ||
        !cupid_contract_binary_read_slice(context, &reader, &digest)) {
      return 0;
    }
    for (artifact_index = 0u; artifact_index < state->artifact_count;
         artifact_index++) {
      if (cupid_contract_slice_equals_text(&name, &state->artifacts[artifact_index].path)) {
        found = (int)artifact_index;
        break;
      }
    }
    if (found < 0 || matched[(size_t)found] != 0 || kind != 1u ||
        size != state->artifacts[(size_t)found].size ||
        !cupid_contract_slice_equals_text(&digest,
                           &state->artifacts[(size_t)found].sha256)) {
      return cupid_contract_set_error(context, "artifact observation differs from the manifest");
    }
    matched[(size_t)found] = 1;
  }
  for (index = 0u; index < MANIFEST_ARTIFACT_COUNT; index++) {
    if (matched[index] == 0) {
      return cupid_contract_set_error(context, "artifact observation is missing");
    }
  }
  if (!cupid_contract_binary_read_u32(context, &reader, &observation_count) ||
      observation_count != MANIFEST_EXPECTED_INPUT_COUNT) {
    return cupid_contract_set_error(context, "request input observation count differs");
  }
  for (index = 0u; index < MANIFEST_EXPECTED_INPUT_COUNT; index++) {
    byte_slice_t path;
    byte_slice_t digest;
    uint32_t kind;
    uint64_t size;
    size_t input_index;
    int found = -1;
    if (!cupid_contract_binary_read_slice(context, &reader, &path) ||
        !cupid_contract_binary_read_u32(context, &reader, &kind) ||
        !cupid_contract_binary_read_u64(context, &reader, &size) ||
        !cupid_contract_binary_read_slice(context, &reader, &digest)) {
      return 0;
    }
    for (input_index = 0u; input_index < state->input_count; input_index++) {
      if (cupid_contract_slice_equals_text(&path, &state->inputs[input_index].path)) {
        found = (int)input_index;
        break;
      }
    }
    if (found < 0 || input_matched[(size_t)found] != 0 || kind != 1u ||
        size != state->inputs[(size_t)found].size ||
        !cupid_contract_slice_equals_text(&digest, &state->inputs[(size_t)found].sha256)) {
      return cupid_contract_set_error(context, "live input observation differs from the manifest");
    }
    input_matched[(size_t)found] = 1;
  }
  for (index = 0u; index < state->input_count; index++) {
    if (input_matched[index] == 0) {
      return cupid_contract_set_error(context, "live input observation is missing");
    }
  }
  if (!cupid_contract_binary_read_u32(context, &reader, &observation_count) ||
      observation_count != MANIFEST_EXPECTED_BOOTSTRAP_FILE_COUNT) {
    return cupid_contract_set_error(context, "request bootstrap observation count differs");
  }
  for (index = 0u; index < MANIFEST_EXPECTED_BOOTSTRAP_FILE_COUNT; index++) {
    byte_slice_t path;
    byte_slice_t digest;
    uint32_t kind;
    uint64_t size;
    size_t file_index;
    int found = -1;
    if (!cupid_contract_binary_read_slice(context, &reader, &path) ||
        !cupid_contract_binary_read_u32(context, &reader, &kind) ||
        !cupid_contract_binary_read_u64(context, &reader, &size) ||
        !cupid_contract_binary_read_slice(context, &reader, &digest)) {
      return 0;
    }
    for (file_index = 0u; file_index < state->bootstrap_file_count;
         file_index++) {
      if (cupid_contract_slice_equals_text(&path, &state->bootstrap_files[file_index].path)) {
        found = (int)file_index;
        break;
      }
    }
    if (found < 0 || bootstrap_matched[(size_t)found] != 0 || kind != 1u ||
        size != state->bootstrap_files[(size_t)found].size ||
        !cupid_contract_slice_equals_text(
            &digest, &state->bootstrap_files[(size_t)found].sha256)) {
      return cupid_contract_set_error(context,
          "live bootstrap observation differs from the manifest");
    }
    bootstrap_matched[(size_t)found] = 1;
  }
  for (index = 0u; index < state->bootstrap_file_count; index++) {
    if (bootstrap_matched[index] == 0) {
      return cupid_contract_set_error(context, "live bootstrap observation is missing");
    }
  }
  {
    byte_slice_t seed_path;
    byte_slice_t seed_source;
    manifest_seed_closure_t seed_closure;
    char seed_digest[65];
    int ok;
    (void)memset(&seed_closure, 0, sizeof(seed_closure));
    if (!cupid_contract_binary_read_slice(context, &reader, &seed_path) ||
        !cupid_contract_binary_read_slice(context, &reader, &seed_source)) {
      return 0;
    }
    manifest_digest_hex(seed_source.bytes, seed_source.size, seed_digest);
    if (!cupid_contract_slice_equals_text(&seed_path, &state->seed_manifest_path) ||
        state->seed_manifest_sha256.size != 64u ||
        memcmp(state->seed_manifest_sha256.bytes, seed_digest, 64u) != 0) {
      return cupid_contract_set_error(context, "live bootstrap seed differs from the manifest");
    }
    ok = manifest_parse_seed_closure(context, seed_source, &seed_closure);
    if (ok &&
        (!cupid_contract_text_equals_literal(&seed_closure.build_plan_sha256,
                            manifest_expected_seed_build_plan_sha256) ||
         !cupid_contract_text_equals_literal(&state->build_plan_sha256,
                            manifest_expected_build_plan_sha256))) {
      ok = cupid_contract_set_error(context, "live bootstrap build plan differs from the manifest");
    }
    if (ok && (!cupid_contract_binary_read_u32(context, &reader, &observation_count) ||
               observation_count != SEED_ARTIFACT_COUNT)) {
      ok = cupid_contract_set_error(context, "request seed artifact observation count differs");
    }
    for (index = 0u; ok && index < SEED_ARTIFACT_COUNT; index++) {
      byte_slice_t name;
      byte_slice_t digest;
      uint32_t kind;
      uint64_t size;
      size_t seed_index;
      int found = -1;
      if (!cupid_contract_binary_read_slice(context, &reader, &name) ||
          !cupid_contract_binary_read_u32(context, &reader, &kind) ||
          !cupid_contract_binary_read_u64(context, &reader, &size) ||
          !cupid_contract_binary_read_slice(context, &reader, &digest)) {
        ok = 0;
        break;
      }
      for (seed_index = 0u; seed_index < SEED_ARTIFACT_COUNT; seed_index++) {
        if (cupid_contract_slice_equals_text(&name, &seed_closure.files[seed_index])) {
          found = (int)seed_index;
          break;
        }
      }
      if (found < 0 || seed_matched[(size_t)found] != 0 || kind != 1u ||
          size != seed_closure.sizes[(size_t)found] ||
          !cupid_contract_slice_equals_text(&digest,
                             &seed_closure.sha256[(size_t)found])) {
        ok = cupid_contract_set_error(context, "live seed artifact differs from the seed manifest");
        break;
      }
      seed_matched[(size_t)found] = 1;
    }
    for (index = 0u; ok && index < SEED_ARTIFACT_COUNT; index++) {
      if (seed_matched[index] == 0) {
        ok = cupid_contract_set_error(context, "live seed artifact observation is missing");
      }
    }
    manifest_seed_closure_release(&seed_closure);
    if (!ok) {
      return 0;
    }
  }
  if (reader.position != reader.size) {
    return cupid_contract_set_error(context, "request has trailing input");
  }
  return 1;
}

static int manifest_author_read_observation(error_context_t *context, binary_reader_t *reader,
                                            byte_slice_t *name,
                                            uint64_t *size,
                                            byte_slice_t *digest) {
  uint32_t kind;
  if (!cupid_contract_binary_read_slice(context, reader, name) ||
      !cupid_contract_binary_read_u32(context, reader, &kind) ||
      !cupid_contract_binary_read_u64(context, reader, size) ||
      !cupid_contract_binary_read_slice(context, reader, digest)) {
    return 0;
  }
  if (kind != 1u || !manifest_slice_sha256_valid(digest)) {
    return cupid_contract_set_error(context, "manifest author observation differs");
  }
  return 1;
}

static int manifest_author_read_artifacts(error_context_t *context, binary_reader_t *reader,
                                          manifest_state_t *state) {
  int seen[MANIFEST_ARTIFACT_COUNT];
  uint32_t count;
  size_t index;
  (void)memset(seen, 0, sizeof(seen));
  if (!cupid_contract_binary_read_u32(context, reader, &count) ||
      count != MANIFEST_ARTIFACT_COUNT) {
    return cupid_contract_set_error(context, "manifest author artifact count differs");
  }
  state->artifact_count = MANIFEST_ARTIFACT_COUNT;
  for (index = 0u; index < MANIFEST_ARTIFACT_COUNT; index++) {
    byte_slice_t name;
    byte_slice_t digest;
    uint64_t size;
    int artifact_index;
    if (!manifest_author_read_observation(context,
            reader, &name, &size, &digest)) {
      return 0;
    }
    artifact_index = manifest_slice_literal_index(
        &name, manifest_artifact_names, MANIFEST_ARTIFACT_COUNT);
    if (artifact_index < 0 || seen[(size_t)artifact_index] != 0) {
      return cupid_contract_set_error(context, "manifest author artifact inventory differs");
    }
    if (state->artifact_total_bytes > ~(uint64_t)0u - size) {
      return cupid_contract_set_error(context, "manifest author artifact byte total overflows");
    }
    if (!manifest_text_copy_slice(context,
            &state->artifacts[(size_t)artifact_index].path, &name) ||
        !manifest_text_copy_slice(context,
            &state->artifacts[(size_t)artifact_index].sha256, &digest)) {
      return 0;
    }
    state->artifacts[(size_t)artifact_index].size = size;
    state->artifact_total_bytes += size;
    seen[(size_t)artifact_index] = 1;
  }
  for (index = 0u; index < MANIFEST_COMPARISON_COUNT; index++) {
    const manifest_artifact_t *artifact = manifest_find_artifact(
        state, manifest_comparison_artifacts[index]);
    byte_slice_t digest;
    if (artifact == (const manifest_artifact_t *)0) {
      return cupid_contract_set_error(context, "manifest author comparison artifact is missing");
    }
    digest.bytes = artifact->sha256.bytes;
    digest.size = artifact->sha256.size;
    if (!manifest_text_copy_slice(context, &state->comparisons[index], &digest)) {
      return 0;
    }
  }
  return 1;
}

static int manifest_author_read_inputs(error_context_t *context, binary_reader_t *reader,
                                       manifest_state_t *state) {
  int seen[MANIFEST_EXPECTED_INPUT_COUNT];
  uint32_t count;
  size_t index;
  (void)memset(seen, 0, sizeof(seen));
  if (!cupid_contract_binary_read_u32(context, reader, &count) ||
      count != MANIFEST_EXPECTED_INPUT_COUNT) {
    return cupid_contract_set_error(context, "manifest author input count differs");
  }
  state->input_count = MANIFEST_EXPECTED_INPUT_COUNT;
  state->declared_input_count = MANIFEST_EXPECTED_INPUT_COUNT;
  for (index = 0u; index < MANIFEST_EXPECTED_INPUT_COUNT; index++) {
    byte_slice_t path;
    byte_slice_t digest;
    uint64_t size;
    int input_index;
    if (!manifest_author_read_observation(context,
            reader, &path, &size, &digest)) {
      return 0;
    }
    input_index = manifest_slice_literal_index(
        &path, manifest_expected_input_paths,
        MANIFEST_EXPECTED_INPUT_COUNT);
    if (input_index < 0 || seen[(size_t)input_index] != 0) {
      return cupid_contract_set_error(context, "manifest author input inventory differs");
    }
    if (!manifest_text_copy_slice(context,
            &state->inputs[(size_t)input_index].path, &path) ||
        !manifest_text_copy_slice(context,
            &state->inputs[(size_t)input_index].sha256, &digest)) {
      return 0;
    }
    state->inputs[(size_t)input_index].size = size;
    seen[(size_t)input_index] = 1;
  }
  return 1;
}

static int manifest_author_read_bootstrap_inputs(error_context_t *context,
    binary_reader_t *reader, manifest_state_t *state) {
  int seen[MANIFEST_EXPECTED_BOOTSTRAP_FILE_COUNT];
  byte_slice_t expected_snapshot;
  char actual_snapshot[65];
  uint32_t count;
  size_t index;
  (void)memset(seen, 0, sizeof(seen));
  if (!cupid_contract_binary_read_u32(context, reader, &count) ||
      count != MANIFEST_EXPECTED_BOOTSTRAP_FILE_COUNT) {
    return cupid_contract_set_error(context, "manifest author bootstrap input count differs");
  }
  state->bootstrap_file_count =
      MANIFEST_EXPECTED_BOOTSTRAP_FILE_COUNT;
  state->declared_bootstrap_file_count =
      MANIFEST_EXPECTED_BOOTSTRAP_FILE_COUNT;
  for (index = 0u; index < MANIFEST_EXPECTED_BOOTSTRAP_FILE_COUNT;
       index++) {
    byte_slice_t path;
    byte_slice_t digest;
    uint64_t size;
    int input_index;
    if (!manifest_author_read_observation(context,
            reader, &path, &size, &digest)) {
      return 0;
    }
    input_index = manifest_slice_literal_index(
        &path, manifest_expected_bootstrap_paths,
        MANIFEST_EXPECTED_BOOTSTRAP_FILE_COUNT);
    if (input_index < 0 || seen[(size_t)input_index] != 0) {
      return cupid_contract_set_error(context, "manifest author bootstrap inventory differs");
    }
    if (!manifest_text_copy_slice(context,
            &state->bootstrap_files[(size_t)input_index].path, &path) ||
        !manifest_text_copy_slice(context,
            &state->bootstrap_files[(size_t)input_index].sha256, &digest)) {
      return 0;
    }
    state->bootstrap_files[(size_t)input_index].size = size;
    seen[(size_t)input_index] = 1;
  }
  if (!cupid_contract_binary_read_slice(context, reader, &expected_snapshot)) {
    return 0;
  }
  if (!manifest_slice_sha256_valid(&expected_snapshot)) {
    return cupid_contract_set_error(context, "manifest author bootstrap snapshot differs");
  }
  if (!manifest_bootstrap_snapshot_digest(context, state, actual_snapshot)) {
    return 0;
  }
  if (memcmp(expected_snapshot.bytes, actual_snapshot, 64u) != 0) {
    return cupid_contract_set_error(context, "manifest author bootstrap snapshot differs");
  }
  return 1;
}

static int manifest_author_read_seed(error_context_t *context, binary_reader_t *reader,
                                     manifest_state_t *state) {
  byte_slice_t seed_path;
  byte_slice_t seed_source;
  manifest_seed_closure_t closure;
  char seed_digest[65];
  int seen[SEED_ARTIFACT_COUNT];
  uint32_t count;
  size_t index;
  int ok = 1;
  (void)memset(&closure, 0, sizeof(closure));
  (void)memset(seen, 0, sizeof(seen));
  if (!cupid_contract_binary_read_slice(context, reader, &seed_path) ||
      !cupid_contract_binary_read_slice(context, reader, &seed_source)) {
    return 0;
  }
  manifest_digest_hex(seed_source.bytes, seed_source.size, seed_digest);
  if (!manifest_slice_equals_literal(
          &seed_path, manifest_expected_seed_path) ||
      memcmp(seed_digest, manifest_expected_seed_manifest_sha256, 64u) != 0 ||
      !manifest_parse_seed_closure(context, seed_source, &closure) ||
      !cupid_contract_text_equals_literal(&closure.build_plan_sha256,
                           manifest_expected_seed_build_plan_sha256)) {
    manifest_seed_closure_release(&closure);
    if (!context->has_error) {
      return cupid_contract_set_error(context, "manifest author seed closure differs");
    }
    return 0;
  }
  if (!cupid_contract_binary_read_u32(context, reader, &count) || count != SEED_ARTIFACT_COUNT) {
    manifest_seed_closure_release(&closure);
    return cupid_contract_set_error(context, "manifest author seed observation count differs");
  }
  for (index = 0u; index < SEED_ARTIFACT_COUNT; index++) {
    byte_slice_t name;
    byte_slice_t digest;
    uint64_t size;
    int seed_index;
    if (!manifest_author_read_observation(context,
            reader, &name, &size, &digest)) {
      ok = 0;
      break;
    }
    seed_index = manifest_slice_literal_index(
        &name, cupid_contract_seed_files, SEED_ARTIFACT_COUNT);
    if (seed_index < 0 || seen[(size_t)seed_index] != 0 ||
        size != closure.sizes[(size_t)seed_index] ||
        !cupid_contract_slice_equals_text(
            &digest, &closure.sha256[(size_t)seed_index])) {
      ok = cupid_contract_set_error(context, "manifest author seed observation differs");
      break;
    }
    seen[(size_t)seed_index] = 1;
  }
  if (ok) {
    byte_slice_t seed_digest_slice = {
        (const unsigned char *)seed_digest,
        64u,
    };
    byte_slice_t build_plan_slice = {
        (const unsigned char *)manifest_expected_build_plan_sha256,
        sizeof(manifest_expected_build_plan_sha256) - 1u,
    };
    ok = manifest_text_copy_slice(context, &state->seed_manifest_path, &seed_path) &&
         manifest_text_copy_slice(context,
             &state->seed_manifest_sha256, &seed_digest_slice) &&
         manifest_text_copy_slice(context,
             &state->build_plan_sha256, &build_plan_slice);
  }
  manifest_seed_closure_release(&closure);
  return ok;
}

static int manifest_author_read_equal_pair(error_context_t *context,
    binary_reader_t *reader, byte_slice_t *name,
    byte_slice_t *stage_four_bytes, char stage_four_digest[65],
    const char *message) {
  byte_slice_t stage_three_bytes;
  char stage_three_digest[65];
  uint32_t stage_three_kind;
  uint32_t stage_four_kind;
  if (!cupid_contract_binary_read_slice(context, reader, name) ||
      !cupid_contract_binary_read_u32(context, reader, &stage_three_kind) ||
      !cupid_contract_binary_read_slice(context, reader, &stage_three_bytes) ||
      !cupid_contract_binary_read_u32(context, reader, &stage_four_kind) ||
      !cupid_contract_binary_read_slice(context, reader, stage_four_bytes)) {
    return 0;
  }
  if (stage_three_kind != 1u || stage_four_kind != 1u ||
      stage_three_bytes.size == 0u ||
      stage_three_bytes.size != stage_four_bytes->size ||
      memcmp(stage_three_bytes.bytes, stage_four_bytes->bytes,
             stage_three_bytes.size) != 0) {
    return cupid_contract_set_error(context, message);
  }
  manifest_digest_hex(stage_three_bytes.bytes, stage_three_bytes.size,
                      stage_three_digest);
  manifest_digest_hex(stage_four_bytes->bytes, stage_four_bytes->size,
                      stage_four_digest);
  if (memcmp(stage_three_digest, stage_four_digest, 64u) != 0) {
    return cupid_contract_set_error(context, message);
  }
  return 1;
}

static int manifest_author_read_object_comparisons(error_context_t *context,
    binary_reader_t *reader, manifest_state_t *state) {
  int seen[MANIFEST_OBJECT_COMPARISON_COUNT];
  uint32_t count;
  size_t index;
  (void)memset(seen, 0, sizeof(seen));
  if (!cupid_contract_binary_read_u32(context, reader, &count) ||
      count != MANIFEST_OBJECT_COMPARISON_COUNT) {
    return cupid_contract_set_error(context, "manifest author object comparison count differs");
  }
  for (index = 0u; index < MANIFEST_OBJECT_COMPARISON_COUNT; index++) {
    byte_slice_t name;
    byte_slice_t stage_four_bytes;
    byte_slice_t digest;
    char stage_four_digest[65];
    int comparison_index;
    if (!manifest_author_read_equal_pair(context,
            reader, &name, &stage_four_bytes, stage_four_digest,
            "manifest author object comparison differs")) {
      return 0;
    }
    comparison_index = manifest_slice_literal_index(
        &name, manifest_object_comparison_names,
        MANIFEST_OBJECT_COMPARISON_COUNT);
    if (comparison_index < 0 || seen[(size_t)comparison_index] != 0) {
      return cupid_contract_set_error(context, "manifest author object comparison differs");
    }
    digest.bytes = (const unsigned char *)stage_four_digest;
    digest.size = 64u;
    if (!manifest_text_copy_slice(context,
            &state->object_comparisons[(size_t)comparison_index].sha256,
            &digest)) {
      return 0;
    }
    state->object_comparisons[(size_t)comparison_index].size =
        (uint64_t)stage_four_bytes.size;
    seen[(size_t)comparison_index] = 1;
  }
  return 1;
}

static int manifest_author_read_executable_comparisons(error_context_t *context,
    binary_reader_t *reader, manifest_state_t *state) {
  int seen[MANIFEST_COMPARISON_COUNT];
  uint32_t count;
  size_t index;
  (void)memset(seen, 0, sizeof(seen));
  if (!cupid_contract_binary_read_u32(context, reader, &count) ||
      count != MANIFEST_COMPARISON_COUNT) {
    return cupid_contract_set_error(context, "manifest author executable comparison count differs");
  }
  for (index = 0u; index < MANIFEST_COMPARISON_COUNT; index++) {
    const manifest_artifact_t *artifact;
    byte_slice_t name;
    byte_slice_t stage_four_bytes;
    byte_slice_t digest;
    char stage_four_digest[65];
    int comparison_index;
    if (!manifest_author_read_equal_pair(context,
            reader, &name, &stage_four_bytes, stage_four_digest,
            "manifest author executable comparison differs")) {
      return 0;
    }
    comparison_index = manifest_slice_literal_index(
        &name, manifest_comparison_names, MANIFEST_COMPARISON_COUNT);
    if (comparison_index < 0 || seen[(size_t)comparison_index] != 0) {
      return cupid_contract_set_error(context, "manifest author executable comparison differs");
    }
    artifact = manifest_find_artifact(
        state, manifest_comparison_artifacts[(size_t)comparison_index]);
    digest.bytes = (const unsigned char *)stage_four_digest;
    digest.size = 64u;
    if (artifact == (const manifest_artifact_t *)0 ||
        artifact->size != (uint64_t)stage_four_bytes.size ||
        !cupid_contract_slice_equals_text(&digest, &artifact->sha256)) {
      return cupid_contract_set_error(context, "manifest author executable evidence differs");
    }
    seen[(size_t)comparison_index] = 1;
  }
  return 1;
}

static int manifest_author_read_fixed_pairs(error_context_t *context,
    binary_reader_t *reader, const char *const *names, size_t expected_count,
    const char *message) {
  int seen[MANIFEST_BOOTSTRAP_OBJECT_COUNT];
  uint32_t count;
  size_t index;
  (void)memset(seen, 0, sizeof(seen));
  if (!cupid_contract_binary_read_u32(context, reader, &count) ||
      (size_t)count != expected_count) {
    return cupid_contract_set_error(context, message);
  }
  for (index = 0u; index < expected_count; index++) {
    byte_slice_t name;
    byte_slice_t stage_four_bytes;
    char stage_four_digest[65];
    int pair_index;
    if (!manifest_author_read_equal_pair(context,
            reader, &name, &stage_four_bytes, stage_four_digest, message)) {
      return 0;
    }
    pair_index = manifest_slice_literal_index(&name, names, expected_count);
    if (pair_index < 0 || seen[(size_t)pair_index] != 0) {
      return cupid_contract_set_error(context, message);
    }
    seen[(size_t)pair_index] = 1;
  }
  return 1;
}

static int manifest_validate_author_request(error_context_t *context, const file_image_t *request,
                                            manifest_state_t *state) {
  binary_reader_t reader = {request->bytes, request->size, 0u};
  (void)memset(state, 0, sizeof(*state));
  if (reader.size < sizeof(manifest_author_request_magic) ||
      memcmp(reader.bytes, manifest_author_request_magic,
             sizeof(manifest_author_request_magic)) != 0) {
    return cupid_contract_set_error(context, "request magic differs from CUPMAN4");
  }
  reader.position = sizeof(manifest_author_request_magic);
  if (!manifest_author_read_artifacts(context, &reader, state) ||
      !manifest_author_read_inputs(context, &reader, state) ||
      !manifest_author_read_bootstrap_inputs(context, &reader, state) ||
      !manifest_author_read_seed(context, &reader, state) ||
      !manifest_author_read_object_comparisons(context, &reader, state) ||
      !manifest_author_read_executable_comparisons(context, &reader, state) ||
      !manifest_author_read_fixed_pairs(context,
          &reader, manifest_bootstrap_object_names,
          MANIFEST_BOOTSTRAP_OBJECT_COUNT,
          "manifest author bootstrap object comparison differs") ||
      !manifest_author_read_fixed_pairs(context,
          &reader, manifest_bootstrap_tool_names,
          MANIFEST_BOOTSTRAP_TOOL_COUNT,
          "manifest author bootstrap tool comparison differs")) {
    return 0;
  }
  if (reader.position != reader.size) {
    return cupid_contract_set_error(context, "manifest author request has trailing input");
  }
  return manifest_expected_inventories_match(context, state);
}

static void manifest_write_text(const text_t *text) {
  if (text->size != 0u) {
    (void)fwrite(text->bytes, 1u, text->size, stdout);
  }
}

static int manifest_write_author_report(error_context_t *context, manifest_state_t *state) {
  char snapshot_digest[65];
  size_t index;
  if (!manifest_bootstrap_snapshot_digest(context, state, snapshot_digest)) {
    return 0;
  }
  (void)printf("{\n  \"artifacts\": [\n");
  for (index = 0u; index < MANIFEST_ARTIFACT_COUNT; index++) {
    const manifest_artifact_t *artifact =
        &state->artifacts[manifest_artifact_output_order[index]];
    (void)printf("    {\n      \"path\": \"");
    manifest_write_text(&artifact->path);
    (void)printf("\",\n      \"sha256\": \"");
    manifest_write_text(&artifact->sha256);
    (void)printf("\",\n      \"size\": %llu\n    }%s\n",
                 (unsigned long long)artifact->size,
                 index + 1u == MANIFEST_ARTIFACT_COUNT ? "" : ",");
  }
  (void)printf("  ],\n  \"bootstrap\": {\n"
               "    \"build_plan_sha256\": \"");
  manifest_write_text(&state->build_plan_sha256);
  (void)printf("\",\n    \"seed_manifest\": {\n      \"path\": \"");
  manifest_write_text(&state->seed_manifest_path);
  (void)printf("\",\n      \"sha256\": \"");
  manifest_write_text(&state->seed_manifest_sha256);
  (void)printf("\"\n    },\n    \"source_inputs\": {\n"
               "      \"count\": %u,\n      \"files\": {\n",
               (unsigned int)state->bootstrap_file_count);
  for (index = 0u; index < state->bootstrap_file_count; index++) {
    const manifest_bootstrap_file_t *file =
        &state->bootstrap_files[index];
    (void)printf("        \"");
    manifest_write_text(&file->path);
    (void)printf("\": {\n          \"sha256\": \"");
    manifest_write_text(&file->sha256);
    (void)printf("\",\n          \"size\": %llu\n        }%s\n",
                 (unsigned long long)file->size,
                 index + 1u == state->bootstrap_file_count ? "" : ",");
  }
  (void)printf("      },\n      \"sha256\": \"%s\"\n"
               "    }\n  },\n  \"comparisons\": {\n",
               snapshot_digest);
  for (index = 0u; index < MANIFEST_COMPARISON_COUNT; index++) {
    size_t comparison_index =
        (size_t)manifest_comparison_output_order[index];
    (void)printf("    \"%s\": \"",
                 manifest_comparison_names[comparison_index]);
    manifest_write_text(&state->comparisons[comparison_index]);
    (void)printf("\"%s\n",
                 index + 1u == MANIFEST_COMPARISON_COUNT ? "" : ",");
  }
  (void)printf("  },\n  \"input_count\": %u,\n  \"inputs\": {\n",
               (unsigned int)state->input_count);
  for (index = 0u; index < state->input_count; index++) {
    const manifest_input_t *input = &state->inputs[index];
    (void)printf("    \"");
    manifest_write_text(&input->path);
    (void)printf("\": {\n      \"sha256\": \"");
    manifest_write_text(&input->sha256);
    (void)printf("\",\n      \"size\": %llu\n    }%s\n",
                 (unsigned long long)input->size,
                 index + 1u == state->input_count ? "" : ",");
  }
  (void)printf("  },\n  \"object_comparisons\": {\n");
  for (index = 0u; index < MANIFEST_OBJECT_COMPARISON_COUNT; index++) {
    size_t comparison_index =
        (size_t)manifest_object_comparison_output_order[index];
    (void)printf("    \"%s\": {\n      \"sha256\": \"",
                 manifest_object_comparison_names[comparison_index]);
    manifest_write_text(
        &state->object_comparisons[comparison_index].sha256);
    (void)printf("\",\n      \"size\": %llu\n    }%s\n",
                 (unsigned long long)
                     state->object_comparisons[comparison_index].size,
                 index + 1u == MANIFEST_OBJECT_COMPARISON_COUNT ? "" : ",");
  }
  (void)printf(
      "  },\n  \"schema\": \"%s\",\n  \"status\": \"pass\",\n"
      "  \"target\": {\n    \"architecture\": \"i386\",\n"
      "    \"entry\": 134512640,\n    \"linkage\": \"static\",\n"
      "    \"operating_system\": \"linux\"\n  },\n"
      "  \"tool_fixed_point\": {\n    \"all_equal\": true,\n"
      "    \"c_objects\": %u,\n    \"compared_generations\": [\n"
      "      \"stage-three\",\n      \"stage-four\"\n    ],\n"
      "    \"startup_objects\": %u,\n    \"tool_images\": %u\n  }\n}\n",
      manifest_schema, (unsigned int)MANIFEST_BOOTSTRAP_C_OBJECT_COUNT,
      (unsigned int)MANIFEST_BOOTSTRAP_STARTUP_OBJECT_COUNT,
      (unsigned int)MANIFEST_BOOTSTRAP_TOOL_COUNT);
  if (fflush(stdout) != 0 || ferror(stdout) != 0) {
    return cupid_contract_set_error(context, "manifest author output could not be written");
  }
  return 1;
}

static int manifest_run_author(error_context_t *context, const char *path) {
  file_image_t first;
  file_image_t second;
  manifest_state_t state;
  int ok;
  (void)memset(&state, 0, sizeof(state));
  context->bytes[0] = '\0';
  context->has_error = 0;
  if (!read_request_file(context, path, &first)) {
    (void)fprintf(stderr, "Cupid Toolchain manifest contract failed: %s\n",
                  context->bytes);
    return 1;
  }
  ok = manifest_validate_author_request(context, &first, &state);
  if (ok) {
    ok = read_request_file(context, path, &second);
    if (ok && (second.size != first.size ||
               memcmp(second.bytes, first.bytes, first.size) != 0)) {
      ok = cupid_contract_set_error(context, "request changed while it was checked");
    }
    file_image_release(&second);
  }
  file_image_release(&first);
  if (ok) {
    ok = manifest_write_author_report(context, &state);
  }
  manifest_state_release(&state);
  if (!ok) {
    (void)fprintf(stderr, "Cupid Toolchain manifest contract failed: %s\n",
                  context->bytes);
    return 1;
  }
  return 0;
}

static int manifest_run_check(error_context_t *context, const char *path) {
  file_image_t first;
  file_image_t second;
  manifest_state_t state;
  int ok;
  (void)memset(&state, 0, sizeof(state));
  context->bytes[0] = '\0';
  context->has_error = 0;
  if (!read_request_file(context, path, &first)) {
    (void)fprintf(stderr, "Cupid Toolchain manifest contract failed: %s\n",
                  context->bytes);
    return 1;
  }
  ok = manifest_validate_request(context, &first, &state);
  if (ok) {
    ok = read_request_file(context, path, &second);
    if (ok && (second.size != first.size ||
               memcmp(second.bytes, first.bytes, first.size) != 0)) {
      ok = cupid_contract_set_error(context, "request changed while it was checked");
    }
    file_image_release(&second);
  }
  file_image_release(&first);
  if (!ok) {
    manifest_state_release(&state);
    (void)fprintf(stderr, "Cupid Toolchain manifest contract failed: %s\n",
                  context->bytes);
    return 1;
  }
  (void)printf("{\"artifact_count\":%u,\"artifact_total_bytes\":%llu,"
               "\"bootstrap_source_input_count\":%u,\"input_count\":%u,"
               "\"schema\":\"%s\"}\n",
               (unsigned int)state.artifact_count,
               (unsigned long long)state.artifact_total_bytes,
               (unsigned int)state.bootstrap_file_count,
               (unsigned int)state.input_count,
               manifest_report_schema);
  manifest_state_release(&state);
  return 0;
}

int main(int argc, char **argv) {
  char diagnostic[512];
  error_context_t error_state = {diagnostic, sizeof(diagnostic), 0};
  error_context_t *context = &error_state;
  if (argc == 3 && strcmp(argv[1], "check") == 0) {
    return manifest_run_check(context, argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "author") == 0) {
    return manifest_run_author(context, argv[2]);
  }
  (void)fprintf(stderr,
                "Cupid Toolchain manifest contract failed: usage: "
                "toolchain-manifest-contract (check|author) REQUEST\n");
  return 2;
}
