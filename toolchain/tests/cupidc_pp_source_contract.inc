/**
 * @file cupidc_pp_source_contract.inc
 * @brief Check source phases, locations, and predefined macros.
 */

static int run_phases(void) {
    static const char source_text[] = "int joined = 1 + \\\n"
                                      "2; // removed comment\n"
                                      "const char *s = \"/*literal*/\";\n"
                                      "name/**/suffix\n";
    static const char *const expected_text[] = {
        "int", "joined",          "=", "1",    "+",     "2", ";", "const", "char", "*", "s",
        "=",   "\"/*literal*/\"", ";", "name", "suffix"};
    static const ctool_c_pp_token_kind_t expected_kind[] = {
        CTOOL_C_PP_TOKEN_IDENTIFIER, CTOOL_C_PP_TOKEN_IDENTIFIER, CTOOL_C_PP_TOKEN_PUNCTUATOR,
        CTOOL_C_PP_TOKEN_NUMBER,     CTOOL_C_PP_TOKEN_PUNCTUATOR, CTOOL_C_PP_TOKEN_NUMBER,
        CTOOL_C_PP_TOKEN_PUNCTUATOR, CTOOL_C_PP_TOKEN_IDENTIFIER, CTOOL_C_PP_TOKEN_IDENTIFIER,
        CTOOL_C_PP_TOKEN_PUNCTUATOR, CTOOL_C_PP_TOKEN_IDENTIFIER, CTOOL_C_PP_TOKEN_PUNCTUATOR,
        CTOOL_C_PP_TOKEN_STRING,     CTOOL_C_PP_TOKEN_PUNCTUATOR, CTOOL_C_PP_TOKEN_IDENTIFIER,
        CTOOL_C_PP_TOKEN_IDENTIFIER};
    ctool_host_adapter_t adapter;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_source_t source;
    ctool_c_pp_request_t request;
    ctool_c_pp_result_t result;
    ctool_status_t status;
    ctool_u32 index;

    if (open_job("phases", &adapter, &job) != 0) {
        return 1;
    }

    source.path.text = ctool_string("/phase.c");
    source.contents = ctool_bytes(source_text, (ctool_u32)(sizeof(source_text) - 1u));
    (void)memset(&request, 0, sizeof(request));
    request.mode = CTOOL_C_PP_MODE_C11;
    (void)memset(&result, 0xa5, sizeof(result));

    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK ||
        result.token_count != (ctool_u32)(sizeof(expected_text) / sizeof(expected_text[0]))) {
        (void)fprintf(stderr, "phases: preprocessing failed (%s, %u tokens)\n",
                      ctool_status_name(status), result.token_count);
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }

    for (index = 0u; index < result.token_count; index++) {
        if (result.tokens[index].kind != expected_kind[index] ||
            !string_equal(result.tokens[index].spelling, expected_text[index]) ||
            !string_equal(result.tokens[index].location.path, "/phase.c") ||
            result.tokens[index].pack_alignment != 0u) {
            (void)fprintf(stderr, "phases: token %u differs\n", index);
            ctool_job_close(job);
            return 1;
        }
    }

    if (result.tokens[5].location.line != 2u || result.tokens[5].location.column != 1u ||
        result.tokens[14].location.line != 4u || result.tokens[14].location.column != 1u ||
        result.tokens[15].location.line != 4u || result.tokens[15].location.column != 9u) {
        (void)fprintf(stderr, "phases: source locations differ\n");
        ctool_job_close(job);
        return 1;
    }

    ctool_job_close(job);
    (void)printf("phases: ok\n");
    return 0;
}

static int run_tokens(void) {
    static char source_text[] = "L'a' u8\"wide\" .5e+2 0x1p-2 >>= <<= ... <: :> <% %> -> "
                                "hash # ## %: %:%:\n";
    static char source_path[] = "/tokens.c";
    static const char *const expected_text[] = {
        "L'a'", "u8\"wide\"", ".5e+2", "0x1p-2", ">>=", "<<=", "...", "<:",  ":>",
        "<%",   "%>",         "->",    "hash",   "#",   "##",  "%:",  "%:%:"};
    static const ctool_c_pp_token_kind_t expected_kind[] = {
        CTOOL_C_PP_TOKEN_CHARACTER,  CTOOL_C_PP_TOKEN_STRING,     CTOOL_C_PP_TOKEN_NUMBER,
        CTOOL_C_PP_TOKEN_NUMBER,     CTOOL_C_PP_TOKEN_PUNCTUATOR, CTOOL_C_PP_TOKEN_PUNCTUATOR,
        CTOOL_C_PP_TOKEN_PUNCTUATOR, CTOOL_C_PP_TOKEN_PUNCTUATOR, CTOOL_C_PP_TOKEN_PUNCTUATOR,
        CTOOL_C_PP_TOKEN_PUNCTUATOR, CTOOL_C_PP_TOKEN_PUNCTUATOR, CTOOL_C_PP_TOKEN_PUNCTUATOR,
        CTOOL_C_PP_TOKEN_IDENTIFIER, CTOOL_C_PP_TOKEN_PUNCTUATOR, CTOOL_C_PP_TOKEN_PUNCTUATOR,
        CTOOL_C_PP_TOKEN_PUNCTUATOR, CTOOL_C_PP_TOKEN_PUNCTUATOR};
    ctool_host_adapter_t adapter;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_source_t source;
    ctool_c_pp_request_t request;
    ctool_c_pp_result_t result;
    ctool_status_t status;
    ctool_u32 index;

    if (open_job("tokens", &adapter, &job) != 0) {
        return 1;
    }
    source.path.text.data = source_path;
    source.path.text.size = (ctool_u32)(sizeof(source_path) - 1u);
    source.contents = ctool_bytes(source_text, (ctool_u32)(sizeof(source_text) - 1u));
    (void)memset(&request, 0, sizeof(request));
    request.mode = CTOOL_C_PP_MODE_C11;

    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK ||
        result.token_count != (ctool_u32)(sizeof(expected_text) / sizeof(expected_text[0]))) {
        (void)fprintf(stderr, "tokens: preprocessing failed\n");
        ctool_job_close(job);
        return 1;
    }
    for (index = 0u; index < result.token_count; index++) {
        if (result.tokens[index].kind != expected_kind[index] ||
            !string_equal(result.tokens[index].spelling, expected_text[index])) {
            (void)fprintf(stderr, "tokens: token %u differs\n", index);
            ctool_job_close(job);
            return 1;
        }
    }
    source_text[0] = 'X';
    source_path[1] = 'X';
    if (!string_equal(result.tokens[0].spelling, "L'a'") ||
        !string_equal(result.tokens[0].location.path, "/tokens.c")) {
        (void)fprintf(stderr, "tokens: result retained a borrowed source view\n");
        ctool_job_close(job);
        return 1;
    }
    source_text[0] = 'L';
    source_path[1] = 't';

    source.path.text = ctool_string("/empty.c");
    source.contents = ctool_bytes(NULL, 0u);
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || result.tokens != NULL || result.token_count != 0u) {
        (void)fprintf(stderr, "tokens: empty input differs\n");
        ctool_job_close(job);
        return 1;
    }
    ctool_job_close(job);
    (void)printf("tokens: ok\n");
    return 0;
}

static int run_errors(void) {
    static const char invalid_text[] = "int retained;\n\"unterminated\n";
    static const char valid_text[] = "joined\\\r\n_name\n";
    ctool_host_adapter_t adapter;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_source_t source;
    ctool_c_pp_request_t request;
    ctool_c_pp_result_t result;
    ctool_arena_mark_t mark;
    const ctool_diagnostic_t *diagnostic;
    ctool_status_t status;

    if (open_job("errors", &adapter, &job) != 0) {
        return 1;
    }

    source.path.text = ctool_string("/invalid.c");
    source.contents = ctool_bytes(invalid_text, (ctool_u32)(sizeof(invalid_text) - 1u));
    (void)memset(&request, 0, sizeof(request));
    request.mode = CTOOL_C_PP_MODE_C11;
    (void)memset(&result, 0xa5, sizeof(result));
    mark = ctool_arena_mark(ctool_job_arena(job));

    status = ctool_c_preprocess(job, &source, &request, &result);
    diagnostic = ctool_job_diagnostic(job, 0u);
    if (status != CTOOL_ERR_INPUT || result.tokens != NULL || result.token_count != 0u ||
        ctool_job_diagnostic_count(job) != 1u || diagnostic == NULL ||
        diagnostic->severity != CTOOL_DIAG_ERROR || diagnostic->code != CTOOL_C_PP_DIAG_LEXICAL ||
        !string_equal(diagnostic->path, "/invalid.c") || diagnostic->line != 2u ||
        diagnostic->column != 1u ||
        !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
        (void)fprintf(stderr, "errors: lexical failure was not transactional\n");
        ctool_job_close(job);
        return 1;
    }

    source.path.text = ctool_string("/valid.c");
    source.contents = ctool_bytes(valid_text, (ctool_u32)(sizeof(valid_text) - 1u));
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || result.token_count != 1u || result.tokens == NULL ||
        !string_equal(result.tokens[0].spelling, "joined_name") ||
        result.tokens[0].location.line != 1u || result.tokens[0].location.column != 1u ||
        ctool_job_diagnostic_count(job) != 1u) {
        (void)fprintf(stderr, "errors: recovery preprocessing failed\n");
        ctool_job_close(job);
        return 1;
    }

    ctool_job_close(job);
    (void)printf("errors: ok\n");
    return 0;
}

static int run_unsupported(void) {
    static const char *const unsupported_text[] = {"#pragma cupid_vendor_extension\n"};
    static const ctool_u32 unsupported_code[] = {CTOOL_C_PP_DIAG_DIRECTIVE};
    static const ctool_u32 unsupported_line[] = {1u};
    ctool_host_adapter_t adapter;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_source_t source;
    ctool_c_pp_request_t request;
    ctool_c_pp_result_t result;
    ctool_arena_mark_t mark;
    const ctool_diagnostic_t *diagnostic;
    ctool_status_t status;
    ctool_u32 index;

    if (open_job("unsupported", &adapter, &job) != 0) {
        return 1;
    }
    (void)memset(&request, 0, sizeof(request));
    request.mode = CTOOL_C_PP_MODE_C11;
    source.path.text = ctool_string("/builtin.c");
    for (index = 0u; index < (ctool_u32)(sizeof(unsupported_text) / sizeof(unsupported_text[0]));
         index++) {
        source.contents =
            ctool_bytes(unsupported_text[index], (ctool_u32)strlen(unsupported_text[index]));
        mark = ctool_arena_mark(ctool_job_arena(job));
        status = ctool_c_preprocess(job, &source, &request, &result);
        diagnostic = ctool_job_diagnostic(job, index);
        if (status != CTOOL_ERR_UNSUPPORTED || result.tokens != NULL || result.token_count != 0u ||
            diagnostic == NULL || diagnostic->code != unsupported_code[index] ||
            diagnostic->line != unsupported_line[index] || diagnostic->column != 1u ||
            !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
            (void)fprintf(stderr, "unsupported: table case %u did not fail explicitly\n", index);
            ctool_job_close(job);
            return 1;
        }
    }
    ctool_job_close(job);
    (void)printf("unsupported: ok\n");
    return 0;
}

static int run_line_directives(void) {
    static char source_text[] = "before __FILE__ __LINE__\n"
                                "#line 100 \"virtual.c\"\n"
                                "mapped __FILE__ __LINE__\n"
                                "#line 000010\n"
                                "retained __FILE__ __LINE__\n"
                                "#define NUMBER 200\n"
                                "#define NAME \"macro.c\"\n"
                                "#define BOTH(value, name) value name\n"
                                "#line BOTH(NUMBER, NAME)\n"
                                "macro __FILE__ __LINE__\n"
                                "%:line 300 \"\"\n"
                                "empty __FILE__ __LINE__\n"
                                "#if 0\n"
                                "#line 0 L\"bad\"\n"
                                "#endif\n"
                                "inactive_ok __FILE__ __LINE__\n"
                                "#define CURRENT __LINE__ __FILE__\n"
                                "#line CURRENT\n"
                                "current __FILE__ __LINE__\n"
                                "#li\\\n"
                                "ne \\\n"
                                "400 \\\n"
                                "\"splice.c\"\n"
                                "spliced __FILE__ __LINE__\n"
                                "ordinary \\\n"
                                "continued __LINE__\n"
                                "#line 2147483647 \"max.c\"\n"
                                "maximum __LINE__\n"
                                "after_max __LINE__\n";
    static const char cupid_text[] = "#line 50 \"cupid-v.cc\"\n"
                                     "#exe { body __FILE__ __LINE__ }\n";
    static const char escaped_text[] = "#line 9 \"esc\\101\\x42\\n\\t\\\\\\\"\\u03b1\\0end\"\n"
                                       "escaped __FILE__ __LINE__\n";
    static const char inactive_marker_text[] = "#if 0\n"
                                               "# 1 \"ignored-marker.c\"\n"
                                               "#endif\n"
                                               "alive\n";
    static const char expected_escaped_path[] = {'e',         's',  'c',  'A', 'B',
                                                 '\n',        '\t', '\\', '"', (char)0xceu,
                                                 (char)0xb1u, '\0', 'e',  'n', 'd'};
    static const char expected_escaped_spelling[] = {
        '"', 'e',  's', 'c', 'A', 'B',  '\\', '0', '1', '2',  '\\', '0', '1', '1', '\\', '\\', '\\',
        '"', '\\', '3', '1', '6', '\\', '2',  '6', '1', '\\', '0',  '0', '0', 'e', 'n',  'd',  '"'};
    static const char *const expected_spelling[] = {"before",
                                                    "\"/line/main.c\"",
                                                    "1",
                                                    "mapped",
                                                    "\"virtual.c\"",
                                                    "100",
                                                    "retained",
                                                    "\"virtual.c\"",
                                                    "10",
                                                    "macro",
                                                    "\"macro.c\"",
                                                    "200",
                                                    "empty",
                                                    "\"\"",
                                                    "300",
                                                    "inactive_ok",
                                                    "\"\"",
                                                    "304",
                                                    "current",
                                                    "\"\"",
                                                    "306",
                                                    "spliced",
                                                    "\"splice.c\"",
                                                    "400",
                                                    "ordinary",
                                                    "continued",
                                                    "402",
                                                    "maximum",
                                                    "2147483647",
                                                    "after_max",
                                                    "2147483648"};
    static const char *const expected_path[] = {"/line/main.c",
                                                "/line/main.c",
                                                "/line/main.c",
                                                "virtual.c",
                                                "virtual.c",
                                                "virtual.c",
                                                "virtual.c",
                                                "virtual.c",
                                                "virtual.c",
                                                "macro.c",
                                                "macro.c",
                                                "macro.c",
                                                "",
                                                "",
                                                "",
                                                "",
                                                "",
                                                "",
                                                "",
                                                "",
                                                "",
                                                "splice.c",
                                                "splice.c",
                                                "splice.c",
                                                "splice.c",
                                                "splice.c",
                                                "splice.c",
                                                "max.c",
                                                "max.c",
                                                "max.c",
                                                "max.c"};
    static const ctool_u32 expected_line[] = {
        1u,   1u,   1u,   100u, 100u, 100u,        10u,         10u,         10u,        200u, 200u,
        200u, 300u, 300u, 300u, 304u, 304u,        304u,        306u,        306u,       306u, 400u,
        400u, 400u, 401u, 402u, 402u, 2147483647u, 2147483647u, 2147483648u, 2147483648u};
    static const ctool_u32 expected_physical_line[] = {
        1u,  1u,  1u,  3u,  3u,  3u,  5u,  5u,  5u,  10u, 10u, 10u, 12u, 12u, 12u, 16u,
        16u, 16u, 19u, 19u, 19u, 24u, 24u, 24u, 25u, 26u, 26u, 28u, 28u, 29u, 29u};
    ctool_host_adapter_t adapter;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_source_t source;
    ctool_c_pp_request_t request;
    ctool_c_pp_result_t result;
    ctool_status_t status;
    ctool_u32 index;

    if (open_job("line-directives", &adapter, &job) != 0) {
        return 1;
    }
    source.path.text = ctool_string("/line/main.c");
    source.contents = ctool_bytes(source_text, (ctool_u32)(sizeof(source_text) - 1u));
    (void)memset(&request, 0, sizeof(request));
    request.mode = CTOOL_C_PP_MODE_C11;
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK ||
        result.token_count !=
            (ctool_u32)(sizeof(expected_spelling) / sizeof(expected_spelling[0])) ||
        ctool_job_diagnostic_count(job) != 0u) {
        (void)fprintf(stderr, "line-directives: C11 preprocessing failed (%s, %u tokens)\n",
                      ctool_status_name(status), result.token_count);
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    for (index = 0u; index < result.token_count; index++) {
        if (!string_equal(result.tokens[index].spelling, expected_spelling[index]) ||
            !token_locations_equal(&result.tokens[index], expected_path[index],
                                   expected_line[index], "/line/main.c",
                                   expected_physical_line[index])) {
            (void)fprintf(stderr, "line-directives: token/location %u differs\n", index);
            ctool_job_close(job);
            return 1;
        }
    }

    source.path.text = ctool_string("/line/cupid.cc");
    source.contents = ctool_bytes(cupid_text, (ctool_u32)(sizeof(cupid_text) - 1u));
    request.mode = CTOOL_C_PP_MODE_CUPID;
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || result.token_count != 6u ||
        result.tokens[0].kind != CTOOL_C_PP_TOKEN_CUPID_EXE ||
        !string_equal(result.tokens[0].spelling, "exe") ||
        !string_equal(result.tokens[1].spelling, "{") ||
        !string_equal(result.tokens[2].spelling, "body") ||
        !string_equal(result.tokens[3].spelling, "\"cupid-v.cc\"") ||
        !string_equal(result.tokens[4].spelling, "50") ||
        !string_equal(result.tokens[5].spelling, "}") || ctool_job_diagnostic_count(job) != 0u) {
        (void)fprintf(stderr, "line-directives: Cupid #exe mapping differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    for (index = 0u; index < result.token_count; index++) {
        if (!token_locations_equal(&result.tokens[index], "cupid-v.cc", 50u, "/line/cupid.cc",
                                   2u)) {
            (void)fprintf(stderr, "line-directives: Cupid token %u location differs\n", index);
            ctool_job_close(job);
            return 1;
        }
    }

    source.path.text = ctool_string("/line/escaped.c");
    source.contents = ctool_bytes(escaped_text, (ctool_u32)(sizeof(escaped_text) - 1u));
    request.mode = CTOOL_C_PP_MODE_C11;
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || result.token_count != 3u ||
        !string_equal(result.tokens[0].spelling, "escaped") ||
        !strings_equal(result.tokens[1].spelling,
                       (ctool_string_t){expected_escaped_spelling,
                                        (ctool_u32)sizeof(expected_escaped_spelling)}) ||
        !string_equal(result.tokens[2].spelling, "9") ||
        !strings_equal(
            result.tokens[0].location.path,
            (ctool_string_t){expected_escaped_path, (ctool_u32)sizeof(expected_escaped_path)}) ||
        !string_equal(result.tokens[0].physical_location.path, "/line/escaped.c") ||
        result.tokens[0].location.line != 9u || result.tokens[0].physical_location.line != 2u ||
        ctool_job_diagnostic_count(job) != 0u) {
        (void)fprintf(stderr, "line-directives: escaped counted filename differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }

    source.path.text = ctool_string("/line/inactive-marker.c");
    source.contents =
        ctool_bytes(inactive_marker_text, (ctool_u32)(sizeof(inactive_marker_text) - 1u));
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || result.token_count != 1u ||
        !string_equal(result.tokens[0].spelling, "alive") ||
        !token_locations_equal(&result.tokens[0], "/line/inactive-marker.c", 4u,
                               "/line/inactive-marker.c", 4u) ||
        ctool_job_diagnostic_count(job) != 0u) {
        (void)fprintf(stderr, "line-directives: inactive GNU marker was not ignored\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    ctool_job_close(job);
    (void)printf("line-directives: ok\n");
    return 0;
}

static int run_line_errors(void) {
    typedef struct {
        const char *text;
        ctool_status_t status;
        ctool_u32 code;
        const char *path;
        ctool_u32 line;
    } line_error_case_t;

    static const line_error_case_t cases[] = {
        {"#line\n", CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_LINE, "/line-errors.c", 1u},
        {"#line 0\n", CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_LINE, "/line-errors.c", 1u},
        {"#line 2147483648\n", CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_LINE, "/line-errors.c", 1u},
        {"#line +1\n", CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_LINE, "/line-errors.c", 1u},
        {"#line 0x10\n", CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_LINE, "/line-errors.c", 1u},
        {"#line 1U\n", CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_LINE, "/line-errors.c", 1u},
        {"#line 1.0\n", CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_LINE, "/line-errors.c", 1u},
        {"#line 1 L\"wide.c\"\n", CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_LINE, "/line-errors.c", 1u},
        {"#line 1 \"a.c\" \"b.c\"\n", CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_LINE, "/line-errors.c", 1u},
        {"#define EMPTY\n#line EMPTY\n", CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_LINE, "/line-errors.c",
         2u},
        {"#define BAD 1 extra\n#line BAD\n", CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_LINE,
         "/line-errors.c", 2u},
        {"#line 1 \"bad\\q.c\"\n", CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_LINE, "/line-errors.c", 1u},
        {"#line 1 \"bad\\uD800.c\"\n", CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_LINE, "/line-errors.c", 1u},
        {"# 10 \"gnu-marker.c\"\n", CTOOL_ERR_UNSUPPORTED, CTOOL_C_PP_DIAG_LINE, "/line-errors.c",
         1u},
        {"#line 50 \"before-invalid.c\"\n#line 0\n", CTOOL_ERR_INPUT, CTOOL_C_PP_DIAG_LINE,
         "before-invalid.c", 50u},
        {"#line 50 \"condition-name.c\"\n#ifdef __VA_ARGS__\n#endif\n", CTOOL_ERR_INPUT,
         CTOOL_C_PP_DIAG_MACRO_EXPANSION, "condition-name.c", 50u},
        {"#line 10 \"\"\n#error empty presumed source name\n", CTOOL_ERR_INPUT,
         CTOOL_C_PP_DIAG_ERROR_DIRECTIVE, "", 10u},
        {"#line 70 \"virtual-diag.c\"\n#include \"missing.h\"\n", CTOOL_ERR_NOT_FOUND,
         CTOOL_C_PP_DIAG_INCLUDE_NOT_FOUND, "virtual-diag.c", 70u}};
    static const char recovery_text[] = "#line 5 \"recovered.c\"\n"
                                        "recovered __FILE__ __LINE__\n";
    ctool_host_adapter_t adapter;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_source_t source;
    ctool_c_pp_request_t request;
    ctool_c_pp_result_t result;
    ctool_arena_mark_t mark;
    const ctool_diagnostic_t *diagnostic;
    ctool_status_t status;
    ctool_u32 index;

    if (open_job("line-errors", &adapter, &job) != 0) {
        return 1;
    }
    source.path.text = ctool_string("/line-errors.c");
    (void)memset(&request, 0, sizeof(request));
    request.mode = CTOOL_C_PP_MODE_C11;
    for (index = 0u; index < (ctool_u32)(sizeof(cases) / sizeof(cases[0])); index++) {
        source.contents = ctool_bytes(cases[index].text, (ctool_u32)strlen(cases[index].text));
        mark = ctool_arena_mark(ctool_job_arena(job));
        status = ctool_c_preprocess(job, &source, &request, &result);
        diagnostic = ctool_job_diagnostic(job, index);
        if (status != cases[index].status || result.tokens != NULL || result.token_count != 0u ||
            diagnostic == NULL || diagnostic->code != cases[index].code ||
            !string_equal(diagnostic->path, cases[index].path) ||
            diagnostic->line != cases[index].line || diagnostic->column == 0u ||
            !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
            (void)fprintf(stderr, "line-errors: case %u differs\n", index);
            (void)ctool_job_render_diagnostics(job);
            ctool_job_close(job);
            return 1;
        }
    }

    source.contents = ctool_bytes(recovery_text, (ctool_u32)(sizeof(recovery_text) - 1u));
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || result.token_count != 3u ||
        !string_equal(result.tokens[0].spelling, "recovered") ||
        !string_equal(result.tokens[1].spelling, "\"recovered.c\"") ||
        !string_equal(result.tokens[2].spelling, "5") ||
        !token_locations_equal(&result.tokens[0], "recovered.c", 5u, "/line-errors.c", 2u) ||
        ctool_job_diagnostic_count(job) != (ctool_u32)(sizeof(cases) / sizeof(cases[0]))) {
        (void)fprintf(stderr, "line-errors: recovery differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    ctool_job_close(job);
    (void)printf("line-errors: ok\n");
    return 0;
}

static int run_line_files(void) {
    static char forced_text[] = "#line 40 \"forced-v.h\"\n"
                                "forced __FILE__ __LINE__\n";
    static char header_text[] = "#line 70 \"header-v.h\"\n"
                                "header __FILE__ __LINE__\n"
                                "#define HEADER_SITE __FILE__ __LINE__\n"
                                "#include \"sibling.h\"\n"
                                "after_header __FILE__ __LINE__\n";
    static char sibling_text[] = "sibling __FILE__ __LINE__\n";
    static char replay_text[] = "#line REPLAY_LINE REPLAY_NAME\n"
                                "replay __FILE__ __LINE__\n";
    static char once_one_text[] = "#pragma once\n"
                                  "#line 500 \"shared-v.h\"\n"
                                  "#pragma pack(push, 1)\n"
                                  "once_one __FILE__ __LINE__\n"
                                  "#pragma pack(pop)\n";
    static char once_two_text[] = "#pragma once\n"
                                  "#line 500 \"shared-v.h\"\n"
                                  "#pragma pack(push, 1)\n"
                                  "once_two __FILE__ __LINE__\n"
                                  "#pragma pack(pop)\n";
    static const char source_text[] = "#line 100 \"parent-v.c\"\n"
                                      "#include \"dir/header.h\"\n"
                                      "parent HEADER_SITE __FILE__ __LINE__\n"
                                      "#define REPLAY_LINE 200\n"
                                      "#define REPLAY_NAME \"replay-a.h\"\n"
                                      "#include \"dir/replay.h\"\n"
                                      "#undef REPLAY_LINE\n"
                                      "#undef REPLAY_NAME\n"
                                      "#define REPLAY_LINE 300\n"
                                      "#define REPLAY_NAME \"replay-b.h\"\n"
                                      "#include \"dir/replay.h\"\n"
                                      "#include \"dir/once-one.h\"\n"
                                      "#include \"dir/once-one.h\"\n"
                                      "#include \"dir/once-two.h\"\n"
                                      "after_parent __FILE__ __LINE__\n";
    static const char *const expected_spelling[] = {"forced",
                                                    "\"forced-v.h\"",
                                                    "40",
                                                    "header",
                                                    "\"header-v.h\"",
                                                    "70",
                                                    "sibling",
                                                    "\"/line/dir/sibling.h\"",
                                                    "1",
                                                    "after_header",
                                                    "\"header-v.h\"",
                                                    "73",
                                                    "parent",
                                                    "\"parent-v.c\"",
                                                    "101",
                                                    "\"parent-v.c\"",
                                                    "101",
                                                    "replay",
                                                    "\"replay-a.h\"",
                                                    "200",
                                                    "replay",
                                                    "\"replay-b.h\"",
                                                    "300",
                                                    "once_one",
                                                    "\"shared-v.h\"",
                                                    "501",
                                                    "once_two",
                                                    "\"shared-v.h\"",
                                                    "501",
                                                    "after_parent",
                                                    "\"parent-v.c\"",
                                                    "113"};
    fixture_file_t files[6];
    fixture_store_t store;
    ctool_host_adapter_t adapter;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_source_t source;
    ctool_path_t forced;
    ctool_c_pp_request_t request;
    ctool_c_pp_result_t result;
    ctool_status_t status;
    ctool_u32 index;

    files[0].path = ctool_string("/line/forced.h");
    files[0].contents = ctool_bytes(forced_text, (ctool_u32)(sizeof(forced_text) - 1u));
    files[1].path = ctool_string("/line/dir/header.h");
    files[1].contents = ctool_bytes(header_text, (ctool_u32)(sizeof(header_text) - 1u));
    files[2].path = ctool_string("/line/dir/sibling.h");
    files[2].contents = ctool_bytes(sibling_text, (ctool_u32)(sizeof(sibling_text) - 1u));
    files[3].path = ctool_string("/line/dir/replay.h");
    files[3].contents = ctool_bytes(replay_text, (ctool_u32)(sizeof(replay_text) - 1u));
    files[4].path = ctool_string("/line/dir/once-one.h");
    files[4].contents = ctool_bytes(once_one_text, (ctool_u32)(sizeof(once_one_text) - 1u));
    files[5].path = ctool_string("/line/dir/once-two.h");
    files[5].contents = ctool_bytes(once_two_text, (ctool_u32)(sizeof(once_two_text) - 1u));
    store.files = files;
    store.file_count = 6u;
    store.read_count = 0u;
    if (open_fixture_job("line-files", &store, &adapter, &job) != 0) {
        return 1;
    }
    source.path.text = ctool_string("/line/main.c");
    source.contents = ctool_bytes(source_text, (ctool_u32)(sizeof(source_text) - 1u));
    forced.text = ctool_string("/line/forced.h");
    (void)memset(&request, 0, sizeof(request));
    request.mode = CTOOL_C_PP_MODE_C11;
    request.forced_includes = &forced;
    request.forced_include_count = 1u;
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || store.read_count != 6u ||
        result.token_count !=
            (ctool_u32)(sizeof(expected_spelling) / sizeof(expected_spelling[0])) ||
        ctool_job_diagnostic_count(job) != 0u) {
        (void)fprintf(stderr, "line-files: preprocessing failed (%s, %u tokens, %u reads)\n",
                      ctool_status_name(status), result.token_count, store.read_count);
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    for (index = 0u; index < result.token_count; index++) {
        if (!string_equal(result.tokens[index].spelling, expected_spelling[index])) {
            (void)fprintf(stderr, "line-files: token %u differs\n", index);
            ctool_job_close(job);
            return 1;
        }
    }
    for (index = 0u; index < 3u; index++) {
        if (!token_locations_equal(&result.tokens[index], "forced-v.h", 40u, "/line/forced.h",
                                   2u)) {
            (void)fprintf(stderr, "line-files: forced location differs\n");
            ctool_job_close(job);
            return 1;
        }
    }
    for (index = 3u; index < 6u; index++) {
        if (!token_locations_equal(&result.tokens[index], "header-v.h", 70u, "/line/dir/header.h",
                                   2u)) {
            (void)fprintf(stderr, "line-files: header location differs\n");
            ctool_job_close(job);
            return 1;
        }
    }
    for (index = 6u; index < 9u; index++) {
        if (!token_locations_equal(&result.tokens[index], "/line/dir/sibling.h", 1u,
                                   "/line/dir/sibling.h", 1u)) {
            (void)fprintf(stderr, "line-files: sibling location differs\n");
            ctool_job_close(job);
            return 1;
        }
    }
    for (index = 9u; index < 12u; index++) {
        if (!token_locations_equal(&result.tokens[index], "header-v.h", 73u, "/line/dir/header.h",
                                   5u)) {
            (void)fprintf(stderr, "line-files: restored header differs\n");
            ctool_job_close(job);
            return 1;
        }
    }
    for (index = 12u; index < 17u; index++) {
        if (!token_locations_equal(&result.tokens[index], "parent-v.c", 101u, "/line/main.c", 3u)) {
            (void)fprintf(stderr, "line-files: parent location differs\n");
            ctool_job_close(job);
            return 1;
        }
    }
    for (index = 17u; index < 20u; index++) {
        if (!token_locations_equal(&result.tokens[index], "replay-a.h", 200u, "/line/dir/replay.h",
                                   2u)) {
            (void)fprintf(stderr, "line-files: first replay differs\n");
            ctool_job_close(job);
            return 1;
        }
    }
    for (index = 20u; index < 23u; index++) {
        if (!token_locations_equal(&result.tokens[index], "replay-b.h", 300u, "/line/dir/replay.h",
                                   2u)) {
            (void)fprintf(stderr, "line-files: second replay differs\n");
            ctool_job_close(job);
            return 1;
        }
    }
    for (index = 23u; index < 29u; index++) {
        const char *physical = index < 26u ? "/line/dir/once-one.h" : "/line/dir/once-two.h";
        if (!token_locations_equal(&result.tokens[index], "shared-v.h", 501u, physical, 4u) ||
            result.tokens[index].pack_alignment != 1u) {
            (void)fprintf(stderr, "line-files: once/pack location differs\n");
            ctool_job_close(job);
            return 1;
        }
    }
    for (index = 29u; index < 32u; index++) {
        if (!token_locations_equal(&result.tokens[index], "parent-v.c", 113u, "/line/main.c",
                                   15u)) {
            (void)fprintf(stderr, "line-files: final parent location differs\n");
            ctool_job_close(job);
            return 1;
        }
    }
    ctool_job_close(job);
    (void)printf("line-files: ok\n");
    return 0;
}

static int run_line_scale(void) {
    enum { ALIAS_COUNT = 32, REPEAT_COUNT = 32768 };

    static const char dispatcher_text[] = "#line p000\n";
    static char alias_names[ALIAS_COUNT][4];
    static ctool_c_pp_macro_action_t actions[ALIAS_COUNT];
    static ctool_path_t forced[REPEAT_COUNT];
    fixture_file_t file;
    fixture_store_t store;
    ctool_host_adapter_t adapter;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_job_config_t config;
    ctool_limits_t limits;
    ctool_source_t source;
    ctool_c_pp_request_t request;
    ctool_c_pp_result_t result;
    ctool_status_t status;
    ctool_u32 index;

    for (index = 0u; index < ALIAS_COUNT; index++) {
        ctool_u32 name_size = 0u;
        if (append_parameter_name(alias_names[index], 4u, &name_size, index) != 0 ||
            name_size != 4u) {
            return 1;
        }
        actions[index].kind = CTOOL_C_PP_MACRO_DEFINE;
        actions[index].name.data = alias_names[index];
        actions[index].name.size = 4u;
        if (index + 1u < ALIAS_COUNT) {
            actions[index].replacement.data = alias_names[index + 1u];
            actions[index].replacement.size = 4u;
        } else {
            actions[index].replacement = ctool_string("7");
        }
    }
    file.path = ctool_string("/line-dispatcher.h");
    file.contents = ctool_bytes(dispatcher_text, (ctool_u32)(sizeof(dispatcher_text) - 1u));
    store.files = &file;
    store.file_count = 1u;
    store.read_count = 0u;
    for (index = 0u; index < REPEAT_COUNT; index++) {
        forced[index].text = ctool_string("/line-dispatcher.h");
    }
    status = ctool_host_adapter_init(&adapter, ".");
    if (status != CTOOL_OK) {
        (void)fprintf(stderr, "line-scale: host adapter failed (%s)\n", ctool_status_name(status));
        return 1;
    }
    limits = ctool_default_limits();
    limits.arena_block_bytes = 4096u;
    limits.arena_bytes = 512u * 1024u;
    config = ctool_host_job_config(&adapter, limits);
    config.files.context = &store;
    config.files.file_size = fixture_file_size;
    config.files.read_exact = fixture_read_exact;
    config.files.write_all = fixture_write_all;
    status = ctool_job_open(&config, &job);
    if (status != CTOOL_OK) {
        (void)fprintf(stderr, "line-scale: job open failed (%s)\n", ctool_status_name(status));
        return 1;
    }
    source.path.text = ctool_string("/line-scale.c");
    source.contents = ctool_bytes("", 0u);
    (void)memset(&request, 0, sizeof(request));
    request.mode = CTOOL_C_PP_MODE_C11;
    request.forced_includes = forced;
    request.forced_include_count = REPEAT_COUNT;
    request.macro_actions = actions;
    request.macro_action_count = ALIAS_COUNT;
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || result.token_count != 0u || store.read_count != 1u ||
        ctool_job_diagnostic_count(job) != 0u) {
        (void)fprintf(stderr, "line-scale: scratch/frame reuse differs (%s, %u reads)\n",
                      ctool_status_name(status), store.read_count);
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    ctool_job_close(job);
    (void)printf("line-scale: ok\n");
    return 0;
}

static int run_conditional_expressions(void) {
    static const char source_text[] =
        "ordinary_defined defined follower\n"
        "#define ONE 1\n"
        "#define VALUE (ONE + 1)\n"
        "#define TWICE(value) ((value) * 2)\n"
        "#define ALIAS ONE\n"
        "#define IGNORE(value) 1\n"
        "#if defined(ONE) && defined ALIAS && !defined(MISSING)\n"
        "defined_ok\n"
        "#endif\n"
        "#if IGNORE(defined())\n"
        "unused_defined_ok\n"
        "#endif\n"
        "#if UNKNOWN == 0 && VALUE == 2 && TWICE(3) == 6\n"
        "macro_ok\n"
        "#endif\n"
        "#if 1 + 2 * 3 == 7 && 20 / 4 == 5 && 20 % 6 == 2\n"
        "arithmetic_ok\n"
        "#endif\n"
        "#if (1 << 5) == 32 && (32 >> 3) == 4\n"
        "shifts_ok\n"
        "#endif\n"
        "#if 3 < 4 && 4 <= 4 && 5 > 4 && 5 >= 5 && 6 != 7\n"
        "relations_ok\n"
        "#endif\n"
        "#if ((0xf0U & 0x0fU) == 0U) && ((1U | 2U) == 3U) && "
        "((7U ^ 3U) == 4U) && (~0U != 0U)\n"
        "bitwise_ok\n"
        "#endif\n"
        "#if +1 == 1 && -1 < 0 && !0 && !!1 && -1 > 0U\n"
        "unary_ok\n"
        "#endif\n"
        "#if (0 && (1 / 0)) == 0 && (1 || (1 / 0)) == 1 && "
        "(0 ? (1 / 0) : 9) == 9 && (1 ? 7 : (1 / 0)) == 7\n"
        "short_circuit_ok\n"
        "#endif\n"
        "#if 010 == 8 && 0x10UL == 16 && '\\n' == 10 && "
        "'\\x41' == 'A' && '\\101' == 'A'\n"
        "literals_ok\n"
        "#endif\n"
        "#if U'\\x110000' == 0x110000 && U'\\xffffffff' == 0xffffffffU && "
        "'\\u0024' == '$' && '\\u0040' == '@' && '\\u0060' == '`'\n"
        "character_ranges_ok\n"
        "#endif\n"
        "#if L'\\x80000000' < 0 && L'\\xffffffff' == -1\n"
        "wide_ranges_ok\n"
        "#endif\n"
        "#if (0 && (1, 2)) == 0 && (1 || (1, 2)) == 1 && "
        "(0 ? (1, 2) : 3) == 3 && (1 ? -1 : (0, 0U)) > 0\n"
        "unevaluated_comma_ok\n"
        "#endif\n"
        "#if (0 ? 1 : 0 ? 2 : 3) == 3 && (1 ? -1 : 0U) > 0U\n"
        "conditional_ok\n"
        "#endif\n"
        "#if (1 + 2 << 2) == 12 && (1 | 2 && 0) == 0 && "
        "(1 || 0 ? 4 : 5) == 4\n"
        "precedence_ok\n"
        "#endif\n"
        "#if 1UL == 1LU && 1ULL == 1LLU && "
        "18446744073709551615ULL == ~0ULL\n"
        "suffixes_ok\n"
        "#endif\n"
        "#if 0xffffffff > 0 && -1 < 0xffffffff && -1 > 0xffffffffU\n"
        "preprocessor_width_ok\n"
        "#endif\n"
        "#if ONE && \\\n"
        "    VALUE == 2\n"
        "splice_ok\n"
        "#endif\n"
        "#if 0\n"
        "not_taken\n"
        "#elif (3 * 3) == 9\n"
        "elif_ok\n"
        "#else\n"
        "not_taken\n"
        "#endif\n"
        "%:if 1\n"
        "digraph_ok\n"
        "%:endif\n"
        "#if COMMAND_FLAG == 12\n"
        "command_ok\n"
        "#endif\n"
        "restored_defined defined follower\n";
    static const char gnu_text[] = "#if 0b1010ULL == 10 && 9223372036854775808 > 0 && '\\e' == 27\n"
                                   "gnu_literals_ok\n"
                                   "#endif\n";
    static const char *const expected_text[] = {"ordinary_defined",  "defined",
                                                "follower",          "defined_ok",
                                                "unused_defined_ok", "macro_ok",
                                                "arithmetic_ok",     "shifts_ok",
                                                "relations_ok",      "bitwise_ok",
                                                "unary_ok",          "short_circuit_ok",
                                                "literals_ok",       "character_ranges_ok",
                                                "wide_ranges_ok",    "unevaluated_comma_ok",
                                                "conditional_ok",    "precedence_ok",
                                                "suffixes_ok",       "preprocessor_width_ok",
                                                "splice_ok",         "elif_ok",
                                                "digraph_ok",        "command_ok",
                                                "restored_defined",  "defined",
                                                "follower"};
    ctool_host_adapter_t adapter;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_source_t source;
    ctool_c_pp_macro_action_t action;
    ctool_c_pp_request_t request;
    ctool_c_pp_result_t result;
    ctool_status_t status;
    ctool_u32 index;

    if (open_job("conditional-expressions", &adapter, &job) != 0) {
        return 1;
    }
    source.path.text = ctool_string("/conditional-expressions.c");
    source.contents = ctool_bytes(source_text, (ctool_u32)(sizeof(source_text) - 1u));
    action.kind = CTOOL_C_PP_MACRO_DEFINE;
    action.name = ctool_string("COMMAND_FLAG");
    action.replacement = ctool_string("(3 << 2)");
    (void)memset(&request, 0, sizeof(request));
    request.mode = CTOOL_C_PP_MODE_C11;
    request.macro_actions = &action;
    request.macro_action_count = 1u;
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK ||
        result.token_count != (ctool_u32)(sizeof(expected_text) / sizeof(expected_text[0]))) {
        (void)fprintf(stderr, "conditional-expressions: preprocessing failed (%s, %u tokens)\n",
                      ctool_status_name(status), result.token_count);
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    for (index = 0u; index < result.token_count; index++) {
        if (!string_equal(result.tokens[index].spelling, expected_text[index]) ||
            !string_equal(result.tokens[index].location.path, "/conditional-expressions.c")) {
            (void)fprintf(stderr, "conditional-expressions: token %u differs\n", index);
            ctool_job_close(job);
            return 1;
        }
    }
    source.path.text = ctool_string("/conditional-gnu.c");
    source.contents = ctool_bytes(gnu_text, (ctool_u32)(sizeof(gnu_text) - 1u));
    request.gnu_extensions = CTOOL_TRUE;
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || result.token_count != 1u ||
        !string_equal(result.tokens[0].spelling, "gnu_literals_ok") ||
        !string_equal(result.tokens[0].location.path, "/conditional-gnu.c") ||
        ctool_job_diagnostic_count(job) != 0u) {
        (void)fprintf(stderr, "conditional-expressions: GNU literal probe differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    ctool_job_close(job);
    (void)printf("conditional-expressions: ok\n");
    return 0;
}

static int run_predefined_macros(void) {
    static char source_text[] =
        "#define SITE __FILE__ __LINE__\n"
        "first __FILE__ __LINE__ __STDC__ __STDC_HOSTED__ __STDC_VERSION__\n"
        "site SITE\n"
        "#if defined(__FILE__) && defined __LINE__ && defined(__STDC__) && "
        "__STDC__ == 1 && __STDC_VERSION__ == 201112L && "
        "__STDC_HOSTED__ == 0\n"
        "predefined_condition\n"
        "#endif\n"
        "#ifdef __STDC__\n"
        "ifdef_ok\n"
        "#endif\n"
        "#ifndef __LINE__\n"
        "not_taken\n"
        "#endif\n"
        "orders __ATOMIC_RELAXED __ATOMIC_CONSUME __ATOMIC_ACQUIRE "
        "__ATOMIC_RELEASE __ATOMIC_ACQ_REL __ATOMIC_SEQ_CST\n"
        "#if defined(__ATOMIC_RELAXED) && defined(__ATOMIC_CONSUME) && "
        "defined(__ATOMIC_ACQUIRE) && defined(__ATOMIC_RELEASE) && "
        "defined(__ATOMIC_ACQ_REL) && defined(__ATOMIC_SEQ_CST) && "
        "__ATOMIC_RELAXED == 0 && __ATOMIC_CONSUME == 1 && "
        "__ATOMIC_ACQUIRE == 2 && __ATOMIC_RELEASE == 3 && "
        "__ATOMIC_ACQ_REL == 4 && __ATOMIC_SEQ_CST == 5\n"
        "atomic_orders_condition\n"
        "#endif\n"
        "timestamp __DATE__ __TIME__\n"
        "#if defined(__DATE__) && defined(__TIME__)\n"
        "timestamp_condition\n"
        "#endif\n";
    static char source_path[] = "/predefined/path.c";
    static const char hosted_text[] = "hosted __STDC_HOSTED__\n";
    static const char cupid_text[] = "cupid __FILE__ __LINE__ __STDC__ __STDC_HOSTED__ "
                                     "__STDC_VERSION__ __DATE__ __TIME__\n";
    static const char custom_text[] = "custom __DATE__ __TIME__\n";
    static char custom_date[] = "Feb 29 2024";
    static char custom_time[] = "23:59:58";
    static const char escaped_text[] = "escaped __FILE__\n";
    static char escaped_path[] = "/q\"\n\001x.c";
    static const char *const expected_text[] = {"first",
                                                "\"/predefined/path.c\"",
                                                "2",
                                                "1",
                                                "0",
                                                "201112L",
                                                "site",
                                                "\"/predefined/path.c\"",
                                                "3",
                                                "predefined_condition",
                                                "ifdef_ok",
                                                "orders",
                                                "0",
                                                "1",
                                                "2",
                                                "3",
                                                "4",
                                                "5",
                                                "atomic_orders_condition",
                                                "timestamp",
                                                "\"Jan  1 1970\"",
                                                "\"00:00:00\"",
                                                "timestamp_condition"};
    ctool_host_adapter_t adapter;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_source_t source;
    ctool_c_pp_request_t request;
    ctool_c_pp_result_t result;
    ctool_status_t status;
    ctool_u32 index;

    if (open_job("predefined", &adapter, &job) != 0) {
        return 1;
    }
    source.path.text.data = source_path;
    source.path.text.size = (ctool_u32)(sizeof(source_path) - 1u);
    source.contents = ctool_bytes(source_text, (ctool_u32)(sizeof(source_text) - 1u));
    (void)memset(&request, 0, sizeof(request));
    request.mode = CTOOL_C_PP_MODE_C11;
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK ||
        result.token_count != (ctool_u32)(sizeof(expected_text) / sizeof(expected_text[0]))) {
        (void)fprintf(stderr, "predefined: freestanding preprocessing failed (%s, %u tokens)\n",
                      ctool_status_name(status), result.token_count);
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    for (index = 0u; index < result.token_count; index++) {
        if (!string_equal(result.tokens[index].spelling, expected_text[index]) ||
            !string_equal(result.tokens[index].location.path, "/predefined/path.c")) {
            (void)fprintf(stderr, "predefined: token %u differs\n", index);
            ctool_job_close(job);
            return 1;
        }
    }
    if (result.tokens[1].location.line != 2u || result.tokens[1].location.column != 7u ||
        result.tokens[7].location.line != 3u || result.tokens[7].location.column != 6u ||
        result.tokens[8].location.line != 3u || result.tokens[8].location.column != 6u) {
        (void)fprintf(stderr, "predefined: generated locations differ\n");
        ctool_job_close(job);
        return 1;
    }
    source_text[0] = 'X';
    source_path[1] = 'X';
    if (!string_equal(result.tokens[1].spelling, "\"/predefined/path.c\"") ||
        !string_equal(result.tokens[7].spelling, "\"/predefined/path.c\"") ||
        !string_equal(result.tokens[1].location.path, "/predefined/path.c")) {
        (void)fprintf(stderr, "predefined: generated values retained input\n");
        ctool_job_close(job);
        return 1;
    }
    source_text[0] = '#';
    source_path[1] = 'p';

    source.path.text = ctool_string("/hosted.c");
    source.contents = ctool_bytes(hosted_text, (ctool_u32)(sizeof(hosted_text) - 1u));
    request.hosted_environment = CTOOL_TRUE;
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || result.token_count != 2u ||
        !string_equal(result.tokens[0].spelling, "hosted") ||
        !string_equal(result.tokens[1].spelling, "1") ||
        !string_equal(result.tokens[1].location.path, "/hosted.c") ||
        ctool_job_diagnostic_count(job) != 0u) {
        (void)fprintf(stderr, "predefined: hosted expansion differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }

    source.path.text = ctool_string("/cupid.cc");
    source.contents = ctool_bytes(cupid_text, (ctool_u32)(sizeof(cupid_text) - 1u));
    request.mode = CTOOL_C_PP_MODE_CUPID;
    request.hosted_environment = CTOOL_FALSE;
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || result.token_count != 8u ||
        !string_equal(result.tokens[0].spelling, "cupid") ||
        !string_equal(result.tokens[1].spelling, "\"/cupid.cc\"") ||
        !string_equal(result.tokens[2].spelling, "1") ||
        !string_equal(result.tokens[3].spelling, "1") ||
        !string_equal(result.tokens[4].spelling, "0") ||
        !string_equal(result.tokens[5].spelling, "201112L") ||
        !string_equal(result.tokens[6].spelling, "\"Jan  1 1970\"") ||
        !string_equal(result.tokens[7].spelling, "\"00:00:00\"") ||
        ctool_job_diagnostic_count(job) != 0u) {
        (void)fprintf(stderr, "predefined: Cupid-mode expansion differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }

    source.path.text = ctool_string("/custom-time.c");
    source.contents = ctool_bytes(custom_text, (ctool_u32)(sizeof(custom_text) - 1u));
    request.mode = CTOOL_C_PP_MODE_C11;
    request.translation_date = ctool_string(custom_date);
    request.translation_time = ctool_string(custom_time);
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || result.token_count != 3u ||
        !string_equal(result.tokens[0].spelling, "custom") ||
        !string_equal(result.tokens[1].spelling, "\"Feb 29 2024\"") ||
        !string_equal(result.tokens[2].spelling, "\"23:59:58\"") ||
        ctool_job_diagnostic_count(job) != 0u) {
        (void)fprintf(stderr, "predefined: custom timestamp differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    custom_date[0] = 'X';
    custom_time[0] = 'X';
    if (!string_equal(result.tokens[1].spelling, "\"Feb 29 2024\"") ||
        !string_equal(result.tokens[2].spelling, "\"23:59:58\"")) {
        (void)fprintf(stderr, "predefined: custom timestamp retained input\n");
        ctool_job_close(job);
        return 1;
    }
    custom_date[0] = 'F';
    custom_time[0] = '2';

    source.path.text.data = escaped_path;
    source.path.text.size = (ctool_u32)(sizeof(escaped_path) - 1u);
    source.contents = ctool_bytes(escaped_text, (ctool_u32)(sizeof(escaped_text) - 1u));
    request.mode = CTOOL_C_PP_MODE_C11;
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || result.token_count != 2u ||
        !string_equal(result.tokens[0].spelling, "escaped") ||
        !string_equal(result.tokens[1].spelling, "\"/q\\\"\\012\\001x.c\"") ||
        !string_equal(result.tokens[1].location.path, "/q\"\n\001x.c")) {
        (void)fprintf(stderr, "predefined: escaped file spelling differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    escaped_path[1] = 'X';
    if (!string_equal(result.tokens[1].spelling, "\"/q\\\"\\012\\001x.c\"") ||
        !string_equal(result.tokens[1].location.path, "/q\"\n\001x.c")) {
        (void)fprintf(stderr, "predefined: escaped path retained input\n");
        ctool_job_close(job);
        return 1;
    }
    escaped_path[1] = 'q';
    ctool_job_close(job);
    (void)printf("predefined: ok\n");
    return 0;
}

static int run_predefined_files(void) {
    static char forced_text[] = "forced __FILE__ __LINE__\n";
    static char header_text[] = "included __FILE__ __LINE__\n"
                                "#define HEADER_SITE __FILE__ __LINE__\n"
                                "#define HEADER_FUNCTION(value) value __FILE__ __LINE__\n";
    static const char source_text[] = "#include \"predefined.h\"\n"
                                      "invoked HEADER_SITE\n"
                                      "#define STR(value) #value\n"
                                      "#define XSTR(value) STR(value)\n"
                                      "raw STR(__LINE__)\n"
                                      "expanded XSTR(__LINE__)\n"
                                      "#define CAT(left, right) left ## right\n"
                                      "paste CAT(__LINE__, X)\n"
                                      "#if defined(HEADER_SITE)\nheader_macro_ok\n#endif\n"
                                      "function_site HEADER_FUNCTION(value)\n";
    static const char *const expected_text[] = {"forced",
                                                "\"/forced.h\"",
                                                "1",
                                                "included",
                                                "\"/predefined/predefined.h\"",
                                                "1",
                                                "invoked",
                                                "\"/predefined/main.c\"",
                                                "2",
                                                "raw",
                                                "\"__LINE__\"",
                                                "expanded",
                                                "\"6\"",
                                                "paste",
                                                "__LINE__X",
                                                "header_macro_ok",
                                                "function_site",
                                                "value",
                                                "\"/predefined/main.c\"",
                                                "12"};
    static const char *const expected_path[] = {"/forced.h",
                                                "/forced.h",
                                                "/forced.h",
                                                "/predefined/predefined.h",
                                                "/predefined/predefined.h",
                                                "/predefined/predefined.h",
                                                "/predefined/main.c",
                                                "/predefined/main.c",
                                                "/predefined/main.c",
                                                "/predefined/main.c",
                                                "/predefined/main.c",
                                                "/predefined/main.c",
                                                "/predefined/main.c",
                                                "/predefined/main.c",
                                                "/predefined/main.c",
                                                "/predefined/main.c",
                                                "/predefined/main.c",
                                                "/predefined/main.c",
                                                "/predefined/main.c",
                                                "/predefined/main.c"};
    fixture_file_t files[2];
    fixture_store_t store;
    ctool_host_adapter_t adapter;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_source_t source;
    ctool_path_t forced;
    ctool_c_pp_request_t request;
    ctool_c_pp_result_t result;
    ctool_status_t status;
    ctool_u32 index;

    files[0].path = ctool_string("/forced.h");
    files[0].contents = ctool_bytes(forced_text, (ctool_u32)(sizeof(forced_text) - 1u));
    files[1].path = ctool_string("/predefined/predefined.h");
    files[1].contents = ctool_bytes(header_text, (ctool_u32)(sizeof(header_text) - 1u));
    store.files = files;
    store.file_count = 2u;
    store.read_count = 0u;
    if (open_fixture_job("predefined-files", &store, &adapter, &job) != 0) {
        return 1;
    }
    source.path.text = ctool_string("/predefined/main.c");
    source.contents = ctool_bytes(source_text, (ctool_u32)(sizeof(source_text) - 1u));
    forced.text = ctool_string("/forced.h");
    (void)memset(&request, 0, sizeof(request));
    request.mode = CTOOL_C_PP_MODE_C11;
    request.forced_includes = &forced;
    request.forced_include_count = 1u;
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || store.read_count != 2u ||
        result.token_count != (ctool_u32)(sizeof(expected_text) / sizeof(expected_text[0]))) {
        (void)fprintf(stderr, "predefined-files: preprocessing failed (%s, %u tokens)\n",
                      ctool_status_name(status), result.token_count);
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    for (index = 0u; index < result.token_count; index++) {
        if (!string_equal(result.tokens[index].spelling, expected_text[index]) ||
            !string_equal(result.tokens[index].location.path, expected_path[index])) {
            (void)fprintf(stderr, "predefined-files: token %u differs\n", index);
            ctool_job_close(job);
            return 1;
        }
    }
    forced_text[0] = 'X';
    header_text[0] = 'X';
    if (!string_equal(result.tokens[1].spelling, "\"/forced.h\"") ||
        !string_equal(result.tokens[4].spelling, "\"/predefined/predefined.h\"") ||
        !string_equal(result.tokens[7].spelling, "\"/predefined/main.c\"") ||
        !string_equal(result.tokens[1].location.path, "/forced.h")) {
        (void)fprintf(stderr, "predefined-files: result retained fixture storage\n");
        ctool_job_close(job);
        return 1;
    }
    forced_text[0] = 'f';
    header_text[0] = 'i';
    ctool_job_close(job);
    (void)printf("predefined-files: ok\n");
    return 0;
}

static int run_predefined_errors(void) {
    static const char *const names[] = {"__FILE__",         "__LINE__",         "__DATE__",
                                        "__TIME__",         "__STDC__",         "__STDC_HOSTED__",
                                        "__STDC_VERSION__", "__ATOMIC_RELAXED", "__ATOMIC_CONSUME",
                                        "__ATOMIC_ACQUIRE", "__ATOMIC_RELEASE", "__ATOMIC_ACQ_REL",
                                        "__ATOMIC_SEQ_CST", "defined"};
    static const char recovery_text[] =
        "#define ID(defined) defined\nrecovered ID(value) __STDC__\n";
    static const char action_text[] = "unreached\n";
    static const char *const invalid_dates[] = {"Foo 10 2026", "Jan 01 1970", "Jul 32 2026",
                                                "Feb 31 2026", "Apr 31 2026", "Feb 29 2025"};
    static const char *const invalid_times[] = {"24:00:00", "23:60:00", "23:59:61"};
    ctool_host_adapter_t adapter;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_source_t source;
    ctool_c_pp_request_t request;
    ctool_c_pp_macro_action_t action;
    ctool_c_pp_result_t result;
    ctool_arena_mark_t mark;
    const ctool_diagnostic_t *diagnostic;
    ctool_status_t status;
    ctool_u32 diagnostic_index = 0u;
    ctool_u32 index;
    ctool_u32 operation;
    char source_text[96];

    if (open_job("predefined-errors", &adapter, &job) != 0) {
        return 1;
    }
    source.path.text = ctool_string("/predefined-errors.c");
    for (index = 0u; index < (ctool_u32)(sizeof(names) / sizeof(names[0])); index++) {
        for (operation = 0u; operation < 2u; operation++) {
            int written =
                snprintf(source_text, sizeof(source_text),
                         operation == 0u ? "#define %s 1\n" : "#undef %s\n", names[index]);
            if (written < 0 || (size_t)written >= sizeof(source_text)) {
                (void)fprintf(stderr, "predefined-errors: source fixture overflowed\n");
                ctool_job_close(job);
                return 1;
            }
            source.contents = ctool_bytes(source_text, (ctool_u32)written);
            (void)memset(&request, 0, sizeof(request));
            request.mode = CTOOL_C_PP_MODE_C11;
            (void)memset(&result, 0xa5, sizeof(result));
            mark = ctool_arena_mark(ctool_job_arena(job));
            status = ctool_c_preprocess(job, &source, &request, &result);
            diagnostic = ctool_job_diagnostic(job, diagnostic_index);
            if (status != CTOOL_ERR_INPUT || result.tokens != NULL || result.token_count != 0u ||
                diagnostic == NULL || diagnostic->code != CTOOL_C_PP_DIAG_MACRO_DEFINITION ||
                diagnostic->line != 1u || diagnostic->column != (operation == 0u ? 9u : 8u) ||
                !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
                (void)fprintf(stderr, "predefined-errors: source case %u/%u differs\n", index,
                              operation);
                (void)ctool_job_render_diagnostics(job);
                ctool_job_close(job);
                return 1;
            }
            diagnostic_index++;
        }
    }

    source.contents = ctool_bytes(action_text, (ctool_u32)(sizeof(action_text) - 1u));
    for (index = 0u; index < (ctool_u32)(sizeof(names) / sizeof(names[0])); index++) {
        for (operation = 0u; operation < 2u; operation++) {
            (void)memset(&request, 0, sizeof(request));
            request.mode = CTOOL_C_PP_MODE_C11;
            action.kind = operation == 0u ? CTOOL_C_PP_MACRO_DEFINE : CTOOL_C_PP_MACRO_UNDEF;
            action.name = ctool_string(names[index]);
            action.replacement = operation == 0u ? ctool_string("1") : ctool_string("");
            request.macro_actions = &action;
            request.macro_action_count = 1u;
            (void)memset(&result, 0xa5, sizeof(result));
            mark = ctool_arena_mark(ctool_job_arena(job));
            status = ctool_c_preprocess(job, &source, &request, &result);
            diagnostic = ctool_job_diagnostic(job, diagnostic_index);
            if (status != CTOOL_ERR_INPUT || result.tokens != NULL || result.token_count != 0u ||
                diagnostic == NULL || diagnostic->code != CTOOL_C_PP_DIAG_MACRO_DEFINITION ||
                diagnostic->line != 0u || diagnostic->column != 0u ||
                !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
                (void)fprintf(stderr, "predefined-errors: action case %u/%u differs\n", index,
                              operation);
                (void)ctool_job_render_diagnostics(job);
                ctool_job_close(job);
                return 1;
            }
            diagnostic_index++;
        }
    }

    for (index = 0u; index < (ctool_u32)(sizeof(invalid_dates) / sizeof(invalid_dates[0])) +
                                 (ctool_u32)(sizeof(invalid_times) / sizeof(invalid_times[0]));
         index++) {
        ctool_u32 date_count = (ctool_u32)(sizeof(invalid_dates) / sizeof(invalid_dates[0]));
        (void)memset(&request, 0, sizeof(request));
        request.mode = CTOOL_C_PP_MODE_C11;
        if (index < date_count) {
            request.translation_date = ctool_string(invalid_dates[index]);
        } else {
            request.translation_time = ctool_string(invalid_times[index - date_count]);
        }
        (void)memset(&result, 0xa5, sizeof(result));
        mark = ctool_arena_mark(ctool_job_arena(job));
        status = ctool_c_preprocess(job, &source, &request, &result);
        diagnostic = ctool_job_diagnostic(job, diagnostic_index);
        if (status != CTOOL_ERR_INVALID_ARGUMENT || result.tokens != NULL ||
            result.token_count != 0u || diagnostic == NULL ||
            diagnostic->code != CTOOL_C_PP_DIAG_INVALID_REQUEST || diagnostic->line != 0u ||
            diagnostic->column != 0u ||
            !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
            (void)fprintf(stderr, "predefined-errors: timestamp case %u differs\n", index);
            (void)ctool_job_render_diagnostics(job);
            ctool_job_close(job);
            return 1;
        }
        diagnostic_index++;
    }

    source.contents = ctool_bytes(recovery_text, (ctool_u32)(sizeof(recovery_text) - 1u));
    (void)memset(&request, 0, sizeof(request));
    request.mode = CTOOL_C_PP_MODE_C11;
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || result.token_count != 3u ||
        !string_equal(result.tokens[0].spelling, "recovered") ||
        !string_equal(result.tokens[1].spelling, "value") ||
        !string_equal(result.tokens[2].spelling, "1") ||
        ctool_job_diagnostic_count(job) != diagnostic_index) {
        (void)fprintf(stderr, "predefined-errors: recovery differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    ctool_job_close(job);
    (void)printf("predefined-errors: ok\n");
    return 0;
}
