/**
 * @file cupidobj_iso.inc
 * @brief Build ISO 9660 images and Rock Ridge records.
 */

static ctool_status_t obj_iso_failure(ctool_job_t *job, const ctool_source_t *source,
                                      ctool_arena_t *arena, ctool_arena_mark_t mark,
                                      ctool_status_t status, ctool_u32 code, ctool_u32 line,
                                      const char *message) {
    ctool_status_t emitted = obj_emit_failure_at(job, source, status, code, line, 0u, message);
    ctool_status_t rewound = ctool_arena_rewind(arena, mark);
    return rewound == CTOOL_OK ? emitted : rewound;
}

static ctool_u8 obj_iso_fold(ctool_u8 character) {
    if (character >= (ctool_u8)'A' && character <= (ctool_u8)'Z') {
        return (ctool_u8)(character + ((ctool_u8)'a' - (ctool_u8)'A'));
    }
    return character;
}

static ctool_i32 obj_iso_string_order(ctool_string_t left, ctool_string_t right) {
    ctool_u32 common = left.size < right.size ? left.size : right.size;
    ctool_u32 index;
    for (index = 0u; index < common; index++) {
        ctool_u8 left_byte = obj_iso_fold((ctool_u8)left.data[index]);
        ctool_u8 right_byte = obj_iso_fold((ctool_u8)right.data[index]);
        if (left_byte != right_byte) {
            return left_byte < right_byte ? -1 : 1;
        }
    }
    if (left.size != right.size) {
        return left.size < right.size ? -1 : 1;
    }
    for (index = 0u; index < left.size; index++) {
        ctool_u8 left_byte = (ctool_u8)left.data[index];
        ctool_u8 right_byte = (ctool_u8)right.data[index];
        if (left_byte != right_byte) {
            return left_byte < right_byte ? -1 : 1;
        }
    }
    return 0;
}

static ctool_bool obj_iso_string_equal_folded(ctool_string_t left, ctool_string_t right) {
    ctool_u32 index;
    if (left.size != right.size) {
        return CTOOL_FALSE;
    }
    for (index = 0u; index < left.size; index++) {
        if (obj_iso_fold((ctool_u8)left.data[index]) != obj_iso_fold((ctool_u8)right.data[index])) {
            return CTOOL_FALSE;
        }
    }
    return CTOOL_TRUE;
}

static ctool_bool obj_iso_component_character(ctool_u8 character) {
    return ((character >= (ctool_u8)'a' && character <= (ctool_u8)'z') ||
            (character >= (ctool_u8)'A' && character <= (ctool_u8)'Z') ||
            (character >= (ctool_u8)'0' && character <= (ctool_u8)'9') ||
            character == (ctool_u8)'.' || character == (ctool_u8)'_' || character == (ctool_u8)'-')
               ? CTOOL_TRUE
               : CTOOL_FALSE;
}

static ctool_bool obj_iso_path_shape(ctool_string_t path, ctool_bool directory,
                                     ctool_string_t *name_out) {
    ctool_u32 component_start = 0u;
    ctool_u32 components = 0u;
    ctool_u32 index;
    if (path.data == (const char *)0 || path.size == 0u || path.data[0] == '/' ||
        path.data[path.size - 1u] == '/') {
        return CTOOL_FALSE;
    }
    for (index = 0u; index <= path.size; index++) {
        if (index == path.size || path.data[index] == '/') {
            ctool_u32 component_size = index - component_start;
            if (component_size == 0u || component_size > 127u ||
                (component_size == 1u && path.data[component_start] == '.') ||
                (component_size == 2u && path.data[component_start] == '.' &&
                 path.data[component_start + 1u] == '.')) {
                return CTOOL_FALSE;
            }
            components++;
            if (index == path.size) {
                name_out->data = path.data + component_start;
                name_out->size = component_size;
            }
            component_start = index + 1u;
            continue;
        }
        if (obj_iso_component_character((ctool_u8)path.data[index]) == CTOOL_FALSE) {
            return CTOOL_FALSE;
        }
    }
    if ((directory == CTOOL_TRUE && components >= CTOOL_OBJ_ISO_MAX_DIRECTORY_DEPTH) ||
        (directory == CTOOL_FALSE && components > CTOOL_OBJ_ISO_MAX_DIRECTORY_DEPTH)) {
        return CTOOL_FALSE;
    }
    return CTOOL_TRUE;
}

static ctool_bool obj_iso_path_parent(ctool_string_t path, ctool_string_t *parent_out) {
    ctool_u32 index = path.size;
    while (index != 0u) {
        index--;
        if (path.data[index] == '/') {
            parent_out->data = path.data;
            parent_out->size = index;
            return CTOOL_TRUE;
        }
    }
    parent_out->data = path.data;
    parent_out->size = 0u;
    return CTOOL_FALSE;
}

static ctool_status_t obj_iso_validate_manifest(ctool_job_t *job, const ctool_source_t *manifest,
                                                obj_iso_node_t *nodes, ctool_u32 node_count,
                                                ctool_u8 *seen, ctool_arena_t *arena,
                                                ctool_arena_mark_t mark) {
    ctool_bytes_t contents = manifest->contents;
    ctool_u32 offset = 0u;
    ctool_u32 line = 1u;
    ctool_u32 matched = 0u;
    if (contents.data == (const ctool_u8 *)0 || contents.size == 0u) {
        return obj_iso_failure(job, manifest, arena, mark, CTOOL_ERR_INPUT,
                               CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
                               "CupidObj ISO fixture manifest is empty or invalid");
    }
    while (offset < contents.size) {
        ctool_u32 start = offset;
        ctool_u32 end;
        ctool_string_t path;
        ctool_string_t name;
        ctool_u32 index;
        ctool_u32 exact = 0xffffffffu;
        while (offset < contents.size && contents.data[offset] != (ctool_u8)'\n') {
            offset++;
        }
        end = offset;
        if (end > start && contents.data[end - 1u] == (ctool_u8)'\r') {
            end--;
        }
        path.data = (const char *)(contents.data + start);
        path.size = end - start;
        if (obj_iso_path_shape(path, CTOOL_FALSE, &name) == CTOOL_FALSE) {
            return obj_iso_failure(job, manifest, arena, mark, CTOOL_ERR_INPUT,
                                   CTOOL_OBJ_DIAG_INVALID_INPUT, line,
                                   "CupidObj ISO fixture manifest path is invalid");
        }
        for (index = 1u; index < node_count; index++) {
            if (obj_string_equal(path, nodes[index].path) == CTOOL_TRUE) {
                exact = index;
                break;
            }
            if (obj_iso_string_equal_folded(path, nodes[index].path) == CTOOL_TRUE) {
                return obj_iso_failure(job, manifest, arena, mark, CTOOL_ERR_INPUT,
                                       CTOOL_OBJ_DIAG_SYMBOL_COLLISION, line,
                                       "CupidObj ISO fixture manifest has a case collision");
            }
        }
        if (exact == 0xffffffffu) {
            return obj_iso_failure(job, manifest, arena, mark, CTOOL_ERR_INPUT,
                                   CTOOL_OBJ_DIAG_INVALID_INPUT, line,
                                   "CupidObj ISO fixture manifest entry has no typed input");
        }
        if (seen[exact - 1u] != 0u) {
            return obj_iso_failure(job, manifest, arena, mark, CTOOL_ERR_INPUT,
                                   CTOOL_OBJ_DIAG_SYMBOL_COLLISION, line,
                                   "CupidObj ISO fixture manifest contains a duplicate path");
        }
        seen[exact - 1u] = 1u;
        matched++;
        if (offset < contents.size) {
            offset++;
            line++;
        }
    }
    if (matched != node_count - 1u) {
        return obj_iso_failure(job, manifest, arena, mark, CTOOL_ERR_INPUT,
                               CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
                               "CupidObj ISO fixture typed input is absent from the manifest");
    }
    return CTOOL_OK;
}

static ctool_u8 obj_iso_identifier_character(ctool_u8 character) {
    ctool_u8 upper = character;
    if (upper >= (ctool_u8)'a' && upper <= (ctool_u8)'z') {
        upper = (ctool_u8)(upper - ((ctool_u8)'a' - (ctool_u8)'A'));
    }
    if ((upper >= (ctool_u8)'A' && upper <= (ctool_u8)'Z') ||
        (upper >= (ctool_u8)'0' && upper <= (ctool_u8)'9') || upper == (ctool_u8)'_') {
        return upper;
    }
    return (ctool_u8)'_';
}

static ctool_bool obj_iso_identifier_equal(const obj_iso_node_t *left, const ctool_u8 *right,
                                           ctool_u32 right_size) {
    ctool_u32 index;
    if (left->identifier_size != right_size) {
        return CTOOL_FALSE;
    }
    for (index = 0u; index < right_size; index++) {
        if (left->identifier[index] != right[index]) {
            return CTOOL_FALSE;
        }
    }
    return CTOOL_TRUE;
}

static ctool_i32 obj_iso_identifier_order(const obj_iso_node_t *left, const obj_iso_node_t *right) {
    ctool_u32 common = left->identifier_size < right->identifier_size ? left->identifier_size
                                                                      : right->identifier_size;
    ctool_u32 index;
    for (index = 0u; index < common; index++) {
        if (left->identifier[index] != right->identifier[index]) {
            return left->identifier[index] < right->identifier[index] ? -1 : 1;
        }
    }
    if (left->identifier_size == right->identifier_size) {
        return 0;
    }
    return left->identifier_size < right->identifier_size ? -1 : 1;
}

static ctool_status_t obj_iso_allocate_identifier(ctool_job_t *job, const ctool_source_t *manifest,
                                                  obj_iso_node_t *nodes, ctool_u32 node_count,
                                                  ctool_u32 node_index, ctool_arena_t *arena,
                                                  ctool_arena_mark_t mark) {
    obj_iso_node_t *node = &nodes[node_index];
    ctool_u32 dot = 0xffffffffu;
    ctool_u32 stem_size;
    ctool_u32 extension_size = 0u;
    ctool_u32 index;
    ctool_u32 sequence = 0u;
    if (node->directory == CTOOL_FALSE && node->name.size > 2u && node->name.data[0] != '.' &&
        node->name.data[node->name.size - 1u] != '.') {
        for (index = node->name.size; index != 0u; index--) {
            if (node->name.data[index - 1u] == '.') {
                dot = index - 1u;
                break;
            }
        }
    }
    stem_size = dot == 0xffffffffu ? node->name.size : dot;
    if (dot != 0xffffffffu) {
        extension_size = node->name.size - dot - 1u;
    }
    for (;;) {
        ctool_u8 candidate[CTOOL_OBJ_ISO_IDENTIFIER_BYTES];
        ctool_u8 stem[8];
        ctool_u8 extension[3];
        char suffix[10];
        ctool_u32 clean_stem = stem_size < 8u ? stem_size : 8u;
        ctool_u32 clean_extension = extension_size < 3u ? extension_size : 3u;
        ctool_u32 suffix_size = 0u;
        ctool_u32 candidate_size = 0u;
        ctool_bool collision = CTOOL_FALSE;
        if (clean_stem == 0u) {
            stem[0] = (ctool_u8)'_';
            clean_stem = 1u;
        } else {
            for (index = 0u; index < clean_stem; index++) {
                stem[index] = obj_iso_identifier_character((ctool_u8)node->name.data[index]);
            }
        }
        for (index = 0u; index < clean_extension; index++) {
            extension[index] =
                obj_iso_identifier_character((ctool_u8)node->name.data[dot + 1u + index]);
        }
        if (sequence != 0u) {
            suffix[0] = '_';
            suffix_size = 1u + obj_disk_decimal(suffix + 1u, sequence);
            if (suffix_size >= 8u) {
                return obj_iso_failure(job, manifest, arena, mark, CTOOL_ERR_LIMIT,
                                       CTOOL_OBJ_DIAG_LIMIT, 0u,
                                       "CupidObj ISO identifier collision space is exhausted");
            }
            if (clean_stem > 8u - suffix_size) {
                clean_stem = 8u - suffix_size;
            }
        }
        for (index = 0u; index < clean_stem; index++) {
            candidate[candidate_size++] = stem[index];
        }
        for (index = 0u; index < suffix_size; index++) {
            candidate[candidate_size++] = (ctool_u8)suffix[index];
        }
        if (node->directory == CTOOL_FALSE) {
            candidate[candidate_size++] = (ctool_u8)'.';
            for (index = 0u; index < clean_extension; index++) {
                candidate[candidate_size++] = extension[index];
            }
            candidate[candidate_size++] = (ctool_u8)';';
            candidate[candidate_size++] = (ctool_u8)'1';
        }
        for (index = 1u; index < node_count; index++) {
            if (index != node_index && nodes[index].parent == node->parent &&
                nodes[index].identifier_size != 0u &&
                obj_iso_identifier_equal(&nodes[index], candidate, candidate_size) == CTOOL_TRUE) {
                collision = CTOOL_TRUE;
                break;
            }
        }
        if (collision == CTOOL_FALSE) {
            node->identifier_size = candidate_size;
            for (index = 0u; index < candidate_size; index++) {
                node->identifier[index] = candidate[index];
            }
            return CTOOL_OK;
        }
        sequence++;
    }
}

static ctool_status_t obj_iso_build_nodes(ctool_job_t *job, const ctool_obj_request_t *request,
                                          ctool_arena_t *arena, ctool_arena_mark_t mark,
                                          obj_iso_node_t **nodes_out, ctool_u32 *node_count_out,
                                          ctool_u32 **directories_out,
                                          ctool_u32 *directory_count_out, ctool_u32 **files_out,
                                          ctool_u32 *file_count_out, ctool_u32 **children_out) {
    const ctool_obj_iso_fixture_request_t *iso = &request->as.iso_fixture;
    obj_iso_node_t *nodes = (obj_iso_node_t *)0;
    ctool_u8 *seen = (ctool_u8 *)0;
    ctool_u32 *directories = (ctool_u32 *)0;
    ctool_u32 *files = (ctool_u32 *)0;
    ctool_u32 *children = (ctool_u32 *)0;
    ctool_u32 node_count;
    ctool_u32 directory_count = 0u;
    ctool_u32 file_count = 0u;
    ctool_u32 index;
    ctool_status_t status;
    if (iso->entries == (const ctool_obj_iso_fixture_entry_t *)0 || iso->entry_count == 0u) {
        return obj_iso_failure(job, request->input, arena, mark, CTOOL_ERR_INVALID_ARGUMENT,
                               CTOOL_OBJ_DIAG_INVALID_REQUEST, 0u,
                               "CupidObj ISO fixture entries are required");
    }
    if (iso->entry_count > CTOOL_OBJ_ISO_ENTRY_LIMIT) {
        return obj_iso_failure(job, request->input, arena, mark, CTOOL_ERR_LIMIT,
                               CTOOL_OBJ_DIAG_LIMIT, 0u,
                               "CupidObj ISO fixture entry limit exceeded");
    }
    node_count = iso->entry_count + 1u;
    status = ctool_arena_alloc_zero(arena, node_count, (ctool_u32)sizeof(*nodes),
                                    (ctool_u32)sizeof(void *), (void **)&nodes);
    if (status == CTOOL_OK) {
        status = ctool_arena_alloc_zero(arena, iso->entry_count, 1u, 1u, (void **)&seen);
    }
    if (status == CTOOL_OK) {
        status = ctool_arena_alloc_zero(arena, node_count, (ctool_u32)sizeof(*directories),
                                        (ctool_u32)sizeof(ctool_u32), (void **)&directories);
    }
    if (status == CTOOL_OK) {
        status = ctool_arena_alloc_zero(arena, node_count, (ctool_u32)sizeof(*files),
                                        (ctool_u32)sizeof(ctool_u32), (void **)&files);
    }
    if (status == CTOOL_OK) {
        status = ctool_arena_alloc_zero(arena, node_count, (ctool_u32)sizeof(*children),
                                        (ctool_u32)sizeof(ctool_u32), (void **)&children);
    }
    if (status != CTOOL_OK) {
        return obj_iso_failure(job, request->input, arena, mark, status, CTOOL_OBJ_DIAG_LIMIT, 0u,
                               "CupidObj ISO fixture inventory exceeds its arena limit");
    }
    nodes[0].path = ctool_string("");
    nodes[0].name = ctool_string("");
    nodes[0].parent = 0xffffffffu;
    nodes[0].directory = CTOOL_TRUE;
    nodes[0].identifier[0] = 0u;
    nodes[0].identifier_size = 1u;
    for (index = 0u; index < iso->entry_count; index++) {
        const ctool_obj_iso_fixture_entry_t *entry = &iso->entries[index];
        obj_iso_node_t *node = &nodes[index + 1u];
        const ctool_limits_t *limits = ctool_job_limits(job);
        ctool_bool directory =
            entry->kind == CTOOL_OBJ_ISO_FIXTURE_DIRECTORY ? CTOOL_TRUE : CTOOL_FALSE;
        ctool_u32 prior;
        if (entry->kind != CTOOL_OBJ_ISO_FIXTURE_DIRECTORY &&
            entry->kind != CTOOL_OBJ_ISO_FIXTURE_FILE) {
            return obj_iso_failure(job, request->input, arena, mark, CTOOL_ERR_INVALID_ARGUMENT,
                                   CTOOL_OBJ_DIAG_INVALID_REQUEST, 0u,
                                   "CupidObj ISO fixture entry kind is invalid");
        }
        if ((directory == CTOOL_TRUE && entry->source != (const ctool_source_t *)0) ||
            (directory == CTOOL_FALSE && entry->source == (const ctool_source_t *)0)) {
            return obj_iso_failure(job, request->input, arena, mark, CTOOL_ERR_INVALID_ARGUMENT,
                                   CTOOL_OBJ_DIAG_INVALID_REQUEST, 0u,
                                   "CupidObj ISO fixture entry source does not match its kind");
        }
        if (directory == CTOOL_FALSE && entry->source->contents.data == (const ctool_u8 *)0 &&
            entry->source->contents.size != 0u) {
            return obj_iso_failure(job, entry->source, arena, mark, CTOOL_ERR_INVALID_ARGUMENT,
                                   CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
                                   "CupidObj ISO fixture file bytes are invalid");
        }
        if (entry->path.size > limits->path_bytes) {
            return obj_iso_failure(job, request->input, arena, mark, CTOOL_ERR_LIMIT,
                                   CTOOL_OBJ_DIAG_LIMIT, 0u,
                                   "CupidObj ISO fixture logical path exceeds the job limit");
        }
        if (obj_iso_path_shape(entry->path, directory, &node->name) == CTOOL_FALSE) {
            return obj_iso_failure(job, request->input, arena, mark, CTOOL_ERR_INPUT,
                                   CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
                                   "CupidObj ISO fixture logical path is invalid");
        }
        for (prior = 1u; prior <= index; prior++) {
            if (obj_iso_string_equal_folded(entry->path, nodes[prior].path) == CTOOL_TRUE) {
                return obj_iso_failure(job, request->input, arena, mark, CTOOL_ERR_INPUT,
                                       CTOOL_OBJ_DIAG_SYMBOL_COLLISION, 0u,
                                       "CupidObj ISO fixture paths have a case collision");
            }
        }
        node->entry = entry;
        node->path = entry->path;
        node->directory = directory;
    }
    status = obj_iso_validate_manifest(job, request->input, nodes, node_count, seen, arena, mark);
    if (status != CTOOL_OK) {
        return status;
    }
    for (index = 1u; index < node_count; index++) {
        ctool_string_t parent_path;
        ctool_bool has_parent = obj_iso_path_parent(nodes[index].path, &parent_path);
        ctool_u32 candidate;
        ctool_u32 parent = 0xffffffffu;
        if (has_parent == CTOOL_FALSE) {
            nodes[index].parent = 0u;
            continue;
        }
        for (candidate = 1u; candidate < node_count; candidate++) {
            if (obj_string_equal(parent_path, nodes[candidate].path) == CTOOL_TRUE) {
                parent = candidate;
                break;
            }
        }
        if (parent == 0xffffffffu || nodes[parent].directory == CTOOL_FALSE) {
            return obj_iso_failure(job, request->input, arena, mark, CTOOL_ERR_INPUT,
                                   CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
                                   "CupidObj ISO fixture entry has no directory parent");
        }
        nodes[index].parent = parent;
    }
    for (index = 0u; index < node_count; index++) {
        ctool_u32 child_count = 0u;
        ctool_u32 allocated;
        ctool_u32 child;
        if (nodes[index].directory == CTOOL_FALSE) {
            continue;
        }
        for (child = 1u; child < node_count; child++) {
            if (nodes[child].parent == index) {
                child_count++;
            }
        }
        for (allocated = 0u; allocated < child_count; allocated++) {
            ctool_u32 selected = 0xffffffffu;
            for (child = 1u; child < node_count; child++) {
                if (nodes[child].parent != index || nodes[child].identifier_size != 0u) {
                    continue;
                }
                if (selected == 0xffffffffu ||
                    obj_iso_string_order(nodes[child].name, nodes[selected].name) < 0) {
                    selected = child;
                }
            }
            if (selected == 0xffffffffu) {
                return obj_iso_failure(job, request->input, arena, mark, CTOOL_ERR_INTERNAL,
                                       CTOOL_OBJ_DIAG_OUTPUT, 0u,
                                       "CupidObj ISO fixture child ordering failed");
            }
            status = obj_iso_allocate_identifier(job, request->input, nodes, node_count, selected,
                                                 arena, mark);
            if (status != CTOOL_OK) {
                return status;
            }
        }
    }
    {
        ctool_u32 child_total = 0u;
        for (index = 0u; index < node_count; index++) {
            ctool_u32 child;
            nodes[index].child_start = child_total;
            if (nodes[index].directory == CTOOL_FALSE) {
                continue;
            }
            for (child = 1u; child < node_count; child++) {
                ctool_u32 position;
                if (nodes[child].parent != index) {
                    continue;
                }
                position = child_total;
                while (position > nodes[index].child_start &&
                       obj_iso_identifier_order(&nodes[child], &nodes[children[position - 1u]]) <
                           0) {
                    children[position] = children[position - 1u];
                    position--;
                }
                children[position] = child;
                child_total++;
            }
            nodes[index].child_count = child_total - nodes[index].child_start;
        }
    }
    nodes[0].directory_number = 1u;
    directories[directory_count++] = 0u;
    for (index = 0u; index < directory_count; index++) {
        ctool_u32 parent = directories[index];
        ctool_u32 ordinal;
        for (ordinal = 0u; ordinal < nodes[parent].child_count; ordinal++) {
            ctool_u32 selected = children[nodes[parent].child_start + ordinal];
            if (nodes[selected].directory == CTOOL_FALSE) {
                continue;
            }
            if (directory_count == 65535u) {
                return obj_iso_failure(job, request->input, arena, mark, CTOOL_ERR_LIMIT,
                                       CTOOL_OBJ_DIAG_LIMIT, 0u,
                                       "CupidObj ISO fixture directory count exceeds ECMA-119");
            }
            nodes[selected].directory_number = directory_count + 1u;
            directories[directory_count++] = selected;
        }
    }
    for (index = 1u; index < node_count; index++) {
        ctool_u32 position;
        if (nodes[index].directory == CTOOL_TRUE) {
            continue;
        }
        position = file_count;
        while (position != 0u &&
               obj_iso_string_order(nodes[index].path, nodes[files[position - 1u]].path) < 0) {
            files[position] = files[position - 1u];
            position--;
        }
        files[position] = index;
        file_count++;
    }
    *nodes_out = nodes;
    *node_count_out = node_count;
    *directories_out = directories;
    *directory_count_out = directory_count;
    *files_out = files;
    *file_count_out = file_count;
    *children_out = children;
    return CTOOL_OK;
}

typedef enum {
    OBJ_ISO_RECORD_PVD_ROOT = 0,
    OBJ_ISO_RECORD_ROOT_DOT,
    OBJ_ISO_RECORD_DOT,
    OBJ_ISO_RECORD_DOT_DOT,
    OBJ_ISO_RECORD_CHILD
} obj_iso_record_kind_t;

static void obj_iso_copy(ctool_u8 *destination, const ctool_u8 *source, ctool_u32 count) {
    ctool_u32 index;
    for (index = 0u; index < count; index++) {
        destination[index] = source[index];
    }
}

static void obj_iso_fill(ctool_u8 *destination, ctool_u8 value, ctool_u32 count) {
    ctool_u32 index;
    for (index = 0u; index < count; index++) {
        destination[index] = value;
    }
}

static void obj_iso_write_le16(ctool_u8 *bytes, ctool_u16 value) {
    bytes[0] = (ctool_u8)(value & 0xffu);
    bytes[1] = (ctool_u8)((value >> 8u) & 0xffu);
}

static void obj_iso_write_be16(ctool_u8 *bytes, ctool_u16 value) {
    bytes[0] = (ctool_u8)((value >> 8u) & 0xffu);
    bytes[1] = (ctool_u8)(value & 0xffu);
}

static void obj_iso_write_le32(ctool_u8 *bytes, ctool_u32 value) {
    bytes[0] = (ctool_u8)(value & 0xffu);
    bytes[1] = (ctool_u8)((value >> 8u) & 0xffu);
    bytes[2] = (ctool_u8)((value >> 16u) & 0xffu);
    bytes[3] = (ctool_u8)((value >> 24u) & 0xffu);
}

static void obj_iso_write_be32(ctool_u8 *bytes, ctool_u32 value) {
    bytes[0] = (ctool_u8)((value >> 24u) & 0xffu);
    bytes[1] = (ctool_u8)((value >> 16u) & 0xffu);
    bytes[2] = (ctool_u8)((value >> 8u) & 0xffu);
    bytes[3] = (ctool_u8)(value & 0xffu);
}

static void obj_iso_write_both16(ctool_u8 *bytes, ctool_u16 value) {
    obj_iso_write_le16(bytes, value);
    obj_iso_write_be16(bytes + 2u, value);
}

static void obj_iso_write_both32(ctool_u8 *bytes, ctool_u32 value) {
    obj_iso_write_le32(bytes, value);
    obj_iso_write_be32(bytes + 4u, value);
}

static ctool_u32 obj_iso_directory_child_count(const obj_iso_node_t *nodes, ctool_u32 node_count,
                                               ctool_u32 parent) {
    ctool_u32 count = 0u;
    ctool_u32 index;
    for (index = 1u; index < node_count; index++) {
        if (nodes[index].parent == parent && nodes[index].directory == CTOOL_TRUE) {
            count++;
        }
    }
    return count;
}

static ctool_u32 obj_iso_record_susp_size(obj_iso_record_kind_t kind, const obj_iso_node_t *node) {
    if (kind == OBJ_ISO_RECORD_PVD_ROOT) {
        return 0u;
    }
    if (kind == OBJ_ISO_RECORD_ROOT_DOT) {
        return 7u + 36u + 26u + 28u;
    }
    if (kind == OBJ_ISO_RECORD_DOT || kind == OBJ_ISO_RECORD_DOT_DOT) {
        return 36u + 26u;
    }
    return 36u + 26u + 5u + node->name.size;
}

static ctool_u32 obj_iso_record_size(obj_iso_record_kind_t kind, ctool_u32 identifier_size,
                                     const obj_iso_node_t *node) {
    ctool_u32 size = 33u + identifier_size + (identifier_size % 2u == 0u ? 1u : 0u) +
                     obj_iso_record_susp_size(kind, node);
    if (size % 2u != 0u) {
        size++;
    }
    return size;
}

static ctool_status_t obj_iso_directory_size(ctool_job_t *job, const ctool_source_t *manifest,
                                             const obj_iso_node_t *nodes, const ctool_u32 *children,
                                             ctool_u32 directory, ctool_u32 *size_out,
                                             ctool_arena_t *arena, ctool_arena_mark_t mark) {
    ctool_u32 cursor = 0u;
    ctool_u32 index;
    ctool_u32 sizes[2];
    sizes[0] = obj_iso_record_size(directory == 0u ? OBJ_ISO_RECORD_ROOT_DOT : OBJ_ISO_RECORD_DOT,
                                   1u, &nodes[directory]);
    sizes[1] = obj_iso_record_size(OBJ_ISO_RECORD_DOT_DOT, 1u, &nodes[directory]);
    for (index = 0u; index < 2u; index++) {
        ctool_u32 remaining = CTOOL_OBJ_ISO_BLOCK_BYTES - (cursor % CTOOL_OBJ_ISO_BLOCK_BYTES);
        if (sizes[index] > 255u) {
            return obj_iso_failure(job, manifest, arena, mark, CTOOL_ERR_LIMIT,
                                   CTOOL_OBJ_DIAG_LIMIT, 0u,
                                   "CupidObj ISO directory record exceeds one byte");
        }
        if (sizes[index] > remaining) {
            cursor += remaining;
        }
        cursor += sizes[index];
    }
    for (index = 0u; index < nodes[directory].child_count; index++) {
        ctool_u32 child = children[nodes[directory].child_start + index];
        ctool_u32 size;
        ctool_u32 remaining;
        size =
            obj_iso_record_size(OBJ_ISO_RECORD_CHILD, nodes[child].identifier_size, &nodes[child]);
        if (size > 255u) {
            return obj_iso_failure(job, manifest, arena, mark, CTOOL_ERR_LIMIT,
                                   CTOOL_OBJ_DIAG_LIMIT, 0u,
                                   "CupidObj Rock Ridge directory record is too long");
        }
        remaining = CTOOL_OBJ_ISO_BLOCK_BYTES - (cursor % CTOOL_OBJ_ISO_BLOCK_BYTES);
        if (size > remaining) {
            cursor += remaining;
        }
        cursor += size;
    }
    if (cursor % CTOOL_OBJ_ISO_BLOCK_BYTES != 0u) {
        cursor += CTOOL_OBJ_ISO_BLOCK_BYTES - (cursor % CTOOL_OBJ_ISO_BLOCK_BYTES);
    }
    *size_out = cursor;
    return CTOOL_OK;
}

static void obj_iso_write_px(ctool_u8 *bytes, const obj_iso_node_t *nodes, ctool_u32 node_count,
                             ctool_u32 node_index) {
    const obj_iso_node_t *node = &nodes[node_index];
    ctool_u32 mode = node->directory == CTOOL_TRUE ? 040555u : 0100444u;
    ctool_u32 links = node->directory == CTOOL_TRUE
                          ? 2u + obj_iso_directory_child_count(nodes, node_count, node_index)
                          : 1u;
    bytes[0] = (ctool_u8)'P';
    bytes[1] = (ctool_u8)'X';
    bytes[2] = 36u;
    bytes[3] = 1u;
    obj_iso_write_both32(bytes + 4u, mode);
    obj_iso_write_both32(bytes + 12u, links);
    obj_iso_write_both32(bytes + 20u, 0u);
    obj_iso_write_both32(bytes + 28u, 0u);
}

static void obj_iso_write_tf(ctool_u8 *bytes) {
    static const ctool_u8 recording[7] = {100u, 1u, 1u, 0u, 0u, 0u, 0u};
    bytes[0] = (ctool_u8)'T';
    bytes[1] = (ctool_u8)'F';
    bytes[2] = 26u;
    bytes[3] = 1u;
    bytes[4] = 0x0eu;
    obj_iso_copy(bytes + 5u, recording, 7u);
    obj_iso_copy(bytes + 12u, recording, 7u);
    obj_iso_copy(bytes + 19u, recording, 7u);
}

static void obj_iso_write_ce(ctool_u8 *bytes, ctool_u32 extent) {
    bytes[0] = (ctool_u8)'C';
    bytes[1] = (ctool_u8)'E';
    bytes[2] = 28u;
    bytes[3] = 1u;
    obj_iso_write_both32(bytes + 4u, extent);
    obj_iso_write_both32(bytes + 12u, 0u);
    obj_iso_write_both32(bytes + 20u, CTOOL_OBJ_ISO_ER_BYTES);
}

static void obj_iso_write_nm(ctool_u8 *bytes, ctool_string_t name) {
    bytes[0] = (ctool_u8)'N';
    bytes[1] = (ctool_u8)'M';
    bytes[2] = (ctool_u8)(5u + name.size);
    bytes[3] = 1u;
    bytes[4] = 0u;
    obj_iso_copy(bytes + 5u, (const ctool_u8 *)name.data, name.size);
}

static ctool_u32 obj_iso_write_record(ctool_u8 *bytes, ctool_u32 extent, ctool_u32 data_size,
                                      const ctool_u8 *identifier, ctool_u32 identifier_size,
                                      ctool_bool directory, obj_iso_record_kind_t kind,
                                      const obj_iso_node_t *nodes, ctool_u32 node_count,
                                      ctool_u32 metadata_node, ctool_u32 continuation_extent) {
    static const ctool_u8 recording[7] = {100u, 1u, 1u, 0u, 0u, 0u, 0u};
    const obj_iso_node_t *node = &nodes[metadata_node];
    ctool_u32 size = obj_iso_record_size(kind, identifier_size, node);
    ctool_u32 padding = identifier_size % 2u == 0u ? 1u : 0u;
    ctool_u32 susp = 33u + identifier_size + padding;
    bytes[0] = (ctool_u8)size;
    bytes[1] = 0u;
    obj_iso_write_both32(bytes + 2u, extent);
    obj_iso_write_both32(bytes + 10u, data_size);
    obj_iso_copy(bytes + 18u, recording, 7u);
    bytes[25] = directory == CTOOL_TRUE ? 0x02u : 0u;
    bytes[26] = 0u;
    bytes[27] = 0u;
    obj_iso_write_both16(bytes + 28u, 1u);
    bytes[32] = (ctool_u8)identifier_size;
    obj_iso_copy(bytes + 33u, identifier, identifier_size);
    if (kind == OBJ_ISO_RECORD_PVD_ROOT) {
        return size;
    }
    if (kind == OBJ_ISO_RECORD_ROOT_DOT) {
        static const ctool_u8 sp[7] = {'S', 'P', 7u, 1u, 0xbeu, 0xefu, 0u};
        obj_iso_copy(bytes + susp, sp, 7u);
        susp += 7u;
    }
    obj_iso_write_px(bytes + susp, nodes, node_count, metadata_node);
    susp += 36u;
    obj_iso_write_tf(bytes + susp);
    susp += 26u;
    if (kind == OBJ_ISO_RECORD_ROOT_DOT) {
        obj_iso_write_ce(bytes + susp, continuation_extent);
    } else if (kind == OBJ_ISO_RECORD_CHILD) {
        obj_iso_write_nm(bytes + susp, node->name);
    }
    return size;
}

static void obj_iso_write_directory(ctool_u8 *bytes, const obj_iso_node_t *nodes,
                                    ctool_u32 node_count, const ctool_u32 *children,
                                    ctool_u32 directory, ctool_u32 continuation_extent) {
    const obj_iso_node_t *node = &nodes[directory];
    ctool_u32 parent = directory == 0u ? 0u : node->parent;
    ctool_u32 cursor = 0u;
    ctool_u32 index;
    ctool_u8 dot = 0u;
    ctool_u8 dot_dot = 1u;
    obj_iso_record_kind_t dot_kind = directory == 0u ? OBJ_ISO_RECORD_ROOT_DOT : OBJ_ISO_RECORD_DOT;
    ctool_u32 size = obj_iso_record_size(dot_kind, 1u, node);
    cursor += obj_iso_write_record(bytes + cursor, node->extent, node->size, &dot, 1u, CTOOL_TRUE,
                                   dot_kind, nodes, node_count, directory, continuation_extent);
    size = obj_iso_record_size(OBJ_ISO_RECORD_DOT_DOT, 1u, &nodes[parent]);
    if (size > CTOOL_OBJ_ISO_BLOCK_BYTES - (cursor % CTOOL_OBJ_ISO_BLOCK_BYTES)) {
        cursor += CTOOL_OBJ_ISO_BLOCK_BYTES - (cursor % CTOOL_OBJ_ISO_BLOCK_BYTES);
    }
    cursor += obj_iso_write_record(bytes + cursor, nodes[parent].extent, nodes[parent].size,
                                   &dot_dot, 1u, CTOOL_TRUE, OBJ_ISO_RECORD_DOT_DOT, nodes,
                                   node_count, parent, continuation_extent);
    for (index = 0u; index < nodes[directory].child_count; index++) {
        ctool_u32 child = children[nodes[directory].child_start + index];
        const obj_iso_node_t *child_node = &nodes[child];
        ctool_u32 child_size = child_node->directory == CTOOL_TRUE
                                   ? child_node->size
                                   : child_node->entry->source->contents.size;
        size = obj_iso_record_size(OBJ_ISO_RECORD_CHILD, child_node->identifier_size, child_node);
        if (size > CTOOL_OBJ_ISO_BLOCK_BYTES - (cursor % CTOOL_OBJ_ISO_BLOCK_BYTES)) {
            cursor += CTOOL_OBJ_ISO_BLOCK_BYTES - (cursor % CTOOL_OBJ_ISO_BLOCK_BYTES);
        }
        cursor += obj_iso_write_record(bytes + cursor, child_node->extent, child_size,
                                       child_node->identifier, child_node->identifier_size,
                                       child_node->directory, OBJ_ISO_RECORD_CHILD, nodes,
                                       node_count, child, continuation_extent);
    }
}

static void obj_iso_write_identifier_field(ctool_u8 *bytes, ctool_u32 size, const char *value) {
    ctool_string_t text = ctool_string(value);
    obj_iso_fill(bytes, (ctool_u8)' ', size);
    obj_iso_copy(bytes, (const ctool_u8 *)text.data, text.size);
}

static void obj_iso_write_er(ctool_u8 *bytes) {
    static const char identifier[] = "RRIP_1991A";
    static const char description[] =
        "THE ROCK RIDGE INTERCHANGE PROTOCOL PROVIDES SUPPORT FOR POSIX "
        "FILE SYSTEM SEMANTICS";
    static const char source[] = "PLEASE CONTACT DISC PUBLISHER FOR SPECIFICATION SOURCE.  SEE "
                                 "PUBLISHER IDENTIFIER IN PRIMARY VOLUME DESCRIPTOR FOR CONTACT "
                                 "INFORMATION.";
    bytes[0] = (ctool_u8)'E';
    bytes[1] = (ctool_u8)'R';
    bytes[2] = (ctool_u8)CTOOL_OBJ_ISO_ER_BYTES;
    bytes[3] = 1u;
    bytes[4] = (ctool_u8)((ctool_u32)sizeof(identifier) - 1u);
    bytes[5] = (ctool_u8)((ctool_u32)sizeof(description) - 1u);
    bytes[6] = (ctool_u8)((ctool_u32)sizeof(source) - 1u);
    bytes[7] = 1u;
    obj_iso_copy(bytes + 8u, (const ctool_u8 *)identifier, (ctool_u32)sizeof(identifier) - 1u);
    obj_iso_copy(bytes + 18u, (const ctool_u8 *)description, (ctool_u32)sizeof(description) - 1u);
    obj_iso_copy(bytes + 102u, (const ctool_u8 *)source, (ctool_u32)sizeof(source) - 1u);
}

static ctool_u32 obj_iso_path_table_size(const obj_iso_node_t *nodes, const ctool_u32 *directories,
                                         ctool_u32 directory_count) {
    ctool_u32 size = 0u;
    ctool_u32 index;
    for (index = 0u; index < directory_count; index++) {
        ctool_u32 identifier_size = index == 0u ? 1u : nodes[directories[index]].identifier_size;
        size += 8u + identifier_size + (identifier_size % 2u != 0u ? 1u : 0u);
    }
    return size;
}

static void obj_iso_write_path_table(ctool_u8 *bytes, const obj_iso_node_t *nodes,
                                     const ctool_u32 *directories, ctool_u32 directory_count,
                                     ctool_bool big_endian) {
    ctool_u32 cursor = 0u;
    ctool_u32 index;
    ctool_u8 root_identifier = 0u;
    for (index = 0u; index < directory_count; index++) {
        const obj_iso_node_t *node = &nodes[directories[index]];
        const ctool_u8 *identifier = index == 0u ? &root_identifier : node->identifier;
        ctool_u32 identifier_size = index == 0u ? 1u : node->identifier_size;
        ctool_u32 parent_number = index == 0u ? 1u : nodes[node->parent].directory_number;
        bytes[cursor] = (ctool_u8)identifier_size;
        bytes[cursor + 1u] = 0u;
        if (big_endian == CTOOL_TRUE) {
            obj_iso_write_be32(bytes + cursor + 2u, node->extent);
            obj_iso_write_be16(bytes + cursor + 6u, (ctool_u16)parent_number);
        } else {
            obj_iso_write_le32(bytes + cursor + 2u, node->extent);
            obj_iso_write_le16(bytes + cursor + 6u, (ctool_u16)parent_number);
        }
        obj_iso_copy(bytes + cursor + 8u, identifier, identifier_size);
        cursor += 8u + identifier_size;
        if (identifier_size % 2u != 0u) {
            bytes[cursor++] = 0u;
        }
    }
}

static void obj_iso_write_primary_descriptor(ctool_u8 *bytes, ctool_u32 volume_blocks,
                                             ctool_u32 path_table_size,
                                             ctool_u32 little_path_extent,
                                             ctool_u32 big_path_extent, const obj_iso_node_t *nodes,
                                             ctool_u32 node_count) {
    static const ctool_u8 header[7] = {1u, 'C', 'D', '0', '0', '1', 1u};
    static const ctool_u8 volume_date[17] = {'2', '0', '0', '0', '0', '1', '0', '1', '0',
                                             '0', '0', '0', '0', '0', '0', '0', 0u};
    static const ctool_u8 unspecified_date[17] = {'0', '0', '0', '0', '0', '0', '0', '0', '0',
                                                  '0', '0', '0', '0', '0', '0', '0', 0u};
    ctool_u8 root_identifier = 0u;
    obj_iso_copy(bytes, header, 7u);
    obj_iso_write_identifier_field(bytes + 8u, 32u, "CUPID OS");
    obj_iso_write_identifier_field(bytes + 40u, 32u, "CUPID_OS_TEST");
    obj_iso_write_both32(bytes + 80u, volume_blocks);
    obj_iso_write_both16(bytes + 120u, 1u);
    obj_iso_write_both16(bytes + 124u, 1u);
    obj_iso_write_both16(bytes + 128u, (ctool_u16)CTOOL_OBJ_ISO_BLOCK_BYTES);
    obj_iso_write_both32(bytes + 132u, path_table_size);
    obj_iso_write_le32(bytes + 140u, little_path_extent);
    obj_iso_write_be32(bytes + 148u, big_path_extent);
    (void)obj_iso_write_record(bytes + 156u, nodes[0].extent, nodes[0].size, &root_identifier, 1u,
                               CTOOL_TRUE, OBJ_ISO_RECORD_PVD_ROOT, nodes, node_count, 0u, 0u);
    obj_iso_write_identifier_field(bytes + 190u, 128u, "CUPID_OS_TEST_FIXTURE");
    obj_iso_write_identifier_field(bytes + 318u, 128u, "CUPID OS");
    obj_iso_write_identifier_field(bytes + 446u, 128u, "CUPID OS REPOSITORY HOSTBUILD");
    obj_iso_write_identifier_field(bytes + 574u, 128u, "CUPID OS DETERMINISTIC ISO9660 AUTHOR");
    obj_iso_write_identifier_field(bytes + 702u, 37u, "");
    obj_iso_write_identifier_field(bytes + 739u, 37u, "");
    obj_iso_write_identifier_field(bytes + 776u, 37u, "");
    obj_iso_copy(bytes + 813u, volume_date, 17u);
    obj_iso_copy(bytes + 830u, volume_date, 17u);
    obj_iso_copy(bytes + 847u, unspecified_date, 17u);
    obj_iso_copy(bytes + 864u, volume_date, 17u);
    bytes[881u] = 1u;
}

static ctool_status_t obj_iso_fixture(ctool_job_t *job, const ctool_obj_request_t *request,
                                      ctool_buffer_t *output, ctool_obj_result_t *result_out) {
    ctool_arena_t *arena = ctool_job_arena(job);
    ctool_arena_mark_t mark = ctool_arena_mark(arena);
    obj_iso_node_t *nodes = (obj_iso_node_t *)0;
    ctool_u32 *directories = (ctool_u32 *)0;
    ctool_u32 *files = (ctool_u32 *)0;
    ctool_u32 *children = (ctool_u32 *)0;
    ctool_u32 node_count = 0u;
    ctool_u32 directory_count = 0u;
    ctool_u32 file_count = 0u;
    ctool_u32 path_table_size;
    ctool_u32 path_table_blocks;
    ctool_u32 little_path_extent = 18u;
    ctool_u32 big_path_extent;
    ctool_u32 continuation_extent;
    ctool_u64 next_extent;
    ctool_u64 output_bytes_u64;
    ctool_u32 output_offset;
    ctool_mut_bytes_t reserved;
    ctool_u8 *bytes;
    ctool_u32 index;
    ctool_status_t status;
    ctool_status_t rewind_status;

    if (request->input->contents.data == (const ctool_u8 *)0 &&
        request->input->contents.size != 0u) {
        return obj_iso_failure(job, request->input, arena, mark, CTOOL_ERR_INVALID_ARGUMENT,
                               CTOOL_OBJ_DIAG_INVALID_INPUT, 0u,
                               "CupidObj ISO fixture manifest bytes are invalid");
    }
    status = obj_iso_build_nodes(job, request, arena, mark, &nodes, &node_count, &directories,
                                 &directory_count, &files, &file_count, &children);
    if (status != CTOOL_OK) {
        return status;
    }
    for (index = 0u; index < directory_count; index++) {
        ctool_u32 node_index = directories[index];
        status = obj_iso_directory_size(job, request->input, nodes, children, node_index,
                                        &nodes[node_index].size, arena, mark);
        if (status != CTOOL_OK) {
            return status;
        }
    }
    path_table_size = obj_iso_path_table_size(nodes, directories, directory_count);
    path_table_blocks =
        (path_table_size + CTOOL_OBJ_ISO_BLOCK_BYTES - 1u) / CTOOL_OBJ_ISO_BLOCK_BYTES;
    big_path_extent = little_path_extent + path_table_blocks;
    next_extent = (ctool_u64)big_path_extent + path_table_blocks;
    for (index = 0u; index < directory_count; index++) {
        obj_iso_node_t *node = &nodes[directories[index]];
        if (next_extent > (ctool_u64)0xffffffffu) {
            return obj_iso_failure(job, request->input, arena, mark, CTOOL_ERR_OVERFLOW,
                                   CTOOL_OBJ_DIAG_LIMIT, 0u,
                                   "CupidObj ISO directory extents overflow ECMA-119");
        }
        node->extent = (ctool_u32)next_extent;
        next_extent += node->size / CTOOL_OBJ_ISO_BLOCK_BYTES;
    }
    if (next_extent > (ctool_u64)0xffffffffu) {
        return obj_iso_failure(job, request->input, arena, mark, CTOOL_ERR_OVERFLOW,
                               CTOOL_OBJ_DIAG_LIMIT, 0u,
                               "CupidObj ISO continuation extent overflows ECMA-119");
    }
    continuation_extent = (ctool_u32)next_extent;
    next_extent++;
    for (index = 0u; index < file_count; index++) {
        obj_iso_node_t *node = &nodes[files[index]];
        ctool_u32 size = node->entry->source->contents.size;
        if (size == 0u) {
            node->extent = 0u;
            continue;
        }
        if (next_extent > (ctool_u64)0xffffffffu) {
            return obj_iso_failure(job, request->input, arena, mark, CTOOL_ERR_OVERFLOW,
                                   CTOOL_OBJ_DIAG_LIMIT, 0u,
                                   "CupidObj ISO file extents overflow ECMA-119");
        }
        node->extent = (ctool_u32)next_extent;
        next_extent +=
            ((ctool_u64)size + CTOOL_OBJ_ISO_BLOCK_BYTES - 1u) / CTOOL_OBJ_ISO_BLOCK_BYTES;
    }
    output_bytes_u64 = next_extent * CTOOL_OBJ_ISO_BLOCK_BYTES;
    if (next_extent > (ctool_u64)0xffffffffu || output_bytes_u64 > (ctool_u64)0xffffffffu) {
        return obj_iso_failure(job, request->input, arena, mark, CTOOL_ERR_OVERFLOW,
                               CTOOL_OBJ_DIAG_LIMIT, 0u,
                               "CupidObj ISO output exceeds the i386 byte limit");
    }
    status =
        ctool_buffer_reserve_zero(output, (ctool_u32)output_bytes_u64, &output_offset, &reserved);
    if (status != CTOOL_OK) {
        return obj_iso_failure(job, request->input, arena, mark, status,
                               status == CTOOL_ERR_LIMIT || status == CTOOL_ERR_OVERFLOW
                                   ? CTOOL_OBJ_DIAG_LIMIT
                                   : CTOOL_OBJ_DIAG_OUTPUT,
                               0u, "CupidObj ISO output cannot be reserved");
    }
    bytes = reserved.data;
    obj_iso_write_primary_descriptor(bytes + 16u * CTOOL_OBJ_ISO_BLOCK_BYTES,
                                     (ctool_u32)next_extent, path_table_size, little_path_extent,
                                     big_path_extent, nodes, node_count);
    bytes[17u * CTOOL_OBJ_ISO_BLOCK_BYTES] = 0xffu;
    obj_iso_copy(bytes + 17u * CTOOL_OBJ_ISO_BLOCK_BYTES + 1u, (const ctool_u8 *)"CD001", 5u);
    bytes[17u * CTOOL_OBJ_ISO_BLOCK_BYTES + 6u] = 1u;
    obj_iso_write_path_table(bytes + little_path_extent * CTOOL_OBJ_ISO_BLOCK_BYTES, nodes,
                             directories, directory_count, CTOOL_FALSE);
    obj_iso_write_path_table(bytes + big_path_extent * CTOOL_OBJ_ISO_BLOCK_BYTES, nodes,
                             directories, directory_count, CTOOL_TRUE);
    obj_iso_write_er(bytes + continuation_extent * CTOOL_OBJ_ISO_BLOCK_BYTES);
    for (index = 0u; index < directory_count; index++) {
        ctool_u32 node_index = directories[index];
        obj_iso_write_directory(bytes + nodes[node_index].extent * CTOOL_OBJ_ISO_BLOCK_BYTES, nodes,
                                node_count, children, node_index, continuation_extent);
    }
    for (index = 0u; index < file_count; index++) {
        const obj_iso_node_t *node = &nodes[files[index]];
        ctool_bytes_t contents = node->entry->source->contents;
        if (contents.size != 0u) {
            obj_iso_copy(bytes + node->extent * CTOOL_OBJ_ISO_BLOCK_BYTES, contents.data,
                         contents.size);
        }
    }
    rewind_status = ctool_arena_rewind(arena, mark);
    if (rewind_status != CTOOL_OK) {
        return rewind_status;
    }
    (void)output_offset;
    result_out->bytes = ctool_buffer_view(output);
    return CTOOL_OK;
}
