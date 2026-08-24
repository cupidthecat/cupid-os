#include "ctool.h"
#include "ctool_host.h"
#include "cupidasm.h"

#include <stdio.h>
#include <string.h>

static int check_status(ctool_status_t actual, ctool_status_t expected,
                        const char *operation) {
  if (actual != expected) {
    (void)fprintf(stderr, "%s: expected %s, got %s\n", operation,
                  ctool_status_name(expected), ctool_status_name(actual));
    return 0;
  }
  return 1;
}

typedef struct {
  ctool_string_t path;
  ctool_bytes_t contents;
} asm_contract_file_t;

typedef struct {
  const asm_contract_file_t *files;
  ctool_u32 count;
} asm_contract_store_t;

static int contract_string_equal(ctool_string_t left,
                                 ctool_string_t right) {
  return left.size == right.size &&
                 (left.size == 0u ||
                  memcmp(left.data, right.data, (size_t)left.size) == 0)
             ? 1
             : 0;
}

static const asm_contract_file_t *contract_find_file(
    const asm_contract_store_t *store, ctool_string_t path) {
  ctool_u32 index;
  for (index = 0u; index < store->count; index++) {
    if (contract_string_equal(store->files[index].path, path)) {
      return &store->files[index];
    }
  }
  return (const asm_contract_file_t *)0;
}

static ctool_status_t contract_file_size(void *context,
                                         ctool_string_t path,
                                         ctool_u32 *size_out) {
  const asm_contract_file_t *file = contract_find_file(
      (const asm_contract_store_t *)context, path);
  if (size_out == (ctool_u32 *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  *size_out = 0u;
  if (file == (const asm_contract_file_t *)0) {
    return CTOOL_ERR_NOT_FOUND;
  }
  *size_out = file->contents.size;
  return CTOOL_OK;
}

static ctool_status_t contract_read_exact(void *context,
                                          ctool_string_t path,
                                          ctool_u8 *destination,
                                          ctool_u32 size) {
  const asm_contract_file_t *file = contract_find_file(
      (const asm_contract_store_t *)context, path);
  if (file == (const asm_contract_file_t *)0) {
    return CTOOL_ERR_NOT_FOUND;
  }
  if (file->contents.size != size ||
      (destination == (ctool_u8 *)0 && size != 0u)) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  if (size != 0u) {
    (void)memcpy(destination, file->contents.data, (size_t)size);
  }
  return CTOOL_OK;
}

static int contract_result_is_zero(const ctool_asm_result_t *result) {
  return result->artifact == 0 &&
                 result->bytes.data == (const ctool_u8 *)0 &&
                 result->bytes.size == 0u &&
                 result->regions == (const ctool_asm_region_t *)0 &&
                 result->region_count == 0u &&
                 result->has_entry == CTOOL_FALSE &&
                 result->entry_address == 0u &&
                 result->entry_symbol.data == (const char *)0 &&
                 result->entry_symbol.size == 0u &&
                 result->raw_ranges ==
                     (const ctool_asm_raw_range_t *)0 &&
                 result->raw_range_count == 0u &&
                 result->raw_edges == (const ctool_asm_raw_edge_t *)0 &&
                 result->raw_edge_count == 0u && result->raw_origin == 0u
             ? 1
             : 0;
}

static int contract_has_diagnostic(const ctool_job_t *job,
                                   ctool_u32 code,
                                   const char *expected_path) {
  ctool_u32 index;
  ctool_string_t path = ctool_string(expected_path);
  for (index = 0u; index < ctool_job_diagnostic_count(job); index++) {
    const ctool_diagnostic_t *diagnostic = ctool_job_diagnostic(job, index);
    if (diagnostic != (const ctool_diagnostic_t *)0 &&
        diagnostic->code == code &&
        diagnostic->severity == CTOOL_DIAG_ERROR &&
        contract_string_equal(diagnostic->path, path)) {
      return 1;
    }
  }
  return 0;
}

static int contract_has_diagnostic_message(const ctool_job_t *job,
                                           ctool_u32 code,
                                           const char *expected_path,
                                           const char *expected_message) {
  ctool_u32 index;
  ctool_string_t path = ctool_string(expected_path);
  ctool_string_t message = ctool_string(expected_message);
  for (index = 0u; index < ctool_job_diagnostic_count(job); index++) {
    const ctool_diagnostic_t *diagnostic = ctool_job_diagnostic(job, index);
    if (diagnostic != (const ctool_diagnostic_t *)0 &&
        diagnostic->code == code &&
        diagnostic->severity == CTOOL_DIAG_ERROR &&
        contract_string_equal(diagnostic->path, path) &&
        contract_string_equal(diagnostic->message, message)) {
      return 1;
    }
  }
  return 0;
}

static int contract_has_source_diagnostic(const ctool_job_t *job,
                                          ctool_u32 code,
                                          const char *expected_path,
                                          ctool_u32 expected_line,
                                          ctool_u32 expected_column,
                                          const char *expected_message) {
  ctool_u32 index;
  ctool_string_t path = ctool_string(expected_path);
  ctool_string_t message = ctool_string(expected_message);
  for (index = 0u; index < ctool_job_diagnostic_count(job); index++) {
    const ctool_diagnostic_t *diagnostic = ctool_job_diagnostic(job, index);
    if (diagnostic != (const ctool_diagnostic_t *)0 &&
        diagnostic->code == code &&
        diagnostic->severity == CTOOL_DIAG_ERROR &&
        contract_string_equal(diagnostic->path, path) &&
        diagnostic->line == expected_line &&
        diagnostic->column == expected_column &&
        contract_string_equal(diagnostic->message, message)) {
      return 1;
    }
  }
  return 0;
}

static int expect_assembly_failure(
    const char *name, ctool_job_config_t config, const char *path,
    const char *source_text, const ctool_asm_request_t *request,
    ctool_status_t expected_status, ctool_u32 expected_code,
    const char *expected_path) {
  ctool_job_t *job;
  ctool_buffer_t *output;
  ctool_source_t source;
  ctool_asm_result_t result;
  ctool_bytes_t bytes;
  ctool_u32 initial_capacity = config.limits.output_bytes < 16u
                                   ? config.limits.output_bytes
                                   : 16u;
  ctool_status_t status = ctool_job_open(&config, &job);
  if (!check_status(status, CTOOL_OK, name)) {
    return 0;
  }
  status = ctool_job_open_buffer(job, initial_capacity,
                                 config.limits.output_bytes, &output);
  if (!check_status(status, CTOOL_OK, name)) {
    ctool_job_close(job);
    return 0;
  }
  source.path.text = ctool_string(path);
  source.contents = ctool_bytes(source_text, (ctool_u32)strlen(source_text));
  (void)memset(&result, 0xa5, sizeof(result));
  status = ctool_asm_assemble(job, &source, request, output, &result);
  bytes = ctool_buffer_view(output);
  if (!check_status(status, expected_status, name) || bytes.size != 0u ||
      !contract_result_is_zero(&result) ||
      !contract_has_diagnostic(job, expected_code, expected_path)) {
    (void)fprintf(stderr, "%s failure contract differs\n", name);
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 0;
  }
  ctool_buffer_close(output);
  ctool_job_close(job);
  return 1;
}

static const ctool_elf32_section_t *find_section(
    const ctool_elf32_object_t *object, const char *name) {
  ctool_u32 index;
  size_t size = strlen(name);
  for (index = 0u; index < object->section_count; index++) {
    const ctool_elf32_section_t *section = &object->sections[index];
    if ((size_t)section->name.size == size &&
        memcmp(section->name.data, name, size) == 0) {
      return section;
    }
  }
  return (const ctool_elf32_section_t *)0;
}

static const ctool_elf32_symbol_t *find_symbol(
    const ctool_elf32_object_t *object, const char *name) {
  ctool_u32 index;
  size_t size = strlen(name);
  for (index = 0u; index < object->symbol_count; index++) {
    const ctool_elf32_symbol_t *symbol = &object->symbols[index];
    if ((size_t)symbol->name.size == size &&
        memcmp(symbol->name.data, name, size) == 0) {
      return symbol;
    }
  }
  return (const ctool_elf32_symbol_t *)0;
}

static int run_raw_basic(void) {
  static const char source_text[] =
      "BITS 16\n"
      "ORG 0x7c00\n"
      "start:\n"
      "    jmp .done\n"
      "    mov ax, 0x1234\n"
      ".done:\n"
      "    mov [byte_slot], dl\n"
      "byte_slot: db 0\n"
      "    dw 0xaa55\n"
      "    cmovne ax, cx\n"
      "    cmovnz eax, [bx + si + 0x7f]\n"
      "    setp dl\n"
      "    a32 setnp byte [ebx + ecx * 4 + 0x12345678]\n"
      "BITS 32\n"
      "    cmovg ax, [ebx + 0x7f]\n"
      "    cmovnle eax, ecx\n"
      "    setnp dl\n"
      "    a16 setp byte [bx + si + 0x7f]\n"
      "    cmovc eax, ecx\n"
      "    cmovnae eax, ecx\n"
      "    cmovnc eax, ecx\n"
      "    cmovnb eax, ecx\n"
      "    cmovz eax, ecx\n"
      "    cmovna eax, ecx\n"
      "    cmovnbe eax, ecx\n"
      "    cmovpe eax, ecx\n"
      "    cmovpo eax, ecx\n"
      "    cmovnge eax, ecx\n"
      "    cmovnl eax, ecx\n"
      "    cmovng eax, ecx\n"
      "BITS 16\n"
      "    imul ax, cx, 0x1234\n"
      "    imul eax, [bx + si + 0x7f], 0x12345678\n"
      "BITS 32\n"
      "    imul eax, ecx, 0x228\n"
      "    imul esi, [ebx + ecx * 4 + 0x12345678], -7\n"
      "    imul dx, [ebx + 0x7f], -2\n"
      "    nop\n"
      "    nop eax\n"
      "    nop ax\n"
      "    nop [eax]\n"
      "    nop word [eax]\n"
      "    nop dword [eax + eax * 1 + 0x12345678]\n"
      "    a16 nop [bx + si + 0x7f]\n"
      "    nop word [cs:eax + eax]\n"
      "BITS 16\n"
      "    nop eax\n"
      "    nop ax\n"
      "    nop [bx + si]\n"
      "    nop dword [bx + si]\n"
      "    a32 nop dword [ebx + ecx * 4 + 0x12345678]\n";
  static const ctool_u8 expected[] = {
      0xebu, 0x03u, 0xb8u, 0x34u, 0x12u, 0x88u,
      0x16u, 0x09u, 0x7cu, 0x00u, 0x55u, 0xaau,
      0x0fu, 0x45u, 0xc1u,
      0x66u, 0x0fu, 0x45u, 0x40u, 0x7fu,
      0x0fu, 0x9au, 0xc2u,
      0x67u, 0x0fu, 0x9bu, 0x84u, 0x8bu,
      0x78u, 0x56u, 0x34u, 0x12u,
      0x66u, 0x0fu, 0x4fu, 0x43u, 0x7fu,
      0x0fu, 0x4fu, 0xc1u,
      0x0fu, 0x9bu, 0xc2u,
      0x67u, 0x0fu, 0x9au, 0x40u, 0x7fu,
      0x0fu, 0x42u, 0xc1u, 0x0fu, 0x42u, 0xc1u,
      0x0fu, 0x43u, 0xc1u, 0x0fu, 0x43u, 0xc1u,
      0x0fu, 0x44u, 0xc1u, 0x0fu, 0x46u, 0xc1u,
      0x0fu, 0x47u, 0xc1u, 0x0fu, 0x4au, 0xc1u,
      0x0fu, 0x4bu, 0xc1u, 0x0fu, 0x4cu, 0xc1u,
      0x0fu, 0x4du, 0xc1u, 0x0fu, 0x4eu, 0xc1u,
      0x69u, 0xc1u, 0x34u, 0x12u,
      0x66u, 0x69u, 0x40u, 0x7fu, 0x78u, 0x56u, 0x34u, 0x12u,
      0x69u, 0xc1u, 0x28u, 0x02u, 0x00u, 0x00u,
      0x6bu, 0xb4u, 0x8bu, 0x78u, 0x56u, 0x34u, 0x12u, 0xf9u,
      0x66u, 0x6bu, 0x53u, 0x7fu, 0xfeu,
      0x90u,
      0x0fu, 0x1fu, 0xc0u,
      0x66u, 0x0fu, 0x1fu, 0xc0u,
      0x0fu, 0x1fu, 0x00u,
      0x66u, 0x0fu, 0x1fu, 0x00u,
      0x0fu, 0x1fu, 0x84u, 0x00u, 0x78u, 0x56u, 0x34u, 0x12u,
      0x67u, 0x0fu, 0x1fu, 0x40u, 0x7fu,
      0x2eu, 0x66u, 0x0fu, 0x1fu, 0x04u, 0x00u,
      0x66u, 0x0fu, 0x1fu, 0xc0u,
      0x0fu, 0x1fu, 0xc0u,
      0x0fu, 0x1fu, 0x00u,
      0x66u, 0x0fu, 0x1fu, 0x00u,
      0x66u, 0x67u, 0x0fu, 0x1fu, 0x84u, 0x8bu,
      0x78u, 0x56u, 0x34u, 0x12u};
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_buffer_t *output;
  ctool_source_t source;
  ctool_asm_request_t request;
  ctool_asm_result_t result;
  ctool_bytes_t bytes;
  ctool_status_t status;

  status = ctool_host_adapter_init(&adapter, ".");
  if (!check_status(status, CTOOL_OK, "host adapter init")) {
    return 1;
  }
  config = ctool_host_job_config(&adapter, ctool_default_limits());
  status = ctool_job_open(&config, &job);
  if (!check_status(status, CTOOL_OK, "job open")) {
    return 1;
  }
  status = ctool_job_open_buffer(job, 16u,
                                 ctool_default_limits().output_bytes,
                                 &output);
  if (!check_status(status, CTOOL_OK, "output open")) {
    ctool_job_close(job);
    return 1;
  }
  if (ctool_buffer_view(output).size != 0u) {
    (void)fprintf(stderr, "new assembly output is not empty\n");
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  source.path.text = ctool_string("/raw-basic.asm");
  source.contents =
      ctool_bytes(source_text, (ctool_u32)(sizeof(source_text) - 1u));
  (void)memset(&request, 0, sizeof(request));
  request.artifact = CTOOL_ASM_ARTIFACT_RAW;
  request.initial_mode = CTOOL_X86_MODE_32;
  (void)memset(&result, 0xa5, sizeof(result));

  status = ctool_asm_assemble(job, &source, &request, output, &result);
  bytes = ctool_buffer_view(output);
  if (!check_status(status, CTOOL_OK, "raw basic assembly") ||
      bytes.size != (ctool_u32)sizeof(expected) ||
      memcmp(bytes.data, expected, sizeof(expected)) != 0) {
    (void)fprintf(stderr, "raw basic bytes differ\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }
  if (result.artifact != CTOOL_ASM_ARTIFACT_RAW ||
      result.bytes.data != bytes.data || result.bytes.size != bytes.size ||
      result.regions != (const ctool_asm_region_t *)0 ||
      result.region_count != 0u || result.has_entry != CTOOL_FALSE ||
      result.entry_address != 0u || ctool_job_diagnostic_count(job) != 0u) {
    (void)fprintf(stderr, "raw basic artifact invariants differ\n");
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  ctool_buffer_close(output);
  ctool_job_close(job);
  (void)puts("raw-basic: ok");
  return 0;
}

static int run_raw_expressions(void) {
  static const char source_text[] =
      "[BITS 16]\n"
      "[ORG 0x100]\n"
      "%define COUNT 2\n"
      "SCALE equ COUNT + 1\n"
      "start:\n"
      "    db 'A', 10000010b\n"
      "    dw SCALE\n"
      "    dd ~(1 << 8)\n"
      "    dq 0x00CF9A000000FFFF\n"
      "done:\n"
      "    dw done - start\n"
      "    times 24 - ($ - $$) db 0xcc\n";
  static const ctool_u8 expected[] = {
      0x41u, 0x82u, 0x03u, 0x00u, 0xffu, 0xfeu, 0xffu, 0xffu,
      0xffu, 0xffu, 0x00u, 0x00u, 0x00u, 0x9au, 0xcfu, 0x00u,
      0x10u, 0x00u, 0xccu, 0xccu, 0xccu, 0xccu, 0xccu, 0xccu};
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_buffer_t *output;
  ctool_source_t source;
  ctool_asm_request_t request;
  ctool_asm_result_t result;
  ctool_bytes_t bytes;
  ctool_status_t status;

  status = ctool_host_adapter_init(&adapter, ".");
  if (!check_status(status, CTOOL_OK, "host adapter init")) {
    return 1;
  }
  config = ctool_host_job_config(&adapter, ctool_default_limits());
  status = ctool_job_open(&config, &job);
  if (!check_status(status, CTOOL_OK, "job open")) {
    return 1;
  }
  status = ctool_job_open_buffer(job, 32u,
                                 ctool_default_limits().output_bytes,
                                 &output);
  if (!check_status(status, CTOOL_OK, "output open")) {
    ctool_job_close(job);
    return 1;
  }
  source.path.text = ctool_string("/raw-expressions.asm");
  source.contents =
      ctool_bytes(source_text, (ctool_u32)(sizeof(source_text) - 1u));
  (void)memset(&request, 0, sizeof(request));
  request.artifact = CTOOL_ASM_ARTIFACT_RAW;
  request.initial_mode = CTOOL_X86_MODE_32;
  (void)memset(&result, 0, sizeof(result));

  status = ctool_asm_assemble(job, &source, &request, output, &result);
  bytes = ctool_buffer_view(output);
  if (!check_status(status, CTOOL_OK, "raw expression assembly") ||
      bytes.size != (ctool_u32)sizeof(expected) ||
      memcmp(bytes.data, expected, sizeof(expected)) != 0) {
    (void)fprintf(stderr, "raw expression bytes differ\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }
  if (result.artifact != CTOOL_ASM_ARTIFACT_RAW ||
      result.bytes.data != bytes.data || result.bytes.size != bytes.size ||
      ctool_job_diagnostic_count(job) != 0u) {
    (void)fprintf(stderr, "raw expression artifact invariants differ\n");
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }
  ctool_buffer_close(output);
  ctool_job_close(job);
  (void)puts("raw-expressions: ok");
  return 0;
}

static int run_object_basic(void) {
  static const char source_text[] =
      "[BITS 32]\n"
      "extern target\n"
      "extern slot\n"
      "global entry\n"
      "section .text\n"
      "entry:\n"
      " call target\n"
      " mov eax, [value]\n"
      " call dword [slot]\n"
      " ret 4\n"
      "section .data\n"
      "value: dd entry\n";
  static const ctool_u8 expected_text[] = {
      0xe8u, 0xfcu, 0xffu, 0xffu, 0xffu, 0xa1u,
      0x00u, 0x00u, 0x00u, 0x00u, 0xffu, 0x15u,
      0x00u, 0x00u, 0x00u, 0x00u, 0xc2u, 0x04u, 0x00u};
  static const ctool_u8 expected_data[] = {0u, 0u, 0u, 0u};
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_buffer_t *output;
  ctool_source_t source;
  ctool_source_t object_source;
  ctool_asm_request_t request;
  ctool_asm_result_t result;
  ctool_elf32_object_t object;
  const ctool_elf32_section_t *text;
  const ctool_elf32_section_t *data;
  const ctool_elf32_symbol_t *entry;
  const ctool_elf32_symbol_t *value;
  const ctool_elf32_symbol_t *target;
  const ctool_elf32_symbol_t *slot;
  ctool_bytes_t bytes;
  ctool_status_t status;

  status = ctool_host_adapter_init(&adapter, ".");
  if (!check_status(status, CTOOL_OK, "host adapter init")) {
    return 1;
  }
  config = ctool_host_job_config(&adapter, ctool_default_limits());
  status = ctool_job_open(&config, &job);
  if (!check_status(status, CTOOL_OK, "job open")) {
    return 1;
  }
  status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes,
                                 &output);
  if (!check_status(status, CTOOL_OK, "object output open")) {
    ctool_job_close(job);
    return 1;
  }
  source.path.text = ctool_string("/object-basic.asm");
  source.contents =
      ctool_bytes(source_text, (ctool_u32)(sizeof(source_text) - 1u));
  (void)memset(&request, 0, sizeof(request));
  request.artifact = CTOOL_ASM_ARTIFACT_ELF32_REL;
  request.initial_mode = CTOOL_X86_MODE_16;
  (void)memset(&result, 0xa5, sizeof(result));
  (void)memset(&object, 0, sizeof(object));

  status = ctool_asm_assemble(job, &source, &request, output, &result);
  bytes = ctool_buffer_view(output);
  if (status == CTOOL_OK) {
    object_source.path.text = ctool_string("/object-basic.o");
    object_source.contents = result.bytes;
    status = ctool_elf32_read(job, &object_source, &object);
  }
  text = find_section(&object, ".text");
  data = find_section(&object, ".data");
  entry = find_symbol(&object, "entry");
  value = find_symbol(&object, "value");
  target = find_symbol(&object, "target");
  slot = find_symbol(&object, "slot");
  if (!check_status(status, CTOOL_OK, "basic ELF32 object assembly") ||
      result.artifact != CTOOL_ASM_ARTIFACT_ELF32_REL ||
      result.bytes.data != bytes.data || result.bytes.size != bytes.size ||
      result.regions != (const ctool_asm_region_t *)0 ||
      result.region_count != 0u || result.has_entry != CTOOL_FALSE ||
      result.entry_symbol.data != (const char *)0 ||
      result.entry_symbol.size != 0u || result.entry_address != 0u ||
      object.file_type != CTOOL_ELF32_ET_REL ||
      text == (const ctool_elf32_section_t *)0 ||
      data == (const ctool_elf32_section_t *)0) {
    (void)fprintf(stderr, "basic ELF32 object artifact differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }
  if (text->file_index != 1u ||
      text->type != CTOOL_ELF32_SHT_PROGBITS ||
      text->flags !=
          (CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR) ||
      text->alignment != 16u ||
      text->contents.size != (ctool_u32)sizeof(expected_text) ||
      memcmp(text->contents.data, expected_text, sizeof(expected_text)) != 0 ||
      data->file_index != 2u ||
      data->type != CTOOL_ELF32_SHT_PROGBITS ||
      data->flags != (CTOOL_ELF32_SHF_WRITE | CTOOL_ELF32_SHF_ALLOC) ||
      data->alignment != 4u ||
      data->contents.size != (ctool_u32)sizeof(expected_data) ||
      memcmp(data->contents.data, expected_data, sizeof(expected_data)) != 0) {
    (void)fprintf(stderr, "basic ELF32 object sections differ\n");
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }
  if (entry == (const ctool_elf32_symbol_t *)0 ||
      entry->binding != CTOOL_ELF32_BIND_GLOBAL ||
      entry->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
      entry->section_file_index != text->file_index || entry->value != 0u ||
      value == (const ctool_elf32_symbol_t *)0 ||
      value->binding != CTOOL_ELF32_BIND_LOCAL ||
      value->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
      value->section_file_index != data->file_index || value->value != 0u ||
      target == (const ctool_elf32_symbol_t *)0 ||
      target->binding != CTOOL_ELF32_BIND_GLOBAL ||
      target->placement != CTOOL_ELF32_SYMBOL_UNDEFINED ||
      target->section_file_index != CTOOL_ELF32_NO_SECTION ||
      target->value != 0u || slot == (const ctool_elf32_symbol_t *)0 ||
      slot->binding != CTOOL_ELF32_BIND_GLOBAL ||
      slot->placement != CTOOL_ELF32_SYMBOL_UNDEFINED ||
      slot->section_file_index != CTOOL_ELF32_NO_SECTION ||
      slot->value != 0u) {
    (void)fprintf(stderr, "basic ELF32 object symbols differ\n");
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }
  if (object.relocation_count != 4u || text->relocation_first != 0u ||
      text->relocation_count != 3u || data->relocation_first != 3u ||
      data->relocation_count != 1u ||
      object.relocations[0].target_section_file_index != text->file_index ||
      object.relocations[0].offset != 1u ||
      object.relocations[0].symbol_file_index != target->file_index ||
      object.relocations[0].type != CTOOL_ELF32_R_386_PC32 ||
      object.relocations[0].addend_known == CTOOL_FALSE ||
      object.relocations[0].addend != -4 ||
      object.relocations[1].target_section_file_index != text->file_index ||
      object.relocations[1].offset != 6u ||
      object.relocations[1].symbol_file_index != value->file_index ||
      object.relocations[1].type != CTOOL_ELF32_R_386_32 ||
      object.relocations[1].addend_known == CTOOL_FALSE ||
      object.relocations[1].addend != 0 ||
      object.relocations[2].target_section_file_index != text->file_index ||
      object.relocations[2].offset != 12u ||
      object.relocations[2].symbol_file_index != slot->file_index ||
      object.relocations[2].type != CTOOL_ELF32_R_386_32 ||
      object.relocations[2].addend_known == CTOOL_FALSE ||
      object.relocations[2].addend != 0 ||
      object.relocations[3].target_section_file_index != data->file_index ||
      object.relocations[3].offset != 0u ||
      object.relocations[3].symbol_file_index != entry->file_index ||
      object.relocations[3].type != CTOOL_ELF32_R_386_32 ||
      object.relocations[3].addend_known == CTOOL_FALSE ||
      object.relocations[3].addend != 0) {
    (void)fprintf(stderr, "basic ELF32 object relocations differ\n");
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  ctool_buffer_close(output);
  ctool_job_close(job);
  (void)puts("object-basic: ok");
  return 0;
}

static int run_object_entry(void) {
  static const char missing_source_text[] =
      "BITS 32\n"
      "section .text\n"
      "helper: ret\n";
  static const char source_text[] =
      "BITS 32\n"
      "section .text\n"
      "_start: ret\n"
      "main: ret\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_buffer_t *output;
  ctool_source_t source;
  ctool_source_t object_source;
  ctool_asm_request_t request;
  ctool_asm_result_t result;
  ctool_elf32_object_t object;
  const ctool_elf32_symbol_t *main_symbol;
  const ctool_elf32_symbol_t *start_symbol;
  ctool_string_t saved_entry_candidate;
  ctool_string_t entry_candidates[2];
  ctool_bytes_t first_bytes;
  ctool_u8 *first_copy;
  ctool_status_t status;

  status = ctool_host_adapter_init(&adapter, ".");
  if (!check_status(status, CTOOL_OK, "host adapter init")) {
    return 1;
  }
  config = ctool_host_job_config(&adapter, ctool_default_limits());
  status = ctool_job_open(&config, &job);
  if (!check_status(status, CTOOL_OK, "entry metadata job open")) {
    return 1;
  }
  status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes,
                                 &output);
  if (!check_status(status, CTOOL_OK, "entry metadata output open")) {
    ctool_job_close(job);
    return 1;
  }
  entry_candidates[0] = ctool_string("main");
  entry_candidates[1] = ctool_string("_start");
  (void)memset(&request, 0, sizeof(request));
  request.artifact = CTOOL_ASM_ARTIFACT_ELF32_REL;
  request.initial_mode = CTOOL_X86_MODE_32;
  request.entry_candidates = entry_candidates;
  request.entry_candidate_count = 2u;

  source.path.text = ctool_string("/object-entry-missing.asm");
  source.contents = ctool_bytes(
      missing_source_text,
      (ctool_u32)(sizeof(missing_source_text) - 1u));
  (void)memset(&result, 0xa5, sizeof(result));
  status = ctool_asm_assemble(job, &source, &request, output, &result);
  if (!check_status(status, CTOOL_ERR_NOT_FOUND,
                    "missing object entry") ||
      ctool_buffer_view(output).size != 0u ||
      !contract_result_is_zero(&result) ||
      ctool_job_diagnostic_count(job) != 1u ||
      !contract_has_diagnostic(job, CTOOL_ASM_DIAG_ENTRY,
                               "/object-entry-missing.asm") ||
      !contract_has_diagnostic_message(
          job, CTOOL_ASM_DIAG_ENTRY, "/object-entry-missing.asm",
          "none of the assembly entry candidates is defined in code")) {
    (void)fprintf(stderr, "missing object entry rollback differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  source.path.text = ctool_string("/object-entry.asm");
  source.contents = ctool_bytes(
      source_text, (ctool_u32)(sizeof(source_text) - 1u));
  (void)memset(&result, 0xa5, sizeof(result));
  (void)memset(&object, 0, sizeof(object));
  status = ctool_asm_assemble(job, &source, &request, output, &result);
  if (status == CTOOL_OK) {
    object_source.path.text = ctool_string("/object-entry.o");
    object_source.contents = result.bytes;
    status = ctool_elf32_read(job, &object_source, &object);
  }
  main_symbol = find_symbol(&object, "main");
  start_symbol = find_symbol(&object, "_start");
  if (!check_status(status, CTOOL_OK, "object entry recovery") ||
      result.artifact != CTOOL_ASM_ARTIFACT_ELF32_REL ||
      result.has_entry != CTOOL_TRUE || result.entry_address != 0u ||
      !contract_string_equal(result.entry_symbol, ctool_string("main")) ||
      main_symbol == (const ctool_elf32_symbol_t *)0 ||
      main_symbol->binding != CTOOL_ELF32_BIND_GLOBAL ||
      start_symbol == (const ctool_elf32_symbol_t *)0 ||
      start_symbol->binding != CTOOL_ELF32_BIND_LOCAL ||
      ctool_job_diagnostic_count(job) != 1u) {
    (void)fprintf(stderr, "object entry metadata differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }
  first_bytes = ctool_buffer_view(output);
  status = ctool_arena_alloc(ctool_job_arena(job), first_bytes.size, 1u,
                             (void **)&first_copy);
  if (status == CTOOL_OK) {
    (void)memcpy(first_copy, first_bytes.data, first_bytes.size);
    status = ctool_buffer_rewind(output, 0u);
  }
  if (status == CTOOL_OK) {
    (void)memset(&result, 0xa5, sizeof(result));
    status = ctool_asm_assemble(job, &source, &request, output, &result);
  }
  if (!check_status(status, CTOOL_OK, "object entry repeat") ||
      result.has_entry != CTOOL_TRUE ||
      !contract_string_equal(result.entry_symbol, ctool_string("main")) ||
      ctool_buffer_view(output).size != first_bytes.size ||
      memcmp(ctool_buffer_view(output).data, first_copy,
             first_bytes.size) != 0 ||
      ctool_job_diagnostic_count(job) != 1u) {
    (void)fprintf(stderr, "object entry repeat differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  status = ctool_buffer_rewind(output, 0u);
  entry_candidates[0] = ctool_string("absent");
  if (status == CTOOL_OK) {
    (void)memset(&result, 0xa5, sizeof(result));
    status = ctool_asm_assemble(job, &source, &request, output, &result);
  }
  if (!check_status(status, CTOOL_OK, "object _start fallback") ||
      result.has_entry != CTOOL_TRUE || result.entry_address != 0u ||
      !contract_string_equal(result.entry_symbol, ctool_string("_start"))) {
    (void)fprintf(stderr, "object _start fallback differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }
  (void)memset(&object, 0, sizeof(object));
  object_source.contents = result.bytes;
  status = ctool_elf32_read(job, &object_source, &object);
  start_symbol = find_symbol(&object, "_start");
  if (!check_status(status, CTOOL_OK, "object _start read") ||
      start_symbol == (const ctool_elf32_symbol_t *)0 ||
      start_symbol->binding != CTOOL_ELF32_BIND_GLOBAL) {
    (void)fprintf(stderr, "object _start visibility differs\n");
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  status = ctool_buffer_rewind(output, 0u);
  saved_entry_candidate = entry_candidates[0];
  entry_candidates[0] = ctool_string("bad entry");
  (void)memset(&result, 0xa5, sizeof(result));
  if (status == CTOOL_OK) {
    status = ctool_asm_assemble(job, &source, &request, output, &result);
  }
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "malformed object entry") ||
      ctool_buffer_view(output).size != 0u ||
      !contract_result_is_zero(&result) ||
      ctool_job_diagnostic_count(job) != 2u ||
      !contract_has_diagnostic(job, CTOOL_ASM_DIAG_INVALID_REQUEST,
                               "/object-entry.asm") ||
      !contract_has_diagnostic_message(
          job, CTOOL_ASM_DIAG_INVALID_REQUEST, "/object-entry.asm",
          "invalid assembly entry candidate")) {
    (void)fprintf(stderr, "malformed object entry rollback differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }
  entry_candidates[0] = saved_entry_candidate;
  (void)memset(&result, 0xa5, sizeof(result));
  status = ctool_asm_assemble(job, &source, &request, output, &result);
  if (!check_status(status, CTOOL_OK, "malformed entry recovery") ||
      result.has_entry != CTOOL_TRUE ||
      !contract_string_equal(result.entry_symbol, ctool_string("_start")) ||
      ctool_job_diagnostic_count(job) != 2u) {
    (void)fprintf(stderr, "malformed object entry recovery differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  ctool_buffer_close(output);
  ctool_job_close(job);
  (void)puts("object-entry: ok");
  return 0;
}

static int run_object_symbolic_immediate(void) {
  static const char source_text[] =
      "BITS 32\n"
      "section .data\n"
      "message: db 0\n"
      "section .text\n"
      "global entry\n"
      "entry:\n"
      " push message\n"
      " ret\n";
  static const ctool_u8 expected_text[] = {
      0x68u, 0x00u, 0x00u, 0x00u, 0x00u, 0xc3u};
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_buffer_t *output;
  ctool_source_t source;
  ctool_source_t object_source;
  ctool_asm_request_t request;
  ctool_asm_result_t result;
  ctool_elf32_object_t object;
  const ctool_elf32_section_t *text;
  const ctool_elf32_symbol_t *message;
  ctool_status_t status;

  status = ctool_host_adapter_init(&adapter, ".");
  if (!check_status(status, CTOOL_OK, "host adapter init")) {
    return 1;
  }
  config = ctool_host_job_config(&adapter, ctool_default_limits());
  status = ctool_job_open(&config, &job);
  if (!check_status(status, CTOOL_OK, "job open")) {
    return 1;
  }
  status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes,
                                 &output);
  if (!check_status(status, CTOOL_OK, "object output open")) {
    ctool_job_close(job);
    return 1;
  }
  source.path.text = ctool_string("/object-symbolic-immediate.asm");
  source.contents =
      ctool_bytes(source_text, (ctool_u32)(sizeof(source_text) - 1u));
  (void)memset(&request, 0, sizeof(request));
  request.artifact = CTOOL_ASM_ARTIFACT_ELF32_REL;
  request.initial_mode = CTOOL_X86_MODE_32;
  (void)memset(&result, 0xa5, sizeof(result));
  (void)memset(&object, 0, sizeof(object));

  status = ctool_asm_assemble(job, &source, &request, output, &result);
  if (status == CTOOL_OK) {
    object_source.path.text = ctool_string("/object-symbolic-immediate.o");
    object_source.contents = result.bytes;
    status = ctool_elf32_read(job, &object_source, &object);
  }
  text = find_section(&object, ".text");
  message = find_symbol(&object, "message");
  if (!check_status(status, CTOOL_OK, "symbolic immediate ELF32 assembly") ||
      text == (const ctool_elf32_section_t *)0 ||
      text->contents.size != (ctool_u32)sizeof(expected_text) ||
      memcmp(text->contents.data, expected_text, sizeof(expected_text)) != 0 ||
      message == (const ctool_elf32_symbol_t *)0 ||
      message->binding != CTOOL_ELF32_BIND_LOCAL ||
      message->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
      object.relocation_count != 1u || text->relocation_count != 1u ||
      object.relocations[0].target_section_file_index != text->file_index ||
      object.relocations[0].offset != 1u ||
      object.relocations[0].symbol_file_index != message->file_index ||
      object.relocations[0].type != CTOOL_ELF32_R_386_32 ||
      object.relocations[0].addend_known == CTOOL_FALSE ||
      object.relocations[0].addend != 0 ||
      ctool_job_diagnostic_count(job) != 0u) {
    (void)fprintf(stderr,
                  "symbolic immediate width or relocation differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  ctool_buffer_close(output);
  ctool_job_close(job);
  (void)puts("object-symbolic-immediate: ok");
  return 0;
}

static int run_fixed_image(void) {
  static const char source_text[] =
      "BITS 32\n"
      "section .data\n"
      "message: db 72, 105, 0\n"
      "section .text\n"
      "Main:\n"
      " push message\n"
      " call PRINT\n"
      " ret\n"
      " jmp message\n";
  static const char collision_source_text[] =
      "BITS 32\n"
      "section .text\n"
      "main: ret\n"
      "Foo: ret\n"
      "foo: ret\n";
  static const ctool_u8 expected_code[] = {
      0x68u, 0x00u, 0x00u, 0xb0u, 0x01u, 0xe8u,
      0x26u, 0x20u, 0x70u, 0xfeu, 0xc3u, 0xe9u,
      0xf0u, 0xffu, 0x0fu, 0x00u};
  static const ctool_u8 expected_data[] = {72u, 105u, 0u};
  static const char text_name[] = ".text";
  static const char data_name[] = ".data";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_buffer_t *output;
  ctool_source_t source;
  ctool_asm_definition_t definitions[1];
  ctool_string_t entry_candidates[2];
  ctool_asm_request_t request;
  ctool_asm_result_t result;
  const ctool_asm_region_t *text;
  const ctool_asm_region_t *data;
  const ctool_diagnostic_t *diagnostic;
  ctool_bytes_t bytes;
  ctool_status_t status;

  status = ctool_host_adapter_init(&adapter, ".");
  if (!check_status(status, CTOOL_OK, "host adapter init")) {
    return 1;
  }
  config = ctool_host_job_config(&adapter, ctool_default_limits());
  status = ctool_job_open(&config, &job);
  if (!check_status(status, CTOOL_OK, "job open")) {
    return 1;
  }
  status = ctool_job_open_buffer(job, 32u, config.limits.output_bytes,
                                 &output);
  if (!check_status(status, CTOOL_OK, "fixed image output open")) {
    ctool_job_close(job);
    return 1;
  }
  source.path.text = ctool_string("/fixed-image.asm");
  source.contents =
      ctool_bytes(source_text, (ctool_u32)(sizeof(source_text) - 1u));
  definitions[0].name = ctool_string("print");
  definitions[0].kind = CTOOL_ASM_DEFINE_ABSOLUTE;
  definitions[0].value = 0x00102030u;
  entry_candidates[0] = ctool_string("main");
  entry_candidates[1] = ctool_string("_start");
  (void)memset(&request, 0, sizeof(request));
  request.artifact = CTOOL_ASM_ARTIFACT_FIXED_IMAGE;
  request.initial_mode = CTOOL_X86_MODE_16;
  request.definitions = definitions;
  request.definition_count = 1u;
  request.entry_candidates = entry_candidates;
  request.entry_candidate_count = 2u;
  request.case_insensitive_symbols = CTOOL_TRUE;
  request.as.fixed.code.base_address = 0x01a00000u;
  request.as.fixed.code.maximum_bytes = 0x1000u;
  request.as.fixed.data.base_address = 0x01b00000u;
  request.as.fixed.data.maximum_bytes = 0x1000u;
  (void)memset(&result, 0xa5, sizeof(result));

  status = ctool_asm_assemble(job, &source, &request, output, &result);
  bytes = ctool_buffer_view(output);
  text = result.region_count >= 1u ? &result.regions[0]
                                   : (const ctool_asm_region_t *)0;
  data = result.region_count >= 2u ? &result.regions[1]
                                   : (const ctool_asm_region_t *)0;
  if (!check_status(status, CTOOL_OK, "fixed image assembly") ||
      result.artifact != CTOOL_ASM_ARTIFACT_FIXED_IMAGE ||
      result.bytes.data != bytes.data || result.bytes.size != bytes.size ||
      bytes.size !=
          (ctool_u32)(sizeof(expected_code) + sizeof(expected_data)) ||
      memcmp(bytes.data, expected_code, sizeof(expected_code)) != 0 ||
      memcmp(bytes.data + sizeof(expected_code), expected_data,
             sizeof(expected_data)) != 0 ||
      result.region_count != 2u || text == (const ctool_asm_region_t *)0 ||
      data == (const ctool_asm_region_t *)0 ||
      text->name.size != (ctool_u32)(sizeof(text_name) - 1u) ||
      memcmp(text->name.data, text_name, sizeof(text_name) - 1u) != 0 ||
      text->address != 0x01a00000u || text->output_offset != 0u ||
      text->file_size != (ctool_u32)sizeof(expected_code) ||
      text->memory_size != (ctool_u32)sizeof(expected_code) ||
      text->flags !=
          (CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR) ||
      data->name.size != (ctool_u32)(sizeof(data_name) - 1u) ||
      memcmp(data->name.data, data_name, sizeof(data_name) - 1u) != 0 ||
      data->address != 0x01b00000u ||
      data->output_offset != (ctool_u32)sizeof(expected_code) ||
      data->file_size != (ctool_u32)sizeof(expected_data) ||
      data->memory_size != (ctool_u32)sizeof(expected_data) ||
      data->flags != (CTOOL_ELF32_SHF_WRITE | CTOOL_ELF32_SHF_ALLOC) ||
      result.has_entry != CTOOL_TRUE ||
      result.entry_address != 0x01a00000u ||
      ctool_job_diagnostic_count(job) != 0u) {
    (void)fprintf(stderr, "fixed image artifact differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  status = ctool_buffer_rewind(output, 0u);
  request.as.fixed.code.maximum_bytes =
      (ctool_u32)(sizeof(expected_code) - 1u);
  (void)memset(&result, 0xa5, sizeof(result));
  if (status == CTOOL_OK) {
    status = ctool_asm_assemble(job, &source, &request, output, &result);
  }
  bytes = ctool_buffer_view(output);
  diagnostic = ctool_job_diagnostic(job, 0u);
  if (!check_status(status, CTOOL_ERR_LIMIT, "fixed code region limit") ||
      bytes.size != 0u || result.artifact != 0 ||
      result.bytes.data != (const ctool_u8 *)0 || result.bytes.size != 0u ||
      result.regions != (const ctool_asm_region_t *)0 ||
      result.region_count != 0u || result.has_entry != CTOOL_FALSE ||
      result.entry_address != 0u || ctool_job_diagnostic_count(job) != 1u ||
      diagnostic == (const ctool_diagnostic_t *)0 ||
      diagnostic->code != CTOOL_ASM_DIAG_LAYOUT ||
      diagnostic->severity != CTOOL_DIAG_ERROR) {
    (void)fprintf(stderr, "fixed image region limit differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  status = ctool_buffer_rewind(output, 0u);
  source.path.text = ctool_string("/fixed-case-collision.asm");
  source.contents = ctool_bytes(
      collision_source_text,
      (ctool_u32)(sizeof(collision_source_text) - 1u));
  request.as.fixed.code.maximum_bytes = 0x1000u;
  (void)memset(&result, 0xa5, sizeof(result));
  if (status == CTOOL_OK) {
    status = ctool_asm_assemble(job, &source, &request, output, &result);
  }
  bytes = ctool_buffer_view(output);
  diagnostic = ctool_job_diagnostic(job, 1u);
  if (!check_status(status, CTOOL_ERR_INPUT,
                    "case-insensitive duplicate symbol") ||
      bytes.size != 0u || result.artifact != 0 ||
      result.bytes.data != (const ctool_u8 *)0 || result.bytes.size != 0u ||
      result.regions != (const ctool_asm_region_t *)0 ||
      result.region_count != 0u || result.has_entry != CTOOL_FALSE ||
      result.entry_address != 0u || ctool_job_diagnostic_count(job) != 2u ||
      diagnostic == (const ctool_diagnostic_t *)0 ||
      diagnostic->code != CTOOL_ASM_DIAG_DUPLICATE_SYMBOL ||
      diagnostic->severity != CTOOL_DIAG_ERROR) {
    (void)fprintf(stderr,
                  "case-insensitive duplicate symbol diagnostic differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  ctool_buffer_close(output);
  ctool_job_close(job);
  (void)puts("fixed-image: ok");
  return 0;
}

static int run_fixed_directives(void) {
  static const char source_text[] =
      "BITS 32\n"
      "section .text\n"
      "main:\n"
      " ret\n"
      "section .data\n"
      "greeting db \"Cupid\", 0x21, 0\n"
      "inline: db \"OK\", 10\n"
      "times 2 db 0xaa\n"
      "byte_pad resb 1\n"
      "word_pad resw 1\n"
      "dword_pad resd 1\n"
      "byte_alias rb 2\n"
      "word_alias rw 2\n"
      "dword_alias rd 2\n"
      "tail reserve 3\n"
      "data_label db 0x5a\n"
      "data_address equ data_label\n"
      "dd data_address\n";
  static const ctool_u8 expected_code[] = {0xc3u};
  static const ctool_u8 expected_data[] = {
      0x43u, 0x75u, 0x70u, 0x69u, 0x64u, 0x21u, 0x00u, 0x4fu, 0x4bu,
      0x0au, 0xaau, 0xaau, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
      0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
      0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
      0x5au, 0x24u, 0x00u, 0xb1u, 0x01u};
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_buffer_t *output;
  ctool_source_t source;
  ctool_string_t entry_candidate;
  ctool_asm_request_t request;
  ctool_asm_result_t result;
  const ctool_asm_region_t *text;
  const ctool_asm_region_t *data;
  ctool_bytes_t bytes;
  ctool_status_t status;

  status = ctool_host_adapter_init(&adapter, ".");
  if (!check_status(status, CTOOL_OK, "host adapter init")) {
    return 1;
  }
  config = ctool_host_job_config(&adapter, ctool_default_limits());
  status = ctool_job_open(&config, &job);
  if (!check_status(status, CTOOL_OK, "job open")) {
    return 1;
  }
  status = ctool_job_open_buffer(job, 64u, config.limits.output_bytes,
                                 &output);
  if (!check_status(status, CTOOL_OK, "fixed directive output open")) {
    ctool_job_close(job);
    return 1;
  }
  source.path.text = ctool_string("/fixed-directives.asm");
  source.contents =
      ctool_bytes(source_text, (ctool_u32)(sizeof(source_text) - 1u));
  entry_candidate = ctool_string("main");
  (void)memset(&request, 0, sizeof(request));
  request.artifact = CTOOL_ASM_ARTIFACT_FIXED_IMAGE;
  request.initial_mode = CTOOL_X86_MODE_16;
  request.entry_candidates = &entry_candidate;
  request.entry_candidate_count = 1u;
  request.as.fixed.code.base_address = 0x01a10000u;
  request.as.fixed.code.maximum_bytes = 0x1000u;
  request.as.fixed.data.base_address = 0x01b10000u;
  request.as.fixed.data.maximum_bytes = 0x1000u;
  (void)memset(&result, 0xa5, sizeof(result));

  status = ctool_asm_assemble(job, &source, &request, output, &result);
  bytes = ctool_buffer_view(output);
  text = result.region_count >= 1u ? &result.regions[0]
                                   : (const ctool_asm_region_t *)0;
  data = result.region_count >= 2u ? &result.regions[1]
                                   : (const ctool_asm_region_t *)0;
  if (!check_status(status, CTOOL_OK, "fixed directive assembly") ||
      result.artifact != CTOOL_ASM_ARTIFACT_FIXED_IMAGE ||
      result.bytes.data != bytes.data || result.bytes.size != bytes.size ||
      bytes.size !=
          (ctool_u32)(sizeof(expected_code) + sizeof(expected_data)) ||
      memcmp(bytes.data, expected_code, sizeof(expected_code)) != 0 ||
      memcmp(bytes.data + sizeof(expected_code), expected_data,
             sizeof(expected_data)) != 0 ||
      result.region_count != 2u || text == (const ctool_asm_region_t *)0 ||
      data == (const ctool_asm_region_t *)0 ||
      text->address != 0x01a10000u || text->output_offset != 0u ||
      text->file_size != (ctool_u32)sizeof(expected_code) ||
      text->memory_size != (ctool_u32)sizeof(expected_code) ||
      data->address != 0x01b10000u ||
      data->output_offset != (ctool_u32)sizeof(expected_code) ||
      data->file_size != (ctool_u32)sizeof(expected_data) ||
      data->memory_size != (ctool_u32)sizeof(expected_data) ||
      result.has_entry != CTOOL_TRUE ||
      result.entry_address != 0x01a10000u ||
      ctool_job_diagnostic_count(job) != 0u) {
    (void)fprintf(stderr, "fixed directive artifact differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  ctool_buffer_close(output);
  ctool_job_close(job);
  (void)puts("fixed-directives: ok");
  return 0;
}

static int check_include_artifact(const ctool_asm_result_t *result,
                                  ctool_bytes_t bytes,
                                  const char *operation) {
  static const ctool_u8 expected_code[] = {
      0xe8u, 0x01u, 0x00u, 0x00u, 0x00u, 0xc3u, 0x68u,
      0x00u, 0x00u, 0xd0u, 0x01u, 0xe8u, 0x20u, 0x20u,
      0x50u, 0xfeu, 0x83u, 0xc4u, 0x04u, 0xc3u};
  static const ctool_u8 expected_data[] = {
      0x25u, 0x69u, 0x6eu, 0x63u, 0x6cu, 0x75u, 0x64u, 0x65u, 0x20u,
      0x64u, 0x65u, 0x6du, 0x6fu, 0x20u, 0x6cu, 0x6fu, 0x61u, 0x64u,
      0x65u, 0x64u, 0x20u, 0x73u, 0x75u, 0x63u, 0x63u, 0x65u, 0x73u,
      0x73u, 0x66u, 0x75u, 0x6cu, 0x6cu, 0x79u, 0x0au, 0x00u};
  const ctool_asm_region_t *text =
      result->region_count >= 1u ? &result->regions[0]
                                 : (const ctool_asm_region_t *)0;
  const ctool_asm_region_t *data =
      result->region_count >= 2u ? &result->regions[1]
                                 : (const ctool_asm_region_t *)0;

  if (result->artifact != CTOOL_ASM_ARTIFACT_FIXED_IMAGE ||
      result->bytes.data != bytes.data || result->bytes.size != bytes.size ||
      bytes.size !=
          (ctool_u32)(sizeof(expected_code) + sizeof(expected_data)) ||
      memcmp(bytes.data, expected_code, sizeof(expected_code)) != 0 ||
      memcmp(bytes.data + sizeof(expected_code), expected_data,
             sizeof(expected_data)) != 0 ||
      result->region_count != 2u || text == (const ctool_asm_region_t *)0 ||
      data == (const ctool_asm_region_t *)0 ||
      text->address != 0x01c00000u || text->output_offset != 0u ||
      text->file_size != (ctool_u32)sizeof(expected_code) ||
      text->memory_size != (ctool_u32)sizeof(expected_code) ||
      data->address != 0x01d00000u ||
      data->output_offset != (ctool_u32)sizeof(expected_code) ||
      data->file_size != (ctool_u32)sizeof(expected_data) ||
      data->memory_size != (ctool_u32)sizeof(expected_data) ||
      result->has_entry != CTOOL_TRUE ||
      result->entry_address != 0x01c00000u) {
    (void)fprintf(stderr, "%s: included artifact differs\n", operation);
    return 0;
  }
  return 1;
}

static int run_include_resolution(void) {
  static const char parent_source_text[] =
      "%include \"include_helper.asm\"\n";
  static const char missing_source_text[] =
      "%include \"missing-helper.asm\"\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_buffer_t *output;
  ctool_path_t root;
  ctool_path_t feature_path;
  ctool_source_t source;
  ctool_asm_definition_t definition;
  ctool_string_t entry_candidate;
  ctool_asm_request_t request;
  ctool_asm_result_t result;
  const ctool_diagnostic_t *diagnostic;
  ctool_bytes_t bytes;
  ctool_status_t status;

  status = ctool_host_adapter_init(&adapter, "..");
  if (!check_status(status, CTOOL_OK, "include host adapter init")) {
    return 1;
  }
  config = ctool_host_job_config(&adapter, ctool_default_limits());
  status = ctool_job_open(&config, &job);
  if (!check_status(status, CTOOL_OK, "include job open")) {
    return 1;
  }
  status = ctool_job_open_buffer(job, 64u, config.limits.output_bytes,
                                 &output);
  if (!check_status(status, CTOOL_OK, "include output open")) {
    ctool_job_close(job);
    return 1;
  }
  status = ctool_path_root(ctool_job_arena(job), &root);
  if (status == CTOOL_OK) {
    status = ctool_path_resolve(ctool_job_arena(job), &root,
                                ctool_string("demos/include_feature.asm"),
                                config.limits.path_bytes, &feature_path);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_load_source(job, &feature_path, &source);
  }
  if (!check_status(status, CTOOL_OK, "load include feature")) {
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  definition.name = ctool_string("print");
  definition.kind = CTOOL_ASM_DEFINE_ABSOLUTE;
  definition.value = 0x00102030u;
  entry_candidate = ctool_string("main");
  (void)memset(&request, 0, sizeof(request));
  request.artifact = CTOOL_ASM_ARTIFACT_FIXED_IMAGE;
  request.initial_mode = CTOOL_X86_MODE_32;
  request.definitions = &definition;
  request.definition_count = 1u;
  request.include_roots = &root;
  request.include_root_count = 1u;
  request.entry_candidates = &entry_candidate;
  request.entry_candidate_count = 1u;
  request.as.fixed.code.base_address = 0x01c00000u;
  request.as.fixed.code.maximum_bytes = 0x1000u;
  request.as.fixed.data.base_address = 0x01d00000u;
  request.as.fixed.data.maximum_bytes = 0x1000u;
  (void)memset(&result, 0xa5, sizeof(result));

  status = ctool_asm_assemble(job, &source, &request, output, &result);
  bytes = ctool_buffer_view(output);
  if (!check_status(status, CTOOL_OK, "include-root assembly") ||
      !check_include_artifact(&result, bytes, "include-root assembly") ||
      ctool_job_diagnostic_count(job) != 0u) {
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  status = ctool_buffer_rewind(output, 0u);
  source.path.text = ctool_string("/demos/parent-feature.asm");
  source.contents = ctool_bytes(
      parent_source_text,
      (ctool_u32)(sizeof(parent_source_text) - 1u));
  request.include_roots = (const ctool_path_t *)0;
  request.include_root_count = 0u;
  (void)memset(&result, 0xa5, sizeof(result));
  if (status == CTOOL_OK) {
    status = ctool_asm_assemble(job, &source, &request, output, &result);
  }
  bytes = ctool_buffer_view(output);
  if (!check_status(status, CTOOL_OK, "include-parent assembly") ||
      !check_include_artifact(&result, bytes, "include-parent assembly") ||
      ctool_job_diagnostic_count(job) != 0u) {
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  status = ctool_buffer_rewind(output, 0u);
  source.path.text = ctool_string("/demos/missing-feature.asm");
  source.contents = ctool_bytes(
      missing_source_text,
      (ctool_u32)(sizeof(missing_source_text) - 1u));
  (void)memset(&result, 0xa5, sizeof(result));
  if (status == CTOOL_OK) {
    status = ctool_asm_assemble(job, &source, &request, output, &result);
  }
  bytes = ctool_buffer_view(output);
  diagnostic = ctool_job_diagnostic(job, 0u);
  if (!check_status(status, CTOOL_ERR_NOT_FOUND, "missing include") ||
      bytes.size != 0u || result.artifact != 0 ||
      result.bytes.data != (const ctool_u8 *)0 || result.bytes.size != 0u ||
      result.regions != (const ctool_asm_region_t *)0 ||
      result.region_count != 0u || result.has_entry != CTOOL_FALSE ||
      result.entry_address != 0u || ctool_job_diagnostic_count(job) != 1u ||
      diagnostic == (const ctool_diagnostic_t *)0 ||
      diagnostic->code != CTOOL_ASM_DIAG_INCLUDE_NOT_FOUND ||
      diagnostic->severity != CTOOL_DIAG_ERROR) {
    (void)fprintf(stderr, "missing include diagnostic differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  ctool_buffer_close(output);
  ctool_job_close(job);
  (void)puts("include-resolution: ok");
  return 0;
}

static void init_raw_request(ctool_asm_request_t *request) {
  (void)memset(request, 0, sizeof(*request));
  request->artifact = CTOOL_ASM_ARTIFACT_RAW;
  request->initial_mode = CTOOL_X86_MODE_32;
}

static void init_object_request(ctool_asm_request_t *request) {
  (void)memset(request, 0, sizeof(*request));
  request->artifact = CTOOL_ASM_ARTIFACT_ELF32_REL;
  request->initial_mode = CTOOL_X86_MODE_32;
}

static void init_fixed_request(ctool_asm_request_t *request,
                               const ctool_string_t *entry) {
  (void)memset(request, 0, sizeof(*request));
  request->artifact = CTOOL_ASM_ARTIFACT_FIXED_IMAGE;
  request->initial_mode = CTOOL_X86_MODE_32;
  request->entry_candidates = entry;
  request->entry_candidate_count = 1u;
  request->as.fixed.code.base_address = 0x01a00000u;
  request->as.fixed.code.maximum_bytes = 0x1000u;
  request->as.fixed.data.base_address = 0x01b00000u;
  request->as.fixed.data.maximum_bytes = 0x1000u;
}

static int run_raw_source_contracts(void) {
  static const char duplicate_origin_source[] =
      "BITS 16\n"
      "ORG 0x7c00\n"
      "db 0x11\n"
      "ORG 0x8000\n"
      "db 0x22\n";
  static const char equ_preamble_source[] =
      "BITS 32\n"
      "VALUE equ 1\n"
      "section .data\n"
      "db VALUE\n";
  static const char one_section_source[] =
      "BITS 16\n"
      "section .payload\n"
      "ORG 0x100\n"
      "db 0x11\n"
      "section .payload\n"
      "ret\n";
  static const char control_edge_source[] =
      "BITS 16\n"
      "ORG 0x8000\n"
      "start: jmp next\n"
      "next: jmp dword 0x08:pm32\n"
      "BITS 32\n"
      "pm32: call eax\n"
      "jmp 0x08:0x00100000\n";
  static const char multi_section_source[] =
      "BITS 32\n"
      "section .text\n"
      "start: ret\n"
      "section .data\n"
      "db 0x2a\n";
  static const char early_multi_section_source[] =
      "BITS 32\n"
      "section .first\n"
      "section .second\n"
      "db 0x2a\n";
  static const char label_preamble_source[] =
      "BITS 32\n"
      "implicit_text:\n"
      "section .data\n"
      "db 0x2a\n";
  static const ctool_u8 expected[] = {0x11u, 0xc3u};
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_buffer_t *output;
  ctool_source_t source;
  ctool_asm_request_t request;
  ctool_asm_result_t result;
  ctool_bytes_t bytes;
  ctool_status_t status;

  status = ctool_host_adapter_init(&adapter, ".");
  if (!check_status(status, CTOOL_OK,
                    "raw source contract host adapter init")) {
    return 1;
  }
  config = ctool_host_job_config(&adapter, ctool_default_limits());
  init_raw_request(&request);
  status = ctool_job_open(&config, &job);
  if (!check_status(status, CTOOL_OK, "raw source contract job open")) {
    return 1;
  }
  status = ctool_job_open_buffer(job, 16u, config.limits.output_bytes,
                                 &output);
  if (!check_status(status, CTOOL_OK, "raw source contract output open")) {
    ctool_job_close(job);
    return 1;
  }

  source.path.text = ctool_string("/duplicate-org.asm");
  source.contents = ctool_bytes(
      duplicate_origin_source,
      (ctool_u32)(sizeof(duplicate_origin_source) - 1u));
  (void)memset(&result, 0xa5, sizeof(result));
  status = ctool_asm_assemble(job, &source, &request, output, &result);
  if (!check_status(status, CTOOL_ERR_INPUT, "duplicate raw origin") ||
      ctool_buffer_view(output).size != 0u ||
      !contract_result_is_zero(&result) ||
      ctool_job_diagnostic_count(job) != 1u ||
      !contract_has_source_diagnostic(
          job, CTOOL_ASM_DIAG_INVALID_ORIGIN, "/duplicate-org.asm", 4u,
          1u, "raw output accepts only one ORG directive")) {
    (void)fprintf(stderr, "duplicate raw origin contract differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  status = ctool_buffer_rewind(output, 0u);
  source.path.text = ctool_string("/raw-equ-preamble.asm");
  source.contents = ctool_bytes(
      equ_preamble_source,
      (ctool_u32)(sizeof(equ_preamble_source) - 1u));
  (void)memset(&result, 0xa5, sizeof(result));
  if (status == CTOOL_OK) {
    status = ctool_asm_assemble(job, &source, &request, output, &result);
  }
  bytes = ctool_buffer_view(output);
  if (!check_status(status, CTOOL_OK, "raw EQU preamble") ||
      bytes.size != 1u || bytes.data[0] != 1u ||
      result.artifact != CTOOL_ASM_ARTIFACT_RAW ||
      result.bytes.data != bytes.data || result.bytes.size != bytes.size ||
      result.raw_origin != 0u || result.raw_range_count != 1u ||
      result.raw_ranges[0].offset != 0u ||
      result.raw_ranges[0].kind != CTOOL_ASM_RAW_RANGE_DATA ||
      ctool_job_diagnostic_count(job) != 1u) {
    (void)fprintf(stderr, "raw EQU preamble contract differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  status = ctool_buffer_rewind(output, 0u);
  source.path.text = ctool_string("/raw-one-section.asm");
  source.contents = ctool_bytes(
      one_section_source, (ctool_u32)(sizeof(one_section_source) - 1u));
  (void)memset(&result, 0xa5, sizeof(result));
  status = ctool_asm_assemble(job, &source, &request, output, &result);
  bytes = ctool_buffer_view(output);
  if (!check_status(status, CTOOL_OK, "raw source recovery") ||
      bytes.size != (ctool_u32)sizeof(expected) ||
      memcmp(bytes.data, expected, sizeof(expected)) != 0 ||
      result.artifact != CTOOL_ASM_ARTIFACT_RAW ||
      result.bytes.data != bytes.data || result.bytes.size != bytes.size ||
      result.raw_origin != 0x100u || result.raw_range_count != 2u ||
      result.raw_ranges[0].offset != 0u ||
      result.raw_ranges[0].kind != CTOOL_ASM_RAW_RANGE_DATA ||
      result.raw_ranges[1].offset != 1u ||
      result.raw_ranges[1].kind != CTOOL_ASM_RAW_RANGE_CODE16 ||
      ctool_job_diagnostic_count(job) != 1u) {
    (void)fprintf(stderr, "raw source recovery artifact differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  status = ctool_buffer_rewind(output, 0u);
  if (status == CTOOL_OK) {
    (void)memset(&result, 0xa5, sizeof(result));
    status = ctool_asm_assemble(job, &source, &request, output, &result);
  }
  bytes = ctool_buffer_view(output);
  if (!check_status(status, CTOOL_OK,
                    "raw source deterministic repeat") ||
      bytes.size != (ctool_u32)sizeof(expected) ||
      memcmp(bytes.data, expected, sizeof(expected)) != 0 ||
      result.raw_origin != 0x100u ||
      ctool_job_diagnostic_count(job) != 1u) {
    (void)fprintf(stderr, "raw source deterministic repeat differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  status = ctool_buffer_rewind(output, 0u);
  source.path.text = ctool_string("/raw-control-edges.asm");
  source.contents = ctool_bytes(
      control_edge_source,
      (ctool_u32)(sizeof(control_edge_source) - 1u));
  (void)memset(&result, 0xa5, sizeof(result));
  if (status == CTOOL_OK) {
    status = ctool_asm_assemble(job, &source, &request, output, &result);
  }
  bytes = ctool_buffer_view(output);
  if (!check_status(status, CTOOL_OK, "raw control-edge metadata") ||
      bytes.size != 19u || result.raw_origin != 0x8000u ||
      result.raw_range_count != 2u || result.raw_edge_count != 4u ||
      result.raw_edges[0].source_offset != 0u ||
      result.raw_edges[0].kind != CTOOL_ASM_RAW_EDGE_RELATIVE ||
      result.raw_edges[0].class_id != CTOOL_ASM_RAW_EDGE_LOCAL ||
      result.raw_edges[0].target_offset != 2u ||
      result.raw_edges[0].target_address != 0x8002u ||
      result.raw_edges[0].target_mode != CTOOL_X86_MODE_16 ||
      result.raw_edges[0].target_segment != 0u ||
      result.raw_edges[1].source_offset != 2u ||
      result.raw_edges[1].kind != CTOOL_ASM_RAW_EDGE_FAR ||
      result.raw_edges[1].class_id != CTOOL_ASM_RAW_EDGE_LOCAL ||
      result.raw_edges[1].target_offset != 10u ||
      result.raw_edges[1].target_address != 0x800au ||
      result.raw_edges[1].target_mode != CTOOL_X86_MODE_32 ||
      result.raw_edges[1].target_segment != 8u ||
      result.raw_edges[2].source_offset != 10u ||
      result.raw_edges[2].kind != CTOOL_ASM_RAW_EDGE_INDIRECT ||
      result.raw_edges[2].class_id != CTOOL_ASM_RAW_EDGE_UNPROVABLE ||
      result.raw_edges[2].target_offset != CTOOL_ASM_RAW_EDGE_NO_TARGET ||
      result.raw_edges[2].target_address != CTOOL_ASM_RAW_EDGE_NO_TARGET ||
      result.raw_edges[2].target_mode != 0 ||
      result.raw_edges[2].target_segment != CTOOL_ASM_RAW_EDGE_NO_TARGET ||
      result.raw_edges[3].source_offset != 12u ||
      result.raw_edges[3].kind != CTOOL_ASM_RAW_EDGE_FAR ||
      result.raw_edges[3].class_id != CTOOL_ASM_RAW_EDGE_EXTERNAL ||
      result.raw_edges[3].target_offset != CTOOL_ASM_RAW_EDGE_NO_TARGET ||
      result.raw_edges[3].target_address != 0x00100000u ||
      result.raw_edges[3].target_mode != CTOOL_X86_MODE_32 ||
      result.raw_edges[3].target_segment != 8u ||
      ctool_job_diagnostic_count(job) != 1u) {
    (void)fprintf(stderr, "raw control-edge metadata differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  status = ctool_buffer_rewind(output, 0u);
  source.path.text = ctool_string("/raw-multi-section.asm");
  source.contents = ctool_bytes(
      multi_section_source,
      (ctool_u32)(sizeof(multi_section_source) - 1u));
  (void)memset(&result, 0xa5, sizeof(result));
  if (status == CTOOL_OK) {
    status = ctool_asm_assemble(job, &source, &request, output, &result);
  }
  bytes = ctool_buffer_view(output);
  if (!check_status(status, CTOOL_ERR_INPUT,
                    "raw multi-section layout") ||
      bytes.size != 0u ||
      !contract_result_is_zero(&result) ||
      ctool_job_diagnostic_count(job) != 2u ||
      !contract_has_source_diagnostic(
          job, CTOOL_ASM_DIAG_INVALID_SECTION,
          "/raw-multi-section.asm", 4u, 1u,
          "raw output supports only one source section")) {
    (void)fprintf(stderr, "raw multi-section contract differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  source.path.text = ctool_string("/raw-one-section.asm");
  source.contents = ctool_bytes(
      one_section_source, (ctool_u32)(sizeof(one_section_source) - 1u));
  (void)memset(&result, 0xa5, sizeof(result));
  status = ctool_asm_assemble(job, &source, &request, output, &result);
  bytes = ctool_buffer_view(output);
  if (!check_status(status, CTOOL_OK,
                    "raw multi-section recovery") ||
      bytes.size != (ctool_u32)sizeof(expected) ||
      memcmp(bytes.data, expected, sizeof(expected)) != 0 ||
      result.raw_origin != 0x100u ||
      ctool_job_diagnostic_count(job) != 2u) {
    (void)fprintf(stderr, "raw multi-section recovery differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  status = ctool_buffer_rewind(output, 0u);
  source.path.text = ctool_string("/raw-early-multi-section.asm");
  source.contents = ctool_bytes(
      early_multi_section_source,
      (ctool_u32)(sizeof(early_multi_section_source) - 1u));
  (void)memset(&result, 0xa5, sizeof(result));
  if (status == CTOOL_OK) {
    status = ctool_asm_assemble(job, &source, &request, output, &result);
  }
  if (!check_status(status, CTOOL_ERR_INPUT,
                    "raw early multi-section layout") ||
      ctool_buffer_view(output).size != 0u ||
      !contract_result_is_zero(&result) ||
      ctool_job_diagnostic_count(job) != 3u ||
      !contract_has_source_diagnostic(
          job, CTOOL_ASM_DIAG_INVALID_SECTION,
          "/raw-early-multi-section.asm", 3u, 1u,
          "raw output supports only one source section")) {
    (void)fprintf(stderr, "raw early multi-section contract differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  status = ctool_buffer_rewind(output, 0u);
  source.path.text = ctool_string("/raw-label-preamble.asm");
  source.contents = ctool_bytes(
      label_preamble_source,
      (ctool_u32)(sizeof(label_preamble_source) - 1u));
  (void)memset(&result, 0xa5, sizeof(result));
  if (status == CTOOL_OK) {
    status = ctool_asm_assemble(job, &source, &request, output, &result);
  }
  if (!check_status(status, CTOOL_ERR_INPUT,
                    "raw label preamble section claim") ||
      ctool_buffer_view(output).size != 0u ||
      !contract_result_is_zero(&result) ||
      ctool_job_diagnostic_count(job) != 4u ||
      !contract_has_source_diagnostic(
          job, CTOOL_ASM_DIAG_INVALID_SECTION,
          "/raw-label-preamble.asm", 3u, 1u,
          "raw output supports only one source section")) {
    (void)fprintf(stderr, "raw label preamble contract differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  ctool_buffer_close(output);
  ctool_job_close(job);
  (void)puts("raw-source-contracts: ok");
  return 0;
}

static int run_alignment(void) {
  static const char invalid_fill_source_text[] =
      "BITS 32\n"
      "align 4, 256\n";
  static const char raw_source_text[] =
      "BITS 32\n"
      "ORG 0x101\n"
      "db 0x11\n"
      "align 4, 0xa5\n"
      "aligned_raw:\n"
      "dw aligned_raw\n"
      "align 8\n"
      "db 0x22\n";
  static const ctool_u8 expected_raw[] = {
      0x11u, 0xa5u, 0xa5u, 0x04u, 0x01u, 0x00u, 0x00u, 0x22u};
  static const char object_source_text[] =
      "BITS 32\n"
      "section .data\n"
      "db 1\n"
      "align 16, 0x5a\n"
      "aligned_data:\n"
      "dd aligned_data\n"
      "section .bss\n"
      "resb 3\n"
      "align 32\n"
      "aligned_bss:\n"
      "resb 5\n";
  static const char fixed_source_text[] =
      "BITS 32\n"
      "section .text\n"
      "main: ret\n"
      "section .data\n"
      "db 0x11\n"
      "align 16, 0xcc\n"
      "aligned_fixed:\n"
      "dd aligned_fixed\n"
      "section .bss\n"
      "resb 3\n"
      "align 32\n"
      "fixed_bss:\n"
      "resb 5\n";
  static const char fixed_empty_source_text[] =
      "BITS 32\n"
      "section .text\n"
      "align 32\n"
      "main:\n"
      "section .data\n"
      "dd main\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_buffer_t *output;
  ctool_source_t source;
  ctool_source_t object_source;
  ctool_asm_request_t raw;
  ctool_asm_request_t object_request;
  ctool_asm_request_t fixed;
  ctool_asm_request_t fixed_overflow;
  ctool_asm_request_t fixed_empty;
  ctool_asm_request_t fixed_empty_overflow;
  ctool_asm_result_t result;
  ctool_elf32_object_t object;
  const ctool_elf32_section_t *data;
  const ctool_elf32_section_t *bss;
  const ctool_elf32_symbol_t *aligned_data;
  const ctool_elf32_symbol_t *aligned_bss;
  const ctool_asm_region_t *text_region;
  const ctool_asm_region_t *data_region;
  const ctool_diagnostic_t *diagnostic;
  ctool_string_t entry = ctool_string("main");
  ctool_bytes_t bytes;
  ctool_status_t status;
  ctool_u32 index;

  status = ctool_host_adapter_init(&adapter, ".");
  if (!check_status(status, CTOOL_OK, "alignment host adapter init")) {
    return 1;
  }
  config = ctool_host_job_config(&adapter, ctool_default_limits());
  init_raw_request(&raw);
  init_object_request(&object_request);
  init_fixed_request(&fixed, &entry);
  fixed_overflow = fixed;
  fixed_overflow.as.fixed.data.base_address = 0xfffffff1u;
  fixed_empty = fixed;
  fixed_empty.as.fixed.code.base_address = 0x01a00003u;
  fixed_empty_overflow = fixed;
  fixed_empty_overflow.as.fixed.code.base_address = 0xfffffff1u;

  if (!expect_assembly_failure(
          "zero alignment", config, "/align-zero.asm",
          "BITS 32\nalign 0\n", &raw, CTOOL_ERR_INPUT,
          CTOOL_ASM_DIAG_LAYOUT, "/align-zero.asm") ||
      !expect_assembly_failure(
          "non-power-of-two alignment", config, "/align-three.asm",
          "BITS 32\nalign 3\n", &raw, CTOOL_ERR_INPUT,
          CTOOL_ASM_DIAG_LAYOUT, "/align-three.asm") ||
      !expect_assembly_failure(
          "initialized BSS alignment", config, "/align-bss-fill.asm",
          "BITS 32\nsection .bss\nresb 1\nalign 8, 1\n", &object_request,
          CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_INVALID_SECTION,
          "/align-bss-fill.asm") ||
      !expect_assembly_failure(
          "fixed absolute alignment overflow", config,
          "/align-fixed-overflow.asm",
          "BITS 32\nsection .text\nmain: ret\nsection .data\n"
          "align 16\ndb 1\n",
          &fixed_overflow, CTOOL_ERR_OVERFLOW, CTOOL_ASM_DIAG_LAYOUT,
          "/align-fixed-overflow.asm") ||
      !expect_assembly_failure(
          "fixed empty-section alignment overflow", config,
          "/align-fixed-empty-overflow.asm",
          "BITS 32\nsection .text\nalign 16\nmain:\n",
          &fixed_empty_overflow, CTOOL_ERR_OVERFLOW,
          CTOOL_ASM_DIAG_LAYOUT,
          "/align-fixed-empty-overflow.asm")) {
    return 1;
  }

  status = ctool_job_open(&config, &job);
  if (!check_status(status, CTOOL_OK, "alignment raw job open")) {
    return 1;
  }
  status = ctool_job_open_buffer(job, 16u, config.limits.output_bytes,
                                 &output);
  if (!check_status(status, CTOOL_OK, "alignment raw output open")) {
    ctool_job_close(job);
    return 1;
  }
  source.path.text = ctool_string("/align-fill.asm");
  source.contents = ctool_bytes(
      invalid_fill_source_text,
      (ctool_u32)(sizeof(invalid_fill_source_text) - 1u));
  (void)memset(&result, 0xa5, sizeof(result));
  status = ctool_asm_assemble(job, &source, &raw, output, &result);
  diagnostic = ctool_job_diagnostic(job, 0u);
  if (!check_status(status, CTOOL_ERR_INPUT, "alignment fill overflow") ||
      ctool_buffer_view(output).size != 0u ||
      !contract_result_is_zero(&result) ||
      ctool_job_diagnostic_count(job) != 1u ||
      diagnostic == (const ctool_diagnostic_t *)0 ||
      diagnostic->code != CTOOL_ASM_DIAG_LAYOUT) {
    (void)fprintf(stderr, "alignment fill diagnostic differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  source.path.text = ctool_string("/alignment-raw.asm");
  source.contents = ctool_bytes(
      raw_source_text, (ctool_u32)(sizeof(raw_source_text) - 1u));
  (void)memset(&result, 0xa5, sizeof(result));
  status = ctool_asm_assemble(job, &source, &raw, output, &result);
  bytes = ctool_buffer_view(output);
  if (!check_status(status, CTOOL_OK, "raw alignment recovery") ||
      bytes.size != (ctool_u32)sizeof(expected_raw) ||
      memcmp(bytes.data, expected_raw, sizeof(expected_raw)) != 0 ||
      result.artifact != CTOOL_ASM_ARTIFACT_RAW ||
      result.bytes.data != bytes.data || result.bytes.size != bytes.size ||
      ctool_job_diagnostic_count(job) != 1u) {
    (void)fprintf(stderr, "raw alignment recovery artifact differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }
  status = ctool_buffer_rewind(output, 0u);
  if (status == CTOOL_OK) {
    (void)memset(&result, 0xa5, sizeof(result));
    status = ctool_asm_assemble(job, &source, &raw, output, &result);
  }
  bytes = ctool_buffer_view(output);
  if (!check_status(status, CTOOL_OK, "raw alignment deterministic repeat") ||
      bytes.size != (ctool_u32)sizeof(expected_raw) ||
      memcmp(bytes.data, expected_raw, sizeof(expected_raw)) != 0 ||
      ctool_job_diagnostic_count(job) != 1u) {
    (void)fprintf(stderr, "raw alignment repeat differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }
  ctool_buffer_close(output);
  ctool_job_close(job);

  status = ctool_job_open(&config, &job);
  if (!check_status(status, CTOOL_OK, "alignment object job open")) {
    return 1;
  }
  status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes,
                                 &output);
  if (!check_status(status, CTOOL_OK, "alignment object output open")) {
    ctool_job_close(job);
    return 1;
  }
  source.path.text = ctool_string("/alignment-object.asm");
  source.contents = ctool_bytes(
      object_source_text, (ctool_u32)(sizeof(object_source_text) - 1u));
  (void)memset(&result, 0xa5, sizeof(result));
  (void)memset(&object, 0, sizeof(object));
  status = ctool_asm_assemble(job, &source, &object_request, output, &result);
  if (status == CTOOL_OK) {
    object_source.path.text = ctool_string("/alignment-object.o");
    object_source.contents = result.bytes;
    status = ctool_elf32_read(job, &object_source, &object);
  }
  data = find_section(&object, ".data");
  bss = find_section(&object, ".bss");
  aligned_data = find_symbol(&object, "aligned_data");
  aligned_bss = find_symbol(&object, "aligned_bss");
  if (!check_status(status, CTOOL_OK, "ELF32 alignment assembly") ||
      data == (const ctool_elf32_section_t *)0 ||
      data->alignment != 16u || data->size != 20u ||
      data->contents.size != 20u || data->contents.data[0] != 1u ||
      bss == (const ctool_elf32_section_t *)0 ||
      bss->type != CTOOL_ELF32_SHT_NOBITS || bss->alignment != 32u ||
      bss->size != 37u || bss->contents.size != 0u ||
      aligned_data == (const ctool_elf32_symbol_t *)0 ||
      aligned_data->section_file_index != data->file_index ||
      aligned_data->value != 16u ||
      aligned_bss == (const ctool_elf32_symbol_t *)0 ||
      aligned_bss->section_file_index != bss->file_index ||
      aligned_bss->value != 32u || object.relocation_count != 1u ||
      object.relocations[0].target_section_file_index != data->file_index ||
      object.relocations[0].offset != 16u ||
      object.relocations[0].symbol_file_index != aligned_data->file_index ||
      object.relocations[0].type != CTOOL_ELF32_R_386_32 ||
      object.relocations[0].addend_known == CTOOL_FALSE ||
      object.relocations[0].addend != 0 ||
      ctool_job_diagnostic_count(job) != 0u) {
    (void)fprintf(stderr, "ELF32 alignment artifact differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }
  for (index = 1u; index < 16u; index++) {
    if (data->contents.data[index] != 0x5au) {
      (void)fprintf(stderr, "ELF32 alignment fill differs\n");
      ctool_buffer_close(output);
      ctool_job_close(job);
      return 1;
    }
  }
  for (index = 16u; index < 20u; index++) {
    if (data->contents.data[index] != 0u) {
      (void)fprintf(stderr, "ELF32 alignment relocation field differs\n");
      ctool_buffer_close(output);
      ctool_job_close(job);
      return 1;
    }
  }
  ctool_buffer_close(output);
  ctool_job_close(job);

  fixed.as.fixed.data.base_address = 0x01b00003u;
  status = ctool_job_open(&config, &job);
  if (!check_status(status, CTOOL_OK, "alignment fixed job open")) {
    return 1;
  }
  status = ctool_job_open_buffer(job, 64u, config.limits.output_bytes,
                                 &output);
  if (!check_status(status, CTOOL_OK, "alignment fixed output open")) {
    ctool_job_close(job);
    return 1;
  }
  source.path.text = ctool_string("/alignment-fixed.asm");
  source.contents = ctool_bytes(
      fixed_source_text, (ctool_u32)(sizeof(fixed_source_text) - 1u));
  (void)memset(&result, 0xa5, sizeof(result));
  status = ctool_asm_assemble(job, &source, &fixed, output, &result);
  bytes = ctool_buffer_view(output);
  text_region = result.region_count >= 1u ? &result.regions[0]
                                          : (const ctool_asm_region_t *)0;
  data_region = result.region_count >= 2u ? &result.regions[1]
                                          : (const ctool_asm_region_t *)0;
  if (!check_status(status, CTOOL_OK, "fixed alignment assembly") ||
      bytes.size != 34u || bytes.data[0] != 0xc3u ||
      text_region == (const ctool_asm_region_t *)0 ||
      text_region->address != 0x01a00000u || text_region->file_size != 1u ||
      text_region->memory_size != 1u ||
      data_region == (const ctool_asm_region_t *)0 ||
      data_region->address != 0x01b00003u ||
      data_region->output_offset != 1u || data_region->file_size != 33u ||
      data_region->memory_size != 98u || result.has_entry != CTOOL_TRUE ||
      result.entry_address != 0x01a00000u ||
      ctool_job_diagnostic_count(job) != 0u) {
    (void)fprintf(stderr, "fixed alignment artifact differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }
  for (index = 1u; index < 14u; index++) {
    if (bytes.data[index] != 0u) {
      (void)fprintf(stderr, "fixed leading alignment differs\n");
      ctool_buffer_close(output);
      ctool_job_close(job);
      return 1;
    }
  }
  if (bytes.data[14] != 0x11u) {
    (void)fprintf(stderr, "fixed aligned section payload differs\n");
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }
  for (index = 15u; index < 30u; index++) {
    if (bytes.data[index] != 0xccu) {
      (void)fprintf(stderr, "fixed alignment fill differs\n");
      ctool_buffer_close(output);
      ctool_job_close(job);
      return 1;
    }
  }
  if (bytes.data[30] != 0x20u || bytes.data[31] != 0x00u ||
      bytes.data[32] != 0xb0u || bytes.data[33] != 0x01u) {
    (void)fprintf(stderr, "fixed aligned label address differs\n");
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }
  ctool_buffer_close(output);
  ctool_job_close(job);

  status = ctool_job_open(&config, &job);
  if (!check_status(status, CTOOL_OK, "empty alignment fixed job open")) {
    return 1;
  }
  status = ctool_job_open_buffer(job, 16u, config.limits.output_bytes,
                                 &output);
  if (!check_status(status, CTOOL_OK, "empty alignment fixed output open")) {
    ctool_job_close(job);
    return 1;
  }
  source.path.text = ctool_string("/alignment-fixed-empty.asm");
  source.contents = ctool_bytes(
      fixed_empty_source_text,
      (ctool_u32)(sizeof(fixed_empty_source_text) - 1u));
  (void)memset(&result, 0xa5, sizeof(result));
  status = ctool_asm_assemble(job, &source, &fixed_empty, output, &result);
  bytes = ctool_buffer_view(output);
  data_region = result.region_count == 1u
                    ? &result.regions[0]
                    : (const ctool_asm_region_t *)0;
  if (!check_status(status, CTOOL_OK, "fixed empty alignment assembly") ||
      bytes.size != 4u || bytes.data[0] != 0x20u ||
      bytes.data[1] != 0x00u || bytes.data[2] != 0xa0u ||
      bytes.data[3] != 0x01u || result.region_count != 1u ||
      data_region == (const ctool_asm_region_t *)0 ||
      data_region->address != fixed_empty.as.fixed.data.base_address ||
      data_region->output_offset != 0u || data_region->file_size != 4u ||
      data_region->memory_size != 4u || result.has_entry != CTOOL_TRUE ||
      result.entry_address != 0x01a00020u ||
      ctool_job_diagnostic_count(job) != 0u) {
    (void)fprintf(stderr, "fixed empty alignment artifact differs\n");
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }
  ctool_buffer_close(output);
  ctool_job_close(job);
  (void)puts("alignment: ok");
  return 0;
}

static int run_error_contracts(void) {
  static const char cycle_a[] = "%include \"cycle-b.asm\"\n";
  static const char cycle_b[] = "%include \"cycle-a.asm\"\n";
  static const char include_bad[] =
      "BITS 32\n"
      "main:\n"
      " mov eax, st0\n";
  static const char include_bad_main[] = "%include \"bad.inc\"\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_config_t limited_config;
  ctool_job_config_t store_config;
  ctool_asm_request_t raw;
  ctool_asm_request_t object;
  ctool_asm_request_t fixed;
  ctool_asm_request_t invalid;
  ctool_string_t entry = ctool_string("main");
  asm_contract_file_t cycle_files[2];
  asm_contract_store_t cycle_store;
  asm_contract_file_t bad_file;
  asm_contract_store_t bad_store;
  char depth_paths[33][24];
  char depth_contents[33][40];
  asm_contract_file_t depth_files[33];
  asm_contract_store_t depth_store;
  ctool_u32 index;
  ctool_status_t status = ctool_host_adapter_init(&adapter, ".");
  if (!check_status(status, CTOOL_OK, "error host adapter init")) {
    return 1;
  }
  config = ctool_host_job_config(&adapter, ctool_default_limits());
  init_raw_request(&raw);
  init_object_request(&object);
  init_fixed_request(&fixed, &entry);

  invalid = raw;
  invalid.initial_mode = (ctool_x86_mode_t)0;
  if (!expect_assembly_failure(
          "invalid request", config, "/invalid-request.asm", "BITS 32\n",
          &invalid, CTOOL_ERR_UNSUPPORTED, CTOOL_ASM_DIAG_INVALID_REQUEST,
          "/invalid-request.asm") ||
      !expect_assembly_failure(
          "undefined symbol", config, "/undefined.asm", "dd missing\n",
          &raw, CTOOL_ERR_NOT_FOUND, CTOOL_ASM_DIAG_UNDEFINED_SYMBOL,
          "/undefined.asm") ||
      !expect_assembly_failure(
          "expression overflow", config, "/overflow.asm",
          "BITS 32\nsection .text\nmain: ret\nsection .data\n"
          "dd 0xffffffffffffffff + 1\n",
          &fixed, CTOOL_ERR_OVERFLOW,
          CTOOL_ASM_DIAG_EXPRESSION_OVERFLOW, "/overflow.asm") ||
      !expect_assembly_failure(
          "fixed label addend overflow", config,
          "/label-overflow.asm",
          "BITS 32\nsection .text\nmain: ret\nsection .data\n"
          "dd main + 0xffffffffffffffff\n",
          &fixed, CTOOL_ERR_OVERFLOW,
          CTOOL_ASM_DIAG_EXPRESSION_OVERFLOW,
          "/label-overflow.asm") ||
      !expect_assembly_failure(
          "non-relocatable expression", config, "/nonrel.asm",
          "extern left\nextern right\ndd left + right\n", &object,
          CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_NON_RELOCATABLE_EXPRESSION,
          "/nonrel.asm") ||
      !expect_assembly_failure(
          "narrow relocation", config, "/narrow-reloc.asm",
          "extern target\ndb target\n", &object, CTOOL_ERR_UNSUPPORTED,
          CTOOL_ASM_DIAG_RELOCATION, "/narrow-reloc.asm") ||
      !expect_assembly_failure(
          "invalid object origin", config, "/object-org.asm", "ORG 1\n",
          &object, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_INVALID_ORIGIN,
          "/object-org.asm") ||
      !expect_assembly_failure(
          "invalid bits mode", config, "/bits.asm", "BITS 64\n", &raw,
          CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_INVALID_MODE, "/bits.asm") ||
      !expect_assembly_failure(
          "return cleanup overflow", config, "/ret-overflow.asm",
          "BITS 32\nret 65536\n", &raw, CTOOL_ERR_INPUT,
          CTOOL_ASM_DIAG_ENCODING, "/ret-overflow.asm") ||
      !expect_assembly_failure(
          "conditional move byte operands", config, "/cmov-byte.asm",
          "BITS 32\ncmovne al, cl\n", &raw, CTOOL_ERR_INPUT,
          CTOOL_ASM_DIAG_ENCODING, "/cmov-byte.asm") ||
      !expect_assembly_failure(
          "conditional move immediate source", config,
          "/cmov-immediate.asm", "BITS 32\ncmovne eax, 1\n", &raw,
          CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_ENCODING,
          "/cmov-immediate.asm") ||
      !expect_assembly_failure(
          "conditional move width mismatch", config,
          "/cmov-width.asm", "BITS 32\ncmovne eax, cx\n", &raw,
          CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_ENCODING,
          "/cmov-width.asm") ||
      !expect_assembly_failure(
          "conditional move lock prefix", config, "/cmov-lock.asm",
          "BITS 32\nlock cmovne eax, ecx\n", &raw, CTOOL_ERR_INPUT,
          CTOOL_ASM_DIAG_ENCODING, "/cmov-lock.asm") ||
      !expect_assembly_failure(
          "conditional move repeat prefix", config, "/cmov-rep.asm",
          "BITS 32\nrep cmovne eax, ecx\n", &raw, CTOOL_ERR_INPUT,
          CTOOL_ASM_DIAG_ENCODING, "/cmov-rep.asm") ||
      !expect_assembly_failure(
          "conditional move repeat-not-equal prefix", config,
          "/cmov-repne.asm", "BITS 32\nrepne cmovne eax, ecx\n", &raw,
          CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_ENCODING,
          "/cmov-repne.asm") ||
      !expect_assembly_failure(
          "parity SETcc non-byte register", config, "/setp-width.asm",
          "BITS 32\nsetp eax\n", &raw, CTOOL_ERR_INPUT,
          CTOOL_ASM_DIAG_ENCODING, "/setp-width.asm") ||
      !expect_assembly_failure(
          "parity SETcc immediate operand", config,
          "/setnp-immediate.asm", "BITS 32\nsetnp 1\n", &raw,
          CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_ENCODING,
          "/setnp-immediate.asm") ||
      !expect_assembly_failure(
          "parity SETcc lock prefix", config, "/setp-lock.asm",
          "BITS 32\nlock setp byte [eax]\n", &raw, CTOOL_ERR_INPUT,
          CTOOL_ASM_DIAG_ENCODING, "/setp-lock.asm") ||
      !expect_assembly_failure(
          "parity SETcc alternate spelling", config, "/setpe-alias.asm",
          "BITS 32\nsetpe dl\n", &raw, CTOOL_ERR_INPUT,
          CTOOL_ASM_DIAG_SYNTAX, "/setpe-alias.asm") ||
      !expect_assembly_failure(
          "parity SETcc alternate spelling", config, "/setpo-alias.asm",
          "BITS 32\nsetpo dl\n", &raw, CTOOL_ERR_INPUT,
          CTOOL_ASM_DIAG_SYNTAX, "/setpo-alias.asm") ||
      !expect_assembly_failure(
          "immediate IMUL memory destination", config,
          "/imul-memory-destination.asm",
          "BITS 32\nimul dword [eax], ecx, 2\n", &raw,
          CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_ENCODING,
          "/imul-memory-destination.asm") ||
      !expect_assembly_failure(
          "immediate IMUL byte operands", config, "/imul-byte.asm",
          "BITS 32\nimul al, cl, 2\n", &raw, CTOOL_ERR_INPUT,
          CTOOL_ASM_DIAG_ENCODING, "/imul-byte.asm") ||
      !expect_assembly_failure(
          "immediate IMUL width mismatch", config, "/imul-width.asm",
          "BITS 32\nimul eax, cx, 2\n", &raw, CTOOL_ERR_INPUT,
          CTOOL_ASM_DIAG_ENCODING, "/imul-width.asm") ||
      !expect_assembly_failure(
          "immediate IMUL lock prefix", config, "/imul-lock.asm",
          "BITS 32\nlock imul eax, ecx, 2\n", &raw, CTOOL_ERR_INPUT,
          CTOOL_ASM_DIAG_ENCODING, "/imul-lock.asm") ||
      !expect_assembly_failure(
          "immediate IMUL repeat prefix", config, "/imul-rep.asm",
          "BITS 32\nrep imul eax, ecx, 2\n", &raw, CTOOL_ERR_INPUT,
          CTOOL_ASM_DIAG_ENCODING, "/imul-rep.asm") ||
      !expect_assembly_failure(
          "immediate IMUL repeat-not-equal prefix", config,
          "/imul-repne.asm", "BITS 32\nrepne imul eax, ecx, 2\n", &raw,
          CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_ENCODING,
          "/imul-repne.asm") ||
      !expect_assembly_failure(
          "padding NOP byte memory", config, "/nop-byte.asm",
          "BITS 32\nnop byte [eax]\n", &raw, CTOOL_ERR_INPUT,
          CTOOL_ASM_DIAG_ENCODING, "/nop-byte.asm") ||
      !expect_assembly_failure(
          "padding NOP qword memory", config, "/nop-qword.asm",
          "BITS 32\nnop qword [eax]\n", &raw, CTOOL_ERR_INPUT,
          CTOOL_ASM_DIAG_ENCODING, "/nop-qword.asm") ||
      !expect_assembly_failure(
          "padding NOP byte register", config, "/nop-register.asm",
          "BITS 32\nnop al\n", &raw, CTOOL_ERR_INPUT,
          CTOOL_ASM_DIAG_ENCODING, "/nop-register.asm") ||
      !expect_assembly_failure(
          "padding NOP lock prefix", config, "/nop-lock.asm",
          "BITS 32\nlock nop [eax]\n", &raw, CTOOL_ERR_INPUT,
          CTOOL_ASM_DIAG_ENCODING, "/nop-lock.asm") ||
      !expect_assembly_failure(
          "padding NOP repeat prefix", config, "/nop-rep.asm",
          "BITS 32\nrep nop [eax]\n", &raw, CTOOL_ERR_INPUT,
          CTOOL_ASM_DIAG_ENCODING, "/nop-rep.asm") ||
      !expect_assembly_failure(
          "padding NOP extra operand", config, "/nop-extra.asm",
          "BITS 32\nnop eax, ecx\n", &raw, CTOOL_ERR_INPUT,
          CTOOL_ASM_DIAG_ENCODING, "/nop-extra.asm") ||
      !expect_assembly_failure(
          "instruction in bss", config, "/bss-instruction.asm",
          "BITS 32\nsection .bss\nmain: ret\n", &fixed,
          CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_INVALID_SECTION,
          "/bss-instruction.asm") ||
      !expect_assembly_failure(
          "missing fixed entry", config, "/missing-entry.asm",
          "BITS 32\nret\n", &fixed, CTOOL_ERR_NOT_FOUND,
          CTOOL_ASM_DIAG_ENTRY, "/missing-entry.asm")) {
    return 1;
  }

  limited_config = config;
  limited_config.limits.output_bytes = 2u;
  if (!expect_assembly_failure(
          "output rollback", limited_config, "/output-limit.asm",
          "BITS 32\ndd 1\n", &raw, CTOOL_ERR_LIMIT,
          CTOOL_ASM_DIAG_OUTPUT, "/output-limit.asm")) {
    return 1;
  }

  cycle_files[0].path = ctool_string("/cycle-a.asm");
  cycle_files[0].contents =
      ctool_bytes(cycle_a, (ctool_u32)(sizeof(cycle_a) - 1u));
  cycle_files[1].path = ctool_string("/cycle-b.asm");
  cycle_files[1].contents =
      ctool_bytes(cycle_b, (ctool_u32)(sizeof(cycle_b) - 1u));
  cycle_store.files = cycle_files;
  cycle_store.count = 2u;
  store_config = config;
  store_config.files.context = &cycle_store;
  store_config.files.file_size = contract_file_size;
  store_config.files.read_exact = contract_read_exact;
  if (!expect_assembly_failure(
          "include cycle", store_config, "/cycle-a.asm", cycle_a, &raw,
          CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_INCLUDE_CYCLE,
          "/cycle-b.asm")) {
    return 1;
  }

  for (index = 0u; index < 33u; index++) {
    int path_size = snprintf(depth_paths[index], sizeof(depth_paths[index]),
                             "/depth-%u.asm", (unsigned)index);
    int content_size;
    if (index + 1u < 33u) {
      content_size = snprintf(depth_contents[index],
                              sizeof(depth_contents[index]),
                              "%%include \"depth-%u.asm\"\n",
                              (unsigned)(index + 1u));
    } else {
      content_size = snprintf(depth_contents[index],
                              sizeof(depth_contents[index]), "BITS 32\n");
    }
    if (path_size <= 0 || content_size <= 0 ||
        (size_t)path_size >= sizeof(depth_paths[index]) ||
        (size_t)content_size >= sizeof(depth_contents[index])) {
      (void)fprintf(stderr, "include depth fixture formatting failed\n");
      return 1;
    }
    depth_files[index].path = ctool_string(depth_paths[index]);
    depth_files[index].contents = ctool_bytes(
        depth_contents[index], (ctool_u32)(unsigned)content_size);
  }
  depth_store.files = depth_files;
  depth_store.count = 33u;
  store_config.files.context = &depth_store;
  if (!expect_assembly_failure(
          "include depth", store_config, "/depth-0.asm", depth_contents[0],
          &raw, CTOOL_ERR_LIMIT, CTOOL_ASM_DIAG_INCLUDE_DEPTH,
          "/depth-31.asm")) {
    return 1;
  }

  bad_file.path = ctool_string("/bad.inc");
  bad_file.contents =
      ctool_bytes(include_bad, (ctool_u32)(sizeof(include_bad) - 1u));
  bad_store.files = &bad_file;
  bad_store.count = 1u;
  store_config.files.context = &bad_store;
  if (!expect_assembly_failure(
          "included encoding path", store_config, "/include-main.asm",
          include_bad_main, &raw, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_ENCODING,
          "/bad.inc")) {
    return 1;
  }

  (void)puts("errors: ok");
  return 0;
}

static int run_long_source_line(void) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_buffer_t *output;
  ctool_source_t source;
  ctool_asm_request_t request;
  ctool_asm_result_t result;
  ctool_bytes_t bytes;
  char source_text[512];
  ctool_u32 position = 0u;
  ctool_u32 index;
  ctool_status_t status;
  static const char prefix[] = "BITS 32\ndb ";
  for (index = 0u; index < (ctool_u32)(sizeof(prefix) - 1u); index++) {
    source_text[position++] = prefix[index];
  }
  for (index = 0u; index < 70u; index++) {
    source_text[position++] = '1';
    source_text[position++] = index + 1u == 70u ? '\n' : ',';
  }
  source_text[position] = '\0';
  status = ctool_host_adapter_init(&adapter, ".");
  if (!check_status(status, CTOOL_OK, "long line adapter init")) {
    return 1;
  }
  config = ctool_host_job_config(&adapter, ctool_default_limits());
  status = ctool_job_open(&config, &job);
  if (!check_status(status, CTOOL_OK, "long line job open")) {
    return 1;
  }
  status = ctool_job_open_buffer(job, 128u, config.limits.output_bytes,
                                 &output);
  if (!check_status(status, CTOOL_OK, "long line output open")) {
    ctool_job_close(job);
    return 1;
  }
  source.path.text = ctool_string("/long-line.asm");
  source.contents = ctool_bytes(source_text, position);
  init_raw_request(&request);
  (void)memset(&result, 0, sizeof(result));
  status = ctool_asm_assemble(job, &source, &request, output, &result);
  bytes = ctool_buffer_view(output);
  if (!check_status(status, CTOOL_OK, "long source line") ||
      bytes.size != 70u || ctool_job_diagnostic_count(job) != 0u) {
    (void)ctool_job_render_diagnostics(job);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }
  for (index = 0u; index < bytes.size; index++) {
    if (bytes.data[index] != 1u) {
      (void)fprintf(stderr, "long source line byte mismatch\n");
      ctool_buffer_close(output);
      ctool_job_close(job);
      return 1;
    }
  }
  ctool_buffer_close(output);
  ctool_job_close(job);
  (void)puts("long-line: ok");
  return 0;
}

int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "raw-basic") == 0) {
    return run_raw_basic();
  }
  if (argc == 2 && strcmp(argv[1], "raw-expressions") == 0) {
    return run_raw_expressions();
  }
  if (argc == 2 && strcmp(argv[1], "raw-source-contracts") == 0) {
    return run_raw_source_contracts();
  }
  if (argc == 2 && strcmp(argv[1], "object-basic") == 0) {
    return run_object_basic();
  }
  if (argc == 2 && strcmp(argv[1], "object-symbolic-immediate") == 0) {
    return run_object_symbolic_immediate();
  }
  if (argc == 2 && strcmp(argv[1], "object-entry") == 0) {
    return run_object_entry();
  }
  if (argc == 2 && strcmp(argv[1], "fixed-image") == 0) {
    return run_fixed_image();
  }
  if (argc == 2 && strcmp(argv[1], "fixed-directives") == 0) {
    return run_fixed_directives();
  }
  if (argc == 2 && strcmp(argv[1], "alignment") == 0) {
    return run_alignment();
  }
  if (argc == 2 && strcmp(argv[1], "include-resolution") == 0) {
    return run_include_resolution();
  }
  if (argc == 2 && strcmp(argv[1], "errors") == 0) {
    return run_error_contracts();
  }
  if (argc == 2 && strcmp(argv[1], "long-line") == 0) {
    return run_long_source_line();
  }
  (void)fprintf(stderr,
                "usage: cupidasm-contract raw-basic|raw-expressions|"
                "raw-source-contracts|object-basic|object-symbolic-immediate|"
                "object-entry|fixed-image|fixed-directives|alignment|"
                "include-resolution|errors|long-line\n");
  return 2;
}
