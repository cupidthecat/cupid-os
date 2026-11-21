/**
 * @file cupidbuild_host_profile_parent.inc
 * @brief Retain and prepare the destination directory for profile builds.
 */

static void cupidbuild_host_profile_parent_set_error(cupidbuild_host_profile_parent_t *preparation,
                                                     const char *message) {
    if (preparation != (cupidbuild_host_profile_parent_t *)0) {
        (void)cupidbuild_host_copy_text(preparation->error, sizeof(preparation->error), message);
    }
}

static int
cupidbuild_host_profile_parent_component_current(const char *path,
                                                 const cupidbuild_host_snapshot_t *expected,
#if defined(_WIN32)
                                                 HANDLE handle
#else
                                                 int descriptor
#endif
);

#if defined(_WIN32)
static HANDLE cupidbuild_host_profile_parent_open_root(const char *path) {
    return cupidbuild_host_windows_open_repository(path);
}

static HANDLE cupidbuild_host_profile_parent_open_component(HANDLE parent, const char *name,
                                                            int create, int delete_access) {
    unsigned short name_buffer[128];
    cupidbuild_windows_unicode_string_t unicode_name;
    cupidbuild_windows_object_attributes_t attributes;
    cupidbuild_windows_io_status_t status;
    HANDLE handle = INVALID_HANDLE_VALUE;
    size_t name_size = 0u;
    long result;
    if (parent == INVALID_HANDLE_VALUE ||
        !cupidbuild_host_windows_component(
            name, name_buffer, sizeof(name_buffer) / sizeof(name_buffer[0]), &name_size)) {
        return INVALID_HANDLE_VALUE;
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
    result = cupid_windows_nt_create_file(
        &handle,
        (delete_access != 0 ? DELETE : 0u) | FILE_READ_ATTRIBUTES | FILE_TRAVERSE | FILE_ADD_FILE |
            FILE_ADD_SUBDIRECTORY | FILE_LIST_DIRECTORY | SYNCHRONIZE,
        &attributes, &status, (void *)0, 0u,
        FILE_SHARE_READ | FILE_SHARE_WRITE | (delete_access != 0 ? 0u : FILE_SHARE_DELETE),
        create != 0 ? CUPIDBUILD_WINDOWS_FILE_CREATE : CUPIDBUILD_WINDOWS_FILE_OPEN,
        CUPIDBUILD_WINDOWS_FILE_DIRECTORY_FILE | CUPIDBUILD_WINDOWS_FILE_SYNCHRONOUS_IO_NONALERT |
            CUPIDBUILD_WINDOWS_FILE_OPEN_REPARSE_POINT,
        (void *)0, 0u);
    return result >= 0 ? handle : INVALID_HANDLE_VALUE;
}

#if defined(CUPIDBUILD_PROFILE_PARENT_RACE_TEST)
static int cupidbuild_host_profile_parent_test_replace_existing(HANDLE parent, const char *name) {
    static int replaced = 0;
    const char *requested = getenv("CUPIDBUILD_PROFILE_PARENT_TEST_REPLACE_EXISTING");
    const char *displaced =
        strcmp(name, "build") == 0 ? "displaced-existing-build" : "displaced-existing-bootstrap";
    HANDLE original;
    HANDLE replacement;
    int renamed;
    int original_closed;
    int replacement_closed;
    if (replaced != 0 || requested == (const char *)0 || strcmp(requested, name) != 0) {
        return 1;
    }
    replaced = 1;
    original = cupidbuild_host_profile_parent_open_component(parent, name, 0, 1);
    if (original == INVALID_HANDLE_VALUE) {
        return 0;
    }
    renamed = cupidbuild_host_windows_rename_handle(original, parent, displaced, 0);
    original_closed = CloseHandle(original) != 0;
    if (!renamed || !original_closed) {
        return 0;
    }
    replacement = cupidbuild_host_profile_parent_open_component(parent, name, 1, 0);
    if (replacement == INVALID_HANDLE_VALUE) {
        return 0;
    }
    replacement_closed = CloseHandle(replacement) != 0;
    return replacement_closed;
}
#endif

static int
cupidbuild_host_profile_parent_remove_component(HANDLE parent, const char *name, const char *path,
                                                const cupidbuild_host_snapshot_t *expected,
                                                HANDLE *pinned_handle) {
    cupidbuild_windows_io_status_t status;
    cupidbuild_host_snapshot_t delete_snapshot;
    HANDLE delete_handle;
    unsigned char disposition = 1u;
    long result;
    int delete_closed;
    int pinned_closed;
    if (pinned_handle == (HANDLE *)0 || *pinned_handle == INVALID_HANDLE_VALUE ||
        !cupidbuild_host_profile_parent_component_current(path, expected, *pinned_handle)) {
        return 0;
    }
    delete_handle = cupidbuild_host_profile_parent_open_component(parent, name, 0, 1);
    if (delete_handle == INVALID_HANDLE_VALUE ||
        !cupidbuild_host_windows_directory_handle_snapshot(delete_handle, &delete_snapshot) ||
        !cupidbuild_host_snapshot_identity_equal(&delete_snapshot, expected)) {
        if (delete_handle != INVALID_HANDLE_VALUE) {
            (void)CloseHandle(delete_handle);
        }
        return 0;
    }
    (void)memset(&status, 0, sizeof(status));
    result = cupid_windows_nt_set_information_file(delete_handle, &status, &disposition,
                                                   (unsigned long)sizeof(disposition), 13u);
    delete_closed = CloseHandle(delete_handle) != 0;
    pinned_closed = CloseHandle(*pinned_handle) != 0;
    *pinned_handle = INVALID_HANDLE_VALUE;
    return result >= 0 && delete_closed != 0 && pinned_closed != 0 &&
           cupidbuild_host_path_missing(path);
}
#else
static int cupidbuild_host_profile_parent_open_component(int parent, const char *name) {
    int descriptor;
#if defined(CUPIDBUILD_CUSTOM_LINUX)
    descriptor = cupid_linux_syscall4(
        CUPIDBUILD_LINUX_SYS_OPENAT, (unsigned int)parent, (unsigned int)name,
        CUPIDBUILD_LINUX_O_DIRECTORY | CUPIDBUILD_LINUX_O_NOFOLLOW | CUPIDBUILD_LINUX_O_CLOEXEC,
        0u);
#else
    descriptor = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
#endif
    return descriptor;
}

#if defined(CUPIDBUILD_PROFILE_PARENT_RACE_TEST)
static int cupidbuild_host_profile_parent_test_replace_root_before_open(int parent,
                                                                        const char *name) {
    static int replaced = 0;
    const char *requested = getenv("CUPIDBUILD_PROFILE_PARENT_TEST_REPLACE_ROOT_BEFORE_OPEN");
    if (replaced != 0 || requested == (const char *)0 || strcmp(requested, name) != 0 ||
        strcmp(name, "displaced-root-component") == 0) {
        return 1;
    }
    replaced = 1;
    return renameat(parent, name, parent, "displaced-root-component") == 0 &&
           symlinkat("displaced-root-component", parent, name) == 0;
}

static int cupidbuild_host_profile_parent_test_replace_existing(int parent, const char *name) {
    static int replaced = 0;
    const char *requested = getenv("CUPIDBUILD_PROFILE_PARENT_TEST_REPLACE_EXISTING");
    const char *displaced =
        strcmp(name, "build") == 0 ? "displaced-existing-build" : "displaced-existing-bootstrap";
    if (replaced != 0 || requested == (const char *)0 || strcmp(requested, name) != 0) {
        return 1;
    }
    replaced = 1;
    return renameat(parent, name, parent, displaced) == 0 && mkdirat(parent, name, 0700) == 0;
}
#endif

static int cupidbuild_host_profile_parent_open_root(const char *path) {
    char component[CUPIDBUILD_HOST_PATH_BYTES];
    size_t component_size;
    size_t index;
    int descriptor;
    int child;
    if (path == (const char *)0 || path[0] != '/') {
        return -1;
    }
    descriptor = cupidbuild_host_open_directory("/");
    if (descriptor < 0) {
        return -1;
    }
    index = 1u;
    for (;;) {
        while (path[index] == '/') {
            index++;
        }
        if (path[index] == '\0') {
            return descriptor;
        }
        component_size = 0u;
        while (path[index] != '\0' && path[index] != '/') {
            if (component_size >= CUPIDBUILD_HOST_PATH_BYTES - 1u) {
                cupidbuild_host_close_directory(descriptor);
                return -1;
            }
            component[component_size++] = path[index++];
        }
        component[component_size] = '\0';
#if defined(CUPIDBUILD_PROFILE_PARENT_RACE_TEST)
        if (!cupidbuild_host_profile_parent_test_replace_root_before_open(descriptor, component)) {
            cupidbuild_host_close_directory(descriptor);
            return -1;
        }
#endif
        child = cupidbuild_host_profile_parent_open_component(descriptor, component);
        cupidbuild_host_close_directory(descriptor);
        if (child < 0) {
            return -1;
        }
        descriptor = child;
    }
}

#endif

static int
cupidbuild_host_profile_parent_component_current(const char *path,
                                                 const cupidbuild_host_snapshot_t *expected,
#if defined(_WIN32)
                                                 HANDLE handle
#else
                                                 int descriptor
#endif
) {
    cupidbuild_host_snapshot_t path_snapshot;
    cupidbuild_host_snapshot_t open_snapshot;
    if (!cupidbuild_host_directory_snapshot(path, &path_snapshot) ||
#if defined(_WIN32)
        !cupidbuild_host_windows_directory_handle_snapshot(handle, &open_snapshot) ||
#else
        !cupidbuild_host_directory_descriptor_snapshot(descriptor, &open_snapshot) ||
#endif
        !cupidbuild_host_snapshot_identity_equal(&path_snapshot, expected) ||
        !cupidbuild_host_snapshot_identity_equal(&open_snapshot, expected)) {
        return 0;
    }
    return 1;
}

int cupidbuild_host_profile_parent_prepare(const char *repository_root,
                                           cupidbuild_host_profile_parent_t **preparation_out) {
    cupidbuild_host_profile_parent_t *preparation;
    cupidbuild_host_snapshot_t open_snapshot;
    cupidbuild_host_snapshot_t initial_build_snapshot;
    cupidbuild_host_snapshot_t initial_bootstrap_snapshot;
    int build_missing;
    int bootstrap_missing;
    if (preparation_out == (cupidbuild_host_profile_parent_t **)0) {
        return 0;
    }
    *preparation_out = (cupidbuild_host_profile_parent_t *)0;
    preparation = (cupidbuild_host_profile_parent_t *)calloc(1u, sizeof(*preparation));
    if (preparation == (cupidbuild_host_profile_parent_t *)0) {
        return 0;
    }
    (void)memset(&initial_build_snapshot, 0, sizeof(initial_build_snapshot));
    (void)memset(&initial_bootstrap_snapshot, 0, sizeof(initial_bootstrap_snapshot));
#if defined(_WIN32)
    preparation->repository_root_handle = INVALID_HANDLE_VALUE;
    preparation->build_handle = INVALID_HANDLE_VALUE;
    preparation->bootstrap_handle = INVALID_HANDLE_VALUE;
#else
    preparation->repository_root_descriptor = -1;
    preparation->build_descriptor = -1;
    preparation->bootstrap_descriptor = -1;
#endif
    *preparation_out = preparation;
    if (!cupidbuild_host_absolute_directory(
            preparation->repository_root, sizeof(preparation->repository_root), repository_root) ||
        !cupidbuild_host_join(preparation->build_path, sizeof(preparation->build_path),
                              preparation->repository_root, "build") ||
        !cupidbuild_host_join(preparation->bootstrap_path, sizeof(preparation->bootstrap_path),
                              preparation->build_path, "bootstrap") ||
        !cupidbuild_host_directory_snapshot(preparation->repository_root,
                                            &preparation->repository_root_snapshot)) {
        cupidbuild_host_profile_parent_set_error(preparation,
                                                 "profile parent repository root is invalid");
        return 0;
    }
#if defined(_WIN32)
    preparation->repository_root_handle =
        cupidbuild_host_profile_parent_open_root(preparation->repository_root);
    if (!cupidbuild_host_windows_directory_handle_snapshot(preparation->repository_root_handle,
                                                           &open_snapshot)) {
#else
    preparation->repository_root_descriptor =
        cupidbuild_host_profile_parent_open_root(preparation->repository_root);
    if (!cupidbuild_host_directory_descriptor_snapshot(preparation->repository_root_descriptor,
                                                       &open_snapshot)) {
#endif
        cupidbuild_host_profile_parent_set_error(preparation,
                                                 "profile parent repository root cannot be pinned");
        return 0;
    }

    if (!cupidbuild_host_snapshot_identity_equal(&open_snapshot,
                                                 &preparation->repository_root_snapshot)) {
        cupidbuild_host_profile_parent_set_error(
            preparation, "profile parent repository root changed while opening");
        return 0;
    }
    build_missing = cupidbuild_host_path_missing(preparation->build_path);
    if (!build_missing &&
        !cupidbuild_host_directory_snapshot(preparation->build_path, &initial_build_snapshot)) {
        cupidbuild_host_profile_parent_set_error(
            preparation, "profile parent build component is a link or collision");
        return 0;
    }
#if defined(_WIN32)
#if defined(CUPIDBUILD_PROFILE_PARENT_RACE_TEST)
    if (!build_missing && !cupidbuild_host_profile_parent_test_replace_existing(
                              preparation->repository_root_handle, "build")) {
        cupidbuild_host_profile_parent_set_error(preparation,
                                                 "profile parent build replacement test failed");
        return 0;
    }
#endif
    preparation->build_handle = cupidbuild_host_profile_parent_open_component(
        preparation->repository_root_handle, "build", build_missing, 0);
    if (preparation->build_handle != INVALID_HANDLE_VALUE && build_missing) {
        preparation->build_created = 1;
    }
    if (preparation->build_handle == INVALID_HANDLE_VALUE ||
        !cupidbuild_host_windows_directory_handle_snapshot(preparation->build_handle,
                                                           &open_snapshot)) {
#else
    if (build_missing) {
        cupidbuild_host_profile_parent_set_error(
            preparation, "profile parent build component must already exist on POSIX");
        return 0;
    }
#if defined(CUPIDBUILD_PROFILE_PARENT_RACE_TEST)
    if (!cupidbuild_host_profile_parent_test_replace_existing(
            preparation->repository_root_descriptor, "build")) {
        cupidbuild_host_profile_parent_set_error(preparation,
                                                 "profile parent build replacement test failed");
        return 0;
    }
#endif
    preparation->build_descriptor = cupidbuild_host_profile_parent_open_component(
        preparation->repository_root_descriptor, "build");
    if (preparation->build_descriptor < 0 || !cupidbuild_host_directory_descriptor_snapshot(
                                                 preparation->build_descriptor, &open_snapshot)) {
#endif
        cupidbuild_host_profile_parent_set_error(
            preparation, "profile parent build component cannot be prepared");
        return 0;
    }
#if defined(_WIN32)
    if (build_missing) {
        initial_build_snapshot = open_snapshot;
    }
#endif
    if (!cupidbuild_host_directory_snapshot(preparation->build_path,
                                            &preparation->build_snapshot) ||
        !cupidbuild_host_snapshot_identity_equal(&open_snapshot, &preparation->build_snapshot) ||
        !cupidbuild_host_snapshot_identity_equal(&initial_build_snapshot, &open_snapshot) ||
        cupidbuild_host_snapshot_identity_equal(&preparation->build_snapshot,
                                                &preparation->repository_root_snapshot)) {
        cupidbuild_host_profile_parent_set_error(
            preparation, "profile parent build component changed or aliases root");
        return 0;
    }
    bootstrap_missing = cupidbuild_host_path_missing(preparation->bootstrap_path);
    if (!bootstrap_missing && !cupidbuild_host_directory_snapshot(preparation->bootstrap_path,
                                                                  &initial_bootstrap_snapshot)) {
        cupidbuild_host_profile_parent_set_error(
            preparation, "profile parent bootstrap component is a link or collision");
        return 0;
    }
#if defined(_WIN32)
#if defined(CUPIDBUILD_PROFILE_PARENT_RACE_TEST)
    if (!bootstrap_missing && !cupidbuild_host_profile_parent_test_replace_existing(
                                  preparation->build_handle, "bootstrap")) {
        cupidbuild_host_profile_parent_set_error(
            preparation, "profile parent bootstrap replacement test failed");
        return 0;
    }
#endif
    preparation->bootstrap_handle = cupidbuild_host_profile_parent_open_component(
        preparation->build_handle, "bootstrap", bootstrap_missing, 0);
    if (preparation->bootstrap_handle != INVALID_HANDLE_VALUE && bootstrap_missing) {
        preparation->bootstrap_created = 1;
    }
    if (preparation->bootstrap_handle == INVALID_HANDLE_VALUE ||
        !cupidbuild_host_windows_directory_handle_snapshot(preparation->bootstrap_handle,
                                                           &open_snapshot)) {
#else
    if (bootstrap_missing) {
        cupidbuild_host_profile_parent_set_error(
            preparation, "profile parent bootstrap component must already exist on POSIX");
        return 0;
    }
#if defined(CUPIDBUILD_PROFILE_PARENT_RACE_TEST)
    if (!cupidbuild_host_profile_parent_test_replace_existing(preparation->build_descriptor,
                                                              "bootstrap")) {
        cupidbuild_host_profile_parent_set_error(
            preparation, "profile parent bootstrap replacement test failed");
        return 0;
    }
#endif
    preparation->bootstrap_descriptor =
        cupidbuild_host_profile_parent_open_component(preparation->build_descriptor, "bootstrap");
    if (preparation->bootstrap_descriptor < 0 ||
        !cupidbuild_host_directory_descriptor_snapshot(preparation->bootstrap_descriptor,
                                                       &open_snapshot)) {
#endif
        cupidbuild_host_profile_parent_set_error(
            preparation, "profile parent bootstrap component cannot be prepared");
        return 0;
    }
#if defined(_WIN32)
    if (bootstrap_missing) {
        initial_bootstrap_snapshot = open_snapshot;
    }
#endif
    if (!cupidbuild_host_directory_snapshot(preparation->bootstrap_path,
                                            &preparation->bootstrap_snapshot) ||
        !cupidbuild_host_snapshot_identity_equal(&open_snapshot,
                                                 &preparation->bootstrap_snapshot) ||
        !cupidbuild_host_snapshot_identity_equal(&initial_bootstrap_snapshot, &open_snapshot) ||
        !cupidbuild_host_directory_snapshot(preparation->repository_root, &open_snapshot) ||
        !cupidbuild_host_snapshot_identity_equal(&open_snapshot,
                                                 &preparation->repository_root_snapshot) ||
        cupidbuild_host_snapshot_identity_equal(&preparation->bootstrap_snapshot,
                                                &preparation->repository_root_snapshot) ||
        cupidbuild_host_snapshot_identity_equal(&preparation->bootstrap_snapshot,
                                                &preparation->build_snapshot) ||
        !cupidbuild_host_profile_parent_component_current(preparation->bootstrap_path,
                                                          &preparation->bootstrap_snapshot,
#if defined(_WIN32)
                                                          preparation->bootstrap_handle
#else
                                                          preparation->bootstrap_descriptor
#endif
                                                          ) ||
        !cupidbuild_host_profile_parent_component_current(preparation->build_path,
                                                          &preparation->build_snapshot,
#if defined(_WIN32)
                                                          preparation->build_handle
#else
                                                          preparation->build_descriptor
#endif
                                                          )) {
        cupidbuild_host_profile_parent_set_error(
            preparation, "profile parent bootstrap component changed or aliases its parent");
        return 0;
    }
    return 1;
}

void cupidbuild_host_profile_parent_commit(cupidbuild_host_profile_parent_t *preparation) {
    if (preparation != (cupidbuild_host_profile_parent_t *)0) {
        preparation->committed = 1;
    }
}

int cupidbuild_host_profile_parent_close(cupidbuild_host_profile_parent_t *preparation) {
    int cleanup_succeeded = 1;
    if (preparation == (cupidbuild_host_profile_parent_t *)0) {
        return 1;
    }
#if defined(_WIN32)
    if (preparation->bootstrap_created != 0 && preparation->committed == 0) {
        int removed = cupidbuild_host_profile_parent_remove_component(
            preparation->build_handle, "bootstrap", preparation->bootstrap_path,
            &preparation->bootstrap_snapshot, &preparation->bootstrap_handle);
        if (preparation->bootstrap_handle != INVALID_HANDLE_VALUE) {
            (void)CloseHandle(preparation->bootstrap_handle);
            preparation->bootstrap_handle = INVALID_HANDLE_VALUE;
        }
        if (removed == 0 || !cupidbuild_host_path_missing(preparation->bootstrap_path)) {
            cleanup_succeeded = 0;
        }
    } else if (preparation->bootstrap_handle != INVALID_HANDLE_VALUE) {
        if (!CloseHandle(preparation->bootstrap_handle)) {
            cleanup_succeeded = 0;
        }
        preparation->bootstrap_handle = INVALID_HANDLE_VALUE;
    }
    if (preparation->build_created != 0 && preparation->committed == 0) {
        int removed = cupidbuild_host_profile_parent_remove_component(
            preparation->repository_root_handle, "build", preparation->build_path,
            &preparation->build_snapshot, &preparation->build_handle);
        if (preparation->build_handle != INVALID_HANDLE_VALUE) {
            (void)CloseHandle(preparation->build_handle);
            preparation->build_handle = INVALID_HANDLE_VALUE;
        }
        if (removed == 0 || !cupidbuild_host_path_missing(preparation->build_path)) {
            cleanup_succeeded = 0;
        }
    } else if (preparation->build_handle != INVALID_HANDLE_VALUE) {
        if (!CloseHandle(preparation->build_handle)) {
            cleanup_succeeded = 0;
        }
        preparation->build_handle = INVALID_HANDLE_VALUE;
    }
    if (preparation->repository_root_handle != INVALID_HANDLE_VALUE) {
        if (!CloseHandle(preparation->repository_root_handle)) {
            cleanup_succeeded = 0;
        }
        preparation->repository_root_handle = INVALID_HANDLE_VALUE;
    }
#else
    if (!cupidbuild_host_close_directory(preparation->bootstrap_descriptor)) {
        cleanup_succeeded = 0;
    }
    preparation->bootstrap_descriptor = -1;
    if (!cupidbuild_host_close_directory(preparation->build_descriptor)) {
        cleanup_succeeded = 0;
    }
    preparation->build_descriptor = -1;
    if (!cupidbuild_host_close_directory(preparation->repository_root_descriptor)) {
        cleanup_succeeded = 0;
    }
    preparation->repository_root_descriptor = -1;
#endif
    free(preparation);
    return cleanup_succeeded;
}

int cupidbuild_host_profile_parent_bind(cupidbuild_host_profile_parent_t *preparation,
                                        cupidbuild_host_transaction_t *transaction) {
    cupidbuild_host_snapshot_t transaction_root;
    cupidbuild_host_snapshot_t transaction_parent;
    if (preparation == (cupidbuild_host_profile_parent_t *)0 ||
        transaction == (cupidbuild_host_transaction_t *)0 || transaction->runner_transaction != 0 ||
        !cupidbuild_host_profile_parent_component_current(preparation->repository_root,
                                                          &preparation->repository_root_snapshot,
#if defined(_WIN32)
                                                          preparation->repository_root_handle) ||
        !cupidbuild_host_profile_parent_component_current(preparation->bootstrap_path,
                                                          &preparation->bootstrap_snapshot,
                                                          preparation->bootstrap_handle) ||
        !cupidbuild_host_windows_directory_handle_snapshot(transaction->repository_root_handle,
                                                           &transaction_root) ||
        !cupidbuild_host_windows_directory_handle_snapshot(transaction->output_parent_handle,
                                                           &transaction_parent) ||
#else
                                                          preparation
                                                              ->repository_root_descriptor) ||
        !cupidbuild_host_profile_parent_component_current(preparation->bootstrap_path,
                                                          &preparation->bootstrap_snapshot,
                                                          preparation->bootstrap_descriptor) ||
        !cupidbuild_host_directory_descriptor_snapshot(transaction->repository_root_descriptor,
                                                       &transaction_root) ||
        !cupidbuild_host_directory_descriptor_snapshot(transaction->output_parent_descriptor,
                                                       &transaction_parent) ||
#endif
        !cupidbuild_host_snapshot_identity_equal(&preparation->repository_root_snapshot,
                                                 &transaction->repository_root_snapshot) ||
        !cupidbuild_host_snapshot_identity_equal(&preparation->repository_root_snapshot,
                                                 &transaction_root) ||
        !cupidbuild_host_snapshot_identity_equal(&preparation->bootstrap_snapshot,
                                                 &transaction->output_parent_snapshot) ||
        !cupidbuild_host_snapshot_identity_equal(&preparation->bootstrap_snapshot,
                                                 &transaction_parent)) {
        cupidbuild_host_set_error(transaction, "profile parent changed while checked tools ran");
        return 0;
    }
    return 1;
}
