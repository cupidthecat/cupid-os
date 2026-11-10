# ADR 0350: Reuse CupidDis strict decode maps

## Status

Accepted on 2026-08-25.

## Context

Strict CupidDis inspection decoded large tool images and relocatable objects up
to three times. One pass counted decoded instructions and relocation ownership,
a second marked instruction starts, and a third classified local targets.
Whole-image certification made that repeated work visible in both fixed-point
lanes.

The instruction-start map is already the information needed by the target and
anchor policies. Rebuilding it in a separate pass added time without adding a
different proof.

## Decision

Let each summary decode populate the instruction-start map when strict policy
checks need one. Anchor-only checks use that single pass. Local-target checks
add one necessary validation pass because a forward target cannot be classified
until the complete start map exists.

Combine raw local-target and source-edge validation in the same second walk.
Reuse the same summary map for static ELF executables and PE32 sections. For
ELF32 relocatables, allocate and rewind one map per executable section.

Replace the relocatable ownership byte array with per-instruction claim state.
Relocations are sorted by section and site. Only fields in the current decoded
instruction can claim rows at that site, so at most `CTOOL_X86_MAX_FIELDS`
indices are live. Preserve total, matched, unmatched, duplicate, and
incompatible-field results.

Keep the public request, report, CLI output, diagnostics, and arena recovery
contracts unchanged.

## Evidence

Source-seam tests lock the summary-map reuse and pass counts for raw, `ET_REL`,
`ET_EXEC`, and PE32 input. Direct contracts compare exhaustive and indexed
decoders across raw, targets, object, executable, anchor, PE32, and error modes.

All direct hosted CupidDis modes pass under strict Clang warnings. The combined
CupidDis and x86 Python run passes 51 tests with one host-specific skip.
`git diff --check` also passes.

## Consequences

Strict inspection now performs one decode pass for summaries and anchors, or
two when local or source-edge target validation needs the completed map.
Relocatable inspection uses less peak scratch because it no longer allocates a
relocation-sized claim array.

The semantic evidence is unchanged, so this is not a seed promotion or an
ownership transfer. Fixed-point runtime measurements will be refreshed with
the next source-head cohort.
