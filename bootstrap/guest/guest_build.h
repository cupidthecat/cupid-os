/**
 * @file guest_build.h
 * @brief Run individual toolchain requests from a guest build.
 */

#ifndef CUPID_BOOTSTRAP_GUEST_BUILD_H
#define CUPID_BOOTSTRAP_GUEST_BUILD_H

#include "guest_job.h"

#define CUPID_GUEST_LINK_INPUT_LIMIT 512u

typedef ctool_status_t (*cupid_guest_operation_t)(cupid_syscall_table_t *sys,
                                                const cupid_guest_request_t *job);

ctool_status_t cupid_guest_compile_job(cupid_syscall_table_t *sys, const cupid_guest_request_t *job);
ctool_status_t cupid_guest_link_job(cupid_syscall_table_t *sys, const cupid_guest_request_t *job);

ctool_status_t cupid_guest_symbols_job(cupid_syscall_table_t *sys, const cupid_guest_request_t *job);
ctool_status_t cupid_guest_object_job(cupid_syscall_table_t *sys, const cupid_guest_request_t *job);
ctool_status_t cupid_guest_raw_job(cupid_syscall_table_t *sys, const cupid_guest_request_t *job);
ctool_status_t cupid_guest_assemble_job(cupid_syscall_table_t *sys, const cupid_guest_request_t *job);
ctool_status_t cupid_guest_install_job(cupid_syscall_table_t *sys, const cupid_guest_request_t *job);
ctool_status_t cupid_guest_disk_job(cupid_syscall_table_t *sys, const cupid_guest_request_t *job);

ctool_status_t cupid_guest_kernel_job(cupid_syscall_table_t *sys, const cupid_guest_request_t *job);

#endif
