# ADR 0429: Retain an explicit private-input capacity

## Status

Implemented and tested on 2026-10-05. Guarded ISO publication,
committed-source qualification, seed carriage and normal recipe handoff remain
open.

## Context

The hosted CupidObj bundle reader permits a 64 MiB payload allowance plus
1,056,784 bytes of bounded manifest and entry metadata. Its image output remains
64 MiB. CupidBuild's private writer and readers previously used the same 64 MiB
bound for both inputs and outputs. Long repeated logical names can make a valid
request exceed that bound while the produced image still fits.

The measured fixture declares seven nested directories with 127-byte components
and 505 files with 64-byte names and 131,072-byte payloads. It reaches 512 entries,
eight directory levels and a 960-byte logical path. The complete bundle is
67,176,834 bytes; the independent Python image is 66,342,912 bytes. The committed
adapter rejects the private request before a tool launch.

## Decision

Add `cupidbuild_host_write_private_output_bounded` with a capacity from one through
2,147,483,647 bytes. The private input retains that capacity through complete
capture, digest revalidation and ordinary checked-tool launches. Windows handle
transitions use the same capacity while preserving retained and named identity
checks. A successful replacement starts a new capacity lifetime; launching a
tool whose standard output becomes the private artifact restores that operation's
existing limit.

The existing writer keeps its 64 MiB bound. Candidate, public-output, ordinary
frozen-input and tool-stream bounds keep their current values. ISO callers can
select exactly 68,165,648 bytes, matching the existing producer allowance and
metadata formula. The API supplies capacity for checked private inputs rather
than changing the ISO producer's output policy.

Null transaction or bytes, zero capacity, capacity above the signed 32-bit host
range and size above capacity fail before replacing the prior input. Empty input
still requires a valid bytes pointer. A physical write failure requires closing
the transaction. Retained cleanup uses identity and metadata and can dispose of
a larger input without widening unrelated artifact reads.

## Evidence

All nine new methods pass with native and Cupid-built callers on Windows and
Linux. They cover the complete larger input, frozen real-author execution,
publication and timestamp reuse, legacy bounds, cleanup after rewrite, invalid
capacity and null arguments, empty input, snapshot rejection and private-output
capacity reset. Full observer and 528-input regressions and all eight injected
rename-boundary faults also pass. Independent evidence verifies 338 selected
methods, 328 executions and ten platform skips. Conditional-inventory and
unknown-token checks add four executions without skips. The two Cupid-built
caller objects are identical: 40,128 bytes, SHA-256
`469c1c782d4a1141ff28d9ea24c89e03032d905e60b2cc2b91d52944a03add88`.
Evidence is `iso-private-input-capacity-paired-independent.json` under
`cupid-native-iso-proof-20261005`. Installed parents and normal ISO ownership
remain unchanged.

The committed baseline rejects the larger request, and that native negative
receipt remains retained. The first Windows implementation still used the old
bound during retained handle transitions. The corrected adapter then exposed
native CRT sharing assumptions in the child and producer fixtures. Compatible
native reader sharing and the Cupid-built Windows producer exercise the actual
retained path; the native producer retains its ordinary-file byte comparison.
The first checked attempt also records a 180-second outer timeout and is
interrupted after its known failures. Its partial suite does not qualify the
final source. New large-request calls use a 600-second allowance; production
launch deadlines and existing observer test bounds remain unchanged.

The 161,772-byte capacity manual passes fresh installed-seed kernel/image and
strict private four-CPU max/e1000 runtime checks with completed ls and SMP
verification. Independent rereading checks 1,589 source controls, all 429 objects
and sixteen artifacts. Only its manual wrapper changes; the accepted FAT16
object and FAT data from sector 20,480 remain intact. The raw kernel measures
9,595,904 bytes, while final/pass-one ELF sizes remain 9,822,652 and 9,691,580
bytes. Only the raw-kernel policy row changes. The 200 MiB image has SHA-256
`89b1fe8694fd8fa99a53b5b29fea6977743237baed895a1de22285980552e25a`.
Evidence is `manual-independent-windows.json` under
`cupid-native-iso-private-bounds-manual-20261005`. This installed-seed manual
acceptance does not qualify or install the new source cohort.

## Consequences

The ISO publisher can carry the complete existing bundle contract through the
private transaction. It must still reserve all 528 frozen inputs, bind the
retained observer, validate selected paired seed authority and independently
check the authored image before publication. Normal ISO ownership remains
447 CupidBuild and five Python actions across 452 transforms. `TempleOS/` remains
read-only and excluded from these counts.
