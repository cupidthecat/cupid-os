#ifndef CUPID_BOOTSTRAP_GUEST_JOB_H
#define CUPID_BOOTSTRAP_GUEST_JOB_H

#include "ctool.h"
#include "cupid.h"

#define CUPID_GUEST_CACHE_SLOTS 64u
#define CUPID_GUEST_PATH_JOB_BYTES (2u * VFS_MAX_PATH + 1u)

typedef struct {
    void *allocation;
    ctool_u32 size;
} cupid_guest_cached_allocation_t;

typedef struct {
    cupid_syscall_table_t *sys;
    ctool_string_t input_prefix;
    const char *pending_path;
    ctool_u32 cached_bytes;
    cupid_guest_cached_allocation_t cache[CUPID_GUEST_CACHE_SLOTS];
} cupid_guest_io_t;

/* The adapter and syscall table must outlive the job. Input paths are
 * relative to input_prefix. Output paths already name guest VFS files. */
ctool_job_config_t cupid_guest_job_config(cupid_guest_io_t *io);

/* Read two canonical paths. The output is under /home. Views borrow file. */
int cupid_guest_read_path_job(cupid_syscall_table_t *sys, const char *job_path,
                              char *file, ctool_invocation_request_t *invocation);

/* Return cached, released blocks. Live allocations still belong to the job. */
void cupid_guest_job_drain_cache(cupid_guest_io_t *io);

#endif
