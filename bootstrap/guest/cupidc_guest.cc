/**
 * @file cupidc_guest.cc
 * @brief Compile a source or a source list and publish its output to HomeFS.
 */

#include "cupidc_bundle.h"
#include "guest_build.h"

#define SOURCE_MOUNT "/src"
#define JOB_PATH "/home/cupidc.job"
#define PENDING_LEAF ".cupidc-pending.o"
#define PENDING_PATH "/home/" PENDING_LEAF
#define JOB_ARENA_BYTES (192u * 1024u * 1024u)
#define INCLUDE_ROOT_LIMIT 64u
#define FORCED_INCLUDE_LIMIT 16u
#define MACRO_LIMIT 64u
/* Allow both the name and replacement of every definition to reach their limit. */
#define JOB_BYTES (CUPID_GUEST_PATH_JOB_BYTES + sizeof("doom\n") - 1u + \
                   (INCLUDE_ROOT_LIMIT + FORCED_INCLUDE_LIMIT) * (VFS_MAX_PATH + 8u) + \
                   MACRO_LIMIT * (2u * VFS_MAX_PATH + 8u) + sizeof("bundle\n"))
#define COMPILE_LIST_BYTES (JOB_BYTES + CUPID_GUEST_OBJECT_LIST_BYTES)

typedef struct {
    ctool_c_pp_include_root_t roots[INCLUDE_ROOT_LIMIT];
    ctool_path_t forced[FORCED_INCLUDE_LIMIT];
    ctool_c_pp_macro_action_t macros[MACRO_LIMIT];
} guest_compile_options_t;

static int read_option(char *line, ctool_c_engine_request_t *request,
                       guest_compile_options_t *options) {
    ctool_c_pp_request_t *pp = &request->preprocessor;
    pp->include_roots = options->roots;
    pp->forced_includes = options->forced;
    pp->macro_actions = options->macros;
    if (strcmp(line, "doom") == 0) {
        if (request->parser.implicit_function_declarations)
            return 0;
        request->parser.implicit_function_declarations = CTOOL_TRUE;
        request->parser.compatibility_pointer_conversions = CTOOL_TRUE;
        return 1;
    }

    char *argument = line;
    while (*argument && *argument != ' ')
        argument++;
    if (*argument == '\0' || argument[1] == '\0')
        return 0;
    *argument++ = '\0';
    if (strcmp(line, "define") == 0) {
        if (pp->macro_action_count == MACRO_LIMIT)
            return 0;
        char *value = argument;
        while (*value && *value != '=')
            value++;
        ctool_string_t replacement = ctool_string("1");
        if (*value) {
            *value++ = '\0';
            replacement = ctool_string(value);
        }
        if (*argument == '\0' || strlen(argument) >= VFS_MAX_PATH ||
            replacement.size >= VFS_MAX_PATH)
            return 0;
        ctool_c_pp_macro_action_t macro = {CTOOL_C_PP_MACRO_DEFINE, ctool_string(argument), replacement};
        options->macros[pp->macro_action_count++] = macro;
        return 1;
    }

    ctool_path_t path = {ctool_string(argument)};
    if (path.text.size >= VFS_MAX_PATH || !ctool_path_is_canonical(&path))
        return 0;
    if (strcmp(line, "force") == 0) {
        if (pp->forced_include_count == FORCED_INCLUDE_LIMIT)
            return 0;
        options->forced[pp->forced_include_count++] = path;
        return 1;
    }
    ctool_u32 forms = CTOOL_C_PP_INCLUDE_ANGLE;
    if (strcmp(line, "include") == 0)
        forms |= CTOOL_C_PP_INCLUDE_QUOTED;
    else if (strcmp(line, "angle") != 0)
        return 0;
    if (pp->include_root_count == INCLUDE_ROOT_LIMIT)
        return 0;
    ctool_c_pp_include_root_t root = {path, forms};
    options->roots[pp->include_root_count++] = root;
    return 1;
}

static int read_profile(char **cursor, char *end, const char *delimiter,
                        ctool_c_engine_request_t *request, guest_compile_options_t *options) {
    while (*cursor < end) {
        char *line = cupid_guest_next_job_line(cursor, end);
        if (!line)
            return 0;
        if (delimiter && strcmp(line, delimiter) == 0)
            return 1;
        if (!read_option(line, request, options))
            return 0;
    }
    return delimiter == NULL;
}

static int read_request(const cupid_guest_request_t *job, char *file, ctool_c_engine_request_t *request,
                        guest_compile_options_t *options, int *bundle) {
    int size = cupid_guest_read_request(__sys, job, file, JOB_BYTES);
    if (size < 1 || (ctool_u32)size >= JOB_BYTES)
        return 0;
    char *cursor = file;
    char *end = file + size;
    ctool_path_t paths[2];
    if (!cupid_guest_read_path_lines(__sys, &cursor, end, paths, 2u))
        return 0;
    ctool_string_t output = paths[1].text;
    ctool_u32 prefix = cupid_guest_output_prefix(__sys, output);
    if (output.size < 8u || strcmp(output.data + prefix, PENDING_LEAF) == 0)
        return 0;
    if (output.size >= 10u && strcmp(output.data + output.size - 4u, ".iso") == 0) {
        char *mode = cupid_guest_next_job_line(&cursor, end);
        if (!mode || strcmp(mode, "bundle") != 0)
            return 0;
        *bundle = 1;
    } else if (strcmp(output.data + output.size - 2u, ".o") != 0) {
        return 0;
    }
    request->invocation.input_path = paths[0].text;
    request->invocation.output_path = output;
    return read_profile(&cursor, end, NULL, request, options);
}

static ctool_status_t compile_bundle(ctool_invocation_t *invocation, void *profile) {
    return cupid_guest_compile_bundle(invocation, profile, __sys);
}

static ctool_status_t compile_request(cupid_syscall_table_t *sys, const ctool_c_engine_request_t *parsed,
                                      int bundle) {
    ctool_c_engine_request_t request = *parsed;
    ctool_c_pp_include_root_t roots[] = {
        {{ctool_string("/toolchain")}, CTOOL_C_PP_INCLUDE_QUOTED | CTOOL_C_PP_INCLUDE_ANGLE},
        {{ctool_string("/toolchain/hosted/i386-linux/include")}, CTOOL_C_PP_INCLUDE_ANGLE},
        {{ctool_string("/user")}, CTOOL_C_PP_INCLUDE_QUOTED}
    };
    request.abi = CTOOL_C_ENGINE_ABI;
    request.bytes = sizeof(request);
    const char *source_mount =
        bundle || cupid_guest_output_prefix(sys, request.invocation.input_path) ? "" : SOURCE_MOUNT;
    cupid_guest_io_t io = {
        .sys = sys, .input_prefix = ctool_string(source_mount), .pending_path = PENDING_PATH
    };
    request.config = cupid_guest_job_config(&io);
    request.config.limits.arena_bytes = JOB_ARENA_BYTES;
    request.preprocessor.mode = CTOOL_C_PP_MODE_C11;
    request.preprocessor.gnu_extensions = CTOOL_TRUE;
    if (!request.preprocessor.include_roots) {
        request.preprocessor.include_roots = roots;
        request.preprocessor.include_root_count = sizeof(roots) / sizeof(roots[0]);
    }
    request.parser.mode = request.preprocessor.mode;
    request.parser.gnu_extensions = request.preprocessor.gnu_extensions;

    ctool_invocation_result_t result = {0};
    ctool_status_t status = bundle
        ? ctool_invoke(&request.config, &request.invocation, compile_bundle, &request, &result)
        : ctool_c_engine_compile(&request, &result);
    cupid_guest_job_drain_cache(&io);
    if (status == CTOOL_OK && !result.output_committed)
        status = CTOOL_ERR_IO;
    if (status == CTOOL_OK) {
        sys->serial_write_string("[cupidc-guest] wrote ");
        sys->serial_write_string(request.invocation.output_path.data);
        sys->serial_write_string("\n");
    } else {
        sys->serial_write_string("[cupidc-guest] compilation failed: ");
        sys->serial_write_string(ctool_status_name(status));
        sys->serial_write_string("\n");
    }
    return status;
}

ctool_status_t cupid_guest_compile_job(cupid_syscall_table_t *sys, const cupid_guest_request_t *job) {
    if (!sys || sys->version != CUPID_SYSCALL_VERSION || sys->table_size < sizeof(*sys))
        return CTOOL_ERR_INVALID_ARGUMENT;
    cupid_init(sys);
    char file[JOB_BYTES];
    ctool_c_engine_request_t request = {0};
    guest_compile_options_t options;
    int bundle = 0;
    if (read_request(job, file, &request, &options, &bundle))
        return compile_request(sys, &request, bundle);
    sys->serial_write_string("[cupidc-guest] invalid request file: ");
    sys->serial_write_string(job->path);
    sys->serial_write_string("\n");
    return CTOOL_ERR_INPUT;
}

ctool_status_t cupid_guest_compile_list(cupid_syscall_table_t *sys, const cupid_guest_request_t *job) {
    cupid_init(sys);
    char *file = sys->malloc(COMPILE_LIST_BYTES);
    if (!file)
        return CTOOL_ERR_NO_MEMORY;
    int size = cupid_guest_read_request(sys, job, file, COMPILE_LIST_BYTES);
    ctool_c_engine_request_t request = {0};
    guest_compile_options_t options;
    ctool_invocation_request_t items[CUPID_GUEST_OBJECT_LIST_LIMIT];
    ctool_u32 count = 0u;
    if (size > 0 && (ctool_u32)size < COMPILE_LIST_BYTES) {
        char *cursor = file;
        char *end = file + size;
        if (read_profile(&cursor, end, "sources", &request, &options))
            count = cupid_guest_read_object_list(sys, cursor, end, PENDING_LEAF, items);
    }
    ctool_status_t status = count ? CTOOL_OK : CTOOL_ERR_INPUT;
    if (status != CTOOL_OK)
        sys->serial_write_string("[cupidc-guest] invalid compile list\n");
    for (ctool_u32 i = 0; status == CTOOL_OK && i < count; i++) {
        request.invocation = items[i];
        status = compile_request(sys, &request, 0);
        if (status != CTOOL_OK) {
            sys->serial_write_string("[cupidc-guest] list stopped at ");
            sys->serial_write_string(items[i].input_path.data);
            sys->serial_write_string("\n");
        }
    }
    sys->free(file);
    return status;
}

void cupidc_guest_run(cupid_syscall_table_t *sys) {
    cupid_guest_compile_job(sys, &(cupid_guest_request_t){.path = JOB_PATH});
}
