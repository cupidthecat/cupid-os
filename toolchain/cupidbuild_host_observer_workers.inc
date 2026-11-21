/**
 * @file cupidbuild_host_observer_workers.inc
 * @brief Validate retained observation groups on the supported hosts.
 */

#if defined(_WIN32)
#define CUPIDBUILD_OBSERVER_WORKERS 8u
typedef DWORD (*cupidbuild_observer_thread_function_t)(void *argument);

typedef struct {
    cupidbuild_observer_thread_function_t function;
    void *argument;
    volatile LONG *complete;
} cupidbuild_observer_thread_call_t;

#if defined(CUPID_HOSTED_I386_WINDOWS_H)
_Static_assert(sizeof(cupidbuild_observer_thread_call_t) == 12u,
               "the Windows callback adapter uses three i386 words");
void cupid_runtime_heap_enable_threads(void);
HANDLE cupid_windows_observer_thread_open(void *call);
void cupid_windows_observer_thread_finish(volatile LONG *complete);
LONG cupid_windows_observer_thread_finished(volatile LONG *complete);
#else
static void cupid_windows_observer_thread_finish(volatile LONG *complete);

static DWORD WINAPI cupidbuild_observer_native_thread_entry(void *opaque) {
    cupidbuild_observer_thread_call_t *call = (cupidbuild_observer_thread_call_t *)opaque;
    DWORD result = call->function(call->argument);
    /* The descriptor is never read again after publishing completion. */
    cupid_windows_observer_thread_finish(call->complete);
    return result;
}

static HANDLE cupid_windows_observer_thread_open(void *opaque) {
    cupidbuild_observer_thread_call_t *call = (cupidbuild_observer_thread_call_t *)opaque;
    if (call == (cupidbuild_observer_thread_call_t *)0 ||
        call->function == (cupidbuild_observer_thread_function_t)0 ||
        call->complete == (volatile LONG *)0)
        return (HANDLE)0;
    return CreateThread((LPSECURITY_ATTRIBUTES)0, 131072u, cupidbuild_observer_native_thread_entry,
                        call, 0u, (LPDWORD)0);
}

static void cupid_windows_observer_thread_finish(volatile LONG *complete) {
    (void)InterlockedExchange(complete, 1);
}

static LONG cupid_windows_observer_thread_finished(volatile LONG *complete) {
    return InterlockedCompareExchange(complete, 1, 1);
}
#endif
/* No error publication or heap mutation occurs on worker paths. Buffered
 * payload and membership entries remain on the calling thread. */
static const char *cupidbuild_observer_validate_entry(cupidbuild_host_observer_t *observer,
                                                      cupidbuild_observer_entry_t *entry,
                                                      int root_identity_only) {
    cupidbuild_observer_stat_t current;
    int identity_only = entry->directory && (entry != observer->root || root_identity_only);
    if (entry->absent) {
        if (!entry->parent || !cupidbuild_observer_child_absent(entry->parent->handle, entry->name))
            return "observed first absence appeared or became unavailable";
        return (const char *)0;
    }
    if (!cupidbuild_observer_stat(entry->handle, entry->directory, &current) ||
        !cupidbuild_observer_same(&entry->captured, &current, identity_only))
        return "retained observation changed";
    if (entry->parent != (cupidbuild_observer_entry_t *)0) {
        cupidbuild_observer_handle_t fresh =
            cupidbuild_observer_open_child(entry->parent->handle, entry->name, entry->directory, 0);
        int valid;
        if (fresh == CUPIDBUILD_OBSERVER_INVALID)
            return "observed path disappeared or became a link";
        valid = cupidbuild_observer_stat(fresh, entry->directory, &current) &&
                cupidbuild_observer_same(&entry->captured, &current, identity_only);
        if (!cupidbuild_observer_close_handle(fresh))
            valid = 0;
        if (!valid)
            return "observed path was replaced or changed";
    }
    if (entry->payload == 2) {
        unsigned char digest[32];
        if (!cupidbuild_observer_stream_read(entry, entry->captured.size,
                                             (cupidbuild_host_stream_sink_t)0, (void *)0, digest))
            return "streamed payload cannot be reread unchanged";
        if (memcmp(digest, entry->digest, sizeof(digest)) != 0)
            return "streamed payload changed";
    } else if (entry->payload) {
        unsigned char *bytes;
        unsigned char digest[32];
        if (!cupidbuild_observer_read(entry, (size_t)entry->captured.size, &bytes))
            return "captured payload cannot be reread unchanged";
        cupidbuild_sha256(bytes, (size_t)entry->captured.size, digest);
        free(bytes);
        if (memcmp(digest, entry->digest, sizeof(digest)) != 0)
            return "captured payload changed";
    }
    if (!cupidbuild_observer_seed_members_current(entry))
        return "observed seed executable membership changed";
    if (entry->membership && !cupidbuild_observer_members(observer, entry))
        return "observed directory membership changed";
    return (const char *)0;
}

typedef struct {
    cupidbuild_observer_entry_t *entry;
    const char *error;
    int checked;
} cupidbuild_observer_validation_row_t;

typedef struct {
    cupidbuild_observer_thread_call_t call;
    cupidbuild_host_observer_t *observer;
    cupidbuild_observer_validation_row_t *rows;
    size_t count;
    unsigned int index;
    unsigned int stride;
    int root_identity_only;
    volatile LONG complete;
} cupidbuild_observer_validation_worker_t;

static int cupidbuild_observer_parallel_entry(cupidbuild_host_observer_t *observer,
                                              cupidbuild_observer_entry_t *entry) {
    return entry != observer->root && entry->parent != (cupidbuild_observer_entry_t *)0 &&
           entry->membership == 0 && !entry->seed_members &&
           (entry->payload == 0 || entry->payload == 2);
}

static DWORD cupidbuild_observer_validate_worker(void *opaque) {
    cupidbuild_observer_validation_worker_t *worker =
        (cupidbuild_observer_validation_worker_t *)opaque;
    size_t index;
    for (index = worker->index; index < worker->count; index += worker->stride) {
        cupidbuild_observer_validation_row_t *row = &worker->rows[index];
        if (cupidbuild_observer_parallel_entry(worker->observer, row->entry)) {
            row->error = cupidbuild_observer_validate_entry(worker->observer, row->entry,
                                                            worker->root_identity_only);
            row->checked = 1;
        }
    }
    return 0u;
}
#endif

#if defined(_WIN32)
static int cupidbuild_observer_require_current_serial(cupidbuild_host_observer_t *observer,
                                                      int root_identity_only) {
    cupidbuild_observer_entry_t *entry;
    if (observer == (cupidbuild_host_observer_t *)0 || observer->error[0] != 0)
        return 0;
    if (!cupidbuild_observer_source_root_binding(observer))
        return 0;
    for (entry = observer->entries; entry != (cupidbuild_observer_entry_t *)0;
         entry = entry->next) {
        cupidbuild_observer_stat_t current;
        int identity_only = entry->directory && (entry != observer->root || root_identity_only);
        if (entry->absent) {
            if (!entry->parent ||
                !cupidbuild_observer_child_absent(entry->parent->handle, entry->name))
                return cupidbuild_observer_fail(
                    observer, "observed first absence appeared or became unavailable");
            continue;
        }
        if (!cupidbuild_observer_stat(entry->handle, entry->directory, &current) ||
            !cupidbuild_observer_same(&entry->captured, &current, identity_only))
            return cupidbuild_observer_fail(observer, "retained observation changed");
        if (entry->parent != (cupidbuild_observer_entry_t *)0) {
            cupidbuild_observer_handle_t fresh = cupidbuild_observer_open_child(
                entry->parent->handle, entry->name, entry->directory, 0);
            int valid;
            if (fresh == CUPIDBUILD_OBSERVER_INVALID)
                return cupidbuild_observer_fail(observer,
                                                "observed path disappeared or became a link");
            valid = cupidbuild_observer_stat(fresh, entry->directory, &current) &&
                    cupidbuild_observer_same(&entry->captured, &current, identity_only);
            if (!cupidbuild_observer_close_handle(fresh))
                valid = 0;
            if (!valid)
                return cupidbuild_observer_fail(observer, "observed path was replaced or changed");
        }
        if (entry->payload == 2) {
            unsigned char digest[32];
            if (!cupidbuild_observer_stream_read(entry, entry->captured.size,
                                                 (cupidbuild_host_stream_sink_t)0, (void *)0,
                                                 digest))
                return cupidbuild_observer_fail(observer,
                                                "streamed payload cannot be reread unchanged");
            if (memcmp(digest, entry->digest, sizeof(digest)) != 0)
                return cupidbuild_observer_fail(observer, "streamed payload changed");
        } else if (entry->payload) {
            unsigned char *bytes;
            unsigned char digest[32];
            if (!cupidbuild_observer_read(entry, (size_t)entry->captured.size, &bytes))
                return cupidbuild_observer_fail(observer,
                                                "captured payload cannot be reread unchanged");
            cupidbuild_sha256(bytes, (size_t)entry->captured.size, digest);
            free(bytes);
            if (memcmp(digest, entry->digest, sizeof(digest)) != 0)
                return cupidbuild_observer_fail(observer, "captured payload changed");
        }
        if (!cupidbuild_observer_seed_members_current(entry))
            return cupidbuild_observer_fail(observer, "observed seed executable membership changed");
        if (entry->membership && !cupidbuild_observer_members(observer, entry))
            return cupidbuild_observer_fail(observer, "observed directory membership changed");
    }
    return 1;
}

static int cupidbuild_observer_require_current(cupidbuild_host_observer_t *observer,
                                               int root_identity_only) {
#if defined(_WIN32)
    cupidbuild_observer_entry_t *entry;
    cupidbuild_observer_validation_row_t *rows;
    cupidbuild_observer_validation_worker_t workers[CUPIDBUILD_OBSERVER_WORKERS];
    HANDLE threads[CUPIDBUILD_OBSERVER_WORKERS];
    size_t count = 0u, eligible = 0u, index;
    unsigned int worker, created = 0u;
    int valid = 1;
    const char *error = (const char *)0;
    if (observer == (cupidbuild_host_observer_t *)0 || observer->error[0] != 0)
        return 0;
    for (entry = observer->entries; entry != (cupidbuild_observer_entry_t *)0;
         entry = entry->next) {
        count++;
        if (cupidbuild_observer_parallel_entry(observer, entry))
            eligible++;
    }
    if (eligible < 256u)
        return cupidbuild_observer_require_current_serial(observer, root_identity_only);
    if (!cupidbuild_observer_source_root_binding(observer))
        return 0;
    rows = (cupidbuild_observer_validation_row_t *)calloc(count, sizeof(*rows));
    if (rows == (cupidbuild_observer_validation_row_t *)0)
        return cupidbuild_observer_require_current_serial(observer, root_identity_only);
    index = 0u;
    for (entry = observer->entries; entry != (cupidbuild_observer_entry_t *)0; entry = entry->next)
        rows[index++].entry = entry;
#if defined(CUPID_HOSTED_I386_WINDOWS_H)
    cupid_runtime_heap_enable_threads();
#endif
    for (worker = 0u; worker < CUPIDBUILD_OBSERVER_WORKERS; worker++) {
        workers[worker].call.function = cupidbuild_observer_validate_worker;
        workers[worker].call.argument = &workers[worker];
        workers[worker].call.complete = &workers[worker].complete;
        workers[worker].observer = observer;
        workers[worker].rows = rows;
        workers[worker].count = count;
        workers[worker].index = worker;
        workers[worker].stride = CUPIDBUILD_OBSERVER_WORKERS;
        workers[worker].root_identity_only = root_identity_only;
        workers[worker].complete = 0;
        threads[worker] = cupid_windows_observer_thread_open(&workers[worker].call);
        if (threads[worker] == (HANDLE)0)
            break;
        created++;
    }
    for (worker = 0u; worker < created; worker++) {
        if (WaitForSingleObject(threads[worker], INFINITE) != WAIT_OBJECT_0) {
            valid = 0;
            /* Completion is also a release/acquire fence. Even a failed wait must
             * not allow worker context or observations to be released early. */
            while (cupid_windows_observer_thread_finished(&workers[worker].complete) == 0)
                (void)WaitForSingleObject(threads[worker], 1u);
        }
        if (!CloseHandle(threads[worker]))
            valid = 0;
    }
    /* Deferred root, membership and buffered payload checks run only after all
     * worker memory accesses end. Failed creation leaves untouched rows here. */
    for (index = 0u; index < count; index++) {
        if (!rows[index].checked)
            rows[index].error =
                cupidbuild_observer_validate_entry(observer, rows[index].entry, root_identity_only);
        if (error == (const char *)0 && rows[index].error != (const char *)0)
            error = rows[index].error;
    }
    free(rows);
    if (!valid)
        return cupidbuild_observer_fail(observer, "observation worker synchronization failed");
    if (error != (const char *)0)
        return cupidbuild_observer_fail(observer, error);
    return 1;
#else
    return cupidbuild_observer_require_current_serial(observer, root_identity_only);
#endif
}

#else
        static int cupidbuild_observer_require_current(cupidbuild_host_observer_t * observer,
                                                       int root_identity_only) {
            cupidbuild_observer_entry_t *entry;
            if (observer == (cupidbuild_host_observer_t *)0 || observer->error[0] != 0)
                return 0;
            if (!cupidbuild_observer_source_root_binding(observer))
                return 0;
            for (entry = observer->entries; entry != (cupidbuild_observer_entry_t *)0;
                 entry = entry->next) {
                cupidbuild_observer_stat_t current;
                int identity_only =
                    entry->directory && (entry != observer->root || root_identity_only);
                if (entry->absent) {
                    if (!entry->parent ||
                        !cupidbuild_observer_child_absent(entry->parent->handle, entry->name))
                        return cupidbuild_observer_fail(
                            observer, "observed first absence appeared or became unavailable");
                    continue;
                }
                if (!cupidbuild_observer_stat(entry->handle, entry->directory, &current) ||
                    !cupidbuild_observer_same(&entry->captured, &current, identity_only))
                    return cupidbuild_observer_fail(observer, "retained observation changed");
                if (entry->parent != (cupidbuild_observer_entry_t *)0) {
                    cupidbuild_observer_handle_t fresh = cupidbuild_observer_open_child(
                        entry->parent->handle, entry->name, entry->directory, 0);
                    int valid;
                    if (fresh == CUPIDBUILD_OBSERVER_INVALID)
                        return cupidbuild_observer_fail(
                            observer, "observed path disappeared or became a link");
                    valid = cupidbuild_observer_stat(fresh, entry->directory, &current) &&
                            cupidbuild_observer_same(&entry->captured, &current, identity_only);
                    if (!cupidbuild_observer_close_handle(fresh))
                        valid = 0;
                    if (!valid)
                        return cupidbuild_observer_fail(observer,
                                                        "observed path was replaced or changed");
                }
                if (entry->payload == 2) {
                    unsigned char digest[32];
                    if (!cupidbuild_observer_stream_read(entry, entry->captured.size,
                                                         (cupidbuild_host_stream_sink_t)0,
                                                         (void *)0, digest))
                        return cupidbuild_observer_fail(
                            observer, "streamed payload cannot be reread unchanged");
                    if (memcmp(digest, entry->digest, sizeof(digest)) != 0)
                        return cupidbuild_observer_fail(observer, "streamed payload changed");
                } else if (entry->payload) {
                    unsigned char *bytes;
                    unsigned char digest[32];
                    if (!cupidbuild_observer_read(entry, (size_t)entry->captured.size, &bytes))
                        return cupidbuild_observer_fail(
                            observer, "captured payload cannot be reread unchanged");
                    cupidbuild_sha256(bytes, (size_t)entry->captured.size, digest);
                    free(bytes);
                    if (memcmp(digest, entry->digest, sizeof(digest)) != 0)
                        return cupidbuild_observer_fail(observer, "captured payload changed");
                }
                if (!cupidbuild_observer_seed_members_current(entry))
                    return cupidbuild_observer_fail(observer, "observed seed executable membership changed");
                if (entry->membership && !cupidbuild_observer_members(observer, entry))
                    return cupidbuild_observer_fail(observer,
                                                    "observed directory membership changed");
            }
            return 1;
        }

#endif

/* Capture a group member through its already retained no-follow parent.
 * The group owns whole-observer validation; each member still brackets its
 * leaf payload with held/fresh metadata and immediate parent identity checks. */
static int
cupidbuild_observer_stream_group_member(cupidbuild_host_observer_t *observer,
                                        const cupidbuild_host_retained_stream_request_t *request,
                                        cupidbuild_host_stream_observation_t *result_out) {
    cupidbuild_observer_entry_t *entry =
        cupidbuild_observer_retained_file(observer, request->logical);
    cupidbuild_observer_entry_t streaming;
    cupidbuild_observer_stat_t current;
    cupidbuild_observer_handle_t fresh = CUPIDBUILD_OBSERVER_INVALID;
    cupidbuild_observer_handle_t named = CUPIDBUILD_OBSERVER_INVALID;
    unsigned char digest[32];
    int parent_identity_only;
    int valid;
    if (!entry || entry->directory || entry->absent || !entry->parent)
        return cupidbuild_observer_fail(observer,
                                        "group streaming requires retained regular files");
    parent_identity_only = entry->parent != observer->root;
    valid = cupidbuild_observer_stat(entry->parent->handle, 1, &current) &&
            cupidbuild_observer_same(&entry->parent->captured, &current, parent_identity_only) &&
            cupidbuild_observer_stat(entry->handle, 0, &current) &&
            cupidbuild_observer_same(&entry->captured, &current, 0);
    if (valid) {
        fresh = cupidbuild_observer_open_child(entry->parent->handle, entry->name, 0, 1);
        valid = fresh != CUPIDBUILD_OBSERVER_INVALID;
    }
    if (valid) {
        streaming = *entry;
        streaming.handle = fresh;
        valid = cupidbuild_observer_stream_read(&streaming, request->limit, request->sink,
                                                request->context, digest) &&
                (!entry->payload || memcmp(digest, entry->digest, sizeof(digest)) == 0) &&
                observer->error[0] == 0 && cupidbuild_observer_stat(entry->handle, 0, &current) &&
                cupidbuild_observer_same(&entry->captured, &current, 0);
    }
    if (valid) {
        named = cupidbuild_observer_open_child(entry->parent->handle, entry->name, 0, 0);
        valid = named != CUPIDBUILD_OBSERVER_INVALID &&
                cupidbuild_observer_stat(named, 0, &current) &&
                cupidbuild_observer_same(&entry->captured, &current, 0) &&
                cupidbuild_observer_stat(entry->parent->handle, 1, &current) &&
                cupidbuild_observer_same(&entry->parent->captured, &current, parent_identity_only);
    }
    if (named != CUPIDBUILD_OBSERVER_INVALID && !cupidbuild_observer_close_handle(named))
        valid = 0;
    if (valid && !cupidbuild_observer_close_handle(entry->handle))
        valid = 0;
    if (!valid) {
        if (fresh != CUPIDBUILD_OBSERVER_INVALID)
            (void)cupidbuild_observer_close_handle(fresh);
        return cupidbuild_observer_fail(observer,
                                        "group stream changed, exceeded its limit or failed");
    }
    entry->handle = fresh;
    entry->payload = 2;
    (void)memcpy(entry->digest, digest, sizeof(digest));
    result_out->size = entry->captured.size;
    (void)memcpy(result_out->sha256, digest, sizeof(digest));
    return 1;
}

int cupidbuild_host_observer_files_stream_retained(
    cupidbuild_host_observer_t *observer, const cupidbuild_host_retained_stream_request_t *requests,
    size_t count, cupidbuild_host_stream_observation_t *results_out) {
    size_t index;
    int valid;
    if (count > (size_t)-1 / sizeof(*results_out) || count > (size_t)-1 / sizeof(*requests))
        return cupidbuild_observer_fail(observer, "retained stream group count overflow");
    if (results_out && count)
        (void)memset(results_out, 0, count * sizeof(*results_out));
    if (!observer || observer->error[0])
        return 0;
    if (observer->stream_group_active || (count && (!requests || !results_out)))
        return cupidbuild_observer_fail(observer, "invalid or reentered retained stream group");
    if (!cupidbuild_observer_require_current(observer, 0))
        return 0;
    /* Reject every invalid request before the first callback or handle upgrade. */
    for (index = 0u; index < count; index++) {
        cupidbuild_observer_entry_t *entry =
            cupidbuild_observer_retained_file(observer, requests[index].logical);
        if (!entry || entry->directory || entry->absent || !entry->parent)
            return cupidbuild_observer_fail(observer,
                                            "group streaming requires retained regular files");
    }
    observer->stream_group_active = 1;
    valid = 1;
    for (index = 0u; valid && index < count; index++)
        valid = cupidbuild_observer_stream_group_member(observer, &requests[index],
                                                        &results_out[index]);
    if (valid)
        valid = cupidbuild_observer_require_current(observer, 0);
    observer->stream_group_active = 0;
    if (!valid) {
        if (results_out && count)
            (void)memset(results_out, 0, count * sizeof(*results_out));
        return cupidbuild_observer_fail(observer, "retained stream group did not validate");
    }
    return 1;
}

/* Whole-observer checks own the group lifetime; each path still checks its
 * immediate root/parent, fresh no-follow names and held entry metadata. */
static int cupidbuild_observer_paths_kind_profile(cupidbuild_host_observer_t *observer,
                                                  const char *const *logical, size_t count,
                                                  cupidbuild_host_path_observation_sink_t sink,
                                                  void *context,
                                                  cupidbuild_host_entry_kind_t *kinds_out,
                                                  int root_identity_only) {
    size_t index;
    int valid;
    if (count > (size_t)-1 / sizeof(*logical) || count > (size_t)-1 / sizeof(*kinds_out))
        return cupidbuild_observer_fail(observer, "path observation group count overflow");
    if (kinds_out && count)
        (void)memset(kinds_out, 0, count * sizeof(*kinds_out));
    if (!observer || observer->error[0])
        return 0;
    if (observer->stream_group_active || (root_identity_only && !observer->selection_only) ||
        (count && (!logical || !kinds_out)))
        return cupidbuild_observer_fail(observer, "invalid or reentered path observation group");
    for (index = 0u; index < count; index++)
        if (!cupidbuild_observer_path_name_valid(observer, logical[index]))
            return 0;
    if (!cupidbuild_observer_require_current(observer, root_identity_only))
        return 0;
    observer->stream_group_active = 1;
    valid = 1;
    for (index = 0u; valid && index < count; index++) {
        valid = cupidbuild_observer_path_kind_profile(observer, logical[index], &kinds_out[index],
                                                      root_identity_only ? 2 : 1);
        if (valid && sink)
            valid = sink(context, index, logical[index], kinds_out[index]);
        if (observer->error[0])
            valid = 0;
    }
    if (valid)
        valid = cupidbuild_observer_require_current(observer, root_identity_only);
    observer->stream_group_active = 0;
    if (!valid) {
        if (kinds_out && count)
            (void)memset(kinds_out, 0, count * sizeof(*kinds_out));
        return cupidbuild_observer_fail(observer, "path observation group did not validate");
    }
    return 1;
}

int cupidbuild_host_observer_paths_kind(cupidbuild_host_observer_t *observer,
                                        const char *const *logical, size_t count,
                                        cupidbuild_host_path_observation_sink_t sink, void *context,
                                        cupidbuild_host_entry_kind_t *kinds_out) {
    return cupidbuild_observer_paths_kind_profile(observer, logical, count, sink, context,
                                                  kinds_out, 0);
}

int cupidbuild_host_observer_source_selection_paths_kind(
    cupidbuild_host_observer_t *observer, const char *const *logical, size_t count,
    cupidbuild_host_path_observation_sink_t sink, void *context,
    cupidbuild_host_entry_kind_t *kinds_out) {
    return cupidbuild_observer_paths_kind_profile(observer, logical, count, sink, context,
                                                  kinds_out, 1);
}

int cupidbuild_host_observer_require_unchanged(cupidbuild_host_observer_t *observer) {
    if (observer && observer->stream_group_active)
        return cupidbuild_observer_fail(observer, "retained capture is still active");
    return cupidbuild_observer_require_current(observer, 0);
}

int cupidbuild_host_observer_require_source_unchanged(cupidbuild_host_observer_t *observer) {
    if (observer && observer->stream_group_active)
        return cupidbuild_observer_fail(observer, "retained capture is still active");
    return cupidbuild_observer_require_current(observer, 1);
}
