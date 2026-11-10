# ADR 0392: Capture every kernel-profile closure

Date: 2026-09-20

## Decision

The kernel compiler profile has 157 approved roots: 156 ordinary sources and
the generated kernel-symbol table. Each root has an explicit source and
header closure in both the Python reference wrapper and CupidBuild. CupidBuild
serializes these captured bytes into the closed source bundle from ADR 0390.
The Python reference wrapper retains its private directory representation.
Neither coordinator may use live source reads for an approved kernel root
that lacks a closure.

The tables follow the existing explicit Make prerequisites and the recursive
include graph. That comparison exposed six omitted dependencies: `pe32.h` in
the desktop rule, and `gfx2d_icons.h`, `as_elf.h`, `cupidasm.h`, `cupidld.h`, and
`pe32.h` in the shell rule. The rules now include those headers. No OS source
or include is removed to make capture work.

The largest closure contains 90 files, for the in-kernel CupidC entry point.
The assembler entry point contains 79. A named native capacity covers all
three closure arrays, and the transaction checks the count before indexing
them. Source capture and the seven seed inputs remain within the existing
512-input transaction limit. Bundle framing and the 64 MiB bundle limit are
unchanged.

The kernel compiler arguments, logical source identities, generated-symbol
timeout, object validation, output bindings, locks, and publication checks
remain unchanged. The 83 Doom compilations have separate profiles and remain
outside this command. Generated installation tables and user-program objects
also retain their separate paths.

## Evidence and adoption

Tests compare the complete native and Python tables with all approved kernel
roots and Make prerequisites. Recursive include checks guard against a header
that live compilation could read but a captured closure would omit. Object
parity covers every admitted source, including both large closures. Missing
late headers must preserve the destination's bytes and timestamp.

Source capability does not change production ownership. A committed source
checkpoint must pass the Linux and native Windows fixed-point proofs before
its tools become checked seeds. The recipe handoff then needs production
parity, the full OS build, artifact checks, and runtime smoke. The bootstrap
log records executed checks and any failed approaches.

All 157 roots already use `.cc`. TempleOS remains read-only reference material
and is excluded from the source and ownership counts.
