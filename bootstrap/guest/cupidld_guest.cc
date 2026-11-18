#include "cupidld.h"
#include "guest_job.h"

#define LINK_JOB "/home/cupidld.job"
#define LINK_PENDING "/home/.cupidld-pending.elf"
#define LINK_MAX_OBJECTS 512u
#define LINK_JOB_BYTES ((LINK_MAX_OBJECTS + 3u) * VFS_MAX_PATH)
#define LINK_EXTERNAL_BASE 0x01c00000u
#define LINK_WORKER_BASE 0x01e00000u

typedef struct {
    ctool_path_t output;
    ctool_ld_fixed_text_layout_t layout;
    ctool_u32 maximum_span;
    ctool_path_t inputs[LINK_MAX_OBJECTS];
    ctool_u32 count;
} guest_link_job_t;

static char *next_line(char **cursor, char *end) {
    char *line = *cursor;
    if (line >= end)
        return NULL;
    char *stop = line;
    while (stop < end && *stop != '\n') {
        if (*stop == '\r' || *stop == '\0')
            return NULL;
        stop++;
    }
    if (stop == line)
        return NULL;
    *stop = '\0';
    *cursor = stop < end ? stop + 1 : stop;
    return line;
}

static int read_link_job(char *file, guest_link_job_t *job) {
    int bytes = __sys->vfs_read_text(LINK_JOB, file, LINK_JOB_BYTES);
    if (bytes <= 0)
        return 0;

    char *cursor = file;
    char *end = file + bytes;
    char *output = next_line(&cursor, end);
    char *base = next_line(&cursor, end);
    char *symbol = next_line(&cursor, end);
    if (!output || !base || !symbol)
        return 0;

    job->output.text = ctool_string(output);
    if (!ctool_path_is_canonical(&job->output) || strncmp(output, "/home/", 6u) != 0 ||
        strcmp(output, LINK_PENDING) == 0 || job->output.text.size >= VFS_MAX_PATH ||
        job->output.text.size <= 10u ||
        strcmp(output + job->output.text.size - 4u, ".elf") != 0)
        return 0;

    if (strcmp(base, "0x01c00000") == 0) {
        job->layout.base_address = LINK_EXTERNAL_BASE;
        job->maximum_span = LINK_WORKER_BASE - LINK_EXTERNAL_BASE;
    } else if (strcmp(base, "0x01e00000") == 0) {
        job->layout.base_address = LINK_WORKER_BASE;
        job->maximum_span = 0x02200000u - LINK_WORKER_BASE;
    } else {
        return 0;
    }
    job->layout.entry_symbol = ctool_string(symbol);
    if (job->layout.entry_symbol.size >= VFS_MAX_PATH)
        return 0;

    job->count = 0;
    while (cursor < end) {
        char *path = next_line(&cursor, end);
        if (!path || job->count >= LINK_MAX_OBJECTS)
            return 0;
        ctool_path_t item = {ctool_string(path)};
        if (!ctool_path_is_canonical(&item) || item.text.size >= VFS_MAX_PATH)
            return 0;
        job->inputs[job->count++] = item;
    }
    return job->count != 0;
}

static ctool_status_t link_image(cupid_syscall_table_t *sys, const guest_link_job_t *link) {
    cupid_guest_io_t io = {sys, ctool_string(""), LINK_PENDING};
    ctool_job_config_t config = cupid_guest_job_config(&io);

    ctool_job_t *job = NULL;
    ctool_status_t status = ctool_job_open(&config, &job);
    if (status != CTOOL_OK)
        return status;

    ctool_source_t objects[LINK_MAX_OBJECTS];
    ctool_buffer_t *buffer = NULL;
    for (ctool_u32 i = 0; i < link->count && status == CTOOL_OK; i++)
        status = ctool_job_load_source(job, &link->inputs[i], &objects[i]);
    if (status == CTOOL_OK)
        status = ctool_job_open_buffer(job, 4096u, config.limits.output_bytes, &buffer);

    ctool_ld_request_t request = {0};
    request.objects = objects;
    request.object_count = link->count;
    request.image_kind = CTOOL_LD_IMAGE_ELF32;
    request.layout.kind = CTOOL_LD_LAYOUT_FIXED_TEXT;
    request.layout.as.fixed_text = link->layout;
    request.maximum_image_span = link->maximum_span;
    ctool_ld_result_t result = {0};
    if (status == CTOOL_OK)
        status = ctool_ld_link(job, &request, buffer, &result);
    if (status == CTOOL_OK)
        status = ctool_job_write(job, &link->output, ctool_buffer_view(buffer));
    if (status != CTOOL_OK && ctool_job_diagnostic_count(job) > 0u)
        ctool_job_render_diagnostics(job);
    if (buffer)
        ctool_buffer_close(buffer);
    ctool_job_close(job);
    return status;
}

void cupidld_guest_run(cupid_syscall_table_t *sys) {
    if (!sys || sys->version != CUPID_SYSCALL_VERSION || sys->table_size < sizeof(*sys))
        return;
    cupid_init(sys);
    char file[LINK_JOB_BYTES];
    guest_link_job_t link;
    if (!read_link_job(file, &link)) {
        sys->serial_write_string("[cupidld-guest] invalid request\n");
        return;
    }
    ctool_status_t status = link_image(sys, &link);
    if (status == CTOOL_OK) {
        sys->serial_write_string("[cupidld-guest] wrote ");
        sys->serial_write_string(link.output.text.data);
        sys->serial_write_string("\n");
    } else {
        sys->serial_write_string("[cupidld-guest] link failed: ");
        sys->serial_write_string(ctool_status_name(status));
        sys->serial_write_string("\n");
    }
}
