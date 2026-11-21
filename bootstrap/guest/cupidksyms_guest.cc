/**
 * @file cupidksyms_guest.cc
 * @brief Generate kernel symbol source from a linked ELF.
 */

#include "cupiddis.h"
#include "cupidobj.h"
#include "guest_build.h"

#define KSYMS_JOB "/home/cupidksyms.job"
#define KSYMS_PENDING "/home/.cupidksyms-pending.cc"
#define KSYMS_SOURCE_BYTES (16u * 1024u * 1024u)
#define KSYMS_ARENA_BYTES (96u * 1024u * 1024u)

static ctool_status_t append_symbols(void *context, ctool_bytes_t bytes) {
    return ctool_buffer_append((ctool_buffer_t *)context, bytes);
}

static ctool_status_t generate_symbols(ctool_invocation_t *invocation, void *context) {
    (void)context;
    ctool_dis_request_t inspect = {
        .input = CTOOL_DIS_INPUT_ELF32, .views = CTOOL_DIS_VIEW_SYMBOLS
    };
    ctool_dis_report_t report = {0};
    ctool_status_t status = ctool_dis_inspect(invocation->job, invocation->input, &inspect, &report);
    if (status != CTOOL_OK)
        return status;

    ctool_buffer_t *text = NULL;
    status = ctool_job_open_buffer(invocation->job, 4096u, KSYMS_SOURCE_BYTES, &text);
    if (status != CTOOL_OK)
        return status;
    ctool_text_sink_t sink = {text, append_symbols};
    status = ctool_dis_render(invocation->job, &report, CTOOL_DIS_TEXT_NM, sink);
    if (status == CTOOL_OK) {
        ctool_source_t symbols = {
            .path = {ctool_string("/kernel-symbols.nm")}, .contents = ctool_buffer_view(text)
        };
        ctool_obj_request_t request = {
            .operation = CTOOL_OBJ_GENERATE_KSYMS_SOURCE, .input = &symbols
        };
        ctool_obj_result_t result = {0};
        status = ctool_obj_transform(invocation->job, &request, invocation->output, &result);
    }
    ctool_buffer_close(text);
    return status;
}

ctool_status_t cupid_guest_symbols_job(cupid_syscall_table_t *sys, const char *job_path) {
    if (!sys || sys->version != CUPID_SYSCALL_VERSION || sys->table_size < sizeof(*sys))
        return CTOOL_ERR_INVALID_ARGUMENT;
    cupid_init(sys);
    char file[CUPID_GUEST_PATH_JOB_BYTES];
    ctool_invocation_request_t request;
    if (!cupid_guest_read_path_job(sys, job_path, file, &request) ||
        strcmp(request.output_path.data, KSYMS_PENDING) == 0) {
        sys->serial_write_string("[cupidksyms-guest] invalid request\n");
        return CTOOL_ERR_INPUT;
    }

    cupid_guest_io_t io = {
        .sys = sys, .input_prefix = ctool_string(""), .pending_path = KSYMS_PENDING
    };
    ctool_job_config_t config = cupid_guest_job_config(&io);
    config.limits.source_bytes = KSYMS_SOURCE_BYTES;
    config.limits.arena_bytes = KSYMS_ARENA_BYTES;
    ctool_invocation_result_t result = {0};
    ctool_status_t status = ctool_invoke(&config, &request, generate_symbols, NULL, &result);
    cupid_guest_job_drain_cache(&io);
    if (status == CTOOL_OK && !result.output_committed)
        status = CTOOL_ERR_IO;
    if (status == CTOOL_OK) {
        sys->serial_write_string("[cupidksyms-guest] wrote ");
        sys->serial_write_string(request.output_path.data);
    } else {
        sys->serial_write_string("[cupidksyms-guest] generation failed: ");
        sys->serial_write_string(ctool_status_name(status));
    }
    sys->serial_write_string("\n");
    return status;
}

void cupidksyms_guest_run(cupid_syscall_table_t *sys) {
    cupid_guest_symbols_job(sys, KSYMS_JOB);
}
