/**
 * @file cupidbuild_host_candidate_store.inc
 * @brief Build, inspect and flush a retained disk image candidate in blocks.
 */

/* Internal transaction-owned store. candidate_snapshot is the sole extent and
 * file identity record. Other transaction operations remain caller-exclusive. */
static int cupidbuild_host_store_enter(cupidbuild_host_transaction_t *transaction) {
    if (!transaction)
        return 0;
#if defined(CUPID_HOSTED_I386_LINUX_ABI_H)
    cupid_runtime_mutex_acquire(&transaction->candidate_store_lock);
#else
    while (__atomic_exchange_n(&transaction->candidate_store_lock, 1u, __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(&transaction->candidate_store_lock, __ATOMIC_RELAXED)) {
        }
    }
#endif
    return 1;
}

static int cupidbuild_host_store_leave(cupidbuild_host_transaction_t *transaction, int valid) {
    if (!valid) {
        transaction->candidate_store_failed = 1;
        if (!transaction->error[0])
            cupidbuild_host_set_error(transaction, "native candidate store request failed");
    }
#if defined(CUPID_HOSTED_I386_LINUX_ABI_H)
    cupid_runtime_mutex_release(&transaction->candidate_store_lock);
#else
    __atomic_store_n(&transaction->candidate_store_lock, 0u, __ATOMIC_RELEASE);
#endif
    return valid;
}

static int cupidbuild_host_store_metadata_equal(const cupidbuild_host_snapshot_t *left,
                                                const cupidbuild_host_snapshot_t *right) {
    return left->present && right->present && left->size == right->size &&
           !memcmp(left->identity, right->identity, sizeof(left->identity)) &&
           !memcmp(left->modified, right->modified, sizeof(left->modified)) &&
           !memcmp(left->changed, right->changed, sizeof(left->changed));
}

static int cupidbuild_host_store_current(cupidbuild_host_transaction_t *transaction,
                                         cupidbuild_host_snapshot_t *retained) {
    cupidbuild_host_snapshot_t named;
    if (!cupidbuild_host_private_root_is_owned(transaction))
        return 0;
#if defined(_WIN32)
    return transaction->candidate_handle != INVALID_HANDLE_VALUE &&
           cupidbuild_host_windows_regular_metadata(transaction->candidate_handle, retained) &&
           cupidbuild_host_windows_relative_regular_metadata(transaction->private_handle,
                                                             "candidate.o", 0, &named) &&
           cupidbuild_host_snapshot_identity_equal(retained, &named);
#else
    if (transaction->candidate_descriptor < 0 ||
        !cupidbuild_host_open_regular_metadata(transaction->candidate_descriptor, retained))
        return 0;
    if (cupidbuild_host_private_entry_is_anonymous(transaction, "candidate.o"))
        return 1;
    return cupidbuild_host_regular_metadata_at(
               transaction->private_descriptor,
               cupidbuild_host_private_entry_name(transaction, "candidate.o"), &named) &&
           cupidbuild_host_snapshot_identity_equal(retained, &named);
#endif
}

static int cupidbuild_host_store_check(cupidbuild_host_transaction_t *transaction) {
    cupidbuild_host_snapshot_t current;
    return !transaction->candidate_store_failed &&
           cupidbuild_host_store_current(transaction, &current) &&
           cupidbuild_host_store_metadata_equal(&current, &transaction->candidate_snapshot);
}

#if defined(_WIN32)
static int cupidbuild_host_store_seek(HANDLE handle, unsigned long long offset) {
    return offset <= 2147483647ULL &&
           SetFilePointer(handle, (LONG)offset, NULL, FILE_BEGIN) == (DWORD)offset;
}
#endif

static int cupidbuild_host_store_resize(cupidbuild_host_transaction_t *transaction,
                                        unsigned long long extent) {
#if defined(_WIN32)
    cupidbuild_windows_io_status_t status;
    unsigned long long end_of_file = extent;
    return cupid_windows_nt_set_information_file(transaction->candidate_handle, &status,
                                                &end_of_file, sizeof(end_of_file),
                                                CUPIDBUILD_WINDOWS_END_OF_FILE_INFORMATION) == 0L;
#elif defined(CUPIDBUILD_CUSTOM_LINUX)
    int result;
    do {
        result = cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_FTRUNCATE64, (unsigned int)transaction->candidate_descriptor,
                                      (unsigned int)extent, (unsigned int)(extent >> 32u));
    } while (result == -CUPIDBUILD_LINUX_EINTR);
    return result == 0;
#else
    int result;
    do {
        result = ftruncate(transaction->candidate_descriptor, (off_t)extent);
    } while (result < 0 && errno == EINTR);
    return result == 0;
#endif
}

int cupidbuild_host_candidate_store_open(cupidbuild_host_transaction_t *transaction,
                                         unsigned long long extent) {
    cupidbuild_host_snapshot_t previous;
    cupidbuild_host_snapshot_t current;
    int valid;
    if (!cupidbuild_host_store_enter(transaction))
        return 0;
    valid = transaction->candidate_store_state == CUPIDBUILD_CANDIDATE_STORE_CLOSED &&
            !transaction->candidate_store_failed && !transaction->observer_publication_started &&
            !transaction->runner_transaction && !transaction->candidate_captured &&
            !transaction->candidate_sealed && !transaction->candidate_published &&
            !transaction->candidate_publish_created && extent <= transaction->candidate_limit &&
            !transaction->candidate_snapshot.size &&
            cupidbuild_host_require_frozen_inputs(transaction) &&
            cupidbuild_host_prepare_private_candidate(transaction) &&
            cupidbuild_host_store_check(transaction);
    if (!valid)
        return cupidbuild_host_store_leave(transaction, 0);
    previous = transaction->candidate_snapshot;
#if defined(_WIN32)
    {
        HANDLE writer = cupidbuild_host_windows_open_relative_access_share_status(
            transaction->private_handle, "candidate.o", 0, 1, GENERIC_WRITE | DELETE,
            FILE_SHARE_READ | FILE_SHARE_DELETE, (long *)0);
        if (writer == INVALID_HANDLE_VALUE ||
            !cupidbuild_host_windows_regular_metadata(writer, &current) ||
            !cupidbuild_host_store_metadata_equal(&current, &previous)) {
            if (writer != INVALID_HANDLE_VALUE)
                (void)CloseHandle(writer);
            return cupidbuild_host_store_leave(transaction, 0);
        }
        if (!CloseHandle(transaction->candidate_handle)) {
            (void)CloseHandle(writer);
            transaction->candidate_handle = INVALID_HANDLE_VALUE;
            return cupidbuild_host_store_leave(transaction, 0);
        }
        transaction->candidate_handle = writer;
    }
#endif
    transaction->candidate_store_state = CUPIDBUILD_CANDIDATE_STORE_WRITING;
    valid = cupidbuild_host_store_resize(transaction, extent) &&
            cupidbuild_host_store_current(transaction, &current) && current.size == extent &&
            cupidbuild_host_snapshot_identity_equal(&current, &previous);
    if (valid) {
        transaction->candidate_snapshot = current;
        valid = cupidbuild_host_store_check(transaction);
    }
    return cupidbuild_host_store_leave(transaction, valid);
}

int cupidbuild_host_candidate_store_extent(cupidbuild_host_transaction_t *transaction,
                                           unsigned long long *extent_out) {
    int valid;
    if (extent_out)
        *extent_out = 0ull;
    if (!cupidbuild_host_store_enter(transaction))
        return 0;
    valid = extent_out && transaction->candidate_store_state &&
            cupidbuild_host_store_check(transaction);
    if (valid)
        *extent_out = transaction->candidate_snapshot.size;
    return cupidbuild_host_store_leave(transaction, valid);
}

static int cupidbuild_host_store_range(cupidbuild_host_transaction_t *transaction,
                                       unsigned long long offset, const void *bytes, size_t size) {
    return size <= 65536u && (bytes || !size) && offset <= transaction->candidate_snapshot.size &&
           (unsigned long long)size <= transaction->candidate_snapshot.size - offset;
}

static int cupidbuild_host_read_at(
#if defined(_WIN32)
    HANDLE handle,
#else
    int descriptor,
#endif
    unsigned long long offset, unsigned char *bytes,
                                            size_t size) {
    size_t done = 0u;
#if defined(_WIN32)
    if (!cupidbuild_host_store_seek(handle, offset))
        return 0;
#endif
    while (done < size) {
#if defined(_WIN32)
        DWORD count = 0u;
        if (!ReadFile(handle, bytes + done, (DWORD)(size - done), &count,
                      (LPOVERLAPPED)0) ||
            !count || (size_t)count > size - done)
            return 0;
#elif defined(CUPIDBUILD_CUSTOM_LINUX)
        unsigned long long position = offset + (unsigned long long)done;
        int count = cupid_linux_syscall5(CUPIDBUILD_LINUX_SYS_PREAD64,
                                         (unsigned int)descriptor,
                                         (unsigned int)(bytes + done), (unsigned int)(size - done),
                                         (unsigned int)position, (unsigned int)(position >> 32u));
        if (count == -CUPIDBUILD_LINUX_EINTR)
            continue;
        if (count <= 0 || (size_t)count > size - done)
            return 0;
#else
        ssize_t count = pread(descriptor, bytes + done, size - done,
                              (off_t)(offset + (unsigned long long)done));
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0 || (size_t)count > size - done)
            return 0;
#endif
        done += (size_t)count;
    }
    return 1;
}

int cupidbuild_host_candidate_store_read(cupidbuild_host_transaction_t *transaction,
                                         unsigned long long offset, unsigned char *bytes,
                                         size_t size) {
    unsigned char *staged = (unsigned char *)0;
    int valid;
    if (!cupidbuild_host_store_enter(transaction))
        return 0;
    valid = transaction->candidate_store_state &&
            cupidbuild_host_store_range(transaction, offset, bytes, size) &&
            cupidbuild_host_store_check(transaction);
    if (valid && size) {
        staged = (unsigned char *)malloc(size);
        valid = staged != (void *)0;
    }
    if (valid)
        valid = cupidbuild_host_read_at(
#if defined(_WIN32)
                    transaction->candidate_handle,
#else
                    transaction->candidate_descriptor,
#endif
                    offset, staged, size) &&
                cupidbuild_host_store_check(transaction);
    if (valid && size)
        (void)memcpy(bytes, staged, size);
    free(staged);
    return cupidbuild_host_store_leave(transaction, valid);
}

static int cupidbuild_host_store_write_range(cupidbuild_host_transaction_t *transaction,
                                             unsigned long long offset, const unsigned char *bytes,
                                             size_t size) {
    size_t done = 0u;
#if defined(_WIN32)
    if (!cupidbuild_host_store_seek(transaction->candidate_handle, offset))
        return 0;
#endif
    while (done < size) {
#if defined(_WIN32)
        DWORD count = 0u;
        if (!WriteFile(transaction->candidate_handle, bytes + done, (DWORD)(size - done), &count,
                       (LPOVERLAPPED)0) ||
            !count || (size_t)count > size - done)
            return 0;
#elif defined(CUPIDBUILD_CUSTOM_LINUX)
        unsigned long long position = offset + (unsigned long long)done;
        int count = cupid_linux_syscall5(CUPIDBUILD_LINUX_SYS_PWRITE64, (unsigned int)transaction->candidate_descriptor,
                                         (unsigned int)(bytes + done), (unsigned int)(size - done),
                                         (unsigned int)position, (unsigned int)(position >> 32u));
        if (count == -CUPIDBUILD_LINUX_EINTR)
            continue;
        if (count <= 0 || (size_t)count > size - done)
            return 0;
#else
        ssize_t count = pwrite(transaction->candidate_descriptor, bytes + done, size - done,
                               (off_t)(offset + (unsigned long long)done));
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0 || (size_t)count > size - done)
            return 0;
#endif
        done += (size_t)count;
    }
    return 1;
}

int cupidbuild_host_candidate_store_write(cupidbuild_host_transaction_t *transaction,
                                          unsigned long long offset, const unsigned char *bytes,
                                          size_t size) {
    cupidbuild_host_snapshot_t after;
    int valid;
    if (!cupidbuild_host_store_enter(transaction))
        return 0;
    valid = transaction->candidate_store_state == CUPIDBUILD_CANDIDATE_STORE_WRITING &&
            cupidbuild_host_store_range(transaction, offset, bytes, size) &&
            cupidbuild_host_store_check(transaction) &&
            cupidbuild_host_store_write_range(transaction, offset, bytes, size) &&
            cupidbuild_host_store_current(transaction, &after) &&
            after.size == transaction->candidate_snapshot.size &&
            cupidbuild_host_snapshot_identity_equal(&after, &transaction->candidate_snapshot);
    if (valid) {
        transaction->candidate_snapshot = after;
        valid = cupidbuild_host_store_check(transaction);
    }
    return cupidbuild_host_store_leave(transaction, valid);
}

static int cupidbuild_host_store_flush(cupidbuild_host_transaction_t *transaction) {
#if defined(_WIN32)
    return FlushFileBuffers(transaction->candidate_handle);
#elif defined(CUPIDBUILD_CUSTOM_LINUX)
    int result;
    do {
        result = cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_FSYNC,
                                      (unsigned int)transaction->candidate_descriptor);
    } while (result == -CUPIDBUILD_LINUX_EINTR);
    return result == 0;
#else
    int result;
    do {
        result = fsync(transaction->candidate_descriptor);
    } while (result < 0 && errno == EINTR);
    return result == 0;
#endif
}

int cupidbuild_host_candidate_store_finish(cupidbuild_host_transaction_t *transaction,
                                           cupidbuild_host_snapshot_t *snapshot_out) {
    cupidbuild_host_snapshot_t complete;
    int valid;
    if (snapshot_out)
        (void)memset(snapshot_out, 0, sizeof(*snapshot_out));
    if (!cupidbuild_host_store_enter(transaction))
        return 0;
    valid = snapshot_out && transaction->candidate_store_state == CUPIDBUILD_CANDIDATE_STORE_WRITING &&
            cupidbuild_host_store_check(transaction) && cupidbuild_host_store_flush(transaction);
    if (valid) {
#if defined(_WIN32)
        valid = cupidbuild_host_windows_read_open_regular(transaction->candidate_handle,
                                                          transaction->candidate_limit, &complete,
                                                          (unsigned char **)0);
#else
        valid = cupidbuild_host_read_open_file(transaction->candidate_descriptor,
                                               transaction->candidate_limit, &complete,
                                               (unsigned char **)0);
#endif
        valid = valid &&
                cupidbuild_host_store_metadata_equal(&complete, &transaction->candidate_snapshot) &&
                cupidbuild_host_store_check(transaction);
    }
    if (valid) {
        transaction->candidate_snapshot = complete;
        transaction->candidate_store_state = CUPIDBUILD_CANDIDATE_STORE_FINISHED;
        valid = cupidbuild_host_capture_candidate(transaction, &complete, (unsigned char **)0) &&
                cupidbuild_host_store_check(transaction);
    }
    if (valid)
        *snapshot_out = complete;
    return cupidbuild_host_store_leave(transaction, valid);
}
