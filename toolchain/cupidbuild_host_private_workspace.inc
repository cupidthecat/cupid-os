/**
 * @file cupidbuild_host_private_workspace.inc
 * @brief Keep private transaction paths and discover source inputs.
 */

static const char *
cupidbuild_host_private_entry_name(const cupidbuild_host_transaction_t *transaction,
                                   const char *name) {
    if (transaction == (const cupidbuild_host_transaction_t *)0 || name == (const char *)0 ||
        transaction->private_flat == 0) {
        return name;
    }
    if (strcmp(name, "candidate.o") == 0) {
        return transaction->candidate_name;
    }
    if (strcmp(name, CUPIDBUILD_HOST_CANDIDATE_PUBLISH) == 0) {
        return transaction->candidate_publish_name;
    }
    if (strcmp(name, "candidate.map") == 0) {
        return transaction->private_output_name;
    }
    if (strcmp(name, "tool.stdout") == 0) {
        return transaction->tool_stdout_name;
    }
    if (strcmp(name, "tool.stderr") == 0) {
        return transaction->tool_stderr_name;
    }
    return name;
}

static int cupidbuild_host_private_entry_path(const cupidbuild_host_transaction_t *transaction,
                                              char *destination, size_t capacity,
                                              const char *name) {
    const char *entry_name = cupidbuild_host_private_entry_name(transaction, name);
    if (transaction == (const cupidbuild_host_transaction_t *)0 || destination == (char *)0 ||
        entry_name == (const char *)0) {
        return 0;
    }
#if !defined(_WIN32)
    if (transaction->private_flat != 0) {
        int descriptor = -1;
        int written;
        if (strcmp(entry_name, transaction->candidate_name) == 0) {
            descriptor = transaction->candidate_descriptor;
        } else if (strcmp(entry_name, transaction->private_output_name) == 0) {
            descriptor = transaction->private_output_descriptor;
        } else if (strcmp(entry_name, transaction->tool_stdout_name) == 0) {
            descriptor = transaction->tool_stdout_descriptor;
        } else if (strcmp(entry_name, transaction->tool_stderr_name) == 0) {
            descriptor = transaction->tool_stderr_descriptor;
        }
        if (descriptor < 0) {
            return 0;
        }
        written = snprintf(destination, capacity, "/proc/self/fd/%d", descriptor);
        return written >= 0 && (size_t)written < capacity;
    }
#endif
    return cupidbuild_host_join(destination, capacity, transaction->private_root, entry_name);
}

static int cupidbuild_host_read_private_regular(cupidbuild_host_transaction_t *transaction,
                                                const char *name, int optional, unsigned long long limit,
                                                cupidbuild_host_snapshot_t *snapshot,
                                                unsigned char **bytes_out) {
    if (transaction == (cupidbuild_host_transaction_t *)0 || transaction->runner_transaction != 0 ||
        name == (const char *)0 || name[0] == '\0') {
        return 0;
    }
    name = cupidbuild_host_private_entry_name(transaction, name);
#if defined(_WIN32)
    return cupidbuild_host_windows_read_relative_regular(transaction->private_handle, name,
                                                         optional, limit, snapshot, bytes_out);
#else
#if defined(CUPIDBUILD_CUSTOM_LINUX)
    return cupidbuild_linux_read_relative_regular(transaction->private_descriptor, name, optional,
                                                  limit, snapshot, bytes_out);
#else
    return cupidbuild_native_read_relative_regular(transaction->private_descriptor, name, optional,
                                                   limit, snapshot, bytes_out);
#endif
#endif
}

static int cupidbuild_host_write_private_exclusive(cupidbuild_host_transaction_t *transaction,
                                                   const char *name, const unsigned char *bytes,
                                                   size_t size, int inheritable,
                                                   cupidbuild_host_snapshot_t *snapshot,
#if defined(_WIN32)
                                                   HANDLE *handle_out) {
#else
                                                   int *descriptor_out) {
#endif
    if (transaction == (cupidbuild_host_transaction_t *)0 || transaction->runner_transaction != 0 ||
        transaction->private_created == 0 || !cupidbuild_host_private_root_is_owned(transaction)) {
        return 0;
    }
    name = cupidbuild_host_private_entry_name(transaction, name);
#if defined(_WIN32)
    return cupidbuild_host_windows_create_relative_regular(transaction->private_handle, name, bytes,
                                                           size, inheritable, snapshot, handle_out);
#else
    return cupidbuild_host_write_exclusive_at(transaction->private_descriptor, name, bytes, size,
                                              inheritable, snapshot, descriptor_out);
#endif
}

#if !defined(_WIN32)
static int cupidbuild_host_open_anonymous_artifact(const char *name,
                                                   cupidbuild_host_snapshot_t *snapshot,
                                                   int *descriptor_out) {
    char label[CUPIDBUILD_HOST_PATH_BYTES];
    if (!cupidbuild_host_open_anonymous(name, label, sizeof(label), descriptor_out) ||
        !cupidbuild_host_promote_retained_descriptor(descriptor_out) ||
        !cupidbuild_host_read_open_file(*descriptor_out, CUPIDBUILD_HOST_FILE_LIMIT, snapshot,
                                        (unsigned char **)0)) {
        cupidbuild_host_close_directory(*descriptor_out);
        *descriptor_out = -1;
        return 0;
    }
    return 1;
}

static int
cupidbuild_host_private_entry_is_anonymous(const cupidbuild_host_transaction_t *transaction,
                                           const char *name) {
    return transaction != (const cupidbuild_host_transaction_t *)0 &&
           transaction->private_flat != 0 && name != (const char *)0 &&
           strcmp(name, "candidate.o") != 0 && strcmp(name, CUPIDBUILD_HOST_CANDIDATE_PUBLISH) != 0;
}
#endif

static int cupidbuild_host_read_retained_private_regular(cupidbuild_host_transaction_t *transaction,
                                                         const char *name, unsigned long long limit,
#if defined(_WIN32)
                                                         HANDLE handle,
#else
                                                         int descriptor,
#endif
                                                         cupidbuild_host_snapshot_t *snapshot,
                                                         unsigned char **bytes_out) {
    cupidbuild_host_snapshot_t retained;
    cupidbuild_host_snapshot_t named;
    unsigned char *bytes = (unsigned char *)0;
#if defined(_WIN32)
    int valid = cupidbuild_host_windows_read_open_regular(
        handle, limit, &retained, bytes_out != (unsigned char **)0 ? &bytes : (unsigned char **)0);
#else
    int valid = descriptor >= 0 && cupidbuild_host_read_open_file(descriptor, limit, &retained,
                                                                  bytes_out != (unsigned char **)0
                                                                      ? &bytes
                                                                      : (unsigned char **)0);
#endif
    if (valid == 0 || retained.size > limit) {
        free(bytes);
        return 0;
    }
#if !defined(_WIN32)
    if (!cupidbuild_host_private_entry_is_anonymous(transaction, name)) {
#endif
        if (!cupidbuild_host_read_private_regular(transaction, name, 0, limit, &named,
                                                  (unsigned char **)0) ||
            !cupidbuild_host_snapshot_equal(&retained, &named)) {
            free(bytes);
            return 0;
        }
#if !defined(_WIN32)
    }
#endif
    *snapshot = retained;
    if (bytes_out != (unsigned char **)0) {
        *bytes_out = bytes;
    }
    return 1;
}

#if !defined(_WIN32)
static int cupidbuild_host_unlink_exact_entry_at(int directory, const char *name,
                                                 const cupidbuild_host_snapshot_t *expected) {
    cupidbuild_host_snapshot_t current;
    return cupidbuild_host_regular_snapshot_at_limit(directory, name, expected->size, &current) &&
           cupidbuild_host_snapshot_equal(&current, expected) &&
           cupidbuild_host_unlink_entry_at(directory, name);
}

static int cupidbuild_host_delete_owned_regular_at(int directory, const char *name,
                                                   const cupidbuild_host_snapshot_t *expected) {
    char retained_name[CUPIDBUILD_HOST_PATH_BYTES];
    char moved_name[CUPIDBUILD_HOST_PATH_BYTES];
    cupidbuild_host_snapshot_t current;
    cupidbuild_host_snapshot_t retained;
    cupidbuild_host_snapshot_t moved;
    unsigned int process_id = cupidbuild_host_process_id();
    int retained_written;
    int moved_written;
    int removed_moved;
    int removed_retained;
    int restored;
    (void)memset(&retained, 0, sizeof(retained));
    (void)memset(&moved, 0, sizeof(moved));
    if (directory < 0 || name == (const char *)0 || name[0] == '\0' ||
        expected == (const cupidbuild_host_snapshot_t *)0)
        return 0;
    if (!cupidbuild_host_regular_snapshot_at_limit(directory, name, expected->size, &current)) {
        cupidbuild_host_snapshot_t optional;
#if defined(CUPIDBUILD_CUSTOM_LINUX)
        if (!cupidbuild_linux_read_relative_regular(directory, name, 1, expected->size, &optional,
                                                    (unsigned char **)0)) {
#else
        if (!cupidbuild_native_read_relative_regular(directory, name, 1, expected->size, &optional,
                                                     (unsigned char **)0)) {
#endif
            return 0;
        }
        return optional.present == 0;
    }
    if (expected->present == 0 || !cupidbuild_host_snapshot_equal(&current, expected)) {
        return 0;
    }
    retained_written =
        snprintf(retained_name, sizeof(retained_name), ".%s.cleanup-link-%08x", name, process_id);
    moved_written =
        snprintf(moved_name, sizeof(moved_name), ".%s.cleanup-move-%08x", name, process_id);
    if (retained_written <= 0 || (size_t)retained_written >= sizeof(retained_name) ||
        moved_written <= 0 || (size_t)moved_written >= sizeof(moved_name) ||
        !cupidbuild_host_link_entry_at(directory, name, directory, retained_name)) {
        return 0;
    }
    if (!cupidbuild_host_regular_snapshot_at_limit(directory, retained_name, expected->size,
                                                   &retained) ||
        !cupidbuild_host_snapshot_equal(&retained, expected)) {
        (void)cupidbuild_host_unlink_exact_entry_at(directory, retained_name, expected);
        return 0;
    }
    if (!cupidbuild_host_rename_entry_noreplace_at(directory, name, directory, moved_name)) {
        (void)cupidbuild_host_unlink_exact_entry_at(directory, retained_name, expected);
        return 0;
    }
    if (!cupidbuild_host_regular_snapshot_at_limit(directory, moved_name, expected->size, &moved) ||
        !cupidbuild_host_snapshot_equal(&moved, expected)) {
        cupidbuild_host_snapshot_t restored_snapshot;
        restored = cupidbuild_host_link_entry_at(directory, retained_name, directory, name) &&
                   cupidbuild_host_regular_snapshot_at_limit(directory, name, expected->size,
                                                             &restored_snapshot) &&
                   cupidbuild_host_snapshot_equal(&restored_snapshot, expected);
        if (restored) {
            (void)cupidbuild_host_unlink_exact_entry_at(directory, retained_name, expected);
        }
        return 0;
    }
    removed_moved = cupidbuild_host_unlink_exact_entry_at(directory, moved_name, expected);
    removed_retained = cupidbuild_host_unlink_exact_entry_at(directory, retained_name, expected);
    return removed_moved && removed_retained;
}

static int cupidbuild_host_close_retained_descriptor(int *descriptor) {
    int result;
    if (descriptor == (int *)0 || *descriptor < 0) {
        return 0;
    }
#if defined(CUPIDBUILD_CUSTOM_LINUX)
    result = cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)*descriptor);
#else
    result = close(*descriptor);
#endif
    *descriptor = -1;
    return result == 0;
}

static int cupidbuild_host_cleanup_retained_regular_at(int directory, const char *name,
                                                       cupidbuild_host_snapshot_t *expected,
                                                       int *descriptor) {
    char retained_name[CUPIDBUILD_HOST_PATH_BYTES];
    char moved_name[CUPIDBUILD_HOST_PATH_BYTES];
    cupidbuild_host_snapshot_t opened;
    cupidbuild_host_snapshot_t named;
    cupidbuild_host_snapshot_t retained;
    cupidbuild_host_snapshot_t moved;
    unsigned int process_id = cupidbuild_host_process_id();
    int retained_written;
    int moved_written;
    int valid;
    int removed_moved;
    int removed_retained;
    int closed;
    retained_name[0] = '\0';
    moved_name[0] = '\0';
    if (directory < 0 || name == (const char *)0 || name[0] == '\0' ||
        expected == (cupidbuild_host_snapshot_t *)0 || descriptor == (int *)0 || *descriptor < 0) {
        return 0;
    }
    valid = expected->present != 0 && cupidbuild_host_open_regular_metadata(*descriptor, &opened) &&
            cupidbuild_host_regular_metadata_at(directory, name, &named) &&
            cupidbuild_host_snapshot_identity_equal(&opened, expected) &&
            cupidbuild_host_snapshot_identity_equal(&named, expected) &&
            cupidbuild_host_snapshot_identity_equal(&opened, &named);
    if (valid == 0) {
        (void)cupidbuild_host_close_retained_descriptor(descriptor);
        return 0;
    }
    retained_written =
        snprintf(retained_name, sizeof(retained_name), ".%s.cleanup-link-%08x", name, process_id);
    moved_written =
        snprintf(moved_name, sizeof(moved_name), ".%s.cleanup-move-%08x", name, process_id);
    if (retained_written <= 0 || (size_t)retained_written >= sizeof(retained_name) ||
        moved_written <= 0 || (size_t)moved_written >= sizeof(moved_name) ||
        !cupidbuild_host_link_open_file_at(*descriptor, directory, retained_name) ||
        !cupidbuild_host_regular_metadata_at(directory, retained_name, &retained) ||
        !cupidbuild_host_snapshot_identity_equal(&retained, expected)) {
        (void)cupidbuild_host_unlink_identity_entry_at(directory, retained_name, expected);
        (void)cupidbuild_host_close_retained_descriptor(descriptor);
        return 0;
    }
    if (!cupidbuild_host_rename_entry_noreplace_at(directory, name, directory, moved_name)) {
        (void)cupidbuild_host_unlink_identity_entry_at(directory, retained_name, expected);
        (void)cupidbuild_host_close_retained_descriptor(descriptor);
        return 0;
    }
    if (!cupidbuild_host_regular_metadata_at(directory, moved_name, &moved) ||
        !cupidbuild_host_snapshot_identity_equal(&moved, expected)) {
        cupidbuild_host_snapshot_t restored;
        int restored_expected =
            cupidbuild_host_link_entry_at(directory, retained_name, directory, name) &&
            cupidbuild_host_regular_metadata_at(directory, name, &restored) &&
            cupidbuild_host_snapshot_identity_equal(&restored, expected);
        if (restored_expected != 0) {
            (void)cupidbuild_host_unlink_identity_entry_at(directory, retained_name, expected);
        }
        (void)cupidbuild_host_close_retained_descriptor(descriptor);
        return 0;
    }
    removed_moved = cupidbuild_host_unlink_identity_entry_at(directory, moved_name, expected);
    removed_retained = cupidbuild_host_unlink_identity_entry_at(directory, retained_name, expected);
    closed = cupidbuild_host_close_retained_descriptor(descriptor);
    if (!removed_moved || !removed_retained || !closed) {
        return 0;
    }
    expected->present = 0;
    return 1;
}
#endif

static int cupidbuild_host_delete_owned_private_name(cupidbuild_host_transaction_t *transaction,
                                                     const char *name,
                                                     cupidbuild_host_snapshot_t *expected) {
    cupidbuild_host_snapshot_t current;
    if (transaction == (cupidbuild_host_transaction_t *)0 || transaction->runner_transaction != 0 ||
        name == (const char *)0 || expected == (cupidbuild_host_snapshot_t *)0 ||
        !cupidbuild_host_read_private_regular(transaction, name, 1, expected->size, &current,
                                              (unsigned char **)0)) {
        return 0;
    }
#if !defined(_WIN32)
    if (cupidbuild_host_private_entry_is_anonymous(transaction, name)) {
        expected->present = 0;
        return 1;
    }
#endif
    name = cupidbuild_host_private_entry_name(transaction, name);
    if (current.present == 0) {
        expected->present = 0;
        return 1;
    }
    if (expected->present == 0 || !cupidbuild_host_snapshot_equal(&current, expected)) {
        return 0;
    }
#if defined(_WIN32)
    {
        HANDLE handle = cupidbuild_host_windows_open_relative_access(transaction->private_handle,
                                                                     name, 0, 1, DELETE);
        cupidbuild_host_snapshot_t opened;
        cupidbuild_host_snapshot_t after;
        if (handle == INVALID_HANDLE_VALUE ||
            !cupidbuild_host_windows_read_open_regular(handle, expected->size, &opened,
                                                       (unsigned char **)0) ||
            !cupidbuild_host_snapshot_equal(&opened, expected)) {
            if (handle != INVALID_HANDLE_VALUE) {
                (void)CloseHandle(handle);
            }
            return 0;
        }
        if (!cupidbuild_host_windows_dispose_handle(handle) ||
            !cupidbuild_host_read_private_regular(transaction, name, 1, expected->size, &after,
                                                  (unsigned char **)0) ||
            after.present != 0) {
            return 0;
        }
    }
#else
    if (!cupidbuild_host_delete_owned_regular_at(transaction->private_descriptor, name, expected)) {
        return 0;
    }
#endif
    expected->present = 0;
    return 1;
}

static int
cupidbuild_host_close_private_stream_handles(cupidbuild_host_transaction_t *transaction) {
    int valid = 1;
#if defined(_WIN32)
    if (transaction->tool_stdout_handle != INVALID_HANDLE_VALUE) {
        if (!cupidbuild_host_windows_dispose_retained_at(transaction->private_handle, "tool.stdout",
                                                         &transaction->tool_stdout_snapshot,
                                                         &transaction->tool_stdout_handle)) {
            valid = 0;
        }
    }
    if (transaction->tool_stderr_handle != INVALID_HANDLE_VALUE) {
        if (!cupidbuild_host_windows_dispose_retained_at(transaction->private_handle, "tool.stderr",
                                                         &transaction->tool_stderr_snapshot,
                                                         &transaction->tool_stderr_handle)) {
            valid = 0;
        }
    }
#else
    if (transaction->tool_stdout_descriptor >= 0) {
        if (!(transaction->private_flat != 0
                  ? cupidbuild_host_close_retained_descriptor(&transaction->tool_stdout_descriptor)
                  : cupidbuild_host_cleanup_retained_regular_at(
                        transaction->private_descriptor,
                        cupidbuild_host_private_entry_name(transaction, "tool.stdout"),
                        &transaction->tool_stdout_snapshot,
                        &transaction->tool_stdout_descriptor))) {
            valid = 0;
        }
    }
    if (transaction->tool_stderr_descriptor >= 0) {
        if (!(transaction->private_flat != 0
                  ? cupidbuild_host_close_retained_descriptor(&transaction->tool_stderr_descriptor)
                  : cupidbuild_host_cleanup_retained_regular_at(
                        transaction->private_descriptor,
                        cupidbuild_host_private_entry_name(transaction, "tool.stderr"),
                        &transaction->tool_stderr_snapshot,
                        &transaction->tool_stderr_descriptor))) {
            valid = 0;
        }
    }
#endif
    transaction->tool_stdout_sealed = 0;
    transaction->tool_stderr_sealed = 0;
    return valid;
}

static int cupidbuild_host_prepare_private_streams(cupidbuild_host_transaction_t *transaction) {
    static const unsigned char empty[1] = {0u};
    if (transaction == (cupidbuild_host_transaction_t *)0 ||
#if defined(_WIN32)
        transaction->tool_stdout_handle != INVALID_HANDLE_VALUE ||
        transaction->tool_stderr_handle != INVALID_HANDLE_VALUE ||
#else
        transaction->tool_stdout_descriptor >= 0 || transaction->tool_stderr_descriptor >= 0 ||
#endif
        transaction->private_created == 0) {
        return 0;
    }
#if !defined(_WIN32)
    if (transaction->private_flat != 0) {
        if (!cupidbuild_host_open_anonymous_artifact("cupidbuild-stdout",
                                                     &transaction->tool_stdout_snapshot,
                                                     &transaction->tool_stdout_descriptor) ||
            !cupidbuild_host_open_anonymous_artifact("cupidbuild-stderr",
                                                     &transaction->tool_stderr_snapshot,
                                                     &transaction->tool_stderr_descriptor) ||
            !cupidbuild_host_private_entry_path(transaction, transaction->tool_stdout,
                                                sizeof(transaction->tool_stdout), "tool.stdout") ||
            !cupidbuild_host_private_entry_path(transaction, transaction->tool_stderr,
                                                sizeof(transaction->tool_stderr), "tool.stderr")) {
            (void)cupidbuild_host_close_retained_descriptor(&transaction->tool_stdout_descriptor);
            (void)cupidbuild_host_close_retained_descriptor(&transaction->tool_stderr_descriptor);
            (void)memset(&transaction->tool_stdout_snapshot, 0,
                         sizeof(transaction->tool_stdout_snapshot));
            (void)memset(&transaction->tool_stderr_snapshot, 0,
                         sizeof(transaction->tool_stderr_snapshot));
            return 0;
        }
        return 1;
    }
#endif
    if (!cupidbuild_host_write_private_exclusive(transaction, "tool.stdout", empty, 0u, 1,
                                                 &transaction->tool_stdout_snapshot,
#if defined(_WIN32)
                                                 &transaction->tool_stdout_handle
#else
                                                 &transaction->tool_stdout_descriptor
#endif
                                                 )) {
        return 0;
    }
    if (!cupidbuild_host_write_private_exclusive(transaction, "tool.stderr", empty, 0u, 1,
                                                 &transaction->tool_stderr_snapshot,
#if defined(_WIN32)
                                                 &transaction->tool_stderr_handle
#else
                                                 &transaction->tool_stderr_descriptor
#endif
                                                 )) {
#if defined(_WIN32)
        (void)cupidbuild_host_windows_dispose_retained_at(
            transaction->private_handle, "tool.stdout", &transaction->tool_stdout_snapshot,
            &transaction->tool_stdout_handle);
#else
        (void)cupidbuild_host_cleanup_retained_regular_at(
            transaction->private_descriptor,
            cupidbuild_host_private_entry_name(transaction, "tool.stdout"),
            &transaction->tool_stdout_snapshot, &transaction->tool_stdout_descriptor);
#endif
        return 0;
    }
    return cupidbuild_host_private_entry_path(transaction, transaction->tool_stdout,
                                              sizeof(transaction->tool_stdout), "tool.stdout") &&
           cupidbuild_host_private_entry_path(transaction, transaction->tool_stderr,
                                              sizeof(transaction->tool_stderr), "tool.stderr");
}

static int cupidbuild_host_prepare_private_stderr(cupidbuild_host_transaction_t *transaction) {
    static const unsigned char empty[1] = {0u};
    if (transaction == (cupidbuild_host_transaction_t *)0 || transaction->private_created == 0 ||
        !cupidbuild_host_private_root_is_owned(transaction)) {
        return 0;
    }
#if !defined(_WIN32)
    if (transaction->private_flat != 0) {
        if (!cupidbuild_host_open_anonymous_artifact("cupidbuild-stderr",
                                                     &transaction->tool_stderr_snapshot,
                                                     &transaction->tool_stderr_descriptor) ||
            !cupidbuild_host_private_entry_path(transaction, transaction->tool_stderr,
                                                sizeof(transaction->tool_stderr), "tool.stderr")) {
            (void)cupidbuild_host_close_retained_descriptor(&transaction->tool_stderr_descriptor);
            (void)memset(&transaction->tool_stderr_snapshot, 0,
                         sizeof(transaction->tool_stderr_snapshot));
            return 0;
        }
        return 1;
    }
#endif
    return cupidbuild_host_write_private_exclusive(transaction, "tool.stderr", empty, 0u, 1,
                                                   &transaction->tool_stderr_snapshot,
#if defined(_WIN32)
                                                   &transaction->tool_stderr_handle
#else
                                                   &transaction->tool_stderr_descriptor
#endif
                                                   ) &&
           cupidbuild_host_private_entry_path(transaction, transaction->tool_stderr,
                                              sizeof(transaction->tool_stderr), "tool.stderr");
}

static int cupidbuild_host_capture_private_streams(cupidbuild_host_transaction_t *transaction) {
    cupidbuild_host_snapshot_t standard_output;
    cupidbuild_host_snapshot_t standard_error;
#if defined(_WIN32)
    if ((transaction->tool_stdout_sealed == 0 &&
         !cupidbuild_host_windows_transition_retained_at(
             transaction->private_handle, "tool.stdout", &transaction->tool_stdout_snapshot,
             &transaction->tool_stdout_handle, 1, 0, 1)) ||
        (transaction->tool_stderr_sealed == 0 &&
         !cupidbuild_host_windows_transition_retained_at(
             transaction->private_handle, "tool.stderr", &transaction->tool_stderr_snapshot,
             &transaction->tool_stderr_handle, 1, 0, 1))) {
        return 0;
    }
    transaction->tool_stdout_sealed = 1;
    transaction->tool_stderr_sealed = 1;
#endif
#if !defined(_WIN32)
    if (transaction->private_flat != 0 &&
        ((transaction->tool_stdout_sealed == 0 &&
          !cupidbuild_host_seal_anonymous(transaction->tool_stdout_descriptor)) ||
         (transaction->tool_stderr_sealed == 0 &&
          !cupidbuild_host_seal_anonymous(transaction->tool_stderr_descriptor)))) {
        return 0;
    }
    transaction->tool_stdout_sealed = 1;
    transaction->tool_stderr_sealed = 1;
#endif
    if (!cupidbuild_host_read_retained_private_regular(transaction, "tool.stdout",
                                                       CUPIDBUILD_HOST_STREAM_LIMIT,
#if defined(_WIN32)
                                                       transaction->tool_stdout_handle,
#else
                                                       transaction->tool_stdout_descriptor,
#endif
                                                       &standard_output, (unsigned char **)0) ||
        !cupidbuild_host_read_retained_private_regular(transaction, "tool.stderr",
                                                       CUPIDBUILD_HOST_STREAM_LIMIT,
#if defined(_WIN32)
                                                       transaction->tool_stderr_handle,
#else
                                                       transaction->tool_stderr_descriptor,
#endif
                                                       &standard_error, (unsigned char **)0) ||
        !cupidbuild_host_snapshot_identity_equal(&standard_output,
                                                 &transaction->tool_stdout_snapshot) ||
        !cupidbuild_host_snapshot_identity_equal(&standard_error,
                                                 &transaction->tool_stderr_snapshot)) {
        return 0;
    }
    transaction->tool_stdout_snapshot = standard_output;
    transaction->tool_stderr_snapshot = standard_error;
    return 1;
}

static int cupidbuild_host_prepare_private_candidate(cupidbuild_host_transaction_t *transaction) {
    static const unsigned char empty[1] = {0u};
    if (transaction->candidate_snapshot.present != 0) {
#if defined(_WIN32)
        return transaction->candidate_handle != INVALID_HANDLE_VALUE &&
               cupidbuild_host_private_entry_path(transaction, transaction->candidate,
                                                  sizeof(transaction->candidate), "candidate.o");
#else
        return transaction->candidate_descriptor >= 0 &&
               cupidbuild_host_private_entry_path(transaction, transaction->candidate,
                                                  sizeof(transaction->candidate), "candidate.o");
#endif
    }
    return cupidbuild_host_write_private_exclusive(transaction, "candidate.o", empty, 0u, 0,
                                                   &transaction->candidate_snapshot,
#if defined(_WIN32)
                                                   &transaction->candidate_handle
#else
                                                   &transaction->candidate_descriptor
#endif
                                                   ) &&
           cupidbuild_host_private_entry_path(transaction, transaction->candidate,
                                              sizeof(transaction->candidate), "candidate.o");
}

static int cupidbuild_host_capture_retained_candidate(cupidbuild_host_transaction_t *transaction) {
    cupidbuild_host_snapshot_t candidate;
#if defined(_WIN32)
    if (transaction->candidate_sealed == 0 &&
        !cupidbuild_host_windows_transition_retained_at_limit(
            transaction->private_handle, "candidate.o", &transaction->candidate_snapshot,
            &transaction->candidate_handle, 1, 0, 1, transaction->candidate_limit)) {
        return 0;
    }
    transaction->candidate_sealed = 1;
#endif
    if (!cupidbuild_host_read_retained_private_regular(transaction, "candidate.o",
                                                       transaction->candidate_limit,
#if defined(_WIN32)
                                                       transaction->candidate_handle,
#else
                                                       transaction->candidate_descriptor,
#endif
                                                       &candidate, (unsigned char **)0) ||
        !cupidbuild_host_snapshot_identity_equal(&candidate, &transaction->candidate_snapshot) ||
        (transaction->candidate_captured != 0 &&
         !cupidbuild_host_snapshot_equal(&candidate, &transaction->candidate_snapshot))) {
        return 0;
    }
    transaction->candidate_snapshot = candidate;
    return 1;
}

static int cupidbuild_host_close_private_output_handle(cupidbuild_host_transaction_t *transaction) {
#if defined(_WIN32)
    if (transaction->private_output_handle != INVALID_HANDLE_VALUE) {
        int disposed = cupidbuild_host_windows_dispose_retained_at(
            transaction->private_handle, "candidate.map", &transaction->private_output_snapshot,
            &transaction->private_output_handle);
        transaction->private_output_sealed = 0;
        return disposed;
    }
#else
    if (transaction->private_output_descriptor >= 0) {
        int disposed =
            transaction->private_flat != 0
                ? cupidbuild_host_close_retained_descriptor(&transaction->private_output_descriptor)
                : cupidbuild_host_cleanup_retained_regular_at(
                      transaction->private_descriptor,
                      cupidbuild_host_private_entry_name(transaction, "candidate.map"),
                      &transaction->private_output_snapshot,
                      &transaction->private_output_descriptor);
        transaction->private_output_sealed = 0;
        return disposed;
    }
#endif
    transaction->private_output_sealed = 0;
    return 1;
}

static int
cupidbuild_host_prepare_private_output_stream(cupidbuild_host_transaction_t *transaction) {
    static const unsigned char empty[1] = {0u};
#if !defined(_WIN32)
    if (transaction->private_flat != 0) {
        if (!cupidbuild_host_open_anonymous_artifact("cupidbuild-output",
                                                     &transaction->private_output_snapshot,
                                                     &transaction->private_output_descriptor) ||
            !cupidbuild_host_private_entry_path(transaction, transaction->private_output,
                                                sizeof(transaction->private_output),
                                                "candidate.map")) {
            (void)cupidbuild_host_close_retained_descriptor(
                &transaction->private_output_descriptor);
            (void)memset(&transaction->private_output_snapshot, 0,
                         sizeof(transaction->private_output_snapshot));
            return 0;
        }
        return 1;
    }
#endif
    return cupidbuild_host_write_private_exclusive(transaction, "candidate.map", empty, 0u, 1,
                                                   &transaction->private_output_snapshot,
#if defined(_WIN32)
                                                   &transaction->private_output_handle
#else
                                                   &transaction->private_output_descriptor
#endif
                                                   ) &&
           cupidbuild_host_private_entry_path(transaction, transaction->private_output,
                                              sizeof(transaction->private_output), "candidate.map");
}

static int cupidbuild_host_prepare_private_output_path(cupidbuild_host_transaction_t *transaction) {
    static const unsigned char empty[1] = {0u};
    if (transaction->private_output_snapshot.present != 0) {
#if defined(_WIN32)
        return transaction->private_output_handle != INVALID_HANDLE_VALUE &&
               cupidbuild_host_private_entry_path(transaction, transaction->private_output,
                                                  sizeof(transaction->private_output),
                                                  "candidate.map");
#else
        return transaction->private_output_descriptor >= 0 &&
               cupidbuild_host_private_entry_path(transaction, transaction->private_output,
                                                  sizeof(transaction->private_output),
                                                  "candidate.map");
#endif
    }
#if !defined(_WIN32)
    if (transaction->private_flat != 0) {
        return cupidbuild_host_prepare_private_output_stream(transaction);
    }
#endif
    return cupidbuild_host_write_private_exclusive(transaction, "candidate.map", empty, 0u, 0,
                                                   &transaction->private_output_snapshot,
#if defined(_WIN32)
                                                   &transaction->private_output_handle
#else
                                                   &transaction->private_output_descriptor
#endif
                                                   ) &&
           cupidbuild_host_private_entry_path(transaction, transaction->private_output,
                                              sizeof(transaction->private_output), "candidate.map");
}

static size_t
cupidbuild_host_private_output_read_limit(const cupidbuild_host_transaction_t *transaction) {
    return transaction->private_output_limit != 0u ? transaction->private_output_limit
                                                   : CUPIDBUILD_HOST_FILE_LIMIT;
}

static int
cupidbuild_host_capture_retained_private_output(cupidbuild_host_transaction_t *transaction) {
    cupidbuild_host_snapshot_t output;
#if defined(_WIN32)
    if (transaction->private_output_sealed == 0 &&
        !cupidbuild_host_windows_transition_retained_at_limit(
            transaction->private_handle, "candidate.map", &transaction->private_output_snapshot,
            &transaction->private_output_handle, 1, 0, 1,
            cupidbuild_host_private_output_read_limit(transaction))) {
        return 0;
    }
    transaction->private_output_sealed = 1;
#endif
#if !defined(_WIN32)
    if (transaction->private_flat != 0 && transaction->private_output_sealed == 0) {
        if (!cupidbuild_host_seal_anonymous(transaction->private_output_descriptor)) {
            return 0;
        }
        transaction->private_output_sealed = 1;
    }
#endif
    if (!cupidbuild_host_read_retained_private_regular(transaction, "candidate.map",
                                                       transaction->private_output_limit != 0u
                                                           ? transaction->private_output_limit
                                                           : CUPIDBUILD_HOST_STREAM_LIMIT,
#if defined(_WIN32)
                                                       transaction->private_output_handle,
#else
                                                       transaction->private_output_descriptor,
#endif
                                                       &output, (unsigned char **)0) ||
        !cupidbuild_host_snapshot_identity_equal(&output, &transaction->private_output_snapshot) ||
        (transaction->private_output_captured != 0 &&
         !cupidbuild_host_snapshot_equal(&output, &transaction->private_output_snapshot))) {
        return 0;
    }
    transaction->private_output_snapshot = output;
    return 1;
}

const char *
cupidbuild_host_profile_parent_error(const cupidbuild_host_profile_parent_t *preparation) {
    if (preparation == (const cupidbuild_host_profile_parent_t *)0 ||
        preparation->error[0] == '\0') {
        return "profile parent preparation could not be allocated";
    }
    return preparation->error;
}

int cupidbuild_host_seed_members_exact(cupidbuild_host_transaction_t *transaction,
                                       const char *directory, const char *suffix,
                                       const char *const *expected, size_t expected_count) {
    const char *logical_directory;
    if (transaction == (cupidbuild_host_transaction_t *)0 || directory == (const char *)0 ||
        directory[0] == '\0' || suffix == (const char *)0 || suffix[0] == '\0' ||
        expected == (const char *const *)0 || expected_count == 0u) {
        return 0;
    }
    if (transaction->runner_transaction != 0) {
        return cupidbuild_host_seed_members_platform(directory, suffix, expected, expected_count);
    }
    return cupidbuild_host_repository_logical_path(transaction, directory, &logical_directory) &&
           cupidbuild_host_seed_members_repository(transaction, logical_directory, suffix, expected,
                                                   expected_count);
}

static void cupidbuild_host_discovery_query_close(cupidbuild_host_discovery_query_t *query) {
    cupidbuild_host_path_list_close(&query->roots);
    cupidbuild_host_path_list_close(&query->suffixes);
    cupidbuild_host_path_list_close(&query->expected);
}

static int cupidbuild_host_remember_discovery(cupidbuild_host_transaction_t *transaction,
                                              const char *const *roots, size_t root_count,
                                              const char *const *suffixes, size_t suffix_count,
                                              int skip_hidden_files, int reject_matching_nonfiles,
                                              const cupidbuild_host_path_list_t *paths) {
    cupidbuild_host_discovery_query_t *query;
    cupidbuild_host_snapshot_t empty;
    size_t index;
    if (transaction->compile_discovery == 0) {
        return 1;
    }
    if (transaction->discovery_query_count >= 16u) {
        return 0;
    }
    query = &transaction->discovery_queries[transaction->discovery_query_count];
    (void)memset(&empty, 0, sizeof(empty));
    query->skip_hidden_files = skip_hidden_files;
    query->reject_matching_nonfiles = reject_matching_nonfiles;
    for (index = 0u; index < root_count; index++) {
        if (!cupidbuild_host_path_list_add(&query->roots, roots[index], &empty,
                                           CUPIDBUILD_HOST_DISCOVERY_DIRECTORIES)) {
            goto failed;
        }
    }
    for (index = 0u; index < suffix_count; index++) {
        if (!cupidbuild_host_path_list_add(&query->suffixes, suffixes[index], &empty, 16u)) {
            goto failed;
        }
    }
    for (index = 0u; index < paths->count; index++) {
        if (!cupidbuild_host_discovery_add(&query->expected, paths->paths[index],
                                           &paths->snapshots[index])) {
            goto failed;
        }
    }
    transaction->discovery_query_count++;
    return 1;
failed:
    cupidbuild_host_discovery_query_close(query);
    return 0;
}

int cupidbuild_host_discover_files(cupidbuild_host_transaction_t *transaction,
                                   const char *const *logical_roots, size_t root_count,
                                   const char *const *suffixes, size_t suffix_count,
                                   int skip_hidden_files, int reject_matching_nonfiles,
                                   cupidbuild_host_path_list_t *paths_out) {
    size_t index;
    if (paths_out == (cupidbuild_host_path_list_t *)0) {
        return 0;
    }
    (void)memset(paths_out, 0, sizeof(*paths_out));
    if (transaction == (cupidbuild_host_transaction_t *)0 || transaction->runner_transaction != 0 ||
        (transaction->discovery_sealed != 0 && transaction->compile_discovery != 0) ||
        logical_roots == (const char *const *)0 || root_count == 0u ||
        suffixes == (const char *const *)0 || suffix_count == 0u) {
        return 0;
    }
    for (index = 0u; index < root_count; index++) {
        if (!cupidbuild_host_path_is_relative_safe(logical_roots[index]) ||
            !cupidbuild_host_discover_platform(transaction, logical_roots[index], suffixes,
                                               suffix_count, skip_hidden_files,
                                               reject_matching_nonfiles, paths_out)) {
            cupidbuild_host_path_list_close(paths_out);
            return 0;
        }
    }
    if (cupidbuild_host_discovery_sort(paths_out) &&
        cupidbuild_host_remember_discovery(transaction, logical_roots, root_count, suffixes,
                                           suffix_count, skip_hidden_files,
                                           reject_matching_nonfiles, paths_out)) {
        return 1;
    }
    cupidbuild_host_path_list_close(paths_out);
    return 0;
}

int cupidbuild_host_seal_discovery(cupidbuild_host_transaction_t *transaction) {
    if (transaction == (cupidbuild_host_transaction_t *)0 || transaction->runner_transaction != 0 ||
        transaction->discovery_directory_count == 0u || transaction->compile_discovery != 0) {
        return 0;
    }
    transaction->discovery_sealed = 1;
    return 1;
}

int cupidbuild_host_begin_compile_discovery(cupidbuild_host_transaction_t *transaction) {
    if (transaction == (cupidbuild_host_transaction_t *)0 || transaction->runner_transaction != 0 ||
        transaction->discovery_sealed != 0 || transaction->discovery_directory_count != 0u) {
        return 0;
    }
    transaction->compile_discovery = 1;
    return 1;
}

int cupidbuild_host_seal_compile_discovery(cupidbuild_host_transaction_t *transaction) {
    if (transaction == (cupidbuild_host_transaction_t *)0 || transaction->runner_transaction != 0 ||
        transaction->compile_discovery == 0 || transaction->discovery_query_count == 0u ||
        transaction->discovery_directory_count == 0u) {
        return 0;
    }
    transaction->discovery_sealed = 1;
    return 1;
}

static int cupidbuild_host_require_discovery_queries(cupidbuild_host_transaction_t *transaction) {
    size_t query_index;
    for (query_index = 0u; query_index < transaction->discovery_query_count; query_index++) {
        cupidbuild_host_discovery_query_t *query = &transaction->discovery_queries[query_index];
        cupidbuild_host_path_list_t current;
        size_t index;
        int valid = 1;
        (void)memset(&current, 0, sizeof(current));
        for (index = 0u; index < query->roots.count; index++) {
            if (!cupidbuild_host_discover_platform(transaction, query->roots.paths[index],
                                                   (const char *const *)query->suffixes.paths,
                                                   query->suffixes.count, query->skip_hidden_files,
                                                   query->reject_matching_nonfiles, &current)) {
                valid = 0;
                break;
            }
        }
        if (valid != 0 &&
            (!cupidbuild_host_discovery_sort(&current) || current.count != query->expected.count)) {
            valid = 0;
        }
        for (index = 0u; valid != 0 && index < current.count; index++) {
            if (strcmp(current.paths[index], query->expected.paths[index]) != 0 ||
                !cupidbuild_host_snapshot_equal(&current.snapshots[index],
                                                &query->expected.snapshots[index])) {
                valid = 0;
            }
        }
        cupidbuild_host_path_list_close(&current);
        if (valid == 0) {
            return 0;
        }
    }
    return 1;
}

#if defined(CUPIDBUILD_PROFILE_DIRECTORY_RACE_TEST) && !defined(CUPIDBUILD_CUSTOM_LINUX)
static int cupidbuild_host_profile_directory_test_pause(const char *ready_variable,
                                                        const char *resume_variable) {
    const char *ready = getenv(ready_variable);
    const char *resume = getenv(resume_variable);
    FILE *signal;
    unsigned int attempt;
    if (ready == (const char *)0 || resume == (const char *)0) {
        return 1;
    }
    signal = fopen(ready, "wb");
    if (signal == (FILE *)0 || fclose(signal) != 0) {
        return 0;
    }
    for (attempt = 0u; attempt < 30000u; attempt++) {
        FILE *permission = fopen(resume, "rb");
        if (permission != (FILE *)0) {
            return fclose(permission) == 0;
        }
#if defined(_WIN32)
        Sleep(1u);
#else
        {
            struct timespec pause_time;
            pause_time.tv_sec = 0;
            pause_time.tv_nsec = 1000000L;
            (void)nanosleep(&pause_time, (struct timespec *)0);
        }
#endif
    }
    return 0;
}
#endif

static int
cupidbuild_host_require_discovery_directory(cupidbuild_host_transaction_t *transaction,
                                            cupidbuild_host_discovery_directory_t *directory) {
    cupidbuild_host_snapshot_t retained;
    cupidbuild_host_snapshot_t named;
#if defined(_WIN32)
    HANDLE opened;
    if (!cupidbuild_host_windows_named_directory_snapshot(transaction, directory->logical,
                                                          directory->handle, &retained, (int *)0)) {
        return 0;
    }
    opened = cupidbuild_host_windows_open_relative_path(transaction->repository_root_handle,
                                                        directory->logical, 1, 1);
    if (!cupidbuild_host_windows_named_directory_snapshot(transaction, directory->logical, opened,
                                                          &named, (int *)0)) {
        if (opened != INVALID_HANDLE_VALUE) {
            (void)CloseHandle(opened);
        }
        return 0;
    }
    if (!CloseHandle(opened)) {
        return 0;
    }
#else
    int opened;
#if defined(CUPIDBUILD_CUSTOM_LINUX)
    if (!cupidbuild_linux_descriptor_snapshot(directory->descriptor, &retained)) {
        return 0;
    }
    opened = cupidbuild_linux_open_relative_path(transaction->repository_root_descriptor,
                                                 directory->logical, 1);
    if (!cupidbuild_linux_descriptor_snapshot(opened, &named)) {
        if (opened >= 0) {
            (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)opened);
        }
        return 0;
    }
    if (cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)opened) < 0) {
        return 0;
    }
#else
    if (!cupidbuild_native_directory_descriptor_snapshot(directory->descriptor, &retained)) {
        return 0;
    }
    opened = cupidbuild_native_open_relative_path(transaction->repository_root_descriptor,
                                                  directory->logical, 1);
    if (!cupidbuild_native_directory_descriptor_snapshot(opened, &named)) {
        if (opened >= 0) {
            (void)close(opened);
        }
        return 0;
    }
    if (close(opened) != 0) {
        return 0;
    }
#endif
#endif
    if (!cupidbuild_host_discovery_snapshot_equal(transaction, &directory->snapshot, &retained) ||
        !cupidbuild_host_discovery_snapshot_equal(transaction, &directory->snapshot, &named)) {
        return 0;
    }
    return 1;
}

static int
cupidbuild_host_require_discovery_directory_pass(cupidbuild_host_transaction_t *transaction) {
    size_t index;
    for (index = 0u; index < transaction->discovery_directory_count; index++) {
        if (!cupidbuild_host_require_discovery_directory(
                transaction, &transaction->discovery_directories[index])) {
            return 0;
        }
    }
    return 1;
}

static int
cupidbuild_host_require_discovery_directories(cupidbuild_host_transaction_t *transaction) {
#if defined(CUPIDBUILD_PROFILE_DIRECTORY_RACE_TEST) && !defined(CUPIDBUILD_CUSTOM_LINUX)
    transaction->discovery_boundary_count++;
    if (transaction->discovery_boundary_count == (transaction->compile_discovery != 0 ? 1u : 2u) &&
        !cupidbuild_host_profile_directory_test_pause("CUPIDBUILD_PROFILE_TEST_DIRECTORY_READY",
                                                      "CUPIDBUILD_PROFILE_TEST_DIRECTORY_RESUME")) {
        return 0;
    }
#endif
    if (transaction->discovery_directory_count != 0u && transaction->discovery_sealed == 0) {
        return 0;
    }
    if (!cupidbuild_host_require_discovery_directory_pass(transaction)) {
        return 0;
    }
#if defined(CUPIDBUILD_PROFILE_DIRECTORY_RACE_TEST) && !defined(CUPIDBUILD_CUSTOM_LINUX)
    if (transaction->discovery_boundary_count == (transaction->compile_discovery != 0 ? 1u : 2u) &&
        !cupidbuild_host_profile_directory_test_pause(
            "CUPIDBUILD_PROFILE_TEST_DIRECTORY_AFTER_FIRST_PASS_READY",
            "CUPIDBUILD_PROFILE_TEST_DIRECTORY_AFTER_FIRST_PASS_RESUME")) {
        return 0;
    }
#endif
    return cupidbuild_host_require_discovery_queries(transaction) &&
           cupidbuild_host_require_discovery_directory_pass(transaction);
}

static int cupidbuild_host_write_lock_exclusive(cupidbuild_host_transaction_t *transaction,
                                                const unsigned char *bytes, size_t size,
                                                cupidbuild_host_snapshot_t *snapshot) {
#if defined(_WIN32)
    return cupidbuild_host_windows_create_lock(transaction, bytes, size, snapshot);
#else
    return cupidbuild_host_write_lock_exclusive_at(transaction, bytes, size, snapshot);
#endif
}

static int cupidbuild_host_read_lock(const cupidbuild_host_transaction_t *transaction, int optional,
                                     cupidbuild_host_snapshot_t *snapshot,
                                     unsigned char **bytes_out) {
#if defined(_WIN32)
    return cupidbuild_host_windows_read_relative_regular(
        transaction->output_parent_handle, transaction->lock_name, optional,
        CUPIDBUILD_HOST_FILE_LIMIT, snapshot, bytes_out);
#else
#if defined(CUPIDBUILD_CUSTOM_LINUX)
    return cupidbuild_linux_read_relative_regular(transaction->output_parent_descriptor,
                                                  transaction->lock_name, optional,
                                                  CUPIDBUILD_HOST_FILE_LIMIT, snapshot, bytes_out);
#else
    return cupidbuild_native_read_relative_regular(transaction->output_parent_descriptor,
                                                   transaction->lock_name, optional,
                                                   CUPIDBUILD_HOST_FILE_LIMIT, snapshot, bytes_out);
#endif
#endif
}

static int cupidbuild_host_reclaim_lock(cupidbuild_host_transaction_t *transaction,
                                        const cupidbuild_host_snapshot_t *expected,
                                        unsigned int owner) {
#if defined(_WIN32)
    (void)owner;
    return cupidbuild_host_windows_reclaim_lock(transaction, expected);
#else
    return cupidbuild_host_reclaim_lock_at(transaction, expected, owner);
#endif
}

static int cupidbuild_host_read_owner(cupidbuild_host_transaction_t *transaction,
                                      unsigned int *owner_out,
                                      cupidbuild_host_snapshot_t *snapshot_out) {
    cupidbuild_host_snapshot_t snapshot;
    unsigned char *bytes = (unsigned char *)0;
    unsigned int owner = 0u;
    size_t index;
    if (!cupidbuild_host_read_lock(transaction, 0, &snapshot, &bytes) || snapshot.size < 2u ||
        snapshot.size > 16u || bytes[snapshot.size - 1u] != '\n') {
        free(bytes);
        return 0;
    }
    for (index = 0u; index + 1u < snapshot.size; index++) {
        unsigned int digit;
        if (bytes[index] < '0' || bytes[index] > '9') {
            free(bytes);
            return 0;
        }
        digit = (unsigned int)(bytes[index] - '0');
        if (owner > (4294967295u - digit) / 10u) {
            free(bytes);
            return 0;
        }
        owner = owner * 10u + digit;
    }
    free(bytes);
    if (owner == 0u) {
        return 0;
    }
    *owner_out = owner;
    if (snapshot_out != (cupidbuild_host_snapshot_t *)0) {
        *snapshot_out = snapshot;
    }
    return 1;
}

static int cupidbuild_host_acquire_lock(cupidbuild_host_transaction_t *transaction) {
    char owner_text[32];
    unsigned int attempt;
    unsigned int process_id = cupidbuild_host_process_id();
    int owner_written = snprintf(owner_text, sizeof(owner_text), "%u\n", process_id);
    if (owner_written <= 0 || (size_t)owner_written >= sizeof(owner_text)) {
        return 0;
    }
    for (attempt = 0u; attempt < 4u; attempt++) {
        if (cupidbuild_host_write_lock_exclusive(transaction, (const unsigned char *)owner_text,
                                                 (size_t)owner_written,
                                                 &transaction->lock_snapshot)) {
            transaction->lock_held = 1;
            return 1;
        }
        {
            unsigned int owner = 0u;
            cupidbuild_host_snapshot_t stale_snapshot;
            if (!cupidbuild_host_read_owner(transaction, &owner, &stale_snapshot)) {
                cupidbuild_host_set_error(transaction,
                                          "publication lock is not a regular owner file");
                return 0;
            }
            if (cupidbuild_host_process_alive(owner)) {
                cupidbuild_host_set_error(transaction,
                                          "publication lock is held by a live process");
                return 0;
            }
            if (!cupidbuild_host_reclaim_lock(transaction, &stale_snapshot, owner)) {
                cupidbuild_host_set_error(transaction,
                                          "publication lock could not enter stale recovery");
                return 0;
            }
        }
    }
    cupidbuild_host_set_error(transaction, "stale publication lock could not be reclaimed");
    return 0;
}

static int cupidbuild_host_lock_is_unchanged(const cupidbuild_host_transaction_t *transaction) {
    cupidbuild_host_snapshot_t current;
    return transaction != (const cupidbuild_host_transaction_t *)0 && transaction->lock_held != 0 &&
           cupidbuild_host_read_lock(transaction, 0, &current, (unsigned char **)0) &&
           cupidbuild_host_lock_snapshot_equal(&current, &transaction->lock_snapshot);
}

static int cupidbuild_host_require_lock(cupidbuild_host_transaction_t *transaction) {
    if (!cupidbuild_host_lock_is_unchanged(transaction)) {
        cupidbuild_host_set_error(transaction, "publication lock changed while checked tools ran");
        return 0;
    }
    return 1;
}

static int cupidbuild_host_release_lock(cupidbuild_host_transaction_t *transaction) {
    if (transaction == (cupidbuild_host_transaction_t *)0 || transaction->lock_held == 0) {
        return 1;
    }
#if defined(_WIN32)
    if (!cupidbuild_host_windows_release_lock(transaction)) {
        return 0;
    }
#else
    if (!cupidbuild_host_release_lock_at(transaction)) {
        return 0;
    }
#endif
    transaction->lock_held = 0;
    return 1;
}

static const cupidbuild_host_input_t *cupidbuild_host_find_input_identity(
    const cupidbuild_host_transaction_t *transaction, const cupidbuild_host_snapshot_t *snapshot) {
    unsigned int index;
    for (index = 0u; index < transaction->input_count; index++) {
        const cupidbuild_host_input_t *input = &transaction->inputs[index];
        if (cupidbuild_host_snapshot_identity_equal(&input->snapshot, snapshot))
            return input;
    }
    return NULL;
}

static int cupidbuild_host_register_input(cupidbuild_host_transaction_t *transaction,
                                          const char *live_path, const char *frozen_path,
                                          const char *frozen_name,
                                          const cupidbuild_host_snapshot_t *snapshot,
                                          unsigned long long limit) {
    cupidbuild_host_input_t *input;
    size_t live_size = strlen(live_path) + 1u;
    size_t frozen_size = strlen(frozen_path) + 1u;
    size_t name_size = strlen(frozen_name) + 1u;
    char *paths;

    if (live_size > CUPIDBUILD_HOST_PATH_BYTES || frozen_size > CUPIDBUILD_HOST_PATH_BYTES ||
        name_size > CUPIDBUILD_HOST_PATH_BYTES) {
        cupidbuild_host_set_error(transaction, "transaction input path is too long");
        return 0;
    }
    if (transaction->input_count >= transaction->input_limit) {
        cupidbuild_host_set_error(transaction, "too many frozen transaction inputs");
        return 0;
    }
    if (transaction->input_count == transaction->input_capacity) {
        size_t capacity =
            transaction->input_capacity == 0u ? 16u : (size_t)transaction->input_capacity * 2u;
        if (capacity > transaction->input_limit) {
            capacity = transaction->input_limit;
        }
        if (!cupidbuild_host_reserve_inputs(transaction, capacity)) {
            return 0;
        }
    }
    paths = (char *)malloc(live_size + frozen_size + name_size);
    if (!paths) {
        cupidbuild_host_set_error(transaction, "transaction input paths cannot be allocated");
        return 0;
    }
    input = &transaction->inputs[transaction->input_count];
    input->live_path = paths;
    input->frozen_path = paths + live_size;
    input->frozen_name = paths + live_size + frozen_size;
    (void)memcpy(input->live_path, live_path, live_size);
    (void)memcpy(input->frozen_path, frozen_path, frozen_size);
    (void)memcpy(input->frozen_name, frozen_name, name_size);
#if defined(_WIN32)
    input->frozen_handle = INVALID_HANDLE_VALUE;
#else
    input->frozen_descriptor = -1;
#endif
    input->snapshot = *snapshot;
    input->limit = limit;
    input->previous_output = 0;
    input->external_input = 0;
    input->frozen_tool_output = 0;
    transaction->input_count++;
    return 1;
}
