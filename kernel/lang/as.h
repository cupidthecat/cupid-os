/**
 * as.h - CupidASM kernel command adapter
 *
 * Parsing, layout, instruction encoding, and fixed-image production live in
 * toolchain/cupidasm.  This adapter owns CupidOS runtime bindings, executable
 * memory placement, JIT execution, and the temporary ET_EXEC wrapper.
 */

#ifndef AS_H
#define AS_H

#include "types.h"

#define AS_MAX_CODE (1024u * 1024u)
#define AS_MAX_DATA (1024u * 1024u)

#define AS_JIT_CODE_BASE 0x01A00000u
#define AS_JIT_DATA_BASE 0x01B00000u

void as_jit(const char *path);
void as_aot(const char *src_path, const char *out_path);
void as_kernel_selftest(void);

#endif
