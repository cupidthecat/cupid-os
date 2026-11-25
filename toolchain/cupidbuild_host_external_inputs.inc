/**
 * @file cupidbuild_host_external_inputs.inc
 * @brief Retain observed roots and original source requests for publication.
 */

#define CUPIDBUILD_HOST_SOURCE_REQUEST_GROUPS 68u

static int cupidbuild_host_external_input_failure(cupidbuild_host_transaction_t *transaction,
                                                  const char *message) {
    transaction->external_binding_failed = 1;
    cupidbuild_host_set_error(transaction, message);
    return 0;
}

static int cupidbuild_host_require_external_observers(cupidbuild_host_transaction_t *transaction) {
    if (transaction->external_binding_failed)
        return 0;
    for (size_t index = 0u; index < transaction->external_observer_count; index++) {
        const cupidbuild_host_external_observer_t *binding = &transaction->external_observers[index];
        cupidbuild_host_observer_t *observer = binding->observer;
        if (observer->stream_group_active ||
            !cupidbuild_observer_require_current(observer, binding->root_identity_only))
            return cupidbuild_host_external_input_failure(
                transaction, "borrowed external observer changed or capture is active");
    }
    return 1;
}

static int cupidbuild_host_source_request_failure(cupidbuild_host_transaction_t *transaction,
                                                   const char *message) {
    transaction->source_request_state = -1;
    cupidbuild_host_set_error(transaction, message);
    return 0;
}

const char *cupidbuild_host_source_requests_error(const cupidbuild_host_source_request_set_t *set) {
    size_t index;
    size_t total;
    if (!set || set->group_count > CUPIDBUILD_HOST_SOURCE_REQUEST_GROUPS ||
        (!set->groups && set->group_count) || (!set->missing && set->missing_count) ||
        set->missing_count > CUPIDBUILD_HOST_DISK_INPUTS)
        return "invalid publication source requests";
    total = set->missing_count;
    for (index = 0u; index < set->group_count; index++) {
        const cupidbuild_host_file_request_group_t *group = &set->groups[index];
        if (!group->observer || group->observer->selection_only ||
            (!group->requests && group->count) || group->count > CUPIDBUILD_HOST_DISK_INPUTS - total)
            return "invalid publication source request group";
        total += group->count;
        if (!cupidbuild_host_observer_require_source_unchanged(group->observer))
            return "source observation changed";
    }
    for (index = 0u; index < set->group_count; index++) {
        const cupidbuild_host_file_request_group_t *group = &set->groups[index];
        if (!cupidbuild_host_observer_require_file_request_bindings(
                group->observer, group->requests, group->count, NULL, NULL))
            return "original source path changed";
    }
    for (index = 0u; index < set->missing_count; index++) {
        if (!cupidbuild_host_source_missing(set->missing[index]))
            return "missing source appeared";
    }
    for (index = 0u; index < set->group_count; index++) {
        const cupidbuild_host_file_request_group_t *group = &set->groups[index];
        if (!cupidbuild_host_observer_require_source_unchanged(group->observer))
            return "source observation changed";
    }
    return NULL;
}

static int cupidbuild_host_require_source_requests(cupidbuild_host_transaction_t *transaction) {
    const char *error;
    if (transaction->source_request_state <= 0)
        return transaction->source_request_state == 0;
    error = cupidbuild_host_source_requests_error(transaction->source_requests);
    return error ? cupidbuild_host_source_request_failure(transaction, error) : 1;
}

int cupidbuild_host_transaction_bind_source_requests(
    cupidbuild_host_transaction_t *transaction, const cupidbuild_host_source_request_set_t *set) {
    if (!transaction)
        return 0;
    if (transaction->source_request_state || transaction->runner_transaction ||
        transaction->observer_publication_started)
        return cupidbuild_host_source_request_failure(transaction,
                                                      "invalid publication source requests");
    transaction->source_requests = set;
    transaction->source_request_state = 1;
    return cupidbuild_host_require_source_requests(transaction);
}

static int cupidbuild_host_borrow_external(cupidbuild_host_transaction_t *transaction,
                                            cupidbuild_host_observer_t *observer,
                                            int root_identity_only) {
    cupidbuild_host_external_observer_t *binding;
    if (!transaction || transaction->external_binding_failed)
        return 0;
    if (transaction->runner_transaction || transaction->observer_binding_state != 1 ||
        transaction->observer_publication_started || !observer || observer->selection_only ||
        observer->stream_group_active || !observer->root ||
        transaction->external_observer_count >= CUPIDBUILD_HOST_INPUTS ||
        !cupidbuild_host_require_borrowed_observer(transaction) ||
        !cupidbuild_observer_require_current(observer, root_identity_only))
        return cupidbuild_host_external_input_failure(
            transaction, "external observer requires a valid primary and retained root");

    if (cupidbuild_observer_same(&observer->root->captured,
                                 &transaction->borrowed_observer->root->captured, 1))
        return cupidbuild_host_external_input_failure(transaction,
                                                    "external observer repeats the primary root");
    for (size_t index = 0u; index < transaction->external_observer_count; index++) {
        cupidbuild_host_observer_t *existing = transaction->external_observers[index].observer;
        if (cupidbuild_observer_same(&observer->root->captured, &existing->root->captured, 1))
            return cupidbuild_host_external_input_failure(
                transaction, "external observer root may only be borrowed once");
    }
    binding = &transaction->external_observers[transaction->external_observer_count++];
    binding->observer = observer;
    binding->root_identity_only = root_identity_only;
    return 1;
}

int cupidbuild_host_transaction_borrow_external_observer(cupidbuild_host_transaction_t *transaction,
                                                         cupidbuild_host_observer_t *observer) {
    return cupidbuild_host_borrow_external(transaction, observer, 0);
}

int cupidbuild_host_transaction_borrow_external_source_observer(
    cupidbuild_host_transaction_t *transaction, cupidbuild_host_observer_t *observer) {
    return cupidbuild_host_borrow_external(transaction, observer, 1);
}

static int cupidbuild_host_external_observer_is_borrowed(const cupidbuild_host_transaction_t *tx,
                                                         const cupidbuild_host_observer_t *observer) {
    for (size_t index = 0u; index < tx->external_observer_count; index++) {
        if (tx->external_observers[index].observer == observer)
            return 1;
    }
    return 0;
}

static int cupidbuild_host_read_external_input(cupidbuild_host_observer_t *observer,
                                               const char *logical, unsigned long long capacity,
                                               cupidbuild_host_snapshot_t *snapshot) {
    cupidbuild_observer_entry_t *entry = cupidbuild_observer_retained_file(observer, logical);
    cupidbuild_host_stream_observation_t observed;
    if (!entry || entry->captured.size > capacity ||
        !cupidbuild_observer_retained_bindings(observer, entry))
        return 0;
    if (!entry->payload && !cupidbuild_host_observer_file_stream_retained(
                               observer, logical, capacity, NULL, NULL, &observed))
        return 0;
#if defined(_WIN32)
    if (!cupidbuild_host_windows_regular_metadata(entry->handle, snapshot))
#else
    if (!cupidbuild_host_open_regular_metadata(entry->handle, snapshot))
#endif
        return 0;
    if (snapshot->size != entry->captured.size)
        return 0;
    (void)memcpy(snapshot->sha256, entry->digest, sizeof(snapshot->sha256));
    return 1;
}

static int cupidbuild_host_freeze_external(cupidbuild_host_transaction_t *transaction,
                                            cupidbuild_host_observer_t *observer, const char *logical,
                                            const char *private_name, unsigned long long capacity,
                                            const char **frozen_path_out,
                                            cupidbuild_host_snapshot_t *snapshot_out,
                                            int share_input) {
    const unsigned long long limit = share_input ? CUPIDBUILD_HOST_MAX_EXTENT : 2147483647ULL;
    if (frozen_path_out)
        *frozen_path_out = NULL;
    if (snapshot_out)
        (void)memset(snapshot_out, 0, sizeof(*snapshot_out));
    if (!transaction || transaction->external_binding_failed)
        return 0;
    if (transaction->observer_publication_started || !observer || !logical || capacity == 0u ||
        capacity > limit || !cupidbuild_host_path_is_relative_safe(private_name) ||
        (share_input && transaction->source_request_state != 1) ||
        !cupidbuild_host_external_observer_is_borrowed(transaction, observer) ||
        !cupidbuild_host_require_borrowed_observer(transaction))
        return cupidbuild_host_external_input_failure(
            transaction, "external input could not be captured through its retained observer");
    if (!cupidbuild_host_freeze_input_internal(transaction, logical, private_name, frozen_path_out,
                                              snapshot_out, capacity, 1, 0, observer,
                                              share_input)) {
        transaction->external_binding_failed = 1;
        return 0;
    }
    return 1;
}

int cupidbuild_host_freeze_external_input(cupidbuild_host_transaction_t *transaction,
                                          cupidbuild_host_observer_t *observer, const char *logical,
                                          const char *private_name, unsigned long long capacity,
                                          const char **frozen_path_out,
                                          cupidbuild_host_snapshot_t *snapshot_out) {
    return cupidbuild_host_freeze_external(transaction, observer, logical, private_name, capacity,
                                          frozen_path_out, snapshot_out, 0);
}

int cupidbuild_host_freeze_disk_input(cupidbuild_host_transaction_t *transaction,
                                      cupidbuild_host_observer_t *external, const char *source,
                                      const char *private_name, unsigned long long capacity,
                                      const char **frozen_path_out,
                                      cupidbuild_host_snapshot_t *snapshot_out) {
    if (external)
        return cupidbuild_host_freeze_external(transaction, external, source, private_name, capacity,
                                              frozen_path_out, snapshot_out, 1);
    return cupidbuild_host_freeze_bounded_input(transaction, source, private_name, capacity,
                                               frozen_path_out, snapshot_out, 1);
}
