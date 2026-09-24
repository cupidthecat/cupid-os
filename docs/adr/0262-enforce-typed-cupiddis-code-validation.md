# ADR 0262: Enforce typed CupidDis code validation

## Status

Accepted on 2026-08-11.

ADR 0265 records checked-seed carriage. The decision and evidence below
describe the source capability before that promotion and remain unchanged.
The normal kernel path now applies the strict policy to its complete object
and linked-image cohort inside one frozen hostbuild transaction. Checked
CupidDis validates the private cohort, then checked CupidObj flattens its
frozen final ELF. Hostbuild rechecks live trust inputs and the `kernel.bin`
boundary before parent-relative atomic publication. Every failure preserves
the prior raw kernel. The later production evidence is recorded in ADR 0265.

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

Source-head CupidDis exposes a stable inspection result that a build gate can
consume without parsing rendered text. At this decision boundary, the checked
seed did not carry the option and the normal build did not invoke it. ADR 0265
later promoted the option and adopted it for the active object cohort and both
kernel ELFs. The current kernel publication freezes that cohort, runs checked
CupidDis, and then runs checked CupidObj flat in one transaction.

A final poisoned-host `make -j2 all` passed with exit 0 in 1,022.190 seconds.
The exact-size prerequisite accepted all nine artifacts before publishing the
209,715,200-byte image with SHA-256
`326844ca58c1f864a6b9a2480dfaeb5ed71ec3df22cdb46da17a6bb356e7e726`.
The final four-vCPU E1000 and RTL8139 frontiers passed from that image. Both
used the partitioned USB fixture, `--smp 4`, `--cpu max`, SMP and frontier
runtime verification, a private image, and a 300-second phase timeout. E1000
exited 0 in 725.058 seconds with 103,673 changed framebuffer pixels, 29,608,822
AC97 frames at peak 25,600, and 76,784 PC speaker frames at peak 30,710.
RTL8139 exited 0 in 725.406 seconds with 106,151 changed pixels, 29,601,879
AC97 frames at peak 25,600, and 76,719 PC speaker frames at peak 31,501. Both
used a 640 by 480 framebuffer, and the image hash remained unchanged.

No production output changes owner, no host dependency is removed, and no boot
artifact changes. No `.c` to `.cc` rename is due. `TempleOS/` remains read-only
reference material.
