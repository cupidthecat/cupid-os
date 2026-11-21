/**
 * @file cupidobj_guest.cc
 * @brief Flatten an ELF or produce wrapped kernel assets in HomeFS.
 */

#include "cupidobj.h"
#include "guest_job.h"

#define CUPIDBUILD_JPEG_GUEST
#include "cupidbuild_jpeg.h"
#undef CUPIDBUILD_JPEG_GUEST

#define OBJECT_JOB "/home/cupidobj.job"
#define OBJECT_PENDING "/home/.cupidobj-pending.bin"
#define OBJECT_SOURCE_BYTES (16u * 1024u * 1024u)
#define OBJECT_ARENA_BYTES (64u * 1024u * 1024u)
#define OBJECT_JOB_BYTES (CUPID_GUEST_PATH_BYTES(3u) + sizeof("wrap-readonly\n"))
#define OBJECT_SYMBOL_BYTES (VFS_MAX_PATH + sizeof("_binary__start"))
#define OBJECT_BUNDLE_LIMIT 256u
#define OBJECT_BUNDLE_BYTES (OBJECT_BUNDLE_LIMIT * OBJECT_JOB_BYTES)

typedef struct {
    ctool_invocation_request_t invocation;
    ctool_obj_operation_t operation;
    ctool_string_t identity;
    ctool_bool readonly;
} guest_object_job_t;

static int ascii_alnum(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
}

static int read_symbol_stem(const char *stem, ctool_string_t *identity) {
    ctool_string_t name = ctool_string(stem);
    if (name.size == 0u || name.size >= VFS_MAX_PATH || (stem[0] >= '0' && stem[0] <= '9'))
        return 0;
    for (ctool_u32 i = 0; i < name.size; i++) {
        if (!ascii_alnum(stem[i]) && stem[i] != '_')
            return 0;
    }
    *identity = name;
    return 1;
}

static int read_asset(const char *mode, const char *logical, guest_object_job_t *job) {
    if (strcmp(mode, "wrap-readonly") == 0) {
        job->operation = CTOOL_OBJ_WRAP_BINARY;
        job->readonly = CTOOL_TRUE;
        return read_symbol_stem(logical, &job->identity);
    }
    if (strcmp(mode, "wrap") == 0)
        job->operation = CTOOL_OBJ_WRAP_BINARY;
    else if (strcmp(mode, "wrap-text") == 0)
        job->operation = CTOOL_OBJ_WRAP_TEXT;
    else if (strcmp(mode, "wrap-jpeg") == 0)
        job->operation = CTOOL_OBJ_WRAP_JPEG;
    else
        return 0;

    ctool_path_t identity = {ctool_string(logical)};
    if (identity.text.size <= 1u || identity.text.size >= VFS_MAX_PATH ||
        !ctool_path_is_canonical(&identity))
        return 0;
    job->identity = (ctool_string_t){identity.text.data + 1u, identity.text.size - 1u};
    return 1;
}

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
    if (!mode)
        return 0;
    if (strcmp(mode, "wrap-bundle") == 0) {
        job->operation = CTOOL_OBJ_BUILD_ISO_FIXTURE;
        return cursor == end;
    }
    char *logical = cupid_guest_next_job_line(&cursor, end);
    return logical && cursor == end && read_asset(mode, logical, job);
}

static ctool_string_t asset_symbol(char *output, const guest_object_job_t *job, const char *suffix) {
    const char prefix[] = "_binary_";
    ctool_u32 length = 0u;
    if (!job->readonly) {
        length = sizeof(prefix) - 1u;
        memcpy(output, prefix, length);
    }
    for (ctool_u32 i = 0; i < job->identity.size; i++) {
        char ch = job->identity.data[i];
        output[length++] = ascii_alnum(ch) ? ch : '_';
    }
    memcpy(output + length, suffix, strlen(suffix) + 1u);
    return ctool_string(output);
}

static ctool_status_t wrap_asset(ctool_job_t *ctool, const ctool_source_t *input,
                                 ctool_buffer_t *output, const guest_object_job_t *job) {
    ctool_obj_request_t request = {
        .operation = job->operation, .input = input
    };
    char start[OBJECT_SYMBOL_BYTES];
    char end[OBJECT_SYMBOL_BYTES];
    char size[OBJECT_SYMBOL_BYTES];
    request.as.wrap_binary = (ctool_obj_wrap_binary_request_t){
        .section_name = ctool_string(job->readonly ? ".rodata" : ".data"),
        .section_flags = CTOOL_ELF32_SHF_ALLOC | (job->readonly ? 0u : CTOOL_ELF32_SHF_WRITE),
        .section_alignment = 1u,
        .start_symbol = asset_symbol(start, job, "_start"),
        .end_symbol = asset_symbol(end, job, "_end"),
        .size_symbol = asset_symbol(size, job, "_size")
    };
    ctool_obj_result_t result = {0};
    ctool_status_t status = ctool_obj_transform(ctool, &request, output, &result);
    if (status != CTOOL_OK || job->operation != CTOOL_OBJ_WRAP_JPEG)
        return status;

    char reason[160];
    if (!cupidbuild_jpeg_validate_object(ctool, ctool_buffer_view(output), input->contents, job->identity)) {
        const ctool_diagnostic_t diagnostic = {CTOOL_DIAG_ERROR, CTOOL_OBJ_DIAG_INVALID_INPUT,
            input->path.text, 0u, 0u, ctool_string("JPEG object does not match the source payload and symbols")};
        ctool_job_emit(ctool, &diagnostic);
        return CTOOL_ERR_INPUT;
    }
    if (!cupidbuild_jpeg_validate_stream(input->contents.data, input->contents.size, reason, sizeof(reason))) {
        const ctool_diagnostic_t diagnostic = {CTOOL_DIAG_ERROR, CTOOL_OBJ_DIAG_INVALID_INPUT,
            input->path.text, 0u, 0u, ctool_string(reason)};
        ctool_job_emit(ctool, &diagnostic);
        return CTOOL_ERR_INPUT;
    }
    return CTOOL_OK;
}

static int read_bundle_member(char **cursor, char *end, ctool_path_t *input,
                              ctool_path_t *member, guest_object_job_t *asset) {
    char *source = cupid_guest_next_job_line(cursor, end);
    char *name = cupid_guest_next_job_line(cursor, end);
    char *mode = cupid_guest_next_job_line(cursor, end);
    char *identity = cupid_guest_next_job_line(cursor, end);
    if (!source || !name || !mode || !identity)
        return 0;
    input->text = ctool_string(source);
    member->text = ctool_string(name);
    return input->text.size < VFS_MAX_PATH && ctool_path_is_canonical(input) &&
           member->text.size > 1u && member->text.size < VFS_MAX_PATH &&
           ctool_path_is_canonical(member) && read_asset(mode, identity, asset);
}

static ctool_status_t build_asset_bundle(ctool_invocation_t *invocation) {
    ctool_bytes_t manifest = invocation->input->contents;
    if (manifest.size == 0u)
        return CTOOL_ERR_INPUT;
    if (manifest.size > OBJECT_BUNDLE_BYTES)
        return CTOOL_ERR_LIMIT;
    ctool_job_t *job = invocation->job;
    ctool_arena_t *arena = ctool_job_arena(job);
    char *text = NULL;
    ctool_status_t status = ctool_arena_alloc(arena, manifest.size + 1u, 1u, (void **)&text);
    if (status != CTOOL_OK)
        return status;
    memcpy(text, manifest.data, manifest.size);
    char *cursor = text;
    char *end = text + manifest.size;

    ctool_buffer_t *buffer = NULL;
    status = ctool_job_open_buffer(job, 4096u, ctool_job_limits(job)->output_bytes, &buffer);
    if (status != CTOOL_OK)
        return status;
    ctool_source_t objects[OBJECT_BUNDLE_LIMIT];
    ctool_obj_iso_fixture_entry_t entries[OBJECT_BUNDLE_LIMIT];
    ctool_u32 count = 0u;
    while (cursor < end && status == CTOOL_OK) {
        if (count == OBJECT_BUNDLE_LIMIT) {
            status = CTOOL_ERR_LIMIT;
            break;
        }
        ctool_path_t input;
        ctool_path_t member;
        guest_object_job_t asset = {0};
        if (!read_bundle_member(&cursor, end, &input, &member, &asset)) {
            status = CTOOL_ERR_INPUT;
            break;
        }
        ctool_source_t source;
        status = ctool_job_load_source(job, &input, &source);
        if (status == CTOOL_OK)
            status = wrap_asset(job, &source, buffer, &asset);
        if (status == CTOOL_OK) {
            objects[count].path = member;
            status = ctool_arena_copy_bytes(arena, ctool_buffer_view(buffer), &objects[count].contents);
            entries[count] = (ctool_obj_iso_fixture_entry_t){
                {member.text.data + 1u, member.text.size - 1u}, CTOOL_OBJ_ISO_FIXTURE_FILE, &objects[count]
            };
            count++;
        }
        ctool_buffer_clear(buffer);
    }
    for (ctool_u32 i = 0; i < count && status == CTOOL_OK; i++) {
        ctool_string_t name = entries[i].path;
        status = ctool_buffer_append(buffer, ctool_bytes(name.data, name.size));
        if (status == CTOOL_OK)
            status = ctool_buffer_append(buffer, ctool_bytes("\n", 1u));
    }
    if (status == CTOOL_OK) {
        ctool_source_t inventory = {invocation->input->path, ctool_buffer_view(buffer)};
        const ctool_obj_request_t request = {
            .operation = CTOOL_OBJ_BUILD_ISO_FIXTURE, .input = &inventory,
            .as = {.iso_fixture = {entries, count}}
        };
        ctool_obj_result_t result = {0};
        status = ctool_obj_transform(job, &request, invocation->output, &result);
    }
    ctool_buffer_close(buffer);
    return status;
}

static ctool_status_t build_object(ctool_invocation_t *invocation, void *context) {
    const guest_object_job_t *job = context;
    if (job->operation == CTOOL_OBJ_BUILD_ISO_FIXTURE)
        return build_asset_bundle(invocation);
    if (job->operation != CTOOL_OBJ_EXTRACT_FLAT) {
        return wrap_asset(invocation->job, invocation->input, invocation->output, job);
    }
    const ctool_obj_request_t request = {.operation = CTOOL_OBJ_EXTRACT_FLAT, .input = invocation->input};
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
        const char *operation = job.operation == CTOOL_OBJ_EXTRACT_FLAT ? "flatten" :
                                job.operation == CTOOL_OBJ_BUILD_ISO_FIXTURE ? "bundle" : "wrap";
        sys->serial_write_string("[cupidobj-guest] ");
        sys->serial_write_string(operation);
        sys->serial_write_string(" failed: ");
        sys->serial_write_string(ctool_status_name(status));
    }
    sys->serial_write_string("\n");
}
