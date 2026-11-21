/**
 * @file cupidld_guest.cc
 * @brief Link guest objects using the job's address or linker script.
 */

#include "cupidld.h"
#include "guest_job.h"

#define LINK_JOB "/home/cupidld.job"
#define LINK_PENDING "/home/.cupidld-pending.elf"
#define LINK_MAX_OBJECTS 512u
#define LINK_JOB_BYTES ((LINK_MAX_OBJECTS + 3u) * VFS_MAX_PATH)
#define LINK_EXTERNAL_BASE 0x01c00000u
#define LINK_WORKER_BASE 0x01e00000u
#define LINK_SCRIPT_SPAN (16u * 1024u * 1024u)
#define LINK_SCRIPT_ARENA (128u * 1024u * 1024u)

typedef struct {
    ctool_path_t output;
    ctool_path_t script;
    ctool_ld_fixed_text_layout_t fixed_text;
    ctool_u32 maximum_span;
    ctool_path_t inputs[LINK_MAX_OBJECTS];
    ctool_u32 count;
} guest_link_job_t;

static int read_link_job(char *file, guest_link_job_t *job) {
    int bytes = __sys->vfs_read_text(LINK_JOB, file, LINK_JOB_BYTES);
    if (bytes <= 0 || (ctool_u32)bytes >= LINK_JOB_BYTES)
        return 0;

    char *cursor = file;
    char *end = file + bytes;
    char *output = cupid_guest_next_job_line(&cursor, end);
    char *layout = cupid_guest_next_job_line(&cursor, end);
    char *argument = cupid_guest_next_job_line(&cursor, end);
    if (!output || !layout || !argument)
        return 0;

    job->output.text = ctool_string(output);
    if (!ctool_path_is_canonical(&job->output) || strncmp(output, "/home/", 6u) != 0 ||
        strcmp(output, LINK_PENDING) == 0 || job->output.text.size >= VFS_MAX_PATH ||
        job->output.text.size <= 10u ||
        strcmp(output + job->output.text.size - 4u, ".elf") != 0)
        return 0;

    if (strcmp(layout, "script") == 0) {
        job->script.text = ctool_string(argument);
        if (!ctool_path_is_canonical(&job->script) || job->script.text.size >= VFS_MAX_PATH)
            return 0;
        job->maximum_span = LINK_SCRIPT_SPAN;
    } else {
        if (strcmp(layout, "0x01c00000") == 0) {
            job->fixed_text.base_address = LINK_EXTERNAL_BASE;
            job->maximum_span = LINK_WORKER_BASE - LINK_EXTERNAL_BASE;
        } else if (strcmp(layout, "0x01e00000") == 0) {
            job->fixed_text.base_address = LINK_WORKER_BASE;
            job->maximum_span = 0x02200000u - LINK_WORKER_BASE;
        } else {
            return 0;
        }
        job->fixed_text.entry_symbol = ctool_string(argument);
        if (job->fixed_text.entry_symbol.size >= VFS_MAX_PATH)
            return 0;
    }

    job->count = 0;
    while (cursor < end) {
        char *path = cupid_guest_next_job_line(&cursor, end);
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
    cupid_guest_io_t io = {
        .sys = sys, .input_prefix = ctool_string(""), .pending_path = LINK_PENDING
    };
    ctool_job_config_t config = cupid_guest_job_config(&io);
    if (link->script.text.size)
        config.limits.arena_bytes = LINK_SCRIPT_ARENA;

    ctool_job_t *job = NULL;
    ctool_status_t status = ctool_job_open(&config, &job);
    if (status != CTOOL_OK) {
        cupid_guest_job_drain_cache(&io);
        return status;
    }

    ctool_source_t objects[LINK_MAX_OBJECTS];
    ctool_source_t script;
    ctool_buffer_t *buffer = NULL;
    ctool_ld_request_t request = {0};
    request.objects = objects;
    request.object_count = link->count;
    request.image_kind = CTOOL_LD_IMAGE_ELF32;
    request.maximum_image_span = link->maximum_span;
    if (link->script.text.size) {
        status = ctool_job_load_source(job, &link->script, &script);
        request.layout.kind = CTOOL_LD_LAYOUT_SCRIPT;
        request.layout.as.script = &script;
    } else {
        request.layout.kind = CTOOL_LD_LAYOUT_FIXED_TEXT;
        request.layout.as.fixed_text = link->fixed_text;
    }
    for (ctool_u32 i = 0; i < link->count && status == CTOOL_OK; i++)
        status = ctool_job_load_source(job, &link->inputs[i], &objects[i]);
    if (status == CTOOL_OK)
        status = ctool_job_open_buffer(job, 4096u, config.limits.output_bytes, &buffer);

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
    cupid_guest_job_drain_cache(&io);
    return status;
}

void cupidld_guest_run(cupid_syscall_table_t *sys) {
    if (!sys || sys->version != CUPID_SYSCALL_VERSION || sys->table_size < sizeof(*sys))
        return;
    cupid_init(sys);
    char file[LINK_JOB_BYTES];
    guest_link_job_t link = {0};
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
