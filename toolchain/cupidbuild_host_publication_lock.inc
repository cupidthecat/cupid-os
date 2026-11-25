/**
 * @file cupidbuild_host_publication_lock.inc
 * @brief Acquire publication locks and recover interrupted replacements.
 */

#if defined(_WIN32)
static int cupidbuild_host_windows_dispose_handle(HANDLE handle) {
    cupidbuild_windows_io_status_t status;
    unsigned char disposition = 1u;
    long result;
    if (handle == INVALID_HANDLE_VALUE) {
        return 0;
    }
    (void)memset(&status, 0, sizeof(status));
    result = cupid_windows_nt_set_information_file(handle, &status, &disposition,
                                                   (unsigned long)sizeof(disposition), 13u);
    return CloseHandle(handle) != 0 && result >= 0;
}

static int cupidbuild_host_windows_create_relative_regular(HANDLE parent, const char *name,
                                                           const unsigned char *bytes, size_t size,
                                                           int inheritable,
                                                           cupidbuild_host_snapshot_t *snapshot,
                                                           HANDLE *handle_out) {
    unsigned short name_buffer[4096];
    cupidbuild_windows_unicode_string_t unicode_name;
    cupidbuild_windows_object_attributes_t attributes;
    cupidbuild_windows_io_status_t status;
    BY_HANDLE_FILE_INFORMATION information;
    HANDLE handle = INVALID_HANDLE_VALUE;
    HANDLE write_handle = INVALID_HANDLE_VALUE;
    size_t name_size = 0u;
    size_t offset = 0u;
    long result;
    if (parent == INVALID_HANDLE_VALUE || snapshot == (void *)0 || handle_out == (HANDLE *)0 ||
        (bytes == (const unsigned char *)0 && size != 0u) ||
        !cupidbuild_host_windows_component(
            name, name_buffer, sizeof(name_buffer) / sizeof(name_buffer[0]), &name_size)) {
        return 0;
    }
    *handle_out = INVALID_HANDLE_VALUE;
    unicode_name.length = (unsigned short)(name_size * 2u);
    unicode_name.maximum_length = (unsigned short)((name_size + 1u) * 2u);
    unicode_name.buffer = name_buffer;
    (void)memset(&attributes, 0, sizeof(attributes));
    attributes.length = (unsigned long)sizeof(attributes);
    attributes.root_directory = parent;
    attributes.object_name = &unicode_name;
    attributes.attributes =
        CUPIDBUILD_WINDOWS_OBJECT_CASE_INSENSITIVE | CUPIDBUILD_WINDOWS_OBJECT_DONT_REPARSE;
    (void)memset(&status, 0, sizeof(status));
    result = cupid_windows_nt_create_file(&handle,
                                          DELETE | GENERIC_READ | FILE_READ_ATTRIBUTES |
                                              SYNCHRONIZE | (inheritable != 0 ? GENERIC_WRITE : 0u),
                                          &attributes, &status, (void *)0, 0u,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                          CUPIDBUILD_WINDOWS_FILE_CREATE,
                                          CUPIDBUILD_WINDOWS_FILE_NON_DIRECTORY_FILE |
                                              CUPIDBUILD_WINDOWS_FILE_SYNCHRONOUS_IO_NONALERT |
                                              CUPIDBUILD_WINDOWS_FILE_OPEN_REPARSE_POINT,
                                          (void *)0, 0u);
    if (result < 0 || handle == INVALID_HANDLE_VALUE) {
        return 0;
    }
    write_handle =
        inheritable != 0
            ? handle
            : cupidbuild_host_windows_open_relative_access(parent, name, 0, 0, GENERIC_WRITE);
    if (write_handle == INVALID_HANDLE_VALUE) {
        (void)cupidbuild_host_windows_dispose_handle(handle);
        return 0;
    }
    while (offset < size) {
        DWORD written = 0u;
        DWORD chunk = (DWORD)(size - offset);
        if (!WriteFile(write_handle, bytes + offset, chunk, &written, (LPOVERLAPPED)0) ||
            written == 0u) {
            if (write_handle != handle) {
                (void)CloseHandle(write_handle);
            }
            (void)cupidbuild_host_windows_dispose_handle(handle);
            return 0;
        }
        offset += (size_t)written;
    }
    if (!FlushFileBuffers(write_handle)) {
        if (write_handle != handle) {
            (void)CloseHandle(write_handle);
        }
        (void)cupidbuild_host_windows_dispose_handle(handle);
        return 0;
    }
    if (write_handle != handle) {
        if (!CloseHandle(write_handle)) {
            (void)cupidbuild_host_windows_dispose_handle(handle);
            return 0;
        }
        write_handle = INVALID_HANDLE_VALUE;
    }
    if (!GetFileInformationByHandle(handle, &information) ||
        (information.dwFileAttributes &
         (FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0u) {
        (void)cupidbuild_host_windows_dispose_handle(handle);
        return 0;
    }
    (void)memset(snapshot, 0, sizeof(*snapshot));
    snapshot->present = 1;
    snapshot->size = size;
    snapshot->identity[0] = information.dwVolumeSerialNumber;
    snapshot->identity[1] = information.nFileIndexHigh;
    snapshot->identity[2] = information.nFileIndexLow;
    snapshot->modified[0] = information.ftLastWriteTime.dwHighDateTime;
    snapshot->modified[1] = information.ftLastWriteTime.dwLowDateTime;
    cupidbuild_sha256(bytes, size, snapshot->sha256);
    *handle_out = handle;
    return 1;
}

static int cupidbuild_host_windows_create_lock(cupidbuild_host_transaction_t *transaction,
                                               const unsigned char *bytes, size_t size,
                                               cupidbuild_host_snapshot_t *snapshot) {
    return cupidbuild_host_windows_create_relative_regular(transaction->output_parent_handle,
                                                           transaction->lock_name, bytes, size, 0,
                                                           snapshot, &transaction->lock_handle);
}

static int cupidbuild_host_windows_read_open_regular(HANDLE handle, unsigned long long limit,
                                                     cupidbuild_host_snapshot_t *snapshot,
                                                     unsigned char **bytes_out) {
    BY_HANDLE_FILE_INFORMATION information;
    unsigned char *bytes = (unsigned char *)0;
    unsigned char digest[32];
    unsigned long long size;
    if (handle == INVALID_HANDLE_VALUE || SetFilePointer(handle, 0, 0, FILE_BEGIN) != 0u ||
        !GetFileInformationByHandle(handle, &information) ||
        (information.dwFileAttributes &
         (FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0u ||
        (((unsigned long long)information.nFileSizeHigh << 32u) |
          information.nFileSizeLow) > limit) {
        return 0;
    }
    size = ((unsigned long long)information.nFileSizeHigh << 32u) |
           information.nFileSizeLow;
    if (!cupidbuild_host_read_open_contents(
            handle, size, bytes_out != (unsigned char **)0 ? &bytes : (unsigned char **)0,
            digest)) {
        return 0;
    }
    (void)memset(snapshot, 0, sizeof(*snapshot));
    snapshot->present = 1;
    snapshot->size = size;
    snapshot->identity[0] = information.dwVolumeSerialNumber;
    snapshot->identity[1] = information.nFileIndexHigh;
    snapshot->identity[2] = information.nFileIndexLow;
    snapshot->modified[0] = information.ftLastWriteTime.dwHighDateTime;
    snapshot->modified[1] = information.ftLastWriteTime.dwLowDateTime;
    (void)memcpy(snapshot->sha256, digest, sizeof(digest));
    if (bytes_out != (unsigned char **)0) {
        *bytes_out = bytes;
    } else {
        free(bytes);
    }
    return 1;
}

static int cupidbuild_host_windows_regular_metadata(HANDLE handle,
                                                    cupidbuild_host_snapshot_t *snapshot) {
    BY_HANDLE_FILE_INFORMATION information;
    if (handle == INVALID_HANDLE_VALUE || snapshot == (cupidbuild_host_snapshot_t *)0 ||
        !GetFileInformationByHandle(handle, &information) ||
        (information.dwFileAttributes &
         (FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0u) {
        return 0;
    }
    (void)memset(snapshot, 0, sizeof(*snapshot));
    snapshot->present = 1;
    snapshot->size =
        ((unsigned long long)information.nFileSizeHigh << 32u) | information.nFileSizeLow;
    snapshot->identity[0] = information.dwVolumeSerialNumber;
    snapshot->identity[1] = information.nFileIndexHigh;
    snapshot->identity[2] = information.nFileIndexLow;
    snapshot->modified[0] = information.ftLastWriteTime.dwHighDateTime;
    snapshot->modified[1] = information.ftLastWriteTime.dwLowDateTime;
    return 1;
}

static int cupidbuild_host_windows_relative_regular_metadata(HANDLE parent, const char *name,
                                                             int optional,
                                                             cupidbuild_host_snapshot_t *snapshot) {
    long status = 0;
    unsigned long unsigned_status;
    HANDLE handle;
    int valid;
    if (snapshot == (cupidbuild_host_snapshot_t *)0) {
        return 0;
    }
    (void)memset(snapshot, 0, sizeof(*snapshot));
    handle = cupidbuild_host_windows_open_relative_access_status(parent, name, 0, 0, 0u, &status);
    if (handle == INVALID_HANDLE_VALUE) {
        unsigned_status = (unsigned long)status;
        return optional != 0 && (unsigned_status == 0xc000000fu || unsigned_status == 0xc0000034u ||
                                 unsigned_status == 0xc000003au || unsigned_status == 0xc0000056u);
    }
    valid = cupidbuild_host_windows_regular_metadata(handle, snapshot);
    if (!CloseHandle(handle)) {
        valid = 0;
    }
    return valid;
}

static int cupidbuild_host_windows_dispose_retained_at(HANDLE parent, const char *name,
                                                       cupidbuild_host_snapshot_t *expected,
                                                       HANDLE *retained_handle) {
    cupidbuild_host_snapshot_t retained;
    cupidbuild_host_snapshot_t named;
    cupidbuild_host_snapshot_t after;
    int valid;
    int disposed;
#if defined(CUPIDBUILD_PUBLICATION_RACE_TEST)
    if (name != (const char *)0 && strncmp(name, ".cupidbuild-old-", 16u) == 0 &&
        getenv("CUPIDBUILD_PUBLICATION_TEST_FAIL_OLD_DISPOSITION") != (const char *)0) {
        return 0;
    }
#endif
    if (parent == INVALID_HANDLE_VALUE || name == (const char *)0 ||
        expected == (cupidbuild_host_snapshot_t *)0 || retained_handle == (HANDLE *)0 ||
        *retained_handle == INVALID_HANDLE_VALUE) {
        return 0;
    }
    valid = expected->present != 0 &&
            cupidbuild_host_windows_regular_metadata(*retained_handle, &retained) &&
            cupidbuild_host_windows_relative_regular_metadata(parent, name, 0, &named) &&
            cupidbuild_host_snapshot_identity_equal(&retained, expected) &&
            cupidbuild_host_snapshot_identity_equal(&named, expected) &&
            cupidbuild_host_snapshot_identity_equal(&retained, &named);
    if (valid == 0) {
        (void)CloseHandle(*retained_handle);
        *retained_handle = INVALID_HANDLE_VALUE;
        return 0;
    }
    disposed = cupidbuild_host_windows_dispose_handle(*retained_handle);
    *retained_handle = INVALID_HANDLE_VALUE;
    if (!disposed || !cupidbuild_host_windows_relative_regular_metadata(parent, name, 1, &after) ||
        after.present != 0) {
        return 0;
    }
    expected->present = 0;
    return 1;
}

static int cupidbuild_host_windows_dispose_read_retained_at(HANDLE parent, const char *name,
                                                            cupidbuild_host_snapshot_t *expected,
                                                            HANDLE *retained_handle) {
    cupidbuild_host_snapshot_t retained;
    cupidbuild_host_snapshot_t named;
    cupidbuild_host_snapshot_t disposable;
    cupidbuild_host_snapshot_t after;
    HANDLE delete_handle = INVALID_HANDLE_VALUE;
    int valid;
    if (parent == INVALID_HANDLE_VALUE || name == (const char *)0 ||
        expected == (cupidbuild_host_snapshot_t *)0 || retained_handle == (HANDLE *)0 ||
        *retained_handle == INVALID_HANDLE_VALUE) {
        return 0;
    }
    valid = expected->present != 0 &&
            cupidbuild_host_windows_regular_metadata(*retained_handle, &retained) &&
            cupidbuild_host_windows_read_relative_regular(parent, name, 0, expected->size, &named,
                                                          (unsigned char **)0) &&
            cupidbuild_host_snapshot_identity_equal(&retained, expected) &&
            cupidbuild_host_snapshot_equal(&named, expected);
    if (valid == 0) {
        (void)CloseHandle(*retained_handle);
        *retained_handle = INVALID_HANDLE_VALUE;
        return 0;
    }
    if (!CloseHandle(*retained_handle)) {
        *retained_handle = INVALID_HANDLE_VALUE;
        return 0;
    }
    *retained_handle = INVALID_HANDLE_VALUE;
    delete_handle = cupidbuild_host_windows_open_relative_access_share_status(
        parent, name, 0, 1, DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        (long *)0);
    if (delete_handle == INVALID_HANDLE_VALUE ||
        !cupidbuild_host_windows_read_open_regular(delete_handle, expected->size, &disposable,
                                                   (unsigned char **)0) ||
        !cupidbuild_host_snapshot_equal(&disposable, expected)) {
        if (delete_handle != INVALID_HANDLE_VALUE) {
            (void)CloseHandle(delete_handle);
        }
        return 0;
    }
    if (!cupidbuild_host_windows_dispose_handle(delete_handle) ||
        !cupidbuild_host_windows_relative_regular_metadata(parent, name, 1, &after) ||
        after.present != 0) {
        return 0;
    }
    expected->present = 0;
    return 1;
}

static int cupidbuild_host_windows_transition_retained_at_limit(
    HANDLE parent, const char *name, cupidbuild_host_snapshot_t *expected, HANDLE *retained_handle,
    int seal, int require_unchanged, int retain_delete_access, unsigned long long limit) {
    cupidbuild_host_snapshot_t current;
    cupidbuild_host_snapshot_t transition_snapshot;
    cupidbuild_host_snapshot_t final_snapshot;
    cupidbuild_host_snapshot_t named_snapshot;
    HANDLE transition_handle;
    HANDLE final_handle;
    int requested_profile_opened = 1;
    unsigned long final_share =
        seal != 0 ? FILE_SHARE_READ : FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    unsigned long final_access = retain_delete_access != 0 ? DELETE : 0u;
    if (parent == INVALID_HANDLE_VALUE || name == (const char *)0 ||
        expected == (cupidbuild_host_snapshot_t *)0 || retained_handle == (HANDLE *)0 ||
        *retained_handle == INVALID_HANDLE_VALUE || expected->present == 0 ||
        !cupidbuild_host_windows_read_open_regular(*retained_handle, limit, &current,
                                                   (unsigned char **)0) ||
        !cupidbuild_host_snapshot_identity_equal(&current, expected) ||
        (require_unchanged != 0 && !cupidbuild_host_snapshot_equal(&current, expected))) {
        return 0;
    }
    transition_handle = cupidbuild_host_windows_open_relative_access_share_status(
        parent, name, 0, 1, 0u, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, (long *)0);
    if (transition_handle == INVALID_HANDLE_VALUE ||
        !cupidbuild_host_windows_read_open_regular(transition_handle, limit, &transition_snapshot,
                                                   (unsigned char **)0) ||
        !cupidbuild_host_snapshot_equal(&current, &transition_snapshot)) {
        if (transition_handle != INVALID_HANDLE_VALUE) {
            (void)CloseHandle(transition_handle);
        }
        return 0;
    }
    if (!CloseHandle(*retained_handle)) {
        (void)CloseHandle(transition_handle);
        *retained_handle = INVALID_HANDLE_VALUE;
        return 0;
    }
    *retained_handle = INVALID_HANDLE_VALUE;
    final_handle = cupidbuild_host_windows_open_relative_access_share_status(
        parent, name, 0, 1, final_access, final_share, (long *)0);
    if (final_handle == INVALID_HANDLE_VALUE) {
        requested_profile_opened = 0;
        if (seal != 0 && retain_delete_access != 0) {
            final_handle = cupidbuild_host_windows_open_relative_access_share_status(
                parent, name, 0, 1, DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                (long *)0);
        }
    }
    if (final_handle == INVALID_HANDLE_VALUE) {
        final_handle = cupidbuild_host_windows_open_relative_access_share_status(
            parent, name, 0, 1, 0u, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            (long *)0);
    }
    if (final_handle == INVALID_HANDLE_VALUE ||
        !cupidbuild_host_windows_read_open_regular(final_handle, limit, &final_snapshot,
                                                   (unsigned char **)0) ||
        !cupidbuild_host_windows_read_relative_regular(parent, name, 0, limit, &named_snapshot,
                                                       (unsigned char **)0) ||
        !cupidbuild_host_snapshot_equal(&current, &final_snapshot) ||
        !cupidbuild_host_snapshot_equal(&current, &named_snapshot)) {
        if (final_handle != INVALID_HANDLE_VALUE) {
            *retained_handle = final_handle;
        }
        (void)CloseHandle(transition_handle);
        return 0;
    }
    if (!CloseHandle(transition_handle)) {
        *retained_handle = final_handle;
        return 0;
    }
    *retained_handle = final_handle;
    *expected = final_snapshot;
    return requested_profile_opened != 0;
}

static int cupidbuild_host_windows_transition_retained_at(HANDLE parent, const char *name,
                                                          cupidbuild_host_snapshot_t *expected,
                                                          HANDLE *retained_handle, int seal,
                                                          int require_unchanged,
                                                          int retain_delete_access) {
    return cupidbuild_host_windows_transition_retained_at_limit(
        parent, name, expected, retained_handle, seal, require_unchanged, retain_delete_access,
        CUPIDBUILD_HOST_FILE_LIMIT);
}

static int cupidbuild_host_windows_reclaim_lock(cupidbuild_host_transaction_t *transaction,
                                                const cupidbuild_host_snapshot_t *expected) {
    HANDLE handle = cupidbuild_host_windows_open_relative_access(
        transaction->output_parent_handle, transaction->lock_name, 0, 1, DELETE);
    cupidbuild_host_snapshot_t current;
    if (handle == INVALID_HANDLE_VALUE ||
        !cupidbuild_host_windows_read_open_regular(handle, CUPIDBUILD_HOST_FILE_LIMIT, &current,
                                                   (unsigned char **)0) ||
        !cupidbuild_host_lock_snapshot_equal(&current, expected)) {
        if (handle != INVALID_HANDLE_VALUE) {
            (void)CloseHandle(handle);
        }
        return 0;
    }
    return cupidbuild_host_windows_dispose_handle(handle);
}

static int cupidbuild_host_windows_release_lock(cupidbuild_host_transaction_t *transaction) {
    cupidbuild_host_snapshot_t current;
    cupidbuild_host_snapshot_t after;
    int unchanged = cupidbuild_host_windows_read_relative_regular(
                        transaction->output_parent_handle, transaction->lock_name, 0,
                        CUPIDBUILD_HOST_FILE_LIMIT, &current, (unsigned char **)0) &&
                    cupidbuild_host_lock_snapshot_equal(&current, &transaction->lock_snapshot);
    int disposed = cupidbuild_host_windows_dispose_handle(transaction->lock_handle);
    transaction->lock_handle = INVALID_HANDLE_VALUE;
    if (!disposed || !unchanged ||
        !cupidbuild_host_windows_read_relative_regular(
            transaction->output_parent_handle, transaction->lock_name, 1,
            CUPIDBUILD_HOST_FILE_LIMIT, &after, (unsigned char **)0) ||
        after.present != 0) {
        return 0;
    }
    return 1;
}
#endif

#if !defined(_WIN32)
static int cupidbuild_host_unlink_entry_at(int directory, const char *name) {
    int result;
#if defined(CUPIDBUILD_CUSTOM_LINUX)
    do {
        result = cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_UNLINKAT, (unsigned int)directory,
                                      (unsigned int)name, 0u);
    } while (result == -CUPIDBUILD_LINUX_EINTR);
#else
    do {
        result = unlinkat(directory, name, 0);
    } while (result != 0 && errno == EINTR);
#endif
    return result == 0;
}

static int cupidbuild_host_link_entry_at(int source_directory, const char *source_name,
                                         int destination_directory, const char *destination_name) {
    int result;
#if defined(CUPIDBUILD_CUSTOM_LINUX)
    do {
        result = cupid_linux_syscall5(
            CUPIDBUILD_LINUX_SYS_LINKAT, (unsigned int)source_directory, (unsigned int)source_name,
            (unsigned int)destination_directory, (unsigned int)destination_name, 0u);
    } while (result == -CUPIDBUILD_LINUX_EINTR);
#else
    do {
        result = linkat(source_directory, source_name, destination_directory, destination_name, 0);
    } while (result != 0 && errno == EINTR);
#endif
    return result == 0;
}

static int cupidbuild_host_link_open_file_at(int descriptor, int destination_directory,
                                             const char *destination_name) {
    char descriptor_path[64];
    int written =
        snprintf(descriptor_path, sizeof(descriptor_path), "/proc/self/fd/%d", descriptor);
    int result;
    if (descriptor < 0 || written <= 0 || (size_t)written >= sizeof(descriptor_path)) {
        return 0;
    }
#if defined(CUPIDBUILD_CUSTOM_LINUX)
    do {
        result = cupid_linux_syscall5(
            CUPIDBUILD_LINUX_SYS_LINKAT, (unsigned int)CUPIDBUILD_LINUX_AT_FDCWD,
            (unsigned int)descriptor_path, (unsigned int)destination_directory,
            (unsigned int)destination_name, CUPIDBUILD_LINUX_AT_SYMLINK_FOLLOW);
    } while (result == -CUPIDBUILD_LINUX_EINTR);
#else
    do {
        result = linkat(AT_FDCWD, descriptor_path, destination_directory, destination_name,
                        AT_SYMLINK_FOLLOW);
    } while (result != 0 && errno == EINTR);
#endif
    return result == 0;
}

static int cupidbuild_host_rename_entry_at(int source_directory, const char *source_name,
                                           int destination_directory,
                                           const char *destination_name) {
    int result;
#if defined(CUPIDBUILD_CUSTOM_LINUX)
    do {
        result =
            cupid_linux_syscall4(CUPIDBUILD_LINUX_SYS_RENAMEAT, (unsigned int)source_directory,
                                 (unsigned int)source_name, (unsigned int)destination_directory,
                                 (unsigned int)destination_name);
    } while (result == -CUPIDBUILD_LINUX_EINTR);
#else
    do {
        result = renameat(source_directory, source_name, destination_directory, destination_name);
    } while (result != 0 && errno == EINTR);
#endif
    return result == 0;
}

#define CUPIDBUILD_HOST_NOREPLACE_FAILED 0
#define CUPIDBUILD_HOST_NOREPLACE_COMPLETE 1
#define CUPIDBUILD_HOST_NOREPLACE_DESTINATION_LINKED 2

static int cupidbuild_host_rename_entry_noreplace_status_at(int source_directory,
                                                            const char *source_name,
                                                            int destination_directory,
                                                            const char *destination_name) {
    int result;
#if defined(CUPIDBUILD_NOREPLACE_RACE_TEST) && !defined(CUPIDBUILD_CUSTOM_LINUX)
    const char *test_destination = getenv("CUPIDBUILD_NOREPLACE_TEST_DESTINATION");
    int test_target =
        test_destination != (const char *)0 && strcmp(test_destination, destination_name) == 0;
#endif
#if defined(CUPIDBUILD_CUSTOM_LINUX)
    do {
        result =
            cupid_linux_syscall5(CUPIDBUILD_LINUX_SYS_RENAMEAT2, (unsigned int)source_directory,
                                 (unsigned int)source_name, (unsigned int)destination_directory,
                                 (unsigned int)destination_name, CUPIDBUILD_LINUX_RENAME_NOREPLACE);
    } while (result == -CUPIDBUILD_LINUX_EINTR);
#else
#if defined(CUPIDBUILD_NOREPLACE_RACE_TEST)
    if (test_target != 0 && getenv("CUPIDBUILD_NOREPLACE_TEST_FORCE_FALLBACK") != (const char *)0) {
        result = -1;
    } else
#endif
    {
        do {
            result = renameat2(source_directory, source_name, destination_directory,
                               destination_name, RENAME_NOREPLACE);
        } while (result != 0 && errno == EINTR);
    }
#endif
    if (result == 0) {
        return CUPIDBUILD_HOST_NOREPLACE_COMPLETE;
    }
    /* DrvFS rejects renameat2 flags. Hard-linking first preserves the
       no-replace rule for the regular files handled by this helper. */
    if (!cupidbuild_host_link_entry_at(source_directory, source_name, destination_directory,
                                       destination_name)) {
        return CUPIDBUILD_HOST_NOREPLACE_FAILED;
    }
    result =
#if defined(CUPIDBUILD_NOREPLACE_RACE_TEST) && !defined(CUPIDBUILD_CUSTOM_LINUX)
        test_target != 0 &&
                getenv("CUPIDBUILD_NOREPLACE_TEST_FAIL_SOURCE_UNLINK") != (const char *)0
            ? 0
            :
#endif
            cupidbuild_host_unlink_entry_at(source_directory, source_name);
    if (!result) {
#if defined(CUPIDBUILD_NOREPLACE_RACE_TEST) && !defined(CUPIDBUILD_CUSTOM_LINUX)
        if (test_target != 0 &&
            getenv("CUPIDBUILD_NOREPLACE_TEST_REPLACE_DESTINATION") != (const char *)0) {
            static const unsigned char replacement[] = "foreign replacement\n";
            size_t offset = 0u;
            int replacement_descriptor;
            if (!cupidbuild_host_unlink_entry_at(destination_directory, destination_name)) {
                return CUPIDBUILD_HOST_NOREPLACE_DESTINATION_LINKED;
            }
            do {
                replacement_descriptor =
                    openat(destination_directory, destination_name,
                           O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
            } while (replacement_descriptor < 0 && errno == EINTR);
            if (replacement_descriptor < 0) {
                return CUPIDBUILD_HOST_NOREPLACE_DESTINATION_LINKED;
            }
            while (offset + 1u < sizeof(replacement)) {
                ssize_t count = write(replacement_descriptor, replacement + offset,
                                      sizeof(replacement) - 1u - offset);
                if (count < 0 && errno == EINTR) {
                    continue;
                }
                if (count <= 0) {
                    break;
                }
                offset += (size_t)count;
            }
            (void)close(replacement_descriptor);
        }
#endif
#if defined(CUPIDBUILD_PUBLICATION_RACE_TEST) && !defined(CUPIDBUILD_CUSTOM_LINUX)
        if (!cupidbuild_host_publication_test_pause("after-noreplace-link")) {
            return CUPIDBUILD_HOST_NOREPLACE_DESTINATION_LINKED;
        }
#endif
        return CUPIDBUILD_HOST_NOREPLACE_DESTINATION_LINKED;
    }
    return CUPIDBUILD_HOST_NOREPLACE_COMPLETE;
}

static int cupidbuild_host_rename_entry_noreplace_at(int source_directory, const char *source_name,
                                                     int destination_directory,
                                                     const char *destination_name) {
    return cupidbuild_host_rename_entry_noreplace_status_at(
               source_directory, source_name, destination_directory, destination_name) ==
           CUPIDBUILD_HOST_NOREPLACE_COMPLETE;
}

static int cupidbuild_host_regular_snapshot_at_limit(int directory, const char *name, unsigned long long limit,
                                                     cupidbuild_host_snapshot_t *snapshot) {
#if defined(CUPIDBUILD_CUSTOM_LINUX)
    return cupidbuild_linux_read_relative_regular(directory, name, 0, limit, snapshot,
                                                  (unsigned char **)0);
#else
    return cupidbuild_native_read_relative_regular(directory, name, 0, limit, snapshot,
                                                   (unsigned char **)0);
#endif
}

static int cupidbuild_host_regular_snapshot_at(int directory, const char *name,
                                               cupidbuild_host_snapshot_t *snapshot) {
    return cupidbuild_host_regular_snapshot_at_limit(directory, name, CUPIDBUILD_HOST_FILE_LIMIT,
                                                     snapshot);
}

static int cupidbuild_host_regular_metadata_at(int directory, const char *name,
                                               cupidbuild_host_snapshot_t *snapshot) {
#if defined(CUPIDBUILD_CUSTOM_LINUX)
    unsigned char information[96];
    (void)memset(snapshot, 0, sizeof(*snapshot));
    if (directory < 0 || name == (const char *)0 || name[0] == '\0' ||
        cupid_linux_syscall4(CUPIDBUILD_LINUX_SYS_FSTATAT64, (unsigned int)directory,
                             (unsigned int)name, (unsigned int)information,
                             CUPIDBUILD_LINUX_AT_SYMLINK_NOFOLLOW) < 0 ||
        (cupidbuild_linux_mode(information) & CUPIDBUILD_LINUX_S_IFMT) !=
            CUPIDBUILD_LINUX_S_IFREG) {
        return 0;
    }
    snapshot->present = 1;
    snapshot->size = ((unsigned long long)cupidbuild_linux_u32(information + 48u) << 32u) |
                     cupidbuild_linux_u32(information + 44u);
    snapshot->identity[0] = cupidbuild_linux_u32(information);
    snapshot->identity[1] = cupidbuild_linux_u32(information + 4u);
    snapshot->identity[2] = cupidbuild_linux_u32(information + 88u);
    snapshot->identity[3] = cupidbuild_linux_u32(information + 92u);
    snapshot->modified[0] = cupidbuild_linux_u32(information + 72u);
    snapshot->modified[1] = cupidbuild_linux_u32(information + 76u);
    return 1;
#else
    struct stat information;
    (void)memset(snapshot, 0, sizeof(*snapshot));
    if (directory < 0 || name == (const char *)0 || name[0] == '\0' ||
        fstatat(directory, name, &information, AT_SYMLINK_NOFOLLOW) != 0 ||
        !S_ISREG(information.st_mode) || information.st_size < 0) {
        return 0;
    }
    snapshot->present = 1;
    snapshot->size = (unsigned long long)information.st_size;
    snapshot->identity[0] = (unsigned int)(unsigned long long)information.st_dev;
    snapshot->identity[1] = (unsigned int)((unsigned long long)information.st_dev >> 32u);
    snapshot->identity[2] = (unsigned int)(unsigned long long)information.st_ino;
    snapshot->identity[3] = (unsigned int)((unsigned long long)information.st_ino >> 32u);
    snapshot->modified[0] = (unsigned int)information.st_mtime;
    return 1;
#endif
}

static int cupidbuild_host_open_regular_metadata(int descriptor,
                                                 cupidbuild_host_snapshot_t *snapshot) {
#if defined(CUPIDBUILD_CUSTOM_LINUX)
    return cupidbuild_host_linux_open_snapshot(descriptor, snapshot);
#else
    return cupidbuild_host_posix_open_snapshot(descriptor, snapshot);
#endif
}

static int cupidbuild_host_directory_entry_snapshot_at(int directory, const char *name,
                                                       cupidbuild_host_snapshot_t *snapshot) {
#if defined(CUPIDBUILD_CUSTOM_LINUX)
    unsigned char information[96];
    (void)memset(snapshot, 0, sizeof(*snapshot));
    if (directory < 0 || name == (const char *)0 || name[0] == '\0' ||
        cupid_linux_syscall4(CUPIDBUILD_LINUX_SYS_FSTATAT64, (unsigned int)directory,
                             (unsigned int)name, (unsigned int)information,
                             CUPIDBUILD_LINUX_AT_SYMLINK_NOFOLLOW) < 0 ||
        (cupidbuild_linux_mode(information) & CUPIDBUILD_LINUX_S_IFMT) !=
            CUPIDBUILD_LINUX_S_IFDIR) {
        return 0;
    }
    snapshot->present = 1;
    snapshot->identity[0] = cupidbuild_linux_u32(information);
    snapshot->identity[1] = cupidbuild_linux_u32(information + 4u);
    snapshot->identity[2] = cupidbuild_linux_u32(information + 88u);
    snapshot->identity[3] = cupidbuild_linux_u32(information + 92u);
    return 1;
#else
    struct stat information;
    (void)memset(snapshot, 0, sizeof(*snapshot));
    if (directory < 0 || name == (const char *)0 || name[0] == '\0' ||
        fstatat(directory, name, &information, AT_SYMLINK_NOFOLLOW) != 0 ||
        !S_ISDIR(information.st_mode)) {
        return 0;
    }
    snapshot->present = 1;
    snapshot->identity[0] = (unsigned int)(unsigned long long)information.st_dev;
    snapshot->identity[1] = (unsigned int)((unsigned long long)information.st_dev >> 32u);
    snapshot->identity[2] = (unsigned int)(unsigned long long)information.st_ino;
    snapshot->identity[3] = (unsigned int)((unsigned long long)information.st_ino >> 32u);
    return 1;
#endif
}

static int cupidbuild_host_unlink_identity_entry_at(int directory, const char *name,
                                                    const cupidbuild_host_snapshot_t *expected) {
    cupidbuild_host_snapshot_t current;
    return cupidbuild_host_regular_metadata_at(directory, name, &current) &&
           cupidbuild_host_snapshot_identity_equal(&current, expected) &&
           cupidbuild_host_unlink_entry_at(directory, name);
}

static int cupidbuild_host_promote_retained_descriptor(int *descriptor) {
    int duplicated;
    if (descriptor == (int *)0 || *descriptor < 0) {
        return 0;
    }
    if (*descriptor > 2) {
        return 1;
    }
#if defined(CUPIDBUILD_CUSTOM_LINUX)
    if (!cupidbuild_host_linux_promote_above_standard(descriptor)) {
        return 0;
    }
    duplicated = *descriptor;
    if (cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_FCNTL64, (unsigned int)duplicated,
                             CUPIDBUILD_LINUX_F_SETFD, CUPIDBUILD_LINUX_FD_CLOEXEC) != 0) {
        if (duplicated >= 0) {
            (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)duplicated);
        }
        *descriptor = -1;
        return 0;
    }
#else
    if (!cupidbuild_host_posix_promote_above_standard(descriptor)) {
        return 0;
    }
    duplicated = *descriptor;
    if (fcntl(duplicated, F_SETFD, FD_CLOEXEC) != 0) {
        if (duplicated >= 0) {
            (void)close(duplicated);
        }
        *descriptor = -1;
        return 0;
    }
#endif
    *descriptor = duplicated;
    return 1;
}

static int cupidbuild_host_write_exclusive_at(int directory, const char *name,
                                              const unsigned char *bytes, size_t size,
                                              int inheritable, cupidbuild_host_snapshot_t *snapshot,
                                              int *descriptor_out) {
    int descriptor;
    cupidbuild_host_snapshot_t created;
    size_t offset = 0u;
    (void)inheritable;
    if (directory < 0 || name == (const char *)0 || name[0] == '\0' ||
        strchr(name, '/') != (char *)0 || strchr(name, '\\') != (char *)0 ||
        snapshot == (cupidbuild_host_snapshot_t *)0 || descriptor_out == (int *)0 ||
        (bytes == (const unsigned char *)0 && size != 0u)) {
        return 0;
    }
    *descriptor_out = -1;
#if defined(CUPIDBUILD_CUSTOM_LINUX)
    do {
        descriptor = cupid_linux_syscall4(
            CUPIDBUILD_LINUX_SYS_OPENAT, (unsigned int)directory, (unsigned int)name,
            CUPIDBUILD_LINUX_O_RDWR | CUPIDBUILD_LINUX_O_CREAT | CUPIDBUILD_LINUX_O_EXCL |
                CUPIDBUILD_LINUX_O_NOFOLLOW | CUPIDBUILD_LINUX_O_CLOEXEC |
                CUPIDBUILD_LINUX_O_LARGEFILE,
            0600u);
    } while (descriptor == -CUPIDBUILD_LINUX_EINTR);
#else
    do {
        descriptor =
            openat(directory, name, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    } while (descriptor < 0 && errno == EINTR);
#endif
    if (descriptor < 0) {
        return 0;
    }
#if defined(CUPIDBUILD_CUSTOM_LINUX)
    if (!cupidbuild_host_linux_open_snapshot(descriptor, &created)) {
#else
    if (!cupidbuild_host_posix_open_snapshot(descriptor, &created)) {
#endif
        if (cupidbuild_host_open_regular_metadata(descriptor, &created)) {
            (void)cupidbuild_host_unlink_identity_entry_at(directory, name, &created);
        }
        cupidbuild_host_close_directory(descriptor);
        return 0;
    }
    if (!cupidbuild_host_promote_retained_descriptor(&descriptor)) {
        (void)cupidbuild_host_unlink_identity_entry_at(directory, name, &created);
        return 0;
    }
    while (offset < size) {
#if defined(CUPIDBUILD_CUSTOM_LINUX)
        int count =
            cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_WRITE, (unsigned int)descriptor,
                                 (unsigned int)(bytes + offset), (unsigned int)(size - offset));
        if (count == -CUPIDBUILD_LINUX_EINTR) {
            continue;
        }
#else
        ssize_t count = write(descriptor, bytes + offset, size - offset);
        if (count < 0 && errno == EINTR) {
            continue;
        }
#endif
        if (count <= 0) {
            (void)cupidbuild_host_unlink_identity_entry_at(directory, name, &created);
            cupidbuild_host_close_directory(descriptor);
            return 0;
        }
        offset += (size_t)count;
    }
#if defined(CUPIDBUILD_CUSTOM_LINUX)
    if (cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_FSYNC, (unsigned int)descriptor) != 0 ||
        !cupidbuild_host_linux_open_snapshot(descriptor, snapshot)) {
#else
    if (fsync(descriptor) != 0 || !cupidbuild_host_posix_open_snapshot(descriptor, snapshot)) {
#endif
        (void)cupidbuild_host_unlink_identity_entry_at(directory, name, &created);
        cupidbuild_host_close_directory(descriptor);
        return 0;
    }
    cupidbuild_sha256(bytes, size, snapshot->sha256);
    *descriptor_out = descriptor;
    return 1;
}

static int cupidbuild_host_write_lock_exclusive_at(cupidbuild_host_transaction_t *transaction,
                                                   const unsigned char *bytes, size_t size,
                                                   cupidbuild_host_snapshot_t *snapshot) {
    int descriptor = -1;
    if (!cupidbuild_host_write_exclusive_at(transaction->output_parent_descriptor,
                                            transaction->lock_name, bytes, size, 0, snapshot,
                                            &descriptor)) {
        return 0;
    }
    cupidbuild_host_close_directory(descriptor);
    return 1;
}

static int cupidbuild_host_unlink_owned_entry_at(int directory, const char *name,
                                                 const cupidbuild_host_snapshot_t *expected) {
    cupidbuild_host_snapshot_t current;
    return cupidbuild_host_regular_snapshot_at(directory, name, &current) &&
           cupidbuild_host_lock_snapshot_equal(&current, expected) &&
           cupidbuild_host_unlink_entry_at(directory, name);
}

static int cupidbuild_host_reclaim_lock_at(cupidbuild_host_transaction_t *transaction,
                                           const cupidbuild_host_snapshot_t *expected,
                                           unsigned int owner) {
    char retained_name[CUPIDBUILD_HOST_PATH_BYTES];
    char moved_name[CUPIDBUILD_HOST_PATH_BYTES];
    cupidbuild_host_snapshot_t retained;
    cupidbuild_host_snapshot_t moved;
    int removed_moved;
    int removed_retained;
    int restored;
    (void)memset(&retained, 0, sizeof(retained));
    (void)memset(&moved, 0, sizeof(moved));
    int retained_written = snprintf(retained_name, sizeof(retained_name), "%s.reclaim-link-%08x",
                                    transaction->lock_name, owner);
    int moved_written = snprintf(moved_name, sizeof(moved_name), "%s.reclaim-move-%08x",
                                 transaction->lock_name, owner);
    if (retained_written <= 0 || (size_t)retained_written >= sizeof(retained_name) ||
        moved_written <= 0 || (size_t)moved_written >= sizeof(moved_name) ||
        !cupidbuild_host_link_entry_at(transaction->output_parent_descriptor,
                                       transaction->lock_name,
                                       transaction->output_parent_descriptor, retained_name)) {
        return 0;
    }
    if (!cupidbuild_host_regular_snapshot_at(transaction->output_parent_descriptor, retained_name,
                                             &retained) ||
        !cupidbuild_host_lock_snapshot_equal(&retained, expected)) {
        (void)cupidbuild_host_unlink_owned_entry_at(transaction->output_parent_descriptor,
                                                    retained_name, expected);
        return 0;
    }
    if (!cupidbuild_host_rename_entry_noreplace_at(
            transaction->output_parent_descriptor, transaction->lock_name,
            transaction->output_parent_descriptor, moved_name)) {
        (void)cupidbuild_host_unlink_owned_entry_at(transaction->output_parent_descriptor,
                                                    retained_name, expected);
        return 0;
    }
    if (!cupidbuild_host_regular_snapshot_at(transaction->output_parent_descriptor, moved_name,
                                             &moved) ||
        !cupidbuild_host_lock_snapshot_equal(&moved, expected)) {
        cupidbuild_host_snapshot_t restored_snapshot;
        restored =
            cupidbuild_host_link_entry_at(transaction->output_parent_descriptor, retained_name,
                                          transaction->output_parent_descriptor,
                                          transaction->lock_name) &&
            cupidbuild_host_regular_snapshot_at(transaction->output_parent_descriptor,
                                                transaction->lock_name, &restored_snapshot) &&
            cupidbuild_host_lock_snapshot_equal(&restored_snapshot, expected);
        if (restored) {
            (void)cupidbuild_host_unlink_owned_entry_at(transaction->output_parent_descriptor,
                                                        retained_name, expected);
        }
        return 0;
    }
    removed_moved = cupidbuild_host_unlink_owned_entry_at(transaction->output_parent_descriptor,
                                                          moved_name, expected);
    removed_retained = cupidbuild_host_unlink_owned_entry_at(transaction->output_parent_descriptor,
                                                             retained_name, expected);
    return removed_moved && removed_retained;
}

static int cupidbuild_host_release_lock_at(cupidbuild_host_transaction_t *transaction) {
    char retained_name[CUPIDBUILD_HOST_PATH_BYTES];
    char moved_name[CUPIDBUILD_HOST_PATH_BYTES];
    cupidbuild_host_snapshot_t retained;
    cupidbuild_host_snapshot_t moved;
    int removed_moved;
    int removed_retained;
    int restored;
    (void)memset(&retained, 0, sizeof(retained));
    (void)memset(&moved, 0, sizeof(moved));
    unsigned int process_id = cupidbuild_host_process_id();
    int retained_written = snprintf(retained_name, sizeof(retained_name), "%s.release-link-%08x",
                                    transaction->lock_name, process_id);
    int moved_written = snprintf(moved_name, sizeof(moved_name), "%s.release-move-%08x",
                                 transaction->lock_name, process_id);
    if (retained_written <= 0 || (size_t)retained_written >= sizeof(retained_name) ||
        moved_written <= 0 || (size_t)moved_written >= sizeof(moved_name) ||
        !cupidbuild_host_link_entry_at(transaction->output_parent_descriptor,
                                       transaction->lock_name,
                                       transaction->output_parent_descriptor, retained_name)) {
        return 0;
    }
    if (!cupidbuild_host_regular_snapshot_at(transaction->output_parent_descriptor, retained_name,
                                             &retained) ||
        !cupidbuild_host_lock_snapshot_equal(&retained, &transaction->lock_snapshot)) {
        (void)cupidbuild_host_unlink_owned_entry_at(transaction->output_parent_descriptor,
                                                    retained_name, &transaction->lock_snapshot);
        return 0;
    }
    if (!cupidbuild_host_rename_entry_noreplace_at(
            transaction->output_parent_descriptor, transaction->lock_name,
            transaction->output_parent_descriptor, moved_name)) {
        (void)cupidbuild_host_unlink_owned_entry_at(transaction->output_parent_descriptor,
                                                    retained_name, &transaction->lock_snapshot);
        return 0;
    }
    if (!cupidbuild_host_regular_snapshot_at(transaction->output_parent_descriptor, moved_name,
                                             &moved) ||
        !cupidbuild_host_lock_snapshot_equal(&moved, &transaction->lock_snapshot)) {
        cupidbuild_host_snapshot_t restored_snapshot;
        restored =
            cupidbuild_host_link_entry_at(transaction->output_parent_descriptor, retained_name,
                                          transaction->output_parent_descriptor,
                                          transaction->lock_name) &&
            cupidbuild_host_regular_snapshot_at(transaction->output_parent_descriptor,
                                                transaction->lock_name, &restored_snapshot) &&
            cupidbuild_host_lock_snapshot_equal(&restored_snapshot, &transaction->lock_snapshot);
        if (restored) {
            (void)cupidbuild_host_unlink_owned_entry_at(transaction->output_parent_descriptor,
                                                        retained_name, &transaction->lock_snapshot);
        }
        return 0;
    }
    removed_moved = cupidbuild_host_unlink_owned_entry_at(transaction->output_parent_descriptor,
                                                          moved_name, &transaction->lock_snapshot);
    removed_retained = cupidbuild_host_unlink_owned_entry_at(
        transaction->output_parent_descriptor, retained_name, &transaction->lock_snapshot);
    return removed_moved && removed_retained;
}
#endif
