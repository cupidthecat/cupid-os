/**
 * @file cupidld_pe32_cases.inc
 * @brief Check fixed PE32 layout and named imports.
 */

static int run_pe32_fixed(void) {
    static const ctool_u8 text[] = {0xa1u, 0u, 0u, 0u, 0u, 0xc3u};
    static const ctool_u8 rodata[] = {1u, 2u, 3u};
    static const ctool_u8 data[] = {0x44u, 0x33u, 0x22u, 0x11u};
    static const char *names[4] = {".text", ".rodata", ".data", ".bss"};
    static const ctool_u32 virtual_sizes[4] = {6u, 3u, 4u, 16u};
    static const ctool_u32 virtual_addresses[4] = {0x1000u, 0x2000u, 0x3000u, 0x4000u};
    static const ctool_u32 raw_sizes[4] = {0x200u, 0x200u, 0x200u, 0u};
    static const ctool_u32 raw_offsets[4] = {0x400u, 0x600u, 0x800u, 0u};
    static const ctool_u32 characteristics[4] = {0x60000020u, 0x40000040u, 0xc0000040u,
                                                 0xc0000080u};
    ctool_host_adapter_t adapter;
    ctool_job_config_t config;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_buffer_t *object_buffer = (ctool_buffer_t *)0;
    ctool_buffer_t *output = (ctool_buffer_t *)0;
    ctool_buffer_t *repeat_output = (ctool_buffer_t *)0;
    ctool_buffer_t *failure_output = (ctool_buffer_t *)0;
    ctool_buffer_t *short_output = (ctool_buffer_t *)0;
    ctool_elf32_section_spec_t sections[4];
    ctool_elf32_symbol_spec_t symbols[2];
    ctool_elf32_relocation_spec_t relocation;
    ctool_elf32_object_spec_t object_spec;
    ctool_source_t object_source;
    ctool_ld_request_t request;
    ctool_ld_result_t result;
    ctool_ld_result_t repeat_result;
    ctool_ld_result_t recovery_result;
    ctool_arena_mark_t mark;
    ctool_bytes_t image;
    ctool_u32 section_headers = 0x178u;
    ctool_u32 index;
    ctool_status_t status;
    int passed = 0;
    if (!open_job(&adapter, &config, &job)) {
        return 1;
    }
    status = ctool_job_open_buffer(job, 512u, config.limits.output_bytes, &object_buffer);
    if (status == CTOOL_OK) {
        status = ctool_job_open_buffer(job, 512u, config.limits.output_bytes, &output);
    }
    if (status == CTOOL_OK) {
        status = ctool_job_open_buffer(job, 512u, config.limits.output_bytes, &repeat_output);
    }
    if (status == CTOOL_OK) {
        status = ctool_job_open_buffer(job, 512u, config.limits.output_bytes, &failure_output);
    }
    if (status == CTOOL_OK) {
        status = ctool_job_open_buffer(job, 512u, 0x09ffu, &short_output);
    }
    if (status != CTOOL_OK) {
        (void)fprintf(stderr, "PE32 buffer setup failed: %s\n", ctool_status_name(status));
        goto cleanup;
    }
    (void)memset(sections, 0, sizeof(sections));
    for (index = 0u; index < 4u; index++) {
        sections[index].name = ctool_string(names[index]);
        sections[index].type = index == 3u ? CTOOL_ELF32_SHT_NOBITS : CTOOL_ELF32_SHT_PROGBITS;
        sections[index].flags = CTOOL_ELF32_SHF_ALLOC;
        sections[index].alignment = index == 0u ? 16u : 4u;
    }
    sections[0].flags |= CTOOL_ELF32_SHF_EXECINSTR;
    sections[0].size = (ctool_u32)sizeof(text);
    sections[0].contents = ctool_bytes(text, (ctool_u32)sizeof(text));
    sections[1].size = (ctool_u32)sizeof(rodata);
    sections[1].contents = ctool_bytes(rodata, (ctool_u32)sizeof(rodata));
    sections[2].flags |= CTOOL_ELF32_SHF_WRITE;
    sections[2].size = (ctool_u32)sizeof(data);
    sections[2].contents = ctool_bytes(data, (ctool_u32)sizeof(data));
    sections[3].flags |= CTOOL_ELF32_SHF_WRITE;
    sections[3].size = 16u;
    sections[3].contents = ctool_bytes((const void *)0, 0u);
    (void)memset(symbols, 0, sizeof(symbols));
    symbols[0].name = ctool_string("_start");
    symbols[0].binding = CTOOL_ELF32_BIND_GLOBAL;
    symbols[0].type = CTOOL_ELF32_SYMBOL_FUNCTION;
    symbols[0].visibility = CTOOL_ELF32_VIS_DEFAULT;
    symbols[0].placement = CTOOL_ELF32_SYMBOL_DEFINED;
    symbols[0].section = 0u;
    symbols[0].size = (ctool_u32)sizeof(text);
    symbols[1].name = ctool_string("datum");
    symbols[1].binding = CTOOL_ELF32_BIND_GLOBAL;
    symbols[1].type = CTOOL_ELF32_SYMBOL_OBJECT;
    symbols[1].visibility = CTOOL_ELF32_VIS_DEFAULT;
    symbols[1].placement = CTOOL_ELF32_SYMBOL_DEFINED;
    symbols[1].section = 2u;
    symbols[1].size = (ctool_u32)sizeof(data);
    relocation.target_section = 0u;
    relocation.offset = 1u;
    relocation.symbol = 1u;
    relocation.type = CTOOL_ELF32_R_386_32;
    relocation.addend = 0;
    object_spec.sections = sections;
    object_spec.section_count = 4u;
    object_spec.symbols = symbols;
    object_spec.symbol_count = 2u;
    object_spec.relocations = &relocation;
    object_spec.relocation_count = 1u;
    status = ctool_elf32_write(job, &object_spec, object_buffer);
    if (status != CTOOL_OK) {
        (void)fprintf(stderr, "PE32 fixture write failed: %s\n", ctool_status_name(status));
        goto cleanup;
    }
    object_source.path.text = ctool_string("/pe-layout.o");
    object_source.contents = ctool_buffer_view(object_buffer);
    (void)memset(&request, 0, sizeof(request));
    request.objects = &object_source;
    request.object_count = 1u;
    request.image_kind = CTOOL_LD_IMAGE_PE32_FIXED;
    request.layout.kind = CTOOL_LD_LAYOUT_FIXED_TEXT;
    request.layout.as.fixed_text.base_address = 0x00401000u;
    request.layout.as.fixed_text.entry_symbol = ctool_string("_start");
    request.maximum_image_span = 0x00005000u;
    mark = ctool_arena_mark(ctool_job_arena(job));
    (void)memset(&result, 0xa5, sizeof(result));
    status = ctool_ld_link(job, &request, output, &result);
    image = ctool_buffer_view(output);
    if (status != CTOOL_OK || image.size != 0x0a00u || result.bytes != image.size ||
        result.entry != 0x00401000u || result.load_address != 0x00401000u ||
        result.loaded_end != 0x00403004u || result.memory_end != 0x00404010u ||
        result.output_section_count != 4u || result.resolved_symbol_count != 2u ||
        result.applied_relocation_count != 1u || result.imported_symbol_count != 0u ||
        result.imported_library_count != 0u || image.data[0] != 0x4du || image.data[1] != 0x5au ||
        read_le32(image.data, 0x3cu) != 0x80u || read_le32(image.data, 0x80u) != 0x00004550u ||
        read_le16(image.data, 0x84u) != 0x014cu || read_le16(image.data, 0x86u) != 4u ||
        read_le32(image.data, 0x88u) != 0u || read_le32(image.data, 0x8cu) != 0u ||
        read_le32(image.data, 0x90u) != 0u || read_le16(image.data, 0x94u) != 0x00e0u ||
        read_le16(image.data, 0x96u) != 0x0103u || read_le16(image.data, 0x98u) != 0x010bu ||
        read_le32(image.data, 0x9cu) != 0x0200u || read_le32(image.data, 0xa0u) != 0x0400u ||
        read_le32(image.data, 0xa4u) != 16u || read_le32(image.data, 0xa8u) != 0x1000u ||
        read_le32(image.data, 0xacu) != 0x1000u || read_le32(image.data, 0xb0u) != 0x2000u ||
        read_le32(image.data, 0xb4u) != 0x00400000u || read_le32(image.data, 0xb8u) != 0x1000u ||
        read_le32(image.data, 0xbcu) != 0x0200u || read_le32(image.data, 0xd0u) != 0x5000u ||
        read_le32(image.data, 0xd4u) != 0x0400u || read_le16(image.data, 0xdcu) != 3u ||
        read_le16(image.data, 0xdeu) != 0x0100u || read_le32(image.data, 0xf4u) != 16u) {
        (void)fprintf(stderr, "PE32 image header differs (status=%s)\n", ctool_status_name(status));
        goto cleanup;
    }
    for (index = 0u; index < 16u; index++) {
        if (read_le32(image.data, 0xf8u + index * 8u) != 0u ||
            read_le32(image.data, 0xfcu + index * 8u) != 0u) {
            (void)fprintf(stderr, "PE32 data directory is not empty\n");
            goto cleanup;
        }
    }
    for (index = 0u; index < 4u; index++) {
        ctool_u32 header = section_headers + index * 40u;
        ctool_u32 name_size = (ctool_u32)strlen(names[index]);
        ctool_u32 padding;
        if (memcmp(image.data + header, names[index], name_size) != 0 ||
            image.data[header + name_size] != 0u ||
            read_le32(image.data, header + 8u) != virtual_sizes[index] ||
            read_le32(image.data, header + 12u) != virtual_addresses[index] ||
            read_le32(image.data, header + 16u) != raw_sizes[index] ||
            read_le32(image.data, header + 20u) != raw_offsets[index] ||
            read_le32(image.data, header + 24u) != 0u ||
            read_le32(image.data, header + 28u) != 0u ||
            read_le16(image.data, header + 32u) != 0u ||
            read_le16(image.data, header + 34u) != 0u ||
            read_le32(image.data, header + 36u) != characteristics[index]) {
            (void)fprintf(stderr, "PE32 section %u differs\n", index);
            goto cleanup;
        }
        if (raw_sizes[index] == 0u) {
            continue;
        }
        for (padding = virtual_sizes[index]; padding < raw_sizes[index]; padding++) {
            if (image.data[raw_offsets[index] + padding] != 0u) {
                (void)fprintf(stderr, "PE32 section padding is not zero\n");
                goto cleanup;
            }
        }
    }
    if (read_le32(image.data, 0x0401u) != 0x00403000u ||
        memcmp(image.data + 0x0600u, rodata, sizeof(rodata)) != 0 ||
        memcmp(image.data + 0x0800u, data, sizeof(data)) != 0) {
        (void)fprintf(stderr, "PE32 relocated section data differs\n");
        goto cleanup;
    }
    (void)memset(&repeat_result, 0xa5, sizeof(repeat_result));
    status = ctool_ld_link(job, &request, repeat_output, &repeat_result);
    if (status != CTOOL_OK || memcmp(&result, &repeat_result, sizeof(result)) != 0 ||
        ctool_buffer_view(repeat_output).size != image.size ||
        memcmp(ctool_buffer_view(repeat_output).data, image.data, image.size) != 0) {
        (void)fprintf(stderr, "PE32 output is not deterministic\n");
        goto cleanup;
    }
    request.layout.as.fixed_text.base_address = 0x00402000u;
    if (!expect_link_failure(job, &request, failure_output, CTOOL_ERR_INPUT,
                             CTOOL_LD_DIAG_BAD_LAYOUT, "PE32 text address")) {
        goto cleanup;
    }
    request.layout.as.fixed_text.base_address = 0x00401000u;
    request.maximum_image_span = 0x00004fffu;
    if (!expect_link_failure(job, &request, failure_output, CTOOL_ERR_LIMIT, CTOOL_LD_DIAG_LIMIT,
                             "PE32 image span")) {
        goto cleanup;
    }
    request.maximum_image_span = 0x00005000u;
    if (!expect_link_failure(job, &request, short_output, CTOOL_ERR_LIMIT, CTOOL_LD_DIAG_LIMIT,
                             "PE32 short output")) {
        goto cleanup;
    }
    status = ctool_buffer_append(failure_output, ctool_bytes("sentinel", 8u));
    if (status != CTOOL_OK ||
        !expect_link_failure(job, &request, failure_output, CTOOL_ERR_INPUT,
                             CTOOL_LD_DIAG_NONEMPTY_OUTPUT, "PE32 nonempty output") ||
        ctool_buffer_view(failure_output).size != 8u ||
        memcmp(ctool_buffer_view(failure_output).data, "sentinel", 8u) != 0) {
        goto cleanup;
    }
    status = ctool_buffer_rewind(failure_output, 0u);
    if (status == CTOOL_OK) {
        status = ctool_ld_link(job, &request, failure_output, &recovery_result);
    }
    if (status != CTOOL_OK || memcmp(&result, &recovery_result, sizeof(result)) != 0 ||
        ctool_buffer_view(failure_output).size != image.size ||
        memcmp(ctool_buffer_view(failure_output).data, image.data, image.size) != 0 ||
        !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
        (void)fprintf(stderr, "PE32 same-job recovery differs\n");
        goto cleanup;
    }
    passed = 1;

cleanup:
    if (short_output != (ctool_buffer_t *)0) {
        ctool_buffer_close(short_output);
    }
    if (failure_output != (ctool_buffer_t *)0) {
        ctool_buffer_close(failure_output);
    }
    if (repeat_output != (ctool_buffer_t *)0) {
        ctool_buffer_close(repeat_output);
    }
    if (output != (ctool_buffer_t *)0) {
        ctool_buffer_close(output);
    }
    if (object_buffer != (ctool_buffer_t *)0) {
        ctool_buffer_close(object_buffer);
    }
    if (job != (ctool_job_t *)0) {
        ctool_job_close(job);
    }
    if (passed != 0) {
        (void)puts("pe32-fixed: ok");
    }
    return passed != 0 ? 0 : 1;
}

static int run_pe32_imports(void) {
    static const ctool_u8 text[] = {0xffu, 0x15u, 0u, 0u, 0u, 0u, 0xc3u};
    ctool_host_adapter_t adapter;
    ctool_job_config_t config;
    ctool_job_t *job = (ctool_job_t *)0;
    ctool_buffer_t *object_buffer = (ctool_buffer_t *)0;
    ctool_buffer_t *output = (ctool_buffer_t *)0;
    ctool_buffer_t *repeat_output = (ctool_buffer_t *)0;
    ctool_buffer_t *failure_output = (ctool_buffer_t *)0;
    ctool_elf32_section_spec_t sections[2];
    ctool_elf32_symbol_spec_t symbols[2];
    ctool_elf32_relocation_spec_t relocation;
    ctool_elf32_object_spec_t object_spec;
    ctool_source_t object_source;
    ctool_ld_pe32_import_t import;
    ctool_ld_pe32_import_t duplicate_imports[2];
    ctool_ld_request_t request;
    ctool_ld_result_t result;
    ctool_ld_result_t repeat_result;
    ctool_ld_result_t recovery_result;
    ctool_bytes_t image;
    ctool_arena_mark_t mark;
    ctool_status_t status;
    int passed = 0;
    if (!open_job_with_output_limit(&adapter, &config, &job, 0xffffffffu)) {
        return 1;
    }
    status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes, &object_buffer);
    if (status == CTOOL_OK) {
        status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes, &output);
    }
    if (status == CTOOL_OK) {
        status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes, &repeat_output);
    }
    if (status == CTOOL_OK) {
        status = ctool_job_open_buffer(job, 256u, config.limits.output_bytes, &failure_output);
    }
    if (status != CTOOL_OK) {
        (void)fprintf(stderr, "PE32 import buffer setup failed: %s\n", ctool_status_name(status));
        goto cleanup;
    }
    (void)memset(sections, 0, sizeof(sections));
    sections[0].name = ctool_string(".text");
    sections[0].type = CTOOL_ELF32_SHT_PROGBITS;
    sections[0].flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_EXECINSTR;
    sections[0].alignment = 16u;
    sections[0].size = (ctool_u32)sizeof(text);
    sections[0].contents = ctool_bytes(text, (ctool_u32)sizeof(text));
    (void)memset(symbols, 0, sizeof(symbols));
    symbols[0].name = ctool_string("_start");
    symbols[0].binding = CTOOL_ELF32_BIND_GLOBAL;
    symbols[0].type = CTOOL_ELF32_SYMBOL_FUNCTION;
    symbols[0].visibility = CTOOL_ELF32_VIS_DEFAULT;
    symbols[0].placement = CTOOL_ELF32_SYMBOL_DEFINED;
    symbols[0].section = 0u;
    symbols[0].size = (ctool_u32)sizeof(text);
    symbols[1].name = ctool_string("__imp_ExitProcess");
    symbols[1].binding = CTOOL_ELF32_BIND_GLOBAL;
    symbols[1].type = CTOOL_ELF32_SYMBOL_NOTYPE;
    symbols[1].visibility = CTOOL_ELF32_VIS_DEFAULT;
    symbols[1].placement = CTOOL_ELF32_SYMBOL_UNDEFINED;
    symbols[1].section = CTOOL_ELF32_NO_SECTION;
    (void)memset(&relocation, 0, sizeof(relocation));
    relocation.target_section = 0u;
    relocation.offset = 2u;
    relocation.symbol = 1u;
    relocation.type = CTOOL_ELF32_R_386_32;
    (void)memset(&object_spec, 0, sizeof(object_spec));
    object_spec.sections = sections;
    object_spec.section_count = 1u;
    object_spec.symbols = symbols;
    object_spec.symbol_count = 2u;
    object_spec.relocations = &relocation;
    object_spec.relocation_count = 1u;
    status = ctool_elf32_write(job, &object_spec, object_buffer);
    if (status != CTOOL_OK) {
        (void)fprintf(stderr, "PE32 import fixture write failed: %s\n", ctool_status_name(status));
        goto cleanup;
    }
    object_source.path.text = ctool_string("/pe-import.o");
    object_source.contents = ctool_buffer_view(object_buffer);
    import.symbol_name = ctool_string("__imp_ExitProcess");
    import.library_name = ctool_string("KERNEL32.dll");
    import.procedure_name = ctool_string("ExitProcess");
    (void)memset(&request, 0, sizeof(request));
    request.objects = &object_source;
    request.object_count = 1u;
    request.image_kind = CTOOL_LD_IMAGE_PE32_FIXED;
    request.layout.kind = CTOOL_LD_LAYOUT_FIXED_TEXT;
    request.layout.as.fixed_text.base_address = 0x00401000u;
    request.layout.as.fixed_text.entry_symbol = ctool_string("_start");
    request.pe32_imports = &import;
    request.pe32_import_count = 1u;
    request.maximum_image_span = 0x00003000u;
    mark = ctool_arena_mark(ctool_job_arena(job));
    (void)memset(&result, 0xa5, sizeof(result));
    status = ctool_ld_link(job, &request, output, &result);
    image = ctool_buffer_view(output);
    if (status != CTOOL_OK || image.size != 0x0600u || result.bytes != image.size ||
        result.entry != 0x00401000u || result.load_address != 0x00401000u ||
        result.loaded_end != 0x00402054u || result.memory_end != 0x00402054u ||
        result.output_section_count != 2u || result.resolved_symbol_count != 2u ||
        result.applied_relocation_count != 1u || result.imported_symbol_count != 1u ||
        result.imported_library_count != 1u || read_le16(image.data, 0x86u) != 2u ||
        read_le32(image.data, 0xd0u) != 0x3000u || read_le32(image.data, 0x100u) != 0x2000u ||
        read_le32(image.data, 0x104u) != 0x28u || read_le32(image.data, 0x158u) != 0x2030u ||
        read_le32(image.data, 0x15cu) != 8u || read_le32(image.data, 0x202u) != 0x00402030u ||
        memcmp(image.data + 0x1a0u, ".idata", 6u) != 0 || read_le32(image.data, 0x1a8u) != 0x54u ||
        read_le32(image.data, 0x1acu) != 0x2000u || read_le32(image.data, 0x1b0u) != 0x200u ||
        read_le32(image.data, 0x1b4u) != 0x400u || read_le32(image.data, 0x1c4u) != 0xc0000040u ||
        read_le32(image.data, 0x400u) != 0x2028u || read_le32(image.data, 0x40cu) != 0x2038u ||
        read_le32(image.data, 0x410u) != 0x2030u || read_le32(image.data, 0x428u) != 0x2046u ||
        read_le32(image.data, 0x42cu) != 0u || read_le32(image.data, 0x430u) != 0x2046u ||
        read_le32(image.data, 0x434u) != 0u ||
        memcmp(image.data + 0x438u, "KERNEL32.dll", 12u) != 0 || image.data[0x444u] != 0u ||
        read_le16(image.data, 0x446u) != 0u ||
        memcmp(image.data + 0x448u, "ExitProcess", 11u) != 0 || image.data[0x453u] != 0u) {
        (void)fprintf(stderr, "PE32 import image differs (status=%s)\n", ctool_status_name(status));
        goto cleanup;
    }
    (void)memset(&repeat_result, 0xa5, sizeof(repeat_result));
    status = ctool_ld_link(job, &request, repeat_output, &repeat_result);
    if (status != CTOOL_OK || memcmp(&result, &repeat_result, sizeof(result)) != 0 ||
        ctool_buffer_view(repeat_output).size != image.size ||
        memcmp(ctool_buffer_view(repeat_output).data, image.data, image.size) != 0) {
        (void)fprintf(stderr, "PE32 import output is not deterministic\n");
        goto cleanup;
    }
    request.pe32_imports = (const ctool_ld_pe32_import_t *)0;
    if (!expect_link_failure(job, &request, failure_output, CTOOL_ERR_INVALID_ARGUMENT,
                             CTOOL_LD_DIAG_INVALID_REQUEST, "PE32 missing import array")) {
        goto cleanup;
    }
    request.pe32_imports = &import;
    request.maximum_image_span = 0x00002000u;
    if (!expect_link_failure(job, &request, failure_output, CTOOL_ERR_LIMIT, CTOOL_LD_DIAG_LIMIT,
                             "PE32 imported image span")) {
        goto cleanup;
    }
    request.maximum_image_span = 0x00003000u;
    import.library_name = ctool_string("KERNEL32/evil.dll");
    if (!expect_link_failure(job, &request, failure_output, CTOOL_ERR_INPUT,
                             CTOOL_LD_DIAG_BAD_IMPORT, "PE32 unsafe import name")) {
        goto cleanup;
    }
    import.library_name = ctool_string("KERNEL32.dll");
    duplicate_imports[0] = import;
    duplicate_imports[1] = import;
    request.pe32_imports = duplicate_imports;
    request.pe32_import_count = 2u;
    if (!expect_link_failure(job, &request, failure_output, CTOOL_ERR_INPUT,
                             CTOOL_LD_DIAG_BAD_IMPORT, "PE32 duplicate import")) {
        goto cleanup;
    }
    request.image_kind = CTOOL_LD_IMAGE_ELF32;
    request.pe32_imports = &import;
    request.pe32_import_count = 1u;
    if (!expect_link_failure(job, &request, failure_output, CTOOL_ERR_INPUT,
                             CTOOL_LD_DIAG_BAD_IMPORT, "non-PE import")) {
        goto cleanup;
    }
    request.image_kind = CTOOL_LD_IMAGE_PE32_FIXED;
    sections[1].name = ctool_string(".bss");
    sections[1].type = CTOOL_ELF32_SHT_NOBITS;
    sections[1].flags = CTOOL_ELF32_SHF_ALLOC | CTOOL_ELF32_SHF_WRITE;
    sections[1].alignment = 4096u;
    sections[1].size = 0x7fffe000u;
    object_spec.section_count = 2u;
    ctool_buffer_clear(object_buffer);
    status = ctool_elf32_write(job, &object_spec, object_buffer);
    object_source.contents = ctool_buffer_view(object_buffer);
    request.maximum_image_span = 0xffffffffu;
    if (status != CTOOL_OK ||
        !expect_link_failure(job, &request, failure_output, CTOOL_ERR_INPUT,
                             CTOOL_LD_DIAG_BAD_IMPORT, "PE32 import ordinal RVA")) {
        goto cleanup;
    }
    object_spec.section_count = 1u;
    request.maximum_image_span = 0x00003000u;
    relocation.addend = 4;
    ctool_buffer_clear(object_buffer);
    status = ctool_elf32_write(job, &object_spec, object_buffer);
    object_source.contents = ctool_buffer_view(object_buffer);
    if (status != CTOOL_OK ||
        !expect_link_failure(job, &request, failure_output, CTOOL_ERR_INPUT,
                             CTOOL_LD_DIAG_BAD_IMPORT, "PE32 nonzero import addend")) {
        goto cleanup;
    }
    relocation.addend = 0;
    ctool_buffer_clear(object_buffer);
    status = ctool_elf32_write(job, &object_spec, object_buffer);
    object_source.contents = ctool_buffer_view(object_buffer);
    if (status != CTOOL_OK) {
        (void)fprintf(stderr, "PE32 import recovery fixture write failed: %s\n",
                      ctool_status_name(status));
        goto cleanup;
    }
    status = ctool_ld_link(job, &request, failure_output, &recovery_result);
    if (status != CTOOL_OK || memcmp(&result, &recovery_result, sizeof(result)) != 0 ||
        ctool_buffer_view(failure_output).size != image.size ||
        memcmp(ctool_buffer_view(failure_output).data, image.data, image.size) != 0 ||
        !arena_marks_equal(mark, ctool_arena_mark(ctool_job_arena(job)))) {
        (void)fprintf(stderr, "PE32 import recovery differs\n");
        goto cleanup;
    }
    passed = 1;

cleanup:
    if (failure_output != (ctool_buffer_t *)0) {
        ctool_buffer_close(failure_output);
    }
    if (repeat_output != (ctool_buffer_t *)0) {
        ctool_buffer_close(repeat_output);
    }
    if (output != (ctool_buffer_t *)0) {
        ctool_buffer_close(output);
    }
    if (object_buffer != (ctool_buffer_t *)0) {
        ctool_buffer_close(object_buffer);
    }
    if (job != (ctool_job_t *)0) {
        ctool_job_close(job);
    }
    if (passed != 0) {
        (void)puts("pe32-imports: ok");
    }
    return passed != 0 ? 0 : 1;
}
