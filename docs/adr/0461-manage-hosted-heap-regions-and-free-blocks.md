# ADR 0461: Manage hosted heap regions and free blocks

## Status

Integrated into the shared hosted runtime source on 2026-10-10 after the private
source-eight/source-nine implementation and guarded cohort-ten checks.
Reproducible Cupid-built density, mutation and OS-failure contracts accompany
the source. Named-commit producer qualification and seed promotion remain open.
The [prototype patch](../bootstrap/prototypes/hosted-heap.patch) is retained as
historical evidence.

## Context

ADR 0460 exposes an allocation failure in the Cupid-built Windows discovery
caller. Its unchanged 4,096-input request fails, while 3,607 missing inputs pass.
An allocator-only caller reproduces exhaustion near 32,500 live allocations,
even when each request is one byte. It uses the original runtime object and
checks alignment and stored data.

External process maps show that each small allocation creates a separate
4 KiB region at a 64 KiB aligned address. The same executable admits exactly
32,548 requests for both one-byte and 8 KiB payloads. Committed storage changes
from 134,545,408 to 401,178,624 bytes without changing that boundary. The failure
therefore follows address placement rather than the amount of payload storage.
The first map checker incorrectly assumes 64 KiB reservations. That failed
check and its correction remain retained.

Linux has a separate allocator cost. With 1,024 live one-byte allocations,
the original first-fit search inspects 523,776 physical blocks, exactly
`n * (n - 1) / 2`. It scans allocated blocks that cannot satisfy a request.
A complete discovery phase replay spends 52.3 of 59.6 seconds in construction.
Its marginal pass does not replace the original failed sixty-second selections.

## Decision

Windows obtains shared regions of at least 64 KiB through its existing
VirtualAlloc bridge. Each region owns a physical block list. Allocation splits
free blocks, and freeing coalesces adjacent blocks within that region. A fully
free region returns through VirtualFree. Separate regions never coalesce.
If region release fails, its free block remains available for reuse.

Both backends search size bins containing only free blocks. Physical neighbor
links remain separate from free-list links. Splitting and coalescing update
both structures. Linux retains its brk growth and tail-release behavior; a
failed tail release restores the free block to its bin.

The i386 block header is 32 bytes, retaining sixteen-byte payload alignment.
Alignment, header and address arithmetic reject overflow. calloc checks its
product. Failed realloc leaves the original data and allocation alive. Zero
requests, free(NULL), shrinking and adjacent growth keep the represented
runtime contract.

No discovery, descriptor, path, image-size or validation limit changes. The
allocator supplies storage and search behavior for the existing accepted work.

## Evidence

The identical Windows density fixture fails on the original runtime and admits
all 65,536 allocations on the changed runtime. One-byte and 64-byte cases also
free alternating blocks, reuse them through calloc, check neighboring data and
release the remaining blocks. Useful negatives cover size/product overflow,
actual allocation failure and preserved data after failed realloc. Both hosts
pass their complete original runtime contracts and strict runtime disassembly.

The isolated Linux work replay records zero free-block inspections for the
same uninterrupted live-allocation pattern at 128, 256, 512 and 1,024 requests.
Debug counters and phase markers exist only in clearly named debug copies.

An initial source-nine Windows selection times out in the nested case while
the native capacity selection runs alongside it. The isolated full replay
passes in 45.7 seconds, and a complete serial retry passes. These observations
do not establish a stable timing ratio or prove contention as the sole cause.

The initial recorder also omits the nine forbidden host-producer variables.
Guarded cohort ten rebuilds all four callers and repeats complete native and
Cupid selections, serially within each host. It also rebuilds and repeats every
ordinary/UNC observer selection. Checked runs enforce all nine sentinels.
Every original per-command deadline, two-worker checked build and Linux 32 MiB
runtime bound remains in force. Original failures remain held.

Paired independent review accepts 256 selected methods, 242 executions, fourteen
declared platform skips and 410 actual discovery calls. It rereads all 206
source/support controls, 109 private producer inputs, five complete object pairs
and 128 direct identity comparisons. All 100 legacy observer executions and
106 calls also pass. Every checked object other than the runtime retains its
source-seven bytes on each host. Complete native and checked results agree
within each host. The 99 normal inputs and fifteen installed seeds stay unchanged.

Evidence and the remaining qualification work are described in the
[hosted heap record](../bootstrap/HOSTED-HEAP.md). The normal 99-input producer
closure, installed seeds and held manual remain unchanged. TempleOS stays
read-only and excluded.

## Consequences

This change strengthens the hosted runtime used by Cupid-built tools.
It does not retire a coordinator or integrate the new image CLI. The patch uses
repository LF line endings; private receipts retain their original bytes.
Seed adoption requires qualification from a named source revision, complete
consumer acceptance and the relevant OS/runtime checks. Source integration shares
the bin implementation between both backends and adds compile-time header layout
checks. The normal test suite now builds both positive and useful failure
fixtures through the verified Cupid toolchain on the current host.
