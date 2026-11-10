# Name fixed-point sources consistently

- Status: Accepted
- Date: 2026-07-26
- Supersedes: The deferred five-source naming boundary in ADR 0124

## Context

ADR 0124 renamed every source owned only by production CupidC and left five
normal-build Toolchain roots with `.c` names. Those five roots also belong to
the checked i386 Linux fixed point. Fourteen other fixed-point and hosted
adapter roots still used `.c` for the same historical reason.

The extension could not describe CupidC ownership while those 19 checked
sources retained host C names. Renaming them safely required a complete build
plan update, explicit native compiler language selection, and proof that the
old checked seed could reproduce the new graph.

## Decision

Rename all 19 fixed-point C sources to `.cc`:

- `toolchain/ctool.cc` and `toolchain/ctool_host.cc`
- `toolchain/elf32.cc` and `toolchain/x86.cc`
- the implementation and command-adapter roots for CupidASM, CupidDis,
  CupidLD, and CupidObj
- `toolchain/cupidc_pp.cc`, `toolchain/cupidc_type.cc`,
  `toolchain/cupidc_frontend.cc`, `toolchain/cupidc_ir.cc`,
  `toolchain/cupidc_emit.cc`, and `toolchain/cupidc_main.cc`
- `toolchain/hosted/i386-linux/runtime.cc`

Keep the source language as C. Every native GCC or Clang compile rule for a
renamed root passes `-x c`. The build audit checks that selection and rejects
a missing language mode or `-x c++`.

Update the checked bootstrap plan to the new paths without replacing any seed
image in this change. Its canonical SHA-256 is
`59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc`.
The seed manifest still verifies the original five artifacts and their
recorded hashes.

Treat a root recursive Make rule as orchestration, not source delivery. The
supplemental Toolchain graph owns the underlying compile transforms. Count
both `.c` and `.cc` roots when validating the hosted i386 closure.

## Rejected alternatives

Leaving the fourteen hosted-only roots at `.c` was rejected because they are
part of the same CupidC-built fixed point and would preserve an avoidable
naming exception.

Letting GCC or Clang infer a language from `.cc` was rejected because both
would select C++. The Make recipes state `-x c`, and tests reject drift.

Refreshing the seed before the old seed reproduced the renamed graph was
rejected. A candidate seed must come from a proven transition, not replace
the compiler used to establish that transition.

Editing historical ADRs, log entries, or platform baselines to show the new
paths was rejected. Those records keep the graph and command lines they
actually measured.

## Evidence

`make verify-bootstrap-seed` verifies all five original i386 Linux seed
artifacts after the plan update.

`make bootstrap-from-seed
BOOTSTRAP_SEED_OUTPUT=build/bootstrap/strict-frontier-naming` completes under
Windows through WSL. The report covers 40 frozen source inputs, 19 C objects,
one startup object, five tool images, five help cases, ten successful behavior
cases, and six failure cases. Every stage-two object and tool image matches
stage three byte for byte. The source snapshot SHA-256 is
`c3aaf91d6133d0382e5ddb7b33cca665a7344fb7f38688c467db2d28a1a82aa4`.

The old seed already matches the new CupidASM, CupidDis, CupidLD, and CupidObj
images. CupidC differs because ADR 0125 adds decimal floating scalar support.
That expected difference is the candidate for the next seed refresh.

The regenerated active build audit reports 698 active sources, 253 feature
IDs, 500 transforms, and 42 accounted unreachable files. It records 151
CupidC transforms, 146 host C transforms, and 163 Python transforms.

## Consequences

All 144 checked-in sources in the normal CupidC cohort now use `.cc`. The
generated `kernel/cpu/ksyms_data.cc` source makes all 145 normal CupidC
transforms consistently named.

The fourteen renamed hosted and command-adapter roots do not change normal OS
ownership. They make the complete 19-source fixed-point plan consistent and
keep native C selection explicit.

The checked seed remains unchanged until its candidate images are promoted
with revision provenance and the full bootstrap is repeated from those
promoted artifacts.
