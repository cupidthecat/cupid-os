/**
 * @file cupidbuild_kernel.cc
 * @brief Build the production kernel and disk prefix from an ordered object list.
 */

#include "guest_build.h"

#define BOOT_BIN "/home/boot.bin"
#define PASS_ONE "/home/pass1.elf"
#define SYMBOL_SOURCE "/home/ksyms_data.cc"
#define SYMBOL_OBJECT "/home/ksyms_data.o"
#define KERNEL_ELF "/home/kernel.elf"
#define KERNEL_BIN "/home/kernel.bin"
#define DISK_PREFIX "/home/disk-prefix.img"
#define LINK_SCRIPT "\nscript\n/src/link.ld\n"
#define PASS_ONE_LIMIT (CUPID_GUEST_LINK_INPUT_LIMIT - 1u)

typedef struct {
    char list[CUPID_GUEST_PATH_BYTES(PASS_ONE_LIMIT)];
    ctool_string_t objects[PASS_ONE_LIMIT];
    ctool_u32 count;
    char link[CUPID_GUEST_PATH_BYTES(CUPID_GUEST_LINK_INPUT_LIMIT + 3u)];
} kernel_inputs_t;

static int read_objects(cupid_syscall_table_t *sys, const cupid_guest_request_t *job, kernel_inputs_t *inputs) {
    int size = cupid_guest_read_request(sys, job, inputs->list, sizeof(inputs->list));
    if (size <= 0 || (ctool_u32)size >= sizeof(inputs->list))
        return 0;
    char *cursor = inputs->list;
    char *end = cursor + size;
    inputs->count = 0;
    while (cursor < end && inputs->count < PASS_ONE_LIMIT) {
        char *line = cupid_guest_next_job_line(&cursor, end);
        if (!line)
            return 0;
        ctool_path_t path = {ctool_string(line)};
        if (path.text.size <= 2u || path.text.size >= VFS_MAX_PATH ||
            !ctool_path_is_canonical(&path) || strcmp(line + path.text.size - 2u, ".o") != 0 ||
            strcmp(line, SYMBOL_OBJECT) == 0)
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
    const char *header = final_link ? KERNEL_ELF LINK_SCRIPT : PASS_ONE LINK_SCRIPT;
    size_t size = strlen(header);
    memcpy(inputs->link, header, size);
    for (ctool_u32 i = 0; i < inputs->count; i++) {
        const ctool_string_t *path = &inputs->objects[i];
        memcpy(inputs->link + size, path->data, path->size);
        size += path->size;
        inputs->link[size++] = '\n';
    }
    if (final_link) {
        memcpy(inputs->link + size, SYMBOL_OBJECT "\n", sizeof(SYMBOL_OBJECT));
        size += sizeof(SYMBOL_OBJECT);
    }
    return (ctool_string_t){inputs->link, size};
}

static ctool_status_t build_kernel(cupid_syscall_table_t *sys, const char *path, kernel_inputs_t *inputs) {
    static const struct {
        const char *name;
        cupid_guest_operation_t run;
        const char *text;
    } steps[] = {
        {"bootloader", cupid_guest_raw_job, "/src/boot/boot.asm\n" BOOT_BIN "\nbootloader\n"},
        {"pass-one link", cupid_guest_link_job, NULL},
        {"symbol source", cupid_guest_symbols_job, PASS_ONE "\n" SYMBOL_SOURCE "\n"},
        {"symbol object", cupid_guest_compile_job, SYMBOL_SOURCE "\n" SYMBOL_OBJECT
         "\ninclude /src/kernel/cpu\ninclude /src/kernel/core\n"},
        {"final link", cupid_guest_link_job, NULL},
        {"raw kernel", cupid_guest_object_job, KERNEL_ELF "\n" KERNEL_BIN "\n"},
        {"disk prefix", cupid_guest_disk_job, BOOT_BIN "\n" KERNEL_BIN "\n" DISK_PREFIX "\n"},
    };
    int final_link = 0;
    for (ctool_u32 i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
        cupid_guest_request_t job = {.path = path};
        if (steps[i].text) {
            job.text = ctool_string(steps[i].text);
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
        status = build_kernel(sys, job->path, inputs);
    } else {
        sys->serial_write_string("[cupidbuild-guest] invalid kernel object list\n");
    }
    sys->free(inputs);
    return status;
}
