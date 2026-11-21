/**
 * @file cupidasm_source_contract.inc
 * @brief Check assembly source directives, layout and diagnostics.
 */

static void init_raw_request(ctool_asm_request_t *request) {
    (void)memset(request, 0, sizeof(*request));
    request->artifact = CTOOL_ASM_ARTIFACT_RAW;
    request->initial_mode = CTOOL_X86_MODE_32;
}

static void init_object_request(ctool_asm_request_t *request) {
    (void)memset(request, 0, sizeof(*request));
    request->artifact = CTOOL_ASM_ARTIFACT_ELF32_REL;
    request->initial_mode = CTOOL_X86_MODE_32;
}

static void init_fixed_request(ctool_asm_request_t *request, const ctool_string_t *entry) {
    (void)memset(request, 0, sizeof(*request));
    request->artifact = CTOOL_ASM_ARTIFACT_FIXED_IMAGE;
    request->initial_mode = CTOOL_X86_MODE_32;
    request->entry_candidates = entry;
    request->entry_candidate_count = 1u;
    request->as.fixed.code.base_address = 0x01a00000u;
    request->as.fixed.code.maximum_bytes = 0x1000u;
    request->as.fixed.data.base_address = 0x01b00000u;
    request->as.fixed.data.maximum_bytes = 0x1000u;
}

static int run_raw_source_contracts(void) {
    static const char duplicate_origin_source[] = "BITS 16\n"
                                                  "ORG 0x7c00\n"
                                                  "db 0x11\n"
                                                  "ORG 0x8000\n"
                                                  "db 0x22\n";
    static const char equ_preamble_source[] = "BITS 32\n"
                                              "VALUE equ 1\n"
                                              "section .data\n"
                                              "db VALUE\n";
    static const char one_section_source[] = "BITS 16\n"
                                             "section .payload\n"
                                             "ORG 0x100\n"
                                             "db 0x11\n"
                                             "section .payload\n"
                                             "ret\n";
    static const char control_edge_source[] = "BITS 16\n"
                                              "ORG 0x8000\n"
                                              "start: jmp next\n"
                                              "next: jmp dword 0x08:pm32\n"
                                              "BITS 32\n"
                                              "pm32: call eax\n"
                                              "jmp 0x08:0x00100000\n";
    static const char multi_section_source[] = "BITS 32\n"
                                               "section .text\n"
                                               "start: ret\n"
                                               "section .data\n"
                                               "db 0x2a\n";
    static const char early_multi_section_source[] = "BITS 32\n"
                                                     "section .first\n"
                                                     "section .second\n"
                                                     "db 0x2a\n";
    static const char label_preamble_source[] = "BITS 32\n"
                                                "implicit_text:\n"
                                                "section .data\n"
                                                "db 0x2a\n";
    static const ctool_u8 expected[] = {0x11u, 0xc3u};
    ctool_host_adapter_t adapter;
    ctool_job_config_t config;
    ctool_job_t *job;
    ctool_buffer_t *output;
    ctool_source_t source;
    ctool_asm_request_t request;
    ctool_asm_result_t result;
    ctool_bytes_t bytes;
    ctool_status_t status;

    status = ctool_host_adapter_init(&adapter, ".");
    if (!check_status(status, CTOOL_OK, "raw source contract host adapter init")) {
        return 1;
    }
    config = ctool_host_job_config(&adapter, ctool_default_limits());
    init_raw_request(&request);
    status = ctool_job_open(&config, &job);
    if (!check_status(status, CTOOL_OK, "raw source contract job open")) {
        return 1;
    }
    status = ctool_job_open_buffer(job, 16u, config.limits.output_bytes, &output);
    if (!check_status(status, CTOOL_OK, "raw source contract output open")) {
        ctool_job_close(job);
        return 1;
    }

    source.path.text = ctool_string("/duplicate-org.asm");
    source.contents =
        ctool_bytes(duplicate_origin_source, (ctool_u32)(sizeof(duplicate_origin_source) - 1u));
    (void)memset(&result, 0xa5, sizeof(result));
    status = ctool_asm_assemble(job, &source, &request, output, &result);
    if (!check_status(status, CTOOL_ERR_INPUT, "duplicate raw origin") ||
        ctool_buffer_view(output).size != 0u || !contract_result_is_zero(&result) ||
        ctool_job_diagnostic_count(job) != 1u ||
        !contract_has_source_diagnostic(job, CTOOL_ASM_DIAG_INVALID_ORIGIN, "/duplicate-org.asm",
                                        4u, 1u, "raw output accepts only one ORG directive")) {
        (void)fprintf(stderr, "duplicate raw origin contract differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_buffer_close(output);
        ctool_job_close(job);
        return 1;
    }

    status = ctool_buffer_rewind(output, 0u);
    source.path.text = ctool_string("/raw-equ-preamble.asm");
    source.contents =
        ctool_bytes(equ_preamble_source, (ctool_u32)(sizeof(equ_preamble_source) - 1u));
    (void)memset(&result, 0xa5, sizeof(result));
    if (status == CTOOL_OK) {
        status = ctool_asm_assemble(job, &source, &request, output, &result);
    }
    bytes = ctool_buffer_view(output);
    if (!check_status(status, CTOOL_OK, "raw EQU preamble") || bytes.size != 1u ||
        bytes.data[0] != 1u || result.artifact != CTOOL_ASM_ARTIFACT_RAW ||
        result.bytes.data != bytes.data || result.bytes.size != bytes.size ||
        result.raw_origin != 0u || result.raw_range_count != 1u ||
        result.raw_ranges[0].offset != 0u ||
        result.raw_ranges[0].kind != CTOOL_ASM_RAW_RANGE_DATA ||
        ctool_job_diagnostic_count(job) != 1u) {
        (void)fprintf(stderr, "raw EQU preamble contract differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_buffer_close(output);
        ctool_job_close(job);
        return 1;
    }

    status = ctool_buffer_rewind(output, 0u);
    source.path.text = ctool_string("/raw-one-section.asm");
    source.contents = ctool_bytes(one_section_source, (ctool_u32)(sizeof(one_section_source) - 1u));
    (void)memset(&result, 0xa5, sizeof(result));
    status = ctool_asm_assemble(job, &source, &request, output, &result);
    bytes = ctool_buffer_view(output);
    if (!check_status(status, CTOOL_OK, "raw source recovery") ||
        bytes.size != (ctool_u32)sizeof(expected) ||
        memcmp(bytes.data, expected, sizeof(expected)) != 0 ||
        result.artifact != CTOOL_ASM_ARTIFACT_RAW || result.bytes.data != bytes.data ||
        result.bytes.size != bytes.size || result.raw_origin != 0x100u ||
        result.raw_range_count != 2u || result.raw_ranges[0].offset != 0u ||
        result.raw_ranges[0].kind != CTOOL_ASM_RAW_RANGE_DATA ||
        result.raw_ranges[1].offset != 1u ||
        result.raw_ranges[1].kind != CTOOL_ASM_RAW_RANGE_CODE16 ||
        ctool_job_diagnostic_count(job) != 1u) {
        (void)fprintf(stderr, "raw source recovery artifact differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_buffer_close(output);
        ctool_job_close(job);
        return 1;
    }

    status = ctool_buffer_rewind(output, 0u);
    if (status == CTOOL_OK) {
        (void)memset(&result, 0xa5, sizeof(result));
        status = ctool_asm_assemble(job, &source, &request, output, &result);
    }
    bytes = ctool_buffer_view(output);
    if (!check_status(status, CTOOL_OK, "raw source deterministic repeat") ||
        bytes.size != (ctool_u32)sizeof(expected) ||
        memcmp(bytes.data, expected, sizeof(expected)) != 0 || result.raw_origin != 0x100u ||
        ctool_job_diagnostic_count(job) != 1u) {
        (void)fprintf(stderr, "raw source deterministic repeat differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_buffer_close(output);
        ctool_job_close(job);
        return 1;
    }

    status = ctool_buffer_rewind(output, 0u);
    source.path.text = ctool_string("/raw-control-edges.asm");
    source.contents =
        ctool_bytes(control_edge_source, (ctool_u32)(sizeof(control_edge_source) - 1u));
    (void)memset(&result, 0xa5, sizeof(result));
    if (status == CTOOL_OK) {
        status = ctool_asm_assemble(job, &source, &request, output, &result);
    }
    bytes = ctool_buffer_view(output);
    if (!check_status(status, CTOOL_OK, "raw control-edge metadata") || bytes.size != 19u ||
        result.raw_origin != 0x8000u || result.raw_range_count != 2u ||
        result.raw_edge_count != 4u || result.raw_edges[0].source_offset != 0u ||
        result.raw_edges[0].kind != CTOOL_ASM_RAW_EDGE_RELATIVE ||
        result.raw_edges[0].class_id != CTOOL_ASM_RAW_EDGE_LOCAL ||
        result.raw_edges[0].target_offset != 2u || result.raw_edges[0].target_address != 0x8002u ||
        result.raw_edges[0].target_mode != CTOOL_X86_MODE_16 ||
        result.raw_edges[0].target_segment != 0u || result.raw_edges[1].source_offset != 2u ||
        result.raw_edges[1].kind != CTOOL_ASM_RAW_EDGE_FAR ||
        result.raw_edges[1].class_id != CTOOL_ASM_RAW_EDGE_LOCAL ||
        result.raw_edges[1].target_offset != 10u || result.raw_edges[1].target_address != 0x800au ||
        result.raw_edges[1].target_mode != CTOOL_X86_MODE_32 ||
        result.raw_edges[1].target_segment != 8u || result.raw_edges[2].source_offset != 10u ||
        result.raw_edges[2].kind != CTOOL_ASM_RAW_EDGE_INDIRECT ||
        result.raw_edges[2].class_id != CTOOL_ASM_RAW_EDGE_UNPROVABLE ||
        result.raw_edges[2].target_offset != CTOOL_ASM_RAW_EDGE_NO_TARGET ||
        result.raw_edges[2].target_address != CTOOL_ASM_RAW_EDGE_NO_TARGET ||
        result.raw_edges[2].target_mode != 0 ||
        result.raw_edges[2].target_segment != CTOOL_ASM_RAW_EDGE_NO_TARGET ||
        result.raw_edges[3].source_offset != 12u ||
        result.raw_edges[3].kind != CTOOL_ASM_RAW_EDGE_FAR ||
        result.raw_edges[3].class_id != CTOOL_ASM_RAW_EDGE_EXTERNAL ||
        result.raw_edges[3].target_offset != CTOOL_ASM_RAW_EDGE_NO_TARGET ||
        result.raw_edges[3].target_address != 0x00100000u ||
        result.raw_edges[3].target_mode != CTOOL_X86_MODE_32 ||
        result.raw_edges[3].target_segment != 8u || ctool_job_diagnostic_count(job) != 1u) {
        (void)fprintf(stderr, "raw control-edge metadata differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_buffer_close(output);
        ctool_job_close(job);
        return 1;
    }

    status = ctool_buffer_rewind(output, 0u);
    source.path.text = ctool_string("/raw-multi-section.asm");
    source.contents =
        ctool_bytes(multi_section_source, (ctool_u32)(sizeof(multi_section_source) - 1u));
    (void)memset(&result, 0xa5, sizeof(result));
    if (status == CTOOL_OK) {
        status = ctool_asm_assemble(job, &source, &request, output, &result);
    }
    bytes = ctool_buffer_view(output);
    if (!check_status(status, CTOOL_ERR_INPUT, "raw multi-section layout") || bytes.size != 0u ||
        !contract_result_is_zero(&result) || ctool_job_diagnostic_count(job) != 2u ||
        !contract_has_source_diagnostic(job, CTOOL_ASM_DIAG_INVALID_SECTION,
                                        "/raw-multi-section.asm", 4u, 1u,
                                        "raw output supports only one source section")) {
        (void)fprintf(stderr, "raw multi-section contract differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_buffer_close(output);
        ctool_job_close(job);
        return 1;
    }

    source.path.text = ctool_string("/raw-one-section.asm");
    source.contents = ctool_bytes(one_section_source, (ctool_u32)(sizeof(one_section_source) - 1u));
    (void)memset(&result, 0xa5, sizeof(result));
    status = ctool_asm_assemble(job, &source, &request, output, &result);
    bytes = ctool_buffer_view(output);
    if (!check_status(status, CTOOL_OK, "raw multi-section recovery") ||
        bytes.size != (ctool_u32)sizeof(expected) ||
        memcmp(bytes.data, expected, sizeof(expected)) != 0 || result.raw_origin != 0x100u ||
        ctool_job_diagnostic_count(job) != 2u) {
        (void)fprintf(stderr, "raw multi-section recovery differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_buffer_close(output);
        ctool_job_close(job);
        return 1;
    }

    status = ctool_buffer_rewind(output, 0u);
    source.path.text = ctool_string("/raw-early-multi-section.asm");
    source.contents = ctool_bytes(early_multi_section_source,
                                  (ctool_u32)(sizeof(early_multi_section_source) - 1u));
    (void)memset(&result, 0xa5, sizeof(result));
    if (status == CTOOL_OK) {
        status = ctool_asm_assemble(job, &source, &request, output, &result);
    }
    if (!check_status(status, CTOOL_ERR_INPUT, "raw early multi-section layout") ||
        ctool_buffer_view(output).size != 0u || !contract_result_is_zero(&result) ||
        ctool_job_diagnostic_count(job) != 3u ||
        !contract_has_source_diagnostic(job, CTOOL_ASM_DIAG_INVALID_SECTION,
                                        "/raw-early-multi-section.asm", 3u, 1u,
                                        "raw output supports only one source section")) {
        (void)fprintf(stderr, "raw early multi-section contract differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_buffer_close(output);
        ctool_job_close(job);
        return 1;
    }

    status = ctool_buffer_rewind(output, 0u);
    source.path.text = ctool_string("/raw-label-preamble.asm");
    source.contents =
        ctool_bytes(label_preamble_source, (ctool_u32)(sizeof(label_preamble_source) - 1u));
    (void)memset(&result, 0xa5, sizeof(result));
    if (status == CTOOL_OK) {
        status = ctool_asm_assemble(job, &source, &request, output, &result);
    }
    if (!check_status(status, CTOOL_ERR_INPUT, "raw label preamble section claim") ||
        ctool_buffer_view(output).size != 0u || !contract_result_is_zero(&result) ||
        ctool_job_diagnostic_count(job) != 4u ||
        !contract_has_source_diagnostic(job, CTOOL_ASM_DIAG_INVALID_SECTION,
                                        "/raw-label-preamble.asm", 3u, 1u,
                                        "raw output supports only one source section")) {
        (void)fprintf(stderr, "raw label preamble contract differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_buffer_close(output);
        ctool_job_close(job);
        return 1;
    }

    ctool_buffer_close(output);
    ctool_job_close(job);
    (void)puts("raw-source-contracts: ok");
    return 0;
}

static int run_alignment(void) {
    static const char invalid_fill_source_text[] = "BITS 32\n"
                                                   "align 4, 256\n";
    static const char raw_source_text[] = "BITS 32\n"
                                          "ORG 0x101\n"
                                          "db 0x11\n"
                                          "align 4, 0xa5\n"
                                          "aligned_raw:\n"
                                          "dw aligned_raw\n"
                                          "align 8\n"
                                          "db 0x22\n";
    static const ctool_u8 expected_raw[] = {0x11u, 0xa5u, 0xa5u, 0x04u, 0x01u, 0x00u, 0x00u, 0x22u};
    static const char object_source_text[] = "BITS 32\n"
                                             "section .data\n"
                                             "db 1\n"
                                             "align 16, 0x5a\n"
                                             "aligned_data:\n"
                                             "dd aligned_data\n"
                                             "section .bss\n"
                                             "resb 3\n"
                                             "align 32\n"
                                             "aligned_bss:\n"
                                             "resb 5\n";
    static const char fixed_source_text[] = "BITS 32\n"
                                            "section .text\n"
                                            "main: ret\n"
                                            "section .data\n"
                                            "db 0x11\n"
                                            "align 16, 0xcc\n"
                                            "aligned_fixed:\n"
                                            "dd aligned_fixed\n"
                                            "section .bss\n"
                                            "resb 3\n"
                                            "align 32\n"
                                            "fixed_bss:\n"
                                            "resb 5\n";
    static const char fixed_empty_source_text[] = "BITS 32\n"
                                                  "section .text\n"
                                                  "align 32\n"
                                                  "main:\n"
                                                  "section .data\n"
                                                  "dd main\n";
    ctool_host_adapter_t adapter;
    ctool_job_config_t config;
    ctool_job_t *job;
    ctool_buffer_t *output;
    ctool_source_t source;
    ctool_source_t object_source;
    ctool_asm_request_t raw;
    ctool_asm_request_t object_request;
    ctool_asm_request_t fixed;
    ctool_asm_request_t fixed_overflow;
    ctool_asm_request_t fixed_empty;
    ctool_asm_request_t fixed_empty_overflow;
    ctool_asm_result_t result;
    ctool_elf32_object_t object;
    const ctool_elf32_section_t *data;
    const ctool_elf32_section_t *bss;
    const ctool_elf32_symbol_t *aligned_data;
    const ctool_elf32_symbol_t *aligned_bss;
    const ctool_asm_region_t *text_region;
    const ctool_asm_region_t *data_region;
    const ctool_diagnostic_t *diagnostic;
    ctool_string_t entry = ctool_string("main");
    ctool_bytes_t bytes;
    ctool_status_t status;
    ctool_u32 index;

    status = ctool_host_adapter_init(&adapter, ".");
    if (!check_status(status, CTOOL_OK, "alignment host adapter init")) {
        return 1;
    }
    config = ctool_host_job_config(&adapter, ctool_default_limits());
    init_raw_request(&raw);
    init_object_request(&object_request);
    init_fixed_request(&fixed, &entry);
    fixed_overflow = fixed;
    fixed_overflow.as.fixed.data.base_address = 0xfffffff1u;
    fixed_empty = fixed;
    fixed_empty.as.fixed.code.base_address = 0x01a00003u;
    fixed_empty_overflow = fixed;
    fixed_empty_overflow.as.fixed.code.base_address = 0xfffffff1u;

    if (!expect_assembly_failure("zero alignment", config, "/align-zero.asm", "BITS 32\nalign 0\n",
                                 &raw, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_LAYOUT, "/align-zero.asm") ||
        !expect_assembly_failure("non-power-of-two alignment", config, "/align-three.asm",
                                 "BITS 32\nalign 3\n", &raw, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_LAYOUT,
                                 "/align-three.asm") ||
        !expect_assembly_failure("initialized BSS alignment", config, "/align-bss-fill.asm",
                                 "BITS 32\nsection .bss\nresb 1\nalign 8, 1\n", &object_request,
                                 CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_INVALID_SECTION,
                                 "/align-bss-fill.asm") ||
        !expect_assembly_failure("fixed absolute alignment overflow", config,
                                 "/align-fixed-overflow.asm",
                                 "BITS 32\nsection .text\nmain: ret\nsection .data\n"
                                 "align 16\ndb 1\n",
                                 &fixed_overflow, CTOOL_ERR_OVERFLOW, CTOOL_ASM_DIAG_LAYOUT,
                                 "/align-fixed-overflow.asm") ||
        !expect_assembly_failure(
            "fixed empty-section alignment overflow", config, "/align-fixed-empty-overflow.asm",
            "BITS 32\nsection .text\nalign 16\nmain:\n", &fixed_empty_overflow, CTOOL_ERR_OVERFLOW,
            CTOOL_ASM_DIAG_LAYOUT, "/align-fixed-empty-overflow.asm")) {
        return 1;
    }

    status = ctool_job_open(&config, &job);
    if (!check_status(status, CTOOL_OK, "alignment raw job open")) {
        return 1;
    }
    status = ctool_job_open_buffer(job, 16u, config.limits.output_bytes, &output);
    if (!check_status(status, CTOOL_OK, "alignment raw output open")) {
        ctool_job_close(job);
        return 1;
    }
    source.path.text = ctool_string("/align-fill.asm");
    source.contents =
        ctool_bytes(invalid_fill_source_text, (ctool_u32)(sizeof(invalid_fill_source_text) - 1u));
    (void)memset(&result, 0xa5, sizeof(result));
    status = ctool_asm_assemble(job, &source, &raw, output, &result);
    diagnostic = ctool_job_diagnostic(job, 0u);
    if (!check_status(status, CTOOL_ERR_INPUT, "alignment fill overflow") ||
        ctool_buffer_view(output).size != 0u || !contract_result_is_zero(&result) ||
        ctool_job_diagnostic_count(job) != 1u || diagnostic == (const ctool_diagnostic_t *)0 ||
        diagnostic->code != CTOOL_ASM_DIAG_LAYOUT) {
        (void)fprintf(stderr, "alignment fill diagnostic differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_buffer_close(output);
        ctool_job_close(job);
        return 1;
    }

    source.path.text = ctool_string("/alignment-raw.asm");
    source.contents = ctool_bytes(raw_source_text, (ctool_u32)(sizeof(raw_source_text) - 1u));
    (void)memset(&result, 0xa5, sizeof(result));
    status = ctool_asm_assemble(job, &source, &raw, output, &result);
    bytes = ctool_buffer_view(output);
    if (!check_status(status, CTOOL_OK, "raw alignment recovery") ||
        bytes.size != (ctool_u32)sizeof(expected_raw) ||
        memcmp(bytes.data, expected_raw, sizeof(expected_raw)) != 0 ||
        result.artifact != CTOOL_ASM_ARTIFACT_RAW || result.bytes.data != bytes.data ||
        result.bytes.size != bytes.size || ctool_job_diagnostic_count(job) != 1u) {
        (void)fprintf(stderr, "raw alignment recovery artifact differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_buffer_close(output);
        ctool_job_close(job);
        return 1;
    }
    status = ctool_buffer_rewind(output, 0u);
    if (status == CTOOL_OK) {
        (void)memset(&result, 0xa5, sizeof(result));
        status = ctool_asm_assemble(job, &source, &raw, output, &result);
    }
    bytes = ctool_buffer_view(output);
    if (!check_status(status, CTOOL_OK, "raw alignment deterministic repeat") ||
        bytes.size != (ctool_u32)sizeof(expected_raw) ||
        memcmp(bytes.data, expected_raw, sizeof(expected_raw)) != 0 ||
        ctool_job_diagnostic_count(job) != 1u) {
        (void)fprintf(stderr, "raw alignment repeat differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_buffer_close(output);
        ctool_job_close(job);
        return 1;
    }
    ctool_buffer_close(output);
    ctool_job_close(job);

    status = ctool_job_open(&config, &job);
    if (!check_status(status, CTOOL_OK, "alignment object job open")) {
        return 1;
    }
    status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes, &output);
    if (!check_status(status, CTOOL_OK, "alignment object output open")) {
        ctool_job_close(job);
        return 1;
    }
    source.path.text = ctool_string("/alignment-object.asm");
    source.contents = ctool_bytes(object_source_text, (ctool_u32)(sizeof(object_source_text) - 1u));
    (void)memset(&result, 0xa5, sizeof(result));
    (void)memset(&object, 0, sizeof(object));
    status = ctool_asm_assemble(job, &source, &object_request, output, &result);
    if (status == CTOOL_OK) {
        object_source.path.text = ctool_string("/alignment-object.o");
        object_source.contents = result.bytes;
        status = ctool_elf32_read(job, &object_source, &object);
    }
    data = find_section(&object, ".data");
    bss = find_section(&object, ".bss");
    aligned_data = find_symbol(&object, "aligned_data");
    aligned_bss = find_symbol(&object, "aligned_bss");
    if (!check_status(status, CTOOL_OK, "ELF32 alignment assembly") ||
        data == (const ctool_elf32_section_t *)0 || data->alignment != 16u || data->size != 20u ||
        data->contents.size != 20u || data->contents.data[0] != 1u ||
        bss == (const ctool_elf32_section_t *)0 || bss->type != CTOOL_ELF32_SHT_NOBITS ||
        bss->alignment != 32u || bss->size != 37u || bss->contents.size != 0u ||
        aligned_data == (const ctool_elf32_symbol_t *)0 ||
        aligned_data->section_file_index != data->file_index || aligned_data->value != 16u ||
        aligned_bss == (const ctool_elf32_symbol_t *)0 ||
        aligned_bss->section_file_index != bss->file_index || aligned_bss->value != 32u ||
        object.relocation_count != 1u ||
        object.relocations[0].target_section_file_index != data->file_index ||
        object.relocations[0].offset != 16u ||
        object.relocations[0].symbol_file_index != aligned_data->file_index ||
        object.relocations[0].type != CTOOL_ELF32_R_386_32 ||
        object.relocations[0].addend_known == CTOOL_FALSE || object.relocations[0].addend != 0 ||
        ctool_job_diagnostic_count(job) != 0u) {
        (void)fprintf(stderr, "ELF32 alignment artifact differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_buffer_close(output);
        ctool_job_close(job);
        return 1;
    }
    for (index = 1u; index < 16u; index++) {
        if (data->contents.data[index] != 0x5au) {
            (void)fprintf(stderr, "ELF32 alignment fill differs\n");
            ctool_buffer_close(output);
            ctool_job_close(job);
            return 1;
        }
    }
    for (index = 16u; index < 20u; index++) {
        if (data->contents.data[index] != 0u) {
            (void)fprintf(stderr, "ELF32 alignment relocation field differs\n");
            ctool_buffer_close(output);
            ctool_job_close(job);
            return 1;
        }
    }
    ctool_buffer_close(output);
    ctool_job_close(job);

    fixed.as.fixed.data.base_address = 0x01b00003u;
    status = ctool_job_open(&config, &job);
    if (!check_status(status, CTOOL_OK, "alignment fixed job open")) {
        return 1;
    }
    status = ctool_job_open_buffer(job, 64u, config.limits.output_bytes, &output);
    if (!check_status(status, CTOOL_OK, "alignment fixed output open")) {
        ctool_job_close(job);
        return 1;
    }
    source.path.text = ctool_string("/alignment-fixed.asm");
    source.contents = ctool_bytes(fixed_source_text, (ctool_u32)(sizeof(fixed_source_text) - 1u));
    (void)memset(&result, 0xa5, sizeof(result));
    status = ctool_asm_assemble(job, &source, &fixed, output, &result);
    bytes = ctool_buffer_view(output);
    text_region = result.region_count >= 1u ? &result.regions[0] : (const ctool_asm_region_t *)0;
    data_region = result.region_count >= 2u ? &result.regions[1] : (const ctool_asm_region_t *)0;
    if (!check_status(status, CTOOL_OK, "fixed alignment assembly") || bytes.size != 34u ||
        bytes.data[0] != 0xc3u || text_region == (const ctool_asm_region_t *)0 ||
        text_region->address != 0x01a00000u || text_region->file_size != 1u ||
        text_region->memory_size != 1u || data_region == (const ctool_asm_region_t *)0 ||
        data_region->address != 0x01b00003u || data_region->output_offset != 1u ||
        data_region->file_size != 33u || data_region->memory_size != 98u ||
        result.has_entry != CTOOL_TRUE || result.entry_address != 0x01a00000u ||
        ctool_job_diagnostic_count(job) != 0u) {
        (void)fprintf(stderr, "fixed alignment artifact differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_buffer_close(output);
        ctool_job_close(job);
        return 1;
    }
    for (index = 1u; index < 14u; index++) {
        if (bytes.data[index] != 0u) {
            (void)fprintf(stderr, "fixed leading alignment differs\n");
            ctool_buffer_close(output);
            ctool_job_close(job);
            return 1;
        }
    }
    if (bytes.data[14] != 0x11u) {
        (void)fprintf(stderr, "fixed aligned section payload differs\n");
        ctool_buffer_close(output);
        ctool_job_close(job);
        return 1;
    }
    for (index = 15u; index < 30u; index++) {
        if (bytes.data[index] != 0xccu) {
            (void)fprintf(stderr, "fixed alignment fill differs\n");
            ctool_buffer_close(output);
            ctool_job_close(job);
            return 1;
        }
    }
    if (bytes.data[30] != 0x20u || bytes.data[31] != 0x00u || bytes.data[32] != 0xb0u ||
        bytes.data[33] != 0x01u) {
        (void)fprintf(stderr, "fixed aligned label address differs\n");
        ctool_buffer_close(output);
        ctool_job_close(job);
        return 1;
    }
    ctool_buffer_close(output);
    ctool_job_close(job);

    status = ctool_job_open(&config, &job);
    if (!check_status(status, CTOOL_OK, "empty alignment fixed job open")) {
        return 1;
    }
    status = ctool_job_open_buffer(job, 16u, config.limits.output_bytes, &output);
    if (!check_status(status, CTOOL_OK, "empty alignment fixed output open")) {
        ctool_job_close(job);
        return 1;
    }
    source.path.text = ctool_string("/alignment-fixed-empty.asm");
    source.contents =
        ctool_bytes(fixed_empty_source_text, (ctool_u32)(sizeof(fixed_empty_source_text) - 1u));
    (void)memset(&result, 0xa5, sizeof(result));
    status = ctool_asm_assemble(job, &source, &fixed_empty, output, &result);
    bytes = ctool_buffer_view(output);
    data_region = result.region_count == 1u ? &result.regions[0] : (const ctool_asm_region_t *)0;
    if (!check_status(status, CTOOL_OK, "fixed empty alignment assembly") || bytes.size != 4u ||
        bytes.data[0] != 0x20u || bytes.data[1] != 0x00u || bytes.data[2] != 0xa0u ||
        bytes.data[3] != 0x01u || result.region_count != 1u ||
        data_region == (const ctool_asm_region_t *)0 ||
        data_region->address != fixed_empty.as.fixed.data.base_address ||
        data_region->output_offset != 0u || data_region->file_size != 4u ||
        data_region->memory_size != 4u || result.has_entry != CTOOL_TRUE ||
        result.entry_address != 0x01a00020u || ctool_job_diagnostic_count(job) != 0u) {
        (void)fprintf(stderr, "fixed empty alignment artifact differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_buffer_close(output);
        ctool_job_close(job);
        return 1;
    }
    ctool_buffer_close(output);
    ctool_job_close(job);
    (void)puts("alignment: ok");
    return 0;
}

static int run_error_contracts(void) {
    static const char cycle_a[] = "%include \"cycle-b.asm\"\n";
    static const char cycle_b[] = "%include \"cycle-a.asm\"\n";
    static const char include_bad[] = "BITS 32\n"
                                      "main:\n"
                                      " mov eax, st0\n";
    static const char include_bad_main[] = "%include \"bad.inc\"\n";
    ctool_host_adapter_t adapter;
    ctool_job_config_t config;
    ctool_job_config_t limited_config;
    ctool_job_config_t store_config;
    ctool_asm_request_t raw;
    ctool_asm_request_t object;
    ctool_asm_request_t fixed;
    ctool_asm_request_t invalid;
    ctool_string_t entry = ctool_string("main");
    asm_contract_file_t cycle_files[2];
    asm_contract_store_t cycle_store;
    asm_contract_file_t bad_file;
    asm_contract_store_t bad_store;
    char depth_paths[33][24];
    char depth_contents[33][40];
    asm_contract_file_t depth_files[33];
    asm_contract_store_t depth_store;
    ctool_u32 index;
    ctool_status_t status = ctool_host_adapter_init(&adapter, ".");
    if (!check_status(status, CTOOL_OK, "error host adapter init")) {
        return 1;
    }
    config = ctool_host_job_config(&adapter, ctool_default_limits());
    init_raw_request(&raw);
    init_object_request(&object);
    init_fixed_request(&fixed, &entry);

    invalid = raw;
    invalid.initial_mode = (ctool_x86_mode_t)0;
    if (!expect_assembly_failure("invalid request", config, "/invalid-request.asm", "BITS 32\n",
                                 &invalid, CTOOL_ERR_UNSUPPORTED, CTOOL_ASM_DIAG_INVALID_REQUEST,
                                 "/invalid-request.asm") ||
        !expect_assembly_failure("undefined symbol", config, "/undefined.asm", "dd missing\n", &raw,
                                 CTOOL_ERR_NOT_FOUND, CTOOL_ASM_DIAG_UNDEFINED_SYMBOL,
                                 "/undefined.asm") ||
        !expect_assembly_failure("expression overflow", config, "/overflow.asm",
                                 "BITS 32\nsection .text\nmain: ret\nsection .data\n"
                                 "dd 0xffffffffffffffff + 1\n",
                                 &fixed, CTOOL_ERR_OVERFLOW, CTOOL_ASM_DIAG_EXPRESSION_OVERFLOW,
                                 "/overflow.asm") ||
        !expect_assembly_failure("fixed label addend overflow", config, "/label-overflow.asm",
                                 "BITS 32\nsection .text\nmain: ret\nsection .data\n"
                                 "dd main + 0xffffffffffffffff\n",
                                 &fixed, CTOOL_ERR_OVERFLOW, CTOOL_ASM_DIAG_EXPRESSION_OVERFLOW,
                                 "/label-overflow.asm") ||
        !expect_assembly_failure("non-relocatable expression", config, "/nonrel.asm",
                                 "extern left\nextern right\ndd left + right\n", &object,
                                 CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_NON_RELOCATABLE_EXPRESSION,
                                 "/nonrel.asm") ||
        !expect_assembly_failure("narrow relocation", config, "/narrow-reloc.asm",
                                 "extern target\ndb target\n", &object, CTOOL_ERR_UNSUPPORTED,
                                 CTOOL_ASM_DIAG_RELOCATION, "/narrow-reloc.asm") ||
        !expect_assembly_failure("invalid object origin", config, "/object-org.asm", "ORG 1\n",
                                 &object, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_INVALID_ORIGIN,
                                 "/object-org.asm") ||
        !expect_assembly_failure("missing symbol type", config, "/missing-symbol-type.asm",
                                 "global entry:\nentry: ret\n", &object, CTOOL_ERR_INPUT,
                                 CTOOL_ASM_DIAG_INVALID_SYMBOL_TYPE, "/missing-symbol-type.asm") ||
        !expect_assembly_failure("unsupported symbol type", config, "/object-symbol-type.asm",
                                 "global entry:object\nentry: ret\n", &object, CTOOL_ERR_INPUT,
                                 CTOOL_ASM_DIAG_INVALID_SYMBOL_TYPE, "/object-symbol-type.asm") ||
        !expect_assembly_failure("invalid bits mode", config, "/bits.asm", "BITS 64\n", &raw,
                                 CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_INVALID_MODE, "/bits.asm") ||
        !expect_assembly_failure("return cleanup overflow", config, "/ret-overflow.asm",
                                 "BITS 32\nret 65536\n", &raw, CTOOL_ERR_INPUT,
                                 CTOOL_ASM_DIAG_ENCODING, "/ret-overflow.asm") ||
        !expect_assembly_failure("conditional move byte operands", config, "/cmov-byte.asm",
                                 "BITS 32\ncmovne al, cl\n", &raw, CTOOL_ERR_INPUT,
                                 CTOOL_ASM_DIAG_ENCODING, "/cmov-byte.asm") ||
        !expect_assembly_failure("conditional move immediate source", config, "/cmov-immediate.asm",
                                 "BITS 32\ncmovne eax, 1\n", &raw, CTOOL_ERR_INPUT,
                                 CTOOL_ASM_DIAG_ENCODING, "/cmov-immediate.asm") ||
        !expect_assembly_failure("conditional move width mismatch", config, "/cmov-width.asm",
                                 "BITS 32\ncmovne eax, cx\n", &raw, CTOOL_ERR_INPUT,
                                 CTOOL_ASM_DIAG_ENCODING, "/cmov-width.asm") ||
        !expect_assembly_failure("conditional move lock prefix", config, "/cmov-lock.asm",
                                 "BITS 32\nlock cmovne eax, ecx\n", &raw, CTOOL_ERR_INPUT,
                                 CTOOL_ASM_DIAG_ENCODING, "/cmov-lock.asm") ||
        !expect_assembly_failure("conditional move repeat prefix", config, "/cmov-rep.asm",
                                 "BITS 32\nrep cmovne eax, ecx\n", &raw, CTOOL_ERR_INPUT,
                                 CTOOL_ASM_DIAG_ENCODING, "/cmov-rep.asm") ||
        !expect_assembly_failure("conditional move repeat-not-equal prefix", config,
                                 "/cmov-repne.asm", "BITS 32\nrepne cmovne eax, ecx\n", &raw,
                                 CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_ENCODING, "/cmov-repne.asm") ||
        !expect_assembly_failure("parity SETcc non-byte register", config, "/setp-width.asm",
                                 "BITS 32\nsetp eax\n", &raw, CTOOL_ERR_INPUT,
                                 CTOOL_ASM_DIAG_ENCODING, "/setp-width.asm") ||
        !expect_assembly_failure("parity SETcc immediate operand", config, "/setnp-immediate.asm",
                                 "BITS 32\nsetnp 1\n", &raw, CTOOL_ERR_INPUT,
                                 CTOOL_ASM_DIAG_ENCODING, "/setnp-immediate.asm") ||
        !expect_assembly_failure("parity SETcc lock prefix", config, "/setp-lock.asm",
                                 "BITS 32\nlock setp byte [eax]\n", &raw, CTOOL_ERR_INPUT,
                                 CTOOL_ASM_DIAG_ENCODING, "/setp-lock.asm") ||
        !expect_assembly_failure("parity SETcc alternate spelling", config, "/setpe-alias.asm",
                                 "BITS 32\nsetpe dl\n", &raw, CTOOL_ERR_INPUT,
                                 CTOOL_ASM_DIAG_SYNTAX, "/setpe-alias.asm") ||
        !expect_assembly_failure("parity SETcc alternate spelling", config, "/setpo-alias.asm",
                                 "BITS 32\nsetpo dl\n", &raw, CTOOL_ERR_INPUT,
                                 CTOOL_ASM_DIAG_SYNTAX, "/setpo-alias.asm") ||
        !expect_assembly_failure("immediate IMUL memory destination", config,
                                 "/imul-memory-destination.asm",
                                 "BITS 32\nimul dword [eax], ecx, 2\n", &raw, CTOOL_ERR_INPUT,
                                 CTOOL_ASM_DIAG_ENCODING, "/imul-memory-destination.asm") ||
        !expect_assembly_failure("immediate IMUL byte operands", config, "/imul-byte.asm",
                                 "BITS 32\nimul al, cl, 2\n", &raw, CTOOL_ERR_INPUT,
                                 CTOOL_ASM_DIAG_ENCODING, "/imul-byte.asm") ||
        !expect_assembly_failure("immediate IMUL width mismatch", config, "/imul-width.asm",
                                 "BITS 32\nimul eax, cx, 2\n", &raw, CTOOL_ERR_INPUT,
                                 CTOOL_ASM_DIAG_ENCODING, "/imul-width.asm") ||
        !expect_assembly_failure("immediate IMUL lock prefix", config, "/imul-lock.asm",
                                 "BITS 32\nlock imul eax, ecx, 2\n", &raw, CTOOL_ERR_INPUT,
                                 CTOOL_ASM_DIAG_ENCODING, "/imul-lock.asm") ||
        !expect_assembly_failure("immediate IMUL repeat prefix", config, "/imul-rep.asm",
                                 "BITS 32\nrep imul eax, ecx, 2\n", &raw, CTOOL_ERR_INPUT,
                                 CTOOL_ASM_DIAG_ENCODING, "/imul-rep.asm") ||
        !expect_assembly_failure("immediate IMUL repeat-not-equal prefix", config,
                                 "/imul-repne.asm", "BITS 32\nrepne imul eax, ecx, 2\n", &raw,
                                 CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_ENCODING, "/imul-repne.asm") ||
        !expect_assembly_failure("padding NOP byte memory", config, "/nop-byte.asm",
                                 "BITS 32\nnop byte [eax]\n", &raw, CTOOL_ERR_INPUT,
                                 CTOOL_ASM_DIAG_ENCODING, "/nop-byte.asm") ||
        !expect_assembly_failure("padding NOP qword memory", config, "/nop-qword.asm",
                                 "BITS 32\nnop qword [eax]\n", &raw, CTOOL_ERR_INPUT,
                                 CTOOL_ASM_DIAG_ENCODING, "/nop-qword.asm") ||
        !expect_assembly_failure("padding NOP byte register", config, "/nop-register.asm",
                                 "BITS 32\nnop al\n", &raw, CTOOL_ERR_INPUT,
                                 CTOOL_ASM_DIAG_ENCODING, "/nop-register.asm") ||
        !expect_assembly_failure("padding NOP lock prefix", config, "/nop-lock.asm",
                                 "BITS 32\nlock nop [eax]\n", &raw, CTOOL_ERR_INPUT,
                                 CTOOL_ASM_DIAG_ENCODING, "/nop-lock.asm") ||
        !expect_assembly_failure("padding NOP repeat prefix", config, "/nop-rep.asm",
                                 "BITS 32\nrep nop [eax]\n", &raw, CTOOL_ERR_INPUT,
                                 CTOOL_ASM_DIAG_ENCODING, "/nop-rep.asm") ||
        !expect_assembly_failure("padding NOP extra operand", config, "/nop-extra.asm",
                                 "BITS 32\nnop eax, ecx\n", &raw, CTOOL_ERR_INPUT,
                                 CTOOL_ASM_DIAG_ENCODING, "/nop-extra.asm") ||
        !expect_assembly_failure("instruction in bss", config, "/bss-instruction.asm",
                                 "BITS 32\nsection .bss\nmain: ret\n", &fixed, CTOOL_ERR_INPUT,
                                 CTOOL_ASM_DIAG_INVALID_SECTION, "/bss-instruction.asm") ||
        !expect_assembly_failure("missing fixed entry", config, "/missing-entry.asm",
                                 "BITS 32\nret\n", &fixed, CTOOL_ERR_NOT_FOUND,
                                 CTOOL_ASM_DIAG_ENTRY, "/missing-entry.asm")) {
        return 1;
    }

    limited_config = config;
    limited_config.limits.output_bytes = 2u;
    if (!expect_assembly_failure("output rollback", limited_config, "/output-limit.asm",
                                 "BITS 32\ndd 1\n", &raw, CTOOL_ERR_LIMIT, CTOOL_ASM_DIAG_OUTPUT,
                                 "/output-limit.asm")) {
        return 1;
    }

    cycle_files[0].path = ctool_string("/cycle-a.asm");
    cycle_files[0].contents = ctool_bytes(cycle_a, (ctool_u32)(sizeof(cycle_a) - 1u));
    cycle_files[1].path = ctool_string("/cycle-b.asm");
    cycle_files[1].contents = ctool_bytes(cycle_b, (ctool_u32)(sizeof(cycle_b) - 1u));
    cycle_store.files = cycle_files;
    cycle_store.count = 2u;
    store_config = config;
    store_config.files.context = &cycle_store;
    store_config.files.file_size = contract_file_size;
    store_config.files.read_exact = contract_read_exact;
    if (!expect_assembly_failure("include cycle", store_config, "/cycle-a.asm", cycle_a, &raw,
                                 CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_INCLUDE_CYCLE, "/cycle-b.asm")) {
        return 1;
    }

    for (index = 0u; index < 33u; index++) {
        int path_size = snprintf(depth_paths[index], sizeof(depth_paths[index]), "/depth-%u.asm",
                                 (unsigned)index);
        int content_size;
        if (index + 1u < 33u) {
            content_size = snprintf(depth_contents[index], sizeof(depth_contents[index]),
                                    "%%include \"depth-%u.asm\"\n", (unsigned)(index + 1u));
        } else {
            content_size =
                snprintf(depth_contents[index], sizeof(depth_contents[index]), "BITS 32\n");
        }
        if (path_size <= 0 || content_size <= 0 ||
            (size_t)path_size >= sizeof(depth_paths[index]) ||
            (size_t)content_size >= sizeof(depth_contents[index])) {
            (void)fprintf(stderr, "include depth fixture formatting failed\n");
            return 1;
        }
        depth_files[index].path = ctool_string(depth_paths[index]);
        depth_files[index].contents =
            ctool_bytes(depth_contents[index], (ctool_u32)(unsigned)content_size);
    }
    depth_store.files = depth_files;
    depth_store.count = 33u;
    store_config.files.context = &depth_store;
    if (!expect_assembly_failure("include depth", store_config, "/depth-0.asm", depth_contents[0],
                                 &raw, CTOOL_ERR_LIMIT, CTOOL_ASM_DIAG_INCLUDE_DEPTH,
                                 "/depth-31.asm")) {
        return 1;
    }

    bad_file.path = ctool_string("/bad.inc");
    bad_file.contents = ctool_bytes(include_bad, (ctool_u32)(sizeof(include_bad) - 1u));
    bad_store.files = &bad_file;
    bad_store.count = 1u;
    store_config.files.context = &bad_store;
    if (!expect_assembly_failure("included encoding path", store_config, "/include-main.asm",
                                 include_bad_main, &raw, CTOOL_ERR_INPUT, CTOOL_ASM_DIAG_ENCODING,
                                 "/bad.inc")) {
        return 1;
    }

    (void)puts("errors: ok");
    return 0;
}

static int run_long_source_line(void) {
    ctool_host_adapter_t adapter;
    ctool_job_config_t config;
    ctool_job_t *job;
    ctool_buffer_t *output;
    ctool_source_t source;
    ctool_asm_request_t request;
    ctool_asm_result_t result;
    ctool_bytes_t bytes;
    char source_text[512];
    ctool_u32 position = 0u;
    ctool_u32 index;
    ctool_status_t status;
    static const char prefix[] = "BITS 32\ndb ";
    for (index = 0u; index < (ctool_u32)(sizeof(prefix) - 1u); index++) {
        source_text[position++] = prefix[index];
    }
    for (index = 0u; index < 70u; index++) {
        source_text[position++] = '1';
        source_text[position++] = index + 1u == 70u ? '\n' : ',';
    }
    source_text[position] = '\0';
    status = ctool_host_adapter_init(&adapter, ".");
    if (!check_status(status, CTOOL_OK, "long line adapter init")) {
        return 1;
    }
    config = ctool_host_job_config(&adapter, ctool_default_limits());
    status = ctool_job_open(&config, &job);
    if (!check_status(status, CTOOL_OK, "long line job open")) {
        return 1;
    }
    status = ctool_job_open_buffer(job, 128u, config.limits.output_bytes, &output);
    if (!check_status(status, CTOOL_OK, "long line output open")) {
        ctool_job_close(job);
        return 1;
    }
    source.path.text = ctool_string("/long-line.asm");
    source.contents = ctool_bytes(source_text, position);
    init_raw_request(&request);
    (void)memset(&result, 0, sizeof(result));
    status = ctool_asm_assemble(job, &source, &request, output, &result);
    bytes = ctool_buffer_view(output);
    if (!check_status(status, CTOOL_OK, "long source line") || bytes.size != 70u ||
        ctool_job_diagnostic_count(job) != 0u) {
        (void)ctool_job_render_diagnostics(job);
        ctool_buffer_close(output);
        ctool_job_close(job);
        return 1;
    }
    for (index = 0u; index < bytes.size; index++) {
        if (bytes.data[index] != 1u) {
            (void)fprintf(stderr, "long source line byte mismatch\n");
            ctool_buffer_close(output);
            ctool_job_close(job);
            return 1;
        }
    }
    ctool_buffer_close(output);
    ctool_job_close(job);
    (void)puts("long-line: ok");
    return 0;
}

static int run_conditionals(void) {
    static const char *const sources[] = {
        "%ifdef SELECT\ndb 1\n%else\ndb 9\n%endif\n",
        "%ifdef ABSOLUTE\ndb 9\n%else\ndb 2\n%endif\n",
        "%ifdef select\ndb 9\n%else\ndb 3\n%endif\n",
        "%ifdef select\ndb 4\n%else\ndb 9\n%endif\n",
        "%ifdef LATER\ndb 9\n%endif\n%define LATER 0\n%ifdef LATER\ndb 5\n%endif\n",
        ("%ifdef MISSING\n%define IGNORED 1\nbad \"unterminated\n%endif\n%ifndef IGNORED\ndb "
         "6\n%endif\n"),
        "anchor:\n%define .LOCAL 1\n%ifdef .LOCAL\ndb 7\n%endif\n",
        "%ifdef MISSING\n%ifdef .LOCAL\ndb 9\n%endif\n%endif\ndb 8\n",
        "%ifdef MISSING\n\r%else\ndb 9\n%endif\n",
        "%ifdef MISSING\n%endif_extra \"unterminated\n%endif\ndb 10\n",
        "%else\n",
        "%ifdef SELECT\n",
        "%ifdef SELECT\n%else\n%else\n%endif\n"};
    ctool_host_adapter_t adapter;
    ctool_job_config_t config;
    ctool_asm_definition_t definitions[2];
    ctool_u32 index;
    if (ctool_host_adapter_init(&adapter, ".") != CTOOL_OK)
        return 1;
    config = ctool_host_job_config(&adapter, ctool_default_limits());
    definitions[0].name = ctool_string("SELECT");
    definitions[0].kind = CTOOL_ASM_DEFINE_CONSTANT;
    definitions[0].value = 0u;
    definitions[1].name = ctool_string("ABSOLUTE");
    definitions[1].kind = CTOOL_ASM_DEFINE_ABSOLUTE;
    definitions[1].value = 1u;
    for (index = 0u; index < 13u; index++) {
        ctool_job_t *job;
        ctool_buffer_t *output;
        ctool_source_t source;
        ctool_asm_request_t request;
        ctool_asm_result_t result;
        ctool_status_t status;
        ctool_bytes_t bytes;
        int good;
        if (ctool_job_open(&config, &job) != CTOOL_OK)
            return 2;
        if (ctool_job_open_buffer(job, 16u, ctool_default_limits().output_bytes, &output) !=
            CTOOL_OK) {
            ctool_job_close(job);
            return 3;
        }
        source.path.text = ctool_string("/conditionals.asm");
        source.contents = ctool_bytes(sources[index], (ctool_u32)strlen(sources[index]));
        init_raw_request(&request);
        request.definitions = definitions;
        request.definition_count = 2u;
        request.case_insensitive_symbols = index == 3u ? CTOOL_TRUE : CTOOL_FALSE;
        (void)memset(&result, 0xa5, sizeof(result));
        status = ctool_asm_assemble(job, &source, &request, output, &result);
        bytes = ctool_buffer_view(output);
        good = index < 10u ? status == CTOOL_OK && bytes.size == 1u && bytes.data[0] == index + 1u
                           : status == CTOOL_ERR_INPUT && bytes.size == 0u &&
                                 contract_result_is_zero(&result);
        if (!good) {
            (void)fprintf(stderr, "conditional contract row %u failed\n", (unsigned int)index);
            (void)ctool_job_render_diagnostics(job);
        }
        ctool_buffer_close(output);
        ctool_job_close(job);
        if (!good)
            return 4;
    }
    (void)puts("conditionals: ok");
    return 0;
}
