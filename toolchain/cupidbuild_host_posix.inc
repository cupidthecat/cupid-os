/**
 * @file cupidbuild_host_posix.inc
 * @brief Implement native POSIX retained I/O, process launch and rollback.
 */

#if defined(CUPIDBUILD_HOST_NATIVE_EINTR_TEST)
#define CUPIDBUILD_HOST_EINTR_READ 1u
#define CUPIDBUILD_HOST_EINTR_WRITE 2u
#define CUPIDBUILD_HOST_EINTR_WAIT_NOHANG 3u
#define CUPIDBUILD_HOST_EINTR_WAIT_BLOCKING 4u

void cupidbuild_host_native_eintr_test_arm(unsigned int operation);
int cupidbuild_host_native_eintr_test_retry_observed(unsigned int operation);

typedef struct {
    volatile unsigned int operation;
    volatile int injected;
    volatile int retried;
} cupidbuild_host_native_eintr_state_t;

static cupidbuild_host_native_eintr_state_t *cupidbuild_host_native_eintr_state;

void cupidbuild_host_native_eintr_test_arm(unsigned int operation) {
    if (cupidbuild_host_native_eintr_state == (cupidbuild_host_native_eintr_state_t *)0) {
        void *mapping = mmap((void *)0, sizeof(*cupidbuild_host_native_eintr_state),
                             PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if (mapping != MAP_FAILED) {
            cupidbuild_host_native_eintr_state = (cupidbuild_host_native_eintr_state_t *)mapping;
        }
    }
    if (cupidbuild_host_native_eintr_state != (cupidbuild_host_native_eintr_state_t *)0) {
        cupidbuild_host_native_eintr_state->operation = operation;
        cupidbuild_host_native_eintr_state->injected = 0;
        cupidbuild_host_native_eintr_state->retried = 0;
    }
}

int cupidbuild_host_native_eintr_test_retry_observed(unsigned int operation) {
    return cupidbuild_host_native_eintr_state != (cupidbuild_host_native_eintr_state_t *)0 &&
           cupidbuild_host_native_eintr_state->operation == operation &&
           cupidbuild_host_native_eintr_state->injected != 0 &&
           cupidbuild_host_native_eintr_state->retried != 0;
}

static int cupidbuild_host_native_eintr_test_inject(unsigned int operation) {
    if (cupidbuild_host_native_eintr_state == (cupidbuild_host_native_eintr_state_t *)0 ||
        cupidbuild_host_native_eintr_state->operation != operation) {
        return 0;
    }
    if (cupidbuild_host_native_eintr_state->injected == 0) {
        cupidbuild_host_native_eintr_state->injected = 1;
        errno = EINTR;
        return 1;
    }
    cupidbuild_host_native_eintr_state->retried = 1;
    return 0;
}

static ssize_t cupidbuild_host_native_eintr_read(int descriptor, void *bytes, size_t size) {
    if (cupidbuild_host_native_eintr_test_inject(CUPIDBUILD_HOST_EINTR_READ)) {
        return -1;
    }
    return read(descriptor, bytes, size);
}

static ssize_t cupidbuild_host_native_eintr_write(int descriptor, const void *bytes, size_t size) {
    if (cupidbuild_host_native_eintr_test_inject(CUPIDBUILD_HOST_EINTR_WRITE)) {
        return -1;
    }
    return write(descriptor, bytes, size);
}

static pid_t cupidbuild_host_native_eintr_waitpid(pid_t process, int *status, int options) {
    unsigned int operation = (options & WNOHANG) != 0 ? CUPIDBUILD_HOST_EINTR_WAIT_NOHANG
                                                      : CUPIDBUILD_HOST_EINTR_WAIT_BLOCKING;
    if (cupidbuild_host_native_eintr_test_inject(operation)) {
        return (pid_t)-1;
    }
    return waitpid(process, status, options);
}

#define CUPIDBUILD_HOST_LAUNCH_READ cupidbuild_host_native_eintr_read
#define CUPIDBUILD_HOST_LAUNCH_WRITE cupidbuild_host_native_eintr_write
#define CUPIDBUILD_HOST_LAUNCH_WAITPID cupidbuild_host_native_eintr_waitpid
#else
#define CUPIDBUILD_HOST_LAUNCH_READ read
#define CUPIDBUILD_HOST_LAUNCH_WRITE write
#define CUPIDBUILD_HOST_LAUNCH_WAITPID waitpid
#endif

static int cupidbuild_host_path_missing(const char *path) {
    struct stat information;
    return lstat(path, &information) != 0 && errno == ENOENT;
}

static int cupidbuild_host_path_has_link(const char *path) {
    char prefix[CUPIDBUILD_HOST_PATH_BYTES];
    size_t index;
    size_t size = strlen(path);
    if (size + 1u > sizeof(prefix)) {
        return 1;
    }
    (void)memcpy(prefix, path, size + 1u);
    for (index = 1u; index <= size; index++) {
        if (prefix[index] == '/' || prefix[index] == '\0') {
            struct stat information;
            char saved = prefix[index];
            prefix[index] = '\0';
            if (lstat(prefix, &information) != 0 || S_ISLNK(information.st_mode)) {
                prefix[index] = saved;
                return 1;
            }
            prefix[index] = saved;
        }
    }
    return 0;
}

static int cupidbuild_host_read_regular(const char *path, int optional,
                                        cupidbuild_host_snapshot_t *snapshot,
                                        unsigned char **bytes_out) {
    int descriptor;
    struct stat information;
    unsigned char *bytes = (unsigned char *)0;
    unsigned char digest[32];
    size_t offset = 0u;
    (void)memset(snapshot, 0, sizeof(*snapshot));
    if (optional != 0 && lstat(path, &information) != 0 && errno == ENOENT) {
        snapshot->present = 0;
        if (bytes_out != (unsigned char **)0) {
            *bytes_out = (unsigned char *)0;
        }
        return 1;
    }
    if (cupidbuild_host_path_has_link(path)) {
        return 0;
    }
    descriptor = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (descriptor < 0) {
        if (optional != 0 && errno == ENOENT) {
            snapshot->present = 0;
            if (bytes_out != (unsigned char **)0) {
                *bytes_out = (unsigned char *)0;
            }
            return 1;
        }
        return 0;
    }
    if (fstat(descriptor, &information) != 0 || !S_ISREG(information.st_mode) ||
        information.st_size < 0 ||
        (unsigned long long)information.st_size > CUPIDBUILD_HOST_FILE_LIMIT) {
        (void)close(descriptor);
        return 0;
    }
    if (!cupidbuild_host_read_open_contents(
            descriptor, (size_t)information.st_size,
            bytes_out != (unsigned char **)0 ? &bytes : (unsigned char **)0, digest)) {
        (void)close(descriptor);
        return 0;
    }
    offset = (size_t)information.st_size;
    snapshot->present = 1;
    snapshot->size = offset;
    snapshot->identity[0] = (unsigned int)(unsigned long long)information.st_dev;
    snapshot->identity[1] = (unsigned int)((unsigned long long)information.st_dev >> 32u);
    snapshot->identity[2] = (unsigned int)(unsigned long long)information.st_ino;
    snapshot->identity[3] = (unsigned int)((unsigned long long)information.st_ino >> 32u);
    snapshot->modified[0] = (unsigned int)information.st_mtime;
    (void)memcpy(snapshot->sha256, digest, sizeof(digest));
    (void)close(descriptor);
    if (bytes_out != (unsigned char **)0) {
        *bytes_out = bytes;
    } else {
        free(bytes);
    }
    return 1;
}

static int cupidbuild_host_directory_snapshot(const char *path,
                                              cupidbuild_host_snapshot_t *snapshot) {
    struct stat information;
    (void)memset(snapshot, 0, sizeof(*snapshot));
    if (cupidbuild_host_path_has_link(path) || stat(path, &information) != 0 ||
        !S_ISDIR(information.st_mode)) {
        return 0;
    }
    snapshot->present = 1;
    snapshot->identity[0] = (unsigned int)(unsigned long long)information.st_dev;
    snapshot->identity[1] = (unsigned int)((unsigned long long)information.st_dev >> 32u);
    snapshot->identity[2] = (unsigned int)(unsigned long long)information.st_ino;
    snapshot->identity[3] = (unsigned int)((unsigned long long)information.st_ino >> 32u);
    snapshot->modified[0] = (unsigned int)information.st_mtim.tv_sec;
    snapshot->modified[1] = (unsigned int)information.st_mtim.tv_nsec;
    snapshot->changed[0] = (unsigned int)information.st_ctim.tv_sec;
    snapshot->changed[1] = (unsigned int)information.st_ctim.tv_nsec;
    return 1;
}

static int cupidbuild_native_directory_descriptor_snapshot(int descriptor,
                                                           cupidbuild_host_snapshot_t *snapshot) {
    struct stat information;
    (void)memset(snapshot, 0, sizeof(*snapshot));
    if (descriptor < 0 || fstat(descriptor, &information) != 0 || !S_ISDIR(information.st_mode)) {
        return 0;
    }
    snapshot->present = 1;
    snapshot->identity[0] = (unsigned int)(unsigned long long)information.st_dev;
    snapshot->identity[1] = (unsigned int)((unsigned long long)information.st_dev >> 32u);
    snapshot->identity[2] = (unsigned int)(unsigned long long)information.st_ino;
    snapshot->identity[3] = (unsigned int)((unsigned long long)information.st_ino >> 32u);
    snapshot->modified[0] = (unsigned int)information.st_mtim.tv_sec;
    snapshot->modified[1] = (unsigned int)information.st_mtim.tv_nsec;
    snapshot->changed[0] = (unsigned int)information.st_ctim.tv_sec;
    snapshot->changed[1] = (unsigned int)information.st_ctim.tv_nsec;
    return 1;
}

static int cupidbuild_native_information_identity_equal(const struct stat *left,
                                                        const struct stat *right) {
    return left->st_dev == right->st_dev && left->st_ino == right->st_ino;
}

static int cupidbuild_native_open_relative(int parent, const char *name, int directory) {
    int flags = O_RDONLY | O_NOFOLLOW | O_CLOEXEC;
    if (directory != 0) {
        flags |= O_DIRECTORY;
    } else {
        flags |= O_NONBLOCK;
    }
    return openat(parent, name, flags);
}

static int cupidbuild_native_open_relative_path(int root, const char *logical, int directory) {
    int current = root;
    int next;
    char component[260];
    size_t start = 0u;
    size_t size = strlen(logical);
    size_t index;
    int owns_current = 0;
    if (root < 0 || size == 0u) {
        return -1;
    }
    for (index = 0u; index <= size; index++) {
        if (logical[index] == '/' || logical[index] == '\0') {
            size_t component_size = index - start;
            int last = index == size;
            if (component_size == 0u || component_size >= sizeof(component)) {
                if (owns_current != 0) {
                    (void)close(current);
                }
                return -1;
            }
            (void)memcpy(component, logical + start, component_size);
            component[component_size] = '\0';
            next = cupidbuild_native_open_relative(current, component, last != 0 ? directory : 1);
            if (owns_current != 0) {
                (void)close(current);
            }
            if (next < 0) {
                return -1;
            }
            current = next;
            owns_current = 1;
            start = index + 1u;
        }
    }
    return current;
}

static int cupidbuild_native_open_repository(const char *path) {
    const char *anchor;
    int current;
    int next;
    char component[260];
    size_t start;
    size_t size;
    size_t index;
    if (path == (const char *)0 || path[0] == '\0') {
        return -1;
    }
    anchor = path[0] == '/' ? "/" : ".";
    start = path[0] == '/' ? 1u : 0u;
    size = strlen(path);
    current = open(anchor, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (current < 0 || strcmp(path, "/") == 0 || strcmp(path, ".") == 0) {
        return current;
    }
    for (index = start; index <= size; index++) {
        if (path[index] == '/' || path[index] == '\0') {
            size_t component_size = index - start;
            if (component_size == 0u || component_size >= sizeof(component)) {
                (void)close(current);
                return -1;
            }
            (void)memcpy(component, path + start, component_size);
            component[component_size] = '\0';
            if ((component_size == 1u && component[0] == '.') ||
                (component_size == 2u && component[0] == '.' && component[1] == '.')) {
                (void)close(current);
                return -1;
            }
            next = cupidbuild_native_open_relative(current, component, 1);
            (void)close(current);
            if (next < 0) {
                return -1;
            }
            current = next;
            start = index + 1u;
        }
    }
    return current;
}

static int cupidbuild_native_read_repository_regular(int repository, const char *logical,
                                                     unsigned long long limit,
                                                     cupidbuild_host_snapshot_t *snapshot,
                                                     unsigned char **bytes_out) {
    struct stat information;
    int descriptor = cupidbuild_native_open_relative_path(repository, logical, 0);
    unsigned long long size;
    unsigned char *bytes = (unsigned char *)0;
    unsigned char digest[32];
    (void)memset(snapshot, 0, sizeof(*snapshot));
    if (descriptor < 0 || fstat(descriptor, &information) != 0 || !S_ISREG(information.st_mode) ||
        information.st_size < 0) {
        if (descriptor >= 0) {
            (void)close(descriptor);
        }
        return 0;
    }
    if ((unsigned long long)information.st_size > (unsigned long long)limit) {
        (void)close(descriptor);
        return 0;
    }
    size = (unsigned long long)information.st_size;
    if (!cupidbuild_host_read_open_contents(
            descriptor, size, bytes_out != (unsigned char **)0 ? &bytes : (unsigned char **)0,
            digest)) {
        (void)close(descriptor);
        return 0;
    }
    if (close(descriptor) != 0) {
        free(bytes);
        return 0;
    }
    snapshot->present = 1;
    snapshot->size = size;
    snapshot->identity[0] = (unsigned int)(unsigned long long)information.st_dev;
    snapshot->identity[1] = (unsigned int)((unsigned long long)information.st_dev >> 32u);
    snapshot->identity[2] = (unsigned int)(unsigned long long)information.st_ino;
    snapshot->identity[3] = (unsigned int)((unsigned long long)information.st_ino >> 32u);
    snapshot->modified[0] = (unsigned int)information.st_mtime;
    (void)memcpy(snapshot->sha256, digest, sizeof(digest));
    if (bytes_out != (unsigned char **)0) {
        *bytes_out = bytes;
    } else {
        free(bytes);
    }
    return 1;
}

static int cupidbuild_native_read_relative_regular(int parent, const char *name, int optional,
                                                   unsigned long long limit,
                                                   cupidbuild_host_snapshot_t *snapshot,
                                                   unsigned char **bytes_out) {
    struct stat information;
    int descriptor = cupidbuild_native_open_relative(parent, name, 0);
    unsigned long long size;
    unsigned char *bytes = (unsigned char *)0;
    unsigned char digest[32];
    (void)memset(snapshot, 0, sizeof(*snapshot));
    if (descriptor < 0) {
        if (optional != 0 && errno == ENOENT) {
            if (bytes_out != (unsigned char **)0) {
                *bytes_out = (unsigned char *)0;
            }
            return 1;
        }
        return 0;
    }
    if (fstat(descriptor, &information) != 0 || !S_ISREG(information.st_mode) ||
        information.st_size < 0 ||
        (unsigned long long)information.st_size > (unsigned long long)limit) {
        (void)close(descriptor);
        return 0;
    }
    size = (unsigned long long)information.st_size;
    if (!cupidbuild_host_read_open_contents(
            descriptor, size, bytes_out != (unsigned char **)0 ? &bytes : (unsigned char **)0,
            digest)) {
        (void)close(descriptor);
        return 0;
    }
    if (close(descriptor) != 0) {
        free(bytes);
        return 0;
    }
    snapshot->present = 1;
    snapshot->size = size;
    snapshot->identity[0] = (unsigned int)(unsigned long long)information.st_dev;
    snapshot->identity[1] = (unsigned int)((unsigned long long)information.st_dev >> 32u);
    snapshot->identity[2] = (unsigned int)(unsigned long long)information.st_ino;
    snapshot->identity[3] = (unsigned int)((unsigned long long)information.st_ino >> 32u);
    snapshot->modified[0] = (unsigned int)information.st_mtime;
    (void)memcpy(snapshot->sha256, digest, sizeof(digest));
    if (bytes_out != (unsigned char **)0) {
        *bytes_out = bytes;
    } else {
        free(bytes);
    }
    return 1;
}

static int cupidbuild_host_seed_members_platform(const char *directory, const char *suffix,
                                                 const char *const *expected,
                                                 size_t expected_count) {
    DIR *stream = opendir(directory);
    struct dirent *entry;
    int valid = stream != (DIR *)0;
    if (stream == (DIR *)0) {
        return 0;
    }
    errno = 0;
    while ((entry = readdir(stream)) != (struct dirent *)0) {
        if (cupidbuild_host_name_has_suffix(entry->d_name, suffix) &&
            !cupidbuild_host_name_is_expected(entry->d_name, expected, expected_count)) {
            valid = 0;
            break;
        }
        errno = 0;
    }
    if (entry == (struct dirent *)0 && errno != 0) {
        valid = 0;
    }
    if (closedir(stream) != 0) {
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
    int repository = transaction->repository_root_descriptor;
    int root = -1;
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
    if (!cupidbuild_native_directory_descriptor_snapshot(repository, &repository_snapshot) ||
        !cupidbuild_host_snapshot_identity_equal(&transaction->repository_root_snapshot,
                                                 &repository_snapshot)) {
        valid = 0;
    } else {
        root = cupidbuild_native_open_relative_path(repository, logical_root, 1);
        if (!cupidbuild_native_directory_descriptor_snapshot(root, &root_snapshot) ||
            cupidbuild_host_snapshot_identity_equal(&repository_snapshot, &root_snapshot) ||
            !cupidbuild_host_discovery_worklist_add(&worklist, logical_root, &root_snapshot,
                                                    root)) {
            valid = 0;
        } else {
            visited[visited_count++] = root_snapshot;
            root = -1;
            directory_count = 1u;
        }
    }
    if (root >= 0) {
        (void)close(root);
    }
    while (valid != 0 && worklist.count != 0u) {
        cupidbuild_host_discovery_directory_t current = worklist.directories[worklist.count - 1u];
        cupidbuild_host_snapshot_t completed_snapshot;
        int enumeration_descriptor = fcntl(current.descriptor, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
        DIR *stream = enumeration_descriptor < 0 ? (DIR *)0 : fdopendir(enumeration_descriptor);
        struct dirent *entry = (struct dirent *)0;
        int retained = 0;
        worklist.count--;
        if (stream == (DIR *)0) {
            if (enumeration_descriptor >= 0) {
                (void)close(enumeration_descriptor);
            }
            valid = 0;
        }
        errno = 0;
        while (valid != 0 && (entry = readdir(stream)) != (struct dirent *)0) {
            char logical[CUPIDBUILD_HOST_PATH_BYTES];
            struct stat information;
            int matches;
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                errno = 0;
                continue;
            }
            if (!cupidbuild_host_join(logical, sizeof(logical), current.logical, entry->d_name)) {
                valid = 0;
                break;
            }
            if (fstatat(dirfd(stream), entry->d_name, &information, AT_SYMLINK_NOFOLLOW) != 0) {
                if (transaction->compile_discovery != 0 && errno == ENOENT &&
                    !cupidbuild_host_discovery_suffix(entry->d_name, suffixes, suffix_count)) {
                    errno = 0;
                    continue;
                }
                valid = 0;
                break;
            }
            if (entry->d_name[0] == '.' &&
                (S_ISDIR(information.st_mode) || skip_hidden_files != 0)) {
                errno = 0;
                continue;
            }
            if (S_ISLNK(information.st_mode)) {
                valid = 0;
                break;
            }
            matches = cupidbuild_host_discovery_suffix(entry->d_name, suffixes, suffix_count);
            if (S_ISDIR(information.st_mode) && matches != 0 && reject_matching_nonfiles != 0) {
                valid = 0;
                break;
            }
            if (S_ISDIR(information.st_mode)) {
                struct stat opened_information;
                cupidbuild_host_snapshot_t child_snapshot;
                int child = cupidbuild_native_open_relative(dirfd(stream), entry->d_name, 1);
                size_t prior;
                int alias = 0;
                if (child < 0 || fstat(child, &opened_information) != 0 ||
                    !cupidbuild_native_information_identity_equal(&information,
                                                                  &opened_information) ||
                    !cupidbuild_native_directory_descriptor_snapshot(child, &child_snapshot)) {
                    if (child >= 0) {
                        (void)close(child);
                    }
                    valid = 0;
                    break;
                }
                for (prior = 0u; prior < visited_count; prior++) {
                    if (cupidbuild_host_snapshot_identity_equal(&visited[prior], &child_snapshot)) {
                        alias = 1;
                        break;
                    }
                }
                if (alias != 0 || directory_count >= CUPIDBUILD_HOST_DISCOVERY_DIRECTORIES ||
                    !cupidbuild_host_discovery_worklist_add(&worklist, logical, &child_snapshot,
                                                            child)) {
                    (void)close(child);
                    valid = 0;
                    break;
                }
                visited[visited_count++] = child_snapshot;
                directory_count++;
            } else if (S_ISREG(information.st_mode) && matches != 0) {
                struct stat opened_information;
                cupidbuild_host_snapshot_t child_snapshot;
                int child = cupidbuild_native_open_relative(dirfd(stream), entry->d_name, 0);
                if (child < 0 || fstat(child, &opened_information) != 0 ||
                    !S_ISREG(opened_information.st_mode) ||
                    !cupidbuild_native_information_identity_equal(&information,
                                                                  &opened_information) ||
                    !cupidbuild_host_read_open_file(child, CUPIDBUILD_HOST_FILE_LIMIT,
                                                    &child_snapshot, (unsigned char **)0) ||
                    !cupidbuild_host_discovery_add(paths, logical, &child_snapshot)) {
                    if (child >= 0) {
                        (void)close(child);
                    }
                    valid = 0;
                    break;
                }
                if (close(child) != 0) {
                    valid = 0;
                    break;
                }
            } else if (matches != 0 && reject_matching_nonfiles != 0) {
                valid = 0;
                break;
            }
            errno = 0;
        }
        if (valid != 0 && entry == (struct dirent *)0 && errno != 0) {
            valid = 0;
        }
        if (stream != (DIR *)0 && closedir(stream) != 0) {
            valid = 0;
        }
        if (valid != 0 && (!cupidbuild_native_directory_descriptor_snapshot(current.descriptor,
                                                                            &completed_snapshot) ||
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
            current.descriptor = -1;
            current.logical = (char *)0;
        }
        if (current.descriptor >= 0 && close(current.descriptor) != 0) {
            valid = 0;
        }
        free(current.logical);
    }
    while (worklist.count != 0u) {
        cupidbuild_host_discovery_directory_t *directory = &worklist.directories[--worklist.count];
        (void)close(directory->descriptor);
        free(directory->logical);
    }
    free(worklist.directories);
    free(visited);
    return valid;
}

static int cupidbuild_host_seed_members_repository(cupidbuild_host_transaction_t *transaction,
                                                   const char *logical_directory,
                                                   const char *suffix, const char *const *expected,
                                                   size_t expected_count) {
    cupidbuild_host_snapshot_t root_snapshot;
    int descriptor;
    DIR *stream;
    struct dirent *entry = (struct dirent *)0;
    int valid;
    if (!cupidbuild_native_directory_descriptor_snapshot(transaction->repository_root_descriptor,
                                                         &root_snapshot) ||
        !cupidbuild_host_snapshot_identity_equal(&transaction->repository_root_snapshot,
                                                 &root_snapshot)) {
        return 0;
    }
    descriptor = cupidbuild_native_open_relative_path(transaction->repository_root_descriptor,
                                                      logical_directory, 1);
    if (descriptor < 0) {
        return 0;
    }
    stream = fdopendir(descriptor);
    if (stream == (DIR *)0) {
        (void)close(descriptor);
        return 0;
    }
    valid = 1;
    errno = 0;
    while ((entry = readdir(stream)) != (struct dirent *)0) {
        if (cupidbuild_host_name_has_suffix(entry->d_name, suffix) &&
            !cupidbuild_host_name_is_expected(entry->d_name, expected, expected_count)) {
            valid = 0;
            break;
        }
        errno = 0;
    }
    if (entry == (struct dirent *)0 && errno != 0) {
        valid = 0;
    }
    if (closedir(stream) != 0) {
        valid = 0;
    }
    return valid;
}

static int cupidbuild_host_write_anonymous(const char *name, const unsigned char *bytes,
                                           size_t size, char *path, size_t path_capacity,
                                           cupidbuild_host_snapshot_t *snapshot,
                                           int *descriptor_out) {
    struct stat information;
    size_t offset = 0u;
    int descriptor = memfd_create(name, MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (descriptor < 0) {
        return 0;
    }
    while (offset < size) {
        ssize_t count = write(descriptor, bytes + offset, size - offset);
        if (count <= 0) {
            (void)close(descriptor);
            return 0;
        }
        offset += (size_t)count;
    }
    if (fsync(descriptor) != 0 || fstat(descriptor, &information) != 0 ||
        fcntl(descriptor, F_ADD_SEALS, F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE) !=
            0) {
        (void)close(descriptor);
        return 0;
    }
    if (!cupidbuild_host_copy_text(path, path_capacity, name)) {
        (void)close(descriptor);
        return 0;
    }
    (void)memset(snapshot, 0, sizeof(*snapshot));
    snapshot->present = 1;
    snapshot->size = size;
    snapshot->identity[0] = (unsigned int)(unsigned long long)information.st_dev;
    snapshot->identity[1] = (unsigned int)((unsigned long long)information.st_dev >> 32u);
    snapshot->identity[2] = (unsigned int)(unsigned long long)information.st_ino;
    snapshot->identity[3] = (unsigned int)((unsigned long long)information.st_ino >> 32u);
    snapshot->modified[0] = (unsigned int)information.st_mtime;
    cupidbuild_sha256(bytes, size, snapshot->sha256);
    *descriptor_out = descriptor;
    return 1;
}

static int cupidbuild_host_open_anonymous(const char *name, char *path, size_t path_capacity,
                                          int *descriptor_out) {
    int descriptor = memfd_create(name, MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (descriptor < 0 || !cupidbuild_host_copy_text(path, path_capacity, name)) {
        if (descriptor >= 0) {
            (void)close(descriptor);
        }
        return 0;
    }
    *descriptor_out = descriptor;
    return 1;
}

static int cupidbuild_host_seal_anonymous(int descriptor) {
    return fcntl(descriptor, F_ADD_SEALS,
                 F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE) == 0;
}

static int cupidbuild_host_absolute_directory(char *destination, size_t capacity,
                                              const char *path) {
    char current[CUPIDBUILD_HOST_PATH_BYTES];
    if (path == (const char *)0 || path[0] == '\0') {
        return 0;
    }
    if (path[0] == '/') {
        return cupidbuild_host_copy_text(destination, capacity, path);
    }
    if (getcwd(current, sizeof(current)) == (char *)0) {
        return 0;
    }
    return cupidbuild_host_join(destination, capacity, current, path);
}

static int cupidbuild_host_make_executable(const char *path) {
    return chmod(path, 0700) == 0;
}

static void cupidbuild_host_delete_file(const char *path) {
    (void)unlink(path);
}

static unsigned int cupidbuild_host_process_id(void) {
    return (unsigned int)getpid();
}

static int cupidbuild_host_process_alive(unsigned int process_id) {
    return kill((pid_t)process_id, 0) == 0 || errno == EPERM;
}

static int cupidbuild_host_posix_open_snapshot(int descriptor,
                                               cupidbuild_host_snapshot_t *snapshot) {
    struct stat information;
    if (snapshot == (cupidbuild_host_snapshot_t *)0) {
        return 1;
    }
    (void)memset(snapshot, 0, sizeof(*snapshot));
    if (fstat(descriptor, &information) != 0 || !S_ISREG(information.st_mode) ||
        information.st_size < 0) {
        return 0;
    }
    snapshot->present = 1;
    snapshot->size = (unsigned long long)information.st_size;
    snapshot->identity[0] = (unsigned int)information.st_dev;
    snapshot->identity[1] = (unsigned int)((unsigned long long)information.st_dev >> 32u);
    snapshot->identity[2] = (unsigned int)information.st_ino;
    snapshot->identity[3] = (unsigned int)((unsigned long long)information.st_ino >> 32u);
    snapshot->modified[0] = (unsigned int)information.st_mtime;
    return 1;
}

static int cupidbuild_host_read_open_file(int descriptor, unsigned long long limit,
                                          cupidbuild_host_snapshot_t *snapshot,
                                          unsigned char **bytes_out) {
    cupidbuild_host_snapshot_t current;
    unsigned char *bytes = (unsigned char *)0;
    unsigned char digest[32];
    if (!cupidbuild_host_posix_open_snapshot(descriptor, &current) || current.size > limit) {
        return 0;
    }
    if (!cupidbuild_host_read_open_contents(
            descriptor, current.size,
            bytes_out != (unsigned char **)0 ? &bytes : (unsigned char **)0, digest)) {
        return 0;
    }
    (void)memcpy(current.sha256, digest, sizeof(digest));
    if (snapshot != (cupidbuild_host_snapshot_t *)0) {
        *snapshot = current;
    }
    if (bytes_out != (unsigned char **)0) {
        *bytes_out = bytes;
    } else {
        free(bytes);
    }
    return 1;
}

static char *cupidbuild_host_exec_argument(const char *argument) {
    char *mutable_argument;
    (void)memcpy(&mutable_argument, &argument, sizeof(mutable_argument));
    return mutable_argument;
}

static void cupidbuild_host_write_launch_failure(int descriptor) {
    unsigned char marker = 1u;
    ssize_t result;
    do {
        result = CUPIDBUILD_HOST_LAUNCH_WRITE(descriptor, &marker, 1u);
    } while (result < 0 && errno == EINTR);
}

static int cupidbuild_host_read_launch_status(int descriptor) {
    unsigned char marker = 0u;
    ssize_t result;
    do {
        result = CUPIDBUILD_HOST_LAUNCH_READ(descriptor, &marker, 1u);
    } while (result < 0 && errno == EINTR);
    if (result == 0) {
        return 0;
    }
    if (result == 1 && marker == 1u) {
        return 1;
    }
    return -1;
}

static int cupidbuild_host_posix_duplicate_above_standard(int descriptor) {
    int result;
    if (descriptor > STDERR_FILENO) {
        return descriptor;
    }
    do {
        result = fcntl(descriptor, F_DUPFD, STDERR_FILENO + 1);
    } while (result < 0 && errno == EINTR);
    return result;
}

static int cupidbuild_host_posix_promote_above_standard(int *descriptor) {
    int original;
    int duplicated;
    if (descriptor == (int *)0 || *descriptor < 0) {
        return 0;
    }
    if (*descriptor > STDERR_FILENO) {
        return 1;
    }
    original = *descriptor;
#if defined(CUPIDBUILD_HOST_LOW_FD_FAILURE_TEST)
    if (getenv("CUPIDBUILD_LOW_FD_TEST_FAIL_PROMOTION") != (const char *)0) {
        (void)close(original);
        *descriptor = -1;
        errno = EMFILE;
        return 0;
    }
#endif
    duplicated = cupidbuild_host_posix_duplicate_above_standard(original);
    if (duplicated <= STDERR_FILENO) {
        (void)close(original);
        *descriptor = -1;
        return 0;
    }
    (void)close(original);
    *descriptor = duplicated;
    return 1;
}

static int cupidbuild_host_posix_duplicate_stream(int source, int target) {
    int result;
    if (source == target) {
        return 1;
    }
    do {
        result = dup2(source, target);
    } while (result < 0 && (errno == EINTR || errno == EBUSY));
    return result == target;
}

static int cupidbuild_host_run_process(
    const char *tool, const cupidbuild_host_snapshot_t *expected_tool, int tool_descriptor,
    const char *const *arguments, const char *stdout_path, const char *stderr_path,
    int stdout_descriptor, int stderr_descriptor, const char *working_directory,
    int working_descriptor, const cupidbuild_host_transaction_t *inherited_transaction,
    cupidbuild_host_snapshot_t *stdout_opened, cupidbuild_host_snapshot_t *stderr_opened,
    unsigned int timeout_milliseconds) {
    char **argv;
    size_t argument_count = 0u;
    size_t index;
    pid_t child;
    int own_stream_descriptors = stdout_descriptor < 0;
    int own_tool_descriptor = tool_descriptor < 0;
    int launch_pipe[2] = {-1, -1};
    unsigned int elapsed = 0u;
    int status = 0;
    pid_t wait_result;
    struct timespec pause_time;
    if (own_stream_descriptors == 0 &&
        (stdout_descriptor <= STDERR_FILENO || stderr_descriptor <= STDERR_FILENO ||
         stdout_descriptor == stderr_descriptor)) {
        return -1;
    }
    while (arguments[argument_count] != (const char *)0) {
        if (argument_count == (size_t)-2) {
            return -1;
        }
        argument_count++;
    }
    argv = (char **)calloc(argument_count + 2u, sizeof(*argv));
    if (argv == (char **)0) {
        return -1;
    }
    argv[0] = cupidbuild_host_exec_argument(tool);
    for (index = 0u; index < argument_count; index++) {
        argv[index + 1u] = cupidbuild_host_exec_argument(arguments[index]);
    }
    if (own_tool_descriptor != 0) {
        tool_descriptor = open(tool, O_RDONLY | O_NOFOLLOW);
    }
    {
        cupidbuild_host_snapshot_t tool_snapshot;
        int valid = tool_descriptor >= 0 &&
                    cupidbuild_host_read_open_file(tool_descriptor, CUPIDBUILD_HOST_FILE_LIMIT,
                                                   &tool_snapshot, (unsigned char **)0) &&
                    cupidbuild_host_snapshot_equal(&tool_snapshot, expected_tool);
        if (valid == 0) {
            if (tool_descriptor >= 0 && own_tool_descriptor != 0) {
                (void)close(tool_descriptor);
            }
            free(argv);
            return -1;
        }
    }
    if (own_stream_descriptors != 0) {
        stdout_descriptor = open(stdout_path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        stderr_descriptor = open(stderr_path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    }
    if (stdout_descriptor < 0 || stderr_descriptor < 0 ||
        !cupidbuild_host_posix_open_snapshot(stdout_descriptor, stdout_opened) ||
        !cupidbuild_host_posix_open_snapshot(stderr_descriptor, stderr_opened)) {
        if (stdout_descriptor >= 0 && own_stream_descriptors != 0) {
            (void)close(stdout_descriptor);
        }
        if (stderr_descriptor >= 0 && own_stream_descriptors != 0) {
            (void)close(stderr_descriptor);
        }
        if (own_tool_descriptor != 0) {
            (void)close(tool_descriptor);
        }
        free(argv);
        return -1;
    }
    if (pipe(launch_pipe) != 0 || !cupidbuild_host_posix_promote_above_standard(&launch_pipe[0]) ||
        !cupidbuild_host_posix_promote_above_standard(&launch_pipe[1]) ||
        fcntl(launch_pipe[1], F_SETFD, FD_CLOEXEC) != 0) {
        if (launch_pipe[0] >= 0) {
            (void)close(launch_pipe[0]);
        }
        if (launch_pipe[1] >= 0) {
            (void)close(launch_pipe[1]);
        }
        if (own_stream_descriptors != 0) {
            (void)close(stdout_descriptor);
            (void)close(stderr_descriptor);
        }
        if (own_tool_descriptor != 0) {
            (void)close(tool_descriptor);
        }
        free(argv);
        return -1;
    }
    child = fork();
    if (child == 0) {
        int exec_tool_descriptor;
        unsigned int inherited_index;
        (void)close(launch_pipe[0]);
        if ((inherited_transaction != (const cupidbuild_host_transaction_t *)0 &&
             inherited_transaction->private_flat != 0 &&
             ((inherited_transaction->candidate_descriptor >= 0 &&
               fcntl(inherited_transaction->candidate_descriptor, F_SETFD, 0) != 0) ||
              (inherited_transaction->private_output_descriptor >= 0 &&
               fcntl(inherited_transaction->private_output_descriptor, F_SETFD, 0) != 0))) ||
            (working_descriptor >= 0 && fchdir(working_descriptor) != 0) ||
            (working_descriptor < 0 && working_directory != (const char *)0 &&
             chdir(working_directory) != 0)) {
            cupidbuild_host_write_launch_failure(launch_pipe[1]);
            _exit(125);
        }
        if (inherited_transaction != (const cupidbuild_host_transaction_t *)0 &&
            inherited_transaction->private_flat != 0) {
            for (inherited_index = 0u; inherited_index < inherited_transaction->input_count;
                 inherited_index++) {
                int descriptor = inherited_transaction->inputs[inherited_index].frozen_descriptor;
                if (descriptor >= 0 && fcntl(descriptor, F_SETFD, 0) != 0) {
                    cupidbuild_host_write_launch_failure(launch_pipe[1]);
                    _exit(125);
                }
            }
        }
        exec_tool_descriptor = cupidbuild_host_posix_duplicate_above_standard(tool_descriptor);
        if (exec_tool_descriptor < 0 ||
            !cupidbuild_host_posix_duplicate_stream(stdout_descriptor, STDOUT_FILENO) ||
            !cupidbuild_host_posix_duplicate_stream(stderr_descriptor, STDERR_FILENO)) {
            cupidbuild_host_write_launch_failure(launch_pipe[1]);
            _exit(125);
        }
        if (stdout_descriptor > STDERR_FILENO) {
            (void)close(stdout_descriptor);
        }
        if (stderr_descriptor > STDERR_FILENO && stderr_descriptor != stdout_descriptor) {
            (void)close(stderr_descriptor);
        }
        (void)fexecve(exec_tool_descriptor, argv, environ);
        cupidbuild_host_write_launch_failure(launch_pipe[1]);
        _exit(125);
    }
    if (own_stream_descriptors != 0) {
        (void)close(stdout_descriptor);
        (void)close(stderr_descriptor);
    }
    if (own_tool_descriptor != 0) {
        (void)close(tool_descriptor);
    }
    (void)close(launch_pipe[1]);
    if (child < 0) {
        (void)close(launch_pipe[0]);
        free(argv);
        return -1;
    }
    free(argv);
    pause_time.tv_sec = 0;
    pause_time.tv_nsec = 10000000L;
#if defined(CUPIDBUILD_PUBLICATION_RACE_TEST)
    if (!cupidbuild_host_publication_test_pause("after-tool-launch")) {
        (void)kill(child, SIGKILL);
        do {
            wait_result = CUPIDBUILD_HOST_LAUNCH_WAITPID(child, &status, 0);
        } while (wait_result < 0 && errno == EINTR);
        (void)close(launch_pipe[0]);
        return -1;
    }
#endif
    for (;;) {
        wait_result = CUPIDBUILD_HOST_LAUNCH_WAITPID(child, &status, WNOHANG);
        if (wait_result > 0) {
            break;
        }
        if (wait_result < 0 && errno == EINTR) {
            continue;
        }
        if (wait_result < 0) {
            (void)close(launch_pipe[0]);
            return -1;
        }
        if (elapsed >= timeout_milliseconds) {
            (void)kill(child, SIGKILL);
            do {
                wait_result = CUPIDBUILD_HOST_LAUNCH_WAITPID(child, &status, 0);
            } while (wait_result < 0 && errno == EINTR);
            (void)close(launch_pipe[0]);
            return -2;
        }
        (void)nanosleep(&pause_time, (struct timespec *)0);
        elapsed += 10u;
    }
    wait_result = (pid_t)cupidbuild_host_read_launch_status(launch_pipe[0]);
    (void)close(launch_pipe[0]);
    if (wait_result != 0) {
        return -1;
    }
    if (!WIFEXITED(status)) {
        return 125;
    }
    return WEXITSTATUS(status);
}

static int cupidbuild_host_open_directory(const char *path) {
    return open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
}

static int cupidbuild_host_directory_descriptor_snapshot(int descriptor,
                                                         cupidbuild_host_snapshot_t *snapshot) {
    struct stat information;
    (void)memset(snapshot, 0, sizeof(*snapshot));
    if (descriptor < 0 || fstat(descriptor, &information) != 0 || !S_ISDIR(information.st_mode)) {
        return 0;
    }
    snapshot->present = 1;
    snapshot->identity[0] = (unsigned int)(unsigned long long)information.st_dev;
    snapshot->identity[1] = (unsigned int)((unsigned long long)information.st_dev >> 32u);
    snapshot->identity[2] = (unsigned int)(unsigned long long)information.st_ino;
    snapshot->identity[3] = (unsigned int)((unsigned long long)information.st_ino >> 32u);
    snapshot->modified[0] = (unsigned int)information.st_mtim.tv_sec;
    snapshot->modified[1] = (unsigned int)information.st_mtim.tv_nsec;
    snapshot->changed[0] = (unsigned int)information.st_ctim.tv_sec;
    snapshot->changed[1] = (unsigned int)information.st_ctim.tv_nsec;
    return 1;
}

#if defined(CUPIDBUILD_HOST_CLOSE_FAILURE_TEST)
static unsigned int cupidbuild_host_close_failure_countdown;

void cupidbuild_host_close_failure_test_arm(unsigned int close_index) {
    cupidbuild_host_close_failure_countdown = close_index;
}
#endif

static int cupidbuild_host_close_directory(int descriptor) {
    int result;
    if (descriptor < 0) {
        return 1;
    }
    result = close(descriptor);
#if defined(CUPIDBUILD_HOST_CLOSE_FAILURE_TEST)
    if (cupidbuild_host_close_failure_countdown != 0u) {
        cupidbuild_host_close_failure_countdown--;
        if (cupidbuild_host_close_failure_countdown == 0u) {
            return 0;
        }
    }
#endif
    return result == 0;
}

static int cupidbuild_host_atomic_replace(cupidbuild_host_transaction_t *transaction) {
    return cupidbuild_host_posix_atomic_replace(transaction);
}
