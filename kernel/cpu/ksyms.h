/*
 * ksyms - kernel function symbol table for backtrace decoding.
 *
 * The production blob is generated after the pass-one link by
 * tools/hostbuild.py mksyms from kernel/kernel.elf.pass1, then placed in
 * the .ksyms section by ksyms_data.o. The legacy tools/mksyms.sh oracle
 * documents the shared format. Lookup tolerates a missing or corrupt blob
 * and returns NULL, so callers can fall back to printing raw addresses.
*/
#ifndef KSYMS_H
#define KSYMS_H

#include "types.h"

/* Look up the function containing addr.  On match, sets *off_out to
 * (addr - function_start) and returns the function name (NUL-term).
 * Returns NULL if no symbol contains addr or the blob is unavailable.*/
const char *ksym_lookup(uint32_t addr, uint32_t *off_out);

/* Walk the EBP frame chain starting from start_ebp, calling print_line
 * for each frame.  start_eip is printed as frame #0; thereafter return
 * addresses are read from each frame.  Caps at max_frames.*/
void ksym_backtrace(uint32_t start_ebp, uint32_t start_eip, int max_frames,
                    void (*print_line)(int frame, uint32_t addr,
                                       const char *name, uint32_t off));

#endif
