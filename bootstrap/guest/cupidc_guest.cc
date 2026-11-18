#include "cupidc_engine.h"
#include "cupid.h"

#define SOURCE_MOUNT "/src"
#define JOB_PATH "/home/cupidc.job"
#define PENDING_PATH "/home/.cupidc-pending.o"
#define JOB_ARENA_BYTES (192u * 1024u * 1024u)
#define GUEST_ENOENT (-2)
#define GUEST_ENOSPC (-28)

static ctool_status_t file_failure(int status) {
    if (status == GUEST_ENOENT)
        return CTOOL_ERR_NOT_FOUND;
    if (status == GUEST_ENOSPC)
        return CTOOL_ERR_LIMIT;
    return CTOOL_ERR_IO;
}

static ctool_status_t source_path(ctool_string_t logical, char path[VFS_MAX_PATH]) {
    ctool_path_t candidate = {logical};
    size_t prefix = sizeof(SOURCE_MOUNT) - 1u;
    if (!ctool_path_is_canonical(&candidate))
        return CTOOL_ERR_PATH;
    if (logical.size >= VFS_MAX_PATH - prefix)
        return CTOOL_ERR_LIMIT;
    memcpy(path, SOURCE_MOUNT, prefix);
    memcpy(path + prefix, logical.data, logical.size);
    path[prefix + logical.size] = '\0';
    return CTOOL_OK;
}

static ctool_status_t guest_size(void *context, ctool_string_t logical, ctool_u32 *size) {
    (void)context;
    char path[VFS_MAX_PATH];
    ctool_status_t status = source_path(logical, path);
    if (status != CTOOL_OK)
        return status;
    cupid_stat_t information;
    int result = stat(path, &information);
    if (result < 0)
        return file_failure(result);
    if (information.type != VFS_TYPE_FILE)
        return CTOOL_ERR_INPUT;
    *size = information.size;
    return CTOOL_OK;
}

static ctool_status_t guest_read(void *context, ctool_string_t logical, ctool_u8 *bytes,
                                 ctool_u32 size) {
    (void)context;
    char path[VFS_MAX_PATH];
    ctool_status_t status = source_path(logical, path);
    if (status != CTOOL_OK)
        return status;
    int count = __sys->vfs_read_all(path, bytes, size);
    if (count < 0)
        return file_failure(count);
    return (ctool_u32)count == size ? CTOOL_OK : CTOOL_ERR_IO;
}

static ctool_status_t guest_write(void *context, ctool_string_t path, ctool_bytes_t contents) {
    (void)context;
    cupid_stat_t pending;
    int status = stat(PENDING_PATH, &pending);
    if (status != GUEST_ENOENT)
        return file_failure(status);
    status = __sys->vfs_write_all(PENDING_PATH, contents.data, contents.size);
    if (status >= 0 && (ctool_u32)status == contents.size)
        status = __sys->vfs_rename(PENDING_PATH, path.data);
    else if (status >= 0)
        status = -1;
    if (status < 0) {
        unlink(PENDING_PATH);
        return file_failure(status);
    }
    return CTOOL_OK;
}

static ctool_status_t guest_diagnostic(void *context, ctool_bytes_t text) {
    (void)context;
    for (ctool_u32 i = 0; i < text.size; i++)
        __sys->serial_write_char((char)text.data[i]);
    return CTOOL_OK;
}

static void *guest_allocate(void *context, ctool_u32 size) {
    (void)context;
    return malloc(size);
}

static void guest_release(void *context, void *allocation, ctool_u32 size) {
    (void)context;
    (void)size;
    free(allocation);
}

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
        {{ctool_string("/toolchain/hosted/i386-linux/include")}, CTOOL_C_PP_INCLUDE_ANGLE}
    };
    request.abi = CTOOL_C_ENGINE_ABI;
    request.bytes = sizeof(request);
    request.config.limits = ctool_default_limits();
    request.config.limits.arena_bytes = JOB_ARENA_BYTES;
    request.config.allocator = (ctool_allocator_t){NULL, guest_allocate, guest_release};
    request.config.files = (ctool_file_store_t){NULL, guest_size, guest_read, guest_write};
    request.config.diagnostics = (ctool_text_sink_t){NULL, guest_diagnostic};
    request.preprocessor.mode = CTOOL_C_PP_MODE_C11;
    request.preprocessor.gnu_extensions = CTOOL_TRUE;
    request.preprocessor.include_roots = roots;
    request.preprocessor.include_root_count = sizeof(roots) / sizeof(roots[0]);
    request.parser.mode = request.preprocessor.mode;
    request.parser.gnu_extensions = request.preprocessor.gnu_extensions;

    ctool_invocation_result_t result = {0};
    ctool_status_t status = ctool_c_engine_compile(&request, &result);
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
