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
 * object. A failure after argument validation restores the output, rewinds
 * allocations made during the operation, and retains a structured
 * diagnostic. The emitter lowers represented functions, including fixed
 * local frames and linked 32-bit file-object loads, through CupidC linear IR
 * and the shared x86 model. Direct object addresses use text `R_386_32`
 * relocations, while direct calls use `R_386_PC32`. The emitter writes those
 * functions beside static definitions in the same object. */

#endif
