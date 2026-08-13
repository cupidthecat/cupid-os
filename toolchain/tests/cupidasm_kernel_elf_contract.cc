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
  ctool_ld_result_t link_result;
  ctool_ld_result_t repeat_result;
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
    status = as_elf32_exec_link(job, &assembly_result, 0x01a00000u,
                                0x00200000u, repeat_output,
                                &repeat_result);
  }
  if (status != CTOOL_OK ||
      memcmp(&repeat_result, &link_result, sizeof(link_result)) != 0 ||
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
  (void)fprintf(stderr,
                "usage: cupidasm-kernel-elf-contract "
                "linked-object|link-errors|code-only|code-data-bss|errors\n");
  return 2;
}
