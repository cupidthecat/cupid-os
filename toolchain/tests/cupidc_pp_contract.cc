#include "ctool.h"
#include "ctool_host.h"
#include "cupidc_pp.h"

#include <stdio.h>
#include <string.h>

static int string_equal(ctool_string_t actual, const char *expected) {
    size_t expected_size = strlen(expected);
    return actual.size == (ctool_u32)expected_size &&
                   (expected_size == 0u || memcmp(actual.data, expected, expected_size) == 0)
               ? 1
               : 0;
}

static int string_starts_with(ctool_string_t actual, const char *prefix) {
    size_t prefix_size = strlen(prefix);
    return actual.size >= (ctool_u32)prefix_size &&
                   (prefix_size == 0u || memcmp(actual.data, prefix, prefix_size) == 0)
               ? 1
               : 0;
}

static int token_locations_equal(const ctool_c_pp_token_t *token, const char *presumed_path,
                                 ctool_u32 presumed_line, const char *physical_path,
                                 ctool_u32 physical_line) {
    return token != NULL && string_equal(token->location.path, presumed_path) &&
                   token->location.line == presumed_line && token->location.column != 0u &&
                   string_equal(token->physical_location.path, physical_path) &&
                   token->physical_location.line == physical_line &&
                   token->physical_location.column == token->location.column
               ? 1
               : 0;
}

static ctool_u32 text_line_of(const char *text, const char *needle) {
    const char *found = strstr(text, needle);
    ctool_u32 line = 1u;
    if (found == NULL) {
        return 0u;
    }
    while (text != found) {
        if (*text == '\n') {
            line++;
        }
        text++;
    }
    return line;
}

static int arena_marks_equal(ctool_arena_mark_t left, ctool_arena_mark_t right) {
    return left.owner == right.owner && left.block == right.block && left.used == right.used &&
                   left.generation == right.generation
               ? 1
               : 0;
}

typedef enum {
    ACTIVE_ROW_PROFILE = 1,
    ACTIVE_ROW_INCLUDE_ROOT,
    ACTIVE_ROW_MACRO,
    ACTIVE_ROW_FORCED_INCLUDE,
    ACTIVE_ROW_CASE,
    ACTIVE_ROW_GENERATED_CASE,
    ACTIVE_ROW_INCLUDE_ONLY,
    ACTIVE_ROW_NON_ROOT,
    ACTIVE_ROW_DEFERRED_HOSTED
} active_row_kind_t;

typedef struct {
    active_row_kind_t kind;
    const char *profile;
    const char *first;
    const char *second;
    ctool_u32 value;
    ctool_bool flag;
    ctool_bool hosted_environment;
    ctool_bool implicit_function_declarations;
    ctool_bool compatibility_pointer_conversions;
} active_row_t;

typedef struct {
    const char *name;
    ctool_c_pp_mode_t mode;
    ctool_bool gnu_extensions;
    ctool_bool hosted_environment;
    ctool_bool implicit_function_declarations;
    ctool_bool compatibility_pointer_conversions;
    ctool_u32 tracked_case_count;
    ctool_u32 include_root_count;
    ctool_u32 macro_count;
    ctool_u32 forced_include_count;
} active_expected_profile_t;

static const active_row_t active_rows[] = {
#define CUPIDC_PP_PROFILE(NAME, MODE, GNU_BOOL, HOSTED_BOOL, IMPLICIT_BOOL, COMPAT_POINTER_BOOL)   \
    {ACTIVE_ROW_PROFILE,   #NAME,      NULL,          NULL,                                        \
     (ctool_u32)(MODE),    (GNU_BOOL), (HOSTED_BOOL), (IMPLICIT_BOOL),                             \
     (COMPAT_POINTER_BOOL)},
#define CUPIDC_PP_INCLUDE_ROOT(NAME, PATH, FORMS)                                                  \
    {ACTIVE_ROW_INCLUDE_ROOT,                                                                      \
     #NAME,                                                                                        \
     (PATH),                                                                                       \
     NULL,                                                                                         \
     (FORMS),                                                                                      \
     CTOOL_FALSE,                                                                                  \
     CTOOL_FALSE,                                                                                  \
     CTOOL_FALSE,                                                                                  \
     CTOOL_FALSE},
#define CUPIDC_PP_MACRO(NAME, MACRO_NAME, REPLACEMENT)                                             \
    {ACTIVE_ROW_MACRO, #NAME,       (MACRO_NAME), (REPLACEMENT), 0u,                               \
     CTOOL_FALSE,      CTOOL_FALSE, CTOOL_FALSE,  CTOOL_FALSE},
#define CUPIDC_PP_FORCED_INCLUDE(NAME, PATH)                                                       \
    {ACTIVE_ROW_FORCED_INCLUDE,                                                                    \
     #NAME,                                                                                        \
     (PATH),                                                                                       \
     NULL,                                                                                         \
     0u,                                                                                           \
     CTOOL_FALSE,                                                                                  \
     CTOOL_FALSE,                                                                                  \
     CTOOL_FALSE,                                                                                  \
     CTOOL_FALSE},
#define CUPIDC_PP_ACTIVE_CASE(NAME, PATH)                                                          \
    {ACTIVE_ROW_CASE, #NAME, (PATH), NULL, 0u, CTOOL_FALSE, CTOOL_FALSE, CTOOL_FALSE, CTOOL_FALSE},
#define CUPIDC_PP_GENERATED_CASE(NAME, PATH)                                                       \
    {ACTIVE_ROW_GENERATED_CASE,                                                                    \
     #NAME,                                                                                        \
     (PATH),                                                                                       \
     NULL,                                                                                         \
     0u,                                                                                           \
     CTOOL_FALSE,                                                                                  \
     CTOOL_FALSE,                                                                                  \
     CTOOL_FALSE,                                                                                  \
     CTOOL_FALSE},
#define CUPIDC_PP_INCLUDE_ONLY(PATH, OWNER)                                                        \
    {ACTIVE_ROW_INCLUDE_ONLY,                                                                      \
     NULL,                                                                                         \
     (PATH),                                                                                       \
     (OWNER),                                                                                      \
     0u,                                                                                           \
     CTOOL_FALSE,                                                                                  \
     CTOOL_FALSE,                                                                                  \
     CTOOL_FALSE,                                                                                  \
     CTOOL_FALSE},
#define CUPIDC_PP_NON_ROOT(PATH, REASON)                                                           \
    {ACTIVE_ROW_NON_ROOT, NULL,        (PATH),      (REASON),   0u,                                \
     CTOOL_FALSE,         CTOOL_FALSE, CTOOL_FALSE, CTOOL_FALSE},
#define CUPIDC_PP_DEFERRED_HOSTED(PATH, REASON)                                                    \
    {ACTIVE_ROW_DEFERRED_HOSTED,                                                                   \
     NULL,                                                                                         \
     (PATH),                                                                                       \
     (REASON),                                                                                     \
     0u,                                                                                           \
     CTOOL_FALSE,                                                                                  \
     CTOOL_FALSE,                                                                                  \
     CTOOL_FALSE,                                                                                  \
     CTOOL_FALSE},
#include "cupidc_pp_active_cases.inc"
#undef CUPIDC_PP_PROFILE
#undef CUPIDC_PP_INCLUDE_ROOT
#undef CUPIDC_PP_MACRO
#undef CUPIDC_PP_FORCED_INCLUDE
#undef CUPIDC_PP_ACTIVE_CASE
#undef CUPIDC_PP_GENERATED_CASE
#undef CUPIDC_PP_INCLUDE_ONLY
#undef CUPIDC_PP_NON_ROOT
#undef CUPIDC_PP_DEFERRED_HOSTED
};

static const active_expected_profile_t active_expected_profiles[] = {
    {"KERNEL_I386", CTOOL_C_PP_MODE_C11, CTOOL_TRUE, CTOOL_FALSE, CTOOL_FALSE, CTOOL_FALSE, 156u,
     18u, 8u, 0u},
    {"DOOM_COMPAT_I386", CTOOL_C_PP_MODE_C11, CTOOL_TRUE, CTOOL_FALSE, CTOOL_TRUE, CTOOL_TRUE, 3u,
     20u, 7u, 0u},
    {"DOOM_TREE_I386", CTOOL_C_PP_MODE_C11, CTOOL_TRUE, CTOOL_FALSE, CTOOL_TRUE, CTOOL_TRUE, 80u,
     20u, 9u, 1u},
    {"USER_I386", CTOOL_C_PP_MODE_C11, CTOOL_TRUE, CTOOL_FALSE, CTOOL_FALSE, CTOOL_FALSE, 3u, 1u,
     6u, 0u},
    {"FREESTANDING_I386", CTOOL_C_PP_MODE_C11, CTOOL_FALSE, CTOOL_FALSE, CTOOL_FALSE, CTOOL_FALSE,
     1u, 0u, 1u, 0u},
    {"CUPID_RUNTIME", CTOOL_C_PP_MODE_CUPID, CTOOL_FALSE, CTOOL_FALSE, CTOOL_FALSE, CTOOL_FALSE,
     108u, 0u, 0u, 0u},
    {"HOSTED_TOOLCHAIN_64", CTOOL_C_PP_MODE_C11, CTOOL_FALSE, CTOOL_TRUE, CTOOL_FALSE, CTOOL_FALSE,
     0u, 1u, 1u, 0u},
    {"HOSTED_KERNEL_BRIDGE_64", CTOOL_C_PP_MODE_C11, CTOOL_FALSE, CTOOL_TRUE, CTOOL_FALSE,
     CTOOL_FALSE, 0u, 2u, 1u, 0u},
    {"HOSTED_I386_LINUX", CTOOL_C_PP_MODE_C11, CTOOL_FALSE, CTOOL_TRUE, CTOOL_FALSE, CTOOL_FALSE,
     59u, 2u, 1u, 0u},
    {"HOSTED_I386_WINDOWS", CTOOL_C_PP_MODE_C11, CTOOL_FALSE, CTOOL_TRUE, CTOOL_FALSE, CTOOL_FALSE,
     9u, 2u, 2u, 0u},
    {"HOSTED_I386_KERNEL_BRIDGE", CTOOL_C_PP_MODE_C11, CTOOL_FALSE, CTOOL_TRUE, CTOOL_FALSE,
     CTOOL_FALSE, 2u, 3u, 1u, 0u},
    {"HOSTED_I386_LINUX_GNU", CTOOL_C_PP_MODE_C11, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE, CTOOL_FALSE,
     3u, 2u, 1u, 0u},
    {"HOSTED_I386_WINDOWS_UTF8", CTOOL_C_PP_MODE_C11, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE,
     CTOOL_FALSE, 1u, 2u, 2u, 0u},
    {"HOSTED_I386_WINDOWS_PUBLICATION", CTOOL_C_PP_MODE_C11, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE,
     CTOOL_FALSE, 1u, 2u, 3u, 0u},
    {"HOSTED_I386_WINDOWS_BUILD", CTOOL_C_PP_MODE_C11, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE,
     CTOOL_FALSE, 1u, 2u, 3u, 0u},
    {"HOSTED_I386_WINDOWS_UTF8_GNU", CTOOL_C_PP_MODE_C11, CTOOL_TRUE, CTOOL_TRUE, CTOOL_FALSE,
     CTOOL_FALSE, 1u, 2u, 2u, 0u}};

static int open_job_at_root(const char *mode, const char *host_root, ctool_host_adapter_t *adapter,
                            ctool_job_t **job_out) {
    ctool_job_config_t config;
    ctool_status_t status = ctool_host_adapter_init(adapter, host_root);
    if (status != CTOOL_OK) {
        (void)fprintf(stderr, "%s: host adapter: %s\n", mode, ctool_status_name(status));
        return 1;
    }
    config = ctool_host_job_config(adapter, ctool_default_limits());
    status = ctool_job_open(&config, job_out);
    if (status != CTOOL_OK) {
        (void)fprintf(stderr, "%s: job open: %s\n", mode, ctool_status_name(status));
        return 1;
    }
    return 0;
}

static int open_job(const char *mode, ctool_host_adapter_t *adapter, ctool_job_t **job_out) {
    return open_job_at_root(mode, ".", adapter, job_out);
}

static int append_character(char *buffer, ctool_u32 capacity, ctool_u32 *size, char value) {
    if (*size == capacity) {
        return 1;
    }
    buffer[*size] = value;
    (*size)++;
    return 0;
}

static int append_text(char *buffer, ctool_u32 capacity, ctool_u32 *size, const char *text) {
    while (*text != '\0') {
        if (append_character(buffer, capacity, size, *text) != 0) {
            return 1;
        }
        text++;
    }
    return 0;
}

static int append_decimal(char *buffer, ctool_u32 capacity, ctool_u32 *size, ctool_u32 value) {
    char digits[10];
    ctool_u32 count = 0u;
    do {
        digits[count] = (char)('0' + (char)(value % 10u));
        count++;
        value /= 10u;
    } while (value != 0u);
    while (count != 0u) {
        count--;
        if (append_character(buffer, capacity, size, digits[count]) != 0) {
            return 1;
        }
    }
    return 0;
}

static int append_parameter_name(char *buffer, ctool_u32 capacity, ctool_u32 *size,
                                 ctool_u32 index) {
    return append_character(buffer, capacity, size, 'p') != 0 ||
                   append_character(buffer, capacity, size, (char)('0' + (char)(index / 100u))) !=
                       0 ||
                   append_character(buffer, capacity, size,
                                    (char)('0' + (char)((index / 10u) % 10u))) != 0 ||
                   append_character(buffer, capacity, size, (char)('0' + (char)(index % 10u))) != 0
               ? 1
               : 0;
}

typedef struct {
    ctool_string_t path;
    ctool_bytes_t contents;
} fixture_file_t;

typedef struct {
    const fixture_file_t *files;
    ctool_u32 file_count;
    ctool_u32 read_count;
} fixture_store_t;

static ctool_bool strings_equal(ctool_string_t left, ctool_string_t right) {
    return left.size == right.size &&
                   (left.size == 0u || memcmp(left.data, right.data, (size_t)left.size) == 0)
               ? CTOOL_TRUE
               : CTOOL_FALSE;
}

static ctool_status_t fixture_file_size(void *context, ctool_string_t logical_path,
                                        ctool_u32 *size_out) {
    fixture_store_t *store = (fixture_store_t *)context;
    ctool_u32 index;
    for (index = 0u; index < store->file_count; index++) {
        if (strings_equal(store->files[index].path, logical_path) == CTOOL_TRUE) {
            *size_out = store->files[index].contents.size;
            return CTOOL_OK;
        }
    }
    return CTOOL_ERR_NOT_FOUND;
}

static ctool_status_t fixture_read_exact(void *context, ctool_string_t logical_path,
                                         ctool_u8 *destination, ctool_u32 size) {
    fixture_store_t *store = (fixture_store_t *)context;
    ctool_u32 index;
    for (index = 0u; index < store->file_count; index++) {
        if (strings_equal(store->files[index].path, logical_path) == CTOOL_TRUE) {
            if (size != store->files[index].contents.size) {
                return CTOOL_ERR_IO;
            }
            if (size != 0u) {
                (void)memcpy(destination, store->files[index].contents.data, (size_t)size);
            }
            store->read_count++;
            return CTOOL_OK;
        }
    }
    return CTOOL_ERR_NOT_FOUND;
}

static ctool_status_t fixture_write_all(void *context, ctool_string_t logical_path,
                                        ctool_bytes_t contents) {
    (void)context;
    (void)logical_path;
    (void)contents;
    return CTOOL_ERR_UNSUPPORTED;
}

static int open_fixture_job(const char *mode, fixture_store_t *store, ctool_host_adapter_t *adapter,
                            ctool_job_t **job_out) {
    ctool_job_config_t config;
    ctool_status_t status = ctool_host_adapter_init(adapter, ".");
    if (status != CTOOL_OK) {
        (void)fprintf(stderr, "%s: host adapter: %s\n", mode, ctool_status_name(status));
        return 1;
    }
    config = ctool_host_job_config(adapter, ctool_default_limits());
    config.files.context = store;
    config.files.file_size = fixture_file_size;
    config.files.read_exact = fixture_read_exact;
    config.files.write_all = fixture_write_all;
    status = ctool_job_open(&config, job_out);
    if (status != CTOOL_OK) {
        (void)fprintf(stderr, "%s: job open: %s\n", mode, ctool_status_name(status));
        return 1;
    }
    return 0;
}

#include "cupidc_pp_source_contract.inc"

#include "cupidc_pp_directive_contract.inc"

#include "cupidc_pp_macro_contract.inc"

#include "cupidc_pp_include_contract.inc"

static ctool_u32 active_row_count(void) {
    return (ctool_u32)(sizeof(active_rows) / sizeof(active_rows[0]));
}

static int active_profile_index(const char *name) {
    ctool_u32 index;
    for (index = 0u; index < (ctool_u32)(sizeof(active_expected_profiles) /
                                         sizeof(active_expected_profiles[0]));
         index++) {
        if (strcmp(name, active_expected_profiles[index].name) == 0) {
            return (int)index;
        }
    }
    return -1;
}

static int active_path_is_canonical(const char *text) {
    ctool_path_t path;
    if (text == NULL || text[0] != '/') {
        return 0;
    }
    path.text = ctool_string(text);
    return ctool_path_is_canonical(&path) == CTOOL_TRUE ? 1 : 0;
}

static int active_rows_match(const active_row_t *left, const active_row_t *right) {
    if (left->kind != right->kind) {
        return 0;
    }
    if (left->profile != NULL && right->profile != NULL &&
        strcmp(left->profile, right->profile) != 0) {
        return 0;
    }
    if ((left->profile == NULL) != (right->profile == NULL)) {
        return 0;
    }
    return left->first != NULL && right->first != NULL && strcmp(left->first, right->first) == 0
               ? 1
               : 0;
}

static int validate_active_manifest(const char *mode) {
    static const char *const generated_paths[] = {
        "/kernel/cpu/ksyms_data.cc", "/kernel/util/bin_programs_gen.cc",
        "/kernel/util/demos_programs_gen.cc", "/kernel/util/docs_programs_gen.cc"};
    ctool_u32 kind_counts[ACTIVE_ROW_DEFERRED_HOSTED + 1u];
    ctool_u32
        profile_case_counts[sizeof(active_expected_profiles) / sizeof(active_expected_profiles[0])];
    ctool_u32 profile_include_root_counts[sizeof(active_expected_profiles) /
                                          sizeof(active_expected_profiles[0])];
    ctool_u32 profile_macro_counts[sizeof(active_expected_profiles) /
                                   sizeof(active_expected_profiles[0])];
    ctool_u32 profile_forced_include_counts[sizeof(active_expected_profiles) /
                                            sizeof(active_expected_profiles[0])];
    active_row_kind_t previous_kind = ACTIVE_ROW_PROFILE;
    ctool_u32 profile_count = 0u;
    ctool_u32 generated_index = 0u;
    ctool_u32 row_index;

    (void)memset(kind_counts, 0, sizeof(kind_counts));
    (void)memset(profile_case_counts, 0, sizeof(profile_case_counts));
    (void)memset(profile_include_root_counts, 0, sizeof(profile_include_root_counts));
    (void)memset(profile_macro_counts, 0, sizeof(profile_macro_counts));
    (void)memset(profile_forced_include_counts, 0, sizeof(profile_forced_include_counts));
    for (row_index = 0u; row_index < active_row_count(); row_index++) {
        const active_row_t *row = &active_rows[row_index];
        int profile_index = -1;
        ctool_u32 earlier;

        if (row->kind < ACTIVE_ROW_PROFILE || row->kind > ACTIVE_ROW_DEFERRED_HOSTED ||
            row->kind < previous_kind) {
            (void)fprintf(stderr, "%s: manifest row %u has invalid ordering\n", mode, row_index);
            return 1;
        }
        previous_kind = row->kind;
        kind_counts[(ctool_u32)row->kind]++;

        if (row->kind <= ACTIVE_ROW_GENERATED_CASE) {
            if (row->profile == NULL) {
                (void)fprintf(stderr, "%s: manifest row %u has no profile\n", mode, row_index);
                return 1;
            }
            profile_index = active_profile_index(row->profile);
            if (profile_index < 0) {
                (void)fprintf(stderr, "%s: manifest row %u has unknown profile %s\n", mode,
                              row_index, row->profile);
                return 1;
            }
        } else if (row->profile != NULL) {
            (void)fprintf(stderr, "%s: manifest row %u has a stray profile\n", mode, row_index);
            return 1;
        }

        if (row->kind == ACTIVE_ROW_PROFILE) {
            const active_expected_profile_t *expected;
            if (profile_count >= (ctool_u32)(sizeof(active_expected_profiles) /
                                             sizeof(active_expected_profiles[0]))) {
                (void)fprintf(stderr, "%s: manifest has too many profiles\n", mode);
                return 1;
            }
            expected = &active_expected_profiles[profile_count];
            if (strcmp(row->profile, expected->name) != 0 || row->first != NULL ||
                row->second != NULL || row->value != (ctool_u32)expected->mode ||
                row->flag != expected->gnu_extensions ||
                row->hosted_environment != expected->hosted_environment ||
                row->implicit_function_declarations != expected->implicit_function_declarations ||
                row->compatibility_pointer_conversions !=
                    expected->compatibility_pointer_conversions) {
                (void)fprintf(stderr, "%s: manifest profile %u metadata differs\n", mode,
                              profile_count);
                return 1;
            }
            profile_count++;
        } else if (row->kind == ACTIVE_ROW_INCLUDE_ROOT) {
            if (active_path_is_canonical(row->first) == 0 || row->second != NULL ||
                row->flag != CTOOL_FALSE || row->hosted_environment != CTOOL_FALSE ||
                row->implicit_function_declarations != CTOOL_FALSE ||
                row->compatibility_pointer_conversions != CTOOL_FALSE || row->value == 0u ||
                (row->value & ~(CTOOL_C_PP_INCLUDE_QUOTED | CTOOL_C_PP_INCLUDE_ANGLE)) != 0u) {
                (void)fprintf(stderr, "%s: invalid include-root row %u\n", mode, row_index);
                return 1;
            }
            profile_include_root_counts[(ctool_u32)profile_index]++;
        } else if (row->kind == ACTIVE_ROW_MACRO) {
            if (row->first == NULL || row->first[0] == '\0' || row->second == NULL ||
                row->value != 0u || row->flag != CTOOL_FALSE ||
                row->hosted_environment != CTOOL_FALSE ||
                row->implicit_function_declarations != CTOOL_FALSE ||
                row->compatibility_pointer_conversions != CTOOL_FALSE) {
                (void)fprintf(stderr, "%s: invalid macro row %u\n", mode, row_index);
                return 1;
            }
            profile_macro_counts[(ctool_u32)profile_index]++;
        } else if (row->kind == ACTIVE_ROW_FORCED_INCLUDE || row->kind == ACTIVE_ROW_CASE) {
            if (active_path_is_canonical(row->first) == 0 || row->second != NULL ||
                row->value != 0u || row->flag != CTOOL_FALSE ||
                row->hosted_environment != CTOOL_FALSE ||
                row->implicit_function_declarations != CTOOL_FALSE ||
                row->compatibility_pointer_conversions != CTOOL_FALSE) {
                (void)fprintf(stderr, "%s: invalid source row %u\n", mode, row_index);
                return 1;
            }
            if (row->kind == ACTIVE_ROW_CASE) {
                profile_case_counts[(ctool_u32)profile_index]++;
            } else {
                profile_forced_include_counts[(ctool_u32)profile_index]++;
            }
        } else {
            if (active_path_is_canonical(row->first) == 0 || row->value != 0u ||
                row->flag != CTOOL_FALSE || row->hosted_environment != CTOOL_FALSE ||
                row->implicit_function_declarations != CTOOL_FALSE ||
                row->compatibility_pointer_conversions != CTOOL_FALSE) {
                (void)fprintf(stderr, "%s: invalid classification row %u\n", mode, row_index);
                return 1;
            }
            if (row->kind == ACTIVE_ROW_GENERATED_CASE) {
                if (strcmp(row->profile, "KERNEL_I386") != 0 || row->second != NULL ||
                    generated_index >=
                        (ctool_u32)(sizeof(generated_paths) / sizeof(generated_paths[0])) ||
                    strcmp(row->first, generated_paths[generated_index]) != 0) {
                    (void)fprintf(stderr, "%s: generated source metadata differs at row %u\n", mode,
                                  row_index);
                    return 1;
                }
                generated_index++;
            } else if (row->second == NULL || row->second[0] == '\0') {
                (void)fprintf(stderr, "%s: classification row %u has no owner/reason\n", mode,
                              row_index);
                return 1;
            }
        }

        for (earlier = 0u; earlier < row_index; earlier++) {
            if (active_rows_match(row, &active_rows[earlier]) != 0) {
                (void)fprintf(stderr, "%s: duplicate manifest row %u\n", mode, row_index);
                return 1;
            }
        }
    }

    if (profile_count !=
            (ctool_u32)(sizeof(active_expected_profiles) / sizeof(active_expected_profiles[0])) ||
        kind_counts[ACTIVE_ROW_PROFILE] != 16u || kind_counts[ACTIVE_ROW_CASE] != 428u ||
        kind_counts[ACTIVE_ROW_GENERATED_CASE] != 4u ||
        kind_counts[ACTIVE_ROW_INCLUDE_ONLY] != 24u || kind_counts[ACTIVE_ROW_NON_ROOT] != 3u ||
        kind_counts[ACTIVE_ROW_DEFERRED_HOSTED] != 0u) {
        (void)fprintf(stderr,
                      "%s: manifest counts differ "
                      "(profiles=%u tracked=%u generated=%u include-only=%u "
                      "non-root=%u deferred=%u)\n",
                      mode, kind_counts[ACTIVE_ROW_PROFILE], kind_counts[ACTIVE_ROW_CASE],
                      kind_counts[ACTIVE_ROW_GENERATED_CASE], kind_counts[ACTIVE_ROW_INCLUDE_ONLY],
                      kind_counts[ACTIVE_ROW_NON_ROOT], kind_counts[ACTIVE_ROW_DEFERRED_HOSTED]);
        return 1;
    }

    for (row_index = 0u; row_index < (ctool_u32)(sizeof(active_expected_profiles) /
                                                 sizeof(active_expected_profiles[0]));
         row_index++) {
        if (profile_case_counts[row_index] !=
                active_expected_profiles[row_index].tracked_case_count ||
            profile_include_root_counts[row_index] !=
                active_expected_profiles[row_index].include_root_count ||
            profile_macro_counts[row_index] != active_expected_profiles[row_index].macro_count ||
            profile_forced_include_counts[row_index] !=
                active_expected_profiles[row_index].forced_include_count) {
            (void)fprintf(stderr,
                          "%s: profile %s metadata differs "
                          "(cases=%u roots=%u macros=%u forced=%u)\n",
                          mode, active_expected_profiles[row_index].name,
                          profile_case_counts[row_index], profile_include_root_counts[row_index],
                          profile_macro_counts[row_index],
                          profile_forced_include_counts[row_index]);
            return 1;
        }
    }
    return 0;
}

static int build_active_request(const char *mode, const char *profile_name,
                                ctool_c_pp_request_t *request,
                                ctool_c_pp_include_root_t *include_roots,
                                ctool_c_pp_macro_action_t *macro_actions,
                                ctool_path_t *forced_includes) {
    ctool_u32 row_index;
    ctool_u32 profile_count = 0u;

    (void)memset(request, 0, sizeof(*request));
    for (row_index = 0u; row_index < active_row_count(); row_index++) {
        const active_row_t *row = &active_rows[row_index];
        if (row->profile == NULL || strcmp(row->profile, profile_name) != 0) {
            continue;
        }
        if (row->kind == ACTIVE_ROW_PROFILE) {
            request->mode = (ctool_c_pp_mode_t)row->value;
            request->gnu_extensions = row->flag;
            request->hosted_environment = row->hosted_environment;
            profile_count++;
        } else if (row->kind == ACTIVE_ROW_INCLUDE_ROOT) {
            include_roots[request->include_root_count].directory.text = ctool_string(row->first);
            include_roots[request->include_root_count].forms = row->value;
            request->include_root_count++;
        } else if (row->kind == ACTIVE_ROW_MACRO) {
            macro_actions[request->macro_action_count].kind = CTOOL_C_PP_MACRO_DEFINE;
            macro_actions[request->macro_action_count].name = ctool_string(row->first);
            macro_actions[request->macro_action_count].replacement = ctool_string(row->second);
            request->macro_action_count++;
        } else if (row->kind == ACTIVE_ROW_FORCED_INCLUDE) {
            forced_includes[request->forced_include_count].text = ctool_string(row->first);
            request->forced_include_count++;
        }
    }
    if (profile_count != 1u) {
        (void)fprintf(stderr, "%s: profile %s request metadata is incomplete\n", mode,
                      profile_name);
        return 1;
    }
    if (request->hosted_environment == CTOOL_TRUE &&
        (strcmp(profile_name, "HOSTED_TOOLCHAIN_64") == 0 ||
         strcmp(profile_name, "HOSTED_KERNEL_BRIDGE_64") == 0) &&
        sizeof(void *) != 8u) {
        (void)fprintf(stderr,
                      "%s: profile %s requires an eight-byte bootstrap-host "
                      "pointer (found %u)\n",
                      mode, profile_name, (unsigned int)sizeof(void *));
        return 1;
    }
    request->include_roots = include_roots;
    request->macro_actions = macro_actions;
    request->forced_includes = forced_includes;
    return 0;
}

static int active_pack_alignment_is_valid(ctool_u32 alignment) {
    return alignment == 0u || alignment == 1u || alignment == 2u || alignment == 4u ||
                   alignment == 8u || alignment == 16u
               ? 1
               : 0;
}

static int active_token_is_valid(const ctool_c_pp_token_t *token, ctool_c_pp_mode_t profile_mode) {
    ctool_path_t path;
    if (token->kind < CTOOL_C_PP_TOKEN_IDENTIFIER || token->kind > CTOOL_C_PP_TOKEN_CUPID_EXE ||
        token->spelling.data == NULL || token->spelling.size == 0u ||
        token->location.path.data == NULL || token->location.path.size == 0u ||
        token->location.path.data[0] != '/' || token->location.line == 0u ||
        token->location.column == 0u || token->physical_location.path.data == NULL ||
        token->physical_location.path.size == 0u || token->physical_location.path.data[0] != '/' ||
        token->physical_location.line == 0u || token->physical_location.column == 0u ||
        strings_equal(token->location.path, token->physical_location.path) == CTOOL_FALSE ||
        token->location.line != token->physical_location.line ||
        token->location.column != token->physical_location.column ||
        active_pack_alignment_is_valid(token->pack_alignment) == 0) {
        return 0;
    }
    path.text = token->location.path;
    if (ctool_path_is_canonical(&path) != CTOOL_TRUE) {
        return 0;
    }
    path.text = token->physical_location.path;
    if (ctool_path_is_canonical(&path) != CTOOL_TRUE) {
        return 0;
    }
    if (token->kind == CTOOL_C_PP_TOKEN_CUPID_EXE &&
        (profile_mode != CTOOL_C_PP_MODE_CUPID || !string_equal(token->spelling, "exe"))) {
        return 0;
    }
    return 1;
}

static int run_one_active_case(const char *mode, const char *host_root, const char *profile_name,
                               const char *logical_path) {
    ctool_c_pp_include_root_t include_roots[sizeof(active_rows) / sizeof(active_rows[0])];
    ctool_c_pp_macro_action_t macro_actions[sizeof(active_rows) / sizeof(active_rows[0])];
    ctool_path_t forced_includes[sizeof(active_rows) / sizeof(active_rows[0])];
    ctool_host_adapter_t adapter;
    ctool_job_config_t config;
    ctool_limits_t limits = ctool_default_limits();
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_path_t path;
    ctool_source_t source;
    ctool_c_pp_request_t request;
    ctool_c_pp_result_t result;
    ctool_status_t status;
    ctool_u32 token_index;

    if (build_active_request(mode, profile_name, &request, include_roots, macro_actions,
                             forced_includes) != 0) {
        return 1;
    }
    limits.arena_bytes = 256u * 1024u * 1024u;
    status = ctool_host_adapter_init(&adapter, host_root);
    if (status != CTOOL_OK) {
        (void)fprintf(stderr, "%s: %s %s: %s (host adapter)\n", mode, profile_name, logical_path,
                      ctool_status_name(status));
        return 1;
    }
    config = ctool_host_job_config(&adapter, limits);
    status = ctool_job_open(&config, &job);
    if (status != CTOOL_OK) {
        (void)fprintf(stderr, "%s: %s %s: %s (job open)\n", mode, profile_name, logical_path,
                      ctool_status_name(status));
        return 1;
    }

    path.text = ctool_string(logical_path);
    status = ctool_job_load_source(job, &path, &source);
    if (status != CTOOL_OK) {
        (void)fprintf(stderr, "%s: %s %s: %s (source load)\n", mode, profile_name, logical_path,
                      ctool_status_name(status));
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }

    (void)memset(&result, 0, sizeof(result));
    status = ctool_c_preprocess(job, &source, &request, &result);
    if (status != CTOOL_OK || ((result.token_count == 0u) != (result.tokens == NULL)) ||
        ctool_job_diagnostic_count(job) != 0u) {
        (void)fprintf(stderr, "%s: %s %s: %s (%u tokens, %u diagnostics)\n", mode, profile_name,
                      logical_path, ctool_status_name(status), result.token_count,
                      ctool_job_diagnostic_count(job));
        (void)ctool_job_render_diagnostics(job);
        ctool_job_close(job);
        return 1;
    }
    for (token_index = 0u; token_index < result.token_count; token_index++) {
        if (active_token_is_valid(&result.tokens[token_index], request.mode) == 0) {
            (void)fprintf(stderr, "%s: %s %s: %s (invalid public token %u)\n", mode, profile_name,
                          logical_path, ctool_status_name(status), token_index);
            (void)ctool_job_render_diagnostics(job);
            ctool_job_close(job);
            return 1;
        }
    }

    ctool_job_close(job);
    return 0;
}

static int run_active_corpus(const char *mode, const char *host_root, ctool_bool generated) {
    ctool_u32 expected_count = generated == CTOOL_TRUE ? 4u : 428u;
    ctool_u32 executed_count = 0u;
    ctool_u32 row_index;

    if (validate_active_manifest(mode) != 0) {
        return 1;
    }
    for (row_index = 0u; row_index < active_row_count(); row_index++) {
        const active_row_t *row = &active_rows[row_index];
        const char *profile_name;
        if ((generated == CTOOL_TRUE && row->kind != ACTIVE_ROW_GENERATED_CASE) ||
            (generated == CTOOL_FALSE && row->kind != ACTIVE_ROW_CASE)) {
            continue;
        }
        profile_name = row->profile;
        if (run_one_active_case(mode, host_root, profile_name, row->first) != 0) {
            return 1;
        }
        executed_count++;
    }
    if (executed_count != expected_count) {
        (void)fprintf(stderr, "%s: executed %u roots, expected %u\n", mode, executed_count,
                      expected_count);
        return 1;
    }
    (void)printf("%s: ok\n", mode);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3 ||
        (argc == 3 && strcmp(argv[1], "macro-active-cases") != 0 &&
         strcmp(argv[1], "cupid-exe-active") != 0 && strcmp(argv[1], "active-corpus") != 0 &&
         strcmp(argv[1], "active-corpus-generated") != 0)) {
        (void)fprintf(stderr,
                      "usage: cupidc-pp-contract phases|tokens|errors|unsupported|"
                      "line-directives|line-files|line-errors|line-scale|"
                      "conditional-expressions|predefined|predefined-files|predefined-errors|"
                      "pragmas|pragma-files|pragma-errors|pragma-scale|"
                      "cupid-exe|cupid-exe-active <repo-root>|cupid-exe-files|"
                      "cupid-exe-errors|"
                      "cupid-exe-scale|"
                      "conditional-errors|"
                      "conditional-active|conditional-scale|"
                      "function-macros|function-scale|macro-operators|macro-gnu|"
                      "macro-operator-scale|macro-active-cases <toolchain-root>|"
                      "macro-operator-errors|"
                      "function-errors|object-includes|include-macros|"
                      "include-macro-errors|include-macro-scale|"
                      "active-corpus <repo-root>|"
                      "active-corpus-generated <repo-root>|"
                      "directive-errors|limits\n");
        return 2;
    }
    if (strcmp(argv[1], "phases") == 0) {
        return run_phases();
    }
    if (strcmp(argv[1], "tokens") == 0) {
        return run_tokens();
    }
    if (strcmp(argv[1], "errors") == 0) {
        return run_errors();
    }
    if (strcmp(argv[1], "unsupported") == 0) {
        return run_unsupported();
    }
    if (strcmp(argv[1], "line-directives") == 0) {
        return run_line_directives();
    }
    if (strcmp(argv[1], "line-files") == 0) {
        return run_line_files();
    }
    if (strcmp(argv[1], "line-errors") == 0) {
        return run_line_errors();
    }
    if (strcmp(argv[1], "line-scale") == 0) {
        return run_line_scale();
    }
    if (strcmp(argv[1], "conditional-expressions") == 0) {
        return run_conditional_expressions();
    }
    if (strcmp(argv[1], "predefined") == 0) {
        return run_predefined_macros();
    }
    if (strcmp(argv[1], "predefined-files") == 0) {
        return run_predefined_files();
    }
    if (strcmp(argv[1], "predefined-errors") == 0) {
        return run_predefined_errors();
    }
    if (strcmp(argv[1], "pragmas") == 0) {
        return run_pragmas();
    }
    if (strcmp(argv[1], "pragma-files") == 0) {
        return run_pragma_files();
    }
    if (strcmp(argv[1], "pragma-errors") == 0) {
        return run_pragma_errors();
    }
    if (strcmp(argv[1], "pragma-scale") == 0) {
        return run_pragma_scale();
    }
    if (strcmp(argv[1], "cupid-exe") == 0) {
        return run_cupid_exe();
    }
    if (strcmp(argv[1], "cupid-exe-active") == 0) {
        if (argc != 3) {
            (void)fprintf(stderr, "cupid-exe-active requires the repository root\n");
            return 2;
        }
        return run_cupid_exe_active(argv[2]);
    }
    if (strcmp(argv[1], "cupid-exe-files") == 0) {
        return run_cupid_exe_files();
    }
    if (strcmp(argv[1], "cupid-exe-errors") == 0) {
        return run_cupid_exe_errors();
    }
    if (strcmp(argv[1], "cupid-exe-scale") == 0) {
        return run_cupid_exe_scale();
    }
    if (strcmp(argv[1], "conditional-errors") == 0) {
        return run_conditional_expression_errors();
    }
    if (strcmp(argv[1], "conditional-active") == 0) {
        return run_conditional_active_cases();
    }
    if (strcmp(argv[1], "conditional-scale") == 0) {
        return run_conditional_expression_scale();
    }
    if (strcmp(argv[1], "function-macros") == 0) {
        return run_function_macros();
    }
    if (strcmp(argv[1], "function-scale") == 0) {
        return run_function_macro_scale();
    }
    if (strcmp(argv[1], "macro-operators") == 0) {
        return run_macro_operators();
    }
    if (strcmp(argv[1], "macro-gnu") == 0) {
        return run_macro_gnu();
    }
    if (strcmp(argv[1], "macro-operator-scale") == 0) {
        return run_macro_operator_scale();
    }
    if (strcmp(argv[1], "macro-active-cases") == 0) {
        if (argc != 3) {
            (void)fprintf(stderr, "macro-active-cases requires the toolchain root\n");
            return 2;
        }
        return run_macro_active_cases(argv[2]);
    }
    if (strcmp(argv[1], "macro-operator-errors") == 0) {
        return run_macro_operator_errors();
    }
    if (strcmp(argv[1], "function-errors") == 0) {
        return run_function_macro_errors();
    }
    if (strcmp(argv[1], "object-includes") == 0) {
        return run_object_includes();
    }
    if (strcmp(argv[1], "include-macros") == 0) {
        return run_include_macros();
    }
    if (strcmp(argv[1], "include-macro-errors") == 0) {
        return run_include_macro_errors();
    }
    if (strcmp(argv[1], "include-macro-scale") == 0) {
        return run_include_macro_scale();
    }
    if (strcmp(argv[1], "active-corpus") == 0) {
        if (argc != 3) {
            (void)fprintf(stderr, "active-corpus requires the repository root\n");
            return 2;
        }
        return run_active_corpus(argv[1], argv[2], CTOOL_FALSE);
    }
    if (strcmp(argv[1], "active-corpus-generated") == 0) {
        if (argc != 3) {
            (void)fprintf(stderr, "active-corpus-generated requires the repository root\n");
            return 2;
        }
        return run_active_corpus(argv[1], argv[2], CTOOL_TRUE);
    }
    if (strcmp(argv[1], "directive-errors") == 0) {
        return run_directive_errors();
    }
    if (strcmp(argv[1], "limits") == 0) {
        return run_limits();
    }
    (void)fprintf(stderr, "unknown mode: %s\n", argv[1]);
    return 2;
}
