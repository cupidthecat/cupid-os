#include "as_elf.h"
#include "ctool.h"
#include "ctool_host.h"
#include "cupidasm.h"
#include "cupidld.h"
#include "elf32.h"

#include <stdio.h>
#include <string.h>

static const ctool_elf32_section_t *find_section(
    const ctool_elf32_object_t *object, const char *name) {
  ctool_string_t expected = ctool_string(name);
  ctool_u32 index;
  for (index = 0u; index < object->section_count; index++) {
    const ctool_elf32_section_t *section = &object->sections[index];
    if (section->name.size == expected.size &&
        (expected.size == 0u ||
         memcmp(section->name.data, expected.data, expected.size) == 0)) {
      return section;
    }
  }
  return (const ctool_elf32_section_t *)0;
}

static const ctool_elf32_symbol_t *find_symbol(
    const ctool_elf32_object_t *object, const char *name) {
  ctool_string_t expected = ctool_string(name);
  ctool_u32 index;
  for (index = 0u; index < object->symbol_count; index++) {
    const ctool_elf32_symbol_t *symbol = &object->symbols[index];
    if (symbol->name.size == expected.size &&
        (expected.size == 0u ||
         memcmp(symbol->name.data, expected.data, expected.size) == 0)) {
      return symbol;
    }
  }
  return (const ctool_elf32_symbol_t *)0;
}

static int link_result_is_zero(const ctool_ld_result_t *result) {
  const ctool_u8 *bytes = (const ctool_u8 *)result;
  ctool_u32 index;
  for (index = 0u; index < (ctool_u32)sizeof(*result); index++) {
    if (bytes[index] != 0u) {
      return 0;
    }
  }
  return 1;
}

static int run_linked_object(void) {
  static const char source_text[] =
      "BITS 32\n"
      "section .text\n"
      "_start: ret\n"
      "main:\n"
      " call exit\n"
      " mov eax, [value]\n"
      " ret\n"
      "section .data\n"
      "value: dd 0x12345678\n"
      "section .bss\n"
      "scratch: resb 12\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *object_output = (ctool_buffer_t *)0;
  ctool_buffer_t *executable_output = (ctool_buffer_t *)0;
  ctool_buffer_t *repeat_output = (ctool_buffer_t *)0;
  ctool_source_t assembly_source;
  ctool_source_t executable_source;
  ctool_asm_request_t assembly_request;
  ctool_asm_result_t assembly_result;
  as_artifact_request_t artifact_request;
  as_artifact_result_t artifact_result;
  ctool_ld_result_t link_result;
  ctool_elf32_object_t executable;
  const ctool_elf32_section_t *text;
  const ctool_elf32_section_t *data;
  const ctool_elf32_section_t *bss;
  ctool_string_t entries[2];
  ctool_asm_definition_t definition;
  ctool_bytes_t first_image;
  ctool_u32 relocated_value;
  ctool_i32 call_displacement;
  ctool_u32 call_target;
  ctool_status_t status;

  status = ctool_host_adapter_init(&adapter, ".");
  if (status == CTOOL_OK) {
    config = ctool_host_job_config(&adapter, ctool_default_limits());
    status = ctool_job_open(&config, &job);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes,
                                   &object_output);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes,
                                   &executable_output);
  }
  if (status != CTOOL_OK) {
    if (job != (ctool_job_t *)0) {
      ctool_job_close(job);
    }
    return 1;
  }

  entries[0] = ctool_string("main");
  entries[1] = ctool_string("_start");
  assembly_source.path.text = ctool_string("/kernel-aot.asm");
  assembly_source.contents = ctool_bytes(
      source_text, (ctool_u32)(sizeof(source_text) - 1u));
  (void)memset(&assembly_request, 0, sizeof(assembly_request));
  assembly_request.artifact = CTOOL_ASM_ARTIFACT_ELF32_REL;
  assembly_request.initial_mode = CTOOL_X86_MODE_32;
  definition.name = ctool_string("exit");
  definition.kind = CTOOL_ASM_DEFINE_ABSOLUTE;
  definition.value = 0x12345000u;
  assembly_request.definitions = &definition;
  assembly_request.definition_count = 1u;
  assembly_request.entry_candidates = entries;
  assembly_request.entry_candidate_count = 2u;
  status = ctool_asm_assemble(job, &assembly_source, &assembly_request,
                              object_output, &assembly_result);
  if (status == CTOOL_OK) {
    status = as_elf32_exec_link(job, &assembly_result, 0x01a00000u,
                                0x00200000u, executable_output,
                                &link_result);
  }
  first_image = ctool_buffer_view(executable_output);
  executable_source.path.text = ctool_string("/kernel-aot.elf");
  executable_source.contents = first_image;
  (void)memset(&executable, 0, sizeof(executable));
  if (status == CTOOL_OK) {
    status = ctool_elf32_read(job, &executable_source, &executable);
  }
  text = find_section(&executable, ".text");
  data = find_section(&executable, ".data");
  bss = find_section(&executable, ".bss");
  if (text != (const ctool_elf32_section_t *)0 &&
      text->contents.size >= 11u) {
    call_displacement = (ctool_i32)(
        (ctool_u32)text->contents.data[2] |
        ((ctool_u32)text->contents.data[3] << 8u) |
        ((ctool_u32)text->contents.data[4] << 16u) |
        ((ctool_u32)text->contents.data[5] << 24u));
    call_target = text->address + 6u + (ctool_u32)call_displacement;
    relocated_value = (ctool_u32)text->contents.data[7] |
                      ((ctool_u32)text->contents.data[8] << 8u) |
                      ((ctool_u32)text->contents.data[9] << 16u) |
                      ((ctool_u32)text->contents.data[10] << 24u);
  } else {
    call_target = 0u;
    relocated_value = 0u;
  }
  if (status != CTOOL_OK ||
      assembly_result.artifact != CTOOL_ASM_ARTIFACT_ELF32_REL ||
      assembly_result.has_entry != CTOOL_TRUE ||
      assembly_result.entry_symbol.size != 4u ||
      memcmp(assembly_result.entry_symbol.data, "main", 4u) != 0 ||
      link_result.entry != 0x01a00001u ||
      link_result.load_address != 0x01a00000u ||
      link_result.applied_relocation_count != 2u ||
      executable.file_type != CTOOL_ELF32_ET_EXEC ||
      executable.entry_point != link_result.entry ||
      text == (const ctool_elf32_section_t *)0 || text->size != 12u ||
      text->contents.size != 12u || text->contents.data[0] != 0xc3u ||
      text->contents.data[1] != 0xe8u || call_target != definition.value ||
      text->contents.data[6] != 0xa1u ||
      data == (const ctool_elf32_section_t *)0 || data->size != 4u ||
      data->contents.size != 4u || relocated_value != data->address ||
      bss == (const ctool_elf32_section_t *)0 || bss->size != 12u ||
      link_result.memory_end != bss->address + bss->size) {
    (void)fprintf(stderr, "CupidASM AOT link result differs (%s)\n",
                  ctool_status_name(status));
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(executable_output);
    ctool_buffer_close(object_output);
    ctool_job_close(job);
    return 1;
  }

  status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes,
                                 &repeat_output);
  if (status == CTOOL_OK) {
    (void)memset(&artifact_request, 0, sizeof(artifact_request));
    artifact_request.format = AS_ARTIFACT_FORMAT_EXEC;
    artifact_request.initial_mode = CTOOL_X86_MODE_32;
    artifact_request.definitions = &definition;
    artifact_request.definition_count = 1u;
    artifact_request.entry_candidates = entries;
    artifact_request.entry_candidate_count = 2u;
    artifact_request.executable_text_address = 0x01a00000u;
    artifact_request.executable_maximum_span = 0x00200000u;
    status = as_artifact_assemble(job, &assembly_source, &artifact_request,
                                  repeat_output, &artifact_result);
  }
  if (status != CTOOL_OK ||
      artifact_result.format != AS_ARTIFACT_FORMAT_EXEC ||
      artifact_result.entry_address != link_result.entry ||
      memcmp(&artifact_result.link, &link_result, sizeof(link_result)) != 0 ||
      ctool_buffer_view(repeat_output).size != first_image.size ||
      memcmp(ctool_buffer_view(repeat_output).data, first_image.data,
             first_image.size) != 0) {
    (void)fprintf(stderr, "CupidASM AOT link repeat differs\n");
    if (repeat_output != (ctool_buffer_t *)0) {
      ctool_buffer_close(repeat_output);
    }
    ctool_buffer_close(executable_output);
    ctool_buffer_close(object_output);
    ctool_job_close(job);
    return 1;
  }

  ctool_buffer_close(repeat_output);
  ctool_buffer_close(executable_output);
  ctool_buffer_close(object_output);
  ctool_job_close(job);
  (void)puts("linked-object: ok");
  return 0;
}

static int run_link_errors(void) {
  static const char source_text[] =
      "BITS 32\n"
      "section .text\n"
      "main: ret\n";
  static const ctool_u8 bad_object[] = {0x7fu, 'E', 'L', 'F'};
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *object_output = (ctool_buffer_t *)0;
  ctool_buffer_t *output = (ctool_buffer_t *)0;
  ctool_buffer_t *limited_output = (ctool_buffer_t *)0;
  ctool_source_t source;
  ctool_asm_request_t request;
  ctool_asm_result_t artifact;
  ctool_asm_result_t malformed;
  ctool_ld_result_t result;
  ctool_string_t entry = ctool_string("main");
  ctool_status_t status;

  status = ctool_host_adapter_init(&adapter, ".");
  if (status == CTOOL_OK) {
    config = ctool_host_job_config(&adapter, ctool_default_limits());
    status = ctool_job_open(&config, &job);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes,
                                   &object_output);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes,
                                   &output);
  }
  if (status == CTOOL_OK) {
    status = ctool_buffer_open(config.allocator, 16u, 64u,
                               &limited_output);
  }
  if (status != CTOOL_OK) {
    if (job != (ctool_job_t *)0) {
      ctool_job_close(job);
    }
    return 1;
  }

  source.path.text = ctool_string("/kernel-aot-errors.asm");
  source.contents = ctool_bytes(
      source_text, (ctool_u32)(sizeof(source_text) - 1u));
  (void)memset(&request, 0, sizeof(request));
  request.artifact = CTOOL_ASM_ARTIFACT_ELF32_REL;
  request.initial_mode = CTOOL_X86_MODE_32;
  request.entry_candidates = &entry;
  request.entry_candidate_count = 1u;
  status = ctool_asm_assemble(job, &source, &request, object_output,
                              &artifact);
  if (status != CTOOL_OK) {
    goto failed;
  }

  malformed = artifact;
  malformed.entry_symbol.data = (const char *)0;
  malformed.entry_symbol.size = 0u;
  (void)memset(&result, 0xa5, sizeof(result));
  status = as_elf32_exec_link(job, &malformed, 0x01a00000u,
                              0x00200000u, output, &result);
  if (status != CTOOL_ERR_INVALID_ARGUMENT ||
      ctool_buffer_view(output).size != 0u ||
      !link_result_is_zero(&result)) {
    (void)fprintf(stderr, "missing link entry rollback differs\n");
    goto failed;
  }

  malformed = artifact;
  malformed.bytes = ctool_bytes(bad_object,
                                (ctool_u32)sizeof(bad_object));
  (void)memset(&result, 0xa5, sizeof(result));
  status = as_elf32_exec_link(job, &malformed, 0x01a00000u,
                              0x00200000u, output, &result);
  if (status == CTOOL_OK || ctool_buffer_view(output).size != 0u ||
      !link_result_is_zero(&result)) {
    (void)fprintf(stderr, "bad object rollback differs\n");
    goto failed;
  }

  (void)memset(&result, 0xa5, sizeof(result));
  status = as_elf32_exec_link(job, &artifact, 0x01a00000u,
                              0x00200000u, limited_output, &result);
  if (status != CTOOL_ERR_LIMIT ||
      ctool_buffer_view(limited_output).size != 0u ||
      !link_result_is_zero(&result)) {
    (void)fprintf(stderr, "limited link output rollback differs (%s)\n",
                  ctool_status_name(status));
    goto failed;
  }

  (void)memset(&result, 0xa5, sizeof(result));
  status = as_elf32_exec_link(job, &artifact, 0x01a00000u,
                              0x00200000u, output, &result);
  if (status != CTOOL_OK || ctool_buffer_view(output).size == 0u ||
      result.entry != 0x01a00000u || result.load_address != 0x01a00000u) {
    (void)fprintf(stderr, "CupidASM AOT link recovery differs (%s)\n",
                  ctool_status_name(status));
    (void)ctool_job_render_diagnostics(job);
    goto failed;
  }

  ctool_buffer_close(limited_output);
  ctool_buffer_close(output);
  ctool_buffer_close(object_output);
  ctool_job_close(job);
  (void)puts("link-errors: ok");
  return 0;

failed:
  ctool_buffer_close(limited_output);
  ctool_buffer_close(output);
  ctool_buffer_close(object_output);
  ctool_job_close(job);
  return 1;
}

static int run_code_only(void) {
  static const ctool_u8 code[] = {
      0xb8u, 0x78u, 0x56u, 0x34u, 0x12u, 0xc3u};
  static const ctool_u8 expected[134] = {
      [0] = 0x7fu, [1] = 'E', [2] = 'L', [3] = 'F', [4] = 1u,
      [5] = 1u, [6] = 1u,
      [16] = 2u, [18] = 3u, [20] = 1u,
      [26] = 0xa0u, [27] = 0x01u,
      [28] = 0x34u,
      [40] = 0x34u, [42] = 0x20u, [44] = 1u,
      [52] = 1u, [56] = 0x80u,
      [62] = 0xa0u, [63] = 0x01u,
      [66] = 0xa0u, [67] = 0x01u,
      [68] = 6u, [72] = 6u, [76] = 5u, [80] = 4u,
      [128] = 0xb8u, [129] = 0x78u, [130] = 0x56u,
      [131] = 0x34u, [132] = 0x12u, [133] = 0xc3u};
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_buffer_t *output;
  ctool_asm_region_t region;
  ctool_asm_result_t artifact;
  ctool_source_t source;
  ctool_elf32_object_t object;
  ctool_bytes_t image;
  ctool_status_t status;

  status = ctool_host_adapter_init(&adapter, ".");
  if (status != CTOOL_OK) {
    return 1;
  }
  config = ctool_host_job_config(&adapter, ctool_default_limits());
  status = ctool_job_open(&config, &job);
  if (status != CTOOL_OK) {
    return 1;
  }
  status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes,
                                 &output);
  if (status != CTOOL_OK) {
    ctool_job_close(job);
    return 1;
  }

  (void)memset(&region, 0, sizeof(region));
  region.name = ctool_string(".text");
  region.address = 0x01a00000u;
  region.output_offset = 0u;
  region.file_size = (ctool_u32)sizeof(code);
  region.memory_size = (ctool_u32)sizeof(code);
  region.flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR;
  (void)memset(&artifact, 0, sizeof(artifact));
  artifact.artifact = CTOOL_ASM_ARTIFACT_FIXED_IMAGE;
  artifact.bytes = ctool_bytes(code, (ctool_u32)sizeof(code));
  artifact.regions = &region;
  artifact.region_count = 1u;
  artifact.has_entry = CTOOL_TRUE;
  artifact.entry_address = 0x01a00000u;

  status = as_elf32_exec_write(&artifact, output);
  image = ctool_buffer_view(output);
  if (status != CTOOL_OK || image.size != (ctool_u32)sizeof(expected) ||
      memcmp(image.data, expected, sizeof(expected)) != 0) {
    (void)fprintf(stderr, "code-only executable bytes differ\n");
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  source.path.text = ctool_string("/code-only.elf");
  source.contents = image;
  (void)memset(&object, 0, sizeof(object));
  status = ctool_elf32_read(job, &source, &object);
  if (status != CTOOL_OK || object.file_type != CTOOL_ELF32_ET_EXEC ||
      object.entry_point != artifact.entry_address ||
      object.program_header_count != 1u ||
      object.program_headers[0].type != CTOOL_ELF32_PT_LOAD ||
      object.program_headers[0].file_offset != 0x80u ||
      object.program_headers[0].virtual_address != region.address ||
      object.program_headers[0].physical_address != region.address ||
      object.program_headers[0].file_size != region.file_size ||
      object.program_headers[0].memory_size != region.memory_size ||
      object.program_headers[0].flags !=
          (CTOOL_ELF32_PF_R | CTOOL_ELF32_PF_X) ||
      object.program_headers[0].alignment != 4u ||
      object.program_headers[0].contents.size != (ctool_u32)sizeof(code) ||
      memcmp(object.program_headers[0].contents.data, code,
             sizeof(code)) != 0) {
    (void)fprintf(stderr, "code-only executable metadata differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  ctool_buffer_close(output);
  ctool_job_close(job);
  (void)puts("code-only: ok");
  return 0;
}

static int run_code_data_bss(void) {
  static const ctool_u8 payload[] = {
      0x90u, 0xc3u, 0xccu, 0x11u, 0x22u, 0x33u};
  static const ctool_u8 expected[135] = {
      [0] = 0x7fu, [1] = 'E', [2] = 'L', [3] = 'F', [4] = 1u,
      [5] = 1u, [6] = 1u,
      [16] = 2u, [18] = 3u, [20] = 1u,
      [24] = 1u, [26] = 0xa0u, [27] = 0x01u,
      [28] = 0x34u,
      [40] = 0x34u, [42] = 0x20u, [44] = 2u,
      [52] = 1u, [56] = 0x80u,
      [62] = 0xa0u, [63] = 0x01u,
      [66] = 0xa0u, [67] = 0x01u,
      [68] = 3u, [72] = 3u, [76] = 5u, [80] = 4u,
      [84] = 1u, [88] = 0x84u,
      [94] = 0xb0u, [95] = 0x01u,
      [98] = 0xb0u, [99] = 0x01u,
      [100] = 3u, [104] = 7u, [108] = 6u, [112] = 4u,
      [128] = 0x90u, [129] = 0xc3u, [130] = 0xccu,
      [132] = 0x11u, [133] = 0x22u, [134] = 0x33u};
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_buffer_t *output;
  ctool_asm_region_t regions[2];
  ctool_asm_result_t artifact;
  ctool_source_t source;
  ctool_elf32_object_t object;
  ctool_bytes_t image;
  ctool_status_t status;

  status = ctool_host_adapter_init(&adapter, ".");
  if (status != CTOOL_OK) {
    return 1;
  }
  config = ctool_host_job_config(&adapter, ctool_default_limits());
  status = ctool_job_open(&config, &job);
  if (status != CTOOL_OK) {
    return 1;
  }
  status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes,
                                 &output);
  if (status != CTOOL_OK) {
    ctool_job_close(job);
    return 1;
  }

  (void)memset(regions, 0, sizeof(regions));
  regions[0].name = ctool_string(".text");
  regions[0].address = 0x01a00000u;
  regions[0].output_offset = 0u;
  regions[0].file_size = 3u;
  regions[0].memory_size = 3u;
  regions[0].flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR;
  regions[1].name = ctool_string(".data");
  regions[1].address = 0x01b00000u;
  regions[1].output_offset = 3u;
  regions[1].file_size = 3u;
  regions[1].memory_size = 7u;
  regions[1].flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE;
  (void)memset(&artifact, 0, sizeof(artifact));
  artifact.artifact = CTOOL_ASM_ARTIFACT_FIXED_IMAGE;
  artifact.bytes = ctool_bytes(payload, (ctool_u32)sizeof(payload));
  artifact.regions = regions;
  artifact.region_count = 2u;
  artifact.has_entry = CTOOL_TRUE;
  artifact.entry_address = 0x01a00001u;

  status = as_elf32_exec_write(&artifact, output);
  image = ctool_buffer_view(output);
  if (status != CTOOL_OK || image.size != (ctool_u32)sizeof(expected) ||
      memcmp(image.data, expected, sizeof(expected)) != 0) {
    (void)fprintf(stderr, "code/data/BSS executable bytes differ\n");
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  source.path.text = ctool_string("/code-data-bss.elf");
  source.contents = image;
  (void)memset(&object, 0, sizeof(object));
  status = ctool_elf32_read(job, &source, &object);
  if (status != CTOOL_OK || object.file_type != CTOOL_ELF32_ET_EXEC ||
      object.entry_point != artifact.entry_address ||
      object.program_header_count != 2u ||
      object.program_headers[0].file_offset != 0x80u ||
      object.program_headers[0].virtual_address != regions[0].address ||
      object.program_headers[0].file_size != 3u ||
      object.program_headers[0].memory_size != 3u ||
      object.program_headers[0].flags !=
          (CTOOL_ELF32_PF_R | CTOOL_ELF32_PF_X) ||
      object.program_headers[1].type != CTOOL_ELF32_PT_LOAD ||
      object.program_headers[1].file_offset != 0x84u ||
      object.program_headers[1].virtual_address != regions[1].address ||
      object.program_headers[1].physical_address != regions[1].address ||
      object.program_headers[1].file_size != 3u ||
      object.program_headers[1].memory_size != 7u ||
      object.program_headers[1].flags !=
          (CTOOL_ELF32_PF_R | CTOOL_ELF32_PF_W) ||
      object.program_headers[1].alignment != 4u ||
      object.program_headers[1].contents.size != 3u ||
      memcmp(object.program_headers[1].contents.data, payload + 3u, 3u) != 0) {
    (void)fprintf(stderr, "code/data/BSS executable metadata differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  ctool_buffer_clear(output);
  regions[1].flags = CTOOL_ELF32_SHF_ALLOC;
  status = as_elf32_exec_write(&artifact, output);
  image = ctool_buffer_view(output);
  source.path.text = ctool_string("/code-rodata.elf");
  source.contents = image;
  (void)memset(&object, 0, sizeof(object));
  if (status == CTOOL_OK) {
    status = ctool_elf32_read(job, &source, &object);
  }
  if (status != CTOOL_OK || object.program_header_count != 2u ||
      object.program_headers[1].flags != CTOOL_ELF32_PF_R) {
    (void)fprintf(stderr, "read-only data segment metadata differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  ctool_buffer_close(output);
  ctool_job_close(job);
  (void)puts("code-data-bss: ok");
  return 0;
}

static int expect_empty_failure(const char *name,
                                const ctool_asm_result_t *artifact,
                                ctool_buffer_t *output,
                                ctool_status_t expected) {
  ctool_status_t status = as_elf32_exec_write(artifact, output);
  if (status != expected || ctool_buffer_view(output).size != 0u) {
    (void)fprintf(stderr, "%s failure contract differs\n", name);
    return 0;
  }
  return 1;
}

static int run_errors(void) {
  static const ctool_u8 payload[] = {0x90u, 0xc3u, 0x11u, 0x22u};
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_buffer_t *output;
  ctool_buffer_t *limited_output;
  ctool_asm_region_t regions[2];
  ctool_asm_region_t malformed_regions[2];
  ctool_asm_result_t artifact;
  ctool_asm_result_t malformed;
  ctool_status_t status;

  status = ctool_host_adapter_init(&adapter, ".");
  if (status != CTOOL_OK) {
    return 1;
  }
  config = ctool_host_job_config(&adapter, ctool_default_limits());
  status = ctool_buffer_open(config.allocator, 32u,
                             config.limits.output_bytes, &output);
  if (status != CTOOL_OK) {
    return 1;
  }
  /* Header, program table, and code fit; data alignment fails after bytes have
   * been written, proving whole-operation rollback rather than one append. */
  status = ctool_buffer_open(config.allocator, 16u, 130u, &limited_output);
  if (status != CTOOL_OK) {
    ctool_buffer_close(output);
    return 1;
  }

  (void)memset(regions, 0, sizeof(regions));
  regions[0].name = ctool_string(".text");
  regions[0].address = 0x01a00000u;
  regions[0].output_offset = 0u;
  regions[0].file_size = 2u;
  regions[0].memory_size = 2u;
  regions[0].flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR;
  regions[1].name = ctool_string(".data");
  regions[1].address = 0x01b00000u;
  regions[1].output_offset = 2u;
  regions[1].file_size = 2u;
  regions[1].memory_size = 6u;
  regions[1].flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE;
  (void)memset(&artifact, 0, sizeof(artifact));
  artifact.artifact = CTOOL_ASM_ARTIFACT_FIXED_IMAGE;
  artifact.bytes = ctool_bytes(payload, (ctool_u32)sizeof(payload));
  artifact.regions = regions;
  artifact.region_count = 2u;
  artifact.has_entry = CTOOL_TRUE;
  artifact.entry_address = regions[0].address;

  malformed = artifact;
  malformed.artifact = CTOOL_ASM_ARTIFACT_RAW;
  if (!expect_empty_failure("non-fixed artifact", &malformed, output,
                            CTOOL_ERR_INVALID_ARGUMENT)) {
    goto failed;
  }
  malformed = artifact;
  malformed.has_entry = CTOOL_FALSE;
  if (!expect_empty_failure("missing entry", &malformed, output,
                            CTOOL_ERR_INVALID_ARGUMENT)) {
    goto failed;
  }
  malformed = artifact;
  malformed.region_count = 0u;
  if (!expect_empty_failure("missing code region", &malformed, output,
                            CTOOL_ERR_INVALID_ARGUMENT)) {
    goto failed;
  }

  (void)memcpy(malformed_regions, regions, sizeof(regions));
  malformed_regions[0].flags |= CTOOL_ELF32_SHF_WRITE;
  malformed = artifact;
  malformed.regions = malformed_regions;
  if (!expect_empty_failure("writable code", &malformed, output,
                            CTOOL_ERR_INVALID_ARGUMENT)) {
    goto failed;
  }
  (void)memcpy(malformed_regions, regions, sizeof(regions));
  malformed_regions[1].memory_size = 1u;
  malformed.regions = malformed_regions;
  if (!expect_empty_failure("data file exceeds memory", &malformed, output,
                            CTOOL_ERR_INVALID_ARGUMENT)) {
    goto failed;
  }
  (void)memcpy(malformed_regions, regions, sizeof(regions));
  malformed_regions[1].output_offset = 1u;
  malformed.regions = malformed_regions;
  if (!expect_empty_failure("noncontiguous payload", &malformed, output,
                            CTOOL_ERR_INVALID_ARGUMENT)) {
    goto failed;
  }
  (void)memcpy(malformed_regions, regions, sizeof(regions));
  malformed_regions[1].address = regions[0].address;
  malformed.regions = malformed_regions;
  if (!expect_empty_failure("overlapping load regions", &malformed, output,
                            CTOOL_ERR_INVALID_ARGUMENT)) {
    goto failed;
  }
  (void)memcpy(malformed_regions, regions, sizeof(regions));
  malformed_regions[1].address = 0xfffffffcu;
  malformed_regions[1].memory_size = 8u;
  malformed.regions = malformed_regions;
  if (!expect_empty_failure("load address overflow", &malformed, output,
                            CTOOL_ERR_OVERFLOW)) {
    goto failed;
  }
  malformed = artifact;
  malformed.entry_address = regions[0].address + regions[0].memory_size;
  if (!expect_empty_failure("entry outside code", &malformed, output,
                             CTOOL_ERR_INVALID_ARGUMENT)) {
    goto failed;
  }
  (void)memcpy(malformed_regions, regions, sizeof(regions));
  malformed_regions[0].memory_size = 4u;
  malformed = artifact;
  malformed.regions = malformed_regions;
  malformed.entry_address = regions[0].address + regions[0].file_size;
  if (!expect_empty_failure("entry outside file-backed code", &malformed,
                            output, CTOOL_ERR_INVALID_ARGUMENT)) {
    goto failed;
  }
  malformed = artifact;
  malformed.bytes.size--;
  if (!expect_empty_failure("payload size mismatch", &malformed, output,
                            CTOOL_ERR_INVALID_ARGUMENT)) {
    goto failed;
  }
  if (!expect_empty_failure("late output limit", &artifact, limited_output,
                            CTOOL_ERR_LIMIT)) {
    goto failed;
  }

  ctool_buffer_close(limited_output);
  ctool_buffer_close(output);
  (void)puts("errors: ok");
  return 0;

failed:
  ctool_buffer_close(limited_output);
  ctool_buffer_close(output);
  return 1;
}

static int run_artifact_raw(void) {
  static const char source_text[] =
      "BITS 16\n"
      "ORG 0x8000\n"
      "start: mov ax, 0x1234\n"
      "db 0xaa, 0xbb\n"
      "BITS 32\n"
      "mov eax, 0x12345678\n"
      "ret\n";
  static const ctool_u8 expected_bytes[] = {
      0xb8u, 0x34u, 0x12u, 0xaau, 0xbbu,
      0xb8u, 0x78u, 0x56u, 0x34u, 0x12u, 0xc3u};
  static const char expected_map[] =
      "cupid.raw-map.v1\n"
      "size 11\n"
      "base 0x00008000\n"
      "range 0x00000000 code16\n"
      "range 0x00000003 data\n"
      "range 0x00000005 code32\n";
  static const char empty_source_text[] =
      "ORG 0x9000\n"
      "VALUE equ 1\n";
  static const char expected_empty_map[] =
      "cupid.raw-map.v1\n"
      "size 0\n"
      "base 0x00009000\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *output = (ctool_buffer_t *)0;
  ctool_buffer_t *map_output = (ctool_buffer_t *)0;
  ctool_source_t source;
  as_artifact_request_t request;
  as_artifact_result_t result;
  ctool_status_t status;

  status = ctool_host_adapter_init(&adapter, ".");
  if (status == CTOOL_OK) {
    config = ctool_host_job_config(&adapter, ctool_default_limits());
    status = ctool_job_open(&config, &job);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 32u, config.limits.output_bytes,
                                   &output);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 64u, config.limits.output_bytes,
                                   &map_output);
  }
  if (status != CTOOL_OK) {
    if (output != (ctool_buffer_t *)0) ctool_buffer_close(output);
    if (job != (ctool_job_t *)0) ctool_job_close(job);
    return 1;
  }

  source.path.text = ctool_string("/mixed-kernel.asm");
  source.contents = ctool_bytes(
      source_text, (ctool_u32)(sizeof(source_text) - 1u));
  (void)memset(&request, 0, sizeof(request));
  request.format = AS_ARTIFACT_FORMAT_BIN;
  request.initial_mode = CTOOL_X86_MODE_32;
  status = as_artifact_assemble(job, &source, &request, output, &result);
  if (status == CTOOL_OK) {
    status = as_artifact_render_raw_map(&result, map_output);
  }
  if (status != CTOOL_OK || result.format != AS_ARTIFACT_FORMAT_BIN ||
      result.bytes.size != (ctool_u32)sizeof(expected_bytes) ||
      memcmp(result.bytes.data, expected_bytes, sizeof(expected_bytes)) != 0 ||
      result.raw_origin != 0x8000u || result.raw_range_count != 3u ||
      result.raw_ranges[0].offset != 0u ||
      result.raw_ranges[0].kind != CTOOL_ASM_RAW_RANGE_CODE16 ||
      result.raw_ranges[1].offset != 3u ||
      result.raw_ranges[1].kind != CTOOL_ASM_RAW_RANGE_DATA ||
      result.raw_ranges[2].offset != 5u ||
      result.raw_ranges[2].kind != CTOOL_ASM_RAW_RANGE_CODE32 ||
      ctool_buffer_view(map_output).size !=
          (ctool_u32)(sizeof(expected_map) - 1u) ||
      memcmp(ctool_buffer_view(map_output).data, expected_map,
             sizeof(expected_map) - 1u) != 0) {
    (void)fprintf(stderr, "kernel raw artifact differs (%s)\n",
                  ctool_status_name(status));
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(map_output);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  ctool_buffer_clear(map_output);
  ctool_buffer_clear(output);
  source.contents = ctool_bytes(
      empty_source_text, (ctool_u32)(sizeof(empty_source_text) - 1u));
  status = as_artifact_assemble(job, &source, &request, output, &result);
  if (status == CTOOL_OK) {
    status = as_artifact_render_raw_map(&result, map_output);
  }
  if (status != CTOOL_OK || result.bytes.size != 0u ||
      result.raw_ranges != (const ctool_asm_raw_range_t *)0 ||
      result.raw_range_count != 0u || result.raw_origin != 0x9000u ||
      ctool_buffer_view(map_output).size !=
          (ctool_u32)(sizeof(expected_empty_map) - 1u) ||
      memcmp(ctool_buffer_view(map_output).data, expected_empty_map,
             sizeof(expected_empty_map) - 1u) != 0) {
    (void)fprintf(stderr, "empty kernel raw artifact differs (%s)\n",
                  ctool_status_name(status));
    ctool_buffer_close(map_output);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  ctool_buffer_close(map_output);
  ctool_buffer_close(output);
  ctool_job_close(job);
  (void)puts("artifact-raw: ok");
  return 0;
}

static int run_artifact_relocatable(void) {
  static const char source_text[] =
      "BITS 32\n"
      "extern target\n"
      "global main\n"
      "section .text\n"
      "main: call target\n"
      "ret\n"
      "section .data\n"
      "pointer: dd target\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *output = (ctool_buffer_t *)0;
  ctool_source_t source;
  ctool_source_t object_source;
  ctool_elf32_object_t object;
  as_artifact_request_t request;
  as_artifact_result_t result;
  const ctool_elf32_section_t *text;
  const ctool_elf32_section_t *data;
  const ctool_elf32_symbol_t *target;
  ctool_status_t status;

  status = ctool_host_adapter_init(&adapter, ".");
  if (status == CTOOL_OK) {
    config = ctool_host_job_config(&adapter, ctool_default_limits());
    status = ctool_job_open(&config, &job);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes,
                                   &output);
  }
  if (status != CTOOL_OK) {
    if (job != (ctool_job_t *)0) ctool_job_close(job);
    return 1;
  }

  source.path.text = ctool_string("/kernel-relocatable.asm");
  source.contents = ctool_bytes(
      source_text, (ctool_u32)(sizeof(source_text) - 1u));
  (void)memset(&request, 0, sizeof(request));
  request.format = AS_ARTIFACT_FORMAT_ELF32;
  request.initial_mode = CTOOL_X86_MODE_32;
  status = as_artifact_assemble(job, &source, &request, output, &result);
  object_source.path.text = ctool_string("/kernel-relocatable.o");
  object_source.contents = ctool_buffer_view(output);
  (void)memset(&object, 0, sizeof(object));
  if (status == CTOOL_OK) {
    status = ctool_elf32_read(job, &object_source, &object);
  }
  text = find_section(&object, ".text");
  data = find_section(&object, ".data");
  target = find_symbol(&object, "target");
  if (status != CTOOL_OK || result.format != AS_ARTIFACT_FORMAT_ELF32 ||
      result.bytes.data != ctool_buffer_view(output).data ||
      result.bytes.size != ctool_buffer_view(output).size ||
      result.entry_symbol.data != (const char *)0 ||
      result.entry_symbol.size != 0u ||
      result.entry_address != 0u || result.raw_ranges != (const ctool_asm_raw_range_t *)0 ||
      object.file_type != CTOOL_ELF32_ET_REL ||
      object.relocation_count != 2u ||
      target == (const ctool_elf32_symbol_t *)0 ||
      target->binding != CTOOL_ELF32_BIND_GLOBAL ||
      target->placement != CTOOL_ELF32_SYMBOL_UNDEFINED ||
      target->section_file_index != CTOOL_ELF32_NO_SECTION ||
      text == (const ctool_elf32_section_t *)0 || text->contents.size != 6u ||
      data == (const ctool_elf32_section_t *)0 || data->contents.size != 4u) {
    (void)fprintf(stderr, "kernel relocatable artifact differs (%s)\n",
                  ctool_status_name(status));
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  ctool_buffer_close(output);
  ctool_job_close(job);
  (void)puts("artifact-relocatable: ok");
  return 0;
}

static int result_is_zero(const as_artifact_result_t *result) {
  const ctool_u8 *bytes = (const ctool_u8 *)result;
  ctool_u32 index;
  for (index = 0u; index < (ctool_u32)sizeof(*result); index++) {
    if (bytes[index] != 0u) return 0;
  }
  return 1;
}

static int run_artifact_errors(void) {
  static const char source_text[] = "BITS 32\nmain: ret\n";
  static const ctool_u8 sentinel[] = {0x51u, 0x52u};
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *output = (ctool_buffer_t *)0;
  ctool_buffer_t *map_output = (ctool_buffer_t *)0;
  ctool_source_t source;
  ctool_string_t entry;
  as_artifact_request_t request;
  as_artifact_result_t result;
  as_command_t command;
  ctool_status_t status;

  status = ctool_host_adapter_init(&adapter, ".");
  if (status == CTOOL_OK) {
    config = ctool_host_job_config(&adapter, ctool_default_limits());
    status = ctool_job_open(&config, &job);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 32u, config.limits.output_bytes,
                                   &output);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 32u, 40u, &map_output);
  }
  if (status != CTOOL_OK) {
    if (output != (ctool_buffer_t *)0) ctool_buffer_close(output);
    if (job != (ctool_job_t *)0) ctool_job_close(job);
    return 1;
  }

  source.path.text = ctool_string("/artifact-errors.asm");
  source.contents = ctool_bytes(
      source_text, (ctool_u32)(sizeof(source_text) - 1u));
  entry = ctool_string("main");
  (void)memset(&request, 0, sizeof(request));
  request.format = (as_artifact_format_t)99;
  request.initial_mode = CTOOL_X86_MODE_32;
  (void)memset(&result, 0xa5, sizeof(result));
  status = as_artifact_assemble(job, &source, &request, output, &result);
  if (status != CTOOL_ERR_INVALID_ARGUMENT ||
      ctool_buffer_view(output).size != 0u || !result_is_zero(&result)) {
    (void)fprintf(stderr, "invalid artifact format rollback differs\n");
    goto failed;
  }

  request.format = AS_ARTIFACT_FORMAT_EXEC;
  request.entry_candidates = &entry;
  request.entry_candidate_count = 1u;
  (void)memset(&result, 0xa5, sizeof(result));
  status = as_artifact_assemble(job, &source, &request, output, &result);
  if (status != CTOOL_ERR_INVALID_ARGUMENT ||
      ctool_buffer_view(output).size != 0u || !result_is_zero(&result)) {
    (void)fprintf(stderr, "missing exec layout rollback differs\n");
    goto failed;
  }

  request.format = AS_ARTIFACT_FORMAT_BIN;
  request.entry_candidates = (const ctool_string_t *)0;
  request.entry_candidate_count = 0u;
  status = ctool_buffer_append(output,
                               ctool_bytes(sentinel, sizeof(sentinel)));
  (void)memset(&result, 0xa5, sizeof(result));
  if (status == CTOOL_OK) {
    status = as_artifact_assemble(job, &source, &request, output, &result);
  }
  if (status != CTOOL_ERR_INVALID_ARGUMENT ||
      ctool_buffer_view(output).size != sizeof(sentinel) ||
      memcmp(ctool_buffer_view(output).data, sentinel, sizeof(sentinel)) != 0 ||
      !result_is_zero(&result)) {
    (void)fprintf(stderr, "nonempty artifact output preservation differs\n");
    goto failed;
  }
  ctool_buffer_clear(output);
  status = as_artifact_assemble(job, &source, &request, output, &result);
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "artifact recovery differs (%s)\n",
                  ctool_status_name(status));
    goto failed;
  }
  status = as_artifact_render_raw_map(&result, map_output);
  if (status != CTOOL_ERR_LIMIT || ctool_buffer_view(map_output).size != 0u) {
    (void)fprintf(stderr, "raw map rollback differs (%s)\n",
                  ctool_status_name(status));
    goto failed;
  }

  status = as_command_parse(AS_COMMAND_AS,
                            ctool_string("-f bin -o /out /in.asm"),
                            &command);
  if (status != CTOOL_ERR_INPUT ||
      as_command_parse(AS_COMMAND_AS,
                       ctool_string("-f bad -o /out /in.asm"),
                       &command) != CTOOL_ERR_INPUT ||
      as_command_parse(AS_COMMAND_CUPIDASM,
                       ctool_string("-f elf32 --map /map /in.asm -o /out"),
                       &command) != CTOOL_ERR_INPUT ||
      as_command_parse(AS_COMMAND_CUPIDASM,
                       ctool_string("-f bin --map /out -o /out /in.asm"),
                       &command) != CTOOL_ERR_INPUT ||
      as_command_parse(AS_COMMAND_CUPIDASM,
                       ctool_string("-f elf32 /in.asm"),
                       &command) != CTOOL_ERR_INPUT) {
    (void)fprintf(stderr, "invalid artifact command parsing differs\n");
    goto failed;
  }
  if (as_command_parse(AS_COMMAND_AS, ctool_string("-o /out /in.asm"),
                       &command) != CTOOL_OK ||
      command.kind != AS_COMMAND_ARTIFACT ||
      command.format != AS_ARTIFACT_FORMAT_EXEC) {
    (void)fprintf(stderr, "as executable compatibility parsing differs\n");
    goto failed;
  }
  if (as_command_parse(AS_COMMAND_AS, ctool_string("/in.asm"),
                       &command) != CTOOL_OK ||
      command.kind != AS_COMMAND_JIT) {
    (void)fprintf(stderr, "as JIT compatibility parsing differs\n");
    goto failed;
  }
  if (as_command_parse(AS_COMMAND_CUPIDASM,
                       ctool_string("-f bin --map /map -o /out /in.asm"),
                       &command) != CTOOL_OK ||
      command.kind != AS_COMMAND_ARTIFACT ||
      command.format != AS_ARTIFACT_FORMAT_BIN || command.map.size != 4u) {
    (void)fprintf(stderr, "raw artifact command parsing differs\n");
    goto failed;
  }
  if (as_command_parse(AS_COMMAND_CUPIDASM,
                       ctool_string("/in.asm -f elf32 -o /out"),
                       &command) != CTOOL_OK ||
      command.kind != AS_COMMAND_ARTIFACT ||
      command.format != AS_ARTIFACT_FORMAT_ELF32) {
    (void)fprintf(stderr, "relocatable artifact command parsing differs\n");
    goto failed;
  }
  if (as_command_parse(AS_COMMAND_CUPIDASM,
                       ctool_string("/in.asm -o /out"), &command) != CTOOL_OK ||
      command.kind != AS_COMMAND_ARTIFACT ||
      command.format != AS_ARTIFACT_FORMAT_EXEC) {
    (void)fprintf(stderr, "cupidasm output compatibility parsing differs\n");
    goto failed;
  }
  if (as_command_parse(AS_COMMAND_CUPIDASM, ctool_string("/in.asm"),
                       &command) != CTOOL_OK ||
      command.kind != AS_COMMAND_ARTIFACT ||
      command.format != AS_ARTIFACT_FORMAT_EXEC || command.output.size != 0u) {
    (void)fprintf(stderr, "cupidasm default output parsing differs\n");
    goto failed;
  }

  ctool_buffer_close(map_output);
  ctool_buffer_close(output);
  ctool_job_close(job);
  (void)puts("artifact-errors: ok");
  return 0;

failed:
  ctool_buffer_close(map_output);
  ctool_buffer_close(output);
  ctool_job_close(job);
  return 1;
}

#define FAKE_FILE_COUNT 16u
#define FAKE_FILE_BYTES 4096u

typedef struct {
  const char *path;
  ctool_u8 bytes[FAKE_FILE_BYTES];
  ctool_u32 size;
  ctool_bool exists;
} fake_file_t;

typedef struct {
  fake_file_t files[FAKE_FILE_COUNT];
  ctool_u32 commit_inspect_count;
  ctool_u32 fail_commit_inspect_at;
  ctool_u32 write_count;
  ctool_u32 fail_write_at;
  ctool_u32 write_then_fail_at;
  ctool_u32 write_prefix_then_fail_at;
  ctool_u32 replace_count;
  ctool_u32 fail_replace_at;
  ctool_u32 fail_replace_again_at;
  ctool_u32 remove_count;
  ctool_u32 fail_remove_at;
} fake_publication_t;

static fake_file_t *fake_find(fake_publication_t *store,
                              ctool_string_t path) {
  ctool_u32 index;
  for (index = 0u; index < FAKE_FILE_COUNT; index++) {
    const char *name = store->files[index].path;
    if (name != (const char *)0 && strlen(name) == path.size &&
        memcmp(name, path.data, path.size) == 0) {
      return &store->files[index];
    }
  }
  return (fake_file_t *)0;
}

static ctool_status_t fake_inspect(void *context, ctool_string_t path,
                                   ctool_bool *exists_out) {
  fake_publication_t *store = (fake_publication_t *)context;
  fake_file_t *file = fake_find(store, path);
  if (file == (fake_file_t *)0 || exists_out == (ctool_bool *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  if (file == &store->files[6] || file == &store->files[7] ||
      file == &store->files[8]) {
    store->commit_inspect_count++;
    if (store->commit_inspect_count == store->fail_commit_inspect_at) {
      return CTOOL_ERR_IO;
    }
  }
  *exists_out = file->exists;
  return CTOOL_OK;
}

static ctool_status_t fake_read(void *context, ctool_string_t path,
                                ctool_mut_bytes_t destination,
                                ctool_u32 *size_out) {
  fake_file_t *file = fake_find((fake_publication_t *)context, path);
  if (file == (fake_file_t *)0 || file->exists == CTOOL_FALSE ||
      destination.data == (ctool_u8 *)0 || size_out == (ctool_u32 *)0 ||
      file->size > destination.size) {
    return CTOOL_ERR_IO;
  }
  if (file->size != 0u) {
    (void)memcpy(destination.data, file->bytes, file->size);
  }
  *size_out = file->size;
  return CTOOL_OK;
}

static ctool_status_t fake_write_new(void *context, ctool_string_t path,
                                     ctool_bytes_t contents) {
  fake_publication_t *store = (fake_publication_t *)context;
  fake_file_t *file = fake_find(store, path);
  store->write_count++;
  if (store->write_count == store->fail_write_at) return CTOOL_ERR_IO;
  if (file == (fake_file_t *)0 || file->exists == CTOOL_TRUE ||
      contents.size > FAKE_FILE_BYTES) {
    return CTOOL_ERR_IO;
  }
  if (store->write_count == store->write_prefix_then_fail_at) {
    ctool_u32 prefix = contents.size / 2u;
    if (prefix != 0u) (void)memcpy(file->bytes, contents.data, prefix);
    file->size = prefix;
    file->exists = CTOOL_TRUE;
    return CTOOL_ERR_IO;
  }
  if (contents.size != 0u) {
    (void)memcpy(file->bytes, contents.data, contents.size);
  }
  file->size = contents.size;
  file->exists = CTOOL_TRUE;
  if (store->write_count == store->write_then_fail_at) return CTOOL_ERR_IO;
  return CTOOL_OK;
}

static ctool_status_t fake_replace(void *context, ctool_string_t from,
                                   ctool_string_t to) {
  fake_publication_t *store = (fake_publication_t *)context;
  fake_file_t *source = fake_find(store, from);
  fake_file_t *destination = fake_find(store, to);
  store->replace_count++;
  if (store->replace_count == store->fail_replace_at ||
      store->replace_count == store->fail_replace_again_at) {
    return CTOOL_ERR_IO;
  }
  if (source == (fake_file_t *)0 || destination == (fake_file_t *)0 ||
      source->exists == CTOOL_FALSE) {
    return CTOOL_ERR_IO;
  }
  (void)memcpy(destination->bytes, source->bytes, source->size);
  destination->size = source->size;
  destination->exists = CTOOL_TRUE;
  source->size = 0u;
  source->exists = CTOOL_FALSE;
  return CTOOL_OK;
}

static ctool_status_t fake_remove(void *context, ctool_string_t path) {
  fake_publication_t *store = (fake_publication_t *)context;
  fake_file_t *file = fake_find(store, path);
  store->remove_count++;
  if (store->remove_count == store->fail_remove_at) return CTOOL_ERR_IO;
  if (file == (fake_file_t *)0) return CTOOL_ERR_INVALID_ARGUMENT;
  file->size = 0u;
  file->exists = CTOOL_FALSE;
  return CTOOL_OK;
}

static void fake_reset(fake_publication_t *store) {
  static const char *paths[FAKE_FILE_COUNT] = {
      "/out", "/out.new", "/out.cupid-as-old", "/map",
      "/map.new", "/map.cupid-as-old", "/map.cupid-as-done",
      "/map2.cupid-as-done", "/out.cupid-as-done",
      "/map2", "/map2.new", "/map2.cupid-as-old",
      "/out2", "/out2.new", "/out2.cupid-as-old",
      "/out2.cupid-as-done"};
  static const char old_output[] = "old-output";
  static const char old_map[] = "old-map";
  ctool_u32 index;
  (void)memset(store, 0, sizeof(*store));
  for (index = 0u; index < FAKE_FILE_COUNT; index++) {
    store->files[index].path = paths[index];
  }
  (void)memcpy(store->files[0].bytes, old_output, sizeof(old_output) - 1u);
  store->files[0].size = (ctool_u32)(sizeof(old_output) - 1u);
  store->files[0].exists = CTOOL_TRUE;
  (void)memcpy(store->files[3].bytes, old_map, sizeof(old_map) - 1u);
  store->files[3].size = (ctool_u32)(sizeof(old_map) - 1u);
  store->files[3].exists = CTOOL_TRUE;
}

static int fake_has(fake_publication_t *store, ctool_u32 index,
                    const char *text) {
  ctool_u32 size = (ctool_u32)strlen(text);
  return store->files[index].exists == CTOOL_TRUE &&
         store->files[index].size == size &&
         memcmp(store->files[index].bytes, text, size) == 0;
}

static void fake_seed_commit_record(fake_publication_t *store,
                                    const ctool_u8 *backup,
                                    ctool_u32 backup_size) {
  static const ctool_u8 magic[8] = {'C', 'U', 'P', 'I', 'D', 'A', 'S', 1u};
  static const char commit[] = "/out.cupid-as-done";
  fake_file_t *file = &store->files[8];
  ctool_u32 cursor = 0u;
  ctool_u32 index;
  for (index = 0u; index < 8u; index++) file->bytes[cursor++] = magic[index];
  file->bytes[cursor++] = 1u;
  file->bytes[cursor++] = 0u;
  file->bytes[cursor++] = 0u;
  file->bytes[cursor++] = 0u;
  file->bytes[cursor++] = (ctool_u8)backup_size;
  file->bytes[cursor++] = 0u;
  file->bytes[cursor++] = 0u;
  file->bytes[cursor++] = 0u;
  for (index = 0u; index < backup_size; index++) {
    file->bytes[cursor++] = backup[index];
  }
  file->bytes[cursor++] = 0u;
  file->bytes[cursor++] = (ctool_u8)(sizeof(commit) - 1u);
  file->bytes[cursor++] = 0u;
  file->bytes[cursor++] = 0u;
  file->bytes[cursor++] = 0u;
  for (index = 0u; index < (ctool_u32)(sizeof(commit) - 1u); index++) {
    file->bytes[cursor++] = (ctool_u8)commit[index];
  }
  file->bytes[cursor++] = 0u;
  file->size = cursor;
  file->exists = CTOOL_TRUE;
}

static void fake_make_long_publication_paths(char *target, char *backup,
                                             char *commit, char fill) {
  static const char backup_suffix[] = ".cupid-as-old";
  static const char commit_suffix[] = ".cupid-as-done";
  ctool_u32 index;
  target[0] = '/';
  for (index = 1u; index < 496u; index++) target[index] = fill;
  target[496] = '\0';
  for (index = 0u; index < 496u; index++) {
    backup[index] = target[index];
    commit[index] = target[index];
  }
  for (index = 0u; index < (ctool_u32)sizeof(backup_suffix); index++) {
    backup[496u + index] = backup_suffix[index];
  }
  for (index = 0u; index < (ctool_u32)sizeof(commit_suffix); index++) {
    commit[496u + index] = commit_suffix[index];
  }
}

static int run_artifact_publication(void) {
  static const ctool_u8 output_bytes[] = "new-output";
  static const ctool_u8 map_bytes[] = "new-map";
  static char long_output[512];
  static char long_output_backup[512];
  static char long_output_commit[512];
  static char long_map[512];
  static char long_map_backup[512];
  static char long_map_commit[512];
  ctool_u8 publication_scratch[512u * 4u + 32u];
  fake_publication_t store;
  as_artifact_publication_ops_t ops;
  as_artifact_publication_request_t request;
  ctool_status_t status;

  (void)memset(&ops, 0, sizeof(ops));
  ops.context = &store;
  ops.inspect = fake_inspect;
  ops.read = fake_read;
  ops.write_new = fake_write_new;
  ops.replace = fake_replace;
  ops.remove = fake_remove;
  (void)memset(&request, 0, sizeof(request));
  request.artifact.target = ctool_string("/out");
  request.artifact.candidate = ctool_string("/out.new");
  request.artifact.backup = ctool_string("/out.cupid-as-old");
  request.artifact.commit = ctool_string("/out.cupid-as-done");
  request.map.target = ctool_string("/map");
  request.map.candidate = ctool_string("/map.new");
  request.map.backup = ctool_string("/map.cupid-as-old");
  request.map.commit = ctool_string("/map.cupid-as-done");
  request.scratch.data = publication_scratch;
  request.scratch.size = (ctool_u32)sizeof(publication_scratch);
  request.artifact_bytes = ctool_bytes(
      output_bytes, (ctool_u32)(sizeof(output_bytes) - 1u));
  request.map_bytes = ctool_bytes(
      map_bytes, (ctool_u32)(sizeof(map_bytes) - 1u));

  {
    as_artifact_publication_request_t invalid = request;
    invalid.map.candidate = invalid.artifact.target;
    if (as_artifact_publish(&ops, &invalid) != CTOOL_ERR_INVALID_ARGUMENT) {
      (void)fprintf(stderr, "overlapping publication paths were accepted\n");
      return 1;
    }
    invalid = request;
    invalid.artifact.commit = ctool_string("/other.cupid-as-done");
    if (as_artifact_publish(&ops, &invalid) != CTOOL_ERR_INVALID_ARGUMENT) {
      (void)fprintf(stderr, "mismatched recovery paths were accepted\n");
      return 1;
    }
  }

  {
    static const ctool_u8 embedded_backup[] = {
        '/', 'o', 'u', 't', '\0', '.', 'c', 'u', 'p', 'i', 'd', '-', 'a',
        's', '-', 'o', 'l', 'd'};
    fake_reset(&store);
    fake_seed_commit_record(
        &store, embedded_backup, (ctool_u32)sizeof(embedded_backup));
    status = as_artifact_publish(&ops, &request);
    if (status != CTOOL_ERR_INPUT ||
        !fake_has(&store, 0u, "old-output") ||
        !fake_has(&store, 3u, "old-map") || store.remove_count != 0u) {
      (void)fprintf(stderr, "unsafe recovery record was accepted\n");
      return 1;
    }
  }

  fake_make_long_publication_paths(
      long_output, long_output_backup, long_output_commit, 'a');
  fake_make_long_publication_paths(
      long_map, long_map_backup, long_map_commit, 'b');
  fake_reset(&store);
  store.files[0].path = long_output;
  store.files[2].path = long_output_backup;
  store.files[8].path = long_output_commit;
  store.files[3].path = long_map;
  store.files[5].path = long_map_backup;
  store.files[6].path = long_map_commit;
  request.artifact.target = ctool_string(long_output);
  request.artifact.backup = ctool_string(long_output_backup);
  request.artifact.commit = ctool_string(long_output_commit);
  request.map.target = ctool_string(long_map);
  request.map.backup = ctool_string(long_map_backup);
  request.map.commit = ctool_string(long_map_commit);
  status = as_artifact_publish(&ops, &request);
  if (status != CTOOL_OK || !fake_has(&store, 0u, "new-output") ||
      !fake_has(&store, 3u, "new-map") ||
      store.files[2].exists == CTOOL_TRUE ||
      store.files[5].exists == CTOOL_TRUE ||
      store.files[6].exists == CTOOL_TRUE ||
      store.files[8].exists == CTOOL_TRUE) {
    (void)fprintf(stderr, "near-limit recovery record differs\n");
    return 1;
  }
  request.artifact.target = ctool_string("/out");
  request.artifact.backup = ctool_string("/out.cupid-as-old");
  request.artifact.commit = ctool_string("/out.cupid-as-done");
  request.map.target = ctool_string("/map");
  request.map.backup = ctool_string("/map.cupid-as-old");
  request.map.commit = ctool_string("/map.cupid-as-done");

  fake_reset(&store);
  store.fail_write_at = 1u;
  status = as_artifact_publish(&ops, &request);
  if (status != CTOOL_ERR_IO || !fake_has(&store, 0u, "old-output") ||
      !fake_has(&store, 3u, "old-map")) {
    (void)fprintf(stderr, "first publication write rollback differs\n");
    return 1;
  }

  fake_reset(&store);
  store.fail_write_at = 2u;
  status = as_artifact_publish(&ops, &request);
  if (status != CTOOL_ERR_IO || !fake_has(&store, 0u, "old-output") ||
      !fake_has(&store, 3u, "old-map") ||
      store.files[1].exists == CTOOL_TRUE) {
    (void)fprintf(stderr, "second publication write rollback differs\n");
    return 1;
  }

  fake_reset(&store);
  store.fail_write_at = 3u;
  status = as_artifact_publish(&ops, &request);
  if (status != CTOOL_ERR_IO || !fake_has(&store, 0u, "old-output") ||
      !fake_has(&store, 3u, "old-map") ||
      store.files[6].exists == CTOOL_TRUE ||
      store.files[8].exists == CTOOL_TRUE) {
    (void)fprintf(stderr, "commit-marker write rollback differs\n");
    return 1;
  }

  fake_reset(&store);
  store.write_prefix_then_fail_at = 3u;
  status = as_artifact_publish(&ops, &request);
  if (status != CTOOL_ERR_IO || !fake_has(&store, 0u, "old-output") ||
      !fake_has(&store, 3u, "old-map") ||
      store.files[2].exists == CTOOL_TRUE ||
      store.files[5].exists == CTOOL_TRUE ||
      store.files[6].exists == CTOOL_TRUE ||
      store.files[8].exists == CTOOL_TRUE) {
    (void)fprintf(stderr, "partial commit-marker rollback differs\n");
    return 1;
  }

  fake_reset(&store);
  store.write_then_fail_at = 3u;
  store.fail_commit_inspect_at = 3u;
  status = as_artifact_publish(&ops, &request);
  if (status != CTOOL_ERR_IO || !fake_has(&store, 0u, "new-output") ||
      !fake_has(&store, 3u, "new-map") ||
      !fake_has(&store, 2u, "old-output") ||
      !fake_has(&store, 5u, "old-map") ||
      store.files[8].exists == CTOOL_FALSE) {
    (void)fprintf(stderr, "ambiguous commit-marker write was not retained\n");
    return 1;
  }
  store.write_then_fail_at = 0u;
  store.fail_commit_inspect_at = 0u;
  store.fail_write_at = store.write_count + 1u;
  status = as_artifact_publish(&ops, &request);
  if (status != CTOOL_ERR_IO || !fake_has(&store, 0u, "new-output") ||
      !fake_has(&store, 3u, "new-map") ||
      store.files[2].exists == CTOOL_TRUE ||
      store.files[5].exists == CTOOL_TRUE ||
      store.files[6].exists == CTOOL_TRUE ||
      store.files[8].exists == CTOOL_TRUE) {
    (void)fprintf(stderr, "ambiguous commit-marker recovery differs\n");
    return 1;
  }

  fake_reset(&store);
  store.fail_replace_at = 4u;
  status = as_artifact_publish(&ops, &request);
  if (status != CTOOL_ERR_IO || !fake_has(&store, 0u, "old-output") ||
      !fake_has(&store, 3u, "old-map")) {
    (void)fprintf(stderr, "publication replacement rollback differs\n");
    return 1;
  }

  store.fail_replace_at = 0u;
  store.write_count = 0u;
  store.replace_count = 0u;
  status = as_artifact_publish(&ops, &request);
  if (status != CTOOL_OK || !fake_has(&store, 0u, "new-output") ||
      !fake_has(&store, 3u, "new-map") ||
      store.files[1].exists == CTOOL_TRUE ||
      store.files[2].exists == CTOOL_TRUE ||
      store.files[4].exists == CTOOL_TRUE ||
      store.files[5].exists == CTOOL_TRUE ||
      store.files[6].exists == CTOOL_TRUE ||
      store.files[8].exists == CTOOL_TRUE) {
    (void)fprintf(stderr, "publication recovery differs (%s)\n",
                  ctool_status_name(status));
    return 1;
  }

  fake_reset(&store);
  store.fail_remove_at = 1u;
  status = as_artifact_publish(&ops, &request);
  if (status != CTOOL_OK || !fake_has(&store, 0u, "new-output") ||
      !fake_has(&store, 3u, "new-map") ||
      !fake_has(&store, 2u, "old-output") ||
      store.files[6].exists == CTOOL_FALSE ||
      store.files[8].exists == CTOOL_FALSE) {
    (void)fprintf(stderr, "deferred backup cleanup differs\n");
    return 1;
  }
  store.fail_remove_at = 0u;
  store.fail_write_at = store.write_count + 1u;
  status = as_artifact_publish(&ops, &request);
  if (status != CTOOL_ERR_IO || !fake_has(&store, 0u, "new-output") ||
      !fake_has(&store, 3u, "new-map") ||
      store.files[2].exists == CTOOL_TRUE ||
      store.files[6].exists == CTOOL_TRUE ||
      store.files[8].exists == CTOOL_TRUE) {
    (void)fprintf(stderr, "deferred cleanup recovery differs\n");
    return 1;
  }

  fake_reset(&store);
  store.fail_remove_at = 2u;
  status = as_artifact_publish(&ops, &request);
  if (status != CTOOL_OK || !fake_has(&store, 0u, "new-output") ||
      !fake_has(&store, 3u, "new-map") ||
      !fake_has(&store, 5u, "old-map") ||
      store.files[6].exists == CTOOL_FALSE ||
      store.files[8].exists == CTOOL_FALSE) {
    (void)fprintf(stderr, "deferred map cleanup differs\n");
    return 1;
  }
  request.artifact.target = ctool_string("/out2");
  request.artifact.candidate = ctool_string("/out2.new");
  request.artifact.backup = ctool_string("/out2.cupid-as-old");
  request.artifact.commit = ctool_string("/out2.cupid-as-done");
  store.fail_remove_at = 0u;
  store.fail_write_at = store.write_count + 1u;
  status = as_artifact_publish(&ops, &request);
  if (status != CTOOL_ERR_IO || !fake_has(&store, 0u, "new-output") ||
      !fake_has(&store, 3u, "new-map") ||
      store.files[5].exists == CTOOL_TRUE ||
      store.files[6].exists == CTOOL_TRUE ||
      store.files[8].exists == CTOOL_TRUE ||
      store.files[12].exists == CTOOL_TRUE) {
    (void)fprintf(stderr, "changed-artifact map preservation differs\n");
    return 1;
  }
  request.artifact.target = ctool_string("/out");
  request.artifact.candidate = ctool_string("/out.new");
  request.artifact.backup = ctool_string("/out.cupid-as-old");
  request.artifact.commit = ctool_string("/out.cupid-as-done");

  fake_reset(&store);
  store.fail_remove_at = 2u;
  status = as_artifact_publish(&ops, &request);
  if (status != CTOOL_OK || !fake_has(&store, 0u, "new-output") ||
      !fake_has(&store, 3u, "new-map") ||
      !fake_has(&store, 5u, "old-map") ||
      store.files[6].exists == CTOOL_FALSE ||
      store.files[8].exists == CTOOL_FALSE) {
    (void)fprintf(stderr, "changed-map setup cleanup differs\n");
    return 1;
  }
  request.map.target = ctool_string("/map2");
  request.map.candidate = ctool_string("/map2.new");
  request.map.backup = ctool_string("/map2.cupid-as-old");
  request.map.commit = ctool_string("/map2.cupid-as-done");
  store.fail_remove_at = 0u;
  store.fail_write_at = 0u;
  store.write_count = 0u;
  store.replace_count = 0u;
  store.remove_count = 0u;
  status = as_artifact_publish(&ops, &request);
  if (status != CTOOL_OK || !fake_has(&store, 0u, "new-output") ||
      !fake_has(&store, 3u, "new-map") ||
      !fake_has(&store, 9u, "new-map") ||
      store.files[5].exists == CTOOL_TRUE ||
      store.files[8].exists == CTOOL_TRUE) {
    (void)fprintf(stderr, "changed-map cleanup recovery differs\n");
    return 1;
  }
  request.map.target = ctool_string("/map");
  request.map.candidate = ctool_string("/map.new");
  request.map.backup = ctool_string("/map.cupid-as-old");
  request.map.commit = ctool_string("/map.cupid-as-done");
  store.fail_write_at = store.write_count + 1u;
  status = as_artifact_publish(&ops, &request);
  if (status != CTOOL_ERR_IO || !fake_has(&store, 0u, "new-output") ||
      !fake_has(&store, 3u, "new-map") ||
      store.files[5].exists == CTOOL_TRUE) {
    (void)fprintf(stderr, "return-to-map preservation differs\n");
    return 1;
  }

  fake_reset(&store);
  store.fail_replace_at = 4u;
  store.fail_replace_again_at = 6u;
  status = as_artifact_publish(&ops, &request);
  if (status != CTOOL_ERR_IO || !fake_has(&store, 0u, "old-output") ||
      store.files[3].exists == CTOOL_TRUE ||
      !fake_has(&store, 5u, "old-map")) {
    (void)fprintf(stderr, "failed restoration did not retain its backup\n");
    return 1;
  }
  (void)memcpy(store.files[3].bytes, "partial-map", 11u);
  store.files[3].size = 11u;
  store.files[3].exists = CTOOL_TRUE;
  store.fail_replace_at = 0u;
  store.fail_replace_again_at = 0u;
  store.fail_write_at = 1u;
  store.write_count = 0u;
  store.replace_count = 0u;
  status = as_artifact_publish(&ops, &request);
  if (status != CTOOL_ERR_IO || !fake_has(&store, 0u, "old-output") ||
      !fake_has(&store, 3u, "old-map") ||
      store.files[5].exists == CTOOL_TRUE) {
    (void)fprintf(stderr, "retained backup restoration differs\n");
    return 1;
  }
  store.fail_write_at = 0u;
  store.write_count = 0u;
  store.replace_count = 0u;
  status = as_artifact_publish(&ops, &request);
  if (status != CTOOL_OK || !fake_has(&store, 0u, "new-output") ||
      !fake_has(&store, 3u, "new-map") ||
      store.files[5].exists == CTOOL_TRUE) {
    (void)fprintf(stderr, "retained backup recovery differs\n");
    return 1;
  }

  fake_reset(&store);
  (void)memset(&request.map, 0, sizeof(request.map));
  request.map_bytes = ctool_bytes((const void *)0, 0u);
  store.fail_replace_at = 2u;
  status = as_artifact_publish(&ops, &request);
  if (status != CTOOL_ERR_IO || !fake_has(&store, 0u, "old-output") ||
      !fake_has(&store, 3u, "old-map")) {
    (void)fprintf(stderr, "single artifact rollback differs\n");
    return 1;
  }
  store.fail_replace_at = 0u;
  store.write_count = 0u;
  store.replace_count = 0u;
  status = as_artifact_publish(&ops, &request);
  if (status != CTOOL_OK || !fake_has(&store, 0u, "new-output") ||
      !fake_has(&store, 3u, "old-map")) {
    (void)fprintf(stderr, "single artifact recovery differs\n");
    return 1;
  }

  (void)puts("artifact-publication: ok");
  return 0;
}

int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "linked-object") == 0) {
    return run_linked_object();
  }
  if (argc == 2 && strcmp(argv[1], "link-errors") == 0) {
    return run_link_errors();
  }
  if (argc == 2 && strcmp(argv[1], "code-only") == 0) {
    return run_code_only();
  }
  if (argc == 2 && strcmp(argv[1], "code-data-bss") == 0) {
    return run_code_data_bss();
  }
  if (argc == 2 && strcmp(argv[1], "errors") == 0) {
    return run_errors();
  }
  if (argc == 2 && strcmp(argv[1], "artifact-raw") == 0) {
    return run_artifact_raw();
  }
  if (argc == 2 && strcmp(argv[1], "artifact-relocatable") == 0) {
    return run_artifact_relocatable();
  }
  if (argc == 2 && strcmp(argv[1], "artifact-errors") == 0) {
    return run_artifact_errors();
  }
  if (argc == 2 && strcmp(argv[1], "artifact-publication") == 0) {
    return run_artifact_publication();
  }
  (void)fprintf(stderr,
                "usage: cupidasm-kernel-elf-contract "
                "linked-object|link-errors|code-only|code-data-bss|errors|"
                "artifact-raw|artifact-relocatable|artifact-errors|"
                "artifact-publication\n");
  return 2;
}
