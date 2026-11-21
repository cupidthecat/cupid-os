/**
 * @file cupidbuild_host_transaction_lifetime.inc
 * @brief Open, verify and close checked transactions.
 */

static int cupidbuild_host_transaction_open_internal(
    const char *repository_root, const char *source_logical, const char *output_logical,
    cupidbuild_host_profile_parent_t *profile_parent,
    cupidbuild_host_output_parent_t *output_parent, unsigned long long candidate_limit, int capture_source,
    cupidbuild_host_transaction_t **transaction_out) {
    cupidbuild_host_transaction_t *transaction;
    cupidbuild_host_snapshot_t created_private_snapshot;
    cupidbuild_host_snapshot_t opened_private_snapshot;
    cupidbuild_host_snapshot_t live_private_snapshot;
    unsigned int attempt;
    const char *frozen = (const char *)0;
    int lock_written;
    size_t lock_capacity;
    if (transaction_out == (cupidbuild_host_transaction_t **)0) {
        return 0;
    }
    *transaction_out = (cupidbuild_host_transaction_t *)0;
    transaction = (cupidbuild_host_transaction_t *)calloc(1u, sizeof(*transaction));
    if (transaction == (cupidbuild_host_transaction_t *)0) {
        return 0;
    }
    transaction->candidate_limit = candidate_limit;
    transaction->input_limit = capture_source ? CUPIDBUILD_HOST_INPUTS : CUPIDBUILD_HOST_DISK_INPUTS;
    (void)memset(&created_private_snapshot, 0, sizeof(created_private_snapshot));
    (void)memset(&opened_private_snapshot, 0, sizeof(opened_private_snapshot));
    (void)memset(&live_private_snapshot, 0, sizeof(live_private_snapshot));
#if defined(_WIN32)
    transaction->repository_root_handle = INVALID_HANDLE_VALUE;
    transaction->output_parent_handle = INVALID_HANDLE_VALUE;
    transaction->initial_output_handle = INVALID_HANDLE_VALUE;
    transaction->lock_handle = INVALID_HANDLE_VALUE;
    transaction->candidate_handle = INVALID_HANDLE_VALUE;
    transaction->private_output_handle = INVALID_HANDLE_VALUE;
    transaction->tool_stdout_handle = INVALID_HANDLE_VALUE;
    transaction->tool_stderr_handle = INVALID_HANDLE_VALUE;
    transaction->private_handle = INVALID_HANDLE_VALUE;
    transaction->working_directory_handle = INVALID_HANDLE_VALUE;
#else
    transaction->repository_root_descriptor = -1;
    transaction->output_parent_descriptor = -1;
    transaction->initial_output_descriptor = -1;
    transaction->working_directory_descriptor = -1;
    transaction->private_reservation_descriptor = -1;
    transaction->candidate_descriptor = -1;
    transaction->private_output_descriptor = -1;
    transaction->private_descriptor = -1;
    transaction->tool_stdout_descriptor = -1;
    transaction->tool_stderr_descriptor = -1;
#endif
    if ((capture_source && !cupidbuild_host_path_is_relative_safe(source_logical)) ||
        !cupidbuild_host_path_is_relative_safe(output_logical) ||
        !cupidbuild_host_copy_text(transaction->repository_root,
                                   sizeof(transaction->repository_root), repository_root) ||
        (capture_source &&
         !cupidbuild_host_join(transaction->source_path, sizeof(transaction->source_path),
                               repository_root, source_logical)) ||
        !cupidbuild_host_join(transaction->output_path, sizeof(transaction->output_path),
                              repository_root, output_logical) ||
        !cupidbuild_host_parent(transaction->output_parent, sizeof(transaction->output_parent),
                                transaction->output_path) ||
        !cupidbuild_host_basename(transaction->output_name, sizeof(transaction->output_name),
                                  transaction->output_path) ||
        !cupidbuild_host_directory_snapshot(repository_root,
                                            &transaction->repository_root_snapshot) ||
        !cupidbuild_host_directory_snapshot(transaction->output_parent,
                                            &transaction->output_parent_snapshot)) {
        cupidbuild_host_set_error(transaction, "guarded artifact paths are invalid");
        *transaction_out = transaction;
        return 0;
    }
#if defined(_WIN32)
    {
        cupidbuild_host_snapshot_t open_root_snapshot;
        transaction->repository_root_handle =
            cupidbuild_host_windows_open_repository(repository_root);
        if (!cupidbuild_host_windows_directory_handle_snapshot(transaction->repository_root_handle,
                                                               &open_root_snapshot) ||
            !cupidbuild_host_snapshot_identity_equal(&transaction->repository_root_snapshot,
                                                     &open_root_snapshot)) {
            cupidbuild_host_set_error(transaction, "repository root cannot be pinned");
            *transaction_out = transaction;
            return 0;
        }
    }
#else
    {
        cupidbuild_host_snapshot_t open_root_snapshot;
#if defined(CUPIDBUILD_CUSTOM_LINUX)
        transaction->repository_root_descriptor = cupidbuild_linux_open_repository(repository_root);
        if (transaction->repository_root_descriptor >= 0 &&
            transaction->repository_root_descriptor <= 2) {
            int original = transaction->repository_root_descriptor;
            int duplicated = cupidbuild_host_linux_duplicate_above_standard(original);
            (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)original);
            transaction->repository_root_descriptor = duplicated;
            if (duplicated > 2) {
                if (cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_FCNTL64,
                                         (unsigned int)transaction->repository_root_descriptor,
                                         CUPIDBUILD_LINUX_F_SETFD,
                                         CUPIDBUILD_LINUX_FD_CLOEXEC) != 0) {
                    cupidbuild_host_close_directory(transaction->repository_root_descriptor);
                    transaction->repository_root_descriptor = -1;
                }
            }
        }
        if (!cupidbuild_linux_descriptor_snapshot(transaction->repository_root_descriptor,
                                                  &open_root_snapshot) ||
#else
        transaction->repository_root_descriptor =
            cupidbuild_native_open_repository(repository_root);
        if (transaction->repository_root_descriptor >= 0 &&
            transaction->repository_root_descriptor <= STDERR_FILENO) {
            int original = transaction->repository_root_descriptor;
            int duplicated = cupidbuild_host_posix_duplicate_above_standard(original);
            (void)close(original);
            transaction->repository_root_descriptor = duplicated;
            if (duplicated > STDERR_FILENO) {
                if (fcntl(transaction->repository_root_descriptor, F_SETFD, FD_CLOEXEC) != 0) {
                    cupidbuild_host_close_directory(transaction->repository_root_descriptor);
                    transaction->repository_root_descriptor = -1;
                }
            }
        }
        if (!cupidbuild_native_directory_descriptor_snapshot(
                transaction->repository_root_descriptor, &open_root_snapshot) ||
#endif
            !cupidbuild_host_snapshot_identity_equal(&transaction->repository_root_snapshot,
                                                     &open_root_snapshot)) {
            cupidbuild_host_set_error(transaction, "repository root cannot be pinned");
            *transaction_out = transaction;
            return 0;
        }
    }
#endif
    lock_capacity = sizeof(transaction->lock_name);
    lock_written = snprintf(transaction->lock_name, lock_capacity, "%s.cupidbuild.lock",
                            transaction->output_name);
    if (lock_written < 0 || (size_t)lock_written >= lock_capacity ||
        !cupidbuild_host_join(transaction->lock_path, sizeof(transaction->lock_path),
                              transaction->output_parent, transaction->lock_name)) {
        cupidbuild_host_set_error(transaction, "guarded artifact paths are invalid");
        *transaction_out = transaction;
        return 0;
    }
#if defined(_WIN32)
    {
        const char *parent_logical;
        cupidbuild_host_snapshot_t open_parent_snapshot;
        if (strcmp(transaction->output_parent, transaction->repository_root) == 0) {
            transaction->output_parent_handle = transaction->repository_root_handle;
            transaction->output_parent_is_repository_root = 1;
        } else {
            if (!cupidbuild_host_repository_logical_path(transaction, transaction->output_parent,
                                                         &parent_logical)) {
                cupidbuild_host_set_error(transaction, "output parent cannot be pinned");
                *transaction_out = transaction;
                return 0;
            }
            transaction->output_parent_handle = cupidbuild_host_windows_open_relative_path_access(
                transaction->repository_root_handle, parent_logical, 1, 1,
                FILE_ADD_FILE | FILE_DELETE_CHILD);
        }
        if (!cupidbuild_host_windows_directory_handle_snapshot(transaction->output_parent_handle,
                                                               &open_parent_snapshot) ||
            !cupidbuild_host_snapshot_identity_equal(&transaction->output_parent_snapshot,
                                                     &open_parent_snapshot)) {
            cupidbuild_host_set_error(transaction, "output parent cannot be pinned");
            *transaction_out = transaction;
            return 0;
        }
    }
#else
    {
        const char *parent_logical;
        cupidbuild_host_snapshot_t open_parent_snapshot;
        if (strcmp(transaction->output_parent, transaction->repository_root) == 0) {
            transaction->output_parent_descriptor = transaction->repository_root_descriptor;
            transaction->output_parent_is_repository_root = 1;
        } else if (!cupidbuild_host_repository_logical_path(transaction, transaction->output_parent,
                                                            &parent_logical)) {
            cupidbuild_host_set_error(transaction, "output parent cannot be pinned");
            *transaction_out = transaction;
            return 0;
        }
#if defined(CUPIDBUILD_CUSTOM_LINUX)
        if (transaction->output_parent_is_repository_root == 0) {
            transaction->output_parent_descriptor = cupidbuild_linux_open_relative_path(
                transaction->repository_root_descriptor, parent_logical, 1);
        }
        if (!cupidbuild_linux_descriptor_snapshot(transaction->output_parent_descriptor,
                                                  &open_parent_snapshot) ||
#else
        if (transaction->output_parent_is_repository_root == 0) {
            transaction->output_parent_descriptor = cupidbuild_native_open_relative_path(
                transaction->repository_root_descriptor, parent_logical, 1);
        }
        if (!cupidbuild_native_directory_descriptor_snapshot(transaction->output_parent_descriptor,
                                                             &open_parent_snapshot) ||
#endif
            !cupidbuild_host_snapshot_identity_equal(&transaction->output_parent_snapshot,
                                                     &open_parent_snapshot)) {
            cupidbuild_host_set_error(transaction, "output parent cannot be pinned");
            *transaction_out = transaction;
            return 0;
        }
    }
#endif
    if (profile_parent != (cupidbuild_host_profile_parent_t *)0 &&
        !cupidbuild_host_profile_parent_bind(profile_parent, transaction)) {
        *transaction_out = transaction;
        return 0;
    }
    if (output_parent != (cupidbuild_host_output_parent_t *)0 &&
        !cupidbuild_output_parent_attach(output_parent, transaction)) {
        cupidbuild_host_set_error(transaction, cupidbuild_host_output_parent_error(output_parent));
        *transaction_out = transaction;
        return 0;
    }
    if (!cupidbuild_host_acquire_lock(transaction)) {
        *transaction_out = transaction;
        return 0;
    }
    for (attempt = 0u; attempt < CUPIDBUILD_HOST_PRIVATE_ATTEMPTS; attempt++) {
#if defined(_WIN32)
        int written = snprintf(transaction->private_name, sizeof(transaction->private_name),
                               ".cupidbuild-object-%08x", attempt);
        if (written < 0 || (size_t)written >= sizeof(transaction->private_name) ||
            !cupidbuild_host_join(transaction->private_root, sizeof(transaction->private_root),
                                  repository_root, transaction->private_name)) {
            break;
        }
        transaction->private_handle = cupidbuild_host_profile_parent_open_component(
            transaction->repository_root_handle, transaction->private_name, 1, 1);
        if (transaction->private_handle != INVALID_HANDLE_VALUE) {
            transaction->private_created = 1;
            (void)cupidbuild_host_windows_directory_handle_snapshot(transaction->private_handle,
                                                                    &created_private_snapshot);
            break;
        }
#else
        int prefix_written =
            snprintf(transaction->private_prefix, sizeof(transaction->private_prefix),
                     ".cupidbuild-object-%08x", attempt);
        int reservation_written;
        int candidate_written;
        int publish_written;
        int output_written;
        int stdout_written;
        int stderr_written;
        if (prefix_written < 0 || (size_t)prefix_written >= sizeof(transaction->private_prefix)) {
            break;
        }
        reservation_written = snprintf(transaction->private_name, sizeof(transaction->private_name),
                                       "%s.reserve", transaction->private_prefix);
        candidate_written =
            snprintf(transaction->candidate_name, sizeof(transaction->candidate_name),
                     "%s.candidate.o", transaction->private_prefix);
        publish_written = snprintf(transaction->candidate_publish_name,
                                   sizeof(transaction->candidate_publish_name),
                                   "%s.candidate.publish", transaction->private_prefix);
        output_written =
            snprintf(transaction->private_output_name, sizeof(transaction->private_output_name),
                     "%s.candidate.map", transaction->private_prefix);
        stdout_written =
            snprintf(transaction->tool_stdout_name, sizeof(transaction->tool_stdout_name),
                     "%s.tool.stdout", transaction->private_prefix);
        stderr_written =
            snprintf(transaction->tool_stderr_name, sizeof(transaction->tool_stderr_name),
                     "%s.tool.stderr", transaction->private_prefix);
        if (reservation_written < 0 || candidate_written < 0 || publish_written < 0 ||
            output_written < 0 || stdout_written < 0 || stderr_written < 0 ||
            (size_t)reservation_written >= sizeof(transaction->private_name) ||
            (size_t)candidate_written >= sizeof(transaction->candidate_name) ||
            (size_t)publish_written >= sizeof(transaction->candidate_publish_name) ||
            (size_t)output_written >= sizeof(transaction->private_output_name) ||
            (size_t)stdout_written >= sizeof(transaction->tool_stdout_name) ||
            (size_t)stderr_written >= sizeof(transaction->tool_stderr_name) ||
            !cupidbuild_host_copy_text(transaction->private_root, sizeof(transaction->private_root),
                                       transaction->repository_root)) {
            break;
        }
        if (cupidbuild_host_flat_entry_conflicts(transaction, transaction->private_name,
                                                 (const char *)0) ||
            cupidbuild_host_flat_entry_conflicts(transaction, transaction->candidate_name,
                                                 (const char *)0) ||
            cupidbuild_host_flat_entry_conflicts(transaction, transaction->candidate_publish_name,
                                                 (const char *)0) ||
            cupidbuild_host_flat_entry_conflicts(transaction, transaction->private_output_name,
                                                 (const char *)0) ||
            cupidbuild_host_flat_entry_conflicts(transaction, transaction->tool_stdout_name,
                                                 (const char *)0) ||
            cupidbuild_host_flat_entry_conflicts(transaction, transaction->tool_stderr_name,
                                                 (const char *)0)) {
            continue;
        }
        if (cupidbuild_host_write_exclusive_at(transaction->repository_root_descriptor,
                                               transaction->private_name, (const unsigned char *)"",
                                               0u, 0, &created_private_snapshot,
                                               &transaction->private_reservation_descriptor)) {
            transaction->private_created = 1;
            transaction->private_flat = 1;
            transaction->private_descriptor = transaction->repository_root_descriptor;
            break;
        }
#endif
    }
#if defined(_WIN32)
    if (transaction->private_created == 0 || created_private_snapshot.present == 0 ||
        transaction->private_handle == INVALID_HANDLE_VALUE ||
        !cupidbuild_host_windows_directory_handle_snapshot(transaction->private_handle,
                                                           &opened_private_snapshot) ||
        !cupidbuild_host_directory_snapshot(transaction->private_root, &live_private_snapshot) ||
        !cupidbuild_host_snapshot_identity_equal(&created_private_snapshot,
                                                 &opened_private_snapshot) ||
        !cupidbuild_host_snapshot_identity_equal(&created_private_snapshot,
                                                 &live_private_snapshot)) {
        cupidbuild_host_set_error(transaction, "private artifact transaction cannot be pinned");
        *transaction_out = transaction;
        return 0;
    }
    transaction->private_root_snapshot = created_private_snapshot;
    if (!cupidbuild_host_copy_text(transaction->candidate_name, sizeof(transaction->candidate_name),
                                   "candidate.o") ||
        !cupidbuild_host_copy_text(transaction->candidate_publish_name,
                                   sizeof(transaction->candidate_publish_name),
                                   CUPIDBUILD_HOST_CANDIDATE_PUBLISH) ||
        !cupidbuild_host_copy_text(transaction->private_output_name,
                                   sizeof(transaction->private_output_name), "candidate.map") ||
        !cupidbuild_host_copy_text(transaction->tool_stdout_name,
                                   sizeof(transaction->tool_stdout_name), "tool.stdout") ||
        !cupidbuild_host_copy_text(transaction->tool_stderr_name,
                                   sizeof(transaction->tool_stderr_name), "tool.stderr")) {
        cupidbuild_host_set_error(transaction, "private artifact names cannot be prepared");
        *transaction_out = transaction;
        return 0;
    }
#else
    transaction->working_directory_descriptor = cupidbuild_host_open_directory("/proc");
    if (transaction->private_created == 0 || transaction->private_flat == 0 ||
        transaction->private_descriptor < 0 || transaction->working_directory_descriptor < 0 ||
        transaction->private_reservation_descriptor < 0 || created_private_snapshot.present == 0 ||
        !cupidbuild_host_read_open_file(transaction->private_reservation_descriptor,
                                        CUPIDBUILD_HOST_FILE_LIMIT, &opened_private_snapshot,
                                        (unsigned char **)0) ||
        !cupidbuild_host_regular_snapshot_at(transaction->repository_root_descriptor,
                                             transaction->private_name, &live_private_snapshot) ||
        !cupidbuild_host_snapshot_equal(&created_private_snapshot, &opened_private_snapshot) ||
        !cupidbuild_host_snapshot_equal(&created_private_snapshot, &live_private_snapshot)) {
        cupidbuild_host_set_error(transaction, "private artifact reservation cannot be pinned");
        *transaction_out = transaction;
        return 0;
    }
    transaction->private_reservation_snapshot = created_private_snapshot;
    transaction->private_root_snapshot = transaction->repository_root_snapshot;
#endif
#if defined(_WIN32)
    if (!cupidbuild_host_private_entry_path(transaction, transaction->candidate,
                                            sizeof(transaction->candidate), "candidate.o") ||
        !cupidbuild_host_private_entry_path(transaction, transaction->private_output,
                                            sizeof(transaction->private_output), "candidate.map") ||
        !cupidbuild_host_private_entry_path(transaction, transaction->tool_stdout,
                                            sizeof(transaction->tool_stdout), "tool.stdout") ||
        !cupidbuild_host_private_entry_path(transaction, transaction->tool_stderr,
                                            sizeof(transaction->tool_stderr), "tool.stderr")) {
        cupidbuild_host_set_error(transaction, "private artifact paths cannot be prepared");
        *transaction_out = transaction;
        return 0;
    }
#endif
    if (output_parent != (cupidbuild_host_output_parent_t *)0 &&
        !cupidbuild_host_output_parent_require_current(output_parent)) {
        cupidbuild_host_set_error(transaction, cupidbuild_host_output_parent_error(output_parent));
        *transaction_out = transaction;
        return 0;
    }
    if ((capture_source &&
         (!cupidbuild_host_freeze_input(transaction, transaction->source_path, "source.asm", &frozen,
                                        (cupidbuild_host_snapshot_t *)0) ||
          !cupidbuild_host_copy_text(transaction->frozen_source, sizeof(transaction->frozen_source),
                                     frozen))) ||
        !cupidbuild_host_pin_initial_output(transaction)) {
        if (transaction->error[0] == '\0') {
            cupidbuild_host_set_error(transaction, "private artifact transaction cannot be opened");
        }
        *transaction_out = transaction;
        return 0;
    }
#if !defined(_WIN32)
    if (!cupidbuild_host_prepare_private_candidate(transaction) ||
        !cupidbuild_host_prepare_private_output_path(transaction)) {
        cupidbuild_host_set_error(transaction, "private artifact files cannot be prepared");
        *transaction_out = transaction;
        return 0;
    }
#endif
    if (capture_source && transaction->initial_output_snapshot.present != 0 &&
        memcmp(transaction->initial_output_snapshot.identity,
               transaction->inputs[0].snapshot.identity,
               sizeof(transaction->initial_output_snapshot.identity)) == 0) {
        cupidbuild_host_set_error(transaction, "output may not replace an input");
        *transaction_out = transaction;
        return 0;
    }
    *transaction_out = transaction;
    return 1;
}

int cupidbuild_host_transaction_open(const char *repository_root, const char *source_logical,
                                     const char *output_logical,
                                     cupidbuild_host_transaction_t **transaction_out) {
    return cupidbuild_host_transaction_open_internal(
        repository_root, source_logical, output_logical, (cupidbuild_host_profile_parent_t *)0,
        (cupidbuild_host_output_parent_t *)0, CUPIDBUILD_HOST_FILE_LIMIT, 1, transaction_out);
}

int cupidbuild_host_transaction_open_bounded(const char *repository_root,
                                             const char *source_logical, const char *output_logical,
                                             unsigned long long candidate_capacity,
                                             cupidbuild_host_transaction_t **transaction_out) {
    if (transaction_out != (cupidbuild_host_transaction_t **)0)
        *transaction_out = (cupidbuild_host_transaction_t *)0;
    if (candidate_capacity == 0ull || candidate_capacity > 2147483647ull)
        return 0;
    return cupidbuild_host_transaction_open_internal(
        repository_root, source_logical, output_logical, (cupidbuild_host_profile_parent_t *)0,
        (cupidbuild_host_output_parent_t *)0, candidate_capacity, 1, transaction_out);
}

int cupidbuild_host_profile_transaction_open(const char *repository_root,
                                             const char *source_logical, const char *output_logical,
                                             cupidbuild_host_profile_parent_t *profile_parent,
                                             cupidbuild_host_transaction_t **transaction_out) {
    return cupidbuild_host_transaction_open_internal(
        repository_root, source_logical, output_logical, profile_parent,
        (cupidbuild_host_output_parent_t *)0, CUPIDBUILD_HOST_FILE_LIMIT, 1, transaction_out);
}

int cupidbuild_host_output_transaction_open(const char *root, const char *source,
                                            const char *output,
                                            cupidbuild_host_output_parent_t *preparation,
                                            cupidbuild_host_transaction_t **transaction_out) {
    if (preparation == (cupidbuild_host_output_parent_t *)0) {
        if (transaction_out != (cupidbuild_host_transaction_t **)0)
            *transaction_out = (cupidbuild_host_transaction_t *)0;
        return 0;
    }
    if (!cupidbuild_host_transaction_open_internal(
            root, source, output, (cupidbuild_host_profile_parent_t *)0, preparation,
            CUPIDBUILD_HOST_FILE_LIMIT, 1, transaction_out))
        return 0;
    return cupidbuild_host_require_publication_boundary(*transaction_out);
}

int cupidbuild_host_publication_transaction_open_bounded(
    const char *root, const char *output, unsigned long long capacity,
    cupidbuild_host_transaction_t **transaction_out) {
    if (transaction_out)
        *transaction_out = NULL;
    if (capacity == 0u || capacity > CUPIDBUILD_HOST_MAX_EXTENT)
        return 0;
    return cupidbuild_host_transaction_open_internal(root, NULL, output, NULL, NULL,
                                                      capacity, 0, transaction_out);
}

int cupidbuild_host_runner_open(const char *working_directory,
                                cupidbuild_host_transaction_t **transaction_out) {
    cupidbuild_host_transaction_t *transaction;
#if defined(_WIN32)
    cupidbuild_host_snapshot_t handle_snapshot;
    cupidbuild_host_snapshot_t private_handle_snapshot;
    static const unsigned char empty[1] = {0u};
    unsigned int attempt;
#else
    cupidbuild_host_snapshot_t descriptor_snapshot;
#endif
    if (transaction_out == (cupidbuild_host_transaction_t **)0) {
        return 0;
    }
    *transaction_out = (cupidbuild_host_transaction_t *)0;
    transaction = (cupidbuild_host_transaction_t *)calloc(1u, sizeof(*transaction));
    if (transaction == (cupidbuild_host_transaction_t *)0) {
        return 0;
    }
    transaction->runner_transaction = 1;
    transaction->input_limit = CUPIDBUILD_HOST_INPUTS;
#if defined(_WIN32)
    transaction->repository_root_handle = INVALID_HANDLE_VALUE;
    transaction->output_parent_handle = INVALID_HANDLE_VALUE;
    transaction->initial_output_handle = INVALID_HANDLE_VALUE;
    transaction->lock_handle = INVALID_HANDLE_VALUE;
    transaction->candidate_handle = INVALID_HANDLE_VALUE;
    transaction->private_output_handle = INVALID_HANDLE_VALUE;
    transaction->tool_stdout_handle = INVALID_HANDLE_VALUE;
    transaction->tool_stderr_handle = INVALID_HANDLE_VALUE;
    transaction->private_handle = INVALID_HANDLE_VALUE;
    transaction->working_directory_handle = INVALID_HANDLE_VALUE;
#else
    transaction->repository_root_descriptor = -1;
    transaction->output_parent_descriptor = -1;
    transaction->initial_output_descriptor = -1;
    transaction->working_directory_descriptor = -1;
    transaction->private_reservation_descriptor = -1;
    transaction->candidate_descriptor = -1;
    transaction->private_output_descriptor = -1;
    transaction->private_descriptor = -1;
    transaction->tool_stdout_descriptor = -1;
    transaction->tool_stderr_descriptor = -1;
#endif
    if (!cupidbuild_host_absolute_directory(transaction->repository_root,
                                            sizeof(transaction->repository_root),
                                            working_directory) ||
        !cupidbuild_host_directory_snapshot(transaction->repository_root,
                                            &transaction->repository_root_snapshot)) {
        cupidbuild_host_set_error(transaction, "checked tool working directory is invalid");
        *transaction_out = transaction;
        return 0;
    }
#if !defined(_WIN32)
    transaction->output_parent_descriptor =
        cupidbuild_host_open_directory(transaction->repository_root);
    if (transaction->output_parent_descriptor < 0 ||
        !cupidbuild_host_directory_descriptor_snapshot(transaction->output_parent_descriptor,
                                                       &descriptor_snapshot) ||
        !cupidbuild_host_snapshot_identity_equal(&descriptor_snapshot,
                                                 &transaction->repository_root_snapshot)) {
        cupidbuild_host_set_error(transaction, "checked tool working directory cannot be pinned");
        *transaction_out = transaction;
        return 0;
    }
    if (!cupidbuild_host_open_anonymous("cupidbuild-stdout", transaction->tool_stdout,
                                        sizeof(transaction->tool_stdout),
                                        &transaction->tool_stdout_descriptor) ||
        !cupidbuild_host_promote_retained_descriptor(&transaction->tool_stdout_descriptor) ||
        !cupidbuild_host_open_anonymous("cupidbuild-stderr", transaction->tool_stderr,
                                        sizeof(transaction->tool_stderr),
                                        &transaction->tool_stderr_descriptor) ||
        !cupidbuild_host_promote_retained_descriptor(&transaction->tool_stderr_descriptor)) {
        cupidbuild_host_set_error(transaction, "anonymous checked-tool streams cannot be opened");
        *transaction_out = transaction;
        return 0;
    }
    *transaction_out = transaction;
    return 1;
#endif
#if defined(_WIN32)
    transaction->working_directory_handle =
        CreateFileA(transaction->repository_root,
                    FILE_READ_ATTRIBUTES | FILE_TRAVERSE | FILE_ADD_SUBDIRECTORY |
                        FILE_LIST_DIRECTORY | SYNCHRONIZE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, (LPSECURITY_ATTRIBUTES)0, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, (HANDLE)0);
    if (transaction->working_directory_handle == INVALID_HANDLE_VALUE ||
        !cupidbuild_host_windows_directory_handle_snapshot(transaction->working_directory_handle,
                                                           &handle_snapshot) ||
        !cupidbuild_host_snapshot_identity_equal(&handle_snapshot,
                                                 &transaction->repository_root_snapshot)) {
        cupidbuild_host_set_error(transaction, "checked tool working directory cannot be pinned");
        *transaction_out = transaction;
        return 0;
    }
    for (attempt = 0u; attempt < CUPIDBUILD_HOST_PRIVATE_ATTEMPTS; attempt++) {
        int written = snprintf(transaction->output_name, sizeof(transaction->output_name),
                               ".cupidbuild-run-%08x-%08x", cupidbuild_host_process_id(), attempt);
        if (written < 0 || (size_t)written >= sizeof(transaction->output_name) ||
            !cupidbuild_host_copy_text(transaction->private_name, sizeof(transaction->private_name),
                                       transaction->output_name) ||
            !cupidbuild_host_join(transaction->private_root, sizeof(transaction->private_root),
                                  transaction->repository_root, transaction->output_name)) {
            break;
        }
        transaction->private_handle = cupidbuild_host_profile_parent_open_component(
            transaction->working_directory_handle, transaction->output_name, 1, 1);
        if (transaction->private_handle != INVALID_HANDLE_VALUE) {
            transaction->private_created = 1;
            break;
        }
    }
    if (transaction->private_created == 0 ||
#if defined(_WIN32)
        transaction->private_handle == INVALID_HANDLE_VALUE ||
        !cupidbuild_host_windows_directory_handle_snapshot(transaction->private_handle,
                                                           &private_handle_snapshot) ||
#else
        transaction->private_descriptor < 0 ||
#endif
        !cupidbuild_host_directory_snapshot(transaction->private_root,
                                            &transaction->private_root_snapshot) ||
#if defined(_WIN32)
        !cupidbuild_host_snapshot_identity_equal(&private_handle_snapshot,
                                                 &transaction->private_root_snapshot) ||
#endif
        !cupidbuild_host_join(transaction->tool_stdout, sizeof(transaction->tool_stdout),
                              transaction->private_root, "tool.stdout") ||
        !cupidbuild_host_join(transaction->tool_stderr, sizeof(transaction->tool_stderr),
                              transaction->private_root, "tool.stderr")) {
        cupidbuild_host_set_error(transaction, "private checked-tool runner cannot be opened");
        *transaction_out = transaction;
        return 0;
    }
#if defined(_WIN32)
    if (!cupidbuild_host_windows_create_relative_regular(
            transaction->private_handle, "tool.stdout", empty, 0u, 1,
            &transaction->tool_stdout_snapshot, &transaction->tool_stdout_handle) ||
        !cupidbuild_host_windows_create_relative_regular(
            transaction->private_handle, "tool.stderr", empty, 0u, 1,
            &transaction->tool_stderr_snapshot, &transaction->tool_stderr_handle)) {
        cupidbuild_host_set_error(transaction, "private checked-tool streams cannot be opened");
        *transaction_out = transaction;
        return 0;
    }
#endif
    *transaction_out = transaction;
    return 1;
#endif
}

#if defined(_WIN32)
static int cupidbuild_host_read_runner_snapshot(cupidbuild_host_transaction_t *transaction,
                                                const char *path, int optional,
                                                cupidbuild_host_snapshot_t *snapshot) {
    (void)transaction;
    return cupidbuild_host_read_regular(path, optional, snapshot, (unsigned char **)0);
}

static int cupidbuild_host_delete_runner_directory(cupidbuild_host_transaction_t *transaction) {
    BY_HANDLE_FILE_INFORMATION information;
    cupidbuild_windows_io_status_t status;
    unsigned char disposition = 1u;
    long result;
    if (transaction->private_handle == INVALID_HANDLE_VALUE ||
        !GetFileInformationByHandle(transaction->private_handle, &information) ||
        information.dwVolumeSerialNumber != transaction->private_root_snapshot.identity[0] ||
        information.nFileIndexHigh != transaction->private_root_snapshot.identity[1] ||
        information.nFileIndexLow != transaction->private_root_snapshot.identity[2]) {
        return 0;
    }
    (void)memset(&status, 0, sizeof(status));
    result =
        cupid_windows_nt_set_information_file(transaction->private_handle, &status, &disposition,
                                              (unsigned long)sizeof(disposition), 13u);
    if (!CloseHandle(transaction->private_handle) || result < 0) {
        transaction->private_handle = INVALID_HANDLE_VALUE;
        return 0;
    }
    transaction->private_handle = INVALID_HANDLE_VALUE;
    return 1;
}

static int cupidbuild_host_remove_runner_private(cupidbuild_host_transaction_t *transaction) {
    unsigned int index;
    char name[CUPIDBUILD_HOST_PATH_BYTES];
    int complete = 1;
    if (transaction->tool_stdout_handle != INVALID_HANDLE_VALUE &&
        !cupidbuild_host_windows_dispose_retained_at(transaction->private_handle, "tool.stdout",
                                                     &transaction->tool_stdout_snapshot,
                                                     &transaction->tool_stdout_handle)) {
        complete = 0;
    }
    if (transaction->tool_stderr_handle != INVALID_HANDLE_VALUE &&
        !cupidbuild_host_windows_dispose_retained_at(transaction->private_handle, "tool.stderr",
                                                     &transaction->tool_stderr_snapshot,
                                                     &transaction->tool_stderr_handle)) {
        complete = 0;
    }
    for (index = 0u; index < transaction->input_count; index++) {
        if (!cupidbuild_host_basename(name, sizeof(name), transaction->inputs[index].frozen_path) ||
            (transaction->inputs[index].frozen_handle != INVALID_HANDLE_VALUE &&
             !cupidbuild_host_windows_dispose_read_retained_at(
                 transaction->private_handle, name, &transaction->inputs[index].frozen_snapshot,
                 &transaction->inputs[index].frozen_handle))) {
            complete = 0;
        }
    }
    return complete != 0 && cupidbuild_host_delete_runner_directory(transaction);
}
#else
static int cupidbuild_host_remove_runner_private(cupidbuild_host_transaction_t *transaction) {
    return transaction->private_created == 0;
}
#endif

static int cupidbuild_host_private_root_is_owned(cupidbuild_host_transaction_t *transaction) {
    cupidbuild_host_snapshot_t pinned;
    cupidbuild_host_snapshot_t entry;
    if (transaction->private_created == 0) {
        return 1;
    }
#if defined(_WIN32)
    {
        HANDLE opened;
        HANDLE repository = transaction->runner_transaction != 0
                                ? transaction->working_directory_handle
                                : transaction->repository_root_handle;
        int valid;
        if (transaction->private_handle == INVALID_HANDLE_VALUE ||
            !cupidbuild_host_windows_directory_handle_snapshot(transaction->private_handle,
                                                               &pinned)) {
            return 0;
        }
        opened = cupidbuild_host_windows_open_relative(repository, transaction->private_name, 1, 0);
        valid = opened != INVALID_HANDLE_VALUE &&
                cupidbuild_host_windows_directory_handle_snapshot(opened, &entry);
        if (opened != INVALID_HANDLE_VALUE) {
            (void)CloseHandle(opened);
        }
        if (!valid) {
            return 0;
        }
    }
#else
    if (transaction->private_flat != 0) {
        cupidbuild_host_snapshot_t reservation;
        if (transaction->private_descriptor != transaction->repository_root_descriptor ||
            transaction->private_reservation_descriptor < 0 ||
            !cupidbuild_host_directory_descriptor_snapshot(transaction->repository_root_descriptor,
                                                           &pinned) ||
            !cupidbuild_host_read_open_file(transaction->private_reservation_descriptor,
                                            CUPIDBUILD_HOST_FILE_LIMIT, &reservation,
                                            (unsigned char **)0) ||
            !cupidbuild_host_regular_snapshot_at(transaction->repository_root_descriptor,
                                                 transaction->private_name, &entry)) {
            return 0;
        }
        return cupidbuild_host_snapshot_identity_equal(&pinned,
                                                       &transaction->repository_root_snapshot) &&
               cupidbuild_host_snapshot_equal(&reservation,
                                              &transaction->private_reservation_snapshot) &&
               cupidbuild_host_snapshot_equal(&entry, &transaction->private_reservation_snapshot);
    }
    if (transaction->private_descriptor < 0 ||
        !cupidbuild_host_directory_descriptor_snapshot(transaction->private_descriptor, &pinned) ||
        !cupidbuild_host_directory_entry_snapshot_at(transaction->repository_root_descriptor,
                                                     transaction->private_name, &entry)) {
        return 0;
    }
#endif
    return cupidbuild_host_snapshot_identity_equal(&pinned, &transaction->private_root_snapshot) &&
           cupidbuild_host_snapshot_identity_equal(&entry, &transaction->private_root_snapshot);
}

#if defined(_WIN32)
static int cupidbuild_host_windows_relative_directory_missing(HANDLE parent, const char *name) {
    long status = 0;
    HANDLE handle =
        cupidbuild_host_windows_open_relative_access_status(parent, name, 1, 0, 0u, &status);
    unsigned long unsigned_status = (unsigned long)status;
    if (handle != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(handle);
        return 0;
    }
    return unsigned_status == 0xc000000fu || unsigned_status == 0xc0000034u ||
           unsigned_status == 0xc000003au;
}
#else
static int cupidbuild_host_directory_entry_missing_at(int directory, const char *name) {
#if defined(CUPIDBUILD_CUSTOM_LINUX)
    unsigned char information[96];
    return cupid_linux_syscall4(CUPIDBUILD_LINUX_SYS_FSTATAT64, (unsigned int)directory,
                                (unsigned int)name, (unsigned int)information,
                                CUPIDBUILD_LINUX_AT_SYMLINK_NOFOLLOW) == -CUPIDBUILD_LINUX_ENOENT;
#else
    struct stat information;
    int result = fstatat(directory, name, &information, AT_SYMLINK_NOFOLLOW);
    return result != 0 && errno == ENOENT;
#endif
}
#endif

static int cupidbuild_host_remove_owned_private_root(cupidbuild_host_transaction_t *transaction) {
    if (transaction->private_created == 0) {
        return 1;
    }
    if (!cupidbuild_host_private_root_is_owned(transaction)) {
        return 0;
    }
#if defined(_WIN32)
    if (!cupidbuild_host_windows_dispose_handle(transaction->private_handle)) {
        transaction->private_handle = INVALID_HANDLE_VALUE;
        return 0;
    }
    transaction->private_handle = INVALID_HANDLE_VALUE;
    if (!cupidbuild_host_windows_relative_directory_missing(transaction->repository_root_handle,
                                                            transaction->private_name)) {
        return 0;
    }
#else
    if (transaction->private_flat != 0) {
        if (!cupidbuild_host_cleanup_retained_regular_at(
                transaction->repository_root_descriptor, transaction->private_name,
                &transaction->private_reservation_snapshot,
                &transaction->private_reservation_descriptor)) {
            return 0;
        }
        transaction->private_created = 0;
        return 1;
    }
    {
        char moved_name[128];
        cupidbuild_host_snapshot_t moved;
        cupidbuild_host_snapshot_t source;
        unsigned int process_id = cupidbuild_host_process_id();
        unsigned int attempt;
        int moved_entry = 0;
        int removed;
        for (attempt = 0u; attempt < 16u; attempt++) {
            int written = snprintf(moved_name, sizeof(moved_name), "%s.cleanup-%08x-%02x",
                                   transaction->private_name, process_id, attempt);
            if (written <= 0 || (size_t)written >= sizeof(moved_name) ||
                !cupidbuild_host_directory_entry_snapshot_at(
                    transaction->repository_root_descriptor, transaction->private_name, &source) ||
                !cupidbuild_host_snapshot_identity_equal(&source,
                                                         &transaction->private_root_snapshot)) {
                break;
            }
            if (cupidbuild_host_rename_entry_noreplace_at(
                    transaction->repository_root_descriptor, transaction->private_name,
                    transaction->repository_root_descriptor, moved_name)) {
                moved_entry = 1;
                break;
            }
        }
        if (moved_entry == 0 || !cupidbuild_host_directory_entry_snapshot_at(
                                    transaction->repository_root_descriptor, moved_name, &moved)) {
            return 0;
        }
        if (!cupidbuild_host_snapshot_identity_equal(&moved, &transaction->private_root_snapshot)) {
            (void)cupidbuild_host_rename_entry_noreplace_at(
                transaction->repository_root_descriptor, moved_name,
                transaction->repository_root_descriptor, transaction->private_name);
            return 0;
        }
        if (!cupidbuild_host_directory_entry_snapshot_at(transaction->repository_root_descriptor,
                                                         moved_name, &source) ||
            !cupidbuild_host_snapshot_identity_equal(&source,
                                                     &transaction->private_root_snapshot)) {
            (void)cupidbuild_host_rename_entry_noreplace_at(
                transaction->repository_root_descriptor, moved_name,
                transaction->repository_root_descriptor, transaction->private_name);
            return 0;
        }
#if defined(CUPIDBUILD_CUSTOM_LINUX)
        do {
            removed = cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_UNLINKAT,
                                           (unsigned int)transaction->repository_root_descriptor,
                                           (unsigned int)moved_name, CUPIDBUILD_LINUX_AT_REMOVEDIR);
        } while (removed == -CUPIDBUILD_LINUX_EINTR);
        removed = removed == 0;
#else
        do {
            removed = unlinkat(transaction->repository_root_descriptor, moved_name, AT_REMOVEDIR);
        } while (removed != 0 && errno == EINTR);
        removed = removed == 0;
#endif
        if (!removed || !cupidbuild_host_directory_entry_missing_at(
                            transaction->repository_root_descriptor, moved_name)) {
            if (!removed) {
                (void)cupidbuild_host_rename_entry_noreplace_at(
                    transaction->repository_root_descriptor, moved_name,
                    transaction->repository_root_descriptor, transaction->private_name);
            }
            return 0;
        }
        cupidbuild_host_close_directory(transaction->private_descriptor);
        transaction->private_descriptor = -1;
    }
#endif
    transaction->private_created = 0;
    return 1;
}

static int cupidbuild_host_close_discovery_directories(cupidbuild_host_transaction_t *transaction) {
    size_t index;
    int valid = 1;
    for (index = 0u; index < transaction->discovery_directory_count; index++) {
        cupidbuild_host_discovery_directory_t *directory =
            &transaction->discovery_directories[index];
#if defined(_WIN32)
        if (directory->handle != INVALID_HANDLE_VALUE && !CloseHandle(directory->handle)) {
            valid = 0;
        }
        directory->handle = INVALID_HANDLE_VALUE;
#else
#if defined(CUPIDBUILD_CUSTOM_LINUX)
        if (directory->descriptor >= 0 &&
            cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)directory->descriptor) <
                0) {
            valid = 0;
        }
#else
        if (directory->descriptor >= 0 && close(directory->descriptor) != 0) {
            valid = 0;
        }
#endif
        directory->descriptor = -1;
#endif
        free(directory->logical);
    }
    for (index = 0u; index < transaction->discovery_query_count; index++) {
        cupidbuild_host_discovery_query_close(&transaction->discovery_queries[index]);
    }
    transaction->discovery_query_count = 0u;
    free(transaction->discovery_directories);
    transaction->discovery_directories = (cupidbuild_host_discovery_directory_t *)0;
    transaction->discovery_directory_count = 0u;
    transaction->discovery_directory_capacity = 0u;
    return valid;
}

static int
cupidbuild_host_abandon_interfered_transaction(cupidbuild_host_transaction_t *transaction) {
    unsigned int index;
    (void)cupidbuild_host_close_discovery_directories(transaction);
#if defined(_WIN32)
    for (index = 0u; index < transaction->input_count; index++) {
        if (transaction->inputs[index].frozen_handle != INVALID_HANDLE_VALUE) {
            (void)CloseHandle(transaction->inputs[index].frozen_handle);
            transaction->inputs[index].frozen_handle = INVALID_HANDLE_VALUE;
        }
    }
    if (transaction->candidate_handle != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(transaction->candidate_handle);
    }
    if (transaction->private_output_handle != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(transaction->private_output_handle);
    }
    if (transaction->tool_stdout_handle != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(transaction->tool_stdout_handle);
    }
    if (transaction->tool_stderr_handle != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(transaction->tool_stderr_handle);
    }
    if (transaction->initial_output_handle != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(transaction->initial_output_handle);
    }
    if (transaction->lock_handle != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(transaction->lock_handle);
    }
    if (transaction->private_handle != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(transaction->private_handle);
    }
    if (transaction->working_directory_handle != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(transaction->working_directory_handle);
    }
    if (transaction->output_parent_handle != INVALID_HANDLE_VALUE &&
        transaction->output_parent_is_repository_root == 0) {
        (void)CloseHandle(transaction->output_parent_handle);
    }
    if (transaction->repository_root_handle != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(transaction->repository_root_handle);
    }
#else
    for (index = 0u; index < transaction->input_count; index++) {
        cupidbuild_host_close_directory(transaction->inputs[index].frozen_descriptor);
        transaction->inputs[index].frozen_descriptor = -1;
    }
    cupidbuild_host_close_directory(transaction->candidate_descriptor);
    cupidbuild_host_close_directory(transaction->initial_output_descriptor);
    cupidbuild_host_close_directory(transaction->private_output_descriptor);
    cupidbuild_host_close_directory(transaction->tool_stdout_descriptor);
    cupidbuild_host_close_directory(transaction->tool_stderr_descriptor);
    cupidbuild_host_close_directory(transaction->private_reservation_descriptor);
    cupidbuild_host_close_directory(transaction->working_directory_descriptor);
    if (transaction->private_descriptor >= 0 &&
        transaction->private_descriptor != transaction->repository_root_descriptor &&
        transaction->private_descriptor != transaction->output_parent_descriptor) {
        cupidbuild_host_close_directory(transaction->private_descriptor);
    }
    if (transaction->output_parent_descriptor >= 0 &&
        transaction->output_parent_is_repository_root == 0) {
        cupidbuild_host_close_directory(transaction->output_parent_descriptor);
    }
    cupidbuild_host_close_directory(transaction->repository_root_descriptor);
#endif
    cupidbuild_host_release_input_paths(transaction);
    free(transaction);
    return 0;
}

int cupidbuild_host_transaction_close(cupidbuild_host_transaction_t *transaction) {
    unsigned int index;
    if (transaction == (cupidbuild_host_transaction_t *)0) {
        return 1;
    }
    if (transaction->namespace_interfered != 0) {
        return cupidbuild_host_abandon_interfered_transaction(transaction);
    }
    if (transaction->runner_transaction != 0) {
        int cleanup_succeeded = 1;
        if (!cupidbuild_host_close_discovery_directories(transaction)) {
            cleanup_succeeded = 0;
        }
        if (transaction->private_created != 0 &&
            !cupidbuild_host_remove_runner_private(transaction)) {
            cleanup_succeeded = 0;
        }
#if defined(_WIN32)
        for (index = 0u; index < transaction->input_count; index++) {
            if (transaction->inputs[index].frozen_handle != INVALID_HANDLE_VALUE &&
                !CloseHandle(transaction->inputs[index].frozen_handle)) {
                cleanup_succeeded = 0;
            }
            transaction->inputs[index].frozen_handle = INVALID_HANDLE_VALUE;
        }
        if (transaction->private_handle != INVALID_HANDLE_VALUE) {
            if (!CloseHandle(transaction->private_handle)) {
                cleanup_succeeded = 0;
            }
        }
        if (transaction->working_directory_handle != INVALID_HANDLE_VALUE) {
            if (!CloseHandle(transaction->working_directory_handle)) {
                cleanup_succeeded = 0;
            }
        }
#else
        for (index = 0u; index < transaction->input_count; index++) {
            if (!cupidbuild_host_close_directory(transaction->inputs[index].frozen_descriptor)) {
                cleanup_succeeded = 0;
            }
            transaction->inputs[index].frozen_descriptor = -1;
        }
        if (!cupidbuild_host_close_directory(transaction->tool_stdout_descriptor)) {
            cleanup_succeeded = 0;
        }
        if (!cupidbuild_host_close_directory(transaction->tool_stderr_descriptor)) {
            cleanup_succeeded = 0;
        }
        if (!cupidbuild_host_close_directory(transaction->private_descriptor)) {
            cleanup_succeeded = 0;
        }
        if (!cupidbuild_host_close_directory(transaction->output_parent_descriptor)) {
            cleanup_succeeded = 0;
        }
#endif
        cupidbuild_host_release_input_paths(transaction);
        free(transaction);
        return cleanup_succeeded;
    }
    {
        int cleanup_succeeded = 1;
        int private_cleanup_succeeded = cupidbuild_host_private_root_is_owned(transaction);
        char name[CUPIDBUILD_HOST_PATH_BYTES];
        if (private_cleanup_succeeded == 0) {
            cleanup_succeeded = 0;
        }
        if (!cupidbuild_host_close_discovery_directories(transaction)) {
            cleanup_succeeded = 0;
        }
#if defined(_WIN32)
        if (transaction->candidate_handle != INVALID_HANDLE_VALUE) {
            if (transaction->candidate_published != 0) {
                if (!CloseHandle(transaction->candidate_handle)) {
                    cleanup_succeeded = 0;
                }
            } else if (transaction->candidate_snapshot.present != 0) {
                if (!cupidbuild_host_windows_dispose_retained_at(
                        transaction->private_handle, "candidate.o",
                        &transaction->candidate_snapshot, &transaction->candidate_handle)) {
                    cleanup_succeeded = 0;
                    private_cleanup_succeeded = 0;
                }
            } else if (!CloseHandle(transaction->candidate_handle)) {
                cleanup_succeeded = 0;
            }
            transaction->candidate_handle = INVALID_HANDLE_VALUE;
        }
#else
        if (transaction->candidate_descriptor >= 0) {
            if (transaction->candidate_published != 0) {
                if (!cupidbuild_host_close_retained_descriptor(
                        &transaction->candidate_descriptor)) {
                    cleanup_succeeded = 0;
                }
            } else if (!cupidbuild_host_cleanup_retained_regular_at(
                           transaction->private_descriptor,
                           cupidbuild_host_private_entry_name(transaction, "candidate.o"),
                           &transaction->candidate_snapshot, &transaction->candidate_descriptor)) {
                cleanup_succeeded = 0;
                private_cleanup_succeeded = 0;
            }
        }
#endif
        if (private_cleanup_succeeded != 0 && transaction->private_created != 0 &&
            !cupidbuild_host_delete_owned_private_name(transaction, "candidate.o",
                                                       &transaction->candidate_snapshot)) {
            cleanup_succeeded = 0;
            private_cleanup_succeeded = 0;
        }
        if (private_cleanup_succeeded != 0 && transaction->private_created != 0 &&
            transaction->candidate_publish_created != 0 &&
            !cupidbuild_host_delete_owned_private_name(transaction,
                                                       CUPIDBUILD_HOST_CANDIDATE_PUBLISH,
                                                       &transaction->candidate_publish_snapshot)) {
            cleanup_succeeded = 0;
            private_cleanup_succeeded = 0;
        }
#if !defined(_WIN32)
        if (transaction->private_flat != 0 && transaction->private_output_descriptor >= 0 &&
            !cupidbuild_host_close_retained_descriptor(&transaction->private_output_descriptor)) {
            cleanup_succeeded = 0;
        }
#endif
        if (private_cleanup_succeeded != 0 && transaction->private_created != 0 &&
#if !defined(_WIN32)
            transaction->private_flat == 0 &&
#endif
            !cupidbuild_host_close_private_output_handle(transaction)) {
            cleanup_succeeded = 0;
            private_cleanup_succeeded = 0;
        }
        if (private_cleanup_succeeded != 0 && transaction->private_created != 0 &&
            !transaction->private_output_transferred &&
            !cupidbuild_host_delete_owned_private_name(transaction, "candidate.map",
                                                       &transaction->private_output_snapshot)) {
            cleanup_succeeded = 0;
            private_cleanup_succeeded = 0;
        }
        if (!cupidbuild_host_close_private_stream_handles(transaction)) {
            cleanup_succeeded = 0;
            private_cleanup_succeeded = 0;
        }
        if (private_cleanup_succeeded != 0 && transaction->private_created != 0 &&
            !cupidbuild_host_delete_owned_private_name(transaction, "tool.stdout",
                                                       &transaction->tool_stdout_snapshot)) {
            cleanup_succeeded = 0;
            private_cleanup_succeeded = 0;
        }
        if (private_cleanup_succeeded != 0 && transaction->private_created != 0 &&
            !cupidbuild_host_delete_owned_private_name(transaction, "tool.stderr",
                                                       &transaction->tool_stderr_snapshot)) {
            cleanup_succeeded = 0;
            private_cleanup_succeeded = 0;
        }
        for (index = 0u; index < transaction->input_count; index++) {
            if (!cupidbuild_host_copy_text(name, sizeof(name),
                                           transaction->inputs[index].frozen_name)) {
                cleanup_succeeded = 0;
                private_cleanup_succeeded = 0;
                continue;
            }
#if defined(_WIN32)
            if (transaction->inputs[index].frozen_handle != INVALID_HANDLE_VALUE &&
                !cupidbuild_host_windows_dispose_read_retained_at(
                    transaction->private_handle, name, &transaction->inputs[index].frozen_snapshot,
                    &transaction->inputs[index].frozen_handle)) {
                cleanup_succeeded = 0;
                private_cleanup_succeeded = 0;
            }
#else
            if (transaction->inputs[index].frozen_descriptor >= 0 &&
                !(transaction->private_flat != 0
                      ? cupidbuild_host_close_retained_descriptor(
                            &transaction->inputs[index].frozen_descriptor)
                      : cupidbuild_host_cleanup_retained_regular_at(
                            transaction->private_descriptor, name,
                            &transaction->inputs[index].frozen_snapshot,
                            &transaction->inputs[index].frozen_descriptor))) {
                cleanup_succeeded = 0;
                private_cleanup_succeeded = 0;
            }
#endif
        }
#if defined(_WIN32)
        if (transaction->initial_output_handle != INVALID_HANDLE_VALUE) {
            if (transaction->initial_output_parked != 0 &&
                transaction->publication_committed == 0 &&
                !cupidbuild_host_windows_restore_initial_output(transaction)) {
                cleanup_succeeded = 0;
            }
            if (transaction->initial_output_parked != 0 &&
                transaction->publication_committed != 0) {
                cleanup_succeeded = 0;
            }
            if (transaction->initial_output_handle != INVALID_HANDLE_VALUE &&
                !CloseHandle(transaction->initial_output_handle)) {
                cleanup_succeeded = 0;
            }
            transaction->initial_output_handle = INVALID_HANDLE_VALUE;
        } else if (transaction->initial_output_parked != 0) {
            cleanup_succeeded = 0;
        }
#else
        if (transaction->initial_output_descriptor >= 0 &&
            !cupidbuild_host_close_retained_descriptor(&transaction->initial_output_descriptor)) {
            cleanup_succeeded = 0;
        }
        if (transaction->initial_output_parked != 0 &&
            !cupidbuild_host_posix_dispose_parked_output(transaction)) {
            cleanup_succeeded = 0;
        }
#endif
        if (!cupidbuild_host_release_lock(transaction)) {
            cleanup_succeeded = 0;
        }
        if (private_cleanup_succeeded != 0 &&
            !cupidbuild_host_remove_owned_private_root(transaction)) {
            cleanup_succeeded = 0;
            private_cleanup_succeeded = 0;
        }
#if defined(_WIN32)
        if (transaction->private_handle != (HANDLE)0 &&
            transaction->private_handle != INVALID_HANDLE_VALUE) {
            if (!CloseHandle(transaction->private_handle)) {
                cleanup_succeeded = 0;
            }
        }
        if (transaction->output_parent_handle != (HANDLE)0 &&
            transaction->output_parent_handle != INVALID_HANDLE_VALUE &&
            transaction->output_parent_is_repository_root == 0) {
            if (!CloseHandle(transaction->output_parent_handle)) {
                cleanup_succeeded = 0;
            }
        }
        if (transaction->repository_root_handle != (HANDLE)0 &&
            transaction->repository_root_handle != INVALID_HANDLE_VALUE) {
            if (!CloseHandle(transaction->repository_root_handle)) {
                cleanup_succeeded = 0;
            }
        }
#else
        if (transaction->private_reservation_descriptor >= 0 &&
            !cupidbuild_host_close_retained_descriptor(
                &transaction->private_reservation_descriptor)) {
            cleanup_succeeded = 0;
        }
        if (!cupidbuild_host_close_directory(transaction->working_directory_descriptor)) {
            cleanup_succeeded = 0;
        }
        if (transaction->private_flat == 0) {
            if (!cupidbuild_host_close_directory(transaction->private_descriptor)) {
                cleanup_succeeded = 0;
            }
        }
        if (transaction->output_parent_is_repository_root == 0) {
            if (!cupidbuild_host_close_directory(transaction->output_parent_descriptor)) {
                cleanup_succeeded = 0;
            }
        }
        if (!cupidbuild_host_close_directory(transaction->repository_root_descriptor)) {
            cleanup_succeeded = 0;
        }
#endif
        cupidbuild_host_release_input_paths(transaction);
        free(transaction);
        return cleanup_succeeded;
    }
}
