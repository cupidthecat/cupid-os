/**
 * @file cupidbuild_host_input_snapshots.inc
 * @brief Freeze exact file inputs and retain their recorded snapshots.
 */

static void cupidbuild_host_release_input_paths(cupidbuild_host_transaction_t *transaction) {
    unsigned int index;
    for (index = 0u; index < transaction->input_count; index++)
        free(transaction->inputs[index].live_path);
    free(transaction->inputs);
}

int cupidbuild_host_reserve_inputs(cupidbuild_host_transaction_t *transaction, size_t capacity) {
    cupidbuild_host_input_t *grown;
    if (transaction == (cupidbuild_host_transaction_t *)0 || capacity > transaction->input_limit) {
        if (transaction != (cupidbuild_host_transaction_t *)0) {
            cupidbuild_host_set_error(transaction, "too many frozen transaction inputs");
        }
        return 0;
    }
    if (capacity <= transaction->input_capacity) {
        return 1;
    }
    grown = (cupidbuild_host_input_t *)realloc(transaction->inputs, capacity * sizeof(*grown));
    if (grown == (cupidbuild_host_input_t *)0) {
        cupidbuild_host_set_error(transaction, "transaction input table cannot be allocated");
        return 0;
    }
    (void)memset(grown + transaction->input_capacity, 0,
                 (capacity - transaction->input_capacity) * sizeof(*grown));
    transaction->inputs = grown;
    transaction->input_capacity = (unsigned int)capacity;
    return 1;
}

static int cupidbuild_host_repository_logical_path(const cupidbuild_host_transaction_t *transaction,
                                                   const char *path, const char **logical_out) {
    size_t root_size;
    size_t index;
    const char *logical;
    if (transaction == (const cupidbuild_host_transaction_t *)0 || path == (const char *)0 ||
        logical_out == (const char **)0) {
        return 0;
    }
    root_size = strlen(transaction->repository_root);
    if (root_size == 0u) {
        return 0;
    }
    for (index = 0u; index < root_size; index++) {
        char left = path[index];
        char right = transaction->repository_root[index];
#if defined(_WIN32)
        if (left == '\\') {
            left = '/';
        }
        if (right == '\\') {
            right = '/';
        }
        left = cupidbuild_host_ascii_fold(left);
        right = cupidbuild_host_ascii_fold(right);
#endif
        if (left == '\0' || left != right) {
            return 0;
        }
    }
    if (transaction->repository_root[root_size - 1u] == '/' ||
        transaction->repository_root[root_size - 1u] == '\\') {
        logical = path + root_size;
    } else if (path[root_size] == '/' || path[root_size] == '\\') {
        logical = path + root_size + 1u;
    } else {
        return 0;
    }
    if (!cupidbuild_host_path_is_relative_safe(logical)) {
        return 0;
    }
    *logical_out = logical;
    return 1;
}

static int cupidbuild_host_read_transaction_input(cupidbuild_host_transaction_t *transaction,
                                                  const char *path, unsigned long long limit,
                                                  cupidbuild_host_snapshot_t *snapshot,
                                                  unsigned char **bytes_out) {
    const char *logical;
    cupidbuild_host_snapshot_t root_snapshot;
    if (transaction == (cupidbuild_host_transaction_t *)0) {
        return 0;
    }
    if (transaction->runner_transaction != 0) {
        return limit == CUPIDBUILD_HOST_FILE_LIMIT
                   ? cupidbuild_host_read_regular(path, 0, snapshot, bytes_out)
                   : 0;
    }
    if (!cupidbuild_host_repository_logical_path(transaction, path, &logical)) {
        return 0;
    }
#if defined(_WIN32)
    if (!cupidbuild_host_windows_directory_handle_snapshot(transaction->repository_root_handle,
                                                           &root_snapshot) ||
        !cupidbuild_host_snapshot_identity_equal(&transaction->repository_root_snapshot,
                                                 &root_snapshot)) {
        return 0;
    }
    return cupidbuild_host_windows_read_repository_regular(transaction->repository_root_handle,
                                                           logical, limit, snapshot, bytes_out);
#else
#if defined(CUPIDBUILD_CUSTOM_LINUX)
    if (!cupidbuild_linux_descriptor_snapshot(transaction->repository_root_descriptor,
                                              &root_snapshot) ||
        !cupidbuild_host_snapshot_identity_equal(&transaction->repository_root_snapshot,
                                                 &root_snapshot)) {
        return 0;
    }
    return cupidbuild_linux_read_repository_regular(transaction->repository_root_descriptor,
                                                    logical, limit, snapshot, bytes_out);
#else
    if (!cupidbuild_native_directory_descriptor_snapshot(transaction->repository_root_descriptor,
                                                         &root_snapshot) ||
        !cupidbuild_host_snapshot_identity_equal(&transaction->repository_root_snapshot,
                                                 &root_snapshot)) {
        return 0;
    }
    return cupidbuild_native_read_repository_regular(transaction->repository_root_descriptor,
                                                     logical, limit, snapshot, bytes_out);
#endif
#endif
}

int cupidbuild_host_input_matches_snapshot(cupidbuild_host_transaction_t *transaction,
                                           const char *live_path,
                                           const cupidbuild_host_snapshot_t *expected) {
    cupidbuild_host_snapshot_t current;
    unsigned long long limit = CUPIDBUILD_HOST_FILE_LIMIT;
    unsigned int index;
    if (transaction != (cupidbuild_host_transaction_t *)0 && live_path != (const char *)0) {
        for (index = 0u; index < transaction->input_count; index++) {
            if (strcmp(transaction->inputs[index].live_path, live_path) == 0) {
                limit = transaction->inputs[index].limit;
                break;
            }
        }
    }
    return transaction != (cupidbuild_host_transaction_t *)0 && live_path != (const char *)0 &&
           expected != (const cupidbuild_host_snapshot_t *)0 &&
           cupidbuild_host_read_transaction_input(transaction, live_path, limit, &current,
                                                  (unsigned char **)0) &&
           cupidbuild_host_snapshot_equal(&current, expected);
}

static int cupidbuild_host_read_output(cupidbuild_host_transaction_t *transaction, int optional,
                                       cupidbuild_host_snapshot_t *snapshot,
                                       unsigned char **bytes_out) {
    if (transaction == (cupidbuild_host_transaction_t *)0 || transaction->runner_transaction != 0) {
        return 0;
    }
#if defined(_WIN32)
    return cupidbuild_host_windows_read_relative_regular(
        transaction->output_parent_handle, transaction->output_name, optional,
        transaction->candidate_limit, snapshot, bytes_out);
#else
#if defined(CUPIDBUILD_CUSTOM_LINUX)
    return cupidbuild_linux_read_relative_regular(
        transaction->output_parent_descriptor, transaction->output_name, optional,
        transaction->candidate_limit, snapshot, bytes_out);
#else
    return cupidbuild_native_read_relative_regular(
        transaction->output_parent_descriptor, transaction->output_name, optional,
        transaction->candidate_limit, snapshot, bytes_out);
#endif
#endif
}

#if !defined(_WIN32)
static int cupidbuild_host_flat_entry_conflicts(const cupidbuild_host_transaction_t *transaction,
                                                const char *name,
                                                const char *additional_live_path) {
    char path[CUPIDBUILD_HOST_PATH_BYTES];
    unsigned int index;
    if (transaction == (const cupidbuild_host_transaction_t *)0 || name == (const char *)0 ||
        name[0] == '\0' ||
        !cupidbuild_host_join(path, sizeof(path), transaction->repository_root, name) ||
        !cupidbuild_host_directory_entry_missing_at(transaction->repository_root_descriptor,
                                                    name) ||
        strcmp(path, transaction->source_path) == 0 ||
        strcmp(path, transaction->output_path) == 0 || strcmp(path, transaction->lock_path) == 0 ||
        (additional_live_path != (const char *)0 && strcmp(path, additional_live_path) == 0)) {
        return 1;
    }
    for (index = 0u; index < transaction->input_count; index++) {
        if (strcmp(path, transaction->inputs[index].live_path) == 0 ||
            strcmp(path, transaction->inputs[index].frozen_path) == 0) {
            return 1;
        }
    }
    return 0;
}
#endif

#if defined(_WIN32)
static int cupidbuild_host_windows_pin_initial_output(cupidbuild_host_transaction_t *transaction) {
    cupidbuild_host_snapshot_t retained;
    cupidbuild_host_snapshot_t named;
    long status = 0;
    unsigned long unsigned_status;
    HANDLE handle = cupidbuild_host_windows_open_relative_access_share_status(
        transaction->output_parent_handle, transaction->output_name, 0, 1, DELETE, FILE_SHARE_READ,
        &status);
    if (handle == INVALID_HANDLE_VALUE) {
        unsigned_status = (unsigned long)status;
        if (unsigned_status != 0xc000000fu && unsigned_status != 0xc0000034u &&
            unsigned_status != 0xc000003au) {
            return 0;
        }
        return cupidbuild_host_windows_read_relative_regular(
                   transaction->output_parent_handle, transaction->output_name, 1,
                   transaction->candidate_limit, &transaction->initial_output_snapshot,
                   (unsigned char **)0) &&
               transaction->initial_output_snapshot.present == 0;
    }
    if (!cupidbuild_host_windows_read_open_regular(handle, transaction->candidate_limit, &retained,
                                                   (unsigned char **)0) ||
        !cupidbuild_host_windows_read_relative_regular(
            transaction->output_parent_handle, transaction->output_name, 0,
            transaction->candidate_limit, &named, (unsigned char **)0) ||
        !cupidbuild_host_snapshot_equal(&retained, &named)) {
        (void)CloseHandle(handle);
        return 0;
    }
    transaction->initial_output_snapshot = retained;
    transaction->initial_output_handle = handle;
    return 1;
}
#endif

static int cupidbuild_host_pin_initial_output(cupidbuild_host_transaction_t *transaction) {
#if defined(_WIN32)
    return cupidbuild_host_windows_pin_initial_output(transaction);
#else
    cupidbuild_host_snapshot_t retained;
    cupidbuild_host_snapshot_t named;
#if defined(CUPIDBUILD_CUSTOM_LINUX)
    int descriptor = cupidbuild_linux_open_relative(transaction->output_parent_descriptor,
                                                    transaction->output_name, 0);
#else
    int descriptor = cupidbuild_native_open_relative(transaction->output_parent_descriptor,
                                                     transaction->output_name, 0);
#endif
    if (descriptor < 0) {
        return cupidbuild_host_read_output(transaction, 1, &transaction->initial_output_snapshot,
                                           (unsigned char **)0) &&
               transaction->initial_output_snapshot.present == 0;
    }
    if (!cupidbuild_host_promote_retained_descriptor(&descriptor) ||
        !cupidbuild_host_read_open_file(descriptor, transaction->candidate_limit, &retained,
                                        (unsigned char **)0) ||
        !cupidbuild_host_read_output(transaction, 0, &named, (unsigned char **)0) ||
        !cupidbuild_host_snapshot_equal(&retained, &named)) {
        cupidbuild_host_close_directory(descriptor);
        return 0;
    }
    transaction->initial_output_snapshot = retained;
    transaction->initial_output_descriptor = descriptor;
    return 1;
#endif
}

static int cupidbuild_host_read_initial_output(cupidbuild_host_transaction_t *transaction,
                                               cupidbuild_host_snapshot_t *snapshot) {
    return transaction->initial_output_snapshot.present != 0 &&
#if defined(_WIN32)
           transaction->initial_output_handle != INVALID_HANDLE_VALUE &&
           cupidbuild_host_windows_read_open_regular(transaction->initial_output_handle,
#else
           transaction->initial_output_descriptor >= 0 &&
           cupidbuild_host_read_open_file(transaction->initial_output_descriptor,
#endif
                                                     transaction->candidate_limit, snapshot,
                                                     (unsigned char **)0) &&
           cupidbuild_host_snapshot_equal(snapshot, &transaction->initial_output_snapshot);
}

static int cupidbuild_host_copy_frozen_input(cupidbuild_host_transaction_t *transaction,
                                             const char *live_path, const char *private_name,
                                             unsigned long long capacity, int previous_output,
                                             cupidbuild_host_observer_t *external,
                                             const cupidbuild_host_snapshot_t *input_snapshot,
                                             cupidbuild_host_snapshot_t *frozen_snapshot,
#if defined(_WIN32)
                                             HANDLE frozen_handle);
#else
                                             int frozen_descriptor);
#endif

static int cupidbuild_host_freeze_input_internal(cupidbuild_host_transaction_t *transaction,
                                                 const char *live_path, const char *private_name,
                                                 const char **frozen_path_out,
                                                 cupidbuild_host_snapshot_t *snapshot_out,
                                                 unsigned long long capacity, int streamed,
                                                 int previous_output,
                                                 cupidbuild_host_observer_t *external,
                                                 int share_input) {
    char frozen_path[CUPIDBUILD_HOST_PATH_BYTES];
    cupidbuild_host_snapshot_t snapshot;
    cupidbuild_host_snapshot_t frozen_snapshot;
    const cupidbuild_host_input_t *existing;
    unsigned char *bytes = (unsigned char *)0;
    size_t copied_size;
#if defined(_WIN32)
    HANDLE frozen_handle = INVALID_HANDLE_VALUE;
#else
    int frozen_descriptor = -1;
#endif
    if (transaction == (cupidbuild_host_transaction_t *)0 || private_name == (const char *)0 ||
        strchr(private_name, '/') != 0 || strchr(private_name, '\\') != 0 ||
        !(previous_output
              ? cupidbuild_host_read_initial_output(transaction, &snapshot)
              : external
                    ? cupidbuild_host_read_external_input(external, live_path, capacity, &snapshot)
                    : cupidbuild_host_read_transaction_input(transaction, live_path, capacity,
                                                             &snapshot, streamed ? NULL : &bytes))) {
        cupidbuild_host_set_error(transaction, "transaction input cannot be pinned and read");
        free(bytes);
        return 0;
    }
    if (!previous_output && transaction->initial_output_snapshot.present &&
        cupidbuild_host_snapshot_identity_equal(&transaction->initial_output_snapshot, &snapshot)) {
        cupidbuild_host_set_error(transaction, "output may not replace an input");
        free(bytes);
        return 0;
    }
    existing = cupidbuild_host_find_input_identity(transaction, &snapshot);
    if (existing) {
        free(bytes);
        if (!share_input) {
            cupidbuild_host_set_error(transaction, "transaction inputs may not share a file identity");
            return 0;
        }
        if (!cupidbuild_host_snapshot_equal(&existing->snapshot, &snapshot)) {
            cupidbuild_host_set_error(transaction, "shared transaction input changed while being captured");
            return 0;
        }
        if (frozen_path_out)
            *frozen_path_out = existing->frozen_path;
        if (snapshot_out)
            *snapshot_out = snapshot;
        return 1;
    }
    copied_size = streamed ? 0u : (size_t)snapshot.size;
    if (transaction->runner_transaction != 0) {
#if defined(_WIN32)
        if (!cupidbuild_host_join(frozen_path, sizeof(frozen_path), transaction->private_root,
                                  private_name) ||
            !cupidbuild_host_windows_create_relative_regular(transaction->private_handle,
                                                             private_name, bytes, copied_size, 0,
                                                             &frozen_snapshot, &frozen_handle)) {
            cupidbuild_host_set_error(transaction, "private frozen input cannot be created");
            free(bytes);
            return 0;
        }
#else
        if (!cupidbuild_host_write_anonymous(private_name, bytes, copied_size, frozen_path,
                                             sizeof(frozen_path), &frozen_snapshot,
                                             &frozen_descriptor)) {
            cupidbuild_host_set_error(transaction, "anonymous frozen input cannot be created");
            free(bytes);
            return 0;
        }
#endif
    } else {
#if defined(_WIN32)
        if (!cupidbuild_host_private_entry_path(transaction, frozen_path, sizeof(frozen_path),
                                                private_name) ||
            !cupidbuild_host_write_private_exclusive(transaction, private_name, bytes, copied_size,
                                                     0, &frozen_snapshot, &frozen_handle)) {
            cupidbuild_host_set_error(transaction, "private frozen input cannot be created");
            free(bytes);
            return 0;
        }
#else
        if (transaction->private_flat != 0) {
            int written;
            int created =
                streamed
                    ? cupidbuild_host_open_anonymous(private_name, frozen_path, sizeof(frozen_path),
                                                     &frozen_descriptor) &&
                          cupidbuild_host_read_open_file(frozen_descriptor, capacity,
                                                         &frozen_snapshot, (unsigned char **)0)
                    : cupidbuild_host_write_anonymous(private_name, bytes, copied_size, frozen_path,
                                                      sizeof(frozen_path), &frozen_snapshot,
                                                      &frozen_descriptor);
            if (!created || !cupidbuild_host_promote_retained_descriptor(&frozen_descriptor)) {
                cupidbuild_host_close_directory(frozen_descriptor);
                cupidbuild_host_set_error(transaction, "anonymous frozen input cannot be created");
                free(bytes);
                return 0;
            }
            written =
                snprintf(frozen_path, sizeof(frozen_path), "/proc/self/fd/%d", frozen_descriptor);
            if (written < 0 || (size_t)written >= sizeof(frozen_path)) {
                cupidbuild_host_close_directory(frozen_descriptor);
                frozen_descriptor = -1;
                cupidbuild_host_set_error(transaction,
                                          "anonymous frozen input path cannot be rendered");
                free(bytes);
                return 0;
            }
        } else if (!cupidbuild_host_private_entry_path(transaction, frozen_path,
                                                       sizeof(frozen_path), private_name) ||
                   !cupidbuild_host_write_private_exclusive(transaction, private_name, bytes,
                                                            copied_size, 0, &frozen_snapshot,
                                                            &frozen_descriptor)) {
            cupidbuild_host_set_error(transaction, "private frozen input cannot be created");
            free(bytes);
            return 0;
        }
#endif
    }
    if (streamed &&
        !cupidbuild_host_copy_frozen_input(transaction, live_path, private_name, capacity,
                                           previous_output, external, &snapshot, &frozen_snapshot,
#if defined(_WIN32)
                                           frozen_handle
#else
                                           frozen_descriptor
#endif
                                           )) {
#if defined(_WIN32)
        (void)cupidbuild_host_windows_dispose_retained_at(transaction->private_handle, private_name,
                                                          &frozen_snapshot, &frozen_handle);
#else
        if (transaction->private_flat != 0) {
            cupidbuild_host_close_directory(frozen_descriptor);
        } else {
            (void)cupidbuild_host_cleanup_retained_regular_at(transaction->private_descriptor,
                                                              private_name, &frozen_snapshot,
                                                              &frozen_descriptor);
        }
#endif
        cupidbuild_host_set_error(transaction, "streamed frozen input copy failed");
        free(bytes);
        return 0;
    }
#if defined(_WIN32)
    if (transaction->runner_transaction == 0 || frozen_handle != INVALID_HANDLE_VALUE) {
        if (!cupidbuild_host_windows_transition_retained_at_limit(
                transaction->private_handle, private_name, &frozen_snapshot, &frozen_handle, 1, 1,
                0, capacity)) {
            if (frozen_handle != INVALID_HANDLE_VALUE) {
                (void)cupidbuild_host_windows_dispose_read_retained_at(
                    transaction->private_handle, private_name, &frozen_snapshot, &frozen_handle);
            }
            cupidbuild_host_set_error(transaction,
                                      "private frozen input cannot be pinned read-only");
            free(bytes);
            return 0;
        }
    }
#else
    if (transaction->runner_transaction == 0 && transaction->private_flat == 0) {
        int created_descriptor = frozen_descriptor;
        cupidbuild_host_snapshot_t reopened_snapshot;
#if defined(CUPIDBUILD_CUSTOM_LINUX)
        frozen_descriptor =
            cupidbuild_linux_open_relative(transaction->private_descriptor, private_name, 0);
#else
        frozen_descriptor =
            cupidbuild_native_open_relative(transaction->private_descriptor, private_name, 0);
#endif
        if (frozen_descriptor < 0 ||
            !cupidbuild_host_promote_retained_descriptor(&frozen_descriptor) ||
            !cupidbuild_host_read_open_file(frozen_descriptor, capacity, &reopened_snapshot,
                                            (unsigned char **)0) ||
            !cupidbuild_host_snapshot_equal(&reopened_snapshot, &frozen_snapshot)) {
            cupidbuild_host_close_directory(frozen_descriptor);
            (void)cupidbuild_host_cleanup_retained_regular_at(transaction->private_descriptor,
                                                              private_name, &frozen_snapshot,
                                                              &created_descriptor);
            cupidbuild_host_set_error(transaction, "private frozen input cannot be pinned");
            free(bytes);
            return 0;
        }
        cupidbuild_host_close_directory(created_descriptor);
    }
#endif
    free(bytes);
    if (!cupidbuild_host_register_input(transaction, live_path, frozen_path, private_name,
                                        &snapshot, capacity)) {
#if defined(_WIN32)
        if (frozen_handle != INVALID_HANDLE_VALUE) {
            (void)cupidbuild_host_windows_dispose_read_retained_at(
                transaction->private_handle, private_name, &frozen_snapshot, &frozen_handle);
        } else
#else
        if (frozen_descriptor >= 0) {
            if (transaction->runner_transaction != 0 || transaction->private_flat != 0) {
                cupidbuild_host_close_directory(frozen_descriptor);
                frozen_descriptor = -1;
            } else {
                (void)cupidbuild_host_cleanup_retained_regular_at(transaction->private_descriptor,
                                                                  private_name, &frozen_snapshot,
                                                                  &frozen_descriptor);
            }
        } else
#endif
            cupidbuild_host_delete_file(frozen_path);
        return 0;
    }
    transaction->inputs[transaction->input_count - 1u].frozen_snapshot = frozen_snapshot;
    transaction->inputs[transaction->input_count - 1u].previous_output = previous_output;
    transaction->inputs[transaction->input_count - 1u].external_input = external != NULL;
#if defined(_WIN32)
    transaction->inputs[transaction->input_count - 1u].frozen_handle = frozen_handle;
#else
    transaction->inputs[transaction->input_count - 1u].frozen_descriptor = frozen_descriptor;
#endif
    if (frozen_path_out != (const char **)0) {
        *frozen_path_out = transaction->inputs[transaction->input_count - 1u].frozen_path;
    }
    if (snapshot_out != (cupidbuild_host_snapshot_t *)0) {
        *snapshot_out = snapshot;
    }
    return 1;
}

int cupidbuild_host_freeze_input(cupidbuild_host_transaction_t *transaction, const char *live_path,
                                 const char *private_name, const char **frozen_path_out,
                                 cupidbuild_host_snapshot_t *snapshot_out) {
    return cupidbuild_host_freeze_input_internal(transaction, live_path, private_name,
                                                 frozen_path_out, snapshot_out,
                                                 CUPIDBUILD_HOST_FILE_LIMIT, 0, 0, NULL, 0);
}

static int cupidbuild_host_freeze_bounded_input(cupidbuild_host_transaction_t *transaction,
                                                 const char *live_path, const char *private_name,
                                                 unsigned long long capacity, const char **frozen_path_out,
                                                 cupidbuild_host_snapshot_t *snapshot_out,
                                                 int share_input) {
    if (frozen_path_out != (const char **)0)
        *frozen_path_out = (const char *)0;
    if (snapshot_out != (cupidbuild_host_snapshot_t *)0)
        (void)memset(snapshot_out, 0, sizeof(*snapshot_out));
    const unsigned long long limit = share_input ? CUPIDBUILD_HOST_MAX_EXTENT : 2147483647ULL;
    if (transaction == (cupidbuild_host_transaction_t *)0 || transaction->runner_transaction != 0 ||
        capacity == 0u || capacity > limit ||
        (share_input && (transaction->source_request_state != 1 ||
                         transaction->observer_publication_started))) {
        cupidbuild_host_set_error(transaction,
                                  "invalid streamed frozen input capacity or transaction");
        return 0;
    }
    return cupidbuild_host_freeze_input_internal(transaction, live_path, private_name,
                                                 frozen_path_out, snapshot_out, capacity, 1,
                                                 0, NULL, share_input);
}

int cupidbuild_host_freeze_input_bounded(cupidbuild_host_transaction_t *transaction,
                                         const char *live_path, const char *private_name,
                                         unsigned long long capacity, const char **frozen_path_out,
                                         cupidbuild_host_snapshot_t *snapshot_out) {
    return cupidbuild_host_freeze_bounded_input(transaction, live_path, private_name, capacity,
                                               frozen_path_out, snapshot_out, 0);
}

unsigned long long cupidbuild_host_initial_output_extent(
    const cupidbuild_host_transaction_t *transaction) {
    return transaction && transaction->initial_output_snapshot.present
               ? transaction->initial_output_snapshot.size : 0u;
}

int cupidbuild_host_freeze_previous_output(cupidbuild_host_transaction_t *transaction,
                                           const char *private_name, const char **frozen_path_out,
                                           cupidbuild_host_snapshot_t *snapshot_out) {
    int accepted;
    if (frozen_path_out != (const char **)0)
        *frozen_path_out = (const char *)0;
    if (snapshot_out != (cupidbuild_host_snapshot_t *)0)
        (void)memset(snapshot_out, 0, sizeof(*snapshot_out));
    if (transaction == (cupidbuild_host_transaction_t *)0)
        return 0;
    if (transaction->runner_transaction != 0 || transaction->previous_output_state != 0 ||
        transaction->observer_publication_started != 0 ||
        !cupidbuild_host_path_is_relative_safe(private_name) ||
        strchr(private_name, '/') != (char *)0 || strchr(private_name, '\\') != (char *)0 ||
        !cupidbuild_host_require_public_binding(transaction, &transaction->initial_output_snapshot,
                                                1)) {
        transaction->previous_output_state = -1;
        cupidbuild_host_set_error(transaction, "invalid previous-output capture request");
        return 0;
    }
    transaction->previous_output_state = 2;
    if (transaction->initial_output_snapshot.present == 0) {
        transaction->previous_output_state = 1;
        return 1;
    }
    accepted = cupidbuild_host_freeze_input_internal(transaction, transaction->output_path,
                                                     private_name, frozen_path_out, snapshot_out,
                                                     transaction->candidate_limit, 1, 1, NULL, 0);
    transaction->previous_output_state = accepted ? 1 : -1;
    return accepted;
}

unsigned char *cupidbuild_host_read_frozen_input(cupidbuild_host_transaction_t *transaction,
                                                 const char *frozen_path, size_t limit,
                                                 size_t *size_out) {
    unsigned int index;
    cupidbuild_host_snapshot_t snapshot;
    unsigned char *bytes = (unsigned char *)0;
    if (transaction == (cupidbuild_host_transaction_t *)0 || frozen_path == (const char *)0 ||
        size_out == (size_t *)0) {
        return (unsigned char *)0;
    }
    if (limit > CUPIDBUILD_HOST_FILE_LIMIT)
        limit = CUPIDBUILD_HOST_FILE_LIMIT;
    for (index = 0u; index < transaction->input_count; index++) {
        if (strcmp(transaction->inputs[index].frozen_path, frozen_path) != 0) {
            continue;
        }
#if defined(_WIN32)
        if (transaction->inputs[index].frozen_handle != INVALID_HANDLE_VALUE) {
            cupidbuild_host_snapshot_t named;
            int valid =
                transaction->runner_transaction != 0
                    ? cupidbuild_host_windows_read_open_regular(
                          transaction->inputs[index].frozen_handle, limit, &snapshot, &bytes) &&
                          cupidbuild_host_read_regular_limit(frozen_path, 0, limit, &named,
                                                             (unsigned char **)0) &&
                          cupidbuild_host_snapshot_equal(&snapshot, &named)
                    : cupidbuild_host_read_retained_private_regular(
                          transaction, transaction->inputs[index].frozen_name, limit,
                          transaction->inputs[index].frozen_handle, &snapshot, &bytes);
            if (!valid || !cupidbuild_host_snapshot_equal(
                              &snapshot, &transaction->inputs[index].frozen_snapshot)) {
                free(bytes);
                return (unsigned char *)0;
            }
        } else
#else
        if (transaction->inputs[index].frozen_descriptor >= 0) {
            if (transaction->runner_transaction != 0) {
                if (!cupidbuild_host_read_open_file(transaction->inputs[index].frozen_descriptor,
                                                    limit, &snapshot, &bytes)) {
                    return (unsigned char *)0;
                }
            } else if (!cupidbuild_host_read_retained_private_regular(
                           transaction, transaction->inputs[index].frozen_name, limit,
                           transaction->inputs[index].frozen_descriptor, &snapshot, &bytes) ||
                       !cupidbuild_host_snapshot_equal(
                           &snapshot, &transaction->inputs[index].frozen_snapshot)) {
                free(bytes);
                return (unsigned char *)0;
            }
        } else
#endif
            if (!cupidbuild_host_read_regular(frozen_path, 0, &snapshot, &bytes) ||
                snapshot.size > limit) {
            free(bytes);
            return (unsigned char *)0;
        }
        *size_out = (size_t)snapshot.size;
        return bytes;
    }
    return (unsigned char *)0;
}

int cupidbuild_host_make_input_executable(cupidbuild_host_transaction_t *transaction,
                                          const char *frozen_path) {
    unsigned int index;
    if (transaction == (cupidbuild_host_transaction_t *)0 || frozen_path == (const char *)0) {
        return 0;
    }
    for (index = 0u; index < transaction->input_count; index++) {
        if (strcmp(transaction->inputs[index].frozen_path, frozen_path) == 0) {
            if (transaction->inputs[index].snapshot.size > CUPIDBUILD_HOST_FILE_LIMIT) {
                cupidbuild_host_set_error(transaction,
                                          "checked executable exceeds ordinary input limit");
                return 0;
            }
#if !defined(_WIN32)
            if (transaction->inputs[index].frozen_descriptor >= 0) {
#if defined(CUPIDBUILD_CUSTOM_LINUX)
                if (cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_FCHMOD,
                                         (unsigned int)transaction->inputs[index].frozen_descriptor,
                                         0700u) == 0) {
#else
                if (fchmod(transaction->inputs[index].frozen_descriptor, 0700) == 0) {
#endif
                    return 1;
                }
            } else
#endif
                if (cupidbuild_host_make_executable(frozen_path)) {
                return 1;
            }
            cupidbuild_host_set_error(transaction,
                                      "private checked tool cannot be made executable");
            return 0;
        }
    }
    cupidbuild_host_set_error(transaction, "executable is not a frozen transaction input");
    return 0;
}
