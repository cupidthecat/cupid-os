/**
 * @file cupiddisk_guest.cc
 * @brief Write a bootable disk template from a guest job.
 */

#include "cupidobj.h"
#include "guest_build.h"

#define DISK_JOB "/home/cupiddisk.job"
#define DISK_PENDING "/home/.cupiddisk-pending.img"
#define DISK_SOURCE_BYTES (16u * 1024u * 1024u)
#define DISK_ARENA_BYTES (64u * 1024u * 1024u)
#define DISK_IMAGE_SECTORS 409600u
#define DISK_FAT_START 20480u

static ctool_status_t make_template(ctool_invocation_t *invocation, void *context) {
    const ctool_path_t *kernel_path = context;
    ctool_source_t kernel;
    ctool_status_t status = ctool_job_load_source(invocation->job, kernel_path, &kernel);
    if (status != CTOOL_OK)
        return status;
    ctool_obj_disk_template_request_t geometry = {&kernel, DISK_IMAGE_SECTORS, DISK_FAT_START};
    ctool_obj_request_t request = {
        .operation = CTOOL_OBJ_BUILD_DISK_TEMPLATE, .input = invocation->input
    };
    request.as.disk_template = geometry;
    ctool_obj_result_t result = {0};
    return ctool_obj_transform(invocation->job, &request, invocation->output, &result);
}

ctool_status_t cupid_guest_disk_job(cupid_syscall_table_t *sys, const cupid_guest_request_t *job) {
    if (!sys || sys->version != CUPID_SYSCALL_VERSION || sys->table_size < sizeof(*sys))
        return CTOOL_ERR_INVALID_ARGUMENT;
    cupid_init(sys);
    char file[CUPID_GUEST_PATH_BYTES(3u)];
    ctool_path_t paths[3];
    if (!cupid_guest_read_paths(sys, job, file, sizeof(file), paths, 3u) ||
        strcmp(paths[2].text.data, DISK_PENDING) == 0) {
        sys->serial_write_string("[cupiddisk-guest] invalid request\n");
        return CTOOL_ERR_INPUT;
    }

    cupid_guest_io_t io = {
        .sys = sys, .input_prefix = ctool_string(""), .pending_path = DISK_PENDING
    };
    ctool_job_config_t config = cupid_guest_job_config(&io);
    config.limits.source_bytes = DISK_SOURCE_BYTES;
    config.limits.arena_bytes = DISK_ARENA_BYTES;
    ctool_invocation_request_t request = {paths[0].text, paths[2].text};
    ctool_invocation_result_t result = {0};
    ctool_status_t status = ctool_invoke(&config, &request, make_template, &paths[1], &result);
    cupid_guest_job_drain_cache(&io);
    if (status == CTOOL_OK && !result.output_committed)
        status = CTOOL_ERR_IO;
    if (status == CTOOL_OK) {
        sys->serial_write_string("[cupiddisk-guest] wrote ");
        sys->serial_write_string(request.output_path.data);
    } else {
        sys->serial_write_string("[cupiddisk-guest] template failed: ");
        sys->serial_write_string(ctool_status_name(status));
    }
    sys->serial_write_string("\n");
    return status;
}

void cupiddisk_guest_run(cupid_syscall_table_t *sys) {
    cupid_guest_disk_job(sys, &(cupid_guest_request_t){.path = DISK_JOB});
}
