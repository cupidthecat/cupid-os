/**
 * @file cupidbuild_host_source_paths.inc
 * @brief Resolve retained source and output paths and check their bindings.
 */

struct cupidbuild_host_output_parent {
    cupidbuild_host_observer_t *observer;
    cupidbuild_observer_entry_t *leaf;
    char output[CUPIDBUILD_HOST_PATH_BYTES];
    char resolved_root[CUPIDBUILD_HOST_PATH_BYTES];
    char resolved_source[CUPIDBUILD_HOST_PATH_BYTES];
    int resolved;
    char error[CUPIDBUILD_HOST_ERROR_BYTES];
};

static int cupidbuild_output_parent_fail(cupidbuild_host_output_parent_t *parent,
                                         const char *message) {
    if (parent != (cupidbuild_host_output_parent_t *)0 && parent->error[0] == 0)
        (void)cupidbuild_host_copy_text(parent->error, sizeof(parent->error), message);
    return 0;
}

const char *cupidbuild_host_output_parent_error(const cupidbuild_host_output_parent_t *parent) {
    return parent != (const cupidbuild_host_output_parent_t *)0 && parent->error[0]
               ? parent->error
               : "output parent preparation failed";
}

int cupidbuild_host_output_parent_require_current(cupidbuild_host_output_parent_t *parent) {
    if (parent == (cupidbuild_host_output_parent_t *)0 || parent->error[0] ||
        parent->leaf == (cupidbuild_observer_entry_t *)0)
        return 0;
    if (!cupidbuild_observer_require_current(parent->observer, 1))
        return cupidbuild_output_parent_fail(parent,
                                             cupidbuild_host_observer_error(parent->observer));
    return 1;
}

static int cupidbuild_output_parent_fill(cupidbuild_host_output_parent_t *parent, const char *root,
                                         const char *output, int create_directories) {
    cupidbuild_observer_entry_t *entry;
    const char *cursor;
    char component[CUPIDBUILD_OBSERVER_NAME];
    if (output == (const char *)0 || !output[0] || strlen(output) >= sizeof(parent->output))
        return cupidbuild_output_parent_fail(parent, "invalid normalized output path");
    /* Check the whole request before creating even its first directory. */
    cursor = output;
    for (;;) {
        size_t length = 0u;
        while (*cursor && *cursor != '/') {
            if (length + 1u >= sizeof(component))
                return cupidbuild_output_parent_fail(parent,
                                                     "output component exceeds its byte limit");
            component[length++] = *cursor++;
        }
        component[length] = 0;
        if (!cupidbuild_observer_name_valid(component))
            return cupidbuild_output_parent_fail(parent, "output path is not normalized");
        if (!*cursor)
            break;
        cursor++;
    }
    (void)memcpy(parent->output, output, strlen(output) + 1u);
    if (!cupidbuild_host_observer_open(root, &parent->observer))
        return cupidbuild_output_parent_fail(parent, "cannot retain output repository root");
    entry = parent->observer->root;
    cursor = output;
    for (;;) {
        size_t length = 0u;
        cupidbuild_observer_entry_t *child;
        cupidbuild_observer_entry_t *ancestor;
        cupidbuild_observer_handle_t handle;
        while (*cursor && *cursor != '/')
            component[length++] = *cursor++;
        component[length] = 0;
        if (!*cursor)
            break;
        cursor++;
        if (!cupidbuild_observer_require_current(parent->observer, 1))
            return cupidbuild_output_parent_fail(parent,
                                                 cupidbuild_host_observer_error(parent->observer));
        handle = cupidbuild_observer_open_child_mode(entry->handle, component, 1, 0, (long *)0,
                                                     create_directories);
        child = cupidbuild_observer_add(parent->observer, entry, component, handle, 1);
        if (child == (cupidbuild_observer_entry_t *)0)
            return cupidbuild_output_parent_fail(parent,
                                                 cupidbuild_host_observer_error(parent->observer));
        for (ancestor = entry; ancestor != (cupidbuild_observer_entry_t *)0;
             ancestor = ancestor->parent) {
            if (cupidbuild_observer_same(&ancestor->captured, &child->captured, 1))
                return cupidbuild_output_parent_fail(parent,
                                                     "output directory aliases an ancestor");
        }
        entry = child;
    }
    parent->leaf = entry;
    return cupidbuild_host_output_parent_require_current(parent);
}

static int cupidbuild_output_parent_open(const char *root, const char *output,
                                         cupidbuild_host_output_parent_t **parent_out,
                                         int create_directories) {
    cupidbuild_host_output_parent_t *parent;
    if (parent_out == (cupidbuild_host_output_parent_t **)0)
        return 0;
    *parent_out = (cupidbuild_host_output_parent_t *)0;
    parent = (cupidbuild_host_output_parent_t *)calloc(1u, sizeof(*parent));
    if (parent == (cupidbuild_host_output_parent_t *)0)
        return 0;
    *parent_out = parent;
    return cupidbuild_output_parent_fill(parent, root, output, create_directories);
}

int cupidbuild_host_output_parent_prepare(const char *root, const char *output,
                                          cupidbuild_host_output_parent_t **parent_out) {
    return cupidbuild_output_parent_open(root, output, parent_out, 1);
}

int cupidbuild_host_output_parent_open_existing(const char *root, const char *output,
                                                cupidbuild_host_output_parent_t **parent_out) {
    return cupidbuild_output_parent_open(root, output, parent_out, 0);
}

/* Follow aliases only while resolving existing directories. The resulting
 * physical chain is reopened without following links and its identities are
 * compared before any resolution handle is released. */
#if defined(CUPID_HOSTED_I386_WINDOWS_H)
/* The existing runtime assembly wrapper accepts UTF-16 directly. */
unsigned int cupid_windows_create_file_wide(const unsigned short *, unsigned int, unsigned int,
                                            void *, unsigned int, unsigned int, unsigned int);
unsigned int cupid_windows_get_full_path_name_wide(const unsigned short *, unsigned int,
                                                   unsigned short *, unsigned short **);
#endif

static cupidbuild_observer_handle_t
cupidbuild_output_resolve_directory_captured(const char *path, char *physical, size_t capacity,
                                             cupidbuild_observer_stat_t *captured_out) {
    cupidbuild_observer_handle_t handle = CUPIDBUILD_OBSERVER_INVALID;
    cupidbuild_observer_stat_t information;
    int valid = 0;
    if (captured_out)
        (void)memset(captured_out, 0, sizeof(*captured_out));
#if defined(_WIN32)
    unsigned short *wide;
    DWORD units;
    size_t bytes = 0u;
    size_t index;
#if defined(CUPID_HOSTED_I386_WINDOWS_H)
    {
        unsigned short *input =
            (unsigned short *)malloc(CUPIDBUILD_HOST_PATH_BYTES * sizeof(*input));
        unsigned short *absolute =
            (unsigned short *)malloc(CUPIDBUILD_HOST_PATH_BYTES * sizeof(*absolute));
        unsigned short *extended =
            (unsigned short *)malloc((CUPIDBUILD_HOST_PATH_BYTES + 8u) * sizeof(*extended));
        size_t input_units = 0u, extended_units = 0u, absolute_bytes = 0u;
        DWORD absolute_units = 0u;
        if (input && absolute && extended && path &&
            cupidbuild_path_to_utf16(path, strlen(path), input, CUPIDBUILD_HOST_PATH_BYTES,
                                     &input_units)) {
            absolute_units = cupid_windows_get_full_path_name_wide(
                input, CUPIDBUILD_HOST_PATH_BYTES, absolute, (unsigned short **)0);
            if (absolute_units && absolute_units < CUPIDBUILD_HOST_PATH_BYTES &&
                cupidbuild_path_to_utf8(absolute, (size_t)absolute_units, (char *)0, 0u,
                                        &absolute_bytes) &&
                absolute_bytes < CUPIDBUILD_HOST_PATH_BYTES) {
                for (index = 0u; index < (size_t)absolute_units; index++)
                    if (absolute[index] == '/')
                        absolute[index] = '\\';
                if (cupidbuild_path_windows_extended(absolute, (size_t)absolute_units, extended,
                                                     CUPIDBUILD_HOST_PATH_BYTES + 8u,
                                                     &extended_units))
                    handle = cupid_windows_create_file_wide(
                        extended,
                        FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | FILE_TRAVERSE | SYNCHRONIZE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, (void *)0, OPEN_EXISTING,
                        FILE_FLAG_BACKUP_SEMANTICS, (HANDLE)0);
            }
        }
        free(extended);
        free(absolute);
        free(input);
    }
#else
    {
        unsigned short *input =
            (unsigned short *)malloc(CUPIDBUILD_HOST_PATH_BYTES * sizeof(*input));
        unsigned short *absolute =
            (unsigned short *)malloc(CUPIDBUILD_HOST_PATH_BYTES * sizeof(*absolute));
        unsigned short *extended =
            (unsigned short *)malloc((CUPIDBUILD_HOST_PATH_BYTES + 8u) * sizeof(*extended));
        size_t input_units = 0u, extended_units = 0u;
        DWORD absolute_units = 0u;
        if (input && absolute && extended &&
            cupidbuild_path_to_utf16(path, strlen(path), input, CUPIDBUILD_HOST_PATH_BYTES,
                                     &input_units)) {
            absolute_units =
                GetFullPathNameW(input, CUPIDBUILD_HOST_PATH_BYTES, absolute, (LPWSTR *)0);
            if (absolute_units && absolute_units < CUPIDBUILD_HOST_PATH_BYTES) {
                for (index = 0u; index < (size_t)absolute_units; index++)
                    if (absolute[index] == '/')
                        absolute[index] = '\\';
                if (cupidbuild_path_windows_extended(absolute, (size_t)absolute_units, extended,
                                                     CUPIDBUILD_HOST_PATH_BYTES + 8u,
                                                     &extended_units))
                    handle = CreateFileW(
                        extended,
                        FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | FILE_TRAVERSE | SYNCHRONIZE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, (LPSECURITY_ATTRIBUTES)0, OPEN_EXISTING,
                        FILE_FLAG_BACKUP_SEMANTICS, (HANDLE)0);
            }
        }
        free(extended);
        free(absolute);
        free(input);
    }
#endif
    if (handle == CUPIDBUILD_OBSERVER_INVALID)
        return handle;
    wide = (unsigned short *)malloc(CUPIDBUILD_HOST_PATH_BYTES * sizeof(*wide));
    if (wide != (unsigned short *)0) {
        units = GetFinalPathNameByHandleW(handle, wide, CUPIDBUILD_HOST_PATH_BYTES, 0u);
        if (units > 4u && units < CUPIDBUILD_HOST_PATH_BYTES && wide[0] == '\\' &&
            wide[1] == '\\' && wide[2] == '?' && wide[3] == '\\') {
            if (units > 6u && wide[5] == ':') {
                valid = cupidbuild_path_to_utf8(wide + 4u, (size_t)units - 4u, physical, capacity,
                                                &bytes);
            } else if (units > 8u && wide[4] == 'U' && wide[5] == 'N' && wide[6] == 'C' &&
                       wide[7] == '\\' && capacity > 2u) {
                valid = cupidbuild_path_to_utf8(wide + 8u, (size_t)units - 8u, physical + 2u,
                                                capacity - 2u, &bytes);
                if (valid) {
                    physical[0] = '/';
                    physical[1] = '/';
                    bytes += 2u;
                }
            }
            if (valid) {
                for (index = 0u; index < bytes; index++)
                    if (physical[index] == '\\')
                        physical[index] = '/';
            }
        }
        free(wide);
    }
#else
            char descriptor_path[64];
            int count;
#if defined(CUPIDBUILD_CUSTOM_LINUX)
            handle =
                cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_OPEN, (unsigned int)path,
                                     CUPIDBUILD_LINUX_O_DIRECTORY | CUPIDBUILD_LINUX_O_CLOEXEC |
                                         CUPIDBUILD_LINUX_O_LARGEFILE,
                                     0u);
            if (handle < 0)
                return CUPIDBUILD_OBSERVER_INVALID;
#else
            handle = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (handle < 0)
                return CUPIDBUILD_OBSERVER_INVALID;
#endif
            count = snprintf(descriptor_path, sizeof(descriptor_path), "/proc/self/fd/%d", handle);
            if (count > 0 && (size_t)count < sizeof(descriptor_path) && capacity > 1u) {
#if defined(CUPIDBUILD_CUSTOM_LINUX)
                count = cupid_linux_syscall3(85u /* readlink */, (unsigned int)descriptor_path,
                                             (unsigned int)physical, (unsigned int)(capacity - 1u));
#else
                count = (int)readlink(descriptor_path, physical, capacity - 1u);
#endif
                if (count > 0 && (size_t)count < capacity - 1u) {
                    physical[count] = 0;
                    valid = physical[0] == '/';
                }
            }
#endif
    if (!valid || !cupidbuild_observer_stat(handle, 1, &information)) {
        (void)cupidbuild_observer_close_handle(handle);
        return CUPIDBUILD_OBSERVER_INVALID;
    }
    if (captured_out)
        *captured_out = information;
    return handle;
}

static cupidbuild_observer_handle_t
cupidbuild_output_resolve_directory(const char *path, char *physical, size_t capacity) {
    return cupidbuild_output_resolve_directory_captured(path, physical, capacity,
                                                        (cupidbuild_observer_stat_t *)0);
}

static int cupidbuild_observer_request_parent(const char *path, char *parent, const char **leaf) {
    size_t length, split, parent_length;
    if (!path || !path[0])
        return 0;
    length = strlen(path);
    if (length >= CUPIDBUILD_HOST_PATH_BYTES)
        return 0;
#if defined(_WIN32)
    if (!((length >= 3u && path[1] == ':' && (path[2] == '/' || path[2] == '\\')) ||
          (length >= 2u && (path[0] == '/' || path[0] == '\\') &&
           (path[1] == '/' || path[1] == '\\'))))
        return 0;
#else
            if (path[0] != '/')
                return 0;
#endif
    split = length;
    while (split && path[split - 1u] != '/'
#if defined(_WIN32)
           && path[split - 1u] != '\\'
#endif
    )
        split--;
    if (!split || split == length)
        return 0;
    parent_length = split - 1u;
    if (!parent_length)
        parent_length = 1u;
#if defined(_WIN32)
    if (split == 3u && path[1] == ':')
        parent_length = 3u;
#endif
    (void)memcpy(parent, path, parent_length);
    parent[parent_length] = 0;
    *leaf = path + split;
    return 1;
}

int cupidbuild_host_observer_require_file_request_binding(cupidbuild_host_observer_t *observer,
                                                          const char *logical,
                                                          const char *requested_absolute,
                                                          const char *physical_absolute,
                                                          int parent_identity_only) {
    cupidbuild_observer_entry_t *entry;
    cupidbuild_observer_handle_t parent = CUPIDBUILD_OBSERVER_INVALID;
    cupidbuild_observer_handle_t file = CUPIDBUILD_OBSERVER_INVALID;
    cupidbuild_observer_handle_t fresh = CUPIDBUILD_OBSERVER_INVALID;
    cupidbuild_observer_stat_t parent_before, current;
    char requested_parent[CUPIDBUILD_HOST_PATH_BYTES];
    char expected_parent[CUPIDBUILD_HOST_PATH_BYTES];
    char physical[CUPIDBUILD_HOST_PATH_BYTES];
    const char *requested_leaf, *expected_leaf;
    int valid;
    if (!observer || observer->error[0])
        return 0;
    entry = cupidbuild_observer_retained_file(observer, logical);
    valid =
        entry && !entry->directory && !entry->absent && entry->parent &&
        (parent_identity_only == 0 || parent_identity_only == 1) &&
        cupidbuild_observer_request_parent(requested_absolute, requested_parent, &requested_leaf) &&
        cupidbuild_observer_request_parent(physical_absolute, expected_parent, &expected_leaf) &&
        strcmp(requested_leaf, expected_leaf) == 0 &&
        cupidbuild_observer_input_name_valid(observer, requested_leaf);
    if (valid) {
        parent = cupidbuild_output_resolve_directory_captured(requested_parent, physical,
                                                              sizeof(physical), &parent_before);
        valid = parent != CUPIDBUILD_OBSERVER_INVALID && strcmp(physical, expected_parent) == 0 &&
                cupidbuild_observer_same(&entry->parent->captured, &parent_before, 1);
    }
    if (valid) {
        file = cupidbuild_observer_open_child(parent, requested_leaf, 0, 0);
        valid = file != CUPIDBUILD_OBSERVER_INVALID &&
                cupidbuild_observer_stat(file, 0, &current) &&
                cupidbuild_observer_same(&entry->captured, &current, 0);
    }
    if (valid) {
        fresh = cupidbuild_output_resolve_directory_captured(requested_parent, physical,
                                                             sizeof(physical), &current);
        valid = fresh != CUPIDBUILD_OBSERVER_INVALID && strcmp(physical, expected_parent) == 0 &&
                cupidbuild_observer_same(&parent_before, &current, parent_identity_only) &&
                cupidbuild_observer_stat(file, 0, &current) &&
                cupidbuild_observer_same(&entry->captured, &current, 0);
    }
    if (fresh != CUPIDBUILD_OBSERVER_INVALID && !cupidbuild_observer_close_handle(fresh))
        valid = 0;
    if (file != CUPIDBUILD_OBSERVER_INVALID && !cupidbuild_observer_close_handle(file))
        valid = 0;
    if (parent != CUPIDBUILD_OBSERVER_INVALID && !cupidbuild_observer_close_handle(parent))
        valid = 0;
    if (!valid)
        return cupidbuild_observer_fail(
            observer, "original file request no longer matches its retained binding");
    return 1;
}

typedef struct {
    size_t index;
    size_t requested_parent_length;
    size_t expected_parent_length;
    const char *requested_leaf;
    cupidbuild_observer_entry_t *entry;
} cupidbuild_request_group_row_t;

static int cupidbuild_request_group_parent_compare(const cupidbuild_host_file_request_t *requests,
                                                   const cupidbuild_request_group_row_t *left,
                                                   const cupidbuild_request_group_row_t *right) {
    size_t length = left->requested_parent_length < right->requested_parent_length
                        ? left->requested_parent_length
                        : right->requested_parent_length;
    int order = memcmp(requests[left->index].requested_absolute,
                       requests[right->index].requested_absolute, length);
    if (order)
        return order;
    return left->requested_parent_length < right->requested_parent_length   ? -1
           : left->requested_parent_length > right->requested_parent_length ? 1
                                                                            : 0;
}

static int cupidbuild_request_group_compare(const cupidbuild_host_file_request_t *requests,
                                            const cupidbuild_request_group_row_t *left,
                                            const cupidbuild_request_group_row_t *right) {
    int order = cupidbuild_request_group_parent_compare(requests, left, right);
    if (order)
        return order;
    return left->index < right->index ? -1 : left->index > right->index ? 1 : 0;
}

static void cupidbuild_request_group_sift(const cupidbuild_host_file_request_t *requests,
                                          cupidbuild_request_group_row_t *rows, size_t root,
                                          size_t count) {
    if (count < 2u)
        return;
    while (root <= (count - 2u) / 2u) {
        size_t child = root * 2u + 1u;
        cupidbuild_request_group_row_t temporary;
        if (child + 1u < count &&
            cupidbuild_request_group_compare(requests, &rows[child], &rows[child + 1u]) < 0)
            child++;
        if (cupidbuild_request_group_compare(requests, &rows[root], &rows[child]) >= 0)
            return;
        temporary = rows[root];
        rows[root] = rows[child];
        rows[child] = temporary;
        root = child;
    }
}

static void cupidbuild_request_group_sort(const cupidbuild_host_file_request_t *requests,
                                          cupidbuild_request_group_row_t *rows, size_t count) {
    size_t index;
    if (count < 2u)
        return;
    index = count / 2u;
    while (index)
        cupidbuild_request_group_sift(requests, rows, --index, count);
    index = count;
    while (index > 1u) {
        cupidbuild_request_group_row_t temporary;
        index--;
        temporary = rows[0];
        rows[0] = rows[index];
        rows[index] = temporary;
        cupidbuild_request_group_sift(requests, rows, 0u, index);
    }
}

static int cupidbuild_request_group_parent(cupidbuild_host_observer_t *observer,
                                           const cupidbuild_host_file_request_t *requests,
                                           const cupidbuild_request_group_row_t *rows, size_t begin,
                                           size_t end, cupidbuild_host_file_request_sink_t sink,
                                           void *context) {
    cupidbuild_observer_handle_t parent = CUPIDBUILD_OBSERVER_INVALID;
    cupidbuild_observer_handle_t fresh = CUPIDBUILD_OBSERVER_INVALID;
    cupidbuild_observer_handle_t file = CUPIDBUILD_OBSERVER_INVALID;
    cupidbuild_observer_stat_t before, current;
    char requested_parent[CUPIDBUILD_HOST_PATH_BYTES];
    char physical[CUPIDBUILD_HOST_PATH_BYTES];
    const cupidbuild_request_group_row_t *first = &rows[begin];
    size_t index;
    int identity_only = 1;
    int valid;
    (void)memcpy(requested_parent, requests[first->index].requested_absolute,
                 first->requested_parent_length);
    requested_parent[first->requested_parent_length] = 0;
    parent = cupidbuild_output_resolve_directory_captured(requested_parent, physical,
                                                          sizeof(physical), &before);
    valid = parent != CUPIDBUILD_OBSERVER_INVALID;
    for (index = begin; valid && index < end; index++) {
        const cupidbuild_request_group_row_t *row = &rows[index];
        const cupidbuild_host_file_request_t *request = &requests[row->index];
        valid = strlen(physical) == row->expected_parent_length &&
                memcmp(physical, request->physical_absolute, row->expected_parent_length) == 0 &&
                cupidbuild_observer_same(&row->entry->parent->captured, &before, 1);
        if (!request->parent_identity_only)
            identity_only = 0;
        if (valid) {
            file = cupidbuild_observer_open_child(parent, row->requested_leaf, 0, 0);
            valid = file != CUPIDBUILD_OBSERVER_INVALID &&
                    cupidbuild_observer_stat(file, 0, &current) &&
                    cupidbuild_observer_same(&row->entry->captured, &current, 0);
        }
        if (valid && sink)
            valid = sink(context, row->index, request);
        if (valid)
            valid = observer->error[0] == 0 && cupidbuild_observer_stat(file, 0, &current) &&
                    cupidbuild_observer_same(&row->entry->captured, &current, 0);
        if (file != CUPIDBUILD_OBSERVER_INVALID) {
            if (!cupidbuild_observer_close_handle(file))
                valid = 0;
            file = CUPIDBUILD_OBSERVER_INVALID;
        }
    }
    if (valid) {
        fresh = cupidbuild_output_resolve_directory_captured(requested_parent, physical,
                                                             sizeof(physical), &current);
        valid = fresh != CUPIDBUILD_OBSERVER_INVALID &&
                strlen(physical) == first->expected_parent_length &&
                memcmp(physical, requests[first->index].physical_absolute,
                       first->expected_parent_length) == 0 &&
                cupidbuild_observer_same(&before, &current, identity_only);
    }
    if (fresh != CUPIDBUILD_OBSERVER_INVALID && !cupidbuild_observer_close_handle(fresh))
        valid = 0;
    if (file != CUPIDBUILD_OBSERVER_INVALID && !cupidbuild_observer_close_handle(file))
        valid = 0;
    if (parent != CUPIDBUILD_OBSERVER_INVALID && !cupidbuild_observer_close_handle(parent))
        valid = 0;
    return valid;
}

#if defined(_WIN32)
/* Independent original-parent groups may allocate concurrently. The checked
 * runtime enables its heap mutex before any worker is created. */
typedef struct {
    size_t begin;
    size_t end;
    int valid;
    int checked;
} cupidbuild_request_parent_group_t;

typedef struct {
    cupidbuild_observer_thread_call_t call;
    cupidbuild_host_observer_t *observer;
    const cupidbuild_host_file_request_t *requests;
    const cupidbuild_request_group_row_t *rows;
    cupidbuild_request_parent_group_t *groups;
    size_t count;
    unsigned int index;
    volatile LONG complete;
} cupidbuild_request_parent_worker_t;

static DWORD cupidbuild_request_parent_worker(void *opaque) {
    cupidbuild_request_parent_worker_t *worker = (cupidbuild_request_parent_worker_t *)opaque;
    size_t index;
    for (index = worker->index; index < worker->count; index += CUPIDBUILD_OBSERVER_WORKERS) {
        cupidbuild_request_parent_group_t *group = &worker->groups[index];
        group->valid = cupidbuild_request_group_parent(
            worker->observer, worker->requests, worker->rows, group->begin, group->end,
            (cupidbuild_host_file_request_sink_t)0, (void *)0);
        group->checked = 1;
    }
    return 0u;
}

static int cupidbuild_request_parent_groups_parallel(cupidbuild_host_observer_t *observer,
                                                     const cupidbuild_host_file_request_t *requests,
                                                     const cupidbuild_request_group_row_t *rows,
                                                     size_t count, int *ran) {
    cupidbuild_request_parent_group_t *groups;
    cupidbuild_request_parent_worker_t workers[CUPIDBUILD_OBSERVER_WORKERS];
    HANDLE threads[CUPIDBUILD_OBSERVER_WORKERS];
    size_t begin = 0u, end, group_count = 0u, index;
    unsigned int worker, created = 0u;
    int valid = 1;
    *ran = 0;
    if (count > (size_t)-1 / sizeof(*groups))
        return 1;
    groups = (cupidbuild_request_parent_group_t *)calloc(count, sizeof(*groups));
    if (!groups)
        return 1;
    while (begin < count) {
        end = begin + 1u;
        while (end < count &&
               cupidbuild_request_group_parent_compare(requests, &rows[begin], &rows[end]) == 0)
            end++;
        groups[group_count].begin = begin;
        groups[group_count++].end = end;
        begin = end;
    }
    if (group_count < 256u) {
        free(groups);
        return 1;
    }
    *ran = 1;
#if defined(CUPID_HOSTED_I386_WINDOWS_H)
    cupid_runtime_heap_enable_threads();
#endif
    for (worker = 0u; worker < CUPIDBUILD_OBSERVER_WORKERS; worker++) {
        workers[worker].call.function = cupidbuild_request_parent_worker;
        workers[worker].call.argument = &workers[worker];
        workers[worker].call.complete = &workers[worker].complete;
        workers[worker].observer = observer;
        workers[worker].requests = requests;
        workers[worker].rows = rows;
        workers[worker].groups = groups;
        workers[worker].count = group_count;
        workers[worker].index = worker;
        workers[worker].complete = 0;
        threads[worker] = cupid_windows_observer_thread_open(&workers[worker].call);
        if (threads[worker] == (HANDLE)0)
            break;
        created++;
    }
    for (worker = 0u; worker < created; worker++) {
        if (WaitForSingleObject(threads[worker], INFINITE) != WAIT_OBJECT_0) {
            valid = 0;
            while (!cupid_windows_observer_thread_finished(&workers[worker].complete))
                (void)WaitForSingleObject(threads[worker], 1u);
        }
        if (!CloseHandle(threads[worker]))
            valid = 0;
    }
    for (index = 0u; index < group_count; index++) {
        if (!groups[index].checked)
            groups[index].valid = cupidbuild_request_group_parent(
                observer, requests, rows, groups[index].begin, groups[index].end,
                (cupidbuild_host_file_request_sink_t)0, (void *)0);
        if (!groups[index].valid)
            valid = 0;
    }
    free(groups);
    return valid;
}
#endif

int cupidbuild_host_observer_require_file_request_bindings(
    cupidbuild_host_observer_t *observer, const cupidbuild_host_file_request_t *requests,
    size_t count, cupidbuild_host_file_request_sink_t sink, void *context) {
    cupidbuild_request_group_row_t *rows = (cupidbuild_request_group_row_t *)0;
    char requested_parent[CUPIDBUILD_HOST_PATH_BYTES];
    char expected_parent[CUPIDBUILD_HOST_PATH_BYTES];
    size_t index, end;
    int valid = 1;
    if (!observer || observer->error[0])
        return 0;
    if (observer->stream_group_active)
        return cupidbuild_observer_fail(observer,
                                        "original request group cannot reenter active capture");
    if (count > (size_t)-1 / sizeof(*rows) || count > (size_t)-1 / sizeof(*requests) ||
        (count && !requests))
        return cupidbuild_observer_fail(observer, "invalid original request group storage");
    if (count)
        rows = (cupidbuild_request_group_row_t *)calloc(count, sizeof(*rows));
    if (count && !rows)
        return cupidbuild_observer_fail(observer, "original request group allocation failed");
    for (index = 0u; valid && index < count; index++) {
        const cupidbuild_host_file_request_t *request = &requests[index];
        const char *expected_leaf = (const char *)0;
        cupidbuild_request_group_row_t *row = &rows[index];
        row->index = index;
        row->entry = cupidbuild_observer_retained_file(observer, request->logical);
        valid = row->entry && !row->entry->directory && !row->entry->absent && row->entry->parent &&
                (request->parent_identity_only == 0 || request->parent_identity_only == 1) &&
                cupidbuild_observer_request_parent(request->requested_absolute, requested_parent,
                                                   &row->requested_leaf) &&
                cupidbuild_observer_request_parent(request->physical_absolute, expected_parent,
                                                   &expected_leaf) &&
                strcmp(row->requested_leaf, expected_leaf) == 0 &&
                cupidbuild_observer_input_name_valid(observer, row->requested_leaf);
        if (valid) {
            row->requested_parent_length = strlen(requested_parent);
            row->expected_parent_length = strlen(expected_parent);
        }
    }
    if (valid)
        cupidbuild_request_group_sort(requests, rows, count);
    if (valid) {
        observer->stream_group_active = 1;
#if defined(_WIN32)
        int ran = 0;
        if (!sink && count >= 256u)
            valid =
                cupidbuild_request_parent_groups_parallel(observer, requests, rows, count, &ran);
        if (!ran) {
#endif
            index = 0u;
            while (valid && index < count) {
                end = index + 1u;
                while (end < count && cupidbuild_request_group_parent_compare(
                                          requests, &rows[index], &rows[end]) == 0)
                    end++;
                valid = cupidbuild_request_group_parent(observer, requests, rows, index, end, sink,
                                                        context);
                index = end;
            }
#if defined(_WIN32)
        }
#endif
        observer->stream_group_active = 0;
    }
    free(rows);
    if (!valid)
        return cupidbuild_observer_fail(
            observer, "original request group no longer matches its retained bindings");
    return 1;
}

static int cupidbuild_observer_source_root_binding(cupidbuild_host_observer_t *observer) {
    cupidbuild_observer_handle_t handle;
    cupidbuild_observer_stat_t information;
    char physical[CUPIDBUILD_HOST_PATH_BYTES];
    int valid;
    if (!observer || observer->error[0])
        return 0;
    if (!observer->source_root_request)
        return 1;
    handle = cupidbuild_output_resolve_directory_captured(observer->source_root_request, physical,
                                                          sizeof(physical), &information);
    if (handle == CUPIDBUILD_OBSERVER_INVALID)
        return cupidbuild_observer_fail(observer, "source root request cannot be resolved");
    valid = observer->root && observer->source_root_physical &&
            strcmp(physical, observer->source_root_physical) == 0 &&
            cupidbuild_observer_same(&observer->root->captured, &information, 1);
    if (!cupidbuild_observer_close_handle(handle))
        valid = 0;
    if (!valid)
        return cupidbuild_observer_fail(
            observer, "source root request no longer selects the retained directory");
    return 1;
}

static int cupidbuild_observer_resolve_source_profile(const char *requested_root,
                                                      size_t entry_quota, int selection,
                                                      cupidbuild_host_observer_t **observer_out,
                                                      const char **physical_root_out) {
    cupidbuild_observer_handle_t handle = CUPIDBUILD_OBSERVER_INVALID;
    cupidbuild_observer_stat_t information;
    cupidbuild_host_observer_t *observer = (cupidbuild_host_observer_t *)0;
    char physical[CUPIDBUILD_HOST_PATH_BYTES];
    const char *cursor;
    unsigned int scalar;
    size_t length;
    int valid;
    if (observer_out)
        *observer_out = (cupidbuild_host_observer_t *)0;
    if (physical_root_out)
        *physical_root_out = (const char *)0;
    if (!observer_out || !physical_root_out || !requested_root || !requested_root[0] ||
        entry_quota == 0u || entry_quota == (size_t)-1)
        return 0;
    length = strlen(requested_root);
    if (length >= CUPIDBUILD_HOST_PATH_BYTES)
        return 0;
    cursor = requested_root;
    while (*cursor)
        if (!cupidbuild_observer_scalar(&cursor, &scalar))
            return 0;
#if defined(_WIN32)
    if (!((length >= 3u && requested_root[1] == ':' &&
           (requested_root[2] == '/' || requested_root[2] == '\\')) ||
          (length >= 2u && (requested_root[0] == '/' || requested_root[0] == '\\') &&
           (requested_root[1] == '/' || requested_root[1] == '\\'))))
        return 0;
#else
            if (requested_root[0] != '/')
                return 0;
#endif
    handle = cupidbuild_output_resolve_directory_captured(requested_root, physical,
                                                          sizeof(physical), &information);
    if (handle == CUPIDBUILD_OBSERVER_INVALID)
        return 0;
    if (selection) {
        observer = (cupidbuild_host_observer_t *)calloc(1u, sizeof(*observer));
        if (observer) {
            observer->entry_quota = entry_quota;
            observer->source_names = 1;
            observer->selection_only = 1;
            observer->root = cupidbuild_observer_add_captured(
                observer, (cupidbuild_observer_entry_t *)0, "", handle, 1, &information);
            handle = CUPIDBUILD_OBSERVER_INVALID;
        }
        valid = observer && observer->root;
    } else {
        valid = cupidbuild_host_observer_open_source_quota(physical, entry_quota, &observer) &&
                cupidbuild_observer_same(&observer->root->captured, &information, 1);
    }
    if (valid) {
        observer->source_root_request = (char *)malloc(length + 1u);
        observer->source_root_physical = (char *)malloc(strlen(physical) + 1u);
        valid = observer->source_root_request && observer->source_root_physical;
        if (valid) {
            (void)memcpy(observer->source_root_request, requested_root, length + 1u);
            (void)strcpy(observer->source_root_physical, physical);
            valid = selection == 2 || cupidbuild_observer_require_current(observer, 0);
        }
    }
    if (handle != CUPIDBUILD_OBSERVER_INVALID && !cupidbuild_observer_close_handle(handle))
        valid = 0;
    if (!valid) {
        (void)cupidbuild_host_observer_close(observer);
        return 0;
    }
    *observer_out = observer;
    *physical_root_out = observer->source_root_physical;
    return 1;
}

int cupidbuild_host_observer_resolve_source_root_quota(const char *requested_root,
                                                       size_t entry_quota,
                                                       cupidbuild_host_observer_t **observer_out,
                                                       const char **physical_root_out) {
    return cupidbuild_observer_resolve_source_profile(requested_root, entry_quota, 0, observer_out,
                                                      physical_root_out);
}

int cupidbuild_host_observer_resolve_source_selection_quota(
    const char *requested_root, size_t entry_quota, cupidbuild_host_observer_t **observer_out,
    const char **physical_root_out) {
    return cupidbuild_observer_resolve_source_profile(requested_root, entry_quota, 1, observer_out,
                                                      physical_root_out);
}

int cupidbuild_host_observer_begin_source_selection_quota(const char *requested_root,
                                                          size_t entry_quota,
                                                          cupidbuild_host_observer_t **observer_out,
                                                          const char **physical_root_out) {
    return cupidbuild_observer_resolve_source_profile(requested_root, entry_quota, 2, observer_out,
                                                      physical_root_out);
}

typedef struct {
    char directory[CUPIDBUILD_HOST_PATH_BYTES];
    char input_parent[CUPIDBUILD_HOST_PATH_BYTES];
    char output_parent[CUPIDBUILD_HOST_PATH_BYTES];
    char input[CUPIDBUILD_HOST_PATH_BYTES];
    char output[CUPIDBUILD_HOST_PATH_BYTES];
    char input_name[CUPIDBUILD_OBSERVER_NAME];
    char output_name[CUPIDBUILD_OBSERVER_NAME];
} cupidbuild_output_resolution_t;

static int cupidbuild_output_resolve_request(const char *root, const char *request, char *directory,
                                             size_t capacity, char *name) {
    char *separator;
    size_t index;
    int absolute;
    const char *cursor = request;
    unsigned int scalar;
    if (request == (const char *)0 || !request[0] || strlen(request) >= CUPIDBUILD_HOST_PATH_BYTES)
        return 0;
    while (*cursor)
        if (!cupidbuild_observer_scalar(&cursor, &scalar))
            return 0;
#if defined(_WIN32)
    absolute = request[0] == '/' || request[0] == '\\' ||
               (request[1] == ':' && (request[2] == '/' || request[2] == '\\'));
    if (!absolute && strchr(request, ':') != (const char *)0)
        return 0;
#else
            absolute = request[0] == '/';
#endif
    if (!(absolute ? cupidbuild_host_copy_text(directory, capacity, request)
                   : cupidbuild_host_join(directory, capacity, root, request)))
        return 0;
    for (index = 0u; directory[index]; index++) {
#if defined(_WIN32)
        if (directory[index] == '\\')
            directory[index] = '/';
#else
                if (directory[index] == '\\')
                    return 0;
#endif
    }
    separator = strrchr(directory, '/');
    if (separator == (char *)0 || !cupidbuild_observer_name_valid(separator + 1u) ||
        !cupidbuild_host_copy_text(name, CUPIDBUILD_OBSERVER_NAME, separator + 1u))
        return 0;
    if (separator == directory || (separator == directory + 2u && directory[1] == ':'))
        separator[1] = 0;
    else
        *separator = 0;
    return 1;
}

static const char *cupidbuild_output_below_root(const char *root, const char *path) {
    size_t index;
    size_t length = strlen(root);
    for (index = 0u; index < length; index++) {
        char left = root[index], right = path[index];
#if defined(_WIN32)
        left = cupidbuild_host_ascii_fold(left);
        right = cupidbuild_host_ascii_fold(right);
#endif
        if (!right || left != right)
            return (const char *)0;
    }
    if (length && root[length - 1u] == '/')
        return path + length;
    return path[length] == '/' ? path + length + 1u : (const char *)0;
}

static int cupidbuild_output_resolve_leaf(cupidbuild_observer_handle_t directory, const char *name,
                                          int optional) {
    cupidbuild_observer_handle_t handle = cupidbuild_observer_open_child(directory, name, 0, 0);
    cupidbuild_observer_stat_t information;
    int valid;
    if (handle == CUPIDBUILD_OBSERVER_INVALID)
        return optional &&
               cupidbuild_observer_issue(directory, name, 0) == CUPIDBUILD_OBSERVATION_MISSING;
    valid = cupidbuild_observer_stat(handle, 0, &information);
    return cupidbuild_observer_close_handle(handle) && valid;
}

int cupidbuild_host_output_parent_resolve_existing(const char *root, const char *source,
                                                   const char *output,
                                                   cupidbuild_host_output_parent_t **parent_out) {
    cupidbuild_host_output_parent_t *parent;
    cupidbuild_output_resolution_t *paths;
    cupidbuild_observer_handle_t handles[3];
    cupidbuild_observer_stat_t identities[3];
    const char *input_relative, *output_relative;
    const char *failure = "cannot resolve existing user-link directories";
    unsigned int index;
    int valid = 0;
    if (parent_out == (cupidbuild_host_output_parent_t **)0)
        return 0;
    *parent_out = (cupidbuild_host_output_parent_t *)0;
    parent = (cupidbuild_host_output_parent_t *)calloc(1u, sizeof(*parent));
    if (parent == (cupidbuild_host_output_parent_t *)0)
        return 0;
    *parent_out = parent;
    paths = (cupidbuild_output_resolution_t *)calloc(1u, sizeof(*paths));
    for (index = 0u; index < 3u; index++)
        handles[index] = CUPIDBUILD_OBSERVER_INVALID;
    if (paths == (cupidbuild_output_resolution_t *)0 || root == (const char *)0 || !root[0] ||
        strlen(root) >= CUPIDBUILD_HOST_PATH_BYTES)
        goto resolved;
    handles[0] = cupidbuild_output_resolve_directory(root, parent->resolved_root,
                                                     sizeof(parent->resolved_root));
    if (handles[0] == CUPIDBUILD_OBSERVER_INVALID)
        goto resolved;
    if (!cupidbuild_output_resolve_request(parent->resolved_root, source, paths->directory,
                                           sizeof(paths->directory), paths->input_name))
        goto resolved;
    handles[1] = cupidbuild_output_resolve_directory(paths->directory, paths->input_parent,
                                                     sizeof(paths->input_parent));
    if (handles[1] == CUPIDBUILD_OBSERVER_INVALID)
        goto resolved;
    if (!cupidbuild_output_resolve_request(parent->resolved_root, output, paths->directory,
                                           sizeof(paths->directory), paths->output_name))
        goto resolved;
    handles[2] = cupidbuild_output_resolve_directory(paths->directory, paths->output_parent,
                                                     sizeof(paths->output_parent));
    if (handles[2] == CUPIDBUILD_OBSERVER_INVALID)
        goto resolved;
    for (index = 0u; index < 3u; index++)
        if (!cupidbuild_observer_stat(handles[index], 1, &identities[index]))
            goto resolved;
    failure = "user-link paths escape the repository or have different physical parents";
    if (!cupidbuild_observer_same(&identities[1], &identities[2], 1) ||
        !cupidbuild_host_join(paths->input, sizeof(paths->input), paths->input_parent,
                              paths->input_name) ||
        !cupidbuild_host_join(paths->output, sizeof(paths->output), paths->output_parent,
                              paths->output_name))
        goto resolved;
    input_relative = cupidbuild_output_below_root(parent->resolved_root, paths->input);
    output_relative = cupidbuild_output_below_root(parent->resolved_root, paths->output);
    if (input_relative == (const char *)0 || output_relative == (const char *)0 ||
        !cupidbuild_host_copy_text(parent->resolved_source, sizeof(parent->resolved_source),
                                   input_relative))
        goto resolved;
    failure = "user-link leaves must be regular files without links";
    if (!cupidbuild_output_resolve_leaf(handles[1], paths->input_name, 0) ||
        !cupidbuild_output_resolve_leaf(handles[2], paths->output_name, 1))
        goto resolved;
    failure = "resolved user-link directories changed before retention";
    if (!cupidbuild_output_parent_fill(parent, parent->resolved_root, output_relative, 0) ||
        !cupidbuild_observer_same(&identities[0], &parent->observer->root->captured, 1) ||
        !cupidbuild_observer_same(&identities[2], &parent->leaf->captured, 1))
        goto resolved;
    valid = 1;
resolved:
    for (index = 0u; index < 3u; index++) {
        if (handles[index] != CUPIDBUILD_OBSERVER_INVALID &&
            !cupidbuild_observer_close_handle(handles[index])) {
            valid = 0;
            failure = "cannot close user-link resolution handle";
        }
    }
    free(paths);
    if (!valid)
        return cupidbuild_output_parent_fail(parent, failure);
    parent->resolved = 1;
    return cupidbuild_host_output_parent_require_current(parent);
}

const char *
cupidbuild_host_output_parent_resolved_root(const cupidbuild_host_output_parent_t *parent) {
    return parent && parent->resolved && !parent->error[0] ? parent->resolved_root
                                                           : (const char *)0;
}

const char *
cupidbuild_host_output_parent_resolved_source(const cupidbuild_host_output_parent_t *parent) {
    return parent && parent->resolved && !parent->error[0] ? parent->resolved_source
                                                           : (const char *)0;
}

const char *
cupidbuild_host_output_parent_resolved_output(const cupidbuild_host_output_parent_t *parent) {
    return parent && parent->resolved && !parent->error[0] ? parent->output : (const char *)0;
}

static int cupidbuild_output_parent_attach(cupidbuild_host_output_parent_t *parent,
                                           cupidbuild_host_transaction_t *transaction) {
    cupidbuild_observer_stat_t root, leaf;
    const char *name;
    if (!cupidbuild_host_output_parent_require_current(parent) ||
        transaction == (cupidbuild_host_transaction_t *)0)
        return 0;
    name = strrchr(parent->output, '/');
    name = name ? name + 1u : parent->output;
    if (transaction->prepared_output_parent != (cupidbuild_host_output_parent_t *)0 ||
        strcmp(name, transaction->output_name) != 0 ||
#if defined(_WIN32)
        !cupidbuild_observer_stat(transaction->repository_root_handle, 1, &root) ||
        !cupidbuild_observer_stat(transaction->output_parent_handle, 1, &leaf) ||
#else
                !cupidbuild_observer_stat(transaction->repository_root_descriptor, 1, &root) ||
                !cupidbuild_observer_stat(transaction->output_parent_descriptor, 1, &leaf) ||
#endif
        !cupidbuild_observer_same(&parent->observer->root->captured, &root, 1) ||
        !cupidbuild_observer_same(&parent->leaf->captured, &leaf, 1)) {
        return cupidbuild_output_parent_fail(parent,
                                             "prepared parent and transaction binding differ");
    }
    transaction->prepared_output_parent = parent;
    return 1;
}

int cupidbuild_host_output_parent_bind(cupidbuild_host_output_parent_t *parent,
                                       cupidbuild_host_transaction_t *transaction) {
    return cupidbuild_output_parent_attach(parent, transaction) &&
           cupidbuild_host_require_publication_boundary(transaction);
}

int cupidbuild_host_output_parent_close(cupidbuild_host_output_parent_t *parent) {
    int result = 1;
    if (parent != (cupidbuild_host_output_parent_t *)0) {
        result = cupidbuild_host_observer_close(parent->observer);
        free(parent);
    }
    return result;
}

int cupidbuild_host_absolute_root(const char *root, char *output, size_t capacity) {
    if (output == (char *)0 || capacity == 0u)
        return 0;
    output[0] = 0;
    if (root == (const char *)0 || root[0] == 0 ||
        !cupidbuild_host_absolute_directory(output, capacity, root)) {
        output[0] = 0;
        return 0;
    }
    return 1;
}

/* The image command records absence of the complete requested pathname. Parent
 * aliases may resolve, disappear or appear while that leaf remains absent. */
int cupidbuild_host_source_missing(const char *requested_absolute) {
    const char *cursor;
    unsigned int scalar;
    size_t length;
    if (!requested_absolute || !requested_absolute[0])
        return 0;
    length = strlen(requested_absolute);
    if (length >= CUPIDBUILD_HOST_PATH_BYTES)
        return 0;
    cursor = requested_absolute;
    while (*cursor)
        if (!cupidbuild_observer_scalar(&cursor, &scalar))
            return 0;
#if defined(_WIN32)
    if (!((length >= 3u &&
           ((requested_absolute[0] >= 'A' && requested_absolute[0] <= 'Z') ||
            (requested_absolute[0] >= 'a' && requested_absolute[0] <= 'z')) &&
           requested_absolute[1] == ':' &&
           (requested_absolute[2] == '/' || requested_absolute[2] == '\\')) ||
          (length >= 2u && (requested_absolute[0] == '/' || requested_absolute[0] == '\\') &&
           (requested_absolute[1] == '/' || requested_absolute[1] == '\\'))))
        return 0;
#else
            if (requested_absolute[0] != '/')
                return 0;
#endif
    return cupidbuild_host_path_missing(requested_absolute);
}

int cupidbuild_host_image_output_prepare(const char *absolute,
    cupidbuild_host_observer_t **observer_out, const char **physical_out, const char **leaf_out) {
    char parent[CUPIDBUILD_HOST_PATH_BYTES];
    char ancestor[CUPIDBUILD_HOST_PATH_BYTES];
    char next[CUPIDBUILD_HOST_PATH_BYTES];
    char physical[CUPIDBUILD_HOST_PATH_BYTES];
    cupidbuild_host_output_parent_t *preparation = NULL;
    cupidbuild_host_observer_t *observer = NULL;
    cupidbuild_observer_handle_t handle;
    cupidbuild_observer_stat_t ancestor_snapshot;
    const char *resolved = NULL;
    const char *leaf;
    const char *relative;
    int valid;

    if (observer_out)
        *observer_out = NULL;
    if (physical_out)
        *physical_out = NULL;
    if (leaf_out)
        *leaf_out = NULL;
    if (!observer_out || !physical_out || !leaf_out ||
        !cupidbuild_observer_request_parent(absolute, parent, &leaf) ||
        !cupidbuild_host_path_is_relative_safe(leaf) ||
        !cupidbuild_host_copy_text(ancestor, sizeof(ancestor), parent))
        return 0;

    for (;;) {
        const char *component;
        handle = cupidbuild_output_resolve_directory_captured(ancestor, physical, sizeof(physical),
                                                              &ancestor_snapshot);
        if (handle != CUPIDBUILD_OBSERVER_INVALID)
            break;
        if (!cupidbuild_host_source_missing(ancestor) ||
            !cupidbuild_observer_request_parent(ancestor, next, &component) ||
            !cupidbuild_host_copy_text(ancestor, sizeof(ancestor), next))
            return 0;
    }
    valid = cupidbuild_observer_close_handle(handle);
    if (valid && strcmp(parent, ancestor) != 0) {
        int windows = cupidbuild_host_execution_format() == 2u;
        char *unit;
        relative = absolute + strlen(ancestor);
        while (*relative == '/' || (windows && *relative == '\\'))
            relative++;
        valid = cupidbuild_host_copy_text(next, sizeof(next), relative);
        if (valid && windows) {
            for (unit = next; *unit; unit++) {
                if (*unit == '\\')
                    *unit = '/';
            }
        }
        if (valid)
            valid = cupidbuild_host_output_parent_prepare(physical, next, &preparation);
    }
    if (valid)
        valid = cupidbuild_host_observer_resolve_source_root_quota(
                    parent, CUPIDBUILD_HOST_PATH_BYTES, &observer, &resolved) &&
                cupidbuild_output_resolve_leaf(observer->root->handle, leaf, 1);
    if (valid) {
        const cupidbuild_observer_stat_t *expected =
            preparation ? &preparation->leaf->captured : &ancestor_snapshot;
        valid = cupidbuild_observer_same(expected, &observer->root->captured, 1);
    }
    if (!cupidbuild_host_output_parent_close(preparation))
        valid = 0;
    if (!valid) {
        (void)cupidbuild_host_observer_close(observer);
        return 0;
    }
    *observer_out = observer;
    *physical_out = resolved;
    *leaf_out = leaf;
    return 1;
}
