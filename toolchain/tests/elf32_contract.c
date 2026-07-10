#include "ctool.h"
#include "ctool_host.h"
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

static ctool_u32 build_exec_fixture(ctool_u8 *bytes, ctool_u32 capacity,
                                     ctool_bool with_sections) {
  static const ctool_u8 text[] = {0x55u, 0x89u, 0xe5u, 0xc3u};
  static const char strtab[] = "\0entry\0";
  static const char shstrtab[] =
      "\0.text\0.symtab\0.strtab\0.shstrtab\0";
  const ctool_u32 image_size = with_sections == CTOOL_TRUE ? 364u : 88u;
  const ctool_u32 section_headers = 164u;
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
  write_le32(bytes, 24u, 0x00100000u);
  write_le32(bytes, 28u, 52u);
  write_le32(bytes, 32u,
             with_sections == CTOOL_TRUE ? section_headers : 0u);
  write_le32(bytes, 36u, 0x12345678u);
  write_le16(bytes, 40u, 52u);
  write_le16(bytes, 42u, 32u);
  write_le16(bytes, 44u, 1u);
  write_le16(bytes, 46u, with_sections == CTOOL_TRUE ? 40u : 0u);
  write_le16(bytes, 48u, with_sections == CTOOL_TRUE ? 5u : 0u);
  write_le16(bytes, 50u, with_sections == CTOOL_TRUE ? 4u : 0u);

  write_le32(bytes, 52u, 1u);
  write_le32(bytes, 56u, 84u);
  write_le32(bytes, 60u, 0x00100000u);
  write_le32(bytes, 64u, 0x00100000u);
  write_le32(bytes, 68u, 4u);
  write_le32(bytes, 72u, 8u);
  write_le32(bytes, 76u, 5u);
  write_le32(bytes, 80u, 4u);
  (void)memcpy(bytes + 84u, text, sizeof(text));
  if (with_sections == CTOOL_FALSE) {
    return image_size;
  }

  (void)memcpy(bytes + 88u, strtab, sizeof(strtab));
  write_le32(bytes, 112u, 1u);
  write_le32(bytes, 116u, 0x00100000u);
  write_le32(bytes, 120u, 4u);
  bytes[124u] = 0x12u;
  write_le16(bytes, 126u, 1u);
  (void)memcpy(bytes + 128u, shstrtab, sizeof(shstrtab));

  header = section_headers + 40u;
  write_le32(bytes, header, 1u);
  write_le32(bytes, header + 4u, 1u);
  write_le32(bytes, header + 8u, 6u);
  write_le32(bytes, header + 12u, 0x00100000u);
  write_le32(bytes, header + 16u, 84u);
  write_le32(bytes, header + 20u, 4u);
  write_le32(bytes, header + 32u, 4u);

  header = section_headers + 80u;
  write_le32(bytes, header, 7u);
  write_le32(bytes, header + 4u, 2u);
  write_le32(bytes, header + 16u, 96u);
  write_le32(bytes, header + 20u, 32u);
  write_le32(bytes, header + 24u, 3u);
  write_le32(bytes, header + 28u, 1u);
  write_le32(bytes, header + 32u, 4u);
  write_le32(bytes, header + 36u, 16u);

  header = section_headers + 120u;
  write_le32(bytes, header, 15u);
  write_le32(bytes, header + 4u, 3u);
  write_le32(bytes, header + 16u, 88u);
  write_le32(bytes, header + 20u, (ctool_u32)sizeof(strtab));
  write_le32(bytes, header + 32u, 1u);

  header = section_headers + 160u;
  write_le32(bytes, header, 23u);
  write_le32(bytes, header + 4u, 3u);
  write_le32(bytes, header + 16u, 128u);
  write_le32(bytes, header + 20u, (ctool_u32)sizeof(shstrtab));
  write_le32(bytes, header + 32u, 1u);
  return image_size;
}

static ctool_u32 build_tls_exec_fixture(ctool_u8 *bytes,
                                         ctool_u32 capacity) {
  static const char strtab[] = "\0tls_init\0tls_zero\0";
  static const char shstrtab[] =
      "\0.text\0.tdata\0.tbss\0.symtab\0.strtab\0.shstrtab\0";
  const ctool_u32 image_size = 520u;
  const ctool_u32 section_headers = 240u;
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
  write_le32(bytes, 24u, 0x1000u);
  write_le32(bytes, 28u, 52u);
  write_le32(bytes, 32u, section_headers);
  write_le16(bytes, 40u, 52u);
  write_le16(bytes, 42u, 32u);
  write_le16(bytes, 44u, 2u);
  write_le16(bytes, 46u, 40u);
  write_le16(bytes, 48u, 7u);
  write_le16(bytes, 50u, 6u);

  write_le32(bytes, 52u, CTOOL_ELF32_PT_LOAD);
  write_le32(bytes, 56u, 116u);
  write_le32(bytes, 60u, 0x1000u);
  write_le32(bytes, 64u, 0x1000u);
  write_le32(bytes, 68u, 1u);
  write_le32(bytes, 72u, 1u);
  write_le32(bytes, 76u, CTOOL_ELF32_PF_R | CTOOL_ELF32_PF_X);
  write_le32(bytes, 80u, 1u);
  write_le32(bytes, 84u, CTOOL_ELF32_PT_TLS);
  write_le32(bytes, 88u, 120u);
  write_le32(bytes, 92u, 0x2000u);
  write_le32(bytes, 96u, 0x2000u);
  write_le32(bytes, 100u, 4u);
  write_le32(bytes, 104u, 8u);
  write_le32(bytes, 108u, CTOOL_ELF32_PF_R);
  write_le32(bytes, 112u, 4u);

  bytes[116u] = 0xc3u;
  write_le32(bytes, 120u, 1u);
  write_le32(bytes, 140u, 1u);
  write_le32(bytes, 144u, 0u);
  write_le32(bytes, 148u, 4u);
  bytes[152u] = 0x16u;
  write_le16(bytes, 154u, 2u);
  write_le32(bytes, 156u, 10u);
  write_le32(bytes, 160u, 4u);
  write_le32(bytes, 164u, 4u);
  bytes[168u] = 0x16u;
  write_le16(bytes, 170u, 3u);
  (void)memcpy(bytes + 172u, strtab, sizeof(strtab));
  (void)memcpy(bytes + 192u, shstrtab, sizeof(shstrtab));

  header = section_headers + 40u;
  write_le32(bytes, header, 1u);
  write_le32(bytes, header + 4u, CTOOL_ELF32_SHT_PROGBITS);
  write_le32(bytes, header + 8u,
             CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR);
  write_le32(bytes, header + 12u, 0x1000u);
  write_le32(bytes, header + 16u, 116u);
  write_le32(bytes, header + 20u, 1u);
  write_le32(bytes, header + 32u, 1u);

  header = section_headers + 80u;
  write_le32(bytes, header, 7u);
  write_le32(bytes, header + 4u, CTOOL_ELF32_SHT_PROGBITS);
  write_le32(bytes, header + 8u,
             CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE |
                 CTOOL_ELF32_SHF_TLS);
  write_le32(bytes, header + 12u, 0x2000u);
  write_le32(bytes, header + 16u, 120u);
  write_le32(bytes, header + 20u, 4u);
  write_le32(bytes, header + 32u, 4u);

  header = section_headers + 120u;
  write_le32(bytes, header, 14u);
  write_le32(bytes, header + 4u, CTOOL_ELF32_SHT_NOBITS);
  write_le32(bytes, header + 8u,
             CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE |
                 CTOOL_ELF32_SHF_TLS);
  write_le32(bytes, header + 12u, 0x2004u);
  write_le32(bytes, header + 16u, 124u);
  write_le32(bytes, header + 20u, 4u);
  write_le32(bytes, header + 32u, 4u);

  header = section_headers + 160u;
  write_le32(bytes, header, 20u);
  write_le32(bytes, header + 4u, 2u);
  write_le32(bytes, header + 16u, 124u);
  write_le32(bytes, header + 20u, 48u);
  write_le32(bytes, header + 24u, 5u);
  write_le32(bytes, header + 28u, 1u);
  write_le32(bytes, header + 32u, 4u);
  write_le32(bytes, header + 36u, 16u);

  header = section_headers + 200u;
  write_le32(bytes, header, 28u);
  write_le32(bytes, header + 4u, 3u);
  write_le32(bytes, header + 16u, 172u);
  write_le32(bytes, header + 20u, (ctool_u32)sizeof(strtab));
  write_le32(bytes, header + 32u, 1u);

  header = section_headers + 240u;
  write_le32(bytes, header, 36u);
  write_le32(bytes, header + 4u, 3u);
  write_le32(bytes, header + 16u, 192u);
  write_le32(bytes, header + 20u, (ctool_u32)sizeof(shstrtab));
  write_le32(bytes, header + 32u, 1u);
  return image_size;
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

static int expect_reader_failure(ctool_job_t *job, const ctool_u8 *bytes,
                                 ctool_u32 size, ctool_status_t expected_status,
                                 ctool_u32 expected_code,
                                 const char *case_name);

static int open_job_at(const char *root, ctool_host_adapter_t *adapter,
                       ctool_job_config_t *config, ctool_job_t **job) {
  ctool_status_t status = ctool_host_adapter_init(adapter, root);
  if (!check_status(status, CTOOL_OK, "host adapter init")) {
    return 0;
  }
  *config = ctool_host_job_config(adapter, ctool_default_limits());
  status = ctool_job_open(config, job);
  return check_status(status, CTOOL_OK, "job open");
}

static int open_job(ctool_host_adapter_t *adapter,
                    ctool_job_config_t *config, ctool_job_t **job) {
  return open_job_at(".", adapter, config, job);
}

static ctool_u32 find_section(ctool_bytes_t image, const char *name) {
  ctool_u32 section_headers = read_le32(image.data, 32u);
  ctool_u32 section_count = (ctool_u32)read_le16(image.data, 48u);
  ctool_u32 shstrtab = (ctool_u32)read_le16(image.data, 50u);
  ctool_u32 shstr_header = section_headers + shstrtab * 40u;
  ctool_u32 strings = read_le32(image.data, shstr_header + 16u);
  ctool_u32 index;
  for (index = 1u; index < section_count; index++) {
    ctool_u32 header = section_headers + index * 40u;
    ctool_u32 name_offset = read_le32(image.data, header);
    if (strcmp((const char *)(image.data + strings + name_offset), name) == 0) {
      return index;
    }
  }
  return 0u;
}

static const char *symbol_name(ctool_bytes_t image, ctool_u32 strtab_offset,
                               ctool_u32 symtab_offset,
                               ctool_u32 symbol_index) {
  ctool_u32 name_offset =
      read_le32(image.data, symtab_offset + symbol_index * 16u);
  return (const char *)(image.data + strtab_offset + name_offset);
}

static int run_writer_basic(void) {
  static const ctool_u8 text[] = {0xc3u};
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_buffer_t *first;
  ctool_buffer_t *second;
  ctool_elf32_section_spec_t section;
  ctool_elf32_object_spec_t object;
  ctool_bytes_t first_bytes;
  ctool_bytes_t second_bytes;
  ctool_u32 section_headers;
  ctool_u32 text_header;
  ctool_u32 text_offset;
  ctool_status_t status;

  if (!open_job(&adapter, &config, &job)) {
    return 1;
  }
  status = ctool_job_open_buffer(job, 64u, config.limits.output_bytes, &first);
  if (status == CTOOL_OK) {
    status =
        ctool_job_open_buffer(job, 64u, config.limits.output_bytes, &second);
  }
  if (!check_status(status, CTOOL_OK, "output buffers")) {
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
  object.sections = &section;
  object.section_count = 1u;
  object.symbols = (const ctool_elf32_symbol_spec_t *)0;
  object.symbol_count = 0u;
  object.relocations = (const ctool_elf32_relocation_spec_t *)0;
  object.relocation_count = 0u;

  status = ctool_elf32_write(job, &object, first);
  if (status == CTOOL_OK) {
    status = ctool_elf32_write(job, &object, second);
  }
  first_bytes = ctool_buffer_view(first);
  second_bytes = ctool_buffer_view(second);
  if (!check_status(status, CTOOL_OK, "basic ELF32 write") ||
      first_bytes.size != second_bytes.size || first_bytes.size < 52u ||
      memcmp(first_bytes.data, second_bytes.data, first_bytes.size) != 0) {
    (void)fprintf(stderr, "writer output is not deterministic\n");
    ctool_buffer_close(second);
    ctool_buffer_close(first);
    ctool_job_close(job);
    return 1;
  }

  section_headers = read_le32(first_bytes.data, 32u);
  text_header = section_headers + 40u;
  text_offset = read_le32(first_bytes.data, text_header + 16u);
  if (first_bytes.data[0] != 0x7fu || first_bytes.data[1] != (ctool_u8)'E' ||
      first_bytes.data[2] != (ctool_u8)'L' ||
      first_bytes.data[3] != (ctool_u8)'F' || first_bytes.data[4] != 1u ||
      first_bytes.data[5] != 1u || read_le16(first_bytes.data, 16u) != 1u ||
      read_le16(first_bytes.data, 18u) != 3u ||
      read_le32(first_bytes.data, 28u) != 0u ||
      read_le16(first_bytes.data, 44u) != 0u ||
      read_le16(first_bytes.data, 46u) != 40u ||
      read_le16(first_bytes.data, 48u) != 5u ||
      read_le16(first_bytes.data, 50u) != 4u ||
      text_header + 40u > first_bytes.size ||
      read_le32(first_bytes.data, text_header + 4u) != 1u ||
      read_le32(first_bytes.data, text_header + 8u) != 6u ||
      read_le32(first_bytes.data, text_header + 20u) != 1u ||
      read_le32(first_bytes.data, text_header + 32u) != 16u ||
      text_offset >= first_bytes.size || first_bytes.data[text_offset] != 0xc3u) {
    (void)fprintf(stderr, "writer emitted an invalid basic ELF32 object\n");
    ctool_buffer_close(second);
    ctool_buffer_close(first);
    ctool_job_close(job);
    return 1;
  }

  ctool_buffer_close(second);
  ctool_buffer_close(first);
  ctool_job_close(job);
  (void)puts("writer-basic: ok");
  return 0;
}

static int run_writer_model(const char *root) {
  static const ctool_u8 text[] = {0xe8u, 0u, 0u, 0u, 0u, 0xc3u};
  static const ctool_u8 data[] = {0u, 0u, 0u, 0u};
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_buffer_t *output;
  ctool_elf32_section_spec_t sections[3];
  ctool_elf32_symbol_spec_t symbols[7];
  ctool_elf32_relocation_spec_t relocations[2];
  ctool_elf32_object_spec_t object;
  ctool_bytes_t image;
  ctool_u32 section_headers;
  ctool_u32 text_index;
  ctool_u32 data_index;
  ctool_u32 bss_index;
  ctool_u32 rel_text_index;
  ctool_u32 rel_data_index;
  ctool_u32 symtab_index;
  ctool_u32 strtab_index;
  ctool_u32 text_header;
  ctool_u32 data_header;
  ctool_u32 bss_header;
  ctool_u32 rel_text_header;
  ctool_u32 rel_data_header;
  ctool_u32 symtab_header;
  ctool_u32 strtab_header;
  ctool_u32 text_offset;
  ctool_u32 data_offset;
  ctool_u32 symtab_offset;
  ctool_u32 strtab_offset;
  ctool_u32 rel_text_offset;
  ctool_u32 rel_data_offset;
  ctool_u32 symbol_offset;
  ctool_path_t logical_root;
  ctool_path_t output_path;
  ctool_status_t status;

  if (!open_job_at(root != (const char *)0 ? root : ".", &adapter, &config,
                   &job)) {
    return 1;
  }
  status =
      ctool_job_open_buffer(job, 256u, config.limits.output_bytes, &output);
  if (!check_status(status, CTOOL_OK, "model output buffer")) {
    ctool_job_close(job);
    return 1;
  }

  sections[0].name = ctool_string(".text");
  sections[0].type = CTOOL_ELF32_SHT_PROGBITS;
  sections[0].flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR;
  sections[0].alignment = 16u;
  sections[0].entry_size = 0u;
  sections[0].size = (ctool_u32)sizeof(text);
  sections[0].contents = ctool_bytes(text, (ctool_u32)sizeof(text));
  sections[1].name = ctool_string(".data");
  sections[1].type = CTOOL_ELF32_SHT_PROGBITS;
  sections[1].flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE;
  sections[1].alignment = 4u;
  sections[1].entry_size = 0u;
  sections[1].size = (ctool_u32)sizeof(data);
  sections[1].contents = ctool_bytes(data, (ctool_u32)sizeof(data));
  sections[2].name = ctool_string(".bss");
  sections[2].type = CTOOL_ELF32_SHT_NOBITS;
  sections[2].flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE;
  sections[2].alignment = 16u;
  sections[2].entry_size = 0u;
  sections[2].size = 32u;
  sections[2].contents = ctool_bytes((const void *)0, 0u);

  symbols[0].name = ctool_string("entry");
  symbols[0].binding = CTOOL_ELF32_BIND_GLOBAL;
  symbols[0].type = CTOOL_ELF32_SYMBOL_FUNCTION;
  symbols[0].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[0].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  symbols[0].section = 0u;
  symbols[0].value = 0u;
  symbols[0].size = 6u;
  symbols[0].alignment = 0u;
  symbols[1].name = ctool_string("");
  symbols[1].binding = CTOOL_ELF32_BIND_LOCAL;
  symbols[1].type = CTOOL_ELF32_SYMBOL_SECTION;
  symbols[1].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[1].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  symbols[1].section = 0u;
  symbols[1].value = 0u;
  symbols[1].size = 0u;
  symbols[1].alignment = 0u;
  symbols[2].name = ctool_string("weak_hook");
  symbols[2].binding = CTOOL_ELF32_BIND_WEAK;
  symbols[2].type = CTOOL_ELF32_SYMBOL_FUNCTION;
  symbols[2].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[2].placement = CTOOL_ELF32_SYMBOL_UNDEFINED;
  symbols[2].section = CTOOL_ELF32_NO_SECTION;
  symbols[2].value = 0u;
  symbols[2].size = 0u;
  symbols[2].alignment = 0u;
  symbols[3].name = ctool_string("fixture.c");
  symbols[3].binding = CTOOL_ELF32_BIND_LOCAL;
  symbols[3].type = CTOOL_ELF32_SYMBOL_FILE;
  symbols[3].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[3].placement = CTOOL_ELF32_SYMBOL_ABSOLUTE;
  symbols[3].section = CTOOL_ELF32_NO_SECTION;
  symbols[3].value = 0u;
  symbols[3].size = 0u;
  symbols[3].alignment = 0u;
  symbols[4].name = ctool_string("external");
  symbols[4].binding = CTOOL_ELF32_BIND_GLOBAL;
  symbols[4].type = CTOOL_ELF32_SYMBOL_NOTYPE;
  symbols[4].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[4].placement = CTOOL_ELF32_SYMBOL_UNDEFINED;
  symbols[4].section = CTOOL_ELF32_NO_SECTION;
  symbols[4].value = 0u;
  symbols[4].size = 0u;
  symbols[4].alignment = 0u;
  symbols[5].name = ctool_string("common_data");
  symbols[5].binding = CTOOL_ELF32_BIND_GLOBAL;
  symbols[5].type = CTOOL_ELF32_SYMBOL_OBJECT;
  symbols[5].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[5].placement = CTOOL_ELF32_SYMBOL_COMMON_STORAGE;
  symbols[5].section = CTOOL_ELF32_NO_SECTION;
  symbols[5].value = 0u;
  symbols[5].size = 12u;
  symbols[5].alignment = 16u;
  symbols[6].name = ctool_string("constant");
  symbols[6].binding = CTOOL_ELF32_BIND_LOCAL;
  symbols[6].type = CTOOL_ELF32_SYMBOL_NOTYPE;
  symbols[6].visibility = CTOOL_ELF32_VIS_HIDDEN;
  symbols[6].placement = CTOOL_ELF32_SYMBOL_ABSOLUTE;
  symbols[6].section = CTOOL_ELF32_NO_SECTION;
  symbols[6].value = 0x1234u;
  symbols[6].size = 0u;
  symbols[6].alignment = 0u;

  relocations[0].target_section = 0u;
  relocations[0].offset = 1u;
  relocations[0].symbol = 4u;
  relocations[0].type = CTOOL_ELF32_R_386_PC32;
  relocations[0].addend = -4;
  relocations[1].target_section = 1u;
  relocations[1].offset = 0u;
  relocations[1].symbol = 5u;
  relocations[1].type = CTOOL_ELF32_R_386_32;
  relocations[1].addend = 7;
  object.sections = sections;
  object.section_count = 3u;
  object.symbols = symbols;
  object.symbol_count = 7u;
  object.relocations = relocations;
  object.relocation_count = 2u;

  status = ctool_elf32_write(job, &object, output);
  image = ctool_buffer_view(output);
  if (!check_status(status, CTOOL_OK, "ELF32 semantic model write")) {
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }
  section_headers = read_le32(image.data, 32u);
  text_index = find_section(image, ".text");
  data_index = find_section(image, ".data");
  bss_index = find_section(image, ".bss");
  rel_text_index = find_section(image, ".rel.text");
  rel_data_index = find_section(image, ".rel.data");
  symtab_index = find_section(image, ".symtab");
  strtab_index = find_section(image, ".strtab");
  if (text_index != 1u || data_index != 2u || bss_index != 3u ||
      rel_text_index != 4u || rel_data_index != 5u || symtab_index != 6u ||
      strtab_index != 7u || read_le16(image.data, 48u) != 9u) {
    (void)fprintf(stderr, "writer section order differs\n");
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  text_header = section_headers + text_index * 40u;
  data_header = section_headers + data_index * 40u;
  bss_header = section_headers + bss_index * 40u;
  rel_text_header = section_headers + rel_text_index * 40u;
  rel_data_header = section_headers + rel_data_index * 40u;
  symtab_header = section_headers + symtab_index * 40u;
  strtab_header = section_headers + strtab_index * 40u;
  text_offset = read_le32(image.data, text_header + 16u);
  data_offset = read_le32(image.data, data_header + 16u);
  rel_text_offset = read_le32(image.data, rel_text_header + 16u);
  rel_data_offset = read_le32(image.data, rel_data_header + 16u);
  symtab_offset = read_le32(image.data, symtab_header + 16u);
  strtab_offset = read_le32(image.data, strtab_header + 16u);

  if (read_le32(image.data, bss_header + 4u) != 8u ||
      read_le32(image.data, bss_header + 20u) != 32u ||
      read_le32(image.data, bss_header + 32u) != 16u ||
      read_le32(image.data, rel_text_header + 4u) != 9u ||
      read_le32(image.data, rel_text_header + 24u) != symtab_index ||
      read_le32(image.data, rel_text_header + 28u) != text_index ||
      read_le32(image.data, rel_data_header + 24u) != symtab_index ||
      read_le32(image.data, rel_data_header + 28u) != data_index ||
      read_le32(image.data, symtab_header + 20u) != 8u * 16u ||
      read_le32(image.data, symtab_header + 28u) != 4u ||
      read_le32(image.data, rel_text_offset) != 1u ||
      read_le32(image.data, rel_text_offset + 4u) != 0x00000602u ||
      read_le32(image.data, rel_data_offset) != 0u ||
      read_le32(image.data, rel_data_offset + 4u) != 0x00000701u ||
      read_le32(image.data, text_offset + 1u) != 0xfffffffcu ||
      read_le32(image.data, data_offset) != 7u) {
    (void)fprintf(stderr, "writer section or relocation metadata differs\n");
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  if (strcmp(symbol_name(image, strtab_offset, symtab_offset, 1u), "") != 0 ||
      strcmp(symbol_name(image, strtab_offset, symtab_offset, 2u),
             "fixture.c") != 0 ||
      strcmp(symbol_name(image, strtab_offset, symtab_offset, 3u),
             "constant") != 0 ||
      strcmp(symbol_name(image, strtab_offset, symtab_offset, 4u), "entry") !=
          0 ||
      strcmp(symbol_name(image, strtab_offset, symtab_offset, 5u),
             "weak_hook") != 0 ||
      strcmp(symbol_name(image, strtab_offset, symtab_offset, 6u),
             "external") != 0 ||
      strcmp(symbol_name(image, strtab_offset, symtab_offset, 7u),
             "common_data") != 0) {
    (void)fprintf(stderr, "writer symbol partition differs\n");
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }
  symbol_offset = symtab_offset + 5u * 16u;
  if (image.data[symbol_offset + 12u] != 0x22u ||
      read_le16(image.data, symbol_offset + 14u) != 0u) {
    (void)fprintf(stderr, "weak undefined symbol differs\n");
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }
  symbol_offset = symtab_offset + 7u * 16u;
  if (read_le32(image.data, symbol_offset + 4u) != 16u ||
      read_le32(image.data, symbol_offset + 8u) != 12u ||
      image.data[symbol_offset + 12u] != 0x11u ||
      read_le16(image.data, symbol_offset + 14u) != 0xfff2u) {
    (void)fprintf(stderr, "common symbol differs\n");
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }
  symbol_offset = symtab_offset + 3u * 16u;
  if (read_le32(image.data, symbol_offset + 4u) != 0x1234u ||
      image.data[symbol_offset + 13u] != 2u ||
      read_le16(image.data, symbol_offset + 14u) != 0xfff1u) {
    (void)fprintf(stderr, "absolute symbol differs\n");
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  if (root != (const char *)0) {
    status = ctool_path_root(ctool_job_arena(job), &logical_root);
    if (status == CTOOL_OK) {
      status = ctool_path_resolve(ctool_job_arena(job), &logical_root,
                                  ctool_string("/cupid.o"),
                                  config.limits.path_bytes, &output_path);
    }
    if (status == CTOOL_OK) {
      status = ctool_job_write(job, &output_path, image);
    }
    if (!check_status(status, CTOOL_OK, "write Cupid ELF32 oracle")) {
      ctool_buffer_close(output);
      ctool_job_close(job);
      return 1;
    }
  }

  ctool_buffer_close(output);
  ctool_job_close(job);
  (void)puts("writer-model: ok");
  return 0;
}

static int run_writer_errors(void) {
  static const ctool_u8 text[] = {0x90u, 0u, 0u, 0u, 0u, 0u, 0u, 0xc3u};
  static const ctool_u8 merged_strings[] = {'o', 'k', 0u};
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_buffer_t *output;
  ctool_buffer_t *limited;
  ctool_elf32_section_spec_t section;
  ctool_elf32_symbol_spec_t symbol;
  ctool_elf32_relocation_spec_t relocations[2];
  ctool_elf32_object_spec_t object;
  const ctool_diagnostic_t *diagnostic;
  ctool_status_t status;
  if (!open_job(&adapter, &config, &job)) {
    return 1;
  }
  status = ctool_job_open_buffer(job, 64u, config.limits.output_bytes, &output);
  if (status == CTOOL_OK) {
    status = ctool_job_open_buffer(job, 16u, 64u, &limited);
  }
  if (!check_status(status, CTOOL_OK, "error output buffers")) {
    ctool_job_close(job);
    return 1;
  }
  section.name = ctool_string(".text");
  section.type = CTOOL_ELF32_SHT_PROGBITS;
  section.flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR;
  section.alignment = 3u;
  section.entry_size = 0u;
  section.size = (ctool_u32)sizeof(text);
  section.contents = ctool_bytes(text, (ctool_u32)sizeof(text));
  object.sections = &section;
  object.section_count = 1u;
  object.symbols = (const ctool_elf32_symbol_spec_t *)0;
  object.symbol_count = 0u;
  object.relocations = (const ctool_elf32_relocation_spec_t *)0;
  object.relocation_count = 0u;
  status = ctool_elf32_write(job, &object, output);
  diagnostic = ctool_job_diagnostic(job, 0u);
  if (!check_status(status, CTOOL_ERR_INPUT, "invalid writer model") ||
      ctool_buffer_view(output).size != 0u ||
      ctool_job_diagnostic_count(job) != 1u ||
      diagnostic == (const ctool_diagnostic_t *)0 ||
      diagnostic->code != CTOOL_ELF32_DIAG_INVALID_SPEC ||
      strcmp(diagnostic->message.data,
             "invalid ELF32 section description") != 0) {
    (void)fprintf(stderr, "invalid model diagnostic differs\n");
    ctool_buffer_close(limited);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  section.alignment = 16u;
  section.flags = 0x00000040u;
  status = ctool_elf32_write(job, &object, output);
  diagnostic = ctool_job_diagnostic(job, 1u);
  if (!check_status(status, CTOOL_ERR_INPUT, "unsupported section flags") ||
      ctool_buffer_view(output).size != 0u ||
      ctool_job_diagnostic_count(job) != 2u ||
      diagnostic == (const ctool_diagnostic_t *)0 ||
      diagnostic->code != CTOOL_ELF32_DIAG_INVALID_SPEC) {
    (void)fprintf(stderr, "unsupported flag diagnostic differs\n");
    ctool_buffer_close(limited);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  section.name = ctool_string(".rodata.str1.1");
  section.flags = CTOOL_ELF32_SHF_STRINGS;
  section.alignment = 1u;
  section.entry_size = 1u;
  section.size = (ctool_u32)sizeof(merged_strings);
  section.contents =
      ctool_bytes(merged_strings, (ctool_u32)sizeof(merged_strings));
  status = ctool_elf32_write(job, &object, output);
  diagnostic = ctool_job_diagnostic(job, 2u);
  if (!check_status(status, CTOOL_ERR_INPUT, "strings without merge") ||
      ctool_buffer_view(output).size != 0u ||
      ctool_job_diagnostic_count(job) != 3u ||
      diagnostic == (const ctool_diagnostic_t *)0 ||
      diagnostic->code != CTOOL_ELF32_DIAG_INVALID_SPEC) {
    (void)fprintf(stderr, "merge/string diagnostic differs\n");
    ctool_buffer_close(limited);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  section.flags = CTOOL_ELF32_SHF_MERGE | CTOOL_ELF32_SHF_STRINGS;
  status = ctool_elf32_write(job, &object, output);
  if (!check_status(status, CTOOL_OK, "merge/string section") ||
      ctool_buffer_view(output).size == 0u ||
      ctool_buffer_rewind(output, 0u) != CTOOL_OK) {
    (void)fprintf(stderr, "valid merge/string section differs\n");
    ctool_buffer_close(limited);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  section.name = ctool_string(".text");
  section.flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR;
  section.alignment = 16u;
  section.entry_size = 0u;
  section.size = (ctool_u32)sizeof(text);
  section.contents = ctool_bytes(text, (ctool_u32)sizeof(text));
  symbol.name = ctool_string("target");
  symbol.binding = CTOOL_ELF32_BIND_GLOBAL;
  symbol.type = CTOOL_ELF32_SYMBOL_NOTYPE;
  symbol.visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbol.placement = CTOOL_ELF32_SYMBOL_UNDEFINED;
  symbol.section = CTOOL_ELF32_NO_SECTION;
  symbol.value = 0u;
  symbol.size = 0u;
  symbol.alignment = 0u;
  relocations[0].target_section = 0u;
  relocations[0].offset = 1u;
  relocations[0].symbol = 0u;
  relocations[0].type = CTOOL_ELF32_R_386_PC32;
  relocations[0].addend = -4;
  relocations[1] = relocations[0];
  relocations[1].offset = 3u;
  object.symbols = &symbol;
  object.symbol_count = 1u;
  object.relocations = relocations;
  object.relocation_count = 2u;
  status = ctool_elf32_write(job, &object, output);
  diagnostic = ctool_job_diagnostic(job, 3u);
  if (!check_status(status, CTOOL_ERR_INPUT, "overlapping relocations") ||
      ctool_buffer_view(output).size != 0u ||
      ctool_job_diagnostic_count(job) != 4u ||
      diagnostic == (const ctool_diagnostic_t *)0 ||
      diagnostic->code != CTOOL_ELF32_DIAG_INVALID_SPEC ||
      strcmp(diagnostic->message.data, "ELF32 relocation fields overlap") !=
          0) {
    (void)fprintf(stderr, "overlapping relocation diagnostic differs\n");
    ctool_buffer_close(limited);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }

  object.symbols = (const ctool_elf32_symbol_spec_t *)0;
  object.symbol_count = 0u;
  object.relocations = (const ctool_elf32_relocation_spec_t *)0;
  object.relocation_count = 0u;
  status = ctool_elf32_write(job, &object, limited);
  diagnostic = ctool_job_diagnostic(job, 4u);
  if (!check_status(status, CTOOL_ERR_LIMIT, "limited writer output") ||
      ctool_buffer_view(limited).size != 0u ||
      ctool_job_diagnostic_count(job) != 5u ||
      diagnostic == (const ctool_diagnostic_t *)0 ||
      diagnostic->code != CTOOL_ELF32_DIAG_LIMIT ||
      strcmp(diagnostic->message.data, "ELF32 output limit exceeded") != 0) {
    (void)fprintf(stderr, "output limit rollback or diagnostic differs\n");
    ctool_buffer_close(limited);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }
  ctool_buffer_close(limited);
  ctool_buffer_close(output);
  ctool_job_close(job);
  (void)puts("writer-errors: ok");
  return 0;
}

static const ctool_elf32_section_t *find_view_section(
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

static const ctool_elf32_symbol_t *find_view_symbol(
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

static ctool_status_t load_elf32_object(ctool_job_t *job,
                                         const char *logical_name,
                                         ctool_source_t *source,
                                         ctool_elf32_object_t *object) {
  ctool_path_t root;
  ctool_path_t path;
  ctool_status_t status = ctool_path_root(ctool_job_arena(job), &root);
  if (status == CTOOL_OK) {
    status = ctool_path_resolve(ctool_job_arena(job), &root,
                                ctool_string(logical_name), 4096u, &path);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_load_source(job, &path, source);
  }
  if (status == CTOOL_OK) {
    status = ctool_elf32_read(job, source, object);
  }
  return status;
}

static int run_reader_roundtrip(void) {
  static const ctool_u8 text[] = {0xe8u, 0u, 0u, 0u, 0u, 0xc3u};
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_buffer_t *output;
  ctool_elf32_section_spec_t section;
  ctool_elf32_symbol_spec_t symbols[2];
  ctool_elf32_relocation_spec_t relocation;
  ctool_elf32_object_spec_t spec;
  ctool_source_t source;
  ctool_elf32_object_t object = {0};
  const ctool_elf32_section_t *text_view;
  const ctool_elf32_section_t *rel_view;
  ctool_u8 *copy;
  ctool_u32 section_headers;
  ctool_u32 text_header;
  ctool_u32 rel_header;
  ctool_u32 rel_offset;
  ctool_u32 raw_info;
  ctool_status_t status;
  if (!open_job(&adapter, &config, &job)) {
    return 1;
  }
  status =
      ctool_job_open_buffer(job, 256u, config.limits.output_bytes, &output);
  if (!check_status(status, CTOOL_OK, "roundtrip output buffer")) {
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
  symbols[0].name = ctool_string("entry");
  symbols[0].binding = CTOOL_ELF32_BIND_GLOBAL;
  symbols[0].type = CTOOL_ELF32_SYMBOL_FUNCTION;
  symbols[0].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[0].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  symbols[0].section = 0u;
  symbols[0].value = 0u;
  symbols[0].size = 6u;
  symbols[0].alignment = 0u;
  symbols[1].name = ctool_string("external");
  symbols[1].binding = CTOOL_ELF32_BIND_GLOBAL;
  symbols[1].type = CTOOL_ELF32_SYMBOL_NOTYPE;
  symbols[1].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[1].placement = CTOOL_ELF32_SYMBOL_UNDEFINED;
  symbols[1].section = CTOOL_ELF32_NO_SECTION;
  symbols[1].value = 0u;
  symbols[1].size = 0u;
  symbols[1].alignment = 0u;
  relocation.target_section = 0u;
  relocation.offset = 1u;
  relocation.symbol = 1u;
  relocation.type = CTOOL_ELF32_R_386_PC32;
  relocation.addend = -4;
  spec.sections = &section;
  spec.section_count = 1u;
  spec.symbols = symbols;
  spec.symbol_count = 2u;
  spec.relocations = &relocation;
  spec.relocation_count = 1u;
  status = ctool_elf32_write(job, &spec, output);
  source.path.text = ctool_string("/roundtrip.o");
  source.contents = ctool_buffer_view(output);
  if (status == CTOOL_OK) {
    status = ctool_elf32_read(job, &source, &object);
  }
  text_view = find_view_section(&object, ".text");
  rel_view = find_view_section(&object, ".rel.text");
  if (!check_status(status, CTOOL_OK, "ELF32 read after write") ||
      object.image.data != source.contents.data ||
      object.image.size != source.contents.size || object.section_count != 6u ||
      object.file_type != CTOOL_ELF32_ET_REL || object.entry_point != 0u ||
      object.flags != 0u || object.program_headers !=
                                (const ctool_elf32_program_header_t *)0 ||
      object.program_header_count != 0u ||
      object.symbol_count != 3u || object.relocation_count != 1u ||
      text_view == (const ctool_elf32_section_t *)0 ||
      text_view->file_index != 1u ||
      text_view->type != CTOOL_ELF32_SHT_PROGBITS ||
      text_view->flags !=
          (CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR) ||
      text_view->alignment != 16u || text_view->contents.size != 6u ||
      rel_view == (const ctool_elf32_section_t *)0 || rel_view->type != 9u ||
      object.symbols[0].file_index != 0u ||
      object.symbols[0].name.size != 0u ||
      strcmp(object.symbols[1].name.data, "entry") != 0 ||
      object.symbols[1].placement != CTOOL_ELF32_SYMBOL_DEFINED ||
      object.symbols[1].section_file_index != 1u ||
      strcmp(object.symbols[2].name.data, "external") != 0 ||
      object.symbols[2].placement != CTOOL_ELF32_SYMBOL_UNDEFINED ||
      object.relocations[0].target_section_file_index != 1u ||
      object.relocations[0].offset != 1u ||
      object.relocations[0].symbol_file_index != 2u ||
      object.relocations[0].type != CTOOL_ELF32_R_386_PC32 ||
      object.relocations[0].addend_known == CTOOL_FALSE ||
      object.relocations[0].addend != -4 ||
      text_view->relocation_count != 1u) {
    (void)fprintf(stderr, "ELF32 roundtrip view differs\n");
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }
  copy = (ctool_u8 *)malloc((size_t)source.contents.size);
  if (copy == (ctool_u8 *)0) {
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }
  (void)memcpy(copy, source.contents.data, (size_t)source.contents.size);
  section_headers = read_le32(copy, 32u);
  text_header = section_headers + find_section(source.contents, ".text") * 40u;
  rel_header =
      section_headers + find_section(source.contents, ".rel.text") * 40u;
  rel_offset = read_le32(copy, rel_header + 16u);
  raw_info = read_le32(copy, rel_offset + 4u);
  write_le32(copy, text_header + 4u, 0x70000042u);
  write_le32(copy, text_header + 12u, 0x12345678u);
  write_le32(copy, rel_offset + 4u, (raw_info & 0xffffff00u) | 0x7fu);
  source.contents = ctool_bytes(copy, source.contents.size);
  status = ctool_elf32_read(job, &source, &object);
  text_view = find_view_section(&object, ".text");
  if (!check_status(status, CTOOL_OK, "unknown ELF32 records") ||
      text_view == (const ctool_elf32_section_t *)0 ||
      text_view->type != 0x70000042u ||
      text_view->address != 0x12345678u || object.relocation_count != 1u ||
      object.relocations[0].type != 0x7fu ||
      object.relocations[0].addend_known != CTOOL_FALSE ||
      object.relocations[0].addend != 0) {
    (void)fprintf(stderr, "unknown ELF32 record view differs\n");
    free(copy);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }
  free(copy);
  ctool_buffer_close(output);
  ctool_job_close(job);
  (void)puts("reader-roundtrip: ok");
  return 0;
}

static int run_reader_exec(void) {
  ctool_u8 image[600];
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_source_t source;
  ctool_elf32_object_t object;
  const ctool_elf32_program_header_t *load;
  const ctool_elf32_section_t *text;
  const ctool_elf32_symbol_t *entry;
  const ctool_elf32_symbol_t *tls_init;
  const ctool_elf32_symbol_t *tls_zero;
  ctool_u32 image_size;
  ctool_u32 section_headers;
  ctool_u32 symtab_header;
  ctool_u32 symtab_offset;
  ctool_u32 entry_offset;
  ctool_u32 index;
  ctool_status_t status;
  if (!open_job(&adapter, &config, &job)) {
    return 1;
  }
  image_size = build_exec_fixture(image, (ctool_u32)sizeof(image), CTOOL_TRUE);
  source.path.text = ctool_string("/kernel.elf");
  source.contents = ctool_bytes(image, image_size);
  status = ctool_elf32_read(job, &source, &object);
  load = object.program_header_count == 1u ? &object.program_headers[0]
                                           : (const ctool_elf32_program_header_t *)0;
  text = find_view_section(&object, ".text");
  entry = find_view_symbol(&object, "entry");
  if (!check_status(status, CTOOL_OK, "read ELF32 executable") ||
      object.file_type != CTOOL_ELF32_ET_EXEC ||
      object.entry_point != 0x00100000u || object.flags != 0x12345678u ||
      load == (const ctool_elf32_program_header_t *)0 ||
      load->file_index != 0u || load->type != CTOOL_ELF32_PT_LOAD ||
      load->file_offset != 84u || load->virtual_address != 0x00100000u ||
      load->physical_address != 0x00100000u || load->file_size != 4u ||
      load->memory_size != 8u ||
      load->flags != (CTOOL_ELF32_PF_R | CTOOL_ELF32_PF_X) ||
      load->alignment != 4u || load->contents.data != image + 84u ||
      load->contents.size != 4u || load->contents.data[3] != 0xc3u ||
      object.section_count != 5u || text == (const ctool_elf32_section_t *)0 ||
      text->address != 0x00100000u || text->contents.size != 4u ||
      object.symbol_count != 2u || entry == (const ctool_elf32_symbol_t *)0 ||
      entry->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
      entry->section_file_index != 1u || entry->value != 0x00100000u ||
      entry->size != 4u) {
    (void)fprintf(stderr, "ELF32 executable view differs\n");
    ctool_job_close(job);
    return 1;
  }

  section_headers = read_le32(image, 32u);
  symtab_header =
      section_headers + find_section(ctool_bytes(image, image_size),
                                     ".symtab") * 40u;
  symtab_offset = read_le32(image, symtab_header + 16u);
  entry_offset = symtab_offset + 16u;
  write_le32(image, entry_offset + 4u, 0x00100008u);
  write_le32(image, entry_offset + 8u, 0u);
  image[entry_offset + 12u] = 0x10u;
  status = ctool_elf32_read(job, &source, &object);
  entry = find_view_symbol(&object, "entry");
  if (!check_status(status, CTOOL_OK, "read executable linker boundary") ||
      entry == (const ctool_elf32_symbol_t *)0 ||
      entry->type != CTOOL_ELF32_SYMBOL_NOTYPE || entry->size != 0u ||
      entry->value != 0x00100008u ||
      entry->section_file_index != 1u) {
    (void)fprintf(stderr, "ELF32 executable linker boundary differs\n");
    ctool_job_close(job);
    return 1;
  }

  image_size = build_exec_fixture(image, (ctool_u32)sizeof(image), CTOOL_TRUE);
  section_headers = read_le32(image, 32u);
  write_le32(image, section_headers + 40u + 8u,
             CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE |
                 CTOOL_ELF32_SHF_TLS);
  symtab_header =
      section_headers + find_section(ctool_bytes(image, image_size),
                                     ".symtab") * 40u;
  symtab_offset = read_le32(image, symtab_header + 16u);
  entry_offset = symtab_offset + 16u;
  write_le32(image, entry_offset + 4u, 0u);
  image[entry_offset + 12u] = 0x16u;
  source.path.text = ctool_string("/tls-symbol.elf");
  source.contents = ctool_bytes(image, image_size);
  status = ctool_elf32_read(job, &source, &object);
  entry = find_view_symbol(&object, "entry");
  if (!check_status(status, CTOOL_OK, "read executable TLS symbol") ||
      entry == (const ctool_elf32_symbol_t *)0 ||
      entry->type != CTOOL_ELF32_SYMBOL_TLS || entry->value != 0u ||
      entry->size != 4u || entry->section_file_index != 1u) {
    (void)fprintf(stderr, "ELF32 executable TLS symbol differs\n");
    ctool_job_close(job);
    return 1;
  }
  write_le32(image, entry_offset + 4u, 5u);
  if (!expect_reader_failure(job, image, image_size, CTOOL_ERR_INPUT,
                             CTOOL_ELF32_DIAG_BAD_SYMBOL,
                             "out-of-range executable TLS symbol")) {
    ctool_job_close(job);
    return 1;
  }
  write_le32(image, entry_offset + 4u, 0u);
  write_le16(image, entry_offset + 14u, 0xfff1u);
  if (!expect_reader_failure(job, image, image_size, CTOOL_ERR_INPUT,
                             CTOOL_ELF32_DIAG_BAD_SYMBOL,
                             "absolute executable TLS symbol")) {
    ctool_job_close(job);
    return 1;
  }

  image_size = build_exec_fixture(image, (ctool_u32)sizeof(image), CTOOL_TRUE);
  section_headers = read_le32(image, 32u);
  write_le16(image, 50u, 0u);
  for (index = 0u; index < 5u; index++) {
    write_le32(image, section_headers + index * 40u, 0u);
  }
  source.path.text = ctool_string("/unnamed-sections.elf");
  source.contents = ctool_bytes(image, image_size);
  status = ctool_elf32_read(job, &source, &object);
  entry = find_view_symbol(&object, "entry");
  if (!check_status(status, CTOOL_OK, "read unnamed ELF32 sections") ||
      object.section_count != 5u ||
      object.sections[1].name.size != 0u ||
      entry == (const ctool_elf32_symbol_t *)0 ||
      entry->section_file_index != 1u) {
    (void)fprintf(stderr, "unnamed ELF32 section view differs\n");
    ctool_job_close(job);
    return 1;
  }

  image_size = build_exec_fixture(image, (ctool_u32)sizeof(image), CTOOL_TRUE);
  section_headers = read_le32(image, 32u);
  (void)memmove(image + section_headers + 1u, image + section_headers,
                5u * 40u);
  write_le32(image, 32u, section_headers + 1u);
  image_size++;
  source.path.text = ctool_string("/unaligned-sections.elf");
  source.contents = ctool_bytes(image, image_size);
  status = ctool_elf32_read(job, &source, &object);
  if (!check_status(status, CTOOL_OK, "read unaligned section table") ||
      object.section_count != 5u ||
      find_view_section(&object, ".text") ==
          (const ctool_elf32_section_t *)0) {
    (void)fprintf(stderr, "unaligned section-table view differs\n");
    ctool_job_close(job);
    return 1;
  }

  image_size =
      build_exec_fixture(image, (ctool_u32)sizeof(image), CTOOL_FALSE);
  source.path.text = ctool_string("/sectionless.elf");
  source.contents = ctool_bytes(image, image_size);
  status = ctool_elf32_read(job, &source, &object);
  if (!check_status(status, CTOOL_OK, "read sectionless ELF32 executable") ||
      object.file_type != CTOOL_ELF32_ET_EXEC ||
      object.entry_point != 0x00100000u || object.program_header_count != 1u ||
      object.program_headers[0].contents.data != image + 84u ||
      object.program_headers[0].contents.size != 4u ||
      object.sections != (const ctool_elf32_section_t *)0 ||
      object.section_count != 0u ||
      object.symbols != (const ctool_elf32_symbol_t *)0 ||
      object.symbol_count != 0u ||
      object.relocations != (const ctool_elf32_relocation_t *)0 ||
      object.relocation_count != 0u) {
    (void)fprintf(stderr, "sectionless ELF32 executable view differs\n");
    ctool_job_close(job);
    return 1;
  }
  (void)memmove(image + 53u, image + 52u, 36u);
  write_le32(image, 28u, 53u);
  write_le32(image, 57u, 85u);
  write_le32(image, 81u, 1u);
  image_size++;
  source.path.text = ctool_string("/unaligned-programs.elf");
  source.contents = ctool_bytes(image, image_size);
  status = ctool_elf32_read(job, &source, &object);
  if (!check_status(status, CTOOL_OK, "read unaligned program table") ||
      object.program_header_count != 1u ||
      object.program_headers[0].file_offset != 85u ||
      object.program_headers[0].contents.data != image + 85u ||
      object.program_headers[0].contents.size != 4u ||
      object.program_headers[0].contents.data[3] != 0xc3u) {
    (void)fprintf(stderr, "unaligned program-table view differs\n");
    ctool_job_close(job);
    return 1;
  }
  image_size =
      build_exec_fixture(image, (ctool_u32)sizeof(image), CTOOL_FALSE);
  write_le32(image, 28u, 0u);
  write_le16(image, 44u, 0u);
  write_le16(image, 46u, 40u);
  source.path.text = ctool_string("/empty-tables.elf");
  source.contents = ctool_bytes(image, image_size);
  status = ctool_elf32_read(job, &source, &object);
  if (!check_status(status, CTOOL_OK, "read zero-count ELF32 tables") ||
      object.program_header_count != 0u || object.section_count != 0u) {
    (void)fprintf(stderr, "zero-count ELF32 table view differs\n");
    ctool_job_close(job);
    return 1;
  }
  image_size = build_tls_exec_fixture(image, (ctool_u32)sizeof(image));
  source.path.text = ctool_string("/multi-section-tls.elf");
  source.contents = ctool_bytes(image, image_size);
  status = ctool_elf32_read(job, &source, &object);
  tls_init = find_view_symbol(&object, "tls_init");
  tls_zero = find_view_symbol(&object, "tls_zero");
  if (!check_status(status, CTOOL_OK, "read multi-section TLS layout") ||
      object.program_header_count != 2u ||
      object.program_headers[1].type != CTOOL_ELF32_PT_TLS ||
      object.program_headers[1].memory_size != 8u ||
      tls_init == (const ctool_elf32_symbol_t *)0 ||
      tls_init->type != CTOOL_ELF32_SYMBOL_TLS || tls_init->value != 0u ||
      tls_init->section_file_index != 2u ||
      tls_zero == (const ctool_elf32_symbol_t *)0 ||
      tls_zero->type != CTOOL_ELF32_SYMBOL_TLS || tls_zero->value != 4u ||
      tls_zero->section_file_index != 3u) {
    (void)fprintf(stderr, "multi-section ELF32 TLS view differs\n");
    ctool_job_close(job);
    return 1;
  }
  write_le32(image, 160u, 8u);
  if (!expect_reader_failure(job, image, image_size, CTOOL_ERR_INPUT,
                             CTOOL_ELF32_DIAG_BAD_SYMBOL,
                             "out-of-range multi-section TLS symbol")) {
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)puts("reader-exec: ok");
  return 0;
}

static int run_reader_exec_malformed(void) {
  ctool_u8 image[364];
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_u32 image_size;
  if (!open_job(&adapter, &config, &job)) {
    return 1;
  }
  image_size =
      build_exec_fixture(image, (ctool_u32)sizeof(image), CTOOL_FALSE);
  write_le32(image, 28u, image_size - 16u);
  if (!expect_reader_failure(job, image, image_size, CTOOL_ERR_INPUT,
                             CTOOL_ELF32_DIAG_BAD_HEADER,
                             "truncated program-header table")) {
    ctool_job_close(job);
    return 1;
  }
  image_size =
      build_exec_fixture(image, (ctool_u32)sizeof(image), CTOOL_FALSE);
  write_le32(image, 68u, 9u);
  if (!expect_reader_failure(job, image, image_size, CTOOL_ERR_INPUT,
                             CTOOL_ELF32_DIAG_BAD_PROGRAM_HEADER,
                             "program contents out of range")) {
    ctool_job_close(job);
    return 1;
  }
  image_size =
      build_exec_fixture(image, (ctool_u32)sizeof(image), CTOOL_FALSE);
  write_le32(image, 72u, 3u);
  if (!expect_reader_failure(job, image, image_size, CTOOL_ERR_INPUT,
                             CTOOL_ELF32_DIAG_BAD_PROGRAM_HEADER,
                             "load file size exceeds memory size")) {
    ctool_job_close(job);
    return 1;
  }
  image_size =
      build_exec_fixture(image, (ctool_u32)sizeof(image), CTOOL_FALSE);
  write_le32(image, 80u, 3u);
  if (!expect_reader_failure(job, image, image_size, CTOOL_ERR_INPUT,
                             CTOOL_ELF32_DIAG_BAD_PROGRAM_HEADER,
                             "invalid program alignment")) {
    ctool_job_close(job);
    return 1;
  }
  image_size =
      build_exec_fixture(image, (ctool_u32)sizeof(image), CTOOL_FALSE);
  write_le32(image, 60u, 0xfffffffeu);
  write_le32(image, 80u, 1u);
  if (!expect_reader_failure(job, image, image_size, CTOOL_ERR_INPUT,
                             CTOOL_ELF32_DIAG_BAD_PROGRAM_HEADER,
                             "overflowing load address")) {
    ctool_job_close(job);
    return 1;
  }
  image_size =
      build_exec_fixture(image, (ctool_u32)sizeof(image), CTOOL_TRUE);
  write_le32(image, read_le32(image, 32u) + 40u + 12u, 0xfffffffeu);
  if (!expect_reader_failure(job, image, image_size, CTOOL_ERR_INPUT,
                             CTOOL_ELF32_DIAG_BAD_SECTION,
                             "overflowing executable section address")) {
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)puts("reader-exec-malformed: ok");
  return 0;
}

static int run_reader_string_offsets(void) {
  const ctool_u32 symbol_count = 512u;
  const ctool_u32 name_size = 32768u;
  const ctool_u32 name_step = name_size / symbol_count;
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_buffer_t *output;
  ctool_elf32_symbol_spec_t *symbols;
  ctool_elf32_object_spec_t spec;
  ctool_elf32_object_t object;
  ctool_source_t source;
  ctool_bytes_t image;
  ctool_u8 *name;
  ctool_u8 *copy;
  ctool_u32 section_headers;
  ctool_u32 symtab_header;
  ctool_u32 symtab_offset;
  ctool_u32 index;
  ctool_status_t status;
  symbols = (ctool_elf32_symbol_spec_t *)calloc(
      (size_t)symbol_count, sizeof(ctool_elf32_symbol_spec_t));
  name = (ctool_u8 *)malloc((size_t)name_size);
  if (symbols == (ctool_elf32_symbol_spec_t *)0 || name == (ctool_u8 *)0) {
    free(name);
    free(symbols);
    return 1;
  }
  (void)memset(name, (int)'x', (size_t)name_size);
  for (index = 0u; index < symbol_count; index++) {
    symbols[index].name.data = (const char *)name;
    symbols[index].name.size = name_size;
    symbols[index].binding = CTOOL_ELF32_BIND_GLOBAL;
    symbols[index].type = CTOOL_ELF32_SYMBOL_NOTYPE;
    symbols[index].visibility = CTOOL_ELF32_VIS_DEFAULT;
    symbols[index].placement = CTOOL_ELF32_SYMBOL_UNDEFINED;
    symbols[index].section = CTOOL_ELF32_NO_SECTION;
    symbols[index].value = 0u;
    symbols[index].size = 0u;
    symbols[index].alignment = 0u;
  }
  if (!open_job(&adapter, &config, &job)) {
    free(name);
    free(symbols);
    return 1;
  }
  status =
      ctool_job_open_buffer(job, 256u, config.limits.output_bytes, &output);
  if (!check_status(status, CTOOL_OK, "string-offset output buffer")) {
    ctool_job_close(job);
    free(name);
    free(symbols);
    return 1;
  }
  spec.sections = (const ctool_elf32_section_spec_t *)0;
  spec.section_count = 0u;
  spec.symbols = symbols;
  spec.symbol_count = symbol_count;
  spec.relocations = (const ctool_elf32_relocation_spec_t *)0;
  spec.relocation_count = 0u;
  status = ctool_elf32_write(job, &spec, output);
  image = ctool_buffer_view(output);
  copy = (ctool_u8 *)malloc((size_t)image.size);
  if (!check_status(status, CTOOL_OK, "string-offset writer") ||
      copy == (ctool_u8 *)0) {
    free(copy);
    ctool_buffer_close(output);
    ctool_job_close(job);
    free(name);
    free(symbols);
    return 1;
  }
  (void)memcpy(copy, image.data, (size_t)image.size);
  section_headers = read_le32(copy, 32u);
  symtab_header = section_headers + find_section(image, ".symtab") * 40u;
  symtab_offset = read_le32(copy, symtab_header + 16u);
  for (index = 0u; index < symbol_count; index++) {
    ctool_u32 name_offset =
        1u + (symbol_count - 1u - index) * name_step;
    write_le32(copy, symtab_offset + (index + 1u) * 16u, name_offset);
  }
  source.path.text = ctool_string("/string-offsets.o");
  source.contents = ctool_bytes(copy, image.size);
  status = ctool_elf32_read(job, &source, &object);
  if (!check_status(status, CTOOL_OK, "descending string offsets") ||
      object.symbol_count != symbol_count + 1u ||
      object.symbols[1].name.size != name_step ||
      object.symbols[symbol_count].name.size != name_size) {
    (void)fprintf(stderr, "descending string-offset view differs\n");
    free(copy);
    ctool_buffer_close(output);
    ctool_job_close(job);
    free(name);
    free(symbols);
    return 1;
  }
  free(copy);
  ctool_buffer_close(output);
  ctool_job_close(job);
  free(name);
  free(symbols);
  (void)puts("reader-string-offsets: ok");
  return 0;
}

static int expect_reader_failure(ctool_job_t *job, const ctool_u8 *bytes,
                                 ctool_u32 size, ctool_status_t expected_status,
                                 ctool_u32 expected_code,
                                 const char *case_name) {
  ctool_source_t source;
  ctool_elf32_object_t object;
  ctool_u32 before = ctool_job_diagnostic_count(job);
  const ctool_diagnostic_t *diagnostic;
  ctool_status_t status;
  source.path.text = ctool_string("/malformed.o");
  source.contents = ctool_bytes(bytes, size);
  object.image.data = (const ctool_u8 *)bytes;
  object.image.size = 1u;
  object.file_type = CTOOL_ELF32_ET_EXEC;
  object.entry_point = 1u;
  object.flags = 1u;
  object.program_headers = (const ctool_elf32_program_header_t *)bytes;
  object.program_header_count = 1u;
  object.section_count = 1u;
  object.symbol_count = 1u;
  object.relocation_count = 1u;
  status = ctool_elf32_read(job, &source, &object);
  diagnostic = ctool_job_diagnostic(job, before);
  if (status != expected_status ||
      ctool_job_diagnostic_count(job) != before + 1u ||
      diagnostic == (const ctool_diagnostic_t *)0 ||
      diagnostic->code != expected_code || diagnostic->message.size == 0u ||
      object.image.data != (const ctool_u8 *)0 || object.image.size != 0u ||
      (ctool_u32)object.file_type != 0u || object.entry_point != 0u ||
      object.flags != 0u ||
      object.program_headers != (const ctool_elf32_program_header_t *)0 ||
      object.program_header_count != 0u ||
      object.sections != (const ctool_elf32_section_t *)0 ||
      object.section_count != 0u ||
      object.symbols != (const ctool_elf32_symbol_t *)0 ||
      object.symbol_count != 0u ||
      object.relocations != (const ctool_elf32_relocation_t *)0 ||
      object.relocation_count != 0u) {
    (void)fprintf(stderr,
                  "%s: status, diagnostic, or failure atomicity differs\n",
                  case_name);
    return 0;
  }
  return 1;
}

static int run_reader_malformed(void) {
  static const ctool_u8 text[] = {0xe8u, 0u, 0u, 0u, 0u, 0xe8u,
                                  0u,    0u, 0u, 0u, 0xc3u};
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_buffer_t *output;
  ctool_elf32_section_spec_t section;
  ctool_elf32_symbol_spec_t symbols[2];
  ctool_elf32_relocation_spec_t relocations[2];
  ctool_elf32_object_spec_t spec;
  ctool_source_t invalid_source;
  ctool_elf32_object_t invalid_object;
  ctool_bytes_t valid;
  ctool_u8 *copy;
  ctool_u32 section_headers;
  ctool_u32 text_header;
  ctool_u32 rel_header;
  ctool_u32 symtab_header;
  ctool_u32 strtab_header;
  ctool_u32 symtab_offset;
  ctool_u32 rel_offset;
  ctool_u32 truncation;
  ctool_status_t status;
  if (!open_job(&adapter, &config, &job)) {
    return 1;
  }
  status =
      ctool_job_open_buffer(job, 256u, config.limits.output_bytes, &output);
  if (!check_status(status, CTOOL_OK, "malformed fixture buffer")) {
    ctool_job_close(job);
    return 1;
  }
  invalid_source.path.text = ctool_string("/invalid.o");
  invalid_source.contents = ctool_bytes((const void *)0, 1u);
  invalid_object.image = ctool_bytes(text, (ctool_u32)sizeof(text));
  invalid_object.file_type = CTOOL_ELF32_ET_EXEC;
  invalid_object.entry_point = 1u;
  invalid_object.flags = 1u;
  invalid_object.program_headers =
      (const ctool_elf32_program_header_t *)(const void *)text;
  invalid_object.program_header_count = 1u;
  invalid_object.sections = (const ctool_elf32_section_t *)&section;
  invalid_object.section_count = 1u;
  invalid_object.symbols = (const ctool_elf32_symbol_t *)symbols;
  invalid_object.symbol_count = 1u;
  invalid_object.relocations = (const ctool_elf32_relocation_t *)relocations;
  invalid_object.relocation_count = 1u;
  invalid_object.symbol_table_section_file_index = 1u;
  status = ctool_elf32_read(job, &invalid_source, &invalid_object);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "invalid reader source") ||
      invalid_object.image.data != (const ctool_u8 *)0 ||
      invalid_object.image.size != 0u ||
      (ctool_u32)invalid_object.file_type != 0u ||
      invalid_object.entry_point != 0u || invalid_object.flags != 0u ||
      invalid_object.program_headers !=
          (const ctool_elf32_program_header_t *)0 ||
      invalid_object.program_header_count != 0u ||
      invalid_object.sections != (const ctool_elf32_section_t *)0 ||
      invalid_object.section_count != 0u ||
      invalid_object.symbols != (const ctool_elf32_symbol_t *)0 ||
      invalid_object.symbol_count != 0u ||
      invalid_object.relocations != (const ctool_elf32_relocation_t *)0 ||
      invalid_object.relocation_count != 0u ||
      invalid_object.symbol_table_section_file_index != 0u) {
    (void)fprintf(stderr, "invalid reader output was not cleared\n");
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }
  invalid_source.path.text.data = (const char *)0;
  invalid_source.path.text.size = 1u;
  invalid_source.contents = ctool_bytes(text, (ctool_u32)sizeof(text));
  status = ctool_elf32_read(job, &invalid_source, &invalid_object);
  if (!check_status(status, CTOOL_ERR_INVALID_ARGUMENT,
                    "invalid reader path") ||
      ctool_job_diagnostic_count(job) != 0u) {
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }
  invalid_source.path.text = ctool_string("/invalid.o");
  section.name = ctool_string(".text");
  section.type = CTOOL_ELF32_SHT_PROGBITS;
  section.flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR;
  section.alignment = 16u;
  section.entry_size = 0u;
  section.size = (ctool_u32)sizeof(text);
  section.contents = ctool_bytes(text, (ctool_u32)sizeof(text));
  symbols[0].name = ctool_string("entry");
  symbols[0].binding = CTOOL_ELF32_BIND_GLOBAL;
  symbols[0].type = CTOOL_ELF32_SYMBOL_FUNCTION;
  symbols[0].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[0].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  symbols[0].section = 0u;
  symbols[0].value = 0u;
  symbols[0].size = (ctool_u32)sizeof(text);
  symbols[0].alignment = 0u;
  symbols[1].name = ctool_string("external");
  symbols[1].binding = CTOOL_ELF32_BIND_GLOBAL;
  symbols[1].type = CTOOL_ELF32_SYMBOL_NOTYPE;
  symbols[1].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[1].placement = CTOOL_ELF32_SYMBOL_UNDEFINED;
  symbols[1].section = CTOOL_ELF32_NO_SECTION;
  symbols[1].value = 0u;
  symbols[1].size = 0u;
  symbols[1].alignment = 0u;
  relocations[0].target_section = 0u;
  relocations[0].offset = 1u;
  relocations[0].symbol = 1u;
  relocations[0].type = CTOOL_ELF32_R_386_PC32;
  relocations[0].addend = -4;
  relocations[1] = relocations[0];
  relocations[1].offset = 6u;
  spec.sections = &section;
  spec.section_count = 1u;
  spec.symbols = symbols;
  spec.symbol_count = 2u;
  spec.relocations = relocations;
  spec.relocation_count = 2u;
  status = ctool_elf32_write(job, &spec, output);
  valid = ctool_buffer_view(output);
  copy = (ctool_u8 *)malloc((size_t)valid.size);
  if (!check_status(status, CTOOL_OK, "malformed source write") || !copy) {
    free(copy);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 1;
  }
  section_headers = read_le32(valid.data, 32u);
  text_header = section_headers + find_section(valid, ".text") * 40u;
  rel_header = section_headers + find_section(valid, ".rel.text") * 40u;
  symtab_header = section_headers + find_section(valid, ".symtab") * 40u;
  strtab_header = section_headers + find_section(valid, ".strtab") * 40u;
  symtab_offset = read_le32(valid.data, symtab_header + 16u);
  rel_offset = read_le32(valid.data, rel_header + 16u);

  for (truncation = 0u; truncation < 52u; truncation++) {
    if (!expect_reader_failure(job, valid.data, truncation, CTOOL_ERR_INPUT,
                               CTOOL_ELF32_DIAG_BAD_HEADER,
                               "truncated ELF32 header")) {
      free(copy);
      ctool_buffer_close(output);
      ctool_job_close(job);
      return 1;
    }
  }
  (void)memcpy(copy, valid.data, (size_t)valid.size);
  copy[0] = 0u;
  if (!expect_reader_failure(job, copy, valid.size, CTOOL_ERR_INPUT,
                             CTOOL_ELF32_DIAG_BAD_MAGIC, "bad ELF32 magic")) {
    goto malformed_fail;
  }
  (void)memcpy(copy, valid.data, (size_t)valid.size);
  copy[4] = 2u;
  if (!expect_reader_failure(job, copy, valid.size, CTOOL_ERR_UNSUPPORTED,
                             CTOOL_ELF32_DIAG_UNSUPPORTED_DOMAIN,
                             "unsupported ELF class")) {
    goto malformed_fail;
  }
  (void)memcpy(copy, valid.data, (size_t)valid.size);
  write_le32(copy, 32u, 0xfffffffcu);
  if (!expect_reader_failure(job, copy, valid.size, CTOOL_ERR_INPUT,
                             CTOOL_ELF32_DIAG_BAD_HEADER,
                             "section table overflow")) {
    goto malformed_fail;
  }
  (void)memcpy(copy, valid.data, (size_t)valid.size);
  write_le32(copy, text_header + 32u, 3u);
  if (!expect_reader_failure(job, copy, valid.size, CTOOL_ERR_INPUT,
                             CTOOL_ELF32_DIAG_BAD_SECTION,
                             "invalid section alignment")) {
    goto malformed_fail;
  }
  (void)memcpy(copy, valid.data, (size_t)valid.size);
  write_le32(copy, text_header, 0xffffffffu);
  if (!expect_reader_failure(job, copy, valid.size, CTOOL_ERR_INPUT,
                             CTOOL_ELF32_DIAG_BAD_STRING,
                             "invalid section name")) {
    goto malformed_fail;
  }
  (void)memcpy(copy, valid.data, (size_t)valid.size);
  write_le32(copy, symtab_header + 4u, 11u);
  if (!expect_reader_failure(job, copy, valid.size, CTOOL_ERR_UNSUPPORTED,
                             CTOOL_ELF32_DIAG_UNSUPPORTED_FEATURE,
                             "dynamic symbol table")) {
    goto malformed_fail;
  }
  (void)memcpy(copy, valid.data, (size_t)valid.size);
  write_le32(copy, symtab_header + 36u, 15u);
  if (!expect_reader_failure(job, copy, valid.size, CTOOL_ERR_INPUT,
                             CTOOL_ELF32_DIAG_BAD_SYMBOL,
                             "invalid symbol entry size")) {
    goto malformed_fail;
  }
  (void)memcpy(copy, valid.data, (size_t)valid.size);
  write_le32(copy, strtab_header + 36u, 1u);
  if (!expect_reader_failure(job, copy, valid.size, CTOOL_ERR_INPUT,
                             CTOOL_ELF32_DIAG_BAD_SYMBOL,
                             "invalid string table metadata")) {
    goto malformed_fail;
  }
  (void)memcpy(copy, valid.data, (size_t)valid.size);
  write_le32(copy, symtab_header + 28u, 2u);
  if (!expect_reader_failure(job, copy, valid.size, CTOOL_ERR_INPUT,
                             CTOOL_ELF32_DIAG_BAD_SYMBOL,
                             "invalid local symbol partition")) {
    goto malformed_fail;
  }
  (void)memcpy(copy, valid.data, (size_t)valid.size);
  write_le32(copy, symtab_offset + 16u, 0xffffffffu);
  if (!expect_reader_failure(job, copy, valid.size, CTOOL_ERR_INPUT,
                             CTOOL_ELF32_DIAG_BAD_SYMBOL,
                             "invalid symbol name")) {
    goto malformed_fail;
  }
  (void)memcpy(copy, valid.data, (size_t)valid.size);
  copy[symtab_offset + 16u + 12u] = 0x13u;
  if (!expect_reader_failure(job, copy, valid.size, CTOOL_ERR_INPUT,
                             CTOOL_ELF32_DIAG_BAD_SYMBOL,
                             "invalid section symbol semantics")) {
    goto malformed_fail;
  }
  (void)memcpy(copy, valid.data, (size_t)valid.size);
  write_le32(copy, symtab_offset + 32u + 4u, 4u);
  write_le32(copy, symtab_offset + 32u + 8u, 4u);
  copy[symtab_offset + 32u + 12u] = 0x12u;
  copy[symtab_offset + 32u + 14u] = 0xf2u;
  copy[symtab_offset + 32u + 15u] = 0xffu;
  if (!expect_reader_failure(job, copy, valid.size, CTOOL_ERR_INPUT,
                             CTOOL_ELF32_DIAG_BAD_SYMBOL,
                             "invalid common function")) {
    goto malformed_fail;
  }
  (void)memcpy(copy, valid.data, (size_t)valid.size);
  write_le32(copy, rel_header + 24u, 0u);
  if (!expect_reader_failure(job, copy, valid.size, CTOOL_ERR_INPUT,
                             CTOOL_ELF32_DIAG_BAD_RELOCATION,
                             "invalid relocation symbol table")) {
    goto malformed_fail;
  }
  (void)memcpy(copy, valid.data, (size_t)valid.size);
  write_le32(copy, rel_offset + 4u, 0xffffff02u);
  if (!expect_reader_failure(job, copy, valid.size, CTOOL_ERR_INPUT,
                             CTOOL_ELF32_DIAG_BAD_RELOCATION,
                             "invalid relocation symbol")) {
    goto malformed_fail;
  }
  (void)memcpy(copy, valid.data, (size_t)valid.size);
  write_le32(copy, rel_offset + 8u, 3u);
  if (!expect_reader_failure(job, copy, valid.size, CTOOL_ERR_INPUT,
                             CTOOL_ELF32_DIAG_BAD_RELOCATION,
                             "overlapping reader relocations")) {
    goto malformed_fail;
  }
  (void)memcpy(copy, valid.data, (size_t)valid.size);
  write_le32(copy, rel_offset, (ctool_u32)sizeof(text));
  if (!expect_reader_failure(job, copy, valid.size, CTOOL_ERR_INPUT,
                             CTOOL_ELF32_DIAG_BAD_RELOCATION,
                             "invalid relocation target")) {
    goto malformed_fail;
  }

  free(copy);
  ctool_buffer_close(output);
  ctool_job_close(job);
  (void)puts("reader-malformed: ok");
  return 0;

malformed_fail:
  free(copy);
  ctool_buffer_close(output);
  ctool_job_close(job);
  return 1;
}

static int run_reader_inspect(const char *root, const char *logical_name) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_source_t source;
  ctool_elf32_object_t object;
  ctool_status_t status;
  if (!open_job_at(root, &adapter, &config, &job)) {
    return 1;
  }
  status = load_elf32_object(job, logical_name, &source, &object);
  if (!check_status(status, CTOOL_OK, "inspect ELF32 object")) {
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  (void)printf("inspect: sections=%u symbols=%u relocations=%u\n",
               object.section_count, object.symbol_count,
               object.relocation_count);
  ctool_job_close(job);
  return 0;
}

static int run_reader_c_oracle(const char *root) {
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_source_t source;
  ctool_elf32_object_t object;
  const ctool_elf32_symbol_t *common;
  const ctool_elf32_symbol_t *external;
  const ctool_elf32_symbol_t *weak;
  const ctool_elf32_symbol_t *use_values;
  ctool_u32 absolute_relocations = 0u;
  ctool_u32 pc_relocations = 0u;
  ctool_u32 index;
  ctool_status_t status;
  if (!open_job_at(root, &adapter, &config, &job)) {
    return 1;
  }
  status = load_elf32_object(job, "/oracle.o", &source, &object);
  if (!check_status(status, CTOOL_OK, "read compiler oracle")) {
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  common = find_view_symbol(&object, "common_value");
  external = find_view_symbol(&object, "external_value");
  weak = find_view_symbol(&object, "weak_function");
  use_values = find_view_symbol(&object, "use_values");
  for (index = 0u; index < object.relocation_count; index++) {
    if (object.relocations[index].type == CTOOL_ELF32_R_386_32) {
      absolute_relocations++;
    } else if (object.relocations[index].type == CTOOL_ELF32_R_386_PC32) {
      pc_relocations++;
    }
  }
  if (common == (const ctool_elf32_symbol_t *)0 ||
      common->placement != CTOOL_ELF32_SYMBOL_COMMON_STORAGE ||
      common->binding != CTOOL_ELF32_BIND_GLOBAL || common->size != 4u ||
      common->alignment != 4u ||
      external == (const ctool_elf32_symbol_t *)0 ||
      external->placement != CTOOL_ELF32_SYMBOL_UNDEFINED ||
      weak == (const ctool_elf32_symbol_t *)0 ||
      weak->binding != CTOOL_ELF32_BIND_WEAK ||
      weak->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
      use_values == (const ctool_elf32_symbol_t *)0 ||
      use_values->binding != CTOOL_ELF32_BIND_GLOBAL ||
      use_values->placement != CTOOL_ELF32_SYMBOL_DEFINED ||
      absolute_relocations < 2u || pc_relocations < 1u) {
    (void)fprintf(stderr, "compiler oracle semantic view differs\n");
    ctool_job_close(job);
    return 1;
  }
  ctool_job_close(job);
  (void)puts("reader-c-oracle: ok");
  return 0;
}

static int run_reader_isr_oracle(const char *root) {
  static const char *expected[] = {"isr_handler", "irq_handler",
                                   "fpu_nm_handler", "fpu_mf_handler",
                                   "fpu_xf_handler"};
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job;
  ctool_source_t source;
  ctool_elf32_object_t object;
  const ctool_elf32_section_t *text_section;
  ctool_u32 index;
  ctool_status_t status;
  if (!open_job_at(root, &adapter, &config, &job)) {
    return 1;
  }
  status = load_elf32_object(job, "/isr.o", &source, &object);
  if (!check_status(status, CTOOL_OK, "read NASM ISR oracle")) {
    (void)ctool_job_render_diagnostics(job);
    ctool_job_close(job);
    return 1;
  }
  text_section = find_view_section(&object, ".text");
  if (text_section == (const ctool_elf32_section_t *)0 ||
      text_section->size != 377u || text_section->alignment != 16u ||
      text_section->relocation_count != 5u || object.relocation_count != 5u) {
    (void)fprintf(stderr, "NASM ISR section view differs\n");
    ctool_job_close(job);
    return 1;
  }
  for (index = 0u; index < 5u; index++) {
    const ctool_elf32_relocation_t *relocation =
        &object.relocations[text_section->relocation_first + index];
    const ctool_elf32_symbol_t *symbol;
    if (relocation->symbol_file_index >= object.symbol_count) {
      (void)fprintf(stderr, "NASM ISR relocation symbol is invalid\n");
      ctool_job_close(job);
      return 1;
    }
    symbol = &object.symbols[relocation->symbol_file_index];
    if (relocation->type != CTOOL_ELF32_R_386_PC32 ||
        relocation->addend_known == CTOOL_FALSE || relocation->addend != -4 ||
        strcmp(symbol->name.data, expected[index]) != 0) {
      (void)fprintf(stderr, "NASM ISR relocation view differs\n");
      ctool_job_close(job);
      return 1;
    }
  }
  ctool_job_close(job);
  (void)puts("reader-isr-oracle: ok");
  return 0;
}

int main(int argc, char **argv) {
  if (argc == 2 && strcmp(argv[1], "writer-basic") == 0) {
    return run_writer_basic();
  }
  if (argc == 2 && strcmp(argv[1], "writer-model") == 0) {
    return run_writer_model((const char *)0);
  }
  if (argc == 3 && strcmp(argv[1], "write-oracle") == 0) {
    return run_writer_model(argv[2]);
  }
  if (argc == 2 && strcmp(argv[1], "writer-errors") == 0) {
    return run_writer_errors();
  }
  if (argc == 2 && strcmp(argv[1], "reader-roundtrip") == 0) {
    return run_reader_roundtrip();
  }
  if (argc == 2 && strcmp(argv[1], "reader-exec") == 0) {
    return run_reader_exec();
  }
  if (argc == 2 && strcmp(argv[1], "reader-exec-malformed") == 0) {
    return run_reader_exec_malformed();
  }
  if (argc == 2 && strcmp(argv[1], "reader-string-offsets") == 0) {
    return run_reader_string_offsets();
  }
  if (argc == 2 && strcmp(argv[1], "reader-malformed") == 0) {
    return run_reader_malformed();
  }
  if (argc == 4 && strcmp(argv[1], "inspect") == 0) {
    return run_reader_inspect(argv[2], argv[3]);
  }
  if (argc == 3 && strcmp(argv[1], "reader-c-oracle") == 0) {
    return run_reader_c_oracle(argv[2]);
  }
  if (argc == 3 && strcmp(argv[1], "reader-isr-oracle") == 0) {
    return run_reader_isr_oracle(argv[2]);
  }
  (void)fprintf(stderr,
                "usage: elf32-contract writer-basic|writer-model|"
                "writer-errors|reader-roundtrip|reader-string-offsets|"
                "reader-exec|reader-exec-malformed|reader-malformed|"
                "write-oracle ROOT|inspect ROOT LOGICAL_PATH|"
                "reader-c-oracle ROOT|reader-isr-oracle ROOT\n");
  return 2;
}
