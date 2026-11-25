/**
 * @file cupidbuild_compile_profile.inc
 * @brief Freeze compiler inputs and publish checked profile objects and manifests.
 */

static char cupidbuild_profile_fold(char character) {
    return character >= 'A' && character <= 'Z' ? (char)(character - 'A' + 'a') : character;
}

static int cupidbuild_profile_path_equal_folded(const char *left, const char *right) {
    size_t index = 0u;
    while (left[index] != '\0' && right[index] != '\0') {
        if (cupidbuild_profile_fold(left[index]) != cupidbuild_profile_fold(right[index])) {
            return 0;
        }
        index++;
    }
    return left[index] == right[index];
}

static int cupidbuild_profile_path_valid(const char *path) {
    size_t size;
    size_t component = 0u;
    size_t index;
    if (path == (const char *)0 || path[0] == '\0') {
        return 0;
    }
    size = strlen(path);
    if (size > 1024u || path[0] == '/' || path[size - 1u] == '/') {
        return 0;
    }
    for (index = 0u; index <= size; index++) {
        unsigned char byte = (unsigned char)path[index];
        if (index == size || byte == (unsigned char)'/') {
            size_t component_size = index - component;
            if (component_size == 0u || (component_size == 1u && path[component] == '.') ||
                (component_size == 2u && path[component] == '.' && path[component + 1u] == '.')) {
                return 0;
            }
            component = index + 1u;
        } else if (!((byte >= (unsigned char)'a' && byte <= (unsigned char)'z') ||
                     (byte >= (unsigned char)'A' && byte <= (unsigned char)'Z') ||
                     (byte >= (unsigned char)'0' && byte <= (unsigned char)'9') ||
                     byte == (unsigned char)'.' || byte == (unsigned char)'_' ||
                     byte == (unsigned char)'-')) {
            return 0;
        }
    }
    return 1;
}

static int cupidbuild_profile_paths_valid(const cupidbuild_host_path_list_t *paths) {
    size_t index;
    size_t prior;
    if (paths == (const cupidbuild_host_path_list_t *)0 ||
        (paths->count != 0u &&
         (paths->paths == (char **)0 || paths->snapshots == (cupidbuild_host_snapshot_t *)0))) {
        return 0;
    }
    for (index = 0u; index < paths->count; index++) {
        if (!cupidbuild_profile_path_valid(paths->paths[index])) {
            return 0;
        }
        for (prior = 0u; prior < index; prior++) {
            if (cupidbuild_profile_path_equal_folded(paths->paths[prior], paths->paths[index])) {
                return 0;
            }
        }
    }
    return 1;
}

static size_t cupidbuild_profile_compat_source_count(void) {
    return sizeof(cupidbuild_profile_compat_sources) / sizeof(cupidbuild_profile_compat_sources[0]);
}

static size_t cupidbuild_profile_tree_source_count(void) {
    return sizeof(cupidbuild_profile_tree_sources) / sizeof(cupidbuild_profile_tree_sources[0]);
}

static const char *cupidbuild_profile_expected_source(size_t index) {
    size_t compat_count = cupidbuild_profile_compat_source_count();
    return index < compat_count ? cupidbuild_profile_compat_sources[index]
                                : cupidbuild_profile_tree_sources[index - compat_count];
}

static int cupidbuild_profile_sources_exact(const cupidbuild_host_path_list_t *sources) {
    size_t expected_count =
        cupidbuild_profile_compat_source_count() + cupidbuild_profile_tree_source_count();
    size_t index;
    if (sources->count != expected_count) {
        return 0;
    }
    for (index = 0u; index < expected_count; index++) {
        if (strcmp(sources->paths[index], cupidbuild_profile_expected_source(index)) != 0) {
            return 0;
        }
    }
    return 1;
}

static void cupidbuild_profile_membership_close(cupidbuild_profile_membership_t *membership) {
    cupidbuild_host_path_list_close(&membership->headers);
    cupidbuild_host_path_list_close(&membership->sources);
}

static int cupidbuild_profile_discover(cupidbuild_host_transaction_t *transaction,
                                       cupidbuild_profile_membership_t *membership) {
    (void)memset(membership, 0, sizeof(*membership));
    if (!cupidbuild_host_discover_files(transaction, cupidbuild_profile_header_roots,
                                        sizeof(cupidbuild_profile_header_roots) /
                                            sizeof(cupidbuild_profile_header_roots[0]),
                                        cupidbuild_profile_header_suffixes,
                                        sizeof(cupidbuild_profile_header_suffixes) /
                                            sizeof(cupidbuild_profile_header_suffixes[0]),
                                        1, 0, &membership->headers) ||
        !cupidbuild_host_discover_files(transaction, cupidbuild_profile_source_roots,
                                        sizeof(cupidbuild_profile_source_roots) /
                                            sizeof(cupidbuild_profile_source_roots[0]),
                                        cupidbuild_profile_source_suffixes,
                                        sizeof(cupidbuild_profile_source_suffixes) /
                                            sizeof(cupidbuild_profile_source_suffixes[0]),
                                        0, 1, &membership->sources)) {
        cupidbuild_profile_membership_close(membership);
        return 0;
    }
    if (membership->headers.count == 0u || !cupidbuild_profile_paths_valid(&membership->headers) ||
        !cupidbuild_profile_paths_valid(&membership->sources) ||
        !cupidbuild_profile_sources_exact(&membership->sources)) {
        cupidbuild_profile_membership_close(membership);
        return 0;
    }
    return 1;
}

static int cupidbuild_profile_membership_equal(const cupidbuild_profile_membership_t *left,
                                               const cupidbuild_profile_membership_t *right) {
    size_t index;
    if (left == (const cupidbuild_profile_membership_t *)0 ||
        right == (const cupidbuild_profile_membership_t *)0 ||
        left->headers.count != right->headers.count ||
        left->sources.count != right->sources.count) {
        return 0;
    }
    for (index = 0u; index < left->headers.count; index++) {
        if (strcmp(left->headers.paths[index], right->headers.paths[index]) != 0 ||
            !cupidbuild_host_snapshot_equal(&left->headers.snapshots[index],
                                            &right->headers.snapshots[index])) {
            return 0;
        }
    }
    for (index = 0u; index < left->sources.count; index++) {
        if (strcmp(left->sources.paths[index], right->sources.paths[index]) != 0 ||
            !cupidbuild_host_snapshot_equal(&left->sources.snapshots[index],
                                            &right->sources.snapshots[index])) {
            return 0;
        }
    }
    return 1;
}

static int cupidbuild_profile_require_membership(cupidbuild_host_transaction_t *transaction,
                                                 const cupidbuild_profile_membership_t *expected) {
    cupidbuild_profile_membership_t current;
    int valid = cupidbuild_profile_discover(transaction, &current);
    if (valid != 0) {
        valid = cupidbuild_profile_membership_equal(expected, &current);
    }
    cupidbuild_profile_membership_close(&current);
    return valid;
}

static int cupidbuild_profile_buffer_reserve(cupidbuild_profile_buffer_t *buffer,
                                             size_t additional) {
    size_t needed;
    size_t capacity;
    unsigned char *grown;
    if (additional > CUPIDBUILD_PROFILE_SNAPSHOT_BYTES - buffer->size) {
        return 0;
    }
    needed = buffer->size + additional;
    if (needed <= buffer->capacity) {
        return 1;
    }
    capacity = buffer->capacity == 0u ? 4096u : buffer->capacity;
    while (capacity < needed) {
        if (capacity > CUPIDBUILD_PROFILE_SNAPSHOT_BYTES / 2u) {
            capacity = CUPIDBUILD_PROFILE_SNAPSHOT_BYTES;
            break;
        }
        capacity *= 2u;
    }
    grown = (unsigned char *)realloc(buffer->bytes, capacity);
    if (grown == (unsigned char *)0) {
        return 0;
    }
    buffer->bytes = grown;
    buffer->capacity = capacity;
    return 1;
}

static int cupidbuild_profile_append(cupidbuild_profile_buffer_t *buffer, const void *bytes,
                                     size_t size) {
    if (!cupidbuild_profile_buffer_reserve(buffer, size)) {
        return 0;
    }
    if (size != 0u) {
        (void)memcpy(buffer->bytes + buffer->size, bytes, size);
    }
    buffer->size += size;
    return 1;
}

static int cupidbuild_profile_append_text(cupidbuild_profile_buffer_t *buffer, const char *text) {
    return cupidbuild_profile_append(buffer, text, strlen(text));
}

static int cupidbuild_profile_append_u32(cupidbuild_profile_buffer_t *buffer, size_t value) {
    unsigned char bytes[4];
    if (value > 4294967295u) {
        return 0;
    }
    bytes[0] = (unsigned char)value;
    bytes[1] = (unsigned char)(value >> 8u);
    bytes[2] = (unsigned char)(value >> 16u);
    bytes[3] = (unsigned char)(value >> 24u);
    return cupidbuild_profile_append(buffer, bytes, sizeof(bytes));
}

static int cupidbuild_profile_append_snapshot_text(cupidbuild_profile_buffer_t *buffer,
                                                   const char *text) {
    size_t size = strlen(text);
    return cupidbuild_profile_append_u32(buffer, size) &&
           cupidbuild_profile_append(buffer, text, size);
}

static int cupidbuild_profile_append_snapshot_sources(cupidbuild_profile_buffer_t *buffer,
                                                      const char *const *sources, size_t count) {
    size_t index;
    if (!cupidbuild_profile_append_u32(buffer, count)) {
        return 0;
    }
    for (index = 0u; index < count; index++) {
        if (!cupidbuild_profile_append_snapshot_text(buffer, sources[index])) {
            return 0;
        }
    }
    return 1;
}

static int cupidbuild_profile_append_snapshot_profile(cupidbuild_profile_buffer_t *buffer,
                                                      const char *name,
                                                      const cupidbuild_host_path_list_t *headers,
                                                      const char *const *sources,
                                                      size_t source_count) {
    size_t index;
    if (!cupidbuild_profile_append_snapshot_text(buffer, name) ||
        !cupidbuild_profile_append_u32(buffer, headers->count)) {
        return 0;
    }
    for (index = 0u; index < headers->count; index++) {
        if (!cupidbuild_profile_append_snapshot_text(buffer, headers->paths[index])) {
            return 0;
        }
    }
    return cupidbuild_profile_append_snapshot_sources(buffer, sources, source_count);
}

static int cupidbuild_profile_snapshot(cupidbuild_host_transaction_t *transaction,
                                       const cupidbuild_profile_membership_t *membership,
                                       cupidbuild_profile_header_t *headers,
                                       cupidbuild_profile_buffer_t *snapshot) {
    static const unsigned char magic[8] = {'C', 'U', 'P', 'R', 'O', 'F', '1', 0};
    size_t index;
    if (!cupidbuild_profile_append(snapshot, magic, sizeof(magic)) ||
        !cupidbuild_profile_append_snapshot_text(snapshot, "cupid.doom-profile-inputs.v1") ||
        !cupidbuild_profile_append_u32(snapshot, 2u) ||
        !cupidbuild_profile_append_snapshot_profile(snapshot, "doom-compat", &membership->headers,
                                                    cupidbuild_profile_compat_sources,
                                                    cupidbuild_profile_compat_source_count()) ||
        !cupidbuild_profile_append_snapshot_profile(snapshot, "doom-tree", &membership->headers,
                                                    cupidbuild_profile_tree_sources,
                                                    cupidbuild_profile_tree_source_count()) ||
        !cupidbuild_profile_append_u32(snapshot, membership->headers.count)) {
        return 0;
    }
    for (index = 0u; index < membership->headers.count; index++) {
        unsigned char *contents;
        size_t size = 0u;
        if (!cupidbuild_profile_append_snapshot_text(snapshot, headers[index].path)) {
            return 0;
        }
        contents = cupidbuild_host_read_frozen_input(transaction, headers[index].frozen_path,
                                                     CUPIDBUILD_TOOL_BYTES, &size);
        if (contents == (unsigned char *)0 || !cupidbuild_profile_append_u32(snapshot, size) ||
            !cupidbuild_profile_append(snapshot, contents, size)) {
            free(contents);
            return 0;
        }
        headers[index].size = size;
        cupidbuild_host_sha256_bytes(contents, size, headers[index].sha256);
        free(contents);
    }
    return 1;
}

static int cupidbuild_profile_append_json_path_array(cupidbuild_profile_buffer_t *json,
                                                     const char *const *paths, size_t count) {
    size_t index;
    for (index = 0u; index < count; index++) {
        if (!cupidbuild_profile_append_text(json, "      \"") ||
            !cupidbuild_profile_append_text(json, paths[index]) ||
            !cupidbuild_profile_append_text(json, index + 1u == count ? "\"\n" : "\",\n")) {
            return 0;
        }
    }
    return 1;
}

static int cupidbuild_profile_append_json_digest(cupidbuild_profile_buffer_t *json,
                                                 const unsigned char digest[32]) {
    static const char hex[] = "0123456789abcdef";
    char text[65];
    size_t index;
    for (index = 0u; index < 32u; index++) {
        text[index * 2u] = hex[digest[index] >> 4u];
        text[index * 2u + 1u] = hex[digest[index] & 15u];
    }
    text[64] = '\0';
    return cupidbuild_profile_append_text(json, text);
}

static int cupidbuild_profile_render_json(const cupidbuild_profile_membership_t *membership,
                                          const cupidbuild_profile_header_t *headers,
                                          cupidbuild_profile_buffer_t *json) {
    char size_text[32];
    size_t index;
    if (!cupidbuild_profile_append_text(json, "{\n  \"inputs\": [\n")) {
        return 0;
    }
    for (index = 0u; index < membership->headers.count; index++) {
        int written =
            snprintf(size_text, sizeof(size_text), "%u", (unsigned int)headers[index].size);
        if (written <= 0 || (size_t)written >= sizeof(size_text) ||
            !cupidbuild_profile_append_text(json, "    {\n      \"bytes\": ") ||
            !cupidbuild_profile_append_text(json, size_text) ||
            !cupidbuild_profile_append_text(json, ",\n      \"path\": \"") ||
            !cupidbuild_profile_append_text(json, headers[index].path) ||
            !cupidbuild_profile_append_text(json, "\",\n      \"sha256\": \"") ||
            !cupidbuild_profile_append_json_digest(json, headers[index].sha256) ||
            !cupidbuild_profile_append_text(
                json, index + 1u == membership->headers.count ? "\"\n    }\n" : "\"\n    },\n")) {
            return 0;
        }
    }
    if (!cupidbuild_profile_append_text(json,
                                        "  ],\n  \"profiles\": {\n    \"doom-compat\": [\n") ||
        !cupidbuild_profile_append_json_path_array(
            json, (const char *const *)membership->headers.paths, membership->headers.count) ||
        !cupidbuild_profile_append_text(json, "    ],\n    \"doom-tree\": [\n") ||
        !cupidbuild_profile_append_json_path_array(
            json, (const char *const *)membership->headers.paths, membership->headers.count) ||
        !cupidbuild_profile_append_text(json,
                                        "    ]\n  },\n  \"schema\": "
                                        "\"cupid.doom-profile-inputs.v1\",\n  \"sources\": {\n"
                                        "    \"doom-compat\": [\n") ||
        !cupidbuild_profile_append_json_path_array(json, cupidbuild_profile_compat_sources,
                                                   cupidbuild_profile_compat_source_count()) ||
        !cupidbuild_profile_append_text(json, "    ],\n    \"doom-tree\": [\n") ||
        !cupidbuild_profile_append_json_path_array(json, cupidbuild_profile_tree_sources,
                                                   cupidbuild_profile_tree_source_count()) ||
        !cupidbuild_profile_append_text(json, "    ]\n  }\n}\n")) {
        return 0;
    }
    return 1;
}

int cupidbuild_generate_profile_manifest(const cupidbuild_profile_request_t *request) {
    return cupidbuild_generate_profile_manifest_with_release(request, (const char *)0);
}

int cupidbuild_generate_profile_manifest_with_release(const cupidbuild_profile_request_t *request,
                                                      const char *seed_release) {
    cupidbuild_host_transaction_t *transaction = (cupidbuild_host_transaction_t *)0;
    cupidbuild_host_profile_parent_t *profile_parent = (cupidbuild_host_profile_parent_t *)0;
    cupidbuild_seed_capture_t seed;
    cupidbuild_profile_membership_t membership;
    cupidbuild_profile_header_t *headers = (cupidbuild_profile_header_t *)0;
    cupidbuild_profile_buffer_t snapshot;
    cupidbuild_profile_buffer_t expected;
    cupidbuild_host_snapshot_t snapshot_capture;
    cupidbuild_host_snapshot_t candidate_capture;
    unsigned char *candidate = (unsigned char *)0;
    const char *object_arguments[5];
    size_t index;
    int status;
    int changed = 0;
    int result = 1;
    (void)memset(&seed, 0, sizeof(seed));
    (void)memset(&membership, 0, sizeof(membership));
    (void)memset(&snapshot, 0, sizeof(snapshot));
    (void)memset(&expected, 0, sizeof(expected));
    if (request == (const cupidbuild_profile_request_t *)0 ||
        !cupidbuild_path_safe(request->repository_root, 0) ||
        !cupidbuild_path_safe(request->output, 1) ||
        !cupidbuild_path_safe(request->seed_manifest, 0) || strlen(request->output) < 5u ||
        strcmp(request->output + strlen(request->output) - 5u, ".json") != 0) {
        (void)fprintf(stderr, "cupidbuild: invalid profile manifest request\n");
        return 1;
    }
    if (strcmp(request->output, CUPIDBUILD_PROFILE_OUTPUT) == 0 &&
        !cupidbuild_host_profile_parent_prepare(request->repository_root, &profile_parent)) {
        (void)fprintf(stderr, "cupidbuild: %s\n",
                      cupidbuild_host_profile_parent_error(profile_parent));
        goto done;
    }
    if (!cupidbuild_host_profile_transaction_open(request->repository_root,
                                                  cupidbuild_profile_compat_sources[0],
                                                  request->output, profile_parent, &transaction)) {
        (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
        goto done;
    }
    if (!cupidbuild_profile_discover(transaction, &membership)) {
        (void)fprintf(stderr, "cupidbuild: Doom profile closure is malformed or differs from the "
                              "approved source cohort\n");
        goto done;
    }
    if (!cupidbuild_host_seal_discovery(transaction)) {
        (void)fprintf(stderr, "cupidbuild: Doom profile directory closure cannot be "
                              "sealed\n");
        goto done;
    }
    {
        char initial_source[CUPIDBUILD_PATH_BYTES];
        if (!cupidbuild_join(initial_source, sizeof(initial_source), request->repository_root,
                             membership.sources.paths[0]) ||
            !cupidbuild_host_input_matches_snapshot(transaction, initial_source,
                                                    &membership.sources.snapshots[0])) {
            (void)fprintf(stderr, "cupidbuild: Doom profile source changed after discovery\n");
            goto done;
        }
    }
    if (membership.headers.count + membership.sources.count + 7u +
                (seed_release != (const char *)0 ? 1u : 0u) >
            CUPIDBUILD_PROFILE_TRANSACTION_INPUTS ||
        !cupidbuild_host_reserve_inputs(transaction,
                                        membership.headers.count + membership.sources.count + 7u +
                                            (seed_release != (const char *)0 ? 1u : 0u))) {
        (void)fprintf(stderr, "cupidbuild: Doom profile closure exceeds the frozen input "
                              "limit\n");
        goto done;
    }
    headers = (cupidbuild_profile_header_t *)calloc(membership.headers.count, sizeof(*headers));
    if (headers == (cupidbuild_profile_header_t *)0) {
        (void)fprintf(stderr, "cupidbuild: Doom profile header table cannot be allocated\n");
        goto done;
    }
    for (index = 0u; index < membership.headers.count; index++) {
        char live_path[CUPIDBUILD_PATH_BYTES];
        char private_name[32];
        cupidbuild_host_snapshot_t frozen_from;
        int written = snprintf(private_name, sizeof(private_name), "profile-header-%03u",
                               (unsigned int)index);
        headers[index].path = membership.headers.paths[index];
        if (written <= 0 || (size_t)written >= sizeof(private_name) ||
            !cupidbuild_join(live_path, sizeof(live_path), request->repository_root,
                             headers[index].path) ||
            !cupidbuild_host_freeze_input(transaction, live_path, private_name,
                                          &headers[index].frozen_path, &frozen_from)) {
            (void)fprintf(stderr, "cupidbuild: Doom profile header cannot be frozen: %s\n",
                          cupidbuild_host_error(transaction));
            goto done;
        }
        if (!cupidbuild_host_snapshot_equal(&frozen_from, &membership.headers.snapshots[index])) {
            (void)fprintf(stderr, "cupidbuild: Doom profile header changed after discovery: %s\n",
                          headers[index].path);
            goto done;
        }
    }
    for (index = 1u; index < membership.sources.count; index++) {
        char live_path[CUPIDBUILD_PATH_BYTES];
        char private_name[32];
        const char *frozen_source = (const char *)0;
        cupidbuild_host_snapshot_t frozen_from;
        int written = snprintf(private_name, sizeof(private_name), "profile-source-%03u",
                               (unsigned int)index);
        if (written <= 0 || (size_t)written >= sizeof(private_name) ||
            !cupidbuild_join(live_path, sizeof(live_path), request->repository_root,
                             membership.sources.paths[index]) ||
            !cupidbuild_host_freeze_input(transaction, live_path, private_name, &frozen_source,
                                          &frozen_from)) {
            (void)fprintf(stderr, "cupidbuild: Doom profile source cannot be frozen: %s\n",
                          cupidbuild_host_error(transaction));
            goto done;
        }
        if (!cupidbuild_host_snapshot_equal(&frozen_from, &membership.sources.snapshots[index])) {
            (void)fprintf(stderr, "cupidbuild: Doom profile source changed after discovery: %s\n",
                          membership.sources.paths[index]);
            goto done;
        }
    }
    if (!cupidbuild_seed_freeze(transaction, request->repository_root, request->seed_manifest, 1, 1,
                                seed_release, &seed)) {
        goto done;
    }
    if (!cupidbuild_profile_snapshot(transaction, &membership, headers, &snapshot) ||
        !cupidbuild_host_write_private_output(transaction, snapshot.bytes, snapshot.size) ||
        !cupidbuild_host_capture_private_output(transaction, &snapshot_capture,
                                                (unsigned char **)0)) {
        (void)fprintf(stderr, "cupidbuild: CUPROF1 snapshot could not be frozen: %s\n",
                      cupidbuild_host_error(transaction));
        goto done;
    }
    object_arguments[0] = "profile-manifest";
    object_arguments[1] = cupidbuild_host_private_output(transaction);
    object_arguments[2] = "-o";
    object_arguments[3] = cupidbuild_host_candidate(transaction);
    object_arguments[4] = (const char *)0;
    status =
        cupidbuild_host_run_in_private(transaction, seed.frozen_tools[4], object_arguments, 60000u);
    if (!cupidbuild_seed_require_live(transaction, &seed)) {
        goto done;
    }
    if (status != 0) {
        cupidbuild_report_checked_failure(transaction, "checked CupidObj failed");
        goto done;
    }
    if (!cupidbuild_host_require_private_output(transaction, &snapshot_capture) ||
        !cupidbuild_host_capture_candidate(transaction, &candidate_capture, &candidate)) {
        (void)fprintf(stderr, "cupidbuild: %s\n", cupidbuild_host_error(transaction));
        goto done;
    }
    if (!cupidbuild_profile_render_json(&membership, headers, &expected)) {
        (void)fprintf(stderr, "cupidbuild: independent profile manifest renderer failed\n");
        goto done;
    }
    if (candidate_capture.size != expected.size ||
        memcmp(candidate, expected.bytes, expected.size) != 0) {
        (void)fprintf(stderr, "cupidbuild: checked CupidObj profile manifest differs from the "
                              "independent renderer\n");
        goto done;
    }
    free(candidate);
    candidate = (unsigned char *)0;
    if (!cupidbuild_profile_require_membership(transaction, &membership)) {
        (void)fprintf(stderr, "cupidbuild: Doom profile input membership changed while "
                              "authoring the manifest\n");
        goto done;
    }
    if (!cupidbuild_seed_require_live(transaction, &seed) ||
        !cupidbuild_host_require_frozen_inputs(transaction) ||
        !cupidbuild_host_require_private_output(transaction, &snapshot_capture) ||
        !cupidbuild_host_require_candidate(transaction, &candidate_capture) ||
        (profile_parent != (cupidbuild_host_profile_parent_t *)0 &&
         !cupidbuild_host_profile_parent_bind(profile_parent, transaction)) ||
        !cupidbuild_host_require_publication_boundary(transaction)) {
        (void)fprintf(stderr, "cupidbuild: profile manifest publication failed: %s\n",
                      cupidbuild_host_error(transaction));
        goto done;
    }
    if (!cupidbuild_profile_require_membership(transaction, &membership)) {
        (void)fprintf(stderr, "cupidbuild: Doom profile input membership changed at publication "
                              "boundary\n");
        goto done;
    }
    if (!cupidbuild_host_publish_if_changed(transaction, &changed)) {
        (void)fprintf(stderr, "cupidbuild: profile manifest publication failed: %s\n",
                      cupidbuild_host_error(transaction));
        goto done;
    }
    (void)changed;
    cupidbuild_host_profile_parent_commit(profile_parent);
    result = 0;

done:
    free(candidate);
    free(expected.bytes);
    free(snapshot.bytes);
    free(headers);
    cupidbuild_profile_membership_close(&membership);
    cupidbuild_seed_capture_close(&seed);
    result = cupidbuild_finish_publication(transaction, result, "profile manifest");
    if (!cupidbuild_host_profile_parent_close(profile_parent)) {
        (void)fprintf(stderr, "cupidbuild: profile parent cleanup failed\n");
        result = 1;
    }
    return result;
}

static void cupidbuild_compile_put_u32(unsigned char *bytes, size_t value) {
    bytes[0] = (unsigned char)value;
    bytes[1] = (unsigned char)(value >> 8u);
    bytes[2] = (unsigned char)(value >> 16u);
    bytes[3] = (unsigned char)(value >> 24u);
}

static int cupidbuild_compile_write_bundle(cupidbuild_host_transaction_t *transaction,
                                           const cupidbuild_compile_input_t *inputs, size_t count,
                                           cupidbuild_host_snapshot_t *bundle_snapshot) {
    size_t total = 12u;
    size_t offset = 12u;
    size_t index;
    unsigned char *bundle;
    int result = 0;
    if (count == 0u || count > CUPIDBUILD_PROFILE_TRANSACTION_INPUTS) {
        return 0;
    }
    for (index = 0u; index < count; index++) {
        size_t overhead = 9u + strlen(inputs[index].path);
        if ((index != 0u && strcmp(inputs[index - 1u].path, inputs[index].path) >= 0) ||
            overhead > CUPIDBUILD_TOOL_BYTES - total ||
            inputs[index].snapshot.size > CUPIDBUILD_TOOL_BYTES - total - overhead) {
            return 0;
        }
        total += overhead + (size_t)inputs[index].snapshot.size;
    }
    bundle = (unsigned char *)malloc(total);
    if (bundle == (unsigned char *)0) {
        return 0;
    }
    (void)memcpy(bundle, "CUPSRC1\n", 8u);
    cupidbuild_compile_put_u32(bundle + 8u, count);
    for (index = 0u; index < count; index++) {
        size_t size = 0u;
        size_t path_size = strlen(inputs[index].path) + 1u;
        unsigned char *contents = cupidbuild_host_read_frozen_input(
            transaction, inputs[index].frozen_path, CUPIDBUILD_TOOL_BYTES, &size);
        if (contents == (unsigned char *)0 || size != inputs[index].snapshot.size) {
            free(contents);
            goto done;
        }
        cupidbuild_compile_put_u32(bundle + offset, path_size);
        cupidbuild_compile_put_u32(bundle + offset + 4u, size);
        offset += 8u;
        bundle[offset++] = '/';
        (void)memcpy(bundle + offset, inputs[index].path, path_size - 1u);
        offset += path_size - 1u;
        if (size != 0u) {
            (void)memcpy(bundle + offset, contents, size);
        }
        offset += size;
        free(contents);
    }
    result =
        offset == total && cupidbuild_host_write_private_output(transaction, bundle, total) &&
        cupidbuild_host_capture_private_output(transaction, bundle_snapshot, (unsigned char **)0);
done:
    free(bundle);
    return result;
}

static int cupidbuild_compile_capture_input(cupidbuild_host_transaction_t *transaction,
                                            const char *root, const char *source, const char *path,
                                            size_t index,
                                            const cupidbuild_host_snapshot_t *discovered,
                                            cupidbuild_compile_input_t *input) {
    char live_path[CUPIDBUILD_PATH_BYTES];
    char private_name[32];
    int written =
        snprintf(private_name, sizeof(private_name), "compile-input-%03u", (unsigned int)index);
    (void)memset(input, 0, sizeof(*input));
    input->path = path;
    if (written <= 0 || (size_t)written >= sizeof(private_name) ||
        !cupidbuild_join(live_path, sizeof(live_path), root, path)) {
        return 0;
    }
    if (strcmp(path, source) == 0) {
        unsigned char *contents;
        size_t captured_size;
        if (discovered != (const cupidbuild_host_snapshot_t *)0 &&
            !cupidbuild_host_input_matches_snapshot(transaction, live_path, discovered)) {
            return 0;
        }
        input->frozen_path = cupidbuild_host_frozen_source(transaction);
        contents = cupidbuild_host_read_frozen_input(transaction, input->frozen_path,
                                                     CUPIDBUILD_TOOL_BYTES, &captured_size);
        if (contents == (unsigned char *)0) {
            return 0;
        }
        input->snapshot.size = captured_size;
        free(contents);
        return 1;
    }
    return cupidbuild_host_freeze_input(transaction, live_path, private_name, &input->frozen_path,
                                        &input->snapshot) &&
           (discovered == (const cupidbuild_host_snapshot_t *)0 ||
            cupidbuild_host_snapshot_equal(&input->snapshot, discovered));
}

static int cupidbuild_compile_bundle(cupidbuild_host_transaction_t *transaction, const char *root,
                                     const cupidbuild_compile_closure_t *closure,
                                     cupidbuild_host_snapshot_t *bundle_snapshot,
                                     size_t seed_input_count) {
    cupidbuild_compile_input_t inputs[CUPIDBUILD_COMPILE_INPUTS];
    size_t index;
    int source_found = 0;
    if (closure == (const cupidbuild_compile_closure_t *)0 || closure->source == (const char *)0 ||
        closure->count == 0u || closure->count > CUPIDBUILD_COMPILE_INPUTS ||
        !cupidbuild_host_reserve_inputs(transaction, closure->count + seed_input_count)) {
        return 0;
    }
    for (index = 0u; index < closure->count; index++) {
        if (closure->inputs[index] == (const char *)0 ||
            !cupidbuild_compile_capture_input(
                transaction, root, closure->source, closure->inputs[index], index,
                (const cupidbuild_host_snapshot_t *)0, &inputs[index])) {
            return 0;
        }
        if (strcmp(closure->inputs[index], closure->source) == 0) {
            source_found = 1;
        }
    }
    return source_found &&
           cupidbuild_compile_write_bundle(transaction, inputs, closure->count, bundle_snapshot);
}

static int cupidbuild_compile_doom_bundle(cupidbuild_host_transaction_t *transaction,
                                          const char *root, const char *source,
                                          cupidbuild_host_snapshot_t *bundle_snapshot,
                                          size_t seed_input_count) {
    cupidbuild_profile_membership_t membership;
    cupidbuild_compile_input_t *inputs = (cupidbuild_compile_input_t *)0;
    cupidbuild_compile_input_t selected;
    size_t index;
    size_t insertion = 0u;
    size_t total;
    int source_found = 0;
    int result = 0;
    (void)memset(&membership, 0, sizeof(membership));
    (void)memset(&selected, 0, sizeof(selected));
    if (!cupidbuild_host_begin_compile_discovery(transaction) ||
        !cupidbuild_profile_discover(transaction, &membership) ||
        !cupidbuild_host_seal_compile_discovery(transaction)) {
        (void)fprintf(stderr, "cupidbuild: Doom compiler input membership is invalid\n");
        goto done;
    }
    total = membership.headers.count + membership.sources.count + seed_input_count;
    if (total > CUPIDBUILD_PROFILE_TRANSACTION_INPUTS ||
        !cupidbuild_host_reserve_inputs(transaction, total)) {
        (void)fprintf(stderr, "cupidbuild: Doom compiler input count exceeds the limit\n");
        goto done;
    }
    inputs = (cupidbuild_compile_input_t *)calloc(membership.headers.count + 1u, sizeof(*inputs));
    if (inputs == (cupidbuild_compile_input_t *)0) {
        goto done;
    }
    for (index = 0u; index < membership.headers.count; index++) {
        if (!cupidbuild_compile_capture_input(
                transaction, root, source, membership.headers.paths[index], index,
                &membership.headers.snapshots[index], &inputs[index])) {
            goto done;
        }
    }
    for (index = 0u; index < membership.sources.count; index++) {
        cupidbuild_compile_input_t captured;
        if (!cupidbuild_compile_capture_input(transaction, root, source,
                                              membership.sources.paths[index],
                                              membership.headers.count + index,
                                              &membership.sources.snapshots[index], &captured)) {
            goto done;
        }
        if (strcmp(captured.path, source) == 0) {
            selected = captured;
            source_found = 1;
        }
    }
    if (!source_found) {
        goto done;
    }
    while (insertion < membership.headers.count && strcmp(inputs[insertion].path, source) < 0) {
        insertion++;
    }
    for (index = membership.headers.count; index > insertion; index--) {
        inputs[index] = inputs[index - 1u];
    }
    inputs[insertion] = selected;
    result = cupidbuild_compile_write_bundle(transaction, inputs, membership.headers.count + 1u,
                                             bundle_snapshot);
done:
    free(inputs);
    cupidbuild_profile_membership_close(&membership);
    return result;
}

static int cupidbuild_compile(const cupidbuild_compile_request_t *request,
                              cupidbuild_compile_kind_t kind, const char *seed_release) {
    cupidbuild_host_transaction_t *transaction = (cupidbuild_host_transaction_t *)0;
    cupidbuild_host_output_parent_t *output_parent = (cupidbuild_host_output_parent_t *)0;
    cupidbuild_seed_capture_t seed;
    const cupidbuild_compile_closure_t *closure = (const cupidbuild_compile_closure_t *)0;
    cupidbuild_host_snapshot_t bundle_snapshot;
    cupidbuild_host_snapshot_t candidate_snapshot;
    unsigned char *candidate = (unsigned char *)0;
    char logical_source[CUPIDBUILD_PATH_BYTES];
    char compiler_root[CUPIDBUILD_PATH_BYTES];
    char logical_output[CUPIDBUILD_PATH_BYTES];
    char expected_output[CUPIDBUILD_PATH_BYTES];
    const char *arguments[80];
    const char *candidate_path;
    const char *separator;
    size_t index;
    size_t count = 0u;
    size_t source_size;
    int doom = kind == CUPIDBUILD_COMPILE_DOOM;
    int production = kind == CUPIDBUILD_COMPILE_PRODUCTION;
    int user = kind == CUPIDBUILD_COMPILE_USER;
    const char *cohort = user ? "user" : (doom ? "Doom" : (production ? "production" : "kernel"));
    const cupidbuild_compile_closure_t *closures =
        user ? cupidbuild_compile_user_closures
             : (production ? cupidbuild_compile_production_closures : cupidbuild_compile_closures);
    size_t closure_count =
        user
            ? sizeof(cupidbuild_compile_user_closures) / sizeof(cupidbuild_compile_user_closures[0])
            : (production
                   ? sizeof(cupidbuild_compile_production_closures) /
                         sizeof(cupidbuild_compile_production_closures[0])
                   : sizeof(cupidbuild_compile_closures) / sizeof(cupidbuild_compile_closures[0]));
    int doom_tree = 0;
    const char *const *profile = cupidbuild_compile_profile;
    size_t profile_count =
        sizeof(cupidbuild_compile_profile) / sizeof(cupidbuild_compile_profile[0]);
    int status;
    int changed = 0;
    int result = 1;
    (void)memset(&seed, 0, sizeof(seed));
    if (user) {
        profile = cupidbuild_compile_user_profile;
        profile_count =
            sizeof(cupidbuild_compile_user_profile) / sizeof(cupidbuild_compile_user_profile[0]);
    }
    if (request == (const cupidbuild_compile_request_t *)0 ||
        (!user && !cupidbuild_path_safe(request->repository_root, 0)) ||
        !cupidbuild_path_safe(request->source, 1) ||
        (!user && !cupidbuild_path_safe(request->output, 1)) ||
        !cupidbuild_path_safe(request->seed_manifest, 0)) {
        (void)fprintf(stderr, "cupidbuild: invalid %s compile request\n", cohort);
        return 1;
    }
    if (doom != 0) {
        size_t source_count =
            cupidbuild_profile_compat_source_count() + cupidbuild_profile_tree_source_count();
        for (index = 0u; index < source_count; index++) {
            if (strcmp(request->source, cupidbuild_profile_expected_source(index)) == 0) {
                break;
            }
        }
        if (index == source_count) {
            (void)fprintf(stderr, "cupidbuild: source is outside the approved Doom cohort\n");
            return 1;
        }
        doom_tree = index >= cupidbuild_profile_compat_source_count();
        profile = cupidbuild_compile_doom_profile;
        profile_count =
            sizeof(cupidbuild_compile_doom_profile) / sizeof(cupidbuild_compile_doom_profile[0]);
    } else {
        for (index = 0u; index < closure_count; index++) {
            if (strcmp(request->source, closures[index].source) == 0) {
                closure = &closures[index];
                break;
            }
        }
        if (closure == (const cupidbuild_compile_closure_t *)0) {
            (void)fprintf(stderr, "cupidbuild: source has no approved frozen %s closure\n", cohort);
            return 1;
        }
    }
    source_size = strlen(request->source);
    (void)memcpy(expected_output, request->source, source_size - 2u);
    expected_output[source_size - 2u] = 'o';
    expected_output[source_size - 1u] = '\0';
    if (!user && strcmp(request->output, expected_output) != 0) {
        (void)fprintf(stderr, "cupidbuild: %s source and output binding differ\n", cohort);
        return 1;
    }
    logical_source[0] = '/';
    (void)memcpy(logical_source + 1u, request->source, source_size + 1u);
    if (user && !cupidbuild_host_output_parent_prepare(request->repository_root, request->output,
                                                       &output_parent)) {
        (void)fprintf(stderr, "cupidbuild: %s\n",
                      cupidbuild_host_output_parent_error(output_parent));
        goto done;
    }
    if (!(user ? cupidbuild_host_output_transaction_open(request->repository_root, request->source,
                                                         request->output, output_parent,
                                                         &transaction)
               : cupidbuild_host_transaction_open(request->repository_root, request->source,
                                                  request->output, &transaction))) {
        goto host_failure;
    }
    if (!(doom != 0 ? cupidbuild_compile_doom_bundle(transaction, request->repository_root,
                                                     request->source, &bundle_snapshot,
                                                     seed_release != (const char *)0 ? 8u : 7u)
                    : cupidbuild_compile_bundle(transaction, request->repository_root, closure,
                                                &bundle_snapshot,
                                                seed_release != (const char *)0 ? 8u : 7u))) {
        (void)fprintf(stderr, "cupidbuild: frozen compiler closure cannot be captured\n");
        goto host_failure;
    }
    if (!cupidbuild_seed_freeze(transaction, request->repository_root, request->seed_manifest, 1, 1,
                                seed_release, &seed)) {
        goto done;
    }
    candidate_path = cupidbuild_host_candidate(transaction);
    separator = strrchr(candidate_path, '/');
    if (separator == (const char *)0 || separator == candidate_path ||
        (size_t)(separator - candidate_path) >= sizeof(compiler_root) ||
        strlen(separator) >= sizeof(logical_output)) {
        goto host_failure;
    }
    (void)memcpy(compiler_root, candidate_path, (size_t)(separator - candidate_path));
    compiler_root[separator - candidate_path] = '\0';
    (void)memcpy(logical_output, separator, strlen(separator) + 1u);
    arguments[count++] = "-c";
    arguments[count++] = logical_source;
    arguments[count++] = "-o";
    arguments[count++] = logical_output;
    if (profile_count + 9u +
            (doom_tree ? sizeof(cupidbuild_compile_doom_tree_extra) /
                             sizeof(cupidbuild_compile_doom_tree_extra[0])
                       : 0u) >
        sizeof(arguments) / sizeof(arguments[0])) {
        goto host_failure;
    }
    for (index = 0u; index < profile_count; index++) {
        arguments[count++] = profile[index];
    }
    if (doom_tree) {
        for (index = 0u; index < sizeof(cupidbuild_compile_doom_tree_extra) /
                                     sizeof(cupidbuild_compile_doom_tree_extra[0]);
             index++) {
            arguments[count++] = cupidbuild_compile_doom_tree_extra[index];
        }
    }
    arguments[count++] = "--root";
    arguments[count++] = compiler_root;
    arguments[count++] = "--source-bundle";
    arguments[count++] = cupidbuild_host_private_output(transaction);
    arguments[count] = (const char *)0;
    if (kind != CUPIDBUILD_COMPILE_KERNEL &&
        !cupidbuild_host_require_publication_boundary(transaction)) {
        goto host_failure;
    }
    status = cupidbuild_host_run(
        transaction, seed.frozen_tools[1], arguments,
        strcmp(request->source, "kernel/cpu/ksyms_data.cc") == 0 ? 600000u : 180000u);
    if (!cupidbuild_seed_require_live(transaction, &seed)) {
        goto done;
    }
    if (!cupidbuild_host_require_private_output(transaction, &bundle_snapshot)) {
        goto host_failure;
    }
    if (status != 0) {
        (void)fprintf(stderr, "cupidbuild: checked CupidC failed\n");
        goto done;
    }
    if (!cupidbuild_host_capture_candidate(transaction, &candidate_snapshot, &candidate)) {
        goto host_failure;
    }
    if (!cupidbuild_validate_compiler_object_bytes(candidate, (size_t)candidate_snapshot.size)) {
        (void)fprintf(stderr, "cupidbuild: compiler object validation failed\n");
        goto done;
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
    free(candidate);
    cupidbuild_seed_capture_close(&seed);
    result = cupidbuild_finish_publication(
        transaction, result,
        doom ? "Doom compiler object"
             : (user ? "user compiler object"
                     : (production ? "production compiler object" : "kernel compiler object")));
    if (!cupidbuild_host_output_parent_close(output_parent)) {
        (void)fprintf(stderr, "cupidbuild: output parent cleanup failed\n");
        return 1;
    }
    return result;
}

int cupidbuild_compile_kernel(const cupidbuild_compile_request_t *request) {
    return cupidbuild_compile_kernel_with_release(request, (const char *)0);
}

int cupidbuild_compile_kernel_with_release(const cupidbuild_compile_request_t *request,
                                           const char *seed_release) {
    return cupidbuild_compile(request, CUPIDBUILD_COMPILE_KERNEL, seed_release);
}

int cupidbuild_compile_doom(const cupidbuild_compile_request_t *request) {
    return cupidbuild_compile_doom_with_release(request, (const char *)0);
}

int cupidbuild_compile_doom_with_release(const cupidbuild_compile_request_t *request,
                                         const char *seed_release) {
    return cupidbuild_compile(request, CUPIDBUILD_COMPILE_DOOM, seed_release);
}

int cupidbuild_compile_production(const cupidbuild_compile_request_t *request) {
    return cupidbuild_compile_production_with_release(request, (const char *)0);
}

int cupidbuild_compile_production_with_release(const cupidbuild_compile_request_t *request,
                                               const char *seed_release) {
    return cupidbuild_compile(request, CUPIDBUILD_COMPILE_PRODUCTION, seed_release);
}

int cupidbuild_compile_user(const cupidbuild_compile_request_t *request) {
    return cupidbuild_compile_user_with_release(request, (const char *)0);
}

int cupidbuild_compile_user_with_release(const cupidbuild_compile_request_t *request,
                                         const char *seed_release) {
    cupidbuild_user_compile_paths_t *paths =
        (cupidbuild_user_compile_paths_t *)calloc(1u, sizeof(*paths));
    char absolute_root[CUPIDBUILD_PATH_BYTES];
    char error[256];
    cupidbuild_compile_request_t normalized;
    int result = 1;
    if (paths == (cupidbuild_user_compile_paths_t *)0 ||
        request == (const cupidbuild_compile_request_t *)0 ||
        !cupidbuild_path_safe(request->seed_manifest, 0) ||
        !cupidbuild_host_absolute_root(request->repository_root, absolute_root,
                                       sizeof(absolute_root))) {
        (void)fprintf(stderr, "cupidbuild: invalid user compile request\n");
        goto done;
    }
    if (!cupidbuild_resolve_user_compile_paths(absolute_root, request->source, request->output,
                                               cupidbuild_host_execution_format() == 2u, paths,
                                               error, sizeof(error))) {
        (void)fprintf(stderr, "cupidbuild: %s\n", error);
        goto done;
    }
    normalized.seed_manifest = request->seed_manifest;
    normalized.repository_root = paths->repository_root;
    normalized.source = paths->source;
    normalized.output = paths->output;
    result = cupidbuild_compile(&normalized, CUPIDBUILD_COMPILE_USER, seed_release);
done:
    free(paths);
    return result;
}
