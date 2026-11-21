/**
 * @file cupidbuild_host_observer_inventory.inc
 * @brief Capture grouped source files and directory membership.
 */

int cupidbuild_host_observer_file_stream_retained(
    cupidbuild_host_observer_t *observer, const char *logical, uint64_t limit,
    cupidbuild_host_stream_sink_t sink, void *context,
    cupidbuild_host_stream_observation_t *result_out) {
    cupidbuild_observer_entry_t *entry;
    cupidbuild_observer_entry_t streaming;
    cupidbuild_observer_handle_t fresh = CUPIDBUILD_OBSERVER_INVALID;
    unsigned char digest[32];
    int valid = 0;
    if (result_out != (cupidbuild_host_stream_observation_t *)0)
        (void)memset(result_out, 0, sizeof(*result_out));
    if (observer == (cupidbuild_host_observer_t *)0 || observer->error[0] != 0)
        return 0;
    entry = cupidbuild_observer_retained_file(observer, logical);
    if (result_out == (cupidbuild_host_stream_observation_t *)0 ||
        entry == (cupidbuild_observer_entry_t *)0 || entry->directory ||
        entry->parent == (cupidbuild_observer_entry_t *)0)
        return cupidbuild_observer_fail(observer,
                                        "streaming requires an already retained regular file");
    if (!cupidbuild_observer_retained_bindings(observer, entry))
        return cupidbuild_observer_fail(observer,
                                        "retained stream bindings changed before capture");
    fresh = cupidbuild_observer_open_child(entry->parent->handle, entry->name, 0, 1);
    if (fresh == CUPIDBUILD_OBSERVER_INVALID)
        return cupidbuild_observer_fail(observer, "cannot reopen retained file for streaming");
    streaming = *entry;
    streaming.handle = fresh;
    valid = cupidbuild_observer_stream_read(&streaming, limit, sink, context, digest) &&
            (!entry->payload || memcmp(digest, entry->digest, sizeof(digest)) == 0) &&
            cupidbuild_observer_retained_bindings(observer, entry);
    if (!valid || !cupidbuild_observer_close_handle(entry->handle)) {
        (void)cupidbuild_observer_close_handle(fresh);
        return cupidbuild_observer_fail(observer,
                                        "retained stream changed, exceeded its limit or failed");
    }
    entry->handle = fresh;
    entry->payload = 2;
    (void)memcpy(entry->digest, digest, sizeof(digest));
    result_out->size = entry->captured.size;
    (void)memcpy(result_out->sha256, digest, sizeof(digest));
    return 1;
}

static int cupidbuild_observer_comparison_fail(cupidbuild_host_observer_t *left,
                                               cupidbuild_host_observer_t *right) {
    (void)cupidbuild_observer_fail(left, "invalid retained identity comparison");
    (void)cupidbuild_observer_fail(right, "invalid retained identity comparison");
    return 0;
}

static int cupidbuild_observer_compare(cupidbuild_host_observer_t *left, const char *left_logical,
                                       cupidbuild_host_observer_t *right, const char *right_logical,
                                       int root, int captured, int *same_out) {
    cupidbuild_observer_entry_t *left_entry;
    cupidbuild_observer_entry_t *right_entry;
    if (same_out != (int *)0)
        *same_out = 0;
    if (same_out == (int *)0 || left == (cupidbuild_host_observer_t *)0 ||
        right == (cupidbuild_host_observer_t *)0 || left->error[0] || right->error[0])
        return cupidbuild_observer_comparison_fail(left, right);
    left_entry = root ? left->root : cupidbuild_observer_retained_file(left, left_logical);
    right_entry = root ? right->root : cupidbuild_observer_retained_file(right, right_logical);
    if (!left_entry || !right_entry ||
        (!root && (left_entry->directory || right_entry->directory)) ||
        (!captured && (!cupidbuild_host_observer_require_unchanged(left) ||
                       !cupidbuild_host_observer_require_unchanged(right))))
        return cupidbuild_observer_comparison_fail(left, right);
    *same_out = left_entry->captured.device == right_entry->captured.device &&
                left_entry->captured.identity == right_entry->captured.identity;
    return 1;
}

int cupidbuild_host_observer_roots_same(cupidbuild_host_observer_t *left,
                                        cupidbuild_host_observer_t *right, int *same_out) {
    return cupidbuild_observer_compare(left, (const char *)0, right, (const char *)0, 1, 0,
                                       same_out);
}

int cupidbuild_host_observer_files_same(cupidbuild_host_observer_t *left, const char *left_logical,
                                        cupidbuild_host_observer_t *right,
                                        const char *right_logical, int *same_out) {
    return cupidbuild_observer_compare(left, left_logical, right, right_logical, 0, 0, same_out);
}

int cupidbuild_host_observer_roots_captured_same(cupidbuild_host_observer_t *left,
                                                 cupidbuild_host_observer_t *right, int *same_out) {
    return cupidbuild_observer_compare(left, (const char *)0, right, (const char *)0, 1, 1,
                                       same_out);
}

int cupidbuild_host_observer_file_bindings_captured_same(cupidbuild_host_observer_t *left,
                                                         const char *left_logical,
                                                         cupidbuild_host_observer_t *right,
                                                         const char *right_logical, int *same_out) {
    cupidbuild_observer_entry_t *a;
    cupidbuild_observer_entry_t *b;
    if (same_out)
        *same_out = 0;
    if (!same_out || !left || !right || left->error[0] || right->error[0])
        return cupidbuild_observer_comparison_fail(left, right);
    a = cupidbuild_observer_retained_file(left, left_logical);
    b = cupidbuild_observer_retained_file(right, right_logical);
    if (!a || !b || a->directory || b->directory || !a->parent || !b->parent)
        return cupidbuild_observer_comparison_fail(left, right);
    *same_out = a->captured.device == b->captured.device &&
                a->captured.identity == b->captured.identity &&
                a->parent->captured.device == b->parent->captured.device &&
                a->parent->captured.identity == b->parent->captured.identity;
    return 1;
}

int cupidbuild_host_observer_file_parents_captured_same(cupidbuild_host_observer_t *left,
                                                        const char *left_logical,
                                                        cupidbuild_host_observer_t *right,
                                                        const char *right_logical, int *same_out) {
    cupidbuild_observer_entry_t *left_entry;
    cupidbuild_observer_entry_t *right_entry;
    if (same_out)
        *same_out = 0;
    if (!same_out || !left || !right || left->error[0] || right->error[0])
        return cupidbuild_observer_comparison_fail(left, right);
    left_entry = cupidbuild_observer_retained_file(left, left_logical);
    right_entry = cupidbuild_observer_retained_file(right, right_logical);
    if (!left_entry || !right_entry || left_entry->directory || right_entry->directory ||
        !left_entry->parent || !right_entry->parent)
        return cupidbuild_observer_comparison_fail(left, right);
    *same_out = left_entry->parent->captured.device == right_entry->parent->captured.device &&
                left_entry->parent->captured.identity == right_entry->parent->captured.identity;
    return 1;
}

int cupidbuild_host_observer_file(cupidbuild_host_observer_t *observer, const char *logical,
                                  size_t limit, unsigned char **bytes_out, uint64_t *size_out) {
    cupidbuild_observer_entry_t *entry;
    unsigned char *bytes = (unsigned char *)0;
    if (bytes_out != (unsigned char **)0)
        *bytes_out = (unsigned char *)0;
    if (size_out != (uint64_t *)0)
        *size_out = 0u;
    if (observer == (cupidbuild_host_observer_t *)0 || observer->error[0] != 0)
        return 0;
    if (size_out == (uint64_t *)0 || logical == (const char *)0 || logical[0] == 0)
        return cupidbuild_observer_fail(observer, "invalid file observation arguments");
    entry = cupidbuild_observer_walk(observer, observer->root, logical, 0,
                                     bytes_out != (unsigned char **)0);
    if (entry == (cupidbuild_observer_entry_t *)0)
        return cupidbuild_observer_fail(observer, "unsafe or unavailable file observation");
    if (bytes_out != (unsigned char **)0) {
        if (!cupidbuild_observer_read(entry, limit, &bytes))
            return cupidbuild_observer_fail(observer,
                                            "payload exceeds limit or changed during read");
        cupidbuild_sha256(bytes, (size_t)entry->captured.size, entry->digest);
        entry->payload = 1;
        *bytes_out = bytes;
    }
    *size_out = entry->captured.size;
    return 1;
}

int cupidbuild_host_observer_kind(cupidbuild_host_observer_t *observer, const char *logical,
                                  cupidbuild_host_entry_kind_t *kind_out) {
    cupidbuild_observer_entry_t *entry;
    if (kind_out != (cupidbuild_host_entry_kind_t *)0)
        *kind_out = CUPIDBUILD_ENTRY_NONE;
    if (observer == (cupidbuild_host_observer_t *)0 || observer->error[0] != 0)
        return 0;
    if (logical == (const char *)0 || kind_out == (cupidbuild_host_entry_kind_t *)0)
        return cupidbuild_observer_fail(observer, "invalid kind observation arguments");
    entry = cupidbuild_observer_walk(observer, observer->root, logical, -1, 0);
    if (entry == (cupidbuild_observer_entry_t *)0)
        return cupidbuild_observer_fail(observer, "unsafe or unavailable kind observation");
    *kind_out = entry->directory ? CUPIDBUILD_ENTRY_DIRECTORY : CUPIDBUILD_ENTRY_FILE;
    return 1;
}

int cupidbuild_host_observer_files(cupidbuild_host_observer_t *observer,
                                   const char *const *logical_paths, size_t count,
                                   cupidbuild_host_file_observation_t *results) {
    size_t index;
    int ok = 1;
    if (count > CUPIDBUILD_OBSERVER_ENTRIES)
        return cupidbuild_observer_fail(observer, "file observation batch exceeds limit");
    if (results != (cupidbuild_host_file_observation_t *)0)
        (void)memset(results, 0, count * sizeof(*results));
    if (observer == (cupidbuild_host_observer_t *)0 || observer->error[0] != 0)
        return 0;
    if (count != 0u && (logical_paths == (const char *const *)0 ||
                        results == (cupidbuild_host_file_observation_t *)0))
        return cupidbuild_observer_fail(observer, "invalid file observation batch arguments");
    for (index = 0u; index < count; index++) {
        cupidbuild_observer_entry_t *entry;
        const char *logical = logical_paths[index];
        observer->last_issue = CUPIDBUILD_OBSERVATION_UNAVAILABLE;
        results[index].issue = CUPIDBUILD_OBSERVATION_UNAVAILABLE;
        if (logical == (const char *)0 || logical[0] == 0) {
            (void)cupidbuild_observer_fail(observer, "invalid file observation batch path");
            ok = 0;
            continue;
        }
        /* A failed row never clears poison. Internal walks may still collect later
         * diagnostic rows; no public operation can recover this observer. */
        entry = cupidbuild_observer_walk(observer, observer->root, logical, 0, 0);
        if (entry == (cupidbuild_observer_entry_t *)0) {
            results[index].issue = observer->last_issue;
            (void)cupidbuild_observer_fail(observer, "unsafe or unavailable batch observation");
            ok = 0;
            continue;
        }
        results[index].size = entry->captured.size;
        results[index].observed = 1;
        results[index].issue = CUPIDBUILD_OBSERVATION_OK;
    }
    return ok;
}

typedef int (*cupidbuild_observer_member_sink_t)(void *context, const char *name);

typedef struct {
    cupidbuild_host_observer_t *observer;
    cupidbuild_observer_entry_t *entry;
    unsigned char *seen;
} cupidbuild_observer_members_context_t;

static int cupidbuild_observer_member(void *context, const char *name) {
    cupidbuild_observer_members_context_t *members =
        (cupidbuild_observer_members_context_t *)context;
    cupidbuild_observer_entry_t *entry = members->entry;
    size_t index;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return 1;
    if (!cupidbuild_observer_input_name_valid(members->observer, name))
        return 0;
    for (index = 0u; index < entry->member_count; index++) {
        if (strcmp(name, entry->members[index]) == 0) {
            if (members->seen[index])
                return 0;
            members->seen[index] = 1u;
            return 1;
        }
    }
    return 0;
}

static int cupidbuild_observer_enumerate(cupidbuild_observer_entry_t *entry,
                                         cupidbuild_observer_member_sink_t sink, void *context) {
    int valid = 1;
#if defined(_WIN32)
    int restart = 1;
    while (valid) {
        unsigned char storage[2055];
        unsigned char *info = storage + ((8u - ((size_t)storage & 7u)) & 7u);
        cupidbuild_windows_io_status_t status;
        char name[CUPIDBUILD_OBSERVER_NAME];
        size_t length = 0u;
        size_t offset;
        size_t name_bytes;
        long result;
        (void)memset(&status, 0, sizeof(status));
        result = cupid_windows_nt_query_directory_file(
            entry->handle, (HANDLE)0, (void *)0, (void *)0, &status, info, 2048u, 38u, 1u,
            (cupidbuild_windows_unicode_string_t *)0, restart ? 1u : 0u);
        restart = 0;
        if ((unsigned long)result == 0x80000006u)
            break;
        if (result < 0 || status.information < 80u || status.information > 2048u) {
            valid = 0;
            break;
        }
        name_bytes = cupidbuild_host_windows_u32(info + 60u);
        if (name_bytes == 0u || (name_bytes & 1u) ||
            name_bytes > (size_t)status.information - 80u ||
            cupidbuild_host_windows_u32(info) != 0u) {
            valid = 0;
            break;
        }
        for (offset = 0u; offset < name_bytes; offset += 2u) {
            unsigned int scalar = info[80u + offset] | ((unsigned int)info[81u + offset] << 8u);
            if (scalar >= 0xd800u && scalar <= 0xdbffu) {
                unsigned int low;
                offset += 2u;
                if (offset >= name_bytes) {
                    valid = 0;
                    break;
                }
                low = info[80u + offset] | ((unsigned int)info[81u + offset] << 8u);
                if (low < 0xdc00u || low > 0xdfffu) {
                    valid = 0;
                    break;
                }
                scalar = 0x10000u + ((scalar - 0xd800u) << 10u) + low - 0xdc00u;
            } else if (scalar >= 0xdc00u && scalar <= 0xdfffu) {
                valid = 0;
                break;
            }
            if (scalar == 0u || length + 4u >= sizeof(name)) {
                valid = 0;
                break;
            }
            if (scalar < 0x80u)
                name[length++] = (char)scalar;
            else {
                if (scalar >= 0x10000u)
                    name[length++] = (char)(0xf0u | (scalar >> 18u));
                else if (scalar >= 0x800u)
                    name[length++] = (char)(0xe0u | (scalar >> 12u));
                else
                    name[length++] = (char)(0xc0u | (scalar >> 6u));
                if (scalar >= 0x10000u)
                    name[length++] = (char)(0x80u | ((scalar >> 12u) & 63u));
                if (scalar >= 0x800u)
                    name[length++] = (char)(0x80u | ((scalar >> 6u) & 63u));
                name[length++] = (char)(0x80u | (scalar & 63u));
            }
        }
        name[length] = 0;
        if (valid)
            valid = sink(context, name);
    }
#elif defined(CUPIDBUILD_CUSTOM_LINUX)
            if (cupid_linux_syscall3(19u, (unsigned int)entry->handle, 0u, 0u) != 0)
                valid = 0;
            while (valid) {
                unsigned char buffer[4096];
                size_t offset = 0u;
                int count = cupid_linux_syscall3(CUPIDBUILD_LINUX_SYS_GETDENTS64,
                                                 (unsigned int)entry->handle, (unsigned int)buffer,
                                                 sizeof(buffer));
                if (count == -4)
                    continue;
                if (count == 0)
                    break;
                if (count < 0 || count > (int)sizeof(buffer)) {
                    valid = 0;
                    break;
                }
                while (offset < (size_t)count) {
                    size_t length;
                    size_t end;
                    if ((size_t)count - offset < 20u) {
                        valid = 0;
                        break;
                    }
                    length = cupidbuild_linux_u16(buffer + offset + 16u);
                    if (length < 20u || length > (size_t)count - offset) {
                        valid = 0;
                        break;
                    }
                    end = 19u;
                    while (end < length && buffer[offset + end] != 0u)
                        end++;
                    if (end == length || !sink(context, (const char *)(buffer + offset + 19u))) {
                        valid = 0;
                        break;
                    }
                    offset += length;
                }
            }
#else
    int descriptor = dup(entry->handle);
    DIR *directory = descriptor >= 0 ? fdopendir(descriptor) : (DIR *)0;
    if (directory == (DIR *)0) {
        if (descriptor >= 0)
            (void)close(descriptor);
        valid = 0;
    } else {
        struct dirent *item;
        rewinddir(directory);
        while (valid) {
            errno = 0;
            item = readdir(directory);
            if (item == (struct dirent *)0) {
                if (errno != 0)
                    valid = 0;
                break;
            }
            valid = sink(context, item->d_name);
        }
        if (closedir(directory) != 0)
            valid = 0;
    }
#endif
    return valid;
}

typedef struct {
    const cupidbuild_observer_seed_members_t *members;
    const char *suffix;
    unsigned int seen;
} cupidbuild_observer_seed_scan_t;

static int cupidbuild_observer_seed_member(void *context, const char *name) {
    cupidbuild_observer_seed_scan_t *scan = (cupidbuild_observer_seed_scan_t *)context;
    unsigned int index;
    if (!cupidbuild_host_name_has_suffix(name, scan->suffix))
        return 1;
    for (index = 0u; index < scan->members->count; index++) {
        unsigned int bit = 1u << index;
        if (!strcmp(name, scan->members->names[index])) {
            if (scan->seen & bit)
                return 0;
            scan->seen |= bit;
            return 1;
        }
    }
    return 0;
}

static const char *cupidbuild_observer_seed_suffix(void) {
    return cupidbuild_host_execution_format() == 2u ? ".exe" : ".elf";
}

static int cupidbuild_observer_seed_members_current(cupidbuild_observer_entry_t *entry) {
    cupidbuild_observer_seed_scan_t scan;
    if (!entry->seed_members)
        return 1;
    scan.members = entry->seed_members;
    scan.suffix = cupidbuild_observer_seed_suffix();
    scan.seen = 0u;
    return cupidbuild_observer_enumerate(entry, cupidbuild_observer_seed_member, &scan) &&
           scan.seen == (1u << scan.members->count) - 1u;
}

int cupidbuild_host_observer_seed_directory(
    cupidbuild_host_observer_t *observer, const char *logical, const char *const *expected,
    size_t count) {
    cupidbuild_observer_entry_t *entry;
    const char *suffix = cupidbuild_observer_seed_suffix();
    size_t index, other;
    if (!observer || observer->error[0])
        return 0;
    if (observer->selection_only || observer->stream_group_active || !logical || !expected ||
        !count || count > CUPIDBUILD_OBSERVER_SEED_TOOLS)
        return cupidbuild_observer_fail(observer, "invalid seed directory observation");
    for (index = 0u; index < count; index++) {
        if (!cupidbuild_observer_input_name_valid(observer, expected[index]) ||
            !cupidbuild_host_name_has_suffix(expected[index], suffix))
            return cupidbuild_observer_fail(observer, "invalid seed executable name");
        for (other = 0u; other < index; other++) {
            if (!strcmp(expected[index], expected[other]))
                return cupidbuild_observer_fail(observer, "duplicate seed executable name");
        }
    }
    /* Reuse the directory that already retains the manifest and its siblings. */
    entry = *logical ? cupidbuild_observer_retained_path(observer, logical, 1) : observer->root;
    if (!entry && *logical)
        entry = cupidbuild_observer_walk(observer, observer->root, logical, 1, 0);
    if (!entry || entry->seed_members)
        return cupidbuild_observer_fail(observer, "seed directory cannot be observed again");
    entry->seed_members = (cupidbuild_observer_seed_members_t *)calloc(1u, sizeof(*entry->seed_members));
    if (!entry->seed_members)
        return cupidbuild_observer_fail(observer, "seed directory observation allocation failed");
    entry->seed_members->count = (unsigned int)count;
    for (index = 0u; index < count; index++) {
        size_t size = strlen(expected[index]) + 1u;
        entry->seed_members->names[index] = (char *)malloc(size);
        if (!entry->seed_members->names[index])
            return cupidbuild_observer_fail(observer, "seed executable name allocation failed");
        (void)memcpy(entry->seed_members->names[index], expected[index], size);
    }
    if (!cupidbuild_observer_seed_members_current(entry))
        return cupidbuild_observer_fail(observer, "seed executable membership differs from expected set");
    return 1;
}

static int cupidbuild_observer_members(cupidbuild_host_observer_t *observer,
                                       cupidbuild_observer_entry_t *entry) {
    cupidbuild_observer_members_context_t members;
    size_t index;
    int valid;
    members.observer = observer;
    members.entry = entry;
    members.seen = (unsigned char *)calloc(entry->member_count + 1u, 1u);
    if (members.seen == (unsigned char *)0)
        return 0;
    valid = cupidbuild_observer_enumerate(entry, cupidbuild_observer_member, &members);
    for (index = 0u; valid && index < entry->member_count; index++)
        if (!members.seen[index])
            valid = 0;
    free(members.seen);
    return valid;
}

int cupidbuild_host_observer_directory(cupidbuild_host_observer_t *observer, const char *logical,
                                       const char *const *expected, size_t expected_count) {
    cupidbuild_observer_entry_t *entry;
    size_t index;
    size_t other;
    if (observer == (cupidbuild_host_observer_t *)0 || observer->error[0] != 0)
        return 0;
    if (logical == (const char *)0 ||
        expected_count > CUPIDBUILD_OBSERVER_ENTRIES - observer->member_count ||
        (expected_count != 0u && expected == (const char *const *)0))
        return cupidbuild_observer_fail(observer, "invalid directory observation arguments");
    for (index = 0u; index < expected_count; index++) {
        if (!cupidbuild_observer_input_name_valid(observer, expected[index]))
            return cupidbuild_observer_fail(observer, "unsafe expected directory member");
        for (other = 0u; other < index; other++)
            if (strcmp(expected[index], expected[other]) == 0)
                return cupidbuild_observer_fail(observer, "duplicate expected directory member");
    }
    entry = logical[0] != 0 ? cupidbuild_observer_walk(observer, observer->root, logical, 1, 0)
                            : observer->root;
    if (entry == (cupidbuild_observer_entry_t *)0)
        return cupidbuild_observer_fail(observer, "unsafe or unavailable directory observation");
    if (entry->membership) {
        if (!cupidbuild_observer_members(observer, entry))
            return cupidbuild_observer_fail(observer, "previous directory observation changed");
        for (index = 0u; index < entry->member_count; index++)
            free(entry->members[index]);
        free(entry->members);
        observer->member_count -= entry->member_count;
        entry->members = (char **)0;
        entry->member_count = 0u;
        entry->membership = 0;
    }
    entry->members = (char **)calloc(expected_count + 1u, sizeof(char *));
    if (entry->members == (char **)0)
        return cupidbuild_observer_fail(observer, "directory membership allocation failed");
    entry->member_count = expected_count;
    observer->member_count += expected_count;
    for (index = 0u; index < expected_count; index++) {
        entry->members[index] = (char *)malloc(strlen(expected[index]) + 1u);
        if (entry->members[index] == (char *)0)
            return cupidbuild_observer_fail(observer, "directory member allocation failed");
        (void)strcpy(entry->members[index], expected[index]);
    }
    entry->membership = 1;
    if (!cupidbuild_observer_members(observer, entry))
        return cupidbuild_observer_fail(observer, "directory membership differs from expected set");
    return 1;
}

#if defined(CUPIDBUILD_DIRECTORY_SNAPSHOT_TEST)
/* Linked only by the separate fault caller; ordinary and supplied callers do
 * not contain this hook. A nonzero return injects a failed capture step. */
int cupidbuild_directory_snapshot_test_checkpoint(unsigned int phase);
#endif

static int cupidbuild_directory_snapshot_checkpoint(unsigned int phase) {
#if defined(CUPIDBUILD_DIRECTORY_SNAPSHOT_TEST)
    return cupidbuild_directory_snapshot_test_checkpoint(phase);
#else
            (void)phase;
            return 0;
#endif
}

static void *cupidbuild_directory_snapshot_allocate(size_t count, size_t size) {
    if (cupidbuild_directory_snapshot_checkpoint(1u))
        return (void *)0;
    return calloc(count, size);
}

static void cupidbuild_directory_snapshot_release(cupidbuild_host_directory_snapshot_t *snapshot) {
    size_t index;
    if (snapshot == (cupidbuild_host_directory_snapshot_t *)0)
        return;
    for (index = 0u; index < snapshot->count; index++)
        free(snapshot->names[index]);
    free(snapshot->names);
    free(snapshot);
}

typedef struct {
    cupidbuild_host_observer_t *observer;
    cupidbuild_host_directory_snapshot_t *snapshot;
    size_t limit;
} cupidbuild_directory_snapshot_capture_t;

static int cupidbuild_directory_snapshot_member(void *context, const char *name) {
    cupidbuild_directory_snapshot_capture_t *capture =
        (cupidbuild_directory_snapshot_capture_t *)context;
    cupidbuild_host_directory_snapshot_t *snapshot = capture->snapshot;
    char *copy;
    size_t index;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return 1;
    if (!cupidbuild_observer_input_name_valid(capture->observer, name) ||
        snapshot->count >= capture->limit || cupidbuild_directory_snapshot_checkpoint(2u))
        return 0;
    for (index = 0u; index < snapshot->count; index++)
        if (strcmp(name, snapshot->names[index]) == 0)
            return 0;
    if (snapshot->count == snapshot->capacity) {
        size_t capacity = snapshot->capacity == 0u ? 1u : snapshot->capacity * 2u;
        char **names;
        if (capacity > capture->limit)
            capacity = capture->limit;
        names = (char **)cupidbuild_directory_snapshot_allocate(capacity, sizeof(char *));
        if (names == (char **)0)
            return 0;
        for (index = 0u; index < snapshot->count; index++)
            names[index] = snapshot->names[index];
        free(snapshot->names);
        snapshot->names = names;
        snapshot->capacity = capacity;
    }
    copy = (char *)cupidbuild_directory_snapshot_allocate(strlen(name) + 1u, 1u);
    if (copy == (char *)0)
        return 0;
    (void)strcpy(copy, name);
    snapshot->names[snapshot->count++] = copy;
    return 1;
}

static void cupidbuild_directory_snapshot_sort(cupidbuild_host_directory_snapshot_t *snapshot) {
    size_t index;
    for (index = 1u; index < snapshot->count; index++) {
        char *name = snapshot->names[index];
        size_t position = index;
        while (position != 0u && strcmp(snapshot->names[position - 1u], name) > 0) {
            snapshot->names[position] = snapshot->names[position - 1u];
            position--;
        }
        snapshot->names[position] = name;
    }
}

static int
cupidbuild_directory_snapshot_bind(cupidbuild_host_observer_t *observer,
                                   cupidbuild_observer_entry_t *entry,
                                   const cupidbuild_host_directory_snapshot_t *snapshot) {
    char **members = (char **)0;
    size_t index;
    if (entry->membership) {
        if (entry->member_count != snapshot->count)
            return 0;
        for (index = 0u; index < snapshot->count; index++) {
            size_t other;
            for (other = 0u; other < entry->member_count; other++)
                if (strcmp(snapshot->names[index], entry->members[other]) == 0)
                    break;
            if (other == entry->member_count)
                return 0;
        }
        return 1;
    }
    if (snapshot->count > CUPIDBUILD_OBSERVER_ENTRIES - observer->member_count)
        return 0;
    if (snapshot->count != 0u) {
        members = (char **)cupidbuild_directory_snapshot_allocate(snapshot->count, sizeof(char *));
        if (members == (char **)0)
            return 0;
        for (index = 0u; index < snapshot->count; index++) {
            members[index] = (char *)cupidbuild_directory_snapshot_allocate(
                strlen(snapshot->names[index]) + 1u, 1u);
            if (members[index] == (char *)0) {
                size_t previous;
                for (previous = 0u; previous < index; previous++)
                    free(members[previous]);
                free(members);
                return 0;
            }
            (void)strcpy(members[index], snapshot->names[index]);
        }
    }
    entry->members = members;
    entry->member_count = snapshot->count;
    entry->membership = 1;
    observer->member_count += snapshot->count;
    return 1;
}

int cupidbuild_host_observer_directory_snapshot(
    cupidbuild_host_observer_t *observer, const char *logical,
    const cupidbuild_host_directory_snapshot_t **snapshot_out) {
    cupidbuild_observer_entry_t *entry;
    cupidbuild_directory_snapshot_capture_t capture;
    cupidbuild_host_directory_snapshot_t *snapshot;
    size_t remaining;
    if (snapshot_out != (const cupidbuild_host_directory_snapshot_t **)0)
        *snapshot_out = (const cupidbuild_host_directory_snapshot_t *)0;
    if (observer == (cupidbuild_host_observer_t *)0 || observer->error[0] != 0)
        return 0;
    if (logical == (const char *)0 ||
        snapshot_out == (const cupidbuild_host_directory_snapshot_t **)0)
        return cupidbuild_observer_fail(observer, "invalid directory snapshot arguments");
    if (!cupidbuild_host_observer_require_unchanged(observer))
        return 0;
    if (observer->snapshot_count >= CUPIDBUILD_OBSERVER_ENTRIES)
        return cupidbuild_observer_fail(observer, "directory snapshot token limit exceeded");
    entry = logical[0] != 0 ? cupidbuild_observer_walk(observer, observer->root, logical, 1, 0)
                            : observer->root;
    if (entry == (cupidbuild_observer_entry_t *)0)
        return cupidbuild_observer_fail(observer, "unsafe or unavailable snapshot directory");
    snapshot = (cupidbuild_host_directory_snapshot_t *)cupidbuild_directory_snapshot_allocate(
        1u, sizeof(*snapshot));
    if (snapshot == (cupidbuild_host_directory_snapshot_t *)0)
        return cupidbuild_observer_fail(observer, "directory snapshot allocation failed");
    capture.observer = observer;
    capture.snapshot = snapshot;
    capture.limit = CUPIDBUILD_OBSERVER_ENTRIES - observer->snapshot_name_count;
    remaining = entry->membership ? entry->member_count
                                  : CUPIDBUILD_OBSERVER_ENTRIES - observer->member_count;
    if (remaining < capture.limit)
        capture.limit = remaining;
    if (!cupidbuild_observer_enumerate(entry, cupidbuild_directory_snapshot_member, &capture) ||
        cupidbuild_directory_snapshot_checkpoint(3u))
        goto failed;
    cupidbuild_directory_snapshot_sort(snapshot);
    if (!cupidbuild_directory_snapshot_bind(observer, entry, snapshot) ||
        cupidbuild_directory_snapshot_checkpoint(4u) ||
        !cupidbuild_host_observer_require_unchanged(observer))
        goto failed;
    /* All counters were bounded before allocation and calls are serialized.
     * Link the immutable token only after its complete final observer check. */
    snapshot->next = observer->snapshots;
    observer->snapshots = snapshot;
    observer->snapshot_count++;
    observer->snapshot_name_count += snapshot->count;
    *snapshot_out = snapshot;
    return 1;
failed:
    cupidbuild_directory_snapshot_release(snapshot);
    return cupidbuild_observer_fail(observer, "directory snapshot capture failed");
}

int cupidbuild_host_observer_directory_snapshot_names(
    cupidbuild_host_observer_t *observer, const cupidbuild_host_directory_snapshot_t *snapshot,
    const char *const **names_out, size_t *count_out) {
    const cupidbuild_host_directory_snapshot_t *known;
    if (names_out != (const char *const **)0)
        *names_out = (const char *const *)0;
    if (count_out != (size_t *)0)
        *count_out = 0u;
    if (observer == (cupidbuild_host_observer_t *)0 || observer->error[0] != 0)
        return 0;
    if (names_out == (const char *const **)0 || count_out == (size_t *)0)
        return cupidbuild_observer_fail(observer, "invalid directory snapshot view outputs");
    for (known = observer->snapshots; known != (const cupidbuild_host_directory_snapshot_t *)0;
         known = known->next)
        if (known == snapshot)
            break;
    if (known == (const cupidbuild_host_directory_snapshot_t *)0)
        return cupidbuild_observer_fail(observer, "directory snapshot does not belong to observer");
    if (!cupidbuild_host_observer_require_unchanged(observer))
        return 0;
    *names_out = (const char *const *)known->names;
    *count_out = known->count;
    return 1;
}
