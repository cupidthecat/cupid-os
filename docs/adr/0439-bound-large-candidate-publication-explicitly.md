# ADR 0439: Bound large candidate publication explicitly

## Status

Implemented privately on 2026-10-06. Complete 200 MiB publication controls and
ordinary observer, private-capacity and snapshot regressions pass with native
and Cupid-built producers on both hosts. The second checked-tool invocation
also passes with candidates above 64 MiB. Complete compiler qualification, seed
carriage and normal disk publication remain open.

## Context

The disk composer can produce the complete 200 MiB image with fixed sector
buffers. Digest-only transaction reads also use bounded memory. The existing
transaction constructor still rejects a candidate or previous output above
64 MiB, so those two capabilities cannot yet share a publication boundary.

The public snapshot carries `size_t`. Changing that layout would also change
every existing caller, discovery list and publication contract. The current
disk image fits the hosted size representation.

## Decision

Add `cupidbuild_host_transaction_open_bounded` with an explicit unsigned
64-bit capacity argument. Validate the complete argument before converting it
to `size_t`. Admit capacities from one through 2,147,483,647 bytes. Reject zero,
larger values and a missing result pointer before creating transaction state,
files or locks.

Record the capacity in the opaque transaction. Apply it to the candidate,
previous output, retained handles, named aliases, publication checks, Windows
sealing and recovery reads. Cleanup uses the accepted snapshot's own extent
when it reopens and verifies a captured file. Cleanup of an owned writable
handle keeps its existing identity check because the checked writer may have
changed that file. Existing constructors
continue to select the 64 MiB limit.

Keep source and checked-tool freezing, discovery, captured process streams
and private-output capacities under their existing rules. A requested candidate
payload remains capped at 64 MiB. Digest-only capture can check the larger
extent with the same fixed 64 KiB block. A rejected payload capture does not
mark the candidate captured or prevent a later digest-only request.

This interface preserves the existing public snapshot layout. It provides
bounded publication for the current image; it does not represent larger
high-word file extents. Streamed frozen-input copies, full-range snapshots,
flush guarantees, independent disk validation and the normal recipe handoff
remain separate work.

## Evidence and failed approaches

The controls publish complete 200 MiB candidates into absent and changed
destinations, reuse equal output without changing its identity or timestamp,
and compare the complete bytes through an independent SHA-256 reader. Linux
also publishes over a large previous output under a fixed 32 MiB address-space
limit. Negative controls cover zero and high-word capacities, overflow,
candidate and previous-output size excess, the unchanged ordinary limit,
payload rejection and forged digests.

The four complete regression runs select 396 methods. All 382 executed methods
pass, with 14 expected platform skips. Independent rereading checks the retained
logs, callers and products, plus 88 snapshot invocations and 60 complete
candidate files. Corresponding objects and executables agree between native
and Cupid-built producers on each host. The Linux snapshot suites retain both
32 MiB address-space cases. The original suite and case limits remain unchanged.
Evidence is `stack-publication-regressions2-four-producer-independent.json`;
[ADR 0444](0444-fold-proven-scalar-load-stack-transfers.md) records the four
complete large-candidate suites and the compiler prototype's limits.

Review found one remaining Windows reopen through the ordinary 64 MiB wrapper.
After the first checked writer sealed a large candidate, the second checked
tool failed before launch. That transition now uses the transaction's accepted
capacity. The retained identity, digest, sharing and single-capture protocol
remain checked.

The retained old API reproduces the failure for a 64 MiB plus one-byte candidate
and passes a 65-byte control. Fresh native and complete prepared compiler
producers pass the repaired controls on both hosts: ten method executions,
fourteen checked invocations and two expected Windows skips. The second tool
reads the complete candidate. A deliberately wrong extent makes it return 94
and preserves the previous output. Both Linux producers also pass under a
32 MiB address-space limit. Independent rereading checks all 35 captured inputs,
complete output digests and namespace cleanup. Corresponding objects and
programs agree within each host; the Linux API object is unchanged because the
repair applies only to Windows. Evidence is
`stack-candidate-reopen1-four-producer-independent.json`.

The first fixture used stdio helpers absent from the hosted header and failed
to compile. The second fixture attempted a second public candidate capture
after the checked tool returned. The corrected fixture uses the supported
buffered reader and reuses the first accepted snapshot. Both failures remain
retained. This 35-input publisher proof is separate from the earlier frozen
compiler preparation source and does not qualify a new complete cohort.

The first payload-recovery fixture wrote directly over the POSIX candidate
reservation. Its later digest capture correctly rejected that unrecorded
candidate. The corrected fixture freezes and runs the checked writer through
the existing transaction protocol. The original source, command, executable
and failed report remain retained under `disk-candidate-extent1-native-linux`.
The corrected source also checks a null cleanup expectation before reading its
extent. No capacity or test deadline changes.
