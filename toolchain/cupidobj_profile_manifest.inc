/**
 * @file cupidobj_profile_manifest.inc
 * @brief Validate and render checked compiler profiles.
 */

static ctool_status_t obj_profile_failure(ctool_job_t *job, const ctool_source_t *source,
                                          ctool_arena_t *arena, ctool_arena_mark_t mark,
                                          ctool_status_t status, ctool_u32 code, ctool_u32 column,
                                          const char *message) {
    ctool_status_t emitted = obj_emit_failure(job, source, status, code, column, message);
    ctool_status_t rewound = ctool_arena_rewind(arena, mark);
    return rewound == CTOOL_OK ? emitted : rewound;
}

static ctool_bool obj_profile_read_u32(obj_profile_reader_t *reader, ctool_u32 *value_out) {
    const ctool_u8 *bytes;
    ctool_u32 offset;
    if (reader == (obj_profile_reader_t *)0 || value_out == (ctool_u32 *)0 ||
        reader->offset > reader->bytes.size || reader->bytes.size - reader->offset < 4u) {
        return CTOOL_FALSE;
    }
    bytes = reader->bytes.data + reader->offset;
    offset = reader->offset;
    *value_out = (ctool_u32)bytes[0] | ((ctool_u32)bytes[1] << 8u) | ((ctool_u32)bytes[2] << 16u) |
                 ((ctool_u32)bytes[3] << 24u);
    reader->offset = offset + 4u;
    return CTOOL_TRUE;
}

static ctool_bool obj_profile_read_bytes(obj_profile_reader_t *reader, ctool_bytes_t *bytes_out) {
    ctool_u32 size;
    if (bytes_out == (ctool_bytes_t *)0 || obj_profile_read_u32(reader, &size) == CTOOL_FALSE ||
        reader->offset > reader->bytes.size || size > reader->bytes.size - reader->offset) {
        return CTOOL_FALSE;
    }
    bytes_out->data = reader->bytes.data + reader->offset;
    bytes_out->size = size;
    reader->offset += size;
    return CTOOL_TRUE;
}

static ctool_bool obj_profile_read_string(obj_profile_reader_t *reader,
                                          ctool_string_t *string_out) {
    ctool_bytes_t bytes;
    if (string_out == (ctool_string_t *)0 ||
        obj_profile_read_bytes(reader, &bytes) == CTOOL_FALSE) {
        return CTOOL_FALSE;
    }
    string_out->data = (const char *)(const void *)bytes.data;
    string_out->size = bytes.size;
    return CTOOL_TRUE;
}

static ctool_bool obj_profile_name_valid(ctool_string_t name, ctool_u32 limit) {
    ctool_u32 index;
    if (obj_string_valid(name) == CTOOL_FALSE || name.size > limit) {
        return CTOOL_FALSE;
    }
    for (index = 0u; index < name.size; index++) {
        ctool_u8 character = (ctool_u8)name.data[index];
        if (!((character >= (ctool_u8)'a' && character <= (ctool_u8)'z') ||
              (character >= (ctool_u8)'A' && character <= (ctool_u8)'Z') ||
              (character >= (ctool_u8)'0' && character <= (ctool_u8)'9') ||
              character == (ctool_u8)'.' || character == (ctool_u8)'_' ||
              character == (ctool_u8)'-')) {
            return CTOOL_FALSE;
        }
    }
    return CTOOL_TRUE;
}

static ctool_bool obj_profile_path_valid(ctool_string_t path) {
    ctool_u32 component = 0u;
    ctool_u32 index;
    if (obj_string_valid(path) == CTOOL_FALSE || path.size > CTOOL_OBJ_PROFILE_PATH_BYTES ||
        path.data[0] == '/' || path.data[path.size - 1u] == '/') {
        return CTOOL_FALSE;
    }
    for (index = 0u; index <= path.size; index++) {
        if (index == path.size || path.data[index] == '/') {
            ctool_u32 size = index - component;
            if (size == 0u || (size == 1u && path.data[component] == '.') ||
                (size == 2u && path.data[component] == '.' && path.data[component + 1u] == '.')) {
                return CTOOL_FALSE;
            }
            component = index + 1u;
        } else if (obj_iso_component_character((ctool_u8)path.data[index]) == CTOOL_FALSE) {
            return CTOOL_FALSE;
        }
    }
    return CTOOL_TRUE;
}

static ctool_i32 obj_profile_string_order(ctool_string_t left, ctool_string_t right) {
    ctool_u32 common = left.size < right.size ? left.size : right.size;
    ctool_u32 index;
    for (index = 0u; index < common; index++) {
        ctool_u8 left_byte = (ctool_u8)left.data[index];
        ctool_u8 right_byte = (ctool_u8)right.data[index];
        if (left_byte != right_byte) {
            return left_byte < right_byte ? -1 : 1;
        }
    }
    if (left.size == right.size) {
        return 0;
    }
    return left.size < right.size ? -1 : 1;
}

static ctool_u8 obj_profile_fold_ascii(ctool_u8 character) {
    return character >= (ctool_u8)'A' && character <= (ctool_u8)'Z'
               ? character + ((ctool_u8)'a' - (ctool_u8)'A')
               : character;
}

static ctool_i32 obj_profile_string_order_folded(ctool_string_t left, ctool_string_t right) {
    ctool_u32 common = left.size < right.size ? left.size : right.size;
    ctool_u32 index;
    for (index = 0u; index < common; index++) {
        ctool_u8 left_byte = obj_profile_fold_ascii((ctool_u8)left.data[index]);
        ctool_u8 right_byte = obj_profile_fold_ascii((ctool_u8)right.data[index]);
        if (left_byte != right_byte) {
            return left_byte < right_byte ? -1 : 1;
        }
    }
    if (left.size == right.size) {
        return 0;
    }
    return left.size < right.size ? -1 : 1;
}

static void obj_profile_sift_strings_folded(ctool_string_t *strings, ctool_u32 root,
                                            ctool_u32 count) {
    while (root < count / 2u) {
        ctool_u32 child = root * 2u + 1u;
        ctool_string_t value;
        if (child + 1u < count &&
            obj_profile_string_order_folded(strings[child], strings[child + 1u]) < 0) {
            child++;
        }
        if (obj_profile_string_order_folded(strings[root], strings[child]) >= 0) {
            return;
        }
        value = strings[root];
        strings[root] = strings[child];
        strings[child] = value;
        root = child;
    }
}

static void obj_profile_sort_strings_folded(ctool_string_t *strings, ctool_u32 count) {
    ctool_u32 index;
    if (count < 2u) {
        return;
    }
    for (index = count / 2u; index != 0u; index--) {
        obj_profile_sift_strings_folded(strings, index - 1u, count);
    }
    for (index = count; index > 1u; index--) {
        ctool_string_t value = strings[0];
        strings[0] = strings[index - 1u];
        strings[index - 1u] = value;
        obj_profile_sift_strings_folded(strings, 0u, index - 1u);
    }
}

static void obj_profile_sift_strings(ctool_string_t *strings, ctool_u32 root, ctool_u32 count) {
    while (root < count / 2u) {
        ctool_u32 child = root * 2u + 1u;
        ctool_string_t value;
        if (child + 1u < count &&
            obj_profile_string_order(strings[child], strings[child + 1u]) < 0) {
            child++;
        }
        if (obj_profile_string_order(strings[root], strings[child]) >= 0) {
            return;
        }
        value = strings[root];
        strings[root] = strings[child];
        strings[child] = value;
        root = child;
    }
}

static void obj_profile_sort_strings(ctool_string_t *strings, ctool_u32 count) {
    ctool_u32 index;
    if (count < 2u) {
        return;
    }
    for (index = count / 2u; index != 0u; index--) {
        obj_profile_sift_strings(strings, index - 1u, count);
    }
    for (index = count; index > 1u; index--) {
        ctool_string_t value = strings[0];
        strings[0] = strings[index - 1u];
        strings[index - 1u] = value;
        obj_profile_sift_strings(strings, 0u, index - 1u);
    }
}

static void obj_profile_sift_profiles(obj_profile_t *profiles, ctool_u32 root, ctool_u32 count) {
    while (root < count / 2u) {
        ctool_u32 child = root * 2u + 1u;
        obj_profile_t value;
        if (child + 1u < count &&
            obj_profile_string_order(profiles[child].name, profiles[child + 1u].name) < 0) {
            child++;
        }
        if (obj_profile_string_order(profiles[root].name, profiles[child].name) >= 0) {
            return;
        }
        value = profiles[root];
        profiles[root] = profiles[child];
        profiles[child] = value;
        root = child;
    }
}

static void obj_profile_sort_profiles(obj_profile_t *profiles, ctool_u32 count) {
    ctool_u32 index;
    if (count < 2u) {
        return;
    }
    for (index = count / 2u; index != 0u; index--) {
        obj_profile_sift_profiles(profiles, index - 1u, count);
    }
    for (index = count; index > 1u; index--) {
        obj_profile_t value = profiles[0];
        profiles[0] = profiles[index - 1u];
        profiles[index - 1u] = value;
        obj_profile_sift_profiles(profiles, 0u, index - 1u);
    }
}

static void obj_profile_sift_inputs(obj_profile_input_t *inputs, ctool_u32 root, ctool_u32 count) {
    while (root < count / 2u) {
        ctool_u32 child = root * 2u + 1u;
        obj_profile_input_t value;
        if (child + 1u < count &&
            obj_profile_string_order(inputs[child].path, inputs[child + 1u].path) < 0) {
            child++;
        }
        if (obj_profile_string_order(inputs[root].path, inputs[child].path) >= 0) {
            return;
        }
        value = inputs[root];
        inputs[root] = inputs[child];
        inputs[child] = value;
        root = child;
    }
}

static void obj_profile_sort_inputs(obj_profile_input_t *inputs, ctool_u32 count) {
    ctool_u32 index;
    if (count < 2u) {
        return;
    }
    for (index = count / 2u; index != 0u; index--) {
        obj_profile_sift_inputs(inputs, index - 1u, count);
    }
    for (index = count; index > 1u; index--) {
        obj_profile_input_t value = inputs[0];
        inputs[0] = inputs[index - 1u];
        inputs[index - 1u] = value;
        obj_profile_sift_inputs(inputs, 0u, index - 1u);
    }
}

static void obj_profile_sift_inputs_folded(obj_profile_input_t *inputs, ctool_u32 root,
                                           ctool_u32 count) {
    while (root < count / 2u) {
        ctool_u32 child = root * 2u + 1u;
        obj_profile_input_t value;
        if (child + 1u < count &&
            obj_profile_string_order_folded(inputs[child].path, inputs[child + 1u].path) < 0) {
            child++;
        }
        if (obj_profile_string_order_folded(inputs[root].path, inputs[child].path) >= 0) {
            return;
        }
        value = inputs[root];
        inputs[root] = inputs[child];
        inputs[child] = value;
        root = child;
    }
}

static void obj_profile_sort_inputs_folded(obj_profile_input_t *inputs, ctool_u32 count) {
    ctool_u32 index;
    if (count < 2u) {
        return;
    }
    for (index = count / 2u; index != 0u; index--) {
        obj_profile_sift_inputs_folded(inputs, index - 1u, count);
    }
    for (index = count; index > 1u; index--) {
        obj_profile_input_t value = inputs[0];
        inputs[0] = inputs[index - 1u];
        inputs[index - 1u] = value;
        obj_profile_sift_inputs_folded(inputs, 0u, index - 1u);
    }
}

static ctool_bool obj_profile_reference_count(ctool_u32 count, ctool_u32 *total) {
    if (*total > CTOOL_OBJ_PROFILE_REFERENCE_LIMIT - count) {
        return CTOOL_FALSE;
    }
    *total += count;
    return CTOOL_TRUE;
}

static ctool_status_t obj_profile_allocate_strings(ctool_arena_t *arena, ctool_u32 count,
                                                   ctool_string_t **strings_out) {
    if (count == 0u) {
        *strings_out = (ctool_string_t *)0;
        return CTOOL_OK;
    }
    return ctool_arena_alloc_zero(arena, count, (ctool_u32)sizeof(**strings_out),
                                  (ctool_u32)sizeof(void *), (void **)strings_out);
}

static ctool_status_t obj_profile_parse(ctool_job_t *job, const ctool_source_t *source,
                                        ctool_arena_t *arena, ctool_arena_mark_t mark,
                                        obj_profile_snapshot_t *snapshot) {
    static const ctool_u8 magic[CTOOL_OBJ_PROFILE_MAGIC_BYTES] = {
        (ctool_u8)'C', (ctool_u8)'U', (ctool_u8)'P', (ctool_u8)'R',
        (ctool_u8)'O', (ctool_u8)'F', (ctool_u8)'1', 0u};
    obj_profile_reader_t reader;
    ctool_u32 reference_count = 0u;
    ctool_u32 index;
    ctool_status_t status;
    reader.bytes = source->contents;
    reader.offset = 0u;
    if (reader.bytes.data == (const ctool_u8 *)0 && reader.bytes.size != 0u) {
        return obj_profile_failure(job, source, arena, mark, CTOOL_ERR_INVALID_ARGUMENT,
                                   CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
                                   "CupidObj profile snapshot bytes are invalid");
    }
    if (reader.bytes.size < CTOOL_OBJ_PROFILE_MAGIC_BYTES) {
        return obj_profile_failure(job, source, arena, mark, CTOOL_ERR_INPUT,
                                   CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
                                   "CupidObj profile snapshot is truncated");
    }
    for (index = 0u; index < CTOOL_OBJ_PROFILE_MAGIC_BYTES; index++) {
        if (reader.bytes.data[index] != magic[index]) {
            return obj_profile_failure(job, source, arena, mark, CTOOL_ERR_INPUT,
                                       CTOOL_OBJ_DIAG_INVALID_INPUT, index,
                                       "CupidObj profile snapshot magic is invalid");
        }
    }
    reader.offset = CTOOL_OBJ_PROFILE_MAGIC_BYTES;
    if (obj_profile_read_string(&reader, &snapshot->schema) == CTOOL_FALSE ||
        obj_profile_read_u32(&reader, &snapshot->profile_count) == CTOOL_FALSE) {
        return obj_profile_failure(job, source, arena, mark, CTOOL_ERR_INPUT,
                                   CTOOL_OBJ_DIAG_INVALID_INPUT, reader.offset,
                                   "CupidObj profile snapshot is truncated");
    }
    if (obj_profile_name_valid(snapshot->schema, CTOOL_OBJ_PROFILE_SCHEMA_BYTES) == CTOOL_FALSE) {
        return obj_profile_failure(job, source, arena, mark, CTOOL_ERR_INPUT,
                                   CTOOL_OBJ_DIAG_INVALID_INPUT, reader.offset,
                                   "CupidObj profile snapshot schema is invalid");
    }
    if (snapshot->profile_count == 0u || snapshot->profile_count > CTOOL_OBJ_PROFILE_LIMIT) {
        return obj_profile_failure(
            job, source, arena, mark,
            snapshot->profile_count > CTOOL_OBJ_PROFILE_LIMIT ? CTOOL_ERR_LIMIT : CTOOL_ERR_INPUT,
            snapshot->profile_count > CTOOL_OBJ_PROFILE_LIMIT ? CTOOL_OBJ_DIAG_LIMIT
                                                              : CTOOL_OBJ_DIAG_INVALID_INPUT,
            reader.offset, "CupidObj profile snapshot profile count is invalid");
    }
    status = ctool_arena_alloc_zero(arena, snapshot->profile_count,
                                    (ctool_u32)sizeof(*snapshot->profiles),
                                    (ctool_u32)sizeof(void *), (void **)&snapshot->profiles);
    if (status != CTOOL_OK) {
        return obj_profile_failure(job, source, arena, mark, status, CTOOL_OBJ_DIAG_LIMIT,
                                   reader.offset,
                                   "CupidObj profile snapshot exceeds its arena limit");
    }
    for (index = 0u; index < snapshot->profile_count; index++) {
        obj_profile_t *profile = &snapshot->profiles[index];
        ctool_u32 member;
        ctool_u32 prior;
        if (obj_profile_read_string(&reader, &profile->name) == CTOOL_FALSE ||
            obj_profile_read_u32(&reader, &profile->header_count) == CTOOL_FALSE) {
            return obj_profile_failure(job, source, arena, mark, CTOOL_ERR_INPUT,
                                       CTOOL_OBJ_DIAG_INVALID_INPUT, reader.offset,
                                       "CupidObj profile snapshot is truncated");
        }
        if (obj_profile_name_valid(profile->name, CTOOL_OBJ_PROFILE_NAME_BYTES) == CTOOL_FALSE) {
            return obj_profile_failure(job, source, arena, mark, CTOOL_ERR_INPUT,
                                       CTOOL_OBJ_DIAG_INVALID_INPUT, reader.offset,
                                       "CupidObj profile snapshot profile name is invalid");
        }
        for (prior = 0u; prior < index; prior++) {
            if (obj_iso_string_equal_folded(profile->name, snapshot->profiles[prior].name) ==
                CTOOL_TRUE) {
                ctool_bool exact = obj_string_equal(profile->name, snapshot->profiles[prior].name);
                return obj_profile_failure(
                    job, source, arena, mark, CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_SYMBOL_COLLISION,
                    reader.offset,
                    exact == CTOOL_TRUE
                        ? "CupidObj profile snapshot profile name is duplicated"
                        : "CupidObj profile snapshot profile name has a case collision");
            }
        }
        if (profile->header_count == 0u || profile->header_count > CTOOL_OBJ_PROFILE_INPUT_LIMIT) {
            return obj_profile_failure(
                job, source, arena, mark,
                profile->header_count == 0u ? CTOOL_ERR_INPUT : CTOOL_ERR_LIMIT,
                profile->header_count == 0u ? CTOOL_OBJ_DIAG_INVALID_INPUT : CTOOL_OBJ_DIAG_LIMIT,
                reader.offset, "CupidObj profile snapshot header count is invalid");
        }
        if (obj_profile_reference_count(profile->header_count, &reference_count) == CTOOL_FALSE) {
            return obj_profile_failure(
                job, source, arena, mark, CTOOL_ERR_LIMIT, CTOOL_OBJ_DIAG_LIMIT, reader.offset,
                "CupidObj profile snapshot membership reference limit is exceeded");
        }
        status = obj_profile_allocate_strings(arena, profile->header_count, &profile->headers);
        if (status != CTOOL_OK) {
            return obj_profile_failure(job, source, arena, mark, status, CTOOL_OBJ_DIAG_LIMIT,
                                       reader.offset,
                                       "CupidObj profile snapshot exceeds its arena limit");
        }
        for (member = 0u; member < profile->header_count; member++) {
            if (obj_profile_read_string(&reader, &profile->headers[member]) == CTOOL_FALSE) {
                return obj_profile_failure(job, source, arena, mark, CTOOL_ERR_INPUT,
                                           CTOOL_OBJ_DIAG_INVALID_INPUT, reader.offset,
                                           "CupidObj profile snapshot is truncated");
            }
            if (obj_profile_path_valid(profile->headers[member]) == CTOOL_FALSE) {
                return obj_profile_failure(job, source, arena, mark, CTOOL_ERR_INPUT,
                                           CTOOL_OBJ_DIAG_INVALID_INPUT, reader.offset,
                                           "CupidObj profile snapshot repository path is invalid");
            }
        }
        obj_profile_sort_strings_folded(profile->headers, profile->header_count);
        for (member = 1u; member < profile->header_count; member++) {
            if (obj_profile_string_order_folded(profile->headers[member - 1u],
                                                profile->headers[member]) == 0) {
                ctool_bool exact =
                    obj_string_equal(profile->headers[member - 1u], profile->headers[member]);
                return obj_profile_failure(
                    job, source, arena, mark, CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_SYMBOL_COLLISION,
                    reader.offset,
                    exact == CTOOL_TRUE
                        ? "CupidObj profile snapshot header path is duplicated"
                        : "CupidObj profile snapshot header path has a case collision");
            }
        }
        if (obj_profile_read_u32(&reader, &profile->source_count) == CTOOL_FALSE) {
            return obj_profile_failure(job, source, arena, mark, CTOOL_ERR_INPUT,
                                       CTOOL_OBJ_DIAG_INVALID_INPUT, reader.offset,
                                       "CupidObj profile snapshot is truncated");
        }
        if (profile->source_count == 0u || profile->source_count > CTOOL_OBJ_PROFILE_INPUT_LIMIT) {
            return obj_profile_failure(
                job, source, arena, mark,
                profile->source_count == 0u ? CTOOL_ERR_INPUT : CTOOL_ERR_LIMIT,
                profile->source_count == 0u ? CTOOL_OBJ_DIAG_INVALID_INPUT : CTOOL_OBJ_DIAG_LIMIT,
                reader.offset, "CupidObj profile snapshot source count is invalid");
        }
        if (obj_profile_reference_count(profile->source_count, &reference_count) == CTOOL_FALSE) {
            return obj_profile_failure(
                job, source, arena, mark, CTOOL_ERR_LIMIT, CTOOL_OBJ_DIAG_LIMIT, reader.offset,
                "CupidObj profile snapshot membership reference limit is exceeded");
        }
        status = obj_profile_allocate_strings(arena, profile->source_count, &profile->sources);
        if (status != CTOOL_OK) {
            return obj_profile_failure(job, source, arena, mark, status, CTOOL_OBJ_DIAG_LIMIT,
                                       reader.offset,
                                       "CupidObj profile snapshot exceeds its arena limit");
        }
        for (member = 0u; member < profile->source_count; member++) {
            if (obj_profile_read_string(&reader, &profile->sources[member]) == CTOOL_FALSE) {
                return obj_profile_failure(job, source, arena, mark, CTOOL_ERR_INPUT,
                                           CTOOL_OBJ_DIAG_INVALID_INPUT, reader.offset,
                                           "CupidObj profile snapshot is truncated");
            }
            if (obj_profile_path_valid(profile->sources[member]) == CTOOL_FALSE) {
                return obj_profile_failure(job, source, arena, mark, CTOOL_ERR_INPUT,
                                           CTOOL_OBJ_DIAG_INVALID_INPUT, reader.offset,
                                           "CupidObj profile snapshot repository path is invalid");
            }
        }
        obj_profile_sort_strings_folded(profile->sources, profile->source_count);
        for (member = 1u; member < profile->source_count; member++) {
            if (obj_profile_string_order_folded(profile->sources[member - 1u],
                                                profile->sources[member]) == 0) {
                ctool_bool exact =
                    obj_string_equal(profile->sources[member - 1u], profile->sources[member]);
                return obj_profile_failure(
                    job, source, arena, mark, CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_SYMBOL_COLLISION,
                    reader.offset,
                    exact == CTOOL_TRUE
                        ? "CupidObj profile snapshot source path is duplicated"
                        : "CupidObj profile snapshot source path has a case collision");
            }
        }
    }
    if (obj_profile_read_u32(&reader, &snapshot->input_count) == CTOOL_FALSE) {
        return obj_profile_failure(job, source, arena, mark, CTOOL_ERR_INPUT,
                                   CTOOL_OBJ_DIAG_INVALID_INPUT, reader.offset,
                                   "CupidObj profile snapshot is truncated");
    }
    if (snapshot->input_count == 0u || snapshot->input_count > CTOOL_OBJ_PROFILE_INPUT_LIMIT) {
        return obj_profile_failure(
            job, source, arena, mark,
            snapshot->input_count > CTOOL_OBJ_PROFILE_INPUT_LIMIT ? CTOOL_ERR_LIMIT
                                                                  : CTOOL_ERR_INPUT,
            snapshot->input_count > CTOOL_OBJ_PROFILE_INPUT_LIMIT ? CTOOL_OBJ_DIAG_LIMIT
                                                                  : CTOOL_OBJ_DIAG_INVALID_INPUT,
            reader.offset, "CupidObj profile snapshot input count is invalid");
    }
    status =
        ctool_arena_alloc_zero(arena, snapshot->input_count, (ctool_u32)sizeof(*snapshot->inputs),
                               (ctool_u32)sizeof(void *), (void **)&snapshot->inputs);
    if (status != CTOOL_OK) {
        return obj_profile_failure(job, source, arena, mark, status, CTOOL_OBJ_DIAG_LIMIT,
                                   reader.offset,
                                   "CupidObj profile snapshot exceeds its arena limit");
    }
    for (index = 0u; index < snapshot->input_count; index++) {
        if (obj_profile_read_string(&reader, &snapshot->inputs[index].path) == CTOOL_FALSE ||
            obj_profile_read_bytes(&reader, &snapshot->inputs[index].contents) == CTOOL_FALSE) {
            return obj_profile_failure(job, source, arena, mark, CTOOL_ERR_INPUT,
                                       CTOOL_OBJ_DIAG_INVALID_INPUT, reader.offset,
                                       "CupidObj profile snapshot is truncated");
        }
        if (obj_profile_path_valid(snapshot->inputs[index].path) == CTOOL_FALSE) {
            return obj_profile_failure(job, source, arena, mark, CTOOL_ERR_INPUT,
                                       CTOOL_OBJ_DIAG_INVALID_INPUT, reader.offset,
                                       "CupidObj profile snapshot repository path is invalid");
        }
    }
    obj_profile_sort_inputs_folded(snapshot->inputs, snapshot->input_count);
    for (index = 1u; index < snapshot->input_count; index++) {
        if (obj_iso_string_equal_folded(snapshot->inputs[index - 1u].path,
                                        snapshot->inputs[index].path) == CTOOL_TRUE) {
            ctool_bool exact =
                obj_string_equal(snapshot->inputs[index - 1u].path, snapshot->inputs[index].path);
            return obj_profile_failure(
                job, source, arena, mark, CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_SYMBOL_COLLISION,
                reader.offset,
                exact == CTOOL_TRUE
                    ? "CupidObj profile snapshot captured input path is duplicated"
                    : "CupidObj profile snapshot captured input path has a case collision");
        }
    }
    if (reader.offset != reader.bytes.size) {
        return obj_profile_failure(job, source, arena, mark, CTOOL_ERR_INPUT,
                                   CTOOL_OBJ_DIAG_INVALID_INPUT, reader.offset,
                                   "CupidObj profile snapshot has trailing data");
    }
    return CTOOL_OK;
}

static ctool_status_t obj_profile_validate_membership(ctool_job_t *job,
                                                      const ctool_source_t *source,
                                                      ctool_arena_t *arena, ctool_arena_mark_t mark,
                                                      obj_profile_snapshot_t *snapshot) {
    ctool_u8 *seen = (ctool_u8 *)0;
    ctool_status_t status =
        ctool_arena_alloc_zero(arena, snapshot->input_count, 1u, 1u, (void **)&seen);
    ctool_u32 profile_index;
    if (status != CTOOL_OK) {
        return obj_profile_failure(job, source, arena, mark, status, CTOOL_OBJ_DIAG_LIMIT, 0u,
                                   "CupidObj profile snapshot exceeds its arena limit");
    }
    for (profile_index = 0u; profile_index < snapshot->profile_count; profile_index++) {
        obj_profile_t *profile = &snapshot->profiles[profile_index];
        ctool_u32 header;
        for (header = 0u; header < profile->header_count; header++) {
            ctool_u32 low = 0u;
            ctool_u32 high = snapshot->input_count;
            ctool_u32 input = 0u;
            ctool_bool found = CTOOL_FALSE;
            while (low < high) {
                ctool_u32 middle = low + (high - low) / 2u;
                ctool_i32 order = obj_profile_string_order(profile->headers[header],
                                                           snapshot->inputs[middle].path);
                if (order == 0) {
                    input = middle;
                    seen[input] = 1u;
                    found = CTOOL_TRUE;
                    break;
                }
                if (order < 0) {
                    high = middle;
                } else {
                    low = middle + 1u;
                }
            }
            if (found == CTOOL_FALSE) {
                return obj_profile_failure(
                    job, source, arena, mark, CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_INVALID_INPUT, header,
                    "CupidObj profile snapshot header has no captured input");
            }
        }
    }
    for (profile_index = 0u; profile_index < snapshot->input_count; profile_index++) {
        if (seen[profile_index] == 0u) {
            return obj_profile_failure(
                job, source, arena, mark, CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_INVALID_INPUT,
                profile_index,
                "CupidObj profile snapshot captured input is absent from every profile");
        }
    }
    return CTOOL_OK;
}

static ctool_status_t
obj_profile_validate_portable_identity(ctool_job_t *job, const ctool_source_t *source,
                                       ctool_arena_t *arena, ctool_arena_mark_t mark,
                                       const obj_profile_snapshot_t *snapshot) {
    ctool_u32 count = snapshot->input_count;
    ctool_string_t *paths = (ctool_string_t *)0;
    ctool_u32 profile_index;
    ctool_u32 index = 0u;
    ctool_status_t status;
    for (profile_index = 0u; profile_index < snapshot->profile_count; profile_index++) {
        const obj_profile_t *profile = &snapshot->profiles[profile_index];
        if (count > 0xffffffffu - profile->header_count ||
            count + profile->header_count > 0xffffffffu - profile->source_count) {
            return obj_profile_failure(job, source, arena, mark, CTOOL_ERR_OVERFLOW,
                                       CTOOL_OBJ_DIAG_LIMIT, 0u,
                                       "CupidObj profile snapshot path inventory overflows i386");
        }
        count += profile->header_count + profile->source_count;
    }
    status = ctool_arena_alloc_zero(arena, count, (ctool_u32)sizeof(*paths),
                                    (ctool_u32)sizeof(void *), (void **)&paths);
    if (status != CTOOL_OK) {
        return obj_profile_failure(job, source, arena, mark, status, CTOOL_OBJ_DIAG_LIMIT, 0u,
                                   "CupidObj profile snapshot exceeds its arena limit");
    }
    for (profile_index = 0u; profile_index < snapshot->profile_count; profile_index++) {
        const obj_profile_t *profile = &snapshot->profiles[profile_index];
        ctool_u32 member;
        for (member = 0u; member < profile->header_count; member++) {
            paths[index++] = profile->headers[member];
        }
        for (member = 0u; member < profile->source_count; member++) {
            paths[index++] = profile->sources[member];
        }
    }
    for (profile_index = 0u; profile_index < snapshot->input_count; profile_index++) {
        paths[index++] = snapshot->inputs[profile_index].path;
    }
    obj_profile_sort_strings_folded(paths, count);
    for (index = 0u; index < count; index++) {
        if (index != 0u && obj_profile_string_order_folded(paths[index - 1u], paths[index]) == 0 &&
            obj_string_equal(paths[index - 1u], paths[index]) == CTOOL_FALSE) {
            return obj_profile_failure(
                job, source, arena, mark, CTOOL_ERR_INPUT, CTOOL_OBJ_DIAG_SYMBOL_COLLISION, index,
                "CupidObj profile snapshot path spelling has a case collision");
        }
    }
    return CTOOL_OK;
}

static ctool_u32 obj_profile_rotate_right(ctool_u32 value, ctool_u32 count) {
    return (value >> count) | (value << (32u - count));
}

static void obj_profile_sha256_block(ctool_u32 state[8], const ctool_u8 block[64]) {
    static const ctool_u32 constants[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
        0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
        0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
        0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
        0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
        0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
        0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
        0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
        0xc67178f2u};
    ctool_u32 words[64];
    ctool_u32 a;
    ctool_u32 b;
    ctool_u32 c;
    ctool_u32 d;
    ctool_u32 e;
    ctool_u32 f;
    ctool_u32 g;
    ctool_u32 h;
    ctool_u32 index;
    for (index = 0u; index < 16u; index++) {
        ctool_u32 offset = index * 4u;
        words[index] = ((ctool_u32)block[offset] << 24u) | ((ctool_u32)block[offset + 1u] << 16u) |
                       ((ctool_u32)block[offset + 2u] << 8u) | (ctool_u32)block[offset + 3u];
    }
    for (index = 16u; index < 64u; index++) {
        ctool_u32 left = words[index - 15u];
        ctool_u32 right = words[index - 2u];
        ctool_u32 small_zero =
            obj_profile_rotate_right(left, 7u) ^ obj_profile_rotate_right(left, 18u) ^ (left >> 3u);
        ctool_u32 small_one = obj_profile_rotate_right(right, 17u) ^
                              obj_profile_rotate_right(right, 19u) ^ (right >> 10u);
        words[index] = words[index - 16u] + small_zero + words[index - 7u] + small_one;
    }
    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];
    f = state[5];
    g = state[6];
    h = state[7];
    for (index = 0u; index < 64u; index++) {
        ctool_u32 large_one = obj_profile_rotate_right(e, 6u) ^ obj_profile_rotate_right(e, 11u) ^
                              obj_profile_rotate_right(e, 25u);
        ctool_u32 choose = (e & f) ^ ((~e) & g);
        ctool_u32 temporary_one = h + large_one + choose + constants[index] + words[index];
        ctool_u32 large_zero = obj_profile_rotate_right(a, 2u) ^ obj_profile_rotate_right(a, 13u) ^
                               obj_profile_rotate_right(a, 22u);
        ctool_u32 majority = (a & b) ^ (a & c) ^ (b & c);
        ctool_u32 temporary_two = large_zero + majority;
        h = g;
        g = f;
        f = e;
        e = d + temporary_one;
        d = c;
        c = b;
        b = a;
        a = temporary_one + temporary_two;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

static void obj_profile_sha256(ctool_bytes_t contents, ctool_u8 digest[32]) {
    ctool_u32 state[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                          0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    ctool_u8 tail[128];
    ctool_u32 offset = 0u;
    ctool_u32 remaining;
    ctool_u32 tail_size;
    ctool_u32 index;
    ctool_u64 bit_length = (ctool_u64)contents.size * 8u;
    while (contents.size - offset >= 64u) {
        obj_profile_sha256_block(state, contents.data + offset);
        offset += 64u;
    }
    remaining = contents.size - offset;
    obj_zero(tail, (ctool_u32)sizeof(tail));
    for (index = 0u; index < remaining; index++) {
        tail[index] = contents.data[offset + index];
    }
    tail[remaining] = 0x80u;
    tail_size = remaining < 56u ? 64u : 128u;
    for (index = 0u; index < 8u; index++) {
        tail[tail_size - 1u - index] = (ctool_u8)((bit_length >> (index * 8u)) & 0xffu);
    }
    obj_profile_sha256_block(state, tail);
    if (tail_size == 128u) {
        obj_profile_sha256_block(state, tail + 64u);
    }
    for (index = 0u; index < 8u; index++) {
        digest[index * 4u] = (ctool_u8)((state[index] >> 24u) & 0xffu);
        digest[index * 4u + 1u] = (ctool_u8)((state[index] >> 16u) & 0xffu);
        digest[index * 4u + 2u] = (ctool_u8)((state[index] >> 8u) & 0xffu);
        digest[index * 4u + 3u] = (ctool_u8)(state[index] & 0xffu);
    }
}

static ctool_status_t obj_profile_append_decimal(ctool_buffer_t *output, ctool_u32 value) {
    char decimal[10];
    ctool_u32 size = obj_disk_decimal(decimal, value);
    return ctool_buffer_append(output, ctool_bytes(decimal, size));
}

static ctool_status_t obj_profile_append_quoted(ctool_buffer_t *output, ctool_string_t string) {
    ctool_status_t status = obj_append_literal(output, "\"");
    if (status == CTOOL_OK) {
        status = obj_append_string(output, string);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, "\"");
    }
    return status;
}

static ctool_status_t obj_profile_append_digest(ctool_buffer_t *output, const ctool_u8 digest[32]) {
    static const char hexadecimal[] = "0123456789abcdef";
    char text[64];
    ctool_u32 index;
    for (index = 0u; index < 32u; index++) {
        text[index * 2u] = hexadecimal[digest[index] >> 4u];
        text[index * 2u + 1u] = hexadecimal[digest[index] & 0x0fu];
    }
    return ctool_buffer_append(output, ctool_bytes(text, 64u));
}

static ctool_status_t obj_profile_emit_membership(ctool_buffer_t *output,
                                                  const obj_profile_t *profiles,
                                                  ctool_u32 profile_count, ctool_bool sources) {
    ctool_u32 profile_index;
    ctool_status_t status = obj_append_literal(output, " {");
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, "\n");
    }
    for (profile_index = 0u; status == CTOOL_OK && profile_index < profile_count; profile_index++) {
        const obj_profile_t *profile = &profiles[profile_index];
        const ctool_string_t *paths = sources == CTOOL_TRUE ? profile->sources : profile->headers;
        ctool_u32 count = sources == CTOOL_TRUE ? profile->source_count : profile->header_count;
        ctool_u32 member;
        status = obj_append_literal(output, "    ");
        if (status == CTOOL_OK) {
            status = obj_profile_append_quoted(output, profile->name);
        }
        if (status == CTOOL_OK) {
            status = obj_append_literal(output, ": [\n");
        }
        for (member = 0u; status == CTOOL_OK && member < count; member++) {
            status = obj_append_literal(output, "      ");
            if (status == CTOOL_OK) {
                status = obj_profile_append_quoted(output, paths[member]);
            }
            if (status == CTOOL_OK) {
                status = obj_append_literal(output, member + 1u == count ? "\n" : ",\n");
            }
        }
        if (status == CTOOL_OK) {
            status = obj_append_literal(output, profile_index + 1u == profile_count ? "    ]\n"
                                                                                    : "    ],\n");
        }
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, "  }");
    }
    return status;
}

static ctool_status_t obj_profile_emit(ctool_buffer_t *output,
                                       const obj_profile_snapshot_t *snapshot) {
    ctool_u32 index;
    ctool_status_t status = obj_append_literal(output, "{\n  \"inputs\": [\n");
    for (index = 0u; status == CTOOL_OK && index < snapshot->input_count; index++) {
        const obj_profile_input_t *input = &snapshot->inputs[index];
        status = obj_append_literal(output, "    {\n      \"bytes\": ");
        if (status == CTOOL_OK) {
            status = obj_profile_append_decimal(output, input->contents.size);
        }
        if (status == CTOOL_OK) {
            status = obj_append_literal(output, ",\n      \"path\": ");
        }
        if (status == CTOOL_OK) {
            status = obj_profile_append_quoted(output, input->path);
        }
        if (status == CTOOL_OK) {
            status = obj_append_literal(output, ",\n      \"sha256\": \"");
        }
        if (status == CTOOL_OK) {
            status = obj_profile_append_digest(output, input->digest);
        }
        if (status == CTOOL_OK) {
            status = obj_append_literal(
                output, index + 1u == snapshot->input_count ? "\"\n    }\n" : "\"\n    },\n");
        }
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, "  ],\n  \"profiles\":");
    }
    if (status == CTOOL_OK) {
        status = obj_profile_emit_membership(output, snapshot->profiles, snapshot->profile_count,
                                             CTOOL_FALSE);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, ",\n  \"schema\": ");
    }
    if (status == CTOOL_OK) {
        status = obj_profile_append_quoted(output, snapshot->schema);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, ",\n  \"sources\":");
    }
    if (status == CTOOL_OK) {
        status = obj_profile_emit_membership(output, snapshot->profiles, snapshot->profile_count,
                                             CTOOL_TRUE);
    }
    if (status == CTOOL_OK) {
        status = obj_append_literal(output, "\n}\n");
    }
    return status;
}

static ctool_status_t obj_profile_manifest(ctool_job_t *job, const ctool_obj_request_t *request,
                                           ctool_buffer_t *output, ctool_obj_result_t *result_out) {
    ctool_arena_t *arena = ctool_job_arena(job);
    ctool_arena_mark_t mark = ctool_arena_mark(arena);
    obj_profile_snapshot_t snapshot;
    ctool_status_t status;
    ctool_u32 profile_index;
    ctool_u32 input_index;
    obj_zero(&snapshot, (ctool_u32)sizeof(snapshot));
    status = obj_profile_parse(job, request->input, arena, mark, &snapshot);
    if (status != CTOOL_OK) {
        return status;
    }
    for (profile_index = 0u; profile_index < snapshot.profile_count; profile_index++) {
        obj_profile_sort_strings(snapshot.profiles[profile_index].headers,
                                 snapshot.profiles[profile_index].header_count);
        obj_profile_sort_strings(snapshot.profiles[profile_index].sources,
                                 snapshot.profiles[profile_index].source_count);
    }
    obj_profile_sort_profiles(snapshot.profiles, snapshot.profile_count);
    obj_profile_sort_inputs(snapshot.inputs, snapshot.input_count);
    status = obj_profile_validate_portable_identity(job, request->input, arena, mark, &snapshot);
    if (status != CTOOL_OK) {
        return status;
    }
    status = obj_profile_validate_membership(job, request->input, arena, mark, &snapshot);
    if (status != CTOOL_OK) {
        return status;
    }
    for (input_index = 0u; input_index < snapshot.input_count; input_index++) {
        obj_profile_sha256(snapshot.inputs[input_index].contents,
                           snapshot.inputs[input_index].digest);
    }
    status = obj_profile_emit(output, &snapshot);
    if (status != CTOOL_OK) {
        return obj_profile_failure(job, request->input, arena, mark, status,
                                   status == CTOOL_ERR_LIMIT || status == CTOOL_ERR_OVERFLOW ||
                                           status == CTOOL_ERR_NO_MEMORY
                                       ? CTOOL_OBJ_DIAG_LIMIT
                                       : CTOOL_OBJ_DIAG_OUTPUT,
                                   0u, "CupidObj could not emit the profile manifest");
    }
    status = ctool_arena_rewind(arena, mark);
    if (status != CTOOL_OK) {
        return status;
    }
    result_out->bytes = ctool_buffer_view(output);
    return CTOOL_OK;
}
