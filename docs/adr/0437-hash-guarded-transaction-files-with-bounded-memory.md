# ADR 0437: Hash guarded transaction files with bounded memory

## Status

Implemented and tested privately on 2026-10-06. Producer qualification and
large disk transactions remain open.

## Context

Candidate capture already accepts a null payload output. The transaction's
retained and named snapshot readers nevertheless allocate the complete file
before hashing it. Publication repeats those reads to check the candidate,
previous output, aliases and recovery bindings. A large digest-only request
therefore still requires enough address space for the whole payload.

The retained observer has a bounded SHA-256 context, but observing a file does
not replace the transaction's file ownership and publication protocol. The
transaction readers need the same bounded hashing within their existing
retained-handle and no-follow path checks.

## Decision

Route all twelve regular-file snapshot readers through one internal content
reader. It hashes ordered blocks of at most 65,536 bytes through the existing
incremental SHA-256 implementation. A null payload output uses a fixed block.
A requested payload retains its complete allocation, caller ownership and
trailing NUL byte. Both modes produce the same digest and metadata.

Windows resets the retained handle's position and uses `ReadFile`. Hosted
Linux and native POSIX use positioned reads; interrupted reads retry and short
positive reads feed their actual lengths into the hash. A failed or premature
zero-byte read fails the snapshot. The caller retains the existing ordinary
file checks, size checks, metadata representation and close behavior.

Keep the public snapshot layout and every existing file extent limit. The
ordinary candidate, initial output, frozen input and private output still stop
at 64 MiB. The explicit private-input-capacity writer keeps its own recorded
capacity. This change adds no large candidate constructor, streamed frozen
input writer, sink callback, flush guarantee or publication authority.

## Evidence

Fifteen methods are selected through native and Cupid-built callers on Windows
and Linux: sixty selections, 56 executions and four Windows skips for POSIX
address-space tests. Complete 48 MiB plus 65-byte candidates publish into absent
and changed outputs; equal output keeps its identity and modification time.
Private files, empty files, SHA padding and block boundaries, returned payloads,
forged digests, restored-time source drift and the old size limits are checked.

Both Linux callers publish a complete large candidate and reuse an equally
large previous output under a fixed 32 MiB address-space limit. The retained
pre-change native host succeeds without that limit, then rejects the candidate
and previous-output cases under the same bound. This distinguishes bounded
snapshot storage from the former whole-file allocation.

CupidDis accepts known instructions, local targets and code anchors for every
new checked component. The 11,296-byte contract object is identical across
Cupid compilers, with SHA-256
`ed502819e0a014f27539673b44d553a0003f541d7ddb3740bb17ef85fb59f50c`.
The complete observer regression passes 300 selections, with 288 executions
and twelve declared skips. Their native Windows/Linux and checked
Windows/Linux split is five, two, four and one. Forty borrowed streamed-observer
boundary executions also pass. All nine explicit private-capacity methods pass
through all four callers, for 36 executions. Independent rereading checks all
88 complete transaction fixture invocations, original sources and commands,
linked component identities and the former-reader comparison. Evidence is
`snapshot-blocks-paired-independent1.json` under
`cupid-native-iso-proof-20261005`: 436 total selections, 420 executions and
sixteen declared skips. These include four actual fixed-memory Linux cases.

The first contract wrote directly over the POSIX candidate reservation and
bypassed the checked writer's sealing step. Using the ordinary frozen-writer
protocol resolves that test error without changing production code. The native
Windows writer also needs delete sharing for retained candidates and binary
stdout; it now uses the existing Win32 file interfaces. The initial harness
misread preserved oversized Windows private files as successful cleanup. It
now checks rejection and retained recovery evidence. The first Cupid contract
used `strtoul`, which the hosted header does not declare; its bounded decimal
control protocol now uses the same explicit parsing as the other host
contracts. Original failed commands, callers and logs remain retained. No
production file limit or accepted test timeout changes.

The Windows private-capacity regression reaches the inherited controller's
600-second whole-module limit after five methods pass. Its original log and
identical linked caller remain retained. All nine individual methods then pass
under their original 600-second bounds; the longest takes 433 seconds. Neither
test nor production limits change. A collector initially assigned the ordinary skips to the wrong host
combinations. It now checks the original reports' five/two/four/one split;
the tests and total of twelve skips are unchanged.
