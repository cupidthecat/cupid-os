/**
 * @file cupidobj_image_contract.inc
 * @brief Check disk templates and ISO fixture images.
 */

static int disk_template_small_layout(const ctool_obj_result_t *result, const ctool_u8 *boot,
                                      ctool_bytes_t kernel) {
    const ctool_u8 *bytes = result->bytes.data;
    const ctool_u8 *bpb;
    if (bytes == (const ctool_u8 *)0 || result->bytes.size != 38400u ||
        result->base_address != 0u || result->end_address != 0u) {
        (void)fprintf(stderr, "disk-template: small result shape differs\n");
        return 0;
    }
    if (memcmp(bytes, boot, 446u) != 0 || bytes[446u] != 0x80u || bytes[447u] != 0xfeu ||
        bytes[448u] != 0xffu || bytes[449u] != 0xffu || bytes[450u] != 0x06u ||
        bytes[451u] != 0xfeu || bytes[452u] != 0xffu || bytes[453u] != 0xffu ||
        read_le32(bytes, 454u) != 8u || read_le32(bytes, 458u) != 4200u ||
        !byte_range_is_zero(bytes, 462u, 510u) || bytes[510u] != 0x55u || bytes[511u] != 0xaau) {
        (void)fprintf(stderr, "disk-template: small MBR differs\n");
        return 0;
    }
    if (memcmp(bytes + 512u, boot + 512u, 2048u) != 0 ||
        memcmp(bytes + 2560u, kernel.data, (size_t)kernel.size) != 0 ||
        !byte_range_is_zero(bytes, 2560u + kernel.size, 4096u)) {
        (void)fprintf(stderr, "disk-template: boot or kernel lane differs\n");
        return 0;
    }

    bpb = bytes + 4096u;
    if (bpb[0] != 0xebu || bpb[1] != 0x3cu || bpb[2] != 0x90u ||
        memcmp(bpb + 3u, "CUPIDOS ", 8u) != 0 || read_le16(bpb, 11u) != 512u || bpb[13u] != 1u ||
        read_le16(bpb, 14u) != 1u || bpb[16u] != 2u || read_le16(bpb, 17u) != 512u ||
        read_le16(bpb, 19u) != 4200u || bpb[21u] != 0xf8u || read_le16(bpb, 22u) != 17u ||
        read_le16(bpb, 24u) != 63u || read_le16(bpb, 26u) != 255u || read_le32(bpb, 28u) != 8u ||
        read_le32(bpb, 32u) != 0u || bpb[36u] != 0x80u || bpb[38u] != 0x29u ||
        read_le32(bpb, 39u) != 0x0c001d05u || memcmp(bpb + 43u, "CUPIDOS    ", 11u) != 0 ||
        memcmp(bpb + 54u, "FAT16   ", 8u) != 0 || !byte_range_is_zero(bpb, 62u, 510u) ||
        bpb[510u] != 0x55u || bpb[511u] != 0xaau) {
        (void)fprintf(stderr, "disk-template: small FAT16 BPB differs\n");
        return 0;
    }
    if (memcmp(bytes + 4608u, "\xf8\xff\xff\xff", 4u) != 0 ||
        !byte_range_is_zero(bytes, 4612u, 13312u) ||
        memcmp(bytes + 13312u, "\xf8\xff\xff\xff", 4u) != 0 ||
        !byte_range_is_zero(bytes, 13316u, 22016u) || !byte_range_is_zero(bytes, 22016u, 38400u)) {
        (void)fprintf(stderr, "disk-template: small FAT/root layout differs\n");
        return 0;
    }
    return 1;
}

static int disk_template_active_layout(const ctool_obj_result_t *result, ctool_u32 kernel_size) {
    const ctool_u8 *bytes = result->bytes.data;
    const ctool_u8 *bpb;
    if (bytes == (const ctool_u8 *)0 || result->bytes.size != 10697216u ||
        result->base_address != 0u || result->end_address != 0u) {
        (void)fprintf(stderr, "disk-template: active result shape differs\n");
        return 0;
    }
    if (read_le32(bytes, 454u) != 20480u || read_le32(bytes, 458u) != 389120u ||
        !byte_range_is_zero(bytes, 2560u + kernel_size, 10485760u)) {
        (void)fprintf(stderr, "disk-template: active MBR/kernel lane differs\n");
        return 0;
    }
    bpb = bytes + 10485760u;
    if (read_le16(bpb, 11u) != 512u || bpb[13u] != 8u || read_le16(bpb, 14u) != 1u ||
        bpb[16u] != 2u || read_le16(bpb, 17u) != 512u || read_le16(bpb, 19u) != 0u ||
        read_le16(bpb, 22u) != 190u || read_le32(bpb, 28u) != 20480u ||
        read_le32(bpb, 32u) != 389120u || bpb[510u] != 0x55u || bpb[511u] != 0xaau) {
        (void)fprintf(stderr, "disk-template: active FAT16 geometry differs\n");
        return 0;
    }
    if (memcmp(bytes + 10486272u, "\xf8\xff\xff\xff", 4u) != 0 ||
        !byte_range_is_zero(bytes, 10486276u, 10583552u) ||
        memcmp(bytes + 10583552u, "\xf8\xff\xff\xff", 4u) != 0 ||
        !byte_range_is_zero(bytes, 10583556u, 10680832u) ||
        !byte_range_is_zero(bytes, 10680832u, 10697216u)) {
        (void)fprintf(stderr, "disk-template: active FAT/root layout differs\n");
        return 0;
    }
    return 1;
}

static int run_disk_template(void) {
    enum {
        DISK_SECTOR_BYTES = 512,
        DISK_BOOT_SECTORS = 5,
        SMALL_IMAGE_SECTORS = 4208,
        SMALL_FAT_START_LBA = 8,
        ACTIVE_IMAGE_SECTORS = 409600,
        ACTIVE_FAT_START_LBA = 20480
    };

    static const ctool_u8 kernel_bytes[] = {0x43u, 0x55u, 0x50u, 0x49u, 0x44u, 0x2du, 0x4fu, 0x53u};
    ctool_u8 boot[DISK_BOOT_SECTORS * DISK_SECTOR_BYTES];
    ctool_u8 overlapping_kernel[3u * DISK_SECTOR_BYTES + 1u];
    ctool_host_adapter_t adapter;
    ctool_job_config_t config;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_buffer_t *first = (ctool_buffer_t *)0;
    ctool_buffer_t *repeat = (ctool_buffer_t *)0;
    ctool_buffer_t *active = (ctool_buffer_t *)0;
    ctool_buffer_t *limited = (ctool_buffer_t *)0;
    ctool_source_t boot_source;
    ctool_source_t kernel_source;
    ctool_source_t overlap_source;
    const ctool_diagnostic_t *diagnostic;
    ctool_obj_request_t request;
    ctool_obj_disk_template_request_t *disk_request;
    ctool_obj_result_t first_result;
    ctool_obj_result_t repeat_result;
    ctool_obj_result_t active_result;
    ctool_obj_result_t recovery_result;
    ctool_arena_mark_t mark;
    ctool_status_t status;
    ctool_u32 index;
    int have_first = 0;
    int have_repeat = 0;
    int ok = 1;

    for (index = 0u; index < (ctool_u32)sizeof(boot); index++) {
        boot[index] = (ctool_u8)((index * 37u + 11u) & 0xffu);
    }
    (void)memset(overlapping_kernel, 0x6bu, sizeof(overlapping_kernel));
    if (!open_job(&adapter, &config, &job)) {
        return 1;
    }
    status = ctool_job_open_buffer(job, 64u, config.limits.output_bytes, &first);
    if (status == CTOOL_OK) {
        status = ctool_job_open_buffer(job, 64u, config.limits.output_bytes, &repeat);
    }
    if (status == CTOOL_OK) {
        status = ctool_job_open_buffer(job, 64u, config.limits.output_bytes, &active);
    }
    if (status == CTOOL_OK) {
        status = ctool_job_open_buffer(job, 64u, 38399u, &limited);
    }
    if (status != CTOOL_OK) {
        if (limited != (ctool_buffer_t *)0) {
            ctool_buffer_close(limited);
        }
        if (active != (ctool_buffer_t *)0) {
            ctool_buffer_close(active);
        }
        if (repeat != (ctool_buffer_t *)0) {
            ctool_buffer_close(repeat);
        }
        if (first != (ctool_buffer_t *)0) {
            ctool_buffer_close(first);
        }
        ctool_job_close(job);
        return 1;
    }

    boot_source.path.text = ctool_string("/boot.bin");
    boot_source.contents = ctool_bytes(boot, (ctool_u32)sizeof(boot));
    kernel_source.path.text = ctool_string("/kernel.bin");
    kernel_source.contents = ctool_bytes(kernel_bytes, (ctool_u32)sizeof(kernel_bytes));
    overlap_source.path.text = ctool_string("/overlap-kernel.bin");
    overlap_source.contents =
        ctool_bytes(overlapping_kernel, (ctool_u32)sizeof(overlapping_kernel));
    (void)memset(&request, 0, sizeof(request));
    request.operation = CTOOL_OBJ_BUILD_DISK_TEMPLATE;
    request.input = &boot_source;
    disk_request = &request.as.disk_template;
    disk_request->kernel = &kernel_source;
    disk_request->image_sectors = SMALL_IMAGE_SECTORS;
    disk_request->fat_start_lba = SMALL_FAT_START_LBA;

    (void)memset(&first_result, 0, sizeof(first_result));
    (void)memset(&repeat_result, 0, sizeof(repeat_result));
    (void)memset(&active_result, 0, sizeof(active_result));
    (void)memset(&recovery_result, 0, sizeof(recovery_result));
    mark = ctool_arena_mark(ctool_job_arena(job));
    status = ctool_obj_transform(job, &request, first, &first_result);
    if (status != CTOOL_OK || !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) ||
        first_result.bytes.data != ctool_buffer_view(first).data ||
        first_result.bytes.size != ctool_buffer_view(first).size ||
        !disk_template_small_layout(&first_result, boot, kernel_source.contents)) {
        (void)fprintf(stderr, "disk-template: small transform failed\n");
        ok = 0;
    } else {
        have_first = 1;
    }

    mark = ctool_arena_mark(ctool_job_arena(job));
    status = ctool_obj_transform(job, &request, repeat, &repeat_result);
    if (status != CTOOL_OK || !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) ||
        have_first == 0 || first_result.bytes.size != repeat_result.bytes.size ||
        memcmp(first_result.bytes.data, repeat_result.bytes.data,
               (size_t)first_result.bytes.size) != 0) {
        (void)fprintf(stderr, "disk-template: deterministic repeat differs\n");
        ok = 0;
    } else {
        have_repeat = 1;
    }

    disk_request->image_sectors = ACTIVE_IMAGE_SECTORS;
    disk_request->fat_start_lba = ACTIVE_FAT_START_LBA;
    mark = ctool_arena_mark(ctool_job_arena(job));
    status = ctool_obj_transform(job, &request, active, &active_result);
    if (status != CTOOL_OK || !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) ||
        active_result.bytes.data != ctool_buffer_view(active).data ||
        active_result.bytes.size != ctool_buffer_view(active).size ||
        !disk_template_active_layout(&active_result, kernel_source.contents.size)) {
        (void)fprintf(stderr, "disk-template: active transform failed\n");
        ok = 0;
    }

    ctool_buffer_clear(active);
    disk_request->image_sectors = 8304u;
    disk_request->fat_start_lba = 16u;
    mark = ctool_arena_mark(ctool_job_arena(job));
    status = ctool_obj_transform(job, &request, active, &active_result);
    if (status != CTOOL_OK || !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) ||
        active_result.bytes.data != ctool_buffer_view(active).data ||
        active_result.bytes.size != 42496u ||
        active_result.bytes.data[16u * DISK_SECTOR_BYTES + 13u] != 2u ||
        read_le16(active_result.bytes.data + 16u * DISK_SECTOR_BYTES, 22u) != 17u) {
        (void)fprintf(stderr, "disk-template: FAT-size cycle recovery differs\n");
        ok = 0;
    }

    ctool_buffer_clear(active);
    overlap_source.contents =
        ctool_bytes(overlapping_kernel, (ctool_u32)sizeof(overlapping_kernel) - 1u);
    disk_request->image_sectors = SMALL_IMAGE_SECTORS;
    disk_request->fat_start_lba = SMALL_FAT_START_LBA;
    disk_request->kernel = &overlap_source;
    mark = ctool_arena_mark(ctool_job_arena(job));
    status = ctool_obj_transform(job, &request, active, &active_result);
    if (status != CTOOL_OK || !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) ||
        !disk_template_small_layout(&active_result, boot, overlap_source.contents)) {
        (void)fprintf(stderr, "disk-template: exact kernel boundary failed\n");
        ok = 0;
    }
    overlap_source.contents =
        ctool_bytes(overlapping_kernel, (ctool_u32)sizeof(overlapping_kernel));
    disk_request->kernel = &kernel_source;

    ctool_buffer_clear(first);
    disk_request->image_sectors = SMALL_IMAGE_SECTORS;
    disk_request->fat_start_lba = SMALL_FAT_START_LBA;
    disk_request->kernel = (const ctool_source_t *)0;
    boot_source.contents = ctool_bytes((const ctool_u8 *)0, 1u);
    disk_request->fat_start_lba = 0u;
    ok &= expect_failure_message(job, first, &request, CTOOL_ERR_INVALID_ARGUMENT,
                                 CTOOL_OBJ_DIAG_INVALID_REQUEST,
                                 "CupidObj disk template kernel is required",
                                 "disk-template missing kernel precedes byte and geometry errors");
    disk_request->kernel = &kernel_source;
    ok &= expect_failure_message(job, first, &request, CTOOL_ERR_INVALID_ARGUMENT,
                                 CTOOL_OBJ_DIAG_INVALID_INPUT,
                                 "CupidObj bootloader bytes are invalid",
                                 "disk-template invalid boot precedes geometry errors");
    boot_source.contents = ctool_bytes(boot, 1u);
    ok &=
        expect_failure_message(job, first, &request, CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_INVALID_INPUT,
                               "FAT partition must start after bootloader and kernel area",
                               "disk-template geometry precedes short boot");
    disk_request->fat_start_lba = SMALL_FAT_START_LBA;
    boot_source.contents = ctool_bytes(boot, (ctool_u32)sizeof(boot) - 1u);
    ok &= expect_rewound_failure(job, first, &request, CTOOL_ERR_INPUT,
                                 CTOOL_OBJ_DIAG_INVALID_INPUT, "disk-template short boot");
    boot_source.contents = ctool_bytes(boot, (ctool_u32)sizeof(boot));

    disk_request->image_sectors = 100u;
    ok &= expect_rewound_failure(job, first, &request, CTOOL_ERR_INPUT,
                                 CTOOL_OBJ_DIAG_INVALID_INPUT, "disk-template bad geometry");
    disk_request->image_sectors = SMALL_IMAGE_SECTORS;

    disk_request->kernel = &overlap_source;
    ok &= expect_rewound_failure(job, first, &request, CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_OVERLAP,
                                 "disk-template kernel overlap");
    diagnostic = ctool_job_diagnostic(job, ctool_job_diagnostic_count(job) - 1u);
    if (diagnostic == (const ctool_diagnostic_t *)0 ||
        !string_equal(diagnostic->path, "/overlap-kernel.bin") ||
        !string_equal(diagnostic->message, "CupidObj kernel overlaps FAT partition at LBA 8")) {
        (void)fprintf(stderr, "disk-template: kernel overlap path differs\n");
        ok = 0;
    }
    disk_request->kernel = &kernel_source;

    ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_LIMIT, CTOOL_OBJ_DIAG_LIMIT,
                                 "disk-template output limit");

    disk_request->image_sectors = 8392808u;
    disk_request->fat_start_lba = 8388608u;
    ok &= expect_rewound_failure(job, first, &request, CTOOL_ERR_OVERFLOW, CTOOL_OBJ_DIAG_LIMIT,
                                 "disk-template i386 size overflow");
    disk_request->image_sectors = SMALL_IMAGE_SECTORS;
    disk_request->fat_start_lba = SMALL_FAT_START_LBA;

    mark = ctool_arena_mark(ctool_job_arena(job));
    status = ctool_obj_transform(job, &request, first, &recovery_result);
    if (status != CTOOL_OK || !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) ||
        !disk_template_small_layout(&recovery_result, boot, kernel_source.contents) ||
        have_repeat == 0 || recovery_result.bytes.size != repeat_result.bytes.size ||
        memcmp(recovery_result.bytes.data, repeat_result.bytes.data,
               (size_t)recovery_result.bytes.size) != 0) {
        (void)fprintf(stderr, "disk-template: same-job recovery failed\n");
        ok = 0;
    }

    ctool_buffer_close(limited);
    ctool_buffer_close(active);
    ctool_buffer_close(repeat);
    ctool_buffer_close(first);
    ctool_job_close(job);
    return ok != 0 ? 0 : 1;
}

static int iso_fixture_layout(const ctool_obj_result_t *result) {
    const ctool_u32 block = 2048u;
    const ctool_u8 *bytes = result->bytes.data;
    const ctool_u32 descriptor = 16u * block;
    const ctool_u32 root = 20u * block;
    const ctool_u32 sub = 21u * block;
    if (bytes == (const ctool_u8 *)0 || result->bytes.size != 25u * block ||
        result->base_address != 0u || result->end_address != 0u) {
        (void)fprintf(stderr, "iso-fixture: result shape differs\n");
        return 0;
    }
    if (memcmp(bytes + descriptor,
               "\x01"
               "CD001\x01",
               7u) != 0 ||
        memcmp(bytes + descriptor + 40u, "CUPID_OS_TEST", 13u) != 0 ||
        read_le32(bytes, descriptor + 80u) != 25u || read_be32(bytes, descriptor + 84u) != 25u ||
        read_le32(bytes, descriptor + 132u) != 22u || read_be32(bytes, descriptor + 136u) != 22u ||
        read_le32(bytes, descriptor + 140u) != 18u || read_be32(bytes, descriptor + 148u) != 19u ||
        read_le32(bytes, descriptor + 158u) != 20u || read_be32(bytes, descriptor + 162u) != 20u ||
        read_le32(bytes, descriptor + 166u) != block ||
        read_be32(bytes, descriptor + 170u) != block ||
        memcmp(bytes + 17u * block,
               "\xff"
               "CD001\x01",
               7u) != 0) {
        (void)fprintf(stderr, "iso-fixture: descriptor layout differs\n");
        return 0;
    }
    if (bytes[18u * block] != 1u || read_le32(bytes, 18u * block + 2u) != 20u ||
        read_le16(bytes, 18u * block + 6u) != 1u || bytes[18u * block + 10u] != 3u ||
        read_le32(bytes, 18u * block + 12u) != 21u || read_le16(bytes, 18u * block + 16u) != 1u ||
        memcmp(bytes + 18u * block + 18u, "SUB", 3u) != 0) {
        (void)fprintf(stderr, "iso-fixture: little path table differs\n");
        return 0;
    }
    if (bytes[19u * block] != 1u || read_be32(bytes, 19u * block + 2u) != 20u ||
        read_be16(bytes, 19u * block + 6u) != 1u || bytes[19u * block + 10u] != 3u ||
        read_be32(bytes, 19u * block + 12u) != 21u || read_be16(bytes, 19u * block + 16u) != 1u ||
        memcmp(bytes + 19u * block + 18u, "SUB", 3u) != 0) {
        (void)fprintf(stderr, "iso-fixture: big path table differs\n");
        return 0;
    }
    if (memcmp(bytes + root + 34u, "SP\x07\x01\xbe\xef\x00", 7u) != 0 ||
        memcmp(bytes + root + 103u, "CE\x1c\x01", 4u) != 0 ||
        read_le32(bytes, root + 107u) != 22u || read_le32(bytes, root + 123u) != 237u ||
        memcmp(bytes + root + 228u + 33u, "EMPTY.;1", 8u) != 0 ||
        memcmp(bytes + root + 342u + 33u, "HELLO.TXT;1", 11u) != 0 ||
        memcmp(bytes + root + 462u + 33u, "SUB", 3u) != 0 ||
        memcmp(bytes + sub + 192u + 33u, "NESTED.BIN;1", 12u) != 0) {
        (void)fprintf(stderr, "iso-fixture: directory records differ\n");
        return 0;
    }
    if (memcmp(bytes + 22u * block, "ER\xed\x01", 4u) != 0 ||
        memcmp(bytes + 23u * block, "hello", 5u) != 0 ||
        memcmp(bytes + 24u * block, "nest", 4u) != 0) {
        (void)fprintf(stderr, "iso-fixture: continuation or files differ\n");
        return 0;
    }
    return 1;
}

static int run_iso_fixture(void) {
    static const char manifest_bytes[] = "empty\nhello.txt\nsub\nsub/nested.bin\n";
    static const char reordered_manifest_bytes[] =
        "sub/nested.bin\r\nsub\r\nhello.txt\r\nempty\r\n";
    static const char orphan_manifest[] = "lost/file.bin\n";
    static const char collision_manifest[] = "A\na\n";
    static const char file_parent_manifest[] = "parent\nparent/child\n";
    static const char wide_manifest[] = "wide.bin\n";
    static const char missing_typed_manifest[] = "empty\nhello.txt\nsub\n";
    static const char extra_manifest[] = "empty\nhello.txt\nsub\nsub/nested.bin\nextra\n";
    static const char long_component[] =
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    static const ctool_u8 hello_bytes[] = {'h', 'e', 'l', 'l', 'o'};
    static const ctool_u8 nested_bytes[] = {'n', 'e', 's', 't'};
    ctool_host_adapter_t adapter;
    ctool_job_config_t config;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_buffer_t *first = (ctool_buffer_t *)0;
    ctool_buffer_t *second = (ctool_buffer_t *)0;
    ctool_buffer_t *limited = (ctool_buffer_t *)0;
    ctool_source_t manifest;
    ctool_source_t hello;
    ctool_source_t nested;
    ctool_source_t empty;
    ctool_source_t invalid_source;
    ctool_source_t wide_source;
    ctool_obj_iso_fixture_entry_t entries[4];
    ctool_obj_iso_fixture_entry_t reordered[4];
    ctool_obj_iso_fixture_entry_t orphan;
    ctool_obj_iso_fixture_entry_t collisions[2];
    ctool_obj_iso_fixture_entry_t file_parent[2];
    ctool_obj_iso_fixture_entry_t wide_entry;
    ctool_obj_request_t request;
    ctool_obj_result_t first_result;
    ctool_obj_result_t second_result;
    ctool_arena_mark_t mark;
    ctool_arena_mark_t before_fill;
    ctool_arena_mark_t full_mark;
    ctool_status_t status;
    void *arena_fill = (void *)0;
    int ok = 1;

    if (!open_job(&adapter, &config, &job)) {
        return 1;
    }
    status = ctool_job_open_buffer(job, 64u, config.limits.output_bytes, &first);
    if (status == CTOOL_OK) {
        status = ctool_job_open_buffer(job, 64u, config.limits.output_bytes, &second);
    }
    if (status == CTOOL_OK) {
        status = ctool_job_open_buffer(job, 64u, 25u * 2048u - 1u, &limited);
    }
    if (status != CTOOL_OK) {
        if (limited != (ctool_buffer_t *)0) {
            ctool_buffer_close(limited);
        }
        if (second != (ctool_buffer_t *)0) {
            ctool_buffer_close(second);
        }
        if (first != (ctool_buffer_t *)0) {
            ctool_buffer_close(first);
        }
        ctool_job_close(job);
        return 1;
    }

    manifest.path.text = ctool_string("/fixtures.manifest");
    manifest.contents = ctool_bytes(manifest_bytes, (ctool_u32)sizeof(manifest_bytes) - 1u);
    hello.path.text = ctool_string("/hello.txt");
    hello.contents = ctool_bytes(hello_bytes, (ctool_u32)sizeof(hello_bytes));
    nested.path.text = ctool_string("/nested.bin");
    nested.contents = ctool_bytes(nested_bytes, (ctool_u32)sizeof(nested_bytes));
    empty.path.text = ctool_string("/empty");
    empty.contents = ctool_bytes((const void *)0, 0u);

    entries[0].path = ctool_string("sub/nested.bin");
    entries[0].kind = CTOOL_OBJ_ISO_FIXTURE_FILE;
    entries[0].source = &nested;
    entries[1].path = ctool_string("empty");
    entries[1].kind = CTOOL_OBJ_ISO_FIXTURE_FILE;
    entries[1].source = &empty;
    entries[2].path = ctool_string("sub");
    entries[2].kind = CTOOL_OBJ_ISO_FIXTURE_DIRECTORY;
    entries[2].source = (const ctool_source_t *)0;
    entries[3].path = ctool_string("hello.txt");
    entries[3].kind = CTOOL_OBJ_ISO_FIXTURE_FILE;
    entries[3].source = &hello;
    reordered[0] = entries[3];
    reordered[1] = entries[2];
    reordered[2] = entries[1];
    reordered[3] = entries[0];

    (void)memset(&request, 0, sizeof(request));
    request.operation = CTOOL_OBJ_BUILD_ISO_FIXTURE;
    request.input = &manifest;
    request.as.iso_fixture.entries = entries;
    request.as.iso_fixture.entry_count = 4u;
    mark = ctool_arena_mark(ctool_job_arena(job));
    status = ctool_obj_transform(job, &request, first, &first_result);
    if (status != CTOOL_OK || !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) ||
        !iso_fixture_layout(&first_result)) {
        (void)fprintf(stderr, "iso-fixture: first transform failed\n");
        ok = 0;
    }
    request.as.iso_fixture.entries = reordered;
    manifest.contents =
        ctool_bytes(reordered_manifest_bytes, (ctool_u32)sizeof(reordered_manifest_bytes) - 1u);
    mark = ctool_arena_mark(ctool_job_arena(job));
    status = ctool_obj_transform(job, &request, second, &second_result);
    if (status != CTOOL_OK || !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) ||
        first_result.bytes.size != second_result.bytes.size ||
        memcmp(first_result.bytes.data, second_result.bytes.data,
               (size_t)first_result.bytes.size) != 0) {
        (void)fprintf(stderr, "iso-fixture: reordered inventory differs\n");
        ok = 0;
    }

    {
        char boundary_paths[512][5];
        ctool_u8 boundary_manifest[512u * 5u];
        ctool_obj_iso_fixture_entry_t boundary_entries[512];
        ctool_u32 index;
        for (index = 0u; index < 512u; index++) {
            boundary_paths[index][0] = 'd';
            boundary_paths[index][1] = (char)('0' + (char)((index / 100u) % 10u));
            boundary_paths[index][2] = (char)('0' + (char)((index / 10u) % 10u));
            boundary_paths[index][3] = (char)('0' + (char)(index % 10u));
            boundary_paths[index][4] = '\0';
            (void)memcpy(boundary_manifest + index * 5u, boundary_paths[index], 4u);
            boundary_manifest[index * 5u + 4u] = (ctool_u8)'\n';
            boundary_entries[index].path = ctool_string(boundary_paths[index]);
            boundary_entries[index].kind = CTOOL_OBJ_ISO_FIXTURE_DIRECTORY;
            boundary_entries[index].source = (const ctool_source_t *)0;
        }
        ctool_buffer_clear(second);
        manifest.contents = ctool_bytes(boundary_manifest, (ctool_u32)sizeof(boundary_manifest));
        request.as.iso_fixture.entries = boundary_entries;
        request.as.iso_fixture.entry_count = 512u;
        mark = ctool_arena_mark(ctool_job_arena(job));
        status = ctool_obj_transform(job, &request, second, &second_result);
        if (status != CTOOL_OK ||
            !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) ||
            second_result.bytes.size % 2048u != 0u) {
            (void)fprintf(stderr, "iso-fixture: complete entry boundary failed\n");
            ok = 0;
        }
    }

    ctool_buffer_clear(second);
    request.as.iso_fixture.entries = entries;
    request.as.iso_fixture.entry_count = 4u;
    manifest.contents = ctool_bytes(manifest_bytes, (ctool_u32)sizeof(manifest_bytes) - 1u);
    ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_LIMIT, CTOOL_OBJ_DIAG_LIMIT,
                                 "iso-fixture output limit");

    manifest.contents = ctool_bytes(orphan_manifest, (ctool_u32)sizeof(orphan_manifest) - 1u);
    orphan.path = ctool_string("lost/file.bin");
    orphan.kind = CTOOL_OBJ_ISO_FIXTURE_FILE;
    orphan.source = &nested;
    request.as.iso_fixture.entries = &orphan;
    request.as.iso_fixture.entry_count = 1u;
    ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_INPUT,
                                 CTOOL_OBJ_DIAG_INVALID_INPUT, "iso-fixture missing parent");

    manifest.contents = ctool_bytes(collision_manifest, (ctool_u32)sizeof(collision_manifest) - 1u);
    collisions[0].path = ctool_string("A");
    collisions[0].kind = CTOOL_OBJ_ISO_FIXTURE_FILE;
    collisions[0].source = &empty;
    collisions[1].path = ctool_string("a");
    collisions[1].kind = CTOOL_OBJ_ISO_FIXTURE_FILE;
    collisions[1].source = &empty;
    request.as.iso_fixture.entries = collisions;
    request.as.iso_fixture.entry_count = 2u;
    ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_INPUT,
                                 CTOOL_OBJ_DIAG_SYMBOL_COLLISION, "iso-fixture case collision");

    request.as.iso_fixture.entries = (const ctool_obj_iso_fixture_entry_t *)0;
    request.as.iso_fixture.entry_count = 1u;
    ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_INVALID_ARGUMENT,
                                 CTOOL_OBJ_DIAG_INVALID_REQUEST, "iso-fixture missing inventory");
    request.as.iso_fixture.entries = entries;
    request.as.iso_fixture.entry_count = 0u;
    ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_INVALID_ARGUMENT,
                                 CTOOL_OBJ_DIAG_INVALID_REQUEST, "iso-fixture empty inventory");
    request.as.iso_fixture.entry_count = 513u;
    ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_LIMIT, CTOOL_OBJ_DIAG_LIMIT,
                                 "iso-fixture entry limit");

    request.as.iso_fixture.entry_count = 4u;
    entries[0].kind = (ctool_obj_iso_fixture_kind_t)0;
    ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_INVALID_ARGUMENT,
                                 CTOOL_OBJ_DIAG_INVALID_REQUEST, "iso-fixture invalid entry kind");
    entries[0].kind = CTOOL_OBJ_ISO_FIXTURE_FILE;
    entries[0].source = (const ctool_source_t *)0;
    ok &=
        expect_rewound_failure(job, limited, &request, CTOOL_ERR_INVALID_ARGUMENT,
                               CTOOL_OBJ_DIAG_INVALID_REQUEST, "iso-fixture file source required");
    entries[0].source = &nested;
    entries[2].source = &empty;
    ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_INVALID_ARGUMENT,
                                 CTOOL_OBJ_DIAG_INVALID_REQUEST,
                                 "iso-fixture directory source forbidden");
    entries[2].source = (const ctool_source_t *)0;
    invalid_source.path.text = ctool_string("/invalid.bin");
    invalid_source.contents = ctool_bytes((const void *)0, 1u);
    entries[0].source = &invalid_source;
    ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_INVALID_ARGUMENT,
                                 CTOOL_OBJ_DIAG_INVALID_INPUT, "iso-fixture invalid file bytes");
    entries[0].source = &nested;

    entries[2].path = ctool_string("/absolute");
    ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_INPUT,
                                 CTOOL_OBJ_DIAG_INVALID_INPUT, "iso-fixture absolute path");
    entries[2].path = ctool_string("double//slash");
    ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_INPUT,
                                 CTOOL_OBJ_DIAG_INVALID_INPUT, "iso-fixture empty component");
    entries[2].path = ctool_string("back\\slash");
    ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_INPUT,
                                 CTOOL_OBJ_DIAG_INVALID_INPUT, "iso-fixture native separator");
    entries[2].path = ctool_string("parent/../escape");
    ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_INPUT,
                                 CTOOL_OBJ_DIAG_INVALID_INPUT, "iso-fixture traversal path");
    entries[2].path = ctool_string("white space");
    ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_INPUT,
                                 CTOOL_OBJ_DIAG_INVALID_INPUT, "iso-fixture whitespace path");
    entries[2].path = ctool_string(long_component);
    ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_INPUT,
                                 CTOOL_OBJ_DIAG_INVALID_INPUT, "iso-fixture component limit");
    entries[2].path = ctool_string("a/b/c/d/e/f/g/h");
    ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_INPUT,
                                 CTOOL_OBJ_DIAG_INVALID_INPUT, "iso-fixture hierarchy limit");
    entries[2].path = ctool_string("sub");

    manifest.contents =
        ctool_bytes(missing_typed_manifest, (ctool_u32)sizeof(missing_typed_manifest) - 1u);
    ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_INPUT,
                                 CTOOL_OBJ_DIAG_INVALID_INPUT,
                                 "iso-fixture typed entry missing manifest");
    manifest.contents = ctool_bytes(extra_manifest, (ctool_u32)sizeof(extra_manifest) - 1u);
    ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_INPUT,
                                 CTOOL_OBJ_DIAG_INVALID_INPUT,
                                 "iso-fixture manifest entry missing input");

    manifest.contents =
        ctool_bytes(file_parent_manifest, (ctool_u32)sizeof(file_parent_manifest) - 1u);
    file_parent[0].path = ctool_string("parent");
    file_parent[0].kind = CTOOL_OBJ_ISO_FIXTURE_FILE;
    file_parent[0].source = &empty;
    file_parent[1].path = ctool_string("parent/child");
    file_parent[1].kind = CTOOL_OBJ_ISO_FIXTURE_FILE;
    file_parent[1].source = &empty;
    request.as.iso_fixture.entries = file_parent;
    request.as.iso_fixture.entry_count = 2u;
    ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_INPUT,
                                 CTOOL_OBJ_DIAG_INVALID_INPUT, "iso-fixture file parent");

    manifest.contents = ctool_bytes(wide_manifest, (ctool_u32)sizeof(wide_manifest) - 1u);
    wide_source.path.text = ctool_string("/wide.bin");
    wide_source.contents = ctool_bytes((const void *)1, 0xffffffffu);
    wide_entry.path = ctool_string("wide.bin");
    wide_entry.kind = CTOOL_OBJ_ISO_FIXTURE_FILE;
    wide_entry.source = &wide_source;
    request.as.iso_fixture.entries = &wide_entry;
    request.as.iso_fixture.entry_count = 1u;
    ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_OVERFLOW, CTOOL_OBJ_DIAG_LIMIT,
                                 "iso-fixture i386 output overflow");

    manifest.contents = ctool_bytes(manifest_bytes, (ctool_u32)sizeof(manifest_bytes) - 1u);
    request.as.iso_fixture.entries = entries;
    request.as.iso_fixture.entry_count = 4u;
    before_fill = ctool_arena_mark(ctool_job_arena(job));
    status = ctool_arena_alloc(ctool_job_arena(job), config.limits.arena_bytes, 1u, &arena_fill);
    full_mark = ctool_arena_mark(ctool_job_arena(job));
    if (status != CTOOL_OK || arena_fill == (void *)0) {
        (void)fprintf(stderr, "iso-fixture: arena-limit setup failed\n");
        ok = 0;
    } else {
        ok &= expect_rewound_failure(job, limited, &request, CTOOL_ERR_LIMIT, CTOOL_OBJ_DIAG_LIMIT,
                                     "iso-fixture arena limit");
        if (!arena_marks_equal(full_mark, ctool_arena_mark(ctool_job_arena(job)))) {
            (void)fprintf(stderr, "iso-fixture: arena failure did not rewind\n");
            ok = 0;
        }
        if (ctool_arena_rewind(ctool_job_arena(job), before_fill) != CTOOL_OK) {
            (void)fprintf(stderr, "iso-fixture: arena recovery setup failed\n");
            ok = 0;
        }
    }
    ctool_buffer_clear(second);
    mark = ctool_arena_mark(ctool_job_arena(job));
    status = ctool_obj_transform(job, &request, second, &second_result);
    if (status != CTOOL_OK || !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job))) ||
        !iso_fixture_layout(&second_result)) {
        (void)fprintf(stderr, "iso-fixture: same-job recovery failed\n");
        ok = 0;
    }

    ctool_buffer_close(limited);
    ctool_buffer_close(second);
    ctool_buffer_close(first);
    ctool_job_close(job);
    return ok != 0 ? 0 : 1;
}
