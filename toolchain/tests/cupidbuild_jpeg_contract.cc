#include "cupidbuild.h"
#include "ctool_host.h"
#include "elf32.h"

#include <stdio.h>
#include <string.h>

enum {
  JPEG_REASON_BYTES = 160,
  JPEG_REJECTION_BYTES = 64,
  JPEG_OBJECT_BYTES = 4096,
  JPEG_SHT_NOTE = 7
};

typedef enum {
  JPEG_REJECT_MISSING_SOI,
  JPEG_REJECT_MALFORMED_MARKER_STREAM,
  JPEG_REJECT_STUFFED_DATA_BEFORE_SCAN,
  JPEG_REJECT_TRAILING_BYTES_AFTER_EOI,
  JPEG_REJECT_STANDALONE_MARKER,
  JPEG_REJECT_TRUNCATED_MARKER_LENGTH,
  JPEG_REJECT_INVALID_MARKER_LENGTH,
  JPEG_REJECT_DUPLICATE_FRAME,
  JPEG_REJECT_TRUNCATED_FRAME,
  JPEG_REJECT_INVALID_FRAME_COMPONENTS,
  JPEG_REJECT_INVALID_FRAME_PRECISION,
  JPEG_REJECT_INVALID_FRAME_SIZE,
  JPEG_REJECT_SCAN_BEFORE_FRAME,
  JPEG_REJECT_TRUNCATED_SCAN,
  JPEG_REJECT_INVALID_SCAN_COMPONENTS,
  JPEG_REJECT_PARTIAL_ENTROPY_MARKER,
  JPEG_REJECT_PROGRESSIVE_FRAME,
  JPEG_REJECT_MISSING_FRAME,
  JPEG_REJECT_UNSUPPORTED_FRAME,
  JPEG_REJECT_MISSING_SCAN,
  JPEG_REJECT_MISSING_EOI,
  JPEG_REJECTION_COUNT
} jpeg_rejection_kind_t;

typedef struct {
  const char *name;
  const char *expected_reason;
  unsigned char bytes[JPEG_REJECTION_BYTES];
  size_t size;
} jpeg_rejection_case_t;

static const unsigned char baseline_jpeg[] = {
    0xffu, 0xd8u, 0xffu, 0xc0u, 0x00u, 0x0bu, 0x08u,
    0x00u, 0x01u, 0x00u, 0x01u, 0x01u, 0x01u, 0x11u,
    0x00u, 0xffu, 0xdau, 0x00u, 0x08u, 0x01u, 0x01u,
    0x00u, 0x00u, 0x3fu, 0x00u, 0xffu, 0xd9u};

/* Keep the first four JPEG bytes unchanged when the writer applies R_386_32. */
static const ctool_i32 jpeg_prefix_relocation_addend = -1056974593;

static ctool_u32 jpeg_read_le16(const unsigned char *bytes) {
  return (ctool_u32)bytes[0] | ((ctool_u32)bytes[1] << 8u);
}

static ctool_u32 jpeg_read_le32(const unsigned char *bytes) {
  return jpeg_read_le16(bytes) | (jpeg_read_le16(bytes + 2u) << 16u);
}

static int expect_valid_jpeg(const char *name, const unsigned char *bytes,
                             size_t size) {
  char reason[JPEG_REASON_BYTES];
  (void)memset(reason, 'x', sizeof(reason));
  if (!cupidbuild_validate_jpeg_bytes(bytes, size, reason,
                                      sizeof(reason))) {
    (void)fprintf(stderr, "%s: rejected valid JPEG: %s\n", name, reason);
    return 0;
  }
  if (reason[0] != '\0') {
    (void)fprintf(stderr, "%s: successful validation left a diagnostic\n",
                  name);
    return 0;
  }
  return 1;
}

static int expect_rejected_jpeg(const char *name,
                                const unsigned char *bytes, size_t size,
                                const char *expected_reason) {
  char reason[JPEG_REASON_BYTES];
  (void)memset(reason, 'x', sizeof(reason));
  if (cupidbuild_validate_jpeg_bytes(bytes, size, reason, sizeof(reason))) {
    (void)fprintf(stderr, "%s: accepted malformed JPEG\n", name);
    return 0;
  }
  if (memchr(reason, '\0', sizeof(reason)) == (void *)0) {
    (void)fprintf(stderr, "%s: diagnostic is not NUL-terminated\n", name);
    return 0;
  }
  if (strcmp(reason, expected_reason) != 0) {
    (void)fprintf(stderr,
                  "%s: diagnostic mismatch\n  expected: %s\n  actual: %s\n",
                  name, expected_reason, reason);
    return 0;
  }
  return 1;
}

static int run_positive_contract(void) {
  unsigned char sof1[sizeof(baseline_jpeg)];
  unsigned char entropy[34];
  int success = 1;

  (void)memcpy(sof1, baseline_jpeg, sizeof(sof1));
  sof1[3] = 0xc1u;

  (void)memcpy(entropy, baseline_jpeg, 25u);
  entropy[25] = 0x12u;
  entropy[26] = 0xffu;
  entropy[27] = 0x00u;
  entropy[28] = 0x34u;
  entropy[29] = 0xffu;
  entropy[30] = 0xd0u;
  entropy[31] = 0x56u;
  (void)memcpy(entropy + 32u, baseline_jpeg + 25u, 2u);

  success &= expect_valid_jpeg("baseline SOF0", baseline_jpeg,
                               sizeof(baseline_jpeg));
  success &= expect_valid_jpeg("extended sequential SOF1", sof1,
                               sizeof(sof1));
  success &= expect_valid_jpeg("stuffed entropy and restart marker", entropy,
                               sizeof(entropy));
  return success;
}

typedef struct {
  const char *name;
  const char *section_name;
  ctool_u32 section_flags;
  const unsigned char *object_payload;
  size_t object_payload_size;
  const char *start_name;
  const char *end_name;
  const char *size_name;
  ctool_u32 end_value;
  ctool_u32 size_value;
  ctool_u32 extra_allocated_section_type;
  int add_relocation;
  int add_extra_symbol;
  int expected;
  ctool_u32 start_value;
} jpeg_object_case_t;

static int expect_jpeg_object_validation(const jpeg_object_case_t *test_case) {
  static const char identity[] = "asset.jpg";
  static const unsigned char extra_payload[] = {0x5au};
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *output = (ctool_buffer_t *)0;
  ctool_elf32_section_spec_t sections[2];
  ctool_elf32_symbol_spec_t symbols[4];
  ctool_elf32_relocation_spec_t relocation;
  ctool_elf32_object_spec_t object;
  ctool_elf32_object_t mutated_object;
  ctool_source_t mutated_source;
  ctool_bytes_t encoded;
  unsigned char mutated_bytes[JPEG_OBJECT_BYTES];
  const unsigned char *validation_bytes;
  ctool_u32 section_table_offset;
  ctool_u32 section_header_size;
  ctool_u32 section_count;
  ctool_u32 extra_header_offset;
  ctool_u32 index;
  int found_extra_type = 0;
  ctool_status_t status;
  int actual;

  if (ctool_host_adapter_init(&adapter, ".") != CTOOL_OK) {
    (void)fprintf(stderr, "%s: host adapter setup failed\n", test_case->name);
    return 0;
  }
  config = ctool_host_job_config(&adapter, ctool_default_limits());
  if (ctool_job_open(&config, &job) != CTOOL_OK ||
      ctool_job_open_buffer(job, 64u, config.limits.output_bytes, &output) !=
          CTOOL_OK) {
    (void)fprintf(stderr, "%s: object writer setup failed\n",
                  test_case->name);
    if (job != (ctool_job_t *)0) {
      ctool_job_close(job);
    }
    return 0;
  }

  (void)memset(sections, 0, sizeof(sections));
  (void)memset(symbols, 0, sizeof(symbols));
  (void)memset(&relocation, 0, sizeof(relocation));
  (void)memset(&object, 0, sizeof(object));
  sections[0].name = ctool_string(test_case->section_name);
  sections[0].type = CTOOL_ELF32_SHT_PROGBITS;
  sections[0].flags = test_case->section_flags;
  sections[0].alignment = 1u;
  sections[0].size = (ctool_u32)test_case->object_payload_size;
  sections[0].contents = ctool_bytes(
      test_case->object_payload, (ctool_u32)test_case->object_payload_size);
  sections[1].name = ctool_string(".extra");
  sections[1].type = CTOOL_ELF32_SHT_PROGBITS;
  sections[1].flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE;
  sections[1].alignment = 1u;
  sections[1].size = (ctool_u32)sizeof(extra_payload);
  sections[1].contents =
      ctool_bytes(extra_payload, (ctool_u32)sizeof(extra_payload));

  symbols[0].name = ctool_string(test_case->start_name);
  symbols[0].binding = CTOOL_ELF32_BIND_GLOBAL;
  symbols[0].type = CTOOL_ELF32_SYMBOL_NOTYPE;
  symbols[0].visibility = CTOOL_ELF32_VIS_DEFAULT;
  symbols[0].placement = CTOOL_ELF32_SYMBOL_DEFINED;
  symbols[0].section = 0u;
  symbols[0].value = test_case->start_value;
  symbols[1] = symbols[0];
  symbols[1].name = ctool_string(test_case->end_name);
  symbols[1].value = test_case->end_value;
  symbols[2] = symbols[0];
  symbols[2].name = ctool_string(test_case->size_name);
  symbols[2].placement = CTOOL_ELF32_SYMBOL_ABSOLUTE;
  symbols[2].section = CTOOL_ELF32_NO_SECTION;
  symbols[2].value = test_case->size_value;
  symbols[3] = symbols[2];
  symbols[3].name = ctool_string("unexpected_export");
  symbols[3].value = 1u;

  relocation.target_section = 0u;
  relocation.offset = 0u;
  relocation.symbol = 0u;
  relocation.type = CTOOL_ELF32_R_386_32;
  relocation.addend = jpeg_prefix_relocation_addend;
  object.sections = sections;
  object.section_count =
      test_case->extra_allocated_section_type != 0u ? 2u : 1u;
  object.symbols = symbols;
  object.symbol_count = test_case->add_extra_symbol != 0 ? 4u : 3u;
  object.relocations = &relocation;
  object.relocation_count = test_case->add_relocation != 0 ? 1u : 0u;
  status = ctool_elf32_write(job, &object, output);
  encoded = ctool_buffer_view(output);
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "%s: ELF fixture writer rejected the case\n",
                  test_case->name);
    ctool_buffer_close(output);
    ctool_job_close(job);
    return 0;
  }
  validation_bytes = encoded.data;
  if (test_case->extra_allocated_section_type != 0u &&
      test_case->extra_allocated_section_type != CTOOL_ELF32_SHT_PROGBITS) {
    if (encoded.size > (ctool_u32)sizeof(mutated_bytes) ||
        encoded.size < 52u) {
      (void)fprintf(stderr, "%s: ELF fixture is outside mutation bounds\n",
                    test_case->name);
      ctool_buffer_close(output);
      ctool_job_close(job);
      return 0;
    }
    (void)memcpy(mutated_bytes, encoded.data, (size_t)encoded.size);
    section_table_offset = jpeg_read_le32(mutated_bytes + 32u);
    section_header_size = jpeg_read_le16(mutated_bytes + 46u);
    section_count = jpeg_read_le16(mutated_bytes + 48u);
    if (section_header_size < 40u || section_count <= 2u ||
        section_table_offset > encoded.size ||
        section_header_size > encoded.size - section_table_offset ||
        section_header_size >
            (encoded.size - section_table_offset) / 2u) {
      (void)fprintf(stderr, "%s: ELF section table is outside mutation bounds\n",
                    test_case->name);
      ctool_buffer_close(output);
      ctool_job_close(job);
      return 0;
    }
    extra_header_offset = section_table_offset + section_header_size * 2u;
    mutated_bytes[extra_header_offset + 4u] =
        (unsigned char)test_case->extra_allocated_section_type;
    mutated_bytes[extra_header_offset + 5u] = 0u;
    mutated_bytes[extra_header_offset + 6u] = 0u;
    mutated_bytes[extra_header_offset + 7u] = 0u;
    mutated_source.path.text = ctool_string("/mutated-candidate.o");
    mutated_source.contents = ctool_bytes(mutated_bytes, encoded.size);
    if (ctool_elf32_read(job, &mutated_source, &mutated_object) != CTOOL_OK) {
      (void)fprintf(stderr, "%s: reader rejected the raw section type\n",
                    test_case->name);
      ctool_buffer_close(output);
      ctool_job_close(job);
      return 0;
    }
    for (index = 0u; index < mutated_object.section_count; index++) {
      const ctool_elf32_section_t *candidate =
          &mutated_object.sections[index];
      if (candidate->type == test_case->extra_allocated_section_type &&
          (candidate->flags & CTOOL_ELF32_SHF_ALLOC) != 0u &&
          candidate->size != 0u) {
        found_extra_type = 1;
      }
    }
    if (found_extra_type == 0) {
      (void)fprintf(stderr, "%s: raw allocated section was not preserved\n",
                    test_case->name);
      ctool_buffer_close(output);
      ctool_job_close(job);
      return 0;
    }
    validation_bytes = mutated_bytes;
  }
  actual = cupidbuild_validate_jpeg_object_bytes(
      validation_bytes, (size_t)encoded.size, baseline_jpeg,
      sizeof(baseline_jpeg), identity);
  if (actual != test_case->expected) {
    (void)fprintf(stderr, "%s: JPEG object validation result differs\n",
                  test_case->name);
  }
  ctool_buffer_close(output);
  ctool_job_close(job);
  return actual == test_case->expected;
}

static int run_object_contract(void) {
  static const char start_name[] = "_binary_asset_jpg_start";
  static const char end_name[] = "_binary_asset_jpg_end";
  static const char size_name[] = "_binary_asset_jpg_size";
  unsigned char wrong_payload[sizeof(baseline_jpeg)];
  jpeg_object_case_t cases[] = {
      {"exact wrapped object", ".data",
       CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE, baseline_jpeg,
       sizeof(baseline_jpeg), start_name, end_name, size_name,
       (ctool_u32)sizeof(baseline_jpeg), (ctool_u32)sizeof(baseline_jpeg), 0,
       0, 0, 1, 0u},
      {"wrong wrapped payload", ".data",
       CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE, wrong_payload,
       sizeof(wrong_payload), start_name, end_name, size_name,
       (ctool_u32)sizeof(wrong_payload), (ctool_u32)sizeof(wrong_payload), 0,
       0, 0, 0, 0u},
      {"code-only object", ".text",
       CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR, baseline_jpeg,
       sizeof(baseline_jpeg), start_name, end_name, size_name,
       (ctool_u32)sizeof(baseline_jpeg), (ctool_u32)sizeof(baseline_jpeg), 0,
       0, 0, 0, 0u},
      {"wrong start identity", ".data",
       CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE, baseline_jpeg,
       sizeof(baseline_jpeg), "wrong_start", end_name, size_name,
       (ctool_u32)sizeof(baseline_jpeg), (ctool_u32)sizeof(baseline_jpeg), 0,
       0, 0, 0, 0u},
      {"displaced start symbol", ".data",
       CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE, baseline_jpeg,
       sizeof(baseline_jpeg), start_name, end_name, size_name,
       (ctool_u32)sizeof(baseline_jpeg), (ctool_u32)sizeof(baseline_jpeg), 0,
       0, 0, 0, 1u},
      {"wrong end identity", ".data",
       CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE, baseline_jpeg,
       sizeof(baseline_jpeg), start_name, "wrong_end", size_name,
       (ctool_u32)sizeof(baseline_jpeg), (ctool_u32)sizeof(baseline_jpeg), 0,
       0, 0, 0, 0u},
      {"wrong size identity", ".data",
       CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE, baseline_jpeg,
       sizeof(baseline_jpeg), start_name, end_name, "wrong_size",
       (ctool_u32)sizeof(baseline_jpeg), (ctool_u32)sizeof(baseline_jpeg), 0,
       0, 0, 0, 0u},
      {"displaced end symbol", ".data",
       CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE, baseline_jpeg,
       sizeof(baseline_jpeg), start_name, end_name, size_name,
       (ctool_u32)sizeof(baseline_jpeg) - 1u,
       (ctool_u32)sizeof(baseline_jpeg), 0, 0, 0, 0, 0u},
      {"wrong size value", ".data",
       CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE, baseline_jpeg,
       sizeof(baseline_jpeg), start_name, end_name, size_name,
       (ctool_u32)sizeof(baseline_jpeg),
       (ctool_u32)sizeof(baseline_jpeg) - 1u, 0, 0, 0, 0, 0u},
      {"relocated payload", ".data",
       CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE, baseline_jpeg,
       sizeof(baseline_jpeg), start_name, end_name, size_name,
       (ctool_u32)sizeof(baseline_jpeg), (ctool_u32)sizeof(baseline_jpeg), 0,
       1, 0, 0, 0u},
      {"extra allocated payload", ".data",
       CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE, baseline_jpeg,
       sizeof(baseline_jpeg), start_name, end_name, size_name,
       (ctool_u32)sizeof(baseline_jpeg), (ctool_u32)sizeof(baseline_jpeg), 1,
       0, 0, 0, 0u},
      {"extra allocated note", ".data",
       CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE, baseline_jpeg,
       sizeof(baseline_jpeg), start_name, end_name, size_name,
       (ctool_u32)sizeof(baseline_jpeg), (ctool_u32)sizeof(baseline_jpeg),
       JPEG_SHT_NOTE, 0, 0, 0, 0u},
      {"extra exported symbol", ".data",
       CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE, baseline_jpeg,
       sizeof(baseline_jpeg), start_name, end_name, size_name,
       (ctool_u32)sizeof(baseline_jpeg), (ctool_u32)sizeof(baseline_jpeg), 0,
       0, 1, 0, 0u},
  };
  size_t index;
  int success = 1;
  (void)memcpy(wrong_payload, baseline_jpeg, sizeof(wrong_payload));
  wrong_payload[6] ^= 1u;

  for (index = 0u; index < sizeof(cases) / sizeof(cases[0]); index++) {
    success &= expect_jpeg_object_validation(&cases[index]);
  }
  return success;
}

static int run_rejection_contract(void) {
  jpeg_rejection_case_t cases[JPEG_REJECTION_COUNT] = {
      {"missing SOI", "JPEG input has no SOI marker", {0u}, 0u},
      {"malformed marker stream",
       "JPEG marker stream is malformed outside a scan", {0u}, 0u},
      {"stuffed data before scan",
       "JPEG marker stream contains stuffed data before a scan", {0u}, 0u},
      {"trailing bytes after EOI",
       "JPEG input has trailing bytes after the EOI marker", {0u}, 0u},
      {"standalone marker", "unexpected standalone JPEG marker 0xd8", {0u},
       0u},
      {"truncated marker length", "JPEG marker length is truncated", {0u},
       0u},
      {"invalid marker length", "JPEG marker length is invalid", {0u}, 0u},
      {"duplicate frame", "JPEG input contains more than one frame header",
       {0u}, 0u},
      {"truncated frame", "JPEG frame header is truncated", {0u}, 0u},
      {"invalid frame components",
       "JPEG frame header has an invalid component table", {0u}, 0u},
      {"invalid frame precision",
       "JPEG frame header has an invalid sample precision", {0u}, 0u},
      {"invalid frame size", "JPEG frame header has an invalid image size",
       {0u}, 0u},
      {"scan before frame", "JPEG scan appears before its frame header", {0u},
       0u},
      {"truncated scan", "JPEG scan header is truncated", {0u}, 0u},
      {"invalid scan components",
       "JPEG scan header has an invalid component table", {0u}, 0u},
      {"partial entropy marker",
       "JPEG entropy data ends with a partial marker", {0u}, 0u},
      {"progressive frame",
       "unsupported progressive JPEG frame; check in a baseline SOF0/SOF1 "
       "asset",
       {0u}, 0u},
      {"missing frame", "JPEG input has no supported SOF0/SOF1 frame", {0u},
       0u},
      {"unsupported frame",
       "unsupported JPEG frame marker 0xc3; check in a baseline SOF0/SOF1 "
       "asset",
       {0u}, 0u},
      {"missing scan", "JPEG input has no scan", {0u}, 0u},
      {"missing EOI", "JPEG input has no EOI marker", {0u}, 0u}};
  size_t index;
  int success = 1;

  for (index = 0u; index < JPEG_REJECTION_COUNT; index++) {
    (void)memcpy(cases[index].bytes, baseline_jpeg, sizeof(baseline_jpeg));
    cases[index].size = sizeof(baseline_jpeg);
  }

  cases[JPEG_REJECT_MISSING_SOI].bytes[0] = 0x00u;
  (void)memcpy(cases[JPEG_REJECT_MALFORMED_MARKER_STREAM].bytes,
               baseline_jpeg, 15u);
  cases[JPEG_REJECT_MALFORMED_MARKER_STREAM].bytes[15] = 0x01u;
  (void)memcpy(cases[JPEG_REJECT_MALFORMED_MARKER_STREAM].bytes + 16u,
               baseline_jpeg + 15u, 12u);
  cases[JPEG_REJECT_MALFORMED_MARKER_STREAM].size = 28u;
  (void)memcpy(cases[JPEG_REJECT_STUFFED_DATA_BEFORE_SCAN].bytes,
               baseline_jpeg, 15u);
  cases[JPEG_REJECT_STUFFED_DATA_BEFORE_SCAN].bytes[15] = 0xffu;
  cases[JPEG_REJECT_STUFFED_DATA_BEFORE_SCAN].bytes[16] = 0x00u;
  (void)memcpy(cases[JPEG_REJECT_STUFFED_DATA_BEFORE_SCAN].bytes + 17u,
               baseline_jpeg + 15u, 12u);
  cases[JPEG_REJECT_STUFFED_DATA_BEFORE_SCAN].size = 29u;
  cases[JPEG_REJECT_TRAILING_BYTES_AFTER_EOI].bytes[27] = 0x00u;
  cases[JPEG_REJECT_TRAILING_BYTES_AFTER_EOI].size = 28u;
  (void)memcpy(cases[JPEG_REJECT_STANDALONE_MARKER].bytes, baseline_jpeg,
               15u);
  cases[JPEG_REJECT_STANDALONE_MARKER].bytes[15] = 0xffu;
  cases[JPEG_REJECT_STANDALONE_MARKER].bytes[16] = 0xd8u;
  (void)memcpy(cases[JPEG_REJECT_STANDALONE_MARKER].bytes + 17u,
               baseline_jpeg + 15u, 12u);
  cases[JPEG_REJECT_STANDALONE_MARKER].size = 29u;
  cases[JPEG_REJECT_TRUNCATED_MARKER_LENGTH].bytes[0] = 0xffu;
  cases[JPEG_REJECT_TRUNCATED_MARKER_LENGTH].bytes[1] = 0xd8u;
  cases[JPEG_REJECT_TRUNCATED_MARKER_LENGTH].bytes[2] = 0xffu;
  cases[JPEG_REJECT_TRUNCATED_MARKER_LENGTH].bytes[3] = 0xdbu;
  cases[JPEG_REJECT_TRUNCATED_MARKER_LENGTH].bytes[4] = 0x00u;
  cases[JPEG_REJECT_TRUNCATED_MARKER_LENGTH].size = 5u;
  cases[JPEG_REJECT_INVALID_MARKER_LENGTH].bytes[0] = 0xffu;
  cases[JPEG_REJECT_INVALID_MARKER_LENGTH].bytes[1] = 0xd8u;
  cases[JPEG_REJECT_INVALID_MARKER_LENGTH].bytes[2] = 0xffu;
  cases[JPEG_REJECT_INVALID_MARKER_LENGTH].bytes[3] = 0xdbu;
  cases[JPEG_REJECT_INVALID_MARKER_LENGTH].bytes[4] = 0x00u;
  cases[JPEG_REJECT_INVALID_MARKER_LENGTH].bytes[5] = 0x01u;
  cases[JPEG_REJECT_INVALID_MARKER_LENGTH].size = 6u;
  (void)memcpy(cases[JPEG_REJECT_DUPLICATE_FRAME].bytes, baseline_jpeg, 15u);
  (void)memcpy(cases[JPEG_REJECT_DUPLICATE_FRAME].bytes + 15u,
               baseline_jpeg + 2u, 13u);
  (void)memcpy(cases[JPEG_REJECT_DUPLICATE_FRAME].bytes + 28u,
               baseline_jpeg + 15u, 12u);
  cases[JPEG_REJECT_DUPLICATE_FRAME].size = 40u;
  cases[JPEG_REJECT_TRUNCATED_FRAME].bytes[5] = 0x07u;
  cases[JPEG_REJECT_INVALID_FRAME_COMPONENTS].bytes[5] = 0x08u;
  cases[JPEG_REJECT_INVALID_FRAME_PRECISION].bytes[6] = 0x00u;
  cases[JPEG_REJECT_INVALID_FRAME_SIZE].bytes[8] = 0x00u;
  (void)memcpy(cases[JPEG_REJECT_SCAN_BEFORE_FRAME].bytes, baseline_jpeg, 2u);
  (void)memcpy(cases[JPEG_REJECT_SCAN_BEFORE_FRAME].bytes + 2u,
               baseline_jpeg + 15u, 12u);
  cases[JPEG_REJECT_SCAN_BEFORE_FRAME].size = 14u;
  cases[JPEG_REJECT_TRUNCATED_SCAN].bytes[18] = 0x05u;
  cases[JPEG_REJECT_INVALID_SCAN_COMPONENTS].bytes[18] = 0x06u;
  (void)memcpy(cases[JPEG_REJECT_PARTIAL_ENTROPY_MARKER].bytes,
               baseline_jpeg, 25u);
  cases[JPEG_REJECT_PARTIAL_ENTROPY_MARKER].bytes[25] = 0xffu;
  cases[JPEG_REJECT_PARTIAL_ENTROPY_MARKER].size = 26u;
  cases[JPEG_REJECT_PROGRESSIVE_FRAME].bytes[3] = 0xc2u;
  cases[JPEG_REJECT_MISSING_FRAME].bytes[0] = 0xffu;
  cases[JPEG_REJECT_MISSING_FRAME].bytes[1] = 0xd8u;
  cases[JPEG_REJECT_MISSING_FRAME].bytes[2] = 0xffu;
  cases[JPEG_REJECT_MISSING_FRAME].bytes[3] = 0xd9u;
  cases[JPEG_REJECT_MISSING_FRAME].size = 4u;
  cases[JPEG_REJECT_UNSUPPORTED_FRAME].bytes[3] = 0xc3u;
  (void)memcpy(cases[JPEG_REJECT_MISSING_SCAN].bytes, baseline_jpeg, 15u);
  (void)memcpy(cases[JPEG_REJECT_MISSING_SCAN].bytes + 15u,
               baseline_jpeg + 25u, 2u);
  cases[JPEG_REJECT_MISSING_SCAN].size = 17u;
  (void)memcpy(cases[JPEG_REJECT_MISSING_EOI].bytes, baseline_jpeg, 25u);
  cases[JPEG_REJECT_MISSING_EOI].size = 25u;

  for (index = 0u; index < JPEG_REJECTION_COUNT; index++) {
    success &= expect_rejected_jpeg(
        cases[index].name, cases[index].bytes, cases[index].size,
        cases[index].expected_reason);
  }
  return success;
}

int main(void) {
  int success = run_positive_contract();
  success &= run_object_contract();
  success &= run_rejection_contract();
  return success != 0 ? 0 : 1;
}
