# ADR 0374: Promote the kernel-flattening CupidBuild seeds

- Status: Accepted
- Date: 2026-08-30

## Context

ADR 0372 added the complete `cupidbuild flatten-kernel` transaction at source
head. The active Linux and Windows seeds predated that command, so Make could
not use it without stepping outside the checked bootstrap boundary.

The same source change added hosted `strrchr`. That runtime object is linked
into every tool, which means the refresh changes the complete six-tool cohort
even though CupidBuild is the only tool with a new command.

## Decision

Promote the paired stage-four Linux ELF and native Windows PE cohorts produced
from commit `0232cb57aad5d6bdfd7bd77499762514b2f0ebfd`. Both manifests bind the
same 59-file source snapshot:

`0b591a0bef928186641b3aa1fb98c1e145e6c4905c8b6cb87c34a1ace4bc87d2`

The Linux build-plan identity remains:

`52dd857bcb74e079e7e2eec45eaa90a0a0838ad2f4e817bebc35c9904efbecbd`

The native Windows build-plan identity remains:

`f9dce66230a693de9d9d0e60127a4a6c44ea465989f381c995086bfe723cff14`

The promoted Linux manifest has SHA-256:

`470fcd1b8b1a1506f26d3dd33d51f55d6896571aacb7329b792d4612f9434781`

The promoted Windows manifest has SHA-256:

`e7e65908eb03eec43e44e2946b395723b164f5701d980aae8ffaaf1006c3d7e4`

The Windows manifest names the promoted Linux manifest as its reviewed plan
seed and retains both preceding manifests as parents. The exact artifact
identities are pinned in the manifests, bootstrap verifier, and artifact-size
policy.

## Evidence

The first candidate attempt failed before publication because its behavior
fixture used paths below the behavior directory instead of the production
`kernel/` identities. The second attempt fixed those paths but exposed a
separate missing behavior-workspace directory. Neither run published a
candidate. Both setup defects now have structural regression coverage.

The first self-consumption run against the copied candidates found a separate
promotion-boundary defect. Source CupidBuild still recognized only the parent
pair recorded by the active seeds, so the Windows runner rejected the new
manifest with `fixed-point provenance differs`. The Linux run was stopped
once the shared cause was known. The validator now recognizes the exact old
and new parent digest/revision pairs independently; it still rejects a digest
from one generation combined with the revision from another. The replacement
candidates are built from an archived, hash-checked copy of the committed
parent seeds.

The corrected paired proof passed. Linux compared 22 C objects and all six
tools, then passed 28 failure, six help, and 35 success groups. Native Windows
compared 23 C objects, three assembly objects, and all six tools, then passed
17 failure, six help, and 22 success groups. Independent hashes also confirmed
that every stage-three tool matched its stage-four counterpart.

The 51,575-byte Linux report has SHA-256
`a27e157398790b29c9c53b1c2957c9ec6b2ed531a94082383e84fc0c9b50e627`.
The 64,681-byte Windows report has SHA-256
`5671a1afc538183477df876e7bb746debfe2ab9f92b3dba45751ec4364a8221d`.

After the exact stage-four tools were copied into the checked directories,
both seeds rebuilt all six initial images byte-for-byte at stage two and kept
the same final-stage behavior totals. The 51,569-byte Linux reproof report has
SHA-256
`fdcfaf61182eb5e7cf1063067a5e7bb69901cd6276746ee49ad2dc391bf31c03`.
The 64,675-byte native Windows report has SHA-256
`c722c702b5ffde37eeb8b135fca36fde641a8e831eee11388d5ddb37f6342994`.

Both checked-seed verifiers pass with six tools. The native and Python
manifest contracts pass, as do the focused provenance, named-source-snapshot,
and artifact-size policy tests.

The first normal-build verification reached the final kernel but found that
the Cupid-built artifact-size contract still recognized only the preceding
seed parents. It rejected the promoted Linux manifest before checking the
size ledger. The contract now follows the same narrow transition rule as
CupidBuild: it accepts either complete parent digest/revision pair and rejects
mixed pairs for the Linux seed and both Windows parent links. The focused
contract and runner suite passes 58 tests with four platform skips.

The corrected normal replay compiled the complete kernel and all 83 Doom
roots, completed both CupidLD links, passed broad CupidDis inspection, and
accepted all 16 exact artifact rows. The final 9,513,992-byte flat kernel has
SHA-256
`8d764164c3bef57ee01062f899922aa1a8344f02d1ce53dfc68b68dae6365d4e`.
The 3,382-byte policy covers 38,330,596 bytes and has SHA-256
`ea0fd0ff6b88ef9ad0a89e0548f3d932c2533126a5d89270b7384d7ff9545706`.
A private four-vCPU `max`/E1000 boot passed the strong SMP runtime gate and ran
`/bin/ls.cc` to normal JIT completion.

## Consequences

Both active CupidBuild images now carry `flatten-kernel`, and every seed tool
includes the same hosted runtime contract. This commit establishes seed
carriage only. The normal kernel-flatten recipe remains under Hostbuild until
the next green commit transfers the Make edge and its audit ownership.

The disk-image, ISO, and Doom profile composite paths remain Python
coordinated. Issue #34 therefore stays open. No GCC, NASM, host linker, or host
object utility enters the supported build. `TempleOS/` remains read-only
reference material.
