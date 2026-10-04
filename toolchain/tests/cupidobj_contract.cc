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

static int profile_snapshot_put_bytes(ctool_u8 *bytes, ctool_u32 capacity,
                                      ctool_u32 *size,
                                      const ctool_u8 *value,
                                      ctool_u32 value_size) {
  ctool_u32 index;
  if (*size > capacity || capacity - *size < 4u ||
      value_size > capacity - *size - 4u) {
    return 0;
  }
  write_le32(bytes, *size, value_size);
  *size += 4u;
  for (index = 0u; index < value_size; index++) {
    bytes[*size + index] = value[index];
  }
  *size += value_size;
  return 1;
}

static int profile_snapshot_put_string(ctool_u8 *bytes,
                                       ctool_u32 capacity,
                                       ctool_u32 *size,
                                       const char *value) {
  ctool_string_t string = ctool_string(value);
  return profile_snapshot_put_bytes(
      bytes, capacity, size, (const ctool_u8 *)(const void *)string.data,
      string.size);
}

static int profile_snapshot_put_u32(ctool_u8 *bytes, ctool_u32 capacity,
                                    ctool_u32 *size, ctool_u32 value) {
  if (*size > capacity || capacity - *size < 4u) {
    return 0;
  }
  write_le32(bytes, *size, value);
  *size += 4u;
  return 1;
}

static ctool_u32 build_profile_snapshot(ctool_u8 *bytes,
                                        ctool_u32 capacity,
                                        int reverse) {
  static const ctool_u8 magic[8] = {
      (ctool_u8)'C', (ctool_u8)'U', (ctool_u8)'P', (ctool_u8)'R',
      (ctool_u8)'O', (ctool_u8)'F', (ctool_u8)'1', 0u};
  static const ctool_u8 abc[] = {(ctool_u8)'a', (ctool_u8)'b',
                                 (ctool_u8)'c'};
  static const ctool_u8 zed[] = {(ctool_u8)'z', (ctool_u8)'\n'};
  ctool_u32 size = 0u;
  ctool_u32 index;
  if (capacity < 8u) {
    return 0u;
  }
  for (index = 0u; index < 8u; index++) {
    bytes[index] = magic[index];
  }
  size = 8u;
  if (!profile_snapshot_put_string(bytes, capacity, &size,
                                   "cupid.doom-profile-inputs.v1") ||
      !profile_snapshot_put_u32(bytes, capacity, &size, 2u)) {
    return 0u;
  }
  if (reverse) {
    if (!profile_snapshot_put_string(bytes, capacity, &size, "doom-tree") ||
        !profile_snapshot_put_u32(bytes, capacity, &size, 2u) ||
        !profile_snapshot_put_string(bytes, capacity, &size, "z.h") ||
        !profile_snapshot_put_string(bytes, capacity, &size, "a.h") ||
        !profile_snapshot_put_u32(bytes, capacity, &size, 2u) ||
        !profile_snapshot_put_string(bytes, capacity, &size,
                                     "kernel/doom/z.cc") ||
        !profile_snapshot_put_string(bytes, capacity, &size,
                                     "kernel/doom/a.cc") ||
        !profile_snapshot_put_string(bytes, capacity, &size, "doom-compat") ||
        !profile_snapshot_put_u32(bytes, capacity, &size, 1u) ||
        !profile_snapshot_put_string(bytes, capacity, &size, "a.h") ||
        !profile_snapshot_put_u32(bytes, capacity, &size, 1u) ||
        !profile_snapshot_put_string(bytes, capacity, &size,
                                     "kernel/doom/d.cc")) {
      return 0u;
    }
  } else {
    if (!profile_snapshot_put_string(bytes, capacity, &size, "doom-compat") ||
        !profile_snapshot_put_u32(bytes, capacity, &size, 1u) ||
        !profile_snapshot_put_string(bytes, capacity, &size, "a.h") ||
        !profile_snapshot_put_u32(bytes, capacity, &size, 1u) ||
        !profile_snapshot_put_string(bytes, capacity, &size,
                                     "kernel/doom/d.cc") ||
        !profile_snapshot_put_string(bytes, capacity, &size, "doom-tree") ||
        !profile_snapshot_put_u32(bytes, capacity, &size, 2u) ||
        !profile_snapshot_put_string(bytes, capacity, &size, "a.h") ||
        !profile_snapshot_put_string(bytes, capacity, &size, "z.h") ||
        !profile_snapshot_put_u32(bytes, capacity, &size, 2u) ||
        !profile_snapshot_put_string(bytes, capacity, &size,
                                     "kernel/doom/a.cc") ||
        !profile_snapshot_put_string(bytes, capacity, &size,
                                     "kernel/doom/z.cc")) {
      return 0u;
    }
  }
  if (!profile_snapshot_put_u32(bytes, capacity, &size, 2u)) {
    return 0u;
  }
  if (reverse) {
    if (!profile_snapshot_put_string(bytes, capacity, &size, "z.h") ||
        !profile_snapshot_put_bytes(bytes, capacity, &size, zed,
                                    (ctool_u32)sizeof(zed)) ||
        !profile_snapshot_put_string(bytes, capacity, &size, "a.h") ||
        !profile_snapshot_put_bytes(bytes, capacity, &size, abc,
                                    (ctool_u32)sizeof(abc))) {
      return 0u;
    }
  } else if (!profile_snapshot_put_string(bytes, capacity, &size, "a.h") ||
             !profile_snapshot_put_bytes(bytes, capacity, &size, abc,
                                         (ctool_u32)sizeof(abc)) ||
             !profile_snapshot_put_string(bytes, capacity, &size, "z.h") ||
             !profile_snapshot_put_bytes(bytes, capacity, &size, zed,
                                         (ctool_u32)sizeof(zed))) {
    return 0u;
  }
  return size;
}

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

static ctool_u16 read_be16(const ctool_u8 *bytes, ctool_u32 offset) {
  return (ctool_u16)(((ctool_u16)bytes[offset] << 8u) |
                     (ctool_u16)bytes[offset + 1u]);
}

static ctool_u32 read_be32(const ctool_u8 *bytes, ctool_u32 offset) {
  return ((ctool_u32)bytes[offset] << 24u) |
         ((ctool_u32)bytes[offset + 1u] << 16u) |
         ((ctool_u32)bytes[offset + 2u] << 8u) |
         (ctool_u32)bytes[offset + 3u];
}

static int byte_range_is_zero(const ctool_u8 *bytes, ctool_u32 begin,
                              ctool_u32 end) {
  ctool_u32 index;
  for (index = begin; index < end; index++) {
    if (bytes[index] != 0u) {
      return 0;
    }
  }
  return 1;
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
  static const ctool_u8 payload[] = {0x00u, 0x0du, 0x0au,
                                     0x7fu, 0x80u, 0xffu};
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

static int arena_marks_equal(ctool_arena_mark_t left,
                             ctool_arena_mark_t right) {
  return left.owner == right.owner && left.block == right.block &&
                 left.used == right.used &&
                 left.generation == right.generation
             ? 1
             : 0;
}

static int run_wrap_text(void) {
  static const ctool_u8 payload[] = {'a',  '\r', '\n', 'b', '\n',
                                     'c',  '\r', 'd',  '\r', '\n'};
  static const ctool_u8 expected[] = {'a', '\n', 'b', '\n',
                                      'c', '\r', 'd', '\n'};
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *first = (ctool_buffer_t *)0;
  ctool_buffer_t *second = (ctool_buffer_t *)0;
  ctool_source_t source;
  ctool_source_t object_source;
  ctool_obj_request_t request;
  ctool_obj_result_t first_result;
  ctool_obj_result_t second_result;
  ctool_elf32_object_t object;
  const ctool_elf32_section_t *section = (const ctool_elf32_section_t *)0;
  const ctool_elf32_symbol_t *end = (const ctool_elf32_symbol_t *)0;
  const ctool_elf32_symbol_t *size = (const ctool_elf32_symbol_t *)0;
  ctool_arena_mark_t mark;
  ctool_status_t status;
  ctool_u32 index;
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

  source.path.text = ctool_string("/manual.txt");
  source.contents = ctool_bytes(payload, (ctool_u32)sizeof(payload));
  (void)memset(&request, 0, sizeof(request));
  request.operation = CTOOL_OBJ_WRAP_TEXT;
  request.input = &source;
  request.as.wrap_binary.section_name = ctool_string(".data");
  request.as.wrap_binary.section_flags =
      CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE;
  request.as.wrap_binary.section_alignment = 1u;
  request.as.wrap_binary.start_symbol = ctool_string("manual_start");
  request.as.wrap_binary.end_symbol = ctool_string("manual_end");
  request.as.wrap_binary.size_symbol = ctool_string("manual_size");

  mark = ctool_arena_mark(ctool_job_arena(job));
  if (ctool_obj_transform(job, &request, first, &first_result) != CTOOL_OK ||
      !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) ||
      ctool_obj_transform(job, &request, second, &second_result) != CTOOL_OK ||
      first_result.bytes.size != second_result.bytes.size ||
      memcmp(first_result.bytes.data, second_result.bytes.data,
             (size_t)first_result.bytes.size) != 0) {
    (void)fprintf(stderr, "wrap-text: deterministic transform mismatch\n");
    ok = 0;
  }

  object_source.path.text = ctool_string("/manual.o");
  object_source.contents = first_result.bytes;
  if (ok != 0 &&
      ctool_elf32_read(job, &object_source, &object) != CTOOL_OK) {
    ok = 0;
  }
  if (ok != 0) {
    for (index = 0u; index < object.section_count; index++) {
      if (string_equal(object.sections[index].name, ".data")) {
        section = &object.sections[index];
      }
    }
    for (index = 0u; index < object.symbol_count; index++) {
      if (string_equal(object.symbols[index].name, "manual_end")) {
        end = &object.symbols[index];
      } else if (string_equal(object.symbols[index].name, "manual_size")) {
        size = &object.symbols[index];
      }
    }
  }
  if (ok != 0 &&
      (section == (const ctool_elf32_section_t *)0 ||
       section->size != (ctool_u32)sizeof(expected) ||
       memcmp(section->contents.data, expected, sizeof(expected)) != 0 ||
       end == (const ctool_elf32_symbol_t *)0 ||
       end->value != (ctool_u32)sizeof(expected) ||
       size == (const ctool_elf32_symbol_t *)0 ||
       size->value != (ctool_u32)sizeof(expected))) {
    (void)fprintf(stderr, "wrap-text: canonical contents mismatch\n");
    ok = 0;
  }

  ctool_buffer_close(second);
  ctool_buffer_close(first);
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

static int expect_rewound_failure(ctool_job_t *job, ctool_buffer_t *output,
                                  const ctool_obj_request_t *request,
                                  ctool_status_t expected_status,
                                  ctool_u32 expected_code,
                                  const char *case_name) {
  ctool_arena_mark_t mark = ctool_arena_mark(ctool_job_arena(job));
  if (!expect_failure(job, output, request, expected_status, expected_code, 0u,
                      case_name)) {
    return 0;
  }
  if (!arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
    (void)fprintf(stderr, "%s: failure did not rewind the arena\n", case_name);
    return 0;
  }
  return 1;
}

static int expect_failure_message(ctool_job_t *job,
                                  ctool_buffer_t *output,
                                  const ctool_obj_request_t *request,
                                  ctool_status_t expected_status,
                                  ctool_u32 expected_code,
                                  const char *expected_message,
                                  const char *case_name) {
  const ctool_diagnostic_t *diagnostic;
  if (!expect_failure(job, output, request, expected_status, expected_code,
                      0u, case_name)) {
    return 0;
  }
  diagnostic =
      ctool_job_diagnostic(job, ctool_job_diagnostic_count(job) - 1u);
  if (diagnostic == (const ctool_diagnostic_t *)0 ||
      !string_equal(diagnostic->message, expected_message)) {
    (void)fprintf(stderr, "%s: diagnostic mismatch\n", case_name);
    return 0;
  }
  return 1;
}

static int run_wrap_jpeg(void) {
  enum { JPEG_REJECTION_COUNT = 21 };
  static const ctool_u8 baseline[] = {
      0xffu, 0xd8u, 0xffu, 0xc0u, 0x00u, 0x0bu, 0x08u,
      0x00u, 0x01u, 0x00u, 0x01u, 0x01u, 0x01u, 0x11u,
      0x00u, 0xffu, 0xdau, 0x00u, 0x08u, 0x01u, 0x01u,
      0x00u, 0x00u, 0x3fu, 0x00u, 0xffu, 0xd9u};
  static const char *const rejection_names[JPEG_REJECTION_COUNT] = {
      "missing SOI",
      "malformed marker stream",
      "stuffed data before scan",
      "trailing bytes after EOI",
      "standalone marker",
      "truncated marker length",
      "invalid marker length",
      "duplicate frame",
      "truncated frame",
      "invalid frame components",
      "invalid frame precision",
      "invalid frame size",
      "scan before frame",
      "truncated scan",
      "invalid scan components",
      "partial entropy marker",
      "progressive frame",
      "missing frame",
      "unsupported frame",
      "missing scan",
      "missing EOI"};
  static const char *const rejection_messages[JPEG_REJECTION_COUNT] = {
      "JPEG input has no SOI marker",
      "JPEG marker stream is malformed outside a scan",
      "JPEG marker stream contains stuffed data before a scan",
      "JPEG input has trailing bytes after the EOI marker",
      "unexpected standalone JPEG marker 0xd8",
      "JPEG marker length is truncated",
      "JPEG marker length is invalid",
      "JPEG input contains more than one frame header",
      "JPEG frame header is truncated",
      "JPEG frame header has an invalid component table",
      "JPEG frame header has an invalid sample precision",
      "JPEG frame header has an invalid image size",
      "JPEG scan appears before its frame header",
      "JPEG scan header is truncated",
      "JPEG scan header has an invalid component table",
      "JPEG entropy data ends with a partial marker",
      "unsupported progressive JPEG frame; check in a baseline SOF0/SOF1 asset",
      "JPEG input has no supported SOF0/SOF1 frame",
      "unsupported JPEG frame marker 0xc3; check in a baseline SOF0/SOF1 asset",
      "JPEG input has no scan",
      "JPEG input has no EOI marker"};
  ctool_u8 sof1[sizeof(baseline)];
  ctool_u8 entropy[34];
  const ctool_u8 *positive_bytes[3];
  ctool_u32 positive_sizes[3];
  ctool_u8 rejection_bytes[JPEG_REJECTION_COUNT][64];
  ctool_u32 rejection_sizes[JPEG_REJECTION_COUNT];
  ctool_status_t rejection_statuses[JPEG_REJECTION_COUNT];
  ctool_u32 rejection_codes[JPEG_REJECTION_COUNT];
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *jpeg_output = (ctool_buffer_t *)0;
  ctool_buffer_t *binary_output = (ctool_buffer_t *)0;
  ctool_buffer_t *limited = (ctool_buffer_t *)0;
  ctool_source_t source;
  ctool_obj_request_t request;
  ctool_obj_result_t jpeg_result;
  ctool_obj_result_t binary_result;
  ctool_arena_mark_t mark;
  ctool_status_t status;
  ctool_u32 case_index;
  int ok = 1;

  if (!open_job(&adapter, &config, &job)) {
    return 1;
  }
  status = ctool_job_open_buffer(job, 64u, config.limits.output_bytes,
                                 &jpeg_output);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 64u, config.limits.output_bytes,
                                   &binary_output);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 1u, 5u, &limited);
  }
  if (status != CTOOL_OK) {
    if (binary_output != (ctool_buffer_t *)0) {
      ctool_buffer_close(binary_output);
    }
    if (jpeg_output != (ctool_buffer_t *)0) {
      ctool_buffer_close(jpeg_output);
    }
    ctool_job_close(job);
    return 1;
  }

  (void)memcpy(sof1, baseline, sizeof(baseline));
  sof1[3] = 0xc1u;
  (void)memcpy(entropy, baseline, 25u);
  entropy[25] = 0x12u;
  entropy[26] = 0xffu;
  entropy[27] = 0x00u;
  entropy[28] = 0x34u;
  entropy[29] = 0xffu;
  entropy[30] = 0xd0u;
  entropy[31] = 0x56u;
  (void)memcpy(entropy + 32u, baseline + 25u, 2u);
  positive_bytes[0] = baseline;
  positive_sizes[0] = (ctool_u32)sizeof(baseline);
  positive_bytes[1] = sof1;
  positive_sizes[1] = (ctool_u32)sizeof(sof1);
  positive_bytes[2] = entropy;
  positive_sizes[2] = (ctool_u32)sizeof(entropy);

  (void)memset(rejection_bytes, 0, sizeof(rejection_bytes));
  for (case_index = 0u; case_index < JPEG_REJECTION_COUNT; case_index++) {
    (void)memcpy(rejection_bytes[case_index], baseline, sizeof(baseline));
    rejection_sizes[case_index] = (ctool_u32)sizeof(baseline);
    rejection_statuses[case_index] = CTOOL_ERR_INPUT;
    rejection_codes[case_index] = CTOOL_OBJ_DIAG_INVALID_INPUT;
  }
  rejection_bytes[0][0] = 0x00u;
  (void)memcpy(rejection_bytes[1], baseline, 15u);
  rejection_bytes[1][15] = 0x01u;
  (void)memcpy(rejection_bytes[1] + 16u, baseline + 15u, 12u);
  rejection_sizes[1] = 28u;
  (void)memcpy(rejection_bytes[2], baseline, 15u);
  rejection_bytes[2][15] = 0xffu;
  rejection_bytes[2][16] = 0x00u;
  (void)memcpy(rejection_bytes[2] + 17u, baseline + 15u, 12u);
  rejection_sizes[2] = 29u;
  rejection_bytes[3][27] = 0x00u;
  rejection_sizes[3] = 28u;
  (void)memcpy(rejection_bytes[4], baseline, 15u);
  rejection_bytes[4][15] = 0xffu;
  rejection_bytes[4][16] = 0xd8u;
  (void)memcpy(rejection_bytes[4] + 17u, baseline + 15u, 12u);
  rejection_sizes[4] = 29u;
  rejection_bytes[5][0] = 0xffu;
  rejection_bytes[5][1] = 0xd8u;
  rejection_bytes[5][2] = 0xffu;
  rejection_bytes[5][3] = 0xdbu;
  rejection_bytes[5][4] = 0x00u;
  rejection_sizes[5] = 5u;
  rejection_bytes[6][0] = 0xffu;
  rejection_bytes[6][1] = 0xd8u;
  rejection_bytes[6][2] = 0xffu;
  rejection_bytes[6][3] = 0xdbu;
  rejection_bytes[6][4] = 0x00u;
  rejection_bytes[6][5] = 0x01u;
  rejection_sizes[6] = 6u;
  (void)memcpy(rejection_bytes[7], baseline, 15u);
  (void)memcpy(rejection_bytes[7] + 15u, baseline + 2u, 13u);
  (void)memcpy(rejection_bytes[7] + 28u, baseline + 15u, 12u);
  rejection_sizes[7] = 40u;
  rejection_bytes[8][5] = 0x07u;
  rejection_bytes[9][5] = 0x08u;
  rejection_bytes[10][6] = 0x00u;
  rejection_bytes[11][8] = 0x00u;
  (void)memcpy(rejection_bytes[12], baseline, 2u);
  (void)memcpy(rejection_bytes[12] + 2u, baseline + 15u, 12u);
  rejection_sizes[12] = 14u;
  rejection_bytes[13][18] = 0x05u;
  rejection_bytes[14][18] = 0x06u;
  (void)memcpy(rejection_bytes[15], baseline, 25u);
  rejection_bytes[15][25] = 0xffu;
  rejection_sizes[15] = 26u;
  rejection_bytes[16][3] = 0xc2u;
  rejection_statuses[16] = CTOOL_ERR_UNSUPPORTED;
  rejection_codes[16] = CTOOL_OBJ_DIAG_UNSUPPORTED;
  rejection_bytes[17][0] = 0xffu;
  rejection_bytes[17][1] = 0xd8u;
  rejection_bytes[17][2] = 0xffu;
  rejection_bytes[17][3] = 0xd9u;
  rejection_sizes[17] = 4u;
  rejection_bytes[18][3] = 0xc3u;
  rejection_statuses[18] = CTOOL_ERR_UNSUPPORTED;
  rejection_codes[18] = CTOOL_OBJ_DIAG_UNSUPPORTED;
  (void)memcpy(rejection_bytes[19], baseline, 15u);
  (void)memcpy(rejection_bytes[19] + 15u, baseline + 25u, 2u);
  rejection_sizes[19] = 17u;
  (void)memcpy(rejection_bytes[20], baseline, 25u);
  rejection_sizes[20] = 25u;

  source.path.text = ctool_string("/photo.jpg");
  (void)memset(&request, 0, sizeof(request));
  request.operation = CTOOL_OBJ_WRAP_JPEG;
  request.input = &source;
  request.as.wrap_binary.section_name = ctool_string(".data");
  request.as.wrap_binary.section_flags =
      CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE;
  request.as.wrap_binary.section_alignment = 1u;
  request.as.wrap_binary.start_symbol = ctool_string("photo_start");
  request.as.wrap_binary.end_symbol = ctool_string("photo_end");
  request.as.wrap_binary.size_symbol = ctool_string("photo_size");
  mark = ctool_arena_mark(ctool_job_arena(job));

  for (case_index = 0u; case_index < 3u; case_index++) {
    source.contents =
        ctool_bytes(positive_bytes[case_index], positive_sizes[case_index]);
    status = ctool_obj_transform(job, &request, jpeg_output, &jpeg_result);
    request.operation = CTOOL_OBJ_WRAP_BINARY;
    if (status == CTOOL_OK) {
      status = ctool_obj_transform(job, &request, binary_output,
                                   &binary_result);
    }
    request.operation = CTOOL_OBJ_WRAP_JPEG;
    if (status != CTOOL_OK || jpeg_result.bytes.size == 0u ||
        jpeg_result.bytes.size != binary_result.bytes.size ||
        memcmp(jpeg_result.bytes.data, binary_result.bytes.data,
               (size_t)jpeg_result.bytes.size) != 0 ||
        !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
      (void)fprintf(stderr,
                    "wrap-jpeg: positive case %u differs from binary wrap\n",
                    case_index);
      ok = 0;
    }
    ctool_buffer_clear(binary_output);
    ctool_buffer_clear(jpeg_output);
  }

  for (case_index = 0u; case_index < JPEG_REJECTION_COUNT; case_index++) {
    source.contents = ctool_bytes(rejection_bytes[case_index],
                                  rejection_sizes[case_index]);
    ok &= expect_failure_message(
        job, jpeg_output, &request, rejection_statuses[case_index],
        rejection_codes[case_index], rejection_messages[case_index],
        rejection_names[case_index]);
    if (!arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
      (void)fprintf(stderr, "%s: failure did not rewind the arena\n",
                    rejection_names[case_index]);
      ok = 0;
    }
    source.contents = ctool_bytes(baseline, (ctool_u32)sizeof(baseline));
    if (ctool_obj_transform(job, &request, jpeg_output, &jpeg_result) !=
            CTOOL_OK ||
        jpeg_result.bytes.size == 0u ||
        !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
      (void)fprintf(stderr, "%s: same-job recovery failed\n",
                    rejection_names[case_index]);
      ok = 0;
    }
    ctool_buffer_clear(jpeg_output);
  }

  source.contents = ctool_bytes(baseline, (ctool_u32)sizeof(baseline));
  ok &= expect_failure(job, limited, &request, CTOOL_ERR_LIMIT,
                       CTOOL_OBJ_DIAG_LIMIT, 0u,
                       "JPEG object output rollback");
  if (!arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
    (void)fprintf(stderr, "wrap-jpeg: failure did not rewind the arena\n");
    ok = 0;
  }
  if (ctool_obj_transform(job, &request, jpeg_output, &jpeg_result) !=
          CTOOL_OK ||
      jpeg_result.bytes.size == 0u ||
      !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
    (void)fprintf(stderr, "wrap-jpeg: output-limit recovery failed\n");
    ok = 0;
  }

  ctool_buffer_close(limited);
  ctool_buffer_close(binary_output);
  ctool_buffer_close(jpeg_output);
  ctool_job_close(job);
  return ok != 0 ? 0 : 1;
}

static int run_install_source(void) {
  static const char expected[] =
      "/* Auto-generated -- do not edit. */\n"
      "/* Lists all embedded CupidASM demos from demos/ directory */\n"
      "#include \"ramfs.h\"\n"
      "#include \"types.h\"\n"
      "#include \"../drivers/serial.h\"\n"
      "extern const char _binary_demos_alpha_asm_start[];\n"
      "extern const char _binary_demos_beta_test_asm_start[];\n"
      "extern const char _binary_demos_alpha_asm_end[];\n"
      "extern const char _binary_demos_beta_test_asm_end[];\n"
      "void install_demo_programs(void *fs_private);\n"
      "void install_demo_programs(void *fs_private) {\n"
      "    { uint32_t sz = (uint32_t)(_binary_demos_alpha_asm_end - _binary_demos_alpha_asm_start); ramfs_add_file(fs_private, \"demos/alpha.asm\", _binary_demos_alpha_asm_start, sz); serial_printf(\"[kernel] Installed /demos/alpha.asm (%u bytes)\\n\", sz); ramfs_add_file(fs_private, \"docs/demos/alpha.asm\", _binary_demos_alpha_asm_start, sz); serial_printf(\"[kernel] Installed /docs/demos/alpha.asm (%u bytes)\\n\", sz); }\n"
      "    { uint32_t sz = (uint32_t)(_binary_demos_beta_test_asm_end - _binary_demos_beta_test_asm_start); ramfs_add_file(fs_private, \"demos/beta_test.asm\", _binary_demos_beta_test_asm_start, sz); serial_printf(\"[kernel] Installed /demos/beta_test.asm (%u bytes)\\n\", sz); ramfs_add_file(fs_private, \"docs/demos/beta_test.asm\", _binary_demos_beta_test_asm_start, sz); serial_printf(\"[kernel] Installed /docs/demos/beta_test.asm (%u bytes)\\n\", sz); }\n"
      "}\n";
  static const ctool_u8 payload[] = {0u};
  ctool_string_t demos[2];
  ctool_string_t invalid[2];
  ctool_string_t stray_bin[1];
  ctool_string_t boundary_paths[513];
  ctool_string_t bin_collision[1];
  ctool_string_t browser_collision[1];
  ctool_string_t ctxt_collision[2];
  ctool_string_t doc_collision[1];
  ctool_string_t home_collision[1];
  ctool_string_t alias_doc[1];
  ctool_string_t alias_home[1];
  char boundary_storage[513][32];
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *output = (ctool_buffer_t *)0;
  ctool_buffer_t *limited = (ctool_buffer_t *)0;
  ctool_source_t source;
  ctool_obj_request_t request;
  ctool_obj_result_t first_result;
  ctool_obj_result_t second_result;
  ctool_status_t status;
  ctool_u32 index;
  int ok = 1;
  if (!open_job(&adapter, &config, &job)) {
    return 1;
  }
  status = ctool_job_open_buffer(job, 64u, config.limits.output_bytes,
                                 &output);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 8u, 64u, &limited);
  }
  if (status != CTOOL_OK) {
    if (output != (ctool_buffer_t *)0) {
      ctool_buffer_close(output);
    }
    ctool_job_close(job);
    return 1;
  }
  source.path.text = ctool_string("/demos/alpha.asm");
  source.contents = ctool_bytes(payload, (ctool_u32)sizeof(payload));
  demos[0] = ctool_string("demos/alpha.asm");
  demos[1] = ctool_string("demos/beta_test.asm");
  invalid[0] = ctool_string("demos/alpha.cc");
  invalid[1] = demos[1];
  stray_bin[0] = ctool_string("bin/alpha.cc");
  bin_collision[0] = ctool_string("bin/browser_alpha.cc");
  browser_collision[0] = ctool_string("bin/browser/alpha.cc");
  ctxt_collision[0] = ctool_string("cupidos-txt/a-b.CTXT");
  ctxt_collision[1] = ctool_string("cupidos-txt/a_b.CTXT");
  doc_collision[0] = ctool_string("a-b.bmp");
  home_collision[0] = ctool_string("a_b.bmp");
  alias_doc[0] = ctool_string("image.bmp");
  alias_home[0] = alias_doc[0];
  for (index = 0u; index < 513u; index++) {
    (void)snprintf(boundary_storage[index], sizeof(boundary_storage[index]),
                   "demos/demo_%u.asm", (unsigned int)index);
    boundary_paths[index] = ctool_string(boundary_storage[index]);
  }
  (void)memset(&request, 0, sizeof(request));
  request.operation = CTOOL_OBJ_GENERATE_INSTALL_SOURCE;
  request.input = &source;
  request.as.install_source.kind = CTOOL_OBJ_INSTALL_DEMOS;
  request.as.install_source.demo_paths = demos;
  request.as.install_source.demo_count = 2u;
  status = ctool_obj_transform(job, &request, output, &first_result);
  if (status != CTOOL_OK ||
      first_result.bytes.size != (ctool_u32)sizeof(expected) - 1u ||
      memcmp(first_result.bytes.data, expected, sizeof(expected) - 1u) != 0) {
    (void)fprintf(stderr, "install source: exact output mismatch\n");
    ok = 0;
  }
  ctool_buffer_clear(output);
  status = ctool_obj_transform(job, &request, output, &second_result);
  if (status != CTOOL_OK || second_result.bytes.size != first_result.bytes.size ||
      memcmp(second_result.bytes.data, expected, sizeof(expected) - 1u) != 0) {
    (void)fprintf(stderr, "install source: repeat output mismatch\n");
    ok = 0;
  }
  ctool_buffer_clear(output);

  request.as.install_source.demo_paths = invalid;
  ok &= expect_failure(job, output, &request, CTOOL_ERR_INPUT,
                       CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
                       "invalid install path");
  request.as.install_source.demo_paths = demos;
  request.as.install_source.bin_paths = stray_bin;
  request.as.install_source.bin_count = 1u;
  ok &= expect_failure(job, output, &request, CTOOL_ERR_INVALID_ARGUMENT,
                       CTOOL_OBJ_DIAG_INVALID_REQUEST, 0u,
                       "mixed install categories");
  request.as.install_source.bin_paths = (const ctool_string_t *)0;
  request.as.install_source.bin_count = 0u;
  request.as.install_source.demo_paths = boundary_paths;
  request.as.install_source.demo_count = 512u;
  status = ctool_obj_transform(job, &request, output, &second_result);
  if (status != CTOOL_OK || second_result.bytes.size == 0u) {
    (void)fprintf(stderr, "install inventory boundary: 512 paths failed\n");
    ok = 0;
  }
  ctool_buffer_clear(output);

  request.as.install_source.demo_count = 513u;
  ok &= expect_failure(job, output, &request, CTOOL_ERR_LIMIT,
                       CTOOL_OBJ_DIAG_LIMIT, 0u,
                       "install inventory limit");

  request.as.install_source.kind = CTOOL_OBJ_INSTALL_BIN;
  request.as.install_source.demo_paths = (const ctool_string_t *)0;
  request.as.install_source.demo_count = 0u;
  request.as.install_source.bin_paths = boundary_paths;
  request.as.install_source.bin_count = 513u;
  ok &= expect_failure(job, output, &request, CTOOL_ERR_LIMIT,
                       CTOOL_OBJ_DIAG_LIMIT, 0u,
                       "bin install inventory limit");

  request.as.install_source.kind = CTOOL_OBJ_INSTALL_DOCS;
  request.as.install_source.bin_paths = (const ctool_string_t *)0;
  request.as.install_source.bin_count = 0u;
  request.as.install_source.ctxt_paths = boundary_paths;
  request.as.install_source.ctxt_count = 513u;
  ok &= expect_failure(job, output, &request, CTOOL_ERR_LIMIT,
                       CTOOL_OBJ_DIAG_LIMIT, 0u,
                       "docs install inventory limit");

  request.as.install_source.kind = CTOOL_OBJ_INSTALL_BIN;
  request.as.install_source.ctxt_paths = (const ctool_string_t *)0;
  request.as.install_source.ctxt_count = 0u;
  request.as.install_source.bin_paths = boundary_paths;
  request.as.install_source.bin_count = 256u;
  request.as.install_source.header_paths = boundary_paths + 256u;
  request.as.install_source.header_count = 257u;
  ok &= expect_failure(job, output, &request, CTOOL_ERR_LIMIT,
                       CTOOL_OBJ_DIAG_LIMIT, 0u,
                       "combined install inventory limit");

  request.as.install_source.bin_count = 0xffffffffu;
  request.as.install_source.header_paths = (const ctool_string_t *)0;
  request.as.install_source.header_count = 0u;
  ok &= expect_failure(job, output, &request, CTOOL_ERR_LIMIT,
                       CTOOL_OBJ_DIAG_LIMIT, 0u,
                       "overflowing install inventory limit");

  (void)memset(&request.as.install_source, 0,
               sizeof(request.as.install_source));
  request.as.install_source.kind = CTOOL_OBJ_INSTALL_BIN;
  request.as.install_source.bin_paths = bin_collision;
  request.as.install_source.bin_count = 1u;
  request.as.install_source.browser_paths = browser_collision;
  request.as.install_source.browser_count = 1u;
  ok &= expect_failure(job, output, &request, CTOOL_ERR_INPUT,
                       CTOOL_OBJ_DIAG_SYMBOL_COLLISION, 0u,
                       "bin and browser symbol collision");

  (void)memset(&request.as.install_source, 0,
               sizeof(request.as.install_source));
  request.as.install_source.kind = CTOOL_OBJ_INSTALL_DOCS;
  request.as.install_source.ctxt_paths = ctxt_collision;
  request.as.install_source.ctxt_count = 2u;
  ok &= expect_failure(job, output, &request, CTOOL_ERR_INPUT,
                       CTOOL_OBJ_DIAG_SYMBOL_COLLISION, 0u,
                       "manual symbol collision");

  (void)memset(&request.as.install_source, 0,
               sizeof(request.as.install_source));
  request.as.install_source.kind = CTOOL_OBJ_INSTALL_DOCS;
  request.as.install_source.doc_asset_paths = doc_collision;
  request.as.install_source.doc_asset_count = 1u;
  request.as.install_source.home_asset_paths = home_collision;
  request.as.install_source.home_asset_count = 1u;
  ok &= expect_failure(job, output, &request, CTOOL_ERR_INPUT,
                       CTOOL_OBJ_DIAG_SYMBOL_COLLISION, 0u,
                       "documentation and home symbol collision");

  request.as.install_source.doc_asset_paths = alias_doc;
  request.as.install_source.home_asset_paths = alias_home;
  status = ctool_obj_transform(job, &request, output, &second_result);
  if (status != CTOOL_OK || second_result.bytes.size == 0u) {
    (void)fprintf(stderr, "install symbol alias: shared BMP path failed\n");
    ok = 0;
  }
  ctool_buffer_clear(output);

  (void)memset(&request.as.install_source, 0,
               sizeof(request.as.install_source));
  request.as.install_source.kind = CTOOL_OBJ_INSTALL_DEMOS;
  request.as.install_source.demo_paths = demos;
  request.as.install_source.demo_count = 2u;
  ok &= expect_failure(job, limited, &request, CTOOL_ERR_LIMIT,
                       CTOOL_OBJ_DIAG_LIMIT, 0u,
                       "install output rollback");
  if (ctool_obj_transform(job, &request, output, &second_result) != CTOOL_OK ||
      second_result.bytes.size != (ctool_u32)sizeof(expected) - 1u) {
    (void)fprintf(stderr, "install source: same-job recovery failed\n");
    ok = 0;
  }

  ctool_buffer_close(limited);
  ctool_buffer_close(output);
  ctool_job_close(job);
  return ok != 0 ? 0 : 1;
}

static int run_ksyms_source(void) {
  static const char symbol_text[] =
      "00002000 T second\n"
      "00001000 T first\n"
      "00001000 T duplicate\n"
      "         U unresolved\n"
      "00003000 D data_only\n"
      "00004000 t .Lprivate\n"
      "00005000 W weak_text\n";
  static const char expected[] =
      "/* Auto-generated by tools/hostbuild.py -- do not edit. */\n"
      "#include \"ksyms.h\"\n\n"
      "/* i386 words preserve the blob bytes with fewer initializers. */\n"
      "const unsigned int\n"
      "__attribute__((section(\".ksyms\"), used, aligned(4)))\n"
      "ksym_blob[] = {\n"
      "  0x4d59534bu, 0x00000003u, 0x00000028u, 0x0000003fu, 0x00001000u, 0x00000000u, 0x00002000u, 0x00000006u,\n"
      "  0x00005000u, 0x0000000du, 0x73726966u, 0x65730074u, 0x646e6f63u, 0x61657700u, 0x65745f6bu, 0x00007478u,\n"
      "};\n\n"
      "const unsigned int ksym_blob_size = 63u;\n";
  static const char malformed[] = "not-an-address T broken\n";
  static const char malformed_second[] =
      "00001000 T valid\n"
      "not-an-address T broken\n";
  static const char outside[] = "100000000 T too_wide\n";
  static const char omitted[] = "T missing_address\n";
  static const char no_text[] = "00002000 D data_only\n";
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *output = (ctool_buffer_t *)0;
  ctool_buffer_t *limited = (ctool_buffer_t *)0;
  ctool_source_t source;
  ctool_obj_request_t request;
  ctool_obj_result_t first_result;
  ctool_obj_result_t second_result;
  ctool_arena_mark_t mark;
  ctool_arena_mark_t before_fill;
  ctool_arena_mark_t full_mark;
  ctool_status_t status;
  void *arena_fill = (void *)0;
  int ok = 1;
  if (!open_job(&adapter, &config, &job)) {
    return 1;
  }
  status = ctool_job_open_buffer(job, 64u, config.limits.output_bytes,
                                 &output);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 8u, 32u, &limited);
  }
  if (status != CTOOL_OK) {
    if (output != (ctool_buffer_t *)0) {
      ctool_buffer_close(output);
    }
    ctool_job_close(job);
    return 1;
  }
  source.path.text = ctool_string("/kernel.symbols");
  source.contents = ctool_bytes(symbol_text,
                                (ctool_u32)sizeof(symbol_text) - 1u);
  (void)memset(&request, 0, sizeof(request));
  request.operation = CTOOL_OBJ_GENERATE_KSYMS_SOURCE;
  request.input = &source;
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_obj_transform(job, &request, output, &first_result);
  if (status != CTOOL_OK ||
      first_result.bytes.size != (ctool_u32)sizeof(expected) - 1u ||
      memcmp(first_result.bytes.data, expected, sizeof(expected) - 1u) != 0 ||
      !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
    (void)fprintf(stderr, "ksyms source: exact output mismatch\n");
    ok = 0;
  }
  ctool_buffer_clear(output);
  status = ctool_obj_transform(job, &request, output, &second_result);
  if (status != CTOOL_OK || second_result.bytes.size != first_result.bytes.size ||
      memcmp(second_result.bytes.data, expected, sizeof(expected) - 1u) != 0 ||
      !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
    (void)fprintf(stderr, "ksyms source: repeat output mismatch\n");
    ok = 0;
  }
  ctool_buffer_clear(output);

  source.contents = ctool_bytes(malformed,
                                (ctool_u32)sizeof(malformed) - 1u);
  ok &= expect_failure(job, output, &request, CTOOL_ERR_INPUT,
                       CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
                       "invalid ksyms address");
  source.contents = ctool_bytes(
      malformed_second, (ctool_u32)sizeof(malformed_second) - 1u);
  ok &= expect_failure(job, output, &request, CTOOL_ERR_INPUT,
                       CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
                       "second-line invalid ksyms address");
  if (ctool_job_diagnostic(job, ctool_job_diagnostic_count(job) - 1u)->line !=
      2u) {
    (void)fprintf(stderr, "ksyms source: diagnostic line mismatch\n");
    ok = 0;
  }
  source.contents = ctool_bytes(outside, (ctool_u32)sizeof(outside) - 1u);
  ok &= expect_failure(job, output, &request, CTOOL_ERR_INPUT,
                       CTOOL_OBJ_DIAG_ADDRESS_OVERFLOW, 0u,
                       "wide ksyms address");
  source.contents = ctool_bytes(omitted, (ctool_u32)sizeof(omitted) - 1u);
  ok &= expect_failure(job, output, &request, CTOOL_ERR_INPUT,
                       CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
                       "missing ksyms address");
  source.contents = ctool_bytes(no_text, (ctool_u32)sizeof(no_text) - 1u);
  ok &= expect_failure(job, output, &request, CTOOL_ERR_INPUT,
                       CTOOL_OBJ_DIAG_NO_LOAD, 0u,
                       "empty ksyms text set");
  source.contents = ctool_bytes(symbol_text,
                                (ctool_u32)sizeof(symbol_text) - 1u);
  ok &= expect_failure(job, limited, &request, CTOOL_ERR_LIMIT,
                       CTOOL_OBJ_DIAG_LIMIT, 0u,
                       "ksyms output rollback");
  if (ctool_obj_transform(job, &request, output, &second_result) != CTOOL_OK ||
      second_result.bytes.size != (ctool_u32)sizeof(expected) - 1u) {
    (void)fprintf(stderr, "ksyms source: same-job recovery failed\n");
    ok = 0;
  }
  ctool_buffer_clear(output);

  before_fill = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_arena_alloc(ctool_job_arena(job), config.limits.arena_bytes,
                             1u, &arena_fill);
  full_mark = ctool_arena_mark(ctool_job_arena(job));
  if (status != CTOOL_OK || arena_fill == (void *)0) {
    (void)fprintf(stderr, "ksyms arena limit: setup failed\n");
    ok = 0;
  } else {
    ok &= expect_failure(job, output, &request, CTOOL_ERR_LIMIT,
                         CTOOL_OBJ_DIAG_LIMIT, 0u,
                         "ksyms symbol inventory arena limit");
    if (!arena_marks_equal(full_mark,
                           ctool_arena_mark(ctool_job_arena(job)))) {
      (void)fprintf(stderr, "ksyms arena limit: failure did not rewind\n");
      ok = 0;
    }
    if (ctool_arena_rewind(ctool_job_arena(job), before_fill) != CTOOL_OK ||
        ctool_obj_transform(job, &request, output, &second_result) !=
            CTOOL_OK ||
        !arena_marks_equal(before_fill,
                           ctool_arena_mark(ctool_job_arena(job)))) {
      (void)fprintf(stderr, "ksyms arena limit: same-job recovery failed\n");
      ok = 0;
    }
  }

  ctool_buffer_close(limited);
  ctool_buffer_close(output);
  ctool_job_close(job);
  return ok != 0 ? 0 : 1;
}

static int disk_template_small_layout(const ctool_obj_result_t *result,
                                      const ctool_u8 *boot,
                                      ctool_bytes_t kernel) {
  const ctool_u8 *bytes = result->bytes.data;
  const ctool_u8 *bpb;
  if (bytes == (const ctool_u8 *)0 || result->bytes.size != 38400u ||
      result->base_address != 0u || result->end_address != 0u) {
    (void)fprintf(stderr, "disk-template: small result shape differs\n");
    return 0;
  }
  if (memcmp(bytes, boot, 446u) != 0 || bytes[446u] != 0x80u ||
      bytes[447u] != 0xfeu || bytes[448u] != 0xffu ||
      bytes[449u] != 0xffu || bytes[450u] != 0x06u ||
      bytes[451u] != 0xfeu || bytes[452u] != 0xffu ||
      bytes[453u] != 0xffu || read_le32(bytes, 454u) != 8u ||
      read_le32(bytes, 458u) != 4200u ||
      !byte_range_is_zero(bytes, 462u, 510u) || bytes[510u] != 0x55u ||
      bytes[511u] != 0xaau) {
    (void)fprintf(stderr, "disk-template: small MBR differs\n");
    return 0;
  }
  if (memcmp(bytes + 512u, boot + 512u, 2048u) != 0 ||
      memcmp(bytes + 2560u, kernel.data, (size_t)kernel.size) != 0 ||
      !byte_range_is_zero(bytes, 2560u + kernel.size, 4096u)) {
    (void)fprintf(stderr, "disk-template: boot or kernel lane differs\n");
    return 0;
  }

  bpb = bytes + 4096u;
  if (bpb[0] != 0xebu || bpb[1] != 0x3cu || bpb[2] != 0x90u ||
      memcmp(bpb + 3u, "CUPIDOS ", 8u) != 0 ||
      read_le16(bpb, 11u) != 512u || bpb[13u] != 1u ||
      read_le16(bpb, 14u) != 1u || bpb[16u] != 2u ||
      read_le16(bpb, 17u) != 512u || read_le16(bpb, 19u) != 4200u ||
      bpb[21u] != 0xf8u || read_le16(bpb, 22u) != 17u ||
      read_le16(bpb, 24u) != 63u || read_le16(bpb, 26u) != 255u ||
      read_le32(bpb, 28u) != 8u || read_le32(bpb, 32u) != 0u ||
      bpb[36u] != 0x80u || bpb[38u] != 0x29u ||
      read_le32(bpb, 39u) != 0x0c001d05u ||
      memcmp(bpb + 43u, "CUPIDOS    ", 11u) != 0 ||
      memcmp(bpb + 54u, "FAT16   ", 8u) != 0 ||
      !byte_range_is_zero(bpb, 62u, 510u) || bpb[510u] != 0x55u ||
      bpb[511u] != 0xaau) {
    (void)fprintf(stderr, "disk-template: small FAT16 BPB differs\n");
    return 0;
  }
  if (memcmp(bytes + 4608u, "\xf8\xff\xff\xff", 4u) != 0 ||
      !byte_range_is_zero(bytes, 4612u, 13312u) ||
      memcmp(bytes + 13312u, "\xf8\xff\xff\xff", 4u) != 0 ||
      !byte_range_is_zero(bytes, 13316u, 22016u) ||
      !byte_range_is_zero(bytes, 22016u, 38400u)) {
    (void)fprintf(stderr, "disk-template: small FAT/root layout differs\n");
    return 0;
  }
  return 1;
}

static int disk_template_active_layout(const ctool_obj_result_t *result,
                                       ctool_u32 kernel_size) {
  const ctool_u8 *bytes = result->bytes.data;
  const ctool_u8 *bpb;
  if (bytes == (const ctool_u8 *)0 || result->bytes.size != 10697216u ||
      result->base_address != 0u || result->end_address != 0u) {
    (void)fprintf(stderr, "disk-template: active result shape differs\n");
    return 0;
  }
  if (read_le32(bytes, 454u) != 20480u ||
      read_le32(bytes, 458u) != 389120u ||
      !byte_range_is_zero(bytes, 2560u + kernel_size, 10485760u)) {
    (void)fprintf(stderr, "disk-template: active MBR/kernel lane differs\n");
    return 0;
  }
  bpb = bytes + 10485760u;
  if (read_le16(bpb, 11u) != 512u || bpb[13u] != 8u ||
      read_le16(bpb, 14u) != 1u || bpb[16u] != 2u ||
      read_le16(bpb, 17u) != 512u || read_le16(bpb, 19u) != 0u ||
      read_le16(bpb, 22u) != 190u || read_le32(bpb, 28u) != 20480u ||
      read_le32(bpb, 32u) != 389120u || bpb[510u] != 0x55u ||
      bpb[511u] != 0xaau) {
    (void)fprintf(stderr, "disk-template: active FAT16 geometry differs\n");
    return 0;
  }
  if (memcmp(bytes + 10486272u, "\xf8\xff\xff\xff", 4u) != 0 ||
      !byte_range_is_zero(bytes, 10486276u, 10583552u) ||
      memcmp(bytes + 10583552u, "\xf8\xff\xff\xff", 4u) != 0 ||
      !byte_range_is_zero(bytes, 10583556u, 10680832u) ||
      !byte_range_is_zero(bytes, 10680832u, 10697216u)) {
    (void)fprintf(stderr, "disk-template: active FAT/root layout differs\n");
    return 0;
  }
  return 1;
}

static int run_disk_template(void) {
  enum {
    DISK_SECTOR_BYTES = 512,
    DISK_BOOT_SECTORS = 5,
    SMALL_IMAGE_SECTORS = 4208,
    SMALL_FAT_START_LBA = 8,
    ACTIVE_IMAGE_SECTORS = 409600,
    ACTIVE_FAT_START_LBA = 20480
  };
  static const ctool_u8 kernel_bytes[] = {0x43u, 0x55u, 0x50u, 0x49u,
                                         0x44u, 0x2du, 0x4fu, 0x53u};
  ctool_u8 boot[DISK_BOOT_SECTORS * DISK_SECTOR_BYTES];
  ctool_u8 overlapping_kernel[3u * DISK_SECTOR_BYTES + 1u];
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *first = (ctool_buffer_t *)0;
  ctool_buffer_t *repeat = (ctool_buffer_t *)0;
  ctool_buffer_t *active = (ctool_buffer_t *)0;
  ctool_buffer_t *limited = (ctool_buffer_t *)0;
  ctool_source_t boot_source;
  ctool_source_t kernel_source;
  ctool_source_t overlap_source;
  const ctool_diagnostic_t *diagnostic;
  ctool_obj_request_t request;
  ctool_obj_disk_template_request_t *disk_request;
  ctool_obj_result_t first_result;
  ctool_obj_result_t repeat_result;
  ctool_obj_result_t active_result;
  ctool_obj_result_t recovery_result;
  ctool_arena_mark_t mark;
  ctool_status_t status;
  ctool_u32 index;
  int have_first = 0;
  int have_repeat = 0;
  int ok = 1;

  for (index = 0u; index < (ctool_u32)sizeof(boot); index++) {
    boot[index] = (ctool_u8)((index * 37u + 11u) & 0xffu);
  }
  (void)memset(overlapping_kernel, 0x6bu, sizeof(overlapping_kernel));
  if (!open_job(&adapter, &config, &job)) {
    return 1;
  }
  status = ctool_job_open_buffer(job, 64u, config.limits.output_bytes, &first);
  if (status == CTOOL_OK) {
    status =
        ctool_job_open_buffer(job, 64u, config.limits.output_bytes, &repeat);
  }
  if (status == CTOOL_OK) {
    status =
        ctool_job_open_buffer(job, 64u, config.limits.output_bytes, &active);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 64u, 38399u, &limited);
  }
  if (status != CTOOL_OK) {
    if (limited != (ctool_buffer_t *)0) {
      ctool_buffer_close(limited);
    }
    if (active != (ctool_buffer_t *)0) {
      ctool_buffer_close(active);
    }
    if (repeat != (ctool_buffer_t *)0) {
      ctool_buffer_close(repeat);
    }
    if (first != (ctool_buffer_t *)0) {
      ctool_buffer_close(first);
    }
    ctool_job_close(job);
    return 1;
  }

  boot_source.path.text = ctool_string("/boot.bin");
  boot_source.contents = ctool_bytes(boot, (ctool_u32)sizeof(boot));
  kernel_source.path.text = ctool_string("/kernel.bin");
  kernel_source.contents =
      ctool_bytes(kernel_bytes, (ctool_u32)sizeof(kernel_bytes));
  overlap_source.path.text = ctool_string("/overlap-kernel.bin");
  overlap_source.contents = ctool_bytes(
      overlapping_kernel, (ctool_u32)sizeof(overlapping_kernel));
  (void)memset(&request, 0, sizeof(request));
  request.operation = CTOOL_OBJ_BUILD_DISK_TEMPLATE;
  request.input = &boot_source;
  disk_request = &request.as.disk_template;
  disk_request->kernel = &kernel_source;
  disk_request->image_sectors = SMALL_IMAGE_SECTORS;
  disk_request->fat_start_lba = SMALL_FAT_START_LBA;

  (void)memset(&first_result, 0, sizeof(first_result));
  (void)memset(&repeat_result, 0, sizeof(repeat_result));
  (void)memset(&active_result, 0, sizeof(active_result));
  (void)memset(&recovery_result, 0, sizeof(recovery_result));
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_obj_transform(job, &request, first, &first_result);
  if (status != CTOOL_OK ||
      !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) ||
      first_result.bytes.data != ctool_buffer_view(first).data ||
      first_result.bytes.size != ctool_buffer_view(first).size ||
      !disk_template_small_layout(&first_result, boot,
                                  kernel_source.contents)) {
    (void)fprintf(stderr, "disk-template: small transform failed\n");
    ok = 0;
  } else {
    have_first = 1;
  }

  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_obj_transform(job, &request, repeat, &repeat_result);
  if (status != CTOOL_OK ||
      !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) ||
      have_first == 0 ||
      first_result.bytes.size != repeat_result.bytes.size ||
      memcmp(first_result.bytes.data, repeat_result.bytes.data,
             (size_t)first_result.bytes.size) != 0) {
    (void)fprintf(stderr, "disk-template: deterministic repeat differs\n");
    ok = 0;
  } else {
    have_repeat = 1;
  }

  disk_request->image_sectors = ACTIVE_IMAGE_SECTORS;
  disk_request->fat_start_lba = ACTIVE_FAT_START_LBA;
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_obj_transform(job, &request, active, &active_result);
  if (status != CTOOL_OK ||
      !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) ||
      active_result.bytes.data != ctool_buffer_view(active).data ||
      active_result.bytes.size != ctool_buffer_view(active).size ||
      !disk_template_active_layout(&active_result,
                                   kernel_source.contents.size)) {
    (void)fprintf(stderr, "disk-template: active transform failed\n");
    ok = 0;
  }

  ctool_buffer_clear(active);
  disk_request->image_sectors = 8304u;
  disk_request->fat_start_lba = 16u;
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_obj_transform(job, &request, active, &active_result);
  if (status != CTOOL_OK ||
      !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) ||
      active_result.bytes.data != ctool_buffer_view(active).data ||
      active_result.bytes.size != 42496u ||
      active_result.bytes.data[16u * DISK_SECTOR_BYTES + 13u] != 2u ||
      read_le16(active_result.bytes.data + 16u * DISK_SECTOR_BYTES, 22u) !=
          17u) {
    (void)fprintf(stderr,
                  "disk-template: FAT-size cycle recovery differs\n");
    ok = 0;
  }

  ctool_buffer_clear(active);
  overlap_source.contents = ctool_bytes(
      overlapping_kernel, (ctool_u32)sizeof(overlapping_kernel) - 1u);
  disk_request->image_sectors = SMALL_IMAGE_SECTORS;
  disk_request->fat_start_lba = SMALL_FAT_START_LBA;
  disk_request->kernel = &overlap_source;
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_obj_transform(job, &request, active, &active_result);
  if (status != CTOOL_OK ||
      !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) ||
      !disk_template_small_layout(&active_result, boot,
                                  overlap_source.contents)) {
    (void)fprintf(stderr,
                  "disk-template: exact kernel boundary failed\n");
    ok = 0;
  }
  overlap_source.contents = ctool_bytes(
      overlapping_kernel, (ctool_u32)sizeof(overlapping_kernel));
  disk_request->kernel = &kernel_source;

  ctool_buffer_clear(first);
  disk_request->image_sectors = SMALL_IMAGE_SECTORS;
  disk_request->fat_start_lba = SMALL_FAT_START_LBA;
  boot_source.contents =
      ctool_bytes(boot, (ctool_u32)sizeof(boot) - 1u);
  ok &= expect_rewound_failure(job, first, &request, CTOOL_ERR_INPUT,
                               CTOOL_OBJ_DIAG_INVALID_INPUT,
                               "disk-template short boot");
  boot_source.contents = ctool_bytes(boot, (ctool_u32)sizeof(boot));

  disk_request->image_sectors = 100u;
  ok &= expect_rewound_failure(job, first, &request, CTOOL_ERR_INPUT,
                               CTOOL_OBJ_DIAG_INVALID_INPUT,
                               "disk-template bad geometry");
  disk_request->image_sectors = SMALL_IMAGE_SECTORS;

  disk_request->kernel = &overlap_source;
  ok &= expect_rewound_failure(job, first, &request, CTOOL_ERR_INPUT,
                               CTOOL_OBJ_DIAG_OVERLAP,
                               "disk-template kernel overlap");
  diagnostic = ctool_job_diagnostic(
      job, ctool_job_diagnostic_count(job) - 1u);
  if (diagnostic == (const ctool_diagnostic_t *)0 ||
      !string_equal(diagnostic->path, "/overlap-kernel.bin")) {
    (void)fprintf(stderr,
                  "disk-template: kernel overlap path differs\n");
    ok = 0;
  }
  disk_request->kernel = &kernel_source;

  ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_LIMIT,
                               CTOOL_OBJ_DIAG_LIMIT,
                               "disk-template output limit");

  disk_request->image_sectors = 8392808u;
  disk_request->fat_start_lba = 8388608u;
  ok &= expect_rewound_failure(job, first, &request, CTOOL_ERR_OVERFLOW,
                               CTOOL_OBJ_DIAG_LIMIT,
                               "disk-template i386 size overflow");
  disk_request->image_sectors = SMALL_IMAGE_SECTORS;
  disk_request->fat_start_lba = SMALL_FAT_START_LBA;

  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_obj_transform(job, &request, first, &recovery_result);
  if (status != CTOOL_OK ||
      !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) ||
      !disk_template_small_layout(&recovery_result, boot,
                                  kernel_source.contents) ||
      have_repeat == 0 ||
      recovery_result.bytes.size != repeat_result.bytes.size ||
      memcmp(recovery_result.bytes.data, repeat_result.bytes.data,
             (size_t)recovery_result.bytes.size) != 0) {
    (void)fprintf(stderr, "disk-template: same-job recovery failed\n");
    ok = 0;
  }

  ctool_buffer_close(limited);
  ctool_buffer_close(active);
  ctool_buffer_close(repeat);
  ctool_buffer_close(first);
  ctool_job_close(job);
  return ok != 0 ? 0 : 1;
}

static int iso_fixture_layout(const ctool_obj_result_t *result) {
  const ctool_u32 block = 2048u;
  const ctool_u8 *bytes = result->bytes.data;
  const ctool_u32 descriptor = 16u * block;
  const ctool_u32 root = 20u * block;
  const ctool_u32 sub = 21u * block;
  if (bytes == (const ctool_u8 *)0 || result->bytes.size != 25u * block ||
      result->base_address != 0u || result->end_address != 0u) {
    (void)fprintf(stderr, "iso-fixture: result shape differs\n");
    return 0;
  }
  if (memcmp(bytes + descriptor, "\x01" "CD001\x01", 7u) != 0 ||
      memcmp(bytes + descriptor + 40u, "CUPID_OS_TEST", 13u) != 0 ||
      read_le32(bytes, descriptor + 80u) != 25u ||
      read_be32(bytes, descriptor + 84u) != 25u ||
      read_le32(bytes, descriptor + 132u) != 22u ||
      read_be32(bytes, descriptor + 136u) != 22u ||
      read_le32(bytes, descriptor + 140u) != 18u ||
      read_be32(bytes, descriptor + 148u) != 19u ||
      read_le32(bytes, descriptor + 158u) != 20u ||
      read_be32(bytes, descriptor + 162u) != 20u ||
      read_le32(bytes, descriptor + 166u) != block ||
      read_be32(bytes, descriptor + 170u) != block ||
      memcmp(bytes + 17u * block, "\xff" "CD001\x01", 7u) != 0) {
    (void)fprintf(stderr, "iso-fixture: descriptor layout differs\n");
    return 0;
  }
  if (bytes[18u * block] != 1u ||
      read_le32(bytes, 18u * block + 2u) != 20u ||
      read_le16(bytes, 18u * block + 6u) != 1u ||
      bytes[18u * block + 10u] != 3u ||
      read_le32(bytes, 18u * block + 12u) != 21u ||
      read_le16(bytes, 18u * block + 16u) != 1u ||
      memcmp(bytes + 18u * block + 18u, "SUB", 3u) != 0) {
    (void)fprintf(stderr, "iso-fixture: little path table differs\n");
    return 0;
  }
  if (bytes[19u * block] != 1u ||
      read_be32(bytes, 19u * block + 2u) != 20u ||
      read_be16(bytes, 19u * block + 6u) != 1u ||
      bytes[19u * block + 10u] != 3u ||
      read_be32(bytes, 19u * block + 12u) != 21u ||
      read_be16(bytes, 19u * block + 16u) != 1u ||
      memcmp(bytes + 19u * block + 18u, "SUB", 3u) != 0) {
    (void)fprintf(stderr, "iso-fixture: big path table differs\n");
    return 0;
  }
  if (memcmp(bytes + root + 34u, "SP\x07\x01\xbe\xef\x00", 7u) != 0 ||
      memcmp(bytes + root + 103u, "CE\x1c\x01", 4u) != 0 ||
      read_le32(bytes, root + 107u) != 22u ||
      read_le32(bytes, root + 123u) != 237u ||
      memcmp(bytes + root + 228u + 33u, "EMPTY.;1", 8u) != 0 ||
      memcmp(bytes + root + 342u + 33u, "HELLO.TXT;1", 11u) != 0 ||
      memcmp(bytes + root + 462u + 33u, "SUB", 3u) != 0 ||
      memcmp(bytes + sub + 192u + 33u, "NESTED.BIN;1", 12u) != 0) {
    (void)fprintf(stderr, "iso-fixture: directory records differ\n");
    return 0;
  }
  if (memcmp(bytes + 22u * block, "ER\xed\x01", 4u) != 0 ||
      memcmp(bytes + 23u * block, "hello", 5u) != 0 ||
      memcmp(bytes + 24u * block, "nest", 4u) != 0) {
    (void)fprintf(stderr, "iso-fixture: continuation or files differ\n");
    return 0;
  }
  return 1;
}

static int run_iso_fixture(void) {
  static const char manifest_bytes[] =
      "empty\nhello.txt\nsub\nsub/nested.bin\n";
  static const char reordered_manifest_bytes[] =
      "sub/nested.bin\r\nsub\r\nhello.txt\r\nempty\r\n";
  static const char orphan_manifest[] = "lost/file.bin\n";
  static const char collision_manifest[] = "A\na\n";
  static const char file_parent_manifest[] = "parent\nparent/child\n";
  static const char wide_manifest[] = "wide.bin\n";
  static const char missing_typed_manifest[] =
      "empty\nhello.txt\nsub\n";
  static const char extra_manifest[] =
      "empty\nhello.txt\nsub\nsub/nested.bin\nextra\n";
  static const char long_component[] =
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  static const ctool_u8 hello_bytes[] = {'h', 'e', 'l', 'l', 'o'};
  static const ctool_u8 nested_bytes[] = {'n', 'e', 's', 't'};
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *first = (ctool_buffer_t *)0;
  ctool_buffer_t *second = (ctool_buffer_t *)0;
  ctool_buffer_t *limited = (ctool_buffer_t *)0;
  ctool_source_t manifest;
  ctool_source_t hello;
  ctool_source_t nested;
  ctool_source_t empty;
  ctool_source_t invalid_source;
  ctool_source_t wide_source;
  ctool_obj_iso_fixture_entry_t entries[4];
  ctool_obj_iso_fixture_entry_t reordered[4];
  ctool_obj_iso_fixture_entry_t orphan;
  ctool_obj_iso_fixture_entry_t collisions[2];
  ctool_obj_iso_fixture_entry_t file_parent[2];
  ctool_obj_iso_fixture_entry_t wide_entry;
  ctool_obj_request_t request;
  ctool_obj_result_t first_result;
  ctool_obj_result_t second_result;
  ctool_arena_mark_t mark;
  ctool_arena_mark_t before_fill;
  ctool_arena_mark_t full_mark;
  ctool_status_t status;
  void *arena_fill = (void *)0;
  int ok = 1;

  if (!open_job(&adapter, &config, &job)) {
    return 1;
  }
  status = ctool_job_open_buffer(job, 64u, config.limits.output_bytes, &first);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 64u, config.limits.output_bytes,
                                   &second);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 64u, 25u * 2048u - 1u, &limited);
  }
  if (status != CTOOL_OK) {
    if (limited != (ctool_buffer_t *)0) {
      ctool_buffer_close(limited);
    }
    if (second != (ctool_buffer_t *)0) {
      ctool_buffer_close(second);
    }
    if (first != (ctool_buffer_t *)0) {
      ctool_buffer_close(first);
    }
    ctool_job_close(job);
    return 1;
  }

  manifest.path.text = ctool_string("/fixtures.manifest");
  manifest.contents = ctool_bytes(manifest_bytes,
                                  (ctool_u32)sizeof(manifest_bytes) - 1u);
  hello.path.text = ctool_string("/hello.txt");
  hello.contents = ctool_bytes(hello_bytes, (ctool_u32)sizeof(hello_bytes));
  nested.path.text = ctool_string("/nested.bin");
  nested.contents =
      ctool_bytes(nested_bytes, (ctool_u32)sizeof(nested_bytes));
  empty.path.text = ctool_string("/empty");
  empty.contents = ctool_bytes((const void *)0, 0u);

  entries[0].path = ctool_string("sub/nested.bin");
  entries[0].kind = CTOOL_OBJ_ISO_FIXTURE_FILE;
  entries[0].source = &nested;
  entries[1].path = ctool_string("empty");
  entries[1].kind = CTOOL_OBJ_ISO_FIXTURE_FILE;
  entries[1].source = &empty;
  entries[2].path = ctool_string("sub");
  entries[2].kind = CTOOL_OBJ_ISO_FIXTURE_DIRECTORY;
  entries[2].source = (const ctool_source_t *)0;
  entries[3].path = ctool_string("hello.txt");
  entries[3].kind = CTOOL_OBJ_ISO_FIXTURE_FILE;
  entries[3].source = &hello;
  reordered[0] = entries[3];
  reordered[1] = entries[2];
  reordered[2] = entries[1];
  reordered[3] = entries[0];

  (void)memset(&request, 0, sizeof(request));
  request.operation = CTOOL_OBJ_BUILD_ISO_FIXTURE;
  request.input = &manifest;
  request.as.iso_fixture.entries = entries;
  request.as.iso_fixture.entry_count = 4u;
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_obj_transform(job, &request, first, &first_result);
  if (status != CTOOL_OK ||
      !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) ||
      !iso_fixture_layout(&first_result)) {
    (void)fprintf(stderr, "iso-fixture: first transform failed\n");
    ok = 0;
  }
  request.as.iso_fixture.entries = reordered;
  manifest.contents = ctool_bytes(
      reordered_manifest_bytes,
      (ctool_u32)sizeof(reordered_manifest_bytes) - 1u);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_obj_transform(job, &request, second, &second_result);
  if (status != CTOOL_OK ||
      !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) ||
      first_result.bytes.size != second_result.bytes.size ||
      memcmp(first_result.bytes.data, second_result.bytes.data,
             (size_t)first_result.bytes.size) != 0) {
    (void)fprintf(stderr, "iso-fixture: reordered inventory differs\n");
    ok = 0;
  }

  {
    char boundary_paths[512][5];
    ctool_u8 boundary_manifest[512u * 5u];
    ctool_obj_iso_fixture_entry_t boundary_entries[512];
    ctool_u32 index;
    for (index = 0u; index < 512u; index++) {
      boundary_paths[index][0] = 'd';
      boundary_paths[index][1] =
          (char)('0' + (char)((index / 100u) % 10u));
      boundary_paths[index][2] =
          (char)('0' + (char)((index / 10u) % 10u));
      boundary_paths[index][3] = (char)('0' + (char)(index % 10u));
      boundary_paths[index][4] = '\0';
      (void)memcpy(boundary_manifest + index * 5u,
                   boundary_paths[index], 4u);
      boundary_manifest[index * 5u + 4u] = (ctool_u8)'\n';
      boundary_entries[index].path = ctool_string(boundary_paths[index]);
      boundary_entries[index].kind = CTOOL_OBJ_ISO_FIXTURE_DIRECTORY;
      boundary_entries[index].source = (const ctool_source_t *)0;
    }
    ctool_buffer_clear(second);
    manifest.contents = ctool_bytes(boundary_manifest,
                                    (ctool_u32)sizeof(boundary_manifest));
    request.as.iso_fixture.entries = boundary_entries;
    request.as.iso_fixture.entry_count = 512u;
    mark = ctool_arena_mark(ctool_job_arena(job));
    status = ctool_obj_transform(job, &request, second, &second_result);
    if (status != CTOOL_OK ||
        !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) ||
        second_result.bytes.size % 2048u != 0u) {
      (void)fprintf(stderr,
                    "iso-fixture: complete entry boundary failed\n");
      ok = 0;
    }
  }

  ctool_buffer_clear(second);
  request.as.iso_fixture.entries = entries;
  request.as.iso_fixture.entry_count = 4u;
  manifest.contents = ctool_bytes(manifest_bytes,
                                  (ctool_u32)sizeof(manifest_bytes) - 1u);
  ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_LIMIT,
                               CTOOL_OBJ_DIAG_LIMIT,
                               "iso-fixture output limit");

  manifest.contents = ctool_bytes(orphan_manifest,
                                  (ctool_u32)sizeof(orphan_manifest) - 1u);
  orphan.path = ctool_string("lost/file.bin");
  orphan.kind = CTOOL_OBJ_ISO_FIXTURE_FILE;
  orphan.source = &nested;
  request.as.iso_fixture.entries = &orphan;
  request.as.iso_fixture.entry_count = 1u;
  ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_INPUT,
                               CTOOL_OBJ_DIAG_INVALID_INPUT,
                               "iso-fixture missing parent");

  manifest.contents =
      ctool_bytes(collision_manifest,
                  (ctool_u32)sizeof(collision_manifest) - 1u);
  collisions[0].path = ctool_string("A");
  collisions[0].kind = CTOOL_OBJ_ISO_FIXTURE_FILE;
  collisions[0].source = &empty;
  collisions[1].path = ctool_string("a");
  collisions[1].kind = CTOOL_OBJ_ISO_FIXTURE_FILE;
  collisions[1].source = &empty;
  request.as.iso_fixture.entries = collisions;
  request.as.iso_fixture.entry_count = 2u;
  ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_INPUT,
                               CTOOL_OBJ_DIAG_SYMBOL_COLLISION,
                               "iso-fixture case collision");

  request.as.iso_fixture.entries = (const ctool_obj_iso_fixture_entry_t *)0;
  request.as.iso_fixture.entry_count = 1u;
  ok &= expect_rewound_failure(job, limited, &request,
                               CTOOL_ERR_INVALID_ARGUMENT,
                               CTOOL_OBJ_DIAG_INVALID_REQUEST,
                               "iso-fixture missing inventory");
  request.as.iso_fixture.entries = entries;
  request.as.iso_fixture.entry_count = 0u;
  ok &= expect_rewound_failure(job, limited, &request,
                               CTOOL_ERR_INVALID_ARGUMENT,
                               CTOOL_OBJ_DIAG_INVALID_REQUEST,
                               "iso-fixture empty inventory");
  request.as.iso_fixture.entry_count = 513u;
  ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_LIMIT,
                               CTOOL_OBJ_DIAG_LIMIT,
                               "iso-fixture entry limit");

  request.as.iso_fixture.entry_count = 4u;
  entries[0].kind = (ctool_obj_iso_fixture_kind_t)0;
  ok &= expect_rewound_failure(job, limited, &request,
                               CTOOL_ERR_INVALID_ARGUMENT,
                               CTOOL_OBJ_DIAG_INVALID_REQUEST,
                               "iso-fixture invalid entry kind");
  entries[0].kind = CTOOL_OBJ_ISO_FIXTURE_FILE;
  entries[0].source = (const ctool_source_t *)0;
  ok &= expect_rewound_failure(job, limited, &request,
                               CTOOL_ERR_INVALID_ARGUMENT,
                               CTOOL_OBJ_DIAG_INVALID_REQUEST,
                               "iso-fixture file source required");
  entries[0].source = &nested;
  entries[2].source = &empty;
  ok &= expect_rewound_failure(job, limited, &request,
                               CTOOL_ERR_INVALID_ARGUMENT,
                               CTOOL_OBJ_DIAG_INVALID_REQUEST,
                               "iso-fixture directory source forbidden");
  entries[2].source = (const ctool_source_t *)0;
  invalid_source.path.text = ctool_string("/invalid.bin");
  invalid_source.contents = ctool_bytes((const void *)0, 1u);
  entries[0].source = &invalid_source;
  ok &= expect_rewound_failure(job, limited, &request,
                               CTOOL_ERR_INVALID_ARGUMENT,
                               CTOOL_OBJ_DIAG_INVALID_INPUT,
                               "iso-fixture invalid file bytes");
  entries[0].source = &nested;

  entries[2].path = ctool_string("/absolute");
  ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_INPUT,
                               CTOOL_OBJ_DIAG_INVALID_INPUT,
                               "iso-fixture absolute path");
  entries[2].path = ctool_string("double//slash");
  ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_INPUT,
                               CTOOL_OBJ_DIAG_INVALID_INPUT,
                               "iso-fixture empty component");
  entries[2].path = ctool_string("back\\slash");
  ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_INPUT,
                               CTOOL_OBJ_DIAG_INVALID_INPUT,
                               "iso-fixture native separator");
  entries[2].path = ctool_string("parent/../escape");
  ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_INPUT,
                               CTOOL_OBJ_DIAG_INVALID_INPUT,
                               "iso-fixture traversal path");
  entries[2].path = ctool_string("white space");
  ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_INPUT,
                               CTOOL_OBJ_DIAG_INVALID_INPUT,
                               "iso-fixture whitespace path");
  entries[2].path = ctool_string(long_component);
  ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_INPUT,
                               CTOOL_OBJ_DIAG_INVALID_INPUT,
                               "iso-fixture component limit");
  entries[2].path = ctool_string("a/b/c/d/e/f/g/h");
  ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_INPUT,
                               CTOOL_OBJ_DIAG_INVALID_INPUT,
                               "iso-fixture hierarchy limit");
  entries[2].path = ctool_string("sub");

  manifest.contents = ctool_bytes(
      missing_typed_manifest,
      (ctool_u32)sizeof(missing_typed_manifest) - 1u);
  ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_INPUT,
                               CTOOL_OBJ_DIAG_INVALID_INPUT,
                               "iso-fixture typed entry missing manifest");
  manifest.contents = ctool_bytes(extra_manifest,
                                  (ctool_u32)sizeof(extra_manifest) - 1u);
  ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_INPUT,
                               CTOOL_OBJ_DIAG_INVALID_INPUT,
                               "iso-fixture manifest entry missing input");

  manifest.contents = ctool_bytes(
      file_parent_manifest,
      (ctool_u32)sizeof(file_parent_manifest) - 1u);
  file_parent[0].path = ctool_string("parent");
  file_parent[0].kind = CTOOL_OBJ_ISO_FIXTURE_FILE;
  file_parent[0].source = &empty;
  file_parent[1].path = ctool_string("parent/child");
  file_parent[1].kind = CTOOL_OBJ_ISO_FIXTURE_FILE;
  file_parent[1].source = &empty;
  request.as.iso_fixture.entries = file_parent;
  request.as.iso_fixture.entry_count = 2u;
  ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_INPUT,
                               CTOOL_OBJ_DIAG_INVALID_INPUT,
                               "iso-fixture file parent");

  manifest.contents =
      ctool_bytes(wide_manifest, (ctool_u32)sizeof(wide_manifest) - 1u);
  wide_source.path.text = ctool_string("/wide.bin");
  wide_source.contents = ctool_bytes((const void *)1, 0xffffffffu);
  wide_entry.path = ctool_string("wide.bin");
  wide_entry.kind = CTOOL_OBJ_ISO_FIXTURE_FILE;
  wide_entry.source = &wide_source;
  request.as.iso_fixture.entries = &wide_entry;
  request.as.iso_fixture.entry_count = 1u;
  ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_OVERFLOW,
                               CTOOL_OBJ_DIAG_LIMIT,
                               "iso-fixture i386 output overflow");

  manifest.contents = ctool_bytes(manifest_bytes,
                                  (ctool_u32)sizeof(manifest_bytes) - 1u);
  request.as.iso_fixture.entries = entries;
  request.as.iso_fixture.entry_count = 4u;
  before_fill = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_arena_alloc(ctool_job_arena(job),
                             config.limits.arena_bytes, 1u, &arena_fill);
  full_mark = ctool_arena_mark(ctool_job_arena(job));
  if (status != CTOOL_OK || arena_fill == (void *)0) {
    (void)fprintf(stderr, "iso-fixture: arena-limit setup failed\n");
    ok = 0;
  } else {
    ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_LIMIT,
                                 CTOOL_OBJ_DIAG_LIMIT,
                                 "iso-fixture arena limit");
    if (!arena_marks_equal(full_mark,
                           ctool_arena_mark(ctool_job_arena(job)))) {
      (void)fprintf(stderr, "iso-fixture: arena failure did not rewind\n");
      ok = 0;
    }
    if (ctool_arena_rewind(ctool_job_arena(job), before_fill) != CTOOL_OK) {
      (void)fprintf(stderr, "iso-fixture: arena recovery setup failed\n");
      ok = 0;
    }
  }
  ctool_buffer_clear(second);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_obj_transform(job, &request, second, &second_result);
  if (status != CTOOL_OK ||
      !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) ||
      !iso_fixture_layout(&second_result)) {
    (void)fprintf(stderr, "iso-fixture: same-job recovery failed\n");
    ok = 0;
  }

  ctool_buffer_close(limited);
  ctool_buffer_close(second);
  ctool_buffer_close(first);
  ctool_job_close(job);
  return ok != 0 ? 0 : 1;
}

static int run_profile_manifest(void) {
  static const char expected[] =
      "{\n"
      "  \"inputs\": [\n"
      "    {\n"
      "      \"bytes\": 3,\n"
      "      \"path\": \"a.h\",\n"
      "      \"sha256\": "
      "\"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\"\n"
      "    },\n"
      "    {\n"
      "      \"bytes\": 2,\n"
      "      \"path\": \"z.h\",\n"
      "      \"sha256\": "
      "\"c865f6c5ab8d1b0bcd383a5e1e3879d22681c96bf462c269b7581d523fbe70ab\"\n"
      "    }\n"
      "  ],\n"
      "  \"profiles\": {\n"
      "    \"doom-compat\": [\n"
      "      \"a.h\"\n"
      "    ],\n"
      "    \"doom-tree\": [\n"
      "      \"a.h\",\n"
      "      \"z.h\"\n"
      "    ]\n"
      "  },\n"
      "  \"schema\": \"cupid.doom-profile-inputs.v1\",\n"
      "  \"sources\": {\n"
      "    \"doom-compat\": [\n"
      "      \"kernel/doom/d.cc\"\n"
      "    ],\n"
      "    \"doom-tree\": [\n"
      "      \"kernel/doom/a.cc\",\n"
      "      \"kernel/doom/z.cc\"\n"
      "    ]\n"
      "  }\n"
      "}\n";
  ctool_u8 snapshot[512];
  ctool_u8 reordered[512];
  ctool_u32 snapshot_size = build_profile_snapshot(
      snapshot, (ctool_u32)sizeof(snapshot), 0);
  ctool_u32 reordered_size = build_profile_snapshot(
      reordered, (ctool_u32)sizeof(reordered), 1);
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *first = (ctool_buffer_t *)0;
  ctool_buffer_t *second = (ctool_buffer_t *)0;
  ctool_buffer_t *limited = (ctool_buffer_t *)0;
  ctool_source_t source;
  ctool_obj_request_t request;
  ctool_obj_result_t first_result;
  ctool_obj_result_t second_result;
  ctool_arena_mark_t mark;
  ctool_arena_mark_t before_fill;
  ctool_arena_mark_t full_mark;
  void *arena_fill = (void *)0;
  ctool_status_t status;
  int ok = 1;
  if (snapshot_size == 0u || reordered_size == 0u ||
      !open_job(&adapter, &config, &job)) {
    return 1;
  }
  status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                 &first);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 1024u, config.limits.output_bytes,
                                   &second);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 1u, 64u, &limited);
  }
  if (status != CTOOL_OK) {
    if (second != (ctool_buffer_t *)0) {
      ctool_buffer_close(second);
    }
    if (first != (ctool_buffer_t *)0) {
      ctool_buffer_close(first);
    }
    ctool_job_close(job);
    return 1;
  }
  source.path.text = ctool_string("/profile.snapshot");
  source.contents = ctool_bytes(snapshot, snapshot_size);
  (void)memset(&request, 0, sizeof(request));
  request.operation = CTOOL_OBJ_GENERATE_PROFILE_MANIFEST;
  request.input = &source;
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_obj_transform(job, &request, first, &first_result);
  if (status != CTOOL_OK ||
      !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) ||
      first_result.bytes.size != (ctool_u32)sizeof(expected) - 1u ||
      memcmp(first_result.bytes.data, expected, sizeof(expected) - 1u) != 0) {
    (void)fprintf(stderr, "profile-manifest: exact output failed\n");
    ok = 0;
  }
  source.contents = ctool_bytes(reordered, reordered_size);
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_obj_transform(job, &request, second, &second_result);
  if (status != CTOOL_OK ||
      !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) ||
      first_result.bytes.size != second_result.bytes.size ||
      memcmp(first_result.bytes.data, second_result.bytes.data,
             (size_t)first_result.bytes.size) != 0) {
    (void)fprintf(stderr, "profile-manifest: reordered snapshot differs\n");
    ok = 0;
  }

  ctool_buffer_clear(first);
  source.contents = ctool_bytes(snapshot, snapshot_size);
  snapshot[0] = (ctool_u8)'X';
  ok &= expect_rewound_failure(job, first, &request, CTOOL_ERR_INPUT,
                               CTOOL_OBJ_DIAG_INVALID_INPUT,
                               "profile-manifest magic");
  snapshot[0] = (ctool_u8)'C';
  source.contents = ctool_bytes(snapshot, snapshot_size - 1u);
  ok &= expect_rewound_failure(job, first, &request, CTOOL_ERR_INPUT,
                               CTOOL_OBJ_DIAG_INVALID_INPUT,
                               "profile-manifest truncation");
  source.contents = ctool_bytes(snapshot, snapshot_size);
  ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_LIMIT,
                               CTOOL_OBJ_DIAG_LIMIT,
                               "profile-manifest output limit");

  source.contents = ctool_bytes((const void *)0, 1u);
  ok &= expect_rewound_failure(job, first, &request,
                               CTOOL_ERR_INVALID_ARGUMENT,
                               CTOOL_OBJ_DIAG_INVALID_INPUT,
                               "profile-manifest invalid bytes");
  source.contents = ctool_bytes(snapshot, snapshot_size);

  before_fill = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_arena_alloc(ctool_job_arena(job), config.limits.arena_bytes,
                             1u, &arena_fill);
  full_mark = ctool_arena_mark(ctool_job_arena(job));
  if (status != CTOOL_OK || arena_fill == (void *)0) {
    (void)fprintf(stderr, "profile-manifest: arena setup failed\n");
    ok = 0;
  } else {
    ok &= expect_rewound_failure(job, first, &request, CTOOL_ERR_LIMIT,
                                 CTOOL_OBJ_DIAG_LIMIT,
                                 "profile-manifest arena limit");
    if (!arena_marks_equal(full_mark,
                           ctool_arena_mark(ctool_job_arena(job)))) {
      (void)fprintf(stderr,
                    "profile-manifest: arena failure did not rewind\n");
      ok = 0;
    }
    if (ctool_arena_rewind(ctool_job_arena(job), before_fill) != CTOOL_OK) {
      (void)fprintf(stderr, "profile-manifest: arena recovery setup failed\n");
      ok = 0;
    }
  }
  mark = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_obj_transform(job, &request, first, &second_result);
  if (status != CTOOL_OK ||
      !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) ||
      second_result.bytes.size != (ctool_u32)sizeof(expected) - 1u) {
    (void)fprintf(stderr, "profile-manifest: same-job recovery failed\n");
    ok = 0;
  }

  ctool_buffer_close(limited);
  ctool_buffer_close(second);
  ctool_buffer_close(first);
  ctool_job_close(job);
  return ok != 0 ? 0 : 1;
}

static int run_errors(void) {
  static const ctool_u8 payload[] = {1u, 2u, 3u};
  static const ctool_u8 text_payload[] = {'x', '\r', '\n', 'y'};
  static const ctool_u8 malformed[] = {0x7fu};
  ctool_u8 image[200];
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *output = (ctool_buffer_t *)0;
  ctool_buffer_t *limited = (ctool_buffer_t *)0;
  ctool_source_t source;
  ctool_obj_request_t request;
  ctool_obj_result_t recovery_result;
  ctool_arena_mark_t before_fill;
  ctool_arena_mark_t full_mark;
  ctool_status_t status;
  void *arena_fill = (void *)0;
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

  request.operation = CTOOL_OBJ_WRAP_TEXT;
  source.path.text = ctool_string("/limited.txt");
  source.contents =
      ctool_bytes(text_payload, (ctool_u32)sizeof(text_payload));
  ok &= expect_failure(job, limited, &request, CTOOL_ERR_LIMIT,
                       CTOOL_OBJ_DIAG_LIMIT, 0u, "text output limit");
  if (ctool_obj_transform(job, &request, output, &recovery_result) != CTOOL_OK) {
    (void)fprintf(stderr, "text output limit: same-job recovery failed\n");
    ok = 0;
  }
  ctool_buffer_clear(output);

  before_fill = ctool_arena_mark(ctool_job_arena(job));
  status = ctool_arena_alloc(ctool_job_arena(job), config.limits.arena_bytes,
                             1u, &arena_fill);
  full_mark = ctool_arena_mark(ctool_job_arena(job));
  if (status != CTOOL_OK || arena_fill == (void *)0) {
    (void)fprintf(stderr, "text arena limit: setup failed\n");
    ok = 0;
  } else {
    ok &= expect_failure(job, output, &request, CTOOL_ERR_LIMIT,
                         CTOOL_OBJ_DIAG_LIMIT, 0u,
                         "text normalization arena limit");
    if (!arena_marks_equal(full_mark,
                           ctool_arena_mark(ctool_job_arena(job)))) {
      (void)fprintf(stderr, "text arena limit: failure did not rewind\n");
      ok = 0;
    }
    if (ctool_arena_rewind(ctool_job_arena(job), before_fill) != CTOOL_OK ||
        ctool_obj_transform(job, &request, output, &recovery_result) !=
            CTOOL_OK ||
        !arena_marks_equal(before_fill,
                           ctool_arena_mark(ctool_job_arena(job)))) {
      (void)fprintf(stderr, "text arena limit: same-job recovery failed\n");
      ok = 0;
    }
    ctool_buffer_clear(output);
  }

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
  if (argc == 2 && strcmp(argv[1], "wrap-text") == 0) {
    return run_wrap_text();
  }
  if (argc == 2 && strcmp(argv[1], "wrap-jpeg") == 0) {
    return run_wrap_jpeg();
  }
  if (argc == 2 && strcmp(argv[1], "extract-fallback") == 0) {
    return run_extract_fallback();
  }
  if (argc == 2 && strcmp(argv[1], "errors") == 0) {
    return run_errors();
  }
  if (argc == 2 && strcmp(argv[1], "install-source") == 0) {
    return run_install_source();
  }
  if (argc == 2 && strcmp(argv[1], "ksyms-source") == 0) {
    return run_ksyms_source();
  }
  if (argc == 2 && strcmp(argv[1], "disk-template") == 0) {
    return run_disk_template();
  }
  if (argc == 2 && strcmp(argv[1], "iso-fixture") == 0) {
    return run_iso_fixture();
  }
  if (argc == 2 && strcmp(argv[1], "profile-manifest") == 0) {
    return run_profile_manifest();
  }
  (void)fprintf(stderr,
                 "usage: cupidobj-contract wrap-basic|wrap-model|"
                 "wrap-text|wrap-jpeg|extract-basic|extract-fallback|"
                 "install-source|"
                 "ksyms-source|disk-template|iso-fixture|profile-manifest|"
                 "errors\n");
  return 2;
}
