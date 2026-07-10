/**
 * dis.c - CupidDis kernel adapters
 */

#include "dis.h"
#include "ctool_kernel.h"
#include "cupiddis.h"
#include "kernel.h"
#include "vfs.h"

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

void dis_disassemble(const uint8_t *buffer, uint32_t size,
                     uint32_t base_address, const dis_sym_t *symbols,
                     int symbol_count, dis_output_fn output) {
    dis_sink_context_t sink_context;
    ctool_job_config_t config;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_dis_label_t *labels = (ctool_dis_label_t *)0;
    ctool_source_t source;
    ctool_dis_request_t request;
    ctool_dis_report_t report;
    ctool_status_t status;
    ctool_u32 count = 0u;
    ctool_u32 index;

    if (buffer == (const uint8_t *)0 || size == 0u) {
        dis_out(output, "dis: empty code buffer\n");
        return;
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
        request.raw_mode = CTOOL_X86_MODE_32;
        request.raw_base_address = base_address;
        request.labels = labels;
        request.label_count = count;
        status = ctool_dis_inspect(job, &source, &request, &report);
    }
    if (status == CTOOL_OK) {
        status = ctool_dis_render(job, &report, CTOOL_DIS_TEXT_CUPID,
                                  dis_text_sink(&sink_context));
    }
    if (status != CTOOL_OK) {
        dis_report_failure(job, output, status,
                           "dis: disassembly failed\n");
    }
    if (job != (ctool_job_t *)0) {
        ctool_job_close(job);
    }
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
        request.raw_mode = CTOOL_X86_MODE_32;
        request.raw_base_address = 0u;
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
