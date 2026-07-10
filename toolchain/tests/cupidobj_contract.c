#include "ctool.h"
#include "ctool_host.h"
#include "cupidobj.h"
#include "elf32.h"

#include <stdio.h>
#include <string.h>

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

static ctool_u32 build_segment_exec(ctool_u8 *bytes, ctool_u32 capacity) {
  const ctool_u32 image_size = 165u;
  ctool_u32 header;
  if (capacity < image_size) {
    return 0u;
  }
  (void)memset(bytes, 0, (size_t)image_size);
  bytes[0] = 0x7fu;
  bytes[1] = (ctool_u8)'E';
  bytes[2] = (ctool_u8)'L';
  bytes[3] = (ctool_u8)'F';
  bytes[4] = 1u;
  bytes[5] = 1u;
  bytes[6] = 1u;
  write_le16(bytes, 16u, 2u);
  write_le16(bytes, 18u, 3u);
  write_le32(bytes, 20u, 1u);
  write_le32(bytes, 24u, 0x4000u);
  write_le32(bytes, 28u, 52u);
  write_le16(bytes, 40u, 52u);
  write_le16(bytes, 42u, 32u);
  write_le16(bytes, 44u, 3u);

  header = 52u;
  write_le32(bytes, header, CTOOL_ELF32_PT_LOAD);
  write_le32(bytes, header + 4u, 160u);
  write_le32(bytes, header + 8u, 0x4000u);
  write_le32(bytes, header + 12u, 0x1000u);
  write_le32(bytes, header + 16u, 3u);
  write_le32(bytes, header + 20u, 3u);
  write_le32(bytes, header + 24u,
             CTOOL_ELF32_PF_R | CTOOL_ELF32_PF_X);
  write_le32(bytes, header + 28u, 1u);

  header += 32u;
  write_le32(bytes, header, CTOOL_ELF32_PT_LOAD);
  write_le32(bytes, header + 4u, 163u);
  write_le32(bytes, header + 8u, 0x4008u);
  write_le32(bytes, header + 12u, 0x1008u);
  write_le32(bytes, header + 16u, 2u);
  write_le32(bytes, header + 20u, 6u);
  write_le32(bytes, header + 24u,
             CTOOL_ELF32_PF_R | CTOOL_ELF32_PF_W);
  write_le32(bytes, header + 28u, 1u);

  header += 32u;
  write_le32(bytes, header, CTOOL_ELF32_PT_LOAD);
  write_le32(bytes, header + 4u, 165u);
  write_le32(bytes, header + 8u, 0x5000u);
  write_le32(bytes, header + 12u, 0x2000u);
  write_le32(bytes, header + 16u, 0u);
  write_le32(bytes, header + 20u, 16u);
  write_le32(bytes, header + 24u,
             CTOOL_ELF32_PF_R | CTOOL_ELF32_PF_W);
  write_le32(bytes, header + 28u, 1u);

  bytes[160u] = 0xaau;
  bytes[161u] = 0xbbu;
  bytes[162u] = 0xccu;
  bytes[163u] = 0xddu;
  bytes[164u] = 0xeeu;
  return image_size;
}

static ctool_u32 build_section_exec(ctool_u8 *bytes, ctool_u32 capacity,
                                    ctool_u32 payload_type) {
  static const char names[] = "\0.blob\0.shstrtab\0";
  const ctool_u32 image_size = 200u;
  ctool_u32 header;
  if (capacity < image_size) {
    return 0u;
  }
  (void)memset(bytes, 0, (size_t)image_size);
  bytes[0] = 0x7fu;
  bytes[1] = (ctool_u8)'E';
  bytes[2] = (ctool_u8)'L';
  bytes[3] = (ctool_u8)'F';
  bytes[4] = 1u;
  bytes[5] = 1u;
  bytes[6] = 1u;
  write_le16(bytes, 16u, 2u);
  write_le16(bytes, 18u, 3u);
  write_le32(bytes, 20u, 1u);
  write_le32(bytes, 24u, 0x3000u);
  write_le32(bytes, 32u, 80u);
  write_le16(bytes, 40u, 52u);
  write_le16(bytes, 46u, 40u);
  write_le16(bytes, 48u, 3u);
  write_le16(bytes, 50u, 2u);
  bytes[52u] = 0x11u;
  bytes[53u] = 0x22u;
  (void)memcpy(bytes + 56u, names, sizeof(names));

  header = 120u;
  write_le32(bytes, header, 1u);
  write_le32(bytes, header + 4u, payload_type);
  write_le32(bytes, header + 8u, CTOOL_ELF32_SHF_ALLOC);
  write_le32(bytes, header + 12u, 0x3000u);
  write_le32(bytes, header + 16u, 52u);
  write_le32(bytes, header + 20u, 2u);
  write_le32(bytes, header + 32u, 1u);

  header = 160u;
  write_le32(bytes, header, 7u);
  write_le32(bytes, header + 4u, 3u);
  write_le32(bytes, header + 16u, 56u);
  write_le32(bytes, header + 20u, (ctool_u32)sizeof(names));
  write_le32(bytes, header + 32u, 1u);
  return image_size;
}

static ctool_u32 build_empty_exec(ctool_u8 *bytes, ctool_u32 capacity) {
  if (capacity < 52u) {
    return 0u;
  }
  (void)memset(bytes, 0, 52u);
  bytes[0] = 0x7fu;
  bytes[1] = (ctool_u8)'E';
  bytes[2] = (ctool_u8)'L';
  bytes[3] = (ctool_u8)'F';
  bytes[4] = 1u;
  bytes[5] = 1u;
  bytes[6] = 1u;
  write_le16(bytes, 16u, 2u);
  write_le16(bytes, 18u, 3u);
  write_le32(bytes, 20u, 1u);
  write_le16(bytes, 40u, 52u);
  return 52u;
}

static int string_equal(ctool_string_t actual, const char *expected) {
  ctool_u32 size = (ctool_u32)strlen(expected);
  return actual.size == size &&
         (size == 0u || memcmp(actual.data, expected, (size_t)size) == 0);
}

static int open_job(ctool_host_adapter_t *adapter,
                    ctool_job_config_t *config, ctool_job_t **job) {
  ctool_status_t status = ctool_host_adapter_init(adapter, ".");
  if (status != CTOOL_OK) {
    return 0;
  }
  *config = ctool_host_job_config(adapter, ctool_default_limits());
  status = ctool_job_open(config, job);
  return status == CTOOL_OK;
}

static int run_wrap_basic(void) {
  static const ctool_u8 payload[] = {0x00u, 0x7fu, 0x80u, 0xffu};
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *output = (ctool_buffer_t *)0;
  ctool_source_t source;
  ctool_source_t object_source;
  ctool_obj_request_t request;
  ctool_obj_result_t result;
  ctool_elf32_object_t object;
  const ctool_elf32_section_t *section = (const ctool_elf32_section_t *)0;
  const ctool_elf32_symbol_t *start = (const ctool_elf32_symbol_t *)0;
  const ctool_elf32_symbol_t *end = (const ctool_elf32_symbol_t *)0;
  const ctool_elf32_symbol_t *size = (const ctool_elf32_symbol_t *)0;
  ctool_status_t status;
  ctool_u32 index;
  int ok = 1;

  (void)memset(&request, 0, sizeof(request));
  (void)memset(&result, 0xa5, sizeof(result));
  if (!open_job(&adapter, &config, &job)) {
    (void)fprintf(stderr, "wrap-basic: job setup failed\n");
    return 1;
  }
  status = ctool_job_open_buffer(job, 64u, config.limits.output_bytes,
                                 &output);
  if (status != CTOOL_OK) {
    ctool_job_close(job);
    return 1;
  }

  source.path.text = ctool_string("/payload.bin");
  source.contents = ctool_bytes(payload, (ctool_u32)sizeof(payload));
  request.operation = CTOOL_OBJ_WRAP_BINARY;
  request.input = &source;
  request.as.wrap_binary.section_name = ctool_string(".rodata");
  request.as.wrap_binary.section_flags = CTOOL_ELF32_SHF_ALLOC;
  request.as.wrap_binary.section_alignment = 4u;
  request.as.wrap_binary.start_symbol = ctool_string("payload_start");
  request.as.wrap_binary.end_symbol = ctool_string("payload_end");
  request.as.wrap_binary.size_symbol = ctool_string("payload_size");

  status = ctool_obj_transform(job, &request, output, &result);
  if (status != CTOOL_OK || result.bytes.data == (const ctool_u8 *)0 ||
      result.bytes.size == 0u || result.base_address != 0u ||
      result.end_address != 0u ||
      result.bytes.data != ctool_buffer_view(output).data ||
      result.bytes.size != ctool_buffer_view(output).size) {
    (void)fprintf(stderr, "wrap-basic: transform result mismatch\n");
    ok = 0;
  }

  object_source.path.text = ctool_string("/wrapped.o");
  object_source.contents = result.bytes;
  if (ok != 0 &&
      ctool_elf32_read(job, &object_source, &object) != CTOOL_OK) {
    (void)fprintf(stderr, "wrap-basic: output is not readable ELF32\n");
    ok = 0;
  }
  if (ok != 0 && object.file_type != CTOOL_ELF32_ET_REL) {
    ok = 0;
  }
  if (ok != 0) {
    for (index = 0u; index < object.section_count; index++) {
      if (string_equal(object.sections[index].name, ".rodata")) {
        section = &object.sections[index];
      }
    }
    for (index = 0u; index < object.symbol_count; index++) {
      if (string_equal(object.symbols[index].name, "payload_start")) {
        start = &object.symbols[index];
      } else if (string_equal(object.symbols[index].name, "payload_end")) {
        end = &object.symbols[index];
      } else if (string_equal(object.symbols[index].name, "payload_size")) {
        size = &object.symbols[index];
      }
    }
  }
  if (ok != 0 &&
      (section == (const ctool_elf32_section_t *)0 ||
       section->type != CTOOL_ELF32_SHT_PROGBITS ||
       section->flags != CTOOL_ELF32_SHF_ALLOC || section->alignment != 4u ||
       section->size != (ctool_u32)sizeof(payload) ||
       memcmp(section->contents.data, payload, sizeof(payload)) != 0 ||
       start == (const ctool_elf32_symbol_t *)0 ||
       end == (const ctool_elf32_symbol_t *)0 ||
       size == (const ctool_elf32_symbol_t *)0 ||
       start->binding != CTOOL_ELF32_BIND_GLOBAL ||
       start->type != CTOOL_ELF32_SYMBOL_NOTYPE ||
       start->placement != CTOOL_ELF32_SYMBOL_DEFINED || start->value != 0u ||
       start->section_file_index != section->file_index ||
       end->binding != CTOOL_ELF32_BIND_GLOBAL ||
       end->type != CTOOL_ELF32_SYMBOL_NOTYPE ||
       end->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
       end->value != (ctool_u32)sizeof(payload) ||
       end->section_file_index != section->file_index ||
       size->binding != CTOOL_ELF32_BIND_GLOBAL ||
       size->type != CTOOL_ELF32_SYMBOL_NOTYPE ||
       size->placement != CTOOL_ELF32_SYMBOL_ABSOLUTE ||
       size->section_file_index != CTOOL_ELF32_NO_SECTION ||
       size->value != (ctool_u32)sizeof(payload))) {
    (void)fprintf(stderr, "wrap-basic: wrapped object semantics mismatch\n");
    ok = 0;
  }

  ctool_buffer_close(output);
  ctool_job_close(job);
  return ok != 0 ? 0 : 1;
}

static int run_extract_basic(void) {
  static const ctool_u8 expected[] = {0xaau, 0xbbu, 0xccu, 0x00u, 0x00u,
                                      0x00u, 0x00u, 0x00u, 0xddu, 0xeeu};
  ctool_u8 image[165];
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *output = (ctool_buffer_t *)0;
  ctool_source_t source;
  ctool_obj_request_t request;
  ctool_obj_result_t result;
  ctool_status_t status;
  int ok = 1;

  if (build_segment_exec(image, (ctool_u32)sizeof(image)) !=
      (ctool_u32)sizeof(image)) {
    return 1;
  }
  if (!open_job(&adapter, &config, &job)) {
    return 1;
  }
  status = ctool_job_open_buffer(job, 16u, config.limits.output_bytes,
                                 &output);
  if (status != CTOOL_OK) {
    ctool_job_close(job);
    return 1;
  }
  source.path.text = ctool_string("/segments.elf");
  source.contents = ctool_bytes(image, (ctool_u32)sizeof(image));
  (void)memset(&request, 0, sizeof(request));
  request.operation = CTOOL_OBJ_EXTRACT_FLAT;
  request.input = &source;
  (void)memset(&result, 0xa5, sizeof(result));
  status = ctool_obj_transform(job, &request, output, &result);
  if (status != CTOOL_OK || result.base_address != 0x1000u ||
      result.end_address != 0x100au ||
      result.bytes.size != (ctool_u32)sizeof(expected) ||
      result.bytes.data != ctool_buffer_view(output).data ||
      memcmp(result.bytes.data, expected, sizeof(expected)) != 0) {
    (void)fprintf(stderr, "extract-basic: gap/BSS result mismatch\n");
    ok = 0;
  }
  ctool_buffer_close(output);
  ctool_job_close(job);
  return ok != 0 ? 0 : 1;
}

static int run_wrap_model(void) {
  static const ctool_u8 payload[] = {0x31u, 0x32u, 0x33u};
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *first = (ctool_buffer_t *)0;
  ctool_buffer_t *second = (ctool_buffer_t *)0;
  ctool_source_t source;
  ctool_obj_request_t request;
  ctool_obj_result_t first_result;
  ctool_obj_result_t second_result;
  ctool_source_t object_source;
  ctool_elf32_object_t object;
  ctool_u32 index;
  ctool_bool found_empty = CTOOL_FALSE;
  ctool_status_t status;
  int ok = 1;

  if (!open_job(&adapter, &config, &job)) {
    return 1;
  }
  status = ctool_job_open_buffer(job, 64u, config.limits.output_bytes, &first);
  if (status == CTOOL_OK) {
    status =
        ctool_job_open_buffer(job, 64u, config.limits.output_bytes, &second);
  }
  if (status != CTOOL_OK) {
    if (first != (ctool_buffer_t *)0) {
      ctool_buffer_close(first);
    }
    ctool_job_close(job);
    return 1;
  }
  source.path.text = ctool_string("/model.bin");
  source.contents = ctool_bytes(payload, (ctool_u32)sizeof(payload));
  (void)memset(&request, 0, sizeof(request));
  request.operation = CTOOL_OBJ_WRAP_BINARY;
  request.input = &source;
  request.as.wrap_binary.section_name = ctool_string(".data");
  request.as.wrap_binary.section_flags =
      CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE;
  request.as.wrap_binary.section_alignment = 1u;
  request.as.wrap_binary.start_symbol = ctool_string("model_start");
  request.as.wrap_binary.end_symbol = ctool_string("model_end");
  request.as.wrap_binary.size_symbol = ctool_string("model_size");
  if (ctool_obj_transform(job, &request, first, &first_result) != CTOOL_OK ||
      ctool_obj_transform(job, &request, second, &second_result) != CTOOL_OK ||
      first_result.bytes.size != second_result.bytes.size ||
      memcmp(first_result.bytes.data, second_result.bytes.data,
             (size_t)first_result.bytes.size) != 0) {
    (void)fprintf(stderr, "wrap-model: output is not deterministic\n");
    ok = 0;
  }

  ctool_buffer_clear(first);
  source.contents = ctool_bytes((const void *)0, 0u);
  request.as.wrap_binary.start_symbol = ctool_string("empty_start");
  request.as.wrap_binary.end_symbol = ctool_string("empty_end");
  request.as.wrap_binary.size_symbol = ctool_string("empty_size");
  if (ctool_obj_transform(job, &request, first, &first_result) != CTOOL_OK) {
    (void)fprintf(stderr, "wrap-model: empty payload was rejected\n");
    ok = 0;
  }
  object_source.path.text = ctool_string("/empty.o");
  object_source.contents = first_result.bytes;
  if (ok != 0 && ctool_elf32_read(job, &object_source, &object) == CTOOL_OK) {
    for (index = 0u; index < object.section_count; index++) {
      if (string_equal(object.sections[index].name, ".data") &&
          object.sections[index].size == 0u) {
        found_empty = CTOOL_TRUE;
      }
    }
  }
  if (found_empty == CTOOL_FALSE) {
    (void)fprintf(stderr, "wrap-model: empty section missing\n");
    ok = 0;
  }
  ctool_buffer_close(second);
  ctool_buffer_close(first);
  ctool_job_close(job);
  return ok != 0 ? 0 : 1;
}

static int run_extract_fallback(void) {
  static const ctool_u8 expected[] = {0x11u, 0x22u};
  ctool_u8 image[200];
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *first = (ctool_buffer_t *)0;
  ctool_buffer_t *second = (ctool_buffer_t *)0;
  ctool_source_t source;
  ctool_obj_request_t request;
  ctool_obj_result_t first_result;
  ctool_obj_result_t second_result;
  ctool_status_t status;
  int ok = 1;

  if (build_section_exec(image, (ctool_u32)sizeof(image),
                         CTOOL_ELF32_SHT_PROGBITS) !=
          (ctool_u32)sizeof(image) ||
      !open_job(&adapter, &config, &job)) {
    return 1;
  }
  status = ctool_job_open_buffer(job, 8u, config.limits.output_bytes, &first);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 8u, config.limits.output_bytes,
                                   &second);
  }
  if (status != CTOOL_OK) {
    if (first != (ctool_buffer_t *)0) {
      ctool_buffer_close(first);
    }
    ctool_job_close(job);
    return 1;
  }
  source.path.text = ctool_string("/sectioned.elf");
  source.contents = ctool_bytes(image, (ctool_u32)sizeof(image));
  (void)memset(&request, 0, sizeof(request));
  request.operation = CTOOL_OBJ_EXTRACT_FLAT;
  request.input = &source;
  if (ctool_obj_transform(job, &request, first, &first_result) != CTOOL_OK ||
      ctool_obj_transform(job, &request, second, &second_result) != CTOOL_OK ||
      first_result.base_address != 0x3000u ||
      first_result.end_address != 0x3002u ||
      first_result.bytes.size != (ctool_u32)sizeof(expected) ||
      memcmp(first_result.bytes.data, expected, sizeof(expected)) != 0 ||
      first_result.bytes.size != second_result.bytes.size ||
      memcmp(first_result.bytes.data, second_result.bytes.data,
             (size_t)first_result.bytes.size) != 0) {
    (void)fprintf(stderr, "extract-fallback: result mismatch\n");
    ok = 0;
  }
  ctool_buffer_close(second);
  ctool_buffer_close(first);
  ctool_job_close(job);
  return ok != 0 ? 0 : 1;
}

static int result_is_zero(const ctool_obj_result_t *result) {
  return result->bytes.data == (const ctool_u8 *)0 && result->bytes.size == 0u &&
         result->base_address == 0u && result->end_address == 0u;
}

static int expect_failure(ctool_job_t *job, ctool_buffer_t *output,
                          const ctool_obj_request_t *request,
                          ctool_status_t expected_status,
                          ctool_u32 expected_code, ctool_u32 output_size,
                          const char *case_name) {
  ctool_obj_result_t result;
  ctool_u32 before = ctool_job_diagnostic_count(job);
  ctool_status_t status;
  (void)memset(&result, 0xa5, sizeof(result));
  status = ctool_obj_transform(job, request, output, &result);
  if (status != expected_status || !result_is_zero(&result) ||
      ctool_buffer_view(output).size != output_size ||
      ctool_job_diagnostic_count(job) <= before ||
      ctool_job_diagnostic(job, ctool_job_diagnostic_count(job) - 1u)->code !=
          expected_code) {
    (void)fprintf(stderr, "%s: failure contract mismatch\n", case_name);
    return 0;
  }
  return 1;
}

static int run_errors(void) {
  static const ctool_u8 payload[] = {1u, 2u, 3u};
  static const ctool_u8 malformed[] = {0x7fu};
  ctool_u8 image[200];
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *output = (ctool_buffer_t *)0;
  ctool_buffer_t *limited = (ctool_buffer_t *)0;
  ctool_source_t source;
  ctool_obj_request_t request;
  ctool_status_t status;
  int ok = 1;

  if (!open_job(&adapter, &config, &job)) {
    return 1;
  }
  status = ctool_job_open_buffer(job, 16u, config.limits.output_bytes,
                                 &output);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 1u, 5u, &limited);
  }
  if (status != CTOOL_OK) {
    if (output != (ctool_buffer_t *)0) {
      ctool_buffer_close(output);
    }
    ctool_job_close(job);
    return 1;
  }
  source.path.text = ctool_string("/errors.bin");
  source.contents = ctool_bytes(payload, (ctool_u32)sizeof(payload));
  (void)memset(&request, 0, sizeof(request));
  request.operation = CTOOL_OBJ_WRAP_BINARY;
  request.input = &source;
  request.as.wrap_binary.section_name = ctool_string(".data");
  request.as.wrap_binary.section_flags =
      CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE;
  request.as.wrap_binary.section_alignment = 1u;
  request.as.wrap_binary.start_symbol = ctool_string("error_start");
  request.as.wrap_binary.end_symbol = ctool_string("error_end");
  request.as.wrap_binary.size_symbol = ctool_string("error_size");

  request.operation = (ctool_obj_operation_t)0;
  ok &= expect_failure(job, output, &request, CTOOL_ERR_INVALID_ARGUMENT,
                       CTOOL_OBJ_DIAG_INVALID_REQUEST, 0u,
                       "invalid operation");
  request.operation = CTOOL_OBJ_WRAP_BINARY;
  request.input = (const ctool_source_t *)0;
  ok &= expect_failure(job, output, &request, CTOOL_ERR_INVALID_ARGUMENT,
                       CTOOL_OBJ_DIAG_INVALID_REQUEST, 0u, "missing input");
  request.input = &source;

  status = ctool_buffer_put_u8(output, 0x5au);
  if (status != CTOOL_OK) {
    ok = 0;
  }
  ok &= expect_failure(job, output, &request, CTOOL_ERR_INVALID_ARGUMENT,
                       CTOOL_OBJ_DIAG_INVALID_REQUEST, 1u,
                       "nonempty output");
  if (ctool_buffer_view(output).size != 1u ||
      ctool_buffer_view(output).data[0] != 0x5au) {
    ok = 0;
  }
  ctool_buffer_clear(output);

  source.contents.data = (const ctool_u8 *)0;
  source.contents.size = 1u;
  ok &= expect_failure(job, output, &request, CTOOL_ERR_INVALID_ARGUMENT,
                       CTOOL_OBJ_DIAG_INVALID_INPUT, 0u, "bad source view");
  source.contents = ctool_bytes(payload, (ctool_u32)sizeof(payload));

  request.as.wrap_binary.end_symbol = request.as.wrap_binary.start_symbol;
  ok &= expect_failure(job, output, &request, CTOOL_ERR_INPUT,
                       CTOOL_OBJ_DIAG_SYMBOL_COLLISION, 0u,
                       "symbol collision");
  request.as.wrap_binary.end_symbol = ctool_string("error_end");
  request.as.wrap_binary.section_flags = 0x00000008u;
  ok &= expect_failure(job, output, &request, CTOOL_ERR_INPUT,
                       CTOOL_OBJ_DIAG_INVALID_SECTION, 0u, "invalid flags");
  request.as.wrap_binary.section_flags = CTOOL_ELF32_SHF_ALLOC;
  request.as.wrap_binary.section_alignment = 3u;
  ok &= expect_failure(job, output, &request, CTOOL_ERR_INPUT,
                       CTOOL_OBJ_DIAG_INVALID_SECTION, 0u,
                       "invalid alignment");
  request.as.wrap_binary.section_alignment = 1u;
  request.as.wrap_binary.section_name = ctool_string(".symtab");
  ok &= expect_failure(job, output, &request, CTOOL_ERR_INPUT,
                       CTOOL_OBJ_DIAG_INVALID_SECTION, 0u,
                       "reserved section");
  request.as.wrap_binary.section_name = ctool_string(".data");
  request.as.wrap_binary.start_symbol = ctool_string("");
  ok &= expect_failure(job, output, &request, CTOOL_ERR_INPUT,
                       CTOOL_OBJ_DIAG_INVALID_SYMBOL, 0u, "empty symbol");
  request.as.wrap_binary.start_symbol = ctool_string("error_start");

  ok &= expect_failure(job, limited, &request, CTOOL_ERR_LIMIT,
                       CTOOL_OBJ_DIAG_LIMIT, 0u, "wrap output limit");

  source.path.text = ctool_string("/malformed.elf");
  source.contents = ctool_bytes(malformed, (ctool_u32)sizeof(malformed));
  request.operation = CTOOL_OBJ_EXTRACT_FLAT;
  ok &= expect_failure(job, output, &request, CTOOL_ERR_INPUT,
                       CTOOL_OBJ_DIAG_INVALID_INPUT, 0u, "malformed ELF");

  source.path.text = ctool_string("/empty.elf");
  source.contents = ctool_bytes(image, build_empty_exec(image,
                                                        (ctool_u32)sizeof(image)));
  ok &= expect_failure(job, output, &request, CTOOL_ERR_INPUT,
                       CTOOL_OBJ_DIAG_NO_LOAD, 0u, "empty executable");

  source.path.text = ctool_string("/unsupported-section.elf");
  source.contents = ctool_bytes(
      image, build_section_exec(image, (ctool_u32)sizeof(image), 7u));
  ok &= expect_failure(job, output, &request, CTOOL_ERR_UNSUPPORTED,
                       CTOOL_OBJ_DIAG_UNSUPPORTED, 0u,
                       "unsupported section fallback");

  (void)build_segment_exec(image, (ctool_u32)sizeof(image));
  write_le32(image, 96u, 0x1002u);
  source.path.text = ctool_string("/overlap.elf");
  source.contents = ctool_bytes(image, 165u);
  ok &= expect_failure(job, output, &request, CTOOL_ERR_INPUT,
                       CTOOL_OBJ_DIAG_OVERLAP, 0u, "overlap rollback");

  (void)build_segment_exec(image, (ctool_u32)sizeof(image));
  write_le32(image, 64u, 0xfffffffeu);
  source.path.text = ctool_string("/overflow.elf");
  source.contents = ctool_bytes(image, 165u);
  ok &= expect_failure(job, output, &request, CTOOL_ERR_OVERFLOW,
                       CTOOL_OBJ_DIAG_ADDRESS_OVERFLOW, 0u,
                       "address overflow");

  (void)build_segment_exec(image, (ctool_u32)sizeof(image));
  source.path.text = ctool_string("/limited.elf");
  source.contents = ctool_bytes(image, 165u);
  ok &= expect_failure(job, limited, &request, CTOOL_ERR_LIMIT,
                       CTOOL_OBJ_DIAG_LIMIT, 0u, "output limit rollback");

  ctool_buffer_close(limited);
  ctool_buffer_close(output);
  ctool_job_close(job);
  return ok != 0 ? 0 : 1;
}

int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "wrap-basic") == 0) {
    return run_wrap_basic();
  }
  if (argc == 2 && strcmp(argv[1], "extract-basic") == 0) {
    return run_extract_basic();
  }
  if (argc == 2 && strcmp(argv[1], "wrap-model") == 0) {
    return run_wrap_model();
  }
  if (argc == 2 && strcmp(argv[1], "extract-fallback") == 0) {
    return run_extract_fallback();
  }
  if (argc == 2 && strcmp(argv[1], "errors") == 0) {
    return run_errors();
  }
  (void)fprintf(stderr,
                "usage: cupidobj-contract wrap-basic|wrap-model|"
                "extract-basic|extract-fallback|errors\n");
  return 2;
}
