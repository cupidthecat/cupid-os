/**
 * @file cupidbuild_guest.cc
 * @brief Run an ordered build plan inside Cupid OS.
 */

#include "guest_build.h"

#define BUILD_JOB "/home/cupidbuild.job"
#define BUILD_STEP_LIMIT 64u
#define BUILD_JOB_BYTES (BUILD_STEP_LIMIT * (VFS_MAX_PATH + sizeof("compile\n")))

typedef struct {
    ctool_status_t (*run)(cupid_syscall_table_t *sys, const char *job_path);
    const char *job_path;
} build_step_t;

static ctool_u32 read_plan(cupid_syscall_table_t *sys, char *text, build_step_t *steps) {
    int size = sys->vfs_read_text(BUILD_JOB, text, BUILD_JOB_BYTES);
    if (size <= 0 || (ctool_u32)size >= BUILD_JOB_BYTES)
        return 0u;

    char *cursor = text;
    char *end = text + size;
    ctool_u32 count = 0u;
    while (cursor < end && count < BUILD_STEP_LIMIT) {
        const char *operation = cupid_guest_next_job_line(&cursor, end);
        const char *name = cupid_guest_next_job_line(&cursor, end);
        if (!operation || !name)
            return 0u;

        ctool_path_t path = {ctool_string(name)};
        if (path.text.size <= 1u || path.text.size >= VFS_MAX_PATH || !ctool_path_is_canonical(&path))
            return 0u;

        if (strcmp(operation, "compile") == 0) {
            steps[count++] = (build_step_t){cupid_guest_compile_job, name};
        } else if (strcmp(operation, "link") == 0) {
            steps[count++] = (build_step_t){cupid_guest_link_job, name};
        } else {
            return 0u;
        }
    }
    return cursor == end ? count : 0u;
}

void cupidbuild_guest_run(cupid_syscall_table_t *sys) {
    if (!sys || sys->version != CUPID_SYSCALL_VERSION || sys->table_size < sizeof(*sys))
        return;
    cupid_init(sys);

    char text[BUILD_JOB_BYTES];
    build_step_t steps[BUILD_STEP_LIMIT];
    ctool_u32 count = read_plan(sys, text, steps);
    if (count == 0u) {
        sys->serial_write_string("[cupidbuild-guest] invalid request file: " BUILD_JOB "\n");
        return;
    }
    for (ctool_u32 index = 0; index < count; index++) {
        if (steps[index].run(sys, steps[index].job_path) != CTOOL_OK) {
            sys->serial_write_string("[cupidbuild-guest] stopped at ");
            sys->serial_write_string(steps[index].job_path);
            sys->serial_write_string("\n");
            return;
        }
    }
    sys->serial_write_string("[cupidbuild-guest] build complete\n");
}
