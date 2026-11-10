#ifndef CUPID_TOOLCHAIN_CUPIDASM_H
#define CUPID_TOOLCHAIN_CUPIDASM_H

#include "ctool.h"
#include "elf32.h"
#include "x86.h"

#define CTOOL_ASM_MAX_INCLUDE_DEPTH 32u

typedef enum {
  CTOOL_ASM_ARTIFACT_RAW = 1,
  CTOOL_ASM_ARTIFACT_ELF32_REL,
  CTOOL_ASM_ARTIFACT_FIXED_IMAGE
} ctool_asm_artifact_kind_t;

typedef enum {
  CTOOL_ASM_DEFINE_CONSTANT = 1,
  CTOOL_ASM_DEFINE_ABSOLUTE
} ctool_asm_definition_kind_t;

typedef struct {
  ctool_string_t name;
  ctool_asm_definition_kind_t kind;
  ctool_u32 value;
} ctool_asm_definition_t;

typedef struct {
  ctool_u32 base_address;
  ctool_u32 maximum_bytes;
} ctool_asm_fixed_region_spec_t;

typedef enum {
  CTOOL_ASM_RAW_RANGE_CODE16 = 1,
  CTOOL_ASM_RAW_RANGE_CODE32,
  CTOOL_ASM_RAW_RANGE_DATA
} ctool_asm_raw_range_kind_t;

typedef struct {
  ctool_u32 offset;
  ctool_asm_raw_range_kind_t kind;
} ctool_asm_raw_range_t;

typedef enum {
  CTOOL_ASM_RAW_EDGE_RELATIVE = 1,
  CTOOL_ASM_RAW_EDGE_FAR,
  CTOOL_ASM_RAW_EDGE_INDIRECT
} ctool_asm_raw_edge_kind_t;

typedef enum {
  CTOOL_ASM_RAW_EDGE_LOCAL = 1,
  CTOOL_ASM_RAW_EDGE_EXTERNAL,
  CTOOL_ASM_RAW_EDGE_UNPROVABLE
} ctool_asm_raw_edge_class_t;

#define CTOOL_ASM_RAW_EDGE_NO_TARGET 0xffffffffu

typedef struct {
  ctool_u32 source_offset;
  ctool_asm_raw_edge_kind_t kind;
  ctool_asm_raw_edge_class_t class_id;
  ctool_u32 target_offset;
  ctool_u32 target_address;
  ctool_x86_mode_t target_mode;
  ctool_u32 target_segment;
} ctool_asm_raw_edge_t;

typedef struct {
  ctool_asm_artifact_kind_t artifact;
  ctool_x86_mode_t initial_mode;
  const ctool_asm_definition_t *definitions;
  ctool_u32 definition_count;
  const ctool_path_t *include_roots;
  ctool_u32 include_root_count;
  const ctool_string_t *entry_candidates;
  ctool_u32 entry_candidate_count;
  /* Default symbol identity is case-sensitive.  The kernel compatibility
   * adapter opts into its historical ASCII-insensitive symbol policy. */
  ctool_bool case_insensitive_symbols;
  /* Permit otherwise undeclared object references to become global imports. */
  ctool_bool allow_implicit_externs;
  union {
    struct {
      ctool_u32 initial_origin;
    } raw;
    struct {
      ctool_u32 reserved;
    } elf32_rel;
    struct {
      ctool_asm_fixed_region_spec_t code;
      ctool_asm_fixed_region_spec_t data;
    } fixed;
  } as;
} ctool_asm_request_t;

typedef struct {
  ctool_string_t name;
  ctool_u32 address;
  ctool_u32 output_offset;
  ctool_u32 file_size;
  ctool_u32 memory_size;
  /* ELF32 SHF_* bits.  Fixed results publish code then data and omit empty
   * regions; output_offset/file_size select initialized result bytes. */
  ctool_u32 flags;
} ctool_asm_region_t;

typedef struct {
  ctool_asm_artifact_kind_t artifact;
  ctool_bytes_t bytes;
  const ctool_asm_region_t *regions;
  ctool_u32 region_count;
  ctool_bool has_entry;
  /* Fixed images publish an address.  Relocatable objects publish the
   * selected code symbol so a linker can own final placement. */
  ctool_string_t entry_symbol;
  ctool_u32 entry_address;
  /* Raw results describe the source-derived code and data partition.
   * Adjacent statements of the same kind are coalesced. */
  const ctool_asm_raw_range_t *raw_ranges;
  ctool_u32 raw_range_count;
  /* Source-resolved direct transfers and explicit indirect transfers, in
   * instruction order.  NO_TARGET marks fields that runtime state owns. */
  const ctool_asm_raw_edge_t *raw_edges;
  ctool_u32 raw_edge_count;
  ctool_u32 raw_origin;
} ctool_asm_result_t;

typedef enum {
  CTOOL_ASM_DIAG_INVALID_REQUEST = 0x06000001u,
  CTOOL_ASM_DIAG_INVALID_SOURCE = 0x06000002u,
  CTOOL_ASM_DIAG_LEXICAL = 0x06000003u,
  CTOOL_ASM_DIAG_SYNTAX = 0x06000004u,
  CTOOL_ASM_DIAG_UNKNOWN_DIRECTIVE = 0x06000005u,
  CTOOL_ASM_DIAG_DUPLICATE_SYMBOL = 0x06000006u,
  CTOOL_ASM_DIAG_UNDEFINED_SYMBOL = 0x06000007u,
  CTOOL_ASM_DIAG_INVALID_EXPRESSION = 0x06000008u,
  CTOOL_ASM_DIAG_NON_RELOCATABLE_EXPRESSION = 0x06000009u,
  CTOOL_ASM_DIAG_EXPRESSION_OVERFLOW = 0x0600000au,
  CTOOL_ASM_DIAG_INCLUDE_NOT_FOUND = 0x0600000bu,
  CTOOL_ASM_DIAG_INCLUDE_CYCLE = 0x0600000cu,
  CTOOL_ASM_DIAG_INCLUDE_DEPTH = 0x0600000du,
  CTOOL_ASM_DIAG_INCLUDE_PATH = 0x0600000eu,
  CTOOL_ASM_DIAG_INVALID_MODE = 0x0600000fu,
  CTOOL_ASM_DIAG_INVALID_ORIGIN = 0x06000010u,
  CTOOL_ASM_DIAG_INVALID_SECTION = 0x06000011u,
  CTOOL_ASM_DIAG_ENCODING = 0x06000012u,
  CTOOL_ASM_DIAG_RELOCATION = 0x06000013u,
  CTOOL_ASM_DIAG_LAYOUT = 0x06000014u,
  CTOOL_ASM_DIAG_ENTRY = 0x06000015u,
  CTOOL_ASM_DIAG_OUTPUT = 0x06000016u,
  CTOOL_ASM_DIAG_LIMIT = 0x06000017u,
  CTOOL_ASM_DIAG_INVALID_SYMBOL_TYPE = 0x06000018u
} ctool_asm_diag_code_t;

ctool_status_t ctool_asm_assemble(ctool_job_t *job,
                                  const ctool_source_t *source,
                                  const ctool_asm_request_t *request,
                                  ctool_buffer_t *output,
                                  ctool_asm_result_t *result_out);

/* Source/request views are borrowed for the call.  Includes try the including
 * source's logical parent, then include_roots in caller order.  Successful
 * result metadata is job-arena owned and result bytes borrow the caller-owned
 * output buffer.  Output must be empty and is rewound to empty on failure;
 * result_out is fully zeroed on failure. */

#endif
