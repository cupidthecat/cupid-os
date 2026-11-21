/**
 * @file guest_build.h
 * @brief Run individual toolchain requests from a guest build.
 */

#ifndef CUPID_BOOTSTRAP_GUEST_BUILD_H
#define CUPID_BOOTSTRAP_GUEST_BUILD_H

#include "guest_job.h"

ctool_status_t cupid_guest_compile_job(cupid_syscall_table_t *sys, const char *job_path);
ctool_status_t cupid_guest_link_job(cupid_syscall_table_t *sys, const char *job_path);

ctool_status_t cupid_guest_symbols_job(cupid_syscall_table_t *sys, const char *job_path);
ctool_status_t cupid_guest_object_job(cupid_syscall_table_t *sys, const char *job_path);
ctool_status_t cupid_guest_raw_job(cupid_syscall_table_t *sys, const char *job_path);
ctool_status_t cupid_guest_assemble_job(cupid_syscall_table_t *sys, const char *job_path);
ctool_status_t cupid_guest_install_job(cupid_syscall_table_t *sys, const char *job_path);
ctool_status_t cupid_guest_disk_job(cupid_syscall_table_t *sys, const char *job_path);

#endif
