/**
 * @file cupidc_pp_directive_contract.inc
 * @brief Check pragmas, #exe blocks, and conditional directives.
 */

static int run_pragmas(void) {
    static const char source_text[] = "natural\n"
                                      "#define OBJECT object\n"
                                      "#define FUNCTION(value) value __LINE__\n"
                                      "#define STRING(value) #value\n"
                                      "#define PASTE(left, right) left ## right\n"
                                      "#define VARIADIC(...) __VA_ARGS__\n"
                                      "#pragma pack(push, 1)\n"
                                      "packed direct OBJECT FUNCTION(argument) STRING(stringified) "
                                      "PASTE(paste, d) VARIADIC(variable) __FILE__\n"
                                      "#if 0\n"
                                      "#pragma cupid_ignored\n"
                                      "#pragma pack(pop)\n"
                                      "#endif\n"
                                      "still_packed\n"
                                      "%:pragma pack(push, 4)\n"
                                      "four_byte\n"
                                      "%:pragma pack(2)\n"
                                      "two_byte\n"
                                      "%:pragma pack()\n"
                                      "reset_inside_push\n"
                                      "%:pragma pack(pop)\n"
                                      "back_to_one\n"
                                      "#pragma pack(push)\n"
                                      "unnamed_same\n"
                                      "#pragma pack(pop)\n"
                                      "#pragma pack(push, outer, 0x8U)\n"
                                      "named_eight\n"
                                      "#pragma pack(push, inner, 2)\n"
                                      "named_two\n"
                                      "#pragma pack(pop, outer)\n"
                                      "named_back_to_one\n"
                                      "#pragma pack(push, same)\n"
                                      "named_same\n"
                                      "#pragma pack(pop, same)\n"
                                      "#pragma pack(push, duplicate, 4U)\n"
                                      "duplicate_four\n"
                                      "#pragma pack(push, duplicate, 02)\n"
                                      "duplicate_two\n"
                                      "#pragma pack(pop, duplicate)\n"
                                      "duplicate_back_four\n"
                                      "#pragma pack(pop, duplicate)\n"
                                      "duplicate_back_one\n"
                                      "#pragma pack(push, 0)\n"
                                      "pushed_natural\n"
                                      "#pragma pack(pop)\n"
                                      "after_zero_push\n"
                                      "#pragma pack(0U)\n"
                                      "direct_natural\n"
                                      "#pragma pack(0x10ULL)\n"
                                      "wide_sixteen\n"
                                      "#pragma pack(01)\n"
                                      "direct_octal_one\n"
                                      "#pragma pack(pop)\n"
                                      "natural_again\n";
    static const char unbalanced_text[] = "#pragma pack(push, dangling, 1)\n"
                                          "accepted_dangling_push\n";
    static const char *const expected_text[] = {"natural",
                                                "packed",
                                                "direct",
                                                "object",
                                                "argument",
                                                "8",
                                                "\"stringified\"",
                                                "pasted",
                                                "variable",
                                                "\"/pragmas.c\"",
                                                "still_packed",
                                                "four_byte",
                                                "two_byte",
                                                "reset_inside_push",
                                                "back_to_one",
                                                "unnamed_same",
                                                "named_eight",
                                                "named_two",
                                                "named_back_to_one",
                                                "named_same",
                                                "duplicate_four",
                                                "duplicate_two",
                                                "duplicate_back_four",
                                                "duplicate_back_one",
                                                "pushed_natural",
                                                "after_zero_push",
                                                "direct_natural",
                                                "wide_sixteen",
                                                "direct_octal_one",
                                                "natural_again"};
    static const ctool_u32 expected_pack[] = {0u, 1u, 1u, 1u, 1u, 1u, 1u, 1u,  1u, 1u,
                                              1u, 4u, 2u, 0u, 1u, 1u, 8u, 2u,  1u, 1u,
                                              4u, 2u, 4u, 1u, 0u, 1u, 0u, 16u, 1u, 0u};
    ctool_host_adapter_t adapter;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_source_t source;
    ctool_c_pp_request_t request;
    ctool_c_pp_result_t result;
    ctool_status_t status;
    ctool_u32 index;
    ctool_u32 mode_index;

    if (open_job("pragmas", &adapter, &job) != 0) {
        return 1;
    }
    source.path.text = ctool_string("/pragmas.c");
    source.contents = ctool_bytes(source_text, (ctool_u32)(sizeof(source_text) - 1u));
    (void)memset(&request, 0, sizeof(request));
    for (mode_index = 0u; mode_index < 2u; mode_index++) {
        request.mode = mode_index == 0u ? CTOOL_C_PP_MODE_C11 : CTOOL_C_PP_MODE_CUPID;
        status = ctool_c_preprocess(job, &source, &request, &result);
        if (status != CTOOL_OK ||
            result.token_count != (ctool_u32)(sizeof(expected_text) / sizeof(expected_text[0])) ||
            result.token_count != (ctool_u32)(sizeof(expected_pack) / sizeof(expected_pack[0]))) {
            (void)fprintf(stderr, "pragmas: preprocessing failed (%s, %u tokens)\n",
                          ctool_status_name(status), result.token_count);
            (void)ctool_job_render_diagnostics(job);
            ctool_job_close(job);
            return 1;
        }
        for (index = 0u; index < result.token_count; index++) {
            if (!string_equal(result.tokens[index].spelling, expected_text[index]) ||
                !string_equal(result.tokens[index].location.path, "/pragmas.c") ||
                result.tokens[index].pack_alignment != expected_pack[index]) {
                (void)fprintf(stderr, "pragmas: mode %u token %u differs\n", mode_index, index);
                ctool_job_close(job);
                return 1;
            }
        }
    }
    source.path.text = ctool_string("/unbalanced-pragma.c");
    source.contents = ctool_bytes(unbalanced_text, (ctool_u32)(sizeof(unbalanced_text) - 1u));
    request.mode = CTOOL_C_PP_MODE_C11;
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || result.token_count != 1u ||
        !string_equal(result.tokens[0].spelling, "accepted_dangling_push") ||
        result.tokens[0].pack_alignment != 1u) {
        (void)fprintf(stderr, "pragmas: dangling push policy differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    ctool_job_close(job);
    (void)printf("pragmas: ok\n");
    return 0;
}

static int run_pragma_files(void) {
    static const char forced_text[] = "#pragma once\n"
                                      "#pragma pack(push, forced_scope, 4)\n"
                                      "forced_once\n";
    static const char once_text[] = "before_once\n"
                                    "#pragma once\n"
                                    "after_once\n"
                                    "#include \"once.h\"\n";
    static const char pack_text[] = "#pragma pack(push, header_scope, 1)\n"
                                    "header_packed\n";
    static const char leak_text[] = "#pragma pack(2)\n"
                                    "leaked_header\n";
    static const char twin_text[] = "#pragma once\n"
                                    "path_once\n";
    static const char inactive_once_text[] = "#if 0\n"
                                             "#pragma once\n"
                                             "#endif\n"
                                             "inactive_once_repeat\n";
    static const char ambient_text[] = "ambient_replay\n";
    static const char source_text[] = "#pragma pack(pop, forced_scope)\n"
                                      "primary_natural\n"
                                      "#include \"once.h\"\n"
                                      "#include \"./once.h\"\n"
                                      "#include \"twin-a.h\"\n"
                                      "#include \"twin-b.h\"\n"
                                      "#include \"inactive-once.h\"\n"
                                      "#include \"inactive-once.h\"\n"
                                      "#pragma pack(1)\n"
                                      "#include \"ambient.h\"\n"
                                      "#pragma pack(2)\n"
                                      "#include \"ambient.h\"\n"
                                      "#pragma pack()\n"
                                      "#include \"pack.h\"\n"
                                      "#pragma pack(pop, header_scope)\n"
                                      "between_natural\n"
                                      "#include \"pack.h\"\n"
                                      "#pragma pack(pop, header_scope)\n"
                                      "after_natural\n"
                                      "#include \"leak.h\"\n"
                                      "after_leak\n"
                                      "#pragma pack()\n"
                                      "#include \"forced-once.h\"\n";
    static const char self_text[] = "#pragma once\n"
                                    "self_once\n"
                                    "#include \"./self.c\"\n";
    static const char *const expected_text[] = {
        "forced_once",    "primary_natural", "before_once",          "after_once",
        "path_once",      "path_once",       "inactive_once_repeat", "inactive_once_repeat",
        "ambient_replay", "ambient_replay",  "header_packed",        "between_natural",
        "header_packed",  "after_natural",   "leaked_header",        "after_leak"};
    static const char *const expected_path[] = {
        "/forced-once.h", "/main.c",        "/inc/once.h",          "/inc/once.h",
        "/inc/twin-a.h",  "/inc/twin-b.h",  "/inc/inactive-once.h", "/inc/inactive-once.h",
        "/inc/ambient.h", "/inc/ambient.h", "/inc/pack.h",          "/main.c",
        "/inc/pack.h",    "/main.c",        "/inc/leak.h",          "/main.c"};
    static const ctool_u32 expected_pack[] = {4u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
                                              1u, 2u, 1u, 0u, 1u, 0u, 2u, 2u};
    fixture_file_t files[8];
    fixture_store_t store;
    ctool_host_adapter_t adapter;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_source_t source;
    ctool_path_t forced;
    ctool_c_pp_include_root_t roots[2];
    ctool_c_pp_request_t request;
    ctool_c_pp_result_t result;
    ctool_status_t status;
    ctool_u32 index;

    files[0].path = ctool_string("/forced-once.h");
    files[0].contents = ctool_bytes(forced_text, (ctool_u32)(sizeof(forced_text) - 1u));
    files[1].path = ctool_string("/inc/once.h");
    files[1].contents = ctool_bytes(once_text, (ctool_u32)(sizeof(once_text) - 1u));
    files[2].path = ctool_string("/inc/pack.h");
    files[2].contents = ctool_bytes(pack_text, (ctool_u32)(sizeof(pack_text) - 1u));
    files[3].path = ctool_string("/inc/leak.h");
    files[3].contents = ctool_bytes(leak_text, (ctool_u32)(sizeof(leak_text) - 1u));
    files[4].path = ctool_string("/inc/twin-a.h");
    files[4].contents = ctool_bytes(twin_text, (ctool_u32)(sizeof(twin_text) - 1u));
    files[5].path = ctool_string("/inc/twin-b.h");
    files[5].contents = ctool_bytes(twin_text, (ctool_u32)(sizeof(twin_text) - 1u));
    files[6].path = ctool_string("/inc/inactive-once.h");
    files[6].contents =
        ctool_bytes(inactive_once_text, (ctool_u32)(sizeof(inactive_once_text) - 1u));
    files[7].path = ctool_string("/inc/ambient.h");
    files[7].contents = ctool_bytes(ambient_text, (ctool_u32)(sizeof(ambient_text) - 1u));
    store.files = files;
    store.file_count = 8u;
    store.read_count = 0u;
    if (open_fixture_job("pragma-files", &store, &adapter, &job) != 0) {
        return 1;
    }
    roots[0].directory.text = ctool_string("/inc");
    roots[0].forms = CTOOL_C_PP_INCLUDE_QUOTED;
    roots[1].directory.text = ctool_string("/");
    roots[1].forms = CTOOL_C_PP_INCLUDE_QUOTED;
    forced.text = ctool_string("/forced-once.h");
    source.path.text = ctool_string("/main.c");
    source.contents = ctool_bytes(source_text, (ctool_u32)(sizeof(source_text) - 1u));
    (void)memset(&request, 0, sizeof(request));
    request.mode = CTOOL_C_PP_MODE_C11;
    request.include_roots = roots;
    request.include_root_count = 2u;
    request.forced_includes = &forced;
    request.forced_include_count = 1u;
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || store.read_count != 8u ||
        result.token_count != (ctool_u32)(sizeof(expected_text) / sizeof(expected_text[0]))) {
        (void)fprintf(stderr, "pragma-files: preprocessing failed (%s, %u tokens, %u reads)\n",
                      ctool_status_name(status), result.token_count, store.read_count);
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    for (index = 0u; index < result.token_count; index++) {
        if (!string_equal(result.tokens[index].spelling, expected_text[index]) ||
            !string_equal(result.tokens[index].location.path, expected_path[index]) ||
            result.tokens[index].pack_alignment != expected_pack[index]) {
            (void)fprintf(stderr, "pragma-files: token %u differs\n", index);
            ctool_job_close(job);
            return 1;
        }
    }

    source.path.text = ctool_string("/self.c");
    source.contents = ctool_bytes(self_text, (ctool_u32)(sizeof(self_text) - 1u));
    request.forced_includes = (const ctool_path_t *)0;
    request.forced_include_count = 0u;
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || store.read_count != 8u || result.token_count != 1u ||
        !string_equal(result.tokens[0].spelling, "self_once") ||
        !string_equal(result.tokens[0].location.path, "/self.c") ||
        result.tokens[0].pack_alignment != 0u || ctool_job_diagnostic_count(job) != 0u) {
        (void)fprintf(stderr, "pragma-files: primary once recursion differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    request.mode = CTOOL_C_PP_MODE_CUPID;
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || store.read_count != 8u || result.token_count != 1u ||
        !string_equal(result.tokens[0].spelling, "self_once") ||
        !string_equal(result.tokens[0].location.path, "/self.c") ||
        result.tokens[0].pack_alignment != 0u || ctool_job_diagnostic_count(job) != 0u) {
        (void)fprintf(stderr, "pragma-files: once state leaked between jobs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    ctool_job_close(job);
    (void)printf("pragma-files: ok\n");
    return 0;
}

static int run_pragma_errors(void) {
    static const char *const error_text[] = {"#pragma once extra\n",
                                             "#pragma pack\n",
                                             "#pragma pack(3)\n",
                                             "#pragma pack(push,)\n",
                                             "#pragma pack(pop)\n",
                                             "#pragma pack(push, 1, 2)\n",
                                             "#pragma cupid_vendor_extension\n",
                                             "#pragma pack(pop, missing)\n",
                                             "#pragma pack(pop, 1)\n",
                                             "#pragma pack(pop, label, 2)\n",
                                             "#pragma pack(push, 123, 1)\n",
                                             "#pragma pack(1UU)\n",
                                             "#pragma pack(0x)\n",
                                             "#define CAP 1\n#pragma pack(CAP)\n"};
    static const ctool_status_t error_status[] = {
        CTOOL_ERR_INPUT, CTOOL_ERR_INPUT,       CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT,
        CTOOL_ERR_INPUT, CTOOL_ERR_UNSUPPORTED, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT,
        CTOOL_ERR_INPUT, CTOOL_ERR_INPUT,       CTOOL_ERR_INPUT, CTOOL_ERR_INPUT};
    static const ctool_u32 error_code[] = {
        CTOOL_C_PP_DIAG_DIRECTIVE,   CTOOL_C_PP_DIAG_PRAGMA_PACK, CTOOL_C_PP_DIAG_PRAGMA_PACK,
        CTOOL_C_PP_DIAG_PRAGMA_PACK, CTOOL_C_PP_DIAG_PRAGMA_PACK, CTOOL_C_PP_DIAG_PRAGMA_PACK,
        CTOOL_C_PP_DIAG_DIRECTIVE,   CTOOL_C_PP_DIAG_PRAGMA_PACK, CTOOL_C_PP_DIAG_PRAGMA_PACK,
        CTOOL_C_PP_DIAG_PRAGMA_PACK, CTOOL_C_PP_DIAG_PRAGMA_PACK, CTOOL_C_PP_DIAG_PRAGMA_PACK,
        CTOOL_C_PP_DIAG_PRAGMA_PACK, CTOOL_C_PP_DIAG_PRAGMA_PACK};
    static const ctool_u32 error_line[] = {1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 2u};
    static const char recovery_text[] = "#pragma pack(push, 2)\n"
                                        "recovered\n"
                                        "#pragma pack(pop)\n"
                                        "natural\n";
    ctool_host_adapter_t adapter;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_source_t source;
    ctool_c_pp_request_t request;
    ctool_c_pp_result_t result;
    ctool_arena_mark_t mark;
    const ctool_diagnostic_t *diagnostic;
    ctool_status_t status;
    ctool_u32 case_count = (ctool_u32)(sizeof(error_text) / sizeof(error_text[0]));
    ctool_u32 index;

    if (open_job("pragma-errors", &adapter, &job) != 0) {
        return 1;
    }
    source.path.text = ctool_string("/pragma-errors.c");
    (void)memset(&request, 0, sizeof(request));
    request.mode = CTOOL_C_PP_MODE_C11;
    for (index = 0u; index < case_count; index++) {
        source.contents = ctool_bytes(error_text[index], (ctool_u32)strlen(error_text[index]));
        (void)memset(&result, 0xa5, sizeof(result));
        mark = ctool_arena_mark(ctool_job_arena(job));
        status = ctool_c_preprocess(job, &source, &request, &result);
        diagnostic = ctool_job_diagnostic(job, index);
        if (status != error_status[index] || result.tokens != NULL || result.token_count != 0u ||
            diagnostic == NULL || diagnostic->code != error_code[index] ||
            !string_equal(diagnostic->path, "/pragma-errors.c") ||
            diagnostic->line != error_line[index] || diagnostic->column != 1u ||
            !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
            (void)fprintf(stderr, "pragma-errors: case %u differs (%s)\n", index,
                          ctool_status_name(status));
            (void)ctool_job_render_diagnostics(job);
            ctool_job_close(job);
            return 1;
        }
    }
    source.contents = ctool_bytes(recovery_text, (ctool_u32)(sizeof(recovery_text) - 1u));
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || result.token_count != 2u ||
        !string_equal(result.tokens[0].spelling, "recovered") ||
        result.tokens[0].pack_alignment != 2u ||
        !string_equal(result.tokens[1].spelling, "natural") ||
        result.tokens[1].pack_alignment != 0u || ctool_job_diagnostic_count(job) != case_count) {
        (void)fprintf(stderr, "pragma-errors: recovery differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    ctool_job_close(job);
    (void)printf("pragma-errors: ok\n");
    return 0;
}

static int run_pragma_scale(void) {
    enum {
        PACK_DEPTH_LIMIT = 256,
        REUSE_COUNT = 2048,
        REUSE_FORCED_COUNT = 16384,
        SOURCE_CAPACITY = 128 * 1024
    };

    static char source_text[SOURCE_CAPACITY];
    static char failure_text[SOURCE_CAPACITY];
    static const char recovery_text[] = "after_failure\n";
    static const char forced_reuse_text[] = "#pragma pack(push, 1)\n"
                                            "#pragma pack(pop)\n";
    static ctool_path_t forced_reuse[REUSE_FORCED_COUNT];
    fixture_file_t reuse_file;
    fixture_store_t reuse_store;
    ctool_host_adapter_t adapter;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_job_config_t config;
    ctool_limits_t limits;
    ctool_source_t source;
    ctool_c_pp_request_t request;
    ctool_c_pp_result_t result;
    ctool_arena_mark_t mark;
    const ctool_diagnostic_t *diagnostic;
    ctool_status_t status;
    ctool_u32 source_size = 0u;
    ctool_u32 failure_size = 0u;
    ctool_u32 index;

    for (index = 0u; index < PACK_DEPTH_LIMIT; index++) {
        if (append_text(source_text, SOURCE_CAPACITY, &source_size, "#pragma pack(push, 1)\n") !=
                0 ||
            append_text(failure_text, SOURCE_CAPACITY, &failure_size, "#pragma pack(push, 1)\n") !=
                0) {
            return 1;
        }
    }
    if (append_text(source_text, SOURCE_CAPACITY, &source_size, "at_limit\n") != 0 ||
        append_text(failure_text, SOURCE_CAPACITY, &failure_size, "#pragma pack(push, 1)\n") != 0) {
        return 1;
    }
    for (index = 0u; index < PACK_DEPTH_LIMIT; index++) {
        if (append_text(source_text, SOURCE_CAPACITY, &source_size, "#pragma pack(pop)\n") != 0) {
            return 1;
        }
    }
    for (index = 0u; index < REUSE_COUNT; index++) {
        if (append_text(source_text, SOURCE_CAPACITY, &source_size,
                        "#pragma pack(push, 2)\n#pragma pack(pop)\n") != 0) {
            return 1;
        }
    }
    if (append_text(source_text, SOURCE_CAPACITY, &source_size, "after_reuse\n") != 0) {
        return 1;
    }

    if (open_job("pragma-scale", &adapter, &job) != 0) {
        return 1;
    }
    source.path.text = ctool_string("/pragma-scale.c");
    source.contents = ctool_bytes(source_text, source_size);
    (void)memset(&request, 0, sizeof(request));
    request.mode = CTOOL_C_PP_MODE_C11;
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || result.token_count != 2u ||
        !string_equal(result.tokens[0].spelling, "at_limit") ||
        result.tokens[0].pack_alignment != 1u ||
        !string_equal(result.tokens[1].spelling, "after_reuse") ||
        result.tokens[1].pack_alignment != 0u) {
        (void)fprintf(stderr, "pragma-scale: bounded success differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }

    source.path.text = ctool_string("/pragma-scale-fail.c");
    source.contents = ctool_bytes(failure_text, failure_size);
    mark = ctool_arena_mark(ctool_job_arena(job));
    status = ctool_c_preprocess(job, &source, &request, &result);
    diagnostic = ctool_job_diagnostic(job, 0u);
    if (status != CTOOL_ERR_LIMIT || result.tokens != NULL || result.token_count != 0u ||
        diagnostic == NULL || diagnostic->code != CTOOL_C_PP_DIAG_PRAGMA_PACK ||
        !string_equal(diagnostic->path, "/pragma-scale-fail.c") || diagnostic->line != 257u ||
        diagnostic->column != 1u ||
        !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
        (void)fprintf(stderr, "pragma-scale: depth failure differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }

    source.path.text = ctool_string("/pragma-scale-recovery.c");
    source.contents = ctool_bytes(recovery_text, (ctool_u32)(sizeof(recovery_text) - 1u));
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || result.token_count != 1u ||
        !string_equal(result.tokens[0].spelling, "after_failure") ||
        result.tokens[0].pack_alignment != 0u || ctool_job_diagnostic_count(job) != 1u) {
        (void)fprintf(stderr, "pragma-scale: recovery differs\n");
        ctool_job_close(job);
        return 1;
    }
    ctool_job_close(job);

    reuse_file.path = ctool_string("/pack-reuse.h");
    reuse_file.contents =
        ctool_bytes(forced_reuse_text, (ctool_u32)(sizeof(forced_reuse_text) - 1u));
    reuse_store.files = &reuse_file;
    reuse_store.file_count = 1u;
    reuse_store.read_count = 0u;
    for (index = 0u; index < REUSE_FORCED_COUNT; index++) {
        forced_reuse[index].text = ctool_string("/pack-reuse.h");
    }
    limits = ctool_default_limits();
    limits.arena_block_bytes = 4096u;
    limits.arena_bytes = 256u * 1024u;
    config = ctool_host_job_config(&adapter, limits);
    config.files.context = &reuse_store;
    config.files.file_size = fixture_file_size;
    config.files.read_exact = fixture_read_exact;
    config.files.write_all = fixture_write_all;
    status = ctool_job_open(&config, &job);
    if (status != CTOOL_OK) {
        (void)fprintf(stderr, "pragma-scale: reuse job open failed (%s)\n",
                      ctool_status_name(status));
        return 1;
    }
    source.path.text = ctool_string("/pack-reuse-primary.c");
    source.contents = ctool_bytes("", 0u);
    (void)memset(&request, 0, sizeof(request));
    request.mode = CTOOL_C_PP_MODE_C11;
    request.forced_includes = forced_reuse;
    request.forced_include_count = REUSE_FORCED_COUNT;
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || result.token_count != 0u || reuse_store.read_count != 1u ||
        ctool_job_diagnostic_count(job) != 0u) {
        (void)fprintf(stderr, "pragma-scale: bounded frame reuse differs (%s, %u reads)\n",
                      ctool_status_name(status), reuse_store.read_count);
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    ctool_job_close(job);
    (void)printf("pragma-scale: ok\n");
    return 0;
}

static int run_cupid_exe(void) {
    static const char source_text[] = "#define VALUE expanded\n"
                                      "#define CALL(value) value\n"
                                      "ordinary\n"
                                      "#pragma pack(2)\n"
                                      "#exe { VALUE __LINE__ CALL(\n"
                                      "cross_line)\n"
                                      "#if 1\n"
                                      "nested\n"
                                      "#endif\n"
                                      "}\n"
                                      "#if 0\n"
                                      "#exe malformed\n"
                                      "#endif\n"
                                      "%:exe { same_line }\n"
                                      "#pragma pack()\n"
                                      "after\n";
    static const char c11_skipped_text[] = "#if 0\n"
                                           "#exe malformed\n"
                                           "#endif\n"
                                           "c11_ok\n";
    static const char selected_text[] = "#if 1\n"
                                        "#exe { selected }\n"
                                        "#endif\n";
    static const char unclosed_text[] = "#exe {\n"
                                        "parser_owns_close\n";
    static const char spliced_text[] = "#ex\\\n"
                                       "e /* comment */ \\\n"
                                       "{ spliced }\n";
    static const char directive_name_text[] = "#define exe changed\n"
                                              "#exe {}\n"
                                              "exe\n";
    char owned_text[] = "#exe {}\n";
    char owned_path[] = "/owned-exe.cc";
    static const char *const expected_text[] = {"ordinary",   "exe",    "{",    "expanded", "5",
                                                "cross_line", "nested", "}",    "exe",      "{",
                                                "same_line",  "}",      "after"};
    static const ctool_c_pp_token_kind_t expected_kind[] = {
        CTOOL_C_PP_TOKEN_IDENTIFIER, CTOOL_C_PP_TOKEN_CUPID_EXE,  CTOOL_C_PP_TOKEN_PUNCTUATOR,
        CTOOL_C_PP_TOKEN_IDENTIFIER, CTOOL_C_PP_TOKEN_NUMBER,     CTOOL_C_PP_TOKEN_IDENTIFIER,
        CTOOL_C_PP_TOKEN_IDENTIFIER, CTOOL_C_PP_TOKEN_PUNCTUATOR, CTOOL_C_PP_TOKEN_CUPID_EXE,
        CTOOL_C_PP_TOKEN_PUNCTUATOR, CTOOL_C_PP_TOKEN_IDENTIFIER, CTOOL_C_PP_TOKEN_PUNCTUATOR,
        CTOOL_C_PP_TOKEN_IDENTIFIER};
    static const ctool_u32 expected_line[] = {3u,  5u,  5u,  5u,  5u,  6u, 8u,
                                              10u, 14u, 14u, 14u, 14u, 16u};
    static const ctool_u32 expected_column[] = {1u, 1u, 6u, 8u, 14u, 1u, 1u,
                                                1u, 1u, 7u, 9u, 19u, 1u};
    static const ctool_u32 expected_pack[] = {0u, 2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u, 0u};
    ctool_host_adapter_t adapter;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_source_t source;
    ctool_c_pp_request_t request;
    ctool_c_pp_result_t result;
    ctool_status_t status;
    ctool_u32 index;

    if (open_job("cupid-exe", &adapter, &job) != 0) {
        return 1;
    }
    source.path.text = ctool_string("/cupid-exe.cc");
    source.contents = ctool_bytes(source_text, (ctool_u32)(sizeof(source_text) - 1u));
    (void)memset(&request, 0, sizeof(request));
    request.mode = CTOOL_C_PP_MODE_CUPID;
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK ||
        result.token_count != (ctool_u32)(sizeof(expected_text) / sizeof(expected_text[0]))) {
        (void)fprintf(stderr, "cupid-exe: preprocessing failed (%s, %u tokens)\n",
                      ctool_status_name(status), result.token_count);
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    for (index = 0u; index < result.token_count; index++) {
        if (!string_equal(result.tokens[index].spelling, expected_text[index]) ||
            result.tokens[index].kind != expected_kind[index] ||
            !string_equal(result.tokens[index].location.path, "/cupid-exe.cc") ||
            result.tokens[index].location.line != expected_line[index] ||
            result.tokens[index].location.column != expected_column[index] ||
            result.tokens[index].pack_alignment != expected_pack[index]) {
            (void)fprintf(stderr, "cupid-exe: token %u differs\n", index);
            ctool_job_close(job);
            return 1;
        }
    }

    source.path.text = ctool_string("/c11-skipped-exe.c");
    source.contents = ctool_bytes(c11_skipped_text, (ctool_u32)(sizeof(c11_skipped_text) - 1u));
    request.mode = CTOOL_C_PP_MODE_C11;
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || result.token_count != 1u ||
        !string_equal(result.tokens[0].spelling, "c11_ok") ||
        result.tokens[0].kind != CTOOL_C_PP_TOKEN_IDENTIFIER ||
        ctool_job_diagnostic_count(job) != 0u) {
        (void)fprintf(stderr, "cupid-exe: inactive C11 directive differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }

    source.path.text = ctool_string("/selected-exe.cc");
    source.contents = ctool_bytes(selected_text, (ctool_u32)(sizeof(selected_text) - 1u));
    request.mode = CTOOL_C_PP_MODE_CUPID;
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || result.token_count != 4u ||
        result.tokens[0].kind != CTOOL_C_PP_TOKEN_CUPID_EXE ||
        result.tokens[0].location.line != 2u || !string_equal(result.tokens[1].spelling, "{") ||
        !string_equal(result.tokens[2].spelling, "selected") ||
        !string_equal(result.tokens[3].spelling, "}") || ctool_job_diagnostic_count(job) != 0u) {
        (void)fprintf(stderr, "cupid-exe: selected conditional differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }

    source.path.text = ctool_string("/unclosed-exe.cc");
    source.contents = ctool_bytes(unclosed_text, (ctool_u32)(sizeof(unclosed_text) - 1u));
    request.mode = CTOOL_C_PP_MODE_CUPID;
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || result.token_count != 3u ||
        result.tokens[0].kind != CTOOL_C_PP_TOKEN_CUPID_EXE ||
        !string_equal(result.tokens[1].spelling, "{") ||
        !string_equal(result.tokens[2].spelling, "parser_owns_close") ||
        ctool_job_diagnostic_count(job) != 0u) {
        (void)fprintf(stderr, "cupid-exe: parser ownership differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }

    source.path.text = ctool_string("/spliced-exe.cc");
    source.contents = ctool_bytes(spliced_text, (ctool_u32)(sizeof(spliced_text) - 1u));
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || result.token_count != 4u ||
        result.tokens[0].kind != CTOOL_C_PP_TOKEN_CUPID_EXE ||
        result.tokens[0].location.line != 1u || result.tokens[0].location.column != 1u ||
        !string_equal(result.tokens[1].spelling, "{") || result.tokens[1].location.line != 3u ||
        result.tokens[1].location.column != 1u ||
        !string_equal(result.tokens[2].spelling, "spliced") ||
        result.tokens[2].location.line != 3u || result.tokens[2].location.column != 3u ||
        !string_equal(result.tokens[3].spelling, "}") || result.tokens[3].location.line != 3u ||
        result.tokens[3].location.column != 11u) {
        (void)fprintf(stderr, "cupid-exe: spliced directive differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }

    source.path.text = ctool_string("/exe-name-macro.cc");
    source.contents =
        ctool_bytes(directive_name_text, (ctool_u32)(sizeof(directive_name_text) - 1u));
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || result.token_count != 4u ||
        result.tokens[0].kind != CTOOL_C_PP_TOKEN_CUPID_EXE ||
        !string_equal(result.tokens[0].spelling, "exe") ||
        !string_equal(result.tokens[1].spelling, "{") ||
        !string_equal(result.tokens[2].spelling, "}") ||
        !string_equal(result.tokens[3].spelling, "changed")) {
        (void)fprintf(stderr, "cupid-exe: directive name expansion differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }

    source.path.text = ctool_string(owned_path);
    source.contents = ctool_bytes(owned_text, (ctool_u32)(sizeof(owned_text) - 1u));
    status = ctool_c_preprocess(job, &source, &request, &result);
    owned_text[1] = 'X';
    owned_path[1] = 'X';
    if (status != CTOOL_OK || result.token_count != 3u ||
        result.tokens[0].kind != CTOOL_C_PP_TOKEN_CUPID_EXE ||
        !string_equal(result.tokens[0].spelling, "exe") ||
        !string_equal(result.tokens[0].location.path, "/owned-exe.cc") ||
        result.tokens[0].location.line != 1u || result.tokens[0].location.column != 1u) {
        (void)fprintf(stderr, "cupid-exe: marker ownership differs\n");
        ctool_job_close(job);
        return 1;
    }
    ctool_job_close(job);
    (void)printf("cupid-exe: ok\n");
    return 0;
}

static int run_cupid_exe_active(const char *host_root) {
    ctool_host_adapter_t adapter;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_path_t path;
    ctool_source_t source;
    ctool_c_pp_request_t request;
    ctool_c_pp_result_t result;
    ctool_status_t status;
    ctool_u32 marker_count = 0u;
    ctool_u32 marker_index = 0u;
    ctool_u32 index;

    if (open_job_at_root("cupid-exe-active", host_root, &adapter, &job) != 0) {
        return 1;
    }
    path.text = ctool_string("/bin/feature6_exe.cc");
    status = ctool_job_load_source(job, &path, &source);
    if (status != CTOOL_OK) {
        (void)fprintf(stderr, "cupid-exe-active: source load failed (%s)\n",
                      ctool_status_name(status));
        ctool_job_close(job);
        return 1;
    }
    (void)memset(&request, 0, sizeof(request));
    request.mode = CTOOL_C_PP_MODE_CUPID;
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK) {
        (void)fprintf(stderr, "cupid-exe-active: preprocessing failed (%s)\n",
                      ctool_status_name(status));
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    for (index = 0u; index < result.token_count; index++) {
        if (result.tokens[index].kind == CTOOL_C_PP_TOKEN_CUPID_EXE) {
            marker_count++;
            marker_index = index;
        }
        if (string_starts_with(result.tokens[index].spelling, "__cc_exe_")) {
            (void)fprintf(stderr, "cupid-exe-active: policy lowering leaked\n");
            ctool_job_close(job);
            return 1;
        }
    }
    if (marker_count != 1u || marker_index + 2u >= result.token_count ||
        !string_equal(result.tokens[marker_index].spelling, "exe") ||
        !string_equal(result.tokens[marker_index].location.path, "/bin/feature6_exe.cc") ||
        result.tokens[marker_index].location.line != 7u ||
        result.tokens[marker_index].location.column != 1u ||
        result.tokens[marker_index].pack_alignment != 0u ||
        !string_equal(result.tokens[marker_index + 1u].spelling, "{") ||
        result.tokens[marker_index + 1u].location.line != 7u ||
        result.tokens[marker_index + 1u].location.column != 6u ||
        !string_equal(result.tokens[marker_index + 2u].spelling, "g_value") ||
        result.tokens[marker_index + 2u].location.line != 8u) {
        (void)fprintf(stderr, "cupid-exe-active: marker contract differs\n");
        ctool_job_close(job);
        return 1;
    }
    ctool_job_close(job);
    (void)printf("cupid-exe-active: ok\n");
    return 0;
}

static int run_cupid_exe_files(void) {
    static const char forced_text[] = "#pragma pack(4)\n"
                                      "#exe { forced_body }\n"
                                      "#pragma pack()\n";
    static const char guardless_text[] = "#exe { guardless_body }\n";
    static const char once_text[] = "#pragma once\n"
                                    "#exe { once_body }\n";
    static const char source_text[] = "#pragma pack(2)\n"
                                      "#include \"guardless-exe.h\"\n"
                                      "#pragma pack(1)\n"
                                      "#include \"./guardless-exe.h\"\n"
                                      "#pragma pack()\n"
                                      "#include \"once-exe.h\"\n"
                                      "#include \"once-exe.h\"\n"
                                      "#exe { primary_body }\n";
    static const char *const expected_path[] = {"/forced-exe.h", "/inc/guardless-exe.h",
                                                "/inc/guardless-exe.h", "/inc/once-exe.h",
                                                "/primary-exe.cc"};
    static const char *const expected_body[] = {"forced_body", "guardless_body", "guardless_body",
                                                "once_body", "primary_body"};
    static const ctool_u32 expected_pack[] = {4u, 2u, 1u, 0u, 0u};
    fixture_file_t files[3];
    fixture_store_t store;
    ctool_host_adapter_t adapter;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_source_t source;
    ctool_path_t forced;
    ctool_c_pp_include_root_t roots[2];
    ctool_c_pp_request_t request;
    ctool_c_pp_result_t result;
    ctool_status_t status;
    ctool_u32 index;

    files[0].path = ctool_string("/forced-exe.h");
    files[0].contents = ctool_bytes(forced_text, (ctool_u32)(sizeof(forced_text) - 1u));
    files[1].path = ctool_string("/inc/guardless-exe.h");
    files[1].contents = ctool_bytes(guardless_text, (ctool_u32)(sizeof(guardless_text) - 1u));
    files[2].path = ctool_string("/inc/once-exe.h");
    files[2].contents = ctool_bytes(once_text, (ctool_u32)(sizeof(once_text) - 1u));
    store.files = files;
    store.file_count = 3u;
    store.read_count = 0u;
    if (open_fixture_job("cupid-exe-files", &store, &adapter, &job) != 0) {
        return 1;
    }
    roots[0].directory.text = ctool_string("/inc");
    roots[0].forms = CTOOL_C_PP_INCLUDE_QUOTED;
    roots[1].directory.text = ctool_string("/");
    roots[1].forms = CTOOL_C_PP_INCLUDE_QUOTED;
    forced.text = ctool_string("/forced-exe.h");
    source.path.text = ctool_string("/primary-exe.cc");
    source.contents = ctool_bytes(source_text, (ctool_u32)(sizeof(source_text) - 1u));
    (void)memset(&request, 0, sizeof(request));
    request.mode = CTOOL_C_PP_MODE_CUPID;
    request.include_roots = roots;
    request.include_root_count = 2u;
    request.forced_includes = &forced;
    request.forced_include_count = 1u;
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || store.read_count != 3u || result.token_count != 20u) {
        (void)fprintf(stderr,
                      "cupid-exe-files: preprocessing failed (%s, %u tokens, "
                      "%u reads)\n",
                      ctool_status_name(status), result.token_count, store.read_count);
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    for (index = 0u; index < 5u; index++) {
        ctool_u32 offset = index * 4u;
        if (result.tokens[offset].kind != CTOOL_C_PP_TOKEN_CUPID_EXE ||
            !string_equal(result.tokens[offset].spelling, "exe") ||
            !string_equal(result.tokens[offset].location.path, expected_path[index]) ||
            result.tokens[offset].pack_alignment != expected_pack[index] ||
            !string_equal(result.tokens[offset + 1u].spelling, "{") ||
            !string_equal(result.tokens[offset + 2u].spelling, expected_body[index]) ||
            !string_equal(result.tokens[offset + 2u].location.path, expected_path[index]) ||
            result.tokens[offset + 2u].pack_alignment != expected_pack[index] ||
            !string_equal(result.tokens[offset + 3u].spelling, "}")) {
            (void)fprintf(stderr, "cupid-exe-files: group %u differs\n", index);
            ctool_job_close(job);
            return 1;
        }
    }
    ctool_job_close(job);
    (void)printf("cupid-exe-files: ok\n");
    return 0;
}

static int run_cupid_exe_errors(void) {
    static const char *const error_text[] = {
        "#exe\n",     "#exe value\n", "#define OPEN {\n#exe OPEN\n",
        "#exe()\n",   "#exe\n{}\n",   "#exe \"file.cc\"\n",
        "#exe {\n}\n"};
    static const ctool_c_pp_mode_t error_mode[] = {
        CTOOL_C_PP_MODE_CUPID, CTOOL_C_PP_MODE_CUPID, CTOOL_C_PP_MODE_CUPID, CTOOL_C_PP_MODE_CUPID,
        CTOOL_C_PP_MODE_CUPID, CTOOL_C_PP_MODE_CUPID, CTOOL_C_PP_MODE_C11};
    static const ctool_status_t error_status[] = {
        CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT,      CTOOL_ERR_INPUT,
        CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_UNSUPPORTED};
    static const ctool_u32 error_line[] = {1u, 1u, 2u, 1u, 1u, 1u, 1u};
    static const char post_marker_error_text[] = "#define BAD(a, b) a ## b\n"
                                                 "#exe { BAD(+, *) }\n";
    static const char recovery_text[] = "#exe { recovered }\n";
    ctool_host_adapter_t adapter;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_source_t source;
    ctool_c_pp_request_t request;
    ctool_c_pp_result_t result;
    ctool_arena_mark_t mark;
    const ctool_diagnostic_t *diagnostic;
    ctool_status_t status;
    ctool_u32 case_count = (ctool_u32)(sizeof(error_text) / sizeof(error_text[0]));
    ctool_u32 index;

    if (open_job("cupid-exe-errors", &adapter, &job) != 0) {
        return 1;
    }
    source.path.text = ctool_string("/cupid-exe-errors.cc");
    (void)memset(&request, 0, sizeof(request));
    for (index = 0u; index < case_count; index++) {
        source.contents = ctool_bytes(error_text[index], (ctool_u32)strlen(error_text[index]));
        request.mode = error_mode[index];
        (void)memset(&result, 0xa5, sizeof(result));
        mark = ctool_arena_mark(ctool_job_arena(job));
        status = ctool_c_preprocess(job, &source, &request, &result);
        diagnostic = ctool_job_diagnostic(job, index);
        if (status != error_status[index] || result.tokens != NULL || result.token_count != 0u ||
            diagnostic == NULL || diagnostic->code != CTOOL_C_PP_DIAG_CUPID_EXE ||
            !string_equal(diagnostic->path, "/cupid-exe-errors.cc") ||
            diagnostic->line != error_line[index] || diagnostic->column != 1u ||
            !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
            (void)fprintf(stderr, "cupid-exe-errors: case %u differs (%s)\n", index,
                          ctool_status_name(status));
            (void)ctool_job_render_diagnostics(job);
            ctool_job_close(job);
            return 1;
        }
    }

    source.contents =
        ctool_bytes(post_marker_error_text, (ctool_u32)(sizeof(post_marker_error_text) - 1u));
    request.mode = CTOOL_C_PP_MODE_CUPID;
    (void)memset(&result, 0xa5, sizeof(result));
    mark = ctool_arena_mark(ctool_job_arena(job));
    status = ctool_c_preprocess(job, &source, &request, &result);
    diagnostic = ctool_job_diagnostic(job, case_count);
    if (status != CTOOL_ERR_INPUT || result.tokens != NULL || result.token_count != 0u ||
        diagnostic == NULL || diagnostic->code != CTOOL_C_PP_DIAG_MACRO_PASTE ||
        !string_equal(diagnostic->path, "/cupid-exe-errors.cc") || diagnostic->line != 2u ||
        diagnostic->column != 8u ||
        !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
        (void)fprintf(stderr, "cupid-exe-errors: post-marker rollback differs (%s)\n",
                      ctool_status_name(status));
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }

    source.contents = ctool_bytes(recovery_text, (ctool_u32)(sizeof(recovery_text) - 1u));
    request.mode = CTOOL_C_PP_MODE_CUPID;
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || result.token_count != 4u ||
        result.tokens[0].kind != CTOOL_C_PP_TOKEN_CUPID_EXE ||
        !string_equal(result.tokens[0].spelling, "exe") ||
        !string_equal(result.tokens[1].spelling, "{") ||
        !string_equal(result.tokens[2].spelling, "recovered") ||
        !string_equal(result.tokens[3].spelling, "}") ||
        ctool_job_diagnostic_count(job) != case_count + 1u) {
        (void)fprintf(stderr, "cupid-exe-errors: recovery differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    ctool_job_close(job);
    (void)printf("cupid-exe-errors: ok\n");
    return 0;
}

static int run_cupid_exe_scale(void) {
    enum { EXE_COUNT = 256, SOURCE_CAPACITY = 4096 };

    static char source_text[SOURCE_CAPACITY];
    ctool_host_adapter_t adapter;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_source_t source;
    ctool_c_pp_request_t request;
    ctool_c_pp_result_t result;
    ctool_status_t status;
    ctool_u32 source_size = 0u;
    ctool_u32 index;

    for (index = 0u; index < EXE_COUNT; index++) {
        if (append_text(source_text, SOURCE_CAPACITY, &source_size, "#exe {}\n") != 0) {
            return 1;
        }
    }
    if (open_job("cupid-exe-scale", &adapter, &job) != 0) {
        return 1;
    }
    source.path.text = ctool_string("/cupid-exe-scale.cc");
    source.contents = ctool_bytes(source_text, source_size);
    (void)memset(&request, 0, sizeof(request));
    request.mode = CTOOL_C_PP_MODE_CUPID;
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || result.token_count != EXE_COUNT * 3u) {
        (void)fprintf(stderr, "cupid-exe-scale: preprocessing failed (%s, %u tokens)\n",
                      ctool_status_name(status), result.token_count);
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    for (index = 0u; index < EXE_COUNT; index++) {
        ctool_u32 offset = index * 3u;
        if (result.tokens[offset].kind != CTOOL_C_PP_TOKEN_CUPID_EXE ||
            !string_equal(result.tokens[offset].spelling, "exe") ||
            result.tokens[offset].location.line != index + 1u ||
            result.tokens[offset].location.column != 1u ||
            !string_equal(result.tokens[offset + 1u].spelling, "{") ||
            !string_equal(result.tokens[offset + 2u].spelling, "}")) {
            (void)fprintf(stderr, "cupid-exe-scale: marker %u differs\n", index);
            ctool_job_close(job);
            return 1;
        }
    }
    ctool_job_close(job);
    (void)printf("cupid-exe-scale: ok\n");
    return 0;
}

static int run_conditional_expression_errors(void) {
    static const char *const error_text[] = {
        "#if defined\n#endif\n",
        "#if defined()\n#endif\n",
        "#if 1 2\n#endif\n",
        "#if (1\n#endif\n",
        "#if 08\n#endif\n",
        "#if 1.0\n#endif\n",
        "#if 0x10000000000000000\n#endif\n",
        "#if 1 / 0\n#endif\n",
        "#if 1 % 0\n#endif\n",
        "#if 1 << 64\n#endif\n",
        "#if 1 << -1\n#endif\n",
        "#if 9223372036854775807 + 1\n#endif\n",
        "#if 4611686018427387904 * 2\n#endif\n",
        "#if -(-9223372036854775807 - 1)\n#endif\n",
        "#if 1 ? 2\n#endif\n",
        "#if \"text\"\n#endif\n",
        "#define MAKE_DEFINED defined\n#if MAKE_DEFINED(NAME)\n#endif\n",
        "#if defined(__VA_ARGS__)\n#endif\n",
        "#if '\\q'\n#endif\n",
        "#define EMPTY\n#if EMPTY\n#endif\n",
        "#if 1 = 1\n#endif\n",
        "#if 1, 2\n#endif\n",
        "#if 0 && (1 + )\n#endif\n",
        "#if '\\u0001'\n#endif\n",
        "#define A 1\n#define PASS(value) value\n#if PASS(defined(A))\n#endif\n",
        "#if '\\u0041'\n#endif\n",
        "#if '\\uD800'\n#endif\n",
        "#if '\\U00110000'\n#endif\n",
        "#if L'\\x100000000'\n#endif\n",
        "#if U'\\x100000000'\n#endif\n",
        "#if u'\\x10000'\n#endif\n",
        "#if 0b10\n#endif\n",
        "#if 9223372036854775808\n#endif\n",
        "#if '\\e'\n#endif\n",
        "#if 1lL\n#endif\n",
        "#if 18446744073709551616ULL\n#endif\n",
        "#if (1, 2)\n#endif\n",
        "#if 1 ? 2, 3 : 4\n#endif\n"};
    static const ctool_status_t error_status[] = {
        CTOOL_ERR_INPUT,    CTOOL_ERR_INPUT,    CTOOL_ERR_INPUT,    CTOOL_ERR_INPUT,
        CTOOL_ERR_INPUT,    CTOOL_ERR_INPUT,    CTOOL_ERR_OVERFLOW, CTOOL_ERR_INPUT,
        CTOOL_ERR_INPUT,    CTOOL_ERR_INPUT,    CTOOL_ERR_INPUT,    CTOOL_ERR_OVERFLOW,
        CTOOL_ERR_OVERFLOW, CTOOL_ERR_OVERFLOW, CTOOL_ERR_INPUT,    CTOOL_ERR_INPUT,
        CTOOL_ERR_INPUT,    CTOOL_ERR_INPUT,    CTOOL_ERR_INPUT,    CTOOL_ERR_INPUT,
        CTOOL_ERR_INPUT,    CTOOL_ERR_INPUT,    CTOOL_ERR_INPUT,    CTOOL_ERR_INPUT,
        CTOOL_ERR_INPUT,    CTOOL_ERR_INPUT,    CTOOL_ERR_INPUT,    CTOOL_ERR_INPUT,
        CTOOL_ERR_OVERFLOW, CTOOL_ERR_OVERFLOW, CTOOL_ERR_INPUT,    CTOOL_ERR_INPUT,
        CTOOL_ERR_OVERFLOW, CTOOL_ERR_INPUT,    CTOOL_ERR_INPUT,    CTOOL_ERR_OVERFLOW,
        CTOOL_ERR_INPUT,    CTOOL_ERR_INPUT};
    static const ctool_u32 error_code[] = {
        CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION, CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
        CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION, CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
        CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION, CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
        CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION, CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
        CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION, CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
        CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION, CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
        CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION, CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
        CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION, CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
        CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION, CTOOL_C_PP_DIAG_MACRO_EXPANSION,
        CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION, CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
        CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION, CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
        CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION, CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
        CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION, CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
        CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION, CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
        CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION, CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
        CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION, CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
        CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION, CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
        CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION, CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION,
        CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION, CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION};
    static const ctool_u32 error_line[] = {1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u,
                                           1u, 1u, 1u, 2u, 1u, 1u, 2u, 1u, 1u, 1u, 1u, 3u, 1u,
                                           1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u};
    static const ctool_u32 error_column[] = {5u, 5u, 7u, 5u, 5u,  5u, 5u, 7u, 7u, 7u,  7u, 25u, 25u,
                                             5u, 7u, 5u, 5u, 13u, 5u, 1u, 7u, 6u, 15u, 5u, 10u, 5u,
                                             5u, 5u, 5u, 5u, 5u,  5u, 5u, 5u, 5u, 5u,  7u, 10u};
    static const char recovery_text[] = "#if 2 + 2 == 4\nrecovered\n#endif\n";
    ctool_host_adapter_t adapter;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_source_t source;
    ctool_c_pp_request_t request;
    ctool_c_pp_result_t result;
    ctool_arena_mark_t mark;
    const ctool_diagnostic_t *diagnostic;
    ctool_status_t status;
    ctool_u32 case_count = (ctool_u32)(sizeof(error_text) / sizeof(error_text[0]));
    ctool_u32 index;

    if (open_job("conditional-errors", &adapter, &job) != 0) {
        return 1;
    }
    source.path.text = ctool_string("/conditional-errors.c");
    (void)memset(&request, 0, sizeof(request));
    request.mode = CTOOL_C_PP_MODE_C11;
    for (index = 0u; index < case_count; index++) {
        source.contents = ctool_bytes(error_text[index], (ctool_u32)strlen(error_text[index]));
        (void)memset(&result, 0xa5, sizeof(result));
        mark = ctool_arena_mark(ctool_job_arena(job));
        status = ctool_c_preprocess(job, &source, &request, &result);
        diagnostic = ctool_job_diagnostic(job, index);
        if (status != error_status[index] || result.tokens != NULL || result.token_count != 0u ||
            diagnostic == NULL || diagnostic->code != error_code[index] ||
            !string_equal(diagnostic->path, "/conditional-errors.c") ||
            diagnostic->line != error_line[index] || diagnostic->column != error_column[index] ||
            !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
            (void)fprintf(stderr, "conditional-errors: case %u differs (%s)\n", index,
                          ctool_status_name(status));
            (void)ctool_job_render_diagnostics(job);
            ctool_job_close(job);
            return 1;
        }
    }
    source.contents = ctool_bytes(recovery_text, (ctool_u32)(sizeof(recovery_text) - 1u));
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || result.token_count != 1u ||
        !string_equal(result.tokens[0].spelling, "recovered") ||
        ctool_job_diagnostic_count(job) != case_count) {
        (void)fprintf(stderr, "conditional-errors: recovery failed\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    ctool_job_close(job);
    (void)printf("conditional-errors: ok\n");
    return 0;
}

static int run_conditional_active_cases(void) {
    typedef struct {
        const char *expression;
        ctool_u32 if_occurrences;
        ctool_u32 elif_occurrences;
        ctool_bool expected_i386;
    } conditional_case_t;

    enum { SOURCE_CAPACITY = 16 * 1024 };

    static const conditional_case_t cases[] = {
#define CUPIDC_PP_CONDITIONAL_CASE(expression, if_count, elif_count, expected)                     \
    {expression, if_count, elif_count, expected != 0 ? CTOOL_TRUE : CTOOL_FALSE},
#include "cupidc_pp_conditional_cases.inc"
#undef CUPIDC_PP_CONDITIONAL_CASE
    };
    static char source_text[SOURCE_CAPACITY];
    static const char host_text[] = "#if defined(_WIN32) && _WIN64 && _MSC_VER >= 1400 && "
                                    "defined(__SIZEOF_POINTER__) && __SIZEOF_POINTER__ == 8 && "
                                    "__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__\n"
                                    "host_profile_ok\n"
                                    "#endif\n";
    ctool_c_pp_macro_action_t actions[11];
    ctool_host_adapter_t adapter;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_source_t source;
    ctool_c_pp_request_t request;
    ctool_c_pp_result_t result;
    ctool_status_t status;
    ctool_u32 source_size = 0u;
    ctool_u32 if_occurrences = 0u;
    ctool_u32 elif_occurrences = 0u;
    ctool_u32 probe_count = 0u;
    ctool_u32 output_index = 0u;
    ctool_u32 index;
    char expected[48];

    for (index = 0u; index < (ctool_u32)(sizeof(cases) / sizeof(cases[0])); index++) {
        if_occurrences += cases[index].if_occurrences;
        elif_occurrences += cases[index].elif_occurrences;
        if (cases[index].if_occurrences != 0u) {
            if (append_text(source_text, SOURCE_CAPACITY, &source_size, "#if ") != 0 ||
                append_text(source_text, SOURCE_CAPACITY, &source_size, cases[index].expression) !=
                    0 ||
                append_text(source_text, SOURCE_CAPACITY, &source_size, "\ncase") != 0 ||
                append_decimal(source_text, SOURCE_CAPACITY, &source_size, index) != 0 ||
                append_text(source_text, SOURCE_CAPACITY, &source_size, "_if_true\n#else\ncase") !=
                    0 ||
                append_decimal(source_text, SOURCE_CAPACITY, &source_size, index) != 0 ||
                append_text(source_text, SOURCE_CAPACITY, &source_size, "_if_false\n#endif\n") !=
                    0) {
                (void)fprintf(stderr, "conditional-active: source fixture overflowed\n");
                return 1;
            }
            probe_count++;
        }
        if (cases[index].elif_occurrences != 0u) {
            if (append_text(source_text, SOURCE_CAPACITY, &source_size,
                            "#if 0\nnot_taken\n#elif ") != 0 ||
                append_text(source_text, SOURCE_CAPACITY, &source_size, cases[index].expression) !=
                    0 ||
                append_text(source_text, SOURCE_CAPACITY, &source_size, "\ncase") != 0 ||
                append_decimal(source_text, SOURCE_CAPACITY, &source_size, index) != 0 ||
                append_text(source_text, SOURCE_CAPACITY, &source_size,
                            "_elif_true\n#else\ncase") != 0 ||
                append_decimal(source_text, SOURCE_CAPACITY, &source_size, index) != 0 ||
                append_text(source_text, SOURCE_CAPACITY, &source_size, "_elif_false\n#endif\n") !=
                    0) {
                (void)fprintf(stderr, "conditional-active: source fixture overflowed\n");
                return 1;
            }
            probe_count++;
        }
    }
    if ((ctool_u32)(sizeof(cases) / sizeof(cases[0])) != 64u || if_occurrences != 493u ||
        elif_occurrences != 28u || probe_count != 67u) {
        (void)fprintf(stderr, "conditional-active: checked manifest totals differ\n");
        return 1;
    }

    if (open_job("conditional-active", &adapter, &job) != 0) {
        return 1;
    }
    actions[0].kind = CTOOL_C_PP_MACRO_DEFINE;
    actions[0].name = ctool_string("__ORDER_LITTLE_ENDIAN__");
    actions[0].replacement = ctool_string("1234");
    actions[1].kind = CTOOL_C_PP_MACRO_DEFINE;
    actions[1].name = ctool_string("__ORDER_BIG_ENDIAN__");
    actions[1].replacement = ctool_string("4321");
    actions[2].kind = CTOOL_C_PP_MACRO_DEFINE;
    actions[2].name = ctool_string("__ORDER_PDP_ENDIAN__");
    actions[2].replacement = ctool_string("3412");
    actions[3].kind = CTOOL_C_PP_MACRO_DEFINE;
    actions[3].name = ctool_string("__BYTE_ORDER__");
    actions[3].replacement = ctool_string("__ORDER_LITTLE_ENDIAN__");
    actions[4].kind = CTOOL_C_PP_MACRO_DEFINE;
    actions[4].name = ctool_string("__SIZEOF_POINTER__");
    actions[4].replacement = ctool_string("4");
    actions[5].kind = CTOOL_C_PP_MACRO_DEFINE;
    actions[5].name = ctool_string("OPL_ENABLE_STEREOEXT");
    actions[5].replacement = ctool_string("0");
    actions[6].kind = CTOOL_C_PP_MACRO_DEFINE;
    actions[6].name = ctool_string("OPL_QUIRK_CHANNELSAMPLEDELAY");
    actions[6].replacement = ctool_string("(!OPL_ENABLE_STEREOEXT)");
    actions[7].kind = CTOOL_C_PP_MACRO_DEFINE;
    actions[7].name = ctool_string("__bool_true_false_are_defined");
    actions[7].replacement = ctool_string("1");
    actions[8].kind = CTOOL_C_PP_MACRO_DEFINE;
    actions[8].name = ctool_string("ORIGCODE");
    actions[8].replacement = ctool_string("1");
    actions[9].kind = CTOOL_C_PP_MACRO_UNDEF;
    actions[9].name = ctool_string("ORIGCODE");
    actions[9].replacement = ctool_string("");
    actions[10].kind = CTOOL_C_PP_MACRO_UNDEF;
    actions[10].name = ctool_string("_WIN32");
    actions[10].replacement = ctool_string("");
    source.path.text = ctool_string("/conditional-active.c");
    source.contents = ctool_bytes(source_text, source_size);
    (void)memset(&request, 0, sizeof(request));
    request.mode = CTOOL_C_PP_MODE_C11;
    request.macro_actions = actions;
    request.macro_action_count = 11u;
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || result.token_count != probe_count) {
        (void)fprintf(stderr, "conditional-active: OS profile failed (%s, %u tokens)\n",
                      ctool_status_name(status), result.token_count);
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    for (index = 0u; index < (ctool_u32)(sizeof(cases) / sizeof(cases[0])); index++) {
        if (cases[index].if_occurrences != 0u) {
            int written = snprintf(expected, sizeof(expected), "case%u_if_%s", index,
                                   cases[index].expected_i386 == CTOOL_TRUE ? "true" : "false");
            if (written < 0 || (size_t)written >= sizeof(expected) ||
                !string_equal(result.tokens[output_index].spelling, expected) ||
                !string_equal(result.tokens[output_index].location.path, "/conditional-active.c")) {
                (void)fprintf(stderr, "conditional-active: #if case %u differs\n", index);
                ctool_job_close(job);
                return 1;
            }
            output_index++;
        }
        if (cases[index].elif_occurrences != 0u) {
            int written = snprintf(expected, sizeof(expected), "case%u_elif_%s", index,
                                   cases[index].expected_i386 == CTOOL_TRUE ? "true" : "false");
            if (written < 0 || (size_t)written >= sizeof(expected) ||
                !string_equal(result.tokens[output_index].spelling, expected) ||
                !string_equal(result.tokens[output_index].location.path, "/conditional-active.c")) {
                (void)fprintf(stderr, "conditional-active: #elif case %u differs\n", index);
                ctool_job_close(job);
                return 1;
            }
            output_index++;
        }
    }
    if (output_index != result.token_count) {
        (void)fprintf(stderr, "conditional-active: OS profile output count differs\n");
        ctool_job_close(job);
        return 1;
    }

    actions[4].replacement = ctool_string("8");
    actions[8].kind = CTOOL_C_PP_MACRO_DEFINE;
    actions[8].name = ctool_string("_WIN32");
    actions[8].replacement = ctool_string("1");
    actions[9].kind = CTOOL_C_PP_MACRO_DEFINE;
    actions[9].name = ctool_string("_WIN64");
    actions[9].replacement = ctool_string("1");
    actions[10].kind = CTOOL_C_PP_MACRO_DEFINE;
    actions[10].name = ctool_string("_MSC_VER");
    actions[10].replacement = ctool_string("1930");
    source.path.text = ctool_string("/conditional-host.c");
    source.contents = ctool_bytes(host_text, (ctool_u32)(sizeof(host_text) - 1u));
    request.hosted_environment = CTOOL_TRUE;
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || result.token_count != 1u ||
        !string_equal(result.tokens[0].spelling, "host_profile_ok") ||
        !string_equal(result.tokens[0].location.path, "/conditional-host.c") ||
        ctool_job_diagnostic_count(job) != 0u) {
        (void)fprintf(stderr, "conditional-active: hosted profile failed\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    ctool_job_close(job);
    (void)printf("conditional-active: ok\n");
    return 0;
}

static int run_conditional_expression_scale(void) {
    enum { TERM_COUNT = 10000, NESTING_LIMIT = 256, SOURCE_CAPACITY = 128 * 1024 };

    static char source_text[SOURCE_CAPACITY];
    static char limit_text[SOURCE_CAPACITY];
    ctool_host_adapter_t adapter;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_source_t source;
    ctool_c_pp_request_t request;
    ctool_c_pp_result_t result;
    ctool_arena_mark_t mark;
    const ctool_diagnostic_t *diagnostic;
    ctool_status_t status;
    ctool_u32 source_size = 0u;
    ctool_u32 limit_size = 0u;
    ctool_u32 index;

    if (append_text(source_text, SOURCE_CAPACITY, &source_size, "#if ") != 0) {
        return 1;
    }
    for (index = 0u; index < TERM_COUNT; index++) {
        if ((index != 0u &&
             append_character(source_text, SOURCE_CAPACITY, &source_size, '+') != 0) ||
            append_character(source_text, SOURCE_CAPACITY, &source_size, '1') != 0) {
            return 1;
        }
    }
    if (append_text(source_text, SOURCE_CAPACITY, &source_size, " == ") != 0 ||
        append_decimal(source_text, SOURCE_CAPACITY, &source_size, TERM_COUNT) != 0 ||
        append_text(source_text, SOURCE_CAPACITY, &source_size, "\nchain_ok\n#endif\n#if ") != 0) {
        return 1;
    }
    for (index = 0u; index < NESTING_LIMIT; index++) {
        if (append_character(source_text, SOURCE_CAPACITY, &source_size, '(') != 0) {
            return 1;
        }
    }
    if (append_character(source_text, SOURCE_CAPACITY, &source_size, '1') != 0) {
        return 1;
    }
    for (index = 0u; index < NESTING_LIMIT; index++) {
        if (append_character(source_text, SOURCE_CAPACITY, &source_size, ')') != 0) {
            return 1;
        }
    }
    if (append_text(source_text, SOURCE_CAPACITY, &source_size,
                    "\nnesting_ok\n#endif\n"
                    "#define SELF SELF\n"
                    "#if SELF\nnot_taken\n#else\nrecursion_ok\n#endif\n") != 0) {
        return 1;
    }

    if (append_text(limit_text, SOURCE_CAPACITY, &limit_size, "#if ") != 0) {
        return 1;
    }
    for (index = 0u; index < NESTING_LIMIT + 1u; index++) {
        if (append_character(limit_text, SOURCE_CAPACITY, &limit_size, '(') != 0) {
            return 1;
        }
    }
    if (append_character(limit_text, SOURCE_CAPACITY, &limit_size, '1') != 0) {
        return 1;
    }
    for (index = 0u; index < NESTING_LIMIT + 1u; index++) {
        if (append_character(limit_text, SOURCE_CAPACITY, &limit_size, ')') != 0) {
            return 1;
        }
    }
    if (append_text(limit_text, SOURCE_CAPACITY, &limit_size, "\n#endif\n") != 0) {
        return 1;
    }

    if (open_job("conditional-scale", &adapter, &job) != 0) {
        return 1;
    }
    source.path.text = ctool_string("/conditional-scale.c");
    source.contents = ctool_bytes(source_text, source_size);
    (void)memset(&request, 0, sizeof(request));
    request.mode = CTOOL_C_PP_MODE_C11;
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || result.token_count != 3u ||
        !string_equal(result.tokens[0].spelling, "chain_ok") ||
        !string_equal(result.tokens[1].spelling, "nesting_ok") ||
        !string_equal(result.tokens[2].spelling, "recursion_ok")) {
        (void)fprintf(stderr, "conditional-scale: positive scale failed (%s, %u tokens)\n",
                      ctool_status_name(status), result.token_count);
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }

    source.path.text = ctool_string("/conditional-depth.c");
    source.contents = ctool_bytes(limit_text, limit_size);
    mark = ctool_arena_mark(ctool_job_arena(job));
    status = ctool_c_preprocess(job, &source, &request, &result);
    diagnostic = ctool_job_diagnostic(job, 0u);
    if (status != CTOOL_ERR_LIMIT || result.tokens != NULL || result.token_count != 0u ||
        diagnostic == NULL || diagnostic->code != CTOOL_C_PP_DIAG_CONDITIONAL_EXPRESSION ||
        !string_equal(diagnostic->path, "/conditional-depth.c") || diagnostic->line != 1u ||
        diagnostic->column != 261u ||
        !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
        (void)fprintf(stderr, "conditional-scale: nesting limit differs (%s)\n",
                      ctool_status_name(status));
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    ctool_job_close(job);
    (void)printf("conditional-scale: ok\n");
    return 0;
}
