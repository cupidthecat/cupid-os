# Doom compiler coordinator: completed handoff

Audit date: 2026-09-20.

Make invokes checked `cupidbuild compile-doom` for all 83 approved Doom
sources: three compatibility roots and 80 Doom-tree roots. It derives the
profile from source membership, captures the exact source cohort and all
profile headers, and sends the selected source and headers to CupidC in a
closed `CUPSRC1` bundle. Missing entries cannot fall back to live files.
Filtered directory checks allow parallel object publication while rejecting
source/header changes and replaced directories. The strict profile-manifest
publisher keeps its existing directory checks. Failed transactions preserve
or retain verified recovery evidence under the documented filesystem policy;
equal validated objects retain their timestamps after full input checks.

The 157 kernel-profile recipes continue to use `compile-kernel`. All 240
kernel/Doom compiler roots already use `.cc`; this handoff changes ownership
without changing the source language, compiler arguments, or OS behavior.

Both checked six-tool cohorts come from source `83d00ce70e5607dc5c011bb97c6478121f24a21c`
and the same 59-input snapshot `f2b3a1349b3cf5476fc2f141b307afe3babe0e673d6154fb98afee3511507718`. Clean proofs match
all 29 Linux and 32 native Windows stage-three/stage-four artifact pairs.
Linux passes 41 failure, seven help, and 47 success groups; Windows passes
29 failure, seven help, and 34 success groups. Each compared coordinator uses
its own generation's complete tool cohort. Independent promotion checks bind
the source inventory to the commit and verify exact stage files and bytes,
executable formats, build plans, parent lineage, and the proposed seed pair.

The Linux manifest has SHA-256 `a11c8af08eb1170d040dc6b361c30df321c088fcb4ae5becd6c2864995380622`; the Windows
manifest has SHA-256 `f5124cbddbeb55a61ce2f8ae93923daae512d6fec6732a532b1e8f0d15bed590`.

The audit records 437 CupidBuild and 15 Python participations across 452
transforms. Python still coordinates six compilations (three generated
installation tables and three user programs), three user links, two image
publications, three verification operations (artifact sizes, user syscall ABI,
and the Toolchain manifest), and one Toolchain build/manifest publication.
Make and host operating-system services remain required. GCC, NASM, and host
linkers are not required by the normal code-producing path.

The next compiler step is the six generated-install and user compilations.
Their existing profiles and captured headers must remain intact. User builds
must keep configurable `BUILD` directories, including safe creation of missing
parents, rather than being restricted to `user/build`. The three user links
remain a separate transaction. IWAD-backed Doom gameplay acceptance is still
open; compiler parity and asset-free smoke tests do not establish gameplay.
`TempleOS/` remains read-only reference material and is excluded from builds
and progress counts.

[ADR 0396](../adr/0396-adopt-checked-doom-compilation-in-make.md) records the promotion and recipe handoff.

The bootstrap log records the production replay, artifact-size checks, and private SMP/ls smoke separately from these fixed-point results.
Fixed-point convergence and OS runtime acceptance remain separate results.

## Original pre-handoff audit

The following design audit records the boundary before native adoption. Its
future-tense requirements and initial measurements are retained as history;
the current ownership and production evidence are recorded above.

The source implementation now follows this plan through `compile-doom`.
ADR 0394 records the transaction and its separate directory policy. The
requirements below remain the review and promotion checklist. Production
ownership stays with the existing Python wrapper until paired fixed-point
proofs, seed promotion, and the Make handoff pass.

The remaining Doom compiler work is 83 sources: three compatibility sources
and 80 Doom-tree sources. CupidC already compiles them. Python owns their
profile discovery, capture, checked execution, validation, and publication.
A native transaction can reuse closed source bundles and most of CupidBuild's
existing profile capture. It first needs a discovery policy that permits
object publication inside the directories being scanned.

## Source and compiler contract

The two approved source lists in `tools/cupidc_kernel_compile.py` are disjoint.
Recursive discovery beneath `kernel/doom` must match their exact union.
Missing or extra `.c` and `.cc` files fail, including legacy `.c` entries.
Matching links, junctions, and non-files also fail. Both profiles capture
every `.h` and `.inc` beneath their include roots, together with the selected
source, and recheck header contents and source membership after compilation.

Both profiles retain the 180-second compiler deadline. The compatibility
profile adds `--doom-compat`, omits `DEBUG=1`, and includes
`/kernel/doom/src` and `/kernel/doom/src/include_stubs`. The tree profile also
defines `DEFAULT_SAVEGAMEDIR="/home/doom/"` and `DOOM_PORT_CUPIDOS=1`, and forces
`/kernel/doom/dglibc_compat.h`. Their argument vectors contain 55 and 61
entries. The full compiler calls fit the existing 80-slot argument array.

A typed `compile-doom` command can derive the profile from source membership.
It should use the corresponding `.o` output binding already enforced by
`compile-kernel`, without accepting caller-supplied compiler flags.

## Measured capture size

| Measure | Initial audit snapshot |
| --- | ---: |
| Shared header and include files | 304 |
| Header content bytes | 926,471 |
| Bundle records per compilation | 305 |
| Largest compatibility bundle, `dglibc.cc` | 1,003,989 bytes |
| Largest tree bundle, `src/info.cc` | 1,076,384 bytes |
| Longest logical header path | 63 bytes |
| Inputs with selected source, headers, and seed | 312 |
| Inputs with all 83 sources, headers, and seed | 394 |

The existing 512-record, 64 MiB bundle and 512-input transaction limits cover
this cohort. The kernel's 90-entry arrays do not. Use bounded allocated
capture records for Doom's discovered inputs. Reuse the native profile root,
suffix, and source policies, then serialize the selected source and captured
headers in logical-path order.

## Directory publication boundary

The profile-manifest publisher retains complete directory snapshots, including
modification and change times. Publication checks repeat those comparisons
after installation. Its output lives under `build/bootstrap`, outside the
discovered source roots. ADR 0383 orders that publisher before writers in the
source tree.

Doom objects live inside the scanned tree. Their own installation changes a
directory snapshot. Parallel kernel or toolchain object publications also
change scanned directories. Reusing the strict manifest discovery checks
would therefore reject valid object builds.

Add a separate discovery policy for compiler transactions. Retain directory
handles and require the named and retained identities to agree. Reject
replacement, links, junctions, and changes to relevant directory membership.
Rediscover the filtered header and source sets through those pinned
directories, require exact path membership, and compare every captured input's
snapshot and contents. Repeat these checks before publication, after
installation, and before accepting unchanged output.

Unrelated `.o` namespace changes may pass this policy. Source and header
changes must still fail. Do not reset retained timestamps to hide changes or
weaken the profile-manifest publisher's existing checks.

## Required evidence

- Compare all 83 objects with ordinary compilation, including the forced
  include, Doom definitions, and absence of `DEBUG`.
- Exercise concurrent distinct outputs in the same Doom directory and
  concurrent kernel/toolchain object writes. Same-output writers remain locked.
- Reject added or removed headers, extra or missing Doom sources, legacy `.c`
  files, links, junctions, and replaced directories.
- Mutate inputs before launch and after installation. Require output
  preservation or the existing documented recovery result.
- Reject missing bundled includes without live-file fallback and reject
  capture-count or byte overflow before launching the compiler.
- Preserve an unchanged object's timestamp while still checking membership.
- Keep the strict profile-manifest race tests passing, then carry the new
  transaction through both staged proofs before promoting seeds and recipes.

This audit changes no source or production owner. Doom gameplay acceptance
remains separate from compiler parity and the asset-free runtime smoke.
