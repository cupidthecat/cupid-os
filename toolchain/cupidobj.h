#ifndef CUPID_TOOLCHAIN_CUPIDOBJ_H
#define CUPID_TOOLCHAIN_CUPIDOBJ_H

#include "ctool.h"
#include "elf32.h"

typedef enum {
  CTOOL_OBJ_WRAP_BINARY = 1,
  CTOOL_OBJ_WRAP_TEXT,
  CTOOL_OBJ_EXTRACT_FLAT
} ctool_obj_operation_t;

typedef struct {
  ctool_string_t section_name;
  ctool_u32 section_flags;
  ctool_u32 section_alignment;
  ctool_string_t start_symbol;
  ctool_string_t end_symbol;
  ctool_string_t size_symbol;
} ctool_obj_wrap_binary_request_t;

typedef struct {
  ctool_obj_operation_t operation;
  const ctool_source_t *input;
  union {
    ctool_obj_wrap_binary_request_t wrap_binary;
  } as;
} ctool_obj_request_t;

typedef struct {
  ctool_bytes_t bytes;
  ctool_u32 base_address;
  ctool_u32 end_address;
} ctool_obj_result_t;

typedef enum {
  CTOOL_OBJ_DIAG_INVALID_REQUEST = 0x08000001u,
  CTOOL_OBJ_DIAG_INVALID_INPUT = 0x08000002u,
  CTOOL_OBJ_DIAG_INVALID_SECTION = 0x08000003u,
  CTOOL_OBJ_DIAG_INVALID_SYMBOL = 0x08000004u,
  CTOOL_OBJ_DIAG_SYMBOL_COLLISION = 0x08000005u,
  CTOOL_OBJ_DIAG_NO_LOAD = 0x08000006u,
  CTOOL_OBJ_DIAG_OVERLAP = 0x08000007u,
  CTOOL_OBJ_DIAG_ADDRESS_OVERFLOW = 0x08000008u,
  CTOOL_OBJ_DIAG_LIMIT = 0x08000009u,
  CTOOL_OBJ_DIAG_OUTPUT = 0x0800000au,
  CTOOL_OBJ_DIAG_UNSUPPORTED = 0x0800000bu
} ctool_obj_diag_code_t;

ctool_status_t ctool_obj_transform(ctool_job_t *job,
                                    const ctool_obj_request_t *request,
                                    ctool_buffer_t *output,
                                    ctool_obj_result_t *result_out);

/* Request/source views are borrowed for the call.  WRAP_BINARY emits one
 * canonical ELF32 ET_REL PROGBITS section with the exact requested bytes and
 * global start, end, and absolute size symbols.  WRAP_TEXT has the same
 * object model but canonicalizes CRLF pairs to LF; lone carriage returns are
 * retained.  EXTRACT_FLAT lays out initialized PT_LOAD bytes by physical
 * address, with a checked allocated-section fallback for executables without
 * load segments; BSS is excluded.
 *
 * Output must be empty.  Every failure preserves its pre-call bytes and fully
 * zeros result_out.  On success result bytes borrow output; extraction addresses
 * describe the half-open initialized range [base_address, end_address).
 * Equal requests and inputs produce byte-identical output. */

#endif
