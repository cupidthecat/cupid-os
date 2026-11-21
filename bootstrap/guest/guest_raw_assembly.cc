/**
 * @file guest_raw_assembly.cc
 * @brief Assemble and validate the bootloader and AP trampoline in Cupid OS.
 */

#include "cupidasm.h"
#include "cupiddis.h"
#include "guest_job.h"

#define RAW_JOB "/home/cupidraw.job"
#define RAW_PENDING "/home/.cupidraw-pending.bin"
#define RAW_JOB_BYTES (CUPID_GUEST_PATH_JOB_BYTES + sizeof("smp-trampoline\n"))

typedef enum {
    GUEST_RAW_BOOTLOADER = 1,
    GUEST_RAW_SMP_TRAMPOLINE
} guest_raw_kind_t;

static int read_raw_job(cupid_syscall_table_t *sys, char *file, ctool_invocation_request_t *request,
                        guest_raw_kind_t *kind) {
    int size = sys->vfs_read_text(RAW_JOB, file, RAW_JOB_BYTES);
    if (size < 1 || (ctool_u32)size >= RAW_JOB_BYTES)
        return 0;
    char *cursor = file;
    char *end = file + size;
    ctool_path_t paths[2];
    if (!cupid_guest_read_path_lines(sys, &cursor, end, paths, 2u) ||
        strcmp(paths[1].text.data, RAW_PENDING) == 0)
        return 0;
    const char *mode = cupid_guest_next_job_line(&cursor, end);
    if (!mode || cursor != end)
        return 0;
    if (strcmp(mode, "bootloader") == 0)
        *kind = GUEST_RAW_BOOTLOADER;
    else if (strcmp(mode, "smp-trampoline") == 0)
        *kind = GUEST_RAW_SMP_TRAMPOLINE;
    else
        return 0;
    *request = (ctool_invocation_request_t){paths[0].text, paths[1].text};
    return 1;
}

static const char smp_map[] =
    "cupid.raw-map.v2\n"
    "size 4096\n"
    "base 0x00008000\n"
    "edges 6\n"
    "range 0x00000000 code16\n"
    "range 0x0000001f data\n"
    "range 0x00000210 code32\n"
    "range 0x00000254 data\n"
    "edge 0x00000017 far local 0x00000210 0x00008210 32 0x00000008\n"
    "edge 0x0000022f relative local 0x0000023a 0x0000823a 32 0x00000000\n"
    "edge 0x00000235 relative local 0x00000229 0x00008229 32 0x00000000\n"
    "edge 0x00000238 relative local 0x00000237 0x00008237 32 0x00000000\n"
    "edge 0x00000250 indirect unprovable - - unknown -\n"
    "edge 0x00000252 relative local 0x00000237 0x00008237 32 0x00000000\n";

static ctool_status_t inspect_smp_layout(ctool_job_t *job, const ctool_asm_result_t *image) {
    ctool_dis_raw_map_t policy = {0};
    const char *message = NULL;
    ctool_status_t status = ctool_dis_parse_raw_map(job, ctool_bytes(smp_map, sizeof(smp_map) - 1u),
                                                    &policy, &message);
    if (status != CTOOL_OK)
        return status;
    if (image->bytes.size != policy.image_size || image->raw_origin != policy.base_address ||
        image->raw_range_count != policy.range_count || image->raw_edge_count != policy.edge_count)
        return CTOOL_ERR_INPUT;
    for (ctool_u32 i = 0; i < policy.range_count; i++) {
        const ctool_asm_raw_range_t *range = &image->raw_ranges[i];
        if (range->offset != policy.ranges[i].offset ||
            (ctool_dis_raw_range_kind_t)range->kind != policy.ranges[i].kind)
            return CTOOL_ERR_INPUT;
    }
    for (ctool_u32 i = 0; i < policy.edge_count; i++) {
        const ctool_asm_raw_edge_t *edge = &image->raw_edges[i];
        const ctool_dis_raw_edge_t *expected = &policy.edges[i];
        if (edge->source_offset != expected->source_offset ||
            (ctool_dis_raw_edge_kind_t)edge->kind != expected->kind ||
            (ctool_dis_raw_edge_class_t)edge->class_id != expected->class_id ||
            edge->target_offset != expected->target_offset ||
            edge->target_address != expected->target_address ||
            edge->target_mode != expected->target_mode || edge->target_segment != expected->target_segment)
            return CTOOL_ERR_INPUT;
    }
    return CTOOL_OK;
}

static ctool_status_t check_raw_image(ctool_job_t *job, const ctool_source_t *source,
                                      const ctool_asm_result_t *image, guest_raw_kind_t kind) {
    if (kind == GUEST_RAW_BOOTLOADER && image->bytes.size != 2560u)
        return CTOOL_ERR_INPUT;
    if (kind == GUEST_RAW_SMP_TRAMPOLINE) {
        ctool_status_t status = inspect_smp_layout(job, image);
        if (status != CTOOL_OK)
            return status;
    }
    if (image->raw_range_count == 0u)
        return CTOOL_ERR_INPUT;

    ctool_arena_t *arena = ctool_job_arena(job);
    ctool_dis_raw_range_t *ranges = NULL;
    ctool_dis_raw_edge_t *edges = NULL;
    ctool_status_t status = ctool_arena_alloc_zero(arena, image->raw_range_count, sizeof(*ranges),
                                                    4u, (void **)&ranges);
    if (status == CTOOL_OK && image->raw_edge_count != 0u)
        status = ctool_arena_alloc_zero(arena, image->raw_edge_count, sizeof(*edges),
                                        4u, (void **)&edges);
    if (status != CTOOL_OK)
        return status;
    for (ctool_u32 i = 0; i < image->raw_range_count; i++) {
        const ctool_asm_raw_range_t *range = &image->raw_ranges[i];
        ranges[i] = (ctool_dis_raw_range_t){range->offset, (ctool_dis_raw_range_kind_t)range->kind};
    }
    for (ctool_u32 i = 0; i < image->raw_edge_count; i++) {
        const ctool_asm_raw_edge_t *edge = &image->raw_edges[i];
        edges[i] = (ctool_dis_raw_edge_t){
            edge->source_offset, (ctool_dis_raw_edge_kind_t)edge->kind,
            (ctool_dis_raw_edge_class_t)edge->class_id, edge->target_offset,
            edge->target_address, edge->target_mode, edge->target_segment
        };
    }

    ctool_source_t candidate = *source;
    candidate.contents = image->bytes;
    const ctool_dis_request_t inspection = {
        .input = CTOOL_DIS_INPUT_RAW, .views = CTOOL_DIS_VIEW_DISASSEMBLY,
        .policies = CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS | CTOOL_DIS_POLICY_SOURCE_CONTROL_EDGES,
        .raw_mode = CTOOL_DIS_RAW_RANGE_MAP, .raw_base_address = image->raw_origin,
        .raw_ranges = ranges, .raw_range_count = image->raw_range_count,
        .raw_edges = edges, .raw_edge_count = image->raw_edge_count,
        .raw_edge_metadata_present = CTOOL_TRUE
    };
    ctool_dis_report_t report = {0};
    status = ctool_dis_inspect(job, &candidate, &inspection, &report);
    if (status != CTOOL_OK)
        return status;
    const ctool_dis_decode_summary_t *summary = &report.decode_summary;
    if (summary->known_count == 0u || summary->unknown_count != 0u ||
        summary->invalid_count != 0u || summary->truncated_count != 0u ||
        summary->direct_relative_outside_image_count != 0u ||
        summary->direct_relative_data_count != 0u || summary->direct_relative_wrong_mode_count != 0u ||
        summary->direct_relative_mid_instruction_count != 0u ||
        summary->source_control_edge_invalid_count != 0u)
        return CTOOL_ERR_INPUT;
    return CTOOL_OK;
}

static ctool_status_t assemble_raw_image(ctool_invocation_t *invocation, void *context) {
    ctool_path_t root;
    ctool_status_t status = ctool_path_root(ctool_job_arena(invocation->job), &root);
    if (status != CTOOL_OK)
        return status;
    const ctool_asm_request_t assembly = {
        .artifact = CTOOL_ASM_ARTIFACT_RAW, .initial_mode = CTOOL_X86_MODE_32,
        .include_roots = &root, .include_root_count = 1u
    };
    ctool_asm_result_t image = {0};
    status = ctool_asm_assemble(invocation->job, invocation->input, &assembly,
                                invocation->output, &image);
    if (status != CTOOL_OK)
        return status;
    return check_raw_image(invocation->job, invocation->input, &image, *(guest_raw_kind_t *)context);
}

void cupidraw_guest_run(cupid_syscall_table_t *sys) {
    if (!sys || sys->version != CUPID_SYSCALL_VERSION || sys->table_size < sizeof(*sys))
        return;
    cupid_init(sys);
    char file[RAW_JOB_BYTES];
    ctool_invocation_request_t request;
    guest_raw_kind_t kind;
    if (!read_raw_job(sys, file, &request, &kind)) {
        sys->serial_write_string("[cupidraw-guest] invalid request file: " RAW_JOB "\n");
        return;
    }

    cupid_guest_io_t io = {.sys = sys, .pending_path = RAW_PENDING};
    ctool_job_config_t config = cupid_guest_job_config(&io);
    ctool_invocation_result_t result = {0};
    ctool_status_t status = ctool_invoke(&config, &request, assemble_raw_image, &kind, &result);
    cupid_guest_job_drain_cache(&io);
    if (status == CTOOL_OK && result.output_committed) {
        sys->serial_write_string("[cupidraw-guest] wrote ");
        sys->serial_write_string(request.output_path.data);
    } else {
        sys->serial_write_string("[cupidraw-guest] assembly failed: ");
        sys->serial_write_string(ctool_status_name(status));
    }
    sys->serial_write_string("\n");
}
