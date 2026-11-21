/**
 * @file cupidinstall_guest.cc
 * @brief Generate kernel installation sources from the guest build inventory.
 */

#include "cupidobj.h"
#include "guest_build.h"

#define INSTALL_JOB "/home/cupidinstall.job"
#define INSTALL_PENDING "/home/.cupidinstall-pending.cc"
#define INSTALL_PATH_LIMIT 512u
#define INSTALL_JOB_BYTES CUPID_GUEST_PATH_BYTES(INSTALL_PATH_LIMIT + 5u)

typedef struct {
    const char *name;
    const ctool_string_t **paths;
    ctool_u32 *count;
} install_group_t;

static int read_groups(char *cursor, char *end, const install_group_t *groups,
                       ctool_u32 group_count, ctool_string_t *paths) {
    ctool_u32 total = 0u;
    char *line = cupid_guest_next_job_line(&cursor, end);
    for (ctool_u32 group = 0u; group < group_count; group++) {
        if (!line || strcmp(line, groups[group].name) != 0)
            return 0;

        ctool_u32 start = total;
        line = NULL;
        while (cursor < end) {
            line = cupid_guest_next_job_line(&cursor, end);
            if (!line)
                return 0;
            if (line[0] == '-')
                break;
            ctool_path_t path = {ctool_string(line)};
            if (total == INSTALL_PATH_LIMIT || path.text.size <= 1u ||
                path.text.size >= VFS_MAX_PATH || !ctool_path_is_canonical(&path))
                return 0;
            paths[total++] = (ctool_string_t){path.text.data + 1u, path.text.size - 1u};
            line = NULL;
        }
        if (total != start) {
            *groups[group].paths = paths + start;
            *groups[group].count = total - start;
        }
    }
    return !line && total != 0u;
}

static int read_install_job(cupid_syscall_table_t *sys, const char *job_path,
                            char *file, ctool_path_t *output,
                            ctool_obj_install_source_request_t *install, ctool_string_t *paths) {
    int size = sys->vfs_read_text(job_path, file, INSTALL_JOB_BYTES);
    if (size < 1 || (ctool_u32)size >= INSTALL_JOB_BYTES)
        return 0;
    char *cursor = file;
    char *end = file + size;
    if (!cupid_guest_read_path_lines(sys, &cursor, end, output, 1u) ||
        output->text.size < sizeof("/home/x.cc") - 1u ||
        strcmp(output->text.data + output->text.size - 3u, ".cc") != 0 ||
        strcmp(output->text.data, INSTALL_PENDING) == 0)
        return 0;

    char *kind = cupid_guest_next_job_line(&cursor, end);
    if (!kind)
        return 0;
    if (strcmp(kind, "bin") == 0) {
        install->kind = CTOOL_OBJ_INSTALL_BIN;
        const install_group_t groups[] = {
            {"--bin", &install->bin_paths, &install->bin_count},
            {"--headers", &install->header_paths, &install->header_count},
            {"--browser", &install->browser_paths, &install->browser_count}
        };
        return read_groups(cursor, end, groups, 3u, paths);
    }
    if (strcmp(kind, "docs") == 0) {
        install->kind = CTOOL_OBJ_INSTALL_DOCS;
        const install_group_t groups[] = {
            {"--ctxt", &install->ctxt_paths, &install->ctxt_count},
            {"--doc-assets", &install->doc_asset_paths, &install->doc_asset_count},
            {"--home-assets", &install->home_asset_paths, &install->home_asset_count}
        };
        return read_groups(cursor, end, groups, 3u, paths);
    }
    if (strcmp(kind, "demos") == 0) {
        install->kind = CTOOL_OBJ_INSTALL_DEMOS;
        const install_group_t group = {"--demos", &install->demo_paths, &install->demo_count};
        return read_groups(cursor, end, &group, 1u, paths);
    }
    return 0;
}

static ctool_status_t publish_source(cupid_syscall_table_t *sys, const ctool_obj_request_t *request,
                                     const ctool_path_t *output) {
    cupid_guest_io_t io = {.sys = sys, .pending_path = INSTALL_PENDING};
    ctool_job_config_t config = cupid_guest_job_config(&io);
    ctool_job_t *job = NULL;
    ctool_status_t status = ctool_job_open(&config, &job);
    if (status == CTOOL_OK) {
        ctool_buffer_t *buffer = NULL;
        status = ctool_job_open_buffer(job, 4096u, config.limits.output_bytes, &buffer);
        if (status == CTOOL_OK) {
            ctool_obj_result_t result = {0};
            status = ctool_obj_transform(job, request, buffer, &result);
            if (status == CTOOL_OK)
                status = ctool_job_write(job, output, ctool_buffer_view(buffer));
            ctool_buffer_close(buffer);
        }
        if (status != CTOOL_OK && ctool_job_diagnostic_count(job) != 0u)
            ctool_job_render_diagnostics(job);
        ctool_job_close(job);
    }
    cupid_guest_job_drain_cache(&io);
    return status;
}

ctool_status_t cupid_guest_install_job(cupid_syscall_table_t *sys, const char *job_path) {
    if (!sys || sys->version != CUPID_SYSCALL_VERSION || sys->table_size < sizeof(*sys))
        return CTOOL_ERR_INVALID_ARGUMENT;
    cupid_init(sys);
    char file[INSTALL_JOB_BYTES];
    ctool_string_t paths[INSTALL_PATH_LIMIT];
    ctool_path_t output;
    ctool_source_t source = {.path = {ctool_string(job_path)}};
    ctool_obj_request_t request = {.operation = CTOOL_OBJ_GENERATE_INSTALL_SOURCE, .input = &source};
    if (!read_install_job(sys, job_path, file, &output, &request.as.install_source, paths)) {
        sys->serial_write_string("[cupidinstall-guest] invalid request file: ");
        sys->serial_write_string(job_path);
        sys->serial_write_string("\n");
        return CTOOL_ERR_INPUT;
    }

    ctool_status_t status = publish_source(sys, &request, &output);
    if (status == CTOOL_OK) {
        sys->serial_write_string("[cupidinstall-guest] wrote ");
        sys->serial_write_string(output.text.data);
    } else {
        sys->serial_write_string("[cupidinstall-guest] generation failed: ");
        sys->serial_write_string(ctool_status_name(status));
    }
    sys->serial_write_string("\n");
    return status;
}

void cupidinstall_guest_run(cupid_syscall_table_t *sys) {
    (void)cupid_guest_install_job(sys, INSTALL_JOB);
}
