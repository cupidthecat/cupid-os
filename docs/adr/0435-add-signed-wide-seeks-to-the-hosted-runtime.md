# ADR 0435: Add signed wide seeks to the hosted runtime

The disk composer accepts 64-bit source lengths and offsets. Its retained host
adapters need file positioning beyond the i386 `long` range. Add
`cupid_fseek64(FILE *, long long, int)` and status-returning
`cupid_ftell64(FILE *, long long *)` to the hosted stdio interface. Keep the
standard `fseek` and `ftell` signatures and implementations unchanged.

The extension covers serialized use of seekable regular-file streams and
nonnegative positions within the signed 64-bit range. Reject an invalid origin
or negative absolute offset with `EINVAL` before a host seek. A missing tell
output is invalid; a provided output is cleared before a failing tell. Failed
operations set the hosted stream's sticky error flag. The API does not promise
snapshot lifetime, filesystem validation, durable flush or publication.

On Windows, reuse the existing `SetFilePointer` shim with a nonnull high-word
pointer. Derive both words from the complete signed displacement. A low result
of `0xffffffff` is a failure only when immediate `GetLastError` is nonzero;
the API clears last error on this ambiguous success path. Map invalid-parameter
and negative-result seek errors to `EINVAL` within this extension. The existing
stdio error mapper keeps its behavior. Append positioning at open and before
each write uses the same wide helper. No new Windows import is required.

On Linux i386, call `_llseek` through the existing five-argument syscall shim
and copy its eight-byte result only after success. Propagate returned errno
without retrying a relative seek. Open regular stdio files with `O_LARGEFILE`;
wide seeking alone cannot admit a file above the signed 32-bit size limit.
No startup assembly changes are required.

Seventeen real sparse-file methods pass through native and freshly Cupid-built
callers on both hosts, for 68 caller/method combinations. They cover positions
at two GiB and beyond four GiB, both directions of wide relative seeks, end
seeks, writes, append after rewind, the all-ones low-word boundary, recovery
after a host seek error, exact `EINVAL`/`EBADF`/`ENOENT` results and null outputs.
Ten existing update-mode methods also pass through both callers on both hosts.
Both existing host-specific runtime contracts pass with the changed runtime.
CupidDis certifies every newly compiled object. Independent rereading checks
the retained commands, source bytes, object identities, touched ranges, holes
and logical lengths. It compares the native and Cupid stream-error conventions
separately.

Each ordinary sparse fixture has a 4,294,967,361-byte logical length; the append
boundary fixture begins at 4,294,967,295 bytes. Recorded Windows allocation is
between zero and 262,144 bytes; Linux allocation is 16,384 bytes. These are
offset and bounded-storage checks, not complete four-GiB image hash comparisons.
The first Windows harness names its mode executable `update.exe` and triggers
installer detection. The neutral `stdio-modes.exe` name passes. The first
independent collector assumes positive allocated size and rejects a valid zero
report; its correction preserves the original one-MiB upper bound. The first
Linux fixtures under `/tmp` disappear before independent rereading. Fresh
closed runs retain their sparse files under `/var/tmp`; the earlier passing
command receipts remain distinct from accepted retained evidence.

Evidence is `wide-file-private-paired-independent.json` under
`cupid-native-iso-proof-20261005`, with the original runs and failed collectors.
This is a private source capability. Complete producer qualification, seed
carriage, retained source capture and the normal disk publisher handoff remain
open. [The platform notes](../bootstrap/HOSTED-WIDE-FILE-IO-NOTES.md) retain the
primary API sources. `TempleOS/` remains read-only and excluded.
