/**
 * @file cupidbuild_guest.cc
 * @brief Run an ordered build plan inside Cupid OS.
 */

#include "guest_build.h"

#define BUILD_PLAN_NAME "cupidbuild.job"
#define BUILD_JOB "/home/" BUILD_PLAN_NAME
#define MEDIA_ROOT "/plan/"
#define MEDIA_JOB MEDIA_ROOT BUILD_PLAN_NAME
#define MEDIA_REQUESTS MEDIA_ROOT "jobs/"
#define BUILD_PLAN_MISSING (-2)
#define BUILD_STEP_LIMIT 512u
#define BUILD_JOB_BYTES (BUILD_STEP_LIMIT * (VFS_MAX_PATH + sizeof("compile-list")) + 1u)

typedef struct {
    cupid_guest_operation_t run;
    const char *job_path;
} build_step_t;

typedef struct {
    char text[BUILD_JOB_BYTES];
    build_step_t steps[BUILD_STEP_LIMIT];
} build_plan_t;

static cupid_guest_operation_t find_operation(const char *name) {
    static const struct {
        const char *name;
        cupid_guest_operation_t run;
    } operations[] = {
        {"compile", cupid_guest_compile_job},
        {"compile-list", cupid_guest_compile_list},
        {"link", cupid_guest_link_job},
        {"symbols", cupid_guest_symbols_job},
        {"object", cupid_guest_object_job},
        {"asset-list", cupid_guest_asset_list},
        {"raw", cupid_guest_raw_job},
        {"assemble", cupid_guest_assemble_job},
        {"install", cupid_guest_install_job},
        {"disk", cupid_guest_disk_job},
        {"kernel", cupid_guest_kernel_job},
    };
    for (ctool_u32 index = 0; index < sizeof(operations) / sizeof(operations[0]); index++) {
        if (strcmp(name, operations[index].name) == 0)
            return operations[index].run;
    }
    return NULL;
}

static const char *job_path(const char *directory, const char *name, char absolute[VFS_MAX_PATH]) {
    if (!directory || name[0] == '/')
        return name;
    size_t prefix = strlen(directory);
    size_t length = strlen(name);
    size_t separator = prefix > 1u;
    if (prefix + separator + length >= VFS_MAX_PATH)
        return NULL;
    memcpy(absolute, directory, prefix);
    if (separator)
        absolute[prefix++] = '/';
    memcpy(absolute + prefix, name, length + 1u);
    return absolute;
}

static ctool_u32 read_plan(int size, const char *request_root, const char *directory,
                           build_plan_t *plan) {
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

        char absolute[VFS_MAX_PATH];
        const char *resolved = job_path(directory, name, absolute);
        if (!resolved)
            return 0u;
        ctool_path_t path = {ctool_string(resolved)};
        if (path.text.size <= 1u || path.text.size >= VFS_MAX_PATH || !ctool_path_is_canonical(&path))
            return 0u;
        if (request_root && (path.text.size <= root_size || strncmp(name, request_root, root_size) != 0))
            return 0u;

        cupid_guest_operation_t run = find_operation(operation);
        if (!run)
            return 0u;
        plan->steps[count++] = (build_step_t){run, name};
    }
    return cursor == end ? count : 0u;
}

static void execute_plan(cupid_syscall_table_t *sys, const char *plan_path, int size,
                         const char *request_root, const char *directory, build_plan_t *plan) {
    ctool_u32 count = read_plan(size, request_root, directory, plan);
    if (count == 0u) {
        sys->serial_write_string("[cupidbuild-guest] invalid request file: ");
        sys->serial_write_string(plan_path);
        sys->serial_write_string("\n");
        return;
    }
    for (ctool_u32 index = 0; index < count; index++) {
        const build_step_t *step = &plan->steps[index];
        char absolute[VFS_MAX_PATH];
        cupid_guest_request_t request = {.path = job_path(directory, step->job_path, absolute)};
        if (step->run(sys, &request) != CTOOL_OK) {
            sys->serial_write_string("[cupidbuild-guest] stopped at ");
            sys->serial_write_string(request.path);
            sys->serial_write_string("\n");
            return;
        }
    }
    sys->serial_write_string("[cupidbuild-guest] build complete\n");
}

static void run_build(cupid_syscall_table_t *sys, const char *default_plan, const char *request_root) {
    if (!sys || sys->version != CUPID_SYSCALL_VERSION || sys->table_size < sizeof(*sys))
        return;
    cupid_init(sys);
    build_plan_t *plan = sys->malloc(sizeof(*plan));
    if (!plan) {
        sys->serial_write_string("[cupidbuild-guest] could not allocate the build plan\n");
        return;
    }
    char directory[VFS_MAX_PATH];
    char local_path[VFS_MAX_PATH];
    const char *local_plan = NULL;
    if (!request_root && sys->shell_get_cwd) {
        ctool_path_t cwd = {ctool_string(sys->shell_get_cwd())};
        if (cwd.text.size < VFS_MAX_PATH && ctool_path_is_canonical(&cwd)) {
            memcpy(directory, cwd.text.data, cwd.text.size + 1u);
            local_plan = job_path(directory, BUILD_PLAN_NAME, local_path);
        }
    }
    int size = local_plan ? sys->vfs_read_text(local_plan, plan->text, BUILD_JOB_BYTES)
                          : BUILD_PLAN_MISSING;
    if (size == BUILD_PLAN_MISSING) {
        size = sys->vfs_read_text(default_plan, plan->text, BUILD_JOB_BYTES);
        execute_plan(sys, default_plan, size, request_root, NULL, plan);
    } else {
        execute_plan(sys, local_plan, size, NULL, directory, plan);
    }
    sys->free(plan);
}

void cupidbuild_guest_run(cupid_syscall_table_t *sys) {
    run_build(sys, BUILD_JOB, NULL);
}

void cupidbuild_media_run(cupid_syscall_table_t *sys) {
    run_build(sys, MEDIA_JOB, MEDIA_REQUESTS);
}
