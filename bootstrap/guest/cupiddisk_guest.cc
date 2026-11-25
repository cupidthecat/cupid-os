/**
 * @file cupiddisk_guest.cc
 * @brief Write a bootable disk prefix or complete image from a guest job.
 */

#include "cupidobj.h"
#include "guest_build.h"
#include "fat16_names.h"

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

typedef struct {
    cupid_syscall_table_t *sys;
    const char *list;
    ctool_u32 sectors;
    ctool_allocator_t allocator;
} disk_staging_t;

typedef struct {
    cupid_syscall_table_t *sys;
    int fd;
} disk_file_t;

const char *cupid_guest_disk_stage_list(cupid_syscall_table_t *sys, const char *option) {
    if (sys->strncmp(option, "stage ", 6u) != 0)
        return NULL;
    ctool_path_t path = {ctool_string(option + 6u)};
    return path.text.size < VFS_MAX_PATH && ctool_path_is_canonical(&path) ? path.text.data : NULL;
}

static ctool_status_t disk_read_at(void *context, ctool_u32 offset, ctool_u8 *bytes, ctool_u32 size) {
    disk_file_t *file = context;
    if (file->sys->vfs_seek(file->fd, (int)offset, SEEK_SET) != (int)offset)
        return CTOOL_ERR_IO;
    while (size) {
        int count = file->sys->vfs_read(file->fd, bytes, size);
        if (count <= 0 || (ctool_u32)count > size)
            return CTOOL_ERR_IO;
        bytes += count;
        size -= (ctool_u32)count;
    }
    return CTOOL_OK;
}

static ctool_status_t disk_read_sector(void *context, ctool_u32 sector, ctool_u8 *bytes) {
    return disk_read_at(context, sector * 512u, bytes, 512u);
}

static ctool_status_t disk_write_sector(void *context, ctool_u32 sector, const ctool_u8 *bytes) {
    disk_file_t *file = context;
    int offset = (int)(sector * 512u);
    if (file->sys->vfs_seek(file->fd, offset, SEEK_SET) != offset ||
        file->sys->vfs_write(file->fd, bytes, 512u) != 512)
        return CTOOL_ERR_IO;
    return CTOOL_OK;
}

static ctool_status_t stage_asset(cupid_syscall_table_t *sys, ctool_fat16_volume_t *volume,
                                  const char *input, const char *destination) {
    ctool_path_t source = {ctool_string(input)};
    ctool_string_t target = ctool_string(destination);
    if (source.text.size >= VFS_MAX_PATH || !ctool_path_is_canonical(&source) ||
        sys->strcmp(input, DISK_PENDING) == 0 || target.size >= VFS_MAX_PATH)
        return CTOOL_ERR_PATH;

    ctool_fat16_name_t names[(VFS_MAX_PATH - 1u) / 2u];
    ctool_u32 count;
    ctool_status_t status = ctool_fat16_project_destination(
        target, CTOOL_FAT16_NAMES_UNICODE_16, names, sizeof(names) / sizeof(names[0]), &count);
    if (status != CTOOL_OK)
        return status;

    cupid_stat_t info;
    int found = sys->vfs_stat(input, &info);
    if (found < 0)
        return found == -2 ? CTOOL_ERR_NOT_FOUND : CTOOL_ERR_IO;
    if (info.type != VFS_TYPE_FILE)
        return CTOOL_ERR_INPUT;
    if (info.size > 0x7fffffffu)
        return CTOOL_ERR_LIMIT;
    disk_file_t payload_file = {sys, sys->vfs_open(input, O_RDONLY)};
    if (payload_file.fd < 0)
        return CTOOL_ERR_IO;
    ctool_fat16_payload_t payload = {&payload_file, info.size, disk_read_at};
    status = ctool_fat16_stage(volume, names, count, payload);
    sys->vfs_close(payload_file.fd);
    return status;
}

static ctool_status_t stage_image(void *context, int fd) {
    disk_staging_t *staging = context;
    cupid_syscall_table_t *sys = staging->sys;
    char *list = sys->malloc(CUPID_GUEST_OBJECT_LIST_BYTES);
    if (!list)
        return CTOOL_ERR_NO_MEMORY;
    int size = sys->vfs_read_text(staging->list, list, CUPID_GUEST_OBJECT_LIST_BYTES);
    ctool_status_t status = CTOOL_ERR_INPUT;
    ctool_fat16_volume_t *volume = NULL;
    if (size > 0 && (ctool_u32)size < CUPID_GUEST_OBJECT_LIST_BYTES) {
        disk_file_t candidate = {sys, fd};
        ctool_fat16_store_t store = {&candidate, staging->sectors, disk_read_sector, disk_write_sector};
        status = ctool_fat16_open(staging->allocator, store, DISK_FAT_START, &volume);
        char *cursor = list;
        char *end = list + size;
        while (status == CTOOL_OK && cursor < end) {
            const char *source = cupid_guest_next_job_line(&cursor, end);
            const char *target = cupid_guest_next_job_line(&cursor, end);
            status = source && target ? stage_asset(sys, volume, source, target) : CTOOL_ERR_INPUT;
        }
        if (volume)
            ctool_fat16_close(volume);
    } else if (size == -2) {
        status = CTOOL_ERR_NOT_FOUND;
    }
    sys->free(list);
    return status;
}

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
                             char *file, ctool_u32 capacity, ctool_path_t *paths, ctool_u32 *extent,
                             const char **stages) {
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
    if (!option || strncmp(paths[2].text.data, "/home/", 6u) != 0 ||
        !cupid_guest_disk_image_extent(sys, option, extent))
        return 0;
    if (cursor == end)
        return 1;
    option = cupid_guest_next_job_line(&cursor, end);
    *stages = option ? cupid_guest_disk_stage_list(sys, option) : NULL;
    return *stages && cursor == end && sys->strcmp(*stages, DISK_PENDING) != 0;
}

ctool_status_t cupid_guest_disk_job(cupid_syscall_table_t *sys, const cupid_guest_request_t *job) {
    if (!sys || sys->version != CUPID_SYSCALL_VERSION || sys->table_size < sizeof(*sys))
        return CTOOL_ERR_INVALID_ARGUMENT;
    cupid_init(sys);
    char file[CUPID_GUEST_PATH_BYTES(4u) + sizeof("image 2047\nstage ")];
    ctool_path_t paths[3];
    ctool_u32 extent = 0u;
    const char *stages = NULL;
    if (!read_disk_request(sys, job, file, sizeof(file), paths, &extent, &stages)) {
        sys->serial_write_string("[cupiddisk-guest] invalid request\n");
        return CTOOL_ERR_INPUT;
    }

    cupid_guest_io_t io = {
        .sys = sys, .pending_path = DISK_PENDING, .output_extent = extent
    };
    ctool_job_config_t config = cupid_guest_job_config(&io);
    disk_staging_t staging = {sys, stages, extent / 512u, config.allocator};
    if (stages) {
        if (!sys->vfs_read)
            return CTOOL_ERR_UNSUPPORTED;
        io.prepare_output = stage_image;
        io.output_context = &staging;
    }
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
