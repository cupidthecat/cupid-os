/**
 * @file cupidasm_guest.cc
 * @brief Assemble and inspect a production relocatable object in Cupid OS.
 */

#include "cupidasm.h"
#include "cupiddis.h"
#include "guest_job.h"

#define ASM_JOB "/home/cupidasm.job"
#define ASM_PENDING "/home/.cupidasm-pending.o"

static ctool_status_t assemble_object(ctool_invocation_t *invocation, void *context) {
    cupid_syscall_table_t *sys = context;
    ctool_path_t root;
    ctool_status_t status = ctool_path_root(ctool_job_arena(invocation->job), &root);
    if (status != CTOOL_OK)
        return status;

    ctool_asm_request_t assembly = {
        .artifact = CTOOL_ASM_ARTIFACT_ELF32_REL,
        .initial_mode = CTOOL_X86_MODE_32,
        .include_roots = &root,
        .include_root_count = 1u
    };
    ctool_asm_result_t object = {0};
    status = ctool_asm_assemble(invocation->job, invocation->input, &assembly,
                                invocation->output, &object);
    if (status != CTOOL_OK)
        return status;

    ctool_source_t candidate = *invocation->input;
    candidate.contents = object.bytes;
    ctool_dis_request_t inspection = {
        .input = CTOOL_DIS_INPUT_ELF32,
        .views = CTOOL_DIS_VIEW_DISASSEMBLY,
        .policies = CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS | CTOOL_DIS_POLICY_CODE_ANCHORS
    };
    ctool_dis_report_t report = {0};
    status = ctool_dis_inspect(invocation->job, &candidate, &inspection, &report);
    if (status != CTOOL_OK)
        return status;

    const ctool_dis_decode_summary_t *summary = &report.decode_summary;
    if (summary->known_count == 0u || summary->unknown_count != 0u ||
        summary->invalid_count != 0u || summary->truncated_count != 0u ||
        summary->direct_relative_outside_section_count != 0u ||
        summary->direct_relative_mid_instruction_count != 0u ||
        summary->code_anchor_outside_executable_count != 0u ||
        summary->code_anchor_mid_instruction_count != 0u) {
        sys->serial_write_string("[cupidasm-guest] object code check failed\n");
        return CTOOL_ERR_INPUT;
    }
    return CTOOL_OK;
}

void cupidasm_guest_run(cupid_syscall_table_t *sys) {
    if (!sys || sys->version != CUPID_SYSCALL_VERSION || sys->table_size < sizeof(*sys))
        return;
    cupid_init(sys);
    char file[CUPID_GUEST_PATH_JOB_BYTES];
    ctool_invocation_request_t request;
    if (!cupid_guest_read_path_job(sys, ASM_JOB, file, &request) ||
        strcmp(request.output_path.data, ASM_PENDING) == 0) {
        sys->serial_write_string("[cupidasm-guest] invalid request file: " ASM_JOB "\n");
        return;
    }

    cupid_guest_io_t io = {
        .sys = sys, .input_prefix = ctool_string(""), .pending_path = ASM_PENDING
    };
    ctool_job_config_t config = cupid_guest_job_config(&io);
    ctool_invocation_result_t result = {0};
    ctool_status_t status = ctool_invoke(&config, &request, assemble_object, sys, &result);
    cupid_guest_job_drain_cache(&io);
    if (status == CTOOL_OK && result.output_committed) {
        sys->serial_write_string("[cupidasm-guest] wrote ");
        sys->serial_write_string(request.output_path.data);
    } else {
        sys->serial_write_string("[cupidasm-guest] assembly failed: ");
        sys->serial_write_string(ctool_status_name(status));
    }
    sys->serial_write_string("\n");
}
