#ifndef BKL_H
#define BKL_H

#include "types.h"

void bkl_init(void);
void bkl_lock(void);
void bkl_unlock(void);
bool bkl_held_by_this_cpu(void);
bool bkl_is_initialized(void);

/* Scheduler context-switch handoff.  The caller snapshots the interrupted
 * process's original EFLAGS before switching stacks.  Assembly releases the
 * outer scheduler acquisition only after the target ESP and FP state are
 * active; unlike bkl_unlock(), that release deliberately leaves IF clear. */
uint32_t bkl_context_switch_eflags(void);
bool bkl_context_switch_release(void);

#endif
