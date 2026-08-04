#ifndef CUPID_TOOLCHAIN_CUPIDOBJ_H
#define CUPID_TOOLCHAIN_CUPIDOBJ_H

#include "ctool.h"
#include "elf32.h"

typedef enum {
  CTOOL_OBJ_WRAP_BINARY = 1,
  CTOOL_OBJ_WRAP_TEXT,
  CTOOL_OBJ_EXTRACT_FLAT,
  CTOOL_OBJ_GENERATE_INSTALL_SOURCE,
  CTOOL_OBJ_GENERATE_KSYMS_SOURCE,
  CTOOL_OBJ_WRAP_JPEG
} ctool_obj_operation_t;

typedef enum {
  CTOOL_OBJ_INSTALL_DEMOS = 1,
  CTOOL_OBJ_INSTALL_BIN,
  CTOOL_OBJ_INSTALL_DOCS
} ctool_obj_install_source_kind_t;

typedef struct {
  ctool_string_t section_name;
  ctool_u32 section_flags;
  ctool_u32 section_alignment;
  ctool_string_t start_symbol;
  ctool_string_t end_symbol;
  ctool_string_t size_symbol;
} ctool_obj_wrap_binary_request_t;

typedef struct {
  ctool_obj_install_source_kind_t kind;
  const ctool_string_t *bin_paths;
  ctool_u32 bin_count;
  const ctool_string_t *header_paths;
  ctool_u32 header_count;
  const ctool_string_t *browser_paths;
  ctool_u32 browser_count;
  const ctool_string_t *ctxt_paths;
  ctool_u32 ctxt_count;
  const ctool_string_t *doc_asset_paths;
  ctool_u32 doc_asset_count;
  const ctool_string_t *home_asset_paths;
  ctool_u32 home_asset_count;
  const ctool_string_t *demo_paths;
  ctool_u32 demo_count;
} ctool_obj_install_source_request_t;

typedef struct {
  ctool_obj_operation_t operation;
  const ctool_source_t *input;
  union {
    ctool_obj_wrap_binary_request_t wrap_binary;
    ctool_obj_install_source_request_t install_source;
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

/* Request/source and installation path views are borrowed for the call.
 * WRAP_BINARY emits one
 * canonical ELF32 ET_REL PROGBITS section with the exact requested bytes and
 * global start, end, and absolute size symbols.  WRAP_TEXT has the same
 * object model but canonicalizes CRLF pairs to LF; lone carriage returns are
 * retained.  WRAP_JPEG first validates one baseline or extended sequential
 * SOF0/SOF1 frame, at least one scan, and a terminal EOI marker, then wraps
 * the exact input bytes with the WRAP_BINARY object model.  Progressive and
 * other unsupported frame types are rejected.  EXTRACT_FLAT lays out
 * initialized PT_LOAD bytes by physical address, with a checked
 * allocated-section fallback for executables without load segments; BSS is
 * excluded.  WRAP_JPEG validation and emission are transactional at this
 * transform boundary: failures retain no temporary arena storage, and the
 * same job can be reused.
 *
 * Output must be empty.  Every failure preserves its pre-call bytes and fully
 * zeros result_out.  On success result bytes borrow output; extraction
 * addresses describe the half-open initialized range [base_address,
 * end_address).  Equal requests and inputs produce byte-identical output.
 *
 * GENERATE_INSTALL_SOURCE emits one of the bin, docs, or demos installation
 * tables from a typed path inventory.  Paths use repository-relative forward
 * slash spelling and their category's exact extension.  A request may contain
 * at most 512 paths across all categories, and that total is checked without
 * overflowing before a list is traversed.  Output keeps caller order within
 * each typed list.  Category mixing, malformed paths, duplicate paths,
 * distinct paths that map to the same complete wrapped binary symbol, and
 * output exhaustion fail before publication.  One exact BMP path may appear
 * once in both the documentation and home lists because both entries use the
 * same wrapped object.
 *
 * GENERATE_KSYMS_SOURCE consumes canonical CupidDis nm text.  It retains
 * defined text and weak-text symbols, orders them by address while preserving
 * input order at equal addresses, keeps the first symbol at each address, and
 * emits the kernel's word-packed .ksyms C source.  Undefined and non-text
 * rows are ignored.  Malformed rows, addresses outside i386, an empty text
 * symbol set, allocation limits, and output limits fail before publication. */

#endif
