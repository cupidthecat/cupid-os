#include "ctool.h"
#include "ctool_host.h"
#include "cupiddis.h"
#include "elf32.h"
#include "pe32.h"

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

static ctool_u32 count_occurrences(const capture_t *capture,
                                   const char *needle) {
  const char *cursor = capture->bytes;
  size_t needle_size = strlen(needle);
  ctool_u32 count = 0u;
  while (needle_size != 0u &&
         (cursor = strstr(cursor, needle)) != (char *)0) {
    count++;
    cursor += needle_size;
  }
  return count;
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

static int open_seed_job(ctool_host_adapter_t *adapter, ctool_job_t **job,
                         ctool_source_t *source) {
  static const char *const roots[] = {".", ".."};
  ctool_u32 root_index;
  for (root_index = 0u;
       root_index < (ctool_u32)(sizeof(roots) / sizeof(roots[0]));
       root_index++) {
    ctool_limits_t limits = ctool_default_limits();
    ctool_job_config_t config;
    ctool_path_t path;
    ctool_status_t status =
        ctool_host_adapter_init(adapter, roots[root_index]);
    limits.arena_bytes = 128u * 1024u * 1024u;
    if (status == CTOOL_OK) {
      config = ctool_host_job_config(adapter, limits);
      status = ctool_job_open(&config, job);
    }
    path.text =
        ctool_string("/bootstrap/seeds/i386-windows/cupidld.exe");
    if (status == CTOOL_OK) {
      status = ctool_job_load_source(*job, &path, source);
    }
    if (status == CTOOL_OK) {
      return 1;
    }
    if (*job != (ctool_job_t *)0) {
      ctool_job_close(*job);
      *job = (ctool_job_t *)0;
    }
  }
  (void)fprintf(stderr, "cannot load checked PE32 seed\n");
  return 0;
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

static int summaries_equal(const ctool_dis_decode_summary_t *left,
                           const ctool_dis_decode_summary_t *right) {
  return left->known_count == right->known_count &&
                 left->unknown_count == right->unknown_count &&
                 left->invalid_count == right->invalid_count &&
                 left->truncated_count == right->truncated_count &&
                 left->executable_relocation_count ==
                     right->executable_relocation_count &&
                 left->unmatched_executable_relocation_count ==
                     right->unmatched_executable_relocation_count &&
                 left->direct_relative_target_count ==
                     right->direct_relative_target_count &&
                 left->direct_relative_outside_image_count ==
                     right->direct_relative_outside_image_count &&
                 left->direct_relative_outside_section_count ==
                     right->direct_relative_outside_section_count &&
                 left->direct_relative_data_count ==
                     right->direct_relative_data_count &&
                  left->direct_relative_wrong_mode_count ==
                      right->direct_relative_wrong_mode_count &&
                  left->direct_relative_mid_instruction_count ==
                      right->direct_relative_mid_instruction_count &&
                  left->code_anchor_count == right->code_anchor_count &&
                  left->code_anchor_outside_executable_count ==
                      right->code_anchor_outside_executable_count &&
                  left->code_anchor_mid_instruction_count ==
                      right->code_anchor_mid_instruction_count &&
                  left->source_control_edge_count ==
                      right->source_control_edge_count &&
                  left->source_control_edge_local_count ==
                      right->source_control_edge_local_count &&
                  left->source_control_edge_external_count ==
                      right->source_control_edge_external_count &&
                  left->source_control_edge_unprovable_count ==
                      right->source_control_edge_unprovable_count &&
                  left->source_control_edge_invalid_count ==
                      right->source_control_edge_invalid_count &&
                  left->source_control_edge_source_mismatch_count ==
                      right->source_control_edge_source_mismatch_count &&
                  left->source_control_edge_target_mismatch_count ==
                      right->source_control_edge_target_mismatch_count &&
                  left->source_control_edge_target_mode_mismatch_count ==
                      right->source_control_edge_target_mode_mismatch_count
             ? 1
             : 0;
}

static int check_unowned_absolute_memory_relocation(
    ctool_job_t *job, const ctool_source_t *source,
    const ctool_dis_request_t *request, ctool_u64 executable_count,
    const char *operation) {
  ctool_dis_report_t report;
  capture_t capture;
  ctool_status_t status;
  (void)memset(&capture, 0, sizeof(capture));
  status = ctool_dis_inspect(job, source, request, &report);
  if (status == CTOOL_OK) {
    status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&capture));
  }
  if (!check_status(status, CTOOL_OK, operation) ||
      report.decode_summary.executable_relocation_count != executable_count ||
      report.decode_summary.unmatched_executable_relocation_count != 1u ||
      !contains(&capture, "dword [0x0]", operation) ||
      strstr(capture.bytes, "dword [external]") != (char *)0) {
    (void)fprintf(stderr, "%s claimed operand ownership\n", operation);
    return 0;
  }
  return 1;
}

static int run_indexed(void) {
  static const ctool_u8 clean[] = {0x90u, 0xc3u};
  static const ctool_u8 mixed[] = {
      0x90u, 0x0fu, 0xffu, 0xc0u, 0x66u, 0x66u, 0x90u, 0x0fu};
  static const struct {
    const char *path;
    const ctool_u8 *bytes;
    ctool_u32 size;
  } inputs[] = {
      {"/clean.bin", clean, (ctool_u32)sizeof(clean)},
      {"/mixed.bin", mixed, (ctool_u32)sizeof(mixed)}};
  ctool_host_adapter_t owner_adapter;
  ctool_host_adapter_t caller_adapter;
  ctool_job_t *owner_job;
  ctool_job_t *caller_job;
  const ctool_x86_decoder_t *decoder;
  ctool_dis_request_t request = raw_request(CTOOL_X86_MODE_32, 0u);
  ctool_dis_report_t exhaustive;
  ctool_dis_report_t indexed;
  ctool_source_t source;
  ctool_status_t status;
  ctool_u32 input;
  if (!open_job(&owner_adapter, &owner_job)) {
    return 1;
  }
  status = ctool_x86_decoder_prepare(owner_job, &decoder);
  if (!check_status(status, CTOOL_OK, "indexed inspector decoder prepare") ||
      !open_job(&caller_adapter, &caller_job)) {
    ctool_job_close(owner_job);
    return 1;
  }
  for (input = 0u;
       input < (ctool_u32)(sizeof(inputs) / sizeof(inputs[0])); input++) {
    source.path.text = ctool_string(inputs[input].path);
    source.contents = ctool_bytes(inputs[input].bytes, inputs[input].size);
    status = ctool_dis_inspect(caller_job, &source, &request, &exhaustive);
    if (status == CTOOL_OK) {
      status = ctool_dis_inspect_indexed(
          caller_job, decoder, &source, &request, &indexed);
    }
    if (!check_status(status, CTOOL_OK, "indexed inspector reuse") ||
        !summaries_equal(&exhaustive.decode_summary,
                         &indexed.decode_summary)) {
      (void)fprintf(stderr, "indexed inspector summary differs for %s\n",
                    inputs[input].path);
      ctool_job_close(caller_job);
      ctool_job_close(owner_job);
      return 1;
    }
  }
  (void)memset(&indexed, 0xa5, sizeof(indexed));
  status = ctool_dis_inspect_indexed(
      caller_job, (const ctool_x86_decoder_t *)0, &source, &request,
      &indexed);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "indexed inspector null decoder") ||
      !is_zeroed(&indexed, sizeof(indexed))) {
    ctool_job_close(caller_job);
    ctool_job_close(owner_job);
    return 1;
  }
  ctool_job_close(caller_job);
  ctool_job_close(owner_job);
  (void)puts("indexed: ok");
  return 0;
}

static int target_summary_matches(
    const ctool_dis_decode_summary_t *summary, ctool_u64 total,
    ctool_u64 outside_image, ctool_u64 outside_section, ctool_u64 data,
    ctool_u64 wrong_mode, ctool_u64 mid_instruction, const char *operation) {
  if (summary->direct_relative_target_count != total ||
      summary->direct_relative_outside_image_count != outside_image ||
      summary->direct_relative_outside_section_count != outside_section ||
      summary->direct_relative_data_count != data ||
      summary->direct_relative_wrong_mode_count != wrong_mode ||
      summary->direct_relative_mid_instruction_count != mid_instruction) {
    (void)fprintf(stderr,
                  "%s: target summary differs: "
                  "%llu/%llu/%llu/%llu/%llu/%llu\n",
                  operation,
                  (unsigned long long)summary->direct_relative_target_count,
                  (unsigned long long)
                      summary->direct_relative_outside_image_count,
                  (unsigned long long)
                      summary->direct_relative_outside_section_count,
                  (unsigned long long)summary->direct_relative_data_count,
                  (unsigned long long)summary->direct_relative_wrong_mode_count,
                  (unsigned long long)
                      summary->direct_relative_mid_instruction_count);
    return 0;
  }
  return 1;
}

static int build_local_target_object(ctool_job_t *job,
                                     const ctool_u8 *text,
                                     ctool_u32 text_size,
                                     ctool_buffer_t **buffer_out) {
  ctool_elf32_section_spec_t section;
  ctool_elf32_object_spec_t object;
  ctool_status_t status =
      ctool_job_open_buffer(job, 256u, ctool_default_limits().output_bytes,
                            buffer_out);
  if (status != CTOOL_OK) {
    return 0;
  }
  (void)memset(&section, 0, sizeof(section));
  section.name = ctool_string(".text");
  section.type = CTOOL_ELF32_SHT_PROGBITS;
  section.flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR;
  section.alignment = 1u;
  section.size = text_size;
  section.contents = ctool_bytes(text, text_size);
  (void)memset(&object, 0, sizeof(object));
  object.sections = &section;
  object.section_count = 1u;
  status = ctool_elf32_write(job, &object, *buffer_out);
  if (status != CTOOL_OK) {
    ctool_buffer_close(*buffer_out);
    *buffer_out = (ctool_buffer_t *)0;
    return 0;
  }
  return 1;
}

static int build_code_anchor_object(
    ctool_job_t *job, ctool_elf32_symbol_placement_t candidate_placement,
    ctool_u32 candidate_section,
    ctool_u32 candidate_value, ctool_buffer_t **buffer_out) {
  static const ctool_u8 text[] = {
      0xe8u, 0xfcu, 0xffu, 0xffu, 0xffu, 0xc3u};
  static const ctool_u8 data[] = {0xc3u};
  ctool_elf32_section_spec_t sections[2];
  ctool_elf32_symbol_spec_t symbols[5];
  ctool_elf32_relocation_spec_t relocation;
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
  sections[0].alignment = 1u;
  sections[0].size = (ctool_u32)sizeof(text);
  sections[0].contents = ctool_bytes(text, (ctool_u32)sizeof(text));
  sections[1].name = ctool_string(".data");
  sections[1].type = CTOOL_ELF32_SHT_PROGBITS;
  sections[1].flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE;
  sections[1].alignment = 1u;
  sections[1].size = (ctool_u32)sizeof(data);
  sections[1].contents = ctool_bytes(data, (ctool_u32)sizeof(data));

  (void)memset(symbols, 0, sizeof(symbols));
  symbols[0].name = ctool_string("entry");
  symbols[0].binding = CTOOL_ELF32_BIND_GLOBAL;
  symbols[0].type = CTOOL_ELF32_SYMBOL_FUNCTION;
  symbols[0].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[0].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  symbols[0].section = 0u;
  symbols[0].value = 0u;
  symbols[1].name = ctool_string("tail");
  symbols[1].binding = CTOOL_ELF32_BIND_LOCAL;
  symbols[1].type = CTOOL_ELF32_SYMBOL_FUNCTION;
  symbols[1].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[1].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  symbols[1].section = 0u;
  symbols[1].value = 5u;
  symbols[2].name = ctool_string("external");
  symbols[2].binding = CTOOL_ELF32_BIND_GLOBAL;
  symbols[2].type = CTOOL_ELF32_SYMBOL_FUNCTION;
  symbols[2].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[2].placement = CTOOL_ELF32_SYMBOL_UNDEFINED;
  symbols[2].section = CTOOL_ELF32_NO_SECTION;
  symbols[3].name = ctool_string("ordinary");
  symbols[3].binding = CTOOL_ELF32_BIND_GLOBAL;
  symbols[3].type = CTOOL_ELF32_SYMBOL_NOTYPE;
  symbols[3].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[3].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  symbols[3].section = 0u;
  symbols[3].value = 1u;
  symbols[4].name = ctool_string("candidate");
  symbols[4].binding = CTOOL_ELF32_BIND_GLOBAL;
  symbols[4].type = CTOOL_ELF32_SYMBOL_FUNCTION;
  symbols[4].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[4].placement = candidate_placement;
  symbols[4].section = candidate_placement == CTOOL_ELF32_SYMBOL_DEFINED
                           ? candidate_section
                           : CTOOL_ELF32_NO_SECTION;
  symbols[4].value = candidate_value;

  relocation.target_section = 0u;
  relocation.offset = 1u;
  relocation.symbol = 2u;
  relocation.type = CTOOL_ELF32_R_386_PC32;
  relocation.addend = -4;
  object.sections = sections;
  object.section_count = 2u;
  object.symbols = symbols;
  object.symbol_count = 5u;
  object.relocations = &relocation;
  object.relocation_count = 1u;
  status = ctool_elf32_write(job, &object, *buffer_out);
  if (status != CTOOL_OK) {
    ctool_buffer_close(*buffer_out);
    *buffer_out = (ctool_buffer_t *)0;
    return 0;
  }
  return 1;
}

static int build_relocated_local_target_object(
    ctool_job_t *job, ctool_buffer_t **buffer_out) {
  static const ctool_u8 text[] = {
      0xe8u, 0xfcu, 0xffu, 0xffu, 0xffu, 0xc3u};
  ctool_elf32_section_spec_t section;
  ctool_elf32_symbol_spec_t symbol;
  ctool_elf32_relocation_spec_t relocation;
  ctool_elf32_object_spec_t object;
  ctool_status_t status =
      ctool_job_open_buffer(job, 256u, ctool_default_limits().output_bytes,
                            buffer_out);
  if (status != CTOOL_OK) {
    return 0;
  }
  (void)memset(&section, 0, sizeof(section));
  section.name = ctool_string(".text");
  section.type = CTOOL_ELF32_SHT_PROGBITS;
  section.flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR;
  section.alignment = 1u;
  section.size = (ctool_u32)sizeof(text);
  section.contents = ctool_bytes(text, (ctool_u32)sizeof(text));
  (void)memset(&symbol, 0, sizeof(symbol));
  symbol.name = ctool_string("external");
  symbol.binding = CTOOL_ELF32_BIND_GLOBAL;
  symbol.type = CTOOL_ELF32_SYMBOL_FUNCTION;
  symbol.visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbol.placement = CTOOL_ELF32_SYMBOL_UNDEFINED;
  symbol.section = CTOOL_ELF32_NO_SECTION;
  relocation.target_section = 0u;
  relocation.offset = 1u;
  relocation.symbol = 0u;
  relocation.type = CTOOL_ELF32_R_386_PC32;
  relocation.addend = -4;
  (void)memset(&object, 0, sizeof(object));
  object.sections = &section;
  object.section_count = 1u;
  object.symbols = &symbol;
  object.symbol_count = 1u;
  object.relocations = &relocation;
  object.relocation_count = 1u;
  status = ctool_elf32_write(job, &object, *buffer_out);
  if (status != CTOOL_OK) {
    ctool_buffer_close(*buffer_out);
    *buffer_out = (ctool_buffer_t *)0;
    return 0;
  }
  return 1;
}

static int build_two_section_local_target_object(
    ctool_job_t *job, ctool_buffer_t **buffer_out) {
  static const ctool_u8 first_text[] = {0x90u, 0x90u, 0x90u, 0xc3u};
  static const ctool_u8 second_text[] = {
      0xebu, 0x01u, 0xb8u, 0x00u, 0x00u, 0x00u, 0x00u, 0xc3u};
  ctool_elf32_section_spec_t sections[2];
  ctool_elf32_object_spec_t object;
  ctool_status_t status =
      ctool_job_open_buffer(job, 256u, ctool_default_limits().output_bytes,
                            buffer_out);
  if (status != CTOOL_OK) {
    return 0;
  }
  (void)memset(sections, 0, sizeof(sections));
  sections[0].name = ctool_string(".text.first");
  sections[0].type = CTOOL_ELF32_SHT_PROGBITS;
  sections[0].flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR;
  sections[0].alignment = 1u;
  sections[0].size = (ctool_u32)sizeof(first_text);
  sections[0].contents =
      ctool_bytes(first_text, (ctool_u32)sizeof(first_text));
  sections[1].name = ctool_string(".text.second");
  sections[1].type = CTOOL_ELF32_SHT_PROGBITS;
  sections[1].flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR;
  sections[1].alignment = 1u;
  sections[1].size = (ctool_u32)sizeof(second_text);
  sections[1].contents =
      ctool_bytes(second_text, (ctool_u32)sizeof(second_text));
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

static int run_targets(void) {
  static const ctool_u8 valid32[] = {0xebu, 0x01u, 0x90u, 0xc3u};
  static const ctool_u8 middle32[] = {
      0xebu, 0x01u, 0xb8u, 0x00u, 0x00u, 0x00u, 0x00u, 0xc3u};
  static const ctool_u8 outside32[] = {0xebu, 0x7fu};
  static const ctool_u8 mapped[] = {0xebu, 0x00u, 0x90u, 0xc3u};
  static const ctool_u8 cross_data[] = {
      0xebu, 0x02u, 0x11u, 0x22u, 0xc3u};
  static const ctool_u8 wrap16[] = {0xebu, 0x00u, 0xc3u};
  static const ctool_u8 direct_far_indirect[] = {
      0xebu, 0xfeu,
      0xeau, 0x00u, 0x00u, 0x00u, 0x00u, 0x08u, 0x00u,
      0xffu, 0xd0u};
  static const ctool_u8 direct_families[] = {
      0xebu, 0x00u,
      0xe9u, 0x00u, 0x00u, 0x00u, 0x00u,
      0xe8u, 0x00u, 0x00u, 0x00u, 0x00u,
      0x74u, 0x00u,
      0x0fu, 0x85u, 0x00u, 0x00u, 0x00u, 0x00u,
      0xc3u};
  static const ctool_u8 source_edge_valid[] = {
      0xebu, 0x00u, 0xc3u, 0xc3u};
  static const ctool_u8 source_edge_changed[] = {
      0xebu, 0x01u, 0xc3u, 0xc3u};
  static const ctool_u8 source_edge_middle[] = {
      0xebu, 0x01u, 0xb8u, 0x00u, 0x00u, 0x00u, 0x00u};
  static const ctool_u8 source_edge_cross_mode[] = {
      0x66u, 0xeau, 0x00u, 0x00u, 0x10u, 0x00u, 0x08u, 0x00u};
  static const ctool_u8 source_edge_maximum[] = {
      0xeau, 0xffu, 0xffu, 0xffu, 0xffu, 0x08u, 0x00u};
  static ctool_u8 oversized_code16[65537];
  static ctool_u8 elf_code_fixture[16384];
  static ctool_u8 elf_allocation_fixture[24576];
  ctool_u32 elf_allocation_fixture_size = 0u;
  static const ctool_dis_raw_range_t data_ranges[] = {
      {0u, CTOOL_DIS_RAW_RANGE_CODE32},
      {2u, CTOOL_DIS_RAW_RANGE_DATA}};
  static const ctool_dis_raw_range_t wrong_mode_ranges[] = {
      {0u, CTOOL_DIS_RAW_RANGE_CODE32},
      {2u, CTOOL_DIS_RAW_RANGE_CODE16}};
  static const ctool_dis_raw_range_t cross_data_ranges[] = {
      {0u, CTOOL_DIS_RAW_RANGE_CODE32},
      {2u, CTOOL_DIS_RAW_RANGE_DATA},
      {4u, CTOOL_DIS_RAW_RANGE_CODE32}};
  static const ctool_dis_raw_range_t source_edge_ranges[] = {
      {0u, CTOOL_DIS_RAW_RANGE_CODE32}};
  static const ctool_dis_raw_range_t source_edge_ranges16[] = {
      {0u, CTOOL_DIS_RAW_RANGE_CODE16}};
  static const ctool_dis_raw_edge_t source_edges[] = {
      {0u, CTOOL_DIS_RAW_EDGE_RELATIVE, CTOOL_DIS_RAW_EDGE_LOCAL,
       2u, 0x1002u, CTOOL_X86_MODE_32, 0u}};
  static const ctool_dis_raw_edge_t middle_edges[] = {
      {0u, CTOOL_DIS_RAW_EDGE_RELATIVE, CTOOL_DIS_RAW_EDGE_LOCAL,
       3u, 0x1003u, CTOOL_X86_MODE_32, 0u}};
  static const ctool_dis_raw_edge_t duplicate_edges[] = {
      {0u, CTOOL_DIS_RAW_EDGE_RELATIVE, CTOOL_DIS_RAW_EDGE_LOCAL,
       2u, 0x1002u, CTOOL_X86_MODE_32, 0u},
      {0u, CTOOL_DIS_RAW_EDGE_RELATIVE, CTOOL_DIS_RAW_EDGE_LOCAL,
       2u, 0x1002u, CTOOL_X86_MODE_32, 0u}};
  static const ctool_dis_raw_edge_t inside_external_edge[] = {
      {0u, CTOOL_DIS_RAW_EDGE_RELATIVE, CTOOL_DIS_RAW_EDGE_EXTERNAL,
       CTOOL_DIS_RAW_EDGE_NO_TARGET, 0x1002u, CTOOL_X86_MODE_32, 0u}};
  static const ctool_dis_raw_edge_t wrapped_edge[] = {
      {0u, CTOOL_DIS_RAW_EDGE_RELATIVE, CTOOL_DIS_RAW_EDGE_LOCAL,
       2u, 0x10001u, CTOOL_X86_MODE_16, 0u}};
  static const ctool_dis_raw_edge_t cross_mode_edge[] = {
      {0u, CTOOL_DIS_RAW_EDGE_FAR, CTOOL_DIS_RAW_EDGE_EXTERNAL,
       CTOOL_DIS_RAW_EDGE_NO_TARGET, 0x00100000u, CTOOL_X86_MODE_32, 8u}};
  static const ctool_dis_raw_edge_t maximum_edge[] = {
      {0u, CTOOL_DIS_RAW_EDGE_FAR, CTOOL_DIS_RAW_EDGE_EXTERNAL,
       CTOOL_DIS_RAW_EDGE_NO_TARGET, 0xffffffffu, CTOOL_X86_MODE_32, 8u}};
  static const struct {
    const char *path;
    const ctool_u8 *bytes;
    ctool_u32 size;
    ctool_x86_mode_t mode;
    ctool_u32 base;
    const ctool_dis_raw_range_t *ranges;
    ctool_u32 range_count;
    ctool_u64 target_count;
    ctool_u64 outside_image;
    ctool_u64 data;
    ctool_u64 wrong_mode;
    ctool_u64 mid_instruction;
  } cases[] = {
      {"/valid32.bin", valid32, (ctool_u32)sizeof(valid32),
       CTOOL_X86_MODE_32, 0u, (const ctool_dis_raw_range_t *)0, 0u,
       1u, 0u, 0u, 0u, 0u},
      {"/middle32.bin", middle32, (ctool_u32)sizeof(middle32),
       CTOOL_X86_MODE_32, 0u, (const ctool_dis_raw_range_t *)0, 0u,
       1u, 0u, 0u, 0u, 1u},
      {"/outside32.bin", outside32, (ctool_u32)sizeof(outside32),
       CTOOL_X86_MODE_32, 0u, (const ctool_dis_raw_range_t *)0, 0u,
       1u, 1u, 0u, 0u, 0u},
      {"/data32.bin", mapped, (ctool_u32)sizeof(mapped),
       CTOOL_DIS_RAW_RANGE_MAP, 0u, data_ranges, 2u,
       1u, 0u, 1u, 0u, 0u},
      {"/wrong-mode.bin", mapped, (ctool_u32)sizeof(mapped),
       CTOOL_DIS_RAW_RANGE_MAP, 0u, wrong_mode_ranges, 2u,
       1u, 0u, 0u, 1u, 0u},
      {"/cross-data.bin", cross_data, (ctool_u32)sizeof(cross_data),
       CTOOL_DIS_RAW_RANGE_MAP, 0u, cross_data_ranges, 3u,
       1u, 0u, 0u, 0u, 0u},
      {"/wrap16.bin", wrap16, (ctool_u32)sizeof(wrap16),
       CTOOL_X86_MODE_16, 0xfffeu, (const ctool_dis_raw_range_t *)0, 0u,
       1u, 0u, 0u, 0u, 0u},
      {"/direct-far-indirect.bin", direct_far_indirect,
       (ctool_u32)sizeof(direct_far_indirect), CTOOL_X86_MODE_32, 0u,
       (const ctool_dis_raw_range_t *)0, 0u,
       1u, 0u, 0u, 0u, 0u},
      {"/direct-families.bin", direct_families,
       (ctool_u32)sizeof(direct_families), CTOOL_X86_MODE_32, 0u,
       (const ctool_dis_raw_range_t *)0, 0u,
       5u, 0u, 0u, 0u, 0u}};
  ctool_host_adapter_t adapter;
  ctool_job_t *job;
  ctool_u32 index;
  if (!open_job(&adapter, &job)) {
    return 1;
  }
  for (index = 0u;
       index < (ctool_u32)(sizeof(cases) / sizeof(cases[0])); index++) {
    ctool_source_t source;
    ctool_dis_request_t request = raw_request(cases[index].mode,
                                               cases[index].base);
    ctool_dis_report_t report;
    ctool_status_t status;
    source.path.text = ctool_string(cases[index].path);
    source.contents = ctool_bytes(cases[index].bytes, cases[index].size);
    request.policies = CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS;
    request.raw_ranges = cases[index].ranges;
    request.raw_range_count = cases[index].range_count;
    status = ctool_dis_inspect(job, &source, &request, &report);
    if (!check_status(status, CTOOL_OK, cases[index].path) ||
        !target_summary_matches(
            &report.decode_summary, cases[index].target_count,
            cases[index].outside_image, 0u,
            cases[index].data, cases[index].wrong_mode,
            cases[index].mid_instruction, cases[index].path)) {
      ctool_job_close(job);
      return 1;
    }
  }
  {
    ctool_host_adapter_t edge_adapter;
    ctool_job_t *edge_job;
    ctool_source_t source;
    ctool_dis_request_t request =
        raw_request(CTOOL_DIS_RAW_RANGE_MAP, 0x1000u);
    ctool_dis_report_t report;
    ctool_status_t status;
    if (!open_job(&edge_adapter, &edge_job)) {
      ctool_job_close(job);
      return 1;
    }
    request.policies = CTOOL_DIS_POLICY_SOURCE_CONTROL_EDGES;
    request.raw_ranges = source_edge_ranges;
    request.raw_range_count = 1u;
    request.raw_edges = source_edges;
    request.raw_edge_count = 1u;
    request.raw_edge_metadata_present = CTOOL_TRUE;
    source.path.text = ctool_string("/source-edge.bin");
    source.contents = ctool_bytes(source_edge_valid,
                                  (ctool_u32)sizeof(source_edge_valid));
    status = ctool_dis_inspect(edge_job, &source, &request, &report);
    if (!check_status(status, CTOOL_OK, "valid source control edge") ||
        report.decode_summary.source_control_edge_count != 1u ||
        report.decode_summary.source_control_edge_local_count != 1u ||
        report.decode_summary.source_control_edge_invalid_count != 0u) {
      (void)fprintf(stderr, "valid source control-edge summary differs\n");
      ctool_job_close(edge_job);
      ctool_job_close(job);
      return 1;
    }

    source.contents = ctool_bytes(source_edge_changed,
                                  (ctool_u32)sizeof(source_edge_changed));
    status = ctool_dis_inspect(edge_job, &source, &request, &report);
    if (!check_status(status, CTOOL_OK,
                      "changed source control edge") ||
        report.decode_summary.source_control_edge_invalid_count != 1u ||
        report.decode_summary.source_control_edge_target_mismatch_count !=
            1u) {
      (void)fprintf(stderr, "changed source control-edge summary differs\n");
      ctool_job_close(edge_job);
      ctool_job_close(job);
      return 1;
    }

    source.contents = ctool_bytes(source_edge_middle,
                                  (ctool_u32)sizeof(source_edge_middle));
    request.raw_edges = middle_edges;
    status = ctool_dis_inspect(edge_job, &source, &request, &report);
    if (!check_status(status, CTOOL_OK,
                      "middle-instruction source edge") ||
        report.decode_summary.source_control_edge_invalid_count != 1u ||
        report.decode_summary.source_control_edge_target_mismatch_count !=
            1u) {
      (void)fprintf(stderr,
                    "middle-instruction source control edge differs\n");
      ctool_job_close(edge_job);
      ctool_job_close(job);
      return 1;
    }

    source.contents = ctool_bytes(source_edge_valid,
                                  (ctool_u32)sizeof(source_edge_valid));
    request.raw_edges = duplicate_edges;
    request.raw_edge_count = 2u;
    (void)memset(&report, 0xa5, sizeof(report));
    status = ctool_dis_inspect(edge_job, &source, &request, &report);
    if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                      "duplicate source control edges") ||
        report.source != (const ctool_source_t *)0) {
      (void)fprintf(stderr, "duplicate source control-edge check differs\n");
      ctool_job_close(edge_job);
      ctool_job_close(job);
      return 1;
    }

    request.raw_edges = inside_external_edge;
    request.raw_edge_count = 1u;
    status = ctool_dis_inspect(edge_job, &source, &request, &report);
    if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                      "in-image external source control edge") ||
        report.source != (const ctool_source_t *)0) {
      (void)fprintf(stderr, "in-image external edge check differs\n");
      ctool_job_close(edge_job);
      ctool_job_close(job);
      return 1;
    }

    source.path.text = ctool_string("/wrapped-source-edge.bin");
    source.contents = ctool_bytes(wrap16, (ctool_u32)sizeof(wrap16));
    request.raw_base_address = 0xffffu;
    request.raw_ranges = source_edge_ranges16;
    request.raw_edges = wrapped_edge;
    status = ctool_dis_inspect(edge_job, &source, &request, &report);
    if (!check_status(status, CTOOL_OK, "wrapped source control edge") ||
        report.decode_summary.source_control_edge_invalid_count != 0u) {
      (void)fprintf(stderr, "wrapped source control edge differs\n");
      ctool_job_close(edge_job);
      ctool_job_close(job);
      return 1;
    }

    source.path.text = ctool_string("/cross-mode-source-edge.bin");
    source.contents = ctool_bytes(
        source_edge_cross_mode,
        (ctool_u32)sizeof(source_edge_cross_mode));
    request.raw_base_address = 0x8000u;
    request.raw_edges = cross_mode_edge;
    status = ctool_dis_inspect(edge_job, &source, &request, &report);
    if (!check_status(status, CTOOL_OK,
                      "cross-mode external source edge") ||
        report.decode_summary.source_control_edge_invalid_count != 0u) {
      (void)fprintf(stderr, "cross-mode external source edge differs\n");
      ctool_job_close(edge_job);
      ctool_job_close(job);
      return 1;
    }

    source.path.text = ctool_string("/maximum-source-edge.bin");
    source.contents = ctool_bytes(
        source_edge_maximum,
        (ctool_u32)sizeof(source_edge_maximum));
    request.raw_base_address = 0u;
    request.raw_ranges = source_edge_ranges;
    request.raw_edges = maximum_edge;
    status = ctool_dis_inspect(edge_job, &source, &request, &report);
    if (!check_status(status, CTOOL_OK,
                      "maximum external source edge") ||
        report.decode_summary.source_control_edge_invalid_count != 0u) {
      (void)fprintf(stderr, "maximum external source edge differs\n");
      ctool_job_close(edge_job);
      ctool_job_close(job);
      return 1;
    }

    source.path.text = ctool_string("/source-edge.bin");
    source.contents = ctool_bytes(source_edge_valid,
                                  (ctool_u32)sizeof(source_edge_valid));
    request.raw_base_address = 0x1000u;
    request.raw_edges = source_edges;
    request.raw_edge_count = 1u;
    status = ctool_dis_inspect(edge_job, &source, &request, &report);
    if (!check_status(status, CTOOL_OK,
                      "source control-edge same-job recovery") ||
        report.decode_summary.source_control_edge_invalid_count != 0u) {
      (void)fprintf(stderr,
                    "source control-edge same-job recovery differs\n");
      ctool_job_close(edge_job);
      ctool_job_close(job);
      return 1;
    }
    ctool_job_close(edge_job);
  }
  {
    ctool_buffer_t *object_bytes = (ctool_buffer_t *)0;
    ctool_source_t source;
    ctool_dis_request_t request;
    ctool_dis_report_t report;
    capture_t capture;
    ctool_status_t status;
    if (!build_local_target_object(
            job, valid32, (ctool_u32)sizeof(valid32), &object_bytes)) {
      ctool_job_close(job);
      return 1;
    }
    source.path.text = ctool_string("/valid-local-target.o");
    source.contents = ctool_buffer_view(object_bytes);
    (void)memset(&request, 0, sizeof(request));
    request.input = CTOOL_DIS_INPUT_ELF32;
    request.views = CTOOL_DIS_VIEW_DISASSEMBLY;
    request.policies = CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS;
    status = ctool_dis_inspect(job, &source, &request, &report);
    (void)memset(&capture, 0, sizeof(capture));
    if (status == CTOOL_OK) {
      status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                                capture_sink(&capture));
    }
    if (!check_status(status, CTOOL_OK, "ELF local target policy") ||
        report.elf32.file_type != CTOOL_ELF32_ET_REL ||
        report.policies != CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS ||
        !target_summary_matches(&report.decode_summary, 1u, 0u, 0u, 0u, 0u,
                                0u, "ELF local target policy") ||
        !contains(&capture, "[disassembly .text]",
                  "ELF local target rendering")) {
      ctool_buffer_close(object_bytes);
      ctool_job_close(job);
      return 1;
    }
    {
      ctool_dis_report_t invalid_report = report;
      invalid_report.elf32.file_type = CTOOL_ELF32_ET_EXEC;
      (void)memset(&capture, 0, sizeof(capture));
      status = ctool_dis_render(job, &invalid_report,
                                CTOOL_DIS_TEXT_CUPID,
                                capture_sink(&capture));
      if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                        "forged executable local target report") ||
          capture.size != 0u) {
        ctool_buffer_close(object_bytes);
        ctool_job_close(job);
        return 1;
      }
    }
    ctool_buffer_close(object_bytes);
  }
  {
    ctool_buffer_t *object_bytes = (ctool_buffer_t *)0;
    ctool_source_t source;
    ctool_dis_request_t request;
    ctool_dis_report_t report;
    ctool_status_t status;
    if (!build_two_section_local_target_object(job, &object_bytes)) {
      ctool_job_close(job);
      return 1;
    }
    source.path.text = ctool_string("/two-section-local-target.o");
    source.contents = ctool_buffer_view(object_bytes);
    (void)memset(&request, 0, sizeof(request));
    request.input = CTOOL_DIS_INPUT_ELF32;
    request.views = CTOOL_DIS_VIEW_DISASSEMBLY;
    request.policies = CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS;
    status = ctool_dis_inspect(job, &source, &request, &report);
    if (!check_status(status, CTOOL_OK,
                      "two-section ELF local target policy") ||
        report.decode_summary.known_count != 7u ||
        !target_summary_matches(&report.decode_summary, 1u, 0u, 0u, 0u, 0u,
                                1u,
                                "two-section ELF local target policy")) {
      ctool_buffer_close(object_bytes);
      ctool_job_close(job);
      return 1;
    }
    ctool_buffer_close(object_bytes);
  }
  {
    ctool_host_adapter_t view_adapter;
    ctool_job_t *view_job;
    ctool_buffer_t *object_bytes = (ctool_buffer_t *)0;
    ctool_source_t source;
    ctool_dis_request_t request;
    ctool_dis_report_t report;
    ctool_status_t status;
    if (!open_job(&view_adapter, &view_job) ||
        !build_local_target_object(
            view_job, valid32, (ctool_u32)sizeof(valid32), &object_bytes)) {
      ctool_job_close(job);
      return 1;
    }
    source.path.text = ctool_string("/missing-local-target-view.o");
    source.contents = ctool_buffer_view(object_bytes);
    (void)memset(&request, 0, sizeof(request));
    request.input = CTOOL_DIS_INPUT_ELF32;
    request.views = CTOOL_DIS_VIEW_HEADER;
    request.policies = CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS;
    (void)memset(&report, 0xa5, sizeof(report));
    status = ctool_dis_inspect(view_job, &source, &request, &report);
    if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                      "ELF local target view") ||
        !is_zeroed(&report, sizeof(report)) ||
        ctool_job_diagnostic_count(view_job) != 1u ||
        !check_diagnostic(
            view_job, 0u, CTOOL_DIS_DIAG_INVALID_REQUEST,
            "ELF local target checks require the disassembly view",
            "ELF local target view diagnostic")) {
      ctool_buffer_close(object_bytes);
      ctool_job_close(view_job);
      ctool_job_close(job);
      return 1;
    }
    ctool_buffer_close(object_bytes);
    ctool_job_close(view_job);
  }
  {
    static const struct {
      const char *path;
      const ctool_u8 *bytes;
      ctool_u32 size;
      ctool_u64 outside_section;
      ctool_u64 mid_instruction;
    } elf_cases[] = {
        {"/outside-local-target.o", outside32,
         (ctool_u32)sizeof(outside32), 1u, 0u},
        {"/middle-local-target.o", middle32,
         (ctool_u32)sizeof(middle32), 0u, 1u},
        {"/far-indirect-local-target.o", direct_far_indirect,
         (ctool_u32)sizeof(direct_far_indirect), 0u, 0u}};
    for (index = 0u;
         index < (ctool_u32)(sizeof(elf_cases) / sizeof(elf_cases[0]));
         index++) {
      ctool_buffer_t *object_bytes = (ctool_buffer_t *)0;
      ctool_source_t source;
      ctool_dis_request_t request;
      ctool_dis_report_t report;
      ctool_status_t status;
      if (!build_local_target_object(job, elf_cases[index].bytes,
                                     elf_cases[index].size,
                                     &object_bytes)) {
        ctool_job_close(job);
        return 1;
      }
      source.path.text = ctool_string(elf_cases[index].path);
      source.contents = ctool_buffer_view(object_bytes);
      (void)memset(&request, 0, sizeof(request));
      request.input = CTOOL_DIS_INPUT_ELF32;
      request.views = CTOOL_DIS_VIEW_DISASSEMBLY;
      request.policies = CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS;
      status = ctool_dis_inspect(job, &source, &request, &report);
      if (!check_status(status, CTOOL_OK, elf_cases[index].path) ||
          !target_summary_matches(
              &report.decode_summary, 1u, 0u,
              elf_cases[index].outside_section, 0u, 0u,
              elf_cases[index].mid_instruction, elf_cases[index].path)) {
        ctool_buffer_close(object_bytes);
        ctool_job_close(job);
        return 1;
      }
      ctool_buffer_close(object_bytes);
    }
  }
  {
    ctool_buffer_t *object_bytes = (ctool_buffer_t *)0;
    ctool_source_t source;
    ctool_dis_request_t request;
    ctool_dis_report_t report;
    ctool_status_t status;
    if (!build_relocated_local_target_object(job, &object_bytes)) {
      ctool_job_close(job);
      return 1;
    }
    source.path.text = ctool_string("/relocated-external-target.o");
    source.contents = ctool_buffer_view(object_bytes);
    (void)memset(&request, 0, sizeof(request));
    request.input = CTOOL_DIS_INPUT_ELF32;
    request.views = CTOOL_DIS_VIEW_DISASSEMBLY;
    request.policies = CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS;
    status = ctool_dis_inspect(job, &source, &request, &report);
    if (!check_status(status, CTOOL_OK,
                      "relocated ELF local target policy") ||
        report.decode_summary.executable_relocation_count != 1u ||
        report.decode_summary.unmatched_executable_relocation_count != 0u ||
        !target_summary_matches(&report.decode_summary, 0u, 0u, 0u, 0u, 0u,
                                0u, "relocated ELF local target policy")) {
      ctool_buffer_close(object_bytes);
      ctool_job_close(job);
      return 1;
    }
    ctool_buffer_close(object_bytes);
  }
  {
    ctool_source_t source;
    ctool_dis_request_t request = raw_request(CTOOL_X86_MODE_32, 0u);
    ctool_dis_report_t report;
    ctool_status_t status;
    source.path.text = ctool_string("/invalid-policy.bin");
    source.contents = ctool_bytes(valid32, (ctool_u32)sizeof(valid32));
    request.policies = 0x80000000u;
    (void)memset(&report, 0xa5, sizeof(report));
    status = ctool_dis_inspect(job, &source, &request, &report);
    if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                      "unknown local target policy") ||
        !is_zeroed(&report, sizeof(report)) ||
        ctool_job_diagnostic_count(job) != 1u ||
        !check_diagnostic(job, 0u, CTOOL_DIS_DIAG_INVALID_REQUEST,
                          "CupidDis policy selection is invalid",
                          "unknown policy diagnostic")) {
      ctool_job_close(job);
      return 1;
    }

    request.input = CTOOL_DIS_INPUT_ELF32;
    request.policies = CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS;
    (void)memset(&report, 0xa5, sizeof(report));
    status = ctool_dis_inspect(job, &source, &request, &report);
    if (!check_status(status, CTOOL_ERR_INPUT,
                      "malformed ELF local target policy") ||
        !is_zeroed(&report, sizeof(report)) ||
        ctool_job_diagnostic_count(job) != 2u ||
        !check_diagnostic(job, 1u, CTOOL_ELF32_DIAG_BAD_HEADER,
                          "ELF32 header is truncated",
                          "malformed ELF policy diagnostic")) {
      ctool_job_close(job);
      return 1;
    }

    source.path.text = ctool_string("/empty-policy.bin");
    source.contents = ctool_bytes((const void *)0, 0u);
    request = raw_request(CTOOL_X86_MODE_32, 0u);
    request.policies = CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS;
    status = ctool_dis_inspect(job, &source, &request, &report);
    if (!check_status(status, CTOOL_OK, "empty local target policy") ||
        report.decode_summary.direct_relative_target_count != 0u ||
        report.decode_summary.known_count != 0u) {
      ctool_job_close(job);
      return 1;
    }

    source.path.text = ctool_string("/oversized-code16.bin");
    source.contents =
        ctool_bytes(oversized_code16, (ctool_u32)sizeof(oversized_code16));
    request = raw_request(CTOOL_X86_MODE_16, 0u);
    request.policies = CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS;
    (void)memset(&report, 0xa5, sizeof(report));
    status = ctool_dis_inspect(job, &source, &request, &report);
    if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                      "oversized code16 local targets") ||
        !is_zeroed(&report, sizeof(report)) ||
        ctool_job_diagnostic_count(job) != 3u ||
        !check_diagnostic(
            job, 2u, CTOOL_DIS_DIAG_INVALID_REQUEST,
            "local target checks require code16 raw input at most 65536 bytes",
            "oversized code16 policy diagnostic")) {
      ctool_job_close(job);
      return 1;
    }

    source.path.text = ctool_string("/policy-recovery.bin");
    source.contents = ctool_bytes(valid32, (ctool_u32)sizeof(valid32));
    request = raw_request(CTOOL_X86_MODE_32, 0u);
    request.policies = CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS;
    status = ctool_dis_inspect(job, &source, &request, &report);
    if (!check_status(status, CTOOL_OK, "local target policy recovery") ||
        report.policies != CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS ||
        !target_summary_matches(&report.decode_summary, 1u, 0u, 0u, 0u, 0u,
                                0u, "local target policy recovery")) {
      ctool_job_close(job);
      return 1;
    }
    {
      ctool_dis_report_t invalid_report = report;
      capture_t capture;
      invalid_report.policies = 0x80000000u;
      (void)memset(&capture, 0, sizeof(capture));
      status = ctool_dis_render(job, &invalid_report, CTOOL_DIS_TEXT_CUPID,
                                capture_sink(&capture));
      if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                        "forged local target policy report") ||
          capture.size != 0u || ctool_job_diagnostic_count(job) != 3u) {
        ctool_job_close(job);
        return 1;
      }
    }
  }
  {
    ctool_buffer_t *object_bytes = (ctool_buffer_t *)0;
    ctool_bytes_t object_view;
    if (!build_local_target_object(
            job, elf_code_fixture, (ctool_u32)sizeof(elf_code_fixture),
            &object_bytes)) {
      ctool_job_close(job);
      return 1;
    }
    object_view = ctool_buffer_view(object_bytes);
    if (object_view.size > (ctool_u32)sizeof(elf_allocation_fixture)) {
      ctool_buffer_close(object_bytes);
      ctool_job_close(job);
      return 1;
    }
    (void)memcpy(elf_allocation_fixture, object_view.data,
                 object_view.size);
    elf_allocation_fixture_size = object_view.size;
    ctool_buffer_close(object_bytes);
  }
  ctool_job_close(job);
  {
    static ctool_u8 allocation_fixture[1024];
    ctool_host_adapter_t limited_adapter;
    ctool_job_config_t config;
    ctool_limits_t limits = ctool_default_limits();
    ctool_source_t source;
    ctool_dis_request_t request = raw_request(CTOOL_X86_MODE_32, 0u);
    ctool_dis_report_t report;
    ctool_status_t status =
        ctool_host_adapter_init(&limited_adapter, ".");
    job = (ctool_job_t *)0;
    limits.arena_block_bytes = 64u;
    limits.arena_bytes = 64u;
    config = ctool_host_job_config(&limited_adapter, limits);
    if (status == CTOOL_OK) {
      status = ctool_job_open(&config, &job);
    }
    if (!check_status(status, CTOOL_OK, "limited local target job")) {
      return 1;
    }
    source.path.text = ctool_string("/local-target-limit.bin");
    source.contents = ctool_bytes(allocation_fixture,
                                  (ctool_u32)sizeof(allocation_fixture));
    request.policies = CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS;
    (void)memset(&report, 0xa5, sizeof(report));
    status = ctool_dis_inspect(job, &source, &request, &report);
    if (!check_status(status, CTOOL_ERR_LIMIT,
                      "local target instruction map limit") ||
        !is_zeroed(&report, sizeof(report)) ||
        ctool_job_diagnostic_count(job) != 0u) {
      ctool_job_close(job);
      return 1;
    }
    request.policies = 0u;
    status = ctool_dis_inspect(job, &source, &request, &report);
    if (!check_status(status, CTOOL_OK,
                      "local target allocation recovery") ||
        report.decode_summary.known_count != 512u ||
        report.decode_summary.direct_relative_target_count != 0u) {
      ctool_job_close(job);
      return 1;
    }
    ctool_job_close(job);
  }
  {
    ctool_host_adapter_t limited_adapter;
    ctool_job_config_t config;
    ctool_limits_t limits = ctool_default_limits();
    ctool_source_t source;
    ctool_dis_request_t request;
    ctool_dis_report_t report;
    ctool_status_t status = ctool_host_adapter_init(&limited_adapter, ".");
    job = (ctool_job_t *)0;
    limits.arena_block_bytes = 512u;
    limits.arena_bytes = 2047u;
    config = ctool_host_job_config(&limited_adapter, limits);
    if (status == CTOOL_OK) {
      status = ctool_job_open(&config, &job);
    }
    if (!check_status(status, CTOOL_OK, "limited ELF local target job")) {
      return 1;
    }
    source.path.text = ctool_string("/local-target-limit.o");
    source.contents = ctool_bytes(elf_allocation_fixture,
                                  elf_allocation_fixture_size);
    (void)memset(&request, 0, sizeof(request));
    request.input = CTOOL_DIS_INPUT_ELF32;
    request.views = CTOOL_DIS_VIEW_DISASSEMBLY;
    request.policies = CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS;
    (void)memset(&report, 0xa5, sizeof(report));
    status = ctool_dis_inspect(job, &source, &request, &report);
    if (!check_status(status, CTOOL_ERR_LIMIT,
                      "ELF local target instruction map limit") ||
        !is_zeroed(&report, sizeof(report)) ||
        ctool_job_diagnostic_count(job) != 0u) {
      ctool_job_close(job);
      return 1;
    }
    request.policies = 0u;
    status = ctool_dis_inspect(job, &source, &request, &report);
    if (!check_status(status, CTOOL_OK,
                      "ELF local target allocation recovery") ||
        report.decode_summary.known_count != 8192u ||
        report.decode_summary.direct_relative_target_count != 0u) {
      ctool_job_close(job);
      return 1;
    }
    ctool_job_close(job);
  }
  (void)puts("targets: ok");
  return 0;
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
  static const ctool_u8 parity_setcc[] = {
      0x0fu, 0x94u, 0xc0u, 0x0fu, 0x9bu, 0xc2u,
      0x20u, 0xd0u, 0x0fu, 0xb6u, 0xc0u,
      0x0fu, 0x95u, 0xc0u, 0x0fu, 0x9au, 0xc2u,
      0x08u, 0xd0u, 0x0fu, 0xb6u, 0xc0u, 0xc3u};
  static const ctool_u8 immediate_imul32[] = {
      0x69u, 0xc1u, 0x28u, 0x02u, 0x00u, 0x00u,
      0x6bu, 0xb4u, 0x8bu, 0x78u, 0x56u, 0x34u, 0x12u, 0xf9u,
      0x66u, 0x69u, 0xc1u, 0x34u, 0x12u};
  static const ctool_u8 immediate_imul16[] = {
      0x69u, 0xc1u, 0x34u, 0x12u,
      0x66u, 0x69u, 0x40u, 0x7fu, 0x78u, 0x56u, 0x34u, 0x12u};
  static const ctool_u8 immediate_imul_recovery[] = {
      0xf0u, 0x6bu, 0xc1u, 0x02u, 0x69u, 0xc1u, 0x34u};
  static const ctool_u8 padding_nops32[] = {
      0x66u, 0x90u,
      0x0fu, 0x1fu, 0x00u,
      0x66u, 0x2eu, 0x0fu, 0x1fu, 0x84u,
      0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
      0xc3u};
  static const ctool_u8 padding_nop_recovery[] = {
      0xf0u, 0x0fu, 0x1fu, 0x00u, 0x0fu, 0x1fu};
  static const ctool_u8 clang_padding_nops[] = {
      0x66u, 0x66u, 0x2eu, 0x0fu, 0x1fu, 0x84u,
      0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
      0x66u, 0x66u, 0x66u, 0x2eu, 0x0fu, 0x1fu, 0x84u,
      0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
      0x66u, 0x66u, 0x66u, 0x66u, 0x2eu, 0x0fu, 0x1fu, 0x84u,
      0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
      0x66u, 0x66u, 0x66u, 0x66u, 0x66u, 0x2eu, 0x0fu, 0x1fu,
      0x84u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
      0x66u, 0x66u, 0x66u, 0x66u, 0x66u, 0x66u, 0x2eu, 0x0fu,
      0x1fu, 0x84u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
      0xc3u};
  static const ctool_u8 clang_padding_near_miss[] = {
      0x66u, 0x66u, 0x90u, 0xc3u};
  static const ctool_u8 mixed_mode[] = {
      0xb8u, 0x34u, 0x12u,
      0x00u, 0x00u, 0x90u, 0xc3u,
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
  static const ctool_u8 decode_summary[] = {
      0x90u, 0x0fu, 0xffu, 0xc0u, 0x66u, 0x66u, 0x90u, 0x0fu,
      0x0fu, 0xffu, 0x66u, 0x66u, 0x0fu};
  ctool_host_adapter_t adapter;
  ctool_job_t *job;
  ctool_source_t source;
  ctool_dis_request_t request;
  ctool_dis_report_t report;
  ctool_dis_label_t labels[2];
  ctool_dis_raw_range_t mixed_ranges[4];
  ctool_dis_raw_range_t invalid_ranges[3];
  ctool_dis_raw_range_t changed_ranges[4];
  ctool_dis_raw_range_t summary_ranges[2];
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

  (void)memset(&capture, 0, sizeof(capture));
  source.path.text = ctool_string("/parity-setcc.bin");
  source.contents = ctool_bytes(
      parity_setcc, (ctool_u32)sizeof(parity_setcc));
  request = raw_request(CTOOL_X86_MODE_32, 0x00401800u);
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (status == CTOOL_OK) {
    status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&capture));
  }
  if (!check_status(status, CTOOL_OK, "parity SETcc inspection") ||
      !contains(&capture, "sete al", "parity SETcc equality predicate") ||
      !contains(&capture, "setnp dl", "parity SETcc ordered guard") ||
      !contains(&capture, "and al, dl", "parity SETcc ordered merge") ||
      !contains(&capture, "setne al", "parity SETcc inequality predicate") ||
      !contains(&capture, "setp dl", "parity SETcc unordered guard") ||
      !contains(&capture, "or al, dl", "parity SETcc unordered merge") ||
      !contains(&capture, "movzx eax, al", "parity SETcc normalization") ||
      !contains(&capture, "ret", "parity SETcc following instruction") ||
      strstr(capture.bytes, "db 0x0F") != (char *)0) {
    ctool_job_close(job);
    return 1;
  }

  (void)memset(&capture, 0, sizeof(capture));
  source.path.text = ctool_string("/immediate-imul32.bin");
  source.contents = ctool_bytes(
      immediate_imul32, (ctool_u32)sizeof(immediate_imul32));
  request = raw_request(CTOOL_X86_MODE_32, 0x00402000u);
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
  if (!check_status(status, CTOOL_OK,
                    "immediate IMUL raw32 inspection") ||
      !contains(&capture, "imul eax, ecx, 0x228",
                "immediate IMUL full-width rendering") ||
      !contains(
          &capture,
          "imul esi, dword [ebx+ecx*4+0x12345678], 0xFFFFFFF9",
          "immediate IMUL sign-extended memory rendering") ||
      !contains(&capture, "imul ax, cx, 0x1234",
                "immediate IMUL 16-bit override rendering") ||
      capture.size != repeat.size ||
      memcmp(capture.bytes, repeat.bytes, (size_t)capture.size) != 0) {
    ctool_job_close(job);
    return 1;
  }

  (void)memset(&capture, 0, sizeof(capture));
  source.path.text = ctool_string("/immediate-imul16.bin");
  source.contents = ctool_bytes(
      immediate_imul16, (ctool_u32)sizeof(immediate_imul16));
  request = raw_request(CTOOL_X86_MODE_16, 0x00007e00u);
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (status == CTOOL_OK) {
    status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&capture));
  }
  if (!check_status(status, CTOOL_OK,
                    "immediate IMUL raw16 inspection") ||
      !contains(&capture, "imul ax, cx, 0x1234",
                "immediate IMUL 16-bit default rendering") ||
      !contains(&capture,
                "imul eax, dword [bx+si+0x7F], 0x12345678",
                "immediate IMUL 32-bit override rendering")) {
    ctool_job_close(job);
    return 1;
  }

  (void)memset(&capture, 0, sizeof(capture));
  source.path.text = ctool_string("/immediate-imul-recovery.bin");
  source.contents =
      ctool_bytes(immediate_imul_recovery,
                  (ctool_u32)sizeof(immediate_imul_recovery));
  request = raw_request(CTOOL_X86_MODE_32, 0x00403000u);
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (status == CTOOL_OK) {
    status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&capture));
  }
  if (!check_status(status, CTOOL_OK,
                    "immediate IMUL recovery inspection") ||
      !contains(&capture, "db 0xF0",
                "immediate IMUL illegal prefix rendering") ||
      !contains(&capture, "imul eax, ecx, 0x2",
                "immediate IMUL recovery rendering") ||
      !contains(&capture, "db 0x69, 0xC1, 0x34",
                "immediate IMUL truncated tail rendering")) {
    ctool_job_close(job);
    return 1;
  }

  (void)memset(&capture, 0, sizeof(capture));
  source.path.text = ctool_string("/padding-nops32.bin");
  source.contents =
      ctool_bytes(padding_nops32, (ctool_u32)sizeof(padding_nops32));
  request = raw_request(CTOOL_X86_MODE_32, 0x00404000u);
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (status == CTOOL_OK) {
    status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&capture));
  }
  if (!check_status(status, CTOOL_OK, "padding NOP raw inspection") ||
      !contains(&capture, "00404000", "operand-size NOP address") ||
      !contains(&capture, "00404002", "memory NOP address") ||
      !contains(&capture, "nop dword [eax]",
                "memory NOP rendering") ||
      !contains(&capture, "00404005", "compiler NOP address") ||
      !contains(&capture, "nop word [cs:eax+eax+0x0]",
                "compiler NOP rendering") ||
      !contains(&capture, "0040400F", "padding NOP following return") ||
      strstr(capture.bytes, "db 0x66") != (char *)0) {
    ctool_job_close(job);
    return 1;
  }

  (void)memset(&capture, 0, sizeof(capture));
  source.path.text = ctool_string("/padding-nop-recovery.bin");
  source.contents = ctool_bytes(
      padding_nop_recovery, (ctool_u32)sizeof(padding_nop_recovery));
  request = raw_request(CTOOL_X86_MODE_32, 0x00405000u);
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (status == CTOOL_OK) {
    status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&capture));
  }
  if (!check_status(status, CTOOL_OK,
                    "padding NOP recovery inspection") ||
      !contains(&capture, "db 0xF0",
                "padding NOP illegal prefix rendering") ||
      !contains(&capture, "nop dword [eax]",
                "padding NOP recovery rendering") ||
      !contains(&capture, "db 0x0F, 0x1F",
                "padding NOP truncated tail rendering")) {
    ctool_job_close(job);
    return 1;
  }

  (void)memset(&capture, 0, sizeof(capture));
  source.path.text = ctool_string("/clang-padding-nops.bin");
  source.contents =
      ctool_bytes(clang_padding_nops,
                  (ctool_u32)sizeof(clang_padding_nops));
  request = raw_request(CTOOL_X86_MODE_32, 0x00406000u);
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (status == CTOOL_OK) {
    status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&capture));
  }
  if (!check_status(status, CTOOL_OK,
                    "Clang padding raw inspection") ||
      !contains(&capture, "00406000", "two-prefix padding address") ||
      !contains(&capture, "0040600B", "three-prefix padding address") ||
      !contains(&capture, "00406017", "four-prefix padding address") ||
      !contains(&capture, "00406024", "five-prefix padding address") ||
      !contains(&capture, "00406032", "six-prefix padding address") ||
      !contains(&capture, "00406041", "Clang padding following return") ||
      count_occurrences(&capture, "nop word [cs:eax+eax+0x0]") != 5u ||
      strstr(capture.bytes, "db 0x66") != (char *)0) {
    ctool_job_close(job);
    return 1;
  }

  (void)memset(&capture, 0, sizeof(capture));
  source.path.text = ctool_string("/clang-padding-near-miss.bin");
  source.contents =
      ctool_bytes(clang_padding_near_miss,
                  (ctool_u32)sizeof(clang_padding_near_miss));
  request = raw_request(CTOOL_X86_MODE_32, 0x00406100u);
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (status == CTOOL_OK) {
    status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&capture));
  }
  if (!check_status(status, CTOOL_OK,
                    "Clang padding near-miss inspection") ||
      !contains(&capture, "00406100", "near-miss invalid address") ||
      !contains(&capture, "db 0x66", "near-miss invalid byte") ||
      !contains(&capture, "00406101", "near-miss recovery address") ||
      !contains(&capture, "00406103", "near-miss following return")) {
    ctool_job_close(job);
    return 1;
  }

  mixed_ranges[0].offset = 0u;
  mixed_ranges[0].kind = CTOOL_DIS_RAW_RANGE_CODE16;
  mixed_ranges[1].offset = 3u;
  mixed_ranges[1].kind = CTOOL_DIS_RAW_RANGE_DATA;
  mixed_ranges[2].offset = 7u;
  mixed_ranges[2].kind = CTOOL_DIS_RAW_RANGE_CODE32;
  mixed_ranges[3].offset = 12u;
  mixed_ranges[3].kind = CTOOL_DIS_RAW_RANGE_CODE16;
  labels[0].address = 0x00007c05u;
  labels[0].name = ctool_string("table_value");
  labels[1].address = 0x00007c07u;
  labels[1].name = ctool_string("protected_mode");
  (void)memset(&capture, 0, sizeof(capture));
  source.path.text = ctool_string("/mixed-mode.bin");
  source.contents =
      ctool_bytes(mixed_mode, (ctool_u32)sizeof(mixed_mode));
  request = raw_request(CTOOL_DIS_RAW_RANGE_MAP, 0x00007c00u);
  request.raw_ranges = mixed_ranges;
  request.raw_range_count = 4u;
  request.labels = labels;
  request.label_count = 2u;
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
      report.mode != CTOOL_DIS_RAW_RANGE_MAP ||
      report.raw_ranges != mixed_ranges || report.raw_range_count != 4u ||
      !contains(&capture, "00007C00", "mixed-mode 16-bit address") ||
      !contains(&capture, "mov ax, 0x1234", "mixed-mode 16-bit operand") ||
      !contains(&capture, "00007C03", "mixed-mode data address") ||
      !contains(&capture, "db 0x00, 0x00", "mixed-mode first data row") ||
      !contains(&capture, "00007C05 <table_value>:",
                "mixed-mode data label") ||
      !contains(&capture, "db 0x90, 0xC3", "mixed-mode second data row") ||
      strstr(capture.bytes, "add byte") != (char *)0 ||
      !contains(&capture, "00007C07 <protected_mode>:",
                "mixed-mode boundary label") ||
      !contains(&capture, "00007C07", "mixed-mode 32-bit address") ||
      !contains(&capture, "mov eax, 0x12345678",
                "mixed-mode 32-bit operand") ||
      !contains(&capture, "00007C0C", "mixed-mode return address") ||
      !contains(&capture, "mov ax, 0xABCD",
                "mixed-mode return to 16-bit") ||
      capture.size != repeat.size ||
      memcmp(capture.bytes, repeat.bytes, (size_t)capture.size) != 0) {
    ctool_job_close(job);
    return 1;
  }

  request = raw_request(CTOOL_DIS_RAW_RANGE_MAP, 0x00007c00u);
  (void)memset(&report, 0xa5, sizeof(report));
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "zero-range raw range map") ||
      !is_zeroed(&report, sizeof(report)) ||
      ctool_job_diagnostic_count(job) != 1u ||
      !check_diagnostic(job, 0u, CTOOL_DIS_DIAG_INVALID_REQUEST,
                        "raw range map requires at least one range",
                        "zero-range map diagnostic")) {
    ctool_job_close(job);
    return 1;
  }

  request.raw_range_count = 1u;
  (void)memset(&report, 0xa5, sizeof(report));
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "missing raw range-map storage") ||
      !is_zeroed(&report, sizeof(report)) ||
      ctool_job_diagnostic_count(job) != 2u ||
      !check_diagnostic(job, 1u, CTOOL_DIS_DIAG_INVALID_REQUEST,
                        "raw range map storage is missing",
                        "missing range-map storage diagnostic")) {
    ctool_job_close(job);
    return 1;
  }

  invalid_ranges[0].offset = 0u;
  invalid_ranges[0].kind = CTOOL_DIS_RAW_RANGE_CODE16;
  request.raw_ranges = invalid_ranges;
  source.contents = ctool_bytes((const void *)0, 0u);
  (void)memset(&report, 0xa5, sizeof(report));
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "empty mapped raw input") ||
      !is_zeroed(&report, sizeof(report)) ||
      ctool_job_diagnostic_count(job) != 3u ||
      !check_diagnostic(job, 2u, CTOOL_DIS_DIAG_INVALID_REQUEST,
                        "raw range map requires nonempty input",
                        "empty range-map input diagnostic")) {
    ctool_job_close(job);
    return 1;
  }

  source.contents =
      ctool_bytes(mixed_mode, (ctool_u32)sizeof(mixed_mode));
  invalid_ranges[0].offset = 1u;
  (void)memset(&report, 0xa5, sizeof(report));
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "nonzero raw range-map start") ||
      !is_zeroed(&report, sizeof(report)) ||
      ctool_job_diagnostic_count(job) != 4u ||
      !check_diagnostic(job, 3u, CTOOL_DIS_DIAG_INVALID_REQUEST,
                        "raw range map must start at offset zero",
                        "range-map start diagnostic")) {
    ctool_job_close(job);
    return 1;
  }

  invalid_ranges[0].offset = 0u;
  invalid_ranges[0].kind = (ctool_dis_raw_range_kind_t)64;
  (void)memset(&report, 0xa5, sizeof(report));
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "invalid raw range kind") ||
      !is_zeroed(&report, sizeof(report)) ||
      ctool_job_diagnostic_count(job) != 5u ||
      !check_diagnostic(job, 4u, CTOOL_DIS_DIAG_INVALID_REQUEST,
                        "raw range kind must be code16, code32, or data",
                        "raw range kind diagnostic")) {
    ctool_job_close(job);
    return 1;
  }

  invalid_ranges[0].kind = CTOOL_DIS_RAW_RANGE_CODE16;
  invalid_ranges[1].offset = (ctool_u32)sizeof(mixed_mode);
  invalid_ranges[1].kind = CTOOL_DIS_RAW_RANGE_CODE32;
  request.raw_range_count = 2u;
  (void)memset(&report, 0xa5, sizeof(report));
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "raw range outside input") ||
      !is_zeroed(&report, sizeof(report)) ||
      ctool_job_diagnostic_count(job) != 6u ||
      !check_diagnostic(job, 5u, CTOOL_DIS_DIAG_INVALID_REQUEST,
                        "raw range start is outside input",
                        "raw range boundary diagnostic")) {
    ctool_job_close(job);
    return 1;
  }

  invalid_ranges[1].offset = 3u;
  invalid_ranges[2].offset = 3u;
  invalid_ranges[2].kind = CTOOL_DIS_RAW_RANGE_CODE16;
  request.raw_range_count = 3u;
  (void)memset(&report, 0xa5, sizeof(report));
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "duplicate raw range offset") ||
      !is_zeroed(&report, sizeof(report)) ||
      ctool_job_diagnostic_count(job) != 7u ||
      !check_diagnostic(job, 6u, CTOOL_DIS_DIAG_INVALID_REQUEST,
                        "raw range starts must increase without overlap",
                        "duplicate or overlapping raw range diagnostic")) {
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
                        "raw range starts must increase without overlap",
                        "decreasing or overlapping raw range diagnostic")) {
    ctool_job_close(job);
    return 1;
  }

  source.contents = ctool_bytes(mixed_mode, 2u);
  (void)memset(&report, 0xa5, sizeof(report));
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "raw range-map range limit") ||
      !is_zeroed(&report, sizeof(report)) ||
      ctool_job_diagnostic_count(job) != 9u ||
      !check_diagnostic(job, 8u, CTOOL_DIS_DIAG_INVALID_REQUEST,
                        "raw range map has too many ranges",
                        "raw range-map range limit diagnostic")) {
    ctool_job_close(job);
    return 1;
  }

  source.contents =
      ctool_bytes(mixed_mode, (ctool_u32)sizeof(mixed_mode));
  request = raw_request(CTOOL_DIS_RAW_RANGE_MAP, 0x00007c00u);
  request.raw_ranges = mixed_ranges;
  request.raw_range_count = 4u;
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
  {
    ctool_dis_report_t invalid_report = report;
    for (index = 0u; index < 4u; index++) {
      changed_ranges[index] = mixed_ranges[index];
    }
    changed_ranges[1].kind = (ctool_dis_raw_range_kind_t)64;
    invalid_report.raw_ranges = changed_ranges;
    (void)memset(&capture, 0, sizeof(capture));
    status = ctool_dis_render(job, &invalid_report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&capture));
    if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                      "mutated raw range-kind report") ||
        capture.size != 0u || ctool_job_diagnostic_count(job) != 9u) {
      ctool_job_close(job);
      return 1;
    }
  }

  request = raw_request(CTOOL_X86_MODE_16, 0x00007c00u);
  request.raw_ranges = mixed_ranges;
  request.raw_range_count = 4u;
  (void)memset(&report, 0xa5, sizeof(report));
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "fixed raw mode with range data") ||
      !is_zeroed(&report, sizeof(report)) ||
      ctool_job_diagnostic_count(job) != 10u ||
      !check_diagnostic(job, 9u, CTOOL_DIS_DIAG_INVALID_REQUEST,
                        "raw ranges require mapped mode",
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
  labels[0].address = 0x00400002u;
  labels[0].name = ctool_string("target");
  request = raw_request(CTOOL_X86_MODE_32, 0x00400000u);
  request.labels = labels;
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

  summary_ranges[0].offset = 0u;
  summary_ranges[0].kind = CTOOL_DIS_RAW_RANGE_CODE32;
  summary_ranges[1].offset = 8u;
  summary_ranges[1].kind = CTOOL_DIS_RAW_RANGE_DATA;
  (void)memset(&capture, 0, sizeof(capture));
  source.path.text = ctool_string("/decode-summary.bin");
  source.contents =
      ctool_bytes(decode_summary, (ctool_u32)sizeof(decode_summary));
  request = raw_request(CTOOL_DIS_RAW_RANGE_MAP, 0x00407000u);
  request.raw_ranges = summary_ranges;
  request.raw_range_count = 2u;
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (status == CTOOL_OK) {
    status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&capture));
  }
  if (!check_status(status, CTOOL_OK, "typed decode summary") ||
      report.decode_summary.known_count != 3u ||
      report.decode_summary.unknown_count != 1u ||
      report.decode_summary.invalid_count != 1u ||
      report.decode_summary.truncated_count != 1u ||
      report.decode_summary.executable_relocation_count != 0u ||
      report.decode_summary.unmatched_executable_relocation_count != 0u ||
      !contains(&capture, "00407001:  0F  db 0x0F",
                "unknown decode boundary") ||
      !contains(&capture, "00407002:  FF C0  inc eax",
                "unknown decode recovery") ||
      !contains(&capture, "00407004:  66  db 0x66",
                "invalid decode boundary") ||
      !contains(&capture, "00407005:  66 90  nop",
                "invalid decode recovery") ||
      !contains(&capture, "00407007:  0F  db 0x0F",
                "truncated decode boundary") ||
      !contains(&capture, "00407008:  0F FF 66 66 0F",
                "declared data rendering")) {
    (void)fprintf(stderr,
                  "decode summary differs: %llu/%llu/%llu/%llu\n",
                  (unsigned long long)report.decode_summary.known_count,
                  (unsigned long long)report.decode_summary.unknown_count,
                  (unsigned long long)report.decode_summary.invalid_count,
                  (unsigned long long)report.decode_summary.truncated_count);
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
      report.elf32.relocation_count != 4u ||
      report.decode_summary.known_count != 4u ||
      report.decode_summary.unknown_count != 0u ||
      report.decode_summary.invalid_count != 0u ||
      report.decode_summary.truncated_count != 0u ||
      report.decode_summary.executable_relocation_count != 3u ||
      report.decode_summary.unmatched_executable_relocation_count != 0u) {
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
        header_report.relocation_site_order_count != 0u ||
        header_report.decode_summary.known_count != 0u ||
        header_report.decode_summary.unknown_count != 0u ||
        header_report.decode_summary.invalid_count != 0u ||
        header_report.decode_summary.truncated_count != 0u ||
        header_report.decode_summary.executable_relocation_count != 0u ||
        header_report.decode_summary.unmatched_executable_relocation_count !=
            0u) {
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
    put_le32(copy, info_offset,
             (info & 0xffffff00u) | CTOOL_ELF32_R_386_PC32);
    source.contents = ctool_bytes(copy, bytes.size);
    request.views = CTOOL_DIS_VIEW_DISASSEMBLY;
    if (!check_unowned_absolute_memory_relocation(
            job, &source, &request, 3u,
            "incompatible relocation inspection")) {
      free(copy);
      ctool_buffer_close(object_bytes);
      ctool_job_close(job);
      return 1;
    }
    put_le32(copy, info_offset, (info & 0xffffff00u) | 0x7fu);
    if (!check_unowned_absolute_memory_relocation(
            job, &source, &request, 3u, "unknown relocation inspection")) {
      free(copy);
      ctool_buffer_close(object_bytes);
      ctool_job_close(job);
      return 1;
    }
    free(copy);
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

typedef struct {
  ctool_u32 address;
  ctool_u32 flags;
  const ctool_u8 *contents;
  ctool_u32 file_size;
  ctool_u32 memory_size;
} exec_fixture_segment_t;

static ctool_u32 build_exec_fixture(
    ctool_u8 *image, ctool_u32 capacity,
    const exec_fixture_segment_t *segments, ctool_u32 segment_count) {
  ctool_u32 header_size = 52u;
  ctool_u32 program_header_size = 32u;
  ctool_u32 payload_offset;
  ctool_u32 image_size;
  ctool_u32 index;
  if (image == (ctool_u8 *)0 || segments == (const exec_fixture_segment_t *)0 ||
      segment_count == 0u || capacity < header_size ||
      segment_count >
          (capacity - header_size) / program_header_size) {
    return 0u;
  }
  payload_offset = header_size + segment_count * program_header_size;
  image_size = payload_offset;
  for (index = 0u; index < segment_count; index++) {
    if (segments[index].file_size > segments[index].memory_size ||
        segments[index].file_size > capacity - image_size) {
      return 0u;
    }
    image_size += segments[index].file_size;
  }
  (void)memset(image, 0, image_size);
  image[0] = 0x7fu;
  image[1] = (ctool_u8)'E';
  image[2] = (ctool_u8)'L';
  image[3] = (ctool_u8)'F';
  image[4] = 1u;
  image[5] = 1u;
  image[6] = 1u;
  put_le16(image, 16u, (ctool_u16)CTOOL_ELF32_ET_EXEC);
  put_le16(image, 18u, 3u);
  put_le32(image, 20u, 1u);
  put_le32(image, 24u, segments[0].address);
  put_le32(image, 28u, header_size);
  put_le16(image, 40u, (ctool_u16)header_size);
  put_le16(image, 42u, (ctool_u16)program_header_size);
  put_le16(image, 44u, (ctool_u16)segment_count);
  for (index = 0u; index < segment_count; index++) {
    ctool_u32 header = header_size + index * program_header_size;
    put_le32(image, header, CTOOL_ELF32_PT_LOAD);
    put_le32(image, header + 4u, payload_offset);
    put_le32(image, header + 8u, segments[index].address);
    put_le32(image, header + 12u, segments[index].address);
    put_le32(image, header + 16u, segments[index].file_size);
    put_le32(image, header + 20u, segments[index].memory_size);
    put_le32(image, header + 24u, segments[index].flags);
    put_le32(image, header + 28u, 1u);
    if (segments[index].file_size != 0u) {
      (void)memcpy(image + payload_offset, segments[index].contents,
                   segments[index].file_size);
    }
    payload_offset += segments[index].file_size;
  }
  return image_size;
}

static ctool_u32 build_exec_anchor_fixture(ctool_u8 *image,
                                            ctool_u32 capacity) {
  static const ctool_u8 text[] = {
      0xb8u, 0x78u, 0x56u, 0x34u, 0x12u, 0xc3u};
  static const char strtab[] = "\0entry\0alias\0ignored\0";
  static const char shstrtab[] =
      "\0.text\0.symtab\0.strtab\0.shstrtab\0";
  const ctool_u32 section_headers = 212u;
  const ctool_u32 image_size = section_headers + 5u * 40u;
  ctool_u32 header;
  if (image == (ctool_u8 *)0 || capacity < image_size) {
    return 0u;
  }
  (void)memset(image, 0, image_size);
  image[0] = 0x7fu;
  image[1] = (ctool_u8)'E';
  image[2] = (ctool_u8)'L';
  image[3] = (ctool_u8)'F';
  image[4] = 1u;
  image[5] = 1u;
  image[6] = 1u;
  put_le16(image, 16u, (ctool_u16)CTOOL_ELF32_ET_EXEC);
  put_le16(image, 18u, 3u);
  put_le32(image, 20u, 1u);
  put_le32(image, 24u, 0x00100000u);
  put_le32(image, 28u, 52u);
  put_le32(image, 32u, section_headers);
  put_le16(image, 40u, 52u);
  put_le16(image, 42u, 32u);
  put_le16(image, 44u, 1u);
  put_le16(image, 46u, 40u);
  put_le16(image, 48u, 5u);
  put_le16(image, 50u, 4u);

  put_le32(image, 52u, CTOOL_ELF32_PT_LOAD);
  put_le32(image, 56u, 84u);
  put_le32(image, 60u, 0x00100000u);
  put_le32(image, 64u, 0x00100000u);
  put_le32(image, 68u, (ctool_u32)sizeof(text));
  put_le32(image, 72u, 8u);
  put_le32(image, 76u, CTOOL_ELF32_PF_R | CTOOL_ELF32_PF_X);
  put_le32(image, 80u, 4u);
  (void)memcpy(image + 84u, text, sizeof(text));
  (void)memcpy(image + 90u, strtab, sizeof(strtab));

  put_le32(image, 128u, 1u);
  put_le32(image, 132u, 0x00100000u);
  put_le32(image, 136u, (ctool_u32)sizeof(text));
  image[140u] = 0x12u;
  put_le16(image, 142u, 1u);
  put_le32(image, 144u, 7u);
  put_le32(image, 148u, 0x00100000u);
  image[156u] = 0x12u;
  put_le16(image, 158u, 1u);
  put_le32(image, 160u, 13u);
  put_le32(image, 164u, 0x00100001u);
  put_le32(image, 168u, 1u);
  image[172u] = 0x11u;
  put_le16(image, 174u, 1u);
  (void)memcpy(image + 176u, shstrtab, sizeof(shstrtab));

  header = section_headers + 40u;
  put_le32(image, header, 1u);
  put_le32(image, header + 4u, CTOOL_ELF32_SHT_PROGBITS);
  put_le32(image, header + 8u,
           CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR);
  put_le32(image, header + 12u, 0x00100000u);
  put_le32(image, header + 16u, 84u);
  put_le32(image, header + 20u, (ctool_u32)sizeof(text));
  put_le32(image, header + 32u, 4u);

  header = section_headers + 80u;
  put_le32(image, header, 7u);
  put_le32(image, header + 4u, 2u);
  put_le32(image, header + 16u, 112u);
  put_le32(image, header + 20u, 64u);
  put_le32(image, header + 24u, 3u);
  put_le32(image, header + 28u, 1u);
  put_le32(image, header + 32u, 4u);
  put_le32(image, header + 36u, 16u);

  header = section_headers + 120u;
  put_le32(image, header, 15u);
  put_le32(image, header + 4u, 3u);
  put_le32(image, header + 16u, 90u);
  put_le32(image, header + 20u, (ctool_u32)sizeof(strtab));
  put_le32(image, header + 32u, 1u);

  header = section_headers + 160u;
  put_le32(image, header, 23u);
  put_le32(image, header + 4u, 3u);
  put_le32(image, header + 16u, 176u);
  put_le32(image, header + 20u, (ctool_u32)sizeof(shstrtab));
  put_le32(image, header + 32u, 1u);
  return image_size;
}

static int run_anchors(void) {
  ctool_u8 image[512];
  ctool_u32 image_size =
      build_exec_anchor_fixture(image, (ctool_u32)sizeof(image));
  ctool_host_adapter_t adapter;
  ctool_job_t *job;
  ctool_source_t source;
  ctool_dis_request_t request;
  ctool_dis_report_t report;
  ctool_dis_report_t indexed_report;
  const ctool_x86_decoder_t *decoder;
  ctool_status_t status;
  if (image_size == 0u || !open_job(&adapter, &job)) {
    return 1;
  }
  source.path.text = ctool_string("/code-anchors.elf");
  source.contents = ctool_bytes(image, image_size);
  (void)memset(&request, 0, sizeof(request));
  request.input = CTOOL_DIS_INPUT_ELF32;
  request.views = CTOOL_DIS_VIEW_DISASSEMBLY;
  request.policies = CTOOL_DIS_POLICY_CODE_ANCHORS;
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_OK, "valid executable code anchors") ||
      report.policies != CTOOL_DIS_POLICY_CODE_ANCHORS ||
      report.decode_summary.code_anchor_count != 3u ||
      report.decode_summary.code_anchor_outside_executable_count != 0u ||
      report.decode_summary.code_anchor_mid_instruction_count != 0u ||
      report.decode_summary.known_count != 2u ||
      ctool_job_diagnostic_count(job) != 0u) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_x86_decoder_prepare(job, &decoder);
  if (status == CTOOL_OK) {
    status = ctool_dis_inspect_indexed(job, decoder, &source, &request,
                                       &indexed_report);
  }
  if (!check_status(status, CTOOL_OK, "indexed executable code anchors") ||
      !summaries_equal(&report.decode_summary,
                       &indexed_report.decode_summary)) {
    ctool_job_close(job);
    return 1;
  }
  request.policies = CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS |
                     CTOOL_DIS_POLICY_CODE_ANCHORS;
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_OK, "combined executable code policies") ||
      report.decode_summary.code_anchor_count != 3u ||
      report.decode_summary.direct_relative_target_count != 0u) {
    ctool_job_close(job);
    return 1;
  }
  request.policies = CTOOL_DIS_POLICY_CODE_ANCHORS;

  put_le16(image, 158u, 0u);
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_OK, "undefined function is not an anchor") ||
      report.decode_summary.code_anchor_count != 2u) {
    ctool_job_close(job);
    return 1;
  }
  put_le16(image, 158u, 0xfff1u);
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_OK, "absolute function is not an anchor") ||
      report.decode_summary.code_anchor_count != 2u) {
    ctool_job_close(job);
    return 1;
  }
  put_le16(image, 158u, 1u);

  put_le32(image, 24u, 0x00100001u);
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_OK, "mid-instruction entry anchor") ||
      report.decode_summary.code_anchor_count != 3u ||
      report.decode_summary.code_anchor_outside_executable_count != 0u ||
      report.decode_summary.code_anchor_mid_instruction_count != 1u) {
    ctool_job_close(job);
    return 1;
  }

  put_le32(image, 24u, 0x00100000u);
  put_le32(image, 148u, 0x00100001u);
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_OK, "mid-instruction function anchor") ||
      report.decode_summary.code_anchor_count != 3u ||
      report.decode_summary.code_anchor_outside_executable_count != 0u ||
      report.decode_summary.code_anchor_mid_instruction_count != 1u) {
    ctool_job_close(job);
    return 1;
  }

  put_le32(image, 148u, 0x00100000u);
  put_le32(image, 24u, 0x00200000u);
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_OK, "outside executable entry anchor") ||
      report.decode_summary.code_anchor_count != 3u ||
      report.decode_summary.code_anchor_outside_executable_count != 1u ||
      report.decode_summary.code_anchor_mid_instruction_count != 0u) {
    ctool_job_close(job);
    return 1;
  }

  put_le32(image, 24u, 0x00100006u);
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_OK, "memory-only entry anchor") ||
      report.decode_summary.code_anchor_count != 3u ||
      report.decode_summary.code_anchor_outside_executable_count != 1u ||
      report.decode_summary.code_anchor_mid_instruction_count != 0u) {
    ctool_job_close(job);
    return 1;
  }

  put_le32(image, 24u, 0x00100000u);
  request.views = CTOOL_DIS_VIEW_HEADER;
  (void)memset(&report, 0xa5, sizeof(report));
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "code anchor view requirement") ||
      !is_zeroed(&report, sizeof(report)) ||
      ctool_job_diagnostic_count(job) != 1u ||
      !check_diagnostic(job, 0u, CTOOL_DIS_DIAG_INVALID_REQUEST,
                        "ELF code anchor checks require the disassembly view",
                        "code anchor view diagnostic")) {
    ctool_job_close(job);
    return 1;
  }

  request.views = CTOOL_DIS_VIEW_DISASSEMBLY;
  put_le32(image, 52u, CTOOL_ELF32_PT_INTERP);
  (void)memset(&report, 0xa5, sizeof(report));
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "dynamic code anchor policy") ||
      !is_zeroed(&report, sizeof(report)) ||
      ctool_job_diagnostic_count(job) != 2u ||
      !check_diagnostic(
          job, 1u, CTOOL_DIS_DIAG_INVALID_REQUEST,
          "executable code anchor checks require a static image without "
          "PT_DYNAMIC or PT_INTERP",
          "dynamic code anchor diagnostic")) {
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  {
    static const ctool_u8 text[] = {0xc3u};
    ctool_buffer_t *valid_object = (ctool_buffer_t *)0;
    ctool_buffer_t *middle_object = (ctool_buffer_t *)0;
    ctool_buffer_t *outside_object = (ctool_buffer_t *)0;
    ctool_buffer_t *absolute_object = (ctool_buffer_t *)0;
    ctool_dis_request_t invalid_request =
        raw_request(CTOOL_X86_MODE_32, 0u);
    if (!open_job(&adapter, &job)) {
      return 1;
    }
    source.path.text = ctool_string("/raw-code-anchor.bin");
    source.contents = ctool_bytes(text, (ctool_u32)sizeof(text));
    invalid_request.policies = CTOOL_DIS_POLICY_CODE_ANCHORS;
    (void)memset(&report, 0xa5, sizeof(report));
    status = ctool_dis_inspect(job, &source, &invalid_request, &report);
    if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                      "raw code anchor policy") ||
        !is_zeroed(&report, sizeof(report)) ||
        ctool_job_diagnostic_count(job) != 1u ||
        !check_diagnostic(
            job, 0u, CTOOL_DIS_DIAG_INVALID_REQUEST,
            "code anchor checks require ELF32 ET_REL or ET_EXEC input",
            "raw code anchor diagnostic")) {
      ctool_job_close(job);
      return 1;
    }
    if (!build_code_anchor_object(
            job, CTOOL_ELF32_SYMBOL_DEFINED, 0u, 0u, &valid_object) ||
        !build_code_anchor_object(
            job, CTOOL_ELF32_SYMBOL_DEFINED, 0u, 1u, &middle_object) ||
        !build_code_anchor_object(
            job, CTOOL_ELF32_SYMBOL_DEFINED, 1u, 0u, &outside_object) ||
        !build_code_anchor_object(
            job, CTOOL_ELF32_SYMBOL_ABSOLUTE, 0u, 0u, &absolute_object)) {
      ctool_job_close(job);
      return 1;
    }
    source.path.text = ctool_string("/relocatable-code-anchor.o");
    source.contents = ctool_buffer_view(valid_object);
    (void)memset(&invalid_request, 0, sizeof(invalid_request));
    invalid_request.input = CTOOL_DIS_INPUT_ELF32;
    invalid_request.views = CTOOL_DIS_VIEW_DISASSEMBLY;
    invalid_request.policies = CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS |
                               CTOOL_DIS_POLICY_CODE_ANCHORS;
    status = ctool_dis_inspect(job, &source, &invalid_request, &report);
    if (!check_status(status, CTOOL_OK, "relocatable code anchors") ||
        report.decode_summary.code_anchor_count != 3u ||
        report.decode_summary.code_anchor_outside_executable_count != 0u ||
        report.decode_summary.code_anchor_mid_instruction_count != 0u ||
        report.decode_summary.direct_relative_target_count != 0u ||
        report.decode_summary.executable_relocation_count != 1u ||
        report.decode_summary.unmatched_executable_relocation_count != 0u ||
        ctool_job_diagnostic_count(job) != 1u) {
      ctool_buffer_close(absolute_object);
      ctool_buffer_close(outside_object);
      ctool_buffer_close(middle_object);
      ctool_buffer_close(valid_object);
      ctool_job_close(job);
      return 1;
    }
    status = ctool_x86_decoder_prepare(job, &decoder);
    if (status == CTOOL_OK) {
      status = ctool_dis_inspect_indexed(
          job, decoder, &source, &invalid_request, &indexed_report);
    }
    if (!check_status(status, CTOOL_OK,
                      "indexed relocatable code anchors") ||
        !summaries_equal(&report.decode_summary,
                         &indexed_report.decode_summary)) {
      ctool_buffer_close(absolute_object);
      ctool_buffer_close(outside_object);
      ctool_buffer_close(middle_object);
      ctool_buffer_close(valid_object);
      ctool_job_close(job);
      return 1;
    }
    invalid_request.policies = CTOOL_DIS_POLICY_CODE_ANCHORS;
    source.path.text = ctool_string("/relocated-field-anchor.o");
    source.contents = ctool_buffer_view(middle_object);
    status = ctool_dis_inspect(job, &source, &invalid_request, &report);
    if (!check_status(status, CTOOL_OK, "relocated field code anchor") ||
        report.decode_summary.code_anchor_count != 3u ||
        report.decode_summary.code_anchor_outside_executable_count != 0u ||
        report.decode_summary.code_anchor_mid_instruction_count != 1u) {
      ctool_buffer_close(absolute_object);
      ctool_buffer_close(outside_object);
      ctool_buffer_close(middle_object);
      ctool_buffer_close(valid_object);
      ctool_job_close(job);
      return 1;
    }
    source.path.text = ctool_string("/data-function-anchor.o");
    source.contents = ctool_buffer_view(outside_object);
    status = ctool_dis_inspect(job, &source, &invalid_request, &report);
    if (!check_status(status, CTOOL_OK, "data function code anchor") ||
        report.decode_summary.code_anchor_count != 3u ||
        report.decode_summary.code_anchor_outside_executable_count != 1u ||
        report.decode_summary.code_anchor_mid_instruction_count != 0u) {
      ctool_buffer_close(absolute_object);
      ctool_buffer_close(outside_object);
      ctool_buffer_close(middle_object);
      ctool_buffer_close(valid_object);
      ctool_job_close(job);
      return 1;
    }
    source.path.text = ctool_string("/absolute-function-anchor.o");
    source.contents = ctool_buffer_view(absolute_object);
    status = ctool_dis_inspect(job, &source, &invalid_request, &report);
    if (!check_status(status, CTOOL_OK, "absolute function code anchor") ||
        report.decode_summary.code_anchor_count != 3u ||
        report.decode_summary.code_anchor_outside_executable_count != 1u ||
        report.decode_summary.code_anchor_mid_instruction_count != 0u) {
      ctool_buffer_close(absolute_object);
      ctool_buffer_close(outside_object);
      ctool_buffer_close(middle_object);
      ctool_buffer_close(valid_object);
      ctool_job_close(job);
      return 1;
    }
    ctool_buffer_close(absolute_object);
    ctool_buffer_close(outside_object);
    ctool_buffer_close(middle_object);
    ctool_buffer_close(valid_object);
    ctool_job_close(job);
  }
  {
    ctool_u8 large_code[8192];
    ctool_u8 large_image[8276];
    exec_fixture_segment_t large_segment;
    ctool_u32 large_image_size;
    ctool_host_adapter_t limited_adapter;
    ctool_job_config_t config;
    ctool_limits_t limits = ctool_default_limits();
    ctool_dis_request_t limited_request;
    ctool_dis_report_t limited_report;
    ctool_status_t limited_status =
        ctool_host_adapter_init(&limited_adapter, ".");
    (void)memset(large_code, 0x90, sizeof(large_code));
    large_segment.address = 0x00400000u;
    large_segment.flags = CTOOL_ELF32_PF_R | CTOOL_ELF32_PF_X;
    large_segment.contents = large_code;
    large_segment.file_size = (ctool_u32)sizeof(large_code);
    large_segment.memory_size = (ctool_u32)sizeof(large_code);
    large_image_size = build_exec_fixture(
        large_image, (ctool_u32)sizeof(large_image), &large_segment, 1u);
    job = (ctool_job_t *)0;
    limits.arena_block_bytes = 512u;
    limits.arena_bytes = 1023u;
    config = ctool_host_job_config(&limited_adapter, limits);
    if (limited_status == CTOOL_OK) {
      limited_status = ctool_job_open(&config, &job);
    }
    if (!check_status(limited_status, CTOOL_OK,
                      "limited executable code anchor job") ||
        large_image_size == 0u) {
      return 1;
    }
    source.path.text = ctool_string("/code-anchor-limit.elf");
    source.contents = ctool_bytes(large_image, large_image_size);
    (void)memset(&limited_request, 0, sizeof(limited_request));
    limited_request.input = CTOOL_DIS_INPUT_ELF32;
    limited_request.views = CTOOL_DIS_VIEW_DISASSEMBLY;
    limited_request.policies = CTOOL_DIS_POLICY_CODE_ANCHORS;
    (void)memset(&limited_report, 0xa5, sizeof(limited_report));
    limited_status = ctool_dis_inspect(
        job, &source, &limited_request, &limited_report);
    if (!check_status(limited_status, CTOOL_ERR_LIMIT,
                      "executable code anchor instruction map limit") ||
        !is_zeroed(&limited_report, sizeof(limited_report)) ||
        ctool_job_diagnostic_count(job) != 0u) {
      ctool_job_close(job);
      return 1;
    }
    limited_request.policies = 0u;
    limited_status = ctool_dis_inspect(
        job, &source, &limited_request, &limited_report);
    if (!check_status(limited_status, CTOOL_OK,
                      "executable code anchor allocation recovery") ||
        limited_report.decode_summary.known_count != 8192u ||
        limited_report.decode_summary.code_anchor_count != 0u) {
      ctool_job_close(job);
      return 1;
    }
    ctool_job_close(job);
  }
  (void)puts("anchors: ok");
  return 0;
}

static int check_exec_target_case(
    ctool_job_t *job, const char *path,
    const exec_fixture_segment_t *segments, ctool_u32 segment_count,
    ctool_u64 total, ctool_u64 outside_load, ctool_u64 non_executable,
    ctool_u64 mid_instruction) {
  ctool_u8 image[512];
  ctool_u32 image_size = build_exec_fixture(
      image, (ctool_u32)sizeof(image), segments, segment_count);
  ctool_source_t source;
  ctool_dis_request_t request;
  ctool_dis_report_t report;
  ctool_status_t status;
  if (image_size == 0u) {
    (void)fprintf(stderr, "%s: executable fixture could not be built\n", path);
    return 0;
  }
  source.path.text = ctool_string(path);
  source.contents = ctool_bytes(image, image_size);
  (void)memset(&request, 0, sizeof(request));
  request.input = CTOOL_DIS_INPUT_ELF32;
  request.views = CTOOL_DIS_VIEW_DISASSEMBLY;
  request.policies = CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS;
  status = ctool_dis_inspect(job, &source, &request, &report);
  return check_status(status, CTOOL_OK, path) &&
                 report.elf32.file_type == CTOOL_ELF32_ET_EXEC &&
                 target_summary_matches(&report.decode_summary, total,
                                        outside_load, 0u, non_executable, 0u,
                                        mid_instruction, path)
             ? 1
             : 0;
}

static int run_exec(void) {
  static const ctool_u8 valid_first[] = {
      0xebu, 0x01u, 0x90u, 0xc3u, 0xe9u,
      0xf7u, 0x00u, 0x00u, 0x00u};
  static const ctool_u8 valid_second[] = {0xc3u};
  static const exec_fixture_segment_t valid_segments[] = {
      {0x00400000u, CTOOL_ELF32_PF_R | CTOOL_ELF32_PF_X,
       valid_first, (ctool_u32)sizeof(valid_first),
       (ctool_u32)sizeof(valid_first)},
      {0x00400100u, CTOOL_ELF32_PF_R | CTOOL_ELF32_PF_X,
       valid_second, (ctool_u32)sizeof(valid_second),
       (ctool_u32)sizeof(valid_second)}};
  static const ctool_u8 outside_code[] = {
      0xe9u, 0xfbu, 0x02u, 0x00u, 0x00u, 0xc3u};
  static const exec_fixture_segment_t outside_segments[] = {
      {0x00400000u, CTOOL_ELF32_PF_R | CTOOL_ELF32_PF_X,
       outside_code, (ctool_u32)sizeof(outside_code),
       (ctool_u32)sizeof(outside_code)}};
  static const ctool_u8 data_target_code[] = {
      0xe9u, 0xfbu, 0x01u, 0x00u, 0x00u, 0xc3u};
  static const ctool_u8 loaded_data[] = {0u};
  static const exec_fixture_segment_t data_target_segments[] = {
      {0x00400000u, CTOOL_ELF32_PF_R | CTOOL_ELF32_PF_X,
       data_target_code, (ctool_u32)sizeof(data_target_code),
       (ctool_u32)sizeof(data_target_code)},
      {0x00400200u, CTOOL_ELF32_PF_R, loaded_data,
       (ctool_u32)sizeof(loaded_data), (ctool_u32)sizeof(loaded_data)}};
  static const ctool_u8 executable_bss_target_code[] = {
      0xe9u, 0xfbu, 0x00u, 0x00u, 0x00u, 0xc3u};
  static const exec_fixture_segment_t executable_bss_target_segments[] = {
      {0x00400000u, CTOOL_ELF32_PF_R | CTOOL_ELF32_PF_X,
       executable_bss_target_code,
       (ctool_u32)sizeof(executable_bss_target_code), 0x101u}};
  static const ctool_u8 middle_code[] = {0xebu, 0xffu, 0xc3u};
  static const exec_fixture_segment_t middle_segments[] = {
      {0x00400000u, CTOOL_ELF32_PF_R | CTOOL_ELF32_PF_X,
       middle_code, (ctool_u32)sizeof(middle_code),
       (ctool_u32)sizeof(middle_code)}};
  static const ctool_u8 far_indirect_code[] = {
      0xeau, 0x00u, 0x01u, 0x40u, 0x00u, 0x08u, 0x00u,
      0xffu, 0xd0u, 0xc3u};
  static const exec_fixture_segment_t far_indirect_segments[] = {
      {0x00400000u, CTOOL_ELF32_PF_R | CTOOL_ELF32_PF_X,
       far_indirect_code, (ctool_u32)sizeof(far_indirect_code),
       (ctool_u32)sizeof(far_indirect_code)}};
  static const ctool_u8 overlap_first[] = {0x90u, 0xc3u};
  static const ctool_u8 overlap_second[] = {0xc3u};
  static const exec_fixture_segment_t overlap_segments[] = {
      {0x00400000u, CTOOL_ELF32_PF_R | CTOOL_ELF32_PF_X,
       overlap_first, (ctool_u32)sizeof(overlap_first),
       (ctool_u32)sizeof(overlap_first)},
      {0x00400001u, CTOOL_ELF32_PF_R | CTOOL_ELF32_PF_X,
       overlap_second, (ctool_u32)sizeof(overlap_second),
       (ctool_u32)sizeof(overlap_second)}};
  static const ctool_u8 unsupported_program_code[] = {0xc3u};
  static const ctool_u8 unsupported_program_data[] = {
      '/', 'l', 'd', '.', 's', 'o', 0u};
  static const exec_fixture_segment_t unsupported_program_segments[] = {
      {0x00400000u, CTOOL_ELF32_PF_R | CTOOL_ELF32_PF_X,
       unsupported_program_code,
       (ctool_u32)sizeof(unsupported_program_code),
       (ctool_u32)sizeof(unsupported_program_code)},
      {0x00400100u, CTOOL_ELF32_PF_R, unsupported_program_data,
       (ctool_u32)sizeof(unsupported_program_data),
       (ctool_u32)sizeof(unsupported_program_data)}};
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
  request.policies = CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS;
  (void)memset(&report, 0xa5, sizeof(report));
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_OK,
                    "executable local target policy") ||
      report.elf32.file_type != CTOOL_ELF32_ET_EXEC ||
      !target_summary_matches(&report.decode_summary, 0u, 0u, 0u, 0u, 0u,
                              0u, "executable local target policy") ||
      ctool_job_diagnostic_count(job) != 0u) {
    ctool_job_close(job);
    return 1;
  }
  (void)memset(&capture, 0, sizeof(capture));
  status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                            capture_sink(&capture));
  if (!check_status(status, CTOOL_OK,
                    "executable local target report validation") ||
      capture.size == 0u) {
    ctool_job_close(job);
    return 1;
  }
  request.policies = 0u;
  (void)memset(&capture, 0, sizeof(capture));
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (status == CTOOL_OK) {
    status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&capture));
  }
  if (!check_status(status, CTOOL_OK, "executable inspection") ||
      report.elf32.file_type != CTOOL_ELF32_ET_EXEC ||
      report.elf32.program_header_count != 2u ||
      report.decode_summary.known_count != 2u ||
      report.decode_summary.unknown_count != 0u ||
      report.decode_summary.invalid_count != 0u ||
      report.decode_summary.truncated_count != 0u ||
      report.decode_summary.executable_relocation_count != 0u ||
      report.decode_summary.unmatched_executable_relocation_count != 0u ||
      !contains(&capture, "ELF32 EXEC i386", "executable header") ||
      !contains(&capture, "[program headers]", "program headers") ||
      !contains(&capture, "] GNU_STACK off=", "GNU stack header type") ||
      !contains(&capture, "[disassembly LOAD#0]", "load disassembly") ||
      !contains(&capture, "00400000", "load address") ||
      !contains(&capture, "mov eax, 0x12345678", "executable code")) {
    ctool_job_close(job);
    return 1;
  }
  if (!check_exec_target_case(
          job, "/valid-exec-target.elf", valid_segments,
          (ctool_u32)(sizeof(valid_segments) / sizeof(valid_segments[0])),
          2u, 0u, 0u, 0u) ||
      !check_exec_target_case(
          job, "/outside-exec-target.elf", outside_segments,
          (ctool_u32)(sizeof(outside_segments) /
                      sizeof(outside_segments[0])),
          1u, 1u, 0u, 0u) ||
      !check_exec_target_case(
          job, "/data-exec-target.elf", data_target_segments,
          (ctool_u32)(sizeof(data_target_segments) /
                      sizeof(data_target_segments[0])),
          1u, 0u, 1u, 0u) ||
      !check_exec_target_case(
          job, "/executable-bss-target.elf", executable_bss_target_segments,
          (ctool_u32)(sizeof(executable_bss_target_segments) /
                      sizeof(executable_bss_target_segments[0])),
          1u, 0u, 1u, 0u) ||
      !check_exec_target_case(
          job, "/middle-exec-target.elf", middle_segments,
          (ctool_u32)(sizeof(middle_segments) /
                      sizeof(middle_segments[0])),
          1u, 0u, 0u, 1u) ||
      !check_exec_target_case(
          job, "/far-indirect-exec-target.elf", far_indirect_segments,
          (ctool_u32)(sizeof(far_indirect_segments) /
                      sizeof(far_indirect_segments[0])),
          0u, 0u, 0u, 0u)) {
    ctool_job_close(job);
    return 1;
  }
  {
    ctool_u8 overlap_image[256];
    ctool_u32 overlap_size = build_exec_fixture(
        overlap_image, (ctool_u32)sizeof(overlap_image), overlap_segments,
        (ctool_u32)(sizeof(overlap_segments) / sizeof(overlap_segments[0])));
    source.path.text = ctool_string("/overlapping-exec-target.elf");
    source.contents = ctool_bytes(overlap_image, overlap_size);
    request.views = CTOOL_DIS_VIEW_DISASSEMBLY;
    request.policies = CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS;
    (void)memset(&report, 0xa5, sizeof(report));
    status = ctool_dis_inspect(job, &source, &request, &report);
    if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                      "overlapping executable local target regions") ||
        !is_zeroed(&report, sizeof(report)) || overlap_size == 0u ||
        ctool_job_diagnostic_count(job) != 1u ||
        !check_diagnostic(
            job, 0u, CTOOL_DIS_DIAG_INVALID_REQUEST,
            "executable local target checks require non-overlapping "
            "executable load regions",
            "overlapping executable local target diagnostic")) {
      ctool_job_close(job);
      return 1;
    }
  }
  {
    static const ctool_u32 unsupported_types[] = {
        CTOOL_ELF32_PT_DYNAMIC, CTOOL_ELF32_PT_INTERP};
    ctool_u32 unsupported_index;
    for (unsupported_index = 0u;
         unsupported_index <
         (ctool_u32)(sizeof(unsupported_types) /
                     sizeof(unsupported_types[0]));
         unsupported_index++) {
      ctool_u8 unsupported_image[256];
      ctool_u32 unsupported_size = build_exec_fixture(
          unsupported_image, (ctool_u32)sizeof(unsupported_image),
          unsupported_program_segments,
          (ctool_u32)(sizeof(unsupported_program_segments) /
                      sizeof(unsupported_program_segments[0])));
      put_le32(unsupported_image, 84u,
               unsupported_types[unsupported_index]);
      source.path.text = ctool_string(
          unsupported_types[unsupported_index] == CTOOL_ELF32_PT_DYNAMIC
              ? "/dynamic-exec-target.elf"
              : "/interpreter-exec-target.elf");
      source.contents = ctool_bytes(unsupported_image, unsupported_size);
      request.views = CTOOL_DIS_VIEW_DISASSEMBLY;
      request.policies = CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS;
      (void)memset(&report, 0xa5, sizeof(report));
      status = ctool_dis_inspect(job, &source, &request, &report);
      if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                        "dynamic executable local target policy") ||
          unsupported_size == 0u ||
          !is_zeroed(&report, sizeof(report)) ||
          ctool_job_diagnostic_count(job) != unsupported_index + 2u ||
          !check_diagnostic(
              job, unsupported_index + 1u,
              CTOOL_DIS_DIAG_INVALID_REQUEST,
              "executable local target checks require a static image "
              "without PT_DYNAMIC or PT_INTERP",
              "dynamic executable local target diagnostic")) {
        ctool_job_close(job);
        return 1;
      }
    }
  }
  source.path.text = ctool_string("/program.elf");
  source.contents = ctool_bytes(image, (ctool_u32)sizeof(image));
  request.views = CTOOL_DIS_VIEW_SYMBOLS;
  request.policies = 0u;
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
  {
    ctool_u8 large_code[8192];
    ctool_u8 large_image[8276];
    exec_fixture_segment_t large_segment;
    ctool_u32 large_image_size;
    ctool_host_adapter_t limited_adapter;
    ctool_job_config_t config;
    ctool_limits_t limits = ctool_default_limits();
    ctool_source_t limited_source;
    ctool_dis_request_t limited_request;
    ctool_dis_report_t limited_report;
    ctool_status_t limited_status =
        ctool_host_adapter_init(&limited_adapter, ".");
    (void)memset(large_code, 0x90, sizeof(large_code));
    large_segment.address = 0x00400000u;
    large_segment.flags = CTOOL_ELF32_PF_R | CTOOL_ELF32_PF_X;
    large_segment.contents = large_code;
    large_segment.file_size = (ctool_u32)sizeof(large_code);
    large_segment.memory_size = (ctool_u32)sizeof(large_code);
    large_image_size = build_exec_fixture(
        large_image, (ctool_u32)sizeof(large_image), &large_segment, 1u);
    job = (ctool_job_t *)0;
    limits.arena_block_bytes = 512u;
    limits.arena_bytes = 1023u;
    config = ctool_host_job_config(&limited_adapter, limits);
    if (limited_status == CTOOL_OK) {
      limited_status = ctool_job_open(&config, &job);
    }
    if (!check_status(limited_status, CTOOL_OK,
                      "limited executable local target job") ||
        large_image_size == 0u) {
      return 1;
    }
    limited_source.path.text = ctool_string("/local-target-limit.elf");
    limited_source.contents = ctool_bytes(large_image, large_image_size);
    (void)memset(&limited_request, 0, sizeof(limited_request));
    limited_request.input = CTOOL_DIS_INPUT_ELF32;
    limited_request.views = CTOOL_DIS_VIEW_DISASSEMBLY;
    limited_request.policies = CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS;
    (void)memset(&limited_report, 0xa5, sizeof(limited_report));
    limited_status = ctool_dis_inspect(
        job, &limited_source, &limited_request, &limited_report);
    if (!check_status(limited_status, CTOOL_ERR_LIMIT,
                      "executable local target instruction map limit") ||
        !is_zeroed(&limited_report, sizeof(limited_report)) ||
        ctool_job_diagnostic_count(job) != 0u) {
      ctool_job_close(job);
      return 1;
    }
    limited_request.policies = 0u;
    limited_status = ctool_dis_inspect(
        job, &limited_source, &limited_request, &limited_report);
    if (!check_status(limited_status, CTOOL_OK,
                      "executable local target allocation recovery") ||
        limited_report.decode_summary.known_count != 8192u ||
        limited_report.decode_summary.direct_relative_target_count != 0u) {
      ctool_job_close(job);
      return 1;
    }
    ctool_job_close(job);
  }
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

static int expect_pe32_read_failure(
    ctool_job_t *job, const ctool_source_t *source, ctool_status_t expected,
    ctool_u32 code, const char *message, const char *operation) {
  ctool_pe32_image_t image;
  ctool_u32 diagnostic_index = ctool_job_diagnostic_count(job);
  ctool_status_t status = ctool_pe32_read(job, source, &image);
  if (!check_status(status, expected, operation) ||
      !is_zeroed(&image, sizeof(image)) ||
      ctool_job_diagnostic_count(job) != diagnostic_index + 1u ||
      !check_diagnostic(job, diagnostic_index, code, message, operation)) {
    return 0;
  }
  return 1;
}

static ctool_u32 build_large_memory_pe32(ctool_u8 *output,
                                          ctool_u32 capacity,
                                          ctool_bytes_t reference) {
  ctool_u32 text_header = 376u;
  ctool_u32 bss_header = text_header + 40u;
  if (output == (ctool_u8 *)0 || capacity < 1024u ||
      reference.size < 376u) {
    return 0u;
  }
  (void)memcpy(output, reference.data, 376u);
  (void)memset(output + 376u, 0, 1024u - 376u);
  put_le16(output, 132u + 2u, 2u);
  put_le32(output, 152u + 4u, 0x200u);
  put_le32(output, 152u + 8u, 0u);
  put_le32(output, 152u + 12u, 0x7fffe001u);
  put_le32(output, 152u + 16u, 0x1000u);
  put_le32(output, 152u + 20u, 0x1000u);
  put_le32(output, 152u + 24u, 0x2000u);
  put_le32(output, 152u + 56u, 0x80001000u);
  put_le32(output, 152u + 60u, 0x200u);
  (void)memset(output + 152u + 96u, 0, 16u * 8u);
  (void)memcpy(output + text_header, ".text", 5u);
  put_le32(output, text_header + 8u, 1u);
  put_le32(output, text_header + 12u, 0x1000u);
  put_le32(output, text_header + 16u, 0x200u);
  put_le32(output, text_header + 20u, 0x200u);
  put_le32(output, text_header + 36u,
           CTOOL_PE32_SCN_CODE | CTOOL_PE32_SCN_EXECUTE |
               CTOOL_PE32_SCN_READ);
  (void)memcpy(output + bss_header, ".bss", 4u);
  put_le32(output, bss_header + 8u, 0x7fffe001u);
  put_le32(output, bss_header + 12u, 0x2000u);
  put_le32(output, bss_header + 36u,
           CTOOL_PE32_SCN_UNINITIALIZED_DATA | CTOOL_PE32_SCN_READ |
               CTOOL_PE32_SCN_WRITE);
  output[0x200u] = 0xc3u;
  return 1024u;
}

static int run_pe32(void) {
  ctool_host_adapter_t adapter;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_source_t source;
  ctool_pe32_image_t image;
  ctool_dis_request_t request;
  ctool_dis_report_t report;
  capture_t capture;
  ctool_u8 *damaged;
  ctool_status_t status;
  if (!open_seed_job(&adapter, &job, &source)) {
    return 1;
  }
  status = ctool_pe32_read(job, &source, &image);
  if (!check_status(status, CTOOL_OK, "checked PE32 seed read") ||
      image.section_count == 0u || image.import_count == 0u ||
      image.import_library_count == 0u || image.entry_point < 0x00401000u) {
    ctool_job_close(job);
    return 1;
  }
  (void)memset(&request, 0, sizeof(request));
  request.input = CTOOL_DIS_INPUT_PE32;
  request.views = CTOOL_DIS_VIEW_DISASSEMBLY;
  request.policies = CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS |
                     CTOOL_DIS_POLICY_CODE_ANCHORS;
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (!check_status(status, CTOOL_OK, "checked PE32 seed inspection") ||
      report.pe32.section_count != image.section_count ||
      report.pe32.import_count != image.import_count ||
      report.decode_summary.code_anchor_count != 1u ||
      report.decode_summary.code_anchor_outside_executable_count != 0u ||
      report.decode_summary.code_anchor_mid_instruction_count != 0u) {
    ctool_job_close(job);
    return 1;
  }
  request.views = CTOOL_DIS_VIEW_HEADER | CTOOL_DIS_VIEW_SECTIONS |
                  CTOOL_DIS_VIEW_IMPORTS;
  request.policies = 0u;
  (void)memset(&capture, 0, sizeof(capture));
  status = ctool_dis_inspect(job, &source, &request, &report);
  if (status == CTOOL_OK) {
    status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&capture));
  }
  if (!check_status(status, CTOOL_OK, "checked PE32 seed rendering") ||
      !contains(&capture, "PE32 i386", "PE32 header") ||
      !contains(&capture, "[sections]", "PE32 sections") ||
      !contains(&capture, "[imports]", "PE32 imports")) {
    ctool_job_close(job);
    return 1;
  }
  damaged = (ctool_u8 *)malloc((size_t)source.contents.size);
  if (damaged == (ctool_u8 *)0) {
    ctool_job_close(job);
    return 1;
  }
  {
    ctool_source_t invalid = source;
    (void)memcpy(damaged, source.contents.data, source.contents.size);
    invalid.contents = ctool_bytes(damaged, 64u);
    if (!expect_pe32_read_failure(
            job, &invalid, CTOOL_ERR_INPUT,
            CTOOL_PE32_DIAG_BAD_DOS_HEADER,
            "PE32 DOS header is truncated", "truncated PE32 DOS header")) {
      free(damaged);
      ctool_job_close(job);
      return 1;
    }
    invalid.contents = ctool_bytes(damaged, source.contents.size);
    damaged[2] ^= 1u;
    if (!expect_pe32_read_failure(
            job, &invalid, CTOOL_ERR_UNSUPPORTED,
            CTOOL_PE32_DIAG_BAD_DOS_HEADER,
            "PE32 DOS stub is not the CupidLD profile",
            "malformed PE32 DOS header")) {
      free(damaged);
      ctool_job_close(job);
      return 1;
    }
    (void)memcpy(damaged, source.contents.data, source.contents.size);
    invalid.contents = ctool_bytes(damaged, 140u);
    if (!expect_pe32_read_failure(
            job, &invalid, CTOOL_ERR_INPUT,
            CTOOL_PE32_DIAG_BAD_SIGNATURE,
            "PE32 signature or COFF header is truncated",
            "truncated PE32 COFF header")) {
      free(damaged);
      ctool_job_close(job);
      return 1;
    }
    invalid.contents = ctool_bytes(damaged, source.contents.size);
    put_le16(damaged, 132u, 0u);
    if (!expect_pe32_read_failure(
            job, &invalid, CTOOL_ERR_UNSUPPORTED,
            CTOOL_PE32_DIAG_UNSUPPORTED_COFF,
            "PE32 COFF header is outside the CupidLD profile",
            "malformed PE32 COFF header")) {
      free(damaged);
      ctool_job_close(job);
      return 1;
    }
    (void)memcpy(damaged, source.contents.data, source.contents.size);
    invalid.contents = ctool_bytes(damaged, 200u);
    if (!expect_pe32_read_failure(
            job, &invalid, CTOOL_ERR_INPUT,
            CTOOL_PE32_DIAG_BAD_OPTIONAL_HEADER,
            "PE32 optional header is truncated",
            "truncated PE32 optional header")) {
      free(damaged);
      ctool_job_close(job);
      return 1;
    }
    invalid.contents = ctool_bytes(damaged, source.contents.size);
    put_le16(damaged, 152u, 0x020bu);
    if (!expect_pe32_read_failure(
            job, &invalid, CTOOL_ERR_UNSUPPORTED,
            CTOOL_PE32_DIAG_BAD_OPTIONAL_HEADER,
            "PE optional header is not PE32",
            "malformed PE32 optional header")) {
      free(damaged);
      ctool_job_close(job);
      return 1;
    }
  }
  {
    ctool_source_t invalid = source;
    ctool_u32 second_section = 376u + 40u;
    (void)memcpy(damaged, source.contents.data, source.contents.size);
    put_le32(damaged, second_section + 12u, 0x1000u);
    invalid.contents = ctool_bytes(damaged, source.contents.size);
    if (!expect_pe32_read_failure(
            job, &invalid, CTOOL_ERR_INPUT,
            CTOOL_PE32_DIAG_BAD_SECTION,
            "PE32 section virtual ranges overlap",
            "overlapping PE32 sections")) {
      free(damaged);
      ctool_job_close(job);
      return 1;
    }
    (void)memcpy(damaged, source.contents.data, source.contents.size);
    put_le32(damaged, 376u + 20u, source.contents.size);
    if (!expect_pe32_read_failure(
            job, &invalid, CTOOL_ERR_INPUT,
            CTOOL_PE32_DIAG_BAD_SECTION,
            "PE32 section file range is invalid",
            "out-of-bounds PE32 section")) {
      free(damaged);
      ctool_job_close(job);
      return 1;
    }
    (void)memcpy(damaged, source.contents.data, source.contents.size);
    put_le32(damaged, 152u + 16u, image.image_size);
    if (!expect_pe32_read_failure(
            job, &invalid, CTOOL_ERR_INPUT,
            CTOOL_PE32_DIAG_BAD_ENTRY,
            "PE32 entry is not file-backed executable code",
            "out-of-range PE32 entry")) {
      free(damaged);
      ctool_job_close(job);
      return 1;
    }
    invalid.contents = ctool_bytes(
        damaged,
        build_large_memory_pe32(damaged, source.contents.size,
                                source.contents));
    if (invalid.contents.size == 0u ||
        !expect_pe32_read_failure(
            job, &invalid, CTOOL_ERR_UNSUPPORTED,
            CTOOL_PE32_DIAG_BAD_OPTIONAL_HEADER,
            "PE32 image exceeds CupidLD's 2 GiB RVA range",
            "oversized PE32 memory image")) {
      free(damaged);
      ctool_job_close(job);
      return 1;
    }
  }
  {
    ctool_source_t invalid = source;
    const ctool_pe32_section_t *idata =
        (const ctool_pe32_section_t *)0;
    ctool_u32 descriptor_offset;
    ctool_u32 idata_end;
    ctool_u32 lookup_offset;
    ctool_u32 iat_size_offset = 152u + 96u + 12u * 8u + 4u;
    ctool_u32 section_index;
    for (section_index = 0u; section_index < image.section_count;
         section_index++) {
      if (image.sections[section_index].kind == CTOOL_PE32_SECTION_IDATA) {
        idata = &image.sections[section_index];
      }
    }
    if (idata == (const ctool_pe32_section_t *)0 ||
        image.import_library_count == 0u || image.import_count == 0u) {
      free(damaged);
      ctool_job_close(job);
      return 1;
    }
    descriptor_offset = idata->file_offset;
    idata_end = idata->file_offset + idata->virtual_size;
    lookup_offset =
        idata->file_offset +
        (image.import_libraries[0].lookup_rva - idata->virtual_address);
    (void)memcpy(damaged, source.contents.data, source.contents.size);
    put_le32(damaged, descriptor_offset + 4u, 1u);
    invalid.contents = ctool_bytes(damaged, source.contents.size);
    if (!expect_pe32_read_failure(
            job, &invalid, CTOOL_ERR_UNSUPPORTED,
            CTOOL_PE32_DIAG_BAD_IMPORT,
            "stateful PE32 import descriptors are unsupported",
            "invalid PE32 import descriptor")) {
      free(damaged);
      ctool_job_close(job);
      return 1;
    }
    (void)memcpy(damaged, source.contents.data, source.contents.size);
    put_le32(damaged, 152u + 96u + 1u * 8u + 4u,
             ((idata->virtual_size / 20u) + 2u) * 20u);
    if (!expect_pe32_read_failure(
            job, &invalid, CTOOL_ERR_INPUT,
            CTOOL_PE32_DIAG_BAD_IMPORT,
            "PE32 import directory is out of range",
            "out-of-range PE32 import directory")) {
      free(damaged);
      ctool_job_close(job);
      return 1;
    }
    (void)memcpy(damaged, source.contents.data, source.contents.size);
    put_le32(damaged, descriptor_offset,
             image.import_libraries[0].lookup_rva + 4u);
    if (!expect_pe32_read_failure(
            job, &invalid, CTOOL_ERR_INPUT,
            CTOOL_PE32_DIAG_BAD_IMPORT,
            "PE32 import lookup tables are not canonical",
            "misordered PE32 lookup table")) {
      free(damaged);
      ctool_job_close(job);
      return 1;
    }
    (void)memcpy(damaged, source.contents.data, source.contents.size);
    put_le32(damaged, iat_size_offset, image.iat_directory_size - 4u);
    if (!expect_pe32_read_failure(
            job, &invalid, CTOOL_ERR_INPUT,
            CTOOL_PE32_DIAG_BAD_DIRECTORY,
            "PE32 IAT directory does not match its tables",
            "invalid PE32 IAT extent")) {
      free(damaged);
      ctool_job_close(job);
      return 1;
    }
    (void)memcpy(damaged, source.contents.data, source.contents.size);
    put_le32(damaged, lookup_offset,
             get_le32(damaged, lookup_offset) | 0x80000000u);
    if (!expect_pe32_read_failure(
            job, &invalid, CTOOL_ERR_UNSUPPORTED,
            CTOOL_PE32_DIAG_BAD_IMPORT,
            "ordinal PE32 imports are unsupported",
            "ordinal PE32 import")) {
      free(damaged);
      ctool_job_close(job);
      return 1;
    }
    status = ctool_pe32_read(job, &source, &report.pe32);
    if (!check_status(status, CTOOL_OK,
                      "PE32 post-allocation recovery") ||
        report.pe32.import_count != image.import_count) {
      free(damaged);
      ctool_job_close(job);
      return 1;
    }
    {
      const ctool_pe32_import_library_t *last_library =
          &image.import_libraries[image.import_library_count - 1u];
      ctool_u32 terminal =
          idata->file_offset +
          (last_library->lookup_rva - idata->virtual_address) +
          last_library->import_count * 4u;
      (void)memcpy(damaged, source.contents.data, source.contents.size);
      (void)memset(damaged + terminal, 1, idata_end - terminal);
      if (!expect_pe32_read_failure(
              job, &invalid, CTOOL_ERR_INPUT,
              CTOOL_PE32_DIAG_BAD_IMPORT,
              "PE32 import lookup table is unterminated or empty",
              "unterminated PE32 lookup table")) {
        free(damaged);
        ctool_job_close(job);
        return 1;
      }
    }
    {
      const ctool_pe32_import_t *last_import =
          &image.imports[image.import_count - 1u];
      ctool_u32 name_offset =
          idata->file_offset +
          (last_import->hint_name_rva - idata->virtual_address) + 2u;
      ctool_u32 terminator = name_offset + last_import->procedure_name.size;
      (void)memcpy(damaged, source.contents.data, source.contents.size);
      (void)memset(damaged + terminator, (int)'A',
                   idata_end - terminator);
      if (!expect_pe32_read_failure(
              job, &invalid, CTOOL_ERR_INPUT,
              CTOOL_PE32_DIAG_BAD_IMPORT,
              "PE32 import hint or procedure name is invalid",
              "unterminated PE32 procedure name")) {
        free(damaged);
        ctool_job_close(job);
        return 1;
      }
    }
  }
  {
    ctool_source_t changed = source;
    ctool_u32 text_offset = image.sections[0].file_offset;
    (void)memcpy(damaged, source.contents.data, source.contents.size);
    damaged[text_offset] = 0x0fu;
    damaged[text_offset + 1u] = 0xffu;
    changed.contents = ctool_bytes(damaged, source.contents.size);
    (void)memset(&request, 0, sizeof(request));
    request.input = CTOOL_DIS_INPUT_PE32;
    request.views = CTOOL_DIS_VIEW_DISASSEMBLY;
    status = ctool_dis_inspect(job, &changed, &request, &report);
    if (!check_status(status, CTOOL_OK, "unknown PE32 opcode") ||
        report.decode_summary.unknown_count == 0u) {
      free(damaged);
      ctool_job_close(job);
      return 1;
    }
    (void)memcpy(damaged, source.contents.data, source.contents.size);
    damaged[text_offset] = 0xe9u;
    damaged[text_offset + 1u] = 0xfbu;
    damaged[text_offset + 2u] = 0xffu;
    damaged[text_offset + 3u] = 0xffu;
    damaged[text_offset + 4u] = 0x7fu;
    request.policies = CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS;
    status = ctool_dis_inspect(job, &changed, &request, &report);
    if (!check_status(status, CTOOL_OK, "invalid PE32 local target") ||
        report.decode_summary.direct_relative_target_count == 0u ||
        report.decode_summary.direct_relative_outside_image_count == 0u) {
      free(damaged);
      ctool_job_close(job);
      return 1;
    }
    (void)memcpy(damaged, source.contents.data, source.contents.size);
    put_le32(damaged, 152u + 16u,
             image.sections[0].virtual_address + 2u);
    request.policies = CTOOL_DIS_POLICY_CODE_ANCHORS;
    status = ctool_dis_inspect(job, &changed, &request, &report);
    if (!check_status(status, CTOOL_OK, "invalid PE32 entry anchor") ||
        report.decode_summary.code_anchor_count != 1u ||
        report.decode_summary.code_anchor_mid_instruction_count != 1u) {
      free(damaged);
      ctool_job_close(job);
      return 1;
    }
  }
  {
    ctool_source_t invalid = source;
    ctool_dis_report_t invalid_report;
    (void)memcpy(damaged, source.contents.data, source.contents.size);
    invalid.contents = ctool_bytes(damaged, 64u);
    (void)memset(&request, 0, sizeof(request));
    request.input = CTOOL_DIS_INPUT_PE32;
    request.views = CTOOL_DIS_VIEW_HEADER;
    status = ctool_dis_inspect(job, &invalid, &request, &report);
    if (!check_status(status, CTOOL_ERR_INPUT,
                      "PE32 inspection rollback") ||
        !is_zeroed(&report, sizeof(report))) {
      free(damaged);
      ctool_job_close(job);
      return 1;
    }
    status = ctool_dis_inspect(job, &source, &request, &report);
    if (!check_status(status, CTOOL_OK, "PE32 same-process recovery")) {
      free(damaged);
      ctool_job_close(job);
      return 1;
    }
    invalid_report = report;
    invalid_report.pe32.sections = (const ctool_pe32_section_t *)0;
    (void)memset(&capture, 0, sizeof(capture));
    status = ctool_dis_render(job, &invalid_report, CTOOL_DIS_TEXT_CUPID,
                              capture_sink(&capture));
    if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                      "forged PE32 report") ||
        capture.size != 0u) {
      free(damaged);
      ctool_job_close(job);
      return 1;
    }
  }
  free(damaged);
  ctool_job_close(job);
  (void)puts("pe32: ok");
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
  if (strcmp(argv[1], "indexed") == 0) {
    return run_indexed();
  }
  if (strcmp(argv[1], "targets") == 0) {
    return run_targets();
  }
  if (strcmp(argv[1], "object") == 0) {
    return run_object();
  }
  if (strcmp(argv[1], "exec") == 0) {
    return run_exec();
  }
  if (strcmp(argv[1], "anchors") == 0) {
    return run_anchors();
  }
  if (strcmp(argv[1], "nm") == 0) {
    return run_nm();
  }
  if (strcmp(argv[1], "pe32") == 0) {
    return run_pe32();
  }
  if (strcmp(argv[1], "errors") == 0) {
    return run_errors();
  }
  (void)fprintf(stderr, "unknown mode: %s\n", argv[1]);
  return 2;
}
