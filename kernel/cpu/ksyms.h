/*
 * ksyms - kernel function symbol table for backtrace decoding.
 *
 * The blob is built post-link by tools/mksyms.sh from kernel.elf and
 * placed in the .ksyms section by ksyms_data.o.  Format documented at
 * the top of mksyms.sh.  Lookup tolerates a missing/corrupt blob and
 * returns NULL - callers then fall back to printing raw addresses.
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
