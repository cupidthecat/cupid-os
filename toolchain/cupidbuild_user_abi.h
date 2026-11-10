#ifndef CUPIDBUILD_USER_ABI_H
#define CUPIDBUILD_USER_ABI_H

#include "user_syscall_abi.h"

/* Read and retain the six ABI declarations under this repository root, validate
 * their captured bytes, recheck the retained observations, and close them before
 * publishing a successful result. Relative roots resolve against the caller's
 * working directory. POSIX roots allow dot and repeated slash components but
 * reject parent components, links and missing ancestors. No files, directories,
 * locks or child processes are created.
 * Result and diagnostic ownership match cupid_user_abi_validate. Sequential
 * rechecks detect drift; they do not provide an atomic filesystem snapshot. */
int cupidbuild_verify_user_abi(const char *repository_root,
                                cupid_user_abi_report_t *result,
                                char *error, size_t error_size);

#endif
