# ADR 0436: Stream retained file observations in bounded blocks

## Status

Implemented and tested privately on 2026-10-06. Complete producer qualification,
seed carriage and native disk publication remain open.

## Context

The disk publisher preserves a 200 MiB image. The retained observer can already
capture its 64-bit size and reject changes to its ancestor bindings, but its
byte-returning interface allocates the whole file and stops at 64 MiB. The
composer requires stable captured sources rather than a live host pathname.

## Decision

Add `cupidbuild_host_observer_file_stream` to the existing retained observer.
It reads through its retained regular-file handle, checks captured metadata
before and after reading, and records a SHA-256 digest of the complete payload.
The optional sink receives synchronous ordered blocks with their 64-bit byte
offsets. Blocks are nonempty and at most 65,536 bytes. A null sink records the
digest without copying. Empty files produce no sink call.

The required result contains a 64-bit size and 32 digest bytes. Clear it before
argument and prior-error checks. Any failure poisons the observer and leaves
the result cleared, even when a sink has already copied part of the file. The
caller discards that partial copy. Revalidation rereads the complete file with
the same bounded reader, including every borrowed publication boundary.

Keep the existing no-follow path handling, retained ancestors, ordinary-file
checks and close ownership. A stream capture does not make a live file
immutable. The caller owns its stable private copy and retains the observer
through final publication. Same-size edits with restored modification times
are checked against the recorded whole-file digest.

Factor SHA-256 into an internal initialize/update/finish context while retaining
the existing block transform and public byte-hash interface. Streamed lengths
must be below two to the power of 61 bytes so the SHA-256 bit count fits its
64-bit field. The operation holds one 64 KiB payload buffer and small hash and
metadata state; memory use does not grow with the observed file's size.

Calls and sink callbacks must be serialized. A sink must not reenter the
observer, change its input bytes or alter the result storage. The observer does
not open the sink's destination, flush it, independently validate a candidate
or grant publication authority. Large candidate transactions and a complete
disk publisher remain separate work.

## Evidence

Thirteen methods pass through native and Cupid-built callers on Windows and
Linux, for 52 method executions without skips. They compare complete 200 MiB
captures and files above the old 64 MiB byte-returning limit. SHA-256 padding and
stream boundaries, empty files, a null sink, high-word sizes and limits,
callback failure, invalid arguments, unsafe paths, truncation and same-size
edits with restored timestamps all have explicit checks. An edit to a block
already copied during capture is rejected by the final digest reread.

Ten borrowed-observer methods pass through both callers on both hosts, for
forty executions without skips. They exercise changed and equal publication,
timestamp reuse, restored-time payload drift, directory membership, invalid
binding and recovery. The complete ordinary observer regression selects
75 methods per caller/host combination: 300 selections, 288 executions and
twelve declared platform/runtime skips. Native and Cupid-built Linux callers
also hash and revalidate complete 200 MiB fixtures under a fixed 32 MiB process
address-space limit.

Independent rereading checks retained sources, original command results,
complete captured copies and every original caller/linked component. The
10,252-byte stream caller has SHA-256
`55fadfc686f5e20c49a18461a4ba988b708e5bac9c0503d5a67d26494cc6661d`;
the 27,932-byte borrowed caller has SHA-256
`75bd68bb7f832db02001e848fd82ee1b66d7836ef9ed625e984cb46658fc19ae`.
Both objects are identical across Cupid compilers. CupidDis accepts known
instructions, local targets and code anchors for all newly built checked
components and both regression callers. Evidence is
`observer-stream-paired-independent2.json` under
`cupid-native-iso-proof-20261005`.

The first contract used `getchar`, which the hosted stdio header does not
declare. Its replacement uses the existing `fread` interface. The original
native passes and rejected Cupid commands remain retained. The first Windows
Cupid-built ordinary regression hits the previously recorded ten-second
timeout while rejecting a high-word payload. All 75 methods pass on a complete
retry with the identical caller and unchanged bounds; the cause remains
unproven. Both failed-command and failed-log identities are bound into the
final evidence. No production bound changes follow from that timeout.

A separate check compiles the actual pure hash implementation with nineteen
payload lengths and eleven arbitrary read partitions. It compares 76 public
byte hashes and 836 partitioned hashes across all four callers, including valid
zero-length updates. Every one of the 912 digests agrees with Python SHA-256.
This exercises partial-block updates that ordinary regular-file reads do not
reliably produce. Evidence is `observer-stream-hash-paired-independent.json`.
The first Windows standalone link incorrectly reused the runtime built for
CupidBuild's UTF-8 wrappers. Selecting the matching standalone runtime resolves
that test-profile mismatch; the rejected link and unchanged caller remain
retained. Production source does not change for this additional hash check.
