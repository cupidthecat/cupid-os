#ifndef CUPID_TOOLCHAIN_CUPIDC_EMIT_H
#define CUPID_TOOLCHAIN_CUPIDC_EMIT_H

#include "cupidc_frontend.h"

typedef enum {
  CTOOL_C_EMIT_DIAG_INVALID_REQUEST = 0x0c000001u,
  CTOOL_C_EMIT_DIAG_INVALID_UNIT = 0x0c000002u,
  CTOOL_C_EMIT_DIAG_UNSUPPORTED = 0x0c000003u,
  CTOOL_C_EMIT_DIAG_INITIALIZER = 0x0c000004u,
  CTOOL_C_EMIT_DIAG_SYMBOL = 0x0c000005u,
  CTOOL_C_EMIT_DIAG_RELOCATION = 0x0c000006u,
  CTOOL_C_EMIT_DIAG_LIMIT = 0x0c000007u,
  CTOOL_C_EMIT_DIAG_INTERNAL = 0x0c000008u
} ctool_c_emit_diag_code_t;

ctool_status_t ctool_c_emit_object(
    ctool_job_t *job, const ctool_c_translation_unit_t *unit,
    ctool_buffer_t *output);

/* The typed translation unit is borrowed and remains unchanged. The output
 * must be empty. Success writes one deterministic i386 ELF relocatable
 * object. A failure after argument validation restores the output and
 * operation arena while retaining a structured diagnostic. Function
 * definitions remain a checked boundary until code generation is available.
 */

#endif
