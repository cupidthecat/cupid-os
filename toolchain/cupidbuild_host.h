#ifndef CUPID_TOOLCHAIN_BUILD_HOST_H
#define CUPID_TOOLCHAIN_BUILD_HOST_H

#include <stdint.h>
#if !defined(CUPID_HOSTED_SIZE_T_DEFINED)
#include <stddef.h>
#endif

typedef struct cupidbuild_host_transaction cupidbuild_host_transaction_t;
typedef struct cupidbuild_host_profile_parent cupidbuild_host_profile_parent_t;
typedef struct cupidbuild_host_output_parent cupidbuild_host_output_parent_t;

/* Prepare a normalized relative output's directory chain under an absolute
 * root. All components are validated before creating directories. No files or
 * locks are created. Created directories persist after failure and close.
 * Retained handles reject links and track directory identity, not timestamps.
 * Failure can return a partial preparation: always close it. Binding borrows
 * preparation until the transaction is closed and enforces the complete chain
 * at every subsequent publication-boundary check. Close accepts NULL. */
int cupidbuild_host_output_parent_prepare(
    const char *root, const char *output,
    cupidbuild_host_output_parent_t **preparation_out);
/* Retain an existing normalized output-parent chain without creating any
 * directories, files or locks. Missing parents fail. The same binding,
 * revalidation, partial-result and close rules apply as for preparation. */
int cupidbuild_host_output_parent_open_existing(
    const char *root, const char *output,
    cupidbuild_host_output_parent_t **preparation_out);
/* Resolve existing root and parent aliases, reject linked leaves and paths
 * outside the physical root, then retain the physical output chain. Resolution
 * directory identities must match the retained root and parent. This creates
 * no namespace entries. The resolved strings belong to preparation. */
int cupidbuild_host_output_parent_resolve_existing(
    const char *root, const char *source, const char *output,
    cupidbuild_host_output_parent_t **preparation_out);
const char *cupidbuild_host_output_parent_resolved_root(
    const cupidbuild_host_output_parent_t *preparation);
const char *cupidbuild_host_output_parent_resolved_source(
    const cupidbuild_host_output_parent_t *preparation);
const char *cupidbuild_host_output_parent_resolved_output(
    const cupidbuild_host_output_parent_t *preparation);
int cupidbuild_host_output_parent_require_current(
    cupidbuild_host_output_parent_t *preparation);
int cupidbuild_host_output_parent_bind(
    cupidbuild_host_output_parent_t *preparation,
    cupidbuild_host_transaction_t *transaction);
/* Bind the chain before acquiring the output lock or capturing source bytes. */
int cupidbuild_host_output_transaction_open(
    const char *root, const char *source, const char *output,
    cupidbuild_host_output_parent_t *preparation,
    cupidbuild_host_transaction_t **transaction_out);
const char *cupidbuild_host_output_parent_error(
    const cupidbuild_host_output_parent_t *preparation);
int cupidbuild_host_output_parent_close(
    cupidbuild_host_output_parent_t *preparation);
typedef struct cupidbuild_host_observer cupidbuild_host_observer_t;
/* Execution image format for this adapter: ELF32 is 1, PE32 is 2. */
unsigned int cupidbuild_host_execution_format(void);
/* Resolve a bounded root spelling against this process's working directory.
 * This only produces an absolute spelling. Callers must normalize it and
 * retain its filesystem identity before using it for a transaction. */
int cupidbuild_host_absolute_root(const char *root, char *output, size_t capacity);

/* Read-only observations retain live handles until close. Repository roots are
 * absolute (ordinary drive or UNC share roots on Windows). Logical paths are
 * UTF-8, repository-relative, and reject empty, dot, parent and link components.
 * Opening an observer creates no files. A failed operation poisons the observer;
 * require_unchanged cannot subsequently succeed. Close accepts NULL and reports
 * handle-close failures. Root identity, link count, size and modification time
 * are retained at open and rechecked; other directories retain identity only.
 * Repeated ancestor directories are reused only after a fresh binding and
 * original-handle identity check; explicit leaves remain separate observations.
 * Ordinary limits: 4096 retained entries, 4096 expected names in
 * total, 8191 path bytes, and 1023 bytes per UTF-8 component.
 */
int cupidbuild_host_observer_open(const char *repository_root,
                                 cupidbuild_host_observer_t **observer_out);
/* Explicit retained-entry budget. Records allocate only when observed. Zero
 * and SIZE_MAX reject before resolution and clear the owner output. Ordinary
 * open keeps its 4096-entry budget; membership and batch limits stay separate. */
int cupidbuild_host_observer_open_quota(const char *repository_root,
                                       size_t entry_quota,
                                       cupidbuild_host_observer_t **observer_out);
/* Read-only source-input names with the same retained custody, quotas,
 * poison, UTF-8 and byte limits as ordinary open. POSIX additionally accepts
 * colon and backslash component bytes. Windows keeps its ordinary drive/UNC
 * policy. The selected profile stays with this owner for every observation,
 * membership comparison and revalidation; ordinary open is unchanged. */
int cupidbuild_host_observer_open_source_quota(const char *repository_root,
    size_t entry_quota, cupidbuild_host_observer_t **observer_out);
/* Resolve an absolute UTF-8 source directory request and retain its physical
 * directory before closing the resolution handle. The owner copies both
 * request and physical names; physical_root_out borrows storage through close.
 * Complete validation and borrowed publication also recheck the original
 * request binding. Failure clears both supplied outputs. */
int cupidbuild_host_observer_resolve_source_root_quota(const char *requested_root,
    size_t entry_quota, cupidbuild_host_observer_t **observer_out,
    const char **physical_root_out);
/* Temporary read-only source selection. Retain the resolved directory
 * handle and original request binding without walking its physical
 * ancestors again. This observer cannot bind a publication transaction.
 * Compare selected file and parent identities with a complete retained
 * observer and validate that observer before and after selection.
 * Outputs, quotas, source names and borrowed lifetime match root resolve. */
/* Begin a temporary selection from its first retained directory resolution.
 * Capture the leaf, then require complete source validation before using it.
 * This observer remains ineligible for transaction borrowing. */
int cupidbuild_host_observer_begin_source_selection_quota(
    const char *requested_root, size_t entry_quota,
    cupidbuild_host_observer_t **observer_out, const char **physical_root_out);

int cupidbuild_host_observer_resolve_source_selection_quota(const char *requested_root,
    size_t entry_quota, cupidbuild_host_observer_t **observer_out,
    const char **physical_root_out);
/* With bytes_out == NULL, observe metadata without reading the payload.
 * Otherwise read at most min(limit, 64 MiB) bytes and return caller-owned malloc
 * storage. size_out is required; metadata-only observations retain 64-bit sizes.
 * Failure clears both outputs. Empty regular files are valid observations.
 * Validation compares metadata and, for payload reads, the captured SHA-256.
 * Metadata alone does not detect same-size edits with restored timestamps.
 */
int cupidbuild_host_observer_file(cupidbuild_host_observer_t *observer,
                                 const char *logical, size_t limit,
                                 unsigned char **bytes_out, uint64_t *size_out);
typedef struct {
  uint64_t size;
  unsigned char sha256[32];
} cupidbuild_host_stream_observation_t;
typedef int (*cupidbuild_host_stream_sink_t)(
    void *context, uint64_t offset, const unsigned char *bytes, size_t count);
/* Read a regular file through its retained handle using at most 65536 payload
 * bytes at a time. limit is a full 64-bit byte limit; SHA-256 lengths must be
 * below 2^61 bytes. A null sink hashes without copying. Otherwise callbacks are
 * synchronous, ordered, nonempty and at most 65536 bytes; zero means failure.
 * The buffer belongs to the operation and is valid only during the callback.
 * Calls and callbacks must be serialized; a callback must not reenter this
 * observer or modify its input bytes or result storage. result_out is required
 * and cleared before any check. Success retains the whole-file SHA-256 for
 * bounded rereading at require_unchanged and borrowed publication boundaries.
 * Any failure poisons the observer and leaves the result cleared. A sink may
 * already have copied bytes: its caller must discard that partial capture.
 * The caller owns stable copies and their lifetime; this creates no files and
 * does not flush, validate a candidate or authorize publication. */
int cupidbuild_host_observer_file_stream(
    cupidbuild_host_observer_t *observer, const char *logical, uint64_t limit,
    cupidbuild_host_stream_sink_t sink, void *context,
    cupidbuild_host_stream_observation_t *result_out);
/* Stream an already retained regular-file entry without creating another
 * observation. Reopen through its retained parent, check all original bindings
 * before and after streaming, preserve a previous digest, and upgrade that
 * same record's handle only on success. Failure poisons the observer and clears
 * result_out. Sink, limit, ownership and serialization rules are unchanged. */
int cupidbuild_host_observer_file_stream_retained(
    cupidbuild_host_observer_t *observer, const char *logical, uint64_t limit,
    cupidbuild_host_stream_sink_t sink, void *context,
    cupidbuild_host_stream_observation_t *result_out);

typedef struct {
  const char *logical;
  uint64_t limit;
  cupidbuild_host_stream_sink_t sink;
  void *context;
} cupidbuild_host_retained_stream_request_t;
/* Capture serialized retained files with complete observer validation before
 * and after the group. Every member checks its no-follow leaf, held metadata,
 * payload and immediate parent before/after capture; the whole group checks
 * every original ancestor, retained entry, earlier digest and membership.
 * No new observation is created. Existing single-file streaming is unchanged.
 * Requests/results and callback storage must remain valid and unmodified for
 * the call. A callback must not reenter or close this observer. Transaction
 * borrowing and validation of an already borrowed observer reject while the
 * capture is active. A callback's copied bytes remain provisional until this
 * call succeeds. Failure poisons the observer and clears all count results;
 * copied bytes must be discarded. An overflowing count rejects before any
 * array access or output write. Zero count accepts null arrays and still
 * validates the observer. This API grants no publication authority. */
int cupidbuild_host_observer_files_stream_retained(
    cupidbuild_host_observer_t *observer,
    const cupidbuild_host_retained_stream_request_t *requests, size_t count,
    cupidbuild_host_stream_observation_t *results_out);

/* Compare already retained identities after full ordinary revalidation. These
 * operations discover no leaf and grant no authority. Failure clears same_out
 * and poisons all supplied observers. Logical file paths must have been
 * retained as regular-file records by the corresponding observer. */
int cupidbuild_host_observer_roots_same(cupidbuild_host_observer_t *left,
                                       cupidbuild_host_observer_t *right,
                                       int *same_out);
int cupidbuild_host_observer_files_same(cupidbuild_host_observer_t *left,
                                       const char *left_logical,
                                       cupidbuild_host_observer_t *right,
                                       const char *right_logical,
                                       int *same_out);
/* Historical captured identities only: no metadata, digest, absence or binding
 * revalidation. Existing poison cannot be bypassed. Intended for serialized
 * construction followed by full observer and original-path validation before
 * exposing the resulting owner. Later publication checks are never cached. */
int cupidbuild_host_observer_roots_captured_same(cupidbuild_host_observer_t *left,
                                                cupidbuild_host_observer_t *right,
                                                int *same_out);
int cupidbuild_host_observer_files_captured_same(cupidbuild_host_observer_t *left,
                                                const char *left_logical,
                                                cupidbuild_host_observer_t *right,
                                                const char *right_logical,
                                                int *same_out);
/* Compare the captured parent-directory identities of two already retained
 * regular files. Like captured file/root comparisons, this observes nothing
 * and performs no revalidation. It grants no source authority. Full observer
 * and original-request validation must bracket its use before borrowed views.
 * Invalid, unobserved or poisoned inputs clear same_out and poison both owners. */
/* Compare cached regular-file and parent identities with one lookup per
 * observer. Observe and fully validate complete roots before and after
 * use. This adds no observations and grants no publication authority.
 * Invalid captures clear same_out and poison both supplied observers. */
int cupidbuild_host_observer_file_bindings_captured_same(
    cupidbuild_host_observer_t *left, const char *left_logical,
    cupidbuild_host_observer_t *right, const char *right_logical, int *same_out);
/* Recheck an original absolute request against an already retained file.
 * Resolve its physical parent twice, compare parent identity and file metadata,
 * and reject links, replacement or unavailable paths. Add no observations.
 * parent_identity_only is 0 for a required input and 1 for an optional stage.
 * Complete observer validation, including payloads, must bracket this call.
 * Failure poisons the supplied observer. This grants no publication authority. */
int cupidbuild_host_observer_require_file_request_binding(
    cupidbuild_host_observer_t *observer, const char *logical,
    const char *requested_absolute, const char *physical_absolute,
    int parent_identity_only);
/* Recheck original file bindings grouped by identical original parents.
 * Requests, names and callback context stay valid and stable throughout the call.
 * Resolve each parent before and after its group; retain every no-follow file,
 * captured parent identity and file-metadata comparison. A required request
 * also checks unchanged parent metadata across the group. Callback results are
 * provisional until success and arrive in parent order, with original indices.
 * Count overflow rejects before array access; zero accepts null requests.
 * Failure poisons the observer. Active capture rejects closure, complete revalidation,
 * recursive groups and new borrowing. Established borrowing retains its existing
 * lifetime and error rules. This adds no observations or publication authority.
 * The caller must bracket this operation with complete root and payload checks,
 * just as for the ordinary single-request binding validator. */
typedef struct {
  const char *logical;
  const char *requested_absolute;
  const char *physical_absolute;
  int parent_identity_only;
} cupidbuild_host_file_request_t;
typedef int (*cupidbuild_host_file_request_sink_t)(void *context, size_t index,
    const cupidbuild_host_file_request_t *request);
int cupidbuild_host_observer_require_file_request_bindings(
    cupidbuild_host_observer_t *observer,
    const cupidbuild_host_file_request_t *requests, size_t count,
    cupidbuild_host_file_request_sink_t sink, void *context);
int cupidbuild_host_observer_file_parents_captured_same(
    cupidbuild_host_observer_t *left, const char *left_logical,
    cupidbuild_host_observer_t *right, const char *right_logical, int *same_out);
typedef enum {
  CUPIDBUILD_ENTRY_NONE = 0,
  CUPIDBUILD_ENTRY_FILE = 1,
  CUPIDBUILD_ENTRY_DIRECTORY = 2
} cupidbuild_host_entry_kind_t;
/* Retain a regular file or directory without reading payload or membership.
 * An empty logical path selects the retained repository root. kind_out is
 * required and is cleared on failure. Links and special files fail and poison
 * the observer. Final validation retains the same metadata and parent-binding
 * checks as explicit file/directory observations. Capture file bytes or exact
 * directory membership separately when those facts are required. */
int cupidbuild_host_observer_kind(cupidbuild_host_observer_t *observer,
                                 const char *logical,
                                 cupidbuild_host_entry_kind_t *kind_out);
/* Observe a normalized optional path. NONE is a successful retained first
 * absence under the nearest existing no-follow parent; later appearance or
 * lookup failure poisons the observer. Existing regular files/directories keep
 * ordinary metadata observations. This grants no authority or whole-observer
 * verdict. Invalid input clears kind_out and poisons the supplied observer. */
int cupidbuild_host_observer_path_kind(cupidbuild_host_observer_t *observer,
    const char *logical, cupidbuild_host_entry_kind_t *kind_out);
typedef int (*cupidbuild_host_path_observation_sink_t)(void *context,
    size_t index, const char *logical, cupidbuild_host_entry_kind_t kind);
/* Observe normalized logical paths with complete validation before and after
 * the serialized group. Every path keeps immediate parent/root, fresh no-follow
 * leaf and retained metadata checks. The callback receives provisional kinds;
 * failure clears kinds_out and poisons the observer. Callbacks must not reenter
 * or close this observer. They may close separate temporary selectors after
 * comparing captured bindings. All request and result storage must remain stable
 * through the call. Overflow rejects before array access or writes; zero count
 * accepts null arrays and still validates. This grants no publication authority. */
int cupidbuild_host_observer_paths_kind(cupidbuild_host_observer_t *observer,
    const char *const *logical, size_t count,
    cupidbuild_host_path_observation_sink_t sink, void *context,
    cupidbuild_host_entry_kind_t *kinds_out);
/* Observe a group on a temporary selection observer with source validation
 * at both boundaries. Uncaptured sibling changes in its root are allowed;
 * descendant identity, leaf metadata and any captured payload or membership
 * remain checked. Complete observers reject this selection-only entry point.
 * Provisional results, serialization, failure, storage and lifetime rules
 * match paths_kind. Complete retained custody must bracket adoption. */
int cupidbuild_host_observer_source_selection_paths_kind(
    cupidbuild_host_observer_t *observer, const char *const *logical, size_t count,
    cupidbuild_host_path_observation_sink_t sink, void *context,
    cupidbuild_host_entry_kind_t *kinds_out);
typedef enum {
  CUPIDBUILD_OBSERVATION_OK = 0,
  CUPIDBUILD_OBSERVATION_UNAVAILABLE = 1,
  CUPIDBUILD_OBSERVATION_MISSING = 2,
  CUPIDBUILD_OBSERVATION_LINKED = 3,
  CUPIDBUILD_OBSERVATION_PARENT = 4,
  CUPIDBUILD_OBSERVATION_KIND = 5
} cupidbuild_observation_issue_t;

typedef struct {
  uint64_t size;
  int observed;
  cupidbuild_observation_issue_t issue;
} cupidbuild_host_file_observation_t;
/* Observe a bounded batch of regular-file metadata under this retained root.
 * At most 4096 paths. Zero count permits null arrays. For valid count and writable
 * results, every row is cleared before argument or prior-failure checks.
 * A successful row has observed == 1, no issue and its full 64-bit size. Failed
 * rows have zero observed/size and a diagnostic issue. A failed capture may be
 * classified again through its retained parent; concurrent replacement can change
 * the diagnostic but cannot create a successful observation.
 * Partial results are diagnostic only: any failed row poisons the observer,
 * even though this call continues collecting later rows. Returns one only when
 * all rows succeeded. Subsequent calls and require_unchanged still reject poison.
 * An out-of-range count leaves outputs untouched. Input paths and writable result
 * storage must remain valid, disjoint and unmodified throughout the call.
 */
int cupidbuild_host_observer_files(cupidbuild_host_observer_t *observer,
                                   const char *const *logical_paths,
                                   size_t count,
                                   cupidbuild_host_file_observation_t *results);

/* Retain directory identity and exact UTF-8 membership. Order is irrelevant;
 * duplicate or unsafe expected names are invalid. Empty logical names select
 * the repository root. File-kind checks belong to individual observations.
 */
int cupidbuild_host_observer_directory(cupidbuild_host_observer_t *observer,
                                      const char *logical,
                                      const char *const *expected,
                                      size_t expected_count);
typedef struct cupidbuild_host_directory_snapshot
    cupidbuild_host_directory_snapshot_t;
/* Discover every directory name through its retained handle. Empty logical
 * paths select the root. The observer owns the opaque token and independent,
 * immutable copies of its names until observer_close; there is no token close.
 * Tokens remain valid across ordinary directory(expected) calls. Capture and
 * names access both require the complete ordinary observer unchanged.
 * Names are literal strict UTF-8, sorted by unsigned byte spelling, excluding
 * dot and parent entries. The existing ordinary name grammar applies on every
 * host, including its colon/backslash rejection on POSIX. No normalization or
 * suffix filtering occurs. Names establish neither child kinds nor payloads.
 *
 * At most 4096 tokens and 4096 copied token names per observer, in addition to
 * the existing 4096 retained-handle and 4096 membership-name bounds. Repeated
 * captures consume these budgets; even empty tokens consume a token slot.
 * Failure clears snapshot_out and permanently poisons a supplied observer.
 * Failed partial captures remain reclaimable by observer_close.
 */
int cupidbuild_host_observer_directory_snapshot(
    cupidbuild_host_observer_t *observer, const char *logical,
    const cupidbuild_host_directory_snapshot_t **snapshot_out);
/* Validate this token's ownership and the whole observer before exposing its
 * immutable view. Unknown/foreign/null tokens fail without dereferencing them.
 * Empty snapshots return names_out == NULL and count_out == 0 on success.
 * Both outputs are required and clear before argument or poison checks.
 * All request/output storage must be valid and disjoint. Calls are serialized;
 * callers must not reenter, modify returned names or close an observer in use.
 * Neither a valid token nor a successful access promises an atomic filesystem
 * snapshot after the last sequential check or grants publication authority.
 */
int cupidbuild_host_observer_directory_snapshot_names(
    cupidbuild_host_observer_t *observer,
    const cupidbuild_host_directory_snapshot_t *snapshot,
    const char *const **names_out, size_t *count_out);
int cupidbuild_host_observer_require_unchanged(
    cupidbuild_host_observer_t *observer);
/* Revalidate source captures, first absences, exact memberships and original
 * source-root bindings. The retained root's directory identity is checked while
 * its unrelated namespace writes are allowed. Explicit root membership remains
 * exact. Ordinary require_unchanged keeps its complete root-metadata contract. */
int cupidbuild_host_observer_require_source_unchanged(
    cupidbuild_host_observer_t *observer);
/* Query the image command's complete absolute UTF-8 source pathname. One means
 * the leaf is absent and no leaf link/reparse point exists. Parent aliases are
 * followed. Broken-parent children are missing; file-parent children are missing
 * on Windows and reject on POSIX. Other lookup failures, present leaves, invalid
 * text and nonabsolute requests return zero. This retains nothing, creates no
 * files and grants no source or publication authority. Recheck at each consuming
 * boundary; ordinary observers keep their retained first-absence contracts. */
int cupidbuild_host_source_missing(const char *requested_absolute);
const char *cupidbuild_host_observer_error(
    const cupidbuild_host_observer_t *observer);
int cupidbuild_host_observer_close(cupidbuild_host_observer_t *observer);

/* Borrow one successful observer rooted at this transaction's retained root.
 * The caller keeps it alive through transaction close. Every publication
 * boundary rechecks captured file bytes/metadata, ancestor identities and exact
 * directory memberships, including the check after candidate installation.
 * Root directory size/time are excluded because publication owns namespace
 * writes there; its original identity and all captured memberships stay checked.
 * The ordinary read-only observer API keeps its complete root metadata check.
 * Bind only once, before any publication attempt. A failed or repeated bind forbids
 * publication for this transaction. No ownership transfers or files are made.
 * Observed publication inputs still require normal freeze/alias validation. */
int cupidbuild_host_transaction_borrow_observer(
    cupidbuild_host_transaction_t *transaction,
    cupidbuild_host_observer_t *observer);

typedef struct {
  size_t size;
  unsigned char sha256[32];
  unsigned int identity[4];
  unsigned int modified[2];
  unsigned int changed[2];
  int present;
} cupidbuild_host_snapshot_t;

int cupidbuild_host_snapshot_equal(
    const cupidbuild_host_snapshot_t *left,
    const cupidbuild_host_snapshot_t *right);

typedef struct {
  char **paths;
  cupidbuild_host_snapshot_t *snapshots;
  size_t count;
  size_t capacity;
} cupidbuild_host_path_list_t;

int cupidbuild_host_transaction_open(
    const char *repository_root, const char *source_logical,
    const char *output_logical, cupidbuild_host_transaction_t **transaction_out);
/* Explicit candidate and previous-output extent, 1..2147483647 bytes.
 * Source, seed, payload and private-stream limits keep their existing rules.
 * Digest-only capture uses bounded memory; requested payloads still allocate
 * the complete candidate. Invalid capacities create no transaction or files. */
int cupidbuild_host_transaction_open_bounded(
    const char *repository_root, const char *source_logical,
    const char *output_logical, unsigned long long candidate_capacity,
    cupidbuild_host_transaction_t **transaction_out);
int cupidbuild_host_profile_transaction_open(
    const char *repository_root, const char *source_logical,
    const char *output_logical,
    cupidbuild_host_profile_parent_t *profile_parent,
    cupidbuild_host_transaction_t **transaction_out);
int cupidbuild_host_runner_open(
    const char *working_directory,
    cupidbuild_host_transaction_t **transaction_out);
int cupidbuild_host_transaction_close(
    cupidbuild_host_transaction_t *transaction);
int cupidbuild_host_publication_committed(
    const cupidbuild_host_transaction_t *transaction);

int cupidbuild_host_profile_parent_prepare(
    const char *repository_root,
    cupidbuild_host_profile_parent_t **preparation_out);
void cupidbuild_host_profile_parent_commit(
    cupidbuild_host_profile_parent_t *preparation);
int cupidbuild_host_profile_parent_close(
    cupidbuild_host_profile_parent_t *preparation);
const char *cupidbuild_host_profile_parent_error(
    const cupidbuild_host_profile_parent_t *preparation);
int cupidbuild_host_profile_parent_bind(
    cupidbuild_host_profile_parent_t *preparation,
    cupidbuild_host_transaction_t *transaction);

int cupidbuild_host_freeze_input(cupidbuild_host_transaction_t *transaction,
                                 const char *live_path,
                                 const char *private_name,
                                 const char **frozen_path_out,
                                 cupidbuild_host_snapshot_t *snapshot_out);
/* Copy one repository input with fixed 64 KiB storage and an explicit
 * 1..2147483647-byte extent. Its accepted capacity remains attached to the
 * input through every live and frozen revalidation. Runner transactions and
 * requested payloads retain their ordinary limits. Results clear on failure. */
int cupidbuild_host_freeze_input_bounded(
    cupidbuild_host_transaction_t *transaction, const char *live_path,
    const char *private_name, unsigned long long capacity,
    const char **frozen_path_out, cupidbuild_host_snapshot_t *snapshot_out);
/* Capture the initial output once through its retained publication authority.
 * The transaction's candidate capacity bounds the fixed-block copy. An absent
 * initial output succeeds with cleared results and creates no copy. Existing
 * output bytes become a sealed private input; original output binding remains
 * subject to the publication and recovery guards. After replacement, input
 * checks validate this private copy rather than rereading the public pathname.
 * Ordinary input/output aliases still fail. Runner, unsafe-name, repeated or
 * post-publication requests fail, clear results and forbid publication. */
int cupidbuild_host_freeze_previous_output(
    cupidbuild_host_transaction_t *transaction, const char *private_name,
    const char **frozen_path_out, cupidbuild_host_snapshot_t *snapshot_out);
int cupidbuild_host_reserve_inputs(
    cupidbuild_host_transaction_t *transaction, size_t capacity);
unsigned char *cupidbuild_host_read_frozen_input(
    cupidbuild_host_transaction_t *transaction, const char *frozen_path,
    size_t limit, size_t *size_out);
int cupidbuild_host_make_input_executable(
    cupidbuild_host_transaction_t *transaction, const char *frozen_path);
int cupidbuild_host_seed_members_exact(
                                       cupidbuild_host_transaction_t *transaction,
                                       const char *directory,
                                       const char *suffix,
                                       const char *const *expected,
                                       size_t expected_count);
int cupidbuild_host_discover_files(
    cupidbuild_host_transaction_t *transaction,
    const char *const *logical_roots,
    size_t root_count, const char *const *suffixes, size_t suffix_count,
    int skip_hidden_files, int reject_matching_nonfiles,
    cupidbuild_host_path_list_t *paths_out);
int cupidbuild_host_seal_discovery(
    cupidbuild_host_transaction_t *transaction);
int cupidbuild_host_begin_compile_discovery(
    cupidbuild_host_transaction_t *transaction);
int cupidbuild_host_seal_compile_discovery(
    cupidbuild_host_transaction_t *transaction);
void cupidbuild_host_path_list_close(cupidbuild_host_path_list_t *paths);
int cupidbuild_host_input_matches_snapshot(
    cupidbuild_host_transaction_t *transaction, const char *live_path,
    const cupidbuild_host_snapshot_t *expected);
void cupidbuild_host_sha256_bytes(const unsigned char *contents, size_t size,
                                  unsigned char digest[32]);
const char *cupidbuild_host_frozen_source(
    const cupidbuild_host_transaction_t *transaction);
const char *cupidbuild_host_candidate(
    const cupidbuild_host_transaction_t *transaction);
const char *cupidbuild_host_private_output(
    const cupidbuild_host_transaction_t *transaction);

int cupidbuild_host_run(cupidbuild_host_transaction_t *transaction,
                        const char *tool, const char *const *arguments,
                        unsigned int timeout_milliseconds);
int cupidbuild_host_run_in_private(
    cupidbuild_host_transaction_t *transaction, const char *tool,
    const char *const *arguments, unsigned int timeout_milliseconds);
int cupidbuild_host_run_to_private_output(
    cupidbuild_host_transaction_t *transaction, const char *tool,
    const char *const *arguments, unsigned int timeout_milliseconds);
int cupidbuild_host_run_captured(cupidbuild_host_transaction_t *transaction,
                                 const char *tool,
                                 const char *const *arguments,
                                 unsigned int timeout_milliseconds);
int cupidbuild_host_forward_captured(
    cupidbuild_host_transaction_t *transaction);
int cupidbuild_host_capture_candidate(
    cupidbuild_host_transaction_t *transaction,
    cupidbuild_host_snapshot_t *snapshot_out,
    unsigned char **bytes_out);
int cupidbuild_host_require_candidate(
    cupidbuild_host_transaction_t *transaction,
    const cupidbuild_host_snapshot_t *expected);
int cupidbuild_host_capture_private_output(
    cupidbuild_host_transaction_t *transaction,
    cupidbuild_host_snapshot_t *snapshot_out,
    unsigned char **bytes_out);
int cupidbuild_host_require_private_output(
    cupidbuild_host_transaction_t *transaction,
    const cupidbuild_host_snapshot_t *expected);
int cupidbuild_host_write_private_output(
    cupidbuild_host_transaction_t *transaction, const unsigned char *bytes,
    size_t size);
/* Write a private checked input with an explicit 1..2147483647-byte capacity.
 * Capture and revalidation retain that capacity until the next successful write
 * or private-output tool launch. The original writer and tool-produced private
 * outputs keep their existing bound. Candidate, public output, frozen-input and
 * tool-stream bounds are unchanged. Failure before replacement keeps the prior
 * private input; close the transaction after a failed physical write. */
int cupidbuild_host_write_private_output_bounded(
    cupidbuild_host_transaction_t *transaction, const unsigned char *bytes,
    size_t size, size_t capacity);
int cupidbuild_host_require_inputs(
    cupidbuild_host_transaction_t *transaction);
int cupidbuild_host_require_frozen_inputs(
    cupidbuild_host_transaction_t *transaction);
int cupidbuild_host_require_publication_boundary(
    cupidbuild_host_transaction_t *transaction);
int cupidbuild_host_publish(cupidbuild_host_transaction_t *transaction);
int cupidbuild_host_publish_if_changed(
    cupidbuild_host_transaction_t *transaction, int *changed_out);

const char *cupidbuild_host_error(
    const cupidbuild_host_transaction_t *transaction);

#if defined(CUPIDBUILD_HOST_CLOSE_FAILURE_TEST) && !defined(_WIN32)
void cupidbuild_host_close_failure_test_arm(unsigned int close_index);
#endif

#endif
