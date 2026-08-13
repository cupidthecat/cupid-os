#ifndef CUPID_KERNEL_AS_ELF_H
#define CUPID_KERNEL_AS_ELF_H

#include "ctool.h"
#include "cupidasm.h"
#include "cupidld.h"

/* Link one validated CupidASM relocatable object through CupidLD's fixed-text
 * profile.  The assembler result and its bytes are borrowed for the call.
 * Output must be empty.  Failure leaves it empty and zeros the link result. */
ctool_status_t as_elf32_exec_link(ctool_job_t *job,
                                  const ctool_asm_result_t *artifact,
                                  ctool_u32 text_address,
                                  ctool_u32 maximum_image_span,
                                  ctool_buffer_t *output,
                                  ctool_ld_result_t *result_out);

/* Serialize one validated CupidASM fixed image as a sectionless i386 ELF32
 * executable.  The artifact and its byte/region views are borrowed for the
 * call.  Output must be empty; after any failure from an empty output it
 * remains empty. */
ctool_status_t as_elf32_exec_write(const ctool_asm_result_t *artifact,
                                   ctool_buffer_t *output);

#endif
