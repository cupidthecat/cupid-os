/**
 * @file cupidc_bundle.cc
 * @brief Collect compiler outputs and publish them as a checked ISO.
 */

#include "cupidc_bundle.h"
#include "cupidobj.h"

#define COMPILE_BUNDLE_LIMIT 256u
#define COMPILE_BUNDLE_BYTES CUPID_GUEST_PATH_BYTES(2u * COMPILE_BUNDLE_LIMIT)

typedef struct {
    cupid_guest_io_t io;
    ctool_arena_t *arena;
    ctool_bytes_t object;
} compile_capture_t;

static ctool_status_t capture_object(void *context, ctool_string_t path, ctool_bytes_t bytes) {
    (void)path;
    compile_capture_t *capture = context;
    return ctool_arena_copy_bytes(capture->arena, bytes, &capture->object);
}

static int read_compile_member(char **cursor, char *end, ctool_path_t *source, ctool_path_t *member) {
    char *input = cupid_guest_next_job_line(cursor, end);
    char *output = cupid_guest_next_job_line(cursor, end);
    if (!input || !output)
        return 0;
    source->text = ctool_string(input);
    member->text = ctool_string(output);
    return source->text.size > 1u && source->text.size < VFS_MAX_PATH &&
           ctool_path_is_canonical(source) && member->text.size > 3u &&
           member->text.size < VFS_MAX_PATH && ctool_path_is_canonical(member) &&
           output[member->text.size - 2u] == '.' && output[member->text.size - 1u] == 'o';
}

ctool_status_t cupid_guest_compile_bundle(ctool_invocation_t *invocation,
                                          const ctool_c_engine_request_t *profile,
                                          cupid_syscall_table_t *sys) {
    ctool_bytes_t manifest = invocation->input->contents;
    if (manifest.size == 0u)
        return CTOOL_ERR_INPUT;
    if (manifest.size >= COMPILE_BUNDLE_BYTES)
        return CTOOL_ERR_LIMIT;

    ctool_job_t *job = invocation->job;
    ctool_arena_t *arena = ctool_job_arena(job);
    char *text = NULL;
    ctool_status_t status = ctool_arena_alloc(arena, manifest.size + 1u, 1u, (void **)&text);
    if (status != CTOOL_OK)
        return status;
    sys->memcpy(text, manifest.data, manifest.size);
    char *cursor = text;
    char *end = text + manifest.size;

    compile_capture_t capture = {.io = {.sys = sys, .input_prefix = ctool_string("/src")},
                                 .arena = arena};
    ctool_c_engine_request_t request = *profile;
    request.config = cupid_guest_job_config(&capture.io);
    request.config.limits = profile->config.limits;
    request.config.files.write_all = capture_object;
    ctool_source_t objects[COMPILE_BUNDLE_LIMIT];
    ctool_obj_iso_fixture_entry_t entries[COMPILE_BUNDLE_LIMIT];
    ctool_u32 count = 0u;
    ctool_buffer_t *names = NULL;
    status = ctool_job_open_buffer(job, 4096u, COMPILE_BUNDLE_BYTES, &names);
    if (status != CTOOL_OK)
        return status;
    while (cursor < end && status == CTOOL_OK) {
        if (count == COMPILE_BUNDLE_LIMIT) {
            status = CTOOL_ERR_LIMIT;
            break;
        }
        ctool_path_t source;
        ctool_path_t member;
        if (!read_compile_member(&cursor, end, &source, &member)) {
            status = CTOOL_ERR_INPUT;
            break;
        }
        request.invocation = (ctool_invocation_request_t){source.text, member.text};
        ctool_invocation_result_t result = {0};
        status = ctool_c_engine_compile(&request, &result);
        cupid_guest_job_drain_cache(&capture.io);
        if (status == CTOOL_OK && !result.output_committed)
            status = CTOOL_ERR_IO;
        if (status != CTOOL_OK)
            break;

        ctool_string_t name = {member.text.data + 1u, member.text.size - 1u};
        objects[count] = (ctool_source_t){member, capture.object};
        entries[count] = (ctool_obj_iso_fixture_entry_t){name, CTOOL_OBJ_ISO_FIXTURE_FILE, &objects[count]};
        count++;
        status = ctool_buffer_append(names, ctool_bytes(name.data, name.size));
        if (status == CTOOL_OK)
            status = ctool_buffer_append(names, ctool_bytes("\n", 1u));
    }
    if (status == CTOOL_OK) {
        ctool_source_t inventory = {invocation->input->path, ctool_buffer_view(names)};
        const ctool_obj_request_t iso = {
            .operation = CTOOL_OBJ_BUILD_ISO_FIXTURE, .input = &inventory,
            .as = {.iso_fixture = {entries, count}}
        };
        ctool_obj_result_t result = {0};
        status = ctool_obj_transform(job, &iso, invocation->output, &result);
    }
    ctool_buffer_close(names);
    return status;
}
