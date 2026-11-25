/**
 * @file cupidasm_publication_contract.inc
 * @brief Check publication and recovery of assembly artifacts.
 */

#define FAKE_FILE_COUNT 20u
#define FAKE_FILE_BYTES 4096u

typedef struct {
    const char *path;
    ctool_u8 bytes[FAKE_FILE_BYTES];
    ctool_u32 size;
    ctool_bool exists;
} fake_file_t;

typedef struct {
    fake_file_t files[FAKE_FILE_COUNT];
    ctool_u32 commit_inspect_count;
    ctool_u32 fail_commit_inspect_at;
    ctool_u32 write_count;
    ctool_u32 fail_write_at;
    ctool_u32 write_then_fail_at;
    ctool_u32 write_prefix_then_fail_at;
    ctool_u32 replace_count;
    ctool_u32 fail_replace_at;
    ctool_u32 fail_replace_again_at;
    ctool_u32 remove_count;
    ctool_u32 fail_remove_at;
} fake_publication_t;

static fake_file_t *fake_find(fake_publication_t *store, ctool_string_t path) {
    ctool_u32 index;
    for (index = 0u; index < FAKE_FILE_COUNT; index++) {
        const char *name = store->files[index].path;
        if (name != (const char *)0 && strlen(name) == path.size &&
            memcmp(name, path.data, path.size) == 0) {
            return &store->files[index];
        }
    }
    return (fake_file_t *)0;
}

static ctool_status_t fake_inspect(void *context, ctool_string_t path, ctool_bool *exists_out) {
    fake_publication_t *store = (fake_publication_t *)context;
    fake_file_t *file = fake_find(store, path);
    if (file == (fake_file_t *)0 || exists_out == (ctool_bool *)0) {
        return CTOOL_ERR_INVALID_ARGUMENT;
    }
    if (file == &store->files[6] || file == &store->files[7] || file == &store->files[8]) {
        store->commit_inspect_count++;
        if (store->commit_inspect_count == store->fail_commit_inspect_at) {
            return CTOOL_ERR_IO;
        }
    }
    *exists_out = file->exists;
    return CTOOL_OK;
}

static ctool_status_t fake_read(void *context, ctool_string_t path, ctool_mut_bytes_t destination,
                                ctool_u32 *size_out) {
    fake_file_t *file = fake_find((fake_publication_t *)context, path);
    if (file == (fake_file_t *)0 || file->exists == CTOOL_FALSE ||
        destination.data == (ctool_u8 *)0 || size_out == (ctool_u32 *)0 ||
        file->size > destination.size) {
        return CTOOL_ERR_IO;
    }
    if (file->size != 0u) {
        (void)memcpy(destination.data, file->bytes, file->size);
    }
    *size_out = file->size;
    return CTOOL_OK;
}

static ctool_status_t fake_write_new(void *context, ctool_string_t path, ctool_bytes_t contents) {
    fake_publication_t *store = (fake_publication_t *)context;
    fake_file_t *file = fake_find(store, path);
    store->write_count++;
    if (store->write_count == store->fail_write_at)
        return CTOOL_ERR_IO;
    if (file == (fake_file_t *)0 || file->exists == CTOOL_TRUE || contents.size > FAKE_FILE_BYTES) {
        return CTOOL_ERR_IO;
    }
    if (store->write_count == store->write_prefix_then_fail_at) {
        ctool_u32 prefix = contents.size / 2u;
        if (prefix != 0u)
            (void)memcpy(file->bytes, contents.data, prefix);
        file->size = prefix;
        file->exists = CTOOL_TRUE;
        return CTOOL_ERR_IO;
    }
    if (contents.size != 0u) {
        (void)memcpy(file->bytes, contents.data, contents.size);
    }
    file->size = contents.size;
    file->exists = CTOOL_TRUE;
    if (store->write_count == store->write_then_fail_at)
        return CTOOL_ERR_IO;
    return CTOOL_OK;
}

static ctool_status_t fake_replace(void *context, ctool_string_t from, ctool_string_t to) {
    fake_publication_t *store = (fake_publication_t *)context;
    fake_file_t *source = fake_find(store, from);
    fake_file_t *destination = fake_find(store, to);
    store->replace_count++;
    if (store->replace_count == store->fail_replace_at ||
        store->replace_count == store->fail_replace_again_at) {
        return CTOOL_ERR_IO;
    }
    if (source == (fake_file_t *)0 || destination == (fake_file_t *)0 ||
        source->exists == CTOOL_FALSE) {
        return CTOOL_ERR_IO;
    }
    (void)memcpy(destination->bytes, source->bytes, source->size);
    destination->size = source->size;
    destination->exists = CTOOL_TRUE;
    source->size = 0u;
    source->exists = CTOOL_FALSE;
    return CTOOL_OK;
}

static ctool_status_t fake_remove(void *context, ctool_string_t path) {
    fake_publication_t *store = (fake_publication_t *)context;
    fake_file_t *file = fake_find(store, path);
    store->remove_count++;
    if (store->remove_count == store->fail_remove_at)
        return CTOOL_ERR_IO;
    if (file == (fake_file_t *)0)
        return CTOOL_ERR_INVALID_ARGUMENT;
    file->size = 0u;
    file->exists = CTOOL_FALSE;
    return CTOOL_OK;
}

static void fake_reset(fake_publication_t *store) {
    static const char *paths[FAKE_FILE_COUNT] = {"/out",
                                                 "/out.cupid-as-new",
                                                 "/out.cupid-as-old",
                                                 "/map",
                                                 "/map.cupid-as-new",
                                                 "/map.cupid-as-old",
                                                 "/map.cupid-as-done",
                                                 "/map2.cupid-as-done",
                                                 "/out.cupid-as-done",
                                                 "/map2",
                                                 "/map2.cupid-as-new",
                                                 "/map2.cupid-as-old",
                                                 "/out2",
                                                 "/out2.cupid-as-new",
                                                 "/out2.cupid-as-old",
                                                 "/out2.cupid-as-done",
                                                 "/out.cupid-as-absent",
                                                 "/map.cupid-as-absent",
                                                 "/map2.cupid-as-absent",
                                                 "/out2.cupid-as-absent"};
    static const char old_output[] = "old-output";
    static const char old_map[] = "old-map";
    ctool_u32 index;
    (void)memset(store, 0, sizeof(*store));
    for (index = 0u; index < FAKE_FILE_COUNT; index++) {
        store->files[index].path = paths[index];
    }
    (void)memcpy(store->files[0].bytes, old_output, sizeof(old_output) - 1u);
    store->files[0].size = (ctool_u32)(sizeof(old_output) - 1u);
    store->files[0].exists = CTOOL_TRUE;
    (void)memcpy(store->files[3].bytes, old_map, sizeof(old_map) - 1u);
    store->files[3].size = (ctool_u32)(sizeof(old_map) - 1u);
    store->files[3].exists = CTOOL_TRUE;
}

static int fake_has(fake_publication_t *store, ctool_u32 index, const char *text) {
    ctool_u32 size = (ctool_u32)strlen(text);
    return store->files[index].exists == CTOOL_TRUE && store->files[index].size == size &&
           memcmp(store->files[index].bytes, text, size) == 0;
}

static void fake_put(fake_publication_t *store, ctool_u32 index, const char *text) {
    ctool_u32 size = (ctool_u32)strlen(text);
    (void)memcpy(store->files[index].bytes, text, size);
    store->files[index].size = size;
    store->files[index].exists = CTOOL_TRUE;
}

static int fake_pair_private_clear(fake_publication_t *store) {
    static const ctool_u32 private_indexes[] = {1u, 2u, 4u, 5u, 6u, 8u, 16u, 17u};
    ctool_u32 index;
    for (index = 0u; index < (ctool_u32)(sizeof(private_indexes) / sizeof(private_indexes[0]));
         index++) {
        if (store->files[private_indexes[index]].exists == CTOOL_TRUE)
            return 0;
    }
    return 1;
}

static void fake_seed_commit_record(fake_publication_t *store, const ctool_u8 *backup,
                                    ctool_u32 backup_size) {
    static const ctool_u8 magic[8] = {'C', 'U', 'P', 'I', 'D', 'A', 'S', 1u};
    static const char commit[] = "/out.cupid-as-done";
    fake_file_t *file = &store->files[8];
    ctool_u32 cursor = 0u;
    ctool_u32 index;
    for (index = 0u; index < 8u; index++)
        file->bytes[cursor++] = magic[index];
    file->bytes[cursor++] = 1u;
    file->bytes[cursor++] = 0u;
    file->bytes[cursor++] = 0u;
    file->bytes[cursor++] = 0u;
    file->bytes[cursor++] = (ctool_u8)backup_size;
    file->bytes[cursor++] = 0u;
    file->bytes[cursor++] = 0u;
    file->bytes[cursor++] = 0u;
    for (index = 0u; index < backup_size; index++) {
        file->bytes[cursor++] = backup[index];
    }
    file->bytes[cursor++] = 0u;
    file->bytes[cursor++] = (ctool_u8)(sizeof(commit) - 1u);
    file->bytes[cursor++] = 0u;
    file->bytes[cursor++] = 0u;
    file->bytes[cursor++] = 0u;
    for (index = 0u; index < (ctool_u32)(sizeof(commit) - 1u); index++) {
        file->bytes[cursor++] = (ctool_u8)commit[index];
    }
    file->bytes[cursor++] = 0u;
    file->size = cursor;
    file->exists = CTOOL_TRUE;
}

static void fake_seed_linked_pair_record(fake_publication_t *store, ctool_u32 file_index,
                                         ctool_u8 version) {
    static const ctool_u8 magic[7] = {'C', 'U', 'P', 'I', 'D', 'A', 'S'};
    static const char *backups[2] = {"/out.cupid-as-old", "/map.cupid-as-old"};
    static const char *commits[2] = {"/out.cupid-as-done", "/map.cupid-as-done"};
    fake_file_t *file = &store->files[file_index];
    ctool_u32 cursor = 0u;
    ctool_u32 index;
    for (index = 0u; index < 7u; index++)
        file->bytes[cursor++] = magic[index];
    file->bytes[cursor++] = version;
    file->bytes[cursor++] = 2u;
    file->bytes[cursor++] = 0u;
    file->bytes[cursor++] = 0u;
    file->bytes[cursor++] = 0u;
    for (index = 0u; index < 2u; index++) {
        const char *fields[2] = {backups[index], commits[index]};
        ctool_u32 field;
        for (field = 0u; field < 2u; field++) {
            ctool_u32 size = (ctool_u32)strlen(fields[field]);
            ctool_u32 character;
            file->bytes[cursor++] = (ctool_u8)size;
            file->bytes[cursor++] = 0u;
            file->bytes[cursor++] = 0u;
            file->bytes[cursor++] = 0u;
            for (character = 0u; character < size; character++) {
                file->bytes[cursor++] = (ctool_u8)fields[field][character];
            }
            file->bytes[cursor++] = 0u;
        }
    }
    file->size = cursor;
    file->exists = CTOOL_TRUE;
}

static void fake_seed_absence_tombstone(fake_publication_t *store, ctool_u32 file_index) {
    static const ctool_u8 absent[] = {'C', 'U', 'P', 'I', 'D', '-', 'A', 'S',
                                      '-', 'A', 'B', 'S', 'E', 'N', 'T', 1u};
    fake_file_t *file = &store->files[file_index];
    (void)memcpy(file->bytes, absent, sizeof(absent));
    file->size = (ctool_u32)sizeof(absent);
    file->exists = CTOOL_TRUE;
}

static void fake_make_long_publication_paths(char *target, char *candidate, char *backup,
                                             char *absent, char *commit, char fill) {
    static const char candidate_suffix[] = ".cupid-as-new";
    static const char backup_suffix[] = ".cupid-as-old";
    static const char absent_suffix[] = ".cupid-as-absent";
    static const char commit_suffix[] = ".cupid-as-done";
    ctool_u32 index;
    target[0] = '/';
    for (index = 1u; index < 495u; index++)
        target[index] = fill;
    target[495] = '\0';
    for (index = 0u; index < 495u; index++) {
        candidate[index] = target[index];
        backup[index] = target[index];
        absent[index] = target[index];
        commit[index] = target[index];
    }
    for (index = 0u; index < (ctool_u32)sizeof(candidate_suffix); index++) {
        candidate[495u + index] = candidate_suffix[index];
    }
    for (index = 0u; index < (ctool_u32)sizeof(backup_suffix); index++) {
        backup[495u + index] = backup_suffix[index];
    }
    for (index = 0u; index < (ctool_u32)sizeof(absent_suffix); index++) {
        absent[495u + index] = absent_suffix[index];
    }
    for (index = 0u; index < (ctool_u32)sizeof(commit_suffix); index++) {
        commit[495u + index] = commit_suffix[index];
    }
}

static int run_artifact_publication(void) {
    static const ctool_u8 output_bytes[] = "new-output";
    static const ctool_u8 map_bytes[] = "new-map";
    static char long_output[512];
    static char long_output_candidate[512];
    static char long_output_backup[512];
    static char long_output_absent[512];
    static char long_output_commit[512];
    static char long_map[512];
    static char long_map_candidate[512];
    static char long_map_backup[512];
    static char long_map_absent[512];
    static char long_map_commit[512];
    ctool_u8 publication_scratch[512u * 4u + 32u];
    ctool_u8 publication_peer_scratch[512u * 4u + 32u];
    fake_publication_t store;
    as_artifact_publication_ops_t ops;
    as_artifact_publication_request_t request;
    ctool_status_t status;
    ctool_u32 index;

    (void)memset(&ops, 0, sizeof(ops));
    ops.context = &store;
    ops.inspect = fake_inspect;
    ops.read = fake_read;
    ops.write_new = fake_write_new;
    ops.replace = fake_replace;
    ops.remove = fake_remove;
    (void)memset(&request, 0, sizeof(request));
    request.artifact.target = ctool_string("/out");
    request.artifact.candidate = ctool_string("/out.cupid-as-new");
    request.artifact.backup = ctool_string("/out.cupid-as-old");
    request.artifact.absent = ctool_string("/out.cupid-as-absent");
    request.artifact.commit = ctool_string("/out.cupid-as-done");
    request.map.target = ctool_string("/map");
    request.map.candidate = ctool_string("/map.cupid-as-new");
    request.map.backup = ctool_string("/map.cupid-as-old");
    request.map.absent = ctool_string("/map.cupid-as-absent");
    request.map.commit = ctool_string("/map.cupid-as-done");
    request.scratch.data = publication_scratch;
    request.scratch.size = (ctool_u32)sizeof(publication_scratch);
    request.peer_scratch.data = publication_peer_scratch;
    request.peer_scratch.size = (ctool_u32)sizeof(publication_peer_scratch);
    request.artifact_bytes = ctool_bytes(output_bytes, (ctool_u32)(sizeof(output_bytes) - 1u));
    request.map_bytes = ctool_bytes(map_bytes, (ctool_u32)(sizeof(map_bytes) - 1u));

    {
        as_artifact_publication_request_t invalid = request;
        invalid.map.candidate = invalid.artifact.target;
        if (as_artifact_publish(&ops, &invalid) != CTOOL_ERR_INVALID_ARGUMENT) {
            (void)fprintf(stderr, "overlapping publication paths were accepted\n");
            return 1;
        }
        invalid = request;
        invalid.artifact.commit = ctool_string("/other.cupid-as-done");
        if (as_artifact_publish(&ops, &invalid) != CTOOL_ERR_INVALID_ARGUMENT) {
            (void)fprintf(stderr, "mismatched recovery paths were accepted\n");
            return 1;
        }
        invalid = request;
        invalid.artifact.absent = ctool_string("/other.cupid-as-absent");
        if (as_artifact_publish(&ops, &invalid) != CTOOL_ERR_INVALID_ARGUMENT) {
            (void)fprintf(stderr, "mismatched tombstone path was accepted\n");
            return 1;
        }
        fake_reset(&store);
        invalid = request;
        invalid.artifact.target = ctool_string("/out2");
        if (as_artifact_publish(&ops, &invalid) != CTOOL_ERR_INVALID_ARGUMENT) {
            (void)fprintf(stderr, "recovery paths for another target were accepted\n");
            return 1;
        }
        fake_reset(&store);
        invalid = request;
        invalid.artifact.candidate = ctool_string("/out2.cupid-as-new");
        if (as_artifact_publish(&ops, &invalid) != CTOOL_ERR_INVALID_ARGUMENT) {
            (void)fprintf(stderr, "candidate path for another target was accepted\n");
            return 1;
        }
        invalid = request;
        invalid.peer_scratch = invalid.scratch;
        if (as_artifact_publish(&ops, &invalid) != CTOOL_ERR_INVALID_ARGUMENT) {
            (void)fprintf(stderr, "overlapping recovery scratch was accepted\n");
            return 1;
        }
        invalid = request;
        invalid.peer_scratch.data = publication_scratch + 1u;
        invalid.peer_scratch.size = (ctool_u32)sizeof(publication_scratch) - 1u;
        if (as_artifact_publish(&ops, &invalid) != CTOOL_ERR_INVALID_ARGUMENT) {
            (void)fprintf(stderr, "right-overlapping recovery scratch was accepted\n");
            return 1;
        }
        invalid = request;
        invalid.scratch.data = publication_peer_scratch + 1u;
        invalid.scratch.size = (ctool_u32)sizeof(publication_peer_scratch) - 1u;
        if (as_artifact_publish(&ops, &invalid) != CTOOL_ERR_INVALID_ARGUMENT) {
            (void)fprintf(stderr, "left-overlapping recovery scratch was accepted\n");
            return 1;
        }
    }

    {
        static const ctool_u8 embedded_backup[] = {'/', 'o', 'u', 't', '\0', '.', 'c', 'u', 'p',
                                                   'i', 'd', '-', 'a', 's',  '-', 'o', 'l', 'd'};
        fake_reset(&store);
        fake_seed_commit_record(&store, embedded_backup, (ctool_u32)sizeof(embedded_backup));
        status = as_artifact_publish(&ops, &request);
        if (status != CTOOL_ERR_INPUT || !fake_has(&store, 0u, "old-output") ||
            !fake_has(&store, 3u, "old-map") || store.remove_count != 0u) {
            (void)fprintf(stderr, "unsafe recovery record was accepted\n");
            return 1;
        }
    }

    fake_reset(&store);
    fake_put(&store, 0u, "committed-output");
    fake_put(&store, 3u, "committed-map");
    fake_put(&store, 2u, "old-output");
    fake_put(&store, 5u, "old-map");
    fake_seed_linked_pair_record(&store, 8u, 3u);
    fake_seed_linked_pair_record(&store, 6u, 2u);
    request.artifact.target = ctool_string("/out2");
    request.artifact.candidate = ctool_string("/out2.cupid-as-new");
    request.artifact.backup = ctool_string("/out2.cupid-as-old");
    request.artifact.absent = ctool_string("/out2.cupid-as-absent");
    request.artifact.commit = ctool_string("/out2.cupid-as-done");
    status = as_artifact_publish(&ops, &request);
    if (status != CTOOL_ERR_IO || store.files[12].exists == CTOOL_TRUE ||
        !fake_has(&store, 3u, "committed-map") || store.files[5].exists == CTOOL_TRUE ||
        store.files[6].exists == CTOOL_FALSE || store.files[6].bytes[7] != 3u ||
        store.files[8].exists == CTOOL_FALSE) {
        (void)fprintf(stderr, "overlapping committed pair was not retained\n");
        return 1;
    }
    request.artifact.target = ctool_string("/out");
    request.artifact.candidate = ctool_string("/out.cupid-as-new");
    request.artifact.backup = ctool_string("/out.cupid-as-old");
    request.artifact.absent = ctool_string("/out.cupid-as-absent");
    request.artifact.commit = ctool_string("/out.cupid-as-done");

    fake_reset(&store);
    (void)memcpy(store.files[0].bytes, "interrupted-output", 18u);
    store.files[0].size = 18u;
    store.files[0].exists = CTOOL_TRUE;
    store.files[3].size = 0u;
    store.files[3].exists = CTOOL_FALSE;
    fake_seed_absence_tombstone(&store, 16u);
    fake_seed_absence_tombstone(&store, 17u);
    fake_seed_linked_pair_record(&store, 8u, 2u);
    fake_seed_linked_pair_record(&store, 6u, 2u);
    store.fail_write_at = 1u;
    status = as_artifact_publish(&ops, &request);
    if (status != CTOOL_ERR_IO || store.files[0].exists == CTOOL_TRUE ||
        store.files[3].exists == CTOOL_TRUE || store.files[6].exists == CTOOL_TRUE ||
        store.files[8].exists == CTOOL_TRUE || store.files[16].exists == CTOOL_TRUE ||
        store.files[17].exists == CTOOL_TRUE) {
        (void)fprintf(stderr, "interrupted absent-pair publication recovery differs\n");
        return 1;
    }

    fake_make_long_publication_paths(long_output, long_output_candidate, long_output_backup,
                                     long_output_absent, long_output_commit, 'a');
    fake_make_long_publication_paths(long_map, long_map_candidate, long_map_backup, long_map_absent,
                                     long_map_commit, 'b');
    fake_reset(&store);
    store.files[0].path = long_output;
    store.files[1].path = long_output_candidate;
    store.files[2].path = long_output_backup;
    store.files[8].path = long_output_commit;
    store.files[16].path = long_output_absent;
    store.files[3].path = long_map;
    store.files[4].path = long_map_candidate;
    store.files[5].path = long_map_backup;
    store.files[6].path = long_map_commit;
    store.files[17].path = long_map_absent;
    request.artifact.target = ctool_string(long_output);
    request.artifact.candidate = ctool_string(long_output_candidate);
    request.artifact.backup = ctool_string(long_output_backup);
    request.artifact.absent = ctool_string(long_output_absent);
    request.artifact.commit = ctool_string(long_output_commit);
    request.map.target = ctool_string(long_map);
    request.map.candidate = ctool_string(long_map_candidate);
    request.map.backup = ctool_string(long_map_backup);
    request.map.absent = ctool_string(long_map_absent);
    request.map.commit = ctool_string(long_map_commit);
    status = as_artifact_publish(&ops, &request);
    if (status != CTOOL_OK || !fake_has(&store, 0u, "new-output") ||
        !fake_has(&store, 3u, "new-map") || store.files[2].exists == CTOOL_TRUE ||
        store.files[5].exists == CTOOL_TRUE || store.files[6].exists == CTOOL_TRUE ||
        store.files[8].exists == CTOOL_TRUE) {
        (void)fprintf(stderr, "near-limit recovery record differs\n");
        return 1;
    }
    request.artifact.target = ctool_string("/out");
    request.artifact.candidate = ctool_string("/out.cupid-as-new");
    request.artifact.backup = ctool_string("/out.cupid-as-old");
    request.artifact.absent = ctool_string("/out.cupid-as-absent");
    request.artifact.commit = ctool_string("/out.cupid-as-done");
    request.map.target = ctool_string("/map");
    request.map.candidate = ctool_string("/map.cupid-as-new");
    request.map.backup = ctool_string("/map.cupid-as-old");
    request.map.absent = ctool_string("/map.cupid-as-absent");
    request.map.commit = ctool_string("/map.cupid-as-done");

    for (index = 1u; index <= 4u; index++) {
        fake_reset(&store);
        store.fail_write_at = index;
        status = as_artifact_publish(&ops, &request);
        if (status != CTOOL_ERR_IO || !fake_has(&store, 0u, "old-output") ||
            !fake_has(&store, 3u, "old-map") || fake_pair_private_clear(&store) == 0) {
            (void)fprintf(stderr, "pre-move publication write failure %u differs\n",
                          (unsigned int)index);
            return 1;
        }
    }

    for (index = 1u; index <= 4u; index++) {
        fake_reset(&store);
        store.fail_replace_at = index;
        status = as_artifact_publish(&ops, &request);
        if (status != CTOOL_ERR_IO || !fake_has(&store, 0u, "old-output") ||
            !fake_has(&store, 3u, "old-map") || fake_pair_private_clear(&store) == 0) {
            (void)fprintf(stderr, "publication move failure %u differs\n", (unsigned int)index);
            return 1;
        }
    }

    fake_reset(&store);
    store.fail_write_at = 5u;
    status = as_artifact_publish(&ops, &request);
    if (status != CTOOL_ERR_IO || !fake_has(&store, 0u, "old-output") ||
        !fake_has(&store, 3u, "old-map") || fake_pair_private_clear(&store) == 0) {
        (void)fprintf(stderr, "pre-witness rollback differs\n");
        return 1;
    }

    fake_reset(&store);
    store.fail_remove_at = 1u;
    status = as_artifact_publish(&ops, &request);
    if (status != CTOOL_ERR_IO || !fake_has(&store, 0u, "old-output") ||
        !fake_has(&store, 3u, "old-map") || fake_pair_private_clear(&store) == 0) {
        (void)fprintf(stderr, "pending-marker replacement rollback differs\n");
        return 1;
    }

    fake_reset(&store);
    store.write_prefix_then_fail_at = 5u;
    status = as_artifact_publish(&ops, &request);
    if (status != CTOOL_ERR_IO || !fake_has(&store, 0u, "old-output") ||
        !fake_has(&store, 3u, "old-map") || fake_pair_private_clear(&store) == 0) {
        (void)fprintf(stderr, "corrupt pre-witness rollback differs\n");
        return 1;
    }

    fake_reset(&store);
    store.write_then_fail_at = 5u;
    status = as_artifact_publish(&ops, &request);
    if (status != CTOOL_OK || !fake_has(&store, 0u, "new-output") ||
        !fake_has(&store, 3u, "new-map") || fake_pair_private_clear(&store) == 0) {
        (void)fprintf(stderr, "ambiguous v3 witness differs\n");
        return 1;
    }

    fake_reset(&store);
    store.fail_write_at = 6u;
    status = as_artifact_publish(&ops, &request);
    if (status != CTOOL_OK || !fake_has(&store, 0u, "new-output") ||
        !fake_has(&store, 3u, "new-map") || fake_pair_private_clear(&store) == 0) {
        (void)fprintf(stderr, "post-witness completion differs\n");
        return 1;
    }

    fake_reset(&store);
    status = as_artifact_publish(&ops, &request);
    if (status != CTOOL_OK || !fake_has(&store, 0u, "new-output") ||
        !fake_has(&store, 3u, "new-map") || fake_pair_private_clear(&store) == 0) {
        (void)fprintf(stderr, "pair publication differs\n");
        return 1;
    }
    status = as_artifact_publish(&ops, &request);
    if (status != CTOOL_OK || !fake_has(&store, 0u, "new-output") ||
        !fake_has(&store, 3u, "new-map") || fake_pair_private_clear(&store) == 0) {
        (void)fprintf(stderr, "repeated pair publication differs\n");
        return 1;
    }

    fake_reset(&store);
    fake_put(&store, 2u, "old-output");
    fake_put(&store, 5u, "old-map");
    fake_put(&store, 0u, "interrupted-output");
    fake_put(&store, 3u, "interrupted-map");
    fake_seed_linked_pair_record(&store, 8u, 2u);
    fake_seed_linked_pair_record(&store, 6u, 2u);
    store.fail_replace_at = 1u;
    status = as_artifact_publish(&ops, &request);
    if (status != CTOOL_ERR_IO || !fake_has(&store, 0u, "interrupted-output") ||
        !fake_has(&store, 2u, "old-output") || !fake_has(&store, 3u, "interrupted-map") ||
        !fake_has(&store, 5u, "old-map")) {
        (void)fprintf(stderr, "failed recovery replaced a readable public target\n");
        return 1;
    }
    store.fail_replace_at = 0u;
    store.fail_write_at = 1u;
    store.write_count = 0u;
    store.replace_count = 0u;
    status = as_artifact_publish(&ops, &request);
    if (status != CTOOL_ERR_IO || !fake_has(&store, 0u, "old-output") ||
        !fake_has(&store, 3u, "old-map") || fake_pair_private_clear(&store) == 0) {
        (void)fprintf(stderr, "old-pair pending recovery differs\n");
        return 1;
    }

    for (index = 0u; index < 2u; index++) {
        fake_reset(&store);
        fake_put(&store, 0u, "interrupted-output");
        fake_put(&store, 3u, "interrupted-map");
        if (index == 0u) {
            fake_put(&store, 2u, "old-output");
            fake_seed_absence_tombstone(&store, 17u);
        } else {
            fake_seed_absence_tombstone(&store, 16u);
            fake_put(&store, 5u, "old-map");
        }
        fake_seed_linked_pair_record(&store, 8u, 2u);
        fake_seed_linked_pair_record(&store, 6u, 2u);
        store.fail_write_at = 1u;
        status = as_artifact_publish(&ops, &request);
        if (status != CTOOL_ERR_IO ||
            (index == 0u &&
             (!fake_has(&store, 0u, "old-output") || store.files[3].exists == CTOOL_TRUE)) ||
            (index == 1u &&
             (store.files[0].exists == CTOOL_TRUE || !fake_has(&store, 3u, "old-map"))) ||
            fake_pair_private_clear(&store) == 0) {
            (void)fprintf(stderr, "mixed pending recovery %u differs\n", (unsigned int)index);
            return 1;
        }
    }

    for (index = 0u; index < 2u; index++) {
        fake_reset(&store);
        fake_put(&store, 2u, "old-output");
        fake_put(&store, 5u, "old-map");
        fake_put(&store, 0u, "interrupted-output");
        fake_put(&store, 3u, "interrupted-map");
        fake_seed_linked_pair_record(&store, 8u, index == 0u ? 2u : 1u);
        fake_seed_linked_pair_record(&store, 6u, index == 0u ? 1u : 2u);
        store.fail_write_at = 1u;
        status = as_artifact_publish(&ops, &request);
        if (status != CTOOL_ERR_IO || !fake_has(&store, 0u, "old-output") ||
            !fake_has(&store, 3u, "old-map") || fake_pair_private_clear(&store) == 0) {
            (void)fprintf(stderr, "v2/v1 recovery order %u differs\n", (unsigned int)index);
            return 1;
        }
    }

    for (index = 0u; index < 2u; index++) {
        fake_reset(&store);
        fake_put(&store, 0u, "committed-output");
        fake_put(&store, 3u, "committed-map");
        fake_put(&store, 2u, "old-output");
        fake_put(&store, 5u, "old-map");
        fake_seed_linked_pair_record(&store, index == 0u ? 8u : 6u, 1u);
        store.fail_write_at = 1u;
        status = as_artifact_publish(&ops, &request);
        if (status != CTOOL_ERR_IO || !fake_has(&store, 0u, "committed-output") ||
            !fake_has(&store, 3u, "committed-map") || fake_pair_private_clear(&store) == 0) {
            (void)fprintf(stderr, "legacy v1 marker %u recovery differs\n", (unsigned int)index);
            return 1;
        }
    }

    for (index = 0u; index < 2u; index++) {
        fake_reset(&store);
        fake_put(&store, 2u, "old-output");
        fake_put(&store, 5u, "old-map");
        fake_put(&store, 0u, "committed-output");
        fake_put(&store, 3u, "committed-map");
        fake_seed_linked_pair_record(&store, index == 0u ? 8u : 6u, 3u);
        store.fail_write_at = 1u;
        status = as_artifact_publish(&ops, &request);
        if (status != CTOOL_ERR_IO || !fake_has(&store, 0u, "committed-output") ||
            !fake_has(&store, 3u, "committed-map") || fake_pair_private_clear(&store) == 0) {
            (void)fprintf(stderr, "surviving witness %u recovery differs\n", (unsigned int)index);
            return 1;
        }
    }

    fake_reset(&store);
    fake_put(&store, 2u, "old-output");
    fake_put(&store, 0u, "interrupted-output");
    fake_put(&store, 3u, "interrupted-map");
    fake_put(&store, 17u, "bad");
    fake_seed_linked_pair_record(&store, 8u, 2u);
    fake_seed_linked_pair_record(&store, 6u, 2u);
    status = as_artifact_publish(&ops, &request);
    if (status != CTOOL_ERR_INPUT || !fake_has(&store, 0u, "interrupted-output") ||
        !fake_has(&store, 2u, "old-output") || !fake_has(&store, 3u, "interrupted-map") ||
        !fake_has(&store, 17u, "bad")) {
        (void)fprintf(stderr, "corrupt absence tombstone was accepted\n");
        return 1;
    }

    fake_reset(&store);
    store.fail_remove_at = 6u;
    status = as_artifact_publish(&ops, &request);
    if (status != CTOOL_ERR_IO || !fake_has(&store, 0u, "new-output") ||
        !fake_has(&store, 3u, "new-map") || store.files[6].exists == CTOOL_TRUE ||
        store.files[8].exists == CTOOL_FALSE) {
        (void)fprintf(stderr, "witness-last cleanup differs\n");
        return 1;
    }
    store.fail_remove_at = 0u;
    store.fail_write_at = store.write_count + 1u;
    status = as_artifact_publish(&ops, &request);
    if (status != CTOOL_ERR_IO || !fake_has(&store, 0u, "new-output") ||
        !fake_has(&store, 3u, "new-map") || fake_pair_private_clear(&store) == 0) {
        (void)fprintf(stderr, "retained witness recovery differs\n");
        return 1;
    }

    fake_reset(&store);
    store.files[0].exists = CTOOL_FALSE;
    store.files[3].exists = CTOOL_FALSE;
    store.fail_remove_at = 3u;
    status = as_artifact_publish(&ops, &request);
    if (status != CTOOL_ERR_IO || !fake_has(&store, 0u, "new-output") ||
        !fake_has(&store, 3u, "new-map") || store.files[16].exists == CTOOL_FALSE ||
        store.files[6].exists == CTOOL_FALSE || store.files[8].exists == CTOOL_FALSE) {
        (void)fprintf(stderr, "deferred tombstone cleanup differs\n");
        return 1;
    }
    store.fail_remove_at = 0u;
    store.fail_write_at = store.write_count + 1u;
    status = as_artifact_publish(&ops, &request);
    if (status != CTOOL_ERR_IO || !fake_has(&store, 0u, "new-output") ||
        !fake_has(&store, 3u, "new-map") || fake_pair_private_clear(&store) == 0) {
        (void)fprintf(stderr, "retained tombstone recovery differs\n");
        return 1;
    }

    fake_reset(&store);
    store.files[0].exists = CTOOL_FALSE;
    store.files[3].exists = CTOOL_FALSE;
    for (index = 5u; index <= 6u; index++) {
        fake_reset(&store);
        store.files[0].exists = CTOOL_FALSE;
        store.files[3].exists = CTOOL_FALSE;
        store.fail_write_at = index;
        status = as_artifact_publish(&ops, &request);
        if (status != CTOOL_ERR_IO || store.files[0].exists == CTOOL_TRUE ||
            store.files[3].exists == CTOOL_TRUE || fake_pair_private_clear(&store) == 0) {
            (void)fprintf(stderr, "tombstone write failure %u differs\n", (unsigned int)index);
            return 1;
        }
    }

    for (index = 5u; index <= 6u; index++) {
        fake_reset(&store);
        store.files[0].exists = CTOOL_FALSE;
        store.files[3].exists = CTOOL_FALSE;
        store.write_then_fail_at = index;
        status = as_artifact_publish(&ops, &request);
        if (status != CTOOL_ERR_IO || store.files[0].exists == CTOOL_TRUE ||
            store.files[3].exists == CTOOL_TRUE) {
            (void)fprintf(stderr, "ambiguous tombstone write %u differs\n", (unsigned int)index);
            return 1;
        }
        store.write_then_fail_at = 0u;
        store.fail_write_at = store.write_count + 1u;
        status = as_artifact_publish(&ops, &request);
        if (status != CTOOL_ERR_IO || store.files[0].exists == CTOOL_TRUE ||
            store.files[3].exists == CTOOL_TRUE || fake_pair_private_clear(&store) == 0) {
            (void)fprintf(stderr, "ambiguous tombstone retry %u differs\n", (unsigned int)index);
            return 1;
        }
    }

    fake_reset(&store);
    store.files[0].exists = CTOOL_FALSE;
    store.files[3].exists = CTOOL_FALSE;
    status = as_artifact_publish(&ops, &request);
    if (status != CTOOL_OK || !fake_has(&store, 0u, "new-output") ||
        !fake_has(&store, 3u, "new-map") || fake_pair_private_clear(&store) == 0) {
        (void)fprintf(stderr, "absent-pair publication differs\n");
        return 1;
    }

    fake_reset(&store);
    (void)memset(&request.map, 0, sizeof(request.map));
    request.map_bytes = ctool_bytes((const void *)0, 0u);
    store.fail_replace_at = 2u;
    status = as_artifact_publish(&ops, &request);
    if (status != CTOOL_ERR_IO || !fake_has(&store, 0u, "old-output") ||
        !fake_has(&store, 3u, "old-map")) {
        (void)fprintf(stderr, "single artifact rollback differs\n");
        return 1;
    }
    store.fail_replace_at = 0u;
    store.write_count = 0u;
    store.replace_count = 0u;
    status = as_artifact_publish(&ops, &request);
    if (status != CTOOL_OK || !fake_has(&store, 0u, "new-output") ||
        !fake_has(&store, 3u, "old-map")) {
        (void)fprintf(stderr, "single artifact recovery differs\n");
        return 1;
    }

    (void)puts("artifact-publication: ok");
    return 0;
}
