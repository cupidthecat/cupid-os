# ADR 0397: Capture generated installation sources in CupidBuild

Date: 2026-09-20

## Decision

Source-head CupidBuild provides `compile-production` for the three generated
installation tables: `kernel/util/bin_programs_gen.cc`,
`kernel/util/demos_programs_gen.cc`, and `kernel/util/docs_programs_gen.cc`.
CupidObj continues to generate those sources. The new command coordinates
their compilation without changing their contents or the installed programs,
manuals, assets, or demos.

Every source captures the same five headers: `drivers/serial.h`,
`kernel/core/types.h`, `kernel/fs/homefs.h`, `kernel/fs/ramfs.h`, and
`kernel/fs/vfs.h`. Its closed `CUPSRC1` bundle therefore contains six records.
The complete closure remains required even when a source does not directly
include every header. Missing bundle entries cannot read live files.

The command retains the existing fixed kernel compiler profile and 180-second
deadline. It accepts only the approved source and its adjacent `.o` output;
caller-supplied compiler flags and unknown sources fail. User example sources
are outside this first production cohort.

The shared compiler transaction captures the complete six-tool seed, runs
checked CupidC, validates the relocatable object, rechecks its captured
inputs and publication boundary, and publishes through the existing guarded
output path. Equal validated objects keep their timestamps. Failed compilation
or input drift preserves the previous output or retains the documented
recovery evidence when the filesystem cannot safely restore its public name.
Directory locks, cleanup, and the native Linux/DrvFS recovery distinction
remain unchanged.

## Parent compatibility prerequisite

The source step also advances the adjacent v2 parent window in CupidBuild's
native reader and the artifact-size contract. They admit paired `9d2529a7`
and `83d00ce7` parent identities, reject retired `16a86f5b` v2 parents, and
retain the historical v1 parser. Digest/revision pairs must agree; Windows
execution and plan parents must belong to the same accepted generation.
Tests cover both accepted pairs, retired execution and plan roles, paired
retirement, mixed generations, and historical v1 parsing.

This is needed for future proofs seeded by the promoted `83d00ce7` pair and
is separate from generated compilation. The currently checked executables
still carry their preceding `16a86f5b`/`9d2529a7` reader window. Their hashes
and production ownership are unchanged until a later verified promotion.

## Validation boundary

Focused tests compare all three real objects with direct checked CupidC and
the Python reference wrapper. They exercise complete closure requirements,
profile definitions, nested includes and logical filenames, missing source,
invalid C, live-only includes, unsupported inputs and flags, unchanged output,
seed/source/header drift, concurrent distinct outputs, live locks, aliases,
and linked headers. Checked CupidC and CupidASM/CupidLD also build a coordinator
that runs all three real sources. Both host runs passed; the bootstrap log
records their test counts, matching kernels, artifact checks, and runtime smoke.

A shared staged helper covers all three generated-source identities, unchanged
publication, five failures with output-byte and timestamp preservation,
transaction cleanup, and recovery of each object. The unknown-source case
uses its own matching output name, so output-binding rejection cannot mask
missing source approval. Each compared generation receives its own complete
six-tool cohort. The resulting expected full-proof inventories are 46 failure,
seven help, and 54 success groups on Linux, and 34 failure, seven help, and
41 success groups on native Windows. These are expected inventories; helper
and mock runs do not establish a new fixed point.

## Ownership and remaining work

This is a source capability. Production recipes and checked seeds retain
the preceding proven Doom checkpoint: 437 CupidBuild and 15 Python
participations across 452 transforms. The three generated installation
compilations remain Python-coordinated until this command passes clean paired
proofs, seed promotion, all-source parity, the production recipe handoff,
the normal OS build, artifact checks, and runtime smoke. This source step
claims none of those future results.

The three user compilations require a separate output-parent contract. Keep
their freestanding profile, approved source/output names, and configurable
`BUILD` paths. Safe lexical aliases, nested or missing parents, and existing
pathname support must not disappear during native migration. The proposed
retained-directory model leaves prepared directories in place, establishes
identity after a safe open, and rechecks the full chain through publication.
Native path normalization and Windows pathname support still need implementation
and validation. The generated cohort can proceed independently.

All three generated roots already use `.cc`. No language feature, source
simplification, or host C/ASM dependency is introduced. `TempleOS/` remains
read-only reference material and is excluded from the build and ownership
counts. IWAD-backed Doom gameplay acceptance remains separate and open.
