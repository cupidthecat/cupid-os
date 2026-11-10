# ADR 0355: Add typed raw assembly to CupidBuild

## Status

Accepted on 2026-08-27.

## Context

CupidBuild's first production operation publishes guarded ELF32 relocatable
objects. The normal bootloader and SMP trampoline use the same checked tools
and publication discipline, but they also need a private raw range map and
artifact-specific layout rules. Their Python coordinator remains the owner of
those recipes, so transferring them safely requires a typed CupidBuild
interface before a seed or Make change.

## Decision

Source-head CupidBuild provides `assemble-bootloader` and
`assemble-smp-trampoline` beside `assemble-cupidasm-object`. All three commands
share the existing source freeze, six-tool seed validation, private candidate,
owner lock, pinned output parent, drift checks, rollback, and atomic
replacement path.

The raw commands ask checked CupidASM for a binary and a private
`cupid.raw-map.v2` file. CupidBuild never publishes the map. A bootloader
candidate must be exactly 2,560 bytes and carry a nonempty map. An SMP
candidate must be exactly 4,096 bytes, and its complete map must match the
active mixed-mode trampoline layout. Checked CupidDis then requires known
instructions, local targets, and source-resolved edges against the pinned map.
The map is checked again after inspection so a changed sidecar cannot approve
the candidate.

The fixed-point behavior gate runs the object operation and both raw
operations through consecutive CupidBuild generations. It compares each pair,
checks the two exact raw sizes, and proves that a malformed boot image leaves a
previous output intact.

Because this change grows CupidBuild, the promoted CupidBuild image is the one
expected seed-to-stage-two mismatch in a source-head reconstruction. The other
five seed images still match stage two, and stages three and four must converge
for all six tools. A later promotion will restore six initial matches.

This decision adds source-head capability only. The promoted v2 seeds predate
these commands, so the bootloader and SMP Make recipes remain on their guarded
Hostbuild entry points. Seed promotion and direct recipe ownership are a
separate change.

## Evidence

The hosted CupidBuild module passes 50 tests on native Windows, with two
expected Linux-only skips. Positive cases reproduce the active 2,560-byte
bootloader and 4,096-byte trampoline. Negative cases cover incomplete command
lines, wrong sizes, a wrong exact-size SMP layout, a raw direct target that
leaves the represented image, and live source or map drift during validation.
Every rejected case preserves the previous output and removes the private map.

CupidC also compiles and links the changed CupidBuild sources into a static
i386 executable. The fixed-point coordinator contract names all three guarded
operations and uses deterministic raw fixtures, so live OS source outside the
58-input closure cannot change a reported proof.

A clean paired replay passed the complete Linux and native Windows fixed
points in 3,661.449 seconds. On both hosts, CupidBuild was the only promoted
seed image that differed from stage two. Every stage-three and stage-four
object and all six final tool images matched. The normal OS build then passed
both CupidLD links, the full strict CupidDis scan, and all 16 exact artifact
checks. A private four-vCPU `max`/E1000 boot reached in-OS CupidC JIT
completion without a panic.

## Alternatives considered

A generic "run CupidASM" command would expose more authority than either
artifact needs and would leave their size and map policies with the caller.
Publishing the range map beside the binary would create a new public artifact
without a runtime consumer. Moving the Make recipes before seed promotion
would make a fresh checkout call commands that its checked seed does not have.

## Consequences

CupidBuild now has a narrow source-head interface for the two remaining raw
assembly publications. Their normal graph ownership and the 452-transform
participation counts do not change yet. No C source is renamed in this step;
all active CupidC translation units already use `.cc`.

TempleOS remains read-only reference material and is excluded from source and
progress counts.
