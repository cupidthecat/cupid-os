/**
 * @file cupidbuild_host_checked_process.inc
 * @brief Run a checked tool and retain its outputs.
 */

int cupidbuild_host_publication_committed(const cupidbuild_host_transaction_t *transaction) {
    return transaction != (const cupidbuild_host_transaction_t *)0 &&
           transaction->publication_committed != 0;
}

const char *cupidbuild_host_frozen_source(const cupidbuild_host_transaction_t *transaction) {
    return transaction == (const cupidbuild_host_transaction_t *)0 ? (const char *)0
                                                                   : transaction->frozen_source;
}

const char *cupidbuild_host_candidate(const cupidbuild_host_transaction_t *transaction) {
    return transaction == (const cupidbuild_host_transaction_t *)0 ? (const char *)0
                                                                   : transaction->candidate;
}

const char *cupidbuild_host_private_output(const cupidbuild_host_transaction_t *transaction) {
    return transaction == (const cupidbuild_host_transaction_t *)0 ? (const char *)0
                                                                   : transaction->private_output;
}

static const cupidbuild_host_input_t *
cupidbuild_host_frozen_tool_input(const cupidbuild_host_transaction_t *transaction,
                                  const char *tool) {
    unsigned int index;
    if (transaction == (const cupidbuild_host_transaction_t *)0 || tool == (const char *)0) {
        return (const cupidbuild_host_input_t *)0;
    }
    for (index = 0u; index < transaction->input_count; index++) {
        if (strcmp(transaction->inputs[index].frozen_path, tool) == 0) {
            return &transaction->inputs[index];
        }
    }
    return (const cupidbuild_host_input_t *)0;
}

int cupidbuild_host_run_captured(cupidbuild_host_transaction_t *transaction, const char *tool,
                                 const char *const *arguments, unsigned int timeout_milliseconds) {
    const cupidbuild_host_input_t *tool_input;
    cupidbuild_host_snapshot_t stdout_snapshot;
    cupidbuild_host_snapshot_t stderr_snapshot;
#if defined(_WIN32)
    cupidbuild_host_snapshot_t working_handle_snapshot;
    cupidbuild_host_snapshot_t working_path_snapshot;
#endif
    int result;
    if (transaction == (cupidbuild_host_transaction_t *)0 ||
        transaction->runner_transaction == 0 ||
        tool == (const char *)0 || arguments == (const char *const *)0 ||
        timeout_milliseconds == 0u || transaction->captured != 0 ||
        !cupidbuild_host_require_frozen_inputs(transaction)) {
        if (transaction != (cupidbuild_host_transaction_t *)0) {
            cupidbuild_host_set_error(transaction, "checked tool runner request is invalid");
        }
        return -1;
    }
    tool_input = cupidbuild_host_frozen_tool_input(transaction, tool);
    if (tool_input == (const cupidbuild_host_input_t *)0) {
        cupidbuild_host_set_error(transaction, "checked tool is not a frozen input");
        return -1;
    }
#if defined(_WIN32)
    if (!cupidbuild_host_windows_directory_handle_snapshot(transaction->working_directory_handle,
                                                           &working_handle_snapshot) ||
        !cupidbuild_host_directory_snapshot(transaction->repository_root, &working_path_snapshot) ||
        !cupidbuild_host_snapshot_identity_equal(&working_handle_snapshot,
                                                 &transaction->repository_root_snapshot) ||
        !cupidbuild_host_snapshot_identity_equal(&working_path_snapshot,
                                                 &transaction->repository_root_snapshot)) {
        cupidbuild_host_set_error(transaction, "checked tool working directory changed");
        return -1;
    }
#endif
    result = cupidbuild_host_run_process(
        tool, &tool_input->frozen_snapshot,
#if defined(_WIN32)
        tool_input->frozen_handle,
#else
        tool_input->frozen_descriptor,
#endif
        arguments, transaction->tool_stdout, transaction->tool_stderr,
#if defined(_WIN32)
        transaction->tool_stdout_handle, transaction->tool_stderr_handle,
#else
        transaction->tool_stdout_descriptor, transaction->tool_stderr_descriptor,
#endif
        transaction->repository_root,
#if defined(_WIN32)
        -1,
#else
        transaction->output_parent_descriptor,
#endif
        (const cupidbuild_host_transaction_t *)0, &transaction->tool_stdout_snapshot,
        &transaction->tool_stderr_snapshot, timeout_milliseconds);
    if (!cupidbuild_host_require_frozen_inputs(transaction)) {
        cupidbuild_host_set_error(transaction,
                                  "private checked seed changed while checked tool ran");
        return -1;
    }
#if defined(_WIN32)
    if (!cupidbuild_host_windows_transition_retained_at(
            transaction->private_handle, "tool.stdout", &transaction->tool_stdout_snapshot,
            &transaction->tool_stdout_handle, 1, 0, 1) ||
        !cupidbuild_host_windows_transition_retained_at(
            transaction->private_handle, "tool.stderr", &transaction->tool_stderr_snapshot,
            &transaction->tool_stderr_handle, 1, 0, 1)) {
        cupidbuild_host_set_error(transaction, "checked tool output could not be sealed");
        return -1;
    }
    transaction->tool_stdout_sealed = 1;
    transaction->tool_stderr_sealed = 1;
#endif
#if !defined(_WIN32)
    if (!cupidbuild_host_seal_anonymous(transaction->tool_stdout_descriptor) ||
        !cupidbuild_host_seal_anonymous(transaction->tool_stderr_descriptor)) {
        cupidbuild_host_set_error(transaction, "checked tool output could not be sealed");
        return -1;
    }
    if (!cupidbuild_host_read_open_file(transaction->tool_stdout_descriptor,
                                        CUPIDBUILD_HOST_STREAM_LIMIT, &stdout_snapshot,
                                        (unsigned char **)0) ||
        !cupidbuild_host_read_open_file(transaction->tool_stderr_descriptor,
                                        CUPIDBUILD_HOST_STREAM_LIMIT, &stderr_snapshot,
                                        (unsigned char **)0)) {
#else
    {
        cupidbuild_host_snapshot_t stdout_named;
        cupidbuild_host_snapshot_t stderr_named;
        if (!cupidbuild_host_windows_read_open_regular(transaction->tool_stdout_handle,
                                                       CUPIDBUILD_HOST_STREAM_LIMIT,
                                                       &stdout_snapshot, (unsigned char **)0) ||
            !cupidbuild_host_windows_read_open_regular(transaction->tool_stderr_handle,
                                                       CUPIDBUILD_HOST_STREAM_LIMIT,
                                                       &stderr_snapshot, (unsigned char **)0) ||
            !cupidbuild_host_read_regular_limit(transaction->tool_stdout, 0,
                                                CUPIDBUILD_HOST_STREAM_LIMIT, &stdout_named,
                                                (unsigned char **)0) ||
            !cupidbuild_host_read_regular_limit(transaction->tool_stderr, 0,
                                                CUPIDBUILD_HOST_STREAM_LIMIT, &stderr_named,
                                                (unsigned char **)0) ||
            !cupidbuild_host_snapshot_equal(&stdout_snapshot, &stdout_named) ||
            !cupidbuild_host_snapshot_equal(&stderr_snapshot, &stderr_named)) {
#endif
        cupidbuild_host_set_error(transaction, "checked tool output could not be captured");
        return -1;
    }
#if defined(_WIN32)
}
#endif
if (!cupidbuild_host_snapshot_identity_equal(&stdout_snapshot,
                                             &transaction->tool_stdout_snapshot) ||
    !cupidbuild_host_snapshot_identity_equal(&stderr_snapshot,
                                             &transaction->tool_stderr_snapshot)) {
    cupidbuild_host_set_error(transaction, "checked tool output identity changed while tool ran");
    return -1;
}
transaction->tool_stdout_snapshot = stdout_snapshot;
transaction->tool_stderr_snapshot = stderr_snapshot;
if (result < 0 && result != -2) {
    cupidbuild_host_set_error(transaction, "checked tool could not be started or captured");
    return -1;
}
if (stdout_snapshot.present == 0 || stderr_snapshot.present == 0) {
    cupidbuild_host_set_error(transaction, "checked tool could not be started or captured");
    return -1;
}
if (result == -2) {
    cupidbuild_host_set_error(transaction, "checked tool timed out");
    return -2;
}
transaction->captured = 1;
return result;
}

int cupidbuild_host_forward_captured(cupidbuild_host_transaction_t *transaction) {
    cupidbuild_host_snapshot_t stdout_snapshot;
    cupidbuild_host_snapshot_t stderr_snapshot;
    unsigned char *stdout_bytes = (unsigned char *)0;
    unsigned char *stderr_bytes = (unsigned char *)0;
    int success;
    if (transaction == (cupidbuild_host_transaction_t *)0 || transaction->runner_transaction == 0 ||
        transaction->captured == 0) {
        if (transaction != (cupidbuild_host_transaction_t *)0) {
            cupidbuild_host_set_error(transaction, "checked tool output is not available");
        }
        return 0;
    }
#if !defined(_WIN32)
    if (!cupidbuild_host_read_open_file(transaction->tool_stdout_descriptor,
                                        CUPIDBUILD_HOST_STREAM_LIMIT, &stdout_snapshot,
                                        &stdout_bytes) ||
        !cupidbuild_host_read_open_file(transaction->tool_stderr_descriptor,
                                        CUPIDBUILD_HOST_STREAM_LIMIT, &stderr_snapshot,
                                        &stderr_bytes) ||
#else
        {
            cupidbuild_host_snapshot_t stdout_named;
            cupidbuild_host_snapshot_t stderr_named;
            if (!cupidbuild_host_windows_read_open_regular(transaction->tool_stdout_handle,
                                                           CUPIDBUILD_HOST_STREAM_LIMIT,
                                                           &stdout_snapshot, &stdout_bytes) ||
                !cupidbuild_host_windows_read_open_regular(transaction->tool_stderr_handle,
                                                           CUPIDBUILD_HOST_STREAM_LIMIT,
                                                           &stderr_snapshot, &stderr_bytes) ||
                !cupidbuild_host_read_regular_limit(transaction->tool_stdout, 0,
                                                    CUPIDBUILD_HOST_STREAM_LIMIT, &stdout_named,
                                                    (unsigned char **)0) ||
                !cupidbuild_host_read_regular_limit(transaction->tool_stderr, 0,
                                                    CUPIDBUILD_HOST_STREAM_LIMIT, &stderr_named,
                                                    (unsigned char **)0) ||
                !cupidbuild_host_snapshot_equal(&stdout_snapshot, &stdout_named) ||
                !cupidbuild_host_snapshot_equal(&stderr_snapshot, &stderr_named) ||
#endif
        !cupidbuild_host_snapshot_equal(&stdout_snapshot, &transaction->tool_stdout_snapshot) ||
        !cupidbuild_host_snapshot_equal(&stderr_snapshot, &transaction->tool_stderr_snapshot)) {
        free(stdout_bytes);
        free(stderr_bytes);
        cupidbuild_host_set_error(transaction, "checked tool output cannot be read");
        return 0;
    }
#if defined(_WIN32)
}
#endif
#if defined(_WIN32) && !defined(CUPID_HOSTED_I386_WINDOWS_H)
{
    int stdout_mode = -1;
    int stderr_mode = -1;
    success =
        fflush(stdout) == 0 && ferror(stdout) == 0 && fflush(stderr) == 0 && ferror(stderr) == 0;
    if (success != 0) {
        stdout_mode = _setmode(_fileno(stdout), _O_BINARY);
        stderr_mode = _setmode(_fileno(stderr), _O_BINARY);
        success = stdout_mode >= 0 && stderr_mode >= 0 &&
                  fwrite(stdout_bytes, 1u, (size_t)stdout_snapshot.size, stdout) == stdout_snapshot.size &&
                  fflush(stdout) == 0 && ferror(stdout) == 0 &&
                  fwrite(stderr_bytes, 1u, (size_t)stderr_snapshot.size, stderr) == stderr_snapshot.size &&
                  fflush(stderr) == 0 && ferror(stderr) == 0;
    }
    if (stdout_mode >= 0 && _setmode(_fileno(stdout), stdout_mode) < 0) {
        success = 0;
    }
    if (stderr_mode >= 0 && _setmode(_fileno(stderr), stderr_mode) < 0) {
        success = 0;
    }
}
#else
            success =
                fwrite(stdout_bytes, 1u, (size_t)stdout_snapshot.size, stdout) == stdout_snapshot.size &&
                fflush(stdout) == 0 && ferror(stdout) == 0 &&
                fwrite(stderr_bytes, 1u, (size_t)stderr_snapshot.size, stderr) == stderr_snapshot.size &&
                fflush(stderr) == 0 && ferror(stderr) == 0;
#endif
free(stdout_bytes);
free(stderr_bytes);
if (success == 0) {
    cupidbuild_host_set_error(transaction, "checked tool output cannot be forwarded");
}
return success;
}

static int cupidbuild_host_run_at(cupidbuild_host_transaction_t *transaction, const char *tool,
                                  const char *const *arguments, unsigned int timeout_milliseconds,
                                  int private_working_directory) {
    const cupidbuild_host_input_t *tool_input;
    cupidbuild_host_snapshot_t stdout_snapshot;
    cupidbuild_host_snapshot_t stderr_snapshot;
    unsigned char *stdout_bytes = (unsigned char *)0;
    unsigned char *stderr_bytes = (unsigned char *)0;
    int result;
    (void)memset(&stdout_snapshot, 0, sizeof(stdout_snapshot));
    (void)memset(&stderr_snapshot, 0, sizeof(stderr_snapshot));
#if defined(CUPIDBUILD_PUBLICATION_RACE_TEST)
    if (!cupidbuild_host_publication_test_pause("before-tool-launch")) {
        return -1;
    }
#endif
    if (transaction == (cupidbuild_host_transaction_t *)0 ||
        transaction->candidate_store_state != CUPIDBUILD_CANDIDATE_STORE_CLOSED ||
        transaction->candidate_store_failed || transaction->private_output_transferred ||
        tool == (const char *)0 ||
        arguments == (const char *const *)0 || transaction->runner_transaction != 0 ||
        timeout_milliseconds == 0u || !cupidbuild_host_require_frozen_inputs(transaction)) {
        return -1;
    }
    tool_input = cupidbuild_host_frozen_tool_input(transaction, tool);
    if (tool_input == (const cupidbuild_host_input_t *)0) {
        cupidbuild_host_set_error(transaction, "checked tool is not a frozen input");
        return -1;
    }
    transaction->private_output_ready = 0;
    if (!cupidbuild_host_close_private_stream_handles(transaction) ||
        !cupidbuild_host_delete_owned_private_name(transaction, "tool.stdout",
                                                   &transaction->tool_stdout_snapshot) ||
        !cupidbuild_host_delete_owned_private_name(transaction, "tool.stderr",
                                                   &transaction->tool_stderr_snapshot) ||
        !cupidbuild_host_prepare_private_streams(transaction) ||
        !cupidbuild_host_prepare_private_candidate(transaction) ||
        !cupidbuild_host_prepare_private_output_path(transaction)) {
        cupidbuild_host_set_error(transaction, "private checked-tool entries cannot be prepared");
        return -1;
    }
#if defined(_WIN32)
    if (!cupidbuild_host_windows_transition_retained_at_limit(
            transaction->private_handle, "candidate.o", &transaction->candidate_snapshot,
            &transaction->candidate_handle, 0, 1, 1, transaction->candidate_limit) ||
        !cupidbuild_host_windows_transition_retained_at_limit(
            transaction->private_handle, "candidate.map", &transaction->private_output_snapshot,
            &transaction->private_output_handle, 0, 1, 1,
            cupidbuild_host_private_output_read_limit(transaction))) {
        cupidbuild_host_set_error(transaction, "private checked-tool entries cannot be reopened");
        return -1;
    }
    transaction->candidate_sealed = 0;
    transaction->private_output_sealed = 0;
#endif
    result = cupidbuild_host_run_process(
        tool, &tool_input->frozen_snapshot,
#if defined(_WIN32)
        tool_input->frozen_handle,
#else
                tool_input->frozen_descriptor,
#endif
        arguments, transaction->tool_stdout, transaction->tool_stderr,
#if defined(_WIN32)
        transaction->tool_stdout_handle, transaction->tool_stderr_handle,
#else
                transaction->tool_stdout_descriptor, transaction->tool_stderr_descriptor,
#endif
        private_working_directory != 0
#if !defined(_WIN32)
            ? (transaction->private_flat != 0 ? "/proc" : transaction->private_root)
#else
                    ? transaction->private_root
#endif
            : (const char *)0,
#if defined(_WIN32)
        -1,
#else
                private_working_directory != 0
                    ? (transaction->private_flat != 0 ? transaction->working_directory_descriptor
                                                      : transaction->private_descriptor)
                    : -1,
#endif
#if defined(_WIN32)
        (const cupidbuild_host_transaction_t *)0,
#else
                transaction->private_flat != 0 ? transaction
                                               : (const cupidbuild_host_transaction_t *)0,
#endif
        (cupidbuild_host_snapshot_t *)0, (cupidbuild_host_snapshot_t *)0, timeout_milliseconds);
    if (result == -2) {
        result = 124;
    }
#if defined(_WIN32)
    if (transaction->tool_stderr_sealed == 0 &&
        !cupidbuild_host_windows_transition_retained_at(
            transaction->private_handle, "tool.stderr", &transaction->tool_stderr_snapshot,
            &transaction->tool_stderr_handle, 1, 0, 1)) {
        result = -1;
    } else {
        transaction->tool_stderr_sealed = 1;
    }
#endif
    if (!cupidbuild_host_require_frozen_inputs(transaction) ||
        !cupidbuild_host_capture_private_streams(transaction) ||
        !cupidbuild_host_capture_retained_candidate(transaction) ||
        !cupidbuild_host_capture_retained_private_output(transaction)) {
        result = -1;
    }
    if (result >= 0 &&
        (!cupidbuild_host_read_retained_private_regular(transaction, "tool.stdout",
                                                        CUPIDBUILD_HOST_STREAM_LIMIT,
#if defined(_WIN32)
                                                        transaction->tool_stdout_handle,
#else
                                                                transaction->tool_stdout_descriptor,
#endif
                                                        &stdout_snapshot, &stdout_bytes) ||
         !cupidbuild_host_read_retained_private_regular(transaction, "tool.stderr",
                                                        CUPIDBUILD_HOST_STREAM_LIMIT,
#if defined(_WIN32)
                                                        transaction->tool_stderr_handle,
#else
                                                                transaction->tool_stderr_descriptor,
#endif
                                                        &stderr_snapshot, &stderr_bytes) ||
         !cupidbuild_host_snapshot_equal(&stdout_snapshot, &transaction->tool_stdout_snapshot) ||
         !cupidbuild_host_snapshot_equal(&stderr_snapshot, &transaction->tool_stderr_snapshot))) {
        result = -1;
    }
    if (result != 0 && stderr_bytes != (unsigned char *)0 && stderr_snapshot.size != 0u) {
        (void)fwrite(stderr_bytes, 1u, (size_t)stderr_snapshot.size, stderr);
    }
    if (result == 0 && stdout_snapshot.size != 0u) {
        cupidbuild_host_set_error(transaction, "checked tool wrote unexpected standard output");
        result = 125;
    } else if (result == 0 && stderr_snapshot.size != 0u) {
        cupidbuild_host_set_error(transaction, "checked tool wrote unexpected standard error");
        result = 125;
    }
    free(stdout_bytes);
    free(stderr_bytes);
    if (!cupidbuild_host_close_private_stream_handles(transaction) ||
        !cupidbuild_host_delete_owned_private_name(transaction, "tool.stdout",
                                                   &transaction->tool_stdout_snapshot) ||
        !cupidbuild_host_delete_owned_private_name(transaction, "tool.stderr",
                                                   &transaction->tool_stderr_snapshot)) {
        cupidbuild_host_set_error(transaction, "private checked-tool stream entry changed");
        result = -1;
    }
    if (!cupidbuild_host_require_frozen_inputs(transaction)) {
        return -1;
    }
    if (result < 0) {
        cupidbuild_host_set_error(transaction, "checked tool could not be started");
    } else if (result == 124) {
        cupidbuild_host_set_error(transaction, "checked tool timed out");
    }
    transaction->private_output_ready = result == 0;
    return result;
}

int cupidbuild_host_run(cupidbuild_host_transaction_t *transaction, const char *tool,
                        const char *const *arguments, unsigned int timeout_milliseconds) {
    return cupidbuild_host_run_at(transaction, tool, arguments, timeout_milliseconds,
#if defined(_WIN32)
                                  0);
#else
                                          transaction != (cupidbuild_host_transaction_t *)0 &&
                                              transaction->private_flat != 0);
#endif
}

int cupidbuild_host_run_in_private(cupidbuild_host_transaction_t *transaction, const char *tool,
                                   const char *const *arguments,
                                   unsigned int timeout_milliseconds) {
    return cupidbuild_host_run_at(transaction, tool, arguments, timeout_milliseconds, 1);
}

int cupidbuild_host_run_to_private_output(cupidbuild_host_transaction_t *transaction,
                                          const char *tool, const char *const *arguments,
                                          unsigned int timeout_milliseconds) {
    const cupidbuild_host_input_t *tool_input;
    cupidbuild_host_snapshot_t output_snapshot;
    cupidbuild_host_snapshot_t stderr_snapshot;
    unsigned char *stderr_bytes = (unsigned char *)0;
    int result;
    (void)memset(&output_snapshot, 0, sizeof(output_snapshot));
    (void)memset(&stderr_snapshot, 0, sizeof(stderr_snapshot));
    if (transaction == (cupidbuild_host_transaction_t *)0 ||
        transaction->candidate_store_state != CUPIDBUILD_CANDIDATE_STORE_CLOSED ||
        transaction->candidate_store_failed || transaction->private_output_transferred ||
        transaction->runner_transaction != 0 ||
        tool == (const char *)0 || arguments == (const char *const *)0 ||
        timeout_milliseconds == 0u || !cupidbuild_host_require_frozen_inputs(transaction)) {
        if (transaction != (cupidbuild_host_transaction_t *)0) {
            cupidbuild_host_set_error(transaction,
                                      "checked private-output tool request is invalid");
        }
        return -1;
    }
    tool_input = cupidbuild_host_frozen_tool_input(transaction, tool);
    if (tool_input == (const cupidbuild_host_input_t *)0) {
        cupidbuild_host_set_error(transaction, "checked tool is not a frozen input");
        return -1;
    }
    transaction->private_output_ready = 0;
    if (!cupidbuild_host_close_private_output_handle(transaction) ||
        !cupidbuild_host_delete_owned_private_name(transaction, "candidate.map",
                                                   &transaction->private_output_snapshot) ||
        !cupidbuild_host_close_private_stream_handles(transaction) ||
        !cupidbuild_host_delete_owned_private_name(transaction, "tool.stderr",
                                                   &transaction->tool_stderr_snapshot) ||
        !cupidbuild_host_prepare_private_output_stream(transaction) ||
        !cupidbuild_host_prepare_private_stderr(transaction)) {
        cupidbuild_host_set_error(transaction, "private checked-tool output cannot be prepared");
        return -1;
    }
    transaction->private_output_captured = 0;
    transaction->private_output_limit = CUPIDBUILD_HOST_FILE_LIMIT;
    result = cupidbuild_host_run_process(
        tool, &tool_input->frozen_snapshot,
#if defined(_WIN32)
        tool_input->frozen_handle,
#else
                tool_input->frozen_descriptor,
#endif
        arguments, transaction->private_output, transaction->tool_stderr,
#if defined(_WIN32)
        transaction->private_output_handle, transaction->tool_stderr_handle,
#else
                transaction->private_output_descriptor, transaction->tool_stderr_descriptor,
#endif
#if defined(_WIN32)
        (const char *)0, -1, (const cupidbuild_host_transaction_t *)0,
#else
                transaction->private_flat != 0 ? "/proc" : (const char *)0,
                transaction->private_flat != 0 ? transaction->working_directory_descriptor : -1,
                transaction->private_flat != 0 ? transaction
                                               : (const cupidbuild_host_transaction_t *)0,
#endif
        (cupidbuild_host_snapshot_t *)0, (cupidbuild_host_snapshot_t *)0, timeout_milliseconds);
    if (result == -2) {
        result = 124;
    }
#if defined(_WIN32)
    if (!cupidbuild_host_windows_transition_retained_at(
            transaction->private_handle, "tool.stderr", &transaction->tool_stderr_snapshot,
            &transaction->tool_stderr_handle, 1, 0, 1)) {
        result = -1;
    } else {
        transaction->tool_stderr_sealed = 1;
    }
#endif
#if !defined(_WIN32)
    if (transaction->private_flat != 0 && transaction->tool_stderr_sealed == 0) {
        if (!cupidbuild_host_seal_anonymous(transaction->tool_stderr_descriptor)) {
            result = -1;
        } else {
            transaction->tool_stderr_sealed = 1;
        }
    }
#endif
    if (!cupidbuild_host_require_frozen_inputs(transaction) ||
        !cupidbuild_host_capture_retained_private_output(transaction) ||
        !cupidbuild_host_read_retained_private_regular(transaction, "tool.stderr",
                                                       CUPIDBUILD_HOST_STREAM_LIMIT,
#if defined(_WIN32)
                                                       transaction->tool_stderr_handle,
#else
                                                               transaction->tool_stderr_descriptor,
#endif
                                                       &stderr_snapshot, (unsigned char **)0) ||
        !cupidbuild_host_snapshot_identity_equal(&stderr_snapshot,
                                                 &transaction->tool_stderr_snapshot)) {
        result = -1;
    } else {
        transaction->tool_stderr_snapshot = stderr_snapshot;
    }
    if (result >= 0 &&
        (!cupidbuild_host_read_retained_private_regular(transaction, "candidate.map",
                                                        CUPIDBUILD_HOST_STREAM_LIMIT,
#if defined(_WIN32)
                                                        transaction->private_output_handle,
#else
                                                                transaction
                                                                    ->private_output_descriptor,
#endif
                                                        &output_snapshot, (unsigned char **)0) ||
         !cupidbuild_host_read_retained_private_regular(transaction, "tool.stderr",
                                                        CUPIDBUILD_HOST_STREAM_LIMIT,
#if defined(_WIN32)
                                                        transaction->tool_stderr_handle,
#else
                                                                transaction->tool_stderr_descriptor,
#endif
                                                        &stderr_snapshot, &stderr_bytes) ||
         !cupidbuild_host_snapshot_equal(&output_snapshot, &transaction->private_output_snapshot) ||
         !cupidbuild_host_snapshot_equal(&stderr_snapshot, &transaction->tool_stderr_snapshot))) {
        result = -1;
    }
    if (result != 0 && stderr_bytes != (unsigned char *)0 && stderr_snapshot.size != 0u) {
        (void)fwrite(stderr_bytes, 1u, (size_t)stderr_snapshot.size, stderr);
    }
    if (result == 0 && stderr_snapshot.size != 0u) {
        cupidbuild_host_set_error(transaction, "checked tool wrote unexpected standard error");
        result = 125;
    }
    free(stderr_bytes);
    if (!cupidbuild_host_close_private_stream_handles(transaction) ||
        !cupidbuild_host_delete_owned_private_name(transaction, "tool.stderr",
                                                   &transaction->tool_stderr_snapshot)) {
        cupidbuild_host_set_error(transaction, "private checked-tool stream entry changed");
        result = -1;
    }
    if (!cupidbuild_host_require_frozen_inputs(transaction)) {
        result = -1;
    }
    if (result < 0) {
        cupidbuild_host_set_error(transaction, "checked tool could not be started");
    } else if (result == 124) {
        cupidbuild_host_set_error(transaction, "checked tool timed out");
    }
    transaction->private_output_ready = result == 0;
    return result;
}
