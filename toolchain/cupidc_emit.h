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
 * diagnostic. The emitter lowers represented functions, including
 * target-sized fixed local frames, stable slots for block-scope compound
 * literals, automatic narrow character-array string initialization, runtime
 * narrow string literals, one-byte, two-byte, or four-byte integer loads and
 * stores, represented four-byte bit-field loads and plain assignments, and
 * four-byte pointer values, through CupidC linear IR and the shared x86 model.
 * Eight-byte integer constants, loads, conversions, and operation results use
 * private frame snapshots. The emitter widens represented signed or unsigned
 * integers, narrows wide values to represented integer lanes, and normalizes
 * wide Boolean conversions from both words. Same-width signed-to-unsigned
 * conversion and GNU wide-enum promotion preserve the snapshot bits. It also
 * emits two-word left and signed or unsigned right shifts, plus AND, OR, and
 * XOR. Calls preserve the i386 low word in EAX and high word in EDX, and
 * matching returns restore the same register pair.
 * Direct object and literal addresses use text `R_386_32` relocations,
 * including linked objects first declared by a block extern.
 * Block typedefs consume no frame storage and produce no target record.
 * Direct calls use `R_386_PC32`. The emitter writes those functions beside
 * static definitions in the same object. */

#endif
