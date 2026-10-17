# ADR 0239: Author deterministic ISO fixtures with CupidObj

## Status

Accepted on 2026-08-05.

## Context

Checked CupidASM already authors the 4,096-byte spanning file used by the ISO
runtime test. Python still turns the complete fixture tree into ECMA-119 and
Rock Ridge bytes. That remaining writer is deterministic object work and
belongs in CupidObj. Filesystem traversal, input freezing, drift detection,
and atomic publication remain host concerns.

ADR 0191 defines the existing Python format and publication boundary. ADR
0227 gives CupidASM ownership of the spanning file, and ADR 0238 records the
current production split between CupidObj disk templates and Python image
coordination.

The tracked image has several exact compatibility choices. It uses both path
table byte orders, breadth-first directory numbering, uppercase 8.3 base
identifiers, fixed timestamps, `RRIP_1991A` metadata, one forward `CE`
continuation, and no `ST` terminators. Moving the writer must retain every
byte instead of replacing the format with a smaller fixture.

## Decision

Add the freestanding `CTOOL_OBJ_BUILD_ISO_FIXTURE` operation and the hosted
`cupidobj iso-fixture` command. The manifest is the operation's primary ASCII
input. A borrowed typed inventory supplies each logical directory and each
already loaded file. Logical names never depend on native paths, and the core
does not inspect a host filesystem.

The operation builds one bounded node table in arena storage. It validates
one-to-one manifest membership, portable relative paths, represented parent
directories, entry kinds, file sources, case-insensitive uniqueness, the
eight-level primary hierarchy, and the 512-entry request limit. It allocates
base identifiers in case-folded source-name order, assigns directory numbers
breadth first, and places file extents in case-folded path order. All extent,
block, and output calculations use checked 64-bit arithmetic before an i386
value is published.

Each directory materializes one sorted child-index slice and reuses it during
sizing, numbering, and emission. This keeps the accepted 512-entry boundary
quadratic instead of repeating a rank scan for every record.

The result includes the zeroed system area, primary and terminating volume
descriptors, both path tables, block-contained directory streams, the Rock
Ridge continuation, and contiguous file contents. Empty files keep extent
zero. Every failure preserves the caller's prior output, clears the result,
and rewinds temporary arena storage.

The hosted adapter accepts repeated `--directory LOGICAL` and
`--file LOGICAL NATIVE` entries. It loads native file bytes through the
ordinary invocation job while preserving the separate logical identity. A
missing native source names the failed path, and a semantic rejection does
not replace an existing output.

## Evidence

The freestanding selector builds a nested 25-block image and checks its
descriptor, path table, directory records, forward continuation, Rock Ridge
entry, and file extents. Reversed typed entries and CRLF manifest text produce
the same bytes. Negative cases cover malformed requests, bad paths, missing
or mistyped parents, duplicate and case-only names, manifest disagreement,
bad source views, depth and entry limits, i386 output overflow, constrained
arena and output storage, rollback, and same-job recovery.

All eleven CupidObj selectors pass under the strict native C11 build. All 31
hosted CupidObj tests pass, including exact comparison with the tracked
61,440-byte image. That image retains SHA-256
`40359c1cec72219f21e87ce71b31e621209036042440e1b38c5e59de157e0fb6`.
The hosted test also reverses every CLI entry and proves useful missing-parent,
missing-source, and entry-limit failures while an output sentinel survives.
It exercises all 512 accepted directory entries and checks colliding 8.3
identifiers against the Python writer in both input orders. The checked core
also inspects both-endian descriptor fields and both path tables.

The checked seed CupidC compiles the three changed Toolchain roots. The
resulting object sizes and SHA-256 values are 170,440 bytes and
`d149e0fbf10a8f7a45969df72d5da07b26da741f2e01c52b51b3655bd923327c`
for `cupidobj.cc`, 37,804 bytes and
`922e080c8e480581e271488573e2f0415dfda73edefd04444b62724759d6bc60`
for `cupidobj_main.cc`, and 129,496 bytes and
`22caab70a7fab9886bb02375b1485788b3822ef15ec0921e0b09d1315aa21fa5`
for the contract. The 95-test hosted frontend module passes after refreshing
the exact source-shape and active lexical locks.

A fresh checked-seed Toolchain build publishes the complete 20-artifact
cohort after 2,764.533 seconds. Stage two and stage three match across the
compiled objects, startup object, five tool images, and linked contracts; the
hosted runtime also passes. Its 18,232-byte manifest has SHA-256
`8cd0ea08454d9d672e6890e040fce85ba02b2c101c21599aa3933b0d89eee202`
and records 45 frozen inputs, source snapshot SHA-256
`bac03a6d2b36dff48983221aae209a6688b408232b5d5373b6c2128082228a66`,
and seed-manifest SHA-256
`019c77d53ddaf64a382962e1d9588a60046b75a7661f70beb0da7510945f35d0`.
Running the published Cupid-built contract's `iso-fixture` selector directly
passes in 0.698 seconds.

The first direct selector gate failed before dispatch because CupidObj did not
yet recognize `iso-fixture`. The implemented selector now passes. The normal
build graph still has 719 active inputs, 449 transforms, 255 feature records,
and 25 accounted unreachable files. Its ownership counts do not move in this
commit. The final audit source digest is
`b6a340db80dfb5d95eaf429b386aa8f5f6a359091e1f7b879ca38f72f7b6de02`.
The 2,558,331-byte JSON has SHA-256
`4a24cfe4755bfe61f1898f69333d95b2e7e89c23b4e33342e65875b35f2427de`,
and the 12,196-byte summary has SHA-256
`caa636e630cb9b55c9be633c31b45ad1385d2bde3d8cdba2d228eaae694e567f`.

## Rejected alternatives

Moving the whole fixture into CupidASM was rejected. Assembly is the right
owner for `big.bin`, but directory layout, path tables, extent allocation, and
Rock Ridge records are object-format work.

Letting the freestanding operation walk a native directory was rejected. It
would mix filesystem policy with the deterministic byte transform and would
make private frozen paths part of guest naming.

Replacing the Python production writer before seed promotion was rejected.
The checked CupidObj image does not recognize the new command yet, and the
production handoff still needs snapshot, parity, drift, and publication
guards.

## Consequences

Source-head CupidObj can reproduce the complete repository ISO fixture. The
checked seed and normal Make recipe remain unchanged until a full five-tool
promotion carries the command and a later handoff runs checked CupidObj first.
Python remains the independent format oracle and the owner of filesystem
safety and publication. No ordinary C or assembly source changes ownership,
so no `.c` to `.cc` rename is due. `TempleOS/` remains untouched reference
material.
