# ADR 0285: Reject ambiguous raw CupidASM source controls

## Status

Accepted on 2026-08-14.

## Context

A raw CupidASM artifact has one flat address space. `ORG` selects its base,
but the parser accepted another `ORG` and silently replaced the first value.
The source location that established the image could therefore disagree with
the published origin and any address based on it.

Section layout is relative to each section. Raw emission writes one linear
buffer, so a statement in a second section could return to offset zero. That
path reached an internal error after parsing instead of explaining that the
raw profile cannot represent the source layout. ELF32 and fixed-image output
already have defined multi-section layouts and must keep them.

## Decision

A raw request accepts one source `ORG`. A second directive returns
`CTOOL_ERR_INPUT` with `CTOOL_ASM_DIAG_INVALID_ORIGIN` at that directive. The
request's initial origin remains a default that one source `ORG` may replace.

A raw source may claim one section identity. The first section-bound statement
claims the implicit `.text` section unless an explicit section directive
claims another section first. Repeating the same section is valid. Selecting a
different section returns `CTOOL_ERR_INPUT` with
`CTOOL_ASM_DIAG_INVALID_SECTION` at the new directive. Includes use the same
source context, so the rule also covers section and origin controls in included
files.

Failed assemblies publish a zeroed result and leave no output bytes. The hosted
command keeps its commit-gated file publication, so either diagnostic preserves
an existing destination. A defensive raw-emission check also publishes a
layout diagnostic if an internal statement offset ever disagrees with the
output buffer.

ELF32 and fixed-image requests keep their existing section behavior. Valid raw
source bytes, ranges, origin handling, and alignment behavior do not change.

## Evidence

The duplicate-origin CLI test first failed because the command returned success
and replaced the destination. After the parser check, it reports
`CT6000010` at line 4, column 1 and preserves the sentinel file.

The raw multi-section CLI test first failed with only
`cupidasm: assembly failed (internal)`. It now reports `CT6000011` at the
second section directive and preserves the sentinel file.

The native `raw-source-contracts` mode covers both failures, an early section
switch before emitted storage, a repeated valid section, same-job recovery,
and byte-identical repetition. All twelve native CupidASM contract modes pass.
The hosted CLI, active raw and ELF32 source, alignment, and kernel ELF tests
also pass. The first complete demo-corpus run stopped at
`gfx2d_fullscreen_enter` because its fixture lacked both fullscreen ownership
names. A separate fixture repair synchronized those names with
`kernel/lang/as.cc`; all 22 demos now pass deterministic fixed-image
assembly. That repair does not change the raw source-control decision.

## Rejected alternatives

Keeping the last `ORG` was rejected because the source would contain two
conflicting image bases without saying which one was accidental.

Flattening independent sections by parser order was rejected because it would
invent placement semantics that raw source does not currently define. ELF32
and fixed-image output already provide explicit multi-section layouts.

Rejecting every raw section directive was rejected because one named section
is unambiguous and supports clean source shared with other output profiles.

Changing active assembly source was rejected because the limitation belongs to
the raw profile, not to the operating-system source.

## Consequences

Raw source-control mistakes now fail at their source locations without
publishing an artifact. Existing valid raw, fixed-image, and ELF32 source keeps
the same behavior.

This validation adds no host dependency, build-graph owner, or ABI change.
Valid assembler output is unchanged. The updated CTXT manual changes the
embedded documentation payload when the integrated tree rebuilds it. No source
earns a `.c` to `.cc` rename from this work.

The checked execution seeds can compile the changed source but do not yet carry
these diagnostics in their hosted CupidASM images. The next fixed-point seed
promotion must include this source change.
