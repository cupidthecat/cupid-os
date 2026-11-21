/**
 * @file cupidbuild_host_linux.inc
 * @brief Implement retained I/O and process launch through the i386 Linux ABI.
 */

#define CUPIDBUILD_LINUX_SYS_EXIT 1
#define CUPIDBUILD_LINUX_SYS_FORK 2
#define CUPIDBUILD_LINUX_SYS_READ 3
#define CUPIDBUILD_LINUX_SYS_WRITE 4
#define CUPIDBUILD_LINUX_SYS_OPEN 5
#define CUPIDBUILD_LINUX_SYS_CLOSE 6
#define CUPIDBUILD_LINUX_SYS_WAITPID 7
#define CUPIDBUILD_LINUX_SYS_LINK 9
#define CUPIDBUILD_LINUX_SYS_DUP2 63
#define CUPIDBUILD_LINUX_SYS_UNLINK 10
#define CUPIDBUILD_LINUX_SYS_EXECVE 11
#define CUPIDBUILD_LINUX_SYS_CHDIR 12
#define CUPIDBUILD_LINUX_SYS_CHMOD 15
#define CUPIDBUILD_LINUX_SYS_GETPID 20
#define CUPIDBUILD_LINUX_SYS_GETCWD 183
#define CUPIDBUILD_LINUX_SYS_KILL 37
#define CUPIDBUILD_LINUX_SYS_RENAME 38
#define CUPIDBUILD_LINUX_SYS_MKDIR 39
#define CUPIDBUILD_LINUX_SYS_RMDIR 40
#define CUPIDBUILD_LINUX_SYS_PIPE 42
#define CUPIDBUILD_LINUX_SYS_FCHMOD 94
#define CUPIDBUILD_LINUX_SYS_FSYNC 118
#define CUPIDBUILD_LINUX_SYS_FCHDIR 133
#define CUPIDBUILD_LINUX_SYS_NANOSLEEP 162
#define CUPIDBUILD_LINUX_SYS_LSTAT64 196
#define CUPIDBUILD_LINUX_SYS_FSTAT64 197
#define CUPIDBUILD_LINUX_SYS_GETDENTS64 220
#define CUPIDBUILD_LINUX_SYS_FCNTL64 221
#define CUPIDBUILD_LINUX_SYS_OPENAT 295
#define CUPIDBUILD_LINUX_SYS_MKDIRAT 296
#define CUPIDBUILD_LINUX_SYS_FSTATAT64 300
#define CUPIDBUILD_LINUX_SYS_UNLINKAT 301
#define CUPIDBUILD_LINUX_SYS_RENAMEAT 302
#define CUPIDBUILD_LINUX_SYS_LINKAT 303
#define CUPIDBUILD_LINUX_SYS_RENAMEAT2 353
#define CUPIDBUILD_LINUX_SYS_MEMFD_CREATE 356
#define CUPIDBUILD_LINUX_SYS_EXECVEAT 358
#define CUPIDBUILD_LINUX_O_WRONLY 1u
#define CUPIDBUILD_LINUX_O_RDWR 2u
#define CUPIDBUILD_LINUX_O_CREAT 64u
#define CUPIDBUILD_LINUX_O_EXCL 128u
#define CUPIDBUILD_LINUX_O_NONBLOCK 2048u
#define CUPIDBUILD_LINUX_O_LARGEFILE 32768u
#define CUPIDBUILD_LINUX_O_DIRECTORY 65536u
#define CUPIDBUILD_LINUX_O_NOFOLLOW 131072u
#define CUPIDBUILD_LINUX_O_CLOEXEC 524288u
#define CUPIDBUILD_LINUX_WNOHANG 1u
#define CUPIDBUILD_LINUX_AT_FDCWD (-100)
#define CUPIDBUILD_LINUX_AT_REMOVEDIR 512u
#define CUPIDBUILD_LINUX_AT_SYMLINK_FOLLOW 1024u
#define CUPIDBUILD_LINUX_RENAME_NOREPLACE 1u
#define CUPIDBUILD_LINUX_RENAME_EXCHANGE 2u
#define CUPIDBUILD_LINUX_AT_SYMLINK_NOFOLLOW 256u
#define CUPIDBUILD_LINUX_AT_EMPTY_PATH 4096u
#define CUPIDBUILD_LINUX_SIGKILL 9u
#define CUPIDBUILD_LINUX_EBUSY 16
#define CUPIDBUILD_LINUX_MFD_CLOEXEC 1u
#define CUPIDBUILD_LINUX_MFD_ALLOW_SEALING 2u
#define CUPIDBUILD_LINUX_F_SETFD 2u
#define CUPIDBUILD_LINUX_F_DUPFD 0u
#define CUPIDBUILD_LINUX_FD_CLOEXEC 1u
#define CUPIDBUILD_LINUX_F_ADD_SEALS 1033u
#define CUPIDBUILD_LINUX_F_SEAL_ALL 15u
#define CUPIDBUILD_LINUX_ENOENT 2
#define CUPIDBUILD_LINUX_EPERM 1
#define CUPIDBUILD_LINUX_S_IFMT 0170000u
#define CUPIDBUILD_LINUX_S_IFDIR 0040000u
#define CUPIDBUILD_LINUX_S_IFREG 0100000u
#define CUPIDBUILD_LINUX_S_IFLNK 0120000u

typedef struct {
    int seconds;
    int nanoseconds;
} cupidbuild_linux_time_t;

static unsigned int cupidbuild_linux_u32(const unsigned char *bytes) {
    return (unsigned int)bytes[0] | ((unsigned int)bytes[1] << 8u) |
           ((unsigned int)bytes[2] << 16u) | ((unsigned int)bytes[3] << 24u);
}

static unsigned int cupidbuild_linux_u16(const unsigned char *bytes) {
    return (unsigned int)bytes[0] | ((unsigned int)bytes[1] << 8u);
}

static int cupidbuild_linux_stat(const char *path, unsigned char result[96]) {
    return cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_LSTAT64, (unsigned int)path,
                                (unsigned int)result);
}

static int cupidbuild_host_path_missing(const char *path) {
    unsigned char information[96];
    return cupidbuild_linux_stat(path, information) == -CUPIDBUILD_LINUX_ENOENT;
}

static unsigned int cupidbuild_linux_mode(const unsigned char stat_bytes[96]) {
    return cupidbuild_linux_u32(stat_bytes + 16u);
}

static int cupidbuild_linux_descriptor_snapshot(int descriptor,
                                                cupidbuild_host_snapshot_t *snapshot) {
    unsigned char information[96];
    (void)memset(snapshot, 0, sizeof(*snapshot));
    if (descriptor < 0 ||
        cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_FSTAT64, (unsigned int)descriptor,
                             (unsigned int)information) < 0 ||
        (cupidbuild_linux_mode(information) & CUPIDBUILD_LINUX_S_IFMT) !=
            CUPIDBUILD_LINUX_S_IFDIR) {
        return 0;
    }
    snapshot->present = 1;
    snapshot->identity[0] = cupidbuild_linux_u32(information);
    snapshot->identity[1] = cupidbuild_linux_u32(information + 4u);
    snapshot->identity[2] = cupidbuild_linux_u32(information + 88u);
    snapshot->identity[3] = cupidbuild_linux_u32(information + 92u);
    snapshot->modified[0] = cupidbuild_linux_u32(information + 72u);
    snapshot->modified[1] = cupidbuild_linux_u32(information + 76u);
    snapshot->changed[0] = cupidbuild_linux_u32(information + 80u);
    snapshot->changed[1] = cupidbuild_linux_u32(information + 84u);
    return 1;
}

static int cupidbuild_linux_information_identity_equal(const unsigned char left[96],
                                                       const unsigned char right[96]) {
    return cupidbuild_linux_u32(left) == cupidbuild_linux_u32(right) &&
           cupidbuild_linux_u32(left + 4u) == cupidbuild_linux_u32(right + 4u) &&
           cupidbuild_linux_u32(left + 88u) == cupidbuild_linux_u32(right + 88u) &&
           cupidbuild_linux_u32(left + 92u) == cupidbuild_linux_u32(right + 92u);
}

static int cupidbuild_linux_open_relative(int parent, const char *name, int directory) {
    unsigned int flags = CUPIDBUILD_LINUX_O_NOFOLLOW | CUPIDBUILD_LINUX_O_CLOEXEC;
    if (directory != 0) {
        flags |= CUPIDBUILD_LINUX_O_DIRECTORY;
    } else {
        flags |= CUPIDBUILD_LINUX_O_NONBLOCK | CUPIDBUILD_LINUX_O_LARGEFILE;
    }
    return cupid_linux_syscall4(CUPIDBUILD_LINUX_SYS_OPENAT, (unsigned int)parent,
                                (unsigned int)name, flags, 0u);
}

static int cupidbuild_linux_open_relative_path(int root, const char *logical, int directory) {
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
                    (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)current);
                }
                return -1;
            }
            (void)memcpy(component, logical + start, component_size);
            component[component_size] = '\0';
            next = cupidbuild_linux_open_relative(current, component, last != 0 ? directory : 1);
            if (owns_current != 0) {
                (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)current);
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

static int cupidbuild_linux_open_repository(const char *path) {
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
    current = cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_OPEN, (unsigned int)anchor,
                                   CUPIDBUILD_LINUX_O_DIRECTORY | CUPIDBUILD_LINUX_O_NOFOLLOW |
                                       CUPIDBUILD_LINUX_O_CLOEXEC,
                                   0u);
    if (current < 0 || (strcmp(path, "/") == 0) || (strcmp(path, ".") == 0)) {
        return current;
    }
    for (index = start; index <= size; index++) {
        if (path[index] == '/' || path[index] == '\0') {
            size_t component_size = index - start;
            if (component_size == 0u || component_size >= sizeof(component)) {
                (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)current);
                return -1;
            }
            (void)memcpy(component, path + start, component_size);
            component[component_size] = '\0';
            if ((component_size == 1u && component[0] == '.') ||
                (component_size == 2u && component[0] == '.' && component[1] == '.')) {
                (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)current);
                return -1;
            }
            next = cupidbuild_linux_open_relative(current, component, 1);
            (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)current);
            if (next < 0) {
                return -1;
            }
            current = next;
            start = index + 1u;
        }
    }
    return current;
}

static int cupidbuild_linux_read_repository_regular(int repository, const char *logical,
                                                    unsigned long long limit,
                                                    cupidbuild_host_snapshot_t *snapshot,
                                                    unsigned char **bytes_out) {
    unsigned char information[96];
    int descriptor = cupidbuild_linux_open_relative_path(repository, logical, 0);
    unsigned long long size;
    unsigned char *bytes = (unsigned char *)0;
    unsigned char digest[32];
    (void)memset(snapshot, 0, sizeof(*snapshot));
    if (descriptor < 0 ||
        cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_FSTAT64, (unsigned int)descriptor,
                             (unsigned int)information) < 0 ||
        (cupidbuild_linux_mode(information) & CUPIDBUILD_LINUX_S_IFMT) !=
            CUPIDBUILD_LINUX_S_IFREG) {
        if (descriptor >= 0) {
            (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)descriptor);
        }
        return 0;
    }
    size = ((unsigned long long)cupidbuild_linux_u32(information + 48u) << 32u) |
           cupidbuild_linux_u32(information + 44u);
    if (size > limit) {
        (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)descriptor);
        return 0;
    }
    if (!cupidbuild_host_read_open_contents(
            descriptor, size,
            bytes_out != (unsigned char **)0 ? &bytes : (unsigned char **)0, digest)) {
        (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)descriptor);
        return 0;
    }
    if (cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)descriptor) < 0) {
        free(bytes);
        return 0;
    }
    snapshot->present = 1;
    snapshot->size = size;
    snapshot->identity[0] = cupidbuild_linux_u32(information);
    snapshot->identity[1] = cupidbuild_linux_u32(information + 4u);
    snapshot->identity[2] = cupidbuild_linux_u32(information + 88u);
    snapshot->identity[3] = cupidbuild_linux_u32(information + 92u);
    snapshot->modified[0] = cupidbuild_linux_u32(information + 72u);
    snapshot->modified[1] = cupidbuild_linux_u32(information + 76u);
    (void)memcpy(snapshot->sha256, digest, sizeof(digest));
    if (bytes_out != (unsigned char **)0) {
        *bytes_out = bytes;
    } else {
        free(bytes);
    }
    return 1;
}

static int cupidbuild_linux_read_relative_regular(int parent, const char *name, int optional,
                                                  unsigned long long limit,
                                                  cupidbuild_host_snapshot_t *snapshot,
                                                  unsigned char **bytes_out) {
    unsigned char information[96];
    int descriptor = cupidbuild_linux_open_relative(parent, name, 0);
    unsigned long long size;
    unsigned char *bytes = (unsigned char *)0;
    unsigned char digest[32];
    (void)memset(snapshot, 0, sizeof(*snapshot));
    if (descriptor < 0) {
        if (optional != 0 && descriptor == -CUPIDBUILD_LINUX_ENOENT) {
            if (bytes_out != (unsigned char **)0) {
                *bytes_out = (unsigned char *)0;
            }
            return 1;
        }
        return 0;
    }
    if (cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_FSTAT64, (unsigned int)descriptor,
                             (unsigned int)information) < 0 ||
        (cupidbuild_linux_mode(information) & CUPIDBUILD_LINUX_S_IFMT) !=
            CUPIDBUILD_LINUX_S_IFREG) {
        (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)descriptor);
        return 0;
    }
    size = ((unsigned long long)cupidbuild_linux_u32(information + 48u) << 32u) |
           cupidbuild_linux_u32(information + 44u);
    if (size > limit) {
        (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)descriptor);
        return 0;
    }
    if (!cupidbuild_host_read_open_contents(
            descriptor, size,
            bytes_out != (unsigned char **)0 ? &bytes : (unsigned char **)0, digest)) {
        (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)descriptor);
        return 0;
    }
    if (cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)descriptor) < 0) {
        free(bytes);
        return 0;
    }
    snapshot->present = 1;
    snapshot->size = size;
    snapshot->identity[0] = cupidbuild_linux_u32(information);
    snapshot->identity[1] = cupidbuild_linux_u32(information + 4u);
    snapshot->identity[2] = cupidbuild_linux_u32(information + 88u);
    snapshot->identity[3] = cupidbuild_linux_u32(information + 92u);
    snapshot->modified[0] = cupidbuild_linux_u32(information + 72u);
    snapshot->modified[1] = cupidbuild_linux_u32(information + 76u);
    (void)memcpy(snapshot->sha256, digest, sizeof(digest));
    if (bytes_out != (unsigned char **)0) {
        *bytes_out = bytes;
    } else {
        free(bytes);
    }
    return 1;
}

static int cupidbuild_host_path_has_link(const char *path) {
    char prefix[CUPIDBUILD_HOST_PATH_BYTES];
    unsigned char information[96];
    size_t index;
    size_t size = strlen(path);
    if (size + 1u > sizeof(prefix)) {
        return 1;
    }
    (void)memcpy(prefix, path, size + 1u);
    for (index = 1u; index <= size; index++) {
        if (prefix[index] == '/' || prefix[index] == '\0') {
            char saved = prefix[index];
            prefix[index] = '\0';
            if (cupidbuild_linux_stat(prefix, information) < 0 ||
                (cupidbuild_linux_mode(information) & CUPIDBUILD_LINUX_S_IFMT) ==
                    CUPIDBUILD_LINUX_S_IFLNK) {
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
    unsigned char information[96];
    int descriptor;
    unsigned int size;
    unsigned char *bytes = (unsigned char *)0;
    unsigned char digest[32];
    int stat_result = cupidbuild_linux_stat(path, information);
    (void)memset(snapshot, 0, sizeof(*snapshot));
    if (optional != 0 && stat_result == -CUPIDBUILD_LINUX_ENOENT) {
        snapshot->present = 0;
        if (bytes_out != (unsigned char **)0) {
            *bytes_out = (unsigned char *)0;
        }
        return 1;
    }
    if (stat_result < 0 || cupidbuild_host_path_has_link(path)) {
        return 0;
    }
    descriptor = cupid_linux_syscall3(
        CUPIDBUILD_LINUX_SYS_OPEN, (unsigned int)path,
        CUPIDBUILD_LINUX_O_NOFOLLOW | CUPIDBUILD_LINUX_O_NONBLOCK | CUPIDBUILD_LINUX_O_CLOEXEC, 0u);
    if (descriptor < 0 ||
        cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_FSTAT64, (unsigned int)descriptor,
                             (unsigned int)information) < 0 ||
        (cupidbuild_linux_mode(information) & CUPIDBUILD_LINUX_S_IFMT) !=
            CUPIDBUILD_LINUX_S_IFREG ||
        cupidbuild_linux_u32(information + 48u) != 0u) {
        if (descriptor >= 0) {
            (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)descriptor);
        }
        return 0;
    }
    size = cupidbuild_linux_u32(information + 44u);
    if (size > CUPIDBUILD_HOST_FILE_LIMIT) {
        (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)descriptor);
        return 0;
    }
    if (!cupidbuild_host_read_open_contents(
            descriptor, (size_t)size,
            bytes_out != (unsigned char **)0 ? &bytes : (unsigned char **)0, digest)) {
        (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)descriptor);
        return 0;
    }
    (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)descriptor);
    snapshot->present = 1;
    snapshot->size = size;
    snapshot->identity[0] = cupidbuild_linux_u32(information);
    snapshot->identity[1] = cupidbuild_linux_u32(information + 4u);
    snapshot->identity[2] = cupidbuild_linux_u32(information + 88u);
    snapshot->identity[3] = cupidbuild_linux_u32(information + 92u);
    snapshot->modified[0] = cupidbuild_linux_u32(information + 72u);
    snapshot->modified[1] = cupidbuild_linux_u32(information + 76u);
    (void)memcpy(snapshot->sha256, digest, sizeof(digest));
    if (bytes_out != (unsigned char **)0) {
        *bytes_out = bytes;
    } else {
        free(bytes);
    }
    return 1;
}

static int cupidbuild_host_directory_snapshot(const char *path,
                                              cupidbuild_host_snapshot_t *snapshot) {
    unsigned char information[96];
    (void)memset(snapshot, 0, sizeof(*snapshot));
    if (cupidbuild_host_path_has_link(path) || cupidbuild_linux_stat(path, information) < 0 ||
        (cupidbuild_linux_mode(information) & CUPIDBUILD_LINUX_S_IFMT) !=
            CUPIDBUILD_LINUX_S_IFDIR) {
        return 0;
    }
    snapshot->present = 1;
    snapshot->identity[0] = cupidbuild_linux_u32(information);
    snapshot->identity[1] = cupidbuild_linux_u32(information + 4u);
    snapshot->identity[2] = cupidbuild_linux_u32(information + 88u);
    snapshot->identity[3] = cupidbuild_linux_u32(information + 92u);
    snapshot->modified[0] = cupidbuild_linux_u32(information + 72u);
    snapshot->modified[1] = cupidbuild_linux_u32(information + 76u);
    snapshot->changed[0] = cupidbuild_linux_u32(information + 80u);
    snapshot->changed[1] = cupidbuild_linux_u32(information + 84u);
    return 1;
}

static int cupidbuild_host_seed_members_platform(const char *directory, const char *suffix,
                                                 const char *const *expected,
                                                 size_t expected_count) {
    unsigned char entries[CUPIDBUILD_HOST_DIRECTORY_BYTES];
    int descriptor =
        cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_OPEN, (unsigned int)directory,
                             CUPIDBUILD_LINUX_O_DIRECTORY | CUPIDBUILD_LINUX_O_NOFOLLOW, 0u);
    int valid = descriptor >= 0;
    while (valid != 0) {
        int count = cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_GETDENTS64, (unsigned int)descriptor,
                                         (unsigned int)entries, (unsigned int)sizeof(entries));
        size_t offset = 0u;
        if (count == 0) {
            break;
        }
        if (count < 0) {
            valid = 0;
            break;
        }
        while (offset < (size_t)count) {
            unsigned int record_size;
            const char *name;
            size_t name_capacity;
            size_t name_size = 0u;
            if ((size_t)count - offset < 20u) {
                valid = 0;
                break;
            }
            record_size = cupidbuild_linux_u16(entries + offset + 16u);
            if (record_size < 20u || (size_t)record_size > (size_t)count - offset) {
                valid = 0;
                break;
            }
            name = (const char *)(entries + offset + 19u);
            name_capacity = (size_t)record_size - 19u;
            while (name_size < name_capacity && name[name_size] != '\0') {
                name_size++;
            }
            if (name_size == name_capacity) {
                valid = 0;
                break;
            }
            if (cupidbuild_host_name_has_suffix(name, suffix) &&
                !cupidbuild_host_name_is_expected(name, expected, expected_count)) {
                valid = 0;
                break;
            }
            offset += (size_t)record_size;
        }
    }
    if (descriptor >= 0 &&
        cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)descriptor) < 0) {
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
    if (!cupidbuild_linux_descriptor_snapshot(repository, &repository_snapshot) ||
        !cupidbuild_host_snapshot_identity_equal(&transaction->repository_root_snapshot,
                                                 &repository_snapshot)) {
        valid = 0;
    } else {
        root = cupidbuild_linux_open_relative_path(repository, logical_root, 1);
        if (!cupidbuild_linux_descriptor_snapshot(root, &root_snapshot) ||
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
        (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)root);
    }
    while (valid != 0 && worklist.count != 0u) {
        unsigned char entries[CUPIDBUILD_HOST_DIRECTORY_BYTES];
        cupidbuild_host_discovery_directory_t current = worklist.directories[worklist.count - 1u];
        cupidbuild_host_snapshot_t completed_snapshot;
        int retained = 0;
        worklist.count--;
        while (valid != 0) {
            int count = cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_GETDENTS64,
                                             (unsigned int)current.descriptor,
                                             (unsigned int)entries, (unsigned int)sizeof(entries));
            size_t offset = 0u;
            if (count == 0) {
                break;
            }
            if (count < 0) {
                valid = 0;
                break;
            }
            while (offset < (size_t)count) {
                unsigned int record_size;
                const char *name;
                size_t name_capacity;
                size_t name_size = 0u;
                char logical[CUPIDBUILD_HOST_PATH_BYTES];
                unsigned char information[96];
                unsigned int mode;
                int matches;
                if ((size_t)count - offset < 20u) {
                    valid = 0;
                    break;
                }
                record_size = cupidbuild_linux_u16(entries + offset + 16u);
                if (record_size < 20u || (size_t)record_size > (size_t)count - offset) {
                    valid = 0;
                    break;
                }
                name = (const char *)(entries + offset + 19u);
                name_capacity = (size_t)record_size - 19u;
                while (name_size < name_capacity && name[name_size] != '\0') {
                    name_size++;
                }
                if (name_size == name_capacity) {
                    valid = 0;
                    break;
                }
                offset += (size_t)record_size;
                if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
                    continue;
                }
                if (!cupidbuild_host_join(logical, sizeof(logical), current.logical, name)) {
                    valid = 0;
                    break;
                }
                {
                    int stat_result = cupid_linux_syscall4(
                        CUPIDBUILD_LINUX_SYS_FSTATAT64, (unsigned int)current.descriptor,
                        (unsigned int)name, (unsigned int)information,
                        CUPIDBUILD_LINUX_AT_SYMLINK_NOFOLLOW);
                    if (stat_result < 0) {
                        if (transaction->compile_discovery != 0 &&
                            stat_result == -CUPIDBUILD_LINUX_ENOENT &&
                            !cupidbuild_host_discovery_suffix(name, suffixes, suffix_count)) {
                            continue;
                        }
                        valid = 0;
                        break;
                    }
                }
                mode = cupidbuild_linux_mode(information) & CUPIDBUILD_LINUX_S_IFMT;
                if (name[0] == '.' &&
                    (mode == CUPIDBUILD_LINUX_S_IFDIR || skip_hidden_files != 0)) {
                    continue;
                }
                if (mode == CUPIDBUILD_LINUX_S_IFLNK) {
                    valid = 0;
                    break;
                }
                matches = cupidbuild_host_discovery_suffix(name, suffixes, suffix_count);
                if (mode == CUPIDBUILD_LINUX_S_IFDIR && matches != 0 &&
                    reject_matching_nonfiles != 0) {
                    valid = 0;
                    break;
                }
                if (mode == CUPIDBUILD_LINUX_S_IFDIR) {
                    unsigned char opened_information[96];
                    cupidbuild_host_snapshot_t child_snapshot;
                    int child = cupidbuild_linux_open_relative(current.descriptor, name, 1);
                    size_t prior;
                    int alias = 0;
                    if (child < 0 ||
                        cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_FSTAT64, (unsigned int)child,
                                             (unsigned int)opened_information) < 0 ||
                        !cupidbuild_linux_information_identity_equal(information,
                                                                     opened_information) ||
                        !cupidbuild_linux_descriptor_snapshot(child, &child_snapshot)) {
                        if (child >= 0) {
                            (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                                                       (unsigned int)child);
                        }
                        valid = 0;
                        break;
                    }
                    for (prior = 0u; prior < visited_count; prior++) {
                        if (cupidbuild_host_snapshot_identity_equal(&visited[prior],
                                                                    &child_snapshot)) {
                            alias = 1;
                            break;
                        }
                    }
                    if (alias != 0 || directory_count >= CUPIDBUILD_HOST_DISCOVERY_DIRECTORIES ||
                        !cupidbuild_host_discovery_worklist_add(&worklist, logical, &child_snapshot,
                                                                child)) {
                        (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)child);
                        valid = 0;
                        break;
                    }
                    visited[visited_count++] = child_snapshot;
                    directory_count++;
                } else if (mode == CUPIDBUILD_LINUX_S_IFREG && matches != 0) {
                    unsigned char opened_information[96];
                    cupidbuild_host_snapshot_t child_snapshot;
                    int child = cupidbuild_linux_open_relative(current.descriptor, name, 0);
                    if (child < 0 ||
                        cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_FSTAT64, (unsigned int)child,
                                             (unsigned int)opened_information) < 0 ||
                        (cupidbuild_linux_mode(opened_information) & CUPIDBUILD_LINUX_S_IFMT) !=
                            CUPIDBUILD_LINUX_S_IFREG ||
                        !cupidbuild_linux_information_identity_equal(information,
                                                                     opened_information) ||
                        !cupidbuild_host_read_open_file(child, CUPIDBUILD_HOST_FILE_LIMIT,
                                                        &child_snapshot, (unsigned char **)0) ||
                        !cupidbuild_host_discovery_add(paths, logical, &child_snapshot)) {
                        if (child >= 0) {
                            (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                                                       (unsigned int)child);
                        }
                        valid = 0;
                        break;
                    }
                    if (cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)child) < 0) {
                        valid = 0;
                        break;
                    }
                } else if (matches != 0 && reject_matching_nonfiles != 0) {
                    valid = 0;
                    break;
                }
            }
        }
        if (valid != 0 &&
            (!cupidbuild_linux_descriptor_snapshot(current.descriptor, &completed_snapshot) ||
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
        if (current.descriptor >= 0 && cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                                                            (unsigned int)current.descriptor) < 0) {
            valid = 0;
        }
        free(current.logical);
    }
    while (worklist.count != 0u) {
        cupidbuild_host_discovery_directory_t *directory = &worklist.directories[--worklist.count];
        (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)directory->descriptor);
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
    unsigned char entries[CUPIDBUILD_HOST_DIRECTORY_BYTES];
    cupidbuild_host_snapshot_t root_snapshot;
    int descriptor;
    int valid;
    if (!cupidbuild_linux_descriptor_snapshot(transaction->repository_root_descriptor,
                                              &root_snapshot) ||
        !cupidbuild_host_snapshot_identity_equal(&transaction->repository_root_snapshot,
                                                 &root_snapshot)) {
        return 0;
    }
    descriptor = cupidbuild_linux_open_relative_path(transaction->repository_root_descriptor,
                                                     logical_directory, 1);
    valid = descriptor >= 0;
    while (valid != 0) {
        int count = cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_GETDENTS64, (unsigned int)descriptor,
                                         (unsigned int)entries, (unsigned int)sizeof(entries));
        size_t offset = 0u;
        if (count == 0) {
            break;
        }
        if (count < 0) {
            valid = 0;
            break;
        }
        while (offset < (size_t)count) {
            unsigned int record_size;
            const char *name;
            size_t name_capacity;
            size_t name_size = 0u;
            if ((size_t)count - offset < 20u) {
                valid = 0;
                break;
            }
            record_size = cupidbuild_linux_u16(entries + offset + 16u);
            if (record_size < 20u || (size_t)record_size > (size_t)count - offset) {
                valid = 0;
                break;
            }
            name = (const char *)(entries + offset + 19u);
            name_capacity = (size_t)record_size - 19u;
            while (name_size < name_capacity && name[name_size] != '\0') {
                name_size++;
            }
            if (name_size == name_capacity) {
                valid = 0;
                break;
            }
            if (cupidbuild_host_name_has_suffix(name, suffix) &&
                !cupidbuild_host_name_is_expected(name, expected, expected_count)) {
                valid = 0;
                break;
            }
            offset += (size_t)record_size;
        }
    }
    if (descriptor >= 0 &&
        cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)descriptor) < 0) {
        valid = 0;
    }
    return valid;
}

static int cupidbuild_host_write_anonymous(const char *name, const unsigned char *bytes,
                                           size_t size, char *path, size_t path_capacity,
                                           cupidbuild_host_snapshot_t *snapshot,
                                           int *descriptor_out) {
    unsigned char information[96];
    size_t offset = 0u;
    int descriptor =
        cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_MEMFD_CREATE, (unsigned int)name,
                             CUPIDBUILD_LINUX_MFD_CLOEXEC | CUPIDBUILD_LINUX_MFD_ALLOW_SEALING);
    if (descriptor < 0) {
        return 0;
    }
    while (offset < size) {
        int count =
            cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_WRITE, (unsigned int)descriptor,
                                 (unsigned int)(bytes + offset), (unsigned int)(size - offset));
        if (count <= 0) {
            (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)descriptor);
            return 0;
        }
        offset += (size_t)count;
    }
    if (cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_FSYNC, (unsigned int)descriptor) < 0 ||
        cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_FSTAT64, (unsigned int)descriptor,
                             (unsigned int)information) < 0 ||
        cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_FCNTL64, (unsigned int)descriptor,
                             CUPIDBUILD_LINUX_F_ADD_SEALS, CUPIDBUILD_LINUX_F_SEAL_ALL) < 0) {
        (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)descriptor);
        return 0;
    }
    if (!cupidbuild_host_copy_text(path, path_capacity, name)) {
        (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)descriptor);
        return 0;
    }
    (void)memset(snapshot, 0, sizeof(*snapshot));
    snapshot->present = 1;
    snapshot->size = size;
    snapshot->identity[0] = cupidbuild_linux_u32(information);
    snapshot->identity[1] = cupidbuild_linux_u32(information + 4u);
    snapshot->identity[2] = cupidbuild_linux_u32(information + 88u);
    snapshot->identity[3] = cupidbuild_linux_u32(information + 92u);
    snapshot->modified[0] = cupidbuild_linux_u32(information + 72u);
    snapshot->modified[1] = cupidbuild_linux_u32(information + 76u);
    cupidbuild_sha256(bytes, size, snapshot->sha256);
    *descriptor_out = descriptor;
    return 1;
}

static int cupidbuild_host_open_anonymous(const char *name, char *path, size_t path_capacity,
                                          int *descriptor_out) {
    int descriptor =
        cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_MEMFD_CREATE, (unsigned int)name,
                             CUPIDBUILD_LINUX_MFD_CLOEXEC | CUPIDBUILD_LINUX_MFD_ALLOW_SEALING);
    if (descriptor < 0 || !cupidbuild_host_copy_text(path, path_capacity, name)) {
        if (descriptor >= 0) {
            (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)descriptor);
        }
        return 0;
    }
    *descriptor_out = descriptor;
    return 1;
}

static int cupidbuild_host_seal_anonymous(int descriptor) {
    return cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_FCNTL64, (unsigned int)descriptor,
                                CUPIDBUILD_LINUX_F_ADD_SEALS, CUPIDBUILD_LINUX_F_SEAL_ALL) == 0;
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
    if (cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_GETCWD, (unsigned int)current,
                             (unsigned int)sizeof(current)) < 0) {
        return 0;
    }
    return cupidbuild_host_join(destination, capacity, current, path);
}

static int cupidbuild_host_make_executable(const char *path) {
    return cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_CHMOD, (unsigned int)path, 0700u) == 0;
}

static void cupidbuild_host_delete_file(const char *path) {
    (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_UNLINK, (unsigned int)path);
}

static unsigned int cupidbuild_host_process_id(void) {
    return (unsigned int)cupid_linux_syscall0(CUPIDBUILD_LINUX_SYS_GETPID);
}

static int cupidbuild_host_process_alive(unsigned int process_id) {
    int result = cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_KILL, process_id, 0u);
    return result == 0 || result == -CUPIDBUILD_LINUX_EPERM;
}

static int cupidbuild_host_linux_open_snapshot(int descriptor,
                                               cupidbuild_host_snapshot_t *snapshot) {
    unsigned char information[96];
    if (snapshot == (cupidbuild_host_snapshot_t *)0) {
        return 1;
    }
    (void)memset(snapshot, 0, sizeof(*snapshot));
    if (cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_FSTAT64, (unsigned int)descriptor,
                             (unsigned int)information) < 0 ||
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
}

static int cupidbuild_host_read_open_file(int descriptor, unsigned long long limit,
                                          cupidbuild_host_snapshot_t *snapshot,
                                          unsigned char **bytes_out) {
    cupidbuild_host_snapshot_t current;
    unsigned char *bytes = (unsigned char *)0;
    unsigned char digest[32];
    if (!cupidbuild_host_linux_open_snapshot(descriptor, &current) || current.size > limit) {
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

static void cupidbuild_host_write_launch_failure(int descriptor) {
    unsigned char marker = 1u;
    int result;
    do {
        result = cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_WRITE, (unsigned int)descriptor,
                                      (unsigned int)&marker, 1u);
    } while (result == -CUPIDBUILD_LINUX_EINTR);
}

static int cupidbuild_host_read_launch_status(int descriptor) {
    unsigned char marker = 0u;
    int result;
    do {
        result = cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_READ, (unsigned int)descriptor,
                                      (unsigned int)&marker, 1u);
    } while (result == -CUPIDBUILD_LINUX_EINTR);
    if (result == 0) {
        return 0;
    }
    if (result == 1 && marker == 1u) {
        return 1;
    }
    return -1;
}

static int cupidbuild_host_linux_duplicate_above_standard(int descriptor) {
    int result;
    if (descriptor > 2) {
        return descriptor;
    }
    do {
        result = cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_FCNTL64, (unsigned int)descriptor,
                                      CUPIDBUILD_LINUX_F_DUPFD, 3u);
    } while (result == -CUPIDBUILD_LINUX_EINTR);
    return result;
}

static int cupidbuild_host_linux_promote_above_standard(int *descriptor) {
    int original;
    int duplicated;
    if (descriptor == (int *)0 || *descriptor < 0) {
        return 0;
    }
    if (*descriptor > 2) {
        return 1;
    }
    original = *descriptor;
    duplicated = cupidbuild_host_linux_duplicate_above_standard(original);
    if (duplicated <= 2) {
        (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)original);
        *descriptor = -1;
        return 0;
    }
    (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)original);
    *descriptor = duplicated;
    return 1;
}

static int cupidbuild_host_linux_duplicate_stream(int source, unsigned int target) {
    int result;
    if (source == (int)target) {
        return 1;
    }
    do {
        result = cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_DUP2, (unsigned int)source, target);
    } while (result == -CUPIDBUILD_LINUX_EINTR || result == -CUPIDBUILD_LINUX_EBUSY);
    return result == (int)target;
}

static int cupidbuild_host_run_process(
    const char *tool, const cupidbuild_host_snapshot_t *expected_tool, int tool_descriptor,
    const char *const *arguments, const char *stdout_path, const char *stderr_path,
    int stdout_descriptor, int stderr_descriptor, const char *working_directory,
    int working_descriptor, const cupidbuild_host_transaction_t *inherited_transaction,
    cupidbuild_host_snapshot_t *stdout_opened, cupidbuild_host_snapshot_t *stderr_opened,
    unsigned int timeout_milliseconds) {
    const char **argv;
    size_t argument_count = 0u;
    size_t index;
    int child;
    int own_stream_descriptors = stdout_descriptor < 0;
    int own_tool_descriptor = tool_descriptor < 0;
    int launch_pipe[2] = {-1, -1};
    unsigned int elapsed = 0u;
    int status = 0;
    int wait_result;
    cupidbuild_linux_time_t pause_time;
    if (own_stream_descriptors == 0 && (stdout_descriptor <= 2 || stderr_descriptor <= 2 ||
                                        stdout_descriptor == stderr_descriptor)) {
        return -1;
    }
    while (arguments[argument_count] != (const char *)0) {
        if (argument_count == (size_t)-2) {
            return -1;
        }
        argument_count++;
    }
    argv = (const char **)calloc(argument_count + 2u, sizeof(*argv));
    if (argv == (const char **)0) {
        return -1;
    }
    argv[0] = tool;
    for (index = 0u; index < argument_count; index++) {
        argv[index + 1u] = arguments[index];
    }
    if (own_tool_descriptor != 0) {
        tool_descriptor = cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_OPEN, (unsigned int)tool,
                                               CUPIDBUILD_LINUX_O_NOFOLLOW, 0u);
    }
    {
        cupidbuild_host_snapshot_t tool_snapshot;
        int valid = tool_descriptor >= 0 &&
                    cupidbuild_host_read_open_file(tool_descriptor, CUPIDBUILD_HOST_FILE_LIMIT,
                                                   &tool_snapshot, (unsigned char **)0) &&
                    cupidbuild_host_snapshot_equal(&tool_snapshot, expected_tool);
        if (valid == 0) {
            if (tool_descriptor >= 0 && own_tool_descriptor != 0) {
                (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE,
                                           (unsigned int)tool_descriptor);
            }
            free(argv);
            return -1;
        }
    }
    if (own_stream_descriptors != 0) {
        stdout_descriptor =
            cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_OPEN, (unsigned int)stdout_path,
                                 CUPIDBUILD_LINUX_O_WRONLY | CUPIDBUILD_LINUX_O_CREAT |
                                     CUPIDBUILD_LINUX_O_EXCL | CUPIDBUILD_LINUX_O_NOFOLLOW,
                                 0600u);
        stderr_descriptor =
            cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_OPEN, (unsigned int)stderr_path,
                                 CUPIDBUILD_LINUX_O_WRONLY | CUPIDBUILD_LINUX_O_CREAT |
                                     CUPIDBUILD_LINUX_O_EXCL | CUPIDBUILD_LINUX_O_NOFOLLOW,
                                 0600u);
    }
    if (stdout_descriptor < 0 || stderr_descriptor < 0 ||
        !cupidbuild_host_linux_open_snapshot(stdout_descriptor, stdout_opened) ||
        !cupidbuild_host_linux_open_snapshot(stderr_descriptor, stderr_opened)) {
        if (stdout_descriptor >= 0 && own_stream_descriptors != 0) {
            (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)stdout_descriptor);
        }
        if (stderr_descriptor >= 0 && own_stream_descriptors != 0) {
            (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)stderr_descriptor);
        }
        if (own_tool_descriptor != 0) {
            (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)tool_descriptor);
        }
        free(argv);
        return -1;
    }
    if (cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_PIPE, (unsigned int)launch_pipe) != 0 ||
        !cupidbuild_host_linux_promote_above_standard(&launch_pipe[0]) ||
        !cupidbuild_host_linux_promote_above_standard(&launch_pipe[1]) ||
        cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_FCNTL64, (unsigned int)launch_pipe[1],
                             CUPIDBUILD_LINUX_F_SETFD, CUPIDBUILD_LINUX_FD_CLOEXEC) != 0) {
        if (launch_pipe[0] >= 0) {
            (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)launch_pipe[0]);
        }
        if (launch_pipe[1] >= 0) {
            (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)launch_pipe[1]);
        }
        if (own_stream_descriptors != 0) {
            (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)stdout_descriptor);
            (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)stderr_descriptor);
        }
        if (own_tool_descriptor != 0) {
            (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)tool_descriptor);
        }
        free(argv);
        return -1;
    }
    child = cupid_linux_syscall0(CUPIDBUILD_LINUX_SYS_FORK);
    if (child == 0) {
        int exec_tool_descriptor;
        unsigned int inherited_index;
        (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)launch_pipe[0]);
        if ((inherited_transaction != (const cupidbuild_host_transaction_t *)0 &&
             inherited_transaction->private_flat != 0 &&
             ((inherited_transaction->candidate_descriptor >= 0 &&
               cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_FCNTL64,
                                    (unsigned int)inherited_transaction->candidate_descriptor,
                                    CUPIDBUILD_LINUX_F_SETFD, 0u) != 0) ||
              (inherited_transaction->private_output_descriptor >= 0 &&
               cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_FCNTL64,
                                    (unsigned int)inherited_transaction->private_output_descriptor,
                                    CUPIDBUILD_LINUX_F_SETFD, 0u) != 0))) ||
            (working_descriptor >= 0 &&
             cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_FCHDIR, (unsigned int)working_descriptor) !=
                 0) ||
            (working_descriptor < 0 && working_directory != (const char *)0 &&
             cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CHDIR, (unsigned int)working_directory) !=
                 0)) {
            cupidbuild_host_write_launch_failure(launch_pipe[1]);
            (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_EXIT, 125u);
            return 125;
        }
        if (inherited_transaction != (const cupidbuild_host_transaction_t *)0 &&
            inherited_transaction->private_flat != 0) {
            for (inherited_index = 0u; inherited_index < inherited_transaction->input_count;
                 inherited_index++) {
                int descriptor = inherited_transaction->inputs[inherited_index].frozen_descriptor;
                if (descriptor >= 0 &&
                    cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_FCNTL64, (unsigned int)descriptor,
                                         CUPIDBUILD_LINUX_F_SETFD, 0u) != 0) {
                    cupidbuild_host_write_launch_failure(launch_pipe[1]);
                    (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_EXIT, 125u);
                    return 125;
                }
            }
        }
        exec_tool_descriptor = cupidbuild_host_linux_duplicate_above_standard(tool_descriptor);
        if (exec_tool_descriptor < 0 ||
            !cupidbuild_host_linux_duplicate_stream(stdout_descriptor, 1u) ||
            !cupidbuild_host_linux_duplicate_stream(stderr_descriptor, 2u)) {
            cupidbuild_host_write_launch_failure(launch_pipe[1]);
            (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_EXIT, 125u);
            return 125;
        }
        if (stdout_descriptor > 2) {
            (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)stdout_descriptor);
        }
        if (stderr_descriptor > 2 && stderr_descriptor != stdout_descriptor) {
            (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)stderr_descriptor);
        }
        (void)cupid_linux_syscall5(CUPIDBUILD_LINUX_SYS_EXECVEAT,
                                   (unsigned int)exec_tool_descriptor, (unsigned int)"",
                                   (unsigned int)argv, 0u, CUPIDBUILD_LINUX_AT_EMPTY_PATH);
        cupidbuild_host_write_launch_failure(launch_pipe[1]);
        (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_EXIT, 125u);
        return 125;
    }
    if (own_stream_descriptors != 0) {
        (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)stdout_descriptor);
        (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)stderr_descriptor);
    }
    if (own_tool_descriptor != 0) {
        (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)tool_descriptor);
    }
    (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)launch_pipe[1]);
    if (child < 0) {
        (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)launch_pipe[0]);
        free(argv);
        return -1;
    }
    free(argv);
    pause_time.seconds = 0;
    pause_time.nanoseconds = 10000000;
    for (;;) {
        wait_result = cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_WAITPID, (unsigned int)child,
                                           (unsigned int)&status, CUPIDBUILD_LINUX_WNOHANG);
        if (wait_result > 0) {
            break;
        }
        if (wait_result == -CUPIDBUILD_LINUX_EINTR) {
            continue;
        }
        if (wait_result < 0) {
            (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)launch_pipe[0]);
            return -1;
        }
        if (elapsed >= timeout_milliseconds) {
            (void)cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_KILL, (unsigned int)child,
                                       CUPIDBUILD_LINUX_SIGKILL);
            do {
                wait_result = cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_WAITPID,
                                                   (unsigned int)child, (unsigned int)&status, 0u);
            } while (wait_result == -CUPIDBUILD_LINUX_EINTR);
            (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)launch_pipe[0]);
            return -2;
        }
        (void)cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_NANOSLEEP, (unsigned int)&pause_time, 0u);
        elapsed += 10u;
    }
    wait_result = cupidbuild_host_read_launch_status(launch_pipe[0]);
    (void)cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)launch_pipe[0]);
    if (wait_result != 0) {
        return -1;
    }
    if ((status & 0x7f) != 0) {
        return 125;
    }
    return (status >> 8) & 0xff;
}

static int cupidbuild_host_open_directory(const char *path) {
    return cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_OPEN, (unsigned int)path,
                                CUPIDBUILD_LINUX_O_DIRECTORY | CUPIDBUILD_LINUX_O_NOFOLLOW |
                                    CUPIDBUILD_LINUX_O_CLOEXEC,
                                0u);
}

static int cupidbuild_host_directory_descriptor_snapshot(int descriptor,
                                                         cupidbuild_host_snapshot_t *snapshot) {
    unsigned char information[96];
    (void)memset(snapshot, 0, sizeof(*snapshot));
    if (descriptor < 0 ||
        cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_FSTAT64, (unsigned int)descriptor,
                             (unsigned int)information) < 0 ||
        (cupidbuild_linux_mode(information) & CUPIDBUILD_LINUX_S_IFMT) !=
            CUPIDBUILD_LINUX_S_IFDIR) {
        return 0;
    }
    snapshot->present = 1;
    snapshot->identity[0] = cupidbuild_linux_u32(information);
    snapshot->identity[1] = cupidbuild_linux_u32(information + 4u);
    snapshot->identity[2] = cupidbuild_linux_u32(information + 88u);
    snapshot->identity[3] = cupidbuild_linux_u32(information + 92u);
    snapshot->modified[0] = cupidbuild_linux_u32(information + 72u);
    snapshot->modified[1] = cupidbuild_linux_u32(information + 76u);
    snapshot->changed[0] = cupidbuild_linux_u32(information + 80u);
    snapshot->changed[1] = cupidbuild_linux_u32(information + 84u);
    return 1;
}

static int cupidbuild_host_close_directory(int descriptor) {
    return descriptor < 0 ||
           cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)descriptor) == 0;
}

static int cupidbuild_host_atomic_replace(cupidbuild_host_transaction_t *transaction) {
    return cupidbuild_host_posix_atomic_replace(transaction);
}
