/* stdbool.h shim - tells doomtype.h that bool is already defined */
#ifndef _STDBOOL_H
#define _STDBOOL_H
#define __bool_true_false_are_defined 1
/* bool/true/false come from kernel/types.h via dglibc_compat.h */
#endif
