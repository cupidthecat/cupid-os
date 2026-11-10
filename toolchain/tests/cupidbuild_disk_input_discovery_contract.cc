#include "cupidbuild_disk_input_discovery.h"
#include "cupidbuild_source_path.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int digit(int value) {
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    return -1;
}

static char *decode(const char *text) {
    size_t length = strlen(text), index;
    char *result;
    if (length % 2u)
        return (char *)0;
    result = (char *)malloc(length / 2u + 1u);
    if (!result)
        return (char *)0;
    for (index = 0u; index < length; index += 2u) {
        int high = digit(text[index]), low = digit(text[index + 1u]);
        if (high < 0 || low < 0) {
            free(result);
            return (char *)0;
        }
        result[index / 2u] = (char)(high * 16 + low);
    }
    result[length / 2u] = 0;
    return result;
}

static int cleared(const void *view, size_t size) {
    const unsigned char *bytes = (const unsigned char *)view;
    size_t index;
    for (index = 0u; index < size; index++)
        if (bytes[index])
            return 0;
    return 1;
}

static void clobber(char *text) {
    if (text)
        (void)memset(text, 'X', strlen(text));
}

static int wait_line(void) {
    char line[8];
    return fgets(line, sizeof(line), stdin) != (char *)0;
}

static int parent_contract(int argc, char **argv) {
    cupidbuild_host_observer_t *left = (cupidbuild_host_observer_t *)0;
    cupidbuild_host_observer_t *right = (cupidbuild_host_observer_t *)0;
    cupidbuild_host_entry_kind_t kind;
    char *left_root, *right_root, *left_logical, *right_logical;
    int valid, same = 77, expected, reject;
    if (argc != 8)
        return 90;
    left_root = decode(argv[2]);
    right_root = decode(argv[3]);
    left_logical = decode(argv[4]);
    right_logical = decode(argv[5]);
    expected = (int)strtoull(argv[6], (char **)0, 10);
    reject = (int)strtoull(argv[7], (char **)0, 10);
    valid = left_root && right_root && left_logical && right_logical &&
            cupidbuild_host_observer_open_source_quota(left_root, 16u, &left) &&
            cupidbuild_host_observer_open_source_quota(right_root, 16u, &right);
    if (valid)
        valid = cupidbuild_host_observer_path_kind(left, left_logical, &kind) &&
                kind == CUPIDBUILD_ENTRY_FILE;
    if (valid && reject != 1)
        valid = cupidbuild_host_observer_path_kind(right, right_logical, &kind) &&
                kind == CUPIDBUILD_ENTRY_FILE;
    if (valid) {
        int result = cupidbuild_host_observer_file_parents_captured_same(
            left, left_logical, right, right_logical, reject == 2 ? (int *)0 : &same);
        if (reject)
            valid = !result && (reject == 2 || same == 0) &&
                    !cupidbuild_host_observer_require_source_unchanged(left) &&
                    !cupidbuild_host_observer_require_source_unchanged(right);
        else
            valid = result && same == expected &&
                    cupidbuild_host_observer_require_source_unchanged(left) &&
                    cupidbuild_host_observer_require_source_unchanged(right);
    }
    if (!cupidbuild_host_observer_close(left))
        valid = 0;
    if (!cupidbuild_host_observer_close(right))
        valid = 0;
    free(left_root);
    free(right_root);
    free(left_logical);
    free(right_logical);
    puts(valid ? "accept" : "fixture-fail");
    return valid ? 0 : 91;
}

int main(int argc, char **argv) {
    char *root, *manifest, *boot, *kernel, *source, *image, *stage_argument;
    const char *arguments[14];
    cupidbuild_disk_options_t *options = (cupidbuild_disk_options_t *)0;
    cupidbuild_disk_options_view_t interpreted;
    cupidbuild_disk_input_discovery_t *owner = (cupidbuild_disk_input_discovery_t *)0;
    cupidbuild_disk_input_discovery_view_t view;
    char expected_request[8192], expected_image[8192];
    const char *mode;
    size_t quota = 8193u, external_limit = 64u;
    int valid, expected_kind, expected_roots, hold, streaming, null_view;
    if (argc > 1 && strcmp(argv[1], "parent") == 0)
        return parent_contract(argc, argv);
    if (argc != 10)
        return 90;
    mode = argv[1];
    root = decode(argv[2]);
    manifest = decode(argv[3]);
    boot = decode(argv[4]);
    kernel = decode(argv[5]);
    source = decode(argv[6]);
    image = decode(argv[7]);
    expected_kind = (int)strtoull(argv[8], (char **)0, 10);
    expected_roots = (int)strtoull(argv[9], (char **)0, 10);
    if (!root || !manifest || !boot || !kernel || !source || !image)
        return 90;
    stage_argument = (char *)malloc(strlen(source) + 15u);
    if (!stage_argument)
        return 90;
    (void)snprintf(stage_argument, strlen(source) + 15u, "%s:/payload.bin", source);
    arguments[0] = "--seed-manifest";
    arguments[1] = manifest;
    arguments[2] = "--bootloader";
    arguments[3] = boot;
    arguments[4] = "--kernel";
    arguments[5] = kernel;
    arguments[6] = "--image";
    arguments[7] = image;
    arguments[8] = "--stage";
    arguments[9] = stage_argument;
    arguments[10] = "--hdd-mb";
    arguments[11] = "200";
    arguments[12] = "--fat-start-lba";
    arguments[13] = "20480";
    valid = cupidbuild_disk_options_open(14u, arguments, 16u, 4300u, 1u,
                                         cupidbuild_host_execution_format() == 2u ? 1u : 0u, 4096u,
                                         &options) == CUPIDBUILD_DISK_OPTIONS_OK;
    if (valid)
        valid = cupidbuild_disk_options_view(options, &interpreted) == CUPIDBUILD_DISK_OPTIONS_OK;
    if (strcmp(mode, "zero-quota") == 0)
        quota = 0u;
    if (strcmp(mode, "max-quota") == 0)
        quota = (size_t)-1;
    if (strcmp(mode, "quota-one") == 0)
        quota = 1u;
    if (strcmp(mode, "no-external") == 0)
        external_limit = 0u;
    if (strcmp(mode, "root-overflow") == 0)
        external_limit = 65u;
    if (strcmp(mode, "stage-overflow") == 0)
        interpreted.stage_count = 4097u;
    if (strcmp(mode, "null-stages") == 0)
        interpreted.stages = (const cupidbuild_disk_argument_stage_t *)0;
    if (valid)
        valid = cupidbuild_disk_input_discovery_open_quota(
            root,
            strcmp(mode, "null-options") == 0 ? (const cupidbuild_disk_options_view_t *)0
                                              : &interpreted,
            quota, external_limit,
            strcmp(mode, "null-owner") == 0 ? (cupidbuild_disk_input_discovery_t **)0 : &owner);
    if (!valid) {
        int cleared_owner = owner == (cupidbuild_disk_input_discovery_t *)0;
        cupidbuild_disk_options_close(options);
        free(root);
        free(manifest);
        free(boot);
        free(kernel);
        free(source);
        free(image);
        free(stage_argument);
        if (!cleared_owner)
            return 92;
        puts("reject");
        return 1;
    }
    valid =
        cupidbuild_source_path_absolute(root, source, expected_request, sizeof(expected_request)) &&
        cupidbuild_source_path_absolute(root, image, expected_image, sizeof(expected_image));
    clobber(root);
    clobber(manifest);
    clobber(boot);
    clobber(kernel);
    clobber(source);
    clobber(image);
    clobber(stage_argument);
    cupidbuild_disk_options_close(options);
    free(root);
    free(manifest);
    free(boot);
    free(kernel);
    free(source);
    free(image);
    free(stage_argument);
    (void)memset(&view, 0xa5, sizeof(view));
    valid = valid && cupidbuild_disk_input_discovery_view(owner, &view) && view.stage_count == 1u &&
            view.external_root_count == (size_t)expected_roots && view.hdd_mb.magnitude == 200u &&
            strcmp(view.image_requested_absolute, expected_image) == 0 &&
            strcmp(view.stages[0].destination, "/payload.bin") == 0 &&
            strcmp(view.stages[0].file.requested_absolute, expected_request) == 0 &&
            view.stages[0].kind == (cupidbuild_host_entry_kind_t)expected_kind;
    if (valid && expected_kind == CUPIDBUILD_ENTRY_NONE)
        valid = !view.stages[0].file.observer && !view.stages[0].file.physical_root &&
                !view.stages[0].file.logical && !view.stages[0].file.physical_absolute;
    if (valid && expected_roots == 0)
        valid = view.seed_manifest.observer == view.primary_observer &&
                view.bootloader.observer == view.primary_observer &&
                view.kernel.observer == view.primary_observer &&
                (expected_kind == CUPIDBUILD_ENTRY_NONE ||
                 view.stages[0].file.observer == view.primary_observer);
    hold = strncmp(mode, "hold", 4u) == 0;
    streaming = strcmp(mode, "hold-stream") == 0;
    null_view = strcmp(mode, "null-view") == 0;
    if (valid && streaming && expected_kind == CUPIDBUILD_ENTRY_FILE) {
        cupidbuild_host_stream_observation_t observation;
        valid = cupidbuild_host_observer_file_stream_retained(
            view.stages[0].file.observer, view.stages[0].file.logical, 67108864u,
            (cupidbuild_host_stream_sink_t)0, (void *)0, &observation);
    }
    if (valid && hold) {
        puts("ready");
        (void)fflush(stdout);
        if (!wait_line())
            valid = 0;
    }
    if (valid) {
        int result;
        (void)memset(&view, 0xa5, sizeof(view));
        result = cupidbuild_disk_input_discovery_view(
            owner, null_view ? (cupidbuild_disk_input_discovery_view_t *)0 : &view);
        if (null_view) {
            (void)memset(&view, 0xa5, sizeof(view));
            valid = !result && !cupidbuild_disk_input_discovery_view(owner, &view) &&
                    cleared(&view, sizeof(view));
        } else if (!result) {
            valid = cleared(&view, sizeof(view));
            if (valid && strcmp(mode, "hold-recover") == 0) {
                puts("first-reject");
                (void)fflush(stdout);
                if (!wait_line())
                    valid = 0;
                (void)memset(&view, 0xa5, sizeof(view));
                valid = valid && !cupidbuild_disk_input_discovery_view(owner, &view) &&
                        cleared(&view, sizeof(view));
            }
            if (!cupidbuild_disk_input_discovery_close(owner))
                valid = 0;
            if (!valid)
                return 93;
            puts("reject");
            return 1;
        }
    }
    if (!cupidbuild_disk_input_discovery_close(owner))
        valid = 0;
    puts(valid ? "accept" : "fixture-fail");
    return valid ? 0 : 94;
}
