# ADR 0339: Start CupidBuild with guarded object publication

## Status

Accepted on 2026-08-24.

## Context

Python still coordinates every normal-build transform. The smallest useful
place to start removing that dependency is one of the two CupidASM `ET_REL`
object lanes. That transaction already has a clear contract: freeze the
source and checked seed, assemble a private candidate, inspect it strictly,
and preserve the previous object unless every boundary remains unchanged.

Moving only the command invocation would leave Python in charge of the real
work. CupidBuild therefore needs its own hosted process, filesystem, identity,
locking, and publication boundary before a Make recipe can change owners.

## Decision

CupidBuild begins as a CupidC-buildable hosted command with one operation:
`assemble-cupidasm-object`. Its policy module selects the manifest-bound
CupidASM and CupidDis images and requires an i386 `ET_REL` object with
executable `PROGBITS`. CupidDis then applies strict decoding and relocatable
local-target validation.

An opaque hosted transaction owns the platform details. It rejects symbolic
links, junctions, hardlink aliases, unsafe relative paths, and output-parent
changes. It serializes publishers with an adjacent owner lock, reclaims the
lock only when its recorded process is gone, and records the identity and
bytes of the file created during acquisition. Publication rejects a replaced
lock observed at either final lock boundary. Cleanup first quarantines the
current path, then deletes it only when the quarantined identity and bytes
match the acquired snapshot. A successor is restored instead. Linux uses a
private hard link so identity remains stable across Windows-backed WSL mounts;
Windows uses a same-volume move. The transaction skips occupied private roots,
freezes the source and complete checked tool cohort, and checks every live
identity and digest after the tools run. The candidate is captured before
inspection and checked again before publication. Linux publishes through
pinned directory descriptors and `renameat`. Windows holds the output-parent
handle and uses `NtSetInformationFile` for a parent-relative replacement.
Failures remove private files and leave the previous destination alone.

The Windows process and filesystem imports live in a dedicated
`cupidbuild_start.asm`. The existing tools do not inherit them. Linux adds the
zero- and four-argument syscall bridges needed by process discovery and
parent-relative publication.

This decision adds the source-head tool without changing the checked seed or
the normal build. The five-tool manifests and both Python-owned object recipes
stay in place until a later commit proves and promotes complete six-tool Linux
and Windows fixed points.

## Evidence

The public CLI tests cover success, malformed assembly, digest failure,
assembler and inspector failures, unexpected successful tool output, unknown
opcodes, nonlocal direct targets, source, manifest, and tool drift, destination
and parent drift, links, a hardlink alias, occupied candidates, live, stale,
and malformed locks, replacement rollback and recovery, cleanup, help, and
usage errors. A concurrent replacement case also proves that CupidBuild
preserves both the previous object and the successor's owner file.

The initial five-tool producer-seed proof built standalone CupidBuild ELF32 and
PE32 images. Both images ran their public help command and published the real
ISR source as an inspected relocatable object. After the lock-ownership
follow-up, the checked Linux object contract and native Windows and WSL
behavior cases pass. A fresh checked PE/ELF execution pair remains future
evidence, so the bootstrap log keeps the initial hashes separate from the
current follow-up results.

## Consequences

CupidBuild now owns a real guarded transaction at source head, but it does not
own a production transform yet. Python remains the normal-build publisher and
the parity oracle. The checked manifests still contain five tools, so there is
no six-tool fixed-point claim in this decision.

The command pins the required artifact records, bytes, identities, sizes, and
digests, but its small hosted manifest reader does not yet reproduce the
bootstrap verifier's full schema, provenance, target, and build-plan checks.
Normal builds must continue to enter the Python verifier until that trust
boundary moves into Cupid tooling and the six-tool seeds are promoted.

The object lane does not select `--require-code-anchors`. That rule describes
static linked `ET_EXEC` images; relocatable objects use executable-section
structure, strict decode, relocation ownership, and local-target checks.

Lock checks and object replacement are separate filesystem operations. The
protocol prevents overlap between cooperating CupidBuild publishers, which
honor a live owner file, and detects replacement at its named boundaries. It
does not claim atomic exclusion against an arbitrary process that mutates the
lock between the final check and object rename.

`TempleOS/` remains untouched reference material.
