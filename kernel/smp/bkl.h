#ifndef BKL_H
#define BKL_H

#include "types.h"

void bkl_init(void);
void bkl_lock(void);
void bkl_unlock(void);
bool bkl_held_by_this_cpu(void);
bool bkl_is_initialized(void);

#endif
