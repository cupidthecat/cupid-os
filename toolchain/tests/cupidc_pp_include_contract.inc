/**
 * @file cupidc_pp_include_contract.inc
 * @brief Check header lookup, directive errors, and request limits.
 */

static int run_object_includes(void) {
    static const char primary_text[] =
        "#ifndef MAIN_GUARD\n"
        "#define MAIN_GUARD\n"
        "#define LOCAL_VALUE 1\n"
        "#undef LOCAL_VALUE\n"
        "#define LOCAL_VALUE 7\n"
        "#define LOCAL_VALUE 7\n"
        "#define SELF SELF\n"
        "#define MUTUAL_A MUTUAL_B\n"
        "#define MUTUAL_B MUTUAL_A\n"
        "#define REMOVED 99\n"
        "#undef REMOVED\n"
        "#include \"object.h\"\n"
        "#include <angle.h>\n"
        "#include <a b.h>\n"
        "#include <a/**/b.h>\n"
        "#include \"root_only.h\"\n"
        "#ifdef OMIT\n"
        "#error command-line undef did not apply\n"
        "#endif\n"
        "#if 0\n"
        "#ifdef\n"
        "#endif\n"
        "#include \"inactive-missing.h\"\n"
        "#endif\n"
        "#ifndef REMOVED\n"
        "#define CHOICE HEADER_VALUE\n"
        "#else\n"
        "#define CHOICE 0\n"
        "#endif\n"
        "#if 0\n"
        "#error inactive #if branch executed\n"
        "#elif 1\n"
        "#define CONDITIONAL_VALUE 41\n"
        "#else\n"
        "#error inactive #else branch executed\n"
        "#endif\n"
        "#if 1\n"
        "#define SKIPPED_ELIF_VALUE 43\n"
        "#elif NOT_EVALUATED + 1\n"
        "#error skipped #elif branch executed\n"
        "#endif\n"
        "#if 1\n"
        "#elif\n"
        "#endif\n"
        "#ifdef COMMAND_VALUE\n"
        "int values = LOCAL_VALUE + CHOICE + CYCLE_A_VALUE + "
        "CYCLE_B_VALUE + ANGLE_VALUE + FORCED_VALUE + COMMAND_VALUE + "
        "CONDITIONAL_VALUE + SKIPPED_ELIF_VALUE;\n"
        "#endif\n"
        "recursive SELF MUTUAL_A\n"
        "#include \"repeat.h\"\n"
        "#include \"repeat.h\"\n"
        "#include \"main.c\"\n"
        "#endif\n";
    static const char object_text[] = "#ifndef OBJECT_H\n"
                                      "#define OBJECT_H\n"
                                      "#define HEADER_VALUE 11\n"
                                      "#include \"cycle_a.h\"\n"
                                      "#endif\n";
    static const char cycle_a_text[] = "#ifndef CYCLE_A_H\n"
                                       "#define CYCLE_A_H\n"
                                       "#define CYCLE_A_VALUE 13\n"
                                       "#include \"cycle_b.h\"\n"
                                       "#endif\n";
    static const char cycle_b_text[] = "#ifndef CYCLE_B_H\n"
                                       "#define CYCLE_B_H\n"
                                       "#define CYCLE_B_VALUE 17\n"
                                       "#include \"cycle_a.h\"\n"
                                       "#endif\n";
    static const char angle_text[] = "#define ANGLE_VALUE 19\n";
    static const char forced_text[] =
        "#ifdef COMMAND_BASE\n"
        "#define FORCED_VALUE COMMAND_BASE\n"
        "#else\n"
        "#error command macro was not installed before forced include\n"
        "#endif\n";
    static const char repeat_text[] = "repeat 31\n";
    static const char root_only_text[] = "root_only 37\n";
    static const char spaced_text[] = "spaced 47\n";
    static const char commented_text[] = "commented 53\n";
    static const char *const expected_text[] = {
        "spaced",    "47",   "commented", "53",     "root_only", "37",     "int", "values", "=",
        "7",         "+",    "11",        "+",      "13",        "+",      "17",  "+",      "19",
        "+",         "23",   "+",         "29",     "+",         "41",     "+",   "43",     ";",
        "recursive", "SELF", "MUTUAL_A",  "repeat", "31",        "repeat", "31"};
    fixture_file_t files[13];
    fixture_store_t store;
    ctool_host_adapter_t adapter;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_source_t source;
    ctool_c_pp_include_root_t roots[3];
    ctool_path_t forced;
    ctool_c_pp_macro_action_t actions[4];
    ctool_c_pp_request_t request;
    ctool_c_pp_result_t result;
    ctool_status_t status;
    ctool_u32 index;

    files[0].path = ctool_string("/fixtures/object.h");
    files[0].contents = ctool_bytes(object_text, (ctool_u32)(sizeof(object_text) - 1u));
    files[1].path = ctool_string("/fixtures/cycle_a.h");
    files[1].contents = ctool_bytes(cycle_a_text, (ctool_u32)(sizeof(cycle_a_text) - 1u));
    files[2].path = ctool_string("/fixtures/cycle_b.h");
    files[2].contents = ctool_bytes(cycle_b_text, (ctool_u32)(sizeof(cycle_b_text) - 1u));
    files[3].path = ctool_string("/angle/angle.h");
    files[3].contents = ctool_bytes(angle_text, (ctool_u32)(sizeof(angle_text) - 1u));
    files[4].path = ctool_string("/fixtures/forced.h");
    files[4].contents = ctool_bytes(forced_text, (ctool_u32)(sizeof(forced_text) - 1u));
    files[5].path = ctool_string("/fixtures/repeat.h");
    files[5].contents = ctool_bytes(repeat_text, (ctool_u32)(sizeof(repeat_text) - 1u));
    files[6].path = ctool_string("/quoted-first/root_only.h");
    files[6].contents = ctool_bytes(root_only_text, (ctool_u32)(sizeof(root_only_text) - 1u));
    files[7].path = ctool_string("/quoted-first/object.h");
    files[7].contents = ctool_bytes("wrong_object 1\n", 15u);
    files[8].path = ctool_string("/fixtures/angle.h");
    files[8].contents = ctool_bytes("wrong_angle 1\n", 14u);
    files[9].path = ctool_string("/angle/root_only.h");
    files[9].contents = ctool_bytes("wrong_form 1\n", 13u);
    files[10].path = ctool_string("/quoted-second/root_only.h");
    files[10].contents = ctool_bytes("wrong_order 1\n", 14u);
    files[11].path = ctool_string("/angle/a b.h");
    files[11].contents = ctool_bytes(spaced_text, (ctool_u32)(sizeof(spaced_text) - 1u));
    files[12].path = ctool_string("/angle/a/**/b.h");
    files[12].contents = ctool_bytes(commented_text, (ctool_u32)(sizeof(commented_text) - 1u));
    store.files = files;
    store.file_count = 13u;
    store.read_count = 0u;
    if (open_fixture_job("object-includes", &store, &adapter, &job) != 0) {
        return 1;
    }

    source.path.text = ctool_string("/fixtures/main.c");
    source.contents = ctool_bytes(primary_text, (ctool_u32)(sizeof(primary_text) - 1u));
    roots[0].directory.text = ctool_string("/angle");
    roots[0].forms = CTOOL_C_PP_INCLUDE_ANGLE;
    roots[1].directory.text = ctool_string("/quoted-first");
    roots[1].forms = CTOOL_C_PP_INCLUDE_QUOTED;
    roots[2].directory.text = ctool_string("/quoted-second");
    roots[2].forms = CTOOL_C_PP_INCLUDE_QUOTED;
    forced.text = ctool_string("/fixtures/forced.h");
    actions[0].kind = CTOOL_C_PP_MACRO_DEFINE;
    actions[0].name = ctool_string("COMMAND_VALUE");
    actions[0].replacement = ctool_string("29");
    actions[1].kind = CTOOL_C_PP_MACRO_DEFINE;
    actions[1].name = ctool_string("COMMAND_BASE");
    actions[1].replacement = ctool_string("23");
    actions[2].kind = CTOOL_C_PP_MACRO_DEFINE;
    actions[2].name = ctool_string("OMIT");
    actions[2].replacement = ctool_string("1");
    actions[3].kind = CTOOL_C_PP_MACRO_UNDEF;
    actions[3].name = ctool_string("OMIT");
    actions[3].replacement = ctool_string("");
    (void)memset(&request, 0, sizeof(request));
    request.mode = CTOOL_C_PP_MODE_C11;
    request.include_roots = roots;
    request.include_root_count = 3u;
    request.forced_includes = &forced;
    request.forced_include_count = 1u;
    request.macro_actions = actions;
    request.macro_action_count = 4u;

    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK ||
        result.token_count != (ctool_u32)(sizeof(expected_text) / sizeof(expected_text[0]))) {
        (void)fprintf(stderr, "object-includes: preprocessing failed (%s, %u tokens)\n",
                      ctool_status_name(status), result.token_count);
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    for (index = 0u; index < result.token_count; index++) {
        if (!string_equal(result.tokens[index].spelling, expected_text[index])) {
            (void)fprintf(stderr, "object-includes: token %u differs\n", index);
            ctool_job_close(job);
            return 1;
        }
    }
    if (store.read_count != 9u ||
        !string_equal(result.tokens[30].location.path, "/fixtures/repeat.h") ||
        !string_equal(result.tokens[32].location.path, "/fixtures/repeat.h")) {
        (void)fprintf(stderr, "object-includes: source cache or locations differ\n");
        ctool_job_close(job);
        return 1;
    }
    ctool_job_close(job);
    (void)printf("object-includes: ok\n");
    return 0;
}

static int run_include_macros(void) {
    static const char forced_text[] = "#include CONFIG_HEADER\n"
                                      "forced_tail\n";
    static const char config_text[] = "config_body\n";
    static const char local_text[] = "#pragma once\n"
                                     "local_body\n"
                                     "#define NESTED \"nested.h\"\n"
                                     "#include NESTED\n";
    static const char nested_text[] = "nested_body\n";
    static const char angle_text[] = "angle_body\n";
    static const char stringified_text[] = "stringified_body\n";
    static const char literal_text[] = "literal_body\n";
    static const char direct_quote_text[] = "direct_quote_body\n";
    static const char surviving_header_text[] = "surviving_header_body\n";
    static const char pasted_text[] = "pasted_body\n";
    static const char leading_space_text[] = "leading_space_body\n";
    static const char spaced_text[] = "spaced_body\n";
    static const char trailing_space_text[] = "trailing_space_body\n";
    static const char spliced_text[] = "spliced_body\n";
    static const char source_text[] = "#define LOCAL \"local.h\"\n"
                                      "#define PASS(value) value\n"
                                      "#define ANGLE(value) <value>\n"
                                      "#define STRINGIFY_RAW(value) #value\n"
                                      "#define STRINGIFY(value) STRINGIFY_RAW(value)\n"
                                      "#define HEADER_FILE \"pasted.h\"\n"
                                      "#define CAT_RAW(left, right) left ## right\n"
                                      "#define CAT(left, right) CAT_RAW(left, right)\n"
                                      "#define EMPTY\n"
                                      "#define SPLICE \"spliced.h\"\n"
                                      "#define SPACED <space dir/with space.h>\n"
                                      "#define LEADING_SPACE < leading.h>\n"
                                      "#define TRAILING_SPACE <trailing.h >\n"
                                      "#define NAME wrong.h\n"
                                      "#if 0\n"
                                      "#include MALFORMED(\n"
                                      "#endif\n"
                                      "#pragma pack(2)\n"
                                      "#include LOCAL\n"
                                      "#include PASS(LOCAL)\n"
                                      "%:include PASS(ANGLE(deep/with-dash.h))\n"
                                      "#include ANGLE(deep/with-dash.h)\n"
                                      "#include STRINGIFY(stringified.h)\n"
                                      "#include <NAME>\n"
                                      "#include \"NAME\" EMPTY\n"
                                      "#include <SURVIVES> EMPTY\n"
                                      "#include CAT(HEADER_, FILE)\n"
                                      "#include LEADING_SPACE\n"
                                      "#include SPACED\n"
                                      "#include TRAILING_SPACE\n"
                                      "#include PASS( \\\n"
                                      "  SPLICE)\n"
                                      "#pragma pack()\n"
                                      "primary_tail\n";
    static const char *const expected_text[] = {
        "config_body",           "forced_tail",         "local_body",
        "nested_body",           "angle_body",          "angle_body",
        "stringified_body",      "literal_body",        "direct_quote_body",
        "surviving_header_body", "pasted_body",         "leading_space_body",
        "spaced_body",           "trailing_space_body", "spliced_body",
        "primary_tail"};
    static const char *const expected_path[] = {"/config.h",
                                                "/forced.h",
                                                "/src/local.h",
                                                "/src/nested.h",
                                                "/inc/deep/with-dash.h",
                                                "/inc/deep/with-dash.h",
                                                "/src/stringified.h",
                                                "/inc/NAME",
                                                "/src/NAME",
                                                "/inc/SURVIVES",
                                                "/src/pasted.h",
                                                "/inc/ leading.h",
                                                "/inc/space dir/with space.h",
                                                "/inc/trailing.h ",
                                                "/src/spliced.h",
                                                "/src/main.c"};
    static const ctool_u32 expected_pack[] = {0u, 0u, 2u, 2u, 2u, 2u, 2u, 2u,
                                              2u, 2u, 2u, 2u, 2u, 2u, 2u, 0u};
    fixture_file_t files[14];
    fixture_store_t store;
    ctool_host_adapter_t adapter;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_source_t source;
    ctool_path_t forced;
    ctool_c_pp_include_root_t root;
    ctool_c_pp_macro_action_t action;
    ctool_c_pp_request_t request;
    ctool_c_pp_result_t result;
    ctool_status_t status;
    ctool_u32 index;

    files[0].path = ctool_string("/forced.h");
    files[0].contents = ctool_bytes(forced_text, (ctool_u32)(sizeof(forced_text) - 1u));
    files[1].path = ctool_string("/config.h");
    files[1].contents = ctool_bytes(config_text, (ctool_u32)(sizeof(config_text) - 1u));
    files[2].path = ctool_string("/src/local.h");
    files[2].contents = ctool_bytes(local_text, (ctool_u32)(sizeof(local_text) - 1u));
    files[3].path = ctool_string("/src/nested.h");
    files[3].contents = ctool_bytes(nested_text, (ctool_u32)(sizeof(nested_text) - 1u));
    files[4].path = ctool_string("/inc/deep/with-dash.h");
    files[4].contents = ctool_bytes(angle_text, (ctool_u32)(sizeof(angle_text) - 1u));
    files[5].path = ctool_string("/src/stringified.h");
    files[5].contents = ctool_bytes(stringified_text, (ctool_u32)(sizeof(stringified_text) - 1u));
    files[6].path = ctool_string("/inc/NAME");
    files[6].contents = ctool_bytes(literal_text, (ctool_u32)(sizeof(literal_text) - 1u));
    files[7].path = ctool_string("/src/spliced.h");
    files[7].contents = ctool_bytes(spliced_text, (ctool_u32)(sizeof(spliced_text) - 1u));
    files[8].path = ctool_string("/inc/space dir/with space.h");
    files[8].contents = ctool_bytes(spaced_text, (ctool_u32)(sizeof(spaced_text) - 1u));
    files[9].path = ctool_string("/inc/trailing.h ");
    files[9].contents =
        ctool_bytes(trailing_space_text, (ctool_u32)(sizeof(trailing_space_text) - 1u));
    files[10].path = ctool_string("/src/NAME");
    files[10].contents =
        ctool_bytes(direct_quote_text, (ctool_u32)(sizeof(direct_quote_text) - 1u));
    files[11].path = ctool_string("/src/pasted.h");
    files[11].contents = ctool_bytes(pasted_text, (ctool_u32)(sizeof(pasted_text) - 1u));
    files[12].path = ctool_string("/inc/SURVIVES");
    files[12].contents =
        ctool_bytes(surviving_header_text, (ctool_u32)(sizeof(surviving_header_text) - 1u));
    files[13].path = ctool_string("/inc/ leading.h");
    files[13].contents =
        ctool_bytes(leading_space_text, (ctool_u32)(sizeof(leading_space_text) - 1u));
    store.files = files;
    store.file_count = 14u;
    store.read_count = 0u;
    if (open_fixture_job("include-macros", &store, &adapter, &job) != 0) {
        return 1;
    }
    source.path.text = ctool_string("/src/main.c");
    source.contents = ctool_bytes(source_text, (ctool_u32)(sizeof(source_text) - 1u));
    forced.text = ctool_string("/forced.h");
    root.directory.text = ctool_string("/inc");
    root.forms = CTOOL_C_PP_INCLUDE_ANGLE;
    action.kind = CTOOL_C_PP_MACRO_DEFINE;
    action.name = ctool_string("CONFIG_HEADER");
    action.replacement = ctool_string("\"config.h\"");
    (void)memset(&request, 0, sizeof(request));
    request.mode = CTOOL_C_PP_MODE_C11;
    request.include_roots = &root;
    request.include_root_count = 1u;
    request.forced_includes = &forced;
    request.forced_include_count = 1u;
    request.macro_actions = &action;
    request.macro_action_count = 1u;
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || store.read_count != 14u ||
        result.token_count != (ctool_u32)(sizeof(expected_text) / sizeof(expected_text[0]))) {
        (void)fprintf(stderr,
                      "include-macros: preprocessing failed (%s, %u tokens, "
                      "%u reads)\n",
                      ctool_status_name(status), result.token_count, store.read_count);
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    for (index = 0u; index < result.token_count; index++) {
        if (!string_equal(result.tokens[index].spelling, expected_text[index]) ||
            !string_equal(result.tokens[index].location.path, expected_path[index]) ||
            result.tokens[index].pack_alignment != expected_pack[index]) {
            (void)fprintf(stderr, "include-macros: token %u differs\n", index);
            ctool_job_close(job);
            return 1;
        }
    }
    ctool_job_close(job);
    (void)printf("include-macros: ok\n");
    return 0;
}

static int run_include_macro_errors(void) {
    enum { LARGE_SOURCE_CAPACITY = 8192 };

    static const char *const error_text[] = {"#include UNKNOWN\n",
                                             "#define EMPTY\n#include EMPTY\n",
                                             "#define NUMBER 7\n#include NUMBER\n",
                                             "#define TWO \"a.h\" \"b.h\"\n#include TWO\n",
                                             "#define WIDE L\"a.h\"\n#include WIDE\n",
                                             "#define EMPTY_ANGLE <>\n#include EMPTY_ANGLE\n",
                                             "#define OPEN <broken.h\n#include OPEN\n",
                                             "#define TRAILING <a.h> extra\n#include TRAILING\n",
                                             "#define SELF SELF\n#include SELF\n",
                                             "#define BAD(a, b) a ## b\n#include BAD(+, *)\n",
                                             "#define MISSING \"missing.h\"\n#include MISSING\n"};
    static const ctool_status_t error_status[] = {
        CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT,    CTOOL_ERR_INPUT,
        CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT,    CTOOL_ERR_INPUT,
        CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_NOT_FOUND};
    static const ctool_u32 error_code[] = {
        CTOOL_C_PP_DIAG_INCLUDE_PATH,     CTOOL_C_PP_DIAG_INCLUDE_PATH,
        CTOOL_C_PP_DIAG_INCLUDE_PATH,     CTOOL_C_PP_DIAG_INCLUDE_PATH,
        CTOOL_C_PP_DIAG_INCLUDE_PATH,     CTOOL_C_PP_DIAG_INCLUDE_PATH,
        CTOOL_C_PP_DIAG_INCLUDE_PATH,     CTOOL_C_PP_DIAG_INCLUDE_PATH,
        CTOOL_C_PP_DIAG_INCLUDE_PATH,     CTOOL_C_PP_DIAG_MACRO_PASTE,
        CTOOL_C_PP_DIAG_INCLUDE_NOT_FOUND};
    static const ctool_u32 error_line[] = {1u, 2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u, 2u};
    static const ctool_u32 error_column[] = {1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 10u, 1u};
    static const char ok_text[] = "included_ok\n";
    static const char recovery_text[] = "#define GOOD \"ok.h\"\n"
                                        "#include GOOD\n"
                                        "recovered\n";
    static char large_text[LARGE_SOURCE_CAPACITY];
    fixture_file_t file;
    fixture_store_t store;
    ctool_host_adapter_t adapter;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_source_t source;
    ctool_c_pp_include_root_t root;
    ctool_c_pp_request_t request;
    ctool_c_pp_result_t result;
    ctool_limits_t limits;
    ctool_arena_mark_t mark;
    const ctool_diagnostic_t *diagnostic;
    ctool_status_t status;
    ctool_u32 case_count = (ctool_u32)(sizeof(error_text) / sizeof(error_text[0]));
    ctool_u32 large_size = 0u;
    ctool_u32 index;

    limits = ctool_default_limits();
    if (limits.path_bytes + 64u > LARGE_SOURCE_CAPACITY ||
        append_text(large_text, LARGE_SOURCE_CAPACITY, &large_size, "#define LARGE <") != 0) {
        return 1;
    }
    for (index = 0u; index <= limits.path_bytes; index++) {
        if (append_character(large_text, LARGE_SOURCE_CAPACITY, &large_size, 'a') != 0) {
            return 1;
        }
    }
    if (append_text(large_text, LARGE_SOURCE_CAPACITY, &large_size, ">\n#include LARGE\n") != 0) {
        return 1;
    }

    file.path = ctool_string("/ok.h");
    file.contents = ctool_bytes(ok_text, (ctool_u32)(sizeof(ok_text) - 1u));
    store.files = &file;
    store.file_count = 1u;
    store.read_count = 0u;
    if (open_fixture_job("include-macro-errors", &store, &adapter, &job) != 0) {
        return 1;
    }
    source.path.text = ctool_string("/case.c");
    root.directory.text = ctool_string("/");
    root.forms = CTOOL_C_PP_INCLUDE_QUOTED | CTOOL_C_PP_INCLUDE_ANGLE;
    (void)memset(&request, 0, sizeof(request));
    request.mode = CTOOL_C_PP_MODE_C11;
    request.include_roots = &root;
    request.include_root_count = 1u;
    for (index = 0u; index < case_count; index++) {
        source.contents = ctool_bytes(error_text[index], (ctool_u32)strlen(error_text[index]));
        (void)memset(&result, 0xa5, sizeof(result));
        mark = ctool_arena_mark(ctool_job_arena(job));
        status = ctool_c_preprocess(job, &source, &request, &result);
        diagnostic = ctool_job_diagnostic(job, index);
        if (status != error_status[index] || result.tokens != NULL || result.token_count != 0u ||
            diagnostic == NULL || diagnostic->code != error_code[index] ||
            !string_equal(diagnostic->path, "/case.c") || diagnostic->line != error_line[index] ||
            diagnostic->column != error_column[index] ||
            !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
            (void)fprintf(stderr, "include-macro-errors: case %u differs (%s)\n", index,
                          ctool_status_name(status));
            (void)ctool_job_render_diagnostics(job);
            ctool_job_close(job);
            return 1;
        }
    }
    source.contents = ctool_bytes(large_text, large_size);
    (void)memset(&result, 0xa5, sizeof(result));
    mark = ctool_arena_mark(ctool_job_arena(job));
    status = ctool_c_preprocess(job, &source, &request, &result);
    diagnostic = ctool_job_diagnostic(job, case_count);
    if (status != CTOOL_ERR_LIMIT || result.tokens != NULL || result.token_count != 0u ||
        diagnostic == NULL || diagnostic->code != CTOOL_C_PP_DIAG_LIMIT ||
        !string_equal(diagnostic->path, "/case.c") || diagnostic->line != 2u ||
        diagnostic->column != 1u ||
        !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
        (void)fprintf(stderr, "include-macro-errors: expanded path limit differs (%s)\n",
                      ctool_status_name(status));
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    source.contents = ctool_bytes(recovery_text, (ctool_u32)(sizeof(recovery_text) - 1u));
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || store.read_count != 1u || result.token_count != 2u ||
        !string_equal(result.tokens[0].spelling, "included_ok") ||
        !string_equal(result.tokens[0].location.path, "/ok.h") ||
        !string_equal(result.tokens[1].spelling, "recovered") ||
        ctool_job_diagnostic_count(job) != case_count + 1u) {
        (void)fprintf(stderr, "include-macro-errors: recovery differs\n");
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    ctool_job_close(job);
    (void)printf("include-macro-errors: ok\n");
    return 0;
}

static int run_include_macro_scale(void) {
    enum { ALIAS_COUNT = 32, REPEAT_COUNT = 32768 };

    static const char dispatcher_text[] = "#include p000\n";
    static const char once_text[] = "#pragma once\n";
    static char alias_names[ALIAS_COUNT][4];
    static ctool_c_pp_macro_action_t actions[ALIAS_COUNT + 1];
    static ctool_path_t forced[REPEAT_COUNT];
    fixture_file_t files[2];
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
    }
    for (index = 0u; index < ALIAS_COUNT; index++) {
        if (index + 1u < ALIAS_COUNT) {
            actions[index].replacement.data = alias_names[index + 1u];
            actions[index].replacement.size = 4u;
        } else {
            actions[index].replacement = ctool_string("TARGET");
        }
    }
    actions[ALIAS_COUNT].kind = CTOOL_C_PP_MACRO_DEFINE;
    actions[ALIAS_COUNT].name = ctool_string("TARGET");
    actions[ALIAS_COUNT].replacement = ctool_string("\"once.h\"");
    files[0].path = ctool_string("/dispatcher.h");
    files[0].contents = ctool_bytes(dispatcher_text, (ctool_u32)(sizeof(dispatcher_text) - 1u));
    files[1].path = ctool_string("/once.h");
    files[1].contents = ctool_bytes(once_text, (ctool_u32)(sizeof(once_text) - 1u));
    store.files = files;
    store.file_count = 2u;
    store.read_count = 0u;
    for (index = 0u; index < REPEAT_COUNT; index++) {
        forced[index].text = ctool_string("/dispatcher.h");
    }
    status = ctool_host_adapter_init(&adapter, ".");
    if (status != CTOOL_OK) {
        (void)fprintf(stderr, "include-macro-scale: host adapter failed (%s)\n",
                      ctool_status_name(status));
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
        (void)fprintf(stderr, "include-macro-scale: job open failed (%s)\n",
                      ctool_status_name(status));
        return 1;
    }
    source.path.text = ctool_string("/primary.c");
    source.contents = ctool_bytes("", 0u);
    (void)memset(&request, 0, sizeof(request));
    request.mode = CTOOL_C_PP_MODE_C11;
    request.forced_includes = forced;
    request.forced_include_count = REPEAT_COUNT;
    request.macro_actions = actions;
    request.macro_action_count = ALIAS_COUNT + 1u;
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || result.token_count != 0u || store.read_count != 2u ||
        ctool_job_diagnostic_count(job) != 0u) {
        (void)fprintf(stderr,
                      "include-macro-scale: scratch reuse differs (%s, %u "
                      "reads)\n",
                      ctool_status_name(status), store.read_count);
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    ctool_job_close(job);
    (void)printf("include-macro-scale: ok\n");
    return 0;
}

static int run_directive_errors(void) {
    static const char missing_text[] = "#include \"missing.h\"\n";
    static const char conditional_text[] = "#else\n";
    static const char loop_text[] = "#include \"loop.h\"\n";
    static const char *const error_text[] = {"#include <broken",
                                             "#define 7\n",
                                             "#define VALUE 1\n#define VALUE 2\n",
                                             "#ifdef VALUE\n",
                                             "#include \"\"\n",
                                             "#include <>\n",
                                             "#include <a>b>\n",
                                             "#define HEADER \"x.h\"\n#include HEADER\n",
                                             "#ifdef\n#endif\n",
                                             "#if 0\n#elif\n#endif\n",
                                             "#include \"loop.h\"\n"};
    static const ctool_status_t error_status[] = {
        CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT,
        CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_NOT_FOUND,
        CTOOL_ERR_INPUT, CTOOL_ERR_INPUT, CTOOL_ERR_LIMIT};
    static const ctool_u32 error_code[] = {
        CTOOL_C_PP_DIAG_INCLUDE_PATH,       CTOOL_C_PP_DIAG_MACRO_DEFINITION,
        CTOOL_C_PP_DIAG_MACRO_REDEFINITION, CTOOL_C_PP_DIAG_CONDITIONAL,
        CTOOL_C_PP_DIAG_INCLUDE_PATH,       CTOOL_C_PP_DIAG_INCLUDE_PATH,
        CTOOL_C_PP_DIAG_INCLUDE_PATH,       CTOOL_C_PP_DIAG_INCLUDE_NOT_FOUND,
        CTOOL_C_PP_DIAG_CONDITIONAL,        CTOOL_C_PP_DIAG_CONDITIONAL,
        CTOOL_C_PP_DIAG_INCLUDE_DEPTH};
    static const ctool_u32 error_line[] = {1u, 1u, 2u, 1u, 1u, 1u, 1u, 2u, 1u, 2u, 1u};
    static const ctool_u32 error_column[] = {1u, 1u, 9u, 1u, 1u, 1u, 1u, 1u, 1u, 1u, 1u};
    static const char *const error_path[] = {
        "/fixtures/case.c", "/fixtures/case.c", "/fixtures/case.c", "/fixtures/case.c",
        "/fixtures/case.c", "/fixtures/case.c", "/fixtures/case.c", "/fixtures/case.c",
        "/fixtures/case.c", "/fixtures/case.c", "/fixtures/loop.h"};
    fixture_file_t loop_file;
    fixture_store_t store;
    ctool_host_adapter_t adapter;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_source_t source;
    ctool_c_pp_include_root_t root;
    ctool_path_t missing_forced;
    ctool_c_pp_request_t request;
    ctool_c_pp_result_t result;
    ctool_arena_mark_t mark;
    const ctool_diagnostic_t *diagnostic;
    ctool_status_t status;
    ctool_u32 index;

    loop_file.path = ctool_string("/fixtures/loop.h");
    loop_file.contents = ctool_bytes(loop_text, (ctool_u32)(sizeof(loop_text) - 1u));
    store.files = &loop_file;
    store.file_count = 1u;
    store.read_count = 0u;
    if (open_fixture_job("directive-errors", &store, &adapter, &job) != 0) {
        return 1;
    }
    source.path.text = ctool_string("/fixtures/missing.c");
    source.contents = ctool_bytes(missing_text, (ctool_u32)(sizeof(missing_text) - 1u));
    root.directory.text = ctool_string("/fixtures");
    root.forms = CTOOL_C_PP_INCLUDE_QUOTED | CTOOL_C_PP_INCLUDE_ANGLE;
    (void)memset(&request, 0, sizeof(request));
    request.mode = CTOOL_C_PP_MODE_C11;
    request.include_roots = &root;
    request.include_root_count = 1u;
    mark = ctool_arena_mark(ctool_job_arena(job));

    status = ctool_c_preprocess(job, &source, &request, &result);
    diagnostic = ctool_job_diagnostic(job, 0u);
    if (status != CTOOL_ERR_NOT_FOUND || result.tokens != NULL || result.token_count != 0u ||
        diagnostic == NULL || diagnostic->code != CTOOL_C_PP_DIAG_INCLUDE_NOT_FOUND ||
        diagnostic->line != 1u || diagnostic->column != 1u ||
        !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
        (void)fprintf(stderr, "directive-errors: missing include was not transactional\n");
        ctool_job_close(job);
        return 1;
    }

    source.path.text = ctool_string("/fixtures/conditional.c");
    source.contents = ctool_bytes(conditional_text, (ctool_u32)(sizeof(conditional_text) - 1u));
    mark = ctool_arena_mark(ctool_job_arena(job));
    status = ctool_c_preprocess(job, &source, &request, &result);
    diagnostic = ctool_job_diagnostic(job, 1u);
    if (status != CTOOL_ERR_INPUT || result.tokens != NULL || result.token_count != 0u ||
        diagnostic == NULL || diagnostic->code != CTOOL_C_PP_DIAG_CONDITIONAL ||
        diagnostic->line != 1u || diagnostic->column != 1u ||
        !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
        (void)fprintf(stderr, "directive-errors: unmatched conditional was not useful\n");
        ctool_job_close(job);
        return 1;
    }

    source.path.text = ctool_string("/fixtures/case.c");
    for (index = 0u; index < (ctool_u32)(sizeof(error_text) / sizeof(error_text[0])); index++) {
        source.contents = ctool_bytes(error_text[index], (ctool_u32)strlen(error_text[index]));
        mark = ctool_arena_mark(ctool_job_arena(job));
        status = ctool_c_preprocess(job, &source, &request, &result);
        diagnostic = ctool_job_diagnostic(job, 2u + index);
        if (status != error_status[index] || result.tokens != NULL || result.token_count != 0u ||
            diagnostic == NULL || diagnostic->code != error_code[index] ||
            diagnostic->line != error_line[index] || diagnostic->column != error_column[index] ||
            !string_equal(diagnostic->path, error_path[index]) ||
            !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
            (void)fprintf(stderr, "directive-errors: table case %u was not useful\n", index);
            ctool_job_close(job);
            return 1;
        }
    }

    source.contents = ctool_bytes(NULL, 0u);
    missing_forced.text = ctool_string("/fixtures/missing-forced.h");
    request.forced_includes = &missing_forced;
    request.forced_include_count = 1u;
    mark = ctool_arena_mark(ctool_job_arena(job));
    status = ctool_c_preprocess(job, &source, &request, &result);
    diagnostic =
        ctool_job_diagnostic(job, 2u + (ctool_u32)(sizeof(error_text) / sizeof(error_text[0])));
    if (status != CTOOL_ERR_NOT_FOUND || result.tokens != NULL || result.token_count != 0u ||
        diagnostic == NULL || diagnostic->code != CTOOL_C_PP_DIAG_INCLUDE_NOT_FOUND ||
        !string_equal(diagnostic->path, "/fixtures/missing-forced.h") || diagnostic->line != 0u ||
        diagnostic->column != 0u ||
        !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
        (void)fprintf(stderr, "directive-errors: missing forced include was not useful\n");
        ctool_job_close(job);
        return 1;
    }
    ctool_job_close(job);
    (void)printf("directive-errors: ok\n");
    return 0;
}

static int expect_request_limit(ctool_job_t *job, const ctool_source_t *source,
                                const ctool_c_pp_request_t *request, ctool_u32 diagnostic_index,
                                const char *expected_path) {
    ctool_c_pp_result_t result;
    ctool_arena_mark_t mark = ctool_arena_mark(ctool_job_arena(job));
    const ctool_diagnostic_t *diagnostic;
    ctool_status_t status;

    (void)memset(&result, 0xa5, sizeof(result));
    status = ctool_c_preprocess(job, source, request, &result);
    diagnostic = ctool_job_diagnostic(job, diagnostic_index);
    if (status != CTOOL_ERR_LIMIT || result.tokens != NULL || result.token_count != 0u ||
        diagnostic == NULL || diagnostic->code != CTOOL_C_PP_DIAG_LIMIT || diagnostic->line != 0u ||
        diagnostic->column != 0u || !string_equal(diagnostic->path, expected_path) ||
        !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
        (void)fprintf(stderr, "limits: request case %u failed\n", diagnostic_index);
        return 1;
    }
    return 0;
}

static int run_limits(void) {
    static const char oversized_text[] = "123456789";
    static const char valid_text[] = "int x;\n";
    ctool_limits_t limits = ctool_default_limits();
    ctool_host_adapter_t adapter;
    ctool_job_config_t config;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_source_t source;
    ctool_c_pp_request_t request;
    ctool_c_pp_macro_action_t action;
    ctool_c_pp_include_root_t root;
    ctool_path_t forced;
    ctool_c_pp_result_t result;
    ctool_status_t status;

    limits.path_bytes = 8u;
    limits.source_bytes = 8u;
    status = ctool_host_adapter_init(&adapter, ".");
    if (status != CTOOL_OK) {
        (void)fprintf(stderr, "limits: host adapter: %s\n", ctool_status_name(status));
        return 1;
    }
    config = ctool_host_job_config(&adapter, limits);
    status = ctool_job_open(&config, &job);
    if (status != CTOOL_OK) {
        (void)fprintf(stderr, "limits: job open: %s\n", ctool_status_name(status));
        return 1;
    }

    (void)memset(&request, 0, sizeof(request));
    request.mode = CTOOL_C_PP_MODE_C11;
    source.path.text.data = "/";
    source.path.text.size = 0xffffffffu;
    source.contents = ctool_bytes(NULL, 0u);
    if (expect_request_limit(job, &source, &request, 0u, "/cupidc") != 0) {
        ctool_job_close(job);
        return 1;
    }

    source.path.text = ctool_string("/ok.c");
    source.contents = ctool_bytes(oversized_text, (ctool_u32)(sizeof(oversized_text) - 1u));
    if (expect_request_limit(job, &source, &request, 1u, "/ok.c") != 0) {
        ctool_job_close(job);
        return 1;
    }

    source.contents = ctool_bytes(NULL, 0u);
    action.kind = CTOOL_C_PP_MACRO_DEFINE;
    action.name = ctool_string("VALUE");
    action.replacement = ctool_string("123456789");
    request.macro_actions = &action;
    request.macro_action_count = 1u;
    if (expect_request_limit(job, &source, &request, 2u, "/ok.c") != 0) {
        ctool_job_close(job);
        return 1;
    }

    action.name.data = "A";
    action.name.size = 0xffffffffu;
    action.replacement = ctool_string("");
    if (expect_request_limit(job, &source, &request, 3u, "/ok.c") != 0) {
        ctool_job_close(job);
        return 1;
    }

    (void)memset(&request, 0, sizeof(request));
    request.mode = CTOOL_C_PP_MODE_C11;
    root.directory.text.data = "/";
    root.directory.text.size = 0xffffffffu;
    root.forms = CTOOL_C_PP_INCLUDE_QUOTED;
    request.include_roots = &root;
    request.include_root_count = 1u;
    if (expect_request_limit(job, &source, &request, 4u, "/ok.c") != 0) {
        ctool_job_close(job);
        return 1;
    }

    (void)memset(&request, 0, sizeof(request));
    request.mode = CTOOL_C_PP_MODE_C11;
    forced.text.data = "/";
    forced.text.size = 0xffffffffu;
    request.forced_includes = &forced;
    request.forced_include_count = 1u;
    if (expect_request_limit(job, &source, &request, 5u, "/ok.c") != 0) {
        ctool_job_close(job);
        return 1;
    }

    (void)memset(&request, 0, sizeof(request));
    request.mode = CTOOL_C_PP_MODE_C11;
    source.contents = ctool_bytes(valid_text, (ctool_u32)(sizeof(valid_text) - 1u));
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || result.token_count != 3u ||
        !string_equal(result.tokens[0].spelling, "int") ||
        !string_equal(result.tokens[1].spelling, "x") ||
        !string_equal(result.tokens[2].spelling, ";") || ctool_job_diagnostic(job, 6u) != NULL) {
        (void)fprintf(stderr, "limits: same-job recovery failed\n");
        ctool_job_close(job);
        return 1;
    }

    ctool_job_close(job);
    (void)printf("limits: ok\n");
    return 0;
}
