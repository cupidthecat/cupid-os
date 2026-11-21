/**
 * @file cupidobj_guest.cc
 * @brief Flatten an ELF or wrap an asset as a relocatable object in HomeFS.
 */

#include "cupidobj.h"
#include "guest_job.h"

#define OBJECT_JOB "/home/cupidobj.job"
#define OBJECT_PENDING "/home/.cupidobj-pending.bin"
#define OBJECT_SOURCE_BYTES (16u * 1024u * 1024u)
#define OBJECT_ARENA_BYTES (64u * 1024u * 1024u)
#define OBJECT_JOB_BYTES (CUPID_GUEST_PATH_BYTES(3u) + sizeof("wrap-text\n"))
#define OBJECT_SYMBOL_BYTES (VFS_MAX_PATH + sizeof("_binary__start"))

typedef struct {
    ctool_invocation_request_t invocation;
    ctool_obj_operation_t operation;
    ctool_string_t identity;
} guest_object_job_t;

static int read_object_job(cupid_syscall_table_t *sys, char *file, guest_object_job_t *job) {
    int size = sys->vfs_read_text(OBJECT_JOB, file, OBJECT_JOB_BYTES);
    if (size < 1 || (ctool_u32)size >= OBJECT_JOB_BYTES)
        return 0;

    char *cursor = file;
    char *end = file + size;
    ctool_path_t paths[2];
    if (!cupid_guest_read_path_lines(sys, &cursor, end, paths, 2u) ||
        strcmp(paths[1].text.data, OBJECT_PENDING) == 0)
        return 0;
    job->invocation = (ctool_invocation_request_t){paths[0].text, paths[1].text};
    job->operation = CTOOL_OBJ_EXTRACT_FLAT;
    if (cursor == end)
        return 1;

    char *mode = cupid_guest_next_job_line(&cursor, end);
    char *logical = cupid_guest_next_job_line(&cursor, end);
    if (!mode || !logical || cursor != end)
        return 0;
    if (strcmp(mode, "wrap") == 0)
        job->operation = CTOOL_OBJ_WRAP_BINARY;
    else if (strcmp(mode, "wrap-text") == 0)
        job->operation = CTOOL_OBJ_WRAP_TEXT;
    else
        return 0;

    ctool_path_t identity = {ctool_string(logical)};
    if (identity.text.size <= 1u || identity.text.size >= VFS_MAX_PATH ||
        !ctool_path_is_canonical(&identity))
        return 0;
    job->identity = (ctool_string_t){identity.text.data + 1u, identity.text.size - 1u};
    return 1;
}

static ctool_string_t asset_symbol(char *output, ctool_string_t identity, const char *suffix) {
    const char prefix[] = "_binary_";
    ctool_u32 length = sizeof(prefix) - 1u;
    memcpy(output, prefix, length);
    for (ctool_u32 i = 0; i < identity.size; i++) {
        char ch = identity.data[i];
        if ((ch < 'a' || ch > 'z') && (ch < 'A' || ch > 'Z') && (ch < '0' || ch > '9'))
            ch = '_';
        output[length++] = ch;
    }
    memcpy(output + length, suffix, strlen(suffix) + 1u);
    return ctool_string(output);
}

static ctool_status_t build_object(ctool_invocation_t *invocation, void *context) {
    const guest_object_job_t *job = context;
    ctool_obj_request_t request = {
        .operation = job->operation, .input = invocation->input
    };
    char start[OBJECT_SYMBOL_BYTES];
    char end[OBJECT_SYMBOL_BYTES];
    char size[OBJECT_SYMBOL_BYTES];
    if (job->operation != CTOOL_OBJ_EXTRACT_FLAT) {
        request.as.wrap_binary = (ctool_obj_wrap_binary_request_t){
            .section_name = ctool_string(".data"),
            .section_flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE,
            .section_alignment = 1u,
            .start_symbol = asset_symbol(start, job->identity, "_start"),
            .end_symbol = asset_symbol(end, job->identity, "_end"),
            .size_symbol = asset_symbol(size, job->identity, "_size")
        };
    }
    ctool_obj_result_t result = {0};
    return ctool_obj_transform(invocation->job, &request, invocation->output, &result);
}

void cupidobj_guest_run(cupid_syscall_table_t *sys) {
    if (!sys || sys->version != CUPID_SYSCALL_VERSION || sys->table_size < sizeof(*sys))
        return;
    cupid_init(sys);
    char file[OBJECT_JOB_BYTES];
    guest_object_job_t job = {0};
    if (!read_object_job(sys, file, &job)) {
        sys->serial_write_string("[cupidobj-guest] invalid request file: " OBJECT_JOB "\n");
        return;
    }

    cupid_guest_io_t io = {
        .sys = sys, .input_prefix = ctool_string(""), .pending_path = OBJECT_PENDING
    };
    ctool_job_config_t config = cupid_guest_job_config(&io);
    config.limits.source_bytes = OBJECT_SOURCE_BYTES;
    config.limits.arena_bytes = OBJECT_ARENA_BYTES;
    ctool_invocation_result_t result = {0};
    ctool_status_t status = ctool_invoke(&config, &job.invocation, build_object, &job, &result);
    cupid_guest_job_drain_cache(&io);
    if (status == CTOOL_OK && result.output_committed) {
        sys->serial_write_string("[cupidobj-guest] wrote ");
        sys->serial_write_string(job.invocation.output_path.data);
    } else {
        sys->serial_write_string(job.operation == CTOOL_OBJ_EXTRACT_FLAT
                                 ? "[cupidobj-guest] flatten failed: "
                                 : "[cupidobj-guest] wrap failed: ");
        sys->serial_write_string(ctool_status_name(status));
    }
    sys->serial_write_string("\n");
}
