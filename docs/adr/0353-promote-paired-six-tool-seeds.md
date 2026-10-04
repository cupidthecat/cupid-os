# ADR 0353: Promote the paired six-tool seeds

## Status

Accepted on 2026-08-27.

## Context

ADR 0352 defined Linux and Windows v2 seed contracts that CupidBuild can read
without embedding its own future identity. Commit
`f620e3a973c6fca661c8eeefe443f4b3c669dddc` contains that compatibility
boundary. Fresh fixed points built from the preceding v1 seeds converged on
six Linux tools and six Windows tools, including CupidBuild.

Leaving those images as candidates would keep the active build on the older
five-tool trust unit. Promotion therefore has to move both platforms together,
bind Windows to the exact Linux plan manifest, and extend every active seed
closure before either cohort can be treated as checked input.

## Decision

Promote both checked seed directories to their v2 schemas as one change. Each
manifest lists CupidASM, CupidDis, CupidLD, CupidObj, CupidC, and CupidBuild.
CupidC, CupidASM, and CupidLD remain producers. CupidBuild is a checked
non-producer.

The promoted source revision is
`f620e3a973c6fca661c8eeefe443f4b3c669dddc`. Its 58-input snapshot has
SHA-256
`e94b8976e2389aa43f0085349fc273afb23be92943d023013190161f86364922`.
The Linux build plan has SHA-256
`52dd857bcb74e079e7e2eec45eaa90a0a0838ad2f4e817bebc35c9904efbecbd`,
and the native Windows plan has SHA-256
`f9dce66230a693de9d9d0e60127a4a6c44ea465989f381c995086bfe723cff14`.

The canonical Linux manifest is 6,602 bytes with SHA-256
`6a8fc994d9901165f073dbac190bee3ebb59f8bc9a04993b61f010f58e9bf562`.
The Windows manifest records that exact digest in
`plan_seed_manifest_sha256`. Its own 2,852 bytes have SHA-256
`4d3baa5de2eb8e56835fa80e468e95b7dbab1aada7565d1e27bc2363f8daceb4`.
Both retain the v1 manifest and revision identities as their transition
parents.

The host verifier pins the promoted revision, snapshot, artifact sizes, and
artifact digests. CupidBuild keeps structural checks for the new source
identity fields so its source does not depend on the identity it helps create.
The native artifact-size contract independently checks the exact v1 parents,
both plan digests, the six artifact roles, and the Windows-to-Linux manifest
pairing.

Add CupidBuild to the root, user, Toolchain publication, production-frontier,
artifact-size, and generated-audit seed closures. The artifact-size policy now
covers sixteen exact outputs: the boot and three kernel outputs, six Linux
seed images, and six Windows seed images.

Promotion does not give CupidBuild a normal Make recipe, move a recipe away
from Python, or remove the host coordinator. Those ownership transfers require
separate evidence after the promoted seeds prove that they can consume
themselves.

## Evidence

The promotion candidates came from complete fixed points at the named source
revision. The Linux proof compared 22 C objects, one startup object, and six
tool images, then passed 23 failure, six help, and 29 success cases. Its
51,370-byte report has SHA-256
`8eb8204a9e1f23a22e1effad6d521ab692824b73299cb8e43ea2560a772dcfab`.

The Windows proof compared 23 C objects, three assembly objects, and six tool
images, then passed 12 failure, six help, and 16 success cases. Its 64,496-byte
report has SHA-256
`404e9dcf1bd552f95bdd381688f00c0759476dc116c02ed8abd2d06e0a2915a7`.

The promoted Linux manifest then rebuilt the complete tool cohort from its
checked inputs. All six initial images matched. Stages three and four matched
across 22 C objects, the startup object, and six tool images. The 51,389-byte
reproof report has SHA-256
`d2c51e2c4df168cadd2636d1f87423ebc7423d439e1679184f5849947376ecce`.

The promoted Windows manifest passed the same consumption gate with all six
initial images equal. Stages three and four matched across 23 C objects, three
assembly objects, and six PE images. The 64,515-byte reproof report has
SHA-256
`645b1f6e6181dd44e3169cc9735a9d9ca75f96d7ae50b5e585a3038dae32e169`.

The repository tests check both legacy v1 inputs and the active v2 manifests.
They cover exact promoted identities, missing or extra tools, producer drift,
parent and plan drift, escaped JSON strings, platform import profiles, pairing,
and preservation of prior output on failure. The bootstrap log records the
promoted-seed reproofs and the focused checks run against them.

The normal OS replay passed both CupidLD links and the strict 431-input
CupidDis scan. Its first exact-size check measured a 9,504,760-byte raw kernel
against the provisional 9,504,480-byte policy row and failed closed before
publishing an image. After that row was corrected, CTXT lineage corrections
added 896 bytes. A second exact-size check measured 9,505,656 bytes against the
9,504,760-byte row and also failed closed before publication. A final
spec-review CTXT correction then caused a third fail-closed check. It measured
9,505,572 bytes against the 9,505,656-byte row and stopped before image
publication. After that row was updated, the final 3,382-byte policy passed all
sixteen exact paths, totaling 38,120,452 bytes. The policy has SHA-256
`7f9c1f49d1543112bf6984def1e4ecba6df4fb7a55d2481a85deb7370cf4bfc2`.

The final replay produced these artifacts and force-formatted a fresh normal
image:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernel/kernel.elf.pass1` | 9,601,052 | `2c2b16f018c018ecd55c96868428d9427213aca4344c67b44e2241f05a054463` |
| `kernel/kernel.elf` | 9,732,124 | `342d57566d853ee9a894ec5c6d4e0eafbc6169019950c768f648873ce797fee6` |
| `kernel/kernel.bin` | 9,505,572 | `1d12e0ddc98bcc66fe34157357a80f4a4f6916f0f75b921b5e56eaa792de1977` |
| `cupidos.img` | 209,715,200 | `112a9764bc9c99382d06dabcbcf2cb6e28498d0076ccc1aec787f159b46a8bc3` |

The clean normal image contains the normal `hello.iso` staging. The runtime
harness staged `hello`, `ls`, `cat`, and `catfix.txt` into a deterministic
209,715,200-byte derivative with SHA-256
`816f219305dd0b406d2077913a7f1def08a88efd9591239d0effe44b385cbf10`.
The harness booted private copies of that derivative for three QEMU smokes:

| Guest exercise | Log bytes | Log SHA-256 |
| --- | ---: | --- |
| `hello` | 28,807 | `6543cecfb005f2b775541e279201ee6af4bac4af74bed81a27918f5711392c82` |
| `ls` | 30,158 | `03c01d39b2320be1cc5ec2f92b33d6ffbc62acf50de74f4464ac4fc73f55ea27` |
| `cat` against the hostile fixture | 63,987 | `ca5f6622e317e8ade54370d428271e8f64221b9afd3bf3f238d04760ebdec57a` |

All three passed. Afterward, the clean normal image was restored to SHA-256
`112a9764bc9c99382d06dabcbcf2cb6e28498d0076ccc1aec787f159b46a8bc3`.

## Consequences

The checked trust unit now contains six tools on each platform. A production
or contract target that selects a seed manifest also depends on CupidBuild,
even when that target does not execute it directly. Mixing the promoted
Windows cohort with another Linux plan manifest fails before a guarded
operation can publish.

The v1 validators and explicit legacy fixtures remain available for transition
testing. Python coordination, the ISR and context-switch recipe boundary, and
normal CupidBuild recipe ownership remain open work. No active source changes
suffix in this promotion; the hosted toolchain sources are already `.cc`.
Issue #32 remains open for those recipe ownership transfers.
`TempleOS/` remains read-only reference material.
