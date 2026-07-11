#ifndef CUPID_TOOLCHAIN_CUPIDC_FRONTEND_H
#define CUPID_TOOLCHAIN_CUPIDC_FRONTEND_H

#include "cupidc_type.h"

/* Deterministic translation limit for recursively nested declaration syntax.
 * It exceeds the C11 minimum translation limits and converts adversarial
 * nesting into a transactional diagnostic instead of a host-stack failure. */
#define CTOOL_C_PARSE_NESTING_LIMIT 256u

typedef struct {
  ctool_c_pp_mode_t mode;
  ctool_bool gnu_extensions;
} ctool_c_parse_request_t;

typedef enum {
  CTOOL_C_BINDING_TYPEDEF = 1,
  CTOOL_C_BINDING_OBJECT,
  CTOOL_C_BINDING_FUNCTION,
  CTOOL_C_BINDING_ENUMERATOR
} ctool_c_binding_kind_t;

typedef enum {
  CTOOL_C_STORAGE_NONE = 0,
  CTOOL_C_STORAGE_TYPEDEF,
  CTOOL_C_STORAGE_EXTERN,
  CTOOL_C_STORAGE_STATIC,
  CTOOL_C_STORAGE_AUTO,
  CTOOL_C_STORAGE_REGISTER
} ctool_c_storage_class_t;

typedef enum {
  CTOOL_C_LINKAGE_NONE = 0,
  CTOOL_C_LINKAGE_INTERNAL,
  CTOOL_C_LINKAGE_EXTERNAL
} ctool_c_linkage_t;

typedef struct {
  ctool_string_t name;
  ctool_c_binding_kind_t kind;
  /* Canonical bindings retain the first declaration's storage spelling and
   * dual location. Linkage and type describe the merged entity; a composite
   * type is symbol-local and never mutates a shared typedef graph. */
  ctool_c_storage_class_t storage;
  ctool_c_linkage_t linkage;
  ctool_u32 type;
  ctool_c_pp_location_t location;
  ctool_c_pp_location_t physical_location;
  /* ENUMERATOR only: target integer bit pattern and evaluated unsignedness. */
  ctool_u64 integer_bits;
  ctool_bool integer_unsigned;
} ctool_c_binding_t;

typedef struct {
  ctool_string_t name;
  ctool_u32 type;
  ctool_c_pp_location_t location;
  ctool_c_pp_location_t physical_location;
} ctool_c_tag_t;

typedef struct {
  ctool_string_t name;
  ctool_c_pp_location_t location;
  ctool_c_pp_location_t physical_location;
} ctool_c_parameter_t;

typedef struct {
  ctool_c_layout_request_t graph;
  ctool_c_layout_result_t layout;
  const ctool_c_binding_t *bindings;
  ctool_u32 binding_count;
  const ctool_c_tag_t *tags;
  ctool_u32 tag_count;
  /* Parallel to graph.parameter_types. Function slices index both arrays. */
  const ctool_c_parameter_t *parameters;
  ctool_u32 parameter_count;
} ctool_c_translation_unit_t;

typedef enum {
  CTOOL_C_PARSE_DIAG_INVALID_REQUEST = 0x0b000001u,
  CTOOL_C_PARSE_DIAG_EXPECTED_TOKEN = 0x0b000002u,
  CTOOL_C_PARSE_DIAG_DECLARATION_SPECIFIERS = 0x0b000003u,
  CTOOL_C_PARSE_DIAG_DECLARATOR = 0x0b000004u,
  CTOOL_C_PARSE_DIAG_TYPE_NAME = 0x0b000005u,
  CTOOL_C_PARSE_DIAG_REDEFINITION = 0x0b000006u,
  CTOOL_C_PARSE_DIAG_CONSTANT_EXPRESSION = 0x0b000007u,
  CTOOL_C_PARSE_DIAG_UNSUPPORTED = 0x0b000008u,
  CTOOL_C_PARSE_DIAG_LIMIT = 0x0b000009u,
  CTOOL_C_PARSE_DIAG_OVERFLOW = 0x0b00000au,
  CTOOL_C_PARSE_DIAG_INTERNAL = 0x0b00000bu
} ctool_c_parse_diag_code_t;

ctool_status_t ctool_c_parse(ctool_job_t *job,
                             const ctool_c_pp_result_t *tape,
                             const ctool_c_parse_request_t *request,
                             ctool_c_translation_unit_t *unit_out);

/* The tape and request are borrowed only for the call. Success publishes one
 * immutable, job-owned semantic graph plus layouts, ordinary bindings, named
 * tags, and parameter metadata. Presumed and physical locations and every
 * counted name/path are copied. Failure zeros the unit and rewinds all parser
 * allocations while preserving structured diagnostics.
 *
 * The request language settings must match those used to produce the tape;
 * preprocessing provenance does not yet carry that configuration itself.
 * This declaration slice owns the contracted scalar/typedef/storage subset,
 * namespaces, declarators, record/enum definitions, fixed or incomplete
 * arrays, prototypes, compatible file-scope redeclarations, composite array
 * and function types, C linkage, and layout. Type compatibility uses checked
 * iterative graph walks; the public nesting limit applies to recursive source
 * syntax, not derived-type graph depth. `_Thread_local`, `inline`,
 * `_Noreturn`, `_Alignas`, `_Atomic(type-name)`, and complex/imaginary type
 * specifiers are pending and fail closed rather than being skipped.
 * Declaration/member/namespace counts otherwise consume checked job storage
 * rather than fixed frontend tables. Function bodies, object initializers,
 * statement/expression ASTs, code generation, object emission, and Cupid
 * #exe execution remain later frontend operations and are diagnosed rather
 * than skipped. Tentative-definition state/finalization is not yet published,
 * so incomplete array declarations retain their parsed bounds in this slice. */

#endif
