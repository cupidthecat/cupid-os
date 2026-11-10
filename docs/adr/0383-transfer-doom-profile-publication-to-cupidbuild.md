# ADR 0383: Transfer Doom profile publication to CupidBuild

Date: 2026-09-19

Status: Accepted

The clean repaired pair recorded in ADR 0382 carries this transaction. Its
Windows and Linux production regressions pass, as do final-manual parallel
OS replays and the private four-CPU runtime frontiers on the Linux image
with E1000 and RTL8139. Earlier profile fixtures passed with the `962e476b`
pair, whose full Windows replay later stopped at the kernel-flatten
command-line limit. The Windows image with preserved FAT contents separately
passes an SMP and `ls` smoke.

## Context

The paired seeds from revision `16a86f5b1693e017c36c6d902df9946c5d674b17`
carry `cupidbuild generate-profile-manifest`. Their clean candidate proofs
matched every final object and tool image and passed 33/7/38
failure/help/success groups on Linux and 21/7/25 on native Windows. ADR 0382
records that promotion. The normal Make rule still used the Python publisher
for the same closed Doom input manifest.

## Decision

Make calls the promoted platform CupidBuild directly for
`build/bootstrap/doom-cupidc-inputs.json`. The rule depends on `FORCE`, the
profile headers, Makefile, the selected seed manifest, and all six tools named
by that manifest. `FORCE` keeps source and header membership checks active on
every build. CupidBuild preserves the existing bytes and timestamp when the
manifest is unchanged.

The profile check must finish before normal Make recipes write beneath
`kernel/`, `drivers/`, or `toolchain/`. Its directory snapshots deliberately
detect namespace changes, including new object files. An order-only dependency
gates the matching `BOOTSTRAP_ARTIFACTS` outputs and all three generated
installation sources. The latter need their own gates because Make can build
an object's prerequisites before waiting on that object's profile dependency.
The rule follows the complete artifact-list definition and covers all 254
current writers in those roots. Unrelated roots can build in parallel. A new
profile timestamp alone does not rebuild the 171 order-only consumers; the
83 Doom consumers retain their ordinary content dependencies and rebuild when
the manifest changes. Other Make processes and external source writers remain
subject to drift rejection.

Make uses an `override` assignment for the fixed output path. Its existing
seed-derived directory, platform suffix, and six-image closure also remain
fixed against command-line replacement. `PRODUCTION_SEED_MANIFEST` can select
a complete checked cohort; individual tools cannot be redirected separately.

POSIX Make rejects a pre-existing symbolic link at either `build` or
`build/bootstrap` before it runs `mkdir -p build/bootstrap`. CupidBuild then
pins the existing parent chain under ADR 0377's transaction contract and does
not claim those directories for rollback. Windows keeps its native
parent-relative creation and identity-checked rollback. The mkdir command is
part of the manifest recipe, not a separate output transform. These shell
checks reject existing links; they do not eliminate the documented concurrent
namespace race between inspection and directory creation.

The transaction discovers the exact 83-source Doom cohort and current 304
header or include inputs, freezes them with the seed, builds `CUPROF1`, and
runs CupidObj first. An independent native JSON renderer must agree exactly
before publication. Membership, byte, seed, lock, candidate, output, and parent
checks remain inside CupidBuild. The documented POSIX namespace limits are
unchanged.

The build audit identifies the typed recipe as one CupidBuild and CupidObj
transform. It requires the exact output, operation, recipe, and distinct input
closure, including the full selected seed. It rejects missing or duplicate
inputs, changed output paths, altered publisher options, redirectable output
bindings, and missing or incorrectly conditioned POSIX parent preparation.
The Python publisher remains available as an independent oracle.

The audit keeps order-only dependencies in graph reachability and records
them as `order_only_inputs`, separate from content `inputs`. This also keeps
the existing user ABI gate reachable without treating its phony target as
compiler input. The evaluated production graph must give every source-root
writer exactly one profile dependency. Existing Doom compilation dependencies
remain ordinary content inputs; the other writers use scheduling edges.

## Evidence

The real native Windows Make fixture passed against the initial candidate
seed before the final repaired promotion. The
published JSON matched the Python oracle byte for byte. A second Make run
with Python unavailable retained a deliberately fixed timestamp. Renaming an
approved Doom source then stopped the object build with an approved-cohort
diagnostic and preserved the old manifest bytes and timestamp.

The Windows and Linux dry-run cases injected invalid Python, CupidObj,
checked-runner, seed-directory, suffix, closure, and output variables. Both
retained the promoted typed command and fixed output. Only the POSIX rule
prepared the parents. These three focused cases passed in 32.408 seconds.

The first POSIX recipe used plain `mkdir -p`, which could follow an existing
`build` link and create `bootstrap` outside the checkout before CupidBuild
rejected it. A real Make regression failed against that recipe. With both
preflight checks present, links at either parent stop before the tool runs and
leave the outside directory's contents and timestamp unchanged. Audit
mutations reject removal of either guard. The Linux fixture also exposed
`shutil.copyfile` dropping executable mode from the copied seed; `copy2`
retains that mode. The symlink, real publication, and audit mutation cases
passed together through WSL in 23.375 seconds. The complete native Windows
Doom production suite then passed 43 tests in 81.426 seconds, with two
expected POSIX skips. The bootstrap log records the complete build, audit,
and runtime results, including failed runs while the source census changed.

The first full parallel build failed the profile membership check while other
recipes were writing into the scanned directories. The hosted publisher, a
repaired Cupid-built publisher, and the original native candidate all passed
on the same quiet checkout. The ordering fixture runs eight Make jobs,
requires unrelated work to proceed while the profile waits, and checks
ordinary objects, generated sources, linked outputs, and the trampoline.
A second invocation refreshes the profile without changing output timestamps.
The audit rejects missing root filters, omitted installation sources, an
ordinary dependency in place of the order-only barrier, and a barrier placed
before the complete artifact cohort. Per-output mutations check every writer.

Six focused Windows cases passed in 2.999 seconds after the dependency check
was separated from filesystem discovery. Three WSL cases passed in 15.569
seconds: parallel ordering, linked-parent rejection, and real profile
publication with unchanged timestamps and source-cohort rejection. Audit
generation and deterministic check mode both passed. The resulting graph has
171 order-only profile edges and the 83 existing ordinary Doom profile inputs;
source and owner counts are unchanged.

Before the repaired pair was installed, the combined native Windows audit and
Doom production suite passed all 162 tests in 1,372.813 seconds, with two
expected skips for POSIX-only cases. The
separate three-case WSL run covers those platform-specific checks.

After the `962e476b` candidate pair was installed, the real profile fixture passed again
on native Windows in 47.713 seconds and through WSL in 23.758 seconds. Both
runs used private roots and verified native JSON parity, unchanged timestamps
with Python unavailable, and renamed-source rejection with the old manifest
preserved.

The final `16a86f5b` pair passed the corrected 167-case production suite in
1,827.789 seconds with two platform skips. The isolated Linux archive passed
48 handoff tests in 114.080 seconds with one Windows-junction skip. Linux
executed both Windows omissions, including profile-parent symlink rejection,
and checked positive parallel ordering. ADR 0382 and the bootstrap log keep
these results separate from the later passing parallel OS replays and
private four-CPU runtime frontiers with E1000 and RTL8139.

## Consequences

The supported graph keeps 452 transforms, with 443 under root `all`.
CupidBuild participation rises from 195 to 196 and Python participation falls
from 257 to 256. CupidObj remains at 192 because the author and format are
unchanged. Disk and ISO publication are the two remaining Python-coordinated
CupidObj transactions. Production CupidC wrappers, contracts, fixed points,
and broader build coordination remain separate work.

No language, ABI, active source cohort, or source suffix changes. All active
CupidC roots already use `.cc`; this handoff adds no safe rename. `TempleOS/`
remains reference material.
