#ifndef CUPID_KERNEL_AS_ELF_H
#define CUPID_KERNEL_AS_ELF_H

#include "ctool.h"
#include "cupidasm.h"

/* Serialize one validated CupidASM fixed image as a sectionless i386 ELF32
 * executable.  The artifact and its byte/region views are borrowed for the
 * call.  Output must be empty; after any failure from an empty output it
 * remains empty. */
ctool_status_t as_elf32_exec_write(const ctool_asm_result_t *artifact,
                                   ctool_buffer_t *output);

#endif
