#ifndef CUPID_BOOTSTRAP_GUEST_JOB_H
#define CUPID_BOOTSTRAP_GUEST_JOB_H

#include "ctool.h"
#include "cupid.h"

typedef struct {
    cupid_syscall_table_t *sys;
    ctool_string_t input_prefix;
    const char *pending_path;
} cupid_guest_io_t;

/* The adapter and syscall table must outlive the job. Input paths are
 * relative to input_prefix. Output paths already name guest VFS files. */
ctool_job_config_t cupid_guest_job_config(cupid_guest_io_t *io);

#endif
