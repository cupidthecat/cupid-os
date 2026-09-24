# ADR 0372: Add typed kernel flattening to CupidBuild

## Status

Accepted on 2026-08-30.

## Context

Kernel flattening was one of four composite CupidObj paths still coordinated
by Python. The production transaction freezes 431 inputs, runs one broad
CupidDis known-instruction check, applies linked-image target and code-anchor
checks to both kernel ELFs, asks CupidObj for the flat image, and publishes
only after its safety boundaries still match.

CupidBuild already owned smaller guarded publications, but its transaction
input table held only sixteen entries. Its normal POSIX transactions also
closed over private paths rather than retained file descriptors. Neither
limit was suitable for the active kernel cohort.

## Decision

Add `cupidbuild flatten-kernel` with explicit seed manifest, repository root,
input manifest, and output arguments. The input manifest accepts at most 500
canonical relative paths. It rejects missing final newlines, CRLF, blank or
comment rows, whitespace, backslashes, absolute paths, traversal, dot
components, colons, and case-insensitive duplicates. Rejecting every colon
also prevents a manifest row from naming a Windows alternate data stream. Both
`kernel/kernel.elf.pass1` and `kernel/kernel.elf` must be present.

Grow transaction input storage on demand to a bounded 512 entries. Normal
POSIX transactions retain a no-follow descriptor for every frozen file, and
Windows retains the private working-directory handle. A private-directory
runner uses short frozen names so the full cohort fits the Windows command
line. It verifies the pinned directory and every frozen input before and after
each tool call.

Keep the production validation shape from ADR 0318. Run one 300-second broad
CupidDis `--require-known` request over the complete manifest. Then run one
600-second request with `--require-known`, `--require-local-targets`, and
`--require-code-anchors` over the two linked kernels. Run CupidObj `flat` with
a 300-second limit only after both checks pass.

Render the expected flat image independently inside CupidBuild. Prefer
nonempty file-backed `PT_LOAD` regions, ordered by physical address and file
order. If no load headers exist, use allocated `SHT_PROGBITS` sections. Omit
`NOBITS` and load-memory tails, zero-fill address gaps, and reject overlap,
overflow, unsupported allocated content, non-executables, empty initialized
content, and images beyond the 64 MiB transaction limit. CupidObj output must
match this rendering byte for byte before guarded publication.

Add Linux and native Windows fixed-point behavior cases for successful
flattening and malformed-manifest rollback. These checks become active when a
source-head CupidBuild generation is reconstructed; this decision does not
promote either checked seed or transfer the Make recipe.

The hosted i386 string surface now includes standard `strrchr`. CupidBuild
uses it to retain the short name of each frozen input without discarding the
absolute path used for descriptor-backed reads.

## Evidence

The typed CLI tests cover successful checked-tool parity, independent-renderer
ordering, malformed and unsafe manifest forms, the 500-input bound, the linked
kernel pair, an independently rejected span beyond 64 MiB, and rollback after
malformed ELF input. The focused cases pass on native Windows. The CupidBuild
host-runner and JPEG Make contracts also pass.

The native self-host contract compiled every tool source with CupidC and
linked all six static i386 tools. The generated runtime passed its allocator,
file, memory, and string checks, including `strrchr`.

Source-built CupidBuild then processed the real 431-entry production manifest
through the promoted Linux CupidDis and CupidObj images. The command completed
in about 453 seconds and produced a 9,513,536-byte private kernel image with
SHA-256
`3aac627568da71fe5478732c3b1adf8bf3c0cbf8678d63868a4f5982b5097773`,
which matches the tracked production image.

The regenerated source audit and its check-only replay pass with the expanded
Linux and native Windows behavior totals. A final four-vCPU QEMU smoke using
the `max` CPU model, E1000, and the strong SMP runtime check reached the GUI
terminal and completed `/bin/ls.cc` without a panic or exception marker.
After review hardening, the expanded 96-test CupidBuild module, six-tool
self-host link, 431-input source-built replay, and check-only audit also pass.
The first paired candidate attempt found and rejected a behavior fixture whose
linked paths did not use the required `kernel/` identities. The corrected
fixture binds those exact production paths; no candidate from the failed run
was published. A second attempt showed that the corrected placement no longer
created the separate behavior workspace as a side effect. The driver now
creates both roots directly, and the structural regression test requires that
setup before another paired proof can publish a seed.

## Rejected alternatives

Splitting the broad scan into batches of 24 was rejected. It changed the
single-invocation contract, reset the timeout for every batch, altered failure
aggregation, and raised the worst-case broad phase to roughly 90 minutes.

Passing 431 absolute frozen paths in one Windows command was rejected because
the active cohort can exceed the platform command-line limit. Running from the
pinned private directory keeps one invocation and bounded argument length.

Trusting CupidObj as the only flat-image renderer was rejected. The guarded
publisher needs an implementation-independent byte veto before replacement.

## Consequences

Source-head CupidBuild can express the complete kernel-flatten transaction and
can be compiled by CupidC. The fixed-point matrices now define 28 failure, six
help, and 35 success groups on Linux, and 17 failure, six help, and 22 success
groups on native Windows.

The active checked seeds do not yet carry this command, so production Make
still uses Hostbuild for kernel flattening. Seed promotion and the Make and
audit handoff remain separate green commits. Disk-image publication, ISO
publication, and Doom profile-manifest publication are unchanged. No GCC,
NASM, host linker, or host object utility is added. `TempleOS/` remains
read-only reference material.
