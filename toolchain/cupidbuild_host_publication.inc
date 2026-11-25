/**
 * @file cupidbuild_host_publication.inc
 * @brief Capture the completed candidate and publish it through the retained parent.
 */

static int cupidbuild_host_read_candidate(cupidbuild_host_transaction_t *transaction,
                                          cupidbuild_host_snapshot_t *snapshot,
                                          unsigned char **bytes_out) {
    if (transaction->candidate_store_failed ||
        transaction->candidate_store_state == CUPIDBUILD_CANDIDATE_STORE_WRITING)
        return 0;

    unsigned long long limit = transaction->candidate_limit;
    if (bytes_out != (unsigned char **)0 && limit > CUPIDBUILD_HOST_FILE_LIMIT)
        limit = CUPIDBUILD_HOST_FILE_LIMIT;
#if defined(_WIN32)
    if (transaction->candidate_handle == INVALID_HANDLE_VALUE) {
        transaction->candidate_handle = cupidbuild_host_windows_open_relative_access(
            transaction->private_handle, "candidate.o", 0, 1, DELETE);
    }
    return cupidbuild_host_windows_read_open_regular(transaction->candidate_handle, limit, snapshot,
                                                     bytes_out);
#else
            const char *candidate_name =
                cupidbuild_host_private_entry_name(transaction, "candidate.o");
            if (transaction->candidate_descriptor < 0) {
#if defined(CUPIDBUILD_CUSTOM_LINUX)
                transaction->candidate_descriptor = cupidbuild_linux_open_relative(
                    transaction->private_descriptor, candidate_name, 0);
#else
                transaction->candidate_descriptor = cupidbuild_native_open_relative(
                    transaction->private_descriptor, candidate_name, 0);
#endif
            }
            return transaction->candidate_descriptor >= 0 &&
                   cupidbuild_host_read_open_file(transaction->candidate_descriptor, limit,
                                                  snapshot, bytes_out);
#endif
}

static int
cupidbuild_host_candidate_identity_allowed(const cupidbuild_host_transaction_t *transaction,
                                           const cupidbuild_host_snapshot_t *candidate) {
    unsigned int index;
    if (candidate == (const cupidbuild_host_snapshot_t *)0 || candidate->present == 0 ||
        (transaction->initial_output_snapshot.present != 0 &&
         cupidbuild_host_snapshot_identity_equal(candidate,
                                                 &transaction->initial_output_snapshot)) ||
        (transaction->lock_snapshot.present != 0 &&
         cupidbuild_host_snapshot_identity_equal(candidate, &transaction->lock_snapshot))) {
        return 0;
    }
    for (index = 0u; index < transaction->input_count; index++) {
        if ((transaction->inputs[index].snapshot.present != 0 &&
             cupidbuild_host_snapshot_identity_equal(candidate,
                                                     &transaction->inputs[index].snapshot)) ||
            (transaction->inputs[index].frozen_snapshot.present != 0 &&
             cupidbuild_host_snapshot_identity_equal(
                 candidate, &transaction->inputs[index].frozen_snapshot))) {
            return 0;
        }
    }
    return 1;
}

static int
cupidbuild_host_prepare_candidate_publication(cupidbuild_host_transaction_t *transaction,
                                              const cupidbuild_host_snapshot_t *candidate) {
#if defined(_WIN32)
    transaction->candidate_publish_snapshot = *candidate;
    return 1;
#else
            cupidbuild_host_snapshot_t alias;
            const char *publish_name =
                cupidbuild_host_private_entry_name(transaction, CUPIDBUILD_HOST_CANDIDATE_PUBLISH);
            if (transaction->candidate_publish_created == 0) {
                if (!cupidbuild_host_link_open_file_at(transaction->candidate_descriptor,
                                                       transaction->private_descriptor,
                                                       publish_name)) {
                    return 0;
                }
                transaction->candidate_publish_created = 1;
                transaction->candidate_publish_snapshot = *candidate;
            }
            if (!cupidbuild_host_regular_snapshot_at_limit(transaction->private_descriptor,
                                                           publish_name,
                                                           transaction->candidate_limit, &alias)) {
                return 0;
            }
            if (!cupidbuild_host_snapshot_equal(&alias, candidate)) {
                return 0;
            }
            return 1;
#endif
}

static int
cupidbuild_host_candidate_ready_for_publication(cupidbuild_host_transaction_t *transaction) {
    cupidbuild_host_snapshot_t retained;
    if (transaction == (cupidbuild_host_transaction_t *)0 || transaction->candidate_captured == 0 ||
        !cupidbuild_host_read_candidate(transaction, &retained, (unsigned char **)0) ||
        !cupidbuild_host_snapshot_equal(&retained, &transaction->candidate_snapshot) ||
        !cupidbuild_host_candidate_identity_allowed(transaction, &retained)) {
        return 0;
    }
#if !defined(_WIN32)
    const char *publish_name =
        cupidbuild_host_private_entry_name(transaction, CUPIDBUILD_HOST_CANDIDATE_PUBLISH);
    if (transaction->candidate_publish_created == 0 ||
        !cupidbuild_host_regular_snapshot_at_limit(transaction->private_descriptor, publish_name,
                                                   transaction->candidate_limit, &retained) ||
        !cupidbuild_host_snapshot_equal(&retained, &transaction->candidate_publish_snapshot)) {
        return 0;
    }
#endif
    return 1;
}

static int cupidbuild_host_published_candidate_matches(cupidbuild_host_transaction_t *transaction) {
    cupidbuild_host_snapshot_t retained;
    cupidbuild_host_snapshot_t published;
    return cupidbuild_host_read_candidate(transaction, &retained, (unsigned char **)0) &&
           cupidbuild_host_snapshot_equal(&retained, &transaction->candidate_snapshot) &&
           cupidbuild_host_read_output(transaction, 0, &published, (unsigned char **)0) &&
           cupidbuild_host_snapshot_equal(&published, &transaction->candidate_snapshot);
}

#if !defined(_WIN32)
static int cupidbuild_host_rename_exchange_at(int left_directory, const char *left_name,
                                              int right_directory, const char *right_name) {
    int result;
#if defined(CUPIDBUILD_CUSTOM_LINUX)
    do {
        result = cupid_linux_syscall5(CUPIDBUILD_LINUX_SYS_RENAMEAT2, (unsigned int)left_directory,
                                      (unsigned int)left_name, (unsigned int)right_directory,
                                      (unsigned int)right_name, CUPIDBUILD_LINUX_RENAME_EXCHANGE);
    } while (result == -CUPIDBUILD_LINUX_EINTR);
#else
    do {
        result = renameat2(left_directory, left_name, right_directory, right_name, RENAME_EXCHANGE);
    } while (result != 0 && errno == EINTR);
#endif
    return result == 0;
}

static int cupidbuild_host_posix_park_initial_output(cupidbuild_host_transaction_t *transaction) {
    cupidbuild_host_snapshot_t parked;
    cupidbuild_host_snapshot_t current;
    unsigned int attempt;
    if (transaction->initial_output_snapshot.present == 0 ||
        transaction->initial_output_parked != 0) {
        return 0;
    }
    for (attempt = 0u; attempt < CUPIDBUILD_HOST_PRIVATE_ATTEMPTS; attempt++) {
        int written = snprintf(transaction->initial_output_backup_name,
                               sizeof(transaction->initial_output_backup_name),
                               ".cupidbuild-old-%08x-%08x", cupidbuild_host_process_id(), attempt);
        if (written < 0 || (size_t)written >= sizeof(transaction->initial_output_backup_name)) {
            return 0;
        }
        if (cupidbuild_host_link_entry_at(
                transaction->output_parent_descriptor, transaction->output_name,
                transaction->output_parent_descriptor, transaction->initial_output_backup_name)) {
            transaction->initial_output_parked = 1;
            break;
        }
    }
    if (transaction->initial_output_parked == 0 ||
        !cupidbuild_host_regular_snapshot_at_limit(transaction->output_parent_descriptor,
                                                   transaction->initial_output_backup_name,
                                                   transaction->candidate_limit, &parked) ||
        !cupidbuild_host_read_output(transaction, 0, &current, (unsigned char **)0) ||
        !cupidbuild_host_snapshot_equal(&parked, &transaction->initial_output_snapshot) ||
        !cupidbuild_host_snapshot_equal(&current, &transaction->initial_output_snapshot)) {
        if (transaction->initial_output_parked != 0 &&
            cupidbuild_host_delete_owned_regular_at(transaction->output_parent_descriptor,
                                                    transaction->initial_output_backup_name,
                                                    &transaction->initial_output_snapshot)) {
            transaction->initial_output_parked = 0;
            transaction->initial_output_backup_name[0] = '\0';
        }
        return 0;
    }
    return 1;
}

static int cupidbuild_host_posix_dispose_parked_output(cupidbuild_host_transaction_t *transaction) {
    if (transaction->initial_output_parked == 0) {
        return 1;
    }
    if (transaction->initial_output_backup_name[0] == '\0' ||
        !cupidbuild_host_delete_owned_regular_at(transaction->output_parent_descriptor,
                                                 transaction->initial_output_backup_name,
                                                 &transaction->initial_output_snapshot)) {
        return 0;
    }
    transaction->initial_output_parked = 0;
    transaction->initial_output_backup_name[0] = '\0';
    return 1;
}

static int cupidbuild_host_posix_atomic_replace(cupidbuild_host_transaction_t *transaction) {
    const char *publish_name =
        cupidbuild_host_private_entry_name(transaction, CUPIDBUILD_HOST_CANDIDATE_PUBLISH);
    cupidbuild_host_snapshot_t current_output;
    cupidbuild_host_snapshot_t published;
    cupidbuild_host_snapshot_t swapped;
    (void)memset(&current_output, 0, sizeof(current_output));
    (void)memset(&published, 0, sizeof(published));
    (void)memset(&swapped, 0, sizeof(swapped));
    if (!cupidbuild_host_candidate_ready_for_publication(transaction)) {
        cupidbuild_host_set_error(transaction, "checked output is no longer ready for publication");
        return 0;
    }
    if (!cupidbuild_host_require_public_binding(transaction, &transaction->initial_output_snapshot,
                                                1)) {
        return 0;
    }
#if defined(CUPIDBUILD_PUBLICATION_RACE_TEST) && !defined(CUPIDBUILD_CUSTOM_LINUX)
    if (!cupidbuild_host_publication_test_pause("before-mutation")) {
        cupidbuild_host_set_error(transaction, "publication test checkpoint could not resume");
        return 0;
    }
#endif
    if (!cupidbuild_host_read_output(transaction, 1, &current_output, (unsigned char **)0) ||
        !cupidbuild_host_snapshot_equal(&current_output, &transaction->initial_output_snapshot)) {
        cupidbuild_host_set_error(transaction, "output changed immediately before publication");
        return 0;
    }
    if (transaction->initial_output_snapshot.present != 0) {
        int candidate_verified;
        int public_binding;
        int used_exchange = cupidbuild_host_rename_exchange_at(
            transaction->private_descriptor, publish_name, transaction->output_parent_descriptor,
            transaction->output_name);
        if (!used_exchange) {
            if (!cupidbuild_host_posix_park_initial_output(transaction) ||
                !cupidbuild_host_rename_entry_at(transaction->private_descriptor, publish_name,
                                                 transaction->output_parent_descriptor,
                                                 transaction->output_name)) {
                (void)cupidbuild_host_posix_dispose_parked_output(transaction);
                cupidbuild_host_set_error(transaction,
                                          "checked output could not replace the current output");
                return 0;
            }
            transaction->candidate_publish_created = 0;
        }
        transaction->candidate_published = 1;
        {
            int published_read =
                cupidbuild_host_read_output(transaction, 0, &published, (unsigned char **)0);
            int swapped_read = used_exchange ? cupidbuild_host_regular_snapshot_at_limit(
                                                   transaction->private_descriptor, publish_name,
                                                   transaction->candidate_limit, &swapped)
                                             : cupidbuild_host_regular_snapshot_at_limit(
                                                   transaction->output_parent_descriptor,
                                                   transaction->initial_output_backup_name,
                                                   transaction->candidate_limit, &swapped);
            candidate_verified =
#if defined(CUPIDBUILD_PUBLICATION_RACE_TEST) && !defined(CUPIDBUILD_CUSTOM_LINUX)
                cupidbuild_host_publication_test_pause("after-install") &&
#endif
                published_read && swapped_read &&
                cupidbuild_host_snapshot_equal(&published, &transaction->candidate_snapshot) &&
                cupidbuild_host_snapshot_equal(&swapped, &transaction->initial_output_snapshot) &&
                cupidbuild_host_published_candidate_matches(transaction);
            public_binding =
                candidate_verified && cupidbuild_host_require_public_binding(
                                          transaction, &transaction->candidate_snapshot, 1);
            if (!public_binding) {
                cupidbuild_host_snapshot_t before_restore;
                cupidbuild_host_snapshot_t before_alias_restore;
                cupidbuild_host_snapshot_t restored_alias;
                int restored =
                    candidate_verified &&
                    cupidbuild_host_snapshot_equal(&published, &transaction->candidate_snapshot) &&
                    cupidbuild_host_snapshot_equal(&swapped,
                                                   &transaction->initial_output_snapshot) &&
                    cupidbuild_host_read_output(transaction, 0, &before_restore,
                                                (unsigned char **)0) &&
                    cupidbuild_host_snapshot_equal(&before_restore,
                                                   &transaction->candidate_snapshot) &&
                    (used_exchange
                         ? (cupidbuild_host_regular_snapshot_at_limit(
                                transaction->private_descriptor, publish_name,
                                transaction->candidate_limit, &before_alias_restore) &&
                            cupidbuild_host_snapshot_equal(&before_alias_restore,
                                                           &transaction->initial_output_snapshot) &&
                            cupidbuild_host_rename_exchange_at(
                                transaction->private_descriptor, publish_name,
                                transaction->output_parent_descriptor, transaction->output_name))
                         : (cupidbuild_host_regular_snapshot_at_limit(
                                transaction->output_parent_descriptor,
                                transaction->initial_output_backup_name,
                                transaction->candidate_limit, &before_alias_restore) &&
                            cupidbuild_host_snapshot_equal(&before_alias_restore,
                                                           &transaction->initial_output_snapshot) &&
                            cupidbuild_host_rename_entry_at(transaction->output_parent_descriptor,
                                                            transaction->initial_output_backup_name,
                                                            transaction->output_parent_descriptor,
                                                            transaction->output_name)));
                cupidbuild_host_snapshot_t restored_output;
                if (!restored ||
                    !cupidbuild_host_read_output(transaction, 0, &restored_output,
                                                 (unsigned char **)0) ||
                    !(used_exchange
                          ? cupidbuild_host_regular_snapshot_at_limit(
                                transaction->private_descriptor, publish_name,
                                transaction->candidate_limit, &restored_alias)
                          : cupidbuild_host_regular_snapshot_at_limit(
                                transaction->private_descriptor,
                                cupidbuild_host_private_entry_name(transaction, "candidate.o"),
                                transaction->candidate_limit, &restored_alias)) ||
                    !cupidbuild_host_snapshot_equal(&restored_output,
                                                    &transaction->initial_output_snapshot) ||
                    !cupidbuild_host_snapshot_equal(&restored_alias,
                                                    &transaction->candidate_snapshot)) {
                    transaction->namespace_interfered = 1;
                    return 0;
                }
                transaction->candidate_published = 0;
                transaction->candidate_snapshot = restored_alias;
                if (used_exchange) {
                    transaction->candidate_publish_created = 1;
                    transaction->candidate_publish_snapshot = restored_alias;
                } else {
                    transaction->initial_output_parked = 0;
                    transaction->initial_output_backup_name[0] = '\0';
                    transaction->candidate_publish_created = 0;
                    transaction->candidate_publish_snapshot.present = 0;
                }
                if (!cupidbuild_host_require_public_binding(
                        transaction, &transaction->initial_output_snapshot, 0)) {
                    transaction->namespace_interfered = 1;
                }
                return 0;
            }
        }
        if (used_exchange) {
            transaction->candidate_publish_snapshot = swapped;
        } else {
            transaction->candidate_publish_snapshot.present = 0;
        }
    } else {
        int candidate_verified;
        int public_binding;
        int installation = cupidbuild_host_rename_entry_noreplace_status_at(
            transaction->private_descriptor, publish_name, transaction->output_parent_descriptor,
            transaction->output_name);
        if (installation != CUPIDBUILD_HOST_NOREPLACE_COMPLETE) {
            if (installation == CUPIDBUILD_HOST_NOREPLACE_DESTINATION_LINKED) {
                transaction->namespace_interfered = 1;
            }
            cupidbuild_host_set_error(transaction,
                                      "checked output could not claim the missing output");
            return 0;
        }
        transaction->candidate_publish_created = 0;
        transaction->candidate_published = 1;
        candidate_verified =
#if defined(CUPIDBUILD_PUBLICATION_RACE_TEST) && !defined(CUPIDBUILD_CUSTOM_LINUX)
            cupidbuild_host_publication_test_pause("after-install") &&
#endif
            cupidbuild_host_read_output(transaction, 0, &published, (unsigned char **)0) &&
            cupidbuild_host_snapshot_equal(&published, &transaction->candidate_snapshot) &&
            cupidbuild_host_published_candidate_matches(transaction);
        public_binding =
            candidate_verified && cupidbuild_host_require_public_binding(
                                      transaction, &transaction->candidate_snapshot, 1);
        if (!public_binding) {
            cupidbuild_host_snapshot_t restored_alias;
            cupidbuild_host_snapshot_t restored_missing;
            int restored = candidate_verified &&
                           cupidbuild_host_rename_entry_noreplace_at(
                               transaction->output_parent_descriptor, transaction->output_name,
                               transaction->private_descriptor, publish_name);
            if (!restored ||
                !cupidbuild_host_regular_snapshot_at_limit(
                    transaction->private_descriptor, publish_name, transaction->candidate_limit,
                    &restored_alias) ||
                !cupidbuild_host_snapshot_equal(&restored_alias,
                                                &transaction->candidate_snapshot) ||
                !cupidbuild_host_read_output(transaction, 1, &restored_missing,
                                             (unsigned char **)0) ||
                restored_missing.present != 0) {
                transaction->namespace_interfered = 1;
                return 0;
            }
            transaction->candidate_published = 0;
            transaction->candidate_publish_created = 1;
            transaction->candidate_snapshot = restored_alias;
            transaction->candidate_publish_snapshot = restored_alias;
            if (!cupidbuild_host_require_public_binding(transaction,
                                                        &transaction->initial_output_snapshot, 0)) {
                transaction->namespace_interfered = 1;
            }
            return 0;
        }
        transaction->candidate_publish_snapshot.present = 0;
    }
    transaction->candidate_published = 1;
    transaction->publication_committed = 1;
    if (transaction->initial_output_parked != 0) {
        (void)cupidbuild_host_posix_dispose_parked_output(transaction);
    }
    return 1;
}
#endif

int cupidbuild_host_capture_candidate(cupidbuild_host_transaction_t *transaction,
                                      cupidbuild_host_snapshot_t *snapshot_out,
                                      unsigned char **bytes_out) {
    cupidbuild_host_snapshot_t candidate;
    unsigned char *bytes = (unsigned char *)0;
    if (bytes_out != (unsigned char **)0) {
        *bytes_out = (unsigned char *)0;
    }
    if (transaction == (cupidbuild_host_transaction_t *)0 || transaction->candidate_captured != 0 ||
        !cupidbuild_host_read_candidate(transaction, &candidate,
                                        bytes_out != (unsigned char **)0 ? &bytes
                                                                         : (unsigned char **)0) ||
        (transaction->candidate_snapshot.present != 0 &&
         !cupidbuild_host_snapshot_equal(&candidate, &transaction->candidate_snapshot)) ||
        !cupidbuild_host_candidate_identity_allowed(transaction, &candidate) ||
        !cupidbuild_host_prepare_candidate_publication(transaction, &candidate)) {
        free(bytes);
        cupidbuild_host_set_error(transaction, "checked output cannot be pinned");
        return 0;
    }
    transaction->candidate_snapshot = candidate;
    transaction->candidate_captured = 1;
    if (bytes_out != (unsigned char **)0) {
        *bytes_out = bytes;
    }
    if (snapshot_out != (cupidbuild_host_snapshot_t *)0) {
        *snapshot_out = transaction->candidate_snapshot;
    }
    return 1;
}

int cupidbuild_host_require_candidate(cupidbuild_host_transaction_t *transaction,
                                      const cupidbuild_host_snapshot_t *expected) {
    cupidbuild_host_snapshot_t current;
    if (transaction == (cupidbuild_host_transaction_t *)0 ||
        expected == (const cupidbuild_host_snapshot_t *)0 ||
        !cupidbuild_host_read_candidate(transaction, &current, (unsigned char **)0) ||
        !cupidbuild_host_snapshot_equal(&current, expected) ||
        !cupidbuild_host_candidate_identity_allowed(transaction, &current) ||
        !cupidbuild_host_candidate_ready_for_publication(transaction)) {
        cupidbuild_host_set_error(transaction, "checked output changed while validation ran");
        return 0;
    }
    return 1;
}

int cupidbuild_host_capture_private_output(cupidbuild_host_transaction_t *transaction,
                                           cupidbuild_host_snapshot_t *snapshot_out,
                                           unsigned char **bytes_out) {
    cupidbuild_host_snapshot_t captured;
    unsigned char *bytes = (unsigned char *)0;
    int read_captured;
    size_t limit;
    if (bytes_out != (unsigned char **)0) {
        *bytes_out = (unsigned char *)0;
    }
    if (transaction == (cupidbuild_host_transaction_t *)0 || transaction->private_output_transferred) {
        cupidbuild_host_set_error(transaction, "checked private output cannot be pinned");
        return 0;
    }
    limit = cupidbuild_host_private_output_read_limit(transaction);
#if defined(_WIN32)
    read_captured =
        transaction->private_output_handle != INVALID_HANDLE_VALUE
            ? cupidbuild_host_read_retained_private_regular(
                  transaction, "candidate.map", limit, transaction->private_output_handle,
                  &captured, bytes_out != (unsigned char **)0 ? &bytes : (unsigned char **)0)
            : cupidbuild_host_read_private_regular(
                  transaction, "candidate.map", 0, limit, &captured,
                  bytes_out != (unsigned char **)0 ? &bytes : (unsigned char **)0);
#else
            read_captured =
                transaction->private_output_descriptor >= 0
                    ? cupidbuild_host_read_retained_private_regular(
                          transaction, "candidate.map", limit,
                          transaction->private_output_descriptor, &captured,
                          bytes_out != (unsigned char **)0 ? &bytes : (unsigned char **)0)
                    : cupidbuild_host_read_private_regular(
                          transaction, "candidate.map", 0, limit, &captured,
                          bytes_out != (unsigned char **)0 ? &bytes : (unsigned char **)0);
#endif
    if (!read_captured ||
        (transaction->private_output_snapshot.present != 0 &&
         !cupidbuild_host_snapshot_equal(&captured, &transaction->private_output_snapshot))) {
        free(bytes);
        cupidbuild_host_set_error(transaction, "checked private output cannot be pinned");
        return 0;
    }
    transaction->private_output_snapshot = captured;
    transaction->private_output_captured = 1;
    if (bytes_out != (unsigned char **)0) {
        *bytes_out = bytes;
    }
    if (snapshot_out != (cupidbuild_host_snapshot_t *)0) {
        *snapshot_out = transaction->private_output_snapshot;
    }
    return 1;
}

int cupidbuild_host_require_private_output(cupidbuild_host_transaction_t *transaction,
                                           const cupidbuild_host_snapshot_t *expected) {
    cupidbuild_host_snapshot_t current;
    int read_current = 0;
    if (transaction != (cupidbuild_host_transaction_t *)0 &&
        transaction->private_output_captured != 0 &&
        expected != (const cupidbuild_host_snapshot_t *)0) {
        size_t limit = cupidbuild_host_private_output_read_limit(transaction);
#if defined(_WIN32)
        read_current =
            transaction->private_output_handle != INVALID_HANDLE_VALUE
                ? cupidbuild_host_read_retained_private_regular(transaction, "candidate.map", limit,
                                                                transaction->private_output_handle,
                                                                &current, (unsigned char **)0)
                : cupidbuild_host_read_private_regular(transaction, "candidate.map", 0, limit,
                                                       &current, (unsigned char **)0);
#else
                read_current =
                    transaction->private_output_descriptor >= 0
                        ? cupidbuild_host_read_retained_private_regular(
                              transaction, "candidate.map", limit,
                              transaction->private_output_descriptor, &current, (unsigned char **)0)
                        : cupidbuild_host_read_private_regular(transaction, "candidate.map", 0,
                                                               limit, &current,
                                                               (unsigned char **)0);
#endif
    }
    if (!read_current || !cupidbuild_host_snapshot_equal(&current, expected)) {
        cupidbuild_host_set_error(transaction,
                                  "checked private output changed while validation ran");
        return 0;
    }
    return 1;
}

int cupidbuild_host_write_private_output(cupidbuild_host_transaction_t *transaction,
                                         const unsigned char *bytes, size_t size) {
    return cupidbuild_host_write_private_output_bounded(transaction, bytes, size,
                                                        CUPIDBUILD_HOST_FILE_LIMIT);
}

int cupidbuild_host_write_private_output_bounded(cupidbuild_host_transaction_t *transaction,
                                                 const unsigned char *bytes, size_t size,
                                                 size_t capacity) {
    cupidbuild_host_snapshot_t snapshot;
    if (transaction == (cupidbuild_host_transaction_t *)0 || transaction->private_output_transferred ||
        transaction->candidate_store_state != CUPIDBUILD_CANDIDATE_STORE_CLOSED ||
        transaction->candidate_store_failed || transaction->private_output_transferred ||
        bytes == (const unsigned char *)0 ||
        capacity == 0u || capacity > 2147483647u || size > capacity ||
        !cupidbuild_host_require_frozen_inputs(transaction) ||
        !cupidbuild_host_close_private_output_handle(transaction) ||
        !cupidbuild_host_delete_owned_private_name(transaction, "candidate.map",
                                                   &transaction->private_output_snapshot)) {
        cupidbuild_host_set_error(transaction, "private checked input cannot be written");
        return 0;
    }
#if !defined(_WIN32)
    if (transaction->private_flat != 0) {
        if (!cupidbuild_host_write_anonymous("cupidbuild-output", bytes, size,
                                             transaction->private_output,
                                             sizeof(transaction->private_output), &snapshot,
                                             &transaction->private_output_descriptor) ||
            !cupidbuild_host_promote_retained_descriptor(&transaction->private_output_descriptor) ||
            !cupidbuild_host_private_entry_path(transaction, transaction->private_output,
                                                sizeof(transaction->private_output),
                                                "candidate.map")) {
            cupidbuild_host_close_directory(transaction->private_output_descriptor);
            transaction->private_output_descriptor = -1;
            cupidbuild_host_set_error(transaction, "private checked input cannot be written");
            return 0;
        }
        transaction->private_output_sealed = 1;
    } else
#endif
        if (!cupidbuild_host_write_private_exclusive(transaction, "candidate.map", bytes, size, 0,
                                                     &snapshot,
#if defined(_WIN32)
                                                     &transaction->private_output_handle
#else
                                                         &transaction->private_output_descriptor
#endif
                                                     ) ||
            !cupidbuild_host_private_entry_path(transaction, transaction->private_output,
                                                sizeof(transaction->private_output),
                                                "candidate.map")) {
        cupidbuild_host_set_error(transaction, "private checked input cannot be written");
        return 0;
    }
    transaction->private_output_snapshot = snapshot;
    transaction->private_output_limit = capacity;
    transaction->private_output_ready = 0;
    transaction->private_output_captured = 0;
    return 1;
}

int cupidbuild_host_require_inputs(cupidbuild_host_transaction_t *transaction) {
    unsigned int index;
    int require_private_copy = 0;
    if (transaction == (cupidbuild_host_transaction_t *)0) {
        return 0;
    }
    if (transaction->previous_output_state < 0) {
        cupidbuild_host_set_error(transaction, "previous-output capture failed");
        return 0;
    }
    if (!cupidbuild_host_require_external_observers(transaction))
        return 0;
    for (index = 0u; index < transaction->input_count; index++) {
        cupidbuild_host_snapshot_t current;
        if (transaction->inputs[index].previous_output || transaction->inputs[index].external_input ||
            transaction->inputs[index].frozen_tool_output) {
            require_private_copy = 1;
            continue;
        }
        if (!cupidbuild_host_read_transaction_input(
                transaction, transaction->inputs[index].live_path, transaction->inputs[index].limit,
                &current, (unsigned char **)0) ||
            !cupidbuild_host_snapshot_equal(&current, &transaction->inputs[index].snapshot)) {
            cupidbuild_host_set_error(transaction,
                                      index == 0u && transaction->runner_transaction == 0
                                          ? "source changed while checked tools ran"
                                          : "checked seed inputs changed while checked tools ran");
            return 0;
        }
    }
    return !require_private_copy || cupidbuild_host_require_frozen_inputs(transaction);
}

int cupidbuild_host_require_frozen_inputs(cupidbuild_host_transaction_t *transaction) {
    unsigned int index;
    if (transaction == (cupidbuild_host_transaction_t *)0) {
        return 0;
    }
    if (transaction->private_created != 0 && !cupidbuild_host_private_root_is_owned(transaction)) {
        cupidbuild_host_set_error(transaction,
                                  "private checked-tool directory changed while tool ran");
        return 0;
    }
    for (index = 0u; index < transaction->input_count; index++) {
        cupidbuild_host_snapshot_t current;
        int valid;
        if (transaction->runner_transaction != 0) {
#if defined(_WIN32)
            {
                cupidbuild_host_snapshot_t named;
                valid = transaction->inputs[index].frozen_handle != INVALID_HANDLE_VALUE &&
                        cupidbuild_host_windows_read_open_regular(
                            transaction->inputs[index].frozen_handle,
                            transaction->inputs[index].limit, &current, (unsigned char **)0) &&
                        cupidbuild_host_read_runner_snapshot(
                            transaction, transaction->inputs[index].frozen_path, 0, &named) &&
                        cupidbuild_host_snapshot_equal(&current, &named);
            }
#else
                    valid = transaction->inputs[index].frozen_descriptor >= 0 &&
                            cupidbuild_host_read_open_file(
                                transaction->inputs[index].frozen_descriptor,
                                transaction->inputs[index].limit, &current, (unsigned char **)0);
#endif
        } else {
#if defined(_WIN32)
            valid = transaction->inputs[index].frozen_handle != INVALID_HANDLE_VALUE &&
                    cupidbuild_host_read_retained_private_regular(
                        transaction, transaction->inputs[index].frozen_name,
                        transaction->inputs[index].limit, transaction->inputs[index].frozen_handle,
                        &current, (unsigned char **)0);
#else
                    valid = transaction->inputs[index].frozen_descriptor >= 0 &&
                            cupidbuild_host_read_retained_private_regular(
                                transaction, transaction->inputs[index].frozen_name,
                                transaction->inputs[index].limit,
                                transaction->inputs[index].frozen_descriptor, &current,
                                (unsigned char **)0);
#endif
        }
        if (!valid || !cupidbuild_host_snapshot_equal(
                          &current, &transaction->inputs[index].frozen_snapshot)) {
            if (transaction->runner_transaction != 0) {
                cupidbuild_host_set_error(transaction,
                                          "private checked seed changed while checked tool ran");
            } else {
                (void)snprintf(transaction->error, sizeof(transaction->error),
                               "private checked input %u changed while tool ran", index);
            }
            return 0;
        }
    }
    return 1;
}

static int cupidbuild_host_require_public_binding(cupidbuild_host_transaction_t *transaction,
                                                  const cupidbuild_host_snapshot_t *expected_output,
                                                  int require_discovery) {
    cupidbuild_host_snapshot_t root;
    cupidbuild_host_snapshot_t pinned_root;
    cupidbuild_host_snapshot_t parent;
    cupidbuild_host_snapshot_t pinned_parent;
    cupidbuild_host_snapshot_t output;
    if (transaction == (cupidbuild_host_transaction_t *)0 ||
        expected_output == (const cupidbuild_host_snapshot_t *)0) {
        return 0;
    }
    if (transaction->prepared_output_parent != (cupidbuild_host_output_parent_t *)0 &&
        !cupidbuild_host_output_parent_require_current(transaction->prepared_output_parent)) {
        cupidbuild_host_set_error(
            transaction, cupidbuild_host_output_parent_error(transaction->prepared_output_parent));
        return 0;
    }
    if (!cupidbuild_host_directory_snapshot(transaction->repository_root, &root) ||
#if defined(_WIN32)
        !cupidbuild_host_windows_directory_handle_snapshot(transaction->repository_root_handle,
                                                           &pinned_root) ||
#else
                !cupidbuild_host_directory_descriptor_snapshot(
                    transaction->repository_root_descriptor, &pinned_root) ||
#endif
        !cupidbuild_host_snapshot_identity_equal(&root, &transaction->repository_root_snapshot) ||
        !cupidbuild_host_snapshot_identity_equal(&pinned_root,
                                                 &transaction->repository_root_snapshot)) {
        cupidbuild_host_set_error(transaction, "repository root changed while checked tools ran");
        return 0;
    }
    if (!cupidbuild_host_directory_snapshot(transaction->output_parent, &parent) ||
#if defined(_WIN32)
        !cupidbuild_host_windows_directory_handle_snapshot(transaction->output_parent_handle,
                                                           &pinned_parent) ||
#else
                !cupidbuild_host_directory_descriptor_snapshot(
                    transaction->output_parent_descriptor, &pinned_parent) ||
#endif
        memcmp(parent.identity, transaction->output_parent_snapshot.identity,
               sizeof(parent.identity)) != 0 ||
        !cupidbuild_host_snapshot_identity_equal(&pinned_parent,
                                                 &transaction->output_parent_snapshot)) {
        cupidbuild_host_set_error(transaction, "output parent changed while checked tools ran");
        return 0;
    }
    if (!cupidbuild_host_require_lock(transaction)) {
        return 0;
    }
    if (require_discovery != 0 && !cupidbuild_host_require_discovery_directories(transaction)) {
        cupidbuild_host_set_error(transaction,
                                  "discovered directory closure changed while checked tools ran");
        return 0;
    }
    if (require_discovery != 0 && !cupidbuild_host_require_inputs(transaction)) {
        return 0;
    }
    if (require_discovery != 0 && !cupidbuild_host_require_borrowed_observer(transaction)) {
        return 0;
    }
    if (require_discovery != 0 && !cupidbuild_host_require_source_requests(transaction)) {
        return 0;
    }
    if (!cupidbuild_host_read_output(transaction, 1, &output, (unsigned char **)0) ||
        !cupidbuild_host_snapshot_equal(&output, expected_output)) {
        cupidbuild_host_set_error(transaction, "output changed while checked tools ran");
        return 0;
    }
    return 1;
}

int cupidbuild_host_require_publication_boundary(cupidbuild_host_transaction_t *transaction) {
    return transaction != (cupidbuild_host_transaction_t *)0 &&
           cupidbuild_host_require_public_binding(transaction,
                                                  &transaction->initial_output_snapshot, 1);
}

int cupidbuild_host_publish(cupidbuild_host_transaction_t *transaction) {
    if (transaction != (cupidbuild_host_transaction_t *)0)
        transaction->observer_publication_started = 1;
    if (transaction == (cupidbuild_host_transaction_t *)0 || transaction->candidate_captured == 0 ||
        !cupidbuild_host_require_inputs(transaction) ||
        !cupidbuild_host_require_candidate(transaction, &transaction->candidate_snapshot) ||
        !cupidbuild_host_require_publication_boundary(transaction)) {
        return 0;
    }
    if (!cupidbuild_host_require_lock(transaction)) {
        return 0;
    }
    if (!cupidbuild_host_atomic_replace(transaction)) {
        if (transaction->error[0] == '\0') {
            cupidbuild_host_set_error(transaction, "validated output could not be published");
        }
        return 0;
    }
    return 1;
}

int cupidbuild_host_publish_if_changed(cupidbuild_host_transaction_t *transaction,
                                       int *changed_out) {
    if (transaction != (cupidbuild_host_transaction_t *)0)
        transaction->observer_publication_started = 1;
    if (transaction == (cupidbuild_host_transaction_t *)0 || changed_out == (int *)0 ||
        transaction->candidate_captured == 0 || !cupidbuild_host_require_inputs(transaction) ||
        !cupidbuild_host_require_candidate(transaction, &transaction->candidate_snapshot) ||
        !cupidbuild_host_require_publication_boundary(transaction)) {
        return 0;
    }
    if (transaction->initial_output_snapshot.present != 0 &&
        transaction->initial_output_snapshot.size == transaction->candidate_snapshot.size &&
        memcmp(transaction->initial_output_snapshot.sha256, transaction->candidate_snapshot.sha256,
               sizeof(transaction->candidate_snapshot.sha256)) == 0) {
        if (!cupidbuild_host_require_public_binding(transaction,
                                                    &transaction->initial_output_snapshot, 1)) {
            return 0;
        }
        *changed_out = 0;
        return 1;
    }
    if (!cupidbuild_host_require_lock(transaction) ||
        !cupidbuild_host_atomic_replace(transaction)) {
        if (transaction->error[0] == '\0') {
            cupidbuild_host_set_error(transaction, "validated output could not be published");
        }
        return 0;
    }
    *changed_out = 1;
    return 1;
}

const char *cupidbuild_host_error(const cupidbuild_host_transaction_t *transaction) {
    if (transaction == (const cupidbuild_host_transaction_t *)0 || transaction->error[0] == '\0') {
        return "hosted CupidBuild transaction failed";
    }
    return transaction->error;
}
