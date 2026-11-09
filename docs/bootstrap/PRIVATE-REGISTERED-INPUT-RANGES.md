# Bounded registered-input reads

The private adapter can read a registered ordinary or transferred frozen input
without allocating its complete payload. Seventy range executions and eight
real four-thread executions pass through native and Cupid-built callers on
Linux and Windows. Two checked-caller cases retain explicit skips for the
native-only post-read pause hook. Active sources and installed seeds are unchanged.

## Interface and custody

`cupidbuild_host_read_frozen_input_range` accepts a registered borrowed path,
unsigned 64-bit offset and at most 65,536 bytes. Empty ranges permit a null
buffer through EOF. Unknown inputs, invalid ranges and failed store state
reject the call, leave caller bytes unchanged and prevent publication.

The implementation checks the retained identity, accepted extent, precise
timestamps and named binding before and after I/O. It stages only the requested
range and shares the store lock, including Windows file-position changes.
Input registration and table growth keep their caller-exclusive contract.
Complete digest validation remains at final publication; range reads cannot
replace the accepted snapshot with a new baseline.

The implementation lives in a separate include. Existing host bytes remain
an exact prefix before its added include; the public header adds one interface.

## Checks

Complete ordinary, transferred explicit, actual CupidObj ELF, empty and
3 MiB-plus-33-byte payloads publish byte for byte through bounded composition.
Controls cover final short chunks, EOF, null/unknown requests, 65,537-byte
requests, range ends, 64-bit overflow and invalid offsets beyond two and four
GiB. Valid reads above four GiB are not established by this selection.

Real metadata edits before reads run in all four callers. Native post-read
controls change Linux mtime by one nanosecond or Windows mtime by 100 ns after
the actual read, preserving payload and identity. Rejection leaves the entire
64 KiB caller buffer unchanged. Four real threads also interleave 256 registered
reads with 256 store writes and reads per stable caller. A concurrent invalid
input range prevents finish and publication.

All original 180-second range and 60-second thread bounds remain. Linux retains
32 MiB and 10,240 descriptors, with the accepted 256 KiB thread stacks and
checked lower-stack canary. Initial failures concern fixture indentation and
an unguarded native-only environment protocol. The first independent checker
also assumes LF for a Windows-created include suffix; the corrected check
retains complete prefix-byte equality. These failures remain recorded.

The [evidence archive](evidence/private-registered-input-ranges-20261109.json.gz)
contains 379 file records, 84 embedded payloads and fourteen references to the
qualified producer archive. Its SHA-256 is
`3ee319a6268966e31fc2007adf37933d634534dae6ccc8c4336b4f727f35a373`.
## Complete retained images

Twenty additional cases use the preserved 209,715,200-byte OS image. Twelve
reconstructions read and write all 3,200 bounded chunks; eight EOF/capacity
controls preserve the previous public image. Both retained previous-output
capture and explicit bounded-input capture pass. Windows runs eight cases;
Linux runs twelve, including three ordinary-input repetitions per caller.
The adapter, header and three implementation includes match v48 exactly.

Initial fixtures incorrectly use the ordinary primary-source limit, then omit
closing a nonnull owner returned after an open failure. Correcting those callers
requires no adapter change. A later Linux run returns 99 while storage is full;
its caller does not record errno, so the exact error remains unattributed. The
diagnostic replays pass after space recovery with the original memory limits.
The original uninstrumented Linux caller subsequently passes all eight original
cases after storage recovery, using the same program bytes and original limits.
The [replay record](RETAINED-PUBLISHER-MANUAL-IMAGE.md) links that separate evidence.

Independent review rereads the retained Windows outputs. Linux duplicate images
are retired after complete hash and block comparisons; review checks their
canonical source and retirement records rather than claiming another output
reread after removal. The original image remains preserved locally.

The [complete-image archive](evidence/private-complete-image-input-ranges-20261109.json.gz)
retains 130 file records, 55 local payloads and fourteen qualified-archive
references. Its SHA-256 is
`0b26ede93fc8ef1f55eb34d45793e59a019e93f15f8d42b51c490b47d1b59e9e`.
The [combined retained publisher](PRIVATE-RETAINED-CHECKED-PUBLISHER.md) now
passes its separate sixteen-case reconstruction. Active ABI/source-plan
integration and qualification remain open.
