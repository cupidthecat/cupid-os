/**
 * dis.cc - CupidDis kernel adapters
 */

#include "dis.h"
#include "ctool_kernel.h"
#include "cupiddis.h"
#include "kernel.h"
#include "vfs.h"

#ifdef CTOOL_KERNEL_ADAPTER_HOST_CONTRACT
void print(const char *text);
#endif

#define DIS_SINK_CHUNK_BYTES 128u
#define DIS_SOURCE_BYTES 67108864u
#define DIS_ARENA_BYTES 134217728u

typedef struct {
    dis_output_fn output;
} dis_sink_context_t;

static void dis_out(dis_output_fn output, const char *text) {
    if (output != (dis_output_fn)0) {
        output(text);
    } else {
        print(text);
    }
}

static ctool_status_t dis_sink_write(void *context, ctool_bytes_t text) {
    dis_sink_context_t *sink = (dis_sink_context_t *)context;
    ctool_u32 offset = 0u;

    if (sink == (dis_sink_context_t *)0 ||
        (text.data == (const ctool_u8 *)0 && text.size != 0u)) {
        return CTOOL_ERR_INVALID_ARGUMENT;
    }
    while (offset < text.size) {
        char chunk[DIS_SINK_CHUNK_BYTES + 1u];
        ctool_u32 count = text.size - offset;
        ctool_u32 index;
        if (count > DIS_SINK_CHUNK_BYTES) {
            count = DIS_SINK_CHUNK_BYTES;
        }
        for (index = 0u; index < count; index++) {
            chunk[index] = (char)text.data[offset + index];
        }
        chunk[count] = '\0';
        dis_out(sink->output, chunk);
        offset += count;
    }
    return CTOOL_OK;
}

static ctool_text_sink_t dis_text_sink(dis_sink_context_t *context) {
    ctool_text_sink_t sink;
    sink.context = context;
    sink.write = dis_sink_write;
    return sink;
}

static ctool_job_config_t dis_job_config(dis_sink_context_t *context) {
    ctool_limits_t limits = ctool_default_limits();
    ctool_job_config_t config;
    limits.source_bytes = DIS_SOURCE_BYTES;
    limits.arena_bytes = DIS_ARENA_BYTES;
    config = ctool_kernel_job_config(limits);
    config.diagnostics = dis_text_sink(context);
    return config;
}

static int dis_vfs_status(ctool_status_t status) {
    switch (status) {
    case CTOOL_OK:
        return VFS_OK;
    case CTOOL_ERR_NOT_FOUND:
        return VFS_ENOENT;
    case CTOOL_ERR_IO:
        return VFS_EIO;
    case CTOOL_ERR_NO_MEMORY:
    case CTOOL_ERR_LIMIT:
        return VFS_ENOSPC;
    default:
        return VFS_EINVAL;
    }
}

static void dis_report_failure(ctool_job_t *job, dis_output_fn output,
                               ctool_status_t status, const char *fallback) {
    if (job != (ctool_job_t *)0 && ctool_job_diagnostic_count(job) != 0u) {
        (void)ctool_job_render_diagnostics(job);
        return;
    }
    if (status == CTOOL_ERR_NOT_FOUND) {
        dis_out(output, "dis: file not found\n");
    } else if (status == CTOOL_ERR_NO_MEMORY || status == CTOOL_ERR_LIMIT) {
        dis_out(output, "dis: out of memory\n");
    } else if (status == CTOOL_ERR_IO) {
        dis_out(output, "dis: read failed\n");
    } else {
        dis_out(output, fallback);
    }
}

int dis_disassemble_raw(const uint8_t *buffer, uint32_t size,
                        const dis_raw_request_t *raw_request,
                        const dis_sym_t *symbols, int symbol_count,
                        dis_output_fn output) {
    dis_sink_context_t sink_context;
    ctool_job_config_t config;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_dis_label_t *labels = (ctool_dis_label_t *)0;
    ctool_source_t source;
    ctool_dis_request_t request;
    ctool_dis_report_t report;
    ctool_status_t status;
    ctool_bool strict_failed = CTOOL_FALSE;
    ctool_u32 count = 0u;
    ctool_u32 index;

    if (raw_request == (const dis_raw_request_t *)0) {
        dis_out(output, "dis: invalid raw request\n");
        return VFS_EINVAL;
    }
    if (buffer == (const uint8_t *)0 || size == 0u) {
        dis_out(output, "dis: empty code buffer\n");
        return VFS_EINVAL;
    }
    if (symbols != (const dis_sym_t *)0 && symbol_count > 0) {
        count = (ctool_u32)symbol_count;
        if (count > DIS_MAX_SYMS) {
            count = DIS_MAX_SYMS;
        }
    }

    sink_context.output = output;
    config = dis_job_config(&sink_context);
    status = ctool_job_open(&config, &job);
    if (status == CTOOL_OK && count != 0u) {
        status = ctool_arena_alloc_zero(
            ctool_job_arena(job), count,
            (ctool_u32)sizeof(ctool_dis_label_t),
            (ctool_u32)sizeof(ctool_u32), (void **)&labels);
    }
    if (status == CTOOL_OK) {
        for (index = 0u; index < count; index++) {
            labels[index].address = symbols[index].addr;
            labels[index].name = ctool_string(symbols[index].name);
        }
        source.path.text = ctool_string("/jit-code.bin");
        source.contents = ctool_bytes(buffer, size);
        request.input = CTOOL_DIS_INPUT_RAW;
        request.views = CTOOL_DIS_VIEW_DISASSEMBLY;
        request.policies = 0u;
        request.raw_mode = raw_request->mode;
        request.raw_base_address = raw_request->base_address;
        request.raw_ranges = raw_request->ranges;
        request.raw_range_count = raw_request->range_count;
        request.raw_edges = (const ctool_dis_raw_edge_t *)0;
        request.raw_edge_count = 0u;
        request.raw_edge_metadata_present = CTOOL_FALSE;
        request.labels = labels;
        request.label_count = count;
        status = ctool_dis_inspect(job, &source, &request, &report);
    }
    if (status == CTOOL_OK && raw_request->require_known != CTOOL_FALSE &&
        (report.decode_summary.unknown_count != 0u ||
         report.decode_summary.invalid_count != 0u ||
         report.decode_summary.truncated_count != 0u)) {
        status = CTOOL_ERR_INPUT;
        strict_failed = CTOOL_TRUE;
    }
    if (status == CTOOL_OK) {
        status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                                  dis_text_sink(&sink_context));
    }
    if (status != CTOOL_OK) {
        const char *fallback =
            strict_failed != CTOOL_FALSE
                ? "dis: code check failed: unknown, invalid, or truncated instruction\n"
                : "dis: disassembly failed\n";
        dis_report_failure(job, output, status, fallback);
    }
    if (job != (ctool_job_t *)0) {
        ctool_job_close(job);
    }
    return dis_vfs_status(status);
}

void dis_disassemble(const uint8_t *buffer, uint32_t size,
                     uint32_t base_address, const dis_sym_t *symbols,
                     int symbol_count, dis_output_fn output) {
    dis_raw_request_t request;
    request.mode = CTOOL_X86_MODE_32;
    request.base_address = base_address;
    request.ranges = (const ctool_dis_raw_range_t *)0;
    request.range_count = 0u;
    request.require_known = CTOOL_FALSE;
    (void)dis_disassemble_raw(buffer, size, &request, symbols, symbol_count,
                              output);
}

int dis_elf(const char *path, dis_output_fn output) {
    dis_sink_context_t sink_context;
    ctool_job_config_t config;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_path_t root;
    ctool_path_t input_path;
    ctool_source_t source;
    ctool_dis_request_t request;
    ctool_dis_report_t report;
    ctool_status_t status;

    if (path == (const char *)0 || path[0] == '\0') {
        dis_out(output, "dis: invalid path\n");
        return VFS_EINVAL;
    }

    sink_context.output = output;
    config = dis_job_config(&sink_context);
    status = ctool_job_open(&config, &job);
    if (status == CTOOL_OK) {
        status = ctool_path_root(ctool_job_arena(job), &root);
    }
    if (status == CTOOL_OK) {
        status = ctool_path_resolve(ctool_job_arena(job), &root,
                                    ctool_string(path),
                                    config.limits.path_bytes, &input_path);
    }
    if (status == CTOOL_OK) {
        status = ctool_job_load_source(job, &input_path, &source);
    }
    if (status == CTOOL_OK) {
        request.input = CTOOL_DIS_INPUT_ELF32;
        request.views = CTOOL_DIS_VIEW_DISASSEMBLY;
        request.policies = 0u;
        request.raw_mode = CTOOL_X86_MODE_32;
        request.raw_base_address = 0u;
        request.raw_ranges = (const ctool_dis_raw_range_t *)0;
        request.raw_range_count = 0u;
        request.raw_edges = (const ctool_dis_raw_edge_t *)0;
        request.raw_edge_count = 0u;
        request.raw_edge_metadata_present = CTOOL_FALSE;
        request.labels = (const ctool_dis_label_t *)0;
        request.label_count = 0u;
        status = ctool_dis_inspect(job, &source, &request, &report);
    }
    if (status == CTOOL_OK) {
        status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                                  dis_text_sink(&sink_context));
    }
    if (status != CTOOL_OK) {
        dis_report_failure(job, output, status,
                           "dis: not a valid ELF32 i386 file\n");
    }
    if (job != (ctool_job_t *)0) {
        ctool_job_close(job);
    }
    return dis_vfs_status(status);
}

static ctool_status_t dis_load_file(ctool_job_t *job, const char *path,
                                    ctool_source_t *source) {
    ctool_path_t root;
    ctool_path_t input_path;
    ctool_status_t status = ctool_path_root(ctool_job_arena(job), &root);
    if (status == CTOOL_OK) {
        status = ctool_path_resolve(ctool_job_arena(job), &root,
                                    ctool_string(path),
                                    ctool_job_limits(job)->path_bytes,
                                    &input_path);
    }
    if (status == CTOOL_OK) {
        status = ctool_job_load_source(job, &input_path, source);
    }
    return status;
}

int dis_raw_file(const char *path,
                 const dis_raw_file_request_t *file_request,
                 dis_output_fn output) {
    dis_sink_context_t sink_context;
    ctool_job_config_t config;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_job_t *map_job = (ctool_job_t *)0;
    ctool_job_t *failure_job = (ctool_job_t *)0;
    ctool_source_t source;
    ctool_source_t map_source;
    ctool_dis_request_t request = {0};
    ctool_dis_raw_map_t map;
    ctool_dis_report_t report;
    ctool_status_t status;
    const char *map_message = (const char *)0;
    const char *fallback = "dis: disassembly failed\n";
    ctool_bool mapped;

    if (path == (const char *)0 || path[0] == '\0' ||
        file_request == (const dis_raw_file_request_t *)0) {
        dis_out(output, "dis: invalid raw file request\n");
        return VFS_EINVAL;
    }
    mapped = file_request->range_map_path != (const char *)0
                 ? CTOOL_TRUE : CTOOL_FALSE;
    if ((mapped != CTOOL_FALSE &&
         (file_request->range_map_path[0] == '\0' ||
          file_request->mode != CTOOL_DIS_RAW_RANGE_MAP ||
          file_request->base_address != 0u)) ||
        (mapped == CTOOL_FALSE &&
         file_request->mode != CTOOL_X86_MODE_16 &&
         file_request->mode != CTOOL_X86_MODE_32)) {
        dis_out(output, "dis: invalid raw file request\n");
        return VFS_EINVAL;
    }

    sink_context.output = output;
    config = dis_job_config(&sink_context);
    status = ctool_job_open(&config, &job);
    failure_job = job;
    if (status == CTOOL_OK) {
        status = dis_load_file(job, path, &source);
    }
    request.input = CTOOL_DIS_INPUT_RAW;
    request.views = CTOOL_DIS_VIEW_DISASSEMBLY;
    request.raw_mode = file_request->mode;
    request.raw_base_address = file_request->base_address;
    if (status == CTOOL_OK && mapped != CTOOL_FALSE) {
        /* Keep map storage alive through inspection and rendering. The
         * separate source budget rejects oversized maps before reading. */
        config.limits.source_bytes = CTOOL_DIS_RAW_MAP_MAX_BYTES;
        status = ctool_job_open(&config, &map_job);
        failure_job = map_job;
        if (status == CTOOL_OK) {
            status = dis_load_file(map_job, file_request->range_map_path,
                                    &map_source);
        }
        if (status == CTOOL_OK) {
            status = ctool_dis_parse_raw_map(map_job, map_source.contents,
                                              &map, &map_message);
        }
        if (status == CTOOL_OK && map.image_size != source.contents.size) {
            status = CTOOL_ERR_INPUT;
            fallback = "dis: raw range map size does not match input\n";
        }
        if (status == CTOOL_OK) {
            request.raw_base_address = map.base_address;
            request.raw_ranges = map.ranges;
            request.raw_range_count = map.range_count;
            request.raw_edges = map.edges;
            request.raw_edge_count = map.edge_count;
            request.raw_edge_metadata_present = map.edge_metadata_present;
        }
    }
    if (status == CTOOL_OK) {
        failure_job = job;
        status = ctool_dis_inspect(job, &source, &request, &report);
    }
    if (status == CTOOL_OK && file_request->require_known != CTOOL_FALSE &&
        (report.decode_summary.unknown_count != 0u ||
         report.decode_summary.invalid_count != 0u ||
         report.decode_summary.truncated_count != 0u)) {
        status = CTOOL_ERR_INPUT;
        fallback = "dis: code check failed: unknown, invalid, or truncated instruction\n";
    }
    if (status == CTOOL_OK) {
        status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                                  dis_text_sink(&sink_context));
    }
    if (status != CTOOL_OK) {
        if (map_message != (const char *)0) {
            dis_out(output, "dis: ");
            dis_out(output, map_message);
            dis_out(output, "\n");
        } else {
            dis_report_failure(failure_job, output, status, fallback);
        }
    }
    if (map_job != (ctool_job_t *)0) ctool_job_close(map_job);
    if (job != (ctool_job_t *)0) ctool_job_close(job);
    return dis_vfs_status(status);
}

static ctool_bool dis_token_equal(ctool_string_t token, const char *text) {
    ctool_u32 index;
    for (index = 0u; index < token.size; index++) {
        if (text[index] == '\0' || token.data[index] != text[index]) {
            return CTOOL_FALSE;
        }
    }
    return text[token.size] == '\0' ? CTOOL_TRUE : CTOOL_FALSE;
}

static ctool_string_t dis_next_token(ctool_string_t arguments,
                                      ctool_u32 *cursor) {
    ctool_string_t token;
    ctool_u32 start;
    while (*cursor < arguments.size &&
           (arguments.data[*cursor] == ' ' ||
            arguments.data[*cursor] == '\t')) (*cursor)++;
    start = *cursor;
    while (*cursor < arguments.size && arguments.data[*cursor] != ' ' &&
           arguments.data[*cursor] != '\t') (*cursor)++;
    token.data = arguments.data == (const char *)0
                     ? (const char *)0 : arguments.data + start;
    token.size = *cursor - start;
    return token;
}

static ctool_bool dis_token_u32(ctool_string_t token, ctool_u32 *value_out) {
    ctool_u32 index = 0u;
    ctool_u32 base = 10u;
    ctool_u32 value = 0u;
    if (token.size >= 2u && token.data[0] == '0' &&
        (token.data[1] == 'x' || token.data[1] == 'X')) {
        base = 16u;
        index = 2u;
    }
    if (index == token.size) return CTOOL_FALSE;
    for (; index < token.size; index++) {
        ctool_u32 digit;
        char ch = token.data[index];
        if (ch >= '0' && ch <= '9') digit = (ctool_u32)(ch - '0');
        else if (ch >= 'a' && ch <= 'f') digit = (ctool_u32)(ch - 'a') + 10u;
        else if (ch >= 'A' && ch <= 'F') digit = (ctool_u32)(ch - 'A') + 10u;
        else return CTOOL_FALSE;
        if (digit >= base || value > (4294967295u - digit) / base) {
            return CTOOL_FALSE;
        }
        value = value * base + digit;
    }
    *value_out = value;
    return CTOOL_TRUE;
}

ctool_status_t dis_command_parse(ctool_string_t arguments,
                                 dis_command_t *command_out) {
    dis_command_t command = {0};
    ctool_u32 cursor = 0u;
    ctool_bool have_mode = CTOOL_FALSE;
    ctool_bool have_base = CTOOL_FALSE;
    ctool_bool options = CTOOL_TRUE;
    if (command_out != (dis_command_t *)0) *command_out = command;
    if (command_out == (dis_command_t *)0 ||
        (arguments.data == (const char *)0 && arguments.size != 0u)) {
        return CTOOL_ERR_INVALID_ARGUMENT;
    }
    for (cursor = 0u; cursor < arguments.size; cursor++) {
        if (arguments.data[cursor] == '\0') return CTOOL_ERR_INPUT;
    }
    cursor = 0u;
    while (cursor < arguments.size) {
        ctool_string_t token = dis_next_token(arguments, &cursor);
        if (token.size == 0u) break;
        if (options != CTOOL_FALSE && dis_token_equal(token, "--")) {
            options = CTOOL_FALSE;
        } else if (options != CTOOL_FALSE &&
                   dis_token_equal(token, "--raw")) {
            if (command.raw != CTOOL_FALSE) return CTOOL_ERR_INPUT;
            command.raw = CTOOL_TRUE;
        } else if (options != CTOOL_FALSE &&
                   dis_token_equal(token, "--require-known")) {
            if (command.require_known != CTOOL_FALSE) return CTOOL_ERR_INPUT;
            command.require_known = CTOOL_TRUE;
        } else if (options != CTOOL_FALSE &&
                   dis_token_equal(token, "--mode")) {
            if (have_mode != CTOOL_FALSE) return CTOOL_ERR_INPUT;
            token = dis_next_token(arguments, &cursor);
            if (dis_token_equal(token, "16")) command.mode = CTOOL_X86_MODE_16;
            else if (dis_token_equal(token, "32")) command.mode = CTOOL_X86_MODE_32;
            else return CTOOL_ERR_INPUT;
            have_mode = CTOOL_TRUE;
        } else if (options != CTOOL_FALSE &&
                   dis_token_equal(token, "--base")) {
            if (have_base != CTOOL_FALSE) return CTOOL_ERR_INPUT;
            token = dis_next_token(arguments, &cursor);
            if (!dis_token_u32(token, &command.base_address)) return CTOOL_ERR_INPUT;
            have_base = CTOOL_TRUE;
        } else if (options != CTOOL_FALSE &&
                   dis_token_equal(token, "--range-map")) {
            if (command.range_map.size != 0u) return CTOOL_ERR_INPUT;
            token = dis_next_token(arguments, &cursor);
            if (token.size == 0u || token.data[0] == '-') return CTOOL_ERR_INPUT;
            command.range_map = token;
        } else if ((options != CTOOL_FALSE && token.data[0] == '-') ||
                   command.input.size != 0u) {
            return CTOOL_ERR_INPUT;
        } else {
            command.input = token;
        }
    }
    if (command.input.size == 0u) return CTOOL_ERR_INPUT;
    if (command.raw == CTOOL_FALSE) {
        if (have_mode || have_base || command.range_map.size != 0u ||
            command.require_known) return CTOOL_ERR_INPUT;
    } else if (command.range_map.size != 0u) {
        if (have_mode || have_base) return CTOOL_ERR_INPUT;
        command.mode = CTOOL_DIS_RAW_RANGE_MAP;
    } else if (!have_mode || !have_base) {
        return CTOOL_ERR_INPUT;
    }
    *command_out = command;
    return CTOOL_OK;
}
