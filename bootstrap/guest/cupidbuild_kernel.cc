/**
 * @file cupidbuild_kernel.cc
 * @brief Build the production kernel and boot media from an ordered object list.
 */

#include "guest_build.h"

#define BOOT_SOURCE "/boot/boot.asm"
#define PASS_ONE_LIMIT (CUPID_GUEST_LINK_INPUT_LIMIT - 1u)

enum kernel_product {
    BOOT_BIN, PASS_ONE, SYMBOL_SOURCE, SYMBOL_OBJECT, KERNEL_ELF, KERNEL_BIN, DISK_OUTPUT,
    PRODUCT_COUNT
};

typedef struct {
    char list[CUPID_GUEST_PATH_BYTES(PASS_ONE_LIMIT + 3u) + sizeof("root output image 2047\nstage ")];
    char root_path[VFS_MAX_PATH];
    const char *root;
    const char *image_option;
    const char *stage_option;
    ctool_string_t objects[PASS_ONE_LIMIT];
    ctool_u32 count;
    char products[PRODUCT_COUNT][VFS_MAX_PATH];
    char link[CUPID_GUEST_PATH_BYTES(CUPID_GUEST_LINK_INPUT_LIMIT + 3u)];
} kernel_inputs_t;

static char *append_text(char *out, const char *text) {
    size_t size = strlen(text);
    memcpy(out, text, size + 1u);
    return out + size;
}

static int read_output_paths(cupid_syscall_table_t *sys, char **cursor, char *end,
                             const char *project_directory, kernel_inputs_t *inputs) {
    static const char *const leaves[PRODUCT_COUNT] = {
        "/boot.bin", "/pass1.elf", "/ksyms_data.cc", "/ksyms_data.o",
        "/kernel.elf", "/kernel.bin", "/disk-prefix.img"
    };
    char absolute[VFS_MAX_PATH];
    const char *directory = "/home";
    if (end - *cursor >= 7 && strncmp(*cursor, "output ", 7u) == 0) {
        const char *line = cupid_guest_next_job_line(cursor, end);
        if (!line)
            return 0;
        directory = cupid_guest_project_path(sys, project_directory, line + 7, absolute);
        if (!directory)
            return 0;
    }
    ctool_path_t path = {ctool_string(directory)};
    if (!ctool_path_is_canonical(&path))
        return 0;
    inputs->image_option = NULL;
    if (end - *cursor >= 6 && strncmp(*cursor, "image ", 6u) == 0) {
        ctool_u32 extent;
        inputs->image_option = cupid_guest_next_job_line(cursor, end);
        if (!inputs->image_option || !cupid_guest_disk_image_extent(sys, inputs->image_option, &extent))
            return 0;
    }
    inputs->stage_option = NULL;
    if (inputs->image_option && end - *cursor >= 6 && strncmp(*cursor, "stage ", 6u) == 0) {
        char manifest[VFS_MAX_PATH];
        inputs->stage_option = cupid_guest_next_job_line(cursor, end);
        if (!inputs->stage_option ||
            !cupid_guest_disk_stage_list(sys, inputs->stage_option, project_directory, manifest))
            return 0;
    }
    for (ctool_u32 i = 0; i < PRODUCT_COUNT; i++) {
        const char *leaf = i == DISK_OUTPUT && inputs->image_option ? "/cupidos.img" : leaves[i];
        if (path.text.size + strlen(leaf) >= VFS_MAX_PATH)
            return 0;
        append_text(append_text(inputs->products[i], directory), leaf);
        if (!cupid_guest_output_prefix(sys, ctool_string(inputs->products[i])))
            return 0;
    }
    return !inputs->image_option || strncmp(inputs->products[DISK_OUTPUT], "/home/", 6u) == 0;
}

static int read_objects(cupid_syscall_table_t *sys, const cupid_guest_request_t *job, kernel_inputs_t *inputs) {
    int size = cupid_guest_read_request(sys, job, inputs->list, sizeof(inputs->list));
    if (size <= 0 || (ctool_u32)size >= sizeof(inputs->list))
        return 0;
    char *cursor = inputs->list;
    char *end = cursor + size;
    if (!cupid_guest_read_root(sys, &cursor, end, "/src", job->project_directory,
                               inputs->root_path, &inputs->root) ||
        strlen(inputs->root) + sizeof(BOOT_SOURCE) > VFS_MAX_PATH ||
        !read_output_paths(sys, &cursor, end, job->project_directory, inputs))
        return 0;
    inputs->count = 0;
    while (cursor < end && inputs->count < PASS_ONE_LIMIT) {
        char *line = cupid_guest_next_job_line(&cursor, end);
        if (!line)
            return 0;
        ctool_path_t path = {ctool_string(line)};
        if (path.text.size <= 2u || path.text.size >= VFS_MAX_PATH ||
            !ctool_path_is_canonical(&path) || strcmp(line + path.text.size - 2u, ".o") != 0 ||
            strcmp(line, inputs->products[SYMBOL_OBJECT]) == 0)
            return 0;
        for (ctool_u32 i = 0; i < inputs->count; i++) {
            if (strcmp(line, inputs->objects[i].data) == 0)
                return 0;
        }
        inputs->objects[inputs->count++] = path.text;
    }
    return cursor == end && inputs->count != 0u;
}

static ctool_string_t link_request(kernel_inputs_t *inputs, int final_link) {
    char *end = append_text(inputs->link, inputs->products[final_link ? KERNEL_ELF : PASS_ONE]);
    end = append_text(end, "\nscript\n");
    end = append_text(end, inputs->root);
    end = append_text(end, "/link.ld\n");
    for (ctool_u32 i = 0; i < inputs->count; i++) {
        const ctool_string_t *path = &inputs->objects[i];
        memcpy(end, path->data, path->size);
        end += path->size;
        *end++ = '\n';
    }
    if (final_link) {
        end = append_text(end, inputs->products[SYMBOL_OBJECT]);
        *end++ = '\n';
    }
    return (ctool_string_t){inputs->link, (size_t)(end - inputs->link)};
}

static ctool_status_t build_kernel(cupid_syscall_table_t *sys, const cupid_guest_request_t *request,
                                   kernel_inputs_t *inputs) {
    char boot[VFS_MAX_PATH];
    char cpu_include[VFS_MAX_PATH + sizeof("include ")];
    char core_include[VFS_MAX_PATH + sizeof("include ")];
    append_text(append_text(boot, inputs->root), BOOT_SOURCE);
    append_text(append_text(append_text(cpu_include, "include "), inputs->root), "/kernel/cpu");
    append_text(append_text(append_text(core_include, "include "), inputs->root), "/kernel/core");
    char (*products)[VFS_MAX_PATH] = inputs->products;
    const struct {
        const char *name;
        cupid_guest_operation_t run;
        const char *lines[5];
    } steps[] = {
        {"bootloader", cupid_guest_raw_job, {boot, products[BOOT_BIN], "bootloader"}},
        {"pass-one link", cupid_guest_link_job, {NULL}},
        {"symbol source", cupid_guest_symbols_job, {products[PASS_ONE], products[SYMBOL_SOURCE]}},
        {"symbol object", cupid_guest_compile_job,
         {products[SYMBOL_SOURCE], products[SYMBOL_OBJECT], cpu_include, core_include}},
        {"final link", cupid_guest_link_job, {NULL}},
        {"raw kernel", cupid_guest_object_job, {products[KERNEL_ELF], products[KERNEL_BIN]}},
        {"disk", cupid_guest_disk_job,
         {products[BOOT_BIN], products[KERNEL_BIN], products[DISK_OUTPUT], inputs->image_option, inputs->stage_option}},
    };
    char text[CUPID_GUEST_PATH_BYTES(5u) + 2u * sizeof("include ")];
    int final_link = 0;
    for (ctool_u32 i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
        cupid_guest_request_t job = {.path = request->path, .project_directory = request->project_directory};
        if (steps[i].lines[0]) {
            char *end = text;
            for (ctool_u32 line = 0; line < 5u && steps[i].lines[line]; line++) {
                end = append_text(end, steps[i].lines[line]);
                *end++ = '\n';
            }
            job.text = (ctool_string_t){text, (size_t)(end - text)};
        } else {
            job.text = link_request(inputs, final_link);
            final_link = 1;
        }
        ctool_status_t status = steps[i].run(sys, &job);
        if (status != CTOOL_OK) {
            sys->serial_write_string("[cupidbuild-guest] kernel build stopped at ");
            sys->serial_write_string(steps[i].name);
            sys->serial_write_string("\n");
            return status;
        }
    }
    return CTOOL_OK;
}

ctool_status_t cupid_guest_kernel_job(cupid_syscall_table_t *sys, const cupid_guest_request_t *job) {
    cupid_init(sys);
    kernel_inputs_t *inputs = sys->malloc(sizeof(*inputs));
    if (!inputs)
        return CTOOL_ERR_NO_MEMORY;
    ctool_status_t status = CTOOL_ERR_INPUT;
    if (read_objects(sys, job, inputs)) {
        status = build_kernel(sys, job, inputs);
    } else {
        sys->serial_write_string("[cupidbuild-guest] invalid kernel object list\n");
    }
    sys->free(inputs);
    return status;
}
