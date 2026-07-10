#ifndef CUPID_TOOLCHAIN_CUPIDLD_H
#define CUPID_TOOLCHAIN_CUPIDLD_H

#include "ctool.h"

typedef enum {
  CTOOL_LD_LAYOUT_SCRIPT = 0,
  CTOOL_LD_LAYOUT_FIXED_TEXT
} ctool_ld_layout_kind_t;

typedef struct {
  ctool_u32 base_address;
  ctool_string_t entry_symbol;
} ctool_ld_fixed_text_layout_t;

typedef struct {
  ctool_ld_layout_kind_t kind;
  union {
    const ctool_source_t *script;
    ctool_ld_fixed_text_layout_t fixed_text;
  } as;
} ctool_ld_layout_t;

typedef struct {
  const ctool_source_t *objects;
  ctool_u32 object_count;
  ctool_ld_layout_t layout;
  ctool_u32 maximum_image_span;
} ctool_ld_request_t;

typedef struct {
  ctool_u32 bytes;
  ctool_u32 entry;
  ctool_u32 load_address;
  ctool_u32 loaded_end;
  ctool_u32 memory_end;
  ctool_u32 output_section_count;
  ctool_u32 resolved_symbol_count;
  ctool_u32 applied_relocation_count;
} ctool_ld_result_t;

typedef enum {
  CTOOL_LD_DIAG_INVALID_REQUEST = 0x07000001u,
  CTOOL_LD_DIAG_NONEMPTY_OUTPUT = 0x07000002u,
  CTOOL_LD_DIAG_BAD_LAYOUT = 0x07000003u,
  CTOOL_LD_DIAG_UNSUPPORTED_INPUT = 0x07000004u,
  CTOOL_LD_DIAG_UNMATCHED_SECTION = 0x07000005u,
  CTOOL_LD_DIAG_UNSUPPORTED_RELOCATION = 0x07000006u,
  CTOOL_LD_DIAG_DUPLICATE_SYMBOL = 0x07000007u,
  CTOOL_LD_DIAG_UNDEFINED_SYMBOL = 0x07000008u,
  CTOOL_LD_DIAG_BACKWARD_DOT = 0x07000009u,
  CTOOL_LD_DIAG_OVERFLOW = 0x0700000au,
  CTOOL_LD_DIAG_BAD_ENTRY = 0x0700000bu,
  CTOOL_LD_DIAG_ASSERTION_FAILED = 0x0700000cu,
  CTOOL_LD_DIAG_LIMIT = 0x0700000du
} ctool_ld_diag_code_t;

ctool_status_t ctool_ld_link(ctool_job_t *job,
                             const ctool_ld_request_t *request,
                             ctool_buffer_t *output,
                             ctool_ld_result_t *result_out);

/* Object images, layout source, and request strings are borrowed for the
 * call.  Output must be empty.  Success returns a deterministic static i386
 * ET_EXEC; every failure restores the original empty output and zeros the
 * result. */

#endif
