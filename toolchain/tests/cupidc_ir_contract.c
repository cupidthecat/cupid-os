#include "ctool.h"
#include "ctool_host.h"
#include "cupidc_frontend.h"
#include "cupidc_ir.h"
#include "cupidc_pp.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char active_helper[] =
    "static ctool_bool cemit_add_overflows(ctool_u32 left, ctool_u32 right) {\n"
    "  return left > 0xffffffffu - right ? CTOOL_TRUE : CTOOL_FALSE;\n"
    "}\n";

static const char active_multiply_overflows[] =
    "static ctool_bool cemit_multiply_overflows(ctool_u32 left,\n"
    "                                            ctool_u32 right) {\n"
    "  return left != 0u && right > 0xffffffffu / left ? CTOOL_TRUE\n"
    "                                                  : CTOOL_FALSE;\n"
    "}\n";

static const char active_power_of_two[] =
    "static ctool_bool cemit_power_of_two(ctool_u32 value) {\n"
    "  return value != 0u && (value & (value - 1u)) == 0u ? CTOOL_TRUE\n"
    "                                                     : CTOOL_FALSE;\n"
    "}\n";

static const char active_bool_valid[] =
    "static ctool_bool cfront_bool_valid(ctool_bool value) {\n"
    "  return value == CTOOL_FALSE || value == CTOOL_TRUE ? CTOOL_TRUE\n"
    "                                                      : CTOOL_FALSE;\n"
    "}\n";

static const char active_branch_fits[] =
    "static ctool_bool asm_branch_fits_i8(ctool_u32 bits) {\n"
    "  return bits <= 0x7fu || bits >= 0xffffff80u ? CTOOL_TRUE : CTOOL_FALSE;\n"
    "}\n";

static const char active_aes_rotw[] =
    "static uint32_t rotw(uint32_t w) { return (w << 8) | (w >> 24); }";

static const char active_simd_cpuid_return[] =
    "    return ((before ^ after) & (1u << 21)) != 0u;";

static const char active_call[] =
    "static uint32_t syscall_getpid(void) { return process_get_current_pid(); }";

static const char active_addition[] =
    "int add2(int x, int y) {\n"
    "    return x + y;\n"
    "}\n";

static const char active_paint_x[] =
    "  return CANVAS_X + (cx - view_x) * zoom_level;";
static const char active_paint_y[] =
    "  return CANVAS_Y + (cy - view_y) * zoom_level;";

static const char active_vga_object[] =
    "static uint32_t last_flip_ms = 0;";
static const char active_vga_wait_object[] =
    "static bool vga_wait_vsync = false;";
static const char active_vga_wait_setter[] =
    "void vga_set_vsync_wait(bool enabled) { vga_wait_vsync = enabled; }";
static const char active_vga_function[] =
    "bool vga_flip_ready(void) {\n"
    "  uint32_t now = timer_get_uptime_ms();\n"
    "  return (now - last_flip_ms) >= 16u;\n"
    "}\n";
static const char active_vga_function_crlf[] =
    "bool vga_flip_ready(void) {\r\n"
    "  uint32_t now = timer_get_uptime_ms();\r\n"
    "  return (now - last_flip_ms) >= 16u;\r\n"
    "}\r\n";
static const char active_bool_type[] =
    "typedef enum { false = 0, true = 1 } bool;";
static const char active_timer_ticks_member[] =
    "    uint64_t ticks;           // Total number of timer ticks since boot";
static const char active_timer_frequency_member[] =
    "    uint32_t frequency;       // Timer frequency in Hz";
static const char active_timer_ms_member[] =
    "    uint32_t ms_per_tick;     // Milliseconds per tick";
static const char active_timer_calibrated_member[] =
    "    bool is_calibrated;       // Whether timer has been calibrated";
static const char active_timer_type_end[] = "} timer_state_t;";

static const char active_timer_state[] =
    "static timer_state_t timer_state = {\n"
    "    .ticks = 0,\n"
    "    .frequency = 0,\n"
    "    .ms_per_tick = 0,\n"
    "    .is_calibrated = false\n"
    "};\n";
static const char active_timer_frequency[] =
    "uint32_t timer_get_frequency(void) {\n"
    "    return timer_state.frequency;\n"
    "}\n";
static const char active_doom_blue_member[] = "    uint32_t b:8;";
static const char active_doom_green_member[] = "    uint32_t g:8;";
static const char active_doom_red_member[] = "    uint32_t r:8;";
static const char active_doom_alpha_member[] = "    uint32_t a:8;";
static const char active_doom_red_read[] = "c.r >> 3";
static const char active_doom_green_read[] = "c.g >> 2";
static const char active_doom_blue_read[] = "c.b >> 3";

static const char local_fixture[] =
    "typedef unsigned int uint32_t;\n"
    "uint32_t timer_get_uptime_ms(void);\n"
    "uint32_t vga_flip_time_probe(uint32_t prior_value) {\n"
    "  uint32_t now = timer_get_uptime_ms();\n"
    "  register uint32_t prior = prior_value;\n"
    "  auto uint32_t unused;\n"
    "  return now + prior;\n"
    "}\n";

static int check_status(ctool_status_t actual, ctool_status_t expected,
                        const char *context) {
  if (actual == expected) {
    return 1;
  }
  (void)fprintf(stderr, "%s: expected %s, got %s\n", context,
                ctool_status_name(expected), ctool_status_name(actual));
  return 0;
}

static int string_equal(ctool_string_t actual, const char *expected) {
  size_t expected_size = strlen(expected);
  return actual.size == (ctool_u32)expected_size &&
                 (expected_size == 0u ||
                  memcmp(actual.data, expected, expected_size) == 0)
             ? 1
             : 0;
}

static ctool_u32 find_binding(const ctool_c_translation_unit_t *unit,
                              const char *name) {
  ctool_u32 index;
  for (index = 0u; index < unit->binding_count; index++) {
    if (string_equal(unit->bindings[index].name, name) != 0) {
      return index;
    }
  }
  return CTOOL_C_AST_NONE;
}

static ctool_u32 find_block_binding(const ctool_c_translation_unit_t *unit,
                                     const char *name) {
  ctool_u32 index;
  for (index = 0u; index < unit->block_binding_count; index++) {
    if (string_equal(unit->block_bindings[index].name, name) != 0) {
      return index;
    }
  }
  return CTOOL_C_AST_NONE;
}

static ctool_u32 find_member(const ctool_c_translation_unit_t *unit,
                             const char *name) {
  ctool_u32 index;
  for (index = 0u; index < unit->graph.member_count; index++) {
    if (string_equal(unit->graph.members[index].name, name) != 0) {
      return index;
    }
  }
  return CTOOL_C_AST_NONE;
}

static int arena_marks_equal(ctool_arena_mark_t left,
                             ctool_arena_mark_t right) {
  return left.owner == right.owner && left.block == right.block &&
                 left.used == right.used && left.generation == right.generation
             ? 1
             : 0;
}

static int open_job(const char *host_root, ctool_host_adapter_t *adapter,
                    ctool_job_config_t *config, ctool_job_t **job_out) {
  ctool_status_t status = ctool_host_adapter_init(adapter, host_root);
  if (!check_status(status, CTOOL_OK, "host adapter init")) {
    return 0;
  }
  *config = ctool_host_job_config(adapter, ctool_default_limits());
  status = ctool_job_open(config, job_out);
  return check_status(status, CTOOL_OK, "job open");
}

static int open_limited_job(const char *host_root,
                            ctool_host_adapter_t *adapter,
                            ctool_job_t **job_out) {
  ctool_job_config_t config;
  ctool_limits_t limits = ctool_default_limits();
  ctool_status_t status = ctool_host_adapter_init(adapter, host_root);
  if (!check_status(status, CTOOL_OK, "limited host adapter init")) {
    return 0;
  }
  limits.arena_block_bytes = 128u;
  limits.arena_bytes = 256u;
  config = ctool_host_job_config(adapter, limits);
  status = ctool_job_open(&config, job_out);
  return check_status(status, CTOOL_OK, "limited job open");
}

static int parse_source_mode(ctool_job_t *job, const char *path,
                             const char *text,
                             ctool_bool gnu_extensions,
                             ctool_c_translation_unit_t *unit_out) {
  ctool_source_t source;
  ctool_c_pp_request_t pp_request;
  ctool_c_pp_result_t tape;
  ctool_c_parse_request_t parse_request;
  ctool_status_t status;
  ctool_u32 diagnostic_count = ctool_job_diagnostic_count(job);
  size_t text_size = strlen(text);

  if (text_size > UINT32_MAX) {
    (void)fprintf(stderr, "%s: source exceeds the contract limit\n", path);
    return 0;
  }
  source.path.text = ctool_string(path);
  source.contents = ctool_bytes(text, (ctool_u32)text_size);
  (void)memset(&pp_request, 0, sizeof(pp_request));
  pp_request.mode = CTOOL_C_PP_MODE_C11;
  pp_request.gnu_extensions = gnu_extensions;
  pp_request.hosted_environment = CTOOL_FALSE;
  (void)memset(&tape, 0xa5, sizeof(tape));
  status = ctool_c_preprocess(job, &source, &pp_request, &tape);
  if (status != CTOOL_OK || tape.tokens == NULL || tape.token_count == 0u ||
      ctool_job_diagnostic_count(job) != diagnostic_count) {
    (void)fprintf(stderr, "%s: preprocessing failed: %s\n", path,
                  ctool_status_name(status));
    (void)ctool_job_render_diagnostics(job);
    return 0;
  }

  (void)memset(&parse_request, 0, sizeof(parse_request));
  parse_request.mode = CTOOL_C_PP_MODE_C11;
  parse_request.gnu_extensions = gnu_extensions;
  (void)memset(unit_out, 0xa5, sizeof(*unit_out));
  status = ctool_c_parse(job, &tape, &parse_request, unit_out);
  if (status != CTOOL_OK ||
      ctool_job_diagnostic_count(job) != diagnostic_count) {
    (void)fprintf(stderr, "%s: parsing failed: %s\n", path,
                  ctool_status_name(status));
    (void)ctool_job_render_diagnostics(job);
    return 0;
  }
  return 1;
}

static int parse_source(ctool_job_t *job, const char *path, const char *text,
                        ctool_c_translation_unit_t *unit_out) {
  return parse_source_mode(job, path, text, CTOOL_FALSE, unit_out);
}

static int active_source_is_unchanged(ctool_job_t *job) {
  ctool_path_t path;
  ctool_source_t source;
  ctool_status_t status;
  path.text = ctool_string("/toolchain/cupidc_emit.c");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active emitter source") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data, active_helper) == NULL ||
      strstr((const char *)source.contents.data,
             active_multiply_overflows) == NULL ||
      strstr((const char *)source.contents.data, active_power_of_two) ==
          NULL) {
    (void)fprintf(stderr, "an active emitter helper changed\n");
    return 0;
  }
  path.text = ctool_string("/toolchain/cupidc_frontend.c");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active frontend source") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data, active_bool_valid) == NULL) {
    (void)fprintf(stderr, "the active bool validator changed\n");
    return 0;
  }
  path.text = ctool_string("/toolchain/cupidasm.c");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active assembler source") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data, active_branch_fits) == NULL) {
    (void)fprintf(stderr, "the active branch-range helper changed\n");
    return 0;
  }
  path.text = ctool_string("/kernel/crypto/aes.c");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active AES source") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data, active_aes_rotw) == NULL) {
    (void)fprintf(stderr, "the active AES word rotation changed\n");
    return 0;
  }
  path.text = ctool_string("/kernel/cpu/simd.c");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active SIMD source") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data,
             active_simd_cpuid_return) == NULL) {
    (void)fprintf(stderr, "the active CPUID toggle expression changed\n");
    return 0;
  }
  path.text = ctool_string("/kernel/core/syscall.c");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active syscall source") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data, active_call) == NULL) {
    (void)fprintf(stderr, "the active getpid wrapper changed\n");
    return 0;
  }
  path.text = ctool_string("/bin/cupidc_test3.cc");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active addition source") ||
      source.contents.data == NULL ||
      (strstr((const char *)source.contents.data, active_addition) == NULL &&
       strstr((const char *)source.contents.data,
              "int add2(int x, int y) {\r\n"
              "    return x + y;\r\n"
              "}\r\n") == NULL)) {
    (void)fprintf(stderr, "the active add2 function changed\n");
    return 0;
  }
  path.text = ctool_string("/bin/paint.cc");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active Paint source") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data, "int CANVAS_X = 56;") ==
          NULL ||
      strstr((const char *)source.contents.data, "int CANVAS_Y = 20;") ==
          NULL ||
      strstr((const char *)source.contents.data, "int zoom_level = 1;") ==
          NULL ||
      strstr((const char *)source.contents.data, "int view_x = 0;") == NULL ||
      strstr((const char *)source.contents.data, "int view_y = 0;") == NULL ||
      strstr((const char *)source.contents.data,
             "int canvas_to_screen_x(int cx) {") == NULL ||
      strstr((const char *)source.contents.data, active_paint_x) == NULL ||
      strstr((const char *)source.contents.data,
             "int canvas_to_screen_y(int cy) {") == NULL ||
      strstr((const char *)source.contents.data, active_paint_y) == NULL) {
    (void)fprintf(stderr, "the active Paint coordinate transforms changed\n");
    return 0;
  }
  path.text = ctool_string("/drivers/vga.c");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active VGA source") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data, active_vga_object) == NULL ||
      strstr((const char *)source.contents.data, active_vga_wait_object) ==
          NULL ||
      strstr((const char *)source.contents.data, active_vga_wait_setter) ==
          NULL ||
      (strstr((const char *)source.contents.data, active_vga_function) ==
           NULL &&
       strstr((const char *)source.contents.data,
              active_vga_function_crlf) == NULL)) {
    (void)fprintf(stderr, "the active VGA flip function changed\n");
    return 0;
  }
  path.text = ctool_string("/kernel/core/types.h");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active bool and timer types") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data, active_bool_type) == NULL ||
      strstr((const char *)source.contents.data, active_timer_ticks_member) ==
          NULL ||
      strstr((const char *)source.contents.data,
             active_timer_frequency_member) == NULL ||
      strstr((const char *)source.contents.data, active_timer_ms_member) ==
          NULL ||
      strstr((const char *)source.contents.data,
             active_timer_calibrated_member) == NULL ||
      strstr((const char *)source.contents.data, active_timer_type_end) ==
          NULL) {
    (void)fprintf(stderr, "the active bool or timer type changed\n");
    return 0;
  }
  path.text = ctool_string("/drivers/timer.c");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active timer source") ||
      source.contents.data == NULL ||
      (strstr((const char *)source.contents.data, active_timer_state) ==
           NULL &&
       strstr((const char *)source.contents.data,
              "static timer_state_t timer_state = {\r\n"
              "    .ticks = 0,\r\n"
              "    .frequency = 0,\r\n"
              "    .ms_per_tick = 0,\r\n"
              "    .is_calibrated = false\r\n"
              "};\r\n") == NULL) ||
      (strstr((const char *)source.contents.data, active_timer_frequency) ==
           NULL &&
       strstr((const char *)source.contents.data,
              "uint32_t timer_get_frequency(void) {\r\n"
              "    return timer_state.frequency;\r\n"
              "}\r\n") == NULL)) {
    (void)fprintf(stderr, "the active timer state or getter changed\n");
    return 0;
  }
  path.text = ctool_string("/kernel/doom/src/i_video.h");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active Doom color type") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data, active_doom_blue_member) ==
          NULL ||
      strstr((const char *)source.contents.data, active_doom_green_member) ==
          NULL ||
      strstr((const char *)source.contents.data, active_doom_red_member) ==
          NULL ||
      strstr((const char *)source.contents.data, active_doom_alpha_member) ==
          NULL) {
    (void)fprintf(stderr, "the active Doom color type changed\n");
    return 0;
  }
  path.text = ctool_string("/kernel/doom/src/i_video.c");
  (void)memset(&source, 0xa5, sizeof(source));
  status = ctool_job_load_source(job, &path, &source);
  if (!check_status(status, CTOOL_OK, "load active Doom color reads") ||
      source.contents.data == NULL ||
      strstr((const char *)source.contents.data, active_doom_red_read) ==
          NULL ||
      strstr((const char *)source.contents.data, active_doom_green_read) ==
          NULL ||
      strstr((const char *)source.contents.data, active_doom_blue_read) ==
          NULL) {
    (void)fprintf(stderr, "the active Doom color reads changed\n");
    return 0;
  }
  return 1;
}

static char *make_active_fixture(void) {
  static const char prefix[] =
      "typedef unsigned int ctool_u32;\n"
      "typedef int ctool_bool;\n"
      "#define CTOOL_FALSE 0\n"
      "#define CTOOL_TRUE 1\n";
  size_t size = sizeof(prefix) - 1u + sizeof(active_helper);
  char *text = (char *)malloc(size);
  if (text != NULL) {
    (void)memcpy(text, prefix, sizeof(prefix) - 1u);
    (void)memcpy(text + sizeof(prefix) - 1u, active_helper,
                 sizeof(active_helper));
  }
  return text;
}

static char *make_logic_fixture(void) {
  static const char prefix[] =
      "typedef unsigned int ctool_u32;\n"
      "typedef int ctool_bool;\n"
      "#define CTOOL_FALSE 0\n"
      "#define CTOOL_TRUE 1\n";
  size_t size = sizeof(prefix) - 1u + sizeof(active_power_of_two);
  char *text = (char *)malloc(size);
  if (text != NULL) {
    (void)memcpy(text, prefix, sizeof(prefix) - 1u);
    (void)memcpy(text + sizeof(prefix) - 1u, active_power_of_two,
                 sizeof(active_power_of_two));
  }
  return text;
}

static char *make_division_fixture(void) {
  static const char prefix[] =
      "typedef unsigned int ctool_u32;\n"
      "typedef int ctool_bool;\n"
      "#define CTOOL_FALSE 0\n"
      "#define CTOOL_TRUE 1\n";
  size_t size = sizeof(prefix) - 1u + sizeof(active_multiply_overflows);
  char *text = (char *)malloc(size);
  if (text != NULL) {
    (void)memcpy(text, prefix, sizeof(prefix) - 1u);
    (void)memcpy(text + sizeof(prefix) - 1u, active_multiply_overflows,
                 sizeof(active_multiply_overflows));
  }
  return text;
}

static char *make_logical_or_fixture(void) {
  static const char prefix[] =
      "typedef int ctool_bool;\n"
      "#define CTOOL_FALSE 0\n"
      "#define CTOOL_TRUE 1\n";
  size_t size = sizeof(prefix) - 1u + sizeof(active_bool_valid);
  char *text = (char *)malloc(size);
  if (text != NULL) {
    (void)memcpy(text, prefix, sizeof(prefix) - 1u);
    (void)memcpy(text + sizeof(prefix) - 1u, active_bool_valid,
                 sizeof(active_bool_valid));
  }
  return text;
}

static char *make_branch_fit_fixture(void) {
  static const char prefix[] =
      "typedef unsigned int ctool_u32;\n"
      "typedef int ctool_bool;\n"
      "#define CTOOL_FALSE 0\n"
      "#define CTOOL_TRUE 1\n";
  size_t size = sizeof(prefix) - 1u + sizeof(active_branch_fits);
  char *text = (char *)malloc(size);
  if (text != NULL) {
    (void)memcpy(text, prefix, sizeof(prefix) - 1u);
    (void)memcpy(text + sizeof(prefix) - 1u, active_branch_fits,
                 sizeof(active_branch_fits));
  }
  return text;
}

static char *make_aes_rotw_fixture(void) {
  static const char prefix[] = "typedef unsigned int uint32_t;\n";
  size_t size = sizeof(prefix) - 1u + sizeof(active_aes_rotw);
  char *text = (char *)malloc(size);
  if (text != NULL) {
    (void)memcpy(text, prefix, sizeof(prefix) - 1u);
    (void)memcpy(text + sizeof(prefix) - 1u, active_aes_rotw,
                 sizeof(active_aes_rotw));
  }
  return text;
}

static char *make_simd_cpuid_fixture(void) {
  static const char prefix[] =
      "typedef unsigned int uint32_t;\n"
      "typedef enum { false = 0, true = 1 } bool;\n"
      "static bool simd_cpuid_changed(uint32_t before, uint32_t after) {\n";
  static const char suffix[] = "\n}\n";
  size_t size = sizeof(prefix) - 1u + sizeof(active_simd_cpuid_return) - 1u +
                sizeof(suffix);
  char *text = (char *)malloc(size);
  if (text != NULL) {
    size_t offset = sizeof(prefix) - 1u;
    (void)memcpy(text, prefix, offset);
    (void)memcpy(text + offset, active_simd_cpuid_return,
                 sizeof(active_simd_cpuid_return) - 1u);
    offset += sizeof(active_simd_cpuid_return) - 1u;
    (void)memcpy(text + offset, suffix, sizeof(suffix));
  }
  return text;
}

static char *make_call_fixture(void) {
  static const char prefix[] =
      "typedef unsigned int uint32_t;\n"
      "uint32_t process_get_current_pid(void);\n";
  static const char suffix[] =
      "\nint external_sum(int left, int right);\n"
      "int forward_sum(int left, int right) {\n"
      "  return external_sum(left, right);\n"
      "}\n"
      "static int local_target(void) { return 9; }\n"
      "int call_local(void) { return local_target(); }\n"
      "extern void external_sink(int value);\n"
      "void call_void(int value) { external_sink(value); }\n";
  size_t size = sizeof(prefix) - 1u + sizeof(active_call) - 1u +
                sizeof(suffix);
  char *text = (char *)malloc(size);
  if (text != NULL) {
    size_t offset = 0u;
    (void)memcpy(text + offset, prefix, sizeof(prefix) - 1u);
    offset += sizeof(prefix) - 1u;
    (void)memcpy(text + offset, active_call, sizeof(active_call) - 1u);
    offset += sizeof(active_call) - 1u;
    (void)memcpy(text + offset, suffix, sizeof(suffix));
  }
  return text;
}

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t size) {
  const unsigned char *bytes = (const unsigned char *)data;
  size_t index;
  for (index = 0u; index < size; index++) {
    hash ^= bytes[index];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static uint64_t unit_fingerprint(const ctool_c_translation_unit_t *unit) {
  uint64_t hash = UINT64_C(1469598103934665603);
  hash = hash_bytes(hash, unit, sizeof(*unit));
  hash = hash_bytes(hash, unit->graph.types,
                    (size_t)unit->graph.type_count *
                        sizeof(*unit->graph.types));
  hash = hash_bytes(hash, unit->graph.members,
                    (size_t)unit->graph.member_count *
                        sizeof(*unit->graph.members));
  hash = hash_bytes(hash, unit->graph.parameter_types,
                    (size_t)unit->graph.parameter_type_count *
                        sizeof(*unit->graph.parameter_types));
  hash = hash_bytes(hash, unit->layout.types,
                    (size_t)unit->layout.type_count *
                        sizeof(*unit->layout.types));
  hash = hash_bytes(hash, unit->layout.members,
                    (size_t)unit->layout.member_count *
                        sizeof(*unit->layout.members));
  hash = hash_bytes(hash, unit->bindings,
                    (size_t)unit->binding_count * sizeof(*unit->bindings));
  hash = hash_bytes(hash, unit->parameters,
                    (size_t)unit->parameter_count * sizeof(*unit->parameters));
  hash = hash_bytes(hash, unit->block_bindings,
                    (size_t)unit->block_binding_count *
                        sizeof(*unit->block_bindings));
  hash = hash_bytes(hash, unit->initializers,
                    (size_t)unit->initializer_count *
                        sizeof(*unit->initializers));
  hash = hash_bytes(hash, unit->initializer_elements,
                    (size_t)unit->initializer_element_count *
                        sizeof(*unit->initializer_elements));
  hash = hash_bytes(hash, unit->function_definitions,
                    (size_t)unit->function_definition_count *
                        sizeof(*unit->function_definitions));
  hash = hash_bytes(hash, unit->statements,
                    (size_t)unit->statement_count * sizeof(*unit->statements));
  hash = hash_bytes(hash, unit->statement_children,
                    (size_t)unit->statement_child_count *
                        sizeof(*unit->statement_children));
  hash = hash_bytes(hash, unit->expressions,
                    (size_t)unit->expression_count *
                        sizeof(*unit->expressions));
  hash = hash_bytes(hash, unit->expression_children,
                    (size_t)unit->expression_child_count *
                        sizeof(*unit->expression_children));
  return hash;
}

static int ir_is_zero(const ctool_c_ir_unit_t *ir) {
  const unsigned char *bytes = (const unsigned char *)ir;
  size_t index;
  for (index = 0u; index < sizeof(*ir); index++) {
    if (bytes[index] != 0u) {
      return 0;
    }
  }
  return 1;
}

static int expect_new_diagnostic(const ctool_job_t *job, ctool_u32 before,
                                 ctool_u32 code, const char *message,
                                 const char *context) {
  const ctool_diagnostic_t *diagnostic;
  if (ctool_job_diagnostic_count(job) != before + 1u) {
    (void)fprintf(stderr, "%s: expected one diagnostic\n", context);
    return 0;
  }
  diagnostic = ctool_job_diagnostic(job, before);
  if (diagnostic == NULL || diagnostic->severity != CTOOL_DIAG_ERROR ||
      diagnostic->code != code ||
      (message != NULL && string_equal(diagnostic->message, message) == 0)) {
    (void)fprintf(stderr, "%s: diagnostic differs\n", context);
    return 0;
  }
  return 1;
}

static int expect_ir_failure(ctool_job_t *job,
                             const ctool_c_translation_unit_t *unit,
                             ctool_status_t expected_status,
                             ctool_u32 expected_code,
                             const char *expected_message,
                             const char *context) {
  ctool_c_ir_unit_t ir;
  ctool_u32 diagnostic_count = ctool_job_diagnostic_count(job);
  ctool_arena_mark_t mark = ctool_arena_mark(ctool_job_arena(job));
  ctool_status_t status;
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, unit, &ir);
  if (!check_status(status, expected_status, context) || ir_is_zero(&ir) == 0 ||
      arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) == 0 ||
      !expect_new_diagnostic(job, diagnostic_count, expected_code,
                             expected_message, context)) {
    (void)fprintf(stderr, "%s: failure transaction differs\n", context);
    return 0;
  }
  return 1;
}

static int instruction_matches(const ctool_c_ir_instruction_t *instruction,
                               ctool_c_ir_instruction_kind_t kind,
                               ctool_u32 type, ctool_u32 input_type,
                               ctool_c_expression_operator_t operation,
                               ctool_c_conversion_kind_t conversion,
                               ctool_u32 reference, ctool_u64 integer_bits) {
  return instruction->kind == kind && instruction->type == type &&
                 instruction->input_type == input_type &&
                 instruction->operation == operation &&
                 instruction->conversion == conversion &&
                 instruction->reference == reference &&
                 instruction->integer_bits == integer_bits &&
                 string_equal(instruction->location.path,
                              "/active-cemit-add-overflows.c") != 0 &&
                 string_equal(instruction->physical_location.path,
                              "/active-cemit-add-overflows.c") != 0
             ? 1
             : 0;
}

static int validate_active_ir(const ctool_c_translation_unit_t *unit,
                              const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 first_parameter;
  ctool_u32 unsigned_type;
  ctool_u32 result_type;
  ctool_u32 index;

  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->functions == NULL || ir->instruction_count != 12u ||
      ir->instructions == NULL) {
    (void)fprintf(stderr, "active IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 2u ||
      unit->parameter_count < 2u ||
      function_type->first_parameter > unit->parameter_count - 2u) {
    (void)fprintf(stderr, "active function type differs\n");
    return 0;
  }
  first_parameter = function_type->first_parameter;
  unsigned_type = unit->parameters[first_parameter].type;
  result_type = function_type->referenced_type;
  function = &ir->functions[0];
  instructions = ir->instructions + function->first_instruction;
  if (function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u || function->instruction_count != 12u ||
      function->maximum_stack_depth != 3u ||
      !string_equal(function->location.path,
                    "/active-cemit-add-overflows.c")) {
    (void)fprintf(stderr, "active IR function record differs\n");
    return 0;
  }
  if (!instruction_matches(
          &instructions[0], CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
          unsigned_type, CTOOL_C_TYPE_NONE,
          CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
          first_parameter, 0u) ||
      !instruction_matches(
          &instructions[1], CTOOL_C_IR_INSTRUCTION_LOAD, unsigned_type,
          unsigned_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u) ||
      !instruction_matches(
          &instructions[2], CTOOL_C_IR_INSTRUCTION_INTEGER, unsigned_type,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, UINT32_MAX) ||
      !instruction_matches(
          &instructions[3], CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
          unsigned_type, CTOOL_C_TYPE_NONE,
          CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
          first_parameter + 1u, 0u) ||
      !instruction_matches(
          &instructions[4], CTOOL_C_IR_INSTRUCTION_LOAD, unsigned_type,
          unsigned_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u) ||
      !instruction_matches(
          &instructions[5], CTOOL_C_IR_INSTRUCTION_BINARY, unsigned_type,
          unsigned_type, CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u) ||
      !instruction_matches(
          &instructions[6], CTOOL_C_IR_INSTRUCTION_BINARY, result_type,
          unsigned_type, CTOOL_C_EXPRESSION_OPERATOR_GREATER,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u) ||
      !instruction_matches(
          &instructions[7], CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO,
          CTOOL_C_TYPE_NONE, result_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, 10u, 0u) ||
      !instruction_matches(
          &instructions[8], CTOOL_C_IR_INSTRUCTION_INTEGER, result_type,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 1u) ||
      !instruction_matches(
          &instructions[9], CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_C_TYPE_NONE,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, 11u, 0u) ||
      !instruction_matches(
          &instructions[10], CTOOL_C_IR_INSTRUCTION_INTEGER, result_type,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u) ||
      !instruction_matches(
          &instructions[11], CTOOL_C_IR_INSTRUCTION_RETURN_VALUE,
          result_type, result_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u)) {
    (void)fprintf(stderr, "active IR instruction stream differs\n");
    return 0;
  }
  for (index = 0u; index < function->instruction_count; index++) {
    if (instructions[index].location.line == 0u ||
        instructions[index].physical_location.line == 0u) {
      (void)fprintf(stderr, "active IR lost source locations\n");
      return 0;
    }
  }
  return 1;
}

static int logic_instruction_matches(
    const ctool_c_ir_instruction_t *instruction,
    ctool_c_ir_instruction_kind_t kind, ctool_u32 type,
    ctool_u32 input_type, ctool_c_expression_operator_t operation,
    ctool_u32 reference, ctool_u64 integer_bits) {
  return instruction->kind == kind && instruction->type == type &&
                 instruction->input_type == input_type &&
                 instruction->operation == operation &&
                 instruction->conversion == CTOOL_C_CONVERSION_NONE &&
                 instruction->reference == reference &&
                 instruction->integer_bits == integer_bits &&
                 string_equal(instruction->location.path,
                              "/active-cemit-power-of-two.c") != 0 &&
                 string_equal(instruction->physical_location.path,
                              "/active-cemit-power-of-two.c") != 0
             ? 1
             : 0;
}

static int validate_logic_ir(const ctool_c_translation_unit_t *unit,
                             const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 parameter;
  ctool_u32 unsigned_type;
  ctool_u32 result_type;
  ctool_u32 index;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->functions == NULL || ir->instruction_count != 23u ||
      ir->instructions == NULL) {
    (void)fprintf(stderr, "logic IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 1u ||
      function_type->first_parameter >= unit->parameter_count) {
    (void)fprintf(stderr, "logic function type differs\n");
    return 0;
  }
  parameter = function_type->first_parameter;
  unsigned_type = unit->parameters[parameter].type;
  result_type = function_type->referenced_type;
  function = &ir->functions[0];
  instructions = ir->instructions + function->first_instruction;
  if (function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count != 23u ||
      function->maximum_stack_depth != 3u ||
      !logic_instruction_matches(
          &instructions[0], CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
          unsigned_type, CTOOL_C_TYPE_NONE,
          CTOOL_C_EXPRESSION_OPERATOR_NONE, parameter, 0u) ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[1].type != unsigned_type ||
      instructions[1].input_type != unsigned_type ||
      instructions[1].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      !logic_instruction_matches(
          &instructions[2], CTOOL_C_IR_INSTRUCTION_INTEGER, unsigned_type,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_AST_NONE, 0u) ||
      !logic_instruction_matches(
          &instructions[3], CTOOL_C_IR_INSTRUCTION_BINARY, result_type,
          unsigned_type, CTOOL_C_EXPRESSION_OPERATOR_NOT_EQUAL,
          CTOOL_C_AST_NONE, 0u) ||
      !logic_instruction_matches(
          &instructions[4], CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO,
          CTOOL_C_TYPE_NONE, result_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          17u, 0u) ||
      !logic_instruction_matches(
          &instructions[5], CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
          unsigned_type, CTOOL_C_TYPE_NONE,
          CTOOL_C_EXPRESSION_OPERATOR_NONE, parameter, 0u) ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[6].type != unsigned_type ||
      instructions[6].input_type != unsigned_type ||
      instructions[6].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      !logic_instruction_matches(
          &instructions[7], CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
          unsigned_type, CTOOL_C_TYPE_NONE,
          CTOOL_C_EXPRESSION_OPERATOR_NONE, parameter, 0u) ||
      instructions[8].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[8].type != unsigned_type ||
      instructions[8].input_type != unsigned_type ||
      instructions[8].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      !logic_instruction_matches(
          &instructions[9], CTOOL_C_IR_INSTRUCTION_INTEGER, unsigned_type,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_AST_NONE, 1u) ||
      !logic_instruction_matches(
          &instructions[10], CTOOL_C_IR_INSTRUCTION_BINARY, unsigned_type,
          unsigned_type, CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT,
          CTOOL_C_AST_NONE, 0u) ||
      !logic_instruction_matches(
          &instructions[11], CTOOL_C_IR_INSTRUCTION_BINARY, unsigned_type,
          unsigned_type, CTOOL_C_EXPRESSION_OPERATOR_BITWISE_AND,
          CTOOL_C_AST_NONE, 0u) ||
      !logic_instruction_matches(
          &instructions[12], CTOOL_C_IR_INSTRUCTION_INTEGER, unsigned_type,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_AST_NONE, 0u) ||
      !logic_instruction_matches(
          &instructions[13], CTOOL_C_IR_INSTRUCTION_BINARY, result_type,
          unsigned_type, CTOOL_C_EXPRESSION_OPERATOR_EQUAL,
          CTOOL_C_AST_NONE, 0u) ||
      !logic_instruction_matches(
          &instructions[14], CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO,
          CTOOL_C_TYPE_NONE, result_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          17u, 0u) ||
      !logic_instruction_matches(
          &instructions[15], CTOOL_C_IR_INSTRUCTION_INTEGER, result_type,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_AST_NONE, 1u) ||
      !logic_instruction_matches(
          &instructions[16], CTOOL_C_IR_INSTRUCTION_JUMP,
          CTOOL_C_TYPE_NONE, CTOOL_C_TYPE_NONE,
          CTOOL_C_EXPRESSION_OPERATOR_NONE, 18u, 0u) ||
      !logic_instruction_matches(
          &instructions[17], CTOOL_C_IR_INSTRUCTION_INTEGER, result_type,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_AST_NONE, 0u) ||
      !logic_instruction_matches(
          &instructions[18], CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO,
          CTOOL_C_TYPE_NONE, result_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          21u, 0u) ||
      !logic_instruction_matches(
          &instructions[19], CTOOL_C_IR_INSTRUCTION_INTEGER, result_type,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_AST_NONE, 1u) ||
      !logic_instruction_matches(
          &instructions[20], CTOOL_C_IR_INSTRUCTION_JUMP,
          CTOOL_C_TYPE_NONE, CTOOL_C_TYPE_NONE,
          CTOOL_C_EXPRESSION_OPERATOR_NONE, 22u, 0u) ||
      !logic_instruction_matches(
          &instructions[21], CTOOL_C_IR_INSTRUCTION_INTEGER, result_type,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_AST_NONE, 0u) ||
      !logic_instruction_matches(
          &instructions[22], CTOOL_C_IR_INSTRUCTION_RETURN_VALUE,
          result_type, result_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_AST_NONE, 0u)) {
    (void)fprintf(stderr, "logic IR instruction stream differs\n");
    return 0;
  }
  for (index = 0u; index < function->instruction_count; index++) {
    if (instructions[index].location.line == 0u ||
        instructions[index].physical_location.line == 0u) {
      (void)fprintf(stderr, "logic IR lost source locations\n");
      return 0;
    }
  }
  return 1;
}

static int division_instruction_matches(
    const ctool_c_ir_instruction_t *instruction,
    ctool_c_ir_instruction_kind_t kind, ctool_u32 type,
    ctool_u32 input_type, ctool_c_expression_operator_t operation,
    ctool_c_conversion_kind_t conversion, ctool_u32 reference,
    ctool_u64 integer_bits) {
  return instruction->kind == kind && instruction->type == type &&
                 instruction->input_type == input_type &&
                 instruction->operation == operation &&
                 instruction->conversion == conversion &&
                 instruction->reference == reference &&
                 instruction->integer_bits == integer_bits &&
                 string_equal(instruction->location.path,
                              "/active-cemit-multiply-overflows.c") != 0 &&
                 string_equal(instruction->physical_location.path,
                              "/active-cemit-multiply-overflows.c") != 0
             ? 1
             : 0;
}

static int validate_division_ir(const ctool_c_translation_unit_t *unit,
                                const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 left_parameter;
  ctool_u32 right_parameter;
  ctool_u32 unsigned_type;
  ctool_u32 result_type;
  ctool_u32 index;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->functions == NULL || ir->instruction_count != 21u ||
      ir->instructions == NULL) {
    (void)fprintf(stderr, "division IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 2u ||
      function_type->first_parameter > unit->parameter_count ||
      function_type->parameter_count >
          unit->parameter_count - function_type->first_parameter) {
    (void)fprintf(stderr, "division function type differs\n");
    return 0;
  }
  left_parameter = function_type->first_parameter;
  right_parameter = left_parameter + 1u;
  unsigned_type = unit->parameters[left_parameter].type;
  result_type = function_type->referenced_type;
  function = &ir->functions[0];
  instructions = ir->instructions + function->first_instruction;
  if (unit->parameters[right_parameter].type != unsigned_type ||
      function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count != 21u ||
      function->maximum_stack_depth != 3u ||
      !division_instruction_matches(
          &instructions[0], CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
          unsigned_type, CTOOL_C_TYPE_NONE,
          CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
          left_parameter, 0u) ||
      !division_instruction_matches(
          &instructions[1], CTOOL_C_IR_INSTRUCTION_LOAD, unsigned_type,
          unsigned_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u) ||
      !division_instruction_matches(
          &instructions[2], CTOOL_C_IR_INSTRUCTION_INTEGER, unsigned_type,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u) ||
      !division_instruction_matches(
          &instructions[3], CTOOL_C_IR_INSTRUCTION_BINARY, result_type,
          unsigned_type, CTOOL_C_EXPRESSION_OPERATOR_NOT_EQUAL,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u) ||
      !division_instruction_matches(
          &instructions[4], CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO,
          CTOOL_C_TYPE_NONE, result_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, 15u, 0u) ||
      !division_instruction_matches(
          &instructions[5], CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
          unsigned_type, CTOOL_C_TYPE_NONE,
          CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
          right_parameter, 0u) ||
      !division_instruction_matches(
          &instructions[6], CTOOL_C_IR_INSTRUCTION_LOAD, unsigned_type,
          unsigned_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u) ||
      !division_instruction_matches(
          &instructions[7], CTOOL_C_IR_INSTRUCTION_INTEGER, unsigned_type,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0xffffffffu) ||
      !division_instruction_matches(
          &instructions[8], CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
          unsigned_type, CTOOL_C_TYPE_NONE,
          CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
          left_parameter, 0u) ||
      !division_instruction_matches(
          &instructions[9], CTOOL_C_IR_INSTRUCTION_LOAD, unsigned_type,
          unsigned_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u) ||
      !division_instruction_matches(
          &instructions[10], CTOOL_C_IR_INSTRUCTION_BINARY, unsigned_type,
          unsigned_type, CTOOL_C_EXPRESSION_OPERATOR_DIVIDE,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u) ||
      !division_instruction_matches(
          &instructions[11], CTOOL_C_IR_INSTRUCTION_BINARY, result_type,
          unsigned_type, CTOOL_C_EXPRESSION_OPERATOR_GREATER,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u) ||
      !division_instruction_matches(
          &instructions[12], CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO,
          CTOOL_C_TYPE_NONE, result_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, 15u, 0u) ||
      !division_instruction_matches(
          &instructions[13], CTOOL_C_IR_INSTRUCTION_INTEGER, result_type,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 1u) ||
      !division_instruction_matches(
          &instructions[14], CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_C_TYPE_NONE,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, 16u, 0u) ||
      !division_instruction_matches(
          &instructions[15], CTOOL_C_IR_INSTRUCTION_INTEGER, result_type,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u) ||
      !division_instruction_matches(
          &instructions[16], CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO,
          CTOOL_C_TYPE_NONE, result_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, 19u, 0u) ||
      !division_instruction_matches(
          &instructions[17], CTOOL_C_IR_INSTRUCTION_INTEGER, result_type,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 1u) ||
      !division_instruction_matches(
          &instructions[18], CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_C_TYPE_NONE,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, 20u, 0u) ||
      !division_instruction_matches(
          &instructions[19], CTOOL_C_IR_INSTRUCTION_INTEGER, result_type,
          CTOOL_C_TYPE_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u) ||
      !division_instruction_matches(
          &instructions[20], CTOOL_C_IR_INSTRUCTION_RETURN_VALUE,
          result_type, result_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u)) {
    (void)fprintf(stderr, "division IR instruction stream differs\n");
    return 0;
  }
  for (index = 0u; index < function->instruction_count; index++) {
    if (instructions[index].location.line == 0u ||
        instructions[index].physical_location.line == 0u) {
      (void)fprintf(stderr, "division IR lost source locations\n");
      return 0;
    }
  }
  return 1;
}

typedef struct {
  ctool_c_ir_instruction_kind_t kind;
  ctool_bool has_type;
  ctool_bool has_input_type;
  ctool_c_expression_operator_t operation;
  ctool_c_conversion_kind_t conversion;
  ctool_u32 reference;
  ctool_u64 integer_bits;
} logical_or_expected_t;

static int validate_logical_or_ir(const ctool_c_translation_unit_t *unit,
                                  const ctool_c_ir_unit_t *ir) {
  const ctool_u32 parameter_reference = CTOOL_C_AST_NONE - 1u;
  static const logical_or_expected_t expected[] = {
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, CTOOL_TRUE, CTOOL_FALSE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE - 1u, 0u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, CTOOL_TRUE, CTOOL_TRUE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, CTOOL_TRUE, CTOOL_FALSE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_BINARY, CTOOL_TRUE, CTOOL_TRUE,
       CTOOL_C_EXPRESSION_OPERATOR_EQUAL, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, CTOOL_FALSE, CTOOL_TRUE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 7u, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, CTOOL_TRUE, CTOOL_FALSE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 1u},
      {CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_FALSE, CTOOL_FALSE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 15u, 0u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, CTOOL_TRUE, CTOOL_FALSE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE - 1u, 0u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, CTOOL_TRUE, CTOOL_TRUE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, CTOOL_TRUE, CTOOL_FALSE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 1u},
      {CTOOL_C_IR_INSTRUCTION_BINARY, CTOOL_TRUE, CTOOL_TRUE,
       CTOOL_C_EXPRESSION_OPERATOR_EQUAL, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, CTOOL_FALSE, CTOOL_TRUE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 14u, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, CTOOL_TRUE, CTOOL_FALSE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 1u},
      {CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_FALSE, CTOOL_FALSE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 15u, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, CTOOL_TRUE, CTOOL_FALSE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, CTOOL_FALSE, CTOOL_TRUE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 18u, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, CTOOL_TRUE, CTOOL_FALSE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 1u},
      {CTOOL_C_IR_INSTRUCTION_JUMP, CTOOL_FALSE, CTOOL_FALSE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 19u, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, CTOOL_TRUE, CTOOL_FALSE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, CTOOL_TRUE, CTOOL_TRUE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u}};
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 parameter;
  ctool_u32 result_type;
  ctool_u32 index;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->functions == NULL ||
      ir->instruction_count !=
          (ctool_u32)(sizeof(expected) / sizeof(expected[0])) ||
      ir->instructions == NULL) {
    (void)fprintf(stderr, "logical-or IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 1u ||
      function_type->first_parameter >= unit->parameter_count) {
    (void)fprintf(stderr, "logical-or function type differs\n");
    return 0;
  }
  parameter = function_type->first_parameter;
  result_type = function_type->referenced_type;
  function = &ir->functions[0];
  instructions = ir->instructions + function->first_instruction;
  if (unit->parameters[parameter].type != result_type ||
      function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count !=
          (ctool_u32)(sizeof(expected) / sizeof(expected[0])) ||
      function->maximum_stack_depth != 2u) {
    (void)fprintf(stderr, "logical-or IR function record differs\n");
    return 0;
  }
  for (index = 0u; index < function->instruction_count; index++) {
    const logical_or_expected_t *wanted = &expected[index];
    const ctool_c_ir_instruction_t *actual = &instructions[index];
    ctool_u32 reference = wanted->reference == parameter_reference
                              ? parameter
                              : wanted->reference;
    if (actual->kind != wanted->kind ||
        actual->type !=
            (wanted->has_type == CTOOL_TRUE ? result_type
                                             : CTOOL_C_TYPE_NONE) ||
        actual->input_type !=
            (wanted->has_input_type == CTOOL_TRUE ? result_type
                                                   : CTOOL_C_TYPE_NONE) ||
        actual->operation != wanted->operation ||
        actual->conversion != wanted->conversion ||
        actual->reference != reference ||
        actual->integer_bits != wanted->integer_bits ||
        string_equal(actual->location.path,
                     "/active-cfront-bool-valid.c") == 0 ||
        string_equal(actual->physical_location.path,
                     "/active-cfront-bool-valid.c") == 0 ||
        actual->location.line == 0u ||
        actual->physical_location.line == 0u) {
      (void)fprintf(stderr, "logical-or IR instruction %u differs\n",
                    index);
      return 0;
    }
  }
  return 1;
}

typedef enum {
  BRANCH_EXPECT_NONE = 0,
  BRANCH_EXPECT_PARAMETER,
  BRANCH_EXPECT_RESULT
} branch_expected_type_t;

typedef struct {
  ctool_c_ir_instruction_kind_t kind;
  branch_expected_type_t type;
  branch_expected_type_t input_type;
  ctool_c_expression_operator_t operation;
  ctool_c_conversion_kind_t conversion;
  ctool_u32 reference;
  ctool_u64 integer_bits;
} branch_expected_t;

static int validate_branch_fit_ir(const ctool_c_translation_unit_t *unit,
                                  const ctool_c_ir_unit_t *ir) {
  const ctool_u32 parameter_reference = CTOOL_C_AST_NONE - 1u;
  static const branch_expected_t expected[] = {
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, BRANCH_EXPECT_PARAMETER,
       BRANCH_EXPECT_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE - 1u, 0u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, BRANCH_EXPECT_PARAMETER,
       BRANCH_EXPECT_PARAMETER, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, BRANCH_EXPECT_PARAMETER,
       BRANCH_EXPECT_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0x7fu},
      {CTOOL_C_IR_INSTRUCTION_BINARY, BRANCH_EXPECT_RESULT,
       BRANCH_EXPECT_PARAMETER, CTOOL_C_EXPRESSION_OPERATOR_LESS_EQUAL,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, BRANCH_EXPECT_NONE,
       BRANCH_EXPECT_RESULT, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, 7u, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, BRANCH_EXPECT_RESULT,
       BRANCH_EXPECT_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 1u},
      {CTOOL_C_IR_INSTRUCTION_JUMP, BRANCH_EXPECT_NONE, BRANCH_EXPECT_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 15u, 0u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, BRANCH_EXPECT_PARAMETER,
       BRANCH_EXPECT_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE - 1u, 0u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, BRANCH_EXPECT_PARAMETER,
       BRANCH_EXPECT_PARAMETER, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, BRANCH_EXPECT_PARAMETER,
       BRANCH_EXPECT_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0xffffff80u},
      {CTOOL_C_IR_INSTRUCTION_BINARY, BRANCH_EXPECT_RESULT,
       BRANCH_EXPECT_PARAMETER, CTOOL_C_EXPRESSION_OPERATOR_GREATER_EQUAL,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, BRANCH_EXPECT_NONE,
       BRANCH_EXPECT_RESULT, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, 14u, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, BRANCH_EXPECT_RESULT,
       BRANCH_EXPECT_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 1u},
      {CTOOL_C_IR_INSTRUCTION_JUMP, BRANCH_EXPECT_NONE, BRANCH_EXPECT_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 15u, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, BRANCH_EXPECT_RESULT,
       BRANCH_EXPECT_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_BRANCH_ZERO, BRANCH_EXPECT_NONE,
       BRANCH_EXPECT_RESULT, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, 18u, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, BRANCH_EXPECT_RESULT,
       BRANCH_EXPECT_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 1u},
      {CTOOL_C_IR_INSTRUCTION_JUMP, BRANCH_EXPECT_NONE, BRANCH_EXPECT_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, 19u, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, BRANCH_EXPECT_RESULT,
       BRANCH_EXPECT_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, BRANCH_EXPECT_RESULT,
       BRANCH_EXPECT_RESULT, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u}};
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  ctool_u32 parameter;
  ctool_u32 parameter_type;
  ctool_u32 result_type;
  ctool_u32 index;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->functions == NULL || ir->instructions == NULL ||
      ir->instruction_count !=
          (ctool_u32)(sizeof(expected) / sizeof(expected[0]))) {
    (void)fprintf(stderr, "branch-range IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 1u ||
      function_type->first_parameter >= unit->parameter_count) {
    (void)fprintf(stderr, "branch-range function type differs\n");
    return 0;
  }
  parameter = function_type->first_parameter;
  parameter_type = unit->parameters[parameter].type;
  result_type = function_type->referenced_type;
  function = &ir->functions[0];
  if (function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count !=
          (ctool_u32)(sizeof(expected) / sizeof(expected[0])) ||
      function->maximum_stack_depth != 2u) {
    (void)fprintf(stderr, "branch-range IR function record differs\n");
    return 0;
  }
  for (index = 0u; index < function->instruction_count; index++) {
    const branch_expected_t *wanted = &expected[index];
    const ctool_c_ir_instruction_t *actual = &ir->instructions[index];
    ctool_u32 wanted_type = wanted->type == BRANCH_EXPECT_PARAMETER
                                ? parameter_type
                                : wanted->type == BRANCH_EXPECT_RESULT
                                      ? result_type
                                      : CTOOL_C_TYPE_NONE;
    ctool_u32 wanted_input = wanted->input_type == BRANCH_EXPECT_PARAMETER
                                 ? parameter_type
                                 : wanted->input_type == BRANCH_EXPECT_RESULT
                                       ? result_type
                                       : CTOOL_C_TYPE_NONE;
    ctool_u32 wanted_reference =
        wanted->reference == parameter_reference ? parameter
                                                  : wanted->reference;
    if (actual->kind != wanted->kind || actual->type != wanted_type ||
        actual->input_type != wanted_input ||
        actual->operation != wanted->operation ||
        actual->conversion != wanted->conversion ||
        actual->reference != wanted_reference ||
        actual->integer_bits != wanted->integer_bits ||
        string_equal(actual->location.path,
                     "/active-asm-branch-fits-i8.c") == 0 ||
        string_equal(actual->physical_location.path,
                     "/active-asm-branch-fits-i8.c") == 0 ||
        actual->location.line == 0u || actual->physical_location.line == 0u) {
      (void)fprintf(stderr, "branch-range IR instruction %u differs\n",
                    index);
      return 0;
    }
  }
  return 1;
}

typedef enum {
  ROTW_EXPECT_NONE = 0,
  ROTW_EXPECT_VALUE,
  ROTW_EXPECT_COUNT
} rotw_expected_type_t;

typedef struct {
  ctool_c_ir_instruction_kind_t kind;
  rotw_expected_type_t type;
  rotw_expected_type_t input_type;
  ctool_c_expression_operator_t operation;
  ctool_c_conversion_kind_t conversion;
  ctool_u32 reference;
  ctool_u64 integer_bits;
} rotw_expected_t;

static int validate_aes_rotw_ir(const ctool_c_translation_unit_t *unit,
                                const ctool_c_ir_unit_t *ir) {
  const ctool_u32 parameter_reference = CTOOL_C_AST_NONE - 1u;
  static const rotw_expected_t expected[] = {
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, ROTW_EXPECT_VALUE,
       ROTW_EXPECT_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE - 1u, 0u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, ROTW_EXPECT_VALUE, ROTW_EXPECT_VALUE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_LVALUE_TO_VALUE,
       CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, ROTW_EXPECT_COUNT, ROTW_EXPECT_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 8u},
      {CTOOL_C_IR_INSTRUCTION_BINARY, ROTW_EXPECT_VALUE, ROTW_EXPECT_VALUE,
       CTOOL_C_EXPRESSION_OPERATOR_SHIFT_LEFT, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, ROTW_EXPECT_VALUE,
       ROTW_EXPECT_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE - 1u, 0u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, ROTW_EXPECT_VALUE, ROTW_EXPECT_VALUE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_LVALUE_TO_VALUE,
       CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, ROTW_EXPECT_COUNT, ROTW_EXPECT_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 24u},
      {CTOOL_C_IR_INSTRUCTION_BINARY, ROTW_EXPECT_VALUE, ROTW_EXPECT_VALUE,
       CTOOL_C_EXPRESSION_OPERATOR_SHIFT_RIGHT, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_BINARY, ROTW_EXPECT_VALUE, ROTW_EXPECT_VALUE,
       CTOOL_C_EXPRESSION_OPERATOR_BITWISE_OR, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, ROTW_EXPECT_VALUE,
       ROTW_EXPECT_VALUE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u}};
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  ctool_u32 parameter;
  ctool_u32 value_type;
  ctool_u32 count_type;
  ctool_u32 index;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->functions == NULL || ir->instructions == NULL ||
      ir->instruction_count !=
          (ctool_u32)(sizeof(expected) / sizeof(expected[0]))) {
    (void)fprintf(stderr, "AES word-rotation IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 1u ||
      function_type->first_parameter >= unit->parameter_count) {
    (void)fprintf(stderr, "AES word-rotation function type differs\n");
    return 0;
  }
  parameter = function_type->first_parameter;
  value_type = unit->parameters[parameter].type;
  count_type = ir->instructions[2].type;
  if (function_type->referenced_type != value_type ||
      value_type >= unit->layout.type_count ||
      count_type >= unit->layout.type_count ||
      unit->layout.types[value_type].is_integer != CTOOL_TRUE ||
      unit->layout.types[value_type].size != 4u ||
      unit->layout.types[value_type].is_signed != CTOOL_FALSE ||
      unit->layout.types[count_type].is_integer != CTOOL_TRUE ||
      unit->layout.types[count_type].size != 4u ||
      unit->layout.types[count_type].is_signed != CTOOL_TRUE ||
      ir->instructions[6].type != count_type) {
    (void)fprintf(stderr, "AES word-rotation operand types differ\n");
    return 0;
  }
  function = &ir->functions[0];
  if (function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count !=
          (ctool_u32)(sizeof(expected) / sizeof(expected[0])) ||
      function->maximum_stack_depth != 3u) {
    (void)fprintf(stderr, "AES word-rotation IR function record differs\n");
    return 0;
  }
  for (index = 0u; index < function->instruction_count; index++) {
    const rotw_expected_t *wanted = &expected[index];
    const ctool_c_ir_instruction_t *actual = &ir->instructions[index];
    ctool_u32 wanted_type = wanted->type == ROTW_EXPECT_VALUE
                                ? value_type
                                : wanted->type == ROTW_EXPECT_COUNT
                                      ? count_type
                                      : CTOOL_C_TYPE_NONE;
    ctool_u32 wanted_input = wanted->input_type == ROTW_EXPECT_VALUE
                                 ? value_type
                                 : wanted->input_type == ROTW_EXPECT_COUNT
                                       ? count_type
                                       : CTOOL_C_TYPE_NONE;
    ctool_u32 wanted_reference =
        wanted->reference == parameter_reference ? parameter
                                                  : wanted->reference;
    if (actual->kind != wanted->kind || actual->type != wanted_type ||
        actual->input_type != wanted_input ||
        actual->operation != wanted->operation ||
        actual->conversion != wanted->conversion ||
        actual->reference != wanted_reference ||
        actual->integer_bits != wanted->integer_bits ||
        string_equal(actual->location.path, "/active-aes-rotw.c") == 0 ||
        string_equal(actual->physical_location.path,
                     "/active-aes-rotw.c") == 0 ||
        actual->location.line == 0u || actual->physical_location.line == 0u) {
      (void)fprintf(stderr, "AES word-rotation IR instruction %u differs\n",
                    index);
      return 0;
    }
  }
  return 1;
}

typedef enum {
  CPUID_EXPECT_NONE = 0,
  CPUID_EXPECT_VALUE,
  CPUID_EXPECT_COUNT,
  CPUID_EXPECT_COMPARISON,
  CPUID_EXPECT_RESULT
} cpuid_expected_type_t;

typedef struct {
  ctool_c_ir_instruction_kind_t kind;
  cpuid_expected_type_t type;
  cpuid_expected_type_t input_type;
  ctool_c_expression_operator_t operation;
  ctool_c_conversion_kind_t conversion;
  ctool_u32 reference;
  ctool_u64 integer_bits;
} cpuid_expected_t;

static ctool_u32 cpuid_expected_type(cpuid_expected_type_t expected,
                                     ctool_u32 value_type,
                                     ctool_u32 count_type,
                                     ctool_u32 comparison_type,
                                     ctool_u32 result_type) {
  if (expected == CPUID_EXPECT_VALUE) {
    return value_type;
  }
  if (expected == CPUID_EXPECT_COUNT) {
    return count_type;
  }
  if (expected == CPUID_EXPECT_COMPARISON) {
    return comparison_type;
  }
  if (expected == CPUID_EXPECT_RESULT) {
    return result_type;
  }
  return CTOOL_C_TYPE_NONE;
}

static int validate_simd_cpuid_ir(const ctool_c_translation_unit_t *unit,
                                  const ctool_c_ir_unit_t *ir) {
  const ctool_u32 first_parameter_reference = CTOOL_C_AST_NONE - 1u;
  const ctool_u32 second_parameter_reference = CTOOL_C_AST_NONE - 2u;
  static const cpuid_expected_t expected[] = {
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, CPUID_EXPECT_VALUE,
       CPUID_EXPECT_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE - 1u, 0u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, CPUID_EXPECT_VALUE, CPUID_EXPECT_VALUE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_LVALUE_TO_VALUE,
       CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS, CPUID_EXPECT_VALUE,
       CPUID_EXPECT_NONE, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE - 2u, 0u},
      {CTOOL_C_IR_INSTRUCTION_LOAD, CPUID_EXPECT_VALUE, CPUID_EXPECT_VALUE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_LVALUE_TO_VALUE,
       CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_BINARY, CPUID_EXPECT_VALUE, CPUID_EXPECT_VALUE,
       CTOOL_C_EXPRESSION_OPERATOR_BITWISE_XOR, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, CPUID_EXPECT_VALUE, CPUID_EXPECT_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 1u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, CPUID_EXPECT_COUNT, CPUID_EXPECT_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 21u},
      {CTOOL_C_IR_INSTRUCTION_BINARY, CPUID_EXPECT_VALUE, CPUID_EXPECT_VALUE,
       CTOOL_C_EXPRESSION_OPERATOR_SHIFT_LEFT, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_BINARY, CPUID_EXPECT_VALUE, CPUID_EXPECT_VALUE,
       CTOOL_C_EXPRESSION_OPERATOR_BITWISE_AND, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_INTEGER, CPUID_EXPECT_VALUE, CPUID_EXPECT_NONE,
       CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
       CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_BINARY, CPUID_EXPECT_COMPARISON,
       CPUID_EXPECT_VALUE, CTOOL_C_EXPRESSION_OPERATOR_NOT_EQUAL,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_CONVERT, CPUID_EXPECT_RESULT,
       CPUID_EXPECT_COMPARISON, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_ASSIGNMENT, CTOOL_C_AST_NONE, 0u},
      {CTOOL_C_IR_INSTRUCTION_RETURN_VALUE, CPUID_EXPECT_RESULT,
       CPUID_EXPECT_RESULT, CTOOL_C_EXPRESSION_OPERATOR_NONE,
       CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE, 0u}};
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  ctool_u32 first_parameter;
  ctool_u32 second_parameter;
  ctool_u32 value_type;
  ctool_u32 count_type;
  ctool_u32 comparison_type;
  ctool_u32 result_type;
  ctool_u32 index;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->functions == NULL || ir->instructions == NULL ||
      ir->instruction_count !=
          (ctool_u32)(sizeof(expected) / sizeof(expected[0]))) {
    (void)fprintf(stderr, "CPUID toggle IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 2u ||
      function_type->first_parameter + 1u >= unit->parameter_count) {
    (void)fprintf(stderr, "CPUID toggle function type differs\n");
    return 0;
  }
  first_parameter = function_type->first_parameter;
  second_parameter = first_parameter + 1u;
  value_type = unit->parameters[first_parameter].type;
  count_type = ir->instructions[6].type;
  comparison_type = ir->instructions[10].type;
  result_type = function_type->referenced_type;
  if (unit->parameters[second_parameter].type != value_type ||
      value_type >= unit->layout.type_count ||
      count_type >= unit->layout.type_count ||
      comparison_type >= unit->layout.type_count ||
      result_type >= unit->layout.type_count ||
      unit->layout.types[value_type].is_integer != CTOOL_TRUE ||
      unit->layout.types[value_type].size != 4u ||
      unit->layout.types[value_type].is_signed != CTOOL_FALSE ||
      unit->layout.types[count_type].is_integer != CTOOL_TRUE ||
      unit->layout.types[count_type].size != 4u ||
      unit->layout.types[count_type].is_signed != CTOOL_TRUE ||
      unit->layout.types[comparison_type].is_integer != CTOOL_TRUE ||
      unit->layout.types[comparison_type].size != 4u ||
      unit->layout.types[comparison_type].is_signed != CTOOL_TRUE ||
      unit->layout.types[result_type].is_integer != CTOOL_TRUE ||
      unit->layout.types[result_type].size != 4u ||
      result_type == comparison_type) {
    (void)fprintf(stderr, "CPUID toggle operand types differ\n");
    return 0;
  }
  function = &ir->functions[0];
  if (function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count !=
          (ctool_u32)(sizeof(expected) / sizeof(expected[0])) ||
      function->maximum_stack_depth != 3u) {
    (void)fprintf(stderr, "CPUID toggle IR function record differs\n");
    return 0;
  }
  for (index = 0u; index < function->instruction_count; index++) {
    const cpuid_expected_t *wanted = &expected[index];
    const ctool_c_ir_instruction_t *actual = &ir->instructions[index];
    ctool_u32 wanted_type = cpuid_expected_type(
        wanted->type, value_type, count_type, comparison_type, result_type);
    ctool_u32 wanted_input = cpuid_expected_type(
        wanted->input_type, value_type, count_type, comparison_type,
        result_type);
    ctool_u32 wanted_reference = wanted->reference;
    if (wanted_reference == first_parameter_reference) {
      wanted_reference = first_parameter;
    } else if (wanted_reference == second_parameter_reference) {
      wanted_reference = second_parameter;
    }
    if (actual->kind != wanted->kind || actual->type != wanted_type ||
        actual->input_type != wanted_input ||
        actual->operation != wanted->operation ||
        actual->conversion != wanted->conversion ||
        actual->reference != wanted_reference ||
        actual->integer_bits != wanted->integer_bits ||
        string_equal(actual->location.path, "/active-simd-cpuid.c") == 0 ||
        string_equal(actual->physical_location.path,
                     "/active-simd-cpuid.c") == 0 ||
        actual->location.line == 0u || actual->physical_location.line == 0u) {
      (void)fprintf(stderr, "CPUID toggle IR instruction %u differs\n",
                    index);
      return 0;
    }
  }
  return 1;
}

static int validate_simple_ir(const ctool_c_translation_unit_t *unit,
                              const ctool_c_ir_unit_t *ir) {
  const ctool_c_ir_function_t *answer;
  const ctool_c_ir_function_t *idle;
  const ctool_c_ir_function_t *as_unsigned;
  const ctool_c_function_definition_t *conversion_definition;
  const ctool_c_type_node_t *conversion_type;
  const ctool_c_ir_instruction_t *conversion;
  ctool_u32 parameter_type;
  ctool_u32 result_type;
  if (unit->function_definition_count != 3u || ir->function_count != 3u ||
      ir->instruction_count != 7u) {
    (void)fprintf(stderr, "simple IR inventory differs\n");
    return 0;
  }
  answer = &ir->functions[0];
  idle = &ir->functions[1];
  as_unsigned = &ir->functions[2];
  conversion_definition = &unit->function_definitions[2];
  if (conversion_definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  conversion_type = &unit->graph.types[conversion_definition->declared_type];
  if (conversion_type->kind != CTOOL_C_TYPE_FUNCTION ||
      conversion_type->parameter_count != 1u ||
      conversion_type->first_parameter >= unit->parameter_count) {
    return 0;
  }
  parameter_type = unit->parameters[conversion_type->first_parameter].type;
  result_type = conversion_type->referenced_type;
  conversion = &ir->instructions[as_unsigned->first_instruction];
  if (answer->instruction_count != 2u || answer->maximum_stack_depth != 1u ||
      ir->instructions[answer->first_instruction].kind !=
          CTOOL_C_IR_INSTRUCTION_INTEGER ||
      ir->instructions[answer->first_instruction].integer_bits != 42u ||
      ir->instructions[answer->first_instruction + 1u].kind !=
          CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      idle->instruction_count != 1u || idle->maximum_stack_depth != 0u ||
      ir->instructions[idle->first_instruction].kind !=
          CTOOL_C_IR_INSTRUCTION_RETURN_VOID ||
      as_unsigned->instruction_count != 4u ||
      as_unsigned->maximum_stack_depth != 1u ||
      conversion[0].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      conversion[0].type != parameter_type ||
      conversion[0].reference != conversion_type->first_parameter ||
      conversion[1].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      conversion[1].type != parameter_type ||
      conversion[1].input_type != parameter_type ||
      conversion[1].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      conversion[2].kind != CTOOL_C_IR_INSTRUCTION_CONVERT ||
      conversion[2].type != result_type ||
      conversion[2].input_type != parameter_type ||
      conversion[2].conversion != CTOOL_C_CONVERSION_ASSIGNMENT ||
      conversion[3].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      conversion[3].type != result_type ||
      conversion[3].input_type != result_type) {
    (void)fprintf(stderr, "simple IR instructions differ\n");
    return 0;
  }
  return 1;
}

static int validate_addition_ir(const ctool_c_translation_unit_t *unit,
                                const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 first_parameter;
  ctool_u32 value_type;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 6u || ir->functions == NULL ||
      ir->instructions == NULL) {
    (void)fprintf(stderr, "addition IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 2u ||
      unit->parameter_count < 2u ||
      function_type->first_parameter > unit->parameter_count - 2u) {
    (void)fprintf(stderr, "addition function type differs\n");
    return 0;
  }
  first_parameter = function_type->first_parameter;
  value_type = unit->parameters[first_parameter].type;
  function = &ir->functions[0];
  instructions = ir->instructions + function->first_instruction;
  if (function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count != 6u ||
      function->maximum_stack_depth != 2u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[0].type != value_type ||
      instructions[0].reference != first_parameter ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[1].type != value_type ||
      instructions[1].input_type != value_type ||
      instructions[1].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[2].type != value_type ||
      instructions[2].reference != first_parameter + 1u ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[3].type != value_type ||
      instructions[3].input_type != value_type ||
      instructions[3].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_BINARY ||
      instructions[4].type != value_type ||
      instructions[4].input_type != value_type ||
      instructions[4].operation != CTOOL_C_EXPRESSION_OPERATOR_ADD ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[5].type != value_type ||
      instructions[5].input_type != value_type ||
      !string_equal(instructions[4].location.path,
                    "/active-cupidc-add2.c") ||
      !string_equal(instructions[4].physical_location.path,
                    "/active-cupidc-add2.c")) {
    (void)fprintf(stderr, "addition IR instruction stream differs\n");
    return 0;
  }
  return 1;
}

static int paint_instruction_matches(
    const ctool_c_ir_instruction_t *instruction,
    ctool_c_ir_instruction_kind_t kind, ctool_u32 type,
    ctool_u32 input_type, ctool_c_expression_operator_t operation,
    ctool_c_conversion_kind_t conversion, ctool_u32 reference) {
  return instruction->kind == kind && instruction->type == type &&
                 instruction->input_type == input_type &&
                 instruction->operation == operation &&
                 instruction->conversion == conversion &&
                 instruction->reference == reference &&
                 instruction->integer_bits == 0u &&
                 string_equal(instruction->location.path,
                              "/active-paint-multiplication.c") != 0 &&
                 string_equal(instruction->physical_location.path,
                              "/active-paint-multiplication.c") != 0
             ? 1
             : 0;
}

static int validate_paint_multiplication_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  static const char *function_names[] = {"canvas_to_screen_x",
                                         "canvas_to_screen_y"};
  static const char *canvas_names[] = {"CANVAS_X", "CANVAS_Y"};
  static const char *view_names[] = {"view_x", "view_y"};
  ctool_u32 zoom = find_binding(unit, "zoom_level");
  ctool_u32 function_index;
  if (unit->function_definition_count != 2u || ir->function_count != 2u ||
      ir->functions == NULL || ir->instruction_count != 24u ||
      ir->instructions == NULL || zoom == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr, "Paint multiplication IR inventory differs\n");
    return 0;
  }
  for (function_index = 0u; function_index < 2u; function_index++) {
    const ctool_c_function_definition_t *definition =
        &unit->function_definitions[function_index];
    const ctool_c_ir_function_t *function = &ir->functions[function_index];
    const ctool_c_ir_instruction_t *instructions =
        ir->instructions + function->first_instruction;
    const ctool_c_type_node_t *function_type;
    ctool_u32 function_binding =
        find_binding(unit, function_names[function_index]);
    ctool_u32 canvas = find_binding(unit, canvas_names[function_index]);
    ctool_u32 view = find_binding(unit, view_names[function_index]);
    ctool_u32 parameter;
    ctool_u32 integer_type;
    if (function_binding == CTOOL_C_AST_NONE || canvas == CTOOL_C_AST_NONE ||
        view == CTOOL_C_AST_NONE ||
        definition->declared_type >= unit->graph.type_count) {
      return 0;
    }
    function_type = &unit->graph.types[definition->declared_type];
    if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
        function_type->parameter_count != 1u ||
        function_type->first_parameter >= unit->parameter_count) {
      return 0;
    }
    parameter = function_type->first_parameter;
    integer_type = unit->parameters[parameter].type;
    if (function->binding != function_binding ||
        definition->binding != function_binding ||
        function->declared_type != definition->declared_type ||
        function->first_instruction != function_index * 12u ||
        function->instruction_count != 12u ||
        function->maximum_stack_depth != 3u ||
        unit->bindings[canvas].type != integer_type ||
        unit->bindings[view].type != integer_type ||
        unit->bindings[zoom].type != integer_type ||
        function_type->referenced_type != integer_type ||
        !paint_instruction_matches(
            &instructions[0], CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS,
            integer_type, CTOOL_C_TYPE_NONE,
            CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
            canvas) ||
        !paint_instruction_matches(
            &instructions[1], CTOOL_C_IR_INSTRUCTION_LOAD, integer_type,
            integer_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
            CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE) ||
        !paint_instruction_matches(
            &instructions[2], CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
            integer_type, CTOOL_C_TYPE_NONE,
            CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE,
            parameter) ||
        !paint_instruction_matches(
            &instructions[3], CTOOL_C_IR_INSTRUCTION_LOAD, integer_type,
            integer_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
            CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE) ||
        !paint_instruction_matches(
            &instructions[4], CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS,
            integer_type, CTOOL_C_TYPE_NONE,
            CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, view) ||
        !paint_instruction_matches(
            &instructions[5], CTOOL_C_IR_INSTRUCTION_LOAD, integer_type,
            integer_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
            CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE) ||
        !paint_instruction_matches(
            &instructions[6], CTOOL_C_IR_INSTRUCTION_BINARY, integer_type,
            integer_type, CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT,
            CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE) ||
        !paint_instruction_matches(
            &instructions[7], CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS,
            integer_type, CTOOL_C_TYPE_NONE,
            CTOOL_C_EXPRESSION_OPERATOR_NONE, CTOOL_C_CONVERSION_NONE, zoom) ||
        !paint_instruction_matches(
            &instructions[8], CTOOL_C_IR_INSTRUCTION_LOAD, integer_type,
            integer_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
            CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE) ||
        !paint_instruction_matches(
            &instructions[9], CTOOL_C_IR_INSTRUCTION_BINARY, integer_type,
            integer_type, CTOOL_C_EXPRESSION_OPERATOR_MULTIPLY,
            CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE) ||
        !paint_instruction_matches(
            &instructions[10], CTOOL_C_IR_INSTRUCTION_BINARY, integer_type,
            integer_type, CTOOL_C_EXPRESSION_OPERATOR_ADD,
            CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE) ||
        !paint_instruction_matches(
            &instructions[11], CTOOL_C_IR_INSTRUCTION_RETURN_VALUE,
            integer_type, integer_type, CTOOL_C_EXPRESSION_OPERATOR_NONE,
            CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE)) {
      (void)fprintf(stderr,
                    "Paint multiplication IR differs for function %lu\n",
                    (unsigned long)function_index);
      return 0;
    }
  }
  return 1;
}

static int validate_unsigned_multiplication_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 function_binding = find_binding(unit, "multiply_unsigned");
  ctool_u32 parameter;
  ctool_u32 unsigned_type;
  ctool_u32 index;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 5u || ir->functions == NULL ||
      ir->instructions == NULL || function_binding == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr, "unsigned multiplication IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 1u ||
      function_type->first_parameter >= unit->parameter_count) {
    return 0;
  }
  parameter = function_type->first_parameter;
  unsigned_type = unit->parameters[parameter].type;
  function = &ir->functions[0];
  instructions = ir->instructions;
  if (unsigned_type >= unit->layout.type_count ||
      unit->layout.types[unsigned_type].is_integer == CTOOL_FALSE ||
      unit->layout.types[unsigned_type].is_signed != CTOOL_FALSE ||
      unit->layout.types[unsigned_type].size != 4u ||
      function_type->referenced_type != unsigned_type ||
      definition->binding != function_binding ||
      function->binding != function_binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count != 5u ||
      function->maximum_stack_depth != 2u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[0].type != unsigned_type ||
      instructions[0].input_type != CTOOL_C_TYPE_NONE ||
      instructions[0].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instructions[0].conversion != CTOOL_C_CONVERSION_NONE ||
      instructions[0].reference != parameter ||
      instructions[0].integer_bits != 0u ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[1].type != unsigned_type ||
      instructions[1].input_type != unsigned_type ||
      instructions[1].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instructions[1].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      instructions[1].reference != CTOOL_C_AST_NONE ||
      instructions[1].integer_bits != 0u ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[2].type != unsigned_type ||
      instructions[2].input_type != CTOOL_C_TYPE_NONE ||
      instructions[2].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instructions[2].conversion != CTOOL_C_CONVERSION_NONE ||
      instructions[2].reference != CTOOL_C_AST_NONE ||
      instructions[2].integer_bits != 0x80000001u ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_BINARY ||
      instructions[3].type != unsigned_type ||
      instructions[3].input_type != unsigned_type ||
      instructions[3].operation != CTOOL_C_EXPRESSION_OPERATOR_MULTIPLY ||
      instructions[3].conversion != CTOOL_C_CONVERSION_NONE ||
      instructions[3].reference != CTOOL_C_AST_NONE ||
      instructions[3].integer_bits != 0u ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[4].type != unsigned_type ||
      instructions[4].input_type != unsigned_type ||
      instructions[4].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instructions[4].conversion != CTOOL_C_CONVERSION_NONE ||
      instructions[4].reference != CTOOL_C_AST_NONE ||
      instructions[4].integer_bits != 0u) {
    (void)fprintf(stderr, "unsigned multiplication IR stream differs\n");
    return 0;
  }
  for (index = 0u; index < 5u; index++) {
    if (!string_equal(instructions[index].location.path,
                      "/unsigned-multiplication.c") ||
        !string_equal(instructions[index].physical_location.path,
                      "/unsigned-multiplication.c")) {
      (void)fprintf(stderr,
                    "unsigned multiplication IR source path differs\n");
      return 0;
    }
  }
  return 1;
}

static int validate_file_assignment_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 function_binding = find_binding(unit, "vga_set_vsync_wait");
  ctool_u32 object_binding = find_binding(unit, "vga_wait_vsync");
  ctool_u32 parameter;
  ctool_u32 value_type;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 6u || ir->functions == NULL ||
      ir->instructions == NULL || function_binding == CTOOL_C_AST_NONE ||
      object_binding == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr, "file assignment IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 1u ||
      function_type->first_parameter >= unit->parameter_count) {
    return 0;
  }
  parameter = function_type->first_parameter;
  value_type = unit->parameters[parameter].type;
  function = &ir->functions[0];
  instructions = ir->instructions;
  if (function_type->referenced_type >= unit->graph.type_count ||
      unit->graph.types[function_type->referenced_type].kind !=
          CTOOL_C_TYPE_VOID ||
      unit->bindings[object_binding].type != value_type ||
      definition->binding != function_binding ||
      function->binding != function_binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count != 6u ||
      function->maximum_stack_depth != 2u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS ||
      instructions[0].type != value_type ||
      instructions[0].input_type != CTOOL_C_TYPE_NONE ||
      instructions[0].reference != object_binding ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[1].type != value_type ||
      instructions[1].input_type != CTOOL_C_TYPE_NONE ||
      instructions[1].reference != parameter ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[2].type != value_type ||
      instructions[2].input_type != value_type ||
      instructions[2].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_STORE_VALUE ||
      instructions[3].type != value_type ||
      instructions[3].input_type != value_type ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_DISCARD ||
      instructions[4].type != CTOOL_C_TYPE_NONE ||
      instructions[4].input_type != value_type ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID ||
      !string_equal(instructions[3].location.path,
                    "/active-vga-file-assignment.c") ||
      !string_equal(instructions[4].physical_location.path,
                    "/active-vga-file-assignment.c")) {
    (void)fprintf(stderr, "file assignment IR instruction stream differs\n");
    return 0;
  }
  return 1;
}

static int validate_discard_ir(const ctool_c_translation_unit_t *unit,
                               const ctool_c_ir_unit_t *ir) {
  const ctool_c_ir_instruction_t *instructions = ir->instructions;
  ctool_u32 value_type;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 3u || ir->functions == NULL ||
      instructions == NULL || ir->functions[0].first_instruction != 0u ||
      ir->functions[0].instruction_count != 3u ||
      ir->functions[0].maximum_stack_depth != 1u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_INTEGER) {
    (void)fprintf(stderr, "discard IR inventory differs\n");
    return 0;
  }
  value_type = instructions[0].type;
  if (instructions[0].integer_bits != 1u ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_DISCARD ||
      instructions[1].type != CTOOL_C_TYPE_NONE ||
      instructions[1].input_type != value_type ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID) {
    (void)fprintf(stderr, "discard IR instruction stream differs\n");
    return 0;
  }
  return 1;
}

static int validate_chained_assignment_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions = ir->instructions;
  ctool_u32 function_binding = find_binding(unit, "set_both");
  ctool_u32 first = find_binding(unit, "first_state");
  ctool_u32 second = find_binding(unit, "second_state");
  ctool_u32 parameter;
  ctool_u32 value_type;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 7u || ir->functions == NULL ||
      instructions == NULL || function_binding == CTOOL_C_AST_NONE ||
      first == CTOOL_C_AST_NONE || second == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr, "chained assignment IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 1u ||
      function_type->first_parameter >= unit->parameter_count) {
    return 0;
  }
  parameter = function_type->first_parameter;
  value_type = unit->parameters[parameter].type;
  function = &ir->functions[0];
  if (function_type->referenced_type != value_type ||
      unit->bindings[first].type != value_type ||
      unit->bindings[second].type != value_type ||
      function->binding != function_binding ||
      definition->binding != function_binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count != 7u ||
      function->maximum_stack_depth != 3u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS ||
      instructions[0].type != value_type ||
      instructions[0].reference != first ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS ||
      instructions[1].type != value_type ||
      instructions[1].reference != second ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[2].type != value_type ||
      instructions[2].reference != parameter ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[3].type != value_type ||
      instructions[3].input_type != value_type ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_STORE_VALUE ||
      instructions[4].type != value_type ||
      instructions[4].input_type != value_type ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_STORE_VALUE ||
      instructions[5].type != value_type ||
      instructions[5].input_type != value_type ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[6].type != value_type ||
      instructions[6].input_type != value_type ||
      !string_equal(instructions[4].location.path,
                    "/chained-assignment.c") ||
      !string_equal(instructions[5].physical_location.path,
                    "/chained-assignment.c")) {
    (void)fprintf(stderr, "chained assignment IR instruction stream differs\n");
    return 0;
  }
  return 1;
}

static int validate_local_parameter_assignment_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_type_node_t *local_type;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions = ir->instructions;
  ctool_u32 function_binding = find_binding(unit, "assign_local_parameter");
  ctool_u32 local = find_block_binding(unit, "local");
  ctool_u32 parameter;
  ctool_u32 value_type;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 8u || ir->functions == NULL ||
      instructions == NULL || function_binding == CTOOL_C_AST_NONE ||
      local == CTOOL_C_AST_NONE || local >= unit->block_binding_count) {
    (void)fprintf(stderr, "local/parameter assignment IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->parameter_count != 1u ||
      function_type->first_parameter >= unit->parameter_count) {
    return 0;
  }
  parameter = function_type->first_parameter;
  value_type = unit->parameters[parameter].type;
  if (unit->block_bindings[local].type >= unit->graph.type_count) {
    return 0;
  }
  local_type = &unit->graph.types[unit->block_bindings[local].type];
  function = &ir->functions[0];
  if (function_type->referenced_type >= unit->graph.type_count ||
      unit->graph.types[function_type->referenced_type].kind !=
          CTOOL_C_TYPE_VOID ||
      local_type->kind != CTOOL_C_TYPE_QUALIFIED ||
      (local_type->qualifiers & CTOOL_C_QUAL_VOLATILE) == 0u ||
      local_type->referenced_type != value_type ||
      definition->binding != function_binding ||
      function->binding != function_binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count != 8u ||
      function->maximum_stack_depth != 3u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[0].type != value_type ||
      instructions[0].reference != parameter ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[1].type != unit->block_bindings[local].type ||
      instructions[1].reference != local ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[2].type != value_type ||
      instructions[2].reference != parameter ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[3].type != value_type ||
      instructions[3].input_type != value_type ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_STORE_VALUE ||
      instructions[4].type != value_type ||
      instructions[4].input_type != value_type ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_STORE_VALUE ||
      instructions[5].type != value_type ||
      instructions[5].input_type != value_type ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_DISCARD ||
      instructions[6].type != CTOOL_C_TYPE_NONE ||
      instructions[6].input_type != value_type ||
      instructions[7].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VOID ||
      !string_equal(instructions[4].location.path,
                    "/local-parameter-assignment.c") ||
      !string_equal(instructions[6].physical_location.path,
                    "/local-parameter-assignment.c")) {
    (void)fprintf(stderr,
                  "local/parameter assignment IR instruction stream differs\n");
    return 0;
  }
  return 1;
}

static int validate_local_ir(const ctool_c_translation_unit_t *unit,
                             const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions;
  const ctool_c_block_binding_t *binding;
  const ctool_c_initializer_t *initializer;
  ctool_u32 local = find_block_binding(unit, "now");
  ctool_u32 prior = find_block_binding(unit, "prior");
  ctool_u32 unused = find_block_binding(unit, "unused");
  ctool_u32 timer = find_binding(unit, "timer_get_uptime_ms");
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 13u || ir->functions == NULL ||
      ir->instructions == NULL || local == CTOOL_C_AST_NONE ||
      prior == CTOOL_C_AST_NONE || unused == CTOOL_C_AST_NONE ||
      timer == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr, "local IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  function = &ir->functions[0];
  binding = &unit->block_bindings[local];
  if (binding->initializer >= unit->initializer_count) {
    return 0;
  }
  initializer = &unit->initializers[binding->initializer];
  instructions = ir->instructions + function->first_instruction;
  if (initializer->kind != CTOOL_C_INITIALIZER_EXPRESSION ||
      initializer->type != binding->type ||
      initializer->expression >= unit->expression_count ||
      unit->block_bindings[prior].storage != CTOOL_C_STORAGE_REGISTER ||
      unit->block_bindings[unused].storage != CTOOL_C_STORAGE_AUTO ||
      unit->block_bindings[unused].initializer != CTOOL_C_AST_NONE ||
      function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count != 13u ||
      function->maximum_stack_depth != 2u ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[0].type != binding->type ||
      instructions[0].input_type != CTOOL_C_TYPE_NONE ||
      instructions[0].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instructions[0].conversion != CTOOL_C_CONVERSION_NONE ||
      instructions[0].reference != local ||
      instructions[0].integer_bits != 0u ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_CALL_DIRECT ||
      instructions[1].type != binding->type ||
      instructions[1].input_type != unit->bindings[timer].type ||
      instructions[1].reference != timer ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_STORE ||
      instructions[2].type != binding->type ||
      instructions[2].input_type != binding->type ||
      instructions[2].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instructions[2].conversion != CTOOL_C_CONVERSION_NONE ||
      instructions[2].reference != CTOOL_C_AST_NONE ||
      instructions[2].integer_bits != 0u ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[3].type != unit->block_bindings[prior].type ||
      instructions[3].input_type != CTOOL_C_TYPE_NONE ||
      instructions[3].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instructions[3].conversion != CTOOL_C_CONVERSION_NONE ||
      instructions[3].reference != prior ||
      instructions[3].integer_bits != 0u ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS ||
      instructions[4].type != unit->parameters[0].type ||
      instructions[4].reference != 0u ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[5].type != unit->parameters[0].type ||
      instructions[5].input_type != unit->parameters[0].type ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_STORE ||
      instructions[6].type != unit->block_bindings[prior].type ||
      instructions[6].input_type != unit->parameters[0].type ||
      instructions[6].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instructions[6].conversion != CTOOL_C_CONVERSION_NONE ||
      instructions[6].reference != CTOOL_C_AST_NONE ||
      instructions[6].integer_bits != 0u ||
      instructions[7].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[7].type != binding->type ||
      instructions[7].input_type != CTOOL_C_TYPE_NONE ||
      instructions[7].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instructions[7].conversion != CTOOL_C_CONVERSION_NONE ||
      instructions[7].reference != local ||
      instructions[7].integer_bits != 0u ||
      instructions[8].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[8].type != binding->type ||
      instructions[8].input_type != binding->type ||
      instructions[9].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[9].type != unit->block_bindings[prior].type ||
      instructions[9].input_type != CTOOL_C_TYPE_NONE ||
      instructions[9].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instructions[9].conversion != CTOOL_C_CONVERSION_NONE ||
      instructions[9].reference != prior ||
      instructions[9].integer_bits != 0u ||
      instructions[10].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[10].type != unit->block_bindings[prior].type ||
      instructions[10].input_type != unit->block_bindings[prior].type ||
      instructions[11].kind != CTOOL_C_IR_INSTRUCTION_BINARY ||
      instructions[11].type != binding->type ||
      instructions[11].input_type != binding->type ||
      instructions[11].operation != CTOOL_C_EXPRESSION_OPERATOR_ADD ||
      instructions[12].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[12].type != binding->type ||
      instructions[12].input_type != binding->type ||
      !string_equal(instructions[0].location.path,
                    "/active-vga-local.c") ||
      !string_equal(instructions[2].physical_location.path,
                    "/active-vga-local.c")) {
    (void)fprintf(stderr, "local IR instruction stream differs\n");
    return 0;
  }
  return 1;
}

static int validate_file_object_load_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_function_t *function;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 comparison_type;
  ctool_u32 result_type;
  ctool_u32 local = find_block_binding(unit, "now");
  ctool_u32 timer = find_binding(unit, "timer_get_uptime_ms");
  ctool_u32 last_flip = find_binding(unit, "last_flip_ms");
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 12u || ir->functions == NULL ||
      ir->instructions == NULL || local == CTOOL_C_AST_NONE ||
      timer == CTOOL_C_AST_NONE || last_flip == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr, "file-object load IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->referenced_type >= unit->graph.type_count) {
    (void)fprintf(stderr, "file-object load function type differs\n");
    return 0;
  }
  result_type = function_type->referenced_type;
  function = &ir->functions[0];
  if (function->binding != definition->binding ||
      function->declared_type != definition->declared_type ||
      function->first_instruction != 0u ||
      function->instruction_count != 12u ||
      function->maximum_stack_depth != 2u) {
    (void)fprintf(stderr, "file-object load IR function differs\n");
    return 0;
  }
  instructions = ir->instructions + function->first_instruction;
  comparison_type = instructions[9].type;
  if (comparison_type >= unit->layout.type_count ||
      unit->layout.types[comparison_type].is_integer == CTOOL_FALSE ||
      unit->layout.types[comparison_type].is_signed == CTOOL_FALSE ||
      unit->layout.types[comparison_type].size != 4u ||
      comparison_type == result_type ||
      instructions[0].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[0].type != unit->block_bindings[local].type ||
      instructions[0].reference != local ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_CALL_DIRECT ||
      instructions[1].type != unit->block_bindings[local].type ||
      instructions[1].input_type != unit->bindings[timer].type ||
      instructions[1].reference != timer ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_STORE ||
      instructions[2].type != unit->block_bindings[local].type ||
      instructions[2].input_type != unit->block_bindings[local].type ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[3].type != unit->block_bindings[local].type ||
      instructions[3].reference != local ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[4].type != unit->block_bindings[local].type ||
      instructions[4].input_type != unit->block_bindings[local].type ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS ||
      instructions[5].type != unit->bindings[last_flip].type ||
      instructions[5].input_type != CTOOL_C_TYPE_NONE ||
      instructions[5].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instructions[5].conversion != CTOOL_C_CONVERSION_NONE ||
      instructions[5].reference != last_flip ||
      instructions[5].integer_bits != 0u ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[6].type != unit->bindings[last_flip].type ||
      instructions[6].input_type != unit->bindings[last_flip].type ||
      instructions[7].kind != CTOOL_C_IR_INSTRUCTION_BINARY ||
      instructions[7].type != unit->block_bindings[local].type ||
      instructions[7].input_type != unit->block_bindings[local].type ||
      instructions[7].operation != CTOOL_C_EXPRESSION_OPERATOR_SUBTRACT ||
      instructions[8].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[8].type != unit->block_bindings[local].type ||
      instructions[8].integer_bits != 16u ||
      instructions[9].kind != CTOOL_C_IR_INSTRUCTION_BINARY ||
      instructions[9].input_type != unit->block_bindings[local].type ||
      instructions[9].operation !=
          CTOOL_C_EXPRESSION_OPERATOR_GREATER_EQUAL ||
      instructions[9].conversion != CTOOL_C_CONVERSION_NONE ||
      instructions[9].reference != CTOOL_C_AST_NONE ||
      instructions[9].integer_bits != 0u ||
      instructions[10].kind != CTOOL_C_IR_INSTRUCTION_CONVERT ||
      instructions[10].type != result_type ||
      instructions[10].input_type != comparison_type ||
      instructions[10].conversion != CTOOL_C_CONVERSION_ASSIGNMENT ||
      instructions[11].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[11].type != result_type ||
      instructions[11].input_type != result_type ||
      !string_equal(instructions[5].location.path,
                    "/active-vga-flip-ready.c") ||
      !string_equal(instructions[6].physical_location.path,
                    "/active-vga-flip-ready.c")) {
    (void)fprintf(stderr, "file-object load IR stream differs\n");
    return 0;
  }
  return 1;
}

static int validate_external_file_object_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 object = find_binding(unit, "external_clock");
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 3u || ir->functions == NULL ||
      ir->instructions == NULL || object == CTOOL_C_AST_NONE ||
      ir->functions[0].first_instruction != 0u ||
      ir->functions[0].instruction_count != 3u ||
      ir->functions[0].maximum_stack_depth != 1u) {
    (void)fprintf(stderr, "external file-object IR inventory differs\n");
    return 0;
  }
  instructions = ir->instructions;
  if (instructions[0].kind != CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS ||
      instructions[0].type != unit->bindings[object].type ||
      instructions[0].input_type != CTOOL_C_TYPE_NONE ||
      instructions[0].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instructions[0].conversion != CTOOL_C_CONVERSION_NONE ||
      instructions[0].reference != object ||
      instructions[0].integer_bits != 0u ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[1].type != unit->bindings[object].type ||
      instructions[1].input_type != unit->bindings[object].type ||
      instructions[1].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[2].type != unit->bindings[object].type ||
      instructions[2].input_type != unit->bindings[object].type) {
    (void)fprintf(stderr, "external file-object IR stream differs\n");
    return 0;
  }
  return 1;
}

static int validate_file_member_ir(const ctool_c_translation_unit_t *unit,
                                   const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 object = find_binding(unit, "timer_state");
  ctool_u32 member = find_member(unit, "frequency");
  ctool_u32 object_type;
  ctool_u32 value_type;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 4u || ir->functions == NULL ||
      ir->instructions == NULL || object == CTOOL_C_AST_NONE ||
      member == CTOOL_C_AST_NONE ||
      ir->functions[0].first_instruction != 0u ||
      ir->functions[0].instruction_count != 4u ||
      ir->functions[0].maximum_stack_depth != 1u) {
    (void)fprintf(stderr, "file-member IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  object_type = unit->bindings[object].type;
  value_type = unit->graph.members[member].type;
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->referenced_type != value_type ||
      object_type >= unit->layout.type_count ||
      value_type >= unit->layout.type_count ||
      member >= unit->layout.member_count ||
      unit->layout.types[object_type].size != 20u ||
      unit->layout.members[member].byte_offset != 8u ||
      unit->layout.members[member].size != 4u) {
    (void)fprintf(stderr, "file-member type or layout differs\n");
    return 0;
  }
  instructions = ir->instructions;
  if (instructions[0].kind != CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS ||
      instructions[0].type != object_type ||
      instructions[0].input_type != CTOOL_C_TYPE_NONE ||
      instructions[0].reference != object ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_MEMBER_ADDRESS ||
      instructions[1].type != value_type ||
      instructions[1].input_type != object_type ||
      instructions[1].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instructions[1].conversion != CTOOL_C_CONVERSION_NONE ||
      instructions[1].reference != member ||
      instructions[1].integer_bits != 0u ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[2].type != value_type ||
      instructions[2].input_type != value_type ||
      instructions[2].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[3].type != value_type ||
      instructions[3].input_type != value_type ||
      !string_equal(instructions[1].location.path,
                    "/active-timer-frequency.c") ||
      !string_equal(instructions[1].physical_location.path,
                    "/active-timer-frequency.c")) {
    (void)fprintf(stderr, "file-member IR stream differs\n");
    return 0;
  }
  return 1;
}

static int validate_bit_field_ir(const ctool_c_translation_unit_t *unit,
                                 const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *definition;
  const ctool_c_type_node_t *function_type;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 object = find_binding(unit, "state");
  ctool_u32 member = find_member(unit, "r");
  ctool_u32 object_type;
  ctool_u32 value_type;
  if (unit->function_definition_count != 1u || ir->function_count != 1u ||
      ir->instruction_count != 3u || ir->functions == NULL ||
      ir->instructions == NULL || object == CTOOL_C_AST_NONE ||
      member == CTOOL_C_AST_NONE ||
      ir->functions[0].first_instruction != 0u ||
      ir->functions[0].instruction_count != 3u ||
      ir->functions[0].maximum_stack_depth != 1u) {
    (void)fprintf(stderr, "bit-field IR inventory differs\n");
    return 0;
  }
  definition = &unit->function_definitions[0];
  if (definition->declared_type >= unit->graph.type_count) {
    return 0;
  }
  function_type = &unit->graph.types[definition->declared_type];
  object_type = unit->bindings[object].type;
  value_type = unit->graph.members[member].type;
  if (function_type->kind != CTOOL_C_TYPE_FUNCTION ||
      function_type->referenced_type != value_type ||
      object_type >= unit->layout.type_count ||
      value_type >= unit->layout.type_count ||
      member >= unit->layout.member_count ||
      unit->graph.members[member].is_bit_field != CTOOL_TRUE ||
      unit->graph.members[member].bit_width != 8u ||
      unit->layout.types[object_type].size != 4u ||
      unit->layout.members[member].byte_offset != 0u ||
      unit->layout.members[member].bit_offset != 16u ||
      unit->layout.members[member].bit_width != 8u ||
      unit->layout.members[member].size != 4u) {
    (void)fprintf(stderr, "bit-field type or layout differs\n");
    return 0;
  }
  instructions = ir->instructions;
  if (instructions[0].kind != CTOOL_C_IR_INSTRUCTION_FILE_ADDRESS ||
      instructions[0].type != object_type ||
      instructions[0].input_type != CTOOL_C_TYPE_NONE ||
      instructions[0].reference != object ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_BIT_FIELD_LOAD ||
      instructions[1].type != value_type ||
      instructions[1].input_type != object_type ||
      instructions[1].operation != CTOOL_C_EXPRESSION_OPERATOR_NONE ||
      instructions[1].conversion != CTOOL_C_CONVERSION_LVALUE_TO_VALUE ||
      instructions[1].reference != member ||
      instructions[1].integer_bits != 0u ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      instructions[2].type != value_type ||
      instructions[2].input_type != value_type ||
      !string_equal(instructions[1].location.path,
                    "/bit-field-member.c") ||
      !string_equal(instructions[1].physical_location.path,
                    "/bit-field-member.c")) {
    (void)fprintf(stderr, "bit-field IR stream differs\n");
    return 0;
  }
  return 1;
}

static int validate_packed_bit_field_unit(
    const ctool_c_translation_unit_t *unit) {
  ctool_u32 object = find_binding(unit, "state");
  ctool_u32 member = find_member(unit, "ready");
  ctool_u32 record_type;
  const ctool_c_type_node_t *record;
  const ctool_c_record_member_t *field;
  const ctool_c_type_layout_t *record_layout;
  const ctool_c_member_layout_t *field_layout;
  if (object == CTOOL_C_AST_NONE || member == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr, "packed bit-field inventory differs\n");
    return 0;
  }
  record_type = unit->bindings[object].type;
  if (record_type >= unit->graph.type_count ||
      record_type >= unit->layout.type_count ||
      member >= unit->layout.member_count) {
    return 0;
  }
  record = &unit->graph.types[record_type];
  field = &unit->graph.members[member];
  record_layout = &unit->layout.types[record_type];
  field_layout = &unit->layout.members[member];
  if (record->kind != CTOOL_C_TYPE_RECORD ||
      record->record_complete != CTOOL_TRUE ||
      record->record_packed != CTOOL_TRUE ||
      member < record->first_member ||
      member - record->first_member >= record->member_count ||
      field->is_bit_field != CTOOL_TRUE || field->bit_width != 1u ||
      record_layout->size != 1u || record_layout->alignment != 1u ||
      field_layout->byte_offset != 0u || field_layout->bit_offset != 0u ||
      field_layout->bit_width != 1u || field_layout->size != 4u ||
      field_layout->alignment != 1u) {
    (void)fprintf(stderr, "packed bit-field layout differs\n");
    return 0;
  }
  return 1;
}

static int validate_point_of_declaration_ir(
    const ctool_c_translation_unit_t *unit, const ctool_c_ir_unit_t *ir) {
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 first = find_block_binding(unit, "first");
  ctool_u32 second = find_block_binding(unit, "second");
  if (unit->function_definition_count != 1u || unit->block_binding_count != 2u ||
      first == CTOOL_C_AST_NONE || second == CTOOL_C_AST_NONE ||
      ir->function_count != 1u || ir->instruction_count != 10u ||
      ir->functions == NULL || ir->instructions == NULL ||
      ir->functions[0].first_instruction != 0u ||
      ir->functions[0].instruction_count != 10u ||
      ir->functions[0].maximum_stack_depth != 2u) {
    (void)fprintf(stderr, "point-of-declaration IR inventory differs\n");
    return 0;
  }
  instructions = ir->instructions;
  if (instructions[0].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[0].reference != first ||
      instructions[1].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[1].reference != first ||
      instructions[2].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[3].kind != CTOOL_C_IR_INSTRUCTION_STORE ||
      instructions[4].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[4].reference != second ||
      instructions[5].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[5].integer_bits != 1u ||
      instructions[6].kind != CTOOL_C_IR_INSTRUCTION_STORE ||
      instructions[7].kind != CTOOL_C_IR_INSTRUCTION_LOCAL_ADDRESS ||
      instructions[7].reference != second ||
      instructions[8].kind != CTOOL_C_IR_INSTRUCTION_LOAD ||
      instructions[9].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE) {
    (void)fprintf(stderr, "point-of-declaration IR stream differs\n");
    return 0;
  }
  return 1;
}

static int validate_inline_ir(const ctool_c_translation_unit_t *unit,
                              const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *local;
  const ctool_c_function_definition_t *mixed;
  const ctool_c_binding_t *local_binding;
  const ctool_c_binding_t *mixed_binding;
  if (unit->function_definition_count != 2u || ir->function_count != 2u ||
      ir->instruction_count != 4u) {
    (void)fprintf(stderr, "inline IR inventory differs\n");
    return 0;
  }
  local = &unit->function_definitions[0];
  mixed = &unit->function_definitions[1];
  if (local->binding >= unit->binding_count ||
      mixed->binding >= unit->binding_count) {
    return 0;
  }
  local_binding = &unit->bindings[local->binding];
  mixed_binding = &unit->bindings[mixed->binding];
  if (local->storage != CTOOL_C_STORAGE_STATIC ||
      local->function_declaration_flags != CTOOL_C_FUNCTION_DECL_INLINE ||
      local_binding->linkage != CTOOL_C_LINKAGE_INTERNAL ||
      local_binding->function_declaration_flags !=
          CTOOL_C_FUNCTION_DECL_INLINE ||
      mixed->storage != CTOOL_C_STORAGE_NONE ||
      mixed->function_declaration_flags != 0u ||
      mixed_binding->linkage != CTOOL_C_LINKAGE_EXTERNAL ||
      mixed_binding->function_declaration_flags !=
          CTOOL_C_FUNCTION_DECL_INLINE ||
      ir->functions[0].binding != local->binding ||
      ir->functions[0].instruction_count != 2u ||
      ir->instructions[ir->functions[0].first_instruction].kind !=
          CTOOL_C_IR_INSTRUCTION_INTEGER ||
      ir->instructions[ir->functions[0].first_instruction].integer_bits !=
          1u ||
      ir->functions[1].binding != mixed->binding ||
      ir->functions[1].instruction_count != 2u ||
      ir->instructions[ir->functions[1].first_instruction].kind !=
          CTOOL_C_IR_INSTRUCTION_INTEGER ||
      ir->instructions[ir->functions[1].first_instruction].integer_bits !=
          2u) {
    (void)fprintf(stderr, "inline IR policy differs\n");
    return 0;
  }
  return 1;
}

static int call_instruction_matches(
    const ctool_c_ir_instruction_t *instruction,
    ctool_c_ir_instruction_kind_t kind, ctool_u32 type,
    ctool_u32 input_type, ctool_c_conversion_kind_t conversion,
    ctool_u32 reference) {
  return instruction->kind == kind && instruction->type == type &&
                 instruction->input_type == input_type &&
                 instruction->operation == CTOOL_C_EXPRESSION_OPERATOR_NONE &&
                 instruction->conversion == conversion &&
                 instruction->reference == reference &&
                 instruction->integer_bits == 0u &&
                 string_equal(instruction->location.path,
                              "/active-direct-calls.c") != 0 &&
                 string_equal(instruction->physical_location.path,
                              "/active-direct-calls.c") != 0
             ? 1
             : 0;
}

static int validate_call_ir(const ctool_c_translation_unit_t *unit,
                            const ctool_c_ir_unit_t *ir) {
  const ctool_c_function_definition_t *getpid_definition;
  const ctool_c_function_definition_t *forward_definition;
  const ctool_c_function_definition_t *local_definition;
  const ctool_c_function_definition_t *caller_definition;
  const ctool_c_function_definition_t *void_definition;
  const ctool_c_type_node_t *getpid_type;
  const ctool_c_type_node_t *forward_type;
  const ctool_c_type_node_t *local_type;
  const ctool_c_type_node_t *caller_type;
  const ctool_c_type_node_t *void_type;
  const ctool_c_type_node_t *external_type;
  const ctool_c_type_node_t *sink_type;
  const ctool_c_type_node_t *process_type;
  const ctool_c_ir_instruction_t *instructions;
  ctool_u32 process_binding = find_binding(unit, "process_get_current_pid");
  ctool_u32 external_binding = find_binding(unit, "external_sum");
  ctool_u32 local_binding = find_binding(unit, "local_target");
  ctool_u32 sink_binding = find_binding(unit, "external_sink");
  ctool_u32 first_parameter;
  ctool_u32 integer_type;
  if (unit->function_definition_count != 5u || ir->function_count != 5u ||
      ir->functions == NULL || ir->instruction_count != 16u ||
      ir->instructions == NULL || process_binding == CTOOL_C_AST_NONE ||
      external_binding == CTOOL_C_AST_NONE ||
      local_binding == CTOOL_C_AST_NONE || sink_binding == CTOOL_C_AST_NONE) {
    (void)fprintf(stderr, "call IR inventory differs\n");
    return 0;
  }
  getpid_definition = &unit->function_definitions[0];
  forward_definition = &unit->function_definitions[1];
  local_definition = &unit->function_definitions[2];
  caller_definition = &unit->function_definitions[3];
  void_definition = &unit->function_definitions[4];
  if (getpid_definition->declared_type >= unit->graph.type_count ||
      forward_definition->declared_type >= unit->graph.type_count ||
      local_definition->declared_type >= unit->graph.type_count ||
      caller_definition->declared_type >= unit->graph.type_count ||
      void_definition->declared_type >= unit->graph.type_count ||
      unit->bindings[process_binding].type >= unit->graph.type_count ||
      unit->bindings[external_binding].type >= unit->graph.type_count ||
      unit->bindings[sink_binding].type >= unit->graph.type_count) {
    return 0;
  }
  getpid_type = &unit->graph.types[getpid_definition->declared_type];
  forward_type = &unit->graph.types[forward_definition->declared_type];
  local_type = &unit->graph.types[local_definition->declared_type];
  caller_type = &unit->graph.types[caller_definition->declared_type];
  void_type = &unit->graph.types[void_definition->declared_type];
  process_type = &unit->graph.types[unit->bindings[process_binding].type];
  external_type = &unit->graph.types[unit->bindings[external_binding].type];
  sink_type = &unit->graph.types[unit->bindings[sink_binding].type];
  if (getpid_type->kind != CTOOL_C_TYPE_FUNCTION ||
      process_type->kind != CTOOL_C_TYPE_FUNCTION ||
      forward_type->kind != CTOOL_C_TYPE_FUNCTION ||
      external_type->kind != CTOOL_C_TYPE_FUNCTION ||
      local_type->kind != CTOOL_C_TYPE_FUNCTION ||
      caller_type->kind != CTOOL_C_TYPE_FUNCTION ||
      void_type->kind != CTOOL_C_TYPE_FUNCTION ||
      sink_type->kind != CTOOL_C_TYPE_FUNCTION ||
      forward_type->parameter_count != 2u || unit->parameter_count < 2u ||
      forward_type->first_parameter > unit->parameter_count - 2u ||
      void_type->parameter_count != 1u ||
      void_type->first_parameter >= unit->parameter_count ||
      getpid_type->referenced_type != process_type->referenced_type ||
      local_type->referenced_type != caller_type->referenced_type ||
      void_type->referenced_type != sink_type->referenced_type) {
    (void)fprintf(stderr, "call IR function types differ\n");
    return 0;
  }
  first_parameter = forward_type->first_parameter;
  integer_type = unit->parameters[first_parameter].type;
  instructions = ir->instructions;
  if (ir->functions[0].binding != getpid_definition->binding ||
      ir->functions[0].first_instruction != 0u ||
      ir->functions[0].instruction_count != 2u ||
      ir->functions[0].maximum_stack_depth != 1u ||
      !call_instruction_matches(
          &instructions[0], CTOOL_C_IR_INSTRUCTION_CALL_DIRECT,
          getpid_type->referenced_type, unit->bindings[process_binding].type,
          CTOOL_C_CONVERSION_NONE, process_binding) ||
      !call_instruction_matches(
          &instructions[1], CTOOL_C_IR_INSTRUCTION_RETURN_VALUE,
          getpid_type->referenced_type, getpid_type->referenced_type,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE) ||
      ir->functions[1].binding != forward_definition->binding ||
      ir->functions[1].first_instruction != 2u ||
      ir->functions[1].instruction_count != 6u ||
      ir->functions[1].maximum_stack_depth != 2u ||
      !call_instruction_matches(
          &instructions[2], CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
          integer_type, CTOOL_C_TYPE_NONE, CTOOL_C_CONVERSION_NONE,
          first_parameter) ||
      !call_instruction_matches(
          &instructions[3], CTOOL_C_IR_INSTRUCTION_LOAD, integer_type,
          integer_type, CTOOL_C_CONVERSION_LVALUE_TO_VALUE,
          CTOOL_C_AST_NONE) ||
      !call_instruction_matches(
          &instructions[4], CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
          unit->parameters[first_parameter + 1u].type, CTOOL_C_TYPE_NONE,
          CTOOL_C_CONVERSION_NONE, first_parameter + 1u) ||
      !call_instruction_matches(
          &instructions[5], CTOOL_C_IR_INSTRUCTION_LOAD,
          unit->parameters[first_parameter + 1u].type,
          unit->parameters[first_parameter + 1u].type,
          CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE) ||
      !call_instruction_matches(
          &instructions[6], CTOOL_C_IR_INSTRUCTION_CALL_DIRECT,
          forward_type->referenced_type,
          unit->bindings[external_binding].type, CTOOL_C_CONVERSION_NONE,
          external_binding) ||
      !call_instruction_matches(
          &instructions[7], CTOOL_C_IR_INSTRUCTION_RETURN_VALUE,
          forward_type->referenced_type, forward_type->referenced_type,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE) ||
      ir->functions[2].binding != local_definition->binding ||
      ir->functions[2].first_instruction != 8u ||
      ir->functions[2].instruction_count != 2u ||
      ir->functions[2].maximum_stack_depth != 1u ||
      instructions[8].kind != CTOOL_C_IR_INSTRUCTION_INTEGER ||
      instructions[8].integer_bits != 9u ||
      instructions[9].kind != CTOOL_C_IR_INSTRUCTION_RETURN_VALUE ||
      ir->functions[3].binding != caller_definition->binding ||
      ir->functions[3].first_instruction != 10u ||
      ir->functions[3].instruction_count != 2u ||
      ir->functions[3].maximum_stack_depth != 1u ||
      !call_instruction_matches(
          &instructions[10], CTOOL_C_IR_INSTRUCTION_CALL_DIRECT,
          caller_type->referenced_type, local_definition->declared_type,
          CTOOL_C_CONVERSION_NONE, local_binding) ||
      !call_instruction_matches(
          &instructions[11], CTOOL_C_IR_INSTRUCTION_RETURN_VALUE,
          caller_type->referenced_type, caller_type->referenced_type,
          CTOOL_C_CONVERSION_NONE, CTOOL_C_AST_NONE) ||
      ir->functions[4].binding != void_definition->binding ||
      ir->functions[4].first_instruction != 12u ||
      ir->functions[4].instruction_count != 4u ||
      ir->functions[4].maximum_stack_depth != 1u ||
      !call_instruction_matches(
          &instructions[12], CTOOL_C_IR_INSTRUCTION_PARAMETER_ADDRESS,
          unit->parameters[void_type->first_parameter].type,
          CTOOL_C_TYPE_NONE, CTOOL_C_CONVERSION_NONE,
          void_type->first_parameter) ||
      !call_instruction_matches(
          &instructions[13], CTOOL_C_IR_INSTRUCTION_LOAD,
          unit->parameters[void_type->first_parameter].type,
          unit->parameters[void_type->first_parameter].type,
          CTOOL_C_CONVERSION_LVALUE_TO_VALUE, CTOOL_C_AST_NONE) ||
      !call_instruction_matches(
          &instructions[14], CTOOL_C_IR_INSTRUCTION_CALL_DIRECT,
          void_type->referenced_type, unit->bindings[sink_binding].type,
          CTOOL_C_CONVERSION_NONE, sink_binding) ||
      !call_instruction_matches(
          &instructions[15], CTOOL_C_IR_INSTRUCTION_RETURN_VOID,
          CTOOL_C_TYPE_NONE, CTOOL_C_TYPE_NONE, CTOOL_C_CONVERSION_NONE,
          CTOOL_C_AST_NONE)) {
    ctool_u32 index;
    (void)fprintf(stderr, "call IR instructions differ\n");
    for (index = 0u; index < ir->function_count; index++) {
      (void)fprintf(stderr,
                    "function %lu: binding=%lu first=%lu count=%lu "
                    "maximum=%lu\n",
                    (unsigned long)index,
                    (unsigned long)ir->functions[index].binding,
                    (unsigned long)ir->functions[index].first_instruction,
                    (unsigned long)ir->functions[index].instruction_count,
                    (unsigned long)ir->functions[index].maximum_stack_depth);
    }
    for (index = 0u; index < ir->instruction_count; index++) {
      (void)fprintf(stderr,
                    "instruction %lu: kind=%lu type=%lu input=%lu "
                    "operation=%lu conversion=%lu reference=%lu bits=%lu\n",
                    (unsigned long)index,
                    (unsigned long)instructions[index].kind,
                    (unsigned long)instructions[index].type,
                    (unsigned long)instructions[index].input_type,
                    (unsigned long)instructions[index].operation,
                    (unsigned long)instructions[index].conversion,
                    (unsigned long)instructions[index].reference,
                    (unsigned long)instructions[index].integer_bits);
    }
    return 0;
  }
  return 1;
}

static int run_active_leaf(const char *host_root) {
  static const char simple_source[] =
      "int answer(void) { return 42; }\n"
      "static void idle(void) {}\n"
      "unsigned int as_unsigned(int value) { return value; }\n";
  static const char statement_source[] =
      "int choose(int value) {\n"
      "  if (value) return 1;\n"
      "  return 0;\n"
      "}\n";
  static const char expression_source[] =
      "int complement(int value) { return ~value; }\n";
  static const char multiplication_source[] =
      "int CANVAS_X = 56;\n"
      "int CANVAS_Y = 20;\n"
      "int zoom_level = 1;\n"
      "int view_x = 0;\n"
      "int view_y = 0;\n"
      "int canvas_to_screen_x(int cx) {\n"
      "  return CANVAS_X + (cx - view_x) * zoom_level;\n"
      "}\n"
      "int canvas_to_screen_y(int cy) {\n"
      "  return CANVAS_Y + (cy - view_y) * zoom_level;\n"
      "}\n";
  static const char unsigned_multiplication_source[] =
      "unsigned int multiply_unsigned(unsigned int value) {\n"
      "  return value * 0x80000001u;\n"
      "}\n";
  static const char file_assignment_source[] =
      "typedef enum { false = 0, true = 1 } bool;\n"
      "static bool vga_wait_vsync = false;\n"
      "void vga_set_vsync_wait(bool enabled) { vga_wait_vsync = enabled; }\n";
  static const char file_member_source[] =
      "typedef unsigned int uint32_t;\n"
      "typedef unsigned long long uint64_t;\n"
      "typedef enum { false = 0, true = 1 } bool;\n"
      "typedef struct {\n"
      "  uint64_t ticks;\n"
      "  uint32_t frequency;\n"
      "  uint32_t ms_per_tick;\n"
      "  bool is_calibrated;\n"
      "} timer_state_t;\n"
      "static timer_state_t timer_state = {\n"
      "    .ticks = 0,\n"
      "    .frequency = 0,\n"
      "    .ms_per_tick = 0,\n"
      "    .is_calibrated = false\n"
      "};\n"
      "uint32_t timer_get_frequency(void) {\n"
      "    return timer_state.frequency;\n"
      "}\n";
  static const char bit_field_member_source[] =
      "typedef unsigned int uint32_t;\n"
      "struct color {\n"
      "  uint32_t b : 8;\n"
      "  uint32_t g : 8;\n"
      "  uint32_t r : 8;\n"
      "  uint32_t a : 8;\n"
      "};\n"
      "static volatile struct color state;\n"
      "uint32_t read_red(void) { return state.r; }\n";
  static const char narrow_bit_field_source[] =
      "struct flags { _Bool ready : 1; };\n"
      "static struct flags state;\n"
      "int read_ready(void) { return state.ready; }\n";
  static const char atomic_bit_field_source[] =
      "struct flags { _Atomic unsigned int ready : 1; };\n"
      "static struct flags state;\n"
      "unsigned int read_ready(void) { return state.ready; }\n";
  static const char packed_bit_field_source[] =
      "struct flags { unsigned int ready : 1; } "
      "__attribute__((packed));\n"
      "static struct flags state;\n"
      "unsigned int read_ready(void) { return state.ready; }\n";
  static const char atomic_assignment_source[] =
      "_Atomic int atomic_state;\n"
      "void set_atomic(void) { atomic_state = 1; }\n";
  static const char chained_assignment_source[] =
      "int first_state;\n"
      "int second_state;\n"
      "int set_both(int value) { return first_state = second_state = value; }\n";
  static const char local_parameter_assignment_source[] =
      "void assign_local_parameter(int value) {\n"
      "  volatile int local;\n"
      "  value = local = value;\n"
      "}\n";
  static const char wide_assignment_source[] =
      "long long wide_state;\n"
      "void set_wide(void) { wide_state = 1; }\n";
  static const char pointer_assignment_source[] =
      "int *pointer_state;\n"
      "void clear_pointer(void) { pointer_state = 0; }\n";
  static const char compound_assignment_source[] =
      "int counter;\n"
      "void bump(void) { counter += 1; }\n";
  static const char abi_source[] =
      "long long wide(long long value) { return value; }\n";
  static const char inline_success_source[] =
      "static inline int local_inline(void) { return 1; }\n"
      "inline int mixed_inline(void);\n"
      "int mixed_inline(void) { return 2; }\n";
  static const char external_inline_source[] =
      "inline int external_inline(void) { return 1; }\n";
  static const char extern_inline_source[] =
      "extern inline int extern_inline(void) { return 1; }\n";
  static const char conversion_source[] =
      "unsigned int cast_value(int value) {\n"
      "  return (unsigned int)value;\n"
      "}\n";
  static const char indirect_call_source[] =
      "int (*indirect_call)(int);\n"
      "int call_pointer(int value) { return indirect_call(value); }\n";
  static const char wide_call_source[] =
      "int wide_target(long long value);\n"
      "int call_wide(void) { return wide_target(1); }\n";
  static const char wide_comparison_source[] =
      "int wide_less_equal(void) { return 1LL <= 2LL; }\n";
  static const char wide_logical_or_source[] =
      "int wide_logical_or(void) { return 1LL || 0LL; }\n";
  static const char wide_multiplication_source[] =
      "int wide_multiply(void) { return 2LL * 3LL; }\n";
  static const char wide_left_shift_source[] =
      "int wide_left_shift(void) { return 1LL << 1; }\n";
  static const char wide_right_shift_source[] =
      "int wide_right_shift(void) { return 8LL >> 1; }\n";
  static const char wide_bitwise_or_source[] =
      "int wide_bitwise_or(void) { return 1LL | 2LL; }\n";
  static const char wide_bitwise_xor_source[] =
      "int wide_bitwise_xor(void) { return 1LL ^ 2LL; }\n";
  static const char wide_division_source[] =
      "int wide_divide(void) { return 8LL / 2LL; }\n";
  static const char wide_remainder_source[] =
      "int wide_remainder(void) { return 8LL % 3LL; }\n";
  static const char variadic_call_source[] =
      "int variadic_target(int first, ...);\n"
      "int call_variadic(void) { return variadic_target(1); }\n";
  static const char value_statement_source[] =
      "void discard_value(void) { 1; }\n";
  static const char wide_local_source[] =
      "int wide_local(void) { long long value = 1; return 0; }\n";
  static const char short_local_source[] =
      "int short_local(void) { short value = 1; return 0; }\n";
  static const char overaligned_local_source[] =
      "typedef int aligned_int __attribute__((aligned(8)));\n"
      "int overaligned_local(void) {\n"
      "  aligned_int value = 1;\n"
      "  return value;\n"
      "}\n";
  static const char array_local_source[] =
      "int array_local(void) { int values[1]; return 0; }\n";
  static const char static_local_source[] =
      "int static_local(void) { static int value = 1; return value; }\n";
  static const char ownership_source[] =
      "int first_local(void) { int one = 1; return one; }\n"
      "int second_local(void) { int two = 2; return two; }\n";
  static const char point_of_declaration_source[] =
      "int point_of_declaration(void) {\n"
      "  int first = first;\n"
      "  int second = 1;\n"
      "  return second;\n"
      "}\n";
  static const char void_initializer_source[] =
      "void sink(void);\n"
      "int malformed_initializer(void) { int value = 1; return value; }\n"
      "void call_sink(void) { sink(); }\n"
      "unsigned int unsigned_value(void) { return 1u; }\n"
      "int qualified_local(int input) {\n"
      "  const int result = input;\n"
      "  return result;\n"
      "}\n";
  static const char global_frontier_source[] =
      "typedef unsigned int uint32_t;\n"
      "typedef enum { false = 0, true = 1 } bool;\n"
      "uint32_t timer_get_uptime_ms(void);\n"
      "static uint32_t last_flip_ms = 0;\n"
      "bool vga_flip_ready(void) {\n"
      "  uint32_t now = timer_get_uptime_ms();\n"
      "  return (now - last_flip_ms) >= 16u;\n"
      "}\n";
  static const char external_file_object_source[] =
      "extern unsigned int external_clock;\n"
      "unsigned int read_external_clock(void) { return external_clock; }\n";
  static const char narrow_file_object_source[] =
      "extern unsigned short narrow_state;\n"
      "int read_narrow_state(void) { return narrow_state; }\n";
  static const char enumerator_identifier_source[] =
      "enum E { E_ONE = 1 };\n"
      "int read_enumerator(void) { return E_ONE; }\n";
  ctool_host_adapter_t adapter;
  ctool_host_adapter_t limited_adapter;
  ctool_job_config_t config;
  ctool_job_t *job = NULL;
  ctool_job_t *limited_job = NULL;
  ctool_c_translation_unit_t active_unit;
  ctool_c_translation_unit_t logic_unit;
  ctool_c_translation_unit_t addition_unit;
  ctool_c_translation_unit_t multiplication_unit;
  ctool_c_translation_unit_t unsigned_multiplication_unit;
  ctool_c_translation_unit_t file_assignment_unit;
  ctool_c_translation_unit_t file_member_unit;
  ctool_c_translation_unit_t bit_field_member_unit;
  ctool_c_translation_unit_t narrow_bit_field_unit;
  ctool_c_translation_unit_t atomic_bit_field_unit;
  ctool_c_translation_unit_t packed_bit_field_unit;
  ctool_c_translation_unit_t atomic_assignment_unit;
  ctool_c_translation_unit_t chained_assignment_unit;
  ctool_c_translation_unit_t local_parameter_assignment_unit;
  ctool_c_translation_unit_t wide_assignment_unit;
  ctool_c_translation_unit_t pointer_assignment_unit;
  ctool_c_translation_unit_t compound_assignment_unit;
  ctool_c_translation_unit_t local_unit;
  ctool_c_translation_unit_t simple_unit;
  ctool_c_translation_unit_t statement_unit;
  ctool_c_translation_unit_t expression_unit;
  ctool_c_translation_unit_t abi_unit;
  ctool_c_translation_unit_t inline_success_unit;
  ctool_c_translation_unit_t external_inline_unit;
  ctool_c_translation_unit_t extern_inline_unit;
  ctool_c_translation_unit_t conversion_unit;
  ctool_c_translation_unit_t call_unit;
  ctool_c_translation_unit_t indirect_call_unit;
  ctool_c_translation_unit_t wide_call_unit;
  ctool_c_translation_unit_t wide_comparison_unit;
  ctool_c_translation_unit_t division_unit;
  ctool_c_translation_unit_t logical_or_unit;
  ctool_c_translation_unit_t branch_fit_unit;
  ctool_c_translation_unit_t aes_rotw_unit;
  ctool_c_translation_unit_t simd_cpuid_unit;
  ctool_c_translation_unit_t wide_logical_or_unit;
  ctool_c_translation_unit_t wide_multiplication_unit;
  ctool_c_translation_unit_t wide_left_shift_unit;
  ctool_c_translation_unit_t wide_right_shift_unit;
  ctool_c_translation_unit_t wide_bitwise_or_unit;
  ctool_c_translation_unit_t wide_bitwise_xor_unit;
  ctool_c_translation_unit_t wide_division_unit;
  ctool_c_translation_unit_t wide_remainder_unit;
  ctool_c_translation_unit_t variadic_call_unit;
  ctool_c_translation_unit_t value_statement_unit;
  ctool_c_translation_unit_t wide_local_unit;
  ctool_c_translation_unit_t short_local_unit;
  ctool_c_translation_unit_t overaligned_local_unit;
  ctool_c_translation_unit_t array_local_unit;
  ctool_c_translation_unit_t static_local_unit;
  ctool_c_translation_unit_t ownership_unit;
  ctool_c_translation_unit_t point_of_declaration_unit;
  ctool_c_translation_unit_t void_initializer_unit;
  ctool_c_translation_unit_t global_frontier_unit;
  ctool_c_translation_unit_t external_file_object_unit;
  ctool_c_translation_unit_t narrow_file_object_unit;
  ctool_c_translation_unit_t enumerator_identifier_unit;
  ctool_c_translation_unit_t invalid_unit;
  ctool_c_function_definition_t invalid_definition;
  ctool_c_initializer_element_t dangling_initializer_element;
  ctool_c_statement_t *invalid_statements = NULL;
  ctool_c_initializer_t *invalid_initializers = NULL;
  ctool_c_initializer_t *void_initializers = NULL;
  ctool_c_expression_t *invalid_expressions = NULL;
  ctool_c_expression_t *cpuid_expressions = NULL;
  ctool_c_expression_t *ownership_expressions = NULL;
  ctool_c_expression_t *file_expressions = NULL;
  ctool_c_expression_t *assignment_expressions = NULL;
  ctool_u32 *assignment_children = NULL;
  ctool_c_expression_t *member_expressions = NULL;
  ctool_c_member_layout_t *member_layouts = NULL;
  ctool_c_member_layout_t *bit_field_layouts = NULL;
  ctool_c_record_member_t *bit_field_members = NULL;
  ctool_c_binding_t *file_bindings = NULL;
  ctool_c_type_layout_t *invalid_layouts = NULL;
  ctool_c_block_binding_t *invalid_block_bindings = NULL;
  ctool_c_ir_unit_t ir;
  ctool_status_t status;
  ctool_u32 diagnostic_count;
  ctool_u32 file_binding;
  uint64_t fingerprint;
  char *fixture = NULL;
  char *logic_fixture = NULL;
  char *division_fixture = NULL;
  char *logical_or_fixture = NULL;
  char *branch_fit_fixture = NULL;
  char *aes_rotw_fixture = NULL;
  char *simd_cpuid_fixture = NULL;
  char *call_fixture = NULL;
  ctool_u32 xor_expression;
  ctool_u32 comparison_expression;
  ctool_u32 index;
  int passed = 0;

  (void)memset(&active_unit, 0, sizeof(active_unit));
  (void)memset(&logic_unit, 0, sizeof(logic_unit));
  (void)memset(&division_unit, 0, sizeof(division_unit));
  (void)memset(&branch_fit_unit, 0, sizeof(branch_fit_unit));
  (void)memset(&aes_rotw_unit, 0, sizeof(aes_rotw_unit));
  (void)memset(&simd_cpuid_unit, 0, sizeof(simd_cpuid_unit));
  (void)memset(&addition_unit, 0, sizeof(addition_unit));
  (void)memset(&multiplication_unit, 0, sizeof(multiplication_unit));
  (void)memset(&unsigned_multiplication_unit, 0,
               sizeof(unsigned_multiplication_unit));
  (void)memset(&file_assignment_unit, 0, sizeof(file_assignment_unit));
  (void)memset(&file_member_unit, 0, sizeof(file_member_unit));
  (void)memset(&bit_field_member_unit, 0,
               sizeof(bit_field_member_unit));
  (void)memset(&narrow_bit_field_unit, 0,
               sizeof(narrow_bit_field_unit));
  (void)memset(&atomic_bit_field_unit, 0,
               sizeof(atomic_bit_field_unit));
  (void)memset(&packed_bit_field_unit, 0,
               sizeof(packed_bit_field_unit));
  (void)memset(&atomic_assignment_unit, 0,
               sizeof(atomic_assignment_unit));
  (void)memset(&chained_assignment_unit, 0,
               sizeof(chained_assignment_unit));
  (void)memset(&local_parameter_assignment_unit, 0,
               sizeof(local_parameter_assignment_unit));
  (void)memset(&wide_assignment_unit, 0, sizeof(wide_assignment_unit));
  (void)memset(&pointer_assignment_unit, 0,
               sizeof(pointer_assignment_unit));
  (void)memset(&compound_assignment_unit, 0,
               sizeof(compound_assignment_unit));
  (void)memset(&local_unit, 0, sizeof(local_unit));
  (void)memset(&simple_unit, 0, sizeof(simple_unit));
  if (!open_job(host_root, &adapter, &config, &job) ||
      !active_source_is_unchanged(job)) {
    goto cleanup;
  }
  fixture = make_active_fixture();
  if (fixture == NULL ||
      !parse_source(job, "/active-cemit-add-overflows.c", fixture,
                    &active_unit)) {
    (void)fprintf(stderr, "active helper setup failed\n");
    goto cleanup;
  }
  logic_fixture = make_logic_fixture();
  if (logic_fixture == NULL ||
      !parse_source(job, "/active-cemit-power-of-two.c", logic_fixture,
                    &logic_unit)) {
    (void)fprintf(stderr, "active logic helper setup failed\n");
    goto cleanup;
  }
  division_fixture = make_division_fixture();
  if (division_fixture == NULL ||
      !parse_source(job, "/active-cemit-multiply-overflows.c",
                    division_fixture, &division_unit)) {
    (void)fprintf(stderr, "active division helper setup failed\n");
    goto cleanup;
  }
  logical_or_fixture = make_logical_or_fixture();
  if (logical_or_fixture == NULL ||
      !parse_source(job, "/active-cfront-bool-valid.c", logical_or_fixture,
                    &logical_or_unit)) {
    (void)fprintf(stderr, "active logical-or helper setup failed\n");
    goto cleanup;
  }
  branch_fit_fixture = make_branch_fit_fixture();
  if (branch_fit_fixture == NULL ||
      !parse_source(job, "/active-asm-branch-fits-i8.c",
                    branch_fit_fixture, &branch_fit_unit)) {
    (void)fprintf(stderr, "active branch-range helper setup failed\n");
    goto cleanup;
  }
  aes_rotw_fixture = make_aes_rotw_fixture();
  if (aes_rotw_fixture == NULL ||
      !parse_source(job, "/active-aes-rotw.c", aes_rotw_fixture,
                    &aes_rotw_unit)) {
    (void)fprintf(stderr, "active AES word-rotation setup failed\n");
    goto cleanup;
  }
  simd_cpuid_fixture = make_simd_cpuid_fixture();
  if (simd_cpuid_fixture == NULL ||
      !parse_source(job, "/active-simd-cpuid.c", simd_cpuid_fixture,
                    &simd_cpuid_unit)) {
    (void)fprintf(stderr, "active CPUID toggle setup failed\n");
    goto cleanup;
  }

  if (!parse_source(job, "/active-cupidc-add2.c", active_addition,
                    &addition_unit)) {
    (void)fprintf(stderr, "active addition setup failed\n");
    goto cleanup;
  }

  if (!parse_source(job, "/active-paint-multiplication.c",
                    multiplication_source, &multiplication_unit)) {
    (void)fprintf(stderr, "active Paint multiplication setup failed\n");
    goto cleanup;
  }

  if (!parse_source(job, "/unsigned-multiplication.c",
                    unsigned_multiplication_source,
                    &unsigned_multiplication_unit)) {
    (void)fprintf(stderr, "unsigned multiplication setup failed\n");
    goto cleanup;
  }

  if (!parse_source(job, "/active-vga-file-assignment.c",
                    file_assignment_source, &file_assignment_unit)) {
    (void)fprintf(stderr, "active VGA file assignment setup failed\n");
    goto cleanup;
  }

  if (!parse_source(job, "/active-timer-frequency.c", file_member_source,
                    &file_member_unit)) {
    (void)fprintf(stderr, "active timer frequency setup failed\n");
    goto cleanup;
  }

  if (!parse_source(job, "/bit-field-member.c", bit_field_member_source,
                    &bit_field_member_unit)) {
    (void)fprintf(stderr, "bit-field member setup failed\n");
    goto cleanup;
  }
  if (!parse_source(job, "/narrow-bit-field.c", narrow_bit_field_source,
                    &narrow_bit_field_unit) ||
      !expect_ir_failure(
          job, &narrow_bit_field_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "narrow bit-field load")) {
    goto cleanup;
  }
  if (!parse_source(job, "/atomic-bit-field.c", atomic_bit_field_source,
                    &atomic_bit_field_unit) ||
      !expect_ir_failure(
          job, &atomic_bit_field_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "atomic bit-field load")) {
    goto cleanup;
  }
  if (!parse_source_mode(job, "/packed-bit-field.c",
                         packed_bit_field_source, CTOOL_TRUE,
                         &packed_bit_field_unit) ||
      !validate_packed_bit_field_unit(&packed_bit_field_unit) ||
      !expect_ir_failure(
          job, &packed_bit_field_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "packed bit-field load")) {
    goto cleanup;
  }

  if (!parse_source(job, "/atomic-assignment.c", atomic_assignment_source,
                    &atomic_assignment_unit)) {
    goto cleanup;
  }

  if (!parse_source(job, "/chained-assignment.c", chained_assignment_source,
                    &chained_assignment_unit)) {
    (void)fprintf(stderr, "chained assignment setup failed\n");
    goto cleanup;
  }
  if (!parse_source(job, "/local-parameter-assignment.c",
                    local_parameter_assignment_source,
                    &local_parameter_assignment_unit)) {
    (void)fprintf(stderr, "local/parameter assignment setup failed\n");
    goto cleanup;
  }
  if (!expect_ir_failure(
          job, &atomic_assignment_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "atomic assignment")) {
    goto cleanup;
  }
  if (!parse_source(job, "/wide-assignment.c", wide_assignment_source,
                    &wide_assignment_unit) ||
      !expect_ir_failure(
          job, &wide_assignment_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide assignment") ||
      !parse_source(job, "/pointer-assignment.c", pointer_assignment_source,
                    &pointer_assignment_unit) ||
      !expect_ir_failure(
          job, &pointer_assignment_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "pointer assignment") ||
      !parse_source(job, "/compound-assignment.c",
                    compound_assignment_source,
                    &compound_assignment_unit) ||
      !expect_ir_failure(
          job, &compound_assignment_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_EXPRESSION,
          "CupidC IR lowering does not yet support this expression",
          "compound assignment")) {
    goto cleanup;
  }

  if (!parse_source(job, "/active-vga-local.c", local_fixture,
                    &local_unit)) {
    (void)fprintf(stderr, "active VGA local setup failed\n");
    goto cleanup;
  }

  invalid_unit = local_unit;
  invalid_unit.block_bindings = NULL;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "missing local binding table")) {
    goto cleanup;
  }
  invalid_unit = local_unit;
  invalid_unit.object_definition_count = 1u;
  invalid_unit.object_definitions = NULL;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "missing object definition table")) {
    goto cleanup;
  }
  invalid_unit = local_unit;
  invalid_unit.initializers = NULL;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "missing initializer table")) {
    goto cleanup;
  }
  invalid_unit = local_unit;
  invalid_unit.initializer_element_count = 1u;
  invalid_unit.initializer_elements = NULL;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "missing initializer element table")) {
    goto cleanup;
  }
  invalid_block_bindings = (ctool_c_block_binding_t *)malloc(
      (size_t)local_unit.block_binding_count *
      sizeof(*invalid_block_bindings));
  if (invalid_block_bindings == NULL) {
    goto cleanup;
  }
  (void)memcpy(invalid_block_bindings, local_unit.block_bindings,
               (size_t)local_unit.block_binding_count *
                   sizeof(*invalid_block_bindings));
  invalid_block_bindings[0].kind = (ctool_c_binding_kind_t)0;
  invalid_unit = local_unit;
  invalid_unit.block_bindings = invalid_block_bindings;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "invalid local binding kind")) {
    goto cleanup;
  }
  (void)memcpy(invalid_block_bindings, local_unit.block_bindings,
               (size_t)local_unit.block_binding_count *
                   sizeof(*invalid_block_bindings));
  invalid_block_bindings[0].storage = (ctool_c_storage_class_t)99;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "invalid local storage class")) {
    goto cleanup;
  }
  (void)memcpy(invalid_block_bindings, local_unit.block_bindings,
               (size_t)local_unit.block_binding_count *
                   sizeof(*invalid_block_bindings));
  if (local_unit.block_binding_count < 2u ||
      invalid_block_bindings[0].initializer == CTOOL_C_AST_NONE ||
      invalid_block_bindings[1].initializer == CTOOL_C_AST_NONE) {
    goto cleanup;
  }
  invalid_block_bindings[1].initializer =
      invalid_block_bindings[0].initializer;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "duplicate local initializer owner")) {
    goto cleanup;
  }
  (void)memcpy(invalid_block_bindings, local_unit.block_bindings,
               (size_t)local_unit.block_binding_count *
                   sizeof(*invalid_block_bindings));
  dangling_initializer_element.subobject = 0u;
  dangling_initializer_element.initializer =
      invalid_block_bindings[1].initializer;
  invalid_block_bindings[1].initializer = CTOOL_C_AST_NONE;
  invalid_unit = local_unit;
  invalid_unit.block_bindings = invalid_block_bindings;
  invalid_unit.initializer_elements = &dangling_initializer_element;
  invalid_unit.initializer_element_count = 1u;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "dangling initializer owner")) {
    goto cleanup;
  }
  invalid_initializers = (ctool_c_initializer_t *)malloc(
      (size_t)local_unit.initializer_count *
      sizeof(*invalid_initializers));
  if (invalid_initializers == NULL) {
    goto cleanup;
  }
  (void)memcpy(invalid_initializers, local_unit.initializers,
               (size_t)local_unit.initializer_count *
                   sizeof(*invalid_initializers));
  invalid_initializers[local_unit.block_bindings[0].initializer].expression =
      local_unit.expression_count;
  invalid_unit = local_unit;
  invalid_unit.initializers = invalid_initializers;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "invalid local initializer expression")) {
    goto cleanup;
  }
  (void)memcpy(invalid_initializers, local_unit.initializers,
               (size_t)local_unit.initializer_count *
                   sizeof(*invalid_initializers));
  invalid_initializers[local_unit.block_bindings[0].initializer]
      .first_element = 0u;
  invalid_initializers[local_unit.block_bindings[0].initializer]
      .element_count = 1u;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "invalid expression initializer payload")) {
    goto cleanup;
  }
  invalid_layouts = (ctool_c_type_layout_t *)malloc(
      (size_t)local_unit.layout.type_count * sizeof(*invalid_layouts));
  if (invalid_layouts == NULL ||
      local_unit.block_bindings[0].type >= local_unit.layout.type_count) {
    goto cleanup;
  }
  (void)memcpy(invalid_layouts, local_unit.layout.types,
               (size_t)local_unit.layout.type_count *
                   sizeof(*invalid_layouts));
  invalid_layouts[local_unit.block_bindings[0].type].alignment = 0u;
  invalid_unit = local_unit;
  invalid_unit.layout.types = invalid_layouts;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "invalid local layout")) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&local_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &local_unit, &ir);
  if (!check_status(status, CTOOL_OK, "active VGA local lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&local_unit) != fingerprint ||
      !validate_local_ir(&local_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (!parse_source(job, "/point-of-declaration.c",
                    point_of_declaration_source,
                    &point_of_declaration_unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&point_of_declaration_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &point_of_declaration_unit, &ir);
  if (!check_status(status, CTOOL_OK, "point-of-declaration lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&point_of_declaration_unit) != fingerprint ||
      !validate_point_of_declaration_ir(&point_of_declaration_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  invalid_expressions = (ctool_c_expression_t *)malloc(
      (size_t)point_of_declaration_unit.expression_count *
      sizeof(*invalid_expressions));
  if (invalid_expressions == NULL) {
    goto cleanup;
  }
  (void)memcpy(invalid_expressions, point_of_declaration_unit.expressions,
               (size_t)point_of_declaration_unit.expression_count *
                   sizeof(*invalid_expressions));
  for (index = 0u; index < point_of_declaration_unit.expression_count;
       index++) {
    if (invalid_expressions[index].kind ==
            CTOOL_C_EXPRESSION_BLOCK_BINDING &&
        invalid_expressions[index].reference ==
            find_block_binding(&point_of_declaration_unit, "first")) {
      invalid_expressions[index].reference =
          find_block_binding(&point_of_declaration_unit, "second");
      break;
    }
  }
  invalid_unit = point_of_declaration_unit;
  invalid_unit.expressions = invalid_expressions;
  if (index == point_of_declaration_unit.expression_count ||
      !expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "forward local reference")) {
    goto cleanup;
  }
  if (!parse_source(job, "/void-initializer.c", void_initializer_source,
                    &void_initializer_unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&void_initializer_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &void_initializer_unit, &ir);
  if (!check_status(status, CTOOL_OK, "qualified local lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&void_initializer_unit) != fingerprint ||
      ir.function_count != 4u) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  void_initializers = (ctool_c_initializer_t *)malloc(
      (size_t)void_initializer_unit.initializer_count *
      sizeof(*void_initializers));
  if (void_initializers == NULL ||
      void_initializer_unit.block_binding_count != 2u ||
      void_initializer_unit.block_bindings[0].initializer >=
          void_initializer_unit.initializer_count) {
    goto cleanup;
  }
  (void)memcpy(void_initializers, void_initializer_unit.initializers,
               (size_t)void_initializer_unit.initializer_count *
                   sizeof(*void_initializers));
  for (index = 0u; index < void_initializer_unit.expression_count; index++) {
    if (void_initializer_unit.expressions[index].kind ==
        CTOOL_C_EXPRESSION_CALL) {
      break;
    }
  }
  if (index == void_initializer_unit.expression_count) {
    goto cleanup;
  }
  void_initializers[void_initializer_unit.block_bindings[0].initializer]
      .expression = index;
  invalid_unit = void_initializer_unit;
  invalid_unit.initializers = void_initializers;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "void expression local initializer")) {
    goto cleanup;
  }
  (void)memcpy(void_initializers, void_initializer_unit.initializers,
               (size_t)void_initializer_unit.initializer_count *
                   sizeof(*void_initializers));
  for (index = 0u; index < void_initializer_unit.expression_count; index++) {
    if (void_initializer_unit.expressions[index].kind ==
            CTOOL_C_EXPRESSION_INTEGER_CONSTANT &&
        void_initializer_unit.expressions[index].type !=
            void_initializers[void_initializer_unit.block_bindings[0]
                                  .initializer]
                .type) {
      break;
    }
  }
  if (index == void_initializer_unit.expression_count) {
    goto cleanup;
  }
  void_initializers[void_initializer_unit.block_bindings[0].initializer]
      .expression = index;
  invalid_unit.initializers = void_initializers;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "unconverted local initializer")) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&addition_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &addition_unit, &ir);
  if (!check_status(status, CTOOL_OK, "active addition lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&addition_unit) != fingerprint ||
      !validate_addition_ir(&addition_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }

  fingerprint = unit_fingerprint(&multiplication_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &multiplication_unit, &ir);
  if (!check_status(status, CTOOL_OK, "active Paint multiplication lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&multiplication_unit) != fingerprint ||
      !validate_paint_multiplication_ir(&multiplication_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }

  fingerprint = unit_fingerprint(&unsigned_multiplication_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &unsigned_multiplication_unit, &ir);
  if (!check_status(status, CTOOL_OK, "unsigned multiplication lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&unsigned_multiplication_unit) != fingerprint ||
      !validate_unsigned_multiplication_ir(&unsigned_multiplication_unit,
                                           &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }

  fingerprint = unit_fingerprint(&file_assignment_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &file_assignment_unit, &ir);
  if (!check_status(status, CTOOL_OK, "active VGA file assignment lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&file_assignment_unit) != fingerprint ||
      !validate_file_assignment_ir(&file_assignment_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }

  fingerprint = unit_fingerprint(&file_member_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &file_member_unit, &ir);
  if (!check_status(status, CTOOL_OK, "active timer member lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&file_member_unit) != fingerprint ||
      !validate_file_member_ir(&file_member_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }

  fingerprint = unit_fingerprint(&bit_field_member_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &bit_field_member_unit, &ir);
  if (!check_status(status, CTOOL_OK, "bit-field member lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&bit_field_member_unit) != fingerprint ||
      !validate_bit_field_ir(&bit_field_member_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  index = find_member(&bit_field_member_unit, "r");
  bit_field_layouts = (ctool_c_member_layout_t *)malloc(
      (size_t)bit_field_member_unit.layout.member_count *
      sizeof(*bit_field_layouts));
  bit_field_members = (ctool_c_record_member_t *)malloc(
      (size_t)bit_field_member_unit.graph.member_count *
      sizeof(*bit_field_members));
  if (index == CTOOL_C_AST_NONE || bit_field_layouts == NULL ||
      bit_field_members == NULL) {
    goto cleanup;
  }
  (void)memcpy(bit_field_layouts, bit_field_member_unit.layout.members,
               (size_t)bit_field_member_unit.layout.member_count *
                   sizeof(*bit_field_layouts));
  bit_field_layouts[index].bit_offset = 31u;
  invalid_unit = bit_field_member_unit;
  invalid_unit.layout.members = bit_field_layouts;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "bit-field range outside storage unit")) {
    goto cleanup;
  }
  (void)memcpy(bit_field_layouts, bit_field_member_unit.layout.members,
               (size_t)bit_field_member_unit.layout.member_count *
                   sizeof(*bit_field_layouts));
  bit_field_layouts[index].byte_offset = 0xffffffffu;
  invalid_unit.layout.members = bit_field_layouts;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "bit-field storage outside record")) {
    goto cleanup;
  }
  (void)memcpy(bit_field_members, bit_field_member_unit.graph.members,
               (size_t)bit_field_member_unit.graph.member_count *
                   sizeof(*bit_field_members));
  bit_field_members[index].bit_width++;
  invalid_unit = bit_field_member_unit;
  invalid_unit.graph.members = bit_field_members;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "bit-field graph and layout width mismatch")) {
    goto cleanup;
  }
  for (index = 0u; index < file_member_unit.expression_count; index++) {
    if (file_member_unit.expressions[index].kind ==
        CTOOL_C_EXPRESSION_MEMBER) {
      break;
    }
  }
  member_expressions = (ctool_c_expression_t *)malloc(
      (size_t)file_member_unit.expression_count *
      sizeof(*member_expressions));
  member_layouts = (ctool_c_member_layout_t *)malloc(
      (size_t)file_member_unit.layout.member_count *
      sizeof(*member_layouts));
  if (index == file_member_unit.expression_count ||
      member_expressions == NULL || member_layouts == NULL ||
      file_member_unit.layout.member_count == 0u) {
    goto cleanup;
  }
  (void)memcpy(member_expressions, file_member_unit.expressions,
               (size_t)file_member_unit.expression_count *
                   sizeof(*member_expressions));
  member_expressions[index].reference = file_member_unit.graph.member_count;
  invalid_unit = file_member_unit;
  invalid_unit.expressions = member_expressions;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "out-of-range member identity")) {
    goto cleanup;
  }
  (void)memcpy(member_layouts, file_member_unit.layout.members,
               (size_t)file_member_unit.layout.member_count *
                   sizeof(*member_layouts));
  member_layouts[file_member_unit.expressions[index].reference].byte_offset =
      0xffffffffu;
  invalid_unit = file_member_unit;
  invalid_unit.layout.members = member_layouts;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "member layout outside its record")) {
    goto cleanup;
  }

  for (index = 0u; index < file_assignment_unit.expression_count; index++) {
    if (file_assignment_unit.expressions[index].kind ==
        CTOOL_C_EXPRESSION_ASSIGNMENT) {
      break;
    }
  }
  assignment_expressions = (ctool_c_expression_t *)malloc(
      (size_t)file_assignment_unit.expression_count *
      sizeof(*assignment_expressions));
  assignment_children = (ctool_u32 *)malloc(
      (size_t)file_assignment_unit.expression_child_count *
      sizeof(*assignment_children));
  if (index == file_assignment_unit.expression_count ||
      assignment_expressions == NULL || assignment_children == NULL ||
      file_assignment_unit.expressions[index].child_count != 2u ||
      file_assignment_unit.expressions[index].first_child >
          file_assignment_unit.expression_child_count ||
      2u > file_assignment_unit.expression_child_count -
               file_assignment_unit.expressions[index].first_child) {
    goto cleanup;
  }
  (void)memcpy(assignment_expressions, file_assignment_unit.expressions,
               (size_t)file_assignment_unit.expression_count *
                   sizeof(*assignment_expressions));
  assignment_expressions[index].computation_type =
      file_assignment_unit.function_definitions[0].declared_type;
  invalid_unit = file_assignment_unit;
  invalid_unit.expressions = assignment_expressions;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "mismatched assignment computation type")) {
    goto cleanup;
  }
  (void)memcpy(assignment_children,
               file_assignment_unit.expression_children,
               (size_t)file_assignment_unit.expression_child_count *
                   sizeof(*assignment_children));
  assignment_children[file_assignment_unit.expressions[index].first_child] =
      file_assignment_unit.expression_count;
  invalid_unit = file_assignment_unit;
  invalid_unit.expression_children = assignment_children;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "out-of-range assignment child")) {
    goto cleanup;
  }
  (void)memcpy(assignment_children,
               file_assignment_unit.expression_children,
               (size_t)file_assignment_unit.expression_child_count *
                   sizeof(*assignment_children));
  assignment_children[file_assignment_unit.expressions[index].first_child] =
      assignment_children[file_assignment_unit.expressions[index].first_child +
                          1u];
  invalid_unit = file_assignment_unit;
  invalid_unit.expression_children = assignment_children;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "value-producing assignment destination")) {
    goto cleanup;
  }

  fingerprint = unit_fingerprint(&chained_assignment_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &chained_assignment_unit, &ir);
  if (!check_status(status, CTOOL_OK, "chained assignment lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&chained_assignment_unit) != fingerprint ||
      !validate_chained_assignment_ir(&chained_assignment_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }

  fingerprint = unit_fingerprint(&local_parameter_assignment_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &local_parameter_assignment_unit, &ir);
  if (!check_status(status, CTOOL_OK,
                    "local/parameter assignment lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&local_parameter_assignment_unit) != fingerprint ||
      !validate_local_parameter_assignment_ir(
          &local_parameter_assignment_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }

  call_fixture = make_call_fixture();
  if (call_fixture == NULL ||
      !parse_source(job, "/active-direct-calls.c", call_fixture,
                    &call_unit)) {
    (void)fprintf(stderr, "active call setup failed\n");
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&call_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &call_unit, &ir);
  if (!check_status(status, CTOOL_OK, "active direct call lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&call_unit) != fingerprint ||
      !validate_call_ir(&call_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&active_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &active_unit, &ir);
  if (!check_status(status, CTOOL_OK, "active helper lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&active_unit) != fingerprint ||
      !validate_active_ir(&active_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&logic_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &logic_unit, &ir);
  if (!check_status(status, CTOOL_OK, "active logic helper lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&logic_unit) != fingerprint ||
      !validate_logic_ir(&logic_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&division_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &division_unit, &ir);
  if (!check_status(status, CTOOL_OK, "active division helper lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&division_unit) != fingerprint ||
      !validate_division_ir(&division_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&logical_or_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &logical_or_unit, &ir);
  if (!check_status(status, CTOOL_OK, "active logical-or helper lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&logical_or_unit) != fingerprint ||
      !validate_logical_or_ir(&logical_or_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&branch_fit_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &branch_fit_unit, &ir);
  if (!check_status(status, CTOOL_OK,
                    "active branch-range helper lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&branch_fit_unit) != fingerprint ||
      !validate_branch_fit_ir(&branch_fit_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&aes_rotw_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &aes_rotw_unit, &ir);
  if (!check_status(status, CTOOL_OK, "active AES word-rotation lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&aes_rotw_unit) != fingerprint ||
      !validate_aes_rotw_ir(&aes_rotw_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&simd_cpuid_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &simd_cpuid_unit, &ir);
  if (!check_status(status, CTOOL_OK, "active CPUID toggle lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&simd_cpuid_unit) != fingerprint ||
      !validate_simd_cpuid_ir(&simd_cpuid_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  xor_expression = CTOOL_C_AST_NONE;
  comparison_expression = CTOOL_C_AST_NONE;
  for (index = 0u; index < simd_cpuid_unit.expression_count; index++) {
    const ctool_c_expression_t *expression =
        &simd_cpuid_unit.expressions[index];
    if (expression->kind == CTOOL_C_EXPRESSION_BINARY &&
        expression->operation == CTOOL_C_EXPRESSION_OPERATOR_BITWISE_XOR) {
      xor_expression = index;
    } else if (expression->kind == CTOOL_C_EXPRESSION_BINARY &&
               expression->operation ==
                   CTOOL_C_EXPRESSION_OPERATOR_NOT_EQUAL) {
      comparison_expression = index;
    }
  }
  cpuid_expressions = (ctool_c_expression_t *)malloc(
      (size_t)simd_cpuid_unit.expression_count * sizeof(*cpuid_expressions));
  if (xor_expression == CTOOL_C_AST_NONE ||
      comparison_expression == CTOOL_C_AST_NONE ||
      simd_cpuid_unit.expressions[xor_expression].type ==
          simd_cpuid_unit.expressions[comparison_expression].type ||
      cpuid_expressions == NULL) {
    (void)fprintf(stderr, "CPUID toggle invalid-unit setup failed\n");
    goto cleanup;
  }
  (void)memcpy(cpuid_expressions, simd_cpuid_unit.expressions,
               (size_t)simd_cpuid_unit.expression_count *
                   sizeof(*cpuid_expressions));
  cpuid_expressions[xor_expression].type =
      simd_cpuid_unit.expressions[comparison_expression].type;
  invalid_unit = simd_cpuid_unit;
  invalid_unit.expressions = cpuid_expressions;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "mismatched XOR result type") ||
      unit_fingerprint(&simd_cpuid_unit) != fingerprint) {
    goto cleanup;
  }

  if (!parse_source(job, "/simple-leaves.c", simple_source, &simple_unit)) {
    goto cleanup;
  }

  if (!parse_source(job, "/inline-success.c", inline_success_source,
                    &inline_success_unit)) {
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &inline_success_unit, &ir);
  if (!check_status(status, CTOOL_OK,
                    "static inline and later non-inline definition lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      !validate_inline_ir(&inline_success_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }

  if (!parse_source(job, "/external-inline.c", external_inline_source,
                    &external_inline_unit) ||
      !expect_ir_failure(
          job, &external_inline_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_EXTERNAL_INLINE,
          "CupidC IR lowering requires external-inline finalization before "
          "lowering this definition",
          "external inline definition") ||
      !parse_source(job, "/extern-inline.c", extern_inline_source,
                    &extern_inline_unit) ||
      !expect_ir_failure(
          job, &extern_inline_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_EXTERNAL_INLINE,
          "CupidC IR lowering requires external-inline finalization before "
          "lowering this definition",
          "extern inline definition")) {
    goto cleanup;
  }
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &simple_unit, &ir);
  if (!check_status(status, CTOOL_OK, "simple leaf lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      !validate_simple_ir(&simple_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }

  invalid_unit = active_unit;
  invalid_definition = active_unit.function_definitions[0];
  invalid_definition.body = active_unit.statement_count;
  invalid_unit.function_definitions = &invalid_definition;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "invalid function body")) {
    goto cleanup;
  }

  if (!parse_source(job, "/unsupported-statement.c", statement_source,
                    &statement_unit) ||
      !expect_ir_failure(
          job, &statement_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_STATEMENT,
          "CupidC IR lowering does not yet support this statement",
          "unsupported statement")) {
    goto cleanup;
  }
  if (!parse_source(job, "/wide-local.c", wide_local_source,
                    &wide_local_unit) ||
      !expect_ir_failure(
          job, &wide_local_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
           "CupidC IR lowering does not yet support this value type",
           "wide automatic local") ||
      !parse_source(job, "/short-local.c", short_local_source,
                    &short_local_unit) ||
      !expect_ir_failure(
          job, &short_local_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "narrow automatic local") ||
      !parse_source_mode(job, "/overaligned-local.c",
                         overaligned_local_source, CTOOL_TRUE,
                         &overaligned_local_unit) ||
      !expect_ir_failure(
          job, &overaligned_local_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "over-aligned automatic local") ||
      !parse_source(job, "/array-local.c", array_local_source,
                    &array_local_unit) ||
      !expect_ir_failure(
          job, &array_local_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "aggregate automatic local") ||
      !parse_source(job, "/static-local.c", static_local_source,
                    &static_local_unit) ||
      !expect_ir_failure(
          job, &static_local_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_STATEMENT,
          "CupidC IR lowering does not yet support this statement",
          "static block local")) {
    goto cleanup;
  }
  if (!parse_source(job, "/local-ownership.c", ownership_source,
                    &ownership_unit)) {
    goto cleanup;
  }
  invalid_statements = (ctool_c_statement_t *)malloc(
      (size_t)ownership_unit.statement_count * sizeof(*invalid_statements));
  if (invalid_statements == NULL) {
    goto cleanup;
  }
  (void)memcpy(invalid_statements, ownership_unit.statements,
               (size_t)ownership_unit.statement_count *
                   sizeof(*invalid_statements));
  for (index = 0u; index < ownership_unit.statement_count; index++) {
    if (invalid_statements[index].kind == CTOOL_C_STATEMENT_DECLARATION &&
        invalid_statements[index].first_block_binding == 1u) {
      invalid_statements[index].first_block_binding = 0u;
      break;
    }
  }
  invalid_unit = ownership_unit;
  invalid_unit.statements = invalid_statements;
  if (index == ownership_unit.statement_count ||
      !expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "duplicate local ownership")) {
    goto cleanup;
  }
  ownership_expressions = (ctool_c_expression_t *)malloc(
      (size_t)ownership_unit.expression_count *
      sizeof(*ownership_expressions));
  if (ownership_expressions == NULL) {
    goto cleanup;
  }
  (void)memcpy(ownership_expressions, ownership_unit.expressions,
               (size_t)ownership_unit.expression_count *
                   sizeof(*ownership_expressions));
  for (index = 0u; index < ownership_unit.expression_count; index++) {
    if (ownership_expressions[index].kind ==
            CTOOL_C_EXPRESSION_BLOCK_BINDING &&
        ownership_expressions[index].reference ==
            find_block_binding(&ownership_unit, "two")) {
      ownership_expressions[index].reference =
          find_block_binding(&ownership_unit, "one");
      break;
    }
  }
  invalid_unit = ownership_unit;
  invalid_unit.expressions = ownership_expressions;
  if (index == ownership_unit.expression_count ||
      !expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "cross-function local reference")) {
    goto cleanup;
  }
  if (!parse_source(job, "/active-vga-flip-ready.c",
                    global_frontier_source, &global_frontier_unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&global_frontier_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &global_frontier_unit, &ir);
  if (!check_status(status, CTOOL_OK,
                    "active VGA flip readiness lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&global_frontier_unit) != fingerprint ||
      !validate_file_object_load_ir(&global_frontier_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (!parse_source(job, "/external-file-object.c",
                    external_file_object_source,
                    &external_file_object_unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&external_file_object_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &external_file_object_unit, &ir);
  if (!check_status(status, CTOOL_OK, "external file-object lowering") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&external_file_object_unit) != fingerprint ||
      !validate_external_file_object_ir(&external_file_object_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  file_expressions = (ctool_c_expression_t *)malloc(
      (size_t)external_file_object_unit.expression_count *
      sizeof(*file_expressions));
  if (file_expressions == NULL) {
    goto cleanup;
  }
  (void)memcpy(file_expressions, external_file_object_unit.expressions,
               (size_t)external_file_object_unit.expression_count *
                   sizeof(*file_expressions));
  for (index = 0u; index < external_file_object_unit.expression_count;
       index++) {
    if (file_expressions[index].kind == CTOOL_C_EXPRESSION_IDENTIFIER &&
        file_expressions[index].reference ==
            find_binding(&external_file_object_unit, "external_clock")) {
      break;
    }
  }
  invalid_unit = external_file_object_unit;
  invalid_unit.expressions = file_expressions;
  if (index == external_file_object_unit.expression_count) {
    goto cleanup;
  }
  file_expressions[index].reference = external_file_object_unit.binding_count;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "out-of-range file-object binding")) {
    goto cleanup;
  }
  file_expressions[index].reference =
      find_binding(&external_file_object_unit, "read_external_clock");
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "mismatched file-object binding")) {
    goto cleanup;
  }
  file_expressions[index] = external_file_object_unit.expressions[index];
  file_expressions[index].first_child = 0u;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "invalid file-object expression payload")) {
    goto cleanup;
  }
  file_bindings = (ctool_c_binding_t *)malloc(
      (size_t)external_file_object_unit.binding_count *
      sizeof(*file_bindings));
  file_binding = find_binding(&external_file_object_unit, "external_clock");
  if (file_bindings == NULL ||
      file_binding >= external_file_object_unit.binding_count) {
    goto cleanup;
  }
  (void)memcpy(file_bindings, external_file_object_unit.bindings,
               (size_t)external_file_object_unit.binding_count *
                   sizeof(*file_bindings));
  file_bindings[file_binding].linkage = CTOOL_C_LINKAGE_INTERNAL;
  invalid_unit = external_file_object_unit;
  invalid_unit.bindings = file_bindings;
  if (!expect_ir_failure(
          job, &invalid_unit, CTOOL_ERR_INPUT,
          CTOOL_C_IR_DIAG_INVALID_UNIT,
          "CupidC IR lowering received an invalid translation unit",
          "undefined internal file object")) {
    goto cleanup;
  }
  if (!parse_source(job, "/narrow-file-object.c",
                    narrow_file_object_source, &narrow_file_object_unit) ||
      !expect_ir_failure(
          job, &narrow_file_object_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "narrow file-object load") ||
      !parse_source(job, "/enumerator-identifier.c",
                    enumerator_identifier_source,
                    &enumerator_identifier_unit) ||
      !expect_ir_failure(
          job, &enumerator_identifier_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_EXPRESSION,
          "CupidC IR lowering does not yet support this expression",
          "enumerator identifier")) {
    goto cleanup;
  }
  if (!parse_source(job, "/unsupported-expression.c", expression_source,
                    &expression_unit) ||
      !expect_ir_failure(
          job, &expression_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_EXPRESSION,
          "CupidC IR lowering does not yet support this expression",
          "unsupported expression")) {
    goto cleanup;
  }
  if (!parse_source(job, "/wide-comparison.c", wide_comparison_source,
                    &wide_comparison_unit) ||
      !expect_ir_failure(
          job, &wide_comparison_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide less-than-or-equal expression")) {
    goto cleanup;
  }
  if (!parse_source(job, "/wide-logical-or.c", wide_logical_or_source,
                    &wide_logical_or_unit) ||
      !expect_ir_failure(
          job, &wide_logical_or_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide logical-or operand")) {
    goto cleanup;
  }
  if (!parse_source(job, "/wide-multiplication.c",
                    wide_multiplication_source,
                    &wide_multiplication_unit) ||
      !expect_ir_failure(
          job, &wide_multiplication_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide multiplication expression")) {
    goto cleanup;
  }
  if (!parse_source(job, "/wide-left-shift.c", wide_left_shift_source,
                    &wide_left_shift_unit) ||
      !expect_ir_failure(
          job, &wide_left_shift_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide left-shift expression") ||
      !parse_source(job, "/wide-right-shift.c", wide_right_shift_source,
                    &wide_right_shift_unit) ||
      !expect_ir_failure(
          job, &wide_right_shift_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide right-shift expression") ||
      !parse_source(job, "/wide-bitwise-or.c", wide_bitwise_or_source,
                    &wide_bitwise_or_unit) ||
      !expect_ir_failure(
          job, &wide_bitwise_or_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide bitwise-or expression") ||
      !parse_source(job, "/wide-bitwise-xor.c", wide_bitwise_xor_source,
                    &wide_bitwise_xor_unit) ||
      !expect_ir_failure(
          job, &wide_bitwise_xor_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide bitwise-xor expression")) {
    goto cleanup;
  }
  if (!parse_source(job, "/wide-division.c", wide_division_source,
                    &wide_division_unit) ||
      !expect_ir_failure(
          job, &wide_division_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide division expression") ||
      !parse_source(job, "/wide-remainder.c", wide_remainder_source,
                    &wide_remainder_unit) ||
      !expect_ir_failure(
          job, &wide_remainder_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_TYPE,
          "CupidC IR lowering does not yet support this value type",
          "wide remainder expression")) {
    goto cleanup;
  }
  if (!parse_source(job, "/unsupported-conversion.c", conversion_source,
                    &conversion_unit) ||
      !expect_ir_failure(
          job, &conversion_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_CONVERSION,
          "CupidC IR lowering does not yet support this conversion",
          "unsupported explicit conversion")) {
    goto cleanup;
  }
  if (!parse_source(job, "/indirect-call.c", indirect_call_source,
                    &indirect_call_unit) ||
      !expect_ir_failure(
          job, &indirect_call_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_UNSUPPORTED_EXPRESSION,
          "CupidC IR lowering does not yet support this expression",
          "indirect call")) {
    goto cleanup;
  }
  if (!parse_source(job, "/wide-call.c", wide_call_source,
                    &wide_call_unit) ||
      !expect_ir_failure(
          job, &wide_call_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_ABI,
          "CupidC IR lowering supports only fixed, nonvariadic direct calls "
          "with 32-bit integer arguments and void or 32-bit integer results",
          "wide direct call")) {
    goto cleanup;
  }
  if (!parse_source(job, "/variadic-call.c", variadic_call_source,
                    &variadic_call_unit) ||
      !expect_ir_failure(
          job, &variadic_call_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_ABI,
          "CupidC IR lowering supports only fixed, nonvariadic direct calls "
          "with 32-bit integer arguments and void or 32-bit integer results",
          "variadic direct call")) {
    goto cleanup;
  }
  if (!parse_source(job, "/value-statement.c", value_statement_source,
                    &value_statement_unit)) {
    goto cleanup;
  }
  fingerprint = unit_fingerprint(&value_statement_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &value_statement_unit, &ir);
  if (!check_status(status, CTOOL_OK, "nonvoid expression statement") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&value_statement_unit) != fingerprint ||
      !validate_discard_ir(&value_statement_unit, &ir)) {
    (void)ctool_job_render_diagnostics(job);
    goto cleanup;
  }
  if (!parse_source(job, "/unsupported-abi.c", abi_source, &abi_unit) ||
      !expect_ir_failure(
          job, &abi_unit, CTOOL_ERR_UNSUPPORTED,
          CTOOL_C_IR_DIAG_ABI,
          "CupidC IR lowering supports only fixed, nonvariadic cdecl functions "
          "with 32-bit integer parameters and void or 32-bit integer results",
          "unsupported ABI")) {
    goto cleanup;
  }

  if (!open_limited_job(host_root, &limited_adapter, &limited_job) ||
      !expect_ir_failure(
          limited_job, &active_unit, CTOOL_ERR_LIMIT,
          CTOOL_C_IR_DIAG_LIMIT,
          "CupidC IR lowering exceeded a configured resource limit",
          "limited IR storage")) {
    goto cleanup;
  }

  fingerprint = unit_fingerprint(&active_unit);
  diagnostic_count = ctool_job_diagnostic_count(job);
  (void)memset(&ir, 0xa5, sizeof(ir));
  status = ctool_c_lower_ir(job, &active_unit, &ir);
  if (!check_status(status, CTOOL_OK, "same-job recovery") ||
      ctool_job_diagnostic_count(job) != diagnostic_count ||
      unit_fingerprint(&active_unit) != fingerprint ||
      !validate_active_ir(&active_unit, &ir)) {
    (void)fprintf(stderr, "same-job recovery differs\n");
    goto cleanup;
  }
  passed = 1;

cleanup:
  if (limited_job != NULL) {
    ctool_job_close(limited_job);
  }
  if (job != NULL) {
    ctool_job_close(job);
  }
  free(fixture);
  free(logic_fixture);
  free(division_fixture);
  free(logical_or_fixture);
  free(branch_fit_fixture);
  free(aes_rotw_fixture);
  free(simd_cpuid_fixture);
  free(call_fixture);
  free(invalid_statements);
  free(invalid_initializers);
  free(void_initializers);
  free(invalid_expressions);
  free(cpuid_expressions);
  free(ownership_expressions);
  free(file_expressions);
  free(assignment_expressions);
  free(assignment_children);
  free(member_expressions);
  free(member_layouts);
  free(bit_field_layouts);
  free(bit_field_members);
  free(file_bindings);
  free(invalid_layouts);
  free(invalid_block_bindings);
  if (passed != 0) {
    (void)puts("active-leaf: ok");
    return 0;
  }
  return 1;
}

int main(int argc, char **argv) {
  if (argc == 3 && strcmp(argv[1], "active-leaf") == 0) {
    return run_active_leaf(argv[2]);
  }
  (void)fprintf(stderr,
                "usage: cupidc-ir-contract active-leaf HOST_ROOT\n");
  return 2;
}
