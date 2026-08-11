# ADR 0262: Enforce typed CupidDis code validation

## Status

Accepted on 2026-08-11.

## Context

CupidDis could render every active i386 object without losing instruction
boundaries, but that result was an audit observation rather than a reusable
contract. A caller could search rendered `db` rows, but the text did not say
whether a row came from declared data, an unknown opcode, an invalid encoding,
or a truncated instruction. Text scraping would also couple a build check to
presentation details instead of the shared decoder result.

The normal build needs one strict check for many objects. That check must keep
examining later inputs after a missing or malformed file, name every failing
path, and avoid publishing a partial disassembly listing.

## Decision

Add a typed decode summary to `ctool_dis_report_t`. It counts known, unknown,
invalid, and truncated instructions with 64-bit fields. Counts cover only
regions selected as code. Declared raw data ranges, non-executable ELF
sections, and non-executable load segments do not enter the decoder and do not
affect the summary.

The summary follows the same region selection as the disassembly view. Report
preparation remains transactional: a decoder or allocation failure rewinds the
job arena and clears the report. The existing renderer and relocation overlay
remain unchanged.

Expose the policy through:

```text
cupiddis --require-known FILE [FILE...]
```

Raw input retains its explicit mode, range, and base options. Strict mode adds
the disassembly view internally, validates every input, and writes nothing to
standard output. It returns success only when every selected code region has
zero unknown, invalid, and truncated instructions. A decode failure prints the
path and all four counts. A load or inspection failure also names its path, and
later inputs are still checked. Ordinary rendering continues to accept exactly
one input and keeps its previous output.

## Test evidence

The raw contract distinguishes three recovery cases. `0F FF C0` counts one
unknown instruction and then the following `inc eax`. `66 66 90` counts one
invalid instruction and then the following `nop`. A terminal `0F` counts one
truncated instruction. A mapped data range containing the same bytes contributes
no failure.

ELF coverage includes one executable load segment and one relocation-bearing
relocatable object. Strict validation does not disturb the ordinary relocation
render. The active CupidASM outputs for `kernel/cpu/isr.asm` and
`kernel/core/context_switch.asm` both pass with exact instruction boundaries,
and ordinary rendering still names every relocation target.

Multi-input tests cover two clean files, clean and failing files together, and
a missing file between a clean input and a later decode failure. Strict mode
keeps standard output empty in each case and reports every failing path.
Ordinary multi-input use is rejected.

The focused CupidDis and active CupidASM source modules pass 21 tests in 9.734
seconds. One optional platform oracle is skipped because it is unavailable.
`git diff --check` also passes.

## Rejected alternatives

Searching rendered `db` text was rejected because it cannot distinguish data
from decoder fallback and would make formatting part of the policy interface.

Rendering complete output before checking it was rejected because a failed
strict validation must not leak a partial listing.

Stopping at the first bad input was rejected because one build invocation must
report the whole failing cohort.

Counting every byte-bearing ELF region was rejected because debug data,
constants, and other non-executable content are not instruction streams.

## Consequences

Source-head CupidDis now exposes a stable inspection result that a build gate
can consume without parsing rendered text. The current checked seed does not
carry this option yet, so the normal build does not invoke it in this decision.
A later seed promotion can validate the active object cohort and final kernel
with one checked command.

No production output changes owner, no host dependency is removed, and no boot
artifact changes. No `.c` to `.cc` rename is due. `TempleOS/` remains read-only
reference material.
