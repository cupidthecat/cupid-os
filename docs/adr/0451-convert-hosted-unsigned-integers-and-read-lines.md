# ADR 0451: Convert hosted unsigned integers and read lines

## Status

Implemented, tested and integrated into bootstrap source on 2026-10-08.
Both complete committed-source qualifications and independent checks pass.
Seed carriage and complete replacement-tool consumer acceptance pass through
the installed mixed width cohort recorded below.

## Context

The external frozen-input contract uses standard `strtoull` for a wide explicit
extent. The hosted library had neither a declaration nor an implementation.
The conversion fixture's ordinary line input also exposed missing `fgets`.
The existing unbuffered stream representation did not retain an EOF indicator.

## Decision

Declare and implement `strtoull` in the shared hosted library. Accept the C
locale's whitespace, optional signs, bases 2 through 36 and automatic octal,
decimal or hexadecimal selection. Preserve the first unconverted pointer,
including the original input when conversion is absent. Check magnitude before
each multiplication; consume all valid digits after overflow, return the full
unsigned maximum and set `ERANGE`. Unsigned negation follows successful magnitude
conversion. Invalid bases return zero, retain the original pointer and set
`EINVAL`. Successful and absent conversions preserve errno.

Implement `fgets` through the existing unbuffered `fread` boundary. Retain a
newline, terminate successful data and preserve the destination on immediate
EOF. Add an internal EOF field, `feof` and `clearerr`. `fread` sets EOF for regular
file exhaustion and Windows broken pipes. Successful standard and wide seeks
clear EOF; position queries preserve it. `clearerr` resets both indicators and
preserves errno. Nonpositive line capacities fail with `EINVAL`; capacity one
terminates the buffer without reading. Stream storage remains opaque to callers.

These represented standard contracts follow the relevant sections of
[WG14 N1570](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf),
7.22.1.4, 7.21.7.2 and 7.21.10. The capacity and invalid-base diagnostics above
are explicit runtime policies.

## Evidence

The 17 conversion and 15 line/state methods pass through native and Cupid-built
callers on both hosts, for 128 executions without skips. Independent checking
covers 5,856 conversion inputs, all 110 source controls and the 106-input private
producer inventory. It reconstructs complete generated native source, rehashes
all actual objects and callers, verifies static ELF/PE profiles, compares the
six parent tools per host and retains every original producer and case bound.
The private host-extension paths belong to that captured inventory; these
library callers compile the shared runtime and their ordinary contract sources.

The native Windows CRT returns the original pointer for incomplete `0x` inputs,
while the represented conversion consumes the valid leading zero. The fixture
retains both exact expectations and both actual outputs. Windows CRT `fread`
sets its stream error on a write-only read while preserving errno; its native
fixture checks that specific behavior. Actual Cupid callers still require a
nonzero changed error code. Initial CRLF output, missing line input and these
oracle differences remain recorded as failed runs.

The isolated integration copy retains the normal 99 producer paths. Both Cupid
callers pass all 32 methods again, and both byte-output regressions pass.
Independent checking binds 103 source controls, actual products and parents to
those executions and the identical bodies in the retained native contracts.
Exactly three producer paths and four test paths are applied to the bootstrap
worktree. Its source matches the paired integration preparations.

`docs/bootstrap/HOSTED-UNSIGNED-LINE-INPUT.md` records scope, commands, limits and
remaining qualification work. Installed seeds and normal recipe ownership retain
their separate acceptance requirements.

## Installed cohort acceptance, 2026-10-09

The qualified `acbbd834` pair now carries this capability in both installed
host tool sets. All four SDK profiles, complete public bootstrap methods and
both actual Make bootstraps pass independent checks. Current paired normal
image and user builds, all four strict private boots and complete object,
artifact, ABI and image comparisons pass with the revised manual.
[The installed cohort record](../bootstrap/QUALIFIED-MIXED-WIDE-SEEDS.md)
binds all 99 committed inputs, fifteen seed files and final adoption evidence.
This installs the compiler/library capability; native image command ownership,
private host follow-ups and retirement of the three Python coordinators
remain open.
