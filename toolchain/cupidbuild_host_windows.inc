/**
 * @file cupidbuild_host_windows.inc
 * @brief Retain Windows files and run checked Windows processes.
 */

static int cupidbuild_host_path_missing(const char *path) {
    DWORD attributes = GetFileAttributesA(path);
    DWORD error = GetLastError();
    return attributes == INVALID_FILE_ATTRIBUTES &&
           (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND);
}

static int cupidbuild_host_path_has_link(const char *path) {
    char prefix[CUPIDBUILD_HOST_PATH_BYTES];
    size_t index;
    size_t size = strlen(path);
    if (size + 1u > sizeof(prefix)) {
        return 1;
    }
    (void)memcpy(prefix, path, size + 1u);
    for (index = 0u; index <= size; index++) {
        if ((prefix[index] == '/' || prefix[index] == '\\' || prefix[index] == '\0') &&
            index > 2u) {
            DWORD attributes;
            char saved = prefix[index];
            prefix[index] = '\0';
            attributes = GetFileAttributesA(prefix);
            prefix[index] = saved;
            if (attributes == INVALID_FILE_ATTRIBUTES ||
                (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) != 0u) {
                return 1;
            }
        }
    }
    return 0;
}

static int cupidbuild_host_read_regular_limit(const char *path, int optional, size_t limit,
                                              cupidbuild_host_snapshot_t *snapshot,
                                              unsigned char **bytes_out) {
    HANDLE handle;
    BY_HANDLE_FILE_INFORMATION information;
    unsigned char *bytes = (unsigned char *)0;
    unsigned char digest[32];
    size_t size;
    (void)memset(snapshot, 0, sizeof(*snapshot));
    if (optional != 0 && GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES &&
        (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND)) {
        snapshot->present = 0;
        if (bytes_out != (unsigned char **)0) {
            *bytes_out = (unsigned char *)0;
        }
        return 1;
    }
    if (cupidbuild_host_path_has_link(path)) {
        return 0;
    }
    handle = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         (LPSECURITY_ATTRIBUTES)0, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, (HANDLE)0);
    if (handle == INVALID_HANDLE_VALUE) {
        if (optional != 0 &&
            (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND)) {
            snapshot->present = 0;
            if (bytes_out != (unsigned char **)0) {
                *bytes_out = (unsigned char *)0;
            }
            return 1;
        }
        return 0;
    }
    if (!GetFileInformationByHandle(handle, &information) ||
        (information.dwFileAttributes &
         (FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0u ||
        information.nFileSizeHigh != 0u || information.nFileSizeLow > limit) {
        (void)CloseHandle(handle);
        return 0;
    }
    size = (size_t)information.nFileSizeLow;
    if (!cupidbuild_host_read_open_contents(
            handle, size, bytes_out != (unsigned char **)0 ? &bytes : (unsigned char **)0,
            digest)) {
        (void)CloseHandle(handle);
        return 0;
    }
    snapshot->present = 1;
    snapshot->size = size;
    snapshot->identity[0] = information.dwVolumeSerialNumber;
    snapshot->identity[1] = information.nFileIndexHigh;
    snapshot->identity[2] = information.nFileIndexLow;
    snapshot->modified[0] = information.ftLastWriteTime.dwHighDateTime;
    snapshot->modified[1] = information.ftLastWriteTime.dwLowDateTime;
    (void)memcpy(snapshot->sha256, digest, sizeof(digest));
    (void)CloseHandle(handle);
    if (bytes_out != (unsigned char **)0) {
        *bytes_out = bytes;
    } else {
        free(bytes);
    }
    return 1;
}

static int cupidbuild_host_read_regular(const char *path, int optional,
                                        cupidbuild_host_snapshot_t *snapshot,
                                        unsigned char **bytes_out) {
    return cupidbuild_host_read_regular_limit(path, optional, CUPIDBUILD_HOST_FILE_LIMIT, snapshot,
                                              bytes_out);
}

static int cupidbuild_host_directory_snapshot(const char *path,
                                              cupidbuild_host_snapshot_t *snapshot) {
    HANDLE handle;
    BY_HANDLE_FILE_INFORMATION information;
    (void)memset(snapshot, 0, sizeof(*snapshot));
    if (cupidbuild_host_path_has_link(path)) {
        return 0;
    }
    handle = CreateFileA(path, FILE_READ_ATTRIBUTES,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         (LPSECURITY_ATTRIBUTES)0, OPEN_EXISTING,
                         FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, (HANDLE)0);
    if (handle == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle, &information) ||
        (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0u ||
        (information.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) !=
            0u) {
        if (handle != INVALID_HANDLE_VALUE) {
            (void)CloseHandle(handle);
        }
        return 0;
    }
    snapshot->present = 1;
    snapshot->identity[0] = information.dwVolumeSerialNumber;
    snapshot->identity[1] = information.nFileIndexHigh;
    snapshot->identity[2] = information.nFileIndexLow;
    snapshot->modified[0] = information.ftLastWriteTime.dwHighDateTime;
    snapshot->modified[1] = information.ftLastWriteTime.dwLowDateTime;
    (void)CloseHandle(handle);
    return 1;
}

static int cupidbuild_host_windows_directory_handle_snapshot(HANDLE handle,
                                                             cupidbuild_host_snapshot_t *snapshot) {
    BY_HANDLE_FILE_INFORMATION information;
    (void)memset(snapshot, 0, sizeof(*snapshot));
    if (handle == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle, &information) ||
        (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0u ||
        (information.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) !=
            0u) {
        return 0;
    }
    snapshot->present = 1;
    snapshot->identity[0] = information.dwVolumeSerialNumber;
    snapshot->identity[1] = information.nFileIndexHigh;
    snapshot->identity[2] = information.nFileIndexLow;
    snapshot->modified[0] = information.ftLastWriteTime.dwHighDateTime;
    snapshot->modified[1] = information.ftLastWriteTime.dwLowDateTime;
    return 1;
}

static HANDLE cupidbuild_host_windows_open_relative(HANDLE parent, const char *name, int directory,
                                                    int read_contents);
static HANDLE cupidbuild_host_windows_open_relative_access(HANDLE parent, const char *name,
                                                           int directory, int read_contents,
                                                           unsigned long extra_access);
static HANDLE cupidbuild_host_windows_open_relative_access_status(HANDLE parent, const char *name,
                                                                  int directory, int read_contents,
                                                                  unsigned long extra_access,
                                                                  long *status_out);
static HANDLE cupidbuild_host_windows_open_relative_access_share_status(
    HANDLE parent, const char *name, int directory, int read_contents, unsigned long extra_access,
    unsigned long share_access, long *status_out);
static HANDLE cupidbuild_host_windows_open_relative_path(HANDLE root, const char *logical,
                                                         int directory, int read_contents);
static HANDLE cupidbuild_host_windows_open_relative_path_access(HANDLE root, const char *logical,
                                                                int directory, int read_contents,
                                                                unsigned long extra_access);
static HANDLE cupidbuild_host_windows_open_repository(const char *path);
static int cupidbuild_host_windows_query_directory(
    HANDLE directory, int restart, char *name, size_t name_capacity, DWORD *attributes_out,
    DWORD *file_id_high_out, DWORD *file_id_low_out, DWORD *last_write_high_out,
    DWORD *last_write_low_out, DWORD *change_high_out, DWORD *change_low_out, int *complete_out);
static int cupidbuild_host_windows_named_directory_snapshot(
    cupidbuild_host_transaction_t *transaction, const char *logical, HANDLE retained,
    cupidbuild_host_snapshot_t *snapshot, int *metadata_unsettled_out);
#if defined(CUPIDBUILD_PROFILE_DIRECTORY_RACE_TEST) && !defined(CUPIDBUILD_CUSTOM_LINUX)
static int cupidbuild_host_profile_directory_test_pause(const char *ready_variable,
                                                        const char *resume_variable);
#endif

static int cupidbuild_host_seed_members_platform(const char *directory, const char *suffix,
                                                 const char *const *expected,
                                                 size_t expected_count) {
    char pattern[CUPIDBUILD_HOST_PATH_BYTES];
    WIN32_FIND_DATAW entry;
    unsigned short wide_pattern[CUPIDBUILD_HOST_PATH_BYTES];
    char name[781];
    size_t units;
    size_t bytes;
    HANDLE search;
    int valid = 1;
    if (!cupidbuild_host_join(pattern, sizeof(pattern), directory, "*")) {
        return 0;
    }
    if (!cupidbuild_path_to_utf16(pattern, strlen(pattern), wide_pattern,
                                  sizeof(wide_pattern) / sizeof(wide_pattern[0]), &units))
        return 0;
    search = FindFirstFileW(wide_pattern, &entry);
    if (search == INVALID_HANDLE_VALUE) {
        return 0;
    }
    do {
        for (units = 0u; units < 260u && entry.cFileName[units] != 0u; units++) {
        }
        if (units == 260u ||
            !cupidbuild_path_to_utf8(entry.cFileName, units, name, sizeof(name), &bytes)) {
            valid = 0;
            break;
        }
        if (cupidbuild_host_name_has_suffix(name, suffix) &&
            !cupidbuild_host_name_is_expected(name, expected, expected_count)) {
            valid = 0;
            break;
        }
    } while (FindNextFileW(search, &entry));
    if (valid != 0 && GetLastError() != ERROR_NO_MORE_FILES) {
        valid = 0;
    }
    if (!FindClose(search)) {
        valid = 0;
    }
    return valid;
}

static int cupidbuild_host_discover_platform(cupidbuild_host_transaction_t *transaction,
                                             const char *logical_root, const char *const *suffixes,
                                             size_t suffix_count, int skip_hidden_files,
                                             int reject_matching_nonfiles,
                                             cupidbuild_host_path_list_t *paths) {
    cupidbuild_host_discovery_worklist_t worklist;
    cupidbuild_host_snapshot_t *visited;
    cupidbuild_host_snapshot_t repository_snapshot;
    cupidbuild_host_snapshot_t root_snapshot;
    HANDLE repository = transaction->repository_root_handle;
    HANDLE root = INVALID_HANDLE_VALUE;
    size_t visited_count = 0u;
    size_t directory_count = 0u;
    int valid = 1;
    (void)memset(&worklist, 0, sizeof(worklist));
    visited = (cupidbuild_host_snapshot_t *)calloc(CUPIDBUILD_HOST_DISCOVERY_DIRECTORIES,
                                                   sizeof(*visited));
    if (visited == (cupidbuild_host_snapshot_t *)0) {
        free(visited);
        return 0;
    }
    if (!cupidbuild_host_windows_directory_handle_snapshot(repository, &repository_snapshot) ||
        !cupidbuild_host_snapshot_identity_equal(&transaction->repository_root_snapshot,
                                                 &repository_snapshot)) {
        valid = 0;
    } else {
        unsigned int stable_attempt;
        int root_ready = 0;
        for (stable_attempt = 0u; stable_attempt < 4u; stable_attempt++) {
            int metadata_unsettled = 0;
            root = cupidbuild_host_windows_open_relative_path(repository, logical_root, 1, 1);
            if (cupidbuild_host_windows_named_directory_snapshot(
                    transaction, logical_root, root, &root_snapshot, &metadata_unsettled) &&
                !cupidbuild_host_snapshot_identity_equal(&repository_snapshot, &root_snapshot) &&
                cupidbuild_host_discovery_worklist_add(&worklist, logical_root, &root_snapshot,
                                                       root)) {
                root_ready = 1;
                root = INVALID_HANDLE_VALUE;
                break;
            }
            if (root != INVALID_HANDLE_VALUE) {
                (void)CloseHandle(root);
                root = INVALID_HANDLE_VALUE;
            }
            if (metadata_unsettled == 0) {
                break;
            }
        }
        if (root_ready == 0) {
            valid = 0;
        } else {
            visited[visited_count++] = root_snapshot;
            directory_count = 1u;
        }
    }
    if (root != INVALID_HANDLE_VALUE) {
        (void)CloseHandle(root);
    }
    while (valid != 0 && worklist.count != 0u) {
        cupidbuild_host_discovery_directory_t current = worklist.directories[worklist.count - 1u];
        cupidbuild_host_snapshot_t completed_snapshot;
        int retained = 0;
        int restart = 1;
        int complete = 0;
        worklist.count--;
        while (valid != 0) {
            char name[1417];
            char logical[CUPIDBUILD_HOST_PATH_BYTES];
            DWORD attributes = 0u;
            DWORD file_id_high = 0u;
            DWORD file_id_low = 0u;
            DWORD last_write_high = 0u;
            DWORD last_write_low = 0u;
            DWORD change_high = 0u;
            DWORD change_low = 0u;
            int directory_entry;
            int matches;
            if (!cupidbuild_host_windows_query_directory(
                    current.handle, restart, name, sizeof(name), &attributes, &file_id_high,
                    &file_id_low, &last_write_high, &last_write_low, &change_high, &change_low,
                    &complete)) {
                valid = 0;
                break;
            }
            restart = 0;
            if (complete != 0) {
                break;
            }
            if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
                directory_entry = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0u;
                if (!(name[0] == '.' && (directory_entry != 0 || skip_hidden_files != 0))) {
                    if ((attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) !=
                            0u ||
                        !cupidbuild_host_join(logical, sizeof(logical), current.logical, name)) {
                        valid = 0;
                        break;
                    }
                    matches = cupidbuild_host_discovery_suffix(name, suffixes, suffix_count);
                    if (directory_entry != 0 && matches != 0 && reject_matching_nonfiles != 0) {
                        valid = 0;
                        break;
                    }
                    if (directory_entry != 0) {
                        cupidbuild_host_snapshot_t child_snapshot;
                        HANDLE child = INVALID_HANDLE_VALUE;
                        unsigned int stable_attempt;
                        size_t prior;
                        int alias = 0;
                        int child_ready = 0;
                        for (stable_attempt = 0u; stable_attempt < 4u; stable_attempt++) {
                            int metadata_unsettled = 0;
                            child =
                                cupidbuild_host_windows_open_relative(current.handle, name, 1, 1);
                            if (cupidbuild_host_windows_named_directory_snapshot(
                                    transaction, logical, child, &child_snapshot,
                                    &metadata_unsettled) &&
                                (file_id_high != 0u || file_id_low != 0u) &&
                                child_snapshot.identity[0] == current.snapshot.identity[0] &&
                                child_snapshot.identity[1] == file_id_high &&
                                child_snapshot.identity[2] == file_id_low) {
                                child_ready = 1;
                                break;
                            }
                            if (child != INVALID_HANDLE_VALUE) {
                                (void)CloseHandle(child);
                                child = INVALID_HANDLE_VALUE;
                            }
                            if (metadata_unsettled == 0) {
                                break;
                            }
                        }
                        if (child_ready == 0) {
                            if (child != INVALID_HANDLE_VALUE) {
                                (void)CloseHandle(child);
                            }
                            valid = 0;
                            break;
                        }
                        (void)last_write_high;
                        (void)last_write_low;
                        (void)change_high;
                        (void)change_low;
                        for (prior = 0u; prior < visited_count; prior++) {
                            if (cupidbuild_host_snapshot_identity_equal(&visited[prior],
                                                                        &child_snapshot)) {
                                alias = 1;
                                break;
                            }
                        }
                        if (alias != 0 ||
                            directory_count >= CUPIDBUILD_HOST_DISCOVERY_DIRECTORIES ||
                            !cupidbuild_host_discovery_worklist_add(&worklist, logical,
                                                                    &child_snapshot, child)) {
                            (void)CloseHandle(child);
                            valid = 0;
                            break;
                        }
                        visited[visited_count++] = child_snapshot;
                        directory_count++;
                    } else if (matches != 0) {
                        HANDLE child;
                        BY_HANDLE_FILE_INFORMATION information;
                        cupidbuild_host_snapshot_t child_snapshot;
                        child = cupidbuild_host_windows_open_relative(current.handle, name, 0, 1);
                        if (child == INVALID_HANDLE_VALUE ||
                            !GetFileInformationByHandle(child, &information) ||
                            (information.dwFileAttributes &
                             (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0u ||
                            (file_id_high == 0u && file_id_low == 0u) ||
                            information.dwVolumeSerialNumber != current.snapshot.identity[0] ||
                            information.nFileIndexHigh != file_id_high ||
                            information.nFileIndexLow != file_id_low) {
                            if (child != INVALID_HANDLE_VALUE) {
                                (void)CloseHandle(child);
                            }
                            valid = 0;
                            break;
                        }
                        if ((information.dwFileAttributes & FILE_ATTRIBUTE_DEVICE) != 0u) {
                            if (reject_matching_nonfiles != 0) {
                                (void)CloseHandle(child);
                                valid = 0;
                                break;
                            }
                        } else if (!cupidbuild_host_windows_read_open_regular(
                                       child, CUPIDBUILD_HOST_FILE_LIMIT, &child_snapshot,
                                       (unsigned char **)0) ||
                                   !cupidbuild_host_discovery_add(paths, logical,
                                                                  &child_snapshot)) {
                            (void)CloseHandle(child);
                            valid = 0;
                            break;
                        }
                        if (!CloseHandle(child)) {
                            valid = 0;
                            break;
                        }
                    }
                }
            }
        }
        if (valid != 0 &&
            (!cupidbuild_host_windows_named_directory_snapshot(
                 transaction, current.logical, current.handle, &completed_snapshot, (int *)0) ||
             !cupidbuild_host_discovery_snapshot_equal(transaction, &current.snapshot,
                                                       &completed_snapshot))) {
            valid = 0;
        }
        if (valid != 0) {
            current.snapshot = completed_snapshot;
            if (!cupidbuild_host_bind_discovery_directory(transaction, &current, &retained)) {
                valid = 0;
            }
        }
        if (retained != 0) {
            current.handle = INVALID_HANDLE_VALUE;
            current.logical = (char *)0;
        }
        if (current.handle != INVALID_HANDLE_VALUE && !CloseHandle(current.handle)) {
            valid = 0;
        }
        free(current.logical);
    }
    while (worklist.count != 0u) {
        cupidbuild_host_discovery_directory_t *directory = &worklist.directories[--worklist.count];
        (void)CloseHandle(directory->handle);
        free(directory->logical);
    }
    free(worklist.directories);
    free(visited);
    return valid;
}

static int cupidbuild_host_absolute_directory(char *destination, size_t capacity,
                                              const char *path) {
    return _fullpath(destination, path, capacity) != (char *)0;
}

static int cupidbuild_host_make_executable(const char *path) {
    return path != (const char *)0;
}

static void cupidbuild_host_delete_file(const char *path) {
    (void)DeleteFileA(path);
}

static unsigned int cupidbuild_host_process_id(void) {
    return GetCurrentProcessId();
}

static int cupidbuild_host_process_alive(unsigned int process_id) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, 0, (DWORD)process_id);
    DWORD status = 0u;
    if (process == (HANDLE)0) {
        return GetLastError() != ERROR_INVALID_PARAMETER;
    }
    if (!GetExitCodeProcess(process, &status)) {
        (void)CloseHandle(process);
        return 1;
    }
    (void)CloseHandle(process);
    return status == STILL_ACTIVE;
}

static int cupidbuild_host_windows_append_character(char *command, size_t capacity, size_t *used,
                                                    char character) {
    if (*used + 1u >= capacity) {
        return 0;
    }
    command[*used] = character;
    *used = *used + 1u;
    command[*used] = '\0';
    return 1;
}

static int cupidbuild_host_windows_append_argument(char *command, size_t capacity, size_t *used,
                                                   const char *argument) {
    const char *cursor = argument;
    if (*used != 0u && !cupidbuild_host_windows_append_character(command, capacity, used, ' ')) {
        return 0;
    }
    if (!cupidbuild_host_windows_append_character(command, capacity, used, '"')) {
        return 0;
    }
    while (*cursor != '\0') {
        size_t slash_count = 0u;
        size_t index;
        while (cursor[slash_count] == '\\') {
            slash_count++;
        }
        cursor += slash_count;
        if (*cursor == '"') {
            for (index = 0u; index < slash_count * 2u + 1u; index++) {
                if (!cupidbuild_host_windows_append_character(command, capacity, used, '\\')) {
                    return 0;
                }
            }
            if (!cupidbuild_host_windows_append_character(command, capacity, used, '"')) {
                return 0;
            }
            cursor++;
        } else if (*cursor == '\0') {
            for (index = 0u; index < slash_count * 2u; index++) {
                if (!cupidbuild_host_windows_append_character(command, capacity, used, '\\')) {
                    return 0;
                }
            }
        } else {
            for (index = 0u; index < slash_count; index++) {
                if (!cupidbuild_host_windows_append_character(command, capacity, used, '\\')) {
                    return 0;
                }
            }
            if (!cupidbuild_host_windows_append_character(command, capacity, used, *cursor)) {
                return 0;
            }
            cursor++;
        }
    }
    return cupidbuild_host_windows_append_character(command, capacity, used, '"');
}

static int cupidbuild_host_windows_open_snapshot(HANDLE handle,
                                                 cupidbuild_host_snapshot_t *snapshot) {
    BY_HANDLE_FILE_INFORMATION information;
    if (snapshot == (cupidbuild_host_snapshot_t *)0) {
        return 1;
    }
    (void)memset(snapshot, 0, sizeof(*snapshot));
    if (!GetFileInformationByHandle(handle, &information) || information.nFileSizeHigh != 0u) {
        return 0;
    }
    snapshot->present = 1;
    snapshot->size = (size_t)information.nFileSizeLow;
    snapshot->identity[0] = information.dwVolumeSerialNumber;
    snapshot->identity[1] = information.nFileIndexHigh;
    snapshot->identity[2] = information.nFileIndexLow;
    snapshot->modified[0] = information.ftLastWriteTime.dwHighDateTime;
    snapshot->modified[1] = information.ftLastWriteTime.dwLowDateTime;
    return 1;
}

static void cupidbuild_host_windows_close_attribute_list(STARTUPINFOEXA *startup, int initialized) {
    if (startup == (STARTUPINFOEXA *)0) {
        return;
    }
    if (initialized != 0 && startup->lpAttributeList != (LPPROC_THREAD_ATTRIBUTE_LIST)0) {
        DeleteProcThreadAttributeList(startup->lpAttributeList);
    }
    free(startup->lpAttributeList);
    startup->lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)0;
}

static int cupidbuild_host_run_process(
    const char *tool, const cupidbuild_host_snapshot_t *expected_tool, HANDLE tool_handle,
    const char *const *arguments, const char *stdout_path, const char *stderr_path,
    HANDLE stdout_handle, HANDLE stderr_handle, const char *working_directory,
    int working_descriptor, const cupidbuild_host_transaction_t *inherited_transaction,
    cupidbuild_host_snapshot_t *stdout_opened, cupidbuild_host_snapshot_t *stderr_opened,
    unsigned int timeout_milliseconds) {
    STARTUPINFOEXA startup;
    PROCESS_INFORMATION process;
    SECURITY_ATTRIBUTES security;
    HANDLE standard_input = INVALID_HANDLE_VALUE;
    HANDLE standard_output = INVALID_HANDLE_VALUE;
    HANDLE standard_error = INVALID_HANDLE_VALUE;
    HANDLE inherited_handles[3];
    cupidbuild_host_snapshot_t tool_path_snapshot;
    cupidbuild_host_snapshot_t tool_snapshot;
    char *command = (char *)0;
    size_t used = 0u;
    size_t index;
    DWORD wait_status;
    DWORD exit_code = 125u;
    SIZE_T attribute_list_size = 0u;
    int attribute_list_initialized = 0;
    int adapter_failed = 0;
    int streams_inheritable = 0;
    int stdout_inherit_cleared;
    int stderr_inherit_cleared;
    int own_stream_handles =
        stdout_handle == INVALID_HANDLE_VALUE && stderr_handle == INVALID_HANDLE_VALUE;
    (void)working_descriptor;
    (void)inherited_transaction;
    (void)memset(&startup, 0, sizeof(startup));
    (void)memset(&process, 0, sizeof(process));
    (void)memset(&security, 0, sizeof(security));
    security.nLength = (DWORD)sizeof(security);
    security.bInheritHandle = 1;
    startup.StartupInfo.cb = (DWORD)sizeof(startup);
    if ((stdout_handle == INVALID_HANDLE_VALUE) != (stderr_handle == INVALID_HANDLE_VALUE)) {
        return -1;
    }
    if (tool_handle == INVALID_HANDLE_VALUE ||
        !cupidbuild_host_windows_read_open_regular(tool_handle, CUPIDBUILD_HOST_FILE_LIMIT,
                                                   &tool_snapshot, (unsigned char **)0) ||
        !cupidbuild_host_snapshot_equal(&tool_snapshot, expected_tool) ||
        !cupidbuild_host_read_regular(tool, 0, &tool_path_snapshot, (unsigned char **)0) ||
        !cupidbuild_host_snapshot_equal(&tool_path_snapshot, expected_tool)) {
        return -1;
    }
    standard_input = CreateFileA("NUL", GENERIC_READ, 0u, &security, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL, (HANDLE)0);
    if (own_stream_handles != 0) {
        standard_output = CreateFileA(stdout_path, GENERIC_WRITE, 0u, &security, CREATE_NEW,
                                      FILE_ATTRIBUTE_NORMAL, (HANDLE)0);
        standard_error = CreateFileA(stderr_path, GENERIC_WRITE, 0u, &security, CREATE_NEW,
                                     FILE_ATTRIBUTE_NORMAL, (HANDLE)0);
    } else {
        standard_output = stdout_handle;
        standard_error = stderr_handle;
    }
    if (standard_input == INVALID_HANDLE_VALUE || standard_output == INVALID_HANDLE_VALUE ||
        standard_error == INVALID_HANDLE_VALUE ||
        !cupidbuild_host_windows_open_snapshot(standard_output, stdout_opened) ||
        !cupidbuild_host_windows_open_snapshot(standard_error, stderr_opened)) {
        if (standard_input != INVALID_HANDLE_VALUE) {
            (void)CloseHandle(standard_input);
        }
        if (own_stream_handles != 0 && standard_output != INVALID_HANDLE_VALUE) {
            (void)CloseHandle(standard_output);
        }
        if (own_stream_handles != 0 && standard_error != INVALID_HANDLE_VALUE) {
            (void)CloseHandle(standard_error);
        }
        return -1;
    }
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = standard_input;
    startup.StartupInfo.hStdOutput = standard_output;
    startup.StartupInfo.hStdError = standard_error;
    command = (char *)calloc(CUPIDBUILD_HOST_WINDOWS_COMMAND_BYTES, 1u);
    if (command == (char *)0 || !cupidbuild_host_windows_append_argument(
                                    command, CUPIDBUILD_HOST_WINDOWS_COMMAND_BYTES, &used, tool)) {
        (void)CloseHandle(standard_input);
        if (own_stream_handles != 0) {
            (void)CloseHandle(standard_output);
            (void)CloseHandle(standard_error);
        }
        free(command);
        return -1;
    }
    for (index = 0u; arguments[index] != (const char *)0; index++) {
        if (!cupidbuild_host_windows_append_argument(command, CUPIDBUILD_HOST_WINDOWS_COMMAND_BYTES,
                                                     &used, arguments[index])) {
            (void)CloseHandle(standard_input);
            if (own_stream_handles != 0) {
                (void)CloseHandle(standard_output);
                (void)CloseHandle(standard_error);
            }
            free(command);
            return -1;
        }
    }
    inherited_handles[0] = standard_input;
    inherited_handles[1] = standard_output;
    inherited_handles[2] = standard_error;
    (void)InitializeProcThreadAttributeList((LPPROC_THREAD_ATTRIBUTE_LIST)0, 1u, 0u,
                                            &attribute_list_size);
    if (attribute_list_size == 0u) {
        (void)CloseHandle(standard_input);
        if (own_stream_handles != 0) {
            (void)CloseHandle(standard_output);
            (void)CloseHandle(standard_error);
        }
        free(command);
        return -1;
    }
    startup.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)calloc(attribute_list_size, 1u);
    if (startup.lpAttributeList == (LPPROC_THREAD_ATTRIBUTE_LIST)0 ||
        !InitializeProcThreadAttributeList(startup.lpAttributeList, 1u, 0u, &attribute_list_size)) {
        cupidbuild_host_windows_close_attribute_list(&startup, 0);
        (void)CloseHandle(standard_input);
        if (own_stream_handles != 0) {
            (void)CloseHandle(standard_output);
            (void)CloseHandle(standard_error);
        }
        free(command);
        return -1;
    }
    attribute_list_initialized = 1;
    if (own_stream_handles == 0) {
        if (!SetHandleInformation(standard_output, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT) ||
            !SetHandleInformation(standard_error, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) {
            (void)SetHandleInformation(standard_output, HANDLE_FLAG_INHERIT, 0u);
            (void)SetHandleInformation(standard_error, HANDLE_FLAG_INHERIT, 0u);
            cupidbuild_host_windows_close_attribute_list(&startup, attribute_list_initialized);
            (void)CloseHandle(standard_input);
            free(command);
            return -1;
        }
        streams_inheritable = 1;
    }
    if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0u, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                   inherited_handles, (SIZE_T)sizeof(inherited_handles), (void *)0,
                                   (SIZE_T *)0)) {
        if (streams_inheritable != 0) {
            (void)SetHandleInformation(standard_output, HANDLE_FLAG_INHERIT, 0u);
            (void)SetHandleInformation(standard_error, HANDLE_FLAG_INHERIT, 0u);
        }
        cupidbuild_host_windows_close_attribute_list(&startup, attribute_list_initialized);
        (void)CloseHandle(standard_input);
        if (own_stream_handles != 0) {
            (void)CloseHandle(standard_output);
            (void)CloseHandle(standard_error);
        }
        free(command);
        return -1;
    }
    if (!CreateProcessA(tool, command, (LPSECURITY_ATTRIBUTES)0, (LPSECURITY_ATTRIBUTES)0, 1,
                        EXTENDED_STARTUPINFO_PRESENT, (void *)0, working_directory,
                        &startup.StartupInfo, &process)) {
        if (streams_inheritable != 0) {
            (void)SetHandleInformation(standard_output, HANDLE_FLAG_INHERIT, 0u);
            (void)SetHandleInformation(standard_error, HANDLE_FLAG_INHERIT, 0u);
        }
        cupidbuild_host_windows_close_attribute_list(&startup, attribute_list_initialized);
        (void)CloseHandle(standard_input);
        if (own_stream_handles != 0) {
            (void)CloseHandle(standard_output);
            (void)CloseHandle(standard_error);
        }
        free(command);
        return -1;
    }
    cupidbuild_host_windows_close_attribute_list(&startup, attribute_list_initialized);
    if (streams_inheritable != 0) {
        stdout_inherit_cleared =
            SetHandleInformation(standard_output, HANDLE_FLAG_INHERIT, 0u) != 0;
        stderr_inherit_cleared = SetHandleInformation(standard_error, HANDLE_FLAG_INHERIT, 0u) != 0;
        if (stdout_inherit_cleared == 0 || stderr_inherit_cleared == 0) {
            adapter_failed = 1;
        }
    }
    free(command);
    (void)CloseHandle(standard_input);
    if (own_stream_handles != 0) {
        (void)CloseHandle(standard_output);
        (void)CloseHandle(standard_error);
    }
#if defined(CUPIDBUILD_PUBLICATION_RACE_TEST)
    if (!cupidbuild_host_publication_test_pause("after-tool-launch")) {
        adapter_failed = 1;
    }
#endif
    wait_status = WaitForSingleObject(process.hProcess, (DWORD)timeout_milliseconds);
    if (wait_status == WAIT_TIMEOUT) {
        int terminated;
#if defined(CUPIDBUILD_HOST_WINDOWS_TERMINATION_TEST)
        if (getenv("CUPIDBUILD_WINDOWS_TEST_FORCE_TERMINATION_FAILURE") != (const char *)0) {
            terminated = 0;
        } else
#endif
        {
            terminated = TerminateProcess(process.hProcess, 124u) != 0;
        }
        DWORD reaped =
            WaitForSingleObject(process.hProcess, CUPIDBUILD_HOST_WINDOWS_REAP_MILLISECONDS);
        if (terminated == 0 || reaped != WAIT_OBJECT_0) {
            adapter_failed = 1;
        }
        exit_code = 124u;
    } else if (wait_status != WAIT_OBJECT_0) {
        int terminated = TerminateProcess(process.hProcess, 125u) != 0;
        DWORD reaped =
            WaitForSingleObject(process.hProcess, CUPIDBUILD_HOST_WINDOWS_REAP_MILLISECONDS);
        (void)terminated;
        (void)reaped;
        adapter_failed = 1;
    } else if (!GetExitCodeProcess(process.hProcess, &exit_code)) {
        adapter_failed = 1;
    }
    (void)CloseHandle(process.hThread);
    (void)CloseHandle(process.hProcess);
    if (wait_status == WAIT_TIMEOUT && adapter_failed == 0) {
        return -2;
    }
    if (wait_status != WAIT_OBJECT_0 || adapter_failed != 0) {
        return -1;
    }
    return (int)exit_code;
}

typedef struct {
    unsigned int status;
#if defined(_WIN64)
    unsigned int status_padding;
    unsigned long long information;
#else
    unsigned int information;
#endif
} cupidbuild_windows_io_status_t;

typedef struct {
    unsigned short length;
    unsigned short maximum_length;
    unsigned short *buffer;
} cupidbuild_windows_unicode_string_t;

typedef struct {
    unsigned long length;
    HANDLE root_directory;
    cupidbuild_windows_unicode_string_t *object_name;
    unsigned long attributes;
    void *security_descriptor;
    void *security_quality_of_service;
} cupidbuild_windows_object_attributes_t;

typedef struct {
    unsigned char replace_if_exists;
#if defined(_WIN64)
    unsigned char padding[7];
#else
    unsigned char padding[3];
#endif
    HANDLE root_directory;
    DWORD file_name_length;
    unsigned short file_name[1];
} cupidbuild_windows_rename_t;

#if !defined(CUPID_HOSTED_I386_LINUX_ABI_H)
__declspec(dllimport) long __stdcall
NtCreateFile(HANDLE *file, unsigned long access, cupidbuild_windows_object_attributes_t *attributes,
             cupidbuild_windows_io_status_t *status, void *allocation_size,
             unsigned long file_attributes, unsigned long sharing, unsigned long disposition,
             unsigned long options, void *extended, unsigned long extended_size);
__declspec(dllimport) long __stdcall
NtQueryDirectoryFile(HANDLE file, HANDLE event, void *apc_routine, void *apc_context,
                     cupidbuild_windows_io_status_t *status, void *information,
                     unsigned long length, unsigned int information_class,
                     unsigned char return_single_entry,
                     cupidbuild_windows_unicode_string_t *file_name, unsigned char restart_scan);
__declspec(dllimport) long __stdcall NtSetInformationFile(HANDLE file,
                                                          cupidbuild_windows_io_status_t *status,
                                                          void *information, unsigned long length,
                                                          unsigned int information_class);
#define cupid_windows_nt_create_file NtCreateFile
#define cupid_windows_nt_query_directory_file NtQueryDirectoryFile
#define cupid_windows_nt_set_information_file NtSetInformationFile
#endif

#define CUPIDBUILD_WINDOWS_END_OF_FILE_INFORMATION 20u
#define CUPIDBUILD_WINDOWS_FILE_OPEN 1u
#define CUPIDBUILD_WINDOWS_FILE_CREATE 2u
#define CUPIDBUILD_WINDOWS_FILE_DIRECTORY_FILE 0x00000001u
#define CUPIDBUILD_WINDOWS_FILE_NON_DIRECTORY_FILE 0x00000040u
#define CUPIDBUILD_WINDOWS_FILE_SYNCHRONOUS_IO_NONALERT 0x00000020u
#define CUPIDBUILD_WINDOWS_FILE_OPEN_REPARSE_POINT 0x00200000u
#define CUPIDBUILD_WINDOWS_OBJECT_CASE_INSENSITIVE 0x00000040u
#define CUPIDBUILD_WINDOWS_OBJECT_DONT_REPARSE 0x00001000u
#define CUPIDBUILD_WINDOWS_OBJECT_INHERIT 0x00000002u

static unsigned int cupidbuild_host_windows_u32(const unsigned char *bytes) {
    return (unsigned int)bytes[0] | ((unsigned int)bytes[1] << 8u) |
           ((unsigned int)bytes[2] << 16u) | ((unsigned int)bytes[3] << 24u);
}

/* Component policy stays separate from scalar conversion. Capacities and
 * returned lengths here count UTF-16 units, not UTF-8 bytes. */
static int cupidbuild_host_windows_component(const char *name, unsigned short *output,
                                             size_t capacity, size_t *units) {
    size_t bytes;
    size_t index;
    *units = 0u;
    if (output != (unsigned short *)0 && capacity != 0u) {
        output[0] = 0u;
    }
    if (name == (const char *)0 || name[0] == '\0') {
        return 0;
    }
    bytes = strlen(name);
    for (index = 0u; index < bytes; index++) {
        if (name[index] == '/' || name[index] == '\\') {
            return 0;
        }
    }
    return cupidbuild_path_to_utf16(name, bytes, output, capacity, units);
}

static HANDLE cupidbuild_host_windows_open_relative_access_share_status(
    HANDLE parent, const char *name, int directory, int read_contents, unsigned long extra_access,
    unsigned long share_access, long *status_out) {
    unsigned short name_buffer[260];
    cupidbuild_windows_unicode_string_t unicode_name;
    cupidbuild_windows_object_attributes_t attributes;
    cupidbuild_windows_io_status_t status;
    BY_HANDLE_FILE_INFORMATION information;
    HANDLE handle = INVALID_HANDLE_VALUE;
    size_t name_size = 0u;
    unsigned long access = FILE_READ_ATTRIBUTES | SYNCHRONIZE | extra_access;
    unsigned long options = CUPIDBUILD_WINDOWS_FILE_SYNCHRONOUS_IO_NONALERT |
                            CUPIDBUILD_WINDOWS_FILE_OPEN_REPARSE_POINT;
    long result;
    if (status_out != (long *)0) {
        *status_out = (long)0xc000000du;
    }
    if (parent == INVALID_HANDLE_VALUE ||
        !cupidbuild_host_windows_component(
            name, name_buffer, sizeof(name_buffer) / sizeof(name_buffer[0]), &name_size)) {
        return INVALID_HANDLE_VALUE;
    }
    if (directory != 0) {
        access |= FILE_TRAVERSE;
        if (read_contents != 0) {
            access |= FILE_LIST_DIRECTORY;
        }
        options |= CUPIDBUILD_WINDOWS_FILE_DIRECTORY_FILE;
    } else {
        if (read_contents != 0) {
            access |= GENERIC_READ;
        }
        options |= CUPIDBUILD_WINDOWS_FILE_NON_DIRECTORY_FILE;
    }
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
    result = cupid_windows_nt_create_file(&handle, access, &attributes, &status, (void *)0, 0u,
                                          share_access, CUPIDBUILD_WINDOWS_FILE_OPEN, options,
                                          (void *)0, 0u);
    if (status_out != (long *)0) {
        *status_out = result;
    }
    if (result < 0 || !GetFileInformationByHandle(handle, &information) ||
        (information.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) !=
            0u ||
        (((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u) != (directory != 0))) {
        if (handle != INVALID_HANDLE_VALUE) {
            (void)CloseHandle(handle);
        }
        return INVALID_HANDLE_VALUE;
    }
    return handle;
}

static HANDLE cupidbuild_host_windows_open_relative_access_status(HANDLE parent, const char *name,
                                                                  int directory, int read_contents,
                                                                  unsigned long extra_access,
                                                                  long *status_out) {
    return cupidbuild_host_windows_open_relative_access_share_status(
        parent, name, directory, read_contents, extra_access,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, status_out);
}

static HANDLE cupidbuild_host_windows_open_relative_access(HANDLE parent, const char *name,
                                                           int directory, int read_contents,
                                                           unsigned long extra_access) {
    return cupidbuild_host_windows_open_relative_access_status(
        parent, name, directory, read_contents, extra_access, (long *)0);
}

static HANDLE cupidbuild_host_windows_open_relative(HANDLE parent, const char *name, int directory,
                                                    int read_contents) {
    return cupidbuild_host_windows_open_relative_access(parent, name, directory, read_contents, 0u);
}

static HANDLE cupidbuild_host_windows_open_relative_path_access(HANDLE root, const char *logical,
                                                                int directory, int read_contents,
                                                                unsigned long extra_access) {
    HANDLE current = root;
    HANDLE next;
    char component[260];
    size_t start = 0u;
    size_t size = strlen(logical);
    size_t index;
    int owns_current = 0;
    if (root == INVALID_HANDLE_VALUE || size == 0u) {
        return INVALID_HANDLE_VALUE;
    }
    for (index = 0u; index <= size; index++) {
        if (logical[index] == '/' || logical[index] == '\\' || logical[index] == '\0') {
            size_t component_size = index - start;
            int last = index == size;
            if (component_size == 0u || component_size >= sizeof(component)) {
                if (owns_current != 0) {
                    (void)CloseHandle(current);
                }
                return INVALID_HANDLE_VALUE;
            }
            (void)memcpy(component, logical + start, component_size);
            component[component_size] = '\0';
            next = cupidbuild_host_windows_open_relative_access(
                current, component, last != 0 ? directory : 1, last != 0 ? read_contents : 1,
                last != 0 ? extra_access : 0u);
            if (owns_current != 0) {
                (void)CloseHandle(current);
            }
            if (next == INVALID_HANDLE_VALUE) {
                return INVALID_HANDLE_VALUE;
            }
            current = next;
            owns_current = 1;
            start = index + 1u;
        }
    }
    return current;
}

static HANDLE cupidbuild_host_windows_open_relative_path(HANDLE root, const char *logical,
                                                         int directory, int read_contents) {
    return cupidbuild_host_windows_open_relative_path_access(root, logical, directory,
                                                             read_contents, 0u);
}

static HANDLE cupidbuild_host_windows_open_repository(const char *path) {
    char absolute[CUPIDBUILD_HOST_PATH_BYTES];
    char anchor[4];
    HANDLE current;
    HANDLE next;
    char component[260];
    size_t start = 3u;
    size_t size;
    size_t index;
    if (_fullpath(absolute, path, sizeof(absolute)) == (char *)0) {
        return INVALID_HANDLE_VALUE;
    }
    size = strlen(absolute);
    if (size < 3u || absolute[1] != ':' || (absolute[2] != '/' && absolute[2] != '\\') ||
        !((absolute[0] >= 'A' && absolute[0] <= 'Z') ||
          (absolute[0] >= 'a' && absolute[0] <= 'z'))) {
        return INVALID_HANDLE_VALUE;
    }
    anchor[0] = absolute[0];
    anchor[1] = ':';
    anchor[2] = '\\';
    anchor[3] = '\0';
    current = CreateFileA(
        anchor,
        (size == 3u ? FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY | FILE_DELETE_CHILD : 0u) |
            FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | FILE_TRAVERSE | SYNCHRONIZE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, (LPSECURITY_ATTRIBUTES)0,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, (HANDLE)0);
    if (current == INVALID_HANDLE_VALUE) {
        return INVALID_HANDLE_VALUE;
    }
    if (size == 3u) {
        return current;
    }
    for (index = start; index <= size; index++) {
        if (absolute[index] == '/' || absolute[index] == '\\' || absolute[index] == '\0') {
            size_t component_size = index - start;
            if (component_size == 0u || component_size >= sizeof(component)) {
                (void)CloseHandle(current);
                return INVALID_HANDLE_VALUE;
            }
            (void)memcpy(component, absolute + start, component_size);
            component[component_size] = '\0';
            next = cupidbuild_host_windows_open_relative_access(
                current, component, 1, 1,
                index == size ? FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY | FILE_DELETE_CHILD : 0u);
            (void)CloseHandle(current);
            if (next == INVALID_HANDLE_VALUE) {
                return INVALID_HANDLE_VALUE;
            }
            current = next;
            start = index + 1u;
        }
    }
    return current;
}

static int cupidbuild_host_windows_read_repository_regular(HANDLE repository, const char *logical,
                                                           unsigned long long limit,
                                                           cupidbuild_host_snapshot_t *snapshot,
                                                           unsigned char **bytes_out) {
    HANDLE handle = cupidbuild_host_windows_open_relative_path(repository, logical, 0, 1);
    BY_HANDLE_FILE_INFORMATION information;
    unsigned char *bytes = (unsigned char *)0;
    unsigned char digest[32];
    unsigned long long size;
    (void)memset(snapshot, 0, sizeof(*snapshot));
    if (handle == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle, &information) ||
        (information.dwFileAttributes &
         (FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0u ||
        (((unsigned long long)information.nFileSizeHigh << 32u) |
          information.nFileSizeLow) > limit) {
        if (handle != INVALID_HANDLE_VALUE) {
            (void)CloseHandle(handle);
        }
        return 0;
    }
    size = ((unsigned long long)information.nFileSizeHigh << 32u) |
           information.nFileSizeLow;
    if (!cupidbuild_host_read_open_contents(
            handle, size, bytes_out != (unsigned char **)0 ? &bytes : (unsigned char **)0,
            digest)) {
        (void)CloseHandle(handle);
        return 0;
    }
    snapshot->present = 1;
    snapshot->size = size;
    snapshot->identity[0] = information.dwVolumeSerialNumber;
    snapshot->identity[1] = information.nFileIndexHigh;
    snapshot->identity[2] = information.nFileIndexLow;
    snapshot->modified[0] = information.ftLastWriteTime.dwHighDateTime;
    snapshot->modified[1] = information.ftLastWriteTime.dwLowDateTime;
    (void)memcpy(snapshot->sha256, digest, sizeof(digest));
    if (!CloseHandle(handle)) {
        free(bytes);
        return 0;
    }
    if (bytes_out != (unsigned char **)0) {
        *bytes_out = bytes;
    } else {
        free(bytes);
    }
    return 1;
}

static int cupidbuild_host_windows_read_relative_regular(HANDLE parent, const char *name,
                                                         int optional, unsigned long long limit,
                                                         cupidbuild_host_snapshot_t *snapshot,
                                                         unsigned char **bytes_out) {
    long open_status = 0;
    HANDLE handle =
        cupidbuild_host_windows_open_relative_access_status(parent, name, 0, 1, 0u, &open_status);
    BY_HANDLE_FILE_INFORMATION information;
    unsigned char *bytes = (unsigned char *)0;
    unsigned char digest[32];
    unsigned long long size;
    (void)memset(snapshot, 0, sizeof(*snapshot));
    if (handle == INVALID_HANDLE_VALUE) {
        unsigned long status = (unsigned long)open_status;
        if (optional != 0 &&
            (status == 0xc000000fu || status == 0xc0000034u || status == 0xc000003au)) {
            if (bytes_out != (unsigned char **)0) {
                *bytes_out = (unsigned char *)0;
            }
            return 1;
        }
        return 0;
    }
    if (!GetFileInformationByHandle(handle, &information) ||
        (information.dwFileAttributes &
         (FILE_ATTRIBUTE_DEVICE | FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0u ||
        (((unsigned long long)information.nFileSizeHigh << 32u) |
          information.nFileSizeLow) > limit) {
        (void)CloseHandle(handle);
        return 0;
    }
    size = ((unsigned long long)information.nFileSizeHigh << 32u) |
           information.nFileSizeLow;
    if (!cupidbuild_host_read_open_contents(
            handle, size, bytes_out != (unsigned char **)0 ? &bytes : (unsigned char **)0,
            digest)) {
        (void)CloseHandle(handle);
        return 0;
    }
    snapshot->present = 1;
    snapshot->size = size;
    snapshot->identity[0] = information.dwVolumeSerialNumber;
    snapshot->identity[1] = information.nFileIndexHigh;
    snapshot->identity[2] = information.nFileIndexLow;
    snapshot->modified[0] = information.ftLastWriteTime.dwHighDateTime;
    snapshot->modified[1] = information.ftLastWriteTime.dwLowDateTime;
    (void)memcpy(snapshot->sha256, digest, sizeof(digest));
    if (!CloseHandle(handle)) {
        free(bytes);
        return 0;
    }
    if (bytes_out != (unsigned char **)0) {
        *bytes_out = bytes;
    } else {
        free(bytes);
    }
    return 1;
}

static int cupidbuild_host_windows_query_directory_request(
    HANDLE directory, int restart, const char *requested, char *name, size_t name_capacity,
    DWORD *attributes_out, DWORD *file_id_high_out, DWORD *file_id_low_out,
    DWORD *last_write_high_out, DWORD *last_write_low_out, DWORD *change_high_out,
    DWORD *change_low_out, int *complete_out) {
    unsigned char storage[1031];
    unsigned char *information = storage + ((8u - ((size_t)storage & 7u)) & 7u);
    unsigned short requested_buffer[260];
    unsigned short returned_buffer[472];
    size_t returned_bytes;
    cupidbuild_windows_unicode_string_t requested_name;
    cupidbuild_windows_unicode_string_t *requested_pointer =
        (cupidbuild_windows_unicode_string_t *)0;
    cupidbuild_windows_io_status_t status;
    size_t bytes;
    size_t name_bytes;
    size_t index;
    long result;
    *complete_out = 0;
    if (requested != (const char *)0) {
        size_t requested_size = 0u;
        if (!cupidbuild_host_windows_component(requested, requested_buffer, 260u,
                                               &requested_size)) {
            return 0;
        }
        requested_name.length = (unsigned short)(requested_size * 2u);
        requested_name.maximum_length = (unsigned short)((requested_size + 1u) * 2u);
        requested_name.buffer = requested_buffer;
        requested_pointer = &requested_name;
    }
    (void)memset(&status, 0, sizeof(status));
    result = cupid_windows_nt_query_directory_file(directory, (HANDLE)0, (void *)0, (void *)0,
                                                   &status, information, 1024u, 38u, 1u,
                                                   requested_pointer, restart != 0 ? 1u : 0u);
    if ((unsigned long)result == 0x80000006u || (unsigned long)result == 0xc000000fu) {
        *complete_out = 1;
        return 1;
    }
    bytes = (size_t)status.information;
    if (result < 0 || bytes < 80u || bytes > 1024u) {
        return 0;
    }
    name_bytes = (size_t)cupidbuild_host_windows_u32(information + 60u);
    if (cupidbuild_host_windows_u32(information) != 0u || name_bytes == 0u ||
        (name_bytes & 1u) != 0u || name_bytes > bytes - 80u) {
        return 0;
    }
    for (index = 0u; index < name_bytes / 2u; index++) {
        unsigned int character = (unsigned int)information[80u + index * 2u] |
                                 ((unsigned int)information[81u + index * 2u] << 8u);
        if (character == 0u || character == '/' || character == '\\') {
            return 0;
        }
        returned_buffer[index] = (unsigned short)character;
    }
    if (!cupidbuild_path_to_utf8(returned_buffer, name_bytes / 2u, name, name_capacity,
                                 &returned_bytes)) {
        return 0;
    }
    *attributes_out = (DWORD)cupidbuild_host_windows_u32(information + 56u);
    *file_id_low_out = (DWORD)cupidbuild_host_windows_u32(information + 72u);
    *file_id_high_out = (DWORD)cupidbuild_host_windows_u32(information + 76u);
    *last_write_low_out = (DWORD)cupidbuild_host_windows_u32(information + 24u);
    *last_write_high_out = (DWORD)cupidbuild_host_windows_u32(information + 28u);
    *change_low_out = (DWORD)cupidbuild_host_windows_u32(information + 32u);
    *change_high_out = (DWORD)cupidbuild_host_windows_u32(information + 36u);
    return 1;
}

typedef struct {
    char name[1417];
    DWORD attributes;
    DWORD file_id_high;
    DWORD file_id_low;
    DWORD last_write_high;
    DWORD last_write_low;
    DWORD change_high;
    DWORD change_low;
    int complete;
} cupidbuild_host_windows_directory_record_t;

static int cupidbuild_host_windows_query_named_directory_record(
    HANDLE parent, const char *name, cupidbuild_host_windows_directory_record_t *record) {
    (void)memset(record, 0, sizeof(*record));
    return cupidbuild_host_windows_query_directory_request(
        parent, 1, name, record->name, sizeof(record->name), &record->attributes,
        &record->file_id_high, &record->file_id_low, &record->last_write_high,
        &record->last_write_low, &record->change_high, &record->change_low, &record->complete);
}

static int cupidbuild_host_windows_named_directory_record_valid(
    const cupidbuild_host_windows_directory_record_t *record, const char *name) {
    return record->complete == 0 && strcmp(record->name, name) == 0 &&
           (record->attributes & FILE_ATTRIBUTE_DIRECTORY) != 0u &&
           (record->attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) == 0u;
}

static int cupidbuild_host_windows_directory_record_equal(
    const cupidbuild_host_windows_directory_record_t *left,
    const cupidbuild_host_windows_directory_record_t *right) {
    return strcmp(left->name, right->name) == 0 && left->attributes == right->attributes &&
           left->file_id_high == right->file_id_high && left->file_id_low == right->file_id_low &&
           left->last_write_high == right->last_write_high &&
           left->last_write_low == right->last_write_low &&
           left->change_high == right->change_high && left->change_low == right->change_low &&
           left->complete == right->complete;
}

static int cupidbuild_host_windows_directory_record_binding_equal(
    const cupidbuild_host_windows_directory_record_t *left,
    const cupidbuild_host_windows_directory_record_t *right) {
    return strcmp(left->name, right->name) == 0 && left->attributes == right->attributes &&
           left->file_id_high == right->file_id_high && left->file_id_low == right->file_id_low &&
           left->complete == right->complete;
}

static int cupidbuild_host_windows_query_directory(
    HANDLE directory, int restart, char *name, size_t name_capacity, DWORD *attributes_out,
    DWORD *file_id_high_out, DWORD *file_id_low_out, DWORD *last_write_high_out,
    DWORD *last_write_low_out, DWORD *change_high_out, DWORD *change_low_out, int *complete_out) {
    return cupidbuild_host_windows_query_directory_request(
        directory, restart, (const char *)0, name, name_capacity, attributes_out, file_id_high_out,
        file_id_low_out, last_write_high_out, last_write_low_out, change_high_out, change_low_out,
        complete_out);
}

#if defined(CUPIDBUILD_PROFILE_DIRECTORY_RACE_TEST) && !defined(CUPIDBUILD_CUSTOM_LINUX)
static int
cupidbuild_host_windows_named_directory_test_pause(cupidbuild_host_transaction_t *transaction,
                                                   const char *logical) {
    const char *requested = getenv("CUPIDBUILD_PROFILE_TEST_DIRECTORY_QUERY_LOGICAL");
    if (requested == (const char *)0 || strcmp(requested, logical) != 0) {
        return 1;
    }
    transaction->discovery_named_query_count++;
    if (transaction->discovery_named_query_count != 2u) {
        return 1;
    }
    return cupidbuild_host_profile_directory_test_pause(
        "CUPIDBUILD_PROFILE_TEST_DIRECTORY_QUERY_READY",
        "CUPIDBUILD_PROFILE_TEST_DIRECTORY_QUERY_RESUME");
}
#endif

static int cupidbuild_host_windows_named_directory_snapshot(
    cupidbuild_host_transaction_t *transaction, const char *logical, HANDLE retained,
    cupidbuild_host_snapshot_t *snapshot, int *metadata_unsettled_out) {
    char parent_logical[CUPIDBUILD_HOST_PATH_BYTES];
    const char *name = logical;
    size_t index;
    size_t separator = (size_t)-1;
    HANDLE parent = INVALID_HANDLE_VALUE;
    int valid = 0;
    cupidbuild_host_windows_directory_record_t before;
    cupidbuild_host_windows_directory_record_t after;
    cupidbuild_host_snapshot_t opened;
    if (metadata_unsettled_out != (int *)0) {
        *metadata_unsettled_out = 0;
    }
    if (transaction == (cupidbuild_host_transaction_t *)0 || logical == (const char *)0 ||
        retained == INVALID_HANDLE_VALUE || !cupidbuild_host_path_is_relative_safe(logical)) {
        return 0;
    }
    for (index = 0u; logical[index] != '\0'; index++) {
        if (logical[index] == '/' || logical[index] == '\\') {
            separator = index;
            name = logical + index + 1u;
        }
    }
    if (name[0] == '\0') {
        return 0;
    }
    if (separator != (size_t)-1) {
        if (separator == 0u || separator + 1u > sizeof(parent_logical)) {
            return 0;
        }
        (void)memcpy(parent_logical, logical, separator);
        parent_logical[separator] = '\0';
        parent = cupidbuild_host_windows_open_relative_path(transaction->repository_root_handle,
                                                            parent_logical, 1, 1);
        if (parent == INVALID_HANDLE_VALUE) {
            return 0;
        }
    } else {
        cupidbuild_host_snapshot_t parent_snapshot;
        parent = cupidbuild_host_windows_open_repository(transaction->repository_root);
        if (!cupidbuild_host_windows_directory_handle_snapshot(parent, &parent_snapshot) ||
            !cupidbuild_host_snapshot_identity_equal(&parent_snapshot,
                                                     &transaction->repository_root_snapshot)) {
            if (parent != INVALID_HANDLE_VALUE) {
                (void)CloseHandle(parent);
            }
            return 0;
        }
    }
    valid = cupidbuild_host_windows_query_named_directory_record(parent, name, &before) &&
            cupidbuild_host_windows_named_directory_record_valid(&before, name);
#if defined(CUPIDBUILD_PROFILE_DIRECTORY_RACE_TEST) && !defined(CUPIDBUILD_CUSTOM_LINUX)
    if (valid != 0 && !cupidbuild_host_windows_named_directory_test_pause(transaction, logical)) {
        valid = 0;
    }
#endif
    if (valid != 0) {
        valid = cupidbuild_host_windows_directory_handle_snapshot(retained, &opened) &&
                opened.identity[0] == transaction->repository_root_snapshot.identity[0] &&
                opened.identity[1] == before.file_id_high &&
                opened.identity[2] == before.file_id_low &&
                cupidbuild_host_windows_query_named_directory_record(parent, name, &after) &&
                cupidbuild_host_windows_named_directory_record_valid(&after, name) &&
                cupidbuild_host_windows_directory_record_binding_equal(&before, &after);
        if (valid != 0 && transaction->compile_discovery == 0 &&
            !cupidbuild_host_windows_directory_record_equal(&before, &after)) {
            valid = 0;
        } else if (valid != 0 && transaction->compile_discovery == 0 &&
                   (opened.modified[0] != before.last_write_high ||
                    opened.modified[1] != before.last_write_low)) {
            if (metadata_unsettled_out != (int *)0) {
                *metadata_unsettled_out = 1;
            }
            valid = 0;
        }
    }
    if (!CloseHandle(parent)) {
        valid = 0;
    }
    if (!valid) {
        return 0;
    }
    opened.changed[0] = after.change_high;
    opened.changed[1] = after.change_low;
    *snapshot = opened;
    return 1;
}

static int cupidbuild_host_seed_members_repository(cupidbuild_host_transaction_t *transaction,
                                                   const char *logical_directory,
                                                   const char *suffix, const char *const *expected,
                                                   size_t expected_count) {
    HANDLE directory;
    cupidbuild_host_snapshot_t root_snapshot;
    int restart = 1;
    int complete = 0;
    int valid = 1;
    if (!cupidbuild_host_windows_directory_handle_snapshot(transaction->repository_root_handle,
                                                           &root_snapshot) ||
        !cupidbuild_host_snapshot_identity_equal(&transaction->repository_root_snapshot,
                                                 &root_snapshot)) {
        return 0;
    }
    directory = cupidbuild_host_windows_open_relative_path(transaction->repository_root_handle,
                                                           logical_directory, 1, 1);
    if (directory == INVALID_HANDLE_VALUE) {
        return 0;
    }
    while (valid != 0) {
        char name[1417];
        DWORD attributes = 0u;
        DWORD file_id_high = 0u;
        DWORD file_id_low = 0u;
        DWORD last_write_high = 0u;
        DWORD last_write_low = 0u;
        DWORD change_high = 0u;
        DWORD change_low = 0u;
        if (!cupidbuild_host_windows_query_directory(
                directory, restart, name, sizeof(name), &attributes, &file_id_high, &file_id_low,
                &last_write_high, &last_write_low, &change_high, &change_low, &complete)) {
            valid = 0;
            break;
        }
        (void)attributes;
        (void)file_id_high;
        (void)file_id_low;
        (void)last_write_high;
        (void)last_write_low;
        (void)change_high;
        (void)change_low;
        restart = 0;
        if (complete != 0) {
            break;
        }
        if (cupidbuild_host_name_has_suffix(name, suffix) &&
            !cupidbuild_host_name_is_expected(name, expected, expected_count)) {
            valid = 0;
        }
    }
    if (!CloseHandle(directory)) {
        valid = 0;
    }
    return valid;
}

static int cupidbuild_host_windows_rename_handle(HANDLE source, HANDLE destination_parent,
                                                 const char *destination_name,
                                                 int replace_if_exists) {
    cupidbuild_windows_rename_t *rename_information;
    cupidbuild_windows_io_status_t status;
    size_t name_size;
    size_t allocation_size;
    long result;
    if (source == INVALID_HANDLE_VALUE || destination_parent == INVALID_HANDLE_VALUE ||
        destination_name == (const char *)0) {
        return 0;
    }
    if (!cupidbuild_host_windows_component(destination_name, (unsigned short *)0, 0u, &name_size) ||
        name_size > 4096u) {
        return 0;
    }
    allocation_size = sizeof(*rename_information) - sizeof(unsigned short) +
                      (name_size + 1u) * sizeof(unsigned short);
    rename_information = (cupidbuild_windows_rename_t *)calloc(1u, allocation_size);
    if (rename_information == (cupidbuild_windows_rename_t *)0) {
        return 0;
    }
    rename_information->replace_if_exists = replace_if_exists != 0 ? 1u : 0u;
    rename_information->root_directory = destination_parent;
    rename_information->file_name_length = (DWORD)(name_size * 2u);
    if (!cupidbuild_host_windows_component(destination_name, rename_information->file_name,
                                           name_size + 1u, &name_size)) {
        free(rename_information);
        return 0;
    }
    (void)memset(&status, 0, sizeof(status));
    result = cupid_windows_nt_set_information_file(source, &status, rename_information,
                                                   (unsigned long)allocation_size, 10u);
    free(rename_information);
    return result >= 0;
}

static int
cupidbuild_host_windows_restore_initial_output(cupidbuild_host_transaction_t *transaction) {
    cupidbuild_host_snapshot_t restored;
    cupidbuild_host_snapshot_t retained;
    if (transaction->initial_output_parked == 0) {
        return transaction->initial_output_handle == INVALID_HANDLE_VALUE ||
               transaction->initial_output_snapshot.present != 0;
    }
    if (transaction->initial_output_handle == INVALID_HANDLE_VALUE ||
        transaction->initial_output_snapshot.present == 0 ||
        !cupidbuild_host_windows_rename_handle(transaction->initial_output_handle,
                                               transaction->output_parent_handle,
                                               transaction->output_name, 0)) {
        return 0;
    }
    transaction->initial_output_parked = 0;
    transaction->initial_output_backup_name[0] = '\0';
    return cupidbuild_host_windows_read_open_regular(transaction->initial_output_handle,
                                                     transaction->candidate_limit, &retained,
                                                     (unsigned char **)0) &&
           cupidbuild_host_read_output(transaction, 0, &restored, (unsigned char **)0) &&
           cupidbuild_host_snapshot_equal(&retained, &transaction->initial_output_snapshot) &&
           cupidbuild_host_snapshot_equal(&restored, &transaction->initial_output_snapshot);
}

static int cupidbuild_host_windows_park_initial_output(cupidbuild_host_transaction_t *transaction) {
    cupidbuild_host_snapshot_t parked;
    cupidbuild_host_snapshot_t retained;
    cupidbuild_host_snapshot_t missing;
    unsigned int attempt;
    if (transaction->initial_output_snapshot.present == 0) {
        return transaction->initial_output_handle == INVALID_HANDLE_VALUE;
    }
    if (transaction->initial_output_handle == INVALID_HANDLE_VALUE ||
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
        if (cupidbuild_host_windows_rename_handle(transaction->initial_output_handle,
                                                  transaction->output_parent_handle,
                                                  transaction->initial_output_backup_name, 0)) {
            transaction->initial_output_parked = 1;
            break;
        }
    }
    if (transaction->initial_output_parked == 0) {
        transaction->initial_output_backup_name[0] = '\0';
        return 0;
    }
    return cupidbuild_host_windows_read_open_regular(transaction->initial_output_handle,
                                                     transaction->candidate_limit, &retained,
                                                     (unsigned char **)0) &&
           cupidbuild_host_windows_read_relative_regular(
               transaction->output_parent_handle, transaction->initial_output_backup_name, 0,
               transaction->candidate_limit, &parked, (unsigned char **)0) &&
           cupidbuild_host_read_output(transaction, 1, &missing, (unsigned char **)0) &&
           missing.present == 0 &&
           cupidbuild_host_snapshot_equal(&retained, &transaction->initial_output_snapshot) &&
           cupidbuild_host_snapshot_equal(&parked, &transaction->initial_output_snapshot);
}

static int
cupidbuild_host_windows_restore_private_candidate(cupidbuild_host_transaction_t *transaction) {
    cupidbuild_host_snapshot_t snapshot;
    HANDLE bridge = cupidbuild_host_windows_open_relative(transaction->repository_root_handle,
                                                          transaction->private_name, 1, 0);
    HANDLE cleanup;
    int restored;
    if (bridge == INVALID_HANDLE_VALUE ||
        !cupidbuild_host_windows_directory_handle_snapshot(bridge, &snapshot) ||
        !cupidbuild_host_snapshot_identity_equal(&snapshot, &transaction->private_root_snapshot)) {
        if (bridge != INVALID_HANDLE_VALUE) {
            (void)CloseHandle(bridge);
        }
        return 0;
    }
    /* A retained DELETE directory handle conflicts with NT rename's target
       directory open. Keep its identity live while restoring the candidate. */
    if (!CloseHandle(transaction->private_handle)) {
        (void)CloseHandle(bridge);
        return 0;
    }
    transaction->private_handle = bridge;
#if defined(CUPIDBUILD_PUBLICATION_RACE_TEST)
    if (!cupidbuild_host_publication_test_pause("rollback-directory-bridge")) {
        return 0;
    }
#endif
    restored = cupidbuild_host_windows_rename_handle(transaction->candidate_handle, bridge,
                                                     "candidate.o", 0);
    cleanup = cupidbuild_host_windows_open_relative_access_share_status(
        transaction->repository_root_handle, transaction->private_name, 1, 0, DELETE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, (long *)0);
    if (cleanup == INVALID_HANDLE_VALUE ||
        !cupidbuild_host_windows_directory_handle_snapshot(cleanup, &snapshot) ||
        !cupidbuild_host_snapshot_identity_equal(&snapshot, &transaction->private_root_snapshot) ||
        !cupidbuild_host_private_root_is_owned(transaction)) {
        if (cleanup != INVALID_HANDLE_VALUE) {
            (void)CloseHandle(cleanup);
        }
        return 0;
    }
    transaction->private_handle = cleanup;
    return CloseHandle(bridge) != 0 && restored;
}

static int cupidbuild_host_atomic_replace(cupidbuild_host_transaction_t *transaction) {
    cupidbuild_host_snapshot_t published;
    cupidbuild_host_snapshot_t current_output;
    int candidate_at_output = 0;
    if (!cupidbuild_host_candidate_ready_for_publication(transaction) ||
        !cupidbuild_host_require_public_binding(transaction, &transaction->initial_output_snapshot,
                                                1)
#if defined(CUPIDBUILD_PUBLICATION_RACE_TEST)
        || !cupidbuild_host_publication_test_pause("before-mutation")
#endif
        || (transaction->initial_output_snapshot.present != 0 &&
            !cupidbuild_host_windows_park_initial_output(transaction))) {
        if (transaction->initial_output_parked != 0) {
            if (!cupidbuild_host_windows_restore_initial_output(transaction)) {
                transaction->namespace_interfered = 1;
            }
        }
        return 0;
    }
    if (!cupidbuild_host_read_output(transaction, 1, &current_output, (unsigned char **)0) ||
        current_output.present != 0 ||
        !cupidbuild_host_windows_rename_handle(transaction->candidate_handle,
                                               transaction->output_parent_handle,
                                               transaction->output_name, 0)) {
        if (transaction->initial_output_parked != 0) {
            if (!cupidbuild_host_windows_restore_initial_output(transaction)) {
                transaction->namespace_interfered = 1;
            }
        }
        return 0;
    }
    candidate_at_output = 1;
    {
        int candidate_verified =
#if defined(CUPIDBUILD_PUBLICATION_RACE_TEST)
            cupidbuild_host_publication_test_pause("after-install") &&
#endif
            cupidbuild_host_read_output(transaction, 0, &published, (unsigned char **)0) &&
            cupidbuild_host_snapshot_equal(&published, &transaction->candidate_snapshot) &&
            cupidbuild_host_published_candidate_matches(transaction);
        int public_binding =
            candidate_verified && cupidbuild_host_require_public_binding(
                                      transaction, &transaction->candidate_snapshot, 1);
        if (!public_binding) {
            cupidbuild_host_snapshot_t restored_candidate;
            int candidate_restored = 0;
            int output_restored = 0;
            if (cupidbuild_host_windows_restore_private_candidate(transaction)) {
                candidate_at_output = 0;
                candidate_restored =
                    cupidbuild_host_windows_read_relative_regular(
                        transaction->private_handle, "candidate.o", 0, transaction->candidate_limit,
                        &restored_candidate, (unsigned char **)0) &&
                    cupidbuild_host_snapshot_equal(&restored_candidate,
                                                   &transaction->candidate_snapshot);
            }
            if (transaction->initial_output_parked != 0) {
                output_restored = cupidbuild_host_windows_restore_initial_output(transaction);
            } else {
                output_restored = 1;
            }
            if (!candidate_restored || !output_restored ||
                !cupidbuild_host_require_public_binding(transaction,
                                                        &transaction->initial_output_snapshot, 0)) {
                transaction->namespace_interfered = 1;
            }
            if (candidate_at_output != 0) {
                transaction->candidate_published = 1;
            }
            return 0;
        }
    }
    transaction->candidate_publish_snapshot.present = 0;
    transaction->candidate_published = 1;
    transaction->publication_committed = 1;
    if (transaction->initial_output_parked != 0) {
        if (!cupidbuild_host_windows_dispose_retained_at(
                transaction->output_parent_handle, transaction->initial_output_backup_name,
                &transaction->initial_output_snapshot, &transaction->initial_output_handle)) {
            return 1;
        }
        transaction->initial_output_parked = 0;
        transaction->initial_output_backup_name[0] = '\0';
    }
    return 1;
}
