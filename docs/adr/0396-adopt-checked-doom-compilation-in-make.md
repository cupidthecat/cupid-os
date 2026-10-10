# ADR 0396: Adopt checked Doom compilation in Make

Date: 2026-09-20

## Decision

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

The selected host seed and all six images are prerequisites of every Doom
object rule, along with Makefile, the source, and the generated profile
manifest. The profile manifest retains its ordering barrier before source-tree
writers. The recipe fixes the source/output binding and admits no arbitrary
compiler flags. Python's wrapper remains a reference and parity tool.

The promoted readers accept adjacent parent generations `16a86f5b` and
`9d2529a7`, with digest/revision pairing and a shared Windows execution/plan
generation. The new pair records `9d2529a7` as its parent. ADR 0394 defines the
filtered discovery transaction; ADR 0395 defines staged behavior and the
parent window.

## Evidence

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

Both hosts pass the normal OS build, all 83 wrapper-oracle comparisons and
poisoned native compiler replays, sixteen artifact checks, and user-program
builds. Each replay preserves 448 artifacts and 554 controls, including their
timestamps. The three kernel outputs match across hosts. Private four-CPU
SMP/`ls` smokes pass and preserve the images. Windows passes an exact-command
retry after one unexplained command diagnostic; the bootstrap log retains
both results. Fixed-point convergence and runtime acceptance remain separate
checks, and full IWAD-backed Doom acceptance remains open.

The replay must execute all 83 compiler transactions with the old Python
coordinator and host code-producing tools poisoned, verify output bytes and
timestamps, and preserve all declared control inputs. Actual execution is the
evidence; dry-run predictions alone do not establish ownership. Report full
OS build, exact artifact checks, and private runtime results separately from
the fixed-point proofs.

## Remaining work

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
