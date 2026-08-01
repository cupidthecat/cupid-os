#include "ctool.h"
#include "ctool_host.h"
#include "cupidld.h"
#include "elf32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ctool_u16 read_le16(const ctool_u8 *bytes, ctool_u32 offset) {
  return (ctool_u16)((ctool_u16)bytes[offset] |
                     (ctool_u16)((ctool_u16)bytes[offset + 1u] << 8u));
}

static ctool_u32 read_le32(const ctool_u8 *bytes, ctool_u32 offset) {
  return (ctool_u32)bytes[offset] |
         ((ctool_u32)bytes[offset + 1u] << 8u) |
         ((ctool_u32)bytes[offset + 2u] << 16u) |
         ((ctool_u32)bytes[offset + 3u] << 24u);
}

static int open_job(ctool_host_adapter_t *adapter,
                    ctool_job_config_t *config, ctool_job_t **job) {
  ctool_status_t status = ctool_host_adapter_init(adapter, ".");
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "host adapter init: %s\n",
                  ctool_status_name(status));
    return 0;
  }
  *config = ctool_host_job_config(adapter, ctool_default_limits());
  status = ctool_job_open(config, job);
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "job open: %s\n", ctool_status_name(status));
    return 0;
  }
  return 1;
}

static int run_fixed_basic(void) {
  static const ctool_u8 text[] = {0xc3u};
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_buffer_t *object_buffer;
  ctool_buffer_t *output;
  ctool_buffer_t *repeat_output;
  ctool_elf32_section_spec_t section;
  ctool_elf32_symbol_spec_t symbol;
  ctool_elf32_object_spec_t object_spec;
  ctool_source_t object_source;
  ctool_ld_request_t request;
  ctool_ld_result_t result;
  ctool_ld_result_t repeat_result;
  ctool_source_t executable_source;
  ctool_elf32_object_t executable;
  const ctool_elf32_section_t *linked_text =
      (const ctool_elf32_section_t *)0;
  const ctool_elf32_symbol_t *linked_entry =
      (const ctool_elf32_symbol_t *)0;
  ctool_bytes_t image;
  ctool_status_t status;
  ctool_u32 index;

  if (!open_job(&adapter, &config, &job)) {
    return 1;
  }
  status =
      ctool_job_open_buffer(job, 256u, config.limits.output_bytes,
                            &object_buffer);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes,
                                   &output);
  }
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "buffer open: %s\n", ctool_status_name(status));
    ctool_job_close(job);
    return 1;
  }

  section.name = ctool_string(".text");
  section.type = CTOOL_ELF32_SHT_PROGBITS;
  section.flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR;
  section.alignment = 16u;
  section.entry_size = 0u;
  section.size = (ctool_u32)sizeof(text);
  section.contents = ctool_bytes(text, (ctool_u32)sizeof(text));
  symbol.name = ctool_string("_start");
  symbol.binding = CTOOL_ELF32_BIND_GLOBAL;
  symbol.type = CTOOL_ELF32_SYMBOL_FUNCTION;
  symbol.visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbol.placement = CTOOL_ELF32_SYMBOL_DEFINED;
  symbol.section = 0u;
  symbol.value = 0u;
  symbol.size = (ctool_u32)sizeof(text);
  symbol.alignment = 0u;
  object_spec.sections = &section;
  object_spec.section_count = 1u;
  object_spec.symbols = &symbol;
  object_spec.symbol_count = 1u;
  object_spec.relocations = (const ctool_elf32_relocation_spec_t *)0;
  object_spec.relocation_count = 0u;
  status = ctool_elf32_write(job, &object_spec, object_buffer);
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "fixture write: %s\n", ctool_status_name(status));
    ctool_buffer_close(output);
    ctool_buffer_close(object_buffer);
    ctool_job_close(job);
    return 1;
  }

  object_source.path.text = ctool_string("/start.o");
  object_source.contents = ctool_buffer_view(object_buffer);
  request.objects = &object_source;
  request.object_count = 1u;
  request.layout.kind = CTOOL_LD_LAYOUT_FIXED_TEXT;
  request.layout.as.fixed_text.base_address = 0x00100000u;
  request.layout.as.fixed_text.entry_symbol = ctool_string("_start");
  request.maximum_image_span = 0x00010000u;
  (void)memset(&result, 0xa5, sizeof(result));
  status = ctool_ld_link(job, &request, output, &result);
  image = ctool_buffer_view(output);
  if (status != CTOOL_OK || result.bytes != image.size || image.size < 84u ||
      result.entry != 0x00100000u || result.load_address != 0x00100000u ||
      result.loaded_end != 0x00100001u ||
      result.memory_end != 0x00100001u || result.output_section_count != 1u ||
      result.resolved_symbol_count != 1u ||
      result.applied_relocation_count != 0u || read_le16(image.data, 16u) != 2u ||
      read_le16(image.data, 18u) != 3u ||
      read_le32(image.data, 24u) != 0x00100000u) {
    (void)fprintf(stderr, "fixed basic result differs (status=%s)\n",
                  ctool_status_name(status));
    ctool_buffer_close(output);
    ctool_buffer_close(object_buffer);
    ctool_job_close(job);
    return 1;
  }

  executable_source.path.text = ctool_string("/linked.elf");
  executable_source.contents = image;
  status = ctool_elf32_read(job, &executable_source, &executable);
  if (status != CTOOL_OK || executable.file_type != CTOOL_ELF32_ET_EXEC ||
      executable.entry_point != 0x00100000u ||
      executable.program_header_count != 2u ||
      executable.program_headers[1].type != 0x6474e551u ||
      executable.program_headers[1].flags != 6u) {
    (void)fprintf(stderr, "fixed basic ELF is not readable\n");
    ctool_buffer_close(output);
    ctool_buffer_close(object_buffer);
    ctool_job_close(job);
    return 1;
  }
  for (index = 0u; index < executable.section_count; index++) {
    if (executable.sections[index].name.size == 5u &&
        memcmp(executable.sections[index].name.data, ".text", 5u) == 0) {
      linked_text = &executable.sections[index];
    }
  }
  for (index = 0u; index < executable.symbol_count; index++) {
    if (executable.symbols[index].name.size == 6u &&
        memcmp(executable.symbols[index].name.data, "_start", 6u) == 0) {
      linked_entry = &executable.symbols[index];
    }
  }
  if (linked_text == (const ctool_elf32_section_t *)0 ||
      linked_text->address != 0x00100000u || linked_text->size != 1u ||
      linked_text->contents.size != 1u || linked_text->contents.data[0] != 0xc3u ||
      linked_entry == (const ctool_elf32_symbol_t *)0 ||
      linked_entry->value != 0x00100000u ||
      linked_entry->placement != CTOOL_ELF32_SYMBOL_DEFINED) {
    (void)fprintf(stderr, "fixed basic linked contents differ\n");
    ctool_buffer_close(output);
    ctool_buffer_close(object_buffer);
    ctool_job_close(job);
    return 1;
  }

  repeat_output = (ctool_buffer_t *)0;
  status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes,
                                 &repeat_output);
  if (status == CTOOL_OK) {
    status = ctool_ld_link(job, &request, repeat_output, &repeat_result);
  }
  if (status != CTOOL_OK || memcmp(&repeat_result, &result, sizeof(result)) != 0 ||
      ctool_buffer_view(repeat_output).size != image.size ||
      memcmp(ctool_buffer_view(repeat_output).data, image.data, image.size) !=
          0) {
    (void)fprintf(stderr, "fixed basic output is not deterministic\n");
    if (repeat_output != (ctool_buffer_t *)0) {
      ctool_buffer_close(repeat_output);
    }
    ctool_buffer_close(output);
    ctool_buffer_close(object_buffer);
    ctool_job_close(job);
    return 1;
  }
  ctool_buffer_close(repeat_output);

  ctool_buffer_close(output);
  ctool_buffer_close(object_buffer);
  ctool_job_close(job);
  (void)puts("fixed-basic: ok");
  return 0;
}

static int run_fixed_layout(void) {
  static const ctool_u8 text[] = {0xa1u, 0u, 0u, 0u, 0u, 0xc3u};
  static const ctool_u8 rodata[] = {1u, 2u, 3u};
  static const ctool_u8 eh_frame[] = {4u, 5u, 6u, 7u};
  static const ctool_u8 note[] = {8u, 9u, 10u, 11u};
  static const ctool_u8 data[] = {0x44u, 0x33u, 0x22u, 0x11u};
  static const char *names[] = {".text", ".rodata", ".eh_frame",
                                ".note.gnu.property", ".data", ".bss"};
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_buffer_t *object_buffer;
  ctool_buffer_t *output;
  ctool_elf32_section_spec_t sections[6];
  ctool_elf32_symbol_spec_t symbols[2];
  ctool_elf32_relocation_spec_t relocation;
  ctool_elf32_object_spec_t object_spec;
  ctool_source_t object_source;
  ctool_ld_request_t request;
  ctool_ld_result_t result;
  ctool_source_t executable_source;
  ctool_elf32_object_t executable;
  const ctool_elf32_section_t *linked_text =
      (const ctool_elf32_section_t *)0;
  const ctool_elf32_section_t *linked_rodata =
      (const ctool_elf32_section_t *)0;
  const ctool_elf32_section_t *linked_data =
      (const ctool_elf32_section_t *)0;
  const ctool_elf32_section_t *linked_bss =
      (const ctool_elf32_section_t *)0;
  ctool_status_t status;
  ctool_u32 index;

  if (!open_job(&adapter, &config, &job)) {
    return 1;
  }
  status =
      ctool_job_open_buffer(job, 512u, config.limits.output_bytes,
                            &object_buffer);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 512u, config.limits.output_bytes,
                                   &output);
  }
  if (status != CTOOL_OK) {
    ctool_job_close(job);
    return 1;
  }
  (void)memset(sections, 0, sizeof(sections));
  for (index = 0u; index < 6u; index++) {
    sections[index].name = ctool_string(names[index]);
    sections[index].type = index == 5u ? CTOOL_ELF32_SHT_NOBITS
                                       : CTOOL_ELF32_SHT_PROGBITS;
    sections[index].flags = CTOOL_ELF32_SHF_ALLOC;
    sections[index].alignment = index == 0u ? 16u : 4u;
  }
  sections[0].flags |= CTOOL_ELF32_SHF_EXECINSTR;
  sections[0].size = (ctool_u32)sizeof(text);
  sections[0].contents = ctool_bytes(text, (ctool_u32)sizeof(text));
  sections[1].size = (ctool_u32)sizeof(rodata);
  sections[1].contents = ctool_bytes(rodata, (ctool_u32)sizeof(rodata));
  sections[2].size = (ctool_u32)sizeof(eh_frame);
  sections[2].contents = ctool_bytes(eh_frame, (ctool_u32)sizeof(eh_frame));
  sections[3].size = (ctool_u32)sizeof(note);
  sections[3].contents = ctool_bytes(note, (ctool_u32)sizeof(note));
  sections[4].flags |= CTOOL_ELF32_SHF_WRITE;
  sections[4].size = (ctool_u32)sizeof(data);
  sections[4].contents = ctool_bytes(data, (ctool_u32)sizeof(data));
  sections[5].flags |= CTOOL_ELF32_SHF_WRITE;
  sections[5].size = 16u;
  sections[5].contents = ctool_bytes((const void *)0, 0u);

  (void)memset(symbols, 0, sizeof(symbols));
  symbols[0].name = ctool_string("_start");
  symbols[0].binding = CTOOL_ELF32_BIND_GLOBAL;
  symbols[0].type = CTOOL_ELF32_SYMBOL_FUNCTION;
  symbols[0].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[0].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  symbols[0].section = 0u;
  symbols[0].size = (ctool_u32)sizeof(text);
  symbols[1].name = ctool_string("datum");
  symbols[1].binding = CTOOL_ELF32_BIND_GLOBAL;
  symbols[1].type = CTOOL_ELF32_SYMBOL_OBJECT;
  symbols[1].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[1].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  symbols[1].section = 4u;
  symbols[1].size = (ctool_u32)sizeof(data);
  relocation.target_section = 0u;
  relocation.offset = 1u;
  relocation.symbol = 1u;
  relocation.type = CTOOL_ELF32_R_386_32;
  relocation.addend = 0;
  object_spec.sections = sections;
  object_spec.section_count = 6u;
  object_spec.symbols = symbols;
  object_spec.symbol_count = 2u;
  object_spec.relocations = &relocation;
  object_spec.relocation_count = 1u;
  status = ctool_elf32_write(job, &object_spec, object_buffer);
  if (status != CTOOL_OK) {
    ctool_buffer_close(output);
    ctool_buffer_close(object_buffer);
    ctool_job_close(job);
    return 1;
  }
  object_source.path.text = ctool_string("/layout.o");
  object_source.contents = ctool_buffer_view(object_buffer);
  request.objects = &object_source;
  request.object_count = 1u;
  request.layout.kind = CTOOL_LD_LAYOUT_FIXED_TEXT;
  request.layout.as.fixed_text.base_address = 0x00100000u;
  request.layout.as.fixed_text.entry_symbol = ctool_string("_start");
  request.maximum_image_span = 0x00010000u;
  status = ctool_ld_link(job, &request, output, &result);
  executable_source.path.text = ctool_string("/layout.elf");
  executable_source.contents = ctool_buffer_view(output);
  if (status == CTOOL_OK) {
    status = ctool_elf32_read(job, &executable_source, &executable);
  }
  if (status != CTOOL_OK || result.entry != 0x00100000u ||
      result.load_address != 0x00100000u ||
      result.loaded_end != 0x00102004u ||
      result.memory_end != 0x00103010u || result.output_section_count != 4u ||
      result.resolved_symbol_count != 2u ||
      result.applied_relocation_count != 1u ||
      executable.program_header_count != 5u) {
    (void)fprintf(stderr, "fixed layout result differs (status=%s)\n",
                  ctool_status_name(status));
    ctool_buffer_close(output);
    ctool_buffer_close(object_buffer);
    ctool_job_close(job);
    return 1;
  }
  for (index = 0u; index < 4u; index++) {
    ctool_bytes_t image = ctool_buffer_view(output);
    ctool_u32 header = read_le32(image.data, 28u) + index * 32u;
    ctool_u32 file_offset = read_le32(image.data, header + 4u);
    ctool_u32 address = read_le32(image.data, header + 8u);
    ctool_u32 flags = read_le32(image.data, header + 24u);
    ctool_u32 expected_flags = index == 0u ? 5u : (index == 1u ? 4u : 6u);
    if (read_le32(image.data, header) != 1u ||
        file_offset % 4096u != address % 4096u ||
        flags != expected_flags || read_le32(image.data, header + 28u) != 4096u) {
      (void)fprintf(stderr, "fixed layout PT_LOAD differs\n");
      ctool_buffer_close(output);
      ctool_buffer_close(object_buffer);
      ctool_job_close(job);
      return 1;
    }
  }
  if (executable.program_headers[4].type != 0x6474e551u ||
      executable.program_headers[4].file_size != 0u ||
      executable.program_headers[4].memory_size != 0u ||
      executable.program_headers[4].flags != 6u ||
      executable.program_headers[4].alignment != 0u) {
    (void)fprintf(stderr, "fixed layout GNU_STACK differs\n");
    ctool_buffer_close(output);
    ctool_buffer_close(object_buffer);
    ctool_job_close(job);
    return 1;
  }
  for (index = 1u; index < executable.section_count; index++) {
    const ctool_elf32_section_t *candidate = &executable.sections[index];
    if (candidate->name.size == 5u &&
        memcmp(candidate->name.data, ".text", 5u) == 0) {
      linked_text = candidate;
    } else if (candidate->name.size == 7u &&
               memcmp(candidate->name.data, ".rodata", 7u) == 0) {
      linked_rodata = candidate;
    } else if (candidate->name.size == 5u &&
               memcmp(candidate->name.data, ".data", 5u) == 0) {
      linked_data = candidate;
    } else if (candidate->name.size == 4u &&
               memcmp(candidate->name.data, ".bss", 4u) == 0) {
      linked_bss = candidate;
    }
  }
  if (linked_text == (const ctool_elf32_section_t *)0 ||
      linked_rodata == (const ctool_elf32_section_t *)0 ||
      linked_data == (const ctool_elf32_section_t *)0 ||
      linked_bss == (const ctool_elf32_section_t *)0 ||
      linked_text->address != 0x00100000u || linked_text->size != 6u ||
      read_le32(linked_text->contents.data, 1u) != 0x00102000u ||
      linked_rodata->address != 0x00101000u || linked_rodata->size != 12u ||
      linked_data->address != 0x00102000u || linked_data->size != 4u ||
      linked_bss->address != 0x00103000u || linked_bss->size != 16u ||
      linked_bss->type != (ctool_u32)CTOOL_ELF32_SHT_NOBITS) {
    (void)fprintf(stderr, "fixed layout section placement differs\n");
    ctool_buffer_close(output);
    ctool_buffer_close(object_buffer);
    ctool_job_close(job);
    return 1;
  }
  ctool_buffer_close(output);
  ctool_buffer_close(object_buffer);
  ctool_job_close(job);
  (void)puts("fixed-layout: ok");
  return 0;
}

static const ctool_elf32_symbol_t *find_linked_symbol(
    const ctool_elf32_object_t *object, const char *name) {
  ctool_string_t expected = ctool_string(name);
  ctool_u32 index;
  for (index = 1u; index < object->symbol_count; index++) {
    const ctool_elf32_symbol_t *symbol = &object->symbols[index];
    if (symbol->name.size == expected.size &&
        memcmp(symbol->name.data, expected.data, expected.size) == 0) {
      return symbol;
    }
  }
  return (const ctool_elf32_symbol_t *)0;
}

static ctool_u32 find_raw_section(ctool_bytes_t image, const char *name) {
  ctool_u32 section_headers = read_le32(image.data, 32u);
  ctool_u32 section_count = (ctool_u32)read_le16(image.data, 48u);
  ctool_u32 names_index = (ctool_u32)read_le16(image.data, 50u);
  ctool_u32 names_header = section_headers + names_index * 40u;
  ctool_u32 names_offset = read_le32(image.data, names_header + 16u);
  ctool_u32 index;
  for (index = 1u; index < section_count; index++) {
    ctool_u32 header = section_headers + index * 40u;
    ctool_u32 name_offset = read_le32(image.data, header);
    if (strcmp((const char *)(image.data + names_offset + name_offset), name) ==
        0) {
      return index;
    }
  }
  return 0u;
}

static void write_le16(ctool_u8 *bytes, ctool_u32 offset, ctool_u16 value) {
  bytes[offset] = (ctool_u8)(value & 0xffu);
  bytes[offset + 1u] = (ctool_u8)((value >> 8u) & 0xffu);
}

static void write_le32(ctool_u8 *bytes, ctool_u32 offset, ctool_u32 value) {
  bytes[offset] = (ctool_u8)(value & 0xffu);
  bytes[offset + 1u] = (ctool_u8)((value >> 8u) & 0xffu);
  bytes[offset + 2u] = (ctool_u8)((value >> 16u) & 0xffu);
  bytes[offset + 3u] = (ctool_u8)((value >> 24u) & 0xffu);
}

static int result_is_zero(const ctool_ld_result_t *result) {
  static const ctool_ld_result_t zero = {0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
  return memcmp(result, &zero, sizeof(zero)) == 0;
}

static int expect_link_failure(ctool_job_t *job,
                               const ctool_ld_request_t *request,
                               ctool_buffer_t *output,
                               ctool_status_t expected_status,
                               ctool_u32 expected_code,
                               const char *case_name) {
  ctool_ld_result_t result;
  ctool_u32 output_before = ctool_buffer_view(output).size;
  ctool_u32 diagnostics_before = ctool_job_diagnostic_count(job);
  ctool_u32 index;
  ctool_bool found = CTOOL_FALSE;
  ctool_status_t status;
  (void)memset(&result, 0xa5, sizeof(result));
  status = ctool_ld_link(job, request, output, &result);
  for (index = diagnostics_before; index < ctool_job_diagnostic_count(job);
       index++) {
    const ctool_diagnostic_t *diagnostic = ctool_job_diagnostic(job, index);
    if (diagnostic != (const ctool_diagnostic_t *)0 &&
        diagnostic->code == expected_code) {
      found = CTOOL_TRUE;
    }
  }
  if (status != expected_status || result_is_zero(&result) == 0 ||
      ctool_buffer_view(output).size != output_before ||
      found == CTOOL_FALSE) {
    (void)fprintf(stderr,
                  "%s: failure contract differs (expected=%s actual=%s "
                  "code=%x found=%d)\n",
                  case_name, ctool_status_name(expected_status),
                  ctool_status_name(status), expected_code, (int)found);
    return 0;
  }
  return 1;
}

static int arena_marks_equal(ctool_arena_mark_t left,
                             ctool_arena_mark_t right) {
  return left.owner == right.owner && left.block == right.block &&
                 left.used == right.used &&
                 left.generation == right.generation
             ? 1
             : 0;
}

static int expect_repeated_failure_recovery(
    const ctool_source_t *unresolved_source,
    const ctool_source_t *valid_source) {
  ctool_host_adapter_t adapter;
  ctool_limits_t limits = ctool_default_limits();
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_buffer_t *output;
  ctool_ld_request_t request;
  ctool_ld_result_t result;
  ctool_arena_mark_t mark;
  ctool_u32 index;
  ctool_status_t status;
  limits.arena_block_bytes = 4096u;
  limits.arena_bytes = 8192u;
  limits.diagnostic_count = 64u;
  status = ctool_host_adapter_init(&adapter, ".");
  if (status != CTOOL_OK) {
    return 0;
  }
  config = ctool_host_job_config(&adapter, limits);
  status = ctool_job_open(&config, &job);
  if (status != CTOOL_OK) {
    return 0;
  }
  status = ctool_job_open_buffer(job, 512u, limits.output_bytes, &output);
  if (status != CTOOL_OK) {
    ctool_job_close(job);
    return 0;
  }
  request.objects = unresolved_source;
  request.object_count = 1u;
  request.layout.kind = CTOOL_LD_LAYOUT_FIXED_TEXT;
  request.layout.as.fixed_text.base_address = 0x00100000u;
  request.layout.as.fixed_text.entry_symbol = ctool_string("_start");
  request.maximum_image_span = 0x10000u;
  mark = ctool_arena_mark(ctool_job_arena(job));
  for (index = 0u; index < 32u; index++) {
    if (!expect_link_failure(job, &request, output, CTOOL_ERR_INPUT,
                             CTOOL_LD_DIAG_UNDEFINED_SYMBOL,
                             "repeated unresolved symbol") ||
        !arena_marks_equal(mark,
                           ctool_arena_mark(ctool_job_arena(job)))) {
      ctool_buffer_close(output);
      ctool_job_close(job);
      return 0;
    }
  }
  for (index = 0u; index < 32u; index++) {
    const ctool_diagnostic_t *diagnostic = ctool_job_diagnostic(job, index);
    if (diagnostic == (const ctool_diagnostic_t *)0 ||
        diagnostic->code != CTOOL_LD_DIAG_UNDEFINED_SYMBOL ||
        strcmp(diagnostic->path.data, "/unresolved.o") != 0 ||
        strcmp(diagnostic->message.data,
               "CupidLD found an unresolved strong symbol") != 0) {
      ctool_buffer_close(output);
      ctool_job_close(job);
      return 0;
    }
  }
  request.objects = valid_source;
  status = ctool_ld_link(job, &request, output, &result);
  if (status != CTOOL_OK || result.entry != 0x00100000u ||
      result.output_section_count != 1u ||
      !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 0;
  }
  ctool_buffer_close(output);
  ctool_job_close(job);
  return 1;
}

static int run_merge_symbols(void) {
  static const ctool_u8 first_text[] = {0xe8u, 0u, 0u, 0u, 0u, 0xc3u};
  static const ctool_u8 second_text[] = {0xa1u, 0u, 0u, 0u, 0u, 0xc3u};
  static const ctool_u8 leading_rodata[] = {0x7fu};
  static const ctool_u8 first_rodata[] = "same\0first";
  static const ctool_u8 second_rodata[] = "same\0second";
  static const ctool_u8 second_data[] = {1u, 2u, 3u, 4u};
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_buffer_t *first_buffer;
  ctool_buffer_t *second_buffer;
  ctool_buffer_t *output;
  ctool_elf32_section_spec_t first_sections[3];
  ctool_elf32_symbol_spec_t first_symbols[6];
  ctool_elf32_relocation_spec_t first_relocation;
  ctool_elf32_object_spec_t first_spec;
  ctool_elf32_section_spec_t second_sections[4];
  ctool_elf32_symbol_spec_t second_symbols[7];
  ctool_elf32_relocation_spec_t second_relocation;
  ctool_elf32_object_spec_t second_spec;
  ctool_source_t sources[2];
  ctool_ld_request_t request;
  ctool_ld_result_t result;
  ctool_source_t executable_source;
  ctool_elf32_object_t executable;
  const ctool_elf32_section_t *linked_text =
      (const ctool_elf32_section_t *)0;
  const ctool_elf32_section_t *linked_rodata =
      (const ctool_elf32_section_t *)0;
  const ctool_elf32_symbol_t *same_first;
  const ctool_elf32_symbol_t *same_second;
  const ctool_elf32_symbol_t *hook;
  const ctool_elf32_symbol_t *storage;
  const ctool_elf32_symbol_t *pool;
  const ctool_elf32_symbol_t *local_two;
  const ctool_elf32_symbol_t *local_stub;
  ctool_status_t status;
  ctool_u32 index;

  if (!open_job(&adapter, &config, &job)) {
    return 1;
  }
  status =
      ctool_job_open_buffer(job, 512u, config.limits.output_bytes,
                            &first_buffer);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 512u, config.limits.output_bytes,
                                   &second_buffer);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 512u, config.limits.output_bytes,
                                   &output);
  }
  if (status != CTOOL_OK) {
    ctool_job_close(job);
    return 1;
  }
  (void)memset(first_sections, 0, sizeof(first_sections));
  first_sections[0].name = ctool_string(".text");
  first_sections[0].type = CTOOL_ELF32_SHT_PROGBITS;
  first_sections[0].flags =
      CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR;
  first_sections[0].alignment = 1u;
  first_sections[0].size = (ctool_u32)sizeof(first_text);
  first_sections[0].contents =
      ctool_bytes(first_text, (ctool_u32)sizeof(first_text));
  first_sections[1].name = ctool_string(".rodata");
  first_sections[1].type = CTOOL_ELF32_SHT_PROGBITS;
  first_sections[1].flags = CTOOL_ELF32_SHF_ALLOC;
  first_sections[1].alignment = 1u;
  first_sections[1].size = (ctool_u32)sizeof(leading_rodata);
  first_sections[1].contents =
      ctool_bytes(leading_rodata, (ctool_u32)sizeof(leading_rodata));
  first_sections[2].name = ctool_string(".rodata.str1.1");
  first_sections[2].type = CTOOL_ELF32_SHT_PROGBITS;
  first_sections[2].flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_MERGE |
                            CTOOL_ELF32_SHF_STRINGS;
  first_sections[2].alignment = 8u;
  first_sections[2].entry_size = 1u;
  first_sections[2].size = (ctool_u32)sizeof(first_rodata);
  first_sections[2].contents =
      ctool_bytes(first_rodata, (ctool_u32)sizeof(first_rodata));
  (void)memset(first_symbols, 0, sizeof(first_symbols));
  first_symbols[0].name = ctool_string("_start");
  first_symbols[0].binding = CTOOL_ELF32_BIND_GLOBAL;
  first_symbols[0].type = CTOOL_ELF32_SYMBOL_FUNCTION;
  first_symbols[0].visibility = CTOOL_ELF32_VIS_DEFAULT;
  first_symbols[0].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  first_symbols[0].section = 0u;
  first_symbols[0].size = (ctool_u32)sizeof(first_text);
  first_symbols[1].name = ctool_string("hook");
  first_symbols[1].binding = CTOOL_ELF32_BIND_WEAK;
  first_symbols[1].type = CTOOL_ELF32_SYMBOL_FUNCTION;
  first_symbols[1].visibility = CTOOL_ELF32_VIS_DEFAULT;
  first_symbols[1].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  first_symbols[1].section = 0u;
  first_symbols[1].value = 5u;
  first_symbols[1].size = 1u;
  first_symbols[2].name = ctool_string("storage");
  first_symbols[2].binding = CTOOL_ELF32_BIND_GLOBAL;
  first_symbols[2].type = CTOOL_ELF32_SYMBOL_OBJECT;
  first_symbols[2].visibility = CTOOL_ELF32_VIS_DEFAULT;
  first_symbols[2].placement = CTOOL_ELF32_SYMBOL_COMMON_STORAGE;
  first_symbols[2].section = CTOOL_ELF32_NO_SECTION;
  first_symbols[2].size = 4u;
  first_symbols[2].alignment = 4u;
  first_symbols[3].name = ctool_string("pool");
  first_symbols[3].binding = CTOOL_ELF32_BIND_GLOBAL;
  first_symbols[3].type = CTOOL_ELF32_SYMBOL_OBJECT;
  first_symbols[3].visibility = CTOOL_ELF32_VIS_DEFAULT;
  first_symbols[3].placement = CTOOL_ELF32_SYMBOL_COMMON_STORAGE;
  first_symbols[3].section = CTOOL_ELF32_NO_SECTION;
  first_symbols[3].size = 8u;
  first_symbols[3].alignment = 8u;
  first_symbols[4].name = ctool_string("same_first");
  first_symbols[4].binding = CTOOL_ELF32_BIND_GLOBAL;
  first_symbols[4].type = CTOOL_ELF32_SYMBOL_OBJECT;
  first_symbols[4].visibility = CTOOL_ELF32_VIS_DEFAULT;
  first_symbols[4].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  first_symbols[4].section = 2u;
  first_symbols[4].size = 5u;
  first_symbols[5].name = ctool_string("hook");
  first_symbols[5].binding = CTOOL_ELF32_BIND_GLOBAL;
  first_symbols[5].type = CTOOL_ELF32_SYMBOL_NOTYPE;
  first_symbols[5].visibility = CTOOL_ELF32_VIS_DEFAULT;
  first_symbols[5].placement = CTOOL_ELF32_SYMBOL_UNDEFINED;
  first_symbols[5].section = CTOOL_ELF32_NO_SECTION;
  first_relocation.target_section = 0u;
  first_relocation.offset = 1u;
  first_relocation.symbol = 5u;
  first_relocation.type = CTOOL_ELF32_R_386_PC32;
  first_relocation.addend = -4;
  first_spec.sections = first_sections;
  first_spec.section_count = 3u;
  first_spec.symbols = first_symbols;
  first_spec.symbol_count = 6u;
  first_spec.relocations = &first_relocation;
  first_spec.relocation_count = 1u;

  (void)memset(second_sections, 0, sizeof(second_sections));
  second_sections[0].name = ctool_string(".text");
  second_sections[0].type = CTOOL_ELF32_SHT_PROGBITS;
  second_sections[0].flags =
      CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR;
  second_sections[0].alignment = 16u;
  second_sections[0].size = (ctool_u32)sizeof(second_text);
  second_sections[0].contents =
      ctool_bytes(second_text, (ctool_u32)sizeof(second_text));
  second_sections[1].name = ctool_string(".rodata.str1.1");
  second_sections[1].type = CTOOL_ELF32_SHT_PROGBITS;
  second_sections[1].flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_MERGE |
                             CTOOL_ELF32_SHF_STRINGS;
  second_sections[1].alignment = 1u;
  second_sections[1].entry_size = 1u;
  second_sections[1].size = (ctool_u32)sizeof(second_rodata);
  second_sections[1].contents =
      ctool_bytes(second_rodata, (ctool_u32)sizeof(second_rodata));
  second_sections[2].name = ctool_string(".data");
  second_sections[2].type = CTOOL_ELF32_SHT_PROGBITS;
  second_sections[2].flags =
      CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE;
  second_sections[2].alignment = 4u;
  second_sections[2].size = (ctool_u32)sizeof(second_data);
  second_sections[2].contents =
      ctool_bytes(second_data, (ctool_u32)sizeof(second_data));
  second_sections[3].name = ctool_string(".text");
  second_sections[3].type = CTOOL_ELF32_SHT_PROGBITS;
  second_sections[3].flags =
      CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR;
  second_sections[3].alignment = 32u;
  second_sections[3].size = 0u;
  second_sections[3].contents = ctool_bytes((const void *)0, 0u);
  (void)memset(second_symbols, 0, sizeof(second_symbols));
  second_symbols[0].name = ctool_string("hook");
  second_symbols[0].binding = CTOOL_ELF32_BIND_GLOBAL;
  second_symbols[0].type = CTOOL_ELF32_SYMBOL_FUNCTION;
  second_symbols[0].visibility = CTOOL_ELF32_VIS_DEFAULT;
  second_symbols[0].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  second_symbols[0].section = 0u;
  second_symbols[0].size = 2u;
  second_symbols[1].name = ctool_string("local_two");
  second_symbols[1].binding = CTOOL_ELF32_BIND_LOCAL;
  second_symbols[1].type = CTOOL_ELF32_SYMBOL_FUNCTION;
  second_symbols[1].visibility = CTOOL_ELF32_VIS_DEFAULT;
  second_symbols[1].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  second_symbols[1].section = 0u;
  second_symbols[1].value = 1u;
  second_symbols[1].size = 1u;
  second_symbols[2].name = ctool_string("storage");
  second_symbols[2].binding = CTOOL_ELF32_BIND_GLOBAL;
  second_symbols[2].type = CTOOL_ELF32_SYMBOL_OBJECT;
  second_symbols[2].visibility = CTOOL_ELF32_VIS_DEFAULT;
  second_symbols[2].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  second_symbols[2].section = 2u;
  second_symbols[2].size = 4u;
  second_symbols[3].name = ctool_string("pool");
  second_symbols[3].binding = CTOOL_ELF32_BIND_WEAK;
  second_symbols[3].type = CTOOL_ELF32_SYMBOL_OBJECT;
  second_symbols[3].visibility = CTOOL_ELF32_VIS_DEFAULT;
  second_symbols[3].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  second_symbols[3].section = 2u;
  second_symbols[3].size = 4u;
  second_symbols[4].name = ctool_string("same_second");
  second_symbols[4].binding = CTOOL_ELF32_BIND_GLOBAL;
  second_symbols[4].type = CTOOL_ELF32_SYMBOL_OBJECT;
  second_symbols[4].visibility = CTOOL_ELF32_VIS_DEFAULT;
  second_symbols[4].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  second_symbols[4].section = 1u;
  second_symbols[4].size = 5u;
  second_symbols[5].name = ctool_string("local_stub");
  second_symbols[5].binding = CTOOL_ELF32_BIND_LOCAL;
  second_symbols[5].type = CTOOL_ELF32_SYMBOL_NOTYPE;
  second_symbols[5].visibility = CTOOL_ELF32_VIS_DEFAULT;
  second_symbols[5].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  second_symbols[5].section = 0u;
  second_symbols[5].value = 0u;
  second_symbols[5].size = 0u;
  second_symbols[6].name = ctool_string("");
  second_symbols[6].binding = CTOOL_ELF32_BIND_LOCAL;
  second_symbols[6].type = CTOOL_ELF32_SYMBOL_SECTION;
  second_symbols[6].visibility = CTOOL_ELF32_VIS_DEFAULT;
  second_symbols[6].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  second_symbols[6].section = 1u;
  second_symbols[6].value = 0u;
  second_symbols[6].size = 0u;
  second_relocation.target_section = 0u;
  second_relocation.offset = 1u;
  second_relocation.symbol = 6u;
  second_relocation.type = CTOOL_ELF32_R_386_32;
  second_relocation.addend = 5;
  second_spec.sections = second_sections;
  second_spec.section_count = 4u;
  second_spec.symbols = second_symbols;
  second_spec.symbol_count = 7u;
  second_spec.relocations = &second_relocation;
  second_spec.relocation_count = 1u;

  status = ctool_elf32_write(job, &first_spec, first_buffer);
  if (status == CTOOL_OK) {
    status = ctool_elf32_write(job, &second_spec, second_buffer);
  }
  if (status != CTOOL_OK) {
    ctool_buffer_close(output);
    ctool_buffer_close(second_buffer);
    ctool_buffer_close(first_buffer);
    ctool_job_close(job);
    return 1;
  }
  sources[0].path.text = ctool_string("/first.o");
  sources[0].contents = ctool_buffer_view(first_buffer);
  sources[1].path.text = ctool_string("/second.o");
  sources[1].contents = ctool_buffer_view(second_buffer);
  request.objects = sources;
  request.object_count = 2u;
  request.layout.kind = CTOOL_LD_LAYOUT_FIXED_TEXT;
  request.layout.as.fixed_text.base_address = 0x00100000u;
  request.layout.as.fixed_text.entry_symbol = ctool_string("_start");
  request.maximum_image_span = 0x00010000u;
  status = ctool_ld_link(job, &request, output, &result);
  executable_source.path.text = ctool_string("/merged.elf");
  executable_source.contents = ctool_buffer_view(output);
  if (status == CTOOL_OK) {
    status = ctool_elf32_read(job, &executable_source, &executable);
  }
  if (status != CTOOL_OK || result.output_section_count != 4u ||
      result.resolved_symbol_count != 8u ||
      result.applied_relocation_count != 2u ||
      result.loaded_end != 0x00102004u ||
      result.memory_end != 0x00103008u) {
    (void)fprintf(stderr, "merge/symbol result differs (status=%s)\n",
                  ctool_status_name(status));
    ctool_buffer_close(output);
    ctool_buffer_close(second_buffer);
    ctool_buffer_close(first_buffer);
    ctool_job_close(job);
    return 1;
  }
  for (index = 1u; index < executable.section_count; index++) {
    if (executable.sections[index].name.size == 5u &&
        memcmp(executable.sections[index].name.data, ".text", 5u) == 0) {
      linked_text = &executable.sections[index];
    } else if (executable.sections[index].name.size == 7u &&
               memcmp(executable.sections[index].name.data, ".rodata", 7u) ==
                   0) {
      linked_rodata = &executable.sections[index];
    }
  }
  same_first = find_linked_symbol(&executable, "same_first");
  same_second = find_linked_symbol(&executable, "same_second");
  hook = find_linked_symbol(&executable, "hook");
  storage = find_linked_symbol(&executable, "storage");
  pool = find_linked_symbol(&executable, "pool");
  local_two = find_linked_symbol(&executable, "local_two");
  local_stub = find_linked_symbol(&executable, "local_stub");
  if (linked_text == (const ctool_elf32_section_t *)0 ||
      linked_rodata == (const ctool_elf32_section_t *)0 ||
      linked_text->size != 32u ||
      read_le32(linked_text->contents.data, 1u) != 11u ||
      read_le32(linked_text->contents.data, 6u) != 0xccccccccu ||
      read_le32(linked_text->contents.data, 10u) != 0xccccccccu ||
      linked_text->contents.data[14] != 0xccu ||
      linked_text->contents.data[15] != 0xccu ||
      read_le32(linked_text->contents.data, 17u) != 0x00101013u ||
      read_le32(linked_text->contents.data, 22u) != 0xccccccccu ||
      read_le32(linked_text->contents.data, 26u) != 0xccccccccu ||
      linked_text->contents.data[30] != 0xccu ||
      linked_text->contents.data[31] != 0xccu ||
      linked_rodata->size != 26u ||
      (linked_rodata->flags &
       (CTOOL_ELF32_SHF_MERGE | CTOOL_ELF32_SHF_STRINGS)) != 0u ||
      linked_rodata->entry_size != 0u ||
      linked_rodata->contents.data[0] != 0x7fu ||
      read_le32(linked_rodata->contents.data, 1u) != 0u ||
      linked_rodata->contents.data[5] != 0u ||
      linked_rodata->contents.data[6] != 0u ||
      linked_rodata->contents.data[7] != 0u ||
      same_first == (const ctool_elf32_symbol_t *)0 ||
      same_second == (const ctool_elf32_symbol_t *)0 ||
      same_first->value != same_second->value ||
      same_first->value != 0x00101008u ||
      hook == (const ctool_elf32_symbol_t *)0 || hook->value != 0x00100010u ||
      hook->binding != CTOOL_ELF32_BIND_GLOBAL ||
      storage == (const ctool_elf32_symbol_t *)0 ||
      storage->value != 0x00102000u ||
      pool == (const ctool_elf32_symbol_t *)0 || pool->value != 0x00103000u ||
      local_two == (const ctool_elf32_symbol_t *)0 ||
      local_two->binding != CTOOL_ELF32_BIND_LOCAL ||
      local_two->value != 0x00100011u ||
      local_stub == (const ctool_elf32_symbol_t *)0 ||
      local_stub->binding != CTOOL_ELF32_BIND_LOCAL ||
      local_stub->type != CTOOL_ELF32_SYMBOL_NOTYPE ||
      local_stub->value != 0x00100010u) {
    (void)fprintf(stderr, "merge/symbol linked evidence differs\n");
    ctool_buffer_close(output);
    ctool_buffer_close(second_buffer);
    ctool_buffer_close(first_buffer);
    ctool_job_close(job);
    return 1;
  }
  ctool_buffer_close(output);
  ctool_buffer_close(second_buffer);
  ctool_buffer_close(first_buffer);
  ctool_job_close(job);
  (void)puts("merge-symbols: ok");
  return 0;
}

static int run_merge_addends(void) {
  static const ctool_u8 text[4] = {0u, 0u, 0u, 0u};
  static const ctool_u8 relocated_constants[8] = {0u, 0u, 0u, 0u,
                                                  0u, 0u, 0u, 0u};
  static const ctool_u8 first_constants[32] = {
      0x33u, 0x33u, 0x33u, 0x33u, 0x33u, 0x33u, 0x33u, 0x33u,
      0x33u, 0x33u, 0x33u, 0x33u, 0x33u, 0x33u, 0x33u, 0x33u,
      0x22u, 0x22u, 0x22u, 0x22u, 0x22u, 0x22u, 0x22u, 0x22u,
      0x22u, 0x22u, 0x22u, 0x22u, 0x22u, 0x22u, 0x22u, 0x22u};
  static const ctool_u8 second_constants[32] = {
      0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u,
      0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u,
      0x22u, 0x22u, 0x22u, 0x22u, 0x22u, 0x22u, 0x22u, 0x22u,
      0x22u, 0x22u, 0x22u, 0x22u, 0x22u, 0x22u, 0x22u, 0x22u};
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_buffer_t *object_buffer;
  ctool_buffer_t *output;
  ctool_elf32_section_spec_t sections[4];
  ctool_elf32_symbol_spec_t symbols[4];
  ctool_elf32_relocation_spec_t relocations[3];
  ctool_elf32_object_spec_t object_spec;
  ctool_source_t object_source;
  ctool_ld_request_t request;
  ctool_ld_result_t result;
  ctool_source_t executable_source;
  ctool_elf32_object_t executable;
  const ctool_elf32_section_t *linked_text =
      (const ctool_elf32_section_t *)0;
  const ctool_elf32_section_t *linked_rodata =
      (const ctool_elf32_section_t *)0;
  ctool_status_t status;
  ctool_u32 index;

  if (!open_job(&adapter, &config, &job)) {
    return 1;
  }
  status = ctool_job_open_buffer(job, 512u, config.limits.output_bytes,
                                 &object_buffer);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 512u, config.limits.output_bytes,
                                   &output);
  }
  if (status != CTOOL_OK) {
    ctool_job_close(job);
    return 1;
  }

  (void)memset(sections, 0, sizeof(sections));
  sections[0].name = ctool_string(".text");
  sections[0].type = CTOOL_ELF32_SHT_PROGBITS;
  sections[0].flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR;
  sections[0].alignment = 4u;
  sections[0].size = (ctool_u32)sizeof(text);
  sections[0].contents = ctool_bytes(text, (ctool_u32)sizeof(text));
  sections[1].name = ctool_string(".rodata.cst16");
  sections[1].type = CTOOL_ELF32_SHT_PROGBITS;
  sections[1].flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_MERGE;
  sections[1].alignment = 16u;
  sections[1].entry_size = 16u;
  sections[1].size = (ctool_u32)sizeof(first_constants);
  sections[1].contents =
      ctool_bytes(first_constants, (ctool_u32)sizeof(first_constants));
  sections[2].name = ctool_string(".rodata.cst16");
  sections[2].type = CTOOL_ELF32_SHT_PROGBITS;
  sections[2].flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_MERGE;
  sections[2].alignment = 16u;
  sections[2].entry_size = 16u;
  sections[2].size = (ctool_u32)sizeof(second_constants);
  sections[2].contents =
      ctool_bytes(second_constants, (ctool_u32)sizeof(second_constants));
  sections[3].name = ctool_string(".rodata.cst4");
  sections[3].type = CTOOL_ELF32_SHT_PROGBITS;
  sections[3].flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_MERGE;
  sections[3].alignment = 4u;
  sections[3].entry_size = 4u;
  sections[3].size = (ctool_u32)sizeof(relocated_constants);
  sections[3].contents = ctool_bytes(
      relocated_constants, (ctool_u32)sizeof(relocated_constants));

  (void)memset(symbols, 0, sizeof(symbols));
  symbols[0].name = ctool_string("_start");
  symbols[0].binding = CTOOL_ELF32_BIND_GLOBAL;
  symbols[0].type = CTOOL_ELF32_SYMBOL_FUNCTION;
  symbols[0].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[0].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  symbols[0].section = 0u;
  symbols[0].size = (ctool_u32)sizeof(text);
  symbols[1].name = ctool_string("table");
  symbols[1].binding = CTOOL_ELF32_BIND_LOCAL;
  symbols[1].type = CTOOL_ELF32_SYMBOL_OBJECT;
  symbols[1].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[1].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  symbols[1].section = 2u;
  symbols[1].value = 16u;
  symbols[1].size = 16u;
  symbols[2].name = ctool_string("first_absolute");
  symbols[2].binding = CTOOL_ELF32_BIND_LOCAL;
  symbols[2].type = CTOOL_ELF32_SYMBOL_NOTYPE;
  symbols[2].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[2].placement = CTOOL_ELF32_SYMBOL_ABSOLUTE;
  symbols[2].section = CTOOL_ELF32_NO_SECTION;
  symbols[2].value = 0x11223344u;
  symbols[3].name = ctool_string("second_absolute");
  symbols[3].binding = CTOOL_ELF32_BIND_LOCAL;
  symbols[3].type = CTOOL_ELF32_SYMBOL_NOTYPE;
  symbols[3].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[3].placement = CTOOL_ELF32_SYMBOL_ABSOLUTE;
  symbols[3].section = CTOOL_ELF32_NO_SECTION;
  symbols[3].value = 0x55667788u;

  relocations[0].target_section = 0u;
  relocations[0].offset = 0u;
  relocations[0].symbol = 1u;
  relocations[0].type = CTOOL_ELF32_R_386_32;
  relocations[0].addend = -4;
  relocations[1].target_section = 3u;
  relocations[1].offset = 0u;
  relocations[1].symbol = 2u;
  relocations[1].type = CTOOL_ELF32_R_386_32;
  relocations[1].addend = 0;
  relocations[2].target_section = 3u;
  relocations[2].offset = 4u;
  relocations[2].symbol = 3u;
  relocations[2].type = CTOOL_ELF32_R_386_32;
  relocations[2].addend = 0;
  object_spec.sections = sections;
  object_spec.section_count = 4u;
  object_spec.symbols = symbols;
  object_spec.symbol_count = 4u;
  object_spec.relocations = relocations;
  object_spec.relocation_count = 3u;
  status = ctool_elf32_write(job, &object_spec, object_buffer);
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "merge-addend fixture write failed: %s\n",
                  ctool_status_name(status));
    ctool_buffer_close(output);
    ctool_buffer_close(object_buffer);
    ctool_job_close(job);
    return 1;
  }

  object_source.path.text = ctool_string("/merge-addend.o");
  object_source.contents = ctool_buffer_view(object_buffer);
  request.objects = &object_source;
  request.object_count = 1u;
  request.layout.kind = CTOOL_LD_LAYOUT_FIXED_TEXT;
  request.layout.as.fixed_text.base_address = 0x00100000u;
  request.layout.as.fixed_text.entry_symbol = ctool_string("_start");
  request.maximum_image_span = 0x00010000u;
  status = ctool_ld_link(job, &request, output, &result);
  executable_source.path.text = ctool_string("/merge-addend.elf");
  executable_source.contents = ctool_buffer_view(output);
  if (status == CTOOL_OK) {
    status = ctool_elf32_read(job, &executable_source, &executable);
  }
  if (status != CTOOL_OK || result.applied_relocation_count != 3u) {
    (void)fprintf(stderr, "merge-addend link failed: %s\n",
                  ctool_status_name(status));
    ctool_buffer_close(output);
    ctool_buffer_close(object_buffer);
    ctool_job_close(job);
    return 1;
  }
  for (index = 1u; index < executable.section_count; index++) {
    const ctool_elf32_section_t *section = &executable.sections[index];
    if (section->name.size == 5u &&
        memcmp(section->name.data, ".text", 5u) == 0) {
      linked_text = section;
    } else if (section->name.size == 7u &&
               memcmp(section->name.data, ".rodata", 7u) == 0) {
      linked_rodata = section;
    }
  }
  if (linked_text == (const ctool_elf32_section_t *)0 ||
      linked_rodata == (const ctool_elf32_section_t *)0 ||
      linked_text->size != 4u ||
      linked_rodata->size != 56u ||
      linked_rodata->contents.data[0] != 0x33u ||
      linked_rodata->contents.data[16] != 0x22u ||
      linked_rodata->contents.data[32] != 0x11u ||
      read_le32(linked_rodata->contents.data, 48u) != 0x11223344u ||
      read_le32(linked_rodata->contents.data, 52u) != 0x55667788u ||
      read_le32(linked_text->contents.data, 0u) !=
          linked_rodata->address + 12u) {
    (void)fprintf(stderr,
                  "merge-addend relocation did not apply mapped(symbol) + "
                  "addend\n");
    ctool_buffer_close(output);
    ctool_buffer_close(object_buffer);
    ctool_job_close(job);
    return 1;
  }

  ctool_buffer_close(output);
  ctool_buffer_close(object_buffer);
  ctool_job_close(job);
  (void)puts("merge-addends: ok");
  return 0;
}

static int run_script_layout(void) {
  static const ctool_u8 text_start[] = {0x90u};
  static const ctool_u8 text[] = {0xc3u};
  static const ctool_u8 rodata[] = {1u, 2u};
  static const ctool_u8 strings[] = "x";
  static const ctool_u8 data[] = {3u, 4u, 5u, 6u};
  static const ctool_u8 ksyms[] = {7u, 8u, 9u};
  static const char script_text[] =
      "/* Cupid OS kernel layout */\n"
      "ENTRY(_start)\n"
      "SECTIONS\n"
      "{\n"
      "  . = 0x100000;\n"
      "  .text : { *(.text.start) *(.text) }\n"
      "  .rodata : { *(.rodata) *(.rodata.*) }\n"
      "  .data : { *(.data) }\n"
      "  .ksyms ALIGN(4) : {\n"
      "    __ksyms_start = .; *(.ksyms) __ksyms_end = .;\n"
      "  }\n"
      "  _loaded_end = .;\n"
      "  . = ALIGN(4096);\n"
      "  . = 0x101003;\n"
      "  _bss_start = .;\n"
      "  .bss : { *(.bss) *(COMMON) }\n"
      "  _kernel_end = .;\n"
      "  ASSERT(_loaded_end <= 0x8ff600, \"kernel image too large\")\n"
      "  ASSERT(_kernel_end <= 0x110000, \"kernel stack overlap\")\n"
      "}\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_buffer_t *object_buffer;
  ctool_buffer_t *output;
  ctool_elf32_section_spec_t sections[7];
  ctool_elf32_symbol_spec_t symbols[2];
  ctool_elf32_object_spec_t object_spec;
  ctool_source_t object_source;
  ctool_source_t script_source;
  ctool_ld_request_t request;
  ctool_ld_result_t result;
  ctool_source_t executable_source;
  ctool_elf32_object_t executable;
  const ctool_elf32_section_t *linked_ksyms =
      (const ctool_elf32_section_t *)0;
  const ctool_elf32_section_t *linked_bss =
      (const ctool_elf32_section_t *)0;
  const ctool_elf32_symbol_t *ksyms_start;
  const ctool_elf32_symbol_t *ksyms_end;
  const ctool_elf32_symbol_t *loaded_end;
  const ctool_elf32_symbol_t *bss_start;
  const ctool_elf32_symbol_t *kernel_end;
  const ctool_elf32_symbol_t *pool;
  ctool_status_t status;
  ctool_u32 section_index;

  if (!open_job(&adapter, &config, &job)) {
    return 1;
  }
  status =
      ctool_job_open_buffer(job, 512u, config.limits.output_bytes,
                            &object_buffer);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 512u, config.limits.output_bytes,
                                   &output);
  }
  if (status != CTOOL_OK) {
    ctool_job_close(job);
    return 1;
  }
  (void)memset(sections, 0, sizeof(sections));
  sections[0].name = ctool_string(".text.start");
  sections[0].type = CTOOL_ELF32_SHT_PROGBITS;
  sections[0].flags =
      CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR;
  sections[0].alignment = 1u;
  sections[0].size = (ctool_u32)sizeof(text_start);
  sections[0].contents =
      ctool_bytes(text_start, (ctool_u32)sizeof(text_start));
  sections[1].name = ctool_string(".text");
  sections[1].type = CTOOL_ELF32_SHT_PROGBITS;
  sections[1].flags =
      CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR;
  sections[1].alignment = 1u;
  sections[1].size = (ctool_u32)sizeof(text);
  sections[1].contents = ctool_bytes(text, (ctool_u32)sizeof(text));
  sections[2].name = ctool_string(".rodata");
  sections[2].type = CTOOL_ELF32_SHT_PROGBITS;
  sections[2].flags = CTOOL_ELF32_SHF_ALLOC;
  sections[2].alignment = 1u;
  sections[2].size = (ctool_u32)sizeof(rodata);
  sections[2].contents = ctool_bytes(rodata, (ctool_u32)sizeof(rodata));
  sections[3].name = ctool_string(".rodata.str1.1");
  sections[3].type = CTOOL_ELF32_SHT_PROGBITS;
  sections[3].flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_MERGE |
                      CTOOL_ELF32_SHF_STRINGS;
  sections[3].alignment = 16u;
  sections[3].entry_size = 1u;
  sections[3].size = (ctool_u32)sizeof(strings);
  sections[3].contents = ctool_bytes(strings, (ctool_u32)sizeof(strings));
  sections[4].name = ctool_string(".data");
  sections[4].type = CTOOL_ELF32_SHT_PROGBITS;
  sections[4].flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE;
  sections[4].alignment = 1u;
  sections[4].size = (ctool_u32)sizeof(data);
  sections[4].contents = ctool_bytes(data, (ctool_u32)sizeof(data));
  sections[5].name = ctool_string(".ksyms");
  sections[5].type = CTOOL_ELF32_SHT_PROGBITS;
  sections[5].flags = CTOOL_ELF32_SHF_ALLOC;
  sections[5].alignment = 1u;
  sections[5].size = (ctool_u32)sizeof(ksyms);
  sections[5].contents = ctool_bytes(ksyms, (ctool_u32)sizeof(ksyms));
  sections[6].name = ctool_string(".bss");
  sections[6].type = CTOOL_ELF32_SHT_NOBITS;
  sections[6].flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE;
  sections[6].alignment = 1u;
  sections[6].size = 5u;
  sections[6].contents = ctool_bytes((const void *)0, 0u);
  (void)memset(symbols, 0, sizeof(symbols));
  symbols[0].name = ctool_string("_start");
  symbols[0].binding = CTOOL_ELF32_BIND_GLOBAL;
  symbols[0].type = CTOOL_ELF32_SYMBOL_FUNCTION;
  symbols[0].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[0].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  symbols[0].section = 0u;
  symbols[0].size = 1u;
  symbols[1].name = ctool_string("pool");
  symbols[1].binding = CTOOL_ELF32_BIND_GLOBAL;
  symbols[1].type = CTOOL_ELF32_SYMBOL_OBJECT;
  symbols[1].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[1].placement = CTOOL_ELF32_SYMBOL_COMMON_STORAGE;
  symbols[1].section = CTOOL_ELF32_NO_SECTION;
  symbols[1].size = 8u;
  symbols[1].alignment = 8u;
  object_spec.sections = sections;
  object_spec.section_count = 7u;
  object_spec.symbols = symbols;
  object_spec.symbol_count = 2u;
  object_spec.relocations = (const ctool_elf32_relocation_spec_t *)0;
  object_spec.relocation_count = 0u;
  status = ctool_elf32_write(job, &object_spec, object_buffer);
  if (status != CTOOL_OK) {
    ctool_buffer_close(output);
    ctool_buffer_close(object_buffer);
    ctool_job_close(job);
    return 1;
  }
  object_source.path.text = ctool_string("/kernel.o");
  object_source.contents = ctool_buffer_view(object_buffer);
  script_source.path.text = ctool_string("/link.ld");
  script_source.contents =
      ctool_bytes(script_text, (ctool_u32)(sizeof(script_text) - 1u));
  request.objects = &object_source;
  request.object_count = 1u;
  request.layout.kind = CTOOL_LD_LAYOUT_SCRIPT;
  request.layout.as.script = &script_source;
  request.maximum_image_span = 0x00800000u;
  status = ctool_ld_link(job, &request, output, &result);
  executable_source.path.text = ctool_string("/kernel.elf");
  executable_source.contents = ctool_buffer_view(output);
  if (status == CTOOL_OK) {
    status = ctool_elf32_read(job, &executable_source, &executable);
  }
  if (status != CTOOL_OK || result.entry != 0x00100000u ||
      result.load_address != 0x00100000u ||
      result.loaded_end != 0x0010002bu ||
      result.memory_end != 0x00101010u || result.output_section_count != 5u ||
      result.resolved_symbol_count != 7u ||
      executable.program_header_count != 6u) {
    (void)fprintf(stderr,
                  "script layout result differs (status=%s entry=%x load=%x "
                  "loaded=%x memory=%x sections=%u symbols=%u phdrs=%u)\n",
                  ctool_status_name(status), result.entry,
                  result.load_address, result.loaded_end, result.memory_end,
                  result.output_section_count, result.resolved_symbol_count,
                  executable.program_header_count);
    ctool_buffer_close(output);
    ctool_buffer_close(object_buffer);
    ctool_job_close(job);
    return 1;
  }
  for (section_index = 1u; section_index < executable.section_count;
       section_index++) {
    if (executable.sections[section_index].name.size == 6u &&
        memcmp(executable.sections[section_index].name.data, ".ksyms", 6u) ==
            0) {
      linked_ksyms = &executable.sections[section_index];
    } else if (executable.sections[section_index].name.size == 4u &&
               memcmp(executable.sections[section_index].name.data, ".bss",
                      4u) == 0) {
      linked_bss = &executable.sections[section_index];
    }
  }
  ksyms_start = find_linked_symbol(&executable, "__ksyms_start");
  ksyms_end = find_linked_symbol(&executable, "__ksyms_end");
  loaded_end = find_linked_symbol(&executable, "_loaded_end");
  bss_start = find_linked_symbol(&executable, "_bss_start");
  kernel_end = find_linked_symbol(&executable, "_kernel_end");
  pool = find_linked_symbol(&executable, "pool");
  if (ksyms_start == (const ctool_elf32_symbol_t *)0 ||
      ksyms_start->value != 0x00100028u ||
      ksyms_end == (const ctool_elf32_symbol_t *)0 ||
      ksyms_end->value != 0x0010002bu ||
      loaded_end == (const ctool_elf32_symbol_t *)0 ||
      loaded_end->value != 0x0010002bu ||
      bss_start == (const ctool_elf32_symbol_t *)0 ||
      bss_start->value != 0x00101003u ||
      linked_ksyms == (const ctool_elf32_section_t *)0 ||
      bss_start->section_file_index != linked_ksyms->file_index ||
      kernel_end == (const ctool_elf32_symbol_t *)0 ||
      kernel_end->value != 0x00101010u ||
      linked_bss == (const ctool_elf32_section_t *)0 ||
      linked_bss->address != 0x00101003u || linked_bss->size != 13u ||
      linked_bss->alignment != 1u ||
      pool == (const ctool_elf32_symbol_t *)0 ||
      pool->value != 0x00101008u || pool->value % 8u != 0u ||
      kernel_end->section_file_index != linked_bss->file_index) {
    (void)fprintf(
        stderr,
        "script symbol values differ: kstart=%x kend=%x loaded=%x "
        "bstart=%x kernel=%x\n",
        ksyms_start != (const ctool_elf32_symbol_t *)0 ? ksyms_start->value
                                                       : 0u,
        ksyms_end != (const ctool_elf32_symbol_t *)0 ? ksyms_end->value : 0u,
        loaded_end != (const ctool_elf32_symbol_t *)0 ? loaded_end->value : 0u,
        bss_start != (const ctool_elf32_symbol_t *)0 ? bss_start->value : 0u,
        kernel_end != (const ctool_elf32_symbol_t *)0 ? kernel_end->value : 0u);
    ctool_buffer_close(output);
    ctool_buffer_close(object_buffer);
    ctool_job_close(job);
    return 1;
  }
  ctool_buffer_close(output);
  ctool_buffer_close(object_buffer);
  ctool_job_close(job);
  (void)puts("script-layout: ok");
  return 0;
}

static int run_errors(void) {
  static const ctool_u8 text[] = {0u, 0u, 0u, 0u, 0xc3u};
  static const char unmatched_text[] =
      "ENTRY(_start) SECTIONS { . = 0x100000; .other : { *(.other) } }";
  static const char backward_text[] =
      "ENTRY(_start) SECTIONS { . = 0x100000; . = 0; "
      ".text : { *(.text) } }";
  static const char assert_text[] =
      "ENTRY(_start) SECTIONS { . = 0x100000; .text : { *(.text) } "
      "ASSERT(. <= 0x200000, \"first passes\") "
      "ASSERT(. <= 0, \"second fails\") }";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_buffer_t *valid_buffer;
  ctool_buffer_t *unresolved_buffer;
  ctool_buffer_t *relocation_buffer;
  ctool_buffer_t *output;
  ctool_elf32_section_spec_t section;
  ctool_elf32_symbol_spec_t symbols[2];
  ctool_elf32_relocation_spec_t relocation;
  ctool_elf32_object_spec_t spec;
  ctool_source_t valid_sources[2];
  ctool_source_t unresolved_source;
  ctool_source_t unsupported_source;
  ctool_source_t relocation_source;
  ctool_source_t script_source;
  ctool_ld_request_t request;
  ctool_bytes_t valid_image;
  ctool_bytes_t relocation_image;
  ctool_u8 *unsupported_copy;
  ctool_u8 *relocation_copy;
  ctool_u32 text_index;
  ctool_u32 text_header;
  ctool_u32 text_flags;
  ctool_u32 rel_index;
  ctool_u32 rel_header;
  ctool_u32 rel_offset;
  ctool_u32 rel_info;
  ctool_u32 assertion_diagnostics_before;
  ctool_u32 diagnostic_index;
  ctool_bool assertion_message_found = CTOOL_FALSE;
  ctool_status_t status;
  int ok = 1;

  if (!open_job(&adapter, &config, &job)) {
    return 1;
  }
  status =
      ctool_job_open_buffer(job, 512u, config.limits.output_bytes,
                            &valid_buffer);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 512u, config.limits.output_bytes,
                                   &unresolved_buffer);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 512u, config.limits.output_bytes,
                                   &relocation_buffer);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 512u, config.limits.output_bytes,
                                   &output);
  }
  if (status != CTOOL_OK) {
    ctool_job_close(job);
    return 1;
  }
  (void)memset(&section, 0, sizeof(section));
  section.name = ctool_string(".text");
  section.type = CTOOL_ELF32_SHT_PROGBITS;
  section.flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR;
  section.alignment = 1u;
  section.size = (ctool_u32)sizeof(text);
  section.contents = ctool_bytes(text, (ctool_u32)sizeof(text));
  (void)memset(symbols, 0, sizeof(symbols));
  symbols[0].name = ctool_string("_start");
  symbols[0].binding = CTOOL_ELF32_BIND_GLOBAL;
  symbols[0].type = CTOOL_ELF32_SYMBOL_FUNCTION;
  symbols[0].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[0].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  symbols[0].section = 0u;
  symbols[0].size = (ctool_u32)sizeof(text);
  spec.sections = &section;
  spec.section_count = 1u;
  spec.symbols = symbols;
  spec.symbol_count = 1u;
  spec.relocations = (const ctool_elf32_relocation_spec_t *)0;
  spec.relocation_count = 0u;
  status = ctool_elf32_write(job, &spec, valid_buffer);
  symbols[1].name = ctool_string("missing");
  symbols[1].binding = CTOOL_ELF32_BIND_GLOBAL;
  symbols[1].type = CTOOL_ELF32_SYMBOL_NOTYPE;
  symbols[1].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[1].placement = CTOOL_ELF32_SYMBOL_UNDEFINED;
  symbols[1].section = CTOOL_ELF32_NO_SECTION;
  spec.symbol_count = 2u;
  if (status == CTOOL_OK) {
    status = ctool_elf32_write(job, &spec, unresolved_buffer);
  }
  spec.symbol_count = 1u;
  relocation.target_section = 0u;
  relocation.offset = 0u;
  relocation.symbol = 0u;
  relocation.type = CTOOL_ELF32_R_386_32;
  relocation.addend = 0;
  spec.relocations = &relocation;
  spec.relocation_count = 1u;
  if (status == CTOOL_OK) {
    status = ctool_elf32_write(job, &spec, relocation_buffer);
  }
  if (status != CTOOL_OK) {
    ctool_buffer_close(output);
    ctool_buffer_close(relocation_buffer);
    ctool_buffer_close(unresolved_buffer);
    ctool_buffer_close(valid_buffer);
    ctool_job_close(job);
    return 1;
  }
  valid_sources[0].path.text = ctool_string("/valid.o");
  valid_sources[0].contents = ctool_buffer_view(valid_buffer);
  valid_sources[1] = valid_sources[0];
  valid_sources[1].path.text = ctool_string("/duplicate.o");
  unresolved_source.path.text = ctool_string("/unresolved.o");
  unresolved_source.contents = ctool_buffer_view(unresolved_buffer);
  request.objects = valid_sources;
  request.object_count = 2u;
  request.layout.kind = CTOOL_LD_LAYOUT_FIXED_TEXT;
  request.layout.as.fixed_text.base_address = 0x00100000u;
  request.layout.as.fixed_text.entry_symbol = ctool_string("_start");
  request.maximum_image_span = 0x10000u;
  ok &= expect_link_failure(job, &request, output, CTOOL_ERR_INPUT,
                            CTOOL_LD_DIAG_DUPLICATE_SYMBOL,
                            "duplicate strong");
  request.objects = &unresolved_source;
  request.object_count = 1u;
  ok &= expect_link_failure(job, &request, output, CTOOL_ERR_INPUT,
                            CTOOL_LD_DIAG_UNDEFINED_SYMBOL,
                            "unresolved strong");
  request.objects = valid_sources;
  request.object_count = 2u;
  script_source.path.text = ctool_string("/unmatched.ld");
  script_source.contents =
      ctool_bytes(unmatched_text,
                  (ctool_u32)(sizeof(unmatched_text) - 1u));
  request.layout.kind = CTOOL_LD_LAYOUT_SCRIPT;
  request.layout.as.script = &script_source;
  ok &= expect_link_failure(job, &request, output, CTOOL_ERR_INPUT,
                            CTOOL_LD_DIAG_DUPLICATE_SYMBOL,
                            "script duplicate input rejected first");
  request.object_count = 1u;
  ok &= expect_link_failure(job, &request, output, CTOOL_ERR_INPUT,
                            CTOOL_LD_DIAG_UNMATCHED_SECTION,
                            "unmatched allocated section");
  script_source.path.text = ctool_string("/backward.ld");
  script_source.contents =
      ctool_bytes(backward_text,
                  (ctool_u32)(sizeof(backward_text) - 1u));
  ok &= expect_link_failure(job, &request, output, CTOOL_ERR_INPUT,
                            CTOOL_LD_DIAG_BACKWARD_DOT, "backward dot");
  script_source.path.text = ctool_string("/assert.ld");
  script_source.contents =
      ctool_bytes(assert_text, (ctool_u32)(sizeof(assert_text) - 1u));
  assertion_diagnostics_before = ctool_job_diagnostic_count(job);
  ok &= expect_link_failure(job, &request, output, CTOOL_ERR_INPUT,
                            CTOOL_LD_DIAG_ASSERTION_FAILED,
                            "failed assertion");
  for (diagnostic_index = assertion_diagnostics_before;
       diagnostic_index < ctool_job_diagnostic_count(job);
       diagnostic_index++) {
    const ctool_diagnostic_t *diagnostic =
        ctool_job_diagnostic(job, diagnostic_index);
    if (diagnostic != (const ctool_diagnostic_t *)0 &&
        diagnostic->code == CTOOL_LD_DIAG_ASSERTION_FAILED &&
        strcmp(diagnostic->message.data, "second fails") == 0) {
      assertion_message_found = CTOOL_TRUE;
    }
  }
  if (assertion_message_found == CTOOL_FALSE) {
    (void)fprintf(stderr, "failed assertion: script message was discarded\n");
    ok = 0;
  }
  request.layout.kind = CTOOL_LD_LAYOUT_FIXED_TEXT;
  request.layout.as.fixed_text.base_address = 0xffffffffu;
  request.layout.as.fixed_text.entry_symbol = ctool_string("_start");
  ok &= expect_link_failure(job, &request, output, CTOOL_ERR_OVERFLOW,
                            CTOOL_LD_DIAG_OVERFLOW, "address overflow");
  request.layout.as.fixed_text.base_address = 0x00100000u;
  request.layout.as.fixed_text.entry_symbol = ctool_string("absent");
  ok &= expect_link_failure(job, &request, output, CTOOL_ERR_INPUT,
                            CTOOL_LD_DIAG_BAD_ENTRY, "missing entry");
  request.layout.as.fixed_text.entry_symbol = ctool_string("_start");
  request.maximum_image_span = 4u;
  ok &= expect_link_failure(job, &request, output, CTOOL_ERR_LIMIT,
                            CTOOL_LD_DIAG_LIMIT, "maximum span");
  request.maximum_image_span = 0x10000u;
  unsupported_copy =
      (ctool_u8 *)malloc((size_t)valid_sources[0].contents.size);
  valid_image = valid_sources[0].contents;
  relocation_image = ctool_buffer_view(relocation_buffer);
  relocation_copy = (ctool_u8 *)malloc((size_t)relocation_image.size);
  if (unsupported_copy == (ctool_u8 *)0 ||
      relocation_copy == (ctool_u8 *)0) {
    free(relocation_copy);
    free(unsupported_copy);
    ctool_buffer_close(output);
    ctool_buffer_close(relocation_buffer);
    ctool_buffer_close(unresolved_buffer);
    ctool_buffer_close(valid_buffer);
    ctool_job_close(job);
    return 1;
  }
  text_index = find_raw_section(valid_image, ".text");
  text_header = read_le32(valid_image.data, 32u) + text_index * 40u;
  text_flags = read_le32(valid_image.data, text_header + 8u);
  unsupported_source.path.text = ctool_string("/init-array.o");
  unsupported_source.contents =
      ctool_bytes(unsupported_copy, valid_sources[0].contents.size);
  request.objects = &unsupported_source;
  (void)memcpy(unsupported_copy, valid_image.data, (size_t)valid_image.size);
  write_le32(unsupported_copy, text_header + 4u, 14u);
  if (!expect_link_failure(job, &request, output, CTOOL_ERR_UNSUPPORTED,
                           CTOOL_LD_DIAG_UNSUPPORTED_INPUT,
                           "allocated INIT_ARRAY section")) {
    ok = 0;
    ctool_buffer_clear(output);
  }
  (void)memcpy(unsupported_copy, valid_image.data, (size_t)valid_image.size);
  write_le32(unsupported_copy, text_header + 4u, 15u);
  unsupported_source.path.text = ctool_string("/fini-array.o");
  if (!expect_link_failure(job, &request, output, CTOOL_ERR_UNSUPPORTED,
                           CTOOL_LD_DIAG_UNSUPPORTED_INPUT,
                           "allocated FINI_ARRAY section")) {
    ok = 0;
    ctool_buffer_clear(output);
  }
  (void)memcpy(unsupported_copy, valid_image.data, (size_t)valid_image.size);
  write_le32(unsupported_copy, text_header + 8u,
             text_flags | CTOOL_ELF32_SHF_TLS);
  unsupported_source.path.text = ctool_string("/tls-section.o");
  if (!expect_link_failure(job, &request, output, CTOOL_ERR_UNSUPPORTED,
                           CTOOL_LD_DIAG_UNSUPPORTED_INPUT,
                           "allocated TLS section")) {
    ok = 0;
    ctool_buffer_clear(output);
  }
  (void)memcpy(unsupported_copy, valid_image.data, (size_t)valid_image.size);
  write_le32(unsupported_copy, text_header + 8u,
             text_flags | CTOOL_ELF32_SHF_EXCLUDE);
  unsupported_source.path.text = ctool_string("/excluded-section.o");
  if (!expect_link_failure(job, &request, output, CTOOL_ERR_UNSUPPORTED,
                           CTOOL_LD_DIAG_UNSUPPORTED_INPUT,
                           "allocated EXCLUDE section")) {
    ok = 0;
    ctool_buffer_clear(output);
  }
  (void)memcpy(unsupported_copy, valid_image.data, (size_t)valid_image.size);
  write_le16(unsupported_copy, 16u, 2u);
  unsupported_source.path.text = ctool_string("/executable.o");
  unsupported_source.contents =
      ctool_bytes(unsupported_copy, valid_sources[0].contents.size);
  request.objects = &unsupported_source;
  ok &= expect_link_failure(job, &request, output, CTOOL_ERR_UNSUPPORTED,
                            CTOOL_LD_DIAG_UNSUPPORTED_INPUT,
                            "unsupported input domain");
  (void)memcpy(relocation_copy, relocation_image.data,
               (size_t)relocation_image.size);
  rel_index = find_raw_section(relocation_image, ".rel.text");
  rel_header = read_le32(relocation_image.data, 32u) + rel_index * 40u;
  rel_offset = read_le32(relocation_image.data, rel_header + 16u);
  rel_info = read_le32(relocation_copy, rel_offset + 4u);
  write_le32(relocation_copy, rel_offset + 4u,
             (rel_info & 0xffffff00u) | 99u);
  relocation_source.path.text = ctool_string("/relocation.o");
  relocation_source.contents =
      ctool_bytes(relocation_copy, relocation_image.size);
  request.objects = &relocation_source;
  ok &= expect_link_failure(job, &request, output, CTOOL_ERR_UNSUPPORTED,
                            CTOOL_LD_DIAG_UNSUPPORTED_RELOCATION,
                            "unsupported relocation");
  request.objects = valid_sources;
  status = ctool_buffer_put_u8(output, 0x5au);
  if (status != CTOOL_OK ||
      !expect_link_failure(job, &request, output, CTOOL_ERR_INPUT,
                           CTOOL_LD_DIAG_NONEMPTY_OUTPUT,
                           "nonempty output") ||
      ctool_buffer_view(output).size != 1u ||
      ctool_buffer_view(output).data[0] != 0x5au) {
    ok = 0;
  }
  ctool_buffer_clear(output);
  ok &= expect_repeated_failure_recovery(&unresolved_source,
                                          &valid_sources[0]);
  request.maximum_image_span = 0u;
  ok &= expect_link_failure(job, &request, output,
                            CTOOL_ERR_INVALID_ARGUMENT,
                            CTOOL_LD_DIAG_INVALID_REQUEST,
                            "zero maximum span");
  free(relocation_copy);
  free(unsupported_copy);
  ctool_buffer_close(output);
  ctool_buffer_close(relocation_buffer);
  ctool_buffer_close(unresolved_buffer);
  ctool_buffer_close(valid_buffer);
  ctool_job_close(job);
  if (!ok) {
    return 1;
  }
  (void)puts("errors: ok");
  return 0;
}

static int run_real_script(int argc, char **argv, ctool_bool write_output) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_limits_t limits = ctool_default_limits();
  ctool_job_t *job;
  ctool_path_t logical_root;
  ctool_path_t path;
  ctool_source_t script;
  ctool_source_t *objects;
  ctool_buffer_t *output;
  ctool_buffer_t *repeat_output;
  ctool_ld_request_t request;
  ctool_ld_result_t result;
  ctool_ld_result_t repeat_result;
  ctool_source_t executable_source;
  ctool_elf32_object_t executable;
  ctool_u32 object_argument =
      write_output == CTOOL_TRUE ? 5u : 4u;
  ctool_u32 object_count = (ctool_u32)argc - object_argument;
  ctool_u32 index;
  ctool_status_t status;
  if (ctool_host_adapter_init(&adapter, argv[2]) != CTOOL_OK) {
    return 1;
  }
  limits.arena_block_bytes = 64u * 1024u;
  limits.arena_bytes = 256u * 1024u * 1024u;
  limits.source_bytes = 16u * 1024u * 1024u;
  limits.output_bytes = 64u * 1024u * 1024u;
  limits.diagnostic_count = 1024u;
  config = ctool_host_job_config(&adapter, limits);
  status = ctool_job_open(&config, &job);
  if (status != CTOOL_OK) {
    return 1;
  }
  objects =
      (ctool_source_t *)calloc((size_t)object_count, sizeof(*objects));
  if (objects == (ctool_source_t *)0) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_path_root(ctool_job_arena(job), &logical_root);
  if (status == CTOOL_OK) {
    status = ctool_path_resolve(ctool_job_arena(job), &logical_root,
                                ctool_string(argv[3]), limits.path_bytes,
                                &path);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_load_source(job, &path, &script);
  }
  for (index = 0u; status == CTOOL_OK && index < object_count; index++) {
    status = ctool_path_resolve(ctool_job_arena(job), &logical_root,
                                ctool_string(argv[index + object_argument]),
                                limits.path_bytes, &path);
    if (status == CTOOL_OK) {
      status = ctool_job_load_source(job, &path, &objects[index]);
    }
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 4096u, limits.output_bytes, &output);
  } else {
    output = (ctool_buffer_t *)0;
  }
  request.objects = objects;
  request.object_count = object_count;
  request.layout.kind = CTOOL_LD_LAYOUT_SCRIPT;
  request.layout.as.script = &script;
  request.maximum_image_span = 32u * 1024u * 1024u;
  if (status == CTOOL_OK) {
    status = ctool_ld_link(job, &request, output, &result);
  }
  repeat_output = (ctool_buffer_t *)0;
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 4096u, limits.output_bytes,
                                   &repeat_output);
  }
  if (status == CTOOL_OK) {
    status = ctool_ld_link(job, &request, repeat_output, &repeat_result);
  }
  if (status == CTOOL_OK) {
    ctool_bytes_t first_image = ctool_buffer_view(output);
    ctool_bytes_t repeat_image = ctool_buffer_view(repeat_output);
    if (memcmp(&result, &repeat_result, sizeof(result)) != 0 ||
        first_image.size != repeat_image.size ||
        memcmp(first_image.data, repeat_image.data, first_image.size) != 0) {
      status = CTOOL_ERR_INTERNAL;
    }
  }
  if (repeat_output != (ctool_buffer_t *)0) {
    ctool_buffer_close(repeat_output);
  }
  executable_source.path.text = ctool_string("/kernel-cupidld.elf");
  executable_source.contents =
      output != (ctool_buffer_t *)0 ? ctool_buffer_view(output)
                                    : ctool_bytes((const void *)0, 0u);
  if (status == CTOOL_OK) {
    status = ctool_elf32_read(job, &executable_source, &executable);
  }
  if (status != CTOOL_OK || result.bytes != executable_source.contents.size ||
      executable.file_type != CTOOL_ELF32_ET_EXEC ||
      executable.entry_point != result.entry || result.entry != 0x00100000u ||
      result.output_section_count != 5u ||
      result.applied_relocation_count == 0u) {
    (void)fprintf(stderr, "real script link failed: %s\n",
                  ctool_status_name(status));
    (void)ctool_job_render_diagnostics(job);
    if (output != (ctool_buffer_t *)0) {
      ctool_buffer_close(output);
    }
    free(objects);
    ctool_job_close(job);
    return 1;
  }
  {
    static const char *names[5] = {".text", ".rodata", ".data", ".ksyms",
                                   ".bss"};
    const ctool_elf32_symbol_t *ksyms_start =
        find_linked_symbol(&executable, "__ksyms_start");
    const ctool_elf32_symbol_t *ksyms_end =
        find_linked_symbol(&executable, "__ksyms_end");
    const ctool_elf32_symbol_t *loaded_end =
        find_linked_symbol(&executable, "_loaded_end");
    const ctool_elf32_symbol_t *bss_start =
        find_linked_symbol(&executable, "_bss_start");
    const ctool_elf32_symbol_t *kernel_end =
        find_linked_symbol(&executable, "_kernel_end");
    int valid = executable.program_header_count == 6u &&
                        executable.section_count >= 6u
                    ? 1
                    : 0;
    for (index = 0u; valid && index < 5u; index++) {
      const ctool_elf32_section_t *section =
          &executable.sections[index + 1u];
      const ctool_elf32_program_header_t *program =
          &executable.program_headers[index];
      ctool_u32 expected_flags =
          CTOOL_ELF32_PF_R |
          (((section->flags & CTOOL_ELF32_SHF_WRITE) != 0u)
               ? CTOOL_ELF32_PF_W
               : 0u) |
          (((section->flags & CTOOL_ELF32_SHF_EXECINSTR) != 0u)
               ? CTOOL_ELF32_PF_X
               : 0u);
      ctool_u32 expected_file_size =
          section->type == (ctool_u32)CTOOL_ELF32_SHT_NOBITS
              ? 0u
              : section->size;
      valid = strcmp(names[index],
                     (const char *)section->name.data) == 0 &&
                      program->type == CTOOL_ELF32_PT_LOAD &&
                      program->file_offset == section->file_offset &&
                      program->virtual_address == section->address &&
                      program->physical_address == section->address &&
                      program->file_size == expected_file_size &&
                      program->memory_size == section->size &&
                      program->flags == expected_flags &&
                      program->alignment == 4096u &&
                      program->file_offset % 4096u ==
                          program->virtual_address % 4096u
                  ? 1
                  : 0;
    }
    if (valid &&
        (executable.program_headers[5].type != 0x6474e551u ||
         executable.program_headers[5].flags != 6u ||
         executable.program_headers[5].file_size != 0u ||
         executable.program_headers[5].memory_size != 0u ||
         executable.program_headers[5].alignment != 0u ||
         result.load_address != executable.sections[1].address ||
         result.loaded_end != executable.sections[4].address +
                                  executable.sections[4].size ||
         result.memory_end != executable.sections[5].address +
                                  executable.sections[5].size ||
         ksyms_start == (const ctool_elf32_symbol_t *)0 ||
         ksyms_start->value != executable.sections[4].address ||
         ksyms_end == (const ctool_elf32_symbol_t *)0 ||
         ksyms_end->value != result.loaded_end ||
         loaded_end == (const ctool_elf32_symbol_t *)0 ||
         loaded_end->value != result.loaded_end ||
         bss_start == (const ctool_elf32_symbol_t *)0 ||
         bss_start->value != executable.sections[5].address ||
         bss_start->section_file_index != executable.sections[4].file_index ||
         kernel_end == (const ctool_elf32_symbol_t *)0 ||
         kernel_end->value != result.memory_end ||
         kernel_end->section_file_index != executable.sections[5].file_index)) {
      valid = 0;
    }
    if (!valid) {
      (void)fprintf(stderr, "real script ELF structure differs\n");
      ctool_buffer_close(output);
      free(objects);
      ctool_job_close(job);
      return 1;
    }
  }
  if (write_output == CTOOL_TRUE) {
    status = ctool_path_resolve(ctool_job_arena(job), &logical_root,
                                ctool_string(argv[4]), limits.path_bytes,
                                &path);
    if (status == CTOOL_OK) {
      status = ctool_job_write(job, &path, executable_source.contents);
    }
    if (status != CTOOL_OK) {
      (void)fprintf(stderr, "real script output write failed: %s\n",
                    ctool_status_name(status));
      ctool_buffer_close(output);
      free(objects);
      ctool_job_close(job);
      return 1;
    }
  }
  (void)printf(
      "real-script: objects=%u bytes=%u entry=%08x load=%08x loaded=%08x "
      "memory=%08x sections=%u symbols=%u relocations=%u\n",
      object_count, result.bytes, result.entry, result.load_address,
      result.loaded_end, result.memory_end, result.output_section_count,
      result.resolved_symbol_count, result.applied_relocation_count);
  ctool_buffer_close(output);
  free(objects);
  ctool_job_close(job);
  return 0;
}

int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "fixed-basic") == 0) {
    return run_fixed_basic();
  }
  if (argc == 2 && strcmp(argv[1], "fixed-layout") == 0) {
    return run_fixed_layout();
  }
  if (argc == 2 && strcmp(argv[1], "merge-symbols") == 0) {
    return run_merge_symbols();
  }
  if (argc == 2 && strcmp(argv[1], "merge-addends") == 0) {
    return run_merge_addends();
  }
  if (argc == 2 && strcmp(argv[1], "script-layout") == 0) {
    return run_script_layout();
  }
  if (argc == 2 && strcmp(argv[1], "errors") == 0) {
    return run_errors();
  }
  if (argc >= 5 && strcmp(argv[1], "real-script") == 0) {
    return run_real_script(argc, argv, CTOOL_FALSE);
  }
  if (argc >= 6 && strcmp(argv[1], "real-script-write") == 0) {
    return run_real_script(argc, argv, CTOOL_TRUE);
  }
  (void)fprintf(stderr,
                "usage: cupidld-contract fixed-basic|fixed-layout|"
                "merge-symbols|merge-addends|script-layout|errors|"
                "real-script ROOT SCRIPT OBJECT...|"
                "real-script-write ROOT SCRIPT OUTPUT OBJECT...\n");
  return 2;
}
