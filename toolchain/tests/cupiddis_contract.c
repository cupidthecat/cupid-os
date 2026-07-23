#include "ctool.h"
#include "ctool_host.h"
#include "cupiddis.h"
#include "elf32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  char bytes[32768];
  ctool_u32 size;
  ctool_u32 fail_after;
  ctool_job_t *emit_job;
  ctool_bool emitted;
} capture_t;

static const char active_boot_initial_mode[] =
    " [org 0x7c00]\n"
    "[bits 16]\n";

static const char active_boot_mode_transition[] =
    "; 32-bit protected mode entry\n"
    "[bits 32]\n"
    "init_pm:\n"
    "    mov ax, DATA_SEG\n"
    "    mov ds, ax\n"
    "    mov es, ax\n"
    "    mov fs, ax\n"
    "    mov gs, ax\n"
    "    mov ss, ax\n";

static const char active_boot_mode_return[] =
    "; GDT\n"
    "[bits 16]\n"
    "gdt_start:\n";

static const char active_smp_initial_mode[] =
    "BITS 16\n"
    "ORG 0x8000\n\n"
    "ap_start:\n";

static const char active_smp_mode_transition[] =
    "times 0x210 - ($ - $$) db 0\n\n"
    "BITS 32\n"
    "pm32:\n"
    "    mov ax, 0x10\n"
    "    mov ds, ax\n"
    "    mov es, ax\n"
    "    mov fs, ax\n"
    "    mov ss, ax\n";

static ctool_status_t capture_write(void *context, ctool_bytes_t text) {
  capture_t *capture = (capture_t *)context;
  if (capture->emit_job != (ctool_job_t *)0 &&
      capture->emitted == CTOOL_FALSE) {
    ctool_diagnostic_t diagnostic;
    ctool_status_t status;
    diagnostic.severity = CTOOL_DIAG_NOTE;
    diagnostic.code = 0x0500fff0u;
    diagnostic.path = ctool_string("/sink");
    diagnostic.line = 0u;
    diagnostic.column = 0u;
    diagnostic.message = ctool_string("sink allocation survives rendering");
    status = ctool_job_emit(capture->emit_job, &diagnostic);
    if (status != CTOOL_OK) {
      return status;
    }
    capture->emitted = CTOOL_TRUE;
  }
  if (capture->fail_after != 0u &&
      capture->size + text.size > capture->fail_after) {
    return CTOOL_ERR_IO;
  }
  if (text.size > (ctool_u32)sizeof(capture->bytes) - capture->size - 1u) {
    return CTOOL_ERR_LIMIT;
  }
  if (text.size != 0u) {
    (void)memcpy(capture->bytes + capture->size, text.data, text.size);
  }
  capture->size += text.size;
  capture->bytes[capture->size] = '\0';
  return CTOOL_OK;
}

static ctool_status_t invalid_sink_write(void *context, ctool_bytes_t text) {
  (void)context;
  (void)text;
  return CTOOL_ERR_INVALID_ARGUMENT;
}

static ctool_text_sink_t capture_sink(capture_t *capture) {
  ctool_text_sink_t sink;
  sink.context = capture;
  sink.write = capture_write;
  return sink;
}

static int check_status(ctool_status_t actual, ctool_status_t expected,
                        const char *operation) {
  if (actual != expected) {
    (void)fprintf(stderr, "%s: expected %s, got %s\n", operation,
                  ctool_status_name(expected), ctool_status_name(actual));
    return 0;
  }
  return 1;
}

static int contains(const capture_t *capture, const char *needle,
                    const char *operation) {
  if (strstr(capture->bytes, needle) == (char *)0) {
    (void)fprintf(stderr, "%s: missing `%s` in:\n%s", operation, needle,
                  capture->bytes);
    return 0;
  }
  return 1;
}

static int is_zeroed(const void *value, size_t size) {
  const unsigned char *bytes = (const unsigned char *)value;
  size_t index;
  for (index = 0u; index < size; index++) {
    if (bytes[index] != 0u) {
      return 0;
    }
  }
  return 1;
}

static int source_contains_fragment(ctool_bytes_t source,
                                    const char *fragment) {
  ctool_string_t expected = ctool_string(fragment);
  ctool_u32 start;
  for (start = 0u; start < source.size; start++) {
    ctool_u32 source_index = start;
    ctool_u32 expected_index = 0u;
    while (source_index < source.size && expected_index < expected.size) {
      if (source.data[source_index] == (ctool_u8)'\r' &&
          source_index + 1u < source.size &&
          source.data[source_index + 1u] == (ctool_u8)'\n' &&
          expected.data[expected_index] == '\n') {
        source_index++;
      }
      if (source.data[source_index] !=
          (ctool_u8)(unsigned char)expected.data[expected_index]) {
        break;
      }
      source_index++;
      expected_index++;
    }
    if (expected_index == expected.size) {
      return 1;
    }
  }
  return expected.size == 0u ? 1 : 0;
}

static int check_diagnostic(const ctool_job_t *job, ctool_u32 index,
                            ctool_u32 code, const char *message,
                            const char *operation) {
  const ctool_diagnostic_t *diagnostic = ctool_job_diagnostic(job, index);
  if (diagnostic == (const ctool_diagnostic_t *)0 ||
      diagnostic->code != code ||
      strcmp(diagnostic->message.data, message) != 0) {
    (void)fprintf(stderr, "%s: diagnostic differs\n", operation);
    return 0;
  }
  return 1;
}

static int open_job(ctool_host_adapter_t *adapter, ctool_job_t **job) {
  ctool_job_config_t config;
  ctool_status_t status = ctool_host_adapter_init(adapter, ".");
  if (!check_status(status, CTOOL_OK, "host adapter init")) {
    return 0;
  }
  config = ctool_host_job_config(adapter, ctool_default_limits());
  status = ctool_job_open(&config, job);
  return check_status(status, CTOOL_OK, "job open");
}

static int active_mode_transitions_are_unchanged(void) {
  static const char *const roots[] = {".", ".."};
  ctool_u32 root_index;
  for (root_index = 0u;
       root_index < (ctool_u32)(sizeof(roots) / sizeof(roots[0]));
       root_index++) {
    ctool_host_adapter_t adapter;
    ctool_job_config_t config;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_path_t path;
    ctool_source_t boot_source;
    ctool_source_t smp_source;
    ctool_status_t status = ctool_host_adapter_init(&adapter,
                                                     roots[root_index]);
    if (status == CTOOL_OK) {
      config = ctool_host_job_config(&adapter, ctool_default_limits());
      status = ctool_job_open(&config, &job);
    }
    path.text = ctool_string("/boot/boot.asm");
    if (status == CTOOL_OK) {
      status = ctool_job_load_source(job, &path, &boot_source);
    }
    path.text = ctool_string("/kernel/smp/smp_trampoline.S");
    if (status == CTOOL_OK) {
      status = ctool_job_load_source(job, &path, &smp_source);
    }
    if (status == CTOOL_OK) {
      int boot_initial = source_contains_fragment(
          boot_source.contents, active_boot_initial_mode);
      int boot_transition = source_contains_fragment(
          boot_source.contents, active_boot_mode_transition);
      int boot_return = source_contains_fragment(
          boot_source.contents, active_boot_mode_return);
      int smp_initial = source_contains_fragment(
          smp_source.contents, active_smp_initial_mode);
      int smp_transition = source_contains_fragment(
          smp_source.contents, active_smp_mode_transition);
      ctool_job_close(job);
      if (boot_initial != 0 && boot_transition != 0 && boot_return != 0 &&
          smp_initial != 0 && smp_transition != 0) {
        return 1;
      }
      (void)fprintf(stderr, "active raw-mode transition guard changed: "
                            "boot=%d/%d/%d smp=%d/%d\n",
                    boot_initial, boot_transition, boot_return, smp_initial,
                    smp_transition);
      return 0;
    }
    if (job != (ctool_job_t *)0) {
      ctool_job_close(job);
    }
  }
  (void)fprintf(stderr, "cannot load active raw-mode transition sources\n");
  return 0;
}

static ctool_dis_request_t raw_request(ctool_x86_mode_t mode,
                                       ctool_u32 base_address) {
  ctool_dis_request_t request;
  (void)memset(&request, 0, sizeof(request));
  request.input = CTOOL_DIS_INPUT_RAW;
  request.views = CTOOL_DIS_VIEW_DISASSEMBLY;
  request.raw_mode = mode;
  request.raw_base_address = base_address;
  return request;
}

static void put_le16(ctool_u8 *bytes, ctool_u32 offset, ctool_u16 value) {
  bytes[offset] = (ctool_u8)(value & 0xffu);
  bytes[offset + 1u] = (ctool_u8)((value >> 8u) & 0xffu);
}

static void put_le32(ctool_u8 *bytes, ctool_u32 offset, ctool_u32 value) {
  bytes[offset] = (ctool_u8)(value & 0xffu);
  bytes[offset + 1u] = (ctool_u8)((value >> 8u) & 0xffu);
  bytes[offset + 2u] = (ctool_u8)((value >> 16u) & 0xffu);
  bytes[offset + 3u] = (ctool_u8)((value >> 24u) & 0xffu);
}

static ctool_u32 get_le32(const ctool_u8 *bytes, ctool_u32 offset) {
  return (ctool_u32)bytes[offset] |
         ((ctool_u32)bytes[offset + 1u] << 8u) |
         ((ctool_u32)bytes[offset + 2u] << 16u) |
         ((ctool_u32)bytes[offset + 3u] << 24u);
}

static int run_raw(void) {
  static const ctool_u8 raw16[] = {0xb8u, 0x34u, 0x12u, 0xc3u};
  static const ctool_u8 raw32[] = {0xb8u, 0x78u, 0x56u, 0x34u,
                                    0x12u, 0xc3u};
  static const ctool_u8 conditional32[] = {
      0x0fu, 0x40u, 0xc1u, 0x0fu, 0x41u, 0xc1u,
      0x0fu, 0x42u, 0xc1u, 0x0fu, 0x43u, 0xc1u,
      0x0fu, 0x44u, 0xc1u, 0x0fu, 0x45u, 0xc1u,
      0x0fu, 0x46u, 0xc1u, 0x0fu, 0x47u, 0xc1u,
      0x0fu, 0x48u, 0xc1u, 0x0fu, 0x49u, 0xc1u,
      0x0fu, 0x4au, 0xc1u, 0x0fu, 0x4bu, 0xc1u,
      0x0fu, 0x4cu, 0xc1u, 0x0fu, 0x4du, 0xc1u,
      0x0fu, 0x4eu, 0xc1u, 0x0fu, 0x4fu, 0xc1u,
      0x0fu, 0x45u, 0x43u, 0x7fu};
  static const char *const conditional32_text[] = {
      "cmovo eax, ecx",  "cmovno eax, ecx", "cmovb eax, ecx",
      "cmovae eax, ecx", "cmove eax, ecx",  "cmovne eax, ecx",
      "cmovbe eax, ecx", "cmova eax, ecx",  "cmovs eax, ecx",
      "cmovns eax, ecx", "cmovp eax, ecx",  "cmovnp eax, ecx",
      "cmovl eax, ecx",  "cmovge eax, ecx", "cmovle eax, ecx",
      "cmovg eax, ecx"};
  static const ctool_u8 conditional16[] = {
      0x0fu, 0x45u, 0xc1u, 0x66u, 0x0fu, 0x4fu, 0xc1u};
  static const ctool_u8 mixed_mode[] = {
      0xb8u, 0x34u, 0x12u,
      0xb8u, 0x78u, 0x56u, 0x34u, 0x12u,
      0xb8u, 0xcdu, 0xabu, 0xc3u};
  static const ctool_u8 return_cleanup[] = {0xc2u, 0x04u, 0x00u};
  static const ctool_u8 direct[] = {0xa1u, 0u, 0u, 0u, 0xf0u, 0xc3u};
  static const ctool_u8 relative[] = {0xebu, 0u, 0xc3u};
  static const ctool_u8 relative16_short[] = {0xebu, 0u};
  static const ctool_u8 relative16_near[] = {0xe9u, 0u, 0u};
  static const ctool_u8 relative16_wide[] = {0x66u, 0xe9u, 0u,
                                             0u,    0u,    0u};
  static const ctool_u8 recovery[] = {
      0xf0u, 0x0fu, 0x45u, 0xc1u, 0x0fu, 0x4fu};
  ctool_host_adapter_t adapter;
  ctool_job_t *job;
  ctool_source_t source;
  ctool_dis_request_t request;
  ctool_dis_report_t report;
  ctool_dis_label_t label;
  ctool_dis_raw_range_t mixed_ranges[3];
  ctool_dis_raw_range_t invalid_ranges[3];
  capture_t capture;
  capture_t repeat;
  ctool_status_t status;
  ctool_u32 index;
  if (!open_job(&adapter, &job)) {
    return 1;
  }
  if (!active_mode_transitions_are_unchanged()) {
    ctool_job_close(job);
    return 1;
  }

  (void)memset(&capture, 0, sizeof(capture));
  source.path.text = ctool_string("/boot.bin");
  source.contents = ctool_bytes(raw16, (ctool_u32)sizeof(raw16));
  request = raw_request(CTOOL_X86_MODE_16, 0x7c00u);
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (status == CTOOL_OK) {
    status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&capture));
  }
  if (!check_status(status, CTOOL_OK, "raw16 inspection") ||
      !contains(&capture, "[disassembly raw]\n", "raw16 heading") ||
      !contains(&capture, "00007C00", "raw16 base") ||
      !contains(&capture, "mov ax, 0x1234", "raw16 operands") ||
      !contains(&capture, "ret", "raw16 return")) {
    ctool_job_close(job);
    return 1;
  }

  (void)memset(&capture, 0, sizeof(capture));
  source.path.text = ctool_string("/code.bin");
  source.contents = ctool_bytes(raw32, (ctool_u32)sizeof(raw32));
  request = raw_request(CTOOL_X86_MODE_32, 0x00400000u);
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (status == CTOOL_OK) {
    status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&capture));
  }
  if (!check_status(status, CTOOL_OK, "raw32 inspection") ||
      !contains(&capture, "00400000", "raw32 base") ||
      !contains(&capture, "mov eax, 0x12345678", "raw32 operands")) {
    ctool_job_close(job);
    return 1;
  }

  (void)memset(&capture, 0, sizeof(capture));
  source.path.text = ctool_string("/conditional32.bin");
  source.contents = ctool_bytes(
      conditional32, (ctool_u32)sizeof(conditional32));
  request = raw_request(CTOOL_X86_MODE_32, 0x00401000u);
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (status == CTOOL_OK) {
    status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&capture));
  }
  if (!check_status(status, CTOOL_OK,
                    "conditional move raw32 inspection")) {
    ctool_job_close(job);
    return 1;
  }
  for (index = 0u;
       index < (ctool_u32)(sizeof(conditional32_text) /
                           sizeof(conditional32_text[0]));
       index++) {
    if (!contains(&capture, conditional32_text[index],
                  conditional32_text[index])) {
      ctool_job_close(job);
      return 1;
    }
  }
  if (!contains(&capture, "cmovne eax, dword [ebx+0x7F]",
                "conditional move memory source")) {
    ctool_job_close(job);
    return 1;
  }

  (void)memset(&capture, 0, sizeof(capture));
  source.path.text = ctool_string("/conditional16.bin");
  source.contents = ctool_bytes(
      conditional16, (ctool_u32)sizeof(conditional16));
  request = raw_request(CTOOL_X86_MODE_16, 0x00007d00u);
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (status == CTOOL_OK) {
    status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&capture));
  }
  if (!check_status(status, CTOOL_OK,
                    "conditional move raw16 inspection") ||
      !contains(&capture, "cmovne ax, cx",
                "16-bit conditional move") ||
      !contains(&capture, "cmovg eax, ecx",
                "16-bit wide conditional move")) {
    ctool_job_close(job);
    return 1;
  }

  mixed_ranges[0].offset = 0u;
  mixed_ranges[0].mode = CTOOL_X86_MODE_16;
  mixed_ranges[1].offset = 3u;
  mixed_ranges[1].mode = CTOOL_X86_MODE_32;
  mixed_ranges[2].offset = 8u;
  mixed_ranges[2].mode = CTOOL_X86_MODE_16;
  label.address = 0x00007c03u;
  label.name = ctool_string("protected_mode");
  (void)memset(&capture, 0, sizeof(capture));
  source.path.text = ctool_string("/mixed-mode.bin");
  source.contents =
      ctool_bytes(mixed_mode, (ctool_u32)sizeof(mixed_mode));
  request = raw_request(CTOOL_DIS_RAW_MODE_MAP, 0x00007c00u);
  request.raw_ranges = mixed_ranges;
  request.raw_range_count = 3u;
  request.labels = &label;
  request.label_count = 1u;
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (status == CTOOL_OK) {
    status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&capture));
  }
  (void)memset(&repeat, 0, sizeof(repeat));
  if (status == CTOOL_OK) {
    status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&repeat));
  }
  if (!check_status(status, CTOOL_OK, "mixed-mode raw inspection") ||
      report.mode != CTOOL_DIS_RAW_MODE_MAP ||
      report.raw_ranges != mixed_ranges || report.raw_range_count != 3u ||
      !contains(&capture, "00007C00", "mixed-mode 16-bit address") ||
      !contains(&capture, "mov ax, 0x1234", "mixed-mode 16-bit operand") ||
      !contains(&capture, "00007C03 <protected_mode>:",
                "mixed-mode boundary label") ||
      !contains(&capture, "00007C03", "mixed-mode 32-bit address") ||
      !contains(&capture, "mov eax, 0x12345678",
                "mixed-mode 32-bit operand") ||
      !contains(&capture, "00007C08", "mixed-mode return address") ||
      !contains(&capture, "mov ax, 0xABCD",
                "mixed-mode return to 16-bit") ||
      capture.size != repeat.size ||
      memcmp(capture.bytes, repeat.bytes, (size_t)capture.size) != 0) {
    ctool_job_close(job);
    return 1;
  }

  request = raw_request(CTOOL_DIS_RAW_MODE_MAP, 0x00007c00u);
  (void)memset(&report, 0xa5, sizeof(report));
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "zero-range raw mode map") ||
      !is_zeroed(&report, sizeof(report)) ||
      ctool_job_diagnostic_count(job) != 1u ||
      !check_diagnostic(job, 0u, CTOOL_DIS_DIAG_INVALID_REQUEST,
                        "raw mode map requires at least one range",
                        "zero-range mode-map diagnostic")) {
    ctool_job_close(job);
    return 1;
  }

  request.raw_range_count = 1u;
  (void)memset(&report, 0xa5, sizeof(report));
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "missing raw mode-map storage") ||
      !is_zeroed(&report, sizeof(report)) ||
      ctool_job_diagnostic_count(job) != 2u ||
      !check_diagnostic(job, 1u, CTOOL_DIS_DIAG_INVALID_REQUEST,
                        "raw mode map storage is missing",
                        "missing mode-map storage diagnostic")) {
    ctool_job_close(job);
    return 1;
  }

  invalid_ranges[0].offset = 0u;
  invalid_ranges[0].mode = CTOOL_X86_MODE_16;
  request.raw_ranges = invalid_ranges;
  source.contents = ctool_bytes((const void *)0, 0u);
  (void)memset(&report, 0xa5, sizeof(report));
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "empty mapped raw input") ||
      !is_zeroed(&report, sizeof(report)) ||
      ctool_job_diagnostic_count(job) != 3u ||
      !check_diagnostic(job, 2u, CTOOL_DIS_DIAG_INVALID_REQUEST,
                        "raw mode map requires nonempty input",
                        "empty mode-map input diagnostic")) {
    ctool_job_close(job);
    return 1;
  }

  source.contents =
      ctool_bytes(mixed_mode, (ctool_u32)sizeof(mixed_mode));
  invalid_ranges[0].offset = 1u;
  (void)memset(&report, 0xa5, sizeof(report));
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "nonzero raw mode-map start") ||
      !is_zeroed(&report, sizeof(report)) ||
      ctool_job_diagnostic_count(job) != 4u ||
      !check_diagnostic(job, 3u, CTOOL_DIS_DIAG_INVALID_REQUEST,
                        "raw mode map must start at offset zero",
                        "mode-map start diagnostic")) {
    ctool_job_close(job);
    return 1;
  }

  invalid_ranges[0].offset = 0u;
  invalid_ranges[0].mode = (ctool_x86_mode_t)64;
  (void)memset(&report, 0xa5, sizeof(report));
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "invalid raw range mode") ||
      !is_zeroed(&report, sizeof(report)) ||
      ctool_job_diagnostic_count(job) != 5u ||
      !check_diagnostic(job, 4u, CTOOL_DIS_DIAG_INVALID_REQUEST,
                        "raw mode map requires 16-bit or 32-bit range modes",
                        "raw range mode diagnostic")) {
    ctool_job_close(job);
    return 1;
  }

  invalid_ranges[0].mode = CTOOL_X86_MODE_16;
  invalid_ranges[1].offset = (ctool_u32)sizeof(mixed_mode);
  invalid_ranges[1].mode = CTOOL_X86_MODE_32;
  request.raw_range_count = 2u;
  (void)memset(&report, 0xa5, sizeof(report));
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "raw range outside input") ||
      !is_zeroed(&report, sizeof(report)) ||
      ctool_job_diagnostic_count(job) != 6u ||
      !check_diagnostic(job, 5u, CTOOL_DIS_DIAG_INVALID_REQUEST,
                        "raw mode map offset is outside input",
                        "raw range boundary diagnostic")) {
    ctool_job_close(job);
    return 1;
  }

  invalid_ranges[1].offset = 3u;
  invalid_ranges[2].offset = 3u;
  invalid_ranges[2].mode = CTOOL_X86_MODE_16;
  request.raw_range_count = 3u;
  (void)memset(&report, 0xa5, sizeof(report));
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "duplicate raw range offset") ||
      !is_zeroed(&report, sizeof(report)) ||
      ctool_job_diagnostic_count(job) != 7u ||
      !check_diagnostic(job, 6u, CTOOL_DIS_DIAG_INVALID_REQUEST,
                        "raw mode map offsets must increase",
                        "duplicate raw range diagnostic")) {
    ctool_job_close(job);
    return 1;
  }

  invalid_ranges[1].offset = 8u;
  invalid_ranges[2].offset = 3u;
  (void)memset(&report, 0xa5, sizeof(report));
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "decreasing raw range offsets") ||
      !is_zeroed(&report, sizeof(report)) ||
      ctool_job_diagnostic_count(job) != 8u ||
      !check_diagnostic(job, 7u, CTOOL_DIS_DIAG_INVALID_REQUEST,
                        "raw mode map offsets must increase",
                        "decreasing raw range diagnostic")) {
    ctool_job_close(job);
    return 1;
  }

  source.contents = ctool_bytes(mixed_mode, 2u);
  (void)memset(&report, 0xa5, sizeof(report));
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "raw mode-map range limit") ||
      !is_zeroed(&report, sizeof(report)) ||
      ctool_job_diagnostic_count(job) != 9u ||
      !check_diagnostic(job, 8u, CTOOL_DIS_DIAG_INVALID_REQUEST,
                        "raw mode map has too many ranges",
                        "raw mode-map range limit diagnostic")) {
    ctool_job_close(job);
    return 1;
  }

  source.contents =
      ctool_bytes(mixed_mode, (ctool_u32)sizeof(mixed_mode));
  request = raw_request(CTOOL_DIS_RAW_MODE_MAP, 0x00007c00u);
  request.raw_ranges = mixed_ranges;
  request.raw_range_count = 3u;
  status = ctool_dis_inspect(job, &source, &request, &report);
  (void)memset(&capture, 0, sizeof(capture));
  if (status == CTOOL_OK) {
    status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&capture));
  }
  if (!check_status(status, CTOOL_OK, "raw mode-map recovery") ||
      ctool_job_diagnostic_count(job) != 9u ||
      !contains(&capture, "mov eax, 0x12345678",
                "recovered mapped raw output")) {
    ctool_job_close(job);
    return 1;
  }
  {
    ctool_dis_report_t invalid_report = report;
    invalid_report.raw_range_count = 0u;
    (void)memset(&capture, 0, sizeof(capture));
    status = ctool_dis_render(job, &invalid_report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&capture));
    if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                      "mutated raw mode-map report") ||
        capture.size != 0u || ctool_job_diagnostic_count(job) != 9u) {
      ctool_job_close(job);
      return 1;
    }
  }

  request = raw_request(CTOOL_X86_MODE_16, 0x00007c00u);
  request.raw_ranges = mixed_ranges;
  request.raw_range_count = 3u;
  (void)memset(&report, 0xa5, sizeof(report));
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "fixed raw mode with range data") ||
      !is_zeroed(&report, sizeof(report)) ||
      ctool_job_diagnostic_count(job) != 10u ||
      !check_diagnostic(job, 9u, CTOOL_DIS_DIAG_INVALID_REQUEST,
                        "raw mode ranges require mapped mode",
                        "fixed raw mode range diagnostic")) {
    ctool_job_close(job);
    return 1;
  }

  (void)memset(&capture, 0, sizeof(capture));
  source.path.text = ctool_string("/return-cleanup.bin");
  source.contents = ctool_bytes(
      return_cleanup, (ctool_u32)sizeof(return_cleanup));
  request = raw_request(CTOOL_X86_MODE_32, 0u);
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (status == CTOOL_OK) {
    status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&capture));
  }
  if (!check_status(status, CTOOL_OK, "return cleanup inspection") ||
      !contains(&capture, "ret 0x4", "return cleanup operand")) {
    ctool_job_close(job);
    return 1;
  }

  (void)memset(&capture, 0, sizeof(capture));
  source.path.text = ctool_string("/direct.bin");
  source.contents = ctool_bytes(direct, (ctool_u32)sizeof(direct));
  request = raw_request(CTOOL_X86_MODE_32, 0u);
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (status == CTOOL_OK) {
    status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&capture));
  }
  if (!check_status(status, CTOOL_OK, "direct-address inspection") ||
      !contains(&capture, "dword [0xF0000000]",
                "high-bit direct address")) {
    ctool_job_close(job);
    return 1;
  }

  (void)memset(&capture, 0, sizeof(capture));
  source.path.text = ctool_string("/relative.bin");
  source.contents = ctool_bytes(relative, (ctool_u32)sizeof(relative));
  label.address = 0x00400002u;
  label.name = ctool_string("target");
  request = raw_request(CTOOL_X86_MODE_32, 0x00400000u);
  request.labels = &label;
  request.label_count = 1u;
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (status == CTOOL_OK) {
    status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&capture));
  }
  if (!check_status(status, CTOOL_OK, "relative-label inspection") ||
      !contains(&capture, "jmp 0x00400002", "relative target") ||
      !contains(&capture, "00400002 <target>:", "raw label")) {
    ctool_job_close(job);
    return 1;
  }

  (void)memset(&capture, 0, sizeof(capture));
  source.path.text = ctool_string("/relative16-short.bin");
  source.contents =
      ctool_bytes(relative16_short, (ctool_u32)sizeof(relative16_short));
  request = raw_request(CTOOL_X86_MODE_16, 0xffffu);
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (status == CTOOL_OK) {
    status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&capture));
  }
  if (!check_status(status, CTOOL_OK, "16-bit short wrap") ||
      !contains(&capture, "jmp 0x00000001", "16-bit short target")) {
    ctool_job_close(job);
    return 1;
  }

  (void)memset(&capture, 0, sizeof(capture));
  source.path.text = ctool_string("/relative16-near.bin");
  source.contents =
      ctool_bytes(relative16_near, (ctool_u32)sizeof(relative16_near));
  request = raw_request(CTOOL_X86_MODE_16, 0xffffu);
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (status == CTOOL_OK) {
    status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&capture));
  }
  if (!check_status(status, CTOOL_OK, "16-bit near wrap") ||
      !contains(&capture, "jmp 0x00000002", "16-bit near target")) {
    ctool_job_close(job);
    return 1;
  }

  (void)memset(&capture, 0, sizeof(capture));
  source.path.text = ctool_string("/relative16-wide.bin");
  source.contents =
      ctool_bytes(relative16_wide, (ctool_u32)sizeof(relative16_wide));
  request = raw_request(CTOOL_X86_MODE_16, 0xffffu);
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (status == CTOOL_OK) {
    status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&capture));
  }
  if (!check_status(status, CTOOL_OK, "16-bit rel32 target") ||
      !contains(&capture, "jmp 0x00010005", "16-bit rel32 target")) {
    ctool_job_close(job);
    return 1;
  }

  (void)memset(&capture, 0, sizeof(capture));
  source.path.text = ctool_string("/recovery.bin");
  source.contents = ctool_bytes(recovery, (ctool_u32)sizeof(recovery));
  request = raw_request(CTOOL_X86_MODE_32, 0u);
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (status == CTOOL_OK) {
    status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&capture));
  }
  if (!check_status(status, CTOOL_OK, "raw recovery") ||
      !contains(&capture, "db 0xF0", "illegal prefix byte") ||
      !contains(&capture, "cmovne eax, ecx",
                "conditional move recovery") ||
      !contains(&capture, "0x0F, 0x4F", "conditional move truncated tail")) {
    ctool_job_close(job);
    return 1;
  }

  ctool_job_close(job);
  (void)puts("raw: ok");
  return 0;
}

static int build_object(ctool_job_t *job, ctool_buffer_t **buffer_out) {
  static const ctool_u8 text[] = {0xe8u, 0xfcu, 0xffu, 0xffu, 0xffu, 0xa1u,
                                  0u,    0u,    0u,    0u,    0x8du, 0x15u,
                                  0u,    0u,    0u,    0u,    0xc3u};
  static const ctool_u8 data[] = {1u, 2u, 3u, 4u};
  ctool_elf32_section_spec_t sections[2];
  ctool_elf32_symbol_spec_t symbols[7];
  ctool_elf32_relocation_spec_t relocations[4];
  ctool_elf32_object_spec_t object;
  ctool_status_t status =
      ctool_job_open_buffer(job, 256u, ctool_default_limits().output_bytes,
                            buffer_out);
  if (status != CTOOL_OK) {
    return 0;
  }
  (void)memset(sections, 0, sizeof(sections));
  sections[0].name = ctool_string(".text");
  sections[0].type = CTOOL_ELF32_SHT_PROGBITS;
  sections[0].flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR;
  sections[0].alignment = 16u;
  sections[0].size = (ctool_u32)sizeof(text);
  sections[0].contents = ctool_bytes(text, (ctool_u32)sizeof(text));
  sections[1].name = ctool_string(".data");
  sections[1].type = CTOOL_ELF32_SHT_PROGBITS;
  sections[1].flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE;
  sections[1].alignment = 4u;
  sections[1].size = (ctool_u32)sizeof(data);
  sections[1].contents = ctool_bytes(data, (ctool_u32)sizeof(data));

  (void)memset(symbols, 0, sizeof(symbols));
  symbols[0].name = ctool_string("later");
  symbols[0].binding = CTOOL_ELF32_BIND_GLOBAL;
  symbols[0].type = CTOOL_ELF32_SYMBOL_FUNCTION;
  symbols[0].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[0].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  symbols[0].section = 0u;
  symbols[0].value = 16u;
  symbols[0].size = 1u;
  symbols[1].name = ctool_string("entry");
  symbols[1].binding = CTOOL_ELF32_BIND_GLOBAL;
  symbols[1].type = CTOOL_ELF32_SYMBOL_FUNCTION;
  symbols[1].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[1].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  symbols[1].section = 0u;
  symbols[1].value = 0u;
  symbols[1].size = 16u;
  symbols[2].name = ctool_string("external");
  symbols[2].binding = CTOOL_ELF32_BIND_GLOBAL;
  symbols[2].type = CTOOL_ELF32_SYMBOL_NOTYPE;
  symbols[2].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[2].placement = CTOOL_ELF32_SYMBOL_UNDEFINED;
  symbols[2].section = CTOOL_ELF32_NO_SECTION;
  symbols[3].name = ctool_string("");
  symbols[3].binding = CTOOL_ELF32_BIND_LOCAL;
  symbols[3].type = CTOOL_ELF32_SYMBOL_SECTION;
  symbols[3].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[3].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  symbols[3].section = 0u;
  symbols[4].name = ctool_string("weak_data");
  symbols[4].binding = CTOOL_ELF32_BIND_WEAK;
  symbols[4].type = CTOOL_ELF32_SYMBOL_OBJECT;
  symbols[4].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[4].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  symbols[4].section = 0u;
  symbols[4].value = 15u;
  symbols[4].size = 1u;
  symbols[5].name = ctool_string("weak_import");
  symbols[5].binding = CTOOL_ELF32_BIND_WEAK;
  symbols[5].type = CTOOL_ELF32_SYMBOL_OBJECT;
  symbols[5].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[5].placement = CTOOL_ELF32_SYMBOL_UNDEFINED;
  symbols[5].section = CTOOL_ELF32_NO_SECTION;
  symbols[6].name = ctool_string("alias_before_entry");
  symbols[6].binding = CTOOL_ELF32_BIND_GLOBAL;
  symbols[6].type = CTOOL_ELF32_SYMBOL_FUNCTION;
  symbols[6].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[6].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  symbols[6].section = 0u;
  symbols[6].value = 0u;
  symbols[6].size = 16u;

  relocations[0].target_section = 0u;
  relocations[0].offset = 1u;
  relocations[0].symbol = 2u;
  relocations[0].type = CTOOL_ELF32_R_386_PC32;
  relocations[0].addend = -4;
  relocations[1].target_section = 0u;
  relocations[1].offset = 6u;
  relocations[1].symbol = 2u;
  relocations[1].type = CTOOL_ELF32_R_386_32;
  relocations[1].addend = 0;
  relocations[2].target_section = 0u;
  relocations[2].offset = 12u;
  relocations[2].symbol = 3u;
  relocations[2].type = CTOOL_ELF32_R_386_32;
  relocations[2].addend = 144;
  relocations[3].target_section = 1u;
  relocations[3].offset = 0u;
  relocations[3].symbol = 2u;
  relocations[3].type = CTOOL_ELF32_R_386_32;
  relocations[3].addend = 0;
  object.sections = sections;
  object.section_count = 2u;
  object.symbols = symbols;
  object.symbol_count = 7u;
  object.relocations = relocations;
  object.relocation_count = 4u;
  status = ctool_elf32_write(job, &object, *buffer_out);
  if (status != CTOOL_OK) {
    ctool_buffer_close(*buffer_out);
    *buffer_out = (ctool_buffer_t *)0;
    return 0;
  }
  return 1;
}

static int build_merge_object(ctool_job_t *job,
                              ctool_buffer_t **buffer_out) {
  static const ctool_u8 constant[] = {0x78u, 0x56u, 0x34u, 0x12u};
  static const ctool_u8 strings[] = {'o', 'n', 'e', 0u, 't', 'w', 'o', 0u};
  ctool_elf32_section_spec_t sections[2];
  ctool_elf32_object_spec_t object;
  ctool_status_t status =
      ctool_job_open_buffer(job, 256u, ctool_default_limits().output_bytes,
                            buffer_out);
  if (status != CTOOL_OK) {
    return 0;
  }
  (void)memset(sections, 0, sizeof(sections));
  sections[0].name = ctool_string(".rodata.cst4");
  sections[0].type = CTOOL_ELF32_SHT_PROGBITS;
  sections[0].flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_MERGE;
  sections[0].alignment = 4u;
  sections[0].entry_size = 4u;
  sections[0].size = (ctool_u32)sizeof(constant);
  sections[0].contents =
      ctool_bytes(constant, (ctool_u32)sizeof(constant));
  sections[1].name = ctool_string(".rodata.str1.1");
  sections[1].type = CTOOL_ELF32_SHT_PROGBITS;
  sections[1].flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_MERGE |
                      CTOOL_ELF32_SHF_STRINGS;
  sections[1].alignment = 1u;
  sections[1].entry_size = 1u;
  sections[1].size = (ctool_u32)sizeof(strings);
  sections[1].contents = ctool_bytes(strings, (ctool_u32)sizeof(strings));
  (void)memset(&object, 0, sizeof(object));
  object.sections = sections;
  object.section_count = 2u;
  status = ctool_elf32_write(job, &object, *buffer_out);
  if (status != CTOOL_OK) {
    ctool_buffer_close(*buffer_out);
    *buffer_out = (ctool_buffer_t *)0;
    return 0;
  }
  return 1;
}

static int run_object(void) {
  ctool_host_adapter_t adapter;
  ctool_job_t *job;
  ctool_buffer_t *object_bytes;
  ctool_source_t source;
  ctool_dis_request_t request;
  ctool_dis_report_t report;
  capture_t capture;
  ctool_status_t status;
  if (!open_job(&adapter, &job) || !build_object(job, &object_bytes)) {
    return 1;
  }
  source.path.text = ctool_string("/fixture.o");
  source.contents = ctool_buffer_view(object_bytes);
  (void)memset(&request, 0, sizeof(request));
  request.input = CTOOL_DIS_INPUT_ELF32;
  request.views = CTOOL_DIS_VIEW_ALL;
  (void)memset(&capture, 0, sizeof(capture));
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (status == CTOOL_OK) {
    status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&capture));
  }
  if (!check_status(status, CTOOL_OK, "object inspection") ||
      !contains(&capture, "ELF32 REL i386", "ELF header") ||
      !contains(&capture, "[sections]", "section heading") ||
      !contains(&capture, ".text", "text section") ||
      !contains(&capture, "[symbols]", "symbol heading") ||
      !contains(&capture, "entry", "function symbol") ||
      !contains(&capture, "[relocations]", "relocation heading") ||
      !contains(&capture, "R_386_PC32", "relocation type") ||
      !contains(&capture, "R_386_32", "absolute relocation type") ||
      !contains(&capture, "call external-4", "relocation overlay") ||
      !contains(&capture, "dword [external]", "memory relocation overlay") ||
      !contains(&capture, "lea edx, [.text+144]",
                "section-symbol memory relocation overlay") ||
      !contains(&capture, "R_386_32 .text+144",
                "section-symbol relocation row") ||
      !contains(&capture, "[disassembly .text]", "section disassembly")) {
    ctool_buffer_close(object_bytes);
    ctool_job_close(job);
    return 1;
  }
  if (report.elf32.section_count == 0u || report.elf32.symbol_count == 0u ||
      report.elf32.relocation_count != 4u) {
    (void)fprintf(stderr, "typed object report is incomplete\n");
    ctool_buffer_close(object_bytes);
    ctool_job_close(job);
    return 1;
  }
  {
    ctool_buffer_t *merge_bytes;
    if (!build_merge_object(job, &merge_bytes)) {
      ctool_buffer_close(object_bytes);
      ctool_job_close(job);
      return 1;
    }
    source.path.text = ctool_string("/merge.o");
    source.contents = ctool_buffer_view(merge_bytes);
    request.views = CTOOL_DIS_VIEW_SECTIONS;
    (void)memset(&capture, 0, sizeof(capture));
    status = ctool_dis_inspect(job, &source, &request, &report);
    if (status == CTOOL_OK) {
      status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                                capture_sink(&capture));
    }
    if (!check_status(status, CTOOL_OK, "merge section inspection") ||
        !contains(&capture, ".rodata.cst4 type=PROGBITS flags=AM",
                  "merge section flag") ||
        !contains(&capture, ".rodata.str1.1 type=PROGBITS flags=AMS",
                  "string section flags")) {
      ctool_buffer_close(merge_bytes);
      ctool_buffer_close(object_bytes);
      ctool_job_close(job);
      return 1;
    }
    ctool_buffer_close(merge_bytes);
    source.path.text = ctool_string("/fixture.o");
    source.contents = ctool_buffer_view(object_bytes);
    request.views = CTOOL_DIS_VIEW_ALL;
  }
  {
    ctool_dis_report_t header_report;
    request.views = CTOOL_DIS_VIEW_HEADER;
    status = ctool_dis_inspect(job, &source, &request, &header_report);
    request.views = CTOOL_DIS_VIEW_ALL;
    if (!check_status(status, CTOOL_OK, "header-only index gating") ||
        header_report.symbol_order_count != 0u ||
        header_report.function_order_count != 0u ||
        header_report.relocation_order_count != 0u ||
        header_report.relocation_site_order_count != 0u) {
      (void)fprintf(stderr, "header-only report built unused indexes\n");
      ctool_buffer_close(object_bytes);
      ctool_job_close(job);
      return 1;
    }
  }
  {
    ctool_bytes_t bytes = ctool_buffer_view(object_bytes);
    ctool_u8 *copy = (ctool_u8 *)malloc((size_t)bytes.size);
    ctool_u8 header[40];
    ctool_u32 section_headers;
    ctool_u32 rel_text_header;
    ctool_u32 rel_data_header;
    char *text_row;
    char *data_row;
    if (copy == (ctool_u8 *)0) {
      ctool_buffer_close(object_bytes);
      ctool_job_close(job);
      return 1;
    }
    (void)memcpy(copy, bytes.data, (size_t)bytes.size);
    section_headers = get_le32(copy, 32u);
    rel_text_header = section_headers + 3u * 40u;
    rel_data_header = section_headers + 4u * 40u;
    (void)memcpy(header, copy + rel_text_header, sizeof(header));
    (void)memcpy(copy + rel_text_header, copy + rel_data_header,
                 sizeof(header));
    (void)memcpy(copy + rel_data_header, header, sizeof(header));
    source.contents = ctool_bytes(copy, bytes.size);
    request.views = CTOOL_DIS_VIEW_RELOCATIONS;
    (void)memset(&capture, 0, sizeof(capture));
    status = ctool_dis_inspect(job, &source, &request, &report);
    if (status == CTOOL_OK) {
      status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                                capture_sink(&capture));
    }
    text_row = strstr(capture.bytes, "[.rel.text:0]");
    data_row = strstr(capture.bytes, "[.rel.data:0]");
    free(copy);
    if (!check_status(status, CTOOL_OK, "serialized relocation order") ||
        text_row == (char *)0 || data_row == (char *)0 || data_row > text_row) {
      (void)fprintf(stderr, "relocation rows do not follow file order\n");
      ctool_buffer_close(object_bytes);
      ctool_job_close(job);
      return 1;
    }
  }
  {
    ctool_dis_report_t invalid_report = report;
    invalid_report.elf32.sections = (const ctool_elf32_section_t *)0;
    (void)memset(&capture, 0, sizeof(capture));
    status = ctool_dis_render(job, &invalid_report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&capture));
    if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                      "forged object report") ||
        capture.size != 0u) {
      ctool_buffer_close(object_bytes);
      ctool_job_close(job);
      return 1;
    }
  }
  {
    ctool_bytes_t bytes = ctool_buffer_view(object_bytes);
    ctool_u8 *copy = (ctool_u8 *)malloc((size_t)bytes.size);
    ctool_u32 section_headers;
    ctool_u32 relocation_header;
    ctool_u32 relocation_offset;
    ctool_u32 info_offset;
    ctool_u32 info;
    if (copy == (ctool_u8 *)0) {
      ctool_buffer_close(object_bytes);
      ctool_job_close(job);
      return 1;
    }
    (void)memcpy(copy, bytes.data, (size_t)bytes.size);
    section_headers = get_le32(copy, 32u);
    relocation_header = section_headers + 3u * 40u;
    relocation_offset = get_le32(copy, relocation_header + 16u);
    info_offset = relocation_offset + 8u + 4u;
    info = get_le32(copy, info_offset);
    put_le32(copy, info_offset, (info & 0xffffff00u) | 0x7fu);
    source.contents = ctool_bytes(copy, bytes.size);
    request.views = CTOOL_DIS_VIEW_DISASSEMBLY;
    (void)memset(&capture, 0, sizeof(capture));
    status = ctool_dis_inspect(job, &source, &request, &report);
    if (status == CTOOL_OK) {
      status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                                capture_sink(&capture));
    }
    free(copy);
    if (!check_status(status, CTOOL_OK, "unknown relocation inspection") ||
        !contains(&capture, "dword [0x0]", "unknown relocation raw field") ||
        strstr(capture.bytes, "dword [external]") != (char *)0) {
      (void)fprintf(stderr, "unknown relocation claimed operand ownership\n");
      ctool_buffer_close(object_bytes);
      ctool_job_close(job);
      return 1;
    }
  }
  {
    ctool_bytes_t bytes = ctool_buffer_view(object_bytes);
    ctool_u8 *copy = (ctool_u8 *)malloc((size_t)bytes.size);
    if (copy == (ctool_u8 *)0) {
      ctool_buffer_close(object_bytes);
      ctool_job_close(job);
      return 1;
    }
    (void)memcpy(copy, bytes.data, (size_t)bytes.size);
    put_le32(copy, get_le32(copy, 32u) + 40u + 12u, 0xfffffffeu);
    source.contents = ctool_bytes(copy, bytes.size);
    request.views = CTOOL_DIS_VIEW_HEADER;
    status = ctool_dis_inspect(job, &source, &request, &report);
    if (!check_status(status, CTOOL_OK,
                      "metadata-only overflowing code address")) {
      free(copy);
      ctool_buffer_close(object_bytes);
      ctool_job_close(job);
      return 1;
    }
    request.views = CTOOL_DIS_VIEW_SYMBOLS;
    status = ctool_dis_inspect(job, &source, &request, &report);
    if (!check_status(status, CTOOL_ERR_OVERFLOW,
                      "overflowing object symbol address") ||
        report.source != (const ctool_source_t *)0 ||
        ctool_job_diagnostic_count(job) != 1u ||
        !check_diagnostic(job, 0u, CTOOL_DIS_DIAG_ADDRESS_OVERFLOW,
                          "ELF symbol address overflows",
                          "symbol overflow diagnostic")) {
      free(copy);
      ctool_buffer_close(object_bytes);
      ctool_job_close(job);
      return 1;
    }
    request.views = CTOOL_DIS_VIEW_ALL;
    status = ctool_dis_inspect(job, &source, &request, &report);
    free(copy);
    if (!check_status(status, CTOOL_ERR_OVERFLOW,
                       "overflowing object code address") ||
        report.source != (const ctool_source_t *)0 ||
        ctool_job_diagnostic_count(job) != 2u ||
        !check_diagnostic(job, 1u, CTOOL_DIS_DIAG_ADDRESS_OVERFLOW,
                          "ELF disassembly address range overflows",
                          "object overflow diagnostic")) {
      ctool_buffer_close(object_bytes);
      ctool_job_close(job);
      return 1;
    }
  }
  ctool_buffer_close(object_bytes);
  ctool_job_close(job);
  (void)puts("object: ok");
  return 0;
}

static int run_exec(void) {
  ctool_u8 image[122];
  ctool_host_adapter_t adapter;
  ctool_job_t *job;
  ctool_source_t source;
  ctool_dis_request_t request;
  ctool_dis_report_t report;
  capture_t capture;
  ctool_status_t status;
  (void)memset(image, 0, sizeof(image));
  image[0] = 0x7fu;
  image[1] = (ctool_u8)'E';
  image[2] = (ctool_u8)'L';
  image[3] = (ctool_u8)'F';
  image[4] = 1u;
  image[5] = 1u;
  image[6] = 1u;
  put_le16(image, 16u, 2u);
  put_le16(image, 18u, 3u);
  put_le32(image, 20u, 1u);
  put_le32(image, 24u, 0x00400000u);
  put_le32(image, 28u, 52u);
  put_le16(image, 40u, 52u);
  put_le16(image, 42u, 32u);
  put_le16(image, 44u, 2u);
  put_le32(image, 52u, CTOOL_ELF32_PT_LOAD);
  put_le32(image, 56u, 116u);
  put_le32(image, 60u, 0x00400000u);
  put_le32(image, 64u, 0x00400000u);
  put_le32(image, 68u, 6u);
  put_le32(image, 72u, 6u);
  put_le32(image, 76u, CTOOL_ELF32_PF_R | CTOOL_ELF32_PF_X);
  put_le32(image, 80u, 4u);
  put_le32(image, 84u, 0x6474e551u);
  put_le32(image, 108u, CTOOL_ELF32_PF_R | CTOOL_ELF32_PF_W);
  put_le32(image, 112u, 16u);
  image[116] = 0xb8u;
  image[117] = 0x78u;
  image[118] = 0x56u;
  image[119] = 0x34u;
  image[120] = 0x12u;
  image[121] = 0xc3u;
  if (!open_job(&adapter, &job)) {
    return 1;
  }
  source.path.text = ctool_string("/program.elf");
  source.contents = ctool_bytes(image, (ctool_u32)sizeof(image));
  (void)memset(&request, 0, sizeof(request));
  request.input = CTOOL_DIS_INPUT_ELF32;
  request.views = CTOOL_DIS_VIEW_HEADER | CTOOL_DIS_VIEW_DISASSEMBLY;
  (void)memset(&capture, 0, sizeof(capture));
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (status == CTOOL_OK) {
    status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&capture));
  }
  if (!check_status(status, CTOOL_OK, "executable inspection") ||
      report.elf32.file_type != CTOOL_ELF32_ET_EXEC ||
      report.elf32.program_header_count != 2u ||
      !contains(&capture, "ELF32 EXEC i386", "executable header") ||
      !contains(&capture, "[program headers]", "program headers") ||
      !contains(&capture, "] GNU_STACK off=", "GNU stack header type") ||
      !contains(&capture, "[disassembly LOAD#0]", "load disassembly") ||
      !contains(&capture, "00400000", "load address") ||
      !contains(&capture, "mov eax, 0x12345678", "executable code")) {
    ctool_job_close(job);
    return 1;
  }
  request.views = CTOOL_DIS_VIEW_SYMBOLS;
  (void)memset(&capture, 0, sizeof(capture));
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (status == CTOOL_OK) {
    status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_NM,
                              capture_sink(&capture));
  }
  if (!check_status(status, CTOOL_OK, "stripped executable nm") ||
      capture.size != 0u) {
    (void)fprintf(stderr, "stripped executable emitted nm rows\n");
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)puts("exec: ok");
  return 0;
}

static int run_nm(void) {
  ctool_host_adapter_t adapter;
  ctool_job_t *job;
  ctool_buffer_t *object_bytes;
  ctool_source_t source;
  ctool_dis_request_t request;
  ctool_dis_report_t report;
  capture_t capture;
  ctool_status_t status;
  if (!open_job(&adapter, &job) || !build_object(job, &object_bytes)) {
    return 1;
  }
  source.path.text = ctool_string("/fixture.o");
  source.contents = ctool_buffer_view(object_bytes);
  (void)memset(&request, 0, sizeof(request));
  request.input = CTOOL_DIS_INPUT_ELF32;
  request.views = CTOOL_DIS_VIEW_SYMBOLS;
  (void)memset(&capture, 0, sizeof(capture));
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (status == CTOOL_OK) {
    status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_NM,
                              capture_sink(&capture));
  }
  if (!check_status(status, CTOOL_OK, "nm rendering") ||
      strcmp(capture.bytes,
             "00000000 T entry\n         U external\n         v weak_import\n"
             "00000000 T alias_before_entry\n"
             "0000000F V weak_data\n00000010 T later\n") !=
          0) {
    (void)fprintf(stderr, "unexpected nm output:\n%s", capture.bytes);
    ctool_buffer_close(object_bytes);
    ctool_job_close(job);
    return 1;
  }
  (void)memset(&capture, 0, sizeof(capture));
  capture.emit_job = job;
  status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_NM,
                            capture_sink(&capture));
  if (!check_status(status, CTOOL_OK, "reentrant nm sink") ||
      capture.emitted == CTOOL_FALSE ||
      ctool_job_diagnostic_count(job) != 1u ||
      !check_diagnostic(job, 0u, 0x0500fff0u,
                        "sink allocation survives rendering",
                        "reentrant sink diagnostic")) {
    ctool_buffer_close(object_bytes);
    ctool_job_close(job);
    return 1;
  }
  ctool_buffer_close(object_bytes);
  ctool_job_close(job);
  (void)puts("nm: ok");
  return 0;
}

static int run_errors(void) {
  static const ctool_u8 code[] = {0x90u};
  ctool_host_adapter_t adapter;
  ctool_job_t *job;
  ctool_source_t source;
  ctool_dis_request_t request;
  ctool_dis_report_t report;
  capture_t capture;
  ctool_status_t status;
  capture.size = 0u;
  if (!open_job(&adapter, &job)) {
    return 1;
  }
  source.path.text = ctool_string("/invalid.bin");
  source.contents = ctool_bytes(code, (ctool_u32)sizeof(code));
  {
    ctool_source_t invalid_source = source;
    invalid_source.path.text.data = (const char *)0;
    invalid_source.path.text.size = 1u;
    request = raw_request(CTOOL_X86_MODE_32, 0u);
    status = ctool_dis_inspect(job, &invalid_source, &request, &report);
    if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                      "invalid source path") ||
        report.source != (const ctool_source_t *)0 ||
        ctool_job_diagnostic_count(job) != 0u) {
      ctool_job_close(job);
      return 1;
    }
  }
  request = raw_request(CTOOL_X86_MODE_32, 0u);
  request.views = CTOOL_DIS_VIEW_HEADER;
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "raw metadata request") ||
      report.source != (const ctool_source_t *)0 ||
      ctool_job_diagnostic_count(job) != 1u ||
      !check_diagnostic(job, 0u, CTOOL_DIS_DIAG_INVALID_REQUEST,
                        "raw input only supports disassembly",
                        "raw metadata diagnostic")) {
    ctool_job_close(job);
    return 1;
  }
  request = raw_request((ctool_x86_mode_t)64, 0u);
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT, "invalid raw mode") ||
      ctool_job_diagnostic_count(job) != 2u ||
      !check_diagnostic(job, 1u, CTOOL_DIS_DIAG_INVALID_REQUEST,
                        "raw input requires 16-bit or 32-bit mode",
                        "invalid raw mode diagnostic")) {
    ctool_job_close(job);
    return 1;
  }
  request = raw_request(CTOOL_X86_MODE_32, 0u);
  request.label_count = 1u;
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "missing raw labels") ||
      ctool_job_diagnostic_count(job) != 3u ||
      !check_diagnostic(job, 2u, CTOOL_DIS_DIAG_INVALID_REQUEST,
                        "raw label storage is missing",
                        "missing raw labels diagnostic")) {
    ctool_job_close(job);
    return 1;
  }
  request = raw_request(CTOOL_X86_MODE_32, 0u);
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (status == CTOOL_OK) {
    ctool_dis_report_t invalid_report = report;
    invalid_report.label_count = 1u;
    invalid_report.labels = (const ctool_dis_label_t *)0;
    (void)memset(&capture, 0, sizeof(capture));
    status = ctool_dis_render(job, &invalid_report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&capture));
  }
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "forged raw report") ||
      capture.size != 0u || ctool_job_diagnostic_count(job) != 3u) {
    ctool_job_close(job);
    return 1;
  }
  {
    ctool_source_t invalid_source = source;
    ctool_dis_report_t invalid_report = report;
    invalid_source.path.text.data = (const char *)0;
    invalid_source.path.text.size = 1u;
    invalid_report.source = &invalid_source;
    (void)memset(&capture, 0, sizeof(capture));
    status = ctool_dis_render(job, &invalid_report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&capture));
    if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                      "forged source path") ||
        capture.size != 0u || ctool_job_diagnostic_count(job) != 3u) {
      ctool_job_close(job);
      return 1;
    }
  }
  status = ctool_dis_inspect(job, &source, &request, &report);
  (void)memset(&capture, 0, sizeof(capture));
  capture.fail_after = 1u;
  if (status == CTOOL_OK) {
    status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&capture));
  }
  if (!check_status(status, CTOOL_ERR_IO, "failing output sink") ||
      ctool_job_diagnostic_count(job) != 4u ||
      !check_diagnostic(job, 3u, CTOOL_DIS_DIAG_OUTPUT,
                        "CupidDis could not complete report output",
                        "output diagnostic")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (status == CTOOL_OK) {
    ctool_text_sink_t invalid_output;
    invalid_output.context = (void *)0;
    invalid_output.write = invalid_sink_write;
    status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                              invalid_output);
  }
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "invalid-argument output sink") ||
      ctool_job_diagnostic_count(job) != 5u ||
      !check_diagnostic(job, 4u, CTOOL_DIS_DIAG_OUTPUT,
                        "CupidDis could not complete report output",
                        "invalid-argument output diagnostic")) {
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)puts("errors: ok");
  return 0;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    (void)fprintf(stderr, "usage: cupiddis-contract MODE\n");
    return 2;
  }
  if (strcmp(argv[1], "raw") == 0) {
    return run_raw();
  }
  if (strcmp(argv[1], "object") == 0) {
    return run_object();
  }
  if (strcmp(argv[1], "exec") == 0) {
    return run_exec();
  }
  if (strcmp(argv[1], "nm") == 0) {
    return run_nm();
  }
  if (strcmp(argv[1], "errors") == 0) {
    return run_errors();
  }
  (void)fprintf(stderr, "unknown mode: %s\n", argv[1]);
  return 2;
}
