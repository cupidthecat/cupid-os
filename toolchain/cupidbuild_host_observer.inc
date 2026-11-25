/**
 * @file cupidbuild_host_observer.inc
 * @brief Retain observed source roots, parents and file streams.
 */

/* Observations retain originals, including every ancestor. They do not use the
 * runner's frozen files or request publication rights. */
#if defined(_WIN32)
typedef HANDLE cupidbuild_observer_handle_t;
#define CUPIDBUILD_OBSERVER_INVALID INVALID_HANDLE_VALUE
#else
        typedef int cupidbuild_observer_handle_t;
#define CUPIDBUILD_OBSERVER_INVALID (-1)
#endif
#define CUPIDBUILD_OBSERVER_ENTRIES 4096u
#define CUPIDBUILD_OBSERVER_NAME 1024u
#define CUPIDBUILD_OBSERVER_SEED_TOOLS 6u

typedef struct {
    uint64_t device;
    uint64_t identity;
    uint64_t size;
    uint64_t links;
    uint64_t modified;
    unsigned int nanoseconds;
} cupidbuild_observer_stat_t;

typedef struct {
    char *names[CUPIDBUILD_OBSERVER_SEED_TOOLS];
    unsigned int count;
} cupidbuild_observer_seed_members_t;

typedef struct cupidbuild_observer_entry {
    struct cupidbuild_observer_entry *next;
    struct cupidbuild_observer_entry *lookup_next;
    struct cupidbuild_observer_entry *parent;
    cupidbuild_observer_handle_t handle;
    cupidbuild_observer_stat_t captured;
    char *name;
    char **members;
    cupidbuild_observer_seed_members_t *seed_members;
    size_t member_count;
    int directory;
    int membership;
    int payload;
    int absent;
    unsigned char digest[32];
} cupidbuild_observer_entry_t;

struct cupidbuild_host_directory_snapshot {
    struct cupidbuild_host_directory_snapshot *next;
    char **names;
    size_t count;
    size_t capacity;
};

struct cupidbuild_host_observer {
    cupidbuild_observer_entry_t *entries;
    cupidbuild_observer_entry_t *entry_index[256];
    cupidbuild_observer_entry_t *root;
    size_t count;
    size_t member_count;
    cupidbuild_host_directory_snapshot_t *snapshots;
    size_t snapshot_count;
    size_t snapshot_name_count;
    cupidbuild_observation_issue_t last_issue;
    size_t entry_quota;
    int source_names;
    int selection_only;
    int stream_group_active;
    char *source_root_request;
    char *source_root_physical;
    char error[CUPIDBUILD_HOST_ERROR_BYTES];
};

static int cupidbuild_observer_source_root_binding(cupidbuild_host_observer_t *observer);

static int cupidbuild_observer_fail(cupidbuild_host_observer_t *observer, const char *message) {
    if (observer != (cupidbuild_host_observer_t *)0 && observer->error[0] == 0) {
        (void)cupidbuild_host_copy_text(observer->error, sizeof(observer->error), message);
    }
    return 0;
}

/* Decode strictly: overlong forms, surrogate scalars and incomplete sequences
 * cannot name an observation, even on filesystems accepting arbitrary bytes. */
static int cupidbuild_observer_scalar(const char **cursor, unsigned int *out) {
    const unsigned char *p = (const unsigned char *)*cursor;
    unsigned int value = *p++;
    unsigned int count = 0u;
    unsigned int minimum = 0u;
    if (value >= 0xc2u && value <= 0xdfu) {
        value &= 31u;
        count = 1u;
        minimum = 0x80u;
    } else if (value >= 0xe0u && value <= 0xefu) {
        value &= 15u;
        count = 2u;
        minimum = 0x800u;
    } else if (value >= 0xf0u && value <= 0xf4u) {
        value &= 7u;
        count = 3u;
        minimum = 0x10000u;
    } else if (value >= 0x80u || value == 0u) {
        return 0;
    }
    while (count != 0u) {
        if (*p < 0x80u || *p > 0xbfu)
            return 0;
        value = (value << 6u) | (*p++ & 63u);
        count--;
    }
    if (value < minimum || value > 0x10ffffu || (value >= 0xd800u && value <= 0xdfffu))
        return 0;
    *cursor = (const char *)p;
    *out = value;
    return 1;
}

static int cupidbuild_observer_name_valid(const char *name) {
    const char *cursor = name;
    unsigned int scalar;
    if (name == (const char *)0 || name[0] == 0 || strlen(name) >= CUPIDBUILD_OBSERVER_NAME ||
        strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return 0;
    while (*cursor != 0) {
        if (!cupidbuild_observer_scalar(&cursor, &scalar) || scalar == '/' || scalar == '\\' ||
            scalar == ':')
            return 0;
    }
    return 1;
}

/* Source inputs use the host's filename namespace. The ordinary build profile
 * keeps its portable names. POSIX colon and backslash bytes are filename
 * characters; slash, empty, dot, parent, malformed UTF-8 and byte limits still
 * reject. Windows retains the ordinary drive/UNC component policy. */
static int cupidbuild_observer_input_name_valid(const cupidbuild_host_observer_t *observer,
                                                const char *name) {
#if defined(_WIN32)
    (void)observer;
    return cupidbuild_observer_name_valid(name);
#else
            const char *cursor = name;
            unsigned int scalar;
            if (!observer || !observer->source_names)
                return cupidbuild_observer_name_valid(name);
            if (!name || !name[0] || strlen(name) >= CUPIDBUILD_OBSERVER_NAME ||
                strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
                return 0;
            while (*cursor)
                if (!cupidbuild_observer_scalar(&cursor, &scalar) || scalar == '/')
                    return 0;
            return 1;
#endif
}

static int cupidbuild_observer_close_handle(cupidbuild_observer_handle_t handle) {
#if defined(_WIN32)
    return CloseHandle(handle) != 0;
#elif defined(CUPIDBUILD_CUSTOM_LINUX)
            return cupid_linux_syscall1(CUPIDBUILD_LINUX_SYS_CLOSE, (unsigned int)handle) == 0;
#else
    return close(handle) == 0;
#endif
}

static int cupidbuild_observer_stat_kind(cupidbuild_observer_handle_t handle, int directory,
                                         cupidbuild_observer_stat_t *out, int *kind_out) {
    int kind;
    (void)memset(out, 0, sizeof(*out));
    if (kind_out)
        *kind_out = -1;
#if defined(_WIN32)
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(handle, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE)) != 0u)
        return 0;
    kind = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u ? 1 : 0;
    if (directory >= 0 && kind != (directory != 0))
        return 0;
    out->device = info.dwVolumeSerialNumber;
    out->identity = ((uint64_t)info.nFileIndexHigh << 32u) | info.nFileIndexLow;
    out->size = ((uint64_t)info.nFileSizeHigh << 32u) | info.nFileSizeLow;
    out->links = info.nNumberOfLinks;
    out->modified =
        ((uint64_t)info.ftLastWriteTime.dwHighDateTime << 32u) | info.ftLastWriteTime.dwLowDateTime;
#elif defined(CUPIDBUILD_CUSTOM_LINUX)
            unsigned char info[96];
            unsigned int mode;
            if (cupid_linux_syscall2(CUPIDBUILD_LINUX_SYS_FSTAT64, (unsigned int)handle,
                                     (unsigned int)info) < 0)
                return 0;
            mode = cupidbuild_linux_mode(info) & CUPIDBUILD_LINUX_S_IFMT;
            if (mode != CUPIDBUILD_LINUX_S_IFDIR && mode != CUPIDBUILD_LINUX_S_IFREG)
                return 0;
            kind = mode == CUPIDBUILD_LINUX_S_IFDIR ? 1 : 0;
            if (directory >= 0 && kind != (directory != 0))
                return 0;
            out->device =
                ((uint64_t)cupidbuild_linux_u32(info + 4u) << 32u) | cupidbuild_linux_u32(info);
            out->identity = ((uint64_t)cupidbuild_linux_u32(info + 92u) << 32u) |
                            cupidbuild_linux_u32(info + 88u);
            out->size = ((uint64_t)cupidbuild_linux_u32(info + 48u) << 32u) |
                        cupidbuild_linux_u32(info + 44u);
            out->links = cupidbuild_linux_u32(info + 20u);
            out->modified = cupidbuild_linux_u32(info + 72u);
            out->nanoseconds = cupidbuild_linux_u32(info + 76u);
#else
    struct stat info;
    if (fstat(handle, &info) != 0 || (!S_ISDIR(info.st_mode) && !S_ISREG(info.st_mode)))
        return 0;
    kind = S_ISDIR(info.st_mode) ? 1 : 0;
    if (directory >= 0 && kind != (directory != 0))
        return 0;
    out->device = (uint64_t)info.st_dev;
    out->identity = (uint64_t)info.st_ino;
    out->size = (uint64_t)info.st_size;
    out->links = (uint64_t)info.st_nlink;
    out->modified = (uint64_t)info.st_mtim.tv_sec;
    out->nanoseconds = (unsigned int)info.st_mtim.tv_nsec;
#endif
    if (kind_out)
        *kind_out = kind;
    return 1;
}

/* A selected kind and its initial metadata come from one query of the same
 * freshly opened handle. Later fresh-binding, metadata and payload checks
 * remain independent; captured identity alone grants no source authority. */
static int cupidbuild_observer_stat(cupidbuild_observer_handle_t handle, int directory,
                                    cupidbuild_observer_stat_t *out) {
    return cupidbuild_observer_stat_kind(handle, directory ? 1 : 0, out, (int *)0);
}

static int cupidbuild_observer_kind(cupidbuild_observer_handle_t handle,
                                    cupidbuild_observer_stat_t *captured_out) {
    int kind = -1;
    return cupidbuild_observer_stat_kind(handle, -1, captured_out, &kind) ? kind : -1;
}

static int cupidbuild_observer_same(const cupidbuild_observer_stat_t *a,
                                    const cupidbuild_observer_stat_t *b, int directory) {
    return a->device == b->device && a->identity == b->identity &&
           (directory || (a->size == b->size && a->links == b->links &&
                          a->modified == b->modified && a->nanoseconds == b->nanoseconds));
}

static cupidbuild_observer_handle_t
cupidbuild_observer_open_child_mode(cupidbuild_observer_handle_t parent, const char *name,
                                    int directory, int payload, long *status_out,
                                    int create_directory) {
#if defined(_WIN32)
    unsigned short wide[CUPIDBUILD_OBSERVER_NAME];
    size_t length = 0u;
    const char *cursor = name;
    unsigned int scalar;
    cupidbuild_windows_unicode_string_t unicode;
    cupidbuild_windows_object_attributes_t attributes;
    cupidbuild_windows_io_status_t status;
    HANDLE handle = INVALID_HANDLE_VALUE;
    long result;
    unsigned long access = FILE_READ_ATTRIBUTES | SYNCHRONIZE;
    unsigned long options = CUPIDBUILD_WINDOWS_FILE_SYNCHRONOUS_IO_NONALERT |
                            CUPIDBUILD_WINDOWS_FILE_OPEN_REPARSE_POINT;
    while (*cursor != 0) {
        if (!cupidbuild_observer_scalar(&cursor, &scalar) ||
            length + 2u >= CUPIDBUILD_OBSERVER_NAME)
            return INVALID_HANDLE_VALUE;
        if (scalar > 0xffffu) {
            scalar -= 0x10000u;
            wide[length++] = (unsigned short)(0xd800u + (scalar >> 10u));
            wide[length++] = (unsigned short)(0xdc00u + (scalar & 1023u));
        } else
            wide[length++] = (unsigned short)scalar;
    }
    wide[length] = 0u;
    unicode.length = (unsigned short)(length * 2u);
    unicode.maximum_length = (unsigned short)((length + 1u) * 2u);
    unicode.buffer = wide;
    (void)memset(&attributes, 0, sizeof(attributes));
    attributes.length = sizeof(attributes);
    attributes.root_directory = parent;
    attributes.object_name = &unicode;
    attributes.attributes =
        CUPIDBUILD_WINDOWS_OBJECT_CASE_INSENSITIVE | CUPIDBUILD_WINDOWS_OBJECT_DONT_REPARSE;
    (void)memset(&status, 0, sizeof(status));
    if (directory > 0) {
        access |= FILE_LIST_DIRECTORY | FILE_TRAVERSE;
        options |= CUPIDBUILD_WINDOWS_FILE_DIRECTORY_FILE;
    } else if (directory == 0) {
        if (payload)
            access |= GENERIC_READ;
        options |= CUPIDBUILD_WINDOWS_FILE_NON_DIRECTORY_FILE;
    }
    result = cupid_windows_nt_create_file(
        &handle, access, &attributes, &status, (void *)0, 0u, FILE_SHARE_READ | FILE_SHARE_WRITE,
        create_directory ? 3u /* FILE_OPEN_IF */ : CUPIDBUILD_WINDOWS_FILE_OPEN, options, (void *)0,
        0u);
    if (status_out != (long *)0)
        *status_out = result;
    if (result < 0)
        return INVALID_HANDLE_VALUE;
    return handle;
#elif defined(CUPIDBUILD_CUSTOM_LINUX)
            unsigned int flags = CUPIDBUILD_LINUX_O_NOFOLLOW | CUPIDBUILD_LINUX_O_CLOEXEC |
                                 CUPIDBUILD_LINUX_O_LARGEFILE;
            int handle;
            (void)payload;
            (void)status_out;
            if (create_directory) {
                int created = cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_MKDIRAT,
                                                   (unsigned int)parent, (unsigned int)name, 0777u);
                if (created < 0 && created != -17 /* EEXIST */)
                    return CUPIDBUILD_OBSERVER_INVALID;
            }
            flags |= directory > 0 ? CUPIDBUILD_LINUX_O_DIRECTORY : CUPIDBUILD_LINUX_O_NONBLOCK;
            handle = cupid_linux_syscall4(CUPIDBUILD_LINUX_SYS_OPENAT, (unsigned int)parent,
                                          (unsigned int)name, flags, 0u);
            return handle < 0 ? CUPIDBUILD_OBSERVER_INVALID : handle;
#else
    (void)payload;
    (void)status_out;
    if (create_directory && mkdirat(parent, name, 0777) != 0 && errno != EEXIST)
        return CUPIDBUILD_OBSERVER_INVALID;
    return cupidbuild_native_open_relative(parent, name, directory > 0);
#endif
}

static cupidbuild_observer_handle_t
cupidbuild_observer_open_child_status(cupidbuild_observer_handle_t parent, const char *name,
                                      int directory, int payload, long *status_out) {
    return cupidbuild_observer_open_child_mode(parent, name, directory, payload, status_out, 0);
}

static cupidbuild_observer_handle_t
cupidbuild_observer_open_child(cupidbuild_observer_handle_t parent, const char *name, int directory,
                               int payload) {
    return cupidbuild_observer_open_child_status(parent, name, directory, payload, (long *)0);
}

static int cupidbuild_observer_child_absent(cupidbuild_observer_handle_t parent, const char *name) {
#if defined(_WIN32)
    long status = 0;
    cupidbuild_observer_handle_t handle =
        cupidbuild_observer_open_child_status(parent, name, -1, 0, &status);
    if (handle != CUPIDBUILD_OBSERVER_INVALID) {
        (void)cupidbuild_observer_close_handle(handle);
        return 0;
    }
    return (unsigned long)status == 0xc000000fu || (unsigned long)status == 0xc0000034u ||
           (unsigned long)status == 0xc000003au;
#elif defined(CUPIDBUILD_CUSTOM_LINUX)
            unsigned char information[96];
            return cupid_linux_syscall4(CUPIDBUILD_LINUX_SYS_FSTATAT64, (unsigned int)parent,
                                        (unsigned int)name, (unsigned int)information,
                                        CUPIDBUILD_LINUX_AT_SYMLINK_NOFOLLOW) ==
                   -CUPIDBUILD_LINUX_ENOENT;
#else
    struct stat information;
    return fstatat(parent, name, &information, AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT;
#endif
}

/* Diagnose a failed capture through its retained parent. This cannot turn a
 * failure into an observation, and a concurrent replacement may change only
 * the diagnostic. The caller retains poison regardless of the result. */
static cupidbuild_observation_issue_t cupidbuild_observer_issue(cupidbuild_observer_handle_t parent,
                                                                const char *name, int directory) {
#if defined(_WIN32)
    long status = 0;
    cupidbuild_observation_issue_t issue = CUPIDBUILD_OBSERVATION_UNAVAILABLE;
    BY_HANDLE_FILE_INFORMATION info;
    HANDLE handle = cupidbuild_observer_open_child_status(parent, name, -1, 0, &status);
    if (handle == INVALID_HANDLE_VALUE) {
        unsigned long code = (unsigned long)status;
        if (code == 0xc000000fu || code == 0xc0000034u || code == 0xc000003au)
            return CUPIDBUILD_OBSERVATION_MISSING;
        if (code == 0xc000050bu)
            return CUPIDBUILD_OBSERVATION_LINKED;
        return CUPIDBUILD_OBSERVATION_UNAVAILABLE;
    }
    if (GetFileInformationByHandle(handle, &info)) {
        if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u)
            issue = CUPIDBUILD_OBSERVATION_LINKED;
        else if (directory && (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0u)
            issue = CUPIDBUILD_OBSERVATION_PARENT;
        else if (!directory &&
                 (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE)) != 0u)
            issue = CUPIDBUILD_OBSERVATION_KIND;
    }
    if (!cupidbuild_observer_close_handle(handle))
        return CUPIDBUILD_OBSERVATION_UNAVAILABLE;
    return issue;
#elif defined(CUPIDBUILD_CUSTOM_LINUX)
            unsigned char info[96];
            unsigned int mode;
            int result = cupid_linux_syscall4(CUPIDBUILD_LINUX_SYS_FSTATAT64, (unsigned int)parent,
                                              (unsigned int)name, (unsigned int)info,
                                              CUPIDBUILD_LINUX_AT_SYMLINK_NOFOLLOW);
            if (result < 0)
                return result == -CUPIDBUILD_LINUX_ENOENT ? CUPIDBUILD_OBSERVATION_MISSING
                                                          : CUPIDBUILD_OBSERVATION_UNAVAILABLE;
            mode = cupidbuild_linux_mode(info) & CUPIDBUILD_LINUX_S_IFMT;
            if (mode == CUPIDBUILD_LINUX_S_IFLNK)
                return CUPIDBUILD_OBSERVATION_LINKED;
            if (directory && mode != CUPIDBUILD_LINUX_S_IFDIR)
                return CUPIDBUILD_OBSERVATION_PARENT;
            if (!directory && mode != CUPIDBUILD_LINUX_S_IFREG)
                return CUPIDBUILD_OBSERVATION_KIND;
            return CUPIDBUILD_OBSERVATION_UNAVAILABLE;
#else
    struct stat info;
    if (fstatat(parent, name, &info, AT_SYMLINK_NOFOLLOW) != 0)
        return errno == ENOENT ? CUPIDBUILD_OBSERVATION_MISSING
                               : CUPIDBUILD_OBSERVATION_UNAVAILABLE;
    if (S_ISLNK(info.st_mode))
        return CUPIDBUILD_OBSERVATION_LINKED;
    if (directory && !S_ISDIR(info.st_mode))
        return CUPIDBUILD_OBSERVATION_PARENT;
    if (!directory && !S_ISREG(info.st_mode))
        return CUPIDBUILD_OBSERVATION_KIND;
    return CUPIDBUILD_OBSERVATION_UNAVAILABLE;
#endif
}

/* The index changes lookup cost only. The owning list retains every capture,
 * including repeated explicit leaves, for complete validation and close. */
static unsigned int cupidbuild_observer_bucket(cupidbuild_observer_entry_t *parent,
                                               const char *name) {
    size_t address = (size_t)parent;
    unsigned int hash;
    address ^= address >> 8;
    address ^= address >> 16;
    hash = (unsigned int)address;
    while (*name)
        hash = (hash * 16777619u) ^ (unsigned char)*name++;
    return hash & 255u;
}

static void cupidbuild_observer_index(cupidbuild_host_observer_t *observer,
                                      cupidbuild_observer_entry_t *entry) {
    unsigned int bucket = cupidbuild_observer_bucket(entry->parent, entry->name);
    entry->lookup_next = observer->entry_index[bucket];
    observer->entry_index[bucket] = entry;
}

static cupidbuild_observer_entry_t *cupidbuild_observer_lookup(cupidbuild_host_observer_t *observer,
                                                               cupidbuild_observer_entry_t *parent,
                                                               const char *name, int directory,
                                                               int allow_absent) {
    cupidbuild_observer_entry_t *entry =
        observer->entry_index[cupidbuild_observer_bucket(parent, name)];
    for (; entry; entry = entry->lookup_next) {
        if ((allow_absent || !entry->absent) && entry->parent == parent &&
            (directory < 0 || entry->directory == directory) && strcmp(entry->name, name) == 0)
            return entry;
    }
    return (cupidbuild_observer_entry_t *)0;
}

static cupidbuild_observer_entry_t *
cupidbuild_observer_add_captured(cupidbuild_host_observer_t *observer,
                                 cupidbuild_observer_entry_t *parent, const char *name,
                                 cupidbuild_observer_handle_t handle, int directory,
                                 const cupidbuild_observer_stat_t *captured) {
    cupidbuild_observer_entry_t *entry;
    if (handle == CUPIDBUILD_OBSERVER_INVALID) {
        cupidbuild_observer_fail(observer, "cannot open observation without following links");
        return (cupidbuild_observer_entry_t *)0;
    }
    entry = observer->count < observer->entry_quota
                ? (cupidbuild_observer_entry_t *)calloc(1u, sizeof(*entry))
                : (cupidbuild_observer_entry_t *)0;
    if (entry == (cupidbuild_observer_entry_t *)0) {
        (void)cupidbuild_observer_close_handle(handle);
        cupidbuild_observer_fail(observer, "observation handle limit or allocation failure");
        return (cupidbuild_observer_entry_t *)0;
    }
    entry->handle = handle;
    entry->directory = directory;
    entry->parent = parent;
    entry->next = observer->entries;
    observer->entries = entry;
    observer->count++;
    entry->name = (char *)malloc(strlen(name) + 1u);
    if (entry->name == (char *)0 ||
        (!captured && !cupidbuild_observer_stat(handle, directory, &entry->captured))) {
        cupidbuild_observer_fail(observer, "observation allocation or file-kind check failed");
        return (cupidbuild_observer_entry_t *)0;
    }
    if (captured)
        entry->captured = *captured;
    (void)strcpy(entry->name, name);
    cupidbuild_observer_index(observer, entry);
    return entry;
}

static cupidbuild_observer_entry_t *
cupidbuild_observer_add(cupidbuild_host_observer_t *observer, cupidbuild_observer_entry_t *parent,
                        const char *name, cupidbuild_observer_handle_t handle, int directory) {
    return cupidbuild_observer_add_captured(observer, parent, name, handle, directory,
                                            (const cupidbuild_observer_stat_t *)0);
}

static cupidbuild_observer_entry_t *cupidbuild_observer_walk(cupidbuild_host_observer_t *observer,
                                                             cupidbuild_observer_entry_t *parent,
                                                             const char *logical, int directory,
                                                             int payload) {
    char name[CUPIDBUILD_OBSERVER_NAME];
    const char *cursor = logical;
    if (logical == (const char *)0 || strlen(logical) >= CUPIDBUILD_HOST_PATH_BYTES)
        return (cupidbuild_observer_entry_t *)0;
    while (*cursor != 0) {
        size_t length = 0u;
        int kind;
        int has_captured = 0;
        cupidbuild_observer_stat_t captured;
        cupidbuild_observer_handle_t handle;
        cupidbuild_observer_entry_t *entry;
        while (*cursor != 0 && *cursor != '/') {
            if (length + 1u >= sizeof(name))
                return (cupidbuild_observer_entry_t *)0;
            name[length++] = *cursor++;
        }
        name[length] = 0;
        if (!cupidbuild_observer_input_name_valid(observer, name))
            return (cupidbuild_observer_entry_t *)0;
        kind = *cursor != 0 ? 1 : directory;
        handle = cupidbuild_observer_open_child(parent->handle, name, kind, payload);
        if (kind < 0 && handle != CUPIDBUILD_OBSERVER_INVALID) {
            kind = cupidbuild_observer_kind(handle, &captured);
            has_captured = kind >= 0;
            if (kind < 0) {
                int closed = cupidbuild_observer_close_handle(handle);
                cupidbuild_observer_fail(observer,
                                         closed ? "observation is not a regular file or directory"
                                                : "observation kind check and handle close failed");
                return (cupidbuild_observer_entry_t *)0;
            }
        }
        entry = (cupidbuild_observer_entry_t *)0;
        /* Explicit leaves remain distinct observations. Repeated ancestor walks
         * may reuse a retained directory only after checking both its original
         * handle and the freshly opened parent binding. A poisoned batch keeps
         * its original independent-capture behavior. */
        if (*cursor != 0 && handle != CUPIDBUILD_OBSERVER_INVALID && observer->error[0] == 0) {
            entry = cupidbuild_observer_lookup(observer, parent, name, 1, 0);
        }
        if (entry != (cupidbuild_observer_entry_t *)0) {
            cupidbuild_observer_stat_t current;
            int valid = cupidbuild_observer_stat(entry->handle, 1, &current) &&
                        cupidbuild_observer_same(&entry->captured, &current, 1) &&
                        cupidbuild_observer_stat(handle, 1, &current) &&
                        cupidbuild_observer_same(&entry->captured, &current, 1);
            if (!cupidbuild_observer_close_handle(handle))
                valid = 0;
            if (!valid) {
                cupidbuild_observer_fail(observer, "retained ancestor changed during observation");
                return (cupidbuild_observer_entry_t *)0;
            }
        } else
            entry = cupidbuild_observer_add_captured(
                observer, parent, name, handle, kind,
                has_captured ? &captured : (const cupidbuild_observer_stat_t *)0);
        if (entry == (cupidbuild_observer_entry_t *)0) {
            observer->last_issue = cupidbuild_observer_issue(parent->handle, name, kind);
            return entry;
        }
        parent = entry;
        if (*cursor == '/') {
            cursor++;
            if (*cursor == 0)
                return (cupidbuild_observer_entry_t *)0;
        }
    }
    return parent;
}

static int cupidbuild_observer_open_profile(const char *repository_root, size_t entry_quota,
                                            int source_names,
                                            cupidbuild_host_observer_t **observer_out);

int cupidbuild_host_observer_open(const char *repository_root,
                                  cupidbuild_host_observer_t **observer_out) {
    return cupidbuild_host_observer_open_quota(repository_root, CUPIDBUILD_OBSERVER_ENTRIES,
                                               observer_out);
}

int cupidbuild_host_observer_open_quota(const char *repository_root, size_t entry_quota,
                                        cupidbuild_host_observer_t **observer_out) {
    return cupidbuild_observer_open_profile(repository_root, entry_quota, 0, observer_out);
}

int cupidbuild_host_observer_open_source_quota(const char *repository_root, size_t entry_quota,
                                               cupidbuild_host_observer_t **observer_out) {
    return cupidbuild_observer_open_profile(repository_root, entry_quota, 1, observer_out);
}

static int cupidbuild_observer_open_profile(const char *repository_root, size_t entry_quota,
                                            int source_names,
                                            cupidbuild_host_observer_t **observer_out) {
    cupidbuild_host_observer_t *observer;
    cupidbuild_observer_entry_t *anchor;
    cupidbuild_observer_handle_t handle;
    char absolute[CUPIDBUILD_HOST_PATH_BYTES];
    const char *relative;
#if defined(_WIN32)
    char drive[2u * CUPIDBUILD_OBSERVER_NAME + 4u];
    size_t index;
#endif
    if (observer_out == (cupidbuild_host_observer_t **)0)
        return 0;
    *observer_out = (cupidbuild_host_observer_t *)0;
    if (entry_quota == 0u || entry_quota == (size_t)-1 || repository_root == (const char *)0 ||
        repository_root[0] == 0)
        return 0;
#if defined(_WIN32)
    if (_fullpath(absolute, repository_root, sizeof(absolute)) == (char *)0 ||
        strlen(absolute) < 3u)
        return 0;
    for (index = 0u; absolute[index] != 0; index++)
        if (absolute[index] == '\\')
            absolute[index] = '/';
    if (absolute[1] == ':' && absolute[2] == '/') {
        drive[0] = absolute[0];
        drive[1] = ':';
        drive[2] = '\\';
        drive[3] = 0;
        relative = absolute + 3u;
        handle = CreateFileA(
            drive, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | FILE_TRAVERSE | SYNCHRONIZE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, (LPSECURITY_ATTRIBUTES)0, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, (HANDLE)0);
    } else if (absolute[0] == '/' && absolute[1] == '/') {
        char component[CUPIDBUILD_OBSERVER_NAME];
        unsigned short wide[2u * CUPIDBUILD_OBSERVER_NAME + 4u];
        size_t units = 0u;
        size_t part;
        size_t end = 2u;
        for (part = 0u; part < 2u; part++) {
            size_t length = 0u;
            while (absolute[end] && absolute[end] != '/') {
                if (length + 1u >= sizeof(component))
                    return 0;
                component[length++] = absolute[end++];
            }
            component[length] = 0;
            if (!cupidbuild_observer_name_valid(component) ||
                (part == 0u && strcmp(component, "?") == 0))
                return 0;
            if (part == 0u && !absolute[end])
                return 0;
            if (part == 0u)
                end++;
        }
        if (end + 1u >= sizeof(drive))
            return 0;
        (void)memcpy(drive, absolute, end);
        drive[end] = '\\';
        drive[end + 1u] = 0;
        for (index = 0u; index < end; index++)
            if (drive[index] == '/')
                drive[index] = '\\';
        if (!cupidbuild_path_to_utf16(drive, end + 1u, wide, sizeof(wide) / sizeof(wide[0]),
                                      &units))
            return 0;
        relative = absolute + end;
        if (*relative == '/')
            relative++;
#if defined(CUPID_HOSTED_I386_WINDOWS_H)
        /* The existing UTF-8 runtime adapter owns the wide Win32 call. */
        handle =
            CreateFileA(drive,
#else
        handle =
            CreateFileW(wide,
#endif
                        FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | FILE_TRAVERSE | SYNCHRONIZE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, (LPSECURITY_ATTRIBUTES)0, OPEN_EXISTING,
                        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, (HANDLE)0);
    } else
        return 0;
#else
            /* Root paths are absolute so no current-directory change can retarget them. */
            if (repository_root[0] != '/' ||
                !cupidbuild_host_copy_text(absolute, sizeof(absolute), repository_root))
                return 0;
            relative = absolute + 1u;
            handle = cupidbuild_host_open_directory("/");
            if (handle < 0)
                handle = CUPIDBUILD_OBSERVER_INVALID;
#endif
    observer = (cupidbuild_host_observer_t *)calloc(1u, sizeof(*observer));
    if (observer == (cupidbuild_host_observer_t *)0) {
        if (handle != CUPIDBUILD_OBSERVER_INVALID)
            (void)cupidbuild_observer_close_handle(handle);
        return 0;
    }
    observer->entry_quota = entry_quota;
    observer->source_names = source_names;
    anchor = cupidbuild_observer_add(observer, (cupidbuild_observer_entry_t *)0, "", handle, 1);
    if (anchor != (cupidbuild_observer_entry_t *)0)
        observer->root = cupidbuild_observer_walk(observer, anchor, relative, 1, 0);
    if (observer->root == (cupidbuild_observer_entry_t *)0) {
        (void)cupidbuild_host_observer_close(observer);
        return 0;
    }
    *observer_out = observer;
    return 1;
}

static int cupidbuild_observer_read(cupidbuild_observer_entry_t *entry, size_t limit,
                                    unsigned char **out) {
    cupidbuild_observer_stat_t before;
    cupidbuild_observer_stat_t after;
    unsigned char *bytes;
    size_t size;
    size_t offset = 0u;
    *out = (unsigned char *)0;
    if (!cupidbuild_observer_stat(entry->handle, 0, &before) ||
        !cupidbuild_observer_same(&entry->captured, &before, 0) || before.size > limit ||
        before.size >= (size_t)-1 || before.size > CUPIDBUILD_HOST_FILE_LIMIT)
        return 0;
    size = (size_t)before.size;
#if defined(_WIN32)
    if (SetFilePointer(entry->handle, 0, 0, FILE_BEGIN) != 0u)
        return 0;
#elif defined(CUPIDBUILD_CUSTOM_LINUX)
            if (cupid_linux_syscall3(19u, (unsigned int)entry->handle, 0u, 0u) != 0)
                return 0;
#else
    if (lseek(entry->handle, 0, SEEK_SET) != 0)
        return 0;
#endif
    bytes = (unsigned char *)malloc(size + 1u);
    if (bytes == (unsigned char *)0)
        return 0;
    while (offset < size) {
        size_t chunk = size - offset;
        int count;
        if (chunk > 65536u)
            chunk = 65536u;
#if defined(_WIN32)
        DWORD received = 0u;
        count = ReadFile(entry->handle, bytes + offset, (DWORD)chunk, &received, (LPOVERLAPPED)0)
                    ? (int)received
                    : -1;
#elif defined(CUPIDBUILD_CUSTOM_LINUX)
                count = cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_READ, (unsigned int)entry->handle,
                                             (unsigned int)(bytes + offset), (unsigned int)chunk);
                if (count == -4)
                    continue;
#else
        count = (int)read(entry->handle, bytes + offset, chunk);
        if (count < 0 && errno == EINTR)
            continue;
#endif
        if (count <= 0 || (size_t)count > chunk) {
            free(bytes);
            return 0;
        }
        offset += (size_t)count;
    }
    if (!cupidbuild_observer_stat(entry->handle, 0, &after) ||
        !cupidbuild_observer_same(&before, &after, 0)) {
        free(bytes);
        return 0;
    }
    bytes[size] = 0u;
    *out = bytes;
    return 1;
}

static int cupidbuild_observer_stream_read(cupidbuild_observer_entry_t *entry, uint64_t limit,
                                           cupidbuild_host_stream_sink_t sink, void *context,
                                           unsigned char digest[32]) {
    cupidbuild_observer_stat_t before;
    cupidbuild_observer_stat_t after;
    cupidbuild_sha_context_t hash;
    unsigned char bytes[65536];
    uint64_t offset = 0u;
    if (!cupidbuild_observer_stat(entry->handle, 0, &before) ||
        !cupidbuild_observer_same(&entry->captured, &before, 0) || before.size > limit ||
        before.size > (((uint64_t)-1) >> 3u))
        return 0;
#if defined(_WIN32)
    if (SetFilePointer(entry->handle, 0, 0, FILE_BEGIN) != 0u)
        return 0;
#elif defined(CUPIDBUILD_CUSTOM_LINUX)
            if (cupid_linux_syscall3(19u, (unsigned int)entry->handle, 0u, 0u) != 0)
                return 0;
#else
    if (lseek(entry->handle, 0, SEEK_SET) != 0)
        return 0;
#endif
    cupidbuild_sha_initialize(&hash);
    while (offset < before.size) {
        size_t chunk = sizeof(bytes);
        int count;
        if (before.size - offset < (uint64_t)chunk)
            chunk = (size_t)(before.size - offset);
#if defined(_WIN32)
        DWORD received = 0u;
        count = ReadFile(entry->handle, bytes, (DWORD)chunk, &received, (LPOVERLAPPED)0)
                    ? (int)received
                    : -1;
#elif defined(CUPIDBUILD_CUSTOM_LINUX)
                count = cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_READ, (unsigned int)entry->handle,
                                             (unsigned int)bytes, (unsigned int)chunk);
                if (count == -4)
                    continue;
#else
        count = (int)read(entry->handle, bytes, chunk);
        if (count < 0 && errno == EINTR)
            continue;
#endif
        if (count <= 0 || (size_t)count > chunk)
            return 0;
        cupidbuild_sha_update(&hash, bytes, (size_t)count);
        if (sink != (cupidbuild_host_stream_sink_t)0 &&
            !sink(context, offset, bytes, (size_t)count))
            return 0;
        offset += (uint64_t)count;
    }
    if (!cupidbuild_observer_stat(entry->handle, 0, &after) ||
        !cupidbuild_observer_same(&before, &after, 0))
        return 0;
    cupidbuild_sha_finish(&hash, digest);
    return 1;
}

int cupidbuild_host_observer_file_stream(cupidbuild_host_observer_t *observer, const char *logical,
                                         uint64_t limit, cupidbuild_host_stream_sink_t sink,
                                         void *context,
                                         cupidbuild_host_stream_observation_t *result_out) {
    cupidbuild_observer_entry_t *entry;
    unsigned char digest[32];
    if (result_out != (cupidbuild_host_stream_observation_t *)0)
        (void)memset(result_out, 0, sizeof(*result_out));
    if (observer == (cupidbuild_host_observer_t *)0 || observer->error[0] != 0)
        return 0;
    if (result_out == (cupidbuild_host_stream_observation_t *)0 || logical == (const char *)0 ||
        logical[0] == 0)
        return cupidbuild_observer_fail(observer, "invalid streamed file observation arguments");
    entry = cupidbuild_observer_walk(observer, observer->root, logical, 0, 1);
    if (entry == (cupidbuild_observer_entry_t *)0)
        return cupidbuild_observer_fail(observer,
                                        "unsafe or unavailable streamed file observation");
    if (!cupidbuild_observer_stream_read(entry, limit, sink, context, digest))
        return cupidbuild_observer_fail(observer,
                                        "streamed payload exceeded limit, changed or failed");
    (void)memcpy(entry->digest, digest, sizeof(digest));
    entry->payload = 2;
    result_out->size = entry->captured.size;
    (void)memcpy(result_out->sha256, digest, sizeof(digest));
    return 1;
}

static cupidbuild_observer_entry_t *
cupidbuild_observer_retained_path(cupidbuild_host_observer_t *observer, const char *logical,
                                  int last_is_directory) {
    cupidbuild_observer_entry_t *parent;
    const char *cursor = logical;
    char name[CUPIDBUILD_OBSERVER_NAME];
    if (observer == (cupidbuild_host_observer_t *)0 || observer->error[0] != 0 ||
        logical == (const char *)0 || logical[0] == 0 ||
        strlen(logical) >= CUPIDBUILD_HOST_PATH_BYTES)
        return (cupidbuild_observer_entry_t *)0;
    parent = observer->root;
    while (*cursor != 0 && parent != (cupidbuild_observer_entry_t *)0) {
        cupidbuild_observer_entry_t *entry;
        size_t length = 0u;
        int directory;
        while (*cursor != 0 && *cursor != '/') {
            if (length + 1u >= sizeof(name))
                return (cupidbuild_observer_entry_t *)0;
            name[length++] = *cursor++;
        }
        name[length] = 0;
        if (!cupidbuild_observer_input_name_valid(observer, name))
            return (cupidbuild_observer_entry_t *)0;
        directory = *cursor != 0 || last_is_directory;
        entry = cupidbuild_observer_lookup(observer, parent, name, directory, 0);
        if (entry == (cupidbuild_observer_entry_t *)0)
            return entry;
        parent = entry;
        if (*cursor != 0 && *++cursor == 0)
            return (cupidbuild_observer_entry_t *)0;
    }
    return parent;
}

static cupidbuild_observer_entry_t *
cupidbuild_observer_retained_file(cupidbuild_host_observer_t *observer, const char *logical) {
    return cupidbuild_observer_retained_path(observer, logical, 0);
}

static int cupidbuild_observer_retained_bindings(cupidbuild_host_observer_t *observer,
                                                 cupidbuild_observer_entry_t *entry) {
    for (; entry != (cupidbuild_observer_entry_t *)0; entry = entry->parent) {
        cupidbuild_observer_stat_t current;
        int identity_only = entry->directory && entry != observer->root;
        if (!cupidbuild_observer_stat(entry->handle, entry->directory, &current) ||
            !cupidbuild_observer_same(&entry->captured, &current, identity_only))
            return 0;
        if (entry->parent != (cupidbuild_observer_entry_t *)0) {
            cupidbuild_observer_handle_t fresh = cupidbuild_observer_open_child(
                entry->parent->handle, entry->name, entry->directory, 0);
            int valid;
            if (fresh == CUPIDBUILD_OBSERVER_INVALID)
                return 0;
            valid = cupidbuild_observer_stat(fresh, entry->directory, &current) &&
                    cupidbuild_observer_same(&entry->captured, &current, identity_only);
            if (!cupidbuild_observer_close_handle(fresh))
                valid = 0;
            if (!valid)
                return 0;
        }
    }
    return 1;
}

static int cupidbuild_observer_path_name_valid(cupidbuild_host_observer_t *observer,
                                               const char *logical) {
    const char *cursor;
    char name[CUPIDBUILD_OBSERVER_NAME];
    if (!logical || !logical[0] || strlen(logical) >= CUPIDBUILD_HOST_PATH_BYTES)
        return cupidbuild_observer_fail(observer, "invalid optional path observation");
    /* Validate the whole suffix before a first absence can end the walk. */
    cursor = logical;
    for (;;) {
        size_t length = 0u;
        while (*cursor && *cursor != '/') {
            if (length + 1u >= sizeof(name))
                return cupidbuild_observer_fail(observer,
                                                "optional path component exceeds its byte limit");
            name[length++] = *cursor++;
        }
        name[length] = 0;
        if (!cupidbuild_observer_input_name_valid(observer, name))
            return cupidbuild_observer_fail(observer, "optional path is not normalized UTF-8");
        if (!*cursor)
            break;
        cursor++;
    }
    return 1;
}

static int cupidbuild_observer_path_kind_profile(cupidbuild_host_observer_t *observer,
                                                 const char *logical,
                                                 cupidbuild_host_entry_kind_t *kind_out,
                                                 int grouped) {
    const char *cursor;
    char name[CUPIDBUILD_OBSERVER_NAME];
    cupidbuild_observer_entry_t *parent;
    if (kind_out != (cupidbuild_host_entry_kind_t *)0)
        *kind_out = CUPIDBUILD_ENTRY_NONE;
    if (observer == (cupidbuild_host_observer_t *)0 || observer->error[0])
        return 0;
    if (kind_out == (cupidbuild_host_entry_kind_t *)0 || logical == (const char *)0 ||
        !logical[0] || strlen(logical) >= CUPIDBUILD_HOST_PATH_BYTES)
        return cupidbuild_observer_fail(observer, "invalid optional path observation");
    if (!cupidbuild_observer_path_name_valid(observer, logical))
        return 0;
    cursor = logical;
    parent = observer->root;
    if (!parent || (!grouped && !cupidbuild_observer_retained_bindings(observer, parent)))
        return cupidbuild_observer_fail(observer, "optional path root binding changed");
    if (grouped) {
        cupidbuild_observer_stat_t current_root;
        if (!cupidbuild_observer_stat(parent->handle, 1, &current_root) ||
            !cupidbuild_observer_same(&parent->captured, &current_root, grouped == 2))
            return cupidbuild_observer_fail(observer, "group path root metadata changed");
    }
    for (;;) {
        size_t length = 0u;
        cupidbuild_observer_entry_t *entry;
        cupidbuild_observer_handle_t fresh;
        while (*cursor && *cursor != '/')
            name[length++] = *cursor++;
        name[length] = 0;
        if (!parent)
            return cupidbuild_observer_fail(observer, "optional path parent missing");
        if (parent != observer->root) {
            cupidbuild_observer_stat_t current_parent;
            if (!cupidbuild_observer_stat(parent->handle, 1, &current_parent) ||
                !cupidbuild_observer_same(&parent->captured, &current_parent, 1))
                return cupidbuild_observer_fail(observer, "optional path parent identity changed");
        }
        entry = cupidbuild_observer_lookup(observer, parent, name, -1, 1);
        if (entry != (cupidbuild_observer_entry_t *)0 && entry->absent) {
            if (!cupidbuild_observer_child_absent(parent->handle, name))
                return cupidbuild_observer_fail(observer, "retained first absence changed");
            return 1;
        }
        if (cupidbuild_observer_child_absent(parent->handle, name)) {
            if (entry != (cupidbuild_observer_entry_t *)0)
                return cupidbuild_observer_fail(observer, "retained optional path disappeared");
            entry = observer->count < observer->entry_quota
                        ? (cupidbuild_observer_entry_t *)calloc(1u, sizeof(*entry))
                        : (cupidbuild_observer_entry_t *)0;
            if (!entry)
                return cupidbuild_observer_fail(observer,
                                                "optional path quota or allocation failure");
            entry->name = (char *)malloc(length + 1u);
            if (!entry->name) {
                free(entry);
                return cupidbuild_observer_fail(observer, "optional path name allocation failure");
            }
            (void)memcpy(entry->name, name, length + 1u);
            entry->handle = CUPIDBUILD_OBSERVER_INVALID;
            entry->absent = 1;
            entry->parent = parent;
            entry->next = observer->entries;
            observer->entries = entry;
            observer->count++;
            cupidbuild_observer_index(observer, entry);
            return 1;
        }
        fresh = cupidbuild_observer_open_child(parent->handle, name, *cursor ? 1 : -1, 0);
        if (fresh == CUPIDBUILD_OBSERVER_INVALID)
            return cupidbuild_observer_fail(
                observer, "optional path is unavailable, linked or has a non-directory parent");
        if (entry != (cupidbuild_observer_entry_t *)0) {
            cupidbuild_observer_stat_t current;
            int valid = cupidbuild_observer_stat(fresh, entry->directory, &current) &&
                        cupidbuild_observer_same(&entry->captured, &current, entry->directory) &&
                        (!*cursor || entry->directory);
            if (!cupidbuild_observer_close_handle(fresh))
                valid = 0;
            if (!valid)
                return cupidbuild_observer_fail(observer, "retained optional path changed");
        } else {
            cupidbuild_observer_stat_t captured;
            int directory = *cursor ? 1 : cupidbuild_observer_kind(fresh, &captured);
            if (directory < 0) {
                (void)cupidbuild_observer_close_handle(fresh);
                return cupidbuild_observer_fail(observer,
                                                "optional path is not a regular file or directory");
            }
            entry = cupidbuild_observer_add_captured(observer, parent, name, fresh, directory,
                                                     *cursor ? (const cupidbuild_observer_stat_t *)0
                                                             : &captured);
            if (!entry)
                return 0;
        }
        if (!*cursor) {
            *kind_out = entry->directory ? CUPIDBUILD_ENTRY_DIRECTORY : CUPIDBUILD_ENTRY_FILE;
            return 1;
        }
        parent = entry;
        cursor++;
    }
}

int cupidbuild_host_observer_path_kind(cupidbuild_host_observer_t *observer, const char *logical,
                                       cupidbuild_host_entry_kind_t *kind_out) {
    if (observer && observer->stream_group_active) {
        if (kind_out)
            *kind_out = CUPIDBUILD_ENTRY_NONE;
        return cupidbuild_observer_fail(observer, "retained capture is still active");
    }
    return cupidbuild_observer_path_kind_profile(observer, logical, kind_out, 0);
}

int cupidbuild_host_observer_source_path_kind(cupidbuild_host_observer_t *observer,
                                              const char *logical,
                                              cupidbuild_host_entry_kind_t *kind_out) {
    if (observer && observer->stream_group_active) {
        if (kind_out)
            *kind_out = CUPIDBUILD_ENTRY_NONE;
        return cupidbuild_observer_fail(observer, "retained capture is still active");
    }
    return cupidbuild_observer_path_kind_profile(observer, logical, kind_out, 2);
}
