/**
 * @file guest_build.h
 * @brief Run compiler and linker requests from a guest build.
 */

#ifndef CUPID_BOOTSTRAP_GUEST_BUILD_H
#define CUPID_BOOTSTRAP_GUEST_BUILD_H

#include "guest_job.h"

ctool_status_t cupid_guest_compile_job(cupid_syscall_table_t *sys, const char *job_path);
ctool_status_t cupid_guest_link_job(cupid_syscall_table_t *sys, const char *job_path);

#endif
