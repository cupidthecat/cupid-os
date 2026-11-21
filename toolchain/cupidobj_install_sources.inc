/**
 * @file cupidobj_install_sources.inc
 * @brief Generate the source tables installed in Cupid OS.
 */

static ctool_bool obj_install_demo_stem(ctool_string_t path, ctool_string_t *stem_out) {
    ctool_string_t prefix = ctool_string("demos/");
    ctool_string_t suffix = ctool_string(".asm");
    ctool_u32 index;
    if (stem_out == (ctool_string_t *)0 || obj_string_valid(path) == CTOOL_FALSE ||
        obj_string_has_prefix(path, "demos/") == CTOOL_FALSE ||
        obj_string_has_suffix(path, ".asm") == CTOOL_FALSE ||
        path.size <= prefix.size + suffix.size) {
        return CTOOL_FALSE;
    }
    stem_out->data = path.data + prefix.size;
    stem_out->size = path.size - prefix.size - suffix.size;
    for (index = 0u; index < stem_out->size; index++) {
        unsigned char character = (unsigned char)stem_out->data[index];
        if (!((character >= (unsigned char)'a' && character <= (unsigned char)'z') ||
              (character >= (unsigned char)'A' && character <= (unsigned char)'Z') ||
              (character >= (unsigned char)'0' && character <= (unsigned char)'9') ||
              character == (unsigned char)'_')) {
            return CTOOL_FALSE;
        }
    }
    return CTOOL_TRUE;
}

static ctool_bool obj_install_plain_stem(ctool_string_t path, const char *prefix_text,
                                         const char *suffix_text, ctool_string_t *stem_out) {
    ctool_string_t prefix = ctool_string(prefix_text);
    ctool_string_t suffix = ctool_string(suffix_text);
    ctool_u32 index;
    if (stem_out == (ctool_string_t *)0 || obj_string_valid(path) == CTOOL_FALSE ||
        obj_string_has_prefix(path, prefix_text) == CTOOL_FALSE ||
        obj_string_has_suffix(path, suffix_text) == CTOOL_FALSE ||
        path.size <= prefix.size + suffix.size) {
        return CTOOL_FALSE;
    }
    stem_out->data = path.data + prefix.size;
    stem_out->size = path.size - prefix.size - suffix.size;
    for (index = 0u; index < stem_out->size; index++) {
        unsigned char character = (unsigned char)stem_out->data[index];
        if (!((character >= (unsigned char)'a' && character <= (unsigned char)'z') ||
              (character >= (unsigned char)'A' && character <= (unsigned char)'Z') ||
              (character >= (unsigned char)'0' && character <= (unsigned char)'9') ||
              character == (unsigned char)'_')) {
            return CTOOL_FALSE;
        }
    }
    return CTOOL_TRUE;
}

static ctool_status_t obj_install_emit_extern(ctool_buffer_t *output, const char *symbol_prefix,
                                              ctool_string_t stem, const char *symbol_suffix) {
    ctool_status_t status = obj_append_literal(output, "extern const char ");
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, symbol_prefix);
    }
    if (status == CTOOL_OK) {
        status = obj_append_string(output, stem);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, symbol_suffix);
    }
    return status;
}

static ctool_status_t obj_install_emit_bin_entry(ctool_buffer_t *output, const char *symbol_prefix,
                                                 ctool_string_t stem, const char *symbol_type,
                                                 const char *install_directory,
                                                 const char *extension, const char *log_directory) {
    ctool_status_t status = obj_append_literal(output, "    { uint32_t sz = (uint32_t)(");
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, symbol_prefix);
    }
    if (status == CTOOL_OK) {
        status = obj_append_string(output, stem);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, symbol_type);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, "_end - ");
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, symbol_prefix);
    }
    if (status == CTOOL_OK) {
        status = obj_append_string(output, stem);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, symbol_type);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, "_start); ramfs_add_file(fs_private, \"");
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, install_directory);
    }
    if (status == CTOOL_OK) {
        status = obj_append_string(output, stem);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, extension);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, "\", ");
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, symbol_prefix);
    }
    if (status == CTOOL_OK) {
        status = obj_append_string(output, stem);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, symbol_type);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, "_start, sz); serial_printf(\"[kernel] Installed ");
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, log_directory);
    }
    if (status == CTOOL_OK) {
        status = obj_append_string(output, stem);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, extension);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, " (%u bytes)\\n\", sz); }\n");
    }
    return status;
}

static ctool_status_t obj_install_validate_plain_list(ctool_job_t *job,
                                                      const ctool_source_t *source,
                                                      const ctool_string_t *paths, ctool_u32 count,
                                                      const char *prefix, const char *suffix,
                                                      ctool_u32 column_base, const char *message) {
    ctool_u32 index;
    if (count != 0u && paths == (const ctool_string_t *)0) {
        return obj_emit_failure(job, source, CTOOL_ERR_INVALID_ARGUMENT,
                                CTOOL_OBJ_DIAG_INVALID_REQUEST, column_base,
                                "CupidObj installation path list is missing");
    }
    for (index = 0u; index < count; index++) {
        ctool_string_t stem;
        ctool_u32 prior;
        if (obj_install_plain_stem(paths[index], prefix, suffix, &stem) == CTOOL_FALSE) {
            return obj_emit_failure(job, source, CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_INVALID_INPUT,
                                    column_base + index, message);
        }
        for (prior = 0u; prior < index; prior++) {
            if (obj_string_equal(paths[prior], paths[index]) == CTOOL_TRUE) {
                return obj_emit_failure(job, source, CTOOL_ERR_INPUT,
                                        CTOOL_OBJ_DIAG_SYMBOL_COLLISION, column_base + index,
                                        "CupidObj installation path is duplicated");
            }
        }
    }
    return CTOOL_OK;
}

static void obj_install_bin_symbol(const ctool_obj_install_source_request_t *install,
                                   ctool_u32 index, obj_install_symbol_t *symbol_out) {
    if (index < install->bin_count) {
        (void)obj_install_plain_stem(install->bin_paths[index], "bin/", ".cc", &symbol_out->stem);
        symbol_out->prefix = "_binary_bin_";
        symbol_out->suffix = "_cc";
        symbol_out->path = install->bin_paths[index];
        symbol_out->kind = OBJ_INSTALL_SYMBOL_BIN;
        return;
    }
    index -= install->bin_count;
    if (index < install->header_count) {
        (void)obj_install_plain_stem(install->header_paths[index], "bin/", ".h", &symbol_out->stem);
        symbol_out->prefix = "_binary_bin_";
        symbol_out->suffix = "_h";
        symbol_out->path = install->header_paths[index];
        symbol_out->kind = OBJ_INSTALL_SYMBOL_HEADER;
        return;
    }
    index -= install->header_count;
    (void)obj_install_plain_stem(install->browser_paths[index], "bin/browser/", ".cc",
                                 &symbol_out->stem);
    symbol_out->prefix = "_binary_bin_browser_";
    symbol_out->suffix = "_cc";
    symbol_out->path = install->browser_paths[index];
    symbol_out->kind = OBJ_INSTALL_SYMBOL_BROWSER;
}

static ctool_status_t
obj_install_validate_bin_symbols(ctool_job_t *job, const ctool_source_t *source,
                                 const ctool_obj_install_source_request_t *install) {
    ctool_u32 count = install->bin_count + install->header_count + install->browser_count;
    ctool_u32 index;
    for (index = 0u; index < count; index++) {
        obj_install_symbol_t current;
        ctool_u32 prior;
        obj_install_bin_symbol(install, index, &current);
        for (prior = 0u; prior < index; prior++) {
            obj_install_symbol_t earlier;
            obj_install_bin_symbol(install, prior, &earlier);
            if (obj_install_symbols_equal(&earlier, &current) == CTOOL_TRUE) {
                return obj_emit_failure(
                    job, source, CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_SYMBOL_COLLISION, index,
                    "CupidObj installation paths map to the same binary symbol");
            }
        }
    }
    return CTOOL_OK;
}

static ctool_status_t obj_install_output_failure(ctool_job_t *job, const ctool_source_t *source,
                                                 ctool_status_t status) {
    ctool_u32 code =
        status == CTOOL_ERR_LIMIT || status == CTOOL_ERR_OVERFLOW || status == CTOOL_ERR_NO_MEMORY
            ? CTOOL_OBJ_DIAG_LIMIT
            : CTOOL_OBJ_DIAG_OUTPUT;
    return obj_emit_failure(job, source, status, code, 0u,
                            "CupidObj could not emit installation source");
}

static ctool_bool obj_install_count_fits(ctool_u32 count, ctool_u32 *total) {
    if (count > CTOOL_OBJ_INSTALL_PATH_LIMIT || *total > CTOOL_OBJ_INSTALL_PATH_LIMIT - count) {
        return CTOOL_FALSE;
    }
    *total += count;
    return CTOOL_TRUE;
}

static ctool_status_t obj_install_validate_path_limit(ctool_job_t *job,
                                                      const ctool_obj_request_t *request) {
    const ctool_obj_install_source_request_t *install = &request->as.install_source;
    ctool_u32 total = 0u;
    if (obj_install_count_fits(install->bin_count, &total) == CTOOL_FALSE ||
        obj_install_count_fits(install->header_count, &total) == CTOOL_FALSE ||
        obj_install_count_fits(install->browser_count, &total) == CTOOL_FALSE ||
        obj_install_count_fits(install->ctxt_count, &total) == CTOOL_FALSE ||
        obj_install_count_fits(install->doc_asset_count, &total) == CTOOL_FALSE ||
        obj_install_count_fits(install->home_asset_count, &total) == CTOOL_FALSE ||
        obj_install_count_fits(install->demo_count, &total) == CTOOL_FALSE) {
        return obj_emit_failure(job, request->input, CTOOL_ERR_LIMIT, CTOOL_OBJ_DIAG_LIMIT, 0u,
                                "CupidObj installation inventory exceeds 512 paths");
    }
    return CTOOL_OK;
}

static ctool_status_t obj_install_demos(ctool_job_t *job, const ctool_obj_request_t *request,
                                        ctool_buffer_t *output, ctool_obj_result_t *result_out) {
    const ctool_obj_install_source_request_t *install = &request->as.install_source;
    ctool_u32 index;
    ctool_status_t status;
    if (install->demo_paths == (const ctool_string_t *)0 || install->demo_count == 0u) {
        return obj_emit_failure(job, request->input, CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_INVALID_INPUT,
                                0u, "CupidObj demo installation list is empty");
    }
    for (index = 0u; index < install->demo_count; index++) {
        ctool_string_t stem;
        ctool_u32 prior;
        if (obj_install_demo_stem(install->demo_paths[index], &stem) == CTOOL_FALSE) {
            return obj_emit_failure(job, request->input, CTOOL_ERR_INPUT,
                                    CTOOL_OBJ_DIAG_INVALID_INPUT, index,
                                    "CupidObj demo path must match demos/NAME.asm");
        }
        for (prior = 0u; prior < index; prior++) {
            if (obj_string_equal(install->demo_paths[prior], install->demo_paths[index]) ==
                CTOOL_TRUE) {
                return obj_emit_failure(job, request->input, CTOOL_ERR_INPUT,
                                        CTOOL_OBJ_DIAG_SYMBOL_COLLISION, index,
                                        "CupidObj demo installation path is duplicated");
            }
        }
    }
    status =
        obj_append_literal(output, "/* Auto-generated -- do not edit. */\n"
                                   "/* Lists all embedded CupidASM demos from demos/ directory */\n"
                                   "#include \"ramfs.h\"\n"
                                   "#include \"types.h\"\n"
                                   "#include \"../drivers/serial.h\"\n");
    for (index = 0u; status == CTOOL_OK && index < install->demo_count; index++) {
        ctool_string_t stem;
        (void)obj_install_demo_stem(install->demo_paths[index], &stem);
        status = obj_append_literal(output, "extern const char _binary_demos_");
        if (status == CTOOL_OK) {
            status = obj_append_string(output, stem);
        }
        if (status == CTOOL_OK) {
            status = obj_append_literal(output, "_asm_start[];\n");
        }
    }
    for (index = 0u; status == CTOOL_OK && index < install->demo_count; index++) {
        ctool_string_t stem;
        (void)obj_install_demo_stem(install->demo_paths[index], &stem);
        status = obj_append_literal(output, "extern const char _binary_demos_");
        if (status == CTOOL_OK) {
            status = obj_append_string(output, stem);
        }
        if (status == CTOOL_OK) {
            status = obj_append_literal(output, "_asm_end[];\n");
        }
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, "void install_demo_programs(void *fs_private);\n"
                                            "void install_demo_programs(void *fs_private) {\n");
    }
    for (index = 0u; status == CTOOL_OK && index < install->demo_count; index++) {
        ctool_string_t stem;
        (void)obj_install_demo_stem(install->demo_paths[index], &stem);
        status = obj_append_literal(output, "    { uint32_t sz = (uint32_t)(_binary_demos_");
        if (status == CTOOL_OK) {
            status = obj_append_string(output, stem);
        }
        if (status == CTOOL_OK) {
            status = obj_append_literal(output, "_asm_end - _binary_demos_");
        }
        if (status == CTOOL_OK) {
            status = obj_append_string(output, stem);
        }
        if (status == CTOOL_OK) {
            status = obj_append_literal(output, "_asm_start); ramfs_add_file(fs_private, \"demos/");
        }
        if (status == CTOOL_OK) {
            status = obj_append_string(output, stem);
        }
        if (status == CTOOL_OK) {
            status = obj_append_literal(output, ".asm\", _binary_demos_");
        }
        if (status == CTOOL_OK) {
            status = obj_append_string(output, stem);
        }
        if (status == CTOOL_OK) {
            status = obj_append_literal(
                output, "_asm_start, sz); serial_printf(\"[kernel] Installed /demos/");
        }
        if (status == CTOOL_OK) {
            status = obj_append_string(output, stem);
        }
        if (status == CTOOL_OK) {
            status =
                obj_append_literal(output, ".asm (%u bytes)\\n\", sz); ramfs_add_file(fs_private, "
                                           "\"docs/demos/");
        }
        if (status == CTOOL_OK) {
            status = obj_append_string(output, stem);
        }
        if (status == CTOOL_OK) {
            status = obj_append_literal(output, ".asm\", _binary_demos_");
        }
        if (status == CTOOL_OK) {
            status = obj_append_string(output, stem);
        }
        if (status == CTOOL_OK) {
            status = obj_append_literal(
                output, "_asm_start, sz); serial_printf(\"[kernel] Installed /docs/demos/");
        }
        if (status == CTOOL_OK) {
            status = obj_append_string(output, stem);
        }
        if (status == CTOOL_OK) {
            status = obj_append_literal(output, ".asm (%u bytes)\\n\", sz); }\n");
        }
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, "}\n");
    }
    if (status != CTOOL_OK) {
        return obj_install_output_failure(job, request->input, status);
    }
    result_out->bytes = ctool_buffer_view(output);
    return CTOOL_OK;
}

static ctool_status_t obj_install_bin(ctool_job_t *job, const ctool_obj_request_t *request,
                                      ctool_buffer_t *output, ctool_obj_result_t *result_out) {
    const ctool_obj_install_source_request_t *install = &request->as.install_source;
    ctool_status_t status;
    ctool_u32 index;
    if (install->bin_count + install->header_count + install->browser_count == 0u) {
        return obj_emit_failure(job, request->input, CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_INVALID_INPUT,
                                0u, "CupidObj bin installation lists are empty");
    }
    status = obj_install_validate_plain_list(job, request->input, install->bin_paths,
                                             install->bin_count, "bin/", ".cc", 0u,
                                             "CupidObj program path must match bin/NAME.cc");
    if (status == CTOOL_OK) {
        status = obj_install_validate_plain_list(
            job, request->input, install->header_paths, install->header_count, "bin/", ".h",
            install->bin_count, "CupidObj header path must match bin/NAME.h");
    }
    if (status == CTOOL_OK) {
        status = obj_install_validate_plain_list(
            job, request->input, install->browser_paths, install->browser_count, "bin/browser/",
            ".cc", install->bin_count + install->header_count,
            "CupidObj browser path must match bin/browser/NAME.cc");
    }
    if (status == CTOOL_OK) {
        status = obj_install_validate_bin_symbols(job, request->input, install);
    }
    if (status != CTOOL_OK) {
        return status;
    }
    status =
        obj_append_literal(output, "/* Auto-generated -- do not edit. */\n"
                                   "/* Lists all embedded CupidC programs from bin/ directory */\n"
                                   "#include \"ramfs.h\"\n"
                                   "#include \"types.h\"\n"
                                   "#include \"../drivers/serial.h\"\n");
    for (index = 0u; status == CTOOL_OK && index < install->bin_count; index++) {
        ctool_string_t stem;
        (void)obj_install_plain_stem(install->bin_paths[index], "bin/", ".cc", &stem);
        status = obj_install_emit_extern(output, "_binary_bin_", stem, "_cc_start[];\n");
    }
    for (index = 0u; status == CTOOL_OK && index < install->header_count; index++) {
        ctool_string_t stem;
        (void)obj_install_plain_stem(install->header_paths[index], "bin/", ".h", &stem);
        status = obj_install_emit_extern(output, "_binary_bin_", stem, "_h_start[];\n");
    }
    for (index = 0u; status == CTOOL_OK && index < install->bin_count; index++) {
        ctool_string_t stem;
        (void)obj_install_plain_stem(install->bin_paths[index], "bin/", ".cc", &stem);
        status = obj_install_emit_extern(output, "_binary_bin_", stem, "_cc_end[];\n");
    }
    for (index = 0u; status == CTOOL_OK && index < install->header_count; index++) {
        ctool_string_t stem;
        (void)obj_install_plain_stem(install->header_paths[index], "bin/", ".h", &stem);
        status = obj_install_emit_extern(output, "_binary_bin_", stem, "_h_end[];\n");
    }
    for (index = 0u; status == CTOOL_OK && index < install->browser_count; index++) {
        ctool_string_t stem;
        (void)obj_install_plain_stem(install->browser_paths[index], "bin/browser/", ".cc", &stem);
        status = obj_install_emit_extern(output, "_binary_bin_browser_", stem, "_cc_start[];\n");
    }
    for (index = 0u; status == CTOOL_OK && index < install->browser_count; index++) {
        ctool_string_t stem;
        (void)obj_install_plain_stem(install->browser_paths[index], "bin/browser/", ".cc", &stem);
        status = obj_install_emit_extern(output, "_binary_bin_browser_", stem, "_cc_end[];\n");
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, "void install_bin_programs(void *fs_private);\n"
                                            "void install_bin_programs(void *fs_private) {\n");
    }
    for (index = 0u; status == CTOOL_OK && index < install->bin_count; index++) {
        ctool_string_t stem;
        (void)obj_install_plain_stem(install->bin_paths[index], "bin/", ".cc", &stem);
        status =
            obj_install_emit_bin_entry(output, "_binary_bin_", stem, "_cc", "bin/", ".cc", "/bin/");
    }
    for (index = 0u; status == CTOOL_OK && index < install->header_count; index++) {
        ctool_string_t stem;
        (void)obj_install_plain_stem(install->header_paths[index], "bin/", ".h", &stem);
        status =
            obj_install_emit_bin_entry(output, "_binary_bin_", stem, "_h", "bin/", ".h", "/bin/");
    }
    for (index = 0u; status == CTOOL_OK && index < install->browser_count; index++) {
        ctool_string_t stem;
        (void)obj_install_plain_stem(install->browser_paths[index], "bin/browser/", ".cc", &stem);
        status = obj_install_emit_bin_entry(output, "_binary_bin_browser_", stem, "_cc",
                                            "bin/browser/", ".cc", "/bin/browser/");
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, "}\n");
    }
    if (status != CTOOL_OK) {
        return obj_install_output_failure(job, request->input, status);
    }
    result_out->bytes = ctool_buffer_view(output);
    return CTOOL_OK;
}

static ctool_bool obj_install_docs_stem(ctool_string_t path, const char *prefix_text,
                                        const char *suffix_text, ctool_string_t *stem_out) {
    ctool_string_t prefix = ctool_string(prefix_text);
    ctool_string_t suffix = ctool_string(suffix_text);
    ctool_u32 index;
    if (stem_out == (ctool_string_t *)0 || obj_string_valid(path) == CTOOL_FALSE ||
        obj_string_has_prefix(path, prefix_text) == CTOOL_FALSE ||
        obj_string_has_suffix(path, suffix_text) == CTOOL_FALSE ||
        path.size <= prefix.size + suffix.size) {
        return CTOOL_FALSE;
    }
    stem_out->data = path.data + prefix.size;
    stem_out->size = path.size - prefix.size - suffix.size;
    for (index = 0u; index < stem_out->size; index++) {
        unsigned char character = (unsigned char)stem_out->data[index];
        if (!((character >= (unsigned char)'a' && character <= (unsigned char)'z') ||
              (character >= (unsigned char)'A' && character <= (unsigned char)'Z') ||
              (character >= (unsigned char)'0' && character <= (unsigned char)'9') ||
              character == (unsigned char)'_' || character == (unsigned char)'-')) {
            return CTOOL_FALSE;
        }
    }
    return CTOOL_TRUE;
}

static ctool_status_t obj_install_append_symbol_stem(ctool_buffer_t *output, ctool_string_t stem) {
    ctool_u32 index;
    for (index = 0u; index < stem.size; index++) {
        ctool_u8 character = (ctool_u8)stem.data[index];
        ctool_status_t status;
        if (character == (ctool_u8)'-') {
            character = (ctool_u8)'_';
        }
        status = ctool_buffer_put_u8(output, character);
        if (status != CTOOL_OK) {
            return status;
        }
    }
    return CTOOL_OK;
}

static ctool_status_t obj_install_emit_docs_extern(ctool_buffer_t *output,
                                                   const char *symbol_prefix, ctool_string_t stem,
                                                   const char *symbol_extension,
                                                   const char *boundary) {
    ctool_status_t status = obj_append_literal(output, "extern const char ");
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, symbol_prefix);
    }
    if (status == CTOOL_OK) {
        status = obj_install_append_symbol_stem(output, stem);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, symbol_extension);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, boundary);
    }
    return status;
}

static ctool_status_t obj_install_emit_docs_entry(ctool_buffer_t *output, const char *symbol_prefix,
                                                  ctool_string_t stem, const char *symbol_extension,
                                                  const char *install_directory,
                                                  const char *file_extension,
                                                  const char *log_directory) {
    ctool_status_t status = obj_append_literal(output, "    { uint32_t sz = (uint32_t)(");
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, symbol_prefix);
    }
    if (status == CTOOL_OK) {
        status = obj_install_append_symbol_stem(output, stem);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, symbol_extension);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, "_end - ");
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, symbol_prefix);
    }
    if (status == CTOOL_OK) {
        status = obj_install_append_symbol_stem(output, stem);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, symbol_extension);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, "_start); ramfs_add_file(fs_private, \"");
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, install_directory);
    }
    if (status == CTOOL_OK) {
        status = obj_append_string(output, stem);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, file_extension);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, "\", ");
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, symbol_prefix);
    }
    if (status == CTOOL_OK) {
        status = obj_install_append_symbol_stem(output, stem);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, symbol_extension);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, "_start, sz); serial_printf(\"[kernel] Installed ");
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, log_directory);
    }
    if (status == CTOOL_OK) {
        status = obj_append_string(output, stem);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, file_extension);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, " (%u bytes)\\n\", sz); }\n");
    }
    return status;
}

static ctool_status_t obj_install_emit_home_entry(ctool_buffer_t *output, ctool_string_t stem,
                                                  const char *extension) {
    ctool_status_t status = obj_append_literal(output, "    { uint32_t sz = (uint32_t)(_binary_");
    if (status == CTOOL_OK) {
        status = obj_install_append_symbol_stem(output, stem);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, "_");
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, extension);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, "_end - _binary_");
    }
    if (status == CTOOL_OK) {
        status = obj_install_append_symbol_stem(output, stem);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, "_");
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, extension);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, "_start); install_home_asset(\"/home/");
    }
    if (status == CTOOL_OK) {
        status = obj_append_string(output, stem);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, ".");
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, extension);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, "\", _binary_");
    }
    if (status == CTOOL_OK) {
        status = obj_install_append_symbol_stem(output, stem);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, "_");
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, extension);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, "_start, sz); }\n");
    }
    return status;
}

static ctool_u32 obj_install_home_extension(ctool_string_t path, ctool_string_t *stem_out) {
    static const char *extensions[] = {".bmp", ".png", ".jpg", ".jpeg"};
    ctool_u32 index;
    for (index = 0u; index < 4u; index++) {
        if (obj_install_docs_stem(path, "", extensions[index], stem_out) == CTOOL_TRUE) {
            return index + 1u;
        }
    }
    return 0u;
}

static void obj_install_docs_symbol(const ctool_obj_install_source_request_t *install,
                                    ctool_u32 index, obj_install_symbol_t *symbol_out) {
    static const char *home_symbol_suffixes[] = {"_bmp", "_png", "_jpg", "_jpeg"};
    ctool_u32 extension;
    if (index < install->ctxt_count) {
        (void)obj_install_docs_stem(install->ctxt_paths[index], "cupidos-txt/", ".CTXT",
                                    &symbol_out->stem);
        symbol_out->prefix = "_binary_cupidos_txt_";
        symbol_out->suffix = "_CTXT";
        symbol_out->path = install->ctxt_paths[index];
        symbol_out->kind = OBJ_INSTALL_SYMBOL_CTXT;
        return;
    }
    index -= install->ctxt_count;
    if (index < install->doc_asset_count) {
        (void)obj_install_docs_stem(install->doc_asset_paths[index], "", ".bmp", &symbol_out->stem);
        symbol_out->prefix = "_binary_";
        symbol_out->suffix = "_bmp";
        symbol_out->path = install->doc_asset_paths[index];
        symbol_out->kind = OBJ_INSTALL_SYMBOL_DOC_ASSET;
        return;
    }
    index -= install->doc_asset_count;
    extension = obj_install_home_extension(install->home_asset_paths[index], &symbol_out->stem);
    symbol_out->prefix = "_binary_";
    symbol_out->suffix = home_symbol_suffixes[extension - 1u];
    symbol_out->path = install->home_asset_paths[index];
    symbol_out->kind = OBJ_INSTALL_SYMBOL_HOME_ASSET;
}

static ctool_bool obj_install_docs_alias_allowed(const obj_install_symbol_t *left,
                                                 const obj_install_symbol_t *right) {
    if (obj_string_equal(left->path, right->path) == CTOOL_FALSE) {
        return CTOOL_FALSE;
    }
    return ((left->kind == OBJ_INSTALL_SYMBOL_DOC_ASSET &&
             right->kind == OBJ_INSTALL_SYMBOL_HOME_ASSET) ||
            (left->kind == OBJ_INSTALL_SYMBOL_HOME_ASSET &&
             right->kind == OBJ_INSTALL_SYMBOL_DOC_ASSET))
               ? CTOOL_TRUE
               : CTOOL_FALSE;
}

static ctool_status_t
obj_install_validate_docs_symbols(ctool_job_t *job, const ctool_source_t *source,
                                  const ctool_obj_install_source_request_t *install) {
    ctool_u32 count = install->ctxt_count + install->doc_asset_count + install->home_asset_count;
    ctool_u32 index;
    for (index = 0u; index < count; index++) {
        obj_install_symbol_t current;
        ctool_u32 prior;
        obj_install_docs_symbol(install, index, &current);
        for (prior = 0u; prior < index; prior++) {
            obj_install_symbol_t earlier;
            obj_install_docs_symbol(install, prior, &earlier);
            if (obj_install_symbols_equal(&earlier, &current) == CTOOL_TRUE &&
                obj_install_docs_alias_allowed(&earlier, &current) == CTOOL_FALSE) {
                return obj_emit_failure(
                    job, source, CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_SYMBOL_COLLISION, index,
                    "CupidObj installation paths map to the same binary symbol");
            }
        }
    }
    return CTOOL_OK;
}

static ctool_status_t obj_install_validate_docs_list(ctool_job_t *job, const ctool_source_t *source,
                                                     const ctool_string_t *paths, ctool_u32 count,
                                                     const char *prefix, const char *suffix,
                                                     ctool_u32 column_base, const char *message) {
    ctool_u32 index;
    if (count != 0u && paths == (const ctool_string_t *)0) {
        return obj_emit_failure(job, source, CTOOL_ERR_INVALID_ARGUMENT,
                                CTOOL_OBJ_DIAG_INVALID_REQUEST, column_base,
                                "CupidObj installation path list is missing");
    }
    for (index = 0u; index < count; index++) {
        ctool_string_t stem;
        ctool_u32 prior;
        if (obj_install_docs_stem(paths[index], prefix, suffix, &stem) == CTOOL_FALSE) {
            return obj_emit_failure(job, source, CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_INVALID_INPUT,
                                    column_base + index, message);
        }
        for (prior = 0u; prior < index; prior++) {
            if (obj_string_equal(paths[prior], paths[index]) == CTOOL_TRUE) {
                return obj_emit_failure(job, source, CTOOL_ERR_INPUT,
                                        CTOOL_OBJ_DIAG_SYMBOL_COLLISION, column_base + index,
                                        "CupidObj installation path is duplicated");
            }
        }
    }
    return CTOOL_OK;
}

static ctool_status_t obj_install_docs(ctool_job_t *job, const ctool_obj_request_t *request,
                                       ctool_buffer_t *output, ctool_obj_result_t *result_out) {
    const ctool_obj_install_source_request_t *install = &request->as.install_source;
    static const char *home_extensions[] = {"bmp", "png", "jpg", "jpeg"};
    static const char *home_symbol_suffixes[] = {"_bmp", "_png", "_jpg", "_jpeg"};
    ctool_u32 index;
    ctool_status_t status;
    if (install->ctxt_count + install->doc_asset_count + install->home_asset_count == 0u) {
        return obj_emit_failure(job, request->input, CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_INVALID_INPUT,
                                0u, "CupidObj docs installation lists are empty");
    }
    status = obj_install_validate_docs_list(
        job, request->input, install->ctxt_paths, install->ctxt_count, "cupidos-txt/", ".CTXT", 0u,
        "CupidObj manual path must match cupidos-txt/NAME.CTXT");
    if (status == CTOOL_OK) {
        status = obj_install_validate_docs_list(
            job, request->input, install->doc_asset_paths, install->doc_asset_count, "", ".bmp",
            install->ctxt_count, "CupidObj documentation asset must match NAME.bmp");
    }
    if (status == CTOOL_OK && install->home_asset_count != 0u &&
        install->home_asset_paths == (const ctool_string_t *)0) {
        status = obj_emit_failure(
            job, request->input, CTOOL_ERR_INVALID_ARGUMENT, CTOOL_OBJ_DIAG_INVALID_REQUEST,
            install->ctxt_count + install->doc_asset_count, "CupidObj home asset list is missing");
    }
    for (index = 0u; status == CTOOL_OK && index < install->home_asset_count; index++) {
        ctool_string_t stem;
        ctool_u32 prior;
        if (obj_install_home_extension(install->home_asset_paths[index], &stem) == 0u) {
            status = obj_emit_failure(
                job, request->input, CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_INVALID_INPUT,
                install->ctxt_count + install->doc_asset_count + index,
                "CupidObj home asset must be NAME.bmp, NAME.png, NAME.jpg, or NAME.jpeg");
            break;
        }
        for (prior = 0u; prior < index; prior++) {
            if (obj_string_equal(install->home_asset_paths[prior],
                                 install->home_asset_paths[index]) == CTOOL_TRUE) {
                status = obj_emit_failure(job, request->input, CTOOL_ERR_INPUT,
                                          CTOOL_OBJ_DIAG_SYMBOL_COLLISION,
                                          install->ctxt_count + install->doc_asset_count + index,
                                          "CupidObj installation path is duplicated");
                break;
            }
        }
    }
    if (status == CTOOL_OK) {
        status = obj_install_validate_docs_symbols(job, request->input, install);
    }
    if (status != CTOOL_OK) {
        return status;
    }
    status = obj_append_literal(
        output, "/* Auto-generated -- do not edit. */\n"
                "/* Lists all embedded CupidDoc files from cupidos-txt/ directory */\n"
                "#include \"homefs.h\"\n"
                "#include \"ramfs.h\"\n"
                "#include \"types.h\"\n"
                "#include \"vfs.h\"\n"
                "#include \"../drivers/serial.h\"\n");
    for (index = 0u; status == CTOOL_OK && index < install->ctxt_count; index++) {
        ctool_string_t stem;
        (void)obj_install_docs_stem(install->ctxt_paths[index], "cupidos-txt/", ".CTXT", &stem);
        status = obj_install_emit_docs_extern(output, "_binary_cupidos_txt_", stem, "_CTXT",
                                              "_start[];\n");
    }
    for (index = 0u; status == CTOOL_OK && index < install->doc_asset_count; index++) {
        ctool_string_t stem;
        (void)obj_install_docs_stem(install->doc_asset_paths[index], "", ".bmp", &stem);
        status = obj_install_emit_docs_extern(output, "_binary_", stem, "_bmp", "_start[];\n");
    }
    for (index = 0u; status == CTOOL_OK && index < install->home_asset_count; index++) {
        ctool_string_t stem;
        ctool_u32 extension = obj_install_home_extension(install->home_asset_paths[index], &stem);
        status = obj_install_emit_docs_extern(output, "_binary_", stem,
                                              home_symbol_suffixes[extension - 1u], "_start[];\n");
    }
    for (index = 0u; status == CTOOL_OK && index < install->ctxt_count; index++) {
        ctool_string_t stem;
        (void)obj_install_docs_stem(install->ctxt_paths[index], "cupidos-txt/", ".CTXT", &stem);
        status = obj_install_emit_docs_extern(output, "_binary_cupidos_txt_", stem, "_CTXT",
                                              "_end[];\n");
    }
    for (index = 0u; status == CTOOL_OK && index < install->doc_asset_count; index++) {
        ctool_string_t stem;
        (void)obj_install_docs_stem(install->doc_asset_paths[index], "", ".bmp", &stem);
        status = obj_install_emit_docs_extern(output, "_binary_", stem, "_bmp", "_end[];\n");
    }
    for (index = 0u; status == CTOOL_OK && index < install->home_asset_count; index++) {
        ctool_string_t stem;
        ctool_u32 extension = obj_install_home_extension(install->home_asset_paths[index], &stem);
        status = obj_install_emit_docs_extern(output, "_binary_", stem,
                                              home_symbol_suffixes[extension - 1u], "_end[];\n");
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(
            output,
            "static void install_home_asset(const char *path, const char *data, uint32_t size) {\n"
            "    int fd = vfs_open(path, O_WRONLY | O_CREAT | O_TRUNC);\n"
            "    if (fd < 0) { serial_printf(\"[kernel] Failed to open %s (%d)\\n\", path, fd); "
            "return; }\n"
            "    uint32_t off = 0;\n"
            "    while (off < size) {\n"
            "        int n = vfs_write(fd, data + off, size - off);\n"
            "        if (n <= 0) break;\n"
            "        off += (uint32_t)n;\n"
            "    }\n"
            "    vfs_close(fd);\n"
            "    serial_printf(\"[kernel] Installed %s (%u bytes)\\n\", path, off);\n"
            "}\n"
            "void install_docs_programs(void *fs_private);\n"
            "void install_docs_programs(void *fs_private) {\n");
    }
    for (index = 0u; status == CTOOL_OK && index < install->ctxt_count; index++) {
        ctool_string_t stem;
        (void)obj_install_docs_stem(install->ctxt_paths[index], "cupidos-txt/", ".CTXT", &stem);
        status = obj_install_emit_docs_entry(output, "_binary_cupidos_txt_", stem, "_CTXT", "docs/",
                                             ".ctxt", "/docs/");
    }
    for (index = 0u; status == CTOOL_OK && index < install->doc_asset_count; index++) {
        ctool_string_t stem;
        (void)obj_install_docs_stem(install->doc_asset_paths[index], "", ".bmp", &stem);
        status = obj_install_emit_docs_entry(output, "_binary_", stem, "_bmp", "docs/", ".bmp",
                                             "/docs/");
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, "    homefs_seed_begin();\n");
    }
    for (index = 0u; status == CTOOL_OK && index < install->home_asset_count; index++) {
        ctool_string_t stem;
        ctool_u32 extension = obj_install_home_extension(install->home_asset_paths[index], &stem);
        status = obj_install_emit_home_entry(output, stem, home_extensions[extension - 1u]);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, "    homefs_seed_end();\n}\n");
    }
    if (status != CTOOL_OK) {
        return obj_install_output_failure(job, request->input, status);
    }
    result_out->bytes = ctool_buffer_view(output);
    return CTOOL_OK;
}

static ctool_status_t obj_install_source(ctool_job_t *job, const ctool_obj_request_t *request,
                                         ctool_buffer_t *output, ctool_obj_result_t *result_out) {
    const ctool_obj_install_source_request_t *install = &request->as.install_source;
    ctool_status_t status = obj_install_validate_path_limit(job, request);
    if (status != CTOOL_OK) {
        return status;
    }
    if (request->as.install_source.kind == CTOOL_OBJ_INSTALL_DEMOS) {
        if (install->bin_paths != (const ctool_string_t *)0 || install->bin_count != 0u ||
            install->header_paths != (const ctool_string_t *)0 || install->header_count != 0u ||
            install->browser_paths != (const ctool_string_t *)0 || install->browser_count != 0u ||
            install->ctxt_paths != (const ctool_string_t *)0 || install->ctxt_count != 0u ||
            install->doc_asset_paths != (const ctool_string_t *)0 ||
            install->doc_asset_count != 0u ||
            install->home_asset_paths != (const ctool_string_t *)0 ||
            install->home_asset_count != 0u) {
            return obj_emit_failure(job, request->input, CTOOL_ERR_INVALID_ARGUMENT,
                                    CTOOL_OBJ_DIAG_INVALID_REQUEST, 0u,
                                    "CupidObj demo request contains another installation category");
        }
        return obj_install_demos(job, request, output, result_out);
    }
    if (request->as.install_source.kind == CTOOL_OBJ_INSTALL_BIN) {
        if (install->ctxt_paths != (const ctool_string_t *)0 || install->ctxt_count != 0u ||
            install->doc_asset_paths != (const ctool_string_t *)0 ||
            install->doc_asset_count != 0u ||
            install->home_asset_paths != (const ctool_string_t *)0 ||
            install->home_asset_count != 0u || install->demo_paths != (const ctool_string_t *)0 ||
            install->demo_count != 0u) {
            return obj_emit_failure(job, request->input, CTOOL_ERR_INVALID_ARGUMENT,
                                    CTOOL_OBJ_DIAG_INVALID_REQUEST, 0u,
                                    "CupidObj bin request contains another installation category");
        }
        return obj_install_bin(job, request, output, result_out);
    }
    if (request->as.install_source.kind == CTOOL_OBJ_INSTALL_DOCS) {
        if (install->bin_paths != (const ctool_string_t *)0 || install->bin_count != 0u ||
            install->header_paths != (const ctool_string_t *)0 || install->header_count != 0u ||
            install->browser_paths != (const ctool_string_t *)0 || install->browser_count != 0u ||
            install->demo_paths != (const ctool_string_t *)0 || install->demo_count != 0u) {
            return obj_emit_failure(job, request->input, CTOOL_ERR_INVALID_ARGUMENT,
                                    CTOOL_OBJ_DIAG_INVALID_REQUEST, 0u,
                                    "CupidObj docs request contains another installation category");
        }
        return obj_install_docs(job, request, output, result_out);
    }
    return obj_emit_failure(job, request->input, CTOOL_ERR_INVALID_ARGUMENT,
                            CTOOL_OBJ_DIAG_INVALID_REQUEST, 0u,
                            "CupidObj installation source kind is invalid");
}
