/**
 * @file cupidbuild.cc
 * @brief Run checked toolchain operations and publish their artifacts.
 */

#include "cupidbuild.h"
#include "ctool.h"
#include "ctool_host.h"
#include "cupidbuild_host.h"
#include "elf32.h"
#include "pe32_impl.h"
#include "seed_manifest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CUPIDBUILD_PATH_BYTES 8192u
#define CUPIDBUILD_MANIFEST_BYTES 1048576u
#define CUPIDBUILD_TOOL_BYTES 67108864u
#define CUPIDBUILD_JSON_TOKENS 2048u
#define CUPIDBUILD_SEED_ARTIFACTS 6u
#define CUPIDBUILD_CODE_INPUTS 500u
#define CUPIDBUILD_COMPILE_INPUTS 90u
#define CUPIDBUILD_PROFILE_SNAPSHOT_BYTES 4194304u
#define CUPIDBUILD_PROFILE_TRANSACTION_INPUTS 512u
#define CUPIDBUILD_PROFILE_OUTPUT "build/bootstrap/doom-cupidc-inputs.json"
typedef cupid_seed_manifest_artifact_t cupidbuild_seed_artifact_t;

typedef struct {
    char manifest_path[CUPIDBUILD_PATH_BYTES];
    char directory[CUPIDBUILD_PATH_BYTES];
    cupidbuild_seed_artifact_t artifacts[CUPIDBUILD_SEED_ARTIFACTS];
    const char *frozen_tools[CUPIDBUILD_SEED_ARTIFACTS];
    cupidbuild_host_snapshot_t tool_snapshots[CUPIDBUILD_SEED_ARTIFACTS];
    const char *expected_files[CUPIDBUILD_SEED_ARTIFACTS];
    unsigned char *manifest;
    size_t manifest_size;
    size_t artifact_count;
    int current_windows_plan;
} cupidbuild_seed_capture_t;

static const char *const cupidbuild_seed_names[CUPIDBUILD_SEED_ARTIFACTS] = {
    "CupidASM", "CupidC", "CupidDis", "CupidLD", "CupidObj", "CupidBuild"};

typedef struct {
    unsigned int address;
    size_t order;
    size_t name_start;
    size_t name_size;
} cupidbuild_ksyms_symbol_t;

typedef struct {
    unsigned char *bytes;
    size_t size;
    size_t capacity;
} cupidbuild_ksyms_buffer_t;

typedef struct {
    unsigned int address;
    unsigned int order;
    ctool_bytes_t contents;
} cupidbuild_flat_region_t;

typedef struct {
    unsigned char *bytes;
    size_t size;
    size_t capacity;
} cupidbuild_profile_buffer_t;

typedef struct {
    const char *path;
    const char *frozen_path;
    size_t size;
    unsigned char sha256[32];
} cupidbuild_profile_header_t;

typedef struct {
    cupidbuild_host_path_list_t headers;
    cupidbuild_host_path_list_t sources;
} cupidbuild_profile_membership_t;

typedef struct {
    const char *source;
    const char *inputs[CUPIDBUILD_COMPILE_INPUTS];
    size_t count;
} cupidbuild_compile_closure_t;

typedef struct {
    const char *path;
    const char *frozen_path;
    cupidbuild_host_snapshot_t snapshot;
} cupidbuild_compile_input_t;

#include "cupidbuild_compile_inputs.inc"

static int cupidbuild_finish_publication(cupidbuild_host_transaction_t *transaction, int result,
                                         const char *artifact) {
    int committed = cupidbuild_host_publication_committed(transaction);
    if (cupidbuild_host_transaction_close(transaction)) {
        return result;
    }
    if (committed != 0) {
        (void)fprintf(stderr,
                      "cupidbuild: %s was published, but transaction cleanup was incomplete\n",
                      artifact);
        return result;
    }
    (void)fprintf(stderr, "cupidbuild: %s transaction cleanup failed\n", artifact);
    return 1;
}

static const char *const cupidbuild_profile_header_roots[] = {"drivers", "kernel", "toolchain"};
static const char *const cupidbuild_profile_header_suffixes[] = {".h", ".inc"};
static const char *const cupidbuild_profile_source_roots[] = {"kernel/doom"};
static const char *const cupidbuild_profile_source_suffixes[] = {".c", ".cc"};
static const char *const cupidbuild_profile_compat_sources[] = {
    "kernel/doom/dglibc.cc", "kernel/doom/doom_libc_stubs.cc",
    "kernel/doom/doomgeneric_cupidos.cc"};
static const char *const cupidbuild_profile_tree_sources[] = {
    "kernel/doom/i_sound_cupidos.cc", "kernel/doom/src/am_map.cc",
    "kernel/doom/src/d_event.cc",     "kernel/doom/src/d_items.cc",
    "kernel/doom/src/d_iwad.cc",      "kernel/doom/src/d_loop.cc",
    "kernel/doom/src/d_main.cc",      "kernel/doom/src/d_mode.cc",
    "kernel/doom/src/d_net.cc",       "kernel/doom/src/doomdef.cc",
    "kernel/doom/src/doomgeneric.cc", "kernel/doom/src/doomstat.cc",
    "kernel/doom/src/dstrings.cc",    "kernel/doom/src/dummy.cc",
    "kernel/doom/src/f_finale.cc",    "kernel/doom/src/f_wipe.cc",
    "kernel/doom/src/g_game.cc",      "kernel/doom/src/gusconf.cc",
    "kernel/doom/src/hu_lib.cc",      "kernel/doom/src/hu_stuff.cc",
    "kernel/doom/src/i_endoom.cc",    "kernel/doom/src/i_input.cc",
    "kernel/doom/src/i_joystick.cc",  "kernel/doom/src/i_scale.cc",
    "kernel/doom/src/i_system.cc",    "kernel/doom/src/i_timer.cc",
    "kernel/doom/src/i_video.cc",     "kernel/doom/src/icon.cc",
    "kernel/doom/src/info.cc",        "kernel/doom/src/m_argv.cc",
    "kernel/doom/src/m_bbox.cc",      "kernel/doom/src/m_cheat.cc",
    "kernel/doom/src/m_config.cc",    "kernel/doom/src/m_controls.cc",
    "kernel/doom/src/m_fixed.cc",     "kernel/doom/src/m_menu.cc",
    "kernel/doom/src/m_misc.cc",      "kernel/doom/src/m_random.cc",
    "kernel/doom/src/p_ceilng.cc",    "kernel/doom/src/p_doors.cc",
    "kernel/doom/src/p_enemy.cc",     "kernel/doom/src/p_floor.cc",
    "kernel/doom/src/p_inter.cc",     "kernel/doom/src/p_lights.cc",
    "kernel/doom/src/p_map.cc",       "kernel/doom/src/p_maputl.cc",
    "kernel/doom/src/p_mobj.cc",      "kernel/doom/src/p_plats.cc",
    "kernel/doom/src/p_pspr.cc",      "kernel/doom/src/p_saveg.cc",
    "kernel/doom/src/p_setup.cc",     "kernel/doom/src/p_sight.cc",
    "kernel/doom/src/p_spec.cc",      "kernel/doom/src/p_switch.cc",
    "kernel/doom/src/p_telept.cc",    "kernel/doom/src/p_tick.cc",
    "kernel/doom/src/p_user.cc",      "kernel/doom/src/r_bsp.cc",
    "kernel/doom/src/r_data.cc",      "kernel/doom/src/r_draw.cc",
    "kernel/doom/src/r_main.cc",      "kernel/doom/src/r_plane.cc",
    "kernel/doom/src/r_segs.cc",      "kernel/doom/src/r_sky.cc",
    "kernel/doom/src/r_things.cc",    "kernel/doom/src/s_sound.cc",
    "kernel/doom/src/sha1.cc",        "kernel/doom/src/sounds.cc",
    "kernel/doom/src/st_lib.cc",      "kernel/doom/src/st_stuff.cc",
    "kernel/doom/src/statdump.cc",    "kernel/doom/src/tables.cc",
    "kernel/doom/src/v_video.cc",     "kernel/doom/src/w_checksum.cc",
    "kernel/doom/src/w_file.cc",      "kernel/doom/src/w_file_stdc.cc",
    "kernel/doom/src/w_main.cc",      "kernel/doom/src/w_wad.cc",
    "kernel/doom/src/wi_stuff.cc",    "kernel/doom/src/z_zone.cc"};

typedef enum {
    CUPIDBUILD_KSYMS_EMPTY = 0,
    CUPIDBUILD_KSYMS_IGNORED,
    CUPIDBUILD_KSYMS_TEXT,
    CUPIDBUILD_KSYMS_OMITTED_ADDRESS,
    CUPIDBUILD_KSYMS_MALFORMED,
    CUPIDBUILD_KSYMS_INVALID_ADDRESS,
    CUPIDBUILD_KSYMS_ADDRESS_OUTSIDE_I386
} cupidbuild_ksyms_row_kind_t;

static int cupidbuild_user_separator(char c, int windows) {
    return c == '/' || (windows && c == '\\');
}

static unsigned char cupidbuild_user_fold(unsigned char c, int windows) {
    return windows && c >= 'A' && c <= 'Z' ? (unsigned char)(c + 32u) : c;
}

/* Canonical anchors end in '/'. Relative paths on another drive need an
 * external drive working directory and cannot be resolved against this root. */
static size_t cupidbuild_user_anchor(const char *path, int windows, char *out) {
    size_t i, size;
    if (windows && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
        path[1] == ':' && cupidbuild_user_separator(path[2], windows)) {
        out[0] = path[0];
        out[1] = ':';
        out[2] = '/';
        out[3] = 0;
        return 3u;
    }
    if (windows && cupidbuild_user_separator(path[0], windows) &&
        cupidbuild_user_separator(path[1], windows)) {
        i = 2u;
        size = 2u;
        out[0] = '/';
        out[1] = '/';
        for (unsigned int part = 0u; part < 2u; part++) {
            size_t start = i;
            while (path[i] && !cupidbuild_user_separator(path[i], windows)) {
                if (path[i] == ':' || path[i] == '?' || size + 2u >= CUPIDBUILD_USER_PATH_BYTES)
                    return 0u;
                out[size++] = path[i++];
            }
            if (i == start || (i - start == 1u && path[start] == '.') ||
                (i - start == 2u && path[start] == '.' && path[start + 1u] == '.'))
                return 0u;
            out[size++] = '/';
            if (part == 0u) {
                if (!path[i])
                    return 0u;
                i++;
            }
        }
        out[size] = 0;
        return size;
    }
    if (!windows && path[0] == '/') {
        out[0] = '/';
        out[1] = 0;
        /* POSIX preserves exactly two leading slashes, like normpath. */
        if (path[1] == '/' && path[2] != '/') {
            out[1] = '/';
            out[2] = 0;
            return 2u;
        }
        return 1u;
    }
    return 0u;
}

static int cupidbuild_user_normalize(const char *path, int windows, char *out) {
    size_t anchor = cupidbuild_user_anchor(path, windows, out);
    size_t size = anchor, i = anchor;
    if (!anchor)
        return 0;
    /* A UNC share can end without the canonical anchor's final slash. */
    if (i > strlen(path))
        i = strlen(path);
    while (path[i]) {
        size_t start, length;
        while (cupidbuild_user_separator(path[i], windows))
            i++;
        start = i;
        while (path[i] && !cupidbuild_user_separator(path[i], windows))
            i++;
        length = i - start;
        if (!length || (length == 1u && path[start] == '.'))
            continue;
        if (length == 2u && path[start] == '.' && path[start + 1u] == '.') {
            if (size > anchor) {
                while (size > anchor && out[size - 1u] != '/')
                    size--;
                if (size > anchor)
                    size--;
            }
            continue;
        }
        if (size + length + 2u > CUPIDBUILD_USER_PATH_BYTES)
            return 0;
        if (size > anchor)
            out[size++] = '/';
        (void)memcpy(out + size, path + start, length);
        size += length;
    }
    out[size] = 0;
    return 1;
}

static int cupidbuild_user_relative(const char *root, const char *path, int windows, char *out,
                                    char *scratch) {
    char anchor[CUPIDBUILD_USER_PATH_BYTES];
    size_t root_size = strlen(root), i, start;
    if (strlen(path) >= CUPIDBUILD_USER_PATH_BYTES)
        return 0;
    if (cupidbuild_user_anchor(path, windows, anchor)) {
        if (!cupidbuild_user_normalize(path, windows, scratch))
            return 0;
    } else {
        const char *relative = path;
        size_t prefix = root_size;
        if (windows && cupidbuild_user_separator(path[0], 1) &&
            cupidbuild_user_separator(path[1], 1))
            return 0;
        if (windows && path[0] && path[1] == ':') {
            if (root[1] != ':' || cupidbuild_user_fold((unsigned char)root[0], 1) !=
                                      cupidbuild_user_fold((unsigned char)path[0], 1))
                return 0;
            relative += 2;
        } else if (windows && cupidbuild_user_separator(path[0], 1)) {
            prefix = cupidbuild_user_anchor(root, 1, anchor);
            relative++;
        }
        if (prefix + strlen(relative) + 2u > sizeof(anchor))
            return 0;
        (void)memcpy(anchor, root, prefix);
        if (prefix == 0u || anchor[prefix - 1u] != '/')
            anchor[prefix++] = '/';
        (void)memcpy(anchor + prefix, relative, strlen(relative) + 1u);
        if (!cupidbuild_user_normalize(anchor, windows, scratch))
            return 0;
    }
    if (strlen(scratch) <= root_size)
        return 0;
    for (i = 0u; i < root_size; i++) {
        if (cupidbuild_user_fold((unsigned char)root[i], windows) !=
            cupidbuild_user_fold((unsigned char)scratch[i], windows))
            return 0;
    }
    start = root_size;
    if (root[root_size - 1u] != '/') {
        if (scratch[start] != '/')
            return 0;
        start++;
    }
    (void)memcpy(out, scratch + start, strlen(scratch + start) + 1u);
    return 1;
}

int cupidbuild_resolve_user_compile_paths(const char *root, const char *source, const char *output,
                                          int windows, cupidbuild_user_compile_paths_t *result,
                                          char *error, size_t error_capacity) {
    cupidbuild_user_compile_paths_t *paths = NULL;
    char *scratch = NULL;
    const char *reason = "invalid user path request";
    const char *name;
    const char *leaf;
    int ok = 0;
    if (result)
        (void)memset(result, 0, sizeof(*result));
    if (error_capacity && !error)
        return 0;
    if (error_capacity)
        error[0] = 0;
    if (!result || !root || !source || !output || !*root || !*source || !*output ||
        (windows != 0 && windows != 1) || strlen(root) >= CUPIDBUILD_USER_PATH_BYTES)
        goto done;
    paths = (cupidbuild_user_compile_paths_t *)calloc(1u, sizeof(*paths));
    scratch = (char *)malloc(CUPIDBUILD_USER_PATH_BYTES);
    if (!paths || !scratch) {
        reason = "cannot allocate user paths";
        goto done;
    }
    reason = "repository root must be an absolute bounded path";
    if (!cupidbuild_user_normalize(root, windows, paths->repository_root))
        goto done;
    reason = "user source or output leaves the repository or exceeds the path limit";
    if (!cupidbuild_user_relative(paths->repository_root, source, windows, paths->source,
                                  scratch) ||
        !cupidbuild_user_relative(paths->repository_root, output, windows, paths->output, scratch))
        goto done;
    reason = "source is outside the approved user cohort";
    if (strcmp(paths->source, "user/examples/cat.cc") != 0 &&
        strcmp(paths->source, "user/examples/hello.cc") != 0 &&
        strcmp(paths->source, "user/examples/ls.cc") != 0)
        goto done;
    reason = "user source and output binding differ";
    name = paths->source + strlen("user/examples/");
    leaf = strrchr(paths->output, '/');
    if (strncmp(paths->output, "user/", 5u) != 0 || !leaf || leaf == paths->output + 4u ||
        strncmp(paths->output, "user/examples/", 14u) == 0 ||
        strlen(leaf + 1u) != strlen(name) - 1u ||
        strncmp(leaf + 1u, name, strlen(name) - 2u) != 0 ||
        strcmp(leaf + strlen(name) - 1u, "o") != 0)
        goto done;
    *result = *paths;
    ok = 1;
done:
    free(scratch);
    free(paths);
    if (!ok && error_capacity)
        (void)snprintf(error, error_capacity, "%s", reason);
    return ok;
}

static int cupidbuild_path_safe(const char *path, int relative) {
    const char *cursor;
    if (path == (const char *)0 || path[0] == '\0' || strchr(path, '"') != 0) {
        return 0;
    }
    if (relative != 0 && (path[0] == '/' || path[0] == '\\' || strchr(path, ':') != 0)) {
        return 0;
    }
    cursor = path;
    while (*cursor != '\0') {
        const char *start = cursor;
        while (*cursor != '\0' && *cursor != '/' && *cursor != '\\') {
            cursor++;
        }
        if ((cursor - start == 1 && start[0] == '.') ||
            (cursor - start == 2 && start[0] == '.' && start[1] == '.')) {
            return 0;
        }
        if (*cursor != '\0') {
            cursor++;
        }
    }
    return 1;
}

static int cupidbuild_join(char *destination, size_t capacity, const char *left,
                           const char *right) {
    size_t left_size = strlen(left);
    size_t right_size = strlen(right);
    int separator =
        left_size != 0u && left[left_size - 1u] != '/' && left[left_size - 1u] != '\\' ? 1 : 0;
    if (left_size + (size_t)separator + right_size + 1u > capacity) {
        return 0;
    }
    (void)memcpy(destination, left, left_size);
    if (separator != 0) {
        destination[left_size++] = '/';
    }
    (void)memcpy(destination + left_size, right, right_size + 1u);
    return 1;
}

static int cupidbuild_repository_prefix(const char *path, const char *root) {
    size_t index;
    size_t root_size = strlen(root);
    for (index = 0u; index < root_size; index++) {
        char left = path[index];
        char right = root[index];
#if defined(_WIN32)
        if (left == '\\') {
            left = '/';
        }
        if (right == '\\') {
            right = '/';
        }
        if (left >= 'A' && left <= 'Z') {
            left = (char)(left - 'A' + 'a');
        }
        if (right >= 'A' && right <= 'Z') {
            right = (char)(right - 'A' + 'a');
        }
#endif
        if (left != right) {
            return 0;
        }
    }
    return path[root_size] == '/' || path[root_size] == '\\';
}

static int cupidbuild_manifest_directory(const char *manifest, char *directory, size_t capacity) {
    const char *cursor = manifest;
    const char *last = (const char *)0;
    size_t size;
    while (*cursor != '\0') {
        if (*cursor == '/' || *cursor == '\\') {
            last = cursor;
        }
        cursor++;
    }
    if (last == (const char *)0) {
        return 0;
    }
    size = (size_t)(last - manifest);
    if (size == 0u || size + 1u > capacity) {
        return 0;
    }
    (void)memcpy(directory, manifest, size);
    directory[size] = '\0';
    return 1;
}

static int cupidbuild_digest_matches_hex(const unsigned char digest[32], const char *hex) {
    static const char digits[] = "0123456789abcdef";
    size_t index;
    for (index = 0u; index < 32u; index++) {
        if (hex[index * 2u] != digits[digest[index] >> 4u] ||
            hex[index * 2u + 1u] != digits[digest[index] & 15u]) {
            return 0;
        }
    }
    return hex[64] == '\0';
}

static int cupidbuild_artifact_matches(const cupidbuild_host_snapshot_t *snapshot,
                                       const cupidbuild_seed_artifact_t *artifact) {
    return snapshot->present != 0 && snapshot->size == artifact->size &&
           cupidbuild_digest_matches_hex(snapshot->sha256, artifact->sha256);
}

#include "cupidbuild_validation.inc"

static void cupidbuild_seed_capture_close(cupidbuild_seed_capture_t *seed) {
    if (seed != (cupidbuild_seed_capture_t *)0) {
        free(seed->manifest);
        seed->manifest = (unsigned char *)0;
    }
}

static int cupidbuild_seed_manifest_path(const char *working_directory, const char *requested,
                                         int require_inside, char *resolved, size_t capacity) {
    size_t requested_size = strlen(requested);
    int absolute =
        requested[0] == '/' || requested[0] == '\\' || (requested_size > 1u && requested[1] == ':');
    if (absolute != 0) {
        if ((require_inside != 0 &&
             (requested_size <= strlen(working_directory) ||
              !cupidbuild_repository_prefix(requested, working_directory))) ||
            requested_size + 1u > capacity) {
            return 0;
        }
        (void)memcpy(resolved, requested, requested_size + 1u);
        return 1;
    }
    return cupidbuild_join(resolved, capacity, working_directory, requested);
}

static int cupidbuild_seed_validate_capture(cupidbuild_seed_capture_t *seed,
                                             const unsigned char *release, size_t release_size,
                                             int require_promoted) {
    char manifest_reason[128];
    cupid_seed_manifest_result_t parsed_manifest;
#if defined(_WIN32)
    unsigned int manifest_format = 2u;
#else
    unsigned int manifest_format = 1u;
#endif
    int manifest_valid;
    if (release) {
        manifest_valid = cupid_seed_manifest_validate_release(
            release, release_size, seed->manifest, seed->manifest_size, manifest_format,
            &parsed_manifest, manifest_reason, sizeof(manifest_reason));
    } else {
        manifest_valid = cupid_seed_manifest_validate(seed->manifest, seed->manifest_size,
                                                      manifest_format, &parsed_manifest,
                                                      manifest_reason, sizeof(manifest_reason));
    }
    if (!manifest_valid) {
        (void)fprintf(stderr, "cupidbuild: checked seed manifest is invalid: %s\n",
                      manifest_reason);
        return 0;
    }
    memcpy(seed->artifacts, parsed_manifest.artifacts, sizeof(seed->artifacts));
    seed->artifact_count = parsed_manifest.artifact_count;
    seed->current_windows_plan = (int)parsed_manifest.current_windows_plan;
    if (require_promoted != 0 && seed->artifact_count != CUPIDBUILD_SEED_ARTIFACTS) {
        (void)fprintf(stderr, "cupidbuild: checked tool runner requires a promoted "
                              "six-tool seed\n");
        return 0;
    }
    return 1;
}

static int cupidbuild_seed_freeze(cupidbuild_host_transaction_t *transaction,
                                  const char *working_directory, const char *requested_manifest,
                                  int require_inside, int require_promoted,
                                  const char *requested_release, cupidbuild_seed_capture_t *seed) {
    const char *frozen_manifest = (const char *)0;
    unsigned char *release = NULL;
    size_t release_size = 0u;
    size_t index;
    int accepted;
    (void)memset(seed, 0, sizeof(*seed));
    if (!cupidbuild_seed_manifest_path(working_directory, requested_manifest, require_inside,
                                       seed->manifest_path, sizeof(seed->manifest_path))) {
        (void)fprintf(stderr, "cupidbuild: checked seed manifest is outside the working "
                              "directory\n");
        return 0;
    }
    if (!cupidbuild_host_freeze_input(transaction, seed->manifest_path, "manifest.json",
                                      &frozen_manifest, (cupidbuild_host_snapshot_t *)0)) {
        (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
        return 0;
    }
    seed->manifest = cupidbuild_host_read_frozen_input(
        transaction, frozen_manifest, CUPIDBUILD_MANIFEST_BYTES, &seed->manifest_size);
    if (seed->manifest == (unsigned char *)0) {
        (void)fprintf(stderr, "cupidbuild: checked seed manifest is invalid: unreadable bytes\n");
        return 0;
    }
    if (requested_release != (const char *)0) {
        char release_path[CUPIDBUILD_PATH_BYTES];
        const char *frozen_release = (const char *)0;
        if (!cupidbuild_path_safe(requested_release, 0) ||
            !cupidbuild_seed_manifest_path(working_directory, requested_release, require_inside,
                                           release_path, sizeof(release_path))) {
            (void)fprintf(stderr, "cupidbuild: checked seed release path is invalid\n");
            return 0;
        }
        if (!cupidbuild_host_freeze_input(transaction, release_path, "seed-release.json",
                                          &frozen_release, (cupidbuild_host_snapshot_t *)0)) {
            (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
            return 0;
        }
        release =
            cupidbuild_host_read_frozen_input(transaction, frozen_release, 65536u, &release_size);
        if (release == (unsigned char *)0) {
            (void)fprintf(stderr, "cupidbuild: checked seed release is unreadable\n");
            return 0;
        }
    }
    accepted = cupidbuild_seed_validate_capture(seed, release, release_size, require_promoted);
    free(release);
    if (!accepted)
        return 0;
    if (!cupidbuild_manifest_directory(seed->manifest_path, seed->directory,
                                       sizeof(seed->directory))) {
        (void)fprintf(stderr, "cupidbuild: checked seed manifest is invalid: artifact "
                              "path is invalid\n");
        return 0;
    }
    for (index = 0u; index < seed->artifact_count; index++) {
        seed->expected_files[index] = seed->artifacts[index].file;
    }
    if (!cupidbuild_host_seed_members_exact(transaction, seed->directory,
#if defined(_WIN32)
                                            ".exe",
#else
                                            ".elf",
#endif
                                            seed->expected_files, seed->artifact_count)) {
        (void)fprintf(stderr, "cupidbuild: checked seed directory contains an unlisted "
                              "executable file\n");
        return 0;
    }
    for (index = 0u; index < seed->artifact_count; index++) {
        char live_path[CUPIDBUILD_PATH_BYTES];
        if (!cupidbuild_join(live_path, sizeof(live_path), seed->directory,
                             seed->artifacts[index].file) ||
            !cupidbuild_host_freeze_input(transaction, live_path, seed->artifacts[index].file,
                                          &seed->frozen_tools[index],
                                          &seed->tool_snapshots[index]) ||
            !cupidbuild_host_make_input_executable(transaction, seed->frozen_tools[index]) ||
            !cupidbuild_artifact_matches(&seed->tool_snapshots[index], &seed->artifacts[index])) {
            (void)fprintf(stderr, "cupidbuild: checked %s digest mismatch\n",
                          cupidbuild_seed_names[index]);
            return 0;
        }
    }
    for (index = 0u; index < seed->artifact_count; index++) {
        if (!cupidbuild_validate_execution_profile(
                transaction, seed->frozen_tools[index], index,
                seed->artifact_count == CUPIDBUILD_SEED_ARTIFACTS, seed->current_windows_plan)) {
            (void)fprintf(stderr, "cupidbuild: checked seed execution profile mismatch\n");
            return 0;
        }
    }
    return 1;
}

static int cupidbuild_seed_require_live(cupidbuild_host_transaction_t *transaction,
                                        const cupidbuild_seed_capture_t *seed) {
    if (!cupidbuild_host_seed_members_exact(transaction, seed->directory,
#if defined(_WIN32)
                                            ".exe",
#else
                                            ".elf",
#endif
                                            seed->expected_files, seed->artifact_count)) {
        (void)fprintf(stderr, "cupidbuild: checked seed directory membership changed "
                              "while checked tools ran\n");
        return 0;
    }
    if (!cupidbuild_host_require_inputs(transaction)) {
        (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
        return 0;
    }
    return 1;
}

typedef enum {
    CUPIDBUILD_ASSEMBLY_OBJECT,
    CUPIDBUILD_ASSEMBLY_BOOTLOADER,
    CUPIDBUILD_ASSEMBLY_SMP_TRAMPOLINE,
    CUPIDBUILD_ASSEMBLY_ISO_PATTERN
} cupidbuild_assembly_kind_t;

static const unsigned char cupidbuild_iso_pattern_map[] = "cupid.raw-map.v2\n"
                                                          "size 4096\n"
                                                          "base 0x00000000\n"
                                                          "edges 0\n"
                                                          "range 0x00000000 data\n";

static const unsigned char cupidbuild_smp_trampoline_map[] =
    "cupid.raw-map.v2\n"
    "size 4096\n"
    "base 0x00008000\n"
    "edges 6\n"
    "range 0x00000000 code16\n"
    "range 0x0000001f data\n"
    "range 0x00000210 code32\n"
    "range 0x00000254 data\n"
    "edge 0x00000017 far local 0x00000210 0x00008210 32 0x00000008\n"
    "edge 0x0000022f relative local 0x0000023a 0x0000823a 32 0x00000000\n"
    "edge 0x00000235 relative local 0x00000229 0x00008229 32 0x00000000\n"
    "edge 0x00000238 relative local 0x00000237 0x00008237 32 0x00000000\n"
    "edge 0x00000250 indirect unprovable - - unknown -\n"
    "edge 0x00000252 relative local 0x00000237 0x00008237 32 0x00000000\n";

static int cupidbuild_assemble(const cupidbuild_assembly_request_t *request,
                               cupidbuild_assembly_kind_t kind, const char *seed_release) {
    cupidbuild_host_transaction_t *transaction = (cupidbuild_host_transaction_t *)0;
    cupidbuild_seed_capture_t seed;
    const char *frozen_assembler = (const char *)0;
    const char *frozen_inspector = (const char *)0;
    cupidbuild_host_snapshot_t candidate_snapshot;
    cupidbuild_host_snapshot_t map_snapshot;
    unsigned char *candidate = (unsigned char *)0;
    unsigned char *map = (unsigned char *)0;
    const char *assembler_arguments[9];
    const char *inspector_arguments[9];
    int assembler_status;
    int inspector_status;
    int result = 1;
    (void)memset(&seed, 0, sizeof(seed));
    if (request == (const cupidbuild_assembly_request_t *)0 ||
        !cupidbuild_path_safe(request->repository_root, 0) ||
        !cupidbuild_path_safe(request->source, 1) || !cupidbuild_path_safe(request->output, 1) ||
        !cupidbuild_path_safe(request->seed_manifest, 0)) {
        (void)fprintf(stderr, "cupidbuild: invalid guarded assembly request\n");
        return 1;
    }
    if (!cupidbuild_host_transaction_open(request->repository_root, request->source,
                                          request->output, &transaction)) {
        (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
        goto done;
    }
    if (!cupidbuild_seed_freeze(transaction, request->repository_root, request->seed_manifest, 1, 0,
                                seed_release, &seed)) {
        goto done;
    }
    frozen_assembler = seed.frozen_tools[0];
    frozen_inspector = seed.frozen_tools[2];
    assembler_arguments[0] = "--caller-owned-output";
    assembler_arguments[1] = "-f";
    assembler_arguments[2] = kind == CUPIDBUILD_ASSEMBLY_OBJECT ? "elf32" : "bin";
    if (kind == CUPIDBUILD_ASSEMBLY_OBJECT) {
        assembler_arguments[3] = "-o";
        assembler_arguments[4] = cupidbuild_host_candidate(transaction);
        assembler_arguments[5] = cupidbuild_host_frozen_source(transaction);
        assembler_arguments[6] = (const char *)0;
    } else {
        assembler_arguments[3] = "--map";
        assembler_arguments[4] = cupidbuild_host_private_output(transaction);
        assembler_arguments[5] = "-o";
        assembler_arguments[6] = cupidbuild_host_candidate(transaction);
        assembler_arguments[7] = cupidbuild_host_frozen_source(transaction);
        assembler_arguments[8] = (const char *)0;
    }
    assembler_status =
        cupidbuild_host_run(transaction, frozen_assembler, assembler_arguments, 60000u);
    if (!cupidbuild_seed_require_live(transaction, &seed)) {
        goto done;
    }
    if (assembler_status != 0) {
        (void)fprintf(stderr, "cupidbuild: checked CupidASM failed\n");
        goto done;
    }
    if (!cupidbuild_host_capture_candidate(transaction, &candidate_snapshot, &candidate)) {
        (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
        goto done;
    }
    if (kind == CUPIDBUILD_ASSEMBLY_OBJECT) {
        if (!cupidbuild_validate_relocatable(candidate, candidate_snapshot.size, 1)) {
            (void)fprintf(stderr,
                          "cupidbuild: checked CupidASM relocatable object validation failed\n");
            goto done;
        }
    } else if ((kind == CUPIDBUILD_ASSEMBLY_BOOTLOADER && candidate_snapshot.size != 2560u) ||
               ((kind == CUPIDBUILD_ASSEMBLY_SMP_TRAMPOLINE ||
                 kind == CUPIDBUILD_ASSEMBLY_ISO_PATTERN) &&
                candidate_snapshot.size != 4096u)) {
        (void)fprintf(stderr, "cupidbuild: checked CupidASM raw output validation failed\n");
        goto done;
    }
    if (kind == CUPIDBUILD_ASSEMBLY_ISO_PATTERN) {
        size_t index;
        for (index = 0u; index < candidate_snapshot.size; index++) {
            if (candidate[index] != (unsigned char)(index & 0xffu)) {
                (void)fprintf(stderr, "cupidbuild: checked CupidASM ISO pattern differs\n");
                goto done;
            }
        }
    }
    free(candidate);
    candidate = (unsigned char *)0;
    if (!cupidbuild_host_require_candidate(transaction, &candidate_snapshot)) {
        (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
        goto done;
    }
    if (kind != CUPIDBUILD_ASSEMBLY_OBJECT) {
        if (!cupidbuild_host_capture_private_output(transaction, &map_snapshot, &map) ||
            map_snapshot.size == 0u) {
            (void)fprintf(stderr, "cupidbuild: checked CupidASM range map is missing or empty\n");
            goto done;
        }
        if (kind == CUPIDBUILD_ASSEMBLY_SMP_TRAMPOLINE &&
            (map_snapshot.size != sizeof(cupidbuild_smp_trampoline_map) - 1u ||
             memcmp(map, cupidbuild_smp_trampoline_map,
                    sizeof(cupidbuild_smp_trampoline_map) - 1u) != 0)) {
            (void)fprintf(stderr, "cupidbuild: checked CupidASM range map does not match "
                                  "the SMP layout policy\n");
            goto done;
        }
        if (kind == CUPIDBUILD_ASSEMBLY_ISO_PATTERN &&
            (map_snapshot.size != sizeof(cupidbuild_iso_pattern_map) - 1u ||
             memcmp(map, cupidbuild_iso_pattern_map, sizeof(cupidbuild_iso_pattern_map) - 1u) !=
                 0)) {
            (void)fprintf(stderr, "cupidbuild: checked CupidASM range map does not match "
                                  "the ISO pattern layout policy\n");
            goto done;
        }
        free(map);
        map = (unsigned char *)0;
        if (!cupidbuild_host_require_private_output(transaction, &map_snapshot)) {
            (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
            goto done;
        }
    }
    if (kind == CUPIDBUILD_ASSEMBLY_OBJECT) {
        inspector_arguments[0] = "--require-known";
        inspector_arguments[1] = "--require-local-targets";
        inspector_arguments[2] = "--require-code-anchors";
        inspector_arguments[3] = cupidbuild_host_candidate(transaction);
        inspector_arguments[4] = (const char *)0;
    } else {
        inspector_arguments[0] = "--raw";
        inspector_arguments[1] = "--range-map";
        inspector_arguments[2] = cupidbuild_host_private_output(transaction);
        inspector_arguments[3] = "--require-known";
        inspector_arguments[4] = "--require-local-targets";
        inspector_arguments[5] = "--require-source-edges";
        inspector_arguments[6] = cupidbuild_host_candidate(transaction);
        inspector_arguments[7] = (const char *)0;
    }
    inspector_status =
        cupidbuild_host_run(transaction, frozen_inspector, inspector_arguments, 60000u);
    if (!cupidbuild_seed_require_live(transaction, &seed)) {
        goto done;
    }
    if (!cupidbuild_host_require_candidate(transaction, &candidate_snapshot) ||
        (kind != CUPIDBUILD_ASSEMBLY_OBJECT &&
         !cupidbuild_host_require_private_output(transaction, &map_snapshot))) {
        (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
        goto done;
    }
    if (inspector_status != 0) {
        (void)fprintf(stderr, "cupidbuild: checked CupidDis failed\n");
        goto done;
    }
    if (kind == CUPIDBUILD_ASSEMBLY_ISO_PATTERN) {
        int changed;
        if (!cupidbuild_host_publish_if_changed(transaction, &changed)) {
            (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
            goto done;
        }
    } else if (!cupidbuild_host_require_publication_boundary(transaction) ||
               !cupidbuild_host_publish(transaction)) {
        (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
        goto done;
    }
    result = 0;

done:
    free(map);
    free(candidate);
    cupidbuild_seed_capture_close(&seed);
    return cupidbuild_finish_publication(transaction, result, "guarded assembly output");
}

int cupidbuild_assemble_object(const cupidbuild_assembly_request_t *request) {
    return cupidbuild_assemble_object_with_release(request, (const char *)0);
}

int cupidbuild_assemble_object_with_release(const cupidbuild_assembly_request_t *request,
                                            const char *seed_release) {
    return cupidbuild_assemble(request, CUPIDBUILD_ASSEMBLY_OBJECT, seed_release);
}

int cupidbuild_assemble_bootloader(const cupidbuild_assembly_request_t *request) {
    return cupidbuild_assemble_bootloader_with_release(request, (const char *)0);
}

int cupidbuild_assemble_bootloader_with_release(const cupidbuild_assembly_request_t *request,
                                                const char *seed_release) {
    return cupidbuild_assemble(request, CUPIDBUILD_ASSEMBLY_BOOTLOADER, seed_release);
}

int cupidbuild_assemble_smp_trampoline(const cupidbuild_assembly_request_t *request) {
    return cupidbuild_assemble_smp_trampoline_with_release(request, (const char *)0);
}

int cupidbuild_assemble_smp_trampoline_with_release(const cupidbuild_assembly_request_t *request,
                                                    const char *seed_release) {
    return cupidbuild_assemble(request, CUPIDBUILD_ASSEMBLY_SMP_TRAMPOLINE, seed_release);
}

int cupidbuild_assemble_iso_pattern(const cupidbuild_assembly_request_t *request) {
    return cupidbuild_assemble_iso_pattern_with_release(request, (const char *)0);
}

int cupidbuild_assemble_iso_pattern_with_release(const cupidbuild_assembly_request_t *request,
                                                 const char *seed_release) {
    return cupidbuild_assemble(request, CUPIDBUILD_ASSEMBLY_ISO_PATTERN, seed_release);
}

int cupidbuild_embed_jpeg(const cupidbuild_jpeg_request_t *request) {
    return cupidbuild_embed_jpeg_with_release(request, (const char *)0);
}

int cupidbuild_embed_jpeg_with_release(const cupidbuild_jpeg_request_t *request,
                                       const char *seed_release) {
    cupidbuild_host_transaction_t *transaction = (cupidbuild_host_transaction_t *)0;
    cupidbuild_seed_capture_t seed;
    cupidbuild_host_snapshot_t candidate_snapshot;
    unsigned char *candidate = (unsigned char *)0;
    unsigned char *source = (unsigned char *)0;
    size_t source_size = 0u;
    char jpeg_reason[160];
    const char *object_arguments[7];
    int object_status;
    int result = 1;
    (void)memset(&seed, 0, sizeof(seed));
    if (request == (const cupidbuild_jpeg_request_t *)0 ||
        !cupidbuild_path_safe(request->repository_root, 0) ||
        !cupidbuild_path_safe(request->source, 1) || !cupidbuild_path_safe(request->output, 1) ||
        !cupidbuild_path_safe(request->seed_manifest, 0)) {
        (void)fprintf(stderr, "cupidbuild: invalid JPEG embed request\n");
        return 1;
    }
    if (!cupidbuild_host_transaction_open(request->repository_root, request->source,
                                          request->output, &transaction)) {
        (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
        goto done;
    }
    if (!cupidbuild_seed_freeze(transaction, request->repository_root, request->seed_manifest, 1, 1,
                                seed_release, &seed)) {
        goto done;
    }
    object_arguments[0] = "wrap-jpeg";
    object_arguments[1] = cupidbuild_host_frozen_source(transaction);
    object_arguments[2] = "--identity";
    object_arguments[3] = request->source;
    object_arguments[4] = "-o";
    object_arguments[5] = cupidbuild_host_candidate(transaction);
    object_arguments[6] = (const char *)0;
    object_status =
        cupidbuild_host_run(transaction, seed.frozen_tools[4], object_arguments, 60000u);
    if (!cupidbuild_seed_require_live(transaction, &seed)) {
        goto done;
    }
    if (object_status != 0) {
        (void)fprintf(stderr, "cupidbuild: checked CupidObj failed\n");
        goto done;
    }
    if (!cupidbuild_host_capture_candidate(transaction, &candidate_snapshot, &candidate)) {
        (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
        goto done;
    }
    source =
        cupidbuild_host_read_frozen_input(transaction, cupidbuild_host_frozen_source(transaction),
                                          CUPIDBUILD_TOOL_BYTES, &source_size);
    if (source == (unsigned char *)0) {
        (void)fprintf(stderr, "cupidbuild: frozen JPEG input cannot be inspected\n");
        goto done;
    }
    if (!cupidbuild_validate_jpeg_object_bytes(candidate, candidate_snapshot.size, source,
                                               source_size, request->source)) {
        (void)fprintf(stderr, "cupidbuild: checked CupidObj JPEG object validation failed\n");
        goto done;
    }
    if (!cupidbuild_validate_jpeg_bytes(source, source_size, jpeg_reason, sizeof(jpeg_reason))) {
        (void)fprintf(stderr, "cupidbuild: independent JPEG validation failed: %s\n", jpeg_reason);
        goto done;
    }
    free(candidate);
    candidate = (unsigned char *)0;
    free(source);
    source = (unsigned char *)0;
    if (!cupidbuild_seed_require_live(transaction, &seed)) {
        goto done;
    }
    if (!cupidbuild_host_require_candidate(transaction, &candidate_snapshot)) {
        (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
        goto done;
    }
    if (!cupidbuild_host_require_publication_boundary(transaction) ||
        !cupidbuild_host_publish(transaction)) {
        (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
        goto done;
    }
    result = 0;

done:
    free(source);
    free(candidate);
    cupidbuild_seed_capture_close(&seed);
    return cupidbuild_finish_publication(transaction, result, "JPEG object");
}

#include "cupidbuild_kernel_image.inc"

#include "cupidbuild_compile_profile.inc"

static int cupidbuild_user_link_root_is_absolute(const char *root) {
    if (root == (const char *)0 || root[0] == '\0')
        return 0;
    if (cupidbuild_host_execution_format() == 2u) {
        return ((root[0] >= 'A' && root[0] <= 'Z') || (root[0] >= 'a' && root[0] <= 'z')) &&
               root[1] == ':' && (root[2] == '/' || root[2] == '\\');
    }
    return root[0] == '/';
}

static int cupidbuild_link_user_prepared(const cupidbuild_user_link_request_t *request,
                                         cupidbuild_host_output_parent_t *parent,
                                         const char *seed_release) {
    cupidbuild_host_transaction_t *transaction = (cupidbuild_host_transaction_t *)0;
    int own_parent = parent == (cupidbuild_host_output_parent_t *)0;
    cupidbuild_seed_capture_t seed;
    cupidbuild_host_snapshot_t candidate_snapshot;
    unsigned char *object = (unsigned char *)0;
    unsigned char *candidate = (unsigned char *)0;
    const char *separator;
    const char *name;
    const char *arguments[12];
    char reason[256];
    size_t object_size = 0u;
    size_t output_size;
    int changed = 0;
    int result = 1;
    (void)memset(&seed, 0, sizeof(seed));
    if (request == (const cupidbuild_user_link_request_t *)0 ||
        !cupidbuild_user_link_root_is_absolute(request->repository_root) ||
        !cupidbuild_path_safe(request->repository_root, 0) ||
        !cupidbuild_path_safe(request->seed_manifest, 0) ||
        !cupidbuild_path_safe(request->source, 1) || !cupidbuild_path_safe(request->output, 1) ||
        strchr(request->source, '\\') != (const char *)0 ||
        strchr(request->output, '\\') != (const char *)0 ||
        strncmp(request->output, "user/", 5u) != 0 ||
        strlen(request->output) >= CUPIDBUILD_PATH_BYTES - 2u) {
        (void)fprintf(stderr, "cupidbuild: invalid normalized user link request\n");
        return 1;
    }
    separator = strrchr(request->output, '/');
    name = separator + 1u;
    output_size = strlen(request->output);
    if (separator <= request->output + 5u ||
        (strcmp(name, "cat") != 0 && strcmp(name, "hello") != 0 && strcmp(name, "ls") != 0) ||
        strlen(request->source) != output_size + 2u ||
        memcmp(request->source, request->output, output_size) != 0 ||
        strcmp(request->source + output_size, ".o") != 0) {
        (void)fprintf(stderr, "cupidbuild: user link object and executable binding differ\n");
        return 1;
    }
    if (own_parent && !cupidbuild_host_output_parent_open_existing(request->repository_root,
                                                                   request->output, &parent)) {
        (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_output_parent_error(parent));
        goto done;
    }
    if (!cupidbuild_host_output_transaction_open(request->repository_root, request->source,
                                                 request->output, parent, &transaction)) {
        goto host_failure;
    }
    object =
        cupidbuild_host_read_frozen_input(transaction, cupidbuild_host_frozen_source(transaction),
                                          CUPIDBUILD_TOOL_BYTES, &object_size);
    if (!cupidbuild_validate_compiler_object_bytes(object, object_size)) {
        (void)fprintf(stderr, "cupidbuild: user input object validation failed\n");
        goto done;
    }
    free(object);
    object = (unsigned char *)0;
    if (!cupidbuild_seed_freeze(transaction, request->repository_root, request->seed_manifest, 1, 1,
                                seed_release, &seed) ||
        !cupidbuild_host_require_publication_boundary(transaction)) {
        goto host_failure;
    }
    arguments[0] = "--caller-owned-output";
    arguments[1] = "-m";
    arguments[2] = "elf_i386";
    arguments[3] = "--text-address";
    arguments[4] = "0x01C00000";
    arguments[5] = "--entry";
    arguments[6] = "_start";
    arguments[7] = "-o";
    arguments[8] = cupidbuild_host_candidate(transaction);
    arguments[9] = cupidbuild_host_frozen_source(transaction);
    arguments[10] = (const char *)0;
    if (cupidbuild_host_run(transaction, seed.frozen_tools[3], arguments, 60000u) != 0 ||
        !cupidbuild_seed_require_live(transaction, &seed)) {
        (void)fprintf(stderr, "cupidbuild: checked user CupidLD failed\n");
        goto host_failure;
    }
    if (!cupidbuild_host_capture_candidate(transaction, &candidate_snapshot, &candidate)) {
        goto host_failure;
    }
    if (!cupidbuild_validate_user_executable_bytes(candidate, candidate_snapshot.size, reason,
                                                   sizeof(reason))) {
        (void)fprintf(stderr, "cupidbuild: user executable validation failed: %s\n", reason);
        goto done;
    }
    free(candidate);
    candidate = (unsigned char *)0;
    arguments[0] = "--require-known";
    arguments[1] = "--require-local-targets";
    arguments[2] = "--require-code-anchors";
    arguments[3] = cupidbuild_host_candidate(transaction);
    arguments[4] = (const char *)0;
    if (cupidbuild_host_run(transaction, seed.frozen_tools[2], arguments, 60000u) != 0 ||
        !cupidbuild_seed_require_live(transaction, &seed)) {
        (void)fprintf(stderr, "cupidbuild: checked user CupidDis failed\n");
        goto host_failure;
    }
    if (!cupidbuild_host_require_candidate(transaction, &candidate_snapshot) ||
        !cupidbuild_host_require_publication_boundary(transaction) ||
        !cupidbuild_host_publish_if_changed(transaction, &changed)) {
        goto host_failure;
    }
    result = 0;
    goto done;
host_failure:
    (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
done:
    free(object);
    free(candidate);
    cupidbuild_seed_capture_close(&seed);
    result = cupidbuild_finish_publication(transaction, result, "user executable");
    if (own_parent && !cupidbuild_host_output_parent_close(parent)) {
        (void)fprintf(stderr, "cupidbuild: user link parent cleanup failed\n");
        return 1;
    }
    return result;
}

int cupidbuild_link_user_object(const cupidbuild_user_link_request_t *request) {
    return cupidbuild_link_user_object_with_release(request, (const char *)0);
}

int cupidbuild_link_user_object_with_release(const cupidbuild_user_link_request_t *request,
                                             const char *seed_release) {
    return cupidbuild_link_user_prepared(request, (cupidbuild_host_output_parent_t *)0,
                                         seed_release);
}

int cupidbuild_link_user(const cupidbuild_user_link_request_t *request) {
    return cupidbuild_link_user_with_release(request, (const char *)0);
}

int cupidbuild_link_user_with_release(const cupidbuild_user_link_request_t *request,
                                      const char *seed_release) {
    cupidbuild_host_output_parent_t *parent = (cupidbuild_host_output_parent_t *)0;
    cupidbuild_user_link_request_t physical;
    int result = 1;
    if (request == (const cupidbuild_user_link_request_t *)0) {
        (void)fprintf(stderr, "cupidbuild: invalid user link request\n");
        return 1;
    }
    if (!cupidbuild_host_output_parent_resolve_existing(request->repository_root, request->source,
                                                        request->output, &parent)) {
        (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_output_parent_error(parent));
    } else {
        physical = *request;
        physical.repository_root = cupidbuild_host_output_parent_resolved_root(parent);
        physical.source = cupidbuild_host_output_parent_resolved_source(parent);
        physical.output = cupidbuild_host_output_parent_resolved_output(parent);
        result = cupidbuild_link_user_prepared(&physical, parent, seed_release);
    }
    if (!cupidbuild_host_output_parent_close(parent)) {
        (void)fprintf(stderr, "cupidbuild: user link parent cleanup failed\n");
        return 1;
    }
    return result;
}

int cupidbuild_run_checked_tool_with_release(const cupidbuild_run_request_t *request,
                                             const char *seed_release) {
    cupidbuild_host_transaction_t *transaction = (cupidbuild_host_transaction_t *)0;
    cupidbuild_seed_capture_t seed;
    size_t tool_index;
    int status;
    int result = 1;
    (void)memset(&seed, 0, sizeof(seed));
    if (request == (const cupidbuild_run_request_t *)0 ||
        !cupidbuild_path_safe(request->working_directory, 0) ||
        !cupidbuild_path_safe(request->seed_manifest, 0) ||
        request->arguments == (const char *const *)0 || request->tool == (const char *)0 ||
        (strcmp(request->tool, "cupidc") != 0 && strcmp(request->tool, "cupidobj") != 0 &&
         strcmp(request->tool, "cupidld") != 0) ||
        request->timeout_seconds == 0u || request->timeout_seconds > 86400u) {
        (void)fprintf(stderr, "cupidbuild: invalid checked tool request\n");
        return 1;
    }
    if (strcmp(request->tool, "cupidc") == 0) {
        tool_index = 1u;
    } else if (strcmp(request->tool, "cupidld") == 0) {
        tool_index = 3u;
    } else {
        tool_index = 4u;
    }
    if (!cupidbuild_host_runner_open(request->working_directory, &transaction)) {
        (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
        goto done;
    }
    if (!cupidbuild_seed_freeze(transaction, request->working_directory, request->seed_manifest, 0,
                                1, seed_release, &seed)) {
        goto done;
    }
    if (!cupidbuild_host_require_frozen_inputs(transaction)) {
        (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
        goto done;
    }
    status = cupidbuild_host_run_captured(transaction, seed.frozen_tools[tool_index],
                                          request->arguments, request->timeout_seconds * 1000u);
    if (!cupidbuild_seed_require_live(transaction, &seed)) {
        goto done;
    }
    if (!cupidbuild_host_require_frozen_inputs(transaction)) {
        (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
        goto done;
    }
    if (status == -2) {
        (void)fprintf(stderr, "cupidbuild: checked %s timed out\n",
                      cupidbuild_seed_names[tool_index]);
        goto done;
    }
    if (status < 0) {
        (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
        goto done;
    }
    if (!cupidbuild_host_forward_captured(transaction)) {
        (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
        goto done;
    }
    result = status;

done:
    cupidbuild_seed_capture_close(&seed);
    if (!cupidbuild_host_transaction_close(transaction)) {
        (void)fprintf(stderr, "cupidbuild: private checked-tool cleanup failed\n");
        if (result == 0) {
            result = 1;
        }
    }
    return result;
}

int cupidbuild_run_checked_tool(const cupidbuild_run_request_t *request) {
    return cupidbuild_run_checked_tool_with_release(request, (const char *)0);
}

#include "cupidbuild_disk_image.inc"
