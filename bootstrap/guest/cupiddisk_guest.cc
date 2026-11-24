/**
 * @file cupiddisk_guest.cc
 * @brief Write a bootable disk prefix or complete image from a guest job.
 */

#include "cupidobj.h"
#include "guest_build.h"

#define DISK_JOB "/home/cupiddisk.job"
#define DISK_PENDING "/home/.cupiddisk-pending.img"
#define DISK_SOURCE_BYTES (16u * 1024u * 1024u)
#define DISK_ARENA_BYTES (64u * 1024u * 1024u)
#define DISK_IMAGE_SECTORS 409600u
#define DISK_FAT_START 20480u
#define DISK_MIB (1024u * 1024u)
#define DISK_MAX_MIB (0x7fffffffu / DISK_MIB)

typedef struct {
    ctool_path_t kernel;
    ctool_u32 sectors;
} disk_inputs_t;

int cupid_guest_disk_image_extent(cupid_syscall_table_t *sys, const char *option, ctool_u32 *extent) {
    if (sys->strncmp(option, "image ", 6u) != 0)
        return 0;
    ctool_u32 mib = 0u;
    for (const char *digit = option + 6u; *digit; digit++) {
        if (*digit < '0' || *digit > '9')
            return 0;
        mib = mib * 10u + (ctool_u32)(*digit - '0');
        if (mib > DISK_MAX_MIB)
            return 0;
    }
    if (mib <= DISK_FAT_START / (DISK_MIB / 512u))
        return 0;
    *extent = mib * DISK_MIB;
    return 1;
}

static ctool_status_t make_template(ctool_invocation_t *invocation, void *context) {
    const disk_inputs_t *inputs = context;
    ctool_source_t kernel;
    ctool_status_t status = ctool_job_load_source(invocation->job, &inputs->kernel, &kernel);
    if (status != CTOOL_OK)
        return status;
    ctool_obj_disk_template_request_t geometry = {&kernel, inputs->sectors, DISK_FAT_START};
    ctool_obj_request_t request = {
        .operation = CTOOL_OBJ_BUILD_DISK_TEMPLATE, .input = invocation->input
    };
    request.as.disk_template = geometry;
    ctool_obj_result_t result = {0};
    return ctool_obj_transform(invocation->job, &request, invocation->output, &result);
}

static int read_disk_request(cupid_syscall_table_t *sys, const cupid_guest_request_t *job,
                             char *file, ctool_u32 capacity, ctool_path_t *paths, ctool_u32 *extent) {
    int size = cupid_guest_read_request(sys, job, file, capacity);
    if (size <= 0 || (ctool_u32)size >= capacity)
        return 0;
    char *cursor = file;
    char *end = file + size;
    if (!cupid_guest_read_path_lines(sys, &cursor, end, paths, 3u) ||
        strcmp(paths[2].text.data, DISK_PENDING) == 0)
        return 0;
    if (cursor == end)
        return 1;
    const char *option = cupid_guest_next_job_line(&cursor, end);
    return option && cursor == end && strncmp(paths[2].text.data, "/home/", 6u) == 0 &&
           cupid_guest_disk_image_extent(sys, option, extent);
}

ctool_status_t cupid_guest_disk_job(cupid_syscall_table_t *sys, const cupid_guest_request_t *job) {
    if (!sys || sys->version != CUPID_SYSCALL_VERSION || sys->table_size < sizeof(*sys))
        return CTOOL_ERR_INVALID_ARGUMENT;
    cupid_init(sys);
    char file[CUPID_GUEST_PATH_BYTES(3u) + sizeof("image 2047")];
    ctool_path_t paths[3];
    ctool_u32 extent = 0u;
    if (!read_disk_request(sys, job, file, sizeof(file), paths, &extent)) {
        sys->serial_write_string("[cupiddisk-guest] invalid request\n");
        return CTOOL_ERR_INPUT;
    }

    cupid_guest_io_t io = {
        .sys = sys, .pending_path = DISK_PENDING, .output_extent = extent
    };
    ctool_job_config_t config = cupid_guest_job_config(&io);
    config.limits.source_bytes = DISK_SOURCE_BYTES;
    config.limits.arena_bytes = DISK_ARENA_BYTES;
    ctool_invocation_request_t request = {paths[0].text, paths[2].text};
    ctool_invocation_result_t result = {0};
    disk_inputs_t inputs = {paths[1], extent ? extent / 512u : DISK_IMAGE_SECTORS};
    ctool_status_t status = ctool_invoke(&config, &request, make_template, &inputs, &result);
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
