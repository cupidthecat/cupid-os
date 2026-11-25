# ADR 0206: Promote CupidObj symbol collision checks

## Status

Accepted on 2026-08-01.

## Context

The installation-source request contract already rejected duplicate paths,
mixed path categories, more than 512 paths, and caller-order changes. A final
review found a separate collision domain after CupidObj converted paths into
linked symbols. Hyphens become underscores, and the bin and browser prefixes
can overlap. Distinct valid paths could therefore name the same wrapped
object.

Revision `957598ac745958cac87fdf61dfe7ada44f2ad96b` checks every complete emitted
symbol in CupidObj and the Python parity oracle. It preserves one deliberate
alias: the exact same BMP path may appear once in the docs list and once in
the home list because both entries use one wrapped object. That revision was
committed and pushed before the seed candidate was built.

The 19-source build plan remains unchanged at SHA-256
`59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc`.

## Decision

Promote the five stage-three tools as one checked cohort. Four images remain
byte-identical to the preceding seed. CupidObj changes as follows:

| Tool | Bytes | SHA-256 | Producer |
| --- | ---: | --- | --- |
| CupidObj | 253,724 | `f78752dc01daf3d2a9dc9265425f9c60639f438d5dcb91a001cf40d7d241ded5` | no |

The manifest keeps the other four image records unchanged and binds the
complete cohort to the pushed revision above. It is 5,440 bytes with SHA-256
`b59f0262119ae40a02cfbf23d8a9d94773b7641772a44fd2553ab1a9fcc3ec33`.
CupidASM, CupidC, and CupidLD remain the producer tools. CupidDis and CupidObj
remain checked outputs.

## Evidence

The transition bootstrap completed in 651.7 seconds. It froze 41 source
inputs with SHA-256
`cc2cc479b9c7e61342ef119be704dc1ff1854d396237b4b649b78c21de2a72f3`.
All 19 C objects, the startup object, and all five tool images matched between
stage two and stage three. Both stages passed five help cases, ten successful
operations, and six useful failures. Only the preceding CupidObj image
differed from stage two. The 15,054-byte transition report has SHA-256
`b2844433e4b682f0d2b1aaef68b6db56896d4b2fb3b2593c3ea9c188d2de2ee3`.

After promotion, `make verify-bootstrap-seed` passed. The focused checked-seed
regression proved that exactly 512 paths succeed, a 513th path preserves the
existing output, caller order remains intact, and the exact shared BMP alias
stays valid. It also rejected bin and browser overlap, manual-name
normalization, and distinct docs and home asset collisions while preserving
the existing output. The test passed in 4.013 seconds.

The post-promotion bootstrap completed in 643.8 seconds. Every checked image
matched stage two. All 19 C objects, the startup object, and all five tool
images then matched between stage two and stage three, and the complete
behavior matrix passed again. The 15,053-byte report has SHA-256
`aa3d29d16ec0a6193367905b2f3adda389c502924f42f9c4f71efad8dfe4afb6`.

All twelve hosted CupidObj tests passed in 3.714 seconds, and all 39 production
tests passed in 25.698 seconds. The complete checked-seed module passed all 37
tests in 755.179 seconds.

The regenerated audit retains 717 active inputs, 449 transforms, 254 feature
requirements, and 25 classified unreachable files. Its active-source digest
remains
`3f297bdac4b05d8a4b644203d93960610c699eba66c5f1459422e86bd6e8af17`.
The 2,546,938-byte JSON has SHA-256
`37cacb564a8e38633f3f67905eb18c64ed5abcc8467395298a4767e9c4aa9cf5`,
and read-only regeneration passes.

The final build-graph audit module passed all 68 tests in 613.397 seconds.

The complete normal Toolchain target passed in 2,863.8 seconds. Its two
checked stages produced the same 19 C objects, startup object, and five tool
images, and the runtime contract passed. The published 18,231-byte manifest
covers 45 inputs and has SHA-256
`27bcebb78404c8013bc56a3e2a0b9d7400cbfa040053863ed55d0d3131baaf33`.

The normal root build passed in 1,452.910 seconds. Its 8,719,780-byte kernel
ELF has SHA-256
`5a7a491a39372697accff9b678054b4bf84e2e68ffc3e882c5ef815d570cee06`,
and its 8,518,280-byte raw kernel has SHA-256
`ecde61e586fb69bf091e3586c7c0a90d65588a9d7aa22ea6cf7d2f48dc341df3`.
The 209,715,200-byte image has SHA-256
`f488f54c023e6d1f7e9883be1f93f705fbdab4b1de3aab8a2b61b86f3863a085`.
A private copy reached the desktop and terminal, compiled `/bin/ls.cc`, and
completed its JIT execution in 54.025 seconds. The 27,839-byte serial log has
SHA-256
`631670b29e91ffe195e343a3cb957e995776b9860efb441f51ffdee4d443d55f`
and contains no panic marker.

## Rejected alternatives

Keeping the preceding seed was rejected because normal installation-table
generation would retain the reviewed symbol collision gap.

Comparing raw paths alone was rejected because linked symbols are the actual
object-file namespace. Two different paths can become equal only after prefix
selection and name normalization.

Rejecting the existing docs and home BMP alias was rejected because both
entries intentionally refer to the same wrapped object. Allowing any broader
alias would hide a real duplicate-symbol failure.

Replacing CupidObj without rebuilding the complete cohort was rejected. The
manifest is one trust unit, and a stage-three fixed point is required even
when only one image changes.

## Consequences

Normal installation-table generation now runs a checked CupidObj that
validates both the raw request and the complete linked-symbol domain before it
opens the destination. The active table bytes do not change because their
inventories already satisfy the stronger contract.

Python still coordinates the staged bootstrap, and Windows still runs the
static i386 tools through WSL. Those host dependencies remain open.
