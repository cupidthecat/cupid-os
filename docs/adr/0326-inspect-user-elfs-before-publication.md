# ADR 0326: Inspect user ELFs before publication

## Status

Accepted on 2026-08-22.

## Context

The checked user build compiled each program with CupidC, linked it with
CupidLD, and validated the loader-facing ELF layout before publication. The
normal kernel transaction already used CupidDis to reject unknown code,
invalid local targets, and invalid static code anchors. User executables did
not receive that instruction-level check.

## Decision

On the checked Linux and Windows paths, run the selected seed's CupidDis on the
private linked candidate with `--require-known`, `--require-local-targets`, and
`--require-code-anchors`. CupidLD and CupidDis share the caller's frozen seed,
runner, and timeout. A tool failure, nonzero status, or unexpected output on
either stream stops publication and preserves the prior executable.

Capture the validated candidate's identity and bytes before inspection, then
require both to remain unchanged when CupidDis returns. Publication writes the
captured bytes to a separate private file and replaces the destination from
that file. A changed or replaced inspection candidate can never become the
published executable.

The optional native development oracle keeps its existing comparison path. It
does not define the normal checked publication boundary.

## Evidence

The production wrapper suite passes 61 tests. It covers Linux and Windows seed
selection, exact strict arguments, seed drift, timeouts, launch failures,
nonzero inspection, unexpected output on both streams, candidate replacement,
and preservation of an existing executable. The Windows `user:all` build and
the forced Linux checked Make path both build `hello`, `ls`, and `cat` through
the new gate.

The deterministic build audit records CupidDis on all three user links. Total
participation across the supported roots is now CupidC 250, CupidObj 192,
CupidASM 9, CupidLD 9, and CupidDis 9.

## Consequences

Every normally published example user ELF now passes the same three strict
static code policies as the linked kernel cohort. Python still coordinates the
transaction and atomic replacement. This does not add GCC, NASM, binutils, or
WSL to the normal user build, and it does not promote a new seed.

Minimal source-level debug information and Python-free orchestration remain
separate bootstrap work. `TempleOS/` remains read-only reference material.
