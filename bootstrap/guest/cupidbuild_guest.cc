/**
 * @file cupidbuild_guest.cc
 * @brief Run an ordered build plan inside Cupid OS.
 */

#include "guest_build.h"

#define BUILD_JOB "/home/cupidbuild.job"
#define MEDIA_ROOT "/plan/"
#define MEDIA_JOB MEDIA_ROOT "cupidbuild.job"
#define MEDIA_REQUESTS MEDIA_ROOT "jobs/"
#define BUILD_STEP_LIMIT 512u
#define BUILD_JOB_BYTES (BUILD_STEP_LIMIT * (VFS_MAX_PATH + sizeof("assemble")) + 1u)

typedef struct {
    cupid_guest_operation_t run;
    const char *job_path;
} build_step_t;

typedef struct {
    char text[BUILD_JOB_BYTES];
    build_step_t steps[BUILD_STEP_LIMIT];
} build_plan_t;

static cupid_guest_operation_t find_operation(const char *name, int media) {
    if (media && strcmp(name, "kernel") == 0)
        return cupid_guest_kernel_job;
    static const struct {
        const char *name;
        cupid_guest_operation_t run;
    } operations[] = {
        {"compile", cupid_guest_compile_job},
        {"link", cupid_guest_link_job},
        {"symbols", cupid_guest_symbols_job},
        {"object", cupid_guest_object_job},
        {"raw", cupid_guest_raw_job},
        {"assemble", cupid_guest_assemble_job},
        {"install", cupid_guest_install_job},
        {"disk", cupid_guest_disk_job},
    };
    for (ctool_u32 index = 0; index < sizeof(operations) / sizeof(operations[0]); index++) {
        if (strcmp(name, operations[index].name) == 0)
            return operations[index].run;
    }
    return NULL;
}

static ctool_u32 read_plan(cupid_syscall_table_t *sys, const char *job_path,
                           const char *request_root, build_plan_t *plan) {
    int size = sys->vfs_read_text(job_path, plan->text, BUILD_JOB_BYTES);
    if (size <= 0 || (ctool_u32)size >= BUILD_JOB_BYTES)
        return 0u;

    char *cursor = plan->text;
    char *end = cursor + size;
    size_t root_size = request_root ? strlen(request_root) : 0u;
    ctool_u32 count = 0u;
    while (cursor < end && count < BUILD_STEP_LIMIT) {
        const char *operation = cupid_guest_next_job_line(&cursor, end);
        const char *name = cupid_guest_next_job_line(&cursor, end);
        if (!operation || !name)
            return 0u;

        ctool_path_t path = {ctool_string(name)};
        if (path.text.size <= 1u || path.text.size >= VFS_MAX_PATH || !ctool_path_is_canonical(&path))
            return 0u;
        if (request_root && (path.text.size <= root_size || strncmp(name, request_root, root_size) != 0))
            return 0u;

        cupid_guest_operation_t run = find_operation(operation, request_root != NULL);
        if (!run)
            return 0u;
        plan->steps[count++] = (build_step_t){run, name};
    }
    return cursor == end ? count : 0u;
}

static void execute_plan(cupid_syscall_table_t *sys, const char *job_path,
                         const char *request_root, build_plan_t *plan) {
    ctool_u32 count = read_plan(sys, job_path, request_root, plan);
    if (count == 0u) {
        sys->serial_write_string("[cupidbuild-guest] invalid request file: ");
        sys->serial_write_string(job_path);
        sys->serial_write_string("\n");
        return;
    }
    for (ctool_u32 index = 0; index < count; index++) {
        const build_step_t *step = &plan->steps[index];
        cupid_guest_request_t request = {.path = step->job_path};
        if (step->run(sys, &request) != CTOOL_OK) {
            sys->serial_write_string("[cupidbuild-guest] stopped at ");
            sys->serial_write_string(step->job_path);
            sys->serial_write_string("\n");
            return;
        }
    }
    sys->serial_write_string("[cupidbuild-guest] build complete\n");
}

static void run_build(cupid_syscall_table_t *sys, const char *job_path, const char *request_root) {
    if (!sys || sys->version != CUPID_SYSCALL_VERSION || sys->table_size < sizeof(*sys))
        return;
    cupid_init(sys);
    build_plan_t *plan = sys->malloc(sizeof(*plan));
    if (!plan) {
        sys->serial_write_string("[cupidbuild-guest] could not allocate the build plan\n");
        return;
    }
    execute_plan(sys, job_path, request_root, plan);
    sys->free(plan);
}

void cupidbuild_guest_run(cupid_syscall_table_t *sys) {
    run_build(sys, BUILD_JOB, NULL);
}

void cupidbuild_media_run(cupid_syscall_table_t *sys) {
    run_build(sys, MEDIA_JOB, MEDIA_REQUESTS);
}
