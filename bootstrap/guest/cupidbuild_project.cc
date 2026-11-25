/**
 * @file cupidbuild_project.cc
 * @brief Build a kernel from an installed checkout and its source layout.
 */

#include "guest_build.h"

#define PROJECT_OUTPUT "build"
#define PROJECT_ITEM_LIMIT (CUPID_GUEST_LINK_INPUT_LIMIT - 1u)
#define PROJECT_SETTINGS_BYTES (CUPID_GUEST_PATH_BYTES(2u) + sizeof("objects tmp\nimage 2047\nstage "))
#define PROJECT_LAYOUT_BYTES CUPID_GUEST_PATH_BYTES(4u * CUPID_GUEST_LINK_INPUT_LIMIT)

typedef enum {
    INSTALL_BIN, INSTALL_DOCS, INSTALL_DEMOS,
    COMPILE_KERNEL, COMPILE_DOOM_COMPAT, COMPILE_DOOM_TREE,
    ASSEMBLE, TRAMPOLINE, WRAP, WRAP_TEXT, WRAP_JPEG,
    PROJECT_ROLE_COUNT
} project_role_t;

static const char *const install_sources[] = {
    "/kernel/util/bin_programs_gen.cc",
    "/kernel/util/docs_programs_gen.cc",
    "/kernel/util/demos_programs_gen.cc"
};

static const char *const role_names[PROJECT_ROLE_COUNT] = {
    "bin", "docs", "demos", "kernel", "doom-compat", "doom-tree",
    "assemble", "trampoline", "wrap", "wrap-text", "wrap-jpeg"
};

typedef struct {
    project_role_t role;
    const char *source;
} project_item_t;

typedef struct {
    const char *directory;
    char output[VFS_MAX_PATH];
    const char *object_directory;
    const char *image;
    const char *stage;
    ctool_string_t sections[ASSEMBLE];
    project_item_t items[PROJECT_ITEM_LIMIT];
    ctool_u32 item_count;
    char *request;
    char *next;
} project_build_t;

static const char *project_path(cupid_syscall_table_t *sys, const char *directory,
                                const char *name, char absolute[VFS_MAX_PATH]) {
    if (!name || !name[0] || name[0] == '/')
        return NULL;
    const char *resolved = cupid_guest_project_path(sys, directory, name, absolute);
    if (!resolved)
        return NULL;
    ctool_path_t path = {ctool_string(resolved)};
    return ctool_path_is_canonical(&path) ? resolved : NULL;
}

static int read_settings(cupid_syscall_table_t *sys, const cupid_guest_request_t *job,
                          project_build_t *project, char layout[VFS_MAX_PATH],
                          char settings[PROJECT_SETTINGS_BYTES]) {
    project->directory = job->project_directory;
    char absolute[VFS_MAX_PATH];
    if (!project->directory || !sys->vfs_stat ||
        !project_path(sys, project->directory, PROJECT_OUTPUT, project->output) ||
        strncmp(project->output, "/home/", 6u) != 0 ||
        !project_path(sys, project->output, "demos_programs_gen.cc", absolute))
        return 0;

    int size = cupid_guest_read_request(sys, job, settings, PROJECT_SETTINGS_BYTES);
    if (size <= 0 || (ctool_u32)size >= PROJECT_SETTINGS_BYTES)
        return 0;

    char *cursor = settings;
    char *end = settings + size;
    const char *file = cupid_guest_next_job_line(&cursor, end);
    if (!project_path(sys, project->directory, file, layout))
        return 0;

    project->object_directory = project->output;
    const char *line = cupid_guest_next_job_line(&cursor, end);
    if (line && strcmp(line, "objects tmp") == 0) {
        project->object_directory = "/tmp";
        line = cupid_guest_next_job_line(&cursor, end);
    }
    if (line) {
        ctool_u32 extent;
        if (!cupid_guest_disk_image_extent(sys, line, &extent))
            return 0;
        project->image = line;
        line = cupid_guest_next_job_line(&cursor, end);
        if (line) {
            if (strncmp(line, "stage ", 6u) != 0 ||
                !project_path(sys, project->directory, line + 6u, absolute) ||
                !cupid_guest_disk_stage_list(sys, line, project->directory, absolute))
                return 0;
            project->stage = line;
        }
    }
    return cursor == end;
}

static int read_sections(char **cursor, char *end, project_build_t *project) {
    for (ctool_u32 section = 0u; section < ASSEMBLE; section++) {
        const char *name = cupid_guest_next_job_line(cursor, end);
        if (!name || strcmp(name, role_names[section]) != 0)
            return 0;
        const char *start = *cursor;
        for (;;) {
            char *line = cupid_guest_next_job_line(cursor, end);
            if (!line)
                return 0;
            if (strcmp(line, "end") == 0) {
                project->sections[section] = (ctool_string_t){start, (ctool_u32)(line - start)};
                break;
            }
            if (*cursor == end || (section >= COMPILE_KERNEL && strcmp(line, "sources") == 0))
                return 0;
            /* Preserve the worker options as a slice of the layout. */
            (*cursor)[-1] = '\n';
        }
    }
    const char *name = cupid_guest_next_job_line(cursor, end);
    return name && strcmp(name, "objects") == 0;
}

static int read_layout(char *layout, ctool_u32 size, project_build_t *project) {
    char *cursor = layout;
    char *end = layout + size;
    const char *version = cupid_guest_next_job_line(&cursor, end);
    if (!version || strcmp(version, "cupid-kernel 1") != 0 || !read_sections(&cursor, end, project))
        return 0;

    ctool_u32 single_roles = 0u;
    size_t root_size = strlen(project->directory);
    while (cursor < end && project->item_count < PROJECT_ITEM_LIMIT) {
        char *line = cupid_guest_next_job_line(&cursor, end);
        if (!line)
            return 0;
        char *source = line;
        while (*source && *source != ' ')
            source++;
        if (*source != ' ')
            return 0;
        *source++ = '\0';

        ctool_u32 role = 0u;
        while (role < PROJECT_ROLE_COUNT && strcmp(line, role_names[role]) != 0)
            role++;
        ctool_path_t path = {ctool_string(source)};
        if (role == PROJECT_ROLE_COUNT || path.text.size <= 1u ||
            path.text.size + root_size >= VFS_MAX_PATH || !ctool_path_is_canonical(&path))
            return 0;
        if (role <= INSTALL_DEMOS || role == TRAMPOLINE) {
            if (single_roles & (1u << role))
                return 0;
            single_roles |= 1u << role;
            if (role <= INSTALL_DEMOS &&
                (project->sections[role].size == 0u || strcmp(source, install_sources[role]) != 0))
                return 0;
        }
        for (ctool_u32 i = 0u; i < project->item_count; i++) {
            if (strcmp(source, project->items[i].source) == 0)
                return 0;
        }
        project->items[project->item_count++] = (project_item_t){(project_role_t)role, source};
    }
    return cursor == end && project->item_count != 0u;
}

static void append_part(project_build_t *project, const char *text, size_t size) {
    memcpy(project->next, text, size);
    project->next += size;
}

static void append_text(project_build_t *project, const char *text) {
    append_part(project, text, strlen(text));
}

static void append_line(project_build_t *project, const char *text) {
    append_text(project, text);
    append_part(project, "\n", 1u);
}

static void append_object(project_build_t *project, ctool_u32 index) {
    char name[] = "/i000.o";
    name[2] += index / 100u;
    name[3] += (index / 10u) % 10u;
    name[4] += index % 10u;
    append_text(project, project->object_directory);
    append_line(project, name);
}

static void append_section(project_build_t *project, project_role_t role) {
    ctool_string_t section = project->sections[role];
    append_part(project, section.data, section.size);
}

static ctool_status_t run_request(cupid_syscall_table_t *sys, const cupid_guest_request_t *job,
                                  project_build_t *project, cupid_guest_operation_t operation) {
    cupid_guest_request_t request = *job;
    request.text = (ctool_string_t){project->request, (ctool_u32)(project->next - project->request)};
    project->next = project->request;
    return operation(sys, &request);
}

static ctool_status_t assemble_item(cupid_syscall_table_t *sys, const cupid_guest_request_t *job,
                                    project_build_t *project, ctool_u32 index) {
    const project_item_t *item = &project->items[index];
    append_text(project, project->directory);
    append_line(project, item->source);
    if (item->role == ASSEMBLE) {
        append_object(project, index);
        return run_request(sys, job, project, cupid_guest_assemble_job);
    }
    append_text(project, project->output);
    append_text(project, "/smp.bin\nsmp-trampoline\n");
    ctool_status_t status = run_request(sys, job, project, cupid_guest_raw_job);
    if (status != CTOOL_OK)
        return status;
    append_text(project, project->output);
    append_line(project, "/smp.bin");
    append_object(project, index);
    append_text(project, "wrap-readonly\nsmp_trampoline\n");
    return run_request(sys, job, project, cupid_guest_object_job);
}

static void append_install_path(project_build_t *project, const char *source) {
    const char *leaf = source;
    for (const char *p = source; *p; p++) {
        if (*p == '/')
            leaf = p;
    }
    append_text(project, project->output);
    append_line(project, leaf);
}

static ctool_status_t install_item(cupid_syscall_table_t *sys, const cupid_guest_request_t *job,
                                   project_build_t *project, ctool_u32 index) {
    const project_item_t *item = &project->items[index];
    project_role_t role = item->role;
    append_install_path(project, item->source);
    append_line(project, role_names[role]);
    append_section(project, role);
    ctool_status_t status = run_request(sys, job, project, cupid_guest_install_job);
    if (status != CTOOL_OK)
        return status;
    append_install_path(project, item->source);
    append_object(project, index);
    static const char *const includes[] = {"/kernel", "/kernel/core", "/kernel/fs"};
    for (ctool_u32 i = 0u; i < sizeof(includes) / sizeof(includes[0]); i++) {
        append_text(project, "include ");
        append_text(project, project->directory);
        append_line(project, includes[i]);
    }
    return run_request(sys, job, project, cupid_guest_compile_job);
}

static ctool_status_t build_lists(cupid_syscall_table_t *sys, const cupid_guest_request_t *job,
                                  project_build_t *project, project_role_t role) {
    int compile = role < ASSEMBLE;
    cupid_guest_operation_t operation = compile ? cupid_guest_compile_list : cupid_guest_asset_list;
    ctool_u32 count = 0u;
    for (ctool_u32 i = 0u; i < project->item_count; i++) {
        if (project->items[i].role != role)
            continue;
        if (count == 0u) {
            if (compile) {
                append_line(project, "root .");
                append_section(project, role);
                append_line(project, "sources");
            } else {
                append_line(project, role_names[role]);
                append_line(project, project->directory);
            }
        }
        append_line(project, project->items[i].source);
        append_object(project, i);
        if (++count == CUPID_GUEST_OBJECT_LIST_LIMIT) {
            ctool_status_t status = run_request(sys, job, project, operation);
            if (status != CTOOL_OK)
                return status;
            count = 0u;
        }
    }
    return count ? run_request(sys, job, project, operation) : CTOOL_OK;
}

static ctool_status_t build_project(cupid_syscall_table_t *sys, const cupid_guest_request_t *job,
                                    project_build_t *project) {
    ctool_status_t status = cupid_guest_create_directory(sys, project->output);
    for (ctool_u32 i = 0u; status == CTOOL_OK && i < project->item_count; i++) {
        project_role_t role = project->items[i].role;
        if (role == ASSEMBLE || role == TRAMPOLINE)
            status = assemble_item(sys, job, project, i);
        else if (role <= INSTALL_DEMOS)
            status = install_item(sys, job, project, i);
        if (status != CTOOL_OK) {
            sys->serial_write_string("[cupidbuild-guest] project input failed: ");
            sys->serial_write_string(project->items[i].source);
            sys->serial_write_string("\n");
        }
    }
    for (ctool_u32 role = WRAP; status == CTOOL_OK && role <= WRAP_JPEG; role++)
        status = build_lists(sys, job, project, (project_role_t)role);
    for (ctool_u32 role = COMPILE_KERNEL; status == CTOOL_OK && role <= COMPILE_DOOM_TREE; role++)
        status = build_lists(sys, job, project, (project_role_t)role);
    if (status != CTOOL_OK)
        return status;

    append_text(project, "root .\noutput " PROJECT_OUTPUT "\n");
    if (project->image)
        append_line(project, project->image);
    if (project->stage)
        append_line(project, project->stage);
    for (ctool_u32 i = 0u; i < project->item_count; i++)
        append_object(project, i);
    return run_request(sys, job, project, cupid_guest_kernel_job);
}

ctool_status_t cupid_guest_project_kernel(cupid_syscall_table_t *sys, const cupid_guest_request_t *job) {
    cupid_init(sys);
    project_build_t project = {0};
    char layout_path[VFS_MAX_PATH];
    char settings[PROJECT_SETTINGS_BYTES];
    if (!read_settings(sys, job, &project, layout_path, settings)) {
        sys->serial_write_string("[cupidbuild-guest] invalid project kernel settings\n");
        return CTOOL_ERR_INPUT;
    }
    cupid_stat_t info;
    if (sys->vfs_stat(layout_path, &info) != 0 || info.type != VFS_TYPE_FILE ||
        info.size == 0u || info.size >= PROJECT_LAYOUT_BYTES) {
        sys->serial_write_string("[cupidbuild-guest] could not read project kernel layout\n");
        return CTOOL_ERR_INPUT;
    }
    char *layout = sys->malloc(info.size + 1u);
    if (!layout)
        return CTOOL_ERR_NO_MEMORY;
    int size = sys->vfs_read_text(layout_path, layout, info.size + 1u);
    ctool_status_t status = CTOOL_ERR_INPUT;
    if (size == (int)info.size && read_layout(layout, info.size, &project)) {
        /* An individual request adds at most one object path per item, plus its header. */
        project.request = sys->malloc(info.size + CUPID_GUEST_PATH_BYTES(project.item_count + 8u));
        if (project.request) {
            project.next = project.request;
            status = build_project(sys, job, &project);
            sys->free(project.request);
        } else {
            status = CTOOL_ERR_NO_MEMORY;
        }
    } else {
        sys->serial_write_string("[cupidbuild-guest] invalid project kernel layout\n");
    }
    sys->free(layout);
    return status;
}
