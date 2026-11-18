#include "cupidc_engine.h"
#include "guest_job.h"

#define SOURCE_MOUNT "/src"
#define JOB_PATH "/home/cupidc.job"
#define PENDING_PATH "/home/.cupidc-pending.o"
#define JOB_ARENA_BYTES (192u * 1024u * 1024u)

static int read_request(char *file, ctool_invocation_request_t *invocation) {
    int size = __sys->vfs_read_text(JOB_PATH, file, 2u * VFS_MAX_PATH + 1u);
    if (size < 1)
        return 0;
    int split = 0;
    while (split < size && file[split] != '\n' && file[split] != '\0' && file[split] != '\r')
        split++;
    if (split == 0 || split >= VFS_MAX_PATH || split >= size || file[split] != '\n')
        return 0;
    file[split] = '\0';

    char *output = file + split + 1;
    int length = size - split - 1;
    if (length > 0 && output[length - 1] == '\n')
        output[--length] = '\0';
    if (length < 8 || length >= VFS_MAX_PATH || strncmp(output, "/home/", 6u) != 0 ||
        output[length - 2] != '.' || output[length - 1] != 'o' ||
        strcmp(output, PENDING_PATH) == 0)
        return 0;
    for (int i = 6; i < length; i++) {
        if (output[i] == '\0' || output[i] == '\n' || output[i] == '\r')
            return 0;
    }

    ctool_path_t input_path = {{file, (ctool_u32)split}};
    ctool_path_t output_path = {{output, (ctool_u32)length}};
    if (!ctool_path_is_canonical(&input_path) || !ctool_path_is_canonical(&output_path))
        return 0;
    invocation->input_path = input_path.text;
    invocation->output_path = output_path.text;
    return 1;
}

void cupidc_guest_run(cupid_syscall_table_t *sys) {
    if (!sys || sys->version != CUPID_SYSCALL_VERSION || sys->table_size < sizeof(*sys))
        return;
    cupid_init(sys);
    char file[2u * VFS_MAX_PATH + 1u];
    ctool_c_engine_request_t request = {0};
    if (!read_request(file, &request.invocation)) {
        sys->serial_write_string("[cupidc-guest] invalid request file: " JOB_PATH "\n");
        return;
    }

    ctool_c_pp_include_root_t roots[] = {
        {{ctool_string("/toolchain")}, CTOOL_C_PP_INCLUDE_QUOTED | CTOOL_C_PP_INCLUDE_ANGLE},
        {{ctool_string("/toolchain/hosted/i386-linux/include")}, CTOOL_C_PP_INCLUDE_ANGLE},
        {{ctool_string("/user")}, CTOOL_C_PP_INCLUDE_QUOTED}
    };
    request.abi = CTOOL_C_ENGINE_ABI;
    request.bytes = sizeof(request);
    cupid_guest_io_t io = {
        .sys = sys, .input_prefix = ctool_string(SOURCE_MOUNT), .pending_path = PENDING_PATH
    };
    request.config = cupid_guest_job_config(&io);
    request.config.limits.arena_bytes = JOB_ARENA_BYTES;
    request.preprocessor.mode = CTOOL_C_PP_MODE_C11;
    request.preprocessor.gnu_extensions = CTOOL_TRUE;
    request.preprocessor.include_roots = roots;
    request.preprocessor.include_root_count = sizeof(roots) / sizeof(roots[0]);
    request.parser.mode = request.preprocessor.mode;
    request.parser.gnu_extensions = request.preprocessor.gnu_extensions;

    ctool_invocation_result_t result = {0};
    ctool_status_t status = ctool_c_engine_compile(&request, &result);
    cupid_guest_job_drain_cache(&io);
    if (status == CTOOL_OK && result.output_committed) {
        sys->serial_write_string("[cupidc-guest] wrote ");
        sys->serial_write_string(request.invocation.output_path.data);
        sys->serial_write_string("\n");
    } else {
        sys->serial_write_string("[cupidc-guest] compilation failed: ");
        sys->serial_write_string(ctool_status_name(status));
        sys->serial_write_string("\n");
    }
}
