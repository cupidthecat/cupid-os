/**
 * @file cupidbuild_host_observer_binding.inc
 * @brief Bind an observed source tree to a transaction.
 */

static int cupidbuild_host_require_borrowed_observer(cupidbuild_host_transaction_t *transaction) {
    if (transaction->observer_binding_state == 0)
        return 1;
    if (transaction->observer_binding_state < 0)
        return 0;
    if (transaction->borrowed_observer->stream_group_active) {
        cupidbuild_host_set_error(transaction, "retained capture is still active");
        return 0;
    }
    if (!cupidbuild_observer_require_current(transaction->borrowed_observer, 1)) {
        cupidbuild_host_set_error(transaction,
                                  cupidbuild_host_observer_error(transaction->borrowed_observer));
        return 0;
    }
    return 1;
}

int cupidbuild_host_transaction_borrow_observer(cupidbuild_host_transaction_t *transaction,
                                                cupidbuild_host_observer_t *observer) {
    cupidbuild_observer_stat_t root;
    if (transaction == (cupidbuild_host_transaction_t *)0)
        return 0;
    if (transaction->observer_binding_state != 0) {
        transaction->observer_binding_state = -1;
        cupidbuild_host_set_error(transaction, "transaction observer may only be bound once");
        return 0;
    }
    transaction->observer_binding_state = -1;
    if (transaction->observer_publication_started != 0) {
        cupidbuild_host_set_error(transaction, "transaction observer must bind before publication");
        return 0;
    }
    if (observer == (cupidbuild_host_observer_t *)0 || observer->selection_only ||
        observer->stream_group_active || observer->root == (cupidbuild_observer_entry_t *)0 ||
        !cupidbuild_observer_require_current(observer, 1)) {
        cupidbuild_host_set_error(transaction, "transaction observer is not valid");
        return 0;
    }
    if (!cupidbuild_observer_stat(
#if defined(_WIN32)
            transaction->repository_root_handle,
#else
                    transaction->repository_root_descriptor,
#endif
            1, &root) ||
        !cupidbuild_observer_same(&observer->root->captured, &root, 1)) {
        cupidbuild_host_set_error(transaction, "transaction observer has a different root");
        return 0;
    }
    transaction->borrowed_observer = observer;
    transaction->observer_binding_state = 1;
    return 1;
}

const char *cupidbuild_host_observer_error(const cupidbuild_host_observer_t *observer) {
    return observer != (const cupidbuild_host_observer_t *)0 && observer->error[0] != 0
               ? observer->error
               : "read-only observation failed";
}

int cupidbuild_host_observer_close(cupidbuild_host_observer_t *observer) {
    int valid = 1;
    if (observer && observer->stream_group_active)
        return cupidbuild_observer_fail(observer, "cannot close an active retained capture");
    if (observer != (cupidbuild_host_observer_t *)0) {
        cupidbuild_observer_entry_t *entry = observer->entries;
        cupidbuild_host_directory_snapshot_t *snapshot = observer->snapshots;
        while (snapshot != (cupidbuild_host_directory_snapshot_t *)0) {
            cupidbuild_host_directory_snapshot_t *next = snapshot->next;
            cupidbuild_directory_snapshot_release(snapshot);
            snapshot = next;
        }
        while (entry != (cupidbuild_observer_entry_t *)0) {
            cupidbuild_observer_entry_t *next = entry->next;
            size_t index;
            if (!entry->absent && !cupidbuild_observer_close_handle(entry->handle))
                valid = 0;
            for (index = 0u; index < entry->member_count; index++)
                free(entry->members[index]);
            free(entry->members);
            if (entry->seed_members) {
                for (index = 0u; index < entry->seed_members->count; index++)
                    free(entry->seed_members->names[index]);
                free(entry->seed_members);
            }
            free(entry->name);
            free(entry);
            entry = next;
        }
        free(observer->source_root_request);
        free(observer->source_root_physical);
        free(observer);
    }
    return valid;
}

typedef struct {
#if defined(_WIN32)
    HANDLE handle;
#else
            int descriptor;
#endif
    uint64_t copied;
    unsigned long long capacity;
} cupidbuild_host_frozen_copy_t;

static int cupidbuild_host_frozen_copy_block(void *opaque, uint64_t offset,
                                             const unsigned char *bytes, size_t size) {
    cupidbuild_host_frozen_copy_t *copy = (cupidbuild_host_frozen_copy_t *)opaque;
    size_t written = 0u;
    if (copy == (cupidbuild_host_frozen_copy_t *)0 || bytes == (const unsigned char *)0 ||
        size == 0u || size > 65536u || offset != copy->copied || offset > copy->capacity ||
        size > copy->capacity - offset)
        return 0;
    while (written < size) {
#if defined(_WIN32)
        DWORD count = 0u;
        if (!WriteFile(copy->handle, bytes + written, (DWORD)(size - written), &count,
                       (LPOVERLAPPED)0) ||
            count == 0u || count > size - written)
            return 0;
#elif defined(CUPIDBUILD_CUSTOM_LINUX)
                int count = cupid_linux_syscall3(
                    CUPIDBUILD_LINUX_SYS_WRITE, (unsigned int)copy->descriptor,
                    (unsigned int)(bytes + written), (unsigned int)(size - written));
                if (count == -CUPIDBUILD_LINUX_EINTR)
                    continue;
                if (count <= 0 || (size_t)count > size - written)
                    return 0;
#else
        ssize_t count = write(copy->descriptor, bytes + written, size - written);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0 || (size_t)count > size - written)
            return 0;
#endif
        written += (size_t)count;
    }
    copy->copied += size;
    return 1;
}

static int cupidbuild_host_copy_frozen_input(cupidbuild_host_transaction_t *transaction,
                                             const char *live_path, const char *private_name,
                                             unsigned long long capacity, int previous_output,
                                             cupidbuild_host_observer_t *external,
                                             const cupidbuild_host_snapshot_t *input_snapshot,
                                             cupidbuild_host_snapshot_t *frozen_snapshot,
#if defined(_WIN32)
                                             HANDLE frozen_handle) {
#else
                                                     int frozen_descriptor) {
#endif
    cupidbuild_host_observer_t *observer = (cupidbuild_host_observer_t *)0;
    cupidbuild_host_stream_observation_t observed;
    cupidbuild_observer_entry_t previous;
    cupidbuild_observer_stat_t root;
    cupidbuild_host_snapshot_t current;
    cupidbuild_host_snapshot_t copied;
    cupidbuild_host_frozen_copy_t copy;
    const char *logical = (const char *)0;
    int valid = 0;
    (void)memset(&copy, 0, sizeof(copy));
    copy.capacity = capacity;
#if defined(_WIN32)
    copy.handle = INVALID_HANDLE_VALUE;
#else
            copy.descriptor = frozen_descriptor;
            (void)private_name;
#endif
    if (previous_output != 0) {
        (void)memset(&previous, 0, sizeof(previous));
#if defined(_WIN32)
        previous.handle = transaction->initial_output_handle;
#else
                previous.handle = transaction->initial_output_descriptor;
#endif
        valid = cupidbuild_host_read_initial_output(transaction, &current) &&
                cupidbuild_host_snapshot_equal(&current, input_snapshot) &&
                cupidbuild_observer_stat(previous.handle, 0, &previous.captured);
        observed.size = previous.captured.size;
    } else if (external) {
        observer = external;
        logical = live_path;
        valid = 1;
    } else {
        if (!cupidbuild_host_repository_logical_path(transaction, live_path, &logical) ||
            !cupidbuild_host_observer_open(transaction->repository_root, &observer))
            return 0;
        valid = cupidbuild_observer_stat(
#if defined(_WIN32)
                    transaction->repository_root_handle,
#else
                            transaction->repository_root_descriptor,
#endif
                    1, &root) &&
                cupidbuild_observer_same(&observer->root->captured, &root, 1);
    }
#if defined(_WIN32)
    if (valid) {
        /* The creator retains read/delete access. Open the writer through the
         * retained private directory, then bind it to that exact created file. */
        cupidbuild_host_snapshot_t writer;
        copy.handle = cupidbuild_host_windows_open_relative_access_share_status(
            transaction->private_handle, private_name, 0, 0, GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, (long *)0);
        valid = copy.handle != INVALID_HANDLE_VALUE &&
                cupidbuild_host_windows_regular_metadata(copy.handle, &writer) &&
                cupidbuild_host_snapshot_identity_equal(&writer, frozen_snapshot) &&
                writer.size == 0u;
    }
#endif
    if (valid)
        valid = (previous_output
                     ? cupidbuild_observer_stream_read(&previous, capacity,
                                                       cupidbuild_host_frozen_copy_block, &copy,
                                                       observed.sha256)
                     : external
                         ? cupidbuild_host_observer_file_stream_retained(
                               observer, logical, capacity, cupidbuild_host_frozen_copy_block,
                               &copy, &observed)
                         : cupidbuild_host_observer_file_stream(observer, logical, capacity,
                                                            cupidbuild_host_frozen_copy_block,
                                                            &copy, &observed)) &&
                observed.size == input_snapshot->size && copy.copied == input_snapshot->size &&
                memcmp(observed.sha256, input_snapshot->sha256, 32u) == 0 &&
                (previous_output || cupidbuild_observer_require_current(observer, !external));
#if defined(_WIN32)
    if (copy.handle != INVALID_HANDLE_VALUE) {
        if (valid && !FlushFileBuffers(copy.handle))
            valid = 0;
        if (!CloseHandle(copy.handle))
            valid = 0;
    }
#else
#if defined(CUPIDBUILD_CUSTOM_LINUX)
            if (valid && cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_FSYNC,
                                              (unsigned int)frozen_descriptor) != 0)
                valid = 0;
#else
            if (valid && fsync(frozen_descriptor) != 0)
                valid = 0;
#endif
            if (valid && transaction->private_flat != 0 &&
                !cupidbuild_host_seal_anonymous(frozen_descriptor))
                valid = 0;
#endif
    if (valid)
        valid = (previous_output
                     ? cupidbuild_host_read_initial_output(transaction, &current)
                     : external
                         ? cupidbuild_host_read_external_input(external, logical, capacity, &current)
                         : cupidbuild_host_read_transaction_input(transaction, live_path, capacity,
                                                              &current, (unsigned char **)0)) &&
                cupidbuild_host_snapshot_equal(&current, input_snapshot) &&
                (!previous_output || cupidbuild_host_require_public_binding(
                                         transaction, &transaction->initial_output_snapshot, 1));
    if (valid)
        valid =
#if defined(_WIN32)
            cupidbuild_host_windows_read_open_regular(frozen_handle, capacity,
#else
                    cupidbuild_host_read_open_file(frozen_descriptor, capacity,
#endif
                                                      &copied, (unsigned char **)0) &&
            cupidbuild_host_snapshot_identity_equal(&copied, frozen_snapshot) &&
            copied.size == input_snapshot->size &&
            memcmp(copied.sha256, input_snapshot->sha256, 32u) == 0;
    if (!external && !cupidbuild_host_observer_close(observer))
        valid = 0;
    if (valid)
        *frozen_snapshot = copied;
    return valid;
}
