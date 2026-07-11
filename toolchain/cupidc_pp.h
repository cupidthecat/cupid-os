#ifndef CUPID_TOOLCHAIN_CUPIDC_PREPROCESSOR_H
#define CUPID_TOOLCHAIN_CUPIDC_PREPROCESSOR_H

#include "ctool.h"

#define CTOOL_C_PP_INCLUDE_QUOTED 0x01u
#define CTOOL_C_PP_INCLUDE_ANGLE 0x02u

typedef enum {
  CTOOL_C_PP_MODE_C11 = 1,
  CTOOL_C_PP_MODE_CUPID
} ctool_c_pp_mode_t;

typedef struct {
  ctool_path_t directory;
  ctool_u32 forms;
} ctool_c_pp_include_root_t;

typedef enum {
  CTOOL_C_PP_MACRO_DEFINE = 1,
  CTOOL_C_PP_MACRO_UNDEF
} ctool_c_pp_macro_action_kind_t;

typedef struct {
  ctool_c_pp_macro_action_kind_t kind;
  ctool_string_t name;
  ctool_string_t replacement;
} ctool_c_pp_macro_action_t;

typedef struct {
  ctool_c_pp_mode_t mode;
  ctool_bool hosted_environment;
  ctool_bool gnu_extensions;
  /* Unquoted C11 `Mmm dd yyyy` and `hh:mm:ss` spellings. Empty values select
   * the deterministic bootstrap epoch (`Jan  1 1970`, `00:00:00`). */
  ctool_string_t translation_date;
  ctool_string_t translation_time;
  const ctool_c_pp_include_root_t *include_roots;
  ctool_u32 include_root_count;
  const ctool_path_t *forced_includes;
  ctool_u32 forced_include_count;
  const ctool_c_pp_macro_action_t *macro_actions;
  ctool_u32 macro_action_count;
} ctool_c_pp_request_t;

typedef struct {
  ctool_string_t path;
  ctool_u32 line;
  ctool_u32 column;
} ctool_c_pp_location_t;

typedef enum {
  CTOOL_C_PP_TOKEN_IDENTIFIER = 1,
  CTOOL_C_PP_TOKEN_NUMBER,
  CTOOL_C_PP_TOKEN_CHARACTER,
  CTOOL_C_PP_TOKEN_STRING,
  CTOOL_C_PP_TOKEN_PUNCTUATOR,
  /* Spelling is `exe`; location is the introducing # or %: token. */
  CTOOL_C_PP_TOKEN_CUPID_EXE
} ctool_c_pp_token_kind_t;

typedef struct {
  ctool_c_pp_token_kind_t kind;
  ctool_string_t spelling;
  ctool_c_pp_location_t location;
  /* Zero selects natural alignment; otherwise this is the effective
   * #pragma pack member-alignment cap at the token's emission or macro
   * invocation position. */
  ctool_u32 pack_alignment;
} ctool_c_pp_token_t;

typedef struct {
  const ctool_c_pp_token_t *tokens;
  ctool_u32 token_count;
} ctool_c_pp_result_t;

typedef enum {
  CTOOL_C_PP_DIAG_INVALID_REQUEST = 0x09000001u,
  CTOOL_C_PP_DIAG_LEXICAL = 0x09000002u,
  CTOOL_C_PP_DIAG_DIRECTIVE = 0x09000003u,
  CTOOL_C_PP_DIAG_MACRO_DEFINITION = 0x09000004u,
  CTOOL_C_PP_DIAG_MACRO_REDEFINITION = 0x09000005u,
  CTOOL_C_PP_DIAG_MACRO_ARGUMENTS = 0x09000006u,
  CTOOL_C_PP_DIAG_MACRO_PASTE = 0x09000007u,
  CTOOL_C_PP_DIAG_MACRO_EXPANSION = 0x09000008u,
  CTOOL_C_PP_DIAG_INCLUDE_NOT_FOUND = 0x09000009u,
  CTOOL_C_PP_DIAG_INCLUDE_PATH = 0x0900000au,
  CTOOL_C_PP_DIAG_INCLUDE_DEPTH = 0x0900000bu,
  CTOOL_C_PP_DIAG_CONDITIONAL = 0x0900000cu,
  CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION = 0x0900000du,
  CTOOL_C_PP_DIAG_ERROR_DIRECTIVE = 0x0900000eu,
  CTOOL_C_PP_DIAG_PRAGMA_PACK = 0x0900000fu,
  CTOOL_C_PP_DIAG_CUPID_EXE = 0x09000010u,
  CTOOL_C_PP_DIAG_LIMIT = 0x09000011u,
  CTOOL_C_PP_DIAG_UNSUPPORTED_CONFIGURATION = 0x09000012u
} ctool_c_pp_diag_code_t;

ctool_status_t ctool_c_preprocess(ctool_job_t *job,
                                  const ctool_source_t *primary,
                                  const ctool_c_pp_request_t *request,
                                  ctool_c_pp_result_t *result_out);

/* Request/source views are borrowed only for the call.  Successful tokens,
 * spellings, and canonical logical paths are job-arena owned.  Configured
 * macro actions are applied before forced includes, which are processed in
 * order before primary.  Quoted includes try the including-file
 * parent before eligible roots; angle includes use eligible roots only.
 * Exact direct header-name and string operands remain unexpanded. Other
 * include operands undergo normal macro rescanning and must finish as one
 * ordinary string token, one surviving contextual header-name token, or a
 * complete split angle form. Expanded angle forms retain each surviving token
 * separation as one ASCII space.
 *
 * On failure the result is fully zeroed and every operation allocation is
 * rewound, while structured job diagnostics remain valid.
 *
 * Standard variadic macros, stringify/paste operators, opt-in GNU
 * omitted-variable-argument comma elision, C11 #if/#elif integer expressions,
 * and all seven C11 language predefined macros are part of the token-tape
 * contract. `__DATE__` and `__TIME__` use only the request's reproducible
 * translation seed and never consult a host clock.
 *
 * Active `#pragma once` uses canonical logical-path identity for the current
 * preprocessing operation. Active `#pragma pack` state is translation-unit
 * global across forced includes, ordinary includes, and the primary source.
 * The common `pack()`, `pack(n)`, push, named push, pop, and named pop forms
 * accept member caps 0, 1, 2, 4, 8, and 16 and annotate emitted tokens with
 * the effective cap. Unmatched pushes at translation-unit end are accepted;
 * stack underflow, invalid syntax, and a missing named push are errors.
 *
 * In Cupid mode an active `#exe` or `%:exe` with a raw same-logical-line
 * opening brace emits one CTOOL_C_PP_TOKEN_CUPID_EXE marker followed by the
 * ordinary expanded brace/body tape. Block matching, JIT/AOT lowering,
 * execution order, and runtime limits remain parser/runtime policy. C11 mode
 * rejects an active marker explicitly; inactive groups have no effect.
 *
 * During the incremental bootstrap, named GNU `args...` macros and
 * unknown pragmas or directives return explicit CTOOL_ERR_UNSUPPORTED
 * diagnostics until their contract slices land. */

#endif
