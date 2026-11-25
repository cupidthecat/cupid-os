/**
 * @file cupidbuild_host_frozen_inputs.inc
 * @brief Preserve checked tool output and read transaction-owned input ranges.
 */

/* Metadata brackets use the original frozen capture; no new digest authority
 * or input lifetime is established by a range read. */
static int cupidbuild_host_input_range_current(cupidbuild_host_transaction_t *transaction,
                                               const cupidbuild_host_input_t *input) {
    cupidbuild_host_snapshot_t held;
    cupidbuild_host_snapshot_t named;
    if (!cupidbuild_host_private_root_is_owned(transaction))
        return 0;
#if defined(_WIN32)
    if (input->frozen_handle == INVALID_HANDLE_VALUE ||
        !cupidbuild_host_windows_regular_metadata(input->frozen_handle, &held) ||
        !cupidbuild_host_windows_relative_regular_metadata(transaction->private_handle,
                                                           input->frozen_name, 0, &named) ||
        !cupidbuild_host_snapshot_identity_equal(&held, &named))
        return 0;
#else
    if (input->frozen_descriptor < 0 ||
        !cupidbuild_host_open_regular_metadata(input->frozen_descriptor, &held))
        return 0;
    if (!cupidbuild_host_private_entry_is_anonymous(transaction, input->frozen_name) &&
        (!cupidbuild_host_regular_metadata_at(
             transaction->private_descriptor,
             cupidbuild_host_private_entry_name(transaction, input->frozen_name), &named) ||
         !cupidbuild_host_snapshot_identity_equal(&held, &named)))
        return 0;
#endif
    return cupidbuild_host_store_metadata_equal(&held, &input->frozen_snapshot);
}

int cupidbuild_host_read_frozen_input_range(cupidbuild_host_transaction_t *transaction,
                                            const char *frozen_path, unsigned long long offset,
                                            unsigned char *bytes, size_t size) {
    unsigned int index;
    cupidbuild_host_input_t *input = (cupidbuild_host_input_t *)0;
    unsigned char *staged = (unsigned char *)0;
    int valid;
    if (!cupidbuild_host_store_enter(transaction))
        return 0;
    valid = !transaction->runner_transaction && !transaction->candidate_store_failed &&
            frozen_path != (const char *)0 && size <= 65536u && (bytes || !size);
    if (valid) {
        for (index = 0u; index < transaction->input_count; index++) {
            if (!strcmp(transaction->inputs[index].frozen_path, frozen_path)) {
                input = &transaction->inputs[index];
                break;
            }
        }
        valid = input != (cupidbuild_host_input_t *)0 && offset <= input->frozen_snapshot.size &&
                (unsigned long long)size <= input->frozen_snapshot.size - offset &&
                cupidbuild_host_input_range_current(transaction, input);
    }
    if (valid && size) {
        staged = (unsigned char *)malloc(size);
        valid = staged != (unsigned char *)0;
    }
    if (valid)
        valid = cupidbuild_host_read_at(
#if defined(_WIN32)
                    input->frozen_handle,
#else
                    input->frozen_descriptor,
#endif
                    offset, staged, size) &&
                cupidbuild_host_input_range_current(transaction, input);
    if (valid && size)
        memcpy(bytes, staged, size);
    free(staged);
    if (!valid && !transaction->error[0])
        cupidbuild_host_set_error(transaction, "frozen input range request failed");
    return cupidbuild_host_store_leave(transaction, valid);
}

static int cupidbuild_host_discard_unused_candidate(cupidbuild_host_transaction_t *transaction) {
    cupidbuild_host_snapshot_t current;
    if (transaction->candidate_captured || transaction->candidate_publish_created ||
        transaction->candidate_published || transaction->publication_committed ||
        transaction->candidate_store_state != CUPIDBUILD_CANDIDATE_STORE_CLOSED ||
        transaction->candidate_store_failed ||
        transaction->candidate_snapshot.size)
        return 0;
    if (!transaction->candidate_snapshot.present)
        return 1;
    if (!cupidbuild_host_read_retained_private_regular(transaction, "candidate.o",
                                                       transaction->candidate_limit,
#if defined(_WIN32)
                                                       transaction->candidate_handle,
#else
                                                       transaction->candidate_descriptor,
#endif
                                                       &current, (unsigned char **)0) ||
        !cupidbuild_host_snapshot_equal(&current, &transaction->candidate_snapshot))
        return 0;
#if defined(_WIN32)
    if (!(transaction->candidate_sealed
              ? cupidbuild_host_windows_dispose_read_retained_at(
                    transaction->private_handle, "candidate.o", &transaction->candidate_snapshot,
                    &transaction->candidate_handle)
              : cupidbuild_host_windows_dispose_retained_at(
                    transaction->private_handle, "candidate.o", &transaction->candidate_snapshot,
                    &transaction->candidate_handle)))
        return 0;
#else
    if (!cupidbuild_host_cleanup_retained_regular_at(
            transaction->private_descriptor,
            cupidbuild_host_private_entry_name(transaction, "candidate.o"),
            &transaction->candidate_snapshot, &transaction->candidate_descriptor))
        return 0;
#endif
    memset(&transaction->candidate_snapshot, 0, sizeof(transaction->candidate_snapshot));
    transaction->candidate_sealed = 0;
    return 1;
}

int cupidbuild_host_freeze_tool_output(cupidbuild_host_transaction_t *transaction,
                                       const char **frozen_path_out,
                                       cupidbuild_host_snapshot_t *snapshot_out) {
    cupidbuild_host_snapshot_t accepted;
    cupidbuild_host_input_t *input;

    if (frozen_path_out)
        *frozen_path_out = NULL;
    if (snapshot_out)
        (void)memset(snapshot_out, 0, sizeof(*snapshot_out));
    if (!transaction)
        return 0;
    accepted = transaction->private_output_snapshot;
    if (!frozen_path_out || !snapshot_out || transaction->runner_transaction ||
        !transaction->private_output_ready || transaction->private_output_transferred ||
        transaction->observer_publication_started || transaction->candidate_captured ||
        transaction->candidate_store_state != CUPIDBUILD_CANDIDATE_STORE_CLOSED ||
        transaction->candidate_store_failed || transaction->candidate_snapshot.size ||
        !accepted.present || !cupidbuild_host_require_inputs(transaction) ||
        !cupidbuild_host_require_frozen_inputs(transaction) ||
        !cupidbuild_host_capture_retained_private_output(transaction) ||
        !cupidbuild_host_snapshot_equal(&accepted, &transaction->private_output_snapshot) ||
        !cupidbuild_host_discard_unused_candidate(transaction) ||
        cupidbuild_host_find_input_identity(transaction, &accepted) ||
        !cupidbuild_host_register_input(transaction, transaction->private_output,
                                        transaction->private_output, "candidate.map", &accepted,
                                        cupidbuild_host_private_output_read_limit(transaction))) {
        transaction->candidate_store_failed = 1;
        cupidbuild_host_set_error(transaction, "checked output cannot become a frozen input");
        return 0;
    }

    input = &transaction->inputs[transaction->input_count - 1u];
    input->frozen_snapshot = accepted;
    input->frozen_tool_output = 1;
#if defined(_WIN32)
    input->frozen_handle = transaction->private_output_handle;
    transaction->private_output_handle = INVALID_HANDLE_VALUE;
#else
    input->frozen_descriptor = transaction->private_output_descriptor;
    transaction->private_output_descriptor = -1;
#endif
    transaction->private_output_transferred = 1;
    transaction->private_output_ready = 0;
    transaction->private_output_captured = 0;
    transaction->private_output_sealed = 0;
    (void)memset(&transaction->private_output_snapshot, 0, sizeof(transaction->private_output_snapshot));
    transaction->private_output[0] = '\0';
    *frozen_path_out = input->frozen_path;
    *snapshot_out = accepted;
    return 1;
}
