#include "ctool_host.h"
#include "cupidc_engine.h"
#include "elf32.h"

#include <stdio.h>
#include <string.h>

#define ENGINE_START 0x01100000u
#define ENGINE_END 0x01a00000u
#define ELF_PT_GNU_STACK 0x6474e551u
#define I386_SYS_MMAP 90
#define I386_PROT_RWX 7u
#define I386_MAP_PRIVATE_ANONYMOUS_NOREPLACE 0x100022u

typedef ctool_status_t (*engine_entry_t)(const ctool_c_engine_request_t *,
                                       ctool_invocation_result_t *);

_Static_assert(sizeof(ctool_c_engine_request_t) == 156u, "unexpected i386 engine request size");

int cupid_linux_syscall1(int number, unsigned int argument);

static engine_entry_t load_engine(ctool_job_t *job, const char *path) {
    ctool_path_t module_path = {ctool_string(path)};
    ctool_source_t source;
    ctool_elf32_object_t object;
    if (ctool_job_load_source(job, &module_path, &source) != CTOOL_OK ||
        ctool_elf32_read(job, &source, &object) != CTOOL_OK ||
        object.file_type != CTOOL_ELF32_ET_EXEC)
        return (engine_entry_t)0;

    for (ctool_u32 i = 1; i < object.symbol_count; i++) {
        if (object.symbols[i].placement == CTOOL_ELF32_SYMBOL_UNDEFINED)
            return (engine_entry_t)0;
    }

    ctool_bool executable_entry = CTOOL_FALSE;
    ctool_u32 end = ENGINE_START;
    for (ctool_u32 i = 0; i < object.program_header_count; i++) {
        const ctool_elf32_program_header_t *segment = &object.program_headers[i];
        ctool_u32 start = segment->virtual_address;
        if (segment->type == ELF_PT_GNU_STACK &&
            segment->file_size == 0 && segment->memory_size == 0)
            continue;
        if (segment->type != CTOOL_ELF32_PT_LOAD || start < end || start >= ENGINE_END ||
            segment->memory_size == 0 || segment->memory_size > ENGINE_END - start ||
            (segment->flags & ~(CTOOL_ELF32_PF_R | CTOOL_ELF32_PF_W | CTOOL_ELF32_PF_X)))
            return (engine_entry_t)0;
        if ((segment->flags & CTOOL_ELF32_PF_X) && object.entry_point >= start &&
            object.entry_point - start < segment->file_size)
            executable_entry = CTOOL_TRUE;
        end = start + segment->memory_size;
    }
    if (!executable_entry)
        return (engine_entry_t)0;

    /* The Linux test runs in a fresh address space for each invocation. */
    ctool_u32 mapping[] = {ENGINE_START, end - ENGINE_START, I386_PROT_RWX,
                           I386_MAP_PRIVATE_ANONYMOUS_NOREPLACE, 0xffffffffu, 0u};
    if ((ctool_u32)cupid_linux_syscall1(I386_SYS_MMAP, (ctool_u32)mapping) != ENGINE_START)
        return (engine_entry_t)0;

    for (ctool_u32 i = 0; i < object.program_header_count; i++) {
        const ctool_elf32_program_header_t *segment = &object.program_headers[i];
        if (segment->type == CTOOL_ELF32_PT_LOAD && segment->file_size)
            memcpy((void *)segment->virtual_address, segment->contents.data, segment->file_size);
    }
    return (engine_entry_t)object.entry_point;
}

typedef struct {
    ctool_allocator_t backing;
    ctool_u32 live;
    ctool_u32 calls;
} tracker_t;

static void *tracked_allocate(void *context, ctool_u32 bytes) {
    tracker_t *tracker = context;
    void *memory = tracker->backing.allocate(tracker->backing.context, bytes);
    tracker->calls++;
    if (memory)
        tracker->live += bytes;
    return memory;
}

static void tracked_release(void *context, void *memory, ctool_u32 bytes) {
    tracker_t *tracker = context;
    tracker->live -= bytes;
    tracker->backing.release(tracker->backing.context, memory, bytes);
}

static int reject_abi(engine_entry_t entry, const ctool_c_engine_request_t *request,
                      const tracker_t *tracker) {
    ctool_invocation_result_t result;
    unsigned char sentinel[sizeof(result)];
    memset(sentinel, 0xa5, sizeof(sentinel));
    memcpy(&result, sentinel, sizeof(result));
    return entry(request, &result) == CTOOL_ERR_INVALID_ARGUMENT &&
           !memcmp(&result, sentinel, sizeof(result)) && tracker->calls == 0;
}

static int verify_requests(engine_entry_t entry, const ctool_c_engine_request_t *request,
                           const tracker_t *tracker, ctool_string_t rejected_source,
                           ctool_string_t protected_output) {
    ctool_c_engine_request_t bad = *request;
    bad.invocation.output_path = protected_output;
    ctool_c_engine_request_t cases[] = {bad, bad, bad, bad};
    cases[0].abi++;
    cases[1].bytes--;
    cases[2].parser.mode = CTOOL_C_PP_MODE_CUPID;
    cases[3].parser.gnu_extensions = CTOOL_FALSE;

    if (!reject_abi(entry, (const ctool_c_engine_request_t *)0, tracker) ||
        entry(&bad, (ctool_invocation_result_t *)0) != CTOOL_ERR_INVALID_ARGUMENT)
        return 0;
    for (ctool_u32 i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if (!reject_abi(entry, &cases[i], tracker))
            return 0;
    }

    ctool_invocation_result_t result = {0};
    bad.invocation.input_path = rejected_source;
    if (entry(&bad, &result) != CTOOL_ERR_INPUT || !result.diagnostic_count ||
        result.output_committed || tracker->live)
        return 0;

    bad.invocation.input_path = request->invocation.input_path;
    bad.config.limits.source_bytes = 1;
    return entry(&bad, &result) == CTOOL_ERR_LIMIT && !result.output_committed && !tracker->live;
}

int main(int argc, char **argv) {
    ctool_host_adapter_t host;
    ctool_job_t *job;
    if (argc != 7 || ctool_host_adapter_init(&host, argv[1]) != CTOOL_OK)
        return 2;

    ctool_c_engine_request_t request = {0};
    request.config = ctool_host_job_config(&host, ctool_default_limits());
    if (ctool_job_open(&request.config, &job) != CTOOL_OK)
        return 3;
    engine_entry_t entry = load_engine(job, argv[2]);
    ctool_job_close(job);
    if (!entry) {
        fprintf(stderr, "compiler engine ELF rejected\n");
        return 4;
    }

    request.config.limits.arena_bytes = 192u * 1024u * 1024u;
    tracker_t tracker = {request.config.allocator, 0, 0};
    request.config.allocator = (ctool_allocator_t){&tracker, tracked_allocate, tracked_release};
    request.abi = CTOOL_C_ENGINE_ABI;
    request.bytes = sizeof(request);
    request.invocation.input_path = ctool_string(argv[3]);
    request.invocation.output_path = ctool_string(argv[4]);
    ctool_c_pp_include_root_t includes[] = {
        {{ctool_string("/toolchain")}, CTOOL_C_PP_INCLUDE_QUOTED | CTOOL_C_PP_INCLUDE_ANGLE},
        {{ctool_string("/toolchain/hosted/i386-linux/include")}, CTOOL_C_PP_INCLUDE_ANGLE}
    };
    request.preprocessor.mode = CTOOL_C_PP_MODE_C11;
    request.preprocessor.gnu_extensions = CTOOL_TRUE;
    request.preprocessor.include_roots = includes;
    request.preprocessor.include_root_count = sizeof(includes) / sizeof(includes[0]);
    request.parser.mode = request.preprocessor.mode;
    request.parser.gnu_extensions = request.preprocessor.gnu_extensions;
    if (!verify_requests(entry, &request, &tracker, ctool_string(argv[5]), ctool_string(argv[6]))) {
        fprintf(stderr, "compiler engine accepted an invalid request\n");
        return 5;
    }

    ctool_invocation_result_t result = {0};
    ctool_status_t status = entry(&request, &result);
    if (status != CTOOL_OK || !result.output_committed || result.diagnostic_count ||
        tracker.live || !result.output_bytes) {
        fprintf(stderr, "compiler engine failed: %s, live=%u\n", ctool_status_name(status),
                tracker.live);
        return 6;
    }
    printf("compiler engine emitted %u bytes\n", result.output_bytes);
    return 0;
}
