/**
 * @file cupidbuild_host_external_inputs.inc
 * @brief Retain and copy transaction inputs from external observed roots.
 */

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
        cupidbuild_host_observer_t *observer = transaction->external_observers[index];
        if (observer->stream_group_active || !cupidbuild_observer_require_current(observer, 0))
            return cupidbuild_host_external_input_failure(
                transaction, "borrowed external observer changed or capture is active");
    }
    return 1;
}

int cupidbuild_host_transaction_borrow_external_observer(cupidbuild_host_transaction_t *transaction,
                                                         cupidbuild_host_observer_t *observer) {
    if (!transaction || transaction->external_binding_failed)
        return 0;
    if (transaction->runner_transaction || transaction->observer_binding_state != 1 ||
        transaction->observer_publication_started || !observer || observer->selection_only ||
        observer->stream_group_active || !observer->root ||
        transaction->external_observer_count >= CUPIDBUILD_HOST_INPUTS ||
        !cupidbuild_host_require_borrowed_observer(transaction) ||
        !cupidbuild_observer_require_current(observer, 0))
        return cupidbuild_host_external_input_failure(
            transaction, "external observer requires a valid primary and retained root");

    if (cupidbuild_observer_same(&observer->root->captured,
                                 &transaction->borrowed_observer->root->captured, 1))
        return cupidbuild_host_external_input_failure(transaction,
                                                    "external observer repeats the primary root");
    for (size_t index = 0u; index < transaction->external_observer_count; index++) {
        cupidbuild_host_observer_t *existing = transaction->external_observers[index];
        if (cupidbuild_observer_same(&observer->root->captured, &existing->root->captured, 1))
            return cupidbuild_host_external_input_failure(
                transaction, "external observer root may only be borrowed once");
    }
    transaction->external_observers[transaction->external_observer_count++] = observer;
    return 1;
}

static int cupidbuild_host_external_observer_is_borrowed(const cupidbuild_host_transaction_t *tx,
                                                         const cupidbuild_host_observer_t *observer) {
    for (size_t index = 0u; index < tx->external_observer_count; index++) {
        if (tx->external_observers[index] == observer)
            return 1;
    }
    return 0;
}

static int cupidbuild_host_read_external_input(cupidbuild_host_observer_t *observer,
                                               const char *logical, size_t capacity,
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

int cupidbuild_host_freeze_external_input(cupidbuild_host_transaction_t *transaction,
                                          cupidbuild_host_observer_t *observer, const char *logical,
                                          const char *private_name, unsigned long long capacity,
                                          const char **frozen_path_out,
                                          cupidbuild_host_snapshot_t *snapshot_out) {
    if (frozen_path_out)
        *frozen_path_out = NULL;
    if (snapshot_out)
        (void)memset(snapshot_out, 0, sizeof(*snapshot_out));
    if (!transaction || transaction->external_binding_failed)
        return 0;
    if (transaction->observer_publication_started || !observer || !logical || capacity == 0u ||
        capacity > 2147483647ULL || !cupidbuild_host_path_is_relative_safe(private_name) ||
        !cupidbuild_host_external_observer_is_borrowed(transaction, observer) ||
        !cupidbuild_host_require_borrowed_observer(transaction))
        return cupidbuild_host_external_input_failure(
            transaction, "external input could not be captured through its retained observer");
    if (!cupidbuild_host_freeze_input_internal(transaction, logical, private_name, frozen_path_out,
                                              snapshot_out, (size_t)capacity, 1, 0, observer)) {
        transaction->external_binding_failed = 1;
        return 0;
    }
    return 1;
}
