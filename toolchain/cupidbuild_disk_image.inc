/**
 * @file cupidbuild_disk_image.inc
 * @brief Capture checked disk inputs, compose the image and publish it.
 */

#include "cupidbuild_disk_input_discovery.h"
#include "cupidbuild_source_path.h"
#include "disk_image.h"
#include "fat16_names.h"

#define CUPIDBUILD_DISK_SECTOR_BYTES 512u
#define CUPIDBUILD_DISK_BLOCK_BYTES 65536u

typedef struct {
    const char *path;
    ctool_u64 size;
} cupidbuild_disk_file_t;

typedef struct {
    cupidbuild_disk_file_t file;
    const char *destination;
} cupidbuild_disk_stage_t;

typedef struct {
    cupidbuild_disk_file_t bootloader;
    cupidbuild_disk_file_t kernel;
    cupidbuild_disk_file_t checked_template;
    cupidbuild_disk_file_t previous;
    const cupidbuild_disk_stage_t *stages;
    size_t stage_count;
    ctool_u32 sector_count;
    ctool_u32 fat_start_lba;
    ctool_fat16_name_profile_t name_profile;
    ctool_bool force_format;
} cupidbuild_disk_contents_t;

typedef struct {
    cupidbuild_host_transaction_t *transaction;
    cupidbuild_disk_file_t file;
    ctool_u64 base;
    size_t count;
    ctool_u8 bytes[CUPIDBUILD_DISK_BLOCK_BYTES];
} cupidbuild_disk_reader_t;

typedef struct {
    cupidbuild_host_transaction_t *transaction;
    ctool_u64 position;
} cupidbuild_disk_writer_t;

static ctool_status_t cupidbuild_disk_read_file(void *context, ctool_u64 offset,
                                                ctool_u8 *bytes, ctool_u32 count) {
    cupidbuild_disk_reader_t *reader = (cupidbuild_disk_reader_t *)context;
    size_t done = 0u;
    if (offset > reader->file.size || count > reader->file.size - offset)
        return CTOOL_ERR_IO;
    while (done < count) {
        ctool_u64 position = offset + done;
        size_t within;
        size_t take;
        if (!reader->count || position < reader->base ||
            position - reader->base >= reader->count) {
            ctool_u64 remaining;
            reader->base = position & ~((ctool_u64)CUPIDBUILD_DISK_BLOCK_BYTES - 1u);
            remaining = reader->file.size - reader->base;
            reader->count = remaining < CUPIDBUILD_DISK_BLOCK_BYTES
                                ? (size_t)remaining : CUPIDBUILD_DISK_BLOCK_BYTES;
            if (!cupidbuild_host_read_frozen_input_range(reader->transaction, reader->file.path,
                                                        reader->base, reader->bytes, reader->count))
                return CTOOL_ERR_IO;
        }
        within = (size_t)(position - reader->base);
        take = reader->count - within;
        if (take > count - done)
            take = count - done;
        (void)memcpy(bytes + done, reader->bytes + within, take);
        done += take;
    }
    return CTOOL_OK;
}

static ctool_disk_source_t cupidbuild_disk_reader_open(cupidbuild_disk_reader_t *reader,
                                                       cupidbuild_host_transaction_t *transaction,
                                                       cupidbuild_disk_file_t file) {
    ctool_disk_source_t source;
    reader->transaction = transaction;
    reader->file = file;
    reader->base = 0u;
    reader->count = 0u;
    source.context = reader;
    source.size = file.size;
    source.read_exact = cupidbuild_disk_read_file;
    return source;
}

static ctool_status_t cupidbuild_disk_write_range(void *context, ctool_u64 offset,
                                                  const ctool_u8 *bytes, ctool_u32 count) {
    cupidbuild_disk_writer_t *writer = (cupidbuild_disk_writer_t *)context;
    if (offset != writer->position ||
        (bytes && !cupidbuild_host_candidate_store_write(writer->transaction, offset, bytes, count)))
        return CTOOL_ERR_IO;
    /* A newly sized private image already contains zeroes in unwritten ranges. */
    writer->position += count;
    return CTOOL_OK;
}

static ctool_status_t cupidbuild_disk_read_sector(void *context, ctool_u32 sector,
                                                   ctool_u8 *bytes) {
    return cupidbuild_host_candidate_store_read((cupidbuild_host_transaction_t *)context,
                (ctool_u64)sector * CUPIDBUILD_DISK_SECTOR_BYTES, bytes, CUPIDBUILD_DISK_SECTOR_BYTES)
               ? CTOOL_OK : CTOOL_ERR_IO;
}

static ctool_status_t cupidbuild_disk_write_sector(void *context, ctool_u32 sector,
                                                    const ctool_u8 *bytes) {
    return cupidbuild_host_candidate_store_write((cupidbuild_host_transaction_t *)context,
                (ctool_u64)sector * CUPIDBUILD_DISK_SECTOR_BYTES, bytes, CUPIDBUILD_DISK_SECTOR_BYTES)
               ? CTOOL_OK : CTOOL_ERR_IO;
}

static void *cupidbuild_disk_allocate(void *context, ctool_u32 bytes) {
    (void)context;
    return malloc(bytes);
}

static void cupidbuild_disk_release(void *context, void *allocation, ctool_u32 bytes) {
    (void)context;
    (void)bytes;
    free(allocation);
}

static ctool_status_t cupidbuild_disk_read_stage(void *context, ctool_u32 offset,
                                                  ctool_u8 *bytes, ctool_u32 count) {
    return cupidbuild_disk_read_file(context, offset, bytes, count);
}

static ctool_status_t cupidbuild_disk_stage_one(ctool_fat16_volume_t *volume,
                                                cupidbuild_disk_reader_t *reader,
                                                cupidbuild_host_transaction_t *transaction,
                                                const cupidbuild_disk_stage_t *stage,
                                                ctool_fat16_name_profile_t profile) {
    ctool_string_t destination;
    ctool_u32 count = 0u;
    ctool_fat16_name_t *components;
    ctool_fat16_payload_t payload;
    ctool_status_t status;
    if (!stage->file.path)
        return CTOOL_OK;
    if (stage->file.size > 0xffffffffull)
        return CTOOL_ERR_LIMIT;
    destination.data = stage->destination;
    destination.size = (ctool_u32)strlen(stage->destination);
    status = ctool_fat16_project_destination(destination, profile, NULL, 0u, &count);
    if (status != CTOOL_OK)
        return status;
    components = (ctool_fat16_name_t *)calloc(count, sizeof(*components));
    if (!components)
        return CTOOL_ERR_NO_MEMORY;
    status = ctool_fat16_project_destination(destination, profile, components, count, &count);
    if (status == CTOOL_OK) {
        (void)cupidbuild_disk_reader_open(reader, transaction, stage->file);
        payload.context = reader;
        payload.size = (ctool_u32)stage->file.size;
        payload.read_exact = cupidbuild_disk_read_stage;
        status = ctool_fat16_stage(volume, components, count, payload);
    }
    free(components);
    return status;
}

static ctool_status_t cupidbuild_disk_write_serialized(cupidbuild_host_transaction_t *transaction,
                                                          const cupidbuild_disk_contents_t *contents,
                                                          cupidbuild_host_snapshot_t *snapshot) {
    cupidbuild_disk_reader_t *readers = (cupidbuild_disk_reader_t *)calloc(4u, sizeof(*readers));
    ctool_u8 *buffer = (ctool_u8 *)malloc(CUPIDBUILD_DISK_BLOCK_BYTES);
    cupidbuild_disk_writer_t writer;
    ctool_disk_range_target_t range_target;
    ctool_disk_image_request_t request;
    ctool_disk_image_result_t composed;
    ctool_fat16_store_t store;
    ctool_fat16_volume_t *volume = NULL;
    ctool_allocator_t allocator;
    ctool_u64 extent = (ctool_u64)contents->sector_count * CUPIDBUILD_DISK_SECTOR_BYTES;
    ctool_status_t status = CTOOL_ERR_NO_MEMORY;
    size_t index;

    if (!readers || !buffer)
        goto done;
    if (!cupidbuild_host_candidate_store_open(transaction, extent)) {
        status = CTOOL_ERR_IO;
        goto done;
    }
    (void)memset(&request, 0, sizeof(request));
    request.bootloader = cupidbuild_disk_reader_open(&readers[0], transaction, contents->bootloader);
    request.kernel = cupidbuild_disk_reader_open(&readers[1], transaction, contents->kernel);
    request.checked_template = cupidbuild_disk_reader_open(&readers[2], transaction,
                                                            contents->checked_template);
    request.previous = cupidbuild_disk_reader_open(&readers[3], transaction, contents->previous);
    request.previous_present = contents->previous.path ? CTOOL_TRUE : CTOOL_FALSE;
    request.force_format = contents->force_format;
    request.image_sectors = contents->sector_count;
    request.fat_start_lba = contents->fat_start_lba;
    writer.transaction = transaction;
    writer.position = 0u;
    range_target.context = &writer;
    range_target.sector_count = contents->sector_count;
    range_target.write_range = cupidbuild_disk_write_range;
    status = ctool_disk_image_compose_ranges(&request, range_target, buffer,
                                            CUPIDBUILD_DISK_BLOCK_BYTES, &composed);
    if (status != CTOOL_OK)
        goto done;
    if (!composed.ready || writer.position != extent) {
        status = CTOOL_ERR_IO;
        goto done;
    }

    for (index = 0u; index < contents->stage_count; index++) {
        if (contents->stages[index].file.path)
            break;
    }
    if (index < contents->stage_count) {
        allocator.context = NULL;
        allocator.allocate = cupidbuild_disk_allocate;
        allocator.release = cupidbuild_disk_release;
        store.context = transaction;
        store.sector_count = contents->sector_count;
        store.read_sector = cupidbuild_disk_read_sector;
        store.write_sector = cupidbuild_disk_write_sector;
        status = ctool_fat16_open(allocator, store, contents->fat_start_lba, &volume);
        for (; status == CTOOL_OK && index < contents->stage_count; index++)
            status = cupidbuild_disk_stage_one(volume, &readers[0], transaction,
                                               &contents->stages[index], contents->name_profile);
        if (status != CTOOL_OK)
            goto done;
    }
    if (!cupidbuild_host_candidate_store_finish(transaction, snapshot) || snapshot->size != extent)
        status = CTOOL_ERR_IO;

done:
    if (volume)
        ctool_fat16_close(volume);
    free(readers);
    free(buffer);
    return status;
}

#define CUPIDBUILD_DISK_STAGE_COUNT 4096u
#define CUPIDBUILD_DISK_EXTERNAL_ROOTS 64u
#define CUPIDBUILD_DISK_CAPTURE_ROOTS (CUPIDBUILD_DISK_EXTERNAL_ROOTS + 1u)
#define CUPIDBUILD_DISK_INPUT_CAPACITY 2147483647ull

typedef struct {
    cupidbuild_host_transaction_t *transaction;
    cupidbuild_host_observer_t *primary;
    cupidbuild_host_observer_t *external[CUPIDBUILD_DISK_CAPTURE_ROOTS];
    size_t external_count;
} cupidbuild_disk_capture_t;

static int cupidbuild_disk_capture_file(cupidbuild_disk_capture_t *capture,
                                        const cupidbuild_source_file_view_t *selected,
                                        const char *name, unsigned long long limit,
                                        cupidbuild_disk_file_t *file, cupidbuild_host_snapshot_t *snapshot_out,
                                        int share_file) {
    cupidbuild_host_snapshot_t snapshot;
    cupidbuild_host_observer_t *external = NULL;
    const char *frozen = NULL;
    size_t index;
    int same = 0;
    int valid;
    if (!cupidbuild_host_observer_roots_captured_same(capture->primary, selected->observer, &same))
        return 0;
    if (!same) {
        external = selected->observer;
        for (index = 0u; index < capture->external_count; index++) {
            if (capture->external[index] == external)
                break;
        }
        if (index == capture->external_count) {
            if (index == CUPIDBUILD_DISK_CAPTURE_ROOTS ||
                !cupidbuild_host_transaction_borrow_external_source_observer(capture->transaction,
                                                                              external))
                return 0;
            capture->external[capture->external_count++] = external;
        }
    }
    if (share_file)
        valid = cupidbuild_host_freeze_disk_input(capture->transaction, external,
            external ? selected->logical : selected->physical_absolute, name, limit, &frozen, &snapshot);
    else if (external)
        valid = cupidbuild_host_freeze_external_input(capture->transaction, external,
            selected->logical, name, limit, &frozen, &snapshot);
    else
        valid = cupidbuild_host_freeze_input_bounded(capture->transaction,
            selected->physical_absolute, name, limit, &frozen, &snapshot);
    if (!valid || !snapshot.present)
        return 0;
    file->path = frozen;
    file->size = snapshot.size;
    if (snapshot_out)
        *snapshot_out = snapshot;
    return 1;
}

static int cupidbuild_disk_seed_release(cupidbuild_disk_capture_t *capture,
                                        const cupidbuild_disk_input_discovery_view_t *view,
                                        const char *requested, unsigned char **data_out,
                                        size_t *size_out) {
    cupidbuild_source_file_view_t file;
    cupidbuild_host_entry_kind_t kind;
    cupidbuild_disk_file_t frozen;
    char full[CUPIDBUILD_PATH_BYTES];
    char *separator;
    const char *logical;
    *data_out = NULL;
    *size_out = 0u;
    if (!requested)
        return 1;
    if (!cupidbuild_path_safe(requested, 0) ||
        !cupidbuild_seed_manifest_path(view->working_root_physical, requested, 1,
                                       full, sizeof(full)) ||
        !cupidbuild_repository_prefix(full, view->working_root_physical))
        return 0;
    if (cupidbuild_host_execution_format() == 2u) {
        for (separator = full; *separator; separator++) {
            if (*separator == '\\')
                *separator = '/';
        }
    }
    logical = full + strlen(view->working_root_physical);
    while (*logical == '/')
        logical++;
    if (!cupidbuild_host_observer_source_path_kind(view->primary_observer, logical, &kind) ||
        kind != CUPIDBUILD_ENTRY_FILE)
        return 0;
    file.observer = view->primary_observer;
    file.logical = logical;
    file.physical_absolute = full;
    if (!cupidbuild_disk_capture_file(capture, &file, "seed-release.json", 65536u, &frozen, NULL, 0))
        return 0;
    *data_out = cupidbuild_host_read_frozen_input(capture->transaction, frozen.path, 65536u, size_out);
    return *data_out != NULL;
}

static int cupidbuild_disk_seed_capture(cupidbuild_disk_capture_t *capture,
                                        const cupidbuild_disk_input_discovery_view_t *view,
                                        const char *release_path, cupidbuild_seed_capture_t *seed) {
    const cupidbuild_source_file_view_t *manifest = &view->seed_manifest;
    const char *separator = strrchr(manifest->logical, '/');
    size_t directory_size = separator ? (size_t)(separator - manifest->logical) : 0u;
    char directory[CUPIDBUILD_PATH_BYTES];
    cupidbuild_disk_file_t frozen;
    unsigned char *release = NULL;
    size_t release_size;
    size_t index;
    int valid;

    if (!cupidbuild_disk_capture_file(capture, manifest, "manifest.json",
                                      CUPIDBUILD_MANIFEST_BYTES, &frozen, NULL, 0))
        return 0;
    seed->manifest = cupidbuild_host_read_frozen_input(
        capture->transaction, frozen.path, CUPIDBUILD_MANIFEST_BYTES, &seed->manifest_size);
    if (!seed->manifest ||
        !cupidbuild_disk_seed_release(capture, view, release_path, &release, &release_size))
        return 0;
    valid = cupidbuild_seed_validate_capture(seed, release, release_size, 1);
    free(release);
    if (!valid || directory_size >= sizeof(directory))
        return 0;

    (void)memcpy(directory, manifest->logical, directory_size);
    directory[directory_size] = 0;
    for (index = 0u; index < seed->artifact_count; index++)
        seed->expected_files[index] = seed->artifacts[index].file;
    if (!cupidbuild_host_observer_seed_directory(manifest->observer, directory,
                                                  seed->expected_files, seed->artifact_count))
        return 0;
    for (index = 0u; index < seed->artifact_count; index++) {
        const char *name = seed->expected_files[index];
        char relative[CUPIDBUILD_PATH_BYTES];
        char physical[CUPIDBUILD_PATH_BYTES];
        cupidbuild_source_file_view_t tool;
        cupidbuild_host_entry_kind_t kind;
        if (!*directory) {
            tool.logical = name;
        } else {
            if (!cupidbuild_join(relative, sizeof(relative), directory, name))
                return 0;
            tool.logical = relative;
        }
        if (!cupidbuild_join(physical, sizeof(physical), manifest->physical_root, tool.logical) ||
            !cupidbuild_host_observer_source_path_kind(manifest->observer, tool.logical, &kind) ||
            kind != CUPIDBUILD_ENTRY_FILE)
            return 0;
        tool.observer = manifest->observer;
        tool.physical_absolute = physical;
        if (!cupidbuild_disk_capture_file(capture, &tool, name, seed->artifacts[index].size,
                                          &frozen, &seed->tool_snapshots[index], 0) ||
            !cupidbuild_host_make_input_executable(capture->transaction, frozen.path) ||
            !cupidbuild_artifact_matches(&seed->tool_snapshots[index], &seed->artifacts[index]) ||
            !cupidbuild_validate_execution_profile(capture->transaction, frozen.path, index,
                                                    1, seed->current_windows_plan))
            return 0;
        seed->frozen_tools[index] = frozen.path;
    }
    return 1;
}

static int cupidbuild_disk_capture_stages(cupidbuild_disk_capture_t *capture,
                                         const cupidbuild_disk_input_discovery_view_t *view,
                                         cupidbuild_disk_stage_t *stages) {
    size_t index;
    for (index = 0u; index < view->stage_count; index++) {
        const cupidbuild_disk_input_stage_view_t *source = &view->stages[index];
        char name[40];
        stages[index].destination = source->destination;
        if (source->kind == CUPIDBUILD_ENTRY_NONE) {
            if (!strcmp(source->file.requested_absolute, view->image_requested_absolute))
                return 0;
            continue;
        }
        (void)snprintf(name, sizeof(name), "stage-%u.bin", (unsigned int)index);
        if (!cupidbuild_disk_capture_file(capture, &source->file, name,
                                        CUPIDBUILD_DISK_INPUT_CAPACITY, &stages[index].file, NULL, 1))
            return 0;
    }
    return 1;
}

static int cupidbuild_disk_prepare_template(cupidbuild_host_transaction_t *transaction,
                                             const cupidbuild_seed_capture_t *seed,
                                             cupidbuild_disk_contents_t *contents) {
    const char *arguments[11];
    char sectors[16];
    char fat_start[16];
    cupidbuild_host_snapshot_t snapshot;
    (void)snprintf(sectors, sizeof(sectors), "%u", contents->sector_count);
    (void)snprintf(fat_start, sizeof(fat_start), "%u", contents->fat_start_lba);
    arguments[0] = "disk-template";
    arguments[1] = contents->bootloader.path;
    arguments[2] = "--kernel";
    arguments[3] = contents->kernel.path;
    arguments[4] = "--image-sectors";
    arguments[5] = sectors;
    arguments[6] = "--fat-start-lba";
    arguments[7] = fat_start;
    arguments[8] = "-o";
    arguments[9] = cupidbuild_host_private_output(transaction);
    arguments[10] = NULL;
    if (cupidbuild_host_run_in_private(transaction, seed->frozen_tools[4], arguments, 60000u) != 0 ||
        !cupidbuild_host_freeze_tool_output(transaction, &contents->checked_template.path,
                                             &snapshot))
        return 0;
    contents->checked_template.size = snapshot.size;
    return 1;
}

int cupidbuild_publish_disk_image(const char *working_root, const char *seed_release,
                                   size_t count,
                                   const char *const *arguments) {
    cupidbuild_disk_options_t *options = NULL;
    cupidbuild_disk_options_view_t option_view;
    cupidbuild_disk_input_discovery_t *discovery = NULL;
    cupidbuild_disk_input_discovery_view_t view;
    cupidbuild_disk_capture_t capture;
    cupidbuild_disk_contents_t contents;
    cupidbuild_seed_capture_t seed;
    cupidbuild_disk_stage_t *stages = NULL;
    cupidbuild_host_snapshot_t snapshot;
    cupidbuild_host_observer_t *output_observer = NULL;
    const char *output_root;
    const char *output_leaf;
    char requested_output[CUPIDBUILD_PATH_BYTES];
    ctool_u64 extent;
    int changed;
    int status = 1;
    const char *error = NULL;
    cupidbuild_disk_options_status_t parsed;
    (void)memset(&capture, 0, sizeof(capture));
    (void)memset(&contents, 0, sizeof(contents));
    (void)memset(&seed, 0, sizeof(seed));
    parsed = cupidbuild_disk_options_open(count, arguments, 16u, 4300u, 1u,
                                          cupidbuild_host_execution_format() == 2u ? 1u : 0u,
                                          CUPIDBUILD_DISK_STAGE_COUNT, &options);
    if (parsed == CUPIDBUILD_DISK_OPTIONS_HELP) {
        puts("cupidbuild image [--root ROOT] [--seed-release RELEASE] --seed-manifest PATH "
             "--image PATH --bootloader PATH --kernel PATH --hdd-mb MB --fat-start-lba LBA "
             "[--force-format] [--stage SOURCE:DEST] [--wads WAD ...]");
        return 0;
    }
    if (parsed != CUPIDBUILD_DISK_OPTIONS_OK ||
        cupidbuild_disk_options_view(options, &option_view) != CUPIDBUILD_DISK_OPTIONS_OK ||
        !option_view.hdd_mb.magnitude_fits_u32 || option_view.hdd_mb.zero ||
        option_view.hdd_mb.negative || !option_view.fat_start_lba.magnitude_fits_u32 ||
        option_view.fat_start_lba.negative || option_view.fat_start_lba.magnitude <= 5u) {
        error = "invalid image options or geometry";
        goto done;
    }
    extent = (ctool_u64)option_view.hdd_mb.magnitude * 1048576ull;
    if (extent / 512u > 0xffffffffull ||
        option_view.fat_start_lba.magnitude >= extent / 512u) {
        error = "image geometry exceeds the bounded store";
        goto done;
    }
    if (!cupidbuild_source_path_absolute(working_root, option_view.image.data,
                                         requested_output, sizeof(requested_output))) {
        error = "image output pathname is invalid";
        goto done;
    }
    if (!cupidbuild_host_image_output_prepare(requested_output, &output_observer, &output_root,
                                              &output_leaf)) {
        error = "image output directory cannot be retained";
        goto done;
    }
    if (!cupidbuild_disk_input_discovery_open_quota(working_root, &option_view, 8192u,
             CUPIDBUILD_DISK_EXTERNAL_ROOTS, &discovery) ||
        !cupidbuild_disk_input_discovery_view(discovery, &view)) {
        error = "image inputs cannot be retained";
        goto done;
    }
    if (!cupidbuild_host_publication_transaction_open_bounded(output_root, output_leaf,
             CUPIDBUILD_HOST_MAX_EXTENT, &capture.transaction) ||
        !cupidbuild_host_reserve_inputs(capture.transaction, view.stage_count + 12u) ||
        !cupidbuild_host_transaction_borrow_observer(capture.transaction, output_observer) ||
        !cupidbuild_disk_input_discovery_bind_requests(discovery, capture.transaction)) {
        error = "image publication transaction cannot be opened";
        goto done;
    }
    capture.primary = output_observer;
    contents.sector_count = (ctool_u32)(extent / 512u);
    contents.fat_start_lba = view.fat_start_lba.magnitude;
    contents.force_format = view.force_format != 0u ? CTOOL_TRUE : CTOOL_FALSE;
    contents.name_profile = CTOOL_FAT16_NAMES_UNICODE_16;
    if (view.stage_count) {
        stages = (cupidbuild_disk_stage_t *)calloc(view.stage_count, sizeof(*stages));
        if (!stages) {
            error = "image stages cannot be allocated";
            goto done;
        }
    }
    contents.stages = stages;
    contents.stage_count = view.stage_count;
    if (!cupidbuild_disk_seed_capture(&capture, &view, seed_release, &seed) ||
        !cupidbuild_disk_capture_file(&capture, &view.bootloader, "image-boot.bin",
                                     CUPIDBUILD_DISK_INPUT_CAPACITY, &contents.bootloader, NULL, 1) ||
        !cupidbuild_disk_capture_file(&capture, &view.kernel, "image-kernel.bin",
                                     (ctool_u64)contents.fat_start_lba * 512u - 2560u,
                                     &contents.kernel, NULL, 1) ||
        !cupidbuild_disk_capture_stages(&capture, &view, stages)) {
        error = "image inputs cannot be frozen";
        goto done;
    }
    if (!contents.force_format &&
        cupidbuild_host_initial_output_extent(capture.transaction) == extent) {
        if (!cupidbuild_host_freeze_previous_output(capture.transaction, "previous.img",
                                                  &contents.previous.path, &snapshot)) {
            error = "previous image cannot be frozen";
            goto done;
        }
        contents.previous.size = snapshot.size;
    }
    if (!cupidbuild_disk_prepare_template(capture.transaction, &seed, &contents) ||
        cupidbuild_disk_write_serialized(capture.transaction, &contents, &snapshot) != CTOOL_OK ||
        !cupidbuild_host_publish_if_changed(capture.transaction, &changed)) {
        error = "image cannot be composed or published";
        goto done;
    }
    status = 0;

done:
    if (error)
        fprintf(stderr, "cupidbuild: %s: %s\n", error, cupidbuild_host_error(capture.transaction));
    if (!cupidbuild_host_transaction_close(capture.transaction))
        status = 1;
    if (!cupidbuild_disk_input_discovery_close(discovery))
        status = 1;
    if (!cupidbuild_host_observer_close(output_observer))
        status = 1;
    cupidbuild_disk_options_close(options);
    cupidbuild_seed_capture_close(&seed);
    free(stages);
    return status;
}
