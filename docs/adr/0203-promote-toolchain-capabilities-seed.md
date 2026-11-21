# ADR 0203: Promote the toolchain capabilities seed

## Status

Accepted on 2026-08-01.

## Context

The checked i386 Linux seed predated three capability increments already
proven at source head. CupidC could not materialize runtime floating truth.
The shared x86 catalogue lacked `FLDZ`. CupidDis could not mark literal data
ranges, and CupidObj could not generate the three installation-table sources.

The complete capability set was committed and pushed at revision
`03d072fefc6703a53be7bfa4948f6116d238832b` before the candidate was built.
The 19-source build plan remained unchanged at SHA-256
`59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc`.

## Decision

Promote all five stage-three Toolchain images from that revision:

| Tool | Bytes | SHA-256 | Producer |
| --- | ---: | --- | --- |
| CupidASM | 445,616 | `0b12d88f4b2162fe1f07c2774ce5a04acaef31a505b204d712eb37316b9b8071` | yes |
| CupidC | 2,553,244 | `59d90429cdfff1f5d6f8f3b3009f588d06de78c271e2e320dfca5b5e2a58173f` | yes |
| CupidDis | 379,648 | `52922515701dee5f5921e8a0967d57e50c3f8b007627242e18739f803ce25e6e` | no |
| CupidLD | 266,672 | `2bdb6ce6b04678bb89c6bb4f7afac7e152ce6c4a07c4e14e1b3aee0c899008ec` | yes |
| CupidObj | 245,132 | `d39fe725cec9c3c968d9abe33281d34dd9a192f5e3d5f77bb6a9dbc13e935b43` | no |

The manifest keeps CupidASM, CupidC, and CupidLD as producers because stage
two uses them to assemble, compile, and link stage three. CupidDis and
CupidObj remain checked outputs rather than producer tools. The producer
lineage text and stage-three generation rule do not change.

The promoted manifest is 5,440 bytes with SHA-256
`06f69bfeb4777cc5c263dd162bf90cbaf170bfda950aaf86f3f5195d48c86ff3`.
It binds the revision above, the static i386 Linux ABI, the five link orders,
and the unchanged build plan before any seed image runs.

## Evidence

The poisoned-host transition froze 41 source inputs with SHA-256
`074be1d0220c7b6c26a020cfc147246d66189860ac7795bee1a15b7a4dcd485f`.
All 19 C objects, the startup object, and all five tool images matched between
stage two and stage three. Both stages passed five help cases, ten successful
operations, and six useful failures. None of the five input seed images
matched its stage-two replacement, so the complete trust unit changed.

The 15,058-byte transition report has SHA-256
`dcb592b885af6d81e42b1af6657505747c0fa39564e757ec191468c64225a3d9`.
Its `seed_source_revision` names the preceding seed used to begin the run. The
promoted manifest instead names the pushed source revision that produced the
new stage-three images.

After promotion, `make verify-bootstrap-seed` passed. Three focused checked
seed tests passed in 6.750 seconds. They compile, link, and run floating truth
while retaining the atomic rejection; inspect typed raw code and data ranges
while preserving the legacy mode spelling; and generate installation source
through CupidObj. The full checked-seed module passed all 36 tests in 811.387
seconds.

The post-promotion poisoned-host reproof finished in 695.4 seconds. It froze
the same 41-input source snapshot and reproduced all five promoted seed images
at stage two. All 19 C objects, the startup object, and all five tool images
then matched between stage two and stage three. Both stages again passed five
help cases, ten successful operations, and six useful failures. The
15,053-byte report has SHA-256
`855f2c99b0afe541bbc59cbe91b1be513f97ab9bd0649bde3a5bb5df37b165d4`.

Two regression gaps were fixed before this decision was committed. The
fixed-point test now binds the exact 41-input snapshot and requires every
promoted image to match stage two. The floating rejection test starts with an
existing output and requires CupidC to preserve its bytes. The fixed-point
selector passed in 689.814 seconds, and the revised floating selector passed
in 2.394 seconds.

The regenerated active-source audit and its independent check both passed.
They record 717 active inputs, 449 transforms, 254 feature requirements, and
25 classified unreachable files. The active-source digest is
`31a3a757763cd9f5ada368ed6b685b81410440101e7f8bccccb9191304d03249`.
The 2,545,786-byte JSON has SHA-256
`63598adbb291ae2e1d026967dd587b7f96c1a4260e2f6cae85f0fef1b1d72013`.
The 12,136-byte Markdown summary has SHA-256
`7caaf93fddff2e227cc222ab82f857c8310ec66c34d6838d7501bac0eabce0c0`.

## Rejected alternatives

Keeping the preceding seed was rejected because normal checked-seed tests
could not exercise the three new public capabilities.

Replacing only selected executables was rejected because the manifest treats
all five stage-three images as one trust unit. Every image also differed from
its predecessor in this transition.

Changing producer flags was rejected because CupidDis and CupidObj do not
produce stage-three build artifacts. CupidASM, CupidC, and CupidLD still own
that work.

Recording an unpushed worktree state as provenance was rejected. The manifest
names the exact pushed revision used for the candidate build.

## Consequences

The checked seed now carries runtime floating truth, the 591-form x86
catalogue with `FLDZ`, typed CupidDis raw ranges, and CupidObj
installation-source generation. The normal installation-table recipes still
use Python until a separate ownership transfer moves them to CupidObj.

This promotion changes the checked trust root without moving a normal build
owner. Python still coordinates the bootstrap, Windows still runs the static
i386 tools through WSL, and a native Windows fixed point remains open.
