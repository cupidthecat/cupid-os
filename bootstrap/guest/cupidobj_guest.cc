#include "cupidobj.h"
#include "guest_job.h"

#define OBJECT_JOB "/home/cupidobj.job"
#define OBJECT_PENDING "/home/.cupidobj-pending.bin"
#define OBJECT_SOURCE_BYTES (16u * 1024u * 1024u)
#define OBJECT_ARENA_BYTES (64u * 1024u * 1024u)

static ctool_status_t flatten(ctool_invocation_t *invocation, void *context) {
    (void)context;
    ctool_obj_request_t request = {
        .operation = CTOOL_OBJ_EXTRACT_FLAT, .input = invocation->input
    };
    ctool_obj_result_t result = {0};
    return ctool_obj_transform(invocation->job, &request, invocation->output, &result);
}

void cupidobj_guest_run(cupid_syscall_table_t *sys) {
    if (!sys || sys->version != CUPID_SYSCALL_VERSION || sys->table_size < sizeof(*sys))
        return;
    cupid_init(sys);
    char file[CUPID_GUEST_PATH_JOB_BYTES];
    ctool_invocation_request_t request;
    if (!cupid_guest_read_path_job(sys, OBJECT_JOB, file, &request) ||
        strcmp(request.output_path.data, OBJECT_PENDING) == 0) {
        sys->serial_write_string("[cupidobj-guest] invalid request file: " OBJECT_JOB "\n");
        return;
    }

    cupid_guest_io_t io = {
        .sys = sys, .input_prefix = ctool_string(""), .pending_path = OBJECT_PENDING
    };
    ctool_job_config_t config = cupid_guest_job_config(&io);
    config.limits.source_bytes = OBJECT_SOURCE_BYTES;
    config.limits.arena_bytes = OBJECT_ARENA_BYTES;
    ctool_invocation_result_t result = {0};
    ctool_status_t status = ctool_invoke(&config, &request, flatten, NULL, &result);
    cupid_guest_job_drain_cache(&io);
    if (status == CTOOL_OK && result.output_committed) {
        sys->serial_write_string("[cupidobj-guest] wrote ");
        sys->serial_write_string(request.output_path.data);
    } else {
        sys->serial_write_string("[cupidobj-guest] flatten failed: ");
        sys->serial_write_string(ctool_status_name(status));
    }
    sys->serial_write_string("\n");
}
